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
// addon's ray tracing, and the game's DLSS evaluation through the HDR addon's helper upscaler (its bridge to
// renodx-tw-dlaa64.exe: one a process, the ray tracing's). Shared by both addons (cross_addon): SharedData is
// append-only.
namespace twshogun2::hdr_link {

// The game's DLSS evaluation (NVSDK_NGX_D3D11_DLSS_Eval_Params after the DLSS addon's render scale and jitter) for the
// helper's upscaler: the textures' top-left render size in, the display size out.
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
  // camera_VS_PS's view (c2-c5) and projection (c6-c9) as uploaded (registers on column vectors); false: none this frame.
  bool camera = false;
  std::array<float, 16> view = {};
  std::array<float, 16> projection = {};
};
// The HDR addon's: true when it wrote the output (the game's DLSS then skipped).
using UpscaleFunction = bool (*)(const UpscaleRequest& request);

struct __declspec(uuid("22ca4013-6c28-4d0d-b7c0-c7956e91bf43")) SharedData {
  std::atomic_bool rt_profile = false;  // Record Ray Tracing Profile pressed, not yet started
  // The DLSS addon's Upscaler (0: the game's DLSS, else twattila::dlaa_bridge::UPSCALER_* + 1) and Ray Reconstruction
  // (0: off, else NVSDK_NGX_RayReconstruction_Hint_Render_Preset + 1).
  std::atomic_uint32_t upscaler = 0;
  std::atomic_uint32_t ray_reconstruction = 0;
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
inline void SetChoices(uint32_t upscaler, uint32_t ray_reconstruction) {
  if (shared.data == nullptr) return;
  shared.data->upscaler.store(upscaler, std::memory_order_relaxed);
  shared.data->ray_reconstruction.store(ray_reconstruction, std::memory_order_relaxed);
}

// HDR addon: the DLSS addon's choices.
inline std::array<uint32_t, 2> Choices() {
  if (shared.data == nullptr) return {};
  return {shared.data->upscaler.load(std::memory_order_relaxed), shared.data->ray_reconstruction.load(std::memory_order_relaxed)};
}

// HDR addon: its upscaler at attach, null before it detaches.
inline void SetUpscale(UpscaleFunction upscale) {
  if (shared.data == nullptr) return;
  const std::unique_lock lock(shared.data->upscale_mutex);
  shared.data->upscale = upscale;
}

// DLSS addon: the evaluation through the HDR addon's upscaler (its Upscaler not the game's); false: the game's DLSS.
inline bool Upscale(const UpscaleRequest& request) {
  if (shared.data == nullptr || shared.data->upscaler.load(std::memory_order_relaxed) == 0) return false;
  const std::shared_lock lock(shared.data->upscale_mutex);
  return shared.data->upscale != nullptr && shared.data->upscale(request);
}

// DLSS addon's overlay: whether the HDR addon can upscale (loaded, this version).
inline bool UpscalerAvailable() {
  if (shared.data == nullptr) return false;
  const std::shared_lock lock(shared.data->upscale_mutex);
  return shared.data->upscale != nullptr;
}

}  // namespace twshogun2::hdr_link
