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
#include "../twattila/reflex.hpp"

// DLAA, FSR and XeSS at 100% through the TW helper's bridge (Graphics, off by default; divinity2.tsv D2-9). The game has
// no TAA, jitter or motion vectors (FXAA and SMAA only, EngineShaders.pak). R1 / R1b (level A): every scene draw of the
// main view reads PerView at b12 (GlobalConstants_DX11.shdh: View c0-3, Projection c4-7, ViewProjection c8-11, rows acting
// on column vectors as the vertex shaders' dot products; ViewPos c12, ViewInfo c13 = far, near, width, height); the depth
// (D24S8, 0 at the near plane) is linearised by LinearizeDepth (CB4F88B7, t0); PPApplyLUT (C5E7DE4B, t01) reads the
// linear scene (R11G11B10F) at t0 and writes the PQ picture; SMAA (edges, weights 50B25933, blend) or FXAA (02DC3AD9) run
// on that PQ picture, CombineUI_HDR (2EDB0AFA, ui00) lays the UI over it. The inventory's portrait (610x900) draws the
// same chain first, with its own camera. Here: a Halton jitter written into every upload of the main camera, the camera's
// motion from the depth (aa_dlaa_prepare), the helper's DLAA on PPApplyLUT's input in its place (the result bound at its
// t0), the game's SMAA weights not drawn (its blend then copies) and FXAA copied, frame generation's HUD-less frame the
// picture before CombineUI_HDR. An orthographic camera gets no jitter: DLAA stays off there.
namespace divinity2::dlaa {

using Microsoft::WRL::ComPtr;

// The page's anti-aliasing rows (feature_settings::AppendAa, graphics_page.hpp): method 0 the game's, 5 DLSS (DLAA), 6
// FSR, 7 XeSS (the helper's, in DLSS's place); DLSS's preset, frame generation and Reflex.
static twattila::feature_settings::AaSliders sliders;
// Ray tracing (ray_tracing.hpp) is on: the main camera and the scene's depth are kept without DLAA too, the bridge made
// with its ray tracing textures and kept.
static bool rays = false;
// GT-VBGI or Bend (screen_space.hpp) want them too (the camera's rows and motion, the scene's depth).
static bool screen_effects = false;
// The rays want the G-buffer guides (Ray Reconstruction, the paths, the reflections) and the reflections' textures in the
// bridge (ray_tracing.hpp's present, as `rays`).
static bool guides = false;
static bool reflections = false;

namespace internal {

// DLAA, the rays or the screen-space effects want the main camera and the scene's depth.
inline bool CameraWanted() {
  return sliders.method != 0.f || rays || screen_effects;
}

// PerView (GlobalConstants_DX11.shdh): 15 registers; the buffer may be rounded up to 256 bytes.
constexpr UINT PER_VIEW_BYTES = 240;
constexpr size_t PROJECTION = 16;
constexpr size_t VIEW_PROJECTION = 32;
constexpr size_t VIEW_POSITION = 48;
constexpr size_t VIEW_INFO = 52;
constexpr uint32_t JITTER_PHASES = 8;
constexpr UINT SRV_COUNT = 7;  // aa_dlaa_finish's t0-t6
constexpr UINT UAV_COUNT = 3;  // aa_dlaa_prepare's u0-u2

// aa.hlsli's aa_settings (DLAA reads the metrics and the Debug View's).
struct ShaderSettings {
  std::array<float, 4> metrics = {};
  std::array<float, 12> smaa_and_cmaa2 = {};
  std::array<float, 4> dlaa_debug = {};
};

// aa.hlsli's aa_taa: matrices as the game's registers hold them (mul(float4(p, 1), m) of a column-major float4x4 is a dot
// product with each register, as the game's vertex shaders do with PerView's rows).
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

// The main view's PerView rows (acting on column vectors).
struct Camera {
  std::array<float, 16> view = {};
  std::array<float, 16> projection = {};                // without the jitter
  std::array<float, 16> view_projection = {};           // without the jitter
  std::array<float, 16> jittered_view_projection = {};  // as the depth was drawn
  std::array<float, 16> inverse_view_projection = {};   // of the jittered
  std::array<float, 3> position = {};
  std::array<float, 2> planes = {};  // ViewInfo's near and far
  std::array<UINT, 2> size = {};     // ViewInfo's width and height
  std::array<float, 2> jitter = {};  // written into this upload: pixels, x right, y up
  uint64_t frame = 0;
};

struct State {
  std::shared_mutex mutex;
  uint64_t frame = 1;
  std::array<float, 2> jitter = {};  // this frame's (0 unless DLAA ran the frame before)
  std::array<UINT, 2> display = {};  // the back buffer's at the last present (0: none yet)
  Camera camera;                     // the main view's last upload
  ComPtr<ID3D11ShaderResourceView> depth;
  std::array<UINT, 2> depth_size = {};
  uint64_t depth_frame = 0;
  std::array<float, 16> previous_view_projection = {};
  uint64_t evaluated_frame = 0;
  std::array<UINT, 2> evaluated_size = {};
  // The evaluated frame's camera motion for frame generation: the last frame's view-projection it went from, and whether
  // there was one (no reset).
  std::array<float, 16> evaluated_previous_view_projection = {};
  bool evaluated_continued = false;
  bool upload_seen = false;
  uint64_t guides_frame = 0;      // the rays wrote the G-buffer guides (NoteGuides)
  std::vector<std::string> said;  // each status line once
};
static State state;

// The device's objects: only on the game's render thread (PPApplyLUT's draw, destroy_device).
struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> prepare;
  ComPtr<ID3D11ComputeShader> finish;
  ComPtr<ID3D11Buffer> settings;
  ComPtr<ID3D11Buffer> taa;
  ComPtr<ID3D11Texture2D> result;
  ComPtr<ID3D11ShaderResourceView> result_srv;
  ComPtr<ID3D11UnorderedAccessView> result_uav;
  // PPApplyLUT's own input while the result is bound in its place (OnApplyLutDrawn binds it back).
  ComPtr<ID3D11ShaderResourceView> game_input;
};
static Objects objects;

static thread_local ID3D11Buffer* mapped_buffer = nullptr;
static thread_local float* mapped_data = nullptr;

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
      .view_projection = camera.view_projection,
      .inverse_view_projection = camera.inverse_view_projection,
      .previous_view_projection = (continued ? previous_view_projection : camera.view_projection),
      .reset = (continued ? 0.f : 1.f),
      // The camera's jitter (y up) as it moves the picture (y down).
      .jitter = {camera.jitter[0], -camera.jitter[1]},
  };
}

inline bool IsCameraBuffer(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 && (desc.ByteWidth == PER_VIEW_BYTES || desc.ByteWidth == 256);
}

// An upload into a PerView-sized constant buffer: the main view's camera (a perspective projection whose row 3 is
// (0, 0, 1, 0), ViewInfo's size the display's with the projection's aspect, rows 8-11 = rows 4-7 x rows 0-3) noted and
// jittered in place. The shadow cascades' views are orthographic, a light probe's and the portrait's another size.
inline void JitterCamera(float* c) {
  const std::unique_lock lock(state.mutex);
  const float* projection = c + PROJECTION;
  const float* view_info = c + VIEW_INFO;
  const float width = view_info[2];
  const float height = view_info[3];
  if (projection[12] != 0.f || projection[13] != 0.f || projection[14] != 1.f || projection[15] != 0.f || projection[0] <= 0.f
      || projection[5] <= 0.f || !(width >= 1.f) || !(height >= 1.f)
      || std::abs((projection[0] * width) - (projection[5] * height)) > 1e-3f * projection[5] * height) {
    return;
  }
  const auto [display_width, display_height] = state.display;
  if (display_width != 0 && (static_cast<float>(display_width) != width || static_cast<float>(display_height) != height)) return;
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) {
      float product = 0.f;
      for (size_t k = 0; k < 4; ++k) product += projection[(row * 4) + k] * c[(k * 4) + column];
      const float stored = c[VIEW_PROJECTION + (row * 4) + column];
      if (std::abs(product - stored) > 1e-3f * (1.f + std::abs(stored))) return;
    }
  }
  if (!std::exchange(state.upload_seen, true)) {
    renodx::utils::log::i(std::format("[Divinity 2] DLAA: the main camera's PerView upload found ({}x{}, near {} far {}).", width, height,
                                      view_info[1], view_info[0]));
  }
  auto& camera = state.camera;
  std::copy_n(c, 16, camera.view.begin());
  std::copy_n(c + PROJECTION, 16, camera.projection.begin());
  std::copy_n(c + VIEW_PROJECTION, 16, camera.view_projection.begin());
  camera.position = {c[VIEW_POSITION], c[VIEW_POSITION + 1], c[VIEW_POSITION + 2]};
  camera.planes = {view_info[1], view_info[0]};
  camera.size = {static_cast<UINT>(width), static_cast<UINT>(height)};
  camera.jitter = state.jitter;
  camera.frame = state.frame;
  // Clip x / y move by the jitter x w (in clip units: 2 / the size a pixel): rows 0 and 1 gain row 3 x it, the
  // projection's and the view-projection's.
  const float jitter_x = 2.f * state.jitter[0] / width;
  const float jitter_y = 2.f * state.jitter[1] / height;
  for (const size_t matrix : {PROJECTION, VIEW_PROJECTION}) {
    for (size_t column = 0; column < 4; ++column) {
      c[matrix + column] += jitter_x * c[matrix + 12 + column];
      c[matrix + 4 + column] += jitter_y * c[matrix + 12 + column];
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
    for (size_t column = 0; column < 4; ++column) camera.inverse_view_projection[(row * 4) + column] = static_cast<float>(rows[row][4 + column]);
  }
}

inline void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t,
                              reshade::api::map_access access, void** data) {
  if (!CameraWanted() || access == reshade::api::map_access::read_only || offset != 0 || data == nullptr || *data == nullptr
      || device->get_api() != reshade::api::device_api::d3d11) {
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
inline bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset,
                                 uint64_t size) {
  if (!CameraWanted() || data == nullptr || offset != 0 || size < PER_VIEW_BYTES || device->get_api() != reshade::api::device_api::d3d11) {
    return false;
  }
  auto* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
  if (!IsCameraBuffer(buffer)) return false;
  std::array<float, PER_VIEW_BYTES / 4> camera;
  std::memcpy(camera.data(), data, PER_VIEW_BYTES);
  JitterCamera(camera.data());
  if (std::memcmp(camera.data(), data, PER_VIEW_BYTES) == 0) return false;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  ComPtr<ID3D11DeviceContext> context;
  native->GetImmediateContext(context.GetAddressOf());
  // The game's bytes past PerView (a 256-byte buffer's last 16) as it gave them.
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  std::array<uint8_t, 256> upload = {};
  std::memcpy(upload.data(), data, std::min<size_t>(desc.ByteWidth, static_cast<size_t>(size)));
  std::memcpy(upload.data(), camera.data(), PER_VIEW_BYTES);
  context->UpdateSubresource(buffer, 0, nullptr, upload.data(), 0, 0);
  return true;
}

// The next frame's jitter: Halton (2, 3) over 8 frames, centred, only after a frame DLAA ran (a frame without it would
// shake); the display's size from the back buffer (R1: the scene draws at the window's size, the portrait's smaller).
inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
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
    if (swapchain != nullptr) {
      const reshade::api::resource_desc desc = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
      state.display = {desc.texture.width, desc.texture.height};
    }
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
  twattila::dlaa_bridge::OnDestroyDevice(native);
}

// The immediate context of a D3D11 command list (null: another API or a deferred context).
inline ID3D11DeviceContext* ImmediateContext(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  return (native->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE ? native : nullptr);
}

// A view's texture size (0 x 0: none or not a 2D texture).
inline D3D11_TEXTURE2D_DESC TextureOf(ID3D11View* view) {
  D3D11_TEXTURE2D_DESC desc = {};
  if (view == nullptr) return desc;
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
  return desc;
}

}  // namespace internal

// LinearizeDepth (CB4F88B7, before its draw): the scene's depth at its t0 kept for this frame's DLAA and the rays (the
// main view's: the display's size once known; the portrait's is another).
inline void KeepDepth(reshade::api::command_list* cmd_list) {
  if (!internal::CameraWanted()) return;
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  ComPtr<ID3D11ShaderResourceView> depth;
  native->PSGetShaderResources(0, 1, depth.GetAddressOf());
  const D3D11_TEXTURE2D_DESC depth_desc = internal::TextureOf(depth.Get());
  if (depth_desc.Width == 0 || depth_desc.SampleDesc.Count != 1) return;
  const std::unique_lock lock(internal::state.mutex);
  const auto [width, height] = internal::state.display;
  if (width != 0 && (depth_desc.Width != width || depth_desc.Height != height)) return;
  internal::state.depth = depth;
  internal::state.depth_size = {depth_desc.Width, depth_desc.Height};
  internal::state.depth_frame = internal::state.frame;
}

// The bridge for `width` x `height` as DLAA, the rays and frame generation want it (one set of arguments for all: any
// other makes its textures again). The page's 6 and 7 are the helper's FSR and XeSS (dlaa_bridge::UPSCALER_*: the method
// less 5, as Attila's).
inline bool PrepareBridge(ID3D11Device* device, UINT width, UINT height) {
  // AaSettings' method: FSR in DLSS's place when DLSS could not start on this GPU.
  const auto aa = twattila::feature_settings::AaSettings(sliders);
  return twattila::dlaa_bridge::Prepare(device, width, height, rays && guides, rays, twattila::frame_generation::Format(), reflections,
                                        twattila::anti_aliasing::UpscalerOf(aa.method), aa.frame_backend);
}

// The scene's depth as kept this frame (null: none yet) and its size.
inline ComPtr<ID3D11ShaderResourceView> SceneDepth() {
  const std::shared_lock lock(internal::state.mutex);
  return (internal::state.depth_frame == internal::state.frame) ? internal::state.depth : nullptr;
}

inline std::array<UINT, 2> SceneSize() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.depth_size;
}

// The rays wrote this frame's G-buffer guides (Ray Reconstruction may run on them at PPApplyLUT).
inline void NoteGuides() {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.guides_frame = internal::state.frame;
}

// The main view's camera uploaded this frame; false: none yet this frame.
inline bool MainCamera(internal::Camera* camera) {
  const std::shared_lock lock(internal::state.mutex);
  if (internal::state.camera.frame != internal::state.frame) return false;
  *camera = internal::state.camera;
  return true;
}

// DLAA ran this frame on a picture of `width` x `height`.
inline bool Evaluated(UINT width, UINT height) {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.evaluated_frame == internal::state.frame && internal::state.evaluated_size[0] == width
         && internal::state.evaluated_size[1] == height;
}

// PPApplyLUT (before its draw): DLAA on its input (t0, the linear scene), the result bound at t0 in its place.
inline void OnApplyLut(reshade::api::command_list* cmd_list) {
  using internal::objects;
  using internal::state;
  if (sliders.method == 0.f) return;
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  ComPtr<ID3D11ShaderResourceView> input;
  native->PSGetShaderResources(0, 1, input.GetAddressOf());
  const D3D11_TEXTURE2D_DESC target_desc = internal::TextureOf(target_view.Get());
  const D3D11_TEXTURE2D_DESC input_desc = internal::TextureOf(input.Get());
  const UINT width = target_desc.Width;
  const UINT height = target_desc.Height;
  if (width == 0 || input_desc.Width != width || input_desc.Height != height) return;

  const auto report = [&](const std::string& status) {
    const std::unique_lock lock(state.mutex);
    if (std::ranges::find(state.said, status) != state.said.end()) return;
    state.said.push_back(status);
    renodx::utils::log::i("[Divinity 2] DLAA: ", status);
  };

  // This frame's camera and depth under the lock; the work without it (the helper's answer may take a while).
  internal::Camera camera;
  ComPtr<ID3D11ShaderResourceView> depth;
  std::array<float, 16> previous_view_projection = {};
  bool continued = false;
  bool camera_seen = false;
  bool guides_written = false;
  {
    const std::unique_lock lock(state.mutex);
    camera = state.camera;
    camera_seen = camera.frame == state.frame && camera.size[0] == width && camera.size[1] == height;
    // The portrait's chain comes first in a frame: only a picture the main camera's size is the scene's.
    if (!camera_seen) return;
    if (state.depth_frame == state.frame && state.depth_size[0] == width && state.depth_size[1] == height) depth = state.depth;
    previous_view_projection = state.previous_view_projection;
    continued = state.evaluated_frame + 1 == state.frame;
    guides_written = state.guides_frame == state.frame;
  }
  if (!depth) {
    report("off: no scene depth this frame (LinearizeDepth didn't run).");
    return;
  }
  if (input_desc.SampleDesc.Count != 1 || (input_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
    report("off: PPApplyLUT's input can't be read.");
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
  if (result_desc.Width != width || result_desc.Height != height) {
    objects.result.Reset();
    objects.result_srv.Reset();
    objects.result_uav.Reset();
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = width,
        .Height = height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
    };
    if (FAILED(device->CreateTexture2D(&desc, nullptr, objects.result.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(objects.result.Get(), nullptr, objects.result_srv.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(objects.result.Get(), nullptr, objects.result_uav.GetAddressOf()))) {
      objects.result.Reset();
      objects.result_srv.Reset();
      objects.result_uav.Reset();
      report("off: its result texture couldn't be made.");
      return;
    }
  }
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (!PrepareBridge(device.Get(), width, height)) {
    report(std::format("off: {}", bridge.status));
    return;
  }

  // DLSS's presets L and M crush dark tones on a linear frame: its square root for them (Attila's c243f7c4). FSR and XeSS
  // take the frame as it is, and the camera's near, far and field of view from its projection (row vectors: the rows'
  // transpose, without the jitter).
  const auto aa = twattila::feature_settings::AaSettings(sliders);
  const bool dlss = aa.method == twattila::anti_aliasing::Method::DLAA;
  const uint32_t preset = aa.dlaa_preset;
  const bool square_root = dlss && (preset == 12u || preset == 13u);
  const float width_f = static_cast<float>(width);
  const float height_f = static_cast<float>(height);
  const internal::ShaderSettings shader_settings = {
      .metrics = {1.f / width_f, 1.f / height_f, width_f, height_f},
      // The DLSS Debug View: its Motion Vectors (1) alone here (no object motion).
      .dlaa_debug = {(sliders.debug_view != 0.f ? 1.f : 0.f), width_f, height_f, (square_root ? 1.f : 0.f)},
  };
  native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &shader_settings, 0, 0);
  const internal::TaaConstants taa = internal::MakeTaaConstants(camera, previous_view_projection, continued);
  native->UpdateSubresource(objects.taa.Get(), 0, nullptr, &taa, 0, 0);

  // The game's compute state and targets kept; the inputs read while no render target or depth view holds them.
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
  bind(objects.prepare.Get(), {input.Get(), depth.Get()}, {bridge.color.uav.Get(), bridge.depth.uav.Get(), bridge.motion.uav.Get()});
  native->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
  bind(nullptr, {}, {});
  const std::array<float, 16> view_to_clip = internal::Transposed(camera.projection);
  // Ray Reconstruction only on the guides the rays wrote this frame (ray_tracing.hpp), the camera as row vectors.
  const twattila::dlaa_bridge::RayReconstruction reconstruction = {
      .preset = aa.ray_reconstruction_preset,
      .world_to_view = internal::Transposed(camera.view),
      .view_to_clip = view_to_clip,
  };
  const bool reconstructing = aa.ray_reconstruction && guides_written && bridge.guides;
  const bool resolved = twattila::dlaa_bridge::Evaluate(native, width, height, camera.jitter[0], -camera.jitter[1], !continued, preset,
                                                        (reconstructing ? &reconstruction : nullptr), (dlss ? nullptr : &view_to_clip),
                                                        {.fsr_sharpness = aa.fsr_sharpness});
  if (resolved) {
    bind(objects.finish.Get(), {bridge.output.srv.Get(), input.Get(), bridge.motion.srv.Get()}, {objects.result_uav.Get()});
    native->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    bind(nullptr, {}, {});
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
  // The result in place of PPApplyLUT's input for its draw (OnApplyLutDrawn binds the game's back).
  objects.game_input = input;
  ID3D11ShaderResourceView* result = objects.result_srv.Get();
  native->PSSetShaderResources(0, 1, &result);
  {
    const std::unique_lock lock(state.mutex);
    state.evaluated_frame = state.frame;
    state.evaluated_size = {width, height};
    state.evaluated_previous_view_projection = taa.previous_view_projection;
    state.evaluated_continued = continued;
    state.previous_view_projection = camera.view_projection;
  }
  report(std::format("on, {}x{}.", width, height));
}

// PPApplyLUT (after its draw): its own input bound again; frame generation keeps this frame's inputs (its HUD-less image
// from PPApplyLUT's output until KeepPicture takes the one before the UI).
inline void OnApplyLutDrawn(reshade::api::command_list* cmd_list) {
  using internal::objects;
  if (!objects.game_input) return;
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  ID3D11ShaderResourceView* input = objects.game_input.Get();
  native->PSSetShaderResources(0, 1, &input);
  objects.game_input.Reset();
  if (!twattila::frame_generation::Wanted()) return;
  internal::Camera camera;
  std::array<float, 16> previous_view_projection = {};
  bool continued = false;
  {
    const std::shared_lock lock(internal::state.mutex);
    if (internal::state.evaluated_frame != internal::state.frame) return;
    camera = internal::state.camera;
    previous_view_projection = internal::state.evaluated_previous_view_projection;
    continued = internal::state.evaluated_continued;
  }
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  ComPtr<ID3D11Resource> target_resource;
  if (target_view) target_view->GetResource(target_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target;
  if (!target_resource || FAILED(target_resource.As(&target))) return;
  twattila::frame_generation::Inputs inputs = {
      .width = camera.size[0],
      .height = camera.size[1],
      .jitter_x = camera.jitter[0],
      .jitter_y = -camera.jitter[1],
      .reset = !continued,
      .near_plane = camera.planes[0],
      .far_plane = camera.planes[1],
  };
  // The rows as the registers hold them (column vectors), without the jitter.
  twattila::frame_generation::SetCamera(&inputs, camera.projection, camera.view, camera.view_projection, previous_view_projection,
                                        camera.position);
  twattila::frame_generation::KeepFrame(native, target.Get(), inputs);
}

// CombineUI_HDR (before its draw): the PQ picture at its t0 (the scene with the sight, fog of war and anti-aliasing, no UI)
// as frame generation's HUD-less image.
inline void KeepPicture(reshade::api::command_list* cmd_list) {
  if (sliders.method == 0.f || !twattila::frame_generation::Wanted()) return;
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  ComPtr<ID3D11ShaderResourceView> picture;
  native->PSGetShaderResources(0, 1, picture.GetAddressOf());
  if (!picture) return;
  ComPtr<ID3D11Resource> resource;
  picture->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  if (FAILED(resource.As(&texture))) return;
  const D3D11_TEXTURE2D_DESC desc = internal::TextureOf(picture.Get());
  if (!Evaluated(desc.Width, desc.Height)) return;
  twattila::frame_generation::KeepOutput(native, texture.Get());
}

// The game's SMAA weights pass (50B25933) of the DLAA'd picture: not drawn (true), its target stays cleared, so the
// neighbourhood blend copies the picture.
inline bool SkipSmaaWeights(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr || sliders.method == 0.f) return false;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const D3D11_TEXTURE2D_DESC desc = internal::TextureOf(target_view.Get());
  return Evaluated(desc.Width, desc.Height);
}

// The game's FXAA (02DC3AD9) of the DLAA'd picture: its input copied into its target in its place (true: not drawn);
// the game's draw where the two textures differ in size or format.
inline bool SkipFxaa(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr || sliders.method == 0.f) return false;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  ComPtr<ID3D11ShaderResourceView> input;
  native->PSGetShaderResources(0, 1, input.GetAddressOf());
  if (!target_view || !input) return false;
  const D3D11_TEXTURE2D_DESC target_desc = internal::TextureOf(target_view.Get());
  const D3D11_TEXTURE2D_DESC input_desc = internal::TextureOf(input.Get());
  if (!Evaluated(target_desc.Width, target_desc.Height)) return false;
  if (input_desc.Width != target_desc.Width || input_desc.Height != target_desc.Height || input_desc.Format != target_desc.Format
      || input_desc.MipLevels != target_desc.MipLevels || input_desc.ArraySize != target_desc.ArraySize) {
    return false;
  }
  ComPtr<ID3D11Resource> target;
  target_view->GetResource(target.GetAddressOf());
  ComPtr<ID3D11Resource> source;
  input->GetResource(source.GetAddressOf());
  if (target == source) return false;
  native->CopyResource(target.Get(), source.Get());
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

}  // namespace divinity2::dlaa
