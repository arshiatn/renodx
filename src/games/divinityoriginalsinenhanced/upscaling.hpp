/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/upscaling.hpp"
#include "../twattila/dlaa_bridge.hpp"
#include "../twattila/frame_generation.hpp"

// The helper's upscalers at 100% (Experimental): DLAA, FSR or XeSS (renodx-tw-dlaa64.exe next to the add-on, its own
// D3D12 device; twattila/dlaa_bridge.hpp) in place of the game's FXAA / SMAA, which run on the tone-mapped picture. The
// game has no jitter and no motion vectors: the main view's camera (b12 PerView, GlobalConstants_DX11.shdh: a perspective
// projection at the display's size) is jittered as the game uploads it (Map / Unmap or UpdateSubresource; Halton (2, 3)
// over 8 frames), its depth comes from gBuffer1's linear depth (kept at the sun's light pass 0xCCCB7253, t1) and the
// camera's motion from that depth and both frames' cameras without the jitter (d1_upscale_inputs). At the tone mapper's
// draw (0x30A1D40D) its Base (t0: the exposed scene, bloom and god rays in, linear) goes through the upscaler and the
// result is written back into it (d1_upscale_output) before the tone mapper reads it: everything after it (fog of war,
// outlines, the UI) is the game's. Frame generation's HUD-less image is the back buffer before CombineUI (0x20163A55).
// Every view's upload is kept (as uploaded, the jitter in) for the rays' capture camera (ray_tracing.hpp). The device is
// single-threaded (D3D11_CREATE_DEVICE_SINGLETHREADED): everything here runs on the game's thread, in its callbacks.
namespace divinity::upscaling {

using Microsoft::WRL::ComPtr;

// What the page asks for, set each present (addon.cpp).
struct Settings {
  bool upscaling = false;  // an upscaler chosen and the helper next to the add-on
  uint32_t preset = 0;     // DLSS's NVSDK_NGX_DLSS_Hint_Render_Preset
  // The colour's square root (DLSS's L and M presets and Ray Reconstruction crush the darks of linear HDR: memory "DLSS
  // L/M dark tones"), squared back after.
  bool square_root = false;
  bool ray_reconstruction = false;
  uint32_t ray_reconstruction_preset = 0;
  bool debug_view = false;    // the Upscaling Debug View: the motion the upscaler gets
  float fsr_sharpness = 0.f;  // FSR's RCAS, 0-1 (0: off)
};

// The main view's camera as the game uploaded it, without the jitter (b12's rows, column vectors).
struct Camera {
  std::array<float, 16> view = {};
  std::array<float, 16> projection = {};
  std::array<float, 16> view_projection = {};
  std::array<float, 4> position = {};  // ViewPos (w: the wind speed)
  std::array<float, 4> info = {};      // ViewInfo: far, near, width, height
};

namespace internal {

// b12 PerView: row_major View c0-c3, Projection c4-c7, ViewProjection c8-c11, ViewPos c12, ViewInfo c13.
constexpr UINT PER_VIEW_BYTES = 224;
constexpr size_t PROJECTION = 4;
constexpr size_t VIEW_PROJECTION = 8;
constexpr size_t VIEW_POSITION = 12;
constexpr size_t VIEW_INFO = 13;
constexpr uint32_t JITTER_PHASES = 8;  // DLAA's (NVIDIA: 8 x (output / input)^2)
constexpr size_t MAX_UPLOADS = 32;     // b12 buffers kept for the capture camera

// A b12 buffer as last uploaded (the jitter in): its view-projection and camera position.
struct Upload {
  ID3D11Buffer* buffer = nullptr;  // compared only
  std::array<float, 16> view_projection = {};
  std::array<float, 3> position = {};
};

struct InputConstants {
  std::array<float, 16> inverse_view;
  std::array<float, 16> previous_view_projection;
  std::array<float, 4> size;
  std::array<float, 4> projection;
  std::array<float, 4> jitter;
  std::array<float, 4> lens;
};

struct State {
  std::mutex mutex;
  Settings settings;
  uint64_t frame = 0;
  std::array<UINT, 2> display = {};  // the swap chain's back buffer
  std::array<float, 2> jitter = {};  // this frame's, pixels (x right, y up); none while no result comes
  uint32_t phase = 0;
  Camera camera;
  uint64_t camera_frame = UINT64_MAX;  // the frame `camera` was uploaded in
  std::vector<Upload> uploads;
  // The last evaluation's frame and camera (this frame's motion goes from it); a frame without one restarts the history.
  uint64_t evaluated_frame = UINT64_MAX;
  std::array<float, 16> previous_view_projection = {};
  ComPtr<ID3D11ShaderResourceView> linear_depth;  // gBuffer1, kept at the sun's light pass
  uint64_t linear_depth_frame = UINT64_MAX;
  // Frame generation's inputs of the last evaluation (KeepFrame at CombineUI).
  twattila::frame_generation::Inputs generation_inputs;
  uint64_t generation_frame = UINT64_MAX;
  std::string status = "Upscaling off";
  uint32_t changes_logged = 0;  // the result's comings and goings logged this start (a few, never a line a frame)
  // The device's own objects.
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> inputs;
  ComPtr<ID3D11VertexShader> fullscreen;
  ComPtr<ID3D11PixelShader> output;
  ComPtr<ID3D11Buffer> input_constants;
  ComPtr<ID3D11Buffer> output_constants;
  std::unordered_map<ID3D11Resource*, ComPtr<ID3D11RenderTargetView>> scene_targets;
  bool failed = false;
};
static State state;
// Upload hooks act only while the upscaler or the rays want the cameras (one atomic on every Map of every buffer).
static std::atomic_bool watching = false;
// Map's pointer until its Unmap (the game's thread).
static ID3D11Buffer* mapped_buffer = nullptr;
static std::byte* mapped_data = nullptr;

inline float Halton(uint32_t index, uint32_t base) {
  float result = 0.f;
  float fraction = 1.f / static_cast<float>(base);
  for (uint32_t i = index; i > 0; i /= base, fraction /= static_cast<float>(base)) {
    result += fraction * static_cast<float>(i % base);
  }
  return result;
}

inline bool PerViewSized(ID3D11Buffer* buffer) {
  if (buffer == nullptr) return false;
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 && desc.ByteWidth == PER_VIEW_BYTES;
}

// An upload of `data` (PER_VIEW_BYTES) into `buffer`: kept as uploaded; the main view (a perspective projection, its last
// row 0 0 1 0, at the display's size; shadow cascades are orthographic, point light shadows 512 x 512) noted without the
// jitter and jittered while the upscaler gives results. True: `data` changed.
inline bool OnPerView(ID3D11Buffer* buffer, std::byte* data) {
  std::array<float, PER_VIEW_BYTES / 4> c;
  std::memcpy(c.data(), data, PER_VIEW_BYTES);
  const auto reg = [&c](size_t row, size_t column) -> float& { return c[(row * 4) + column]; };
  const std::unique_lock lock(state.mutex);
  const bool perspective = reg(PROJECTION + 3, 0) == 0.f && reg(PROJECTION + 3, 1) == 0.f && reg(PROJECTION + 3, 2) == 1.f
                           && reg(PROJECTION + 3, 3) == 0.f;
  const bool main_view = perspective && state.display[0] != 0 && reg(VIEW_INFO, 2) == static_cast<float>(state.display[0])
                         && reg(VIEW_INFO, 3) == static_cast<float>(state.display[1]);
  bool jittered = false;
  if (main_view) {
    std::copy_n(c.begin(), 16, state.camera.view.begin());
    std::copy_n(c.begin() + (PROJECTION * 4), 16, state.camera.projection.begin());
    std::copy_n(c.begin() + (VIEW_PROJECTION * 4), 16, state.camera.view_projection.begin());
    std::copy_n(c.begin() + (VIEW_POSITION * 4), 4, state.camera.position.begin());
    std::copy_n(c.begin() + (VIEW_INFO * 4), 4, state.camera.info.begin());
    state.camera_frame = state.frame;
    // Clip x / y move by the jitter x w: the projection's row 0 / 1 in its z column, the view-projection's rows by its w row.
    if (state.jitter[0] != 0.f || state.jitter[1] != 0.f) {
      const float jitter_x = 2.f * state.jitter[0] / static_cast<float>(state.display[0]);
      const float jitter_y = 2.f * state.jitter[1] / static_cast<float>(state.display[1]);
      reg(PROJECTION, 2) += jitter_x;
      reg(PROJECTION + 1, 2) += jitter_y;
      for (size_t column = 0; column < 4; ++column) {
        reg(VIEW_PROJECTION, column) += jitter_x * reg(VIEW_PROJECTION + 3, column);
        reg(VIEW_PROJECTION + 1, column) += jitter_y * reg(VIEW_PROJECTION + 3, column);
      }
      jittered = true;
    }
  }
  auto upload = std::ranges::find(state.uploads, buffer, &Upload::buffer);
  if (upload == state.uploads.end() && state.uploads.size() < MAX_UPLOADS) {
    upload = state.uploads.insert(state.uploads.end(), Upload{.buffer = buffer});
  }
  if (upload != state.uploads.end()) {
    std::copy_n(c.begin() + (VIEW_PROJECTION * 4), 16, upload->view_projection.begin());
    upload->position = {reg(VIEW_POSITION, 0), reg(VIEW_POSITION, 1), reg(VIEW_POSITION, 2)};
  }
  if (jittered) std::memcpy(data, c.data(), PER_VIEW_BYTES);
  return jittered;
}

inline void OnMapBufferRegion(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t,
                              reshade::api::map_access access, void** data) {
  if (!watching.load(std::memory_order_relaxed) || access == reshade::api::map_access::read_only || offset != 0 || data == nullptr
      || *data == nullptr) {
    return;
  }
  auto* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
  if (!PerViewSized(buffer)) return;
  mapped_buffer = buffer;
  mapped_data = static_cast<std::byte*>(*data);
}

// Before the native Unmap, while the pointer is still valid.
inline void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  if (mapped_buffer == nullptr || mapped_buffer != reinterpret_cast<ID3D11Buffer*>(resource.handle)) return;
  OnPerView(mapped_buffer, mapped_data);
  mapped_buffer = nullptr;
  mapped_data = nullptr;
}

// UpdateSubresource: the main view's uploaded jittered instead (true: the game's update skipped).
inline bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset,
                                 uint64_t size) {
  if (!watching.load(std::memory_order_relaxed) || data == nullptr || offset != 0) return false;
  auto* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
  if (!PerViewSized(buffer) || (size != UINT64_MAX && size < PER_VIEW_BYTES)) return false;
  std::array<std::byte, PER_VIEW_BYTES> copy;
  std::memcpy(copy.data(), data, PER_VIEW_BYTES);
  if (!OnPerView(buffer, copy.data())) return false;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  ComPtr<ID3D11DeviceContext> context;
  native->GetImmediateContext(context.GetAddressOf());
  context->UpdateSubresource(buffer, 0, nullptr, copy.data(), 0, 0);
  return true;
}

// The device's own objects (once a device); false: not available.
inline bool PrepareDevice(ID3D11Device* device) {
  if (state.device.Get() == device) return !state.failed;
  state.device = device;
  state.scene_targets.clear();
  const D3D11_BUFFER_DESC input_desc = {.ByteWidth = sizeof(InputConstants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC output_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  state.failed = !talesofarise::dlss::MakeContextState(device, &state.private_state)
                 || FAILED(device->CreateComputeShader(__d1_upscale_inputs.data(), __d1_upscale_inputs.size(), nullptr,
                                                       state.inputs.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr,
                                                      state.fullscreen.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__d1_upscale_output.data(), __d1_upscale_output.size(), nullptr,
                                                     state.output.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&input_desc, nullptr, state.input_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&output_desc, nullptr, state.output_constants.ReleaseAndGetAddressOf()));
  if (state.failed) renodx::utils::log::w("[Divinity] Upscaling: its shaders or states could not be made: off.");
  return !state.failed;
}

// The result coming or going (a few lines a start, never one a frame).
inline void LogChange(const std::string& change) {
  if (state.changes_logged > 8) return;
  ++state.changes_logged;
  renodx::utils::log::i("[Divinity] Upscaling: ", change, (state.changes_logged > 8 ? " (no more of these lines this start)" : ""));
}

}  // namespace internal

// Every present (addon.cpp): the page's settings, a new frame and its jitter (none while no result comes).
inline void Update(const Settings& settings, bool cameras_wanted) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  state.settings = settings;
  const bool resulted = settings.upscaling && state.evaluated_frame != UINT64_MAX && state.evaluated_frame + 1 >= state.frame;
  if (resulted) {
    state.phase = (state.phase + 1) % internal::JITTER_PHASES;
    state.jitter = {internal::Halton(state.phase + 1, 2) - 0.5f, internal::Halton(state.phase + 1, 3) - 0.5f};
  } else {
    state.jitter = {};
  }
  if (!settings.upscaling) state.status = "Upscaling off";
  ++state.frame;
  internal::watching.store(settings.upscaling || cameras_wanted, std::memory_order_relaxed);
}

inline void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  if (swapchain->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  const auto desc = swapchain->get_device()->get_resource_desc(swapchain->get_back_buffer(0));
  const std::unique_lock lock(internal::state.mutex);
  internal::state.display = {desc.texture.width, desc.texture.height};
  internal::state.evaluated_frame = UINT64_MAX;
  // The game's textures at the old size go with their views.
  internal::state.linear_depth.Reset();
  internal::state.scene_targets.clear();
}

// The main view's camera of this frame, without the jitter, and its jitter in NDC; false: none uploaded this frame.
inline bool MainCamera(Camera* camera, std::array<float, 2>* jitter_ndc) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.camera_frame != state.frame || state.display[0] == 0) return false;
  *camera = state.camera;
  *jitter_ndc = {2.f * state.jitter[0] / static_cast<float>(state.display[0]), 2.f * state.jitter[1] / static_cast<float>(state.display[1])};
  return true;
}

// `buffer`'s view as last uploaded (the jitter in, as its draws drew); false: not a kept b12 buffer.
inline bool UploadedView(ID3D11Buffer* buffer, std::array<float, 16>* view_projection, std::array<float, 3>* position) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  const auto upload = std::ranges::find(state.uploads, buffer, &internal::Upload::buffer);
  if (upload == state.uploads.end()) return false;
  *view_projection = upload->view_projection;
  *position = upload->position;
  return true;
}

namespace internal {

// The sun's light pass (0xCCCB7253, before its draw): its gBuffer1 (t1, linear depth), the upscaler's depth.
constexpr auto OnSunLight = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!watching.load(std::memory_order_relaxed)) return {};
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11ShaderResourceView> depth;
  if (context != nullptr) context->PSGetShaderResources(1, 1, depth.GetAddressOf());
  if (!depth) return {};
  const std::unique_lock lock(state.mutex);
  state.linear_depth = depth;
  state.linear_depth_frame = state.frame;
  return {};
};

}  // namespace internal

// The tone mapper's draw (0x30A1D40D, before it): its Base (t0) through the upscaler and back into it; the game's picture
// when anything is missing (no helper, no camera this frame, another size).
inline void OnTonemap(ID3D11DeviceContext* context) {
  auto& state = internal::state;
  if (!internal::watching.load(std::memory_order_relaxed) || context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  Settings settings;
  Camera camera;
  ComPtr<ID3D11ShaderResourceView> linear_depth;
  std::array<float, 2> jitter;
  std::array<UINT, 2> display;
  std::array<float, 16> previous_view_projection;
  uint64_t frame = 0;
  bool reset = true;
  {
    const std::unique_lock lock(state.mutex);
    if (!state.settings.upscaling || state.camera_frame != state.frame || state.linear_depth_frame != state.frame) return;
    settings = state.settings;
    camera = state.camera;
    linear_depth = state.linear_depth;
    jitter = state.jitter;
    display = state.display;
    previous_view_projection = state.previous_view_projection;
    frame = state.frame;
    reset = state.evaluated_frame == UINT64_MAX || state.evaluated_frame + 1 != state.frame;
  }
  ComPtr<ID3D11ShaderResourceView> scene_view;
  context->PSGetShaderResources(0, 1, scene_view.GetAddressOf());
  if (!scene_view) return;
  ComPtr<ID3D11Resource> scene;
  scene_view->GetResource(scene.GetAddressOf());
  ComPtr<ID3D11Texture2D> scene_texture;
  if (FAILED(scene.As(&scene_texture))) return;
  D3D11_TEXTURE2D_DESC scene_desc = {};
  scene_texture->GetDesc(&scene_desc);
  if (scene_desc.Width != display[0] || scene_desc.Height != display[1] || (scene_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) return;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()))) || !internal::PrepareDevice(device.Get())) return;
  if (!talesofarise::upscaling::PrepareBridge(device.Get())) {
    const std::unique_lock lock(state.mutex);
    if (state.evaluated_frame != UINT64_MAX) internal::LogChange("stopped (the helper is gone): the game's picture until it is back");
    state.evaluated_frame = UINT64_MAX;
    state.status = std::string(twattila::dlaa_bridge::bridge.status);
    return;
  }
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (bridge.width < display[0] || bridge.height < display[1]) return;
  auto& target = state.scene_targets[scene.Get()];
  if (!target) {
    const D3D11_RENDER_TARGET_VIEW_DESC target_desc = {.Format = scene_desc.Format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
    if (FAILED(device->CreateRenderTargetView(scene.Get(), &target_desc, target.GetAddressOf()))) {
      state.scene_targets.erase(scene.Get());
      return;
    }
  }

  // The projection's depth terms (A + B / z), the far plane (ViewInfo.x).
  const float depth_a = camera.projection[10];
  const float depth_b = camera.projection[11];
  const std::array<float, 2> jitter_ndc = {2.f * jitter[0] / static_cast<float>(display[0]), 2.f * jitter[1] / static_cast<float>(display[1])};
  // Around this frame's camera (the world's coordinates reach ~600 here: last frame's projection of them cancels in
  // floats, 0.09 px at 4K): the inverse view without its translation, last frame's view-projection moved to this
  // camera in doubles.
  std::array<float, 16> inverse_view = talesofarise::frame_generation::Inverse(camera.view);
  std::array<float, 16> previous_relative = (reset ? camera.view_projection : previous_view_projection);
  for (size_t row = 0; row < 4; ++row) {
    double translation = previous_relative[(row * 4) + 3];
    for (size_t column = 0; column < 3; ++column) {
      translation += static_cast<double>(previous_relative[(row * 4) + column]) * static_cast<double>(inverse_view[(column * 4) + 3]);
    }
    previous_relative[(row * 4) + 3] = static_cast<float>(translation);
  }
  for (size_t row = 0; row < 3; ++row) inverse_view[(row * 4) + 3] = 0.f;
  const internal::InputConstants inputs = {
      .inverse_view = inverse_view,
      .previous_view_projection = previous_relative,
      .size = {static_cast<float>(display[0]), static_cast<float>(display[1]), 1.f / static_cast<float>(display[0]),
               1.f / static_cast<float>(display[1])},
      .projection = {depth_a, depth_b, camera.info[0], settings.square_root ? 1.f : 0.f},
      .jitter = {jitter_ndc[0], jitter_ndc[1], reset ? 1.f : 0.f, 0.f},
      .lens = {camera.projection[0], camera.projection[5], camera.projection[2], camera.projection[6]},
  };
  twattila::dlaa_bridge::BeforeSharedWrites(context);
  {
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    context->UpdateSubresource(state.input_constants.Get(), 0, nullptr, &inputs, 0, 0);
    ID3D11ShaderResourceView* views[] = {scene_view.Get(), linear_depth.Get()};
    ID3D11UnorderedAccessView* outputs[] = {bridge.color.uav.Get(), bridge.depth.uav.Get(), bridge.motion.uav.Get()};
    context->CSSetShader(state.inputs.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, views);
    context->CSSetUnorderedAccessViews(0, 3, outputs, nullptr);
    context->CSSetConstantBuffers(0, 1, state.input_constants.GetAddressOf());
    context->Dispatch((display[0] + 7) / 8, (display[1] + 7) / 8, 1);
  }
  bridge.color_square_root = settings.square_root;
  // FSR, XeSS and Ray Reconstruction take the camera as row vectors: the registers' transposes.
  std::array<float, 16> view_to_clip = {};
  std::array<float, 16> world_to_view = {};
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) {
      view_to_clip[(column * 4) + row] = camera.projection[(row * 4) + column];
      world_to_view[(column * 4) + row] = camera.view[(row * 4) + column];
    }
  }
  // Ray Reconstruction only with the rays' guides written this frame (ray_tracing.hpp).
  const twattila::dlaa_bridge::RayReconstruction reconstruction = {
      .preset = settings.ray_reconstruction_preset,
      .world_to_view = world_to_view,
      .view_to_clip = view_to_clip,
  };
  const bool reconstructing = settings.ray_reconstruction && talesofarise::upscaling::Guides() && bridge.guides;
  // The camera's jitter (y up) as it moves the picture (y down).
  const bool evaluated = twattila::dlaa_bridge::Evaluate(context, display[0], display[1], jitter[0], -jitter[1], reset, settings.preset,
                                                         reconstructing ? &reconstruction : nullptr, &view_to_clip,
                                                         {.fsr_sharpness = settings.fsr_sharpness});
  if (evaluated) {
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    const std::array<float, 4> output_settings = {settings.square_root ? 1.f : 0.f, settings.debug_view ? 1.f : 0.f, 0.f, 0.f};
    context->UpdateSubresource(state.output_constants.Get(), 0, nullptr, output_settings.data(), 0, 0);
    const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(display[0]), .Height = static_cast<float>(display[1]), .MaxDepth = 1.f};
    ID3D11RenderTargetView* targets[] = {target.Get()};
    ID3D11ShaderResourceView* views[] = {bridge.output.srv.Get(), bridge.motion.srv.Get(), bridge.color.srv.Get()};
    context->OMSetRenderTargets(1, targets, nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(state.fullscreen.Get(), nullptr, 0);
    context->PSSetShader(state.output.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 3, views);
    context->PSSetConstantBuffers(0, 1, state.output_constants.GetAddressOf());
    context->Draw(3, 0);
  }
  twattila::frame_generation::Inputs generation = {
      .width = display[0],
      .height = display[1],
      .jitter_x = jitter[0],
      .jitter_y = -jitter[1],
      .reset = reset,
      .near_plane = camera.info[1],
      .far_plane = camera.info[0],
  };
  twattila::frame_generation::SetCamera(&generation, camera.projection, camera.view, camera.view_projection,
                                        reset ? camera.view_projection : previous_view_projection,
                                        {camera.position[0], camera.position[1], camera.position[2]});
  const std::unique_lock lock(state.mutex);
  if (evaluated == reset) {
    internal::LogChange(evaluated ? "applied (its result written back into the scene before the tone mapper)"
                                  : "gave no result: the game's picture until one comes");
  }
  state.status = std::string(bridge.status);
  if (!evaluated) {
    state.evaluated_frame = UINT64_MAX;
    return;
  }
  state.evaluated_frame = frame;
  state.previous_view_projection = camera.view_projection;
  state.generation_inputs = generation;
  state.generation_frame = frame;
}

// CombineUI's draw (0x20163A55, before it: the back buffer without the UI): frame generation's HUD-less image and the
// frame's inputs, when this frame was upscaled.
inline void OnCombineUi(ID3D11DeviceContext* context) {
  if (!internal::watching.load(std::memory_order_relaxed) || !twattila::frame_generation::Wanted() || context == nullptr
      || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
    return;
  }
  twattila::frame_generation::Inputs inputs;
  {
    const std::unique_lock lock(internal::state.mutex);
    if (internal::state.generation_frame != internal::state.frame) return;
    inputs = internal::state.generation_inputs;
  }
  ComPtr<ID3D11RenderTargetView> target_view;
  context->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target;
  if (SUCCEEDED(resource.As(&target))) twattila::frame_generation::KeepFrame(context, target.Get(), inputs);
}

// The page's status line: the helper's, or why nothing is upscaled.
inline std::string Status() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.status;
}

inline void Install() {
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
  renodx::utils::command_action::Register(internal::OnSunLight, {.shader_hash = 0xCCCB7253,
                                                                 .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunLight);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
  internal::watching.store(false);
}

// The game's device is going away: the add-on's objects on it with it, the bridge's too.
inline void Release(ID3D11Device* device) {
  auto& state = internal::state;
  {
    const std::unique_lock lock(state.mutex);
    state.linear_depth.Reset();
    state.uploads.clear();
    state.evaluated_frame = UINT64_MAX;
  }
  if (state.device.Get() == device) {
    state.device.Reset();
    state.private_state.Reset();
    state.inputs.Reset();
    state.fullscreen.Reset();
    state.output.Reset();
    state.input_constants.Reset();
    state.output_constants.Reset();
    state.scene_targets.clear();
    state.failed = false;
  }
  twattila::dlaa_bridge::OnDestroyDevice(device);
}

}  // namespace divinity::upscaling
