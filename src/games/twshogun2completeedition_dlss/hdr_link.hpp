/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>

#include "../../utils/cross_addon.hpp"

// What the DLSS addon's overlay hands the RenoDX HDR addon past ReShade.ini (its graphics rows): one-shot requests for
// the HDR addon's ray tracing. Shared by both addons (cross_addon): SharedData is append-only.
namespace twshogun2::hdr_link {

struct __declspec(uuid("22ca4013-6c28-4d0d-b7c0-c7956e91bf43")) SharedData {
  std::atomic_bool rt_profile = false;  // Record Ray Tracing Profile pressed, not yet started
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

// DLSS addon: the overlay's button.
inline void RequestProfile() {
  if (shared.data != nullptr) shared.data->rt_profile.store(true, std::memory_order_relaxed);
}

// HDR addon: each present; true once a press.
inline bool TakeProfileRequest() {
  return shared.data != nullptr && shared.data->rt_profile.exchange(false, std::memory_order_relaxed);
}

}  // namespace twshogun2::hdr_link
