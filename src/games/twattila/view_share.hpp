/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/format.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/log.hpp"
#include "../../utils/pipeline.hpp"
#include "../../utils/state.hpp"
#include "./dxbc.hpp"
#include "./frame_history.hpp"

// The DLSS render scale draws the 3D image into the top-left part of the game's display-size textures. The engine
// reads them right (uv = (vpos + offset) x g_screen_size.zw: the texture's), but many of its shaders then take that uv
// for the view's (as if the view filled the texture): positions rebuilt from the depth (NDC = uv x (2, -2) + (-1, 1)) in
// point lights, decals, fog, water, particles, its AO; and uvs projected back from clip space (x (0.5, -0.5) + 0.5)
// read their textures as if the view filled them. Every pixel shader the game creates is checked (its bytecode):
// - an NDC built from a texture uv (the uv's last writer, followed back, reads camera_VS_PS's g_screen_size or
//   g_render_target_dimensions): the uv first divided by the view's share of the target;
// - a projected uv that a texture read then takes as its coordinate: multiplied by that share.
// The share is cb12 (render_scale: xy the draw's viewport / target, zw the frame's render / display size), bound for
// these shaders' draws while the frame is scaled; unbound it reads 0, which the patch takes as 1 (no change at 100%).
// The addon's own replaced shaders that declare cbuffer render_scale at b12 get it too (render_scale.hlsli).
namespace twattila::view_share {

namespace internal {

using Microsoft::WRL::ComPtr;

constexpr uint32_t SLOT = 12;
constexpr uint32_t OP_DIV = 14, OP_EQ = 24, OP_FTOU = 28, OP_CUSTOMDATA = 53, OP_MAD = 50, OP_MOV = 54, OP_MOVC = 55,
                   OP_MUL = 56, OP_UTOF = 86;
constexpr uint32_t OP_LD = 45, OP_SAMPLE = 69, OP_SAMPLE_C = 70, OP_SAMPLE_C_LZ = 71, OP_SAMPLE_L = 72, OP_SAMPLE_D = 73,
                   OP_SAMPLE_B = 74, OP_GATHER4 = 109, OP_GATHER4_C = 126, OP_GATHER4_PO = 127, OP_GATHER4_PO_C = 128;
constexpr uint32_t OP_DCL_RESOURCE = 88, OP_DCL_CONSTANT_BUFFER = 89, OP_DCL_TEMPS = 104, OP_DCL_GLOBAL_FLAGS = 106;
constexpr uint32_t TYPE_TEMP = 0, TYPE_INPUT = 1, TYPE_IMMEDIATE = 4, TYPE_CONSTANT_BUFFER = 8;
constexpr uint32_t CUSTOMDATA_IMMEDIATE_CONSTANT_BUFFER = 3;
constexpr uint32_t TWO = 0x40000000, MINUS_TWO = 0xC0000000, FOUR = 0x40800000, MINUS_FOUR = 0xC0800000, ONE = 0x3F800000,
                   MINUS_ONE = 0xBF800000, HALF = 0x3F000000, MINUS_HALF = 0xBF000000;

struct Operand {
  size_t at = 0;
  size_t length = 0;
  uint32_t type = 0;
  uint32_t mode = 0;    // 0 mask, 1 swizzle, 2 one component
  uint32_t select = 0;  // its mask, swizzle or component
  bool modified = false;
  bool relative = false;
  std::array<uint32_t, 2> index = {};
  std::array<uint32_t, 4> values = {};
};

struct Instruction {
  size_t at = 0;
  size_t length = 0;
  uint32_t opcode = 0;
  std::vector<Operand> operands;
};

// An operand at `at` (before `end`); its length 0: malformed.
inline size_t ParseOperand(const std::vector<uint32_t>& tokens, size_t at, size_t end, Operand* operand) {
  if (at >= end) return 0;
  const uint32_t token = tokens[at];
  size_t next = at + 1;
  operand->at = at;
  operand->type = (token >> 12) & 0xFF;
  operand->mode = (token >> 2) & 3;
  operand->select = (token >> 4) & 0xFF;
  if ((token >> 31) != 0) {
    uint32_t extended = 0;
    do {
      if (next >= end) return 0;
      extended = tokens[next++];
      // A modifier (type 1) other than none.
      if ((extended & 0x3F) == 1 && ((extended >> 6) & 0xFF) != 0) operand->modified = true;
    } while ((extended >> 31) != 0);
  }
  if (operand->type == TYPE_IMMEDIATE) {
    const size_t count = ((token & 3) == 1) ? 1 : 4;
    if (next + count > end) return 0;
    for (size_t component = 0; component < count; ++component) operand->values[component] = tokens[next + component];
    next += count;
  }
  const uint32_t dimensions = (token >> 20) & 3;
  for (uint32_t dimension = 0; dimension < dimensions; ++dimension) {
    const uint32_t representation = (token >> (22 + 3 * dimension)) & 7;
    if (representation == 0 || representation == 3) {
      if (next >= end) return 0;
      if (dimension < 2) operand->index[dimension] = tokens[next];
      ++next;
    } else if (representation == 1 || representation == 4) {
      next += 2;
    }
    if (representation >= 2) {
      operand->relative = true;
      Operand nested;
      const size_t nested_length = ParseOperand(tokens, next, end, &nested);
      if (nested_length == 0) return 0;
      next += nested_length;
    }
  }
  if (next > end) return 0;
  operand->length = next - at;
  return operand->length;
}

inline bool IsDeclaration(uint32_t opcode, uint32_t token) {
  return (opcode >= OP_DCL_RESOURCE && opcode <= OP_DCL_GLOBAL_FLAGS) || (opcode >= 143 && opcode <= 162)
         || (opcode == OP_CUSTOMDATA && (token >> 11) == CUSTOMDATA_IMMEDIATE_CONSTANT_BUFFER);
}

// The camera's registers in a shader (RDEF: camera_VS_PS's slot, g_screen_size / g_render_target_dimensions /
// g_viewport_dimensions), whether it declares render_scale at b12 (the addon's shaders) or any other b12.
struct Reflection {
  uint32_t camera_slot = UINT32_MAX;
  uint32_t screen = UINT32_MAX;
  uint32_t target = UINT32_MAX;
  uint32_t viewport = UINT32_MAX;
  uint32_t view_projection = UINT32_MAX;  // its first register (4 in all)
  uint32_t projection = UINT32_MAX;
  bool render_scale = false;
  bool slot_taken = false;
};

inline Reflection Reflect(std::span<const uint8_t> code, const dxbc::Program& program) {
  Reflection reflection;
  const auto read = [&](size_t at) {
    uint32_t value = 0;
    if (at + 4 <= code.size()) std::memcpy(&value, code.data() + at, 4);
    return value;
  };
  const auto name_is = [&](size_t at, const char* name) {
    const size_t length = std::strlen(name) + 1;
    return at + length <= code.size() && std::memcmp(code.data() + at, name, length) == 0;
  };
  for (const uint32_t offset : program.offsets) {
    if (offset + 8 > code.size() || std::memcmp(code.data() + offset, "RDEF", 4) != 0) continue;
    const size_t data = offset + 8;
    const uint32_t buffers = read(data);
    const size_t buffer_at = data + read(data + 4);
    const uint32_t bindings = read(data + 8);
    const size_t binding_at = data + read(data + 12);
    const uint32_t major = (read(data + 16) >> 8) & 0xFF;
    const size_t variable_size = (major >= 5) ? 40 : 24;
    for (uint32_t index = 0; index < bindings; ++index) {
      const size_t binding = binding_at + 32 * size_t{index};
      if (read(binding + 4) != 0) continue;  // constant buffers
      const size_t name = data + read(binding);
      const uint32_t slot = read(binding + 20);
      // camera_VS_PS (Attila, Rome II, Thrones), camera (Troy, Pharaoh, Warhammer).
      if (name_is(name, "camera_VS_PS") || name_is(name, "camera")) reflection.camera_slot = slot;
      if (slot == SLOT) {
        if (name_is(name, "render_scale")) {
          reflection.render_scale = true;
        } else {
          reflection.slot_taken = true;
        }
      }
    }
    for (uint32_t index = 0; index < buffers; ++index) {
      const size_t buffer = buffer_at + 24 * size_t{index};
      if (!name_is(data + read(buffer), "camera_VS_PS") && !name_is(data + read(buffer), "camera")) continue;
      const uint32_t variables = read(buffer + 4);
      const size_t variable_at = data + read(buffer + 8);
      for (uint32_t variable = 0; variable < variables; ++variable) {
        const size_t entry = variable_at + variable_size * variable;
        const size_t name = data + read(entry);
        const uint32_t start = read(entry + 4) / 16;
        if (name_is(name, "g_screen_size")) reflection.screen = start;
        if (name_is(name, "g_render_target_dimensions")) reflection.target = start;
        if (name_is(name, "g_viewport_dimensions")) reflection.viewport = start;
        if (name_is(name, "view_projection")) reflection.view_projection = start;
        if (name_is(name, "projection")) reflection.projection = start;
      }
    }
  }
  return reflection;
}

enum class Source { UNKNOWN, TEXTURE, VIEW, CAMERA_PROJECTION };

// What a camera register is: the texture's size, the viewport's, the camera's (view-)projection.
inline Source CameraRegister(const Reflection& reflection, uint32_t reg) {
  if (reg == reflection.viewport) return Source::VIEW;
  if (reg == reflection.screen || reg == reflection.target) return Source::TEXTURE;
  if ((reflection.view_projection != UINT32_MAX && reg >= reflection.view_projection && reg < reflection.view_projection + 4)
      || (reflection.projection != UINT32_MAX && reg >= reflection.projection && reg < reflection.projection + 4)) {
    return Source::CAMERA_PROJECTION;
  }
  return Source::UNKNOWN;
}

// The component a source operand reads for its instruction's component `component`.
inline uint32_t SourceComponent(const Operand& operand, uint32_t component) {
  if (operand.mode == 1) return (operand.select >> (2 * component)) & 3;
  if (operand.mode == 2) return operand.select & 3;
  return component;
}

// Where temp `reg`'s `components` (mask) were last made before instruction `index`: from the camera's texture size
// (TEXTURE), its viewport (VIEW) or unknown.
inline Source Classify(const std::vector<Instruction>& code, size_t index, uint32_t reg, uint32_t components,
                       const Reflection& reflection, int depth = 0) {
  if (depth > 8) return Source::UNKNOWN;
  for (size_t at = index; at-- > 0;) {
    const Instruction& instruction = code[at];
    if (instruction.operands.empty()) continue;
    const Operand& destination = instruction.operands[0];
    if (destination.type != TYPE_TEMP || destination.mode != 0 || destination.relative || destination.index[0] != reg
        || (destination.select & components) == 0) {
      continue;
    }
    // Its sources read for those components.
    for (size_t source = 1; source < instruction.operands.size(); ++source) {
      const Operand& operand = instruction.operands[source];
      if (operand.type == TYPE_CONSTANT_BUFFER && !operand.relative && operand.index[0] == reflection.camera_slot) {
        if (const Source found = CameraRegister(reflection, operand.index[1]); found != Source::UNKNOWN) return found;
      }
    }
    for (size_t source = 1; source < instruction.operands.size(); ++source) {
      const Operand& operand = instruction.operands[source];
      if (operand.type == TYPE_INPUT) return Source::UNKNOWN;
      if (operand.type != TYPE_TEMP || operand.relative) continue;
      uint32_t read = 0;
      for (uint32_t component = 0; component < 4; ++component) {
        if ((destination.select & components & (1u << component)) != 0) read |= 1u << SourceComponent(operand, component);
      }
      const Source found = Classify(code, at, operand.index[0], read, reflection, depth + 1);
      if (found != Source::UNKNOWN) return found;
    }
    return Source::UNKNOWN;
  }
  return Source::UNKNOWN;
}

// A mad whose two written components take immediates (`a0`, `a1`) x and (`b0`, `b1`) +: its first two components
// (`more`: it may write others too).
inline bool MadWith(const Instruction& instruction, uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1,
                    std::array<uint32_t, 2>* components, bool more = false) {
  if (instruction.opcode != OP_MAD || instruction.operands.size() != 4) return false;
  const Operand& destination = instruction.operands[0];
  const Operand& scale = instruction.operands[2];
  const Operand& offset = instruction.operands[3];
  if (destination.type != TYPE_TEMP || destination.mode != 0 || destination.relative || scale.type != TYPE_IMMEDIATE
      || offset.type != TYPE_IMMEDIATE) {
    return false;
  }
  uint32_t found = 0;
  for (uint32_t component = 0; component < 4 && found < 2; ++component) {
    if ((destination.select & (1u << component)) != 0) (*components)[found++] = component;
  }
  if (found != 2 || (!more && (destination.select & ~((1u << (*components)[0]) | (1u << (*components)[1]))) != 0)) return false;
  return scale.values[(*components)[0]] == a0 && scale.values[(*components)[1]] == a1 && offset.values[(*components)[0]] == b0
         && offset.values[(*components)[1]] == b1;
}

// Whether temp `reg`'s `components` written at `index` are read as a texture read's coordinate before being written
// again.
inline bool ReadAsCoordinate(const std::vector<Instruction>& code, size_t index, uint32_t reg, uint32_t components) {
  for (size_t at = index + 1; at < code.size(); ++at) {
    const Instruction& instruction = code[at];
    switch (instruction.opcode) {
      case OP_LD: case OP_SAMPLE: case OP_SAMPLE_C: case OP_SAMPLE_C_LZ: case OP_SAMPLE_L: case OP_SAMPLE_D: case OP_SAMPLE_B:
      case OP_GATHER4: case OP_GATHER4_C: case OP_GATHER4_PO: case OP_GATHER4_PO_C:
        if (instruction.operands.size() > 1 && instruction.operands[1].type == TYPE_TEMP && instruction.operands[1].index[0] == reg) {
          uint32_t read = 0;
          for (uint32_t component = 0; component < 2; ++component) read |= 1u << SourceComponent(instruction.operands[1], component);
          if ((read & components) != 0) return true;
        }
        break;
      default:
        break;
    }
    if (!instruction.operands.empty()) {
      const Operand& destination = instruction.operands[0];
      if (destination.type == TYPE_TEMP && destination.mode == 0 && destination.index[0] == reg
          && (destination.select & components) == components) {
        return false;
      }
    }
  }
  return false;
}

// The patched code (empty: nothing to patch) and what was patched, for the log.
struct Patched {
  std::vector<uint8_t> code;
  uint32_t ndc = 0;
  uint32_t projected = 0;
  uint32_t region = 0;
};

// `region_slots`: the texture slots (bit per slot) whose reads take the frame's render share (render-space textures
// read by a pass at the display's size: after DLSS, or a full-size pass).
inline Patched Patch(std::span<const uint8_t> code, const Reflection& reflection, uint64_t region_slots = 0) {
  Patched result;
  if (reflection.slot_taken) return result;
  const bool camera = reflection.camera_slot != UINT32_MAX && reflection.screen != UINT32_MAX;
  if (!camera && region_slots == 0) return result;
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return result;
  // A compute shader has no rasterizer: its screen positions are all texture coordinates.
  const bool compute = (tokens[0] >> 16) == 5;

  std::vector<Instruction> instructions;
  size_t temps_at = 0;
  size_t code_at = 0;
  uint64_t two_dimensional = 0;  // texture2d slots (dcl_resource)
  for (size_t at = 2; at < tokens.size();) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return result;
    if (IsDeclaration(opcode, tokens[at])) {
      if (opcode == OP_DCL_TEMPS) temps_at = at;
      // dcl_resource_texture2d: dimension 3, its slot the operand's index.
      if (opcode == OP_DCL_RESOURCE && ((tokens[at] >> 11) & 0x1F) == 3 && length >= 4 && tokens[at + 2] < 64) {
        two_dimensional |= uint64_t{1} << tokens[at + 2];
      }
      code_at = at + length;
      at += length;
      continue;
    }
    Instruction instruction = {.at = at, .length = length, .opcode = opcode};
    if (opcode != OP_CUSTOMDATA) {
      size_t next = at + 1;
      if ((tokens[at] >> 31) != 0) {
        while (next < at + length && (tokens[next] >> 31) != 0) ++next;
        ++next;
      }
      while (next < at + length) {
        Operand operand;
        const size_t operand_length = ParseOperand(tokens, next, at + length, &operand);
        if (operand_length == 0) return result;  // an unknown form: left as it is
        instruction.operands.push_back(operand);
        next += operand_length;
      }
    }
    instructions.push_back(std::move(instruction));
    at += length;
  }
  if (temps_at == 0) return result;

  enum class Kind { NDC, PROJECTED, REGION };
  struct Site {
    size_t instruction = 0;
    Kind kind = Kind::NDC;
    std::array<uint32_t, 2> components = {};
  };
  std::vector<Site> sites;
  for (size_t index = 0; index < instructions.size(); ++index) {
    const Instruction& instruction = instructions[index];
    std::array<uint32_t, 2> components = {};
    // Its uv to NDC (x 4: a half-size pass's uv of the full size).
    if (camera && (MadWith(instruction, TWO, MINUS_TWO, MINUS_ONE, ONE, &components)
                   || MadWith(instruction, FOUR, MINUS_FOUR, MINUS_ONE, ONE, &components))) {
      const Operand& source = instruction.operands[1];
      if (source.type != TYPE_TEMP || source.relative || source.mode != 1) continue;
      const uint32_t read = (1u << SourceComponent(source, components[0])) | (1u << SourceComponent(source, components[1]));
      if (Classify(instructions, index, source.index[0], read, reflection) == Source::TEXTURE) {
        sites.push_back({.instruction = index, .kind = Kind::NDC, .components = components});
      }
    } else if (camera && MadWith(instruction, HALF, MINUS_HALF, HALF, HALF, &components, true)) {
      const Operand& destination = instruction.operands[0];
      const Operand& source = instruction.operands[1];
      // A screen position (the camera's projection) read as a coordinate; not another projection (the campaign's
      // overlay, decals' own).
      if (source.type != TYPE_TEMP || source.relative) continue;
      const uint32_t read = (1u << SourceComponent(source, components[0])) | (1u << SourceComponent(source, components[1]));
      if (Classify(instructions, index, source.index[0], read, reflection) == Source::CAMERA_PROJECTION
          && (compute || ReadAsCoordinate(instructions, index, destination.index[0], (1u << components[0]) | (1u << components[1])))) {
        sites.push_back({.instruction = index, .kind = Kind::PROJECTED, .components = components});
      }
    } else if (region_slots != 0) {
      switch (instruction.opcode) {
        case OP_LD: case OP_SAMPLE: case OP_SAMPLE_C: case OP_SAMPLE_C_LZ: case OP_SAMPLE_L: case OP_SAMPLE_D:
        case OP_SAMPLE_B: case OP_GATHER4: case OP_GATHER4_C: {
          if (instruction.operands.size() < 3) break;
          const Operand& address = instruction.operands[1];
          const Operand& resource = instruction.operands[2];
          if (resource.type != 7 || resource.index[0] >= 64 || address.relative || address.modified) break;
          const uint64_t slot = uint64_t{1} << resource.index[0];
          if ((region_slots & slot) == 0 || (two_dimensional & slot) == 0) break;
          sites.push_back({.instruction = index, .kind = Kind::REGION});
          break;
        }
        default:
          break;
      }
    }
  }
  if (sites.empty()) return result;

  // Two more temps: rS the share (cb12, 0 -> 1: xy the draw's, zw the frame's) and rU a site's coordinate.
  const uint32_t share = tokens[temps_at + 1];
  const uint32_t uv = share + 1;
  const auto masked = [](uint32_t index, uint32_t mask) {
    return std::vector<uint32_t>{2u | (mask << 4) | (TYPE_TEMP << 12) | (1u << 20), index};
  };
  const auto swizzled = [](uint32_t index, uint32_t swizzle) {
    return std::vector<uint32_t>{2u | (1u << 2) | (swizzle << 4) | (TYPE_TEMP << 12) | (1u << 20), index};
  };
  const auto constant = [](uint32_t swizzle) {
    return std::vector<uint32_t>{2u | (1u << 2) | (swizzle << 4) | (TYPE_CONSTANT_BUFFER << 12) | (2u << 20), SLOT, 0};
  };
  const auto immediate = [](uint32_t value) {
    return std::vector<uint32_t>{2u | (TYPE_IMMEDIATE << 12), value, value, value, value};
  };
  const auto instruction = [](std::vector<uint32_t>* added, uint32_t opcode, std::initializer_list<std::vector<uint32_t>> operands) {
    size_t length = 1;
    for (const auto& operand : operands) length += operand.size();
    added->push_back(opcode | (static_cast<uint32_t>(length) << 24));
    for (const auto& operand : operands) added->insert(added->end(), operand.begin(), operand.end());
  };
  // A swizzle that puts x at the first component and y at the second (each other component x).
  const auto to_components = [](const std::array<uint32_t, 2>& components) { return (1u << (2 * components[1])); };
  // An instruction with one operand replaced (its length updated).
  const auto replaced = [&](const Instruction& original, const Operand& operand, const std::vector<uint32_t>& with) {
    std::vector<uint32_t> rewritten(tokens.begin() + static_cast<ptrdiff_t>(original.at),
                                    tokens.begin() + static_cast<ptrdiff_t>(operand.at));
    rewritten.insert(rewritten.end(), with.begin(), with.end());
    rewritten.insert(rewritten.end(), tokens.begin() + static_cast<ptrdiff_t>(operand.at + operand.length),
                     tokens.begin() + static_cast<ptrdiff_t>(original.at + original.length));
    rewritten[0] = (rewritten[0] & ~(0x7Fu << 24)) | (static_cast<uint32_t>(rewritten.size()) << 24);
    return rewritten;
  };
  const auto put = [&](const Instruction& original, const std::vector<uint32_t>& code_tokens) {
    tokens.erase(tokens.begin() + static_cast<ptrdiff_t>(original.at), tokens.begin() + static_cast<ptrdiff_t>(original.at + original.length));
    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(original.at), code_tokens.begin(), code_tokens.end());
  };
  std::vector<uint32_t> share_code;
  instruction(&share_code, OP_EQ, {masked(uv, 0b1111), constant(0xE4), immediate(0)});
  instruction(&share_code, OP_MOVC, {masked(share, 0b1111), swizzled(uv, 0xE4), immediate(ONE), constant(0xE4)});

  // From the last site back (the earlier ones keep their place).
  for (auto site = sites.rbegin(); site != sites.rend(); ++site) {
    const Instruction& original = instructions[site->instruction];
    if (site->kind == Kind::NDC) {
      // div rU.xy, <the mad's uv, its two components into x / y>, rS.xyxx; the mad then reads rU.
      const Operand& source = original.operands[1];
      std::vector<uint32_t> uv_operand(tokens.begin() + static_cast<ptrdiff_t>(source.at),
                                       tokens.begin() + static_cast<ptrdiff_t>(source.at + source.length));
      const uint32_t x = SourceComponent(source, site->components[0]);
      const uint32_t y = SourceComponent(source, site->components[1]);
      uv_operand[0] = (uv_operand[0] & ~0xFF0u) | ((x | (y << 2) | (x << 4) | (x << 6)) << 4);
      std::vector<uint32_t> added;
      instruction(&added, OP_DIV, {masked(uv, 0b0011), uv_operand, swizzled(share, 0x04)});
      const auto rewritten = replaced(original, source, swizzled(uv, to_components(site->components)));
      added.insert(added.end(), rewritten.begin(), rewritten.end());
      put(original, added);
      ++result.ndc;
    } else if (site->kind == Kind::PROJECTED) {
      // mul rD.c0c1, rD.c0c1, rS.(x at c0, y at c1) right after the mad.
      const uint32_t destination = original.operands[0].index[0];
      const uint32_t mask = (1u << site->components[0]) | (1u << site->components[1]);
      std::vector<uint32_t> scale;
      instruction(&scale, OP_MUL, {masked(destination, mask), swizzled(destination, 0xE4), swizzled(share, to_components(site->components))});
      tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(original.at + original.length), scale.begin(), scale.end());
      ++result.projected;
    } else {
      // mov rU, <its coordinate>; its xy x the frame's share (ld: texel indices, through float); the read takes rU.
      const Operand& address = original.operands[1];
      std::vector<uint32_t> address_tokens(tokens.begin() + static_cast<ptrdiff_t>(address.at),
                                           tokens.begin() + static_cast<ptrdiff_t>(address.at + address.length));
      std::vector<uint32_t> added;
      instruction(&added, OP_MOV, {masked(uv, 0b1111), address_tokens});
      if (original.opcode == OP_LD) {
        instruction(&added, OP_UTOF, {masked(uv, 0b0011), swizzled(uv, 0x04)});
        instruction(&added, OP_MUL, {masked(uv, 0b0011), swizzled(uv, 0x04), swizzled(share, 0x0E)});
        instruction(&added, OP_FTOU, {masked(uv, 0b0011), swizzled(uv, 0x04)});
      } else {
        instruction(&added, OP_MUL, {masked(uv, 0b0011), swizzled(uv, 0x04), swizzled(share, 0x0E)});
      }
      const auto rewritten = replaced(original, address, swizzled(uv, 0xE4));
      added.insert(added.end(), rewritten.begin(), rewritten.end());
      put(original, added);
      ++result.region;
    }
  }
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(code_at), share_code.begin(), share_code.end());
  // dcl_constantbuffer cb12[1], immediateIndexed before dcl_temps; two more temps.
  const std::array<uint32_t, 4> declaration = {OP_DCL_CONSTANT_BUFFER | (4u << 24), 0x00208E46u, SLOT, 1};
  tokens[temps_at + 1] += 2;
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), declaration.begin(), declaration.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  result.code = dxbc::Write(code, program);
  return result;
}

struct Runtime {
  std::shared_mutex mutex;
  // The patched bytecode by the game's hash (kept: the game may create the shader again), and the code pointers the
  // pipelines get (patched or the addon's render_scale shaders) and their pipelines, each with its region read slots.
  std::unordered_map<uint32_t, std::vector<uint8_t>> patched;
  std::unordered_map<const void*, uint64_t> wanted_code;
  std::unordered_map<uint64_t, uint64_t> pipelines;
  // The addon's region reads: texture slots by the game's shader hash.
  std::unordered_map<uint32_t, uint64_t> region_reads;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Buffer> constants;
  std::array<float, 4> uploaded = {};
  bool bound = false;  // this frame's draws bound cb12
};
static Runtime runtime;
// Render / display of this frame (0: not scaled), and the display's size.
static std::atomic<float> frame_share_x = 0.f;
static std::atomic<float> frame_share_y = 0.f;
static std::atomic<uint32_t> display_width = 0;
static std::atomic<uint32_t> display_height = 0;

inline bool OnCreatePipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                             const reshade::api::pipeline_subobject* subobjects) {
  if (device->get_api() != reshade::api::device_api::d3d11) return false;
  bool modified = false;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    const auto type = subobjects[index].type;
    if (type != reshade::api::pipeline_subobject_type::pixel_shader && type != reshade::api::pipeline_subobject_type::compute_shader) {
      continue;
    }
    auto* desc = static_cast<reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const std::span<const uint8_t> code(static_cast<const uint8_t*>(desc->code), desc->code_size);
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(code.data(), code.size());
    const std::unique_lock lock(runtime.mutex);
    auto found = runtime.patched.find(hash);
    if (found == runtime.patched.end()) {
      const dxbc::Program program = dxbc::Read(code);
      const Reflection reflection = Reflect(code, program);
      if (reflection.render_scale) {
        runtime.wanted_code.emplace(desc->code, 0);
        continue;
      }
      const auto region = runtime.region_reads.find(hash);
      Patched patched = Patch(code, reflection, region != runtime.region_reads.end() ? region->second : 0);
      if (patched.region == 0) {
        runtime.region_reads.erase(hash);
      }
      if (!patched.code.empty()) {
        renodx::utils::log::i("[Total War] Render scale: ", (type == reshade::api::pipeline_subobject_type::compute_shader ? "compute" : "pixel"),
                              " shader ", std::format("0x{:08X}", hash), " patched (", patched.ndc, " positions, ", patched.projected,
                              " projected reads, ", patched.region, " region reads).");
      }
      found = runtime.patched.emplace(hash, std::move(patched.code)).first;
    }
    if (found->second.empty()) continue;
    desc->code = found->second.data();
    desc->code_size = found->second.size();
    const auto region = runtime.region_reads.find(hash);
    runtime.wanted_code.emplace(desc->code, region != runtime.region_reads.end() ? region->second : 0);
    modified = true;
  }
  return modified;
}

inline void OnInitPipeline(renodx::utils::pipeline::PipelineInfo* info, uint32_t subobject_count,
                           const reshade::api::pipeline_subobject* subobjects) {
  for (uint32_t index = 0; index < subobject_count; ++index) {
    const auto type = subobjects[index].type;
    if (type != reshade::api::pipeline_subobject_type::pixel_shader && type != reshade::api::pipeline_subobject_type::compute_shader) {
      continue;
    }
    const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr) continue;
    const std::unique_lock lock(runtime.mutex);
    if (const auto wanted = runtime.wanted_code.find(desc->code); wanted != runtime.wanted_code.end()) {
      runtime.pipelines[info->pipeline.handle] = wanted->second;
    }
  }
}

inline void OnDestroyPipeline(const renodx::utils::pipeline::PipelineInfo& info) {
  const std::unique_lock lock(runtime.mutex);
  runtime.pipelines.erase(info.pipeline.handle);
}

// cb12 for a patched (or render_scale) shader's draw / dispatch: `values` (xy the draw's share, zw the frame's), for
// the compute, pixel or (the addon's render_scale vertex shaders) vertex stage.
inline void Bind(ID3D11DeviceContext* native, const std::array<float, 4>& values, reshade::api::pipeline_stage stage) {
  const std::unique_lock lock(runtime.mutex);
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (runtime.device != device) {
    runtime.device = device;
    runtime.constants.Reset();
  }
  if (!runtime.constants) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(device->CreateBuffer(&desc, nullptr, runtime.constants.GetAddressOf()))) return;
    runtime.uploaded = {};
  }
  if (values != runtime.uploaded) {
    native->UpdateSubresource(runtime.constants.Get(), 0, nullptr, values.data(), 0, 0);
    runtime.uploaded = values;
  }
  ID3D11Buffer* buffer = runtime.constants.Get();
  if (stage == reshade::api::pipeline_stage::compute_shader) {
    native->CSSetConstantBuffers(SLOT, 1, &buffer);
  } else if (stage == reshade::api::pipeline_stage::vertex_shader) {
    native->VSSetConstantBuffers(SLOT, 1, &buffer);
  } else {
    native->PSSetConstantBuffers(SLOT, 1, &buffer);
  }
  runtime.bound = true;
}

// The bound shader's region read slots if it is patched (or a render_scale shader).
inline std::optional<uint64_t> Patched(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stage,
                                       renodx::utils::state::CommandListStateCache* cache) {
  const auto pipeline = renodx::utils::state::GetBoundShaderPipeline(cmd_list, stage, cache);
  if (pipeline.handle == 0u) return std::nullopt;
  const std::shared_lock lock(runtime.mutex);
  const auto found = runtime.pipelines.find(pipeline.handle);
  if (found == runtime.pipelines.end()) return std::nullopt;
  return found->second;
}

// Every draw while the frame is scaled: a patched (or render_scale) pixel shader gets cb12, the draw's viewport over
// its target (a viewport the render scale made, else 1) and the frame's share. Every dispatch: a patched compute shader's share is the frame's in the scaled span
// (before the tone mapper), 1 after.
constexpr auto OnCommand = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  const float frame_x = frame_share_x.load(std::memory_order_relaxed);
  if (frame_x == 0.f) return {};
  const float frame_y = frame_share_y.load(std::memory_order_relaxed);
  constexpr bool COMPUTE = std::is_same_v<Arguments, renodx::utils::command_action::DispatchArguments>;
  const auto region_slots = Patched(command.cmd_list, COMPUTE ? reshade::api::pipeline_stage::compute_shader : reshade::api::pipeline_stage::pixel_shader,
                                    &command.bound_state_cache);
  if (!region_slots.has_value()) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  std::array<float, 4> values = {1.f, 1.f, frame_x, frame_y};
  if (*region_slots != 0) {
    // Region reads of a texture the 3D draws at render scale: the display's size (or a halving of it); others (a small
    // view's own) whole.
    const auto slot = static_cast<UINT>(std::countr_zero(*region_slots));
    ComPtr<ID3D11ShaderResourceView> view;
    if constexpr (COMPUTE) {
      native->CSGetShaderResources(slot, 1, view.GetAddressOf());
    } else {
      native->PSGetShaderResources(slot, 1, view.GetAddressOf());
    }
    ComPtr<ID3D11Resource> resource;
    if (view) view->GetResource(resource.GetAddressOf());
    bool display_sized = false;
    if (ComPtr<ID3D11Texture2D> texture; resource && SUCCEEDED(resource.As(&texture))) {
      D3D11_TEXTURE2D_DESC desc = {};
      texture->GetDesc(&desc);
      const uint32_t width = display_width.load(std::memory_order_relaxed);
      const uint32_t height = display_height.load(std::memory_order_relaxed);
      for (uint32_t halving = 0; halving < 4 && !display_sized; ++halving) {
        display_sized = desc.Width == (width >> halving) && desc.Height == (height >> halving);
      }
    }
    if (!display_sized) {
      values[2] = 1.f;
      values[3] = 1.f;
    }
  }
  if constexpr (COMPUTE) {
    if (!frame_history::tonemapped.load(std::memory_order_relaxed)) values = {frame_x, frame_y, frame_x, frame_y};
  } else {
    D3D11_VIEWPORT viewport = {};
    UINT viewport_count = 1;
    native->RSGetViewports(&viewport_count, &viewport);
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    native->OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
    ComPtr<ID3D11Resource> resource;
    if (target) {
      target->GetResource(resource.GetAddressOf());
    } else if (depth) {
      depth->GetResource(resource.GetAddressOf());
    }
    if (ComPtr<ID3D11Texture2D> texture; resource && SUCCEEDED(resource.As(&texture)) && viewport_count != 0) {
      D3D11_TEXTURE2D_DESC desc = {};
      texture->GetDesc(&desc);
      // Only a viewport the render scale made (at the target's corner, its render share): a small view's own (a
      // portrait at an offset, a full one after DLSS) is whole.
      const float width = static_cast<float>(desc.Width);
      const float height = static_cast<float>(desc.Height);
      if (viewport.TopLeftX == 0.f && viewport.TopLeftY == 0.f && std::abs(viewport.Width - (width * frame_x)) <= 1.5f
          && std::abs(viewport.Height - (height * frame_y)) <= 1.5f) {
        values[0] = std::clamp(viewport.Width / width, 0.f, 1.f);
        values[1] = std::clamp(viewport.Height / height, 0.f, 1.f);
      }
    }
  }
  Bind(native, values, COMPUTE ? reshade::api::pipeline_stage::compute_shader : reshade::api::pipeline_stage::pixel_shader);
  return {};
};

// A draw of the addon's vertex shaders that read the frame's share (Install's `vertex_shaders`): cb12 for the vertex
// stage while the frame is scaled.
constexpr auto OnVertexShareDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const float frame_x = frame_share_x.load(std::memory_order_relaxed);
  if (frame_x == 0.f) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  Bind(native, {1.f, 1.f, frame_x, frame_share_y.load(std::memory_order_relaxed)}, reshade::api::pipeline_stage::vertex_shader);
  return {};
};

}  // namespace internal

// Every present (anti_aliasing.hpp): the next frame's render and display sizes (`scaled` false: 100%, cb12 unbound so
// the patched shaders read 1).
inline void Update(ID3D11DeviceContext* context, bool scaled, const std::array<uint32_t, 2>& render,
                   const std::array<uint32_t, 2>& display) {
  internal::frame_share_x.store(scaled ? static_cast<float>(render[0]) / static_cast<float>(display[0]) : 0.f, std::memory_order_relaxed);
  internal::frame_share_y.store(scaled ? static_cast<float>(render[1]) / static_cast<float>(display[1]) : 0.f, std::memory_order_relaxed);
  internal::display_width.store(display[0], std::memory_order_relaxed);
  internal::display_height.store(display[1], std::memory_order_relaxed);
  if (scaled || context == nullptr) return;
  const std::unique_lock lock(internal::runtime.mutex);
  if (!internal::runtime.bound) return;
  ID3D11Buffer* none = nullptr;
  context->PSSetConstantBuffers(internal::SLOT, 1, &none);
  context->CSSetConstantBuffers(internal::SLOT, 1, &none);
  context->VSSetConstantBuffers(internal::SLOT, 1, &none);
  internal::runtime.bound = false;
}

// The game's hash of a shader this module patched as the game created it (`code`: the code it gave the game), else
// none: another patch by the game's hashes on top (screen_space_shadows.hpp) then gives its code to Repatched.
inline std::optional<uint32_t> OriginalHash(const void* code) {
  const std::shared_lock lock(internal::runtime.mutex);
  for (const auto& [hash, patched] : internal::runtime.patched) {
    if (!patched.empty() && patched.data() == code) return hash;
  }
  return std::nullopt;
}

// Another patch's `code` in place of this module's (`patched`): its pipelines get cb12 the same.
inline void Repatched(const void* patched, const void* code) {
  const std::unique_lock lock(internal::runtime.mutex);
  if (const auto wanted = internal::runtime.wanted_code.find(patched); wanted != internal::runtime.wanted_code.end()) {
    internal::runtime.wanted_code.emplace(code, wanted->second);
  }
}

// A game shader (by its hash) that reads render-space textures at the display's size: those slots' reads take the
// frame's render share (patched as the game creates it). Before the game creates its shaders (the addon's start).
struct RegionRead {
  uint32_t shader_hash = 0;
  uint64_t slots = 0;  // bit per texture slot
};

// `vertex_shaders`: the game's vertex shaders (by hash) the addon replaces with render_scale ones (cb12 at b12).
inline void Install(std::span<const RegionRead> region_reads = {}, std::span<const uint32_t> vertex_shaders = {}) {
  {
    const std::unique_lock lock(internal::runtime.mutex);
    for (const auto& read : region_reads) internal::runtime.region_reads[read.shader_hash] |= read.slots;
  }
  renodx::utils::pipeline::RegisterOnCreateCallback(internal::OnCreatePipeline);
  renodx::utils::pipeline::RegisterOnInitCallback(internal::OnInitPipeline);
  renodx::utils::pipeline::RegisterOnDestroyCallback(internal::OnDestroyPipeline);
  renodx::utils::command_action::Register(internal::OnCommand, {.command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW
                                                                                 | renodx::utils::command_action::COMMAND_TYPE_DISPATCH});
  for (const uint32_t hash : vertex_shaders) {
    renodx::utils::command_action::Register(
        internal::OnVertexShareDraw, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::pipeline::UnregisterOnCreateCallback(internal::OnCreatePipeline);
  renodx::utils::pipeline::UnregisterOnInitCallback(internal::OnInitPipeline);
  renodx::utils::pipeline::UnregisterOnDestroyCallback(internal::OnDestroyPipeline);
  renodx::utils::command_action::Unregister(internal::OnCommand);
  renodx::utils::command_action::Unregister(internal::OnVertexShareDraw);
}

inline void OnDestroyDevice(ID3D11Device* device) {
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.device.Get() == device) {
    internal::runtime.device.Reset();
    internal::runtime.constants.Reset();
  }
}

}  // namespace twattila::view_share
