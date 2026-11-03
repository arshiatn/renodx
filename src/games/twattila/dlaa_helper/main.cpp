/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War DLAA helper (64-bit, renodx-tw-dlaa64.exe next to the addon): NVIDIA's DLSS runs only in 64-bit
// processes and Attila, Rome II and Thrones are 32-bit. The addon starts it with its process id; it opens the addon's
// shared memory and events (dlaa_bridge.h), makes a D3D11 device on the game's GPU, and for each request: its GPU waits
// for the addon's fence value, NVIDIA's DLAA runs on the shared textures (talesofarise_dlss/ngx_backend.hpp), the fence
// is signalled one higher (also after a failure, so the game never waits for nothing). Ray Reconstruction, when the
// addon asks with its guides, runs through D3D12 (rr_d3d12.hpp; nvngx_dlssd.dll next to nvngx_dlss.dll). Ray tracing
// requests (bridge v4: the addon's rt_scene.hpp) run on another D3D12 device (rt_d3d12.hpp). Frame generation requests
// (bridge v5: the addon's frame_generation.hpp, one per game present) present through Streamline's DLSS Frame Generation
// into a child window over the game's (fg_d3d12.hpp). AMD's FSR and Intel's XeSS (bridge v22) run on D3D12 too, in DLSS's
// place (upscale_d3d12.hpp). It quits with the game. It answers addons from bridge version OLDEST_VERSION on.
// Build (x64): see twattila/README.md.
#include <windows.h>

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../talesofarise_dlss/ngx_backend.hpp"
#include "../dlaa_bridge.h"
#include "./fg_d3d12.hpp"
#include "./rr_d3d12.hpp"
#include "./rt_d3d12.hpp"
#include "./upscale_d3d12.hpp"

using Microsoft::WRL::ComPtr;

// ReShade's exports (this process stands in for ReShade): the backend's log lines into renodx-tw-dlaa64.log.
static FILE* log_file = nullptr;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(HMODULE, uint32_t) { return true; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(HMODULE) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  if (log_file == nullptr) return;
  std::fprintf(log_file, "%s\n", message);
  std::fflush(log_file);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argument_count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments == nullptr || argument_count < 2) return 1;
  const DWORD game_id = std::wcstoul(arguments[1], nullptr, 10);
  LocalFree(arguments);

  std::wstring folder(MAX_PATH, L'\0');
  folder.resize(GetModuleFileNameW(nullptr, folder.data(), MAX_PATH));
  folder = folder.substr(0, folder.find_last_of(L"\\/"));
  const std::wstring log_path = folder + L"\\renodx-tw-dlaa64.log";
  _wfopen_s(&log_file, log_path.c_str(), L"w");

  const std::wstring name = L"Local\\RenoDX_TW_DLAA_" + std::to_wstring(game_id);
  HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, game_id);
  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
  HANDLE request = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (name + L"_request").c_str());
  HANDLE done = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (name + L"_done").c_str());
  if (game == nullptr || mapping == nullptr || request == nullptr || done == nullptr) {
    ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] The game's shared memory or events were not found.");
    return 1;
  }
  // The block as the addon's version made it (v2's ends before v3's fields): its version first.
  const auto map = [&](size_t bytes) {
    return static_cast<twattila::dlaa_bridge::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
  };
  auto* shared = map(twattila::dlaa_bridge::SharedBytes(twattila::dlaa_bridge::OLDEST_VERSION));
  const uint32_t version = (shared != nullptr ? shared->version : 0u);
  if (version >= 3 && shared != nullptr) {
    UnmapViewOfFile(shared);
    shared = map(twattila::dlaa_bridge::SharedBytes(version));
  }
  if (shared == nullptr || version < twattila::dlaa_bridge::OLDEST_VERSION || version > twattila::dlaa_bridge::VERSION) {
    ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] Shared memory unusable (another version?).");
    return 1;
  }
  const auto set_status = [&](const char* status) {
    strncpy_s(shared->status, status, _TRUNCATE);
  };
  // The cluster sets' data (v16): the addon's second mapping (null: none, no compact draws).
  const uint8_t* cluster_data = nullptr;
  if (HANDLE clusters = (version >= 16) ? OpenFileMappingW(FILE_MAP_READ, FALSE, (name + L"_clusters").c_str()) : nullptr; clusters != nullptr) {
    cluster_data = static_cast<const uint8_t*>(MapViewOfFile(clusters, FILE_MAP_READ, 0, 0, twattila::dlaa_bridge::RT_CLUSTER_DATA_BYTES));
    CloseHandle(clusters);
  }

  // A device on the game's GPU.
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  const LUID luid = {.LowPart = shared->adapter_luid_low, .HighPart = shared->adapter_luid_high};
  constexpr D3D_FEATURE_LEVEL LEVELS[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  ComPtr<ID3D11Device1> device1;
  ComPtr<ID3D11Device5> device5;
  ComPtr<ID3D11DeviceContext4> context4;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))
      || FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(adapter.GetAddressOf())))
      || FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, LEVELS, 2, D3D11_SDK_VERSION,
                                  device.GetAddressOf(), nullptr, context.GetAddressOf()))
      || FAILED(device.As(&device1)) || FAILED(device.As(&device5)) || FAILED(context.As(&context4))) {
    set_status("DLAA helper: no D3D11.4 device on the game's GPU");
    return 1;
  }
  // Only requests are answered through `done` (the addon reads `ready` before its first one).
  set_status("DLAA helper ready");
  shared->ready = 1;

  talesofarise::dlss::Backend backend;
  bool super_resolution_ran = false;  // its feature is made (freed while Ray Reconstruction runs)
  // Ray Reconstruction (v3 addons with its guides): its D3D12 device and the addon's objects opened there (a new device
  // after a GPU reset removed it).
  auto reconstruction = std::make_unique<twattila::dlaa_helper::RayReconstruction12>();
  bool reconstruction_open = false;
  // FSR or XeSS (v22 addons choosing one): opened with their first request of a set; NGX stays out of it (DLSS and Ray
  // Reconstruction are for NVIDIA's GPUs: their licence).
  auto upscaler = std::make_unique<twattila::dlaa_helper::Upscaler12>();
  bool upscaler_tried = false;
  bool upscaler_open = false;
  bool upscaler_live = false;
  const auto other_upscaler = [&] { return version >= 22 && shared->upscaler != twattila::dlaa_bridge::UPSCALER_DLSS; };
  // Ray tracing (v4 addons with it on): its D3D12 device and the addon's objects opened there. After a GPU reset removed
  // it, a new one with the Experimental options that build the scene in their own way paused until the game restarts; a
  // second removal leaves ray tracing off (ATN 2026-10-29: a hang, then three new devices lost at once, and Windows then
  // refused the helper new devices: frame generation's too). A request that could not be recorded takes a new scene too,
  // nothing paused; the fourth leaves it off (whatever made it fail would come back each time).
  auto scene = std::make_unique<twattila::dlaa_helper::RtScene12>();
  bool scene_open = false;
  // Ray tracing's and Ray Reconstruction's devices let go after IDLE_RELEASE without a request of theirs (gigabytes of the
  // GPU's memory: 15 GB stayed after RT/PT was turned off, 7.5 GB with it never on, ATN 2026-11-01), opened again with
  // their next one (the addon then captures its scene again: rt_device). FSR's and XeSS's too. Frame generation's resources, swap chain and window
  // too, once its set has no textures of it: turned off in the settings, not ReShade's menu open (1.5 GB stayed; ~0.6 GB of
  // it, Streamline and its device, still does: FreeResources).
  constexpr auto IDLE_RELEASE = std::chrono::seconds(10);
  auto rt_used = std::chrono::steady_clock::now();
  auto rr_used = rt_used;
  auto fg_used = rt_used;
  auto upscale_used = rt_used;
  bool generation_resources = false;
  bool scene_idle = false;
  bool reconstruction_idle = false;
  bool scene_live = false;           // its device made (open or closed: a set without ray tracing closes it, its memory stays)
  bool reconstruction_live = false;
  uint32_t scene_losses = 0;
  uint32_t scene_failures = 0;
  const auto scene_off = [&] { return scene_losses >= 2 || scene_failures >= 4; };
  constexpr uint32_t PAUSED_TRACE_FLAGS = twattila::dlaa_bridge::RT_GPU_MICROMAPS | twattila::dlaa_bridge::RT_SWAY_IN_VIEW;
  constexpr uint32_t PAUSED_PATH_FLAGS = twattila::dlaa_bridge::RT_PATH_SER | twattila::dlaa_bridge::RT_PATH_SHARC;
  constexpr const char* RT_OFF = "Ray tracing off after repeated failures (renodx-tw-dlaa64.log): restart the game";
  // The handles the addon shared for the open set (its textures and fence, duplicated into this process), kept until the
  // next set: a new ray tracing device opens them again.
  std::vector<HANDLE> set_handles;
  // Frame generation (v5 addons with it on): Streamline's D3D12 device, its window and swap chain (made again after a GPU
  // reset removed the device).
  auto generation = std::make_unique<twattila::dlaa_helper::FrameGeneration12>();
  bool generation_open = false;
  uint32_t opened_generation = 0;
  ComPtr<ID3D11Texture2D> color;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11Texture2D> motion;
  ComPtr<ID3D11Texture2D> output;
  ComPtr<ID3D11Fence> fence;
  // Ray tracing requests of a kind (trace, Light Bounce, reflections) that ran last time are answered once copied (their
  // fence value is always the next; v16: with their new cluster sets' data, which the addon writes again for its next)
  // and recorded in order by a worker of their own while the game goes on; the rest, and anything that opens or closes
  // the scene, wait for the worker to be done.
  struct Work {
    std::unique_ptr<twattila::dlaa_bridge::Shared> shared;
    std::vector<uint8_t> sets;
  };
  struct RtWork {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Work> pending;
    std::array<bool, 3> ran = {};
    bool busy = false;
    bool stop = false;
  } rt_work;
  // A trace request's new cluster sets' data: the mapping's start up to the end of the last.
  const auto new_sets = [&](const twattila::dlaa_bridge::Shared& from) {
    size_t bytes = 0;
    for (uint32_t index = 0; version >= 16 && from.rt_request == 1 && index < std::min(from.rt_new_set_count, twattila::dlaa_bridge::RT_NEW_SETS); ++index) {
      const auto& set = from.rt_new_sets[index];
      bytes = std::max(bytes, size_t{set.offset} + (size_t{set.clusters} * 16) + (size_t{set.triangles} * 3));
    }
    return (cluster_data != nullptr) ? std::span(cluster_data, std::min<size_t>(bytes, twattila::dlaa_bridge::RT_CLUSTER_DATA_BYTES))
                                     : std::span<const uint8_t>();
  };
  const auto run = [&](const twattila::dlaa_bridge::Shared& from, std::span<const uint8_t> sets) {
    return (version >= 7 && from.rt_request >= 2) ? scene->Paths(from, version, from.rt_request == 3) : scene->Trace(from, version, sets);
  };
  const auto kind = [&](const twattila::dlaa_bridge::Shared& from) {
    return std::min<uint32_t>(version >= 7 ? from.rt_request : 1u, 3u) - 1;
  };
  // Its status for the addon, (v15) the garbage triangles its scene left out so far, the last such capture's key, and
  // (v16) whether it takes compact draws.
  const auto publish = [&] {
    strncpy_s(shared->rt_status, scene_off() ? RT_OFF : scene->Status().c_str(), _TRUNCATE);
    if (const std::string reports = scene->TakeReports(); !reports.empty()) ReShadeLogMessage(nullptr, 0, reports.c_str());
    if (version >= 15) {
      const auto& left_out = scene->LeftOutSoFar();
      shared->rt_left_out = left_out.far_off + left_out.parts + left_out.others;
      shared->rt_left_out_key = left_out.key;
    }
    if (version >= 16) {
      shared->rt_compact_ok = (cluster_data != nullptr) ? scene->CompactState() : 0u;
      shared->rt_dynamic_uvs = scene->DynamicUvs();
    }
  };
  std::thread rt_worker([&] {
    std::unique_lock lock(rt_work.mutex);
    while (true) {
      rt_work.changed.wait(lock, [&] { return rt_work.stop || !rt_work.pending.empty(); });
      if (rt_work.pending.empty()) return;
      const Work work = std::move(rt_work.pending.front());
      rt_work.pending.pop_front();
      rt_work.busy = true;
      lock.unlock();
      const bool ran = run(*work.shared, work.sets);
      publish();
      lock.lock();
      rt_work.ran[kind(*work.shared)] = ran;
      rt_work.busy = false;
      rt_work.changed.notify_all();
    }
  });
  const auto rt_drain = [&] {
    std::unique_lock lock(rt_work.mutex);
    rt_work.changed.wait(lock, [&] { return rt_work.pending.empty() && !rt_work.busy; });
  };
  // A GPU fault or hang (Windows' timeout detection) removed the ray tracing's device, or a request could not be recorded:
  // where the GPU stopped logged (DRED), a new scene (opened by the caller after the first) and the addon told to capture
  // its scene again.
  const auto replace_lost_scene = [&] {
    rt_drain();
    const bool removed = scene->Removed();
    if (removed) {
      ReShadeLogMessage(nullptr, 0, scene->LostReport().c_str());
    }
    scene = std::make_unique<twattila::dlaa_helper::RtScene12>();
    scene_open = false;
    {
      const std::unique_lock lock(rt_work.mutex);
      rt_work.ran = {};
    }
    if (version >= 15) ++shared->rt_device;
    if (!removed) {
      ++scene_failures;
      ReShadeLogMessage(nullptr, 0, scene_off() ? "[TW ray tracing] Off until the game restarts (a fourth failed request)."
                                                : "[TW ray tracing] Started again after a failed request.");
      return;
    }
    ReShadeLogMessage(nullptr, 0, (++scene_losses == 1)
                                      ? "[TW ray tracing] Started again on a new device, with GPU Micromap Baking and the Experimental Swaying "
                                        "Shadows Range, Off-Screen Sway, Shader Execution Reordering and SHaRC Radiance Cache paused until the game "
                                        "restarts."
                                      : "[TW ray tracing] Off until the game restarts (a second GPU reset).");
  };
  const auto open_scene = [&] {
    const bool details = (shared->rt_flags & twattila::dlaa_bridge::RT_DETAILS) != 0;
    scene_live = !scene_off();
    scene_open = scene_live && scene->Initialize(adapter.Get(), folder, details) && scene->Open(*shared, version);
  };
  // The open set's objects in Ray Reconstruction and frame generation (their handles stay open: set_handles), on a new
  // device when a GPU reset removed theirs.
  const auto open_reconstruction = [&] {
    if (reconstruction->Lost()) {
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] A GPU reset removed Ray Reconstruction's device: a new one.");
      reconstruction = std::make_unique<twattila::dlaa_helper::RayReconstruction12>();
    }
    reconstruction_live = true;
    reconstruction_open = reconstruction->Initialize(adapter.Get(), folder)
                          && reconstruction->Open({shared->color, shared->depth, shared->motion, shared->output, shared->diffuse_albedo,
                                                   shared->specular_albedo, shared->normals, shared->roughness},
                                                  shared->fence, (version >= 17) ? shared->rr_hit_distance : 0u);
  };
  const auto open_generation = [&] {
    if (generation->Lost()) {
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] A GPU reset removed frame generation's device: Streamline started again.");
      generation->Shutdown();
      generation = std::make_unique<twattila::dlaa_helper::FrameGeneration12>();
    }
    generation_open = generation->Initialize(adapter.Get(), folder) && generation->Open(*shared, version);
    strncpy_s(shared->fg_status, generation->Status().c_str(), _TRUNCATE);
    shared->fg_failed = (generation_open ? 0u : 1u);
  };
  const auto release_idle = [&] {
    const auto now = std::chrono::steady_clock::now();
    if (scene_live && version >= 15 && now - rt_used > IDLE_RELEASE) {
      rt_drain();
      scene = std::make_unique<twattila::dlaa_helper::RtScene12>();
      scene_open = false;
      scene_live = false;
      scene_idle = true;
      {
        const std::unique_lock lock(rt_work.mutex);
        rt_work.ran = {};
      }
      ++shared->rt_device;
      ReShadeLogMessage(nullptr, 0, "[TW ray tracing] Off for 10 s: its memory released (it starts again when turned on).");
    }
    if (reconstruction_live && now - rr_used > IDLE_RELEASE) {
      reconstruction = std::make_unique<twattila::dlaa_helper::RayReconstruction12>();
      reconstruction_open = false;
      reconstruction_live = false;
      reconstruction_idle = true;
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] Ray Reconstruction off for 10 s: its memory released.");
    }
    if (upscaler_live && now - upscale_used > IDLE_RELEASE) {
      upscaler = std::make_unique<twattila::dlaa_helper::Upscaler12>();
      upscaler_tried = false;
      upscaler_open = false;
      upscaler_live = false;
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] FSR / XeSS off for 10 s: their memory released.");
    }
    if (generation_resources && now - fg_used > IDLE_RELEASE) {
      generation->FreeResources();
      generation_resources = false;
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] Frame generation off for 10 s: its memory released.");
    }
  };
  const HANDLE waits[] = {request, game};
  // With no requests coming (DLSS turned off) the wait still wakes for release_idle.
  const auto next_request = [&] {
    DWORD woken = WaitForMultipleObjects(2, waits, FALSE, 1000);
    for (; woken == WAIT_TIMEOUT; woken = WaitForMultipleObjects(2, waits, FALSE, 1000)) release_idle();
    return woken;
  };
  while (next_request() == WAIT_OBJECT_0) {
    if (shared->generation != opened_generation) {
      rt_drain();
      // The addon's new textures and fence, opened; the last set's handles closed, this set's kept (set_handles).
      for (const HANDLE handle : set_handles) CloseHandle(handle);
      set_handles.clear();
      const auto keep = [&](std::initializer_list<uint64_t> handles) {
        for (const uint64_t handle : handles) {
          if (handle != 0) set_handles.push_back(reinterpret_cast<HANDLE>(handle));
        }
      };
      keep({shared->color, shared->depth, shared->motion, shared->output, shared->fence});
      if (version >= 3) keep({shared->diffuse_albedo, shared->specular_albedo, shared->normals, shared->roughness});
      if (version >= 4) keep({shared->rt_vertices, shared->rt_positions, shared->rt_sun, shared->rt_visibility, shared->rt_ambient});
      if (version >= 5) keep({shared->fg_frame, shared->fg_hudless});
      if (version >= 6) keep({shared->rt_atlas});
      if (version >= 7) keep({shared->rt_light, shared->rt_bounce});
      if (version >= 8) keep({shared->rt_reflection, shared->rt_specular});
      if (version >= 9) keep({shared->rt_light_mask});
      if (version >= 11) keep({shared->rt_fog});
      if (version >= 17) keep({shared->rr_hit_distance});
      if (version >= 18) keep({shared->rt_fog_lights});
      color.Reset();
      depth.Reset();
      motion.Reset();
      output.Reset();
      fence.Reset();
      upscaler->Close();
      upscaler_tried = false;
      upscaler_open = false;
      // The guides serve Ray Reconstruction and the paths.
      reconstruction_open = false;
      if (version >= 3 && shared->diffuse_albedo != 0 && !reconstruction_idle && !other_upscaler()) {
        open_reconstruction();
      } else {
        reconstruction->Close();
      }
      if (scene->Lost()) replace_lost_scene();
      scene_open = false;
      if (version >= 4 && shared->rt_vertices != 0 && !scene_idle) {
        open_scene();
        if (!scene_open) strncpy_s(shared->rt_status, scene_off() ? RT_OFF : scene->Status().c_str(), _TRUNCATE);
      } else {
        scene->Close();
      }
      generation_open = false;
      if (version >= 5 && shared->fg_frame != 0) {
        open_generation();
      } else {
        generation->Close();
      }
      const auto open = [&](uint64_t handle, ComPtr<ID3D11Texture2D>* texture) {
        return SUCCEEDED(device1->OpenSharedResource1(reinterpret_cast<HANDLE>(handle), IID_PPV_ARGS(texture->GetAddressOf())));
      };
      bool opened = open(shared->color, &color);
      opened = open(shared->depth, &depth) && opened;
      opened = open(shared->motion, &motion) && opened;
      opened = open(shared->output, &output) && opened;
      opened = SUCCEEDED(device5->OpenSharedFence(reinterpret_cast<HANDLE>(shared->fence),
                                                  IID_PPV_ARGS(fence.GetAddressOf())))
               && opened;
      opened_generation = shared->generation;
      if (!opened) {
        set_status("DLAA helper: the shared textures could not be opened");
        fence.Reset();
      }
    }
    // A GPU reset removed Ray Reconstruction's or frame generation's device: a new one on this set (until then requests
    // pass without them).
    if (reconstruction_open && reconstruction->Lost()) open_reconstruction();
    if (generation_open && generation->Lost()) open_generation();
    if (upscaler_open && upscaler->Lost()) upscaler_tried = false;
    const auto now = std::chrono::steady_clock::now();
    if (shared->rt_request != 0) {
      rt_used = now;
      if (scene_idle) {
        scene_idle = false;
        open_scene();
      }
    }
    if (shared->ray_reconstruction != 0) {
      rr_used = now;
      if (reconstruction_idle) {
        reconstruction_idle = false;
        open_reconstruction();
      }
    }
    if (shared->fg_frame != 0) {
      fg_used = now;
      generation_resources = true;
    }
    release_idle();
    bool evaluated = false;
    bool answered = false;
    if (fence && version >= 5 && shared->fg_request != 0) {
      // Frame generation (D3D12): its copy queue waits for the addon's value and signals the next.
      evaluated = generation_open && generation->Present(shared);
      if (!generation_open) {
        context4->Wait(fence.Get(), shared->value);
        context4->Signal(fence.Get(), shared->value + 1);
        context->Flush();
      }
      strncpy_s(shared->fg_status, generation->Status().c_str(), _TRUNCATE);
    } else if (fence && version >= 4 && shared->rt_request != 0) {
      // Ray tracing (D3D12): its queue waits for the addon's value and signals the next (v7: request 2 the Light Bounce,
      // v8: request 3 the reflections).
      // A kind that ran last time goes to the worker and is answered at once: the addon's GPU then waits for its value
      // before the addon signals the next (the fence's order kept). Otherwise it runs here, after the worker's.
      // A GPU fault or hang removed its device (ATN 2026-10-28: a freeze, then no ray tracing until the game restarted): a
      // new device on this set's textures after the first (the addon makes a new set at once: the fence the lost device
      // shared reads all ones, dlaa_bridge::Prepare).
      if (scene_open && scene->Lost()) {
        replace_lost_scene();
        open_scene();
      }
      // After a GPU reset the paused Experimental options stay off (the addon writes them again each request).
      if (scene_losses != 0) {
        shared->rt_flags &= ~PAUSED_TRACE_FLAGS;
        if (version >= 8) shared->rt_path_flags &= ~PAUSED_PATH_FLAGS;
        if (version >= 18) shared->rt_sway_range = 0.f;
      }
      bool queued = false;
      if (scene_open) {
        const std::unique_lock lock(rt_work.mutex);
        if (rt_work.ran[kind(*shared)]) {
          auto copy = std::make_unique<twattila::dlaa_bridge::Shared>();
          std::memcpy(copy.get(), shared, twattila::dlaa_bridge::SharedBytes(version));
          const auto sets = new_sets(*shared);
          rt_work.pending.push_back({.shared = std::move(copy), .sets = {sets.begin(), sets.end()}});
          queued = true;
        }
      }
      if (queued) {
        rt_work.changed.notify_all();
        shared->evaluated = 1;
        shared->done_value = shared->value + 1;
        SetEvent(done);
        answered = true;
      } else {
        rt_drain();
        evaluated = scene_open && run(*shared, new_sets(*shared));
        if (scene_open) {
          const std::unique_lock lock(rt_work.mutex);
          rt_work.ran[kind(*shared)] = evaluated;
        } else {
          context4->Wait(fence.Get(), shared->value);
          context4->Signal(fence.Get(), shared->value + 1);
          context->Flush();
        }
        publish();
      }
    } else if (fence && other_upscaler()) {
      // FSR or XeSS (D3D12): its queue waits for the addon's value and signals the next.
      upscale_used = now;
      if (!upscaler_tried) {
        upscaler_tried = true;
        if (upscaler->Lost()) ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] A GPU reset removed FSR's / XeSS's device: a new one.");
        if (upscaler->Lost() || upscaler->Backend() != shared->upscaler) upscaler = std::make_unique<twattila::dlaa_helper::Upscaler12>();
        upscaler_live = true;
        upscaler_open = upscaler->Initialize(adapter.Get(), folder, shared->upscaler)
                        && upscaler->Open({shared->color, shared->depth, shared->motion, shared->output}, shared->fence);
      }
      if (std::exchange(super_resolution_ran, false)) backend.Reset();
      reconstruction->ReleaseFeature();
      twattila::dlaa_helper::Request upscale = {
          .value = shared->value,
          .width = shared->width,
          .height = shared->height,
          .output_width = shared->output_width,
          .output_height = shared->output_height,
          .jitter_x = shared->jitter_x,
          .jitter_y = shared->jitter_y,
          .reset = (shared->reset != 0),
      };
      std::copy_n(shared->view_to_clip, 16, upscale.view_to_clip.begin());
      evaluated = upscaler_open && upscaler->Evaluate(upscale);
      if (!upscaler_open) {
        context4->Wait(fence.Get(), shared->value);
        context4->Signal(fence.Get(), shared->value + 1);
        context->Flush();
      }
      set_status(upscaler->Status().c_str());
    } else if (fence && reconstruction_open && shared->ray_reconstruction != 0) {
      // Ray Reconstruction (D3D12): its queue waits for the addon's value and signals the next.
      twattila::dlaa_helper::Request reconstruct = {
          .value = shared->value,
          .width = shared->width,
          .height = shared->height,
          .output_width = shared->output_width,
          .output_height = shared->output_height,
          .jitter_x = shared->jitter_x,
          .jitter_y = shared->jitter_y,
          .reset = (shared->reset != 0),
          .preset = shared->ray_reconstruction_preset,
          .hit_distance = version >= 17 && shared->rr_hit_distance_written != 0,
      };
      std::copy_n(shared->world_to_view, 16, reconstruct.world_to_view.begin());
      std::copy_n(shared->view_to_clip, 16, reconstruct.view_to_clip.begin());
      if (std::exchange(super_resolution_ran, false)) backend.Reset();
      evaluated = reconstruction->Evaluate(reconstruct);
      set_status(reconstruction->Status().c_str());
    } else if (fence) {
      reconstruction->ReleaseFeature();
      super_resolution_ran = true;
      context4->Wait(fence.Get(), shared->value);
      evaluated = backend.Evaluate(context.Get(), {
                                                      .color = color.Get(),
                                                      .depth = depth.Get(),
                                                      .motion_vectors = motion.Get(),
                                                      .output = output.Get(),
                                                      .width = shared->width,
                                                      .height = shared->height,
                                                      .jitter_x = shared->jitter_x,
                                                      .jitter_y = shared->jitter_y,
                                                      .reset = (shared->reset != 0),
                                                      .output_width = shared->output_width,
                                                      .output_height = shared->output_height,
                                                      .mode = (shared->output_width > shared->width
                                                                   ? talesofarise::dlss::Mode::DLSS_CUSTOM
                                                                   : talesofarise::dlss::Mode::DLAA),
                                                      .preset = shared->preset,
                                                      .create_flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR
                                                                      | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes
                                                                      | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure,
                                                  });
      context4->Signal(fence.Get(), shared->value + 1);
      context->Flush();
      set_status(backend.Status());
    }
    if (answered) continue;
    shared->evaluated = (evaluated ? 1 : 0);
    // The fence value it signalled (none without a fence).
    shared->done_value = (fence ? shared->value + 1 : shared->value);
    SetEvent(done);
  }
  {
    const std::unique_lock lock(rt_work.mutex);
    rt_work.stop = true;
  }
  rt_work.changed.notify_all();
  rt_worker.join();
  return 0;
}
