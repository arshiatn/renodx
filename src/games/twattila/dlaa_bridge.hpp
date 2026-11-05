/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <format>
#include <mutex>
#include <thread>
#include <string_view>
#include <span>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./dlaa_bridge.h"

// The addon's side of the DLAA bridge (dlaa_bridge.h): on first use the 64-bit helper (renodx-tw-dlaa64.exe next to
// the addon) is started in a job that ends it with the game; the inputs (this frame linear, its depth and motion), the
// output and a fence are made shareable and their handles duplicated into it. Each frame: the fence signalled at the
// inputs, one request, and the helper's answer awaited on the CPU (it only submits its work, well under a millisecond);
// only then does the game's GPU wait for the helper's fence value, so a missing or late helper never stalls the GPU
// (no answer in 100 ms, 5 s for the first while NVIDIA sets up: DLAA off). With Ray Reconstruction its guides too
// (written by the addon's G-buffer pass). With ray tracing (rt_scene.hpp) its textures too, and a request of its own
// each frame (TraceScene). With frame generation (frame_generation.hpp) the presented frame and the HUD-less frame in
// fg_format too, and a request at each present (PresentFrame). D3D11.4 (shared fences).
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
  uint32_t kept_frames = 0;  // Shared::rt_kept_frames
  float ambient_range = 2.f;
  uint32_t ambient_rays = 2;
  float sun_softness = 0.f;  // degrees of the sun's disk (0: hard shadows)
  uint32_t frame = 0;
  uint32_t width = 0;  // the render size (the textures' top-left)
  uint32_t height = 0;
  std::span<const RtCapture> captures;
  std::span<const uint32_t> slices;  // as the captures
  std::span<const uint64_t> drawn;
  // v9, with RT_LOCAL_LIGHTS: the local lights (world position, reach in metres; RT_MAX_LIGHTS at most).
  std::span<const std::array<float, 4>> lights;
  std::span<const std::array<float, 4>> light_colours;  // v11: as `lights`
  std::array<float, 16> view_projection = {};            // v11: the camera's registers (the ambient's history)
  std::span<const RtPlacement> placements;               // v14: the known placed draws (RT_PLACEMENTS at most)
  // v16: the compact draws and the cluster sets new to the helper, each one's data as `new_set_data` (copied into the
  // second mapping, the offsets packed there; once the helper answered the last request it no longer reads it).
  std::span<const RtCompactDraw> compact;
  std::span<const RtClusterSet> new_sets;
  std::span<const std::span<const uint8_t>> new_set_data;
  float light_size = 0.f;     // v18: the local lights' radius (metres) for soft shadows under Ray Reconstruction
  uint32_t shadow_rays = 1;   // v18: rays a pixel toward the sun and each light
  float sway_range = 0.f;     // v18: swaying copies within these metres of the camera (0: everywhere)
  float grass_shadow_range = 0.f;  // v18: pixels farther (metres) trace without grass (0: every pixel with it)
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
  uint32_t preset = 0;       // NVIDIA's A-C as 1-3 (0: the driver's)
  bool hud_recomposition = false;
  uint32_t debug_view = 0;  // 1 the HUD-less image, 2 the UI layer tinted (0: off)
  bool sdr_output = false;
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
  uint32_t extra_written = 0;  // bits of RR_EXTRA_*: written for this evaluation
  std::array<float, 16> world_to_view = {};
  std::array<float, 16> view_to_clip = {};
};

// The requests' kinds (each steady or not on its own).
enum RequestKind : size_t { EVALUATE, TRACE_SCENE, TRACE_BOUNCE, TRACE_REFLECTIONS, PRESENT_FRAME, REQUEST_KINDS };

struct Bridge {
  // The last evaluation ran Ray Reconstruction: the addon's own denoisers (Light Bounce, AO filter, the reflections'
  // accumulation and filter) leave its noise to it.
  bool reconstructing = false;
  // The colour last written into `color` is the light's square root (the upscaler's input for L, M, Ray Reconstruction): the
  // helper's paths square it back (RT_PATH_COLOR_SQUARE_ROOT).
  bool color_square_root = false;
  // The Denoiser setting at NRD: which signals are asked for it, and which the helper denoises by it (asked, and it has
  // not said otherwise: rt_nrd_off), the addon's own denoisers off those. Under Ray Reconstruction only the AO, with its row
  // at REBLUR: RT_AO_REBLUR for RTAO, and Path-Traced AO's Light Bounce is asked for it (rt_core.hpp).
  bool nrd_ask_bounce = false;
  bool nrd_ask_reflections = false;
  bool nrd_ambient = false;
  bool nrd_bounce = false;
  bool nrd_reflections = false;
  uint32_t upscaler = UPSCALER_DLSS;
  uint32_t fg_backend = FG_BACKEND_DLSS;
  HANDLE mapping = nullptr;
  HANDLE request = nullptr;
  HANDLE done = nullptr;
  HANDLE job = nullptr;
  HANDLE process = nullptr;
  Shared* shared = nullptr;
  // The cluster sets' data (v16): the second mapping, RT_CLUSTER_DATA_BYTES.
  HANDLE cluster_mapping = nullptr;
  uint8_t* cluster_data = nullptr;
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
  Texture rt_light_mask;  // v9: the local lights' lit bits
  Texture rt_fog;         // v11: the fog shadows (a quarter of the size each way)
  Texture rt_fog_lights;  // v18: the local lights' light in the fog (as rt_fog)
  Texture rt_tint;        // v19: Coloured Shadows' filter (as rt_visibility)
  // The reflections' (v8, made when they are on): the reflected light and the specular occlusion out; Ray
  // Reconstruction's specular hit distance (v17), written by this frame's reflections (`hit_distance_written`).
  Texture rt_reflection;
  Texture rt_specular;
  Texture rr_hit_distance;
  bool hit_distance_written = false;
  // Ray Reconstruction's optional inputs (v25), the bits of `rr_extra` that asked for them.
  std::array<Texture, RR_EXTRA_INPUTS> rr_extra_textures;
  uint32_t rr_extra = 0;
  bool reflections = false;
  // The leaves' opacity masks (an RT_ATLAS_SLICES texture array, made with the first ray tracing textures and kept
  // across sizes: shared with the helper again with every set; `rt_atlas_generation` counts the arrays made).
  Texture rt_atlas;
  uint32_t rt_atlas_generation = 0;
  bool ray_tracing = false;
  bool details = false;  // Log Details (RT_DETAILS)
  // Frame generation's (made when it is on): the presented frame and the HUD-less frame, in fg_format (the swap chain's;
  // HDR10 for an scRGB one).
  ComPtr<ID3D11Texture2D> fg_frame;
  ComPtr<ID3D11RenderTargetView> fg_frame_rtv;
  ComPtr<ID3D11Texture2D> fg_hudless;
  ComPtr<ID3D11RenderTargetView> fg_hudless_rtv;
  DXGI_FORMAT fg_format = DXGI_FORMAT_UNKNOWN;
  ComPtr<ID3D11Fence> fence;
  uint64_t value = 0;
  bool answered = false;  // the helper's first answer came (NVIDIA's set-up: up to seconds)
  // Steady requests (their kind ran last time with this fence; not frame generation's presents) go without waiting for
  // the helper's work (~0.25 ms of the game's frame): the game's GPU waits for the helper's next value at once and the
  // answer is read with the next request (`pending`). `waiting`: that value while unanswered (0: none).
  std::array<bool, REQUEST_KINDS> steady = {};
  bool pending = false;
  RequestKind pending_kind = EVALUATE;
  // The game's thread waiting for the helper's answers so far (QueryPerformanceCounter ticks; the ray tracing profile).
  int64_t answer_ticks = 0;
  std::atomic<uint64_t> waiting = 0;
  // The helper's value the game's GPU has yet to wait for (0: none): a frame generation present's (its copies of the
  // frame, depth and motion) or an unevaluated request's, waited for before the next request's signal or the next
  // writes into the shared textures (BeforeSharedWrites) instead of stalling the game's GPU right after the request.
  uint64_t deferred = 0;
  // If the helper stops while the game's GPU waits for it, the value is signalled through this handle of `fence` from a
  // device of the addon's own (Rescue).
  std::mutex rescue_mutex;
  HANDLE rescue_fence = nullptr;
};

static Bridge bridge;

// The game's GPU's PCI vendor (the addon's first present; 0: not known yet, or a game that does not ask).
inline std::atomic<uint32_t> gpu_vendor = 0;

// DLSS, Ray Reconstruction and Streamline's frame generation start on NVIDIA's GPUs only.
inline bool OnNvidia() {
  const uint32_t vendor = gpu_vendor.load(std::memory_order_relaxed);
  return vendor == 0 || vendor == 0x10DE;
}

// The game's GPU's vendor from its device (ReShade's proxy or not), once. RENODX_TW_FAKE_VENDOR (tests): another's.
inline void ReadGpuVendor(ID3D11Device* device) {
  if (gpu_vendor.load(std::memory_order_relaxed) != 0) return;
  renodx::utils::directx::NativeFromReShadeProxy(&device);
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  if (DXGI_ADAPTER_DESC description = {}; device != nullptr && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf())))
                                          && SUCCEEDED(dxgi_device->GetAdapter(adapter.GetAddressOf())) && SUCCEEDED(adapter->GetDesc(&description))) {
    gpu_vendor.store(description.VendorId);
    std::array<char, 16> fake = {};
    if (GetEnvironmentVariableA("RENODX_TW_FAKE_VENDOR", fake.data(), static_cast<DWORD>(fake.size())) != 0) {
      gpu_vendor.store(std::strtoul(fake.data(), nullptr, 16));
    }
    renodx::utils::log::i("[Total War] GPU vendor ", std::format("0x{:04X}", gpu_vendor.load()));
  }
}

namespace internal {

// renodx-tw-dlaa64.exe next to this addon.
inline std::wstring HelperPath() {
  HMODULE addon = nullptr;
  static const int ANCHOR = 0;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&ANCHOR), &addon);
  std::wstring path(MAX_PATH, L'\0');
  path.resize(GetModuleFileNameW(addon, path.data(), MAX_PATH));
  return path.substr(0, path.find_last_of(L"\\/") + 1) + L"renodx-tw-dlaa64.exe";
}

inline bool Fail(const std::string& status) {
  bridge.failed = true;
  bridge.status = status;
  renodx::utils::log::w("[Total War] ", status);
  return false;
}

// The game's GPU waits for `value`, which the helper will not signal (it stopped or did not answer): signalled from a
// device of the addon's own on the same GPU (the game goes on, without DLAA).
inline void Rescue(uint64_t value) {
  const std::unique_lock lock(bridge.rescue_mutex);
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11Device5> device5;
  ComPtr<ID3D11DeviceContext4> context4;
  ComPtr<ID3D11Fence> fence;
  const LUID luid = {.LowPart = bridge.shared->adapter_luid_low, .HighPart = bridge.shared->adapter_luid_high};
  if (bridge.rescue_fence == nullptr || !renodx::utils::directx::Initialize()
      || FAILED(renodx::utils::directx::pCreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))
      || FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(adapter.GetAddressOf())))
      || FAILED(renodx::utils::directx::pD3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                                           D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                                           context.GetAddressOf()))
      || FAILED(device.As(&device5)) || FAILED(context.As(&context4))
      || FAILED(device5->OpenSharedFence(bridge.rescue_fence, IID_PPV_ARGS(fence.GetAddressOf())))) {
    renodx::utils::log::w("[Total War] DLAA: the helper stopped while the game's GPU waited for it (not freed).");
    return;
  }
  context4->Signal(fence.Get(), value);
  context->Flush();
  // Kept until the GPU signalled it.
  if (const HANDLE signalled = CreateEventW(nullptr, FALSE, FALSE, nullptr); signalled != nullptr) {
    if (SUCCEEDED(fence->SetEventOnCompletion(value, signalled))) WaitForSingleObject(signalled, 2000);
    CloseHandle(signalled);
  }
  renodx::utils::log::w("[Total War] DLAA: the helper stopped while the game's GPU waited for it (freed).");
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
  // The cluster sets' (compact draws without it: none).
  bridge.cluster_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, RT_CLUSTER_DATA_BYTES, (name + L"_clusters").c_str());
  if (bridge.cluster_mapping != nullptr) {
    bridge.cluster_data = static_cast<uint8_t*>(MapViewOfFile(bridge.cluster_mapping, FILE_MAP_ALL_ACCESS, 0, 0, RT_CLUSTER_DATA_BYTES));
  }
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC adapter_desc = {};
  if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf()))) && SUCCEEDED(dxgi_device->GetAdapter(adapter.GetAddressOf()))
      && SUCCEEDED(adapter->GetDesc(&adapter_desc))) {
    bridge.shared->adapter_luid_low = adapter_desc.AdapterLuid.LowPart;
    bridge.shared->adapter_luid_high = adapter_desc.AdapterLuid.HighPart;
  }

  const std::wstring path = internal::HelperPath();
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
  // Its stop while the game's GPU waits for it (a steady request): Rescue.
  std::thread([process = information.hProcess] {
    WaitForSingleObject(process, INFINITE);
    if (const uint64_t value = bridge.waiting.exchange(0); value != 0) Rescue(value);
  }).detach();
  bridge.status = "DLAA: starting the helper";
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

// The last request's answer (once the helper read it and signalled its value; a steady one's usually long there),
// waited for `timeout` ms at most; false: none (it stopped or did not answer: off, and a value the game's GPU waits for
// signalled from here).
inline bool Answer(DWORD timeout = 5000) {
  bridge.pending = false;
  const auto& shared = *bridge.shared;
  const std::array<HANDLE, 2> handles = {bridge.done, bridge.process};
  LARGE_INTEGER started;
  LARGE_INTEGER ended;
  QueryPerformanceCounter(&started);
  const bool answered = WaitForMultipleObjects(2, handles.data(), FALSE, timeout) == WAIT_OBJECT_0
                        && shared.done_value >= bridge.value;
  QueryPerformanceCounter(&ended);
  bridge.answer_ticks += ended.QuadPart - started.QuadPart;
  if (const uint64_t waited = bridge.waiting.exchange(0); waited > (answered ? shared.done_value : 0)) Rescue(waited);
  if (!answered) return Fail("DLAA: the helper did not answer (off).");
  bridge.answered = true;
  // The value it signalled after its work (one higher; also after a failure, so the next request starts above it).
  bridge.value = shared.done_value;
  bridge.status = shared.status;
  return true;
}

// The handles of what a set does not have, zero for the helper.
inline void ClearHandles(Shared* shared, bool guides, bool ray_tracing, bool reflections, bool frames) {
  if (!guides) {
    shared->diffuse_albedo = 0;
    shared->specular_albedo = 0;
    shared->normals = 0;
    shared->roughness = 0;
    std::ranges::fill(shared->rr_extra, 0u);
  }
  if (!ray_tracing) {
    shared->rt_vertices = 0;
    shared->rt_positions = 0;
    shared->rt_sun = 0;
    shared->rt_visibility = 0;
    shared->rt_ambient = 0;
    shared->rt_atlas = 0;
    shared->rt_light = 0;
    shared->rt_bounce = 0;
    shared->rt_light_mask = 0;
    shared->rt_fog = 0;
    shared->rt_fog_lights = 0;
    shared->rt_tint = 0;
  }
  if (!reflections) {
    shared->rt_reflection = 0;
    shared->rt_specular = 0;
    shared->rr_hit_distance = 0;
  }
  if (!frames) {
    shared->fg_frame = 0;
    shared->fg_hudless = 0;
  }
}

}  // namespace internal

// The shared objects for `width` x `height` on `device` (with Ray Reconstruction's guides when `guides`, ray tracing's
// textures when `ray_tracing`, frame generation's in `fg_format` unless unknown, the reflections' with ray tracing when
// `reflections`, `upscaler` the helper's UPSCALER_*, `fg_backend` its FG_BACKEND_*; made again when any changes); false: not
// available.
inline bool Prepare(ID3D11Device* native_device, UINT width, UINT height, bool guides = false, bool ray_tracing = false,
                    DXGI_FORMAT fg_format = DXGI_FORMAT_UNKNOWN, bool reflections = false, uint32_t upscaler = UPSCALER_DLSS,
                    uint32_t fg_backend = FG_BACKEND_DLSS, uint32_t rr_extra = 0u) {
  reflections = reflections && ray_tracing;
  rr_extra = guides ? rr_extra : 0u;
  if (!internal::Start(native_device)) return false;
  ComPtr<ID3D11Device5> device;
  if (FAILED(native_device->QueryInterface(IID_PPV_ARGS(device.GetAddressOf())))) {
    return internal::Fail("DLAA: the game's device has no D3D11.4 (shared fences).");
  }
  // A GPU reset that removed one of the helper's devices leaves the shared fence at all ones: every wait on it passes at
  // once and nothing is in order (ATN 2026-10-29: Ray Reconstruction and frame generation broken after a freeze), so all
  // are made again with a new fence.
  if (bridge.device == device && bridge.width == width && bridge.height == height && bridge.guides == guides
      && bridge.ray_tracing == ray_tracing && bridge.fg_format == fg_format && bridge.reflections == reflections && bridge.upscaler == upscaler
      && bridge.fg_backend == fg_backend && bridge.rr_extra == rr_extra
      && (!bridge.fence || bridge.fence->GetCompletedValue() != UINT64_MAX)) {
    return true;
  }
  // The helper done with the last request before `shared` changes; the new objects' first requests wait for answers.
  if (bridge.pending) internal::Answer();
  bridge.steady = {};
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
  bridge.rt_light_mask = {};
  bridge.rt_fog = {};
  bridge.rt_fog_lights = {};
  bridge.rt_tint = {};
  bridge.rt_reflection = {};
  bridge.rt_specular = {};
  bridge.rr_hit_distance = {};
  bridge.rr_extra_textures = {};
  bridge.hit_distance_written = false;
  bridge.fg_frame.Reset();
  bridge.fg_hudless.Reset();
  bridge.fg_frame_rtv.Reset();
  bridge.fg_hudless_rtv.Reset();
  bridge.fg_format = DXGI_FORMAT_UNKNOWN;
  bridge.fence.Reset();
  bridge.deferred = 0;
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
  const auto make_extra = [&] {
    static constexpr std::array<DXGI_FORMAT, RR_EXTRA_INPUTS> FORMATS = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                                         DXGI_FORMAT_R16_FLOAT};
    std::ranges::fill(shared.rr_extra, 0u);
    for (size_t index = 0; index < RR_EXTRA_INPUTS; ++index) {
      if ((rr_extra & (1u << index)) != 0u && !make(FORMATS[index], &bridge.rr_extra_textures[index], &shared.rr_extra[index])) return false;
    }
    return true;
  };
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
                            && make(DXGI_FORMAT_R32G32B32A32_FLOAT, &bridge.rt_sun, &shared.rt_sun, 16, 1)
                            && make(DXGI_FORMAT_R8_UNORM, &bridge.rt_visibility, &shared.rt_visibility)
                            && make(DXGI_FORMAT_R8_UNORM, &bridge.rt_ambient, &shared.rt_ambient)
                            && make(DXGI_FORMAT_R11G11B10_FLOAT, &bridge.rt_light, &shared.rt_light)
                            && make(DXGI_FORMAT_R16G16B16A16_FLOAT, &bridge.rt_bounce, &shared.rt_bounce)
                            && make(DXGI_FORMAT_R32_UINT, &bridge.rt_light_mask, &shared.rt_light_mask)
                            && make(DXGI_FORMAT_R8_UNORM, &bridge.rt_fog, &shared.rt_fog, (width + 3) / 4, (height + 3) / 4)
                            && make(DXGI_FORMAT_R16G16B16A16_FLOAT, &bridge.rt_fog_lights, &shared.rt_fog_lights, (width + 3) / 4, (height + 3) / 4)
                            && make(DXGI_FORMAT_R8G8B8A8_UNORM, &bridge.rt_tint, &shared.rt_tint)
                            && share_atlas()))
                    && (!reflections
                        || (make(DXGI_FORMAT_R16G16B16A16_FLOAT, &bridge.rt_reflection, &shared.rt_reflection)
                            && make(DXGI_FORMAT_R8_UNORM, &bridge.rt_specular, &shared.rt_specular)
                            && make(DXGI_FORMAT_R16_FLOAT, &bridge.rr_hit_distance, &shared.rr_hit_distance)))
                    && make_extra()
                    && (fg_format == DXGI_FORMAT_UNKNOWN
                        || (make_frame(&bridge.fg_frame, &shared.fg_frame) && make_frame(&bridge.fg_hudless, &shared.fg_hudless)
                            && SUCCEEDED(device->CreateRenderTargetView(bridge.fg_frame.Get(), nullptr, bridge.fg_frame_rtv.GetAddressOf()))
                            && SUCCEEDED(device->CreateRenderTargetView(bridge.fg_hudless.Get(), nullptr,
                                                                        bridge.fg_hudless_rtv.GetAddressOf()))))
                    && SUCCEEDED(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(bridge.fence.GetAddressOf())))
                    && SUCCEEDED(bridge.fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle));
  if (!made) return internal::Fail("DLAA: the shared textures could not be made.");
  shared.fence = internal::ShareWithHelper(fence_handle);
  {
    const std::unique_lock lock(bridge.rescue_mutex);
    if (bridge.rescue_fence != nullptr) CloseHandle(bridge.rescue_fence);
    bridge.rescue_fence = nullptr;
    bridge.fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &bridge.rescue_fence);
  }
  bridge.value = 0;
  internal::ClearHandles(&shared, guides, ray_tracing, reflections, fg_format != DXGI_FORMAT_UNKNOWN);
  shared.fg_format = static_cast<uint32_t>(fg_format);
  shared.fg_failed = 0;
  shared.rt_flags = (bridge.details ? RT_DETAILS : 0u);
  shared.upscaler = upscaler;
  shared.fg_backend = fg_backend;
  ++shared.generation;
  bridge.width = width;
  bridge.height = height;
  bridge.guides = guides;
  bridge.ray_tracing = ray_tracing;
  bridge.fg_format = fg_format;
  bridge.reflections = reflections;
  bridge.upscaler = upscaler;
  bridge.rr_extra = rr_extra;
  bridge.fg_backend = fg_backend;
  return true;
}

namespace internal {

// One request (its fields written by `fill` after the fence's signal) and the game's GPU then waiting for the helper's
// value; false: none (it is not ready, did not run, or did not answer: off). A steady one returns at once (it ran last
// time); the others wait for the helper's answer (at once, but for a new NVIDIA feature: the first, or after a size,
// preset or Ray Reconstruction change).
template <typename Fill>
bool Request(ID3D11DeviceContext* context, RequestKind kind, Fill&& fill) {
  if (bridge.failed || bridge.width == 0) return false;
  auto& shared = *bridge.shared;
  if (shared.ready == 0) {
    if (WaitForSingleObject(bridge.process, 0) == WAIT_OBJECT_0) {
      return Fail(std::string("DLAA: the helper stopped (") + shared.status + "; renodx-tw-dlaa64.log next to the addon).");
    }
    return false;
  }
  ComPtr<ID3D11DeviceContext4> context4;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context4.GetAddressOf())))) {
    return Fail("DLAA: the game's context has no D3D11.4.");
  }
  if (bridge.pending) {
    const RequestKind last = bridge.pending_kind;
    if (!Answer()) return false;
    bridge.steady[last] = shared.evaluated != 0 && shared.done_value == shared.value + 1;
  }
  if (bridge.deferred != 0) context4->Wait(bridge.fence.Get(), std::exchange(bridge.deferred, 0));
  ++bridge.value;
  context4->Signal(bridge.fence.Get(), bridge.value);
  context->Flush();
  shared.value = bridge.value;
  fill(&shared);
  ResetEvent(bridge.done);
  const bool steady = bridge.steady[kind] && kind != PRESENT_FRAME;
  if (steady) bridge.waiting.store(bridge.value + 1);
  SetEvent(bridge.request);
  if (steady) {
    bridge.pending = true;
    bridge.pending_kind = kind;
    context4->Wait(bridge.fence.Get(), bridge.value + 1);
    return true;
  }
  // Waited for: the first after new shared objects may start NVIDIA's features (Streamline and DLSS-G: seconds when cold).
  if (!Answer(20000)) return false;
  bridge.steady[kind] = shared.evaluated != 0 && shared.done_value == shared.value + 1;
  // Nothing of the helper's to read after a present or without a result: its value is waited for later (`deferred`).
  if (shared.evaluated == 0 || kind == PRESENT_FRAME) {
    bridge.deferred = bridge.value;
    return shared.evaluated != 0;
  }
  context4->Wait(bridge.fence.Get(), bridge.value);
  return true;
}

}  // namespace internal

// Before the game's GPU writes the shared textures (the DLAA inputs, the frame generation frames) on `context`: the
// helper's reads of the last ones done.
inline void BeforeSharedWrites(ID3D11DeviceContext* context) {
  if (bridge.deferred == 0) return;
  // A helper that stopped may never signal it.
  if (bridge.failed) {
    bridge.deferred = 0;
    return;
  }
  ComPtr<ID3D11DeviceContext4> context4;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context4.GetAddressOf())))) return;
  context4->Wait(bridge.fence.Get(), std::exchange(bridge.deferred, 0));
}

// This frame's ray tracing (its inputs written on `context` before this: the vertex texture's captures, the positions,
// the sun) in the helper into `bridge.rt_visibility` / `rt_ambient`, which the game's GPU then waits for; false: nothing
// traced this frame.
inline bool TraceScene(ID3D11DeviceContext* context, const TraceRequest& request) {
  if (!bridge.ray_tracing) return false;
  return internal::Request(context, TRACE_SCENE, [&](Shared* shared) {
    shared->fg_request = 0;
    shared->rt_request = 1;
    shared->rt_flags = request.flags;
    shared->rt_kept_frames = request.kept_frames;
    shared->rt_ambient_range = request.ambient_range;
    shared->rt_ambient_rays = request.ambient_rays;
    shared->rt_sun_softness = request.sun_softness;
    shared->rt_light_size = request.light_size;
    shared->rt_shadow_rays = request.shadow_rays;
    shared->rt_sway_range = request.sway_range;
    shared->rt_grass_shadow_range = request.grass_shadow_range;
    shared->rt_frame = request.frame;
    shared->rt_width = request.width;
    shared->rt_height = request.height;
    shared->rt_capture_count = static_cast<uint32_t>(std::min<size_t>(request.captures.size(), RT_CAPTURES));
    std::copy_n(request.captures.begin(), shared->rt_capture_count, shared->rt_captures);
    std::fill_n(shared->rt_capture_slices, shared->rt_capture_count, 0u);
    std::copy_n(request.slices.begin(), std::min<size_t>(request.slices.size(), shared->rt_capture_count), shared->rt_capture_slices);
    shared->rt_drawn_count = static_cast<uint32_t>(std::min<size_t>(request.drawn.size(), RT_DRAWN));
    std::copy_n(request.drawn.begin(), shared->rt_drawn_count, shared->rt_drawn);
    shared->rt_light_count = static_cast<uint32_t>(std::min<size_t>(request.lights.size(), RT_MAX_LIGHTS));
    for (uint32_t light = 0; light < shared->rt_light_count; ++light) {
      std::copy_n(request.lights[light].begin(), 4, shared->rt_lights[light]);
      if (light < request.light_colours.size()) std::copy_n(request.light_colours[light].begin(), 4, shared->rt_light_colours[light]);
    }
    std::copy_n(request.view_projection.begin(), 16, shared->rt_view_projection);
    shared->rt_placement_count = static_cast<uint32_t>(std::min<size_t>(request.placements.size(), RT_PLACEMENTS));
    std::copy_n(request.placements.begin(), shared->rt_placement_count, shared->rt_placements);
    shared->rt_compact_count = static_cast<uint32_t>(std::min<size_t>(request.compact.size(), RT_COMPACT_DRAWS));
    std::copy_n(request.compact.begin(), shared->rt_compact_count, shared->rt_compact);
    shared->rt_new_set_count = 0;
    uint32_t offset = 0;
    for (size_t index = 0; index < std::min({request.new_sets.size(), request.new_set_data.size(), size_t{RT_NEW_SETS}}); ++index) {
      const auto data = request.new_set_data[index];
      if (bridge.cluster_data == nullptr || uint64_t{offset} + data.size() > RT_CLUSTER_DATA_BYTES) break;
      std::memcpy(bridge.cluster_data + offset, data.data(), data.size());
      shared->rt_new_sets[shared->rt_new_set_count] = request.new_sets[index];
      shared->rt_new_sets[shared->rt_new_set_count++].offset = offset;
      offset += static_cast<uint32_t>((data.size() + 15) / 16 * 16);
    }
  });
}

// The helper's compact draw state (v16, rt_compact_ok): 0 none taken, else it changes each time its sets were dropped
// (sent again as they are drawn).
inline uint32_t CompactState() {
  return (bridge.shared != nullptr && bridge.cluster_data != nullptr) ? bridge.shared->rt_compact_ok : 0u;
}

// The helper's alpha-tested dynamic corners a request (rt_dynamic_uvs; an older helper's: RT_DYNAMIC_UVS / 2).
inline uint32_t DynamicUvs() {
  return (bridge.shared != nullptr && bridge.shared->rt_dynamic_uvs != 0) ? std::min(bridge.shared->rt_dynamic_uvs, RT_DYNAMIC_UVS)
                                                                         : RT_DYNAMIC_UVS / 2;
}

// A mesh's triangles (`indices`, three a triangle) in Morton order of their centres (`corners`: its first instance's
// captured corners, three a triangle in index order; garbage ones last): a cluster set made from them has its clusters
// close in space. In index order a grass patch's clusters each spanned the whole patch (its blades in no order), every
// ray entering it went through all of them (the harness's field of 1000-blade patches at 4K: 0.35 ms of the trace in
// random order, 0.03 sorted).
inline void SortTriangles(std::span<uint32_t> indices, std::span<const std::array<float, 4>> corners) {
  const size_t triangles = std::min(indices.size(), corners.size()) / 3;
  if (triangles < 2) return;
  std::array<float, 3> low = {FLT_MAX, FLT_MAX, FLT_MAX};
  std::array<float, 3> high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
  for (size_t corner = 0; corner < triangles * 3; ++corner) {
    for (size_t axis = 0; axis < 3; ++axis) {
      if (!std::isfinite(corners[corner][axis])) continue;
      low[axis] = std::min(low[axis], corners[corner][axis]);
      high[axis] = std::max(high[axis], corners[corner][axis]);
    }
  }
  // 10 bits an axis of the centre in the mesh's box, interleaved.
  const auto spread = [](uint32_t value) {
    value = (value | (value << 16)) & 0x030000FFu;
    value = (value | (value << 8)) & 0x0300F00Fu;
    value = (value | (value << 4)) & 0x030C30C3u;
    return (value | (value << 2)) & 0x09249249u;
  };
  std::vector<std::pair<uint32_t, uint32_t>> order(triangles);
  for (size_t triangle = 0; triangle < triangles; ++triangle) {
    uint32_t code = 0;
    bool finite = true;
    for (size_t axis = 0; axis < 3; ++axis) {
      const float centre = (corners[triangle * 3][axis] + corners[triangle * 3 + 1][axis] + corners[triangle * 3 + 2][axis]) / 3.f;
      finite = finite && std::isfinite(centre);
      const float extent = high[axis] - low[axis];
      const float unit = (finite && extent > 0.f) ? std::clamp((centre - low[axis]) / extent, 0.f, 1.f) : 0.f;
      code |= spread(static_cast<uint32_t>(unit * 1023.f)) << axis;
    }
    order[triangle] = {finite ? code : UINT32_MAX, static_cast<uint32_t>(triangle)};
  }
  std::ranges::stable_sort(order, {}, &std::pair<uint32_t, uint32_t>::first);
  std::vector<uint32_t> sorted;
  sorted.reserve(triangles * 3);
  for (const auto& [code, triangle] : order) sorted.insert(sorted.end(), indices.begin() + (triangle * 3), indices.begin() + (triangle * 3) + 3);
  std::ranges::copy(sorted, indices.begin());
}

// A mesh's cluster set (v16) from its index list (three a triangle; degenerate ones left out): its triangles in order,
// in clusters of at most RT_CLUSTER_TRIANGLES over at most RT_CLUSTER_VERTICES vertices; `remap` the mesh vertex each
// texel of a block streams (each cluster's vertices in turn: a vertex in two clusters streams twice), `data` the set's
// data as RtClusterSet has it. Empty: none (indices past 2^24, garbage).
struct ClusterSetData {
  std::vector<uint32_t> remap;
  std::vector<uint8_t> data;
  uint32_t clusters = 0;
  uint32_t triangles = 0;
};
inline ClusterSetData MakeClusterSet(std::span<const uint32_t> indices) {
  ClusterSetData set;
  const uint32_t highest = indices.empty() ? 0u : *std::ranges::max_element(indices);
  if (indices.size() < 3 || highest >= (1u << 24)) return set;
  std::vector<std::array<uint32_t, 4>> clusters;  // first vertex in the block, vertices, first triangle, triangles
  std::vector<uint8_t> corners;
  // Each mesh vertex's cluster when it was last given a place in one, and that place; the cluster being filled.
  std::vector<uint32_t> cluster_of(size_t{highest} + 1, UINT32_MAX);
  std::vector<uint8_t> place(cluster_of.size());
  uint32_t current = UINT32_MAX;
  for (size_t at = 0; at + 2 < indices.size(); at += 3) {
    const std::array<uint32_t, 3> triangle = {indices[at], indices[at + 1], indices[at + 2]};
    if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2]) continue;
    const auto added = static_cast<uint32_t>(std::ranges::count_if(triangle, [&](uint32_t vertex) { return cluster_of[vertex] != current; }));
    if (current == UINT32_MAX || clusters[current][3] == RT_CLUSTER_TRIANGLES || clusters[current][1] + added > RT_CLUSTER_VERTICES) {
      current = static_cast<uint32_t>(clusters.size());
      clusters.push_back({static_cast<uint32_t>(set.remap.size()), 0, set.triangles, 0});
    }
    auto& cluster = clusters[current];
    for (const uint32_t vertex : triangle) {
      if (cluster_of[vertex] != current) {
        cluster_of[vertex] = current;
        place[vertex] = static_cast<uint8_t>(cluster[1]++);
        set.remap.push_back(vertex);
      }
      corners.push_back(place[vertex]);
    }
    ++cluster[3];
    ++set.triangles;
  }
  set.clusters = static_cast<uint32_t>(clusters.size());
  set.data.resize((clusters.size() * 16) + corners.size());
  std::memcpy(set.data.data(), clusters.data(), clusters.size() * 16);
  std::memcpy(set.data.data() + (clusters.size() * 16), corners.data(), corners.size());
  return set;
}

// The ray-traced Light Bounce (its prepared light copied into `bridge.rt_light` on `context` before this) in the helper
// into `bridge.rt_bounce`, which the game's GPU then waits for; false: not traced.
inline bool TraceBounce(ID3D11DeviceContext* context, const BounceRequest& request) {
  if (!bridge.ray_tracing || !bridge.rt_bounce.texture) return false;
  return internal::Request(context, TRACE_BOUNCE, [&](Shared* shared) {
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
    shared->rt_path_flags = request.path_flags | (bridge.color_square_root ? RT_PATH_COLOR_SQUARE_ROOT : 0u);
    shared->rt_path_bounces = request.path_bounces;
    shared->rt_path_samples = request.path_samples;
    shared->rt_guides = (request.guides && bridge.guides) ? 1u : 0u;
  });
}

// The reflections and specular occlusion (v8; the guides written on `context` before this) in the helper into
// `bridge.rt_reflection` / `rt_specular`, which the game's GPU then waits for; false: not traced.
inline bool TraceReflections(ID3D11DeviceContext* context, const ReflectRequest& request) {
  if (!bridge.ray_tracing || !bridge.rt_reflection.texture) return false;
  const bool traced = internal::Request(context, TRACE_REFLECTIONS, [&](Shared* shared) {
    shared->fg_request = 0;
    shared->rt_request = 3;
    shared->rt_frame = request.frame;
    shared->rt_width = request.width;
    shared->rt_height = request.height;
    shared->rt_path_flags = request.flags | (bridge.color_square_root ? RT_PATH_COLOR_SQUARE_ROOT : 0u);
    shared->rt_path_bounces = request.bounces;
    shared->rt_path_samples = request.samples;
    shared->rt_bounce_range = request.range;
    shared->rt_reflection_range = request.reflection_range;
    std::copy_n(request.view_projection.begin(), 16, shared->rt_view_projection);
    shared->rt_guides = (request.guides && bridge.guides) ? 1u : 0u;
  });
  constexpr uint32_t RAW_REFLECTIONS = RT_REFLECTIONS | RT_REFLECTIONS_RAW;
  bridge.hit_distance_written = traced && (request.flags & RAW_REFLECTIONS) == RAW_REFLECTIONS && bridge.rr_hit_distance.texture;
  return traced;
}

// The helper's last ray tracing status (one line).
inline std::string TraceStatus() {
  return (bridge.shared != nullptr) ? std::string(bridge.shared->rt_status) : std::string();
}

// False: only the HDR addon is installed (no renodx-tw-dlaa64.exe next to it): no DLSS, frame generation or ray tracing.
inline bool HelperInstalled() {
  static const bool installed = GetFileAttributesW(internal::HelperPath().c_str()) != INVALID_FILE_ATTRIBUTES;
  return installed;
}

// The helper's ray tracing device count (a new device after a GPU fault or hang: its scene empty).
inline uint32_t RayTracingDevice() {
  return (bridge.shared != nullptr) ? bridge.shared->rt_device : 0u;
}

// The triangles the helper left out of the scene so far (far off or stretched: garbage) and the last capture's key.
inline std::pair<uint32_t, uint64_t> RayTracingLeftOut() {
  return (bridge.shared != nullptr) ? std::pair{bridge.shared->rt_left_out, bridge.shared->rt_left_out_key} : std::pair{0u, uint64_t{0}};
}

// The inputs (written on `context` before this; their top-left `input_width` x `input_height`) through NVIDIA's DLAA
// (or DLSS, when that is below the textures' size; Ray Reconstruction with `ray_reconstruction` and the guides) into
// `bridge.output`, which the game's GPU then waits for; false: no result this frame (helper not ready, or no answer).
// FSR and XeSS take the camera's near, far and field of view from `view_to_clip` (row vectors).
inline bool Evaluate(ID3D11DeviceContext* context, UINT input_width, UINT input_height, float jitter_x, float jitter_y,
                     bool reset, uint32_t preset, const RayReconstruction* ray_reconstruction = nullptr,
                     const std::array<float, 16>* view_to_clip = nullptr) {
  const bool evaluated = internal::Request(context, EVALUATE, [&](Shared* shared) {
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
    if (view_to_clip != nullptr) std::copy_n(view_to_clip->begin(), 16, shared->view_to_clip);
    shared->rr_hit_distance_written = (shared->ray_reconstruction != 0 && bridge.hit_distance_written) ? 1u : 0u;
    shared->rr_extra_written = (shared->ray_reconstruction != 0) ? (ray_reconstruction->extra_written & bridge.rr_extra) : 0u;
  });
  bridge.hit_distance_written = false;
  bridge.reconstructing = evaluated && ray_reconstruction != nullptr && bridge.guides;
  if (bridge.shared != nullptr) {
    // Ray tracing's counts change every frame: its line is logged when the part before ": " changes, else with Log Details
    // each minute.
    const std::string_view ray_tracing = bridge.shared->rt_status;
    const std::array<std::string_view, 3> lines = {bridge.status, bridge.shared->fg_status,
                                                   ray_tracing.substr(0, ray_tracing.find(": "))};
    const uint64_t now = GetTickCount64();
    for (size_t index = 0; index < lines.size(); ++index) {
      if (bridge.logged[index] == lines[index] && (index != 2 || lines[index].empty() || !bridge.details || now < bridge.logged_time + 60000)) continue;
      bridge.logged[index] = lines[index];
      if (index == 2) bridge.logged_time = now;
      if (!lines[index].empty()) renodx::utils::log::i("[Total War] ", (index == 2 ? ray_tracing : lines[index]));
    }
  }
  return evaluated;
}

// A present through frame generation (the frame and HUD-less frame written on `context` before this): the helper copies
// the frame and presents it into its window over the game's (the game's GPU waits for its copies only before the next
// shared writes); false: not presented (helper not ready, off, or frame generation unavailable: FrameGenerationFailed).
inline bool PresentFrame(ID3D11DeviceContext* context, const FrameRequest& request) {
  if (bridge.fg_format == DXGI_FORMAT_UNKNOWN) return false;
  return internal::Request(context, PRESENT_FRAME, [&](Shared* shared) {
    shared->rt_request = 0;
    shared->fg_request = 1;
    shared->fg_window = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(request.window));
    shared->fg_multiplier = request.multiplier;
    shared->fg_reflex_boost = (request.reflex_boost ? 1u : 0u);
    shared->fg_frame_limit = request.frame_limit;
    shared->fg_preset = request.preset;
    shared->fg_ui = (request.hud_recomposition ? 1u : 0u);
    shared->fg_debug = request.debug_view;
    shared->fg_sdr = (request.sdr_output ? 1u : 0u);
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

// Whether the helper found frame generation unavailable here (its DLLs missing, DLSS's without an RTX 40, ...).
inline bool FrameGenerationFailed() {
  return bridge.shared != nullptr && bridge.shared->fg_failed != 0;
}

// The frame generation backend the helper runs (FG_BACKEND_*: Auto's choice too).
inline uint32_t FrameGenerationBackend() {
  return (bridge.shared != nullptr) ? bridge.shared->fg_running : FG_BACKEND_DLSS;
}

// The helper's last frame generation status (one line).
inline std::string FrameGenerationStatus() {
  return (bridge.shared != nullptr) ? std::string(bridge.shared->fg_status) : std::string();
}

// The game's device is going away: the shared objects with it (the helper reopens the next ones). `tell_helper`: another
// anti-aliasing took over: an empty set for the helper (its handles and DLSS's feature go at once, the rest with its idle
// release); the device and the leaves' atlas stay for a quick return, its meshes still point there (rt_scene frees it).
inline void Release(bool tell_helper = false) {
  if (tell_helper && bridge.pending) internal::Answer();
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
  bridge.rt_light_mask = {};
  bridge.rt_fog = {};
  bridge.rt_fog_lights = {};
  bridge.rt_tint = {};
  bridge.rt_reflection = {};
  bridge.rt_specular = {};
  bridge.rr_hit_distance = {};
  bridge.rr_extra_textures = {};
  bridge.fg_frame.Reset();
  bridge.fg_hudless.Reset();
  bridge.fg_frame_rtv.Reset();
  bridge.fg_hudless_rtv.Reset();
  bridge.fg_format = DXGI_FORMAT_UNKNOWN;
  bridge.fence.Reset();
  bridge.width = 0;
  bridge.reconstructing = false;
  bridge.color_square_root = false;
  bridge.deferred = 0;
  bridge.steady = {};
  bridge.guides = false;
  bridge.ray_tracing = false;
  bridge.reflections = false;
  bridge.hit_distance_written = false;
  if (!tell_helper) {
    bridge.rt_atlas = {};
    bridge.device.Reset();
  }
  if (!tell_helper || bridge.failed) return;
  auto& shared = *bridge.shared;
  internal::ClearHandles(&shared, false, false, false, false);
  shared.color = 0;
  shared.depth = 0;
  shared.motion = 0;
  shared.output = 0;
  shared.fence = 0;
  shared.rt_request = 0;
  shared.fg_request = 0;
  shared.ray_reconstruction = 0;
  shared.value = 0;
  bridge.value = 0;
  ++shared.generation;
  ResetEvent(bridge.done);
  SetEvent(bridge.request);
  internal::Answer(20000);
}

}  // namespace twattila::dlaa_bridge
