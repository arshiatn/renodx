/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// DXBC (Shader Model 4 / 5 bytecode) for the shaders the addon patches as the game creates them
// (screen_space_shadows.hpp's sun light, grass_fade.hpp's grass): the program's tokens out and back in; the
// signatures' elements (ray tracing's scene shaders, rt_core.hpp); the in-draw object motion's targets
// (object_motion.hpp).
namespace twattila::dxbc {

constexpr uint32_t OP_ADD = 0;
constexpr uint32_t OP_DIV = 14;
constexpr uint32_t OP_CUSTOMDATA = 53;
constexpr uint32_t OP_DCL_INPUT_PS = 98;
constexpr uint32_t OP_DCL_OUTPUT = 101;
constexpr uint32_t OP_DCL_TEMPS = 104;
// The in-draw motion's targets (AddMotionTargets): the motion in render pixels, the surface's depth.
constexpr uint32_t MOTION_TARGET = 6;
constexpr uint32_t MOTION_DEPTH_TARGET = 7;

// Its checksum: MD5's rounds over everything after the checksum with DXBC's own padding (the bit count first in the
// last block, (bits >> 2) | 1 last).
inline std::array<uint32_t, 4> Checksum(std::span<const uint8_t> code) {
  static constexpr std::array<uint32_t, 64> SHIFTS = {
      7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
      5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21,
      6, 10, 15, 21, 6, 10, 15, 21};
  static constexpr std::array<uint32_t, 64> SINES = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  std::array<uint32_t, 4> state = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  const auto rounds = [&](const uint8_t* block) {
    std::array<uint32_t, 16> words;
    std::memcpy(words.data(), block, 64);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (uint32_t i = 0; i < 64; ++i) {
      uint32_t f = 0;
      uint32_t g = 0;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      f += a + SINES[i] + words[g];
      a = d;
      d = c;
      c = b;
      b += (f << SHIFTS[i]) | (f >> (32 - SHIFTS[i]));
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
  };
  const std::span<const uint8_t> body = code.subspan(20);
  const auto bits = static_cast<uint32_t>(body.size() * 8);
  const uint32_t bits_part2 = (bits >> 2) | 1u;
  const size_t full = body.size() / 64 * 64;
  for (size_t at = 0; at < full; at += 64) rounds(body.data() + at);
  const size_t left = body.size() - full;
  std::array<uint8_t, 64> block = {};
  if (left < 56) {
    std::memcpy(block.data(), &bits, 4);
    std::memcpy(block.data() + 4, body.data() + full, left);
    block[4 + left] = 0x80;
    std::memcpy(block.data() + 60, &bits_part2, 4);
    rounds(block.data());
  } else {
    std::memcpy(block.data(), body.data() + full, left);
    block[left] = 0x80;
    rounds(block.data());
    block = {};
    std::memcpy(block.data(), &bits, 4);
    std::memcpy(block.data() + 60, &bits_part2, 4);
    rounds(block.data());
  }
  return state;
}

// A shader's program chunk (SHDR / SHEX) as tokens, and where it sits in the container.
struct Program {
  std::vector<uint32_t> tokens;   // empty: not DXBC, or malformed
  std::vector<uint32_t> offsets;  // the container's chunks
  size_t chunk = 0;               // the program's
};

inline Program Read(std::span<const uint8_t> code) {
  const auto read = [&](size_t at) {
    uint32_t value = 0;
    std::memcpy(&value, code.data() + at, 4);
    return value;
  };
  Program program;
  if (code.size() < 32 || std::memcmp(code.data(), "DXBC", 4) != 0 || read(24) != code.size()) return {};
  const uint32_t chunk_count = read(28);
  if (code.size() < 32 + 4 * size_t{chunk_count}) return {};
  program.offsets.resize(chunk_count);
  program.chunk = chunk_count;
  for (uint32_t index = 0; index < chunk_count; ++index) {
    program.offsets[index] = read(32 + 4 * size_t{index});
    if (program.offsets[index] + 8 > code.size()) return {};
    if (std::memcmp(code.data() + program.offsets[index], "SHDR", 4) == 0
        || std::memcmp(code.data() + program.offsets[index], "SHEX", 4) == 0) {
      program.chunk = index;
    }
  }
  if (program.chunk == chunk_count) return {};
  const uint32_t size = read(program.offsets[program.chunk] + 4);
  if (program.offsets[program.chunk] + 8 + size_t{size} > code.size() || size % 4 != 0) return {};
  program.tokens.resize(size / 4);
  std::memcpy(program.tokens.data(), code.data() + program.offsets[program.chunk] + 8, size);
  if (program.tokens.size() < 2 || program.tokens[1] != program.tokens.size()) return {};
  return program;
}

// An instruction's (or declaration's) length in tokens at `at`; 0: malformed.
inline size_t Length(const std::vector<uint32_t>& tokens, size_t at) {
  const size_t length = ((tokens[at] & 0x7FF) == OP_CUSTOMDATA) ? tokens[at + 1] : ((tokens[at] >> 24) & 0x7F);
  return (at + length > tokens.size()) ? 0 : length;
}

// `code` with `program`'s tokens (grown, their count in the second) as its program: every chunk after it moved, the
// size and the checksum again.
inline std::vector<uint8_t> Write(std::span<const uint8_t> code, const Program& program) {
  uint32_t old_size;
  std::memcpy(&old_size, code.data() + program.offsets[program.chunk] + 4, 4);
  const auto new_size = static_cast<uint32_t>(program.tokens.size() * 4);
  const uint32_t growth = new_size - old_size;
  std::vector<uint8_t> patched(code.size() + growth);
  const uint32_t program_start = program.offsets[program.chunk] + 8;
  const uint32_t program_end = program_start + old_size;
  std::memcpy(patched.data(), code.data(), program_start);
  std::memcpy(patched.data() + program_start, program.tokens.data(), new_size);
  std::memcpy(patched.data() + program_end + growth, code.data() + program_end, code.size() - program_end);
  const auto write = [&](size_t at, uint32_t value) { std::memcpy(patched.data() + at, &value, 4); };
  write(program_start - 4, new_size);
  write(24, static_cast<uint32_t>(patched.size()));
  for (size_t index = 0; index < program.offsets.size(); ++index) {
    if (program.offsets[index] > program.offsets[program.chunk]) write(32 + 4 * index, program.offsets[index] + growth);
  }
  const auto checksum = Checksum(patched);
  std::memcpy(patched.data() + 4, checksum.data(), 16);
  return patched;
}

// A signature chunk's (`tag`: ISGN / OSGN) element: its semantic name and index, its component type
// (D3D_REGISTER_COMPONENT_TYPE).
struct SignatureElement {
  std::string_view name;
  uint32_t index = 0;
  uint32_t component_type = 0;
};

// The first element of `program`'s chunk `tag` that `matches` (the element, its register) into `found`; false: none.
inline bool FindSignatureElement(std::span<const uint8_t> code, const Program& program, const char (&tag)[5], const auto& matches,
                                 SignatureElement* found) {
  const auto read = [&](size_t at) {
    uint32_t value = 0;
    if (at + 4 <= code.size()) std::memcpy(&value, code.data() + at, 4);
    return value;
  };
  for (const uint32_t offset : program.offsets) {
    if (std::memcmp(code.data() + offset, tag, 4) != 0) continue;
    const size_t data = offset + 8;
    const uint32_t count = read(data);
    for (uint32_t element = 0; element < count; ++element) {
      const size_t at = data + 8 + (24 * size_t{element});
      const size_t name = data + read(at);
      if (at + 24 > code.size() || name >= code.size()) return false;
      const SignatureElement candidate = {
          .name = std::string_view(reinterpret_cast<const char*>(code.data() + name),
                                   strnlen(reinterpret_cast<const char*>(code.data() + name), code.size() - name)),
          .index = read(at + 4),
          .component_type = read(at + 12),
      };
      if (matches(candidate, read(at + 16))) {
        *found = candidate;
        return true;
      }
    }
  }
  return false;
}

// The in-draw object motion (object_motion.hpp) in the game's pixel shader `code`: its motion vertex shader's MOTION0
// and MOTION1 in at `motion_register` and the next (this frame's and last frame's clip positions, x and y in render
// pixels, without the jitter), first thing the motion (to where the surface was) into SV_Target6.xy and the surface's
// depth into SV_Target7.x (a discarded pixel writes neither), in one temporary register more. Empty: not patched (not a
// shader model 4 / 5 pixel shader with ISGN / OSGN, outputs other than targets 0-5, inputs of its own at those
// registers).
inline std::vector<uint8_t> AddMotionTargets(std::span<const uint8_t> code, uint32_t motion_register) {
  Program program = Read(code);
  // The version token: the program type from bit 16 (0: pixel), the major version in bits 4-7.
  if (program.tokens.empty() || (program.tokens[0] >> 16) != 0 || ((program.tokens[0] >> 4) & 0xF) < 4) return {};
  const auto read = [&](size_t at) {
    uint32_t value = 0;
    std::memcpy(&value, code.data() + at, 4);
    return value;
  };
  const auto find_chunk = [&](const char (&tag)[5]) {
    const auto found = std::ranges::find_if(program.offsets, [&](uint32_t offset) { return std::memcmp(code.data() + offset, tag, 4) == 0; });
    return static_cast<size_t>(found - program.offsets.begin());
  };
  const size_t input_chunk = find_chunk("ISGN");
  const size_t output_chunk = find_chunk("OSGN");
  if (input_chunk == program.offsets.size() || output_chunk == program.offsets.size()) return {};

  // A signature chunk: the count and 8, the elements (24 bytes each), their names.
  struct Element {
    std::string name;
    uint32_t index = 0;
    uint32_t system_value = 0;
    uint32_t component_type = 0;
    uint32_t reg = 0;
    uint8_t mask = 0;
    uint8_t rw_mask = 0;  // inputs: the components always read; outputs: never written
  };
  const auto read_signature = [&](size_t chunk, std::vector<Element>* elements) {
    const size_t data = program.offsets[chunk] + 8;
    const size_t size = read(program.offsets[chunk] + 4);
    if (size < 8 || data + size > code.size()) return false;
    const uint32_t count = read(data);
    if (8 + 24 * size_t{count} > size) return false;
    for (uint32_t element = 0; element < count; ++element) {
      const size_t at = data + 8 + 24 * size_t{element};
      const size_t name = read(at);
      if (name >= size) return false;
      const char* text = reinterpret_cast<const char*>(code.data() + data + name);
      elements->push_back({
          .name = std::string(text, strnlen(text, size - name)),
          .index = read(at + 4),
          .system_value = read(at + 8),
          .component_type = read(at + 12),
          .reg = read(at + 16),
          .mask = code[at + 20],
          .rw_mask = code[at + 21],
      });
    }
    return true;
  };
  const auto write_signature = [](const std::vector<Element>& elements) {
    std::vector<uint8_t> data(8 + 24 * elements.size());
    const auto put = [&](size_t at, uint32_t value) { std::memcpy(data.data() + at, &value, 4); };
    put(0, static_cast<uint32_t>(elements.size()));
    put(4, 8);
    std::vector<std::pair<std::string_view, uint32_t>> names;
    for (size_t element = 0; element < elements.size(); ++element) {
      const Element& written = elements[element];
      auto name = std::ranges::find(names, std::string_view(written.name), &std::pair<std::string_view, uint32_t>::first);
      if (name == names.end()) {
        name = names.insert(names.end(), {written.name, static_cast<uint32_t>(data.size())});
        data.insert(data.end(), written.name.begin(), written.name.end());
        data.push_back(0);
      }
      const size_t at = 8 + 24 * element;
      put(at, name->second);
      put(at + 4, written.index);
      put(at + 8, written.system_value);
      put(at + 12, written.component_type);
      put(at + 16, written.reg);
      data[at + 20] = written.mask;
      data[at + 21] = written.rw_mask;
    }
    while (data.size() % 4 != 0) data.push_back(0xAB);
    return data;
  };
  const auto named = [](const Element& element, std::string_view lower_case) {
    return std::ranges::equal(element.name, lower_case, [](char a, char b) { return a == b || (b >= 'a' && b <= 'z' && (a | 0x20) == b); });
  };
  std::vector<Element> inputs;
  std::vector<Element> outputs;
  if (!read_signature(input_chunk, &inputs) || !read_signature(output_chunk, &outputs)) return {};
  for (const Element& input : inputs) {
    if (input.reg == motion_register || input.reg == motion_register + 1) return {};
  }
  // Its targets (and a coverage mask, which drops the motion with the rest), no depth of its own.
  for (const Element& output : outputs) {
    const bool target = named(output, "sv_target");
    if (target && output.reg >= MOTION_TARGET) return {};
    if (!target && !named(output, "sv_coverage")) return {};
  }
  const auto game_target = std::ranges::find_if(outputs, [&](const Element& output) { return named(output, "sv_target"); });
  if (game_target == outputs.end()) return {};
  // MOTION0 / MOTION1 among the inputs by register (system values such as SV_IsFrontFace can sit above them); the
  // motion's targets after the game's.
  const auto inputs_above = std::ranges::find_if(inputs, [&](const Element& input) { return input.reg > motion_register; });
  inputs.insert(inputs_above, {{.name = "MOTION", .index = 0, .component_type = 3, .reg = motion_register, .mask = 0xF, .rw_mask = 0xF},
                               {.name = "MOTION", .index = 1, .component_type = 3, .reg = motion_register + 1, .mask = 0xF, .rw_mask = 0xB}});
  const std::string target_name = game_target->name;
  const uint32_t target_value = game_target->system_value;
  const auto targets_end = std::ranges::find_if(outputs, [&](const Element& output) { return !named(output, "sv_target"); });
  outputs.insert(targets_end, {{.name = target_name, .index = MOTION_TARGET, .system_value = target_value, .component_type = 3,
                                .reg = MOTION_TARGET, .mask = 0x3, .rw_mask = 0xC},
                               {.name = target_name, .index = MOTION_DEPTH_TARGET, .system_value = target_value,
                                .component_type = 3, .reg = MOTION_DEPTH_TARGET, .mask = 0x1, .rw_mask = 0xE}});

  // The program: the declarations (resources, inputs, outputs, temporaries, immediate constants) end at its first
  // instruction; the motion's declarations and instructions go there.
  std::vector<uint32_t>& tokens = program.tokens;
  size_t first_instruction = 2;
  size_t temps_at = 0;  // dcl_temps' count (0: none)
  while (first_instruction < tokens.size()) {
    const uint32_t opcode = tokens[first_instruction] & 0x7FF;
    const bool declaration = (opcode >= 88 && opcode <= 106) || (opcode >= 143 && opcode <= 162) || opcode == 206
                             || (opcode == OP_CUSTOMDATA && (tokens[first_instruction] >> 11) == 3);
    if (!declaration) break;
    const size_t length = Length(tokens, first_instruction);
    if (length == 0) return {};
    if (opcode == OP_DCL_TEMPS) temps_at = first_instruction + 1;
    first_instruction += length;
  }
  if (first_instruction >= tokens.size()) return {};
  const uint32_t temp = (temps_at != 0 ? tokens[temps_at] : 0u);
  // Operands with 4 components and a 1D immediate index: a mask (destinations) or a swizzle (sources).
  constexpr uint32_t TEMP = 0;
  constexpr uint32_t INPUT = 1;
  constexpr uint32_t OUTPUT = 2;
  const auto masked = [](uint32_t type, uint32_t mask) { return 2u | (mask << 4) | (type << 12) | (1u << 20); };
  const auto swizzled = [](uint32_t type, uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    return 2u | (1u << 2) | ((x | (y << 2) | (z << 4) | (w << 6)) << 4) | (type << 12) | (1u << 20);
  };
  constexpr uint32_t NEGATED = 0x80000000u;  // an extended operand token follows: modifier (1), negate (1 << 6)
  const uint32_t current = motion_register;
  const uint32_t previous = motion_register + 1;
  std::vector<uint32_t> motion = {
      OP_DCL_INPUT_PS | (2u << 11) | (3u << 24), masked(INPUT, 0xF), current,  // linear
      OP_DCL_INPUT_PS | (2u << 11) | (3u << 24), masked(INPUT, 0xF), previous,
      OP_DCL_OUTPUT | (3u << 24), masked(OUTPUT, 0x3), MOTION_TARGET,
      OP_DCL_OUTPUT | (3u << 24), masked(OUTPUT, 0x1), MOTION_DEPTH_TARGET,
  };
  if (temps_at != 0) {
    tokens[temps_at] = temp + 1;
  } else {
    motion.insert(motion.end(), {OP_DCL_TEMPS | (2u << 24), 1u});
  }
  motion.insert(motion.end(), {
      // div r.xy, current.xyxx, current.wwww; div r.zw, previous.xxxy, previous.wwww
      OP_DIV | (7u << 24), masked(TEMP, 0x3), temp, swizzled(INPUT, 0, 1, 0, 0), current, swizzled(INPUT, 3, 3, 3, 3), current,
      OP_DIV | (7u << 24), masked(TEMP, 0xC), temp, swizzled(INPUT, 0, 0, 0, 1), previous, swizzled(INPUT, 3, 3, 3, 3), previous,
      // add o6.xy, r.zwzz, -r.xyxx
      OP_ADD | (8u << 24), masked(OUTPUT, 0x3), MOTION_TARGET, swizzled(TEMP, 2, 3, 2, 2), temp,
      swizzled(TEMP, 0, 1, 0, 0) | NEGATED, 1u | (1u << 6), temp,
      // div o7.x, current.zzzz, current.wwww
      OP_DIV | (7u << 24), masked(OUTPUT, 0x1), MOTION_DEPTH_TARGET, swizzled(INPUT, 2, 2, 2, 2), current,
      swizzled(INPUT, 3, 3, 3, 3), current,
  });
  tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(first_instruction), motion.begin(), motion.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());

  // The container again: the three chunks replaced, the rest as they were.
  const std::vector<uint8_t> input_data = write_signature(inputs);
  const std::vector<uint8_t> output_data = write_signature(outputs);
  const std::span<const uint8_t> program_data(reinterpret_cast<const uint8_t*>(tokens.data()), tokens.size() * 4);
  std::vector<std::span<const uint8_t>> chunks;
  for (size_t index = 0; index < program.offsets.size(); ++index) {
    if (index == input_chunk) {
      chunks.emplace_back(input_data);
    } else if (index == output_chunk) {
      chunks.emplace_back(output_data);
    } else if (index == program.chunk) {
      chunks.emplace_back(program_data);
    } else {
      chunks.emplace_back(code.subspan(program.offsets[index] + 8, read(program.offsets[index] + 4)));
    }
  }
  size_t size = 32 + 4 * chunks.size();
  for (const auto& chunk : chunks) size += 8 + chunk.size();
  std::vector<uint8_t> patched(size);
  const auto write = [&](size_t at, uint32_t value) { std::memcpy(patched.data() + at, &value, 4); };
  std::memcpy(patched.data(), code.data(), 24);
  write(24, static_cast<uint32_t>(size));
  write(28, static_cast<uint32_t>(chunks.size()));
  size_t at = 32 + 4 * chunks.size();
  for (size_t index = 0; index < chunks.size(); ++index) {
    write(32 + 4 * index, static_cast<uint32_t>(at));
    std::memcpy(patched.data() + at, code.data() + program.offsets[index], 4);
    write(at + 4, static_cast<uint32_t>(chunks[index].size()));
    std::memcpy(patched.data() + at + 8, chunks[index].data(), chunks[index].size());
    at += 8 + chunks[index].size();
  }
  const auto checksum = Checksum(patched);
  std::memcpy(patched.data() + 4, checksum.data(), 16);
  return patched;
}

}  // namespace twattila::dxbc
