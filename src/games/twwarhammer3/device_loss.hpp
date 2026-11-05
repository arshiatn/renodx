/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
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
constexpr uint64_t STALL_LOG_MS = 20000;
constexpr DWORD WATCH_POLL_MS = 1000;

static std::shared_mutex devices_mutex;
static std::vector<ID3D11Device*> devices;  // not owned: from init_device to destroy_device
static std::atomic_bool presented = false;
static bool after_loss = false;
static bool keep_game_desc = false;
static reshade::api::swapchain_desc game_desc = {};
static std::atomic_uint64_t last_present_tick = 0;
static std::atomic_uint32_t present_thread = 0;
static std::vector<ID3D11Device*> removed_logged;  // under devices_mutex
static HANDLE watch_thread = nullptr;
static HANDLE watch_stop = nullptr;

static const char* WindowInFront() {
  DWORD process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &process);
  return (process == GetCurrentProcessId()) ? "in front" : "not in front";
}

// 0x887A0006: hung, the game's own work (ours within it). 0x887A0007: reset, another's work.
static void LogRemoved(ID3D11Device* device, HRESULT reason) {
  if (std::ranges::find(removed_logged, device) != removed_logged.end()) return;
  removed_logged.push_back(device);
  renodx::utils::log::w(std::format("[Total War] GPU reset: the game's device removed (0x{:08X}), last frame {} ms ago, the game's window {}.",
                                    static_cast<uint32_t>(reason), GetTickCount64() - last_present_tick.load(std::memory_order_relaxed), WindowInFront()));
}

// Where the frame thread stands when frames stopped: its instruction and the code addresses on its stack, nearest first (no
// unwinding, so some are stale; nothing that can take a lock while it is suspended).
static void LogStall(uint64_t age_ms) {
  const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, present_thread.load(std::memory_order_relaxed));
  if (thread == nullptr) return;
  std::array<uint64_t, 1024> stack = {};
  SIZE_T stack_bytes = 0;
  CONTEXT context = {.ContextFlags = CONTEXT_CONTROL};
  SuspendThread(thread);
  const bool stopped = GetThreadContext(thread, &context) != FALSE;
  if (stopped) {
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(context.Rsp), stack.data(), sizeof(stack), &stack_bytes);
  }
  ResumeThread(thread);
  CloseHandle(thread);
  if (!stopped) return;
  std::string line = std::format("[Total War] No frame for {} s, the game's window {}, the frame thread at:", age_ms / 1000, WindowInFront());
  const auto add_address = [&line](uint64_t address) {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &module) == FALSE) return false;
    MEMORY_BASIC_INFORMATION page = {};
    constexpr DWORD EXECUTABLE = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (VirtualQuery(reinterpret_cast<const void*>(address), &page, sizeof(page)) == 0 || (page.Protect & EXECUTABLE) == 0) return false;
    std::array<char, MAX_PATH> path = {};
    GetModuleFileNameA(module, path.data(), MAX_PATH);
    const std::string file = path.data();
    line += std::format(" {}+0x{:X}", file.substr(file.find_last_of('\\') + 1), address - reinterpret_cast<uint64_t>(module));
    return true;
  };
  add_address(context.Rip);
  int added = 0;
  for (size_t word = 0; word < stack_bytes / sizeof(uint64_t) && added < 32; ++word) {
    if (add_address(stack[word])) ++added;
  }
  renodx::utils::log::w(line);
}

static DWORD WINAPI WatchLoop(void*) {
  const char* last_front = nullptr;
  uint64_t stall_logged_for = 0;
  while (WaitForSingleObject(watch_stop, WATCH_POLL_MS) == WAIT_TIMEOUT) {
    {
      const std::unique_lock lock(devices_mutex);
      for (ID3D11Device* device : devices) {
        if (const HRESULT reason = device->GetDeviceRemovedReason(); FAILED(reason)) LogRemoved(device, reason);
      }
    }
    if (const uint64_t frame = last_present_tick.load(std::memory_order_relaxed); frame != 0) {
      if (const char* front = WindowInFront(); last_front == nullptr || std::string_view(front) != last_front) {
        if (last_front != nullptr) renodx::utils::log::i("[Total War] The game's window: ", front, ".");
        last_front = front;
      }
      if (const uint64_t age = GetTickCount64() - frame; age >= STALL_LOG_MS && stall_logged_for != frame) {
        stall_logged_for = frame;
        LogStall(age);
      }
    }
  }
  return 0;
}

// Before the addon can go; a device's last destroy or the addon's detach, whichever comes first.
static void StopWatch() {
  if (watch_thread == nullptr) return;
  SetEvent(watch_stop);
  WaitForSingleObject(watch_thread, INFINITE);
  CloseHandle(watch_thread);
  CloseHandle(watch_stop);
  watch_thread = nullptr;
}

static void OnInitDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const std::unique_lock lock(devices_mutex);
  devices.push_back(reinterpret_cast<ID3D11Device*>(device->get_native()));
  if (watch_thread == nullptr) {
    watch_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    watch_thread = CreateThread(nullptr, 0, WatchLoop, nullptr, 0, nullptr);
  }
}

static void OnDestroyDevice(reshade::api::device* device) {
  bool last_device = false;
  {
    const std::unique_lock lock(devices_mutex);
    std::erase(devices, reinterpret_cast<ID3D11Device*>(device->get_native()));
    last_device = devices.empty();
  }
  if (last_device) StopWatch();
}

// Registered before the swapchain mod: the old chain goes with its context's bindings and a Flush (D3D11 destroys late).
static bool BeforeSwapchainMod(reshade::api::device_api, reshade::api::swapchain_desc& desc, void*) {
  {
    const std::unique_lock lock(devices_mutex);
    const auto removed = std::ranges::remove_if(devices, [](ID3D11Device* device) {
      const HRESULT reason = device->GetDeviceRemovedReason();
      if (reason == S_OK) return false;
      LogRemoved(device, reason);
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
  last_present_tick.store(GetTickCount64(), std::memory_order_relaxed);
  present_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
}

}  // namespace twwarhammer3::device_loss
