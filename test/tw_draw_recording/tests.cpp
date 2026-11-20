// Threaded Draw Recording (twattila/draw_recording.hpp), x86 only.
// Part 1 (C.2-1): the pool: start / stop 100 times, K 2 to 16 to 2 live, the x87 and SSE control words of the dispatching
// thread seen by every worker on every dispatch, the CPU-set mirror of Keep Render Core Free's published sets (applied once
// per generation), worker thread creation failing (one attempt per stage and K), the types' offsets (static asserts in the
// header).
// Part 2 (C.2-2): the depth path on a synthetic loop with the same state machine as the game's (written from its listing),
// driven by a synthetic driver that splits the list into portions from the tracker's +0x50 (so the portions start at
// begin != 0, back to back on one tracker, and a stale stride changes the split). The fake flush does the real flush's
// writes (+0x5c, +0x28, the +0x69 byte it reads and clears) and logs the +0x69 byte it saw. The serial call log equals the log
// of the Chunked, Recorded, One Worker and Workers stages at K = 2 to 16 over lists with runs of 1 to 300 items, animation
// -1 runs, meshes without a technique before cuts and in the last chunk, tiny op buffers (overflow: run again), two passes in a
// row on one tracker, calls with 200000 items; Touch-Ahead; the fall-backs (entry state, continuation of foreign mesh state);
// the flush entry site; the Record rules (group, any byte); the trace's in-frame compare and its time accounting. Controls:
// each safeguard off (seed validation, pointer translation, continuation guard, any capture, the stride rule), a dropped
// flush, a wrong animation stack slot, an op injected into one chunk: every one must make the run differ.
// Part 3 (C.2-3): the colour path on a second synthetic loop (same machine plus the variation path, the constants, the
// textures, the faction block and a vtable check at the mesh setup), meshes that recur in several chunks (the owner table: the
// call runs directly), meshes of an unknown class, constants that are NaN / -0 / equal in a row with the game's cache flag on
// and off, every faction case, an "any" byte left over at a chunk's end, and the colour controls (snapshot restore off, the
// owner table off with another chunk's write racing in, seed validation off): each must make the run differ.
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

#include "src/games/twattila/draw_recording.hpp"

namespace dr = twattila::draw_recording;
namespace cs = twattila::cpu_sets;
namespace di = twattila::draw_recording::internal;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

static uint32_t ProcessThreadCount() {
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  uint32_t count = 0;
  THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
  for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
  }
  CloseHandle(snapshot);
  return count;
}

// ---------------------------------------------------------------- part 1: the pool

struct WorkerReport {
  std::atomic<uint32_t> runs{0};
  uint32_t x87 = 0;
  uint32_t sse = 0;
  uint32_t mxcsr = 0;
  DWORD thread_id = 0;
};
static std::array<WorkerReport, dr::MAX_WORKERS> reports;

static void ReportWork(const di::Descriptor&, uint32_t worker) {
  WorkerReport& report = reports[worker];
  unsigned int x87 = 0;
  unsigned int sse = 0;
  __control87_2(0, 0, &x87, &sse);
  report.x87 = x87;
  report.sse = sse;
  report.mxcsr = _mm_getcsr();
  report.thread_id = GetCurrentThreadId();
  // Game code on a worker may leave its own control words changed: the next dispatch must set them again.
  unsigned int ignored = 0;
  __control87_2(_PC_24 | _RC_CHOP, _MCW_RC | _MCW_PC, &ignored, nullptr);
  __control87_2(_RC_CHOP | _DN_FLUSH, _MCW_RC | _MCW_DN, nullptr, &ignored);
  report.runs.fetch_add(1);
}

static void RunReport(uint32_t workers) {
  di::descriptor.work = ReportWork;
  di::RunOnWorkers(workers, [] {}, nullptr);
}

static dr::Settings Workers(uint32_t threads) { return {.stage = dr::Stage::WORKERS, .threads = threads, .loops = 1}; }

static void PoolStartStop() {
  const uint32_t baseline = ProcessThreadCount();
  for (int cycle = 0; cycle < 100; ++cycle) {
    dr::Update(Workers(16), true);
    REQUIRE(di::worker_count == 15);
    for (WorkerReport& report : reports) report.runs = 0;
    RunReport(15);
    std::array<DWORD, 15> ids = {};
    for (uint32_t w = 0; w < 15; ++w) {
      CHECK(reports[w].runs == 1);
      ids[w] = reports[w].thread_id;
      CHECK(ids[w] != GetCurrentThreadId());
    }
    std::ranges::sort(ids);
    CHECK(std::ranges::adjacent_find(ids) == ids.end());
    CHECK(ProcessThreadCount() == baseline + 15);
    dr::Update({.stage = dr::Stage::OFF}, false);
    CHECK(di::worker_count == 0);
    CHECK(ProcessThreadCount() == baseline);
  }
  // The row off while sites are still in: the pool stays.
  dr::Update(Workers(4), true);
  dr::Update({.stage = dr::Stage::OFF}, true);
  CHECK(di::worker_count == 3);
  dr::Update({.stage = dr::Stage::OFF}, false);
  CHECK(di::worker_count == 0);
  std::printf("pool: 100 start / stop cycles, 15 distinct threads each, no thread left\n");
}

static void PoolLiveK() {
  const std::array<uint32_t, 11> sequence = {2, 4, 6, 8, 12, 16, 12, 8, 6, 4, 2};
  uint32_t most = 0;
  for (const uint32_t k : sequence) {
    dr::Update(Workers(k), true);
    most = std::max(most, k - 1);
    CHECK(di::worker_count == most);
    for (WorkerReport& report : reports) report.runs = 0;
    RunReport(k - 1);
    for (uint32_t w = 0; w < dr::MAX_WORKERS; ++w) CHECK(reports[w].runs == (w < k - 1 ? 1u : 0u));
  }
  dr::Update({.stage = dr::Stage::OFF}, false);
  std::printf("pool: K 2 to 16 to 2 live: only the first K - 1 workers ran each time\n");
}

static void PoolControlWords() {
  dr::Update(Workers(16), true);
  unsigned int saved_x87 = 0;
  unsigned int saved_sse = 0;
  __control87_2(0, 0, &saved_x87, &saved_sse);
  const unsigned int x87_mask = _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC;
  const unsigned int x87_values[] = {_PC_24 | _RC_UP, _PC_53 | _RC_CHOP, _PC_64 | _RC_DOWN, _PC_24 | _RC_NEAR};
  const unsigned int rounding[] = {_RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP};
  const unsigned int denormals[] = {_DN_SAVE, _DN_FLUSH, _DN_FLUSH_OPERANDS_SAVE_RESULTS, _DN_SAVE_OPERANDS_FLUSH_RESULTS};
  int checked = 0;
  for (const unsigned int x87_value : x87_values) {
    for (const unsigned int round : rounding) {
      for (const unsigned int denormal : denormals) {
        unsigned int ignored = 0;
        __control87_2(x87_value, x87_mask, &ignored, nullptr);
        __control87_2(round | denormal, _MCW_RC | _MCW_DN, nullptr, &ignored);
        const uint32_t mine = _mm_getcsr();
        unsigned int my_x87 = 0;
        __control87_2(0, 0, &my_x87, &ignored);
        // Back to back on the same workers, each of which clobbered its own words after the last dispatch.
        RunReport(15);
        for (uint32_t w = 0; w < 15; ++w) {
          CHECK(((reports[w].mxcsr ^ mine) & 0xffc0) == 0);
          CHECK(((reports[w].x87 ^ my_x87) & x87_mask) == 0);
        }
        ++checked;
      }
    }
  }
  __control87_2(saved_x87, x87_mask, &saved_x87, nullptr);
  __control87_2(saved_sse, _MCW_EM | _MCW_RC | _MCW_DN, nullptr, &saved_sse);
  dr::Update({.stage = dr::Stage::OFF}, false);
  std::printf("pool: x87 PC / RC and MXCSR RC / DAZ / FTZ copied to all 15 workers on every dispatch (%d combinations)\n", checked);
}

static void PoolCpuSets() {
  const std::vector<cs::CpuSet> system = cs::SystemCpuSets();
  if (system.size() < 4) {
    std::printf("pool: CPU-set mirror skipped (fewer than 4 CPU sets)\n");
    return;
  }
  std::vector<ULONG> sets = {system[2].id, system[3].id};
  std::ranges::sort(sets);
  dr::Update(Workers(8), true);
  const auto publish = [](const std::vector<ULONG>& published_sets) {
    const std::unique_lock lock(cs::published.mutex);
    cs::published.worker_sets = published_sets;
    cs::published.generation.fetch_add(1, std::memory_order_release);
  };
  const auto all_equal = [](const std::vector<ULONG>& expected) {
    for (uint32_t w = 0; w < di::worker_count; ++w) {
      if (cs::ReadSets(GetThreadSelectedCpuSets, di::pool[w].thread) != expected) return false;
    }
    return true;
  };
  CHECK(all_equal({}));
  publish(sets);
  dr::Update(Workers(8), true);
  CHECK(all_equal(sets));
  // A thread started later is given the sets too.
  dr::Update(Workers(12), true);
  CHECK(di::worker_count == 11);
  CHECK(all_equal(sets));
  // The sets are read once per generation: a change made behind the pool's back stays until the next publish.
  SetThreadSelectedCpuSets(di::pool[0].thread, nullptr, 0);
  dr::Update(Workers(12), true);
  CHECK(cs::ReadSets(GetThreadSelectedCpuSets, di::pool[0].thread) == std::vector<ULONG>{});
  CHECK(di::given_generation == cs::AppliedGeneration());
  publish({});
  dr::Update(Workers(12), true);
  CHECK(all_equal({}));
  dr::Update({.stage = dr::Stage::OFF}, false);
  std::printf("pool: CPU-set mirror: the published sets are the pool threads' sets (set, grown, cleared), applied once per generation\n");
}

// A worker thread that can't be created is tried once per stage and K, not every present.
static void PoolThreadFailure() {
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  dr::Update({.stage = dr::Stage::OFF}, false);
  CHECK(di::worker_count == 0);
  di::controls.fail_thread_creation = true;
  di::thread_attempts = 0;
  for (int present = 0; present < 5; ++present) dr::Update(Workers(8), true);
  CHECK(di::thread_attempts == 1);
  CHECK(di::worker_count == 0);
  dr::Update(Workers(6), true);
  CHECK(di::thread_attempts == 2);
  const uint32_t attempts = di::thread_attempts;
  di::controls.fail_thread_creation = false;
  // Same stage and K: still not tried.
  dr::Update(Workers(6), true);
  CHECK(di::thread_attempts == 2 && di::worker_count == 0);
  // The stage or K changed: tried again, and now it works.
  dr::Update(Workers(7), true);
  CHECK(di::worker_count == 6);
  dr::Update({.stage = dr::Stage::OFF}, false);
  CHECK(di::worker_count == 0);
  std::printf("pool: a failed CreateThread is tried once per stage and K (%u attempts for 7 presents)\n", attempts);
#else
  std::printf("pool: thread failure skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

// ---------------------------------------------------------------- part 2: the synthetic game

template <class T>
static T& Field(void* base, uint32_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

struct SynthMesh {
  uint32_t* vtable;
  uint32_t kind;
  uint8_t pad0[0x38];
  uint32_t view_tag;
  uint32_t pass;
  uint8_t pad1[0x14];
  uint8_t flag;
  uint8_t pad2[0x23];
};
static_assert(sizeof(SynthMesh) == 0x80 && offsetof(SynthMesh, view_tag) == 0x40 && offsetof(SynthMesh, pass) == 0x44 && offsetof(SynthMesh, flag) == 0x5c);

// A faction mask: the index at +4 (the loop copies it into the tracker); its bytes pick colours of the faction's list.
struct SynthMask {
  uint8_t bytes[4];
  int32_t index;
  uint8_t more[248];
};

// Item flags of the colour loop.
constexpr uint32_t ITEM_CONST1 = 1;
constexpr uint32_t ITEM_CONST2 = 2;
constexpr uint32_t ITEM_TEXTURES = 4;
constexpr uint32_t ITEM_FACTION = 8;
// Sets the "any" byte and does not count: a chunk can end with the byte set and nothing to flush.
constexpr uint32_t ITEM_ANY_ONLY = 16;

struct SynthItem {
  SynthMesh* mesh;
  uint8_t pad0[0x54 - 4];
  const SynthMask* mask;
  uint32_t pad1;
  int32_t anim;
  uint32_t texture1;
  uint32_t texture2;
  int32_t faction;
  uint32_t value;
  uint32_t repeat;
  uint32_t flags;
  uint8_t pad2[0xec - 0x78];
  uint32_t variation;
  uint8_t pad3[0x100 - 0xf0];
};
static_assert(sizeof(SynthItem) == 0x100 && offsetof(SynthItem, mask) == 0x54 && offsetof(SynthItem, anim) == 0x5c
              && offsetof(SynthItem, variation) == 0xec);

struct SynthList {
  uint32_t pad;
  uint32_t count;
  SynthItem** data;
};
static_assert(offsetof(SynthList, count) == 4 && offsetof(SynthList, data) == 8);

// kind, mesh, the 10 flush arguments (or the call's arguments), view_tag, pass, the +0x69 byte the flush saw.
using LogEntry = std::array<uint32_t, 15>;
static std::vector<LogEntry> game_log;
// The depth worlds' fake flush reads the mesh state it should not need; the colour worlds' does what the game's flush does.
static bool strict_flush = true;
static dr::Shared* g_shared = nullptr;

// What the test changes in the thunk rule (the controls).
struct TestControls {
  bool drop_end_flush = false;
  uint32_t anim_stack = 9;
  int inject_chunk = -1;
  // The flush entry's site id for every flush instead of the call-site ids.
  bool entry_site = false;
};
static TestControls test_controls;

// The real flush's tracker writes: the cursor +0x5c advances by count x stride, the +0x28 cache is written, the +0x69 byte is
// read and cleared. The pointers in the arguments are the tracker's.
static void __thiscall FakeFlush(void* mesh, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9) {
  // A flush of no instances returns at once (the colour loop can ask for one).
  if (a4 == 0) return;
  auto* m = static_cast<SynthMesh*>(mesh);
  auto* any = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(a3));
  if (strict_flush) {
    // The depth tests' flush reads the mesh's state (more than the game's does).
    game_log.push_back({1, reinterpret_cast<uint32_t>(mesh), a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, m->view_tag, m->pass, *any});
  } else {
    // The game's: +0x40 is written first (0x17783b9), +0x44 is never read (S1).
    m->view_tag = a1 ^ 0x77;
    game_log.push_back({1, reinterpret_cast<uint32_t>(mesh), a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, m->view_tag, 0, *any});
  }
  *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(a6)) += a4 * (m->kind + 1);
  *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(a8)) = a4 ^ a2;
  *any = 0;
}
static uint8_t __thiscall FakeVf68(void* mesh, uint32_t view, uint32_t zero) {
  const auto* m = static_cast<const SynthMesh*>(mesh);
  game_log.push_back({2, reinterpret_cast<uint32_t>(mesh), view, zero, 0, 0, 0, 0, 0, 0, 0, 0, m->view_tag, m->pass, 0});
  return static_cast<uint8_t>(m->kind & 1);
}
static void __thiscall FakeAnimCall(void* state, uint32_t value) {
  game_log.push_back({3, reinterpret_cast<uint32_t>(state), value, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}

static std::array<uint32_t, 0x70 / 4> mesh_vtable = {};
// A class the census did not cover.
static std::array<uint32_t, 0x70 / 4> foreign_vtable = {};

// The colour loop's environment: the node (a vtable with the faction lookup, the animation state, the second constant's block),
// four blocks of the first constant, the faction lists.
struct SynthColour {
  float c[3];
};
struct SynthFactionList {
  uint32_t pad;
  uint32_t count;
  SynthColour* data;
};
struct SynthBlock {
  uint8_t pad[0x14];
  float* cell;
  uint8_t more[0x28];
};
struct SynthNode {
  uint32_t* vtable;
  uint8_t pad[0x2950 - 4];
  uint8_t state[0x400];
};
static_assert(offsetof(SynthNode, state) == 0x2950 && 0x2950 + 0x2e8 + 4 <= sizeof(SynthNode));

static SynthNode synth_node;
static std::array<uint32_t, 0xc4 / 4> node_vtable = {};
static std::array<SynthBlock, 4> const1_blocks;
static std::array<float, 4> const1_cells;
static float const2_cell = 0;
static uint8_t cached_constants = 0;
static float faction_default = 0.25f;
static std::array<SynthColour, 256> faction_colours;
static std::array<SynthFactionList, 4> faction_lists;

static void __thiscall FakeDirty(uint32_t object) {
  uint32_t bits = 0;
  std::memcpy(&bits, *reinterpret_cast<float* const*>(static_cast<uintptr_t>(object) + 0x14), 4);
  game_log.push_back({4, object, bits});
}
static uint32_t __thiscall FakeView(uint32_t texture) { return texture == 0 ? 0 : texture ^ 0x1000; }
static void __thiscall FakeSrv(uint32_t slot, uint32_t view) { game_log.push_back({5, slot, view}); }
static void __thiscall FakeFactionColours(uint32_t state, const float* colours) {
  LogEntry entry = {6, state};
  std::memcpy(&entry[2], colours, 48);
  game_log.push_back(entry);
}
static void __thiscall FakeFactionLists(void*, int32_t faction, uint32_t* list) { *list = reinterpret_cast<uint32_t>(&faction_lists[static_cast<uint32_t>(faction) & 3]); }

// What the game does at those sites, written from its listing (the replay's own functions are checked against these).
static void GameConst(uint32_t object, uint32_t bits) {
  float value = 0;
  std::memcpy(&value, &bits, 4);
  auto* const cell = *reinterpret_cast<float**>(static_cast<uintptr_t>(object) + 0x14);
  if (cached_constants == 0 || !(*cell == value)) {
    std::memcpy(cell, &bits, 4);
    FakeDirty(object);
  }
}
static void GameSrv(uint32_t slot, uint32_t texture) { FakeSrv(slot, FakeView(texture)); }
static void GameFaction(int32_t faction, const SynthMask* mask, void* node, uint32_t state) {
  if (faction == -1) {
    const std::array<float, 12> entries = {faction_default, faction_default, 1.0f, faction_default, faction_default, 1.0f,
                                           faction_default, faction_default, 1.0f, faction_default, faction_default, 1.0f};
    FakeFactionColours(state, entries.data());
    return;
  }
  uint32_t found = 0;
  const auto lookup = reinterpret_cast<void(__thiscall*)(void*, int32_t, uint32_t*)>((*static_cast<uint32_t**>(node))[0xc0 / 4]);
  lookup(node, faction, &found);
  const auto* list = reinterpret_cast<const SynthFactionList*>(found);
  std::vector<SynthColour> gathered;
  const SynthColour* chosen = list->data;
  uint32_t count = list->count;
  if (mask != nullptr) {
    for (uint32_t j = 0; j < list->count; ++j) gathered.push_back(list->data[reinterpret_cast<const uint8_t*>(mask)[j]]);
    chosen = gathered.data();
    count = static_cast<uint32_t>(gathered.size());
  }
  if (count >= 4) FakeFactionColours(state, chosen[0].c);
}

static bool InArena(void* tracker) { return reinterpret_cast<uint32_t>(tracker) - g_shared->arena < g_shared->arena_size; }

struct StackImage {
  dr::Frame frame;
  uint32_t stack[16];
};

static StackImage MakeImage(dr::Site site, uint32_t ecx) {
  StackImage image = {};
  image.frame.site = static_cast<uint32_t>(site);
  image.frame.ecx = ecx;
  image.frame.eax = 0x55555555;
  for (uint32_t i = 0; i < 16; ++i) image.stack[i] = 0xdead0000 + i;
  return image;
}

static void FlushSite(dr::Site site, void* t, uint32_t mesh, uint32_t technique, uint32_t count) {
  const uint32_t address = reinterpret_cast<uint32_t>(t);
  const std::array<uint32_t, 10> args = {Field<uint32_t>(t, 0x18), Field<uint32_t>(t, 0x1c), technique, address + 0x69, count,
                                         Field<uint32_t>(t, 0x24), address + 0x5c, Field<uint32_t>(t, 0x78), address + 0x28, 0};
  if (!InArena(t)) {
    FakeFlush(reinterpret_cast<void*>(mesh), args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
    return;
  }
  auto* slot = static_cast<dr::Slot*>(t);
  if (test_controls.drop_end_flush && (site == dr::Site::DEPTH_FLUSH_END || site == dr::Site::COLOUR_FLUSH_END) && slot->mode == dr::MODE_RECORD) return;
  StackImage image = MakeImage(test_controls.entry_site ? dr::Site::FLUSH_ENTRY : site, mesh);
  for (uint32_t i = 0; i < 10; ++i) image.stack[1 + i] = args[i];
  dr::Record(slot, &image.frame);
  if (test_controls.inject_chunk == static_cast<int>(slot->chunk) && slot->mode == dr::MODE_RECORD
      && (site == dr::Site::DEPTH_FLUSH_BOUNDARY || site == dr::Site::COLOUR_FLUSH_BOUNDARY)) {
    test_controls.inject_chunk = -1;
    dr::Record(slot, &image.frame);
  }
}

static uint8_t Vf68Site(dr::Site site, void* t, uint32_t mesh, uint32_t view) {
  if (!InArena(t)) {
    const auto function = reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(static_cast<uintptr_t>((*reinterpret_cast<uint32_t**>(mesh))[0x68 / 4]));
    return function(reinterpret_cast<void*>(mesh), view, 0);
  }
  StackImage image = MakeImage(site, mesh);
  image.stack[1] = view;
  image.stack[2] = 0;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
  return static_cast<uint8_t>(image.frame.eax);
}

static void AnimSite(dr::Site site, void* t, uint32_t state, uint32_t value, uint32_t index) {
  if (!InArena(t)) {
    g_shared->last_anim = static_cast<int32_t>(index);
    FakeAnimCall(reinterpret_cast<void*>(state), value);
    return;
  }
  StackImage image = MakeImage(site, state);
  image.stack[1] = value;
  // The colour loop's frame is two dwords deeper.
  image.stack[test_controls.anim_stack + (site == dr::Site::COLOUR_ANIM ? 2 : 0)] = index;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

// The colour loop's other sites: outside the arena the game's own code, inside a Record frame.
static bool SetupSite(void* t, uint32_t mesh) {
  if (!InArena(t)) return false;
  StackImage image = MakeImage(dr::Site::COLOUR_SETUP, 0);
  image.frame.ebx = mesh;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
  return image.frame.eax != 0;
}

static void Const1Site(void* t, uint32_t block) {
  if (!InArena(t)) {
    GameConst(block, 0);
    return;
  }
  StackImage image = MakeImage(dr::Site::COLOUR_CONST1, block);
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

static void Const2Site(void* t, uint32_t state, uint32_t bits) {
  if (!InArena(t)) {
    GameConst(state + di::CONST2_BLOCK, bits);
    return;
  }
  StackImage image = MakeImage(dr::Site::COLOUR_CONST2, 0);
  image.frame.xmm0 = bits;
  image.stack[5] = state;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

static void SrvSite(dr::Site site, void* t, uint32_t mesh, uint32_t texture) {
  if (!InArena(t)) {
    GameSrv(mesh + (site == dr::Site::COLOUR_SRV_DIFFUSE ? 0x41f4 : 0x421c), texture);
    return;
  }
  StackImage image = MakeImage(site, texture);
  image.frame.ebx = mesh;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

static void FactionSite(void* t, SynthItem* item, SynthNode* node, uint32_t state) {
  if (!InArena(t)) {
    Field<int32_t>(t, 0x0c) = (item->mask != nullptr ? item->mask->index : -1);
    GameFaction(item->faction, item->mask, node, state);
    return;
  }
  StackImage image = MakeImage(dr::Site::COLOUR_FACTION, static_cast<uint32_t>(item->faction));
  image.frame.edi = reinterpret_cast<uint32_t>(item);
  image.stack[0x28 / 4] = reinterpret_cast<uint32_t>(node);
  image.stack[0x14 / 4] = state;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

// The depth loop, from its listing: the thunk rule on its init, flush, vf68 and animation sites.
static void __thiscall SynthDepthLoop(void* node, void* t, void* list_pointer, uint32_t begin, uint32_t end) {
  const auto* list = static_cast<const SynthList*>(list_pointer);
  if (list->count == 0) return;
  int32_t anim = (InArena(t) ? static_cast<dr::Slot*>(t)->anim : g_shared->seed_anim);
  const uint32_t state = reinterpret_cast<uint32_t>(node) + 0x2950;
  for (uint32_t i = begin; i < end; ++i) {
    SynthItem* item = list->data[i];
    SynthMesh* mesh = item->mesh;
    const uint32_t mesh_address = reinterpret_cast<uint32_t>(mesh);
    Field<uint32_t>(t, 0x6c) = mesh_address;
    const bool changed = (mesh_address != Field<uint32_t>(t, 0x70) || Field<uint32_t>(t, 0x14) != 0);
    const uint32_t count = Field<uint32_t>(t, 0x58);
    if (changed ? count != 0 : count == Field<uint32_t>(t, 0x64)) {
      FlushSite(dr::Site::DEPTH_FLUSH_BOUNDARY, t, Field<uint32_t>(t, 0x70), Field<uint32_t>(t, 0x74), count);
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
    }
    Field<uint32_t>(t, 0x70) = mesh_address;
    Field<uint32_t>(t, 0x14) = 0;
    if (changed) {
      mesh->view_tag = Field<uint32_t>(t, 0x20);
      Field<uint8_t>(t, 0x68) = mesh->flag;
      const uint32_t type = mesh->kind ^ (mesh->view_tag & 0x10000);
      mesh->pass = Field<uint32_t>(t, 0x14);
      const uint32_t kind = type & 0xffff;
      const uint32_t technique = (kind == 0 ? 0 : (kind == 1 ? 0x101 : kind * 0x10));
      Field<uint32_t>(t, 0x74) = technique;
      if (technique != 0) {
        if ((technique & 1) == 0) {
          // The stride the driver reads; written only by a mesh with an enabled technique.
          Field<uint32_t>(t, 0x50) = 4 + mesh->kind;
          Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::DEPTH_VF68, t, mesh_address, Field<uint32_t>(t, 0x1c));
          // A path that sets the "any" byte itself.
          if (kind == 3) Field<uint8_t>(t, 0x69) = 1;
        } else {
          Field<uint32_t>(t, 0x74) = 0;
        }
      }
    }
    const uint32_t technique = Field<uint32_t>(t, 0x74);
    if (technique == 0) continue;
    if (anim != item->anim) {
      const uint32_t pending = Field<uint32_t>(t, 0x58);
      if (pending != 0) {
        FlushSite(dr::Site::DEPTH_FLUSH_ANIM, t, Field<uint32_t>(t, 0x6c), technique, pending);
        Field<uint32_t>(t, 0x58) = 0;
      }
      Field<uint32_t>(t, 0x78) = 8;
      anim = item->anim;
      AnimSite(dr::Site::DEPTH_ANIM, t, state, static_cast<uint32_t>(anim) * 3 + 1, static_cast<uint32_t>(anim));
    }
    ++Field<uint32_t>(t, 0x58);
  }
  const uint32_t pending = Field<uint32_t>(t, 0x58);
  if (pending != 0) {
    FlushSite(dr::Site::DEPTH_FLUSH_END, t, Field<uint32_t>(t, 0x6c), Field<uint32_t>(t, 0x74), pending);
    Field<uint32_t>(t, 0x58) = 0;
    Field<uint32_t>(t, 0x78) = 0;
  }
}

// The colour loop, from its listing (0x17b0400): the depth loop's mesh change, plus the vtable check at the setup, the variation
// path (+0x14 = the item's variation, written into the mesh's +0x44 with the shared-state call after it), the constants, the
// textures and faction block (each only when the tracker's fields +0x00..+0x0c say the item changed them), and the item's
// end: the pure virtual call that sets the "any" byte on the first instance. Mesh kinds from 6 on have no shared-state call at
// their setup (lets the constants of two chunks touch).
static void __thiscall SynthColourLoop(void* node_pointer, void* t, void* list_pointer, uint32_t, uint32_t begin, uint32_t end) {
  const auto* list = static_cast<const SynthList*>(list_pointer);
  if (list->count == 0) return;
  auto* node = static_cast<SynthNode*>(node_pointer);
  int32_t anim = (InArena(t) ? static_cast<dr::Slot*>(t)->anim : g_shared->seed_anim);
  const uint32_t state = reinterpret_cast<uint32_t>(node->state);
  for (uint32_t i = begin; i < end; ++i) {
    SynthItem* item = list->data[i];
    SynthMesh* mesh = item->mesh;
    const uint32_t mesh_address = reinterpret_cast<uint32_t>(mesh);
    Field<uint32_t>(t, 0x6c) = mesh_address;
    const bool changed = (mesh_address != Field<uint32_t>(t, 0x70) || Field<uint32_t>(t, 0x14) != 0);
    const uint32_t count = Field<uint32_t>(t, 0x58);
    if (changed ? count != 0 : count == Field<uint32_t>(t, 0x64)) {
      FlushSite(dr::Site::COLOUR_FLUSH_BOUNDARY, t, Field<uint32_t>(t, 0x70), Field<uint32_t>(t, 0x74), count);
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
    }
    Field<uint32_t>(t, 0x70) = mesh_address;
    Field<uint32_t>(t, 0x14) = 0;
    if (changed) {
      if (SetupSite(t, mesh_address)) {
        Field<uint32_t>(t, 0x74) = 0;
        continue;
      }
      mesh->view_tag = Field<uint32_t>(t, 0x20);
      Field<uint8_t>(t, 0x68) = mesh->flag;
      const uint32_t type = mesh->kind ^ (mesh->view_tag & 0x10000);
      mesh->pass = Field<uint32_t>(t, 0x14);
      const uint32_t kind = type & 0xffff;
      const uint32_t technique = (kind == 0 ? 0 : (kind == 1 ? 0x101 : kind * 0x10));
      Field<uint32_t>(t, 0x74) = technique;
      if (technique != 0) {
        if ((technique & 1) == 0) {
          Field<uint32_t>(t, 0x50) = 4 + mesh->kind;
          if (mesh->kind < 6) Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::COLOUR_VF68_SETUP, t, mesh_address, Field<uint32_t>(t, 0x1c));
        } else {
          Field<uint32_t>(t, 0x74) = 0;
        }
      }
    }
    const uint32_t technique = Field<uint32_t>(t, 0x74);
    if (technique == 0) continue;
    // The variation path: a full batch or another variation flushes; a new variation is written into the mesh.
    const uint32_t pending = Field<uint32_t>(t, 0x58);
    if (pending == Field<uint32_t>(t, 0x64) || item->variation != Field<uint32_t>(t, 0x14)) {
      FlushSite(dr::Site::COLOUR_FLUSH_VARIATION, t, Field<uint32_t>(t, 0x70), technique, pending);
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
      if (item->variation != Field<uint32_t>(t, 0x14)) {
        Field<uint32_t>(t, 0x14) = item->variation;
        mesh->pass = item->variation;
        Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::COLOUR_VF68_VARIATION, t, mesh_address, Field<uint32_t>(t, 0x1c));
      }
    }
    // (An item that only sets the "any" byte leaves the animation alone, so a chunk can end with the change mask clear.)
    if (anim != item->anim && (item->flags & ITEM_ANY_ONLY) == 0) {
      const uint32_t waiting = Field<uint32_t>(t, 0x58);
      if (waiting != 0) {
        FlushSite(dr::Site::COLOUR_FLUSH_CHANGE, t, Field<uint32_t>(t, 0x6c), technique, waiting);
        Field<uint32_t>(t, 0x58) = 0;
      }
      Field<uint32_t>(t, 0x78) = 8;
      anim = item->anim;
      AnimSite(dr::Site::COLOUR_ANIM, t, state, static_cast<uint32_t>(anim) * 3 + 1, static_cast<uint32_t>(anim));
    }
    if ((item->flags & ITEM_CONST1) != 0) Const1Site(t, reinterpret_cast<uint32_t>(&const1_blocks[mesh->kind & 3]));
    if ((item->flags & ITEM_CONST2) != 0) {
      for (uint32_t r = 0; r < item->repeat; ++r) Const2Site(t, state, item->value);
    }
    if ((item->flags & ITEM_TEXTURES) != 0 && (item->texture1 != Field<uint32_t>(t, 0x04) || item->texture2 != Field<uint32_t>(t, 0x08))) {
      Field<uint32_t>(t, 0x04) = item->texture1;
      SrvSite(dr::Site::COLOUR_SRV_DIFFUSE, t, mesh_address, item->texture1);
      Field<uint32_t>(t, 0x08) = item->texture2;
      SrvSite(dr::Site::COLOUR_SRV_NORMAL, t, mesh_address, item->texture2);
    }
    if ((item->flags & ITEM_FACTION) != 0) {
      const int32_t mask_index = (item->mask != nullptr ? item->mask->index : -1);
      if (item->faction != Field<int32_t>(t, 0x00) || mask_index != Field<int32_t>(t, 0x0c)) {
        Field<int32_t>(t, 0x00) = item->faction;
        FactionSite(t, item, node, state);
      }
    }
    if (Field<uint32_t>(t, 0x58) == 0 && Field<uint8_t>(t, 0x68) != 0) Field<uint8_t>(t, 0x69) |= (mesh->kind >> 1) & 1;
    if ((item->flags & ITEM_ANY_ONLY) != 0) {
      Field<uint8_t>(t, 0x69) |= 1;
      continue;
    }
    ++Field<uint32_t>(t, 0x58);
  }
  const uint32_t pending = Field<uint32_t>(t, 0x58);
  if (pending != 0) {
    FlushSite(dr::Site::COLOUR_FLUSH_END, t, Field<uint32_t>(t, 0x6c), Field<uint32_t>(t, 0x74), pending);
    Field<uint32_t>(t, 0x58) = 0;
    Field<uint32_t>(t, 0x78) = 0;
  }
}

struct Spec {
  uint32_t items = 6000;
  uint32_t seed = 1;
  uint32_t max_run = 8;
  uint32_t none_percent = 10;
  uint32_t anim_minus_percent = 0;
  uint32_t anim_change_percent = 20;
  uint32_t max_count = 1000;
  bool pattern = false;
  // Items from this percent of the list on use meshes without an enabled technique.
  uint32_t enabled_percent = 100;
  // The list starts with 40 items of one mesh, and the tracker starts as a continuation of it with foreign mesh state.
  bool continuation = false;
  // The driver's portion = this / the tracker's stride.
  uint32_t portion_numerator = 12000;
  // The colour loop's list: items with the colour fields.
  bool colour = false;
  // Every run of items gets a mesh of its own (the list never returns to a mesh).
  bool unique_meshes = false;
  // The first and the last items but 5 share a mesh (with unique_meshes).
  bool repeat_far = false;
  // Meshes of a class outside the census.
  uint32_t foreign_percent = 0;
  uint32_t variation_percent = 20;
  // Items that only set the "any" byte (no count).
  uint32_t any_only_percent = 0;
  // Meshes of kind 6 (no shared-state call at the setup), constants of one value, mostly any-only items.
  bool merge = false;
  // Items that set the first constant / the second constant / the textures / the faction block.
  uint32_t const1_percent = 50;
  uint32_t const2_percent = 60;
  uint32_t texture_percent = 60;
  uint32_t faction_percent = 50;
};

struct World {
  std::vector<SynthMesh> meshes;
  std::vector<SynthItem> items;
  std::vector<SynthItem*> pointers;
  std::array<SynthMask, 3> masks = {};
  bool colour = false;
  SynthList list = {};
  alignas(16) std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  uint32_t max_count = 1000;
  uint32_t portion_numerator = 12000;
  bool continuation = false;
  // The tracker's state when a call starts (a call the loop itself ended leaves both 0).
  uint32_t pending = 0;
  uint32_t change_mask = 0;
};

static std::unique_ptr<World> BuildWorld(const Spec& spec) {
  auto world = std::make_unique<World>();
  uint64_t state = spec.seed * 0x9e3779b97f4a7c15ull + 1;
  const auto random = [&state](uint32_t bound) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<uint32_t>((state >> 11) % bound);
  };
  world->max_count = spec.max_count;
  world->portion_numerator = spec.portion_numerator;
  world->continuation = spec.continuation;
  world->colour = spec.colour;
  world->meshes.resize(spec.unique_meshes ? spec.items + 1 : 60);
  for (uint32_t m = 0; m < world->meshes.size(); ++m) {
    SynthMesh& mesh = world->meshes[m];
    mesh.vtable = ((spec.foreign_percent != 0 && random(100) < spec.foreign_percent) ? foreign_vtable.data() : mesh_vtable.data());
    mesh.flag = static_cast<uint8_t>(m & 1);
    if (spec.merge) {
      mesh.kind = 6;
    } else if (spec.pattern) {
      mesh.kind = (m < 6 ? 2 + m % 4 : 0);
    } else if (spec.enabled_percent < 100) {
      mesh.kind = (m < 4 ? m % 2 : 2 + random(4));
    } else if (random(100) < spec.none_percent) {
      mesh.kind = random(2);
    } else {
      mesh.kind = 2 + random(4);
    }
  }
  world->items.resize(spec.items);
  uint32_t i = 0;
  int32_t anim = 0;
  uint32_t pattern_run = 0;
  while (i < spec.items) {
    uint32_t mesh_index = 0;
    uint32_t run = 1;
    if (spec.unique_meshes) {
      mesh_index = pattern_run++;
      run = 1 + random(1u << random(1 + static_cast<uint32_t>(std::bit_width(spec.max_run))));
      run = std::min(run, spec.max_run);
    } else if (spec.pattern) {
      // Five items of a valid mesh, three of one without a technique.
      const bool none = (pattern_run % 2 == 1);
      mesh_index = (none ? 6 + (pattern_run / 2) % 4 : (pattern_run / 2) % 6);
      run = (none ? 3 : 5);
      ++pattern_run;
    } else {
      if (spec.enabled_percent < 100) {
        const bool tail = (static_cast<uint64_t>(i) * 100 >= static_cast<uint64_t>(spec.items) * spec.enabled_percent);
        mesh_index = (tail ? random(4) : 4 + random(static_cast<uint32_t>(world->meshes.size()) - 4));
      } else {
        mesh_index = random(static_cast<uint32_t>(world->meshes.size()));
      }
      // Log-uniform lengths from 1 to max_run.
      run = 1 + random(1u << random(1 + static_cast<uint32_t>(std::bit_width(spec.max_run))));
      run = std::min(run, spec.max_run);
    }
    if (!spec.pattern && random(100) < spec.anim_minus_percent) anim = -1;
    for (uint32_t r = 0; r < run && i < spec.items; ++r, ++i) {
      if (spec.pattern) {
        anim = 5;
      } else if (random(100) < spec.anim_change_percent) {
        anim = (random(100) < spec.anim_minus_percent ? -1 : static_cast<int32_t>(random(7)));
      }
      world->items[i].mesh = &world->meshes[mesh_index];
      world->items[i].anim = anim;
    }
  }
  if (spec.continuation) {
    world->meshes[0].kind = 2;
    for (uint32_t r = 0; r < 40 && r < spec.items; ++r) {
      world->items[r].mesh = &world->meshes[0];
      world->items[r].anim = 0;
    }
    // The colour list returns to the mesh once, a little later in the same chunk.
    for (uint32_t r = 100; spec.colour && r < 110 && r < spec.items; ++r) world->items[r].mesh = &world->meshes[0];
  }
  if (spec.repeat_far) {
    for (uint32_t r = 0; r < 5; ++r) world->items[spec.items - 5 + r].mesh = world->items[5].mesh;
  }
  if (spec.unique_meshes && spec.enabled_percent < 100) {
    // The meshes of the last items have no technique.
    for (uint32_t r = spec.items * spec.enabled_percent / 100; r < spec.items; ++r) world->items[r].mesh->kind = random(2);
  }
  if (spec.colour) {
    // Factions 0 to 3 have lists of 6, 3, 70 and 4 colours (all pointing into one table); -1 is the default.
    for (uint32_t e = 0; e < faction_colours.size(); ++e) {
      faction_colours[e] = {{1.0f + 0.5f * static_cast<float>(e), 0.25f * static_cast<float>(e), 0.5f + 0.125f * static_cast<float>(e)}};
    }
    const std::array<uint32_t, 4> list_counts = {6, 3, 70, 4};
    for (uint32_t f = 0; f < 4; ++f) faction_lists[f] = {.count = list_counts[f], .data = faction_colours.data()};
    for (uint32_t m = 0; m < world->masks.size(); ++m) {
      uint8_t* bytes = reinterpret_cast<uint8_t*>(&world->masks[m]);
      for (uint32_t j = 0; j < sizeof(SynthMask); ++j) bytes[j] = static_cast<uint8_t>(j * 37 + m * 11);
      world->masks[m].index = static_cast<int32_t>(10 + m);
      for (uint32_t j = 0; j < 4; ++j) world->masks[m].bytes[j] = static_cast<uint8_t>(j + 3 * m);
    }
    const std::array<uint32_t, 6> values = {0x00000000, 0x80000000, 0x7fc00000, 0x3f800000, 0x40000000, 0x3f800000};
    int32_t faction = -1;
    uint32_t value_index = 0;
    uint32_t texture1 = 1;
    uint32_t texture2 = 1;
    for (SynthItem& item : world->items) {
      // Fields change now and then, so the tracker's detection fields hold across items and across a chunk's first item.
      if (random(100) < 15) faction = static_cast<int32_t>(random(5)) - 1;
      if (random(100) < 20) value_index = random(static_cast<uint32_t>(values.size()));
      if (random(100) < 25) texture1 = random(4);
      if (random(100) < 25) texture2 = random(3);
      item.faction = faction;
      item.value = values[value_index];
      item.texture1 = texture1;
      item.texture2 = texture2;
      item.repeat = 1 + random(3);
      item.variation = (random(100) < spec.variation_percent ? 1 + random(3) : 0);
      item.mask = (random(3) == 0 ? nullptr : &world->masks[random(3)]);
      item.flags = (random(100) < spec.const1_percent ? ITEM_CONST1 : 0) | (random(100) < spec.const2_percent ? ITEM_CONST2 : 0)
                   | (random(100) < spec.texture_percent ? ITEM_TEXTURES : 0) | (random(100) < spec.faction_percent ? ITEM_FACTION : 0)
                   | (random(100) < spec.any_only_percent ? ITEM_ANY_ONLY : 0);
      if (spec.merge) {
        // One animation for the whole list (the loop starts with it): every seed hits.
        item.flags = ITEM_CONST2 | (random(100) < 70 ? ITEM_ANY_ONLY : 0);
        item.value = values[3];
        item.variation = 0;
        item.anim = -1;
      }
    }
  }
  world->pointers.resize(spec.items);
  for (uint32_t k = 0; k < spec.items; ++k) world->pointers[k] = &world->items[k];
  world->list = {.count = spec.items, .data = world->pointers.data()};
  return world;
}

struct Outcome {
  std::vector<LogEntry> log;
  std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  std::vector<std::array<uint32_t, 2>> mesh_fields;
  // The constants' cells at the end (bits).
  std::array<uint32_t, 5> cells = {};
  uint32_t chunks = 0;
  uint32_t reruns = 0;
  uint32_t replayed = 0;
  uint32_t ops = 0;
  uint32_t direct_calls = 0;
  // The driver's calls, and those with fewer items than the dispatcher's floor.
  uint32_t portions = 0;
  uint32_t small_calls = 0;
  std::array<uint32_t, 6> compares = {};
  // Trace frames: the calls that ran directly because a mesh recurred in two chunks, the ops by kind, and the constants that a
  // chunk end split (the compare run's op went on in the next chunk).
  uint32_t conflicts = 0;
  std::array<uint32_t, static_cast<size_t>(di::OpKind::COUNT)> kind_ops = {};
  uint32_t split_constants = 0;
  // Trace frames: total + the trace-only phases == wall, in every call.
  bool accounted = true;
  uint32_t most_buffer = 0;
  std::string first_reason;
};

static bool CompareDiffers(const Outcome& outcome) {
  return outcome.compares[static_cast<size_t>(di::CompareResult::DIFFERS)] + outcome.compares[static_cast<size_t>(di::CompareResult::COMPARE_HAS_MORE)] > 0;
}

static void ResetWorld(World* world) {
  for (SynthMesh& mesh : world->meshes) {
    mesh.view_tag = 0xffffffff;
    mesh.pass = 0xffffffff;
  }
  world->tracker = {};
  uint8_t* t = world->tracker.data();
  Field<uint32_t>(t, 0x18) = 0xd0;
  Field<uint32_t>(t, 0x1c) = 0xe0;
  Field<uint32_t>(t, 0x20) = 0x12345;
  Field<uint32_t>(t, 0x24) = 0xb0;
  Field<uint32_t>(t, 0x50) = 6;
  Field<uint32_t>(t, 0x64) = world->max_count;
  strict_flush = !world->colour;
  if (world->colour) {
    // The detection fields start unlike any item's; the constants' cells and the cache flag are the test's.
    Field<int32_t>(t, 0x00) = -2;
    Field<int32_t>(t, 0x0c) = -2;
    // The flush writes the mesh's +0x40 with the value the setup wrote (the view's type, once per tracker).
    Field<uint32_t>(t, 0x20) = Field<uint32_t>(t, 0x1c) ^ 0x77;
    const1_cells.fill(1.0f);
    const2_cell = 0.5f;
  }
  if (world->continuation || world->pending != 0 || world->change_mask != 0) {
    // A call that starts in the middle of a mesh whose state is another view's (or not this call's).
    SynthMesh* mesh = (world->continuation ? world->items[0].mesh : &world->meshes.back());
    Field<uint32_t>(t, 0x70) = reinterpret_cast<uint32_t>(mesh);
    Field<uint32_t>(t, 0x74) = 0x20;
    Field<uint32_t>(t, 0x58) = world->pending;
    Field<uint32_t>(t, 0x78) = world->change_mask;
    mesh->view_tag = 0xabc;
    mesh->pass = 0x77;
  }
  g_shared->seed_anim = -1;
  g_shared->last_anim = -1;
  game_log.clear();
}

static Outcome Capture(World* world) {
  Outcome outcome = {.log = game_log, .tracker = world->tracker};
  for (const SynthMesh& mesh : world->meshes) outcome.mesh_fields.push_back({mesh.view_tag, mesh.pass});
  for (uint32_t i = 0; i < 4; ++i) std::memcpy(&outcome.cells[i], &const1_cells[i], 4);
  std::memcpy(&outcome.cells[4], &const2_cell, 4);
  return outcome;
}

static void* const NODE = reinterpret_cast<void*>(0x1000);

// The serial loop of the world's group, and the same call through its dispatcher.
static void SerialPortion(World* world, uint32_t begin, uint32_t end) {
  if (world->colour) {
    SynthColourLoop(&synth_node, world->tracker.data(), &world->list, 0, begin, end);
  } else {
    SynthDepthLoop(NODE, world->tracker.data(), &world->list, begin, end);
  }
}

static void DispatchPortion(World* world, uint32_t begin, uint32_t end) {
  if (world->colour) {
    dr::DispatchColour(&synth_node, nullptr, world->tracker.data(), &world->list, 0, begin, end);
  } else {
    dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, begin, end);
  }
}

// The driver: portions of the list sized from the tracker's +0x50 (read after each portion), back to back on one tracker.
template <class RunPortion>
static void Driver(World* world, RunPortion&& run_portion) {
  const uint32_t total = world->list.count;
  for (uint32_t begin = 0; begin < total;) {
    const uint32_t stride = std::max<uint32_t>(1, Field<uint32_t>(world->tracker.data(), 0x50));
    const uint32_t end = std::min(total, begin + world->portion_numerator / stride);
    run_portion(begin, end);
    begin = end;
  }
}

static Outcome RunSerial(World* world) {
  ResetWorld(world);
  for (int pass = 0; pass < 2; ++pass) {
    Driver(world, [&](uint32_t begin, uint32_t end) { SerialPortion(world, begin, end); });
  }
  return Capture(world);
}

// Two passes of the driver through the dispatcher (the world is reset and the settings applied).
static Outcome RunCalls(World* world, bool tracing) {
  Outcome total = {};
  for (int pass = 0; pass < 2; ++pass) {
    Driver(world, [&](uint32_t begin, uint32_t end) {
      DispatchPortion(world, begin, end);
      ++total.portions;
      total.small_calls += (end - begin < di::MIN_ITEMS ? 1 : 0);
      total.chunks += di::last_call.chunks;
      total.reruns += di::last_call.reruns;
      total.replayed += di::last_call.replayed;
      total.ops += di::last_call.ops;
      total.direct_calls += (di::last_call.direct ? 1 : 0);
      total.split_constants += di::last_call.split_constants;
      if (tracing && !di::trace.calls.empty()) {
        const di::CallTrace& call = di::trace.calls.back();
        ++total.compares[static_cast<size_t>(call.compare_result)];
        if (total.first_reason.empty()) total.first_reason = call.direct_reason;
        total.conflicts += (std::string_view(call.direct_reason) == "conflict" ? 1 : 0);
        for (size_t kind = 0; kind < call.ops.size(); ++kind) total.kind_ops[kind] += call.ops[kind];
      }
    });
  }
  if (tracing) {
    for (const di::CallTrace& call : di::trace.calls) {
      const uint64_t trace_only = call.compare + call.run_scan + (call.stage == dr::Stage::CHUNKED ? call.warm_ups : 0);
      total.accounted = total.accounted && call.total + trace_only == call.wall;
      total.most_buffer = std::max(total.most_buffer, call.op_buffer_bytes);
    }
  }
  di::trace.active = false;
  Outcome outcome = Capture(world);
  outcome.chunks = total.chunks;
  outcome.reruns = total.reruns;
  outcome.replayed = total.replayed;
  outcome.ops = total.ops;
  outcome.direct_calls = total.direct_calls;
  outcome.portions = total.portions;
  outcome.small_calls = total.small_calls;
  outcome.compares = total.compares;
  outcome.conflicts = total.conflicts;
  outcome.kind_ops = total.kind_ops;
  outcome.split_constants = total.split_constants;
  outcome.accounted = total.accounted;
  outcome.most_buffer = total.most_buffer;
  outcome.first_reason = total.first_reason;
  return outcome;
}

static Outcome RunStaged(World* world, dr::Stage stage, uint32_t threads, bool tracing = false, bool update = true) {
  ResetWorld(world);
  if (update) dr::Update({.stage = stage, .threads = threads, .loops = (world->colour ? 2u : 1u)}, true);
  di::trace.active = tracing;
  di::trace.frames_left = 1u << 30;
  di::trace.calls.clear();
  return RunCalls(world, tracing);
}

static bool Equal(const Outcome& serial, const Outcome& staged, const char* label, bool print = true) {
  bool equal = true;
  if (serial.log != staged.log) {
    equal = false;
    if (print) {
      size_t at = 0;
      while (at < std::min(serial.log.size(), staged.log.size()) && serial.log[at] == staged.log[at]) ++at;
      std::printf("  %s: logs differ at entry %zu of %zu / %zu\n", label, at, serial.log.size(), staged.log.size());
      for (const std::vector<LogEntry>* log : {&serial.log, &staged.log}) {
        if (at >= log->size()) continue;
        std::printf("    %s:", (log == &serial.log ? "serial" : "staged"));
        for (const uint32_t word : (*log)[at]) std::printf(" %x", word);
        std::printf("\n");
      }
    }
  }
  if (serial.tracker != staged.tracker) {
    equal = false;
    if (print) std::printf("  %s: tracker bytes differ\n", label);
  }
  if (serial.mesh_fields != staged.mesh_fields) {
    equal = false;
    if (print) std::printf("  %s: mesh fields differ\n", label);
  }
  if (serial.cells != staged.cells) {
    equal = false;
    if (print) std::printf("  %s: constant cells differ\n", label);
  }
  return equal;
}

static const std::array<dr::Stage, 4> RECORD_STAGES = {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS};
static const char* StageName(dr::Stage stage) {
  switch (stage) {
    case dr::Stage::CHUNKED: return "chunked";
    case dr::Stage::RECORDED: return "recorded";
    case dr::Stage::ONE_WORKER: return "one worker";
    case dr::Stage::WORKERS: return "workers";
    case dr::Stage::TOUCH_AHEAD: return "touch-ahead";
    default: return "?";
  }
}

struct ListCase {
  const char* name;
  Spec spec;
  bool expect_chunks;
};

static std::vector<ListCase> Lists() {
  std::vector<ListCase> lists;
  lists.push_back({"short runs", {.items = 6000, .seed = 1, .max_run = 8}, true});
  lists.push_back({"runs 1 to 300", {.items = 9000, .seed = 2, .max_run = 300, .none_percent = 15}, true});
  lists.push_back({"animation -1 runs", {.items = 6000, .seed = 3, .max_run = 20, .anim_minus_percent = 60, .anim_change_percent = 40}, true});
  lists.push_back({"no technique before cuts", {.items = 6000, .seed = 4, .max_run = 6, .none_percent = 45}, true});
  lists.push_back({"seed breaking pattern", {.items = 4000, .seed = 5, .pattern = true}, true});
  lists.push_back({"max count flushes", {.items = 6000, .seed = 6, .max_run = 120, .max_count = 6}, true});
  lists.push_back({"last chunks without technique", {.items = 6000, .seed = 7, .max_run = 12, .enabled_percent = 10}, true});
  // No animation changes: every seed hits, so the chunks without a technique are replayed and copied back, not run again.
  lists.push_back({"last chunks without technique or animation", {.items = 6000, .seed = 8, .max_run = 12, .anim_minus_percent = 100, .enabled_percent = 10}, true});
  return lists;
}

static dr::GameCalls Game(bool usable) {
  return {.loops = {{reinterpret_cast<uint32_t>(&SynthDepthLoop), reinterpret_cast<uint32_t>(&SynthColourLoop)}},
          .flush = reinterpret_cast<uint32_t>(&FakeFlush),
          .anim_call = reinterpret_cast<uint32_t>(&FakeAnimCall),
          .dirty_call = reinterpret_cast<uint32_t>(&FakeDirty),
          .view_call = reinterpret_cast<uint32_t>(&FakeView),
          .srv_call = reinterpret_cast<uint32_t>(&FakeSrv),
          .faction_call = reinterpret_cast<uint32_t>(&FakeFactionColours),
          .cached_constants = &cached_constants,
          .faction_default = &faction_default,
          .weighted_vtables = {{reinterpret_cast<uint32_t>(mesh_vtable.data())}},
          .usable = usable};
}

static void Setup() {
  mesh_vtable.fill(0);
  mesh_vtable[0x68 / 4] = reinterpret_cast<uint32_t>(&FakeVf68);
  foreign_vtable = mesh_vtable;
  node_vtable.fill(0);
  node_vtable[0xc0 / 4] = reinterpret_cast<uint32_t>(&FakeFactionLists);
  synth_node.vtable = node_vtable.data();
  // The first constant's four blocks and the second constant's block (state + 0x2d4, its cell pointer at +0x14 of it).
  for (uint32_t i = 0; i < const1_blocks.size(); ++i) const1_blocks[i].cell = &const1_cells[i];
  *reinterpret_cast<float**>(synth_node.state + 0x2e8) = &const2_cell;
  g_shared = dr::SharedPage();
  REQUIRE(g_shared != nullptr);
  REQUIRE(g_shared->record == reinterpret_cast<uint32_t>(&dr::Record));
  REQUIRE(g_shared->dispatch_depth == reinterpret_cast<uint32_t>(&dr::DispatchDepth));
  REQUIRE(g_shared->dispatch_colour == reinterpret_cast<uint32_t>(&dr::DispatchColour));
  dr::SetGame(Game(true));
  // Every traced call compares (a real trace compares only in its last frame); DepthTrace checks the real rule.
  di::controls.compare_every_call = true;
}

static void DepthStages() {
  uint32_t cases = 0;
  uint32_t reruns_seen = 0;
  for (const ListCase& list : Lists()) {
    const auto world = BuildWorld(list.spec);
    const Outcome serial = RunSerial(world.get());
    CHECK(serial.log.size() > 100);
    for (const dr::Stage stage : RECORD_STAGES) {
      for (uint32_t k = 2; k <= 16; ++k) {
        for (const bool tracing : {false, true}) {
          const Outcome staged = RunStaged(world.get(), stage, k, tracing);
          char label[128];
          std::snprintf(label, sizeof(label), "%s, %s, K %u%s", list.name, StageName(stage), k, (tracing ? ", traced" : ""));
          const bool equal = Equal(serial, staged, label);
          CHECK(equal);
          CHECK(staged.portions > 2);
          CHECK(staged.direct_calls == staged.small_calls);
          CHECK(staged.accounted);
          if (list.expect_chunks && k >= 4) CHECK(staged.chunks >= 2 * 2);
          if (stage != dr::Stage::CHUNKED) CHECK(staged.replayed + staged.reruns == staged.chunks);
          if (stage != dr::Stage::CHUNKED && staged.chunks > 0) CHECK(staged.ops > 0 || staged.reruns > 0);
          CHECK(!CompareDiffers(staged));
          reruns_seen += staged.reruns;
          ++cases;
        }
      }
    }
  }
  CHECK(reruns_seen > 0);
  std::printf("depth: %u cases (8 lists x 4 stages x K 2..16 x trace off / on, portions at begin != 0): logs, tracker (+0x50, +0x5c, +0x28, +0x69 included) and mesh fields equal the serial run; %u chunks run again\n", cases, reruns_seen);
}

static void DepthFlushEntry() {
  test_controls.entry_site = true;
  uint32_t cases = 0;
  for (const ListCase& list : Lists()) {
    const auto world = BuildWorld(list.spec);
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      for (const uint32_t k : {4u, 16u}) {
        const Outcome staged = RunStaged(world.get(), stage, k, true);
        CHECK(Equal(serial, staged, list.name));
        CHECK(staged.ops > 0);
        ++cases;
      }
    }
  }
  test_controls.entry_site = false;
  std::printf("depth: the flush entry's site id (shared group) for every flush: %u cases equal\n", cases);
}

static void DepthTouchAhead() {
  const auto world = BuildWorld({.items = 6000, .seed = 11, .max_run = 20});
  const Outcome serial = RunSerial(world.get());
  for (uint32_t k = 2; k <= 16; ++k) {
    const uint32_t before = di::touch_generation.load();
    const Outcome staged = RunStaged(world.get(), dr::Stage::TOUCH_AHEAD, k);
    CHECK(Equal(serial, staged, "touch-ahead"));
    CHECK(staged.direct_calls == staged.small_calls);
    CHECK(di::descriptor.workers == k - 1);
    CHECK(di::touch_generation.load() == before + (staged.portions - staged.small_calls));
  }
  std::printf("depth: touch-ahead at K 2..16: the loop unchanged, the helpers stopped after each call\n");
}

static void DepthOverflow() {
  const auto world = BuildWorld({.items = 6000, .seed = 12, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  uint32_t reruns = 0;
  for (const uint32_t limit : {200u, 1000u, 4000u}) {
    di::op_buffer_limit = limit;
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      for (uint32_t k : {2u, 5u, 16u}) {
        const Outcome staged = RunStaged(world.get(), stage, k, true);
        CHECK(Equal(serial, staged, "tiny buffers"));
        reruns += staged.reruns;
        CHECK(staged.reruns > 0);
        CHECK(!CompareDiffers(staged));
      }
    }
  }
  di::op_buffer_limit = di::OP_ARENA_BYTES;
  std::printf("depth: tiny op buffers: overflowing chunks run again, logs equal (%u reruns)\n", reruns);
}

// One call of 200000 items: chunks larger than the op buffer's reserve.
static void DepthLarge() {
  const auto world = BuildWorld({.items = 200000, .seed = 41, .max_run = 8, .max_count = 6, .portion_numerator = 12000000});
  const Outcome serial = RunSerial(world.get());
  for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
    for (const uint32_t k : {2u, 3u}) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "large call"));
      CHECK(staged.portions == 2 && staged.direct_calls == 0);
      CHECK(staged.accounted);
      CHECK(staged.most_buffer <= di::OP_ARENA_BYTES);
    }
  }
  CHECK(di::op_committed <= di::OP_ARENA_BYTES);
  std::printf("depth: 200000 items in one call at K 2 / 3: logs equal, op memory within its %u MB reserve (%u KB committed)\n",
              di::OP_ARENA_BYTES >> 20, di::op_committed >> 10);
}

// A call that starts in a state the chunks can't reproduce runs directly.
static void DepthEntryState() {
  const auto world = BuildWorld({.items = 6000, .seed = 16, .max_run = 10});
  for (const uint32_t variant : {0u, 1u}) {
    world->pending = (variant == 0 ? 5 : 0);
    world->change_mask = (variant == 1 ? 8 : 0);
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      const Outcome staged = RunStaged(world.get(), stage, 8, true);
      CHECK(Equal(serial, staged, (variant == 0 ? "pending instances" : "change mask")));
      CHECK(staged.first_reason == "entry state");
      CHECK(staged.direct_calls > staged.small_calls);
    }
  }
  world->pending = 0;
  world->change_mask = 0;
  std::printf("depth: a call that starts with pending instances or a change mask runs directly (entry state), logs equal\n");
}

// The first mesh of the call is the tracker's last one, with state another view wrote and that recurs later in the list.
static void DepthContinuation() {
  const auto world = BuildWorld({.items = 6000, .seed = 17, .max_run = 10, .max_count = 6, .continuation = true});
  const Outcome serial = RunSerial(world.get());
  uint32_t cases = 0;
  for (const dr::Stage stage : {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
    for (uint32_t k = 2; k <= 16; ++k) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "continuation"));
      CHECK(staged.first_reason == "continuation");
      ++cases;
    }
  }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  // Guard off: the compare run has set the mesh up before chunk 0's snapshot is taken, so the replay flushes it with the wrong state.
  di::controls.guard_continuation = false;
  const Outcome unguarded = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
  di::controls.guard_continuation = true;
  CHECK(!Equal(serial, unguarded, "control: continuation guard off", false));
#endif
  std::printf("depth: a continuation of foreign mesh state runs directly (%u cases equal)\n", cases);
}

static void DepthFallbacks() {
  const auto world = BuildWorld({.items = 6000, .seed = 13, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  // Fewer than 512 items.
  const auto few_items = BuildWorld({.items = 300, .seed = 14, .max_run = 10});
  const Outcome few_serial = RunSerial(few_items.get());
  const Outcome few_staged = RunStaged(few_items.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(few_serial, few_staged, "small call"));
  CHECK(few_staged.direct_calls == few_staged.portions && few_staged.portions == 2);
  // The stage Direct and the row off.
  for (const dr::Stage stage : {dr::Stage::DIRECT, dr::Stage::OFF}) {
    const Outcome staged = RunStaged(world.get(), stage, 8);
    CHECK(Equal(serial, staged, "stage direct"));
    CHECK(staged.direct_calls == staged.portions);
  }
  // The pool smaller than the stage needs.
  dr::Update({.stage = dr::Stage::OFF}, false);
  RunStaged(world.get(), dr::Stage::WORKERS, 4);
  CHECK(di::worker_count == 3);
  di::config.threads = 16;
  const Outcome missing = RunStaged(world.get(), dr::Stage::WORKERS, 16, false, false);
  CHECK(Equal(serial, missing, "pool missing"));
  CHECK(missing.direct_calls == missing.portions);
  // Another thread.
  dr::Update({.stage = dr::Stage::WORKERS, .threads = 8, .loops = 1}, true);
  ResetWorld(world.get());
  Outcome other_outcome;
  std::thread other([&] { other_outcome = RunCalls(world.get(), false); });
  other.join();
  CHECK(other_outcome.direct_calls == other_outcome.portions);
  CHECK(Equal(serial, other_outcome, "other thread"));
  // A loop group that is off.
  dr::Update({.stage = dr::Stage::WORKERS, .threads = 8, .loops = 2}, true);
  ResetWorld(world.get());
  const Outcome loop_off = RunCalls(world.get(), false);
  CHECK(loop_off.direct_calls == loop_off.portions);
  CHECK(Equal(serial, loop_off, "loop off"));
  // Not usable.
  dr::SetGame(Game(false));
  const Outcome unusable = RunStaged(world.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(serial, unusable, "not usable"));
  CHECK(unusable.direct_calls == unusable.portions);
  dr::SetGame(Game(true));
  std::printf("depth: fall-backs (small call, Direct, Off, pool missing, other thread, loop off, not usable) run the loop directly, logs equal\n");
}

static void DepthTrace() {
  const auto world = BuildWorld({.items = 6000, .seed = 15, .max_run = 12});
  // The trace armed through the environment: written next to the exe after its frames.
  const std::filesystem::path file = std::filesystem::path(__argv[0]).parent_path() / "tw_draw_recording.txt";
  std::filesystem::remove(file);
  SetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", "0,3");
  di::trace.read_environment = false;
  di::trace.armed = false;
  di::trace.finished = false;
  di::controls.compare_every_call = false;
  ResetWorld(world.get());
  const dr::Settings settings = {.stage = dr::Stage::WORKERS, .threads = 6, .loops = 1};
  dr::Update(settings, true);
  CHECK(di::trace.armed);
  dr::Update(settings, true);
  CHECK(di::trace.active);
  // Frames 1 and 2 of the trace are timed (no compare, no compare slot); the last one compares.
  for (int frame = 0; frame < 2; ++frame) {
    const size_t calls_before = di::trace.calls.size();
    Driver(world.get(), [&](uint32_t begin, uint32_t end) { dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, begin, end); });
    CHECK(di::trace.calls.size() > calls_before);
    for (size_t i = calls_before; i < di::trace.calls.size(); ++i) {
      const di::CallTrace& call = di::trace.calls[i];
      CHECK(call.compared == (frame == 1));
      CHECK((call.compare != 0) == (frame == 1 && call.chunks > 1));
      CHECK(call.run_scan != 0);
      CHECK((call.compare_result == di::CompareResult::NONE) == !(frame == 1 && call.chunks > 1));
      CHECK(call.compare_result != di::CompareResult::DIFFERS);
    }
    dr::Update(settings, true);
  }
  di::controls.compare_every_call = true;
  CHECK(!di::trace.active && di::trace.finished);
  std::ifstream stream(file, std::ios::binary);
  const std::string text_value((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  CHECK(text_value.find("compare: equal") != std::string::npos);
  CHECK(text_value.find("compare: not run (timing frame)") != std::string::npos);
  CHECK(text_value.find(" timing\r\n") != std::string::npos && text_value.find(" compared\r\n") != std::string::npos);
  CHECK(text_value.find("frame:") != std::string::npos);
  CHECK(text_value.find("K 6") != std::string::npos);
  CHECK(text_value.find("chunk items:") != std::string::npos);
  CHECK(text_value.find("group 0 family -") != std::string::npos);
  CHECK(text_value.find("const ") != std::string::npos && text_value.find("srv ") != std::string::npos && text_value.find("faction ") != std::string::npos);
  CHECK(text_value.find("differs") == std::string::npos);
  SetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", nullptr);
  stream.close();
  std::filesystem::remove(file);
  std::printf("depth: trace armed from the environment wrote the file (group, K, chunk sizes), compare equal\n");
}

// The Record rules the thunks rely on, called directly.
static void RecordRules() {
  static dr::Slot unit;
  static std::array<uint32_t, 64> buffer;
  static SynthMesh mesh;
  mesh = {};
  mesh.view_tag = 0x40;
  mesh.pass = 0x44;
  const auto reset = [&](uint32_t group, uint32_t mode) {
    unit = {};
    unit.group = group;
    unit.mode = mode;
    unit.ops_begin = unit.ops_cursor = buffer.data();
    unit.ops_end = buffer.data() + buffer.size();
    buffer.fill(0);
  };
  const auto flush = [&](dr::Site site, uint32_t count) {
    StackImage image = MakeImage(site, reinterpret_cast<uint32_t>(&mesh));
    image.stack[5] = count;
    dr::Record(&unit, &image.frame);
  };
  CHECK(di::FLUSH_PAYLOAD == 14);
  // A site of another loop group in the chunk: run again, nothing recorded.
  reset(1, dr::MODE_RECORD);
  StackImage vf68 = MakeImage(dr::Site::DEPTH_VF68, reinterpret_cast<uint32_t>(&mesh));
  dr::Record(&unit, &vf68.frame);
  CHECK((unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0 && unit.ops_cursor == unit.ops_begin);
  // A site id past the table.
  reset(0, dr::MODE_RECORD);
  StackImage unknown = MakeImage(static_cast<dr::Site>(static_cast<uint32_t>(dr::Site::COUNT) + 3), 0);
  dr::Record(&unit, &unknown.frame);
  CHECK((unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0);
  // The flush entry (shared group) records in any loop group, with the any byte.
  reset(1, dr::MODE_RECORD);
  unit.tracker[0x69] = 1;
  flush(dr::Site::FLUSH_ENTRY, 3);
  CHECK(unit.flags == 0 && unit.ops_cursor == unit.ops_begin + 1 + di::FLUSH_PAYLOAD);
  CHECK((buffer[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::FLUSH) && (buffer[0] >> 16) == 1 + di::FLUSH_PAYLOAD);
  CHECK(buffer[1 + 13] == 1 && unit.tracker[0x69] == 0 && buffer[1 + 11] == 0x40 && buffer[1 + 12] == 0x44);
  // Suppress mode: the byte is cleared, nothing recorded.
  reset(0, dr::MODE_SUPPRESS);
  unit.tracker[0x69] = 1;
  flush(dr::Site::DEPTH_FLUSH_END, 3);
  CHECK(unit.tracker[0x69] == 0 && unit.ops_cursor == unit.ops_begin && unit.flags == 0);
  // A flush of no instances leaves the byte alone, in both modes.
  for (const uint32_t mode : {dr::MODE_RECORD, dr::MODE_SUPPRESS}) {
    reset(0, mode);
    unit.tracker[0x69] = 1;
    flush(dr::Site::DEPTH_FLUSH_END, 0);
    CHECK(unit.tracker[0x69] == 1 && unit.ops_cursor == unit.ops_begin);
  }
  // A full buffer: flagged, and the byte is cleared all the same.
  reset(0, dr::MODE_RECORD);
  unit.ops_end = unit.ops_begin + 3;
  unit.tracker[0x69] = 1;
  flush(dr::Site::DEPTH_FLUSH_END, 3);
  CHECK((unit.flags & dr::SLOT_OVERFLOW) != 0 && unit.tracker[0x69] == 0);
  // The animation site keeps the index in both modes.
  for (const uint32_t mode : {dr::MODE_RECORD, dr::MODE_SUPPRESS}) {
    reset(0, mode);
    StackImage anim = MakeImage(dr::Site::DEPTH_ANIM, 0x1000);
    anim.stack[9] = 5;
    dr::Record(&unit, &anim.frame);
    CHECK(unit.anim == 5 && (unit.ops_cursor != unit.ops_begin) == (mode == dr::MODE_RECORD));
  }
  std::printf("record: group check, unknown site, flush entry with the any byte (record, suppress, count 0, overflow), animation index\n");
}

// Each control makes one thing wrong; its run must differ (or the compare must report it).
static void DepthControls() {
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  const auto world = BuildWorld({.items = 6000, .seed = 21, .max_run = 8});
  const Outcome serial = RunSerial(world.get());
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 8), "control baseline"));

  const auto pattern = BuildWorld({.items = 4000, .seed = 22, .pattern = true});
  const Outcome pattern_serial = RunSerial(pattern.get());
  // With validation on the pattern list is run again at its misses; off, the run differs.
  const Outcome validated = RunStaged(pattern.get(), dr::Stage::RECORDED, 8);
  CHECK(Equal(pattern_serial, validated, "pattern validated"));
  CHECK(validated.reruns > 0);
  di::controls.validate_seeds = false;
  const Outcome unvalidated = RunStaged(pattern.get(), dr::Stage::RECORDED, 8);
  di::controls.validate_seeds = true;
  CHECK(!Equal(pattern_serial, unvalidated, "control: validation off", false));

  test_controls.drop_end_flush = true;
  const Outcome dropped = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  test_controls.drop_end_flush = false;
  CHECK(!Equal(serial, dropped, "control: dropped flush", false));

  di::controls.translate_pointers = false;
  const Outcome untranslated = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  di::controls.translate_pointers = true;
  CHECK(!Equal(serial, untranslated, "control: translation off", false));

  test_controls.anim_stack = 8;
  const Outcome wrong_anim = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  test_controls.anim_stack = 9;
  CHECK(!Equal(serial, wrong_anim, "control: wrong anim stack slot", false));

  // The any byte the loop sets itself must travel in the flush op.
  di::controls.capture_any = false;
  const Outcome uncaptured = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.capture_any = true;
  CHECK(!Equal(serial, uncaptured, "control: any capture off", false));

  // The stride of the last chunk that wrote one, not the last chunk's.
  const auto tail = BuildWorld({.items = 6000, .seed = 8, .max_run = 12, .anim_minus_percent = 100, .enabled_percent = 10});
  const Outcome tail_serial = RunSerial(tail.get());
  di::controls.stride_non_zero = false;
  const Outcome stale = RunStaged(tail.get(), dr::Stage::RECORDED, 8);
  di::controls.stride_non_zero = true;
  CHECK(!Equal(tail_serial, stale, "control: stride copied always", false));
  CHECK(Equal(tail_serial, RunStaged(tail.get(), dr::Stage::RECORDED, 8), "control: stride rule on"));

  // An op in one chunk that the serial run does not have: the log differs and the compare names it.
  test_controls.inject_chunk = 2;
  const Outcome injected = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
  test_controls.inject_chunk = -1;
  CHECK(!Equal(serial, injected, "control: injected op", false));
  CHECK(CompareDiffers(injected));

  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::WORKERS, 8), "controls cleared"));
  std::printf("controls: validation off, dropped flush, translation off, wrong anim slot, any capture off, stride copied always, injected op: each run differs (injection named by the compare)\n");
#else
  std::printf("controls: skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

// ---------------------------------------------------------------- part 3: the colour path

static std::vector<ListCase> ColourLists() {
  std::vector<ListCase> lists;
  lists.push_back({"colour short runs", {.items = 6000, .seed = 51, .max_run = 8, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour runs 1 to 300", {.items = 9000, .seed = 52, .max_run = 300, .none_percent = 15, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour animation -1 runs",
                   {.items = 6000, .seed = 53, .max_run = 20, .anim_minus_percent = 60, .anim_change_percent = 40, .colour = true, .unique_meshes = true},
                   true});
  lists.push_back({"colour no technique", {.items = 6000, .seed = 54, .max_run = 6, .none_percent = 45, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour max count flushes", {.items = 6000, .seed = 55, .max_run = 120, .max_count = 6, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour many variations", {.items = 6000, .seed = 56, .max_run = 30, .colour = true, .unique_meshes = true, .variation_percent = 70}, true});
  lists.push_back({"colour any-only items", {.items = 6000, .seed = 57, .max_run = 12, .colour = true, .unique_meshes = true, .any_only_percent = 20}, true});
  lists.push_back({"colour last chunks without technique",
                   {.items = 6000, .seed = 58, .max_run = 12, .enabled_percent = 10, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour constants only",
                   {.items = 6000, .seed = 59, .max_run = 10, .colour = true, .unique_meshes = true, .const1_percent = 100, .const2_percent = 100,
                    .texture_percent = 0, .faction_percent = 0},
                   true});
  lists.push_back({"colour constants merge across chunks",
                   {.items = 6000, .seed = 60, .max_run = 6, .anim_change_percent = 0, .colour = true, .unique_meshes = true, .merge = true}, true});
  return lists;
}

static const std::array<uint32_t, 8> COLOUR_K = {2, 3, 4, 5, 6, 8, 11, 16};

static void ColourStages() {
  uint32_t cases = 0;
  uint32_t reruns_seen = 0;
  uint32_t split_seen = 0;
  std::array<uint32_t, static_cast<size_t>(di::OpKind::COUNT)> kinds = {};
  for (const ListCase& list : ColourLists()) {
    const auto world = BuildWorld(list.spec);
    for (const uint8_t cache_flag : {uint8_t{0}, uint8_t{1}}) {
      cached_constants = cache_flag;
      const Outcome serial = RunSerial(world.get());
      CHECK(serial.log.size() > 100);
      for (const dr::Stage stage : RECORD_STAGES) {
        for (const uint32_t k : COLOUR_K) {
          for (const bool tracing : {false, true}) {
            const Outcome staged = RunStaged(world.get(), stage, k, tracing);
            char label[160];
            std::snprintf(label, sizeof(label), "%s, cache flag %u, %s, K %u%s", list.name, cache_flag, StageName(stage), k, (tracing ? ", traced" : ""));
            CHECK(Equal(serial, staged, label));
            CHECK(staged.portions > 2);
            CHECK(staged.direct_calls == staged.small_calls);
            CHECK(staged.accounted);
            CHECK(staged.conflicts == 0);
            if (stage != dr::Stage::CHUNKED) CHECK(staged.replayed + staged.reruns == staged.chunks);
            if (stage != dr::Stage::CHUNKED && staged.chunks > 0) CHECK(staged.ops > 0 || staged.reruns > 0);
            CHECK(!CompareDiffers(staged));
            reruns_seen += staged.reruns;
            split_seen += staged.split_constants;
            for (size_t kind = 0; kind < kinds.size(); ++kind) kinds[kind] += staged.kind_ops[kind];
            ++cases;
          }
        }
      }
    }
  }
  cached_constants = 0;
  CHECK(reruns_seen > 0);
  for (const di::OpKind kind : {di::OpKind::FLUSH, di::OpKind::VF68, di::OpKind::ANIM, di::OpKind::CONSTANT, di::OpKind::SRV, di::OpKind::FACTION}) {
    CHECK(kinds[static_cast<size_t>(kind)] > 0);
  }
  CHECK(split_seen > 0);
  std::printf("colour: %u cases (10 lists x cache flag 0 / 1 x 4 stages x 8 K x trace off / on): logs, tracker, mesh fields and constant cells equal the serial run; %u chunks run again; ops flush %u vf68 %u anim %u const %u srv %u faction %u; %u constants split by a chunk end\n",
              cases, reruns_seen, kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], split_seen);
}

// Meshes that recur in several chunks: the call runs directly (before any replay), and with the owner table off a write of
// another chunk landing before a snapshot makes the run differ.
static void ColourInterleaved() {
  uint32_t cases = 0;
  // (No animation changes, textures or faction blocks: the seeds hit, so the chunks of the control are replayed, not run again.)
  for (const Spec& spec : {Spec{.items = 6000, .seed = 61, .max_run = 8, .none_percent = 0, .anim_change_percent = 0, .portion_numerator = 12000000,
                                .colour = true, .texture_percent = 0, .faction_percent = 0},
                           Spec{.items = 6000, .seed = 62, .max_run = 8, .none_percent = 0, .anim_change_percent = 0, .portion_numerator = 12000000,
                                .colour = true, .unique_meshes = true, .repeat_far = true, .texture_percent = 0, .faction_percent = 0}}) {
    const auto world = BuildWorld(spec);
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
      for (const uint32_t k : {2u, 4u, 16u}) {
        const Outcome staged = RunStaged(world.get(), stage, k, true);
        CHECK(Equal(serial, staged, "interleaved meshes"));
        CHECK(staged.portions == 2 && staged.conflicts == 2);
        ++cases;
      }
    }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
    // The owner table off and another chunk's write racing in before the snapshots of chunks 1 to 3: wrong.
    di::controls.race_chunks = 0xe;
    CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 4, true), "owner table on, race hook on"));
    di::controls.owner_check = false;
    const Outcome raced = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
    di::controls.owner_check = true;
    di::controls.race_chunks = 0;
    CHECK(!Equal(serial, raced, "control: owner table off", false));
    CHECK(raced.conflicts == 0);
#endif
  }
  std::printf("colour: meshes recurring in several chunks: %u cases run directly (both portions of each pass), logs equal; owner table off + a racing write differs\n", cases);
}

// A mesh class the census did not cover: its chunk is run again.
static void ColourUnknownClass() {
  const auto world = BuildWorld({.items = 6000, .seed = 64, .max_run = 8, .colour = true, .unique_meshes = true, .foreign_percent = 10});
  const Outcome serial = RunSerial(world.get());
  uint32_t cases = 0;
  for (const dr::Stage stage : {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
    for (const uint32_t k : {2u, 4u, 8u, 16u}) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "unknown class"));
      CHECK(!CompareDiffers(staged));
      if (stage != dr::Stage::CHUNKED) CHECK(staged.reruns > 0);
      ++cases;
    }
  }
  std::printf("colour: meshes of an unknown class (10%% of the meshes): the chunks that hold one run again, logs equal (%u cases, the compare says not comparable)\n", cases);
}

// The tracker's first mesh is the last call's, with another view's state, and returns later in the first chunk.
static void ColourContinuation() {
  const auto world = BuildWorld({.items = 6000, .seed = 63, .max_run = 10, .max_count = 6, .continuation = true, .colour = true, .unique_meshes = true});
  const Outcome serial = RunSerial(world.get());
  uint32_t cases = 0;
  for (const dr::Stage stage : {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
    for (uint32_t k = 2; k <= 16; ++k) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "colour continuation"));
      CHECK(staged.first_reason == "continuation");
      ++cases;
    }
  }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  // With the guard off the variation path's flush (it writes the mesh's +0x40, not run while recording) would be missing before
  // the shared-state call that reads it: the replay's snapshot is the foreign +0x40.
  // (Not traced: the compare run's setup of the returning mesh would write the flush's value into it by chance.)
  di::controls.guard_continuation = false;
  const Outcome unguarded = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  di::controls.guard_continuation = true;
  CHECK(!Equal(serial, unguarded, "control: colour continuation guard off", false));
#endif
  std::printf("colour: a continuation of foreign mesh state runs directly (%u cases equal)\n", cases);
}

// The replay's own constants, textures and faction block against the game's (written separately from the listing).
static void ColourReplayUnits() {
  const auto world = BuildWorld({.items = 10, .seed = 70, .colour = true});
  const uint32_t block = reinterpret_cast<uint32_t>(&const1_blocks[0]);
  uint32_t cases = 0;
  const auto run = [&](auto&& game, auto&& replay, auto&& setup) {
    setup();
    game_log.clear();
    game();
    const std::vector<LogEntry> expected = game_log;
    const uint32_t expected_cell = std::bit_cast<uint32_t>(const1_cells[0]);
    setup();
    game_log.clear();
    replay();
    CHECK(game_log == expected);
    CHECK(std::bit_cast<uint32_t>(const1_cells[0]) == expected_cell);
    ++cases;
  };
  for (const uint8_t flag : {uint8_t{0}, uint8_t{1}}) {
    for (const uint32_t cell_bits : {0x3f800000u, 0x00000000u, 0x80000000u, 0x7fc00000u}) {
      for (const uint32_t value_bits : {0x00000000u, 0x80000000u, 0x7fc00000u, 0x3f800000u}) {
        for (const uint32_t repeat : {1u, 3u}) {
          const auto setup = [&] {
            cached_constants = flag;
            const1_cells[0] = std::bit_cast<float>(cell_bits);
          };
          run([&] { for (uint32_t i = 0; i < repeat; ++i) GameConst(block, value_bits); }, [&] { di::ReplayConst(block, value_bits, repeat); }, setup);
        }
      }
    }
  }
  cached_constants = 0;
  for (const uint32_t texture : {0u, 5u}) {
    run([&] { GameSrv(0x1234, texture); }, [&] { di::ReplaySrv(0x1234, texture); }, [] {});
  }
  for (const int32_t faction : {-1, 0, 1, 2, 3}) {
    for (const SynthMask* mask : std::initializer_list<const SynthMask*>{nullptr, &world->masks[0], &world->masks[1], &world->masks[2]}) {
      run([&] { GameFaction(faction, mask, &synth_node, 0x5678); },
          [&] { di::ReplayFaction(faction, reinterpret_cast<uint32_t>(mask), reinterpret_cast<uint32_t>(&synth_node), 0x5678); }, [] {});
    }
  }
  std::printf("colour: the replay of constants (flag 0 / 1, cell 1 / 0 / -0 / NaN, value 0 / -0 / NaN / 1, repeat 1 / 3), textures and the faction block (-1 and the lists of 6 / 3 / 70 / 4 colours, with and without a mask) equals the game's: %u cases\n", cases);
}

// The colour sites in Record, called directly.
static void ColourRecordRules() {
  static dr::Slot unit;
  static std::array<uint32_t, 128> buffer;
  static SynthMesh mesh;
  static SynthMesh foreign;
  mesh = {};
  mesh.vtable = mesh_vtable.data();
  mesh.view_tag = 0x40;
  mesh.pass = 0x44;
  foreign = {};
  foreign.vtable = foreign_vtable.data();
  const auto reset = [&](uint32_t mode, uint32_t chunk) {
    unit = {};
    unit.group = static_cast<uint32_t>(dr::Group::COLOUR_A);
    unit.mode = mode;
    unit.chunk = chunk;
    unit.ops_begin = unit.ops_cursor = buffer.data();
    unit.ops_end = buffer.data() + buffer.size();
    buffer.fill(0);
  };
  const auto setup = [&](SynthMesh* target) {
    StackImage image = MakeImage(dr::Site::COLOUR_SETUP, 0);
    image.frame.ebx = reinterpret_cast<uint32_t>(target);
    dr::Record(&unit, &image.frame);
    return image.frame.eax;
  };
  const auto constant = [&](dr::Site site, uint32_t ecx, uint32_t state, uint32_t bits) {
    StackImage image = MakeImage(site, ecx);
    image.frame.xmm0 = bits;
    image.stack[5] = state;
    dr::Record(&unit, &image.frame);
  };
  // The setup: a known class registers its chunk (another chunk's setup of the same mesh is a conflict), an unknown one is
  // skipped and flagged, in both modes; the compare slot and the suppressed warm-up register nothing.
  di::NextOwnerCall();
  reset(dr::MODE_RECORD, 1);
  CHECK(setup(&mesh) == 0 && unit.flags == 0 && unit.ops_cursor == unit.ops_begin);
  CHECK(setup(&mesh) == 0 && unit.flags == 0);
  reset(dr::MODE_RECORD, 2);
  CHECK(setup(&mesh) == 0 && (unit.flags & dr::SLOT_CONFLICT) != 0);
  reset(dr::MODE_RECORD, dr::COMPARE_SLOT);
  CHECK(setup(&mesh) == 0 && unit.flags == 0);
  reset(dr::MODE_SUPPRESS, 3);
  CHECK(setup(&mesh) == 0 && unit.flags == 0);
  reset(dr::MODE_RECORD, 1);
  CHECK(setup(&foreign) == 1 && (unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0);
  reset(dr::MODE_SUPPRESS, 1);
  CHECK(setup(&foreign) == 1 && (unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0);
  di::NextOwnerCall();
  reset(dr::MODE_RECORD, 2);
  CHECK(setup(&mesh) == 0 && unit.flags == 0);
  // The depth group has no setup site.
  reset(dr::MODE_RECORD, 1);
  unit.group = static_cast<uint32_t>(dr::Group::DEPTH_A);
  setup(&mesh);
  CHECK((unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0);

  // Constants: equal ones in a row are one op with a repeat count; another value, object or op in between starts a new one.
  reset(dr::MODE_RECORD, 1);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x3f800000);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x3f800000);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x3f800000);
  CHECK(unit.ops_cursor == unit.ops_begin + 1 + di::CONST_PAYLOAD);
  CHECK((buffer[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::CONSTANT) && buffer[1] == 0x2000 + di::CONST2_BLOCK && buffer[2] == 0x3f800000 && buffer[3] == 3);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x7fc00000);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x7fc00000);
  constant(dr::Site::COLOUR_CONST2, 0, 0x3000, 0x7fc00000);
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  CHECK(unit.ops_cursor == unit.ops_begin + 4 * (1 + di::CONST_PAYLOAD));
  const uint32_t* second = buffer.data() + (1 + di::CONST_PAYLOAD);
  CHECK(second[3] == 2 && second[1] == 0x2000 + di::CONST2_BLOCK && second[2] == 0x7fc00000);
  const uint32_t* fourth = buffer.data() + 3 * (1 + di::CONST_PAYLOAD);
  CHECK(fourth[1] == 0x4000 && fourth[2] == 0 && fourth[3] == 2);
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  CHECK(fourth[3] == 3 && unit.ops_cursor == unit.ops_begin + 4 * (1 + di::CONST_PAYLOAD));
  // An op between (a texture) ends the run.
  {
    StackImage srv = MakeImage(dr::Site::COLOUR_SRV_DIFFUSE, 0x77);
    srv.frame.ebx = 0x9000;
    dr::Record(&unit, &srv.frame);
    StackImage normal = MakeImage(dr::Site::COLOUR_SRV_NORMAL, 0x78);
    normal.frame.ebx = 0x9000;
    dr::Record(&unit, &normal.frame);
  }
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  const uint32_t* diffuse_op = buffer.data() + 4 * (1 + di::CONST_PAYLOAD);
  CHECK((diffuse_op[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::SRV) && diffuse_op[1] == 0x9000 + 0x41f4 && diffuse_op[2] == 0x77);
  const uint32_t* normal_op = diffuse_op + (1 + di::SRV_PAYLOAD);
  CHECK(normal_op[1] == 0x9000 + 0x421c && normal_op[2] == 0x78);
  const uint32_t* after = normal_op + (1 + di::SRV_PAYLOAD);
  CHECK((after[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::CONSTANT) && after[3] == 1 && fourth[3] == 3);
  // The suppressed warm-up records nothing for constants and textures.
  reset(dr::MODE_SUPPRESS, 1);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x3f800000);
  CHECK(unit.ops_cursor == unit.ops_begin);

  // The faction block: the mask's index goes into the tracker's +0x0c in both modes; the op carries faction, mask, node, state.
  static SynthItem item;
  static SynthMask mask;
  mask = {};
  mask.index = 7;
  for (const uint32_t mode : {dr::MODE_RECORD, dr::MODE_SUPPRESS}) {
    for (const bool masked : {true, false}) {
      reset(mode, 1);
      item = {};
      item.mask = (masked ? &mask : nullptr);
      StackImage image = MakeImage(dr::Site::COLOUR_FACTION, 3);
      image.frame.edi = reinterpret_cast<uint32_t>(&item);
      image.stack[0x28 / 4] = 0xaaaa;
      image.stack[0x14 / 4] = 0xbbbb;
      dr::Record(&unit, &image.frame);
      int32_t stored = 0;
      std::memcpy(&stored, unit.tracker.data() + 0x0c, 4);
      CHECK(stored == (masked ? 7 : -1));
      if (mode == dr::MODE_RECORD) {
        CHECK((buffer[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::FACTION) && buffer[1] == 3 && buffer[2] == reinterpret_cast<uint32_t>(item.mask)
              && buffer[3] == 0xaaaa && buffer[4] == 0xbbbb && unit.ops_cursor == unit.ops_begin + 1 + di::FACTION_PAYLOAD);
      } else {
        CHECK(unit.ops_cursor == unit.ops_begin);
      }
    }
  }
  // A full buffer flags the chunk instead of writing past it.
  reset(dr::MODE_RECORD, 1);
  unit.ops_end = unit.ops_begin + 2;
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  CHECK((unit.flags & dr::SLOT_OVERFLOW) != 0 && unit.ops_cursor == unit.ops_begin);
  std::printf("record (colour): setup registers / conflicts / skips an unknown class, constants merge into one op with a repeat count, textures, faction block (index stored in both modes), full buffer\n");
}

// The compare's rules for ops (S1): flush snapshots are not compared, the any byte is compared with what earlier chunks left.
static void ColourOpsEqual() {
  std::array<uint32_t, 1 + di::FLUSH_PAYLOAD> whole = {};
  std::array<uint32_t, 1 + di::FLUSH_PAYLOAD> chunk = {};
  whole[0] = chunk[0] = static_cast<uint32_t>(di::OpKind::FLUSH) | ((1 + di::FLUSH_PAYLOAD) << 16);
  for (uint32_t i = 1; i <= 10; ++i) whole[i] = chunk[i] = 0x100 + i;
  whole[1 + 13] = 1;
  chunk[1 + 13] = 0;
  CHECK(!di::OpsEqual(chunk.data(), whole.data(), 0));
  CHECK(di::OpsEqual(chunk.data(), whole.data(), 1));
  whole[1 + 11] = 0x55;
  whole[1 + 12] = 0x66;
  CHECK(di::OpsEqual(chunk.data(), whole.data(), 1));
  chunk[1 + 5] ^= 4;
  CHECK(!di::OpsEqual(chunk.data(), whole.data(), 1));
  std::array<uint32_t, 1 + di::VF68_PAYLOAD> vf_whole = {};
  std::array<uint32_t, 1 + di::VF68_PAYLOAD> vf_chunk = {};
  vf_whole[0] = vf_chunk[0] = static_cast<uint32_t>(di::OpKind::VF68) | ((1 + di::VF68_PAYLOAD) << 16);
  CHECK(di::OpsEqual(vf_chunk.data(), vf_whole.data(), 0));
  vf_whole[1 + 4] = 9;
  CHECK(!di::OpsEqual(vf_chunk.data(), vf_whole.data(), 0));
  std::printf("colour: compare rules: flush snapshots ignored, the any byte joined with the earlier chunks', vf68 snapshots compared\n");
}

// RECORD_MESHES (the B groups' mesh rounds) flags every chunk its worker owns, also one worker alone.
static void RecordWorkMeshes() {
  di::Descriptor& d = di::descriptor;
  d.kind = di::WorkKind::RECORD_MESHES;
  d.chunks = 5;
  const auto flagged = [] {
    std::array<bool, 6> result = {};
    for (uint32_t k = 0; k < result.size(); ++k) {
      result[k] = (di::SlotAt(k)->flags & dr::SLOT_UNKNOWN_CLASS) != 0;
      di::SlotAt(k)->flags = 0;
    }
    return result;
  };
  flagged();
  d.one_worker = true;
  di::RecordWork(d, 0);
  CHECK((flagged() == std::array<bool, 6>{false, true, true, true, true, false}));
  d.one_worker = false;
  di::RecordWork(d, 2);
  CHECK((flagged() == std::array<bool, 6>{false, false, false, true, false, false}));
  d.kind = di::WorkKind::RECORD_LOOP;
  d.one_worker = false;
  std::printf("record work: RECORD_MESHES flags every chunk of its worker (one worker: chunks 1 to n - 1)\n");
}

// Each colour control makes one thing wrong; its run must differ.
static void ColourControls() {
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  const auto world = BuildWorld({.items = 6000, .seed = 71, .max_run = 8, .colour = true, .unique_meshes = true, .variation_percent = 60});
  const Outcome serial = RunSerial(world.get());
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 8), "colour control baseline"));
  // The snapshots the ops carry (the shared-state call reads the mesh's +0x44, the flush logs it).
  di::controls.restore_snapshots = false;
  const Outcome unrestored = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.restore_snapshots = true;
  CHECK(!Equal(serial, unrestored, "control: snapshot restore off", false));
  // Seeds: the fields a chunk starts with are the warm-up's, validated at replay.
  di::controls.validate_seeds = false;
  const Outcome unvalidated = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.validate_seeds = true;
  CHECK(!Equal(serial, unvalidated, "control: colour seed validation off", false));
  // The "any" byte the loop sets itself travels in the flush op.
  di::controls.capture_any = false;
  const Outcome uncaptured = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.capture_any = true;
  CHECK(!Equal(serial, uncaptured, "control: colour any capture off", false));
  test_controls.drop_end_flush = true;
  const Outcome dropped = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  test_controls.drop_end_flush = false;
  CHECK(!Equal(serial, dropped, "control: colour dropped flush", false));
  test_controls.anim_stack = 8;
  const Outcome wrong_anim = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  test_controls.anim_stack = 9;
  CHECK(!Equal(serial, wrong_anim, "control: colour wrong anim stack slot", false));
  test_controls.inject_chunk = 2;
  const Outcome injected = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
  test_controls.inject_chunk = -1;
  CHECK(!Equal(serial, injected, "control: colour injected op", false));
  CHECK(CompareDiffers(injected));
  // A chunk that ends with the "any" byte set and nothing to flush: the compare must join it to the next chunk's first flush.
  const auto merge = BuildWorld({.items = 6000, .seed = 60, .max_run = 6, .anim_change_percent = 0, .colour = true, .unique_meshes = true, .merge = true});
  const Outcome merge_serial = RunSerial(merge.get());
  const Outcome joined = RunStaged(merge.get(), dr::Stage::RECORDED, 8, true);
  CHECK(Equal(merge_serial, joined, "pending any joined") && !CompareDiffers(joined));
  di::controls.pending_any_compare = false;
  const Outcome unjoined = RunStaged(merge.get(), dr::Stage::RECORDED, 8, true);
  di::controls.pending_any_compare = true;
  CHECK(Equal(merge_serial, unjoined, "pending any not joined: the run itself is exact"));
  CHECK(unjoined.reruns == 0 && joined.reruns == 0);
  CHECK(CompareDiffers(unjoined));
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::WORKERS, 8), "colour controls cleared"));
  std::printf("colour controls: snapshot restore off, seed validation off, any capture off, dropped flush, wrong anim slot, injected op: each run differs (injection named by the compare); the compare without the earlier chunks' any byte reports a difference on a list whose chunks end with it set (the run itself is exact); owner table off and the continuation guard off are in their own tests\n");
#else
  std::printf("colour controls: skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

static void Detach() {
  const auto world = BuildWorld({.items = 6000, .seed = 31, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  // The pool starts from nothing (earlier tests grew it to 15).
  dr::Update({.stage = dr::Stage::OFF}, false);
  dr::Update({.stage = dr::Stage::WORKERS, .threads = 8, .loops = 1}, true);
  // Sites still in: everything stays, the dispatcher runs directly.
  dr::Uninstall(true);
  CHECK(di::detached.load() && di::shared != nullptr && di::worker_count == 7);
  const Outcome detached = RunStaged(world.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(serial, detached, "detached"));
  CHECK(detached.direct_calls == detached.portions);
  // A dispatch still counted active: kept too.
  g_shared->active = 1;
  dr::Uninstall(false);
  CHECK(di::shared != nullptr && di::worker_count == 7);
  g_shared->active = 0;
  dr::Uninstall(false);
  CHECK(di::shared == nullptr && di::worker_count == 0 && di::arena == nullptr && di::module_reference == nullptr);
  std::printf("detach: kept while sites are in or a dispatch is active; clean case frees the pool and the memory\n");
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  PoolStartStop();
  PoolLiveK();
  PoolControlWords();
  PoolCpuSets();
  PoolThreadFailure();
  Setup();
  RecordRules();
  DepthStages();
  DepthFlushEntry();
  DepthTouchAhead();
  DepthOverflow();
  DepthLarge();
  DepthEntryState();
  DepthContinuation();
  DepthFallbacks();
  DepthTrace();
  DepthControls();
  ColourRecordRules();
  ColourOpsEqual();
  RecordWorkMeshes();
  ColourReplayUnits();
  ColourStages();
  ColourInterleaved();
  ColourUnknownClass();
  ColourContinuation();
  ColourControls();
  Detach();
  if (failures != 0) {
    std::printf("%d FAILED\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
