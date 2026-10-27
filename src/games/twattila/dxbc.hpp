/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// DXBC (Shader Model 4 / 5 bytecode) for the shaders the addon patches as the game creates them
// (screen_space_shadows.hpp's sun light, grass_fade.hpp's grass): the program's tokens out and back in.
namespace twattila::dxbc {

constexpr uint32_t OP_CUSTOMDATA = 53;

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

}  // namespace twattila::dxbc
