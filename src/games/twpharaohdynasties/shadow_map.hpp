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
constexpr std::array<size_t, 2> TOP_ROWS = {4, 5};  // the highest Shadows qualities
constexpr size_t ROWS_SIZE = ROW_SIZE * TOP_ROWS.size();
constexpr size_t SIZE_FIELD = 4;
constexpr size_t TILE_FIELD = 8;

static uint8_t* rows = nullptr;     // the table's rows 4 and 5
static uint8_t* extreme = nullptr;  // the immediates
static std::array<uint8_t, ROWS_SIZE> original_rows = {};
static std::array<uint8_t, EXTREME_SIZE> original_extreme = {};
static uint32_t written = 0;  // 0: the game's

}  // namespace internal

// The highest Shadows settings' shadow map size (0: the game's) from the next time the game makes its shadows.
inline void Set(uint32_t size) {
  if (internal::rows == nullptr || size == internal::written) return;
  std::array<uint8_t, internal::ROWS_SIZE> rows = internal::original_rows;
  std::array<uint8_t, internal::EXTREME_SIZE> extreme = internal::original_extreme;
  if (size != 0) {
    const uint32_t tile = size / 2;
    const float size_float = static_cast<float>(size);
    for (size_t row = 0; row < internal::TOP_ROWS.size(); ++row) {
      std::memcpy(&rows[(row * internal::ROW_SIZE) + internal::SIZE_FIELD], &size, sizeof(size));
      std::memcpy(&rows[(row * internal::ROW_SIZE) + internal::TILE_FIELD], &tile, sizeof(tile));
    }
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
  internal::written = size;
}

// At the game's device creation, before it makes its shadows: finds the table and the immediates.
inline void Install(uint32_t size) {
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
    internal::rows = read + 7 + offset + (internal::TOP_ROWS[0] * internal::ROW_SIZE);
    internal::extreme = extreme;
    std::memcpy(internal::original_rows.data(), internal::rows, internal::ROWS_SIZE);
    std::memcpy(internal::original_extreme.data(), internal::extreme, internal::EXTREME_SIZE);
    renodx::utils::log::i("[Total War] Graphics: shadow map size found");
  }
  Set(size);
}

// Addon detach: the game's sizes back.
inline void Uninstall() {
  Set(0);
}

}  // namespace twpharaohdynasties::shadow_map
