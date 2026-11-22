/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The game's SSAO chain left out while ours stands in for it, one rule for every Total War add-on: Attila's,
// rt_scene::AmbientReplaced (the rays' AO or Path-Traced Ambient, traced within its frames; nothing before the first
// trace). Ours replaces the game's, never stacks: under Path-Traced Ambient alone too (Attila's AO pass writes white
// then; Troy's and Pharaoh's SSAO 2 since ho_g03). While it holds, every pass of the chain is left out and its output
// cleared open in its place (ClearOpen: the Warhammers' collect, Troy's and Pharaoh's generate). WARP: test/tw_ao_skip.

#include <d3d11.h>
#include <wrl/client.h>

#include <array>

#include "../../utils/directx.hpp"
#include "./rt_scene.hpp"

namespace twattila::ao_skip {

// The pass's AO output cleared open (1): the compute collect's u0 (`compute`: the Warhammers) or the draw's first target
// (Troy's and Pharaoh's SSAO 2 generate). On any context, a deferred one too. The caller leaves the pass's draw out
// whatever this finds (nothing bound: nothing to draw into), as it leaves the chain's other passes out: the chain is
// never split (the game's generate over a skipped downsample and blurs).
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
