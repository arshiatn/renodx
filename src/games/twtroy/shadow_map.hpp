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
// (+0x2978 / +0x297c the size as floats, +0x164 the tile, +0x168 the cascades), as Pharaoh's (twpharaohdynasties/shadow_map.hpp)
// without its extreme-quality immediates. The highest quality's row gets the chosen size and half of it; read when the game
// makes its shadows (start-up, Shadows changed). tools\tw\troy\par_t_shadow_rows.py found the table; test\tw_troy_tweaks reads it.
// Two Shadow Cascades Under Rays: the highest quality's row as the lowest's own (quality 1: 2 cascades, the tile the whole size:
// they lie side by side) at the highest's size, and the split distances it reads as quality 1's: a table of 4 floats a row
// (+0x2e81970, right after the sizes) by battle / campaign x quality, (+0x144 x 5 + +0x148) x 4 + cascade (+0x2a04f10,
// +0x2a04fe0); battle quality 1 30 / 120 m against quality 4's 30 / 60 / 150 / 300, campaign 6 / 11 against 4 / 9 / 18 / 35.
namespace twtroy::shadow_map {

namespace internal {

// lea rax, [rip + table]; mov rdx, rcx; xorps xmm0, xmm0; shl rdx, 4; add rdx, rax; mov eax, [rdx + 4];
// cvtsi2ss xmm0, rax; movss [rbx + 0x2978], xmm0
constexpr std::string_view TABLE_READ =
    "48 8D 05 ?? ?? ?? ?? 48 8B D1 0F 57 C0 48 C1 E2 04 48 03 D0 8B 42 04 F3 48 0F 2A C0 F3 0F 11 83 78 29 00 00";
// movsxd rax, [rcx + 0x144]; lea rcx, [rax + rax * 4]; movsxd rax, [rbx + 0x148]; add rcx, rax; lea rax, [rdx + rcx * 4];
// lea rcx, [rip + splits]
constexpr std::string_view SPLITS_READ = "48 63 81 44 01 00 00 48 8D 0C 80 48 63 83 48 01 00 00 48 03 C8 48 8D 04 8A 48 8D 0D";
constexpr size_t TOP_ROW = 4;     // the highest Shadows quality
constexpr size_t LOWEST_ROW = 1;  // the lowest (0: shadows off)
constexpr size_t ROW_SIZE = 16;
constexpr size_t SIZE_FIELD = 4;  // after the cascades; then the tile
constexpr size_t TILE_FIELD = 8;
constexpr size_t QUALITIES = 5;   // split rows a mode
constexpr size_t MODES = 2;       // battle, campaign

static uint8_t* rows = nullptr;    // the sizes table
static uint8_t* splits = nullptr;  // the split distances
static std::array<uint8_t, ROW_SIZE> original = {};                 // the top row
static std::array<uint8_t, ROW_SIZE * MODES> original_splits = {};  // the top row's split distances, battle then campaign
static uint32_t written = 0;  // 0: the game's
static bool two_cascades_written = false;

inline uint32_t Read(const uint8_t* at) {
  uint32_t value;
  std::memcpy(&value, at, sizeof(value));
  return value;
}

inline void Write(uint8_t* at, const void* bytes, size_t size) {
  DWORD protection;
  if (VirtualProtect(at, size, PAGE_READWRITE, &protection) == FALSE) return;
  std::memcpy(at, bytes, size);
  VirtualProtect(at, size, protection, &protection);
}

}  // namespace internal

// The sizes table the shadow setup reads, or null.
inline uint8_t* FindRows(const tw::engine_tweaks::Sections& sections) {
  uint8_t* read = tw::engine_tweaks::FindPattern(sections.code, internal::TABLE_READ);
  if (read == nullptr) return nullptr;
  return tw::engine_tweaks::internal::RipTarget(read + 3, read + 7);
}

// The split distances table the cascades read, or null.
inline uint8_t* FindSplits(const tw::engine_tweaks::Sections& sections) {
  uint8_t* read = tw::engine_tweaks::FindPattern(sections.code, internal::SPLITS_READ);
  if (read == nullptr) return nullptr;
  const size_t lea = internal::SPLITS_READ.size() / 3 + 1;
  return tw::engine_tweaks::internal::RipTarget(read + lea, read + lea + 4);
}

// The highest Shadows setting's shadow map size (0: the game's) and, with `two_cascades`, the lowest setting's 2 cascades,
// from the next time the game makes its shadows. The cascades only where the lowest row is 2 cascades in tiles of its whole
// size (another build or a mod: none written).
inline void Set(uint32_t size, bool two_cascades) {
  if (internal::rows == nullptr) return;
  const uint8_t* lowest = internal::rows + (internal::LOWEST_ROW * internal::ROW_SIZE);
  two_cascades = two_cascades && internal::splits != nullptr && internal::Read(lowest) == 2
                 && internal::Read(lowest + internal::SIZE_FIELD) == internal::Read(lowest + internal::TILE_FIELD);
  if (size == internal::written && two_cascades == internal::two_cascades_written) return;
  std::array<uint8_t, internal::ROW_SIZE> row = internal::original;
  if (size != 0) {
    const uint32_t tile = size / 2;
    std::memcpy(&row[internal::SIZE_FIELD], &size, sizeof(size));
    std::memcpy(&row[internal::TILE_FIELD], &tile, sizeof(tile));
  }
  if (two_cascades) {
    std::memcpy(&row[0], lowest, sizeof(uint32_t));
    std::memcpy(&row[internal::TILE_FIELD], &row[internal::SIZE_FIELD], sizeof(uint32_t));
  }
  internal::Write(internal::rows + (internal::TOP_ROW * internal::ROW_SIZE), row.data(), row.size());
  if (internal::splits != nullptr) {
    for (size_t mode = 0; mode < internal::MODES; ++mode) {
      uint8_t* top = internal::splits + (((mode * internal::QUALITIES) + internal::TOP_ROW) * internal::ROW_SIZE);
      const uint8_t* source = two_cascades ? internal::splits + (((mode * internal::QUALITIES) + internal::LOWEST_ROW) * internal::ROW_SIZE)
                                           : internal::original_splits.data() + (mode * internal::ROW_SIZE);
      internal::Write(top, source, internal::ROW_SIZE);
    }
  }
  internal::written = size;
  internal::two_cascades_written = two_cascades;
}

// At the game's device creation, before it makes its shadows: finds the tables (nothing is written at the game's values).
inline void Install(uint32_t size, bool two_cascades) {
  if (internal::rows == nullptr) {
    const auto sections = tw::engine_tweaks::ModuleSections(GetModuleHandleW(nullptr));
    internal::rows = FindRows(sections);
    if (internal::rows == nullptr) {
      renodx::utils::log::i("[Total War] Graphics: shadow map size not found");
      return;
    }
    std::memcpy(internal::original.data(), internal::rows + (internal::TOP_ROW * internal::ROW_SIZE), internal::original.size());
    internal::splits = FindSplits(sections);
    if (internal::splits != nullptr) {
      for (size_t mode = 0; mode < internal::MODES; ++mode) {
        std::memcpy(internal::original_splits.data() + (mode * internal::ROW_SIZE),
                    internal::splits + (((mode * internal::QUALITIES) + internal::TOP_ROW) * internal::ROW_SIZE), internal::ROW_SIZE);
      }
    }
    renodx::utils::log::i("[Total War] Graphics: shadow map size found, shadow split distances ", internal::splits != nullptr ? "found" : "not found");
  }
  Set(size, two_cascades);
}

// Addon detach: the game's sizes and split distances back.
inline void Uninstall() {
  Set(0, false);
}

}  // namespace twtroy::shadow_map
