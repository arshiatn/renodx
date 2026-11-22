/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The game's SSAO chain left out under Ray-Traced AO (the games whose sun draws read rt_scene's combined texture in place
// of the game's SSAO: Warhammer, Warhammer II, Warhammer III, Troy). One rule for all of them. WARP: test/tw_ao_skip.

#include <cstdint>
#include <mutex>

#include "./dlaa_bridge.hpp"
#include "./rt_scene.hpp"

namespace twattila::ao_skip {

// Ray-Traced AO on, the bridge tracing, and the rays traced this frame or the last: the chain draws before this frame's
// trace (at the first sun draw), whose AO then stands for its output. Not under Path-Traced Ambient alone: the game's
// SSAO stays then (as with GT-VBAO off under the paths). Before the first trace (traced_frame UINT64_MAX) the game's:
// UINT64_MAX + 1 wraps to frame 0, which skipped the chain at frame 0 with nothing traced.
inline bool SkipGameSsao() {
  auto& runtime = rt_scene::internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  return runtime.settings.ambient_occlusion && dlaa_bridge::bridge.ray_tracing && runtime.traced_frame != UINT64_MAX
         && runtime.traced_frame + 1 >= runtime.frame;
}

}  // namespace twattila::ao_skip
