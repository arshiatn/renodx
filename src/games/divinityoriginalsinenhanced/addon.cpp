/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <d3d11.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/draw_dispatch.hpp"
#include "../twattila/frame_trace.hpp"
#include "../twattila/output_encoding.hpp"
#include "../twattila/pass_timing.hpp"
#include "../twattila/psycho35_settings.hpp"
// Scripted runs' RENODX_TW_PROFILE records the Total War ray-traced scene, which this add-on lacks.
#define RENODX_TEST_HOOKS_PROFILE ([] { renodx::utils::log::w("[Divinity] Test: RENODX_TW_PROFILE: no profile in this add-on."); })
#include "../twattila/test_hooks.hpp"
#include "./shared.h"

const std::string build_date = __DATE__;
const std::string build_time = __TIME__;

namespace {

ShaderInjectData shader_injection = {.tonemap_type = TONEMAP_EXTENDED, .purity_scale = 1.f};
float current_settings_mode = 0.f;

bool IsAdvancedSettings() {
  return current_settings_mode >= 1.f;
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
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
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

// Each replaced pass's first draw is logged once: a run counts by these "applied" lines.
#define UpgradeRTVReplaceShader(value)                                                                \
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
        .labels = {"Simple", "Advanced"},
        .is_global = true,
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
        .tooltip = "HDR (UC2 Extended / PsychoV30) with less bloom, the brazier and candle boosts.",
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
        .tooltip = "Full-RGB PsychoV30 purity, used by UC2 Extended / PsychoV30.",
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
        .tooltip = "Fire Highlights EXTRA ON TOP of my highlights.",
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
        .label = "Candle/staff/small particles Highlights",
        .section = "Pragmap Effects",
        .tooltip = "Candle/staff/small particles Highlights EXTRA ON TOP of my highlights.",
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
        .tooltip = "Off uses stock emission.\nOn (old version) applies my old brightness boost.\n"
                   "On (new color channel boost) is what I would use.",
        .labels = {"Off", "On (old version)", "On (new color channel boost)"},
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "candlehighlightmultiplierpsycho",
        .binding = &shader_injection.candlehighlightmultiplierpsycho,
        .default_value = 1.0f,
        .label = "Candle/Rain/small particles Highlights",
        .section = "Fire / Particles",
        .tooltip = "Candle/staff/small particles Highlights multiplier. 1.0 is stock",
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
        .tooltip = "Bloom multiplier (HDR only). 1.0 is stock.",
        .min = 0.f,
        .max = 2.f,
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
        .tooltip = "Multiplier on vignette mask.",
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
        .tooltip = "Godrays multiplier.",
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
        .tooltip = "Toggle UI elements, enough for screenshot in pause menu.",
        .labels = {"Off", "On"},
        .is_visible = IsAdvancedSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "playerLightIntensity",
        .binding = &shader_injection.player_light_intensity,
        .default_value = 1.f,
        .label = "Player/Other stuff's Light Intensity",
        .section = "Game-Specific Settings",
        .tooltip = "Adjusts the brightness of player, some other stuff's light intensity\n.",
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
        .label = "Local/Enemy Light Intensity",
        .section = "Game-Specific Settings",
        .tooltip = "Adjusts the brightness of torches, player and enemies.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.1f",
        .is_enabled = IsHDR,
        .is_visible = ShowHDRSettings,
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

// Time Each Pass's frame, and the frame trace's (renodx_trace_now next to the exe; FrameTrace=1 in ReShade.ini's
// [renodx]: 600 frames of the 3D view in).
void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twattila::pass_timing::OnPresent(queue, "divinity");
  twattila::frame_trace::OnPresent(false, tonemap_drawn.exchange(false, std::memory_order_relaxed));
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
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
        .recommended_preset = &ApplyRecommendedPreset,
        .log_name = "[Divinity]",
    });
    // A first start is one without a saved Rendering Mode: this add-on's key is tonemap_type (Configure looks for the
    // other add-ons' tonemapper).
    float saved_mode = 0.f;
    twattila::output_encoding::first_start =
        !reshade::get_config_value(nullptr, (renodx::utils::settings::global_name + "-preset1").c_str(), "tonemap_type", saved_mode);
    settings.push_back(setting);
  }

  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX by ShortFuse. PsychoV30, PsychoV35 and Hue Clip Emulation: Musa's. UC2 Extended by Musa and "
                   "DabVeed. Divinity: Original Sin Enhanced Edition by ATN.",
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

  twattila::psycho35::InsertBefore(&settings, "PsychoV30",
                                   twattila::psycho35::Rows(&shader_injection, ShowPsychoV35Settings, HUE_CLIP_DEFAULT));

  configured = true;
}

}  // namespace

// DllMain //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Divinity: Original Sin Enhanced Edition by ATN";

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(h_module)) return FALSE;
      // Time Each Pass: before every other draw hook (a skipped draw still gets its stamp).
      twattila::pass_timing::Install();
      ConfigureAddon();
      // Constants once each for the tone mapper, the exposure, the shadow filter, the sun, fog, SSAO and bloom's combine
      // (b0-b13: the frame b13, the view b12; the camera fields are Attila's b0 registers, not this game's); blends of fire,
      // candles and the UI combine.
      twattila::frame_trace::Install({
          .value_hashes = {0x30A1D40D, 0x4E112ABA, 0x8F114E47, 0xCCCB7253, 0xEDA7F8A1, 0x385DCF3C, 0x88B0D842},
          .value_slots = 14,
          .value_floats = 64,
          .on_demand = true,
          .at_100 = true,
          .blend_hashes = {0xED93F883, 0x93AEBB93, 0x20163A55},
      });
      // Before the swap chain mod: a removed device's chain let go, each new chain's output decided.
      twattila::device_loss::Install();
      reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);  // peak nits
      reshade::register_event<reshade::addon_event::present>(OnPresent);
      renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
      if (const float mode = NormalizeTonemapMode(shader_injection.tonemap_type); mode != shader_injection.tonemap_type) {
        renodx::utils::settings::UpdateSetting("tonemap_type", mode);
      }
      // Scripted runs (RENODX_TW_SET / SHOT, renodx_test_hooks.txt next to the exe): nothing without them.
      twattila::test_hooks::Install(&settings, true, "[Divinity]");
      renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
      renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
      // The fast draw dispatch with the resource upgrades' gate (the swap chain mod's view cloning registers them).
      twattila::draw_dispatch::Use(DLL_PROCESS_ATTACH);
      // Frame capture: after RenoDX, which drops these copies.
      reshade::register_event<reshade::addon_event::copy_resource>(frame_capture::OnCopyResource);
      reshade::register_event<reshade::addon_event::copy_texture_region>(frame_capture::OnCopyTextureRegion);
      reshade::register_event<reshade::addon_event::destroy_device>(frame_capture::OnDestroyDevice);
      twattila::device_loss::InstallLast();
      break;
    case DLL_PROCESS_DETACH:
      twattila::device_loss::Uninstall();
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
      reshade::unregister_addon(h_module);
      break;
  }

  return TRUE;
}
