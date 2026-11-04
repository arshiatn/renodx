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
#include <utility>
#include <vector>

// DXBC (Shader Model 4 / 5 bytecode) for the shaders the addon patches as the game creates them
// (screen_space_shadows.hpp's sun light, grass_fade.hpp's grass): the program's tokens out and back in; the
// signatures' elements (ray tracing's scene shaders, rt_core.hpp); the in-draw object motion's targets
// (object_motion.hpp); WH3's velocity (twwarhammer3/object_motion.hpp).
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

// An operand of an instruction: its type (D3D10_SB_OPERAND_TYPE), its component mode and selection, modifier, its
// indices (immediate; `relative` when not) or an immediate's values.
constexpr uint32_t OPERAND_IMMEDIATE = 4;
struct Operand {
  size_t at = 0;
  size_t length = 0;
  uint32_t type = 0;
  uint32_t mode = 0;    // 0 mask, 1 swizzle, 2 one component
  uint32_t select = 0;  // its mask, swizzle or component
  bool modified = false;
  bool negated = false;  // its modifier: negate only
  bool relative = false;
  std::array<uint32_t, 2> index = {};
  std::array<uint32_t, 4> values = {};  // an immediate's (a scalar's for every component)
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
      if ((extended & 0x3F) == 1 && ((extended >> 6) & 0xFF) == 1) operand->negated = true;
    } while ((extended >> 31) != 0);
  }
  if (operand->type == OPERAND_IMMEDIATE) {
    const size_t count = ((token & 3) == 1) ? 1 : 4;
    if (next + count > end) return 0;
    for (size_t component = 0; component < 4; ++component) operand->values[component] = tokens[next + (count == 1 ? 0 : component)];
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

// `code` with `program`'s tokens (grown, their count in the second) as its program: every chunk after it moved, the
// size and the checksum again.
inline std::vector<uint8_t> Write(std::span<const uint8_t> code, const Program& program) {
  uint32_t old_size;
  std::memcpy(&old_size, code.data() + program.offsets[program.chunk] + 4, 4);
  const auto new_size = static_cast<uint32_t>(program.tokens.size() * 4);
  const uint32_t growth = new_size - old_size;  // wraps when the program shrinks: unsigned sums below wrap back
  std::vector<uint8_t> patched(static_cast<size_t>(static_cast<uint32_t>(code.size()) + growth));
  const uint32_t program_start = program.offsets[program.chunk] + 8;
  const uint32_t program_end = program_start + old_size;
  std::memcpy(patched.data(), code.data(), program_start);
  std::memcpy(patched.data() + program_start, program.tokens.data(), new_size);
  std::memcpy(patched.data() + static_cast<uint32_t>(program_end + growth), code.data() + program_end, code.size() - program_end);
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
// (D3D_REGISTER_COMPONENT_TYPE), its system value (D3D_NAME) and components.
struct SignatureElement {
  std::string_view name;
  uint32_t index = 0;
  uint32_t component_type = 0;
  uint32_t system_value = 0;
  uint8_t mask = 0;
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
          .system_value = read(at + 8),
          .mask = code[at + 20],
      };
      if (matches(candidate, read(at + 16))) {
        *found = candidate;
        return true;
      }
    }
  }
  return false;
}

namespace internal {

// A signature chunk's element, to read and write the chunk whole.
struct Element {
  std::string name;
  uint32_t index = 0;
  uint32_t system_value = 0;
  uint32_t component_type = 0;
  uint32_t reg = 0;
  uint8_t mask = 0;
  uint8_t rw_mask = 0;  // inputs: the components always read; outputs: never written
};

inline size_t FindChunk(std::span<const uint8_t> code, const Program& program, const char (&tag)[5]) {
  const auto found = std::ranges::find_if(program.offsets, [&](uint32_t offset) { return std::memcmp(code.data() + offset, tag, 4) == 0; });
  return static_cast<size_t>(found - program.offsets.begin());
}

// A signature chunk: the count and 8, the elements (24 bytes each), their names.
inline bool ReadElements(std::span<const uint8_t> code, const Program& program, size_t chunk, std::vector<Element>* elements) {
  const auto read = [&](size_t at) {
    uint32_t value = 0;
    std::memcpy(&value, code.data() + at, 4);
    return value;
  };
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
}

inline std::vector<uint8_t> WriteElements(const std::vector<Element>& elements) {
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
}

// The container again with `program`'s tokens and both signatures replaced, the rest as they were.
inline std::vector<uint8_t> ReplaceChunks(std::span<const uint8_t> code, const Program& program, size_t input_chunk, size_t output_chunk,
                                          const std::vector<Element>& inputs, const std::vector<Element>& outputs) {
  const auto read = [&](size_t at) {
    uint32_t value = 0;
    std::memcpy(&value, code.data() + at, 4);
    return value;
  };
  const std::vector<uint8_t> input_data = WriteElements(inputs);
  const std::vector<uint8_t> output_data = WriteElements(outputs);
  const std::span<const uint8_t> program_data(reinterpret_cast<const uint8_t*>(program.tokens.data()), program.tokens.size() * 4);
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

// Where a program's declarations end (its first instruction), 0: malformed; `temps_at`: dcl_temps' count (0: none).
inline size_t FirstInstruction(const std::vector<uint32_t>& tokens, size_t* temps_at) {
  size_t first_instruction = 2;
  while (first_instruction < tokens.size()) {
    const uint32_t opcode = tokens[first_instruction] & 0x7FF;
    const bool declaration = (opcode >= 88 && opcode <= 106) || (opcode >= 143 && opcode <= 162) || opcode == 206
                             || (opcode == OP_CUSTOMDATA && (tokens[first_instruction] >> 11) == 3);
    if (!declaration) break;
    const size_t length = Length(tokens, first_instruction);
    if (length == 0) return 0;
    if (opcode == OP_DCL_TEMPS) *temps_at = first_instruction + 1;
    first_instruction += length;
  }
  return (first_instruction >= tokens.size() ? 0 : first_instruction);
}

// An instruction's first operand, past its extended opcode tokens.
inline size_t FirstOperand(const std::vector<uint32_t>& tokens, size_t at) {
  size_t next = at;
  while ((tokens[next] >> 31) != 0) ++next;
  return next + 1;
}

// One operand at `at` moved for AddPreviousPosition's copy of the program: temporaries up by `temps`, outputs into the temporaries from
// `outputs_at`, time_in_sec (cb0[35].x) to prev_time_in_sec (.y). Returns its length; 0: malformed, or it reads .y already.
inline size_t MoveOperand(std::vector<uint32_t>* tokens, size_t at, size_t end, uint32_t temps, uint32_t outputs_at) {
  constexpr uint32_t TEMP = 0;
  constexpr uint32_t OUTPUT = 2;
  constexpr uint32_t CONSTANT_BUFFER = 8;
  if (at >= end) return 0;
  const uint32_t token = (*tokens)[at];
  const uint32_t type = (token >> 12) & 0xFF;
  size_t next = at + 1;
  if ((token >> 31) != 0) {
    while (next < end && ((*tokens)[next] >> 31) != 0) ++next;
    ++next;
  }
  if (type == OPERAND_IMMEDIATE) next += (((token & 3) == 1) ? 1 : 4);
  const uint32_t dimensions = (token >> 20) & 3;
  std::array<size_t, 2> index_at = {};
  for (uint32_t dimension = 0; dimension < dimensions; ++dimension) {
    const uint32_t representation = (token >> (22 + 3 * dimension)) & 7;
    if (representation == 0 || representation == 3) {
      if (next >= end) return 0;
      if (representation == 0 && dimension < 2) index_at[dimension] = next;
      ++next;
    } else if (representation == 1 || representation == 4) {
      next += 2;
    }
    if (representation >= 2) {
      const size_t nested = MoveOperand(tokens, next, end, temps, outputs_at);
      if (nested == 0) return 0;
      next += nested;
    }
  }
  if (next > end) return 0;
  if (type == TEMP && dimensions == 1 && index_at[0] != 0) {
    (*tokens)[index_at[0]] += temps;
  } else if (type == OUTPUT && dimensions == 1 && index_at[0] != 0) {
    (*tokens)[index_at[0]] += outputs_at;
    (*tokens)[at] = token & ~(0xFFu << 12);
  } else if (type == TEMP || type == OUTPUT) {
    return 0;
  } else if (type == CONSTANT_BUFFER && dimensions == 2 && index_at[0] != 0 && index_at[1] != 0
             && (*tokens)[index_at[0]] == 0 && (*tokens)[index_at[1]] == 35) {
    const uint32_t mode = (token >> 2) & 3;
    uint32_t select = (token >> 4) & 0xFF;
    if (mode == 1) {
      for (uint32_t component = 0; component < 4; ++component) {
        const uint32_t read = (select >> (2 * component)) & 3;
        if (read == 1) return 0;
        if (read == 0) select |= 1u << (2 * component);
      }
    } else if (mode == 2) {
      if ((select & 3) == 1) return 0;
      if ((select & 3) == 0) select |= 1u;
    } else {
      return 0;
    }
    (*tokens)[at] = (token & ~(0xFFu << 4)) | (select << 4);
  }
  return next - at;
}

}  // namespace internal

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
  const size_t input_chunk = internal::FindChunk(code, program, "ISGN");
  const size_t output_chunk = internal::FindChunk(code, program, "OSGN");
  if (input_chunk == program.offsets.size() || output_chunk == program.offsets.size()) return {};
  const auto named = [](const internal::Element& element, std::string_view lower_case) {
    return std::ranges::equal(element.name, lower_case, [](char a, char b) { return a == b || (b >= 'a' && b <= 'z' && (a | 0x20) == b); });
  };
  std::vector<internal::Element> inputs;
  std::vector<internal::Element> outputs;
  if (!internal::ReadElements(code, program, input_chunk, &inputs) || !internal::ReadElements(code, program, output_chunk, &outputs)) return {};
  for (const internal::Element& input : inputs) {
    if (input.reg == motion_register || input.reg == motion_register + 1) return {};
  }
  // Its targets (and a coverage mask, which drops the motion with the rest), no depth of its own.
  for (const internal::Element& output : outputs) {
    const bool target = named(output, "sv_target");
    if (target && output.reg >= MOTION_TARGET) return {};
    if (!target && !named(output, "sv_coverage")) return {};
  }
  const auto game_target = std::ranges::find_if(outputs, [&](const internal::Element& output) { return named(output, "sv_target"); });
  if (game_target == outputs.end()) return {};
  // MOTION0 / MOTION1 among the inputs by register (system values such as SV_IsFrontFace can sit above them); the
  // motion's targets after the game's.
  const auto inputs_above = std::ranges::find_if(inputs, [&](const internal::Element& input) { return input.reg > motion_register; });
  inputs.insert(inputs_above, {{.name = "MOTION", .index = 0, .component_type = 3, .reg = motion_register, .mask = 0xF, .rw_mask = 0xF},
                               {.name = "MOTION", .index = 1, .component_type = 3, .reg = motion_register + 1, .mask = 0xF, .rw_mask = 0xB}});
  const std::string target_name = game_target->name;
  const uint32_t target_value = game_target->system_value;
  const auto targets_end = std::ranges::find_if(outputs, [&](const internal::Element& output) { return !named(output, "sv_target"); });
  outputs.insert(targets_end, {{.name = target_name, .index = MOTION_TARGET, .system_value = target_value, .component_type = 3,
                                .reg = MOTION_TARGET, .mask = 0x3, .rw_mask = 0xC},
                               {.name = target_name, .index = MOTION_DEPTH_TARGET, .system_value = target_value,
                                .component_type = 3, .reg = MOTION_DEPTH_TARGET, .mask = 0x1, .rw_mask = 0xE}});

  // The program: the declarations (resources, inputs, outputs, temporaries, immediate constants) end at its first
  // instruction; the motion's declarations and instructions go there.
  std::vector<uint32_t>& tokens = program.tokens;
  size_t temps_at = 0;
  const size_t first_instruction = internal::FirstInstruction(tokens, &temps_at);
  if (first_instruction == 0) return {};
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

  return internal::ReplaceChunks(code, program, input_chunk, output_chunk, inputs, outputs);
}

// WH3's velocity (twwarhammer3/object_motion.hpp), for a G-buffer pixel shader that writes none: as the game's own (ps 761079CC,
// fx_rigid's end), SV_Target3.xy from its previous world position (v1.xyz) and world position (v2.xyz), with `camera` at b0; first thing, in
// temporaries of its own; and the flag byte in o2.w's literals with bit 4 ("this draw wrote its motion", ps D409F9B5) set and
// bit 16 ("no motion") clear. Empty: not patched (not the G-buffer's layout, flag not from literals, interpolation not its own).
inline std::vector<uint8_t> AddVelocityTarget(std::span<const uint8_t> code) {
  constexpr uint32_t OP_MOV = 54;
  constexpr uint32_t OP_MOVC = 55;
  constexpr uint32_t OP_DP4 = 17;
  constexpr uint32_t OP_DCL_CONSTANT_BUFFER = 89;
  constexpr uint32_t CAMERA_REGISTERS = 66;
  constexpr uint32_t TEMP = 0;
  constexpr uint32_t INPUT = 1;
  constexpr uint32_t OUTPUT = 2;
  constexpr uint32_t CONSTANT_BUFFER = 8;
  constexpr uint32_t PREVIOUS = 1;
  constexpr uint32_t CURRENT = 2;
  constexpr uint32_t VELOCITY = 3;
  constexpr uint32_t FLAG_OUTPUT = 2;
  Program program = Read(code);
  if (program.tokens.empty() || (program.tokens[0] >> 16) != 0) return {};
  const size_t input_chunk = internal::FindChunk(code, program, "ISGN");
  const size_t output_chunk = internal::FindChunk(code, program, "OSGN");
  if (input_chunk == program.offsets.size() || output_chunk == program.offsets.size()) return {};
  std::vector<internal::Element> inputs;
  std::vector<internal::Element> outputs;
  if (!internal::ReadElements(code, program, input_chunk, &inputs) || !internal::ReadElements(code, program, output_chunk, &outputs)) return {};
  for (const uint32_t reg : {PREVIOUS, CURRENT}) {
    const auto input = std::ranges::find(inputs, reg, &internal::Element::reg);
    if (input == inputs.end() || (input->mask & 0x7) != 0x7 || input->system_value != 0) return {};
    input->rw_mask |= 0x7;
  }
  if (std::ranges::find(outputs, VELOCITY, &internal::Element::reg) != outputs.end()) return {};
  const auto flag_output = std::ranges::find(outputs, FLAG_OUTPUT, &internal::Element::reg);
  if (flag_output == outputs.end()) return {};
  const auto outputs_above = std::ranges::find_if(outputs, [&](const internal::Element& output) { return output.reg > VELOCITY; });
  outputs.insert(outputs_above, internal::Element{.name = "SV_Target", .index = VELOCITY, .system_value = flag_output->system_value, .component_type = 3, .reg = VELOCITY, .mask = 0x3, .rw_mask = 0xC});

  std::vector<uint32_t>& tokens = program.tokens;
  size_t temps_at = 0;
  const size_t first_instruction = internal::FirstInstruction(tokens, &temps_at);
  if (first_instruction == 0) return {};

  // Its declarations: the camera's whole range, v1 and v2 linear.
  bool camera_declared = false;
  std::array<bool, 2> input_declared = {};
  for (size_t at = 2; at < first_instruction; at += Length(tokens, at)) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    Operand operand;
    if (opcode == OP_DCL_CONSTANT_BUFFER) {
      if (ParseOperand(tokens, at + 1, at + Length(tokens, at), &operand) != 0 && operand.index[0] == 0) {
        camera_declared = true;
        // The size is the operand's second index, right after the token.
        if (tokens[at + 3] < CAMERA_REGISTERS) tokens[at + 3] = CAMERA_REGISTERS;
      }
    } else if (opcode == OP_DCL_INPUT_PS) {
      if (ParseOperand(tokens, at + 1, at + Length(tokens, at), &operand) == 0) return {};
      if (operand.index[0] != PREVIOUS && operand.index[0] != CURRENT) continue;
      const uint32_t interpolation = (tokens[at] >> 11) & 0xF;
      if (interpolation != 2 && interpolation != 3) return {};  // linear, linear centroid
      if (input_declared[operand.index[0] - PREVIOUS]) return {};
      input_declared[operand.index[0] - PREVIOUS] = true;
      tokens[at + 1] |= 0x7u << 4;
    }
  }
  if (!camera_declared) return {};

  // The flag's literals.
  bool flag_written = false;
  for (size_t at = first_instruction; at < tokens.size();) {
    const size_t length = Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode != OP_CUSTOMDATA) {
      const size_t end = at + length;
      size_t next = internal::FirstOperand(tokens, at);
      Operand destination;
      const bool has_destination = next < end && ParseOperand(tokens, next, end, &destination) != 0;
      if (has_destination && destination.type == OUTPUT && destination.index[0] == FLAG_OUTPUT && destination.mode == 0 && ((destination.select >> 3) & 1) != 0) {
        if (opcode != OP_MOV && opcode != OP_MOVC) return {};
        next += destination.length;
        // movc's condition: its first source, not a value.
        if (opcode == OP_MOVC) {
          Operand condition;
          if (ParseOperand(tokens, next, end, &condition) == 0) return {};
          next += condition.length;
        }
        while (next < end) {
          Operand source;
          if (ParseOperand(tokens, next, end, &source) == 0 || source.type != OPERAND_IMMEDIATE) return {};
          // A scalar's value, or w of four: last either way.
          const size_t value_at = source.at + source.length - 1;
          float value = 0.f;
          std::memcpy(&value, &tokens[value_at], 4);
          const auto flag = static_cast<uint32_t>(value * 255.f + 0.5f);
          if (flag > 255) return {};
          const float flagged = static_cast<float>((flag | 4u) & ~16u) / 255.f;
          std::memcpy(&tokens[value_at], &flagged, 4);
          next += source.length;
        }
        flag_written = true;
      }
    }
    at += length;
  }
  if (!flag_written) return {};

  const auto masked = [](uint32_t type, uint32_t mask) { return 2u | (mask << 4) | (type << 12) | (1u << 20); };
  const auto swizzled = [](uint32_t type, uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    return 2u | (1u << 2) | ((x | (y << 2) | (z << 4) | (w << 6)) << 4) | (type << 12) | (1u << 20);
  };
  // cb0's register: two immediate indices.
  const auto camera = [](uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    return 2u | (1u << 2) | ((x | (y << 2) | (z << 4) | (w << 6)) << 4) | (CONSTANT_BUFFER << 12) | (2u << 20);
  };
  constexpr uint32_t NEGATED = 0x80000000u;
  constexpr uint32_t NEGATE = 1u | (1u << 6);
  constexpr uint32_t ONE_FLOAT = 0x3F800000u;
  constexpr uint32_t IMMEDIATE_SCALAR = 1u | (OPERAND_IMMEDIATE << 12);
  const uint32_t temp = (temps_at != 0 ? tokens[temps_at] : 0u);
  const uint32_t r0 = temp;
  const uint32_t r1 = temp + 1;
  const uint32_t r2 = temp + 2;
  std::vector<uint32_t> velocity = {OP_DCL_OUTPUT | (3u << 24), masked(OUTPUT, 0x3), VELOCITY};
  for (const uint32_t reg : {PREVIOUS, CURRENT}) {
    if (!input_declared[reg - PREVIOUS]) {
      velocity.insert(velocity.end(), {OP_DCL_INPUT_PS | (2u << 11) | (3u << 24), masked(INPUT, 0x7), reg});
    }
  }
  if (temps_at != 0) {
    tokens[temps_at] = temp + 3;
  } else {
    velocity.insert(velocity.end(), {OP_DCL_TEMPS | (2u << 24), 3u});
  }
  const auto dp4 = [&](uint32_t mask, uint32_t destination, uint32_t source, uint32_t row) {
    velocity.insert(velocity.end(), {OP_DP4 | (8u << 24), masked(TEMP, mask), destination, swizzled(TEMP, 0, 1, 2, 3), source, camera(0, 1, 2, 3), 0u, row});
  };
  // The game's order: this frame's clip without the jitter, last frame's without its jitter, their difference.
  const auto clip = [&](uint32_t input, uint32_t position, uint32_t into, uint32_t w_mask, uint32_t row_x, uint32_t row_w) {
    velocity.insert(velocity.end(), {OP_MOV | (5u << 24), masked(TEMP, 0x7), position, swizzled(INPUT, 0, 1, 2, 2), input});
    velocity.insert(velocity.end(), {OP_MOV | (5u << 24), masked(TEMP, 0x8), position, IMMEDIATE_SCALAR, ONE_FLOAT});
    dp4(0x1, into, position, row_x);
    dp4(0x2, into, position, row_x + 1);
    dp4(w_mask, r0, position, row_w);
  };
  clip(CURRENT, r0, r1, 0x1, 10, 13);
  // div r0.xy, r1.xyxx, r0.xxxx; add r0.xy, r0.xyxx, -cb0[64].yzyy
  velocity.insert(velocity.end(), {OP_DIV | (7u << 24), masked(TEMP, 0x3), r0, swizzled(TEMP, 0, 1, 0, 0), r1, swizzled(TEMP, 0, 0, 0, 0), r0});
  velocity.insert(velocity.end(), {OP_ADD | (9u << 24), masked(TEMP, 0x3), r0, swizzled(TEMP, 0, 1, 0, 0), r0, camera(1, 2, 1, 1) | NEGATED, NEGATE, 0u, 64u});
  clip(PREVIOUS, r1, r2, 0x4, 14, 17);
  // div r0.zw, r2.xxxy, r0.zzzz; add r0.zw, r0.zzzw, -cb0[65].xxxy
  velocity.insert(velocity.end(), {OP_DIV | (7u << 24), masked(TEMP, 0xC), r0, swizzled(TEMP, 0, 0, 0, 1), r2, swizzled(TEMP, 2, 2, 2, 2), r0});
  velocity.insert(velocity.end(), {OP_ADD | (9u << 24), masked(TEMP, 0xC), r0, swizzled(TEMP, 2, 2, 2, 3), r0, camera(0, 0, 0, 1) | NEGATED, NEGATE, 0u, 65u});
  // add o3.xy, -r0.zwzz, r0.xyxx
  velocity.insert(velocity.end(), {OP_ADD | (8u << 24), masked(OUTPUT, 0x3), VELOCITY, swizzled(TEMP, 2, 3, 2, 2) | NEGATED, NEGATE, r0, swizzled(TEMP, 0, 1, 0, 0), r0});
  tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(first_instruction), velocity.begin(), velocity.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return internal::ReplaceChunks(code, program, input_chunk, output_chunk, inputs, outputs);
}

// A WH3 G-buffer vertex shader that moves by time_in_sec and writes o1.xyz (the pixel shader's previous world position) as 0: its whole
// program again before its `ret`, in temporaries of its own, its outputs into temporaries, with prev_time_in_sec for time_in_sec; o1.xyz
// from that copy's o2.xyz. Every other output stays the game's own bits. Empty: not patched (loops are fine; a second ret, calls, indexed
// temporaries or prev_time_in_sec already read aren't).
inline std::vector<uint8_t> AddPreviousPosition(std::span<const uint8_t> code) {
  constexpr uint32_t OP_MOV = 54;
  constexpr uint32_t OP_RET = 62;
  constexpr uint32_t OP_RETC = 63;
  constexpr uint32_t OP_CALL = 4;
  constexpr uint32_t OP_CALLC = 5;
  constexpr uint32_t OP_LABEL = 44;
  constexpr uint32_t OP_DCL_INDEXABLE_TEMP = 105;
  constexpr uint32_t OP_DCL_FUNCTION_BODY = 147;
  constexpr uint32_t OUTPUT_REGISTERS = 32;
  constexpr uint32_t PREVIOUS = 1;
  constexpr uint32_t CURRENT = 2;
  Program program = Read(code);
  if (program.tokens.empty() || (program.tokens[0] >> 16) != 1) return {};
  std::vector<uint32_t>& tokens = program.tokens;
  size_t temps_at = 0;
  const size_t first_instruction = internal::FirstInstruction(tokens, &temps_at);
  if (first_instruction == 0 || temps_at == 0) return {};
  for (size_t at = 2; at < first_instruction; at += Length(tokens, at)) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_DCL_INDEXABLE_TEMP || opcode == OP_DCL_FUNCTION_BODY) return {};
  }
  // Exactly one ret, the last instruction.
  size_t ret_at = 0;
  for (size_t at = first_instruction; at < tokens.size();) {
    const size_t length = Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_RETC || opcode == OP_CALL || opcode == OP_CALLC || opcode == OP_LABEL || ret_at != 0) return {};
    if (opcode == OP_RET) ret_at = at;
    at += length;
  }
  if (ret_at == 0) return {};

  const uint32_t temps = tokens[temps_at];
  const uint32_t outputs_at = 2 * temps;
  std::vector<uint32_t> copy(tokens.begin() + static_cast<std::ptrdiff_t>(first_instruction), tokens.begin() + static_cast<std::ptrdiff_t>(ret_at));
  for (size_t at = 0; at < copy.size();) {
    const size_t length = Length(copy, at);
    if (length == 0) return {};
    if ((copy[at] & 0x7FF) != OP_CUSTOMDATA) {
      const size_t end = at + length;
      size_t next = internal::FirstOperand(copy, at);
      while (next < end) {
        const size_t moved = internal::MoveOperand(&copy, next, end, temps, outputs_at);
        if (moved == 0) return {};
        next += moved;
      }
    }
    at += length;
  }
  constexpr uint32_t DESTINATION_XYZ = 2u | (0x7u << 4) | (2u << 12) | (1u << 20);  // o#.xyz
  const uint32_t source_xyz = 2u | (1u << 2) | ((0u | (1u << 2) | (2u << 4) | (2u << 6)) << 4) | (0u << 12) | (1u << 20);  // r#.xyzz
  copy.insert(copy.end(), {OP_MOV | (5u << 24), DESTINATION_XYZ, PREVIOUS, source_xyz, outputs_at + CURRENT});
  tokens[temps_at] = outputs_at + OUTPUT_REGISTERS;
  tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(ret_at), copy.begin(), copy.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return Write(code, program);
}

}  // namespace twattila::dxbc
