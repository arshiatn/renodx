/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

// R.U.S.E. (64-bit, DX9). Needs the in-game HDR setting on: that path renders a 10-bit
// scene with bloom (postprocess00, 0x232EB5DF). The game has no tonemapper, its 8-bit back
// buffer clips at 1. Keep the native scene targets and tonemap into the FP16 output buffer.
// DX9 has no HDR swap chain: RenoDX presents through a DX11 one (device proxy).

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <string>

#include <d3d9.h>
#include <dxgi1_6.h>
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/settings.hpp"
#include "../../utils/swapchain.hpp"
#include "../../utils/vtable.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
static_assert(sizeof(ShaderInjectData) == 5 * 4 * sizeof(float), "Keep the DX9 c200-c204 injection layout in sync.");
// Capture controls once for all replaced draws in a host frame. Presentation
// uses a fixed nit reference, so UI changes cannot rescale an older scene frame.
ShaderInjectData frame_injection;
bool frame_latched = false;
float current_settings_mode = 0.f;

// ReShade's DX9 Present callback runs before its DX9 overlay and the native
// Present. The nested HDR proxy has already displayed the frame at that point.
// Suppress the subsequent native presentation so its stale 8-bit buffer cannot
// overwrite the HDR window. Keep this workaround local to R.U.S.E.
using D3D9PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using D3D9PresentExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using D3D9SwapchainPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using DXGIPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
D3D9PresentFn original_present = nullptr;
D3D9PresentExFn original_present_ex = nullptr;
D3D9SwapchainPresentFn original_swapchain_present = nullptr;
DXGIPresentFn original_proxy_present = nullptr;
renodx::utils::vtable::Slot<D3D9PresentFn> present_slot;
renodx::utils::vtable::Slot<D3D9PresentExFn> present_ex_slot;
renodx::utils::vtable::Slot<D3D9SwapchainPresentFn> swapchain_present_slot;
renodx::utils::vtable::Slot<DXGIPresentFn> proxy_present_slot;
IDirect3DDevice9* hooked_device = nullptr;
IDirect3DDevice9Ex* hooked_device_ex = nullptr;
IDirect3DSwapChain9* hooked_swapchain = nullptr;
IDXGISwapChain* hooked_proxy_swapchain = nullptr;
bool present_hook_attempted = false;
thread_local IDirect3DDevice9* presenting_host = nullptr;
thread_local bool proxy_present_started = false;
thread_local bool proxy_present_finished = false;
thread_local bool skip_host_present = false;
thread_local UINT host_sync_interval = 1u;

bool ConsumeHostPresentSkip() {
  if (presenting_host != hooked_device || !skip_host_present) return false;
  skip_host_present = false;
  // Preserve the game's device-loss/reset handling when the native device fails.
  return SUCCEEDED(presenting_host->TestCooperativeLevel());
}

HRESULT STDMETHODCALLTYPE PresentHost(
    IDirect3DDevice9* native, const RECT* source, const RECT* dest, HWND window, const RGNDATA* dirty) {
  if (native == hooked_device && ConsumeHostPresentSkip()) return D3D_OK;
  return original_present(native, source, dest, window, dirty);
}

HRESULT STDMETHODCALLTYPE PresentHostEx(
    IDirect3DDevice9Ex* native, const RECT* source, const RECT* dest, HWND window, const RGNDATA* dirty, DWORD flags) {
  if (native == hooked_device_ex && ConsumeHostPresentSkip()) return D3D_OK;
  return original_present_ex(native, source, dest, window, dirty, flags);
}

HRESULT STDMETHODCALLTYPE PresentHostSwapchain(
    IDirect3DSwapChain9* native, const RECT* source, const RECT* dest, HWND window, const RGNDATA* dirty, DWORD flags) {
  if (native == hooked_swapchain && ConsumeHostPresentSkip()) return D3D_OK;
  return original_swapchain_present(native, source, dest, window, dirty, flags);
}

HRESULT STDMETHODCALLTYPE PresentProxy(IDXGISwapChain* native, UINT interval, UINT flags) {
  if (native != hooked_proxy_swapchain || presenting_host == nullptr || !proxy_present_started
      || (flags & DXGI_PRESENT_TEST) != 0u) {
    return original_proxy_present(native, interval, flags);
  }
  // The suppressed native Present supplied VSync. Move that pacing to DXGI;
  // ALLOW_TEARING is only valid with a zero synchronization interval.
  if (host_sync_interval != 0u) {
    flags &= ~DXGI_PRESENT_ALLOW_TEARING;
  }
  const HRESULT result = original_proxy_present(native, host_sync_interval, flags);
  proxy_present_finished = (result == S_OK);
  return result;
}

void UninstallPresentHooks() {
  presenting_host = nullptr;
  skip_host_present = false;
  proxy_present_started = false;
  proxy_present_finished = false;
  auto uninstall = []<typename Function>(renodx::utils::vtable::Slot<Function>* slot) {
    if (slot->installed_address == nullptr) return;
    try {
      renodx::utils::vtable::Uninstall(slot);
      *slot = {};
    } catch (const std::exception& error) {
      reshade::log::message(reshade::log::level::warning, error.what());
    }
  };
  uninstall(&proxy_present_slot);
  uninstall(&swapchain_present_slot);
  uninstall(&present_ex_slot);
  uninstall(&present_slot);
  if (present_slot.installed_address == nullptr
      && present_ex_slot.installed_address == nullptr
      && swapchain_present_slot.installed_address == nullptr
      && proxy_present_slot.installed_address == nullptr) {
    hooked_device = nullptr;
    hooked_device_ex = nullptr;
    hooked_swapchain = nullptr;
    hooked_proxy_swapchain = nullptr;
    present_hook_attempted = false;
  }
}

void OnDestroyGameSwapchain(reshade::api::swapchain* swapchain, bool) {
  if (swapchain->get_device()->get_api() != reshade::api::device_api::d3d9
      && !renodx::utils::device_proxy::IsProxyDevice(swapchain->get_device())) return;
  UninstallPresentHooks();
  frame_latched = false;
}

void OnFinishPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain) {
  if (swapchain->get_device()->get_api() == reshade::api::device_api::d3d9) {
    presenting_host = nullptr;
    skip_host_present = false;
    proxy_present_started = false;
    proxy_present_finished = false;
  }
}

void OnAfterProxyPresent(
    reshade::api::command_queue*, reshade::api::swapchain* swapchain,
    const reshade::api::rect*, const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  if (swapchain->get_device()->get_api() != reshade::api::device_api::d3d9) return;
  // Registered after the framework's proxy callback, so its recovery state has
  // been updated. Missing handoff, TEST-only, resize and failed proxy calls keep
  // the original native Present available.
  skip_host_present = presenting_host == hooked_device && hooked_device != nullptr
                      && proxy_present_finished
                      && renodx::utils::device_proxy::UseProxyRequested()
                      && renodx::utils::device_proxy::last_device_proxy_shared_resource.handle != 0u
                      && !renodx::utils::device_proxy::device_proxy_creation_failed
                      && !renodx::utils::device_proxy::proxy_device_needs_resize.load()
                      && renodx::utils::device_proxy::proxy_invalid_call_streak.load() == 0u;
}

bool IsAdvancedSettings() {
  return current_settings_mode >= 1.f;
}

bool IsExtendedMode() {
  return shader_injection.hdr == 1.f && current_settings_mode == 1;
}

bool IsPsychoMode() {
  return (shader_injection.hdr == 2.f || shader_injection.hdr == 3.f) && IsAdvancedSettings();
}

bool HasHDRScene() {
  return shader_injection.hdr >= 1.f;
}

// Extra sliders: all HDR modes.
bool IsHDRMode() {
  return HasHDRScene() && IsAdvancedSettings();
}

bool OnFrameShaderDraw(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d9) return true;
  if (!frame_latched) {
    frame_injection = shader_injection;
    frame_injection.ui_scale = std::pow(
        std::max(frame_injection.graphics_white_nits, 1.f) / RUSE_REFERENCE_WHITE, 1.f / 2.2f);
    frame_latched = true;
  }
  // The same UI shader can also draw world labels or offscreen UI textures.
  // Scale only its final backbuffer draw, avoiding double scaling upstream.
  frame_injection.ui_draw = (renodx::utils::swapchain::HasBackBufferRenderTarget(cmd_list) ? 1.f : 0.f);
  return true;
}

void OnPresentFrame(
    reshade::api::command_queue*, reshade::api::swapchain* swapchain,
    const reshade::api::rect*, const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  auto* device = swapchain->get_device();
  // The nested DX11 proxy Present must not start a new host frame.
  if (device->get_api() != reshade::api::device_api::d3d9) {
    if (presenting_host != nullptr && renodx::utils::device_proxy::IsProxyDevice(device)) {
      proxy_present_started = true;
      if (proxy_present_slot.installed_address == nullptr) {
        proxy_present_slot = {
            .object = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native()),
            .index = 8u,
            .original = &original_proxy_present,
            .replacement = &PresentProxy,
        };
        try {
          renodx::utils::vtable::Install(&proxy_present_slot);
          hooked_proxy_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
        } catch (const std::exception& error) {
          reshade::log::message(reshade::log::level::warning, error.what());
        }
      }
    }
    return;
  }
  presenting_host = reinterpret_cast<IDirect3DDevice9*>(device->get_native());
  skip_host_present = false;
  proxy_present_started = false;
  proxy_present_finished = false;
  host_sync_interval = 1u;
  if (presenting_host != nullptr && !present_hook_attempted) {
    present_hook_attempted = true;
    try {
      present_slot = {
          .object = presenting_host,
          .index = 17u,
          .original = &original_present,
          .replacement = &PresentHost,
      };
      renodx::utils::vtable::Install(&present_slot);
      hooked_device = presenting_host;
      if (SUCCEEDED(hooked_device->QueryInterface(IID_PPV_ARGS(&hooked_device_ex)))) {
        present_ex_slot = {
            .object = hooked_device_ex,
            .index = 121u,
            .original = &original_present_ex,
            .replacement = &PresentHostEx,
        };
        try {
          renodx::utils::vtable::Install(&present_ex_slot);
        } catch (...) {
          hooked_device_ex->Release();
          throw;
        }
        hooked_device_ex->Release();
      }
      if (SUCCEEDED(hooked_device->GetSwapChain(0, &hooked_swapchain))) {
        swapchain_present_slot = {
            .object = hooked_swapchain,
            .index = 3u,
            .original = &original_swapchain_present,
            .replacement = &PresentHostSwapchain,
        };
        try {
          renodx::utils::vtable::Install(&swapchain_present_slot);
        } catch (...) {
          hooked_swapchain->Release();
          throw;
        }
        hooked_swapchain->Release();
      }
      reshade::log::message(reshade::log::level::info, "R.U.S.E.: native DX9 Present guard installed.");
    } catch (const std::exception& error) {
      reshade::log::message(reshade::log::level::warning, error.what());
      UninstallPresentHooks();
      present_hook_attempted = true;
    }
  }
  if (hooked_swapchain != nullptr) {
    D3DPRESENT_PARAMETERS parameters = {};
    if (SUCCEEDED(hooked_swapchain->GetPresentParameters(&parameters))) {
      switch (parameters.PresentationInterval) {
        case D3DPRESENT_INTERVAL_DEFAULT:
        case D3DPRESENT_INTERVAL_ONE:
          host_sync_interval = 1u;
          break;
        case D3DPRESENT_INTERVAL_TWO:
          host_sync_interval = 2u;
          break;
        case D3DPRESENT_INTERVAL_THREE:
          host_sync_interval = 3u;
          break;
        case D3DPRESENT_INTERVAL_FOUR:
          host_sync_interval = 4u;
          break;
        default:
          host_sync_interval = 0u;
          break;
      }
    }
  }
  if (!frame_latched) {
    // Screens without any known replacement still update output controls.
    frame_injection = shader_injection;
  }
  frame_latched = false;
}

void ApplyResetPreset() {
  // Peak / Paper White / UI brightness, Settings Mode, and output settings are preserved.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 1.f},

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},

      // HDR Extended / user grading
      {"exposure_tpm", 1.f},
      {"highlights_tpm", 1.f},
      {"shadows_tpm", 1.f},
      {"contrast_tpm", 1.f},
      {"saturation_tpm", 1.f},

      // Extra
      {"bloom", 1.f},
      {"vignette", 1.f},
  });
}

void ApplyPsychoRecommended() {
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
  });
}

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      __ALL_CUSTOM_SHADERS,
  };
  for (auto& [hash, shader] : custom_shaders) {
    shader.on_draw = OnFrameShaderDraw;
  }

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
      new renodx::utils::settings::Setting{
          .key = "peak_white_nits",
          .binding = &shader_injection.peak_white_nits,
          .default_value = 1000.f,
          .can_reset = false,
          .label = "Peak",
          .section = "General",
          .tooltip = "Target highlight peak in nits. The final HDR composite is limited to this value.",
          .min = 400.f,
          .max = 10000.f,
          .is_visible = HasHDRScene,
      },
      new renodx::utils::settings::Setting{
          .key = "diffuse_white_nits",
          .binding = &shader_injection.diffuse_white_nits,
          .default_value = 203.f,
          .label = "Paper White",
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
          .key = "tonemapper",
          .binding = &shader_injection.hdr,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Rendering Mode",
          .section = "General",
          .tooltip = "Needs the in-game HDR setting on.\n"
                     "SDR: the game's original picture (no tonemapper, highlights clip).\n"
                     "HDR (Extended): the original picture, clipped highlights restored up to Peak.\n"
                     "HDR (PsychoV30): maps the graded image after native gamma/brightness.\n"
                     "HDR (PsychoV30 Direct): replaces native luminance gamma/brightness; retains grading and decoded input colour ratios. Experimental.",
          .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV30)", "HDR (PsychoV30 Direct)"},
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
          .label = "Reset Image Settings",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "Resets both grading groups, Bloom and Vignette; selects Extended.\n"
                     "Peak, Paper White, UI brightness, Settings Mode, and output settings are preserved.",
          .on_change = []() {
            ApplyResetPreset();
          },
      },

      // Psycho V30 //////////////////////////////////////////////////////////////////////////////
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
          .tooltip = "Psycho contrast response. Both Psycho modes use this slider value directly.",
          .min = 0.00f,
          .max = 2.00f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },

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

      // Extra //////////////////////////////////////////////////////////////////////////////////
      new renodx::utils::settings::Setting{
          .key = "bloom",
          .binding = &shader_injection.bloom,
          .default_value = 1.f,
          .label = "Bloom",
          .section = "Extra",
          .tooltip = "Bloom multiplier. Needs the in-game HDR setting on.",
          .min = 0.f,
          .max = 2.f,
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
  };
}

bool configured = false;
bool attached = false;
bool addon_registered = false;

}  // namespace

// Lifecycle /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  // The DX9 swap chain can't report the display's peak (only DXGI can): 1000 then.
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

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - R.U.S.E. by ATN";

namespace {

void ConfigureAddon() {
  if (configured) return;

  // Construct the non-trivial containers here rather than during DLL loading.
  BuildRuntimeData();

  // Shaders: DX9 has no constant buffers, the injection goes into pixel shader
  // constants c200-c204 (shared.h). The game's shaders use the low registers.
  renodx::mods::shader::force_pipeline_cloning = true;
  renodx::mods::shader::expected_constant_buffer_space = 50;
  renodx::mods::shader::expected_constant_buffer_index = 13;
  renodx::mods::shader::constant_buffer_offset = 200 * 4;

  // Swapchain cb (the DX11 proxy)
  renodx::mods::swapchain::expected_constant_buffer_index = 13;
  renodx::mods::swapchain::expected_constant_buffer_space = 50;

  // DX9 presents through a DX11 HDR swap chain. The game renders into an FP16 copy of its
  // 8-bit back buffer, drawn to that swap chain with swap_chain_proxy_*.hlsl.
  renodx::mods::swapchain::use_device_proxy = true;
  renodx::mods::swapchain::use_resource_cloning = true;
  renodx::mods::swapchain::force_borderless = false;
  renodx::mods::swapchain::prevent_full_screen = true;
  renodx::mods::swapchain::swap_chain_proxy_vertex_shader = __swap_chain_proxy_vertex_shader_dx11;
  renodx::mods::swapchain::swap_chain_proxy_pixel_shader = __swap_chain_proxy_pixel_shader_dx11;

  // Do not upgrade the native packed 10-bit scene targets: that broad rule
  // produces a black scene in R.U.S.E. The proxy still supplies FP16 output.
  // DX9 shared-resource handoff has no keyed mutex. Finish each GPU side's
  // transfer before the other side reads/reuses it during loading transitions.
  renodx::mods::swapchain::device_proxy_wait_idle_source = true;
  renodx::mods::swapchain::device_proxy_wait_idle_destination = true;

  // Appended Settings (HDR10)
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "HDR10: PQ / BT.2020 output.\nscRGB: floating-point linear output.\nRestart after changing this setting.",
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
      .label = "Turn on the in-game HDR setting.",
      .section = "About",
  });
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
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroyGameSwapchain);
  reshade::register_event<reshade::addon_event::present>(OnPresentFrame);
  reshade::register_event<reshade::addon_event::finish_present>(OnFinishPresent);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  frame_injection = shader_injection;

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &frame_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &frame_injection);

  reshade::register_event<reshade::addon_event::present>(OnAfterProxyPresent);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroyGameSwapchain);
  reshade::unregister_event<reshade::addon_event::present>(OnPresentFrame);
  reshade::unregister_event<reshade::addon_event::finish_present>(OnFinishPresent);
  reshade::unregister_event<reshade::addon_event::present>(OnAfterProxyPresent);
  UninstallPresentHooks();

  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &frame_injection);

  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &frame_injection);

  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);

  attached = false;
  frame_latched = false;
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
