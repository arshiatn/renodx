/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The game's SSAO chain left out while ours stands in for it, one rule for the hosts that skip it (GameSsaoOut: the
// Warhammers, Troy, Pharaoh), every pass of the chain left out and its output cleared open in its place (ClearOpen).
// Ours replaces the game's, never stacks. Attila's, Rome II's and Thrones' AO pass (rt_ambient_alone, its post multiply
// in rt_scene's OnAmbientOcclusionDraw) keeps rt_scene::AmbientReplaced: its paths term is this one, its rays term keeps
// 30 frames of the trace (against this frame or the last) until that multiply takes the same window.
// WARP: test/tw_ao_skip.

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <mutex>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "./dlaa_bridge.hpp"
#include "./rt_scene.hpp"

namespace twattila::ao_skip {

// Ours stands in for the game's AO this frame:
// - Ray-Traced AO, traced this frame or the last, the bridge still up: the sun combine's own window (it multiplies this
//   frame's trace, or the last one's, which bridge.rt_ambient keeps). Longer dropouts bring the game's AO back (a trace
//   older than a frame, without reprojection, lands on moved geometry).
// - Path-Traced Ambient taking the game's ambient out (rt_scene::internal::PathsReplaceAmbientNow: the row on, after its
//   warm-up of PATH_AMBIENT_FRAMES path frames, the paths traced this frame or the last): OnPathAmbientDraw's own window.
//   The row alone (its warm-up, a path dropout, the rays tracing without the Light Bounce) keeps the game's.
// Nothing before the first trace (UINT64_MAX: UINT64_MAX + 1 wraps to frame 0).
inline bool GameSsaoOut() {
  auto& runtime = rt_scene::internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  const bool rays = runtime.settings.ambient_occlusion && dlaa_bridge::bridge.ray_tracing
                    && runtime.traced_frame != UINT64_MAX && runtime.traced_frame + 1 >= runtime.frame;
  return rays || rt_scene::internal::PathsReplaceAmbientNow();
}

// The pass's AO output cleared open (1): the compute pass's u0 (`compute`: the Warhammers' collect, the line sweep's
// blur) or the draw's first target (Troy's and Pharaoh's SSAO 2 generate). On any context, a deferred one too. The
// caller leaves the pass's draw out whatever this finds (nothing bound: nothing to draw into), as it leaves the chain's
// other passes out: the chain is never split (the game's generate over a skipped downsample and blurs).
inline void ClearOpen(reshade::api::command_list* cmd_list, bool compute) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  constexpr std::array<float, 4> OPEN = {1.f, 1.f, 1.f, 1.f};
  if (compute) {
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
    native->CSGetUnorderedAccessViews(0, 1, output.GetAddressOf());
    if (output) {
      native->ClearUnorderedAccessViewFloat(output.Get(), OPEN.data());
    }
    return;
  }
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output;
  native->OMGetRenderTargets(1, output.GetAddressOf(), nullptr);
  if (output) {
    native->ClearRenderTargetView(output.Get(), OPEN.data());
  }
}

}  // namespace twattila::ao_skip
