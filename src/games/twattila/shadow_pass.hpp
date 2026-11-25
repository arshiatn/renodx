/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"
#include "./attila64_plugin/plugin_block.h"
#include "./native_hook_lifetime.hpp"
#include "./rt_scene.hpp"

// Skip the Game's Shadow Map Work (Experimental; Attila, Thrones of Britannia, Rome II): under the rays' sun shadows every caster
// drawn into the sun's shadow map is skipped at the draw (rt_scene OnBindTargets / OnCasterDraw), after the render thread
// set up and submitted it (Constantinople outside the walls: 1561 such draws in 4.4-4.8 ms of the render thread). Here
// the model manager's draws of those passes (its rigid lists, trees and soldiers) are not made at all: each frame one of
// them still is, in turn, so the cut-out census (rt_scene OnCutOutShadowDraw: the alpha-tested F79BFB14 draws) sees every
// cascade within a few frames. Which passes those are is learned in game, not assumed: a pass kind every one of whose
// calls drew into the sun's map alone (bound alone for drawing at its start or by it, nothing else bound, still at its
// end), 16 calls without one that wasn't. Off, or without the rays' shadows: every call made as the game makes it.
// The 32-bit games hook the pass draw here; on the 64-bit port the engine plugin hooks it and calls PassBegin / PassEnd
// (attila64_plugin_link.hpp).
namespace twattila::shadow_pass {

namespace internal {

// void __thiscall Render(Renderable* self, Context* context, Pass* pass) (+0x17fa310, the model manager's renderable at
// its +0x10, vtable +8; Thrones of Britannia +0x186e8a0, the same bytes): self +4 0: nothing; else the pass's kind (its
// vtable +0x3c, 0 to 0x62: a switch, +0x17fa8a8) picks the lists drawn (+0xa88 to +0xb3c through +0x17ae940) and whether
// the soldiers are (+0x17fb8a0, which runs the frame's first bone pack wherever it comes first). Its first calls of a frame
// draw the sun's cascades (heapfix's pass timing). ret 8.
constexpr std::string_view RENDER =
    "81 EC A4 00 00 00 57 8B F9 80 7F 04 00 0F 84 ?? ?? ?? ?? 53 8B 9C 24 B4 00 00 00 8B CB 55 56 8B 03 FF 50 3C";
// Rome II's (+0x14b84a0, its vtable entry at +0x18d8be8): the same steps without the context, void __thiscall
// Render(Renderable* self, Pass* pass), ret 4 (self in ebx, self +4 0: nothing, the pass's kind by its vtable +0x3c).
constexpr std::string_view RENDER_ROME2 =
    "81 EC A0 00 00 00 53 8B D9 89 5C 24 1C 80 7B 04 00 0F 84 ?? ?? ?? ?? 55 56 57 8B BC 24 B4 00 00 00 8B CF 8B 07 FF 50 3C";
constexpr size_t SELF_ENABLED = 4;
constexpr size_t PASS_KIND_SLOT = 0x3c / 4;
constexpr uint32_t PASS_KINDS = 0x63;
constexpr uint32_t LEARNED_CALLS = 16;

using Render = void(__fastcall*)(uint8_t* self, void* unused_edx, void* context, uint8_t* pass);
using RenderRome2 = void(__fastcall*)(uint8_t* self, void* unused_edx, uint8_t* pass);
using PassKind = uint32_t(__fastcall*)(uint8_t* pass, void* unused_edx);

struct Kind {
  uint32_t sun_only = 0;    // calls that drew into the sun's map alone
  bool mixed = false;       // a call that bound another target, or ran with the map not bound
  uint32_t calls = 0;       // this frame's
  uint32_t last_calls = 0;  // the last frame's that drew this kind
  // Frames that drew this kind: the kept call turns with them. The 64-bit loader's shadow refresh rests the shadow view on
  // some frames (no call then); turning with the present count would keep the same call on every drawn frame.
  uint32_t drawn_frames = 0;
  bool logged = false;
};

// The render thread's (Render, the binds and the present run there; other threads' calls pass through).
static std::array<Kind, PASS_KINDS> kinds;
static std::atomic<DWORD> render_thread = 0;
static std::atomic<bool> active = false;  // the row on and the casters skipped (Update, at the present)
static std::atomic<bool> other_thread_logged = false;
static uint32_t sun_binds = 0;
static uint32_t other_binds = 0;
static bool binds_counted = false;  // Install registered OnBindTargets
static Render original_render = nullptr;
static RenderRome2 original_render_rome2 = nullptr;
static std::vector<renodx::utils::detour::Function> hooks;
static tw::native_hooks::Lifetime hook_lifetime;
static bool searched = false;

}  // namespace internal

// Whether pass draws on this thread are counted now: the row active, on the render thread (the binds are counted there
// only).
inline bool Counting() {
  return internal::active.load(std::memory_order_relaxed)
         && GetCurrentThreadId() == internal::render_thread.load(std::memory_order_relaxed);
}

// Before a pass draw that is Counting, its node enabled: skip it, draw it, or draw and measure it (then PassEnd with the
// same `measure`, after the draw). Leaves measure->reserved alone (the 64-bit link's).
inline attila64_plugin::PassAction PassBegin(uint32_t kind_index, attila64_plugin::PassMeasure* measure) {
  if (kind_index >= internal::PASS_KINDS) return attila64_plugin::PASS_DRAW;
  auto& kind = internal::kinds[kind_index];
  const uint32_t call = kind.calls++;
  if (kind.mixed) return attila64_plugin::PASS_DRAW;
  if (kind.sun_only >= internal::LEARNED_CALLS && call != kind.drawn_frames % std::max(kind.last_calls, 1u)) {
    return attila64_plugin::PASS_SKIP;
  }
  measure->sun_binds_before = internal::sun_binds;
  measure->other_binds_before = internal::other_binds;
  measure->sun_at_start = rt_scene::SkippingCasters() ? 1u : 0u;
  return attila64_plugin::PASS_DRAW_MEASURED;
}

// After a PASS_DRAW_MEASURED draw: whether it drew into the sun's map alone.
inline void PassEnd(uint32_t kind_index, const attila64_plugin::PassMeasure& measure) {
  if (kind_index >= internal::PASS_KINDS) return;
  auto& kind = internal::kinds[kind_index];
  if (kind.mixed) return;
  if (internal::other_binds == measure.other_binds_before
      && (measure.sun_at_start != 0u || internal::sun_binds != measure.sun_binds_before) && rt_scene::SkippingCasters()) {
    ++kind.sun_only;
  } else {
    kind.mixed = true;
  }
  if (!kind.logged && (kind.mixed || kind.sun_only == internal::LEARNED_CALLS)) {
    kind.logged = true;
    renodx::utils::log::i("[Total War] Shadow map work: pass kind ", kind_index,
                          kind.mixed ? " draws more than the sun's map: always drawn"
                                     : " draws only into the sun's map: left out but one call a frame");
  }
}

namespace internal {

// A Render call of either 32-bit build: `draw` makes the game's call.
template <typename Draw>
static void OnPassDraw(const uint8_t* self, uint8_t* pass, const Draw& draw) {
  const bool on_render_thread = (GetCurrentThreadId() == render_thread.load(std::memory_order_relaxed));
  // Once: the hook went in at the present on the assumption that only the render thread runs Render (its install's
  // CALLER_GUARANTEES_QUIESCENCE); a call from another thread says that assumption is wrong for this game.
  if (!on_render_thread && !other_thread_logged.load(std::memory_order_relaxed) && !other_thread_logged.exchange(true)) {
    renodx::utils::log::w("[Total War] Shadow map work: the pass draw also runs on thread ", GetCurrentThreadId(),
                          " (not the render thread): its hook's install at the present was not quiescent");
  }
  if (!active.load(std::memory_order_relaxed) || !on_render_thread || self[SELF_ENABLED] == 0 || pass == nullptr) {
    return draw();
  }
  void** pass_vtable;
  std::memcpy(&pass_vtable, pass, sizeof(pass_vtable));
  const uint32_t kind_index = reinterpret_cast<PassKind>(pass_vtable[PASS_KIND_SLOT])(pass, nullptr);
  attila64_plugin::PassMeasure measure;
  switch (PassBegin(kind_index, &measure)) {
    case attila64_plugin::PASS_SKIP:
      return;
    case attila64_plugin::PASS_DRAW_MEASURED:
      draw();
      PassEnd(kind_index, measure);
      return;
    case attila64_plugin::PASS_DRAW:
    default:
      return draw();
  }
}

static void __fastcall OnRender(uint8_t* self, void* unused_edx, void* context, uint8_t* pass) {
  OnPassDraw(self, pass, [&] { original_render(self, unused_edx, context, pass); });
}

static void __fastcall OnRenderRome2(uint8_t* self, void* unused_edx, uint8_t* pass) {
  OnPassDraw(self, pass, [&] { original_render_rome2(self, unused_edx, pass); });
}

static void OnBindTargets(reshade::api::command_list*, uint32_t, const reshade::api::resource_view*, reshade::api::resource_view) {
  if (!Counting()) return;
  if (rt_scene::SkippingCasters()) {
    ++sun_binds;
  } else {
    ++other_binds;
  }
}

}  // namespace internal

// After rt_scene::InstallShadowsOnly (its bind callback first: SkippingCasters is the bind's).
inline void Install() {
  internal::binds_counted = true;
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(internal::OnBindTargets);
}

// Every present (the render thread): `row` and whether this frame's casters are skipped (rt_scene::CastersSkipped). The
// hook goes in the first time the row is on; without the row no game byte changes. `plugin_hooked`: the 64-bit engine
// plugin hooks the pass draw (nothing searched here). Returns whether the pass draws are learned and skipped now.
inline bool Update(bool row, bool casters_skipped, bool plugin_hooked = false) {
  internal::render_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
  for (auto& kind : internal::kinds) {
    if (kind.calls != 0) {
      kind.last_calls = kind.calls;
      ++kind.drawn_frames;
    }
    kind.calls = 0;
  }
  if (row && !plugin_hooked && !internal::searched && internal::binds_counted) {
    internal::searched = true;
    HMODULE module = GetModuleHandleW(L"empire.retail.dll");
    const std::vector<std::string_view> code = (module != nullptr ? tw::engine_tweaks::ModuleSections(module).code
                                                                  : std::vector<std::string_view>{});
    std::string result = "not found";
    if (uint8_t* render = tw::engine_tweaks::FindPattern(code, internal::RENDER); render != nullptr) {
      internal::original_render = reinterpret_cast<internal::Render>(render);
      internal::hooks.emplace_back(&internal::original_render, &internal::OnRender,
                                   renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE);
    } else if (uint8_t* render_rome2 = tw::engine_tweaks::FindPattern(code, internal::RENDER_ROME2); render_rome2 != nullptr) {
      internal::original_render_rome2 = reinterpret_cast<internal::RenderRome2>(render_rome2);
      internal::hooks.emplace_back(&internal::original_render_rome2, &internal::OnRenderRome2,
                                   renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE);
    }
    if (!internal::hooks.empty()) {
      try {
        internal::hook_lifetime.Install(internal::hooks);
        result = "hooked";
      } catch (const std::exception& error) {
        internal::hooks.clear();
        result = std::string("hook failed: ") + error.what();
      }
    }
    renodx::utils::log::i("[Total War] Shadow map work: the model manager's pass draw ", result);
  }
  const bool active = row && casters_skipped && (plugin_hooked ? internal::binds_counted : !internal::hooks.empty());
  internal::active.store(active, std::memory_order_relaxed);
  return active;
}

// Addon detach.
inline void Uninstall() {
  internal::active.store(false, std::memory_order_relaxed);
  if (std::exchange(internal::binds_counted, false)) {
    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(internal::OnBindTargets);
  }
  if (internal::hooks.empty()) return;
  try {
    internal::hook_lifetime.Uninstall(&internal::hooks);
  } catch (const std::exception& error) {
    renodx::utils::log::e("[Total War] Shadow map work hook could not be removed; addon retained until process exit: ", error.what());
  }
}

}  // namespace twattila::shadow_pass
