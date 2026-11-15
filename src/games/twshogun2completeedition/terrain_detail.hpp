/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "../../utils/log.hpp"
#include "./engine.hpp"

// Terrain detail (graphics option, experimental, ATN): the battle terrain's cells pick their level of detail by their
// distance in cells from the camera (+0x1b4f697, the grid after debug_terrain_culling_v2). The engine's debug tweaks
// enable_force_lod (bool) and force_lod (int, 0) replace that with one level for every cell: the cell's own base + 0,
// its finest. Their addresses come from the code that reads them.
namespace twshogun2::terrain_detail {

namespace internal {

// cmp byte [rip + enable_force_lod], 0; mov esi, [rbx + 0x48]; mov ecx, [rbx + 0x54]; mov edx, [rbx + 0x4c]; je;
// add ecx, [rip + force_lod]. +0x1b4f998 (2026-10-13 build).
constexpr std::string_view FORCE_LOD_READ = "80 3D ?? ?? ?? ?? 00 8B 73 48 8B 4B 54 8B 53 4C 74 ?? 03 0D ?? ?? ?? ??";
constexpr size_t ADD_FORCE_LOD = 18;

static uint8_t* enable_force_lod = nullptr;
static int32_t* force_lod = nullptr;
static std::string status = "not searched";

}  // namespace internal

// Device creation (the engine module is loaded by then).
inline void Install() {
  if (internal::enable_force_lod != nullptr) return;
  if (uint8_t* read = engine::FindPattern(internal::FORCE_LOD_READ); read != nullptr) {
    internal::enable_force_lod = engine::RipTarget(read + 2, read + 7);
    uint8_t* add = read + internal::ADD_FORCE_LOD;
    internal::force_lod = reinterpret_cast<int32_t*>(engine::RipTarget(add + 2, add + 6));
    internal::status = std::format("found (enable_force_lod +{:#x}, force_lod +{:#x})",
                                   internal::enable_force_lod - reinterpret_cast<uint8_t*>(engine::Module()),
                                   reinterpret_cast<uint8_t*>(internal::force_lod) - reinterpret_cast<uint8_t*>(engine::Module()));
  } else {
    internal::status = "the game's terrain code was not found";
  }
  renodx::utils::log::i("[Shogun 2 DLSS] Terrain detail: ", internal::status);
}

// Present: finest everywhere, or the game's distance levels.
inline void Apply(bool finest) {
  if (internal::enable_force_lod == nullptr) return;
  *internal::enable_force_lod = (finest ? 1 : 0);
  if (finest) {
    *internal::force_lod = 0;
  }
}

inline const std::string& Status() {
  return internal::status;
}

}  // namespace twshogun2::terrain_detail
