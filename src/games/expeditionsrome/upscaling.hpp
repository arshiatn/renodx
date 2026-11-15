/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>

#include <cstdint>
#include <mutex>

#include "../twattila/dlaa_bridge.hpp"

// The helper's bridge (twattila/dlaa_bridge.hpp: renodx-tw-dlaa64.exe next to the add-on, one a process) as this add-on
// uses it: the upscaler at the temporal pass (dlss_graphics.hpp: DLSS, Ray Reconstruction, FSR or XeSS in place of the
// game's TAA), frame generation and the ray tracing (ray_tracing.hpp). Every Prepare asks the same (PrepareBridge):
// another set would make all of its textures again. The bridge is the swap chain's size, the game's TAA output's.
namespace expeditionsrome::upscaling {

// What the bridge serves, set each present: the ray tracing (installed), its guides (reflections, paths, Ray
// Reconstruction) and reflections, the upscaler (0 none, else dlaa_bridge::UPSCALER_* + 1), Ray Reconstruction (0 off,
// else its NGX preset + 1; with the rays only), frame generation (its frames' format: frame_generation::Format, unknown
// while off; its backend) and the size.
struct BridgeUse {
  bool ray_tracing = false;
  bool guides = false;
  bool reflections = false;
  uint32_t upscaler = 0;
  uint32_t ray_reconstruction = 0;
  DXGI_FORMAT frame_format = DXGI_FORMAT_UNKNOWN;
  uint32_t frame_backend = twattila::dlaa_bridge::FG_BACKEND_DLSS;
  UINT width = 0;
  UINT height = 0;
};

namespace internal {
static std::mutex mutex;
static BridgeUse use;
}  // namespace internal

inline void SetUse(const BridgeUse& use) {
  const std::unique_lock lock(internal::mutex);
  internal::use = use;
}

inline BridgeUse Use() {
  const std::unique_lock lock(internal::mutex);
  return internal::use;
}

// Whether the G-buffer guides are written this frame's sun draw (ray_tracing.hpp): reflections, paths, Ray Reconstruction.
inline bool Guides() {
  const BridgeUse use = Use();
  return use.guides || (use.upscaler == twattila::dlaa_bridge::UPSCALER_DLSS + 1 && use.ray_reconstruction != 0);
}

// The bridge for what it serves; false: not available (no helper next to the add-on: nothing started, no wait; no size yet).
inline bool PrepareBridge(ID3D11Device* device) {
  const BridgeUse use = Use();
  if (use.width == 0 || use.height == 0 || !twattila::dlaa_bridge::HelperInstalled()) return false;
  return twattila::dlaa_bridge::Prepare(device, use.width, use.height, Guides(), use.ray_tracing, use.frame_format, use.reflections,
                                        (use.upscaler != 0) ? use.upscaler - 1 : twattila::dlaa_bridge::UPSCALER_DLSS, use.frame_backend);
}

}  // namespace expeditionsrome::upscaling
