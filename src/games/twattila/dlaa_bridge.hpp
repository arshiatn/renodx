/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <span>
#include <cstdint>
#include <cstring>
#include <string>

#include "../../utils/log.hpp"
#include "./dlaa_bridge.h"

// The addon's side of the DLAA bridge (dlaa_bridge.h): on first use the 64-bit helper (renodx-tw-dlaa64.exe next to
// the addon) is started in a job that ends it with the game; the inputs (this frame linear, its depth and motion), the
// output and a fence are made shareable and their handles duplicated into it. Each frame: the fence signalled at the
// inputs, one request, and the helper's answer awaited on the CPU (it only submits its work, well under a millisecond);
// only then does the game's GPU wait for the helper's fence value, so a missing or late helper never stalls the GPU
// (no answer in 100 ms, 5 s for the first while NVIDIA sets up: DLAA off). With Ray Reconstruction its guides too
// (written by the addon's G-buffer pass). With ray tracing (rt_scene.hpp) its textures too, and a request of its own
// each frame (TraceScene). With frame generation (frame_generation.hpp) the presented frame and the HUD-less frame in the
// swap chain's format too, and a request at each present (PresentFrame). D3D11.4 (shared fences).
namespace twattila::dlaa_bridge {

using Microsoft::WRL::ComPtr;

struct Texture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

// A ray tracing request (rt_scene.hpp): its flags and settings, the frame's new captures (a leaf's opacity mask slice
// each) and the meshes it drew.
struct TraceRequest {
  uint32_t flags = 0;  // RT_SUN_SHADOWS, RT_AMBIENT_OCCLUSION
  float ambient_range = 2.f;
  uint32_t ambient_rays = 2;
  float sun_softness = 0.f;  // degrees of the sun's disk (0: hard shadows)
  uint32_t frame = 0;
  uint32_t width = 0;  // the render size (the textures' top-left)
  uint32_t height = 0;
  std::span<const RtCapture> captures;
  std::span<const uint32_t> slices;  // as the captures
  std::span<const uint64_t> drawn;
};

// A ray-traced Light Bounce request (light_bounce.hpp through rt_scene.hpp): the render size, the bounce's size and its
// texels' render pixels, the rays per texel and their reach (metres), this frame's view-projection (the camera's
// registers).
struct BounceRequest {
  uint32_t frame = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t bounce_width = 0;
  uint32_t bounce_height = 0;
  uint32_t scale = 1;
  uint32_t rays = 4;
  float range = 10.f;
  std::array<float, 16> view_projection = {};
  // v8: path tracing (RT_PATH_TRACING), its surfaces past the first and paths a texel; the guides written this frame.
  uint32_t path_flags = 0;
  uint32_t path_bounces = 2;
  uint32_t path_samples = 1;
  bool guides = false;
};

// A reflections request (v8, rt_scene.hpp at the game's reflection pass): the render size, RT_REFLECTIONS and / or
// RT_SPECULAR_OCCLUSION, the GGX samples a pixel and the surfaces their paths go on past the first, the paths' reach
// past the first surface and the specular occlusion's (metres), this frame's view-projection, the guides written.
struct ReflectRequest {
  uint32_t frame = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t flags = 0;
  uint32_t samples = 1;
  uint32_t bounces = 1;
  float range = 10.f;
  float reflection_range = 10.f;
  std::array<float, 16> view_projection = {};
  bool guides = false;
};

// A frame generation request (frame_generation.hpp): the game's window, the settings, whether the HUD-less frame and
// this frame's DLAA inputs (the depth and motion textures, their size and jitter) were written, and the camera (row
// vectors, without the jitter).
struct FrameRequest {
  HWND window = nullptr;
  uint32_t multiplier = 0;
  bool reflex_boost = false;
  uint32_t frame_limit = 0;  // Reflex's, rendered frames per second (0: none)
  bool hudless = false;
  bool inputs = false;
  uint32_t frame_index = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  float jitter_x = 0.f;
  float jitter_y = 0.f;
  bool reset = false;
  std::array<float, 16> view_to_clip = {};
  std::array<float, 16> view_projection = {};
  std::array<float, 16> previous_view_projection = {};
  std::array<float, 12> camera = {};  // position, right, up, forward
  float near_plane = 0.f;
  float far_plane = 0.f;
};

// Ray Reconstruction's settings for a request (its guides are the bridge's textures).
struct RayReconstruction {
  uint32_t preset = 0;  // NVSDK_NGX_RayReconstruction_Hint_Render_Preset
  std::array<float, 16> world_to_view = {};
  std::array<float, 16> view_to_clip = {};
};

struct Bridge {
  // The last evaluation ran Ray Reconstruction: the addon's own denoisers (Light Bounce, AO filter, the reflections'
  // accumulation and filter) leave its noise to it.
  bool reconstructing = false;
  HANDLE mapping = nullptr;
  HANDLE request = nullptr;
  HANDLE done = nullptr;
  HANDLE job = nullptr;
  HANDLE process = nullptr;
  Shared* shared = nullptr;
  bool started = false;
  bool failed = false;
  std::string status = "DLAA: not started";
  // The helper's DLAA, frame generation and ray tracing lines as last logged (each change goes to the log).
  std::array<std::string, 3> logged;
  uint64_t logged_time = 0;  // the ray tracing line's (GetTickCount64)
  // The game's device and its shared objects (the textures at `width` x `height`).
  ComPtr<ID3D11Device5> device;
  UINT width = 0;
  UINT height = 0;
  Texture color;
  Texture depth;
  Texture motion;
  Texture output;
  // Ray Reconstruction's guides (made when it is on).
  Texture diffuse_albedo;
  Texture specular_albedo;
  Texture normals;
  Texture roughness;
  bool guides = false;
  // Ray tracing's (made when it is on): the vertex texture, world positions, the sun in; the visibilities out.
  Texture rt_vertices;
  Texture rt_positions;
  Texture rt_sun;
  Texture rt_visibility;
  Texture rt_ambient;
  // The ray-traced Light Bounce's (v7): its prepared light in, the bounce out.
  Texture rt_light;
  Texture rt_bounce;
  // The reflections' (v8, made when they are on): the reflected light and the specular occlusion out.
  Texture rt_reflection;
  Texture rt_specular;
  bool reflections = false;
  // The leaves' opacity masks (an RT_ATLAS_SLICES texture array, made with the first ray tracing textures and kept
  // across sizes: shared with the helper again with every set; `rt_atlas_generation` counts the arrays made).
  Texture rt_atlas;
  uint32_t rt_atlas_generation = 0;
  bool ray_tracing = false;
  // Frame generation's (made when it is on): the presented frame and the HUD-less frame, in the swap chain's format.
  ComPtr<ID3D11Texture2D> fg_frame;
  ComPtr<ID3D11Texture2D> fg_hudless;
  ComPtr<ID3D11RenderTargetView> fg_hudless_rtv;
  DXGI_FORMAT fg_format = DXGI_FORMAT_UNKNOWN;
  ComPtr<ID3D11Fence> fence;
  uint64_t value = 0;
  bool answered = false;  // the helper's first answer came (NVIDIA's set-up: up to seconds)
};

static Bridge bridge;

namespace internal {

inline bool Fail(const std::string& status) {
  bridge.failed = true;
  bridge.status = status;
  renodx::utils::log::w("[Total War] ", status);
  return false;
}

// The helper started (once; false: it cannot be), told the game's GPU (`device`'s) before it runs.
inline bool Start(ID3D11Device* device) {
  if (bridge.started) return !bridge.failed;
  bridge.started = true;
  const std::wstring name = L"Local\\RenoDX_TW_DLAA_" + std::to_wstring(GetCurrentProcessId());
  bridge.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name.c_str());
  bridge.request = CreateEventW(nullptr, FALSE, FALSE, (name + L"_request").c_str());
  bridge.done = CreateEventW(nullptr, FALSE, FALSE, (name + L"_done").c_str());
  if (bridge.mapping == nullptr || bridge.request == nullptr || bridge.done == nullptr) {
    return Fail("DLAA: shared memory unavailable.");
  }
  bridge.shared = static_cast<Shared*>(MapViewOfFile(bridge.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
  if (bridge.shared == nullptr) return Fail("DLAA: shared memory unavailable.");
  *bridge.shared = Shared{};
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC adapter_desc = {};
  if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf()))) && SUCCEEDED(dxgi_device->GetAdapter(adapter.GetAddressOf()))
      && SUCCEEDED(adapter->GetDesc(&adapter_desc))) {
    bridge.shared->adapter_luid_low = adapter_desc.AdapterLuid.LowPart;
    bridge.shared->adapter_luid_high = adapter_desc.AdapterLuid.HighPart;
  }

  // renodx-tw-dlaa64.exe next to this addon.
  HMODULE addon = nullptr;
  static const int ANCHOR = 0;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&ANCHOR), &addon);
  std::wstring path(MAX_PATH, L'\0');
  path.resize(GetModuleFileNameW(addon, path.data(), MAX_PATH));
  path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"renodx-tw-dlaa64.exe";
  std::wstring command = L"\"" + path + L"\" " + std::to_wstring(GetCurrentProcessId());
  STARTUPINFOW startup = {.cb = sizeof(STARTUPINFOW)};
  PROCESS_INFORMATION information = {};
  if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                      nullptr, &startup, &information)) {
    return Fail("DLAA: renodx-tw-dlaa64.exe not found next to the addon.");
  }
  // Ended with the game (the job's last handle closes with it).
  bridge.job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (bridge.job != nullptr) {
    SetInformationJobObject(bridge.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    AssignProcessToJobObject(bridge.job, information.hProcess);
  }
  ResumeThread(information.hThread);
  CloseHandle(information.hThread);
  bridge.process = information.hProcess;
  bridge.status = "DLAA: starting NVIDIA's helper";
  renodx::utils::log::i("[Total War] DLAA: renodx-tw-dlaa64.exe started.");
  return true;
}

// `object`'s shared handle, duplicated into the helper (closed here).
inline uint64_t ShareWithHelper(HANDLE handle) {
  HANDLE duplicate = nullptr;
  DuplicateHandle(GetCurrentProcess(), handle, bridge.process, &duplicate, 0, FALSE,
                  DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(duplicate));
}

}  // namespace internal

// The shared objects for `width` x `height` on `device` (with Ray Reconstruction's guides when `guides`, ray tracing's
// textures when `ray_tracing`, frame generation's in `fg_format` unless unknown, the reflections' with ray tracing when
// `reflections`; made again when any changes); false: not available.
inline bool Prepare(ID3D11Device* native_device, UINT width, UINT height, bool guides = false, bool ray_tracing = false,
                    DXGI_FORMAT fg_format = DXGI_FORMAT_UNKNOWN, bool reflections = false) {
  reflections = reflections && ray_tracing;
  if (!internal::Start(native_device)) return false;
  ComPtr<ID3D11Device5> device;
  if (FAILED(native_device->QueryInterface(IID_PPV_ARGS(device.GetAddressOf())))) {
    return internal::Fail("DLAA: the game's device has no D3D11.4 (shared fences).");
  }
  if (bridge.device == device && bridge.width == width && bridge.height == height && bridge.guides == guides
      && bridge.ray_tracing == ray_tracing && bridge.fg_format == fg_format && bridge.reflections == reflections) {
    return true;
  }
  bridge.color = {};
  bridge.depth = {};
  bridge.motion = {};
  bridge.output = {};
  bridge.diffuse_albedo = {};
  bridge.specular_albedo = {};
  bridge.normals = {};
  bridge.roughness = {};
  bridge.rt_vertices = {};
  bridge.rt_positions = {};
  bridge.rt_sun = {};
  bridge.rt_visibility = {};
  bridge.rt_ambient = {};
  bridge.rt_light = {};
  bridge.rt_bounce = {};
  bridge.rt_reflection = {};
  bridge.rt_specular = {};
  bridge.fg_frame.Reset();
  bridge.fg_hudless.Reset();
  bridge.fg_hudless_rtv.Reset();
  bridge.fg_format = DXGI_FORMAT_UNKNOWN;
  bridge.fence.Reset();
  if (bridge.device != device) bridge.rt_atlas = {};
  bridge.device = device;
  bridge.width = 0;
  const auto make = [&](DXGI_FORMAT format, Texture* texture, uint64_t* shared_handle, UINT texture_width = 0, UINT texture_height = 0) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = (texture_width != 0 ? texture_width : width),
        .Height = (texture_height != 0 ? texture_height : height),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
        .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
    };
    ComPtr<IDXGIResource1> resource;
    HANDLE handle = nullptr;
    const bool made = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->texture.GetAddressOf()))
                      && SUCCEEDED(device->CreateShaderResourceView(texture->texture.Get(), nullptr, texture->srv.GetAddressOf()))
                      && SUCCEEDED(device->CreateUnorderedAccessView(texture->texture.Get(), nullptr, texture->uav.GetAddressOf()))
                      && SUCCEEDED(texture->texture.As(&resource))
                      && SUCCEEDED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                                nullptr, &handle));
    if (made) *shared_handle = internal::ShareWithHelper(handle);
    return made;
  };
  // Frame generation's: copied into and drawn into (render targets), read by the helper.
  const auto make_frame = [&](ComPtr<ID3D11Texture2D>* texture, uint64_t* shared_handle) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = width,
        .Height = height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = fg_format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
        .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
    };
    ComPtr<IDXGIResource1> resource;
    HANDLE handle = nullptr;
    const bool made = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->GetAddressOf())) && SUCCEEDED(texture->As(&resource))
                      && SUCCEEDED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                                nullptr, &handle));
    if (made) *shared_handle = internal::ShareWithHelper(handle);
    return made;
  };
  // The leaves' opacity masks: made once, shared again with every set.
  const auto share_atlas = [&]() {
    if (!bridge.rt_atlas.texture) {
      const D3D11_TEXTURE2D_DESC desc = {
          .Width = RT_ATLAS_SIZE,
          .Height = RT_ATLAS_SIZE,
          .MipLevels = 1,
          .ArraySize = RT_ATLAS_SLICES,
          .Format = DXGI_FORMAT_R8_UNORM,
          .SampleDesc = {.Count = 1},
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
          .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
      };
      if (FAILED(device->CreateTexture2D(&desc, nullptr, bridge.rt_atlas.texture.GetAddressOf()))
          || FAILED(device->CreateUnorderedAccessView(bridge.rt_atlas.texture.Get(), nullptr, bridge.rt_atlas.uav.GetAddressOf()))) {
        bridge.rt_atlas = {};
        return false;
      }
      ++bridge.rt_atlas_generation;
    }
    ComPtr<IDXGIResource1> resource;
    HANDLE handle = nullptr;
    if (FAILED(bridge.rt_atlas.texture.As(&resource))
        || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle))) {
      return false;
    }
    bridge.shared->rt_atlas = internal::ShareWithHelper(handle);
    return true;
  };
  auto& shared = *bridge.shared;
  HANDLE fence_handle = nullptr;
  const bool made = make(DXGI_FORMAT_R16G16B16A16_FLOAT, &bridge.color, &shared.color)
                    && make(DXGI_FORMAT_R32_FLOAT, &bridge.depth, &shared.depth)
                    && make(DXGI_FORMAT_R16G16_FLOAT, &bridge.motion, &shared.motion)
                    && make(DXGI_FORMAT_R16G16B16A16_FLOAT, &bridge.output, &shared.output)
                    && (!guides
                        || (make(DXGI_FORMAT_R8G8B8A8_UNORM, &bridge.diffuse_albedo, &shared.diffuse_albedo)
                            && make(DXGI_FORMAT_R8G8B8A8_UNORM, &bridge.specular_albedo, &shared.specular_albedo)
                            && make(DXGI_FORMAT_R16G16B16A16_FLOAT, &bridge.normals, &shared.normals)
                            && make(DXGI_FORMAT_R8_UNORM, &bridge.roughness, &shared.roughness)))
                    && (!ray_tracing
                        || (make(DXGI_FORMAT_R32G32B32A32_FLOAT, &bridge.rt_vertices, &shared.rt_vertices, RT_TRANSFER_WIDTH, RT_TRANSFER_HEIGHT)
                            && make(DXGI_FORMAT_R32G32B32A32_FLOAT, &bridge.rt_positions, &shared.rt_positions)
                            && make(DXGI_FORMAT_R32G32B32A32_FLOAT, &bridge.rt_sun, &shared.rt_sun, 2, 1)
                            && make(DXGI_FORMAT_R8_UNORM, &bridge.rt_visibility, &shared.rt_visibility)
                            && make(DXGI_FORMAT_R8_UNORM, &bridge.rt_ambient, &shared.rt_ambient)
                            && make(DXGI_FORMAT_R11G11B10_FLOAT, &bridge.rt_light, &shared.rt_light)
                            && make(DXGI_FORMAT_R16G16B16A16_FLOAT, &bridge.rt_bounce, &shared.rt_bounce) && share_atlas()))
                    && (!reflections
                        || (make(DXGI_FORMAT_R16G16B16A16_FLOAT, &bridge.rt_reflection, &shared.rt_reflection)
                            && make(DXGI_FORMAT_R8_UNORM, &bridge.rt_specular, &shared.rt_specular)))
                    && (fg_format == DXGI_FORMAT_UNKNOWN
                        || (make_frame(&bridge.fg_frame, &shared.fg_frame) && make_frame(&bridge.fg_hudless, &shared.fg_hudless)
                            && SUCCEEDED(device->CreateRenderTargetView(bridge.fg_hudless.Get(), nullptr,
                                                                        bridge.fg_hudless_rtv.GetAddressOf()))))
                    && SUCCEEDED(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(bridge.fence.GetAddressOf())))
                    && SUCCEEDED(bridge.fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle));
  if (!made) return internal::Fail("DLAA: the shared textures could not be made.");
  shared.fence = internal::ShareWithHelper(fence_handle);
  bridge.value = 0;
  if (!guides) {
    shared.diffuse_albedo = 0;
    shared.specular_albedo = 0;
    shared.normals = 0;
    shared.roughness = 0;
  }
  if (!ray_tracing) {
    shared.rt_vertices = 0;
    shared.rt_positions = 0;
    shared.rt_sun = 0;
    shared.rt_visibility = 0;
    shared.rt_ambient = 0;
    shared.rt_atlas = 0;
    shared.rt_light = 0;
    shared.rt_bounce = 0;
  }
  if (!reflections) {
    shared.rt_reflection = 0;
    shared.rt_specular = 0;
  }
  if (fg_format == DXGI_FORMAT_UNKNOWN) {
    shared.fg_frame = 0;
    shared.fg_hudless = 0;
  }
  shared.fg_format = static_cast<uint32_t>(fg_format);
  shared.fg_failed = 0;
  ++shared.generation;
  bridge.width = width;
  bridge.height = height;
  bridge.guides = guides;
  bridge.ray_tracing = ray_tracing;
  bridge.fg_format = fg_format;
  bridge.reflections = reflections;
  return true;
}

namespace internal {

// One request (its fields written by `fill` after the fence's signal) and the helper's answer; false: none (it is not
// ready, or did not answer: off). The game's GPU then waits for the helper's value when `waited` and it ran.
template <typename Fill>
bool Request(ID3D11DeviceContext* context, Fill&& fill) {
  if (bridge.failed || bridge.width == 0) return false;
  auto& shared = *bridge.shared;
  if (shared.ready == 0) {
    if (WaitForSingleObject(bridge.process, 0) == WAIT_OBJECT_0) {
      return Fail(std::string("DLAA: NVIDIA's helper stopped (") + shared.status + "; renodx-tw-dlaa64.log next to the addon).");
    }
    return false;
  }
  ComPtr<ID3D11DeviceContext4> context4;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context4.GetAddressOf())))) {
    return Fail("DLAA: the game's context has no D3D11.4.");
  }
  ++bridge.value;
  context4->Signal(bridge.fence.Get(), bridge.value);
  context->Flush();
  shared.value = bridge.value;
  fill(&shared);
  ResetEvent(bridge.done);
  SetEvent(bridge.request);
  // It answers at once, but for a new NVIDIA feature (the first, or after a size, preset or Ray Reconstruction change).
  if (WaitForSingleObject(bridge.done, 5000) != WAIT_OBJECT_0 || shared.done_value < bridge.value) {
    return Fail("DLAA: NVIDIA's helper did not answer (off).");
  }
  bridge.answered = true;
  // The value it signalled after its work (one higher; also after a failure, so the next request starts above it).
  bridge.value = shared.done_value;
  if (shared.evaluated == 0) return false;
  context4->Wait(bridge.fence.Get(), bridge.value);
  return true;
}

}  // namespace internal

// This frame's ray tracing (its inputs written on `context` before this: the vertex texture's captures, the positions,
// the sun) in the helper into `bridge.rt_visibility` / `rt_ambient`, which the game's GPU then waits for; false: nothing
// traced this frame.
inline bool TraceScene(ID3D11DeviceContext* context, const TraceRequest& request) {
  if (!bridge.ray_tracing) return false;
  return internal::Request(context, [&](Shared* shared) {
    shared->fg_request = 0;
    shared->rt_request = 1;
    shared->rt_flags = request.flags;
    shared->rt_ambient_range = request.ambient_range;
    shared->rt_ambient_rays = request.ambient_rays;
    shared->rt_sun_softness = request.sun_softness;
    shared->rt_frame = request.frame;
    shared->rt_width = request.width;
    shared->rt_height = request.height;
    shared->rt_capture_count = static_cast<uint32_t>(std::min<size_t>(request.captures.size(), RT_CAPTURES));
    std::copy_n(request.captures.begin(), shared->rt_capture_count, shared->rt_captures);
    std::fill_n(shared->rt_capture_slices, shared->rt_capture_count, 0u);
    std::copy_n(request.slices.begin(), std::min<size_t>(request.slices.size(), shared->rt_capture_count), shared->rt_capture_slices);
    shared->rt_drawn_count = static_cast<uint32_t>(std::min<size_t>(request.drawn.size(), RT_DRAWN));
    std::copy_n(request.drawn.begin(), shared->rt_drawn_count, shared->rt_drawn);
  });
}

// The ray-traced Light Bounce (its prepared light copied into `bridge.rt_light` on `context` before this) in the helper
// into `bridge.rt_bounce`, which the game's GPU then waits for; false: not traced.
inline bool TraceBounce(ID3D11DeviceContext* context, const BounceRequest& request) {
  if (!bridge.ray_tracing || !bridge.rt_bounce.texture) return false;
  return internal::Request(context, [&](Shared* shared) {
    shared->fg_request = 0;
    shared->rt_request = 2;
    shared->rt_frame = request.frame;
    shared->rt_width = request.width;
    shared->rt_height = request.height;
    shared->rt_bounce_width = request.bounce_width;
    shared->rt_bounce_height = request.bounce_height;
    shared->rt_bounce_scale = request.scale;
    shared->rt_bounce_rays = request.rays;
    shared->rt_bounce_range = request.range;
    std::copy_n(request.view_projection.begin(), 16, shared->rt_view_projection);
    shared->rt_path_flags = request.path_flags;
    shared->rt_path_bounces = request.path_bounces;
    shared->rt_path_samples = request.path_samples;
    shared->rt_guides = (request.guides && bridge.guides) ? 1u : 0u;
  });
}

// The reflections and specular occlusion (v8; the guides written on `context` before this) in the helper into
// `bridge.rt_reflection` / `rt_specular`, which the game's GPU then waits for; false: not traced.
inline bool TraceReflections(ID3D11DeviceContext* context, const ReflectRequest& request) {
  if (!bridge.ray_tracing || !bridge.rt_reflection.texture) return false;
  return internal::Request(context, [&](Shared* shared) {
    shared->fg_request = 0;
    shared->rt_request = 3;
    shared->rt_frame = request.frame;
    shared->rt_width = request.width;
    shared->rt_height = request.height;
    shared->rt_path_flags = request.flags;
    shared->rt_path_bounces = request.bounces;
    shared->rt_path_samples = request.samples;
    shared->rt_bounce_range = request.range;
    shared->rt_reflection_range = request.reflection_range;
    std::copy_n(request.view_projection.begin(), 16, shared->rt_view_projection);
    shared->rt_guides = (request.guides && bridge.guides) ? 1u : 0u;
  });
}

// The helper's last ray tracing status (one line).
inline std::string TraceStatus() {
  return (bridge.shared != nullptr) ? std::string(bridge.shared->rt_status) : std::string();
}

// The inputs (written on `context` before this; their top-left `input_width` x `input_height`) through NVIDIA's DLAA
// (or DLSS, when that is below the textures' size; Ray Reconstruction with `ray_reconstruction` and the guides) into
// `bridge.output`, which the game's GPU then waits for; false: no result this frame (helper not ready, or no answer).
inline bool Evaluate(ID3D11DeviceContext* context, UINT input_width, UINT input_height, float jitter_x, float jitter_y,
                     bool reset, uint32_t preset, const RayReconstruction* ray_reconstruction = nullptr) {
  const bool evaluated = internal::Request(context, [&](Shared* shared) {
    shared->fg_request = 0;
    shared->rt_request = 0;
    shared->width = input_width;
    shared->height = input_height;
    shared->output_width = bridge.width;
    shared->output_height = bridge.height;
    shared->jitter_x = jitter_x;
    shared->jitter_y = jitter_y;
    shared->reset = (reset ? 1u : 0u);
    shared->preset = preset;
    shared->ray_reconstruction = (ray_reconstruction != nullptr && bridge.guides) ? 1u : 0u;
    if (shared->ray_reconstruction != 0) {
      shared->ray_reconstruction_preset = ray_reconstruction->preset;
      std::copy_n(ray_reconstruction->world_to_view.begin(), 16, shared->world_to_view);
      std::copy_n(ray_reconstruction->view_to_clip.begin(), 16, shared->view_to_clip);
    }
  });
  if (!bridge.failed && bridge.answered) bridge.status = bridge.shared->status;
  bridge.reconstructing = evaluated && ray_reconstruction != nullptr && bridge.guides;
  if (bridge.shared != nullptr) {
    // Ray tracing's counts change every frame: its line is logged when the part before ": " changes, else each minute.
    const std::string_view ray_tracing = bridge.shared->rt_status;
    const std::array<std::string_view, 3> lines = {bridge.status, bridge.shared->fg_status,
                                                   ray_tracing.substr(0, ray_tracing.find(": "))};
    const uint64_t now = GetTickCount64();
    for (size_t index = 0; index < lines.size(); ++index) {
      if (bridge.logged[index] == lines[index] && (index != 2 || lines[index].empty() || now < bridge.logged_time + 60000)) continue;
      bridge.logged[index] = lines[index];
      if (index == 2) bridge.logged_time = now;
      if (!lines[index].empty()) renodx::utils::log::i("[Total War] ", (index == 2 ? ray_tracing : lines[index]));
    }
  }
  return evaluated;
}

// A present through frame generation (the frame and HUD-less frame written on `context` before this): the helper copies
// the frame and presents it into its window over the game's, which the game's GPU waits for; false: not presented
// (helper not ready, off, or frame generation unavailable: FrameGenerationFailed).
inline bool PresentFrame(ID3D11DeviceContext* context, const FrameRequest& request) {
  if (bridge.fg_format == DXGI_FORMAT_UNKNOWN) return false;
  return internal::Request(context, [&](Shared* shared) {
    shared->rt_request = 0;
    shared->fg_request = 1;
    shared->fg_window = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(request.window));
    shared->fg_multiplier = request.multiplier;
    shared->fg_reflex_boost = (request.reflex_boost ? 1u : 0u);
    shared->fg_frame_limit = request.frame_limit;
    shared->fg_hudless_valid = (request.hudless ? 1u : 0u);
    shared->fg_inputs = (request.inputs ? 1u : 0u);
    shared->fg_frame_index = request.frame_index;
    shared->width = request.width;
    shared->height = request.height;
    shared->jitter_x = request.jitter_x;
    shared->jitter_y = request.jitter_y;
    shared->reset = (request.reset ? 1u : 0u);
    std::copy_n(request.view_to_clip.begin(), 16, shared->fg_view_to_clip);
    std::copy_n(request.view_projection.begin(), 16, shared->fg_view_projection);
    std::copy_n(request.previous_view_projection.begin(), 16, shared->fg_previous_view_projection);
    std::copy_n(request.camera.begin(), 12, shared->fg_camera);
    shared->fg_near = request.near_plane;
    shared->fg_far = request.far_plane;
  });
}

// Whether the helper found frame generation unavailable here (Streamline missing, no RTX 40, ...).
inline bool FrameGenerationFailed() {
  return bridge.shared != nullptr && bridge.shared->fg_failed != 0;
}

// The helper's last frame generation status (one line).
inline std::string FrameGenerationStatus() {
  return (bridge.shared != nullptr) ? std::string(bridge.shared->fg_status) : std::string();
}

// The game's device is going away: the shared objects with it (the helper reopens the next ones).
inline void Release() {
  bridge.color = {};
  bridge.depth = {};
  bridge.motion = {};
  bridge.output = {};
  bridge.diffuse_albedo = {};
  bridge.specular_albedo = {};
  bridge.normals = {};
  bridge.roughness = {};
  bridge.rt_vertices = {};
  bridge.rt_positions = {};
  bridge.rt_sun = {};
  bridge.rt_visibility = {};
  bridge.rt_ambient = {};
  bridge.rt_light = {};
  bridge.rt_bounce = {};
  bridge.rt_reflection = {};
  bridge.rt_specular = {};
  bridge.rt_atlas = {};
  bridge.fg_frame.Reset();
  bridge.fg_hudless.Reset();
  bridge.fg_hudless_rtv.Reset();
  bridge.fg_format = DXGI_FORMAT_UNKNOWN;
  bridge.fence.Reset();
  bridge.device.Reset();
  bridge.width = 0;
}

}  // namespace twattila::dlaa_bridge
