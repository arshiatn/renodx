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
#include <string_view>
#include <unordered_map>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/dlaa_bridge.hpp"
#include "../twattila/frame_generation.hpp"
#include "../twattila/frame_trace.hpp"
#include "../twattila/reflex.hpp"

// DLSS, FSR or XeSS in place of the game's TAA (it has no working DLSS of its own: preferences gfx_dlss never creates one):
// after its TAA resolve (cs 0x1ADBFF18 TAA / 0x6A8FC282 TAA High: t0 the G-buffer depth in x, t2 the velocity, t4 this
// frame's colour, jittered; u1 its result), the colour, depth and motion go through the upscaler in the TW helper
// (twattila/dlaa_bridge.hpp, renodx-tw-dlaa64.exe) and its result replaces the TAA's. The game's jitter (camera
// g_camera_jitter) and velocity (everything it draws) are its own. At the game's Resolution Scale of 100% only: below it
// the main view's camera never passes Upload's test, so the game's TAA and upscale stay and the helper doesn't start. In
// the addon's own context state (the game's put back after). Frame generation: the helper's too (twattila/frame_generation.hpp).
namespace twwarhammer3::taa_dlss {

using Microsoft::WRL::ComPtr;

namespace internal {

constexpr std::array<uint32_t, 2> TAA_SHADERS = {0x1ADBFF18u, 0x6A8FC282u};
// camera (1056 bytes): the view's registers c2-c5, the projection's c6-c9, g_screen_size c37, g_viewport_dimensions c39,
// g_camera_jitter (clip space) at float 257.
constexpr uint32_t CAMERA_BYTES = 66 * 16;
constexpr uint32_t CAMERA_MAX_BYTES = 2048;
constexpr size_t VIEW = 2 * 4;
constexpr size_t PROJECTION = 6 * 4;
constexpr size_t VIEW_PROJECTION = 10 * 4;
constexpr size_t INVERSE_PROJECTION = 26 * 4;
constexpr size_t NEAR_FAR = 34 * 4;
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
  ComPtr<ID3D11Texture2D> motion_view;   // the Motion Debug View's, while it is on
  ComPtr<ID3D11UnorderedAccessView> motion_view_uav;
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
  std::array<float, 16> previous_view_projection = {};  // the last evaluation's, without the jitter
  twattila::frame_generation::Inputs generation;        // this evaluation's for frame generation
  uint64_t generation_frame = UINT64_MAX;               // its frame, until the tone mapper's draw keeps it
  bool logged = false;
  bool no_camera_logged = false;
  // A hook's first run, in the log (one that never ran looks like one that did nothing).
  uint32_t taa_logged_hash = 0;
  bool camera_logged = false;
  bool guides_logged = false;
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
  const auto method = state.settings.method;
  const bool nvidia_model = method == twattila::anti_aliasing::Method::DLAA;
  if (!state.camera_seen) {
    state.no_camera_frame = state.frame;
    if (!state.no_camera_logged) {
      state.no_camera_logged = true;
      renodx::utils::log::i(std::format("[Warhammer III] Upscaling: the game's TAA ran without the main view's camera (colour {}x{}, "
                                        "output {}x{}): its Resolution Scale below 100%? The game's TAA stays.",
                                        color_desc.Width, color_desc.Height, output_desc.Width, output_desc.Height));
    }
    release();
    return;
  }
  if (!Create(device.Get()) || FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))
      || !twattila::dlaa_bridge::Prepare(device.Get(), output_desc.Width, output_desc.Height, state.settings.ray_reconstruction,
                                         state.settings.ray_tracing, twattila::frame_generation::Format(), state.settings.ray_traced_reflections,
                                         static_cast<uint32_t>(method) - static_cast<uint32_t>(twattila::anti_aliasing::Method::DLAA),
                                         state.settings.frame_backend)) {
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
    renodx::utils::log::i(std::format("[Warhammer III] Upscaling: {}x{} -> {}x{} (colour format {}, output {}, depth {}x{}), the "
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
    // NVSDK_NGX_DLSS_Hint_Render_Preset L, M; K, FSR and XeSS take the light as it is.
    const std::array<float, 4> shader_settings = {(nvidia_model && (reconstruct || state.settings.dlaa_preset == 12u || state.settings.dlaa_preset == 13u)) ? 1.f : 0.f};
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
    // registers act on column vectors), given NVIDIA as row-vector matrices (their transposes). FSR and XeSS: the
    // projection alone (its z row and c7.y hold near, far and the field of view; the jitter is in its column 2).
    twattila::dlaa_bridge::RayReconstruction reconstruction = {.preset = state.settings.ray_reconstruction_preset};
    if (reconstruct || !nvidia_model) {
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
                                                state.settings.dlaa_preset, reconstruct ? &reconstruction : nullptr,
                                                nvidia_model ? nullptr : &reconstruction.view_to_clip);
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
  // Frame generation's camera has no jitter, the uploaded view-projection does: made again from the view and the projection
  // without its jitter terms (c6.z, c7.z). Near and far as uploaded; depth is forward (the helper's assumption).
  std::array<float, 16> view = {};
  std::array<float, 16> projection = {};
  std::copy_n(c + VIEW, 16, view.begin());
  std::copy_n(c + PROJECTION, 16, projection.begin());
  projection[2] = 0.f;
  projection[6] = 0.f;
  std::array<float, 16> view_projection = {};
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) {
      for (size_t k = 0; k < 4; ++k) view_projection[(row * 4) + column] += projection[(row * 4) + k] * view[(k * 4) + column];
    }
  }
  if (twattila::frame_generation::Wanted()) {
    state.generation = {
        .width = color_desc.Width,
        .height = color_desc.Height,
        .jitter_x = jitter[0],
        .jitter_y = jitter[1],
        .reset = reset,
        .near_plane = c[NEAR_FAR],
        .far_plane = c[NEAR_FAR + 1],
    };
    twattila::frame_generation::SetCamera(&state.generation, projection, view, view_projection,
                                           (reset ? view_projection : state.previous_view_projection), {c[0], c[1], c[2]});
    state.generation_frame = state.frame;
  }
  state.previous_view_projection = view_projection;
}

// The picture's target on `native` for frame generation: bound as the first render target, its size the upscaler's, 16 bits
// (what the swap chain shader takes); else none.
inline ComPtr<ID3D11Texture2D> PictureTarget(ID3D11DeviceContext* native) {
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  auto target = TextureOf(target_view.Get());
  if (!target) return target;
  D3D11_TEXTURE2D_DESC desc = {};
  target->GetDesc(&desc);
  const auto& bridge = twattila::dlaa_bridge::bridge;
  if (desc.Width != bridge.width || desc.Height != bridge.height || desc.SampleDesc.Count != 1
      || (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS)) {
    return {};
  }
  return target;
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
    if (!state.guides_logged) {
      state.guides_logged = true;
      renodx::utils::log::i("[Warhammer III] First run: Ray Reconstruction's guides written.");
    }
  }
  for (auto* view : gbuffer) {
    if (view != nullptr) view->Release();
  }
}

// While a frame is traced: the camera in `buffer` as `pass` has it: near, far, the depth at the near plane (0: forward Z,
// 1: reversed) and what the trace's lines can't show.
inline void MarkCamera(ID3D11DeviceContext* native, ID3D11Buffer* buffer, std::string_view pass) {
  if (buffer == nullptr) return;
  const auto c = twattila::frame_trace::ReadBuffer<float>(native, buffer, 0, internal::CAMERA_BYTES / 4);
  if (c.empty()) return;
  const float near_plane = c[internal::NEAR_FAR];
  twattila::frame_trace::Mark(std::format("{}: camera screen {} {} {} {}, viewport {} {} {} {}, near {} far {}, depth at near {}, jitter {} {}", pass,
                                          c[internal::SCREEN_SIZE], c[internal::SCREEN_SIZE + 1], c[internal::SCREEN_SIZE + 2], c[internal::SCREEN_SIZE + 3],
                                          c[internal::VIEWPORT], c[internal::VIEWPORT + 1], c[internal::VIEWPORT + 2], c[internal::VIEWPORT + 3],
                                          near_plane, c[internal::NEAR_FAR + 1], c[(8 * 4) + 2] + (c[(8 * 4) + 3] / near_plane),
                                          c[internal::CAMERA_JITTER], c[internal::CAMERA_JITTER + 1]));
}

// The tone mapper drew (its on_drawn, its target still the addon's 16-bit texture): the Motion Debug View in place of the
// picture. Here, not at the TAA pass: there the game's exposure, bloom and grading would still act on its colours.
inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  using internal::state;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  const std::unique_lock lock(state.mutex);
  if (state.settings.debug_view != 1u || state.evaluated_frame != state.frame) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const auto target = internal::TextureOf(target_view.Get());
  ComPtr<ID3D11DeviceContext1> context1;
  if (!target || FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  D3D11_TEXTURE2D_DESC desc = {};
  target->GetDesc(&desc);
  const auto& bridge = twattila::dlaa_bridge::bridge;
  auto& objects = state.objects;
  if (desc.Width != bridge.width || desc.Height != bridge.height || desc.SampleDesc.Count != 1 || (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0
      || (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS)) {
    return;
  }
  D3D11_TEXTURE2D_DESC made = {};
  if (objects.motion_view) objects.motion_view->GetDesc(&made);
  if (made.Width != desc.Width || made.Height != desc.Height) {
    objects.motion_view_uav.Reset();
    objects.motion_view.Reset();
    D3D11_TEXTURE2D_DESC view_desc = desc;
    view_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    view_desc.MiscFlags = 0;
    const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
    if (FAILED(objects.device->CreateTexture2D(&view_desc, nullptr, objects.motion_view.GetAddressOf()))
        || FAILED(objects.device->CreateUnorderedAccessView(objects.motion_view.Get(), &uav_desc, objects.motion_view_uav.GetAddressOf()))) {
      objects.motion_view.Reset();
      return;
    }
  }
  const D3D11_SHADER_RESOURCE_VIEW_DESC picture_desc = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
  ComPtr<ID3D11ShaderResourceView> picture;
  if (FAILED(objects.device->CreateShaderResourceView(target.Get(), &picture_desc, picture.GetAddressOf()))) return;
  {
    talesofarise::dlss::ContextStateScope scope(context1.Get(), objects.private_state.Get());
    constexpr std::array<float, 4> SHADER_SETTINGS = {0.f, 1.f};
    context1->UpdateSubresource(objects.shader_settings.Get(), 0, nullptr, SHADER_SETTINGS.data(), 0, 0);
    const std::array<ID3D11ShaderResourceView*, 2> sources = {picture.Get(), bridge.motion.srv.Get()};
    context1->CSSetShader(objects.output.Get(), nullptr, 0);
    context1->CSSetShaderResources(0, static_cast<UINT>(sources.size()), sources.data());
    context1->CSSetUnorderedAccessViews(0, 1, objects.motion_view_uav.GetAddressOf(), nullptr);
    context1->CSSetConstantBuffers(0, 1, objects.shader_settings.GetAddressOf());
    context1->Dispatch((desc.Width + 7) / 8, (desc.Height + 7) / 8, 1);
    context1->ClearState();
  }
  native->CopyResource(target.Get(), objects.motion_view.Get());
}

// After OnTonemapDrawn (its target still the addon's 16-bit texture, with what the Debug View drew: the HUD-less image shows
// what the frame shows): the frame's inputs for frame generation, and its HUD-less image until KeepOutput gives a later one.
inline void KeepFrame(reshade::api::command_list* cmd_list) {
  using internal::state;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  const std::unique_lock lock(state.mutex);
  if (state.generation_frame != state.frame) return;
  state.generation_frame = UINT64_MAX;
  if (const auto target = internal::PictureTarget(native)) twattila::frame_generation::KeepFrame(native, target.Get(), state.generation);
}

// The frame's main resolve, then sharpening, drew (no UI yet): the HUD-less image from their target, with the flares and
// the vignette t00's doesn't have.
inline void KeepOutput(reshade::api::command_list* cmd_list) {
  if (twattila::frame_generation::Format() == DXGI_FORMAT_UNKNOWN) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  if (const auto target = internal::PictureTarget(native)) twattila::frame_generation::KeepOutput(native, target.Get());
}

namespace internal {

constexpr auto OnTaa = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (twattila::frame_trace::Tracing()) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&native);
    ComPtr<ID3D11Buffer> camera;
    if (native != nullptr) native->CSGetConstantBuffers(0, 1, camera.GetAddressOf());
    MarkCamera(native, camera.Get(), "the game's TAA");
  }
  {
    const std::unique_lock lock(state.mutex);
    state.taa_frame = state.frame;
    if (state.taa_logged_hash != command.matched_shader_hash) {
      state.taa_logged_hash = command.matched_shader_hash;
      renodx::utils::log::i(std::format("[Warhammer III] First run: the game's TAA, cs {:08X}.", command.matched_shader_hash));
    }
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
  if (!state.camera_logged) {
    state.camera_logged = true;
    // Depth at near: 0 forward, what frame generation takes; above 0.5 reversed.
    renodx::utils::log::i(std::format("[Warhammer III] First run: the main view's camera, screen {}x{}, near {} far {}, depth at near {}.",
                                      c[SCREEN_SIZE], c[SCREEN_SIZE + 1], c[NEAR_FAR], c[NEAR_FAR + 1],
                                      c[(8 * 4) + 2] + (c[(8 * 4) + 3] / c[NEAR_FAR])));
  }
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
  bool view_3d = false;
  {
    const std::unique_lock lock(state.mutex);
    view_3d = state.camera_seen || state.taa_frame == state.frame;
    ++state.frame;
    state.camera_seen = false;
    state.trace_wanted = twattila::frame_trace::Wanted();
  }
  // FrameTrace: a frame of the 3D view (600 presents with its camera or its TAA: any Resolution Scale, DLSS on or off), or
  // the next one when renodx_trace_now sits next to the exe (checked each second; removed).
  if (static uint32_t presents = 0; ++presents % 60 == 0) {
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    const auto marker = std::filesystem::path(exe).parent_path() / L"renodx_trace_now";
    if (DeleteFileW(marker.c_str()) != 0) {
      twattila::frame_trace::StartSoon();
      view_3d = true;
    }
  }
  twattila::frame_trace::OnPresent(view_3d);
}

}  // namespace internal

// Every present.
inline void Update(reshade::api::swapchain* swapchain, const twattila::anti_aliasing::Settings& settings) {
  twattila::frame_generation::Update({
      .multiplier = (twattila::anti_aliasing::Upscales(settings.method) ? settings.frame_generation : 0u),
      .reflex_boost = (settings.reflex == 2),
      .frame_limit = settings.frame_limit,
      .preset = settings.frame_preset,
      .hud_recomposition = settings.hud_recomposition,
      .debug_view = settings.frame_debug_view,
      .sdr_output = settings.sdr_output,
  });
  twattila::reflex::Update(swapchain, settings.reflex, settings.frame_limit);
  const std::unique_lock lock(internal::state.mutex);
  internal::state.settings = settings;
  if (settings.debug_view != 1u) {
    internal::state.objects.motion_view_uav.Reset();
    internal::state.objects.motion_view.Reset();
  }
  // Off, frame generation's window hidden: the shared set and the helper's upscaler back (their VRAM).
  if (auto& bridge = twattila::dlaa_bridge::bridge; !twattila::anti_aliasing::Upscales(settings.method) && bridge.width != 0
      && bridge.shared != nullptr && bridge.shared->ready != 0 && twattila::frame_generation::Format() == DXGI_FORMAT_UNKNOWN) {
    twattila::dlaa_bridge::Release(true);
  }
}

// What the upscaler really does, for its status line.
inline std::string Status() {
  using internal::state;
  const std::unique_lock lock(state.mutex);
  const auto in_last_second = [](uint64_t frame) { return frame != UINT64_MAX && state.frame - frame < 60; };
  const auto& bridge = twattila::dlaa_bridge::bridge;
  if (bridge.failed) return bridge.status;
  if (in_last_second(state.evaluated_frame)) return (bridge.shared != nullptr) ? bridge.shared->status : bridge.status;
  if (in_last_second(state.no_camera_frame)) return "Not running: in-game Resolution Scale is below 100%. The game's own TAA and upscale stay.";
  if (in_last_second(state.taa_frame)) return bridge.status;
  return "Not running: keep in-game Anti-aliasing on TAA or TAA High (menus have none).";
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

inline void Install() {
  twattila::frame_trace::Install(internal::SCREEN_SIZE / 4, internal::VIEWPORT / 4);
  twattila::reflex::Install();
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
  twattila::reflex::Uninstall();
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
  twattila::frame_generation::OnDestroyDevice(native);
  twattila::reflex::OnDestroyDevice(native);
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.objects.device.Get() != native) return;
  internal::state.objects = {};
  twattila::dlaa_bridge::Release();
}

}  // namespace twwarhammer3::taa_dlss
