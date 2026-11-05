/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"

// The game after a GPU reset: it releases its flip swap chain and makes a new device and chain for the same window, the
// old device alive. A chain whose back buffer's view is bound or held keeps the window: CreateSwapChain answers
// 0x80070005. The game then asks for a blt chain, which would work, but the swapchain mod turned that into flip too, and
// the game hung (ATN's log 2026-11-05; WARP warp_tests/swap_loss). Nothing here runs without a removed device.
namespace twwarhammer3::device_loss {

constexpr auto NO_FRAME_LIMIT = std::chrono::seconds(90);

static std::shared_mutex devices_mutex;
static std::vector<ID3D11Device*> devices;  // not owned: from init_device to destroy_device
static std::atomic_bool presented = false;
static bool after_loss = false;
static bool keep_game_desc = false;
static reshade::api::swapchain_desc game_desc = {};

static void OnInitDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const std::unique_lock lock(devices_mutex);
  devices.push_back(reinterpret_cast<ID3D11Device*>(device->get_native()));
}

static void OnDestroyDevice(reshade::api::device* device) {
  const std::unique_lock lock(devices_mutex);
  std::erase(devices, reinterpret_cast<ID3D11Device*>(device->get_native()));
}

// Registered before the swapchain mod: the old chain goes with its context's bindings and a Flush (D3D11 destroys late).
static bool BeforeSwapchainMod(reshade::api::device_api, reshade::api::swapchain_desc& desc, void*) {
  {
    const std::unique_lock lock(devices_mutex);
    const auto removed = std::ranges::remove_if(devices, [](ID3D11Device* device) {
      if (device->GetDeviceRemovedReason() == S_OK) return false;
      ID3D11DeviceContext* context = nullptr;
      device->GetImmediateContext(&context);
      context->ClearState();
      context->Flush();
      context->Release();
      return true;
    });
    if (!removed.empty()) {
      devices.erase(removed.begin(), removed.end());
      after_loss = true;
      presented = false;
      HMODULE this_module = nullptr;  // pinned: the thread below may outlive an unload
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                         reinterpret_cast<LPCWSTR>(&presented), &this_module);
      std::thread([] {
        const auto started = std::chrono::steady_clock::now();
        while (!presented) {
          if (std::chrono::steady_clock::now() - started > NO_FRAME_LIMIT) {
            renodx::utils::log::e("[Total War] GPU reset: no frame for 90 s after the game's new device, the game is closed.");
            TerminateProcess(GetCurrentProcess(), 1);
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        renodx::utils::log::i("[Total War] GPU reset: the game draws again.");
      }).detach();
    }
  }
  keep_game_desc = after_loss && desc.present_mode == DXGI_SWAP_EFFECT_DISCARD;
  if (keep_game_desc) game_desc = desc;
  return false;
}

// Registered after the swapchain mod: the game's blt fallback stays as the game asked.
static bool AfterSwapchainMod(reshade::api::device_api, reshade::api::swapchain_desc& desc, void*) {
  if (!keep_game_desc) return false;
  keep_game_desc = false;
  desc = game_desc;
  renodx::utils::log::w("[Total War] GPU reset: the window is still taken, the game's own swap chain is used.");
  return true;
}

static void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  presented.store(true, std::memory_order_relaxed);
}

}  // namespace twwarhammer3::device_loss
