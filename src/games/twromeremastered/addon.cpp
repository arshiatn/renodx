/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <d3d11.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

// Extra sliders: both HDR modes.
bool IsHDRMode() {
  return shader_injection.hdr >= 1.f && current_settings_mode == 1;
}

// bool IsModdedAutoExposureEnabled() {
//   return shader_injection.auto_exposure_highlight_protection == 1.f && IsPsychoMode();
// }

void ApplyResetPreset() {
  // Reset only controls that are currently active in this addon.
  // Peak / Paper White / UI brightness, Settings Mode, and output settings
  // are intentionally preserved.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 1.f},

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 1.f},
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
      // {"chromaticaberration", 1.f},
      // {"sharpening", 1.f},
      // {"some_indicators", 1.f},

      // Active experimental controls
      // {"auto_exposure_highlight_protection", 0.f},
      // {"auto_exposure_highlight_headroom_stops", 2.f},
      // {"vfx_fire_brightness", 1.f},
      // {"vfx_base_brightness", 1.f},
      // {"sky_deband", 2.f},
  });
}

void ApplyPsychoRecommended() {
  // Recommended PsychoV30 tuning for a ~1000-nit display.
  // Only PsychoV30 controls are changed. Peak / Paper White / UI brightness
  // and unrelated controls are preserved.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 2.f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      {"bloom", 0.05f},
      {"lensflare", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
  });
}

// Frame capture //////////////////////////////////////////////////////////////////////////
// The loading screen picture is the back buffer copied into an 8-bit texture (read back on
// the CPU). The back buffer is RenoDX's 16-bit copy and RenoDX only copies identical formats,
// so the copy was dropped and the picture was black. We draw the frame into an 8-bit texture
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
      UpgradeRTVReplaceShader(0xB72063D6), //t00
      UpgradeRTVReplaceShader(0x3FCAE7F4), //smaa02
      UpgradeRTVReplaceShader(0xEEFF2F33), //fxaa00
      // UpgradeRTVReplaceShader(0xA03BE4CE),
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
        .tooltip = "SDR: the game's original tonemapper (Uncharted 2 / Hable).\n"
                   "HDR (Extended): Used (Uncharted 2/Hable Extended) instead\n"
                   "HDR (PsychoV30): replaces (Uncharted 2 / Hable) with PsychoV30 as tonemapper.",
        .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV30)"},
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (PsychoV30)",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Recommended Setting is PsychoV30\n"
                   "Peak, Paper White, UI brightness, and unrelated controls are preserved.",
        .on_change = []() {
          ApplyPsychoRecommended();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-line-2",
        .tooltip = "Resets active image controls to their defaults.\n"
                   "Peak, Paper White, UI brightness, Settings Mode, and output settings are preserved.",
        .on_change = []() {
          ApplyResetPreset();
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
        .default_value = 1.00f,
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
    // ONLY FOR DEBUG. ADD AGAIN IF MID GREY IS WRONG
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


    // User Grading (HDR Extended) //////////////////////////////////////////////////////////////
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

    new renodx::utils::settings::Setting{
        .key = "bloom",
        .binding = &shader_injection.bloom,
        .default_value = 1.0f,
        .label = "Bloom",
        .section = "Extra",
        .tooltip = "Bloom multiplier. Needs in-game setting on.\n"
                   "Personally, I wouldn't use it. It really doesn't look good.",
        .min = 0.00f,
        .max = 1.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsHDRMode,
    },
      new renodx::utils::settings::Setting{
        .key = "lensflare",
        .binding = &shader_injection.lensflare,
        .default_value = 1.00f,
        .label = "Lensflare",
        .section = "Extra",
        .tooltip = "Lensflare multiplier. Needs in-game setting on.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsHDRMode,
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
        .is_enabled = IsHDRMode,
        .is_visible = IsHDRMode, 
    },
    new renodx::utils::settings::Setting{
        .key = "distortion",
        .binding = &shader_injection.uvdistort,
        .default_value = 1.f,
        .label = "Distortion",
        .section = "Extra",
        .tooltip = "Multiplier on distortion. Needs in-game setting on.",
        // .tooltip = "Multiplier on distortion. YOU NEED TO CHOOSE PRESET ULTRA\n"
        //            "THEN YOUR OWN CUSTOME SETTINGS for the hidden distortion\n"
        //            "to get turned on. \n"
        //            "It was probably hidden because it is not getting used anymore!",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsHDRMode,
    },
    // new renodx::utils::settings::Setting{
    //     .key = "chromaticaberration",
    //     .binding = &shader_injection.chromaticaberration,
    //     .default_value = 1.f,
    //     .label = "Chromatic aberration",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on Chromatic aberration. Needs in-game setting on.",
    //     .min = 0.f,
    //     .max = 2.f,
    //     .format = "%.2f",
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "sharpening",
    //     .binding = &shader_injection.sharpening,
    //     .default_value = 1.f,
    //     .label = "Sharpening",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on Sharpening. Needs in-game setting on.",
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
    //     .default_value = 1.0f,
    //     .label = "Brightness of some Unit/Building Indicator ",
    //     .section = "Extra",
    //     .tooltip = "Brightness of selected highlight indicators.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsHDRMode,
    // },


    // EXTRA EXPERIMENTAL//////////
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
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "auto_exposure_highlight_headroom_stops",
    //     .binding = &shader_injection.auto_exposure_highlight_headroom_stops,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 2.0f,
    //     .label = "Modded Auto Exposure: Sensitivity",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Maximum metered highlight above the frame average, in stops. Lower values protect exposure more strongly. Higher values are closer to stock.",
    //     .min = 0.6f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    //     .is_enabled = IsModdedAutoExposureEnabled,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fire_brightness",
    //     .binding = &shader_injection.vfxfirebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Brightness boost: torches/burning houses/etc.",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "torches/burning houses/etc. Recommmendation is max 10\n"
    //                "Experimental! Find your sweets spot! ",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.1f",
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_base_brightness",
    //     .binding = &shader_injection.vfxbasebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness boost: All of them!",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Brightness of every single VFX effect. Recommendation: 2.5 for 1000nits, 6 for 2500nits.\n"
    //                "Probably not usable in desert storms. Experimental! Find your sweets spot! ",
    //     .min = 1.0f,
    //     .max = 20.0f,
    //     .format = "%.1f",
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "sky_deband",
    //     .binding = &shader_injection.sky_deband,
    //     .value_type = renodx::utils::settings::SettingValueType::INTEGER,
    //     .default_value = 2.f,
    //     .label = "Dithering for sky's 7-bit texture",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Dithering to reduce sky's bandings.\n"
    //                "Higher levels destroy more sky details while fixing banding.",
    //     .labels = {"Off", "Low", "Medium", "High"},
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // VFX //////////////////////////////////////////////////////////////////////////////////////
   
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
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: ROME REMASTERED by ATN";

namespace {

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
  // Game renders into a persistent FP16 clone of the back buffer (HDR10 always does).
  // scRGB's default converts the back buffer in place at Present, so a frame presented
  // again without a redraw gets converted twice (black loading screen).
  renodx::mods::swapchain::swapchain_proxy_compatibility_mode = false;
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

    #if RENODX_MODS_SWAPCHAIN_VERSION == 2
      renodx::mods::swapchain::resource_upgrade_infos.push_back({
    #else
      renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    #endif
          .old_format = reshade::api::format::r8g8b8a8_typeless,
          .new_format = reshade::api::format::r16g16b16a16_float,
          .ignore_size = true,
          // .use_resource_view_cloning = true,  // TODO: needed?
          // .use_resource_view_hot_swap = true,
          .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
          .usage_include = reshade::api::resource_usage::render_target,
      });

    // Resource Upgrades
    #if RENODX_MODS_SWAPCHAIN_VERSION == 2
      renodx::mods::swapchain::resource_upgrade_infos.push_back({
    #else
      renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    #endif
          .old_format = reshade::api::format::r11g11b10_float,
          .new_format = reshade::api::format::r16g16b16a16_float,
          .ignore_size = true,
          .use_resource_view_cloning = true,  // TODO: needed?
          .use_resource_view_hot_swap = true,
          .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
          .usage_include = reshade::api::resource_usage::render_target,
      });
          // Resource Upgrades
    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::b8g8r8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = true,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::render_target,
    //   });

    //             // Resource Upgrades
    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = true,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::render_target,
    //   });

    //       #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm_srgb,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = true,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::render_target,
    //   });

    //       // Resource Upgrades
    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .shader_hash = 0x6F09FC39,
    //       // .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::copy_source
    //   });


      

    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::copy_dest,
    //   });

    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::shader_resource,
    //   });
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

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);

  // After RenoDX, which drops these copies.
  reshade::register_event<reshade::addon_event::copy_resource>(frame_capture::OnCopyResource);
  reshade::register_event<reshade::addon_event::copy_texture_region>(frame_capture::OnCopyTextureRegion);
  reshade::register_event<reshade::addon_event::destroy_device>(frame_capture::OnDestroyDevice);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

  reshade::unregister_event<reshade::addon_event::copy_resource>(frame_capture::OnCopyResource);
  reshade::unregister_event<reshade::addon_event::copy_texture_region>(frame_capture::OnCopyTextureRegion);
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_capture::OnDestroyDevice);

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
