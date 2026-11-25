/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// renodx_attila64.dll: a plugin of the 64-bit Attila loader (it runs the game's Linux build on Windows) that hooks two
// game functions for the Attila add-on (plugin_block.h). The loader loads it from its plugins folder, after its own
// patches and before any game code runs; the add-on finds its block once ReShade loads it. Built by build.ps1.

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "../module_version.hpp"
#include "hooks.hpp"
#include "loader_api.h"
#include "patcher.hpp"
#include "plugin_block.h"
#include "sites.h"

namespace {

using namespace twattila::attila64_plugin;

inline constexpr char PLUGIN_VERSION[] = "1";
inline constexpr std::array<const char*, ROW_COUNT> ROW_WORDS = {"bone flush", "pass draw"};
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

}  // namespace

extern "C" __declspec(dllexport) int attila64_plugin_init(const LoaderApi* api) {
  loader = api;
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

  // <exe dir>\ReShade.ini; without a path every key keeps its default.
  wchar_t ini[PATH_CAPACITY];
  bool has_ini = false;
  if (const DWORD exe_length = GetModuleFileNameW(nullptr, ini, PATH_CAPACITY);
      exe_length != 0 && exe_length != PATH_CAPACITY) {
    if (wchar_t* exe_name = std::wcsrchr(ini, L'\\');
        exe_name != nullptr && static_cast<size_t>(exe_name + 1 - ini) + std::size(INI_FILE) <= PATH_CAPACITY) {
      std::wmemcpy(exe_name + 1, INI_FILE, std::size(INI_FILE));
      has_ini = true;
    }
  }
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
      "renodx_attila64 %s: game build ok, loader api %u (%s) / exe %u.%u.%u.%u, game end %p, perffix64 %s, "
      "other plugins: %s; changes drawing only (multiplayer peers may differ)",
      PLUGIN_VERSION, api->api_version, api->engine_version, exe_version[0], exe_version[1], exe_version[2],
      exe_version[3], reinterpret_cast<void*>(api->game_hi), (perffix ? "loaded" : "off"), other_plugins);

  for (size_t index = 0; index < SITES.size(); ++index) {
    const Site& site = SITES[index];
    const auto row = static_cast<Row>(site.row);
    SiteCheck check = {.refusal = REFUSAL_OFF};
    if (ini_int(ROW_INI_KEYS[row], ROW_INSTALL_DEFAULT) != 0) {
      check = CheckSite(site, FOREIGN);
    }
    if (check.refusal == REFUSAL_NONE) {
      check.refusal = ApplySite(*api, site, REPLACEMENTS[row], &applied_sites[index]);
    }
    if (check.refusal == REFUSAL_NONE) {
      originals[row] = applied_sites[index].original;
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
  char rows[128] = {};
  size_t rows_length = 0;
  for (uint32_t row = 0; row < ROW_COUNT && rows_length < sizeof(rows); ++row) {
    const bool on = (block.rows_applied & RowBit(static_cast<Row>(row))) != 0;
    const int written = std::snprintf(rows + rows_length, sizeof(rows) - rows_length, "%s%s %s",
                                      ((row == 0) ? "" : ", "), ROW_WORDS[row], (on ? "on" : "off"));
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
}

extern "C" __declspec(dllexport) Block* renodx_attila64_block() { return &block; }
