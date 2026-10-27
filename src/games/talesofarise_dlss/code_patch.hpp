/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Small patches of the game's code (from rysesonofrome_dlss's engine_memory.hpp).
namespace talesofarise::code_patch {

// A few code bytes, replaced while applied.
struct CodePatch {
  uint8_t* address = nullptr;  // null: none
  std::vector<uint8_t> original;
  std::vector<uint8_t> replacement;
  bool applied = false;
  std::string details;  // where, or why not; for the log
};

// Whether [begin, end) is committed memory with one of the `protections` (and no guard / no-access page).
inline bool IsCommitted(const uint8_t* begin, const uint8_t* end, DWORD protections) {
  for (const uint8_t* at = begin; at < end;) {
    MEMORY_BASIC_INFORMATION info = {};
    if (VirtualQuery(at, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT || (info.Protect & protections) == 0
        || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
      return false;
    }
    at = static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize;
  }
  return true;
}

// Every match of `pattern` (hex bytes, "??" for any, e.g. displacements) in the exe's code sections that are
// committed and readable.
inline std::vector<uint8_t*> FindPatterns(std::string_view pattern) {
  std::vector<int16_t> bytes;  // -1: any
  for (size_t at = 0; at + 1 < pattern.size(); at += 3) {
    bytes.push_back((pattern[at] == '?') ? int16_t{-1} : static_cast<int16_t>(std::stoi(std::string(pattern.substr(at, 2)), nullptr, 16)));
  }
  // The longest run of fixed bytes, for a fast search; the rest is checked at each hit.
  size_t anchor = 0;
  size_t anchor_length = 0;
  for (size_t begin = 0; begin < bytes.size();) {
    size_t end = begin;
    while (end < bytes.size() && bytes[end] >= 0) ++end;
    if (end - begin > anchor_length) {
      anchor = begin;
      anchor_length = end - begin;
    }
    begin = end + 1;
  }
  if (anchor_length == 0) return {};
  std::vector<uint8_t> fixed;
  for (size_t at = anchor; at < anchor + anchor_length; ++at) fixed.push_back(static_cast<uint8_t>(bytes[at]));
  const std::boyer_moore_horspool_searcher searcher(fixed.begin(), fixed.end());
  const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  constexpr DWORD EXECUTABLE = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  std::vector<uint8_t*> matches;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
    const uint8_t* begin = base + section->VirtualAddress;
    const uint8_t* end = begin + (section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData);
    if (!IsCommitted(begin, end, EXECUTABLE)) continue;
    for (auto hit = std::search(begin, end, searcher); hit != end; hit = std::search(hit + 1, end, searcher)) {
      const uint8_t* start = hit - anchor;
      if (start < begin || start + bytes.size() > end) continue;
      bool match = true;
      for (size_t at = 0; match && at < bytes.size(); ++at) match = bytes[at] < 0 || start[at] == bytes[at];
      if (match) matches.push_back(const_cast<uint8_t*>(start));
    }
  }
  return matches;
}

// The unique match of `pattern`, or null. `details`: where, or how many matches there were.
inline uint8_t* FindPattern(std::string_view pattern, std::string* details) {
  const auto matches = FindPatterns(pattern);
  if (matches.size() != 1) {
    *details = std::format("{} matches", matches.size());
    return nullptr;
  }
  *details = std::format("at exe+0x{:X}", matches[0] - reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr)));
  return matches[0];
}

// Debug: the game's image as loaded (the exe's code is SteamStub-encrypted on disk, decrypted in memory), each
// readable section at its RVA, for offline analysis. Returns what was written, for the overlay.
inline std::string SaveImage(const std::filesystem::path& path) {
  const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return std::format("could not create {}", path.string());
  file.write(reinterpret_cast<const char*>(base), nt->OptionalHeader.SizeOfHeaders);
  constexpr DWORD READABLE = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                             | PAGE_EXECUTE_WRITECOPY;
  uint32_t written = 0;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    const uint8_t* begin = base + section->VirtualAddress;
    const uint8_t* end = begin + (section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData);
    if (!IsCommitted(begin, end, READABLE)) continue;
    file.seekp(section->VirtualAddress);
    file.write(reinterpret_cast<const char*>(begin), end - begin);
    ++written;
  }
  // Zeros up to the image's size, so file offsets are RVAs.
  file.seekp(nt->OptionalHeader.SizeOfImage - 1);
  file.put(0);
  if (!file) return std::format("writing {} failed", path.string());
  return std::format("Saved {} ({} sections, {} MB)", path.string(), written, nt->OptionalHeader.SizeOfImage >> 20);
}

// Writes the replacement (true) or the original bytes (false), after checking the other set is in place. Within
// one aligned 8-byte block it is a single atomic store: other threads see the old or the new instructions.
inline bool ApplyCodePatch(CodePatch* patch, bool apply) {
  if (patch->address == nullptr) return false;
  if (patch->applied == apply) return true;
  const auto& expected = (apply ? patch->original : patch->replacement);
  const auto& bytes = (apply ? patch->replacement : patch->original);
  if (std::memcmp(patch->address, expected.data(), expected.size()) != 0) {
    patch->details += "; changed by something else, left alone";
    patch->address = nullptr;
    return false;
  }
  const auto first = reinterpret_cast<uintptr_t>(patch->address);
  const auto block = first & ~uintptr_t{7};
  const bool atomic = first + bytes.size() <= block + 8;
  void* protect_begin = (atomic ? reinterpret_cast<void*>(block) : patch->address);
  const size_t protect_size = (atomic ? 8 : bytes.size());
  DWORD protection = 0;
  if (!VirtualProtect(protect_begin, protect_size, PAGE_EXECUTE_READWRITE, &protection)) return false;
  if (atomic) {
    uint64_t current;
    std::memcpy(&current, reinterpret_cast<const void*>(block), sizeof(current));
    uint64_t desired = current;
    std::memcpy(reinterpret_cast<uint8_t*>(&desired) + (first - block), bytes.data(), bytes.size());
    InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(block), static_cast<LONG64>(desired), static_cast<LONG64>(current));
  } else {
    std::memcpy(patch->address, bytes.data(), bytes.size());
  }
  VirtualProtect(protect_begin, protect_size, protection, &protection);
  FlushInstructionCache(GetCurrentProcess(), protect_begin, protect_size);
  patch->applied = apply;
  return true;
}

}  // namespace talesofarise::code_patch
