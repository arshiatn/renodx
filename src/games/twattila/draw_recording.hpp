/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#if !defined(_WIN64)
#include <emmintrin.h>
#include <float.h>
#include <xmmintrin.h>

#include <bit>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/log.hpp"
#include "./cpu_sets.hpp"
#endif

// Threaded Draw Recording (private build, Attila 32-bit): the game's draw loops scan their instance lists on private copies
// of the tracker, on the render thread and on a few workers; every step with a side effect (the flush, the virtual calls
// that write shared state) becomes an op, and the render thread replays the ops in item order, so D3D11, ReShade and the
// add-on callbacks see the calls of the serial loop, on the same thread, in the same order.
// A chunk starts at a mesh change; what the loops carry across a mesh change (the animation index for the depth loop) is
// seeded by a one-item warm-up of the game's own loop and checked at replay. Anything else (a seed miss, an op buffer
// overflow, a site this build does not record, a call that starts from state the chunks can't reproduce) runs that chunk or
// call again serially on the real tracker: slower, never different.
// The thunks that call Record and the sites they replace belong to engine_code.hpp; this file owns the types they use.
// x86 only: on x64 everything is an empty stub.
namespace twattila::draw_recording {

// Row values, in the order of the stages.
enum class Stage : uint8_t { OFF, DIRECT, TOUCH_AHEAD, CHUNKED, RECORDED, ONE_WORKER, WORKERS };

// The loop groups (append only): the four loop groups are the bits of Settings::loops; SHARED is the flush entry's.
enum class Group : uint32_t { DEPTH_A, COLOUR_A, DEPTH_B, COLOUR_B, SHARED, COUNT };

struct Settings {
  Stage stage = Stage::OFF;
  // Threads that scan, the render thread included (2 to 16).
  uint32_t threads = 4;
  uint32_t loops = 1;
  // The thread context_hooks installed on (0: not installed), for the trace's note.
  DWORD context_hooks_thread = 0;
};

#if !defined(_WIN64)

constexpr uint32_t MAX_THREADS = 16;
constexpr uint32_t MAX_WORKERS = MAX_THREADS - 1;
constexpr uint32_t SLOT_SIZE = 256;
// One slot per chunk, one more for the compare run.
constexpr uint32_t SLOT_COUNT = MAX_THREADS + 1;
constexpr uint32_t COMPARE_SLOT = MAX_THREADS;
constexpr uint32_t TRACKER_SIZE = 0xa0;

// The page the thunks read at fixed offsets. Never freed while any site may be written.
struct Shared {
  // Cut stubs: lock inc before the dispatcher, lock dec after.
  std::atomic<int32_t> active{0};
  // The animation index a direct loop call starts with.
  int32_t seed_anim = -1;
  // The last index a direct animation site set.
  int32_t last_anim = -1;
  uint32_t arena = 0;
  uint32_t arena_size = 0;
  uint32_t record = 0;
  uint32_t dispatch_depth = 0;
  uint32_t dispatch_colour = 0;
  // The B groups' per-mesh cut (a later step; 0 until then).
  uint32_t dispatch_mesh = 0;
};
static_assert(std::atomic<int32_t>::is_always_lock_free && sizeof(std::atomic<int32_t>) == 4);
static_assert(offsetof(Shared, active) == 0x00 && offsetof(Shared, seed_anim) == 0x04 && offsetof(Shared, last_anim) == 0x08
              && offsetof(Shared, arena) == 0x0c && offsetof(Shared, arena_size) == 0x10 && offsetof(Shared, record) == 0x14
              && offsetof(Shared, dispatch_depth) == 0x18 && offsetof(Shared, dispatch_colour) == 0x1c
              && offsetof(Shared, dispatch_mesh) == 0x20);

constexpr uint32_t MODE_SUPPRESS = 0;
constexpr uint32_t MODE_RECORD = 1;

// Why a slot's chunk is run again serially instead of replayed.
constexpr uint32_t SLOT_OVERFLOW = 1;
constexpr uint32_t SLOT_UNKNOWN_CLASS = 2;
// The whole call is run again.
constexpr uint32_t SLOT_CONFLICT = 4;

// A chunk's tracker copy: the game's loop runs on it while a thunk finds `esi` inside the arena. The census names no field
// above +0x7c in any group's tracker (every group works on the driver's one 0xa0 byte object): GROUPS[].tracker_bytes.
struct alignas(SLOT_SIZE) Slot {
  std::array<uint8_t, TRACKER_SIZE> tracker = {};
  uint32_t mode = MODE_RECORD;
  // Read by the init sites; written by the animation site's record in both modes.
  int32_t anim = -1;
  // C++ only from here on.
  uint32_t* ops_begin = nullptr;
  uint32_t* ops_cursor = nullptr;
  uint32_t* ops_end = nullptr;
  uint32_t flags = 0;
  // The Group the chunk belongs to (a site of another loop group in it is a bug: the chunk is run again).
  uint32_t group = 0;
  uint32_t chunk = 0;
  uint8_t* real_tracker = nullptr;
  // What the warm-up left: the tracker's first 0x14 bytes (read by colour only) and the animation index.
  std::array<uint8_t, 0x14> seed_fields = {};
  int32_t seed_anim = -1;
};
static_assert(sizeof(Slot) == SLOT_SIZE && offsetof(Slot, tracker) == 0x00 && offsetof(Slot, mode) == 0xa0 && offsetof(Slot, anim) == 0xa4);

// What the thunk leaves on the stack; the game's stack at the site starts right above it.
struct Frame {
  uint32_t site;
  // The low float of xmm0, as bits (read by the constant ops only).
  uint32_t xmm0;
  uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
  uint32_t eflags;
};
static_assert(sizeof(Frame) == 44 && offsetof(Frame, xmm0) == 4 && offsetof(Frame, edi) == 8 && offsetof(Frame, esi) == 12
              && offsetof(Frame, esp) == 20 && offsetof(Frame, ecx) == 32 && offsetof(Frame, eax) == 36 && offsetof(Frame, eflags) == 40);

// One per recording site (append only); the numbers are what the thunks push. The call-site flush ids stay as the fallback
// of group A when the flush entry hook can't be written; FLUSH_ENTRY is the hook's (its slot is arg 4 - 0x69).
enum class Site : uint32_t {
  DEPTH_FLUSH_BOUNDARY,
  DEPTH_FLUSH_END,
  DEPTH_FLUSH_ANIM,
  DEPTH_VF68,
  DEPTH_ANIM,
  COLOUR_FLUSH_BOUNDARY,
  COLOUR_FLUSH_END,
  COLOUR_FLUSH_VARIATION,
  COLOUR_FLUSH_CHANGE,
  COLOUR_SETUP,
  COLOUR_VF68_SETUP,
  COLOUR_VF68_VARIATION,
  COLOUR_SETANY,
  COLOUR_ANIM,
  COLOUR_CONST1,
  COLOUR_CONST2,
  COLOUR_SRV_DIFFUSE,
  COLOUR_SRV_NORMAL,
  COLOUR_FACTION,
  FLUSH_ENTRY,
  COUNT,
};

// The game's addresses, decoded from the sites the engine code found.
struct GameCalls {
  // The loops' entry points, by Group (the B groups call meshes' own bodies: 0).
  std::array<uint32_t, static_cast<size_t>(Group::COUNT)> loops = {};
  uint32_t flush = 0;
  uint32_t anim_call = 0;
  uint32_t dirty_call = 0;
  uint32_t view_call = 0;
  uint32_t srv_call = 0;
  uint32_t faction_call = 0;
  const uint8_t* cached_constants = nullptr;
  const float* faction_default = nullptr;
  std::array<uint32_t, 10> weighted_vtables = {};
  // False: a pattern did not match or an entry jump is in; the recording stands down whole.
  bool usable = false;
};

#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
constexpr bool TEST_SWITCHES = true;
#else
constexpr bool TEST_SWITCHES = false;
#endif

namespace internal {

// The kinds of op; 0 is a site this build does not record (its chunk is run again).
enum class OpKind : uint16_t { NONE, FLUSH, VF68, ANIM, COUNT };

struct SiteInfo {
  Group group;
  OpKind kind;
  // The animation site's index on the game's stack (stack[0] is the return address).
  uint32_t anim_stack;
};
constexpr std::array<SiteInfo, static_cast<size_t>(Site::COUNT)> SITES = {{
    {Group::DEPTH_A, OpKind::FLUSH, 0},
    {Group::DEPTH_A, OpKind::FLUSH, 0},
    {Group::DEPTH_A, OpKind::FLUSH, 0},
    {Group::DEPTH_A, OpKind::VF68, 0},
    {Group::DEPTH_A, OpKind::ANIM, 9},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::COLOUR_A, OpKind::NONE, 0},
    {Group::SHARED, OpKind::FLUSH, 0},
}};

// Payloads, in dwords. An op is a header (kind in the low 16 bits, the op's size in dwords, header included, in the high 16)
// and its payload.
constexpr uint32_t FLUSH_PAYLOAD = 14;  // mesh, 10 arguments, mesh +0x40, mesh +0x44, the tracker's +0x69 byte (any)
constexpr uint32_t VF68_PAYLOAD = 5;    // mesh, 2 arguments, mesh +0x40, mesh +0x44
constexpr uint32_t ANIM_PAYLOAD = 3;    // state, value, index
// The most bytes the ops of one depth item take: its flush, the shared-state call and an animation change.
constexpr uint32_t DEPTH_OP_BYTES_PER_ITEM = 4 * (1 + FLUSH_PAYLOAD + 1 + VF68_PAYLOAD + 1 + ANIM_PAYLOAD);

struct CopyRange {
  uint32_t begin;
  uint32_t end;
};
// What replay copies from a replayed chunk's tracker to the real one, per group. Besides these: the stride at +0x50 only
// when the chunk wrote one (the driver reads it; the slot's is zeroed when it is filled and after the warm-up, so non-zero
// means "written here", and the last chunk to write wins as in the serial order), and +0x69 ORed in.
struct GroupInfo {
  uint32_t tracker_bytes;
  std::array<CopyRange, 4> copy_back;
  uint32_t op_bytes_per_item;
};
constexpr GroupInfo SAME_TRACKER = {TRACKER_SIZE, {{{0x00, 0x18}, {0x58, 0x5c}, {0x68, 0x69}, {0x6c, 0x7c}}}, DEPTH_OP_BYTES_PER_ITEM};
constexpr std::array<GroupInfo, static_cast<size_t>(Group::COUNT)> GROUPS = {SAME_TRACKER, SAME_TRACKER, SAME_TRACKER, SAME_TRACKER, SAME_TRACKER};
constexpr uint32_t STRIDE_OFFSET = 0x50;
constexpr uint32_t ANY_OFFSET = 0x69;

// Test switches (compiled in only with TWATTILA_DRAW_RECORDING_CONTROLS): each turns one safeguard off, and the test checks
// that the run then differs.
struct Controls {
  bool validate_seeds = true;
  bool translate_pointers = true;
  bool guard_continuation = true;
  // Trace frames compare only in the last frame of a trace (the others are timed); the tests compare every call.
  bool compare_every_call = false;
  bool capture_any = true;
  bool stride_non_zero = true;
  bool fail_thread_creation = false;
};
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
static Controls controls;
#else
constexpr Controls controls;
#endif

}  // namespace internal

// The thunk's call (thiscall `esi` = the tracker, which is the slot's first byte): appends the site's op in RECORD mode,
// keeps the slot's animation index and the tracker's +0x69 byte in both modes, and sets the results the loop expects (the
// shared-state virtual call returns 0).
inline void __fastcall Record(Slot* slot, Frame* frame) noexcept {
  using internal::OpKind;
  if (frame->site >= static_cast<uint32_t>(Site::COUNT)) {
    slot->flags |= SLOT_UNKNOWN_CLASS;
    return;
  }
  const internal::SiteInfo& site = internal::SITES[frame->site];
  if (site.group != Group::SHARED && static_cast<uint32_t>(site.group) != slot->group) {
    slot->flags |= SLOT_UNKNOWN_CLASS;
    return;
  }
  const uint32_t* stack = reinterpret_cast<const uint32_t*>(frame + 1);
  const bool recording = (slot->mode == MODE_RECORD);
  const auto* mesh = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(frame->ecx));
  // Returns the payload, or null when the buffer is full (the chunk is then run again).
  const auto append = [slot](OpKind kind, uint32_t payload_dwords) noexcept -> uint32_t* {
    const uint32_t total = payload_dwords + 1;
    if ((slot->flags & SLOT_OVERFLOW) != 0 || static_cast<size_t>(slot->ops_end - slot->ops_cursor) < total) {
      slot->flags |= SLOT_OVERFLOW;
      return nullptr;
    }
    uint32_t* op = slot->ops_cursor;
    op[0] = static_cast<uint32_t>(kind) | (total << 16);
    slot->ops_cursor += total;
    return op + 1;
  };
  switch (site.kind) {
    case OpKind::FLUSH: {
      // A flush of no instances returns at once in the game and leaves *any alone: dropped (exact).
      if (stack[5] == 0) return;
      // The flush reads and clears *any (the tracker's +0x69); the copy's is taken here, in both modes, and carried in the
      // op: the replay ORs it into the real byte just before the flush.
      uint32_t any = 0;
      if (internal::controls.capture_any) {
        any = slot->tracker[internal::ANY_OFFSET];
        slot->tracker[internal::ANY_OFFSET] = 0;
      }
      if (!recording) return;
      uint32_t* payload = append(OpKind::FLUSH, internal::FLUSH_PAYLOAD);
      if (payload == nullptr) return;
      const uint32_t slot_address = reinterpret_cast<uint32_t>(slot);
      const uint32_t real_address = reinterpret_cast<uint32_t>(slot->real_tracker);
      const uint32_t tracker_bytes = internal::GROUPS[slot->group].tracker_bytes;
      payload[0] = frame->ecx;
      // The pointers into the tracker copy are the real tracker's for the replay.
      for (uint32_t i = 0; i < 10; ++i) {
        const uint32_t argument = stack[1 + i];
        payload[1 + i] = ((internal::controls.translate_pointers && argument - slot_address < tracker_bytes) ? real_address + (argument - slot_address) : argument);
      }
      std::memcpy(payload + 11, mesh + 0x40, 4);
      std::memcpy(payload + 12, mesh + 0x44, 4);
      payload[13] = any;
      return;
    }
    case OpKind::VF68: {
      frame->eax = 0;
      if (!recording) return;
      uint32_t* payload = append(OpKind::VF68, internal::VF68_PAYLOAD);
      if (payload == nullptr) return;
      payload[0] = frame->ecx;
      payload[1] = stack[1];
      payload[2] = stack[2];
      std::memcpy(payload + 3, mesh + 0x40, 4);
      std::memcpy(payload + 4, mesh + 0x44, 4);
      return;
    }
    case OpKind::ANIM: {
      const uint32_t index = stack[site.anim_stack];
      slot->anim = static_cast<int32_t>(index);
      if (!recording) return;
      uint32_t* payload = append(OpKind::ANIM, internal::ANIM_PAYLOAD);
      if (payload == nullptr) return;
      payload[0] = frame->ecx;
      payload[1] = stack[1];
      payload[2] = index;
      return;
    }
    default:
      slot->flags |= SLOT_UNKNOWN_CLASS;
      return;
  }
}

namespace internal {

using DepthLoopFn = void(__thiscall*)(void* node, void* tracker, void* list, uint32_t begin, uint32_t end);
using FlushFn = void(__thiscall*)(void* mesh, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Vf68Fn = uint8_t(__thiscall*)(void* mesh, uint32_t, uint32_t);
using AnimCallFn = void(__thiscall*)(void* state, uint32_t value);

// One call of a loop, as the dispatcher got it.
struct Call {
  Group group = Group::DEPTH_A;
  uint32_t loop = 0;
  void* node = nullptr;
  void* list = nullptr;
  uint32_t unused = 0;
};

// What the workers are woken to do.
enum class WorkKind : uint8_t { RECORD_LOOP, RECORD_MESHES, TOUCH };

// The B groups' round: a range of meshes of one driver round, the arguments every mesh body gets (a later step).
struct MeshRound {
  void* device = nullptr;
  void* view = nullptr;
  uint32_t list_type = 0;
  void* node = nullptr;
  void* sorted = nullptr;
};

// The hand-off to the workers: written by the render thread before it wakes anyone.
struct alignas(64) Descriptor {
  void (*work)(const Descriptor&, uint32_t worker) = nullptr;
  WorkKind kind = WorkKind::RECORD_LOOP;
  // Workers woken for this dispatch.
  uint32_t workers = 0;
  uint32_t x87 = 0;
  uint32_t sse = 0;
  Call call;
  MeshRound meshes;
  uint32_t chunks = 0;
  bool one_worker = false;
  std::array<uint32_t, MAX_THREADS + 1> cuts = {};
  // Touch-ahead: the range, and the stop generation the helpers poll.
  uint32_t touch_begin = 0;
  uint32_t touch_end = 0;
  uint32_t touch_generation = 0;
};

struct alignas(64) PoolWorker {
  std::atomic<uint32_t> ticket{0};
  HANDLE thread = nullptr;
};

// What the dispatcher reads on the render thread (Update writes it there too).
struct Config {
  Stage stage = Stage::OFF;
  uint32_t threads = 4;
  uint32_t loops = 1;
};

enum class CompareResult : uint8_t { NONE, EQUAL, SLOT_NOT_COMPARABLE, CHUNK_RERUN, DIFFERS, COMPARE_HAS_MORE };

constexpr uint32_t NO_FAMILY = 0xff;

// Phases of one call, in QPC ticks. `total` leaves out what only trace frames do (the compare run, the mesh run length scan,
// the warm-ups of Chunked), so that the sum of the calls of a trace frame is what the stage costs in a normal frame.
struct CallTrace {
  Group group = Group::DEPTH_A;
  // The B family of the call (0 to 7; NO_FAMILY: not a B call).
  uint32_t family = NO_FAMILY;
  uint32_t items = 0;
  uint32_t threads = 0;
  uint32_t chunks = 0;
  std::array<uint32_t, MAX_THREADS> chunk_items = {};
  // The call ran the compare (its times are not a normal frame's: read timings only from calls without it).
  bool compared = false;
  Stage stage = Stage::OFF;
  const char* direct_reason = "";
  uint32_t cut_targets = 0;
  uint32_t cut_probes = 0;
  uint32_t cut_failures = 0;
  // Mesh run lengths: 1, 2-3, 4-7, ... 256+.
  std::array<uint32_t, 9> runs = {};
  std::array<uint32_t, static_cast<size_t>(OpKind::COUNT)> ops = {};
  uint32_t op_bytes = 0;
  uint32_t op_buffer_bytes = 0;
  uint32_t seed_hits = 0;
  uint32_t seed_misses = 0;
  uint32_t overflows = 0;
  uint32_t unknown_classes = 0;
  uint32_t conflicts = 0;
  uint32_t reruns = 0;
  uint64_t direct = 0;
  uint64_t cut = 0;
  uint64_t warm_ups = 0;
  uint64_t chunk_zero = 0;
  uint64_t join_wait = 0;
  uint64_t replay = 0;
  uint64_t rerun = 0;
  uint64_t total = 0;
  // Trace frames only.
  uint64_t compare = 0;
  uint64_t run_scan = 0;
  uint64_t wall = 0;
  CompareResult compare_result = CompareResult::NONE;
  uint32_t compare_op = 0;
  uint32_t compare_kind = 0;
};

struct FrameTrace {
  uint64_t call_ticks = 0;
  bool thread_matched = true;
};

// The one-frame trace: armed by RENODX_TW_DRAW_TRACE=<seconds after the first present>[,<frames>]. Render thread only.
struct Trace {
  bool armed = false;
  bool active = false;
  bool finished = false;
  bool read_environment = false;
  uint64_t arm_ticks = 0;
  uint32_t frames = 3;
  uint32_t frames_left = 0;
  uint64_t first_present = 0;
  uint64_t frame_call_ticks = 0;
  std::vector<CallTrace> calls;
  std::vector<FrameTrace> frame_list;
};

// What the last call did (the tests read it).
struct LastCall {
  uint32_t chunks = 0;
  uint32_t reruns = 0;
  uint32_t replayed = 0;
  uint32_t ops = 0;
  bool direct = false;
};

// Op memory is one address range, committed as a call needs it. The chunks of a call tile its items, so their buffers (the
// group's worst case bytes an item, plus the end flush) take consecutive parts of one buffer of the whole call; the compare run's
// follows. A part that does not fit makes that chunk overflow, which runs it again (exact, slower).
constexpr uint32_t OP_ARENA_BYTES = 32 * 1024 * 1024;
constexpr uint32_t OP_SLACK_BYTES = 128;
// Calls with fewer items run directly.
constexpr uint32_t MIN_ITEMS = 512;
// How far past its target a cut looks for a mesh change, and the shortest chunk.
constexpr uint32_t CUT_SEARCH = 256;
constexpr uint32_t MIN_CHUNK = 64;
constexpr uint32_t PREFETCH_AHEAD = 8;
constexpr uint32_t WORKER_STACK_BYTES = 512 * 1024;

static GameCalls game_calls;
static Config config;
static LastCall last_call;
static Trace trace;
static std::atomic<DWORD> present_thread = 0;
static std::atomic<bool> detached = false;
static bool allocation_failed = false;
// The stage and K a worker thread could not be created for (no retry until they change).
static uint32_t pool_failed_key = ~0u;
static bool kept_logged = false;
static Shared* shared = nullptr;
static uint8_t* arena = nullptr;
static uint8_t* op_memory = nullptr;
static uint32_t op_committed = 0;
static HMODULE module_reference = nullptr;
// The most op bytes a chunk may use (tests lower it to force overflows).
static uint32_t op_buffer_limit = OP_ARENA_BYTES;
static uint32_t thread_attempts = 0;

static std::array<PoolWorker, MAX_WORKERS> pool;
static uint32_t worker_count = 0;
alignas(64) static std::atomic<uint32_t> done = 0;
static std::atomic<bool> stopping = false;
static std::atomic<uint32_t> touch_generation = 0;
alignas(64) static Descriptor descriptor;
// The generation of cpu_sets' published sets the pool threads were given (all ones: none yet).
static uint32_t given_generation = ~0u;

static uint64_t qpc_frequency = [] {
  LARGE_INTEGER frequency = {};
  QueryPerformanceFrequency(&frequency);
  return static_cast<uint64_t>(frequency.QuadPart);
}();

inline uint64_t Qpc() {
  LARGE_INTEGER counter = {};
  QueryPerformanceCounter(&counter);
  return static_cast<uint64_t>(counter.QuadPart);
}

inline Slot* SlotAt(uint32_t index) { return reinterpret_cast<Slot*>(arena + static_cast<size_t>(index) * SLOT_SIZE); }

inline void RunLoop(const Call& call, void* tracker, uint32_t begin, uint32_t end) {
  switch (call.group) {
    case Group::DEPTH_A:
      reinterpret_cast<DepthLoopFn>(static_cast<uintptr_t>(call.loop))(call.node, tracker, call.list, begin, end);
      break;
    default:
      // No-op: the other groups arrive with their loops; the dispatcher never gets here for them.
      break;
  }
}

// A worker records its chunk: worker w runs chunk w + 1, or, one worker alone, chunks 1 to n - 1 in order.
inline void RecordWork(const Descriptor& d, uint32_t worker) {
  if (d.kind != WorkKind::RECORD_LOOP) {
    // The B groups' mesh rounds arrive with their step: until then a chunk given to a worker for them is run again.
    SlotAt(worker + 1)->flags |= SLOT_UNKNOWN_CLASS;
    return;
  }
  if (d.one_worker) {
    for (uint32_t chunk = 1; chunk < d.chunks; ++chunk) {
      RunLoop(d.call, SlotAt(chunk), d.cuts[chunk], d.cuts[chunk + 1]);
    }
    return;
  }
  RunLoop(d.call, SlotAt(worker + 1), d.cuts[worker + 1], d.cuts[worker + 2]);
}

// Touch-ahead: helper h of H reads blocks of 16 items from begin + 64 + 16 h on, stride 16 H, until the end or the stop.
// Reads only; the loop itself runs unchanged on the render thread.
inline void TouchWork(const Descriptor& d, uint32_t worker) {
  const uint32_t* data = *reinterpret_cast<uint32_t* const*>(static_cast<uint8_t*>(d.call.list) + 8);
  uint32_t sink = 0;
  for (uint32_t block = d.touch_begin + 64 + 16 * worker; block < d.touch_end; block += 16 * d.workers) {
    if (touch_generation.load(std::memory_order_relaxed) != d.touch_generation) break;
    for (uint32_t i = block; i < std::min(block + 16, d.touch_end); ++i) {
      const auto* item = reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(data[i]));
      sink += item[0x00 / 4] + item[0x3c / 4] + item[0x5c / 4] + item[0xec / 4];
      const auto* mesh = reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(item[0]));
      sink += mesh[0x00 / 4] + mesh[0x40 / 4] + mesh[0x44 / 4];
    }
  }
  static volatile uint32_t touched = 0;
  touched = touched + sink;
}

// A worker: spins about 3 us for its next ticket, then waits on it; takes the dispatching thread's x87 and SSE control
// words every dispatch (game code on a worker may have changed its own and not restored it); runs the descriptor's work;
// the last one to finish wakes the join.
static DWORD WINAPI WorkerMain(void* context) {
  PoolWorker& self = pool[reinterpret_cast<uintptr_t>(context)];
  const uint32_t index = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context));
  const uint64_t spin_ticks = std::max<uint64_t>(1, qpc_frequency * 3 / 1000000);
  uint32_t seen = 0;
  for (;;) {
    uint32_t ticket = self.ticket.load(std::memory_order_acquire);
    if (ticket == seen) {
      for (const uint64_t start = Qpc(); ticket == seen && Qpc() - start < spin_ticks; ticket = self.ticket.load(std::memory_order_acquire)) {
        _mm_pause();
      }
      while (ticket == seen) {
        self.ticket.wait(seen);
        ticket = self.ticket.load(std::memory_order_acquire);
      }
    }
    seen = ticket;
    if (stopping.load(std::memory_order_acquire)) return 0;
    const Descriptor& d = descriptor;
    unsigned int ignored = 0;
    __control87_2(d.x87, _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC, &ignored, nullptr);
    __control87_2(d.sse, _MCW_EM | _MCW_RC | _MCW_DN, nullptr, &ignored);
    const uint32_t woken = d.workers;
    d.work(d, index);
    if (done.fetch_add(1, std::memory_order_release) + 1 == woken) {
      done.notify_one();
    }
  }
}

// Render thread: wakes the first `count` workers for the filled descriptor, runs `render_part`, and joins (a short spin,
// then a wait; no timeout: a worker can't be stopped inside the game's code). `join_ticks` (optional) gets the join wait.
template <typename Part>
inline void RunOnWorkers(uint32_t count, Part&& render_part, uint64_t* join_ticks) {
  descriptor.workers = count;
  __control87_2(0, 0, &descriptor.x87, &descriptor.sse);
  done.store(0, std::memory_order_relaxed);
  for (uint32_t w = 0; w < count; ++w) {
    pool[w].ticket.fetch_add(1, std::memory_order_release);
    pool[w].ticket.notify_one();
  }
  render_part();
  const uint64_t join_start = Qpc();
  const uint64_t spin_ticks = qpc_frequency * 2 / 1000;
  while (done.load(std::memory_order_acquire) != count && Qpc() - join_start < spin_ticks) {
    _mm_pause();
  }
  for (uint32_t finished = done.load(std::memory_order_acquire); finished != count; finished = done.load(std::memory_order_acquire)) {
    done.wait(finished);
  }
  if (join_ticks != nullptr) {
    *join_ticks += Qpc() - join_start;
  }
}

// Ends the workers and joins them (Update with the row off, Uninstall; never DllMain).
inline void StopPool() {
  if (worker_count == 0) return;
  stopping.store(true, std::memory_order_release);
  std::array<HANDLE, MAX_WORKERS> handles = {};
  for (uint32_t w = 0; w < worker_count; ++w) {
    handles[w] = pool[w].thread;
    pool[w].ticket.fetch_add(1, std::memory_order_release);
    pool[w].ticket.notify_one();
  }
  WaitForMultipleObjects(worker_count, handles.data(), TRUE, INFINITE);
  for (uint32_t w = 0; w < worker_count; ++w) {
    CloseHandle(handles[w]);
    pool[w].thread = nullptr;
  }
  worker_count = 0;
  given_generation = ~0u;
}

// The shared implementation of the loop dispatchers (one per group).
inline void Dispatch(Group group, void* node, void* tracker, void* list, uint32_t unused, uint32_t begin, uint32_t end) {
  const Config settings = config;
  const uint32_t group_index = static_cast<uint32_t>(group);
  const Call call = {.group = group, .loop = game_calls.loops[group_index], .node = node, .list = list, .unused = unused};
  const uint32_t items = (end > begin ? end - begin : 0);
  CallTrace* const trace_call = (trace.active ? &trace.calls.emplace_back() : nullptr);
  const bool compare_now = (trace_call != nullptr && (controls.compare_every_call || trace.frames_left == 1));
  uint64_t last_lap = (trace_call != nullptr ? Qpc() : 0);
  const uint64_t call_start = last_lap;
  // Work only trace frames do: left out of the call's total.
  uint64_t trace_only_ticks = 0;
  // Adds the time since the last lap to a phase of the trace.
  const auto lap = [&](uint64_t CallTrace::* phase, bool trace_only = false) {
    if (trace_call == nullptr) return;
    const uint64_t now = Qpc();
    trace_call->*phase += now - last_lap;
    if (trace_only) {
      trace_only_ticks += now - last_lap;
    }
    last_lap = now;
  };
  if (trace_call != nullptr) {
    trace_call->group = group;
    trace_call->items = items;
    trace_call->threads = settings.threads;
    trace_call->stage = settings.stage;
    trace_call->compared = compare_now;
  }
  const uint32_t* data = (items != 0 ? *reinterpret_cast<uint32_t* const*>(static_cast<uint8_t*>(list) + 8) : nullptr);
  const auto mesh_of = [data](uint32_t i) { return *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(data[i])); };
  const auto finish = [&](const char* direct_reason) {
    if (trace_call != nullptr) {
      if (items != 0) {
        // Mesh run lengths over the whole call: trace-only, after everything else the call does, so it warms nothing.
        uint32_t run = 1;
        for (uint32_t i = begin + 1; i <= end; ++i) {
          if (i < end && mesh_of(i) == mesh_of(i - 1)) {
            ++run;
            continue;
          }
          ++trace_call->runs[std::min<uint32_t>(std::bit_width(run) - 1, trace_call->runs.size() - 1)];
          run = 1;
        }
      }
      lap(&CallTrace::run_scan, true);
      trace_call->direct_reason = (direct_reason != nullptr ? direct_reason : "");
      trace_call->wall = Qpc() - call_start;
      trace_call->total = trace_call->wall - trace_only_ticks;
      trace.frame_call_ticks += trace_call->total;
    }
  };
  if constexpr (TEST_SWITCHES) {
    last_call = {.direct = true};
  }

  // The workers this stage wakes.
  uint32_t helpers = 0;
  switch (settings.stage) {
    case Stage::TOUCH_AHEAD:
    case Stage::WORKERS:
      helpers = settings.threads - 1;
      break;
    case Stage::ONE_WORKER:
      helpers = 1;
      break;
    default:
      break;
  }
  const auto read32 = [](const void* base, uint32_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, 4);
    return value;
  };
  const char* direct_reason = nullptr;
  if (!game_calls.usable) {
    direct_reason = "not usable";
  } else if (detached.load(std::memory_order_relaxed)) {
    direct_reason = "detached";
  } else if (settings.stage <= Stage::DIRECT) {
    direct_reason = "stage";
  } else if (((settings.loops >> group_index) & 1) == 0) {
    direct_reason = "loop off";
  } else if (GetCurrentThreadId() != present_thread.load(std::memory_order_relaxed)) {
    direct_reason = "not the present thread";
  } else if (items < MIN_ITEMS) {
    direct_reason = "small call";
  } else if (worker_count < helpers) {
    direct_reason = "pool missing";
  } else if (read32(tracker, 0x58) != 0 || read32(tracker, 0x78) != 0) {
    // The chunks start with no pending instances and no change mask, as a call the loop itself ended does.
    direct_reason = "entry state";
  } else if (controls.guard_continuation && settings.stage >= Stage::CHUNKED && mesh_of(begin) == read32(tracker, 0x70) && read32(tracker, 0x14) == 0
             && (read32(reinterpret_cast<const void*>(static_cast<uintptr_t>(mesh_of(begin))), 0x40) != read32(tracker, 0x20)
                 || read32(reinterpret_cast<const void*>(static_cast<uintptr_t>(mesh_of(begin))), 0x44) != 0)) {
    // The call continues a mesh whose state is not what this tracker's setup would write (another view's, the colour
    // loop's): no setup precedes its first flush, so a snapshot taken while a chunk sets the mesh up would differ.
    direct_reason = "continuation";
  }
  if (direct_reason != nullptr) {
    RunLoop(call, tracker, begin, end);
    lap(&CallTrace::direct);
    finish(direct_reason);
    return;
  }

  if (settings.stage == Stage::TOUCH_AHEAD) {
    descriptor.work = TouchWork;
    descriptor.kind = WorkKind::TOUCH;
    descriptor.call = call;
    descriptor.touch_begin = begin;
    descriptor.touch_end = end;
    descriptor.touch_generation = touch_generation.load(std::memory_order_relaxed);
    RunOnWorkers(helpers, [&] {
      RunLoop(call, tracker, begin, end);
      touch_generation.fetch_add(1, std::memory_order_relaxed);
    }, (trace_call != nullptr ? &trace_call->join_wait : nullptr));
    lap(&CallTrace::chunk_zero);
    if constexpr (TEST_SWITCHES) {
      last_call.direct = false;
    }
    finish(nullptr);
    return;
  }

  // The cut: targets begin + j * items / K, each moved forward (at most CUT_SEARCH items) to the first mesh change.
  std::array<uint32_t, MAX_THREADS + 1> cuts = {begin};
  uint32_t chunks = 1;
  for (uint32_t j = 1; j < settings.threads; ++j) {
    const uint32_t target = begin + static_cast<uint32_t>(static_cast<uint64_t>(items) * j / settings.threads);
    const uint32_t limit = std::min(target + CUT_SEARCH, end);
    uint32_t cut = 0;
    for (uint32_t i = std::max(target, cuts[chunks - 1] + 1); i < limit; ++i) {
      if (i + PREFETCH_AHEAD < end) {
        _mm_prefetch(reinterpret_cast<const char*>(static_cast<uintptr_t>(data[i + PREFETCH_AHEAD])), _MM_HINT_T0);
      }
      if (trace_call != nullptr) {
        ++trace_call->cut_probes;
      }
      if (mesh_of(i) != mesh_of(i - 1)) {
        cut = i;
        break;
      }
    }
    if (trace_call != nullptr) {
      ++trace_call->cut_targets;
    }
    if (cut != 0 && cut - cuts[chunks - 1] >= MIN_CHUNK && end - cut >= MIN_CHUNK) {
      cuts[chunks++] = cut;
    } else if (trace_call != nullptr) {
      ++trace_call->cut_failures;
    }
  }
  cuts[chunks] = end;
  lap(&CallTrace::cut);
  if (trace_call != nullptr) {
    trace_call->chunks = chunks;
    for (uint32_t k = 0; k < chunks; ++k) {
      trace_call->chunk_items[k] = cuts[k + 1] - cuts[k];
    }
  }
  if (chunks == 1) {
    RunLoop(call, tracker, begin, end);
    lap(&CallTrace::direct);
    finish("no cut");
    return;
  }
  if constexpr (TEST_SWITCHES) {
    last_call = {.chunks = chunks};
  }

  // The slots: a copy of the real tracker each, an empty op buffer sized for the chunk, the next part of the op memory
  // (committed as needed).
  uint32_t op_used = 0;
  const auto prepare_slot = [&](uint32_t index, uint32_t chunk_items) {
    Slot* slot = SlotAt(index);
    std::memcpy(slot->tracker.data(), tracker, GROUPS[group_index].tracker_bytes);
    // The stride and the pending "any" flag are this chunk's own from here on.
    std::memset(slot->tracker.data() + STRIDE_OFFSET, 0, 4);
    slot->tracker[ANY_OFFSET] = 0;
    slot->mode = MODE_RECORD;
    slot->anim = -1;
    const uint32_t wanted_bytes = chunk_items * GROUPS[group_index].op_bytes_per_item + OP_SLACK_BYTES;
    uint32_t bytes = std::min({wanted_bytes, op_buffer_limit, OP_ARENA_BYTES - op_used}) & ~3u;
    if (op_used + bytes > op_committed) {
      const uint32_t target = (op_used + bytes + 4095) & ~4095u;
      if (VirtualAlloc(op_memory + op_committed, target - op_committed, MEM_COMMIT, PAGE_READWRITE) != nullptr) {
        op_committed = target;
      } else {
        bytes = (op_committed - op_used) & ~3u;
      }
    }
    slot->ops_begin = reinterpret_cast<uint32_t*>(op_memory + op_used);
    op_used += bytes;
    slot->ops_cursor = slot->ops_begin;
    slot->ops_end = slot->ops_begin + bytes / 4;
    slot->flags = 0;
    slot->group = group_index;
    slot->chunk = index;
    slot->real_tracker = static_cast<uint8_t*>(tracker);
    if (trace_call != nullptr && index != COMPARE_SLOT) {
      trace_call->op_buffer_bytes = std::max(trace_call->op_buffer_bytes, bytes);
    }
    return slot;
  };
  if (settings.stage >= Stage::RECORDED || compare_now) {
    for (uint32_t k = 0; k < chunks; ++k) {
      prepare_slot(k, cuts[k + 1] - cuts[k]);
    }
    // Warm-up of chunk k: the game's own loop over the item before its first, suppressed; what it leaves is the seed.
    for (uint32_t k = 1; k < chunks; ++k) {
      Slot* slot = SlotAt(k);
      slot->mode = MODE_SUPPRESS;
      RunLoop(call, slot, cuts[k] - 1, cuts[k]);
      std::memcpy(slot->seed_fields.data(), slot->tracker.data(), slot->seed_fields.size());
      slot->seed_anim = slot->anim;
      slot->mode = MODE_RECORD;
      slot->flags = 0;
      std::memset(slot->tracker.data() + STRIDE_OFFSET, 0, 4);
      slot->tracker[ANY_OFFSET] = 0;
    }
    // Chunked does these in compared trace frames only (to count seed hits).
    lap(&CallTrace::warm_ups, settings.stage == Stage::CHUNKED);
  }

  if (settings.stage == Stage::CHUNKED) {
    // One chunk after the other on the real tracker, each started with the animation index the one before ended with.
    shared->last_anim = -1;
    for (uint32_t k = 0; k < chunks; ++k) {
      shared->seed_anim = (k == 0 ? -1 : shared->last_anim);
      if (compare_now && k > 0) {
        if (SlotAt(k)->seed_anim == shared->seed_anim) {
          ++trace_call->seed_hits;
        } else {
          ++trace_call->seed_misses;
        }
      }
      RunLoop(call, tracker, cuts[k], cuts[k + 1]);
    }
    shared->seed_anim = -1;
    lap(&CallTrace::chunk_zero);
    if constexpr (TEST_SWITCHES) {
      last_call.direct = false;
    }
    finish(nullptr);
    return;
  }

  // The in-frame compare (trace frames): the same call recorded whole in the compare slot, before anything changes.
  Slot* compare_slot = nullptr;
  if (compare_now) {
    compare_slot = prepare_slot(COMPARE_SLOT, items);
    RunLoop(call, compare_slot, begin, end);
    lap(&CallTrace::compare, true);
  }

  // Record: all chunks here (Recorded), or chunk 0 here and the rest on the workers.
  if (settings.stage == Stage::RECORDED) {
    for (uint32_t k = 0; k < chunks; ++k) {
      RunLoop(call, SlotAt(k), cuts[k], cuts[k + 1]);
    }
  } else {
    descriptor.work = RecordWork;
    descriptor.kind = WorkKind::RECORD_LOOP;
    descriptor.call = call;
    descriptor.chunks = chunks;
    descriptor.one_worker = (settings.stage == Stage::ONE_WORKER);
    descriptor.cuts = cuts;
    RunOnWorkers((settings.stage == Stage::ONE_WORKER ? 1 : chunks - 1), [&] { RunLoop(call, SlotAt(0), cuts[0], cuts[1]); },
                 (trace_call != nullptr ? &trace_call->join_wait : nullptr));
  }
  lap(&CallTrace::chunk_zero);

  bool conflict = false;
  for (uint32_t k = 0; k < chunks; ++k) {
    conflict = conflict || (SlotAt(k)->flags & SLOT_CONFLICT) != 0;
  }
  if (conflict) {
    if (trace_call != nullptr) {
      ++trace_call->conflicts;
    }
    RunLoop(call, tracker, begin, end);
    lap(&CallTrace::rerun);
    finish("conflict");
    return;
  }

  // Replay in item order; a chunk that can't be trusted runs again, serially, on the real tracker.
  auto* const real = static_cast<uint8_t*>(tracker);
  int32_t real_last = -1;
  uint32_t replayed = 0;
  uint32_t reruns = 0;
  uint32_t op_count = 0;
  const uint32_t* compare_cursor = (compare_slot != nullptr ? compare_slot->ops_begin : nullptr);
  CompareResult compare_result = CompareResult::NONE;
  if (compare_slot != nullptr) {
    compare_result = ((compare_slot->flags & (SLOT_OVERFLOW | SLOT_UNKNOWN_CLASS)) != 0 ? CompareResult::SLOT_NOT_COMPARABLE : CompareResult::EQUAL);
  }
  uint32_t compared_ops = 0;
  uint32_t compare_kind = 0;
  for (uint32_t k = 0; k < chunks; ++k) {
    Slot* slot = SlotAt(k);
    const bool flagged = (slot->flags & (SLOT_OVERFLOW | SLOT_UNKNOWN_CLASS)) != 0;
    const bool seed_missed = (k > 0 && slot->seed_anim != real_last);
    if (trace_call != nullptr) {
      trace_call->overflows += ((slot->flags & SLOT_OVERFLOW) != 0 ? 1 : 0);
      trace_call->unknown_classes += ((slot->flags & SLOT_UNKNOWN_CLASS) != 0 ? 1 : 0);
      if (k > 0) {
        if (seed_missed) {
          ++trace_call->seed_misses;
        } else {
          ++trace_call->seed_hits;
        }
      }
    }
    if (flagged || (seed_missed && controls.validate_seeds)) {
      lap(&CallTrace::replay);
      shared->seed_anim = real_last;
      shared->last_anim = real_last;
      RunLoop(call, tracker, cuts[k], cuts[k + 1]);
      real_last = shared->last_anim;
      shared->seed_anim = -1;
      ++reruns;
      lap(&CallTrace::rerun);
      if (compare_result == CompareResult::EQUAL) {
        compare_result = CompareResult::CHUNK_RERUN;
      }
      continue;
    }
    if (compare_cursor != nullptr && compare_result == CompareResult::EQUAL) {
      // This chunk's stream must be the compare run's next bytes.
      const uint32_t* stream = slot->ops_begin;
      while (stream < slot->ops_cursor) {
        const uint32_t header = *stream;
        const uint32_t size = header >> 16;
        if (compare_cursor >= compare_slot->ops_cursor || *compare_cursor != header || std::memcmp(compare_cursor, stream, size * 4) != 0) {
          compare_result = CompareResult::DIFFERS;
          compare_kind = header & 0xffff;
          break;
        }
        compare_cursor += size;
        stream += size;
        ++compared_ops;
      }
    }
    for (const uint32_t* op = slot->ops_begin; op < slot->ops_cursor; op += (*op >> 16)) {
      const uint32_t* payload = op + 1;
      const auto kind = static_cast<OpKind>(*op & 0xffff);
      ++op_count;
      if (trace_call != nullptr) {
        ++trace_call->ops[std::min<size_t>(static_cast<size_t>(kind), trace_call->ops.size() - 1)];
        trace_call->op_bytes += (*op >> 16) * 4;
      }
      switch (kind) {
        case OpKind::FLUSH: {
          auto* const mesh = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(payload[0]));
          std::memcpy(mesh + 0x40, payload + 11, 4);
          std::memcpy(mesh + 0x44, payload + 12, 4);
          real[ANY_OFFSET] |= static_cast<uint8_t>(payload[13]);
          reinterpret_cast<FlushFn>(static_cast<uintptr_t>(game_calls.flush))(mesh, payload[1], payload[2], payload[3], payload[4], payload[5],
                                                                               payload[6], payload[7], payload[8], payload[9], payload[10]);
          break;
        }
        case OpKind::VF68: {
          auto* const mesh = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(payload[0]));
          std::memcpy(mesh + 0x40, payload + 3, 4);
          std::memcpy(mesh + 0x44, payload + 4, 4);
          const auto vf68 = reinterpret_cast<Vf68Fn>(static_cast<uintptr_t>((*reinterpret_cast<const uint32_t* const*>(mesh))[0x68 / 4]));
          real[ANY_OFFSET] |= vf68(mesh, payload[1], payload[2]);
          break;
        }
        case OpKind::ANIM:
          reinterpret_cast<AnimCallFn>(static_cast<uintptr_t>(game_calls.anim_call))(reinterpret_cast<void*>(static_cast<uintptr_t>(payload[0])), payload[1]);
          real_last = static_cast<int32_t>(payload[2]);
          break;
        default:
          break;
      }
    }
    for (const auto& [from, to] : GROUPS[group_index].copy_back) {
      std::memcpy(real + from, slot->tracker.data() + from, to - from);
    }
    // The stride only when this chunk wrote one; the pending "any" flag joins the real one.
    uint32_t stride = 0;
    std::memcpy(&stride, slot->tracker.data() + STRIDE_OFFSET, 4);
    if (stride != 0 || !controls.stride_non_zero) {
      std::memcpy(real + STRIDE_OFFSET, &stride, 4);
    }
    real[ANY_OFFSET] |= slot->tracker[ANY_OFFSET];
    ++replayed;
  }
  lap(&CallTrace::replay);
  if (compare_slot != nullptr && compare_result == CompareResult::EQUAL && compare_cursor != compare_slot->ops_cursor) {
    compare_result = CompareResult::COMPARE_HAS_MORE;
  }
  if (trace_call != nullptr) {
    trace_call->reruns = reruns;
    trace_call->compare_result = compare_result;
    trace_call->compare_op = compared_ops;
    trace_call->compare_kind = compare_kind;
  }
  if constexpr (TEST_SWITCHES) {
    last_call = {.chunks = chunks, .reruns = reruns, .replayed = replayed, .ops = op_count, .direct = false};
  }
  finish(nullptr);
}

}  // namespace internal

inline void __fastcall DispatchDepth(void* node, void*, void* tracker, void* list, uint32_t begin, uint32_t end) {
  internal::Dispatch(Group::DEPTH_A, node, tracker, list, 0, begin, end);
}

namespace internal {

// The first allocation (SharedPage or the first Update with the row on): the page, the slots, the op address space and a
// reference to this module (the thunks and the workers run its code). False after a failure, which is logged once.
inline bool Allocate() {
  if (shared != nullptr) return true;
  if (allocation_failed) return false;
  void* const page = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  void* const slots = VirtualAlloc(nullptr, static_cast<size_t>(SLOT_COUNT) * SLOT_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  void* const ops = VirtualAlloc(nullptr, OP_ARENA_BYTES, MEM_RESERVE, PAGE_READWRITE);
  HMODULE module = nullptr;
  if (page == nullptr || slots == nullptr || ops == nullptr
      || GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&Record), &module) == FALSE) {
    for (void* memory : {page, slots, ops}) {
      if (memory != nullptr) {
        VirtualFree(memory, 0, MEM_RELEASE);
      }
    }
    allocation_failed = true;
    renodx::utils::log::w("[Total War] Threaded Draw Recording: stands down: its memory could not be allocated.");
    return false;
  }
  arena = static_cast<uint8_t*>(slots);
  op_memory = static_cast<uint8_t*>(ops);
  module_reference = module;
  op_committed = 0;
  for (uint32_t k = 0; k < SLOT_COUNT; ++k) {
    new (arena + static_cast<size_t>(k) * SLOT_SIZE) Slot();
  }
  shared = new (page) Shared();
  shared->arena = reinterpret_cast<uint32_t>(arena);
  shared->arena_size = SLOT_COUNT * SLOT_SIZE;
  shared->record = reinterpret_cast<uint32_t>(&Record);
  shared->dispatch_depth = reinterpret_cast<uint32_t>(&DispatchDepth);
  return true;
}

}  // namespace internal

// The page the thunks read; created the first time it is asked for. Null when the memory could not be allocated.
inline Shared* SharedPage() { return (internal::Allocate() ? internal::shared : nullptr); }

// The game's addresses (Prepare, once the sites are verified).
inline void SetGame(const GameCalls& calls) { internal::game_calls = calls; }

// Every present on the render thread, after the engine code's Update. `sites_in`: any of this feature's sites is written.
inline void Update(const Settings& settings, bool sites_in) {
  using namespace internal;
  present_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
  Stage stage = settings.stage;
  const uint32_t threads = std::clamp<uint32_t>(settings.threads, 1, MAX_THREADS);
  if (stage != Stage::OFF && !Allocate()) {
    stage = Stage::DIRECT;
  }

  // The pool grows here, never inside a dispatch. After a failure it is not tried again until the stage or K changes.
  uint32_t wanted = 0;
  switch (stage) {
    case Stage::TOUCH_AHEAD:
    case Stage::WORKERS:
      wanted = threads - 1;
      break;
    case Stage::ONE_WORKER:
      wanted = 1;
      break;
    default:
      break;
  }
  const uint32_t pool_key = (static_cast<uint32_t>(stage) << 8) | threads;
  if (stage == Stage::OFF || pool_key != pool_failed_key) {
    pool_failed_key = ~0u;
  }
  if (wanted > worker_count && pool_failed_key == ~0u) {
    stopping.store(false, std::memory_order_release);
  }
  for (; worker_count < wanted && pool_failed_key == ~0u; ++worker_count) {
    pool[worker_count].ticket.store(0, std::memory_order_relaxed);
    HANDLE thread = nullptr;
    if constexpr (TEST_SWITCHES) {
      ++thread_attempts;
    }
    if (!controls.fail_thread_creation) {
      thread = CreateThread(nullptr, WORKER_STACK_BYTES, WorkerMain, reinterpret_cast<void*>(static_cast<uintptr_t>(worker_count)),
                            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    }
    if (thread == nullptr) {
      renodx::utils::log::w("[Total War] Threaded Draw Recording: a worker thread could not start; the calls run directly.");
      pool_failed_key = pool_key;
      break;
    }
    SetThreadDescription(thread, L"RenoDX draw recording");
    SetThreadPriority(thread, GetThreadPriority(GetCurrentThread()));
    pool[worker_count].thread = thread;
    given_generation = ~0u;
  }

  // The pool follows Keep Render Core Free: the sets it applied to the game's other threads (none: cleared). The sets are
  // copied only when their generation changed.
  if (worker_count > 0 && cpu_sets::AppliedGeneration() != given_generation) {
    uint32_t generation = 0;
    const std::vector<ULONG> sets = cpu_sets::AppliedWorkerSets(&generation);
    for (uint32_t w = 0; w < worker_count; ++w) {
      SetThreadSelectedCpuSets(pool[w].thread, (sets.empty() ? nullptr : sets.data()), static_cast<ULONG>(sets.size()));
    }
    given_generation = generation;
  }
  if (stage == Stage::OFF && !sites_in) {
    StopPool();
  }

  // The one-frame trace.
  if (!trace.read_environment) {
    trace.read_environment = true;
    trace.first_present = Qpc();
    char text[64] = {};
    if (GetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", text, sizeof(text)) != 0) {
      const std::string_view view(text);
      double seconds = 0;
      const auto [seconds_end, seconds_error] = std::from_chars(view.data(), view.data() + view.size(), seconds);
      if (seconds_error == std::errc()) {
        trace.armed = true;
        trace.arm_ticks = trace.first_present + static_cast<uint64_t>(seconds * static_cast<double>(qpc_frequency));
        if (seconds_end != view.data() + view.size() && *seconds_end == ',') {
          std::from_chars(seconds_end + 1, view.data() + view.size(), trace.frames);
        }
      }
    }
  }
  if (trace.active) {
    trace.frame_list.push_back({.call_ticks = trace.frame_call_ticks,
                                .thread_matched = (settings.context_hooks_thread == 0 || settings.context_hooks_thread == GetCurrentThreadId())});
    trace.frame_call_ticks = 0;
    if (--trace.frames_left == 0) {
      trace.active = false;
      trace.finished = true;
      std::array<wchar_t, MAX_PATH> exe = {};
      GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
      const auto milliseconds = [](uint64_t ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(qpc_frequency); };
      std::string text = "Threaded Draw Recording trace\n";
      for (const FrameTrace& frame : trace.frame_list) {
        text += std::format("frame: calls {:.3f} ms (trace-only work left out; read timings from calls marked 'timing'), present thread {} context_hooks'\n",
                            milliseconds(frame.call_ticks), (frame.thread_matched ? "is" : "is not"));
      }
      for (const CallTrace& call : trace.calls) {
        text += std::format("call: group {} family {} stage {} K {} items {} chunks {} direct '{}' {}\n  chunk items:", static_cast<uint32_t>(call.group),
                            (call.family == NO_FAMILY ? std::string("-") : std::to_string(call.family)), static_cast<uint32_t>(call.stage), call.threads,
                            call.items, call.chunks, call.direct_reason, (call.compared ? "compared" : "timing"));
        for (uint32_t k = 0; k < call.chunks && k < call.chunk_items.size(); ++k) {
          text += std::format(" {}", call.chunk_items[k]);
        }
        text += std::format("\n  cut: targets {} probes {} failures {}; runs", call.cut_targets, call.cut_probes, call.cut_failures);
        for (const uint32_t runs : call.runs) {
          text += std::format(" {}", runs);
        }
        const char* compare_text = (call.compared ? "" : "not run (timing frame)");
        switch (call.compare_result) {
          case CompareResult::EQUAL:
            compare_text = "equal";
            break;
          case CompareResult::SLOT_NOT_COMPARABLE:
            compare_text = "not comparable (compare slot)";
            break;
          case CompareResult::CHUNK_RERUN:
            compare_text = "not comparable (a chunk was run again)";
            break;
          case CompareResult::COMPARE_HAS_MORE:
            compare_text = "differs (the compare run has more)";
            break;
          default:
            break;
        }
        text += std::format(
            "\n  ops: flush {} vf68 {} anim {} bytes {} (buffer {}); seeds hit {} miss {}; overflow {} unknown {} conflict {} rerun {}\n"
            "  ms: direct {:.3f} cut {:.3f} warm-ups {:.3f} chunk0 {:.3f} join {:.3f} replay {:.3f} rerun {:.3f} total {:.3f}"
            " (trace only: compare {:.3f} run scan {:.3f}; wall {:.3f})\n  compare: ",
            call.ops[1], call.ops[2], call.ops[3], call.op_bytes, call.op_buffer_bytes, call.seed_hits, call.seed_misses, call.overflows,
            call.unknown_classes, call.conflicts, call.reruns, milliseconds(call.direct), milliseconds(call.cut), milliseconds(call.warm_ups),
            milliseconds(call.chunk_zero), milliseconds(call.join_wait), milliseconds(call.replay), milliseconds(call.rerun), milliseconds(call.total),
            milliseconds(call.compare), milliseconds(call.run_scan), milliseconds(call.wall));
        if (call.compare_result == CompareResult::DIFFERS) {
          text += std::format("differs at op {} (kind {})\n", call.compare_op, call.compare_kind);
        } else {
          text += std::format("{}\n", compare_text);
        }
      }
      std::ofstream(std::filesystem::path(exe.data()).parent_path() / "tw_draw_recording.txt") << text;
      trace.calls.clear();
      trace.frame_list.clear();
    }
  } else if (trace.armed && !trace.finished && Qpc() >= trace.arm_ticks) {
    trace.active = true;
    trace.frames_left = std::max<uint32_t>(trace.frames, 1);
    trace.calls.reserve(256);
  }

  config = {.stage = stage, .threads = threads, .loops = settings.loops};
}

// Addon detach, right after the engine code's Uninstall. `sites_in`: a site is still written (the engine code gave up).
inline void Uninstall(bool sites_in) {
  using namespace internal;
  detached.store(true, std::memory_order_relaxed);
  if (shared == nullptr) return;
  if (sites_in || shared->active.load(std::memory_order_acquire) != 0) {
    // A thread may still be in a thunk or the dispatcher: everything stays for the life of the process.
    if (!kept_logged) {
      renodx::utils::log::w("[Total War] Threaded Draw Recording: left in at detach (a game thread may still be in it).");
    }
    kept_logged = true;
    return;
  }
  StopPool();
  for (void* memory : {static_cast<void*>(shared), static_cast<void*>(arena), static_cast<void*>(op_memory)}) {
    VirtualFree(memory, 0, MEM_RELEASE);
  }
  shared = nullptr;
  arena = nullptr;
  op_memory = nullptr;
  FreeLibrary(module_reference);
  module_reference = nullptr;
}

#else

struct Shared {};
struct GameCalls {
  bool usable = false;
};
inline Shared* SharedPage() { return nullptr; }
inline void SetGame(const GameCalls&) {}
inline void Update(const Settings&, bool) {}
inline void Uninstall(bool) {}

#endif

}  // namespace twattila::draw_recording
