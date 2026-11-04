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
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/format.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/log.hpp"
#include "../../utils/pipeline.hpp"
#include "./dxbc.hpp"
#include "./light_bounce.hpp"
#include "./view_share.hpp"

// Screen-space shadows (Experimental): Bend Studio's screen-space shadows (Apache 2.0, Copyright 2023 Sony Interactive
// Entertainment; sss_setup.cs_5_0.hlsl, twshogun2completeedition/shaders/bend_sss_gpu.hlsli) on the sun's light.
// The game's shadowed sun light shaders are patched as they are created (their bytecode, on top of the render scale's
// patch (view_share.hpp): after their shadow's saturate, x 1 - t49 at the pixel; t49 unbound reads 0, the game's
// light), so the sun's direct light, and nothing else, gets the screen-space shadow. At the frame's first patched sun
// draw of the main view (the display's size), with compute: the
// scene's depth (the depth-stencil buffer the sun draws keep bound, copied: 24 / 32 bits; the G-buffer's depth channel
// is too coarse far away for Bend's thickness test, whole areas came out shadowed in blocks (ATN debug4); it only without
// one) into a single-channel texture (sss_depth; with the game's MSAA sample 0), the dispatch list for its camera and sun
// (sss_setup, on the GPU: the constants stay there), then Bend's dispatches (sss_bend, indirect) into the occlusion mask
// (with Softness smoothed along surfaces by sss_blur into a second one); every patched sun draw of the main view then
// reads it at t49. Everything the compute binds is put back after. D3D11. Debug View (ATN: grass at the screen's
// border gets none): after the tone mapper, the occlusion as the picture (sss_debug).
namespace twattila::screen_space_shadows {

using Microsoft::WRL::ComPtr;

struct Settings {
  bool enabled = false;
  float samples = 240.f;     // per pixel: the shadow's length in pixels
  float thickness = 0.005f;  // Bend's SurfaceThickness
  float contrast = 4.f;          // Bend's ShadowContrast
  float edge_threshold = 0.02f;  // Bend's BilinearThreshold
  float hard_samples = 4.f;      // Bend's HARD_SHADOW_SAMPLES (a multiple of 4)
  float fade_samples = 8.f;      // Bend's FADE_OUT_SAMPLES (a multiple of 4)
  bool ignore_edges = false;     // Bend's IgnoreEdgePixels
  bool precision_offset = false;  // Bend's UsePrecisionOffset
  bool sample_along_ray = false;  // Bend's BilinearSamplingOffsetMode
  float softness = 0.f;          // sss_blur's tap spacing in pixels, 0 off
  int debug_view = 0;            // 0 off, 1 the occlusion, 2 with the screen edge's limit (sss_debug)
  bool mirror_edges = false;     // past the left, right and bottom edges: the depth mirrored (Bend's MirrorEdges)
  float foliage_light = 0.f;     // the sun's share through the shadows grass and leaves cast (InstallFoliage)
};

namespace internal {

constexpr UINT MASK_SLOT = 49;      // the patched sun shaders' occlusion
constexpr UINT TINT_SLOT = 50;      // their sun colour's filter (Coloured Shadows: 1 - the colour; unbound reads 0, none)
constexpr UINT AMBIENT_SLOT = 51;   // their diffuse ambient's share taken away (texel 0's x; unbound reads 0: the game's)

// Another occlusion of the sun's light (Attila's ray-traced sun shadows: rt_scene::Occlusion): given the screen-space
// shadow's mask this frame (null: off), the occlusion to bind instead (the darker of both), or null (the mask alone).
using ExtraOcclusion = ID3D11ShaderResourceView* (*)(ID3D11DeviceContext*, ID3D11ShaderResourceView*);
static ExtraOcclusion extra_occlusion = nullptr;
// The smoke's dimming over all that (Attila: smoke_map::Occlusion): given the occlusion so far (null: none), the draw and
// its G-buffer depth, the occlusion to bind instead, or null (no smoke).
using SmokeOcclusion = ID3D11ShaderResourceView* (*)(ID3D11DeviceContext*, ID3D11ShaderResourceView*, const light_bounce::SunDraw&, ID3D11ShaderResourceView*);
static SmokeOcclusion smoke_occlusion = nullptr;
constexpr UINT DISPATCH_COUNT = 8;  // Bend's most

// sss.hlsli's sss_settings.
struct ShaderSettings {
  float width = 0.f;
  float height = 0.f;
  float inverse_width = 0.f;
  float inverse_height = 0.f;
  float thickness = 0.f;
  float samples = 0.f;
  float contrast = 0.f;
  float edge_threshold = 0.f;
  float hard_samples = 0.f;
  float fade_samples = 0.f;
  float ignore_edges = 0.f;
  float precision_offset = 0.f;
  float sample_along_ray = 0.f;
  float softness = 0.f;
  float debug_view = 0.f;
  float mirror_edges = 0.f;
  float sun_direction = 0.f;  // its first float in the sun draw's lighting_VS_PS
  float render_width = 0.f;   // the 3D image's (DLSS: the top-left corner of the depth)
  float render_height = 0.f;
  float foliage_light = 0.f;  // sss_depth, sss_bend
};

// A shadowed sun light shader with the screen-space shadow, its shadow x (1 - t49 at the pixel), rT = 1 - t49 from
//   ftou rT.xy, vP.xyxx / mov rT.zw, l(0) / ld rT.x, rT.xyzw, t49.xyzw / mad rT.x, rT.x, l(-1), l(1)
// first, with rT one more temp and t49 declared (texture2d, float):
// - Attila, Thrones, Rome II: after its shadow's last step right before div r0.xy, r0.xyxx, r0.wwww (mov_sat rN.c,
//   rN.c; Rome II: min rN.c, rN.c, rM.c, its cascades joined; the one such pair in each shader):
//   mul_sat rN.c, rN.c, rT.xxxx.
// - Else (Troy, Pharaoh, Warhammer: their shadow ends differently in each shader): after each shadow map comparison
//   (sample_c, sample_c_lz, gather4_c, gather4_po_c) mul rX.mask, rX.xyzw, rT.xxxx: the shadow is their filtered
//   average, so it is scaled the same.
// - With lighting_VS_PS (Attila's and the others' sun colour at its c1, the RDEF's slot for it): every read of c1 from
//   rC (one more temp), the colour x the filter at t50 (Coloured Shadows: light through cloth and sails), before rT's
//   ld: ld rC.xyzw, rT.xyzw, t50.xyzw / mad rC.xyz, rC.xyzx, l(-1), l(1) / mul rC.xyz, rC.xyzx, cbL[1].xyzx. None when
//   one reads c1 relatively or its w.
// - Else (Warhammer III's shadow mask): rT first, and after each read of the texture named t_shadow (ld / sample) the
//   same mul.
// - A forward pass reading the sun's shadow map (`forward`: Attila's under Ray-Traced Shadows Only) as Troy's.
// Empty: not such a shader.
inline std::vector<uint8_t> Patch(std::span<const uint8_t> code, bool forward = false) {
  constexpr uint32_t OP_CUSTOMDATA = 53, OP_DIV = 14, OP_FTOU = 28, OP_LD = 45, OP_MAD = 50, OP_MIN = 51, OP_MOV = 54,
                     OP_MUL = 56, OP_SAMPLE = 69, OP_SAMPLE_C = 70, OP_SAMPLE_C_LZ = 71, OP_SAMPLE_L = 72,
                     OP_GATHER4_C = 126, OP_GATHER4_PO_C = 128;
  constexpr uint32_t OP_DCL_RESOURCE = 88, OP_DCL_INPUT_PS_SIV = 100, OP_DCL_TEMPS = 104, OP_DCL_GLOBAL_FLAGS = 106;
  constexpr uint32_t CUSTOMDATA_IMMEDIATE_CONSTANT_BUFFER = 3;
  constexpr uint32_t SATURATE = 1u << 13;
  constexpr uint32_t EXTENDED = 1u << 31;
  constexpr uint32_t NAME_POSITION = 1;
  constexpr uint32_t TYPE_TEMP = 0, TYPE_INPUT = 1, TYPE_IMMEDIATE = 4, TYPE_RESOURCE = 7, TYPE_CONSTANT_BUFFER = 8;
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};

  // The shadow mask's slot: the RDEF chunk's texture (shader input type 2) named t_shadow (32-byte bindings, SM 5.0);
  // the lighting constants' (type 0, lighting_VS_PS).
  uint32_t mask_slot = UINT32_MAX;
  uint32_t lighting_slot = UINT32_MAX;
  for (const uint32_t offset : program.offsets) {
    if (std::memcmp(code.data() + offset, "RDEF", 4) != 0) continue;
    const size_t data = offset + 8;
    const auto read = [&](size_t at) {
      uint32_t value = 0;
      if (at + 4 <= code.size()) std::memcpy(&value, code.data() + at, 4);
      return value;
    };
    const uint32_t bindings = read(data + 8);
    const size_t binding_at = data + read(data + 12);
    for (uint32_t index = 0; index < bindings && binding_at + 32 * (index + 1) <= code.size(); ++index) {
      const size_t binding = binding_at + 32 * size_t{index};
      const size_t name = data + read(binding);
      if (read(binding + 4) == 2 && name + 9 <= code.size() && std::memcmp(code.data() + name, "t_shadow", 9) == 0) {
        mask_slot = read(binding + 20);
      }
      if (read(binding + 4) == 0 && name + 15 <= code.size() && std::memcmp(code.data() + name, "lighting_VS_PS", 15) == 0) {
        lighting_slot = read(binding + 20);
      }
    }
  }

  // The sun colour's reads (c1 of lighting_VS_PS) from rC, the temp after rT.
  const auto temps = std::ranges::find_if(tokens.begin() + 2, tokens.end(), [](uint32_t token) { return (token & 0x7FF) == OP_DCL_TEMPS; });
  const uint32_t colour_temp = (temps != tokens.end() && temps + 1 != tokens.end()) ? *(temps + 1) + 1 : UINT32_MAX;
  bool coloured = lighting_slot != UINT32_MAX && colour_temp != UINT32_MAX;
  std::vector<uint32_t> recoloured(tokens.begin(), tokens.begin() + 2);
  for (size_t at = 2; at < tokens.size() && coloured;) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    size_t operand_at = at + 1;
    if ((tokens[at] & EXTENDED) != 0 && opcode != OP_CUSTOMDATA && length > 1) {
      while (operand_at < at + length && (tokens[operand_at] & EXTENDED) != 0) ++operand_at;
      ++operand_at;
    }
    const bool declaration = (opcode >= OP_DCL_RESOURCE && opcode <= OP_DCL_GLOBAL_FLAGS) || (opcode >= 143 && opcode <= 162) || opcode == OP_CUSTOMDATA;
    if (declaration) operand_at = at + length;
    const size_t first = recoloured.size();
    recoloured.insert(recoloured.end(), tokens.begin() + static_cast<ptrdiff_t>(at), tokens.begin() + static_cast<ptrdiff_t>(operand_at));
    bool changed = false;
    while (operand_at < at + length) {
      dxbc::Operand operand;
      const size_t operand_length = dxbc::ParseOperand(tokens, operand_at, at + length, &operand);
      if (operand_length == 0) break;
      const bool sun_colour = operand.type == TYPE_CONSTANT_BUFFER && operand.index[0] == lighting_slot && operand.index[1] == 1;
      if (sun_colour) {
        const bool reads_w = (operand.mode == 1) ? std::ranges::any_of(std::array{0u, 2u, 4u, 6u}, [&](uint32_t shift) { return ((operand.select >> shift) & 3) == 3; })
                                                 : (operand.mode == 2 && operand.select == 3);
        if (operand.relative || reads_w) {
          coloured = false;
          break;
        }
        // The same selection from rC: a temp, one immediate index; its modifiers (extended tokens) kept.
        const size_t indices_at = operand_at + operand_length - 2;
        recoloured.push_back((tokens[operand_at] & ~((0xFFu << 12) | (3u << 20) | (0x3Fu << 22))) | (TYPE_TEMP << 12) | (1u << 20));
        recoloured.insert(recoloured.end(), tokens.begin() + static_cast<ptrdiff_t>(operand_at + 1),
                          tokens.begin() + static_cast<ptrdiff_t>(indices_at));
        recoloured.push_back(colour_temp);
        changed = true;
      } else {
        recoloured.insert(recoloured.end(), tokens.begin() + static_cast<ptrdiff_t>(operand_at),
                          tokens.begin() + static_cast<ptrdiff_t>(operand_at + operand_length));
      }
      operand_at += operand_length;
    }
    recoloured.insert(recoloured.end(), tokens.begin() + static_cast<ptrdiff_t>(operand_at), tokens.begin() + static_cast<ptrdiff_t>(at + length));
    if (changed) {
      recoloured[first] = (recoloured[first] & ~(0x7Fu << 24)) | (static_cast<uint32_t>(recoloured.size() - first) << 24);
    }
    at += length;
  }
  if (coloured && recoloured.size() != tokens.size()) {
    tokens = std::move(recoloured);
    tokens[1] = static_cast<uint32_t>(tokens.size());
  } else {
    coloured = false;
  }

  // The declarations (where the code starts), the shadow's last step, the comparisons and the shadow mask's reads.
  size_t temps_at = 0;
  size_t code_at = 0;
  uint32_t position = UINT32_MAX;
  size_t saturate_at = 0;
  size_t previous_at = 0;
  std::vector<size_t> comparisons;
  std::vector<size_t> mask_reads;
  for (size_t at = 2; at < tokens.size();) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    if ((opcode >= OP_DCL_RESOURCE && opcode <= OP_DCL_GLOBAL_FLAGS)
        || (opcode == OP_CUSTOMDATA && (tokens[at] >> 11) == CUSTOMDATA_IMMEDIATE_CONSTANT_BUFFER)) {
      code_at = at + length;
    }
    if (opcode == OP_DCL_TEMPS) temps_at = at;
    if (opcode == OP_DCL_INPUT_PS_SIV && length == 4 && tokens[at + 3] == NAME_POSITION) position = tokens[at + 2];
    if (opcode == OP_SAMPLE_C || opcode == OP_SAMPLE_C_LZ || opcode == OP_GATHER4_C || opcode == OP_GATHER4_PO_C) {
      comparisons.push_back(at);
    }
    if (mask_slot != UINT32_MAX && (opcode == OP_LD || opcode == OP_SAMPLE || opcode == OP_SAMPLE_L)) {
      // Its resource operand: t (type 7), one immediate index.
      for (size_t token = at + 1; token + 1 < at + length; ++token) {
        if ((tokens[token] & 0x003FF000) == 0x00107000 && tokens[token + 1] == mask_slot) {
          mask_reads.push_back(at);
          break;
        }
      }
    }
    if (opcode == OP_DIV && previous_at != 0) {
      const uint32_t previous = tokens[previous_at];
      const uint32_t previous_length = (previous >> 24) & 0x7F;
      const bool mov_sat = ((previous & 0x7FF) == OP_MOV && (previous & SATURATE) != 0 && previous_length == 5);
      const bool joined = ((previous & 0x7FF) == OP_MIN && (previous & SATURATE) == 0 && previous_length == 7
                           && ((tokens[previous_at + 5] >> 12) & 0xFF) == TYPE_TEMP);
      // mov_sat rN.c, rN.c / min rN.c, rN.c, ...: a temp's single component (mask) from itself (select-1 or swizzle).
      const uint32_t dest = tokens[previous_at + 1];
      const uint32_t source = tokens[previous_at + 3];
      if ((mov_sat || joined) && (dest & 0xFF00F) == 0x00002 && ((source & 0xFF00F) == 0x0000A || (source & 0xFF00F) == 0x00006)
          && tokens[previous_at + 2] == tokens[previous_at + 4]) {
        if (saturate_at != 0) return {};
        saturate_at = previous_at;
      }
    }
    previous_at = at;
    at += length;
  }
  // A shader reading t_shadow has its shadow there: a joined pair in it is its ambient occlusion (Warhammer III).
  if (forward || mask_slot != UINT32_MAX) saturate_at = 0;
  const std::vector<size_t>& scaled = (comparisons.empty() && !forward) ? mask_reads : comparisons;
  if (temps_at == 0 || position == UINT32_MAX || (saturate_at == 0 && scaled.empty())) return {};

  // Operands: temp with a write mask, any register with a swizzle, a 1-component immediate.
  const uint32_t temp = tokens[temps_at + 1];
  const auto masked = [](uint32_t index, uint32_t mask) { return std::array<uint32_t, 2>{2u | (mask << 4) | (1u << 20), index}; };
  const auto swizzled = [](uint32_t type, uint32_t index, uint32_t swizzle) {
    return std::array<uint32_t, 2>{2u | (1u << 2) | (swizzle << 4) | (type << 12) | (1u << 20), index};
  };
  constexpr uint32_t XYXX = 0x04, XYZX = 0x24, XYZW = 0xE4, XXXX = 0x00;
  constexpr uint32_t MINUS_ONE = 0xBF800000, ONE = 0x3F800000;
  const auto instruction = [](std::vector<uint32_t>* added, uint32_t opcode,
                              std::initializer_list<std::array<uint32_t, 2>> operands, uint32_t flags = 0) {
    added->push_back(opcode | flags | (static_cast<uint32_t>(1 + 2 * operands.size()) << 24));
    for (const auto& operand : operands) added->insert(added->end(), operand.begin(), operand.end());
  };
  std::vector<uint32_t> load;
  instruction(&load, OP_FTOU, {masked(temp, 0b0011), swizzled(TYPE_INPUT, position, XYXX)});
  load.insert(load.end(), {OP_MOV | (8u << 24), masked(temp, 0b1100)[0], temp, 2u | (TYPE_IMMEDIATE << 12), 0, 0, 0, 0});
  if (coloured) {
    const uint32_t colour = temp + 1;
    instruction(&load, OP_LD, {masked(colour, 0b1111), swizzled(TYPE_TEMP, temp, XYZW), swizzled(TYPE_RESOURCE, TINT_SLOT, XYZW)});
    load.insert(load.end(), {OP_MAD | (15u << 24), masked(colour, 0b0111)[0], colour, swizzled(TYPE_TEMP, colour, XYZX)[0], colour,
                             2u | (TYPE_IMMEDIATE << 12), MINUS_ONE, MINUS_ONE, MINUS_ONE, MINUS_ONE,
                             2u | (TYPE_IMMEDIATE << 12), ONE, ONE, ONE, ONE});
    load.insert(load.end(), {OP_MUL | (8u << 24), masked(colour, 0b0111)[0], colour, swizzled(TYPE_TEMP, colour, XYZX)[0], colour,
                             (swizzled(TYPE_CONSTANT_BUFFER, lighting_slot, XYZX)[0] & ~(3u << 20)) | (2u << 20), lighting_slot, 1});
  }
  instruction(&load, OP_LD, {masked(temp, 0b0001), swizzled(TYPE_TEMP, temp, XYZW), swizzled(TYPE_RESOURCE, MASK_SLOT, XYZW)});
  instruction(&load, OP_MAD, {masked(temp, 0b0001), swizzled(TYPE_TEMP, temp, XXXX), {1u | (TYPE_IMMEDIATE << 12), MINUS_ONE},
                              {1u | (TYPE_IMMEDIATE << 12), ONE}});
  if (saturate_at != 0) {
    std::vector<uint32_t> scale;
    instruction(&scale, OP_MUL,
                {{tokens[saturate_at + 1], tokens[saturate_at + 2]}, {tokens[saturate_at + 3], tokens[saturate_at + 4]},
                 swizzled(TYPE_TEMP, temp, XXXX)},
                SATURATE);
    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(saturate_at + ((tokens[saturate_at] >> 24) & 0x7F)), scale.begin(),
                  scale.end());
  } else {
    // From the last one back (the earlier ones keep their place).
    for (auto at = scaled.rbegin(); at != scaled.rend(); ++at) {
      size_t dest_at = *at + 1;
      if ((tokens[*at] & EXTENDED) != 0) {
        while ((tokens[dest_at] & EXTENDED) != 0) ++dest_at;
        ++dest_at;
      }
      // A temp's components (mask), one immediate index.
      const uint32_t dest = tokens[dest_at];
      if ((dest & 0xFFFFF00F) != 0x00100002) return {};
      std::vector<uint32_t> scale;
      instruction(&scale, OP_MUL, {{dest, tokens[dest_at + 1]}, swizzled(TYPE_TEMP, tokens[dest_at + 1], XYZW),
                                   swizzled(TYPE_TEMP, temp, XXXX)});
      tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(*at + ((tokens[*at] >> 24) & 0x7F)), scale.begin(), scale.end());
    }
  }
  // rT (and rC) where the code starts.
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(code_at), load.begin(), load.end());
  // dcl_resource_texture2d (float,float,float,float) t49 (and t50), before dcl_temps (the declarations come first).
  std::vector<uint32_t> declarations;
  for (const UINT slot : {MASK_SLOT, TINT_SLOT}) {
    if (slot == TINT_SLOT && !coloured) continue;
    declarations.insert(declarations.end(), {OP_DCL_RESOURCE | (3u << 11) | (4u << 24), (TYPE_RESOURCE << 12) | (1u << 20), slot, 0x5555});
  }
  tokens[temps_at + 1] += coloured ? 2 : 1;
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), declarations.begin(), declarations.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return dxbc::Write(code, program);
}

// A sun light shader's diffuse ambient (Attila: the ambient cube at the normal, lighting_VS_PS c2-c7, the first of its
// cube reads; the foliage ones read it again at the reflection for a sky sheen, which stays) x (1 - t51's texel 0 x):
// after its cube's last step (the second mad after the movc of c7 / c6)
//   ld rA.x, l(0, 0, 0, 0), t51.xyzw / mad rA.x, rA.x, l(-1), l(1) / mul rC.xyz, rC.xyzx, rA.xxxx
// with rA one more temp and t51 declared. Path-Traced Ambient binds a texel of 1 there (the paths bring the ambient), the
// game's ambient otherwise. `foliage`: the foliage shaders' (their ambient 0.6 or 0.8 of the others', and the sheen).
// Empty: no such cube read.
// `final_sites` above 0 (Warhammer III, its count per shader): the cube is only one way there (spherical harmonics first, its
// probes' Global Illumination over either), and that second mad is inside one of them. Instead where they all end,
//   movc rA.xyzw, rB.s, rC.xyzw, rA.wxyz / mul rA.yzw, ...   (rA.yzw the irradiance; rA.x its AO is divided by, left alone)
// the same three instructions on rA.yzw between those two. Another number of such places: empty.
inline std::vector<uint8_t> PatchAmbient(std::span<const uint8_t> code, bool* foliage, uint32_t final_sites = 0) {
  constexpr uint32_t OP_LD = 45, OP_MAD = 50, OP_MOVC = 55, OP_MUL = 56, OP_CUSTOMDATA = 53;
  constexpr uint32_t OP_DCL_TEMPS = 104;
  constexpr uint32_t TYPE_TEMP = 0, TYPE_IMMEDIATE = 4, TYPE_RESOURCE = 7, TYPE_CONSTANT_BUFFER = 8;
  constexpr uint32_t EXTENDED = 1u << 31;
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  // The foliage shaders' ambient factors (c11.x > 0: 0.004, 360, 0.6, 1).
  constexpr std::array<uint32_t, 4> FOLIAGE_FACTORS = {0x3B83126F, 0x43B40000, 0x3F19999A, 0x3F800000};
  *foliage = std::ranges::search(tokens, FOLIAGE_FACTORS).begin() != tokens.end();
  uint32_t lighting_slot = UINT32_MAX;
  for (const uint32_t offset : program.offsets) {
    if (std::memcmp(code.data() + offset, "RDEF", 4) != 0) continue;
    const size_t data = offset + 8;
    const auto read = [&](size_t at) {
      uint32_t value = 0;
      if (at + 4 <= code.size()) std::memcpy(&value, code.data() + at, 4);
      return value;
    };
    const uint32_t bindings = read(data + 8);
    const size_t binding_at = data + read(data + 12);
    for (uint32_t index = 0; index < bindings && binding_at + 32 * (index + 1) <= code.size(); ++index) {
      const size_t binding = binding_at + 32 * size_t{index};
      const size_t name = data + read(binding);
      if (read(binding + 4) == 0 && name + 15 <= code.size() && std::memcmp(code.data() + name, "lighting_VS_PS", 15) == 0) {
        lighting_slot = read(binding + 20);
      }
    }
  }
  if (lighting_slot == UINT32_MAX) return {};
  const auto masked = [](uint32_t index, uint32_t mask) { return std::array<uint32_t, 2>{2u | (mask << 4) | (1u << 20), index}; };
  const auto swizzled = [](uint32_t type, uint32_t index, uint32_t swizzle) {
    return std::array<uint32_t, 2>{2u | (1u << 2) | (swizzle << 4) | (type << 12) | (1u << 20), index};
  };
  constexpr uint32_t XYZX = 0x24, XYZW = 0xE4, XXXX = 0x00, WXYZ = 0x93;
  size_t temps_at = 0;
  size_t cube_at = 0;    // the movc reading c7 / c6
  size_t mads = 0;       // after it
  // After the cube's last step, or `final_sites` places.
  std::vector<std::pair<size_t, uint32_t>> sites;
  for (size_t at = 2; at < tokens.size() && (final_sites != 0 || sites.empty());) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    if (opcode == OP_DCL_TEMPS) temps_at = at;
    if (final_sites != 0) {
      const size_t next = at + length;
      if (opcode == OP_MOVC && length == 9 && next + 2 < tokens.size() && (tokens[next] & 0x7FF) == OP_MUL) {
        const uint32_t irradiance = tokens[at + 2];
        if (tokens[at + 1] == masked(irradiance, 0b1111)[0] && tokens[at + 5] == swizzled(TYPE_TEMP, 0, XYZW)[0]
            && tokens[at + 7] == swizzled(TYPE_TEMP, 0, WXYZ)[0] && tokens[at + 8] == irradiance
            && tokens[next + 1] == masked(irradiance, 0b1110)[0] && tokens[next + 2] == irradiance) {
          sites.emplace_back(next, irradiance);
        }
      }
      at += length;
      continue;
    }
    size_t operand_at = at + 1;
    if ((tokens[at] & EXTENDED) != 0 && opcode != OP_CUSTOMDATA) {
      while (operand_at < at + length && (tokens[operand_at] & EXTENDED) != 0) ++operand_at;
      ++operand_at;
    }
    if (opcode == OP_MOVC && cube_at == 0) {
      // movc rX, rY, cbL[7], cbL[6]: its last two operands.
      std::array<dxbc::Operand, 4> operands = {};
      size_t next = operand_at;
      size_t count = 0;
      while (next < at + length && count < operands.size()) {
        const size_t operand_length = dxbc::ParseOperand(tokens, next, at + length, &operands[count]);
        if (operand_length == 0) return {};
        next += operand_length;
        ++count;
      }
      const auto reads = [&](const dxbc::Operand& operand, uint32_t index) {
        return operand.type == TYPE_CONSTANT_BUFFER && !operand.relative && operand.index[0] == lighting_slot && operand.index[1] == index;
      };
      if (count == 4 && reads(operands[2], 7) && reads(operands[3], 6)) cube_at = at;
    } else if (opcode == OP_MAD && cube_at != 0 && ++mads == 2) {
      dxbc::Operand dest;
      if (dxbc::ParseOperand(tokens, operand_at, at + length, &dest) == 0 || dest.type != TYPE_TEMP || dest.mode != 0 || dest.relative
          || (dest.select & 0b0111) != 0b0111) {
        return {};
      }
      sites.emplace_back(at + length, dest.index[0]);
    }
    at += length;
  }
  if (temps_at == 0 || sites.size() != std::max(final_sites, 1u)) return {};
  const uint32_t temp = tokens[temps_at + 1];
  constexpr uint32_t MINUS_ONE = 0xBF800000, ONE = 0x3F800000;
  const std::array<uint32_t, 2> taken = masked(temp, 0b0001);
  const std::array<uint32_t, 2> resource = swizzled(TYPE_RESOURCE, AMBIENT_SLOT, XYZW);
  const std::array<uint32_t, 2> share = swizzled(TYPE_TEMP, temp, XXXX);
  // From the last one back (the earlier ones keep their place).
  for (auto site = sites.rbegin(); site != sites.rend(); ++site) {
    const auto [insert_at, ambient_temp] = *site;
    const std::array<uint32_t, 2> ambient = masked(ambient_temp, (final_sites != 0) ? 0b1110 : 0b0111);
    const std::array<uint32_t, 2> ambient_read = swizzled(TYPE_TEMP, ambient_temp, (final_sites != 0) ? XYZW : XYZX);
    const std::vector<uint32_t> scale = {
        OP_LD | (10u << 24), taken[0], taken[1], 2u | (TYPE_IMMEDIATE << 12), 0, 0, 0, 0, resource[0], resource[1],
        OP_MAD | (9u << 24), taken[0], taken[1], share[0], share[1], 1u | (TYPE_IMMEDIATE << 12), MINUS_ONE, 1u | (TYPE_IMMEDIATE << 12), ONE,
        OP_MUL | (7u << 24), ambient[0], ambient[1], ambient_read[0], ambient_read[1], share[0], share[1],
    };
    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(insert_at), scale.begin(), scale.end());
  }
  // dcl_resource_texture2d (float,float,float,float) t51 before dcl_temps (the declarations come first); one more temp.
  tokens[temps_at + 1] += 1;
  constexpr uint32_t OP_DCL_RESOURCE = 88;
  const std::array<uint32_t, 4> declaration = {OP_DCL_RESOURCE | (3u << 11) | (4u << 24), (TYPE_RESOURCE << 12) | (1u << 20), AMBIENT_SLOT, 0x5555};
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), declaration.begin(), declaration.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return dxbc::Write(code, program);
}

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> depth_copy;
  ComPtr<ID3D11ComputeShader> depth_copy_ms;
  ComPtr<ID3D11ComputeShader> setup;
  ComPtr<ID3D11ComputeShader> bend;
  ComPtr<ID3D11ComputeShader> blur;
  ComPtr<ID3D11ComputeShader> debug;
  ComPtr<ID3D11Buffer> settings;
  ComPtr<ID3D11Buffer> camera;  // the sun draw's in Attila's layout (frame_history::AttilaCamera)
  std::array<ComPtr<ID3D11Buffer>, DISPATCH_COUNT> dispatch_indexes;
  ComPtr<ID3D11Buffer> arguments;
  ComPtr<ID3D11UnorderedAccessView> arguments_uav;
  ComPtr<ID3D11Buffer> parameters;
  ComPtr<ID3D11UnorderedAccessView> parameters_uav;
  ComPtr<ID3D11ShaderResourceView> parameters_srv;
  ComPtr<ID3D11SamplerState> border;
  // The screen's size: the depth copy and the occlusion.
  UINT width = 0;
  UINT height = 0;
  ComPtr<ID3D11UnorderedAccessView> depth_uav;
  ComPtr<ID3D11ShaderResourceView> depth_srv;
  ComPtr<ID3D11UnorderedAccessView> occlusion_uav;
  ComPtr<ID3D11ShaderResourceView> occlusion_srv;
  // The occlusion after Softness (sss_blur).
  ComPtr<ID3D11UnorderedAccessView> soft_uav;
  ComPtr<ID3D11ShaderResourceView> soft_srv;
  // Debug View: the tone mapper's output's size and format, made when first shown.
  ComPtr<ID3D11Texture2D> debug_texture;
  ComPtr<ID3D11UnorderedAccessView> debug_uav;
  // The scene's depth-stencil buffer and the addon's copy of it (its typeless format, readable).
  ComPtr<ID3D11Texture2D> scene_depth;
  ComPtr<ID3D11Texture2D> scene_depth_copy;
  ComPtr<ID3D11ShaderResourceView> scene_depth_srv;
  // The depth buffer before and after the foliage's depth pass (OnFoliageOrder).
  ComPtr<ID3D11Texture2D> before_foliage;
  ComPtr<ID3D11ShaderResourceView> before_foliage_srv;
  ComPtr<ID3D11Texture2D> after_foliage;
  ComPtr<ID3D11ShaderResourceView> after_foliage_srv;
  bool logged_gbuffer_depth = false;
  ShaderSettings uploaded = {};
};

struct Runtime {
  std::shared_mutex mutex;
  // Set by the addon every present.
  Settings settings = {};
  uint64_t frame = 0;
  UINT display_width = 0;
  UINT display_height = 0;
  // The frame the occlusion is this frame's for (UINT64_MAX: none yet).
  uint64_t computed_frame = UINT64_MAX;
  uint64_t debug_frame = UINT64_MAX;  // the Debug View drawn (OnTonemapDrawn)
  // A report into ReShade.log at the next computed frame (each time the shadows are turned on).
  // The frames those copies are of.
  uint64_t before_foliage_frame = UINT64_MAX;
  uint64_t after_foliage_frame = UINT64_MAX;
  // The patched sun shaders by their original hash (kept: the game may create them again), the forward passes' too.
  std::unordered_map<uint32_t, std::vector<uint8_t>> patched;
  // The sun shaders whose diffuse ambient t51 takes away (PatchAmbient), by hash: whether they are foliage ones.
  std::unordered_map<uint32_t, bool> ambient_patched;
  std::unordered_map<uint32_t, uint32_t> ambient_final_sites;  // light_bounce::SunDraw::ambient_final_sites by hash, where set
  bool ambient_not_as_expected = false;
  std::vector<uint32_t> sun_hashes;
  std::vector<uint32_t> forward_hashes;
  DeviceObjects objects;
  uint32_t off_presents = 0;
};

static Runtime runtime;

// The device's objects (the size-bound ones again when the screen's size changes); false: not available.
inline bool Prepare(ID3D11Device* device, UINT width, UINT height) {
  auto& objects = runtime.objects;
  if (objects.device.Get() != device) {
    objects = {};
    objects.device = device;
    const auto compute = [&](const auto& code, ComPtr<ID3D11ComputeShader>* shader) {
      return SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
    };
    const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = sizeof(ShaderSettings), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC arguments_desc = {
        .ByteWidth = DISPATCH_COUNT * 12,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
        .MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
    };
    const D3D11_UNORDERED_ACCESS_VIEW_DESC arguments_view = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = DISPATCH_COUNT * 3, .Flags = D3D11_BUFFER_UAV_FLAG_RAW},
    };
    const D3D11_BUFFER_DESC parameters_desc = {
        .ByteWidth = (1 + DISPATCH_COUNT) * 16,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
        .StructureByteStride = 16,
    };
    // Off screen: the far depth (Bend reads past the edges on purpose).
    const D3D11_SAMPLER_DESC border_desc = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
        .AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
        .AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
        .AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .BorderColor = {1.f, 1.f, 1.f, 1.f},
        .MaxLOD = D3D11_FLOAT32_MAX,
    };

    bool made = compute(__sss_depth, &objects.depth_copy) && compute(__sss_depth_ms, &objects.depth_copy_ms)
                && compute(__sss_setup, &objects.setup) && compute(__sss_bend, &objects.bend)
                && compute(__sss_blur, &objects.blur) && compute(__sss_debug, &objects.debug)
                && SUCCEEDED(device->CreateBuffer(&settings_desc, nullptr, objects.settings.GetAddressOf()))
                && SUCCEEDED(device->CreateBuffer(&arguments_desc, nullptr, objects.arguments.GetAddressOf()))
                && SUCCEEDED(device->CreateUnorderedAccessView(objects.arguments.Get(), &arguments_view,
                                                               objects.arguments_uav.GetAddressOf()))
                && SUCCEEDED(device->CreateBuffer(&parameters_desc, nullptr, objects.parameters.GetAddressOf()))
                && SUCCEEDED(device->CreateUnorderedAccessView(objects.parameters.Get(), nullptr,
                                                               objects.parameters_uav.GetAddressOf()))
                && SUCCEEDED(device->CreateShaderResourceView(objects.parameters.Get(), nullptr,
                                                              objects.parameters_srv.GetAddressOf()))
                && SUCCEEDED(device->CreateSamplerState(&border_desc, objects.border.GetAddressOf()));
    // sss_bend's b3: which of the list's dispatches it is.
    for (UINT index = 0; made && index < DISPATCH_COUNT; ++index) {
      const std::array<UINT, 4> value = {index, 0, 0, 0};
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = value.data()};
      made = SUCCEEDED(device->CreateBuffer(&desc, &data, objects.dispatch_indexes[index].GetAddressOf()));
    }
    if (!made) {
      renodx::utils::log::w("[Total War] Screen-space shadows: could not create its shaders or buffers.");
      objects = {};
      objects.device = device;
      return false;
    }
  }
  if (!objects.setup) return false;
  if (objects.width == width && objects.height == height) return true;
  objects.width = 0;
  objects.depth_uav.Reset();
  objects.depth_srv.Reset();
  objects.occlusion_uav.Reset();
  objects.occlusion_srv.Reset();
  objects.soft_uav.Reset();
  objects.soft_srv.Reset();
  objects.debug_texture.Reset();
  objects.debug_uav.Reset();
  const auto make = [&](DXGI_FORMAT format, ComPtr<ID3D11UnorderedAccessView>* uav, ComPtr<ID3D11ShaderResourceView>* srv) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = width,
        .Height = height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
    };
    ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()))
           && SUCCEEDED(device->CreateUnorderedAccessView(texture.Get(), nullptr, uav->GetAddressOf()))
           && SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, srv->GetAddressOf()));
  };
  if (!make(DXGI_FORMAT_R32_FLOAT, &objects.depth_uav, &objects.depth_srv)
      || !make(DXGI_FORMAT_R8_UNORM, &objects.occlusion_uav, &objects.occlusion_srv)
      || !make(DXGI_FORMAT_R8_UNORM, &objects.soft_uav, &objects.soft_srv)) {
    renodx::utils::log::w("[Total War] Screen-space shadows: could not create its textures (", width, " x ", height, ").");
    return false;
  }
  objects.width = width;
  objects.height = height;
  return true;
}

// A readable copy of a depth-stencil buffer (`desc`) and its depth's view; false: none.
inline bool MakeDepthCopy(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc, ComPtr<ID3D11Texture2D>* copy, ComPtr<ID3D11ShaderResourceView>* view) {
  copy->Reset();
  view->Reset();
  const auto [typeless, view_format] = frame_history::DepthFormats(desc.Format);
  D3D11_TEXTURE2D_DESC copy_desc = desc;
  copy_desc.Format = typeless;
  copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  copy_desc.CPUAccessFlags = 0;
  copy_desc.MiscFlags = 0;
  copy_desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = view_format};
  if (desc.SampleDesc.Count > 1) {
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
  } else {
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view_desc.Texture2D = {.MostDetailedMip = 0, .MipLevels = 1};
  }
  const bool made = (typeless != DXGI_FORMAT_UNKNOWN) && SUCCEEDED(device->CreateTexture2D(&copy_desc, nullptr, copy->GetAddressOf()))
                    && SUCCEEDED(device->CreateShaderResourceView(copy->Get(), &view_desc, view->GetAddressOf()));
  if (!made) {
    copy->Reset();
    view->Reset();
  }
  return made;
}

// The scene's depth-stencil buffer (`depth_stencil`, the sun draw's) copied for the compute (`msaa`: multisampled);
// null: none at the screen's size or not a depth format.
inline ID3D11ShaderResourceView* SceneDepth(ID3D11Device* device, ID3D11DeviceContext* native,
                                            ID3D11DepthStencilView* depth_stencil, bool* msaa) {
  auto& objects = runtime.objects;
  if (depth_stencil == nullptr) return nullptr;
  ComPtr<ID3D11Resource> resource;
  depth_stencil->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  if (FAILED(resource.As(&texture))) return nullptr;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (desc.Width != objects.width || desc.Height != objects.height || desc.ArraySize != 1) return nullptr;
  *msaa = (desc.SampleDesc.Count > 1);
  if (objects.scene_depth.Get() != texture.Get()) {
    objects.scene_depth = texture;
    const bool made = MakeDepthCopy(device, desc, &objects.scene_depth_copy, &objects.scene_depth_srv);
    renodx::utils::log::i("[Total War] Screen-space shadows: the depth-stencil buffer (format ", static_cast<int>(desc.Format),
                          ", ", desc.SampleDesc.Count, " samples) ", (made ? "copied for the shadows." : "unusable."));
  }
  if (!objects.scene_depth_srv) return nullptr;
  native->CopyResource(objects.scene_depth_copy.Get(), texture.Get());
  return objects.scene_depth_srv.Get();
}

// This frame's occlusion from the scene's depth (`depth_view`; `msaa`: multisampled), camera and sun constants, with
// the compute stage's bindings put back after.
inline void Compute(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth_view, bool msaa, ID3D11Buffer* camera, ID3D11Buffer* lighting,
                    UINT sun_direction) {
  auto& objects = runtime.objects;
  // DLSS below 100%: the shadows' length (in pixels) x the render size's share, the same length on screen as at 100%;
  // Bend's per-pixel depth tolerances over it (a render pixel holds more of the scene: the depth steps between neighbours
  // and along the ray grow by 1 / share): its surface thickness, else thin occluders fall between the samples of every
  // other line, and its bilinear threshold, else slopes read as edges and shadow themselves (ATN debug19 at Ultra
  // Performance: combed fence shadows, striped hills).
  const auto render = frame_history::RenderSize(objects.width, objects.height);
  const float share = static_cast<float>(render[0]) / static_cast<float>(objects.width);
  const float samples = std::max(std::round(runtime.settings.samples * share), runtime.settings.hard_samples + 4.f);
  const ShaderSettings settings = {
      .width = static_cast<float>(objects.width),
      .height = static_cast<float>(objects.height),
      .inverse_width = 1.f / static_cast<float>(objects.width),
      .inverse_height = 1.f / static_cast<float>(objects.height),
      .thickness = runtime.settings.thickness / share,
      .samples = samples,
      .contrast = runtime.settings.contrast,
      .edge_threshold = runtime.settings.edge_threshold / share,
      .hard_samples = runtime.settings.hard_samples,
      // Within the samples left after the hard ones (Bend reads past them otherwise).
      .fade_samples = std::min(runtime.settings.fade_samples, samples - runtime.settings.hard_samples),
      .ignore_edges = (runtime.settings.ignore_edges ? 1.f : 0.f),
      .precision_offset = (runtime.settings.precision_offset ? 1.f : 0.f),
      .sample_along_ray = (runtime.settings.sample_along_ray ? 1.f : 0.f),
      .softness = runtime.settings.softness,
      .debug_view = static_cast<float>(runtime.settings.debug_view),
      .mirror_edges = (runtime.settings.mirror_edges ? 1.f : 0.f),
      .sun_direction = static_cast<float>(sun_direction),
      .render_width = static_cast<float>(render[0]),
      .render_height = static_cast<float>(render[1]),
      .foliage_light = runtime.settings.foliage_light,
  };
  if (std::memcmp(&settings, &objects.uploaded, sizeof(ShaderSettings)) != 0) {
    native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &settings, 0, 0);
    objects.uploaded = settings;
  }

  ComPtr<ID3D11ComputeShader> game_shader;
  native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  std::array<ID3D11ShaderResourceView*, 3> game_views = {};
  native->CSGetShaderResources(0, 3, game_views.data());
  std::array<ID3D11UnorderedAccessView*, 2> game_uavs = {};
  native->CSGetUnorderedAccessViews(0, 2, game_uavs.data());
  std::array<ID3D11Buffer*, 4> game_constants = {};
  native->CSGetConstantBuffers(0, 4, game_constants.data());
  ID3D11SamplerState* game_sampler = nullptr;
  native->CSGetSamplers(0, 1, &game_sampler);

  // The occlusion is written below: not bound for the pixel shaders meanwhile.
  ID3D11ShaderResourceView* no_view = nullptr;
  native->PSSetShaderResources(MASK_SLOT, 1, &no_view);
  constexpr UINT KEEP_COUNTS[2] = {UINT_MAX, UINT_MAX};
  // The unordered access views first: D3D11 drops a view of a resource still bound for writing (the last pass's
  // output), which had Bend read null parameters and depth (ATN debug7: one corner fully shadowed).
  const auto bind = [&](ID3D11ComputeShader* shader, std::array<ID3D11ShaderResourceView*, 2> views,
                        std::array<ID3D11UnorderedAccessView*, 2> uavs, std::array<ID3D11Buffer*, 4> constants) {
    native->CSSetShader(shader, nullptr, 0);
    native->CSSetUnorderedAccessViews(0, 2, uavs.data(), KEEP_COUNTS);
    native->CSSetShaderResources(0, 2, views.data());
    native->CSSetConstantBuffers(0, 4, constants.data());
  };
  // The depth (8 x 8 groups; with Light Through Foliage its foliage marked), the dispatch list, the occlusion cleared,
  // Bend's dispatches.
  const ComPtr<ID3D11ComputeShader>& depth_copy = (msaa ? objects.depth_copy_ms : objects.depth_copy);
  const bool foliage_marked = runtime.before_foliage_frame == runtime.frame && runtime.after_foliage_frame == runtime.frame;
  bind(depth_copy.Get(), {depth_view, foliage_marked ? objects.before_foliage_srv.Get() : nullptr}, {objects.depth_uav.Get(), nullptr},
       {nullptr, nullptr, objects.settings.Get(), nullptr});
  ID3D11ShaderResourceView* after_foliage = foliage_marked ? objects.after_foliage_srv.Get() : nullptr;
  native->CSSetShaderResources(2, 1, &after_foliage);
  native->Dispatch((objects.width + 7) / 8, (objects.height + 7) / 8, 1);
  bind(objects.setup.Get(), {}, {objects.arguments_uav.Get(), objects.parameters_uav.Get()},
       {camera, lighting, objects.settings.Get(), nullptr});
  native->Dispatch(1, 1, 1);
  constexpr FLOAT NO_OCCLUSION[4] = {0.f, 0.f, 0.f, 0.f};
  native->ClearUnorderedAccessViewFloat(objects.occlusion_uav.Get(), NO_OCCLUSION);
  bind(objects.bend.Get(), {objects.depth_srv.Get(), objects.parameters_srv.Get()}, {objects.occlusion_uav.Get(), nullptr},
       {nullptr, nullptr, objects.settings.Get(), nullptr});
  native->CSSetSamplers(0, 1, objects.border.GetAddressOf());
  for (UINT index = 0; index < DISPATCH_COUNT; ++index) {
    native->CSSetConstantBuffers(3, 1, objects.dispatch_indexes[index].GetAddressOf());
    native->DispatchIndirect(objects.arguments.Get(), index * 12);
  }
  if (runtime.settings.softness > 0.f) {
    bind(objects.blur.Get(), {objects.depth_srv.Get(), objects.occlusion_srv.Get()}, {objects.soft_uav.Get(), nullptr},
         {nullptr, nullptr, objects.settings.Get(), nullptr});
    native->Dispatch((objects.width + 7) / 8, (objects.height + 7) / 8, 1);
  }

  native->CSSetShader(game_shader.Get(), nullptr, 0);
  native->CSSetUnorderedAccessViews(0, 2, game_uavs.data(), KEEP_COUNTS);
  native->CSSetShaderResources(0, 3, game_views.data());
  native->CSSetConstantBuffers(0, 4, game_constants.data());
  native->CSSetSamplers(0, 1, &game_sampler);
  for (auto* view : game_views) {
    if (view != nullptr) view->Release();
  }
  for (auto* uav : game_uavs) {
    if (uav != nullptr) uav->Release();
  }
  for (auto* buffer : game_constants) {
    if (buffer != nullptr) buffer->Release();
  }
  if (game_sampler != nullptr) game_sampler->Release();
}

// The game creates a pipeline: its sun light shaders with the screen-space shadow (renodx keeps their original hash), its
// forward passes reading the sun's shadow map with t49 too.
inline bool OnCreatePipeline(reshade::api::device* device, reshade::api::pipeline_layout,
                             uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects) {
  if (device->get_api() != reshade::api::device_api::d3d11) return false;
  bool modified = false;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
    auto* desc = static_cast<reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const std::span<const uint8_t> code(static_cast<const uint8_t*>(desc->code), desc->code_size);
    // The game's hash, also of a sun shader the render scale patched first.
    const auto scaled = view_share::OriginalHash(desc->code);
    const uint32_t hash = scaled.value_or(renodx::utils::hash::ComputeCRC32(code.data(), code.size()));
    const std::unique_lock lock(runtime.mutex);
    const bool forward = std::ranges::find(runtime.forward_hashes, hash) != runtime.forward_hashes.end();
    if (!forward && std::ranges::find(runtime.sun_hashes, hash) == runtime.sun_hashes.end()) continue;
    auto [entry, added] = runtime.patched.try_emplace(hash);
    if (added) {
      entry->second = Patch(code, forward);
      std::stringstream s;
      s << "[Total War] Screen-space shadows: " << (forward ? "forward shader " : "sun shader ") << PRINT_CRC32(hash)
        << (entry->second.empty() ? " has no shadow to patch." : " patched.");
      // Its diffuse ambient for Path-Traced Ambient (on the shadow's patch, else on the game's code).
      bool foliage = false;
      const auto final_sites = runtime.ambient_final_sites.find(hash);
      const bool sites_known = final_sites != runtime.ambient_final_sites.end();
      std::vector<uint8_t> ambient = forward ? std::vector<uint8_t>() : PatchAmbient(entry->second.empty() ? code : std::span<const uint8_t>(entry->second), &foliage, sites_known ? final_sites->second : 0u);
      if (!ambient.empty()) {
        entry->second = std::move(ambient);
        runtime.ambient_patched[hash] = foliage;
        s << " Its diffuse ambient patched" << (foliage ? " (foliage)." : ".");
      } else if (sites_known) {
        runtime.ambient_not_as_expected = true;
        s << " Its diffuse ambient not as expected: Path-Traced Ambient stays off.";
      }
      reshade::log::message(reshade::log::level::info, s.str().c_str());
    }
    if (entry->second.empty()) continue;
    desc->code = entry->second.data();
    desc->code_size = entry->second.size();
    view_share::Repatched(code.data(), desc->code, hash);
    modified = true;
  }
  return modified;
}

}  // namespace internal

constexpr auto OnSunDraw = []<typename T, typename Context>(
                               Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&native);
    if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
    auto& runtime = internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    const auto& sun_draw = command.template GetCallbackData<T>();
    // The patched shaders' occlusion: this frame's on the main view, else none (reads 0: the game's light); with another
    // (extra_occlusion), the darker.
    ID3D11ShaderResourceView* occlusion = nullptr;
    const auto patched = runtime.patched.find(sun_draw.shader_hash);
    if ((runtime.settings.enabled || internal::extra_occlusion != nullptr) && patched != runtime.patched.end() && !patched->second.empty()
        && frame_history::MainViewport(native)) {
      ComPtr<ID3D11ShaderResourceView> depth_view;
      native->PSGetShaderResources(sun_draw.gbuffer_slot + 3, 1, depth_view.GetAddressOf());
      D3D11_SHADER_RESOURCE_VIEW_DESC depth_desc = {};
      ComPtr<ID3D11Resource> depth_resource;
      ComPtr<ID3D11Texture2D> depth_texture;
      D3D11_TEXTURE2D_DESC texture_desc = {};
      if (depth_view) {
        depth_view->GetDesc(&depth_desc);
        depth_view->GetResource(depth_resource.GetAddressOf());
      }
      if (depth_resource && SUCCEEDED(depth_resource.As(&depth_texture))) depth_texture->GetDesc(&texture_desc);
      if (texture_desc.Width == runtime.display_width && texture_desc.Height == runtime.display_height
          && texture_desc.Width != 0) {
        if (runtime.settings.enabled && runtime.computed_frame != runtime.frame) {
          ComPtr<ID3D11Device> device;
          native->GetDevice(device.GetAddressOf());
          std::array<ID3D11Buffer*, 1> camera = {};
          std::array<ID3D11Buffer*, 1> lighting = {};
          native->PSGetConstantBuffers(sun_draw.camera_slot, 1, camera.data());
          native->PSGetConstantBuffers(sun_draw.lighting_slot, 1, lighting.data());
          if (camera[0] != nullptr && lighting[0] != nullptr
              && internal::Prepare(device.Get(), texture_desc.Width, texture_desc.Height)) {
            // The scene's depth buffer, else the G-buffer's depth channel.
            ComPtr<ID3D11DepthStencilView> depth_stencil;
            native->OMGetRenderTargets(0, nullptr, depth_stencil.GetAddressOf());
            bool msaa = false;
            ID3D11ShaderResourceView* scene_depth = internal::SceneDepth(device.Get(), native, depth_stencil.Get(), &msaa);
            if (scene_depth == nullptr) {
              scene_depth = depth_view.Get();
              msaa = (depth_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DMS);
              if (!runtime.objects.logged_gbuffer_depth) {
                renodx::utils::log::i("[Total War] Screen-space shadows: the G-buffer's depth channel (format ",
                                      static_cast<int>(texture_desc.Format), ") instead.");
                runtime.objects.logged_gbuffer_depth = true;
              }
            }
            internal::Compute(native, scene_depth, msaa,
                              frame_history::AttilaCamera(device.Get(), native, camera[0], sun_draw.camera_layout, &runtime.objects.camera),
                              lighting[0], sun_draw.sun_direction);
            runtime.computed_frame = runtime.frame;
          }
          if (camera[0] != nullptr) camera[0]->Release();
          if (lighting[0] != nullptr) lighting[0]->Release();
        }
        if (runtime.settings.enabled && runtime.computed_frame == runtime.frame) {
          const auto& mask = (runtime.settings.softness > 0.f ? runtime.objects.soft_srv : runtime.objects.occlusion_srv);
          occlusion = mask.Get();
        }
        if (internal::extra_occlusion != nullptr) {
          if (auto* combined = internal::extra_occlusion(native, occlusion); combined != nullptr) occlusion = combined;
        }
        if (internal::smoke_occlusion != nullptr) {
          if (auto* smoked = internal::smoke_occlusion(native, occlusion, sun_draw, depth_view.Get()); smoked != nullptr) occlusion = smoked;
        }
      }
    }
    native->PSSetShaderResources(internal::MASK_SLOT, 1, &occlusion);
    return {};
  }
};

// Light Through Foliage: Bend's march has to know what casts a shadow, and neither the game's stencil nor its material byte
// tell foliage apart. Its depth pass follows the terrain's and comes before the rigid objects': the depth buffer copied at
// the frame's first foliage draw (`after` false) and at the first draw of the rest after it (`after` true) differs where it
// drew (sss_depth; branches among the leaves count as leaves).
constexpr auto OnFoliageOrder = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    auto& runtime = internal::runtime;
    const bool after = command.template GetCallbackData<T>();
    const std::unique_lock lock(runtime.mutex);
    if (!runtime.settings.enabled || runtime.settings.foliage_light <= 0.f
        || (after ? (runtime.before_foliage_frame != runtime.frame || runtime.after_foliage_frame == runtime.frame)
                  : runtime.before_foliage_frame == runtime.frame)) {
      return {};
    }
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&native);
    if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || !frame_history::MainViewport(native)) return {};
    ComPtr<ID3D11DepthStencilView> depth;
    native->OMGetRenderTargets(0, nullptr, depth.GetAddressOf());
    if (!depth) return {};
    ComPtr<ID3D11Resource> resource;
    depth->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) return {};
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    auto& objects = runtime.objects;
    auto& copy = after ? objects.after_foliage : objects.before_foliage;
    auto& view = after ? objects.after_foliage_srv : objects.before_foliage_srv;
    D3D11_TEXTURE2D_DESC held = {};
    if (copy) copy->GetDesc(&held);
    if ((held.Width != desc.Width || held.Height != desc.Height || held.SampleDesc.Count != desc.SampleDesc.Count)
        && !internal::MakeDepthCopy(device.Get(), desc, &copy, &view)) {
      return {};
    }
    native->CopyResource(copy.Get(), texture.Get());
    (after ? runtime.after_foliage_frame : runtime.before_foliage_frame) = runtime.frame;
    return {};
  }
};

// The foliage's vertex shaders and those of the draws after it (OnFoliageOrder; a game without them keeps every shadow
// whole).
inline void InstallFoliage(std::span<const uint32_t> foliage_shaders, std::span<const uint32_t> following_shaders) {
  static constexpr bool BEFORE = false;
  static constexpr bool AFTER = true;
  for (const uint32_t hash : foliage_shaders) {
    renodx::utils::command_action::Register(OnFoliageOrder, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &BEFORE);
  }
  for (const uint32_t hash : following_shaders) {
    renodx::utils::command_action::Register(OnFoliageOrder, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &AFTER);
  }
}

// Every present: the settings and the main view's size (the display's).
inline void Update(reshade::api::swapchain* swapchain, const Settings& settings) {
  reshade::api::resource_desc display = {};
  if (settings.enabled || internal::extra_occlusion != nullptr) {
    display = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
  }
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.settings = settings;
  internal::runtime.display_width = display.texture.width;
  internal::runtime.display_height = display.texture.height;
  // Off for about 10 s: its memory back (Prepare makes it again; the other occlusions use nothing of it). Not at once: a 4K copy
  // made again at each switch of what replaces it.
  internal::runtime.off_presents = settings.enabled ? 0 : internal::runtime.off_presents + 1;
  if (internal::runtime.off_presents > 600 && internal::runtime.objects.device) {
    internal::runtime.objects = {};
  }
  ++internal::runtime.frame;
}

// Whether the sun shader `hash` takes its diffuse ambient away at t51 (PatchAmbient; `foliage`: one of the foliage ones).
inline bool AmbientPatched(uint32_t hash, bool* foliage) {
  const std::unique_lock lock(internal::runtime.mutex);
  const auto found = internal::runtime.ambient_patched.find(hash);
  if (found == internal::runtime.ambient_patched.end()) return false;
  *foliage = found->second;
  return true;
}

// A sun shader with a known place for its ambient (SunDraw::ambient_final_sites) didn't have it: none is taken away.
inline bool AmbientNotAsExpected() {
  const std::shared_lock lock(internal::runtime.mutex);
  return internal::runtime.ambient_not_as_expected;
}

// Another occlusion of the sun's light from now on (null: none).
inline void SetExtraOcclusion(internal::ExtraOcclusion function) {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::extra_occlusion = function;
}

// The smoke's dimming from now on (null: none).
inline void SetSmokeOcclusion(internal::SmokeOcclusion function) {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::smoke_occlusion = function;
}

// The tone mapper drew (its replaced shaders' on_drawn, before the anti-aliasing): with a Debug View, its target replaced
// by this frame's occlusion (sss_debug), once a frame (not again at a later output pass), the compute stage's bindings
// put back after. DLSS then upscales it as it does the shadows in the picture (ATN debug20: stretched after it, the
// render pixels showed as blocks).
inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& objects = runtime.objects;
  if (runtime.settings.debug_view == 0 || runtime.computed_frame != runtime.frame || runtime.debug_frame == runtime.frame
      || !objects.debug) {
    return;
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target;
  if (FAILED(resource.As(&target))) return;
  D3D11_TEXTURE2D_DESC desc = {};
  target->GetDesc(&desc);
  if (desc.Width != objects.width || desc.Height != objects.height || desc.SampleDesc.Count != 1 || desc.ArraySize != 1
      || desc.MipLevels != 1
      || (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS)) {
    return;
  }
  if (!objects.debug_uav) {
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    D3D11_TEXTURE2D_DESC debug_desc = desc;
    debug_desc.Usage = D3D11_USAGE_DEFAULT;
    debug_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    debug_desc.CPUAccessFlags = 0;
    debug_desc.MiscFlags = 0;
    const D3D11_UNORDERED_ACCESS_VIEW_DESC view = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                   .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
    if (FAILED(device->CreateTexture2D(&debug_desc, nullptr, objects.debug_texture.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(objects.debug_texture.Get(), &view, objects.debug_uav.GetAddressOf()))) {
      objects.debug_texture.Reset();
      objects.debug_uav.Reset();
      return;
    }
  }

  ComPtr<ID3D11ComputeShader> game_shader;
  native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  std::array<ID3D11ShaderResourceView*, 2> game_views = {};
  native->CSGetShaderResources(0, 2, game_views.data());
  ID3D11UnorderedAccessView* game_uav = nullptr;
  native->CSGetUnorderedAccessViews(0, 1, &game_uav);
  ID3D11Buffer* game_constants = nullptr;
  native->CSGetConstantBuffers(2, 1, &game_constants);

  const auto& mask = (runtime.settings.softness > 0.f ? objects.soft_srv : objects.occlusion_srv);
  const std::array<ID3D11ShaderResourceView*, 2> views = {mask.Get(), objects.parameters_srv.Get()};
  constexpr UINT KEEP_COUNT = UINT_MAX;
  native->CSSetShader(objects.debug.Get(), nullptr, 0);
  native->CSSetUnorderedAccessViews(0, 1, objects.debug_uav.GetAddressOf(), &KEEP_COUNT);
  native->CSSetShaderResources(0, 2, views.data());
  native->CSSetConstantBuffers(2, 1, objects.settings.GetAddressOf());
  native->Dispatch((objects.width + 7) / 8, (objects.height + 7) / 8, 1);

  native->CSSetShader(game_shader.Get(), nullptr, 0);
  native->CSSetUnorderedAccessViews(0, 1, &game_uav, &KEEP_COUNT);
  native->CSSetShaderResources(0, 2, game_views.data());
  native->CSSetConstantBuffers(2, 1, &game_constants);
  for (auto* view : game_views) {
    if (view != nullptr) view->Release();
  }
  if (game_uav != nullptr) game_uav->Release();
  if (game_constants != nullptr) game_constants->Release();
  native->CopyResource(target.Get(), objects.debug_texture.Get());
  runtime.debug_frame = runtime.frame;
}

// The game's sun light draws (`draws`: the addon's, kept for as long as it is installed; the Light Bounce's list), their
// shaders patched as the game creates them. Before light_bounce::Install (its callback draws again).
inline void Install(std::span<const light_bounce::SunDraw> draws) {
  {
    const std::unique_lock lock(internal::runtime.mutex);
    for (const auto& draw : draws) {
      internal::runtime.sun_hashes.push_back(draw.shader_hash);
      if (draw.ambient_final_sites != 0) internal::runtime.ambient_final_sites[draw.shader_hash] = draw.ambient_final_sites;
    }
  }
  renodx::utils::pipeline::RegisterOnCreateCallback(internal::OnCreatePipeline);
  for (const auto& draw : draws) {
    renodx::utils::command_action::Register(
        OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &draw);
  }
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.objects.device.Get() != native) return;
  internal::runtime.objects = {};
}

// The game's forward passes that read the sun's shadow map (`hashes`), patched as they are created to take its
// occlusion at t49 too (each comparison x (1 - t49 at the pixel); t49 unbound reads 0: the game's shadow); their draws
// bind it themselves (Attila: rt_scene::InstallShadowReaders, under Ray-Traced Shadows Only). After Install.
inline void PatchForwardShadows(std::span<const uint32_t> hashes) {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.forward_hashes.insert(internal::runtime.forward_hashes.end(), hashes.begin(), hashes.end());
}

inline void Uninstall() {
  renodx::utils::pipeline::UnregisterOnCreateCallback(internal::OnCreatePipeline);
  renodx::utils::command_action::Unregister(OnSunDraw);
  renodx::utils::command_action::Unregister(OnFoliageOrder);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.objects = {};
}

}  // namespace twattila::screen_space_shadows
