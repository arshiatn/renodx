/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// The game's engine module (empire.retail.x64.dll; Remastered CE runs its patched copy as CE.dll, the byte patterns
// unchanged): the code the addon hooks or patches is found by byte patterns.
namespace twshogun2::engine {

inline HMODULE Module() {
  HMODULE module = GetModuleHandleW(L"empire.retail.x64.dll");
  return module != nullptr ? module : GetModuleHandleW(L"CE.dll");
}

// The first match of a byte pattern ("48 8B ?? ...") in the engine's executable sections, or null.
inline uint8_t* FindPattern(std::string_view pattern) {
  HMODULE module = Module();
  if (module == nullptr) return nullptr;
  std::vector<int> bytes;
  for (size_t at = 0; at + 1 < pattern.size(); at += 3) {
    bytes.push_back(pattern[at] == '?' ? -1 : std::stoi(std::string(pattern.substr(at, 2)), nullptr, 16));
  }
  auto* base = reinterpret_cast<uint8_t*>(module);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
    uint8_t* begin = base + section->VirtualAddress;
    uint8_t* end = begin + section->Misc.VirtualSize - bytes.size();
    for (uint8_t* at = begin; at < end; ++at) {
      size_t matched = 0;
      while (matched < bytes.size() && (bytes[matched] < 0 || at[matched] == bytes[matched])) ++matched;
      if (matched == bytes.size()) return at;
    }
  }
  return nullptr;
}

// The target of an instruction's rip-relative displacement (the displacement at `at`, the instruction ending at `end`).
inline uint8_t* RipTarget(uint8_t* at, uint8_t* end) {
  int32_t displacement;
  std::memcpy(&displacement, at, sizeof(displacement));
  return end + displacement;
}

}  // namespace twshogun2::engine
