/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/settings.hpp"
#include "../twattila/frame_history.hpp"
#include "../twattila/light_bounce.h"
#include "../twattila/light_bounce.hpp"
#include "../twattila/lighting_settings.hpp"
#include "./native_state.hpp"

// Global Illumination's GT-VBGI (Light Bounce, Experimental) on an Unreal game: the Total War games' passes
// (twattila/light_bounce_*.hlsli, game-neutral; the trace is GT-VBGI's visibility bitmask) on Unreal's G-buffer and View
// (ue_light_bounce.hlsli, each game's ue_light_bounce_*.hlsl wrappers), from the scene as the game's lights left it: the
// prepare (that light, at 1 / scale of the render size, with its depth and normal) and its levels, the trace, the
// resolve with the last frames' bounce (the View's Prev* rows: where each point was), the optional denoiser, then the
// apply (the bounce brought up to each render pixel along surfaces, x its diffuse colour x Strength) added to the game's
// light, or into the add-on's own texture for a game whose lighting pass multiplies it in itself. In the add-on's own
// context state (the game's bindings stay); D3D11. Also the rows (lighting_settings.hpp's Global Illumination, Attila's
// keys) and what their sliders make of the passes' constants.
namespace talesofarise::ue_light_bounce {

using Microsoft::WRL::ComPtr;
using twattila::frame_history::Target;

// The rows' bindings, named as lighting_settings::Append binds a shader_injection: its Global Illumination rows bind
// light_bounce_*; its ambient occlusion rows (ssao_*) are left out (each game has its own: ssao_thickness is set from
// them, the passes' thickness).
struct Rows {
  float ssao_mode = 0.f;
  float ssao_strength = 1.f;
  float ssao_radius = 1.f;
  float ssao_thickness = 0.15f;
  float ssao_quality = 0.f;
  float ssao_directions = 0.f;
  float ssao_resolution = 0.f;
  float ssao_auto_thickness = 0.f;
  float light_bounce_strength = 0.f;
  float light_bounce_radius = 20.f;
  float light_bounce_resolution = 2.f;
  float light_bounce_quality = 2.f;
  float light_bounce_directions = 0.f;
  float light_bounce_temporal = 1.f;
  float light_bounce_denoise = 1.f;
  float light_bounce_multi_bounce = 0.f;
  float light_bounce_checkerboard = 1.f;
  float light_bounce_backface = 0.f;
  float light_bounce_auto_thickness = 0.f;
};

// Global Illumination's GT-VBGI rows (lighting_settings::Append's, with its presets) onto `settings`, bound to `rows`;
// `traced`: the rays find the bounce (the screen-space search's rows off).
inline void AppendRows(std::vector<renodx::utils::settings::Setting*>* settings, Rows* rows, const std::function<bool()>& traced) {
  std::vector<renodx::utils::settings::Setting*> sections;
  twattila::lighting_settings::Append(&sections, rows, traced);
  for (renodx::utils::settings::Setting* setting : sections) {
    if (setting->section == twattila::lighting_settings::GI_SECTION) {
      settings->push_back(setting);
    } else {
      delete setting;
    }
  }
}

// GT-VBGI joins Global Illumination's methods (graphics_page.hpp Bindings::has_gtvbgi; light_bounce_strength's default
// 0, Off). Before, a preset's saved strength with the rays' bounce off had Global Illumination off: such presets keep
// it off (strength 0; choosing the rays brings it back to 1), once (SettingsRevision `revision`), before the settings
// are loaded.
inline void KeepSavedGiOff(int revision) {
  const std::string& global = renodx::utils::settings::global_name;
  if (int saved = 0; reshade::get_config_value(nullptr, global.c_str(), "SettingsRevision", saved) && saved >= revision) return;
  for (const char* preset : {"-preset1", "-preset2", "-preset3"}) {
    const std::string section = global + preset;
    float rays = 0.f;
    reshade::get_config_value(nullptr, section.c_str(), "RayTracedLightBounce", rays);
    if (float strength = 0.f; rays == 0.f && reshade::get_config_value(nullptr, section.c_str(), "light_bounce_strength", strength) && strength != 0.f) {
      reshade::set_config_value(nullptr, section.c_str(), "light_bounce_strength", 0.f);
    }
  }
  reshade::set_config_value(nullptr, global.c_str(), "SettingsRevision", revision);
}

// The passes' constants from the rows' sliders (twattila::light_bounce's, strength 0: off) in the add-on's frame `frame`
// (the noise turns with it), as Attila's (twattila/light_bounce.hpp Update).
inline LightBounceConstants ConstantsOf(const twattila::light_bounce::Sliders& sliders, uint64_t frame) {
  using twattila::light_bounce::internal::PowerOfTwo;
  const bool temporal = (sliders.temporal != 0.f);
  return {
      .strength = sliders.strength,
      .radius = sliders.radius,
      .thickness = sliders.thickness,
      .steps = 8.f * PowerOfTwo(sliders.quality, 5.f),
      .slices = PowerOfTwo(sliders.directions, 4.f),
      .scale = PowerOfTwo(sliders.resolution, 3.f),
      .history = (temporal ? twattila::light_bounce::internal::HISTORY_SHARE : 0.f),
      .frame = (temporal ? static_cast<float>(frame % 64) : 0.f),
      .bounces = std::clamp(sliders.multi_bounce, 0.f, 1.f),
      .checkerboard = ((temporal && sliders.checkerboard != 0.f) ? 1.f : 0.f),
      .backface = ((sliders.backface != 0.f) ? 1.f : 0.f),
      .auto_thickness = ((sliders.auto_thickness != 0.f) ? 1.f : 0.f),
  };
}

struct Inputs {
  ID3D11ShaderResourceView* light = nullptr;      // the scene as the game's lights left it (what bounces)
  ID3D11ShaderResourceView* depth = nullptr;      // the scene depth (device Z, reversed)
  ID3D11ShaderResourceView* gbuffer_a = nullptr;  // world normal x 0.5 + 0.5
  ID3D11ShaderResourceView* gbuffer_b = nullptr;  // metallic, specular, roughness, shading model
  ID3D11ShaderResourceView* gbuffer_c = nullptr;  // base colour
  dlss::ConstantBinding view;                     // the View uniform buffer
  std::array<UINT, 2> render_size = {};           // the view's pixels (the textures' top-left)
};

namespace internal {

using twattila::light_bounce::internal::PREPARED_LEVELS;
using twattila::light_bounce::internal::PreparedTarget;
// The views at t40-t48: the light (then the trace or the resolve's input), the history (then the apply's bounce), the
// G-buffer (t42 A, t43 C, t44 B, t45 depth), the prepared light and geometry, the resolve's last frame counts.
constexpr UINT VIEWS_SLOT = 40;
constexpr size_t LIGHT_VIEW = 0;
constexpr size_t HISTORY_VIEW = 1;
constexpr size_t GBUFFER_A_VIEW = 2;
constexpr size_t GBUFFER_C_VIEW = 3;
constexpr size_t GBUFFER_B_VIEW = 4;
constexpr size_t DEPTH_VIEW = 5;
constexpr size_t PREPARED_VIEW = 6;
constexpr size_t FRAMES_VIEW = 8;
constexpr UINT VIEW_COUNT = 9;
// The View at b11 (ue_light_bounce.hlsli), the Light Bounce constants at b12 (light_bounce.h).
constexpr UINT VIEW_SLOT = 11;
constexpr UINT CONSTANTS_SLOT = 12;

struct Objects {
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> prepare;
  ComPtr<ID3D11PixelShader> levels;
  ComPtr<ID3D11PixelShader> trace;
  ComPtr<ID3D11PixelShader> resolve;
  ComPtr<ID3D11PixelShader> denoise;
  ComPtr<ID3D11PixelShader> apply;
  ComPtr<ID3D11BlendState> add_blend;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ComPtr<ID3D11Buffer> constants;
  LightBounceConstants uploaded = {};
  // The render size and scale the prepared textures and histories (1 / scale of it) are made for.
  std::array<UINT, 2> render_size = {};
  UINT scale = 0;
  PreparedTarget prepared_light;
  PreparedTarget prepared_geometry;
  UINT prepared_levels = 0;
  Target traced;
  std::array<Target, 2> history;
  std::array<Target, 2> history_frames;
  size_t history_index = 0;  // the one the last resolve wrote
  Target denoised;
  // The add-on's own bounce (the light arriving x Strength) at the scene depth's size.
  Target irradiance;
  std::array<UINT, 2> irradiance_size = {};
};
static Objects objects;

inline bool CreateShaders(ID3D11Device* device) {
  if (objects.device.Get() == device) return !objects.failed;
  objects = {.device = device};
  D3D11_BLEND_DESC add_desc = {};
  add_desc.RenderTarget[0] = {
      .BlendEnable = TRUE,
      .SrcBlend = D3D11_BLEND_ONE,
      .DestBlend = D3D11_BLEND_ONE,
      .BlendOp = D3D11_BLEND_OP_ADD,
      .SrcBlendAlpha = D3D11_BLEND_ZERO,
      .DestBlendAlpha = D3D11_BLEND_ONE,
      .BlendOpAlpha = D3D11_BLEND_OP_ADD,
      .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE,
  };
  const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE};
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = sizeof(LightBounceConstants), .Usage = D3D11_USAGE_DEFAULT,
                                            .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const auto pixel = [&](const auto& code, ComPtr<ID3D11PixelShader>* shader) {
    return SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
  };
  objects.failed = !dlss::MakeContextState(device, &objects.private_state)
                   || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, objects.full_screen.GetAddressOf()))
                   || !pixel(__ue_light_bounce_prepare, &objects.prepare) || !pixel(__light_bounce_levels, &objects.levels)
                   || !pixel(__ue_light_bounce_trace, &objects.trace) || !pixel(__ue_light_bounce_resolve, &objects.resolve)
                   || !pixel(__ue_light_bounce_denoise, &objects.denoise) || !pixel(__ue_light_bounce_apply, &objects.apply)
                   || FAILED(device->CreateBlendState(&add_desc, objects.add_blend.GetAddressOf()))
                   || FAILED(device->CreateRasterizerState(&rasterizer_desc, objects.rasterizer.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&constants_desc, nullptr, objects.constants.GetAddressOf()));
  return !objects.failed;
}

// The textures for this render size and scale (made again when either changes), cleared: no bounce and no history
// until the first trace.
inline bool CreateTextures(ID3D11DeviceContext* context, std::array<UINT, 2> render_size, UINT scale) {
  if (objects.render_size == render_size && objects.scale == scale) return static_cast<bool>(objects.denoised.texture);
  auto* device = objects.device.Get();
  objects.render_size = render_size;
  objects.scale = scale;
  objects.denoised = {};
  const UINT width = (render_size[0] + scale - 1) / scale;
  const UINT height = (render_size[1] + scale - 1) / scale;
  objects.prepared_levels = std::min<UINT>(PREPARED_LEVELS, std::bit_width(std::max(width, height)));
  const auto make_prepared = [&](DXGI_FORMAT format, PreparedTarget* prepared) {
    *prepared = {};
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = width,
        .Height = height,
        .MipLevels = objects.prepared_levels,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };
    if (FAILED(device->CreateTexture2D(&desc, nullptr, prepared->texture.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(prepared->texture.Get(), nullptr, prepared->srv.GetAddressOf()))) {
      return false;
    }
    for (UINT level = 0; level < objects.prepared_levels; ++level) {
      const D3D11_RENDER_TARGET_VIEW_DESC level_target = {.Format = format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
                                                          .Texture2D = {.MipSlice = level}};
      const D3D11_SHADER_RESOURCE_VIEW_DESC level_view = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                          .Texture2D = {.MostDetailedMip = level, .MipLevels = 1}};
      if (FAILED(device->CreateRenderTargetView(prepared->texture.Get(), &level_target, prepared->level_rtvs[level].GetAddressOf()))
          || FAILED(device->CreateShaderResourceView(prepared->texture.Get(), &level_view, prepared->level_srvs[level].GetAddressOf()))) {
        return false;
      }
    }
    return true;
  };
  using twattila::frame_history::MakeTarget;
  if (!make_prepared(DXGI_FORMAT_R11G11B10_FLOAT, &objects.prepared_light) || !make_prepared(DXGI_FORMAT_R32G32_UINT, &objects.prepared_geometry)
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.traced)
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.history[0])
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.history[1])
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16_FLOAT, &objects.history_frames[0])
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16_FLOAT, &objects.history_frames[1])
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.denoised)) {
    objects.denoised = {};
    return false;
  }
  constexpr std::array<FLOAT, 4> NOTHING = {};
  for (const Target* target : {&objects.history[0], &objects.history[1], &objects.history_frames[0], &objects.history_frames[1],
                               &objects.denoised}) {
    context->ClearRenderTargetView(target->rtv.Get(), NOTHING.data());
  }
  return true;
}

}  // namespace internal

// This frame's bounce from `inputs` with `constants` (strength 0: off) and the denoiser (`denoise`), added into
// `target` (the game's light, at the render size; its diffuse colour from the G-buffer), or, without one, written into
// the add-on's own texture the size of the scene depth (its render-size top-left: the light arriving x Strength), which
// is returned for the game's lighting pass. Null: off, not available, or added into `target`.
inline ID3D11ShaderResourceView* Apply(ID3D11DeviceContext* context, const Inputs& inputs, const LightBounceConstants& constants,
                                       bool denoise, ID3D11RenderTargetView* target) {
  using internal::objects;
  ComPtr<ID3D11DeviceContext1> context1;
  if (!(constants.strength > 0.f) || inputs.light == nullptr || inputs.depth == nullptr || inputs.gbuffer_a == nullptr
      || inputs.gbuffer_b == nullptr || inputs.gbuffer_c == nullptr || !inputs.view.buffer || inputs.render_size[0] == 0
      || inputs.render_size[1] == 0 || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
    return nullptr;
  }
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const auto scale = static_cast<UINT>(constants.scale);
  if (!internal::CreateShaders(device.Get()) || !internal::CreateTextures(context, inputs.render_size, scale)) return nullptr;
  if (target == nullptr) {
    ComPtr<ID3D11Resource> depth_resource;
    inputs.depth->GetResource(depth_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> depth_texture;
    if (FAILED(depth_resource.As(&depth_texture))) return nullptr;
    D3D11_TEXTURE2D_DESC depth_desc = {};
    depth_texture->GetDesc(&depth_desc);
    if (objects.irradiance_size != std::array<UINT, 2>{depth_desc.Width, depth_desc.Height}) {
      objects.irradiance_size = {};
      if (!twattila::frame_history::MakeTarget(device.Get(), depth_desc.Width, depth_desc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.irradiance)) {
        return nullptr;
      }
      constexpr std::array<FLOAT, 4> NOTHING = {};
      context->ClearRenderTargetView(objects.irradiance.rtv.Get(), NOTHING.data());
      objects.irradiance_size = {depth_desc.Width, depth_desc.Height};
    }
  }
  if (std::memcmp(&objects.uploaded, &constants, sizeof(LightBounceConstants)) != 0) {
    context->UpdateSubresource(objects.constants.Get(), 0, nullptr, &constants, 0, 0);
    objects.uploaded = constants;
  }

  const dlss::ContextStateScope scope(context1.Get(), objects.private_state.Get());
  std::array<ID3D11ShaderResourceView*, internal::VIEW_COUNT> views = {};
  views[internal::GBUFFER_A_VIEW] = inputs.gbuffer_a;
  views[internal::GBUFFER_C_VIEW] = inputs.gbuffer_c;
  views[internal::GBUFFER_B_VIEW] = inputs.gbuffer_b;
  views[internal::DEPTH_VIEW] = inputs.depth;
  // A whole-buffer binding reads back as offset 0 and 4096 constants, more than a small buffer holds.
  if (inputs.view.first == 0) {
    context1->PSSetConstantBuffers(internal::VIEW_SLOT, 1, inputs.view.buffer.GetAddressOf());
  } else {
    context1->PSSetConstantBuffers1(internal::VIEW_SLOT, 1, inputs.view.buffer.GetAddressOf(), &inputs.view.first, &inputs.view.count);
  }
  context1->PSSetConstantBuffers(internal::CONSTANTS_SLOT, 1, objects.constants.GetAddressOf());
  context1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context1->VSSetShader(objects.full_screen.Get(), nullptr, 0);
  context1->RSSetState(objects.rasterizer.Get());
  const D3D11_VIEWPORT trace_viewport = {
      .Width = std::ceil(static_cast<float>(inputs.render_size[0]) / scale),
      .Height = std::ceil(static_cast<float>(inputs.render_size[1]) / scale),
      .MaxDepth = 1.f,
  };
  context1->RSSetViewports(1, &trace_viewport);

  // The prepare (the light; the last history for Multi-Bounce), then the levels.
  views[internal::LIGHT_VIEW] = inputs.light;
  views[internal::HISTORY_VIEW] = objects.history[objects.history_index].srv.Get();
  context1->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, views.data());
  const std::array<ID3D11RenderTargetView*, 2> prepared = {objects.prepared_light.level_rtvs[0].Get(), objects.prepared_geometry.level_rtvs[0].Get()};
  context1->OMSetRenderTargets(2, prepared.data(), nullptr);
  context1->PSSetShader(objects.prepare.Get(), nullptr, 0);
  context1->Draw(3, 0);
  D3D11_TEXTURE2D_DESC prepared_desc = {};
  objects.prepared_light.texture->GetDesc(&prepared_desc);
  context1->PSSetShader(objects.levels.Get(), nullptr, 0);
  for (UINT level = 1; level < objects.prepared_levels; ++level) {
    const std::array<ID3D11RenderTargetView*, 2> level_targets = {objects.prepared_light.level_rtvs[level].Get(),
                                                                  objects.prepared_geometry.level_rtvs[level].Get()};
    context1->OMSetRenderTargets(2, level_targets.data(), nullptr);
    const std::array<ID3D11ShaderResourceView*, 2> above = {objects.prepared_light.level_srvs[level - 1].Get(),
                                                            objects.prepared_geometry.level_srvs[level - 1].Get()};
    context1->PSSetShaderResources(internal::VIEWS_SLOT + internal::PREPARED_VIEW, 2, above.data());
    const D3D11_VIEWPORT level_viewport = {
        .Width = static_cast<FLOAT>(std::max(1u, prepared_desc.Width >> level)),
        .Height = static_cast<FLOAT>(std::max(1u, prepared_desc.Height >> level)),
        .MaxDepth = 1.f,
    };
    context1->RSSetViewports(1, &level_viewport);
    context1->Draw(3, 0);
  }
  context1->RSSetViewports(1, &trace_viewport);

  // The trace, the resolve into the other history (and its frame counts), the denoiser; each target set before its
  // input, so no texture is bound as both.
  const auto pass = [&](const Target& output, ID3D11PixelShader* shader, const Target* frames) {
    const std::array<ID3D11RenderTargetView*, 2> outputs = {output.rtv.Get(), (frames != nullptr) ? frames->rtv.Get() : nullptr};
    context1->OMSetRenderTargets((frames != nullptr) ? 2 : 1, outputs.data(), nullptr);
    context1->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, views.data());
    context1->PSSetShader(shader, nullptr, 0);
    context1->Draw(3, 0);
  };
  const size_t previous_index = objects.history_index;
  objects.history_index ^= 1;
  views[internal::LIGHT_VIEW] = nullptr;
  views[internal::HISTORY_VIEW] = objects.history[previous_index].srv.Get();
  views[internal::PREPARED_VIEW] = objects.prepared_light.srv.Get();
  views[internal::PREPARED_VIEW + 1] = objects.prepared_geometry.srv.Get();
  pass(objects.traced, objects.trace.Get(), nullptr);
  views[internal::LIGHT_VIEW] = objects.traced.srv.Get();
  views[internal::FRAMES_VIEW] = objects.history_frames[previous_index].srv.Get();
  pass(objects.history[objects.history_index], objects.resolve.Get(), &objects.history_frames[objects.history_index]);
  views[internal::FRAMES_VIEW] = nullptr;
  if (denoise) {
    views[internal::LIGHT_VIEW] = objects.history[objects.history_index].srv.Get();
    views[internal::HISTORY_VIEW] = nullptr;
    pass(objects.denoised, objects.denoise.Get(), nullptr);
  }

  // The apply at the render size: added to the game's light, or into the add-on's own texture.
  views[internal::LIGHT_VIEW] = nullptr;
  views[internal::HISTORY_VIEW] = (denoise ? objects.denoised.srv.Get() : objects.history[objects.history_index].srv.Get());
  views[internal::PREPARED_VIEW] = nullptr;
  views[internal::PREPARED_VIEW + 1] = nullptr;
  ID3D11RenderTargetView* output = (target != nullptr) ? target : objects.irradiance.rtv.Get();
  context1->OMSetRenderTargets(1, &output, nullptr);
  if (target != nullptr) {
    context1->OMSetBlendState(objects.add_blend.Get(), nullptr, 0xFFFFFFFF);
  }
  context1->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, views.data());
  context1->PSSetShader(objects.apply.Get(), nullptr, 0);
  const D3D11_VIEWPORT render_viewport = {
      .Width = static_cast<float>(inputs.render_size[0]),
      .Height = static_cast<float>(inputs.render_size[1]),
      .MaxDepth = 1.f,
  };
  context1->RSSetViewports(1, &render_viewport);
  context1->Draw(3, 0);
  return (target != nullptr) ? nullptr : objects.irradiance.srv.Get();
}

// The game's device is going away: the add-on's objects on it with it.
inline void Release(ID3D11Device* device) {
  if (internal::objects.device.Get() == device) {
    internal::objects = {};
  }
}

}  // namespace talesofarise::ue_light_bounce
