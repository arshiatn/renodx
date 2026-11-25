/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

namespace twattila {

// A module's file version from its version resource (nullptr: the process's exe). Kernel32 only, so the 64-bit Attila
// plugin can use it too: the resource's fixed part is found by its signature instead of version.lib's query.
inline std::optional<std::array<uint16_t, 4>> ModuleFileVersion(HMODULE module) {
  const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(VS_VERSION_INFO), MAKEINTRESOURCEW(16));  // RT_VERSION
  if (resource == nullptr) return std::nullopt;
  const HGLOBAL loaded = LoadResource(module, resource);
  if (loaded == nullptr) return std::nullopt;
  const auto* bytes = static_cast<const uint8_t*>(LockResource(loaded));
  if (bytes == nullptr) return std::nullopt;
  const DWORD size = SizeofResource(module, resource);
  for (DWORD offset = 0; offset + sizeof(VS_FIXEDFILEINFO) <= size; offset += 4) {
    VS_FIXEDFILEINFO info = {};
    std::memcpy(&info, bytes + offset, sizeof(info));
    if (info.dwSignature != VS_FFI_SIGNATURE) continue;
    return std::array<uint16_t, 4>{HIWORD(info.dwFileVersionMS), LOWORD(info.dwFileVersionMS),
                                   HIWORD(info.dwFileVersionLS), LOWORD(info.dwFileVersionLS)};
  }
  return std::nullopt;
}

}  // namespace twattila
