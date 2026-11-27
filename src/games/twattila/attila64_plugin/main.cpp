/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// renodx_attila64.dll: a plugin of the 64-bit Attila loader (it runs the game's Linux build on Windows) that hooks game
// functions: two for the Attila add-on (plugin_block.h) and the battle step's task, to keep a CPU core free for it and
// one for the render thread (battle_core.hpp; that row works without the add-on). It also checks the game's engine
// values for the add-on, writes the restart-only ones, and sets the loader's frame cap (tweaks.hpp). The loader loads
// it from its plugins folder, after its own patches and before any game code runs; the add-on finds its block once
// ReShade loads it. Built by build.ps1.

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string_view>

#include "../module_version.hpp"
#include "battle_core.hpp"
#include "hooks.hpp"
#include "loader_api.h"
#include "patcher.hpp"
#include "plugin_block.h"
#include "sites.h"
#include "tweaks.h"
#include "tweaks.hpp"

namespace {

using namespace twattila::attila64_plugin;

inline constexpr char PLUGIN_VERSION[] = "2";
inline constexpr std::array<const char*, ROW_COUNT> ROW_WORDS = {
    "bone flush", "pass draw", "battle core", "unit lod",       "model detail",
    "shadow map", "grass",     "grass cells", "campaign trees", "sprites"};
static_assert(std::ranges::find(ROW_WORDS, nullptr) == ROW_WORDS.end(), "a word for every row");
// The rows with a site in this build; the rows line shows the others as "-".
inline constexpr uint32_t ROWS_WITH_SITES = [] {
  uint32_t rows = 0;
  for (const Site& site : SITES) {
    rows |= RowBit(static_cast<Row>(site.row));
  }
  return rows;
}();
inline constexpr size_t ROWS_LINE_CAPACITY = 256;
static_assert([] {
  size_t length = 1;  // the terminating zero
  for (const char* word : ROW_WORDS) {
    length += std::string_view(word).size() + std::string_view(", ").size() + std::string_view(" off").size();
  }
  return length <= ROWS_LINE_CAPACITY;
}(), "the rows line holds every row");
inline constexpr DWORD PATH_CAPACITY = 1024;
inline constexpr wchar_t INI_FILE[] = L"ReShade.ini";
// MODULE_NAME for the loader's narrow file names (ASCII).
inline constexpr auto MODULE_NAME_NARROW = [] {
  std::array<char, std::size(MODULE_NAME)> name = {};
  for (size_t index = 0; index < name.size(); ++index) {
    name[index] = static_cast<char>(MODULE_NAME[index]);
  }
  return name;
}();

std::array<AppliedSite, SITES.size()> applied_sites = {};

// A restart-only value's key in [Attila64Plugin.Values], read at init for the game's start.
struct RestartKey {
  bool present = false;
  bool eight_hex_digits = false;
  uint32_t bits = 0;
};
std::array<RestartKey, TWEAKS.size()> restart_keys = {};
std::array<TweakSlot, TWEAKS.size()> tweak_slots = {};
bool dx11_route = false;

}  // namespace

extern "C" __declspec(dllexport) int attila64_plugin_init(const LoaderApi* api) {
  loader = api;
  // The add-on stores only the scales it changes.
  for (size_t scale = 0; scale < SCALE_COUNT; ++scale) {
    block.scales[scale].store(NEUTRAL_SCALES[scale], std::memory_order_relaxed);
  }
  const auto refuse = [](Status status, const char* why) {
    loader->log("renodx_attila64: not used: %s", why);
    block.status.store(status, std::memory_order_release);
    return 1;
  };
  // The loader keeps a plugin loaded for the process; pinned so that no FreeLibrary can drop the hooked code.
  HMODULE module = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                         reinterpret_cast<LPCWSTR>(&attila64_plugin_init), &module)
      == FALSE) {
    return refuse(STATUS_WRONG_LOADER, "the plugin's module could not be pinned");
  }

  switch (CheckIdentity(*api)) {
    case STATUS_WRONG_LOADER: return refuse(STATUS_WRONG_LOADER, "loader interface or game range not as written for");
    case STATUS_WRONG_GAME:   return refuse(STATUS_WRONG_GAME, "another game build");
    default:                  break;
  }
  // The loader loads plugins in file-name order and another plugin may check the game's code at its own init: a renamed
  // file would come before it and change bytes it expects.
  wchar_t path[PATH_CAPACITY];
  const DWORD path_length = GetModuleFileNameW(module, path, PATH_CAPACITY);
  if (path_length == 0 || path_length == PATH_CAPACITY) {
    return refuse(STATUS_WRONG_LOADER, "the plugin's file path could not be read");
  }
  if (const wchar_t* file_name = std::wcsrchr(path, L'\\');
      file_name == nullptr || lstrcmpiW(file_name + 1, MODULE_NAME) != 0) {
    return refuse(STATUS_WRONG_LOADER, "the file was renamed (the loader's plugin order needs the shipped name)");
  }

  // <exe dir>\<file>; false when the path does not fit.
  const auto beside_exe = [](std::wstring_view file, wchar_t* file_path) {
    const DWORD exe_length = GetModuleFileNameW(nullptr, file_path, PATH_CAPACITY);
    if (exe_length == 0 || exe_length == PATH_CAPACITY) return false;
    wchar_t* exe_name = std::wcsrchr(file_path, L'\\');
    if (exe_name == nullptr || static_cast<size_t>(exe_name + 1 - file_path) + file.size() + 1 > PATH_CAPACITY) {
      return false;
    }
    std::wmemcpy(exe_name + 1, file.data(), file.size());
    exe_name[1 + file.size()] = L'\0';
    return true;
  };
  // Without a path every key keeps its default.
  wchar_t ini[PATH_CAPACITY];
  const bool has_ini = beside_exe(INI_FILE, ini);
  const auto ini_int = [&](const wchar_t* key, int default_value) {
    if (!has_ini) return default_value;
    return static_cast<int>(GetPrivateProfileIntW(INI_SECTION, key, default_value, ini));
  };
  if (ini_int(INI_ENABLED, ENABLED_DEFAULT) == 0) {
    return refuse(STATUS_OFF, "[Attila64Plugin] Enabled=0 in ReShade.ini");
  }

  // Logged only: the loader exe's file version, PerfFix (his module, or his mutex when he is loaded under another name),
  // and the plugins folder's other DLLs (fixed buffers: nothing here may throw into the loader).
  const auto exe_version = twattila::ModuleFileVersion(nullptr).value_or(std::array<uint16_t, 4>{});

  bool perffix = GetModuleHandleW(L"perffix64.dll") != nullptr;
  if (!perffix) {
    wchar_t mutex_name[64];
    std::swprintf(mutex_name, std::size(mutex_name), L"Local\\AttilaEngineFix_%lu", GetCurrentProcessId());
    if (HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, mutex_name); mutex != nullptr) {
      perffix = true;
      CloseHandle(mutex);
    }
  }

  // Without a path for the loader's note the route counts as not DirectX 11: nothing restart-only is written.
  wchar_t route_note[PATH_CAPACITY];
  const tweaks::Route route =
      (beside_exe(tweaks::ROUTE_NOTE_FILE, route_note) ? tweaks::FindRoute(route_note) : tweaks::ROUTE_NO_NOTE);
  dx11_route = (route == tweaks::ROUTE_DX11_SWITCH || route == tweaks::ROUTE_DX11_NOTE);

  char other_plugins[512] = "none";
  size_t other_length = 0;
  char pattern[PATH_CAPACITY];
  std::snprintf(pattern, sizeof(pattern), "%s\\*.dll", api->plugin_dir);
  WIN32_FIND_DATAA found;
  if (HANDLE search = FindFirstFileA(pattern, &found); search != INVALID_HANDLE_VALUE) {
    do {
      if (lstrcmpiA(found.cFileName, MODULE_NAME_NARROW.data()) == 0) continue;
      if (other_length >= sizeof(other_plugins)) continue;
      const int written = std::snprintf(other_plugins + other_length, sizeof(other_plugins) - other_length, "%s%s",
                                        ((other_length == 0) ? "" : ", "), found.cFileName);
      other_length += std::max(written, 0);
    } while (FindNextFileA(search, &found));
    FindClose(search);
  }

  loader->log(
      "renodx_attila64 %s: game build ok, loader api %u (%s) / exe %u.%u.%u.%u, game end %p, graphics %s, "
      "perffix64 %s, other plugins: %s. Changes drawing, sound memory and CPU core use only; battles, replays and "
      "multiplayer stay the same.",
      PLUGIN_VERSION, api->api_version, api->engine_version, exe_version[0], exe_version[1], exe_version[2],
      exe_version[3], reinterpret_cast<void*>(api->game_hi), tweaks::ROUTE_WORDS[route], (perffix ? "loaded" : "off"),
      other_plugins);

  // The restart-only keys the add-on wrote for this start. The loader reads its frame cap on the first frame, after
  // this init.
  const int sol_frame_cap =
      (has_ini ? static_cast<int>(GetPrivateProfileIntW(INI_VALUES_SECTION, INI_SOL_FRAME_CAP, 0, ini)) : 0);
  const HMODULE loader_c_library = tweaks::ImportedFrom(GetModuleHandleW(nullptr), "getenv");
  tweaks::SetLoaderFrameCap(sol_frame_cap == 1, loader_c_library, *api);
  if (ini_int(INI_TWEAKS, TWEAKS_DEFAULT) != 0) {
    for (size_t index = 0; index < TWEAKS.size(); ++index) {
      if (!has_ini || !TWEAKS[index].restart_only) continue;
      wchar_t key[TWEAK_NAME_CAPACITY] = {};
      std::ranges::copy(std::string_view(TWEAKS[index].name), key);
      wchar_t text[16];
      if (GetPrivateProfileStringW(INI_VALUES_SECTION, key, L"", text, static_cast<DWORD>(std::size(text)), ini) == 0) {
        continue;
      }
      restart_keys[index].present = true;
      restart_keys[index].eight_hex_digits = tweaks::ParseKeyBits(text, &restart_keys[index].bits);
    }
    block.tweaks_status.store(TWEAKS_PENDING, std::memory_order_relaxed);
  }

  for (size_t index = 0; index < SITES.size(); ++index) {
    const Site& site = SITES[index];
    const auto row = static_cast<Row>(site.row);
    SiteCheck check = {.refusal = REFUSAL_OFF};
    if (ini_int(ROW_INI_KEYS[row], ROW_INSTALL_DEFAULT) != 0) {
      block.rows_tried |= RowBit(row);
      check = CheckSite(site, FOREIGN);
    }
    if (check.refusal == REFUSAL_NONE) {
      check.refusal = ApplySite(*api, site, REPLACEMENTS[site.label], &applied_sites[index]);
    }
    if (check.refusal == REFUSAL_NONE) {
      originals[site.label] = applied_sites[index].original;
      block.rows_applied |= RowBit(row);
      loader->log("renodx_attila64: site %s %#x: applied", LABEL_WORDS[site.label], site.address);
    } else {
      block.rows_refused |= RowBit(row);
      const bool foreign = (check.refusal == REFUSAL_FOREIGN);
      loader->log("renodx_attila64: site %s %#x: skipped (%s%s%s)", LABEL_WORDS[site.label], site.address,
                  REFUSAL_WORDS[check.refusal], (foreign ? " " : ""), (foreign ? OWNER_WORDS[check.owner] : ""));
    }
  }
  // A row is on only when all of its sites are.
  block.rows_applied &= ~block.rows_refused;
  // The battle core's status is built (not 0) only with its hook in; battle_core.hpp writes it from the first step on.
  // The render core's the same, with its switch.
  if ((block.rows_applied & RowBit(ROW_CPU_PLACEMENT)) != 0) {
    block.core_status.store(uint64_t{CORE_WAITING} << CORE_STATE_SHIFT, std::memory_order_relaxed);
    battle_core::ApplyRenderCoreKey(&block, ini_int(INI_RENDER_CORE, -1));
  }
  char rows[ROWS_LINE_CAPACITY] = {};
  size_t rows_length = 0;
  for (uint32_t row = 0; row < ROW_COUNT && rows_length < sizeof(rows); ++row) {
    const uint32_t bit = RowBit(static_cast<Row>(row));
    const bool on = (block.rows_applied & bit) != 0;
    const char* state = (((ROWS_WITH_SITES & bit) != 0) ? (on ? "on" : "off") : "-");
    const int written = std::snprintf(rows + rows_length, sizeof(rows) - rows_length, "%s%s %s",
                                      ((row == 0) ? "" : ", "), ROW_WORDS[row], state);
    rows_length += std::max(written, 0);
  }
  loader->log("renodx_attila64: rows: %s", rows);
  block.status.store(STATUS_READY, std::memory_order_release);
  return 0;
}

// After the loader's late writes and before the game's main: our bytes must still be ours (nothing can be undone).
extern "C" __declspec(dllexport) void attila64_plugin_game_started(const LoaderApi* /*api*/) {
  if (block.status.load(std::memory_order_relaxed) != STATUS_READY) return;
  uint32_t unchanged = 0;
  for (size_t index = 0; index < SITES.size(); ++index) {
    const Site& site = SITES[index];
    if (applied_sites[index].original == nullptr) continue;
    if (SiteUnchanged(site, applied_sites[index])) {
      ++unchanged;
    } else {
      block.rows_changed.fetch_or(RowBit(static_cast<Row>(site.row)));
      loader->log("renodx_attila64: site %s changed after we patched it", LABEL_WORDS[site.label]);
    }
  }
  loader->log("renodx_attila64: game started: %u sites unchanged", unchanged);
  // The engine values, once: the game's static constructors have built them, and its sound start (the reader of the
  // restart-only ones) comes later in its main.
  if (block.tweaks_status.load(std::memory_order_relaxed) == TWEAKS_PENDING) {
    uint32_t checked = 0;
    uint32_t ok = 0;
    uint32_t restart_applied = 0;
    for (size_t index = 0; index < TWEAKS.size(); ++index) {
      const TweakEntry& entry = TWEAKS[index];
      TweakSlot* slot = &tweak_slots[index];
      tweaks::CheckTweak(entry, slot);
      if (slot->verdict == VERDICT_ABSENT) continue;
      ++checked;
      if (slot->verdict != VERDICT_OK) {
        loader->log("renodx_attila64: tweak %s: refused (%s, value 0x%x)", entry.name,
                    tweaks::VERDICT_WORDS[slot->verdict], slot->default_bits);
        continue;
      }
      ++ok;
      const RestartKey& key = restart_keys[index];
      if (!key.present) continue;
      if (!key.eight_hex_digits) {
        slot->restart_state = RESTART_OUT_OF_RANGE;
        loader->log("renodx_attila64: tweak %s: key not 8 hex digits, the game's value kept", entry.name);
        continue;
      }
      const RestartState state = tweaks::WriteRestartValue(entry, key.bits, dx11_route, slot);
      if (state == RESTART_APPLIED) {
        ++restart_applied;
      }
      loader->log("renodx_attila64: tweak %s: 0x%08x %s", entry.name, key.bits, tweaks::RESTART_WORDS[state]);
    }
    block.tweak_count = static_cast<uint32_t>(tweak_slots.size());
    block.tweaks = tweak_slots.data();
    loader->log("renodx_attila64: tweaks: %u checked, %u ok, %zu absent, restart-only: %u applied", checked, ok,
                TWEAKS.size() - checked, restart_applied);
    block.tweaks_status.store(TWEAKS_READY, std::memory_order_release);
  }
  // This thread goes on to run the game's main: the render thread.
  if ((block.rows_applied & RowBit(ROW_CPU_PLACEMENT)) != 0) {
    battle_core::SetRenderThread(&block, *loader);
  }
}

extern "C" __declspec(dllexport) Block* renodx_attila64_block() { return &block; }
