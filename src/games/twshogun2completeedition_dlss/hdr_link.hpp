/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "../../utils/cross_addon.hpp"

// What the DLSS addon hands the RenoDX HDR addon past ReShade.ini (its graphics rows): one-shot requests for the HDR
// addon's ray tracing, the overlay's helper choices, and the game's DLSS evaluation through the HDR addon's helper
// (its bridge to renodx-tw-dlaa64.exe: one a process, the ray tracing's): its upscaler in place of the game's DLSS,
// frame generation's inputs. Shared by both addons (cross_addon): SharedData is append-only.
namespace twshogun2::hdr_link {

// camera_VS_PS's floats (44 registers).
constexpr size_t CAMERA_FLOATS = 44 * 4;

// The game's DLSS evaluation (NVSDK_NGX_D3D11_DLSS_Eval_Params after the DLSS addon's render scale and jitter): the
// textures' top-left render size in, the display size out.
struct UpscaleRequest {
  ID3D11DeviceContext* context = nullptr;
  ID3D11Resource* color = nullptr;   // RGBA16F, linear, before the tone mapper
  ID3D11Resource* depth = nullptr;   // D24S8, 0 near
  ID3D11Resource* motion = nullptr;  // RG16F, pixels x motion_scale: to where each pixel was last frame
  ID3D11Resource* output = nullptr;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  float jitter_x = 0.f;  // render pixels, NGX's
  float jitter_y = 0.f;
  float motion_scale_x = 1.f;
  float motion_scale_y = 1.f;
  bool reset = false;
  uint32_t preset = 0;  // NVSDK_NGX_DLSS_Hint_Render_Preset
  // The main view's camera_VS_PS as uploaded (position c0, view c2-c5 and projection c6-c9 on column vectors, near and
  // far c34); false: none this frame.
  bool camera = false;
  std::array<float, CAMERA_FLOATS> constants = {};
};
// The HDR addon's: true when it wrote the output (the game's DLSS then skipped).
using UpscaleFunction = bool (*)(const UpscaleRequest& request);

// The DLSS addon's overlay rows for the helper.
struct Choices {
  uint32_t upscaler = 0;            // 0: the game's DLSS, else twattila::dlaa_bridge::UPSCALER_* + 1
  uint32_t ray_reconstruction = 0;  // 0: off, else NVSDK_NGX_RayReconstruction_Hint_Render_Preset + 1
  uint32_t frame_generation = 0;    // frames shown per rendered frame (2-6; 1: Dynamic), 0: off
  uint32_t frame_backend = 0;       // twattila::dlaa_bridge::FG_BACKEND_*
  uint32_t reflex = 0;              // 0 off, 1 on, 2 on + boost
  uint32_t frame_limit = 0;         // rendered frames per second, 0: none
  uint32_t frame_preset = 0;        // NVIDIA's A-C as 1-3, 0: the driver's
  bool hud_recomposition = true;
  uint32_t frame_debug_view = 0;  // 1 the HUD-less image, 2 the UI layer
};

struct __declspec(uuid("22ca4013-6c28-4d0d-b7c0-c7956e91bf43")) SharedData {
  std::atomic_bool rt_profile = false;  // Record Ray Tracing Profile pressed, not yet started
  std::mutex choices_mutex;
  Choices choices;
  // The HDR addon's (null: not loaded); called under the shared lock, cleared under the unique lock before it unloads.
  std::shared_mutex upscale_mutex;
  UpscaleFunction upscale = nullptr;
};

static renodx::utils::cross_addon::Shared<SharedData> shared;

inline void Use(uint32_t fdw_reason) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      shared.RegisterModule();
      break;
    case DLL_PROCESS_DETACH:
      shared.UnregisterModule();
      break;
    default:
      break;
  }
}

// DLSS addon: the overlay's button.
inline void RequestProfile() {
  if (shared.data != nullptr) shared.data->rt_profile.store(true, std::memory_order_relaxed);
}

// HDR addon: each present; true once a press.
inline bool TakeProfileRequest() {
  return shared.data != nullptr && shared.data->rt_profile.exchange(false, std::memory_order_relaxed);
}

// DLSS addon: its overlay's choices (each present).
inline void SetChoices(const Choices& choices) {
  if (shared.data == nullptr) return;
  const std::unique_lock lock(shared.data->choices_mutex);
  shared.data->choices = choices;
}

// HDR addon: the DLSS addon's choices (none: the game's DLSS, no frame generation).
inline Choices ChoicesNow() {
  if (shared.data == nullptr) return {};
  const std::unique_lock lock(shared.data->choices_mutex);
  return shared.data->choices;
}

// HDR addon: its upscaler at attach, null before it detaches.
inline void SetUpscale(UpscaleFunction upscale) {
  if (shared.data == nullptr) return;
  const std::unique_lock lock(shared.data->upscale_mutex);
  shared.data->upscale = upscale;
}

// DLSS addon: the evaluation through the HDR addon's helper (with its upscaler or frame generation chosen); false: the
// game's DLSS.
inline bool Upscale(const UpscaleRequest& request) {
  if (shared.data == nullptr) return false;
  const std::shared_lock lock(shared.data->upscale_mutex);
  return shared.data->upscale != nullptr && shared.data->upscale(request);
}

// DLSS addon's overlay: whether the HDR addon can take the evaluations (loaded, this version).
inline bool UpscalerAvailable() {
  if (shared.data == nullptr) return false;
  const std::shared_lock lock(shared.data->upscale_mutex);
  return shared.data->upscale != nullptr;
}

}  // namespace twshogun2::hdr_link
