/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Ray-Traced Water Reflections (Experimental, off; tools\tw\pharaoh\reflections_plan.md; Pharaoh's, moved here for Troy): the
// water is the only surface the game's screen-space reflections touch (ssr_new: extract_mask takes the G-buffer's channel 3
// alpha bit 64, which only the sea and the rivers write). The rays are traced once a frame at backward_ssr, after the water's
// G-buffer and light: rt_scene's Reflections with the water's depth (its t_depth_src: the positions and the guides again from
// it, rt_host's rewrite_guides) and this frame's light (its t_colour_src, R16G16B16A16F: rt_host's light_copy). Where they left
// from the water, the game's chain is left out (backward_ssr, downsample and its 10 downsample_mip and 10 lute_mip: in every
// trace their target's only other reader is the apply) and our copy of the apply (the addon's shaders/ssrapply00) draws with
// the rays (OnWaterApplyDraw). Under the game's MSAA (its multisampled views bound) the game's reflections stay. The addon
// leaves its apply out of its custom shaders (OnWaterApplyDraw draws ours), Installs before renodx::mods::shader::Use, and
// passes its rows through SetRows and its rt_scene settings through GateSettings.

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../mods/shader.hpp"
#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/settings.hpp"
#include "./frame_history.hpp"
#include "./rt_scene.hpp"
#include "./view_share.hpp"

namespace twattila::water_reflections {

// The game's ssr_new shaders and our apply.
struct Shaders {
  uint32_t backward_ssr = 0;            // the rays traced at its dispatch
  UINT inputs_slot = 0;                 // backward_ssr's t_depth_src: its multisampled view, the light and its multisampled view after it
  std::array<uint32_t, 3> chain = {};   // downsample, downsample_mip, lute_mip: left out where the rays left from the water
  uint32_t apply = 0;                   // ps50_apply: ours in its place
  std::span<const uint8_t> apply_code;  // ours (shaders/ssrapply00)
};

namespace internal {

inline Shaders shaders;
inline const rt_scene::Sliders* sliders = nullptr;
inline std::atomic<bool> water_rays = false;  // this frame's rays left from the water: the chain left out, our apply ready
struct WaterReflections {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // the apply's (compared only)
  Microsoft::WRL::ComPtr<ID3D11PixelShader> apply;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> game_apply;  // bound again after the draw
  bool msaa_logged = false;
  bool logged = false;
  bool scaled_logged = false;
};
inline WaterReflections water_reflections;
// Our apply made on `device` (again on a new one); false: not made. With water_reflections' lock held.
inline bool MakeWaterApply(ID3D11Device* device) {
  auto& state = water_reflections;
  if (state.device == device) return true;
  state.apply.Reset();
  state.device = nullptr;
  if (FAILED(device->CreatePixelShader(shaders.apply_code.data(), shaders.apply_code.size(), nullptr, state.apply.GetAddressOf()))) return false;
  state.device = device;
  return true;
}
// False: left out (the rays left from the water).
inline bool OnBackwardSsr(reshade::api::command_list* cmd_list) {
  water_rays.store(false, std::memory_order_relaxed);
  if (sliders->reflections == 0.f || sliders->water_reflections == 0.f || !rt_scene::internal::active.load(std::memory_order_relaxed)) {
    return true;
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return true;
  // The depth, its multisampled view, the light, its multisampled view (backward_ssr's RDEF; one sample: the multisampled
  // views are the others).
  std::array<ID3D11ShaderResourceView*, 4> inputs = {};
  native->CSGetShaderResources(shaders.inputs_slot, static_cast<UINT>(inputs.size()), inputs.data());
  const bool multisampled = std::ranges::any_of(inputs, [](ID3D11ShaderResourceView* view) {
    if (view == nullptr) return false;
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc = {};
    if (SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
    return desc.SampleDesc.Count > 1;
  });
  std::array<ID3D11ShaderResourceView*, 2> views = {};
  if (!multisampled) {
    const rt_scene::internal::ComputeState kept(native);
    views = rt_scene::Reflections(native, inputs[0], inputs[2]);
  }
  for (auto* view : inputs) {
    if (view != nullptr) view->Release();
  }
  auto& state = water_reflections;
  if (multisampled) {
    const std::unique_lock lock(state.mutex);
    if (!std::exchange(state.msaa_logged, true)) {
      renodx::utils::log::i("[Total War] Ray-Traced Water Reflections: the game's MSAA is on, the game's reflections stay.");
    }
    return true;
  }
  bool water_traced = false;
  {
    auto& runtime = rt_scene::internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    water_traced = views[0] != nullptr && runtime.settings.reflections && runtime.water_traced;
  }
  if (!water_traced) return true;
  // Made before the chain is left out: without it the game's apply would read a buffer nothing wrote.
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  const std::unique_lock lock(state.mutex);
  if (!MakeWaterApply(device.Get())) return true;
  water_rays.store(true, std::memory_order_relaxed);
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i(std::format("[Total War] First run: Ray-Traced Water Reflections: the game's screen-space reflections ({:08X}, {:08X} and "
                                      "its 20 mips) left out, its apply {:08X} drawn with the rays.",
                                      shaders.backward_ssr, shaders.chain[0], shaders.apply));
  }
  return false;
}

// The apply where the rays left from the water this frame: the rays bound (t46, t47, b10: rt_scene's BindReflections) and our
// copy in the game's place for this draw; the game's shader bound again and the rays unbound after it (finalize). Below 100%
// ours also without the rays: the quad's TEXCOORD0 spans the viewport (A0EA5C9E's vb0 0..1 at any scale, Pharaoh's B61
// trace_50 l.3173), which the game's code takes for the target's uv, so it read the display-size textures whole; ours takes
// the draw's share from cb12 (WARP tools\tw\warp_tests\pharaoh\apply_share_v62, tools\tw\troy\ssr_share).
constexpr auto OnWaterApplyDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const bool rays = water_rays.load(std::memory_order_relaxed);
  if ((!rays && frame_history::render_width.load(std::memory_order_relaxed) == 0)
      || command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) {
    return {};
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  native->RSGetViewports(&viewport_count, &viewport);
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
  native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  D3D11_TEXTURE2D_DESC target_desc = {};
  if (Microsoft::WRL::ComPtr<ID3D11Resource> resource; target) {
    target->GetResource(resource.GetAddressOf());
    if (Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource.As(&texture))) texture->GetDesc(&target_desc);
  }
  if (viewport_count == 0 || target_desc.Width == 0 || target_desc.Height == 0) return {};
  const float share_x = std::clamp(viewport.Width / static_cast<float>(target_desc.Width), 0.f, 1.f);
  const float share_y = std::clamp(viewport.Height / static_cast<float>(target_desc.Height), 0.f, 1.f);
  const bool scaled = share_x < 1.f || share_y < 1.f;
  if (!rays && !scaled) return {};
  const bool bound = rays && rt_scene::BindReflections(command.cmd_list, false, true);
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = water_reflections;
  const std::unique_lock lock(state.mutex);
  if ((rays && !bound) || !MakeWaterApply(device.Get())) {
    rt_scene::BindReflections(command.cmd_list, false, false);
    return {};
  }
  if (scaled && !std::exchange(state.scaled_logged, true)) {
    renodx::utils::log::i(std::format("[Total War] First run: the water's reflection apply {:08X} below 100%: ours with the draw's share (", shaders.apply),
                          share_x, " x ", share_y, ")", (rays ? ", with the rays." : ", the game's reflections."));
  }
  // zw (the frame's share) unread by ours: the draw's.
  view_share::internal::Bind(native, {share_x, share_y, share_x, share_y}, reshade::api::pipeline_stage::pixel_shader);
  native->PSGetShader(state.game_apply.ReleaseAndGetAddressOf(), nullptr, nullptr);
  native->PSSetShader(state.apply.Get(), nullptr, 0);
  return {.finalize_callback = [](Context& drawn, const void*) {
    water_rays.store(false, std::memory_order_relaxed);
    rt_scene::BindReflections(drawn.cmd_list, false, false);
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    auto& drawn_state = water_reflections;
    const std::unique_lock drawn_lock(drawn_state.mutex);
    if (drawn_native != nullptr) {
      drawn_native->PSSetShader(drawn_state.game_apply.Get(), nullptr, 0);
    }
    drawn_state.game_apply.Reset();
  }};
};

// Only our apply's device (nullptr: the addon's end, Uninstall).
inline void OnDestroyWaterReflectionsDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = water_reflections;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.apply.Reset();
  state.device = nullptr;
  water_rays.store(false, std::memory_order_relaxed);
}

}  // namespace internal

// The game's reflections fall on the water alone (its apply): without Water Reflections nothing reads the rays' reflections or
// their specular occlusion, so neither is traced.
inline void GateSettings(rt_scene::Settings* settings) {
  settings->reflections = settings->reflections && settings->water_reflections;
  settings->specular_occlusion = settings->specular_occlusion && settings->reflections;
  // Calm water is a mirror to the rays (one ray, no reuse): Samples and Reuse act, and show, only with Softer Water Reflections.
  if (!settings->water_soft) {
    settings->reflection_samples = 1;
    settings->reflection_reuse = false;
    settings->reflection_own = false;
  }
}

// The rows where the water alone takes the rays' reflections (after graphics_page::SetRaysPresetDefaults, before the page):
// Water Reflections Experimental, off (RT/PT turns it on, as ATN's list does); Specular Occlusion and the soft reflections'
// Samples and Reuse shown only where they act.
inline void SetRows(std::vector<renodx::utils::settings::Setting*>* settings, const rt_scene::Sliders* sliders) {
  for (auto* setting : *settings) {
    if (setting->key == "RayTracedWaterReflections") {
      setting->default_value = 0.f;
    } else if (setting->key == "RayTracedSpecularOcclusion") {
      // Only the water's reflected cube, with the water's own rays (OnBackwardSsr): shown with them.
      setting->default_value = 0.f;
      setting->tooltip = "The sky's reflection on the water dimmed where something blocks it. Needs Water Reflections.";
      setting->is_visible = [sliders] { return sliders->reflections != 0.f && sliders->water_reflections != 0.f; };
    } else if (setting->key == "RayTracedReflectionSamples" || setting->key == "RayTracedReflectionReuse") {
      // Calm water is a mirror to the rays (one ray, no reuse): these act on it with Softer Water Reflections alone.
      setting->tooltip = (setting->key == "RayTracedReflectionSamples")
                             ? "More rays per pixel in the water's softer reflections. Heavier."
                             : "Less noise in the water's softer reflections.\n"
                               "From ReSTIR GI: also the path-traced Light Bounce's samples (needs Path Tracing).\n"
                               "Own ReSTIR: their own samples, reused over frames and between neighbours.";
      setting->is_visible = [sliders] { return sliders->water_reflections != 0.f && sliders->water_soft != 0.f; };
    }
  }
}

// Traced at backward_ssr (OnBackwardSsr); where they left from the water, the chain after it left out too and our apply drawn
// (OnWaterApplyDraw). Before renodx::mods::shader::Use (the custom shaders' on_draw).
inline void Install(renodx::mods::shader::CustomShaders* custom_shaders, const Shaders& game_shaders, const rt_scene::Sliders* sliders) {
  internal::shaders = game_shaders;
  internal::sliders = sliders;
  (*custom_shaders)[game_shaders.backward_ssr] = {.crc32 = game_shaders.backward_ssr, .on_draw = &internal::OnBackwardSsr};
  for (const uint32_t shader_hash : game_shaders.chain) {
    (*custom_shaders)[shader_hash] = {.crc32 = shader_hash, .on_draw = [](auto*) { return !internal::water_rays.load(std::memory_order_relaxed); }};
  }
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyWaterReflectionsDevice);
  renodx::utils::command_action::Register(
      internal::OnWaterApplyDraw, {.shader_hash = game_shaders.apply, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  renodx::utils::log::i(std::format("[Total War] Ray-Traced Water Reflections: hooks applied ({:08X}, {:08X}, {:08X}, {:08X}, {:08X}).",
                                    game_shaders.backward_ssr, game_shaders.chain[0], game_shaders.chain[1], game_shaders.chain[2], game_shaders.apply));
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnWaterApplyDraw);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyWaterReflectionsDevice);
  internal::OnDestroyWaterReflectionsDevice(nullptr);
}

}  // namespace twattila::water_reflections
