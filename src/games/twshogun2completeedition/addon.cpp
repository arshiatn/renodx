/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../../utils/swapchain.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twshogun2completeedition_dlss/dlss_jitter.hpp"
#include "../twshogun2completeedition_dlss/hdr_link.hpp"
#include "../twwarhammer2/line_sweep_ao.hpp"
#include "./foliage_light.hpp"
#include "./ray_tracing.hpp"
#include "./upscaling.hpp"
#include "../twattila/reflex.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "./shared.h"
#include "./vfx_effects.hpp"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;
// VFX effect test (Advanced): 0 off, 1 hide the effect, 2 only the effect; the effect's number (vfx_effects.hpp).
float vfx_test = 0.f;
float vfx_test_effect = 1.f;

bool IsAdvancedSettings() {
  return current_settings_mode >= 1.f;
}

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsExtendedMode() {
  return shader_injection.hdr == 1.f && IsAdvancedSettings();
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f && IsAdvancedSettings();
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 3.f && IsAdvancedSettings();
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, UE's film curve).
constexpr float HUE_CLIP_DEFAULT = 70.f;

bool IsHdrOutput() {
  return shader_injection.swap_chain_encoding != 2.f;
}

void ApplyResetPreset() {
  // Peak / Paper White / UI, Settings Mode and output are kept.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 1.f},
      {"ext_exposure", 1.f},
      {"ext_highlights", 1.f},
      {"ext_shadows", 1.f},
      {"ext_contrast", 1.f},
      {"ext_saturation", 1.f},
      {"ext_highlight_saturation", 1.f},
      {"ext_highlight_hue_shift", 0.33f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"cone_response_exponent", 1.f},
      {"saturation", 1.f},
      {"highlight_saturation", 1.f},
      {"roll_off", 1.f},
      {"highlight_hue_shift", 0.46f},
      {"highlight_contrast", 1.f},
      {"shadow_contrast", 1.f},
      {"flare", 0.f},
      {"dechroma", 0.f},
      {"gamut_compression", 1.f},
      {"gamut_compression_mode", 1.f},
      {"highlight_saturation_shift", 0.f},
      {"bloom", 1.f},
      {"lens_flare", 1.f},
      {"vignette", 1.f},
      {"heat_haze", 1.f},
      {"depth_of_field", 1.f},
      {"battle_fog", 1.f},
      {"sun", 1.f},
      {"foliage_glints", 1.f},
      {"vfx_glow", 1.f},
      {"vfx_big_fire", 1.f},
      {"vfx_campfire", 1.f},
      {"vfx_trails", 1.f},
      {"vfx_firelight", 1.f},
      {"vfx_smoke", 1.f},
      {"vfx_smoke_opacity", 1.f},
      {"video_autohdr", 0.f},
      {"video_dithering", 1.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

// The recommended presets (ATN's settings; 2026-11-12: Musa's PsychoV35 at its defaults): HDR (PsychoV35), no lens
// flares, faint foliage glints, faint haze, brighter VFX and videos. PsychoV31's sliders as before, for that mode.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 3.f},
      {"highlight_saturation", 0.75f},
      {"lens_flare", 0.f},
      {"foliage_glints", 0.15f},
      {"video_autohdr", 0.25f},
      {"heat_haze", 0.1f},
      {"vfx_glow", 5.f},
      {"vfx_big_fire", 1.f},
      {"vfx_campfire", 15.f},
      {"vfx_trails", 2.f},
      {"vfx_firelight", 1.8f},
      {"vfx_smoke", 2.f},
      {"vfx_smoke_opacity", 1.f},
      {"sun", 2.f},
  });
}

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;
// Ray tracing (Experimental, ray_tracing.hpp): its settings (the DLSS addon's, ReShade.ini RayTraced* / PathTracing*),
// installed when an effect is on at launch.
twattila::rt_scene::Settings ray_tracing_settings;
// Rays a Pixel (the DLSS addon's RayTracingOutputResolution: 1, Match Output, 2-8; RayTracingRays*: what takes them), as
// twattila/rt_scene.hpp's SettingsOf counts them (each present: Match Output follows the render scale).
struct RaysAPixel {
  uint32_t choice = 0;
  bool shadows = true;
  bool ambient = true;
  bool reflections = true;
  bool paths = true;
} rays_a_pixel;
bool ray_tracing_installed = false;
// Between the reflections' HiZ and their resolve (the generic mip passes there are theirs; render thread).
bool reflection_passes = false;

// Render scale (the DLSS addon draws the 3D passes into the top-left part of the full-size targets) ////////////////
// The rendered fraction for the depth of field, the auto exposure and the tonemap's distortion lookup: a 3D pass's
// viewport over its back-buffer-size target (others: shadows, half size, the UI are skipped). Passes before the capture
// use the camera's g_viewport_dimensions instead (patched to the render size by the DLSS addon).
namespace render_scale {

std::atomic<uint32_t> back_buffer_width = 0;
std::atomic<uint32_t> back_buffer_height = 0;

void Set(float x, float y) {
  shader_injection.render_scale_x = x;
  shader_injection.render_scale_y = y;
}

bool OnDrawCapture(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  if (viewport_count == 0 || viewport.Width <= 0.f || viewport.Height <= 0.f || viewport.TopLeftX != 0.f
      || viewport.TopLeftY != 0.f) {
    return true;
  }
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (target == nullptr) return true;
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;
  target->GetResource(resource.GetAddressOf());
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (FAILED(resource.As(&texture))) return true;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (desc.Width == 0 || desc.Width != back_buffer_width.load() || desc.Height != back_buffer_height.load()) return true;
  const float x = viewport.Width / static_cast<float>(desc.Width);
  const float y = viewport.Height / static_cast<float>(desc.Height);
  if (x < 0.05f || y < 0.05f) return true;
  Set(std::min(x, 1.f), std::min(y, 1.f));
  return true;
}

}  // namespace render_scale

// The sea's colour overlay (selection rings, firing arcs, wakes, bow waves: indicator ps30_main_to_overlay, wake,
// seainteraction; drawn only there): the ocean reads it at the full-screen position (its decal projection), but with the
// DLSS addon's render scale its draws went into the top-left part (ATN debug19, 33%: rings and wakes at a third of their
// size in the top-left). Its draws use the whole back-buffer-size target, the game's viewport restored after each.
namespace sea_overlay {

D3D11_VIEWPORT game_viewport = {};
bool restore = false;

bool OnDraw(reshade::api::command_list* cmd_list) {
  restore = false;
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &game_viewport);
  const uint32_t width = render_scale::back_buffer_width.load();
  const uint32_t height = render_scale::back_buffer_height.load();
  if (viewport_count == 0
      || (game_viewport.Width >= static_cast<float>(width) && game_viewport.Height >= static_cast<float>(height))) {
    return true;
  }
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (target == nullptr) return true;
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;
  target->GetResource(resource.GetAddressOf());
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (FAILED(resource.As(&texture))) return true;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (desc.Width != width || desc.Height != height) return true;
  const D3D11_VIEWPORT full = {
      .TopLeftX = 0.f,
      .TopLeftY = 0.f,
      .Width = static_cast<float>(desc.Width),
      .Height = static_cast<float>(desc.Height),
      .MinDepth = game_viewport.MinDepth,
      .MaxDepth = game_viewport.MaxDepth,
  };
  context->RSSetViewports(1, &full);
  restore = true;
  return true;
}

void OnDrawn(reshade::api::command_list* cmd_list) {
  if (!restore) return;
  restore = false;
  reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->RSSetViewports(1, &game_viewport);
}

}  // namespace sea_overlay

void BuildRuntimeData() {
  custom_shaders = {
      CustomShaderEntry(0xEF060C7C),  // tonemap00 (hdr_to_screen)
      CustomShaderEntry(0xBEDBEFD8),  // tonemap01 (hdr_to_screen, distortion)
      CustomShaderEntry(0x3D56DB58),  // uiresolve00 (ui_resolve)
      CustomShaderEntry(0x3208750E),  // hdrflare00 (hdr_lenseflare: bloom + flare ghosts)
      CustomShaderEntry(0xD6708726),  // flare00 (lenseflare flare_0)
      CustomShaderEntry(0x5C6F73D3),  // flare01
      CustomShaderEntry(0x7645BCB0),  // flare02
      CustomShaderEntry(0xE23F06FF),  // flare03
      CustomShaderEntry(0x658E9237),  // vignette00 (lenseflare vignette)
      CustomShaderEntry(0x77F0657A),  // doffocus00 (depth of field auto focus)
      CustomShaderEntry(0x6449D186),  // dofblur00 (depth of field blur)
      CustomShaderEntry(0x32876DB2),  // dofblur01 (depth of field masked blur)
      CustomShaderEntry(0x962CEB71),  // ssrcopy00 (screen-space reflections onto the scene)
      CustomShaderEntry(0x5EAD0FF6),  // highlight00 (gbuffer_highlight)
      CustomShaderEntry(0x6531CCC3),  // dofblur02 (depth of field blur, 8-bit)
      CustomShaderEntry(0xB25F26C7),  // dofcoc00 (depth of field circle of confusion + bokeh, through the heat haze)
      CustomShaderEntry(0xB333A1B3),  // dofcoc01 (without bokeh)
      CustomShaderEntry(0x34ECD06E),  // sss00 (sun shadow mask: screen-space shadows + cascades)
      CustomShaderEntry(0x1B529672),  // sss01 (screen-space shadows without cascades)
      CustomShaderEntry(0x78E88E1D),  // sss02 (cascades only, screen-space shadows off)
      // Remastered CE's builds of sss00 / sss01 (its edge rejection and banner exclusion): ours.
      {0x9147EB4E, renodx::mods::shader::CreateCustomShader(0x9147EB4E, __0x34ECD06E)},
      {0x7FA1D600, renodx::mods::shader::CreateCustomShader(0x7FA1D600, __0x1B529672)},
      CustomShaderEntry(0x97624E56),  // ssrhiz00 (reflections' HiZ base level)
      CustomShaderEntry(0x5C0AF498),  // ssrray00 (reflection rays)
      CustomShaderEntry(0x13C66077),  // ssrfilter00 (hit buffer blur x)
      CustomShaderEntry(0x073A6CF3),  // ssrfilter01 (hit buffer blur y)
      CustomShaderEntry(0xF87ECE85),  // ssrresolve00 (reflection colour)
      CustomShaderEntry(0x16ADFD60),  // ocean00 (ocean high: Ray-Traced Water Reflections)
      CustomShaderEntry(0x6B8954A6),  // ocean01 (ocean ultra: Ray-Traced Water Reflections)
      CustomShaderEntry(0x9B9EC116),  // waterplane00 (battle waterplane high / medium / ultra: Ray-Traced Water Reflections)
      CustomShaderEntry(0xA00FE3A3),  // ssao00 (ambient occlusion line sweeps)
      CustomShaderEntry(0x98BB1159),  // ssao01 (ambient occlusion collect: the Visibility Bitmask AO)
      CustomShaderEntry(0x5CBF4FB6),  // pointlight00
      CustomShaderEntry(0x57139F8D),  // pointlight01 (8-bit light accumulation)
      CustomShaderEntry(0x70CAA144),  // particlelight00 (light from particles)
      CustomShaderEntry(0xA583B6F4),  // ribbon00 (ribbon particles)
      CustomShaderEntry(0x8233E11F),  // autoexposure00
      CustomShaderEntry(0xA61D2853),  // fog00 (battle fog extinction)
      CustomShaderEntry(0x4355E897),  // fog01 (battle fog haze)
      CustomShaderEntry(0x3E33E81A),  // sun00 (sun disc)
      CustomShaderEntry(0x8BCB52BB),  // video00 (sprite pixel_yuv: the videos)
      CustomShaderEntry(0xAECABA3F),  // dissolve00 (rigidstandard alpha test G-buffer: fade pattern on the pixels)
      CustomShaderEntry(0x557A56D5),  // dissolve01 (rigidvertexwind G-buffer)
      CustomShaderEntry(0xED1BED93),  // sunlight00 (directional light: the sun)
      CustomShaderEntry(0x5496DE73),  // sunlight01 (8-bit)
      CustomShaderEntry(0xC3E0E939),  // sunlight02 (with the map overlay)
      CustomShaderEntry(0xD5FDDE8F),  // sunlight03 (with the map overlay, 8-bit)
      CustomShaderEntry(0x09117812),  // treevelocity00 (rigidleaf_v3 G-buffer: motion vectors)
      CustomShaderEntry(0xB670BCA1),  // treevelocity01 (rigidleafwet_v3)
      CustomShaderEntry(0x29EB778A),  // treevelocity02 (rigidbranch_v3)
      CustomShaderEntry(0x3A9E203D),  // treevelocity03 (rigidbranchwet_v3)
      CustomShaderEntry(0x3C9BC2F9),  // vfxoverlay00 (effects_overlay: the particles onto the scene)
      CustomShaderEntry(0x40744308),  // vfxglow00 (volumetric particles, additive)
      CustomShaderEntry(0xF70E8B73),  // vfxglow01 (volumetric particle splats, additive)
      CustomShaderEntry(0x03F58817),  // projectiletrail00 (arrow, bullet and artillery trails)
      CustomShaderEntry(0xBA0315BE),  // glowmesh00 (rigidglowcolour: burning buildings' fire panels)
      CustomShaderEntry(0x546A3049),  // glowmesh01 (rigidglowtexture)
      CustomShaderEntry(0xBDFF60D4),  // clothvelocity00 (cloth vertex shader: banners' motion vectors while paused)
      CustomShaderEntry(0x40E181C2),  // flagvelocity00 (rigidtextureblendwind: wind-blown flags, the same while paused)
      CustomShaderEntry(0xF45347E8),  // uifigure00 (variantweightedvtf ps30_main_ui: menu figures, clamped)
      CustomShaderEntry(0xA406D4BE),  // uifigure01 (rigidstandard ps30_main_ui)
      CustomShaderEntry(0xF7103E54),  // campaignwater00 (waterplane campaign high / medium / ultra: the map overlay)
      CustomShaderEntry(0x630BA7A4),  // campaignwater01 (waterplane campaign low)
  };

  // Ambient Occlusion: Visibility Bitmask: the line sweep skipped (its camera kept), the addon's own shader in place of
  // the collect (ao_bitmask_collect, with the collect's depth and the sweep's camera at b0); the collect's replacement
  // stays the game's code (Game's costs what it did). The first frame (no camera kept yet): nothing.
  custom_shaders[0xA00FE3A3].on_draw = [](auto* cmd_list) {
    tw::line_sweep_ao::OnBlur(cmd_list);
    return shader_injection.ssao_mode < 1.f;
  };
  custom_shaders[0x98BB1159].on_draw = [](auto* cmd_list) {
    if (shader_injection.ssao_mode < 1.f) return true;
    if (!tw::line_sweep_ao::OnCollect(cmd_list, true, false, 0, 0)) return false;
    tw::line_sweep_ao::DispatchInstead(cmd_list, __ao_bitmask_collect, &shader_injection, sizeof(shader_injection), false,
                                       __ao_upsample_collect, shader_injection.ssao_resolution != 0.f);
    tw::line_sweep_ao::OnCollectDone(cmd_list);
    return false;
  };

  // Reflections: the ray-traced ones in the resolve (ssrresolve00, twattila/rt_reflection.hlsli), its lit scene kept for
  // the next frame's; while they replace the screen-space march, what only the march reads is left out: its HiZ
  // (ssrhiz00 and its levels, cs 0xE2CE69ED), the scene's and the hit buffer's mip chains (cs 0x13A7F7D0, 0x4B546569,
  // 0x80196ECA: generic, left out only between the HiZ and the resolve), the march (ssrray00) and its blurs.
  custom_shaders[0x97624E56].on_draw = [](auto* cmd_list) {
    reflection_passes = ray_tracing_installed;
    if (ray_tracing_installed) twshogun2::ray_tracing::WaterSurface(cmd_list);
    return !ray_tracing_installed || twattila::rt_scene::OnReflectionMarch(cmd_list);
  };
  custom_shaders[0xF87ECE85].on_draw = [](auto* cmd_list) {
    reflection_passes = false;
    if (ray_tracing_installed) {
      twattila::rt_scene::BindReflections(cmd_list, true, true);
      twshogun2::ray_tracing::BindWaterMask(cmd_list, true, true);
    }
    return true;
  };
  custom_shaders[0xF87ECE85].on_drawn = [](auto* cmd_list) {
    if (!ray_tracing_installed) return;
    twattila::rt_scene::BindReflections(cmd_list, true, false);
    twshogun2::ray_tracing::BindWaterMask(cmd_list, true, false);
    twshogun2::ray_tracing::KeepColor(cmd_list);
  };
  // Ray-Traced Water Reflections (ray_tracing.hpp OnSeaDraw): the sea's plane, the rays' reflections for shaders/ocean00.
  for (const uint32_t hash : {0x16ADFD60u, 0x6B8954A6u}) {
    custom_shaders[hash].on_draw = [](auto* cmd_list) {
      if (ray_tracing_installed) twshogun2::ray_tracing::OnSeaDraw(cmd_list, true);
      return true;
    };
    custom_shaders[hash].on_drawn = [](auto* cmd_list) {
      if (ray_tracing_installed) twshogun2::ray_tracing::OnSeaDraw(cmd_list, false);
    };
  }
  for (const uint32_t hash : {0x5C0AF498u, 0x13C66077u, 0x073A6CF3u}) {
    custom_shaders[hash].on_draw = [](auto* cmd_list) { return !ray_tracing_installed || twattila::rt_scene::OnReflectionMarch(cmd_list); };
  }
  for (const uint32_t hash : {0xE2CE69EDu, 0x13A7F7D0u, 0x4B546569u, 0x80196ECAu}) {
    custom_shaders[hash] = {
        .crc32 = hash,
        .on_replace = [](reshade::api::command_list*) { return false; },
        .on_draw = [](auto* cmd_list) { return !reflection_passes || twattila::rt_scene::OnReflectionMarch(cmd_list); },
    };
  }

  // Light Through Foliage (foliage_light.hpp): the screen-space shadow passes' depth (sss00 and Remastered CE's at t3,
  // sss01 and its at t0) with the foliage negative while the row is on, the game's put back after.
  for (const auto [hash, depth_slot] : {std::pair{0x34ECD06Eu, 3u}, std::pair{0x9147EB4Eu, 3u}, std::pair{0x1B529672u, 0u},
                                        std::pair{0x7FA1D600u, 0u}}) {
    custom_shaders[hash].on_draw = [depth_slot](reshade::api::command_list* cmd_list) {
      twshogun2::foliage_light::OnShadowPass(cmd_list, depth_slot, shader_injection.sss_foliage_light);
      return true;
    };
    custom_shaders[hash].on_drawn = twshogun2::foliage_light::OnShadowPassDone;
  }
  // Render scale: captured on the game's own reflections copy and battle fog (drawn every frame before the depth of
  // field and the auto exposure), reset after the tonemaps.
  for (const uint32_t hash : {0x962CEB71u, 0xA61D2853u, 0x4355E897u}) {
    custom_shaders[hash].on_draw = render_scale::OnDrawCapture;
  }
  // Ray tracing's Debug View over the tone mapper's output.
  for (const uint32_t hash : {0xEF060C7Cu, 0xBEDBEFD8u}) {
    custom_shaders[hash].on_drawn = [](reshade::api::command_list* cmd_list) {
      render_scale::Set(1.f, 1.f);
      if (ray_tracing_installed) twshogun2::ray_tracing::OnTonemapDrawn(cmd_list);
      twshogun2::upscaling::KeepFrame(cmd_list);
    };
  }
  // The heat haze's distortion texture was drawn with DLSS's jitter (tonemap01 reads it there).
  custom_shaders[0xBEDBEFD8].on_draw = [](reshade::api::command_list*) {
    const auto jitter = twshogun2::dlss_jitter::Load();
    shader_injection.render_jitter_x = jitter[0];
    shader_injection.render_jitter_y = jitter[1];
    return true;
  };

  // VFX: the effects overlay draws the additive particles' buffer (blend dest ONE), then the alpha-blended one (smoke,
  // dust: dest source alpha, its transmittance).
  custom_shaders[0x3C9BC2F9].on_draw = [](reshade::api::command_list* cmd_list) {
    shader_injection.vfx_smoke_draw = 0.f;
    if (shader_injection.hdr < 1.f || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    context->OMGetBlendState(blend.GetAddressOf(), nullptr, nullptr);
    D3D11_BLEND_DESC desc = {};
    if (blend != nullptr) {
      blend->GetDesc(&desc);
    }
    const bool smoke = (desc.RenderTarget[0].BlendEnable != FALSE && desc.RenderTarget[0].DestBlend == D3D11_BLEND_SRC_ALPHA);
    shader_injection.vfx_smoke_draw = (smoke ? 1.f : 0.f);
    return true;
  };

  // The sea's colour overlay at full size (callbacks only, the game's shaders).
  for (const uint32_t hash : {0xC57C222Du, 0xC8375A74u, 0x06701006u}) {
    custom_shaders[hash] = {
        .crc32 = hash,
        .on_draw = sea_overlay::OnDraw,
        .on_drawn = sea_overlay::OnDrawn,
    };
  }

  // VFX effect test: one effect's fire & glow draws skipped, or all the others.
  auto shown_in_test = [](uint32_t id) {
    const bool selected = (id != 0 && id == twshogun2::vfx_effects::IdOf(static_cast<int>(vfx_test_effect)));
    return (vfx_test == 1.f) ? !selected : selected;
  };
  // Fire & glow draws: the slider their effect follows (vfx_effects.hpp; set in the test).
  auto brightness_of = [](uint32_t id) {
    switch (twshogun2::vfx_effects::KindOf(id)) {
      case twshogun2::vfx_effects::Kind::BIG_FIRE:
        return shader_injection.vfx_big_fire;
      case twshogun2::vfx_effects::Kind::CAMPFIRE:
        return shader_injection.vfx_campfire;
      default:
        return shader_injection.vfx_glow;
    }
  };
  // vfxglow00 / 01: the effect constants at their vertex shaders' b3 / b2, only looked up when the sliders differ or
  // in the test.
  for (const auto& [hash, slot] : {std::pair{0x40744308u, 3u}, std::pair{0xF70E8B73u, 2u}}) {
    custom_shaders[hash].on_draw = [slot, shown_in_test, brightness_of](reshade::api::command_list* cmd_list) {
      shader_injection.vfx_glow_draw = shader_injection.vfx_glow;
      const bool split = (shader_injection.hdr >= 1.f
                          && (shader_injection.vfx_big_fire != shader_injection.vfx_glow
                              || shader_injection.vfx_campfire != shader_injection.vfx_glow));
      if (!split && vfx_test == 0.f) return true;
      const auto effect = twshogun2::vfx_effects::DrawnParticles(cmd_list, slot);
      shader_injection.vfx_glow_draw = brightness_of(effect.id);
      if (vfx_test == 0.f) return true;
      if (effect.id != 0) {
        twshogun2::vfx_effects::Drawn(effect);
      }
      return shown_in_test(effect.id);
    };
  }
  for (const auto& [hash, id] : {std::pair{0xBA0315BEu, twshogun2::vfx_effects::GLOW_MESH_COLOR},
                                 std::pair{0x546A3049u, twshogun2::vfx_effects::GLOW_MESH_TEXTURE}}) {
    custom_shaders[hash].on_draw = [id, shown_in_test, brightness_of](reshade::api::command_list*) {
      shader_injection.vfx_glow_draw = brightness_of(id);
      if (vfx_test == 0.f) return true;
      twshogun2::vfx_effects::Drawn({.id = id});
      return shown_in_test(id);
    };
  }

  // 0-2 slider, 1 = the game's.
  auto slider = [](const std::string& key, float* binding, const std::string& label, const std::string& section,
                   const std::string& tooltip, bool (*is_enabled)(), bool (*is_visible)(), float default_value = 1.f) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = is_enabled,
        .is_visible = is_visible,
    };
  };

  // VFX (Extra (Experimental), HDR): 1 = the game's.
  auto vfx = [](const std::string& key, float* binding, const std::string& label, const std::string& tooltip,
                float max, const char* format = "%.1f") {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = 1.f,
        .label = label,
        .section = "Extra (Experimental)",
        .tooltip = tooltip + " 1 = the game's.",
        .min = 0.f,
        .max = max,
        .format = format,
        .is_enabled = IsHDRMode,
        .is_visible = IsAdvancedSettings,
    };
  };

  // VFX effect test (hidden; uncomment here and its settings below to use it again):
  // // VFX effect test: the previous / next effect drawn in the last frame.
  // auto test_step = [](const std::string& label, int step) {
  //   return new renodx::utils::settings::Setting{
  //       .value_type = renodx::utils::settings::SettingValueType::BUTTON,
  //       .label = label,
  //       .section = "VFX Effect Test",
  //       .group = "vfx-test-line",
  //       .is_enabled = [] { return vfx_test != 0.f; },
  //       .on_change = [step]() {
  //         const auto effects = twshogun2::vfx_effects::Effects();
  //         const int count = static_cast<int>(effects.size());
  //         for (int offset = 1; offset <= count; ++offset) {
  //           const int index = ((static_cast<int>(vfx_test_effect) - 1 + (step * offset)) % count + count) % count;
  //           if (!effects[index].on_screen) continue;
  //           renodx::utils::settings::UpdateSettings({{"vfx_test_effect", static_cast<float>(index + 1)}});
  //           return;
  //         }
  //       },
  //       .is_visible = IsAdvancedSettings,
  //   };
  // };
  //
  // // VFX effect test: the slider the selected effect follows.
  // auto test_assign = [](const std::string& label, twshogun2::vfx_effects::Kind kind) {
  //   return new renodx::utils::settings::Setting{
  //       .value_type = renodx::utils::settings::SettingValueType::BUTTON,
  //       .label = label,
  //       .section = "VFX Effect Test",
  //       .group = "vfx-assign-line",
  //       .tooltip = "The selected effect follows this slider (saved).",
  //       .is_enabled = [] { return vfx_test != 0.f; },
  //       .on_change = [kind]() {
  //         if (const uint32_t id = twshogun2::vfx_effects::IdOf(static_cast<int>(vfx_test_effect)); id != 0) {
  //           twshogun2::vfx_effects::SetKind(id, kind);
  //         }
  //       },
  //       .is_visible = IsAdvancedSettings,
  //   };
  // };

  // Highlight Hue Shift (as talesofarise): one amount per HDR mode, 0-1.
  auto hue_shift = [](const std::string& key, float* binding, const std::string& section, float default_value,
                      bool (*is_shown)()) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = "Highlight Hue Shift",
        .section = section,
        .tooltip = std::format("Bright colors take the game's SDR hues: fire more orange / yellow, less pink.\n"
                               "0 = off, 1 = SDR hues. Default {:.2f}.",
                               default_value),
        .min = 0.f,
        .max = 1.f,
        .format = "%.2f",
        .is_enabled = is_shown,
        .is_visible = is_shown,
    };
  };

  settings = {
      new renodx::utils::settings::Setting{
          .key = "SettingsMode",
          .binding = &current_settings_mode,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .can_reset = false,
          .label = "Settings Mode",
          .labels = {"Simple", "Advanced"},
          .is_global = true,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "The game's HDR sliders do nothing with this mod: use Peak, Paper White and UI below.",
          .section = "General",
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Use these in-game settings: Gamma 22 and Brightness 10.",
          .section = "General",
          .tint = 0xFFD23C,
      },
      new renodx::utils::settings::Setting{
          .key = "peak_white_nits",
          .binding = &shader_injection.peak_white_nits,
          .default_value = 1000.f,
          .can_reset = false,
          .label = "Peak",
          .section = "General",
          .tooltip = "Brightest output in nits.",
          .min = 400.f,
          .max = 10000.f,
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .key = "diffuse_white_nits",
          .binding = &shader_injection.diffuse_white_nits,
          .default_value = 203.f,
          .label = "Paper White",
          .section = "General",
          .tooltip = "Brightness of the game and videos in nits.",
          .min = 1.f,
          .max = 500.f,
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .key = "graphics_white_nits",
          .binding = &shader_injection.graphics_white_nits,
          .default_value = 203.f,
          .label = "UI",
          .section = "General",
          .tooltip = "Brightness of the UI and HUD in nits.",
          .min = 1.f,
          .max = 500.f,
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .key = "tonemapper",
          .binding = &shader_injection.hdr,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Rendering Mode",
          .section = "General",
          .tooltip = "SDR: the game's SDR.\n"
                     "HDR (Extended): the game's own tone curve, extended into HDR. Default.\n"
                     "HDR (PsychoV31): PsychoV31 instead of the game's curve.\n"
                     "HDR (PsychoV35): Musa's PsychoV35 instead of it, with Hue Clip Emulation (Recommended).\n"
                     "SDR output: HDR (Extended) keeps the game's curve, PsychoV31 and PsychoV35 are mapped to SDR.",
          .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "HDR (PsychoV35), no lens flares, faint foliage glints, brighter sun, VFX and videos.\n"
                     "Keeps Peak, Paper White, UI and output.",
          .on_change = []() { ApplyRecommendedPreset(); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (SDR-like Contrast/Saturation)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended, with contrast and saturation closer to the game's SDR.\n"
                     "Keeps Peak, Paper White, UI and output.",
          .on_change = []() {
            ApplyRecommendedPreset();
            renodx::utils::settings::UpdateSettings({
                {"highlights", 1.00f},
                {"cone_response_exponent", 1.3f},
                {"highlight_saturation", 1.00f},
                {"roll_off", 2.00f},
                {"v35_contrast", 1.3f},
                {"v35_roll_off", 2.00f},
            });
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "All image settings back to their defaults (HDR Extended).\n"
                     "Keeps Peak, Paper White, UI and output.",
          .on_change = []() { ApplyResetPreset(); },
      },

      // Extended ////////////////////////////////////////////////////////////////////////////////////
      slider("ext_exposure", &shader_injection.ext_exposure, "Exposure", "Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_highlights", &shader_injection.ext_highlights, "Highlights", "Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_shadows", &shader_injection.ext_shadows, "Shadows", "Extended", "", IsExtendedMode, IsExtendedMode),
      slider("ext_contrast", &shader_injection.ext_contrast, "Contrast", "Extended", "1 = the game's.",
             IsExtendedMode, IsExtendedMode),
      slider("ext_saturation", &shader_injection.ext_saturation, "Saturation / Purity", "Extended", "1 = like SDR.",
             IsExtendedMode, IsExtendedMode),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink, too orange or too pale? Change the settings below.",
          .section = "Extended",
          .is_visible = IsExtendedMode,
      },
      slider("ext_highlight_saturation", &shader_injection.ext_highlight_saturation, "Highlight Saturation",
             "Extended", "How much bright colors lose saturation. 0 = more, 2 = less.", IsExtendedMode,
             IsExtendedMode),
      hue_shift("ext_highlight_hue_shift", &shader_injection.ext_highlight_hue_shift, "Extended", 0.33f,
                IsExtendedMode),

      // PsychoV31 ///////////////////////////////////////////////////////////////////////////////////
      slider("exposure", &shader_injection.exposure, "Exposure", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("highlights", &shader_injection.highlights, "Highlights", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("shadows", &shader_injection.shadows, "Shadows", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("contrast", &shader_injection.contrast, "Contrast", "PsychoV31", "1 = PsychoV31's own. Keeps saturation.",
             IsPsychoMode, IsPsychoMode),
      slider("cone_response_exponent", &shader_injection.cone_response_exponent, "Cone Response Exponent", "PsychoV31",
             "Like Contrast, but also changes saturation. 1 = PsychoV31's own.",
             IsPsychoMode, IsPsychoMode),
      slider("highlight_contrast", &shader_injection.highlight_contrast, "Highlight Contrast", "PsychoV31",
             "Contrast above mid gray. 1 = PsychoV31's own.", IsPsychoMode, IsPsychoMode),
      slider("shadow_contrast", &shader_injection.shadow_contrast, "Shadow Contrast", "PsychoV31",
             "Contrast below mid gray. 1 = PsychoV31's own.", IsPsychoMode, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "flare",
          .binding = &shader_injection.flare,
          .default_value = 0.f,
          .label = "Flare",
          .section = "PsychoV31",
          .tooltip = "Deepens the darkest shadows. 0 = off.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .parse = [](float value) { return std::pow(value, 5.f); },
          .is_visible = IsPsychoMode,
      },
      slider("saturation", &shader_injection.saturation, "Saturation / Purity", "PsychoV31", "1 = PsychoV31's own.",
             IsPsychoMode, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "dechroma",
          .binding = &shader_injection.dechroma,
          .default_value = 0.f,
          .label = "Dechroma",
          .section = "PsychoV31",
          .tooltip = "Very bright colors fade toward white, as if overexposed. 0 = off.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      slider("roll_off", &shader_injection.roll_off, "Highlight Roll-off", "PsychoV31",
             "How early highlights compress toward Peak. 0 = latest, 2 = soonest.",
             IsPsychoMode, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "gamut_compression",
          .binding = &shader_injection.gamut_compression,
          .default_value = 1.f,
          .label = "Gamut Compression",
          .section = "PsychoV31",
          .tooltip = "Pulls colors your display can't show back inside its color space.\n"
                     "0 = off (the display clips them), 1 = full (default).",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .key = "gamut_compression_mode",
          .binding = &shader_injection.gamut_compression_mode,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Gamut Compression Mode",
          .section = "PsychoV31",
          .tooltip = "The color space colors are fitted into.\n"
                     "BT.2020: the whole HDR signal (default).\n"
                     "Display P3: what most HDR monitors and TVs show. BT.709: SDR colors.",
          .labels = {"BT.709", "BT.2020", "Display P3"},
          .is_enabled = []() { return IsPsychoMode() && IsHdrOutput(); },
          // PsychoV31's modes: 0 BT.709, 1 BT.2020, 3 Display P3.
          .parse = [](float value) { return (value == 2.f ? 3.f : value); },
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink, too orange or too pale? Change the settings below.",
          .section = "PsychoV31",
          .is_visible = IsPsychoMode,
      },
      slider("highlight_saturation", &shader_injection.highlight_saturation, "Highlight Saturation", "PsychoV31",
             "Saturation of bright colors. 0 = less, 2 = more.", IsPsychoMode, IsPsychoMode),
      hue_shift("highlight_hue_shift", &shader_injection.highlight_hue_shift, "PsychoV31", 0.46f, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "highlight_saturation_shift",
          .binding = &shader_injection.highlight_saturation_shift,
          .default_value = 0.f,
          .label = "Highlight Saturation Shift",
          .section = "PsychoV31",
          .tooltip = "Gives bright colors the saturation of the game's SDR look (fire cores less pale and pink).\n"
                     "0 = off (default), 1 = SDR's.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },

      // Extra (HDR only) /////////////////////////////////////////////////////////////////////////
      slider("bloom", &shader_injection.bloom, "Bloom", "Extra", "Glow around bright lights.", IsHDRMode, IsAdvancedSettings),
      slider("lens_flare", &shader_injection.lens_flare, "Lens Flares", "Extra",
             "Sun flares and ghosts around bright lights.", IsHDRMode, IsAdvancedSettings),
      slider("vignette", &shader_injection.vignette, "Vignette", "Extra", "Darker screen edges.",
             IsHDRMode, IsAdvancedSettings),
      slider("heat_haze", &shader_injection.heat_haze, "Distortion and Haze", "Extra",
             "Heat haze over fires and other distortion.", IsHDRMode, IsAdvancedSettings),
      slider("depth_of_field", &shader_injection.depth_of_field, "Depth of Field", "Extra", "Depth of field blur strength.",
             IsHDRMode, IsAdvancedSettings),
      slider("battle_fog", &shader_injection.battle_fog, "Battle Fog", "Extra",
             "Battlefield fog, haze and distance blur.", IsHDRMode, IsAdvancedSettings),
      new renodx::utils::settings::Setting{
          .key = "sun",
          .binding = &shader_injection.sun,
          .default_value = 1.f,
          .label = "Sun",
          .section = "Extra",
          .tooltip = "Brightness of the sun disc (evenly bright, as in the game). 1 = the game's.",
          .min = 0.f,
          .max = 20.f,
          .format = "%.1f",
          .is_enabled = IsHDRMode,
          .is_visible = IsAdvancedSettings,
      },
      slider("foliage_glints", &shader_injection.foliage_glints, "Foliage Sun Glints", "Extra",
             "Sun shine on grass, flowers and leaves (flickering white specks). 0 = off.",
             nullptr, nullptr),
      vfx("vfx_glow", &shader_injection.vfx_glow, "VFX Brightness: Fire & Glow",
          "Fire, sparks, explosions and muzzle flashes; not burning houses or campfires.", 10.f),
      vfx("vfx_big_fire", &shader_injection.vfx_big_fire, "VFX Brightness: Big Fires",
          "Burning buildings, castles and walls.", 10.f),
      vfx("vfx_campfire", &shader_injection.vfx_campfire, "VFX Brightness: Campfires", "Campfire flames.", 50.f, "%.0f"),
      vfx("vfx_trails", &shader_injection.vfx_trails, "VFX Brightness: Trails",
          "Arrow, bullet and artillery trails.", 10.f),
      vfx("vfx_firelight", &shader_injection.vfx_firelight, "VFX Brightness: Firelight",
          "Light from fires, explosions, torches and lanterns.", 10.f),
      vfx("vfx_smoke", &shader_injection.vfx_smoke, "VFX Brightness: Smoke & Dust",
          "Lit smoke and dust. Black smoke stays black (see VFX Opacity).", 10.f),
      vfx("vfx_smoke_opacity", &shader_injection.vfx_smoke_opacity, "VFX Opacity: Smoke & Dust",
          "Thickness of smoke and dust, black smoke too.", 3.f),
      new renodx::utils::settings::Setting{
          .key = "video_autohdr",
          .binding = &shader_injection.video_autohdr,
          .default_value = 0.f,
          .label = "Video AutoHDR",
          .section = "Extra",
          .tooltip = "Brighter highlights in the game's videos. 0 = off. High values can show banding.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = [] { return IsHDRMode() && IsHdrOutput(); },
          .is_visible = IsAdvancedSettings,
      },
      new renodx::utils::settings::Setting{
          .key = "video_dithering",
          .binding = &shader_injection.video_dithering,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Video Dithering",
          .section = "Extra",
          .tooltip = "Hides banding in brightened videos.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return IsHDRMode() && IsHdrOutput() && shader_injection.video_autohdr > 0.f; },
          .is_visible = IsAdvancedSettings,
      },
      // VFX effect test (hidden):
      // new renodx::utils::settings::Setting{
      //     .key = "vfx_test",
      //     .binding = &vfx_test,
      //     .value_type = renodx::utils::settings::SettingValueType::INTEGER,
      //     .default_value = 0.f,
      //     .label = "VFX Effect Test",
      //     .section = "VFX Effect Test",
      //     .tooltip = "Find a fire or glow effect (hide it, or show only it), then set the slider it follows.",
      //     .labels = {"Off", "Hide Effect", "Only Effect"},
      //     .is_visible = IsAdvancedSettings,
      // },
      // new renodx::utils::settings::Setting{
      //     .key = "vfx_test_effect",
      //     .binding = &vfx_test_effect,
      //     .value_type = renodx::utils::settings::SettingValueType::INTEGER,
      //     .default_value = 1.f,
      //     .label = "Effect",
      //     .section = "VFX Effect Test",
      //     .tooltip = "Effects are numbered in the order they first appear.",
      //     .min = 1.f,
      //     .max = 200.f,
      //     .is_enabled = [] { return vfx_test != 0.f; },
      //     .is_visible = IsAdvancedSettings,
      // },
      // test_step("Previous on Screen", -1),
      // test_step("Next on Screen", 1),
      // new renodx::utils::settings::Setting{
      //     .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
      //     .section = "VFX Effect Test",
      //     .on_draw = [] {
      //       const auto effects = twshogun2::vfx_effects::Effects();
      //       const auto on_screen = std::ranges::count_if(effects, [](const auto& effect) { return effect.on_screen; });
      //       std::string text = std::format("{} effects seen, {} on screen ({} read back).", effects.size(), on_screen,
      //                                      twshogun2::vfx_effects::ReadBacks());
      //       const int number = static_cast<int>(vfx_test_effect);
      //       if (number >= 1 && number <= static_cast<int>(effects.size())) {
      //         const auto& effect = effects[number - 1];
      //         const bool glow_mesh = (effect.id == twshogun2::vfx_effects::GLOW_MESH_COLOR
      //                                 || effect.id == twshogun2::vfx_effects::GLOW_MESH_TEXTURE);
      //         text += (glow_mesh ? std::format("\nEffect {}: glow mesh, ID {}", number, effect.id)
      //                            : std::format("\nEffect {}: ID {:08X}, emissive {:.2f} to {:.2f}", number, effect.id,
      //                                          effect.emissive_min, effect.emissive_max));
      //         text += (effect.on_screen ? "" : " (not on screen)");
      //         switch (twshogun2::vfx_effects::KindOf(effect.id)) {
      //           case twshogun2::vfx_effects::Kind::BIG_FIRE:
      //             text += "\nFollows: Big Fires";
      //             break;
      //           case twshogun2::vfx_effects::Kind::CAMPFIRE:
      //             text += "\nFollows: Campfires";
      //             break;
      //           default:
      //             text += "\nFollows: Fire & Glow";
      //             break;
      //         }
      //       }
      //       ImGui::TextUnformatted(text.c_str());
      //       return false;
      //     },
      //     .is_visible = [] { return IsAdvancedSettings() && vfx_test != 0.f; },
      // },
      // test_assign("Set as Fire & Glow", twshogun2::vfx_effects::Kind::FIRE_AND_GLOW),
      // test_assign("Set as Big Fires", twshogun2::vfx_effects::Kind::BIG_FIRE),
      // test_assign("Set as Campfires", twshogun2::vfx_effects::Kind::CAMPFIRE),
  };
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));
}


bool configured = false;
bool attached = false;
bool addon_registered = false;

// The DLSS addon's graphics options for shaders here (its overlay, ReShade.ini [Shogun 2 DLSS]): read at attach, each
// frame while ReShade's overlay is open (where they change) and as it closes. Not on reshade_overlay: an add-on with
// that event makes ReShade build its overlay every frame, open or not.
void ReadDlssAddonOptions() {
  int ssr_resolution = 1;
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSRResolution", ssr_resolution);
  shader_injection.ssr_resolution = static_cast<float>(ssr_resolution);
  int sss_samples = 0;
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSSamples", sss_samples);
  shader_injection.sss_samples = static_cast<float>(std::clamp(sss_samples, 0, 5));
  int sss_stability = 0;
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSStability", sss_stability);
  shader_injection.sss_stability = static_cast<float>(std::clamp(sss_stability, 0, 2));
  // Bend's parameters (0: the game's).
  const auto read_bend = [](const char* key, float max, float* setting) {
    float value = 0.f;
    reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
    *setting = std::clamp(value, 0.f, max);
  };
  read_bend("SSSThickness", 0.05f, &shader_injection.sss_thickness);
  read_bend("SSSContrast", 8.f, &shader_injection.sss_contrast);
  read_bend("SSSEdgeThreshold", 0.1f, &shader_injection.sss_edge_threshold);
  read_bend("SSSPrecisionOffset", 1.f, &shader_injection.sss_precision_offset);
  read_bend("SSSFoliageLight", 1.f, &shader_injection.sss_foliage_light);
  read_bend("SSSIgnoreEdges", 1.f, &shader_injection.sss_ignore_edges);
  read_bend("SSSAlongRay", 1.f, &shader_injection.sss_along_ray);
  read_bend("SSSMirrorEdges", 1.f, &shader_injection.sss_mirror_edges);
  // The DLSS addon's choices (0 the game's: 8 and 16, its sss.hlsli's HARD_SHADOW_SAMPLES / FADE_OUT_SAMPLES).
  constexpr std::array<float, 4> HARD_SAMPLES = {0.f, 4.f, 16.f, 32.f};
  constexpr std::array<float, 5> FADE_SAMPLES = {0.f, 4.f, 8.f, 32.f, 64.f};
  int hard_samples = 0;
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSHardSamples", hard_samples);
  shader_injection.sss_hard_samples = HARD_SAMPLES[std::clamp(hard_samples, 0, static_cast<int>(HARD_SAMPLES.size()) - 1)];
  int fade_samples = 0;
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSFadeSamples", fade_samples);
  shader_injection.sss_fade_samples = FADE_SAMPLES[std::clamp(fade_samples, 0, static_cast<int>(FADE_SAMPLES.size()) - 1)];
  int shadow_map_stability = 0;
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapStability", shadow_map_stability);
  shader_injection.shadow_map_stability = static_cast<float>(std::clamp(shadow_map_stability, 0, 2));
  float sun_shadow_softness = 0.f;
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SunShadowSoftness", sun_shadow_softness);
  shader_injection.sun_shadow_softness = std::clamp(sun_shadow_softness, 0.f, 5.f);
  // The ambient occlusion and Global Illumination (the DLSS addon's sections, twattila/lighting_settings.hpp's
  // defaults and ranges).
  const auto read_float = [](const char* key, float default_value, float min, float max, float* setting) {
    float value = default_value;
    reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
    *setting = std::clamp(value, min, max);
  };
  const auto read_choice = [](const char* key, int default_value, int count, float* setting) {
    int value = default_value;
    reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
    *setting = static_cast<float>(std::clamp(value, 0, count - 1));
  };
  read_choice("AmbientOcclusion", 1, 2, &shader_injection.ssao_mode);
  read_float("AmbientOcclusionStrength", 1.f, 0.f, 3.f, &shader_injection.ssao_strength);
  read_float("AmbientOcclusionRadius", 12.5f, 0.25f, 20.f, &shader_injection.ssao_radius);
  read_float("AmbientOcclusionThickness", 0.1f, 0.01f, 3.f, &shader_injection.ssao_thickness);
  read_choice("AmbientOcclusionQuality", 2, 6, &shader_injection.ssao_quality);
  read_choice("AmbientOcclusionDirections", 0, 4, &shader_injection.ssao_directions);
  read_choice("AmbientOcclusionResolution", 0, 2, &shader_injection.ssao_resolution);
  read_choice("AmbientOcclusionAutoThickness", 1, 2, &shader_injection.ssao_auto_thickness);
  read_float("LightBounceStrength", 0.f, 0.f, 4.f, &shader_injection.light_bounce_strength);
  read_float("LightBounceRadius", 20.f, 0.5f, 20.f, &shader_injection.light_bounce_radius);
  read_choice("LightBounceResolution", 2, 4, &shader_injection.light_bounce_resolution);
  read_choice("LightBounceQuality", 3, 6, &shader_injection.light_bounce_quality);
  read_choice("LightBounceDirections", 0, 5, &shader_injection.light_bounce_directions);
  read_choice("LightBounceTemporal", 1, 2, &shader_injection.light_bounce_temporal);
  read_choice("LightBounceDenoise", 1, 2, &shader_injection.light_bounce_denoise);
  read_float("LightBounceMultiBounce", 0.f, 0.f, 1.f, &shader_injection.light_bounce_multi_bounce);
  read_choice("LightBounceCheckerboard", 1, 2, &shader_injection.light_bounce_checkerboard);
  read_choice("LightBounceBackFace", 0, 2, &shader_injection.light_bounce_backface);
  read_choice("LightBounceAutoThickness", 1, 2, &shader_injection.light_bounce_auto_thickness);
  // Ray tracing (the DLSS addon's Ray Tracing section; Attila's keys and ranges where it has the row).
  auto& ray_tracing = ray_tracing_settings;
  const auto read_on = [](const char* key, bool default_value) {
    int value = default_value ? 1 : 0;
    reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
    return value != 0;
  };
  const auto read_count = [](const char* key, int default_value, int min, int max) {
    int value = default_value;
    reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
    return static_cast<uint32_t>(std::clamp(value, min, max));
  };
  ray_tracing.sun_shadows = read_on("RayTracedSunShadows", false);
  read_float("RayTracedShadowSoftness", 0.5f, 0.f, 3.f, &ray_tracing.sun_softness);
  ray_tracing.leaf_light = read_on("RayTracedLeafLight", false);
  ray_tracing.coloured_shadows = ray_tracing.leaf_light && read_on("RayTracedColouredShadows", false);
  ray_tracing.sprite_shadows = read_on("RayTracedSpriteShadows", false);
  ray_tracing.far_tree_shadows = read_on("RayTracedFarTreeShadows", false);
  ray_tracing.casters_out_of_view = ray_tracing.sun_shadows && read_on("RayTracedUnitsOutOfView", false);
  // Attila's Smooth Banner Shadows (RT_SHEETS_UNSELFED: the cloth the pixel shows doesn't shade itself) and Grass Shadow Range
  // (rays from pixels farther than it pass the grass), both the helper's.
  ray_tracing.sheets_unselfed = read_on("RayTracedSheetsUnselfed", false);
  read_float("RayTracedGrassShadowRange", 0.f, 0.f, 200.f, &ray_tracing.grass_shadow_range);
  ray_tracing.ambient_occlusion = read_on("RayTracedAmbientOcclusion", false);
  read_float("RayTracedAmbientRange", 2.f, 0.25f, 5.f, &ray_tracing.ambient_range);
  ray_tracing.ambient_rays = read_count("RayTracedAORays", 2, 1, 8);
  ray_tracing.ambient_quarter = read_on("RayTracedAOQuarter", false);
  // Attila's AO under Ray Reconstruction: 1 REBLUR (RT_AO_REBLUR, only while Ray Reconstruction runs), 0 the light history.
  ray_tracing.ao_reblur = read_on("RayTracedAoReblur", false);
  ray_tracing.reflections = read_on("RayTracedReflections", false);
  ray_tracing.specular_occlusion = read_on("RayTracedSpecularOcclusion", false);
  ray_tracing.water_reflections = read_on("RayTracedWaterReflections", false);
  ray_tracing.reflection_samples = read_count("RayTracedReflectionSamples", 1, 1, 8);
  ray_tracing.water_steady = read_on("RayTracedWaterSteady", false);
  // Softer Water Reflections (Attila's): the water's guide rougher (ray_tracing.hpp WaterSurface), its reflections' history
  // from the water's own positions (rt_core's RT_REFLECTIONS_WATER_POSITIONS).
  ray_tracing.water_soft = ray_tracing.water_reflections && read_on("RayTracedWaterSoft", false);
  ray_tracing.local_lights = read_on("RayTracedLocalLights", false);
  ray_tracing.fog_shadows = read_on("RayTracedFogShadows", false);
  // No torch light in the fog (fog01 takes the sun's part alone): the helper traces none.
  ray_tracing.light_glow = 0.f;
  // Torch and Fire Shadow Lights: 32, 64, 128.
  ray_tracing.light_count = twattila::dlaa_bridge::RT_MAX_LIGHTS << read_count("RayTracedLightCount", 0, 0, 2);
  ray_tracing.light_importance = read_on("RayTracedLightImportance", false);
  ray_tracing.light_steady = read_on("RayTracedLightSteady", false);
  read_float("RayTracedLightRange", 0.f, 0.f, 500.f, &ray_tracing.light_range);
  ray_tracing.light_bounce = read_on("RayTracedLightBounce", false);
  // Reflection Reuse (Attila's): 1 from ReSTIR GI (the ray-traced Global Illumination's), 2 Own ReSTIR.
  const uint32_t reflection_reuse = read_count("RayTracedReflectionReuse", 0, 0, 2);
  ray_tracing.reflection_reuse = reflection_reuse == 1 && ray_tracing.light_bounce;
  ray_tracing.reflection_own = reflection_reuse == 2;
  read_float("RayTracedLightBounceRange", 10.f, 1.f, 50.f, &ray_tracing.light_bounce_range);
  ray_tracing.light_bounce_rays = read_count("RayTracedLightBounceRays", 4, 1, 16);
  read_choice("RayTracedLightBounceResolution", 1, 3, &ray_tracing.light_bounce_resolution);
  ray_tracing.path_tracing = read_on("PathTracing", false);
  // Path-Traced AO (twattila/rt_scene.hpp SettingsOf's): the paths bring the sky's light, the ray-traced AO off. Needs
  // the ray-traced Global Illumination's paths and the Global Illumination (its apply adds them).
  ray_tracing.path_ambient = ray_tracing.path_tracing && ray_tracing.light_bounce && shader_injection.light_bounce_strength > 0.f
                             && read_on("RayTracedPathAmbient", false);
  if (ray_tracing.path_ambient) ray_tracing.ambient_occlusion = false;
  // Own Sky Rays and Steady Bounce Light (rt_sun's texel 3 w 1 and 4 w 1: its cube is the sky, in the game's units).
  ray_tracing.path_sky_apart = read_on("RayTracedPathSkyApart", false);
  ray_tracing.path_kept_cube = read_on("RayTracedPathKeptCube", false);
  // 3 and 4 measured as 2 in Attila (twattila/rt_scene.hpp SettingsOf): the key's older values taken as 2.
  ray_tracing.path_bounces = read_count("PathTracingBounces", 2, 1, 2);
  ray_tracing.sharc = ray_tracing.path_tracing && read_on("RayTracedSharc", false);
  ray_tracing.reuse_lean = ray_tracing.path_tracing && read_on("RayTracedReuseLean", false);
  ray_tracing.path_ser = read_on("RayTracedPathSer", false);
  ray_tracing.micromaps = read_on("RayTracedLeafMicromaps", false);
  ray_tracing.gpu_micromaps = read_on("RayTracedGpuMicromaps", true);
  ray_tracing.fast_leaves = read_on("RayTracedFastLeafShadows", false);
  ray_tracing.debug_view = read_count("RayTracedDebugView", 0, 0, 9);
  ray_tracing.scene_test = (ray_tracing.debug_view != 0) ? read_count("RayTracedSceneTest", 0, 0, 4) : 0u;
  ray_tracing.debug_log = read_on("RayTracedDebugLog", false);
  rays_a_pixel = {
      .choice = read_count("RayTracingOutputResolution", 0, 0, 8),
      .shadows = read_on("RayTracingRaysShadows", true),
      .ambient = read_on("RayTracingRaysAmbient", true),
      .reflections = read_on("RayTracingRaysReflections", true),
      .paths = read_on("RayTracingRaysPaths", true),
  };
  // With the helper's upscaler (OnPresent): NRD, the torches' size under Ray Reconstruction.
  ray_tracing.nrd = read_on("RayTracingDenoiser", false);
  read_float("RayTracedLightSoftness", 0.25f, 0.f, 1.f, &ray_tracing.light_size);
}

std::atomic_bool overlay_open = false;

bool OnOpenOverlay(reshade::api::effect_runtime*, bool open, reshade::api::input_source) {
  overlay_open.store(open, std::memory_order_relaxed);
  if (!open) {
    ReadDlssAddonOptions();
  }
  return false;
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  if (overlay_open.load(std::memory_order_relaxed)) {
    ReadDlssAddonOptions();
  }
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  // The helper's frame generation and Reflex (the DLSS addon's rows), the bridge: what it serves this frame
  // (upscaling.hpp); Ray Reconstruction with a ray-traced effect only.
  const auto choices = twshogun2::hdr_link::ChoicesNow();
  twattila::frame_generation::Update({
      .multiplier = choices.frame_generation,
      .reflex_boost = choices.reflex == 2,
      .frame_limit = choices.frame_limit,
      .preset = choices.frame_preset,
      .hud_recomposition = choices.hud_recomposition,
      .debug_view = choices.frame_debug_view,
      .sdr_output = !IsHdrOutput(),
  });
  twattila::reflex::Update(swapchain, choices.reflex, choices.frame_limit);
  auto rays = ray_tracing_settings;
  const bool any_rays = ray_tracing_installed
                        && (rays.sun_shadows || rays.ambient_occlusion || rays.light_bounce || rays.reflections || rays.local_lights
                            || rays.fog_shadows || rays.specular_occlusion);
  twshogun2::upscaling::SetUse({
      .ray_tracing = ray_tracing_installed,
      .guides = ray_tracing_installed && (rays.reflections || rays.path_tracing),
      .reflections = ray_tracing_installed && rays.reflections,
      .upscaler = choices.upscaler,
      .ray_reconstruction = any_rays ? choices.ray_reconstruction : 0u,
      .frame_format = twattila::frame_generation::Format(),
      .frame_backend = choices.frame_backend,
      .debug_view = choices.upscale_debug_view,
  });
  if (ray_tracing_installed) {
    // NRD reads the upscaler's motion and depth: only with the helper's.
    rays.nrd = rays.nrd && choices.upscaler != 0;
    // Rays a Pixel: Match Output, those of the output pixels a render pixel stands for (at most 8).
    const auto render = twshogun2::ray_tracing::HOST.render_size();
    const uint32_t display = render_scale::back_buffer_width.load();
    const float scale = (render[0] != 0 && display != 0) ? std::min(static_cast<float>(render[0]) / static_cast<float>(display), 1.f) : 1.f;
    const auto density = static_cast<uint32_t>((rays_a_pixel.choice == 1) ? std::clamp(std::lround(1.f / (scale * scale)), 1l, 8l)
                                                                          : std::clamp(rays_a_pixel.choice, 1u, 8u));
    const auto rays_of = [density](bool on) { return on ? density : 1u; };
    rays.shadow_rays = rays_of(rays_a_pixel.shadows);
    rays.ambient_rays *= rays_of(rays_a_pixel.ambient);
    rays.reflection_samples *= rays_of(rays_a_pixel.reflections);
    rays.light_bounce_rays = std::min(rays.light_bounce_rays * rays_of(rays_a_pixel.paths), 16u);
    twattila::light_bounce::SetRayTraced(rays.light_bounce, rays.light_bounce_resolution);
    twshogun2::ray_tracing::Update(rays);
    if (twshogun2::hdr_link::TakeProfileRequest()) twattila::rt_scene::StartProfile();
  }
  reflection_passes = false;
  twshogun2::vfx_effects::OnPresent();
  static uint32_t shadow_frame = 0;
  shader_injection.shadow_noise_frame = static_cast<float>(++shadow_frame % 64);
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  const auto desc = swapchain->get_device()->get_resource_desc(swapchain->get_back_buffer(0));
  render_scale::back_buffer_width = desc.texture.width;
  render_scale::back_buffer_height = desc.texture.height;

  auto peak = renodx::utils::swapchain::GetPeakNits(swapchain);
  if (!peak.has_value()) {
    peak = 1000.f;
  }

  for (auto& setting : settings) {
    if (setting->binding != &shader_injection.peak_white_nits) continue;
    setting->default_value = peak.value();
    setting->max = std::max(peak.value(), setting->max);
    setting->can_reset = true;
    break;
  }
}

void ConfigureAddon() {
  if (configured) return;

  BuildRuntimeData();

  // Shaders cb
  renodx::mods::shader::expected_constant_buffer_space = 50;
  renodx::mods::shader::expected_constant_buffer_index = 13;
  render_scale::Set(1.f, 1.f);

  // Swapchain cb
  renodx::mods::swapchain::expected_constant_buffer_index = 13;
  renodx::mods::swapchain::expected_constant_buffer_space = 50;

  renodx::mods::swapchain::use_resource_cloning = true;
  renodx::mods::swapchain::force_borderless = false;
  renodx::mods::swapchain::prevent_full_screen = true;

  renodx::mods::swapchain::swap_chain_proxy_shaders = {
      {
          reshade::api::device_api::d3d11,
          {
              .vertex_shader = __swap_chain_proxy_vertex_shader_dx11,
              .pixel_shader = __swap_chain_proxy_pixel_shader_dx11,
          },
      },
  };

  // The tonemap's output, the UI target and the frame after them: 8-bit (the game's HDR: 10-bit) at back-buffer
  // aspect, upgraded at creation.
  for (auto format : {
           reshade::api::format::r8g8b8a8_typeless,
           reshade::api::format::r8g8b8a8_unorm,
           reshade::api::format::r8g8b8a8_unorm_srgb,
           reshade::api::format::b8g8r8a8_typeless,
           reshade::api::format::b8g8r8a8_unorm,
           reshade::api::format::b8g8r8a8_unorm_srgb,
           reshade::api::format::r10g10b10a2_typeless,
           reshade::api::format::r10g10b10a2_unorm,
       }) {
    renodx::mods::swapchain::resource_upgrade_infos.push_back({
        .old_format = format,
        .new_format = reshade::api::format::r16g16b16a16_float,
        .use_resource_view_cloning = true,
        .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
        .usage_include = reshade::api::resource_usage::render_target,
    });
  }

  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR. Default. Frame generation uses HDR10 for scRGB.\n"
                   "HDR10: 10-bit. scRGB: 16-bit. SDR: displays without HDR.",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const int value = static_cast<int>(setting->GetValue());
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = value,
        .hdr_encoding = [](int output) {
          // The DLSS addon's frame generation (its HelperFrameGeneration since its v58; Tales' FrameGeneration is no longer
          // written) takes no scRGB: HDR10 with it.
          bool frame_generation = false;
          reshade::get_config_value(nullptr, "Shogun 2 DLSS", "HelperFrameGeneration", frame_generation);
          return (output == twattila::output_encoding::HDR10 || frame_generation) ? twattila::output_encoding::HDR10 : twattila::output_encoding::SCRGB;
        },
    });
    settings.push_back(setting);
  }

  // Bend Studio's screen-space shadows (shipped in the game's shaders.pack, changed here) are Apache 2.0: its license
  // ships as APACHE_LICENSE-2.0.txt (docs/licenses).
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom PsychoV31\n"
                   "and PsychoV35.\n"
                   "Screen-space shadows: Bend Studio, Copyright 2023 Sony Interactive Entertainment, Apache License 2.0\n"
                   "(APACHE_LICENSE-2.0.txt).",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::HELPER,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::DLSS_ADDON,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
          .section = "About",
          .is_visible = IsAdvancedSettings,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "ATN's Ko-fi",
          .section = "About",
          .group = "line_1",
          .tint = 0xFF5A16,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://ko-fi.com/atnthegreat"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "NexusMods (For RenoDX + DLSS)",
          .section = "About",
          .group = "line_1",
          .tint = 0x2196F3,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarshogun2completeedition/mods/3"); },
      },
  });

  // Section titles: one colour, another for Graphics (Experimental) (ATN, as Attila).
  renodx::utils::settings::default_section_tint = 0x7EE08A;
  renodx::utils::settings::section_tints = {{"Graphics (Experimental)", 0xFF7FAF}};

  configured = true;
}

}  // namespace

// The game's device is going away: frame generation's and Reflex's objects on it with it.
void OnDestroyHelperDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  twattila::frame_generation::OnDestroyDevice(native);
  twattila::reflex::OnDestroyDevice(native);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: SHOGUN 2 Complete Edition by ATN";

namespace {

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  ReadDlssAddonOptions();
  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnOpenOverlay);
  reshade::register_event<reshade::addon_event::map_buffer_region>(twshogun2::vfx_effects::OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(twshogun2::vfx_effects::OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(twshogun2::vfx_effects::OnUpdateBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region_command>(
      twshogun2::vfx_effects::OnUpdateBufferRegionCommand);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  twshogun2::vfx_effects::LoadKinds();

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  twshogun2::dlss_jitter::Use(DLL_PROCESS_ATTACH);
  twshogun2::hdr_link::Use(DLL_PROCESS_ATTACH);
  // The DLSS addon's evaluations through the helper's upscaler (its Upscaler row; upscaling.hpp).
  twshogun2::hdr_link::SetUpscale([](const twshogun2::hdr_link::UpscaleRequest& request) {
    return twshogun2::upscaling::Evaluate(request, ray_tracing_installed && twshogun2::ray_tracing::GuidesWritten());
  });
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // Before the Light Bounce's callbacks on the same sun draws: this frame's trace first.
  ray_tracing_installed = ray_tracing_settings.sun_shadows || ray_tracing_settings.ambient_occlusion || ray_tracing_settings.light_bounce
                          || ray_tracing_settings.reflections || ray_tracing_settings.local_lights || ray_tracing_settings.fog_shadows
                          || ray_tracing_settings.specular_occlusion;
  if (ray_tracing_installed) {
    twshogun2::ray_tracing::Install(ray_tracing_settings.casters_out_of_view);
    reshade::register_event<reshade::addon_event::destroy_device>(twshogun2::ray_tracing::OnDestroyDevice);
  }
  // Frame generation (the helper presents; HUD-less images through the swap chain shader, its constants at b13) and
  // Reflex for the game's own presents.
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  twattila::reflex::Install();
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyHelperDevice);
  // After the ray tracing's: the bridge released last.
  reshade::register_event<reshade::addon_event::destroy_device>(twshogun2::upscaling::OnDestroyDevice);
  // One line for a run's log (2026-11-14 data run): the DLSS addon's options this addon reads.
  renodx::utils::log::i(std::format(
      "[Shogun 2] Options: AO mode {}, Light Bounce strength {}, SSR resolution {}, SSS samples {} stability {}, shadow "
      "map stability {}, sun shadow softness {}; ray tracing {} (sun {}, AO {}, GI {}, reflections {}, point lights {}, "
      "paths {})",
      shader_injection.ssao_mode, shader_injection.light_bounce_strength, shader_injection.ssr_resolution,
      shader_injection.sss_samples, shader_injection.sss_stability, shader_injection.shadow_map_stability,
      shader_injection.sun_shadow_softness, (ray_tracing_installed ? "installed" : "not installed"),
      ray_tracing_settings.sun_shadows, ray_tracing_settings.ambient_occlusion, ray_tracing_settings.light_bounce,
      ray_tracing_settings.reflections, ray_tracing_settings.local_lights, ray_tracing_settings.path_tracing));
  // Light Through Foliage's G-buffer constants from the sun light draws: before the Light Bounce's (its own draw skips
  // the later callbacks).
  twshogun2::foliage_light::Install(twshogun2::ray_tracing::SUN_DRAWS);
  reshade::register_event<reshade::addon_event::destroy_device>(twshogun2::foliage_light::OnDestroyDevice);
  // Light Bounce (twattila/light_bounce.hpp): the sun light draws (ray_tracing.hpp's, replaced here).
  twattila::light_bounce::Install(twshogun2::ray_tracing::SUN_DRAWS);

  reshade::register_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::reshade_open_overlay>(OnOpenOverlay);
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(twshogun2::vfx_effects::OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(twshogun2::vfx_effects::OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(twshogun2::vfx_effects::OnUpdateBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(
      twshogun2::vfx_effects::OnUpdateBufferRegionCommand);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  twattila::light_bounce::Uninstall();
  twshogun2::foliage_light::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twshogun2::foliage_light::OnDestroyDevice);
  if (ray_tracing_installed) {
    twshogun2::ray_tracing::Uninstall();
    reshade::unregister_event<reshade::addon_event::destroy_device>(twshogun2::ray_tracing::OnDestroyDevice);
  }
  reshade::unregister_event<reshade::addon_event::destroy_device>(twshogun2::upscaling::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyHelperDevice);
  twattila::reflex::Uninstall();
  twattila::frame_generation::Uninstall();
  twshogun2::hdr_link::SetUpscale(nullptr);

  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);
  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);
  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);
  twshogun2::dlss_jitter::Use(DLL_PROCESS_DETACH);
  twshogun2::hdr_link::Use(DLL_PROCESS_DETACH);

  attached = false;
}

}  // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE) {
  if (!addon_registered) {
    if (!reshade::register_addon(addon_module)) return false;
    addon_registered = true;
  }

  return AttachAddon();
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE) {
  DetachAddon();
  twattila::dlaa_bridge::Shutdown();
  if (addon_registered) {
    reshade::unregister_addon(addon_module);
    addon_registered = false;
  }
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(h_module)) return FALSE;
      addon_registered = true;
      break;

    case DLL_PROCESS_DETACH:
      // ReShade calls AddonUninit before an explicit unload. During process
      // termination, avoid calling into ReShade or RenoDX under the loader lock.
      if (lpv_reserved == nullptr && addon_registered) {
        reshade::unregister_addon(h_module);
        addon_registered = false;
      }
      break;
  }

  return TRUE;
}
