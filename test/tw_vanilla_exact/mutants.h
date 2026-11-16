// Deliberately wrong copies of a game's shader bytes (hdr-review-rules.md, "Vanilla exact first": a wrong copy must
// fail), re-signed so the runtime takes them: tw_vanilla_exact's control and constant mutants, tw_psychov35 part 6's.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "src/games/twattila/dxbc.hpp"

namespace mutants {

// The instructions of a program after its declarations, as (offset, length).
inline std::vector<std::pair<size_t, size_t>> Instructions(const std::vector<uint32_t>& tokens) {
  std::vector<std::pair<size_t, size_t>> list;
  size_t temps_at = 0;
  size_t at = twattila::dxbc::internal::FirstInstruction(tokens, &temps_at);
  while (at != 0 && at < tokens.size()) {
    const size_t length = twattila::dxbc::Length(tokens, at);
    if (length == 0) break;
    if ((tokens[at] & 0x7FF) != twattila::dxbc::OP_CUSTOMDATA) list.emplace_back(at, length);
    at += length;
  }
  return list;
}

// The game's bytes with every write to an output or a UAV reading other components (whichever branch runs): its first
// register source's x and y swapped (a swizzle whose x and y differ), else every component the next one (a replicated
// swizzle, or one component selected). Empty: no such write.
inline std::vector<uint8_t> SwapOutputSwizzle(const std::vector<uint8_t>& code) {
  constexpr uint32_t OUTPUT = 2;
  constexpr uint32_t UAV = 30;
  twattila::dxbc::Program program = twattila::dxbc::Read(code);
  if (program.tokens.empty()) return {};
  bool changed = false;
  for (const auto [at, length] : Instructions(program.tokens)) {
    const size_t end = at + length;
    size_t next = twattila::dxbc::internal::FirstOperand(program.tokens, at);
    twattila::dxbc::Operand destination;
    if (twattila::dxbc::ParseOperand(program.tokens, next, end, &destination) == 0) continue;
    if (destination.type != OUTPUT && destination.type != UAV) continue;
    next += destination.length;
    while (next < end) {
      twattila::dxbc::Operand source;
      if (twattila::dxbc::ParseOperand(program.tokens, next, end, &source) == 0) break;
      next += source.length;
      if (source.type == twattila::dxbc::OPERAND_IMMEDIATE || (source.mode != 1 && source.mode != 2)) continue;
      uint32_t select = source.select;
      const uint32_t first = select & 3;
      const uint32_t second = (select >> 2) & 3;
      if (source.mode == 2) {
        select = (select & ~3u) | ((first + 1) & 3);
      } else if (first != second) {
        select = (select & ~0xFu) | second | (first << 2);
      } else {
        select = 0;
        for (uint32_t c = 0; c < 4; ++c) select |= (((source.select >> (2 * c)) + 1) & 3) << (2 * c);
      }
      program.tokens[source.at] = (program.tokens[source.at] & ~(0xFFu << 4)) | (select << 4);
      changed = true;
      break;
    }
  }
  return changed ? twattila::dxbc::Write(code, program) : std::vector<uint8_t>{};
}

// Every float constant of the program's arithmetic (an immediate of add, div, dp2-4, eq, ge, lt, mad, min, max, mul,
// ne): its token, one mutant each.
inline std::vector<size_t> FloatConstants(const std::vector<uint32_t>& tokens) {
  constexpr std::array<uint32_t, 13> ARITHMETIC = {0, 14, 15, 16, 17, 24, 29, 49, 50, 51, 52, 56, 57};
  std::vector<size_t> list;
  for (const auto [at, length] : Instructions(tokens)) {
    if (std::find(ARITHMETIC.begin(), ARITHMETIC.end(), tokens[at] & 0x7FF) == ARITHMETIC.end()) continue;
    const size_t end = at + length;
    size_t next = twattila::dxbc::internal::FirstOperand(tokens, at);
    while (next < end) {
      twattila::dxbc::Operand operand;
      if (twattila::dxbc::ParseOperand(tokens, next, end, &operand) == 0) break;
      if (operand.type == twattila::dxbc::OPERAND_IMMEDIATE && (tokens[operand.at] >> 31) == 0) {
        const size_t count = ((tokens[operand.at] & 3) == 1) ? 1 : 4;
        for (size_t c = 0; c < count; ++c) {
          const float value = std::bit_cast<float>(tokens[operand.at + 1 + c]);
          if (std::isfinite(value) && std::abs(value) > 1e-30f) list.push_back(operand.at + 1 + c);
        }
      }
      next += operand.length;
    }
  }
  return list;
}

}  // namespace mutants
