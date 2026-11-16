/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <include/reshade.hpp>

#include "../../mods/shader.hpp"
#include "../../utils/command_action.hpp"
#include "../../utils/command_action_fast.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"

// Skip the Game's Sun Shadow Maps (Experimental, with the ray-traced sun shadows): the draws into the game's sun shadow
// maps are left out. The game clears each map before its casters, and its mask passes (sunshadow00-04) clamp the
// receiver's depth below 1, so a map without casters shadows nothing: the sun's shadow mask then holds the rays' alone
// (contact_shadows.hpp max-blends them in before the tiled lighting). The maps are learned from those mask passes, each
// reading one at t2 (the frame before's are skipped): 00 / 01 the two nearest cascades and the character's own map,
// 02 / 03 the middle cascade (three quarters of the sun's caster draws in the R0 traces), 04 the far map the game keeps
// between its rare redraws (never skipped: no cost a frame, and its shadows of what is out of view stay). Besides the mask
// passes (R0 census, tools/nontw/ryse/r0/shadow_readers.py) only the hair's blended strands (six permutations of its
// forward pass: depthMapSampler0 / 1) and the fog volumes' shadows (ps 0x91A72BC2: all four maps) read the maps: they
// lose the sun's shadow where a map is skipped. The eyes, the hair's first pass and the particles read the mask. The
// 16384 atlas is the lights' shadow pool (torches, fires), not the sun's: never learned. Only while the rays shadowed the
// mask the frame before (a frame without them draws the maps again). The draws are left out through command_action's
// bypass, not a draw event's skip: while the scene is traced every draw is replayed by command_action for rt_core's capture
// (OnAnyDraw's post callback), and a replayed draw reaches the GPU whatever another draw event returns.
namespace rysesonofrome::shadow_skip {

using Microsoft::WRL::ComPtr;

// The row's values.
enum Choice : uint8_t {
  OFF = 0,
  MIDDLE_CASCADE = 1,
  ALL = 2,
};

namespace internal {

// What the mask passes read a map as this frame (bits).
constexpr uint8_t NEAR_MAP = 1;
constexpr uint8_t MIDDLE_MAP = 2;
constexpr uint8_t KEPT_MAP = 4;

// The mask passes (the add-on's sunshadow00-04) and what the map each reads at t2 is.
constexpr std::array<std::pair<uint32_t, uint8_t>, 5> MASK_PASSES = {{
    {0xE9264ADE, NEAR_MAP},
    {0xB5DC6E36, NEAR_MAP},
    {0xF2B4EF30, MIDDLE_MAP},
    {0xF864A048, MIDDLE_MAP},
    {0xF81C7168, KEPT_MAP},
}};

struct State {
  std::shared_mutex mutex;
  std::unordered_map<uint64_t, uint8_t> read;  // this frame's maps, by resource
  std::unordered_set<uint64_t> skipped;        // the maps whose draws this frame leaves out
  uint64_t frame = 0;                          // presents so far
  uint64_t rays_frame = UINT64_MAX;            // the last frame the rays' sun shadows went into the mask
  size_t most_logged = 0;                      // the most maps skipped at once so far (logged when it grows)
};
static State state;
static std::atomic_bool any_skipped = false;
// The command list whose bound targets are a skipped map alone (null: none). One list at a time: the game records into
// its deferred context too, and a draw is only ever left out on the list that bound the map (a second list binding one
// meanwhile keeps its draws: less saved, nothing lost).
static std::atomic<reshade::api::command_list*> skipping = nullptr;

inline void OnBindTargets(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view* rtvs,
                          reshade::api::resource_view dsv) {
  bool skip = false;
  if (any_skipped.load(std::memory_order_relaxed) && dsv.handle != 0
      && std::all_of(rtvs, rtvs + count, [](reshade::api::resource_view view) { return view.handle == 0; })) {
    const uint64_t map = cmd_list->get_device()->get_resource_from_view(dsv).handle;
    const std::shared_lock lock(state.mutex);
    skip = state.skipped.contains(map);
  }
  if (skip) {
    skipping.store(cmd_list, std::memory_order_relaxed);
  } else {
    reshade::api::command_list* bound = cmd_list;
    skipping.compare_exchange_strong(bound, nullptr, std::memory_order_relaxed);
  }
}

// Draws, indexed draws and indirect draws on the list that bound a skipped map alone.
constexpr auto OnDraw = []<typename Context>(Context& context) -> renodx::utils::command_action::CallbackResult<Context> {
  if (skipping.load(std::memory_order_relaxed) != context.cmd_list) return {};
  if constexpr (std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::IndirectArguments>) {
    if (context.arguments.command == reshade::api::indirect_command::dispatch) return {};
  }
  return {.bypass = true};
};

}  // namespace internal

// The rays' sun shadows went into this frame's mask (contact_shadows::Apply drew them).
inline void NoteRays() {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.rays_frame = internal::state.frame;
}

// Every present: what the next frame skips, from this frame's maps (`choice`: the row's; OFF without the rays' sun shadows).
inline void OnPresent(Choice choice) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  state.skipped.clear();
  if (choice != OFF && state.rays_frame == state.frame) {
    for (const auto& [map, kinds] : state.read) {
      if ((kinds & internal::KEPT_MAP) == 0 && (choice == ALL || kinds == internal::MIDDLE_MAP)) state.skipped.insert(map);
    }
  }
  state.read.clear();
  ++state.frame;
  internal::any_skipped.store(!state.skipped.empty(), std::memory_order_relaxed);
  if (state.skipped.size() > state.most_logged) {
    state.most_logged = state.skipped.size();
    renodx::utils::log::i(std::format("[Ryse] Skip the Game's Sun Shadow Maps: {} maps without casters.", state.most_logged));
  }
}

// The game's device is going away: its maps with it.
inline void Release() {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  state.read.clear();
  state.skipped.clear();
  internal::any_skipped.store(false, std::memory_order_relaxed);
  internal::skipping.store(nullptr, std::memory_order_relaxed);
}

// With the ray tracing (at launch): the mask passes note their maps (before mods::shader::Use takes `shaders`), the
// draws into the skipped ones are left out (registered before command_action's attach and the fast dispatch's opt-in).
inline void Install(renodx::mods::shader::CustomShaders* shaders) {
  for (const auto& [hash, kind] : internal::MASK_PASSES) {
    const auto pass = shaders->find(hash);
    if (pass == shaders->end()) continue;
    pass->second.on_draw = [kind](reshade::api::command_list* cmd_list) {
      auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&context);
      ComPtr<ID3D11ShaderResourceView> view;
      if (context != nullptr) context->PSGetShaderResources(2, 1, view.GetAddressOf());
      if (!view) return true;
      ComPtr<ID3D11Resource> map;
      view->GetResource(map.GetAddressOf());
      const std::unique_lock lock(internal::state.mutex);
      internal::state.read[reinterpret_cast<uintptr_t>(map.Get())] |= kind;
      return true;
    };
  }
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(internal::OnBindTargets);
  renodx::utils::command_action::Register(
      internal::OnDraw,
      {.command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW | renodx::utils::command_action::COMMAND_TYPE_INDIRECT});
  renodx::utils::command_action::fast::SetGate(internal::OnDraw, {.active = [](reshade::api::command_list* cmd_list) {
                                                  return internal::skipping.load(std::memory_order_relaxed) == cmd_list;
                                                }});
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(internal::OnBindTargets);
  renodx::utils::command_action::fast::ClearGate(internal::OnDraw);
  renodx::utils::command_action::Unregister(internal::OnDraw);
  Release();
}

}  // namespace rysesonofrome::shadow_skip
