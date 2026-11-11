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
#include <format>
#include <shared_mutex>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/pipeline.hpp"
#include "../twattila/dxbc.hpp"
#include "../twattila/particle_history.hpp"

// The game's particles (fx_vfx_render, MBOIT): moments into 3 targets, then colour into one, each pass twice: all that
// the game keeps sharp at the render's size, then what it marks as soft into a half-size layer with its own depth. Traced in 93
// frames, always in that order at every scale. Colour sums linearly in 43606BB8, so a factor on it is that much light, nothing else.
// Smooth Half-Size Particles: the half layer's colour and zeroth moment smoothed over frames (twattila/particle_history.hpp) and
// composited by depth (particle_composite.ps_5_0.hlsl in 43606BB8's place; trace bm_siege/trace_35 lines 1280-1305: the half
// layer's depth is the farthest of each 2 x 2 (7C758D23), A759AD5D adds its moments to the sharp ones by nearest, 43606BB8 reads
// its colour bilinear without depth).
namespace twwarhammer3::vfx_particles {

using Microsoft::WRL::ComPtr;
namespace dxbc = twattila::dxbc;
namespace particle_history = twattila::particle_history;

static float fire_and_glow = 1.f;
static float smoke_and_dust = 1.f;
static float effect_meshes = 1.f;
static float particle_fog = 1.f;
static float half_resolution_row = 0.f;
static float smoothing_row = 0.f;

namespace internal {

constexpr uint32_t SPRITE_MOMENTS_PS = 0x713B947E;
constexpr uint32_t SPRITE_COLOUR_PS = 0x3373F064;
constexpr uint32_t MOMENTS_COMBINE_PS = 0xA759AD5D;
constexpr uint32_t COMPOSITE_PS = 0x43606BB8;
constexpr UINT BRIGHTNESS_SLOT = 11;
// The sprite VS's cb_vfx: g_draw_params.x is the layer's bin, so a layer's draw needs its own.
constexpr UINT DRAW_PARAMS_SLOT = 7;
// The sprites' moments shader's g_tex_depth: the depth its layer is drawn against.
constexpr UINT MOMENTS_DEPTH_SLOT = 4;

// Colour passes in the traces, by census. brightness_component: which of the 4 rows in the constants.
struct ColourShader {
  uint32_t hash = 0u;
  uint32_t brightness_component = 0u;
};
constexpr std::array<ColourShader, 7> COLOUR_SHADERS = {{
    {.hash = SPRITE_COLOUR_PS, .brightness_component = 0u},  // Sprites: the lit bit picks 0 or 1 inside the shader.
    {.hash = 0x6C9E64ED, .brightness_component = 2u},        // wh3_vfx_custom_particle
    {.hash = 0x76D82971, .brightness_component = 2u},        // wh3_vfx_custom_basic
    {.hash = 0x6BFD17D8, .brightness_component = 2u},        // fx_animated_uvs_custom
    {.hash = 0xE81A33DC, .brightness_component = 2u},        // wh3_vfx_custom_basic_pre_lit
    {.hash = 0xC22867B8, .brightness_component = 3u},        // vfx_particle_fog
    {.hash = 0x30A48C40, .brightness_component = 3u},        // vfx_particle_fog_shrouded
}};

struct PatchedShader {
  std::vector<uint8_t> code;
  ComPtr<ID3D11PixelShader> patched;
  bool refused = false;
};

// The sharp layer's sprite draw, held until the half layer's state is bound.
struct HeldDraw {
  ComPtr<ID3D11Buffer> arguments;
  UINT offset = 0u;
  UINT target_width = 0u;
  ComPtr<ID3D11Buffer> draw_params;
  ComPtr<ID3D11Buffer> vertices;
  UINT vertex_stride = 0u;
  UINT vertex_offset = 0u;
  ComPtr<ID3D11Buffer> indices;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0u;
};

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Buffer> brightness;
  std::array<float, 4> written = {-1.f, -1.f, -1.f, -1.f};
  ComPtr<ID3D11PixelShader> game_shader;
  ComPtr<ID3D11Buffer> game_brightness;
  std::array<HeldDraw, 2> held;  // Moments, colour.
  ComPtr<ID3D11PixelShader> composite;  // Smooth Half-Size Particles' 43606BB8
  bool composite_failed = false;
  particle_history::History history;
};

// A sprite moments draw this frame (in the traces the sharp layer's, then the half one's): the depth its layer is drawn
// against, that depth's width, the layer's viewport.
struct LayerDraw {
  ComPtr<ID3D11ShaderResourceView> depth;
  UINT width = 0;
  D3D11_VIEWPORT viewport = {};
};

// Smooth Half-Size Particles: this frame's layers (released at each present: they hold the game's textures) and the composite's
// state, put back after it.
struct Smoothing {
  uint64_t frame = 0;  // counted at each present
  std::array<LayerDraw, 4> layers;
  size_t layer_count = 0;
  ComPtr<ID3D11ShaderResourceView> half_optical_depth;  // the half layer's zeroth moment (A759AD5D's t0)
  bool restart = true;                                   // the history starts again at the next resolve
  float half_resolution_seen = 0.f;
  bool depth_clash_logged = false;
  ComPtr<ID3D11PixelShader> game_shader;
  std::array<ID3D11ShaderResourceView*, particle_history::VIEW_COUNT> game_views = {};
  std::array<ID3D11Buffer*, particle_history::CONSTANTS_COUNT> game_constants = {};
  bool applied_logged = false;
};

static std::shared_mutex mutex;
static std::unordered_map<uint32_t, PatchedShader> colour_shaders;  // By the game's hash.
static DeviceObjects objects;
static Smoothing smoothing;

// Before the one `mul o0.xyz, a, b`: its result goes to a new temp, then o0.xyz = temp x the row. Sprites: after
// `and rN.xyz, r, l(0x20000, 0x2000000, 0x100000, 0)`, rN.z is the emitter's lit bit: temp.w = lit ? cb11[0].y : cb11[0].x.
inline std::vector<uint8_t> PatchColourBrightness(std::span<const uint8_t> code, const ColourShader& shader) {
  constexpr uint32_t OP_AND = 1, OP_MOVC = 55, OP_MUL = 56, OP_DCL_CONSTANT_BUFFER = 89, OP_DCL_TEMPS = 104;
  constexpr uint32_t OUTPUT_XYZ = 0x00102072, TEMP_XYZ = 0x00100072, TEMP_W = 0x00100082;
  constexpr uint32_t TEMP_XYZX = 0x00100246, TEMP_WWWW = 0x00100FF6, IMMEDIATE_VECTOR = 0x00004002;
  constexpr uint32_t LIT_BIT = 0x00100000;
  constexpr uint32_t ROW_X = 0x0020800A, ROW_Y = 0x0020801A;  // cb[][].x, .y
  constexpr std::array<uint32_t, 4> ROW_ALL = {0x00208006, 0x00208556, 0x00208AA6, 0x00208FF6};  // cb[][].xxxx ... .wwww
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  size_t temps_at = 0;
  for (size_t at = 2; at < tokens.size();) {
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_DCL_TEMPS) temps_at = at;
    if (opcode == OP_DCL_CONSTANT_BUFFER && tokens[at + 2] == BRIGHTNESS_SLOT) return {};
    at += length;
  }
  if (temps_at == 0) return {};
  const uint32_t new_temp = tokens[temps_at + 1];
  const bool sprite = shader.hash == SPRITE_COLOUR_PS;
  std::vector<uint32_t> patched(tokens.begin(), tokens.begin() + 2);
  size_t patched_temps_at = 0;
  int lit_found = 0;
  int colour_found = 0;
  for (size_t at = 2; at < tokens.size();) {
    const size_t length = dxbc::Length(tokens, at);
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_DCL_TEMPS) patched_temps_at = patched.size();
    const size_t first_token = patched.size();
    patched.insert(patched.end(), tokens.begin() + static_cast<ptrdiff_t>(at), tokens.begin() + static_cast<ptrdiff_t>(at + length));
    if (sprite && opcode == OP_AND) {
      const auto end = tokens.begin() + static_cast<ptrdiff_t>(at + length);
      const auto immediate = std::find(tokens.begin() + static_cast<ptrdiff_t>(at + 1), end, IMMEDIATE_VECTOR);
      if (immediate != end && end - immediate == 5 && immediate[3] == LIT_BIT) {
        const size_t destination = dxbc::internal::FirstOperand(tokens, at);
        if (tokens[destination] != TEMP_XYZ) return {};
        patched.insert(patched.end(), {OP_MOVC | (11u << 24), TEMP_W, new_temp, 0x0010002A, tokens[destination + 1],
                                       ROW_Y, BRIGHTNESS_SLOT, 0u, ROW_X, BRIGHTNESS_SLOT, 0u});
        ++lit_found;
      }
    }
    if (opcode == OP_MUL) {
      const size_t destination = dxbc::internal::FirstOperand(tokens, at);
      if (tokens[destination] == OUTPUT_XYZ && tokens[destination + 1] == 0u) {
        if ((tokens[at] & (1u << 13)) != 0) return {};
        const size_t patched_destination = first_token + (destination - at);
        patched[patched_destination] = TEMP_XYZ;
        patched[patched_destination + 1] = new_temp;
        if (sprite) {
          patched.insert(patched.end(), {OP_MUL | (7u << 24), OUTPUT_XYZ, 0u, TEMP_XYZX, new_temp, TEMP_WWWW, new_temp});
        } else {
          patched.insert(patched.end(), {OP_MUL | (8u << 24), OUTPUT_XYZ, 0u, TEMP_XYZX, new_temp,
                                         ROW_ALL[shader.brightness_component], BRIGHTNESS_SLOT, 0u});
        }
        ++colour_found;
      }
    }
    at += length;
  }
  if (colour_found != 1 || lit_found != (sprite ? 1 : 0)) return {};
  patched[patched_temps_at + 1] = new_temp + 1;
  const std::array<uint32_t, 4> declaration = {OP_DCL_CONSTANT_BUFFER | (4u << 24), 0x00208E46, BRIGHTNESS_SLOT, 1u};
  patched.insert(patched.begin() + static_cast<ptrdiff_t>(patched_temps_at), declaration.begin(), declaration.end());
  patched[1] = static_cast<uint32_t>(patched.size());
  tokens = std::move(patched);
  return dxbc::Write(code, program);
}

inline void OnInitPipeline(renodx::utils::pipeline::PipelineInfo* info, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects) {
  for (const auto& identity : info->shader_details) {
    if (std::ranges::find(COLOUR_SHADERS, identity.shader_hash, &ColourShader::hash) == COLOUR_SHADERS.end()) continue;
    for (uint32_t index = 0; index < subobject_count; ++index) {
      if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
      const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
      if (desc == nullptr || desc->code == nullptr) continue;
      const auto* bytes = static_cast<const uint8_t*>(desc->code);
      const std::unique_lock lock(mutex);
      colour_shaders[identity.shader_hash] = {.code = std::vector<uint8_t>(bytes, bytes + desc->code_size)};
    }
  }
}

inline bool BrightnessRowsOn() {
  return std::abs(fire_and_glow - 1.f) >= 0.01f || std::abs(smoke_and_dust - 1.f) >= 0.01f
         || std::abs(effect_meshes - 1.f) >= 0.01f || std::abs(particle_fog - 1.f) >= 0.01f;
}

inline void BindDevice(ID3D11Device* device) {
  if (objects.device.Get() == device) return;
  objects = {.device = device};
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  device->CreateBuffer(&constants_desc, nullptr, objects.brightness.GetAddressOf());
  for (auto& [hash, entry] : colour_shaders) entry = {.code = std::move(entry.code)};
}

// With the row on, the sharp layer's sprites wait here and draw in the half layer's state: its targets, depth, camera and moments
// scale are bound for exactly this, and only the bin (and quad buffers) come from their own draw. Effect meshes stay sharp: their
// depth sits in another slot per material. Returns true for the draw that's held.
inline bool HoldOrReplay(const renodx::utils::command_action::IndirectArguments& arguments, ID3D11DeviceContext* native, HeldDraw* held) {
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return false;
  ComPtr<ID3D11Resource> target;
  target_view->GetResource(target.GetAddressOf());
  ComPtr<ID3D11Texture2D> target_texture;
  if (FAILED(target.As(&target_texture))) return false;
  D3D11_TEXTURE2D_DESC target_desc = {};
  target_texture->GetDesc(&target_desc);

  // An odd sharp width's half is rounded either way.
  if (held->arguments && std::abs(static_cast<int>(target_desc.Width * 2) - static_cast<int>(held->target_width)) <= 1) {
    ComPtr<ID3D11Buffer> half_params;
    ComPtr<ID3D11Buffer> half_vertices;
    ComPtr<ID3D11Buffer> half_indices;
    UINT half_stride = 0u;
    UINT half_vertex_offset = 0u;
    DXGI_FORMAT half_format = DXGI_FORMAT_UNKNOWN;
    UINT half_index_offset = 0u;
    native->VSGetConstantBuffers(DRAW_PARAMS_SLOT, 1, half_params.GetAddressOf());
    native->IAGetVertexBuffers(0, 1, half_vertices.GetAddressOf(), &half_stride, &half_vertex_offset);
    native->IAGetIndexBuffer(half_indices.GetAddressOf(), &half_format, &half_index_offset);
    native->VSSetConstantBuffers(DRAW_PARAMS_SLOT, 1, held->draw_params.GetAddressOf());
    native->IASetVertexBuffers(0, 1, held->vertices.GetAddressOf(), &held->vertex_stride, &held->vertex_offset);
    native->IASetIndexBuffer(held->indices.Get(), held->index_format, held->index_offset);
    native->DrawIndexedInstancedIndirect(held->arguments.Get(), held->offset);
    native->VSSetConstantBuffers(DRAW_PARAMS_SLOT, 1, half_params.GetAddressOf());
    native->IASetVertexBuffers(0, 1, half_vertices.GetAddressOf(), &half_stride, &half_vertex_offset);
    native->IASetIndexBuffer(half_indices.Get(), half_format, half_index_offset);
    held->arguments.Reset();
    return false;
  }

  ComPtr<ID3D11Buffer> game_params;
  native->VSGetConstantBuffers(DRAW_PARAMS_SLOT, 1, game_params.GetAddressOf());
  if (!game_params) return false;
  D3D11_BUFFER_DESC params_desc = {};
  game_params->GetDesc(&params_desc);
  bool remade = !held->draw_params;
  if (!remade) {
    D3D11_BUFFER_DESC own_desc = {};
    held->draw_params->GetDesc(&own_desc);
    remade = own_desc.ByteWidth != params_desc.ByteWidth;
  }
  if (remade) {
    held->draw_params.Reset();
    const D3D11_BUFFER_DESC own_desc = {.ByteWidth = params_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(objects.device->CreateBuffer(&own_desc, nullptr, held->draw_params.GetAddressOf()))) return false;
  }
  // The game writes this buffer again for the next draw.
  native->CopyResource(held->draw_params.Get(), game_params.Get());
  held->arguments = reinterpret_cast<ID3D11Buffer*>(arguments.buffer.handle);
  held->offset = static_cast<UINT>(arguments.offset);
  held->target_width = target_desc.Width;
  held->vertices.Reset();
  held->indices.Reset();
  native->IAGetVertexBuffers(0, 1, held->vertices.GetAddressOf(), &held->vertex_stride, &held->vertex_offset);
  native->IAGetIndexBuffer(held->indices.GetAddressOf(), &held->index_format, &held->index_offset);
  return true;
}

// Rows off: nothing is touched, the game's shaders and draws run.
constexpr auto OnParticleDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(command.arguments)>, renodx::utils::command_action::IndirectArguments>) {
    return {};
  } else {
  const uint32_t hash = command.matched_shader_hash;
  const bool sprite = hash == SPRITE_MOMENTS_PS || hash == SPRITE_COLOUR_PS;
  const bool half_on = half_resolution_row != 0.f && sprite && command.arguments.draw_count <= 1u;
  const bool brightness_on = hash != SPRITE_MOMENTS_PS && BrightnessRowsOn();
  const bool layer_seen = smoothing_row != 0.f && hash == SPRITE_MOMENTS_PS;
  if (!half_on && !brightness_on && !layer_seen) {
    if (sprite && objects.device) objects.held[hash == SPRITE_COLOUR_PS ? 1 : 0].arguments.Reset();
    return {};
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());

  ComPtr<ID3D11PixelShader> patched;
  {
    const std::unique_lock lock(mutex);
    BindDevice(device.Get());
    // Smooth Half-Size Particles: this layer's depth and viewport (the composite takes each layer's by its size).
    if (layer_seen && smoothing.layer_count < smoothing.layers.size()) {
      LayerDraw& layer = smoothing.layers[smoothing.layer_count];
      native->PSGetShaderResources(MOMENTS_DEPTH_SLOT, 1, layer.depth.ReleaseAndGetAddressOf());
      UINT viewport_count = 1;
      native->RSGetViewports(&viewport_count, &layer.viewport);
      ComPtr<ID3D11Resource> depth_resource;
      if (layer.depth) layer.depth->GetResource(depth_resource.GetAddressOf());
      if (ComPtr<ID3D11Texture2D> depth_texture; depth_resource && SUCCEEDED(depth_resource.As(&depth_texture)) && viewport_count != 0) {
        D3D11_TEXTURE2D_DESC depth_desc = {};
        depth_texture->GetDesc(&depth_desc);
        layer.width = depth_desc.Width;
        ++smoothing.layer_count;
      }
    }
    if (brightness_on) {
      if (const auto entry = colour_shaders.find(hash); entry != colour_shaders.end()) {
        if (!entry->second.patched && !entry->second.refused) {
          const auto& shader = *std::ranges::find(COLOUR_SHADERS, hash, &ColourShader::hash);
          const std::vector<uint8_t> code = PatchColourBrightness(entry->second.code, shader);
          entry->second.refused = code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, entry->second.patched.GetAddressOf()));
          renodx::utils::log::i(std::format("[Total War] VFX: shader {:08X} {}", hash, entry->second.refused ? "not as expected, stays the game's." : "patched."));
        }
        patched = entry->second.patched;
      }
    }
  }
  // Only seen for Smooth Half-Size Particles (a moments draw): as with the rows off.
  if (!half_on && !brightness_on) {
    objects.held[0].arguments.Reset();
    return {};
  }
  // Before the hold: the sharp sprites replayed there take the sliders too.
  typename renodx::utils::command_action::CallbackResult<Context>::PostCallback put_game_shader_back = nullptr;
  if (patched && objects.brightness) {
    const std::array<float, 4> rows = {fire_and_glow, smoke_and_dust, effect_meshes, particle_fog};
    if (objects.written != rows) {
      objects.written = rows;
      native->UpdateSubresource(objects.brightness.Get(), 0, nullptr, rows.data(), 0, 0);
    }
    native->PSGetShader(objects.game_shader.GetAddressOf(), nullptr, nullptr);
    native->PSGetConstantBuffers(BRIGHTNESS_SLOT, 1, objects.game_brightness.GetAddressOf());
    native->PSSetShader(patched.Get(), nullptr, 0);
    native->PSSetConstantBuffers(BRIGHTNESS_SLOT, 1, objects.brightness.GetAddressOf());
    put_game_shader_back = [](Context& post, const void*) {
      auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
      post_native->PSSetShader(objects.game_shader.Get(), nullptr, 0);
      post_native->PSSetConstantBuffers(BRIGHTNESS_SLOT, 1, objects.game_brightness.GetAddressOf());
      objects.game_shader.Reset();
      objects.game_brightness.Reset();
    };
  }
  const bool held = half_on && HoldOrReplay(command.arguments, native, &objects.held[hash == SPRITE_COLOUR_PS ? 1 : 0]);
  return {.bypass = held, .finalize_callback = put_game_shader_back};
  }
};

// Smooth Half-Size Particles: the half layer's zeroth moment, as A759AD5D adds it to the sharp layer's (its t0).
constexpr auto OnMomentsCombine = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (smoothing_row == 0.f) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  const std::unique_lock lock(mutex);
  native->PSGetShaderResources(0, 1, smoothing.half_optical_depth.ReleaseAndGetAddressOf());
  return {};
};

// Smooth Half-Size Particles: at the particles' composite (43606BB8: t0 the zeroth moment, t1 the sharp layer's colour, t2 the
// half one's, the camera at b0), the half layer smoothed (particle_history.hpp), then the game's draw with
// particle_composite.ps_5_0.hlsl, which brings it up by depth (all put back after). Before taa_dlss.hpp's own callback here
// (Ray Reconstruction's particle layer draws the same shader again). Without this frame's layers: the game's composite.
constexpr auto OnComposite = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (smoothing_row == 0.f) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  const std::unique_lock lock(mutex);
  if (smoothing.layer_count == 0 || !smoothing.half_optical_depth) return {};
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> game_layers;  // the zeroth moment, the sharp and the half colour
  for (UINT slot = 0; slot < game_layers.size(); ++slot) {
    native->PSGetShaderResources(slot, 1, game_layers[slot].GetAddressOf());
    if (!game_layers[slot]) return {};
  }
  const auto width_of = [](ID3D11ShaderResourceView* view) {
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc = {};
    if (SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
    return desc.Width;
  };
  const UINT full_width = width_of(game_layers[1].Get());
  const UINT half_width = width_of(game_layers[2].Get());
  if (half_width == 0 || half_width >= full_width || width_of(smoothing.half_optical_depth.Get()) != half_width) return {};
  const auto layer_of_width = [](UINT width) -> const LayerDraw* {
    const auto end = smoothing.layers.begin() + static_cast<ptrdiff_t>(smoothing.layer_count);
    const auto found = std::ranges::find(smoothing.layers.begin(), end, width, &LayerDraw::width);
    return (found != end) ? &*found : nullptr;
  };
  const LayerDraw* half_layer = layer_of_width(half_width);
  const LayerDraw* full_layer = layer_of_width(full_width);
  if (half_layer == nullptr || full_layer == nullptr) return {};
  // The composite's depth target (taa_dlss.hpp reads and rebinds it): writable and a depth the upsample reads, that read would be
  // unbound. Then the game's composite.
  ComPtr<ID3D11DepthStencilView> depth_target;
  native->OMGetRenderTargets(0, nullptr, depth_target.GetAddressOf());
  if (depth_target) {
    D3D11_DEPTH_STENCIL_VIEW_DESC depth_target_desc = {};
    depth_target->GetDesc(&depth_target_desc);
    ComPtr<ID3D11Resource> target_resource;
    depth_target->GetResource(target_resource.GetAddressOf());
    const auto written = [&](ID3D11ShaderResourceView* view) {
      ComPtr<ID3D11Resource> resource;
      view->GetResource(resource.GetAddressOf());
      return resource == target_resource;
    };
    if ((depth_target_desc.Flags & D3D11_DSV_READ_ONLY_DEPTH) == 0 && (written(half_layer->depth.Get()) || written(full_layer->depth.Get()))) {
      if (!smoothing.depth_clash_logged) {
        smoothing.depth_clash_logged = true;
        renodx::utils::log::w("[Warhammer III] Smooth Half-Size Particles: the composite's depth target is a depth it reads: the game's "
                              "composite.");
      }
      return {};
    }
  }
  ComPtr<ID3D11Buffer> camera;
  native->PSGetConstantBuffers(0, 1, camera.GetAddressOf());
  if (!camera) return {};
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  BindDevice(device.Get());
  if (!objects.composite && !objects.composite_failed) {
    objects.composite_failed = FAILED(device->CreatePixelShader(__particle_composite.data(), __particle_composite.size(), nullptr,
                                                                objects.composite.GetAddressOf()));
    if (objects.composite_failed) renodx::utils::log::w("[Warhammer III] Smooth Half-Size Particles: could not create its composite: off.");
  }
  if (!objects.composite) return {};
  const auto smoothed = particle_history::Resolve(native,
                                                  {.colour = game_layers[2].Get(),
                                                   .optical_depth = smoothing.half_optical_depth.Get(),
                                                   .depth = half_layer->depth.Get(),
                                                   .camera = camera.Get(),
                                                   .viewport = half_layer->viewport},
                                                  smoothing.frame, smoothing.restart, &objects.history);
  smoothing.restart = false;
  if (smoothed[0] == nullptr) return {};

  native->PSGetShader(smoothing.game_shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
  native->PSGetShaderResources(particle_history::VIEWS_SLOT, particle_history::VIEW_COUNT, smoothing.game_views.data());
  native->PSGetConstantBuffers(particle_history::CONSTANTS_SLOT, particle_history::CONSTANTS_COUNT, smoothing.game_constants.data());
  const std::array<ID3D11ShaderResourceView*, particle_history::VIEW_COUNT> views = {
      smoothed[0], smoothed[1], half_layer->depth.Get(), full_layer->depth.Get(), smoothing.half_optical_depth.Get(), nullptr};
  const std::array<ID3D11Buffer*, particle_history::CONSTANTS_COUNT> constants = {objects.history.constants.Get(), nullptr, nullptr, camera.Get()};
  native->PSSetShader(objects.composite.Get(), nullptr, 0);
  native->PSSetShaderResources(particle_history::VIEWS_SLOT, particle_history::VIEW_COUNT, views.data());
  native->PSSetConstantBuffers(particle_history::CONSTANTS_SLOT, particle_history::CONSTANTS_COUNT, constants.data());
  if (!smoothing.applied_logged) {
    smoothing.applied_logged = true;
    renodx::utils::log::i(std::format("[Warhammer III] Smooth Half-Size Particles: the first composite smoothed ({} x {} half layer).",
                                      half_layer->viewport.Width, half_layer->viewport.Height));
  }
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&post_native);
    const std::unique_lock post_lock(mutex);
    post_native->PSSetShader(smoothing.game_shader.Get(), nullptr, 0);
    post_native->PSSetShaderResources(particle_history::VIEWS_SLOT, particle_history::VIEW_COUNT, smoothing.game_views.data());
    post_native->PSSetConstantBuffers(particle_history::CONSTANTS_SLOT, particle_history::CONSTANTS_COUNT, smoothing.game_constants.data());
    smoothing.game_shader.Reset();
    for (auto*& view : smoothing.game_views) {
      if (view != nullptr) view->Release();
      view = nullptr;
    }
    for (auto*& buffer : smoothing.game_constants) {
      if (buffer != nullptr) buffer->Release();
      buffer = nullptr;
    }
  }};
};

}  // namespace internal

inline void Install() {
  renodx::utils::pipeline::RegisterOnInitCallback(internal::OnInitPipeline);
  const auto types = renodx::utils::command_action::COMMAND_TYPE_INDIRECT;
  renodx::utils::command_action::Register(internal::OnParticleDraw, {.shader_hash = internal::SPRITE_MOMENTS_PS, .command_types = types});
  for (const auto& shader : internal::COLOUR_SHADERS) {
    renodx::utils::command_action::Register(internal::OnParticleDraw, {.shader_hash = shader.hash, .command_types = types});
  }
  const auto draws = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW;
  renodx::utils::command_action::Register(internal::OnMomentsCombine, {.shader_hash = internal::MOMENTS_COMBINE_PS, .command_types = draws});
  renodx::utils::command_action::Register(internal::OnComposite, {.shader_hash = internal::COMPOSITE_PS, .command_types = draws});
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnComposite);
  renodx::utils::command_action::Unregister(internal::OnMomentsCombine);
  renodx::utils::command_action::Unregister(internal::OnParticleDraw);
  renodx::utils::pipeline::UnregisterOnInitCallback(internal::OnInitPipeline);
}

// Every present: the frame counted and this frame's layers let go; Smooth Half-Size Particles' memory back while it is off,
// its history started again when Particles at Half Resolution changes what the half layer holds.
inline void OnPresent() {
  const std::unique_lock lock(internal::mutex);
  auto& smoothing = internal::smoothing;
  ++smoothing.frame;
  smoothing.layers = {};
  smoothing.layer_count = 0;
  smoothing.half_optical_depth.Reset();
  if (smoothing_row == 0.f) {
    if (internal::objects.history.width != 0 || internal::objects.history.failed_width != 0) {
      particle_history::ReleaseHistories(&internal::objects.history);
    }
    smoothing.restart = true;
  }
  if (smoothing.half_resolution_seen != half_resolution_row) {
    smoothing.half_resolution_seen = half_resolution_row;
    smoothing.restart = true;
  }
}

inline void OnDestroyDevice(reshade::api::device*) {
  const std::unique_lock lock(internal::mutex);
  internal::objects = {};
  internal::smoothing.layers = {};
  internal::smoothing.layer_count = 0;
  internal::smoothing.half_optical_depth.Reset();
  internal::smoothing.restart = true;
  for (auto& [hash, entry] : internal::colour_shaders) entry = {.code = std::move(entry.code)};
}

}  // namespace twwarhammer3::vfx_particles
