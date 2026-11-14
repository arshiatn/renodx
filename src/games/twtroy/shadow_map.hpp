/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"

// Shadow Map Size: the sun's shadow atlas, one depth texture of size x size holding 2 x 2 cascades in tiles of half its size
// (ATN's traces: 6144 x 6144, 3072 tiles). Troy's shadow setup (+0x2a07b8a) takes them per Shadows quality from a table
// in its data (+0x2e81920: cascades, size, tile, the shadow LOD scale; quality 4: 4, 6144, 3072) into the shadow object
// (+0x2978 / +0x297c the size as floats, +0x164 the tile), as Pharaoh's (twpharaohdynasties/shadow_map.hpp) without its
// extreme-quality immediates. The highest quality's row gets the chosen size and half of it; read when the game makes its
// shadows (start-up, Shadows changed). tools\tw\troy\par_t_shadow_rows.py found the table; test\tw_troy_tweaks reads it.
namespace twtroy::shadow_map {

namespace internal {

// lea rax, [rip + table]; mov rdx, rcx; xorps xmm0, xmm0; shl rdx, 4; add rdx, rax; mov eax, [rdx + 4];
// cvtsi2ss xmm0, rax; movss [rbx + 0x2978], xmm0
constexpr std::string_view TABLE_READ =
    "48 8D 05 ?? ?? ?? ?? 48 8B D1 0F 57 C0 48 C1 E2 04 48 03 D0 8B 42 04 F3 48 0F 2A C0 F3 0F 11 83 78 29 00 00";
constexpr size_t TOP_ROW = 4;  // the highest Shadows quality
constexpr size_t ROW_SIZE = 16;
constexpr size_t SIZE_FIELD = 4;  // then the tile

static uint8_t* sizes = nullptr;  // the top row's size and tile
static std::array<uint8_t, 8> original = {};
static uint32_t written = 0;  // 0: the game's

}  // namespace internal

// The top row's size and tile in the table the shadow setup reads, or null.
inline uint8_t* FindSizes(const tw::engine_tweaks::Sections& sections) {
  uint8_t* read = tw::engine_tweaks::FindPattern(sections.code, internal::TABLE_READ);
  if (read == nullptr) return nullptr;
  int32_t offset;
  std::memcpy(&offset, read + 3, sizeof(offset));
  return read + 7 + offset + (internal::TOP_ROW * internal::ROW_SIZE) + internal::SIZE_FIELD;
}

// The highest Shadows setting's shadow map size (0: the game's) from the next time the game makes its shadows.
inline void Set(uint32_t size) {
  if (internal::sizes == nullptr || size == internal::written) return;
  std::array<uint8_t, 8> sizes = internal::original;
  if (size != 0) {
    const uint32_t tile = size / 2;
    std::memcpy(&sizes[0], &size, sizeof(size));
    std::memcpy(&sizes[4], &tile, sizeof(tile));
  }
  DWORD protection;
  if (VirtualProtect(internal::sizes, sizes.size(), PAGE_READWRITE, &protection) == FALSE) return;
  std::memcpy(internal::sizes, sizes.data(), sizes.size());
  VirtualProtect(internal::sizes, sizes.size(), protection, &protection);
  internal::written = size;
}

// At the game's device creation, before it makes its shadows: finds the table (nothing is written at the game's size).
inline void Install(uint32_t size) {
  if (internal::sizes == nullptr) {
    internal::sizes = FindSizes(tw::engine_tweaks::ModuleSections(GetModuleHandleW(nullptr)));
    if (internal::sizes == nullptr) {
      renodx::utils::log::i("[Total War] Graphics: shadow map size not found");
      return;
    }
    std::memcpy(internal::original.data(), internal::sizes, internal::original.size());
    renodx::utils::log::i("[Total War] Graphics: shadow map size found");
  }
  Set(size);
}

// Addon detach: the game's sizes back.
inline void Uninstall() {
  Set(0);
}

}  // namespace twtroy::shadow_map
