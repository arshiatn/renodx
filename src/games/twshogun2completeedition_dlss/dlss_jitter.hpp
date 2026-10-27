/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "../../utils/cross_addon.hpp"

// The jitter of the frame the game's DLSS evaluated last (its InJitterOffset: where each rendered pixel sampled the
// scene, in render pixels), for the RenoDX HDR addon's passes after DLSS that read textures drawn with it (the heat
// haze's distortion texture: below 100% its jitter moved the haze by up to 1.5 display pixels every frame). 0 while the
// game's DLSS does not run. Shared by both addons (cross_addon): SharedData is append-only.
namespace twshogun2::dlss_jitter {

struct __declspec(uuid("158b1f13-0b1e-4253-afdf-946c88490995")) SharedData {
  std::atomic<float> x = 0.f;
  std::atomic<float> y = 0.f;
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

// DLSS addon: each evaluation, and 0 while the game's DLSS does not run.
inline void Store(float x, float y) {
  if (shared.data == nullptr) return;
  shared.data->x.store(x, std::memory_order_relaxed);
  shared.data->y.store(y, std::memory_order_relaxed);
}

// HDR addon: before its passes after DLSS.
inline std::array<float, 2> Load() {
  if (shared.data == nullptr) return {0.f, 0.f};
  return {shared.data->x.load(std::memory_order_relaxed), shared.data->y.load(std::memory_order_relaxed)};
}

}  // namespace twshogun2::dlss_jitter
