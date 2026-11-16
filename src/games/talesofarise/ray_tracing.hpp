/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <span>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../talesofarise_dlss/constant_mirror.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/rt_shadows.hpp"
#include "../talesofarise_dlss/upscaling.hpp"
#include "../twattila/rt_core.hpp"

// Ray Tracing in the Helper (Experimental; off: rt_shadows.hpp's rays on the add-on's own D3D12 device): Tales of
// Arise's scene through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe, its own D3D12 device), in
// metres (Unreal's centimetres / 100). The scene: the depth prepass's draws into the scene depth (no target, depth
// written): the opaque prepass's vertex shaders (rt_shadows::DEPTH_SHADERS; the characters' dynamic) and the masked
// prepass's (rt_core's SceneCoverage: an alpha test at a texture coordinate as drawn, a dithered fade solid, a mask read
// at a scaled coordinate left out; the characters' by rt_shadows::SKINNED_MASKED_SHADERS dynamic). The trace at Bandai's
// sun shadow mask (cs 0x2084C889: the scene depth at t0, ScreenToShadow at b0 c28-c30, the View at b1): the camera
// (TranslatedWorldToClip c0-c3 and PreViewTranslation c62 in metres), the direction toward the sun (0x7A1E0D12's
// TowardSun, here on the CPU); then the traced sun at t40 for sunshadow00 (its texture's top-left the render pixels it
// loads) and the traced ambient copied to the scene depth's size for the lighting passes' t41 (rt_ambient.hlsli reads it
// by UV), in rt_shadows' place. At the frame's first lighting or toon draw (its G-buffer at t1-t3, the View at b1): the
// G-buffer guides into the bridge (WriteGuides, the anti-aliasing's 0x7A1E0D31), then the reflections and the specular
// occlusion traced (rt_scene::Reflections; their hits light from last frame's colour, the bridge's), copied for t43 /
// t44. The move's steps (ATN 2026-11-16): 1 the sun shadows and the AO, 2 the reflections and specular occlusion.
namespace talesofarise::dlss::ray_tracing {

using Microsoft::WRL::ComPtr;

namespace internal {

// UE 4.21's View (the sun shadow mask's b1, 0x7A1E0D12's rows): TranslatedWorldToClip c0-c3, TranslatedWorldToView c8-c10,
// ViewToClip c24-c25, ScreenToWorld c44-c46, PreViewTranslation c62, ViewSizeAndInvSize c122. Its ScreenToShadow at b0
// c28-c30.
constexpr uint32_t VIEW_TRANSLATED_WORLD_TO_VIEW = 8;
constexpr uint32_t VIEW_VIEW_TO_CLIP = 24;
constexpr uint32_t VIEW_GUIDE_ROWS = VIEW_VIEW_TO_CLIP + 2;
constexpr uint32_t VIEW_SCREEN_TO_WORLD = 44;
constexpr uint32_t VIEW_PRE_VIEW_TRANSLATION = 62;
constexpr uint32_t VIEW_SIZE = 122;
constexpr uint32_t VIEW_ROWS = VIEW_SIZE + 1;
constexpr uint32_t SCREEN_TO_SHADOW = 28;
constexpr double CENTIMETRES = 100.0;

// A traced texture copied to the scene depth's size (the lighting passes read it by UV, as the in-process rays'), the
// render pixels at its top-left, and the frame it was copied.
struct Kept {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  uint64_t frame = UINT64_MAX;
};

struct State {
  std::mutex mutex;
  twattila::rt_scene::Settings settings;
  ID3D11Resource* scene_depth = nullptr;  // the sun shadow mask's t0 (compared only)
  std::array<UINT, 2> render_size = {};
  std::array<UINT, 2> depth_size = {};
  uint64_t frame = 0;
  uint64_t guides_frame = UINT64_MAX;
  uint64_t lit_frame = UINT64_MAX;  // the first lighting draw's (Reflections)
  Kept ambient;
  Kept reflection;
  Kept specular;
  // The guides' pass on the game's device, in the add-on's own context state.
  ID3D11Device* device = nullptr;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> guide_shader;
  ComPtr<ID3D11Buffer> guide_constants;
};
inline State state;

// A depth prepass draw into the scene depth (no target, depth written): an opaque prepass vertex shader's, or a masked
// one's by its pixel shader's coverage; false: not in the scene.
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(1, target.GetAddressOf(), depth_view.GetAddressOf());
  if (target || !depth_view) return false;
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_reference = 0;
  native->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_reference);
  D3D11_DEPTH_STENCIL_DESC depth_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL};
  if (depth_state) depth_state->GetDesc(&depth_desc);
  if (!depth_desc.DepthEnable || depth_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ALL) return false;
  {
    const std::unique_lock lock(state.mutex);
    if (depth_resource.Get() != state.scene_depth) return false;
  }
  const auto depth_shader = std::ranges::find(rt_shadows::DEPTH_SHADERS, shader->hash, &rt_shadows::DepthShader::hash);
  if (depth_shader != rt_shadows::DEPTH_SHADERS.end()) {
    shader->dynamic = depth_shader->dynamic;
    return true;
  }
  ComPtr<ID3D11PixelShader> pixel_shader;
  native->PSGetShader(pixel_shader.GetAddressOf(), nullptr, nullptr);
  if (!pixel_shader || !twattila::rt_scene::SceneCoverage(native, shader)) return false;
  shader->dynamic = std::ranges::find(rt_shadows::SKINNED_MASKED_SHADERS, shader->hash) != rt_shadows::SKINNED_MASKED_SHADERS.end();
  return true;
}

// The direction toward the sun (unit length) from the shadow map's ScreenToShadow rows and the View's ScreenToWorld
// rows (their xyz), as 0x7A1E0D12's TowardSun (sun_direction.hlsli); false: none (a degenerate matrix).
inline bool TowardSun(std::span<const std::array<float, 4>, 3> screen_to_shadow, std::span<const std::array<float, 4>, 3> screen_to_world,
                      std::array<float, 3>* toward_sun) {
  using Vector = std::array<double, 3>;
  const auto cross = [](const Vector& a, const Vector& b) -> Vector {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
  };
  const auto dot = [](const Vector& a, const Vector& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); };
  std::array<Vector, 3> rows;
  for (size_t row = 0; row < 3; ++row) rows[row] = {screen_to_world[row][0], screen_to_world[row][1], screen_to_world[row][2]};
  const double determinant = dot(rows[0], cross(rows[1], rows[2]));
  if (!(std::abs(determinant) > 0.0)) return false;
  // The inverse's columns (k0 - k2), then the shadow map's x, y and z axes in world space.
  const std::array<Vector, 3> inverse = {cross(rows[1], rows[2]), cross(rows[2], rows[0]), cross(rows[0], rows[1])};
  std::array<Vector, 3> axes = {};
  for (size_t axis = 0; axis < 3; ++axis) {
    for (size_t row = 0; row < 3; ++row) {
      for (size_t component = 0; component < 3; ++component) {
        axes[axis][component] += inverse[row][component] / determinant * screen_to_shadow[row][axis];
      }
    }
  }
  Vector toward = cross(axes[0], axes[1]);
  if (dot(toward, axes[2]) > 0.0) toward = {-toward[0], -toward[1], -toward[2]};
  const double length = std::sqrt(dot(toward, toward));
  if (!(length > 0.0) || !std::isfinite(length)) return false;
  *toward_sun = {static_cast<float>(toward[0] / length), static_cast<float>(toward[1] / length), static_cast<float>(toward[2] / length)};
  return true;
}

// The camera from the View's TranslatedWorldToClip (rows c0-c3, row vectors on centimetres) and PreViewTranslation
// (c62): rows acting on metres.
inline twattila::rt_scene::Camera ViewCamera(std::span<const std::array<float, 4>> view) {
  const auto& translation = view[VIEW_PRE_VIEW_TRANSLATION];
  std::array<float, 16> view_projection = {};
  for (size_t row = 0; row < 4; ++row) {
    double offset = view[3][row];
    for (size_t column = 0; column < 3; ++column) {
      view_projection[(row * 4) + column] = static_cast<float>(view[column][row] * CENTIMETRES);
      offset += static_cast<double>(translation[column]) * view[column][row];
    }
    view_projection[(row * 4) + 3] = static_cast<float>(offset);
  }
  return {
      .view_projection = view_projection,
      .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
      .position = {static_cast<float>(-translation[0] / CENTIMETRES), static_cast<float>(-translation[1] / CENTIMETRES),
                   static_cast<float>(-translation[2] / CENTIMETRES)},
  };
}

// `traced` (a bridge texture: the render pixels at its top-left) copied into `kept` at the scene depth's size, as this
// frame's; false: not copied.
inline bool Keep(ID3D11DeviceContext* context, ID3D11ShaderResourceView* traced, uint64_t frame, Kept* kept) {
  if (traced == nullptr) return false;
  ComPtr<ID3D11Resource> resource;
  traced->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  if (FAILED(resource.As(&texture))) return false;
  D3D11_TEXTURE2D_DESC traced_desc = {};
  texture->GetDesc(&traced_desc);
  D3D11_TEXTURE2D_DESC kept_desc = {};
  if (kept->texture) kept->texture->GetDesc(&kept_desc);
  if (kept_desc.Width != state.depth_size[0] || kept_desc.Height != state.depth_size[1] || kept_desc.Format != traced_desc.Format) {
    const D3D11_TEXTURE2D_DESC desc = {.Width = state.depth_size[0], .Height = state.depth_size[1], .MipLevels = 1, .ArraySize = 1,
                                       .Format = traced_desc.Format, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    traced->GetDesc(&view_desc);
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    kept->srv.Reset();
    if (FAILED(device->CreateTexture2D(&desc, nullptr, kept->texture.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateShaderResourceView(kept->texture.Get(), &view_desc, kept->srv.GetAddressOf()))) {
      kept->texture.Reset();
      kept->srv.Reset();
      return false;
    }
  }
  const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = std::min(state.render_size[0], traced_desc.Width),
                            .bottom = std::min(state.render_size[1], traced_desc.Height), .back = 1};
  context->CopySubresourceRegion(kept->texture.Get(), 0, 0, 0, 0, resource.Get(), 0, &region);
  const std::unique_lock lock(state.mutex);
  kept->frame = frame;
  return true;
}

// `kept`'s view when copied in `frame`, else null (under the state's lock).
inline ComPtr<ID3D11ShaderResourceView> ThisFrame(const Kept& kept, uint64_t frame) {
  return (kept.frame == frame) ? kept.srv : nullptr;
}

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .render_size = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.render_size;
    },
    .guides_written = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.guides_frame == internal::state.frame;
    },
    .capture_camera = [](ID3D11DeviceContext*, twattila::rt_scene::Camera*) { return true; },
    .scene_draw = internal::SceneDraw,
    .characters_dynamic = true,
};

// Every present: the settings and a new frame.
inline void Update(const twattila::rt_scene::Settings& settings) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.settings = settings;
    ++internal::state.frame;
  }
  twattila::rt_scene::Update(settings);
}

// Ray Reconstruction's and the helper's G-buffer guides (0x7A1E0D31: `shader`, its 64-byte `constants`) from G-buffer
// A - C (`gbuffer`) and the View (`view`: its rows to ViewToClip's) into the bridge's textures over `extent`.
inline void WriteGuides(ID3D11DeviceContext* context, ID3D11ComputeShader* shader, ID3D11Buffer* constants,
                        const std::array<ID3D11ShaderResourceView*, 3>& gbuffer, std::span<const std::array<float, 4>> view,
                        const std::array<uint32_t, 2>& extent) {
  auto& bridge = twattila::dlaa_bridge::bridge;
  struct GuideConstants {
    std::array<std::array<float, 4>, 3> world_to_view;
    std::array<float, 2> projection;
    std::array<uint32_t, 2> extent;
  };
  static_assert(sizeof(GuideConstants) == 64);
  const GuideConstants guide_constants = {
      .world_to_view = {view[internal::VIEW_TRANSLATED_WORLD_TO_VIEW], view[internal::VIEW_TRANSLATED_WORLD_TO_VIEW + 1],
                        view[internal::VIEW_TRANSLATED_WORLD_TO_VIEW + 2]},
      .projection = {view[internal::VIEW_VIEW_TO_CLIP][0], view[internal::VIEW_VIEW_TO_CLIP + 1][1]},
      .extent = extent,
  };
  context->UpdateSubresource(constants, 0, nullptr, &guide_constants, 0, 0);
  const std::array<ID3D11UnorderedAccessView*, 4> outputs = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(),
                                                             bridge.normals.uav.Get(), bridge.roughness.uav.Get()};
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(0, 1, &constants);
  context->CSSetShaderResources(0, 3, gbuffer.data());
  context->CSSetUnorderedAccessViews(0, 4, outputs.data(), nullptr);
  context->Dispatch((extent[0] + 7) / 8, (extent[1] + 7) / 8, 1);
  constexpr std::array<ID3D11ShaderResourceView*, 3> NO_INPUTS = {};
  constexpr std::array<ID3D11UnorderedAccessView*, 4> NO_OUTPUTS = {};
  context->CSSetShaderResources(0, 3, NO_INPUTS.data());
  context->CSSetUnorderedAccessViews(0, 4, NO_OUTPUTS.data(), nullptr);
}

// At Bandai's sun shadow mask: the camera and the sun to the helper, the trace; the traced sun at t40 (rt_shadows::Unbind
// after the mask), the traced ambient copied for Ambient. False: not traced (nothing bound).
inline bool Trace(ID3D11DeviceContext* context) {
  using internal::state;
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  {
    const std::unique_lock lock(state.mutex);
    settings = state.settings;
    frame = state.frame;
  }
  ComPtr<ID3D11DeviceContext1> context1;
  if (!(settings.sun_shadows || settings.ambient_occlusion) || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return false;
  ComPtr<ID3D11ShaderResourceView> depth;
  context->CSGetShaderResources(0, 1, depth.GetAddressOf());
  std::array<ComPtr<ID3D11Buffer>, 2> buffers;
  std::array<UINT, 2> first_constants = {};
  std::array<UINT, 2> constant_counts = {};
  for (UINT slot = 0; slot < 2; ++slot) {
    context1->CSGetConstantBuffers1(slot, 1, buffers[slot].GetAddressOf(), &first_constants[slot], &constant_counts[slot]);
  }
  if (!depth || !buffers[0] || !buffers[1] || constant_counts[0] <= internal::SCREEN_TO_SHADOW + 2 || constant_counts[1] < internal::VIEW_ROWS) {
    return false;
  }
  std::array<std::array<float, 4>, 3> screen_to_shadow = {};
  std::array<std::array<float, 4>, internal::VIEW_ROWS> view = {};
  talesofarise::dlss::constants::Watch(buffers[0].Get(), (first_constants[0] + internal::SCREEN_TO_SHADOW + 3) * 16);
  talesofarise::dlss::constants::Watch(buffers[1].Get(), (first_constants[1] + internal::VIEW_ROWS) * 16);
  std::array<float, 3> toward_sun = {};
  if (!talesofarise::dlss::constants::Read(buffers[0].Get(), uint64_t{first_constants[0] + internal::SCREEN_TO_SHADOW} * 16,
                                           std::as_writable_bytes(std::span(screen_to_shadow)))
      || !talesofarise::dlss::constants::Read(buffers[1].Get(), uint64_t{first_constants[1]} * 16, std::as_writable_bytes(std::span(view)))
      || !internal::TowardSun(screen_to_shadow, std::span(view).subspan<internal::VIEW_SCREEN_TO_WORLD, 3>(), &toward_sun)) {
    return false;
  }
  ComPtr<ID3D11Resource> depth_resource;
  depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return false;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  const std::array<UINT, 2> render_size = {static_cast<UINT>(view[internal::VIEW_SIZE][0]), static_cast<UINT>(view[internal::VIEW_SIZE][1])};
  if (render_size[0] == 0 || render_size[1] == 0 || render_size[0] > depth_desc.Width || render_size[1] > depth_desc.Height) return false;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (!talesofarise::upscaling::PrepareBridge(device.Get())) return false;
  {
    const std::unique_lock lock(state.mutex);
    state.scene_depth = depth_resource.Get();
    state.render_size = render_size;
    state.depth_size = {depth_desc.Width, depth_desc.Height};
  }
  twattila::rt_scene::WriteSun(context, toward_sun);
  if (!twattila::rt_scene::Trace(context, depth.Get(), internal::ViewCamera(view))) return false;
  const auto [sun, ambient] = twattila::rt_scene::Visibilities();
  if (settings.sun_shadows && sun != nullptr) context->CSSetShaderResources(rt_shadows::VISIBILITY_SLOT, 1, &sun);
  if (settings.ambient_occlusion) internal::Keep(context, ambient, frame, &state.ambient);
  return true;
}

// At the frame's first lighting or toon draw: the guides from its G-buffer (t1-t3) and View (b1), the reflections and
// the specular occlusion traced and copied for t43 / t44 (later draws this frame take the same); null where off or not
// traced.
inline std::array<ComPtr<ID3D11ShaderResourceView>, 2> Reflections(ID3D11DeviceContext* context) {
  using internal::state;
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  {
    const std::unique_lock lock(state.mutex);
    if (state.lit_frame == state.frame) return {internal::ThisFrame(state.reflection, state.frame), internal::ThisFrame(state.specular, state.frame)};
    state.lit_frame = state.frame;
    settings = state.settings;
    frame = state.frame;
  }
  ComPtr<ID3D11DeviceContext1> context1;
  if (!(settings.reflections || settings.specular_occlusion) || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer;
  {
    std::array<ID3D11ShaderResourceView*, 3> pointers = {};
    context->PSGetShaderResources(1, static_cast<UINT>(pointers.size()), pointers.data());
    for (size_t index = 0; index < pointers.size(); ++index) gbuffer[index].Attach(pointers[index]);
  }
  ComPtr<ID3D11Buffer> view_buffer;
  UINT first_constant = 0;
  UINT constant_count = 0;
  context1->PSGetConstantBuffers1(1, 1, view_buffer.GetAddressOf(), &first_constant, &constant_count);
  std::array<std::array<float, 4>, internal::VIEW_GUIDE_ROWS> view = {};
  if (!gbuffer[0] || !gbuffer[1] || !gbuffer[2] || !view_buffer || constant_count < view.size()) return {};
  talesofarise::dlss::constants::Watch(view_buffer.Get(), (first_constant + internal::VIEW_GUIDE_ROWS) * 16);
  if (!talesofarise::dlss::constants::Read(view_buffer.Get(), uint64_t{first_constant} * 16, std::as_writable_bytes(std::span(view)))) return {};
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (state.device != device.Get()) {
    const auto feature_level = device->GetFeatureLevel();
    ComPtr<ID3D11Device1> device1;
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    state.device = device.Get();
    if (FAILED(device.As(&device1))
        || FAILED(device1->CreateDeviceContextState(((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                                                        ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                                                        : 0,
                                                    &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                    state.private_state.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateComputeShader(__0x7A1E0D31.data(), __0x7A1E0D31.size(), nullptr, state.guide_shader.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateBuffer(&constants_desc, nullptr, state.guide_constants.ReleaseAndGetAddressOf()))) {
      state.private_state.Reset();
    }
  }
  if (!state.private_state) return {};
  {
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    WriteGuides(context, state.guide_shader.Get(), state.guide_constants.Get(), {gbuffer[0].Get(), gbuffer[1].Get(), gbuffer[2].Get()}, view,
                state.render_size);
  }
  {
    const std::unique_lock lock(state.mutex);
    state.guides_frame = frame;
  }
  const auto traced = twattila::rt_scene::Reflections(context);
  if (settings.reflections) internal::Keep(context, traced[0], frame, &state.reflection);
  if (settings.specular_occlusion) internal::Keep(context, traced[1], frame, &state.specular);
  const std::unique_lock lock(state.mutex);
  return {internal::ThisFrame(state.reflection, frame), internal::ThisFrame(state.specular, frame)};
}

// This frame's traced ambient for the lighting passes (t41), or null (off, or not traced this frame).
inline ComPtr<ID3D11ShaderResourceView> Ambient() {
  const std::unique_lock lock(internal::state.mutex);
  if (!internal::state.settings.ambient_occlusion) return nullptr;
  return internal::ThisFrame(internal::state.ambient, internal::state.frame);
}

// The helper's settings this frame (Update's).
inline twattila::rt_scene::Settings CurrentSettings() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.settings;
}

// The traced reflections reached the lighting within the last half second: the game's screen-space reflections stay off.
inline bool ReflectingRecently() {
  constexpr uint64_t RECENT_FRAMES = 30;
  const std::unique_lock lock(internal::state.mutex);
  const uint64_t reflected = internal::state.reflection.frame;
  return internal::state.settings.reflections && reflected != UINT64_MAX && reflected + RECENT_FRAMES >= internal::state.frame;
}

// The traced ambient reached last frame's lighting: Bandai's SSAO (before this frame's trace) may leave the AO to it.
inline bool AmbientTracedLastFrame() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.settings.ambient_occlusion && internal::state.ambient.frame + 1 == internal::state.frame;
}

inline void Install() {
  twattila::rt_scene::InstallScene({}, HOST);
}

inline void Uninstall() {
  twattila::rt_scene::UninstallScene();
}

// The game's device is going away: the addon's objects on it with it.
inline void Release(reshade::api::device* device) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.scene_depth = nullptr;
  }
  if (auto& state = internal::state; state.device == reinterpret_cast<ID3D11Device*>(device->get_native())) {
    state.ambient = {};
    state.reflection = {};
    state.specular = {};
    state.device = nullptr;
    state.private_state.Reset();
    state.guide_shader.Reset();
    state.guide_constants.Reset();
  }
  twattila::rt_scene::OnDestroyDevice(device);
}

}  // namespace talesofarise::dlss::ray_tracing
