/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <numeric>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nvsdk_ngx_helpers.h>

#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "../twattila/frame_history.hpp"
#include "../twattila/native_hook_lifetime.hpp"
#include "./engine.hpp"

// The game's own DLSS: empire.retail.x64.dll links NVIDIA's NGX SDK (3.10.4). It runs DLSS as DLAA: the feature is
// created with the display size as input and output (PerfQualityValue 5, DLAA) and evaluated with the display size as
// render subrect, through the SDK's NGX_D3D11_EVALUATE_DLSS_EXT (NVSDK_NGX_D3D11_DLSS_Eval_Params). Both are hooked:
// read now, changed later (render sizes below the display).
// The camera jitter: the frame setup (+0x1a9100c) runs the jitter (+0x199c4d0: a 16-point table, 2 x offset / size in
// clip space, current at camera +0x394, previous at +0x39c) only while a TAA object is active (+0xdf8, +0xe08, or
// +0xe18 and a global flag), so with DLSS the camera never jitters and DLSS gets 0 (traces debug2: TAA jitters,
// DLSS not). JITTER_CONDITION's first test is patched into a jump to the jitter while the game's DLSS runs.
// Render scale (v4): the feature is recreated with the render size as input (the game's holder's handle cleared; its
// quality immediate set to match), evaluated with that render subrect, and the jitter is the addon's Halton (2, 3)
// in render pixels with 8 / scale^2 phases (at least 16); render_scale.hpp makes the 3D image that size.
// The game passes FSR's reactive mask (its G-buffer's SV_Target4: grass writes its vertex alpha there, the leaves
// nothing) to DLSS as pInIsParticleMask (pass +0x1ad16e9, render targets +0xe8; FSR's reactive at +0x1ad1a73).
namespace twshogun2::game_dlss {

namespace internal {

// bool CreateFeature(holder, device, const uint32_t input[2], const uint32_t output[2]): NGX Width / Height and
// OutWidth / OutHeight, PerfQualityValue 5, DLSS.Feature.Create.Flags. +0x1abc6f0 (2026-10-13 build).
constexpr std::string_view CREATE_FEATURE =
    "4C 89 44 24 18 55 53 56 41 55 48 8B EC 48 83 EC 78 80 79 10 00 49 8B F1 49 8B D8 4C 8B E9 75 0C";
// NGX_D3D11_EVALUATE_DLSS_EXT(context, handle, parameters, eval params). +0x1abd950.
constexpr std::string_view EVALUATE =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 40 49 8B D8 0F 29 74 24 30 4D 8B 01 48 8B F2 48 8B E9 "
    "0F 29 7C 24 20 48 8B CB 48 8D 15";
// mov rax, [rsi + 0xdf8]; movaps xmm6, [rsp + 0x3f0]; test rax, rax; je; cmp byte [rax + 0x60], 0; je; cmp byte
// [rax + 0x18], 0; jne (the jitter). +0x1a9100c.
constexpr std::string_view JITTER_CONDITION =
    "48 8B 86 F8 0D 00 00 0F 28 B4 24 F0 03 00 00 48 85 C0 74 0C 80 78 60 00 74 06 80 78 18 00 75 49";
// "test rax, rax; je" (+15) -> "jmp" to the jitter (+0x1a91075, 0x55 after the jump).
constexpr size_t JITTER_PATCH_OFFSET = 15;
constexpr std::array<uint8_t, 5> JITTER_PATCH = {0xE9, 0x55, 0x00, 0x00, 0x00};
// void Jitter(camera, frame, width, height): camera +0x394 / +0x398 this frame's offset, +0x39c / +0x3a0 the last
// frame's, 2 x table / size (clip space). +0x199c4d0.
constexpr std::string_view JITTER_FUNCTION =
    "48 8B C4 48 81 EC 88 00 00 00 F3 0F 10 1D ?? ?? ?? ?? 83 E2 0F 0F 28 05 ?? ?? ?? ?? 0F 28 D3 0F 28 0D ?? ?? ?? ?? "
    "0F 29 04 24";
constexpr size_t CAMERA_JITTER = 0x394;
// The game's DLSS pass: "cmp qword [rip + holder], 0" (no feature yet: create it). +0x1ad159d.
constexpr std::string_view DLSS_PASS = "48 83 3D ?? ?? ?? ?? 00 48 8B DA 48 8B 05 ?? ?? ?? ?? 48 8B F9 8B 70 10";
// CreateFeature +0x1aa: mov r8d, PerfQualityValue (5, DLAA).
constexpr size_t QUALITY_IMMEDIATE = 0x1aa;
constexpr std::array<uint8_t, 3> QUALITY_OPCODE = {0x41, 0xB8, 0x05};
// CreateFeature +0x143 / +0x15b: mov eax, 1 / mov ecx, 0x41, its DLSS.Feature.Create.Flags (IsHDR, | AutoExposure).
// The game never sets MVLowRes: below the display size DLSS then read the motion vectors as display-sized (from the
// wrong place, wrong length: flicker on grass below 100%, ATN debug5); added while the render scale is below 1.
constexpr std::array<size_t, 2> FLAGS_IMMEDIATES = {0x143, 0x15b};
constexpr std::array<std::array<uint8_t, 2>, 2> FLAGS_OPCODES = {{{0xB8, 0x01}, {0xB9, 0x41}}};
// CreateFeature +0xc5: call NVSDK_NGX_Parameter_SetUI(parameters, name, value) (+0x207fcc0), its first preset hint
// (DLSS.Hint.Render.Preset.DLAA, then the other modes'; all 0: DLSS's default per mode).
constexpr size_t SET_UI_CALL = 0xc5;

using CreateFeature = bool (*)(void* holder, void* device, const uint32_t* input, const uint32_t* output);
using Jitter = void (*)(uint8_t* camera, int32_t frame, int32_t width, int32_t height);
using Evaluate = NVSDK_NGX_Result (*)(ID3D11DeviceContext* context, NVSDK_NGX_Handle* handle,
                                      NVSDK_NGX_Parameter* parameters, NVSDK_NGX_D3D11_DLSS_Eval_Params* eval);
using SetUI = void (*)(NVSDK_NGX_Parameter* parameters, const char* name, unsigned int value);

struct State {
  std::shared_mutex mutex;
  bool searched = false;
  std::vector<renodx::utils::detour::Function> hooks;
  tw::native_hooks::Lifetime hook_lifetime;
  std::string status;
  uint32_t input[2] = {};  // the last created feature's
  uint32_t output[2] = {};
  uint32_t creations = 0;
  uint64_t evaluations = 0;
  NVSDK_NGX_D3D11_DLSS_Eval_Params last = {};  // resources only to describe them
  uint8_t* jitter_site = nullptr;
  std::array<uint8_t, JITTER_PATCH.size()> jitter_original = {};
  bool jitter_patched = false;
  uint64_t* holder = nullptr;        // the game's feature handle (0: the pass creates one)
  uint8_t* quality_value = nullptr;  // CreateFeature's PerfQualityValue immediate
  std::array<uint8_t*, FLAGS_IMMEDIATES.size()> flags_values = {};  // its feature flags immediates
};
static State state;
static CreateFeature original_create = nullptr;
static Evaluate original_evaluate = nullptr;
static Jitter original_jitter = nullptr;
static SetUI original_set_ui = nullptr;
// The render scale the game's feature was created with (1: DLAA, the game's) and the one asked for (overlay).
static std::atomic<float> active_scale = 1.f;
static std::atomic<float> requested_scale = 1.f;
// The same for the DLSS preset hint of every mode (0: DLSS's default per mode).
static std::atomic_uint32_t active_preset = 0;
static std::atomic_uint32_t requested_preset = 0;
// The game's DLSS ran in the last frame (present): the addon's jitter only then (TAA keeps the game's).
static std::atomic_bool dlss_running = false;
// Render thread, before each evaluation (the object motion merges its motion vectors, the frame trace marks it).
static std::function<void(ID3D11DeviceContext*, NVSDK_NGX_D3D11_DLSS_Eval_Params*)> on_evaluate;
// Debug: the jitter DLSS gets, x (1 - 2 x bit): bit 0 flips X, bit 1 flips Y.
static std::atomic_int jitter_flip = 0;

inline void WriteCode(uint8_t* site, const uint8_t* bytes, size_t size) {
  DWORD protection = 0;
  VirtualProtect(site, size, PAGE_EXECUTE_READWRITE, &protection);
  std::memcpy(site, bytes, size);
  VirtualProtect(site, size, protection, &protection);
  FlushInstructionCache(GetCurrentProcess(), site, size);
}

// NGX PerfQualityValue for a render scale (DLSS's own ratios).
inline uint8_t QualityValue(float scale) {
  // UltraQuality is refused (v4: 77% failed every frame): Quality from 65% up.
  if (scale >= 0.999f) return NVSDK_NGX_PerfQuality_Value_DLAA;
  if (scale >= 0.65f) return NVSDK_NGX_PerfQuality_Value_MaxQuality;
  if (scale >= 0.56f) return NVSDK_NGX_PerfQuality_Value_Balanced;
  if (scale >= 0.45f) return NVSDK_NGX_PerfQuality_Value_MaxPerf;
  return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
}

inline bool OnCreateFeature(void* holder, void* device, const uint32_t* input, const uint32_t* output) {
  const float scale = active_scale.load();
  const auto render = twattila::frame_history::AspectSize(input[0], input[1], scale);
  {
    const std::unique_lock lock(state.mutex);
    state.input[0] = render[0];
    state.input[1] = render[1];
    state.output[0] = output[0];
    state.output[1] = output[1];
    ++state.creations;
    if (state.quality_value != nullptr) {
      const uint8_t quality = QualityValue(scale);
      WriteCode(state.quality_value, &quality, 1);
    }
    const uint8_t mv_low_res = (scale < 0.999f ? NVSDK_NGX_DLSS_Feature_Flags_MVLowRes : 0);
    for (size_t index = 0; index < FLAGS_IMMEDIATES.size(); ++index) {
      if (state.flags_values[index] == nullptr) continue;
      const uint8_t flags = FLAGS_OPCODES[index][1] | mv_low_res;
      WriteCode(state.flags_values[index], &flags, 1);
    }
  }
  renodx::utils::log::i(std::format("[Shogun 2 DLSS] The game creates DLSS: {}x{} -> {}x{} (scale {:.3f}, preset {})",
                                    render[0], render[1], output[0], output[1], scale, active_preset.load()));
  return original_create(holder, device, render.data(), output);
}

// The game's preset hints replaced by the overlay's preset (0: the game's).
inline void OnSetUI(NVSDK_NGX_Parameter* parameters, const char* name, unsigned int value) {
  if (const uint32_t preset = active_preset.load(std::memory_order_relaxed);
      preset != 0 && name != nullptr && std::string_view(name).starts_with("DLSS.Hint.Render.Preset.")) {
    value = preset;
  }
  original_set_ui(parameters, name, value);
}

// Halton (2, 3) - 0.5 for index >= 1.
inline float Halton(uint32_t index, uint32_t base) {
  float fraction = 1.f;
  float result = 0.f;
  for (; index > 0; index /= base) {
    fraction /= static_cast<float>(base);
    result += fraction * static_cast<float>(index % base);
  }
  return result - 0.5f;
}

// The game's jitter at full size; below it the addon's: Halton in render pixels, 8 / scale^2 phases (at least 16).
inline void OnJitter(uint8_t* camera, int32_t frame, int32_t width, int32_t height) {
  const float scale = active_scale.load(std::memory_order_relaxed);
  if (scale >= 0.999f || !dlss_running.load(std::memory_order_relaxed) || width <= 0 || height <= 0) {
    original_jitter(camera, frame, width, height);
    return;
  }
  const auto phases = std::max(16u, static_cast<uint32_t>(std::ceil(8.f / (scale * scale))));
  const auto render = twattila::frame_history::AspectSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), scale);
  const auto render_width = static_cast<float>(render[0]);
  const auto render_height = static_cast<float>(render[1]);
  auto* jitter = reinterpret_cast<float*>(camera + CAMERA_JITTER);
  const uint32_t current = static_cast<uint32_t>(frame) % phases + 1;
  const uint32_t previous = static_cast<uint32_t>(frame - 1) % phases + 1;
  jitter[0] = 2.f * Halton(current, 2) / render_width;
  jitter[1] = 2.f * Halton(current, 3) / render_height;
  jitter[2] = 2.f * Halton(previous, 2) / render_width;
  jitter[3] = 2.f * Halton(previous, 3) / render_height;
}

inline NVSDK_NGX_Result OnEvaluate(ID3D11DeviceContext* context, NVSDK_NGX_Handle* handle,
                                   NVSDK_NGX_Parameter* parameters, NVSDK_NGX_D3D11_DLSS_Eval_Params* eval) {
  // Render scale: the subrect the 3D image was drawn into; the game turns the clip-space jitter into pixels with the
  // display size: x render / display for render pixels.
  if (const float scale = active_scale.load(std::memory_order_relaxed); scale < 0.999f) {
    const uint32_t display_width = eval->InRenderSubrectDimensions.Width;
    const uint32_t display_height = eval->InRenderSubrectDimensions.Height;
    const auto render = twattila::frame_history::AspectSize(display_width, display_height, scale);
    eval->InJitterOffsetX *= static_cast<float>(render[0]) / static_cast<float>(display_width);
    eval->InJitterOffsetY *= static_cast<float>(render[1]) / static_cast<float>(display_height);
    eval->InRenderSubrectDimensions.Width = render[0];
    eval->InRenderSubrectDimensions.Height = render[1];
  }
  const int flip = jitter_flip.load(std::memory_order_relaxed);
  if ((flip & 1) != 0) eval->InJitterOffsetX = -eval->InJitterOffsetX;
  if ((flip & 2) != 0) eval->InJitterOffsetY = -eval->InJitterOffsetY;
  {
    const std::unique_lock lock(state.mutex);
    state.last = *eval;
    ++state.evaluations;
  }
  if (on_evaluate) on_evaluate(context, eval);
  return original_evaluate(context, handle, parameters, eval);
}

}  // namespace internal

// Device creation (the engine module is loaded by then): hooks the game's DLSS feature creation and evaluation.
inline void Install() {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.searched) return;
  state.searched = true;
  HMODULE module = engine::Module();
  uint8_t* create = engine::FindPattern(internal::CREATE_FEATURE);
  uint8_t* evaluate = engine::FindPattern(internal::EVALUATE);
  if (uint8_t* condition = engine::FindPattern(internal::JITTER_CONDITION)) {
    state.jitter_site = condition + internal::JITTER_PATCH_OFFSET;
    std::memcpy(state.jitter_original.data(), state.jitter_site, state.jitter_original.size());
  }
  uint8_t* jitter = engine::FindPattern(internal::JITTER_FUNCTION);
  if (uint8_t* pass = engine::FindPattern(internal::DLSS_PASS)) {
    state.holder = reinterpret_cast<uint64_t*>(engine::RipTarget(pass + 3, pass + 8));
  }
  if (create != nullptr
      && std::memcmp(create + internal::QUALITY_IMMEDIATE, internal::QUALITY_OPCODE.data(), internal::QUALITY_OPCODE.size()) == 0) {
    state.quality_value = create + internal::QUALITY_IMMEDIATE + 2;
  }
  for (size_t index = 0; index < internal::FLAGS_IMMEDIATES.size(); ++index) {
    const auto& opcode = internal::FLAGS_OPCODES[index];
    if (create != nullptr && std::memcmp(create + internal::FLAGS_IMMEDIATES[index], opcode.data(), opcode.size()) == 0) {
      state.flags_values[index] = create + internal::FLAGS_IMMEDIATES[index] + 1;
    }
  }
  uint8_t* set_ui = nullptr;
  if (create != nullptr && create[internal::SET_UI_CALL] == 0xE8) {
    set_ui = engine::RipTarget(create + internal::SET_UI_CALL + 1, create + internal::SET_UI_CALL + 5);
  }
  if (create == nullptr || evaluate == nullptr || jitter == nullptr) {
    state.status = std::format("the game's DLSS code was not found (module {}, create {}, evaluate {}, jitter {})",
                               static_cast<void*>(module), static_cast<void*>(create), static_cast<void*>(evaluate),
                               static_cast<void*>(jitter));
  } else {
    internal::original_create = reinterpret_cast<internal::CreateFeature>(create);
    internal::original_evaluate = reinterpret_cast<internal::Evaluate>(evaluate);
    internal::original_jitter = reinterpret_cast<internal::Jitter>(jitter);
    state.hooks = {
        renodx::utils::detour::Function(&internal::original_create, &internal::OnCreateFeature, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE),
        renodx::utils::detour::Function(&internal::original_evaluate, &internal::OnEvaluate, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE),
        renodx::utils::detour::Function(&internal::original_jitter, &internal::OnJitter, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE),
    };
    if (set_ui != nullptr) {
      internal::original_set_ui = reinterpret_cast<internal::SetUI>(set_ui);
      state.hooks.push_back(renodx::utils::detour::Function(&internal::original_set_ui, &internal::OnSetUI, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE));
    }
    try {
      state.hook_lifetime.Install(state.hooks);
      const auto offset = [module](const void* address) {
        return (address != nullptr ? std::format("+{:#x}", static_cast<const uint8_t*>(address) - reinterpret_cast<uint8_t*>(module))
                                   : std::string("not found"));
      };
      state.status = std::format(
          "hooked (create {}, evaluate {}, jitter {}, jitter condition {}, holder {}, quality {}, flags {} / {}, "
          "presets {})",
          offset(create), offset(evaluate), offset(jitter), offset(state.jitter_site), offset(state.holder),
          offset(state.quality_value), offset(state.flags_values[0]), offset(state.flags_values[1]), offset(set_ui));
    } catch (const std::exception& error) {
      state.hooks.clear();
      state.status = std::format("hook failed: {}", error.what());
    }
  }
  renodx::utils::log::i("[Shogun 2 DLSS] The game's DLSS: ", state.status);
}

// The camera jitter while the game's DLSS runs (JITTER_CONDITION); off restores the game's code.
inline void SetJitter(bool enabled) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.jitter_site == nullptr || state.jitter_patched == enabled) return;
  const uint8_t* bytes = (enabled ? internal::JITTER_PATCH.data() : state.jitter_original.data());
  internal::WriteCode(state.jitter_site, bytes, internal::JITTER_PATCH.size());
  state.jitter_patched = enabled;
  renodx::utils::log::i("[Shogun 2 DLSS] Camera jitter with DLSS: ", (enabled ? "on" : "off (the game's)"));
}

inline void SetJitterFlip(int flip) {
  internal::jitter_flip.store(flip, std::memory_order_relaxed);
}

inline void SetRunning(bool running) {
  internal::dlss_running.store(running, std::memory_order_relaxed);
}

// Overlay: the render scale to use (1: DLAA). Applied by UpdateFeature.
inline void RequestScale(float scale) {
  internal::requested_scale.store(scale);
}

// Overlay: the DLSS preset for every mode (NVSDK_NGX_DLSS_Hint_Render_Preset_*, 0: the game's, DLSS's default per
// mode). Applied by UpdateFeature.
inline void RequestPreset(uint32_t preset) {
  internal::requested_preset.store(preset);
}

// Present: a new scale or preset clears the game's feature handle, so its DLSS pass creates the feature again with the
// new input size and preset hints (the old feature is left: NGX's release is not hooked). Returns the scale in effect.
inline float UpdateFeature() {
  auto& state = internal::state;
  const float scale = internal::requested_scale.load();
  const uint32_t preset = internal::requested_preset.load();
  if (scale != internal::active_scale.load() || preset != internal::active_preset.load()) {
    const std::unique_lock lock(state.mutex);
    if (state.holder != nullptr) {
      internal::active_scale.store(scale);
      internal::active_preset.store(preset);
      *state.holder = 0;
      renodx::utils::log::i(std::format("[Shogun 2 DLSS] Render scale {:.3f}, preset {}: the game's DLSS is created again",
                                        scale, preset));
    }
  }
  return internal::active_scale.load();
}

// The display size the game's feature outputs (0 x 0 before it is created).
inline std::array<uint32_t, 2> DisplaySize() {
  const std::shared_lock lock(internal::state.mutex);
  return {internal::state.output[0], internal::state.output[1]};
}

// Evaluations so far (a present compares it with the last one: is the game's DLSS running).
inline uint64_t Evaluations() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.evaluations;
}

inline void Uninstall() {
  SetJitter(false);
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.hooks.empty()) return;
  try {
    state.hook_lifetime.Uninstall(&state.hooks);
    // Frame generation keeps the addon's image loaded: a new device attaches the addon again in it, and Install hooks again.
    state.searched = false;
  } catch (const std::exception& error) {
    renodx::utils::log::e("[Shogun 2 DLSS] The game's DLSS hooks could not be removed; addon retained until process exit: ", error.what());
  }
}

inline void SetEvaluateCallback(std::function<void(ID3D11DeviceContext*, NVSDK_NGX_D3D11_DLSS_Eval_Params*)> callback) {
  internal::on_evaluate = std::move(callback);
}

// Overlay: what the game asked of DLSS.
inline std::string Status() {
  auto& state = internal::state;
  const std::shared_lock lock(state.mutex);
  if (state.evaluations == 0 && state.creations == 0) return state.status + "; not used yet (Anti-aliasing: DLSS)";
  const auto& last = state.last;
  return std::format(
      "{}\ncreated {} time(s): {}x{} -> {}x{}\nevaluated {} times: render subrect {}x{}, jitter ({:.3f}, {:.3f}), "
      "MV scale ({:.2f}, {:.2f}), sharpness {:.2f}, pre-exposure {:.3f}",
      state.status, state.creations, state.input[0], state.input[1], state.output[0], state.output[1],
      state.evaluations, last.InRenderSubrectDimensions.Width, last.InRenderSubrectDimensions.Height,
      last.InJitterOffsetX, last.InJitterOffsetY, last.InMVScaleX, last.InMVScaleY, last.Feature.InSharpness,
      last.InPreExposure);
}

}  // namespace twshogun2::game_dlss
