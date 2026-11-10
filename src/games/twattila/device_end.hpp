/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"

// Why the game's D3D11 device or swap chain went away, for a report's ReShade.log: a hung device (the game's own GPU
// work, the addon's within it), a reset (another device's work), no memory (the 32-bit games' address space) and the
// game's own teardown (a display or mode change) look alike without it. Only at these two events, nothing per frame.
namespace twattila::device_end {

// One line: the device's removed reason and, in a 32-bit game, the address space left.
inline void Log(reshade::api::device* device, const char* ended) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const HRESULT reason = reinterpret_cast<ID3D11Device*>(device->get_native())->GetDeviceRemovedReason();
  std::string line = std::format("[Total War] The game's {} ends: the device's removed reason ", ended);
  switch (reason) {
    case S_OK:
      line += "S_OK (not removed)";
      break;
    case DXGI_ERROR_DEVICE_HUNG:
      line += "HUNG";
      break;
    case DXGI_ERROR_DEVICE_RESET:
      line += "RESET";
      break;
    case DXGI_ERROR_DEVICE_REMOVED:
      line += "REMOVED";
      break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
      line += "DRIVER_INTERNAL_ERROR";
      break;
    case E_OUTOFMEMORY:
      line += "E_OUTOFMEMORY";
      break;
    default:
      line += std::format("0x{:08X}", static_cast<uint32_t>(reason));
      break;
  }
#if !defined(_WIN64)
  MEMORYSTATUSEX memory = {.dwLength = sizeof(memory)};
  GlobalMemoryStatusEx(&memory);
  SYSTEM_INFO system = {};
  GetSystemInfo(&system);
  SIZE_T largest_free = 0;
  auto address = reinterpret_cast<uintptr_t>(system.lpMinimumApplicationAddress);
  MEMORY_BASIC_INFORMATION region = {};
  while (address < reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress)
         && VirtualQuery(reinterpret_cast<LPCVOID>(address), &region, sizeof(region)) != 0) {
    if (region.State == MEM_FREE) {
      largest_free = std::max(largest_free, region.RegionSize);
    }
    const uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
    if (next <= address) break;  // the address space's end
    address = next;
  }
  line += std::format("; address space free {} MB of {} MB, largest free block {} MB", memory.ullAvailVirtual >> 20,
                      memory.ullTotalVirtual >> 20, largest_free >> 20);
#endif
  line += ".";
  if (reason == S_OK) {
    renodx::utils::log::i(line);
  } else {
    renodx::utils::log::w(line);
  }
}

// A resize makes the swap chain again and is no end.
inline void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize) {
  if (resize) return;
  Log(swapchain->get_device(), "swap chain");
}

}  // namespace twattila::device_end
