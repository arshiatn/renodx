/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "sites.h"

// Faster Game CPU Code on the 64-bit port (row ROW_GAME_CODE): hand-made stubs in code near the game, each entered by a
// jump from its site (sites.h SITE_RAW_STUB) and standing on a HELD range of the game's own code. The same results for
// every input, register for register: tests\game_code_test.cpp runs these arrays, placed by the plugin's own init,
// against the game's own bytes.
namespace twattila::attila64_plugin::game_code {

// The half: the game's float-to-half function (0x3009b90), with every register, flag and memory byte at its ret as
// the game's leaves them. x = the 32 bits at [rdi], a = x without its sign:
// - 0x38800000 <= a < 0x47800000 (a normal half): the game's result in straight-line code. Rounding as the game's
//   ("halfway rounds down": the lowest set bit of the 13 low bits is dropped before the add); x - 1 has the same 13 low
//   bits as the game's x + 0x1fff, and the sum without the sign has the same bits 13..22 as the game's with it.
// - a >= 0x47800000 (infinity, also for a NaN) and a < 0x33800000 (zero or the smallest half): the game's values in
//   eax, ecx and esi, and its last flag-setting instruction with the same operands.
// - 0x33800000 <= a < 0x38800000 (a denormal half): the game's own code, a copy of its function placed right behind
//   these bytes and entered after its first instruction (its mov edx, [rdi]; edx holds x already, and eax and ecx are
//   written there before they are read). So [rdi] is read once on every path, as the game reads it.
// The fast path of a software float-to-half conversion is the usual one (as in OpenEXR's half, BSD-3-Clause, or
// Fabian Giesen's float_to_half_fast, public domain): classify by the exponent, add the rounding term, shift. Ours,
// and why: those round to nearest even and keep a NaN, and a caller sees only the result; this one keeps the game's
// own rounding, its infinity for a NaN and its leftover values in ecx, edx and esi, because the game's callers were
// compiled against exactly that function.
// One instruction per line, as assembled (clang, x86-64, Intel syntax); offsets from the stub's start.
inline constexpr std::array<uint8_t, 0x92> HALF = {
    /* +0x00 mov edx, dword ptr [rdi] */ 0x8b, 0x17,
    /* +0x02 mov ecx, edx */ 0x89, 0xd1,
    /* +0x04 and ecx, 0x7fffffff */ 0x81, 0xe1, 0xff, 0xff, 0xff, 0x7f,
    /* +0x0a lea eax, [rcx - 0x38800000] */ 0x8d, 0x81, 0x00, 0x00, 0x80, 0xc7,
    /* +0x10 cmp eax, 0xf000000 */ 0x3d, 0x00, 0x00, 0x00, 0x0f,
    /* +0x15 jae 0x4a */ 0x73, 0x33,
    /* +0x17 lea esi, [rdx - 0x1] */ 0x8d, 0x72, 0xff,
    /* +0x1a and esi, edx */ 0x21, 0xd6,
    /* +0x1c and esi, 0x1fff */ 0x81, 0xe6, 0xff, 0x1f, 0x00, 0x00,
    /* +0x22 add ecx, esi */ 0x01, 0xf1,
    /* +0x24 shr ecx, 0xd */ 0xc1, 0xe9, 0x0d,
    /* +0x27 mov esi, ecx */ 0x89, 0xce,
    /* +0x29 and esi, 0x3ff */ 0x81, 0xe6, 0xff, 0x03, 0x00, 0x00,
    /* +0x2f and ecx, 0xfc00 */ 0x81, 0xe1, 0x00, 0xfc, 0x00, 0x00,
    /* +0x35 add ecx, 0x4000 */ 0x81, 0xc1, 0x00, 0x40, 0x00, 0x00,
    /* +0x3b mov eax, edx */ 0x89, 0xd0,
    /* +0x3d shr eax, 0x10 */ 0xc1, 0xe8, 0x10,
    /* +0x40 and eax, 0x8000 */ 0x25, 0x00, 0x80, 0x00, 0x00,
    /* +0x45 or eax, esi */ 0x09, 0xf0,
    /* +0x47 or eax, ecx */ 0x09, 0xc8,
    /* +0x49 ret */ 0xc3,
    /* +0x4a cmp ecx, 0x47800000 */ 0x81, 0xf9, 0x00, 0x00, 0x80, 0x47,
    /* +0x50 jae 0x78 */ 0x73, 0x26,
    /* +0x52 cmp ecx, 0x33800000 */ 0x81, 0xf9, 0x00, 0x00, 0x80, 0x33,
    /* +0x58 jae 0x94 (the copy + 2) */ 0x73, 0x3a,
    /* +0x5a mov esi, ecx */ 0x89, 0xce,
    /* +0x5c shr esi, 0x17 */ 0xc1, 0xee, 0x17,
    /* +0x5f mov eax, edx */ 0x89, 0xd0,
    /* +0x61 shr eax, 0x10 */ 0xc1, 0xe8, 0x10,
    /* +0x64 and eax, 0x8000 */ 0x25, 0x00, 0x80, 0x00, 0x00,
    /* +0x69 cmp ecx, 0x33000400 */ 0x81, 0xf9, 0x00, 0x04, 0x00, 0x33,
    /* +0x6f seta cl */ 0x0f, 0x97, 0xc1,
    /* +0x72 movzx ecx, cl */ 0x0f, 0xb6, 0xc9,
    /* +0x75 or eax, ecx */ 0x09, 0xc8,
    /* +0x77 ret */ 0xc3,
    /* +0x78 mov esi, ecx */ 0x89, 0xce,
    /* +0x7a shr esi, 0x17 */ 0xc1, 0xee, 0x17,
    /* +0x7d mov eax, edx */ 0x89, 0xd0,
    /* +0x7f shr eax, 0x10 */ 0xc1, 0xe8, 0x10,
    /* +0x82 and eax, 0x8000 */ 0x25, 0x00, 0x80, 0x00, 0x00,
    /* +0x87 mov ecx, edx */ 0x89, 0xd1,
    /* +0x89 shr ecx, 0x17 */ 0xc1, 0xe9, 0x17,
    /* +0x8c or eax, 0x7c00 */ 0x0d, 0x00, 0x7c, 0x00, 0x00,
    /* +0x91 ret */ 0xc3,
};

// A site's stub: its bytes, and whether a copy of the site's HELD range (the game's own code, read at init once the
// range is checked) goes right behind them. No bytes: not built, the site is only checked.
struct Stub {
  const char* word = nullptr;  // in the log's stub line
  std::span<const uint8_t> code;
  bool held_copy_follows = false;
};
inline constexpr std::array<Stub, LABEL_COUNT> STUBS = [] {
  std::array<Stub, LABEL_COUNT> stubs = {};
  stubs[LABEL_HALF_ENTRY] = {.word = "half", .code = HALF, .held_copy_follows = true};
  return stubs;
}();

// Where each built stub's block (its bytes and the copy behind them) lies in the row's code, and that code's size.
struct Layout {
  std::array<uint16_t, LABEL_COUNT> offsets = {};
  std::array<uint16_t, LABEL_COUNT> sizes = {};  // 0: not built
  uint16_t bytes = 0;
};
inline constexpr size_t BLOCK_ALIGNMENT = 64;
inline constexpr Layout LAYOUT = [] {
  Layout layout;
  for (const Site& site : SITES) {
    const Stub& stub = STUBS[site.label];
    if (stub.code.empty()) continue;
    size_t size = stub.code.size();
    for (const HeldRange& range : HELD_RANGES) {
      if (stub.held_copy_follows && range.label == site.label) {
        size += range.hi - range.lo;
      }
    }
    layout.offsets[site.label] = layout.bytes;
    layout.sizes[site.label] = static_cast<uint16_t>(size);
    layout.bytes += static_cast<uint16_t>((size + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1));
  }
  return layout;
}();
static_assert(LAYOUT.sizes[LABEL_HALF_ENTRY] == 0x145, "the half's block: its 0x92 bytes and the game's 0xb3");

// Blocks start on a cache line of their own. In front of the first one a gap keeps the stubs out of the 1 KiB
// subpage of data the loader writes into its page while the game runs (Intel 64 and IA-32 Architectures Optimization
// Reference Manual, "Mixing Code and Data": no writes into the 1 KiB subpage of code that runs). Ours: the manual's
// 2 KiB rule for fetched code is not kept, the loader's 8 KiB page has no room for it; a speed matter only.
inline constexpr size_t LEAD_GAP_BYTES = 0x400;

}  // namespace twattila::attila64_plugin::game_code
