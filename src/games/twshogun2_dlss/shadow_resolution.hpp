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

// Shadow map resolution (graphics option): the sun's shadow cascades are 4096 x 4096 at the highest Shadows setting
// (2048 / 1500 below), times the engine's tweak shadow_resolution_multiplier (int, 1; ShadowMapView.cpp). The game
// reads it when it creates the shadow map (+0x1a77b90: start-up, Shadows changed in its options).
namespace twshogun2::shadow_resolution {

namespace internal {

// mov eax, [rip + shadow_resolution_multiplier]; xorps xmm0, xmm0; shl eax, 12; cvtsi2ss xmm0, rax (the 4096 x it of
// the highest setting). +0x1a77c27 (2026-10-13 build).
constexpr std::string_view MULTIPLIER_READ = "8B 05 ?? ?? ?? ?? 0F 57 C0 C1 E0 0C F3 48 0F 2A C0";

static int32_t* multiplier = nullptr;
static std::string status = "not searched";

}  // namespace internal

// Device creation (the engine module is loaded by then, before the game creates its shadow map).
inline void Install() {
  if (internal::multiplier != nullptr) return;
  if (uint8_t* read = engine::FindPattern(internal::MULTIPLIER_READ); read != nullptr) {
    internal::multiplier = reinterpret_cast<int32_t*>(engine::RipTarget(read + 2, read + 6));
    internal::status = std::format("found (shadow_resolution_multiplier +{:#x})",
                                   reinterpret_cast<uint8_t*>(internal::multiplier)
                                       - reinterpret_cast<uint8_t*>(engine::Module()));
  } else {
    internal::status = "the game's shadow map code was not found";
  }
  renodx::utils::log::i("[Shogun 2 DLSS] Shadow map resolution: ", internal::status);
}

// The shadow map's size x this from the next time the game creates it.
inline void Set(int32_t multiplier) {
  if (internal::multiplier == nullptr) return;
  *internal::multiplier = multiplier;
}

inline const std::string& Status() {
  return internal::status;
}

}  // namespace twshogun2::shadow_resolution
