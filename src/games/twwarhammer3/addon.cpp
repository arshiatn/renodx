/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

// Game fixes. 0 = the game's own shaders run (our shader files can stay in the folder).
#define FIX_DOF 1    // DOF below 100% resolution scale: doffocus00, dofcoc00, dofbg00, dof00 + DOF slider
#define FIX_BLOOM 1  // Bloom with the DLSS mod: bloom00

#include <d3d11.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <memory>
#include <mutex>
#include <cmath>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>
#include "./rt_outside_view_gs.h"
#include "./rt_outside_view_uv_gs.h"
#include "./rt_terrain_keep_gs.h"
#include "./rt_terrain_outside_hs.h"

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../twattila/frame_generation.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/ao_filter.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/credits.hpp"
#include "../talesofarise_dlss/pixel_trace.hpp"
#include "../twattila/pass_timing.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "../twattila/test_hooks.hpp"
#include "../twattila/water_effects.hpp"
#include "./building_masks.hpp"
#include "./cut_out_masks.hpp"
#include "./faded_pixel_shaders.hpp"
#include "./game_gi.hpp"
#include "./shadow_map_size.hpp"
#include "./object_motion.hpp"
#include "./vfx_particles.hpp"
#include "./resolution_scale.hpp"
#include "./taa_dlss.hpp"
#include "./more_tests.h"
#include "../twrome2/engine_tweaks.hpp"
#include "../twwarhammer2/ao_trace.hpp"
#include "../twwarhammer2/line_sweep_ao.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
// shared.h's register for it (the other Total War games: b13).
constexpr UINT SHADER_INJECTION_SLOT = 10;
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

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to Warhammer III's SDR picture).
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
      {"v31_highlight_hue_shift", 0.15f},
      {"v31_highlight_saturation_shift", 0.f},

      // PsychoV35: twattila::psycho35::Defaults, below.

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 0.70f},
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
      {"lensflare", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"sharpening", 1.f},
      {"dof", 1.f},
      {"sky_deband", 0.f},
      {"fog_strength", 1.f},
      {"some_indicators", 1.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

void ApplyPsychoRecommended2500() {
  // ATN: every recommended preset in PsychoV35 (2026-11-12; its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},
      {"exposure", 1.f},
      {"highlights", 0.85f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      // Active extra controls
      {"bloom", 0.25f},
      {"lensflare", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"sharpening", 1.f},
      {"dof", 1.f},
      {"sky_deband", 2.f},
  });
}

// SDR: the game's own tonemapper and look, every other control at its default; video AutoHDR is already off in Reset.
void ApplySdrPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemapper", 0.f}});
}

// Frame copy for sharpening / CA ///////////////////////////////////////////////////////
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

// Puts the game's t0 back. Sharpening's picture is frame generation's HUD-less image (it comes before the UI).
void OnDrawn(reshade::api::command_list* cmd_list) {
  if (!swapped) return;
  swapped = false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  context->PSSetShaderResources(0, 1, &game_srv);
  if (game_srv != nullptr) {
    game_srv->Release();
    game_srv = nullptr;
  }
  twwarhammer3::taa_dlss::KeepOutput(cmd_list);
}

void OnDestroyDevice(reshade::api::device* device) {
  const std::scoped_lock lock(mutex);
  if (data.device == device) Destroy();
}

}  // namespace frame_copy

// Render scale (resolution scale / DLSS mod) ///////////////////////////////////////////
// Below 100% the game renders into the top-left part of full-size targets. The DOF
// (0x6307144B) needs that fraction: taken from the scene viewport (sky / fogs), reset
// to 1 at t00 so later passes at full size are untouched.
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
  if (viewport.Width <= 0.f || viewport.Height <= 0.f) return true;
  if (viewport.TopLeftX != 0.f || viewport.TopLeftY != 0.f) return true;

  ID3D11RenderTargetView* rtv = nullptr;
  context->OMGetRenderTargets(1, &rtv, nullptr);
  if (rtv == nullptr) return true;
  ID3D11Resource* resource = nullptr;
  rtv->GetResource(&resource);
  rtv->Release();
  if (resource == nullptr) return true;
  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  resource->GetType(&dimension);
  D3D11_TEXTURE2D_DESC desc = {};
  if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
  resource->Release();

  // Full-size (back buffer) targets only: skips reflections, cubemaps, shadows.
  if (desc.Width == 0u || desc.Width != back_buffer_width || desc.Height != back_buffer_height) return true;
  const float x = viewport.Width / static_cast<float>(desc.Width);
  const float y = viewport.Height / static_cast<float>(desc.Height);
  if (x < 0.05f || y < 0.05f) return true;
  Set(std::min(x, 1.f), std::min(y, 1.f));
  return true;
}

}  // namespace render_scale

#if FIX_BLOOM
// Bloom (DLSS mod) /////////////////////////////////////////////////////////////////////
// The DLSS path upscales before bloom, but bloom keeps the render-size viewport / dispatch,
// so only the top-left part got bloom. There we run it over the whole targets.
// DLSS path = 0xBA2CFC20 (unit mask / depth upscale for t00) ran last frame.
namespace bloom_fix {

std::atomic<bool> upscale_seen = false;  // this frame
std::atomic<bool> upscaled = false;      // last frame
std::atomic<bool> active = false;        // bright pass -> t00 of this frame
float scale_x = 1.f;
float scale_y = 1.f;

constexpr UINT VIEWPORT_COUNT = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
thread_local D3D11_VIEWPORT saved_viewports[VIEWPORT_COUNT] = {};
thread_local UINT saved_viewport_count = 0;
thread_local bool viewport_swapped = false;

// Size of a view's mip.
bool ViewSize(ID3D11View* view, UINT mip, UINT* width, UINT* height) {
  ID3D11Resource* resource = nullptr;
  view->GetResource(&resource);
  if (resource == nullptr) return false;
  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  resource->GetType(&dimension);
  D3D11_TEXTURE2D_DESC desc = {};
  if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
  resource->Release();
  if (desc.Width == 0u || desc.Height == 0u) return false;
  *width = std::max(desc.Width >> mip, 1u);
  *height = std::max(desc.Height >> mip, 1u);
  return true;
}

bool TargetSize(ID3D11DeviceContext* context, UINT* width, UINT* height) {
  ID3D11RenderTargetView* rtv = nullptr;
  context->OMGetRenderTargets(1, &rtv, nullptr);
  if (rtv == nullptr) return false;
  D3D11_RENDER_TARGET_VIEW_DESC desc = {};
  rtv->GetDesc(&desc);
  const bool ok = desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D
                  && ViewSize(rtv, desc.Texture2D.MipSlice, width, height);
  rtv->Release();
  return ok;
}

// Viewport: render-size part -> whole target. Restored after the draw.
void Expand(ID3D11DeviceContext* context) {
  viewport_swapped = false;
  UINT width = 0;
  UINT height = 0;
  if (!TargetSize(context, &width, &height)) return;
  UINT count = 0;
  context->RSGetViewports(&count, nullptr);  // bound count
  if (count == 0u || count > VIEWPORT_COUNT) return;
  context->RSGetViewports(&count, saved_viewports);
  const D3D11_VIEWPORT& viewport = saved_viewports[0];
  if (viewport.TopLeftX != 0.f || viewport.TopLeftY != 0.f) return;
  // Only the render-size part (not already full, not some other rect).
  if (std::abs(viewport.Width - width * scale_x) > 2.f || std::abs(viewport.Height - height * scale_y) > 2.f) return;
  D3D11_VIEWPORT viewports[VIEWPORT_COUNT] = {};
  for (UINT i = 0; i < count; ++i) viewports[i] = saved_viewports[i];
  viewports[0].Width = static_cast<float>(width);
  viewports[0].Height = static_cast<float>(height);
  context->RSSetViewports(count, viewports);
  saved_viewport_count = count;
  viewport_swapped = true;
}

void End() {
  active = false;
  shader_injection.bloom_remap_x = 1.f;
  shader_injection.bloom_remap_y = 1.f;
}

// Bright pass (0xE1A30AFC): starts the fix; the shader reads gbuffer / particle mask scaled.
bool OnDrawBright(reshade::api::command_list* cmd_list) {
  End();
  viewport_swapped = false;
  if (!upscaled) return true;
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());

  UINT width = 0;
  UINT height = 0;
  if (!TargetSize(context, &width, &height)) return true;
  // Main view only (full-size target).
  if (width != render_scale::back_buffer_width || height != render_scale::back_buffer_height) return true;
  D3D11_VIEWPORT viewport = {};
  UINT count = 1;
  context->RSGetViewports(&count, &viewport);
  if (count == 0u || viewport.Width <= 0.f || viewport.Height <= 0.f) return true;
  if (viewport.TopLeftX != 0.f || viewport.TopLeftY != 0.f) return true;

  // Render fraction: scene capture (sky / fogs) or this viewport, the smaller one.
  const float captured_x = shader_injection.render_scale_x > 0.f ? shader_injection.render_scale_x : 1.f;
  const float captured_y = shader_injection.render_scale_y > 0.f ? shader_injection.render_scale_y : 1.f;
  const float x = std::min(captured_x, viewport.Width / static_cast<float>(width));
  const float y = std::min(captured_y, viewport.Height / static_cast<float>(height));
  if ((x >= 0.99f && y >= 0.99f) || x < 0.05f || y < 0.05f) return true;

  scale_x = x;
  scale_y = y;
  shader_injection.bloom_remap_x = x;
  shader_injection.bloom_remap_y = y;
  active = true;
  Expand(context);
  return true;
}

// Blurs (0x743252CD / 0x011FA545).
bool OnDrawBlur(reshade::api::command_list* cmd_list) {
  viewport_swapped = false;
  if (!active) return true;
  Expand(reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()));
  return true;
}

void OnDrawnRestore(reshade::api::command_list* cmd_list) {
  if (!viewport_swapped) return;
  viewport_swapped = false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  context->RSSetViewports(saved_viewport_count, saved_viewports);
}

// Downsample / upsample (0xCBBE3C4C / 0xF632E451, 32x32 groups): whole output instead.
bool OnDispatch(reshade::api::command_list* cmd_list) {
  if (!active) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  ID3D11UnorderedAccessView* uav = nullptr;
  context->CSGetUnorderedAccessViews(0, 1, &uav);
  if (uav == nullptr) return true;
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
  uav->GetDesc(&desc);
  UINT width = 0;
  UINT height = 0;
  const bool ok = desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D
                  && ViewSize(uav, desc.Texture2D.MipSlice, &width, &height);
  uav->Release();
  if (!ok) return true;
  context->Dispatch((width + 31u) / 32u, (height + 31u) / 32u, 1u);
  return false;  // skips the game's render-size dispatch
}

// 0xBA2CFC20: only runs in the DLSS path.
bool OnDispatchUpscale(reshade::api::command_list*) {
  upscale_seen = true;
  return true;
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  upscaled = upscale_seen.exchange(false);
  active = false;
}

}  // namespace bloom_fix
#endif  // FIX_BLOOM

// AA target (FXAA / TAA / DLSS mod) ////////////////////////////////////////////////////
// With AA on, t00 draws into an 8-bit RT, vignette + lens flares add to it, then the game
// copies it to the 16-bit back buffer (DLSS mod) or FXAA/TAA resolve it. Our clone of that
// RT never takes, so the copy was dropped (leftovers on screen). We run that chain in our
// own 16-bit texture instead and hand it to the copy / FXAA / TAA.
namespace aa_target {

struct Data {
  ID3D11Device* device = nullptr;
  ID3D11Texture2D* texture = nullptr;
  ID3D11RenderTargetView* rtv = nullptr;
  ID3D11ShaderResourceView* srv = nullptr;
  // The upscaler's finish below 100% writes the picture through it (taa_dlss.hpp).
  ID3D11UnorderedAccessView* uav = nullptr;
  UINT width = 0;
  UINT height = 0;
};

constexpr UINT RTV_COUNT = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

Data data;
std::mutex mutex;
uintptr_t game_target = 0u;  // the 8-bit RT the frame's first t00 drew into
bool valid = false;          // our texture holds that RT's frame
bool started = false;        // first t00 of the frame seen (portraits etc. stay stock)

thread_local ID3D11RenderTargetView* saved_rtvs[RTV_COUNT] = {};
thread_local ID3D11DepthStencilView* saved_dsv = nullptr;
thread_local UINT saved_rtv_count = 0;
thread_local bool rt_swapped = false;
thread_local ID3D11ShaderResourceView* saved_srv = nullptr;
thread_local bool srv_swapped = false;

bool IsEightBit(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
      return true;
    default:
      return false;
  }
}

bool GetDesc(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC* desc) {
  if (resource == nullptr) return false;
  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  resource->GetType(&dimension);
  if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
  static_cast<ID3D11Texture2D*>(resource)->GetDesc(desc);
  return true;
}

uintptr_t ResourceOf(ID3D11View* view) {
  if (view == nullptr) return 0u;
  ID3D11Resource* resource = nullptr;
  view->GetResource(&resource);
  if (resource == nullptr) return 0u;
  resource->Release();  // still held by the view
  return reinterpret_cast<uintptr_t>(resource);
}

bool IsSwapChain(uintptr_t resource) {
  bool result = false;
  if (resource == 0u) return false;
  renodx::utils::resource::GetResourceInfo({resource}, [&](const renodx::utils::resource::ResourceInfo& info) {
    result = info.is_swap_chain;
  });
  return result;
}

// Caller holds the mutex.
void Release() {
  if (data.uav != nullptr) data.uav->Release();
  if (data.srv != nullptr) data.srv->Release();
  if (data.rtv != nullptr) data.rtv->Release();
  if (data.texture != nullptr) data.texture->Release();
  data = {};
  valid = false;
  game_target = 0u;
}

// Caller holds the mutex. (Re)creates our 16-bit texture at this size.
bool Ensure(ID3D11Device* device, UINT width, UINT height) {
  if (data.device == device && data.width == width && data.height == height && data.srv != nullptr) return true;
  Release();
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &data.texture))
      || FAILED(device->CreateRenderTargetView(data.texture, nullptr, &data.rtv))
      || FAILED(device->CreateShaderResourceView(data.texture, nullptr, &data.srv))
      || FAILED(device->CreateUnorderedAccessView(data.texture, nullptr, &data.uav))) {
    Release();
    return false;
  }
  data.device = device;
  data.width = width;
  data.height = height;
  return true;
}

// Swaps RTV0 to our texture when it's the frame's 8-bit AA target. start: t00.
bool Redirect(reshade::api::command_list* cmd_list, bool start) {
  rt_swapped = false;
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());

  ID3D11RenderTargetView* rtvs[RTV_COUNT] = {};
  ID3D11DepthStencilView* dsv = nullptr;
  context->OMGetRenderTargets(RTV_COUNT, rtvs, &dsv);
  const auto release_views = [&]() {
    for (auto*& rtv : rtvs) {
      if (rtv != nullptr) rtv->Release();
      rtv = nullptr;
    }
    if (dsv != nullptr) dsv->Release();
    dsv = nullptr;
  };

  ID3D11Resource* target = nullptr;
  if (rtvs[0] != nullptr) rtvs[0]->GetResource(&target);
  const auto target_handle = reinterpret_cast<uintptr_t>(target);

  bool redirect = false;
  ID3D11RenderTargetView* our_rtv = nullptr;
  {
    const std::scoped_lock lock(mutex);
    if (start && !started) {
      started = true;
      valid = false;
      game_target = 0u;
      D3D11_TEXTURE2D_DESC desc = {};
      if (GetDesc(target, &desc) && IsEightBit(desc.Format) && desc.SampleDesc.Count == 1
          && !IsSwapChain(target_handle)) {
        ID3D11Device* device = nullptr;
        context->GetDevice(&device);
        if (device != nullptr) {
          if (Ensure(device, desc.Width, desc.Height)) {
            game_target = target_handle;
            valid = true;
          }
          device->Release();
        }
      }
    }
    redirect = valid && target_handle != 0u && target_handle == game_target;
    our_rtv = data.rtv;
  }
  if (target != nullptr) target->Release();
  if (!redirect) {
    release_views();
    return true;
  }

  UINT count = 0;
  for (UINT i = 0; i < RTV_COUNT; ++i) {
    if (rtvs[i] != nullptr) count = i + 1;
  }
  ID3D11RenderTargetView* new_rtvs[RTV_COUNT] = {};
  for (UINT i = 0; i < RTV_COUNT; ++i) {
    new_rtvs[i] = rtvs[i];
    saved_rtvs[i] = rtvs[i];  // refs kept until restored
  }
  new_rtvs[0] = our_rtv;
  saved_dsv = dsv;
  saved_rtv_count = count;
  context->OMSetRenderTargetsAndUnorderedAccessViews(count, new_rtvs, dsv, 0, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, nullptr, nullptr);
  rt_swapped = true;
  return true;
}

bool OnDrawTonemap(reshade::api::command_list* cmd_list) {
  render_scale::Set(1.f, 1.f);  // DOF done, full size from here
#if FIX_BLOOM
  bloom_fix::End();
#endif
  return Redirect(cmd_list, true);
}
bool OnDrawOverlay(reshade::api::command_list* cmd_list) { return Redirect(cmd_list, false); }

// Puts the game's render targets back.
void OnDrawnRestore(reshade::api::command_list* cmd_list) {
  if (!rt_swapped) return;
  rt_swapped = false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  context->OMSetRenderTargetsAndUnorderedAccessViews(saved_rtv_count, saved_rtvs, saved_dsv, 0, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, nullptr, nullptr);
  for (auto*& rtv : saved_rtvs) {
    if (rtv != nullptr) rtv->Release();
    rtv = nullptr;
  }
  if (saved_dsv != nullptr) saved_dsv->Release();
  saved_dsv = nullptr;
}

// DLSS mod path: the game copies the 8-bit RT to the back buffer. Copy ours instead.
// Registered after RenoDX, which drops that copy (8 -> 16-bit, or its 16-bit clone in HDR10).
bool TryCopy(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest,
             const reshade::api::subresource_box* source_box = nullptr) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return false;
  ID3D11Texture2D* ours = nullptr;
  UINT width = 0;
  UINT height = 0;
  {
    const std::scoped_lock lock(mutex);
    if (dest.handle == game_target) {
      valid = false;  // overwritten by the game
      return false;
    }
    if (!valid || source.handle != game_target) return false;
    ours = data.texture;
    width = data.width;
    height = data.height;
  }
  auto* dest_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
  // HDR10: the back buffer is 10-bit, the game draws into RenoDX's 16-bit clone of it. Copy there.
  const auto clone = renodx::utils::resource::upgrade::GetResourceClone(dest, {.require_enabled = true, .allow_create = false});
  if (clone.handle != 0u) dest_resource = reinterpret_cast<ID3D11Resource*>(clone.handle);
  D3D11_TEXTURE2D_DESC desc = {};
  if (!GetDesc(dest_resource, &desc)) return false;
  if (desc.Width != width || desc.Height != height || desc.SampleDesc.Count != 1) return false;
  if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS) return false;
  if (source_box != nullptr  // whole texture only
      && (source_box->left != 0 || source_box->top != 0 || source_box->right < width || source_box->bottom < height)) return false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  context->CopyResource(dest_resource, ours);
  const std::scoped_lock lock(mutex);
  valid = false;  // frame consumed
  return true;
}

// FXAA / TAA: read our texture instead of the 8-bit RT.
bool OnDrawResolve(reshade::api::command_list* cmd_list) {
  srv_swapped = false;
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  ID3D11ShaderResourceView* srv = nullptr;
  context->PSGetShaderResources(0, 1, &srv);
  if (srv == nullptr) return true;
  const auto resource = ResourceOf(srv);
  ID3D11ShaderResourceView* ours = nullptr;
  {
    const std::scoped_lock lock(mutex);
    if (valid && resource != 0u && resource == game_target) ours = data.srv;
  }
  if (ours == nullptr) {
    srv->Release();
    return true;
  }
  // Below 100%: the picture went whole through the upscaler; FXAA would read its flags at display pixels (their data is the render
  // size's corner) and mix misaligned. Our texture instead of the draw; no copy possible: the draw as ever.
  if (twwarhammer3::taa_dlss::ScaledThisFrame()) {
    ID3D11RenderTargetView* target_view = nullptr;
    context->OMGetRenderTargets(1, &target_view, nullptr);
    const bool copied = target_view != nullptr && TryCopy(cmd_list, {resource}, {ResourceOf(target_view)});
    if (target_view != nullptr) target_view->Release();
    if (copied) {
      srv->Release();
      twwarhammer3::taa_dlss::KeepOutput(cmd_list);
      return false;
    }
  }
  saved_srv = srv;  // ref kept until restored
  context->PSSetShaderResources(0, 1, &ours);
  srv_swapped = true;
  return true;
}

void OnDrawnResolve(reshade::api::command_list* cmd_list) {
  if (!srv_swapped) return;
  srv_swapped = false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  context->PSSetShaderResources(0, 1, &saved_srv);
  if (saved_srv != nullptr) saved_srv->Release();
  saved_srv = nullptr;
  {
    const std::scoped_lock lock(mutex);
    valid = false;  // frame consumed
  }
  // The frame's resolve only (its view swapped): FXAA's shader also composites portraits after the UI, never in the HUD-less image.
  twwarhammer3::taa_dlss::KeepOutput(cmd_list);
}

bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest) {
  return TryCopy(cmd_list, source, dest);
}

bool OnCopyTextureRegion(
    reshade::api::command_list* cmd_list,
    reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box,
    reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box,
    reshade::api::filter_mode) {
  if (source_subresource != 0u || dest_subresource != 0u || dest_box != nullptr) return false;
  return TryCopy(cmd_list, source, dest, source_box);
}

bool OnClearRenderTargetView(reshade::api::command_list*, reshade::api::resource_view rtv, const float[4], uint32_t, const reshade::api::rect*) {
  const auto resource = ResourceOf(reinterpret_cast<ID3D11View*>(rtv.handle));
  const std::scoped_lock lock(mutex);
  if (valid && resource != 0u && resource == game_target) valid = false;  // reused
  return false;
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  const std::scoped_lock lock(mutex);
  valid = false;
  started = false;
  game_target = 0u;
}

void OnDestroyDevice(reshade::api::device* device) {
  const std::scoped_lock lock(mutex);
  if (data.device == reinterpret_cast<ID3D11Device*>(device->get_native())) Release();
}

}  // namespace aa_target

// Ray tracing's sliders (twattila/rt_scene.hpp, through the upscaler's helper): enabled with an upscaler.
twattila::rt_scene::Sliders rt_sliders;
// The game's SSAO (fx_ssao: depth downsample 49F538CF, sweep 004BB1D4, collect 2A629B2B or EE5C29AE) has one reader in 94 traces,
// the sun draws' t_ssao (9EAD8951, 24F97B7B by handle); the reflections' resolve binds a 1x1 texture at its t_ssao in all 94, and
// no water, fog or forward shader binds it in the dump. The rays' AO (RTAO, also under Path-Traced AO) stands alone in the sun's
// slot (rt_ambient_combine), so none of the game's passes runs while the rays feed it: its result cleared to open. Only while the
// rays run (this frame's or the last, the trace comes after these passes): else the sun has no AO at all.
bool SkipGameSsao() {
  const auto& runtime = twattila::rt_scene::internal::runtime;
  return runtime.settings.ambient_occlusion && twattila::dlaa_bridge::bridge.ray_tracing
         && (runtime.traced_frame == runtime.frame || runtime.traced_frame + 1 == runtime.frame);
}
// GT-VBAO writes the game's result itself from the kept depth, never from the sweep's line buffer (the bitmask collects bind no t0
// or t1 line buffer): the sweep is skipped once a camera is kept. Its collect then clears to open if the bitmask can't run.
bool sweep_skipped = false;

// The game's collect output (u0) cleared to open: no AO.
void ClearGameSsao(reshade::api::command_list* cmd_list) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
  native->CSGetUnorderedAccessViews(0, 1, output.GetAddressOf());
  const std::array<float, 4> open = {1.f, 1.f, 1.f, 1.f};
  if (output) native->ClearUnorderedAccessViewFloat(output.Get(), open.data());
}

// The game's reflection pass (fx_environment_specular_light): off, nothing of this runs and the game's bytes do. Our copy of its resolve
// (shaders/reflection00) runs in its place with the rays' reflections or specular occlusion, with the march on all surfaces, or with the
// march at one to one; under the rays' reflections the march and its blurs are left out. Water's rays leave from the water (0ED84C3D
// reads the depth with it in; the guides again).
namespace reflections {

enum class Pass : uint8_t { GAME, OURS, RAYS };
Pass pass = Pass::GAME;
float all_surfaces_row = 0.f;  // The method slider's second place: the game's march, closed to water since WH2, on the flags' bit 0 too.
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> main_depth;
// During OnMarch: its G-buffer (the water drawn by then) and camera for the guides again; the trace's dispatches take those slots.
std::array<ID3D11ShaderResourceView*, 3> march_gbuffer = {};
Microsoft::WRL::ComPtr<ID3D11Buffer> march_camera;
struct Copy {
  std::span<const uint8_t> code;
  Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
};
Copy resolve;
Copy march;
Copy depth_full;
Copy blur_x_full;
Copy blur_y_full;
// The mip chains of the radiance and of t_hit know no viewport: below 100% their levels mix in the target's unused part,
// and the resolve's coarse levels lose the reflections along the picture's right and bottom (OnMipStep). The march's hierarchical
// depth the same: its cells on the picture's edge took depths from the unused part.
Copy mip_down;
Copy mip_blur_x;
Copy mip_blur_y;
Copy depth_step;
// The GI voxels' relevance and the shadow blur map a screen uv as if the picture filled its texture (Experimental row).
float scale_fix_row = 0.f;
Copy gi_relevance;
Copy shadow_blur;
enum class MipChain : uint8_t { NONE, RADIANCE, HIT };
MipChain mip_chain = MipChain::NONE;
std::array<float, 2> radiance_share = {};
std::array<float, 2> hit_share = {};
Microsoft::WRL::ComPtr<ID3D11Buffer> share_buffer;  // b12 of the mip copies
std::array<float, 4> share_written = {};
Microsoft::WRL::ComPtr<ID3D11ComputeShader> game_shader;
Microsoft::WRL::ComPtr<ID3D11Buffer> flags_buffer;  // b10 of the resolve and b1 of the march: {0, this frame's sun, all surfaces, one to one}
std::array<uint32_t, 4> flags_written = {};
Microsoft::WRL::ComPtr<ID3D11Buffer> game_b10;
// This frame's sun light in the main view: the resolve takes the cubes back as it added them (SunDraw::diffuse_rule). Skin (0) writes flags
// 0, the resolve leaves those pixels alone.
bool sun_standard = false;
bool sun_low_quality = false;
bool sun_low_quality_logged = false;
bool all_surfaces_logged = false;
bool one_to_one_logged = false;
// Render at most the targets' size (half the display) with Effects at Output: a march pixel to each render pixel, not to 2.
bool one_to_one = false;
// Reflections Lit as This Frame (row): the light buffer the game's own march reads (3AC630DF copies it into the radiance's first
// level; R16G16B16A16F, the water and our Global Illumination in it, the fog not yet), kept from that dispatch for the rays' hits
// (rt_core.hpp Reflections writes it in the helper's format: RT_HOST's light_copy). Without it the hits read the Light Bounce's
// prepared light, and with Global Illumination off last frame's picture after the tone mapper, what the screen never saw black. On
// by default since the run of 2026-11-10: with Global Illumination off the reflections' view fell from 3.68x of the path-traced
// reference to 1.03x, at no cost.
float light_row = 1.f;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> frame_light;
// Fog in Reflections (row): the game's fog numbers and its fog's light, measured after its light draw (rt_fog_learn), for the
// helper's reflections one frame on (twattila/rt_leg_fog.hlsli).
float fog_row = 0.f;
Copy fog_learn;
ID3D11Resource* fog_light_resource = nullptr;  // by identity only
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> fog_light;
Microsoft::WRL::ComPtr<ID3D11Buffer> fog_learn_constants;
struct FogLearnConstants {
  float fog_strength = 0.f;
  std::array<uint32_t, 2> render_size = {};
  uint32_t padding = 0;
};
FogLearnConstants fog_learn_written;
bool fog_learn_logged = false;
// The fog's light draw wrote the sun texture's fog texels since the last march; they hold a fog's numbers.
bool fog_learned = false;
bool fog_in_sun_texels = false;

// The copy in the game's place until SwapBack.
bool Swap(ID3D11DeviceContext* native, Copy* copy) {
  if (!copy->shader) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    if (FAILED(device->CreateComputeShader(copy->code.data(), copy->code.size(), nullptr, copy->shader.GetAddressOf()))) return false;
  }
  native->CSGetShader(game_shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
  native->CSSetShader(copy->shader.Get(), nullptr, 0);
  return true;
}

void SwapBack(reshade::api::command_list* cmd_list) {
  if (!game_shader) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  native->CSSetShader(game_shader.Get(), nullptr, 0);
  game_shader.Reset();
}

bool RaysOn() {
  return rt_sliders.reflections != 0.f || rt_sliders.specular_occlusion != 0.f;
}

bool OnHierarchicalDepth(reshade::api::command_list* cmd_list) {
  one_to_one = false;
  mip_chain = MipChain::NONE;
  const uint32_t render_width = twattila::frame_history::render_width.load(std::memory_order_relaxed);
  const bool effects = render_width != 0 && twattila::frame_history::effect_share.load(std::memory_order_relaxed) < 1.f;
  if (!RaysOn() && !effects) return true;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return true;
  if (RaysOn()) native->CSGetShaderResources(0, 1, main_depth.ReleaseAndGetAddressOf());
  if (effects) {
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> target_view;
    native->CSGetUnorderedAccessViews(0, 1, target_view.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Resource> target;
    if (target_view) target_view->GetResource(target.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (target) target.As(&texture);
    D3D11_TEXTURE2D_DESC desc = {};
    if (texture) texture->GetDesc(&desc);
    one_to_one = render_width <= desc.Width && twattila::frame_history::render_height.load(std::memory_order_relaxed) <= desc.Height && Swap(native, &depth_full);
    if (one_to_one && !one_to_one_logged) {
      one_to_one_logged = true;
      renodx::utils::log::i(std::format("[Warhammer III] Reflections: the march at one to one, render width {} in a target of {}.", render_width, desc.Width));
    }
    if (desc.Width != 0) {
      const float render_height = static_cast<float>(twattila::frame_history::render_height.load(std::memory_order_relaxed));
      // The radiance's target has the display's size, twice this half-display one; t_hit has this size, filled 1:1 or at half.
      radiance_share = {static_cast<float>(render_width) / static_cast<float>(desc.Width * 2), render_height / static_cast<float>(desc.Height * 2)};
      hit_share = radiance_share;
      if (one_to_one) hit_share = {radiance_share[0] * 2.f, radiance_share[1] * 2.f};
      mip_chain = MipChain::RADIANCE;
    }
  }
  return true;
}

// 3AC630DF (the lit scene into the radiance's first level, before the march): its input is the frame's light buffer.
bool OnRadiance(reshade::api::command_list* cmd_list) {
  if (light_row == 0.f || rt_sliders.reflections == 0.f) return true;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native != nullptr) {
    native->CSGetShaderResources(0, 1, frame_light.ReleaseAndGetAddressOf());
  }
  return true;
}

// After the fog's light draw (A8F5202D): its target is the fog's light at half the render's size, its b5 the fog's numbers.
constexpr auto OnFogLight = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (fog_row == 0.f || rt_sliders.reflections == 0.f || !twattila::rt_scene::internal::active.load(std::memory_order_relaxed)) return {};
  return {.post_callback = [](Context& post, const void*) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    auto& bridge = twattila::dlaa_bridge::bridge;
    if (native == nullptr || !bridge.rt_sun.uav || !bridge.rt_positions.srv) return;
    FogLearnConstants constants = {.fog_strength = shader_injection.fog_strength};
    {
      auto& runtime = twattila::rt_scene::internal::runtime;
      const std::unique_lock lock(runtime.mutex);
      constants.render_size = {runtime.traced_size[0], runtime.traced_size[1]};
    }
    if (constants.render_size[0] == 0 || constants.render_size[1] == 0) return;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
    if (!target) return;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    target->GetResource(resource.GetAddressOf());
    if (fog_light_resource != resource.Get()) {
      fog_light.Reset();
      fog_light_resource = resource.Get();
      device->CreateShaderResourceView(resource.Get(), nullptr, fog_light.GetAddressOf());
    }
    if (!fog_light) return;
    if (!fog_learn.shader && FAILED(device->CreateComputeShader(fog_learn.code.data(), fog_learn.code.size(), nullptr, fog_learn.shader.GetAddressOf()))) return;
    if (!fog_learn_constants) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = sizeof(constants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = &constants};
      if (FAILED(device->CreateBuffer(&desc, &data, fog_learn_constants.GetAddressOf()))) return;
      fog_learn_written = constants;
    } else if (constants.fog_strength != fog_learn_written.fog_strength || constants.render_size != fog_learn_written.render_size) {
      native->UpdateSubresource(fog_learn_constants.Get(), 0, nullptr, &constants, 0, 0);
      fog_learn_written = constants;
    }
    Microsoft::WRL::ComPtr<ID3D11Buffer> game_fog;
    Microsoft::WRL::ComPtr<ID3D11Buffer> game_camera;
    native->PSGetConstantBuffers(5, 1, game_fog.GetAddressOf());
    native->PSGetConstantBuffers(0, 1, game_camera.GetAddressOf());
    if (!game_fog || !game_camera) return;
    if (!fog_learn_logged) {
      fog_learn_logged = true;
      renodx::utils::log::i("[Warhammer III] First run: the fog's numbers and light for the reflections, after ps A8F5202D.");
    }
    std::array<ID3D11Buffer*, 2> kept_buffers = {};
    native->CSGetConstantBuffers(1, static_cast<UINT>(kept_buffers.size()), kept_buffers.data());
    // Its light is this draw's target: readable only once unbound, and that unbinds the draw's whole output (every target,
    // the depth, any unordered access view): all of it goes back after.
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets = {};
    std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> game_output_views = {};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
    native->OMGetRenderTargetsAndUnorderedAccessViews(static_cast<UINT>(game_targets.size()), game_targets.data(), depth.GetAddressOf(), 0,
                                                      static_cast<UINT>(game_output_views.size()), game_output_views.data());
    {
      const twattila::rt_scene::internal::ComputeState kept(native);
      native->OMSetRenderTargets(0, nullptr, nullptr);
      const std::array<ID3D11ShaderResourceView*, 2> views = {fog_light.Get(), bridge.rt_positions.srv.Get()};
      const std::array<ID3D11Buffer*, 3> buffers = {game_fog.Get(), game_camera.Get(), fog_learn_constants.Get()};
      native->CSSetShader(fog_learn.shader.Get(), nullptr, 0);
      native->CSSetUnorderedAccessViews(0, 1, bridge.rt_sun.uav.GetAddressOf(), nullptr);
      native->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
      native->CSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
      native->Dispatch(1, 1, 1);
    }
    fog_learned = true;
    fog_in_sun_texels = true;
    native->CSSetConstantBuffers(1, static_cast<UINT>(kept_buffers.size()), kept_buffers.data());
    for (ID3D11Buffer* buffer : kept_buffers) {
      if (buffer != nullptr) {
        buffer->Release();
      }
    }
    const auto first_output_view = static_cast<UINT>(
        std::ranges::find_if(game_output_views, [](auto* output_view) { return output_view != nullptr; }) - game_output_views.begin());
    if (first_output_view == game_output_views.size()) {
      native->OMSetRenderTargets(static_cast<UINT>(game_targets.size()), game_targets.data(), depth.Get());
    } else {
      constexpr std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> KEEP_COUNTS = {UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX,
                                                                                UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX};
      native->OMSetRenderTargetsAndUnorderedAccessViews(
          first_output_view, game_targets.data(), depth.Get(), first_output_view,
          static_cast<UINT>(game_output_views.size()) - first_output_view, game_output_views.data() + first_output_view, KEEP_COUNTS.data());
    }
    for (auto* game_target : game_targets) {
      if (game_target != nullptr) {
        game_target->Release();
      }
    }
    for (auto* output_view : game_output_views) {
      if (output_view != nullptr) {
        output_view->Release();
      }
    }
  }};
};

// The first of the pass's dispatches: the rays traced here, once a frame, before the G-buffer's slots change. False: left out.
bool OnMarch(reshade::api::command_list* cmd_list) {
  pass = Pass::GAME;
  if (mip_chain == MipChain::RADIANCE) mip_chain = MipChain::HIT;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return true;
  const bool all_surfaces = all_surfaces_row != 0.f && rt_sliders.reflections == 0.f;
  if (all_surfaces || one_to_one) pass = Pass::OURS;
  bool water_rays = false;
  if (RaysOn() && main_depth && twattila::rt_scene::internal::active.load(std::memory_order_relaxed)) {
    native->CSGetShaderResources(0, static_cast<UINT>(march_gbuffer.size()), march_gbuffer.data());
    native->CSGetConstantBuffers(0, 1, march_camera.ReleaseAndGetAddressOf());
    // The fog's numbers are a frame old here (the game draws its fog after this pass). With no fog draw since the last
    // march (a scene without that fog, the row or the rays off in between) they are an older scene's: the five texels
    // cleared (rt_leg_fog.hlsli: texels 9-13 of the sun texture, texel 9's w 0 says no fog).
    if (fog_in_sun_texels && !std::exchange(fog_learned, false)) {
      fog_in_sun_texels = false;
      if (const auto& sun_texture = twattila::dlaa_bridge::bridge.rt_sun.texture; sun_texture) {
        constexpr std::array<float, 20> NO_FOG = {};
        const D3D11_BOX fog_texels = {.left = 9, .top = 0, .front = 0, .right = 14, .bottom = 1, .back = 1};
        native->UpdateSubresource(sun_texture.Get(), 0, &fog_texels, NO_FOG.data(), sizeof(NO_FOG), 0);
      }
    }
    std::array<ID3D11ShaderResourceView*, 2> views = {};
    {
      const twattila::rt_scene::internal::ComputeState kept(native);
      views = twattila::rt_scene::Reflections(native, main_depth.Get(), frame_light.Get());
      frame_light.Reset();
    }
    for (auto*& view : march_gbuffer) {
      if (view != nullptr) view->Release();
      view = nullptr;
    }
    march_camera.Reset();
    if (views[0] != nullptr) {
      auto& runtime = twattila::rt_scene::internal::runtime;
      const std::unique_lock lock(runtime.mutex);
      // Rays from the riverbed would show in the water: else the game's march stays there. So too with water's rays off (the rows keep
      // them leaving from the water, for Ray Reconstruction's guides).
      water_rays = runtime.settings.reflections && runtime.water_traced && rt_sliders.water_reflections != 0.f;
      pass = water_rays ? Pass::RAYS : Pass::OURS;
    }
  }
  if (pass == Pass::GAME) return true;
  const bool sun_rule = sun_low_quality && !sun_standard;
  if (sun_rule && !sun_low_quality_logged) {
    sun_low_quality_logged = true;
    renodx::utils::log::i("[Warhammer III] Reflections: the frame's sun is ps 76A707E5, its cubes taken back by its rule.");
  }
  const std::array<uint32_t, 4> flags = {water_rays ? 1u : 0u, sun_rule ? 1u : 0u, all_surfaces ? 1u : 0u, one_to_one ? 1u : 0u};
  if (!flags_buffer) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    const D3D11_BUFFER_DESC desc = {.ByteWidth = sizeof(flags), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = flags.data()};
    if (FAILED(device->CreateBuffer(&desc, &data, flags_buffer.GetAddressOf()))) {
      pass = Pass::GAME;
      return true;
    }
    flags_written = flags;
  } else if (flags != flags_written) {
    native->UpdateSubresource(flags_buffer.Get(), 0, nullptr, flags.data(), 0, 0);
    flags_written = flags;
  }
  if (pass == Pass::RAYS) return false;
  if (all_surfaces && !all_surfaces_logged) {
    all_surfaces_logged = true;
    renodx::utils::log::i("[Warhammer III] Reflections: the game's march on all surfaces.");
  }
  if (all_surfaces || one_to_one) {
    if (Swap(native, &march)) native->CSSetConstantBuffers(1, 1, flags_buffer.GetAddressOf());
  }
  return true;
}

void OnMarched(reshade::api::command_list* cmd_list) {
  if (!game_shader) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native != nullptr) {
    ID3D11Buffer* none = nullptr;
    native->CSSetConstantBuffers(1, 1, &none);
  }
  SwapBack(cmd_list);
}

bool OnBlur(reshade::api::command_list* cmd_list, Copy* copy) {
  if (pass == Pass::RAYS) return false;
  if (!one_to_one) return true;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native != nullptr) Swap(native, copy);
  return true;
}

// `depth_levels`: the hierarchical depth's steps (CB0A1AF6, after 0ED84C3D, before the march), t_hit's target and share.
bool OnMipStep(reshade::api::command_list* cmd_list, Copy* copy, bool depth_levels = false) {
  if (mip_chain == MipChain::NONE) return true;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return true;
  const auto& share = (depth_levels || mip_chain == MipChain::HIT) ? hit_share : radiance_share;
  const std::array<float, 4> values = {share[0], share[1], 0.f, 0.f};
  if (!share_buffer) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    const D3D11_BUFFER_DESC desc = {.ByteWidth = sizeof(values), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(device->CreateBuffer(&desc, nullptr, share_buffer.GetAddressOf()))) return true;
    share_written = {};
  }
  if (values != share_written) {
    share_written = values;
    native->UpdateSubresource(share_buffer.Get(), 0, nullptr, values.data(), 0, 0);
  }
  if (Swap(native, copy)) native->CSSetConstantBuffers(12, 1, share_buffer.GetAddressOf());
  return true;
}

bool OnScaleFix(reshade::api::command_list* cmd_list, Copy* copy) {
  if (scale_fix_row == 0.f || twattila::frame_history::render_width.load(std::memory_order_relaxed) == 0) return true;
  if (twattila::frame_history::effect_share.load(std::memory_order_relaxed) >= 1.f) return true;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native != nullptr) Swap(native, copy);
  return true;
}

void OnMipStepDone(reshade::api::command_list* cmd_list) {
  if (!game_shader) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native != nullptr) {
    ID3D11Buffer* none = nullptr;
    native->CSSetConstantBuffers(12, 1, &none);
  }
  SwapBack(cmd_list);
}

bool OnResolve(reshade::api::command_list* cmd_list) {
  if (pass == Pass::GAME) return true;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || !Swap(native, &resolve)) {
    pass = Pass::GAME;
    return true;
  }
  if (RaysOn()) twattila::rt_scene::BindReflections(cmd_list, true, true);
  native->CSGetConstantBuffers(10, 1, game_b10.ReleaseAndGetAddressOf());
  native->CSSetConstantBuffers(10, 1, flags_buffer.GetAddressOf());
  return true;
}

void OnResolved(reshade::api::command_list* cmd_list) {
  main_depth.Reset();
  frame_light.Reset();
  mip_chain = MipChain::NONE;
  one_to_one = false;
  if (pass == Pass::GAME) return;
  pass = Pass::GAME;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  native->CSSetConstantBuffers(10, 1, game_b10.GetAddressOf());
  game_b10.Reset();
  SwapBack(cmd_list);
  if (RaysOn()) twattila::rt_scene::BindReflections(cmd_list, true, false);
}

void OnDestroyDevice(reshade::api::device*) {
  main_depth.Reset();
  for (Copy* copy : {&resolve, &march, &depth_full, &blur_x_full, &blur_y_full}) copy->shader.Reset();
  game_shader.Reset();
  flags_buffer.Reset();
  share_buffer.Reset();
  mip_down.shader.Reset();
  mip_blur_x.shader.Reset();
  mip_blur_y.shader.Reset();
  depth_step.shader.Reset();
  gi_relevance.shader.Reset();
  shadow_blur.shader.Reset();
  game_b10.Reset();
  frame_light.Reset();
  fog_learn.shader.Reset();
  fog_light.Reset();
  fog_light_resource = nullptr;
  fog_learn_constants.Reset();
  fog_learned = false;
  fog_in_sun_texels = false;
}

}  // namespace reflections

// Point light shadows and fog shadows from rays: 8 light PS (fx_pointlight*: linear and smooth falloff, with and without the point
// shadow map and the aux G-buffer) and the fog's 6 light PS (fx_battlefog, all traced after the sun). Our copies run only where
// rt_scene's hook has just bound the rays' texture; otherwise the game's bytes do, and nothing here touches the picture.
// The sun's mask (E6BCBA8A, before the sun draw and this frame's trace) the same, while rays cast the sun's shadow: lit cascades, no
// contact march; smoke, clouds and the terrain's map stay. Fog, portraits and forward passes read the maps, so the casters still draw.
// Fog Strength off 1 too on what no upsample scales: the 3 full-resolution fog lights and the legacy fog (the half-resolution lights go
// through 78E4CBF2; the extinction 34719E09 is every map's). None of the 4 drew in any run: level B.
namespace traced_lights {

constexpr std::array<uint32_t, 16> SHADERS = {0x8AD6BFD4, 0xEBF0B0A0, 0xB2453C88, 0x1315B4F2, 0x2240F035, 0xF551453C, 0xBE7249F1, 0xEE766574,
                                              0xA8F5202D, 0xC506E37C, 0xD13AF2EA, 0x4347C118, 0x438D86B8, 0x7998DC13,
                                              0xE6BCBA8A, 0xC5B0D1F3};
constexpr size_t POINT_LIGHT_SHADERS = 8;
constexpr size_t FOG_SHADERS = 6;
constexpr size_t SUN_MASK = POINT_LIGHT_SHADERS + FOG_SHADERS;
constexpr size_t LEGACY_FOG = SUN_MASK + 1;
float rays_replace_maps_row = 1.f;
std::array<std::span<const uint8_t>, SHADERS.size()> codes = {};
std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>, SHADERS.size()> ours = {};
std::array<std::atomic_bool, SHADERS.size()> logged = {};
std::atomic_bool shadow_constants_logged = false;
Microsoft::WRL::ComPtr<ID3D11PixelShader> game_shader;
Microsoft::WRL::ComPtr<ID3D11Buffer> fog_strength_buffer;  // b8: the slider minus 1
float fog_strength_written = 1.f;
Microsoft::WRL::ComPtr<ID3D11Buffer> game_b8;

// Registered after rt_scene's hooks, so run after them: their texture is bound by now, or isn't this frame.
constexpr auto OnDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& hash = command.template GetCallbackData<uint32_t>();
  const size_t index = &hash - SHADERS.data();
  const bool first_run = !logged[index].exchange(true, std::memory_order_relaxed);
  if (first_run) {
    renodx::utils::log::i(std::format("[Warhammer III] First run: {} draw, ps {:08X}.", (index == SUN_MASK) ? "sun mask" : "light or fog", hash));
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  // Log Details, once: how far the cascades reach, so how far Shadow Casters Out of View sees what the camera never looked at
  // (shadowmap_PS's g_split_distances, 560 bytes in, 16 apart: its values can't be read offline), and the maps' size. A copy
  // and a wait for it: not at a default start.
  if (index == SUN_MASK && native != nullptr && rt_sliders.debug_log != 0.f && !shadow_constants_logged.exchange(true, std::memory_order_relaxed)) {
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadow_constants;
    native->PSGetConstantBuffers(1, 1, shadow_constants.GetAddressOf());
    D3D11_BUFFER_DESC desc = {};
    if (shadow_constants) shadow_constants->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Buffer> copy;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (desc.ByteWidth >= 640 && SUCCEEDED(device->CreateBuffer(&desc, nullptr, copy.GetAddressOf()))) {
      native->CopyResource(copy.Get(), shadow_constants.Get());
      if (SUCCEEDED(native->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        const auto* splits = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + 560);
        renodx::utils::log::i(std::format("[Warhammer III] Cascades' split distances: {:g} {:g} {:g} {:g} {:g}.", splits[0], splits[4], splits[8], splits[12], splits[16]));
        // g_vHardShadowBufferSize, the shared shadow constants' first four floats: the maps' size and one over it.
        const auto* size = static_cast<const float*>(mapped.pData);
        renodx::utils::log::i(std::format("[Warhammer III] Shadow map sizes: the shared shadow constants' {:g} {:g} {:g} {:g}.", size[0], size[1], size[2], size[3]));
        native->Unmap(copy.Get(), 0);
      }
    }
  }
  if (native == nullptr) return {};
  if (index == SUN_MASK) {
    if (rays_replace_maps_row == 0.f || !twattila::frame_history::MainViewport(native) || !twattila::rt_scene::SunShadowsTraced()) return {};
  } else {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> bound;
    // rt_local_lights.hlsli's mask, rt_fog.hlsli's fog.
    if (index != LEGACY_FOG) native->PSGetShaderResources((index < POINT_LIGHT_SHADERS) ? 42 : 43, 1, bound.GetAddressOf());
    const bool scales_fog = shader_injection.fog_strength != 1.f && (hash == 0xC506E37C || hash == 0x4347C118 || hash == 0x7998DC13 || index == LEGACY_FOG);
    if (!bound && !scales_fog) return {};
    if (scales_fog) {
      const std::array<float, 4> above_one = {shader_injection.fog_strength - 1.f, 0.f, 0.f, 0.f};
      if (!fog_strength_buffer) {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        native->GetDevice(device.GetAddressOf());
        const D3D11_BUFFER_DESC desc = {.ByteWidth = sizeof(above_one), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
        const D3D11_SUBRESOURCE_DATA data = {.pSysMem = above_one.data()};
        if (FAILED(device->CreateBuffer(&desc, &data, fog_strength_buffer.GetAddressOf()))) return {};
        fog_strength_written = shader_injection.fog_strength;
      } else if (fog_strength_written != shader_injection.fog_strength) {
        native->UpdateSubresource(fog_strength_buffer.Get(), 0, nullptr, above_one.data(), 0, 0);
        fog_strength_written = shader_injection.fog_strength;
      }
      native->PSGetConstantBuffers(8, 1, game_b8.ReleaseAndGetAddressOf());
      native->PSSetConstantBuffers(8, 1, fog_strength_buffer.GetAddressOf());
    }
  }
  auto& shader = ours[index];
  if (!shader) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    const auto code = codes[index];
    if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf()))) return {};
    renodx::utils::log::i(std::format("[Warhammer III] Rays applied: {} draw, ps {:08X}.", (index == SUN_MASK) ? "sun mask" : "light or fog", hash));
  }
  native->PSGetShader(game_shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
  native->PSSetShader(shader.Get(), nullptr, 0);
  return {.post_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&post_native);
    if (post_native == nullptr) return;
    post_native->PSSetShader(game_shader.Get(), nullptr, 0);
    game_shader.Reset();
    if (game_b8) {
      post_native->PSSetConstantBuffers(8, 1, game_b8.GetAddressOf());
      game_b8.Reset();
    }
  }};
};

void OnDestroyDevice(reshade::api::device*) {
  for (auto& shader : ours) shader.Reset();
  game_shader.Reset();
  fog_strength_buffer.Reset();
  game_b8.Reset();
}

}  // namespace traced_lights

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

// t00: starts the AA chain in our texture (replaces UpgradeRTVReplaceShader here). The Debug Views while that texture is
// still its target (their tests want 16 bits at the display's size; the game's own is 8-bit).
#define AATonemapShader(value)                                     \
  {                                                                \
      value,                                                       \
      {                                                            \
          .crc32 = value,                                          \
          .code = __##value,                                       \
          .on_draw = &aa_target::OnDrawTonemap,                    \
          .on_drawn = [](auto* cmd_list) {                         \
            twattila::screen_space_shadows::OnTonemapDrawn(cmd_list); \
            twwarhammer3::taa_dlss::OnTonemapDrawnAtRenderSize(cmd_list, aa_target::data.srv, aa_target::data.uav); \
            twattila::rt_scene::OnTonemapDrawn(cmd_list);          \
            twwarhammer3::taa_dlss::OnTonemapDrawn(cmd_list);      \
            twwarhammer3::taa_dlss::KeepFrame(cmd_list);           \
            aa_target::OnDrawnRestore(cmd_list); },                \
      },                                                           \
  }

// Vignette / lens flares / cutscene: follow t00 into our texture.
#define AAOverlayShader(value)                   \
  {                                              \
      value,                                     \
      {                                          \
          .crc32 = value,                        \
          .code = __##value,                     \
          .on_draw = &aa_target::OnDrawOverlay,  \
          .on_drawn = &aa_target::OnDrawnRestore, \
      },                                         \
  }

// FXAA / TAA (stock, no file): read our texture.
#define AAResolveShader(value)                        \
  {                                                   \
      value,                                          \
      {                                               \
          .crc32 = value,                             \
          .on_inject = [](auto*) { return false; },   \
          .on_draw = &aa_target::OnDrawResolve,       \
          .on_drawn = &aa_target::OnDrawnResolve,     \
      },                                              \
  }

// Sky and fogs: capture the render scale for the DOF.
#define RenderScaleShader(value)                   \
  {                                                \
      value,                                       \
      {                                            \
          .crc32 = value,                          \
          .code = __##value,                       \
          .on_draw = &render_scale::OnDrawCapture, \
      },                                           \
  }

// Fix turned off: the game's shader runs, our file (if any) is ignored.
#define StockShader(value)                          \
  {                                                 \
      value,                                        \
      {                                             \
          .crc32 = value,                           \
          .on_inject = [](auto*) { return false; }, \
      },                                            \
  }

#if FIX_BLOOM
// Bloom (DLSS mod): bright pass (file) / blurs (stock) over the whole target.
#define BloomBrightShader(value)                  \
  {                                               \
      value,                                      \
      {                                           \
          .crc32 = value,                         \
          .code = __##value,                      \
          .on_draw = &bloom_fix::OnDrawBright,    \
          .on_drawn = &bloom_fix::OnDrawnRestore, \
      },                                          \
  }

#define BloomBlurShader(value)                       \
  {                                                  \
      value,                                         \
      {                                              \
          .crc32 = value,                            \
          .on_inject = [](auto*) { return false; },  \
          .on_draw = &bloom_fix::OnDrawBlur,         \
          .on_drawn = &bloom_fix::OnDrawnRestore,    \
      },                                             \
  }

// Downsample / upsample (stock): dispatched over the whole output.
#define BloomDispatchShader(value)                   \
  {                                                  \
      value,                                         \
      {                                              \
          .crc32 = value,                            \
          .on_inject = [](auto*) { return false; },  \
          .on_draw = &bloom_fix::OnDispatch,         \
      },                                             \
  }

// Unit mask / depth upscale (stock): marks the DLSS path.
#define UpscaleMarkerShader(value)                   \
  {                                                  \
      value,                                         \
      {                                              \
          .crc32 = value,                            \
          .on_inject = [](auto*) { return false; },  \
          .on_draw = &bloom_fix::OnDispatchUpscale,  \
      },                                             \
  }
#endif  // FIX_BLOOM

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
            twattila::screen_space_shadows::OnTonemapDrawn(cmd_list); },                              \
      },                                                                                              \
  }

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      FrameCopyShader(0x9D3602F2), //sharpening (reads our 16-bit frame copy)
      AATonemapShader(0xE2EE82F8), //t00 (tonemap + grading)
      AAOverlayShader(0x927BF49C), //vignette00
      AAOverlayShader(0xA0C8B886), //lensflare00
      AAOverlayShader(0x8C0AC342), //lensflare01
      AAOverlayShader(0x854A4580), //lensflare02
      AAOverlayShader(0x9E25CEC2), //lensflare03
      AAOverlayShader(0xF8FAE237), //lensblood00 (blood splatter on the lens)
      AAOverlayShader(0x96966B05), //cutscene00
      AAResolveShader(0x93BC84F1), //FXAA
      AAResolveShader(0x55E539AD), //TAA
      AAResolveShader(0xE1D67C6A), //TAA High
#if FIX_DOF || FIX_BLOOM
      RenderScaleShader(0xEFDBC227), //sky00
      RenderScaleShader(0x34719E09), //battlefog00
      RenderScaleShader(0x78E4CBF2), //battlefog01 (the half-resolution fog light's upsample)
#endif
#if !FIX_DOF
      StockShader(0x64BC7737), //doffocus00
      StockShader(0x2A0D335C), //dofcoc00
      StockShader(0xC7EBC4DD), //dofbg00
      StockShader(0x6307144B), //dof00
#endif
#if FIX_BLOOM
      BloomBrightShader(0xE1A30AFC), //bloom00 (bright pass)
      BloomBlurShader(0x743252CD), //bloom blur h
      BloomBlurShader(0x011FA545), //bloom blur v
      BloomDispatchShader(0xCBBE3C4C), //bloom downsample
      BloomDispatchShader(0xF632E451), //bloom upsample
      UpscaleMarkerShader(0xBA2CFC20), //unit mask / depth upscale (DLSS path)
#else
      StockShader(0xE1A30AFC), //bloom00
#endif
      // UpgradeRTVReplaceShader(EMPTY),
      __ALL_CUSTOM_SHADERS,
  };
  // The AO trace (Advanced): the ambient occlusion's depth downsample (ssao00) and collect (ssao01), replaced so their
  // hooks run.
  // Ambient Occlusion: Visibility Bitmask in the collects (line_sweep_bitmask.hlsli), with the camera of the last
  // tonemap (t00's b1, bound at b1) and, for the collect without depth (ssao02), the sweep's depth (bound at t2;
  // line_sweep_ao.hpp).
  custom_shaders[0x49F538CF].on_draw = [](auto* cmd_list) {
    tw::ao_trace::OnPass(cmd_list, 0x49F538CF, true);
    return !SkipGameSsao();
  };
  // The collects' replacements are the game's code only (Game's costs what it did); the Visibility Bitmask is the
  // addon's own pass (DispatchInstead) with OnCollect's depth and camera (without them, the first frame: the game's, or open
  // with the sweep skipped).
  custom_shaders[0xEE5C29AE].on_draw = [](auto* cmd_list) {
    tw::ao_trace::OnPass(cmd_list, 0xEE5C29AE, true);
    if (SkipGameSsao()) {
      ClearGameSsao(cmd_list);
      return false;
    }
    if (shader_injection.ssao_mode < 1.f) return true;
    bool dispatched = false;
    if (tw::line_sweep_ao::OnCollect(cmd_list, true, false, 1, 0)) {
      dispatched = tw::line_sweep_ao::DispatchInstead(
          cmd_list, __ao_bitmask_collect01, &shader_injection, sizeof(shader_injection), false, __ao_upsample_collect01,
          shader_injection.ssao_resolution != 0.f, SHADER_INJECTION_SLOT);
      tw::line_sweep_ao::OnCollectDone(cmd_list);
    }
    if (!dispatched && sweep_skipped) ClearGameSsao(cmd_list);
    return !dispatched && !sweep_skipped;
  };
  custom_shaders[0x2A629B2B].on_draw = [](auto* cmd_list) {
    tw::ao_trace::OnPass(cmd_list, 0x2A629B2B, true);
    if (SkipGameSsao()) {
      ClearGameSsao(cmd_list);
      return false;
    }
    if (shader_injection.ssao_mode < 1.f) return true;
    bool dispatched = false;
    if (tw::line_sweep_ao::OnCollect(cmd_list, true, true, 1, 2)) {
      dispatched = tw::line_sweep_ao::DispatchInstead(
          cmd_list, __ao_bitmask_collect02, &shader_injection, sizeof(shader_injection), false, __ao_upsample_collect02,
          shader_injection.ssao_resolution != 0.f, SHADER_INJECTION_SLOT);
      tw::line_sweep_ao::OnCollectDone(cmd_list);
    }
    if (!dispatched && sweep_skipped) ClearGameSsao(cmd_list);
    return !dispatched && !sweep_skipped;
  };
  // The reflection pass (fx_hierarchical_depth_buffer's first reduction, fx_environment_specular_light's march, its 2 blurs,
  // resolve): its replacement isn't the game's unless asked for (the reflections namespace above).
  const auto not_injected = [](auto*) { return false; };
  // fx_ssao's sweep, the one pass of the AO's that reads a depth at every scale: the downsample stops below about 2/3 (traces
  // c1a, c2a), and a depth kept from it would stay the last full-size frame's.
  custom_shaders[0x004BB1D4] = {.crc32 = 0x004BB1D4, .on_inject = not_injected, .on_draw = [](auto* cmd_list) {
    sweep_skipped = false;
    if (SkipGameSsao()) return false;
    if (shader_injection.ssao_mode < 1.f) return true;
    tw::line_sweep_ao::KeepComputeDepth(cmd_list, 0);
    sweep_skipped = tw::line_sweep_ao::internal::camera != nullptr;
    return !sweep_skipped;
  }};
  reflections::resolve.code = custom_shaders[0x89AF6FF5].code;
  reflections::march.code = __ssr_game_march;
  reflections::depth_full.code = __ssr_game_depth_full;
  reflections::blur_x_full.code = __ssr_game_blur_x_full;
  reflections::blur_y_full.code = __ssr_game_blur_y_full;
  reflections::mip_down.code = __ssr_mip_down;
  reflections::mip_blur_x.code = __ssr_mip_blur_x;
  reflections::mip_blur_y.code = __ssr_mip_blur_y;
  reflections::depth_step.code = __ssr_depth_step;
  reflections::gi_relevance.code = __gi_relevance_scaled;
  reflections::shadow_blur.code = __shadow_blur_scaled;
  reflections::fog_learn.code = __rt_fog_learn;
  custom_shaders[0x3AC630DF] = {.crc32 = 0x3AC630DF, .on_inject = not_injected, .on_draw = &reflections::OnRadiance};
  // The game's light probes' rays (fx_global_illumination): the sky's light alone under ours (game_gi.hpp).
  custom_shaders[0xDC860C60] = {.crc32 = 0xDC860C60, .on_inject = not_injected, .on_draw = &twwarhammer3::game_gi::OnProbeRays, .on_drawn = &twwarhammer3::game_gi::OnProbeRaysDone};
  custom_shaders[0x1810C89D] = {.crc32 = 0x1810C89D, .on_inject = not_injected, .on_draw = [](auto* cmd_list) { return reflections::OnScaleFix(cmd_list, &reflections::gi_relevance); }, .on_drawn = &reflections::SwapBack};
  custom_shaders[0xDDB14D2C] = {.crc32 = 0xDDB14D2C, .on_inject = not_injected, .on_draw = [](auto* cmd_list) { return reflections::OnScaleFix(cmd_list, &reflections::shadow_blur); }, .on_drawn = &reflections::SwapBack};
  custom_shaders[0x79F74068] = {.crc32 = 0x79F74068, .on_inject = not_injected, .on_draw = [](auto* cmd_list) { return reflections::OnMipStep(cmd_list, &reflections::mip_down); }, .on_drawn = &reflections::OnMipStepDone};
  custom_shaders[0x66C301FF] = {.crc32 = 0x66C301FF, .on_inject = not_injected, .on_draw = [](auto* cmd_list) { return reflections::OnMipStep(cmd_list, &reflections::mip_blur_x); }, .on_drawn = &reflections::OnMipStepDone};
  custom_shaders[0xFB547085] = {.crc32 = 0xFB547085, .on_inject = not_injected, .on_draw = [](auto* cmd_list) { return reflections::OnMipStep(cmd_list, &reflections::mip_blur_y); }, .on_drawn = &reflections::OnMipStepDone};
  custom_shaders[0xCB0A1AF6] = {.crc32 = 0xCB0A1AF6, .on_inject = not_injected, .on_draw = [](auto* cmd_list) { return reflections::OnMipStep(cmd_list, &reflections::depth_step, true); }, .on_drawn = &reflections::OnMipStepDone};
  custom_shaders[0x89AF6FF5] = {.crc32 = 0x89AF6FF5, .on_inject = not_injected, .on_draw = &reflections::OnResolve, .on_drawn = &reflections::OnResolved};
  custom_shaders[0x0ED84C3D] = {.crc32 = 0x0ED84C3D, .on_inject = not_injected, .on_draw = &reflections::OnHierarchicalDepth, .on_drawn = &reflections::SwapBack};
  custom_shaders[0xEF6B7767] = {.crc32 = 0xEF6B7767, .on_inject = not_injected, .on_draw = &reflections::OnMarch, .on_drawn = &reflections::OnMarched};
  custom_shaders[0x2559E5AE] = {.crc32 = 0x2559E5AE, .on_inject = not_injected, .on_draw = [](auto* cmd_list) { return reflections::OnBlur(cmd_list, &reflections::blur_x_full); }, .on_drawn = &reflections::SwapBack};
  custom_shaders[0xDB5D3C54] = {.crc32 = 0xDB5D3C54, .on_inject = not_injected, .on_draw = [](auto* cmd_list) { return reflections::OnBlur(cmd_list, &reflections::blur_y_full); }, .on_drawn = &reflections::SwapBack};
  // Point lights and fog: the same, our copies aren't the game's unless the rays are bound (the traced_lights namespace above).
  for (size_t index = 0; index < traced_lights::SHADERS.size(); ++index) {
    const uint32_t hash = traced_lights::SHADERS[index];
    traced_lights::codes[index] = custom_shaders[hash].code;
    custom_shaders[hash] = {.crc32 = hash, .on_inject = not_injected};
  }
  // Unit/Building Indicator Brightness: fx_indicator's colour passes (names.tsv; its VS 550C70F2), in no trace (no unit selected in
  // them): level B. Ours only off 1 in an HDR mode, else the game's bytes. Not fx_indicator_mboit (E01EE900 writes moments, no colour;
  // our copy of its resolve 492ED8DA wasn't the game's bits at 1 on WARP and lost its early depth test).
  for (const uint32_t hash : {0xE441DBFFu, 0x709BE774u, 0xA7748703u, 0xDE0524D6u}) {
    custom_shaders[hash].on_replace = [](auto*) { return shader_injection.someindicators != 1.f && IsHDRMode(); };
  }
  custom_shaders[0xE2EE82F8].on_draw = [](auto* cmd_list) {
    if (shader_injection.ssao_mode >= 1.f) tw::line_sweep_ao::KeepPixelCamera(cmd_list, 1);
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    // A traced frame's: its camera (b1) and hdr_to_screen_PS's overscan (b4, c6.y).
    if (twattila::frame_trace::Tracing()) {
      Microsoft::WRL::ComPtr<ID3D11Buffer> camera;
      Microsoft::WRL::ComPtr<ID3D11Buffer> hdr_to_screen;
      native->PSGetConstantBuffers(1, 1, camera.GetAddressOf());
      native->PSGetConstantBuffers(4, 1, hdr_to_screen.GetAddressOf());
      twwarhammer3::taa_dlss::MarkCamera(native, camera.Get(), "tone mapper");
      if (hdr_to_screen) {
        if (const auto c = twattila::frame_trace::ReadBuffer<float>(native, hdr_to_screen.Get(), 0, 7 * 4); !c.empty()) {
          twattila::frame_trace::Mark(std::format("tone mapper: overscan {}", c[(6 * 4) + 1]));
        }
      }
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> exposure_view;
      native->PSGetShaderResources(4, 1, exposure_view.GetAddressOf());
      if (exposure_view) {
        Microsoft::WRL::ComPtr<ID3D11Resource> exposure;
        exposure_view->GetResource(exposure.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Buffer> exposure_buffer;
        if (SUCCEEDED(exposure.As(&exposure_buffer))) {
          if (const auto brightness = twattila::frame_trace::ReadBuffer<float>(native, exposure_buffer.Get(), 0, 1); !brightness.empty()) {
            twattila::frame_trace::Mark(std::format("tone mapper: auto exposure's brightness {}", brightness[0]));
          }
        }
      }
    }
    const bool draw = aa_target::OnDrawTonemap(cmd_list);
    // Before the mod pushes the constants to this draw.
    shader_injection.tonemap_at_render_size = twwarhammer3::taa_dlss::OnTonemapDraw(native, aa_target::rt_swapped, aa_target::data.width, aa_target::data.height) ? 1.f : 0.f;
    return draw;
  };
  // The game's strength is clamp(clamp((1 - scale) x 1.5, 0, 1) + its own, 0, 1) (TAA's modes): the first part makes up for its own
  // upscale, and would land on the upscaler's result.
  custom_shaders[0x9D3602F2].on_draw = [](auto* cmd_list) {
    shader_injection.sharpening_term =
        twwarhammer3::taa_dlss::ScaledThisFrame() ? std::clamp((1.f - twwarhammer3::taa_dlss::RenderShare()) * 1.5f, 0.f, 1.f) : 0.f;
    static float logged_term = -1.f;
    if (rt_sliders.debug_log != 0.f && logged_term != shader_injection.sharpening_term) {
      logged_term = shader_injection.sharpening_term;
      auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&native);
      Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
      native->PSGetConstantBuffers(1, 1, constants.GetAddressOf());
      if (const auto strength = (constants ? twattila::frame_trace::ReadBuffer<float>(native, constants.Get(), 0, 1) : std::vector<float>{}); !strength.empty()) {
        renodx::utils::log::i(std::format("[Warhammer III] Sharpening: the game's strength {:g}, its part for its own upscale {:g}, left {:g}.",
                                          strength[0], logged_term, std::max(strength[0] - logged_term, 0.f)));
      }
    }
    return frame_copy::OnDraw(cmd_list);
  };
  // Each hook's first run in the log: one that never ran looks like one that did nothing.
  for (auto& [hash, shader] : custom_shaders) {
    shader.on_draw = [hash = hash, on_draw = shader.on_draw, logged = std::make_shared<std::atomic_bool>(false)](auto* cmd_list) {
      if (!logged->load(std::memory_order_relaxed) && !logged->exchange(true)) {
        renodx::utils::log::i(std::format("[Warhammer III] First run: shader {:08X}.", hash));
      }
      return on_draw == nullptr || on_draw(cmd_list);
    };
  }
  // Settings ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // PsychoV31 sliders: 0 - max, 1 = its own (PsychoV35's: twattila/psycho35_settings.hpp).
  const auto v31_slider = [](const std::string& key, float* binding, const std::string& label, const std::string& tooltip,
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
        .label = "Keep the in-game brightness at 100%.",
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
        .tooltip = "SDR: the game's original tonemapper (Extended Reinhard).\n"
                   "HDR (Extended): the game's Reinhard without clipping, extended into HDR.\n"
                   "HDR (PsychoV30): replaces Reinhard with PsychoV30 as tonemapper.\n"
                   "HDR (PsychoV31): replaces it with PsychoV31, more grading sliders.\n"
                   "HDR (PsychoV35): replaces it with Musa's PsychoV35 and Hue Clip Emulation (the Recommended presets).",
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
        .tooltip = "HDR (PsychoV31), Video AutoHDR 0.20 and Bloom 0.25, the rest at its default.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
          ApplyPsychoRecommended2500();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "SDR (monitors without HDR)",
        .section = "Presets",
        .group = "preset-line-3",
        .tooltip = "For SDR monitors: Rendering Mode SDR (the game's own picture) and Output SDR (restart the game if it\n"
                   "was HDR), no video AutoHDR. The game's and the UI's white are your monitor's own white.",
        .tint = 0xFFC107,
        .on_change = []() {
          ApplySdrPreset();
          renodx::utils::settings::UpdateSettings({{"SwapChainEncoding", 2.f}});
        },
    },
    // Extra //////////////////////////////////////////////////////////////////////////////////////
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
        .default_value = 0.70f,
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
        .tooltip = "Hue shift of bright highlights, like SDR: fire and explosions turn orange/yellow instead of pink/red.\n0 = none, 1 = PsychoV30 default, 2 = full.",
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
               1.f, 1.f),
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
               "0 = off, 1 = SDR hues. Default 0.15.",
               1.f, 0.15f),
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
        .tooltip = "Bloom multiplier.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "lensflare",
        .binding = &shader_injection.lensflare,
        .default_value = 1.f,
        .label = "Lensflare",
        .section = "Extra",
        .tooltip = "Lensflare multiplier.",
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
        .key = "ssr_hierarchical",
        .binding = &reflections::all_surfaces_row,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = "Reflections on All Surfaces",
        .section = "Extra",
    },
    new renodx::utils::settings::Setting{
        .key = "fog_strength",
        .binding = &shader_injection.fog_strength,
        .default_value = 1.f,
        .label = "Fog Strength",
        .section = "Extra",
        .tooltip = "How thick the game's fog and haze are. 0 = none, 1 = the game's, 2 = double.",
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
#if FIX_DOF
    new renodx::utils::settings::Setting{
        .key = "dof",
        .binding = &shader_injection.dof,
        .default_value = 1.f,
        .label = "Depth of Field",
        .section = "Extra",
        .tooltip = "Strength of depth of field. Needs in-game setting on.\n0 = off, 1 = stock.",
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
#endif
    new renodx::utils::settings::Setting{
        .key = "sky_deband",
        .binding = &shader_injection.sky_deband,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Sky Debanding",
        .section = "Extra (Experimental)",
        .tooltip = "Reduces sky banding.\nHigher levels smooth more but can soften sky details.",
        .labels = {"Off", "Low", "Medium", "High"},
        .is_enabled = IsHDRMode,
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
    new renodx::utils::settings::Setting{
        .key = "some_indicators",
        .binding = &shader_injection.someindicators,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.f,
        .label = "Unit/Building Indicator Brightness",
        .section = "Extra (Experimental)",
        .tooltip = "Brightness of selected highlight indicators, particularly useful at night.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
    },


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

// Graphics (Experimental): the LOD tweaks (pixels per triangle before a model's next LOD) / the sliders (their tweaks
// are filled in ConfigureAddon).
tw::engine_tweaks::Option unit_detail;
tw::engine_tweaks::Option building_detail;
tw::engine_tweaks::Option tree_detail;
tw::engine_tweaks::Option grass_detail;
tw::engine_tweaks::Option object_detail;
tw::engine_tweaks::Option terrain_detail;
tw::engine_tweaks::Option effects_detail;
// Draw distances: the cull tweaks (pixels an object's bounds must cover to be drawn), copied per model at load too.
tw::engine_tweaks::Option unit_draw;
tw::engine_tweaks::Option building_draw;
tw::engine_tweaks::Option tree_draw;
tw::engine_tweaks::Option grass_draw;
tw::engine_tweaks::Option object_draw;
tw::engine_tweaks::Option terrain_draw;
tw::engine_tweaks::Option grass_density;
tw::engine_tweaks::Option terrain_tessellation;
// Every engine option above, then the tests' (Graphics 2 of 2: more_tests.h).
std::vector<tw::engine_tweaks::Option*> graphics_options;
std::vector<twattila::engine_tests::EngineTest> engine_tests;
HMODULE addon_module_handle = nullptr;
twattila::feature_settings::SssSliders sss_sliders;
// Water Clarity and Caustics (twattila/water_effects.hpp): the shaders that read g_sea_bed_light_scatter (each one's RDEF: lighting_VS_PS
// at that slot, offset 160), and battle_water, battle_river and battle_water_polluted, with their colour per material (material_water).
// Caustics where one colour sample of the refraction and the normal map's slot were read; the others have their samples under branches.
// Left out: rigid_campaign_sea (traced: painted colour and a fresnel mix, no depth term), battle_river_noheight, alpha_multiply (no bed).
constexpr std::array<twattila::water_effects::WaterShader, 16> WATER_SHADERS = {{
    {.hash = 0x2076D289, .world_input = 2, .refraction_slot = 18, .normal_slot = 15, .material_water = true},  // battle_water (traced)
    {.hash = 0x281B184D, .world_input = 2, .refraction_slot = 15, .normal_slot = 12, .material_water = true},  // battle_water
    {.hash = 0x7F46A5A9, .world_input = 2, .refraction_slot = 20, .normal_slot = 15, .material_water = true},  // battle_water_polluted
    {.hash = 0x429AFE64, .world_input = 2, .refraction_slot = 17, .normal_slot = 12, .material_water = true},  // battle_water_polluted
    {.hash = 0xDFA2AEA2, .world_input = 2, .refraction_slot = 18, .normal_slot = 15, .material_water = true},  // battle_river (traced)
    {.hash = 0x9A4DFB07, .world_input = 2, .refraction_slot = 15, .normal_slot = 12, .material_water = true},  // battle_river
    {.hash = 0xE3CA2AAE, .lighting_slot = 3, .world_input = 3, .refraction_slot = 6, .normal_slot = 5},  // fx_waterplane (traced)
    {.hash = 0x598DB0CE, .lighting_slot = 2, .world_input = 3, .refraction_slot = 3, .normal_slot = 2},  // fx_waterplane
    {.hash = 0x928CB5E0, .lighting_slot = 3, .world_input = 3, .refraction_slot = 5, .caustics = false},  // fx_waterplane
    {.hash = 0xE668E3BD, .lighting_slot = 3, .world_input = 3, .refraction_slot = 5, .caustics = false},
    {.hash = 0xEB1CCC82, .lighting_slot = 3, .world_input = 3, .refraction_slot = 5, .caustics = false},
    {.hash = 0x2DD3026D, .lighting_slot = 4, .world_input = 3, .refraction_slot = 8, .caustics = false},
    {.hash = 0xE7FC60E9, .lighting_slot = 4, .world_input = 3, .refraction_slot = 8, .caustics = false},
    {.hash = 0x0468A49E, .lighting_slot = 2, .world_input = 2, .refraction_slot = 6, .caustics = false},  // fx_ocean
    {.hash = 0x1618909A, .lighting_slot = 3, .world_input = 2, .refraction_slot = 9, .caustics = false},
    {.hash = 0x182ADF39, .lighting_slot = 2, .world_input = 2, .refraction_slot = 4, .caustics = false},
}};
// The upscaler in place of the game's TAA (taa_dlss.hpp): 0 the game's TAA, 5 DLSS, 6 FSR, 7 XeSS.
twattila::feature_settings::AaSliders aa_sliders;
constexpr std::array<int, 4> AA_METHODS = {0, 5, 6, 7};
float upscaling_mode_row = 0.f;  // 0 the game's Resolution Scale, 1 to 6 feature_settings::DLSS_MODE_SCALES, 7 Custom
float below_100_detail_row = 1.f;
float lod_bias_trilinear_row = 0.f;
float reflection_lobe_row = 0.f;
float water_motion_row = 0.f;
float placed_objects_row = 1.f;
float rays_hills_out_of_view_row = 0.f;
float rays_terrain_count_later_row = 0.f;
float rays_grass_counts_later_row = 0.f;
float rays_units_out_of_view_row = 1.f;
float rays_skip_cascade_trees_row = 0.f;
float rays_skip_cascades_row = 0.f;
// Trees Out of View up to 150 m: the game draws its trees every frame only into its first shadow map (40 m at the two highest
// Shadows settings; 71 of 75 traces, never into maps 2 to 4, which take them from a cache it fills as the maps scroll). Those
// draws are the rays' only source for a tree the camera does not see, and a cached tree is lost with the helper's scene (ray
// tracing off for 10 s, another upscaler). With the maps skipped their reach costs the GPU nothing: the first map's reach goes
// to just under the second's at every Shadows setting (150 / 250 / 500 m), unless the player's own reach row is set.
float rays_tree_reach_row = 0.f;
// Path-Traced AO's sky as the game's light probes bring it with its Global Illumination on (rt_sun.cs_5_0.hlsl). Off (v40): the
// spherical harmonics' sky, which ATN's logs put at 1/12 to 1/58 of the shade the screen shows ("the game's ambient" against
// "shade" in the helper's Path-Traced AO line): dark, cooked.
float path_probe_sky_row = 1.f;
constexpr float TREE_REACH_METRES = 149.f;
tw::engine_tweaks::Option* first_map_reach = nullptr;  // engine_tests' battle_shadow_split_0
std::atomic_bool tree_reach_logged = false;
float rays_one_at_a_place_row = 0.f;
float rays_banner_light_row = 0.f;
float rays_branch_cut_outs_row = 0.f;
float rays_faded_parts_row = 0.f;
float rays_trunks_as_drawn_row = 0.f;
// The trees' draws into the cascades: 52 M of the shadow pass's indices on the replay's first view, 36 M in the siege (traces c1a, bm_siege).
// Only fog, forward passes and the UI read the maps under Rays Replace Shadow Maps, the sun's shadow comes from rays. fx_rigidbranch_v3,
// fx_rigidleaf_v3; drawn in no other target in any trace (b5_census: no render target).
// Every other caster of the shadow pass after them (batch 6 P4, the 93 traces' census): far tree cards, standing and moving rigid meshes,
// vertexpush, flags, units, tentacles. With the sun's mask from rays and the fog's shadows from rays (Fog Shadows: battlefog02 and 03
// read no map then), who still samples the maps, by their bytes and the traces: the water (2076D289, DFA2AEA2, 034010C4), lit effect
// meshes (9EA8D889), the particles' fog (VS 652893D4) and, through D5DCC85F's depth copy, the particles' shadows (379B000C / 5B535BA0,
// which the rays' mask still reads at t1). Row Skip the Shadow Maps: those lose the sun's shadow.
constexpr std::array<uint32_t, 13> CASCADE_CASTER_SHADERS = {0xD75E6E2B, 0xAF3F74B2, 0x8F975E16, 0x2327ED2B, 0xFA844DB5, 0x6EAC8CE5, 0x7852254A,
                                                             0x53C5A7EC, 0x2B938A0C, 0xBADA4FE3, 0x46E626D5, 0x3434CE02, 0x4E83D6E8};
constexpr size_t CASCADE_TREE_SHADERS = 4;  // the first of them
// The kept (static) maps' array. The game's two cascade arrays swap at each scroll (b5_mom): C883E3FA (vs 4E83D6E8, the
// list's last) copies the kept maps, shifted, out of the last kept array (its t_previous_static_shadows) into the other one,
// which gets the newly shown strips' casters and stays the kept maps (t_static_objects_shadowmap_array, t1 of the half copy
// 800F7F7F) until the next scroll; the last kept array takes the dynamic casters. Skipped with the rest, the copy and the
// strips left the kept maps empty or stale for the rest of the battle once the rows went off (ATN, v40: RT/PT -> Reset,
// a flat picture): only draws into the dynamic maps are skipped, none before the kept array is known.
std::atomic<uintptr_t> kept_maps = 0;
std::atomic_bool cascade_skip_logged = false;
constexpr auto OnCascadeCaster = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const uint32_t* caster = &command.template GetCallbackData<uint32_t>();
  const bool kept_copy = caster == &CASCADE_CASTER_SHADERS.back();
  const bool tree = static_cast<size_t>(caster - CASCADE_CASTER_SHADERS.data()) < CASCADE_TREE_SHADERS;
  if (!kept_copy && ((rays_skip_cascades_row == 0.f && (rays_skip_cascade_trees_row == 0.f || !tree)) || traced_lights::rays_replace_maps_row == 0.f
                     || !twattila::rt_scene::SunShadowsTraced())) {
    return {};
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(0, nullptr, depth_view.GetAddressOf());
  if (!depth_view) return {};
  Microsoft::WRL::ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  const auto target = reinterpret_cast<uintptr_t>(depth_resource.Get());
  if (kept_copy) {
    kept_maps.store(target, std::memory_order_relaxed);
    return {};
  }
  const uintptr_t kept = kept_maps.load(std::memory_order_relaxed);
  if (kept == 0 || target == kept) return {};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return {};
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  if (depth_desc.ArraySize != 4) return {};
  if (!tree && !cascade_skip_logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i(std::format("[Warhammer III] First run: a draw into the shadow maps skipped, vs {:08X}, the maps {}x{}.",
                                      command.template GetCallbackData<uint32_t>(), depth_desc.Width, depth_desc.Height));
  }
  return {.bypass = true};
};

// Shadow Map Size above the game's highest needs three numbers no trace holds (batch 6 P4): the size in the shared shadow constants
// (the sun's mask's line), the static maps' copy's (C883E3FA's static_shadowmap_CB: current_size and buffer_size at bytes 32 and 40;
// it draws only when the game renews its static maps) and what the two half-size copies read and write (800F7F7F, D5DCC85F: `x 2` in
// their code, so a source twice their target). With Log Details, once each, at their next draw.
constexpr std::array<uint32_t, 3> SHADOW_SIZE_SHADERS = {0xC883E3FA, 0x800F7F7F, 0xD5DCC85F};
std::array<std::atomic_bool, SHADOW_SIZE_SHADERS.size()> shadow_size_logged = {};
constexpr auto OnShadowSizeDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& hash = command.template GetCallbackData<uint32_t>();
  // The half copy's t1: the kept maps (OnCascadeCaster), from the first frame on.
  if (hash == 0x800F7F7F) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> kept;
    if (context != nullptr) context->PSGetShaderResources(1, 1, kept.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    if (kept) kept->GetResource(resource.GetAddressOf());
    if (resource) kept_maps.store(reinterpret_cast<uintptr_t>(resource.Get()), std::memory_order_relaxed);
  }
  if (rt_sliders.debug_log == 0.f) return {};
  if (shadow_size_logged[&hash - SHADOW_SIZE_SHADERS.data()].exchange(true, std::memory_order_relaxed)) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  const auto size_of = [](ID3D11View* view) {
    if (view == nullptr) return std::string("none");
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) return std::string("not a texture");
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    return std::format("{}x{} [{}] format {}", desc.Width, desc.Height, desc.ArraySize, static_cast<int>(desc.Format));
  };
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colour;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
  native->OMGetRenderTargets(1, colour.GetAddressOf(), depth.GetAddressOf());
  std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> inputs;
  native->PSGetShaderResources(0, 1, inputs[0].GetAddressOf());
  native->PSGetShaderResources(1, 1, inputs[1].GetAddressOf());
  UINT viewports = 1;
  D3D11_VIEWPORT viewport = {};
  native->RSGetViewports(&viewports, &viewport);
  std::string constants;
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  native->PSGetConstantBuffers(0, 1, buffer.GetAddressOf());
  if (hash == 0xC883E3FA && buffer) {
    for (const float value : twattila::frame_trace::ReadBuffer<float>(native, buffer.Get(), 0, 12)) {
      constants += std::format(" {:g}", value);
    }
  }
  renodx::utils::log::i(std::format("[Warhammer III] Shadow map sizes: ps {:08X}: colour {}, depth {}, viewport {:g}x{:g}, t0 {}, t1 {}{}{}.", hash,
                                    size_of(colour.Get()), size_of(depth.Get()), viewport.Width, viewport.Height, size_of(inputs[0].Get()),
                                    size_of(inputs[1].Get()), (constants.empty() ? "" : ", b0's first 12 floats"), constants));
  return {};
};

// Graphics 1 of 2's (twattila/graphics_page.hpp): what is true here.
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = {"Game's TAA", "Game's TAA", "Game's TAA", "Game's TAA", "Game's TAA", "DLSS", "FSR (Experimental)", "XeSS (Experimental)"},
    .aa_tooltip = "An upscaler in place of the game's TAA: keep in-game Anti-aliasing on TAA or TAA High.\n"
                  "Upscaling Mode: the rows below.\n"
                  "DLSS: NVIDIA's (RTX). FSR: AMD's, FSR 4 on Radeon RX 6000 and newer, else 3.1. XeSS: Intel's, any GPU.\n"
                  "Every ray-traced effect needs an upscaler. Needs renodx-tw-dlaa64.exe and its DLLs next to Warhammer3.exe.",
    .in_game_aa_note = "In-game Anti-Aliasing must be TAA or TAA High for DLSS, FSR, XeSS and Frame Generation (restart after changing it).",
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its ambient occlusion\n"
                     "and shadows. The rest at its default.",
    .no_rays_tooltip = "Bend Screen-Space Shadows (Length 480), GT-VBAO and GT-VBGI at their Recommended settings.\n"
                       "The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows, reflections and ambient occlusion, path-traced Global Illumination. The rest of this\n"
                    "page at its default. Needs an upscaler (Anti-Aliasing above).\n"
                    "On NVIDIA also turns DLSS Ray Reconstruction on. Elsewhere Experimental, slower: its own denoisers.",
    .shadow_tooltip = "Game's Shadow Maps: the original. Bend Screen-Space Shadows: those plus fine contact shadows from\n"
                      "what is on screen (Bend Studio's; soldiers, weapons, grass; needs in-game shadows on).\n"
                      "DXR: DirectX Raytracing, the sun's shadow from rays too, on top of the game's, in place of Bend's.\n"
                      "Needs an upscaler.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep the in-game ambient occlusion on.\n"
                  "Game's SSAO: everything counts as solid, so soldiers and trees get dark halos.\n"
                  "GT-VBAO (Ground Truth AO with a visibility bitmask): objects have a thickness, so light gets behind\n"
                  "thin ones.\n"
                  "RTAO: from rays through the scene; the game's isn't computed then. Needs an upscaler.",
    .ao_paths_label = "Path-Traced (ReSTIR GI, Experimental)",
    .specular_occlusion_label = "Ray-Traced Specular Occlusion (Experimental)",
    .water_caustics_label = "Water Caustics (Experimental)",
    .water_reflections_label = "Water Reflections (Experimental)",
    .water_reflections_tooltip = "Rays on rivers and lakes too, also what is off screen; far hills can show as dark patches.\n"
                                 "Best with ray-traced Global Illumination on. Off: the game's reflections there.",
    .fog_shadows_label = "Fog Shadows (Experimental)",
    .fog_shadows_tooltip = "Light shafts from rays in place of the game's own in the fog: it loses the sun's glow in the shadow of\n"
                           "buildings, trees and hills, measured over the first 400 m. Some fog stays as the game draws it.",
    .light_glow_label = "Torch and Fire Light in Fog (Experimental)",
    .light_glow_tooltip = "Torches and fires light the fog around them, with their shafts and shadows in it. Needs Fog Shadows. 0: off.",
    .gi_tooltip_end = "\nOn, it replaces the game's own bounce light; the sky's light stays the game's. Off: the game's.",
    .coloured_shadows_tooltip = "Sunlight through capture flags takes their colour.\n"
                                "Needs Light Through Leaves, and DLSS Ray Reconstruction or path-traced Global Illumination.",
    .rays_a_pixel_shadows_tooltip = "The sun's shadows take the extra rays.",
    .rays_a_pixel_paths_tooltip = "Ray-traced and path-traced Global Illumination take the extra rays (up to 16 a pixel). The heaviest.",
    .object_motion_tooltip = "Flags, glowing weapons and swaying props tell the upscaler how they move.\nOff: the game's own.",
    .effects_at_output_tooltip = "While the upscaler renders below 100%, Light Bounce, ray-traced AO's rate and Bend's shadow softness\n"
                                 "keep their 100% detail, and at half or below the water's reflections too. Looks better, can cost some performance.",
    .dlss_preset_tooltip = "NVIDIA's models: K the first transformer, M the second, L the sharpest.\n"
                           "Below 100% L and M come out darker than K (L up to 10% at half).",
    .reflections_tooltip = "Reflections. Need in-game Screen Space Reflections on.\n"
                           "Game's SSR: the original, on water only. All Surfaces: the game's own march on armour, metal and the rest too,\n"
                           "no rays needed; costs more, may shimmer. DXR: from rays, also what is off screen or hidden. Needs an upscaler.",
    .reflections_middle_label = "All Surfaces (Experimental)",
    .reflections_rays_label = "Ray-Traced (DXR, Experimental)",
};

renodx::utils::settings::Setting* graphics_status = nullptr;
// The pixel trace's position, % of the screen's width and height.
std::array<float, 2> traced_pixel = {50.f, 50.f};
bool graphics_found = false;
// The graphics watch's values (4 hardware breakpoints).
constexpr std::array<std::string_view, 4> WATCHED_VALUES = {"battle_cull_aabb_at_num_pixels_unit", "campaign_cull_aabb_at_num_pixels_tree", "grass_density_multiplier", "terrain_tessellation_quality_battle"};

bool configured = false;
bool attached = false;
bool addon_registered = false;

}  // namespace

// Lifecycle /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  {
    const auto desc = swapchain->get_device()->get_resource_desc(swapchain->get_back_buffer(0));
    render_scale::back_buffer_width = desc.texture.width;
    render_scale::back_buffer_height = desc.texture.height;
  }

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

// Its camera constants at their places in Attila's camera_VS_PS (frame_history::AttilaCamera) for the
// screen-space shadows and the AO filter (destination, source, bytes).
constexpr std::array<twattila::frame_history::CameraRun, 7> CAMERA_LAYOUT = {{
    {.destination = 0, .source = 0, .bytes = 12},
    {.destination = 16, .source = 32, .bytes = 192},
    {.destination = 208, .source = 288, .bytes = 64},
    {.destination = 272, .source = 416, .bytes = 148},
    {.destination = 420, .source = 576, .bytes = 12},
    {.destination = 432, .source = 592, .bytes = 20},
    {.destination = 464, .source = 624, .bytes = 16},
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl's sun light draws, one shader for every model (g_skin_enable), the camera at b1,
// the G-buffer's channels 1-4 from their first slot.
constexpr std::array<twattila::light_bounce::SunDraw, 7> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0x18A84893, .standard = true, .gbuffer_slot = 10, .camera_slot = 1, .lighting_slot = 4, .camera_layout = CAMERA_LAYOUT, .ao_slot = 4, .ssao_constants_slot = 3, .ambient_final_sites = 2, .sky_harmonics_slot = 5, .environment_slot = 0},
    {.shader_hash = 0x24F97B7B, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1, .ssao_constants_slot = 2, .ambient_final_sites = 1, .diffuse_rule = 1.f, .sky_harmonics_slot = 2, .environment_slot = 0},
    {.shader_hash = 0x76A707E5, .standard = true, .gbuffer_slot = 6, .camera_slot = 1, .camera_layout = CAMERA_LAYOUT, .ambient_final_sites = 1, .diffuse_rule = 2.f, .sky_harmonics_slot = 1, .environment_slot = 0},
    {.shader_hash = 0x9EAD8951, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1, .ssao_constants_slot = 2, .ambient_final_sites = 1, .diffuse_rule = 1.f, .sky_harmonics_slot = 2, .environment_slot = 0},
    {.shader_hash = 0xCCEF2C1E, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1, .ssao_constants_slot = 2, .ambient_final_sites = 1, .diffuse_rule = 1.f, .sky_harmonics_slot = 2, .environment_slot = 0},
    {.shader_hash = 0xF0E2D5EC, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1, .ssao_constants_slot = 2, .ambient_final_sites = 1, .diffuse_rule = 1.f, .sky_harmonics_slot = 2, .environment_slot = 0},
    {.shader_hash = 0xF9F8CE80, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1, .ssao_constants_slot = 2, .ambient_final_sites = 2, .sky_harmonics_slot = 2, .environment_slot = 0},
}};

// Ray tracing's scene (twattila/rt_scene.hpp): the G-buffer's opaque meshes as the game's own vertex shaders draw them
// (their SV_Position streamed out: WH3 has no capture versions), units (skinned, character alpha), flags and grass every
// frame. Not in it: billboards (cards facing the camera: their shadow would turn with it), decals and water, and what has no
// surface: fx_rigidterrain's VS 5767F87C draws boxes (a unit cube between an instance's two corners) in which its PS 568F3F05,
// E6CD215D, 7070E23D, 9A818169 read the depth, discard outside and paint the terrain's materials. In the scene their tops
// lay over the ground: black patches in every ray. The ground is 709BC4D7.
// The grass's vertex shader serves three families: by their pixel shaders' clip, the opacity is t_xml_base_colour at t2 or
// t0; grass rock (no clip) and the depth-only versions stay out.
constexpr std::array<twattila::rt_scene::PixelMask, 4> GRASS_MASKS = {{
    {.pixel_shader = 0xBD3CC4F1, .slot = 2},  // rigid_battle_grass
    {.pixel_shader = 0xD603D107, .slot = 2},
    {.pixel_shader = 0x8DF14BBA, .slot = 0},  // rigid_battle_basic_grass
    {.pixel_shader = 0x87AC76BF, .slot = 0},
}};

// The terrain's pixel shaders and where they read t_tile_mask_map (rt_terrain_keep): what they discard stays out of the scene.
constexpr std::array<twattila::rt_scene::PixelMask, 2> TERRAIN_MASKS = {{
    {.pixel_shader = 0x4A87AE19, .slot = 2},  // its pass with the G-buffer's depth
    {.pixel_shader = 0xBB65FB61, .slot = 8},  // the campaign's
}};

// The ground's other meshes, A40D7DDB (fx_rigidterrain's blended tiles on the height map: the campaign's most, some battle maps'): their first
// draw writes the depth; its PS 1E05843F only blends them in again.
constexpr std::array<twattila::rt_scene::PixelMask, 2> GROUND_MESH_PASSES = {{
    {.pixel_shader = 0xA1C85113},
    {.pixel_shader = 0x90FBDD61},
}};

constexpr std::array<twattila::rt_scene::SceneShader, 46> SCENE_SHADERS = {{
    {.hash = 0xF0395CC4, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_rigid, .faded_rows_shader = __rt_rows_rigid_faded, .cut_out_shader = __rt_capture_rigid_cut_out, .clip_cut_outs = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .key_family = 0xF0395CC4},  // fx_rigidanisotropic
    {.hash = 0x8EE34EC5, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_rigid, .faded_rows_shader = __rt_rows_rigid_faded, .cut_out_shader = __rt_capture_rigid_cut_out, .clip_cut_outs = true, .moving = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .key_family = 0x8EE34EC5},  // fx_rigidambientmapfiremesh
    // Buildings, tents, shrubs, mountains, rocks: the same position math as F0395CC4, by their bytes. 4F35D09F also draws water, decals and effects: its listed pixel shaders only.
    {.hash = 0x4F35D09F, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_rigid, .faded_rows_shader = __rt_rows_rigid_faded, .cut_out_shader = __rt_capture_rigid_cut_out, .clip_cut_outs = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .pixel_masks = twwarhammer3::BUILDING_MASKS, .key_family = 0xF0395CC4},
    // Rocks. A rock's second draw into the terrain's targets (087C74E6, A5A266C3) only paints its blend: the same mesh again, not listed.
    {.hash = 0x41AC9CB1, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_rigid, .faded_rows_shader = __rt_rows_rigid_faded, .key_family = 0xF0395CC4},  // rigid_battle_rock_terrain_blend
    {.hash = 0x33AF7AE5, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_rigid, .faded_rows_shader = __rt_rows_rigid_faded, .key_family = 0xF0395CC4},  // rigid_battle_rock
    {.hash = 0x70F4C9F9, .dynamic = true},  // rigid_campaign_mountain: its height comes from a buffer by the instance
    {.hash = 0x474F97FB, .capture_shader = __rt_capture_branch, .rows_shader = __rt_rows_tree, .second_rows = true, .dynamic_row = &rays_trunks_as_drawn_row, .cut_out_shader = __rt_capture_branch_cut_out, .clip_cut_outs = true, .cut_out_row = &rays_branch_cut_outs_row, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .key_family = 0x474F97FB},                    // fx_rigidbranch_v3
    // Trees with branches moved by the simulation (structured buffers by the instance): every frame.
    {.hash = 0x31584506, .clip_cut_outs = true, .cut_out_row = &rays_branch_cut_outs_row, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2},  // fx_rigidbranch_v3
    {.hash = 0x9498C59D, .dynamic = true, .foliage = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .mask_slot = 1},  // fx_rigidleaf_v3
    {.hash = 0xC0ED2883, .dynamic = true, .thin_row = &rays_banner_light_row},   // fx_rigidtextureblend: unit parts, placed from a buffer out of a capture's key: kept once, stale copies through the banners
    // fx_rigidtextureblend's rigid version on the campaign (17 of 95 traces, G-buffer colour and depth, 12 indices a draw): rows x vertex,
    // then cb0[10-13] (its bytes), as drawn.
    {.hash = 0x03298FD5, .dynamic = true},
    // The vertex-push tentacles of sieges (3 traces, 34-38 k indices): moved by time and wind after the rows, so as drawn; cut out by
    // 6A5EEE23's t0 alpha at TEXCOORD2 (cut_out_masks). Their shadow pass's 7852254A leaves out what is in the view, at rest.
    {.hash = 0x1C3500F0, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2},
    // The campaign's ground where the tessellated terrain's mask leaves it: tiles on the height map (a pass with no holes), every frame as they're culled by the view.
    {.hash = 0x15FE8427, .terrain = true, .dynamic = true},
    {.hash = 0xA40D7DDB, .terrain = true, .dynamic = true, .pixel_masks = GROUND_MESH_PASSES},
    // The campaign's trees and bushes (rows as the rigid objects', by their bytes; wind and the lean toward the camera left out, as Attila's), the plants solid.
    {.hash = 0xD025F795, .capture_shader = __rt_capture_rigid_cut_out, .rows_shader = __rt_rows_rigid, .foliage = true, .still = true, .mask_slot = 2},
    {.hash = 0xBDAEC4F3, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_rigid},
    // fx_rigidtreebillboard, far trees: their alpha at t3 (t0 the burn map, t1 a dissolve map).
    {.hash = 0xCC8297ED, .capture_shader = __rt_capture_far_tree_positions, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 3},
    {.hash = 0x01830D95, .capture_shader = __rt_capture_far_tree, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 3},
    {.hash = 0x709BC4D7, .terrain = true, .dynamic = true, .tessellated = true, .pixel_masks = TERRAIN_MASKS, .tessellated_keep = __rt_terrain_keep, .tessellated_outside = __rt_terrain_outside},  // fx_rigidterraintessellation (the hills): every frame, as its hull shader tessellates and culls by the camera
    {.hash = 0xB8C567C3, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},  // fx_weighted (skinned units)
    {.hash = 0x2C45437C, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_rigid, .faded_rows_shader = __rt_rows_rigid_faded, .cut_out_shader = __rt_capture_rigid_cut_out, .clip_cut_outs = true, .moving = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .key_family = 0x8EE34EC5},  // rigid_character_alpha
    {.hash = 0x3193A71F, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},  // fx_weighted's 4-bone units
    // The units' other vertex shaders (position through cb0[10-13], the UV at TEXCOORD1, by their bytes): in no trace so far.
    {.hash = 0x2A64D0C8, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},  // fx_weighted, burn
    {.hash = 0xF8CB9DB6, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},
    {.hash = 0x7B298C37, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},  // fx_weightedcommon
    {.hash = 0x22B5CCC7, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},  // weighted, wind alpha
    {.hash = 0x033108E7, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},
    {.hash = 0x3689A926, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},
    {.hash = 0x63D8D532, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},  // weighted, nurgle
    {.hash = 0xDB3589E3, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},
    // Cloth: rigid wind planes, sheets waving by the time: in no trace so far.
    {.hash = 0x233A0281, .clip_cut_outs = true, .dynamic = true, .thin = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2},
    {.hash = 0x219075F3, .clip_cut_outs = true, .dynamic = true, .thin = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2},
    {.hash = 0x72CDA7A1, .clip_cut_outs = true, .dynamic = true, .thin = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2},
    // shaders_rigid_ui_capture_flag_xml (waves by the time in b0, out of a capture's key). A sheet: Coloured Shadows' filter.
    {.hash = 0x444F1190, .dynamic = true, .thin = true},
    // fx_rigidleaf_v3: its clip reads the base colour at t1 (t0 is the burn map, the same texture until the tree burns).
    {.hash = 0x783383C6, .capture_shader = __rt_capture_leaf, .rows_shader = __rt_rows_tree, .second_rows = true, .foliage = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .mask_slot = 1, .key_family = 0x783383C6},
    // fx_weighted in the shadow pass (o0 = cb0[10-13] x the world position, o2 that position, by their bytes): drawn for casters whose
    // shadow falls in view, behind the camera too (trace c1a, bm_siege: depth 4096x4096 [4], PS none, 642CCC99, B2358145). Solid, as a unit without its mask.
    // Cut out by the shadow pass's own mask with the row (B2358145: t0's alpha at TEXCOORD1, cut_out_masks), their UV through the second shader.
    {.hash = 0xBADA4FE3, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1, .cascade_keep = __rt_outside_view, .cascade_keep_uv = __rt_outside_view_uv},
    {.hash = 0x46E626D5, .clip_cut_outs = true, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1, .cascade_keep = __rt_outside_view, .cascade_keep_uv = __rt_outside_view_uv},
    // Every other caster family in the shadow pass (traces c1a, bm_siege, bm_gates, bm_battle, c5). Placed from their rows: a mesh's BLAS once, shared with the main pass's by key_family when
    // the main pass has it; 61 M tree indices a frame are some hundred BLASes and their rows. Their rows leave out what's in the view. Same position math as their main pass's, by their bytes.
    // Rigid, standing (no last rows, stride 120); shrubs, rocks, walls, props. Masks: 10BB9F8C, 8071B918, 6CBF7D90 read t0's alpha at TEXCOORD0; E55FDA7B and none are solid.
    {.hash = 0x6EAC8CE5, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_cascade_rigid, .cut_out_shader = __rt_capture_rigid_cut_out, .clip_cut_outs = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .cascade_keep = __rt_outside_view, .key_family = 0xF0395CC4},
    {.hash = 0x7852254A, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_cascade_rigid, .cut_out_shader = __rt_capture_rigid_cut_out, .clip_cut_outs = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .cascade_keep = __rt_outside_view, .key_family = 0xF0395CC4},  // vertexpush, at rest
    // Rigid with last rows (stride 168): 8EE34EC5's and 2C45437C's. Weapons, gates: placed every frame, kept 300 frames (rt_place's MOVED_KEPT_FRAMES).
    {.hash = 0x53C5A7EC, .capture_shader = __rt_capture_rigid, .rows_shader = __rt_rows_cascade_rigid, .cut_out_shader = __rt_capture_rigid_cut_out, .clip_cut_outs = true, .moving = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .cascade_keep = __rt_outside_view, .key_family = 0x8EE34EC5},
    // Trees, at rest. Branches as the main pass's (solid, or cut out by the row: 637E69EE reads the texture B0A4E5B8 reads at t1); leaves by t0's alpha (CE714A19), the texture 783383C6 reads at t1.
    {.hash = 0xD75E6E2B, .capture_shader = __rt_capture_cascade_branch, .rows_shader = __rt_rows_cascade_tree, .cut_out_shader = __rt_capture_cascade_leaf, .clip_cut_outs = true, .cut_out_row = &rays_branch_cut_outs_row, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .cascade_keep = __rt_outside_view, .key_family = 0x474F97FB},
    {.hash = 0x8F975E16, .capture_shader = __rt_capture_cascade_branch, .rows_shader = __rt_rows_cascade_tree, .cut_out_shader = __rt_capture_cascade_leaf, .clip_cut_outs = true, .cut_out_row = &rays_branch_cut_outs_row, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .cascade_keep = __rt_outside_view, .key_family = 0x474F97FB},
    {.hash = 0xAF3F74B2, .capture_shader = __rt_capture_cascade_leaf, .rows_shader = __rt_rows_cascade_tree, .foliage = true, .cascade_keep = __rt_outside_view, .key_family = 0x783383C6},
    {.hash = 0x2327ED2B, .capture_shader = __rt_capture_cascade_leaf, .rows_shader = __rt_rows_cascade_tree, .foliage = true, .cascade_keep = __rt_outside_view, .key_family = 0x783383C6},
    // Flags, waving by a time this cb0 leaves 0: still. Streamed as the units', small enough (0.5 M).
    {.hash = 0x2B938A0C, .dynamic = true, .thin = true, .cascade_keep = __rt_outside_view},
    {.hash = 0xA4527F25, .dynamic = true, .foliage = true, .grass = true, .indirect = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 2, .pixel_masks = GRASS_MASKS},
}};

// The screen-space shadows' Light Through Banners: cloth, banners and flags, the unit parts with Light Through All Banners (as the rays).
constexpr std::array<twattila::screen_space_shadows::ThinShader, 5> THIN_SHADERS = {{
    {.hash = 0x233A0281},
    {.hash = 0x219075F3},
    {.hash = 0x72CDA7A1},
    {.hash = 0x444F1190},
    {.hash = 0xC0ED2883, .row = &rays_banner_light_row},
}};

// Ray tracing's view of the game: DLSS's camera (taa_dlss.hpp; uploaded jittered, as the scene and its depth were drawn)
// and render size; the G-buffer guides (WriteGuides, at the sun draws before the trace).
const twattila::rt_scene::Host RT_HOST = {
    .camera = [](ID3D11Buffer* buffer, twattila::rt_scene::Camera* camera) {
      std::array<float, 66 * 4> c = {};
      if (!twwarhammer3::taa_dlss::MainCamera(buffer, &c)) return false;
      std::array<float, 16> view_projection = {};
      std::copy_n(c.begin() + (10 * 4), 16, view_projection.begin());
      *camera = {
          .view_projection = view_projection,
          .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
          .position = {c[0], c[1], c[2]},
      };
      return true;
    },
    .render_size = twwarhammer3::taa_dlss::RenderSize,
    .guides_written = twwarhammer3::taa_dlss::GuidesWritten,
    .main_view = twattila::frame_history::MainViewport,
    .rewrite_guides = [](ID3D11DeviceContext* native) { twwarhammer3::taa_dlss::RewriteGuides(native, reflections::march_gbuffer, reflections::march_camera.Get()); },
    .light_copy = __rt_light_copy,
    .placed_row = &placed_objects_row,
    .keep_placed = true,
    // All 8 light PS scale their diffuse by this (and specular not at all); the paths light what they meet by the diffuse's rule.
    .local_light_scale = 7.615242e-05f,
    // A tree on the map stands about 1.7 units (camera 4.97 over the sea, its height on screen), one in a battle about 10 m.
    .campaign_metre = 0.1f,
    .faded_row = &rays_faded_parts_row,
    .faded_pixel_shaders = twwarhammer3::FADED_PIXEL_SHADERS,
};

// What only the campaign map draws: its borders, the shroud's plane (rt_positions: its sea's level).
constexpr std::array<uint32_t, 2> CAMPAIGN_SHADERS = {0x2F565F7A, 0x959936FE};
constexpr auto OnCampaignDraw = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  twattila::frame_history::campaign_frames.store(2, std::memory_order_relaxed);
  return {};
};

std::array<std::atomic_bool, LIGHT_BOUNCE_DRAWS.size()> sun_draw_logged = {};
std::array<std::atomic_bool, LIGHT_BOUNCE_DRAWS.size()> sun_values_logged = {};

// Where a sun draw finds its AO: the game's has its own overscan (below about 2/3 scale it fills a part of its texture, at the
// render's pixels), so the draw reads uv x ssao_overscan / warscape_3d_overscan. The AO filter's and rt_ambient_combine's
// textures hold each pixel where the picture has it: read in the game's place, both overscans are 1 for that draw.
namespace sun_ao {
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> game_view;  // the sun draw's, before anything of ours replaced it
Microsoft::WRL::ComPtr<ID3D11Buffer> game_constants;
Microsoft::WRL::ComPtr<ID3D11Buffer> one_to_one;

void OnDestroyDevice(reshade::api::device*) {
  game_view.Reset();
  game_constants.Reset();
  one_to_one.Reset();
}

// After ray tracing's and the AO filter's callbacks, before the Light Bounce's (which draws the sun draw itself: finalize).
constexpr auto OnSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& draw = command.template GetCallbackData<twattila::light_bounce::SunDraw>();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> bound;
  native->PSGetShaderResources(draw.ao_slot, 1, bound.GetAddressOf());
  if (!bound) return {};
  const bool replaced = bound != game_view;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
  native->PSGetConstantBuffers(draw.ssao_constants_slot, 1, constants.GetAddressOf());
  // The Debug View's Screen-Space AO: what the draw reads, but for the traced visibility in the combine's.
  if (rt_sliders.debug_view >= 8.f && constants && twattila::frame_history::MainViewport(native)) {
    if (replaced && rt_sliders.ambient_occlusion == 0.f) {
      twattila::rt_scene::KeepDebugAmbient(native, bound.Get(), 1.f);
    } else if (const auto overscans = twattila::frame_trace::ReadBuffer<float>(native, constants.Get(), 0, 2); overscans.size() == 2) {
      twattila::rt_scene::KeepDebugAmbient(native, game_view.Get(), overscans[1] / overscans[0]);
    }
  }
  if (!replaced) return {};
  if (!one_to_one) {
    constexpr std::array<float, 4> OVERSCANS = {1.f, 1.f, 0.f, 0.f};
    const D3D11_BUFFER_DESC desc = {.ByteWidth = sizeof(OVERSCANS), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = OVERSCANS.data()};
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    if (FAILED(device->CreateBuffer(&desc, &data, one_to_one.GetAddressOf()))) return {};
  }
  game_constants = constants;
  native->PSSetConstantBuffers(draw.ssao_constants_slot, 1, one_to_one.GetAddressOf());
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&post_native);
    if (post_native == nullptr) return;
    post_native->PSSetConstantBuffers(post.template GetCallbackData<twattila::light_bounce::SunDraw>().ssao_constants_slot, 1, game_constants.GetAddressOf());
  }};
};
}  // namespace sun_ao

// The G-buffer guides (taa_dlss.hpp) at the sun light draws: the frame's first writes them.
constexpr auto OnSunDrawGuides = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& draw = command.template GetCallbackData<twattila::light_bounce::SunDraw>();
  if (!sun_draw_logged[&draw - LIGHT_BOUNCE_DRAWS.data()].exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i(std::format("[Warhammer III] First run: sun light draw, ps {:08X}.", draw.shader_hash));
    twattila::test_hooks::StartClock();
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  if (draw.ao_slot != UINT_MAX) native->PSGetShaderResources(draw.ao_slot, 1, sun_ao::game_view.ReleaseAndGetAddressOf());
  twwarhammer3::taa_dlss::WriteGuides(native, draw.gbuffer_slot, draw.camera_slot, draw.diffuse_rule);
  if (draw.diffuse_rule != 0.f && twattila::frame_history::MainViewport(native)) {
    if (draw.diffuse_rule == 1.f) {
      reflections::sun_standard = true;
    } else {
      reflections::sun_low_quality = true;
    }
  }
  // Log Details, once per shader: what this place's ambient is made of (lighting_VS_PS: its values can't be read offline).
  if (rt_sliders.debug_log != 0.f && twattila::frame_history::MainViewport(native)
      && !sun_values_logged[&draw - LIGHT_BOUNCE_DRAWS.data()].exchange(true, std::memory_order_relaxed)) {
    Microsoft::WRL::ComPtr<ID3D11Buffer> lighting;
    native->PSGetConstantBuffers(draw.lighting_slot, 1, lighting.GetAddressOf());
    D3D11_BUFFER_DESC desc = {};
    if (lighting) lighting->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Buffer> copy;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (desc.ByteWidth >= 304 && SUCCEEDED(device->CreateBuffer(&desc, nullptr, copy.GetAddressOf()))) {
      native->CopyResource(copy.Get(), lighting.Get());
      if (SUCCEEDED(native->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        const auto* c = static_cast<const float*>(mapped.pData);
        const auto* flags = static_cast<const uint32_t*>(mapped.pData);
        renodx::utils::log::i(std::format(
            "[Warhammer III] Sun shader {:08X}: spherical harmonics {}, Global Illumination {}, SSR {}; ambient cube's scale {:g}, leaf bias {:g}, "
            "SH terms {:g}, SH fade {:g}; cube +x {:g} {:g} {:g}, -x {:g} {:g} {:g}, +y {:g} {:g} {:g}, -y {:g} {:g} {:g}, +z {:g} {:g} {:g}, -z {:g} {:g} {:g}; "
            "sun colour {:g} {:g} {:g}.",
            draw.shader_hash, flags[46], flags[45], flags[44], c[75], c[72], c[73], c[74], c[8], c[9], c[10], c[12], c[13], c[14], c[16], c[17], c[18],
            c[20], c[21], c[22], c[24], c[25], c[26], c[28], c[29], c[30], c[4], c[5], c[6]));
        native->Unmap(copy.Get(), 0);
      }
    }
  }
  return {};
};

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  if (auto& campaign_frames = twattila::frame_history::campaign_frames; campaign_frames.load(std::memory_order_relaxed) != 0) {
    campaign_frames.fetch_sub(1, std::memory_order_relaxed);
  }
  talesofarise::pixel_trace::OnPresent(queue);
  twattila::pass_timing::OnPresent(queue, "wh3");
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twwarhammer3::vfx_particles::OnPresent();
  reflections::sun_standard = false;
  reflections::sun_low_quality = false;
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  // Before any setting is read this frame: DLSS never starts on another vendor's GPU, a saved DLSS is XeSS on Intel's, else FSR.
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  // A saved method the row doesn't list: the game's TAA.
  if (aa_sliders.method >= 1.f && aa_sliders.method <= 4.f) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", 0.f}});
  }
  // A setup saved with both (the older rows allowed it): ray-traced AO multiplies what runs, the page can't undo GT-VBAO under it.
  if (rt_sliders.ambient_occlusion != 0.f && shader_injection.ssao_mode >= 1.f) {
    renodx::utils::settings::UpdateSettings({{"ssao_mode", 0.f}});
  }
#if FIX_BLOOM
  // A third-party DLSS mod runs its own upscale: Upscaling Mode leaves it alone.
  const bool third_party_upscale = bloom_fix::upscaled.load(std::memory_order_relaxed);
#else
  const bool third_party_upscale = false;
#endif
  // Upscaling Mode: exact sizes, as the game truncates some of its sizes and rounds up others. Before anything reads this frame's size.
  float wanted_scale = 0.f;
  // Nothing would take the game's picture to the display: without the helper, or without a TAA pass to replace, the scale stays the game's.
  const bool no_upscaler = twwarhammer3::taa_dlss::InGameNoTaa() || twattila::dlaa_bridge::bridge.failed;
  const auto back_buffer = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
  if (upscaling_mode_row != 0.f && twattila::feature_settings::Upscaling(aa_sliders) && !third_party_upscale && !no_upscaler) {
    const auto mode = static_cast<size_t>(upscaling_mode_row) - 1;
    wanted_scale = twattila::frame_history::AspectScale(back_buffer.texture.width, back_buffer.texture.height,
                                                        (mode < twattila::feature_settings::DLSS_MODE_SCALES.size())
                                                            ? twattila::feature_settings::DLSS_MODE_SCALES[mode]
                                                            : std::clamp(aa_sliders.render_scale, static_cast<float>(twattila::feature_settings::MIN_CUSTOM_SCALE), 100.f) / 100.f);
  }
  if (twwarhammer3::resolution_scale::Drive(wanted_scale) && wanted_scale > 0.f) {
    twwarhammer3::taa_dlss::ExpectSize({static_cast<UINT>(static_cast<float>(back_buffer.texture.width) * wanted_scale), static_cast<UINT>(static_cast<float>(back_buffer.texture.height) * wanted_scale)},
                                       {back_buffer.texture.width, back_buffer.texture.height});
  }
  auto rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength, twwarhammer3::taa_dlss::RenderShare());
  rt_settings.ambient_alone = true;  // RTAO never multiplies the game's SSAO.
  // Path-Traced AO takes the sun's diffuse ambient (t51); what the sun still lights from the sky (the cube's reflection) takes RTAO
  // from the same trace, in the AO slot the game's SSAO left.
  rt_settings.ambient_occlusion = rt_settings.ambient_occlusion || rt_settings.path_ambient;
  rt_settings.path_probe_sky = path_probe_sky_row != 0.f;
  twattila::texture_lod_bias::SetWrappingTrilinear(lod_bias_trilinear_row != 0.f);
  shader_injection.shadow_map_scale = twwarhammer3::shadow_map_size::Scale();
  rt_settings.cheap_fog_alone = true;
  rt_settings.reflection_squared_lobe = reflection_lobe_row != 0.f;
  rt_settings.reflection_fog = reflections::fog_row != 0.f;
  rt_settings.water_reflections = true;  // Row off: the resolve leaves water to the march (reflections::OnMarch). The rays still leave from it.
  rt_settings.terrain_outside_view = rays_hills_out_of_view_row != 0.f;
  rt_settings.terrain_count_later = rays_terrain_count_later_row != 0.f;
  rt_settings.indirect_counts_later = rays_grass_counts_later_row != 0.f;
  // Under rays and maps together the game's cascades already darken what these would.
  rt_settings.casters_out_of_view = rt_settings.sun_shadows && rays_units_out_of_view_row != 0.f;
  rt_settings.one_at_a_place = rt_settings.casters_out_of_view && rays_one_at_a_place_row != 0.f;
  // Ray-traced shadows replace the screen-space ones too.
  auto sss_settings = twattila::feature_settings::SssSettings(sss_sliders);
  sss_settings.enabled = sss_settings.enabled && !rt_settings.sun_shadows;
  twattila::screen_space_shadows::Update(swapchain, sss_settings);
  // GT-VBAO's Temporal Smoothing (its noise moving each frame) and Denoiser (twattila/ao_filter.hpp).
  const bool bitmask_ao = shader_injection.ssao_mode >= 1.f;
  static uint32_t ao_frame = 0;
  shader_injection.ssao_frame =
      (bitmask_ao && shader_injection.ssao_temporal != 0.f) ? static_cast<float>(++ao_frame % 64) : 0.f;
  twattila::ao_filter::Update(swapchain, {.temporal = bitmask_ao && shader_injection.ssao_temporal != 0.f,
                                          .edge_fix = shader_injection.ssao_edge_fix != 0.f,
                                          .denoise = bitmask_ao && shader_injection.ssao_denoise != 0.f});
  tw::ao_trace::OnPresent();
  twattila::rt_scene::Update(rt_settings);
  // A DLSS Preset saved at v38's Auto (3) loads as L (the row's last choice).
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twwarhammer3::taa_dlss::Update(swapchain, aa_settings, !third_party_upscale, rt_sliders.debug_log != 0.f);
  // The game's LOD and culling count the render's pixels (one battle's index counts: 50% with the sliders at x8 equal 100% at x2). Through
  // the upscaler the display is what they are for: its detail at any scale, from the next load. At 50% with the sliders at x8: 25.85 to 31.95 ms in RT/PT, 11.84 to 13.55 without rays.
  const float scene_share = (below_100_detail_row != 0.f && !third_party_upscale && twattila::anti_aliasing::Upscales(aa_settings.method)) ? twwarhammer3::taa_dlss::SceneShare(twwarhammer3::resolution_scale::Current()) : 1.f;
  for (auto* option : {&unit_detail, &building_detail, &tree_detail, &grass_detail, &object_detail, &terrain_detail, &effects_detail,
                       &unit_draw, &building_draw, &tree_draw, &grass_draw, &object_draw, &terrain_draw}) {
    option->pixels = scene_share * scene_share;
  }
  // Below about 2/3 the AO reads the display's depth, with data in the render size's corner only.
  twattila::depth_levels::SetUsedSize(twwarhammer3::taa_dlss::RenderSizeBelow100());
  const bool own_global_illumination = shader_injection.light_bounce_strength > 0.f;
  twwarhammer3::game_gi::sky_only.store(own_global_illumination && twwarhammer3::game_gi::sky_row != 0.f, std::memory_order_relaxed);
  twwarhammer3::game_gi::off.store(own_global_illumination && twwarhammer3::game_gi::sky_row == 0.f, std::memory_order_relaxed);
  // The row's reach only for this write: the slider keeps the player's 0.
  const bool farther_trees = rays_tree_reach_row != 0.f && rays_skip_cascades_row != 0.f && traced_lights::rays_replace_maps_row != 0.f
                             && rt_settings.casters_out_of_view && first_map_reach != nullptr && first_map_reach->scale == 0.f;
  if (farther_trees) {
    first_map_reach->scale = TREE_REACH_METRES;
    if (first_map_reach->tweaks.front().value != nullptr && !tree_reach_logged.exchange(true, std::memory_order_relaxed)) {
      renodx::utils::log::i(std::format("[Warhammer III] First run: Trees Out of View up to 150 m, the first shadow map's reach {:g} m.", TREE_REACH_METRES));
    }
  }
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
  if (farther_trees) {
    first_map_reach->scale = 0.f;
  }
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: WARHAMMER III by ATN";

namespace {

// The engine values from the game's device on (it may read them before its first present).
void OnInitDevice(reshade::api::device*) {
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
}

void ConfigureAddon() {
  if (configured) return;

  // Construct the non-trivial containers here rather than during DLL loading.
  BuildRuntimeData();

  // Shaders cb
  renodx::mods::shader::expected_constant_buffer_space = 50;
  renodx::mods::shader::expected_constant_buffer_index = SHADER_INJECTION_SLOT;

  // Swapchain cb
  renodx::mods::swapchain::expected_constant_buffer_index = 10;
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
  unit_detail.tweaks = {
      {.name = "num_pixels_per_triangle_lod_change_unit", .inverse = true},
  };
  building_detail.tweaks = {
      {.name = "num_pixels_per_triangle_lod_change_building", .inverse = true},
  };
  tree_detail.tweaks = {
      {.name = "num_pixels_per_triangle_lod_change_battle_tree", .inverse = true},
      {.name = "num_pixels_per_triangle_lod_change_campaign_tree", .inverse = true},
  };
  grass_detail.tweaks = {
      {.name = "num_pixels_per_triangle_lod_change_grass", .inverse = true},
  };
  object_detail.tweaks = {
      {.name = "num_pixels_per_triangle_lod_change_prop", .inverse = true},
      {.name = "num_pixels_per_triangle_lod_change_deployable", .inverse = true},
  };
  terrain_detail.tweaks = {
      {.name = "num_pixels_per_triangle_lod_change_terrain", .inverse = true},
  };
  effects_detail.tweaks = {
      {.name = "num_pixels_per_triangle_lod_change_vfx", .inverse = true},
  };
  // Battle 16 / campaign 8 pixels (the game copies one set when a battle or the campaign loads).
  unit_draw.tweaks = {
      {.name = "battle_cull_aabb_at_num_pixels_unit", .inverse = true},
      {.name = "campaign_cull_aabb_at_num_pixels_unit", .inverse = true},
  };
  building_draw.tweaks = {
      {.name = "battle_cull_aabb_at_num_pixels_building", .inverse = true},
      {.name = "campaign_cull_aabb_at_num_pixels_building", .inverse = true},
  };
  tree_draw.tweaks = {
      {.name = "battle_cull_aabb_at_num_pixels_tree", .inverse = true},
      {.name = "campaign_cull_aabb_at_num_pixels_tree", .inverse = true},
      {.name = "campaign_tree_display_max_distance"},
  };
  grass_draw.tweaks = {
      {.name = "battle_cull_aabb_at_num_pixels_grass", .inverse = true},
      {.name = "campaign_cull_aabb_at_num_pixels_grass", .inverse = true},
  };
  object_draw.tweaks = {
      {.name = "battle_cull_aabb_at_num_pixels_prop", .inverse = true},
      {.name = "campaign_cull_aabb_at_num_pixels_prop", .inverse = true},
      {.name = "battle_cull_aabb_at_num_pixels_deployable", .inverse = true},
      {.name = "campaign_cull_aabb_at_num_pixels_deployable", .inverse = true},
      {.name = "battle_small_prop_cull_distance"},
  };
  terrain_draw.tweaks = {
      {.name = "battle_cull_aabb_at_num_pixels_terrain", .inverse = true},
      {.name = "campaign_cull_aabb_at_num_pixels_terrain", .inverse = true},
  };
  grass_density.tweaks = {
      {.name = "grass_density_multiplier"},
  };
  terrain_tessellation.tweaks = {
      {.name = "terrain_tessellation_quality_battle"},
      {.name = "terrain_tessellation_quality_campaign"},
  };
  engine_tests = more_tests::MoreTests();
  graphics_options = {&unit_detail, &building_detail, &tree_detail, &grass_detail, &object_detail, &terrain_detail, &effects_detail, &unit_draw, &building_draw, &tree_draw, &grass_draw, &object_draw, &terrain_draw, &grass_density, &terrain_tessellation};
  for (auto& test : engine_tests) {
    graphics_options.push_back(&test.option);
    if (test.key == "GraphicsTest_battle_shadow_split_0") first_map_reach = &test.option;
  }
  graphics_status = new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Engine values: not searched yet",
      .section = "Graphics (Experimental)",
  };
  // Graphics presets (ATN): MAX (every graphics setting at its maximum) and Reset (all
  // graphics settings at their defaults; not the ambient occlusion or the Light Bounce).
  const std::vector<std::pair<std::string, float>> graphics_defaults = {
      {"GraphicsUnitDetail", 1.f},
      {"GraphicsBuildingDetail", 1.f},
      {"GraphicsTreeDetail", 1.f},
      {"GraphicsGrassDetail", 1.f},
      {"GraphicsObjectDetail", 1.f},
      {"GraphicsTerrainDetail", 1.f},
      {"GraphicsEffectsDetail", 1.f},
      {"GraphicsUnitDraw", 1.f},
      {"GraphicsBuildingDraw", 1.f},
      {"GraphicsTreeDraw", 1.f},
      {"GraphicsGrassDraw", 1.f},
      {"GraphicsObjectDraw", 1.f},
      {"GraphicsTerrainDraw", 1.f},
      {"GraphicsGrassDensity", 1.f},
      {"GraphicsTerrainTessellation", 1.f},
  };
  const std::vector<std::pair<std::string, float>> graphics_max = {
      {"GraphicsUnitDetail", 8.f},
      {"GraphicsBuildingDetail", 8.f},
      {"GraphicsTreeDetail", 8.f},
      {"GraphicsGrassDetail", 8.f},
      {"GraphicsObjectDetail", 8.f},
      {"GraphicsTerrainDetail", 8.f},
      {"GraphicsEffectsDetail", 8.f},
      {"GraphicsUnitDraw", 8.f},
      {"GraphicsBuildingDraw", 8.f},
      {"GraphicsTreeDraw", 8.f},
      {"GraphicsGrassDraw", 8.f},
      {"GraphicsObjectDraw", 8.f},
      {"GraphicsTerrainDraw", 8.f},
      {"GraphicsGrassDensity", 2.f},
      {"GraphicsTerrainTessellation", 4.f},
  };
  settings.insert(settings.end(), {
      tw::engine_tweaks::Note(),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting back to its default.",
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
      tw::engine_tweaks::Slider(&unit_detail, "GraphicsUnitDetail", "Unit Detail Distance",
                                "Soldiers, monsters and heroes keep their detailed models farther away. Heavier for the CPU\n"
                                "and GPU. From the next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&building_detail, "GraphicsBuildingDetail", "Building Detail Distance",
                                "Buildings and walls keep their detailed models farther away. From the next battle or\n"
                                "campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&tree_detail, "GraphicsTreeDetail", "Tree Detail Distance",
                                "Trees keep their detailed models farther away, in battles and on the campaign map. From the\n"
                                "next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&grass_detail, "GraphicsGrassDetail", "Grass Detail Distance",
                                "Grass keeps its detailed models farther away. From the next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&object_detail, "GraphicsObjectDetail", "Model Detail Distance",
                                "Props, rocks and deployables keep their detailed models farther away. From the next battle or\n"
                                "campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&terrain_detail, "GraphicsTerrainDetail", "Terrain Detail Distance",
                                "The terrain keeps its fine detail farther away. From the next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&effects_detail, "GraphicsEffectsDetail", "Effects Detail Distance",
                                "Effects keep their detailed models farther away. From the next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&unit_draw, "GraphicsUnitDraw", "Unit Draw Distance",
                                "Small or far soldiers stay visible farther away. From the next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&building_draw, "GraphicsBuildingDraw", "Building Draw Distance",
                                "Small or far buildings stay visible farther away. From the next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&tree_draw, "GraphicsTreeDraw", "Tree Draw Distance",
                                "Trees stay visible farther away. From the next battle or campaign load.", 8.f, 0.25f),
      tw::engine_tweaks::Slider(&grass_draw, "GraphicsGrassDraw", "Grass Draw Distance",
                                "Grass stays visible farther away. Heavier. From the next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&object_draw, "GraphicsObjectDraw", "Object Draw Distance",
                                "Props, rocks and deployables stay visible farther away. From the next battle or campaign\n"
                                "load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&terrain_draw, "GraphicsTerrainDraw", "Terrain Draw Distance",
                                "Small terrain pieces stay visible farther away. From the next battle or campaign load.",
                                8.f, 0.25f),
      tw::engine_tweaks::Slider(&grass_density, "GraphicsGrassDensity", "Grass Density",
                                "More grass blades. Heavy for the GPU. From the next battle or campaign load.", 2.f, 0.5f),
      tw::engine_tweaks::Slider(&terrain_tessellation, "GraphicsTerrainTessellation", "Terrain Tessellation",
                                "Finer terrain shapes. Heavier for the GPU.", 4.f, 0.25f),
      graphics_status,
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
      .label = "Tests: each changes the game's own values (1x, 0 m: unchanged). Many apply from the next\n"
               "battle; some may do nothing or crash.",
      .section = engine_tests.front().section,
      .tint = 0xFFD23C,
  });
  twattila::engine_tests::Append(&settings, &engine_tests, nullptr);

  // Appended Settings (Ambient Occlusion, Global Illumination, Screen-Space Shadows, Anti-Aliasing)
  twattila::lighting_settings::Append(&settings, &shader_injection);
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);

  // Ray tracing (twattila/rt_scene.hpp) with an upscaler.
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, false, true, twattila::feature_settings::AA_SECTION,
                                     &shader_injection.light_bounce_strength);
  for (auto* setting : settings) {
    // Their shared rule reads dlss_mode, which no row sets here: the scale is the game's float (upscaling_mode_row).
    if (setting->key == "AntiAliasingTextureLodBias" || setting->key == "AntiAliasingEffectsAtOutput") {
      setting->is_enabled = [] { return twattila::feature_settings::Upscaling(aa_sliders); };
    }
  }
  // Shadow Map Size (shadow_map_size.hpp): the page shows it where the game's shadow maps shadow (rays too, with Rays
  // Replace Shadow Maps off), and no preset writes it.
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "GraphicsShadowMapSize",
      .binding = &twwarhammer3::shadow_map_size::row,
      .value_type = renodx::utils::settings::SettingValueType::INTEGER,
      .default_value = 0.f,
      .label = "Shadow Map Size (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Sharper sun shadows at the game's highest Shadows setting. About 1.5 GB more video memory.\n"
                 "Applies after a restart.",
      .labels = {"4096 (Game's)", "8192"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedShadowMapSize",
      .binding = &twwarhammer3::shadow_map_size::smaller_row,
      .value_type = renodx::utils::settings::SettingValueType::INTEGER,
      .default_value = 0.f,
      .label = "Shadow Maps Under Rays (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "With Rays Replace Shadow Maps: the game's shadow maps smaller, for what still reads them (water, lit\n"
                 "effects, the particles). Less video memory. Applies after a restart, and stays until the next one.",
      .labels = {"4096 (Game's)", "2048", "1024"},
      .is_enabled = [] { return traced_lights::rays_replace_maps_row != 0.f; },
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "GlobalIlluminationGameSky",
      .binding = &twwarhammer3::game_gi::sky_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 1.f,
      .label = "Sky Shade From the Game's Probes",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Under trees, roofs and walls the sky's light stays as dark as in the game's own picture.\n"
                 "Off: the sky's light at full strength everywhere, so shade is brighter.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedPathProbeSky",
      .binding = &path_probe_sky_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 1.f,
      .label = "Sky as Bright as the Game's",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Open ground in Path-Traced AO gets the sky's light as the game's own lighting gives it.\n"
                 "Off: a dimmer sky, darker shade.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedTreeReach",
      .binding = &rays_tree_reach_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Trees Out of View up to 150 m (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "With Skip the Shadow Maps: trees behind the camera keep their ray-traced shadows up to 150 m away,\n"
                 "also after ray tracing was off for a while. Smoke's shadows near the camera get softer.",
      .labels = {"Off", "On"},
      .is_enabled = [] { return rays_skip_cascades_row != 0.f && traced_lights::rays_replace_maps_row != 0.f; },
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedPointLightShadows",
      .binding = &rt_sliders.local_lights,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Torch and Fire Shadows",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Torches and fires cast their shadows from rays too.\n"
                 "With Path-Traced Global Illumination they also light what the screen never saw.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedReflectionLobe",
      .binding = &reflection_lobe_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Reflections as Rough as the Game's (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Ray-traced reflections blur by the material as the game's own light does: less mirror-like on armour.\n"
                 "Can be noisier.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedReflectionLight",
      .binding = &reflections::light_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 1.f,
      .label = "Reflections Lit as This Frame",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Ray-traced reflections show things lit as the picture is, also with Global Illumination off.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedReflectionFog",
      .binding = &reflections::fog_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Fog in Reflections (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Far hills in ray-traced reflections get the game's fog. Needs Reflections Lit as This Frame for the right brightness.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "VfxFireAndGlow",
      .binding = &twwarhammer3::vfx_particles::fire_and_glow,
      .default_value = 1.f,
      .label = "VFX Brightness: Fire & Glow",
      .section = "VFX",
      .tooltip = "Fire, sparks, magic and anything else that makes its own light. 1: the game's.",
      .min = 0.f,
      .max = 10.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "VfxSmokeAndDust",
      .binding = &twwarhammer3::vfx_particles::smoke_and_dust,
      .default_value = 1.f,
      .label = "VFX Brightness: Smoke & Dust",
      .section = "VFX",
      .tooltip = "Smoke, dust and clouds lit by the sun. 1: the game's.",
      .min = 0.f,
      .max = 10.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "VfxEffectMeshes",
      .binding = &twwarhammer3::vfx_particles::effect_meshes,
      .default_value = 1.f,
      .label = "VFX Brightness: Spell Meshes",
      .section = "VFX",
      .tooltip = "Effects drawn as meshes: spells, vortices, shockwaves. 1: the game's.",
      .min = 0.f,
      .max = 10.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "VfxParticleFog",
      .binding = &twwarhammer3::vfx_particles::particle_fog,
      .default_value = 1.f,
      .label = "VFX Brightness: Fog Particles",
      .section = "VFX",
      .tooltip = "Fog drawn as particles. 1: the game's.",
      .min = 0.f,
      .max = 10.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "VfxHalfResolution",
      .binding = &twwarhammer3::vfx_particles::half_resolution_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Particles at Half Resolution (Experimental)",
      .section = "VFX",
      .tooltip = "All smoke, fire and dust sprites drawn at half the render's size, as the game already does for some.\n"
                 "Much faster in big fires and storms, softer particle edges.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "VfxParticleSmoothing",
      .binding = &twwarhammer3::vfx_particles::smoothing_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Smooth Half-Size Particles (Experimental)",
      .section = "VFX",
      .tooltip = "Half-size smoke, fire and dust keep clean edges against units and flicker less.\n"
                 "Small cost.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "VfxParticleSmear",
      .binding = &twwarhammer3::taa_dlss::particle_smear_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Particle Trails Under Upscaling (Experimental)",
      .section = "VFX",
      .tooltip = "Fast smoke and fire leave no trail behind them in DLSS, FSR and XeSS. Can look rougher inside the smoke.\n"
                 "Frame Generation may show more artifacts there.",
      .labels = {"Off", "On"},
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterClarity",
      .binding = &twattila::water_effects::water_clarity,
      .default_value = 1.f,
      .label = "Water Clarity (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "How far you see into water. 1: the game's; higher: clearer water.\n"
                 "The campaign's sea stays as it is.",
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
      .tooltip = "Rippling light on the bed under battle water and rivers. 0: off.",
      .max = 2.f,
      .format = "%.2f",
  });

  // Appended Settings (HDR10)
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR. Default.\n"
                   "HDR10: 10-bit. scRGB: 16-bit. SDR: displays without HDR (all fixes and options stay).",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const int value = static_cast<int>(setting->GetValue());
    // No settings saved yet: a player's first start. On an SDR monitor it starts with the SDR preset.
    float saved_mode = 0.f;
    const bool first_start = !reshade::get_config_value(nullptr, (renodx::utils::settings::global_name + "-preset1").c_str(), "tonemapper", saved_mode);
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = value,
        .hdr_encoding = [](int output) {
          if (output != twattila::output_encoding::AUTO) return output;
          // Frame generation on from the start: HDR10, the frames need no pass of their own (twattila/frame_generation.hpp).
          const bool frame_generation = twattila::feature_settings::Upscaling(aa_sliders) && aa_sliders.frame_generation != 0.f;
          return frame_generation ? twattila::output_encoding::HDR10 : twattila::output_encoding::SCRGB;
        },
        .first_start_without_hdr = (first_start ? &ApplySdrPreset : nullptr),
    });
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.insert(settings.end(), {
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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarwarhammer3/mods/408"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "GitHub (Downloads)",
          .section = "About",
          .group = "line_1",
          .tint = 0x6E7681,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://github.com/arshiatn/renodx/releases"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV30, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's "
                   "custom PsychoV31 and PsychoV35. Visibility Bitmask ambient occlusion and Light Bounce: the method of Therrien, Levesque "
                   "and Gilet (2023) on GTAO (Jimenez et al. 2016), as GT-VBAO and GT-VBGI by Mirko Salm (2024). "
                   "Screen-space shadows: "
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
  });

  // Section titles and Settings Mode's pages as Attila's (twattila/feature_settings.hpp).
  renodx::utils::settings::default_section_tint = twattila::feature_settings::BASE_TINT;
  std::vector<std::string> engine_sections = {twattila::feature_settings::GRAPHICS_SECTION};
  for (const auto& test : engine_tests) {
    if (std::ranges::find(engine_sections, test.section) == engine_sections.end()) engine_sections.push_back(test.section);
  }
  twattila::feature_settings::SetTints({}, engine_sections);

  // Graphics 1 of 2's layout, then Settings Mode's pages. The game owns its scale; its ambient occlusion stays under ray-traced AO
  // yet; smoke and foliage aren't known here. Its march is already hierarchical: no FidelityFX SSSR. Path-Traced AO stays a choice, not the preset's: unproven in game.
  // Water keeps the game's march by default (reflections::OnMarch).
  twattila::rt_scene::internal::water_pass = true;
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::BUTTON,
      .label = "Time Each Pass (One Frame)",
      .tooltip = "The GPU's time of every pass of the next frame into wh3_pass_timing.txt next to the game's exe.",
      .on_change = [] { twattila::pass_timing::Request(); },
  });
  for (auto* setting : settings) {
    if (setting->key == "RayTracedWaterReflections") setting->default_value = 0.f;
  }
  // ATN's RT/PT preset (Attila's page, 2026-11-12) as the defaults of what the methods leave.
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders, .sss = &sss_sliders, .rt = &rt_sliders, .ssao_mode = &shader_injection.ssao_mode,
                                             .light_bounce_strength = &shader_injection.light_bounce_strength,
                                             .ssr_hierarchical = &reflections::all_surfaces_row,
                                             .upscaling_mode = &upscaling_mode_row,
                                             .upscaling_mode_found = twwarhammer3::resolution_scale::Found,
                                             .below_100_detail = &below_100_detail_row,
                                             .lod_bias_trilinear = &lod_bias_trilinear_row,
                                             .placed_objects = &placed_objects_row,
                                             .rays_replace_maps = &traced_lights::rays_replace_maps_row,
                                             .rays_units_out_of_view = &rays_units_out_of_view_row,
                                             .rays_skip_cascade_trees = &rays_skip_cascade_trees_row,
                                             .rays_skip_cascades = &rays_skip_cascades_row,
                                             .rays_one_at_a_place = &rays_one_at_a_place_row,
                                             .rays_cascade_unit_cut_outs = &twwarhammer3::cascade_unit_cut_outs_row,
                                             .rays_banner_light = &rays_banner_light_row,
                                             .rays_branch_cut_outs = &rays_branch_cut_outs_row,
                                             .rays_unseen_cut_outs = &twwarhammer3::unseen_cut_outs_row,
                                             .rays_start_past = &rt_sliders.start_past,
                                             .rays_faded_parts = &rays_faded_parts_row,
                                             .rays_trunks_as_drawn = &rays_trunks_as_drawn_row,
                                             .rays_sheets_unselfed = &rt_sliders.sheets_unselfed,
                                             .pixel_trace = new renodx::utils::settings::Setting{
                                                 .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
                                                 .label = "Pixel trace",
                                                 .on_draw = [] { return talesofarise::pixel_trace::DrawRow(&traced_pixel, "Put the cross\non the black spot."); },
                                             },
                                             .rr_inputs = {
                                                 new renodx::utils::settings::Setting{
                                                     .key = "RayReconstructionParticleLayer",
                                                     .binding = &twwarhammer3::taa_dlss::rr_particle_layer_row,
                                                     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                                                     .default_value = 0.f,
                                                     .label = "Particles Skip the Denoiser (Experimental)",
                                                     .tooltip = "Smoke and fire are only upscaled, not denoised.\n"
                                                                "At 100% render scale they lose depth of field's blur.",
                                                     .labels = {"Off", "On"},
                                                 },
                                                 new renodx::utils::settings::Setting{
                                                     .key = "RayReconstructionColorBefore",
                                                     .binding = &twwarhammer3::taa_dlss::rr_color_before_row,
                                                     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                                                     .default_value = 0.f,
                                                     .label = "Picture Before Particles (Experimental)",
                                                     .tooltip = "Shows the denoiser the picture without smoke and fire, against ghosting and vanishing particles.\n"
                                                                "Not needed with Particles Skip the Denoiser.",
                                                     .labels = {"Off", "On"},
                                                     .is_enabled = [] { return twwarhammer3::taa_dlss::rr_particle_layer_row == 0.f; },
                                                 },
                                                 new renodx::utils::settings::Setting{
                                                     .key = "RayReconstructionDofGuide",
                                                     .binding = &twwarhammer3::taa_dlss::rr_dof_guide_row,
                                                     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                                                     .default_value = 0.f,
                                                     .label = "Depth of Field Guide (Experimental)",
                                                     .tooltip = "Tells the denoiser where depth of field blurred the picture. Needs Ray Reconstruction E.\n"
                                                                "At 100% render scale.",
                                                     .labels = {"Off", "On"},
                                                 },
                                             },
                                             .rays_hills_out_of_view = &rays_hills_out_of_view_row,
                                             .rays_terrain_count_later = &rays_terrain_count_later_row,
                                             .rays_grass_counts_later = &rays_grass_counts_later_row,
                                             .water_motion = &water_motion_row,
                                             .scale_fix_gi_shadow = &reflections::scale_fix_row,
                                             .texts = &PAGE_TEXTS,
                                             .aa_methods = AA_METHODS,
                                             .upscaler_status = twwarhammer3::taa_dlss::Status,
                                             .has_scale_rows = false,
                                             .has_object_motion = true,
                                             .has_foliage_light = true,
                                             .lights_own_row = true,
                                             .rays_replace_ao = false});
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;
  twattila::pass_timing::Install();

  ConfigureAddon();
  tw::engine_tweaks::watch::StartIfRequested(graphics_options, WATCHED_VALUES, 300);
  twattila::test_hooks::Install(&settings, false);

  talesofarise::pixel_trace::file_prefix = "wh3";
  talesofarise::pixel_trace::position_in_viewport = true;
  talesofarise::pixel_trace::on_request = [](float x, float y) {
    const std::unique_lock probe_lock(twattila::rt_scene::ambient_probe.mutex);
    twattila::rt_scene::ambient_probe.wanted = true;
    twattila::rt_scene::ambient_probe.position = {x, y};
    twattila::rt_scene::ambient_probe.text.clear();
  };
  talesofarise::pixel_trace::extra_section = [] {
    const std::unique_lock probe_lock(twattila::rt_scene::ambient_probe.mutex);
    twattila::rt_scene::ambient_probe.wanted = false;
    return twattila::rt_scene::ambient_probe.text.empty() ? std::string("The rays' AO did not run this frame.\n") : twattila::rt_scene::ambient_probe.text;
  };
  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(reflections::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(traced_lights::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twwarhammer3::object_motion::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twwarhammer3::vfx_particles::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);

  // Rows saved at an old default, cleared once. Before v32 both hidden: fog's light nobody has seen at night, and dark
  // patches on rivers under rays. Before v45: NRD when Ray Reconstruction is off, the banners' light and colour on.
  constexpr int SETTINGS_REVISION = 33;
  if (int revision = 0; !reshade::get_config_value(nullptr, "renodx", "SettingsRevision", revision) || revision < SETTINGS_REVISION) {
    for (const char* preset : {"renodx-preset1", "renodx-preset2", "renodx-preset3"}) {
      if (revision < 32) {
        for (const char* key : {"RayTracedLightGlow", "RayTracedWaterReflections"}) {
          reshade::set_config_value(nullptr, preset, key, static_cast<const char*>(nullptr));
        }
      }
      for (const char* key : {"RayTracingDenoiser", "ScreenSpaceShadowsBannerLight", "ScreenSpaceShadowsColoured"}) {
        reshade::set_config_value(nullptr, preset, key, static_cast<const char*>(nullptr));
      }
    }
    reshade::set_config_value(nullptr, "renodx", "SettingsRevision", SETTINGS_REVISION);
  }

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // An earlier version's changed audio memory left this addon in ReShade.ini's early load list: taken out.
  twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  twattila::rt_scene::Install(SCENE_SHADERS, {}, RT_HOST);
  for (const uint32_t shader_hash : CAMPAIGN_SHADERS) {
    renodx::utils::command_action::Register(
        OnCampaignDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  twattila::rt_scene::InstallCutOutMasks(twwarhammer3::CUT_OUT_MASKS);
  twattila::rt_scene::InstallPointLights(std::span(traced_lights::SHADERS).first(traced_lights::POINT_LIGHT_SHADERS));
  twattila::rt_scene::InstallFogShadows(std::span(traced_lights::SHADERS).subspan(traced_lights::POINT_LIGHT_SHADERS, traced_lights::FOG_SHADERS));
  twwarhammer3::object_motion::Install([] { return twattila::feature_settings::Upscaling(aa_sliders) && aa_sliders.object_motion != 0.f; },
                                       [] { return twattila::feature_settings::Upscaling(aa_sliders) && water_motion_row != 0.f; },
                                       [] { return rt_sliders.debug_log != 0.f; });
  // After those two: they bind what decides here (none of these is a sun draw).
  for (const uint32_t& hash : traced_lights::SHADERS) {
    renodx::utils::command_action::Register(
        traced_lights::OnDraw, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &hash);
  }
  // After the fog's light draw's own hooks: only its target and constant buffers are read, after the draw.
  renodx::utils::command_action::Register(
      reflections::OnFogLight, {.shader_hash = 0xA8F5202D, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  twattila::water_effects::water_shaders = WATER_SHADERS;
  renodx::utils::pipeline::RegisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  renodx::utils::pipeline::RegisterOnInitCallback(twattila::water_effects::OnInitMaterialWater);
  twwarhammer3::vfx_particles::Install();
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  // Registration order is run order at a sun draw. The guides first: the trace asks whether they were written this frame.
  for (const auto& draw : LIGHT_BOUNCE_DRAWS) {
    renodx::utils::command_action::Register(
        OnSunDrawGuides, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
  // Ray tracing's next: its trace for the screen-space shadows, the AO filter and the Light Bounce. The ambient cube at float 8
  // of lighting_VS_PS in all 7 (the RDEF of each, c2-c7, with the shader's own reads): the paths' sky; Path-Traced Ambient's
  // callback right after it (the Light Bounce's bypass skips post callbacks: put back in finalize). The sun colour at float 4
  // (c1 in all 7; x 0x389FB405 in rt_sun: twwarhammer3/rt_sun.cs_5_0.hlsl), the paths' sun light.
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);
  // Before the Light Bounce's (its callback draws the sun draw again).
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  // Light Through Foliage: the G-buffer's channel 3 alpha byte, bit 128 (leaves 132, tree cards 160, grass x 128 | 5; the
  // bit its own contact shadows, sun_mask E6BCBA8A, let the sun through).
  twattila::screen_space_shadows::foliage_material_bit = 128;
  // Light Through Banners: cloth, banners and flags (as the rays), the G-buffer's colour linear.
  twattila::screen_space_shadows::InstallThin(THIN_SHADERS, 1.f);
  // Before the Light Bounce: it draws the sun draws itself (no callback after it runs).
  twattila::ao_filter::Install(LIGHT_BOUNCE_DRAWS);
  for (const auto& draw : LIGHT_BOUNCE_DRAWS) {
    if (draw.ao_slot == UINT_MAX) continue;
    renodx::utils::command_action::Register(
        sun_ao::OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
  reshade::register_event<reshade::addon_event::destroy_device>(sun_ao::OnDestroyDevice);
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  for (const uint32_t& shader_hash : CASCADE_CASTER_SHADERS) {
    renodx::utils::command_action::Register(
        OnCascadeCaster, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &shader_hash);
  }
  for (const uint32_t& shader_hash : SHADOW_SIZE_SHADERS) {
    renodx::utils::command_action::Register(
        OnShadowSizeDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &shader_hash);
  }

  twwarhammer3::taa_dlss::Install();
  // This game's swap chain shader reads its constants at b10.
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10, 10);

  // After RenoDX, so our copy of the AA frame lands last.
  reshade::register_event<reshade::addon_event::copy_resource>(aa_target::OnCopyResource);
  reshade::register_event<reshade::addon_event::copy_texture_region>(aa_target::OnCopyTextureRegion);
  reshade::register_event<reshade::addon_event::clear_render_target_view>(aa_target::OnClearRenderTargetView);
  reshade::register_event<reshade::addon_event::present>(aa_target::OnPresent);
#if FIX_BLOOM
  reshade::register_event<reshade::addon_event::present>(bloom_fix::OnPresent);
#endif
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  twwarhammer3::game_gi::Install();
  twwarhammer3::shadow_map_size::Install(traced_lights::rays_replace_maps_row != 0.f);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(aa_target::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twwarhammer3::taa_dlss::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twwarhammer3::resolution_scale::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);

  reshade::register_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::test_hooks::Uninstall();
  talesofarise::pixel_trace::Stop();
  twattila::pass_timing::Uninstall();
  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(reflections::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(traced_lights::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twwarhammer3::object_motion::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twwarhammer3::vfx_particles::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::copy_resource>(aa_target::OnCopyResource);
  reshade::unregister_event<reshade::addon_event::copy_texture_region>(aa_target::OnCopyTextureRegion);
  reshade::unregister_event<reshade::addon_event::clear_render_target_view>(aa_target::OnClearRenderTargetView);
  reshade::unregister_event<reshade::addon_event::present>(aa_target::OnPresent);
#if FIX_BLOOM
  reshade::unregister_event<reshade::addon_event::present>(bloom_fix::OnPresent);
#endif
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  tw::engine_tweaks::Uninstall(graphics_options);
  twwarhammer3::game_gi::Uninstall();
  twwarhammer3::shadow_map_size::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  twattila::screen_space_shadows::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::ao_filter::Uninstall();
  twattila::rt_scene::Uninstall();
  renodx::utils::command_action::Unregister(OnCampaignDraw);
  renodx::utils::command_action::Unregister(OnSunDrawGuides);
  renodx::utils::command_action::Unregister(sun_ao::OnSunDraw);
  reshade::unregister_event<reshade::addon_event::destroy_device>(sun_ao::OnDestroyDevice);
  renodx::utils::command_action::Unregister(traced_lights::OnDraw);
  renodx::utils::command_action::Unregister(reflections::OnFogLight);
  renodx::utils::command_action::Unregister(twattila::water_effects::OnWaterDraw);
  renodx::utils::command_action::Unregister(OnCascadeCaster);
  renodx::utils::command_action::Unregister(OnShadowSizeDraw);
  twwarhammer3::object_motion::Uninstall();
  twwarhammer3::vfx_particles::Uninstall();
  renodx::utils::pipeline::UnregisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  renodx::utils::pipeline::UnregisterOnInitCallback(twattila::water_effects::OnInitMaterialWater);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(aa_target::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twwarhammer3::taa_dlss::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twwarhammer3::resolution_scale::OnDestroyDevice);
  twwarhammer3::taa_dlss::Uninstall();
  twattila::frame_generation::Uninstall();

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
