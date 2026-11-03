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
#include <mutex>
#include <cmath>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/ao_filter.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "./dlss_frame_generation.hpp"
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
  });
}

void ApplyPsychoRecommended2500() {
  // ATN: every recommended preset in PsychoV31 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 3.f},
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
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &data.texture))
      || FAILED(device->CreateRenderTargetView(data.texture, nullptr, &data.rtv))
      || FAILED(device->CreateShaderResourceView(data.texture, nullptr, &data.srv))) {
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
  const std::scoped_lock lock(mutex);
  valid = false;  // frame consumed
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
            twattila::rt_scene::OnTonemapDrawn(cmd_list);          \
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

// Sky (file) / fogs (stock): capture the render scale for the DOF.
#define RenderScaleShader(value)                   \
  {                                                \
      value,                                       \
      {                                            \
          .crc32 = value,                          \
          .code = __##value,                       \
          .on_draw = &render_scale::OnDrawCapture, \
      },                                           \
  }

#define RenderScaleStockShader(value)                \
  {                                                  \
      value,                                         \
      {                                              \
          .crc32 = value,                            \
          .on_inject = [](auto*) { return false; },  \
          .on_draw = &render_scale::OnDrawCapture,   \
      },                                             \
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
      RenderScaleStockShader(0x34719E09), //fog
      RenderScaleStockShader(0x78E4CBF2), //volumetric fog upsample
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
  // tonemap (t00's b1, bound at b1) and, for the collect without depth (ssao02), the downsample's output (bound at t2;
  // line_sweep_ao.hpp).
  custom_shaders[0x49F538CF].on_draw = [](auto* cmd_list) {
    tw::ao_trace::OnPass(cmd_list, 0x49F538CF, true);
    if (shader_injection.ssao_mode >= 1.f) tw::line_sweep_ao::KeepUavDepth(cmd_list);
    return true;
  };
  // The collects' replacements are the game's code only (Game's costs what it did); the Visibility Bitmask is the
  // addon's own pass (DispatchInstead) with OnCollect's depth and camera (without them, the first frame: the game's).
  custom_shaders[0xEE5C29AE].on_draw = [](auto* cmd_list) {
    tw::ao_trace::OnPass(cmd_list, 0xEE5C29AE, true);
    if (shader_injection.ssao_mode < 1.f || !tw::line_sweep_ao::OnCollect(cmd_list, true, false, 1, 0)) return true;
    const bool dispatched = tw::line_sweep_ao::DispatchInstead(
        cmd_list, __ao_bitmask_collect01, &shader_injection, sizeof(shader_injection), false, __ao_upsample_collect01,
        shader_injection.ssao_resolution != 0.f);
    tw::line_sweep_ao::OnCollectDone(cmd_list);
    return !dispatched;
  };
  custom_shaders[0x2A629B2B].on_draw = [](auto* cmd_list) {
    tw::ao_trace::OnPass(cmd_list, 0x2A629B2B, true);
    if (shader_injection.ssao_mode < 1.f || !tw::line_sweep_ao::OnCollect(cmd_list, true, true, 1, 2)) return true;
    const bool dispatched = tw::line_sweep_ao::DispatchInstead(
        cmd_list, __ao_bitmask_collect02, &shader_injection, sizeof(shader_injection), false, __ao_upsample_collect02,
        shader_injection.ssao_resolution != 0.f);
    tw::line_sweep_ao::OnCollectDone(cmd_list);
    return !dispatched;
  };
  custom_shaders[0xE2EE82F8].on_draw = [](auto* cmd_list) {
    if (shader_injection.ssao_mode >= 1.f) tw::line_sweep_ao::KeepPixelCamera(cmd_list, 1);
    return aa_target::OnDrawTonemap(cmd_list);
  };
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
                   "HDR (PsychoV31): replaces it with PsychoV31, more grading sliders.",
        .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV30)", "HDR (PsychoV31)"},
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
        .label = "Hue Shift",
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
// Every engine option above, then the tests' (Graphics 2 of 2: more_tests.h, twattila/engine_tests.hpp).
std::vector<tw::engine_tweaks::Option*> graphics_options;
std::vector<twattila::engine_tests::EngineTest> engine_tests;
std::atomic<bool> at_start_done = false;
std::atomic<bool> at_start_stop = false;
HMODULE addon_module_handle = nullptr;
twattila::feature_settings::SssSliders sss_sliders;
// DLSS in place of the game's TAA (taa_dlss.hpp) and frame generation on it (dlss_frame_generation.hpp: started at
// launch from ReShade.ini [Warhammer III]).
struct DlssSliders {
  float enabled = 0.f;
  float preset = 1.f;  // K, M, L
  float frame_generation = 0.f;  // off, 2x-6x, Dynamic (restart to turn it on)
  float reflex_boost = 0.f;      // (restart)
  float frame_limit = 0.f;       // Reflex's, rendered frames per second (0: off)
  float ray_reconstruction = 0.f;  // NVIDIA's Ray Reconstruction (taa_dlss.hpp)
} dlss_sliders;
// Ray tracing's (twattila/rt_scene.hpp, through DLSS's helper): enabled with DLSS (aa_sliders.method 5 while it is on).
twattila::rt_scene::Sliders rt_sliders;
bool frame_generation = false;  // started at launch
twattila::feature_settings::AaSliders aa_sliders;

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
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

// Output: 0 HDR10, 1 scRGB, 2 SDR (10-bit, gamma 2.2).
void SetOutputEncoding(int encoding) {
  shader_injection.swap_chain_encoding = static_cast<float>(encoding);
  if (encoding == 2) {
    renodx::mods::swapchain::target_format = reshade::api::format::r10g10b10a2_unorm;
    renodx::mods::swapchain::target_color_space = reshade::api::color_space::srgb_nonlinear;
  } else {
    renodx::mods::swapchain::SetUseHDR10(encoding == 0);
  }
}

// Output Auto: scRGB with Windows HDR on for the game's display (or unknown), else SDR. Set at the first swap chain,
// before the swapchain mod's create_swapchain (registered after this).
bool auto_output_pending = false;

bool OnCreateSwapchain(reshade::api::device_api, reshade::api::swapchain_desc&, void* hwnd) {
  if (!auto_output_pending) return false;
  auto_output_pending = false;
  const HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY);
  const auto path = renodx::utils::platform::GetPathInfo(monitor);
  const bool windows_hdr = !path.has_value() || renodx::utils::swapchain::GetHDREnabled(path.value());
  // Frame generation takes no scRGB: HDR10 with it.
  SetOutputEncoding(windows_hdr ? (frame_generation ? 0 : 1) : 2);
  return false;
}

// The tone mapper: frame generation's HUD-less frame after it (the display's size, before the UI).
constexpr std::array<uint32_t, 1> TONEMAPPER_SHADERS = {0xE2EE82F8u};

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
    {.shader_hash = 0x18A84893, .standard = true, .gbuffer_slot = 10, .camera_slot = 1, .lighting_slot = 4, .camera_layout = CAMERA_LAYOUT, .ao_slot = 4},
    {.shader_hash = 0x24F97B7B, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1},
    {.shader_hash = 0x76A707E5, .standard = true, .gbuffer_slot = 6, .camera_slot = 1, .camera_layout = CAMERA_LAYOUT},
    {.shader_hash = 0x9EAD8951, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1},
    {.shader_hash = 0xCCEF2C1E, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1},
    {.shader_hash = 0xF0E2D5EC, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1},
    {.shader_hash = 0xF9F8CE80, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .camera_layout = CAMERA_LAYOUT, .ao_slot = 1},
}};

// Ray tracing's scene (twattila/rt_scene.hpp): the G-buffer's opaque meshes as the game's own vertex shaders draw them
// (their SV_Position streamed out: WH3 has no capture versions), units (skinned, character alpha) every frame. Not in
// it: leaves (no alpha test without their UVs), grass, billboards, decals, water and the tessellated terrain (its
// vertex shader gives control points).
constexpr std::array<twattila::rt_scene::SceneShader, 7> SCENE_SHADERS = {{
    {.hash = 0xF0395CC4},                    // fx_rigidanisotropic
    {.hash = 0x8EE34EC5},                    // fx_rigidambientmapfiremesh
    {.hash = 0x474F97FB},                    // fx_rigidbranch_v3
    {.hash = 0xC0ED2883},                    // fx_rigidtextureblend
    {.hash = 0x5767F87C, .terrain = true},   // fx_rigidterrain
    {.hash = 0xB8C567C3, .dynamic = true},   // fx_weighted (skinned units)
    {.hash = 0x2C45437C, .dynamic = true},   // rigid_character_alpha
}};

// Ray tracing's view of the game: DLSS's camera (taa_dlss.hpp; uploaded jittered, as the scene and its depth were drawn)
// and render size; no G-buffer guides (no reflections, paths without them).
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
    .guides_written = [] { return false; },
};

// Ray Reconstruction's guides (taa_dlss.hpp) at the sun light draws: the frame's first writes them.
constexpr auto OnSunDrawGuides = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& draw = command.template GetCallbackData<twattila::light_bounce::SunDraw>();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native != nullptr) twwarhammer3::taa_dlss::WriteGuides(native, draw.gbuffer_slot, draw.camera_slot);
  return {};
};

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  // The DLSS rows' values in the shape of Attila's rows; DLSS never starts on another vendor's GPU.
  aa_sliders.method = (dlss_sliders.enabled != 0.f && twattila::dlaa_bridge::OnNvidia()) ? 5.f : 0.f;
  aa_sliders.dlaa_preset = dlss_sliders.preset;
  aa_sliders.ray_reconstruction = dlss_sliders.ray_reconstruction;
  const twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
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
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twwarhammer3::taa_dlss::Update(aa_settings, frame_generation);
  talesofarise::frame_generation::frame_limit.store(static_cast<uint32_t>(std::clamp(dlss_sliders.frame_limit, 0.f, 240.f)));
  // The multiplier applies live (turning frame generation on needs a restart): 0 Dynamic in the shared module.
  if (const int choice = static_cast<int>(dlss_sliders.frame_generation); choice != 0) {
    talesofarise::frame_generation::multiplier.store(choice >= 6 ? 0u : static_cast<uint32_t>(choice + 1));
  }
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
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
  renodx::mods::shader::expected_constant_buffer_index = 10;

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
  // Graphics 2 of 2's tests: Attila's that this game has, then More Tests (more_tests.h).
  engine_tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), more_tests::ATTILA_TESTS);
  {
    auto more = more_tests::MoreTests();
    engine_tests.insert(engine_tests.end(), more.begin(), more.end());
  }
  graphics_options = {&unit_detail, &building_detail, &tree_detail, &grass_detail, &object_detail, &terrain_detail, &effects_detail, &unit_draw, &building_draw, &tree_draw, &grass_draw, &object_draw, &terrain_draw, &grass_density, &terrain_tessellation};
  for (auto& test : engine_tests) graphics_options.push_back(&test.option);
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
  {
    const auto dlss_on = [] { return dlss_sliders.enabled != 0.f; };
    // Frame generation's choices are read at launch: written to ReShade.ini [Warhammer III].
    const auto write_frame_generation = [] {
      const int choice = static_cast<int>(dlss_sliders.frame_generation);
      reshade::set_config_value(nullptr, twwarhammer3::dlss_frame_generation::SECTION, "FrameGeneration", choice != 0);
      reshade::set_config_value(nullptr, twwarhammer3::dlss_frame_generation::SECTION, "FrameGenerationMultiplier",
                                (choice >= 6 ? 0u : static_cast<uint32_t>(std::max(choice + 1, 2))));
      reshade::set_config_value(nullptr, twwarhammer3::dlss_frame_generation::SECTION, "ReflexBoost",
                                dlss_sliders.reflex_boost != 0.f);
    };
    settings.insert(settings.end(), {
        new renodx::utils::settings::Setting{
            .key = "Dlss",
            .binding = &dlss_sliders.enabled,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 0.f,
            .label = "DLSS",
            .section = twattila::feature_settings::DLSS_SECTION,
            .tooltip = "NVIDIA's DLSS (DLAA) in place of the game's TAA: keep in-game Anti-aliasing on TAA or TAA High\n"
                       "and Resolution Scale at 100% (below it the game's TAA and upscale stay). Needs an RTX GPU and\n"
                       "renodx-tw-dlaa64.exe and nvngx_dlss.dll next to Warhammer3.exe.",
            .labels = {"Off", "On"},
        },
        // What really runs, or why nothing does.
        new renodx::utils::settings::Setting{
            .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
            .section = twattila::feature_settings::DLSS_SECTION,
            .on_draw = [] {
              ImGui::TextWrapped("%s", twwarhammer3::taa_dlss::Status().c_str());
              return false;
            },
            .is_visible = dlss_on,
        },
        new renodx::utils::settings::Setting{
            .key = "DlssPreset",
            .binding = &dlss_sliders.preset,
            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
            .default_value = 1.f,
            .label = "DLSS Preset",
            .section = twattila::feature_settings::DLSS_SECTION,
            .tooltip = "NVIDIA's models: K the first transformer, M the second (default), L the sharpest.",
            .labels = {"K", "M", "L"},
            .is_enabled = dlss_on,
        },
        new renodx::utils::settings::Setting{
            .key = "DlssRayReconstruction",
            .binding = &dlss_sliders.ray_reconstruction,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 0.f,
            .label = "Ray Reconstruction (Experimental)",
            .section = twattila::feature_settings::DLSS_SECTION,
            .tooltip = "NVIDIA's Ray Reconstruction in place of DLSS: one model denoising the picture and the ray tracing.\n"
                       "Heavier. Best with ray tracing on.",
            .labels = {"Off", "On"},
            .is_enabled = dlss_on,
        },
        new renodx::utils::settings::Setting{
            .key = "DlssFrameGeneration",
            .binding = &dlss_sliders.frame_generation,
            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
            .default_value = 0.f,
            .label = "Frame Generation (Restart Required)",
            .section = twattila::feature_settings::DLSS_SECTION,
            .tooltip = "NVIDIA's DLSS Frame Generation (RTX 40 and newer) on DLSS. Output becomes HDR10 with it.\n"
                       "5x, 6x and Dynamic (as many as the display's refresh rate takes): RTX 50.",
            .labels = {"Off", "2x", "3x", "4x", "5x", "6x", "Dynamic"},
            .is_enabled = dlss_on,
            .on_change = write_frame_generation,
        },
        new renodx::utils::settings::Setting{
            .key = "DlssReflexBoost",
            .binding = &dlss_sliders.reflex_boost,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 0.f,
            .label = "Reflex Boost (Restart Required)",
            .section = twattila::feature_settings::DLSS_SECTION,
            .tooltip = "Keeps the GPU's clocks up for lower latency with frame generation.",
            .labels = {"Off", "On"},
            .is_enabled = [] { return dlss_sliders.enabled != 0.f && dlss_sliders.frame_generation != 0.f; },
            .on_change = write_frame_generation,
        },
        new renodx::utils::settings::Setting{
            .key = "DlssFrameLimit",
            .binding = &dlss_sliders.frame_limit,
            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
            .default_value = 0.f,
            .label = "Frame Rate Limit",
            .section = twattila::feature_settings::DLSS_SECTION,
            .tooltip = "Caps the rendered frame rate with NVIDIA Reflex (frame generation multiplies it). A cap the\n"
                       "game always reaches keeps the frame rate steady: less VRR flicker on OLED displays. 0: off.",
            .min = 0.f,
            .max = 240.f,
            .format = "%.0f fps",
            .is_enabled = [] { return dlss_sliders.enabled != 0.f && dlss_sliders.frame_generation != 0.f; },
        },
    });
  }

  // Ray tracing (twattila/rt_scene.hpp) with DLSS.
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, false, false, twattila::feature_settings::DLSS_SECTION,
                                     &shader_injection.light_bounce_strength);

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
    auto_output_pending = (value == 3);
    // Frame generation takes no scRGB: HDR10 with it.
    talesofarise::frame_generation::LoadSettings(twwarhammer3::dlss_frame_generation::SECTION, &frame_generation);
    SetOutputEncoding(auto_output_pending ? 1 : ((frame_generation && value == 1) ? 0 : value));
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV30 and PsychoV31 by ShortFuse. This mod uses Musa's custom PsychoV31. Visibility "
                   "Bitmask ambient occlusion and Light Bounce: the method of Therrien, Levesque and Gilet (2023) on "
                   "GTAO (Jimenez et al. 2016), as GT-VBAO and GT-VBGI by Mirko Salm (2024). Screen-space shadows: "
                   "Bend Studio (Apache 2.0, Sony Interactive Entertainment). SMAA (MIT, Jimenez et al.), CMAA2 "
                   "(Apache 2.0, Intel).",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::feature_settings::HELPER_CREDITS,
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
  });

  // Section titles and Settings Mode's pages as Attila's (twattila/feature_settings.hpp).
  renodx::utils::settings::default_section_tint = twattila::feature_settings::BASE_TINT;
  std::vector<std::string> engine_sections = {twattila::feature_settings::GRAPHICS_SECTION};
  for (const auto& test : engine_tests) {
    if (std::ranges::find(engine_sections, test.section) == engine_sections.end()) engine_sections.push_back(test.section);
  }
  twattila::feature_settings::SetTints({}, engine_sections);
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections);

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();
  tw::engine_tweaks::watch::StartIfRequested(graphics_options, WATCHED_VALUES, 300);

  reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests: written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(nullptr, &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // Ray tracing's at the sun draws first: its trace for the screen-space shadows, the AO filter and the Light Bounce.
  twattila::rt_scene::Install(SCENE_SHADERS, {}, RT_HOST);
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS);
  for (const auto& draw : LIGHT_BOUNCE_DRAWS) {
    renodx::utils::command_action::Register(
        OnSunDrawGuides, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);
  // Before the Light Bounce's (its callback draws the sun draw again).
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  // Before the Light Bounce: it draws the sun draws itself (no callback after it runs).
  twattila::ao_filter::Install(LIGHT_BOUNCE_DRAWS);
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);

  // Frame generation's present before DLSS counts it (taa_dlss's present after).
  frame_generation = twwarhammer3::dlss_frame_generation::Install(addon_module_handle, TONEMAPPER_SHADERS);
  twwarhammer3::taa_dlss::Install();

  // After RenoDX, so our copy of the AA frame lands last.
  reshade::register_event<reshade::addon_event::copy_resource>(aa_target::OnCopyResource);
  reshade::register_event<reshade::addon_event::copy_texture_region>(aa_target::OnCopyTextureRegion);
  reshade::register_event<reshade::addon_event::clear_render_target_view>(aa_target::OnClearRenderTargetView);
  reshade::register_event<reshade::addon_event::present>(aa_target::OnPresent);
#if FIX_BLOOM
  reshade::register_event<reshade::addon_event::present>(bloom_fix::OnPresent);
#endif
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(aa_target::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twwarhammer3::taa_dlss::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::copy_resource>(aa_target::OnCopyResource);
  reshade::unregister_event<reshade::addon_event::copy_texture_region>(aa_target::OnCopyTextureRegion);
  reshade::unregister_event<reshade::addon_event::clear_render_target_view>(aa_target::OnClearRenderTargetView);
  reshade::unregister_event<reshade::addon_event::present>(aa_target::OnPresent);
#if FIX_BLOOM
  reshade::unregister_event<reshade::addon_event::present>(bloom_fix::OnPresent);
#endif
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  at_start_stop = true;
  while (!at_start_done.load()) {
    Sleep(1);
  }
  twattila::screen_space_shadows::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::ao_filter::Uninstall();
  twattila::rt_scene::Uninstall();
  renodx::utils::command_action::Unregister(OnSunDrawGuides);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(aa_target::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twwarhammer3::taa_dlss::OnDestroyDevice);
  twwarhammer3::taa_dlss::Uninstall();
  if (frame_generation) twwarhammer3::dlss_frame_generation::Uninstall();

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
  if (addon_registered) {
    reshade::unregister_addon(addon_module);
    addon_registered = false;
  }
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      addon_module_handle = h_module;
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
