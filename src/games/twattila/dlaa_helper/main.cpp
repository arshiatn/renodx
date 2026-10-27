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
// into a child window over the game's (fg_d3d12.hpp). It quits with the game. It answers addons of bridge versions 2
// to 5.
// Build (x64): see twattila/README.md.
#include <windows.h>

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "../../talesofarise_dlss/ngx_backend.hpp"
#include "../dlaa_bridge.h"
#include "./fg_d3d12.hpp"
#include "./rr_d3d12.hpp"
#include "./rt_d3d12.hpp"

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
  // Ray Reconstruction (v3 addons with its guides): its D3D12 device and the addon's objects opened there.
  twattila::dlaa_helper::RayReconstruction12 reconstruction;
  bool reconstruction_open = false;
  // Ray tracing (v4 addons with it on): its D3D12 device and the addon's objects opened there.
  twattila::dlaa_helper::RtScene12 scene;
  bool scene_open = false;
  // Frame generation (v5 addons with it on): Streamline's D3D12 device, its window and swap chain.
  twattila::dlaa_helper::FrameGeneration12 generation;
  bool generation_open = false;
  uint32_t opened_generation = 0;
  ComPtr<ID3D11Texture2D> color;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11Texture2D> motion;
  ComPtr<ID3D11Texture2D> output;
  ComPtr<ID3D11Fence> fence;
  const HANDLE waits[] = {request, game};
  while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0) {
    if (shared->generation != opened_generation) {
      // The addon's new textures and fence: opened, the duplicated handles closed.
      color.Reset();
      depth.Reset();
      motion.Reset();
      output.Reset();
      fence.Reset();
      // D3D12 opens them first (the D3D11 opens close the handles).
      reconstruction_open = false;
      if (version >= 3 && shared->diffuse_albedo != 0) {
        reconstruction_open = reconstruction.Initialize(adapter.Get(), folder)
                              && reconstruction.Open({shared->color, shared->depth, shared->motion, shared->output, shared->diffuse_albedo,
                                                      shared->specular_albedo, shared->normals, shared->roughness},
                                                     shared->fence);
        for (const uint64_t guide : {shared->diffuse_albedo, shared->specular_albedo, shared->normals, shared->roughness}) {
          CloseHandle(reinterpret_cast<HANDLE>(guide));
        }
      } else {
        reconstruction.Close();
      }
      scene_open = false;
      if (version >= 4 && shared->rt_vertices != 0) {
        scene_open = scene.Initialize(adapter.Get(), folder) && scene.Open(*shared, version);
        for (const uint64_t texture : {shared->rt_vertices, shared->rt_positions, shared->rt_sun, shared->rt_visibility, shared->rt_ambient}) {
          CloseHandle(reinterpret_cast<HANDLE>(texture));
        }
        if (version >= 6 && shared->rt_atlas != 0) CloseHandle(reinterpret_cast<HANDLE>(shared->rt_atlas));
        if (version >= 7) {
          for (const uint64_t texture : {shared->rt_light, shared->rt_bounce}) {
            if (texture != 0) CloseHandle(reinterpret_cast<HANDLE>(texture));
          }
        }
        if (!scene_open) strncpy_s(shared->rt_status, scene.Status().c_str(), _TRUNCATE);
      } else {
        scene.Close();
      }
      generation_open = false;
      if (version >= 5 && shared->fg_frame != 0) {
        generation_open = generation.Initialize(adapter.Get(), folder) && generation.Open(*shared);
        for (const uint64_t texture : {shared->fg_frame, shared->fg_hudless}) {
          CloseHandle(reinterpret_cast<HANDLE>(texture));
        }
        strncpy_s(shared->fg_status, generation.Status().c_str(), _TRUNCATE);
        shared->fg_failed = (generation_open ? 0u : 1u);
      } else {
        generation.Close();
      }
      const auto open = [&](uint64_t handle, ComPtr<ID3D11Texture2D>* texture) {
        const bool opened = SUCCEEDED(device1->OpenSharedResource1(reinterpret_cast<HANDLE>(handle),
                                                                   IID_PPV_ARGS(texture->GetAddressOf())));
        CloseHandle(reinterpret_cast<HANDLE>(handle));
        return opened;
      };
      bool opened = open(shared->color, &color);
      opened = open(shared->depth, &depth) && opened;
      opened = open(shared->motion, &motion) && opened;
      opened = open(shared->output, &output) && opened;
      opened = SUCCEEDED(device5->OpenSharedFence(reinterpret_cast<HANDLE>(shared->fence),
                                                  IID_PPV_ARGS(fence.GetAddressOf())))
               && opened;
      CloseHandle(reinterpret_cast<HANDLE>(shared->fence));
      opened_generation = shared->generation;
      if (!opened) {
        set_status("DLAA helper: the shared textures could not be opened");
        fence.Reset();
      }
    }
    bool evaluated = false;
    if (fence && version >= 5 && shared->fg_request != 0) {
      // Frame generation (D3D12): its copy queue waits for the addon's value and signals the next.
      evaluated = generation_open && generation.Present(shared);
      if (!generation_open) {
        context4->Wait(fence.Get(), shared->value);
        context4->Signal(fence.Get(), shared->value + 1);
        context->Flush();
      }
      strncpy_s(shared->fg_status, generation.Status().c_str(), _TRUNCATE);
    } else if (fence && version >= 4 && shared->rt_request != 0) {
      // Ray tracing (D3D12): its queue waits for the addon's value and signals the next (v7: request 2 the Light Bounce).
      evaluated = scene_open && ((version >= 7 && shared->rt_request == 2) ? scene.Bounce(*shared) : scene.Trace(*shared, version));
      if (!scene_open) {
        context4->Wait(fence.Get(), shared->value);
        context4->Signal(fence.Get(), shared->value + 1);
        context->Flush();
      }
      strncpy_s(shared->rt_status, scene.Status().c_str(), _TRUNCATE);
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
      };
      std::copy_n(shared->world_to_view, 16, reconstruct.world_to_view.begin());
      std::copy_n(shared->view_to_clip, 16, reconstruct.view_to_clip.begin());
      if (std::exchange(super_resolution_ran, false)) backend.Reset();
      evaluated = reconstruction.Evaluate(reconstruct);
      set_status(reconstruction.Status().c_str());
    } else if (fence) {
      reconstruction.ReleaseFeature();
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
    shared->evaluated = (evaluated ? 1 : 0);
    // The fence value it signalled (none without a fence).
    shared->done_value = (fence ? shared->value + 1 : shared->value);
    SetEvent(done);
  }
  return 0;
}
