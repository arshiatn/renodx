/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <vector>

#include "../twattila/dxbc.hpp"

// Fog Strength on the fog's last pass, every permutation the game ships (dx11_battlefog's upsample, low spec, campaign fog
// and new fog in their shadow-quality and 4.1 versions, the legacy fog): the game's pixel shader as it creates it, its
// outputs at its one `ret` scaled by the strength above one at cb8[0].x (0: every output bit as the game's; the addon binds it
// for the draw). No WARP-free part: tools\tw\warp_tests\pharaoh\fog_v54 (strength mode) runs every hash here.
namespace fog_strength {

enum class Kind : uint8_t {
  TRANSMITTANCE_TARGET,  // o0.xyz the in-scattering, o1.xyz the transmittance (the scene x o1 + o0)
  TRANSMITTANCE_ALPHA,   // o0.xyz the in-scattering, o0.w the transmittance (the scene x o0.w + o0.xyz)
  OPACITY_ALPHA,         // o0.xyz the fog's colour, o0.w its opacity (blended over the scene)
};

struct Shader {
  uint32_t hash = 0;
  Kind kind = Kind::TRANSMITTANCE_TARGET;
};

// Read from each one's last writes (fxc /dumpbin): the upsamples' and low spec's fall-back writes o1 = 1 where o0 is not
// finite (no fog); campaign fog's other branch o0.w = T, o0.xyz = (1 - T) x the colour; new fog's (0, 0, 0, 1) outside it;
// the legacy fog's o0.w = smoothstep x g_fog_lerp over the sky cube's colour.
constexpr std::array<Shader, 24> SHADERS = {{
    {0x8D0D7355, Kind::TRANSMITTANCE_TARGET},  // upsample
    {0xA38C97C7, Kind::TRANSMITTANCE_TARGET},
    {0xDE400C41, Kind::TRANSMITTANCE_TARGET},  // upsample_low_spec
    {0x18CBFFE5, Kind::TRANSMITTANCE_TARGET},
    {0x27A2082F, Kind::TRANSMITTANCE_TARGET},  // upsample_ms
    {0x28FFE1E6, Kind::TRANSMITTANCE_TARGET},
    {0xD08EA707, Kind::TRANSMITTANCE_TARGET},  // upsample_ms2
    {0xDF8EC249, Kind::TRANSMITTANCE_TARGET},
    {0x7E7BE619, Kind::TRANSMITTANCE_TARGET},  // main_low_spec
    {0xCC1B200A, Kind::TRANSMITTANCE_TARGET},
    {0x13A8B466, Kind::TRANSMITTANCE_ALPHA},  // campaign_fog
    {0xEE118C5F, Kind::TRANSMITTANCE_ALPHA},
    {0xEFCDBBB5, Kind::TRANSMITTANCE_ALPHA},  // campaign_fog_low
    {0xDC0169D1, Kind::TRANSMITTANCE_ALPHA},
    {0x88A43294, Kind::TRANSMITTANCE_ALPHA},  // campaign_fog_ms
    {0x7268CCBF, Kind::TRANSMITTANCE_ALPHA},
    {0xF8024BAF, Kind::TRANSMITTANCE_ALPHA},  // campaign_fog_ms_low
    {0xAA6A41D2, Kind::TRANSMITTANCE_ALPHA},
    {0xC42BE764, Kind::TRANSMITTANCE_ALPHA},  // main_new_fog
    {0x2349AA2B, Kind::TRANSMITTANCE_ALPHA},
    {0xB9671779, Kind::TRANSMITTANCE_ALPHA},  // main_new_fog_ms
    {0x906F52CF, Kind::TRANSMITTANCE_ALPHA},
    {0xD9F9A6C9, Kind::OPACITY_ALPHA},  // dx11_battlefog main (legacy)
    {0x51EC7FE0, Kind::OPACITY_ALPHA},  // dx11_legacybattlefog main_ms
}};

constexpr uint32_t SLOT = 8;

// `code` (a shader model 4 / 5 pixel shader with one `ret`, last, and no cb8) with its outputs o0 / o1 written into three
// temporaries more and, at the `ret`, with a = cb8[0].x and s = 1 + a: the in-scattering x s, the transmittance T x s - a
// (at least 0 when a > 0), an opacity x s up to 1 (or its own when above); each output its own value when a is 0 (a select,
// bit for bit). Empty: not patched.
inline std::vector<uint8_t> Patch(std::span<const uint8_t> code, Kind kind) {
  namespace dxbc = twattila::dxbc;
  constexpr uint32_t TEMP = 0;
  constexpr uint32_t OUTPUT = 2;
  constexpr uint32_t CONSTANT_BUFFER = 8;
  constexpr uint32_t OP_LT = 49;
  constexpr uint32_t OP_MAD = 50;
  constexpr uint32_t OP_MIN = 51;
  constexpr uint32_t OP_MAX = 52;
  constexpr uint32_t OP_MOV = 54;
  constexpr uint32_t OP_MOVC = 55;
  constexpr uint32_t OP_MUL = 56;
  constexpr uint32_t OP_NE = 57;
  constexpr uint32_t OP_RET = 62;
  constexpr uint32_t OP_RETC = 63;
  constexpr uint32_t OP_DCL_CONSTANT_BUFFER = 89;
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty() || (tokens[0] >> 16) != 0) return {};
  size_t temps_at = 0;
  const size_t first = dxbc::internal::FirstInstruction(tokens, &temps_at);
  if (first == 0 || temps_at == 0) return {};
  for (size_t at = 2; at < first; at += dxbc::Length(tokens, at)) {
    if ((tokens[at] & 0x7FF) == OP_DCL_CONSTANT_BUFFER && tokens[at + 2] == SLOT) return {};
  }
  const uint32_t game = tokens[temps_at];  // its outputs' copies, s and the select, a work register
  const uint32_t o0 = game, o1 = game + 1, s = game + 2, work = game + 3;
  size_t ret_at = 0;
  for (size_t at = first; at < tokens.size();) {
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_RETC || (opcode == OP_RET && at + length != tokens.size())) return {};
    if (opcode == OP_RET) ret_at = at;
    if (opcode != dxbc::OP_CUSTOMDATA) {
      for (size_t operand_at = dxbc::internal::FirstOperand(tokens, at); operand_at < at + length;) {
        dxbc::Operand operand;
        const size_t operand_length = dxbc::ParseOperand(tokens, operand_at, at + length, &operand);
        if (operand_length == 0) return {};
        if (operand.type == OUTPUT) {
          if (((tokens[operand_at] >> 20) & 3) != 1 || operand.relative || operand.index[0] > 1) return {};
          tokens[operand_at] &= ~(0xFFu << 12);
          tokens[operand_at + operand_length - 1] = game + operand.index[0];
        }
        operand_at += operand_length;
      }
    }
    at += length;
  }
  if (ret_at == 0) return {};

  // Operands: a register's components (mask or swizzle), cb8[0] (x, negated), immediates.
  constexpr uint32_t X = 1, Y = 2, Z = 4, W = 8, XYZ = 7;
  constexpr uint32_t XXXX = 0x00, YYYY = 0x55, ZZZZ = 0xAA, WWWW = 0xFF, XYZW = 0xE4;
  std::vector<uint32_t> added;
  const auto instruction = [&added](uint32_t opcode, std::initializer_list<std::vector<uint32_t>> operands) {
    uint32_t length = 1;
    for (const auto& operand : operands) length += static_cast<uint32_t>(operand.size());
    added.push_back(opcode | (length << 24));
    for (const auto& operand : operands) added.insert(added.end(), operand.begin(), operand.end());
  };
  const auto to = [](uint32_t type, uint32_t index, uint32_t mask) -> std::vector<uint32_t> {
    return {2u | (mask << 4) | (type << 12) | (1u << 20), index};
  };
  const auto from = [](uint32_t index, uint32_t swizzle) -> std::vector<uint32_t> {
    return {2u | (1u << 2) | (swizzle << 4) | (TEMP << 12) | (1u << 20), index};
  };
  const auto above_one = [](bool negated) -> std::vector<uint32_t> {
    const uint32_t token = 2u | (1u << 2) | (XXXX << 4) | (CONSTANT_BUFFER << 12) | (2u << 20);
    if (negated) return {token | (1u << 31), 1u | (1u << 6), SLOT, 0};
    return {token, SLOT, 0};
  };
  const auto value = [](float number) -> std::vector<uint32_t> {
    uint32_t bits = 0;
    std::memcpy(&bits, &number, 4);
    return {2u | (dxbc::OPERAND_IMMEDIATE << 12), bits, bits, bits, bits};
  };
  // s.x = 1 + a, s.y = (a != 0), s.z = (a > 0).
  instruction(dxbc::OP_ADD, {to(TEMP, s, X), above_one(false), value(1.f)});
  instruction(OP_NE, {to(TEMP, s, Y), above_one(false), value(0.f)});
  instruction(OP_LT, {to(TEMP, s, Z), value(0.f), above_one(false)});
  if (kind == Kind::OPACITY_ALPHA) {
    instruction(OP_MOV, {to(OUTPUT, 0, XYZ), from(o0, XYZW)});
    instruction(OP_MUL, {to(TEMP, work, W), from(o0, WWWW), from(s, XXXX)});
    instruction(OP_MAX, {to(TEMP, o1, W), from(o0, WWWW), value(1.f)});
    instruction(OP_MIN, {to(TEMP, work, W), from(work, WWWW), from(o1, WWWW)});
    instruction(OP_MOVC, {to(OUTPUT, 0, W), from(s, YYYY), from(work, WWWW), from(o0, WWWW)});
  } else {
    instruction(OP_MUL, {to(TEMP, work, XYZ), from(o0, XYZW), from(s, XXXX)});
    instruction(OP_MOVC, {to(OUTPUT, 0, XYZ), from(s, YYYY), from(work, XYZW), from(o0, XYZW)});
    const bool target = kind == Kind::TRANSMITTANCE_TARGET;
    const uint32_t transmittance = target ? o1 : o0;
    const uint32_t mask = target ? XYZ : W;
    const uint32_t read = target ? XYZW : WWWW;
    const uint32_t spare = target ? o0 : o1;  // o0's copy is read already; o1's is unused without a second target
    instruction(OP_MAD, {to(TEMP, work, mask), from(transmittance, read), from(s, XXXX), above_one(true)});
    instruction(OP_MAX, {to(TEMP, spare, mask), from(work, read), value(0.f)});
    instruction(OP_MOVC, {to(TEMP, work, mask), from(s, ZZZZ), from(spare, read), from(work, read)});
    instruction(OP_MOVC, {to(OUTPUT, target ? 1 : 0, mask), from(s, YYYY), from(work, read), from(transmittance, read)});
    if (target) {
      instruction(OP_MOV, {to(OUTPUT, 0, W), from(o0, WWWW)});
      instruction(OP_MOV, {to(OUTPUT, 1, W), from(o1, WWWW)});
    }
  }
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(ret_at), added.begin(), added.end());
  tokens[temps_at] = game + 4;
  // dcl_constantbuffer cb8[1], immediateIndexed, before dcl_temps.
  const std::array<uint32_t, 4> declaration = {OP_DCL_CONSTANT_BUFFER | (4u << 24),
                                               2u | (1u << 2) | (XYZW << 4) | (CONSTANT_BUFFER << 12) | (2u << 20), SLOT, 1};
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at - 1), declaration.begin(), declaration.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return dxbc::Write(code, program);
}

}  // namespace fog_strength
