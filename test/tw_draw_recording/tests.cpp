// Threaded Draw Recording (twattila/draw_recording.hpp), x86 only.
// Part 1 (C.2-1): the pool: start / stop 100 times, K 2 to 16 to 2 live, the x87 and SSE control words of the dispatching
// thread seen by every worker, the CPU-set mirror of Keep Render Core Free's published sets, the types' offsets (static
// asserts in the header).
// Part 2 (C.2-2): the depth path on a synthetic loop with the same state machine as the game's (written from its listing):
// the serial call log equals the log of the Chunked, Recorded, One Worker and Workers stages at K = 2 to 16 over lists with
// runs of 1 to 300 items, animation -1 runs, meshes without a technique before cuts, tiny op buffers (overflow: run again),
// two calls in a row on one tracker; Touch-Ahead; the fall-backs; the trace's in-frame compare. Controls: each safeguard
// off (seed validation, pointer translation), a dropped flush, a wrong animation stack slot, an op injected into one chunk:
// every one must make the run differ.
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <iterator>
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
  std::printf("pool: x87 PC / RC and MXCSR RC / DAZ / FTZ copied to all 15 workers (%d combinations)\n", checked);
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
    ++cs::published.generation;
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
  publish({});
  dr::Update(Workers(12), true);
  CHECK(all_equal({}));
  dr::Update({.stage = dr::Stage::OFF}, false);
  std::printf("pool: CPU-set mirror: the published sets are the pool threads' sets (set, grown, cleared)\n");
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
  uint32_t svt;
  uint32_t pass;
  uint8_t pad1[0x14];
  uint8_t flag;
  uint8_t pad2[0x23];
};
static_assert(sizeof(SynthMesh) == 0x80 && offsetof(SynthMesh, svt) == 0x40 && offsetof(SynthMesh, pass) == 0x44 && offsetof(SynthMesh, flag) == 0x5c);

struct SynthItem {
  SynthMesh* mesh;
  uint8_t pad0[0x5c - 4];
  int32_t anim;
  uint8_t pad1[0x100 - 0x60];
};
static_assert(sizeof(SynthItem) == 0x100 && offsetof(SynthItem, anim) == 0x5c);

struct SynthList {
  uint32_t pad;
  uint32_t count;
  SynthItem** data;
};
static_assert(offsetof(SynthList, count) == 4 && offsetof(SynthList, data) == 8);

using LogEntry = std::array<uint32_t, 14>;
static std::vector<LogEntry> game_log;
static dr::Shared* g_shared = nullptr;

// What the test changes in the thunk rule (the controls).
struct TestControls {
  bool drop_end_flush = false;
  uint32_t anim_stack = 9;
  int inject_chunk = -1;
};
static TestControls test_controls;

static void __thiscall FakeFlush(void* mesh, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9) {
  const auto* m = static_cast<const SynthMesh*>(mesh);
  game_log.push_back({1, reinterpret_cast<uint32_t>(mesh), a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, m->svt, m->pass});
}
static uint8_t __thiscall FakeVf68(void* mesh, uint32_t view, uint32_t zero) {
  const auto* m = static_cast<const SynthMesh*>(mesh);
  game_log.push_back({2, reinterpret_cast<uint32_t>(mesh), view, zero, 0, 0, 0, 0, 0, 0, 0, 0, m->svt, m->pass});
  return static_cast<uint8_t>(m->kind & 1);
}
static void __thiscall FakeSetAnimation(void* state, uint32_t value) {
  game_log.push_back({3, reinterpret_cast<uint32_t>(state), value, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}

static std::array<uint32_t, 0x70 / 4> mesh_vtable = {};

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
  if (test_controls.drop_end_flush && site == dr::Site::DEPTH_FLUSH_END && slot->mode == dr::MODE_RECORD) return;
  StackImage image = MakeImage(site, mesh);
  for (uint32_t i = 0; i < 10; ++i) image.stack[1 + i] = args[i];
  dr::Record(slot, &image.frame);
  if (test_controls.inject_chunk == static_cast<int>(slot->chunk) && slot->mode == dr::MODE_RECORD && site == dr::Site::DEPTH_FLUSH_BOUNDARY) {
    test_controls.inject_chunk = -1;
    dr::Record(slot, &image.frame);
  }
}

static uint8_t Vf68Site(void* t, uint32_t mesh, uint32_t view) {
  if (!InArena(t)) {
    const auto function = reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(static_cast<uintptr_t>((*reinterpret_cast<uint32_t**>(mesh))[0x68 / 4]));
    return function(reinterpret_cast<void*>(mesh), view, 0);
  }
  StackImage image = MakeImage(dr::Site::DEPTH_VF68, mesh);
  image.stack[1] = view;
  image.stack[2] = 0;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
  return static_cast<uint8_t>(image.frame.eax);
}

static void AnimSite(void* t, uint32_t state, uint32_t value, uint32_t index) {
  if (!InArena(t)) {
    g_shared->last_anim = static_cast<int32_t>(index);
    FakeSetAnimation(reinterpret_cast<void*>(state), value);
    return;
  }
  StackImage image = MakeImage(dr::Site::DEPTH_ANIM, state);
  image.stack[1] = value;
  image.stack[test_controls.anim_stack] = index;
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
      mesh->svt = Field<uint32_t>(t, 0x20);
      Field<uint8_t>(t, 0x68) = mesh->flag;
      const uint32_t type = mesh->kind ^ (mesh->svt & 0x10000);
      mesh->pass = Field<uint32_t>(t, 0x14);
      const uint32_t kind = type & 0xffff;
      const uint32_t technique = (kind == 0 ? 0 : (kind == 1 ? 0x101 : kind * 0x10));
      Field<uint32_t>(t, 0x74) = technique;
      if (technique != 0) {
        if ((technique & 1) == 0) {
          Field<uint32_t>(t, 0x50) = 4 + mesh->kind;
          Field<uint8_t>(t, 0x69) |= Vf68Site(t, mesh_address, Field<uint32_t>(t, 0x1c));
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
      AnimSite(t, state, static_cast<uint32_t>(anim) * 3 + 1, static_cast<uint32_t>(anim));
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

struct Spec {
  uint32_t items = 6000;
  uint32_t seed = 1;
  uint32_t max_run = 8;
  uint32_t none_percent = 10;
  uint32_t anim_minus_percent = 0;
  uint32_t anim_change_percent = 20;
  uint32_t max_count = 1000;
  bool pattern = false;
};

struct World {
  std::vector<SynthMesh> meshes;
  std::vector<SynthItem> items;
  std::vector<SynthItem*> pointers;
  SynthList list = {};
  alignas(16) std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  uint32_t max_count = 1000;
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
  world->meshes.resize(60);
  for (uint32_t m = 0; m < world->meshes.size(); ++m) {
    SynthMesh& mesh = world->meshes[m];
    mesh.vtable = mesh_vtable.data();
    mesh.flag = static_cast<uint8_t>(m & 1);
    if (spec.pattern) {
      mesh.kind = (m < 6 ? 2 + m % 4 : 0);
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
    if (spec.pattern) {
      // Five items of a valid mesh, three of one without a technique.
      const bool none = (pattern_run % 2 == 1);
      mesh_index = (none ? 6 + (pattern_run / 2) % 4 : (pattern_run / 2) % 6);
      run = (none ? 3 : 5);
      ++pattern_run;
    } else {
      mesh_index = random(static_cast<uint32_t>(world->meshes.size()));
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
  world->pointers.resize(spec.items);
  for (uint32_t k = 0; k < spec.items; ++k) world->pointers[k] = &world->items[k];
  world->list = {.count = spec.items, .data = world->pointers.data()};
  return world;
}

struct Outcome {
  std::vector<LogEntry> log;
  std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  std::vector<std::array<uint32_t, 2>> mesh_fields;
  uint32_t chunks = 0;
  uint32_t reruns = 0;
  uint32_t replayed = 0;
  uint32_t ops = 0;
  uint32_t direct_calls = 0;
  std::string compare;
};

static void ResetWorld(World* world) {
  for (SynthMesh& mesh : world->meshes) {
    mesh.svt = 0xffffffff;
    mesh.pass = 0xffffffff;
  }
  world->tracker = {};
  uint8_t* t = world->tracker.data();
  Field<uint32_t>(t, 0x18) = 0xd0;
  Field<uint32_t>(t, 0x1c) = 0xe0;
  Field<uint32_t>(t, 0x20) = 0x12345;
  Field<uint32_t>(t, 0x24) = 0xb0;
  Field<uint32_t>(t, 0x64) = world->max_count;
  g_shared->seed_anim = -1;
  g_shared->last_anim = -1;
  game_log.clear();
}

static Outcome Capture(World* world) {
  Outcome outcome = {.log = game_log, .tracker = world->tracker};
  // +0x50 is only written, never read (the stride): not copied back, so not compared.
  std::fill_n(outcome.tracker.begin() + 0x50, 4, uint8_t{0});
  for (const SynthMesh& mesh : world->meshes) outcome.mesh_fields.push_back({mesh.svt, mesh.pass});
  return outcome;
}

static void* const NODE = reinterpret_cast<void*>(0x1000);

static Outcome RunSerial(World* world) {
  ResetWorld(world);
  for (int call = 0; call < 2; ++call) {
    Field<uint32_t>(world->tracker.data(), 0x58) = 0;
    SynthDepthLoop(NODE, world->tracker.data(), &world->list, 0, world->list.count);
  }
  return Capture(world);
}

static Outcome RunStaged(World* world, dr::Stage stage, uint32_t threads, bool tracing = false) {
  ResetWorld(world);
  dr::Update({.stage = stage, .threads = threads, .loops = 1}, true);
  di::trace.active = tracing;
  di::trace.frames_left = 1u << 30;
  di::trace.calls.clear();
  Outcome total = {};
  for (int call = 0; call < 2; ++call) {
    Field<uint32_t>(world->tracker.data(), 0x58) = 0;
    dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, 0, world->list.count);
    total.chunks += di::last_call.chunks;
    total.reruns += di::last_call.reruns;
    total.replayed += di::last_call.replayed;
    total.ops += di::last_call.ops;
    total.direct_calls += (di::last_call.direct ? 1 : 0);
    if (tracing && !di::trace.calls.empty() && !di::trace.calls.back().compare_result.empty()) {
      total.compare += di::trace.calls.back().compare_result + ";";
    }
  }
  di::trace.active = false;
  Outcome outcome = Capture(world);
  outcome.chunks = total.chunks;
  outcome.reruns = total.reruns;
  outcome.replayed = total.replayed;
  outcome.ops = total.ops;
  outcome.direct_calls = total.direct_calls;
  outcome.compare = total.compare;
  return outcome;
}

static bool Equal(const Outcome& serial, const Outcome& staged, const char* label, bool print = true) {
  bool equal = true;
  if (serial.log != staged.log) {
    equal = false;
    if (print) {
      size_t at = 0;
      while (at < std::min(serial.log.size(), staged.log.size()) && serial.log[at] == staged.log[at]) ++at;
      std::printf("  %s: logs differ at entry %zu of %zu / %zu\n", label, at, serial.log.size(), staged.log.size());
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
  return lists;
}

static void Setup() {
  mesh_vtable.fill(0);
  mesh_vtable[0x68 / 4] = reinterpret_cast<uint32_t>(&FakeVf68);
  g_shared = dr::SharedPage();
  REQUIRE(g_shared != nullptr);
  REQUIRE(g_shared->record == reinterpret_cast<uint32_t>(&dr::Record));
  REQUIRE(g_shared->dispatch_depth == reinterpret_cast<uint32_t>(&dr::DispatchDepth));
  dr::SetGame({.depth_loop = reinterpret_cast<uint32_t>(&SynthDepthLoop),
               .flush = reinterpret_cast<uint32_t>(&FakeFlush),
               .set_animation = reinterpret_cast<uint32_t>(&FakeSetAnimation),
               .usable = true});
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
          CHECK(staged.direct_calls == 0);
          if (list.expect_chunks && k >= 4) CHECK(staged.chunks >= 2 * 2);
          if (stage != dr::Stage::CHUNKED) CHECK(staged.replayed + staged.reruns == staged.chunks);
          if (stage != dr::Stage::CHUNKED && staged.chunks > 0) CHECK(staged.ops > 0 || staged.reruns > 0);
          CHECK(staged.compare.find("differs") == std::string::npos);
          reruns_seen += staged.reruns;
          ++cases;
        }
      }
    }
  }
  CHECK(reruns_seen > 0);
  std::printf("depth: %u cases (6 lists x 4 stages x K 2..16 x trace off / on): logs, tracker and mesh fields equal the serial run; %u chunks run again\n", cases, reruns_seen);
}

static void DepthTouchAhead() {
  const auto world = BuildWorld({.items = 6000, .seed = 11, .max_run = 20});
  const Outcome serial = RunSerial(world.get());
  for (uint32_t k = 2; k <= 16; ++k) {
    const uint32_t before = di::touch_generation.load();
    const Outcome staged = RunStaged(world.get(), dr::Stage::TOUCH_AHEAD, k);
    CHECK(Equal(serial, staged, "touch-ahead"));
    CHECK(staged.direct_calls == 0);
    CHECK(di::descriptor.workers == k - 1);
    CHECK(di::touch_generation.load() == before + 2);
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
        CHECK(staged.compare.find("differs") == std::string::npos);
      }
    }
  }
  di::op_buffer_limit = di::OP_BUFFER_BYTES;
  std::printf("depth: tiny op buffers: overflowing chunks run again, logs equal (%u reruns)\n", reruns);
}

static void DepthFallbacks() {
  const auto world = BuildWorld({.items = 6000, .seed = 13, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  // Fewer than 512 items.
  const auto few_items = BuildWorld({.items = 300, .seed = 14, .max_run = 10});
  const Outcome few_serial = RunSerial(few_items.get());
  const Outcome few_staged = RunStaged(few_items.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(few_serial, few_staged, "small call"));
  CHECK(few_staged.direct_calls == 2);
  // The stage Direct and the row off.
  for (const dr::Stage stage : {dr::Stage::DIRECT, dr::Stage::OFF}) {
    const Outcome staged = RunStaged(world.get(), stage, 8);
    CHECK(Equal(serial, staged, "stage direct"));
    CHECK(staged.direct_calls == 2);
  }
  // The pool smaller than the stage needs.
  dr::Update({.stage = dr::Stage::OFF}, false);
  RunStaged(world.get(), dr::Stage::WORKERS, 4);
  CHECK(di::worker_count == 3);
  di::config.threads = 16;
  ResetWorld(world.get());
  for (int call = 0; call < 2; ++call) {
    Field<uint32_t>(world->tracker.data(), 0x58) = 0;
    dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, 0, world->list.count);
    CHECK(di::last_call.direct);
  }
  CHECK(Equal(serial, Capture(world.get()), "pool missing"));
  // Another thread.
  dr::Update({.stage = dr::Stage::WORKERS, .threads = 8, .loops = 1}, true);
  ResetWorld(world.get());
  bool direct = false;
  std::thread other([&] {
    for (int call = 0; call < 2; ++call) {
      Field<uint32_t>(world->tracker.data(), 0x58) = 0;
      dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, 0, world->list.count);
      direct = di::last_call.direct;
    }
  });
  other.join();
  CHECK(direct);
  CHECK(Equal(serial, Capture(world.get()), "other thread"));
  // Not usable.
  dr::SetGame({.depth_loop = reinterpret_cast<uint32_t>(&SynthDepthLoop), .flush = reinterpret_cast<uint32_t>(&FakeFlush),
               .set_animation = reinterpret_cast<uint32_t>(&FakeSetAnimation), .usable = false});
  const Outcome unusable = RunStaged(world.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(serial, unusable, "not usable"));
  CHECK(unusable.direct_calls == 2);
  dr::SetGame({.depth_loop = reinterpret_cast<uint32_t>(&SynthDepthLoop), .flush = reinterpret_cast<uint32_t>(&FakeFlush),
               .set_animation = reinterpret_cast<uint32_t>(&FakeSetAnimation), .usable = true});
  std::printf("depth: fall-backs (small call, Direct, Off, pool missing, other thread, not usable) run the loop directly, logs equal\n");
}

static void DepthTrace() {
  const auto world = BuildWorld({.items = 6000, .seed = 15, .max_run = 12});
  const Outcome serial = RunSerial(world.get());
  // The trace armed through the environment: written next to the exe after its frames.
  const std::filesystem::path file = std::filesystem::path(__argv[0]).parent_path() / "tw_draw_recording.txt";
  std::filesystem::remove(file);
  SetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", "0,2");
  di::trace.read_environment = false;
  di::trace.armed = false;
  di::trace.finished = false;
  ResetWorld(world.get());
  const dr::Settings settings = {.stage = dr::Stage::WORKERS, .threads = 6, .loops = 1};
  dr::Update(settings, true);
  CHECK(di::trace.armed);
  dr::Update(settings, true);
  CHECK(di::trace.active);
  for (int frame = 0; frame < 2; ++frame) {
    Field<uint32_t>(world->tracker.data(), 0x58) = 0;
    dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, 0, world->list.count);
    dr::Update(settings, true);
  }
  CHECK(!di::trace.active && di::trace.finished);
  std::ifstream stream(file, std::ios::binary);
  const std::string text_value((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  CHECK(text_value.find("compare: equal") != std::string::npos);
  CHECK(text_value.find("frame:") != std::string::npos);
  CHECK(text_value.find("differs") == std::string::npos);
  SetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", nullptr);
  stream.close();
  std::filesystem::remove(file);
  static_cast<void>(serial);
  std::printf("depth: trace armed from the environment wrote the file, compare equal\n");
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

  // An op in one chunk that the serial run does not have: the log differs and the compare names it.
  test_controls.inject_chunk = 2;
  const Outcome injected = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
  test_controls.inject_chunk = -1;
  CHECK(!Equal(serial, injected, "control: injected op", false));
  CHECK(injected.compare.find("differs at op") != std::string::npos);

  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::WORKERS, 8), "controls cleared"));
  std::printf("controls: validation off, dropped flush, translation off, wrong anim slot, injected op: each run differs (injection named by the compare)\n");
#else
  std::printf("controls: skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

static void Detach() {
  const auto world = BuildWorld({.items = 6000, .seed = 31, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  dr::Update({.stage = dr::Stage::WORKERS, .threads = 8, .loops = 1}, true);
  // Sites still in: everything stays, the dispatcher runs directly.
  dr::Uninstall(true);
  CHECK(di::detached && di::shared != nullptr && di::worker_count == 7);
  const Outcome detached = RunStaged(world.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(serial, detached, "detached"));
  CHECK(detached.direct_calls == 2);
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
  Setup();
  DepthStages();
  DepthTouchAhead();
  DepthOverflow();
  DepthFallbacks();
  DepthTrace();
  DepthControls();
  Detach();
  if (failures != 0) {
    std::printf("%d FAILED\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
