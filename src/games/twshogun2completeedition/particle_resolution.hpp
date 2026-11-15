/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "../../utils/log.hpp"
#include "./engine.hpp"

// Particle resolution: the engine draws particles at a lower resolution while they cost much GPU time (VFX dynamic
// resolution, the effects view: its scale s steps -0.01 per frame while the particles take more than 33% of the frame,
// +0.01 otherwise, between a minimum and 1; viewports int(display x s), scaled to the render size by render_scale.hpp).
// Its minimum (0.3) is a variable only that code reads: raised, smoke and fire never get blurrier than it.
// v17 turned the dynamic resolution off below 100% (smoke stuck on the camera): 80 -> 30 fps in thick smoke (ATN);
// v18 scaled its viewports instead; v19 raised its minimum by a slider; v20 by the render scale (ATN): the particles'
// lowest resolution stays the game's share of the display at any render scale (0.3 / render scale, at most 1).
namespace twshogun2::particle_resolution {

namespace internal {

// comisd xmm0, [budget]; jbe; subss xmm2, [step]; movss xmm1, [minimum]; movaps xmm0, xmm2; call max (the scale's
// step down). +0x1b2cd43 (2026-10-13 build).
constexpr std::string_view MINIMUM_READ =
    "66 0F 2F 05 ?? ?? ?? ?? 76 ?? F3 0F 5C 15 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? 0F 28 C2 E8";

static float* minimum = nullptr;
static float game_minimum = 0.3f;
static std::string status = "not searched";

}  // namespace internal

// Device creation (the engine module is loaded by then).
inline void Install() {
  if (internal::minimum != nullptr) return;
  if (uint8_t* read = engine::FindPattern(internal::MINIMUM_READ); read != nullptr) {
    uint8_t* variable = engine::RipTarget(read + 22, read + 26);
    internal::minimum = reinterpret_cast<float*>(variable);
    internal::game_minimum = *internal::minimum;
    internal::status = std::format("found (minimum +{:#x}, the game's {:.2f})",
                                   variable - reinterpret_cast<uint8_t*>(engine::Module()), internal::game_minimum);
  } else {
    internal::status = "the game's particle code was not found";
  }
  renodx::utils::log::i("[Shogun 2 DLSS] Particle resolution: ", internal::status);
}

// Present: the render size over the display's (1 at 100% or without the render scale).
inline void Apply(float render_fraction) {
  if (internal::minimum == nullptr || render_fraction <= 0.f) return;
  *internal::minimum = std::clamp(internal::game_minimum / render_fraction, internal::game_minimum, 1.f);
}

inline float Minimum() {
  return (internal::minimum != nullptr ? *internal::minimum : 0.f);
}

inline const std::string& Status() {
  return internal::status;
}

}  // namespace twshogun2::particle_resolution
