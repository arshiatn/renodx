/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>

#include "../../utils/cross_addon.hpp"

// Bandai's screen filters before the TAA (painterly blurs, outlines, shadow edge tint, sharpening) are sized in render
// pixels, so they grow on screen as DLSS's render scale drops. The DLSS addon shares the scale (render / output, 1 at
// 100%), whether the game's sharpening stays on, its Ambient occlusion choice (0 the game's, 1 reduced, 2 darker) and
// the sun shadow distance relative to the game's (its penumbras widen with it); the RenoDX HDR addon's replacements of
// those passes read them. Shared by both addons (cross_addon): SharedData is
// append-only.
namespace talesofarise::post_filters {

struct __declspec(uuid("9c1b57b2-0a66-457f-802a-b47ba2c2259a")) SharedData {
  std::atomic<float> scale = 1.f;
  std::atomic<float> sharpening = 1.f;
  std::atomic<float> ambient_occlusion = 1.f;
  std::atomic<float> sun_shadow_distance = 1.f;
  // Appended: Ambient occlusion GT-VBAO (the HDR addon's bitmask_ao.hlsli).
  std::atomic<float> ao_method = 0.f;
  std::atomic<float> ao_radius = 1.f;
  std::atomic<float> ao_strength = 1.f;
  std::atomic<float> ao_thickness = 0.15f;
  std::atomic<float> ao_directions = 1.f;
  std::atomic<float> ao_quality = 1.f;
  std::atomic<float> ao_frame = 0.f;
};

static renodx::utils::cross_addon::Shared<SharedData> shared;

inline void Use(uint32_t fdw_reason) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      shared.RegisterModule();
      break;
    case DLL_PROCESS_DETACH:
      shared.UnregisterModule();
      break;
    default:
      break;
  }
}

struct Filters {
  float scale = 1.f;
  float sharpening = 1.f;
  float ambient_occlusion = 1.f;
  float sun_shadow_distance = 1.f;
  float ao_method = 0.f;  // 0 the game's, 1 GT-VBAO
  float ao_radius = 1.f;  // metres
  float ao_strength = 1.f;
  float ao_thickness = 0.15f;  // metres
  float ao_directions = 1.f;   // 2^this
  float ao_quality = 1.f;      // 8 x 2^this samples per side
  float ao_frame = 0.f;        // 0-63, the noise's frame
};

// DLSS addon: every present; scale and sharpening 1 while it does not upscale, all 1 once it is unloaded.
inline void Store(const Filters& filters) {
  if (shared.data == nullptr) return;
  shared.data->scale.store(filters.scale, std::memory_order_relaxed);
  shared.data->sharpening.store(filters.sharpening, std::memory_order_relaxed);
  shared.data->ambient_occlusion.store(filters.ambient_occlusion, std::memory_order_relaxed);
  shared.data->sun_shadow_distance.store(filters.sun_shadow_distance, std::memory_order_relaxed);
  shared.data->ao_method.store(filters.ao_method, std::memory_order_relaxed);
  shared.data->ao_radius.store(filters.ao_radius, std::memory_order_relaxed);
  shared.data->ao_strength.store(filters.ao_strength, std::memory_order_relaxed);
  shared.data->ao_thickness.store(filters.ao_thickness, std::memory_order_relaxed);
  shared.data->ao_directions.store(filters.ao_directions, std::memory_order_relaxed);
  shared.data->ao_quality.store(filters.ao_quality, std::memory_order_relaxed);
  shared.data->ao_frame.store(filters.ao_frame, std::memory_order_relaxed);
}


// HDR addon: before each of those passes.
inline Filters Load() {
  if (shared.data == nullptr) return {};
  return {
      .scale = shared.data->scale.load(std::memory_order_relaxed),
      .sharpening = shared.data->sharpening.load(std::memory_order_relaxed),
      .ambient_occlusion = shared.data->ambient_occlusion.load(std::memory_order_relaxed),
      .sun_shadow_distance = shared.data->sun_shadow_distance.load(std::memory_order_relaxed),
      .ao_method = shared.data->ao_method.load(std::memory_order_relaxed),
      .ao_radius = shared.data->ao_radius.load(std::memory_order_relaxed),
      .ao_strength = shared.data->ao_strength.load(std::memory_order_relaxed),
      .ao_thickness = shared.data->ao_thickness.load(std::memory_order_relaxed),
      .ao_directions = shared.data->ao_directions.load(std::memory_order_relaxed),
      .ao_quality = shared.data->ao_quality.load(std::memory_order_relaxed),
      .ao_frame = shared.data->ao_frame.load(std::memory_order_relaxed),
  };
}

}  // namespace talesofarise::post_filters
