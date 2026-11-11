/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

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

// Model detail distance (graphics option): rigid models (.rigid_model_v2: grass clumps, plants, rocks, props,
// buildings) carry a distance per level of detail in their file (the default grass_hi: 25, 50 and 100 m; ATN: grass
// gets more detailed while the camera nears it). The loader stores them squared at LOD +0x70 (+0x1b37825); the engine
// picks a level by comparing the camera distance squared, corrected for the camera's zoom (rigid_fov_scale), with
// them (+0x1b26470), and fades between levels with the same comparison (+0x1b265b0). Both get the distance squared
// divided by scale^2: every level reaches scale times farther.
namespace twshogun2::model_detail {

namespace internal {

// void* SelectLod(model, float distance_squared, void** next_lod, uint32_t max_lod). +0x1b26470 (2026-10-13 build).
constexpr std::string_view SELECT_LOD =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 33 DB 0F 29 74 24 20 49 89 18 "
    "41 8B F9 4D 8B F0 0F 28 F1 48 8B F1 39 59 3C";
// float LodFade(model, float distance_squared). +0x1b265b0.
constexpr std::string_view LOD_FADE = "40 53 48 83 EC 30 83 79 3C 00 48 8B D9 0F 29 74 24 20 0F 28 F1 0F 84";

using SelectLod = void* (*)(void* model, float distance_squared, void** next_lod, uint32_t max_lod);
using LodFade = float (*)(void* model, float distance_squared);

static SelectLod original_select_lod = nullptr;
static LodFade original_lod_fade = nullptr;
static std::vector<renodx::utils::detour::Function> hooks;
static tw::native_hooks::Lifetime hook_lifetime;
static bool hook_failed = false;
static std::string status = "not searched";
static std::atomic<float> inverse_scale_squared = 1.f;

inline void* OnSelectLod(void* model, float distance_squared, void** next_lod, uint32_t max_lod) {
  return original_select_lod(model, distance_squared * inverse_scale_squared.load(std::memory_order_relaxed), next_lod,
                             max_lod);
}

inline float OnLodFade(void* model, float distance_squared) {
  return original_lod_fade(model, distance_squared * inverse_scale_squared.load(std::memory_order_relaxed));
}

}  // namespace internal

// Device creation (the engine module is loaded by then). Again after a detach that removed the hooks (frame generation
// keeps the addon's image loaded, and a new device attaches the addon a second time in it); not after a hook that failed.
inline void Install() {
  if (internal::hook_failed || !internal::hooks.empty()) return;
  uint8_t* select_lod = engine::FindPattern(internal::SELECT_LOD);
  uint8_t* lod_fade = engine::FindPattern(internal::LOD_FADE);
  if (select_lod == nullptr || lod_fade == nullptr) {
    internal::status = std::format("the game's model LOD code was not found (select {}, fade {})",
                                   static_cast<void*>(select_lod), static_cast<void*>(lod_fade));
  } else {
    internal::original_select_lod = reinterpret_cast<internal::SelectLod>(select_lod);
    internal::original_lod_fade = reinterpret_cast<internal::LodFade>(lod_fade);
    internal::hooks = {
        renodx::utils::detour::Function(&internal::original_select_lod, &internal::OnSelectLod, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE),
        renodx::utils::detour::Function(&internal::original_lod_fade, &internal::OnLodFade, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE),
    };
    try {
      internal::hook_lifetime.Install(internal::hooks);
      const auto* base = reinterpret_cast<uint8_t*>(engine::Module());
      internal::status = std::format("hooked (select +{:#x}, fade +{:#x})", select_lod - base, lod_fade - base);
    } catch (const std::exception& error) {
      internal::hooks.clear();
      internal::hook_failed = true;
      internal::status = std::format("hook failed: {}", error.what());
    }
  }
  renodx::utils::log::i("[Shogun 2 DLSS] Model detail distance: ", internal::status);
}

inline void Uninstall() {
  if (internal::hooks.empty()) return;
  try {
    internal::hook_lifetime.Uninstall(&internal::hooks);
  } catch (const std::exception& error) {
    renodx::utils::log::e("[Shogun 2 DLSS] Model detail hooks could not be removed; addon retained until process exit: ", error.what());
  }
}

// Overlay: every level of detail reaches scale times farther (1: the game's).
inline void SetScale(float scale) {
  internal::inverse_scale_squared.store(1.f / (scale * scale), std::memory_order_relaxed);
}

inline const std::string& Status() {
  return internal::status;
}

}  // namespace twshogun2::model_detail
