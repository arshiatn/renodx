/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/float16.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/state.hpp"
#include "./anti_aliasing.hpp"
#include "./rt_core.hpp"
#include "./ao_pass.hpp"
#include "./dlaa_bridge.hpp"
#include "./frame_trace.hpp"
#include "./feature_settings.hpp"
#include "./object_motion.hpp"
#include "./light_bounce.hpp"
#include "./screen_space_lights.hpp"
#include "./ssr_hiz.hpp"

// Ray tracing in the Total War games (rt_core.hpp's core with the TW games' passes and settings): at the game's ambient
// occlusion pass (its depth and camera bound) the frame's trace; its ambient visibility then multiplies the pass's
// result (rt_ambient_apply, red only), and the sun's visibility goes to the patched sun shaders' occlusion (t49;
// screen_space_shadows.hpp asks: rt_occlusion, the darker with its own). The sun's direction: the frame's first sun
// light draw's lighting buffer (rt_sun), for the next frame's trace. Games without an ambient occlusion pass of the
// addon's (Warhammer: their own line sweeps) trace at the frame's first sun light draw instead (its G-buffer depth and
// camera), and its ambient visibility multiplies the AO the sun draws read (rt_ambient_combine, bound in its place). The
// Light Bounce's trace (light_bounce.hpp) goes through rays when on; the reflection passes read the reflections. The
// scene's captures: rt_capture_*.vs (tools\tw\attila_motion\make_wind_motion_vs.py: the game's code giving the world
// position), Warhammer III's the game's own shaders. Needs DLAA / DLSS (the bridge). D3D11.
namespace twattila::rt_scene {

// The pixel trace's look at the rays' AO (a game with rt_ambient_combine): the rays and the combine run past ReShade's
// events, so the trace's commands never show them. While wanted, each combine writes `text`.
struct AmbientProbe {
  std::shared_mutex mutex;
  bool wanted = false;
  std::array<float, 2> position = {};  // 0-1 of the picture
  std::string text;
};
inline AmbientProbe ambient_probe;

using Microsoft::WRL::ComPtr;

// The settings as the addon's sliders hold them (in the anti-aliasing section: with DLAA / DLSS).
struct Sliders {
  float sun_shadows = 0.f;
  float sun_softness = 0.5f;       // degrees of the sun's disk (0: hard)
  float light_softness = 0.25f;    // the torches' and fires' radius (metres; 0: hard)
  float light_glow = 1.f;          // torch and fire light in fog and smoke (0: none)
  float output_rays = 0.f;         // Rays a Pixel: 0 one, 1 the output pixels a render pixel stands for, 2-8 that many
  float rays_shadows = 1.f;        // what it counts for
  float rays_ambient = 1.f;
  float rays_reflections = 1.f;
  float rays_paths = 1.f;
  float debug_log = 0.f;           // the statistics in the log every 10 s
  float sprite_shadows = 1.f;      // the flat far soldiers in the scene (Attila)
  float far_tree_shadows = 1.f;    // the flat far trees in the scene (Attila)
  float leaf_light = 0.f;
  float coloured_shadows = 1.f;    // Attila (its sun light shaders' colour)
  float grass_sway = 1.f;          // placed grass's swaying copies (Attila)
  float sway_range = 0.f;          // metres (0: everywhere)
  float sway_off_screen = 1.f;
  float fast_leaves = 0.f;
  float grass_shadow_range = 0.f;  // metres (0: everywhere)
  float grass_range = 0.f;         // metres (0: everywhere; placed grass, Attila)
  float path_ser = 0.f;
  float sharc = 0.f;
  float light_importance = 0.f;
  float light_steady = 0.f;
  float light_count = 0.f;  // Torch and Fire Shadow Lights: 32, 64, 128
  float light_range = 0.f;  // metres (0: any distance)
  float ambient_occlusion = 0.f;   // off, on (Attila: in place of the game's and GT-VBAO)
  float ambient_range = 6.f;       // metres
  float ambient_rays = 2.f;
  float ambient_quarter = 0.f;     // 1: a quarter of the rays (faster)
  float fog_shadows = 0.f;  // the fog's (games whose fog InstallFogShadows hooks)
  float local_lights = 1.f;  // with Ray-Traced Shadows; a row only where the game's light draws aren't proven yet
  float micromaps = 1.f;
  float gpu_micromaps = 1.f;
  float smoke_shadows = 0.f;  // games whose particles InstallSmokeShadows hooks
  float smoke_quality = 1.f;
  float denoiser = 0.f;  // Ray Tracing Denoiser (Attila's page): Built-in, NRD
  float ao_reblur = 1.f;  // AO under Ray Reconstruction: Light History, REBLUR (games without graphics_page's row keep v34's REBLUR)
  float start_past = 0.f;  // Rays Start Past Hidden Shapes (Bindings::rays_start_past)
  float sheets_unselfed = 0.f;  // Banners Without Self-Shadows (Bindings::rays_sheets_unselfed)
  float light_bounce = 0.f;  // the Light Bounce's tracer: 0 its own (screen space), 1 rays, 2 paths
  float light_bounce_rays = 4.f;
  float light_bounce_range = 10.f;
  float light_bounce_resolution = 1.f;  // 0 full, 1 half, 2 quarter
  float path_bounces = 2.f;
  float path_ambient = 0.f;  // Path-Traced Ambient (games InstallSunDraws gives the ambient cube's place)
  float path_sky_apart = 1.f;
  float path_kept_cube = 0.f;  // Path-Traced AO: the bounces' stand-in light from the game's cube
  float reflections = 0.f;
  float specular_occlusion = 0.f;
  float water_reflections = 1.f;  // the water's from its own surface (games whose reflection pass OnReflectionDraw hooks)
  float reflection_samples = 1.f;
  float reflection_reuse = 0.f;  // 0 off, 1 from ReSTIR GI, 2 Own ReSTIR
  float water_steady = 1.f;  // Steadier Water Reflections
  float water_soft = 1.f;    // Softer Water Reflections
  float debug_view = 0.f;  // games whose tone mapper hook calls OnTonemapDrawn
  float scene_test = 0.f;  // with the Debug View on
};

namespace internal {
// The sun draws' lighting buffer's ambient cube, its first float (InstallSunDraws; 0: none): the paths' sky.
static UINT ambient_cube = 0;
static bool fog_shadows_installed = false;
static bool smoke_shadows_installed = false;

// Cloth, sails or banners among the captures (thin): what Coloured Shadows colours. Without them it has nothing to do.
inline bool ThinInScene() {
  return std::ranges::any_of(scene_shaders, [](const SceneShader& shader) { return shader.thin || shader.thin_row != nullptr || shader.thin_sheets_row != nullptr; });
}
}  // namespace internal

// `light_bounce_strength`: the game's Light Bounce (its rays and paths replace the Light Bounce's trace; off, nothing to
// trace for them, so they keep ray tracing off).
// `game_render_scale` above 0: the render's scale where the game owns it (else the rows').
inline Settings SettingsOf(const Sliders& sliders, const feature_settings::AaSliders& aa, float light_bounce_strength, float game_render_scale = 0.f) {
  const bool dlaa = feature_settings::Upscaling(aa);
  const bool bounce = light_bounce_strength > 0.f && dlaa;
  const bool path_ambient = sliders.path_ambient != 0.f && sliders.light_bounce == 2.f && bounce && internal::ambient_cube != 0
                            && !screen_space_shadows::AmbientNotAsExpected();
  // Rays a Pixel: the picture is at the render size until DLSS, nothing traced can be kept finer than that: each render pixel
  // traces more rays instead, each from its own spot of it. Match Output: those of the output pixels it stands for (at most
  // 8), as many on screen as at DLAA (ATN: RR Performance's 1080p rays looked noisy at 4K).
  const float render_scale = (game_render_scale > 0.f) ? game_render_scale : feature_settings::RenderScale(aa);
  const auto density = static_cast<uint32_t>(
      (sliders.output_rays == 1.f) ? std::clamp(std::lround(1.f / (render_scale * render_scale)), 1l, 8l)
                                   : std::clamp(std::lround(sliders.output_rays), 1l, 8l));
  const auto rays_of = [density](float on) { return (on != 0.f) ? density : 1u; };
  return {
      .sun_shadows = sliders.sun_shadows != 0.f && dlaa,
      .ambient_occlusion = sliders.ambient_occlusion != 0.f && dlaa && !path_ambient,
      .ambient_range = sliders.ambient_range,
      .ambient_rays = static_cast<uint32_t>(sliders.ambient_rays) * rays_of(sliders.rays_ambient),
      // Effects at Output Resolution: every pixel each frame from one step of 2 down (as many rays on screen as at 100%;
      // with Rays a Pixel the rays make up for it).
      .ambient_quarter = sliders.ambient_quarter != 0.f && (rays_of(sliders.rays_ambient) > 1u || frame_history::EffectSteps() < 1.f),
      .sun_softness = sliders.sun_softness,
      .light_size = sliders.light_softness,
      .shadow_rays = rays_of(sliders.rays_shadows),
      .light_glow = sliders.light_glow,
      .light_bounce = sliders.light_bounce != 0.f && bounce,
      .light_bounce_rays = std::min(static_cast<uint32_t>(sliders.light_bounce_rays) * rays_of(sliders.rays_paths), 16u),
      .light_bounce_range = sliders.light_bounce_range,
      .light_bounce_resolution = sliders.light_bounce_resolution,
      .path_tracing = sliders.light_bounce == 2.f && bounce,
      // 3 and 4 (the row's until v144) measured as 2: within half a percent in a room wholly out of sight.
      .path_bounces = std::min(static_cast<uint32_t>(sliders.path_bounces), 2u),
      .path_ambient = path_ambient,
      .path_sky_apart = sliders.path_sky_apart != 0.f,
      .path_kept_cube = sliders.path_kept_cube != 0.f,
      .reflections = sliders.reflections != 0.f && dlaa,
      .specular_occlusion = sliders.specular_occlusion != 0.f && dlaa,
      .water_reflections = sliders.water_reflections != 0.f,
      .reflection_samples = static_cast<uint32_t>(sliders.reflection_samples) * rays_of(sliders.rays_reflections),
      .reflection_reuse = sliders.reflection_reuse == 1.f && sliders.light_bounce == 2.f,
      .reflection_own = sliders.reflection_reuse == 2.f,
      .water_steady = sliders.water_steady != 0.f,
      .water_soft = sliders.water_soft != 0.f && sliders.reflections != 0.f && sliders.water_reflections != 0.f && dlaa,
      // Ray-traced sun shadows are every shadow: the point lights' too (games whose light draws InstallPointLights hooks),
      // the game's sun shadow maps not drawn (games that InstallShadowsOnly).
      .local_lights = sliders.local_lights != 0.f && sliders.sun_shadows != 0.f && dlaa,
      .light_count = dlaa_bridge::RT_MAX_LIGHTS << std::clamp(static_cast<int>(sliders.light_count), 0, 2),
      .light_range = sliders.light_range,
      .leaf_light = sliders.leaf_light != 0.f,
      .coloured_shadows = sliders.coloured_shadows != 0.f && sliders.leaf_light != 0.f && internal::ambient_cube != 0 && internal::ThinInScene(),
      .grass_sway = sliders.grass_sway != 0.f,
      .sway_range = sliders.sway_range,
      .sway_off_screen = sliders.sway_off_screen != 0.f,
      .fast_leaves = sliders.fast_leaves != 0.f,
      // Pixels past Grass Ray Tracing Range trace without grass too: past it there is none, and their grass rays met nothing
      // at full cost (harness at 4K, high camera, 60 m: trace 2.42-2.50 ms without, 2.02-2.14 with the cut alone, 1.89-1.95
      // with this).
      .grass_shadow_range = (sliders.grass_range > 0.f && (sliders.grass_shadow_range <= 0.f || sliders.grass_range < sliders.grass_shadow_range))
                                ? sliders.grass_range
                                : sliders.grass_shadow_range,
      .grass_range = sliders.grass_range,
      .path_ser = sliders.path_ser != 0.f,
      .sharc = sliders.sharc != 0.f,
      .light_importance = sliders.light_importance != 0.f,
      .light_steady = sliders.light_steady != 0.f,
      // A page's preset can set it where no fog is hooked: nothing would read the trace.
      .fog_shadows = sliders.fog_shadows != 0.f && dlaa && internal::fog_shadows_installed,
      .micromaps = sliders.micromaps != 0.f,
      .gpu_micromaps = sliders.gpu_micromaps != 0.f,
      .smoke_shadows = sliders.smoke_shadows != 0.f && sliders.sun_shadows != 0.f && dlaa && internal::smoke_shadows_installed,
      .smoke_quality = static_cast<uint32_t>(sliders.smoke_quality),
      .nrd = sliders.denoiser != 0.f,
      .ao_reblur = sliders.ao_reblur != 0.f,
      .start_past = sliders.start_past != 0.f,
      .sheets_unselfed = sliders.sheets_unselfed != 0.f,
      .shadows_only = sliders.sun_shadows != 0.f && dlaa,
      .debug_view = static_cast<uint32_t>(sliders.debug_view),
      .scene_test = ((sliders.debug_view != 0.f) ? static_cast<uint32_t>(sliders.scene_test) : 0u),
      .debug_log = sliders.debug_log != 0.f,
      .sprite_shadows = sliders.sprite_shadows != 0.f,
      .far_tree_shadows = sliders.far_tree_shadows != 0.f,
  };
}

// Softer Water Reflections' least roughness on water (Attila's guides give it 0.03, Warhammer III's 0.01): past the helper's
// mirrors (ReflectPixel: under 0.05, one ray, no history, no filter), so Samples, the history and the 3 x 3 filter take it.
constexpr float WATER_SOFT_ROUGHNESS = 0.06f;

// What the anti-aliasing's bridge makes for these settings: the ray tracing textures, the G-buffer guides (the paths read
// them) and the reflections' textures.
inline void Configure(const Settings& settings, anti_aliasing::Settings* aa) {
  aa->ray_tracing = settings.sun_shadows || settings.ambient_occlusion || settings.light_bounce || settings.reflections
                    || settings.specular_occlusion || settings.local_lights || settings.fog_shadows;
  // Ray Reconstruction only with rays to denoise: without them DLSS's usual model (ATN 2026-11-01). The fog's shadows alone light
  // no surface (Settings::cheap_fog_alone).
  aa->ray_reconstruction = aa->ray_reconstruction && aa->ray_tracing && !internal::CheapFogAlone(settings);
  aa->ray_traced_guides = settings.path_tracing || settings.reflections || settings.specular_occlusion;
  aa->ray_traced_reflections = settings.reflections || settings.specular_occlusion;
  // The Light Bounce through rays: at their own resolution, no Checkerboard.
  light_bounce::SetRayTraced(settings.light_bounce, settings.light_bounce_resolution);
  aa->water_roughness = settings.water_soft ? WATER_SOFT_ROUGHNESS : 0.f;
}

// The TW games' view: DLAA's (anti_aliasing.hpp); a rigid draw DLAA's motion saw moving stays out of the scene.
inline const Host TW_HOST = {
    .camera = [](ID3D11Buffer* buffer, Camera* camera) {
      anti_aliasing::internal::Camera main;
      if (!anti_aliasing::MainCamera(buffer, &main)) return false;
      *camera = {.view_projection = main.view_projection, .inverse_view_projection = main.inverse_view_projection, .position = main.position};
      return true;
    },
    .render_size = anti_aliasing::RenderSize,
    .guides_written = anti_aliasing::GuidesWritten,
    .moving = [](ID3D11DeviceContext* native, const renodx::utils::command_action::DrawArguments& arguments) {
      return object_motion::MovingMesh(object_motion::MeshKey(native, arguments));
    },
    .moving_indexed = [](ID3D11DeviceContext* native, const renodx::utils::command_action::DrawIndexedArguments& arguments) {
      return object_motion::MovingMesh(object_motion::MeshKey(native, arguments));
    },
    .main_view = frame_history::MainViewport,
    .rewrite_guides = anti_aliasing::RewriteGuides,
};

namespace internal {
// A reflection pass OnReflectionDraw hooks ran (Attila's shaders: Ray-Traced Water Reflections shown).
static bool water_pass = false;
// The game has sprite soldiers in its scene shaders (Sprite Soldier Shadows shown).
static bool sprite_shadows_installed = false;
static bool far_tree_shadows_installed = false;
}  // namespace internal

// The game's reflection pass (environment_specular_light, Attila's shaders: Attila, Thrones), before its draws: the
// ray-traced reflections (the water's from its own surface: the depth-stencil the draw reads its stencil from at t8;
// their hits' light: the light buffer it reads at t6),
// and the screen-space march's pyramid and resolve (ssr_hiz.hpp) for what keeps the march (everything without them;
// with them water, with Ray-Traced Water Reflections off). True: draw.
inline bool OnReflectionDraw(reshade::api::command_list* cmd_list) {
  internal::water_pass = true;
  ComPtr<ID3D11ShaderResourceView> depth_stencil;
  ComPtr<ID3D11ShaderResourceView> light_buffer;
  if (auto* native = internal::NativeContext(cmd_list); native != nullptr) {
    native->PSGetShaderResources(6, 1, light_buffer.GetAddressOf());
    native->PSGetShaderResources(8, 1, depth_stencil.GetAddressOf());
  }
  const bool reflected = BindReflections(cmd_list, false, true, depth_stencil.Get(), light_buffer.Get());
  bool water_traced = false;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    water_traced = internal::runtime.water_traced;
  }
  return ssr_hiz::OnReflectionDraw(cmd_list, ssr_hiz::internal::DEPTH_SLOT, !(reflected && water_traced));
}

// After its draws: the views unbound, the screen-space march's resolve when it ran.
inline void OnReflectionDrawn(reshade::api::command_list* cmd_list) {
  ssr_hiz::OnReflectionDrawn(cmd_list);
  BindReflections(cmd_list, false, false);
}

namespace internal {

// The Debug View's copy of `resource` (read as `format`), made again when that changes. Under the lock.
inline void PrepareDebugAmbient(ID3D11DeviceContext* native, const ComPtr<ID3D11Resource>& resource, DXGI_FORMAT format, bool whole_2d_view) {
  auto& objects = runtime.objects;
  if (!resource || resource.Get() == objects.debug_ambient_resource.Get()) return;
  const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = format,
                                                     .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                     .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
  objects.debug_ambient.Reset();
  objects.debug_ambient_copy.Reset();
  objects.debug_ambient_resource = resource;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC copy_desc = {};
  if (whole_2d_view && SUCCEEDED(resource.As(&texture))) {
    texture->GetDesc(&copy_desc);
    copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy_desc.MiscFlags = 0;
    if (copy_desc.SampleDesc.Count == 1 && SUCCEEDED(device->CreateTexture2D(&copy_desc, nullptr, objects.debug_ambient_copy.GetAddressOf()))) {
      device->CreateShaderResourceView(objects.debug_ambient_copy.Get(), &view_desc, objects.debug_ambient.GetAddressOf());
    }
  }
}

}  // namespace internal

// A game whose AO no pass here draws (OnAmbientOcclusionDraw): what its sun draws read, for the Debug View's Screen-Space AO;
// `mapping`: how much further they read that texture than a pixel's share of the picture.
inline void KeepDebugAmbient(ID3D11DeviceContext* native, ID3D11ShaderResourceView* view, float mapping) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (runtime.settings.debug_view < 8) return;
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
  view->GetDesc(&view_desc);
  internal::PrepareDebugAmbient(native, resource, view_desc.Format, view_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D);
  auto& objects = runtime.objects;
  if (!objects.debug_ambient_copy) return;
  native->CopyResource(objects.debug_ambient_copy.Get(), resource.Get());
  runtime.debug_ambient_mapping = mapping;
  runtime.debug_ambient_frame = runtime.frame;
}

namespace internal {
// InstallSunDebugAmbient's: the addon's sliders, whose Debug View each sun draw reads first.
static const Sliders* debug_ambient_sliders = nullptr;
}  // namespace internal

// A sun light draw where no AO pass hook keeps the Debug View's Screen-Space AO: the AO it reads at ao_slot (the game's SSAO
// or GT-VBAO, blurred), copied before the rays' combine and the AO filter put theirs there. Mapping 1 (rt_debug's own): the
// sun shaders of Pharaoh, Troy, Warhammer and Warhammer II read it at (SV_Position + g_vpos_texel_offset) x g_screen_size.zw,
// the textures' own uv (their render's share at the top left), as their G-buffer.
constexpr auto OnSunDebugAmbient = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (internal::debug_ambient_sliders->debug_view < 8.f) return {};
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !frame_history::MainViewport(native)) return {};
  const auto& draw = command.template GetCallbackData<light_bounce::SunDraw>();
  ComPtr<ID3D11ShaderResourceView> game_ambient;
  native->PSGetShaderResources(draw.ao_slot, 1, game_ambient.GetAddressOf());
  if (!game_ambient) return {};
  KeepDebugAmbient(native, game_ambient.Get(), 1.f);
  static std::atomic_bool logged = false;
  if (!logged.exchange(true)) {
    renodx::utils::log::i(std::format("[Total War] First run: the Debug View's Screen-Space AO from sun draw {:08X} (t{}).", draw.shader_hash,
                                      draw.ao_slot));
  }
  return {};
};

// The Debug View's Screen-Space AO at the sun draws with an ao_slot (OnSunDebugAmbient), the addon's, kept while installed:
// before every other sun draw hook (InstallSunDraws', the AO filter's: they bind their AO in its place). `sliders`: the
// addon's, kept too.
inline void InstallSunDebugAmbient(std::span<const light_bounce::SunDraw> sun_draws, const Sliders* sliders) {
  internal::debug_ambient_sliders = sliders;
  for (const auto& draw : sun_draws) {
    if (draw.ao_slot == UINT_MAX) continue;
    renodx::utils::command_action::Register(
        OnSunDebugAmbient, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
}

// The game's ambient occlusion pass (pre callback): this frame's trace; after it (post), its result times the traced
// ambient visibility.
constexpr auto OnAmbientOcclusionDraw = []<typename T, typename Context>(
                                            Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (!internal::active.load(std::memory_order_relaxed)) return {};
    auto* native = internal::NativeContext(command.cmd_list);
    if (native == nullptr) return {};
    auto& runtime = internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    // The Debug View's Screen-Space AO: this pass's target (the game's or GT-VBAO's ambient occlusion), copied after it
    // and before the traced ambient visibility goes in (it showed both, ATN 2026-10-28).
    const bool keep_ambient = runtime.settings.debug_view >= 8;
    if (keep_ambient) {
      ComPtr<ID3D11RenderTargetView> target;
      native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
      ComPtr<ID3D11Resource> resource;
      if (target) target->GetResource(resource.GetAddressOf());
      if (target) {
        D3D11_RENDER_TARGET_VIEW_DESC target_desc = {};
        target->GetDesc(&target_desc);
        internal::PrepareDebugAmbient(native, resource, target_desc.Format, target_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D);
      }
    }
    ComPtr<ID3D11ShaderResourceView> depth;
    ComPtr<ID3D11Buffer> camera;
    native->PSGetShaderResources(ao_pass::internal::DEPTH_SLOT, 1, depth.GetAddressOf());
    native->PSGetConstantBuffers(ao_pass::internal::CAMERA_SLOT, 1, camera.GetAddressOf());
    internal::Trace(native, depth.Get(), camera.Get());
    if (!keep_ambient && (!runtime.settings.ambient_occlusion || runtime.traced_frame != runtime.frame)) return {};
    return {.post_callback = [](Context& post, const void*) {
      auto* post_native = internal::NativeContext(post.cmd_list);
      if (post_native == nullptr) return;
      auto& post_runtime = internal::runtime;
      const std::unique_lock post_lock(post_runtime.mutex);
      auto& objects = post_runtime.objects;
      if (post_runtime.settings.debug_view >= 8 && objects.debug_ambient_copy) {
        post_native->CopyResource(objects.debug_ambient_copy.Get(), objects.debug_ambient_resource.Get());
        post_runtime.debug_ambient_frame = post_runtime.frame;
      }
      if (!post_runtime.settings.ambient_occlusion || post_runtime.traced_frame != post_runtime.frame) return;
      post_runtime.ambient_frame = post_runtime.frame;
      std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
      UINT viewport_count = static_cast<UINT>(viewports.size());
      post_native->RSGetViewports(&viewport_count, viewports.data());
      if (viewport_count == 0 || viewports[0].Width <= 0.f || viewports[0].Height <= 0.f) return;
      // The game's bindings this changes, put back after.
      ComPtr<ID3D11VertexShader> game_vertex_shader;
      ComPtr<ID3D11PixelShader> game_pixel_shader;
      ComPtr<ID3D11BlendState> game_blend;
      std::array<float, 4> blend_factor = {};
      UINT sample_mask = 0;
      ComPtr<ID3D11RasterizerState> game_rasterizer;
      ComPtr<ID3D11DepthStencilState> game_depth;
      UINT stencil_reference = 0;
      ComPtr<ID3D11ShaderResourceView> game_view;
      ComPtr<ID3D11SamplerState> game_sampler;
      ComPtr<ID3D11Buffer> game_constants;
      ComPtr<ID3D11InputLayout> game_layout;
      D3D11_PRIMITIVE_TOPOLOGY game_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      post_native->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
      post_native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
      post_native->OMGetBlendState(game_blend.GetAddressOf(), blend_factor.data(), &sample_mask);
      post_native->RSGetState(game_rasterizer.GetAddressOf());
      post_native->OMGetDepthStencilState(game_depth.GetAddressOf(), &stencil_reference);
      post_native->PSGetShaderResources(0, 1, game_view.GetAddressOf());
      post_native->PSGetSamplers(0, 1, game_sampler.GetAddressOf());
      post_native->PSGetConstantBuffers(0, 1, game_constants.GetAddressOf());
      post_native->IAGetInputLayout(game_layout.GetAddressOf());
      post_native->IAGetPrimitiveTopology(&game_topology);
      const std::array<float, 4> constants = {
          static_cast<float>(post_runtime.traced_size[0]) / viewports[0].Width,
          static_cast<float>(post_runtime.traced_size[1]) / viewports[0].Height,
          1.f / static_cast<float>(dlaa_bridge::bridge.width),
          1.f / static_cast<float>(dlaa_bridge::bridge.height),
      };
      std::array<uint8_t, 96> padded = {};
      std::memcpy(padded.data(), constants.data(), sizeof(constants));
      post_native->UpdateSubresource(objects.constants.Get(), 0, nullptr, padded.data(), 0, 0);
      ID3D11ShaderResourceView* ambient = dlaa_bridge::bridge.rt_ambient.srv.Get();
      post_native->VSSetShader(objects.fullscreen_shader.Get(), nullptr, 0);
      post_native->PSSetShader(objects.ambient_shader.Get(), nullptr, 0);
      post_native->PSSetShaderResources(0, 1, &ambient);
      post_native->PSSetSamplers(0, 1, objects.linear_clamp.GetAddressOf());
      post_native->PSSetConstantBuffers(0, 1, objects.constants.GetAddressOf());
      post_native->IASetInputLayout(nullptr);
      post_native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      post_native->OMSetBlendState(objects.multiply.Get(), nullptr, 0xFFFFFFFF);
      post_native->RSSetState(objects.rasterizer.Get());
      post_native->OMSetDepthStencilState(objects.no_depth.Get(), 0);
      post_native->Draw(3, 0);
      post_native->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
      post_native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
      post_native->OMSetBlendState(game_blend.Get(), blend_factor.data(), sample_mask);
      post_native->RSSetState(game_rasterizer.Get());
      post_native->OMSetDepthStencilState(game_depth.Get(), stencil_reference);
      post_native->PSSetShaderResources(0, 1, game_view.GetAddressOf());
      post_native->PSSetSamplers(0, 1, game_sampler.GetAddressOf());
      post_native->PSSetConstantBuffers(0, 1, game_constants.GetAddressOf());
      post_native->IASetInputLayout(game_layout.Get());
      post_native->IASetPrimitiveTopology(game_topology);
    }};
  }
};

namespace internal {

// Ray-Traced Shadows Only: the sun draws' shadow map (a square depth texture of theirs, 1024 and up), learned at each
// frame's first sun draw; while it is bound alone for drawing, the casters are skipped (the game's clear leaves it
// shadowing nothing) as long as the sun draws took the rays' shadows the frame before (a sun shader without the
// patch, screen_space_shadows.hpp, keeps the game's).
static bool shadows_only_installed = false;
// The Debug View: shown once the addon's tone mapper hook draws it (OnTonemapDrawn).
static std::atomic_bool debug_view_drawn = false;
static std::atomic<uint64_t> shadow_map = 0;
static std::atomic_bool skipping_casters = false;

}  // namespace internal

// A sun light draw (pre callback): the frame's first writes the sun's direction (from its lighting buffer, as the game's
// layout for the draw says) for the next trace. Without an ambient occlusion pass this frame (none in the game, or the
// game's SSAO off) the frame's first traces (its G-buffer depth and camera), and each then reads its AO times the
// traced ambient visibility.
constexpr auto OnSunDraw = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!internal::active.load(std::memory_order_relaxed)) return {};
  const auto& draw = command.template GetCallbackData<light_bounce::SunDraw>();
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !frame_history::MainViewport(native)) return {};
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (runtime.settings.shadows_only && runtime.sun_frame != runtime.frame) {
    std::array<ID3D11ShaderResourceView*, 16> views = {};
    native->PSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    uint64_t map = 0;
    for (auto* view : views) {
      if (view == nullptr) continue;
      ComPtr<ID3D11Resource> resource;
      view->GetResource(resource.GetAddressOf());
      view->Release();
      ComPtr<ID3D11Texture2D> texture;
      D3D11_TEXTURE2D_DESC desc = {};
      if (FAILED(resource.As(&texture))) continue;
      texture->GetDesc(&desc);
      // The host's per-frame map where it names one (Pharaoh: never its kept baked map, a square R16 too).
      const bool per_frame_map = ((internal::host.cascade_target != nullptr)
                                      ? internal::host.cascade_target(desc)
                                      : (desc.Width == desc.Height && desc.Width >= 1024
                                         && (desc.Format == DXGI_FORMAT_R32_TYPELESS || desc.Format == DXGI_FORMAT_R24G8_TYPELESS
                                             || desc.Format == DXGI_FORMAT_R16_TYPELESS)));
      if (per_frame_map) {
        map = reinterpret_cast<uintptr_t>(resource.Get());
      }
    }
    internal::shadow_map.store(map, std::memory_order_relaxed);
  }
  if (runtime.ambient_frame != runtime.frame) {
    auto& objects = runtime.objects;
    if (runtime.traced_frame != runtime.frame) {
      ComPtr<ID3D11ShaderResourceView> depth;
      ComPtr<ID3D11Buffer> camera;
      native->PSGetShaderResources(draw.gbuffer_slot + 3, 1, depth.GetAddressOf());
      native->PSGetConstantBuffers(draw.camera_slot, 1, camera.GetAddressOf());
      internal::Trace(native, depth.Get(), camera.Get());
    }
    ComPtr<ID3D11ShaderResourceView> game_ao;
    if (runtime.settings.ambient_occlusion && runtime.traced_frame == runtime.frame && draw.ao_slot != UINT_MAX) {
      native->PSGetShaderResources(draw.ao_slot, 1, game_ao.GetAddressOf());
    }
    if (game_ao && runtime.combined_frame != runtime.frame) {
      D3D11_TEXTURE2D_DESC current = {};
      if (objects.combined) objects.combined->GetDesc(&current);
      if (current.Width != dlaa_bridge::bridge.width || current.Height != dlaa_bridge::bridge.height) {
        objects.combined.Reset();
        objects.combined_srv.Reset();
        objects.combined_uav.Reset();
        const D3D11_TEXTURE2D_DESC desc = {
            .Width = dlaa_bridge::bridge.width,
            .Height = dlaa_bridge::bridge.height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
            .SampleDesc = {.Count = 1},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
        };
        if (FAILED(objects.device->CreateTexture2D(&desc, nullptr, objects.combined.GetAddressOf()))
            || FAILED(objects.device->CreateShaderResourceView(objects.combined.Get(), nullptr, objects.combined_srv.GetAddressOf()))
            || FAILED(objects.device->CreateUnorderedAccessView(objects.combined.Get(), nullptr, objects.combined_uav.GetAddressOf()))) {
          objects.combined.Reset();
          objects.combined_srv.Reset();
          objects.combined_uav.Reset();
        }
      }
      if (objects.combined) {
        const internal::ComputeState kept(native);
        ComPtr<ID3D11SamplerState> game_sampler;
        native->CSGetSamplers(0, 1, game_sampler.GetAddressOf());
        native->CSSetSamplers(0, 1, objects.linear_clamp.GetAddressOf());
        ComPtr<ID3D11Buffer> game_second;
        if (draw.ssao_constants_slot != UINT_MAX) {
          ComPtr<ID3D11Buffer> sun_mapping;
          native->PSGetConstantBuffers(draw.ssao_constants_slot, 1, sun_mapping.GetAddressOf());
          native->CSGetConstantBuffers(1, 1, game_second.GetAddressOf());
          native->CSSetConstantBuffers(1, 1, sun_mapping.GetAddressOf());
        }
        const std::array<uint32_t, 4> size = {runtime.traced_size[0], runtime.traced_size[1], runtime.settings.ambient_alone ? 1u : 0u, 0u};
        internal::Dispatch(native, objects.combine_shader.Get(), {game_ao.Get(), dlaa_bridge::bridge.rt_ambient.srv.Get()},
                           {objects.combined_uav.Get(), nullptr}, size.data(), sizeof(size), (dlaa_bridge::bridge.width + 7) / 8,
                           (dlaa_bridge::bridge.height + 7) / 8);
        if (draw.ssao_constants_slot != UINT_MAX) native->CSSetConstantBuffers(1, 1, game_second.GetAddressOf());
        native->CSSetSamplers(0, 1, game_sampler.GetAddressOf());
        runtime.combined_frame = runtime.frame;
        runtime.game_ao = game_ao.Get();
        if (const std::unique_lock probe_lock(ambient_probe.mutex); ambient_probe.wanted) {
          constexpr int RADIUS = 2;
          const int traced_width = static_cast<int>(runtime.traced_size[0]);
          const int traced_height = static_cast<int>(runtime.traced_size[1]);
          const int pixel_x = std::min(static_cast<int>(ambient_probe.position[0] * static_cast<float>(traced_width)), traced_width - 1);
          const int pixel_y = std::min(static_cast<int>(ambient_probe.position[1] * static_cast<float>(traced_height)), traced_height - 1);
          const int left = std::max(pixel_x - RADIUS, 0);
          const int top = std::max(pixel_y - RADIUS, 0);
          const int right = std::min(pixel_x + RADIUS + 1, traced_width);
          const int bottom = std::min(pixel_y + RADIUS + 1, traced_height);
          // The texels' values as floats, row after row; empty if the copy failed.
          const auto read = [&](ID3D11Texture2D* texture, const D3D11_BOX& box) {
            std::vector<std::array<float, 4>> texels;
            if (texture == nullptr) return texels;
            D3D11_TEXTURE2D_DESC desc = {};
            texture->GetDesc(&desc);
            const D3D11_TEXTURE2D_DESC staging_desc = {
                .Width = box.right - box.left,
                .Height = box.bottom - box.top,
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = desc.Format,
                .SampleDesc = {.Count = 1},
                .Usage = D3D11_USAGE_STAGING,
                .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
            };
            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(objects.device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()))) return texels;
            native->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture, 0, &box);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(native->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return texels;
            for (UINT row = 0; row < staging_desc.Height; ++row) {
              const auto* bytes = static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(row) * mapped.RowPitch);
              for (UINT column = 0; column < staging_desc.Width; ++column) {
                std::array<float, 4> texel = {};
                if (desc.Format == DXGI_FORMAT_R8_UNORM) {
                  texel[0] = static_cast<float>(bytes[column]) / 255.f;
                } else if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                  std::array<uint16_t, 4> halves = {};
                  std::memcpy(halves.data(), bytes + (static_cast<size_t>(column) * sizeof(halves)), sizeof(halves));
                  for (size_t channel = 0; channel < texel.size(); ++channel) texel[channel] = renodx::utils::float16::Decode(halves[channel]);
                } else {
                  std::memcpy(texel.data(), bytes + (static_cast<size_t>(column) * sizeof(texel)), sizeof(texel));
                }
                texels.push_back(texel);
              }
            }
            native->Unmap(staging.Get(), 0);
            return texels;
          };
          const D3D11_BOX box = {.left = static_cast<UINT>(left), .top = static_cast<UINT>(top), .front = 0, .right = static_cast<UINT>(right), .bottom = static_cast<UINT>(bottom), .back = 1};
          const auto ambient_texels = read(dlaa_bridge::bridge.rt_ambient.texture.Get(), box);
          const auto combined_texels = read(objects.combined.Get(), box);
          const auto position_texels = read(dlaa_bridge::bridge.rt_positions.texture.Get(), box);
          const auto camera_texels = read(dlaa_bridge::bridge.rt_sun.texture.Get(), {.left = 1, .top = 0, .front = 0, .right = 2, .bottom = 1, .back = 1});
          const int columns = right - left;
          std::string& text = ambient_probe.text;
          text = std::format("Rays' AO at the cross: render pixel {},{} of {}x{}, range {:.2f} m (x the game's scale in the request), {} rays a frame, quarter rate {}, in place of the game's AO {}, flags 0x{:X}.\n",
                             pixel_x, pixel_y, traced_width, traced_height, runtime.settings.ambient_range, runtime.settings.ambient_rays,
                             runtime.settings.ambient_quarter, runtime.settings.ambient_alone, dlaa_bridge::bridge.shared != nullptr ? dlaa_bridge::bridge.shared->rt_flags : 0u);
          text += "Each block: the texels around it, the pixel in the middle (fewer at a screen edge). AO 1 is open, 0 black: every ray hit within the range.\n";
          const auto block = [&](const char* title, const std::vector<std::array<float, 4>>& texels, size_t channel) {
            text += std::format("{}:\n", title);
            for (size_t index = 0; index < texels.size(); ++index) {
              text += std::format("{:10.4f}{}", texels[index][channel], (index + 1) % static_cast<size_t>(columns) == 0 ? "\n" : " ");
            }
          };
          block("AO the rays left", ambient_texels, 0);
          block("AO the sun draws read", combined_texels, 0);
          block("View depth, metres (0: sky)", position_texels, 3);
          if (position_texels.size() == static_cast<size_t>(columns * (bottom - top)) && camera_texels.size() == 1) {
            const auto position_at = [&](int x, int y) {
              return position_texels[static_cast<size_t>(((std::clamp(y, 0, traced_height - 1) - top) * columns) + std::clamp(x, 0, traced_width - 1) - left)];
            };
            const auto& position = position_at(pixel_x, pixel_y);
            // As the helper's SurfaceNormal (rt_scene.hlsli), from the same positions.
            const auto& to_left = position_at(pixel_x - 1, pixel_y);
            const auto& to_right = position_at(pixel_x + 1, pixel_y);
            const auto& above = position_at(pixel_x, pixel_y - 1);
            const auto& below = position_at(pixel_x, pixel_y + 1);
            const auto difference = [](const std::array<float, 4>& a, const std::array<float, 4>& b) {
              return std::array<float, 3>{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
            };
            const bool use_right = pixel_x == 0 || (pixel_x != traced_width - 1 && std::abs(to_right[3] - position[3]) < std::abs(to_left[3] - position[3]));
            const bool use_below = pixel_y == 0 || (pixel_y != traced_height - 1 && std::abs(below[3] - position[3]) < std::abs(above[3] - position[3]));
            const auto along_x = use_right ? difference(to_right, position) : difference(position, to_left);
            const auto along_y = use_below ? difference(below, position) : difference(position, above);
            std::array<float, 3> normal = {(along_y[1] * along_x[2]) - (along_y[2] * along_x[1]),
                                           (along_y[2] * along_x[0]) - (along_y[0] * along_x[2]),
                                           (along_y[0] * along_x[1]) - (along_y[1] * along_x[0])};
            const float normal_length = std::sqrt((normal[0] * normal[0]) + (normal[1] * normal[1]) + (normal[2] * normal[2]));
            const auto to_camera = difference(camera_texels[0], position);
            const float facing = (normal[0] * to_camera[0]) + (normal[1] * to_camera[1]) + (normal[2] * to_camera[2]);
            const float sign_to_camera = (facing < 0.f ? -1.f : 1.f);
            const float length = (normal_length > 0.f ? normal_length : 1.f);
            text += std::format("World position {:.3f}, {:.3f}, {:.3f}, camera {:.3f}, {:.3f}, {:.3f}.\n", position[0], position[1], position[2],
                                camera_texels[0][0], camera_texels[0][1], camera_texels[0][2]);
            text += std::format("Normal the rays start from {:.3f}, {:.3f}, {:.3f} (y up{}), steps to its neighbours {:.4f} and {:.4f} m.\n",
                                normal[0] * sign_to_camera / length, normal[1] * sign_to_camera / length, normal[2] * sign_to_camera / length,
                                normal_length > 0.f ? "" : "; none: the rays use the way to the camera",
                                std::sqrt((along_x[0] * along_x[0]) + (along_x[1] * along_x[1]) + (along_x[2] * along_x[2])),
                                std::sqrt((along_y[0] * along_y[0]) + (along_y[1] * along_y[1]) + (along_y[2] * along_y[2])));
          }
        }
      }
    }
    if (game_ao && game_ao.Get() == runtime.game_ao && runtime.combined_frame == runtime.frame) {
      native->PSSetShaderResources(draw.ao_slot, 1, objects.combined_srv.GetAddressOf());
    }
  }
  // The paths, the reflections' rays and the fog's read it too (the sun on what they meet, their sky), also with the game's shadows.
  if ((!runtime.settings.sun_shadows && !runtime.settings.path_tracing && !runtime.settings.reflections && !runtime.settings.fog_shadows) || runtime.sun_frame == runtime.frame
      || !dlaa_bridge::bridge.ray_tracing
      || !runtime.objects.constants) {
    return {};
  }
  ComPtr<ID3D11Buffer> lighting;
  native->PSGetConstantBuffers(draw.lighting_slot, 1, lighting.GetAddressOf());
  if (!lighting) return {};
  const internal::ComputeState kept(native);
  ComPtr<ID3D11Buffer> game_second;
  native->CSGetConstantBuffers(1, 1, game_second.GetAddressOf());
  const bool probe_sky = runtime.settings.path_ambient && runtime.settings.path_probe_sky && draw.environment_slot != UINT_MAX;
  const std::array<uint32_t, 8> offset = {draw.sun_direction, internal::ambient_cube, runtime.settings.path_ambient ? 1u : 0u, probe_sky ? 1u : 0u,
                                          internal::sun_colour, 0u, 0u, 0u};
  native->UpdateSubresource(runtime.objects.sun_constants.Get(), 0, nullptr, offset.data(), 0, 0);
  native->CSSetShader(runtime.objects.sun_shader.Get(), nullptr, 0);
  ID3D11UnorderedAccessView* sun = dlaa_bridge::bridge.rt_sun.uav.Get();
  constexpr UINT KEEP_COUNT = UINT_MAX;
  native->CSSetUnorderedAccessViews(0, 1, &sun, &KEEP_COUNT);
  ID3D11Buffer* constants[] = {lighting.Get(), runtime.objects.sun_constants.Get()};
  native->CSSetConstantBuffers(0, 2, constants);
  ComPtr<ID3D11Buffer> game_third;
  if (draw.sky_harmonics_slot != UINT_MAX) {
    ComPtr<ID3D11ShaderResourceView> harmonics;
    native->PSGetShaderResources(draw.sky_harmonics_slot, 1, harmonics.GetAddressOf());
    native->CSSetShaderResources(0, 1, harmonics.GetAddressOf());
    ComPtr<ID3D11Buffer> camera;
    native->PSGetConstantBuffers(draw.camera_slot, 1, camera.GetAddressOf());
    native->CSGetConstantBuffers(2, 1, game_third.GetAddressOf());
    native->CSSetConstantBuffers(2, 1, camera.GetAddressOf());
  }
  // The environment maps at t1 (ComputeState puts the game's back), their weights at b3, their sampler at s0.
  ComPtr<ID3D11Buffer> game_fourth;
  ComPtr<ID3D11SamplerState> game_sampler;
  if (probe_sky) {
    ComPtr<ID3D11ShaderResourceView> environment_maps;
    native->PSGetShaderResources(draw.environment_slot, 1, environment_maps.GetAddressOf());
    native->CSSetShaderResources(1, 1, environment_maps.GetAddressOf());
    ComPtr<ID3D11Buffer> environment;
    native->PSGetConstantBuffers(draw.environment_slot, 1, environment.GetAddressOf());
    native->CSGetConstantBuffers(3, 1, game_fourth.GetAddressOf());
    native->CSSetConstantBuffers(3, 1, environment.GetAddressOf());
    ComPtr<ID3D11SamplerState> sampler;
    native->PSGetSamplers(draw.environment_slot, 1, sampler.GetAddressOf());
    native->CSGetSamplers(0, 1, game_sampler.GetAddressOf());
    native->CSSetSamplers(0, 1, sampler.GetAddressOf());
  }
  native->Dispatch(1, 1, 1);
  native->CSSetConstantBuffers(1, 1, game_second.GetAddressOf());
  if (draw.sky_harmonics_slot != UINT_MAX) native->CSSetConstantBuffers(2, 1, game_third.GetAddressOf());
  if (probe_sky) {
    native->CSSetConstantBuffers(3, 1, game_fourth.GetAddressOf());
    native->CSSetSamplers(0, 1, game_sampler.GetAddressOf());
  }
  runtime.sun_frame = runtime.frame;
  return {};
};

// Path-Traced Ambient, at each sun light draw after OnSunDraw (its rt_sun read the game's ambient cube first: the
// paths' sky; the paths bring the occluded sky's light instead). A sun shader whose diffuse ambient screen_space_shadows
// patched: t51 a texel of 1 (its diffuse ambient off, the foliage's sky sheen from the cube kept; ATN's debug32: zeroing
// the cube took that sheen too, the far ground came out darker and warmer), its foliage ones also t52 the sun texture
// (their share of the bounce: light_bounce_apply); both unbound after it. Else the draw's lighting constants without the
// game's ambient cube (the copy made once a frame), the game's put back after it. Only while the paths run (a frame
// without them keeps the game's ambient). Put back in finalize callbacks: the Light Bounce draws the sun draw itself
// (bypass), which skips post callbacks (ATN's debug33: t51 left bound took the ambient away under Rays and Off).
constexpr auto OnPathAmbientDraw = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!internal::active.load(std::memory_order_relaxed)) return {};
  const auto& draw = command.template GetCallbackData<light_bounce::SunDraw>();
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !frame_history::MainViewport(native)) return {};
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.path_ambient_on || runtime.bounced_frame + 1 < runtime.frame) return {};
  auto& objects = runtime.objects;
  if (bool foliage = false; screen_space_shadows::AmbientPatched(draw.shader_hash, &foliage)) {
    if (!objects.ambient_off) {
      constexpr float ONE = 1.f;
      const D3D11_TEXTURE2D_DESC one = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                        .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = &ONE, .SysMemPitch = 4};
      ComPtr<ID3D11Texture2D> texture;
      if (FAILED(objects.device->CreateTexture2D(&one, &data, texture.GetAddressOf()))
          || FAILED(objects.device->CreateShaderResourceView(texture.Get(), nullptr, objects.ambient_off.GetAddressOf()))) {
        return {};
      }
    }
    std::array<ID3D11ShaderResourceView*, 2> views = {objects.ambient_off.Get(), foliage ? dlaa_bridge::bridge.rt_sun.srv.Get() : nullptr};
    native->PSSetShaderResources(screen_space_shadows::internal::AMBIENT_SLOT, static_cast<UINT>(views.size()), views.data());
    return {.finalize_callback = [](Context& post, const void*) {
      auto* post_native = internal::NativeContext(post.cmd_list);
      if (post_native == nullptr) return;
      const std::array<ID3D11ShaderResourceView*, 2> none = {};
      post_native->PSSetShaderResources(screen_space_shadows::internal::AMBIENT_SLOT, static_cast<UINT>(none.size()), none.data());
    }};
  }
  ComPtr<ID3D11Buffer> game_lighting;
  native->PSGetConstantBuffers(draw.lighting_slot, 1, game_lighting.GetAddressOf());
  if (!game_lighting) return {};
  D3D11_BUFFER_DESC desc = {};
  game_lighting->GetDesc(&desc);
  constexpr UINT CUBE_BYTES = 96;  // six float3 registers
  const UINT cube_offset = internal::ambient_cube * 4;
  if (desc.ByteWidth < cube_offset + CUBE_BYTES) return {};
  if (objects.ambient_lighting_bytes != desc.ByteWidth) {
    objects.ambient_lighting.Reset();
    objects.ambient_lighting_bytes = 0;
    const D3D11_BUFFER_DESC copy = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(objects.device->CreateBuffer(&copy, nullptr, objects.ambient_lighting.GetAddressOf()))) return {};
    objects.ambient_lighting_bytes = desc.ByteWidth;
  }
  if (!objects.no_ambient) {
    constexpr std::array<float, CUBE_BYTES / 4> ZEROS = {};
    const D3D11_BUFFER_DESC zeros = {.ByteWidth = CUBE_BYTES, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = ZEROS.data()};
    if (FAILED(objects.device->CreateBuffer(&zeros, &data, objects.no_ambient.GetAddressOf()))) return {};
  }
  if (runtime.ambient_off_frame != runtime.frame || runtime.ambient_off_source != game_lighting.Get()) {
    native->CopyResource(objects.ambient_lighting.Get(), game_lighting.Get());
    const D3D11_BOX cube = {.left = 0, .top = 0, .front = 0, .right = CUBE_BYTES, .bottom = 1, .back = 1};
    native->CopySubresourceRegion(objects.ambient_lighting.Get(), 0, cube_offset, 0, 0, objects.no_ambient.Get(), 0, &cube);
    runtime.ambient_off_frame = runtime.frame;
    runtime.ambient_off_source = game_lighting.Get();
  }
  native->PSSetConstantBuffers(draw.lighting_slot, 1, objects.ambient_lighting.GetAddressOf());
  runtime.ambient_game_lighting = std::move(game_lighting);
  runtime.ambient_slot = draw.lighting_slot;
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = internal::NativeContext(post.cmd_list);
    auto& post_runtime = internal::runtime;
    const std::unique_lock post_lock(post_runtime.mutex);
    if (post_native != nullptr) {
      post_native->PSSetConstantBuffers(post_runtime.ambient_slot, 1, post_runtime.ambient_game_lighting.GetAddressOf());
    }
    post_runtime.ambient_game_lighting.Reset();
  }};
};

namespace internal {

// The light draws' instance data as the game's input layouts give it: their POSITION elements past the mesh's (the light
// shaders' inputs), by input layout; InstallPointLights' LightInstances says which hold a light's colour and position.
struct InputPosition {
  uint32_t semantic_index = 0;
  uint32_t slot = 0;
  uint32_t offset = 0;
};
// A light draw's: its instances' buffer slot, the colour's and the position's byte offsets in each.
struct PointLightLayout {
  uint32_t slot = 0;
  uint32_t colour = 0;
  uint32_t position = 0;
};
static std::mutex point_light_mutex;
static std::unordered_map<uint64_t, std::vector<InputPosition>> point_light_layouts;
static bool point_lights_installed = false;
static bool point_lights_read = false;
static bool point_light_layout_missed = false;
static bool shadow_readers_installed = false;

// The light draws' instances copied for the CPU into a ring of staging buffers, read when the ring comes round (three
// frames on: the GPU done with them) into the lights the next trace picks from.
constexpr UINT POINT_LIGHT_BYTES = 512 * 1024;
struct PointLightCopy {
  UINT offset = 0;  // bytes into the staging buffer
  UINT count = 0;
  UINT stride = 0;
  PointLightLayout layout;
  bool shadows_only = false;  // LightInstances'
  bool streamed = false;      // LightInstances::stream_shader's records: an undrawn instance's reach 0, left out
};
// A streamed light record (LightInstances::stream_shader): the light (position, reach), then its colour.
constexpr UINT STREAMED_LIGHT_BYTES = 32;
struct PointLightReadback {
  ComPtr<ID3D11Device> device;
  std::array<ComPtr<ID3D11Buffer>, 3> staging;
  std::array<std::vector<PointLightCopy>, 3> copies;
  std::array<UINT, 3> used = {};
  uint64_t frame = UINT64_MAX;  // the frame the current buffer fills in
  size_t current = 0;
  // LightInstances::stream_shader's (made at its first draw on the device; one family streamed).
  ComPtr<ID3D11GeometryShader> stream_output;
  ComPtr<ID3D11Buffer> streamed;
  bool stream_failed = false;
};
static PointLightReadback point_light_readback;

// InstallPointLights' light source where a light draw holds its one light in a pixel shader constant buffer (Rome II's
// point_light_PS: position c0, colour c1.xyz, reach c1.w) instead of instances: the buffers seen bound at that slot at the
// light draws (the oldest given up for a new one), each with its last upload's first two rows, read on the CPU as the
// game writes them (Map / Unmap, UpdateSubresource).
constexpr UINT NO_LIGHT_CONSTANTS = UINT_MAX;
static UINT light_constants_slot = NO_LIGHT_CONSTANTS;
struct LightUpload {
  std::atomic<ID3D11Buffer*> buffer = nullptr;
  std::array<float, 8> rows = {};  // c0, c1 of its last upload
  bool valid = false;              // `rows` are its last upload
};
static std::mutex light_upload_mutex;  // rows, valid, light_upload_next
static std::array<LightUpload, 4> light_uploads;
static size_t light_upload_next = 0;
// Set while the rays trace the local lights (by the light draws): else every buffer event returns on this alone.
static std::atomic_bool light_uploads_watched = false;
// The watched buffer this thread mapped for writing, and where: copied at its unmap.
static thread_local LightUpload* mapped_light_upload = nullptr;
static thread_local const std::byte* mapped_light_data = nullptr;

inline LightUpload* FindLightUpload(ID3D11Buffer* buffer) {
  const auto found = std::ranges::find_if(light_uploads, [buffer](const LightUpload& upload) {
    return upload.buffer.load(std::memory_order_relaxed) == buffer;
  });
  return (found == light_uploads.end()) ? nullptr : &*found;
}

inline void OnMapLightConstants(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t,
                                reshade::api::map_access access, void** data) {
  if (!light_uploads_watched.load(std::memory_order_relaxed) || access == reshade::api::map_access::read_only) return;
  auto* upload = FindLightUpload(reinterpret_cast<ID3D11Buffer*>(resource.handle));
  if (upload == nullptr) return;
  {
    const std::unique_lock lock(light_upload_mutex);
    upload->valid = false;
  }
  if (offset == 0 && data != nullptr && *data != nullptr) {
    mapped_light_upload = upload;
    mapped_light_data = static_cast<const std::byte*>(*data);
  }
}

// Before the native Unmap, while the pointer is still valid.
inline void OnUnmapLightConstants(reshade::api::device*, reshade::api::resource resource) {
  if (mapped_light_upload == nullptr || mapped_light_upload->buffer.load(std::memory_order_relaxed) != reinterpret_cast<ID3D11Buffer*>(resource.handle)) {
    return;
  }
  {
    const std::unique_lock lock(light_upload_mutex);
    std::memcpy(mapped_light_upload->rows.data(), mapped_light_data, sizeof(mapped_light_upload->rows));
    mapped_light_upload->valid = true;
  }
  mapped_light_upload = nullptr;
  mapped_light_data = nullptr;
}

// UpdateSubresource (ReShade reports the immediate context's only): kept where it writes both rows.
inline bool OnUpdateLightConstants(reshade::api::device*, const void* data, reshade::api::resource resource, uint64_t offset,
                                   uint64_t size) {
  if (!light_uploads_watched.load(std::memory_order_relaxed)) return false;
  auto* upload = FindLightUpload(reinterpret_cast<ID3D11Buffer*>(resource.handle));
  if (upload == nullptr) return false;
  const std::unique_lock lock(light_upload_mutex);
  upload->valid = data != nullptr && offset == 0 && size >= sizeof(upload->rows);
  if (upload->valid) {
    std::memcpy(upload->rows.data(), data, sizeof(upload->rows));
  }
  return false;
}

// The game's device is going away: its readback buffers with it, and the light buffers watched.
inline void OnDestroyPointLightDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  if (point_light_readback.device.Get() == reinterpret_cast<ID3D11Device*>(device->get_native())) {
    point_light_readback = {};
  }
  const std::unique_lock lock(light_upload_mutex);
  for (auto& upload : light_uploads) {
    upload.buffer.store(nullptr, std::memory_order_relaxed);
    upload.valid = false;
  }
}

inline void OnInitInputLayout(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                              const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline pipeline) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  for (const auto& subobject : std::span(subobjects, subobject_count)) {
    if (subobject.type != reshade::api::pipeline_subobject_type::input_layout) continue;
    std::vector<InputPosition> positions;
    for (const auto& element : std::span(static_cast<const reshade::api::input_element*>(subobject.data), subobject.count)) {
      if (element.semantic != nullptr && std::string_view(element.semantic) == "POSITION" && element.semantic_index != 0) {
        positions.push_back({.semantic_index = element.semantic_index, .slot = element.buffer_binding, .offset = element.offset});
      }
    }
    if (positions.size() < 2) continue;
    const std::unique_lock lock(point_light_mutex);
    point_light_layouts[pipeline.handle] = std::move(positions);
  }
}

inline void OnDestroyInputLayout(reshade::api::device*, reshade::api::pipeline pipeline) {
  const std::unique_lock lock(point_light_mutex);
  point_light_layouts.erase(pipeline.handle);
}

}  // namespace internal

// Where a light draw's instances keep each light: the POSITION semantic indices of its colour (16 bytes) and of its world
// position with its reach after it (16 bytes). `shadows_only`: LocalLight's (Warhammer II's spot lights: POSITION4, then
// POSITION1 with POSITION2.x its range). `stream_shader`: lights the vertex shader makes from a particle list, not from
// instance buffers (Troy's fires, particle lights 643FC156): a geometry shader taking that vertex shader's outputs, one
// point an instance, and streaming out each light's record (STREAMED_LIGHT_BYTES: position and reach, then colour; reach 0
// where the instance is not drawn); for the rays alone (the screen-space torch shadows take instance buffers). Empty: the
// instances' semantics.
struct LightInstances {
  uint32_t colour = 1;
  uint32_t position = 2;
  bool shadows_only = false;
  std::span<const uint8_t> stream_shader = {};
};
// Attila's and the other games' point lights.
inline constexpr LightInstances POINT_LIGHTS = {};

// A point light draw (the addon's replaced light shaders with rt_local_lights.hlsli): its instances copied for the next
// traces; this frame's traced lights' bits and positions bound (put back after). Without rays tracing them, the
// screen-space shadows' Torch and Fire Shadows' (screen_space_lights.hpp) where on.
constexpr auto OnPointLightDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  bool traced = internal::active.load(std::memory_order_relaxed);
  uint64_t frame = 0;
  if (traced) {
    const std::unique_lock lock(internal::runtime.mutex);
    traced = internal::runtime.settings.local_lights;
    frame = internal::runtime.frame;
  }
  // The draw's one light in a pixel shader constant buffer (InstallPointLights' light_constants): its last upload (this
  // draw's) for the next trace, the buffer watched from now (its first light missed). Not traced: nothing watched, what
  // was kept stale.
  if (internal::light_constants_slot != internal::NO_LIGHT_CONSTANTS) {
    if (!traced) {
      if (internal::light_uploads_watched.exchange(false, std::memory_order_relaxed)) {
        const std::unique_lock lock(internal::light_upload_mutex);
        for (auto& upload : internal::light_uploads) upload.valid = false;
      }
      return {};
    }
    auto* native = internal::NativeContext(command.cmd_list);
    if (native == nullptr) return {};
    ComPtr<ID3D11Buffer> constants;
    native->PSGetConstantBuffers(internal::light_constants_slot, 1, constants.GetAddressOf());
    D3D11_BUFFER_DESC desc = {};
    if (constants) constants->GetDesc(&desc);
    std::array<float, 8> rows = {};
    bool uploaded = false;
    if (desc.ByteWidth >= sizeof(rows)) {
      const std::unique_lock lock(internal::light_upload_mutex);
      if (auto* upload = internal::FindLightUpload(constants.Get()); upload == nullptr) {
        auto& replaced = internal::light_uploads[internal::light_upload_next];
        internal::light_upload_next = (internal::light_upload_next + 1) % internal::light_uploads.size();
        replaced.valid = false;
        replaced.buffer.store(constants.Get(), std::memory_order_relaxed);
      } else if (upload->valid) {
        rows = upload->rows;
        uploaded = true;
      }
    }
    internal::light_uploads_watched.store(true, std::memory_order_relaxed);
    if (uploaded) {
      const float scale = internal::host.local_light_scale;
      AddLocalLight({.light = {rows[0], rows[1], rows[2], rows[7]}, .colour = {rows[4] * scale, rows[5] * scale, rows[6] * scale, 0.f}});
      if (!std::exchange(internal::point_lights_read, true)) {
        renodx::utils::log::i(std::format("[Total War] Local lights: the first read from a light draw's constants (b{}).", internal::light_constants_slot));
      }
    }
    if (!BindLocalLights(native)) return {};
    return {.post_callback = [](Context& post, const void*) {
      auto* post_native = internal::NativeContext(post.cmd_list);
      if (post_native != nullptr) UnbindLocalLights(post_native);
    }};
  }
  if (!traced && !screen_space_lights::Wanted()) return {};
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr) return {};
  auto& readback = internal::point_light_readback;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  // The rays' readback (the screen-space shadows' copies stay on the GPU).
  if (traced && readback.device != device) {
    readback = {.device = device};
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = internal::POINT_LIGHT_BYTES, .Usage = D3D11_USAGE_STAGING,
                                            .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    for (auto& buffer : readback.staging) device->CreateBuffer(&staging_desc, nullptr, buffer.GetAddressOf());
  }
  // A new frame: the buffer filled three frames ago read (each light once) into this frame's lights, then filled again.
  if (traced && readback.frame != frame) {
    readback.frame = frame;
    readback.current = (readback.current + 1) % readback.staging.size();
    auto& buffer = readback.staging[readback.current];
    auto& copies = readback.copies[readback.current];
    if (D3D11_MAPPED_SUBRESOURCE mapped = {}; !copies.empty() && SUCCEEDED(native->Map(buffer.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      std::vector<LocalLight> lights;
      for (const auto& copy : copies) {
        for (UINT index = 0; index < copy.count; ++index) {
          const auto* instance = static_cast<const std::byte*>(mapped.pData) + copy.offset + (index * copy.stride);
          LocalLight light = {.shadows_only = copy.shadows_only};
          std::memcpy(light.light.data(), instance + copy.layout.position, sizeof(light.light));
          if (copy.streamed && !(light.light[3] > 0.f)) continue;
          std::memcpy(light.colour.data(), instance + copy.layout.colour, sizeof(light.colour));
          for (size_t channel = 0; channel < 3; ++channel) light.colour[channel] *= internal::host.local_light_scale;
          lights.push_back(light);
        }
      }
      native->Unmap(buffer.Get(), 0);
      std::ranges::sort(lights, {}, &LocalLight::light);
      const auto repeated = std::ranges::unique(lights, {}, &LocalLight::light);
      lights.erase(repeated.begin(), repeated.end());
      if (!lights.empty() && !std::exchange(internal::point_lights_read, true)) {
        renodx::utils::log::i(std::format("[Total War] Local lights: first {} read from the light draws.", lights.size()));
      }
      AddLocalLights(lights);
    }
    copies.clear();
    readback.used[readback.current] = 0;
  }
  const auto& light_instances = command.template GetCallbackData<LightInstances>();
  // Lights a particle list makes (LightInstances::stream_shader): the draw again as points, one vertex an instance, its
  // vertex shader's outputs streamed out as light records into the ring, read as instances are; the game's geometry and pixel
  // stages, stream targets and topology put back.
  if (!light_instances.stream_shader.empty()) {
    if (!traced) return {};
    if constexpr (requires { command.arguments.first_index; }) {
      constexpr UINT RECORD = internal::STREAMED_LIGHT_BYTES;
      if (!readback.stream_output && !readback.stream_failed) {
        const std::array<D3D11_SO_DECLARATION_ENTRY, 2> entries = {{
            {.SemanticName = "POSITION", .SemanticIndex = 0, .ComponentCount = 4},
            {.SemanticName = "COLOR", .SemanticIndex = 0, .ComponentCount = 4},
        }};
        const D3D11_BUFFER_DESC streamed_desc = {.ByteWidth = internal::POINT_LIGHT_BYTES, .Usage = D3D11_USAGE_DEFAULT,
                                                 .BindFlags = D3D11_BIND_STREAM_OUTPUT};
        readback.stream_failed =
            FAILED(device->CreateBuffer(&streamed_desc, nullptr, readback.streamed.GetAddressOf()))
            || FAILED(device->CreateGeometryShaderWithStreamOutput(light_instances.stream_shader.data(), light_instances.stream_shader.size(), entries.data(),
                                                                   static_cast<UINT>(entries.size()), &RECORD, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                                   readback.stream_output.GetAddressOf()));
        renodx::utils::log::i(readback.stream_failed ? "[Total War] Local lights: no stream output for the particle lights: not read."
                                                     : "[Total War] Local lights: the particle lights streamed from their vertex shader.");
      }
      auto& buffer = readback.staging[readback.current];
      UINT& used = readback.used[readback.current];
      const UINT count = std::min(command.arguments.instance_count, (internal::POINT_LIGHT_BYTES - used) / RECORD);
      if (buffer && readback.stream_output && count != 0) {
        ComPtr<ID3D11GeometryShader> game_geometry_shader;
        ComPtr<ID3D11PixelShader> game_pixel_shader;
        ComPtr<ID3D11Buffer> game_output;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        native->GSGetShader(game_geometry_shader.GetAddressOf(), nullptr, nullptr);
        native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
        native->SOGetTargets(1, game_output.GetAddressOf());
        native->IAGetPrimitiveTopology(&topology);
        ID3D11Buffer* target = readback.streamed.Get();
        constexpr UINT START = 0;
        native->GSSetShader(readback.stream_output.Get(), nullptr, 0);
        native->PSSetShader(nullptr, nullptr, 0);
        native->SOSetTargets(1, &target, &START);
        native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        native->DrawIndexedInstanced(1, count, command.arguments.first_index, command.arguments.vertex_offset, command.arguments.first_instance);
        ID3D11Buffer* output = game_output.Get();
        constexpr UINT APPEND = UINT_MAX;
        native->SOSetTargets(1, &output, &APPEND);
        native->GSSetShader(game_geometry_shader.Get(), nullptr, 0);
        native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
        native->IASetPrimitiveTopology(topology);
        const D3D11_BOX box = {.left = 0, .top = 0, .front = 0, .right = count * RECORD, .bottom = 1, .back = 1};
        native->CopySubresourceRegion(buffer.Get(), 0, used, 0, 0, readback.streamed.Get(), 0, &box);
        readback.copies[readback.current].push_back({.offset = used, .count = count, .stride = RECORD, .layout = {.colour = 16},
                                                     .shadows_only = light_instances.shadows_only, .streamed = true});
        used += count * RECORD;
      }
    }
    if (!BindLocalLights(native)) return {};
    return {.post_callback = [](Context& post, const void*) {
      auto* post_native = internal::NativeContext(post.cmd_list);
      if (post_native != nullptr) UnbindLocalLights(post_native);
    }};
  }
  ComPtr<ID3D11InputLayout> input_layout;
  native->IAGetInputLayout(input_layout.GetAddressOf());
  internal::PointLightLayout layout;
  bool layout_known = false;
  {
    const std::unique_lock lock(internal::point_light_mutex);
    const auto found = internal::point_light_layouts.find(reinterpret_cast<uintptr_t>(input_layout.Get()));
    if (found != internal::point_light_layouts.end()) {
      const auto& positions = found->second;
      const auto colour = std::ranges::find(positions, light_instances.colour, &internal::InputPosition::semantic_index);
      const auto position = std::ranges::find(positions, light_instances.position, &internal::InputPosition::semantic_index);
      if (colour != positions.end() && position != positions.end() && colour->slot == position->slot) {
        layout = {.slot = position->slot, .colour = colour->offset, .position = position->offset};
        layout_known = true;
      }
    }
  }
  ComPtr<ID3D11Buffer> instances;
  UINT stride = 0;
  UINT offset = 0;
  // Without its layout the instances' place isn't known: the mesh's vertices would be read as lights.
  if (layout_known) {
    native->IAGetVertexBuffers(layout.slot, 1, instances.GetAddressOf(), &stride, &offset);
  } else if (!std::exchange(internal::point_light_layout_missed, true)) {
    renodx::utils::log::i(std::format("[Total War] Local lights: a light draw's input layout has no POSITION{} and POSITION{} in one buffer: not read.",
                                      light_instances.colour, light_instances.position));
  }
  UINT instance_count = 0;
  UINT first_instance = 0;
  if constexpr (requires { command.arguments.instance_count; }) {
    instance_count = command.arguments.instance_count;
    first_instance = command.arguments.first_instance;
  }
  const bool readable = instances && std::max(layout.colour, layout.position) + 16 <= stride;
  const UINT first = offset + (first_instance * stride);
  if (traced) {
    auto& buffer = readback.staging[readback.current];
    UINT& used = readback.used[readback.current];
    const UINT count = (stride == 0) ? 0 : std::min(instance_count, (internal::POINT_LIGHT_BYTES - used) / stride);
    if (readable && buffer && count != 0) {
      const D3D11_BOX box = {.left = first, .top = 0, .front = 0, .right = first + (count * stride), .bottom = 1, .back = 1};
      native->CopySubresourceRegion(buffer.Get(), 0, used, 0, 0, instances.Get(), 0, &box);
      readback.copies[readback.current].push_back(
          {.offset = used, .count = count, .stride = stride, .layout = layout, .shadows_only = light_instances.shadows_only});
      used += count * stride;
    }
    if (!BindLocalLights(native)) return {};
  } else if (!screen_space_lights::OnLightDraw(native, {.buffer = (readable ? instances.Get() : nullptr),
                                                         .first = first,
                                                         .count = instance_count,
                                                         .stride = stride,
                                                         .position = layout.position,
                                                         .colour = layout.colour})) {
    return {};
  }
  return {.post_callback = [](Context& post, const void*) {
    auto* post_native = internal::NativeContext(post.cmd_list);
    if (post_native != nullptr) UnbindLocalLights(post_native);
  }};
};

// Attila's and Thrones' point light shaders without the game's point shadow maps (shaders/pointlight00-07: smooth and
// linear falloff, standard and skin lighting, high and low detail).
constexpr std::array<uint32_t, 8> POINT_LIGHT_SHADERS = {0x6426B042, 0x90981DA1, 0x89FF812E, 0xFD3BF1A8,
                                                         0x593989DE, 0x0D7A1213, 0xB8F368CF, 0xC9E4E52D};

// The point light draws (the addon's replaced pixel shaders reading rt_local_lights.hlsli), kept while installed: the
// game's input layouts tracked from now (before the device: install at load), Ray-Traced Point Light Shadows shown.
// `light_constants`: the pixel shader constant buffer slot holding each draw's one light (position c0, colour c1.xyz, reach
// c1.w; Rome II's point_light_PS b2), its uploads read instead of instances; the screen-space torch shadows, which take a
// draw's instances, stay hidden there. `instances`: where the draws' instances keep each light (static storage). A second
// call (another light family with instances of its own layout: Warhammer II's spot lights) adds its draws alone.
inline void InstallPointLights(std::span<const uint32_t> shader_hashes, UINT light_constants = internal::NO_LIGHT_CONSTANTS,
                               const LightInstances* instances = &POINT_LIGHTS) {
  if (!std::exchange(internal::point_lights_installed, true)) {
    internal::light_constants_slot = light_constants;
    if (light_constants == internal::NO_LIGHT_CONSTANTS) {
      screen_space_lights::installed = true;
      reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitInputLayout);
      reshade::register_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyInputLayout);
    } else {
      reshade::register_event<reshade::addon_event::map_buffer_region>(internal::OnMapLightConstants);
      reshade::register_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapLightConstants);
      reshade::register_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateLightConstants);
    }
    reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyPointLightDevice);
  }
  for (const uint32_t shader_hash : shader_hashes) {
    renodx::utils::command_action::Register(
        OnPointLightDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, instances);
  }
}

// Attila's and Thrones' battle fog (shaders/battlefog00, shader model 3; the MSAA one left out: DLAA has none).
constexpr std::array<uint32_t, 1> FOG_SHADERS = {0xA6A0F887};

// A fog draw (the addon's replaced fog shaders with rt_fog.hlsli): this frame's fog shadows bound (put back after).
constexpr auto OnFogDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!internal::active.load(std::memory_order_relaxed)) return {};
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !BindFogShadows(native)) return {};
  return {.post_callback = [](Context& post, const void*) {
    auto* post_native = internal::NativeContext(post.cmd_list);
    if (post_native != nullptr) UnbindFogShadows(post_native);
  }};
};

// Attila's and Thrones' particles (particle_vfx_main: one vertex a particle, a geometry shader makes its quad) and the
// main pass's lit pixel shaders: ps_main_high_quality by the game's shadow mode (gather, fetch4, pcf, software, none: smoke
// and dust), ps_main (one hash in all: torch and fire smoke). Fire's glow is added, the overlay pass's.
constexpr uint32_t PARTICLE_VERTEX_SHADER = 0xA7F257B5;
constexpr std::array<SmokeVertexShader, 1> PARTICLE_VERTEX_SHADERS = {{{.hash = PARTICLE_VERTEX_SHADER}}};
constexpr std::array<SmokePixelShader, 6> LIT_PARTICLE_PIXEL_SHADERS = {
    {{.hash = 0xACE33D5D}, {.hash = 0xDF0F041A}, {.hash = 0x905429B6}, {.hash = 0x79A71E7B}, {.hash = 0xF9D9D3FB}, {.hash = 0xF5379F78}}};

// A particle draw (Smoke Shadows): its lit particles streamed out for the next trace or smoke_map.hpp.
constexpr auto OnParticleDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (internal::active.load(std::memory_order_relaxed) || internal::runtime.settings.stream_particles) internal::CaptureParticles(command);
  return {};
};

// The particle draws (one of `vertex_shaders`' with one of `pixel_shaders`), kept while installed (after Install, before
// the device: the vertex shaders' code is kept as it is made, by the scene's code hook, made here when the scene has none):
// Ray-Traced Smoke Shadows shown.
inline void InstallSmokeShadows(std::span<const SmokeVertexShader> vertex_shaders, std::span<const SmokePixelShader> pixel_shaders) {
  internal::smoke_shadows_installed = true;
  internal::smoke_vertex_shaders = vertex_shaders;
  internal::smoke_pixel_shaders = pixel_shaders;
  if (!internal::clip_captures) reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  for (const SmokeVertexShader& shader : vertex_shaders) {
    const uint32_t indirect = shader.indirect ? renodx::utils::command_action::COMMAND_TYPE_INDIRECT : 0u;
    renodx::utils::command_action::Register(
        OnParticleDraw, {.shader_hash = shader.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW | indirect});
  }
}

// The fog draws (the addon's replaced pixel shaders reading rt_fog.hlsli), kept while installed: Ray-Traced Fog Shadows
// shown.
inline void InstallFogShadows(std::span<const uint32_t> shader_hashes) {
  internal::fog_shadows_installed = true;
  for (const uint32_t shader_hash : shader_hashes) {
    renodx::utils::command_action::Register(
        OnFogDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

// Attila's forward passes reading the sun's shadow map (ReShade.log 2026-10-28: lit everywhere under Ray-Traced Shadows
// Only): two of the game's (patched: screen_space_shadows::PatchForwardShadows) and the particles' (shaders/vfx00, which
// reads t49 itself); with them the particles' other path (shaders/vfx01: no shadows, the traced lights at b9 alone).
// The campaign's sea too (waterplane ps30_campaign: high to ultra, low; ATN's debug37: lit in the rays' shadows).
constexpr std::array<uint32_t, 2> CAMPAIGN_SEA_SHADERS = {0x05A111E1, 0x450F6992};
constexpr std::array<uint32_t, 6> SHADOW_READER_SHADERS = {0xC9FBACCB, 0x2E9F5935, 0xACE33D5D, 0xF5379F78, CAMPAIGN_SEA_SHADERS[0], CAMPAIGN_SEA_SHADERS[1]};

// A forward pass's draw reading the sun's shadow map: under Ray-Traced Shadows Only (its map empty) the rays' occlusion
// at t49 (the patched sun draws', this frame's), else none (t49 reads 0: the game's shadow).
constexpr auto OnShadowReaderDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr) return {};
  ID3D11ShaderResourceView* occlusion = nullptr;
  ID3D11ShaderResourceView* tint = nullptr;  // Coloured Shadows' filter (t50)
  ID3D11Buffer* lights = nullptr;  // the particles' (vfx00): torches and fires light the smoke and dust near them
  if (internal::active.load(std::memory_order_relaxed)) {
    const std::unique_lock lock(internal::runtime.mutex);
    if (internal::runtime.settings.shadows_only && internal::runtime.occlusion_frame == internal::runtime.frame) {
      occlusion = internal::runtime.objects.occlusion_srv.Get();
      if (internal::runtime.settings.coloured_shadows) tint = dlaa_bridge::bridge.rt_tint.srv.Get();
    }
    if (internal::runtime.lights_traced && internal::runtime.traced_frame == internal::runtime.frame) {
      lights = internal::runtime.objects.light_constants.Get();
    }
  }
  native->PSSetShaderResources(49, 1, &occlusion);
  native->PSSetShaderResources(50, 1, &tint);
  native->PSSetConstantBuffers(9, 1, &lights);
  return {};
};

// The forward passes reading the sun's shadow map (`shader_hashes`), kept while installed: the rays' shadows under
// Ray-Traced Shadows Only.
inline void InstallShadowReaders(std::span<const uint32_t> shader_hashes) {
  internal::shadow_readers_installed = true;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    internal::runtime.reader_shaders.insert(shader_hashes.begin(), shader_hashes.end());
  }
  for (const uint32_t shader_hash : shader_hashes) {
    renodx::utils::command_action::Register(
        OnShadowReaderDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

// A shadow pass draw that alpha-tests its mesh: its diffuse map (t0) noted, the scene's meshes with it cut out as well (ATN's
// debug37: campaign palms and pines were solid cards, blocks of black shadow). Runs before OnCasterDraw: skipped casters too.
constexpr auto OnCutOutShadowDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !internal::active.load(std::memory_order_relaxed)) return {};
  ComPtr<ID3D11ShaderResourceView> view;
  native->PSGetShaderResources(0, 1, view.GetAddressOf());
  if (!view) return {};
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.cut_out_textures.insert(resource.Get());
  return {};
};

// The game's shadow pass pixel shader that alpha-tests its meshes, and the units' G-buffer pixel shaders that clip (sorted).
inline void InstallCutOuts(uint32_t shadow_shader_hash, std::span<const uint32_t> unit_pixel_shaders) {
  internal::cut_out_pixel_shaders = unit_pixel_shaders;
  renodx::utils::command_action::Register(
      OnCutOutShadowDraw, {.shader_hash = shadow_shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

// The G-buffer pixel shaders whose opacity is known (SceneShader::clip_cut_outs; sorted by pixel shader).
inline void InstallCutOutMasks(std::span<const PixelMask> masks) {
  internal::cut_out_masks = masks;
}

// Ray-Traced Shadows Only: whether the frame's draws into the sun's shadow map are skipped (OnBindTargets: the sun draws
// took the rays' shadows the frame before, its map known). Asked after Update (a present), the next frame's: the game's
// own work for those draws can stop then (Attila's soldier shadows, graphics_hooks Scales::unit_shadows).
inline bool CastersSkipped() {
  if (!internal::shadows_only_installed || internal::shadow_map.load(std::memory_order_relaxed) == 0) return false;
  const std::unique_lock lock(internal::runtime.mutex);
  return internal::runtime.settings.shadows_only && internal::runtime.occlusion_frame + 1 >= internal::runtime.frame;
}

// Ray-Traced Shadows Only: draws are skipped while the sun's shadow map is bound alone (depth only) for drawing.
inline void OnBindTargets(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view*,
                          reshade::api::resource_view dsv) {
  bool skip = false;
  const uint64_t map = internal::shadow_map.load(std::memory_order_relaxed);
  if (count == 0 && dsv.handle != 0 && map != 0 && cmd_list->get_device()->get_resource_from_view(dsv).handle == map) {
    skip = CastersSkipped();
  }
  internal::skipping_casters.store(skip, std::memory_order_relaxed);
}

// Under it, in the frame Update picks every 10 s: a draw whose pixel shader reads the sun's shadow map and is no patched
// sun draw (forward-lit: lit everywhere, the map has no casters) has its pixel shader noted for the report.
inline void NoteShadowReader(reshade::api::command_list* cmd_list) {
  auto& runtime = internal::runtime;
  if (runtime.readers_frame != runtime.frame) return;
  const uint64_t map = internal::shadow_map.load(std::memory_order_relaxed);
  auto* native = internal::NativeContext(cmd_list);
  if (map == 0 || native == nullptr) return;
  std::array<ID3D11ShaderResourceView*, 16> views = {};
  native->PSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  bool reads = false;
  for (auto* view : views) {
    if (view == nullptr) continue;
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    view->Release();
    reads = reads || reinterpret_cast<uintptr_t>(resource.Get()) == map;
  }
  if (!reads) return;
  const uint32_t shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
  const uint32_t vertex_shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.sun_draw_shaders.contains(shader) && !runtime.reader_shaders.contains(shader)) {
    runtime.shadow_readers.insert((uint64_t{shader} << 32) | vertex_shader);
  }
}

inline bool OnCasterDraw(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, uint32_t) {
  NoteShadowReader(cmd_list);
  return internal::skipping_casters.load(std::memory_order_relaxed);
}

inline bool OnCasterDrawIndexed(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  NoteShadowReader(cmd_list);
  return internal::skipping_casters.load(std::memory_order_relaxed);
}

inline bool OnCasterDrawIndirect(reshade::api::command_list*, reshade::api::indirect_command, reshade::api::resource, uint64_t, uint32_t,
                                 uint32_t) {
  return internal::skipping_casters.load(std::memory_order_relaxed);
}

// The sun's shadow map's casters skipped under Ray-Traced Shadows Only (shown once installed; needs InstallSunDraws).
inline void InstallShadowsOnly() {
  internal::shadows_only_installed = true;
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindTargets);
  reshade::register_event<reshade::addon_event::draw>(OnCasterDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(OnCasterDrawIndexed);
  reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnCasterDrawIndirect);
}

// The scene's shaders (`shaders`) and the game's ambient occlusion passes (none: the trace at the sun draws), the
// addon's, kept while installed (after the ambient occlusion pass's own hooks: its result is multiplied after); the
// Light Bounce's trace through rays.
inline void Install(std::span<const SceneShader> shaders, std::span<const ao_pass::AmbientOcclusionDraw> ambient_draws,
                    const Host& host = TW_HOST) {
  InstallScene(shaders, host);
  // Far trees face the sun too, under their own row (Capture).
  internal::sprite_shadows_installed = std::ranges::any_of(shaders, [](const SceneShader& shader) { return shader.sun_facing && !shader.far_tree; });
  internal::far_tree_shadows_installed = std::ranges::any_of(shaders, &SceneShader::far_tree);
  light_bounce::SetRayTracedTrace(&internal::TraceLightBounce);
  for (const auto& draw : ambient_draws) {
    renodx::utils::command_action::Register(
        OnAmbientOcclusionDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &draw);
  }
}

// The sun light draws (the addon's, kept while installed): before the screen-space shadows (their occlusion reads this
// frame's trace), the AO filter and the Light Bounce.
// `ambient_cube`: the first float of the lighting buffer's ambient cube (six colours: +x, -x, +y, -y, +z, -z; the paths'
// sky), 0 none. `sun_colour`: the first float of its sun colour where the game's sun light shaders light the G-buffer's
// diffuse colour by it as Attila's (x N.L x the shadow, x 0.004 with the ambient's scale switch, else x the includer's
// TW_RT_SUN_LIGHT_SCALE: rt_sun.cs_5_0.hlsl), the
// paths' sun light of no view with Path-Traced Ambient (RT_PATH_SUN_LIGHT), 0 none (they keep the light learned from the
// screen).
inline void InstallSunDraws(std::span<const light_bounce::SunDraw> sun_draws, UINT ambient_cube = 0, UINT sun_colour = 0) {
  internal::ambient_cube = ambient_cube;
  internal::sun_colour = sun_colour;
  for (const auto& draw : sun_draws) {
    internal::runtime.sun_draw_shaders.insert(draw.shader_hash);
    renodx::utils::command_action::Register(
        OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
    if (ambient_cube != 0) {
      renodx::utils::command_action::Register(
          OnPathAmbientDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
          &draw);
    }
  }
}

inline void Uninstall() {
  light_bounce::SetRayTracedTrace(nullptr);
  renodx::utils::command_action::Unregister(OnAmbientOcclusionDraw);
  renodx::utils::command_action::Unregister(OnSunDraw);
  renodx::utils::command_action::Unregister(OnSunDebugAmbient);
  if (internal::ambient_cube != 0) renodx::utils::command_action::Unregister(OnPathAmbientDraw);
  if (std::exchange(internal::fog_shadows_installed, false)) renodx::utils::command_action::Unregister(OnFogDraw);
  if (std::exchange(internal::smoke_shadows_installed, false)) {
    renodx::utils::command_action::Unregister(OnParticleDraw);
    if (!internal::clip_captures) reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  }
  if (std::exchange(internal::shadow_readers_installed, false)) renodx::utils::command_action::Unregister(OnShadowReaderDraw);
  if (std::exchange(internal::shadows_only_installed, false)) {
    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindTargets);
    reshade::unregister_event<reshade::addon_event::draw>(OnCasterDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnCasterDrawIndexed);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnCasterDrawIndirect);
  }
  if (std::exchange(internal::point_lights_installed, false)) {
    renodx::utils::command_action::Unregister(OnPointLightDraw);
    if (internal::light_constants_slot == internal::NO_LIGHT_CONSTANTS) {
      reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitInputLayout);
      reshade::unregister_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyInputLayout);
    } else {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(internal::OnMapLightConstants);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapLightConstants);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateLightConstants);
      internal::light_uploads_watched.store(false, std::memory_order_relaxed);
    }
    reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyPointLightDevice);
  }
  renodx::utils::command_action::Unregister(OnCutOutShadowDraw);
  UninstallScene();
}

// The settings after the anti-aliasing's (`ambient_alone`: the ray-traced ambient occlusion in place of the game's and
// GT-VBAO, Attila's).
// `reflection_pass`: the game's reflection pass reads the ray-traced reflections (Attila, Thrones, Warhammer,
// Warhammer II); the others get no reflection settings. The Debug View's rows and the profile's button follow it or, shown
// once it has run, the game's tone mapper hook calling OnTonemapDrawn.
// `light_bounce_strength`: the Light Bounce's (Lighting), for a note when the effects that replace its trace are on
// without it (null: none).
inline void AppendSettings(std::vector<renodx::utils::settings::Setting*>* settings, Sliders* sliders,
                           const feature_settings::AaSliders* aa,
                           bool ambient_alone = false, bool reflection_pass = false,
                           const std::string& section = feature_settings::AA_SECTION,
                           const float* light_bounce_strength = nullptr) {
  const auto dlaa_on = [aa] { return feature_settings::Upscaling(*aa); };
  // Turned on: the Light Bounce (Lighting) on with them when it was off (they trace its light: without it they did
  // nothing, ATN 2026-10-28).
  const auto light_bounce_on = [light_bounce_strength] {
    if (light_bounce_strength == nullptr || *light_bounce_strength > 0.f) return;
    if (const auto* setting = renodx::utils::settings::FindSetting("RayTracedLightBounce"); setting != nullptr && setting->GetValue() != 0.f) {
      renodx::utils::settings::UpdateSettings({{"light_bounce_strength", 1.f}});
    }
  };
  const auto leaves_in_scene = [] {
    return internal::host.scene_draw != nullptr || std::ranges::any_of(internal::scene_shaders, [](const SceneShader& shader) { return shader.foliage; });
  };
  // Swaying copies exist only for recorded foliage with wind (`.still`: the campaign's trees, placed as they stand).
  const auto sway_in_scene = [] {
    return std::ranges::any_of(internal::scene_shaders,
                               [](const SceneShader& shader) { return shader.foliage && !shader.rows_shader.empty() && !shader.still; });
  };
  // Grass placed by its instance rows (Attila).
  const auto grass_placed = [] {
    return std::ranges::any_of(internal::scene_shaders, [](const SceneShader& shader) { return shader.grass && !shader.rows_shader.empty(); });
  };
  settings->insert(settings->end(), {
      new renodx::utils::settings::Setting{
          .key = "RayTracedSunShadows",
          .binding = &sliders->sun_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Shadows (Experimental)",
          .section = section,
          .tooltip = "Every shadow from rays: the sun's (terrain, buildings, trees, units, grass, banners) and the torches' and\n"
                     "fires'. Replaces the game's shadow maps and Screen-Space Shadows.\n"
                     "Needs DLAA or an upscaler and renodx-tw-dlaa64.exe.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedShadowSoftness",
          .binding = &sliders->sun_softness,
          .default_value = 0.5f,
          .label = "Ray-Traced Shadow Softness",
          .section = section,
          .tooltip = "How soft ray-traced shadows get away from what casts them. 0: sharp.",
          .min = 0.f,
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightSoftness",
          .binding = &sliders->light_softness,
          .default_value = 0.25f,
          .label = "Torch and Fire Shadow Softness",
          .section = section,
          .tooltip = "How big torches and fires are for their soft shadows. 0: sharp.\n"
                     "Needs DLSS Ray Reconstruction (NVIDIA only).",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f m",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightImportance",
          .binding = &sliders->light_importance,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Importance-Sampled Torch Shadows (Experimental)",
          .section = section,
          .tooltip = "Two torch / fire shadow rays a pixel, picked by how much light they give (as ReSTIR DI picks lights):\n"
                     "faster with many fires; dim fires' shadows follow moving units slower.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightSteady",
          .binding = &sliders->light_steady,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Steady Torch Shadows (Experimental)",
          .section = section,
          .tooltip = "Torch and fire shadows that don't flicker: every fire traced every frame, with sharp edges.\n"
                     "Heavier with many fires. Overrides Importance-Sampled Torch Shadows and Torch and Fire Shadow Softness.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightCount",
          .binding = &sliders->light_count,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Torch and Fire Shadow Lights (Experimental)",
          .section = section,
          .tooltip = "How many torches and fires cast ray-traced shadows, the nearest and brightest first.\n"
                     "Each spot of the screen traces the 32 that light it most. More: heavier in big fires.",
          .labels = {"32", "64", "128"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightRange",
          .binding = &sliders->light_range,
          .default_value = 0.f,
          .label = "Torch and Fire Shadow Range (Experimental)",
          .section = section,
          .tooltip = "Only torches and fires this close to the camera cast ray-traced shadows; the count goes to\n"
                     "the nearer ones. 0 m: any distance.",
          .min = 0.f,
          .max = 500.f,
          .format = "%.0f m",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightGlow",
          .binding = &sliders->light_glow,
          .default_value = 1.f,
          .label = "Torch and Fire Light in Fog and Smoke",
          .section = section,
          .tooltip = "Torches and fires light the fog around them (with Ray-Traced Fog Shadows: their shafts and\n"
                     "shadows in it) and the smoke and dust near them. 0: off.",
          .min = 0.f,
          .max = 2.f,
          .format = "%.2f",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          // Its readers: the fog shadows' (RT_FOG_LIGHTS) and the lit particles' b9.y, bound only by the shadow readers' hook (Attila's vfx00 / vfx01).
          .is_visible = [] { return internal::point_lights_installed && (internal::fog_shadows_installed || internal::shadow_readers_installed); },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingOutputResolution",
          .binding = &sliders->output_rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Rays a Pixel (Use higher for lower scalings)",
          .section = section,
          .tooltip = "More rays for every rendered pixel, each from its own spot of it: less noise, finer shadow edges.\n"
                     "The cost grows with the rays. Match Output: as many rays on screen as at 100% (up to 8 a pixel,\n"
                     "1 at 100%). 2 to 8 rays also work at 100%. The rendered image stays at the upscaler's size.",
          .labels = {"1", "Match Output", "2", "3", "4", "5", "6", "7", "8"},
          .is_enabled = [aa] { return feature_settings::Upscaling(*aa); },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingRaysShadows",
          .binding = &sliders->rays_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Rays a Pixel: Shadows",
          .section = section,
          .tooltip = "The sun's and the torches' and fires' shadows take the extra rays.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->output_rays != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingRaysAmbient",
          .binding = &sliders->rays_ambient,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Rays a Pixel: Ray-Traced AO",
          .section = section,
          .tooltip = "Ray-Traced AO takes the extra rays.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->output_rays != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingRaysReflections",
          .binding = &sliders->rays_reflections,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Rays a Pixel: Reflections",
          .section = section,
          .tooltip = "Ray-traced reflections take the extra rays.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->output_rays != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingRaysPaths",
          .binding = &sliders->rays_paths,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Rays a Pixel: Global Illumination",
          .section = section,
          .tooltip = "Ray-traced and path-traced Global Illumination and Path-Traced AO take the extra rays\n"
                     "(up to 16 a pixel). The heaviest of the four.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->output_rays != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSpriteShadows",
          .binding = &sliders->sprite_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Sprite Soldier Shadows",
          .section = section,
          .tooltip = "The flat far soldiers cast ray-traced shadows too.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::sprite_shadows_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedFarTreeShadows",
          .binding = &sliders->far_tree_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Far Tree Shadows",
          .section = section,
          .tooltip = "The flat far trees cast ray-traced shadows too.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::far_tree_shadows_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedDebugLog",
          .binding = &sliders->debug_log,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Log Details",
          .section = section,
          .tooltip = "Ray tracing's numbers in the logs every 10 s, for bug reports. With it on when a battle starts,\n"
                     "also where a GPU reset stopped. Off: nothing of this runs.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLeafLight",
          .binding = &sliders->leaf_light,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Light Through Leaves",
          .section = section,
          .tooltip = "Leaves, cloth and sails let some sunlight through ray-traced shadows (where they reach past the game's).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          // Shown where the scene has alpha-tested leaves or cloth.
          .is_visible = [] {
            return std::ranges::any_of(internal::scene_shaders,
                                       [](const SceneShader& shader) { return (shader.foliage && !shader.dynamic) || shader.thin; });
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedColouredShadows",
          .binding = &sliders->coloured_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Coloured Shadows",
          .section = section,
          .tooltip = "Sunlight through cloth, sails and banners takes their colour.\nNeeds Light Through Leaves and DLSS Ray Reconstruction (NVIDIA only).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f && sliders->leaf_light != 0.f; },
          .is_visible = [] { return internal::ambient_cube != 0 && internal::ThinInScene(); },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedGrassSway",
          .binding = &sliders->grass_sway,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Swaying Grass Shadows",
          .section = section,
          .tooltip = "Ray-traced grass sways with the grass (as much as fits a frame).\n"
                     "Off: still grass in the ray tracing, faster and less VRAM.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
          .is_visible = grass_placed,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSwayRange",
          .binding = &sliders->sway_range,
          .default_value = 0.f,
          .label = "Swaying Shadows Range (Experimental)",
          .section = section,
          .tooltip = "Trees and grass sway in the ray tracing only this close to the camera;\n"
                     "farther ones stay still: faster. 0 m: everywhere.",
          .min = 0.f,
          .max = 300.f,
          .format = "%.0f m",
          .is_enabled = dlaa_on,
          .is_visible = sway_in_scene,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedOffScreenSway",
          .binding = &sliders->sway_off_screen,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Off-Screen Sway (Experimental)",
          .section = section,
          .tooltip = "Off: trees and grass away from the view stay still in the ray tracing: faster.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
          .is_visible = sway_in_scene,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedGrassShadowRange",
          .binding = &sliders->grass_shadow_range,
          .default_value = 0.f,
          .label = "Grass Shadow Range (Experimental)",
          .section = section,
          .tooltip = "Past this distance grass casts no ray-traced shadows (on itself or the ground):\n"
                     "faster over fields. 0 m: everywhere.",
          .min = 0.f,
          .max = 200.f,
          .format = "%.0f m",
          .is_enabled = dlaa_on,
          .is_visible = [] {
            return std::ranges::any_of(internal::scene_shaders, [](const SceneShader& shader) { return shader.grass; });
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedGrassRange",
          .binding = &sliders->grass_range,
          .default_value = 0.f,
          .label = "Grass Ray Tracing Range (Experimental)",
          .section = section,
          .tooltip = "Grass farther than this is left out of the ray tracing (its shadows there are tiny):\n"
                     "faster in large fields. 0 m: all grass.",
          .min = 0.f,
          .max = 200.f,
          .format = "%.0f m",
          .is_enabled = dlaa_on,
          // The helper leaves out placed grass's instances past the range (other games' grass has no instance rows).
          .is_visible = grass_placed,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAmbientOcclusion",
          .binding = &sliders->ambient_occlusion,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray-Traced Ambient Occlusion (Experimental)",
          .section = section,
          .tooltip = ambient_alone ? "Darker crevices and contact shade from rays, in place of the game's ambient occlusion and\n"
                                     "GT-VBAO. Needs DLAA or an upscaler and renodx-tw-dlaa64.exe."
                                   : "Darker crevices and contact shade from rays, on top of the game's ambient occlusion\n"
                                     "(keep it on). Needs DLAA or an upscaler and renodx-tw-dlaa64.exe.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAORange",
          .binding = &sliders->ambient_range,
          .default_value = 6.f,
          .label = "Ray-Traced AO Range",
          .section = section,
          .tooltip = "How far its rays look for something in the way.",
          .min = 0.5f,
          .max = 10.f,
          .format = "%.1f m",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAORays",
          .binding = &sliders->ambient_rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 2.f,
          .label = "Ray-Traced AO Rays",
          .section = section,
          .tooltip = "More rays: less noise. Heavier.",
          .min = 1.f,
          .max = 8.f,
          .format = "%d",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAOQuarter",
          .binding = &sliders->ambient_quarter,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced AO Rate",
          .section = section,
          .tooltip = "Quarter: one pixel of each 2 x 2 traced a frame, in turn (a quarter of the rays; the upscaler\n"
                     "smooth it over frames). Faster, a little softer at edges.",
          .labels = {"Every pixel", "Quarter"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLeafMicromaps",
          .binding = &sliders->micromaps,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Leaf Micromaps (Experimental)",
          .section = section,
          .tooltip = "Faster ray-traced shadows through leaves: their cut-outs are baked once (NVIDIA Opacity Micro-Map SDK).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] {
            return feature_settings::Upscaling(*aa) && (sliders->sun_shadows != 0.f || sliders->ambient_occlusion != 0.f || sliders->light_bounce != 0.f);
          },
          .is_visible = leaves_in_scene,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedGpuMicromaps",
          .binding = &sliders->gpu_micromaps,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "GPU Micromap Baking",
          .section = section,
          .tooltip = "Leaf Micromaps baked on the GPU (NVIDIA's way in game) instead of CPU threads:\n"
                     "frees the CPU, and new grass's and leaves' shadows settle sooner.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->micromaps != 0.f; },
          .is_visible = leaves_in_scene,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedFastLeafShadows",
          .binding = &sliders->fast_leaves,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Fast Leaf Shadows (Experimental)",
          .section = section,
          .tooltip = "Leaves and grass with Leaf Micromaps skip their alpha tests:\n"
                     "faster shadows under trees and in fields, but coarser and darker.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->micromaps != 0.f; },
          .is_visible = leaves_in_scene,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedFogShadows",
          .binding = &sliders->fog_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Fog Shadows (Experimental)",
          .section = section,
          .tooltip = "Light shafts: the fog loses the sun's glow where it lies in the shadow of buildings, trees and hills.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
          .is_visible = [] { return internal::fog_shadows_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSmokeShadows",
          .binding = &sliders->smoke_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Smoke Shadows (Experimental)",
          .section = section,
          .tooltip = "Smoke and dust dim the ray-traced sunlight they drift through (with Ray-Traced Shadows).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::smoke_shadows_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSmokeQuality",
          .binding = &sliders->smoke_quality,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Smoke Shadow Quality",
          .section = section,
          .tooltip = "Higher: fewer holes and less flicker in thick smoke, slower there.\n"
                     "Layers a sun ray counts: Low 8, Medium 16, High and Ultra 32, Extreme and Maximum 64.\n"
                     "Particles with a shadow at most: Low and Medium 32,768, High 65,536, Ultra 131,072, Extreme 262,144,\n"
                     "Maximum 524,288. Past that the smallest give way. Extreme and Maximum also follow 2 and 4 times the particles.",
          .labels = {"Low", "Medium", "High", "Ultra", "Extreme", "Maximum"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->sun_shadows != 0.f && sliders->smoke_shadows != 0.f; },
          .is_visible = [] { return internal::smoke_shadows_installed; },
      },
      // One tracer for the Light Bounce (ATN: Path Tracing and the rays could both be on, only one ran).
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounce",
          .binding = &sliders->light_bounce,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray-Traced Light Bounce (Experimental)",
          .section = section,
          .tooltip = "What finds the Light Bounce's light in place of its screen-space search:\n"
                     "Rays: one bounce through the scene, nothing guessed behind what the camera sees.\n"
                     "Path Tracing: light bouncing several times, also from what is off screen. Heavy.\n"
                     "Turns the Light Bounce on. Needs DLAA or an upscaler and renodx-tw-dlaa64.exe.",
          .labels = {"Off", "Rays", "Path Tracing"},
          .is_enabled = dlaa_on,
          .on_change = light_bounce_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedPathAmbient",
          .binding = &sliders->path_ambient,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path-Traced Ambient (Experimental)",
          .section = section,
          .tooltip = "With Path Tracing: the paths bring the sky's light too, in place of the game's ambient light and the\n"
                     "ray-traced AO (less noise, faster). Small contact shadows follow the Light Bounce's resolution.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->light_bounce == 2.f; },
          .is_visible = [] { return internal::ambient_cube != 0; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedPathSkyApart",
          .binding = &sliders->path_sky_apart,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Path-Traced AO: Own Sky Rays",
          .section = section,
          .tooltip = "The sky's light averaged from each pixel's own rays, as in Ray-Traced AO:\n"
                     "less noise, and more rays help. Off: through ReSTIR GI.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->light_bounce == 2.f && sliders->path_ambient != 0.f; },
          .is_visible = [] { return internal::ambient_cube != 0; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedPathKeptCube",
          .binding = &sliders->path_kept_cube,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path-Traced AO: Steady Bounce Light (Experimental)",
          .section = section,
          .tooltip = "Light bouncing off walls and ground follows the game's sky, not the view Path-Traced AO started in.\n"
                     "For a Path-Traced AO that stays too dark until turned off and on.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->light_bounce == 2.f && sliders->path_ambient != 0.f; },
          .is_visible = [] { return internal::ambient_cube != 0; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedPathSer",
          .binding = &sliders->path_ser,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path Tracing SER (Experimental)",
          .section = section,
          .tooltip = "Shader execution reordering for Path Tracing and Ray-Traced Reflections\n"
                     "(DXR 1.2, RTX 40 / 50 series): may be faster.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && (sliders->light_bounce == 2.f || sliders->reflections != 0.f); },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSharc",
          .binding = &sliders->sharc,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "SHaRC Radiance Cache (Experimental)",
          .section = section,
          .tooltip = "With Path Tracing: NVIDIA's SHaRC for the light of what the screen does not show\n"
                     "(more bounces off-screen, paths end sooner).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->light_bounce == 2.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceRays",
          .binding = &sliders->light_bounce_rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 4.f,
          .label = "Ray-Traced Light Bounce Rays",
          .section = section,
          .tooltip = "More rays: less noise. Heavier.",
          .min = 1.f,
          .max = 16.f,
          .format = "%d",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->light_bounce != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceResolution",
          .binding = &sliders->light_bounce_resolution,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Ray-Traced Light Bounce Resolution",
          .section = section,
          .tooltip = "Its own resolution (the screen-space Light Bounce's settings no longer apply to it).\n"
                     "Full: sharpest, about 4x Half's cost.",
          .labels = {"Full", "Half", "Quarter"},
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->light_bounce != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceRange",
          .binding = &sliders->light_bounce_range,
          .default_value = 10.f,
          .label = "Ray-Traced Light Bounce Range",
          .section = section,
          .tooltip = "How far its rays look for light.",
          .min = 1.f,
          .max = 50.f,
          .format = "%.0f m",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->light_bounce != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Light Bounce (Lighting) is off: the Ray-Traced Light Bounce does nothing without it.",
          .section = section,
          .tint = 0xFF5050,
          .is_visible = [sliders, light_bounce_strength] {
            return light_bounce_strength != nullptr && *light_bounce_strength <= 0.f && sliders->light_bounce != 0.f;
          },
      },
      new renodx::utils::settings::Setting{
          .key = "PathTracingBounces",
          .binding = &sliders->path_bounces,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 2.f,
          .label = "Path Tracing Bounces",
          .section = section,
          .tooltip = "How many more surfaces a light path follows where the camera sees nothing (behind you, behind walls).\n"
                     "What is on screen already carries every bounce, so there is little to see: at 2, slightly darker light\n"
                     "out of closed-in places.",
          .min = 1.f,
          .max = 2.f,
          .format = "%d",
          .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->light_bounce == 2.f; },
      },
  });
  if (reflection_pass) {
    settings->insert(settings->end(), {
        new renodx::utils::settings::Setting{
            .key = "RayTracedReflections",
            .binding = &sliders->reflections,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 0.f,
            .label = "Ray-Traced Reflections (Experimental)",
            .section = section,
            .tooltip = "Reflections on armour, metal and water from rays: what is off screen or hidden shows too.\n"
                       "Needs DLAA or an upscaler, renodx-tw-dlaa64.exe and in-game Screen Space Reflections on.",
            .labels = {"Off", "On"},
            .is_enabled = dlaa_on,
        },
        new renodx::utils::settings::Setting{
            .key = "RayTracedReflectionSamples",
            .binding = &sliders->reflection_samples,
            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
            .default_value = 1.f,
            .label = "Ray-Traced Reflection Samples",
            .section = section,
            .tooltip = "More rays per pixel smooth rough metal and armour; water and mirrors take one ray. Heavier.",
            .min = 1.f,
            .max = 8.f,
            .format = "%d",
            .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->reflections != 0.f; },
        },
        new renodx::utils::settings::Setting{
            .key = "RayTracedReflectionReuse",
            .binding = &sliders->reflection_reuse,
            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
            .default_value = 0.f,
            .label = "Reflection Reuse (Experimental)",
            .section = section,
            .tooltip = "Less noise on rough reflections.\n"
                       "From ReSTIR GI: also the path-traced Light Bounce's samples (needs Path Tracing).\n"
                       "Own ReSTIR: their own samples, reused over frames and between neighbours.",
            .labels = {"Off", "From ReSTIR GI", "Own ReSTIR"},
            .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->reflections != 0.f; },
        },
        new renodx::utils::settings::Setting{
            .key = "RayTracedWaterReflections",
            .binding = &sliders->water_reflections,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 1.f,
            .label = "Ray-Traced Water Reflections (Experimental)",
            .section = section,
            .tooltip = "Rivers and lakes reflect what the rays find, also off screen. Off: screen-space reflections there.",
            .labels = {"Off", "On"},
            .is_enabled = [sliders, aa] { return feature_settings::Upscaling(*aa) && sliders->reflections != 0.f; },
            .is_visible = [] { return internal::water_pass; },
        },
        new renodx::utils::settings::Setting{
            .key = "RayTracedWaterSteady",
            .binding = &sliders->water_steady,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 1.f,
            .label = "Steadier Water Reflections",
            .section = section,
            .tooltip = "Fewer flickering dots in calm water's reflections: each pixel stays within what the water around it shows.",
            .labels = {"Off", "On"},
            .is_enabled = [sliders, aa] {
              return feature_settings::Upscaling(*aa) && sliders->reflections != 0.f && sliders->water_reflections != 0.f;
            },
            .is_visible = [] { return internal::water_pass; },
        },
        new renodx::utils::settings::Setting{
            .key = "RayTracedWaterSoft",
            .binding = &sliders->water_soft,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 1.f,
            .label = "Softer Water Reflections",
            .section = section,
            .tooltip = "Calmer water reflections, a little softer. Samples and the other reflection settings then work on water too.\n"
                       "Heavier.",
            .labels = {"Off", "On"},
            .is_enabled = [sliders, aa] {
              return feature_settings::Upscaling(*aa) && sliders->reflections != 0.f && sliders->water_reflections != 0.f;
            },
            .is_visible = [] { return internal::water_pass; },
        },
        new renodx::utils::settings::Setting{
            .key = "RayTracedSpecularOcclusion",
            .binding = &sliders->specular_occlusion,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 0.f,
            .label = "Ray-Traced Specular Occlusion (Experimental)",
            .section = section,
            .tooltip = "The sky's reflection dimmed where something blocks it.\n"
                       "Needs DLAA or an upscaler, renodx-tw-dlaa64.exe and in-game Screen Space Reflections on.",
            .labels = {"Off", "On"},
            .is_enabled = dlaa_on,
        },
    });
  }
  settings->insert(settings->end(), {
      new renodx::utils::settings::Setting{
          .key = "RayTracedDebugView",
          .binding = &sliders->debug_view,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray Tracing Debug View",
          .section = section,
          .tooltip = "A ray-traced effect's raw result as the picture (All: every one tiled), to check they work.\n"
                     "Screen-Space AO: the game's or GT-VBAO's. Magenta: it made nothing (off or not traced).",
          .labels = {"Off", "Sun Shadows", "Ambient Occlusion", "Light Bounce", "Reflections", "Specular Occlusion",
                     "Point Light Shadows", "Fog Shadows", "Screen-Space AO", "All"},
          .is_enabled = dlaa_on,
          .is_visible = [] { return internal::debug_view_drawn.load(); },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSceneTest",
          .binding = &sliders->scene_test,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray Tracing Scene (Test)",
          .section = section,
          .tooltip = "While the Debug View is on: one part of the scene alone, to find what breaks an effect.",
          .labels = {"Everything", "Terrain Only", "Objects and Trees Only", "No Leaves", "Nothing Kept Off-Screen"},
          .is_enabled = dlaa_on,
          .is_visible = [sliders] { return internal::debug_view_drawn.load() && sliders->debug_view != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Record Ray Tracing Profile (30 s)",
          .section = section,
          .tooltip = "30 s of every frame's ray tracing costs into tw_rt_profile.csv and tw_rt_profile_helper.csv\n"
                     "(next to the game's exe). Turn the effects on, press, then move the camera over the map.",
          .is_enabled = dlaa_on,
          .on_change = [] { StartProfile(); },
          .is_visible = [reflection_pass] { return reflection_pass || internal::debug_view_drawn.load(); },
      },
  });
}

// After the tone mapper (the addon's tone mapper hook, after the anti-aliasing's): the Debug View over its output.
inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  internal::debug_view_drawn = true;
  auto* native = internal::NativeContext(cmd_list);
  if (native == nullptr) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  if (ComPtr<ID3D11Texture2D> target; SUCCEEDED(resource.As(&target))) DrawDebugView(native, target.Get());
}

}  // namespace twattila::rt_scene
