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
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <d3d9.h>
#include <dxgi1_6.h>
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../../utils/trace.hpp"
#include "../../utils/swapchain.hpp"
#include "../../utils/vtable.hpp"
#include "../twattila/output_encoding.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "./bridge.hpp"
#include "./bridge_replay.hpp"
#include "./ray_tracing.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
static_assert(sizeof(ShaderInjectData) == 11 * 4 * sizeof(float), "Keep the DX9 c200-c210 injection layout in sync.");
// Capture controls once for all replaced draws in a host frame. Presentation
// uses a fixed nit reference, so UI changes cannot rescale an older scene frame.
ShaderInjectData frame_injection;
bool frame_latched = false;
float current_settings_mode = 0.f;

// Frame Trace (a restart row: utils/trace.hpp's events stay registered while it is on): one host frame's binds,
// constants, targets and draws into ReShade.log, from the button or a renodx_trace_now file next to the exe.
bool frame_trace_enabled = false;
bool frame_trace_requested = false;
std::filesystem::path frame_trace_marker;

// The whole-package round's first pieces (plan.md 10b W1, D1), restart rows for the test run: with Frame Trace on, every
// resource the game makes goes to ReShade.log (pools, usages, formats, samples: the B0 trace saw none); Readable Scene
// Depth makes the game's back-buffer-sized single-sample depth surfaces INTZ textures (ReShade's generic depth add-on does
// the same on D3D9), the upscalers' and the ground shadows' depth.
bool readable_depth_enabled = false;
uint32_t back_buffer_width = 0;
uint32_t back_buffer_height = 0;

void OnLogResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*,
                   reshade::api::resource_usage, reshade::api::resource resource) {
  if (device->get_api() != reshade::api::device_api::d3d9) return;
  reshade::log::message(
      reshade::log::level::info,
      std::format("[R.U.S.E.] resource 0x{:x}: type {} {} heap {} usage 0x{:x} flags 0x{:x}", resource.handle,
                  static_cast<uint32_t>(desc.type),
                  (desc.type == reshade::api::resource_type::buffer)
                      ? std::format("{} bytes stride {}", desc.buffer.size, desc.buffer.stride)
                      : std::format("{}x{} levels {} format {} samples {}", desc.texture.width, desc.texture.height,
                                    desc.texture.levels, static_cast<uint32_t>(desc.texture.format), desc.texture.samples),
                  static_cast<uint32_t>(desc.heap), static_cast<uint32_t>(desc.usage), static_cast<uint32_t>(desc.flags))
          .c_str());
}

// The bridge's events (bridge.hpp), registered with the Anti-Aliasing row: the INTZ depths, the bound target / depth / scene
// texture, the game's constants and every draw (the jitter).
void OnInitDepthResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*,
                         reshade::api::resource_usage, reshade::api::resource resource) {
  if (desc.texture.format == reshade::api::format::intz) {
    ruse::bridge::state.intz_depths.insert(resource.handle);
    ruse::bridge::state.host_device = device;
  }
}

void OnBridgeBindTargets(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view* targets,
                         reshade::api::resource_view depth_stencil) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d9) return;
  auto& bridge = ruse::bridge::state;
  bridge.depth_stencil = depth_stencil.handle;
  bridge.render_target = (count > 0) ? targets[0].handle : 0u;
  D3DSURFACE_DESC desc = {};
  bridge.render_target_is_frame_sized =
      bridge.render_target != 0u && SUCCEEDED(reinterpret_cast<IDirect3DSurface9*>(bridge.render_target)->GetDesc(&desc))
      && desc.Width == back_buffer_width && desc.Height == back_buffer_height;
}

void OnBridgePushDescriptors(reshade::api::command_list*, reshade::api::shader_stage stages, reshade::api::pipeline_layout,
                             uint32_t layout_param, const reshade::api::descriptor_table_update& update) {
  // ReShade's D3D9 layout: parameter 1 the pixel samplers with their textures.
  if (layout_param != 1 || update.binding != 0 || update.count == 0
      || update.type != reshade::api::descriptor_type::sampler_with_resource_view) {
    return;
  }
  ruse::bridge::state.pixel_texture_0 = static_cast<const reshade::api::sampler_with_resource_view*>(update.descriptors)[0].view.handle;
}

void OnBridgePushConstants(reshade::api::command_list*, reshade::api::shader_stage, reshade::api::pipeline_layout,
                           uint32_t layout_param, uint32_t first, uint32_t count, const void* values) {
  ruse::bridge::OnPushConstants(layout_param, first, count, values);
  if (ruse::replay::state.enabled) {
    ruse::replay::OnPushConstants(layout_param, first, count, values);
  }
}

bool OnBridgeDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t, uint32_t first_index, int32_t vertex_offset,
                         uint32_t) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d9) return false;
  if (ruse::ray_tracing::state.ground && ruse::bridge::IsSceneDraw()) {
    ruse::ray_tracing::OnSceneDraw(cmd_list);
  }
  ruse::bridge::OnDraw(cmd_list);
  if (ruse::replay::state.enabled && ruse::bridge::IsSceneDraw()) {
    ruse::replay::Record(cmd_list, index_count, first_index, vertex_offset);
  }
  return false;
}

bool OnCreateDepthResource(reshade::api::device* device, reshade::api::resource_desc& desc, reshade::api::subresource_data*,
                           reshade::api::resource_usage) {
  if (device->get_api() != reshade::api::device_api::d3d9) return false;
  // Surfaces only: a D24 texture may be sampled as a hardware shadow map (PCF), which INTZ would break.
  if (desc.type != reshade::api::resource_type::surface || (desc.usage & reshade::api::resource_usage::depth_stencil) == 0) return false;
  if (desc.texture.samples > 1 || desc.texture.width != back_buffer_width || desc.texture.height != back_buffer_height) return false;
  if (desc.texture.format != reshade::api::format::d24_unorm_s8_uint && desc.texture.format != reshade::api::format::d24_unorm_x8_uint) {
    return false;
  }
  desc.texture.format = reshade::api::format::intz;
  desc.usage |= reshade::api::resource_usage::shader_resource;
  reshade::log::message(reshade::log::level::info,
                        std::format("[R.U.S.E.] depth {}x{} made readable (INTZ)", desc.texture.width, desc.texture.height).c_str());
  return true;
}

// Graphics 2 of 2 (ATN Q7): values the game reads from its Option.ini ([advanced_video]: the ClusterLoadSaverOptions
// list) above what its menu sets. The game reads the file at start and writes it only when its options menu applies
// (SaveCommandName) or the file fails to load (ForceSaveIfNotOk): a row writes the file when it changes and at each start,
// so it applies after a restart. The game's own value waits in ReShade.ini [RUSE Option.ini] for the row's return to
// Game's; a value the menu wrote since replaces it.
constexpr char GRAPHICS_2_SECTION[] = "Game Settings Above the Menu (Experimental, Restart Required)";
constexpr char OPTION_INI_CONFIG[] = "RUSE Option.ini";
struct GameOption {
  std::vector<std::string> option_keys;  // [advanced_video]
  std::vector<float> values;             // one per label after Game's
  bool factors = false;                  // the values scale the game's own
};
std::vector<std::pair<renodx::utils::settings::Setting*, GameOption>> game_options;

void ApplyGameOption(const GameOption& option, int choice) {
  // LocalSaveDir: Steam's userdata\<user>\21970\local beside the steamclient64.dll the game loaded (the newest of its users).
  static const std::filesystem::path OPTION_INI = [] {
    std::filesystem::path newest;
    std::wstring module;
    if (HMODULE steam = GetModuleHandleW(L"steamclient64.dll"); steam != nullptr) {
      module.resize(MAX_PATH);
      module.resize(GetModuleFileNameW(steam, module.data(), MAX_PATH));
    }
    std::error_code error;
    std::filesystem::file_time_type newest_time;
    const auto users = std::filesystem::path(module).parent_path() / L"userdata";
    for (const auto& user : std::filesystem::directory_iterator(users, error)) {
      auto candidate = user.path() / L"21970" / L"local" / L"Option.ini";
      const auto time = std::filesystem::last_write_time(candidate, error);
      if (error) {
        error.clear();
        continue;
      }
      if (newest.empty() || time > newest_time) {
        newest = std::move(candidate);
        newest_time = time;
      }
    }
    if (newest.empty()) {
      reshade::log::message(reshade::log::level::warning, "[R.U.S.E.] Option.ini not found (Steam's userdata): Graphics 2 of 2 does nothing");
    }
    return newest;
  }();
  if (OPTION_INI.empty()) return;
  std::string text;
  {
    std::ifstream file(OPTION_INI, std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }
  const size_t section = text.find("[advanced_video]");
  if (section == std::string::npos) return;
  const size_t section_end = std::min(text.find("\n[", section), text.size());
  bool changed = false;
  for (const std::string& name : option.option_keys) {
    const std::string needle = "\t" + name + " = ";
    const size_t at = text.find(needle, section);
    if (at == std::string::npos || at > section_end) continue;  // only values the game wrote
    const size_t value_at = at + needle.size();
    const size_t value_end = std::min(text.find_first_of("\r\n", value_at), text.size());
    const std::string current_text = text.substr(value_at, value_end - value_at);
    float game = 0.f;
    std::from_chars(current_text.data(), current_text.data() + current_text.size(), game);
    const std::string game_key = name + ".Game";
    const std::string written_key = name + ".Written";
    // Still the value this add-on wrote: the game's own is the one kept; else the menu's new value is the game's.
    char written_text[32] = "";
    size_t written_size = sizeof(written_text) - 1;
    if (reshade::get_config_value(nullptr, OPTION_INI_CONFIG, written_key.c_str(), written_text, &written_size)
        && current_text == written_text) {
      reshade::get_config_value(nullptr, OPTION_INI_CONFIG, game_key.c_str(), game);
    }
    float wanted = game;
    if (choice > 0 && choice <= static_cast<int>(option.values.size())) {
      wanted = (option.factors ? game * option.values[choice - 1] : option.values[choice - 1]);
    }
    const std::string wanted_text = std::format("{:g}", wanted);
    if (wanted == game) {
      reshade::set_config_value(nullptr, OPTION_INI_CONFIG, game_key.c_str(), static_cast<const char*>(nullptr));
      reshade::set_config_value(nullptr, OPTION_INI_CONFIG, written_key.c_str(), static_cast<const char*>(nullptr));
    } else {
      reshade::set_config_value(nullptr, OPTION_INI_CONFIG, game_key.c_str(), game);
      reshade::set_config_value(nullptr, OPTION_INI_CONFIG, written_key.c_str(), wanted_text.c_str());
    }
    if (wanted_text == current_text) continue;
    text.replace(value_at, value_end - value_at, wanted_text);
    changed = true;
    reshade::log::message(reshade::log::level::info,
                          std::format("[R.U.S.E.] Option.ini {} {} -> {}", name, current_text, wanted_text).c_str());
  }
  if (!changed) return;
  auto temporary = OPTION_INI;
  temporary += L".renodx";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) return;
  }
  MoveFileExW(temporary.c_str(), OPTION_INI.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ReShade's DX9 Present callback runs before its DX9 overlay and the native
// Present. The nested HDR proxy has already displayed the frame at that point.
// Suppress the subsequent native presentation so its stale 8-bit buffer cannot
// overwrite the HDR window. Keep this workaround local to R.U.S.E.
using D3D9PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using D3D9PresentExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using D3D9SwapchainPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using DXGIPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using D3D9DrawIndexedFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
D3D9PresentFn original_present = nullptr;
D3D9PresentExFn original_present_ex = nullptr;
D3D9SwapchainPresentFn original_swapchain_present = nullptr;
DXGIPresentFn original_proxy_present = nullptr;
D3D9DrawIndexedFn original_draw_indexed = nullptr;
renodx::utils::vtable::Slot<D3D9PresentFn> present_slot;
renodx::utils::vtable::Slot<D3D9PresentExFn> present_ex_slot;
renodx::utils::vtable::Slot<D3D9SwapchainPresentFn> swapchain_present_slot;
renodx::utils::vtable::Slot<DXGIPresentFn> proxy_present_slot;
renodx::utils::vtable::Slot<D3D9DrawIndexedFn> gui_draw_slot;
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
thread_local bool native_gui_phase = false;

bool ConsumeHostPresentSkip() {
  if (presenting_host != hooked_device || !skip_host_present) return false;
  skip_host_present = false;
  // Preserve the game's device-loss/reset handling when the native device fails.
  return SUCCEEDED(presenting_host->TestCooperativeLevel());
}

bool ShouldIsolateNativeGui(reshade::api::effect_runtime* runtime) {
  if (runtime->get_device()->get_api() != reshade::api::device_api::d3d9) return false;
  if (!skip_host_present || !proxy_present_finished) return false;
  if (gui_draw_slot.installed_address == nullptr || hooked_device == nullptr) return false;
  if (presenting_host != hooked_device
      || reinterpret_cast<IDirect3DDevice9*>(runtime->get_device()->get_native()) != hooked_device) return false;
  return SUCCEEDED(hooked_device->TestCooperativeLevel());
}

bool OnOpenReShadeOverlay(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source) {
  return open && ShouldIsolateNativeGui(runtime);
}

void OnReShadeOverlay(reshade::api::effect_runtime* runtime) {
  if (runtime->get_device()->get_api() != reshade::api::device_api::d3d9) return;
  // ReShade emits this after building all windows, including the startup banner,
  // and before submitting ImGui's indexed draws. Closing alone does not remove
  // the banner or widgets already built for this frame; the native draw filter does.
  native_gui_phase = presenting_host == hooked_device && hooked_device != nullptr
                     && reinterpret_cast<IDirect3DDevice9*>(runtime->get_device()->get_native()) == hooked_device;
  if (ShouldIsolateNativeGui(runtime)) {
    runtime->open_overlay(false, reshade::api::input_source::none);
  }
}

void OnReShadePresent(reshade::api::effect_runtime* runtime) {
  if (runtime->get_device()->get_api() == reshade::api::device_api::d3d9) {
    native_gui_phase = false;
  }
}

HRESULT STDMETHODCALLTYPE DrawNativeIndexed(
    IDirect3DDevice9* native, D3DPRIMITIVETYPE type, INT vertex_offset,
    UINT first_vertex, UINT vertex_count, UINT first_index, UINT primitive_count) {
  if (native == hooked_device && presenting_host == hooked_device && native_gui_phase) {
    if (skip_host_present && proxy_present_finished
        && SUCCEEDED(native->TestCooperativeLevel())) {
      return D3D_OK;
    }
  }
  return original_draw_indexed(native, type, vertex_offset, first_vertex, vertex_count, first_index, primitive_count);
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
  native_gui_phase = false;
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
  uninstall(&gui_draw_slot);
  uninstall(&swapchain_present_slot);
  uninstall(&present_ex_slot);
  uninstall(&present_slot);
  if (present_slot.installed_address == nullptr
      && present_ex_slot.installed_address == nullptr
      && swapchain_present_slot.installed_address == nullptr
      && proxy_present_slot.installed_address == nullptr
      && gui_draw_slot.installed_address == nullptr) {
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
    native_gui_phase = false;
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
  return current_settings_mode == 1.f;
}

bool HasHDRScene() {
  return shader_injection.hdr >= 1.f && shader_injection.hdr <= 4.f;
}

// Every HDR mode's rows (Extra).
bool IsPsychoMode() {
  return HasHDRScene() && IsAdvancedSettings();
}

bool IsPsychoV31Mode() {
  return shader_injection.hdr >= 1.f && shader_injection.hdr <= 2.f && IsAdvancedSettings();
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 3.f && IsAdvancedSettings();
}

bool IsHdrOutput() {
  return shader_injection.swap_chain_encoding != static_cast<float>(twattila::output_encoding::SDR);
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, the clip alone).
constexpr float HUE_CLIP_DEFAULT = 100.f;

// The controls a host frame draws with. SDR output: Peak, Paper White and UI all the display's white (the back buffer's
// 203-nit reference), so the game's picture reaches the display as it is.
void LatchFrameInjection() {
  frame_injection = shader_injection;
  if (!IsHdrOutput()) {
    frame_injection.peak_white_nits = RUSE_REFERENCE_WHITE;
    frame_injection.diffuse_white_nits = RUSE_REFERENCE_WHITE;
    frame_injection.graphics_white_nits = RUSE_REFERENCE_WHITE;
  }
  frame_injection.ui_scale = std::pow(std::max(frame_injection.graphics_white_nits, 1.f) / RUSE_REFERENCE_WHITE, 1.f / 2.2f);
  frame_injection.video_scale = std::pow(std::max(frame_injection.diffuse_white_nits, 1.f) / RUSE_REFERENCE_WHITE, 1.f / 2.2f);
}

bool OnFrameShaderDraw(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d9) return true;
  if (!frame_latched) {
    LatchFrameInjection();
    frame_latched = true;
  }
  // UI and video shaders can also draw into offscreen textures. Scale only
  // their final backbuffer draw, avoiding double scaling upstream.
  frame_injection.ui_draw = (renodx::utils::swapchain::HasBackBufferRenderTarget(cmd_list) ? 1.f : 0.f);
  return true;
}

void OnPresentFrame(
    reshade::api::command_queue* queue, reshade::api::swapchain* swapchain,
    const reshade::api::rect*, const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  auto* device = swapchain->get_device();
  // The nested DX11 proxy Present must not start a new host frame.
  if (device->get_api() != reshade::api::device_api::d3d9) {
    if (presenting_host != nullptr && renodx::utils::device_proxy::IsProxyDevice(device)) {
      ruse::ray_tracing::state.proxy_queue = queue;
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
  ruse::bridge::OnPresent();
  if (ruse::ray_tracing::state.ground) {
    ruse::ray_tracing::OnPresent();
  }
  if (frame_trace_enabled) {
    std::error_code ignored;
    if (frame_trace_requested || std::filesystem::remove(frame_trace_marker, ignored)) {
      frame_trace_requested = false;
      renodx::utils::trace::trace_scheduled_device = device;
      reshade::log::message(reshade::log::level::info, "R.U.S.E.: frame trace scheduled.");
      ruse::replay::state.log_next = true;
    }
  }
  ruse::replay::Replay(ruse::bridge::state.camera_valid ? std::span<const float>(ruse::bridge::state.camera).subspan(16, 16)
                                                       : std::span<const float>());
  native_gui_phase = false;
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
      gui_draw_slot = {
          .object = hooked_device,
          .index = 82u,
          .original = &original_draw_indexed,
          .replacement = &DrawNativeIndexed,
      };
      try {
        renodx::utils::vtable::Install(&gui_draw_slot);
        reshade::log::message(reshade::log::level::info, "R.U.S.E.: native DX9 GUI draw filter installed.");
      } catch (const std::exception& error) {
        // Keep the established Present guard if the optional GUI test hook fails.
        reshade::log::message(reshade::log::level::warning, error.what());
      }
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
    LatchFrameInjection();
  }
  frame_latched = false;
}

// Reset (Extended) and SDR: every look control at its default.
void ApplyDefaultLook(float rendering_mode) {
  // Display/output settings and the independent Motion Blur/DOF controls are preserved.
  renodx::utils::settings::UpdateSettings({
      {"rendering_mode", rendering_mode},

      // PsychoV31
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},

      // Extra
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"motion_blur",1.f},
      {"depth_of_field",1.f},
      {"video_autohdr", 0.f},
      {"video_dithering", 1.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

// ATN 2026-11-12: Musa's PsychoV35 at its defaults; PsychoV31's sliders as before, for its modes.
void ApplyPsychoRecommended() {
  // Display/output settings and the independent Motion Blur/DOF controls are preserved.
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
  renodx::utils::settings::UpdateSettings({
      {"rendering_mode", 3.f},

      // PsychoV31
      {"exposure", 1.f},
      {"highlights", 1.65f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},

      // Extra
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"motion_blur",0.f},
      {"depth_of_field",1.f},
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
  // The bridge (bridge.hpp): the final post's HDRParam (c11.x) for the next frame's write-back.
  for (const uint32_t post : {0x232EB5DFu, 0x2D5EFDE2u, 0x44550114u, 0x7E93D12Fu, 0xC3EBA3CAu, 0xFB692F37u, 0x0106B253u}) {
    custom_shaders[post].on_draw = [](reshade::api::command_list* cmd_list) {
      ruse::bridge::state.hdr_param = ruse::bridge::state.pixel_registers[44];
      return OnFrameShaderDraw(cmd_list);
    };
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
          .labels = {"Simple", "Advanced", "Graphics 2 of 2"},
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
          .is_visible = []() { return HasHDRScene() && IsHdrOutput(); },
      },
      new renodx::utils::settings::Setting{
          .key = "diffuse_white_nits",
          .binding = &shader_injection.diffuse_white_nits,
          .default_value = 203.f,
          .label = "Paper White",
          .section = "General",
          .tooltip = "Brightness of the scene and cutscene video in nits.",
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
          .tooltip = "Brightness of UI and HUD in nits.",
          .min = 1.f,
          .max = 500.f,
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          // ATN 2026-11-15: PsychoV35 (the Recommended mode) is the default for new players; Reset stays Extended.
          .key = "rendering_mode",
          .binding = &shader_injection.hdr,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 3.f,
          .label = "Rendering Mode",
          .section = "General",
          .tooltip = "Needs the in-game HDR setting on.\n"
                     "SDR: the game's original picture (no tonemapper, highlights clip).\n"
                     "PsychoV31 - Native Tone: applies PsychoV31 after the game's gamma/brightness response.\n"
                     "PsychoV31 - Full Replacement: PsychoV31 replaces that response.\n"
                     "PsychoV35: Musa's PsychoV35 after the map's own gamma/brightness, with Hue Clip Emulation (Recommended).\n"
                     "Extended: the game's picture up to Paper White, its clipped highlights rolled off to Peak.\n"
                     "All HDR modes retain the game's colour grading, bloom and vignette.",
          .labels = {"SDR", "HDR (PsychoV31 - Native Tone)", "HDR (PsychoV31 - Full Replacement)", "HDR (PsychoV35)", "HDR (Extended)"},
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset (Faithful to SDR)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Resets Psycho controls, Bloom and Vignette; selects HDR (Extended).\n"
                     "Peak, Paper White, UI, output, Motion Blur and Depth of Field are preserved.",
          .on_change = []() {
            ApplyDefaultLook(4.f);
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "SDR",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "For displays without HDR: selects SDR with every look control at its default.",
          .on_change = []() {
            ApplyDefaultLook(0.f);
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (PsychoV35)",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "Selects HDR (PsychoV35) with its controls at their defaults.\n"
                     "Peak, Paper White, UI, output, Motion Blur and Depth of Field are preserved.",
          .on_change = []() {
            ApplyPsychoRecommended();
          },
      },

      // Psycho V31 //////////////////////////////////////////////////////////////////////////////
      new renodx::utils::settings::Setting{
          .key = "exposure",
          .binding = &shader_injection.exposure,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 1.00f,
          .label = "Exposure",
          .section = "Psycho V31",
          .min = 0.00f,
          .max = 2.00f,
          .format = "%.2f",
          .is_enabled = IsPsychoV31Mode,
          .is_visible = IsPsychoV31Mode,
      },
      new renodx::utils::settings::Setting{
          .key = "highlights",
          .binding = &shader_injection.highlights,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 1.00f,
          .label = "Highlights",
          .section = "Psycho V31",
          .min = 0.00f,
          .max = 2.00f,
          .format = "%.2f",
          .is_enabled = IsPsychoV31Mode,
          .is_visible = IsPsychoV31Mode,
      },
      new renodx::utils::settings::Setting{
          .key = "shadows",
          .binding = &shader_injection.shadows,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 1.00f,
          .label = "Shadows",
          .section = "Psycho V31",
          .min = 0.00f,
          .max = 2.00f,
          .format = "%.2f",
          .is_enabled = IsPsychoV31Mode,
          .is_visible = IsPsychoV31Mode,
      },
      new renodx::utils::settings::Setting{
          .key = "contrast",
          .binding = &shader_injection.contrast,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 1.00f,
          .label = "Contrast",
          .section = "Psycho V31",
          .min = 0.00f,
          .max = 2.00f,
          .format = "%.2f",
          .is_enabled = IsPsychoV31Mode,
          .is_visible = IsPsychoV31Mode,
      },
      new renodx::utils::settings::Setting{
          .key = "purity_scale",
          .binding = &shader_injection.saturation,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 1.00f,
          .label = "Saturation / Purity",
          .section = "Psycho V31",
          .min = 0.00f,
          .max = 2.00f,
          .format = "%.2f",
          .is_enabled = IsPsychoV31Mode,
          .is_visible = IsPsychoV31Mode,
      },
      new renodx::utils::settings::Setting{
          .key = "cone_response_exponent",
          .binding = &shader_injection.cone_response_exponent,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 1.00f,
          .label = "Cone Response Exponent",
          .section = "Psycho V31",
          .tooltip = "Multiplies Contrast to set PsychoV31's cone-response power.",
          .min = 0.00f,
          .max = 2.00f,
          .format = "%.2f",
          .is_enabled = IsPsychoV31Mode,
          .is_visible = IsPsychoV31Mode,
      },

      new renodx::utils::settings::Setting{
          .key = "hue_shift",
          .binding = &shader_injection.hue_shift,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 2.f,
          .label = "Highlight Hue Shift",
          .section = "Psycho V31",
          .tooltip = "Both PsychoV31 modes. 1 = bisector hue, 2 = cone-response hue (default).\n"
                     "Intermediate values blend these hue directions according to colour brightness.\n"
                     "Affects all hues; it does not select a fixed red/orange/yellow target.",
          .min = 1.f,
          .max = 2.f,
          .format = "%.2f",
          .is_enabled = IsPsychoV31Mode,
          .is_visible = IsPsychoV31Mode,
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
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
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
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
            new renodx::utils::settings::Setting{
          .key = "motion_blur",
          .binding = &shader_injection.motion_blur,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 1.f,
          .label = "Motion Blur",
          .section = "Extra",
          .tooltip = "0 = off, 1 = native strength. Intermediate values reduce the blur radius.\n"
                     "Works in SDR and both Psycho modes. Has no effect when the game uses its single-tap path.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsAdvancedSettings,
          .is_visible = IsAdvancedSettings,
      },
      new renodx::utils::settings::Setting{
          .key = "depth_of_field",
          .binding = &shader_injection.depth_of_field,
          .value_type = renodx::utils::settings::SettingValueType::FLOAT,
          .default_value = 1.f,
          .label = "Depth of Field",
          .section = "Extra",
          .tooltip = "0 = off, 1 = native strength. Intermediate values reduce blur and its mask strength.\n"
                     "Works in SDR and both Psycho modes where the game applies depth of field.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsAdvancedSettings,
          .is_visible = IsAdvancedSettings,
      },
      new renodx::utils::settings::Setting{
          .key = "video_autohdr",
          .binding = &shader_injection.video_autohdr,
          .default_value = 0.f,
          .label = "Video AutoHDR",
          .section = "Extra",
          .tooltip = "Makes the bright parts of the game's (SDR) videos and loading screens brighter: only what is above\n"
                     "mid gray, the brightest the most. 0 = off, 1 = video white reaches Peak.\n"
                     "High values can show banding (see Video Dithering).",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = []() { return HasHDRScene() && IsHdrOutput(); },
          .is_visible = IsPsychoMode,
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
          .is_enabled = []() { return HasHDRScene() && IsHdrOutput() && shader_injection.video_autohdr > 0.f; },
          .is_visible = IsPsychoMode,
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
  // constants c200-c209 (shared.h). The game's shaders use the low registers.
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
  // transfer before the other side reads/reuses it during loading transitions
  // (a restart row: off is an A/B for the U1 run, the Present guard fixed that corruption later).
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "ProxyGpuWaits",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Wait for Each Frame Copy (Experimental)",
        .section = "Output (Restart Required)",
        .tooltip = "On (default): each frame's copy to the HDR output waits for the GPU.\n"
                   "Off: may be faster; turn it back on if loading screens show garbage.\n"
                   "Restart after changing this setting.",
        .labels = {"Off", "On"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    renodx::mods::swapchain::device_proxy_wait_idle_source = (setting->GetValue() != 0.f);
    renodx::mods::swapchain::device_proxy_wait_idle_destination = (setting->GetValue() != 0.f);
    settings.push_back(setting);
  }

  // Output (twattila/output_encoding.hpp): decided for each new DX9 swap chain (OnCreateSwapchain).
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = static_cast<float>(twattila::output_encoding::AUTO),
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "HDR10: 10-bit. scRGB: 16-bit.\n"
                   "SDR: displays without HDR. Auto (default): scRGB with Windows HDR on, else SDR.\n"
                   "Restart after changing this setting.",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = static_cast<int>(setting->GetValue()),
        .first_start_without_hdr = []() { ApplyDefaultLook(0.f); },
        .recommended_preset = &ApplyPsychoRecommended,
        .log_name = "[R.U.S.E.]",
    });
    settings.push_back(setting);
  }
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "FrameTrace",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Frame Trace",
        .section = "Output (Restart Required)",
        .tooltip = "For testing: Trace One Frame writes one frame's draws, shaders and targets to ReShade.log.\n"
                   "Off (default) costs nothing. Restart after changing this setting.",
        .labels = {"Off", "On"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    frame_trace_enabled = (setting->GetValue() != 0.f);
    if (frame_trace_enabled) {
      std::wstring exe(32768, L'\0');
      exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
      frame_trace_marker = std::filesystem::path(exe).parent_path() / L"renodx_trace_now";
    }
    settings.push_back(setting);
    settings.push_back(new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Trace One Frame",
        .section = "Output (Restart Required)",
        .on_change = []() { frame_trace_requested = true; },
        .is_visible = []() { return frame_trace_enabled && IsAdvancedSettings(); },
    });
  }
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "ReadableDepth",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Readable Scene Depth (Testing)",
        .section = "Output (Restart Required)",
        .tooltip = "For testing: makes the game's depth readable for the upcoming anti-aliasing and shadows.\n"
                   "Needs the in-game anti-aliasing off. Off (default) changes nothing. Restart after changing this setting.",
        .labels = {"Off", "On"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    readable_depth_enabled = (setting->GetValue() != 0.f);
    settings.push_back(setting);
  }
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "RuseAntiAliasing",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Anti-Aliasing (Experimental)",
        .section = "Output (Restart Required)",
        .tooltip = "NVIDIA DLAA, AMD FSR or Intel XeSS at native resolution instead of the game's anti-aliasing.\n"
                   "Needs the in-game anti-aliasing off and the helper beside the add-on. Restart after changing this setting.",
        .labels = {"Game's", "DLAA", "FSR Native AA", "XeSS Native AA"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    settings.push_back(setting);
    const int choice = static_cast<int>(setting->GetValue());
    ruse::bridge::state.enabled = choice != 0 && twattila::dlaa_bridge::HelperInstalled();
    ruse::bridge::state.upscaler = (choice == 2) ? twattila::dlaa_bridge::UPSCALER_FSR
                                                 : ((choice == 3) ? twattila::dlaa_bridge::UPSCALER_XESS : twattila::dlaa_bridge::UPSCALER_DLSS);
    readable_depth_enabled = readable_depth_enabled || ruse::bridge::state.enabled;
    if (ruse::bridge::state.enabled) {
      // The bloom's first pass: the upscaler on the scene it reads.
      for (const uint32_t bloom : {0x41B68A24u, 0xF663E47Eu}) {
        custom_shaders[bloom] = {.crc32 = bloom, .on_draw = [](reshade::api::command_list* cmd_list) {
                                   ruse::bridge::Evaluate(cmd_list, __bridge_quad, __bridge_prepare, __bridge_writeback);
                                   ruse::ray_tracing::OnBloom(cmd_list);
                                   return true;
                                 }};
      }
    }
  }
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "RuseAntiAliasingGpuSync",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Anti-Aliasing GPU Sync (Experimental)",
        .section = "Output (Restart Required)",
        .tooltip = "The GPU waits for the anti-aliasing instead of the CPU (faster when it works).\n"
                   "Restart after changing this setting.",
        .labels = {"Off", "On"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    settings.push_back(setting);
    ruse::bridge::state.gpu_sync = ruse::bridge::state.enabled && setting->GetValue() != 0.f;
  }
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "RuseSceneReplay",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Scene Replay (Testing)",
        .section = "Output (Restart Required)",
        .tooltip = "For testing: draws the scene's geometry a second time for the upcoming ray-traced shadows (costs CPU time).\n"
                   "Needs the in-game anti-aliasing off. Off (default) changes nothing. Restart after changing this setting.",
        .labels = {"Off", "On"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    settings.push_back(setting);
    ruse::replay::state.enabled = setting->GetValue() != 0.f;
    ruse::replay::state.log_next = ruse::replay::state.enabled;
    readable_depth_enabled = readable_depth_enabled || ruse::replay::state.enabled;
  }
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "RuseRayTracedGroundShadows",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Ray-Traced Ground Shadows (Experimental)",
        .section = "Output (Restart Required)",
        .tooltip = "Ray-traced sun shadows on the ground, added to the game's (costs CPU and GPU time).\n"
                   "Needs Anti-Aliasing at DLAA, FSR or XeSS and the in-game anti-aliasing off. Restart after changing this setting.",
        .labels = {"Off", "On"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    settings.push_back(setting);
    // The rays trace the scene the replay captured, in the helper the anti-aliasing starts.
    ruse::ray_tracing::state.ground = ruse::bridge::state.enabled && setting->GetValue() != 0.f;
    ruse::bridge::state.ray_tracing = ruse::ray_tracing::state.ground;
    ruse::replay::state.rays = ruse::ray_tracing::state.ground;
    ruse::replay::state.enabled = ruse::replay::state.enabled || ruse::ray_tracing::state.ground;
    readable_depth_enabled = readable_depth_enabled || ruse::ray_tracing::state.ground;
  }
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "RuseRayTracedObjectShadows",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Ray-Traced Object Shadows (Experimental)",
        .section = "Output (Restart Required)",
        .tooltip = "Ray-traced sun shadows on buildings, units and trees, added to the game's (costs GPU time).\n"
                   "Moving units' shadows are a frame late. Needs Ray-Traced Ground Shadows. Restart after changing this setting.",
        .labels = {"Off", "On"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    settings.push_back(setting);
    // The object shaders with the rays' visibility in their shadow (each proved the game's with it at 1).
    ruse::ray_tracing::state.objects = ruse::ray_tracing::state.ground && setting->GetValue() != 0.f;
    if (ruse::ray_tracing::state.objects) {
      for (const auto& [hash, code] : ruse::ray_tracing::OBJECT_SHADOW_SHADERS) {
        custom_shaders[hash] = {.crc32 = hash, .code = code};
      }
    }
  }
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "RuseRayTracedGroundOcclusion",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Ray-Traced Ground Ambient Occlusion (Experimental)",
        .section = "Output (Restart Required)",
        .tooltip = "Ray-traced ambient occlusion on the ground: softly darker under buildings, units and trees (costs GPU time).\n"
                   "Needs Ray-Traced Ground Shadows. Restart after changing this setting.",
        .labels = {"Off", "On"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    settings.push_back(setting);
    // The terrain shaders with the rays' occlusion in their ambient (each proved the game's with it at 1).
    ruse::ray_tracing::state.ambient = ruse::ray_tracing::state.ground && setting->GetValue() != 0.f;
    if (ruse::ray_tracing::state.ambient) {
      for (const auto& [hash, code] : ruse::ray_tracing::GROUND_AMBIENT_SHADERS) {
        custom_shaders[hash] = {.crc32 = hash, .code = code};
      }
    }
  }
  if (ruse::bridge::state.enabled || ruse::replay::state.enabled) {
    // The camera pass: its matrices (c0-c8) as it drew (the bridge's motion, the replay's placements).
    custom_shaders[0x88292FC6] = {.crc32 = 0x88292FC6, .on_draw = [](reshade::api::command_list*) {
                                    std::memcpy(ruse::bridge::state.camera.data(), ruse::bridge::state.pixel_registers.data(),
                                                sizeof(ruse::bridge::state.camera));
                                    ruse::bridge::state.camera_valid = true;
                                    ruse::ray_tracing::OnCameraPass();
                                    return true;
                                  }};
  }

  twattila::psycho35::InsertBefore(&settings, "Extra", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));

  // Graphics 2 of 2: Option.ini values above the game's menu (ApplyGameOption).
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "These change the game's own settings file. They apply after a restart.",
      .section = GRAPHICS_2_SECTION,
  });
  for (auto [setting, option] : {
           std::pair{
               new renodx::utils::settings::Setting{
                   .key = "GraphicsShadowMapSize",
                   .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                   .default_value = 0.f,
                   .label = "Shadow Map Size",
                   .section = GRAPHICS_2_SECTION,
                   .tooltip = "Sharper sun shadows. More video memory.",
                   .labels = {"Game's", "4096", "8192"},
                   .is_global = true,
               },
               GameOption{.option_keys = {"ShadowRTSize"}, .values = {4096.f, 8192.f}},
           },
           std::pair{
               new renodx::utils::settings::Setting{
                   .key = "GraphicsSpriteResolution",
                   .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                   .default_value = 0.f,
                   .label = "Sprite Resolution",
                   .section = GRAPHICS_2_SECTION,
                   .tooltip = "Sharper distant objects drawn as sprites. More video memory.",
                   .labels = {"Game's", "4096", "8192"},
                   .is_global = true,
               },
               GameOption{.option_keys = {"ImposteurTextureSize"}, .values = {4096.f, 8192.f}},
           },
           std::pair{
               new renodx::utils::settings::Setting{
                   .key = "GraphicsSpriteAntiAliasing",
                   .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                   .default_value = 0.f,
                   .label = "Sprite Anti-Aliasing",
                   .section = GRAPHICS_2_SECTION,
                   .tooltip = "Smoother edges on distant objects drawn as sprites and their shadows,\n"
                              "at the game's highest scene anti-aliasing (8.5).",
                   .labels = {"Game's", "8.5"},
                   .is_global = true,
               },
               GameOption{.option_keys = {"ImposteurRTAA", "ImposteurShadowRTAA"}, .values = {8.5f}},
           },
           std::pair{
               new renodx::utils::settings::Setting{
                   .key = "GraphicsDetailDistance",
                   .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                   .default_value = 0.f,
                   .label = "Detail Distance",
                   .section = GRAPHICS_2_SECTION,
                   .tooltip = "Buildings and objects keep their detailed versions farther away.\n"
                              "Costs CPU and GPU time.",
                   .labels = {"Game's", "2x", "4x"},
                   .is_global = true,
               },
               GameOption{.option_keys = {"LevelBuild_High", "LevelBuild_Mid", "LevelBuild_Low", "LevelBuild_VeryLow"},
                          .values = {2.f, 4.f},
                          .factors = true},
           },
       }) {
    setting->on_change_value = [option](float, float current) { ApplyGameOption(option, static_cast<int>(current)); };
    settings.push_back(setting);
    game_options.emplace_back(setting, std::move(option));
  }

  // The game's own setting the mod needs, first on General.
  settings.insert(settings.begin() + 1, new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Turn on the in-game HDR setting.",
      .section = "General",
  });

  // Appended Settings (About)
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom PsychoV31 "
                   "and PsychoV35.",
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
          .label = "NexusMods",
          .section = "About",
          .group = "line_1",
          .tint = 0x2196F3,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/ruse/mods/6"); },
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

  // Settings Mode's pages: Simple and Advanced the HDR rows, Graphics 2 of 2 the game's settings; About on every page.
  for (auto* setting : settings) {
    if (setting->key == "SettingsMode" || setting->section == "About") continue;
    const bool game_settings = (setting->section == GRAPHICS_2_SECTION);
    setting->is_visible = [game_settings, shown = setting->is_visible] {
      return ((current_settings_mode == 2.f) == game_settings) && (shown == nullptr || shown());
    };
  }

  configured = true;
}

// The game's DX9 swap chain, before the swapchain mod's create_swapchain (registered after this) and its DX11 proxy.
// output_encoding hands an SDR target to the swapchain mod alone (shared step S2): the proxy takes it from here.
bool OnCreateSwapchain(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd) {
  if (api != reshade::api::device_api::d3d9) return false;
  back_buffer_width = desc.back_buffer.texture.width;
  back_buffer_height = desc.back_buffer.texture.height;
  ruse::bridge::state.width = back_buffer_width;
  ruse::bridge::state.height = back_buffer_height;
  shader_injection.rays_texel_x = 1.f / static_cast<float>(std::max(back_buffer_width, 1u));
  shader_injection.rays_texel_y = 1.f / static_cast<float>(std::max(back_buffer_height, 1u));
  shader_injection.rays_half_texel_x = 0.5f * shader_injection.rays_texel_x;
  shader_injection.rays_half_texel_y = 0.5f * shader_injection.rays_texel_y;
  twattila::output_encoding::Decide(static_cast<HWND>(hwnd));
  renodx::utils::device_proxy::SetTargetFormat(renodx::mods::swapchain::target_format);
  renodx::utils::device_proxy::SetTargetColorSpace(renodx::mods::swapchain::target_color_space);
  // Graphics 2 of 2 at each start, for the next one (the game read Option.ini before making its device).
  static bool game_options_written = false;
  if (!game_options_written) {
    game_options_written = true;
    for (const auto& [setting, option] : game_options) {
      ApplyGameOption(option, static_cast<int>(setting->GetValue()));
    }
  }
  return false;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroyGameSwapchain);
  reshade::register_event<reshade::addon_event::present>(OnPresentFrame);
  reshade::register_event<reshade::addon_event::finish_present>(OnFinishPresent);
  reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnOpenReShadeOverlay);
  reshade::register_event<reshade::addon_event::reshade_overlay>(OnReShadeOverlay);
  reshade::register_event<reshade::addon_event::reshade_present>(OnReShadePresent);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  frame_injection = shader_injection;

  if (frame_trace_enabled) {
    renodx::utils::trace::Use(DLL_PROCESS_ATTACH);
    reshade::register_event<reshade::addon_event::init_resource>(OnLogResource);
  }
  if (readable_depth_enabled) {
    reshade::register_event<reshade::addon_event::create_resource>(OnCreateDepthResource);
    reshade::register_event<reshade::addon_event::init_resource>(OnInitDepthResource);
  }
  if (ruse::bridge::state.enabled || ruse::replay::state.enabled) {
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBridgeBindTargets);
    reshade::register_event<reshade::addon_event::push_descriptors>(OnBridgePushDescriptors);
    reshade::register_event<reshade::addon_event::push_constants>(OnBridgePushConstants);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnBridgeDrawIndexed);
    reshade::register_event<reshade::addon_event::destroy_device>(ruse::bridge::OnDestroyDevice);
  }
  if (ruse::replay::state.enabled) {
    // Before the game's device: ReShade hooks the buffers' Lock / Unlock once an add-on asks for map / unmap.
    reshade::register_event<reshade::addon_event::map_buffer_region>(ruse::replay::OnMapBuffer);
    reshade::register_event<reshade::addon_event::unmap_buffer_region>(ruse::replay::OnUnmapBuffer);
    reshade::register_event<reshade::addon_event::destroy_resource>(ruse::replay::OnDestroyResource);
    reshade::register_event<reshade::addon_event::init_pipeline>(ruse::replay::OnInitPipeline);
    reshade::register_event<reshade::addon_event::destroy_pipeline>(ruse::replay::OnDestroyPipeline);
    reshade::register_event<reshade::addon_event::bind_pipeline>(ruse::replay::OnBindPipeline);
    reshade::register_event<reshade::addon_event::bind_vertex_buffers>(ruse::replay::OnBindVertexBuffers);
    reshade::register_event<reshade::addon_event::bind_index_buffer>(ruse::replay::OnBindIndexBuffer);
    reshade::register_event<reshade::addon_event::bind_pipeline_states>(ruse::replay::OnBindPipelineStates);
    reshade::register_event<reshade::addon_event::destroy_device>(ruse::replay::OnDestroyDevice);
  }
  if (ruse::ray_tracing::state.ground) {
    twattila::rt_scene::InstallReplayedScene(ruse::ray_tracing::HOST);
    reshade::register_event<reshade::addon_event::copy_texture_region>(ruse::ray_tracing::OnCopyTextureRegion);
    reshade::register_event<reshade::addon_event::destroy_device>(ruse::ray_tracing::OnDestroyDevice);
  }
  if (ruse::bridge::state.gpu_sync) {
    renodx::utils::gpu_fence::Use(DLL_PROCESS_ATTACH);
  }

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &frame_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &frame_injection);

  reshade::register_event<reshade::addon_event::present>(OnAfterProxyPresent);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroyGameSwapchain);
  reshade::unregister_event<reshade::addon_event::present>(OnPresentFrame);
  reshade::unregister_event<reshade::addon_event::finish_present>(OnFinishPresent);
  reshade::unregister_event<reshade::addon_event::present>(OnAfterProxyPresent);
  reshade::unregister_event<reshade::addon_event::reshade_open_overlay>(OnOpenReShadeOverlay);
  reshade::unregister_event<reshade::addon_event::reshade_overlay>(OnReShadeOverlay);
  reshade::unregister_event<reshade::addon_event::reshade_present>(OnReShadePresent);
  reshade::unregister_event<reshade::addon_event::init_resource>(OnLogResource);
  reshade::unregister_event<reshade::addon_event::create_resource>(OnCreateDepthResource);
  reshade::unregister_event<reshade::addon_event::init_resource>(OnInitDepthResource);
  reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBridgeBindTargets);
  reshade::unregister_event<reshade::addon_event::push_descriptors>(OnBridgePushDescriptors);
  reshade::unregister_event<reshade::addon_event::push_constants>(OnBridgePushConstants);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(OnBridgeDrawIndexed);
  reshade::unregister_event<reshade::addon_event::destroy_device>(ruse::bridge::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(ruse::replay::OnMapBuffer);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(ruse::replay::OnUnmapBuffer);
  reshade::unregister_event<reshade::addon_event::destroy_resource>(ruse::replay::OnDestroyResource);
  reshade::unregister_event<reshade::addon_event::init_pipeline>(ruse::replay::OnInitPipeline);
  reshade::unregister_event<reshade::addon_event::destroy_pipeline>(ruse::replay::OnDestroyPipeline);
  reshade::unregister_event<reshade::addon_event::bind_pipeline>(ruse::replay::OnBindPipeline);
  reshade::unregister_event<reshade::addon_event::bind_vertex_buffers>(ruse::replay::OnBindVertexBuffers);
  reshade::unregister_event<reshade::addon_event::bind_index_buffer>(ruse::replay::OnBindIndexBuffer);
  reshade::unregister_event<reshade::addon_event::bind_pipeline_states>(ruse::replay::OnBindPipelineStates);
  reshade::unregister_event<reshade::addon_event::destroy_device>(ruse::replay::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::copy_texture_region>(ruse::ray_tracing::OnCopyTextureRegion);
  reshade::unregister_event<reshade::addon_event::destroy_device>(ruse::ray_tracing::OnDestroyDevice);
  if (ruse::bridge::state.gpu_sync) {
    renodx::utils::gpu_fence::Use(DLL_PROCESS_DETACH);
  }
  UninstallPresentHooks();

  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &frame_injection);

  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &frame_injection);

  if (frame_trace_enabled) {
    renodx::utils::trace::Use(DLL_PROCESS_DETACH);
  }

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
