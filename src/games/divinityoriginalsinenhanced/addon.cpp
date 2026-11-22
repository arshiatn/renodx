/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <d3d11.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../talesofarise_dlss/upscaling.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/dlaa_bridge.hpp"
#include "../twattila/draw_dispatch.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/frame_generation.hpp"
#include "../twattila/frame_trace.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/output_encoding.hpp"
#include "../twattila/pass_timing.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "../twattila/reflex.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/test_hooks.hpp"
#include "./ray_tracing.hpp"
#include "./shared.h"
#include "./upscaling.hpp"
#include "./vfx_brightness.hpp"

const std::string build_date = __DATE__;
const std::string build_time = __TIME__;

namespace {

ShaderInjectData shader_injection = {.tonemap_type = TONEMAP_EXTENDED, .purity_scale = 1.f};
float current_settings_mode = 0.f;

// Graphics (twattila/graphics_page.hpp): the helper's upscalers and frame generation (upscaling.hpp), the rays
// (ray_tracing.hpp: sun shadows, AO, Global Illumination, Path-Traced Ambient). The game has no GT-VBAO, GT-VBGI, Bend or
// ray-traced reflections yet: those rows bind to values that stay 0 (the page's Bindings say the methods are missing).
twattila::feature_settings::AaSliders aa_sliders;
twattila::rt_scene::Sliders rt_sliders;
twattila::feature_settings::SssSliders no_screen_space_shadows;
float no_gtvbao = 0.f;
// The game's own SSR (its settings object's byte 0x4E, which no setting of the game writes: README "Engine facts").
float game_ssr = 0.f;
// Installed when an effect is on at launch (the scene's shaders are kept from the game's start) and the helper is there.
bool ray_tracing_installed = false;
// Ground Marker Brightness (vfx_brightness.hpp's markers), 1 the game's.
float marker_brightness = 1.f;
// Installed when VFX or Ground Marker Brightness is not 1 at launch (their shaders are patched as the game creates them).
bool vfx_brightness_installed = false;

// The page's texts (graphics_page.hpp Texts): the game's FXAA / SMAA, the helper's upscalers at 100% (the tone mapper's
// input), the rays' sun shadows and AO in place of the game's.
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = {"Game's FXAA / SMAA", "", "", "", "", "DLSS", "FSR", "XeSS"},
    .aa_tooltip = "Game's FXAA / SMAA: the game's own anti-aliasing (its graphics menu).\n"
                  "DLSS: NVIDIA's (RTX), as DLAA at full resolution, the best. FSR: AMD's, FSR 4 on Radeon RX 6000 and newer,\n"
                  "else 3.1. XeSS: Intel's, any GPU. Ray tracing needs one of them. Needs renodx-tw-dlaa64.exe and its DLLs in\n"
                  "the game folder.",
    .in_game_aa_note = "In-game Anti-Aliasing (FXAA / SMAA) must be Off for DLSS, FSR, XeSS and Frame Generation.",
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to its default: the game's shadows and SSAO.",
    .no_rays_tooltip = "No ray tracing: the game's shadows and SSAO. The rest of this page at its default.",
    .rays_tooltip = "Ray-traced sun shadows, ambient occlusion and global illumination at my settings. The rest of this page at\n"
                    "its default. Needs an upscaler (Anti-Aliasing above); on NVIDIA also turns DLSS Ray Reconstruction on.\n"
                    "Restart the game after turning ray tracing on.",
    .shadow_tooltip = "Game's Shadow Maps: the original. DXR: ray-traced sun shadows in place of the game's (needs in-game\n"
                      "shadows on). Needs an upscaler. Restart the game after turning it on.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep in-game SSAO on.\n"
                  "Game's SSAO: the original. RTAO: from rays through the scene, in place of the game's.\n"
                  "Path-Traced: the sky's light itself from Global Illumination's paths, its shade included.\n"
                  "Needs an upscaler. Restart the game after turning it on.",
    .gi_tooltip_end = "\nRestart the game after turning it on.",
};
// The page's anti-aliasing: the game's, DLSS, FSR, XeSS.
constexpr std::array<int, 4> AA_METHODS = {0, 5, 6, 7};

bool IsAdvancedSettings() {
  return current_settings_mode == 1.f;
}

bool IsHDR() {
  return HDR == 1.f;
}

bool IsPragmap() {
  return shader_injection.tonemap_type == TONEMAP_PRAGMAP;
}

bool IsPsychoV30() {
  return shader_injection.tonemap_type == TONEMAP_PSYCHO;
}

bool ShowPsychoSettings() {
  return IsAdvancedSettings() && IsPsychoV30();
}

bool ShowHDRSettings() {
  return IsAdvancedSettings() && IsHDR();
}

bool ShowPragmapSettings() {
  return IsAdvancedSettings() && IsPragmap();
}

bool ShowExtendedSettings() {
  return IsAdvancedSettings() && shader_injection.tonemap_type == TONEMAP_EXTENDED;
}

bool ShowPsychoV35Settings() {
  return IsAdvancedSettings() && shader_injection.tonemap_type == TONEMAP_PSYCHO35;
}

// Hue Clip Emulation's default for both modes (test/tw_divinity1's hue report: fire closest to the game's SDR picture).
constexpr float HUE_CLIP_DEFAULT = 70.f;

// PsychoV35's defaults that follow the game's response (shared.h): Hable's darker shadows and its colour.
const std::vector<std::pair<std::string, float>> V35_GAME_RESPONSE = {
    {"v35_contrast", DIVINITY_V35_CONTRAST},
    {"v35_shadows", DIVINITY_V35_SHADOWS},
    {"v35_saturation", DIVINITY_V35_SATURATION},
    {"v35_highlight_contrast", DIVINITY_V35_HIGHLIGHT_CONTRAST},
    {"v35_shadow_contrast", DIVINITY_V35_SHADOW_CONTRAST},
};

// A saved value outside the modes (an old build's) becomes the default mode.
float NormalizeTonemapMode(float value) {
  if (value == TONEMAP_SDR || value == TONEMAP_PSYCHO || value == TONEMAP_PRAGMAP || value == TONEMAP_EXTENDED
      || value == TONEMAP_PSYCHO35) {
    return value;
  }
  return TONEMAP_EXTENDED;
}

// Peak / Paper White / UI, Settings Mode and output are kept.
void ApplyResetPreset() {
  renodx::utils::settings::UpdateSettings({
      {"tonemap_type", TONEMAP_EXTENDED},
      {"ext_hue_clip", HUE_CLIP_DEFAULT},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"purity_scale", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"cone_response_exponent", 1.f},
      {"firehighlightmultiplier", 0.5f},
      {"candlehighlightmultiplier", 1.f},
      {"legacy_brazier_boost_enabled", 0.f},
      {"candlehighlightmultiplierpsycho", 1.f},
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"godrays", 1.f},
      {"ui", 1.f},
      {"playerLightIntensity", 1.f},
      {"playerLightRadius", 1.f},
      {"sun_shadow_softness", 1.f},
      {"grade_shoulder", 0.f},
      {"vfx_base_brightness", 1.f},
      {"some_indicators", 1.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
  renodx::utils::settings::UpdateSettings(V35_GAME_RESPONSE);
}

// The game's own picture. The SDR button also sets Output SDR; a first start on a display without HDR gets it through
// Output Auto.
void ApplySdrPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemap_type", TONEMAP_SDR}});
}

// The Recommended button (PsychoV35, the bloom of ATN's earlier Recommended); Output Auto takes it once when Windows HDR
// comes on after a first start without HDR.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemap_type", TONEMAP_PSYCHO35}, {"bloom", 0.25f}});
}

// Frame capture //////////////////////////////////////////////////////////////////////////
// Save thumbnails: the game copies the frame into an 8-bit texture (read back on the CPU).
// The frame is 16-bit with RenoDX and RenoDX only copies identical formats, so the copy was
// dropped and the thumbnail was black. We draw the frame into an 8-bit texture
// (frame_capture.ps_5_0.hlsl) and do the game's copy from that.
namespace frame_capture {

struct Pass {
  ID3D11Device* device = nullptr;
  ID3D11VertexShader* vs = nullptr;
  ID3D11PixelShader* ps = nullptr;
  ID3D11Texture2D* target = nullptr;  // 8-bit frame
  ID3D11RenderTargetView* rtv = nullptr;
  D3D11_TEXTURE2D_DESC target_desc = {};
  ID3D11Texture2D* source = nullptr;  // copy of the frame if it can't be sampled
  D3D11_TEXTURE2D_DESC source_desc = {};
};

std::mutex mutex;
Pass pass;
std::atomic<bool> logged_copy = false;
std::atomic<bool> logged_failure = false;
std::vector<std::string> logged_skips;  // under the mutex

template <typename T>
void SafeRelease(T*& object) {
  if (object != nullptr) object->Release();
  object = nullptr;
}

// Caller holds the mutex.
void Release() {
  SafeRelease(pass.vs);
  SafeRelease(pass.ps);
  SafeRelease(pass.rtv);
  SafeRelease(pass.target);
  SafeRelease(pass.source);
  pass = {};
}

// What RenoDX knows about a texture: the game's format and the resource holding the data
// (RenoDX's 16-bit copy if active).
struct Info {
  reshade::api::resource actual = {0u};
  reshade::api::format original = reshade::api::format::unknown;
  reshade::api::format format = reshade::api::format::unknown;
  bool swap_chain = false;
};

bool GetInfo(reshade::api::resource resource, Info* out) {
  bool texture = false;
  renodx::utils::resource::GetResourceInfo(resource, [&](const renodx::utils::resource::ResourceInfo& info) {
    const bool cloned = info.clone_enabled && info.clone.handle != 0u;
    const auto& desc = cloned ? info.clone_desc : info.desc;
    texture = desc.type == reshade::api::resource_type::texture_2d;
    out->actual = cloned ? info.clone : resource;
    out->original = info.upgraded ? info.fallback_desc.texture.format : info.desc.texture.format;
    out->format = desc.texture.format;
    out->swap_chain = info.is_swap_chain;
  });
  return texture && out->actual.handle != 0u;
}

bool IsEightBitColor(reshade::api::format format) {
  switch (reshade::api::format_to_typeless(format)) {
    case reshade::api::format::r8g8b8a8_typeless:
    case reshade::api::format::b8g8r8a8_typeless:
    case reshade::api::format::b8g8r8x8_typeless:
      return true;
    default:
      return false;
  }
}

// The game's pipeline state we change.
struct State {
  static constexpr UINT kInstances = 256;
  ID3D11InputLayout* layout = nullptr;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  ID3D11VertexShader* vs = nullptr;
  ID3D11HullShader* hs = nullptr;
  ID3D11DomainShader* ds = nullptr;
  ID3D11GeometryShader* gs = nullptr;
  ID3D11PixelShader* ps = nullptr;
  ID3D11ClassInstance* instances[5][kInstances] = {};
  UINT instance_counts[5] = {kInstances, kInstances, kInstances, kInstances, kInstances};
  ID3D11ShaderResourceView* srv = nullptr;
  ID3D11RasterizerState* rasterizer = nullptr;
  UINT viewport_count = 0;
  D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  ID3D11BlendState* blend = nullptr;
  FLOAT blend_factor[4] = {};
  UINT sample_mask = 0;
  ID3D11DepthStencilState* depth = nullptr;
  UINT stencil_ref = 0;
  ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* dsv = nullptr;

  void Save(ID3D11DeviceContext* context) {
    context->IAGetInputLayout(&layout);
    context->IAGetPrimitiveTopology(&topology);
    context->VSGetShader(&vs, instances[0], &instance_counts[0]);
    context->HSGetShader(&hs, instances[1], &instance_counts[1]);
    context->DSGetShader(&ds, instances[2], &instance_counts[2]);
    context->GSGetShader(&gs, instances[3], &instance_counts[3]);
    context->PSGetShader(&ps, instances[4], &instance_counts[4]);
    context->PSGetShaderResources(0, 1, &srv);
    context->RSGetState(&rasterizer);
    context->RSGetViewports(&viewport_count, nullptr);
    context->RSGetViewports(&viewport_count, viewports);
    context->OMGetBlendState(&blend, blend_factor, &sample_mask);
    context->OMGetDepthStencilState(&depth, &stencil_ref);
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
  }

  void Restore(ID3D11DeviceContext* context) {
    ID3D11ShaderResourceView* no_srv = nullptr;
    context->PSSetShaderResources(0, 1, &no_srv);  // the frame may be a render target again
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
    context->OMSetBlendState(blend, blend_factor, sample_mask);
    context->OMSetDepthStencilState(depth, stencil_ref);
    context->RSSetState(rasterizer);
    context->RSSetViewports(viewport_count, viewports);
    context->IASetInputLayout(layout);
    context->IASetPrimitiveTopology(topology);
    context->VSSetShader(vs, instances[0], instance_counts[0]);
    context->HSSetShader(hs, instances[1], instance_counts[1]);
    context->DSSetShader(ds, instances[2], instance_counts[2]);
    context->GSSetShader(gs, instances[3], instance_counts[3]);
    context->PSSetShader(ps, instances[4], instance_counts[4]);
    context->PSSetShaderResources(0, 1, &srv);

    SafeRelease(layout);
    SafeRelease(vs);
    SafeRelease(hs);
    SafeRelease(ds);
    SafeRelease(gs);
    SafeRelease(ps);
    for (auto& stage : instances) {
      for (auto*& instance : stage) SafeRelease(instance);
    }
    SafeRelease(srv);
    SafeRelease(rasterizer);
    SafeRelease(blend);
    SafeRelease(depth);
    for (auto*& rtv : rtvs) SafeRelease(rtv);
    SafeRelease(dsv);
  }
};

// Draws the 16-bit frame into our 8-bit texture of the given format family. Caller holds the mutex.
bool Draw(ID3D11DeviceContext* context, ID3D11Device* device, ID3D11Texture2D* frame, DXGI_FORMAT family) {
  if (pass.device != device) {
    Release();
    pass.device = device;
  }
  if (pass.vs == nullptr
      && FAILED(device->CreateVertexShader(__swap_chain_proxy_vertex_shader_dx11.data(), __swap_chain_proxy_vertex_shader_dx11.size(), nullptr, &pass.vs))) {
    pass.vs = nullptr;
    return false;
  }
  if (pass.ps == nullptr
      && FAILED(device->CreatePixelShader(__frame_capture.data(), __frame_capture.size(), nullptr, &pass.ps))) {
    pass.ps = nullptr;
    return false;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  frame->GetDesc(&desc);
  if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1) return false;

  // Something we can sample.
  ID3D11Texture2D* readable = frame;
  if ((desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0u) {
    auto copy_desc = desc;
    copy_desc.Usage = D3D11_USAGE_DEFAULT;
    copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy_desc.CPUAccessFlags = 0;
    copy_desc.MiscFlags = 0;
    if (pass.source == nullptr || std::memcmp(&pass.source_desc, &copy_desc, sizeof(copy_desc)) != 0) {
      SafeRelease(pass.source);
      if (FAILED(device->CreateTexture2D(&copy_desc, nullptr, &pass.source))) {
        pass.source = nullptr;
        return false;
      }
      pass.source_desc = copy_desc;
    }
    context->CopyResource(pass.source, frame);
    readable = pass.source;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = static_cast<DXGI_FORMAT>(reshade::api::format_to_default_typed(
      reshade::api::format_to_typeless(static_cast<reshade::api::format>(desc.Format)), 0));
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MostDetailedMip = 0;
  srv_desc.Texture2D.MipLevels = 1;
  ID3D11ShaderResourceView* srv = nullptr;
  if (FAILED(device->CreateShaderResourceView(readable, &srv_desc, &srv))) return false;

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = desc.Width;
  target_desc.Height = desc.Height;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = family;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (pass.target == nullptr || std::memcmp(&pass.target_desc, &target_desc, sizeof(target_desc)) != 0) {
    SafeRelease(pass.rtv);
    SafeRelease(pass.target);
    // Plain (not sRGB) view: the same bits the game's 8-bit back buffer would hold.
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = static_cast<DXGI_FORMAT>(reshade::api::format_to_default_typed(static_cast<reshade::api::format>(family), 0));
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(device->CreateTexture2D(&target_desc, nullptr, &pass.target))
        || FAILED(device->CreateRenderTargetView(pass.target, &rtv_desc, &pass.rtv))) {
      SafeRelease(pass.rtv);
      SafeRelease(pass.target);
      srv->Release();
      return false;
    }
    pass.target_desc = target_desc;
  }

  auto state = std::make_unique<State>();
  state->Save(context);

  context->OMSetRenderTargets(1, &pass.rtv, nullptr);  // first: the frame may be bound as a target
  context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
  context->OMSetDepthStencilState(nullptr, 0);
  context->RSSetState(nullptr);
  const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(pass.vs, nullptr, 0);
  context->HSSetShader(nullptr, nullptr, 0);
  context->DSSetShader(nullptr, nullptr, 0);
  context->GSSetShader(nullptr, nullptr, 0);
  context->PSSetShader(pass.ps, nullptr, 0);
  context->PSSetShaderResources(0, 1, &srv);
  context->Draw(3, 0);

  state->Restore(context);
  srv->Release();
  return true;
}

// Log: mismatched copies we leave alone (once each).
void LogSkip(const Info& from, const Info& to, bool region) {
  char text[192];
  std::snprintf(text, sizeof(text), "frame_capture: left a %scopy (%sformat %u, game %u -> format %u, game %u)",
                region ? "region " : "", from.swap_chain ? "back buffer, " : "",
                static_cast<uint32_t>(from.format), static_cast<uint32_t>(from.original),
                static_cast<uint32_t>(to.format), static_cast<uint32_t>(to.original));
  {
    const std::scoped_lock lock(mutex);
    if (logged_skips.size() >= 32 || std::find(logged_skips.begin(), logged_skips.end(), text) != logged_skips.end()) return;
    logged_skips.emplace_back(text);
  }
  reshade::log::message(reshade::log::level::info, text);
}

// Does the game's copy when RenoDX can't (16-bit frame into an 8-bit texture).
bool Copy(
    reshade::api::command_list* cmd_list,
    reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box,
    reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box,
    bool region) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return false;
  Info from = {};
  Info to = {};
  if (!GetInfo(source, &from) || !GetInfo(dest, &to)) return false;
  if (from.format == to.format) return false;  // RenoDX copies it

  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  auto* from_resource = reinterpret_cast<ID3D11Resource*>(from.actual.handle);
  auto* to_resource = reinterpret_cast<ID3D11Resource*>(to.actual.handle);
  const auto family = reshade::api::format_to_typeless(to.format);
  const bool eight_bit_dest = IsEightBitColor(to.original) && to.format == to.original;

  if (eight_bit_dest && reshade::api::format_to_typeless(from.format) == family) {
    // Same format family, other type: fine for D3D11, RenoDX only copies identical formats.
    if (region) return false;  // RenoDX lets the game's copy run
    context->CopyResource(to_resource, from_resource);
    return true;
  }

  // The 16-bit back buffer, or an upgraded texture that was 8-bit like the destination.
  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  from_resource->GetType(&dimension);
  if (!eight_bit_dest
      || (!from.swap_chain && reshade::api::format_to_typeless(from.original) != family)
      || source_subresource != 0u
      || dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
    LogSkip(from, to, region);
    return false;
  }

  ID3D11Device* device = nullptr;
  context->GetDevice(&device);
  if (device == nullptr) return false;
  bool done = false;
  {
    const std::scoped_lock lock(mutex);
    if (Draw(context, device, static_cast<ID3D11Texture2D*>(from_resource), static_cast<DXGI_FORMAT>(family))) {
      if (region) {
        context->CopySubresourceRegion(
            to_resource, dest_subresource,
            dest_box != nullptr ? dest_box->left : 0u,
            dest_box != nullptr ? dest_box->top : 0u,
            dest_box != nullptr ? dest_box->front : 0u,
            pass.target, 0u, reinterpret_cast<const D3D11_BOX*>(source_box));
      } else {
        context->CopyResource(to_resource, pass.target);
      }
      done = true;
    }
  }
  device->Release();

  if (done ? !logged_copy.exchange(true) : !logged_failure.exchange(true)) {
    char text[160];
    std::snprintf(text, sizeof(text), "frame_capture: %s (%s, format %u -> %u)",
                  done ? "16-bit frame copied into an 8-bit texture" : "copy failed",
                  from.swap_chain ? "back buffer" : "upgraded texture",
                  static_cast<uint32_t>(from.format), static_cast<uint32_t>(to.format));
    reshade::log::message(done ? reshade::log::level::info : reshade::log::level::warning, text);
  }
  return done;
}

// Registered after RenoDX (it drops these copies).
bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest) {
  return Copy(cmd_list, source, 0u, nullptr, dest, 0u, nullptr, false);
}

bool OnCopyTextureRegion(
    reshade::api::command_list* cmd_list,
    reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box,
    reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box,
    reshade::api::filter_mode) {
  return Copy(cmd_list, source, source_subresource, source_box, dest, dest_subresource, dest_box, true);
}

void OnDestroyDevice(reshade::api::device*) {
  const std::scoped_lock lock(mutex);
  Release();
}

}  // namespace frame_capture

// The 3D view drew this frame (the tone mapper ran): the frame trace's FrameTrace count.
std::atomic_bool tonemap_drawn = false;

// Each replaced pass's first draw is logged once: a run counts by these "applied" lines. Before the tone mapper's draw its
// input goes through the helper's upscaler (upscaling.hpp); before CombineUI's (the UI over the back buffer) frame
// generation keeps the HUD-less image.
#define UpgradeRTVReplaceShader(value)                                                               \
  {                                                                                                   \
      value,                                                                                          \
      {                                                                                               \
          .crc32 = value,                                                                             \
          .code = __##value,                                                                          \
          .on_draw = [](auto* cmd_list) {                                                             \
            static std::atomic_bool applied = false;                                                  \
            if (!applied.load(std::memory_order_relaxed) && !applied.exchange(true)) {                \
              renodx::utils::log::i(std::format("[Divinity] Applied 0x{:08X} (Rendering Mode {}, output {}).", \
                                                value, shader_injection.tonemap_type, shader_injection.swap_chain_encoding)); \
            }                                                                                         \
            if constexpr (value == 0x30A1D40D) {                                                      \
              tonemap_drawn.store(true, std::memory_order_relaxed);                                   \
            }                                                                                         \
            auto rtvs = renodx::utils::swapchain::GetRenderTargets(cmd_list);                         \
            bool changed = false;                                                                     \
            for (auto rtv : rtvs) {                                                                   \
              changed = renodx::mods::swapchain::ActivateCloneHotSwap(cmd_list->get_device(), rtv) || changed;   \
            }                                                                                         \
            if (changed) {                                                                            \
              renodx::mods::swapchain::FlushDescriptors(cmd_list);                                    \
              renodx::mods::swapchain::RewriteRenderTargets(cmd_list, rtvs.size(), rtvs.data(), {0}); \
            }                                                                                         \
            if constexpr (value == 0x30A1D40D || value == 0x20163A55) {                               \
              auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());         \
              renodx::utils::directx::TryNativeFromReShadeProxy(&context);                            \
              if constexpr (value == 0x30A1D40D) {                                                    \
                divinity::upscaling::OnTonemap(context);                                              \
              } else {                                                                                \
                divinity::upscaling::OnCombineUi(context);                                            \
              }                                                                                       \
            }                                                                                         \
            return true; },                                                                           \
      },                                                                                              \
  }

renodx::mods::shader::CustomShaders custom_shaders = {

    // AA, between tonemapper and UI (SAME ORDER)
    UpgradeRTVReplaceShader(0x30A1D40D), // Tonemapper
    UpgradeRTVReplaceShader(0x9B114FEF), // Highlighting with cursor
    UpgradeRTVReplaceShader(0x83553BB1), // Highlighting with cursor
    UpgradeRTVReplaceShader(0x2400D20A), // Common post-pass
    UpgradeRTVReplaceShader(0x35F39F1D), // FXAA
    UpgradeRTVReplaceShader(0xB7FC46C3), // SMAA Pass 1
    UpgradeRTVReplaceShader(0x6AF33A9B), // SMAA Pass 2
    UpgradeRTVReplaceShader(0xFB839D33), // SMAA Pass 3
    UpgradeRTVReplaceShader(0x25539638), // SMAA Pass 1 with LightVolume on
    UpgradeRTVReplaceShader(0xAD516F39), // SMAA Pass 2 with LightVolume on
    UpgradeRTVReplaceShader(0x47E69078), // SMAA Pass 3 with LightVolume on
    UpgradeRTVReplaceShader(0xDDBCD866), // Vignette
    UpgradeRTVReplaceShader(0x20163A55), // Final UI/copy target must retain HDR RGB
    __ALL_CUSTOM_SHADERS,
};

// Settings //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
renodx::utils::settings::Settings settings = {
    new renodx::utils::settings::Setting{
        .key = "SettingsMode",
        .binding = &current_settings_mode,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .can_reset = false,
        .label = "Settings Mode",
        .labels = {"Simple", "Advanced", "Graphics"},
        .is_global = true,
    },
    // GammaCorrection (graphicSettings.lsx, 1 by default) goes to a renderer method as 1 / value (EoCApp.exe 0x1407DF7F8 at
    // the start, 0x140AF3AB1 from the options menu); no shader of the game reads a gamma value (the tone map encodes a
    // fixed 1 / 2.2), and a display gamma ramp needs exclusive fullscreen, which the swap chain mod prevents.
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "Keep the game's Gamma Correction at 1 (neutral). The HDR modes ignore it.",
        .section = "General",
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
        .is_enabled = IsHDR,
        .is_visible = IsHDR,
    },
    new renodx::utils::settings::Setting{
        .key = "diffuse_white_nits",
        .binding = &shader_injection.diffuse_white_nits,
        .default_value = 203.f,
        .label = "Scene (Paper White)",
        .section = "General",
        .tooltip = "Brightness of the scene/game.",
        .min = 1.f,
        .max = 500.f,
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
    },
    new renodx::utils::settings::Setting{
        .key = "tonemap_type",
        .binding = &shader_injection.tonemap_type,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = TONEMAP_EXTENDED,
        .can_reset = true,
        .label = "Rendering Mode",
        .section = "General",
        .tooltip = "SDR (Native): the game's own picture.\n"
                   "HDR (Extended): the game's curve, extended past white.\n"
                   "HDR (PsychoV35): Musa's PsychoV35 on the game's mid gray.",
        .labels = {"SDR (Native)",
                   "HDR (UC2 Extended / PsychoV30)",
                   "HDR (UC2 Extended / Pragmap)",
                   "HDR (Extended)",
                   "HDR (PsychoV35)"},
    },
    new renodx::utils::settings::Setting{
        .key = "ext_hue_clip",
        .binding = &shader_injection.ext_hue_clip,
        .default_value = HUE_CLIP_DEFAULT,
        .label = "Hue Clip Emulation",
        .section = "Extended",
        .tooltip = std::format("Bright colors take the hue SDR clips them to: fire more orange / yellow, sky more cyan.\n"
                               "0 = off. Default {:.0f}: the closest to the SDR picture.",
                               HUE_CLIP_DEFAULT),
        .max = 100.f,
        .is_enabled = ShowExtendedSettings,
        .parse = [](float value) { return value * 0.01f; },
        .is_visible = ShowExtendedSettings,
    },

    // Presets //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended",
        .section = "Presets",
        .group = "preset-buttons-0",
        .tooltip = "HDR (PsychoV35) with less bloom.\nPeak, Paper White, UI, Settings Mode and Output are kept.",
        .on_change = []() { ApplyRecommendedPreset(); },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-buttons-0",
        .tooltip = "Every image setting at its default (HDR Extended).\nPeak, Paper White, UI, Settings Mode and Output are kept.",
        .on_change = []() { ApplyResetPreset(); },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "SDR (monitors without HDR)",
        .section = "Presets",
        .group = "preset-buttons-0",
        .tooltip = "The game's own picture and Output SDR (restart the game if it was HDR).\n"
                   "Peak, Paper White, UI and Settings Mode are kept.",
        .tint = 0xFFC107,
        .on_change = []() {
          ApplySdrPreset();
          renodx::utils::settings::UpdateSettings({{"SwapChainEncoding", static_cast<float>(twattila::output_encoding::SDR)}});
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "PsychoV30 (earlier Recommended)",
        .section = "Presets",
        .group = "preset-buttons-1",
        .tooltip = "HDR (UC2 Extended / PsychoV30) with less bloom, the brazier and candle boosts.\n"
                   "Peak, Paper White, UI, Settings Mode and Output are kept.",
        .on_change = []() {
          ApplyResetPreset();
          renodx::utils::settings::UpdateSettings({
              {"tonemap_type", TONEMAP_PSYCHO},
              {"legacy_brazier_boost_enabled", 2.f},
              {"candlehighlightmultiplierpsycho", 2.5f},
              {"bloom", 0.25f},
          });
        },
    },
    // PsychoV30 //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .key = "exposure",
        .binding = &shader_injection.exposure,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Exposure",
        .section = "PsychoV30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "highlights",
        .binding = &shader_injection.highlights,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Highlights",
        .section = "PsychoV30",
        .tooltip = "1.00 is neutral. Lower values reduce bright tones; higher values strengthen them. This is a nonlinear control.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "purity_scale",
        .binding = &shader_injection.purity_scale,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Saturation / Purity",
        .section = "PsychoV30",
        .tooltip = "Colour purity. 1.00: neutral.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "shadows",
        .binding = &shader_injection.shadows,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Shadows",
        .section = "PsychoV30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "contrast",
        .binding = &shader_injection.contrast,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Contrast",
        .section = "PsychoV30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "cone_response_exponent",
        .binding = &shader_injection.cone_response_exponent,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Cone Response Exponent",
        .section = "PsychoV30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    // Extra //////////////////////////////////////////////////////////////////////////////////////
        new renodx::utils::settings::Setting{
        .key = "firehighlightmultiplier",
        .binding = &shader_injection.firehighlightmultiplier,
        .default_value = 0.50f,
        .label = "Fire Highlights",
        .section = "Pragmap Effects",
        .tooltip = "Extra brightness for fire, on top of Highlights.",
        .min = 0.50f,
        .max = 1.50f,
        .format = "%.2f",
        .is_enabled = IsPragmap,
        .is_visible = ShowPragmapSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "candlehighlightmultiplier",
        .binding = &shader_injection.candlehighlightmultiplier,
        .default_value = 1.0f,
        .label = "Candle / Staff / Small Particle Highlights",
        .section = "Pragmap Effects",
        .tooltip = "Extra brightness for candles, staffs and small glowing particles, on top of Highlights.",
        .min = 0.50f,
        .max = 1.50f,
        .format = "%.2f",
        .is_enabled = IsPragmap,
        .is_visible = ShowPragmapSettings,
    },

    new renodx::utils::settings::Setting{
        .key = "legacy_brazier_boost_enabled",
        .binding = &shader_injection.brazier_boost_enabled,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Brazier Color / Brightness Boost",
        .section = "Fire / Particles",
        .tooltip = "Brighter fire and braziers.\nOff: the game's.\nOn (old version): brighter and redder.\n"
                   "On (new color channel boost): brighter, keeping more of the yellow.",
        .labels = {"Off", "On (old version)", "On (new color channel boost)"},
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "candlehighlightmultiplierpsycho",
        .binding = &shader_injection.candlehighlightmultiplierpsycho,
        .default_value = 1.0f,
        .label = "Candle / Staff / Small Particle Highlights",
        .section = "Fire / Particles",
        .tooltip = "Brightness of candles, staffs and small glowing particles. 1.0: the game's.",
        .min = 1.0f,
        .max = 10.0f,
        .format = "%.1f",
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "bloom",
        .binding = &shader_injection.bloom,
        .default_value = 1.f,
        .label = "Bloom",
        .section = "Extra",
        .tooltip = "Strength of the bloom (HDR only). 1.00: the game's.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = IsHDR,
        .is_visible = ShowHDRSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "vfx_base_brightness",
        .binding = &shader_injection.vfx_brightness,
        .default_value = 1.f,
        .label = "VFX Brightness (Experimental)",
        .section = "Extra",
        .tooltip = "Brightness of fire, spells and particles (HDR only). 1.00: the game's.\n"
                   "Restart the game after first moving it from 1.",
        .min = 0.f,
        .max = 10.f,
        .format = "%.2f",
        .is_enabled = IsHDR,
        .is_visible = ShowHDRSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "some_indicators",
        .binding = &marker_brightness,
        .default_value = 1.f,
        .label = "Ground Marker Brightness",
        .section = "Extra",
        .tooltip = "Brightness of target circles, range previews and selection markers, useful at night (HDR only).\n"
                   "1.00: the game's. Restart the game after first moving it from 1.",
        .min = 0.f,
        .max = 10.f,
        .format = "%.2f",
        .is_enabled = IsHDR,
        .is_visible = ShowHDRSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "vignette",
        .binding = &shader_injection.vignette,
        .default_value = 1.f,
        .label = "Vignette",
        .section = "Extra",
        .tooltip = "Strength of the darkened screen edges. 1.00: the game's.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = IsHDR,
        .is_visible = ShowHDRSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "godrays",
        .binding = &shader_injection.godrays,
        .default_value = 1.f,
        .label = "Godrays",
        .section = "Extra",
        .tooltip = "Strength of the god rays. 1.0: the game's.",
        .min = 0.f,
        .max = 10.f,
        .format = "%.1f",
        .is_enabled = IsHDR,
        .is_visible = ShowHDRSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "ui",
        .binding = &shader_injection.ui,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "UI",
        .section = "Game-Specific Settings",
        .tooltip = "Off hides the UI, for screenshots in the pause menu.",
        .labels = {"Off", "On"},
        .is_visible = IsAdvancedSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "playerLightIntensity",
        .binding = &shader_injection.player_light_intensity,
        .default_value = 1.f,
        .label = "Player Light Intensity",
        .section = "Game-Specific Settings",
        .tooltip = "Brightness of the lights that cast shadows, such as the party's own light. 1.0: the game's.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.1f",
        .is_enabled = IsHDR,
        .is_visible = ShowHDRSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "playerLightRadius",
        .binding = &shader_injection.local_light_intensity,
        .default_value = 1.f,
        .label = "Local Light Intensity",
        .section = "Game-Specific Settings",
        .tooltip = "Brightness of the lights without shadows: torches, lamps and most others. 1.0: the game's.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.1f",
        .is_enabled = IsHDR,
        .is_visible = ShowHDRSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "sun_shadow_softness",
        .binding = &shader_injection.sun_shadow_softness,
        .default_value = 1.f,
        .label = "Sun Shadow Softness (Experimental)",
        .section = "Game-Specific Settings",
        .tooltip = "How soft the sun's shadows get away from what casts them (Ultra shadows). 1: the game's.",
        .min = 0.25f,
        .max = 3.f,
        .format = "%.2f",
        .is_visible = IsAdvancedSettings,
    },
    // The game's grade after the curve with a smooth shoulder from 0.8 instead of the knee at 1 (shaders/tonemappers.hlsli
    // GradeAfterCurve): a look change for ATN to decide, up to 0.033 OkLab near white (test/tw_divinity1 check 4).
    new renodx::utils::settings::Setting{
        .key = "grade_shoulder",
        .binding = &shader_injection.grade_shoulder,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = "Smooth Grade Shoulder (Experimental)",
        .section = "Game-Specific Settings",
        .tooltip = "The game's colour grade eases toward white from 80% brightness instead of stopping at 100%:\n"
                   "smoother bright gradients, colours near white change slightly.",
        .labels = {"Off", "On"},
        .is_visible = [] { return ShowExtendedSettings() || ShowPsychoV35Settings(); },
    },
    new renodx::utils::settings::Setting{
        .key = "GameScreenSpaceReflections",
        .binding = &game_ssr,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = "Game's Screen-Space Reflections (Experimental)",
        .section = "Game-Specific Settings",
        .tooltip = "The game's own screen-space reflections, which its menu does not offer. Restart the game after\n"
                   "changing it.",
        .labels = {"Off", "On"},
        .is_visible = IsAdvancedSettings,
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Trace One Frame",
        .section = "Tools",
        .group = "tools",
        .tooltip = "Writes tw_frame_trace.txt next to the game's exe about a second later, for bug reports.",
        .on_change = []() {
          // FrameTrace's on-demand marker (frame_trace.hpp looks for it every 60 presents).
          std::wstring exe(32768, L'\0');
          exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
          std::ofstream(std::filesystem::path(exe).parent_path() / L"renodx_trace_now", std::ios::binary | std::ios::trunc);
          renodx::utils::log::i("[Divinity] Trace One Frame: renodx_trace_now written.");
        },
        .is_visible = IsAdvancedSettings,
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Time Each Pass (One Frame)",
        .section = "Tools",
        .group = "tools",
        .tooltip = "The GPU's time of every pass of the next frame into divinity_pass_timing.txt next to the game's exe.",
        .on_change = []() { twattila::pass_timing::Request(); },
        .is_visible = IsAdvancedSettings,
    },
};

bool configured = false;
bool attached = false;
bool addon_registered = false;

// The Anti-Aliasing section's status line (graphics_page.hpp Bindings::upscaler_status): the upscaler's, the rays'.
std::string UpscalerStatus() {
  std::string status = divinity::upscaling::Status();
  if (ray_tracing_installed) status += "\n" + twattila::rt_scene::Status();
  return status;
}

// The frame trace's (renodx_trace_now next to the exe; FrameTrace=1 in ReShade.ini's [renodx]: 600 frames of the 3D view
// in). The Graphics page's rows for the next frame: the bridge's use, the upscaler's jitter, the rays, frame generation
// and Reflex.
void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twattila::frame_trace::OnPresent(false, tonemap_drawn.exchange(false, std::memory_order_relaxed));
  auto* device = swapchain->get_device();
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  // DLSS needs NVIDIA's GPU: elsewhere XeSS on Intel's, else FSR (a GPU not known yet changes no saved choice).
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(device->get_native()));
  if (aa_sliders.method == 5.f && twattila::dlaa_bridge::gpu_vendor != 0 && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  if (vfx_brightness_installed) divinity::vfx_brightness::Update(VFX_BRIGHTNESS, (HDR == 1.f ? marker_brightness : 1.f));
  const auto aa = twattila::feature_settings::AaSettings(aa_sliders);
  const bool upscaling = twattila::feature_settings::Upscaling(aa_sliders) && twattila::dlaa_bridge::HelperInstalled();
  auto rays = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, divinity::ray_tracing::light_bounce_strength);
  // Path-Traced Ambient for a host without rt_core's ambient cube (Divinity 2's route): the sun pass's sky in the paths
  // (ray_tracing.hpp), the rays' AO off under it (one AO method).
  rays.path_ambient = rt_sliders.path_ambient != 0.f && rays.path_tracing;
  rays.ambient_occlusion = rays.ambient_occlusion && !rays.path_ambient;
  const bool traced = ray_tracing_installed && upscaling && (rays.sun_shadows || rays.ambient_occlusion || rays.light_bounce);
  // Ray Reconstruction with a ray-traced effect only (DLSS's).
  const bool ray_reconstruction = traced && aa.ray_reconstruction;
  const auto display = device->get_resource_desc(swapchain->get_current_back_buffer()).texture;
  talesofarise::upscaling::SetUse({
      .ray_tracing = ray_tracing_installed && upscaling,
      // The G-buffer guides for Ray Reconstruction and the paths (their surfaces' colours).
      .guides = ray_reconstruction || (traced && rays.path_tracing),
      .upscaler = upscaling ? (aa_sliders.method == 7.f ? twattila::dlaa_bridge::UPSCALER_XESS
                                                        : (aa_sliders.method == 6.f ? twattila::dlaa_bridge::UPSCALER_FSR
                                                                                    : twattila::dlaa_bridge::UPSCALER_DLSS))
                                  + 1
                            : 0u,
      .ray_reconstruction = ray_reconstruction ? aa.ray_reconstruction_preset + 1 : 0u,
      .frame_format = twattila::frame_generation::Format(),
      .frame_backend = aa.frame_backend,
      .width = display.width,
      .height = display.height,
  });
  // DLSS's presets L and M and Ray Reconstruction get the colour's square root (memory "DLSS L/M dark tones").
  divinity::upscaling::Update({.upscaling = upscaling,
                               .preset = aa.dlaa_preset,
                               .square_root = upscaling && (ray_reconstruction || (aa_sliders.method == 5.f && aa.dlaa_preset != 11u)),
                               .ray_reconstruction = ray_reconstruction,
                               .ray_reconstruction_preset = aa.ray_reconstruction_preset,
                               .debug_view = aa_sliders.debug_view != 0.f},
                              traced);
  if (ray_tracing_installed) divinity::ray_tracing::Update(rays);
  twattila::frame_generation::Update({
      .multiplier = upscaling ? aa.frame_generation : 0u,
      .reflex_boost = aa.reflex == 2,
      .frame_limit = aa.frame_limit,
      .preset = aa.frame_preset,
      .hud_recomposition = aa.hud_recomposition,
      .debug_view = aa.frame_debug_view,
      .sdr_output = shader_injection.swap_chain_encoding == static_cast<float>(twattila::output_encoding::SDR),
  });
  twattila::reflex::Update(swapchain, aa.reflex, aa.frame_limit);
}

// The game's device is going away: the upscaler's, the rays', frame generation's and Reflex's objects on it with it, the
// bridge's last.
void OnDestroyHelperDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (ray_tracing_installed) divinity::ray_tracing::Release(device);
  twattila::frame_generation::OnDestroyDevice(native);
  twattila::reflex::OnDestroyDevice(native);
  divinity::upscaling::Release(native);
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
  // The game's SSR before its renderer starts (it reads the byte at its start and at each settings apply): only in the
  // exe the addresses were read from (README "Engine facts": the Steam EoCApp.exe, linked 0x56FD2C80).
  if (game_ssr != 0.f) {
    const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if (headers->FileHeader.TimeDateStamp == 0x56FD2C80u && headers->OptionalHeader.SizeOfImage > 0x12E3E38u) {
      if (auto* settings_object = *reinterpret_cast<uint8_t* const*>(image + 0x12E3E30); settings_object != nullptr) {
        settings_object[0x4E] = 1;
        renodx::utils::log::i("[Divinity] Game's Screen-Space Reflections: on in the game's graphics settings.");
      }
    }
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
}

void ConfigureAddon() {
  if (configured) return;

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
  renodx::mods::swapchain::resource_upgrade_infos.push_back({
      .old_format = reshade::api::format::r8g8b8a8_unorm,
      .new_format = reshade::api::format::r16g16b16a16_float,
      .ignore_size = true,
      .use_resource_view_cloning = true,
      .use_resource_view_hot_swap = true,
      .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::ANY,
      .usage_include = reshade::api::resource_usage::render_target,
  });

  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = static_cast<float>(twattila::output_encoding::AUTO),
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR (default).\n"
                   "HDR10: 10-bit. scRGB: 16-bit. SDR: displays without HDR.",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    // Decided at each new swap chain (device_loss's create_swapchain, before the swap chain mod's), Auto by Windows HDR
    // for the game's display.
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = static_cast<int>(setting->GetValue()),
        .first_start_without_hdr = &ApplySdrPreset,
        .first_start_key = "tonemap_type",
        .recommended_preset = &ApplyRecommendedPreset,
        .log_name = "[Divinity]",
    });
    settings.push_back(setting);
  }

  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::RENODX_PSYCHO_V30_V35,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "UC2 Extended by Musa and DabVeed. Divinity: Original Sin Enhanced Edition by ATN.",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::HELPER,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Build Date: " + build_date + " - " + build_time,
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
          .on_change = []() {
            renodx::utils::platform::LaunchURL("https://www.nexusmods.com/divinityoriginalsinenhancededition/mods/80");
          },
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

  auto v35_rows = twattila::psycho35::Rows(&shader_injection, ShowPsychoV35Settings, HUE_CLIP_DEFAULT);
  for (auto* row : v35_rows) {
    for (const auto& [key, value] : V35_GAME_RESPONSE) {
      if (row->key == key) {
        row->default_value = value;
      }
    }
  }
  twattila::psycho35::InsertBefore(&settings, "PsychoV30", v35_rows);

  // Graphics: the helper's upscalers at 100% (the game has no render scale of its own), Ray Reconstruction, frame
  // generation and Reflex; not the shader anti-aliasing (TW's) or object motion (the camera's alone here). Then the rays'
  // rows (ray_tracing.hpp: sun shadows, AO, Global Illumination's strength and methods, Path-Traced Ambient), the rows the
  // methods leave at Attila's RT/PT values, and the page.
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, false, true, true);
  std::erase_if(settings, [](renodx::utils::settings::Setting* setting) {
    constexpr std::array<std::string_view, 5> SHADER_AA_ROWS = {"AntiAliasingQuality", "AntiAliasingExtraSharpness", "AntiAliasingTaaStrength",
                                                                "AntiAliasingSharpening", "AntiAliasingObjectMotion"};
    if (std::ranges::find(SHADER_AA_ROWS, setting->key) == SHADER_AA_ROWS.end()) return false;
    delete setting;
    return true;
  });
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders);
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "light_bounce_strength",
      .binding = &divinity::ray_tracing::light_bounce_strength,
      .default_value = 0.f,
      .label = "Strength",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Lit ground and walls bounce their light and colour into nearby shadows.\n"
                 "1: physically correct; 0: off.",
      .min = 0.f,
      .max = 4.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedSkipShadowPass",
      .binding = &divinity::ray_tracing::skip_shadow_maps,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Skip the Game's Shadow Maps (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "With ray-traced shadows: nothing is drawn into the game's sun shadow maps, the rays replace them. Faster.",
      .labels = {"Off", "On"},
  });
  // The page has no place for it yet: it follows its last section, shown with the rays' AO.
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedSkipGameSsao",
      .binding = &divinity::ray_tracing::skip_ssao,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Skip the Game's SSAO (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "With ray-traced AO: the game's SSAO isn't drawn, the rays replace it. Faster.",
      .labels = {"Off", "On"},
      .is_visible = [] { return rt_sliders.ambient_occlusion != 0.f; },
  });
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  for (auto* setting : settings) {
    // The helper's opacity micromaps over the game's cut-out masks: off until a run shows them in this game.
    if (setting->key == "RayTracedLeafMicromaps" || setting->key == "RayTracedGpuMicromaps") setting->default_value = 0.f;
  }
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders,
                                             .sss = &no_screen_space_shadows,
                                             .rt = &rt_sliders,
                                             .ssao_mode = &no_gtvbao,
                                             .light_bounce_strength = &divinity::ray_tracing::light_bounce_strength,
                                             .texts = &PAGE_TEXTS,
                                             .aa_methods = AA_METHODS,
                                             .upscaler_status = &UpscalerStatus,
                                             .has_scale_rows = false,
                                             .has_foliage_light = false,
                                             .rays_preset_as_attila = true,
                                             .path_ambient_tested = false,
                                             .has_reflections = false,
                                             .has_gtvbao = false,
                                             .has_screen_space_shadows = false,
                                             .has_gtvbgi = false,
                                             .rays_tested = false,
                                             .has_fog_shadows = false,
                                             .every_row = false});
  // Settings Mode's pages: Simple and Advanced the HDR rows (and the output's), Graphics the page's; About on every
  // page.
  const std::vector<std::string> page_sections = twattila::graphics_page::Sections();
  for (auto* setting : settings) {
    if (setting->key == "SettingsMode" || setting->section == "About") continue;
    const bool graphics = std::ranges::find(page_sections, setting->section) != page_sections.end();
    setting->is_visible = [graphics, shown = setting->is_visible] {
      return (graphics ? current_settings_mode == 2.f : current_settings_mode < 2.f) && (shown == nullptr || shown());
    };
  }

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;
  // Time Each Pass: before every other draw hook (a skipped draw still gets its stamp).
  twattila::pass_timing::Install("divinity");
  ConfigureAddon();
  // Constants once each for the tone mapper, the exposure, the shadow filter, the sun, fog, SSAO, bloom's combine, the
  // point and spot lights, plain and shadowed, and the fire and candle materials' opacity fade and brightness (none of
  // the lights' or materials' in R1's traces) (b0-b13: the frame b13, the view b12; the camera fields are Attila's b0
  // registers, not this game's); blends of fire, candles and the UI combine.
  twattila::frame_trace::Install({
      .value_hashes = {0x30A1D40D, 0x4E112ABA, 0x8F114E47, 0xCCCB7253, 0xEDA7F8A1, 0x385DCF3C, 0x88B0D842, 0xD39E39C8,
                       0x2F56D195, 0x23B84721, 0x7CBAA94E, 0xED93F883, 0x93AEBB93},
      .value_slots = 14,
      .value_floats = 64,
      .on_demand = true,
      .at_100 = true,
      .blend_hashes = {0xED93F883, 0x93AEBB93, 0x20163A55},
  });
  // Before the swap chain mod: a removed device's chain let go, each new chain's output decided. The game makes a
  // new chain for its window 2-4 times a start and at each window-mode change: the last flip chain is let go first.
  twattila::device_loss::release_flip_chains_before_recreate = true;
  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);  // peak nits
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  if (const float mode = NormalizeTonemapMode(shader_injection.tonemap_type); mode != shader_injection.tonemap_type) {
    renodx::utils::settings::UpdateSetting("tonemap_type", mode);
  }
  // Scripted runs (RENODX_TW_SET / SHOT, renodx_test_hooks.txt next to the exe): nothing without them.
  twattila::test_hooks::Install(&settings, true, "[Divinity]");
  // The rays when an effect is on at launch (restart to turn the first one on) and the helper is there to trace them;
  // their Debug View over the tone mapper's output.
  ray_tracing_installed = twattila::dlaa_bridge::HelperInstalled()
                          && (rt_sliders.sun_shadows != 0.f || rt_sliders.ambient_occlusion != 0.f || rt_sliders.light_bounce != 0.f);
  vfx_brightness_installed = shader_injection.vfx_brightness != 1.f || marker_brightness != 1.f;
  if (ray_tracing_installed) {
    custom_shaders[0x30A1D40D].on_drawn = [](reshade::api::command_list* cmd_list) { twattila::rt_scene::OnTonemapDrawn(cmd_list); };
  }
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // The fast draw dispatch with the resource upgrades' gate (the swap chain mod's view cloning registers them).
  twattila::draw_dispatch::Use(DLL_PROCESS_ATTACH);
  // Frame capture: after RenoDX, which drops these copies.
  reshade::register_event<reshade::addon_event::copy_resource>(frame_capture::OnCopyResource);
  reshade::register_event<reshade::addon_event::copy_texture_region>(frame_capture::OnCopyTextureRegion);
  reshade::register_event<reshade::addon_event::destroy_device>(frame_capture::OnDestroyDevice);
  // Graphics: the helper's upscalers (the main camera's jitter, the tone mapper's input), the rays, frame
  // generation (the helper presents; HUD-less images through the swap chain shader, its constants at b10) and Reflex for
  // the game's presents.
  divinity::upscaling::Install();
  if (ray_tracing_installed) divinity::ray_tracing::Install();
  if (vfx_brightness_installed) {
    divinity::vfx_brightness::Install();
    reshade::register_event<reshade::addon_event::destroy_device>(divinity::vfx_brightness::OnDestroyDevice);
  }
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10, 10);
  twattila::reflex::Install();
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyHelperDevice);
  twattila::device_loss::InstallLast();
  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;
  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyHelperDevice);
  twattila::reflex::Uninstall();
  twattila::frame_generation::Uninstall();
  if (ray_tracing_installed) divinity::ray_tracing::Uninstall();
  if (vfx_brightness_installed) {
    reshade::unregister_event<reshade::addon_event::destroy_device>(divinity::vfx_brightness::OnDestroyDevice);
    divinity::vfx_brightness::Uninstall();
  }
  divinity::upscaling::Uninstall();
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);  // peak nits
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  reshade::unregister_event<reshade::addon_event::copy_resource>(frame_capture::OnCopyResource);
  reshade::unregister_event<reshade::addon_event::copy_texture_region>(frame_capture::OnCopyTextureRegion);
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_capture::OnDestroyDevice);
  twattila::draw_dispatch::Use(DLL_PROCESS_DETACH);
  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);
  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);
  twattila::test_hooks::Uninstall();
  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);
  twattila::frame_trace::Uninstall();
  twattila::pass_timing::Uninstall();
  attached = false;
}

}  // namespace

// DllMain //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Divinity: Original Sin Enhanced Edition by ATN";

// ReShade's AddonInit / AddonUninit, outside the loader lock: the helper's bridge waits on its threads when it ends.
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE) {
  if (!addon_registered) {
    if (!reshade::register_addon(addon_module)) return false;
    addon_registered = true;
  }
  return AttachAddon();
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE) {
  DetachAddon();
  // Last of the helper's users: the bridge ends with it.
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
      // ReShade calls AddonUninit before an explicit unload. During process termination, avoid calling into ReShade or
      // RenoDX under the loader lock.
      if (lpv_reserved == nullptr && addon_registered) {
        reshade::unregister_addon(h_module);
        addon_registered = false;
      }
      break;
  }

  return TRUE;
}
