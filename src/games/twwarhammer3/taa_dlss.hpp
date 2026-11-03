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
#include <cmath>
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
#include "../twattila/frame_history.hpp"
#include "../twattila/frame_trace.hpp"
#include "../twattila/reflex.hpp"

// DLSS, FSR or XeSS in place of the game's TAA (it has no working DLSS of its own: preferences gfx_dlss never creates one):
// after its TAA resolve (cs 0x1ADBFF18 TAA / 0x6A8FC282 TAA High: t0 the G-buffer depth in x, t2 the velocity, t4 this
// frame's colour, jittered; u1 its result), the colour, depth and motion go through the upscaler in the TW helper
// (twattila/dlaa_bridge.hpp, renodx-tw-dlaa64.exe) and its result replaces the TAA's. The game's jitter (camera
// g_camera_jitter) and velocity (everything it draws) are its own. At the game's Resolution Scale of 100%: below it the main
// view's camera never passes Upload's test, so the game's TAA and upscale stay and the helper doesn't start. In the addon's
// own context state (the game's put back after). Frame generation: the helper's too (twattila/frame_generation.hpp).
//
// Below 100% with its row on (Experimental), after the tone mapper instead: the game's chain stays at the render size, top-left
// of its display-size textures, so nothing of it needs to know. The TAA's output takes the raw colour (dlss_scaled_inputs), the
// tone mapper draws at the render size with its overscan unused, and that picture goes through the upscaler into its target
// (dlss_scaled_prepare, dlss_scaled_finish). Effects drawn after it stay at the display's size. A size is used only after its first
// frame showed the constants as expected (OnTonemapDraw).
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
constexpr size_t VIEWPORT_ORIGIN = 40 * 4;
constexpr size_t CAMERA_JITTER = 257;
constexpr UINT OVERSCAN_BYTE = (6 * 16) + 4;  // the tone mapper's hdr_to_screen_PS (b4), overscan at c6.y

struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> inputs;
  ComPtr<ID3D11ComputeShader> output;
  ComPtr<ID3D11ComputeShader> guides;
  ComPtr<ID3D11ComputeShader> scaled_inputs;
  ComPtr<ID3D11ComputeShader> scaled_prepare;
  ComPtr<ID3D11ComputeShader> scaled_finish;
  ComPtr<ID3D11SamplerState> linear_sampler;  // dlss_scaled_finish's
  ComPtr<ID3D11Buffer> shader_settings;       // the compute shaders'
  // Written by a compute shader, then copied over the picture (its texture takes no UAV): while the Motion Debug View is on, or below 100%.
  ComPtr<ID3D11Texture2D> display_copy;
  ComPtr<ID3D11UnorderedAccessView> display_copy_uav;
  bool failed = false;
};

// Below 100%: what the game's constants have shown about its Resolution Scale.
enum class ScaleProof : uint8_t {
  NONE,     // the row off, or the game at 100%
  WAITING,  // a new size: the TAA left alone until the tone mapper's draw has checked it
  GOOD,
  REFUSED,  // not as expected: the game's own TAA and upscale until the size or the row changes
};

// What the game's TAA pass does this time.
enum class ScaleChoice : uint8_t {
  WHOLE,   // 100%: the upscaler right here
  STOCK,   // the game's result stays
  SCALED,  // the raw colour in its output, the upscaler after the tone mapper
};

// A camera-sized buffer's last upload: g_screen_size, g_viewport_dimensions, g_viewport_origin.
struct UploadedSize {
  std::array<float, 2> screen = {};
  std::array<float, 2> viewport = {};
  std::array<float, 2> origin = {};
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
  // The reflection march's G-buffer and camera (the water drawn by then): RewriteGuides'.
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> reflection_gbuffer;
  ComPtr<ID3D11Buffer> reflection_camera;
  std::array<float, 16> previous_view_projection = {};  // the last evaluation's, without the jitter
  twattila::frame_generation::Inputs generation;        // this evaluation's for frame generation
  uint64_t generation_frame = UINT64_MAX;               // its frame, until the tone mapper's draw keeps it
  bool logged = false;
  bool no_camera_logged = false;
  // A hook's first run, in the log (one that never ran looks like one that did nothing).
  uint32_t taa_logged_hash = 0;
  bool camera_logged = false;
  bool guides_logged = false;
  // Below 100%, its row on.
  bool below_100 = false;
  std::unordered_map<uint64_t, UploadedSize> uploaded_sizes;
  ScaleProof scale_proof = ScaleProof::NONE;
  ScaleChoice scale_choice = ScaleChoice::WHOLE;  // this frame's first TAA pass
  std::array<UINT, 2> learned_size = {};          // g_viewport_dimensions of the buffer the TAA reads
  float learned_share = 1.f;                      // its part of the display's width
  uint64_t scale_frame = UINT64_MAX;              // the first TAA pass chose
  uint64_t scale_camera = 0;                      // the buffer it read
  uint64_t scale_refused_frame = UINT64_MAX;
  uint64_t scaled_frame = UINT64_MAX;       // its output holds the raw colour
  uint64_t scaled_done_frame = UINT64_MAX;  // the addon's texture holds the whole picture
  uint64_t tonemap_frame = UINT64_MAX;      // the frame's first tone mapper draw came
  bool tonemap_at_render_size = false;      // it is drawing so
  D3D11_VIEWPORT tonemap_viewport = {};     // the game's for it
  // The TAA's for the tone mapper's evaluation.
  std::array<float, CAMERA_BYTES / 4> taa_camera = {};
  std::array<float, 2> taa_jitter = {};
  bool taa_reset = false;
};
static State state;

inline bool Create(ID3D11Device* device) {
  auto& objects = state.objects;
  if (objects.device.Get() == device) return !objects.failed;
  objects = {.device = device};
  ComPtr<ID3D11Device1> device1;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                                           .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxLOD = D3D11_FLOAT32_MAX};
  objects.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device),
                                                               nullptr, objects.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__dlss_inputs.data(), __dlss_inputs.size(), nullptr, objects.inputs.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__dlss_output.data(), __dlss_output.size(), nullptr, objects.output.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rr_guides.data(), __rr_guides.size(), nullptr, objects.guides.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__dlss_scaled_inputs.data(), __dlss_scaled_inputs.size(), nullptr, objects.scaled_inputs.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__dlss_scaled_prepare.data(), __dlss_scaled_prepare.size(), nullptr, objects.scaled_prepare.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__dlss_scaled_finish.data(), __dlss_scaled_finish.size(), nullptr, objects.scaled_finish.GetAddressOf()))
                   || FAILED(device->CreateSamplerState(&sampler_desc, objects.linear_sampler.GetAddressOf()))
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

// The guides from these views (state.mutex held), on the addon's own context state.
inline void DispatchGuides(ID3D11DeviceContext* native, ID3D11DeviceContext1* context1, const std::array<ID3D11ShaderResourceView*, 3>& gbuffer, ID3D11Buffer* camera) {
  auto& bridge = twattila::dlaa_bridge::bridge;
  twattila::dlaa_bridge::BeforeSharedWrites(native);
  talesofarise::dlss::ContextStateScope scope(context1, state.objects.private_state.Get());
  const std::array<ID3D11UnorderedAccessView*, 4> guides = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(),
                                                            bridge.normals.uav.Get(), bridge.roughness.uav.Get()};
  context1->CSSetShader(state.objects.guides.Get(), nullptr, 0);
  context1->CSSetShaderResources(0, static_cast<UINT>(gbuffer.size()), gbuffer.data());
  context1->CSSetUnorderedAccessViews(0, static_cast<UINT>(guides.size()), guides.data(), nullptr);
  context1->CSSetConstantBuffers(0, 1, &camera);
  context1->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
  context1->ClearState();
}

// `display_copy` at `desc`'s size (state.mutex held); false: not made.
inline bool MakeDisplayCopy(D3D11_TEXTURE2D_DESC desc) {
  auto& objects = state.objects;
  D3D11_TEXTURE2D_DESC made = {};
  if (objects.display_copy) objects.display_copy->GetDesc(&made);
  if (made.Width == desc.Width && made.Height == desc.Height) return true;
  objects.display_copy_uav.Reset();
  objects.display_copy.Reset();
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags = 0;
  const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
  if (FAILED(objects.device->CreateTexture2D(&desc, nullptr, objects.display_copy.GetAddressOf()))
      || FAILED(objects.device->CreateUnorderedAccessView(objects.display_copy.Get(), &uav_desc, objects.display_copy_uav.GetAddressOf()))) {
    objects.display_copy.Reset();
    return false;
  }
  return true;
}

// Whether this frame's evaluation is Ray Reconstruction's (its guides written).
inline bool Reconstructs() {
  return state.settings.ray_reconstruction && state.guides_frame == state.frame && twattila::dlaa_bridge::bridge.guides;
}

// NVSDK_NGX_DLSS_Hint_Render_Preset L, M and Ray Reconstruction take the colour's square root; K, FSR and XeSS the light as it is.
inline bool SquareRootInput(bool reconstruct) {
  return state.settings.method == twattila::anti_aliasing::Method::DLAA
         && (reconstruct || state.settings.dlaa_preset == 12u || state.settings.dlaa_preset == 13u);
}

// The inputs written (their top-left `width` x `height`) through the upscaler (state.mutex held, the addon's context state), the
// camera `c` as uploaded.
inline bool EvaluateInputs(ID3D11DeviceContext* context, UINT width, UINT height, const std::array<float, 2>& jitter, bool reset, bool reconstruct, const float* c) {
  const bool nvidia_model = state.settings.method == twattila::anti_aliasing::Method::DLAA;
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
  return twattila::dlaa_bridge::Evaluate(context, width, height, jitter[0], jitter[1], reset, state.settings.dlaa_preset,
                                         reconstruct ? &reconstruction : nullptr, nvidia_model ? nullptr : &reconstruction.view_to_clip);
}

// After an evaluation of `width` x `height` with the camera `c` (state.mutex held).
inline void OnEvaluated(UINT width, UINT height, const std::array<float, 2>& jitter, bool reset, const float* c) {
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
        .width = width,
        .height = height,
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

// At the frame's first TAA pass with the row on (state.mutex held): the size it worked at, from the buffer it reads (`output` its
// output texture). A new size is only learned here: the tone mapper's draw checks it this frame, the upscaler from the next.
inline ScaleChoice ChooseScale(ID3D11DeviceContext* native, const D3D11_TEXTURE2D_DESC& output) {
  if (!state.below_100) return ScaleChoice::WHOLE;
  // Later passes are other views': what the first chose at 100%, else the game's.
  if (state.scale_frame == state.frame) return (state.scale_choice == ScaleChoice::WHOLE) ? ScaleChoice::WHOLE : ScaleChoice::STOCK;
  state.scale_frame = state.frame;
  ComPtr<ID3D11Buffer> camera;
  native->CSGetConstantBuffers(0, 1, camera.GetAddressOf());
  state.scale_camera = reinterpret_cast<uint64_t>(camera.Get());
  const auto found = state.uploaded_sizes.find(state.scale_camera);
  if (found == state.uploaded_sizes.end()) {
    // Nothing to learn from. With the main view's camera seen it is 100%, as without the row.
    if (!state.camera_seen) state.scale_refused_frame = state.frame;
    state.scale_choice = (state.camera_seen ? ScaleChoice::WHOLE : ScaleChoice::STOCK);
    return state.scale_choice;
  }
  const auto& [screen, viewport, origin] = found->second;
  if (viewport == screen) {
    state.scale_proof = ScaleProof::NONE;
    state.learned_size = {};
    state.learned_share = 1.f;
    state.scale_choice = ScaleChoice::WHOLE;
    return state.scale_choice;
  }
  if (const std::array<UINT, 2> size = {static_cast<UINT>(viewport[0]), static_cast<UINT>(viewport[1])}; size != state.learned_size) {
    state.learned_size = size;
    state.learned_share = viewport[0] / screen[0];
    const bool expected = screen[0] == static_cast<float>(output.Width) && screen[1] == static_cast<float>(output.Height)
                          && origin[0] == 0.f && origin[1] == 0.f
                          && viewport[0] > 0.f && viewport[1] > 0.f && viewport[0] <= screen[0] && viewport[1] <= screen[1]
                          && viewport[0] == std::floor(viewport[0]) && viewport[1] == std::floor(viewport[1]);
    state.scale_proof = (expected ? ScaleProof::WAITING : ScaleProof::REFUSED);
    if (!expected) {
      renodx::utils::log::i(std::format("[Warhammer III] Upscaling below 100%: refused: the TAA's camera has screen {}x{}, viewport {}x{}, origin {} {}, its output is {}x{}.",
                                        screen[0], screen[1], viewport[0], viewport[1], origin[0], origin[1], output.Width, output.Height));
    }
  }
  if (state.scale_proof == ScaleProof::REFUSED) state.scale_refused_frame = state.frame;
  state.scale_choice = ((state.scale_proof == ScaleProof::GOOD) ? ScaleChoice::SCALED : ScaleChoice::STOCK);
  return state.scale_choice;
}

// After the game's TAA resolve: its inputs through DLSS, the result into its output. Below 100%: see the namespace's comment.
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
  const ScaleChoice scale = ChooseScale(native, output_desc);
  // The row on and no camera: a scale that just changed, the next frame's.
  if (scale == ScaleChoice::STOCK || (state.below_100 && !state.camera_seen)) {
    release();
    return;
  }
  const bool scaled = scale == ScaleChoice::SCALED;
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
      || !twattila::dlaa_bridge::Prepare(device.Get(), output_desc.Width, output_desc.Height,
                                         state.settings.ray_reconstruction || state.settings.ray_traced_guides,
                                         state.settings.ray_tracing, twattila::frame_generation::Format(), state.settings.ray_traced_reflections,
                                         static_cast<uint32_t>(method) - static_cast<uint32_t>(twattila::anti_aliasing::Method::DLAA),
                                         state.settings.frame_backend)) {
    release();
    return;
  }
  auto& bridge = twattila::dlaa_bridge::bridge;
  // Until the helper is up the game's TAA and upscale stay whole. The finish's texture first: nothing may leave the picture half done.
  if (scaled && (bridge.shared == nullptr || bridge.shared->ready == 0
                 || !MakeDisplayCopy({.Width = output_desc.Width, .Height = output_desc.Height, .MipLevels = 1, .ArraySize = 1,
                                      .Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT}))) {
    release();
    return;
  }
  const UINT render_width = (scaled ? state.learned_size[0] : color_desc.Width);
  const UINT render_height = (scaled ? state.learned_size[1] : color_desc.Height);
  // The game's jitter (clip space, y up) in render pixels as it moves the picture (y down).
  const float* c = state.camera.data();
  const std::array<float, 2> jitter = {c[CAMERA_JITTER] * 0.5f * static_cast<float>(render_width),
                                       -c[CAMERA_JITTER + 1] * 0.5f * static_cast<float>(render_height)};
  const bool reset = state.evaluated_frame + 1 != state.frame;
  if (!state.logged && (c[CAMERA_JITTER] != 0.f || c[CAMERA_JITTER + 1] != 0.f)) {
    state.logged = true;
    D3D11_TEXTURE2D_DESC depth_desc = {};
    if (const auto depth = TextureOf(inputs[0])) depth->GetDesc(&depth_desc);
    renodx::utils::log::i(std::format("[Warhammer III] Upscaling: {}x{} -> {}x{} (colour format {}, output {}, depth {}x{}), the "
                                      "camera's screen {}x{} and viewport {}x{}, the game's jitter {:.5f} {:.5f}, its "
                                      "projection's {:.5f} {:.5f}",
                                      render_width, render_height, output_desc.Width, output_desc.Height,
                                      static_cast<int>(color_desc.Format), static_cast<int>(output_desc.Format),
                                      depth_desc.Width, depth_desc.Height, c[SCREEN_SIZE], c[SCREEN_SIZE + 1], c[VIEWPORT],
                                      c[VIEWPORT + 1], c[CAMERA_JITTER], c[CAMERA_JITTER + 1], c[PROJECTION + 2], c[PROJECTION + 6]));
  }
  bool evaluated = false;
  twattila::dlaa_bridge::BeforeSharedWrites(native);
  {
    talesofarise::dlss::ContextStateScope scope(context1.Get(), state.objects.private_state.Get());
    const bool reconstruct = Reconstructs();
    const std::array<float, 4> shader_settings = {(!scaled && SquareRootInput(reconstruct)) ? 1.f : 0.f, 0.f, static_cast<float>(render_width), static_cast<float>(render_height)};
    context1->UpdateSubresource(state.objects.shader_settings.Get(), 0, nullptr, shader_settings.data(), 0, 0);
    const std::array<ID3D11ShaderResourceView*, 3> sources = {inputs[0], inputs[2], inputs[4]};
    const std::array<ID3D11UnorderedAccessView*, 3> destinations = {(scaled ? target.Get() : bridge.color.uav.Get()), bridge.depth.uav.Get(), bridge.motion.uav.Get()};
    context1->CSSetShader((scaled ? state.objects.scaled_inputs.Get() : state.objects.inputs.Get()), nullptr, 0);
    context1->CSSetShaderResources(0, static_cast<UINT>(sources.size()), sources.data());
    context1->CSSetUnorderedAccessViews(0, static_cast<UINT>(destinations.size()), destinations.data(), nullptr);
    context1->CSSetConstantBuffers(0, 1, state.objects.shader_settings.GetAddressOf());
    context1->Dispatch((render_width + 7) / 8, (render_height + 7) / 8, 1);
    context1->ClearState();
    if (!scaled) evaluated = EvaluateInputs(context1.Get(), render_width, render_height, jitter, reset, reconstruct, c);
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
  if (scaled) {
    state.scaled_frame = state.frame;
    state.taa_camera = state.camera;
    state.taa_jitter = jitter;
    state.taa_reset = reset;
    return;
  }
  if (evaluated) OnEvaluated(render_width, render_height, jitter, reset, c);
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
// at `camera_slot`): the guides (Ray Reconstruction's, also ray tracing's paths') into the bridge's textures (rr_guides), once a
// frame.
inline void WriteGuides(ID3D11DeviceContext* native, UINT gbuffer_slot, UINT camera_slot) {
  using internal::state;
  ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  const std::unique_lock lock(state.mutex);
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (!twattila::anti_aliasing::Upscales(state.settings.method) || !(state.settings.ray_reconstruction || state.settings.ray_traced_guides)
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
    internal::DispatchGuides(native, context1.Get(), gbuffer, camera.Get());
    state.guides_frame = state.frame;
    if (!state.guides_logged) {
      state.guides_logged = true;
      renodx::utils::log::i("[Warhammer III] First run: the G-buffer guides written.");
    }
  }
  for (auto* view : gbuffer) {
    if (view != nullptr) view->Release();
  }
}

// At the reflection march (its CS t0-t2 the G-buffer, b0 the camera): what RewriteGuides reads; the trace's dispatches come
// between and take those slots.
inline void KeepReflectionGuideInputs(ID3D11DeviceContext* native) {
  std::array<ID3D11ShaderResourceView*, 3> gbuffer = {};
  native->CSGetShaderResources(0, static_cast<UINT>(gbuffer.size()), gbuffer.data());
  const std::unique_lock lock(internal::state.mutex);
  for (size_t index = 0; index < gbuffer.size(); ++index) {
    internal::state.reflection_gbuffer[index] = gbuffer[index];
    if (gbuffer[index] != nullptr) gbuffer[index]->Release();
  }
  native->CSGetConstantBuffers(0, 1, internal::state.reflection_camera.ReleaseAndGetAddressOf());
}

// The guides again with the water in the G-buffer, for the rays that leave from it (rt_core Reflections).
inline void RewriteGuides(ID3D11DeviceContext* native) {
  using internal::state;
  ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  const std::unique_lock lock(state.mutex);
  if (state.guides_frame != state.frame || !state.reflection_camera) return;
  internal::DispatchGuides(native, context1.Get(), {state.reflection_gbuffer[0].Get(), state.reflection_gbuffer[1].Get(), state.reflection_gbuffer[2].Get()},
                           state.reflection_camera.Get());
  state.reflection_gbuffer = {};
  state.reflection_camera.Reset();
}

// Whether the G-buffer guides were written this frame.
inline bool GuidesWritten() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.guides_frame == internal::state.frame;
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

// The tone mapper's on_draw, after aa_target's (`redirected`: it draws into the addon's `width` x `height` texture). Below 100%: a
// frame whose TAA left its colour raw draws at the render size (true: its overscan is not to be used); a new size's first frame
// only has its constants checked. Not as expected: the game's draw and this size refused, its TAA's result back from the next frame.
inline bool OnTonemapDraw(ID3D11DeviceContext* native, bool redirected, UINT width, UINT height) {
  using internal::state;
  using internal::ScaleProof;
  const std::unique_lock lock(state.mutex);
  // Later draws are other views' (portraits): MainViewport.
  if (state.scale_proof == ScaleProof::GOOD) twattila::frame_history::tonemapped.store(true, std::memory_order_relaxed);
  state.tonemap_at_render_size = false;
  const bool checking = state.scale_proof == ScaleProof::WAITING && state.scale_frame == state.frame;
  const bool scaled = state.scaled_frame == state.frame;
  if ((!checking && !scaled) || state.tonemap_frame == state.frame) return false;
  state.tonemap_frame = state.frame;
  const auto refuse = [](const std::string& why) {
    state.scale_proof = ScaleProof::REFUSED;
    state.scale_refused_frame = state.frame;
    renodx::utils::log::i(std::format("[Warhammer III] Upscaling below 100%: refused at {}x{}: {}.", state.learned_size[0], state.learned_size[1], why));
    return false;
  };
  if (!redirected) return refuse("the tone mapper doesn't draw into the addon's texture");
  UINT viewports = 0;
  native->RSGetViewports(&viewports, nullptr);
  if (viewports != 1) return refuse(std::format("{} viewports at the tone mapper", viewports));
  D3D11_VIEWPORT viewport = {};
  native->RSGetViewports(&viewports, &viewport);
  if (viewport.TopLeftX != 0.f || viewport.TopLeftY != 0.f || viewport.Width != static_cast<float>(width) || viewport.Height != static_cast<float>(height)) {
    return refuse(std::format("the tone mapper's viewport is {}x{} at {} {}, its target {}x{}", viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY, width, height));
  }
  ComPtr<ID3D11Buffer> camera;
  native->PSGetConstantBuffers(1, 1, camera.GetAddressOf());
  if (!camera) return refuse("the tone mapper has no camera");
  if (scaled) {
    const auto& bridge = twattila::dlaa_bridge::bridge;
    if (bridge.width != width || bridge.height != height) return refuse(std::format("the upscaler's output is {}x{}, the tone mapper's target {}x{}", bridge.width, bridge.height, width, height));
    const auto found = state.uploaded_sizes.find(reinterpret_cast<uint64_t>(camera.Get()));
    if (found != state.uploaded_sizes.end() && found->second.screen != std::array<float, 2>{static_cast<float>(width), static_cast<float>(height)}) {
      return refuse(std::format("the tone mapper's camera has screen {}x{}, its target is {}x{}", found->second.screen[0], found->second.screen[1], width, height));
    }
    state.tonemap_viewport = viewport;
    D3D11_VIEWPORT render_viewport = viewport;
    render_viewport.Width = static_cast<float>(state.learned_size[0]);
    render_viewport.Height = static_cast<float>(state.learned_size[1]);
    native->RSSetViewports(1, &render_viewport);
    state.tonemap_at_render_size = true;
    return true;
  }
  // Once per size: reading them back makes the game wait.
  ComPtr<ID3D11DeviceContext1> context1;
  ComPtr<ID3D11Buffer> hdr_to_screen;
  UINT first_constant = 0;
  UINT constant_count = 0;
  if (FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return refuse("no ID3D11DeviceContext1");
  context1->PSGetConstantBuffers1(4, 1, hdr_to_screen.GetAddressOf(), &first_constant, &constant_count);
  if (!hdr_to_screen || first_constant != 0) return refuse(std::format("the tone mapper's b4 starts at constant {}", first_constant));
  const auto screen = twattila::frame_trace::ReadBuffer<float>(native, camera.Get(), internal::SCREEN_SIZE * 4, 2);
  const auto overscan = twattila::frame_trace::ReadBuffer<float>(native, hdr_to_screen.Get(), internal::OVERSCAN_BYTE, 1);
  if (screen.empty() || overscan.empty()) return refuse("the tone mapper's constants can't be read");
  const std::string values = std::format("target {}x{}, its camera's screen {}x{}, overscan {}, the tone mapper's camera {} the TAA's", width, height, screen[0], screen[1], overscan[0],
                                         (reinterpret_cast<uint64_t>(camera.Get()) == state.scale_camera ? "is" : "isn't"));
  if (screen[0] != static_cast<float>(width) || screen[1] != static_cast<float>(height)) return refuse(values);
  if (std::abs((overscan[0] * static_cast<float>(width)) - static_cast<float>(state.learned_size[0])) > 1.f
      || std::abs((overscan[0] * static_cast<float>(height)) - static_cast<float>(state.learned_size[1])) > 1.f) {
    return refuse(values);
  }
  state.scale_proof = ScaleProof::GOOD;
  renodx::utils::log::i(std::format("[Warhammer III] Upscaling below 100%: {}x{} as expected: {}.", state.learned_size[0], state.learned_size[1], values));
  return false;
}

// The tone mapper drew at the render size into `picture` (its on_drawn; its target that texture, still bound): through the upscaler,
// over the whole display. The viewport back first: nothing after it is at the render size. No result: stretched, never only the corner.
inline void OnTonemapDrawnAtRenderSize(reshade::api::command_list* cmd_list, ID3D11ShaderResourceView* picture) {
  using internal::state;
  const std::unique_lock lock(state.mutex);
  if (!state.tonemap_at_render_size) return;
  state.tonemap_at_render_size = false;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  native->RSSetViewports(1, &state.tonemap_viewport);
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const auto target = internal::TextureOf(target_view.Get());
  ComPtr<ID3D11DeviceContext1> context1;
  if (!target || FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  auto& objects = state.objects;
  const auto& bridge = twattila::dlaa_bridge::bridge;
  const auto [width, height] = state.learned_size;
  bool evaluated = false;
  twattila::dlaa_bridge::BeforeSharedWrites(native);
  {
    talesofarise::dlss::ContextStateScope scope(context1.Get(), objects.private_state.Get());
    const bool reconstruct = internal::Reconstructs();
    std::array<float, 4> shader_settings = {internal::SquareRootInput(reconstruct) ? 1.f : 0.f, 0.f, static_cast<float>(width), static_cast<float>(height)};
    context1->UpdateSubresource(objects.shader_settings.Get(), 0, nullptr, shader_settings.data(), 0, 0);
    context1->CSSetShader(objects.scaled_prepare.Get(), nullptr, 0);
    context1->CSSetShaderResources(0, 1, &picture);
    context1->CSSetUnorderedAccessViews(0, 1, bridge.color.uav.GetAddressOf(), nullptr);
    context1->CSSetConstantBuffers(0, 1, objects.shader_settings.GetAddressOf());
    context1->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    context1->ClearState();
    evaluated = internal::EvaluateInputs(context1.Get(), width, height, state.taa_jitter, state.taa_reset, reconstruct, state.taa_camera.data());
    shader_settings[1] = evaluated ? 0.f : 1.f;
    context1->UpdateSubresource(objects.shader_settings.Get(), 0, nullptr, shader_settings.data(), 0, 0);
    const std::array<ID3D11ShaderResourceView*, 2> sources = {bridge.output.srv.Get(), bridge.color.srv.Get()};
    D3D11_TEXTURE2D_DESC desc = {};
    target->GetDesc(&desc);
    context1->CSSetShader(objects.scaled_finish.Get(), nullptr, 0);
    context1->CSSetShaderResources(0, static_cast<UINT>(sources.size()), sources.data());
    context1->CSSetSamplers(0, 1, objects.linear_sampler.GetAddressOf());
    context1->CSSetUnorderedAccessViews(0, 1, objects.display_copy_uav.GetAddressOf(), nullptr);
    context1->CSSetConstantBuffers(0, 1, objects.shader_settings.GetAddressOf());
    context1->Dispatch((desc.Width + 7) / 8, (desc.Height + 7) / 8, 1);
    context1->ClearState();
  }
  native->CopyResource(target.Get(), objects.display_copy.Get());
  state.scaled_done_frame = state.frame;
  if (evaluated) internal::OnEvaluated(width, height, state.taa_jitter, state.taa_reset, state.taa_camera.data());
}

// Whether this frame's picture came through the upscaler below 100%: the game's FXAA on it would read its flags at display pixels,
// which hold data only in the render size's corner.
inline bool ScaledThisFrame() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.scaled_done_frame == internal::state.frame;
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
  if (!internal::MakeDisplayCopy(desc)) return;
  const D3D11_SHADER_RESOURCE_VIEW_DESC picture_desc = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
  ComPtr<ID3D11ShaderResourceView> picture;
  if (FAILED(objects.device->CreateShaderResourceView(target.Get(), &picture_desc, picture.GetAddressOf()))) return;
  {
    talesofarise::dlss::ContextStateScope scope(context1.Get(), objects.private_state.Get());
    // Below 100% the motion is in its texture's top-left: stretched over the picture.
    const bool scaled = state.scaled_done_frame == state.frame;
    const std::array<float, 4> shader_settings = {0.f, 1.f, scaled ? static_cast<float>(state.learned_size[0]) / static_cast<float>(desc.Width) : 1.f,
                                                  scaled ? static_cast<float>(state.learned_size[1]) / static_cast<float>(desc.Height) : 1.f};
    context1->UpdateSubresource(objects.shader_settings.Get(), 0, nullptr, shader_settings.data(), 0, 0);
    const std::array<ID3D11ShaderResourceView*, 2> sources = {picture.Get(), bridge.motion.srv.Get()};
    context1->CSSetShader(objects.output.Get(), nullptr, 0);
    context1->CSSetShaderResources(0, static_cast<UINT>(sources.size()), sources.data());
    context1->CSSetUnorderedAccessViews(0, 1, objects.display_copy_uav.GetAddressOf(), nullptr);
    context1->CSSetConstantBuffers(0, 1, objects.shader_settings.GetAddressOf());
    context1->Dispatch((desc.Width + 7) / 8, (desc.Height + 7) / 8, 1);
    context1->ClearState();
  }
  native->CopyResource(target.Get(), objects.display_copy.Get());
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
// screen, or below 100% over the size the TAA was seen at, uploaded to `buffer`.
inline void Upload(const float* c, uint64_t buffer) {
  if (state.below_100) {
    state.uploaded_sizes[buffer] = {.screen = {c[SCREEN_SIZE], c[SCREEN_SIZE + 1]},
                                    .viewport = {c[VIEWPORT], c[VIEWPORT + 1]},
                                    .origin = {c[VIEWPORT_ORIGIN], c[VIEWPORT_ORIGIN + 1]}};
  }
  const bool scaled = state.scale_proof == ScaleProof::GOOD;
  const float main_width = (scaled ? static_cast<float>(state.learned_size[0]) : c[SCREEN_SIZE]);
  const float main_height = (scaled ? static_cast<float>(state.learned_size[1]) : c[SCREEN_SIZE + 1]);
  const float* last = c + PROJECTION + 12;
  if (last[0] != 0.f || last[1] != 0.f || last[2] != 1.f || last[3] != 0.f || c[SCREEN_SIZE] <= 0.f
      || c[VIEWPORT] != main_width || c[VIEWPORT + 1] != main_height) {
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
    // The addon's passes follow the game's size (without the row: 0, 1, never set).
    const bool scaled = state.scale_proof == ScaleProof::GOOD;
    twattila::frame_history::tonemapped.store(false, std::memory_order_relaxed);
    twattila::frame_history::render_width.store(scaled ? state.learned_size[0] : 0u, std::memory_order_relaxed);
    twattila::frame_history::render_height.store(scaled ? state.learned_size[1] : 0u, std::memory_order_relaxed);
    twattila::frame_history::effect_share.store(scaled ? state.learned_share : 1.f, std::memory_order_relaxed);
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

// Every present. `below_100`: the upscaler also when the game's Resolution Scale is below 100%.
inline void Update(reshade::api::swapchain* swapchain, const twattila::anti_aliasing::Settings& settings, bool below_100) {
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
  if (internal::state.below_100 != below_100) {
    internal::state.below_100 = below_100;
    internal::state.scale_proof = internal::ScaleProof::NONE;
    internal::state.learned_size = {};
    internal::state.learned_share = 1.f;
    internal::state.uploaded_sizes.clear();
  }
  if ((settings.debug_view != 1u && internal::state.scale_proof != internal::ScaleProof::GOOD) || !twattila::anti_aliasing::Upscales(settings.method)) {
    internal::state.objects.display_copy_uav.Reset();
    internal::state.objects.display_copy.Reset();
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
  if (in_last_second(state.scale_refused_frame)) return "Not running: unexpected constants at this in-game Resolution Scale (values in ReShade.log). The game's own TAA and upscale stay.";
  if (in_last_second(state.no_camera_frame)) return "Not running: in-game Resolution Scale is below 100%. The game's own TAA and upscale stay (or turn on Below 100% Resolution Scale).";
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

// The game's render size against the display's (width), below 100% with the row on; else 1.
inline float RenderShare() {
  const std::unique_lock lock(internal::state.mutex);
  return (internal::state.scale_proof == internal::ScaleProof::GOOD) ? internal::state.learned_share : 1.f;
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
  internal::state.scale_proof = internal::ScaleProof::NONE;
  internal::state.learned_size = {};
  internal::state.uploaded_sizes.clear();
  twattila::dlaa_bridge::Release();
}

}  // namespace twwarhammer3::taa_dlss
