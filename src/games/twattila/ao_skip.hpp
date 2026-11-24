/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The game's SSAO chain left out while ours stands in for it, one rule for the hosts that skip it (GameSsaoOut: the
// Warhammers, Troy, Pharaoh), every pass of the chain left out and its output cleared open in its place (ClearOpen).
// Ours replaces the game's, never stacks. Attila's, Rome II's and Thrones' AO pass (rt_ambient_alone, its post multiply
// in rt_scene's OnAmbientOcclusionDraw) keeps rt_scene::AmbientReplaced until that multiply takes the same window.
// WARP: test/tw_ao_skip.

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <mutex>

#include "../../utils/directx.hpp"
#include "./rt_scene.hpp"

namespace twattila::ao_skip {

// Ours stands in for the game's AO this frame:
// - Ray-Traced AO, traced this frame or the last: the combine multiplies this frame's trace, or the last one's
//   (bridge.rt_ambient keeps it) when this frame's fails (rt_scene's sun combine, ef926642b). Longer dropouts bring the
//   game's AO back (no AO at all there before; a trace older than a frame, without reprojection, lands on moved
//   geometry: rvb_'s g18 review).
// - Path-Traced Ambient, traced within AMBIENT_REPLACED_FRAMES (rt_scene::AmbientReplaced's window): the paths are the
//   occlusion.
// Nothing before the first trace (traced_frame UINT64_MAX: UINT64_MAX + 1 wraps to frame 0).
inline bool GameSsaoOut() {
  auto& runtime = rt_scene::internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (runtime.traced_frame == UINT64_MAX) return false;
  return (runtime.settings.ambient_occlusion && runtime.traced_frame + 1 >= runtime.frame)
         || (runtime.settings.path_ambient && runtime.traced_frame + rt_scene::internal::AMBIENT_REPLACED_FRAMES >= runtime.frame);
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
    if (output) native->ClearUnorderedAccessViewFloat(output.Get(), OPEN.data());
    return;
  }
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output;
  native->OMGetRenderTargets(1, output.GetAddressOf(), nullptr);
  if (output) native->ClearRenderTargetView(output.Get(), OPEN.data());
}

}  // namespace twattila::ao_skip
