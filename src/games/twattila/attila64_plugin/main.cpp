/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// renodx_attila64.dll: a plugin of the 64-bit Attila loader (it runs the game's Linux build on Windows) that hooks game
// functions: two for the Attila add-on (plugin_block.h), the battle step's task, to keep a CPU core free for it and
// one for the render thread (battle_core.hpp; that row works without the add-on), the scale rows' (hooks.hpp; one
// of them inside a function, through the plugin's own trampoline: trampoline.hpp), and the battle step's pair
// resolver (battle_step.hpp; it works without the add-on too). It also checks the game's engine values for the
// add-on, writes the restart-only ones, sets the loader's frame cap (tweaks.hpp), writes the shadow map sizes the
// add-on's key asks for into the game's shadow setup, and puts hand-made stubs in front of game code
// (game_code.hpp: the same results, faster). The loader loads it from its plugins folder, after its own patches
// and before any game code runs; the add-on finds its block once ReShade loads it. Built by build.ps1.

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <span>
#include <string_view>

#include "../module_version.hpp"
#include "battle_core.hpp"
#include "battle_step.hpp"
#include "game_code.hpp"
#include "hooks.hpp"
#include "loader_api.h"
#include "patcher.hpp"
#include "plugin_block.h"
#include "sites.h"
#include "trampoline.hpp"
#include "tweaks.h"
#include "tweaks.hpp"

namespace {

using namespace twattila::attila64_plugin;

inline constexpr char PLUGIN_VERSION[] = "2";
// One array of fixed-size words, not pointers to literals: each word then is a whole string in the DLL file (a zero
// byte before and after), which the batch tool matches. A literal may be stored as the end of a longer one: "battle
// step" was, of the battle core's "no battle step".
inline constexpr size_t ROW_WORD_CAPACITY = 32;  // a longer word does not compile
inline constexpr std::array<std::array<char, ROW_WORD_CAPACITY>, ROW_COUNT> ROW_WORDS = {{
    {"bone flush"}, {"pass draw"},        {"battle core"}, {"unit lod"},       {"model detail"},
    {"shadow map"}, {"grass"},            {"grass cells"}, {"campaign trees"}, {"sprites"},
    {"game code"},  {"parallel effects"}, {"battle step"}}};
static_assert(std::ranges::none_of(ROW_WORDS, [](const auto& word) { return word.front() == 0 || word.back() != 0; }),
              "a word for every row, with its zero byte");
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
  for (const auto& word : ROW_WORDS) {
    length += std::string_view(word.data()).size() + std::string_view(", ").size() + std::string_view(" off").size();
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
  // A whole-number key of [Attila64Plugin.Values]; its default without it.
  const auto values_int = [&](const wchar_t* key, int default_value) {
    return (has_ini ? static_cast<int>(GetPrivateProfileIntW(INI_VALUES_SECTION, key, default_value, ini))
                    : default_value);
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
  const HMODULE loader_c_library = tweaks::ImportedFrom(GetModuleHandleW(nullptr), "getenv");
  tweaks::SetLoaderFrameCap(values_int(INI_SOL_FRAME_CAP, 0) == 1, loader_c_library, *api);
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

  grass_draw_code_read.store(Fnv1a(reinterpret_cast<const uint8_t*>(uintptr_t{SITES[LABEL_GRASS_DRAW_ENTRY].address}),
                                   GRASS_DRAW_NO_LOD_BYTES)
                                 == GRASS_DRAW_NO_LOD_HASH,
                             std::memory_order_relaxed);
  // Every site is checked before any is written: a row goes in whole or not at all, and one site's bytes can lie in
  // another's window (the shadow sizes and cascade counts).
  std::array<SiteCheck, SITES.size()> site_checks = {};
  for (size_t index = 0; index < SITES.size(); ++index) {
    const auto row = static_cast<Row>(SITES[index].row);
    site_checks[index] = {.refusal = REFUSAL_OFF};
    if (ini_int(ROW_INI_KEYS[row], ROW_INSTALL_DEFAULT) != 0) {
      block.rows_tried |= RowBit(row);
      site_checks[index] = CheckSite(SITES[index], FOREIGN);
      // A hand-made stub stands on all of its row's held game code, not only on its own range: the sprite rows' stub
      // computes what the half's function computes.
      if (site_checks[index].refusal == REFUSAL_NONE && SITES[index].kind == SITE_RAW_STUB
          && !std::ranges::all_of(HELD_RANGES, [row](const HeldRange& range) {
               return range.row != row || HeldRangeUnchanged(range, false);
             })) {
        site_checks[index].refusal = REFUSAL_HELD;
      }
    }
    if (site_checks[index].refusal != REFUSAL_NONE) {
      block.rows_refused |= RowBit(row);
    }
  }
  // Shadow Map Size, restart-only like the engine values: the add-on's key, written only where the add-on runs.
  const int shadow_map_size = values_int(INI_SHADOW_MAP_SIZE, 0);
  const bool shadow_size_in_range = (shadow_map_size >= 1 && shadow_map_size <= SHADOW_MAP_SIZE_STEPS);
  const bool shadow_sizes_written = (shadow_size_in_range && dx11_route);
  for (size_t index = 0; index < SITES.size(); ++index) {
    const Site& site = SITES[index];
    const auto row = static_cast<Row>(site.row);
    SiteCheck check = site_checks[index];
    if (check.refusal == REFUSAL_NONE && (block.rows_refused & RowBit(row)) != 0) {
      check.refusal = REFUSAL_ROW;
    }
    // A size site's two floats become the game's x 2^ShadowMapSize, as the 32-bit add-on writes them
    // (graphics_hooks.hpp shadow_map::Set). The cascade sites are only checked: the row is in once all of its sites
    // are the game's, so a later key applies at the next start.
    const bool size_rewritten = (shadow_sizes_written && IsShadowSizeSite(site));
    // A hand-made stub's site belongs to its row's step after this loop, which takes its code memory after every other
    // row's.
    if (check.refusal == REFUSAL_NONE && site.kind == SITE_RAW_STUB) continue;
    if (check.refusal == REFUSAL_NONE && site.kind == SITE_HOOK) {
      check.refusal = ApplySite(*api, site, REPLACEMENTS[site.label], &applied_sites[index]);
    } else if (check.refusal == REFUSAL_NONE && site.kind == SITE_MID) {
      check.refusal = ApplyMidSite(*api, site, reinterpret_cast<trampoline::MidHandler>(REPLACEMENTS[site.label]),
                                   &block.scales_active, MID_SITE_SCALES[site.label], &applied_sites[index]);
    } else if (check.refusal == REFUSAL_NONE) {
      std::array<float, 2> size = {};
      if (size_rewritten) {
        std::memcpy(size.data(), site.window.data() + SITE_MARGIN, sizeof(size));
        size[0] *= static_cast<float>(1 << shadow_map_size);
        size[1] *= static_cast<float>(1 << shadow_map_size);
      }
      check.refusal = ApplyPatch(*api, site, (size_rewritten ? reinterpret_cast<const uint8_t*>(size.data()) : nullptr),
                                 &applied_sites[index]);
    }
    if (check.refusal == REFUSAL_NONE) {
      originals[site.label] = applied_sites[index].original;
      block.rows_applied |= RowBit(row);
      // A patch site that was only checked is in too (the game-start count is the sites in), with its own words.
      if (site.kind == SITE_PATCH && !size_rewritten) {
        loader->log("renodx_attila64: site %s %#x: applied (game's bytes kept)", LABEL_WORDS[site.label], site.address);
      } else {
        loader->log("renodx_attila64: site %s %#x: applied", LABEL_WORDS[site.label], site.address);
      }
    } else {
      block.rows_refused |= RowBit(row);
      const bool foreign = (check.refusal == REFUSAL_FOREIGN);
      loader->log("renodx_attila64: site %s %#x: skipped (%s%s%s)", LABEL_WORDS[site.label], site.address,
                  REFUSAL_WORDS[check.refusal], (foreign ? " " : ""), (foreign ? OWNER_WORDS[check.owner] : ""));
    }
  }
  // The game code row, after every other row has its code memory: when the loader's page is full, only this row stays
  // out. All of its stubs go into one piece of code memory, whole before the first site points at it, then the sites
  // one by one; a site that fails ends the row (the loop below takes its earlier sites out again). With the add-on's
  // value key 0 nothing is written and the row is in: its checks passed, and the key is the player's own.
  if ((block.rows_tried & RowBit(ROW_GAME_CODE)) != 0 && (block.rows_refused & RowBit(ROW_GAME_CODE)) == 0) {
    const bool stubs_wanted = values_int(INI_FASTER_GAME_CODE, FASTER_GAME_CODE_DEFAULT) != 0;
    Refusal row_refusal = REFUSAL_NONE;
    uintptr_t stubs_address = 0;
    // With no stub to place (every site of the row check only) no code memory is taken.
    if (stubs_wanted && game_code::LAYOUT.bytes != 0) {
      // First with the gap in front, then without it (ours: without the gap the results are the same, only the speed
      // rule of game_code.hpp's LEAD_GAP_BYTES is not kept).
      size_t lead = game_code::LEAD_GAP_BYTES;
      const size_t aligned_bytes = (game_code::BLOCK_ALIGNMENT - 1) + game_code::LAYOUT.bytes;
      auto* memory = static_cast<uint8_t*>(api->code_near_game(lead + aligned_bytes));
      if (memory == nullptr) {
        lead = 0;
        memory = static_cast<uint8_t*>(api->code_near_game(aligned_bytes));
      }
      stubs_address = (reinterpret_cast<uintptr_t>(memory) + lead + (game_code::BLOCK_ALIGNMENT - 1))
                      & ~uintptr_t{game_code::BLOCK_ALIGNMENT - 1};
      std::array<uint8_t, game_code::LAYOUT.bytes> code = game_code::CODE;
      bool in_reach = true;
      for (const Site& site : SITES) {
        const game_code::Stub& stub = game_code::STUBS[site.label];
        if (stub.code.empty()) continue;
        const size_t offset = game_code::LAYOUT.offsets[site.label];
        in_reach = in_reach && trampoline::InJumpReach(site.address, stubs_address + offset);
        // The stub's jumps back into the game, from where it will run.
        for (const game_code::StubExit& stub_exit : stub.exits) {
          const uintptr_t jump = stubs_address + offset + stub_exit.rel32_offset - 1;
          in_reach = in_reach && trampoline::InJumpReach(jump, stub_exit.target);
          const auto displacement = static_cast<uint32_t>(stub_exit.target - (jump + trampoline::JUMP_BYTES));
          std::memcpy(code.data() + offset + stub_exit.rel32_offset, &displacement, sizeof(displacement));
        }
      }
      DWORD close_error = 0;
      if (memory == nullptr || !in_reach) {
        row_refusal = REFUSAL_NO_MEMORY;
      } else if (!WriteNearCode(reinterpret_cast<uint8_t*>(stubs_address), code, &close_error)) {
        row_refusal = REFUSAL_WRITE_FAILED;
      } else if (close_error != 0) {
        loader->log("renodx_attila64: game code stubs: their code near the game stays writable (error %lu)", close_error);
      }
    }
    char stub_words[128] = {};
    size_t stub_words_length = 0;
    for (size_t index = 0; index < SITES.size(); ++index) {
      const Site& site = SITES[index];
      if (site.row != ROW_GAME_CODE) continue;
      const game_code::Stub& stub = game_code::STUBS[site.label];
      const uintptr_t stub_address = stubs_address + game_code::LAYOUT.offsets[site.label];
      const bool written = (stubs_wanted && !stub.code.empty() && row_refusal == REFUSAL_NONE);
      Refusal refusal = row_refusal;
      if (written) {
        refusal = ApplyJump(*api, site, stub_address, &applied_sites[index]);
      }
      if (refusal != REFUSAL_NONE) {
        loader->log("renodx_attila64: site %s %#x: skipped (%s)", LABEL_WORDS[site.label], site.address,
                    REFUSAL_WORDS[refusal]);
        // The row's later sites are not tried.
        row_refusal = (written ? REFUSAL_ROW : row_refusal);
        block.rows_refused |= RowBit(ROW_GAME_CODE);
      } else if (written) {
        loader->log("renodx_attila64: site %s %#x: applied", LABEL_WORDS[site.label], site.address);
        const int words = std::snprintf(stub_words + stub_words_length, sizeof(stub_words) - stub_words_length,
                                        "%s%s %#x bytes at %#zx", ((stub_words_length == 0) ? "" : ", "), stub.word,
                                        static_cast<uint32_t>(stub.code.size()), stub_address);
        stub_words_length = std::min(stub_words_length + std::max(words, 0), sizeof(stub_words) - 1);
      } else if (stubs_wanted) {
        // No stub of its own is placed: the site and its held code stay watched for the row's other stubs.
        loader->log("renodx_attila64: site %s %#x: checked (check only)", LABEL_WORDS[site.label], site.address);
      } else {
        loader->log("renodx_attila64: site %s %#x: checked (value 0)", LABEL_WORDS[site.label], site.address);
      }
    }
    if (row_refusal == REFUSAL_NONE) {
      block.rows_applied |= RowBit(ROW_GAME_CODE);
      // Where our code is, for a crash address or a profiler's samples in the loader's page (it holds the loader's own
      // code too).
      if (stub_words_length != 0) {
        loader->log("renodx_attila64: game code stubs %#zx..%#zx (%s)", stubs_address,
                    stubs_address + game_code::LAYOUT.bytes, stub_words);
      }
    }
  }
  // A row is on only when all of its sites are. A patch site or a hand-made stub's site that went in before a later
  // site of its row failed comes out again (the stub's code stays, unused; the loader has no call to give it back); when
  // the loader cannot put its bytes back, the row stays refused and its log line names the site.
  // A hook cannot come out (a mid-function site is left in like one; no row has one beside another site): a refused
  // row's hook stays in and the game keeps calling it, with the row's bit clear in rows_applied. So the add-on sets a
  // row's scales_active bits only while the row is in rows_applied, and the one row with two hooks (the grass row)
  // tests its bit in rows_applied itself (hooks.hpp GrassRowRuns), final from here on.
  block.rows_applied &= ~block.rows_refused;
  for (size_t index = 0; index < SITES.size(); ++index) {
    const Site& site = SITES[index];
    if ((site.kind != SITE_PATCH && site.kind != SITE_RAW_STUB) || !applied_sites[index].applied
        || (block.rows_refused & RowBit(static_cast<Row>(site.row))) == 0) {
      continue;
    }
    if (RevertPatch(*api, site, &applied_sites[index])) {
      loader->log("renodx_attila64: site %s %#x: withdrawn (a later site of its row failed)", LABEL_WORDS[site.label],
                  site.address);
    } else {
      loader->log("renodx_attila64: site %s %#x: its bytes could not be put back", LABEL_WORDS[site.label], site.address);
    }
  }
  if ((block.rows_applied & RowBit(ROW_SHADOW_MAP)) != 0) {
    if (shadow_sizes_written) {
      // The size at Max and High (Medium's is half), from the site's own float.
      float game_size = 0.f;
      std::memcpy(&game_size, SITES[LABEL_SHADOW_SIZE_MAX].window.data() + SITE_MARGIN, sizeof(game_size));
      loader->log("renodx_attila64: shadow map %u: patched", static_cast<uint32_t>(game_size) << shadow_map_size);
    } else if (shadow_map_size == 0) {
      loader->log("renodx_attila64: shadow map: game's size");
    } else if (shadow_size_in_range) {
      loader->log("renodx_attila64: shadow map: game's size (ShadowMapSize=%d not written: not DX11)", shadow_map_size);
    } else {
      loader->log("renodx_attila64: shadow map: game's size (ShadowMapSize=%d out of range)", shadow_map_size);
    }
  }
  if ((block.rows_applied & RowBit(ROW_GRASS)) != 0 && !grass_draw_code_read.load(std::memory_order_relaxed)) {
    loader->log(GRASS_DRAW_CODE_LINE);
  }
  // The battle core's status is built (not 0) only with its hook in; battle_core.hpp writes it from the first step on.
  // The render core's the same, with its switch.
  if ((block.rows_applied & RowBit(ROW_CPU_PLACEMENT)) != 0) {
    block.core_status.store(uint64_t{CORE_WAITING} << CORE_STATE_SHIFT, std::memory_order_relaxed);
    battle_core::ApplyRenderCoreKey(&block, ini_int(INI_RENDER_CORE, -1));
  }
  if ((block.rows_applied & RowBit(ROW_PARALLEL_EFFECTS)) != 0) {
    ApplyEffectsModeKey(&block, ini_int(INI_EFFECTS_MODE, -1));
  }
  // The battle step row: its lane of engine_status from here on (not 0: the add-on shows the row), and its switch.
  // BattleStep 0 left the site out above, 1 sets the row's bit for a start without the add-on, anything else keeps
  // the build's default (while that default holds the bit, the key 1 changes nothing, and no test can tell it from
  // no key: one is due with the build whose default is without the bit). The row runs only with its site in and the
  // game's code as read (checked again at the game's start); with the key 0 nothing is read.
  {
    const int battle_step_key = ini_int(ROW_INI_KEYS[ROW_BATTLE_STEP], -1);
    if (battle_step_key == 1) {
      block.core_wanted.fetch_or(CORE_STEP, std::memory_order_relaxed);
    }
    EngineState lane = ENGINE_OFF_INI;
    if (battle_step_key != 0) {
      const bool code_read = (block.rows_applied & RowBit(ROW_BATTLE_STEP)) != 0 && battle_step::CheckGameCode(*api);
      battle_step::state.code_read.store(code_read, std::memory_order_relaxed);
      lane = ENGINE_CODE_DIFFERS;
      if (code_read) {
        lane = ENGINE_OFF;
        if ((block.core_wanted.load(std::memory_order_relaxed) & CORE_STEP) != 0) {
          lane = ENGINE_NOT_USED_YET;
        }
      }
    }
    battle_step::SetLane(&block, lane);
  }
  char rows[ROWS_LINE_CAPACITY] = {};
  size_t rows_length = 0;
  for (uint32_t row = 0; row < ROW_COUNT && rows_length < sizeof(rows); ++row) {
    const uint32_t bit = RowBit(static_cast<Row>(row));
    const bool on = (block.rows_applied & bit) != 0;
    const char* state = (((ROWS_WITH_SITES & bit) != 0) ? (on ? "on" : "off") : "-");
    const int written = std::snprintf(rows + rows_length, sizeof(rows) - rows_length, "%s%s %s",
                                      ((row == 0) ? "" : ", "), ROW_WORDS[row].data(), state);
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
    if (!applied_sites[index].applied) continue;
    if (SiteUnchanged(site, applied_sites[index])) {
      ++unchanged;
    } else {
      block.rows_changed.fetch_or(RowBit(static_cast<Row>(site.row)));
      loader->log("renodx_attila64: site %s changed after we patched it", LABEL_WORDS[site.label]);
    }
  }
  // The game code a written stub stands on (with none written, no code of ours runs on it): the range without our
  // jump where its site is written, the whole range where the site was only checked. No count: a line only on a change.
  for (const HeldRange& range : HELD_RANGES) {
    const bool row_written = std::ranges::any_of(SITES, [&range](const Site& site) {
      return site.row == range.row && site.kind == SITE_RAW_STUB && applied_sites[site.label].applied;
    });
    if (!row_written || HeldRangeUnchanged(range, applied_sites[range.label].applied)) continue;
    block.rows_changed.fetch_or(RowBit(static_cast<Row>(range.row)));
    loader->log("renodx_attila64: site %s changed after we patched it", LABEL_WORDS[range.label]);
  }
  loader->log("renodx_attila64: game started: %u sites unchanged", unchanged);
  // The grass draw's code once more, after the loader's late writes: our jump is in its first bytes now, so the
  // site's own bytes stand in for them.
  if ((block.rows_applied & RowBit(ROW_GRASS)) != 0 && grass_draw_code_read.load(std::memory_order_relaxed)) {
    const Site& draw = SITES[LABEL_GRASS_DRAW_ENTRY];
    std::array<uint8_t, GRASS_DRAW_NO_LOD_BYTES> code = {};
    std::memcpy(code.data(), reinterpret_cast<const void*>(uintptr_t{draw.address}), code.size());
    std::memcpy(code.data(), draw.window.data() + SITE_MARGIN, draw.length);
    if (Fnv1a(code.data(), code.size()) != GRASS_DRAW_NO_LOD_HASH) {
      grass_draw_code_read.store(false, std::memory_order_relaxed);
      loader->log(GRASS_DRAW_CODE_LINE);
    }
  }
  // The battle step's code once more, after the loader's late writes; its site was re-checked above.
  if (battle_step::state.code_read.load(std::memory_order_relaxed)
      && ((block.rows_changed.load(std::memory_order_relaxed) & RowBit(ROW_BATTLE_STEP)) != 0
          || !battle_step::CheckGameCode(*loader))) {
    battle_step::state.code_read.store(false, std::memory_order_relaxed);
    battle_step::SetLane(&block, ENGINE_CODE_DIFFERS);
  }
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
      // The game's parallel effect flag is offered (OK) only while the loader's own hooks guard the code that flag
      // turns on and the game's one read of it is the code that was read; else it is ABSENT, the add-on hides its row
      // and the walk hook stays inert.
      static_assert(TWEAKS[tweaks::LOADER_FLAG_TWEAK].object + TWEAK_KINDS[TWEAK_BOOL].value_offset == EFFECT_FLAG);
      if (index == tweaks::LOADER_FLAG_TWEAK && slot->verdict == VERDICT_OK) {
        const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        const auto* nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos_header->e_lfanew);
        const auto image_lo = reinterpret_cast<uintptr_t>(image);
        const uintptr_t image_hi = image_lo + nt_headers->OptionalHeader.SizeOfImage;
        if ((block.rows_applied & RowBit(ROW_PARALLEL_EFFECTS)) == 0) {
          loader->log("renodx_attila64: parallel effects: not offered (its row is off)");
          slot->verdict = VERDICT_ABSENT;
        } else if (!LoaderHooked(EFFECT_GUARD_ENTRIES, image_lo, image_hi)) {
          const auto not_the_loaders = std::ranges::find_if(EFFECT_GUARD_ENTRIES, [&](uintptr_t guard) {
            return !LoaderHooked(std::array{guard}, image_lo, image_hi);
          });
          loader->log("renodx_attila64: parallel effects: not offered (%#llx not the loader's)",
                      static_cast<unsigned long long>(*not_the_loaders));
          slot->verdict = VERDICT_ABSENT;
        } else if (std::memcmp(reinterpret_cast<const void*>(EFFECT_FLAG_READ_ADDRESS), EFFECT_FLAG_READ.data(),
                               EFFECT_FLAG_READ.size())
                   != 0) {
          loader->log("renodx_attila64: parallel effects: not offered (flag read differs)");
          slot->verdict = VERDICT_ABSENT;
        } else {
          loader->log("renodx_attila64: parallel effects: offered (loader guards in place)");
          effect_walk.loader_value = static_cast<uint8_t>(slot->default_bits);
          effect_walk.stored = effect_walk.loader_value;
          effects_offered.store(true, std::memory_order_release);
        }
      }
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
  } else if (block.tweaks_status.load(std::memory_order_relaxed) == TWEAKS_NONE) {
    loader->log("renodx_attila64: parallel effects: not offered (Tweaks=0)");
  }
  // This thread goes on to run the game's main: the render thread.
  if ((block.rows_applied & RowBit(ROW_CPU_PLACEMENT)) != 0) {
    battle_core::SetRenderThread(&block, *loader);
  }
}

extern "C" __declspec(dllexport) Block* renodx_attila64_block() { return &block; }
