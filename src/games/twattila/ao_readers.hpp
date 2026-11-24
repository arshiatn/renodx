/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The game's SSAO target's readers beside the sun draws (LIGHT_BOUNCE_DRAWS' ao_slot), a list per game: Pharaoh's
// building pools. Under Ray-Traced AO the sun draws read rt_scene's combined texture in place of the game's (the rays'
// AO where traced, the game's other channels); a listed reader whose slot holds that same game texture reads it too,
// once this frame's combine ran (at the frame's first sun draw: the readers draw after it), and the game's view is
// bound again after its draw. With every reader of a game's SSAO on the rays' AO, its SSAO chain can be left out under
// Ray-Traced AO (as Troy's 9d9dd3284). Otherwise (no rays' AO, Path-Traced Ambient, GT-VBAO, before the combine, another
// texture at the slot): the game's. WARP: test/tw_ao_readers.
// Troy's and Pharaoh's SSAO 2 chain (ssao2.hlsl: downsample B320992B, generate D42C3523, blur_x 7F637259, blur_y BFD285BD
// back into the generate's target) is left out while ours stands in for it (ao_skip::GameSsaoOut, every Total War
// add-on's rule: the rays' AO traced this frame or the last, or Path-Traced Ambient), the generate's target cleared open
// (ClearInsteadOfGenerate); the line-sweep AO of in-game SSAO 1 and 2 (ssao.hlsl) the same at its blur
// (ClearInsteadOfLineSweepBlur).

#include <d3d11.h>
#include <wrl/client.h>

#include <climits>
#include <cstdint>
#include <mutex>
#include <span>
#include <utility>

#include "./ao_skip.hpp"
#include "./rt_scene.hpp"

namespace twattila::ao_readers {

struct AoReader {
  uint32_t shader_hash = 0;
  UINT ao_slot = UINT_MAX;  // its t_ssao (the game's bytes' reflection)
};

// The rays' AO at `slot` in place of the game's view there when it is the one this frame's combine read: that game view
// (bind it again after the draw), else null (nothing changed).
inline Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> BindRaysAo(ID3D11DeviceContext* native, UINT slot) {
  if (!rt_scene::internal::active.load(std::memory_order_relaxed)) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> game_ao;
  native->PSGetShaderResources(slot, 1, game_ao.GetAddressOf());
  if (!game_ao) return {};
  auto& runtime = rt_scene::internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (runtime.combined_frame != runtime.frame || game_ao.Get() != runtime.game_ao || !runtime.objects.combined_srv) return {};
  native->PSSetShaderResources(slot, 1, runtime.objects.combined_srv.GetAddressOf());
  return game_ao;
}

// The SSAO 2 generate's draw while ours stands in for the game's AO (ao_skip::GameSsaoOut): its target cleared open (1) in
// its place (true), what the sun draws and the listed readers read where nothing of ours is bound there (a frame without
// the combine; under Path-Traced Ambient everywhere: the paths are the occlusion, as Attila's white AO pass). False: the
// game's draw. Under the rule it is always true (any context, nothing bound included), as the downsample and the blurs
// are left out under it: the chain is never split.
inline bool ClearInsteadOfGenerate(reshade::api::command_list* cmd_list) {
  if (!ao_skip::GameSsaoOut()) return false;
  ao_skip::ClearOpen(cmd_list, false);
  return true;
}

// The line-sweep AO's blur (ssao.hlsl cs50_blur_x: in-game SSAO 1 and 2; its bytes write every pixel of u0 from the
// collect alone) while ours stands in for the game's AO: u0 cleared open (1) in its place (true), so the sun draws read
// no game AO under the paths and in a dropout's first frame under the rays, as SSAO 2's generate. False: the game's
// dispatch.
inline bool ClearInsteadOfLineSweepBlur(reshade::api::command_list* cmd_list) {
  if (!ao_skip::GameSsaoOut()) return false;
  ao_skip::ClearOpen(cmd_list, true);
  return true;
}

namespace internal {

inline bool installed = false;

// A listed reader's draw: the rays' AO at its slot, the game's view back after it (carried as the post data, its
// reference held until then).
constexpr auto OnAoReaderDraw = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = rt_scene::internal::NativeContext(command.cmd_list);
  if (native == nullptr) return {};
  auto game_ao = BindRaysAo(native, command.template GetCallbackData<T>().ao_slot);
  if (!game_ao) return {};
  return {.post_data = game_ao.Detach(), .finalize_callback = [](Context& drawn, const void* data) {
            auto* game_view = static_cast<ID3D11ShaderResourceView*>(const_cast<void*>(data));
            if (auto* drawn_native = rt_scene::internal::NativeContext(drawn.cmd_list); drawn_native != nullptr) {
              drawn_native->PSSetShaderResources(drawn.template GetCallbackData<T>().ao_slot, 1, &game_view);
            }
            game_view->Release();
          }};
};

}  // namespace internal

// `readers`: static storage (each draw's hook holds its entry).
inline void Install(std::span<const AoReader> readers) {
  internal::installed = !readers.empty();
  for (const auto& reader : readers) {
    renodx::utils::command_action::Register(
        internal::OnAoReaderDraw, {.shader_hash = reader.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &reader);
  }
}

inline void Uninstall() {
  if (std::exchange(internal::installed, false)) renodx::utils::command_action::Unregister(internal::OnAoReaderDraw);
}

}  // namespace twattila::ao_readers
