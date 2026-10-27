/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/frame_generation.hpp"
#include "../twattila/fg_camera.hpp"
#include "./taa_dlss.hpp"

// DLSS Frame Generation (Tales' frame_generation.hpp, Streamline in the game's process) on the addon's DLSS
// (taa_dlss.hpp): its depth, motion and jitter, the main view's camera, the HUD-less frame after the tone mapper. Started
// at launch (ReShade.ini [Warhammer III] FrameGeneration).
namespace twwarhammer3::dlss_frame_generation {

using Microsoft::WRL::ComPtr;

namespace internal {

static twattila::fg_camera::Tracker camera;

inline void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_immediate_command_list()->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  // taa_dlss counts this present after this callback (registered first).
  const uint64_t frame = taa_dlss::Presents();
  talesofarise::frame_generation::Inputs inputs;
  if (taa_dlss::Frame dlss; taa_dlss::LastFrame(frame, &dlss)) {
    inputs = {
        .depth = dlss.depth,
        .motion = dlss.motion,
        .render_width = dlss.render[0],
        .render_height = dlss.render[1],
        .jitter = dlss.jitter,
        .reset = dlss.reset,
        .camera = camera.Next(dlss.camera.data()),
    };
  }
  talesofarise::frame_generation::Submit(context, inputs, frame);
}

// After the tone mapper's draw (the display's size, before the UI): the HUD-less frame from its target.
constexpr auto OnTonemapper = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!talesofarise::frame_generation::Presenting()) return {};
  return {.post_callback = [](Context& command, const void*) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&context);
    if (context == nullptr) return;
    ComPtr<ID3D11RenderTargetView> target;
    context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
    talesofarise::frame_generation::DrawHudless(context, target.Get(), taa_dlss::Presents());
  }};
};

}  // namespace internal

constexpr const char* SECTION = "Warhammer III";

// At the addon's start (before the game's swap chain, after taa_dlss::Install): frame generation when ReShade.ini asks
// for it; false: off.
inline bool Install(HMODULE module, std::span<const uint32_t> tonemapper_shaders) {
  bool enabled = false;
  talesofarise::frame_generation::LoadSettings(SECTION, &enabled);
  if (!enabled) return false;
  std::wstring module_path(32768, L'\0');
  module_path.resize(GetModuleFileNameW(module, module_path.data(), static_cast<DWORD>(module_path.size())));
  talesofarise::frame_generation::Install(std::filesystem::path(module_path).parent_path(), "Warhammer III");
  reshade::register_event<reshade::addon_event::present>(internal::OnPresent);
  for (const uint32_t shader_hash : tonemapper_shaders) {
    renodx::utils::command_action::Register(internal::OnTonemapper, {
                                                                        .shader_hash = shader_hash,
                                                                        .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                                    });
  }
  return true;
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::present>(internal::OnPresent);
  renodx::utils::command_action::Unregister(internal::OnTonemapper);
}

}  // namespace twwarhammer3::dlss_frame_generation
