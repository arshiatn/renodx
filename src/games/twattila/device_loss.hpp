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
#include <cstdint>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../mods/swapchain.hpp"
#include "../../utils/log.hpp"
#include "./output_encoding.hpp"

// A Total War game after a GPU reset. It builds a new device and swap chain for the same window, in one of two orders:
// - The old device alive (WH3, ATN's log 2026-11-05): a chain whose back buffer's view is bound or held keeps the
//   window, CreateSwapChain answers 0x80070005. The game then asks for a blt chain, which would work, but the swapchain
//   mod turned that into flip too, and the game hung (WARP: test/tw_device_loss).
// - The old device destroyed first (Attila, a player's log 2026-11-03): ReShade unloads the addon with the game's last
//   device and loads it again with the new one, so only the destroy's part runs in the old image.
// Nothing here changes a frame without a removed device.
namespace twattila::device_loss {

// A test shortens it.
static uint64_t no_frame_limit_ms = 90000;
// A frame that does not come is sampled at 20, 40 and 60 s: one sample cannot tell a hang from a long job, nor whether
// the frame thread ever left the code it was first seen in.
constexpr uint64_t STALL_LOG_MS = 20000;
constexpr uint32_t STALL_SAMPLES = 3;
constexpr DWORD WATCH_POLL_MS = 1000;

// Not owned: from init_device to destroy_device. A single-threaded device (Attila, Rome II, Thrones, Shogun 2) is only
// asked on the thread that presents.
struct TrackedDevice {
  ID3D11Device* device = nullptr;
  bool single_threaded = false;
};

static std::shared_mutex devices_mutex;
static std::vector<TrackedDevice> devices;
static std::vector<ID3D11Device*> removed_logged;  // under devices_mutex
static std::atomic_bool removal_check_wanted = false;
static std::atomic_bool presented = false;
static std::atomic_bool awaiting_frame = false;  // a device was lost and no frame has come since
static bool after_loss = false;
static bool resizing = false;  // the next create_swapchain is a ResizeBuffers
static bool keep_game_desc = false;
static reshade::api::swapchain_desc game_desc = {};
static HWND native_flip_window = nullptr;  // the game itself asked for a flip chain for it
static std::atomic<HWND> game_window = nullptr;
static std::atomic_uint64_t last_present_tick = 0;
static std::atomic_uint32_t present_thread = 0;
static HANDLE watch_thread = nullptr;
static HANDLE watch_stop = nullptr;

static bool GameInFront() {
  DWORD process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &process);
  return process == GetCurrentProcessId();
}

// 0x887A0006: hung, the game's own work (ours within it). 0x887A0007: reset, another's work. devices_mutex held.
static void LogRemoved(ID3D11Device* device, HRESULT reason) {
  if (std::ranges::find(removed_logged, device) != removed_logged.end()) return;
  removed_logged.push_back(device);
  renodx::utils::log::w(std::format("[Total War] GPU reset: the game's device removed (0x{:08X}), last frame {} ms ago, the game's window {}.",
                                    static_cast<uint32_t>(reason), GetTickCount64() - last_present_tick.load(std::memory_order_relaxed),
                                    GameInFront() ? "in front" : "not in front"));
}

// A removed device lets go of its swap chain's window: its context's bindings and D3D11's late destruction keep it.
static void DropBindings(ID3D11Device* device) {
  ID3D11DeviceContext* context = nullptr;
  device->GetImmediateContext(&context);
  context->ClearState();
  context->Flush();
  context->Release();
}

// Where the frame thread stands when frames stopped: its instruction and the code addresses on its stack, nearest first (no
// unwinding, so some are stale; nothing that can take a lock while it is suspended).
static void LogStall(uint64_t age_ms) {
  const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, present_thread.load(std::memory_order_relaxed));
  if (thread == nullptr) return;
  std::array<uintptr_t, 1024> stack = {};
  SIZE_T stack_bytes = 0;
  CONTEXT context = {.ContextFlags = CONTEXT_CONTROL};
  SuspendThread(thread);
  const bool stopped = GetThreadContext(thread, &context) != FALSE;
#if defined(_WIN64)
  const uintptr_t instruction = context.Rip;
  const uintptr_t stack_top = context.Rsp;
#else
  const uintptr_t instruction = context.Eip;
  const uintptr_t stack_top = context.Esp;
#endif
  if (stopped) {
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(stack_top), stack.data(), sizeof(stack), &stack_bytes);
  }
  ResumeThread(thread);
  CloseHandle(thread);
  if (!stopped) return;
  std::string line = std::format("[Total War] No frame for {} s, the game's window {}, the frame thread at:", age_ms / 1000,
                                 GameInFront() ? "in front" : "not in front");
  const auto add_address = [&line](uintptr_t address) {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &module) == FALSE) return false;
    MEMORY_BASIC_INFORMATION page = {};
    constexpr DWORD EXECUTABLE = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (VirtualQuery(reinterpret_cast<const void*>(address), &page, sizeof(page)) == 0 || (page.Protect & EXECUTABLE) == 0) return false;
    std::array<char, MAX_PATH> path = {};
    GetModuleFileNameA(module, path.data(), MAX_PATH);
    const std::string file = path.data();
    line += std::format(" {}+0x{:X}", file.substr(file.find_last_of('\\') + 1), address - reinterpret_cast<uintptr_t>(module));
    return true;
  };
  add_address(instruction);
  int added = 0;
  for (size_t word = 0; word < stack_bytes / sizeof(uintptr_t) && added < 32; ++word) {
    if (add_address(stack[word])) ++added;
  }
  renodx::utils::log::w(line);
}

static DWORD WINAPI WatchLoop(void*) {
  bool front_known = false;
  bool last_front = false;
  uint64_t stalled_frame = 0;
  uint32_t stall_samples = 0;
  uint64_t awaited_ms = 0;
  while (WaitForSingleObject(watch_stop, WATCH_POLL_MS) == WAIT_TIMEOUT) {
    {
      const std::unique_lock lock(devices_mutex);
      for (const TrackedDevice& tracked : devices) {
        if (tracked.single_threaded) {
          removal_check_wanted.store(true, std::memory_order_relaxed);
          continue;
        }
        if (const HRESULT reason = tracked.device->GetDeviceRemovedReason(); FAILED(reason)) {
          LogRemoved(tracked.device, reason);
        }
      }
    }
    if (const uint64_t frame = last_present_tick.load(std::memory_order_relaxed); frame != 0) {
      if (const bool front = GameInFront(); !front_known || front != last_front) {
        if (front_known) {
          renodx::utils::log::i("[Total War] The game's window: ", front ? "in front" : "not in front", ".");
        }
        front_known = true;
        last_front = front;
      }
      if (frame != stalled_frame) {
        stalled_frame = frame;
        stall_samples = 0;
      }
      if (const uint64_t age = GetTickCount64() - frame; stall_samples < STALL_SAMPLES && age >= STALL_LOG_MS * (stall_samples + 1)) {
        ++stall_samples;
        LogStall(age);
      }
    }
    if (!awaiting_frame.load(std::memory_order_relaxed)) continue;
    if (presented.load(std::memory_order_relaxed)) {
      awaiting_frame.store(false, std::memory_order_relaxed);
      awaited_ms = 0;
      renodx::utils::log::i("[Total War] GPU reset: the game draws again.");
      continue;
    }
    // A game minimised or behind another program draws nothing and is not hung: only the time its window is in front, or
    // does not answer Windows (its ghost is in front then, not the game), counts.
    const HWND window = game_window.load(std::memory_order_relaxed);
    if (IsHungAppWindow(window) != FALSE || (GameInFront() && IsIconic(window) == FALSE)) {
      awaited_ms += WATCH_POLL_MS;
    }
    if (awaited_ms >= no_frame_limit_ms) {
      renodx::utils::log::e(std::format("[Total War] GPU reset: no frame for {} s after the game's new device, the game is closed.",
                                        no_frame_limit_ms / 1000));
      TerminateProcess(GetCurrentProcess(), 1);
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
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  const std::unique_lock lock(devices_mutex);
  devices.push_back({.device = native, .single_threaded = (native->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0});
  if (watch_thread == nullptr) {
    watch_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    watch_thread = CreateThread(nullptr, 0, WatchLoop, nullptr, 0, nullptr);
  }
}

static void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  bool last_device = false;
  {
    const std::unique_lock lock(devices_mutex);
    // Destroyed before the game's new swap chain (Attila's order): the same step as BeforeSwapchainMod's, here.
    if (std::erase_if(devices, [native](const TrackedDevice& tracked) { return tracked.device == native; }) != 0) {
      if (const HRESULT reason = native->GetDeviceRemovedReason(); FAILED(reason)) {
        LogRemoved(native, reason);
        DropBindings(native);
      }
    }
    std::erase(removed_logged, native);
    last_device = devices.empty();
  }
  if (last_device) StopWatch();
}

static void OnDestroySwapchain(reshade::api::swapchain*, bool resize) {
  resizing = resize;
}

static void OnInitSwapchain(reshade::api::swapchain*, bool) {
  resizing = false;
}

// Registered before the swapchain mod: the old chain goes with its context's bindings and a Flush (D3D11 destroys late),
// and a new chain's output is decided.
static bool BeforeSwapchainMod(reshade::api::device_api, reshade::api::swapchain_desc& desc, void* hwnd) {
  auto* window = static_cast<HWND>(hwnd);
  game_window.store(window, std::memory_order_relaxed);
  {
    const std::unique_lock lock(devices_mutex);
    const auto removed = std::ranges::remove_if(devices, [](const TrackedDevice& tracked) {
      const HRESULT reason = tracked.device->GetDeviceRemovedReason();
      if (reason == S_OK) return false;
      LogRemoved(tracked.device, reason);
      DropBindings(tracked.device);
      return true;
    });
    if (!removed.empty()) {
      devices.erase(removed.begin(), removed.end());
      after_loss = true;
      presented.store(false, std::memory_order_relaxed);
      awaiting_frame.store(true, std::memory_order_relaxed);
    }
  }
  if (!std::exchange(resizing, false)) {
    if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD) {
      native_flip_window = window;
    }
    output_encoding::Decide(window);
  }
  // A blt request for a window the game itself gave a flip chain: the game's fallback after 0x80070005. A game that
  // always asks for blt (Attila) has no such request to tell apart, and keeps its upgrade.
  keep_game_desc = after_loss && desc.present_mode == DXGI_SWAP_EFFECT_DISCARD && window == native_flip_window;
  if (keep_game_desc) game_desc = desc;
  return false;
}

// Registered after the swapchain mod: the game's blt fallback stays as the game asked, 8-bit SDR, and the mod treats
// the chain as one it left alone (it would upgrade the views of back buffers that were not upgraded).
static bool AfterSwapchainMod(reshade::api::device_api, reshade::api::swapchain_desc& desc, void*) {
  if (!keep_game_desc) return false;
  keep_game_desc = false;
  desc = game_desc;
  renodx::mods::swapchain::upgraded_swapchain_desc.reset();
  output_encoding::Apply(output_encoding::SDR);
  renodx::utils::log::w("[Total War] GPU reset: the window is still taken, the game's own swap chain is used (SDR until the game restarts).");
  return true;
}

static void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  if (removal_check_wanted.load(std::memory_order_relaxed)) {
    removal_check_wanted.store(false, std::memory_order_relaxed);
    if (auto* device = swapchain->get_device(); device->get_api() == reshade::api::device_api::d3d11) {
      auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
      if (const HRESULT reason = native->GetDeviceRemovedReason(); FAILED(reason)) {
        const std::unique_lock lock(devices_mutex);
        LogRemoved(native, reason);
      }
    }
  }
  presented.store(true, std::memory_order_relaxed);
  last_present_tick.store(GetTickCount64(), std::memory_order_relaxed);
  present_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
}

// Before renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH): create_swapchain callbacks run in registration order.
static void Install() {
  reshade::register_event<reshade::addon_event::create_swapchain>(BeforeSwapchainMod);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
}

// At the end of the addon's attach: after the swapchain mod's create_swapchain, and after every other destroy_device
// (a removed device's Flush comes once the addon's own objects on it are released).
static void InstallLast() {
  reshade::register_event<reshade::addon_event::create_swapchain>(AfterSwapchainMod);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
}

static void Uninstall() {
  reshade::unregister_event<reshade::addon_event::create_swapchain>(AfterSwapchainMod);
  reshade::unregister_event<reshade::addon_event::create_swapchain>(BeforeSwapchainMod);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  StopWatch();
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  // An image kept loaded after the detach tracks nothing.
  const std::unique_lock lock(devices_mutex);
  devices.clear();
  removed_logged.clear();
}

}  // namespace twattila::device_loss
