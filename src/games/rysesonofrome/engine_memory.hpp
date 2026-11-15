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
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The game's memory: CryEngine int console variables and small code patches. The exe's code is decrypted at
// start (Steam DRM), so search only after the game runs.
namespace rysesonofrome::engine_memory {

namespace internal {

struct Section {
  const uint8_t* begin;
  const uint8_t* end;
  DWORD characteristics;
};

// The exe's sections that are committed and readable from start to end (the DRM's may not be).
inline std::vector<Section> ReadableSections() {
  const auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  constexpr DWORD READABLE = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                             | PAGE_EXECUTE_WRITECOPY;
  std::vector<Section> sections;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    const Section range = {
        .begin = base + section->VirtualAddress,
        .end = base + section->VirtualAddress + (section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData),
        .characteristics = section->Characteristics,
    };
    bool readable = true;
    for (const uint8_t* at = range.begin; readable && at < range.end;) {
      MEMORY_BASIC_INFORMATION info = {};
      readable = VirtualQuery(at, &info, sizeof(info)) != 0 && info.State == MEM_COMMIT && (info.Protect & READABLE) != 0
                 && (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
      at = static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize;
    }
    if (readable) sections.push_back(range);
  }
  return sections;
}

}  // namespace internal

struct IntCvar {
  int* value = nullptr;
  std::string details;  // what was found where, or what was seen; for the log
};

// REGISTER_CVAR2(name, &static_int, ...): the code that loads the name (lea rdx) loads the static's address
// (lea r8) within a few instructions. The renderer reads the static every frame, so writing it sets the cvar.
inline IntCvar FindIntCvar(std::string_view name) {
  const auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  std::vector<internal::Section> code;
  std::vector<internal::Section> readonly;
  std::vector<internal::Section> writable;
  for (const auto& section : internal::ReadableSections()) {
    if ((section.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
      code.push_back(section);
    } else if ((section.characteristics & IMAGE_SCN_MEM_WRITE) != 0) {
      writable.push_back(section);
    } else if ((section.characteristics & IMAGE_SCN_MEM_READ) != 0) {
      readonly.push_back(section);
    }
  }
  const auto inside = [](const std::vector<internal::Section>& ranges, const uint8_t* address, size_t size) {
    for (const auto& range : ranges) {
      if (address >= range.begin && address + size <= range.end) return true;
    }
    return false;
  };

  // The name with a terminator, preceded by one (not the tail of a longer name).
  const uint8_t* text = nullptr;
  for (const auto& range : readonly) {
    for (const uint8_t* at = range.begin + 1; at + name.size() < range.end; ++at) {
      if (at[-1] == 0 && at[name.size()] == 0 && std::memcmp(at, name.data(), name.size()) == 0) {
        text = at;
        break;
      }
    }
    if (text != nullptr) break;
  }
  if (text == nullptr) return {.details = std::format("{}: name not found", name)};

  // lea r64, [rip + disp32] with the given REX prefix and ModRM (mod 00, rm 101).
  const auto lea_target = [](const uint8_t* at, uint8_t rex, uint8_t modrm) -> const uint8_t* {
    if (at[0] != rex || at[1] != 0x8D || at[2] != modrm) return nullptr;
    int32_t displacement;
    std::memcpy(&displacement, at + 3, sizeof(displacement));
    return at + 7 + displacement;
  };
  // ICVar* IConsole::Register(const char* name, int* src, int default, ...): this in rcx, the name in rdx
  // (lea rdx = 48 8D 15), the static's address in r8 (lea r8 = 4C 8D 05), within a few instructions.
  std::string seen;
  uint32_t references = 0;
  for (const auto& range : code) {
    for (const uint8_t* at = range.begin; at + 7 <= range.end; ++at) {
      if (lea_target(at, 0x48, 0x15) != text) continue;
      ++references;
      for (int32_t distance = 7; distance <= 64; ++distance) {
        for (const uint8_t* next : {at + distance, at - distance}) {
          if (next < range.begin || next + 7 > range.end) continue;
          const uint8_t* target = lea_target(next, 0x4C, 0x05);
          if (target == nullptr) continue;
          int value = -1;
          const bool usable = reinterpret_cast<uintptr_t>(target) % alignof(int) == 0 && inside(writable, target, sizeof(int));
          if (usable) std::memcpy(&value, target, sizeof(value));
          if (!usable || value < 0 || value > 64) {
            seen += std::format(" [code +{:#x}: r8 +{:#x}{}]", at - base, target - base,
                                (usable ? std::format(" = {}", value) : std::string(" not data")));
            continue;
          }
          return {
              .value = reinterpret_cast<int*>(const_cast<uint8_t*>(target)),
              .details = std::format("{} at +{:#x} (code +{:#x}), value {}", name, target - base, at - base, value),
          };
        }
      }
      // For the log: the bytes around a reference that did not match.
      if (at - 32 >= range.begin && at + 48 <= range.end) {
        seen += std::format(" [code +{:#x}:", at - base);
        for (const uint8_t* byte = at - 32; byte < at + 48; ++byte) seen += std::format(" {:02X}", *byte);
        seen += "]";
      }
    }
  }
  return {.details = std::format("{}: name at +{:#x}, {} code references, no int found{}", name, text - base, references, seen)};
}

// A few code bytes, replaced while applied.
struct CodePatch {
  uint8_t* address = nullptr;  // null: none
  std::vector<uint8_t> original;
  std::vector<uint8_t> replacement;
  bool applied = false;
  std::string details;  // where, or why not; for the log
};

// The unique match of `pattern` (hex bytes, "??" for any, e.g. rip displacements) in the exe's code, or null.
// `details`: where, or how many matches there were.
inline uint8_t* FindPattern(std::string_view pattern, std::string* details) {
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
  if (anchor_length == 0) {
    *details = "invalid pattern";
    return nullptr;
  }
  std::vector<uint8_t> fixed;
  for (size_t at = anchor; at < anchor + anchor_length; ++at) fixed.push_back(static_cast<uint8_t>(bytes[at]));
  const std::boyer_moore_horspool_searcher searcher(fixed.begin(), fixed.end());
  const auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  uint8_t* found = nullptr;
  uint32_t matches = 0;
  for (const auto& section : internal::ReadableSections()) {
    if ((section.characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
    for (auto hit = std::search(section.begin, section.end, searcher); hit != section.end;
         hit = std::search(hit + 1, section.end, searcher)) {
      const uint8_t* start = hit - anchor;
      if (start < section.begin || start + bytes.size() > section.end) continue;
      bool match = true;
      for (size_t at = 0; match && at < bytes.size(); ++at) match = bytes[at] < 0 || start[at] == bytes[at];
      if (!match) continue;
      ++matches;
      found = const_cast<uint8_t*>(start);
    }
  }
  if (matches != 1) {
    *details = std::format("{} matches", matches);
    return nullptr;
  }
  *details = std::format("at +{:#x}", found - base);
  return found;
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

// Executable memory within 1 GB of the exe, reachable by rel32 jumps from its code. Never freed: a thread may
// still run in it after a patch is undone.
inline uint8_t* AllocateNearExe(size_t size) {
  const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  SYSTEM_INFO system = {};
  GetSystemInfo(&system);
  const uintptr_t granularity = system.dwAllocationGranularity;
  const uintptr_t above = (base + nt->OptionalHeader.SizeOfImage + granularity - 1) & ~(granularity - 1);
  const uintptr_t below = base & ~(granularity - 1);
  for (uintptr_t distance = 0; distance < (uintptr_t{1} << 30); distance += granularity) {
    for (const uintptr_t candidate : {above + distance, below - granularity - distance}) {
      if (candidate > below + (uintptr_t{1} << 31)) continue;  // wrapped below 0
      if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) {
        return static_cast<uint8_t*>(memory);
      }
    }
  }
  return nullptr;
}

// Debug: the game's image as loaded (code decrypted), each readable section at its RVA, for offline analysis.
// Returns what was written, for the log.
inline std::string SaveImage(const std::filesystem::path& path) {
  const auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return std::format("could not create {}", path.string());
  file.write(reinterpret_cast<const char*>(base), nt->OptionalHeader.SizeOfHeaders);
  uint32_t written = 0;
  for (const auto& section : internal::ReadableSections()) {
    file.seekp(section.begin - base);
    file.write(reinterpret_cast<const char*>(section.begin), section.end - section.begin);
    ++written;
  }
  // Zeros up to the image's size, so file offsets are RVAs.
  file.seekp(nt->OptionalHeader.SizeOfImage - 1);
  file.put(0);
  if (!file) return std::format("writing {} failed", path.string());
  return std::format("{} ({} sections, base {:#x}, {} MB)", path.string(), written, reinterpret_cast<uintptr_t>(base),
                     nt->OptionalHeader.SizeOfImage >> 20);
}

}  // namespace rysesonofrome::engine_memory
