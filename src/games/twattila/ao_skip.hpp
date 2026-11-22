/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The game's SSAO chain left out while ours stands in for it, its output cleared open in its place. SkipGameSsao: the rule
// for the games whose sun draws read rt_scene's combined texture in place of the game's SSAO under Ray-Traced AO
// (Warhammer, Warhammer II, Warhammer III). ClearOpen: the clear for all of them and for Troy's and Pharaoh's SSAO 2
// (ao_readers::ClearInsteadOfGenerate, under rt_scene::AmbientReplaced). WARP: test/tw_ao_skip.

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <mutex>

#include "../../utils/directx.hpp"
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

// The pass's AO output cleared open (1) in place of its draw: the compute collect's u0 (`compute`: the Warhammers) or the
// draw's first target (Troy's and Pharaoh's SSAO 2 generate). On any context, a deferred one too (the pass is left out
// there as well). False: not D3D11, or nothing bound there.
inline bool ClearOpen(reshade::api::command_list* cmd_list, bool compute) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return false;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return false;
  constexpr std::array<float, 4> OPEN = {1.f, 1.f, 1.f, 1.f};
  if (compute) {
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
    native->CSGetUnorderedAccessViews(0, 1, output.GetAddressOf());
    if (!output) return false;
    native->ClearUnorderedAccessViewFloat(output.Get(), OPEN.data());
    return true;
  }
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output;
  native->OMGetRenderTargets(1, output.GetAddressOf(), nullptr);
  if (!output) return false;
  native->ClearRenderTargetView(output.Get(), OPEN.data());
  return true;
}

}  // namespace twattila::ao_skip
