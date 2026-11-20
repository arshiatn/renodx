// Keep Render Core Free (twattila/cpu_sets.hpp). Part 1: PlanSets on synthetic CPUs (a 7950X3D with and without a CPU set
// rule of its first CCD, 8 cores without SMT, a hybrid CPU, two processor groups, fewer than 4 cores, affinity masks, CPUs
// given to other processes, ties): every set a subset of the base, the render core the most used, the stand-downs.
// Part 2, live, in this process only: its own process default CPU sets, the real worker on its own threads (a render thread
// presenting, busy workers, one with its own CPU sets, one with its own affinity, one started later, one whose sets
// someone changes, two not the game's: started at kernel32!Sleep and a Windows pool thread); a default change, a default of
// 3 cores, another presenting thread, the row off, a render thread with its own rule, an affinity-only base, no game
// modules and no start address query, every start unreadable (nothing to move), kernel32 as the game's module.
// Nothing outside this process is changed.
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
  std::printf("  log: %s\n", message);
}

#include "src/games/twattila/cpu_sets.hpp"

namespace cs = twattila::cpu_sets;

// The game's modules here: the test exe (its threads start in its own code: CreateThread / the static CRT's std::thread).
static constexpr std::array<const wchar_t*, 1> GAME_MODULES = {nullptr};

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

using Ids = std::vector<ULONG>;

// `cores` cores of `per_core` logical CPUs in each of `groups` groups; ids 0x100 + the global index (Windows' numbering);
// CoreIndex = the core's first logical CPU. `efficiency(core)` gives each core's class; `cores_per_cache` cores share an L3.
static std::vector<cs::CpuSet> Topology(int cores, int per_core, int groups = 1, const std::function<BYTE(int)>& efficiency = [](int) -> BYTE { return 0; },
                                        int cores_per_cache = 64) {
  std::vector<cs::CpuSet> system;
  for (int group = 0; group < groups; ++group) {
    for (int core = 0; core < cores; ++core) {
      for (int thread = 0; thread < per_core; ++thread) {
        const int logical = core * per_core + thread;
        system.push_back({.id = static_cast<ULONG>(0x100 + group * cores * per_core + logical), .group = static_cast<WORD>(group),
                          .logical = static_cast<BYTE>(logical), .core = static_cast<BYTE>(core * per_core), .efficiency = efficiency(core),
                          .cache = static_cast<BYTE>(core / cores_per_cache)});
      }
    }
  }
  return system;
}

static Ids IdsOf(const std::vector<cs::CpuSet>& system, int first_logical, int last_logical) {
  Ids ids;
  for (const cs::CpuSet& cpu : system) {
    if (cpu.group == 0 && cpu.logical >= first_logical && cpu.logical <= last_logical) ids.push_back(cpu.id);
  }
  std::ranges::sort(ids);
  return ids;
}

static bool Subset(const Ids& inner, const Ids& outer) {
  return std::ranges::all_of(inner, [&](ULONG id) { return std::ranges::find(outer, id) != outer.end(); });
}

// The base as PlanSets defines it, independently: available, in the default (empty: all), in the affinity.
static Ids BaseOf(const std::vector<cs::CpuSet>& system, const Ids& default_sets, uint64_t affinity) {
  Ids base;
  for (const cs::CpuSet& cpu : system) {
    const bool in_default = default_sets.empty() || std::ranges::find(default_sets, cpu.id) != default_sets.end();
    if (cpu.available && in_default && ((affinity >> cpu.logical) & 1) != 0) base.push_back(cpu.id);
  }
  std::ranges::sort(base);
  return base;
}

// An applied plan: both sets inside the base, disjoint, together the base; the render sets one core's, 2 or more.
static void CheckApplied(const cs::Plan& plan, const std::vector<cs::CpuSet>& system, const Ids& default_sets, uint64_t affinity,
                         int expected_core) {
  CHECK(plan.stand_down.empty());
  if (!plan.stand_down.empty()) std::printf("  stood down: %.*s\n", static_cast<int>(plan.stand_down.size()), plan.stand_down.data());
  const Ids base = BaseOf(system, default_sets, affinity);
  CHECK(Subset(plan.render_sets, base));
  CHECK(Subset(plan.worker_sets, base));
  CHECK(plan.render_sets.size() >= 2);
  CHECK(!plan.worker_sets.empty());
  CHECK(std::ranges::none_of(plan.render_sets, [&](ULONG id) { return std::ranges::find(plan.worker_sets, id) != plan.worker_sets.end(); }));
  CHECK(plan.render_sets.size() + plan.worker_sets.size() == base.size());
  CHECK(plan.render_core == expected_core);
  for (const cs::CpuSet& cpu : system) {
    if (std::ranges::find(plan.render_sets, cpu.id) != plan.render_sets.end()) CHECK(cpu.core == plan.render_core);
  }
}

static void CheckStandDown(const cs::Plan& plan, std::string_view reason_part) {
  CHECK(!plan.stand_down.empty());
  CHECK(plan.stand_down.find(reason_part) != std::string_view::npos);
  CHECK(plan.render_sets.empty() && plan.worker_sets.empty());
}

static void Synthetic() {
  std::printf("Part 1: synthetic CPUs\n");
  const uint64_t all = ~0ull;
  // A 7950X3D: two CCDs of 8 cores (two L3 caches); `one_l3`: 16 cores on one L3.
  const auto x3d = Topology(16, 2, 1, [](int) -> BYTE { return 0; }, 8);
  const auto one_l3 = Topology(16, 2);
  std::array<uint32_t, 64> runs = {};
  int cases = 0;
  // The render core when only `logical` was counted: its core, but core 0's goes to the next core (core 0 avoided).
  const auto expected = [](int logical) { return (logical < 2 ? 2 : (logical & ~1)); };

  // 7950X3D without a rule: both CCDs, stands down; one L3: most presents on logical 9 (core 8), some elsewhere.
  runs = {};
  runs[9] = 40;
  runs[3] = 10;
  runs[30] = 14;
  CheckStandDown(cs::PlanSets(x3d, {}, all, runs), "L3 cache");
  CheckApplied(cs::PlanSets(one_l3, {}, all, runs), one_l3, {}, all, 8);
  // A rule across both CCDs (4-19) stands down too.
  CheckStandDown(cs::PlanSets(x3d, IdsOf(x3d, 4, 19), all, runs), "L3 cache");
  cases += 3;
  // Core 0 avoided: the most used other core (logical 5: core 4), or the next one when only core 0 ran; core 0 when no
  // other core has a second logical CPU in the base (0, 1, 2, 4, 6: cores 2, 4, 6 with one each).
  runs = {};
  runs[0] = 50;
  runs[1] = 10;
  runs[5] = 3;
  CheckApplied(cs::PlanSets(one_l3, {}, all, runs), one_l3, {}, all, 4);
  runs = {};
  runs[0] = 64;
  CheckApplied(cs::PlanSets(one_l3, {}, all, runs), one_l3, {}, all, 2);
  {
    const Ids only_core0_smt = {one_l3[0].id, one_l3[1].id, one_l3[2].id, one_l3[4].id, one_l3[6].id};
    runs = {};
    runs[2] = 64;
    CheckApplied(cs::PlanSets(one_l3, only_core0_smt, all, runs), one_l3, only_core0_smt, all, 0);
  }
  cases += 3;
  // SMT siblings not next to each other (logical k and k + 16 on core k): logical 20 -> core 4, sets 4 and 20.
  {
    std::vector<cs::CpuSet> split;
    for (int logical = 0; logical < 32; ++logical) {
      split.push_back({.id = static_cast<ULONG>(0x100 + logical), .logical = static_cast<BYTE>(logical), .core = static_cast<BYTE>(logical % 16)});
    }
    runs = {};
    runs[20] = 64;
    const cs::Plan plan = cs::PlanSets(split, {}, all, runs);
    CheckApplied(plan, split, {}, all, 4);
    CHECK((plan.render_sets == Ids{0x104, 0x114}));
    ++cases;
  }
  // The same with the rule 0-15 (a CPU set, the V-cache CCD); presents outside it don't count: logical 20 most, then 5.
  const Ids ccd0 = IdsOf(x3d, 0, 15);
  runs = {};
  runs[20] = 50;
  runs[5] = 12;
  runs[14] = 3;
  CheckApplied(cs::PlanSets(x3d, ccd0, all, runs), x3d, ccd0, all, 4);
  ++cases;
  {
    const cs::Plan plan = cs::PlanSets(x3d, ccd0, all, runs);
    CHECK(plan.worker_sets.size() == 14);
    CHECK(plan.render_sets == IdsOf(x3d, 4, 5));
  }
  // The most used core sums its logical CPUs: core 6 (20 + 20) over logical 10 (30).
  runs = {};
  runs[6] = 20;
  runs[7] = 20;
  runs[10] = 30;
  CheckApplied(cs::PlanSets(x3d, ccd0, all, runs), x3d, ccd0, all, 6);
  ++cases;
  // Ties: the lowest core.
  runs = {};
  runs[13] = 8;
  runs[6] = 8;
  CheckApplied(cs::PlanSets(x3d, ccd0, all, runs), x3d, ccd0, all, 6);
  ++cases;
  // Every single logical CPU as the render thread's, one L3 and the 7950X3D with the rule, every affinity of whole cores
  // 4..16 on both (the 7950X3D's past 8 cores spans both CCDs).
  for (int logical = 0; logical < 32; ++logical) {
    runs = {};
    runs[logical] = 64;
    CheckApplied(cs::PlanSets(one_l3, {}, all, runs), one_l3, {}, all, expected(logical));
    ++cases;
    if (logical < 16) {
      CheckApplied(cs::PlanSets(x3d, ccd0, all, runs), x3d, ccd0, all, expected(logical));
    } else {
      CheckStandDown(cs::PlanSets(x3d, ccd0, all, runs), "ran on none");
    }
    ++cases;
    for (int cores = 4; cores <= 16; ++cores) {
      const uint64_t affinity = (1ull << (cores * 2)) - 1;
      const cs::Plan plan = cs::PlanSets(one_l3, {}, affinity, runs);
      const cs::Plan plan_x3d = cs::PlanSets(x3d, {}, affinity, runs);
      if (logical < cores * 2) {
        CheckApplied(plan, one_l3, {}, affinity, expected(logical));
      } else {
        CheckStandDown(plan, "ran on none");
      }
      if (cores > 8) {
        CheckStandDown(plan_x3d, "L3 cache");
      } else if (logical < cores * 2) {
        CheckApplied(plan_x3d, x3d, {}, affinity, expected(logical));
      } else {
        CheckStandDown(plan_x3d, "ran on none");
      }
      cases += 2;
    }
  }
  // The rule and a narrower affinity together: their intersection (0-7: 4 cores applies, 0-5: 3 cores stands down).
  runs = {};
  runs[2] = 64;
  CheckApplied(cs::PlanSets(x3d, ccd0, 0xFF, runs), x3d, ccd0, 0xFF, 2);
  CheckStandDown(cs::PlanSets(x3d, ccd0, 0x3F, runs), "fewer than 4");
  cases += 2;
  // A rule of 3 cores (0-5).
  CheckStandDown(cs::PlanSets(x3d, IdsOf(x3d, 0, 5), all, runs), "fewer than 4");
  // A rule of 4 cores whose most used core has one logical CPU in it (0-6: core 6 only logical 6): the next core with two.
  runs = {};
  runs[6] = 64;
  CheckApplied(cs::PlanSets(x3d, IdsOf(x3d, 0, 6), all, runs), x3d, IdsOf(x3d, 0, 6), all, 2);
  cases += 2;
  // CPUs Windows gave to other processes are not the game's.
  {
    auto allocated = x3d;
    allocated[8].available = false;
    allocated[9].available = false;
    runs = {};
    runs[9] = 30;
    runs[3] = 20;
    const cs::Plan plan = cs::PlanSets(allocated, ccd0, all, runs);
    CheckApplied(plan, allocated, ccd0, all, 2);
    CHECK(std::ranges::find(plan.worker_sets, x3d[8].id) == plan.worker_sets.end());
    ++cases;
  }
  // No presents counted.
  runs = {};
  CheckStandDown(cs::PlanSets(one_l3, {}, all, runs), "ran on none");
  ++cases;
  // 8 cores without SMT.
  {
    const auto no_smt = Topology(8, 1);
    for (int logical = 0; logical < 8; ++logical) {
      runs = {};
      runs[logical] = 64;
      CheckStandDown(cs::PlanSets(no_smt, {}, all, runs), "SMT");
      ++cases;
    }
  }
  // A hybrid CPU: 6 performance cores (class 1) and 8 efficiency cores (class 0): mixed classes stand down; a rule or an
  // affinity of the performance cores alone applies.
  {
    const auto hybrid = Topology(14, 2, 1, [](int core) -> BYTE { return (core < 6 ? 1 : 0); });
    runs = {};
    runs[4] = 64;
    CheckStandDown(cs::PlanSets(hybrid, {}, all, runs), "different kinds");
    const Ids p_cores = IdsOf(hybrid, 0, 11);
    CheckApplied(cs::PlanSets(hybrid, p_cores, all, runs), hybrid, p_cores, all, 4);
    // The same through an affinity of the performance cores.
    CheckApplied(cs::PlanSets(hybrid, {}, 0xFFF, runs), hybrid, {}, 0xFFF, 4);
    cases += 3;
  }
  // Two processor groups (two 7950X3D halves' worth each), with and without a rule inside group 0.
  {
    const auto groups = Topology(16, 2, 2);
    runs = {};
    runs[4] = 64;
    CheckStandDown(cs::PlanSets(groups, {}, all, runs), "processor group");
    CheckStandDown(cs::PlanSets(groups, IdsOf(groups, 0, 7), all, runs), "processor group");
    cases += 2;
  }
  // Fewer than 4 cores: 3 cores with SMT, 2 cores, 1 core.
  for (int cores = 1; cores <= 3; ++cores) {
    const auto few_cores = Topology(cores, 2);
    runs = {};
    runs[0] = 64;
    CheckStandDown(cs::PlanSets(few_cores, {}, all, runs), "fewer than 4");
    ++cases;
  }
  // 4 cores with SMT: the smallest that applies.
  {
    const auto quad = Topology(4, 2);
    runs = {};
    runs[7] = 64;
    CheckApplied(cs::PlanSets(quad, {}, all, runs), quad, {}, all, 6);
    ++cases;
  }
  // No information.
  CheckStandDown(cs::PlanSets({}, {}, all, runs), "no CPU set information");
  // A default naming no CPU of this PC.
  CheckStandDown(cs::PlanSets(x3d, Ids{0x900}, all, runs), "no CPU inside");
  cases += 2;
  std::printf("  %d cases\n", cases);
}

// Live ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static Ids Selected(HANDLE thread) {
  return cs::ReadSets(GetThreadSelectedCpuSets, thread).value_or(Ids{0xFFFFFFFF});
}

static bool LogHas(std::string_view part) {
  const std::unique_lock lock(log_mutex);
  return std::ranges::any_of(log_lines, [&](const std::string& line) { return line.find(part) != std::string::npos; });
}

static void ClearLog() {
  const std::unique_lock lock(log_mutex);
  log_lines.clear();
}

// A start address query that is refused (STATUS_ACCESS_DENIED): every start unreadable.
static LONG WINAPI FailQuery(HANDLE, ULONG, void*, ULONG, ULONG*) {
  return static_cast<LONG>(0xC0000022);
}

static HANDLE OpenSelf() {
  HANDLE self = nullptr;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, 0, FALSE, DUPLICATE_SAME_ACCESS);
  return self;
}

// A Windows pool thread (it starts in ntdll): holds until `release`, then `done` (its wait is over: `release` may close).
struct PoolItem {
  std::atomic<HANDLE> handle = nullptr;
  HANDLE release = nullptr;
  std::atomic<bool> done = false;
};

static DWORD WINAPI RunPoolItem(void* context) {
  auto* item = static_cast<PoolItem*>(context);
  item->handle = OpenSelf();
  WaitForSingleObject(item->release, INFINITE);
  item->done = true;
  return 0;
}

struct Spinner {
  std::thread thread;
  HANDLE handle = nullptr;
};

static std::atomic<bool> stop_spinners = false;

// A busy thread; `setup` runs first on it (its own rule).
static Spinner Spin(const std::function<void()>& setup = [] {}) {
  Spinner spinner;
  std::atomic<HANDLE> handle = nullptr;
  spinner.thread = std::thread([&handle, setup] {
    setup();
    handle = OpenSelf();
    volatile uint64_t work = 0;
    while (!stop_spinners.load(std::memory_order_relaxed)) {
      for (int i = 0; i < 20000; ++i) work = work + i;
      Sleep(0);
    }
  });
  while (handle.load() == nullptr) Sleep(1);
  spinner.handle = handle.load();
  return spinner;
}

// Presents (cs::Update on this thread, ~1 ms apart) until `done` or `ms`.
static bool PresentUntil(const std::function<bool()>& done, int ms, bool enabled = true, std::span<const wchar_t* const> modules = GAME_MODULES) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    cs::Update(enabled, modules);
    if (done()) return true;
    volatile uint64_t work = 0;
    for (int i = 0; i < 50000; ++i) work = work + i;
    Sleep(1);
  }
  return done();
}

// Every thread of this process: its selected sets (empty: none).
static std::vector<std::pair<DWORD, Ids>> AllThreads() {
  std::vector<std::pair<DWORD, Ids>> threads;
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
  for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
    const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
    if (thread == nullptr) continue;
    threads.emplace_back(entry.th32ThreadID, Selected(thread));
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  return threads;
}

static void Live() {
  std::printf("Part 2: live, this process only\n");
  const auto system = cs::SystemCpuSets();
  std::set<WORD> groups;
  std::set<BYTE> classes;
  std::set<int> cores;
  std::set<BYTE> caches;
  for (const cs::CpuSet& cpu : system) {
    groups.insert(cpu.group);
    classes.insert(cpu.efficiency);
    cores.insert(cpu.group * 256 + cpu.core);
    caches.insert(cpu.cache);
  }
  std::printf("  this PC: %zu logical CPUs, %zu cores, %zu groups, %zu L3 caches, efficiency classes:", system.size(), cores.size(),
              groups.size(), caches.size());
  for (const BYTE efficiency : classes) std::printf(" %u", efficiency);
  std::printf("\n");
  for (int k = 0; k < 3; ++k) {
    const auto start = std::chrono::steady_clock::now();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
    int count = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) ++count;
    CloseHandle(snapshot);
    std::printf("  thread snapshot: %d threads, %.2f ms\n", count, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
  }
  if (groups.size() != 1 || classes.size() != 1 || system.size() < 32 || cores.size() * 2 != system.size()) {
    std::printf("  live part needs one group, one efficiency class, 16+ cores with SMT: skipped\n");
    return;
  }
  REQUIRE(cs::ReadSets(GetProcessDefaultCpuSets, GetCurrentProcess()).value_or(Ids{1}).empty());
  // A read every 20 ms, a check every 100 ms.
  cs::tick_ms = 20;
  const Ids set_a = IdsOf(system, 0, 15);
  const Ids set_b = IdsOf(system, 16, 31);
  const Ids set_c = IdsOf(system, 16, 21);
  const Ids foreign_sets = IdsOf(system, 2, 3);
  const auto set_default = [](const Ids& ids) {
    REQUIRE(SetProcessDefaultCpuSets(GetCurrentProcess(), ids.empty() ? nullptr : ids.data(), static_cast<ULONG>(ids.size())) != FALSE);
  };
  const HANDLE render = OpenSelf();
  set_default(set_a);

  std::vector<Spinner> workers;
  for (int i = 0; i < 6; ++i) workers.push_back(Spin());
  Spinner foreign = Spin([&] { SetThreadSelectedCpuSets(GetCurrentThread(), foreign_sets.data(), static_cast<ULONG>(foreign_sets.size())); });
  Spinner narrow = Spin([] { SetThreadAffinityMask(GetCurrentThread(), 0xF0); });
  // Not the game's: a thread started at kernel32!Sleep (sleeps until the process ends) and a Windows pool thread.
  const HANDLE sleeper = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "Sleep")),
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(INFINITE)), 0, nullptr);
  REQUIRE(sleeper != nullptr);
  PoolItem pool_item = {.release = CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  REQUIRE(QueueUserWorkItem(RunPoolItem, &pool_item, WT_EXECUTELONGFUNCTION) != FALSE);
  while (pool_item.handle.load() == nullptr) Sleep(1);
  const HANDLE pool_thread = pool_item.handle.load();
  const auto check_not_games = [&] {
    CHECK(Selected(sleeper).empty());
    CHECK(Selected(pool_thread).empty());
  };

  // The render thread's sets: one core's, inside `base`; every worker the rest of `base`.
  const auto check_applied = [&](const Ids& base, HANDLE render_thread, const std::vector<HANDLE>& others) {
    const Ids render_sets = Selected(render_thread);
    CHECK(render_sets.size() == 2);
    CHECK(Subset(render_sets, base));
    std::set<BYTE> render_cores;
    for (const cs::CpuSet& cpu : system) {
      if (std::ranges::find(render_sets, cpu.id) != render_sets.end()) render_cores.insert(cpu.core);
    }
    CHECK(render_cores.size() == 1);
    Ids rest;
    std::ranges::set_difference(base, render_sets, std::back_inserter(rest));
    for (const HANDLE other : others) CHECK(Selected(other) == rest);
    // The sets published to the draw recording pool are the ones applied to the other threads.
    uint32_t generation = 0;
    CHECK(cs::AppliedWorkerSets(&generation) == rest);
    return render_sets;
  };
  // Every thread's sets inside `base`, but the ones with their own rule.
  const auto check_all_inside = [&](const Ids& base, const std::vector<Ids>& own_rules) {
    for (const auto& [id, sets] : AllThreads()) {
      if (std::ranges::find(own_rules, sets) != own_rules.end()) continue;
      CHECK(Subset(sets, base));
    }
  };
  const auto check_cleared = [&](const std::vector<HANDLE>& threads) {
    for (const HANDLE thread : threads) CHECK(Selected(thread).empty());
  };
  // The sets published to the draw recording pool: empty after every stand-down and clear (the pool follows within a present).
  const auto check_published_empty = [] {
    uint32_t generation = 0;
    CHECK(cs::AppliedWorkerSets(&generation).empty());
    CHECK(generation == cs::AppliedGeneration());
  };
  std::vector<HANDLE> worker_handles;
  for (const Spinner& spinner : workers) worker_handles.push_back(spinner.handle);
  // After an apply: its on line, logged after the apply's first thread snapshot (a late line is a late snapshot on a
  // loaded PC: tens of ms of CPU with thousands of threads).
  const auto wait_on = [](std::span<const wchar_t* const> modules = GAME_MODULES) {
    return PresentUntil([] { return LogHas("Keep Render Core Free: on:"); }, 10000, true, modules);
  };
  // The on line's count before `what` (such as " not the game's"); -1 without one.
  const auto on_count = [](std::string_view what) {
    const std::unique_lock lock(log_mutex);
    for (const std::string& line : log_lines) {
      if (line.find("Keep Render Core Free: on:") == std::string::npos) continue;
      const size_t end = line.find(what);
      if (end == std::string::npos) return -1;
      const size_t begin = line.rfind(' ', end - 1) + 1;
      return std::atoi(line.substr(begin, end - begin).c_str());
    }
    return -1;
  };

  // 1. Applied inside A.
  ClearLog();
  REQUIRE(wait_on());
  std::printf("  1. applied inside A\n");
  const Ids render_a = check_applied(set_a, render, worker_handles);
  CHECK(Selected(foreign.handle) == foreign_sets);
  CHECK(Selected(narrow.handle).empty());
  check_all_inside(set_a, {foreign_sets});
  CHECK(LogHas("Keep Render Core Free: on:"));
  CHECK(LogHas(", 2 left alone"));
  // The game's: the 6 workers (the render thread given first; cpu_sets' own worker moved too, not counted); not: the
  // sleeper, the pool thread, any other pool thread of Windows'.
  check_not_games();
  std::printf("  1. on line: %d of the game's threads, %d not the game's\n", on_count(" of the game's threads"), on_count(" not the game's"));
  CHECK(on_count(" of the game's threads") == 6);
  CHECK(on_count(" not the game's") >= 2);

  // 2. A thread started later gets the workers' sets at a later check.
  Spinner late = Spin();
  CHECK(PresentUntil([&] { return !Selected(late.handle).empty(); }, 3000));
  check_applied(set_a, render, {late.handle});
  check_not_games();
  std::printf("  2. a later thread given, the others' still not\n");

  // 3. Someone else's rule on one of ours: kept from here on.
  const Ids someone = IdsOf(system, 4, 4);
  REQUIRE(SetThreadSelectedCpuSets(workers[0].handle, someone.data(), 1) != FALSE);
  const std::vector<HANDLE> ours_rest(worker_handles.begin() + 1, worker_handles.end());

  // 4. The default changes to B: ours cleared, measured again, applied inside B.
  ClearLog();
  set_default(set_b);
  CHECK(PresentUntil([&] { return LogHas("CPU set or affinity changed"); }, 3000));
  CHECK(wait_on());
  check_applied(set_b, render, ours_rest);
  check_all_inside(set_b, {foreign_sets, someone});
  CHECK(Selected(workers[0].handle) == someone);
  std::printf("  4. default A -> B: applied inside B\n");

  // 5. A default of 3 cores (C): stands down, ours cleared.
  ClearLog();
  const uint32_t generation_applied = cs::AppliedGeneration();
  set_default(set_c);
  CHECK(PresentUntil([&] { return LogHas("fewer than 4"); }, 5000));
  check_cleared({render, late.handle});
  check_cleared(ours_rest);
  check_published_empty();
  CHECK(cs::AppliedGeneration() != generation_applied);
  CHECK(Selected(foreign.handle) == foreign_sets);
  CHECK(Selected(workers[0].handle) == someone);
  check_all_inside(set_c, {foreign_sets, someone});
  std::printf("  5. default of 3 cores: stood down, cleared\n");

  // 6. Back to A: applied again.
  ClearLog();
  set_default(set_a);
  CHECK(wait_on());
  check_applied(set_a, render, ours_rest);
  std::printf("  6. back to A: applied\n");

  // 7. Another thread presents (this one stops): stands down, measured again, that thread the render thread.
  ClearLog();
  {
    std::atomic<bool> stop_presenting = false;
    std::atomic<HANDLE> presenter_handle = nullptr;
    std::thread presenter([&] {
      presenter_handle = OpenSelf();
      while (!stop_presenting) {
        cs::Update(true, GAME_MODULES);
        volatile uint64_t work = 0;
        for (int i = 0; i < 50000; ++i) work = work + i;
        Sleep(1);
      }
    });
    while (presenter_handle.load() == nullptr) Sleep(1);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < end && (Selected(presenter_handle.load()).size() != 2)) Sleep(10);
    CHECK(LogHas("presents from another thread"));
    Sleep(300);
    std::vector<HANDLE> others = ours_rest;
    others.push_back(render);
    check_applied(set_a, presenter_handle.load(), others);
    stop_presenting = true;
    presenter.join();
    CloseHandle(presenter_handle.load());
    std::printf("  7. another presenting thread: measured again, applied to it\n");
  }
  // This thread presents again: back to it.
  ClearLog();
  CHECK(PresentUntil([&] { return LogHas("presents from another thread") && LogHas("Keep Render Core Free: on:"); }, 10000));
  check_applied(set_a, render, ours_rest);

  // 8. The row off: exactly ours cleared, the others' rules kept.
  ClearLog();
  cs::Update(false, GAME_MODULES);
  CHECK(LogHas("off: ours cleared"));
  check_cleared({render, late.handle});
  check_cleared(ours_rest);
  {
    uint32_t generation = 0;
    CHECK(cs::AppliedWorkerSets(&generation).empty());
  }
  CHECK(Selected(foreign.handle) == foreign_sets);
  CHECK(Selected(workers[0].handle) == someone);
  CHECK(Selected(narrow.handle).empty());
  for (const auto& [id, sets] : AllThreads()) CHECK(sets.empty() || sets == foreign_sets || sets == someone);
  check_not_games();
  std::printf("  8. off: ours cleared, the others' kept\n");

  // 9. On and off 20 times, quickly: symmetric every time.
  for (int round = 0; round < 20; ++round) {
    const bool applied = PresentUntil([&] { return !Selected(render).empty(); }, (round % 2 == 0 ? 10000 : 150));
    if (round % 2 == 0) {
      CHECK(applied);
    }
    cs::Update(false, GAME_MODULES);
    check_cleared({render, late.handle});
    check_cleared(ours_rest);
    CHECK(Selected(foreign.handle) == foreign_sets);
    CHECK(Selected(workers[0].handle) == someone);
  }
  std::printf("  9. 20 on / off rounds: symmetric\n");

  // 10. The render thread with its own rule: stands down, nobody given.
  ClearLog();
  const Ids own = IdsOf(system, 0, 1);
  REQUIRE(SetThreadSelectedCpuSets(GetCurrentThread(), own.data(), 2) != FALSE);
  CHECK(PresentUntil([&] { return LogHas("the render thread has its own CPU rule"); }, 5000));
  PresentUntil([] { return false; }, 500);
  check_cleared({late.handle});
  check_cleared(ours_rest);
  check_published_empty();
  CHECK(Selected(render) == own);
  cs::Update(false, GAME_MODULES);
  CHECK(Selected(render) == own);
  REQUIRE(SetThreadSelectedCpuSets(GetCurrentThread(), nullptr, 0) != FALSE);
  std::printf("  10. render thread's own rule: stood down\n");

  // 11. No default, an affinity of 0-15 (this process's): the base is the affinity.
  ClearLog();
  set_default({});
  DWORD_PTR process_mask = 0;
  DWORD_PTR system_mask = 0;
  REQUIRE(GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) != FALSE);
  REQUIRE(SetProcessAffinityMask(GetCurrentProcess(), 0xFFFF) != FALSE);
  std::vector<HANDLE> plain = ours_rest;
  plain.push_back(late.handle);
  CHECK(wait_on());
  CHECK(LogHas("the game's affinity"));
  check_applied(set_a, render, plain);
  cs::Update(false, GAME_MODULES);
  check_cleared(plain);
  check_cleared({render});
  check_published_empty();
  REQUIRE(SetProcessAffinityMask(GetCurrentProcess(), system_mask) != FALSE);
  std::printf("  11. affinity-only base: applied inside it, cleared\n");
  set_default(set_a);

  // 12. Another thread presents 10 times first, then stops (alive): the count starts again on this thread, applied.
  ClearLog();
  {
    std::atomic<bool> release = false;
    std::atomic<HANDLE> early_handle = nullptr;
    std::thread early([&] {
      early_handle = OpenSelf();
      for (int i = 0; i < 10; ++i) {
        cs::Update(true, GAME_MODULES);
        Sleep(1);
      }
      while (!release.load()) Sleep(10);
    });
    while (cs::samples.load() < 10) Sleep(1);
    CHECK(wait_on());
    std::vector<HANDLE> others = ours_rest;
    others.push_back(early_handle.load());
    check_applied(set_a, render, others);
    release = true;
    early.join();
    CloseHandle(early_handle.load());
    cs::Update(false, GAME_MODULES);
    check_cleared({render});
    std::printf("  12. an early presenter that stops: measured again on this thread, applied\n");
  }

  // 13. Another thread is counted to SAMPLE_PRESENTS after the first check (whose base is never stable), then ends
  // before the second: the plan finds it ended, measures again, this thread applied. A check every second here: the
  // thread presents once (the worker starts), waits 1.5 s, presents the rest at once and ends.
  ClearLog();
  cs::tick_ms = 200;
  {
    DWORD ended_id = 0;
    std::thread ending([&] {
      ended_id = GetCurrentThreadId();
      cs::Update(true, GAME_MODULES);
      Sleep(1500);
      while (cs::samples.load() < cs::SAMPLE_PRESENTS) cs::Update(true, GAME_MODULES);
    });
    ending.join();
    CHECK(cs::render_thread.load() == ended_id);
    CHECK(wait_on());
    CHECK(LogHas("the render thread has ended"));
    check_applied(set_a, render, ours_rest);
    cs::Update(false, GAME_MODULES);
    check_cleared({render});
    check_cleared(ours_rest);
    std::printf("  13. a measured presenter that ended: measured again on this thread, applied\n");
  }
  cs::tick_ms = 20;

  // 14. No game modules from the host, then no start address query: stands down, nobody given (never every thread).
  for (const bool null_query : {false, true}) {
    ClearLog();
    const auto query = cs::query_thread_information;
    if (null_query) {
      cs::query_thread_information = nullptr;
    }
    const std::span<const wchar_t* const> modules = (null_query ? std::span<const wchar_t* const>(GAME_MODULES) : std::span<const wchar_t* const>());
    CHECK(PresentUntil([&] { return LogHas("cannot tell the game's threads apart"); }, 5000, true, modules));
    PresentUntil([] { return false; }, 500, true, modules);
    check_cleared({render, late.handle});
    check_cleared(ours_rest);
    check_published_empty();
    check_not_games();
    CHECK(!LogHas("Keep Render Core Free: on:"));
    cs::Update(false, modules);
    cs::query_thread_information = query;
  }
  std::printf("  14. no game modules / no start query: stood down, nobody given\n");

  // 15. Every start unreadable (the query refused): none of the game's threads to move but the render thread: stands
  // down after the first snapshot, ours cleared, not applied again until the row is off.
  ClearLog();
  {
    const auto query = cs::query_thread_information;
    cs::query_thread_information = FailQuery;
    CHECK(PresentUntil([] { return LogHas("none of the game's threads to move"); }, 10000));
    PresentUntil([] { return false; }, 1000);
    check_cleared({render, late.handle});
    check_cleared(ours_rest);
    check_published_empty();
    check_not_games();
    CHECK(!LogHas("Keep Render Core Free: on:"));
    cs::Update(false, GAME_MODULES);
    CHECK(!LogHas("off: ours cleared"));
    cs::query_thread_information = query;
  }
  std::printf("  15. every start unreadable: nothing to move, stood down, cleared\n");

  // 16. kernel32 as the game's module: only the sleeper is the game's; the render thread (started in the exe) is given.
  ClearLog();
  {
    static constexpr std::array<const wchar_t*, 1> KERNEL32 = {L"kernel32.dll"};
    CHECK(wait_on(KERNEL32));
    check_applied(set_a, render, {sleeper});
    check_cleared({late.handle});
    check_cleared(ours_rest);
    CHECK(Selected(pool_thread).empty());
    CHECK(on_count(" of the game's threads") == 1);
    cs::Update(false, KERNEL32);
    check_cleared({render, sleeper});
    check_published_empty();
  }
  std::printf("  16. kernel32 as the game's module: only the sleeper moved, the render thread given\n");

  // Steady cost of a present with the row on (sampling done).
  PresentUntil([&] { return !Selected(render).empty(); }, 10000);
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000000; ++i) cs::Update(true, GAME_MODULES);
  std::printf("  Update(true): %.1f ns a present; Update(false) with the worker stopped: ",
              std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 1e6);
  cs::Update(false, GAME_MODULES);
  const auto off_start = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000000; ++i) cs::Update(false, GAME_MODULES);
  std::printf("%.1f ns\n", std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - off_start).count() / 1e6);

  stop_spinners = true;
  for (Spinner& spinner : workers) {
    spinner.thread.join();
    CloseHandle(spinner.handle);
  }
  for (Spinner* spinner : {&foreign, &narrow, &late}) {
    spinner->thread.join();
    CloseHandle(spinner->handle);
  }
  SetEvent(pool_item.release);
  while (!pool_item.done.load()) Sleep(1);
  CloseHandle(pool_item.release);
  CloseHandle(pool_thread);
  CloseHandle(sleeper);
  CloseHandle(render);
}

// Control: a broken clear (ours kept after the row is off) must be caught by the off check.
static void Control() {
  // Ours kept: a thread with sets nobody clears fails check 8's rule.
  const auto system = cs::SystemCpuSets();
  const Ids kept = IdsOf(system, 6, 7);
  REQUIRE(SetThreadSelectedCpuSets(GetCurrentThread(), kept.data(), static_cast<ULONG>(kept.size())) != FALSE);
  int caught = 0;
  for (const auto& [id, sets] : AllThreads()) {
    if (!sets.empty()) ++caught;
  }
  REQUIRE(SetThreadSelectedCpuSets(GetCurrentThread(), nullptr, 0) != FALSE);
  CHECK(caught == 1);
  std::printf("Control: a thread whose sets were not cleared is seen (%d)\n", caught);
}

int main() {
  Synthetic();
  Live();
  Control();
  if (failures != 0) {
    std::printf("%d FAILED\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
