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
// The safety checks (a word the game changed between two direct units, a site hit by a thread that runs no unit, a render
// thread hit outside a unit) are always on and stored; Update prints them at the next present (no log call inside game code).
// The other counters run only in a worker pool trace window.
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

// The game's functions the scheduler calls. Update fills them from the image; a test sets what it replaces.
struct Entries {
  void(__thiscall* update_instance)(void* instance, void* a1, void* a2, void* a3) = nullptr;
  uint8_t(__thiscall* render_switch)(void* object) = nullptr;
  void* render_switch_object = nullptr;
  void(__thiscall* sweep)(uint32_t effect) = nullptr;
  const uint32_t* random_state = nullptr;
};
static Entries entries;

enum class SerialReason : uint8_t { SESSION_OFF, SMALL, NEW_NODE, NO_WORKERS, START_REFUSED, ALLOCATION, COUNT };
constexpr std::array<std::string_view, static_cast<size_t>(SerialReason::COUNT)> SERIAL_REASON_NAMES = {
    "session off", "too few units last round", "new node", "no workers", "start refused", "allocation"};

// Counters: the first block only in a trace window, the second (from HANGS) always: the safety checks.
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
  HANGS,
  WATCHED_MISMATCHES,
  FOREIGN_HITS,
  CENSUS_ERRORS,
  LATE_OVERFLOWS,
  FAILED_ROUNDS,
  COUNT
};
constexpr std::array<std::string_view, static_cast<size_t>(Counter::COUNT)> COUNTER_NAMES = {
    "units",  "effects", "late units", "late segments", "waits",     "nested runs",    "wake timeouts", "forwarded calls",
    "order points", "rounds", "hangs", "watched mismatches", "foreign hits", "census errors", "late overflows", "failed rounds"};

enum class EventKind : uint8_t { WATCHED_MISMATCH, FOREIGN_HIT, CENSUS_ERROR, LATE_OVERFLOW, HANG };
constexpr std::array<std::string_view, 5> EVENT_NAMES = {"watched word changed", "site hit by a thread without a unit",
                                                         "site hit by the render thread outside a unit", "late overflow", "hang"};
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
  std::atomic<uint8_t> done = 0;
  std::atomic<uint8_t> claimed = 0;
  bool late = false;
  bool direct = false;  // touched only by the thread that runs it
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
};
static Controls controls;
#endif

// Append-only storage in blocks that never move: a reader holding a pointer is safe while a block is added.
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
  bool violation = false;
  DWORD owner = 0;
  uint32_t node = 0;
  uint32_t arguments[3] = {};
  uint32_t enqueued_units = 0;
  uint32_t segment_total = 0;
  uint32_t tail = NONE;  // the last segment in the order
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
  std::array<Watched, 4> watched = {};
  uint32_t watched_count = 0;
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
static thread_local ThreadState this_thread;
static std::array<std::atomic<uint32_t>, static_cast<size_t>(Counter::COUNT)> counters = {};
static std::array<std::atomic<uint32_t>, static_cast<size_t>(SerialReason::COUNT)> serial_rounds = {};
static std::array<std::atomic<uint32_t>, SITE_COUNT> order_point_hits = {};
static std::array<std::atomic<uint32_t>, SITE_COUNT> first_point_hits = {};
static std::array<EventRecord, 64> events = {};
static std::atomic<uint32_t> event_count = 0;
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

// A counter: the trace counters only in a trace window, the safety ones (from HANGS) always.
inline void Count(Counter counter, uint32_t amount = 1) {
  if (counter >= Counter::HANGS || current_round.tracing) {
    counters[static_cast<size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
  }
}

inline void Note(EventKind kind, uint32_t site, uint32_t value) {
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

inline uint32_t& Bucket(uint32_t effect) { return current_round.buckets[((effect >> 4) * 2654435761u >> 20) & (BUCKETS - 1)]; }

// The segment of an effect (NONE: not in this round).
inline uint32_t FindSegment(uint32_t effect) {
  for (uint32_t id = Bucket(effect); id != NONE; id = segments[id].map_next) {
    if (segments[id].effect == effect) return id;
  }
  return NONE;
}

// A segment `id` for `effect`, after `previous` in the order (NONE: first) and before `next`, hashed, with a unit for each of the
// effect's `count` instances from `first_unit` (late units: a segment that came after its effect was due). Not yet published.
inline void AddSegment(uint32_t id, uint32_t effect, uint32_t rank, uint32_t first_unit, uint32_t count, uint32_t previous, uint32_t next, bool late) {
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
  uint32_t& bucket = Bucket(effect);
  segment.map_next = bucket;
  bucket = id;
  const uint32_t entries_at = Read(effect + EFFECT_ENTRIES_OFFSET);
  for (uint32_t position = 0; position < count; ++position) {
    Unit& unit = units[first_unit + position];
    unit.instance = Read(entries_at + 8 * position);
    unit.segment = id;
    unit.position = position;
    unit.next_late = NONE;
    unit.done.store(0, std::memory_order_relaxed);
    unit.claimed.store(0, std::memory_order_relaxed);
    unit.late = late;
    unit.direct = false;
  }
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
    for (uint32_t index = 0; index < total; ++index) {
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

// The unit the frontier points at, or the segment whose sweep it is (the render thread's to run), after advancing it.
inline void Frontier(Unit** unit, Segment** sweep) {
  *unit = nullptr;
  *sweep = nullptr;
  AdvanceFrontier();
  const uint64_t at = current_round.frontier.load(std::memory_order_acquire);
  const uint32_t segment_id = static_cast<uint32_t>(at >> 32);
  const uint32_t position = static_cast<uint32_t>(at);
  if (segment_id == NONE || segment_id >= current_round.segment_count.load(std::memory_order_acquire)) return;
  Segment& segment = segments[segment_id];
  const uint32_t late = segment.late_count.load(std::memory_order_acquire);
  if (position < segment.units) {
    *unit = &units[segment.first_unit + position];
  } else if (position - segment.units < late) {
    *unit = LateUnit(segment, position - segment.units);
  } else {
    *sweep = &segment;
  }
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
    for (uint32_t index = 0; index < round.watched_count; ++index) {
      round.watched[index].value = *round.watched[index].address;
    }
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
  if (blocked) {
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
  Unit* unit = nullptr;
  Segment* sweep = nullptr;
  Frontier(&unit, &sweep);
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
  Unit pseudo;
  pseudo.direct = true;
  pseudo.segment = sweep->id;
  pseudo.position = sweep->units + sweep->late_count.load(std::memory_order_relaxed);
  Unit* const saved_current = this_thread.current;
  this_thread.current = &pseudo;
  ThreadStackLimits();
  data->direct_high.store(this_thread.stack_high, std::memory_order_release);
  entries.sweep(sweep->effect);
  for (uint32_t index = 0; index < round.watched_count; ++index) {
    round.watched[index].value = *round.watched[index].address;
  }
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
    round.violation = true;
  }
}

// The thread runs a unit and is at an order point: wait until it is the oldest (and, for the sites that change what the render
// thread reads while it enumerates, until enumeration is done), then it is the direct unit.
inline void BecomeOldest(Unit* unit, uint32_t site, bool wait_for_enumeration) {
  Round& round = current_round;
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  wait_for_enumeration = wait_for_enumeration && !controls.no_enumeration_wait;
#endif
  if (unit->direct && (!wait_for_enumeration || round.enumeration_done.load(std::memory_order_acquire))) return;
  const uint64_t mine = Pack(unit->segment, unit->position);
  // A direct unit that reaches a site that needs enumeration to be done waits for that only: it is the oldest already.
  WaitUntil(
      [&] {
        if (!unit->direct) AdvanceFrontier();
        return (unit->direct || round.frontier.load(std::memory_order_acquire) == mine) && (!wait_for_enumeration || round.enumeration_done.load(std::memory_order_acquire));
      },
      GetCurrentThreadId() == round.owner);
  if (unit->direct) return;
  CheckWatched();
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
}

// The walk ends (or a round fails): enumeration is done, the render thread runs the rest (the units, the sweeps, the mailbox)
// until every unit and sweep is done, the words are checked, the pool is joined and the gates close. The walk's own remaining
// effects after a failure go the game's way: the handler answers 0 for them (the armed gate is off).
inline void FinishRound(bool failed) {
  Round& round = current_round;
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
  worker_pool::Join();
  data->walk_active.store(0, std::memory_order_seq_cst);
  data->direct_high.store(0, std::memory_order_seq_cst);
  last_round_units = round.enqueued_units;
  small_round_presents = 0;
  if (round.violation) session_off = true;
  if (failed) {
    data->walk_armed.store(0, std::memory_order_seq_cst);
    Count(Counter::FAILED_ROUNDS);
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

// A unit's thread appends a late unit to `segment` (a child appended to the effect of a later segment: serial walks it as that
// effect's last instance), or a late segment: false when there is no room (a late overflow).
inline bool AppendLate(Segment& segment, uint32_t segment_id, uint32_t instance) {
  Round& round = current_round;
  const uint32_t index = round.published_units.load(std::memory_order_relaxed);
  if (!units.Reserve(index + 1)) return false;
  Unit& unit = units[index];
  unit.instance = instance;
  unit.segment = segment_id;
  unit.position = segment.units + segment.late_count.load(std::memory_order_relaxed);
  unit.next_late = NONE;
  unit.done.store(0, std::memory_order_relaxed);
  unit.claimed.store(0, std::memory_order_relaxed);
  unit.late = true;
  unit.direct = false;
  if (segment.late_head == NONE) {
    segment.late_head = index;
  } else {
    units[segment.late_tail].next_late = index;
  }
  segment.late_tail = index;
  round.late_unclaimed.fetch_add(1, std::memory_order_relaxed);
  round.published_units.store(index + 1, std::memory_order_release);
  segment.late_count.fetch_add(1, std::memory_order_release);
  Count(Counter::LATE_UNITS);
  Wake();
  return true;
}

// The handler the thunks call: a switch over the sites. WALK_CALL starts or continues a round (answer 1: the walk's call is done,
// 0: the game's own call), WALK_END ends it, CHILD_APPEND reports a child appended to an effect's vector, every other site is an
// order point of the unit this thread runs.
inline uint32_t __cdecl Handle(Frame* frame, uint32_t site_index) {
  Round& round = current_round;
  const auto word = [&](size_t index) { return (&frame->stack[0])[index]; };
  switch (static_cast<Site>(site_index)) {
    case Site::WALK_CALL: {
      if (!round.open) {
        const auto serial = [](SerialReason reason) {
          serial_rounds[static_cast<size_t>(reason)].fetch_add(1, std::memory_order_relaxed);
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
        if (!units.Reserve(std::max(last_round_units + last_round_units / 4, BLOCK_SIZE)) || !segments.Reserve(BLOCK_SIZE)) {
          return serial(SerialReason::ALLOCATION);
        }
        ThreadStackLimits();
        round.open = true;
        round.tracing = worker_pool::Tracing();
        round.type_added = false;
        round.violation = false;
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
        round.watched = {{{entries.random_state, "random state", *entries.random_state},
                          {reinterpret_cast<const uint32_t*>(node + TABLE_OFFSET), "table base", Read(node + TABLE_OFFSET)},
                          {reinterpret_cast<const uint32_t*>(node + TABLE_OFFSET + 4), "table capacity", Read(node + TABLE_OFFSET + 4)}}};
        round.watched_count = 3;
        data->limit.store(std::min(this_thread.stack_size, worker_pool::internal::WORKER_STACK_BYTES), std::memory_order_relaxed);
        data->direct_high.store(0, std::memory_order_relaxed);
        data->walk_active.store(1, std::memory_order_seq_cst);
        const bool started = worker_pool::Start({.work = &WorkerEntry, .user = worker_pool::User::EFFECT_UPDATE, .join = worker_pool::JoinRule::ENTERED, .trace = round.tracing}, workers);
        if (!started) {
          data->walk_active.store(0, std::memory_order_seq_cst);
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
        FinishRound(true);
        return 0;
      }
      AddSegment(id, effect, round.segment_total * RANK_STEP, first, count, round.tail, NONE, false);
      round.published_units.store(first + count, std::memory_order_release);
      round.segment_count.store(id + 1, std::memory_order_release);
      if (round.tail != NONE) segments[round.tail].next = id;
      round.tail = id;
      round.segment_total += 1;
      round.enqueued_units += count;
      Wake();
      return 1;
    }
    case Site::WALK_END:
      FinishRound(false);
      return 0;
    case Site::CHILD_APPEND_A:
    case Site::CHILD_APPEND_B: {
      Unit* const unit = this_thread.current;
      if (unit == nullptr) {
        Count(Counter::FOREIGN_HITS);
        Note(EventKind::FOREIGN_HIT, site_index, 0);
        return 0;
      }
      BecomeOldest(unit, site_index, true);
      const uint32_t effect = frame->ecx - APPEND_VECTOR_OFFSET;
      const uint32_t child = Read(word(1));
      const Segment& current = segments[unit->segment];
      const uint32_t target = FindSegment(effect);
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
      if (controls.late_child_off) return 0;
#endif
      if (target != NONE) {
        if (segments[target].rank > current.rank && !AppendLate(segments[target], target, child)) {
          Count(Counter::LATE_OVERFLOWS);
          Note(EventKind::LATE_OVERFLOW, site_index, target);
          round.violation = true;
        }
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
      uint32_t before = NONE;
      for (uint32_t slot = target_slot + 1; slot < capacity && before == NONE; ++slot) {
        if (const uint32_t other = Read(slots + slot * SLOT_SIZE + SLOT_EFFECT_OFFSET); other != 0) before = FindSegment(other);
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
        round.violation = true;
        return 0;
      }
      AddSegment(id, effect, rank, first, count, previous, before, true);
      round.late_unclaimed.fetch_add(count, std::memory_order_relaxed);
      round.published_units.store(first + count, std::memory_order_release);
      round.segment_count.store(id + 1, std::memory_order_release);
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
    default:
      break;
  }

  // An order point.
  if (this_thread.serving) return 0;
  Unit* const unit = this_thread.current;
  if (unit == nullptr) {
    if (GetCurrentThreadId() == round.owner) {
      // The render thread, in a round, outside a unit: a site the census did not expect there. It goes on as the original (the units
      // in flight may overlap what it does: draining them first could wait for the enumeration this very thread is in the middle
      // of), and B1 is off for the session after the walk.
      Count(Counter::CENSUS_ERRORS);
      Note(EventKind::CENSUS_ERROR, site_index, 0);
      round.violation = true;
    } else {
      Count(Counter::FOREIGN_HITS);
      Note(EventKind::FOREIGN_HIT, site_index, 0);
    }
    return 0;
  }
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  if (controls.no_order_points) return 0;
#endif
  const Site site = static_cast<Site>(site_index);
  const bool first_point = !unit->direct;
  BecomeOldest(unit, site_index, WaitsForEnumeration(site));
  if (round.tracing) {
    order_point_hits[site_index].fetch_add(1, std::memory_order_relaxed);
    if (first_point) first_point_hits[site_index].fetch_add(1, std::memory_order_relaxed);
  }
  if (site == Site::TYPE_ADD_A || site == Site::TYPE_ADD_B) round.type_added = true;
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
inline void Update(bool row) {
  using namespace internal;
  if (data == nullptr) return;
  if (current_round.open) {
    FinishRound(true);
    renodx::utils::log::w("[Total War] Parallel Effect Update: a round was still open at the present; closed.");
  }
  if (entries.update_instance == nullptr) {
    entries.update_instance = reinterpret_cast<decltype(entries.update_instance)>(const_cast<uint8_t*>(base) + INSTANCE_UPDATE_RVA);
    entries.render_switch = reinterpret_cast<decltype(entries.render_switch)>(const_cast<uint8_t*>(base) + RENDER_SWITCH_GETTER_RVA);
    entries.render_switch_object = const_cast<uint8_t*>(base) + RENDER_SWITCH_RVA;
    entries.sweep = reinterpret_cast<decltype(entries.sweep)>(const_cast<uint8_t*>(sweep));
    entries.random_state = reinterpret_cast<const uint32_t*>(base + RANDOM_STATE_RVA);
  }
  SetHandler(&Handle);
  row_on = row;
  if (session_off && !off_said) {
    off_said = true;
    renodx::utils::log::w("[Total War] Parallel Effect Update: off for the session (a safety check failed; see the lines above).");
  }
  if (row && last_round_units < MIN_ROUND_UNITS && ++small_round_presents >= SMALL_ROUND_RETRY_PRESENTS) {
    last_round_units = MIN_ROUND_UNITS;
    small_round_presents = 0;
  }
  data->walk_armed.store(row && !session_off ? 1 : 0, std::memory_order_seq_cst);

  std::string line;
  for (size_t index = 0; index < counters.size(); ++index) {
    if (const uint32_t value = counters[index].exchange(0, std::memory_order_relaxed); value != 0) {
      line += std::format(" {} {},", COUNTER_NAMES[index], value);
    }
  }
  for (size_t index = 0; index < serial_rounds.size(); ++index) {
    if (const uint32_t value = serial_rounds[index].exchange(0, std::memory_order_relaxed); value != 0) {
      line += std::format(" serial ({}) {},", SERIAL_REASON_NAMES[index], value);
    }
  }
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    if (const uint32_t value = order_point_hits[index].exchange(0, std::memory_order_relaxed); value != 0) {
      line += std::format(" {} {} ({} first),", SITES[index].name, value, first_point_hits[index].exchange(0, std::memory_order_relaxed));
    }
  }
  if (const uint32_t count = handovers.exchange(0, std::memory_order_relaxed); count != 0) {
    line += std::format(" handover {:.0f} ns,", static_cast<double>(handover_ticks.exchange(0, std::memory_order_relaxed)) * 1e9
                                                    / static_cast<double>(worker_pool::qpc_frequency) / count);
  }
  if (const uint64_t longest = longest_wait_ticks.exchange(0, std::memory_order_relaxed); longest != 0) {
    line += std::format(" longest wait {:.1f} us,", static_cast<double>(longest) * 1e6 / static_cast<double>(worker_pool::qpc_frequency));
  }
  if (const uint64_t ticks = forward_wait_ticks.exchange(0, std::memory_order_relaxed); ticks != 0) {
    line += std::format(" forwarded wait {:.1f} us,", static_cast<double>(ticks) * 1e6 / static_cast<double>(worker_pool::qpc_frequency));
  }
  if (!line.empty()) {
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
