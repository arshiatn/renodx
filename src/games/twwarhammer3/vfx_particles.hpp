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
#include "../../utils/log.hpp"
#include "../../utils/pipeline.hpp"
#include "../twattila/dxbc.hpp"

// The game's particles (fx_vfx_render, MBOIT): moments into 3 targets, then colour into one, each pass twice: all that
// the game keeps sharp at the render's size, then what it marks as soft into a half-size layer with its own depth. Traced in 93
// frames, always in that order at every scale. Colour sums linearly in 43606BB8, so a factor on it is that much light, nothing else.
namespace twwarhammer3::vfx_particles {

using Microsoft::WRL::ComPtr;
namespace dxbc = twattila::dxbc;

static float fire_and_glow = 1.f;
static float smoke_and_dust = 1.f;
static float effect_meshes = 1.f;
static float particle_fog = 1.f;
static float half_resolution_row = 0.f;

namespace internal {

constexpr uint32_t SPRITE_MOMENTS_PS = 0x713B947E;
constexpr uint32_t SPRITE_COLOUR_PS = 0x3373F064;
constexpr UINT BRIGHTNESS_SLOT = 11;
// The sprite VS's cb_vfx: g_draw_params.x is the layer's bin, so a layer's draw needs its own.
constexpr UINT DRAW_PARAMS_SLOT = 7;

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
};

static std::shared_mutex mutex;
static std::unordered_map<uint32_t, PatchedShader> colour_shaders;  // By the game's hash.
static DeviceObjects objects;

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

  if (held->arguments && target_desc.Width * 2 == held->target_width) {
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
  if (!half_on && !brightness_on) {
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
  if (half_on && HoldOrReplay(command.arguments, native, &objects.held[hash == SPRITE_COLOUR_PS ? 1 : 0])) {
    return {.bypass = true};
  }
  if (!patched || !objects.brightness) return {};

  const std::array<float, 4> rows = {fire_and_glow, smoke_and_dust, effect_meshes, particle_fog};
  if (objects.written != rows) {
    objects.written = rows;
    native->UpdateSubresource(objects.brightness.Get(), 0, nullptr, rows.data(), 0, 0);
  }
  native->PSGetShader(objects.game_shader.GetAddressOf(), nullptr, nullptr);
  native->PSGetConstantBuffers(BRIGHTNESS_SLOT, 1, objects.game_brightness.GetAddressOf());
  native->PSSetShader(patched.Get(), nullptr, 0);
  native->PSSetConstantBuffers(BRIGHTNESS_SLOT, 1, objects.brightness.GetAddressOf());
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    post_native->PSSetShader(objects.game_shader.Get(), nullptr, 0);
    post_native->PSSetConstantBuffers(BRIGHTNESS_SLOT, 1, objects.game_brightness.GetAddressOf());
    objects.game_shader.Reset();
    objects.game_brightness.Reset();
  }};
  }
};

}  // namespace internal

inline void Install() {
  renodx::utils::pipeline::RegisterOnInitCallback(internal::OnInitPipeline);
  const auto types = renodx::utils::command_action::COMMAND_TYPE_INDIRECT;
  renodx::utils::command_action::Register(internal::OnParticleDraw, {.shader_hash = internal::SPRITE_MOMENTS_PS, .command_types = types});
  for (const auto& shader : internal::COLOUR_SHADERS) {
    renodx::utils::command_action::Register(internal::OnParticleDraw, {.shader_hash = shader.hash, .command_types = types});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnParticleDraw);
  renodx::utils::pipeline::UnregisterOnInitCallback(internal::OnInitPipeline);
}

inline void OnDestroyDevice(reshade::api::device*) {
  const std::unique_lock lock(internal::mutex);
  internal::objects = {};
  for (auto& [hash, entry] : internal::colour_shaders) entry = {.code = std::move(entry.code)};
}

}  // namespace twwarhammer3::vfx_particles
