/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <cstdint>

namespace twattila::nvapi {

// NVIDIA's NVAPI function `id` (nvapi_interface.h), loaded by hand (NVAPI's SDK has no 32-bit library): null without
// NVIDIA's driver or when NvAPI_Initialize fails (the first call loads it, once).
inline void* Function(uint32_t id) {
  using Query = void*(__cdecl*)(uint32_t id);
  using Initialize = int(__cdecl*)();
  static const Query QUERY = [] {
    HMODULE module = LoadLibraryW(sizeof(void*) == 8 ? L"nvapi64.dll" : L"nvapi.dll");
    const auto query = (module != nullptr ? reinterpret_cast<Query>(GetProcAddress(module, "nvapi_QueryInterface")) : nullptr);
    const auto initialize = (query != nullptr ? reinterpret_cast<Initialize>(query(0x0150E828)) : nullptr);
    return (initialize != nullptr && initialize() == 0) ? query : nullptr;
  }();
  return (QUERY != nullptr ? QUERY(id) : nullptr);
}

}  // namespace twattila::nvapi
