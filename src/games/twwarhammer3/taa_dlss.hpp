/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/dlaa_bridge.hpp"
#include "../twattila/frame_trace.hpp"

// DLSS in place of the game's TAA (it has no working DLSS of its own: preferences gfx_dlss never creates one): after
// its TAA resolve (cs 0x1ADBFF18 TAA / 0x6A8FC282 TAA High: t0 the G-buffer depth in x, t2 the velocity, t4 this
// frame's colour, jittered; u1 its result), the colour, depth and motion go through NVIDIA's DLSS in the TW helper
// (twattila/dlaa_bridge.hpp, renodx-tw-dlaa64.exe) and its result replaces the TAA's. The game's jitter (camera
// g_camera_jitter) and velocity (everything it draws) are its own. At the game's Resolution Scale of 100% only: below it
// the main view's camera never passes Upload's test, so the game's TAA and upscale stay and the helper doesn't start. In
// the addon's own context state (the game's put back after).
namespace twwarhammer3::taa_dlss {

using Microsoft::WRL::ComPtr;

// This frame's DLSS inputs (frame generation's): the bridge's depth and motion at the render size, the jitter in render
// pixels (y down), the main view's camera as uploaded.
struct Frame {
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11Texture2D> motion;
  std::array<uint32_t, 2> render = {};
  std::array<float, 2> jitter = {};
  bool reset = false;
  std::array<float, 66 * 4> camera = {};
};

namespace internal {

constexpr std::array<uint32_t, 2> TAA_SHADERS = {0x1ADBFF18u, 0x6A8FC282u};
// camera (1056 bytes): the projection's registers c6-c9, g_screen_size c37, g_viewport_dimensions c39,
// g_camera_jitter (clip space) at float 257.
constexpr uint32_t CAMERA_BYTES = 66 * 16;
constexpr uint32_t CAMERA_MAX_BYTES = 2048;
constexpr size_t PROJECTION = 6 * 4;
constexpr size_t VIEW_PROJECTION = 10 * 4;
constexpr size_t INVERSE_PROJECTION = 26 * 4;
constexpr size_t TIME = 35 * 4;
constexpr size_t SCREEN_SIZE = 37 * 4;
constexpr size_t VIEWPORT = 39 * 4;
constexpr size_t CAMERA_JITTER = 257;

struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> inputs;
  ComPtr<ID3D11ComputeShader> output;
  ComPtr<ID3D11ComputeShader> guides;
  ComPtr<ID3D11Buffer> shader_settings;  // dlss_inputs' and dlss_output's
  bool failed = false;
};

struct State {
  std::mutex mutex;
  twattila::anti_aliasing::Settings settings;
  Objects objects;
  std::unordered_map<uint64_t, const float*> mapped;  // camera-sized constant buffers mapped for writing
  std::atomic_bool any_mapped = false;
  std::array<float, CAMERA_BYTES / 4> camera = {};  // the main view's, last uploaded
  uint64_t camera_buffer = 0;                       // the buffer it was uploaded to
  bool camera_seen = false;                         // this frame: `camera` is valid only then
  bool trace_wanted = false;  // FrameTrace still to come (it starts from the 3D view's camera)
  uint64_t frame = 0;          // presents
  uint64_t evaluated_frame = UINT64_MAX;
  uint64_t taa_frame = UINT64_MAX;        // the game's TAA pass ran
  uint64_t no_camera_frame = UINT64_MAX;  // ... without the main view's camera
  uint64_t guides_frame = UINT64_MAX;  // Ray Reconstruction's guides written
  Frame last;                  // the last evaluation's (frame generation)
  uint64_t last_frame = UINT64_MAX;
  bool logged = false;
  bool no_camera_logged = false;
};
static State state;

inline bool Create(ID3D11Device* device) {
  auto& objects = state.objects;
  if (objects.device.Get() == device) return !objects.failed;
  objects = {.device = device};
  ComPtr<ID3D11Device1> device1;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  objects.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device),
                                                               nullptr, objects.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__dlss_inputs.data(), __dlss_inputs.size(), nullptr, objects.inputs.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__dlss_output.data(), __dlss_output.size(), nullptr, objects.output.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rr_guides.data(), __rr_guides.size(), nullptr, objects.guides.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&settings_desc, nullptr, objects.shader_settings.GetAddressOf()));
  if (objects.failed) renodx::utils::log::i("[Warhammer III] DLSS: its shaders could not be made.");
  return !objects.failed;
}

inline ComPtr<ID3D11Texture2D> TextureOf(ID3D11View* view) {
  ComPtr<ID3D11Resource> resource;
  ComPtr<ID3D11Texture2D> texture;
  if (view != nullptr) view->GetResource(resource.GetAddressOf());
  if (resource) resource.As(&texture);
  return texture;
}

// After the game's TAA resolve: its inputs through DLSS, the result into its output.
inline void Resolve(ID3D11DeviceContext* native) {
  std::array<ID3D11ShaderResourceView*, 5> inputs = {};
  native->CSGetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.data());
  ComPtr<ID3D11UnorderedAccessView> target;
  native->CSGetUnorderedAccessViews(1, 1, target.GetAddressOf());
  const auto release = [&] {
    for (auto* view : inputs) {
      if (view != nullptr) view->Release();
    }
  };
  const auto color = TextureOf(inputs[4]);
  const auto output = TextureOf(target.Get());
  if (inputs[0] == nullptr || inputs[2] == nullptr || !color || !output) {
    release();
    return;
  }
  D3D11_TEXTURE2D_DESC color_desc = {};
  D3D11_TEXTURE2D_DESC output_desc = {};
  color->GetDesc(&color_desc);
  output->GetDesc(&output_desc);
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  ComPtr<ID3D11DeviceContext1> context1;
  const std::unique_lock lock(state.mutex);
  state.taa_frame = state.frame;
  if (!state.camera_seen) {
    state.no_camera_frame = state.frame;
    if (!state.no_camera_logged) {
      state.no_camera_logged = true;
      renodx::utils::log::i(std::format("[Warhammer III] DLSS: the game's TAA ran without the main view's camera (colour {}x{}, "
                                        "output {}x{}): its Resolution Scale below 100%? The game's TAA stays.",
                                        color_desc.Width, color_desc.Height, output_desc.Width, output_desc.Height));
    }
    release();
    return;
  }
  if (!Create(device.Get()) || FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))
      || !twattila::dlaa_bridge::Prepare(device.Get(), output_desc.Width, output_desc.Height, state.settings.ray_reconstruction,
                                         state.settings.ray_tracing, DXGI_FORMAT_UNKNOWN, state.settings.ray_traced_reflections)) {
    release();
    return;
  }
  // The game's jitter (clip space, y up) in render pixels as it moves the picture (y down).
  const float* c = state.camera.data();
  const std::array<float, 2> jitter = {c[CAMERA_JITTER] * 0.5f * static_cast<float>(color_desc.Width),
                                       -c[CAMERA_JITTER + 1] * 0.5f * static_cast<float>(color_desc.Height)};
  const bool reset = state.evaluated_frame + 1 != state.frame;
  if (!state.logged && (c[CAMERA_JITTER] != 0.f || c[CAMERA_JITTER + 1] != 0.f)) {
    state.logged = true;
    D3D11_TEXTURE2D_DESC depth_desc = {};
    if (const auto depth = TextureOf(inputs[0])) depth->GetDesc(&depth_desc);
    renodx::utils::log::i(std::format("[Warhammer III] DLSS: {}x{} -> {}x{} (colour format {}, output {}, depth {}x{}), the "
                                      "camera's screen {}x{} and viewport {}x{}, the game's jitter {:.5f} {:.5f}, its "
                                      "projection's {:.5f} {:.5f}",
                                      color_desc.Width, color_desc.Height, output_desc.Width, output_desc.Height,
                                      static_cast<int>(color_desc.Format), static_cast<int>(output_desc.Format),
                                      depth_desc.Width, depth_desc.Height, c[SCREEN_SIZE], c[SCREEN_SIZE + 1], c[VIEWPORT],
                                      c[VIEWPORT + 1], c[CAMERA_JITTER], c[CAMERA_JITTER + 1], c[PROJECTION + 2], c[PROJECTION + 6]));
  }
  auto& bridge = twattila::dlaa_bridge::bridge;
  bool evaluated = false;
  twattila::dlaa_bridge::BeforeSharedWrites(native);
  {
    talesofarise::dlss::ContextStateScope scope(context1.Get(), state.objects.private_state.Get());
    const bool reconstruct = state.settings.ray_reconstruction && state.guides_frame == state.frame && bridge.guides;
    // NVSDK_NGX_DLSS_Hint_Render_Preset L, M; K takes the light as it is.
    const std::array<float, 4> shader_settings = {(reconstruct || state.settings.dlaa_preset == 12u || state.settings.dlaa_preset == 13u) ? 1.f : 0.f};
    context1->UpdateSubresource(state.objects.shader_settings.Get(), 0, nullptr, shader_settings.data(), 0, 0);
    const std::array<ID3D11ShaderResourceView*, 3> sources = {inputs[0], inputs[2], inputs[4]};
    const std::array<ID3D11UnorderedAccessView*, 3> destinations = {bridge.color.uav.Get(), bridge.depth.uav.Get(), bridge.motion.uav.Get()};
    context1->CSSetShader(state.objects.inputs.Get(), nullptr, 0);
    context1->CSSetShaderResources(0, static_cast<UINT>(sources.size()), sources.data());
    context1->CSSetUnorderedAccessViews(0, static_cast<UINT>(destinations.size()), destinations.data(), nullptr);
    context1->CSSetConstantBuffers(0, 1, state.objects.shader_settings.GetAddressOf());
    context1->Dispatch((color_desc.Width + 7) / 8, (color_desc.Height + 7) / 8, 1);
    context1->ClearState();
    // Ray Reconstruction (its guides written this frame): view = inverse projection x view-projection (the camera's
    // registers act on column vectors), given NVIDIA as row-vector matrices (their transposes).
    twattila::dlaa_bridge::RayReconstruction reconstruction = {.preset = state.settings.ray_reconstruction_preset};
    if (reconstruct) {
      for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
          float sum = 0.f;
          for (size_t k = 0; k < 4; ++k) sum += c[INVERSE_PROJECTION + (row * 4) + k] * c[VIEW_PROJECTION + (k * 4) + column];
          reconstruction.world_to_view[(column * 4) + row] = sum;
          reconstruction.view_to_clip[(column * 4) + row] = c[PROJECTION + (row * 4) + column];
        }
      }
    }
    evaluated = twattila::dlaa_bridge::Evaluate(context1.Get(), color_desc.Width, color_desc.Height, jitter[0], jitter[1], reset,
                                                state.settings.dlaa_preset, reconstruct ? &reconstruction : nullptr);
    if (evaluated) {
      ID3D11ShaderResourceView* result = bridge.output.srv.Get();
      ID3D11UnorderedAccessView* destination = target.Get();
      context1->CSSetShader(state.objects.output.Get(), nullptr, 0);
      context1->CSSetShaderResources(0, 1, &result);
      context1->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
      context1->CSSetConstantBuffers(0, 1, state.objects.shader_settings.GetAddressOf());
      context1->Dispatch((output_desc.Width + 7) / 8, (output_desc.Height + 7) / 8, 1);
    }
  }
  release();
  if (!evaluated) return;
  state.evaluated_frame = state.frame;
  state.last = {
      .depth = bridge.depth.texture,
      .motion = bridge.motion.texture,
      .render = {color_desc.Width, color_desc.Height},
      .jitter = jitter,
      .reset = reset,
  };
  std::copy_n(state.camera.begin(), state.last.camera.size(), state.last.camera.begin());
  state.last_frame = state.frame;
}

}  // namespace internal

// At the frame's first sun light draw of the main view (its G-buffer bound: channels 1-3 from `gbuffer_slot`, the camera
// at `camera_slot`): Ray Reconstruction's guides into the bridge's textures (rr_guides), once a frame.
inline void WriteGuides(ID3D11DeviceContext* native, UINT gbuffer_slot, UINT camera_slot) {
  using internal::state;
  ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  const std::unique_lock lock(state.mutex);
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (!twattila::anti_aliasing::Upscales(state.settings.method) || !state.settings.ray_reconstruction
      || state.guides_frame == state.frame || !bridge.guides
      || state.objects.failed || !state.objects.guides) {
    return;
  }
  std::array<ID3D11ShaderResourceView*, 3> gbuffer = {};
  native->PSGetShaderResources(gbuffer_slot, static_cast<UINT>(gbuffer.size()), gbuffer.data());
  ComPtr<ID3D11Buffer> camera;
  native->PSGetConstantBuffers(camera_slot, 1, camera.GetAddressOf());
  D3D11_TEXTURE2D_DESC desc = {};
  if (const auto normal = internal::TextureOf(gbuffer[0])) normal->GetDesc(&desc);
  if (camera && gbuffer[1] != nullptr && gbuffer[2] != nullptr && desc.Width == bridge.width && desc.Height == bridge.height) {
    twattila::dlaa_bridge::BeforeSharedWrites(native);
    talesofarise::dlss::ContextStateScope scope(context1.Get(), state.objects.private_state.Get());
    const std::array<ID3D11UnorderedAccessView*, 4> guides = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(),
                                                              bridge.normals.uav.Get(), bridge.roughness.uav.Get()};
    context1->CSSetShader(state.objects.guides.Get(), nullptr, 0);
    context1->CSSetShaderResources(0, static_cast<UINT>(gbuffer.size()), gbuffer.data());
    context1->CSSetUnorderedAccessViews(0, static_cast<UINT>(guides.size()), guides.data(), nullptr);
    context1->CSSetConstantBuffers(0, 1, camera.GetAddressOf());
    context1->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
    context1->ClearState();
    state.guides_frame = state.frame;
  }
  for (auto* view : gbuffer) {
    if (view != nullptr) view->Release();
  }
}

namespace internal {

constexpr auto OnTaa = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  {
    const std::unique_lock lock(state.mutex);
    if (!twattila::anti_aliasing::Upscales(state.settings.method)) return {};
  }
  return {.post_callback = [](Context& command, const void*) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&native);
    if (native != nullptr && native->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) Resolve(native);
  }};
};

inline void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t,
                              reshade::api::map_access access, void** data) {
  if (offset != 0 || data == nullptr || *data == nullptr || access == reshade::api::map_access::read_only) return;
  const auto desc = device->get_resource_desc(resource);
  if (desc.type != reshade::api::resource_type::buffer || (desc.usage & reshade::api::resource_usage::constant_buffer) == 0
      || desc.buffer.size < CAMERA_BYTES || desc.buffer.size > CAMERA_MAX_BYTES) {
    return;
  }
  const std::unique_lock lock(state.mutex);
  if (!twattila::anti_aliasing::Upscales(state.settings.method) && !state.trace_wanted) return;
  state.mapped[resource.handle] = static_cast<const float*>(*data);
  state.any_mapped.store(true, std::memory_order_relaxed);
}

// The main view's camera (state.mutex held): a perspective projection (its last register 0, 0, 1, 0) over the whole
// screen, uploaded to `buffer`.
inline void Upload(const float* c, uint64_t buffer) {
  const float* last = c + PROJECTION + 12;
  if (last[0] != 0.f || last[1] != 0.f || last[2] != 1.f || last[3] != 0.f || c[SCREEN_SIZE] <= 0.f
      || c[VIEWPORT] != c[SCREEN_SIZE] || c[VIEWPORT + 1] != c[SCREEN_SIZE + 1]) {
    return;
  }
  std::copy_n(c, state.camera.size(), state.camera.begin());
  state.camera_buffer = buffer;
  state.camera_seen = true;
}

inline void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  if (!state.any_mapped.load(std::memory_order_relaxed)) return;
  const std::unique_lock lock(state.mutex);
  const auto found = state.mapped.find(resource.handle);
  if (found == state.mapped.end()) return;
  const float* c = found->second;
  state.mapped.erase(found);
  state.any_mapped.store(!state.mapped.empty(), std::memory_order_relaxed);
  Upload(c, resource.handle);
}

inline bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset,
                                 uint64_t size) {
  if (data == nullptr || offset != 0 || size < CAMERA_BYTES || size > CAMERA_MAX_BYTES) return false;
  const auto desc = device->get_resource_desc(resource);
  if ((desc.usage & reshade::api::resource_usage::constant_buffer) == 0) return false;
  const std::unique_lock lock(state.mutex);
  if (twattila::anti_aliasing::Upscales(state.settings.method) || state.trace_wanted) Upload(static_cast<const float*>(data), resource.handle);
  return false;
}

inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  bool camera_seen = false;
  {
    const std::unique_lock lock(state.mutex);
    ++state.frame;
    camera_seen = state.camera_seen;
    state.camera_seen = false;
    state.trace_wanted = twattila::frame_trace::Wanted();
  }
  // FrameTrace: a frame of the 3D view (600 presents with its camera), or the next one when renodx_trace_now sits next
  // to the exe (checked each second; removed).
  if (static uint32_t presents = 0; ++presents % 60 == 0) {
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    const auto marker = std::filesystem::path(exe).parent_path() / L"renodx_trace_now";
    if (DeleteFileW(marker.c_str()) != 0) twattila::frame_trace::StartSoon();
  }
  twattila::frame_trace::OnPresent(camera_seen);
}

}  // namespace internal

// `keep_textures`: frame generation in the game's process may hold the bridge's depth and motion.
inline void Update(const twattila::anti_aliasing::Settings& settings, bool keep_textures) {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.settings = settings;
  // Off: the shared set and the helper's DLSS back (their VRAM).
  if (auto& bridge = twattila::dlaa_bridge::bridge; !twattila::anti_aliasing::Upscales(settings.method) && !keep_textures && bridge.width != 0
      && bridge.shared != nullptr && bridge.shared->ready != 0) {
    internal::state.last = {};
    twattila::dlaa_bridge::Release(true);
  }
}

// What DLSS really does, for its status line.
inline std::string Status() {
  using internal::state;
  const std::unique_lock lock(state.mutex);
  const auto in_last_second = [](uint64_t frame) { return frame != UINT64_MAX && state.frame - frame < 60; };
  const auto& bridge = twattila::dlaa_bridge::bridge;
  if (!twattila::dlaa_bridge::OnNvidia()) return "Not running: DLSS needs an NVIDIA GPU.";
  if (bridge.failed) return bridge.status;
  if (in_last_second(state.evaluated_frame)) return (bridge.shared != nullptr) ? bridge.shared->status : bridge.status;
  if (in_last_second(state.no_camera_frame)) return "Not running: in-game Resolution Scale is below 100%. The game's own TAA and upscale stay.";
  if (in_last_second(state.taa_frame)) return bridge.status;
  return "Not running: keep in-game Anti-aliasing on TAA or TAA High (menus have none).";
}

// The last evaluation's inputs if it was in frame `frame` (presents so far); false: none.
inline bool LastFrame(uint64_t frame, Frame* result) {
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.last_frame != frame) return false;
  *result = internal::state.last;
  return true;
}

// The main view's camera constants as uploaded this frame when `buffer` holds them (ray tracing's camera); false: not.
inline bool MainCamera(ID3D11Buffer* buffer, std::array<float, internal::CAMERA_BYTES / 4>* camera) {
  const std::unique_lock lock(internal::state.mutex);
  if (!internal::state.camera_seen || reinterpret_cast<uint64_t>(buffer) != internal::state.camera_buffer) return false;
  *camera = internal::state.camera;
  return true;
}

// The main view's size this frame (its viewport); none yet: zero.
inline std::array<UINT, 2> RenderSize() {
  const std::unique_lock lock(internal::state.mutex);
  if (!internal::state.camera_seen) return {};
  return {static_cast<UINT>(internal::state.camera[internal::VIEWPORT]), static_cast<UINT>(internal::state.camera[internal::VIEWPORT + 1])};
}

// Presents so far (frame generation's frame number).
inline uint64_t Presents() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.frame;
}

inline void Install() {
  twattila::frame_trace::Install();
  for (const uint32_t shader_hash : internal::TAA_SHADERS) {
    renodx::utils::command_action::Register(internal::OnTaa, {
                                                                 .shader_hash = shader_hash,
                                                                 .command_types = renodx::utils::command_action::COMMAND_TYPE_DISPATCH,
                                                             });
  }
  reshade::register_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
  reshade::register_event<reshade::addon_event::present>(internal::OnPresent);
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnTaa);
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
  reshade::unregister_event<reshade::addon_event::present>(internal::OnPresent);
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.objects.device.Get() != native) return;
  internal::state.objects = {};
  internal::state.last = {};
  twattila::dlaa_bridge::Release();
}

}  // namespace twwarhammer3::taa_dlss
