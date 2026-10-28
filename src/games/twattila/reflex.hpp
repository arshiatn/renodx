/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./frame_generation.hpp"
#include "./nvapi.hpp"

// NVIDIA Reflex for the game's own presents (frame generation off; with it the helper's Streamline paces the game):
// NVAPI's low latency sleep mode on the game's D3D11 device, its Sleep right after each present (the next frame starts
// only as the GPU will need it) and the frame's latency markers around them. NVIDIA GPUs only: elsewhere nothing runs.
namespace twattila::reflex {

namespace internal {

using Microsoft::WRL::ComPtr;

// nvapi.h's NV_SET_SLEEP_MODE_PARAMS_V1 and NV_LATENCY_MARKER_PARAMS_V1 (version: the size, 1 << 16).
struct SleepModeParams {
  uint32_t version = 0;
  uint8_t low_latency_mode = 0;
  uint8_t low_latency_boost = 0;
  uint32_t minimum_interval_us = 0;  // 0: no frame rate limit
  uint8_t use_markers_to_optimize = 0;
  uint8_t use_min_queue_time = 0;
  std::array<uint8_t, 30> reserved = {};
};
static_assert(sizeof(SleepModeParams) == 44);

struct LatencyMarkerParams {
  uint32_t version = 0;
  uint64_t frame_id = 0;
  uint32_t marker_type = 0;
  uint64_t reserved_value = 0;
  std::array<uint8_t, 56> reserved = {};
};
static_assert(sizeof(LatencyMarkerParams) == 88);

// NV_LATENCY_MARKER_TYPE.
constexpr uint32_t SIMULATION_START = 0, SIMULATION_END = 1, RENDERSUBMIT_START = 2, RENDERSUBMIT_END = 3,
                   PRESENT_START = 4, PRESENT_END = 5;

using SetSleepMode = int(__cdecl*)(IUnknown* device, SleepModeParams* params);
using Sleep = int(__cdecl*)(IUnknown* device);
using SetLatencyMarker = int(__cdecl*)(IUnknown* device, LatencyMarkerParams* params);

struct State {
  std::shared_mutex mutex;
  bool loaded = false;  // NVAPI looked for (the functions null: none)
  SetSleepMode set_sleep_mode = nullptr;
  Sleep sleep = nullptr;
  SetLatencyMarker set_latency_marker = nullptr;
  ComPtr<ID3D11Device> device;  // the game's, with the sleep mode below asked of it
  uint32_t mode = 0;            // asked: 0 off, 1 on, 2 on + boost
  uint32_t frame_limit = 0;     // asked: rendered frames per second (0: none)
  bool running = false;         // NVAPI took a mode other than off
  uint64_t frame = 0;           // the markers' frame id (strictly increasing)
};

static State state;

// This frame's latency marker `type` (the lock held).
inline void Mark(uint32_t type) {
  if (!state.running || state.set_latency_marker == nullptr) return;
  LatencyMarkerParams params = {.version = sizeof(LatencyMarkerParams) | (1u << 16), .frame_id = state.frame, .marker_type = type};
  state.set_latency_marker(state.device.Get(), &params);
}

// Before the game's present: its frame submitted, the present starting.
inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  const std::unique_lock lock(state.mutex);
  Mark(RENDERSUBMIT_END);
  Mark(PRESENT_START);
}

// After it: the next frame waits in Reflex's sleep until the GPU will need it, then starts.
inline void OnFinishPresent(reshade::api::command_queue*, reshade::api::swapchain*) {
  std::unique_lock lock(state.mutex);
  if (!state.running || state.sleep == nullptr) return;
  Mark(PRESENT_END);
  ++state.frame;
  const ComPtr<ID3D11Device> device = state.device;
  const Sleep sleep = state.sleep;
  lock.unlock();
  sleep(device.Get());
  lock.lock();
  Mark(SIMULATION_START);
  Mark(SIMULATION_END);
  Mark(RENDERSUBMIT_START);
}

}  // namespace internal

// Every present (anti_aliasing.hpp): Reflex `mode` (0 off, 1 on, 2 on + boost) and its `frame_limit` (rendered frames
// per second, 0 none) on the game's device, while frame generation does not present (its own Reflex paces the game).
inline void Update(reshade::api::swapchain* swapchain, uint32_t mode, uint32_t frame_limit) {
  auto& state = internal::state;
  const uint32_t wanted = (frame_generation::Presenting() ? 0u : mode);
  const uint32_t wanted_limit = (wanted != 0 ? frame_limit : 0u);
  auto* device = reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&device);
  const std::unique_lock lock(state.mutex);
  if (wanted == state.mode && wanted_limit == state.frame_limit && (wanted == 0 || device == state.device.Get())) return;
  if (!state.loaded) {
    state.loaded = true;
    state.set_sleep_mode = reinterpret_cast<internal::SetSleepMode>(nvapi::Function(0xAC1CA9E0));
    state.sleep = reinterpret_cast<internal::Sleep>(nvapi::Function(0x852CD1D2));
    state.set_latency_marker = reinterpret_cast<internal::SetLatencyMarker>(nvapi::Function(0xD9984C05));
  }
  state.mode = wanted;
  state.frame_limit = wanted_limit;
  state.device = device;
  if (state.set_sleep_mode == nullptr || state.sleep == nullptr || device == nullptr) {
    state.running = false;
    if (wanted != 0) renodx::utils::log::w("[Total War] Reflex: NVIDIA's NVAPI is not available (not an NVIDIA GPU?).");
    return;
  }
  internal::SleepModeParams params = {
      .version = sizeof(internal::SleepModeParams) | (1u << 16),
      .low_latency_mode = static_cast<uint8_t>(wanted != 0 ? 1 : 0),
      .low_latency_boost = static_cast<uint8_t>(wanted == 2 ? 1 : 0),
      .minimum_interval_us = (wanted_limit != 0 ? 1000000u / wanted_limit : 0u),
  };
  const int result = state.set_sleep_mode(device, &params);
  state.running = (result == 0 && wanted != 0);
  const char* mode_name = (wanted == 0 ? "off" : (wanted == 2 ? "on + boost" : "on"));
  const std::string limit_text = (wanted_limit != 0 ? ", frame rate limit " + std::to_string(wanted_limit) : std::string());
  const std::string result_text = (result == 0 ? std::string(".") : " refused by NVAPI (" + std::to_string(result) + ").");
  renodx::utils::log::i("[Total War] Reflex: ", mode_name, limit_text, result_text);
}

inline void Install() {
  reshade::register_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::register_event<reshade::addon_event::finish_present>(internal::OnFinishPresent);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::unregister_event<reshade::addon_event::finish_present>(internal::OnFinishPresent);
}

// The game's device is going away: Reflex with it.
inline void OnDestroyDevice(ID3D11Device* device) {
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.device.Get() != device) return;
  internal::state.device.Reset();
  internal::state.mode = 0;
  internal::state.frame_limit = 0;
  internal::state.running = false;
}

}  // namespace twattila::reflex
