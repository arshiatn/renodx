/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

#include "../twattila/dlaa_bridge.hpp"

// The bridge to renodx-tw-dlaa64.exe (twattila/dlaa_bridge.hpp, one a process) serves the anti-aliasing (graphics.hpp:
// the helper's DLSS / DLAA, Ray Reconstruction, FSR, XeSS and frame generation in place of the game's TAA) and the ray
// tracing (ray_tracing.hpp) from one set of textures: both prepare it through PrepareBridge with what this frame serves
// (another set would make all of its textures again). Its size is the upscaler's output while the helper upscales (the
// display; below 100% the render size is its top-left), else the caller's (the render size).
namespace rysesonofrome::upscaling {

// What the bridge serves, set each present (addon.cpp): the ray tracing, its G-buffer guides (reflections, paths, Ray
// Reconstruction) and reflections, the upscaler (0: none, the game's TAA; else dlaa_bridge::UPSCALER_* + 1), Ray
// Reconstruction (0 off, else its NGX preset + 1; with the rays only), frame generation (its frames' format:
// twattila/frame_generation.hpp Format, unknown while off; its backend).
struct BridgeUse {
  bool ray_tracing = false;
  bool guides = false;
  bool reflections = false;
  uint32_t upscaler = 0;
  uint32_t ray_reconstruction = 0;
  DXGI_FORMAT frame_format = DXGI_FORMAT_UNKNOWN;
  uint32_t frame_backend = twattila::dlaa_bridge::FG_BACKEND_DLSS;
};

namespace internal {

struct State {
  std::mutex mutex;
  BridgeUse use;
  std::array<UINT, 2> output_size = {};  // the helper upscaler's last output (0: it did not run yet)
};
static State state;

}  // namespace internal

inline void SetUse(const BridgeUse& use) {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.use = use;
}

inline BridgeUse CurrentUse() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.use;
}

// The helper upscaled to `width` x `height` (graphics.hpp, before its PrepareBridge): the ray tracing's prepares take
// that size too.
inline void SetOutputSize(UINT width, UINT height) {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.output_size = {width, height};
}

// The bridge for this frame's use, at `width` x `height` (the caller's render size) or, while the helper upscales, at
// least its output's; false: not available (no helper, not started, failed).
inline bool PrepareBridge(ID3D11Device* device, UINT width, UINT height) {
  BridgeUse use;
  std::array<UINT, 2> output_size;
  {
    const std::unique_lock lock(internal::state.mutex);
    use = internal::state.use;
    output_size = internal::state.output_size;
  }
  if (use.upscaler != 0) {
    width = std::max(width, output_size[0]);
    height = std::max(height, output_size[1]);
  }
  const uint32_t upscaler = (use.upscaler != 0) ? use.upscaler - 1 : twattila::dlaa_bridge::UPSCALER_DLSS;
  return twattila::dlaa_bridge::Prepare(device, width, height, use.guides, use.ray_tracing, use.frame_format, use.reflections,
                                        upscaler, use.frame_backend);
}

}  // namespace rysesonofrome::upscaling
