/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"

// Shadow Map Size: the sun's shadow map, one depth texture of size x size holding the cascades in tiles of half its
// size (D3D11: 16384 at most). The shadow setup (+0x31e51ce) takes them per Shadows quality from a table in .rdata
// (+0x3da08a0: cascades, size, tile, a float; quality 4: 6144 / 3072, 5: 8192 / 4096) and, at quality 4 with the
// engine's shadowmap_extreme_quality, 8192 / 4096 as immediates (+0x31e5271). Both qualities' rows and the immediates
// get the chosen size and half of it; read when the game makes its shadows (start-up, Shadows changed).
// Three Shadow Cascades Under Rays: the same rows' cascade count (qualities 3-5: 4; Medium, quality 2: 3) becomes Medium's.
// The setup stores it at +0x168 and the cascade getter (+0x31d9bc0) reads the table too; the last cascade's split (the
// battle's 40 / 100 / 250 / 600 m at quality 5, the campaign's 5 / 10 / 20 / 40) is the shadow distance, so it ends at the
// third split.
namespace twpharaohdynasties::shadow_map {

namespace internal {

// lea rax, [rip + table]; add rdx, rax; mov eax, [rdx + 4]; cvtsi2ss xmm0, rax; movss [rbx + 0x2a50], xmm0
constexpr std::string_view TABLE_READ =
    "48 8D 05 ?? ?? ?? ?? 48 03 D0 8B 42 04 F3 48 0F 2A C0 F3 0F 11 83 50 2A 00 00";
// mov dword ptr [rbx + 0x2a50], 8192.f; mov dword ptr [rbx + 0x2a54], 8192.f; mov dword ptr [rbx + 0x164], 4096
constexpr std::string_view EXTREME =
    "C7 83 50 2A 00 00 00 00 00 46 C7 83 54 2A 00 00 00 00 00 46 C7 83 64 01 00 00 00 10 00 00";
constexpr size_t EXTREME_SIZE = 30;
constexpr std::array<size_t, 2> EXTREME_FLOATS = {6, 16};  // the width and height (floats)
constexpr size_t EXTREME_TILE = 26;
constexpr size_t ROW_SIZE = 16;
constexpr size_t MEDIUM_ROW = 2;  // the first row kept: Medium, whose cascade count the top rows take
constexpr std::array<size_t, 2> SIZE_ROWS = {4, 5};  // the highest Shadows qualities: Shadow Map Size
constexpr std::array<size_t, 3> CASCADE_ROWS = {3, 4, 5};  // the qualities above Medium: Three Shadow Cascades
constexpr size_t ROWS_SIZE = ROW_SIZE * 4;  // rows 2-5
constexpr size_t CASCADES_FIELD = 0;
constexpr size_t SIZE_FIELD = 4;
constexpr size_t TILE_FIELD = 8;

static uint8_t* rows = nullptr;     // the table's rows 2 to 5
static uint8_t* extreme = nullptr;  // the immediates
static std::array<uint8_t, ROWS_SIZE> original_rows = {};
static std::array<uint8_t, EXTREME_SIZE> original_extreme = {};
static uint32_t written_size = 0;  // 0: the game's
static bool written_cascades = false;

}  // namespace internal

// The highest Shadows settings' shadow map size (0: the game's) and, with `medium_cascades`, Medium's cascade count above
// Medium (only in a row with more cascades than Medium, where Medium has any), from the next time the game makes its shadows.
inline void Set(uint32_t size, bool medium_cascades) {
  if (internal::rows == nullptr || (size == internal::written_size && medium_cascades == internal::written_cascades)) return;
  std::array<uint8_t, internal::ROWS_SIZE> rows = internal::original_rows;
  const auto field = [&rows](size_t row, size_t offset) {
    return &rows[((row - internal::MEDIUM_ROW) * internal::ROW_SIZE) + offset];
  };
  if (medium_cascades) {
    int32_t medium;
    std::memcpy(&medium, field(internal::MEDIUM_ROW, internal::CASCADES_FIELD), sizeof(medium));
    for (const size_t row : internal::CASCADE_ROWS) {
      int32_t cascades;
      std::memcpy(&cascades, field(row, internal::CASCADES_FIELD), sizeof(cascades));
      if (medium > 0 && medium < cascades) std::memcpy(field(row, internal::CASCADES_FIELD), &medium, sizeof(medium));
    }
  }
  std::array<uint8_t, internal::EXTREME_SIZE> extreme = internal::original_extreme;
  if (size != 0) {
    const uint32_t tile = size / 2;
    for (const size_t row : internal::SIZE_ROWS) {
      std::memcpy(field(row, internal::SIZE_FIELD), &size, sizeof(size));
      std::memcpy(field(row, internal::TILE_FIELD), &tile, sizeof(tile));
    }
    const float size_float = static_cast<float>(size);
    for (const size_t offset : internal::EXTREME_FLOATS) {
      std::memcpy(&extreme[offset], &size_float, sizeof(size_float));
    }
    std::memcpy(&extreme[internal::EXTREME_TILE], &tile, sizeof(tile));
  }
  const auto write = [](uint8_t* at, const void* bytes, size_t size) {
    DWORD protection;
    if (VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &protection) == FALSE) return;
    std::memcpy(at, bytes, size);
    VirtualProtect(at, size, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), at, size);
  };
  write(internal::rows, rows.data(), rows.size());
  write(internal::extreme, extreme.data(), extreme.size());
  internal::written_size = size;
  internal::written_cascades = medium_cascades;
}

// At the game's device creation, before it makes its shadows: finds the table and the immediates.
inline void Install(uint32_t size, bool medium_cascades) {
  if (internal::rows == nullptr) {
    const auto sections = tw::engine_tweaks::ModuleSections(GetModuleHandleW(nullptr));
    uint8_t* read = tw::engine_tweaks::FindPattern(sections.code, internal::TABLE_READ);
    uint8_t* extreme = tw::engine_tweaks::FindPattern(sections.code, internal::EXTREME);
    if (read == nullptr || extreme == nullptr) {
      renodx::utils::log::i("[Total War] Graphics: shadow map size not found");
      return;
    }
    int32_t offset;
    std::memcpy(&offset, read + 3, sizeof(offset));
    internal::rows = read + 7 + offset + (internal::MEDIUM_ROW * internal::ROW_SIZE);
    internal::extreme = extreme;
    std::memcpy(internal::original_rows.data(), internal::rows, internal::ROWS_SIZE);
    std::memcpy(internal::original_extreme.data(), internal::extreme, internal::EXTREME_SIZE);
    renodx::utils::log::i("[Total War] Graphics: shadow map size found");
  }
  Set(size, medium_cascades);
}

// Addon detach: the game's sizes and cascades back.
inline void Uninstall() {
  Set(0, false);
}

}  // namespace twpharaohdynasties::shadow_map
