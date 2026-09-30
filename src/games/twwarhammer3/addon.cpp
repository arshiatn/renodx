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
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/settings.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;

bool IsAdvancedSettings() {
  return current_settings_mode >= 1.f;
}

bool IsExtendedMode() {
  return shader_injection.hdr == 1.f && current_settings_mode == 1;
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f && current_settings_mode == 1;
}

bool IsHDRMode() {
  return (shader_injection.hdr == 2.f || shader_injection.hdr == 1.f);
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

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 0.70f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
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

void ApplyPsychoRecommended() {
  // Recommended PsychoV30 tuning for a ~1000-nit display.
  // Only PsychoV30 controls are changed. Peak / Paper White / UI brightness
  // and unrelated controls are preserved.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 2.f},
      {"exposure", 1.f},
      {"highlights", 0.75f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      // Active extra controls
      {"bloom", 0.5f},
      {"lensflare", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"sharpening", 1.f},
      {"dof", 1.f},
      {"sky_deband", 2.f},
  });
}

void ApplyPsychoRecommended2500() {
  // Recommended PsychoV30 tuning for a ~1000-nit display.
  // Only PsychoV30 controls are changed. Peak / Paper White / UI brightness
  // and unrelated controls are preserved.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 2.f},
      {"exposure", 1.f},
      {"highlights", 0.85f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
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
// Registered after RenoDX, which drops that copy (8 -> 16-bit).
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

// t00: starts the AA chain in our texture (replaces UpgradeRTVReplaceShader here).
#define AATonemapShader(value)                   \
  {                                              \
      value,                                     \
      {                                          \
          .crc32 = value,                        \
          .code = __##value,                     \
          .on_draw = &aa_target::OnDrawTonemap,  \
          .on_drawn = &aa_target::OnDrawnRestore, \
      },                                         \
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

  // Settings ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
                   "HDR (PsychoV30): replaces Reinhard with PsychoV30 as tonemapper.",
        .labels = {"SDR", "HDR (Extended)","HDR (PsychoV30)"},
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
        .on_change = []() {
          ApplyPsychoRecommended();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (for 2000nits+)",
        .section = "Presets",
        .group = "preset-line-2",
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
        .is_enabled = IsHDRMode,
        .is_visible = IsAdvancedSettings,
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
        .is_enabled = IsHDRMode,
        .is_visible = IsAdvancedSettings,
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
        .is_enabled = IsHDRMode,
        .is_visible = IsAdvancedSettings,
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
        .is_enabled = IsHDRMode,
        .is_visible = IsAdvancedSettings,
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
        .is_enabled = IsHDRMode,
        .is_visible = IsAdvancedSettings,
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
        .is_enabled = IsHDRMode,
        .is_visible = IsAdvancedSettings,
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
        .is_visible = IsAdvancedSettings,
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

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: WARHAMMER III by ATN";

namespace {

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

  // Appended Settings (HDR10)
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "HDR10: 10bit, enough quality (banding?).\nscRGB: 16bit, overkill quality.",
        .labels = {"HDR10", "scRGB"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const auto value = setting->GetValue();
    shader_injection.swap_chain_encoding = value;
    renodx::mods::swapchain::SetUseHDR10(value == 0);
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
      .section = "About",
      .is_visible = IsAdvancedSettings,
  });

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);

  // After RenoDX, so our copy of the AA frame lands last.
  reshade::register_event<reshade::addon_event::copy_resource>(aa_target::OnCopyResource);
  reshade::register_event<reshade::addon_event::copy_texture_region>(aa_target::OnCopyTextureRegion);
  reshade::register_event<reshade::addon_event::clear_render_target_view>(aa_target::OnClearRenderTargetView);
  reshade::register_event<reshade::addon_event::present>(aa_target::OnPresent);
#if FIX_BLOOM
  reshade::register_event<reshade::addon_event::present>(bloom_fix::OnPresent);
#endif
  reshade::register_event<reshade::addon_event::destroy_device>(aa_target::OnDestroyDevice);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::copy_resource>(aa_target::OnCopyResource);
  reshade::unregister_event<reshade::addon_event::copy_texture_region>(aa_target::OnCopyTextureRegion);
  reshade::unregister_event<reshade::addon_event::clear_render_target_view>(aa_target::OnClearRenderTargetView);
  reshade::unregister_event<reshade::addon_event::present>(aa_target::OnPresent);
#if FIX_BLOOM
  reshade::unregister_event<reshade::addon_event::present>(bloom_fix::OnPresent);
#endif
  reshade::unregister_event<reshade::addon_event::destroy_device>(aa_target::OnDestroyDevice);

  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);

  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);

  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);

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
