// The bind cache (src/utils/pipeline.hpp BindCacheData, OnBindPipeline) against the locked lookup it skips. Each part runs
// in a child process of its own (OFF is sticky for a process, the shared blocks live as long as it). ReShade's exports
// are mocked here; the exe attaches first (utils::shader::Use, as a mod does), so its OnBindPipeline handles the binds;
// partner.cpp is a second module built with the cache, old.cpp an add-on built before it. Parts:
//  hits        repeated binds skip the lookup (no bind-info callback call) and bind the right replacement; a pipeline
//              without one (first bind: the unlocked retry, not kept); stages are part of the key; unknown handles; 0.
//  destroy     destroy: nothing bound; the handle reused by a new pipeline: its replacement; a replacement changed
//              through the protocol, InvalidateBindCache, a bind-info callback unregistered, the device destroyed.
//  threads_*   4 threads bind while the partner destroys, re-creates and re-replaces 64 pipelines at the same handles:
//              a bound replacement is never older than the version published before the bind; 64 stable pipelines
//              always exact. _check: every hit checked (0 mismatches).
//  check       RENODX_PIPELINE_MEMO_CHECK=1: CONTROL a raw write (no bump): the mismatch logged, the locked answer bound;
//              the check's count line.
//  selfcheck   CONTROL a raw write is answered stale until the present phase reaches its index: then off, right after.
//  old_*       an add-on without the protocol: before the attach (off at attach, its file named); after it (CONTROL: its
//              raw write answered stale until the present / init_device check turns the cache off).
//  pointer     shader::GetPipelineShaderDetails(pipeline) (a writable pointer out of the map): off.
//  partner     a partner attached and detached 50 times: never off, its token gone after each detach.
//  memo0/memo1 RENODX_PIPELINE_MEMO=0: off (an info line: the A/B base), every bind looks up; =1: a module without the
//              flag uses the cache. default: no flag, no switch: every bind looks up, nothing logged.
//  full        every module slot taken: off.
//  cost_*      ns a bind (OnBindPipeline with utils::shader's callback) and a GetPipelineInfo, 1 and 4 threads: _locked
//              (RENODX_PIPELINE_MEMO=0) and _cached.
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "test/tw_pipeline_cache/common.hpp"

namespace test = pipeline_cache_test;
namespace pipe = renodx::utils::pipeline;
using api_event = reshade::addon_event;
using reshade::api::pipeline_stage;

#define CHECK(x)                                                \
  do {                                                          \
    if (!(x)) {                                                 \
      std::printf("FAIL %s line %d: %s\n", part, __LINE__, #x); \
      std::fflush(stdout);                                      \
      return 1;                                                 \
    }                                                           \
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

static void PrintLog() {
  const std::unique_lock lock(log_mutex);
  for (const auto& line : warnings) std::printf("  warning: %s\n", line.c_str());
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

// What OnBindPipeline bound as the replacement (0: none), through the handler's registered event as ReShade calls it.
static uint64_t Bind(test::CommandList* list, uint64_t handle, pipeline_stage stage = pipeline_stage::vertex_shader) {
  static const auto callbacks = event_lists[static_cast<size_t>(api_event::bind_pipeline)];
  list->bound = 0u;
  for (void* callback : callbacks) {
    reinterpret_cast<reshade::addon_event_traits<api_event::bind_pipeline>::decl>(callback)(list, stage, {handle});
  }
  return list->bound;
}

// A non-owner bind-info callback counting its calls: a bind that calls it took the locked lookup.
static std::atomic_uint64_t info_calls = 0u;
static std::optional<reshade::api::pipeline> CountingCallback(
    reshade::api::command_list*, pipeline_stage, reshade::api::pipeline, pipe::PipelineBindPoint, const pipe::PipelineInfo*) {
  info_calls.fetch_add(1u, std::memory_order_relaxed);
  return reshade::api::pipeline{0u};
}

static void Attach(bool use) {
  pipe::use_bind_cache = use;
  renodx::utils::shader::Use(DLL_PROCESS_ATTACH);
  pipe::RegisterOnBindInfoCallback(CountingCallback);
}

static size_t Control() { return pipe::internal::bind_cache.data->control.load(); }
static bool CacheOff() { return (Control() & pipe::BIND_CACHE_OFF) != 0u; }
static bool CacheOn() { return (Control() & (pipe::BIND_CACHE_USE | pipe::BIND_CACHE_OFF)) == pipe::BIND_CACHE_USE; }

// OnBindPipeline's table index (its Fibonacci hash).
static uint32_t CacheIndex(uint64_t handle) {
  const auto folded = static_cast<uint32_t>((handle >> 4u) ^ (handle >> 36u));
  return (folded * 0x9E3779B1u) >> (32u - pipe::BIND_CACHE_INDEX_BITS);
}

// Presents until `handle`'s index is outside this frame's self-check phase (a hit is then answered by the cache alone).
static void LeaveSelfCheckPhase(uint64_t handle) {
  while (((CacheIndex(handle) ^ pipe::internal::bind_cache.data->self_check_phase.load()) % pipe::BIND_CACHE_SELF_CHECK_FRAMES) == 0u) {
    Present();
  }
}

// The replacement set behind the protocol's back (no generation bump).
static void RawSetReplacement(uint64_t handle, uint64_t replacement) {
  pipe::internal::shared.data->pipeline_infos.modify_if(handle, [&](std::pair<const uint64_t, pipe::PipelineInfo>& pair) {
    pair.second.details.replacement_pipeline = {replacement};
  });
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

static int Hits(const char* part) {
  Attach(true);
  CHECK(CacheOn());
  CHECK(CountLog(infos, "utils::pipeline bind cache on.") == 1u);
  test::Device device;
  test::CommandList list;
  list.owner = &device;
  const uint64_t replaced = test::PipelineHandle(0u);
  const uint64_t plain = test::PipelineHandle(1u);
  const uint64_t replacement = test::ReplacementHandle(0u, 1u);
  test::Create(&device, replaced, replacement);
  test::Create(&device, plain, 0u);

  CHECK(Bind(&list, replaced) == replacement);
  CHECK(info_calls.load() == 1u);
  // The first bind of a pipeline without a replacement yet: the unlocked retry builds "none" (a write), not kept.
  CHECK(Bind(&list, plain) == 0u);
  CHECK(info_calls.load() == 3u);
  CHECK(Bind(&list, plain) == 0u);
  CHECK(Bind(&list, replaced) == replacement);
  const uint64_t warm = info_calls.load();
  for (int index = 0; index < 1000; ++index) {
    CHECK(Bind(&list, replaced) == replacement);
    CHECK(Bind(&list, plain) == 0u);
  }
  CHECK(info_calls.load() == warm);
  CHECK(list.bound_count == 1002u);
  // The stages are part of the key (one slot a handle: the two stages take turns; D3D11 binds a handle to one stage).
  for (int index = 0; index < 100; ++index) {
    CHECK(Bind(&list, replaced, pipeline_stage::pixel_shader) == 0u);  // the replacement is the vertex stage's
    CHECK(Bind(&list, replaced) == replacement);
  }

  // Unknown handles bind nothing; a pipeline created there afterwards binds its replacement. Handle 0.
  const uint64_t later = test::PipelineHandle(2u);
  for (int index = 0; index < 10; ++index) CHECK(Bind(&list, later) == 0u);
  test::Create(&device, later, test::ReplacementHandle(2u, 1u));
  CHECK(Bind(&list, later) == test::ReplacementHandle(2u, 1u));
  CHECK(Bind(&list, 0u) == 0u);
  CHECK(CacheOn());
  CHECK(warnings.empty());
  return 0;
}

static int Destroy(const char* part) {
  Attach(true);
  test::Device device;
  test::CommandList list;
  list.owner = &device;
  const uint64_t handle = test::PipelineHandle(0u);
  test::Create(&device, handle, test::ReplacementHandle(0u, 1u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));

  pipe::OnDestroyPipeline(&device, {handle});
  CHECK(device.destroyed_pipelines.load() == 1u);  // utils::shader's destroy callback: the replacement
  CHECK(Bind(&list, handle) == 0u);
  CHECK(Bind(&list, handle) == 0u);
  // Reused by a new pipeline.
  test::Create(&device, handle, test::ReplacementHandle(0u, 2u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 2u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 2u));
  // Reused by a new pipeline without destroy (OnInitPipeline replaces the live entry).
  test::Create(&device, handle, test::ReplacementHandle(0u, 3u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 3u));
  // A replacement changed through the protocol.
  pipe::UpdatePipelineShaderDetails({handle}, [](pipe::PipelineShaderDetails& details) {
    details.replacement_pipeline = {test::ReplacementHandle(0u, 4u)};
  });
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 4u));
  pipe::UpdatePipelineInfo({handle}, [](pipe::PipelineInfo& info) {
    info.details.replacement_stages = pipeline_stage::pixel_shader;
  });
  CHECK(Bind(&list, handle) == 0u);
  CHECK(Bind(&list, handle, pipeline_stage::pixel_shader) == test::ReplacementHandle(0u, 4u));
  // InvalidateBindCache: the next bind looks up.
  CHECK(Bind(&list, handle, pipeline_stage::pixel_shader) == test::ReplacementHandle(0u, 4u));
  uint64_t calls = info_calls.load();
  pipe::InvalidateBindCache();
  CHECK(Bind(&list, handle, pipeline_stage::pixel_shader) == test::ReplacementHandle(0u, 4u));
  CHECK(info_calls.load() == calls + 1u);
  // The counting callback unregistered: cached outcomes from before it no longer hit, answers unchanged.
  pipe::UnregisterOnBindInfoCallback(CountingCallback);
  calls = info_calls.load();
  CHECK(Bind(&list, handle, pipeline_stage::pixel_shader) == test::ReplacementHandle(0u, 4u));
  CHECK(info_calls.load() == calls);
  // The device destroyed: its pipelines leave the map.
  pipe::OnDestroyDevice(&device);
  CHECK(Bind(&list, handle, pipeline_stage::pixel_shader) == 0u);
  CHECK(CacheOn());
  CHECK(warnings.empty());
  return 0;
}

static int Threads(const char* part, bool checked) {
  Attach(true);
  HMODULE partner = LoadTestDll(L"pipeline_cache_partner");
  CHECK(partner != nullptr);
  Export<void (*)()>(partner, "PartnerAttach")();
  test::Device device;
  test::ChurnControl control = {.device = &device};
  for (size_t index = 0u; index < test::CHURN_PIPELINES; ++index) {
    test::Create(&device, test::PipelineHandle(index), test::ReplacementHandle(index, 1u));
    control.published[index].store(1u);
  }
  // Stable pipelines (never written after creation): always their replacement.
  constexpr size_t STABLE = 64u;
  for (size_t index = 0u; index < STABLE; ++index) {
    test::Create(&device, test::PipelineHandle(1000u + index), test::ReplacementHandle(1000u + index, 1u));
  }
  std::atomic_uint64_t wrong = 0u;
  std::atomic_uint64_t binds = 0u;
  std::atomic_uint64_t replaced = 0u;
  std::atomic_bool stop = false;
  std::vector<std::thread> binders;
  for (int role = 0; role < 4; ++role) {
    binders.emplace_back([&, role] {
      test::CommandList list;
      list.owner = &device;
      std::mt19937 random(7919u * static_cast<uint32_t>(role + 1));
      uint64_t local_binds = 0u;
      while (!stop.load(std::memory_order_relaxed)) {
        if ((random() % 2u) == 0u) {
          const size_t index = random() % test::CHURN_PIPELINES;
          const uint32_t published = control.published[index].load();
          const uint64_t bound = Bind(&list, test::PipelineHandle(index));
          if (bound != 0u) {
            replaced.fetch_add(1u, std::memory_order_relaxed);
            const auto bound_index = static_cast<size_t>((bound >> 32u) & 0x7fffffffu);
            const auto bound_version = static_cast<uint32_t>(bound);
            if (bound_index != index || bound_version < published) {
              if (wrong.fetch_add(1u) < 8u) {
                std::printf("  wrong: pipeline %zu bound version %u (index %zu), %u published before the bind\n", index, bound_version,
                            bound_index, published);
              }
            }
          }
        } else {
          const size_t index = 1000u + (random() % STABLE);
          if (Bind(&list, test::PipelineHandle(index)) != test::ReplacementHandle(index, 1u)) {
            if (wrong.fetch_add(1u) < 8u) std::printf("  wrong: stable pipeline %zu\n", index);
          }
        }
        ++local_binds;
      }
      binds.fetch_add(local_binds);
    });
  }
  std::thread churn([&] { Export<void (*)(test::ChurnControl*)>(partner, "PartnerChurn")(&control); });
  const auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
    Present();
    ::Sleep(4);
  }
  control.stop.store(true);
  churn.join();
  stop.store(true);
  for (auto& binder : binders) binder.join();
  Export<void (*)()>(partner, "PartnerDetach")();
  std::printf("  %llu binds (%llu with a replacement), %llu churn rounds, races %u, mismatches %u, checked hits %u\n",
              static_cast<unsigned long long>(binds.load()), static_cast<unsigned long long>(replaced.load()),
              static_cast<unsigned long long>(control.rounds.load()), pipe::internal::bind_cache_races.load(),
              pipe::internal::bind_cache_mismatches.load(), pipe::internal::bind_cache_hits.load());
  PrintLog();
  CHECK(wrong.load() == 0u);
  CHECK(control.rounds.load() > 10u);
  CHECK(replaced.load() > 1000u);
  CHECK(CacheOn());
  CHECK(CountLog(warnings, "bind cache") == 0u);
  if (checked) {
    CHECK(pipe::internal::bind_cache_mismatches.load() == 0u);
    CHECK(pipe::internal::bind_cache_hits.load() > 1000u);
  }
  return 0;
}

static int Check(const char* part) {
  Attach(true);
  CHECK(CacheOn());
  CHECK(CountLog(infos, "every hit checked (RENODX_PIPELINE_MEMO_CHECK=1)") == 1u);
  test::Device device;
  test::CommandList list;
  list.owner = &device;
  const uint64_t handle = test::PipelineHandle(0u);
  test::Create(&device, handle, test::ReplacementHandle(0u, 1u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  CHECK(pipe::internal::bind_cache_mismatches.load() == 0u);
  // CONTROL: a raw write; the check finds it, binds the map's answer.
  RawSetReplacement(handle, test::ReplacementHandle(0u, 2u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 2u));
  CHECK(pipe::internal::bind_cache_mismatches.load() == 1u);
  CHECK(CountLog(warnings, "utils::pipeline bind cache mismatch") == 1u);
  CHECK(CacheOn());  // the check mode reports, it does not turn off
  // A write through the protocol: no mismatch.
  pipe::UpdatePipelineShaderDetails({handle}, [](pipe::PipelineShaderDetails& details) {
    details.replacement_pipeline = {test::ReplacementHandle(0u, 3u)};
  });
  for (int index = 0; index < 100000; ++index) CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 3u));
  CHECK(pipe::internal::bind_cache_mismatches.load() == 1u);
  CHECK(CountLog(infos, "utils::pipeline bind cache check: ") >= 1u);
  return 0;
}

static int SelfCheck(const char* part) {
  Attach(true);
  test::Device device;
  test::CommandList list;
  list.owner = &device;
  const uint64_t handle = test::PipelineHandle(0u);
  test::Create(&device, handle, test::ReplacementHandle(0u, 1u));
  LeaveSelfCheckPhase(handle);
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  // CONTROL: a raw write is answered stale while the self-check skips the index.
  RawSetReplacement(handle, test::ReplacementHandle(0u, 2u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  int frames = 0;
  while (!CacheOff() && frames < 2 * static_cast<int>(pipe::BIND_CACHE_SELF_CHECK_FRAMES)) {
    Present();
    ++frames;
    const uint64_t bound = Bind(&list, handle);
    CHECK(bound == (CacheOff() ? test::ReplacementHandle(0u, 2u) : test::ReplacementHandle(0u, 1u)));
  }
  std::printf("  off after %d frames\n", frames);
  CHECK(CacheOff());
  CHECK(frames <= static_cast<int>(pipe::BIND_CACHE_SELF_CHECK_FRAMES));
  CHECK(CountLog(warnings, "bind cache off: self-check mismatch") == 1u);
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 2u));
  return 0;
}

static int OldBefore(const char* part) {
  HMODULE old = LoadTestDll(L"pipeline_cache_old");
  CHECK(old != nullptr);
  Export<void (*)()>(old, "OldAttach")();
  Attach(true);
  CHECK(CacheOff());
  CHECK(CountLog(warnings, "without its protocol: ") == 1u);
  CHECK(CountLog(warnings, "pipeline_cache_old_") == 1u);
  CHECK(CountLog(warnings, "bind cache off at attach") == 1u);
  test::Device device;
  test::CommandList list;
  list.owner = &device;
  const uint64_t handle = test::PipelineHandle(0u);
  test::Create(&device, handle, test::ReplacementHandle(0u, 1u));
  const uint64_t calls = info_calls.load();
  for (int index = 0; index < 10; ++index) CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  CHECK(info_calls.load() == calls + 10u);  // every bind looks up
  return 0;
}

static int OldAfter(const char* part, bool at_init_device) {
  Attach(true);
  test::Device device;
  test::CommandList list;
  list.owner = &device;
  const uint64_t handle = test::PipelineHandle(0u);
  test::Create(&device, handle, test::ReplacementHandle(0u, 1u));
  LeaveSelfCheckPhase(handle);
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  HMODULE old = LoadTestDll(L"pipeline_cache_old");
  CHECK(old != nullptr);
  Export<void (*)()>(old, "OldAttach")();
  // CONTROL: the old module's write is answered stale until the next check.
  Export<void (*)(uint64_t, uint64_t)>(old, "OldSetReplacement")(handle, test::ReplacementHandle(0u, 2u));
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  CHECK(CacheOn());
  if (at_init_device) {
    InitDevice(&device);
  } else {
    Present();
  }
  CHECK(CacheOff());
  CHECK(CountLog(warnings, "pipeline_cache_old_") == 1u);
  CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 2u));
  return 0;
}

static int Pointer(const char* part) {
  Attach(true);
  test::Device device;
  const uint64_t handle = test::PipelineHandle(0u);
  test::Create(&device, handle, test::ReplacementHandle(0u, 1u));
  CHECK(CacheOn());
  CHECK(renodx::utils::shader::GetPipelineShaderDetails(reshade::api::pipeline{handle}) != nullptr);
  CHECK(CacheOff());
  CHECK(CountLog(warnings, "a PipelineShaderDetails pointer left the map") == 1u);
  return 0;
}

static int Partner(const char* part) {
  Attach(true);
  HMODULE partner = LoadTestDll(L"pipeline_cache_partner");
  CHECK(partner != nullptr);
  test::Device device;
  test::CommandList list;
  list.owner = &device;
  const uint64_t handle = test::PipelineHandle(0u);
  test::Create(&device, handle, test::ReplacementHandle(0u, 1u));
  for (int round = 0; round < 50; ++round) {
    Export<void (*)()>(partner, "PartnerAttach")();
    CHECK(pipe::internal::bind_cache.data->module_count == 2u);
    Present();
    CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
    Export<void (*)()>(partner, "PartnerDetach")();
    CHECK(pipe::internal::bind_cache.data->module_count == 1u);
    Present();
    CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
  }
  CHECK(CacheOn());
  CHECK(warnings.empty());
  return 0;
}

// No cache: every bind looks up.
static int Uncached(const char* part, test::CommandList* list, uint64_t handle) {
  const uint64_t calls = info_calls.load();
  for (int index = 0; index < 10; ++index) CHECK(Bind(list, handle) == test::ReplacementHandle(0u, 1u));
  CHECK(info_calls.load() == calls + 10u);
  return 0;
}

static int Memo(const char* part, bool use, char memo) {
  Attach(use);
  test::Device device;
  test::CommandList list;
  list.owner = &device;
  const uint64_t handle = test::PipelineHandle(0u);
  test::Create(&device, handle, test::ReplacementHandle(0u, 1u));
  switch (memo) {
    case '0':
      CHECK(CacheOff());
      CHECK(CountLog(infos, "utils::pipeline bind cache not used (RENODX_PIPELINE_MEMO=0).") == 1u);
      CHECK(warnings.empty());
      return Uncached(part, &list, handle);
    case '1': {
      CHECK(CacheOn());
      CHECK(CountLog(infos, "utils::pipeline bind cache on.") == 1u);
      CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
      const uint64_t calls = info_calls.load();
      for (int index = 0; index < 10; ++index) CHECK(Bind(&list, handle) == test::ReplacementHandle(0u, 1u));
      CHECK(info_calls.load() == calls);
      return 0;
    }
    default:
      CHECK((Control() & pipe::BIND_CACHE_USE) == 0u);
      CHECK(CountLog(infos, "bind cache") == 0u);
      CHECK(warnings.empty());
      CHECK(event_lists[static_cast<size_t>(api_event::present)].empty());
      return Uncached(part, &list, handle);
  }
}

static int Full(const char* part) {
  pipe::internal::bind_cache.RegisterModule();
  auto* cache = pipe::internal::bind_cache.data;
  static const std::array<uint8_t, pipe::BIND_CACHE_MAX_MODULES> others = {};
  for (size_t index = 0u; index < pipe::BIND_CACHE_MAX_MODULES; ++index) cache->modules[index] = &others[index];
  cache->module_count = pipe::BIND_CACHE_MAX_MODULES;
  Attach(true);
  CHECK(CacheOff());
  CHECK(CountLog(warnings, "bind cache off at attach") == 1u);
  return 0;
}

static int Cost(const char* part, bool cached) {
  Attach(true);
  CHECK(CacheOn() == cached);
  test::Device device;
  constexpr size_t PIPELINES = 256u;
  for (size_t index = 0u; index < PIPELINES; ++index) {
    test::Create(&device, test::PipelineHandle(index), (index % 2u == 0u) ? test::ReplacementHandle(index, 1u) : 0u);
  }
  pipe::UnregisterOnBindInfoCallback(CountingCallback);  // utils::shader's callback alone, as in a mod
  const auto run = [&](int threads, bool bind) {
    constexpr int CALLS = 2000000;
    std::vector<std::thread> workers;
    const auto start = std::chrono::steady_clock::now();
    for (int thread = 0; thread < threads; ++thread) {
      workers.emplace_back([&, thread] {
        test::CommandList list;
        list.owner = &device;
        uint64_t sum = 0u;
        for (int call = 0; call < CALLS; ++call) {
          const uint64_t handle = test::PipelineHandle((static_cast<size_t>(call) * 7u + static_cast<size_t>(thread)) % PIPELINES);
          if (bind) {
            sum += Bind(&list, handle);
          } else {
            pipe::GetPipelineInfo({handle}, [&](const pipe::PipelineInfo& info) { sum += info.generation; });
          }
        }
        if (sum == 1u) std::printf(" ");
      });
    }
    for (auto& worker : workers) worker.join();
    const double nanoseconds = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    return nanoseconds / CALLS;
  };
  run(1, true);  // warm: the first binds build "no replacement" for the plain half
  const double bind_one = run(1, true);
  const double bind_four = run(4, true);
  const double lookup_one = run(1, false);
  const double lookup_four = run(4, false);
  std::printf("  %s: a bind %.1f ns (1 thread), %.1f ns (4 threads, wall per thread call); a GetPipelineInfo %.1f / %.1f ns\n",
              cached ? "cached" : "locked (RENODX_PIPELINE_MEMO=0)", bind_one, bind_four, lookup_one, lookup_four);
  return 0;
}

static int RunPart(const char* part) {
  try {
    const std::string name = part;
    if (name == "hits") return Hits(part);
    if (name == "destroy") return Destroy(part);
    if (name == "threads_fast") return Threads(part, false);
    if (name == "threads_check") return Threads(part, true);
    if (name == "check") return Check(part);
    if (name == "selfcheck") return SelfCheck(part);
    if (name == "old_before") return OldBefore(part);
    if (name == "old_present") return OldAfter(part, false);
    if (name == "old_init_device") return OldAfter(part, true);
    if (name == "pointer") return Pointer(part);
    if (name == "partner") return Partner(part);
    if (name == "memo0") return Memo(part, true, '0');
    if (name == "memo1") return Memo(part, false, '1');
    if (name == "default") return Memo(part, false, '\0');
    if (name == "full") return Full(part);
    if (name == "cost_locked") return Cost(part, false);
    if (name == "cost_cached") return Cost(part, true);
  } catch (const std::exception& error) {
    std::printf("FAIL %s: exception %s\n", part, error.what());
    return 1;
  }
  std::printf("FAIL %s: unknown part\n", part);
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
      {"hits", nullptr, nullptr},
      {"destroy", nullptr, nullptr},
      {"threads_fast", nullptr, nullptr},
      {"threads_check", nullptr, "1"},
      {"check", nullptr, "1"},
      {"selfcheck", nullptr, nullptr},
      {"old_before", nullptr, nullptr},
      {"old_present", nullptr, nullptr},
      {"old_init_device", nullptr, nullptr},
      {"pointer", nullptr, nullptr},
      {"partner", nullptr, nullptr},
      {"memo0", "0", nullptr},
      {"memo1", "1", nullptr},
      {"default", nullptr, nullptr},
      {"full", nullptr, nullptr},
      {"cost_locked", "0", nullptr},
      {"cost_cached", nullptr, nullptr},
  }};
  std::wstring exe(MAX_PATH, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), MAX_PATH));
  int failed = 0;
  for (const Part& part : PARTS) {
    SetEnvironmentVariableA("RENODX_PIPELINE_MEMO", part.memo);
    SetEnvironmentVariableA("RENODX_PIPELINE_MEMO_CHECK", part.check);
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
    std::printf("tw_pipeline_cache: all %zu parts passed\n", PARTS.size());
  } else {
    std::printf("tw_pipeline_cache: %d parts FAILED\n", failed);
  }
  return failed == 0 ? 0 : 1;
}
