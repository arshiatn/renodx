/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Water Clarity and Water Caustics at the game's water draws (Attila; WH3; Pharaoh; Warhammer II). The addon gives its water_shaders, registers OnWaterDraw for each of them
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
#include "../../utils/pipeline.hpp"
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
// The opacity a material water's absorption multiplies by (PatchWaterAbsorption): an operand, type 8 a constant buffer (index:
// slot, register) or 1 an input (index: register, 0), and its component. Type 0: every mul by +-log2(e) (WH3's battle water).
// Type 4 an immediate (index[0]: its bits, in the immediate's `component`): the mul or mad reading that literal scales the
// depth (Warhammer II's linear fades); `divisor`: what it makes divides the depth instead, so it is divided by 1 / clarity.
// `component_only`: only that component of the destination is scaled (Thrones' sea: one lane of a vector literal's mul is
// the depth's, the others other terms).
struct WaterOpacity {
  uint32_t type = 0;
  std::array<uint32_t, 2> index = {};
  uint32_t component = 0;
  bool divisor = false;
  bool component_only = false;
};
struct WaterShader {
  uint32_t hash = 0;
  UINT lighting_slot = 0;        // b: lighting_VS_PS
  uint32_t world_input = 0;      // v: the world position
  uint32_t refraction_slot = 0;  // t: the water refraction's output
  bool caustics = true;          // the campaign's world is a twentieth of the battles': a 4 m tile means nothing there
  uint32_t normal_slot = 0;      // t: the normal map, a plain sample of it gives the caustics' sampler
  bool material_water = false;   // Colour per material, no scatter to raise: swapped at the draw for a patched copy (PatchWaterAbsorption)
  WaterOpacity opacity = {};     // material_water's
  bool every_refraction = false;  // the caustics after every refraction colour sample (Warhammer II's battle water reads it 4 times), not the last alone
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
// material_water: WH3's battle_water and battle_river take their colour from a material buffer, so Clarity scales their exponents
// instead (b11, x: 1 / clarity). While a row is on, or the view is below 100%, their draws run a patched copy; else the game's own shader.
constexpr UINT WATER_CLARITY_SLOT = 11;
struct MaterialWater {
  std::vector<uint8_t> code;
  ComPtr<ID3D11PixelShader> patched;
  bool refused = false;
};
static std::unordered_map<uint32_t, MaterialWater> material_waters;  // By the game's hash, under water_mutex.
struct MaterialObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Buffer> constants;
  float written = 0.f;  // The clarity row `constants` has.
  ComPtr<ID3D11PixelShader> game_shader;
  ComPtr<ID3D11Buffer> game_constants;
};
static MaterialObjects material_objects;
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
// every_refraction: after each such sample instead (one of an if / else pair: after the else's).
inline std::vector<uint8_t> PatchWaterCaustics(std::span<const uint8_t> code, const WaterShader& water) {
  constexpr uint32_t OP_ELSE = 18, OP_ENDIF = 21, OP_FRC = 26, OP_IF = 31, OP_MAD = 50, OP_MUL = 56, OP_SAMPLE = 69, OP_SAMPLE_L = 72,
                     OP_DCL_RESOURCE = 88, OP_DCL_TEMPS = 104;
  constexpr uint32_t EXTENDED = 1u << 31;
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  size_t temps_at = 0;
  size_t refraction_at = 0;  // the refraction's colour sample, its operands' start
  size_t refraction_operands = 0;
  size_t normal_at = 0;  // the normal map's (normal_slot) sample, its operands' start
  size_t normal_operands = 0;
  std::vector<std::array<size_t, 2>> sites;  // every_refraction's: each sample's at and operands
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
        if (water.every_refraction && at + length < tokens.size() && (tokens[at + length] & 0x7FF) != OP_ELSE) sites.push_back({at, operands});
      }
    }
    at += length;
  }
  if (temps_at == 0 || refraction_at == 0 || normal_at == 0) return {};
  if (!water.every_refraction) sites = {{refraction_at, refraction_operands}};
  const uint32_t temp = tokens[temps_at + 1];
  const uint32_t scale = std::bit_cast<uint32_t>(1.f / WATER_CAUSTICS_TILE);
  // The normal map's sample's extended tokens and sampler, kept before anything moves.
  const std::vector<uint32_t> normal_extended(tokens.begin() + static_cast<ptrdiff_t>(normal_at + 1), tokens.begin() + static_cast<ptrdiff_t>(normal_operands));
  const uint32_t normal_opcode = tokens[normal_at];
  const std::array<uint32_t, 2> normal_sampler = {tokens[normal_operands + 6], tokens[normal_operands + 7]};
  // The last site first: the earlier ones keep their places.
  for (auto site = sites.rbegin(); site != sites.rend(); ++site) {
    const auto [sample_at, sample_operands] = *site;
    const uint32_t bed = tokens[sample_operands + 1];
    // A whole xyzw read: its w stays.
    const uint32_t destination = ((tokens[sample_operands] & 0xF0) == 0xF0 ? (tokens[sample_operands] & ~0x80u) : tokens[sample_operands]);
    std::vector<uint32_t> added = {
        // mul rT.xy, vW.xzxx, l(scale, scale, 0, 0) / frc rT.xy, rT.xyxx (the tile repeats whatever the sampler's address mode)
        OP_MUL | (10u << 24), 0x00100032, temp, 0x00101086, water.world_input, 0x00004002, scale, scale, 0u, 0u,
        OP_FRC | (5u << 24), 0x00100032, temp, 0x00100046, temp,
        // sample_l(_indexable(texture2d)(float,float,float,float): the normal map's extended tokens, none below shader model 5)
        // rT.x, rT.xyxx, t51.xyzw, sN, l(0)
        OP_SAMPLE_L | (normal_opcode & EXTENDED) | (static_cast<uint32_t>(10 + normal_extended.size() + 1) << 24),
    };
    added.insert(added.end(), normal_extended.begin(), normal_extended.end());
    added.insert(added.end(), {
        0x00100012, temp, 0x00100046, temp, 0x00107E46, WATER_CAUSTICS_SLOT, normal_sampler[0], normal_sampler[1], 0x00004001, 0u,
        // mad rD, rD.xyzw, rT.xxxx, rD.xyzw
        OP_MAD | (9u << 24), destination, bed, 0x00100E46, bed, 0x00100006, temp, 0x00100E46, bed,
    });
    size_t added_at = sample_at + dxbc::Length(tokens, sample_at);
    if ((tokens[added_at] & 0x7FF) == OP_IF) {
      while ((tokens[added_at] & 0x7FF) != OP_ENDIF) added_at += dxbc::Length(tokens, added_at);
      ++added_at;
    } else if (water.material_water && (tokens[added_at] & 0x7FF) == OP_ENDIF) {
      // The near water's five samples or the far water's one: the same register either way.
      ++added_at;
    }
    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(added_at), added.begin(), added.end());
  }
  // dcl_resource_texture2d (float,float,float,float) t51, before dcl_temps (the declarations come first).
  const std::array<uint32_t, 4> declaration = {OP_DCL_RESOURCE | (3u << 11) | (4u << 24), 0x00107000, WATER_CAUSTICS_SLOT, 0x5555};
  ++tokens[temps_at + 1];
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), declaration.begin(), declaration.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return dxbc::Write(code, program);
}

// Beer-Lambert in battle_water and battle_river: every exp(x * log2(e)) of absorption x depth, per channel and for the material's
// opacity. After each `mul rX, rY, l(+-1.442695)`: mul rX, rX, cb11[0].xxxx, which is what looking through 1 / clarity of the depth does.
// An `opacity` operand (Pharaoh's water: exp2(-depth x opacity), no log2(e)): after each mul that reads it. An immediate (Warhammer
// II's linear fades): after each mul or mad that reads it, a div by cb11[0].xxxx where it makes the depth's divisor; with
// `component_only` (Thrones' sea: exp2(depth x -0.4 log2(e)) in one lane) the mul writes that lane alone.
inline std::vector<uint8_t> PatchWaterAbsorption(std::span<const uint8_t> code, const WaterOpacity& opacity) {
  constexpr uint32_t OP_MAD = 50, OP_MUL = 56, OP_DCL_CONSTANT_BUFFER = 89, OP_DCL_TEMPS = 104;
  constexpr uint32_t LOG2_E = 0x3FB8AA3B;
  constexpr uint32_t SIGN = 0x80000000;
  constexpr uint32_t CLARITY_OPERAND = 0x00208006;  // cb[][].xxxx
  const bool immediate = opacity.type == dxbc::OPERAND_IMMEDIATE;
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  std::vector<uint32_t> patched(tokens.begin(), tokens.begin() + 2);
  size_t temps_at = 0;
  int found = 0;
  for (size_t at = 2; at < tokens.size();) {
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_DCL_TEMPS) temps_at = patched.size();
    if (opcode == OP_DCL_CONSTANT_BUFFER && tokens[at + 2] == WATER_CLARITY_SLOT) return {};
    patched.insert(patched.end(), tokens.begin() + static_cast<ptrdiff_t>(at), tokens.begin() + static_cast<ptrdiff_t>(at + length));
    if (opcode == OP_MUL || (immediate && opcode == OP_MAD)) {
      const size_t destination = dxbc::internal::FirstOperand(tokens, at);
      bool absorption = false;
      if (opacity.type == 0) {
        absorption = std::any_of(tokens.begin() + static_cast<ptrdiff_t>(at + 1), tokens.begin() + static_cast<ptrdiff_t>(at + length),
                                 [](uint32_t token) { return (token & ~SIGN) == LOG2_E; });
      } else {
        dxbc::Operand operand;
        for (size_t next = destination + dxbc::ParseOperand(tokens, destination, at + length, &operand); next < at + length;) {
          const size_t operand_length = dxbc::ParseOperand(tokens, next, at + length, &operand);
          if (operand_length == 0) return {};
          absorption = absorption
                       || (immediate ? (operand.type == opacity.type && operand.values[opacity.component & 3] == opacity.index[0])
                                     : (operand.type == opacity.type && operand.mode == 2 && operand.select == opacity.component && !operand.relative
                                        && operand.index == opacity.index));
          next += operand_length;
        }
      }
      if (absorption) {
        if ((tokens[at] & (1u << 13)) != 0 || (tokens[destination] & 0x001FF003) != 0x00100002) return {};
        const uint32_t lane = 0x10u << (opacity.component & 3);
        if (opacity.component_only && (tokens[destination] & lane) == 0) return {};
        const uint32_t written = (opacity.component_only ? ((tokens[destination] & ~0xF0u) | lane) : tokens[destination]);
        patched.insert(patched.end(), {(opacity.divisor ? dxbc::OP_DIV : OP_MUL) | (8u << 24), written, tokens[destination + 1], 0x00100E46,
                                       tokens[destination + 1], CLARITY_OPERAND, WATER_CLARITY_SLOT, 0u});
        ++found;
      }
    }
    at += length;
  }
  if (temps_at == 0 || found == 0) return {};
  const std::array<uint32_t, 4> declaration = {OP_DCL_CONSTANT_BUFFER | (4u << 24), 0x00208E46, WATER_CLARITY_SLOT, 1u};
  patched.insert(patched.begin() + static_cast<ptrdiff_t>(temps_at), declaration.begin(), declaration.end());
  patched[1] = static_cast<uint32_t>(patched.size());
  tokens = std::move(patched);
  return dxbc::Write(code, program);
}

// battle_water and battle_river (WH3): the refraction's depth test rebuilds a position from the projected pixel rP as
// NDC = rP x g_screen_size.zw x (2, -2) + (-1, 1), the texture's share, not the view's. Below 100% (the view in the target's
// top-left) the position lands off and the test picks the wrong refraction along the shore. Each such mad (its source last made,
// without a branch between, by `mul rY, rP, cb0[37].zw` from a temporary) takes (rP - g_viewport_origin) x g_viewport_dimensions.zw
// instead: two instructions before it, the same values at 100%. camera_VS_PS at b0 by the shaders' RDEF: g_screen_size c37,
// g_viewport_dimensions c39, g_viewport_origin c40. `sites`: how many were patched (empty code: none).
inline std::vector<uint8_t> PatchWaterScaleLine(std::span<const uint8_t> code, int* sites) {
  constexpr uint32_t OP_MAD = 50, OP_MUL = 56, OP_DCL_CONSTANT_BUFFER = 89;
  constexpr uint32_t SCREEN_SIZE = 37, VIEWPORT_DIMENSIONS = 39, VIEWPORT_ORIGIN = 40;
  constexpr uint32_t TWO = 0x40000000, MINUS_TWO = 0xC0000000, ONE = 0x3F800000, MINUS_ONE = 0xBF800000;
  // Branches, loops, calls: a writer before one of them isn't provably the last.
  constexpr std::array<uint32_t, 18> FLOW = {2, 3, 4, 5, 6, 7, 8, 10, 18, 21, 22, 23, 31, 44, 48, 62, 63, 76};
  *sites = 0;
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  std::vector<size_t> starts;
  bool camera_declared = false;
  for (size_t at = 2; at < tokens.size();) {
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    if ((tokens[at] & 0x7FF) == OP_DCL_CONSTANT_BUFFER && length == 4 && tokens[at + 2] == 0 && tokens[at + 3] > VIEWPORT_ORIGIN) camera_declared = true;
    starts.push_back(at);
    at += length;
  }
  if (!camera_declared) return {};
  const auto parse = [&](size_t instruction, std::array<dxbc::Operand, 4>* operands) {
    const size_t at = starts[instruction];
    const size_t end = at + dxbc::Length(tokens, at);
    size_t next = dxbc::internal::FirstOperand(tokens, at);
    size_t count = 0;
    while (next < end && count < operands->size()) {
      const size_t length = dxbc::ParseOperand(tokens, next, end, &(*operands)[count]);
      if (length == 0) return size_t{0};
      next += length;
      ++count;
    }
    return count;
  };
  // A temporary's components (mask) written by instruction `instruction`: every 4-component temp operand in mask mode.
  const auto writes = [&](size_t instruction, uint32_t reg, uint32_t mask) {
    std::array<dxbc::Operand, 4> operands = {};
    const size_t count = parse(instruction, &operands);
    for (size_t index = 0; index < count; ++index) {
      const auto& operand = operands[index];
      if (operand.type == 0 && operand.mode == 0 && (tokens[operand.at] & 3) == 2 && operand.index[0] == reg && (operand.select & mask) != 0) return true;
    }
    return false;
  };
  // (mad's index, mul's source temporary, its components for the mad's x and y)
  std::vector<std::array<size_t, 4>> found;
  for (size_t mad = 0; mad < starts.size(); ++mad) {
    const size_t at = starts[mad];
    if ((tokens[at] & 0x7FF) != OP_MAD || (tokens[at] & (1u << 13)) != 0) continue;
    std::array<dxbc::Operand, 4> operands = {};
    if (parse(mad, &operands) != 4) continue;
    const auto& [destination, source, scale, offset] = operands;
    if (destination.type != 0 || destination.mode != 0 || destination.select != 0x3 || destination.length != 2 || source.type != 0 || source.mode != 1
        || source.modified || source.length != 2 || scale.type != dxbc::OPERAND_IMMEDIATE || offset.type != dxbc::OPERAND_IMMEDIATE
        || scale.values[0] != TWO || scale.values[1] != MINUS_TWO || offset.values[0] != MINUS_ONE || offset.values[1] != ONE) {
      continue;
    }
    const uint32_t first = source.select & 3;
    const uint32_t second = (source.select >> 2) & 3;
    const uint32_t read_mask = (1u << first) | (1u << second);
    for (size_t before = mad; before-- > 0;) {
      const uint32_t opcode = tokens[starts[before]] & 0x7FF;
      if (std::ranges::find(FLOW, opcode) != FLOW.end()) break;
      if (!writes(before, source.index[0], read_mask)) continue;
      std::array<dxbc::Operand, 4> mul = {};
      if (opcode != OP_MUL || (tokens[starts[before]] & (1u << 13)) != 0 || parse(before, &mul) != 3) break;
      const auto& made = mul[0];
      const auto& position = mul[1];
      const auto& screen = mul[2];
      const auto component = [](const dxbc::Operand& operand, uint32_t index) { return (operand.select >> (2 * index)) & 3; };
      if (made.index[0] != source.index[0] || (made.select & read_mask) != read_mask || position.type != 0 || position.mode != 1
          || position.modified || screen.type != 8 || screen.mode != 1 || screen.modified || screen.relative || screen.index[0] != 0
          || screen.index[1] != SCREEN_SIZE || component(screen, first) != 2 || component(screen, second) != 3) {
        break;
      }
      const uint32_t x = component(position, first);
      const uint32_t y = component(position, second);
      // rP's x and y unchanged between the mul and the mad.
      bool kept = true;
      for (size_t between = before + 1; between < mad && kept; ++between) kept = !writes(between, position.index[0], (1u << x) | (1u << y));
      if (kept) found.push_back({mad, position.index[0], x, y});
      break;
    }
  }
  if (found.empty()) return {};
  // Back to front: earlier positions stay valid.
  for (auto site = found.rbegin(); site != found.rend(); ++site) {
    const auto [mad, position, x, y] = *site;
    const size_t at = starts[mad];
    const size_t source_at = dxbc::internal::FirstOperand(tokens, at) + 2;
    const uint32_t result = tokens[source_at - 1];
    // The mad reads rX.xyxx.
    tokens[source_at] = 0x00100046;
    tokens[source_at + 1] = result;
    const std::array<uint32_t, 17> added = {
        // add rX.xy, rP.(x)(y)xx, -cb0[40].xyxx
        dxbc::OP_ADD | (9u << 24), 0x00100032, result, 0x00100006 | (static_cast<uint32_t>(x) << 4) | (static_cast<uint32_t>(y) << 6), static_cast<uint32_t>(position),
        0x80208046, 0x00000041, 0u, VIEWPORT_ORIGIN,
        // mul rX.xy, rX.xyxx, cb0[39].zwzz
        OP_MUL | (8u << 24), 0x00100032, result, 0x00100046, result, 0x00208AE6, 0u, VIEWPORT_DIMENSIONS,
    };
    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(at), added.begin(), added.end());
  }
  tokens[1] = static_cast<uint32_t>(tokens.size());
  *sites = static_cast<int>(found.size());
  return dxbc::Write(code, program);
}

// The game's bytes as created, last of all patches (an init callback, the addon registers it where it has material_water shaders).
inline void OnInitMaterialWater(renodx::utils::pipeline::PipelineInfo* info, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects) {
  for (const auto& identity : info->shader_details) {
    const auto water = std::ranges::find(water_shaders, identity.shader_hash, &WaterShader::hash);
    if (water == water_shaders.end() || !water->material_water) continue;
    for (uint32_t index = 0; index < subobject_count; ++index) {
      if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
      const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
      if (desc == nullptr || desc->code == nullptr) continue;
      const auto* bytes = static_cast<const uint8_t*>(desc->code);
      const std::unique_lock lock(water_mutex);
      material_waters[water->hash] = {.code = std::vector<uint8_t>(bytes, bytes + desc->code_size)};
    }
  }
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
    if (water == water_shaders.end() || !water->caustics || water->material_water) continue;
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
  const auto& water = *std::ranges::find(water_shaders, command.matched_shader_hash, &WaterShader::hash);
  if (water.material_water) {
    const bool clarity_on = std::abs(water_clarity - 1.f) >= 0.01f;
    // Below 100% (the view in its target's top-left) the patched copy runs for its refraction's line (PatchWaterScaleLine).
    D3D11_VIEWPORT viewport = {};
    UINT viewport_count = 1;
    native->RSGetViewports(&viewport_count, &viewport);
    ComPtr<ID3D11RenderTargetView> target_view;
    native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    ComPtr<ID3D11Resource> target_resource;
    if (target_view) target_view->GetResource(target_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> target_texture;
    D3D11_TEXTURE2D_DESC target_desc = {};
    if (target_resource && SUCCEEDED(target_resource.As(&target_texture))) target_texture->GetDesc(&target_desc);
    const bool scaled = viewport_count == 1 && target_desc.Width != 0
                        && (viewport.Width + 0.5f < static_cast<float>(target_desc.Width) || viewport.Height + 0.5f < static_cast<float>(target_desc.Height));
    ComPtr<ID3D11PixelShader> patched;
    auto& material = material_objects;
    if (clarity_on || caustics_on || scaled) {
      const std::unique_lock lock(water_mutex);
      if (material.device != device) {
        const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
        material = {.device = device};
        device->CreateBuffer(&constants_desc, nullptr, material.constants.GetAddressOf());
        for (auto& [hash, entry] : material_waters) entry = {.code = std::move(entry.code)};
      }
      if (const auto entry = material_waters.find(water.hash); entry != material_waters.end()) {
        if (!entry->second.patched && !entry->second.refused) {
          // Caustics off (Pharaoh's campaign water): the game's code.
          std::vector<uint8_t> code = (water.caustics ? PatchWaterCaustics(entry->second.code, water) : entry->second.code);
          if (!code.empty()) code = PatchWaterAbsorption(code, water.opacity);
          int line_sites = 0;
          if (!code.empty()) {
            if (auto lined = PatchWaterScaleLine(code, &line_sites); !lined.empty()) code = std::move(lined);
          }
          entry->second.refused = code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, entry->second.patched.GetAddressOf()));
          renodx::utils::log::i(std::format("[Total War] Water: shader {:08X} {} (refraction's line below 100%: {} site(s)).", water.hash,
                                            entry->second.refused ? "not as expected, stays the game's" : "patched", line_sites));
        }
        patched = entry->second.patched;
      }
    }
    if (!patched || !material.constants) {
      if (caustics_on) {
        ID3D11ShaderResourceView* no_view = nullptr;
        native->PSSetShaderResources(WATER_CAUSTICS_SLOT, 1, &no_view);
      }
      return {};
    }
    if (material.written != water_clarity) {
      material.written = water_clarity;
      const std::array<float, 4> constants = {1.f / water_clarity, 0.f, 0.f, 0.f};
      native->UpdateSubresource(material.constants.Get(), 0, nullptr, constants.data(), 0, 0);
    }
    native->PSGetShader(material.game_shader.GetAddressOf(), nullptr, nullptr);
    native->PSGetConstantBuffers(WATER_CLARITY_SLOT, 1, material.game_constants.GetAddressOf());
    native->PSSetShader(patched.Get(), nullptr, 0);
    native->PSSetConstantBuffers(WATER_CLARITY_SLOT, 1, material.constants.GetAddressOf());
    return {.finalize_callback = [](Context& post, const void*) {
      auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
      post_native->PSSetShader(material_objects.game_shader.Get(), nullptr, 0);
      post_native->PSSetConstantBuffers(WATER_CLARITY_SLOT, 1, material_objects.game_constants.GetAddressOf());
      material_objects.game_shader.Reset();
      material_objects.game_constants.Reset();
      ID3D11ShaderResourceView* no_view = nullptr;
      post_native->PSSetShaderResources(WATER_CAUSTICS_SLOT, 1, &no_view);
    }};
  }
  // Water Clarity.
  const UINT lighting_slot = water.lighting_slot;
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

// The game's device is going away: the water's objects on it with it (the draws make them again).
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  if (caustics_objects.device.Get() == native) {
    caustics_objects = {};
  }
  if (water_objects.device.Get() == native) {
    water_objects = {};
  }
  if (material_objects.device.Get() == native) {
    const std::unique_lock lock(water_mutex);
    material_objects = {};
    for (auto& [hash, entry] : material_waters) entry = {.code = std::move(entry.code)};
  }
}

}  // namespace twattila::water_effects

#endif  // SRC_GAMES_TWATTILA_WATER_EFFECTS_HPP_
