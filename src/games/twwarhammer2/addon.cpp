/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <d3d11.h>
#include <mutex>
#include <shared_mutex>
#include <cmath>
#include <algorithm>
#include <array>
#include <format>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>
// Shadow Casters Out of View: Pharaoh's geometry shaders (its shadow vertex shaders' output signatures are these ones').
#include "../twpharaohdynasties/rt_outside_view_unit_gs.h"
#include "../twpharaohdynasties/rt_outside_view_world_gs.h"

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../talesofarise_dlss/pixel_trace.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/ao_filter.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "../twattila/sun_replace_maps.hpp"
#include "../twattila/test_hooks.hpp"
#include "../twattila/water_effects.hpp"
#include "./more_tests.h"
#include "./rt_outside_view_card_split_gs.h"
#include "./rt_outside_view_world_uv_gs.h"
#include "./rt_particle_quads_gs.h"
#include "../twrome2/engine_tweaks.hpp"
#include "./ao_trace.hpp"
#include "./line_sweep_ao.hpp"
#include "./particle_smoothing.hpp"
#include "./trace_reads.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;


bool IsExtendedMode() {
  return shader_injection.hdr == 1.f;
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f;
}

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}
bool IsPsychoV31Mode() {
  return shader_injection.hdr == 3.f;
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 4.f;
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report, part 5 "WH2's t00": this game's tone mapper at burns 0.1,
// 0.3 and 0.5; fire's hue within 1.3 degrees of the game's SDR picture on average, the row's top).
constexpr float HUE_CLIP_DEFAULT = 100.f;

// bool IsModdedAutoExposureEnabled() {
//   return shader_injection.auto_exposure_highlight_protection == 1.f;
// }

void ApplyResetPreset() {
  // Reset only controls that are currently active in this addon.
  // Peak / Paper White / UI brightness, Settings Mode, and output settings
  // are intentionally preserved.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 1.f},
      {"gamma_correction", 1.f},
      {"video_autohdr", 0.f},
      {"lensflare", 1.f},
      // PsychoV31
      {"v31_exposure", 1.f},
      {"v31_highlights", 1.f},
      {"v31_shadows", 1.f},
      {"v31_contrast", 1.f},
      {"v31_highlight_contrast", 1.f},
      {"v31_shadow_contrast", 1.f},
      {"v31_flare", 0.f},
      {"v31_saturation", 1.f},
      {"v31_dechroma", 0.f},
      {"v31_highlight_saturation", 1.f},
      {"v31_roll_off", 1.f},
      {"v31_gamut_compression", 1.f},
      {"v31_gamut_compression_mode", 1.f},
      {"v31_highlight_hue_shift", 0.05f},
      {"v31_highlight_saturation_shift", 0.f},

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 0.71f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 1.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      // HDR Extended / user grading
      {"exposure_tpm", 1.f},
      {"highlights_tpm", 1.f},
      {"shadows_tpm", 1.f},
      {"contrast_tpm", 1.f},
      {"saturation_tpm", 1.f},

      // Active extra controls
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"sharpening", 1.f},
      {"some_indicators", 1.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

void ApplyPsychoRecommended() {
  // ATN 2026-11-12: every recommended preset in PsychoV35 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},
      {"exposure", 1.f},
      {"highlights", 0.78f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      // Active extra controls
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"sharpening", 1.f},
  });
}

// Frame copy for sharpening //////////////////////////////////////////////////////////
// The game copies the frame into an 8-bit texture (dropped in HDR -> black scene).
// Instead: copy the bound RT into our own texture, bind it at t0 for that draw only.
namespace frame_copy {

struct Data {
  reshade::api::device* device = nullptr;
  reshade::api::resource texture = {0u};
  reshade::api::resource_view srv = {0u};
  reshade::api::resource_desc desc = {};
};

Data data;
std::mutex mutex;
thread_local ID3D11ShaderResourceView* game_srv = nullptr;
thread_local bool swapped = false;

// Caller holds the mutex.
void Destroy() {
  if (data.device != nullptr) {
    if (data.srv.handle != 0u) data.device->destroy_resource_view(data.srv);
    if (data.texture.handle != 0u) data.device->destroy_resource(data.texture);
  }
  data = {};
}

// Copies the frame into our texture ((re)created to match it). Returns its SRV.
reshade::api::resource_view CopyFrame(reshade::api::command_list* cmd_list, reshade::api::resource frame) {
  auto* device = cmd_list->get_device();
  const auto frame_desc = device->get_resource_desc(frame);
  if (frame_desc.type != reshade::api::resource_type::texture_2d || frame_desc.texture.samples != 1) return {0u};

  const std::scoped_lock lock(mutex);
  if (data.device != device
      || data.desc.texture.width != frame_desc.texture.width
      || data.desc.texture.height != frame_desc.texture.height
      || data.desc.texture.depth_or_layers != frame_desc.texture.depth_or_layers
      || data.desc.texture.levels != frame_desc.texture.levels
      || data.desc.texture.format != frame_desc.texture.format) {
    Destroy();
    data.device = device;
    data.desc = reshade::api::resource_desc(
        frame_desc.texture.width,
        frame_desc.texture.height,
        frame_desc.texture.depth_or_layers,
        frame_desc.texture.levels,
        frame_desc.texture.format,
        1,
        reshade::api::memory_heap::gpu_only,
        reshade::api::resource_usage::shader_resource | reshade::api::resource_usage::copy_dest);
    if (!device->create_resource(data.desc, nullptr, reshade::api::resource_usage::shader_resource, &data.texture)) {
      data.texture = {0u};
      return {0u};
    }
    const reshade::api::resource_view_desc view_desc(reshade::api::format_to_default_typed(frame_desc.texture.format));
    if (!device->create_resource_view(data.texture, reshade::api::resource_usage::shader_resource, view_desc, &data.srv)) {
      data.srv = {0u};
      return {0u};
    }
  }
  if (data.srv.handle == 0u) return {0u};

  cmd_list->copy_resource(frame, data.texture);
  return data.srv;
}

bool OnDraw(reshade::api::command_list* cmd_list) {
  swapped = false;
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());

  // The frame as bound right now (back buffer, or RenoDX's 16-bit clone of it).
  ID3D11RenderTargetView* rtv = nullptr;
  context->OMGetRenderTargets(1, &rtv, nullptr);
  if (rtv == nullptr) return true;
  ID3D11Resource* frame = nullptr;
  rtv->GetResource(&frame);
  rtv->Release();
  if (frame == nullptr) return true;

  const auto srv = CopyFrame(cmd_list, {reinterpret_cast<uintptr_t>(frame)});
  frame->Release();
  if (srv.handle == 0u) return true;

  // Swap t0 to our copy for this draw.
  context->PSGetShaderResources(0, 1, &game_srv);
  auto* copy_srv = reinterpret_cast<ID3D11ShaderResourceView*>(srv.handle);
  context->PSSetShaderResources(0, 1, &copy_srv);
  swapped = true;
  return true;
}

// Puts the game's t0 back.
void OnDrawn(reshade::api::command_list* cmd_list) {
  if (!swapped) return;
  swapped = false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  context->PSSetShaderResources(0, 1, &game_srv);
  if (game_srv != nullptr) {
    game_srv->Release();
    game_srv = nullptr;
  }
}

void OnDestroyDevice(reshade::api::device* device) {
  const std::scoped_lock lock(mutex);
  if (data.device == device) Destroy();
}

}  // namespace frame_copy

// Our copy of one of the game's pixel shaders with the rays applied (shaders/*_0x<hash>), drawn in its place only where
// rt_scene's hook for that draw (registered before this one) bound the rays at `SLOT`; otherwise the game's own bytes run.
template <uint32_t HASH, UINT SLOT>
struct RaysShader {
  static constexpr uint32_t SHADER = HASH;
  static inline std::span<const uint8_t> code;
  static inline Microsoft::WRL::ComPtr<ID3D11PixelShader> ours;
  static inline Microsoft::WRL::ComPtr<ID3D11PixelShader> game_shader;

  // BuildRuntimeData: our bytes kept for the swap; the game's shader is left as it is.
  static void Take(renodx::mods::shader::CustomShaders* shaders) {
    code = (*shaders)[HASH].code;
    (*shaders)[HASH] = {.crc32 = HASH, .on_inject = [](auto*) { return false; }};
  }

  static constexpr auto OnDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native == nullptr) return {};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rays;
    native->PSGetShaderResources(SLOT, 1, rays.GetAddressOf());
    if (!rays) return {};
    if (!ours) {
      Microsoft::WRL::ComPtr<ID3D11Device> device;
      native->GetDevice(device.GetAddressOf());
      if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, ours.GetAddressOf()))) return {};
      renodx::utils::log::i(std::format("[Total War] First run: {:08X} drawn with the rays (t{}).", HASH, SLOT));
    }
    native->PSGetShader(game_shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
    native->PSSetShader(ours.Get(), nullptr, 0);
    return {.finalize_callback = [](Context& drawn, const void*) {
      auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
      if (drawn_native != nullptr) drawn_native->PSSetShader(game_shader.Get(), nullptr, 0);
      game_shader.Reset();
    }};
  };

  static void OnDestroyDevice(reshade::api::device*) {
    ours.Reset();
    game_shader.Reset();
  }
};

// Torch and Fire Shadows (twattila/rt_scene.hpp InstallPointLights, rt_local_lights.hlsli): the point lights' shader x each
// light's traced visibility (shaders/pointlight00), the rays' mask at t42. Every trace of the data run drew its point lights
// with this one shader (VS D2A0FF23).
using PointLightRays = RaysShader<0xD96A8DC3, 42>;
// The spot lights the same (shaders/spotlight00-01: spotlight F550D451, gobospotlight 0764179D, VS 5E0BC81E), their surface
// light only (the light shaft stays the game's). Their instances (ATN's 2026-11-14 siege traces, FrameTraceReads: 26 lights
// 22-1000 m, stride 76): POSITION1 the place with POSITION2.x the range after it, POSITION4 the colour (its w the cone's
// exponent); their light stays out of the paths, the fog and the glow (lit all around there).
using SpotLightRays = RaysShader<0xF550D451, 42>;
using GoboSpotLightRays = RaysShader<0x0764179D, 42>;
constexpr twattila::rt_scene::LightInstances SPOT_LIGHTS = {.colour = 4, .position = 1, .shadows_only = true};
// Fog Shadows (InstallFogShadows, rt_fog.hlsli): the fog's in-scattering (shaders/battlefog00, the only one in every trace of
// the data run, at the render size) with the rays' sun visibility in place of its shadow-map march near the camera, t43.
using FogRays = RaysShader<0x492B87C7, 43>;

// Smoke Shadows' particles (Experimental, off): ATN's 2026-11-14 traces (all 26 of the campaign and the siege) draw the lit
// particles with vfx vs50_main_instancing 1FC776EF only, indexed indirect (6 indices an instance, a frame-wide GPU-sorted
// list: 19597 in the siege's), and of its six pixel shaders only ps_main_high_quality F32116B4 blends them over the light
// buffer (ONE / INV_SRC_ALPHA, at half size); the others are decals (61AFC16D, DE44A861), the particles' lights (51B21D74),
// overlay (73514DBC) and distortion (F0998601). The one-vertex C3300B96 drew in none. 1FC776EF's output signature and its
// writes to it are Pharaoh's 109B79CE's (fxc -dumpbin): Pharaoh's quad shader at F32116B4's slots
// (rt_particle_quads.gs_5_0.hlsl). Its lower-effects twins DDABB799 / 687378DB (t4, s3, t7: one slot below) drew in no
// trace: left out.
constexpr std::array<twattila::rt_scene::SmokeVertexShader, 1> SMOKE_VERTEX_SHADERS = {{
    {.hash = 0x1FC776EF, .quad_shader = __rt_particle_quads, .indirect = true},
}};
constexpr std::array<twattila::rt_scene::SmokePixelShader, 1> SMOKE_PIXEL_SHADERS = {{{.hash = 0xF32116B4}}};

// Rays Replace Shadow Maps (Experimental, off; Pharaoh's way): the six sun shaders whose shadow the screen-space shadows
// patched (the data run's log: hardware gather 5.0, standard and skin, single-sampled and MSAA) read the sun's atlas
// (g_tShadowBuffer_SM50 t1, 12288 x 12288 R32) only through s1, 28 gather4_c each, s1's only reads; there is no baked map.
// Where the rays' shadow is bound at t49 an ALWAYS sampler at s1 makes every comparison lit, so their patched comparisons
// (each x the rays' visibility) give the rays' shadow alone; the cloud shadows stay the game's. The game's sampler is bound
// again after the draw (finalize: the Light Bounce draws the sun draw itself and bypasses; twattila/sun_replace_maps.hpp at s1).
// Skip the Shadow Maps (under it): rt_scene's Ray-Traced Shadows Only skips the atlas's draws (the atlas is the sun draw's square
// R32 map).
constexpr std::array<uint32_t, 6> ATLAS_SUN_SHADERS = {0x1AB80044, 0x37237EC7, 0x060623DD, 0xD7990748, 0xCEEE8017, 0xDF071F9D};
constexpr UINT ATLAS_SAMPLER_SLOT = 1;
float rays_replace_maps_row = 0.f;
float rays_skip_cascades_row = 0.f;
// The shadow pass's casters in the rays' scene (SCENE_SHADERS' cascade_keep entries): Shadow Casters Out of View, and Hills Out of
// View for its terrain (Experimental, off: no game run has shown them yet).
float rays_units_out_of_view_row = 0.f;
float rays_hills_out_of_view_row = 0.f;

#define FrameCopyShader(value)              \
  {                                         \
      value,                                \
      {                                     \
          .crc32 = value,                   \
          .code = __##value,                \
          .on_draw = &frame_copy::OnDraw,   \
          .on_drawn = &frame_copy::OnDrawn, \
      },                                    \
  }

#define UpgradeRTVReplaceShader(value)                                                                \
  {                                                                                                   \
      value,                                                                                          \
      {                                                                                               \
          .crc32 = value,                                                                             \
          .code = __##value,                                                                          \
          .on_draw = [](auto* cmd_list) {                                                             \
            auto rtvs = renodx::utils::swapchain::GetRenderTargets(cmd_list);                         \
            bool changed = false;                                                                     \
            for (auto rtv : rtvs) {                                                                   \
              changed = renodx::mods::swapchain::ActivateCloneHotSwap(cmd_list->get_device(), rtv);   \
            }                                                                                         \
            if (changed) {                                                                            \
              renodx::mods::swapchain::FlushDescriptors(cmd_list);                                    \
              renodx::mods::swapchain::RewriteRenderTargets(cmd_list, rtvs.size(), rtvs.data(), {0}); \
            }                                                                                         \
            return true; },                                                                           \
          .on_drawn = [](auto* cmd_list) {                                                            \
            twattila::screen_space_shadows::OnTonemapDrawn(cmd_list);                                 \
            twattila::anti_aliasing::OnTonemapDrawn(cmd_list);                                        \
            twattila::rt_scene::OnTonemapDrawn(cmd_list); },                                          \
      },                                                                                              \
  }

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      FrameCopyShader(0xF9646737), //sharpening (reads our 16-bit frame copy)
      UpgradeRTVReplaceShader(0x2333E142), //t00
      UpgradeRTVReplaceShader(0x187C9C6D), //t01
      UpgradeRTVReplaceShader(0x4B738F1E), //t02
      UpgradeRTVReplaceShader(0xF4B4EBD5), //t03-t08: no distortion, MSAA
      UpgradeRTVReplaceShader(0x76357B37),
      UpgradeRTVReplaceShader(0x6122F5DA),
      UpgradeRTVReplaceShader(0x0B975FC2),
      UpgradeRTVReplaceShader(0xACA23A57),
      UpgradeRTVReplaceShader(0x100DB7F6),
      UpgradeRTVReplaceShader(0xCE9EA4E8), //fxaa
      // UpgradeRTVReplaceShader(EMPTY),
      __ALL_CUSTOM_SHADERS,
  };
  // Ambient Occlusion: Visibility Bitmask in place of the AO's collect (its blur does not run at every setting, ATN's
  // AO trace: then the lighting reads the collect's output): the addon's own shader (ao_bitmask_collect), from the depth
  // downsample's input (this frame's depth) and the sweep's camera (b0, still bound at the collect); the game's
  // collect stays its own code (Game's costs what it did); the game's blur, when it runs, blurs it as the game's.
  custom_shaders[0xF1CE6943].on_draw = [](auto* cmd_list) {
    tw::ao_trace::OnPass(cmd_list, 0xF1CE6943, true);
    if (shader_injection.ssao_mode >= 1.f) tw::line_sweep_ao::KeepComputeDepth(cmd_list, 0);
    return true;
  };
  custom_shaders[0x19CF3C83].on_draw = [](auto* cmd_list) {
    tw::ao_trace::OnPass(cmd_list, 0x19CF3C83, true);
    if (shader_injection.ssao_mode < 1.f) return true;
    tw::line_sweep_ao::KeepComputeCamera(cmd_list, 0);
    return !tw::line_sweep_ao::DispatchInstead(cmd_list, __ao_bitmask_collect, &shader_injection, sizeof(shader_injection),
                                               true, __ao_upsample_collect, shader_injection.ssao_resolution != 0.f);
  };

  // Reflections: the ray-traced ones in the resolve (ssr00, twattila/rt_scene.hpp, rt_reflection.hlsli); while they
  // replace the screen-space march, the march (raycast) and its blurs are left out.
  custom_shaders[0x7E1CDD0A].on_draw = [](auto* cmd_list) {
    twattila::rt_scene::BindReflections(cmd_list, true, true);
    return true;
  };
  custom_shaders[0x7E1CDD0A].on_drawn = [](auto* cmd_list) { twattila::rt_scene::BindReflections(cmd_list, true, false); };
  for (const uint32_t hash : {0x143BD25Cu, 0x60AF6F79u, 0xBDBC783Bu}) {
    custom_shaders[hash].on_draw = twattila::rt_scene::OnReflectionMarch;
  }
  // The point and spot lights' and the fog's shaders: ours only while the rays are bound (RaysShader above).
  PointLightRays::Take(&custom_shaders);
  SpotLightRays::Take(&custom_shaders);
  GoboSpotLightRays::Take(&custom_shaders);
  FogRays::Take(&custom_shaders);

  // Settings ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // PsychoV31 sliders: 0 - max, 1 = its own.
  auto v31_slider = [](const std::string& key, float* binding, const std::string& label, const std::string& tooltip,
                       float max = 2.f, float default_value = 1.f) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = label,
        .section = "PsychoV31",
        .tooltip = tooltip,
        .max = max,
        .format = "%.2f",
        .is_enabled = IsPsychoV31Mode,
        .is_visible = IsPsychoV31Mode,
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
        .labels = {"HDR & Effects", "Graphics 1 of 2", "Graphics 2 of 2"},
        .is_global = true,
    },
    twattila::graphics_page::Note(twattila::graphics_page::FULL_DOWNLOAD_NOTE, "General", twattila::graphics_page::WARNING_TINT,
                                  [] { return !twattila::dlaa_bridge::HelperInstalled(); }),
    // Gamma Correction //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BULLET,
    //     .label = "Correct Windows' washed out gamma decode.",
    //     .section = "Gamma Correction",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "gamma_correction",
    //     .binding = &shader_injection.gamma_correction,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "Enable Correction",
    //     .section = "Gamma Correction",
    //     .tooltip = "Emulate Gamma 2.2 EOTF.\n(Attached to Scene Paper White.)",
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "(Google Slides) Further Explanation & Test",
    //     .section = "Gamma Correction",
    //     .group = "button-line-1",
    //     .on_change = []() {
    //       renodx::utils::platform::LaunchURL("https://docs.google.com/presentation/d/e/2PACX-1vSXeLHlbm6repcS7fels1-SXYGRmzziRrnuJ8nDO8J5rsWV3dT1-nVyCKp0Tj_stwx-9qlCI-N6rYIT/pub?start=false&loop=false&slide=id.g3e007eafba8_0_0");
    //     },
    // },
    // Peak //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "peak_test",
    //     .binding = &shader_injection.peak_test,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 0.f,
    //     .label = "Calibration",
    //     .section = "Peak",
    //     .tooltip = "3 rectangles within a large one.\n\n- Left: Not Visible.\n- Middle: Barely Visible.\n- Right: Easily Visible.\n\n(Ofc, you can always set lower to personal pref.)",
    // },
    
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "Use these in-game settings: Gamma 22 and Brightness 10.",
        .section = "General",
        .tint = 0xFFD23C,
    },
    new renodx::utils::settings::Setting{
        .key = "peak_white_nits",
        .binding = &shader_injection.peak_white_nits,
        .default_value = 400.f,
        .can_reset = false,
        .label = "Peak",
        .section = "General",
        .tooltip = "Maximum brightness output in nits.",
        .min = 400.f,
        .max = 10000.f,
        // .is_enabled = IsHDRMode,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    // new renodx::utils::settings::Setting{
    //     .key = "whiteclip",
    //     .binding = &shader_injection.whiteclip,
    //     .default_value = 1.f,
    //     .label = "Clip",
    //     .section = "Peak",
    //     .tooltip = "Raise to increase clipping in highlights.",
    //     .min = 0.1f,
    //     .max = 2.f,
    //     .format = "%.2f",
    // },
    // Paper White //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .key = "diffuse_white_nits",
        .binding = &shader_injection.diffuse_white_nits,
        .default_value = 203.f,
        .label = "Paper White",
        .section = "General",
        .tooltip = "Brightness of the scene/game.",
        .min = 1.f,
        .max = 500.f,
        // .is_enabled = IsHDRMode,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "graphics_white_nits",
        .binding = &shader_injection.graphics_white_nits,
        .default_value = 203.f,
        .label = "UI",
        .section = "General",
        .tooltip = "Brightness of UI and HUD in nits.",
        .min = 1.f,
        .max = 500.f,
        // .is_enabled = IsHDRMode,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "tonemapper",
        .binding = &shader_injection.hdr,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Rendering Mode",
        .section = "General",
        .tooltip = "SDR: the game's SDR tone mapper.\n"
                   "HDR (Extended): the game's Reinhard extended into HDR.\n"
                   "HDR (PsychoV30): PsychoV30 as tone mapper.\n"
                   "HDR (PsychoV31): PsychoV31 as tone mapper, more grading sliders.\n"
                   "HDR (PsychoV35): Musa's PsychoV35 as tone mapper, with Hue Clip Emulation (the Recommended presets).",
        .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV30)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
    },
    new renodx::utils::settings::Setting{
        .key = "gamma_correction",
        .binding = &shader_injection.gamma_correction,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Gamma Correction",
        .section = "General",
        .tooltip = "Rendering Mode SDR: any in-game Gamma shown as gamma 2.2.\n"
                   "Off: the game's own picture, at the in-game Gamma.",
        .labels = {"Off (In-game Gamma value)", "On (Gamma 2.2)"},
        .is_visible = [] { return !IsHDRMode(); },
    },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::TEXT,
    //     .label =
    //         "IMPORTANT: Disable Sharpening in the game's graphics settings.\n"
    //         "Both effects can make the game scene render black.",
    //     .section = "Important",
    // },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Resets active image controls to their defaults.\n"
                   "Peak, Paper White, UI brightness, Settings Mode, and output settings are preserved.",
        .on_change = []() {
          ApplyResetPreset();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended",
        .section = "Presets",
        .group = "preset-line-2",
        .tooltip = "HDR (PsychoV35) and Video AutoHDR 0.20, the rest at its default.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
          ApplyPsychoRecommended();
        },
    },
    // Extra //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "dof",
    //     .binding = &shader_injection.dof,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "Depth of Field",
    //     .section = "Extra",
    //     .tooltip = "Toggle depth of field effect.",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "uvdistort",
    //     .binding = &shader_injection.uvdistort,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "Lens Distortion",
    //     .section = "Extra",
    //     .tooltip = "Toggle lens distortion on color.",
    // },

    // new renodx::utils::settings::Setting{
    //     .key = "filmgrain",
    //     .binding = &shader_injection.filmgrain,
    //     .default_value = 1.f,
    //     .label = "Film Grain",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on film grain effect.",
    //     .min = 0.f,
    //     .max = 1.f,
    //     .format = "%.2f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "lut",
    //     .binding = &shader_injection.lut,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "LUT (Debug)",
    //     .section = "Extra",
    //     .tooltip = "Toggle color grading via LUT.",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "ui",
    //     .binding = &shader_injection.ui,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "UI (Debug)",
    //     .section = "Extra",
    //     .tooltip = "Toggle UI elements, enough for screenshot in pause menu.",
    // },

    // FPS Limit //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "fps_limit",
    //     .binding = &renodx::utils::swapchain::fps_limit,
    //     .default_value = 0.f,
    //     .can_reset = true,
    //     .label = "Value",
    //     .section = "FPS Limit",
    //     .min = 0.f,
    //     .max = 240.f,
    // },
      new renodx::utils::settings::Setting{
        .key = "exposure",
        .binding = &shader_injection.exposure,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Exposure",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "highlights",
        .binding = &shader_injection.highlights,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 0.71f,
        .label = "Highlights",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "shadows",
        .binding = &shader_injection.shadows,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Shadows",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "contrast",
        .binding = &shader_injection.contrast,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Contrast",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    // new renodx::utils::settings::Setting{
    //     .key = "flare",
    //     .binding = &shader_injection.flare,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.0f,
    //     .label = "Flare",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 2.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },
    new renodx::utils::settings::Setting{
        .key = "purity_scale",
        .binding = &shader_injection.saturation,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Saturation / Purity",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "cone_response_exponent",
        .binding = &shader_injection.cone_response_exponent,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Cone Response Exponent",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "hue_shift",
        .binding = &shader_injection.hue_shift,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Highlight Hue Shift",
        .section = "Psycho V30",
        .tooltip = "Hue shift of bright highlights, like SDR: fire and explosions turn orange/yellow instead of pink/red.\n0 = none, 1 = PsychoV30 default (like my old addons), 2 = full.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    // new renodx::utils::settings::Setting{
    //     .key = "current_adaptive_state_bt709",
    //     .binding = &shader_injection.current_adaptive_state_bt709,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.18f,
    //     .label = "current_adaptive_state_bt709",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 1.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },

    // new renodx::utils::settings::Setting{
    //     .key = "current_background_state_bt709",
    //     .binding = &shader_injection.current_background_state_bt709,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.18f,
    //     .label = "current_background_state_bt709",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 1.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },


    //TPM:
    new renodx::utils::settings::Setting{
        .key = "exposure_tpm",
        .binding = &shader_injection.exposure_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Exposure",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "highlights_tpm",
        .binding = &shader_injection.highlights_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Highlights",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "shadows_tpm",
        .binding = &shader_injection.shadows_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Shadows",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "contrast_tpm",
        .binding = &shader_injection.contrast_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Contrast",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "saturation_tpm",
        .binding = &shader_injection.saturation_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Saturation / Purity",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },


    ///EXTRA-----////

    // new renodx::utils::settings::Setting{
    //     .key = "bloom",
    //     .binding = &shader_injection.bloom,
    //     .default_value = 0.5f,
    //     .label = "Bloom",
    //     .section = "Extra",
    //     .tooltip = "Bloom multiplier.",
    //     .min = 0.00f,
    //     .max = 0.75f,
    //     .format = "%.2f",
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "godrays",
    //     .binding = &shader_injection.godrays,
    //     .default_value = 1.f,
    //     .label = "Godrays",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on godrays.  Needs in-game Sun Rays setting on.",
    //     .min = 0.f,
    //     .max = 2.f,
    //     .format = "%.2f",
    //     .is_enabled = IsHDRMode,  
    //     .is_visible = IsAdvancedSettings,
    // },
    // PsychoV31 ///////////////////////////////////////////////////////////////////////////////////
    v31_slider("v31_exposure", &shader_injection.v31_exposure, "Exposure", ""),
    v31_slider("v31_highlights", &shader_injection.v31_highlights, "Highlights", ""),
    v31_slider("v31_shadows", &shader_injection.v31_shadows, "Shadows", ""),
    v31_slider("v31_contrast", &shader_injection.v31_contrast, "Contrast", "1 = PsychoV31's own."),
    v31_slider("v31_highlight_contrast", &shader_injection.v31_highlight_contrast, "Highlight Contrast",
               "Contrast above mid gray. 1 = PsychoV31's own."),
    v31_slider("v31_shadow_contrast", &shader_injection.v31_shadow_contrast, "Shadow Contrast",
               "Contrast below mid gray. 1 = PsychoV31's own."),
    new renodx::utils::settings::Setting{
        .key = "v31_flare",
        .binding = &shader_injection.v31_flare,
        .default_value = 0.f,
        .label = "Flare",
        .section = "PsychoV31",
        .tooltip = "Deepens the darkest shadows. 0 = off.",
        .max = 1.f,
        .format = "%.2f",
        .is_enabled = IsPsychoV31Mode,
        .parse = [](float value) { return std::pow(value, 5.f); },
        .is_visible = IsPsychoV31Mode,
    },
    v31_slider("v31_saturation", &shader_injection.v31_saturation, "Saturation / Purity", "1 = PsychoV31's own."),
    v31_slider("v31_dechroma", &shader_injection.v31_dechroma, "Dechroma",
               "Very bright colors fade toward white, as if overexposed. 0 = off.", 1.f, 0.f),
    v31_slider("v31_roll_off", &shader_injection.v31_roll_off, "Highlight Roll-off",
               "How soon highlights start to compress toward Peak. 0 = latest, 1 = default, 2 = soonest."),
    v31_slider("v31_gamut_compression", &shader_injection.v31_gamut_compression, "Gamut Compression",
               "Pulls colors your display can't show back inside its color space.\n"
               "0 = off (the display clips them), 1 = full (default).",
               1.f),
    new renodx::utils::settings::Setting{
        .key = "v31_gamut_compression_mode",
        .binding = &shader_injection.v31_gamut_compression_mode,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Gamut Compression Mode",
        .section = "PsychoV31",
        .tooltip = "The color space colors are fitted into.\n"
                   "BT.2020: the whole HDR signal (default).\n"
                   "Display P3: what most HDR monitors and TVs show. BT.709: SDR colors.",
        .labels = {"BT.709", "BT.2020", "Display P3"},
        .is_enabled = IsPsychoV31Mode,
        // PsychoV31's modes: 0 BT.709, 1 BT.2020, 3 Display P3.
        .parse = [](float value) { return (value == 2.f ? 3.f : value); },
        .is_visible = IsPsychoV31Mode,
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "Fire too pink, too orange or too pale? Change the settings below.",
        .section = "PsychoV31",
        .is_visible = IsPsychoV31Mode,
    },
    v31_slider("v31_highlight_saturation", &shader_injection.v31_highlight_saturation, "Highlight Saturation",
               "Saturation of bright colors. 0 = less, 1 = PsychoV31's own, 2 = more."),
    v31_slider("v31_highlight_hue_shift", &shader_injection.v31_highlight_hue_shift, "Highlight Hue Shift",
               "Bright colors take the SDR image's hues: fire more orange / yellow, less pink.\n"
               "0 = off, 1 = SDR hues. Default 0.05.",
               1.f, 0.05f),
    v31_slider("v31_highlight_saturation_shift", &shader_injection.v31_highlight_saturation_shift,
               "Highlight Saturation Shift",
               "Gives bright colors the saturation of the SDR image (fire cores less pale and pink).\n"
               "0 = off (default), 1 = SDR's.",
               1.f, 0.f),

    new renodx::utils::settings::Setting{
        .key = "bloom",
        .binding = &shader_injection.bloom,
        .default_value = 1.f,
        .label = "Bloom",
        .section = "Extra",
        .tooltip = "The glow around bright lights. 0 = off, 1 = the game's.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "lensflare",
        .binding = &shader_injection.lensflare,
        .default_value = 1.f,
        .label = "Lens Flare",
        .section = "Extra",
        .tooltip = "Sun lens flares. 0 = off.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "vignette",
        .binding = &shader_injection.vignette,
        .default_value = 1.f,
        .label = "Vignette",
        .section = "Extra",
        .tooltip = "Multiplier on vignette mask. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "distortion",
        .binding = &shader_injection.uvdistort,
        .default_value = 1.f,
        .label = "Distortion",
        .section = "Extra",
        .tooltip = "Multiplier on Distortion. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "sharpening",
        .binding = &shader_injection.sharpening,
        .default_value = 1.f,
        .label = "Sharpening",
        .section = "Extra",
        .tooltip = "Multiplier on Sharpening. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "video_autohdr",
        .binding = &shader_injection.video_autohdr,
        .default_value = 0.f,
        .label = "Video AutoHDR",
        .section = "Extra",
        .tooltip = "Makes the bright parts of the game's (SDR) videos brighter: only what is above mid gray,\n"
                   "the brightest the most. 0 = off, 1 = video white reaches Peak.\n"
                   "High values can show banding (see Video Dithering).",
        .min = 0.f,
        .max = 1.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "video_dithering",
        .binding = &shader_injection.video_dithering,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Video Dithering",
        .section = "Extra",
        .tooltip = "Fine noise that hides banding in the brightened videos.\n"
                   "Only with Video AutoHDR above 0.",
        .labels = {"Off", "On"},
        .is_enabled = [] { return IsHDRMode() && shader_injection.video_autohdr > 0.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "sky_deband",
        .binding = &shader_injection.sky_deband,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Sky Debanding",
        .section = "Extra",
        .tooltip = "Smooths the sky's banding and adds fine dither.\n"
                   "Higher: removes wider bands, softens sky details more.",
        .labels = {"Off", "Low", "Medium", "High"},
    },
    new renodx::utils::settings::Setting{
        .key = "some_indicators",
        .binding = &shader_injection.someindicators,
        .default_value = 1.f,
        .label = "Unit/Building Indicator Brightness (Experimental)",
        .section = "Extra",
        .tooltip = "Brightness of the indicators under units and buildings, fire arcs and deployment lines.\n"
                   "Lower helps at night. 1 = the game's.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "VfxParticleSmoothing",
        .binding = &twwarhammer2::particle_smoothing::row,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = "Smooth Half-Size Particles (Experimental)",
        .section = "Extra",
        .tooltip = "Smoke, fire and dust the game draws at half size flicker less.\n"
                   "Small cost.",
        .labels = {"Off", "On"},
    },
    // new renodx::utils::settings::Setting{
    //     .key = "filmgrain",
    //     .binding = &shader_injection.filmgrain,
    //     .default_value = 1.f,
    //     .label = "Filmgrain",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on filmgrain. Needs in-game setting on.",
    //     .min = 0.f,
    //     .max = 2.f,
    //     .format = "%.2f",
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "some_indicators",
    //     .binding = &shader_injection.someindicators,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.4f,
    //     .label = "Brightness of some Unit/Building Indicator ",
    //     .section = "Extra",
    //     .tooltip = "Brightness of selected highlight indicators, particularly useful at night.",
    //     .min = 0.0f,
    //     .max = 1.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    


    // Auto Exposure //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "auto_exposure_highlight_protection",
    //     .binding = &shader_injection.auto_exposure_highlight_protection,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 0.0f,
    //     .label = "Modded Auto Exposure",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Reduces the exposure influence of isolated highlights such as the sun, fire, and sparks without reducing their rendered brightness. Off is stock.",
    //     .labels = {"Off (Stock)", "On (Protected)"},
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "auto_exposure_highlight_headroom_stops",
    //     .binding = &shader_injection.auto_exposure_highlight_headroom_stops,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Modded Auto Exposure: Sensitivity",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Maximum metered highlight above the frame average, in stops. Lower values protect exposure more strongly. Higher values are closer to stock.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    //     .is_enabled = IsModdedAutoExposureEnabled,
    //     .is_visible = IsAdvancedSettings,
    // },
    // VFX //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fire_brightness",
    //     .binding = &shader_injection.vfxfirebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: fire/smoke/unit's dust",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Fire, embers, sparks, smoke, and .... Recommmendation is max 10 with modded auto exposure.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.0f",
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_blood_splash_brightness",
    //     .binding = &shader_injection.vfxbloodsplash,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Blood splash",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Bloodsplash brightness. Recommmendation is max 10.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.0f",
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fog_brightness",
    //     .binding = &shader_injection.vfxfogbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Storms (Sand/Fog)",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of the fog color applied to particles. Recommmendation is max 1.",
    //     .min = 0.1f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    //   new renodx::utils::settings::Setting{
    //     .key = "vfxsnow",
    //     .binding = &shader_injection.vfxsnow,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Snowflakes",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of snow. Recommmendation is max 5.",
    //     .min = 0.1f,
    //     .max = 20.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxrain",
    //     .binding = &shader_injection.vfxrain,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: rain",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of rain. Recommmendation is max 5.",
    //     .min = 0.1f,
    //     .max = 20.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    /////////////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_base_brightness",
    //     .binding = &shader_injection.vfxbasebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Flame, Dust, Clouds & Smoke",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Brightness of smoke, clouds, dust, and flame. This does not boost the inner fire color.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_normal_strength",
    //     .binding = &shader_injection.vfxnormalstrength,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Normal Strength",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Particle normal-map strength; 0 is flat.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_shadow_strength",
    //     .binding = &shader_injection.vfxshadowstrength,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Shadow Strength",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Received particle shadows; 0 disables them.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_lighting_strength",
    //     .binding = &shader_injection.vfxlightingstrength,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Lighting Strength",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Blends between unlit and stock-lit particles.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_specular_brightness",
    //     .binding = &shader_injection.vfxspecularbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Specular Brightness",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Direct sun specular highlight.",
    //     .min = 0.0f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_reflection_brightness",
    //     .binding = &shader_injection.vfxreflectionbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Reflection Brightness",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Environment-cubemap reflection.",
    //     .min = 0.0f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_soft_particle_strength",
    //     .binding = &shader_injection.vfxsoftparticlestrength,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Soft Particle Strength",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Depth-intersection fading; 0 disables it.",
    //     .min = 0.0f,
    //     .max = 1.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_opacity",
    //     .binding = &shader_injection.vfxopacity,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Opacity",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Opacity.",
    //     .min = 0.0f,
    //     .max = 1.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_overall_brightness",
    //     .binding = &shader_injection.vfxoverallbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Overall VFX Brightness",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Overall VFX Brightness.",
    //     .min = 0.0f,
    //     .max = 10.0f,
    //     .format = "%.1f",
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxweather",
    //     .binding = &shader_injection.vfxweather,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Blood Splash & desert storm & Fog",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Brightness of blood splashes and the camera-following fog used by snowstorms and sandstorms. Recommmendation is max 0.5 in storm/fog.",
    //     .min = 0.01f,
    //     .max = 20.0f,
    //     .format = "%.2f",
    //     .is_visible = IsAdvancedSettings,
    // },

    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fog_amount",
    //     .binding = &shader_injection.vfxfogamount,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Fog Amount",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Amount of atmospheric fog applied to particles.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxdebugmode",
    //     .binding = &shader_injection.vfxdebugmode,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.f,
    //     .label = "Some highlight indicators Brightness",
    //     .section = "Game specific settings",
    //     .tooltip = "Some highligh indicators. For nights so you don't blind yourself",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.0f",
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxdebugslice",
    //     .binding = &shader_injection.vfxdebugslice,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.0f,
    //     .label = "Some highlight indicators Brightness",
    //     .section = "Game specific settings",
    //     .tooltip = "Some highligh indicators. For nights so you don't blind yourself",
    //     .min = 0.0f,
    //     .max = 511.0f,
    //     .format = "%.0f",
    // }
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "HDR Defaults",
    //     .group = "preset-line-1",
    //     .tooltip = "Resets all image tuning to HDR defaults while preserving Peak, Paper White, UI brightness, Settings Mode, and output encoding.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.00f, 1.f, 0.f, 2.f, 1.f, 1.f, 1.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: All Conditions",
    //     .group = "preset-line-1",
    //     .tooltip = "Balanced recommendation intended to work across all weather and lighting conditions. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 4.f, 2.5f, 0.5f, 3.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: Day/Dry/Rain",
    //     .group = "preset-line-2",
    //     .tooltip = "Recommendation for daytime scenes and dry weather. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 4.f, 5.f, 2.f, 5.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: Night/Snow/Fog",
    //     .group = "preset-line-2",
    //     .tooltip = "Recommendation for fog, snow, and nighttime scenes. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 3.f, 2.f, 0.5f, 2.f);
    //     },
    // },
  };
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));
}

// Graphics (Experimental): engine values x the sliders (their tweaks are filled in ConfigureAddon).
tw::engine_tweaks::Option unit_detail[5];  // very small, small, medium, large, very large units
tw::engine_tweaks::Option unit_animation;
tw::engine_tweaks::Option unit_variety;
tw::engine_tweaks::Option footstep_effects;
tw::engine_tweaks::Option model_detail;
tw::engine_tweaks::Option terrain_detail;
tw::engine_tweaks::Option bush_detail;
tw::engine_tweaks::Option campaign_draw;
tw::engine_tweaks::Option shadow_detail;
tw::engine_tweaks::Option small_shadows;
// Shadow Map Size: the highest Shadows setting's 8192 x 8192 (written into the game's code) x 1 or 1.5.
tw::engine_tweaks::Option shadow_map;
// Every engine option above, then the tests' (Graphics 2 of 2: more_tests.h, twattila/engine_tests.hpp).
std::vector<tw::engine_tweaks::Option*> graphics_options;
std::vector<twattila::engine_tests::EngineTest> engine_tests;
std::atomic<bool> at_start_done = false;
std::atomic<bool> at_start_stop = false;
HMODULE addon_module_handle = nullptr;
twattila::feature_settings::SssSliders sss_sliders;
twattila::feature_settings::AaSliders aa_sliders;
// Ray tracing's (twattila/rt_scene.hpp).
twattila::rt_scene::Sliders rt_sliders;
// Texture LOD Bias on All Textures (Graphics 1 of 2, Experimental): the trilinear samplers too
// (texture_lod_bias.hpp SetWrappingTrilinear).
float lod_bias_trilinear_row = 0.f;

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
bool graphics_found = false;
// The graphics watch's values (4 hardware breakpoints).
constexpr std::array<std::string_view, 4> WATCHED_VALUES = {"scrub_lod_distance_default", "campaign_culling_ratio", "shadow_lod_distance_scaler", "cull_small_shadow_start_size"};


// The pixel trace's position, % of the screen's width and height.
std::array<float, 2> traced_pixel = {50.f, 50.f};

bool configured = false;
bool attached = false;
bool addon_registered = false;

}  // namespace

// Lifecycle /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  auto peak = renodx::utils::swapchain::GetPeakNits(swapchain);
  if (!peak.has_value()) {
    peak = 1000.f;
  }

  // find and set
  for (auto& setting : settings) {
    if (setting->binding != &shader_injection.peak_white_nits) continue;
    setting->default_value = peak.value();
    setting->max = std::max(peak.value(), setting->max);
    setting->can_reset = true;
    break;
  }

  // settings[3]->default_value = renodx::utils::swapchain::ComputeReferenceWhite(peak.value());
}

// The shadow map size, before the game builds its shadows.
void OnInitDevice(reshade::api::device*) {
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
}

// DLAA / DLSS object motion (twattila/anti_aliasing.hpp, as Attila's): the game's shaders whose things move on
// their own and their motion versions (tools\tw\attila_motion\port_motion.py, GPU-checked by test_motion.py).
using twattila::anti_aliasing::MotionKind;
constexpr std::array<twattila::anti_aliasing::MotionShader, 24> MOTION_SHADERS = {{
    {.hash = 0x048B32D8, .motion_shader = __motion_ocean_deep_ocean2_048b, .kind = MotionKind::HISTORY, .history_textures = 0b1},
    {.hash = 0x0959BF4F, .motion_shader = __motion_ocean_coast_ocean2_0959, .kind = MotionKind::HISTORY, .history_textures = 0b10101},
    {.hash = 0x16AFAF14, .motion_shader = __motion_rigid_rigidstandard_16af, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidstandard_16af},
    {.hash = 0x3FCE3B26, .motion_shader = __motion_unit_weighted2_liquid_xml_3fce, .kind = MotionKind::UNIT},
    {.hash = 0x407C1E30, .motion_shader = __motion_unit_weighted_407c, .kind = MotionKind::UNIT},
    {.hash = 0x41147CD3, .motion_shader = __motion_tree_campaignvegetation_4114},
    {.hash = 0x56382151, .motion_shader = __motion_oldocean_ocean_5638},
    {.hash = 0x59D73E4C, .motion_shader = __motion_unit_weighted4_animated_overl_59d7, .kind = MotionKind::UNIT},
    {.hash = 0x6609B69E, .motion_shader = __motion_unit_weighted_6609, .kind = MotionKind::UNIT},
    {.hash = 0x6BEF6138, .motion_shader = __motion_unit_weightedwithburn_6bef, .kind = MotionKind::UNIT},
    {.hash = 0x71EB7463, .motion_shader = __motion_grass_grass_71eb},
    {.hash = 0x772F13E0, .motion_shader = __motion_unit_weighted4_animated_overl_772f, .kind = MotionKind::UNIT},
    {.hash = 0x7FDEE7B4, .motion_shader = __motion_branch_branch_v3_7fde},
    {.hash = 0x9ECDC023, .motion_shader = __motion_rope_rope_9ecd, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope_rope_9ecd},
    {.hash = 0xB228811C, .motion_shader = __motion_unit_weighted2_emissive_wind_b228, .kind = MotionKind::UNIT},
    {.hash = 0xB7CE3413, .motion_shader = __motion_rigid_rigidtextureblendwind_b7ce, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblendwind_b7ce},
    {.hash = 0xBB80BC52, .motion_shader = __motion_sail_sailsymbol_bb80, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sailsymbol_bb80},
    {.hash = 0xC251DF91, .motion_shader = __motion_unit_weighted4_emissive_wind_c251, .kind = MotionKind::UNIT},
    {.hash = 0xC5CD75C5, .motion_shader = __motion_rigid_rigidtextureblend_c5cd, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblend_c5cd},
    {.hash = 0xCD0E0357, .motion_shader = __motion_leaf_leaf_v3_cd0e},
    {.hash = 0xEBA791E9, .motion_shader = __motion_ocean_shallow_ocean2_eba7, .kind = MotionKind::HISTORY, .history_textures = 0b101},
    {.hash = 0xEBF2233E, .motion_shader = __motion_unit_weighted4_wind_alpha_xml_ebf2, .kind = MotionKind::UNIT},
    {.hash = 0xF43EAAD7, .motion_shader = __motion_unit_weighted_f43e, .kind = MotionKind::UNIT},
    {.hash = 0xFF9FC42C, .motion_shader = __motion_sail_sail_ff9f, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sail_ff9f},
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl's sun light draws, the standard model's camera at b1, the skin's at b2,
// the G-buffer's channels 1-4 from their first slot.
// Ray tracing's scene (twattila/rt_scene.hpp; tools\tw\rt_port.py): the main view's terrain, rigid objects, tree
// branches and leaves and units, and their capture versions (world positions; leaves also UVs).
// The floating unit banners (rigidtextureblend, 2 bones, 7DF335FF) stay out: the game gives them no shadow (no atlas draw in
// any trace of the data run).
// 16AFAF14's alpha-tested materials (sorted): each discards where its diffuse map's alpha (x the instance's fade, v6.w) is
// below 0.5 at TEXCOORD1.xy (the vertex's TEXCOORD0, the VS's o2), the helper's cut-out; captured with that UV
// (rt_capture_rigid_standard_cut_out). Drawn in ATN's traces and the data run's (tools/tw/wh2/runs): fences, palms and props
// in battle; every campaign object through the shroud's material (45 diffuse maps, most solid).
constexpr std::array<twattila::rt_scene::PixelMask, 5> CUT_OUT_MASKS = {{
    {.pixel_shader = 0x2BE667BC, .slot = 1},  // rigidstandard ps40_gbuffer_shroud
    {.pixel_shader = 0x34858FD0},             // rigidstandard ps40_gbuffer_alpha_test
    {.pixel_shader = 0x40EE89BC, .slot = 1},  // rigidstandard ps40_gbuffer_alpha_test_shroud_fizzle_oblique_tris
    {.pixel_shader = 0x5395F751},             // rigidstandard ps_pixel_selection_alpha_test
    {.pixel_shader = 0xD960DC10},             // rigidambientmapmesh ps40_gbuffer_alpha_test
}};

constexpr std::array<twattila::rt_scene::SceneShader, 25> SCENE_SHADERS = {{
    {.hash = 0x1D5C903F, .capture_shader = __rt_capture_terrain_geom_new_a, .terrain = true},
    {.hash = 0x1FC50EA9, .capture_shader = __rt_capture_terrain_geom_new_b, .terrain = true},
    {.hash = 0xE50F6CDF, .capture_shader = __rt_capture_terrain_custom_material, .terrain = true},
    {.hash = 0xE8291DEF, .capture_shader = __rt_capture_terrain_custom, .terrain = true},
    {.hash = 0x6B25B4D2, .capture_shader = __rt_capture_rigid_default},
    {.hash = 0x16AFAF14, .capture_shader = __rt_capture_rigid_standard, .rows_shader = __motion_record_rigid_rigidstandard_16af,
     .cut_out_shader = __rt_capture_rigid_standard_cut_out, .clip_cut_outs = true},
    {.hash = 0xC5CD75C5, .capture_shader = __rt_capture_rigid_blend, .rows_shader = __motion_record_rigid_rigidtextureblend_c5cd},
    {.hash = 0xB7CE3413, .capture_shader = __rt_capture_rigid_blend_wind, .rows_shader = __motion_record_rigid_rigidtextureblendwind_b7ce},
    {.hash = 0x8F4BAB8C, .capture_shader = __rt_capture_rigid_vertexpush},
    {.hash = 0x7FDEE7B4, .capture_shader = __rt_capture_branch},
    {.hash = 0xCD0E0357, .capture_shader = __rt_capture_leaf, .foliage = true},
    {.hash = 0x6609B69E, .capture_shader = __rt_capture_unit, .dynamic = true},
    {.hash = 0xF43EAAD7, .capture_shader = __rt_capture_unit_4_bones, .dynamic = true},
    {.hash = 0x6BEF6138, .capture_shader = __rt_capture_unit_dismember, .dynamic = true},
    {.hash = 0x407C1E30, .capture_shader = __rt_capture_unit_4_bones_dismember, .dynamic = true},
    {.hash = 0x71EB7463, .capture_shader = __rt_capture_grass_71eb, .dynamic = true, .foliage = true, .grass = true},
    // Far trees' cards (rigidtreebillboard vs40_main and vs40_main_block; the data run's A: 70 and 314 draws a frame), turned to
    // the sun (Far Tree Shadows), cut out by their tree map's alpha (72E40C15 t4, B3758A01 t3).
    {.hash = 0xC1A857A3, .capture_shader = __rt_capture_far_tree, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 4},
    {.hash = 0x62C6A599, .capture_shader = __rt_capture_far_tree_block, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 3},
    // The campaign's trees and plants (rigidcampaignvegetation vs40_main; ATN's 2026-11-14 campaign traces: 156 draws a frame, in
    // no scene before), every frame as drawn (their wind and their lean toward a high camera), cut out by their diffuse map's
    // alpha (F5C1006F: t1.w below 0.5).
    {.hash = 0x41147CD3, .capture_shader = __rt_capture_campaign_vegetation, .dynamic = true, .foliage = true, .mask_slot = 1},
    // The shadow pass's casters into the atlas (the data run's census: drawn there only), what is out of the main view (Pharaoh's
    // rt_outside_view: each shadow vertex shader keeps its view-projection at cb0[9-12] and writes the world it multiplies):
    // rigidterrain vs41_shadow_new and fx__rigidcommon vs40_shadow (TEXCOORD0 the world), fx__weighted vs40_shadow and its
    // 4 bones (TEXCOORD1 the UV, TEXCOORD2 the world). Every frame as drawn. Out: the depth-only EF11AD35.
    {.hash = 0x02EFC035, .terrain = true, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0xE2C36310, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0x2EE37868, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x2A54DBEE, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    // The campaign's trees and plants (rigidcampaignvegetation vs40_shadow; ATN's 2026-11-14 campaign traces: 49 draws a frame,
    // the shadow pass's only): TEXCOORD0 the world, TEXCOORD1 the UV of 5906CF6E's t0 test (its diffuse map's alpha below 0.5).
    {.hash = 0x7D979F60, .dynamic = true, .foliage = true, .cascade_keep = __rt_outside_view_world_uv},
    // The far trees' cards in the shadow pass (rigidtreebillboard vs40_shadow; ATN's siege traces: 2-16 draws a frame), turned to
    // the shadow camera: TEXCOORD0 the UV with the view's whole part and blend, TEXCOORD1 the world; cut out by B7ED5B72's t0
    // test, a card faded below half left out as that shader leaves it.
    {.hash = 0x75914932, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .cascade_keep = __rt_outside_view_card_split},
}};

// The SSR's copy (environment_specular_light ps40_copy) samples the resolve's output (7E1CDD0A, its content at the render
// share) at the quad's whole uv: ATN's 2026-11-14 traces 07-14 and 22-30 (vp 1920x1080 / 1280x720 reading t0 3840x2160).
// The portraits' tone mapper 1C42B26B (sprite.hlsl, vp 214x214) reads a display-size chain: left.
constexpr std::array<twattila::view_share::RegionRead, 1> RENDER_SCALE_READS = {{
    {.shader_hash = 0x7F23C472, .slots = 0b1},
}};

// Path-Traced AO and the paths' sky (rt_sun.cs_5_0.hlsl): each one makes its diffuse ambient in its first if on c11.y
// (spherical harmonics: t_world_spherical_harmonics one slot below lighting_VS_PS in all 31) and multiplies it by the
// diffuse colour right after (screen_space_shadows::PatchAmbient's AMBIENT_AFTER_HARMONICS).
using twattila::light_bounce::AMBIENT_AFTER_HARMONICS;
constexpr std::array<twattila::light_bounce::SunDraw, 31> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0x1AB80044, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},
    {.shader_hash = 0x2DCA8E30, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},
    {.shader_hash = 0xA03688A3, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},
    {.shader_hash = 0xC960A275, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},
    {.shader_hash = 0xE091B8C4, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 2},
    {.shader_hash = 0xF2484739, .standard = true, .gbuffer_slot = 3, .camera_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 1},
    {.shader_hash = 0x1494188B, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},
    {.shader_hash = 0x1579598F, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 2},
    {.shader_hash = 0x3115B6D3, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},
    {.shader_hash = 0x37237EC7, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},
    {.shader_hash = 0xDF7EA1A3, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x060623DD, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // slm_interior
    {.shader_hash = 0x1ECDE63F, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // slm_edges
    {.shader_hash = 0x488C845F, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // slm_edges
    {.shader_hash = 0x53224BCB, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // slm_interior
    {.shader_hash = 0x6257DF22, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // slm_interior
    {.shader_hash = 0xB699C6F8, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 2},  // slm_edges
    {.shader_hash = 0xD7990748, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // slm_edges
    {.shader_hash = 0xDE50CFAD, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // slm_edges
    {.shader_hash = 0xEB90E28A, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // slm_interior
    {.shader_hash = 0xEDADD458, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 2},  // slm_interior
    {.shader_hash = 0x049A86C2, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // skinlm_interior
    {.shader_hash = 0x290BE032, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 2},  // skinlm_edges
    {.shader_hash = 0x8D1F7560, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // skinlm_interior
    {.shader_hash = 0xB9663621, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // skinlm_edges
    {.shader_hash = 0xB9FC954D, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // skinlm_edges
    {.shader_hash = 0xC7D72E8C, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // skinlm_interior
    {.shader_hash = 0xCBA032CA, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // skinlm_edges
    {.shader_hash = 0xCEEE8017, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // skinlm_interior
    {.shader_hash = 0xDEEF87D5, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 2},  // skinlm_interior
    {.shader_hash = 0xDF071F9D, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS, .sky_harmonics_slot = 3},  // skinlm_edges
}};

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // Before any setting is read this frame (as Attila's): DLSS never starts on another vendor's GPU, a saved DLSS or a
  // preset's is XeSS on Intel's, else FSR.
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  tw::ao_trace::OnPresent();
  twwarhammer2::particle_smoothing::OnPresent();
  talesofarise::pixel_trace::OnPresent(queue);
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  // GT-VBAO's Temporal Smoothing (its noise moving each frame) and Denoiser (twattila/ao_filter.hpp).
  const bool bitmask_ao = shader_injection.ssao_mode >= 1.f;
  static uint32_t ao_frame = 0;
  shader_injection.ssao_frame =
      (bitmask_ao && shader_injection.ssao_temporal != 0.f) ? static_cast<float>(++ao_frame % 64) : 0.f;
  twattila::ao_filter::Update(swapchain, {.temporal = bitmask_ao && shader_injection.ssao_temporal != 0.f,
                                          .edge_fix = shader_injection.ssao_edge_fix != 0.f,
                                          .denoise = bitmask_ao && shader_injection.ssao_denoise != 0.f});
  twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
  // Skip the Shadow Maps: Ray-Traced Shadows Only's skip of the atlas's casters (with the rays' sun shadows alone).
  rt_settings.shadows_only = rt_settings.sun_shadows && rays_replace_maps_row != 0.f && rays_skip_cascades_row != 0.f;
  rt_settings.casters_out_of_view = rt_settings.sun_shadows && rays_units_out_of_view_row != 0.f;
  rt_settings.terrain_outside_view = rays_hills_out_of_view_row != 0.f;
  twattila::texture_lod_bias::SetWrappingTrilinear(lod_bias_trilinear_row != 0.f);
  // Ray-traced shadows replace the screen-space ones too (Graphics 1 of 2's DXR: in place of Bend's).
  auto sss_settings = twattila::feature_settings::SssSettings(sss_sliders);
  sss_settings.enabled = sss_settings.enabled && !rt_settings.sun_shadows;
  twattila::screen_space_shadows::Update(swapchain, sss_settings);
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: Warhammer II by ATN";  // TODO: change me!

namespace {

// Water Clarity and Water Caustics (Experimental, off; twattila/water_effects.hpp): the game's water writes the G-buffer with
// its colour per material (the patched copy swapped in at the draw), each family's five shadow variants (ps_4_1 the first).
// battle_water (8686041E in the traces): the colour lerps from shallow to deep by saturate(depth / (10 + 10 x waves)), the
// mad making that divisor divided by 1 / clarity; its refraction read 4 times (two outputs), the caustics after each.
// campaign_sea (DB511A83): saturate(depth x 3.333333), that mul's result x 1 / clarity. campaign_lake (EFE43500): Beer-Lambert,
// the exponent's log2(e) mul as WH3's. Campaign water no caustics (its world a twentieth of the battles'). Left out: campaign_river
// 4F6E96D0 (a fixed 50% lerp, no depth term), battle_river 47339F81 (in no trace; its fade a div_sat by sp_color_fade_dist).
constexpr std::array<twattila::water_effects::WaterShader, 15> WATER_SHADERS = {{
    {.hash = 0x8686041E, .world_input = 1, .refraction_slot = 5, .normal_slot = 10, .material_water = true, .opacity = {.type = 4, .index = {0x41200000, 0}, .divisor = true}, .every_refraction = true},  // battle_water (traced), gather 5_0
    {.hash = 0x094317D6, .world_input = 1, .refraction_slot = 5, .normal_slot = 10, .material_water = true, .opacity = {.type = 4, .index = {0x41200000, 0}, .divisor = true}, .every_refraction = true},  // battle_water, ps_4_1
    {.hash = 0x24ED82AD, .world_input = 1, .refraction_slot = 5, .normal_slot = 10, .material_water = true, .opacity = {.type = 4, .index = {0x41200000, 0}, .divisor = true}, .every_refraction = true},  // battle_water, pcf
    {.hash = 0xFCECCA82, .world_input = 1, .refraction_slot = 5, .normal_slot = 10, .material_water = true, .opacity = {.type = 4, .index = {0x41200000, 0}, .divisor = true}, .every_refraction = true},  // battle_water, software
    {.hash = 0x02533797, .world_input = 1, .refraction_slot = 4, .normal_slot = 9, .material_water = true, .opacity = {.type = 4, .index = {0x41200000, 0}, .divisor = true}, .every_refraction = true},  // battle_water, no shadows
    {.hash = 0xDB511A83, .world_input = 1, .refraction_slot = 5, .caustics = false, .material_water = true, .opacity = {.type = 4, .index = {0x40555555, 0}}},  // campaign_sea (traced), gather 5_0
    {.hash = 0xEAEECE81, .world_input = 1, .refraction_slot = 5, .caustics = false, .material_water = true, .opacity = {.type = 4, .index = {0x40555555, 0}}},  // campaign_sea, ps_4_1
    {.hash = 0x73F2961B, .world_input = 1, .refraction_slot = 5, .caustics = false, .material_water = true, .opacity = {.type = 4, .index = {0x40555555, 0}}},  // campaign_sea, pcf
    {.hash = 0xB814FB8E, .world_input = 1, .refraction_slot = 5, .caustics = false, .material_water = true, .opacity = {.type = 4, .index = {0x40555555, 0}}},  // campaign_sea, software
    {.hash = 0xC82CEAAC, .world_input = 1, .refraction_slot = 4, .caustics = false, .material_water = true, .opacity = {.type = 4, .index = {0x40555555, 0}}},  // campaign_sea, no shadows
    {.hash = 0xEFE43500, .world_input = 3, .refraction_slot = 6, .caustics = false, .material_water = true},  // campaign_lake (traced), gather 5_0
    {.hash = 0x98158B27, .world_input = 3, .refraction_slot = 6, .caustics = false, .material_water = true},  // campaign_lake, ps_4_1
    {.hash = 0x219D206A, .world_input = 3, .refraction_slot = 6, .caustics = false, .material_water = true},  // campaign_lake, pcf
    {.hash = 0x3A864ECC, .world_input = 3, .refraction_slot = 6, .caustics = false, .material_water = true},  // campaign_lake, software
    {.hash = 0xDEA0A768, .world_input = 3, .refraction_slot = 5, .caustics = false, .material_water = true},  // campaign_lake, no shadows
}};

// Graphics 1 of 2's (twattila/graphics_page.hpp): what is true here. The rays' sun shadows multiply the game's shadow maps
// (the sun shaders' patched comparisons), ray-traced AO multiplies the game's SSAO; reflections on the game's SSR resolve;
// the paths' sky from the game's spherical harmonics (rt_sun); Water Clarity and Caustics on the water's own draws.
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = twattila::graphics_page::ATTILA_TEXTS.aa_labels,
    .aa_tooltip = twattila::graphics_page::ATTILA_TEXTS.aa_tooltip,
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its ambient occlusion,\n"
                     "shadows and reflections. The rest at its default.",
    .no_rays_tooltip = "Bend Screen-Space Shadows (Length 480), GT-VBAO and GT-VBGI at their Recommended settings.\n"
                       "The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows, reflections and ambient occlusion, path-traced Global Illumination at my settings.\n"
                    "The rest of this page at its default. Needs an upscaler (Anti-Aliasing above).\n"
                    "On NVIDIA also turns DLSS Ray Reconstruction on. Elsewhere Experimental, slower: its own denoisers.",
    .shadow_tooltip = "Game's Shadow Maps: the original. Bend Screen-Space Shadows: those plus fine contact shadows from\n"
                      "what is on screen (Bend Studio's; soldiers, weapons, grass; needs in-game shadows on).\n"
                      "DXR: DirectX Raytracing, the sun's shadow from rays too, on top of the game's, in place of Bend's.\n"
                      "Needs an upscaler.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep in-game SSAO on for all of these.\n"
                  "Game's SSAO: everything counts as solid, so soldiers and trees get dark halos.\n"
                  "GT-VBAO (Ground Truth AO with a visibility bitmask): objects have a thickness, so light gets behind\n"
                  "thin ones.\n"
                  "RTAO: from rays through the scene, on top of the game's SSAO. Needs an upscaler.",
    .ao_paths_label = "Path-Traced (ReSTIR GI, Experimental)",
    .specular_occlusion_label = "Ray-Traced Specular Occlusion (Experimental)",
    .water_caustics_label = "Water Caustics (Experimental)",
    .fog_shadows_label = "Fog Shadows (Experimental)",
    .fog_shadows_tooltip = "Light shafts from rays in place of the game's own in the fog over the first 400 m: it loses the\n"
                           "sun's light in the shadow of buildings, trees and hills. Further on, the game's shadow maps.",
    .light_glow_label = "Torch and Fire Light in Fog (Experimental)",
    .light_glow_tooltip = "Torches and fires light the fog around them, with their shafts and shadows in it. Needs Fog Shadows. 0: off.",
    .replace_maps_tooltip = "The sun's shadows from rays alone, so Softness shows in full. Cloud shadows stay the game's;\n"
                            "what is outside the view casts none. Off: the rays' shadows on top of the game's shadow maps.",
    .reflections_tooltip = "Reflections on water, armour and metal. Need in-game Screen Space Reflections on.\n"
                           "Game's SSR: the original. DXR: from rays, also what is off screen or hidden. Needs an upscaler.",
};

void ConfigureAddon() {
  if (configured) return;

  // Construct the non-trivial containers here rather than during DLL loading.
  BuildRuntimeData();

  // Shaders cb
  renodx::mods::shader::expected_constant_buffer_space = 50;
  renodx::mods::shader::expected_constant_buffer_index = 13;

  // Swapchain cb
  renodx::mods::swapchain::expected_constant_buffer_index = 13;
  renodx::mods::swapchain::expected_constant_buffer_space = 50;

  // Swapchain upgrade settings
  renodx::mods::swapchain::use_resource_cloning = true;
  renodx::mods::swapchain::force_borderless = false;
  renodx::mods::swapchain::prevent_full_screen = true;

  // Proxy Shaders
  renodx::mods::swapchain::swap_chain_proxy_shaders = {
      {
          reshade::api::device_api::d3d11,
          {
              .vertex_shader = __swap_chain_proxy_vertex_shader_dx11,
              .pixel_shader = __swap_chain_proxy_pixel_shader_dx11,
          },
      },
  };

      // Resource Upgrades
    #if RENODX_MODS_SWAPCHAIN_VERSION == 2
      renodx::mods::swapchain::resource_upgrade_infos.push_back({
    #else
      renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    #endif
          .old_format = reshade::api::format::r8g8b8a8_unorm,
          .new_format = reshade::api::format::r16g16b16a16_float,
          .ignore_size = false,
          .use_resource_view_cloning = true,  // TODO: needed?
          .use_resource_view_hot_swap = true,
          .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
          .usage_include = reshade::api::resource_usage::render_target,
      });

  // Appended Settings (Graphics (Experimental))
  talesofarise::pixel_trace::file_prefix = "warhammer2";
  unit_detail[0].tweaks = {{.name = "VERY_SMALL_IMPOSTER_DISTANCE"}};
  unit_detail[1].tweaks = {{.name = "SMALL_IMPOSTER_DISTANCE"}};
  unit_detail[2].tweaks = {{.name = "MEDIUM_IMPOSTER_DISTANCE"}};
  unit_detail[3].tweaks = {{.name = "LARGE_IMPOSTER_DISTANCE"}};
  unit_detail[4].tweaks = {{.name = "VERY_LARGE_IMPOSTER_DISTANCE"}};
  unit_animation.tweaks = {
      {.name = "VERY_SMALL_SHARED_POSE_DISTANCE"},
      {.name = "SMALL_SHARED_POSE_DISTANCE"},
      {.name = "MEDIUM_SHARED_POSE_DISTANCE"},
      {.name = "LARGE_SHARED_POSE_DISTANCE"},
      {.name = "VERY_LARGE_SHARED_POSE_DISTANCE"},
      {.name = "VERY_SMALL_FACIAL_ANIMATION_DISTANCE"},
      {.name = "SMALL_FACIAL_ANIMATION_DISTANCE"},
      {.name = "MEDIUM_FACIAL_ANIMATION_DISTANCE"},
      {.name = "LARGE_FACIAL_ANIMATION_DISTANCE"},
      {.name = "VERY_LARGE_FACIAL_ANIMATION_DISTANCE"},
      {.name = "VERY_SMALL_HANDS_ANIMATION_DISTANCE"},
      {.name = "SMALL_HANDS_ANIMATION_DISTANCE"},
      {.name = "MEDIUM_HANDS_ANIMATION_DISTANCE"},
      {.name = "LARGE_HANDS_ANIMATION_DISTANCE"},
      {.name = "VERY_LARGE_HANDS_ANIMATION_DISTANCE"},
  };
  unit_variety.tweaks = {
      {.name = "VERY_SMALL_NO_VARIANT_DISTANCE"},
      {.name = "SMALL_NO_VARIANT_DISTANCE"},
      {.name = "MEDIUM_NO_VARIANT_DISTANCE"},
      {.name = "LARGE_NO_VARIANT_DISTANCE"},
      {.name = "VERY_LARGE_NO_VARIANT_DISTANCE"},
  };
  footstep_effects.tweaks = {
      {.name = "VERY_SMALL_FOOTSTEP_VFX_DISTANCE"},
      {.name = "SMALL_FOOTSTEP_VFX_DISTANCE"},
      {.name = "MEDIUM_FOOTSTEP_VFX_DISTANCE"},
      {.name = "LARGE_FOOTSTEP_VFX_DISTANCE"},
      {.name = "VERY_LARGE_FOOTSTEP_VFX_DISTANCE"},
  };
  model_detail.tweaks = {
      {.name = "distance_lod_scaler", .inverse = true},
  };
  // Only tweaks the game's code reads (tools\tw\tw_tweak_survey.py): battle_building_lod*, campaign_tree_lod*,
  // campaign_grass_lod* and shadowmap_extreme_quality are never read (Building / Campaign Detail removed).
  bush_detail.tweaks = {
      // Scrub LOD distance, copied into each scrub when it loads.
      {.name = "scrub_lod_distance_default"},
  };
  campaign_draw.tweaks = {
      // The campaign map's culling: objects smaller than this share of the view are not drawn.
      {.name = "campaign_culling_ratio", .inverse = true},
  };
  shadow_detail.tweaks = {
      // The shadow passes' LOD distance factor (the game sets it per Shadows setting), on the camera distance.
      {.name = "shadow_lod_distance_scaler", .inverse = true},
  };
  small_shadows.tweaks = {
      // Objects smaller than these (fading between them) cast no shadow.
      {.name = "cull_small_shadow_start_size", .inverse = true},
      {.name = "cull_small_shadow_end_size", .inverse = true},
  };
  terrain_detail.tweaks = {
      {.name = "terrain_distance_scale_low"},
      {.name = "terrain_distance_scale_medium"},
      {.name = "terrain_distance_scale_high"},
      {.name = "terrain_distance_scale_ultra"},
      {.name = "terrain_far_shader_distance"},
  };
  // Graphics 2 of 2's tests: Attila's that this game has, as it has them (without the values it lacks, its own game values
  // in the tooltips), then More Tests (more_tests.h).
  engine_tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), more_tests::ATTILA_TESTS);
  for (auto& test : engine_tests) {
    // Attila's top is its 90% x 1.11; this game's 97% (Warhammer2.exe's constructor) reaches 100% at 1.03.
    if (test.key == "AudioDefaultMemoryLimit") {
      test.max = 1.03f;
    }
    if (const auto tooltip = std::ranges::find(more_tests::TOOLTIPS, test.key, &std::pair<std::string, std::string>::first); tooltip != more_tests::TOOLTIPS.end()) {
      test.tooltip = tooltip->second;
    }
  }
  {
    auto more = more_tests::MoreTests();
    engine_tests.insert(engine_tests.end(), more.begin(), more.end());
  }
  graphics_options = {&unit_detail[0], &unit_detail[1], &unit_detail[2], &unit_detail[3], &unit_detail[4], &unit_animation, &unit_variety, &footstep_effects, &model_detail, &terrain_detail, &bush_detail, &campaign_draw, &shadow_detail, &small_shadows, &shadow_map};
  for (auto& test : engine_tests) graphics_options.push_back(&test.option);
  graphics_status = new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Engine values: not searched yet",
      .section = "Graphics (Experimental)",
  };
  // Graphics presets (ATN): MAX (every graphics setting at its maximum) and Reset (all graphics settings and tests at their
  // defaults, as Attila's; not the ambient occlusion or the Light Bounce).
  // The highest Shadows setting (+0x7e157d): mov [rbx + 0x1dd8], 8192.f; mov [rbx + 0x1ddc], 8192.f;
  // mov [rbx + 0x15c], 4096 (read when the game builds its shadow map: start-up, Shadows changed). The lower settings
  // use 4096 x shadow_resolution_multiplier.
  constexpr std::string_view HIGHEST_SHADOW_SIZE = "C7 83 D8 1D 00 00 00 00 00 46 C7 83 DC 1D 00 00 00 00 00 46 C7 83 5C 01 00 00 00 10 00 00";
  shadow_map.tweaks = {
      {.code_pattern = HIGHEST_SHADOW_SIZE, .code_offset = 6},
      {.code_pattern = HIGHEST_SHADOW_SIZE, .code_offset = 16},
      {.code_pattern = HIGHEST_SHADOW_SIZE, .code_offset = 26, .integer = true},
  };
  std::vector<std::pair<std::string, float>> graphics_defaults = {
      {"GraphicsShadowMapSize", 0.f},
      {"GraphicsUnitDetailVerySmall", 1.f},
      {"GraphicsUnitDetailSmall", 1.f},
      {"GraphicsUnitDetailMedium", 1.f},
      {"GraphicsUnitDetailLarge", 1.f},
      {"GraphicsUnitDetailVeryLarge", 1.f},
      {"GraphicsUnitAnimation", 1.f},
      {"GraphicsUnitVariety", 1.f},
      {"GraphicsFootstepEffects", 1.f},
      {"GraphicsModelDetail", 1.f},
      {"GraphicsTerrainDetail", 1.f},
      {"GraphicsBushDetail", 1.f},
      {"GraphicsCampaignDraw", 1.f},
      {"GraphicsShadowDetail", 1.f},
      {"GraphicsSmallShadows", 1.f},
  };
  for (const auto& test : engine_tests) {
    // Its option's start: the game's (1x, Game's or -1).
    graphics_defaults.emplace_back(test.key, test.option.scale);
  }
  const std::vector<std::pair<std::string, float>> graphics_max = {
      {"GraphicsShadowMapSize", 1.f},
      {"GraphicsUnitDetailVerySmall", 8.f},
      {"GraphicsUnitDetailSmall", 8.f},
      {"GraphicsUnitDetailMedium", 8.f},
      {"GraphicsUnitDetailLarge", 8.f},
      {"GraphicsUnitDetailVeryLarge", 8.f},
      {"GraphicsUnitAnimation", 4.f},
      {"GraphicsUnitVariety", 4.f},
      {"GraphicsFootstepEffects", 4.f},
      {"GraphicsModelDetail", 8.f},
      {"GraphicsTerrainDetail", 4.f},
      {"GraphicsBushDetail", 4.f},
      {"GraphicsCampaignDraw", 8.f},
      {"GraphicsShadowDetail", 4.f},
      {"GraphicsSmallShadows", 8.f},
  };
  // ATN's own settings: every graphics setting at its default but these.
  auto graphics_atn = graphics_defaults;
  for (const auto& [key, value] : std::vector<std::pair<std::string, float>>{
      {"GraphicsShadowMapSize", 1.f},
      {"GraphicsUnitDetailVerySmall", 2.f},
      {"GraphicsUnitDetailSmall", 2.f},
      {"GraphicsUnitDetailMedium", 2.f},
      {"GraphicsUnitDetailLarge", 2.f},
      {"GraphicsUnitDetailVeryLarge", 2.f},
      {"GraphicsUnitAnimation", 3.f},
      {"GraphicsUnitVariety", 3.f},
      {"GraphicsFootstepEffects", 3.f},
      {"GraphicsModelDetail", 1.5f},
      {"GraphicsTerrainDetail", 4.f},
       }) {
    std::ranges::find(graphics_atn, key, &std::pair<std::string, float>::first)->second = value;
  }
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Experimental: these may not work, and may crash the game. Some apply only after loading a battle or a save.",
          .section = "Graphics (Experimental)",
          .tint = 0xFFD23C,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting and test back to its default.",
          .on_change = [graphics_defaults]() { renodx::utils::settings::UpdateSettings(graphics_defaults); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "MAX",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting at its maximum. Heavy.",
          .on_change = [graphics_max]() { renodx::utils::settings::UpdateSettings(graphics_max); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "What I would use:",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 2x, Unit Animation, Variety and Footstep Effects 3x, Model Detail 1.50x, Terrain\nDetail 4x, Shadow Map Size 12288. The rest at its default.",
          .on_change = [graphics_atn]() { renodx::utils::settings::UpdateSettings(graphics_atn); },
      },
      tw::engine_tweaks::Slider(&unit_detail[0], "GraphicsUnitDetailVerySmall", "Unit Detail (Very Small Units)",
                                "How far very small units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[1], "GraphicsUnitDetailSmall", "Unit Detail (Small Units)",
                                "How far small units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[2], "GraphicsUnitDetailMedium", "Unit Detail (Medium Units)",
                                "How far medium units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[3], "GraphicsUnitDetailLarge", "Unit Detail (Large Units)",
                                "How far large units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[4], "GraphicsUnitDetailVeryLarge", "Unit Detail (Very Large Units)",
                                "How far very large units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_animation, "GraphicsUnitAnimation", "Unit Animation Distance",
                                "How far soldiers move on their own, with faces and hands (beyond it they copy their\n"
                                "neighbours' moves). Higher: heavier for the CPU.",
                                4.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_variety, "GraphicsUnitVariety", "Unit Variety Distance",
                                "How far soldiers keep their own heads and gear (beyond it they look alike).\n"
                                "Higher: heavier.",
                                4.f, 0.1f),
      tw::engine_tweaks::Slider(&footstep_effects, "GraphicsFootstepEffects", "Footstep Effects Distance",
                                "How far soldiers' footsteps kick up dust. Higher: heavier.", 4.f, 0.1f),
      tw::engine_tweaks::Slider(&model_detail, "GraphicsModelDetail", "Model Detail Distance",
                                "Units, buildings and other models keep their detailed versions farther away.", 8.f),
      tw::engine_tweaks::Slider(&terrain_detail, "GraphicsTerrainDetail", "Terrain Detail Distance",
                                "Detailed terrain reaches farther.", 4.f),
      tw::engine_tweaks::Slider(&bush_detail, "GraphicsBushDetail", "Bush Detail Distance",
                                "Bushes and scrub keep their detail farther away. From the next load.", 4.f),
      tw::engine_tweaks::Slider(&campaign_draw, "GraphicsCampaignDraw", "Campaign Draw Distance",
                                "Campaign map: small objects stay visible farther away. Heavier.", 8.f),
      tw::engine_tweaks::Slider(&shadow_detail, "GraphicsShadowDetail", "Shadow Detail Distance",
                                "Shadows keep the models' detailed shapes farther away. Heavier.", 4.f),
      tw::engine_tweaks::Slider(&small_shadows, "GraphicsSmallShadows", "Small Object Shadows",
                                "Small objects keep their shadows farther away. Heavier.", 8.f),
      new renodx::utils::settings::Setting{
          .key = "GraphicsShadowMapSize",
          .binding = &shadow_map.scale,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Shadow Map Size",
          .section = "Graphics (Experimental)",
          .tooltip = "Sharper sun shadows that flicker less, at the highest Shadows setting. More video memory.\n"
                     "Applies after a restart.",
          .labels = {"8192 (Game's)", "12288"},
          .parse = [](float value) { return (value >= 1.f) ? 1.5f : 1.f; },
      },
      graphics_status,
      // The pixel trace (talesofarise_dlss/pixel_trace.hpp), one frame on request; its marker only while it is open.
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
          .section = "Graphics (Experimental)",
          .on_draw = [] {
            if (!ImGui::TreeNode("Pixel trace")) return false;
            ImGui::SliderFloat2("Traced pixel (% of width, height)", traced_pixel.data(), 0.f, 100.f, "%.1f%%");
            // A red cross marks the pixel, the red line the traced line through it.
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            const ImVec2 cross = {screen.x * traced_pixel[0] / 100.f, screen.y * traced_pixel[1] / 100.f};
            const float half_line = screen.x * talesofarise::pixel_trace::LINE_WIDTH * 0.5f;
            ImDrawList* foreground = ImGui::GetForegroundDrawList(nullptr);
            foreground->AddLine({cross.x - half_line, cross.y}, {cross.x + half_line, cross.y}, IM_COL32(255, 0, 0, 255), 1.f);
            foreground->AddLine({cross.x, cross.y - 12.f}, {cross.x, cross.y + 12.f}, IM_COL32(255, 0, 0, 255), 2.f);
            if (ImGui::Button("Trace these pixels for one frame")) {
              talesofarise::pixel_trace::Request(traced_pixel[0] / 100.f, traced_pixel[1] / 100.f);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Saves every pass that changes the pixels under the red line during the next frame\n"
                                "to warhammer2_pixel_trace.txt next to the game's exe (one short hitch). Put the cross\n"
                                "on something too dark.");
            }
            ImGui::TextWrapped("%s", talesofarise::pixel_trace::Status().c_str());
            ImGui::TreePop();
            return false;
          },
      },
      // Two empty lines between this section and the ambient occlusion (ATN).
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "",
          .section = "Graphics (Experimental)",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "",
          .section = "Graphics (Experimental)",
      },
  });

  // Appended Settings (Graphics 2 of 2's tests)
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Tests: each changes the game's own values (1x / Game's: unchanged). Many apply from the next\n"
               "battle or after a restart; some may do nothing or crash. More Tests: every other graphics\n"
               "value the game reads (unsure ones).",
      .section = engine_tests.empty() ? std::string("More Tests") : engine_tests.front().section,
      .tint = 0xFFD23C,
  });
  twattila::engine_tests::Append(&settings, &engine_tests, SyncEarlyLoad);

  // Appended Settings (Ambient Occlusion, Global Illumination, Screen-Space Shadows, Anti-Aliasing)
  twattila::lighting_settings::Append(&settings, &shader_injection);
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, false, true);

  // Appended Settings (HDR10)
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR. Default.\n"
                   "HDR10: 10-bit. scRGB: 16-bit, 10-bit while Frame Generation is on. SDR: displays without HDR (all fixes\n"
                   "and options stay).",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const int value = static_cast<int>(setting->GetValue());
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = value,
        .hdr_encoding = [](int output) {
          if (output != twattila::output_encoding::AUTO) return output;
          // Frame generation on from the start: HDR10, the frames need no pass of their own (frame_generation.hpp).
          const bool frame_generation = aa_sliders.method == 5.f && aa_sliders.frame_generation != 0.f;
          return frame_generation ? twattila::output_encoding::HDR10 : twattila::output_encoding::SCRGB;
        },
    });
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV30, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom "
                   "PsychoV31 and PsychoV35. Visibility "
                   "Bitmask ambient occlusion and Light Bounce: the method of Therrien, Levesque and Gilet (2023) on "
                   "GTAO (Jimenez et al. 2016), as GT-VBAO and GT-VBGI by Mirko Salm (2024). Screen-space shadows: "
                   "Bend Studio (Apache 2.0, Sony Interactive Entertainment). SMAA (MIT, Jimenez et al.), CMAA2 "
                   "(Apache 2.0, Intel).",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::HELPER,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
          .section = "About",
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
          .label = "NexusMods",
          .section = "About",
          .group = "line_1",
          .tint = 0x2196F3,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarwarhammer2/mods/67"); },
      },
  });

  // Section titles and Settings Mode's pages as Attila's (twattila/feature_settings.hpp).
  renodx::utils::settings::default_section_tint = twattila::feature_settings::BASE_TINT;
  std::vector<std::string> engine_sections = {twattila::feature_settings::GRAPHICS_SECTION};
  for (const auto& test : engine_tests) {
    if (std::ranges::find(engine_sections, test.section) == engine_sections.end()) engine_sections.push_back(test.section);
  }
  twattila::feature_settings::SetTints({}, engine_sections);

  // Graphics 1 of 2's layout (as Pharaoh's and Warhammer III's), ATN's RT/PT preset as the defaults of what the methods
  // leave, then Settings Mode's pages. Only what this game hooks: no water pass for the rays (its rows stay hidden).
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  // Fog Shadows, the torches' light in it, Far Tree Shadows and Smoke Shadows start off (Experimental: no game run has shown
  // them here yet), Specular Occlusion too; RT/PT turns them on, as Attila's (rays_preset_as_attila).
  for (auto* setting : settings) {
    if (setting->key == "RayTracedFogShadows" || setting->key == "RayTracedLightGlow" || setting->key == "RayTracedSmokeShadows"
        || setting->key == "RayTracedSpecularOcclusion") {
      setting->default_value = 0.f;
    } else if (setting->key == "RayTracedFarTreeShadows") {
      setting->default_value = 0.f;
    }
  }
  // Torch and Fire Shadows from rays (Warhammer III's row): after the preset's defaults, so it starts off (Experimental: no game
  // run has shown them here yet); RT/PT turns it on, as ATN's list does.
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedPointLightShadows",
      .binding = &rt_sliders.local_lights,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Torch and Fire Shadows (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Torches, fires and spot lights cast their shadows from rays too.\n"
                 "With Path-Traced Global Illumination torches and fires also light what the screen never saw.",
      .labels = {"Off", "On"},
  });
  // Water Clarity and Caustics (Experimental: no game run has shown them here yet): the game's water at 1 and off.
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterClarity",
      .binding = &twattila::water_effects::water_clarity,
      .default_value = 1.f,
      .label = "Water Clarity (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "How far you see into water. 1: the game's; higher: clearer water.",
      .min = 0.25f,
      .max = 16.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterCaustics",
      .binding = &twattila::water_effects::water_caustics,
      .default_value = 0.f,
      .label = "Water Caustics (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Rippling light on the bed under battle water. 0: off.",
      .max = 2.f,
      .format = "%.2f",
  });
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders, .sss = &sss_sliders, .rt = &rt_sliders, .ssao_mode = &shader_injection.ssao_mode,
                                             .light_bounce_strength = &shader_injection.light_bounce_strength,
                                             .lod_bias_trilinear = &lod_bias_trilinear_row,
                                             .rays_replace_maps = &rays_replace_maps_row,
                                             .rays_replace_maps_tested = false,
                                             .rays_units_out_of_view = &rays_units_out_of_view_row,
                                             .units_out_of_view_tested = false,
                                             .rays_skip_cascades = &rays_skip_cascades_row,
                                             .rays_hills_out_of_view = &rays_hills_out_of_view_row,
                                             .texts = &PAGE_TEXTS,
                                             .foliage_light_tested = false,
                                             .smoke_shadows_tested = false,
                                             .far_cards_tested = false,
                                             .water_caustics_tested = false,
                                             .rays_preset_as_attila = true,
                                             .lights_own_row = true,
                                             .rays_replace_ao = false});
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();
  tw::engine_tweaks::watch::StartIfRequested(graphics_options, WATCHED_VALUES, 300);

  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  // The data run's RENODX_TW_SET / SHOT / PROFILE (nothing without them), timed from the first present.
  twattila::test_hooks::Install(&settings, true);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twwarhammer2::particle_smoothing::OnDestroyDevice);
  twwarhammer2::particle_smoothing::Install();
  // Water Clarity and Caustics: every row a material water (the patched copy swapped in at the draw).
  twattila::water_effects::water_shaders = WATER_SHADERS;
  renodx::utils::pipeline::RegisterOnInitCallback(twattila::water_effects::OnInitMaterialWater);
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests: written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(nullptr, &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // Before the Light Bounce's (its callback draws the sun draw again).
  // Ray tracing's at the sun draws first: its trace for the screen-space shadows and the AO filter after it.
  // The point lights' diffuse scale: D96A8DC3's literal 0x389FB405 (7.615242e-05; Warhammer III's, Pharaoh's).
  twattila::rt_scene::Host rt_host = twattila::rt_scene::TW_HOST;
  rt_host.local_light_scale = 7.615242e-05f;
  // The shadow pass's casters (cascade_keep) as Pharaoh's: its constants kept, world positions; its atlas the sun's one R32 map.
  rt_host.cascade_world = true;
  rt_host.cascade_target = [](const D3D11_TEXTURE2D_DESC& desc) {
    return desc.ArraySize == 1 && desc.Width == desc.Height && desc.Format == DXGI_FORMAT_R32_TYPELESS;
  };
  twattila::rt_scene::Install(SCENE_SHADERS, {}, rt_host);
  twattila::rt_scene::InstallCutOutMasks(CUT_OUT_MASKS);
  // Torch and Fire Shadows: the point lights (one instanced draw a frame, VS D2A0FF23 with PS D96A8DC3; its instances
  // POSITION1 the colour, POSITION2 the place and reach: FrameTraceReads in the data run's B, 131 lights at 25 m), then
  // ours over the game's shader while their rays are bound (PointLightRays). Fog Shadows the same way (FogRays).
  twattila::rt_scene::InstallPointLights(std::array<uint32_t, 1>{PointLightRays::SHADER});
  renodx::utils::command_action::Register(
      PointLightRays::OnDraw, {.shader_hash = PointLightRays::SHADER, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  reshade::register_event<reshade::addon_event::destroy_device>(PointLightRays::OnDestroyDevice);
  twattila::rt_scene::InstallPointLights(std::array<uint32_t, 2>{SpotLightRays::SHADER, GoboSpotLightRays::SHADER},
                                         twattila::rt_scene::internal::NO_LIGHT_CONSTANTS, &SPOT_LIGHTS);
  renodx::utils::command_action::Register(
      SpotLightRays::OnDraw, {.shader_hash = SpotLightRays::SHADER, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  renodx::utils::command_action::Register(
      GoboSpotLightRays::OnDraw, {.shader_hash = GoboSpotLightRays::SHADER, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  reshade::register_event<reshade::addon_event::destroy_device>(SpotLightRays::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(GoboSpotLightRays::OnDestroyDevice);
  twattila::rt_scene::InstallFogShadows(std::array<uint32_t, 1>{FogRays::SHADER});
  renodx::utils::command_action::Register(
      FogRays::OnDraw, {.shader_hash = FogRays::SHADER, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  reshade::register_event<reshade::addon_event::destroy_device>(FogRays::OnDestroyDevice);
  twattila::rt_scene::InstallSmokeShadows(SMOKE_VERTEX_SHADERS, SMOKE_PIXEL_SHADERS);
  // The Debug View's Screen-Space AO: before every other sun draw hook (they replace the AO the draw reads). No AO pass hook
  // keeps it here (the game's AO is compute line sweeps).
  twattila::rt_scene::InstallSunDebugAmbient(LIGHT_BOUNCE_DRAWS, &rt_sliders);
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace. The ambient cube at c2 (float 8) and
  // sun_colour at c1 (float 4) for the paths' sky and sun (rt_sun.cs_5_0.hlsl), with Path-Traced AO's hook.
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4);
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  // Light Through Foliage (Experimental): the G-buffer's channel 3 alpha byte (t7 of the sun draws), bit 128, which the sun
  // shaders' own code tests (1AB80044: and with 128 and 16). In the traces leaves (042027C6), far cards (72E40C15) and blocks
  // (B3758A01) and the banners' and blend meshes' 76B5602C write 128, grass (2315FFAB) 144, the campaign's terrain-blend
  // objects (41FD3F65) 255; the others 0-64 (tools/tw/wh2/material_writes_wh2.txt).
  twattila::screen_space_shadows::foliage_material_bit = 128;
  // Rays Replace Shadow Maps: after the screen-space shadows' and before the Light Bounce's. Skip the Shadow Maps through
  // Ray-Traced Shadows Only (rt_settings.shadows_only, OnPresent).
  twattila::sun_replace_maps::Install(ATLAS_SUN_SHADERS, &rays_replace_maps_row, ATLAS_SAMPLER_SLOT);
  twattila::rt_scene::InstallShadowsOnly();
  // The data run's frame traces (tools\tw\wh2\plan_1114.md; twattila/frame_trace.hpp): on demand (renodx_trace_now next to the
  // exe) and at 100% too. The camera's g_screen_size / g_viewport_dimensions at c27 / c29 as Attila's (the sun PS's camera RDEF).
  // Constants once each: the 11 single-sampled sun PS (directionallight slm / skinlm), the tone mapper (reinhard 2333E142), the
  // point and spot lights' VS (D2A0FF23, 5E0BC81E), the battle fog (inscattering 492B87C7, extinction 16AB86F4), the battle water
  // and river (31AB3DFB, A5294D06), the lit particles (F32116B4, DDABB799; their blend and 687378DB's) and their VS's buffers
  // (C3300B96, 1FC776EF); the lights' instances with FrameTraceReads (trace_reads.hpp).
  // The game maps some uvs to its viewport itself (uv x (c29.zw x c31.xy): the MSAA SSR resolve 4CFEBDF1, the particle
  // collision 537C77D0; tools\tw\wh2\scale_census_wh2_base.txt): view_share leaves them.
  twattila::view_share::SetViewportChains(true);
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, {}, {}, {},
                                   {
                                       .value_hashes = {0x1AB80044, 0x2DCA8E30, 0xA03688A3, 0xC960A275, 0xE091B8C4, 0xF2484739, 0x1494188B,
                                                        0x1579598F, 0x3115B6D3, 0x37237EC7, 0xDF7EA1A3, 0x2333E142, 0xD2A0FF23, 0x5E0BC81E,
                                                        0x492B87C7, 0x16AB86F4, 0x31AB3DFB, 0xA5294D06, 0xF32116B4, 0xDDABB799},
                                       .value_slots = 6,
                                       .value_floats = 256,
                                       .on_demand = true,
                                       .at_100 = true,
                                       .blend_hashes = {0xF32116B4, 0xDDABB799, 0x687378DB},
                                       .vertex_input_hashes = {0xC3300B96, 0x1FC776EF},
                                       .instances_apart = true,
                                       .draw_reads = twwarhammer2::trace_reads::Read,
                                   });
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  // Before the Light Bounce: it draws the sun draws itself (no callback after it runs).
  twattila::ao_filter::Install(LIGHT_BOUNCE_DRAWS);
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);

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
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  twattila::test_hooks::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twwarhammer2::particle_smoothing::OnDestroyDevice);
  twwarhammer2::particle_smoothing::Uninstall();
  renodx::utils::command_action::Unregister(twattila::water_effects::OnWaterDraw);
  renodx::utils::pipeline::UnregisterOnInitCallback(twattila::water_effects::OnInitMaterialWater);
  twattila::engine_tests::StopAtStart(&at_start_stop);
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::rt_scene::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
  twattila::ao_filter::Uninstall();
  talesofarise::pixel_trace::Stop();
  tw::engine_tweaks::Uninstall(graphics_options);
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  renodx::utils::command_action::Unregister(PointLightRays::OnDraw);
  renodx::utils::command_action::Unregister(SpotLightRays::OnDraw);
  renodx::utils::command_action::Unregister(GoboSpotLightRays::OnDraw);
  renodx::utils::command_action::Unregister(FogRays::OnDraw);
  twattila::sun_replace_maps::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(PointLightRays::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(SpotLightRays::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(GoboSpotLightRays::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(FogRays::OnDestroyDevice);

  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);

  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);

  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);

  attached = false;
}

}  // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE) {
  addon_module_handle = addon_module;
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
      addon_module_handle = h_module;
      // Not FALSE when it fails: ReShade next to renodx-tw-dlaa64.exe loads this addon there too, and after a FALSE the CRT's
      // thread_local cleanup faults in the loader (ntdll c0000005, a Windows crash report each start). AddonInit fails instead.
      addon_registered = reshade::register_addon(h_module);
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
