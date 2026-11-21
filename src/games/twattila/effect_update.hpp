/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <cstdint>

#if !defined(_WIN64)
#include <emmintrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <format>
#include <new>
#include <string>
#include <string_view>

#include "../../utils/log.hpp"
#include "./effect_update_sites.hpp"
#include "./worker_pool.hpp"
#endif

// Parallel Effect Update, L1 (Attila 1.6.0, x86): the game's walk over its effects runs the effects' instances on the shared
// worker pool, and the picture is the one the serial walk makes. The sites and their thunks are effect_update_sites.hpp; this
// file is the handler they call.
//
// The rule: an instance's update (a unit) runs on any thread until its first shared touch, an order point (the 27 sites). It
// passes that point only when every unit before it in the serial order is done, and from then on it is the one direct unit:
// its sites take the thunks' fast path, so no add-on code runs there. Units finish in any order; the shared sequence is the
// serial one. The render thread enumerates (the game's own loop calls the walk's call site once per effect: Enqueue copies the
// effect's instances), serves what only it may do, and runs units like a worker.
//
//   key        (segment rank, position): the instances 0..n-1 of a segment, then the late units n.., then its sweep.
//   frontier   the smallest key not done, one 64-bit word (segment id << 32 | position): a unit is the oldest when it is there.
//   claims     one unit at a time, by a byte of the unit (the cursor over the units is only where to look); the late units first.
//   waits      a unit waiting for its turn (or for enumeration to end) runs the frontier unit itself when nobody has it (it never
//              waits, so waits nest once), spins ~20 us, then waits on an epoch word with a timeout; whatever ends a wait bumps
//              the epoch when someone waits.
//   render     only the render thread runs the sweeps (the game's destructors run on one thread in serial order), the rigid add
//              and the random texture fill (forwarded to it through a one-slot mailbox), and it ends the walk.
//   failure    an allocation that fails in a round: enumeration done, the units drained, Join, the walk's remaining effects go
//              the game's own way (the handler answers 0 for them).
//   late       a child appended to a later effect is a late unit of that effect's segment; an effect type registered in the walk
//              whose slot comes after the current effect's is a late segment (all its instances late units) in the gap of the
//              effects' ranks where its slot puts it. A site that changes what the render thread reads while it enumerates (the
//              walk's vectors and table) waits for the enumeration to end, also when its unit is direct already (those sites
//              and the forwarded ones have no fast path in their thunks).
//
// L2, Parallel Effect Spawns (its own row, on top of L1): the three sites of the particle spawn path stop being order points. A unit asks "is there room" (answered true
// while it runs ahead, exactly once it is direct), "spawn" (the 48-byte record is staged in the unit's chunks) and, on the create path only, the three-step random draw (deferred:
// the draw's three outputs are a quiet NaN that carries the draw's number, and the value lands in the staged record, the only place the census says it goes). The units are
// committed in the serial order when a unit becomes direct (and at the walk's end): each unit's deferred draws are made then by the game's own function, so the random state moves as
// in serial, and the staged records are merged into the game's spawn buffer in the serial order at the walk's end through the pipeline's own spawn. It matches the serial walk
// except where the budget (the room in the buffer) fills while units run ahead: those frames are counted (an overflow) and the merge drops what does not fit.
//
// The safety checks (a word the game changed between two direct units, a render thread hit outside a unit, a walk call or end out
// of place, a late overflow, a failed round, a hang) are always on and stored; Update logs each kind once per site and session at
// the next present (no log call inside game code). Every other count (the serial reasons, the sites a thread without a unit hit,
// the waits) is taken only in a worker pool trace window, and printed at the window's presents.
namespace twattila::effect_update {

#if !defined(_WIN64)

namespace internal {

constexpr uint32_t NONE = 0xFFFFFFFF;
constexpr uint32_t BLOCK_SIZE = 4096;
constexpr uint32_t MAX_BLOCKS = 256;
// A normal segment's rank is its index << 12; a late segment takes the middle of the gap it goes into (twelve of them nest).
constexpr uint32_t RANK_STEP = 1u << 12;
constexpr uint32_t RANK_LIMIT = 1u << 19;  // more effects than that in a round: the round does not run
constexpr uint32_t MIN_ROUND_UNITS = 256;
constexpr uint32_t BUCKETS = 4096;
constexpr uint32_t SPIN_MICROSECONDS = 20;
constexpr uint32_t WAIT_TIMEOUT_MILLISECONDS = 50;
// A round too small to be worth it is tried again after this many presents.
constexpr uint32_t SMALL_ROUND_RETRY_PRESENTS = 120;

// The game's addresses (RVAs): the instance's update, the per-effect function's render switch, the random state.
constexpr uint32_t INSTANCE_UPDATE_RVA = 0x1893470;
constexpr uint32_t RENDER_SWITCH_GETTER_RVA = 0xC0130;
constexpr uint32_t RENDER_SWITCH_RVA = 0x2A74D68;
constexpr uint32_t RANDOM_STATE_RVA = 0x1DA7408;
// The node's table of effects (node + 0xC8: a hash table of 16-byte slots, the effect at +0xC) is found from the walk's second
// argument (node + 0x8C); an effect's vector of instances has its count at +0x18 and the 8-byte shared_ptr entries at +0x1C.
constexpr uint32_t NODE_ARGUMENT_OFFSET = 0x8C;
constexpr uint32_t TABLE_OFFSET = 0xC8;
constexpr uint32_t SLOT_SIZE = 16;
constexpr uint32_t SLOT_EFFECT_OFFSET = 0xC;
constexpr uint32_t EFFECT_COUNT_OFFSET = 0x18;
constexpr uint32_t EFFECT_ENTRIES_OFFSET = 0x1C;
constexpr uint32_t APPEND_VECTOR_OFFSET = 0x14;
// L2: the node's pipeline (node + 0x180) and its two vtables, the three-step random draw and its create-path caller's return address, the spawn record (48 bytes at +0x2C of the
// particle the game's spawn takes) and the spawn's vtable slot. The CPU pipeline's buffer is at +0x10 (count +0x28, capacity +8, simulation room from two words at +0x14 / +0x18), the
// GPU's at +0x14 (count +0x14, capacity +0xC).
constexpr uint32_t PIPELINE_OFFSET = 0x180;
constexpr uint32_t CPU_PIPELINE_VTABLE_RVA = 0x1CC91D0;
constexpr uint32_t GPU_PIPELINE_VTABLE_RVA = 0x1CD1894;
constexpr uint32_t DRAW_RVA = 0x190B780;
constexpr uint32_t CREATE_DRAW_RETURN_RVA = 0x181BECB;
constexpr uint32_t SPAWN_RECORD_OFFSET = 0x2C;
constexpr uint32_t SPAWN_SLOT_OFFSET = 0x1C;
constexpr uint32_t RECORD_DWORDS = 12;
constexpr uint32_t RECORD_DRAW_INDEX = 1;  // the dword (record + 4) the create path's first drawn value lands in
constexpr uint32_t CHUNK_RECORDS = 16;
constexpr uint32_t MIN_SPAWN_BUDGET = 512;  // a round with less room than this does not stage (the guard)
// A deferred draw's outputs: a quiet NaN whose low 22 bits are the draw's number in its unit (from 1).
constexpr uint32_t TAG_BASE = 0x7FC00000;
constexpr uint32_t TAG_MASK = 0xFFC00000;
constexpr uint32_t MAX_DRAWS = 0x3FFFFF;

// The game's functions the scheduler calls. Update fills them from the image; a test sets what it replaces.
struct Entries {
  void(__thiscall* update_instance)(void* instance, void* a1, void* a2, void* a3) = nullptr;
  uint8_t(__thiscall* render_switch)(void* object) = nullptr;
  void* render_switch_object = nullptr;
  void(__thiscall* sweep)(uint32_t effect) = nullptr;
  const uint32_t* random_state = nullptr;
  uint32_t(__cdecl* draw)(float* out) = nullptr;                          // the three-step random draw (the game's own function)
  uint8_t(__thiscall* spawn)(uint32_t pipeline, uint32_t particle) = nullptr;  // the pipeline's spawn; null: its vtable's slot 7
};
static Entries entries;

enum class SerialReason : uint8_t { SESSION_OFF, SMALL, NEW_NODE, NO_WORKERS, START_REFUSED, ALLOCATION, COUNT };
constexpr std::array<std::string_view, static_cast<size_t>(SerialReason::COUNT)> SERIAL_REASON_NAMES = {
    "session off", "too few units last round", "new node", "no workers", "start refused", "allocation"};

// Counters: the first block (up to FOREIGN_HITS) only in a trace window, the second (from HANGS) always: the safety checks.
enum class Counter : uint8_t {
  UNITS,
  EFFECTS,
  LATE_UNITS,
  LATE_SEGMENTS,
  WAITS,
  NESTED_RUNS,
  WAKE_TIMEOUTS,
  FORWARDED,
  ORDER_POINTS,
  ROUNDS,
  STAGED_SPAWNS,
  DEFERRED_DRAWS,
  SPAWN_ROUNDS,
  GUARD_ROUNDS,
  FOREIGN_HITS,
  HANGS,
  WATCHED_MISMATCHES,
  CENSUS_ERRORS,
  LATE_OVERFLOWS,
  FAILED_ROUNDS,
  SPAWN_OVERFLOWS,
  TAG_LEAKS,
  STAGE_FULLS,
  COUNT
};
constexpr std::array<std::string_view, static_cast<size_t>(Counter::COUNT)> COUNTER_NAMES = {
    "units",  "effects", "late units", "late segments", "waits",     "nested runs",    "wake timeouts", "forwarded calls",
    "order points", "rounds", "merged spawns", "deferred draws", "spawn rounds", "guard rounds", "foreign hits", "hangs", "watched mismatches", "census errors",
    "late overflows", "failed rounds", "spawn overflows", "tag leaks", "stage full"};

enum class EventKind : uint8_t { WATCHED_MISMATCH, CENSUS_ERROR, LATE_OVERFLOW, HANG, FAILED_ROUND, SPAWN_OVERFLOW, TAG_LEAK, STAGE_FULL };
constexpr std::array<std::string_view, 8> EVENT_NAMES = {"watched word changed", "site hit where the census has none", "late overflow", "hang",
                                                         "round failed", "spawn budget overflow", "deferred draw outside its record", "spawn staging full"};
struct EventRecord {
  EventKind kind = EventKind::WATCHED_MISMATCH;
  uint32_t site = 0;
  uint32_t thread = 0;
  uint32_t value = 0;
};

struct alignas(64) Unit {
  uint32_t instance = 0;
  uint32_t segment = 0;
  uint32_t position = 0;
  uint32_t next_late = NONE;  // the segment's next late unit (an index of the units)
  // L2 (touched by the thread that runs the unit, then by the one committer): the unit's staged records in its chunks, its deferred draws, how many records it had staged at its last
  // answer "there is room" given while it ran ahead, the next unit with staged records in the serial order.
  Unit* next_staged = nullptr;
  uint32_t stage_head = NONE;
  uint32_t stage_tail = NONE;
  uint32_t staged = 0;
  uint32_t draws = 0;
  uint32_t ahead_staged = 0;
  std::atomic<uint8_t> done = 0;
  std::atomic<uint8_t> claimed = 0;
  bool late = false;
  bool direct = false;  // touched only by the thread that runs it
  bool ahead = false;   // L2: answered "there is room" while it ran ahead
  bool pseudo = false;  // a sweep's stand-in (no instance, no staging)

  void Reset(uint32_t instance_, uint32_t segment_, uint32_t position_, bool late_) {
    instance = instance_;
    segment = segment_;
    position = position_;
    next_late = NONE;
    next_staged = nullptr;
    stage_head = NONE;
    stage_tail = NONE;
    staged = 0;
    draws = 0;
    ahead_staged = 0;
    done.store(0, std::memory_order_relaxed);
    claimed.store(0, std::memory_order_relaxed);
    late = late_;
    direct = false;
    ahead = false;
    pseudo = false;
  }
};

// A unit's staged spawn records, 16 to a chunk (the spawn's 48 bytes each), chained.
struct Chunk {
  uint32_t next;
  uint32_t count;
  uint32_t records[CHUNK_RECORDS][RECORD_DWORDS];
};

struct Segment {
  uint32_t id = 0;
  uint32_t effect = 0;
  uint32_t rank = 0;
  uint32_t first_unit = 0;
  uint32_t units = 0;  // n_s: the instances the walk found; a position from here on is a late unit
  uint32_t next = NONE;
  uint32_t previous = NONE;
  uint32_t map_next = NONE;
  uint32_t late_head = NONE;
  uint32_t late_tail = NONE;
  std::atomic<uint32_t> late_count = 0;
  std::atomic<uint8_t> sweep = 0;  // 0 waiting, 1 claimed, 2 done
};

#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
// Test controls (test/tw_engine_code): an allocation that fails (the Nth from now; 0 never) and rules left out that the tests
// must see go wrong.
struct Controls {
  uint32_t fail_allocation_at = 0;
  uint32_t allocations = 0;
  bool no_order_points = false;      // every unit site returns at once
  bool sweep_on_worker = false;      // a sweep runs on whichever thread reaches it
  bool rigid_on_worker = false;      // the forwarded calls run on the unit's thread
  bool no_enumeration_wait = false;  // the sites that wait for enumeration to end do not
  bool no_epoch = false;             // nothing bumps the epoch or wakes
  bool late_child_off = false;       // an append into a later effect makes no late unit
  bool sweep_early = false;          // the render thread sweeps an effect as soon as enumeration is done, before its units are
  bool wait_on_later = false;        // a unit waits for the frontier to pass it (never: until the hang detector has fired once)
  bool commit_reversed = false;      // L2: the units' commits (their deferred draws) run in the reverse of the serial order
  bool merge_by_completion = false;  // L2: the staged records are merged in the reverse of the serial order
  bool overflow_ignored = false;     // L2: a commit does not check the budget
  bool defer_any_caller = false;     // L2: the random draw is deferred for every caller, not only the create path's
};
static Controls controls;
#endif

// Append-only storage in blocks that never move: a reader holding a pointer is safe while a block is added. The blocks are never freed
// (at most MAX_BLOCKS of each kind, the size the biggest round needed; the process owns them for its life).
template <typename T>
struct Blocks {
  std::array<T*, MAX_BLOCKS> blocks = {};
  uint32_t capacity = 0;

  T& operator[](uint32_t index) const { return blocks[index / BLOCK_SIZE][index % BLOCK_SIZE]; }

  bool Reserve(uint32_t count) {
    while (capacity < count) {
      const uint32_t block = capacity / BLOCK_SIZE;
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
      if (controls.fail_allocation_at != 0 && ++controls.allocations == controls.fail_allocation_at) return false;
#endif
      if (block >= MAX_BLOCKS) return false;
      T* const memory = new (std::nothrow) T[BLOCK_SIZE];
      if (memory == nullptr) return false;
      blocks[block] = memory;
      capacity += BLOCK_SIZE;
    }
    return true;
  }
};

struct Watched {
  const uint32_t* address = nullptr;
  std::string_view name;
  uint32_t value = 0;
};

// The one-slot mailbox of the calls only the render thread makes (the rigid add, the random texture fill).
struct Mailbox {
  std::atomic<uint32_t> state = 0;  // 0 empty, 1 posted, 2 done
  uint32_t site = 0;
  uint32_t ecx = 0;
  uint32_t first = 0;
  uint32_t second = 0;
  uint32_t result = 0;
};

struct alignas(64) Round {
  bool open = false;
  bool tracing = false;
  bool type_added = false;
  bool failed = false;                    // an allocation failed or the census broke: the walk's rest goes the game's way (no late segment)
  bool l2 = false;                        // the round stages the spawns
  std::atomic<bool> violation = false;    // a safety check failed: B1 is off for the session after the walk (the render thread and units write it)
  DWORD owner = 0;
  uint32_t node = 0;
  uint32_t arguments[3] = {};
  uint32_t enqueued_units = 0;
  uint32_t segment_total = 0;
  uint32_t tail = NONE;  // the last segment in the order
  uint32_t failed_effect = 0;  // the effect whose walk call failed (the game's own loop goes on from it); 0: none or unknown
  uint32_t last_site = 0;
  std::atomic<uint64_t> last_done_ticks = 0;
  std::atomic<uint32_t> segment_count = 0;    // the segments that exist (normal and late)
  std::atomic<uint32_t> published_units = 0;  // the units that exist (normal and late)
  std::atomic<uint32_t> cursor = 0;
  std::atomic<uint32_t> late_unclaimed = 0;
  std::atomic<bool> enumeration_done = false;
  alignas(64) std::atomic<uint64_t> frontier = 0;
  alignas(64) std::atomic<uint32_t> epoch = 0;
  std::atomic<uint32_t> waiters = 0;
  std::array<uint32_t, BUCKETS> buckets = {};
  Mailbox mailbox;
  std::array<Watched, 16> watched = {};
  uint32_t watched_count = 0;
  // L2. The pipeline, the game's spawn count word and its value at the round's start, the room for has_free answers (query_limit; a create needs it, so no unit's spawns pass it)
  // and for a direct spawn that has no has_free before it (cap_limit, the buffer's), the records committed so far, the commit cursor (a segment id << 32 | position), the units with staged records in the serial order, the next chunk.
  uint32_t pipeline = 0;
  const uint32_t* count_at = nullptr;
  uint32_t count0 = 0;
  uint32_t query_limit = 0;
  uint32_t cap_limit = 0;
  uint32_t spawn_total = 0;
  uint64_t commit_at = 0;
  Unit* merge_head = nullptr;
  Unit* merge_tail = nullptr;
  std::atomic<uint32_t> chunk_next = 0;
  std::atomic<bool> spawn_violation = false;  // a staging check failed: L2 is off for the session after the walk
  std::array<uint32_t, 3> pools = {};         // the slot pools seen at SLOT_TAKE / SLOT_RETURN this round (their words are watched)
  uint32_t pool_count = 0;
};

struct ThreadState {
  Unit* current = nullptr;
  bool serving = false;  // the render thread is making a forwarded call: its site answers 0
  uint32_t stack_high = 0;
  uint32_t stack_size = 0;
};

static Round current_round;
static Blocks<Unit> units;
static Blocks<Segment> segments;
static Blocks<Chunk> chunks;
static thread_local ThreadState this_thread;
static std::array<std::atomic<uint32_t>, static_cast<size_t>(Counter::COUNT)> counters = {};
static std::array<std::atomic<uint32_t>, static_cast<size_t>(SerialReason::COUNT)> serial_rounds = {};
static std::array<std::atomic<uint32_t>, SITE_COUNT> order_point_hits = {};
static std::array<std::atomic<uint32_t>, SITE_COUNT> first_point_hits = {};
static std::array<EventRecord, 64> events = {};
static std::atomic<uint32_t> event_count = 0;
static std::array<std::atomic<uint32_t>, EVENT_NAMES.size()> events_said = {};  // by kind: the sites already stored (once per site and session)
static std::atomic<uint64_t> longest_wait_ticks = 0;
static std::atomic<uint64_t> handover_ticks = 0;
static std::atomic<uint32_t> handovers = 0;
static std::atomic<uint64_t> forward_wait_ticks = 0;

static bool row_on = false;
static bool session_off = false;
static bool off_said = false;
static uint32_t last_round_units = MIN_ROUND_UNITS;
static uint32_t last_node = 0;
static uint32_t small_round_presents = 0;
static bool spawns_row = false;
static bool spawns_off = false;
static uint32_t last_round_spawned = 0;
static uint32_t last_round_chunks = 0;

// A counter: the trace counters only in a trace window, the safety ones (from HANGS) always.
inline void Count(Counter counter, uint32_t amount = 1) {
  if (counter >= Counter::HANGS || current_round.tracing) {
    counters[static_cast<size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
  }
}

inline void Note(EventKind kind, uint32_t site, uint32_t value) {
  const uint32_t site_bit = 1u << site;
  if ((events_said[static_cast<size_t>(kind)].fetch_or(site_bit, std::memory_order_relaxed) & site_bit) != 0) return;
  const uint32_t index = event_count.fetch_add(1, std::memory_order_relaxed);
  if (index < events.size()) {
    events[index] = {.kind = kind, .site = site, .thread = GetCurrentThreadId(), .value = value};
  }
}

inline uint32_t Read(uint32_t address) { return *reinterpret_cast<const uint32_t*>(address); }

constexpr uint64_t Pack(uint32_t segment, uint32_t position) { return (static_cast<uint64_t>(segment) << 32) | position; }

inline Unit* LateUnit(const Segment& segment, uint32_t index) {
  uint32_t at = segment.late_head;
  for (; index != 0; --index) at = units[at].next_late;
  return &units[at];
}

// The address-wait calls, found in kernelbase so that the add-on needs no import library for them (an old system without them falls
// back to short sleeps).
struct AddressWait {
  BOOL(WINAPI* wait)(volatile VOID* address, PVOID compare, SIZE_T size, DWORD milliseconds) = nullptr;
  VOID(WINAPI* wake_all)(PVOID address) = nullptr;
};
static const AddressWait address_wait = [] {
  const HMODULE module = GetModuleHandleW(L"kernelbase.dll");
  return AddressWait{.wait = reinterpret_cast<decltype(AddressWait::wait)>(GetProcAddress(module, "WaitOnAddress")),
                     .wake_all = reinterpret_cast<decltype(AddressWait::wake_all)>(GetProcAddress(module, "WakeByAddressAll"))};
}();

// Wakes the waiters after anything that ends a wait: the epoch moves only when someone waits (the waiter registers, then reads
// the epoch and checks again, so a bump it misses is a state it sees).
inline void Wake() {
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  if (controls.no_epoch) return;
#endif
  if (current_round.waiters.load(std::memory_order_seq_cst) != 0) {
    current_round.epoch.fetch_add(1, std::memory_order_seq_cst);
    if (address_wait.wake_all != nullptr) address_wait.wake_all(&current_round.epoch);
  }
}

// Moves the frontier over every unit that is done, every sweep that ran and into the next segment; the end is segment NONE.
inline void AdvanceFrontier() {
  Round& round = current_round;
  for (;;) {
    uint64_t at = round.frontier.load(std::memory_order_acquire);
    const uint32_t segment_id = static_cast<uint32_t>(at >> 32);
    const uint32_t position = static_cast<uint32_t>(at);
    if (segment_id == NONE) return;
    if (segment_id >= round.segment_count.load(std::memory_order_acquire)) {
      if (round.enumeration_done.load(std::memory_order_seq_cst) && segment_id >= round.segment_count.load(std::memory_order_acquire)) {
        round.frontier.compare_exchange_strong(at, Pack(NONE, 0));
      }
      return;
    }
    const Segment& segment = segments[segment_id];
    const uint32_t late = segment.late_count.load(std::memory_order_acquire);
    uint64_t moved = 0;
    if (position < segment.units) {
      if (units[segment.first_unit + position].done.load(std::memory_order_acquire) == 0) return;
      moved = Pack(segment_id, position + 1);
    } else if (position - segment.units < late) {
      if (LateUnit(segment, position - segment.units)->done.load(std::memory_order_acquire) == 0) return;
      moved = Pack(segment_id, position + 1);
    } else {
      if (segment.sweep.load(std::memory_order_acquire) != 2) return;
      if (segment.next != NONE) {
        moved = Pack(segment.next, 0);
      } else if (round.enumeration_done.load(std::memory_order_seq_cst)) {
        moved = Pack(NONE, 0);
      } else {
        return;
      }
    }
    round.frontier.compare_exchange_strong(at, moved);
  }
}

// One claim: the byte of the unit, whichever way the unit was found.
inline bool Claim(Unit* unit) {
  uint8_t expected = 0;
  if (!unit->claimed.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return false;
  if (unit->late) current_round.late_unclaimed.fetch_sub(1, std::memory_order_relaxed);
  return true;
}

// The next unit for a thread with none: the smallest late unit first (a unit whose key is behind a late one may be waiting for
// it), then the cursor's.
inline Unit* ClaimNext() {
  Round& round = current_round;
  if (round.late_unclaimed.load(std::memory_order_acquire) != 0) {
    Unit* best = nullptr;
    uint64_t best_key = ~0ull;
    const uint32_t total = round.published_units.load(std::memory_order_acquire);
    // The late units come after the walk's own (a late unit is made only once enumeration is done).
    for (uint32_t index = round.enqueued_units; index < total; ++index) {
      Unit& unit = units[index];
      if (!unit.late || unit.claimed.load(std::memory_order_acquire) != 0) continue;
      const uint64_t key = Pack(segments[unit.segment].rank, unit.position);
      if (key < best_key) {
        best = &unit;
        best_key = key;
      }
    }
    if (best != nullptr && Claim(best)) return best;
  }
  for (uint32_t at = round.cursor.load(std::memory_order_relaxed); at < round.published_units.load(std::memory_order_acquire);) {
    if (round.cursor.compare_exchange_weak(at, at + 1, std::memory_order_acq_rel)) {
      if (Claim(&units[at])) return &units[at];
      at = round.cursor.load(std::memory_order_relaxed);
    }
  }
  return nullptr;
}

// The watched words as they are now: after a direct unit or a sweep ran, and after the commits moved the random state.
inline void RefreshWatched() {
  Round& round = current_round;
  for (uint32_t index = 0; index < round.watched_count; ++index) {
    round.watched[index].value = *round.watched[index].address;
  }
}

// L2: a unit's deferred draws, in their order, by the game's own function (so the random state moves as in serial): each value goes into the staged record that holds the draw's
// tag. A draw whose record was never staged (the create failed after it) is spent all the same, as in serial.
inline void ResolveDraws(Unit* unit) {
  if (unit->draws == 0) return;
  ThreadState& state = this_thread;
  const bool was_serving = state.serving;
  state.serving = true;  // the draw's site answers 0: the game's function runs
  uint32_t chunk_index = unit->stage_head;
  uint32_t record_index = 0;
  // The record that holds `tag`: from the last hit on, then from the unit's first record.
  const auto find = [&](uint32_t tag) {
    for (int pass = 0; pass < 2; ++pass) {
      uint32_t at = (pass == 0 ? chunk_index : unit->stage_head);
      uint32_t from = (pass == 0 ? record_index : 0);
      for (; at != NONE; at = chunks[at].next, from = 0) {
        for (uint32_t record = from; record < chunks[at].count; ++record) {
          if (chunks[at].records[record][RECORD_DRAW_INDEX] == tag) {
            chunk_index = at;
            record_index = record + 1;
            return &chunks[at].records[record][RECORD_DRAW_INDEX];
          }
        }
      }
    }
    return static_cast<uint32_t*>(nullptr);
  };
  for (uint32_t draw = 1; draw <= unit->draws; ++draw) {
    std::array<float, 3> out = {};
    entries.draw(out.data());
    if (uint32_t* const slot = find(TAG_BASE | draw)) *slot = std::bit_cast<uint32_t>(out[0]);
  }
  unit->draws = 0;
  state.serving = was_serving;
}

// L2: a unit is committed once every unit before it has been: its draws are made, its records join the merge list, and the budget is checked (an answer "there is room" it gave
// while it ran ahead, and its records, against what the units before it left).
inline void CommitUnit(Unit* unit) {
  Round& round = current_round;
  ResolveDraws(unit);
  const uint32_t before = round.spawn_total;
  bool overflow = (unit->ahead && before + unit->ahead_staged >= round.query_limit) || before + unit->staged > round.query_limit;
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  if (controls.overflow_ignored) overflow = false;
#endif
  if (overflow) {
    Count(Counter::SPAWN_OVERFLOWS);
    Note(EventKind::SPAWN_OVERFLOW, round.last_site, before + unit->staged);
  }
  // What serial would not have created (a create needs the room: spawns are never past query_limit) is not kept: the merge takes `staged` records of each unit.
  unit->staged = std::min(unit->staged, round.query_limit > before ? round.query_limit - before : 0);
  round.spawn_total = before + unit->staged;
  if (unit->staged == 0) return;
  if (round.merge_tail == nullptr) {
    round.merge_head = unit;
  } else {
    round.merge_tail->next_staged = unit;
  }
  round.merge_tail = unit;
}

// L2: commits every unit that is done from the commit cursor on, in the serial order, up to the first that is not. Called where a unit becomes direct (everything before it is done),
// before a sweep runs and at the walk's end: one thread at a time by construction (the direct unit, the sweep, or the finisher once all is done).
inline void CommitDone() {
  Round& round = current_round;
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  std::vector<Unit*> held;
#endif
  for (;;) {
    const uint32_t segment_id = static_cast<uint32_t>(round.commit_at >> 32);
    const uint32_t position = static_cast<uint32_t>(round.commit_at);
    if (segment_id >= round.segment_count.load(std::memory_order_acquire)) break;
    const Segment& segment = segments[segment_id];
    const uint32_t late = segment.late_count.load(std::memory_order_acquire);
    Unit* unit = nullptr;
    if (position < segment.units) {
      unit = &units[segment.first_unit + position];
    } else if (position - segment.units < late) {
      unit = LateUnit(segment, position - segment.units);
    } else if (segment.next != NONE) {
      round.commit_at = Pack(segment.next, 0);
      continue;
    } else {
      break;
    }
    if (unit->done.load(std::memory_order_acquire) == 0) break;
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
    if (controls.commit_reversed) {
      held.push_back(unit);
    } else
#endif
    {
      CommitUnit(unit);
    }
    round.commit_at = Pack(segment_id, position + 1);
  }
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  for (auto at = held.rbegin(); at != held.rend(); ++at) CommitUnit(*at);
#endif
}

// L2: the staged records go into the game's spawn buffer through the pipeline's own spawn, in the serial order (what does not fit is dropped, as the game's spawn refuses it).
inline void Merge() {
  Round& round = current_round;
  const auto spawn = (entries.spawn != nullptr ? entries.spawn : reinterpret_cast<decltype(entries.spawn)>(Read(Read(round.pipeline) + SPAWN_SLOT_OFFSET)));
  uint32_t merged = 0;
  const auto merge_unit = [&](const Unit* unit) {
    uint32_t left = unit->staged;
    for (uint32_t at = unit->stage_head; at != NONE && left != 0; at = chunks[at].next) {
      for (uint32_t record = 0; record < chunks[at].count && left != 0; ++record, --left) {
        merged += (spawn(round.pipeline, Address(chunks[at].records[record]) - SPAWN_RECORD_OFFSET) != 0 ? 1 : 0);
      }
    }
  };
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  if (controls.merge_by_completion) {
    std::vector<const Unit*> all;
    for (const Unit* unit = round.merge_head; unit != nullptr; unit = unit->next_staged) all.push_back(unit);
    for (auto at = all.rbegin(); at != all.rend(); ++at) merge_unit(*at);
    Count(Counter::STAGED_SPAWNS, merged);
    return;
  }
#endif
  for (const Unit* unit = round.merge_head; unit != nullptr; unit = unit->next_staged) merge_unit(unit);
  Count(Counter::STAGED_SPAWNS, merged);
}

inline void ThreadStackLimits() {
  if (this_thread.stack_high != 0) return;
  ULONG_PTR low = 0;
  ULONG_PTR high = 0;
  GetCurrentThreadStackLimits(&low, &high);
  this_thread.stack_high = static_cast<uint32_t>(high);
  this_thread.stack_size = static_cast<uint32_t>(high - low);
}

// The unit runs: the game's instance update on this thread with the walk's three arguments. A unit that became direct clears the
// direct stack before it is done (a later unit on this thread must not ride the fast path), records the watched words, and
// hands over.
inline void RunUnit(Unit* unit) {
  Round& round = current_round;
  ThreadState& state = this_thread;
  Unit* const previous = state.current;
  state.current = unit;
  Count(Counter::UNITS);
  if (previous != nullptr) Count(Counter::NESTED_RUNS);
  entries.update_instance(reinterpret_cast<void*>(unit->instance), reinterpret_cast<void*>(round.arguments[0]),
                          reinterpret_cast<void*>(round.arguments[1]), reinterpret_cast<void*>(round.arguments[2]));
  if (unit->direct) {
    RefreshWatched();
    data->direct_high.store(0, std::memory_order_release);
    unit->direct = false;
    if (round.tracing) round.last_done_ticks.store(worker_pool::Qpc(), std::memory_order_relaxed);
  }
  unit->done.store(1, std::memory_order_seq_cst);
  state.current = previous;
  AdvanceFrontier();
  Wake();
}

// What a wait may do besides wait, the same for the render thread and a worker: run the frontier unit when nobody has it, and for
// the render thread the mailbox and the sweep at the frontier. True: it ran something. WaitUntil and Serve call each other (a unit
// run in a wait waits itself, once), so Serve is declared first.
inline bool Serve(bool render);
inline void CheckWatched();

// Waits until `ready()`: serves, spins, then waits on the epoch (50 ms at most; a frontier that has not moved after one is a hang).
template <typename Ready>
inline void WaitUntil(Ready&& ready, bool render) {
  Round& round = current_round;
  const uint64_t start = worker_pool::Qpc();
  const uint64_t spin_ticks = worker_pool::qpc_frequency * SPIN_MICROSECONDS / 1000000;
  bool blocked = false;
  while (!ready()) {
    if (Serve(render)) continue;
    if (worker_pool::Qpc() - start < spin_ticks) {
      _mm_pause();
      continue;
    }
    blocked = true;
    round.waiters.fetch_add(1, std::memory_order_seq_cst);
    uint32_t seen = round.epoch.load(std::memory_order_seq_cst);
    if (ready() || Serve(render)) {
      round.waiters.fetch_sub(1, std::memory_order_seq_cst);
      continue;
    }
    const uint64_t frontier_before = round.frontier.load(std::memory_order_acquire);
    if (address_wait.wait == nullptr) {
      Sleep(1);
      round.waiters.fetch_sub(1, std::memory_order_seq_cst);
      continue;
    }
    const BOOL woken = address_wait.wait(&round.epoch, &seen, sizeof(seen), WAIT_TIMEOUT_MILLISECONDS);
    round.waiters.fetch_sub(1, std::memory_order_seq_cst);
    if (woken == FALSE && GetLastError() == ERROR_TIMEOUT) {
      Count(Counter::WAKE_TIMEOUTS);
      AdvanceFrontier();
      if (round.frontier.load(std::memory_order_acquire) == frontier_before) {
        Count(Counter::HANGS);
        Note(EventKind::HANG, round.last_site, static_cast<uint32_t>(frontier_before));
      }
    }
  }
  if (blocked && round.tracing) {
    Count(Counter::WAITS);
    const uint64_t ticks = worker_pool::Qpc() - start;
    uint64_t longest = longest_wait_ticks.load(std::memory_order_relaxed);
    while (ticks > longest && !longest_wait_ticks.compare_exchange_weak(longest, ticks, std::memory_order_relaxed)) {
    }
  }
}

inline bool Serve(bool render) {
  Round& round = current_round;
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  const bool sweeps = render || controls.sweep_on_worker;
#else
  const bool sweeps = render;
#endif
  if (render && round.mailbox.state.load(std::memory_order_acquire) == 1) {
    // The forwarded call: the render thread is the direct thread while it runs (a site inside takes the fast path), then the
    // worker's unit is again.
    Mailbox& mail = round.mailbox;
    const uint32_t saved = data->direct_high.load(std::memory_order_relaxed);
    ThreadStackLimits();
    data->direct_high.store(this_thread.stack_high, std::memory_order_release);
    const uint32_t entry = Address(base) + SITES[mail.site].rva;
    this_thread.serving = true;
    if (static_cast<Site>(mail.site) == Site::RIGID_ADD) {
      mail.result = reinterpret_cast<uint32_t(__thiscall*)(uint32_t, uint32_t)>(entry)(mail.ecx, mail.first);
    } else {
      mail.result = reinterpret_cast<uint32_t(__thiscall*)(uint32_t, uint32_t, uint32_t)>(entry)(mail.ecx, mail.first, mail.second);
    }
    this_thread.serving = false;
    data->direct_high.store(saved, std::memory_order_release);
    mail.state.store(2, std::memory_order_seq_cst);
    Wake();
    return true;
  }
  // The unit the frontier points at, or the segment whose sweep it is (the render thread's to run), after advancing it.
  Unit* unit = nullptr;
  Segment* sweep = nullptr;
  AdvanceFrontier();
  const uint64_t at = round.frontier.load(std::memory_order_acquire);
  const uint32_t segment_id = static_cast<uint32_t>(at >> 32);
  const uint32_t position = static_cast<uint32_t>(at);
  if (segment_id != NONE && segment_id < round.segment_count.load(std::memory_order_acquire)) {
    Segment& segment = segments[segment_id];
    const uint32_t late = segment.late_count.load(std::memory_order_acquire);
    if (position < segment.units) {
      unit = &units[segment.first_unit + position];
    } else if (position - segment.units < late) {
      unit = LateUnit(segment, position - segment.units);
    } else {
      sweep = &segment;
    }
  }
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  if (controls.sweep_early && sweeps && round.enumeration_done.load(std::memory_order_acquire)) {
    unit = nullptr;
    sweep = nullptr;
    for (uint32_t id = 0; id < round.segment_count.load(std::memory_order_acquire) && sweep == nullptr; ++id) {
      if (segments[id].sweep.load(std::memory_order_acquire) == 0) sweep = &segments[id];
    }
  }
#endif
  if (unit != nullptr) {
    if (!Claim(unit)) return false;
    RunUnit(unit);
    return true;
  }
  uint8_t waiting = 0;
  if (sweep == nullptr || !sweeps || !round.enumeration_done.load(std::memory_order_acquire)
      || !sweep->sweep.compare_exchange_strong(waiting, 1, std::memory_order_acq_rel)) {
    return false;
  }
  // The sweep is the game's own code: its destructors (the instances', the emitters', slot returns, sound stops) run here, in the
  // serial order, as the direct unit.
  CheckWatched();
  if (round.l2) {
    // The sweep is a direct stand-in: the random state must be where the serial walk has it (every unit before it committed).
    CommitDone();
    RefreshWatched();
  }
  Unit pseudo;
  pseudo.direct = true;
  pseudo.pseudo = true;
  pseudo.segment = sweep->id;
  pseudo.position = sweep->units + sweep->late_count.load(std::memory_order_relaxed);
  Unit* const saved_current = this_thread.current;
  this_thread.current = &pseudo;
  ThreadStackLimits();
  data->direct_high.store(this_thread.stack_high, std::memory_order_release);
  entries.sweep(sweep->effect);
  RefreshWatched();
  data->direct_high.store(0, std::memory_order_release);
  this_thread.current = saved_current;
  sweep->sweep.store(2, std::memory_order_seq_cst);
  AdvanceFrontier();
  Wake();
  return true;
}

// The words the game must not change between two direct units (and between the round's start and the first): compared and
// refreshed where a unit becomes direct and at the round's end. A difference is stored, B1 goes off for the session after the
// walk, nothing stops.
inline void CheckWatched() {
  Round& round = current_round;
  for (uint32_t index = 0; index < round.watched_count; ++index) {
    Watched& word = round.watched[index];
    const uint32_t now = *word.address;
    if (now == word.value) continue;
    Count(Counter::WATCHED_MISMATCHES);
    Note(EventKind::WATCHED_MISMATCH, round.last_site, Address(word.address));
    word.value = now;
    round.violation.store(true, std::memory_order_relaxed);
  }
}

// The walk ends (or a round fails): enumeration is done, the render thread runs the rest (the units, the sweeps, the mailbox)
// until every unit and sweep is done, the words are checked, the pool is joined and the gates close. The walk's own remaining
// effects after a failure go the game's way: the handler answers 0 for them (the armed gate is off).
inline void FinishRound(bool failed) {
  Round& round = current_round;
  if (failed) round.failed = true;  // before enumeration is done: a unit that waited for that reads it after
  round.enumeration_done.store(true, std::memory_order_seq_cst);
  Wake();
  for (;;) {
    if (Serve(true)) continue;
    if (Unit* unit = ClaimNext()) {
      RunUnit(unit);
      continue;
    }
    AdvanceFrontier();
    if ((round.frontier.load(std::memory_order_acquire) >> 32) == NONE) break;
    WaitUntil(
        [&] {
          AdvanceFrontier();
          return (round.frontier.load(std::memory_order_acquire) >> 32) == NONE || round.cursor.load(std::memory_order_relaxed) < round.published_units.load(std::memory_order_acquire)
                 || round.late_unclaimed.load(std::memory_order_relaxed) != 0;
        },
        true);
  }
  CheckWatched();
  if (round.l2) {
    // The staged spawns are committed and merged before the walk is closed (also when it failed: the walk's rest goes the game's way and finds the buffer as serial left it).
    CommitDone();
    Merge();
    if (round.spawn_violation.load(std::memory_order_relaxed)) spawns_off = true;
    last_round_chunks = std::min(round.chunk_next.load(std::memory_order_relaxed), static_cast<uint32_t>(chunks.capacity));
  }
  if (round.count_at != nullptr) last_round_spawned = *round.count_at - round.count0;
  worker_pool::Join();
  data->walk_active.store(0, std::memory_order_seq_cst);
  data->direct_high.store(0, std::memory_order_seq_cst);
  data->staging.store(0, std::memory_order_seq_cst);
  last_round_units = round.enqueued_units;
  small_round_presents = 0;
  if (round.violation.load(std::memory_order_relaxed)) session_off = true;
  if (failed) {
    data->walk_armed.store(0, std::memory_order_seq_cst);
    Count(Counter::FAILED_ROUNDS);
    Note(EventKind::FAILED_ROUND, round.last_site, round.segment_total);
  }
  Count(Counter::ROUNDS);
  Count(Counter::EFFECTS, round.segment_total);
  round.open = false;
}

// The pool's part of the round: claim, run, until enumeration is done and nothing is left to claim (the render thread runs on
// to the end in FinishRound). A late unit that appears after a worker left is the render thread's.
inline void WorkerEntry(void*, uint32_t) {
  Round& round = current_round;
  for (;;) {
    if (Unit* unit = ClaimNext()) {
      RunUnit(unit);
      continue;
    }
    if (round.enumeration_done.load(std::memory_order_acquire) && round.late_unclaimed.load(std::memory_order_relaxed) == 0
        && round.cursor.load(std::memory_order_relaxed) >= round.published_units.load(std::memory_order_acquire)) {
      return;
    }
    WaitUntil(
        [&] {
          return round.cursor.load(std::memory_order_relaxed) < round.published_units.load(std::memory_order_acquire)
                 || round.late_unclaimed.load(std::memory_order_relaxed) != 0 || round.enumeration_done.load(std::memory_order_acquire);
        },
        false);
  }
}

// The handler the thunks call: a switch over the sites. WALK_CALL starts or continues a round (answer 1: the walk's call is done,
// 0: the game's own call), WALK_END ends it, CHILD_APPEND reports a child appended to an effect's vector, every other site is an
// order point of the unit this thread runs.
inline uint32_t __cdecl Handle(Frame* frame, uint32_t site_index) {
  Round& round = current_round;
  const auto word = [&](size_t index) { return (&frame->stack[0])[index]; };
  const auto census_error = [&](uint32_t site) {
    Count(Counter::CENSUS_ERRORS);
    Note(EventKind::CENSUS_ERROR, site, 0);
    round.violation.store(true, std::memory_order_relaxed);
  };
  // A site hit by a thread that runs no unit. The render thread: the census has no such site there, so the round ends (the units
  // in flight drained, the rest of the walk the game's own way) and B1 is off for the session after it. Another thread (a game
  // thread at a shared site, a sound thread): it goes on as the original, counted in a trace window.
  const auto outside_unit = [&](uint32_t site) {
    if (GetCurrentThreadId() == round.owner) {
      census_error(site);
      FinishRound(true);
    } else {
      Count(Counter::FOREIGN_HITS);
    }
    return 0u;
  };
  // A word the game must not change between two direct units (its value now is the baseline).
  const auto watch = [&](const uint32_t* address, std::string_view name) {
    if (round.watched_count < round.watched.size()) round.watched[round.watched_count++] = {address, name, *address};
  };
  // The effects of a round by address: a hash of the effect to the ids of its segments (a segment is an effect's place in the order).
  const auto bucket = [&](uint32_t effect) { return &round.buckets[((effect >> 4) * 2654435761u >> 20) & (BUCKETS - 1)]; };
  const auto find_segment = [&](uint32_t effect) {
    for (uint32_t id = *bucket(effect); id != NONE; id = segments[id].map_next) {
      if (segments[id].effect == effect) return id;
    }
    return NONE;
  };
  // A segment `id` for `effect`, after `previous` in the order (NONE: first) and before `next`, hashed, with a unit for each of the
  // effect's `count` instances from `first_unit` (late units: a segment that came after its effect was due). Not yet published.
  const auto add_segment = [&](uint32_t id, uint32_t effect, uint32_t rank, uint32_t first_unit, uint32_t count, uint32_t previous, uint32_t next, bool late) {
    Segment& segment = segments[id];
    segment.id = id;
    segment.effect = effect;
    segment.rank = rank;
    segment.first_unit = first_unit;
    segment.units = count;
    segment.next = next;
    segment.previous = previous;
    segment.late_head = NONE;
    segment.late_tail = NONE;
    segment.late_count.store(0, std::memory_order_relaxed);
    segment.sweep.store(0, std::memory_order_relaxed);
    uint32_t* const chain = bucket(effect);
    segment.map_next = *chain;
    *chain = id;
    const uint32_t entries_at = Read(effect + EFFECT_ENTRIES_OFFSET);
    for (uint32_t position = 0; position < count; ++position) {
      units[first_unit + position].Reset(Read(entries_at + 8 * position), id, position, late);
    }
  };
  // The thread runs a unit and is at an order point: wait until it is the oldest (and, for the sites that change what the render
  // thread reads while it enumerates, until enumeration is done), then it is the direct unit.
  const auto become_oldest = [&](Unit* unit, uint32_t site, bool wait_for_enumeration) {
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
    wait_for_enumeration = wait_for_enumeration && !controls.no_enumeration_wait;
#endif
    if (unit->direct && (!wait_for_enumeration || round.enumeration_done.load(std::memory_order_acquire))) return;
    const uint64_t mine = Pack(unit->segment, unit->position);
    // A direct unit that reaches a site that needs enumeration to be done waits for that only: it is the oldest already.
    WaitUntil(
        [&] {
          if (!unit->direct) AdvanceFrontier();
          bool oldest = (unit->direct || round.frontier.load(std::memory_order_acquire) == mine);
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
          if (controls.wait_on_later && !unit->direct && counters[static_cast<size_t>(Counter::HANGS)].load(std::memory_order_relaxed) == 0) {
            oldest = (round.frontier.load(std::memory_order_acquire) > mine);
          }
#endif
          return oldest && (!wait_for_enumeration || round.enumeration_done.load(std::memory_order_acquire));
        },
        GetCurrentThreadId() == round.owner);
    if (unit->direct) return;
    CheckWatched();
    if (round.l2) {
      // Every unit before this one is done: their draws are made and their spawns counted now, then this unit's own deferred draws (they come before whatever it does next).
      CommitDone();
      ResolveDraws(unit);
      RefreshWatched();
    }
    round.last_site = site;
    ThreadStackLimits();
    data->direct_high.store(this_thread.stack_high, std::memory_order_release);
    unit->direct = true;
    if (round.tracing) {
      const uint64_t last = round.last_done_ticks.load(std::memory_order_relaxed);
      if (last != 0) {
        handover_ticks.fetch_add(worker_pool::Qpc() - last, std::memory_order_relaxed);
        handovers.fetch_add(1, std::memory_order_relaxed);
      }
    }
  };

  switch (static_cast<Site>(site_index)) {
    case Site::WALK_CALL: {
      // The walk's call from a thread that is not the render thread (a worker, a game thread) is never the round's, and writes nothing: in an open round it would enqueue beside
      // the render thread's (a census error). From inside a unit of the render thread's: the same.
      if (GetCurrentThreadId() != worker_pool::internal::owner.load(std::memory_order_relaxed)) {
        if (round.open) census_error(site_index);
        return 0;
      }
      if (round.open && this_thread.current != nullptr) {
        census_error(site_index);
        return 0;
      }
      if (!round.open) {
        const auto serial = [](SerialReason reason) {
          if (worker_pool::Tracing()) serial_rounds[static_cast<size_t>(reason)].fetch_add(1, std::memory_order_relaxed);
          data->walk_armed.store(0, std::memory_order_seq_cst);
          return 0u;
        };
        const uint32_t node = word(2) - NODE_ARGUMENT_OFFSET;
        if (!row_on || session_off) return serial(SerialReason::SESSION_OFF);
        if (last_round_units < MIN_ROUND_UNITS) return serial(SerialReason::SMALL);
        if (node != last_node) {
          last_node = node;
          return serial(SerialReason::NEW_NODE);
        }
        const uint32_t workers = worker_pool::Available(worker_pool::User::EFFECT_UPDATE);
        if (workers == 0) return serial(SerialReason::NO_WORKERS);
        // The node's pipeline and its spawn buffer (read outside the round: nothing changes them before a direct unit does): the count word, the room for the game's "is there room"
        // (query_limit: CPU, also the simulation's room) and for its spawn (cap_limit). L2 stages when the row asks and there is room enough and the last round left half of it.
        const uint32_t pipeline = Read(node + PIPELINE_OFFSET);
        const uint32_t vtable = (pipeline != 0 ? Read(pipeline) : 0);
        const bool cpu_pipeline = (vtable == Address(base) + CPU_PIPELINE_VTABLE_RVA);
        const bool gpu_pipeline = (vtable == Address(base) + GPU_PIPELINE_VTABLE_RVA);
        const uint32_t buffer = (cpu_pipeline ? Read(pipeline + 0x10) : (gpu_pipeline ? Read(pipeline + 0x14) : 0));
        const uint32_t* const count_word = (buffer != 0 ? reinterpret_cast<const uint32_t*>(buffer + (cpu_pipeline ? 0x28 : 0x14)) : nullptr);
        const uint32_t count0 = (count_word != nullptr ? *count_word : 0);
        const uint32_t capacity = (count_word != nullptr ? Read(buffer + (cpu_pipeline ? 8 : 0xC)) : 0);
        const uint32_t cap_limit = (capacity > count0 ? capacity - count0 : 0);
        uint32_t query_limit = cap_limit;
        if (cpu_pipeline) {
          const int32_t simulation_room = static_cast<int32_t>(Read(Read(pipeline + 0x14) + 8)) - static_cast<int32_t>(Read(Read(pipeline + 0x18) + 0xC));
          if (simulation_room >= static_cast<int32_t>(count0)) query_limit = std::min(cap_limit, static_cast<uint32_t>(simulation_room) - count0);
        }
        const bool asks_l2 = (spawns_row && !spawns_off && count_word != nullptr);
        const bool l2 = (asks_l2 && query_limit >= MIN_SPAWN_BUDGET && static_cast<uint64_t>(last_round_spawned) * 2 < query_limit);
        if (!units.Reserve(std::max(last_round_units + last_round_units / 4, BLOCK_SIZE)) || !segments.Reserve(BLOCK_SIZE)
            || (l2 && !chunks.Reserve(last_round_chunks + last_round_chunks / 4 + 1))) {
          return serial(SerialReason::ALLOCATION);
        }
        ThreadStackLimits();
        round.open = true;
        round.tracing = worker_pool::Tracing();
        round.type_added = false;
        round.failed = false;
        round.failed_effect = 0;
        round.violation.store(false, std::memory_order_relaxed);
        round.owner = worker_pool::internal::owner.load(std::memory_order_relaxed);
        round.node = node;
        round.arguments[0] = word(1);
        round.arguments[1] = word(2);
        round.arguments[2] = word(3);
        round.enqueued_units = 0;
        round.segment_total = 0;
        round.tail = NONE;
        round.last_site = 0;
        round.last_done_ticks.store(0, std::memory_order_relaxed);
        round.segment_count.store(0, std::memory_order_relaxed);
        round.published_units.store(0, std::memory_order_relaxed);
        round.cursor.store(0, std::memory_order_relaxed);
        round.late_unclaimed.store(0, std::memory_order_relaxed);
        round.enumeration_done.store(false, std::memory_order_relaxed);
        round.frontier.store(0, std::memory_order_relaxed);
        round.buckets.fill(NONE);
        round.mailbox.state.store(0, std::memory_order_relaxed);
        round.watched_count = 0;
        watch(entries.random_state, "random state");
        watch(reinterpret_cast<const uint32_t*>(node + TABLE_OFFSET), "table base");
        watch(reinterpret_cast<const uint32_t*>(node + TABLE_OFFSET + 4), "table capacity");
        if (count_word != nullptr) watch(count_word, "spawn count");
        if (cpu_pipeline) {
          watch(reinterpret_cast<const uint32_t*>(Read(pipeline + 0x14) + 8), "simulation room");
          watch(reinterpret_cast<const uint32_t*>(Read(pipeline + 0x18) + 0xC), "simulation count");
        }
        round.pool_count = 0;
        round.pipeline = pipeline;
        round.count_at = count_word;
        round.count0 = count0;
        round.query_limit = query_limit;
        round.cap_limit = cap_limit;
        round.spawn_total = 0;
        round.commit_at = 0;
        round.merge_head = nullptr;
        round.merge_tail = nullptr;
        round.chunk_next.store(0, std::memory_order_relaxed);
        round.spawn_violation.store(false, std::memory_order_relaxed);
        round.l2 = l2;
        if (round.tracing && asks_l2) Count(l2 ? Counter::SPAWN_ROUNDS : Counter::GUARD_ROUNDS);
        data->limit.store(std::min(this_thread.stack_size, worker_pool::internal::WORKER_STACK_BYTES), std::memory_order_relaxed);
        data->direct_high.store(0, std::memory_order_relaxed);
        data->staging.store(l2 ? 1 : 0, std::memory_order_relaxed);
        data->walk_active.store(1, std::memory_order_seq_cst);
        const bool started = worker_pool::Start({.work = &WorkerEntry, .user = worker_pool::User::EFFECT_UPDATE, .join = worker_pool::JoinRule::ENTERED, .trace = round.tracing}, workers);
        if (!started) {
          data->walk_active.store(0, std::memory_order_seq_cst);
          data->staging.store(0, std::memory_order_seq_cst);
          round.open = false;
          return serial(SerialReason::START_REFUSED);
        }
      }
      if (round.mailbox.state.load(std::memory_order_acquire) == 1) Serve(true);
      // The game's per-effect function: nothing at all while the render switch is off.
      if (entries.render_switch(entries.render_switch_object) == 0) return 1;
      const uint32_t effect = frame->ecx;
      const uint32_t count = Read(effect + EFFECT_COUNT_OFFSET);
      const uint32_t first = round.published_units.load(std::memory_order_relaxed);
      const uint32_t id = round.segment_count.load(std::memory_order_relaxed);
      if (round.segment_total >= RANK_LIMIT || !segments.Reserve(id + 1) || !units.Reserve(first + count)) {
        round.failed_effect = effect;  // the game's own loop goes on from this effect
        FinishRound(true);
        return 0;
      }
      add_segment(id, effect, round.segment_total * RANK_STEP, first, count, round.tail, NONE, false);
      round.published_units.store(first + count, std::memory_order_release);
      // seq_cst: this store and the waiters load in Wake are the two halves of a store-load pair (a waiter that registers after the
      // load sees the segment).
      round.segment_count.store(id + 1, std::memory_order_seq_cst);
      if (round.tail != NONE) segments[round.tail].next = id;
      round.tail = id;
      round.segment_total += 1;
      round.enqueued_units += count;
      Wake();
      return 1;
    }
    case Site::WALK_END:
      if (GetCurrentThreadId() != round.owner || this_thread.current != nullptr) {
        census_error(site_index);
        return 0;
      }
      FinishRound(false);
      return 0;
    case Site::CHILD_APPEND_A:
    case Site::CHILD_APPEND_B: {
      if (this_thread.serving) return 0;
      Unit* const unit = this_thread.current;
      if (unit == nullptr) return outside_unit(site_index);
      become_oldest(unit, site_index, true);
      const uint32_t effect = frame->ecx - APPEND_VECTOR_OFFSET;
      const uint32_t child = Read(word(1));
      const Segment& current = segments[unit->segment];
      const uint32_t target = find_segment(effect);
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
      if (controls.late_child_off) return 0;
#endif
      if (target != NONE) {
        // A child appended to the effect of a later segment: serial walks it as that effect's last instance, a late unit of the
        // segment. The current or an earlier segment gets nothing (its instances were copied, the game's own loop does not see it).
        Segment& later = segments[target];
        if (later.rank <= current.rank) return 0;
        const uint32_t index = round.published_units.load(std::memory_order_relaxed);
        if (!units.Reserve(index + 1)) {
          Count(Counter::LATE_OVERFLOWS);
          Note(EventKind::LATE_OVERFLOW, site_index, target);
          round.violation.store(true, std::memory_order_relaxed);
          return 0;
        }
        units[index].Reset(child, target, later.units + later.late_count.load(std::memory_order_relaxed), true);
        if (later.late_head == NONE) {
          later.late_head = index;
        } else {
          units[later.late_tail].next_late = index;
        }
        later.late_tail = index;
        round.late_unclaimed.fetch_add(1, std::memory_order_relaxed);
        round.published_units.store(index + 1, std::memory_order_release);
        later.late_count.fetch_add(1, std::memory_order_release);
        Count(Counter::LATE_UNITS);
        Wake();
        return 0;
      }
      if (!round.type_added) return 0;
      // A new effect type registered in this walk: serial walks it when its slot in the node's table is after the current
      // effect's slot, between the enqueued effects around it (a late segment whose instances are all late units).
      const uint32_t slots = Read(round.node + TABLE_OFFSET);
      const uint32_t capacity = Read(round.node + TABLE_OFFSET + 4);
      const auto slot_of = [&](uint32_t wanted) {
        for (uint32_t slot = 0; slot < capacity; ++slot) {
          if (Read(slots + slot * SLOT_SIZE + SLOT_EFFECT_OFFSET) == wanted) return slot;
        }
        return NONE;
      };
      const uint32_t target_slot = slot_of(effect);
      const uint32_t current_slot = slot_of(current.effect);
      if (target_slot == NONE || current_slot == NONE || target_slot <= current_slot) return 0;
      // After a failure the game's own loop goes on from the effect whose walk call failed (when the round ended for another reason: from the slot after the last enqueued effect)
      // and walks a type whose slot is there or later itself; only a slot the enumeration passed already (empty, or an effect that was enqueued) needs a late segment.
      if (round.failed) {
        const uint32_t tail_slot = (round.tail == NONE ? NONE : slot_of(segments[round.tail].effect));
        const uint32_t resume_slot = (round.failed_effect != 0 ? slot_of(round.failed_effect) : (tail_slot == NONE ? NONE : tail_slot + 1));
        if (resume_slot == NONE || target_slot >= resume_slot) return 0;
      }
      uint32_t before = NONE;
      for (uint32_t slot = target_slot + 1; slot < capacity && before == NONE; ++slot) {
        if (const uint32_t other = Read(slots + slot * SLOT_SIZE + SLOT_EFFECT_OFFSET); other != 0) before = find_segment(other);
      }
      const uint32_t previous = (before == NONE ? round.tail : segments[before].previous);
      const uint32_t lower = segments[previous].rank;
      const uint32_t upper = (before == NONE ? lower + RANK_STEP : segments[before].rank);
      const uint32_t rank = lower + (upper - lower) / 2;
      const uint32_t count = Read(effect + EFFECT_COUNT_OFFSET);
      const uint32_t first = round.published_units.load(std::memory_order_relaxed);
      const uint32_t id = round.segment_count.load(std::memory_order_relaxed);
      if (upper - lower < 2 || !segments.Reserve(id + 1) || !units.Reserve(first + count)) {
        Count(Counter::LATE_OVERFLOWS);
        Note(EventKind::LATE_OVERFLOW, site_index, id);
        round.violation.store(true, std::memory_order_relaxed);
        return 0;
      }
      add_segment(id, effect, rank, first, count, previous, before, true);
      round.late_unclaimed.fetch_add(count, std::memory_order_relaxed);
      round.published_units.store(first + count, std::memory_order_release);
      round.segment_count.store(id + 1, std::memory_order_seq_cst);
      segments[previous].next = id;
      if (before != NONE) {
        segments[before].previous = id;
      } else {
        round.tail = id;
      }
      Count(Counter::LATE_SEGMENTS);
      Count(Counter::LATE_UNITS, count);
      Wake();
      return 0;
    }
    case Site::SPAWN_QUERY:
    case Site::SPAWN_WRITE:
    case Site::RANDOM_TRIPLE: {
      // L2: the spawn path's three sites are answered here, never order points. A round that does not stage, the render thread's own forwarded call and a thread that runs no unit go
      // the order-point way below.
      Unit* const unit = this_thread.current;
      if (!round.l2 || this_thread.serving || unit == nullptr) break;
      if (unit->pseudo) {
        // A sweep (destructors) at the spawn path: the census has none, and the buffer lacks what the units before it staged.
        census_error(site_index);
        return 0;
      }
      const Site site = static_cast<Site>(site_index);
      if (site == Site::SPAWN_QUERY) {
        // Room: exact once the unit is direct (the units before it are committed, spawn_total is the serial count), true while it runs ahead (the commit checks it).
        if (unit->direct) {
          frame->eax = (round.spawn_total + unit->staged < round.query_limit ? 1 : 0);
        } else {
          unit->ahead = true;
          unit->ahead_staged = unit->staged;
          frame->eax = 1;
        }
        return 1;
      }
      if (site == Site::SPAWN_WRITE) {
        if (unit->direct && round.spawn_total + unit->staged >= round.cap_limit) {
          frame->eax = 0;  // the game's spawn refuses a full buffer
          return 1;
        }
        Chunk* tail = (unit->stage_tail == NONE ? nullptr : &chunks[unit->stage_tail]);
        if (tail == nullptr || tail->count == CHUNK_RECORDS) {
          const uint32_t index = round.chunk_next.fetch_add(1, std::memory_order_relaxed);
          if (index >= chunks.capacity) {
            Count(Counter::STAGE_FULLS);
            Note(EventKind::STAGE_FULL, site_index, index);
            round.spawn_violation.store(true, std::memory_order_relaxed);
            frame->eax = 0;
            return 1;
          }
          chunks[index].next = NONE;
          chunks[index].count = 0;
          if (tail == nullptr) {
            unit->stage_head = index;
          } else {
            tail->next = index;
          }
          unit->stage_tail = index;
          tail = &chunks[index];
        }
        uint32_t* const record = tail->records[tail->count++];
        std::memcpy(record, reinterpret_cast<const void*>(word(1) + SPAWN_RECORD_OFFSET), sizeof(tail->records[0]));
        if (round.tracing) {
          // A deferred draw's tag anywhere but where the census says its value goes is a leak: the value would be used before it is known.
          for (uint32_t dword = 0; dword < RECORD_DWORDS; ++dword) {
            if (dword != RECORD_DRAW_INDEX && (record[dword] & TAG_MASK) == TAG_BASE && (record[dword] & MAX_DRAWS) != 0) {
              Count(Counter::TAG_LEAKS);
              Note(EventKind::TAG_LEAK, site_index, dword);
              round.spawn_violation.store(true, std::memory_order_relaxed);
            }
          }
        }
        ++unit->staged;
        frame->eax = 1;
        return 1;
      }
      // The three-step random draw: a direct unit's is the game's. The create path's draw of a unit that runs ahead is deferred (its outputs are tags, the value is made at the
      // commit), unless the emitter raises events (the event's handler would see the tag); every other caller is an order point.
      if (unit->direct) return 0;
      bool create_path = (word(0) == Address(base) + CREATE_DRAW_RETURN_RVA);
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
      create_path = create_path || controls.defer_any_caller;
#endif
      if (create_path && unit->draws < MAX_DRAWS) {
        const uint32_t settings = Read(Read(frame->esi + 0x10) + 0x78);
        if (*reinterpret_cast<const uint8_t*>(settings + 0x298) == 0 || *reinterpret_cast<const uint8_t*>(settings + 0x5C) == 0) {
          uint32_t* const out = reinterpret_cast<uint32_t*>(word(1));
          out[0] = out[1] = out[2] = TAG_BASE | ++unit->draws;
          frame->eax = word(1);
          Count(Counter::DEFERRED_DRAWS);
          return 1;
        }
      }
      break;
    }
    default:
      break;
  }

  // An order point.
  if (this_thread.serving) return 0;
  Unit* const unit = this_thread.current;
  if (unit == nullptr) return outside_unit(site_index);
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  if (controls.no_order_points) return 0;
#endif
  const Site site = static_cast<Site>(site_index);
  const bool first_point = !unit->direct;
  become_oldest(unit, site_index, WaitsForEnumeration(site));
  if (round.tracing) {
    order_point_hits[site_index].fetch_add(1, std::memory_order_relaxed);
    if (first_point) first_point_hits[site_index].fetch_add(1, std::memory_order_relaxed);
  }
  if (site == Site::TYPE_ADD_A || site == Site::TYPE_ADD_B) round.type_added = true;
  if ((site == Site::SLOT_TAKE || site == Site::SLOT_RETURN) && frame->ecx != 0 && round.pool_count < round.pools.size()
      && std::find(round.pools.begin(), round.pools.begin() + round.pool_count, frame->ecx) == round.pools.begin() + round.pool_count) {
    // The slot pool (`this`): its free list's count and the minimum and maximum in the object it points to are only changed by these sites: watched from the first hit.
    round.pools[round.pool_count++] = frame->ecx;
    const uint32_t limits = Read(frame->ecx + 0x18);
    watch(reinterpret_cast<const uint32_t*>(frame->ecx + 0x10), "pool count");
    watch(reinterpret_cast<const uint32_t*>(limits + 0x50), "pool minimum");
    watch(reinterpret_cast<const uint32_t*>(limits + 0x54), "pool maximum");
  }
  bool forwarded = Forwards(site) && GetCurrentThreadId() != round.owner;
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  forwarded = forwarded && !controls.rigid_on_worker;
#endif
  if (!forwarded) return 0;
  // Only the render thread makes the call (the one direct unit posts; one slot is enough): the unit's function returns what it
  // returned.
  Mailbox& mail = round.mailbox;
  mail.site = site_index;
  mail.ecx = frame->ecx;
  mail.first = word(1);
  mail.second = (site == Site::RANDOM_FILL ? word(2) : 0);
  const uint64_t start = (round.tracing ? worker_pool::Qpc() : 0);
  mail.state.store(1, std::memory_order_seq_cst);
  Wake();
  WaitUntil([&] { return mail.state.load(std::memory_order_acquire) == 2; }, false);
  frame->eax = mail.result;
  mail.state.store(0, std::memory_order_release);
  Count(Counter::FORWARDED);
  if (round.tracing) forward_wait_ticks.fetch_add(worker_pool::Qpc() - start, std::memory_order_relaxed);
  return 1;
}

}  // namespace internal

// The present's call (owner thread, outside a round, after engine_code::Update and worker_pool::Update): the row, the handler, the
// gates, and the lines of what the last frames stored. A round still open here (the walk never reached its end) is closed.
inline void Update(bool row, bool spawns = false) {
  using namespace internal;
  if (data == nullptr) return;
  if (current_round.open) {
    FinishRound(true);
    session_off = true;  // the walk never ended: something broke inside it
    renodx::utils::log::w("[Total War] Parallel Effect Update: a round was still open at the present; closed.");
  }
  if (entries.update_instance == nullptr) {
    entries.update_instance = reinterpret_cast<decltype(entries.update_instance)>(const_cast<uint8_t*>(base) + INSTANCE_UPDATE_RVA);
    entries.render_switch = reinterpret_cast<decltype(entries.render_switch)>(const_cast<uint8_t*>(base) + RENDER_SWITCH_GETTER_RVA);
    entries.render_switch_object = const_cast<uint8_t*>(base) + RENDER_SWITCH_RVA;
    entries.sweep = reinterpret_cast<decltype(entries.sweep)>(const_cast<uint8_t*>(sweep));
    entries.random_state = reinterpret_cast<const uint32_t*>(base + RANDOM_STATE_RVA);
    entries.draw = reinterpret_cast<decltype(entries.draw)>(const_cast<uint8_t*>(base) + DRAW_RVA);
  }
  SetHandler(&Handle);
  row_on = row;
  spawns_row = spawns;
  if (session_off && !off_said) {
    off_said = true;
    renodx::utils::log::w("[Total War] Parallel Effect Update: off for the session (a safety check failed; see the lines above).");
  }
  if (row && last_round_units < MIN_ROUND_UNITS && ++small_round_presents >= SMALL_ROUND_RETRY_PRESENTS) {
    last_round_units = MIN_ROUND_UNITS;
    small_round_presents = 0;
  }
  // Every site the walk's gate opens needs all 27 in the game's code: a partial set (a site that stood down) never arms.
  data->walk_armed.store((row && !session_off && written == ALL_SITES ? 1 : 0), std::memory_order_seq_cst);

  // What a trace window counted (nothing else is counted outside one, but the safety counters): one line for its presents. The
  // safety counters ride along; alone they print nothing (their events are logged once, below).
  std::string line;
  bool traced = false;
  for (size_t index = 0; index < counters.size(); ++index) {
    if (const uint32_t value = counters[index].exchange(0, std::memory_order_relaxed); value != 0) {
      line += std::format(" {} {},", COUNTER_NAMES[index], value);
      traced |= (index < static_cast<size_t>(Counter::HANGS));
    }
  }
  for (size_t index = 0; index < serial_rounds.size(); ++index) {
    if (const uint32_t value = serial_rounds[index].exchange(0, std::memory_order_relaxed); value != 0) {
      line += std::format(" serial ({}) {},", SERIAL_REASON_NAMES[index], value);
      traced = true;
    }
  }
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    if (const uint32_t value = order_point_hits[index].exchange(0, std::memory_order_relaxed); value != 0) {
      line += std::format(" {} {} ({} first),", SITES[index].name, value, first_point_hits[index].exchange(0, std::memory_order_relaxed));
      traced = true;
    }
  }
  if (const uint32_t count = handovers.exchange(0, std::memory_order_relaxed); count != 0) {
    line += std::format(" handover {:.0f} ns,", static_cast<double>(handover_ticks.exchange(0, std::memory_order_relaxed)) * 1e9
                                                    / static_cast<double>(worker_pool::qpc_frequency) / count);
    traced = true;
  }
  if (const uint64_t longest = longest_wait_ticks.exchange(0, std::memory_order_relaxed); longest != 0) {
    line += std::format(" longest wait {:.1f} us,", static_cast<double>(longest) * 1e6 / static_cast<double>(worker_pool::qpc_frequency));
    traced = true;
  }
  if (const uint64_t ticks = forward_wait_ticks.exchange(0, std::memory_order_relaxed); ticks != 0) {
    line += std::format(" forwarded wait {:.1f} us,", static_cast<double>(ticks) * 1e6 / static_cast<double>(worker_pool::qpc_frequency));
    traced = true;
  }
  if (traced) {
    line.pop_back();
    renodx::utils::log::i("[Total War] Parallel Effect Update (trace window ", worker_pool::TraceWindow(), "):", line);
  }
  const uint32_t stored = std::min<uint32_t>(event_count.exchange(0, std::memory_order_relaxed), static_cast<uint32_t>(events.size()));
  for (uint32_t index = 0; index < stored; ++index) {
    const EventRecord& event = events[index];
    renodx::utils::log::w("[Total War] Parallel Effect Update: ", EVENT_NAMES[static_cast<size_t>(event.kind)], " (site ", SITES[event.site].name, ", thread ",
                          event.thread, ", value 0x", std::format("{:X}", event.value), ")");
  }
}

#else

inline void Update(bool) {}

#endif  // !defined(_WIN64)

}  // namespace twattila::effect_update
