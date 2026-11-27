/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "loader_api.h"
#include "plugin_block.h"
#include "tweaks.h"

// The game's engine values (tweaks.h) at the game's start: each object checked against what its static constructor
// built, the restart-only ones written from their keys, and what decides those writes: the graphics route of this start.
// Also the loader's frame cap, which the loader reads once from its C library's environment on the first frame.
namespace twattila::attila64_plugin::tweaks {

inline constexpr char FRAME_CAP_VARIABLE[] = "ATTILA_FPS_CAP";
inline constexpr char FRAME_CAP_OFF[] = "ATTILA_FPS_CAP=0";
inline constexpr char ROUTE_VARIABLE[] = "ATTILA_DX11";
inline constexpr wchar_t ROUTE_NOTE_FILE[] = L"dx11_starting.txt";
// FAT32 and exFAT store write times in 2 s steps (FILETIME counts 100 ns).
inline constexpr uint64_t ROUTE_NOTE_SLACK = 2ull * 10'000'000;

// How this start draws, by the loader's own rule: its test switch, else the note it writes beside the exe on the
// DirectX 11 route only, before any plugin loads (it removes it once the game has drawn for a while). The add-on loads
// only under DirectX 11, so its restart-only values apply only there.
enum Route : uint8_t {
  ROUTE_DX11_SWITCH,  // ATTILA_DX11=1
  ROUTE_DX11_NOTE,    // the note, written by this start
  ROUTE_NO_NOTE,      // not DirectX 11: OpenGL (chosen, or the loader's fallback) or DirectX 12
  ROUTE_OLD_NOTE,     // not DirectX 11: the note is from an earlier start that never got far enough to remove it
  ROUTE_COUNT
};
inline constexpr std::array<const char*, ROUTE_COUNT> ROUTE_WORDS = {
    "DX11 (ATTILA_DX11=1)", "DX11 (dx11_starting.txt from this start)", "not DX11 (no dx11_starting.txt)",
    "not DX11 (dx11_starting.txt from an earlier start)"};

inline constexpr std::array<const char*, 5> VERDICT_WORDS = {"ok", "absent", "another type", "another name",
                                                             "not the game's default"};
inline constexpr std::array<const char*, 4> RESTART_WORDS = {"", "written (restart-only)",
                                                             "out of range, the game's value kept",
                                                             "not written (not DX11)"};

// A restart-only key's text: exactly 8 hex digits (the bits; a sign bit and hex do not fit an integer key read).
[[nodiscard]] inline bool ParseKeyBits(const wchar_t* text, uint32_t* bits) noexcept {
  uint32_t value = 0;
  for (size_t index = 0; index < 8; ++index) {
    const wchar_t digit = text[index];
    uint32_t nibble = 0;
    if (digit >= L'0' && digit <= L'9') {
      nibble = digit - L'0';
    } else if (digit >= L'a' && digit <= L'f') {
      nibble = digit - L'a' + 10;
    } else if (digit >= L'A' && digit <= L'F') {
      nibble = digit - L'A' + 10;
    } else {
      return false;
    }
    value = (value << 4) | nibble;
  }
  if (text[8] != L'\0') return false;
  *bits = value;
  return true;
}

[[nodiscard]] inline Route FindRoute(const wchar_t* note_path) noexcept {
  char test_switch[4];
  if (GetEnvironmentVariableA(ROUTE_VARIABLE, test_switch, sizeof(test_switch)) == 1 && test_switch[0] == '1') {
    return ROUTE_DX11_SWITCH;
  }
  WIN32_FILE_ATTRIBUTE_DATA note;
  if (GetFileAttributesExW(note_path, GetFileExInfoStandard, &note) == FALSE) return ROUTE_NO_NOTE;
  FILETIME created = {};
  FILETIME exited = {};
  FILETIME kernel = {};
  FILETIME user = {};
  GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
  const auto ticks = [](const FILETIME& time) { return (uint64_t{time.dwHighDateTime} << 32) | time.dwLowDateTime; };
  // An earlier start's note is older than this process: that start had to load and fail first.
  return (ticks(note.ftLastWriteTime) + ROUTE_NOTE_SLACK >= ticks(created)) ? ROUTE_DX11_NOTE : ROUTE_OLD_NOTE;
}

// One entry's object as the game's static constructors left it: its type's table, its name and (unless the game
// stores the value itself) its default; a flag the loader stores holds 0 or 1. Only an OK slot gets its value address.
inline void CheckTweak(const TweakEntry& entry, TweakSlot* slot) noexcept {
  *slot = {.name = entry.name, .type = entry.type, .restart_only = static_cast<uint8_t>(entry.restart_only)};
  if (entry.object == 0) return;
  const TweakKind& kind = TWEAK_KINDS[entry.type];
  const auto* object = reinterpret_cast<const uint8_t*>(uintptr_t{entry.object});
  uint64_t table = 0;
  std::memcpy(&table, object, sizeof(table));
  if (table != kind.table) {
    slot->verdict = VERDICT_WRONG_TYPE;
    return;
  }
  uint32_t name_length = 0;
  const char16_t* name_units = nullptr;
  std::memcpy(&name_length, object + TWEAK_NAME_LENGTH_OFFSET, sizeof(name_length));
  std::memcpy(&name_units, object + TWEAK_NAME_UNITS_OFFSET, sizeof(name_units));
  const std::string_view name = entry.name;
  bool same_name = (name_length == name.size() && name_units != nullptr);
  for (size_t index = 0; same_name && index < name.size(); ++index) {
    same_name = (name_units[index] == static_cast<char16_t>(name[index]));
  }
  if (!same_name) {
    slot->verdict = VERDICT_WRONG_NAME;
    return;
  }
  uint32_t value = 0;
  std::memcpy(&value, object + kind.value_offset, kind.value_bytes);
  slot->default_bits = value;
  slot->written_bits = value;
  const bool changed = (entry.loader_writes ? (value > 1) : (!entry.game_writes && value != entry.default_bits));
  if (changed) {
    slot->verdict = VERDICT_CHANGED;
    return;
  }
  slot->value_address = uint64_t{entry.object} + kind.value_offset;
  slot->verdict = VERDICT_OK;
}

// A restart-only value's key on an OK slot: stored when the bits are in the entry's range and the game draws with
// DirectX 11. Floats compare as floats, so a NaN is out of range.
inline RestartState WriteRestartValue(const TweakEntry& entry, uint32_t bits, bool dx11, TweakSlot* slot) noexcept {
  bool in_range = false;
  switch (entry.type) {
    case TWEAK_FLOAT:
      in_range = (std::bit_cast<float>(entry.low_bits) <= std::bit_cast<float>(bits)
                  && std::bit_cast<float>(bits) <= std::bit_cast<float>(entry.high_bits));
      break;
    case TWEAK_INT:
      in_range = (static_cast<int32_t>(entry.low_bits) <= static_cast<int32_t>(bits)
                  && static_cast<int32_t>(bits) <= static_cast<int32_t>(entry.high_bits));
      break;
    default:
      in_range = (entry.low_bits <= bits && bits <= entry.high_bits);
      break;
  }
  if (!in_range) {
    slot->restart_state = RESTART_OUT_OF_RANGE;
  } else if (!dx11) {
    slot->restart_state = RESTART_NOT_DX11;
  } else {
    std::memcpy(reinterpret_cast<void*>(slot->value_address), &bits, TWEAK_KINDS[entry.type].value_bytes);
    slot->written_bits = bits;
    slot->restart_state = RESTART_APPLIED;
  }
  return static_cast<RestartState>(slot->restart_state);
}

// The module an image's import of `function` is bound to (read from the import table's own slot); nullptr when the
// image imports no function of that name.
[[nodiscard]] inline HMODULE ImportedFrom(HMODULE image, const char* function) noexcept {
  const auto* base = reinterpret_cast<const uint8_t*>(image);
  const auto* headers =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const IMAGE_DATA_DIRECTORY& imports = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (imports.VirtualAddress == 0) return nullptr;
  for (const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
       descriptor->Name != 0; ++descriptor) {
    if (descriptor->OriginalFirstThunk == 0) continue;  // bound without its names
    const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
    const auto* slots = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
    for (size_t index = 0; names[index].u1.AddressOfData != 0; ++index) {
      if (IMAGE_SNAP_BY_ORDINAL(names[index].u1.Ordinal)) continue;
      const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[index].u1.AddressOfData);
      if (std::strcmp(by_name->Name, function) != 0) continue;
      HMODULE module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(slots[index].u1.Function), &module);
      return module;
    }
  }
  return nullptr;
}

// The loader's frame cap for this start, with its one log line. A value the player set stays. Off is written twice:
// into the process environment, and through the _putenv of the C library the loader's getenv comes from (`c_library`,
// ImportedFrom the loader exe), because that library reads its own copy; this plugin's own C library is a third copy.
// The value is then read back through that getenv, the call the loader makes.
inline void SetLoaderFrameCap(bool keep_cap, HMODULE c_library, const LoaderApi& api) noexcept {
  if (GetEnvironmentVariableA(FRAME_CAP_VARIABLE, nullptr, 0) != 0) {
    char value[64] = {};
    if (GetEnvironmentVariableA(FRAME_CAP_VARIABLE, value, sizeof(value)) >= sizeof(value)) {
      api.log("renodx_attila64: loader frame cap: the player's ATTILA_FPS_CAP (too long to show) kept");
    } else {
      api.log("renodx_attila64: loader frame cap: the player's ATTILA_FPS_CAP=%s kept", value);
    }
    return;
  }
  if (keep_cap) {
    api.log("renodx_attila64: loader frame cap: on (the loader's 240)");
    return;
  }
  using PutEnvironment = int (*)(const char*);
  using GetEnvironment = char* (*)(const char*);
  const auto put =
      (c_library != nullptr) ? reinterpret_cast<PutEnvironment>(GetProcAddress(c_library, "_putenv")) : nullptr;
  const auto get =
      (c_library != nullptr) ? reinterpret_cast<GetEnvironment>(GetProcAddress(c_library, "getenv")) : nullptr;
  if (put == nullptr || get == nullptr) {
    api.log("renodx_attila64: loader frame cap: left on (the loader's C library not found)");
    return;
  }
  SetEnvironmentVariableA(FRAME_CAP_VARIABLE, "0");
  put(FRAME_CAP_OFF);
  if (const char* read_back = get(FRAME_CAP_VARIABLE); read_back != nullptr && std::strcmp(read_back, "0") == 0) {
    api.log("renodx_attila64: loader frame cap: off (ATTILA_FPS_CAP=0, read back by the loader's getenv)");
  } else {
    api.log("renodx_attila64: loader frame cap: left on (the loader's getenv did not read ATTILA_FPS_CAP=0 back)");
  }
}

}  // namespace twattila::attila64_plugin::tweaks
