/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include <embed/shaders.h>
#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../twattila/dlaa_bridge.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/frame_generation.hpp"
#include "../twattila/frame_trace.hpp"
#include "../twattila/reflex.hpp"

// DLAA (Graphics (Experimental) > Anti-Aliasing, off by default; tools\tw\romeremastered\plan_1114.md item 3) through
// the TW helper's bridge. The game has no TAA, jitter or motion vectors. ATN's 2026-11-14 traces (level A): every scene
// draw of the main view reads one 192-byte camera at VS b5 (row vectors: view rows 0-3, projection rows 4-7,
// view-projection rows 8-11); a depth prepass (D32, depth at near 0) and the forward pass on it with EQUAL; the shadow
// mask's resolve reads that depth at t1; ResolveMainColor (the tone mapper) writes the FP16 frame at the display's size,
// SMAA and the UI after it. Here: a Halton jitter written into every upload of the main camera (the prepass and the
// forward pass get the same), the camera's motion from the depth (aa_dlaa_prepare), NVIDIA's DLAA into the tone
// mapper's output, the game's SMAA left with nothing to do (Evaluated: its edges not drawn). Below 100% Resolution Scale
// (ATN's 67% / 50% traces: the scene, the tone mapper and SMAA at the render size, then Feral's upscaler, VS 9F3C9B3D,
// into an FP16 target at the display's size, the UI on it): DLSS from the tone mapper's output to the upscaler's size,
// copied into its target in its place (Upscale). With a ray row on the bridge stays at the depth's size (rt_core
// captures the main view's depth at the bridge's): DLAA at the render size, Feral's upscaler after.
namespace twromeremastered::dlaa {

using Microsoft::WRL::ComPtr;

// The page's anti-aliasing rows (feature_settings::AppendAa, graphics_page.hpp): method 0 the game's, 5 DLSS (DLAA, or
// DLSS below 100%); its preset, frame generation and Reflex.
static twattila::feature_settings::AaSliders sliders;
// Ray tracing (ray_tracing.hpp) is on: the main camera and the scene's depth are kept without DLAA too, the bridge made
// with its ray tracing textures and kept.
static bool rays = false;
// Screen-space shadows (contact_shadows.hpp) are on: the main camera and the scene's depth kept without DLAA too.
static bool contact = false;

namespace internal {

// DLAA, the rays or the screen-space shadows want the main camera and the scene's depth.
inline bool CameraWanted() {
  return sliders.method != 0.f || rays || contact;
}

constexpr UINT CAMERA_BYTES = 192;
constexpr size_t PROJECTION = 16;
constexpr size_t VIEW_PROJECTION = 32;
constexpr uint32_t JITTER_PHASES = 8;
constexpr UINT SRV_COUNT = 7;  // aa_dlaa_finish's t0-t6
constexpr UINT UAV_COUNT = 3;  // aa_dlaa_prepare's u0-u2

// aa.hlsli's aa_settings (DLAA reads the metrics and the Debug View's).
struct ShaderSettings {
  std::array<float, 4> metrics = {};
  std::array<float, 12> smaa_and_cmaa2 = {};
  std::array<float, 4> dlaa_debug = {};
};

// aa.hlsli's aa_taa.
struct TaaConstants {
  std::array<float, 16> view_projection = {};
  std::array<float, 16> inverse_view_projection = {};
  std::array<float, 16> previous_view_projection = {};
  std::array<float, 2> projection_z = {};
  float history_weight = 0.f;
  float reset = 1.f;
  float sharpening = 0.f;
  std::array<float, 2> jitter = {};
  float object_motion = 0.f;
};

// The game's rows (v x rows).
struct Camera {
  std::array<float, 16> view = {};
  std::array<float, 16> projection = {};                // without the jitter
  std::array<float, 16> view_projection = {};           // without the jitter
  std::array<float, 16> jittered_view_projection = {};  // as the depth was drawn
  std::array<float, 16> inverse_view_projection = {};   // of the jittered
  std::array<float, 3> position = {};
  std::array<float, 2> jitter = {};  // written into this upload: pixels, x right, y up
  uint64_t frame = 0;
};

struct State {
  std::shared_mutex mutex;
  uint64_t frame = 1;
  std::array<float, 2> jitter = {};   // this frame's (0 unless DLAA ran the frame before)
  std::array<UINT, 2> display = {};   // the tone mapper's target
  std::array<UINT, 2> upscaler = {};  // Feral's upscaler's target, drawn in upscaler_frame
  uint64_t upscaler_frame = 0;
  uint64_t upscaled_frame = 0;  // DLSS's result (objects.result) waits for the upscaler this frame
  Camera camera;                      // the main view's last upload
  ComPtr<ID3D11ShaderResourceView> depth;
  uint64_t depth_frame = 0;
  std::array<float, 16> previous_view_projection = {};
  uint64_t evaluated_frame = 0;
  std::vector<std::string> said;  // each status line once
};
static State state;

// Below 100% Resolution Scale: Feral's upscaler drew the frame before, its target larger than `width` x `height` (the
// tone mapper's). state.mutex held.
inline bool BelowFullScale(UINT width, UINT height) {
  return state.upscaler_frame + 1 >= state.frame && state.upscaler[0] > width && state.upscaler[1] > height;
}

// The device's objects: only on the game's render thread (the tone mapper's draw, destroy_device).
struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> prepare;
  ComPtr<ID3D11ComputeShader> finish;
  ComPtr<ID3D11Buffer> settings;
  ComPtr<ID3D11Buffer> taa;
  ComPtr<ID3D11Texture2D> result;
  ComPtr<ID3D11UnorderedAccessView> result_uav;
  ComPtr<ID3D11Texture2D> frame;
  ComPtr<ID3D11ShaderResourceView> frame_srv;
};
static Objects objects;

static thread_local ID3D11Buffer* mapped_buffer = nullptr;
static thread_local float* mapped_data = nullptr;

// The game's cameras act as v x rows (its VS: v.x x row 8, + v.y x row 9...); aa.hlsli's matrices (column-major under
// mul(v, m)) and rt_core's (rows acting on column vectors) are their transposes.
inline std::array<float, 16> Transposed(const std::array<float, 16>& rows) {
  std::array<float, 16> columns = {};
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) columns[(column * 4) + row] = rows[(row * 4) + column];
  }
  return columns;
}

// aa_taa for this frame.
inline TaaConstants MakeTaaConstants(const Camera& camera, const std::array<float, 16>& previous_view_projection, bool continued) {
  return {
      .view_projection = Transposed(camera.view_projection),
      .inverse_view_projection = Transposed(camera.inverse_view_projection),
      .previous_view_projection = Transposed(continued ? previous_view_projection : camera.view_projection),
      .reset = (continued ? 0.f : 1.f),
      // The camera's jitter (y up) as it moves the picture (y down).
      .jitter = {camera.jitter[0], -camera.jitter[1]},
  };
}

inline bool IsCameraBuffer(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 && desc.ByteWidth == CAMERA_BYTES;
}

// An upload into a 192-byte constant buffer: the main view's camera (a perspective projection with the display's
// aspect, rows 8-11 = rows 0-3 x rows 4-7; the shadow cascades' are orthographic) noted and jittered in place.
inline void JitterCamera(float* c) {
  const std::unique_lock lock(state.mutex);
  const auto [width, height] = state.display;
  const float* projection = c + PROJECTION;
  if (width == 0 || projection[3] != 0.f || projection[7] != 0.f || projection[11] != 1.f || projection[15] != 0.f
      || projection[0] <= 0.f
      || std::abs((projection[0] * static_cast<float>(width)) - (projection[5] * static_cast<float>(height)))
             > 1e-3f * projection[5] * static_cast<float>(height)) {
    return;
  }
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) {
      float product = 0.f;
      for (size_t k = 0; k < 4; ++k) product += c[(row * 4) + k] * projection[(k * 4) + column];
      const float stored = c[VIEW_PROJECTION + (row * 4) + column];
      if (std::abs(product - stored) > 1e-3f * (1.f + std::abs(stored))) return;
    }
  }
  auto& camera = state.camera;
  std::copy_n(c, 16, camera.view.begin());
  std::copy_n(c + PROJECTION, 16, camera.projection.begin());
  std::copy_n(c + VIEW_PROJECTION, 16, camera.view_projection.begin());
  // The eye: the view's rows 0-2 a rotation, row 3 its translation (the eye x the view = 0).
  for (size_t axis = 0; axis < 3; ++axis) {
    camera.position[axis] = -((c[12] * c[axis * 4]) + (c[13] * c[(axis * 4) + 1]) + (c[14] * c[(axis * 4) + 2]));
  }
  camera.jitter = state.jitter;
  camera.frame = state.frame;
  // Clip x / y move by the jitter x w (in clip units: 2 / the size a pixel), the projection's and the view-projection's.
  const float jitter_x = 2.f * state.jitter[0] / static_cast<float>(width);
  const float jitter_y = 2.f * state.jitter[1] / static_cast<float>(height);
  for (const size_t matrix : {PROJECTION, VIEW_PROJECTION}) {
    for (size_t row = 0; row < 4; ++row) {
      float* values = c + matrix + (row * 4);
      values[0] += jitter_x * values[3];
      values[1] += jitter_y * values[3];
    }
  }
  std::copy_n(c + VIEW_PROJECTION, 16, camera.jittered_view_projection.begin());
  // The jittered view-projection's inverse (Gauss-Jordan in doubles): the depth's positions.
  std::array<std::array<double, 8>, 4> rows = {};
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) rows[row][column] = c[VIEW_PROJECTION + (row * 4) + column];
    rows[row][4 + row] = 1.;
  }
  for (size_t column = 0; column < 4; ++column) {
    size_t pivot = column;
    for (size_t row = column + 1; row < 4; ++row) {
      if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) {
        pivot = row;
      }
    }
    std::swap(rows[pivot], rows[column]);
    const double scale = (rows[column][column] != 0. ? 1. / rows[column][column] : 0.);
    for (double& value : rows[column]) value *= scale;
    for (size_t row = 0; row < 4; ++row) {
      if (row == column) continue;
      const double factor = rows[row][column];
      for (size_t index = 0; index < 8; ++index) rows[row][index] -= factor * rows[column][index];
    }
  }
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) {
      camera.inverse_view_projection[(row * 4) + column] = static_cast<float>(rows[row][4 + column]);
    }
  }
}

inline void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t,
                              reshade::api::map_access access, void** data) {
  if (!CameraWanted() || access == reshade::api::map_access::read_only || offset != 0 || data == nullptr
      || *data == nullptr || device->get_api() != reshade::api::device_api::d3d11) {
    return;
  }
  auto* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
  if (!IsCameraBuffer(buffer)) return;
  mapped_buffer = buffer;
  mapped_data = static_cast<float*>(*data);
}

// Before the native Unmap, while the pointer is still valid.
inline void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  if (mapped_buffer == nullptr || mapped_buffer != reinterpret_cast<ID3D11Buffer*>(resource.handle)) return;
  mapped_buffer = nullptr;
  JitterCamera(std::exchange(mapped_data, nullptr));
}

// UpdateSubresource: the main camera uploaded jittered instead (true: the game's update skipped).
inline bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource,
                                 uint64_t offset, uint64_t size) {
  if (!CameraWanted() || data == nullptr || offset != 0 || size < CAMERA_BYTES
      || device->get_api() != reshade::api::device_api::d3d11) {
    return false;
  }
  auto* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
  if (!IsCameraBuffer(buffer)) return false;
  std::array<float, CAMERA_BYTES / 4> camera;
  std::memcpy(camera.data(), data, CAMERA_BYTES);
  JitterCamera(camera.data());
  if (std::memcmp(camera.data(), data, CAMERA_BYTES) == 0) return false;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  ComPtr<ID3D11DeviceContext> context;
  native->GetImmediateContext(context.GetAddressOf());
  context->UpdateSubresource(buffer, 0, nullptr, camera.data(), 0, 0);
  return true;
}

// The next frame's jitter: Halton (2, 3) over 8 frames, centred, only after a frame DLAA ran (a frame without it would
// shake).
inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  const auto halton = [](uint32_t index, uint32_t base) {
    float result = 0.f;
    float fraction = 1.f / static_cast<float>(base);
    for (; index > 0; index /= base, fraction /= static_cast<float>(base)) {
      result += fraction * static_cast<float>(index % base);
    }
    return result;
  };
  {
    const std::unique_lock lock(state.mutex);
    const uint32_t phase = static_cast<uint32_t>(state.frame % JITTER_PHASES) + 1u;
    state.jitter = ((sliders.method != 0.f && state.evaluated_frame == state.frame)
                        ? std::array<float, 2>{halton(phase, 2) - 0.5f, halton(phase, 3) - 0.5f}
                        : std::array<float, 2>{});
    ++state.frame;
  }
  // Turned off (and no rays): the helper's DLAA and the shared textures let go (made again when turned on).
  if (sliders.method == 0.f && !rays && twattila::dlaa_bridge::bridge.width != 0) {
    twattila::dlaa_bridge::Release(true);
  }
}

inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  twattila::frame_generation::OnDestroyDevice(native);
  twattila::reflex::OnDestroyDevice(native);
  {
    const std::unique_lock lock(state.mutex);
    state.depth.Reset();
    state.evaluated_frame = 0;
  }
  if (objects.device.Get() == native) objects = {};
  twattila::dlaa_bridge::Release();
}

}  // namespace internal

// The shadow mask's resolve (its PS, or CS with `compute`) just read the scene's depth at t1: kept for this frame's DLAA
// (and the rays and the screen-space shadows: SceneDepth).
inline void KeepDepth(reshade::api::command_list* cmd_list, bool compute) {
  if (!internal::CameraWanted() || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  ComPtr<ID3D11ShaderResourceView> depth;
  if (compute) {
    context->CSGetShaderResources(1, 1, depth.GetAddressOf());
  } else {
    context->PSGetShaderResources(1, 1, depth.GetAddressOf());
  }
  const std::unique_lock lock(internal::state.mutex);
  internal::state.depth = depth;
  internal::state.depth_frame = internal::state.frame;
}

// The bridge at `width` x `height` as DLAA, the rays and frame generation want it (one set of arguments for all: any other
// makes its textures again).
inline bool PrepareBridge(ID3D11Device* device, UINT width, UINT height) {
  return twattila::dlaa_bridge::Prepare(device, width, height, false, rays, twattila::frame_generation::Format(), false,
                                        twattila::dlaa_bridge::UPSCALER_DLSS, twattila::feature_settings::AaSettings(sliders).frame_backend);
}

// The scene's depth as last kept (one texture through the frames: before this frame's resolve, the last frame's view of
// it, this frame's prepass already drawn into it); null: none yet.
inline ComPtr<ID3D11ShaderResourceView> SceneDepth() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.depth;
}

// The main view's camera uploaded this frame; false: none yet this frame.
inline bool MainCamera(internal::Camera* camera) {
  const std::shared_lock lock(internal::state.mutex);
  if (internal::state.camera.frame != internal::state.frame) return false;
  *camera = internal::state.camera;
  return true;
}

// DLAA ran this frame: the game's SMAA passes after it find no edges (its blend then copies the frame).
// The render's share of the output's width (the tone mapper's over the upscaler's below 100%; 1 at 100%).
inline float RenderScale() {
  const std::shared_lock lock(internal::state.mutex);
  const auto [width, height] = internal::state.display;
  return internal::BelowFullScale(width, height) ? static_cast<float>(width) / static_cast<float>(internal::state.upscaler[0]) : 1.f;
}

inline bool Evaluated() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.evaluated_frame == internal::state.frame;
}

// After ResolveMainColor (any of its permutations): DLAA into its output.
inline void OnToneMapped(reshade::api::command_list* cmd_list) {
  using internal::objects;
  using internal::state;
  if (sliders.method == 0.f || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> target_resource;
  target_view->GetResource(target_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target;
  if (FAILED(target_resource.As(&target))) return;
  D3D11_TEXTURE2D_DESC target_desc = {};
  target->GetDesc(&target_desc);
  const UINT width = target_desc.Width;
  const UINT height = target_desc.Height;

  const auto report = [&](const std::string& status) {
    const std::unique_lock lock(state.mutex);
    if (std::ranges::find(state.said, status) != state.said.end()) return;
    state.said.push_back(status);
    renodx::utils::log::i("[Rome Remastered] DLAA: ", status);
  };

  // This frame's camera and depth under the lock; the work without it (the helper's answer may take a while).
  internal::Camera camera;
  ComPtr<ID3D11ShaderResourceView> depth;
  std::array<float, 16> previous_view_projection = {};
  bool continued = false;
  bool camera_seen = false;
  // Below 100% (the upscaler drew the frame before, its target larger), no rays: DLSS to the upscaler's size; else DLAA
  // at the tone mapper's.
  std::array<UINT, 2> output = {width, height};
  bool below = false;
  {
    const std::unique_lock lock(state.mutex);
    state.display = {width, height};
    below = internal::BelowFullScale(width, height);
    if (below && !rays) output = state.upscaler;
    camera = state.camera;
    camera_seen = camera.frame == state.frame;
    if (state.depth_frame == state.frame) depth = state.depth;
    previous_view_projection = state.previous_view_projection;
    continued = state.evaluated_frame + 1 == state.frame;
  }
  const bool fp16_target = target_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || target_desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS;
  if (!fp16_target || target_desc.SampleDesc.Count != 1 || (target_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
    report("off: the tone mapper's target isn't the add-on's FP16 texture.");
    return;
  }
  if (!depth) {
    report("off: no scene depth this frame (the shadow mask's resolve didn't run).");
    return;
  }
  {
    ComPtr<ID3D11Resource> depth_resource;
    depth->GetResource(depth_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> depth_texture;
    D3D11_TEXTURE2D_DESC depth_desc = {};
    if (SUCCEEDED(depth_resource.As(&depth_texture))) depth_texture->GetDesc(&depth_desc);
    if (depth_desc.Width != width || depth_desc.Height != height || depth_desc.SampleDesc.Count != 1) {
      report("off: the scene's depth isn't the tone mapper's size or uses MSAA.");
      return;
    }
  }
  if (!camera_seen) {
    report("off: no main camera this frame.");
    return;
  }

  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (objects.device != device) {
    objects = {.device = device};
    const auto compute = [&](const auto& code, ComPtr<ID3D11ComputeShader>* shader) {
      return SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
    };
    const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = sizeof(internal::ShaderSettings), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC taa_desc = {.ByteWidth = sizeof(internal::TaaConstants), .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    if (!compute(__aa_dlaa_prepare, &objects.prepare) || !compute(__aa_dlaa_finish, &objects.finish)
        || FAILED(device->CreateBuffer(&settings_desc, nullptr, objects.settings.GetAddressOf()))
        || FAILED(device->CreateBuffer(&taa_desc, nullptr, objects.taa.GetAddressOf()))) {
      objects = {};
      report("off: its shaders couldn't be made.");
      return;
    }
  }
  D3D11_TEXTURE2D_DESC result_desc = {};
  if (objects.result) objects.result->GetDesc(&result_desc);
  if (result_desc.Width != output[0] || result_desc.Height != output[1]) {
    objects.result.Reset();
    objects.result_uav.Reset();
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = output[0],
        .Height = output[1],
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
    };
    if (FAILED(device->CreateTexture2D(&desc, nullptr, objects.result.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(objects.result.Get(), nullptr, objects.result_uav.GetAddressOf()))) {
      objects.result.Reset();
      report("off: its result texture couldn't be made.");
      return;
    }
  }
  if (objects.frame != target) {
    objects.frame = target;
    objects.frame_srv.Reset();
    const D3D11_SHADER_RESOURCE_VIEW_DESC frame_desc = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
    if (FAILED(device->CreateShaderResourceView(target.Get(), &frame_desc, objects.frame_srv.GetAddressOf()))) {
      objects.frame.Reset();
      report("off: the tone mapper's target can't be read.");
      return;
    }
  }
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (!PrepareBridge(device.Get(), output[0], output[1])) {
    report(std::format("off: {}", bridge.status));
    return;
  }

  // DLSS's presets L and M crush dark tones on a linear frame: its square root for them (Attila's c243f7c4).
  const uint32_t preset = twattila::feature_settings::AaSettings(sliders).dlaa_preset;
  const bool square_root = preset == 12u || preset == 13u;
  // aa_dlaa_prepare at the tone mapper's size, aa_dlaa_finish at the output's (the input size its Debug View's).
  const auto update_settings = [&](UINT pass_width, UINT pass_height) {
    const float pass_width_f = static_cast<float>(pass_width);
    const float pass_height_f = static_cast<float>(pass_height);
    const internal::ShaderSettings shader_settings = {
        .metrics = {1.f / pass_width_f, 1.f / pass_height_f, pass_width_f, pass_height_f},
        .dlaa_debug = {0.f, static_cast<float>(width), static_cast<float>(height), (square_root ? 1.f : 0.f)},
    };
    native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &shader_settings, 0, 0);
  };
  update_settings(width, height);
  const internal::TaaConstants taa = internal::MakeTaaConstants(camera, previous_view_projection, continued);
  native->UpdateSubresource(objects.taa.Get(), 0, nullptr, &taa, 0, 0);

  // The game's compute state and targets kept; the frame read while no render target holds it (D3D11 drops a view of a
  // texture still bound for output).
  ComPtr<ID3D11ComputeShader> game_shader;
  native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  std::array<ComPtr<ID3D11ShaderResourceView>, internal::SRV_COUNT> game_views;
  for (UINT slot = 0; slot < internal::SRV_COUNT; ++slot) native->CSGetShaderResources(slot, 1, game_views[slot].GetAddressOf());
  std::array<ComPtr<ID3D11UnorderedAccessView>, internal::UAV_COUNT> game_uavs;
  for (UINT slot = 0; slot < internal::UAV_COUNT; ++slot) native->CSGetUnorderedAccessViews(slot, 1, game_uavs[slot].GetAddressOf());
  std::array<ComPtr<ID3D11Buffer>, 2> game_constants;
  for (UINT slot = 0; slot < 2; ++slot) native->CSGetConstantBuffers(slot, 1, game_constants[slot].GetAddressOf());
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets = {};
  ComPtr<ID3D11DepthStencilView> game_depth_target;
  native->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, game_targets.data(), game_depth_target.GetAddressOf());
  std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_target_references;
  for (UINT slot = 0; slot < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++slot) game_target_references[slot].Attach(game_targets[slot]);
  native->OMSetRenderTargets(0, nullptr, nullptr);

  constexpr std::array<ID3D11ShaderResourceView*, internal::SRV_COUNT> NO_VIEWS = {};
  // The views off first, then the unordered access views, then the views: D3D11 drops a binding whose resource is still
  // bound the other way.
  const auto bind = [&](ID3D11ComputeShader* shader, const std::array<ID3D11ShaderResourceView*, internal::SRV_COUNT>& views,
                        const std::array<ID3D11UnorderedAccessView*, internal::UAV_COUNT>& uavs) {
    native->CSSetShader(shader, nullptr, 0);
    native->CSSetShaderResources(0, internal::SRV_COUNT, NO_VIEWS.data());
    native->CSSetUnorderedAccessViews(0, internal::UAV_COUNT, uavs.data(), nullptr);
    native->CSSetShaderResources(0, internal::SRV_COUNT, views.data());
  };
  const std::array<ID3D11Buffer*, 2> constants = {objects.settings.Get(), objects.taa.Get()};
  native->CSSetConstantBuffers(0, 2, constants.data());
  bridge.color_square_root = square_root;
  twattila::dlaa_bridge::BeforeSharedWrites(native);
  bind(objects.prepare.Get(), {objects.frame_srv.Get(), depth.Get()},
       {bridge.color.uav.Get(), bridge.depth.uav.Get(), bridge.motion.uav.Get()});
  native->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
  bind(nullptr, {}, {});
  const bool resolved = twattila::dlaa_bridge::Evaluate(native, width, height, camera.jitter[0], -camera.jitter[1], !continued, preset);
  const bool upscaled = output[0] != width;
  if (resolved) {
    update_settings(output[0], output[1]);
    bind(objects.finish.Get(), {bridge.output.srv.Get(), objects.frame_srv.Get(), bridge.motion.srv.Get()},
         {objects.result_uav.Get()});
    native->Dispatch((output[0] + 7) / 8, (output[1] + 7) / 8, 1);
    bind(nullptr, {}, {});
    // DLSS's result waits for the upscaler (Upscale); DLAA's goes back into the tone mapper's output.
    if (!upscaled) native->CopyResource(target.Get(), objects.result.Get());
  }
  // Frame generation: the tone mapper's output after DLAA (or DLSS's result) is the HUD-less frame (the traces: SMAA's
  // passes, copying it under DLAA, the upscaler, then the UI on their target), with this frame's inputs (the camera as the
  // registers would hold it: its rows transposed; near and far from the projection's z column).
  if (resolved && twattila::frame_generation::Wanted()) {
    twattila::frame_generation::Inputs inputs = {
        .width = width,
        .height = height,
        .jitter_x = camera.jitter[0],
        .jitter_y = -camera.jitter[1],
        .reset = !continued,
    };
    const std::array<float, 16> projection = internal::Transposed(camera.projection);
    twattila::frame_generation::SetCamera(&inputs, projection, internal::Transposed(camera.view), internal::Transposed(camera.view_projection),
                                          internal::Transposed(continued ? previous_view_projection : camera.view_projection), camera.position);
    const float depth_scale = projection[10];
    const float depth_offset = projection[11];
    inputs.near_plane = (depth_scale != 0.f ? -depth_offset / depth_scale : 1.f);
    inputs.far_plane = (depth_scale != 1.f ? depth_scale * inputs.near_plane / (depth_scale - 1.f) : 25000.f);
    twattila::frame_generation::KeepFrame(native, (upscaled ? objects.result : target).Get(), inputs);
  }

  native->CSSetShader(game_shader.Get(), nullptr, 0);
  std::array<ID3D11UnorderedAccessView*, internal::UAV_COUNT> uavs = {};
  for (UINT slot = 0; slot < internal::UAV_COUNT; ++slot) uavs[slot] = game_uavs[slot].Get();
  native->CSSetUnorderedAccessViews(0, internal::UAV_COUNT, uavs.data(), nullptr);
  std::array<ID3D11ShaderResourceView*, internal::SRV_COUNT> views = {};
  for (UINT slot = 0; slot < internal::SRV_COUNT; ++slot) views[slot] = game_views[slot].Get();
  native->CSSetShaderResources(0, internal::SRV_COUNT, views.data());
  std::array<ID3D11Buffer*, 2> buffers = {game_constants[0].Get(), game_constants[1].Get()};
  native->CSSetConstantBuffers(0, 2, buffers.data());
  native->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, game_targets.data(), game_depth_target.Get());

  if (!resolved) {
    report(std::format("waiting: {}", bridge.status));
    return;
  }
  {
    const std::unique_lock lock(state.mutex);
    state.evaluated_frame = state.frame;
    state.upscaled_frame = (upscaled ? state.frame : 0u);
    state.previous_view_projection = camera.view_projection;
  }
  if (upscaled) {
    report(std::format("on, DLSS {}x{} to {}x{} (Resolution Scale below 100%).", width, height, output[0], output[1]));
  } else {
    report(std::format("on, {}x{}{}.", width, height, (below ? " (below 100% with ray tracing: Feral's upscaler after)" : "")));
  }
  twattila::frame_trace::Mark(std::format("DLAA {}x{} to {}x{} jitter {} {}{}", width, height, output[0], output[1], camera.jitter[0],
                                          camera.jitter[1], (continued ? "" : " reset")));
}

// Feral's upscaler (below 100%; its pixel shaders): DLSS's result this frame copied into its target in its place (true:
// the game's draw skipped). Its target's size noted for the next frame's DLSS.
inline bool Upscale(reshade::api::command_list* cmd_list) {
  using internal::objects;
  using internal::state;
  if (sliders.method == 0.f || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return false;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return false;
  ComPtr<ID3D11Resource> target_resource;
  target_view->GetResource(target_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target;
  if (FAILED(target_resource.As(&target))) return false;
  D3D11_TEXTURE2D_DESC target_desc = {};
  target->GetDesc(&target_desc);
  {
    const std::unique_lock lock(state.mutex);
    state.upscaler = {target_desc.Width, target_desc.Height};
    state.upscaler_frame = state.frame;
    if (state.upscaled_frame != state.frame) return false;
  }
  D3D11_TEXTURE2D_DESC result_desc = {};
  if (objects.result) objects.result->GetDesc(&result_desc);
  const bool fp16_target = target_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || target_desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS;
  if (!fp16_target || result_desc.Width != target_desc.Width || result_desc.Height != target_desc.Height || target_desc.SampleDesc.Count != 1) {
    return false;
  }
  native->CopySubresourceRegion(target.Get(), 0, 0, 0, 0, objects.result.Get(), 0, nullptr);
  return true;
}

inline void Install() {
  reshade::register_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
  reshade::register_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
  reshade::unregister_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
}

}  // namespace twromeremastered::dlaa
