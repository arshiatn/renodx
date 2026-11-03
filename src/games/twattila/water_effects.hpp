/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Water Clarity and Water Caustics at the game's water draws (Attila; WH3). The addon gives its water_shaders, registers OnWaterDraw for each of them
// and OnCreateWater as a pipeline callback, has the embeds __water_clarity and __water_caustics, and the rows bind water_clarity and water_caustics.
#ifndef SRC_GAMES_TWATTILA_WATER_EFFECTS_HPP_
#define SRC_GAMES_TWATTILA_WATER_EFFECTS_HPP_

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <mutex>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/log.hpp"
#include "./dxbc.hpp"
#include "./rt_scene.hpp"
#include "./view_share.hpp"

namespace twattila::water_effects {

using Microsoft::WRL::ComPtr;

// Water Clarity (ATN: rivers more see-through): the water shaders (water_shaders) blend toward the deep water colour by
// g_sea_bed_light_scatter (lighting_VS_PS c10) to the power of the water they look through. At their draws a copy of those
// constants with it raised to 1 / clarity is bound (water_clarity.cs), the game's put back after the draw (later draws, and
// the game's updates of its own buffer, stay its). Up to 16: murky rivers (ATN's debug28: no change seen at 4) keep little
// light a metre, 4 times as far still dark. Logged once where it applies.
struct WaterShader {
  uint32_t hash = 0;
  UINT lighting_slot = 0;        // b: lighting_VS_PS
  uint32_t world_input = 0;      // v: the world position
  uint32_t refraction_slot = 0;  // t: the water refraction's output
  bool caustics = true;          // the campaign's world is a twentieth of the battles': a 4 m tile means nothing there
  uint32_t normal_slot = 0;      // t: the normal map, a plain sample of it gives the caustics' sampler
};
// The game's water shaders, read per game; the addon's table.
static std::span<const WaterShader> water_shaders;
static float water_clarity = 1.f;
// Water Caustics (ATN): the shaders' refracted bed colour x (1 + t51 at their world position / WATER_CAUSTICS_TILE), the
// tile water_caustics.cs makes at the first water draw of a frame; t51 unbound reads 0 (the game's water). Patched as
// they are created (PatchWaterCaustics).
constexpr UINT WATER_CAUSTICS_SLOT = 51;
constexpr float WATER_CAUSTICS_TILE = 4.f;  // metres
constexpr UINT WATER_CAUSTICS_SIZE = 256;
static float water_caustics = 1.f;
struct CausticsObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> shader;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
  uint64_t made_tick = 0;  // its last time (one a frame: within 4 ms of it, the same frame's)
};
static CausticsObjects caustics_objects;
// The patched water shaders by the game's hash (kept: the game may create them again).
static std::unordered_map<uint32_t, std::vector<uint8_t>> water_patched;
static std::mutex water_mutex;
struct WaterObjects {
  ComPtr<ID3D11Device> device;
  UINT bytes = 0;  // the game's lighting constants' (0: none made)
  ComPtr<ID3D11ComputeShader> shader;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11Buffer> copied;  // raw: the game's constants
  ComPtr<ID3D11ShaderResourceView> copied_srv;
  ComPtr<ID3D11Buffer> clarified;  // raw: with the scatter raised
  ComPtr<ID3D11UnorderedAccessView> clarified_uav;
  ComPtr<ID3D11Buffer> lighting;       // the constant buffer bound for the draw
  ComPtr<ID3D11Buffer> game_lighting;  // the game's, put back after it
  UINT lighting_slot = 0;              // the draw's
  ID3D11Buffer* source = nullptr;      // the game's buffer `lighting` was made from (compared only)
  uint64_t made_tick = 0;              // its last time (within 4 ms: the same frame's)
  bool logged = false;
};
static WaterObjects water_objects;

// A water shader with the caustics (empty: not as expected): after the refraction's colour sample (a sample of 3+
// components into rD from its t: the last, the river's first takes w alone, the sea reads two) or the `if` picking between
// them, with rT one more temp and t51 declared:
//   mul rT.xy, vW.xzxx, l(1 / tile) / frc rT.xy, rT.xyxx / sample_l rT.x, rT.xyxx, t51, s(the normal map's), l(0) /
//   mad rD.xyz(w's left), rD, rT.xxxx, rD
inline std::vector<uint8_t> PatchWaterCaustics(std::span<const uint8_t> code, const WaterShader& water) {
  constexpr uint32_t OP_ENDIF = 21, OP_FRC = 26, OP_IF = 31, OP_MAD = 50, OP_MUL = 56, OP_SAMPLE = 69, OP_SAMPLE_L = 72, OP_DCL_RESOURCE = 88,
                     OP_DCL_TEMPS = 104;
  constexpr uint32_t EXTENDED = 1u << 31;
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  size_t temps_at = 0;
  size_t refraction_at = 0;  // the refraction's colour sample, its operands' start
  size_t refraction_operands = 0;
  size_t normal_at = 0;  // the normal map's (normal_slot) sample, its operands' start
  size_t normal_operands = 0;
  for (size_t at = 2; at < tokens.size();) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    if (opcode == OP_DCL_TEMPS) temps_at = at;
    if (opcode == OP_SAMPLE || opcode == OP_SAMPLE_L) {
      size_t operands = at + 1;
      if ((tokens[at] & EXTENDED) != 0) {
        while ((tokens[operands] & EXTENDED) != 0) ++operands;
        ++operands;
      }
      // dest, address, resource, sampler: two tokens each (one immediate index).
      const bool plain = operands + 8 <= at + length && (tokens[operands + 4] & 0x001FF000) == 0x00107000
                         && (tokens[operands + 6] & 0x001FF000) == 0x00106000;
      if (plain && opcode == OP_SAMPLE && tokens[operands + 5] == water.normal_slot && normal_at == 0) {
        normal_at = at;
        normal_operands = operands;
      }
      if (plain && tokens[operands + 5] == water.refraction_slot && (tokens[operands] & 0x001FF00F) == 0x00100002
          && std::popcount((tokens[operands] >> 4) & 0xFu) >= 3) {
        refraction_at = at;
        refraction_operands = operands;
      }
    }
    at += length;
  }
  if (temps_at == 0 || refraction_at == 0 || normal_at == 0) return {};
  const uint32_t temp = tokens[temps_at + 1];
  const uint32_t bed = tokens[refraction_operands + 1];
  const uint32_t scale = std::bit_cast<uint32_t>(1.f / WATER_CAUSTICS_TILE);
  // A whole xyzw read: its w stays.
  const uint32_t destination = ((tokens[refraction_operands] & 0xF0) == 0xF0 ? (tokens[refraction_operands] & ~0x80u) : tokens[refraction_operands]);
  std::vector<uint32_t> added = {
      // mul rT.xy, vW.xzxx, l(scale, scale, 0, 0) / frc rT.xy, rT.xyxx (the tile repeats whatever the sampler's address mode)
      OP_MUL | (10u << 24), 0x00100032, temp, 0x00101086, water.world_input, 0x00004002, scale, scale, 0u, 0u,
      OP_FRC | (5u << 24), 0x00100032, temp, 0x00100046, temp,
      // sample_l(_indexable(texture2d)(float,float,float,float): the normal map's extended tokens, none below shader model 5)
      // rT.x, rT.xyxx, t51.xyzw, sN, l(0)
      OP_SAMPLE_L | (tokens[normal_at] & EXTENDED) | (static_cast<uint32_t>(10 + normal_operands - normal_at) << 24),
  };
  added.insert(added.end(), tokens.begin() + static_cast<ptrdiff_t>(normal_at + 1), tokens.begin() + static_cast<ptrdiff_t>(normal_operands));
  added.insert(added.end(), {
      0x00100012, temp, 0x00100046, temp, 0x00107E46, WATER_CAUSTICS_SLOT, tokens[normal_operands + 6], tokens[normal_operands + 7], 0x00004001, 0u,
      // mad rD, rD.xyzw, rT.xxxx, rD.xyzw
      OP_MAD | (9u << 24), destination, bed, 0x00100E46, bed, 0x00100006, temp, 0x00100E46, bed,
  });
  size_t added_at = refraction_at + dxbc::Length(tokens, refraction_at);
  if ((tokens[added_at] & 0x7FF) == OP_IF) {
    while ((tokens[added_at] & 0x7FF) != OP_ENDIF) added_at += dxbc::Length(tokens, added_at);
    ++added_at;
  }
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(added_at), added.begin(), added.end());
  // dcl_resource_texture2d (float,float,float,float) t51, before dcl_temps (the declarations come first).
  const std::array<uint32_t, 4> declaration = {OP_DCL_RESOURCE | (3u << 11) | (4u << 24), 0x00107000, WATER_CAUSTICS_SLOT, 0x5555};
  ++tokens[temps_at + 1];
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), declaration.begin(), declaration.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return dxbc::Write(code, program);
}

// The game creates a pipeline: the water shaders with the caustics (renodx keeps their original hash; one the render
// scale patched first by its hash).
inline bool OnCreateWater(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                   const reshade::api::pipeline_subobject* subobjects) {
  if (device->get_api() != reshade::api::device_api::d3d11) return false;
  bool modified = false;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
    auto* desc = static_cast<reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const std::span<const uint8_t> code(static_cast<const uint8_t*>(desc->code), desc->code_size);
    const auto scaled = view_share::OriginalHash(desc->code);
    const uint32_t hash = scaled.value_or(renodx::utils::hash::ComputeCRC32(code.data(), code.size()));
    const auto water = std::ranges::find(water_shaders, hash, &WaterShader::hash);
    if (water == water_shaders.end() || !water->caustics) continue;
    const std::unique_lock lock(water_mutex);
    auto [entry, added] = water_patched.try_emplace(hash);
    if (added) {
      entry->second = PatchWaterCaustics(code, *water);
      renodx::utils::log::i(std::format("[Total War] Water Caustics: water shader {:08X} {}", hash,
                                        entry->second.empty() ? "not as expected, left as it is." : "patched."));
    }
    if (entry->second.empty()) continue;
    desc->code = entry->second.data();
    desc->code_size = entry->second.size();
    view_share::Repatched(code.data(), desc->code, hash);
    modified = true;
  }
  return modified;
}

static std::atomic_bool water_drawn = false;  // logged once: which water shader the game draws

constexpr auto OnWaterDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!water_drawn.exchange(true)) {
    renodx::utils::log::i(std::format("[Total War] Water: first draw of shader {:08X} (Clarity {}, Caustics {}).",
                                      command.matched_shader_hash, water_clarity, water_caustics));
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  // Water Caustics: the tile made at the frame's first water draw, bound at t51 for the draw.
  auto& caustics = caustics_objects;
  if (water_caustics > 0.f && caustics.device != device) {
    caustics = {.device = device};
    const D3D11_TEXTURE2D_DESC tile_desc = {.Width = WATER_CAUSTICS_SIZE, .Height = WATER_CAUSTICS_SIZE, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R16_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                            .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    ComPtr<ID3D11Texture2D> tile;
    const bool made = SUCCEEDED(device->CreateComputeShader(__water_caustics.data(), __water_caustics.size(), nullptr, caustics.shader.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&constants_desc, nullptr, caustics.constants.GetAddressOf()))
                      && SUCCEEDED(device->CreateTexture2D(&tile_desc, nullptr, tile.GetAddressOf()))
                      && SUCCEEDED(device->CreateShaderResourceView(tile.Get(), nullptr, caustics.srv.GetAddressOf()))
                      && SUCCEEDED(device->CreateUnorderedAccessView(tile.Get(), nullptr, caustics.uav.GetAddressOf()));
    if (!made) caustics.srv.Reset();
  }
  const bool caustics_on = water_caustics > 0.f && caustics.srv;
  const uint64_t now = GetTickCount64();
  if (caustics_on) {
    if (now > caustics.made_tick + 4) {
      caustics.made_tick = now;
      const rt_scene::internal::ComputeState kept(native);
      const std::array<float, 4> constants = {static_cast<float>(now % 3600000) / 1000.f, water_caustics, 0.f, 0.f};
      native->UpdateSubresource(caustics.constants.Get(), 0, nullptr, constants.data(), 0, 0);
      native->CSSetShader(caustics.shader.Get(), nullptr, 0);
      native->CSSetUnorderedAccessViews(0, 1, caustics.uav.GetAddressOf(), nullptr);
      native->CSSetConstantBuffers(0, 1, caustics.constants.GetAddressOf());
      native->Dispatch(WATER_CAUSTICS_SIZE / 8, WATER_CAUSTICS_SIZE / 8, 1);
    }
    native->PSSetShaderResources(WATER_CAUSTICS_SLOT, 1, caustics.srv.GetAddressOf());
  }
  // Water Clarity.
  const UINT lighting_slot = std::ranges::find(water_shaders, command.matched_shader_hash, &WaterShader::hash)->lighting_slot;
  ComPtr<ID3D11Buffer> game_lighting;
  if (std::abs(water_clarity - 1.f) >= 0.01f) native->PSGetConstantBuffers(lighting_slot, 1, game_lighting.GetAddressOf());
  D3D11_BUFFER_DESC game_desc = {};
  if (game_lighting) game_lighting->GetDesc(&game_desc);
  auto& objects = water_objects;
  if (game_lighting && (objects.device != device || objects.bytes != game_desc.ByteWidth)) {
    objects = {.device = device};
    const D3D11_BUFFER_DESC raw_in = {.ByteWidth = game_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_BUFFER_DESC raw_out = {.ByteWidth = game_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                       .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_BUFFER_DESC lighting_desc = {.ByteWidth = game_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX};
    srv_desc.BufferEx = {.FirstElement = 0, .NumElements = game_desc.ByteWidth / 4, .Flags = D3D11_BUFFEREX_SRV_FLAG_RAW};
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
    uav_desc.Buffer = {.FirstElement = 0, .NumElements = game_desc.ByteWidth / 4, .Flags = D3D11_BUFFER_UAV_FLAG_RAW};
    const bool made = SUCCEEDED(device->CreateComputeShader(__water_clarity.data(), __water_clarity.size(), nullptr, objects.shader.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&constants_desc, nullptr, objects.constants.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&raw_in, nullptr, objects.copied.GetAddressOf()))
                      && SUCCEEDED(device->CreateShaderResourceView(objects.copied.Get(), &srv_desc, objects.copied_srv.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&raw_out, nullptr, objects.clarified.GetAddressOf()))
                      && SUCCEEDED(device->CreateUnorderedAccessView(objects.clarified.Get(), &uav_desc, objects.clarified_uav.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&lighting_desc, nullptr, objects.lighting.GetAddressOf()));
    if (made) objects.bytes = game_desc.ByteWidth;
  }
  if (!game_lighting || objects.bytes != game_desc.ByteWidth) {
    if (!caustics_on) return {};
    return {.finalize_callback = [](Context& post, const void*) {
      ID3D11ShaderResourceView* no_view = nullptr;
      reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native())->PSSetShaderResources(WATER_CAUSTICS_SLOT, 1, &no_view);
    }};
  }
  // Once a frame (its water draws share the game's lighting buffer; the river is dozens of draws).
  if (now > objects.made_tick + 4 || objects.source != game_lighting.Get()) {
    objects.made_tick = now;
    objects.source = game_lighting.Get();
    const rt_scene::internal::ComputeState kept(native);
    native->CopyResource(objects.copied.Get(), game_lighting.Get());
    const std::array<uint32_t, 4> constants = {objects.bytes / 4, std::bit_cast<uint32_t>(1.f / water_clarity), 0u, 0u};
    native->UpdateSubresource(objects.constants.Get(), 0, nullptr, constants.data(), 0, 0);
    native->CSSetShader(objects.shader.Get(), nullptr, 0);
    native->CSSetShaderResources(0, 1, objects.copied_srv.GetAddressOf());
    native->CSSetUnorderedAccessViews(0, 1, objects.clarified_uav.GetAddressOf(), nullptr);
    native->CSSetConstantBuffers(0, 1, objects.constants.GetAddressOf());
    native->Dispatch(((objects.bytes / 4) + 63) / 64, 1, 1);
    ID3D11UnorderedAccessView* no_uav = nullptr;
    native->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
    native->CopyResource(objects.lighting.Get(), objects.clarified.Get());
  }
  objects.game_lighting = std::move(game_lighting);
  objects.lighting_slot = lighting_slot;
  native->PSSetConstantBuffers(lighting_slot, 1, objects.lighting.GetAddressOf());
  if (!std::exchange(objects.logged, true)) {
    renodx::utils::log::i("[Total War] Water Clarity: applied at a water draw (", objects.bytes, " bytes of lighting constants).");
  }
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    post_native->PSSetConstantBuffers(water_objects.lighting_slot, 1, water_objects.game_lighting.GetAddressOf());
    water_objects.game_lighting.Reset();
    ID3D11ShaderResourceView* no_view = nullptr;
    post_native->PSSetShaderResources(WATER_CAUSTICS_SLOT, 1, &no_view);
  }};
};

}  // namespace twattila::water_effects

#endif  // SRC_GAMES_TWATTILA_WATER_EFFECTS_HPP_
