/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

#include <include/reshade.hpp>

#include "../../utils/command_action_fast.hpp"
#include "../../utils/data.hpp"
#include "../../utils/resource_upgrade.hpp"

// The Total War add-ons' opt-in to command_action's fast dispatch (utils/command_action_fast.hpp), with the gate of the
// resource upgrades' command-wide callback; view_share and object_motion gate their own.
namespace twattila::draw_dispatch {

namespace internal {

// Sticky: set at the first begin_render_pass event of any command list. Before it no list has a pending render pass, and
// the gate below skips its private-data lookup.
static std::atomic_bool render_pass_begun = false;

inline void OnBeginRenderPass(reshade::api::command_list*, uint32_t, const reshade::api::render_pass_render_target_desc*,
                              const reshade::api::render_pass_depth_stencil_desc*) {
  render_pass_begun.store(true, std::memory_order_relaxed);
}

}  // namespace internal

// After mods::shader::Use (or command_action::Use) attaches command_action; detach before it.
inline void Use(DWORD fdw_reason) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      renodx::utils::command_action::fast::Use(fdw_reason);
      reshade::register_event<reshade::addon_event::begin_render_pass>(internal::OnBeginRenderPass);
      // on_render_pass_command acts only on a command list with a pending render pass (OnBeginRenderPass's targets).
      renodx::utils::command_action::fast::SetGate(
          renodx::utils::resource::upgrade::on_render_pass_command,
          {.active = [](reshade::api::command_list* cmd_list) {
            if (!internal::render_pass_begun.load(std::memory_order_relaxed)) return false;
            const auto* data = renodx::utils::data::Get<renodx::utils::resource::upgrade::CommandListData>(cmd_list);
            return data != nullptr && !data->pending_render_pass_render_targets.empty();
          }});
      break;
    case DLL_PROCESS_DETACH:
      renodx::utils::command_action::fast::ClearGate(renodx::utils::resource::upgrade::on_render_pass_command);
      reshade::unregister_event<reshade::addon_event::begin_render_pass>(internal::OnBeginRenderPass);
      renodx::utils::command_action::fast::Use(fdw_reason);
      break;
    default:
      break;
  }
}

}  // namespace twattila::draw_dispatch
