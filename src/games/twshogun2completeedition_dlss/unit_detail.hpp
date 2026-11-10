/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "../twattila/native_hook_lifetime.hpp"
#include "./engine.hpp"

// Unit detail (graphics options, as Attila's): float* UnitDistances(display, float* out, int quality, bool, bool)
// (+0x3a33a0) fills the battle units' distances, squared, for the Unit Detail setting (or the ENTITY_LOD_OVERRIDE
// tweaks): [0] SHARED_POSE_DISTANCE (soldiers copy each other's animation beyond it), [1] NO_VARIANT_DISTANCE (they
// look alike beyond it), [2] IMPOSTER_DISTANCE and [3] LARGE_IMPOSTER_DISTANCE (flat sprites beyond them),
// [4] shared pose x FEATHERED_SHARED_POSE_RANGE, [5] FACIAL_ANIMATION_DISTANCE. Its result x the sliders squared.
namespace twshogun2::unit_detail {

namespace internal {

constexpr std::string_view UNIT_DISTANCES =
    "40 53 48 81 EC 80 00 00 00 80 BC 24 B0 00 00 00 00 48 8B DA 0F 29 74 24 70 0F 29 7C 24 60 44 0F 29 44 24 50 "
    "44 0F 29 4C 24 40 44 0F 29 54 24 30 44 0F 29 5C 24 20 C7 02";

using UnitDistances = float* (*)(void* display, float* out, int32_t quality, bool second, bool third);

static UnitDistances original_unit_distances = nullptr;
static std::vector<renodx::utils::detour::Function> hooks;
static tw::native_hooks::Lifetime hook_lifetime;
static std::string status = "not searched";
static std::atomic<float> imposter_squared = 1.f;
static std::atomic<float> animation_squared = 1.f;
static std::atomic<float> variety_squared = 1.f;

inline float* OnUnitDistances(void* display, float* out, int32_t quality, bool second, bool third) {
  float* distances = original_unit_distances(display, out, quality, second, third);
  const float animation = animation_squared.load(std::memory_order_relaxed);
  const float imposter = imposter_squared.load(std::memory_order_relaxed);
  distances[0] *= animation;
  distances[1] *= variety_squared.load(std::memory_order_relaxed);
  distances[2] *= imposter;
  distances[3] *= imposter;
  distances[4] *= animation;
  distances[5] *= animation;
  return distances;
}

}  // namespace internal

// Device creation (the engine module is loaded by then).
inline void Install() {
  if (!internal::hooks.empty() || internal::original_unit_distances != nullptr) return;
  uint8_t* unit_distances = engine::FindPattern(internal::UNIT_DISTANCES);
  if (unit_distances == nullptr) {
    internal::status = "the game's unit distance code was not found";
  } else {
    internal::original_unit_distances = reinterpret_cast<internal::UnitDistances>(unit_distances);
    internal::hooks = {renodx::utils::detour::Function(&internal::original_unit_distances, &internal::OnUnitDistances)};
    try {
      internal::hook_lifetime.Install(internal::hooks);
      internal::status = std::format("hooked (+{:#x})", unit_distances - reinterpret_cast<uint8_t*>(engine::Module()));
    } catch (const std::exception& error) {
      internal::hooks.clear();
      internal::status = std::format("hook failed: {}", error.what());
    }
  }
  renodx::utils::log::i("[Shogun 2 DLSS] Unit detail: ", internal::status);
}

inline void Uninstall() {
  if (internal::hooks.empty()) return;
  try {
    internal::hook_lifetime.Uninstall(&internal::hooks);
  } catch (const std::exception& error) {
    renodx::utils::log::e("[Shogun 2 DLSS] Unit detail hooks could not be removed; addon retained until process exit: ", error.what());
  }
}

// Overlay: the distances x these (1: the game's).
inline void SetScales(float imposter, float animation, float variety) {
  internal::imposter_squared.store(imposter * imposter, std::memory_order_relaxed);
  internal::animation_squared.store(animation * animation, std::memory_order_relaxed);
  internal::variety_squared.store(variety * variety, std::memory_order_relaxed);
}

inline const std::string& Status() {
  return internal::status;
}

}  // namespace twshogun2::unit_detail
