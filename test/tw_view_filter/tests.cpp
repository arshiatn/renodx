// The view filter (src/utils/resource.hpp ViewFilterData, resource_upgrade.hpp GetResourceViewClone) against the locked
// lookup it skips. Each part runs in a child process of its own (the filter's OFF is sticky for a process, the shared
// blocks live as long as it). ReShade's exports are mocked here; partner.cpp is a second module built with the filter,
// old.cpp an add-on built before it. Parts (the design's T1, utils_view_memo.md section 5, for the filter alone):
//  random      200,000 random operations over two devices (resource / view init and destroy, hot-swap activate and
//              deactivate, GetResourceClone(activate), resource destroy and its after-destroy pass, wrapper writes,
//              construct-path upserts, CloneResourceView), lookups with all 8 option combinations: every answer the
//              oracle's. CONTROL: a write without the protocol is answered wrong until the self-check (its view's frame
//              phase) turns the filter off.
//  check       the same with RENODX_RESOURCE_MEMO_CHECK=1: 0 mismatches; the control's mismatch logged, the map's answer.
//  threads_*   threads started before the partner DLL is loaded; 2 writers, 3 readers, a thread starting and ending
//              short threads, 10 s, all in the partner; barrier rounds compare every handle of both modules with the
//              oracle. _check: both modules read, every filter answer checked (0 mismatches); _fast: both read, the
//              self-check only (never off); _partner_writes: the partner writes without reading.
//  reuse       handle reuse after destroy and of a live entry; an init callback looking up another view inside the lock.
//  sticky      a pointer-form GetResourceViewInfo turns the filter off (one line); right answers after.
//  old_*       an add-on without the protocol: before the reader's attach (off at attach, its file named); after it
//              (CONTROL: its write answered wrong until the init_device / present check turns the filter off); tokens,
//              not counts (an old module plus a token without a module).
//  pinned      the reader detached and attached again without FreeLibrary; a second thread checking while a partner
//              attaches and detaches 200 times (never off).
//  memo0/memo1 RENODX_RESOURCE_MEMO=0: off (an info line: the A/B base), no bits set, right answers; =1: a module
//              without the flag reads.
//  interest    writes before any reader set no bits; the first reader's walk sets the bits of the views enabled before.
//  full        every module slot taken: off.
//  cost        ns a call: the locked lookup and the filter's answer, one thread and four.
#include <windows.h>

#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "test/tw_view_filter/common.hpp"

namespace test = view_filter_test;
namespace resource = renodx::utils::resource;
namespace upgrade = renodx::utils::resource::upgrade;
using api_event = reshade::addon_event;

#define CHECK(x)                                                     \
  do {                                                               \
    if (!(x)) {                                                      \
      std::printf("FAIL %s line %d: %s\n", part, __LINE__, #x);      \
      std::fflush(stdout);                                           \
      return 1;                                                      \
    }                                                                \
  } while (false)

#if defined(_WIN64)
constexpr const wchar_t* ARCH = L"amd64";
#else
constexpr const wchar_t* ARCH = L"x86";
#endif

// ---- ReShade: the exports the utilities call, the event lists, the log.
static std::array<std::vector<void*>, 512> event_lists;
static std::mutex log_mutex;
static std::vector<std::string> warnings;
static std::vector<std::string> infos;
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int level, const char* message) {
  const std::unique_lock lock(log_mutex);
  if (level == static_cast<int>(reshade::log::level::warning)) warnings.emplace_back(message);
  if (level == static_cast<int>(reshade::log::level::info)) infos.emplace_back(message);
}
__declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char*, const char*, char*, size_t*) { return false; }
__declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char*, const char*, const char*) {}
__declspec(dllexport) void ReShadeRegisterEvent(api_event ev, void* callback) { event_lists[static_cast<size_t>(ev)].push_back(callback); }
__declspec(dllexport) void ReShadeRegisterEventForAddon(void*, api_event ev, void* callback) { event_lists[static_cast<size_t>(ev)].push_back(callback); }
__declspec(dllexport) void ReShadeUnregisterEvent(api_event ev, void* callback) { std::erase(event_lists[static_cast<size_t>(ev)], callback); }
__declspec(dllexport) void ReShadeUnregisterEventForAddon(void*, api_event ev, void* callback) { std::erase(event_lists[static_cast<size_t>(ev)], callback); }
}

static size_t CountLog(const std::vector<std::string>& lines, const char* text) {
  const std::unique_lock lock(log_mutex);
  return std::ranges::count_if(lines, [text](const std::string& line) { return line.find(text) != std::string::npos; });
}

static void InitDevice(reshade::api::device* device) {
  const auto list = event_lists[static_cast<size_t>(api_event::init_device)];
  for (void* callback : list) reinterpret_cast<void (*)(reshade::api::device*)>(callback)(device);
}

static void Present() {
  const auto list = event_lists[static_cast<size_t>(api_event::present)];
  for (void* callback : list) {
    reinterpret_cast<reshade::addon_event_traits<api_event::present>::decl>(callback)(nullptr, nullptr, nullptr, nullptr, 0u, nullptr);
  }
}

static void Attach(bool read) {
  resource::use_view_filter = read;
  upgrade::Use(DLL_PROCESS_ATTACH);
}

static HMODULE LoadTestDll(const wchar_t* stem) {
  std::wstring path(MAX_PATH, L'\0');
  path.resize(GetModuleFileNameW(nullptr, path.data(), MAX_PATH));
  path = path.substr(0, path.find_last_of(L'\\') + 1) + stem + L"_" + ARCH + L".dll";
  return LoadLibraryW(path.c_str());
}

template <typename T>
static T Export(HMODULE module, const char* name) {
  return reinterpret_cast<T>(GetProcAddress(module, name));
}

static bool FilterOff() {
  return (resource::view_filter.data->control.load() & resource::VIEW_FILTER_OFF) != 0u;
}

// A view enabled behind the filter's back: on a live resource, a clone, clone_enabled set by a raw write (no bit). The
// right answer for {require_enabled} is its clone; a filter that trusts the bit answers 0. Its bit clear, and outside
// this frame's self-check phase (so one lookup is answered by the filter).
static uint64_t UnprotectedView(test::Workload* workload, void (*raw_enable)(uint64_t)) {
  uint64_t handle = 0u;
  do {
    handle = workload->device->Fresh();
  } while (test::BitSet(handle) || ((resource::ViewFilterIndex(handle) ^ resource::view_filter_phase.load()) % resource::VIEW_FILTER_SELF_CHECK_FRAMES) == 0u);
  resource::UpsertResourceViewInfo({handle}, [&](resource::ResourceViewInfo* info, bool) {
    info->original_resource = {workload->resources[0]};
    info->clone = {workload->device->Fresh()};
  });
  if (raw_enable != nullptr) {
    raw_enable(handle);
  } else {
    resource::shared.data->resource_view_infos.modify_if(handle, [](std::pair<const uint64_t, resource::ResourceViewInfo>& pair) {
      pair.second.clone_enabled = true;
    });
  }
  return handle;
}

static void RandomRun(test::Workload* first, test::Workload* second, int operations, uint32_t seed, test::Counts* counts) {
  std::mt19937 random(seed);
  for (int index = 0; index < operations; ++index) {
    test::Workload* workload = (random() % 2u == 0u) ? first : second;
    if (random() % 10u < 6u) {
      test::Lookup(workload->AnyHandle(), test::ALL_OPTIONS[random() % test::ALL_OPTIONS.size()], counts);
    } else {
      workload->Write(counts);
    }
  }
}

// Every handle of the workloads with this module's lookups (no writer running).
static uint64_t VerifyAll(test::Workload* first, test::Workload* second) {
  test::Counts counts;
  for (test::Workload* workload : {first, second}) {
    for (const uint64_t handle : workload->views) {
      for (const auto& options : test::ALL_OPTIONS) {
        if (!options.allow_create) test::Lookup(handle, options, &counts);
      }
    }
    for (const uint64_t handle : workload->extra) {
      for (const auto& options : test::ALL_OPTIONS) {
        if (!options.allow_create) test::Lookup(handle, options, &counts);
      }
    }
  }
  return counts.wrong;
}

static std::vector<uint64_t> Handles(test::Workload* first, test::Workload* second) {
  std::vector<uint64_t> handles;
  for (test::Workload* workload : {first, second}) {
    handles.insert(handles.end(), workload->views.begin(), workload->views.end());
    handles.insert(handles.end(), workload->extra.begin(), workload->extra.end());
  }
  return handles;
}

// Every view whose clone_enabled is true has its bit (the filter's invariant once a reader walked).
static size_t MissingBits() {
  size_t missing = 0u;
  resource::shared.data->resource_view_infos.for_each([&](const std::pair<const uint64_t, resource::ResourceViewInfo>& pair) {
    if (pair.second.clone_enabled && !test::BitSet(pair.first)) ++missing;
  });
  return missing;
}

static size_t SetBits() {
  size_t set = 0u;
  for (const auto& word : resource::view_filter.data->enabled_bits) set += std::popcount(word.load());
  return set;
}

static int RunPart(const char* part) {
  test::Device first_device, second_device;
  first_device.base = 0x10000000u;
  second_device.base = 0x20000000u;
  test::Workload first, second;
  first.Init(&first_device, 0x10000000u, 11u);
  second.Init(&second_device, 0x20000000u, 23u);
  const std::string name = part;

  if (name == "random" || name == "check") {
    Attach(true);
    InitDevice(&first_device);
    CHECK(resource::view_filter_reading.load());
    CHECK(resource::view_filter_check.load() == (name == "check"));
    test::Counts counts;
    RandomRun(&first, &second, 200000, 5u, &counts);
    std::printf("  %s: %llu lookups, %llu the filter could answer, %llu writes, %zu bits set\n", part, counts.lookups, counts.filter_answers,
                counts.writes, SetBits());
    CHECK(counts.wrong == 0u);
    CHECK(counts.filter_answers > 5000u);
    CHECK(MissingBits() == 0u);
    CHECK(VerifyAll(&first, &second) == 0u);
    CHECK(!FilterOff());
    if (name == "check") {
      CHECK(upgrade::view_filter_mismatches.load() == 0u);
      CHECK(upgrade::view_filter_checked.load() >= counts.filter_answers);
      CHECK(upgrade::view_filter_checked.load() < 100000u || CountLog(infos, "answers checked") >= 1u);
      // CONTROL: a write without the protocol is a mismatch, logged; the map's answer is returned.
      const uint64_t control = UnprotectedView(&first, nullptr);
      test::Counts control_counts;
      test::Lookup(control, test::ALL_OPTIONS[0], &control_counts);
      CHECK(control_counts.wrong == 0u);
      CHECK(upgrade::view_filter_mismatches.load() == 1u);
      CHECK(CountLog(warnings, "view filter mismatch") == 1u);
    } else {
      // CONTROL: a write without the protocol answered wrong (the filter trusts the bit) until the self-check (the
      // view's phase comes within 256 frames) sees it and turns the filter off, with one line; right from then on.
      const uint64_t control = UnprotectedView(&first, nullptr);
      test::Counts control_counts;
      int frames = 0;
      while (!FilterOff() && frames < 300) {
        test::Lookup(control, test::ALL_OPTIONS[0], &control_counts);
        Present();
        ++frames;
      }
      std::printf("  control: %llu wrong answers (frames) before the self-check turned the filter off\n", control_counts.wrong);
      CHECK(control_counts.wrong >= 1u && control_counts.wrong <= resource::VIEW_FILTER_SELF_CHECK_FRAMES);
      CHECK(FilterOff());
      CHECK(CountLog(warnings, "view filter off: self-check mismatch") == 1u);
      const uint64_t wrong_before = control_counts.wrong;
      test::Lookup(control, test::ALL_OPTIONS[0], &control_counts);
      CHECK(control_counts.wrong == wrong_before);
    }
    return 0;
  }

  if (name == "threads_check" || name == "threads_fast" || name == "threads_partner_writes") {
    const bool partner_reads = (name != "threads_partner_writes");
    Attach(true);
    test::StressControl control;
    control.writers = {&first, &second};
    HANDLE start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    void (*stress)(test::StressControl*, int) = nullptr;
    std::vector<std::thread> threads;
    for (int role = 0; role < 6; ++role) {
      threads.emplace_back([&, role] {
        WaitForSingleObject(start, INFINITE);
        stress(&control, role);
      });
    }
    // The module that runs the threads' work is loaded after they exist.
    HMODULE partner = LoadTestDll(L"view_filter_partner");
    CHECK(partner != nullptr);
    Export<void (*)(bool)>(partner, "PartnerAttach")(partner_reads);
    CHECK(Export<bool (*)()>(partner, "PartnerReading")() == partner_reads);
    stress = Export<void (*)(test::StressControl*, int)>(partner, "PartnerStress");
    const auto verify = Export<uint64_t (*)(const uint64_t*, size_t)>(partner, "PartnerVerify");
    InitDevice(&first_device);
    CHECK(!FilterOff());
    SetEvent(start);
    const auto handles = Handles(&first, &second);
    int rounds = 0;
    uint64_t wrong = 0u;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < end) {
      Sleep(100);
      control.pause.store(true);
      while (control.paused.load() < 2) Sleep(0);
      wrong += VerifyAll(&first, &second);
      wrong += verify(handles.data(), handles.size());
      CHECK(MissingBits() == 0u);
      Present();
      ++rounds;
      control.pause.store(false);
    }
    control.stop.store(true);
    for (auto& thread : threads) thread.join();
    uint32_t checked = 0u, races = 0u, mismatches = 0u;
    Export<void (*)(uint32_t*, uint32_t*, uint32_t*)>(partner, "PartnerCheckCounts")(&checked, &races, &mismatches);
    std::printf("  %s: %d barrier rounds, %llu lookups, %llu writes, %llu short threads; partner checked %u (races %u, mismatches %u)\n", part,
                rounds, control.lookups.load(), control.writes.load(), control.short_threads.load(), checked, races, mismatches);
    CHECK(wrong == 0u);
    CHECK(rounds >= 20);
    CHECK(control.short_threads.load() >= 5u);
    CHECK(mismatches == 0u && upgrade::view_filter_mismatches.load() == 0u);
    if (name == "threads_check") {
      CHECK(checked > 100000u);
    }
    CHECK(!FilterOff());
    CHECK(CountLog(warnings, "view filter") == 0u);
    return 0;
  }

  if (name == "reuse") {
    Attach(true);
    test::Counts counts;
    const auto all = [&](uint64_t handle) {
      for (const auto& options : test::ALL_OPTIONS) test::Lookup(handle, options, &counts);
    };
    // Resource 0 with a hot-swap target, view 0 on it, activated: its bit set at once, its clone made on first use.
    first.resource_live[0] = true;
    resource::OnInitResource(&first_device, first_device.get_resource_desc({0u}), nullptr, reshade::api::resource_usage::shader_resource, {first.resources[0]});
    resource::UpdateResourceInfo({first.resources[0]}, [&](resource::ResourceInfo* info) { info->clone_target = &first.targets[0]; });
    resource::OnInitResource(&first_device, first_device.get_resource_desc({0u}), nullptr, reshade::api::resource_usage::shader_resource, {first.resources[1]});
    const reshade::api::resource_view view = {first.views[0]};
    first_device.SetView(view.handle, first.resources[0]);
    resource::OnInitResourceView(&first_device, {first.resources[0]}, reshade::api::resource_usage::shader_resource,
                                 reshade::api::resource_view_desc(reshade::api::format::r8g8b8a8_unorm, 0u, 1u, 0u, 1u), view);
    all(view.handle);
    CHECK(upgrade::ActivateCloneHotSwap(&first_device, view));
    CHECK(test::BitSet(view.handle));
    all(view.handle);
    // Reuse of a live entry (no destroy) on resource 1 (not enabled), then destroyed and reused on resource 0 again.
    first_device.SetView(view.handle, first.resources[1]);
    resource::OnInitResourceView(&first_device, {first.resources[1]}, reshade::api::resource_usage::shader_resource,
                                 reshade::api::resource_view_desc(reshade::api::format::r8g8b8a8_unorm, 0u, 1u, 0u, 1u), view);
    all(view.handle);
    resource::OnDestroyResourceView(&first_device, view);
    all(view.handle);
    first_device.SetView(view.handle, first.resources[0]);
    resource::OnInitResourceView(&first_device, {first.resources[0]}, reshade::api::resource_usage::shader_resource,
                                 reshade::api::resource_view_desc(reshade::api::format::r8g8b8a8_unorm, 0u, 1u, 0u, 1u), view);
    all(view.handle);
    // A never-enabled handle reused on the enabled resource: enabled (and its bit) by the init itself.
    const reshade::api::resource_view other = {first.views[1]};
    first_device.SetView(other.handle, first.resources[1]);
    resource::OnInitResourceView(&first_device, {first.resources[1]}, reshade::api::resource_usage::shader_resource,
                                 reshade::api::resource_view_desc(reshade::api::format::r8g8b8a8_unorm, 0u, 1u, 0u, 1u), other);
    all(other.handle);
    resource::OnDestroyResourceView(&first_device, other);
    first_device.SetView(other.handle, first.resources[0]);
    resource::OnInitResourceView(&first_device, {first.resources[0]}, reshade::api::resource_usage::shader_resource,
                                 reshade::api::resource_view_desc(reshade::api::format::r8g8b8a8_unorm, 0u, 1u, 0u, 1u), other);
    CHECK(test::BitSet(other.handle));
    all(other.handle);
    // Reentrancy: an init callback (inside the view's exclusive lock) looks up a view of another submap: the oracle's
    // answers, no deadlock; after the write, the view itself.
    static uint64_t inner_view = 0u;
    static test::Counts* inner_counts = nullptr;
    const auto submap = [](uint64_t handle) {
      const size_t hash = resource::shared.data->resource_view_infos.hash(handle);
      return ((hash >> 8u) ^ (hash >> 16u) ^ (hash >> 24u)) & 15u;
    };
    const reshade::api::resource_view outer = {first.views[2]};
    for (const uint64_t candidate : {first.views[0], first.views[1], first.extra[0], first.extra[1], first.extra[2], first.extra[3]}) {
      if (submap(candidate) != submap(outer.handle)) inner_view = candidate;
    }
    CHECK(inner_view != 0u);
    inner_counts = &counts;
    const auto callback = [](resource::ResourceViewInfo*) {
      for (const auto& options : test::ALL_OPTIONS) {
        if (!options.allow_create) test::Lookup(inner_view, options, inner_counts);
      }
    };
    resource::RegisterOnInitResourceViewInfoCallback(callback);
    first_device.SetView(outer.handle, first.resources[0]);
    resource::OnInitResourceView(&first_device, {first.resources[0]}, reshade::api::resource_usage::shader_resource,
                                 reshade::api::resource_view_desc(reshade::api::format::r8g8b8a8_unorm, 0u, 1u, 0u, 1u), outer);
    resource::UnregisterOnInitResourceViewInfoCallback(callback);
    all(outer.handle);
    std::printf("  reuse: %llu lookups\n", counts.lookups);
    CHECK(counts.lookups > 60u);
    CHECK(counts.wrong == 0u);
    CHECK(!FilterOff());
    return 0;
  }

  if (name == "sticky") {
    Attach(true);
    test::Counts counts;
    RandomRun(&first, &second, 20000, 9u, &counts);
    CHECK(counts.wrong == 0u);
    const uint64_t control = UnprotectedView(&first, nullptr);
    test::Counts control_counts;
    test::Lookup(control, test::ALL_OPTIONS[0], &control_counts);
    CHECK(control_counts.wrong == 1u);  // CONTROL: the filter on, trusting the bit
    (void)resource::GetResourceViewInfo(reshade::api::resource_view{first.views[0]});
    CHECK(FilterOff());
    (void)resource::GetResourceViewInfo(reshade::api::resource_view{first.views[1]});
    CHECK(CountLog(warnings, "view filter off: a ResourceViewInfo pointer left the map") == 1u);
    test::Lookup(control, test::ALL_OPTIONS[0], &control_counts);
    CHECK(control_counts.wrong == 1u);
    RandomRun(&first, &second, 20000, 10u, &counts);
    CHECK(counts.wrong == 0u);
    return 0;
  }

  if (name == "old_before" || name == "old_init_device" || name == "old_present" || name == "old_tokens") {
    HMODULE old = LoadTestDll(L"view_filter_old");
    CHECK(old != nullptr);
    const auto old_enable = Export<void (*)(uint64_t)>(old, "OldEnable");
    if (name == "old_before") {
      Export<void (*)()>(old, "OldAttach")();
      Attach(true);
      CHECK(FilterOff());
      CHECK(!resource::view_filter_reading.load());
      CHECK(CountLog(warnings, "view filter off at attach") == 1u);
      CHECK(CountLog(warnings, "view_filter_old") == 1u);
    } else {
      Attach(true);
      CHECK(!FilterOff());
      if (name == "old_tokens") {
        // A token without a module (a module registered only in the filter's block): the counts of tokens and
        // attached modules agree once the old module attaches, the tokens don't.
        const std::unique_lock lock(resource::view_filter.data->mutex);
        resource::view_filter.data->modules[resource::view_filter.data->module_count++] = reinterpret_cast<const void*>(uintptr_t{0x1234});
      }
      Export<void (*)()>(old, "OldAttach")();
      test::Counts counts;
      RandomRun(&first, &second, 5000, 3u, &counts);
      CHECK(counts.wrong == 0u);
      // CONTROL: before any check, the old module's write is answered wrong.
      const uint64_t control = UnprotectedView(&first, old_enable);
      test::Counts control_counts;
      test::Lookup(control, test::ALL_OPTIONS[0], &control_counts);
      CHECK(control_counts.wrong == 1u);
      if (name == "old_present") {
        Present();
      } else {
        InitDevice(&first_device);
      }
      CHECK(FilterOff());
      CHECK(CountLog(warnings, "without its protocol") == 1u);
      CHECK(CountLog(warnings, "view_filter_old") == 1u);
      test::Lookup(control, test::ALL_OPTIONS[0], &control_counts);
      CHECK(control_counts.wrong == 1u);
    }
    test::Counts counts;
    RandomRun(&first, &second, 20000, 4u, &counts);
    CHECK(counts.wrong == 0u);
    Export<void (*)()>(old, "OldDetach")();
    FreeLibrary(old);
    RandomRun(&first, &second, 5000, 6u, &counts);
    CHECK(counts.wrong == 0u);
    CHECK(FilterOff());  // sticky
    return 0;
  }

  if (name == "pinned") {
    Attach(true);
    test::Counts counts;
    RandomRun(&first, &second, 30000, 12u, &counts);
    // Detach and attach again without FreeLibrary (Attila pins itself): off while detached, on again, still exact.
    upgrade::Use(DLL_PROCESS_DETACH);
    CHECK(!resource::view_filter_reading.load());
    CHECK(resource::view_filter.data->module_count == 0u);
    RandomRun(&first, &second, 30000, 13u, &counts);
    Attach(true);
    CHECK(resource::view_filter_reading.load());
    RandomRun(&first, &second, 30000, 14u, &counts);
    CHECK(counts.wrong == 0u);
    CHECK(MissingBits() == 0u);
    // A partner attaching and detaching while another thread checks the writers: its token is in before its record
    // turns active and leaves after, so the filter never turns off.
    HMODULE partner = LoadTestDll(L"view_filter_partner");
    CHECK(partner != nullptr);
    std::atomic_bool stop = false;
    std::thread checker([&] {
      while (!stop.load()) resource::CheckViewFilterWriters();
    });
    for (int round = 0; round < 200; ++round) {
      Export<void (*)(bool)>(partner, "PartnerAttach")(round % 2 == 0);
      Export<void (*)()>(partner, "PartnerDetach")();
    }
    stop.store(true);
    checker.join();
    CHECK(!FilterOff());
    RandomRun(&first, &second, 20000, 15u, &counts);
    CHECK(counts.wrong == 0u);
    std::printf("  pinned: %llu lookups, %llu the filter could answer\n", counts.lookups, counts.filter_answers);
    return 0;
  }

  if (name == "memo0" || name == "memo1") {
    Attach(name == "memo0");
    test::Counts counts;
    RandomRun(&first, &second, 50000, 16u, &counts);
    CHECK(counts.wrong == 0u);
    if (name == "memo0") {
      CHECK(FilterOff());
      CHECK(!resource::view_filter_reading.load());
      CHECK(counts.filter_answers == 0u);
      CHECK(SetBits() == 0u);  // no reader announced itself: writers set nothing
      CHECK(CountLog(warnings, "view filter") == 0u);  // the A/B base: an info line, no warning for check_logs
      CHECK(CountLog(infos, "RENODX_RESOURCE_MEMO=0") == 1u);
    } else {
      // The flag is off; RENODX_RESOURCE_MEMO=1 reads anyway. CONTROL: an unprotected write answered wrong proves it.
      CHECK(resource::view_filter_reading.load());
      CHECK(counts.filter_answers > 2000u);
      const uint64_t control = UnprotectedView(&first, nullptr);
      test::Counts control_counts;
      test::Lookup(control, test::ALL_OPTIONS[0], &control_counts);
      CHECK(control_counts.wrong == 1u);
    }
    return 0;
  }

  if (name == "interest") {
    Attach(false);
    test::Counts counts;
    RandomRun(&first, &second, 50000, 17u, &counts);
    CHECK(counts.wrong == 0u);
    CHECK(SetBits() == 0u);
    size_t enabled = 0u;
    resource::ForEachResourceViewInfo([&](const resource::ResourceViewInfo& info) { enabled += info.clone_enabled ? 1u : 0u; });
    CHECK(enabled > 0u);
    HMODULE partner = LoadTestDll(L"view_filter_partner");
    CHECK(partner != nullptr);
    Export<void (*)(bool)>(partner, "PartnerAttach")(true);
    CHECK(Export<bool (*)()>(partner, "PartnerReading")());
    CHECK(MissingBits() == 0u);
    std::printf("  interest: %zu views enabled before the reader, %zu bits set by its walk\n", enabled, SetBits());
    const auto handles = Handles(&first, &second);
    CHECK(Export<uint64_t (*)(const uint64_t*, size_t)>(partner, "PartnerVerify")(handles.data(), handles.size()) == 0u);
    RandomRun(&first, &second, 20000, 18u, &counts);
    CHECK(MissingBits() == 0u);
    CHECK(Export<uint64_t (*)(const uint64_t*, size_t)>(partner, "PartnerVerify")(handles.data(), handles.size()) == 0u);
    return 0;
  }

  if (name == "full") {
    resource::view_filter.RegisterModule();
    {
      const std::unique_lock lock(resource::view_filter.data->mutex);
      for (size_t slot = 0u; slot < resource::VIEW_FILTER_MAX_MODULES; ++slot) {
        resource::view_filter.data->modules[slot] = reinterpret_cast<const void*>(uintptr_t{0x1000u + slot});
      }
      resource::view_filter.data->module_count = resource::VIEW_FILTER_MAX_MODULES;
    }
    Attach(true);
    CHECK(FilterOff());
    CHECK(!resource::view_filter_reading.load());
    test::Counts counts;
    RandomRun(&first, &second, 20000, 19u, &counts);
    CHECK(counts.wrong == 0u);
    return 0;
  }

  if (name == "cost") {
    Attach(true);
    test::Counts counts;
    RandomRun(&first, &second, 20000, 20u, &counts);
    // A view with its bit set (the locked lookup) and one without (the filter's answer).
    resource::UpsertResourceViewInfo({first.extra[0]}, [&](resource::ResourceViewInfo* info, bool) {
      info->original_resource = {first.resources[0]};
      info->clone_enabled = true;
      info->clone = {first_device.Fresh()};
    });
    uint64_t clear = 0u;
    for (uint64_t candidate = 0x40000000u; clear == 0u; candidate += 16u) {
      if (!test::BitSet(candidate) && (resource::ViewFilterIndex(candidate) % resource::VIEW_FILTER_SELF_CHECK_FRAMES) != 0u) clear = candidate;
    }
    resource::UpsertResourceViewInfo({clear}, [&](resource::ResourceViewInfo* info, bool) { info->original_resource = {first.resources[0]}; });
    const auto time = [](uint64_t handle, int threads) {
      constexpr int CALLS = 2000000;
      const auto start = std::chrono::steady_clock::now();
      std::vector<std::thread> workers;
      for (int thread = 0; thread < threads; ++thread) {
        workers.emplace_back([handle] {
          uint64_t sink = 0u;
          for (int call = 0; call < CALLS; ++call) sink += upgrade::GetResourceViewClone({handle}).handle;
          if (sink == 1u) std::printf("!");
        });
      }
      for (auto& worker : workers) worker.join();
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / CALLS;
    };
    std::printf("  cost: locked %.1f ns, filter %.1f ns a call (1 thread); locked %.1f ns, filter %.1f ns (4 threads, wall per call each)\n",
                time(first.extra[0], 1), time(clear, 1), time(first.extra[0], 4), time(clear, 4));
    return 0;
  }

  std::printf("unknown part %s\n", part);
  return 1;
}

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "--part") == 0) {
    const int result = RunPart(argv[2]);
    std::fflush(stdout);
    return result;
  }
  struct Part {
    const char* name;
    const char* memo;
    const char* check;
  };
  constexpr std::array<Part, 17> PARTS = {{
      {"random", nullptr, nullptr},
      {"check", nullptr, "1"},
      {"threads_check", nullptr, "1"},
      {"threads_fast", nullptr, nullptr},
      {"threads_partner_writes", nullptr, nullptr},
      {"reuse", nullptr, nullptr},
      {"sticky", nullptr, nullptr},
      {"old_before", nullptr, nullptr},
      {"old_init_device", nullptr, nullptr},
      {"old_present", nullptr, nullptr},
      {"old_tokens", nullptr, nullptr},
      {"pinned", nullptr, nullptr},
      {"memo0", "0", nullptr},
      {"memo1", "1", nullptr},
      {"interest", nullptr, nullptr},
      {"full", nullptr, nullptr},
      {"cost", nullptr, nullptr},
  }};
  std::wstring exe(MAX_PATH, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), MAX_PATH));
  int failed = 0;
  for (const Part& part : PARTS) {
    SetEnvironmentVariableA("RENODX_RESOURCE_MEMO", part.memo);
    SetEnvironmentVariableA("RENODX_RESOURCE_MEMO_CHECK", part.check);
    std::wstring command = L"\"" + exe + L"\" --part " + std::wstring(part.name, part.name + std::strlen(part.name));
    STARTUPINFOW startup = {.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process = {};
    std::fflush(stdout);
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0u, nullptr, nullptr, &startup, &process)) {
      std::printf("FAIL %s: not started\n", part.name);
      ++failed;
      continue;
    }
    WaitForSingleObject(process.hProcess, 120000u);
    DWORD code = 1u;
    GetExitCodeProcess(process.hProcess, &code);
    if (code == STILL_ACTIVE) {
      TerminateProcess(process.hProcess, 3u);
      std::printf("FAIL %s: hung\n", part.name);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::printf("%s %s\n", code == 0u ? "ok  " : "FAIL", part.name);
    failed += (code == 0u) ? 0 : 1;
  }
  if (failed == 0) {
    std::printf("tw_view_filter: all %zu parts passed\n", PARTS.size());
  } else {
    std::printf("tw_view_filter: %d parts FAILED\n", failed);
  }
  return failed == 0 ? 0 : 1;
}
