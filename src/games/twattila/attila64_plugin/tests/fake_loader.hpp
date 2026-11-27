/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../loader_api.h"
#include "../plugin_block.h"
#include "../sites.h"

// The tests' stand-in for the 64-bit Attila loader: its plugin interface v1 written from its documented behaviour (an
// entry hook copies the first bytes to code near the game, jumps back after them, and enters through
// mov rax, imm64; jmp rax), its stack-check rewrite, the game's address range, and the plugin DLL's whole init.
namespace twattila::attila64_plugin::test {

inline constexpr uintptr_t GAME_RESERVE_END = 0x527d000;  // the last load segment's end, page-rounded

enum HookMode : uint8_t { HOOK_NORMAL, HOOK_NO_MEMORY, HOOK_WRITE_FAILS };

static int checks = 0;
static int failures = 0;
static std::vector<std::string> log_lines;
static HookMode hook_mode = HOOK_NORMAL;

static void Expect(bool ok, const char* what) {
  ++checks;
  if (ok) return;
  ++failures;
  std::printf("FAIL: %s\n", what);
}

static void FakeLog(const char* format, ...) noexcept {
  char line[1024];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(line, sizeof(line), format, arguments);
  va_end(arguments);
  std::printf("  log: %s\n", line);
  log_lines.emplace_back(line);
}

static bool Logged(const char* text) {
  for (const std::string& line : log_lines) {
    if (line.find(text) != std::string::npos) return true;
  }
  return false;
}

// One 8 KB execute-read-write page in rel32 reach of the game, handed out front to back.
static void* FakeCodeNearGame(size_t n) noexcept {
  static uint8_t* page = nullptr;
  static size_t used = 0;
  for (uintptr_t at = 0x5300000; page == nullptr && at < 0x60000000; at += 0x10000) {
    page = static_cast<uint8_t*>(
        VirtualAlloc(reinterpret_cast<void*>(at), 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  }
  if (page == nullptr || used + n > 0x2000) return nullptr;
  used += n;
  return page + used - n;
}

static int FakePatch(uintptr_t at, const uint8_t* expect, const uint8_t* bytes, size_t len) noexcept {
  if (std::memcmp(reinterpret_cast<void*>(at), expect, len) != 0) return 0;
  std::memcpy(reinterpret_cast<void*>(at), bytes, len);
  return 1;
}

// 32 bytes near the game: [0, len) the copied bytes, then jmp rel32 to at + len; at 16 the way in to fn. The site gets
// a jmp rel32 to the way in. Lengths 12..15 would overlap the way in, so this stand-in refuses them.
static void* FakeHook(uintptr_t at, const uint8_t* expect, int len, void* fn) noexcept {
  auto* site = reinterpret_cast<uint8_t*>(at);
  if (hook_mode == HOOK_NO_MEMORY || len < 5 || len > 11 || std::memcmp(site, expect, len) != 0) return nullptr;
  if (hook_mode == HOOK_WRITE_FAILS) {
    site[0] ^= 0xff;  // someone else's write in between
    return nullptr;
  }
  auto* code = static_cast<uint8_t*>(FakeCodeNearGame(32));
  if (code == nullptr) return nullptr;
  std::memcpy(code, site, len);
  code[len] = 0xe9;
  const auto back = static_cast<int32_t>(reinterpret_cast<intptr_t>(site + len) - reinterpret_cast<intptr_t>(code + len + 5));
  std::memcpy(code + len + 1, &back, 4);
  uint8_t* way_in = code + 16;
  way_in[0] = 0x48;
  way_in[1] = 0xb8;
  std::memcpy(way_in + 2, &fn, 8);
  way_in[10] = 0xff;
  way_in[11] = 0xe0;
  const auto in = static_cast<int32_t>(reinterpret_cast<intptr_t>(way_in) - reinterpret_cast<intptr_t>(site + 5));
  site[0] = 0xe9;
  std::memcpy(site + 1, &in, 4);
  FlushInstructionCache(GetCurrentProcess(), site, len);
  return code;
}

static LoaderApi MakeApi(const char* plugin_dir, uintptr_t game_hi) {
  return {
      .api_version = LOADER_API_VERSION,
      .engine_version = "test",
      .game_lo = GAME_LO,
      .game_hi = game_hi,
      .plugin_dir = plugin_dir,
      .log = &FakeLog,
      .patch = &FakePatch,
      .hook = &FakeHook,
      .code_near_game = &FakeCodeNearGame,
  };
}

// The game's whole range, committed with `protect`; false when another mapping is there.
static bool ReserveGame(DWORD protect) {
  return VirtualAlloc(reinterpret_cast<void*>(GAME_LO), GAME_RESERVE_END - GAME_LO, MEM_RESERVE | MEM_COMMIT, protect)
         != nullptr;
}

// The loader's rewrite of the game's stack-check reads (fs:0x28 -> gs:0x28): 64 REX.W (mov | xor | sub | cmp)
// r64, [disp32] with disp32 0x28; the generator's windows hold the result.
static int ApplyStackRule(uint8_t* begin, const uint8_t* end) {
  int rewritten = 0;
  for (uint8_t* p = begin; p + 9 <= end; ++p) {
    if (p[0] != 0x64 || (p[1] & 0xf8) != 0x48) continue;
    if (p[2] != 0x8b && p[2] != 0x33 && p[2] != 0x2b && p[2] != 0x3b) continue;
    if ((p[3] & 0xc7) != 0x04 || p[4] != 0x25 || p[5] != 0x28 || p[6] != 0 || p[7] != 0 || p[8] != 0) continue;
    p[0] = 0x65;
    ++rewritten;
  }
  return rewritten;
}

// One loaded copy of the plugin DLL (a copy at another path is another module with its own globals).
struct Plugin {
  int (*init)(const LoaderApi*) = nullptr;
  void (*game_started)(const LoaderApi*) = nullptr;
  Block* block = nullptr;
};

// The DLL loaded and its three exports found; all nullptr (and a failure) otherwise.
static Plugin LoadPlugin(const wchar_t* dll_path) {
  HMODULE dll = LoadLibraryW(dll_path);
  Expect(dll != nullptr, "the plugin DLL loads");
  if (dll == nullptr) return {};
  const auto block_export = reinterpret_cast<BlockExportFn>(GetProcAddress(dll, BLOCK_EXPORT));
  Plugin plugin = {
      .init = reinterpret_cast<int (*)(const LoaderApi*)>(GetProcAddress(dll, "attila64_plugin_init")),
      .game_started = reinterpret_cast<void (*)(const LoaderApi*)>(GetProcAddress(dll, "attila64_plugin_game_started")),
      .block = (block_export != nullptr) ? block_export() : nullptr,
  };
  if (plugin.init == nullptr || plugin.game_started == nullptr || plugin.block == nullptr) {
    Expect(false, "the plugin DLL has its three exports");
    return {};
  }
  return plugin;
}

// The plugin DLL's init and game start as the loader runs them, on a game image that holds every site's window.
static Plugin ExpectPluginDll(const LoaderApi& api, const wchar_t* dll_path) {
  const Plugin plugin = LoadPlugin(dll_path);
  if (plugin.init == nullptr) return plugin;

  log_lines.clear();
  Expect(plugin.init(&api) == 0, "the plugin's init accepts the game");
  const Block* shared = plugin.block;
  Expect(shared->version == VERSION && shared->bytes == BlockBytes(VERSION), "the block's version and size");
  Expect(shared->status.load(std::memory_order_acquire) == STATUS_READY, "the block is READY after init");
  Expect(shared->rows_applied == (RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW)) && shared->rows_refused == 0,
         "both rows applied");
  // v2's initial values: the rows tried, the core default, every status word not built, the scales neutral.
  bool scales_neutral = true;
  for (size_t scale = 0; scale < SCALE_COUNT; ++scale) {
    scales_neutral = scales_neutral && shared->scales[scale].load() == NEUTRAL_SCALES[scale];
  }
  Expect(shared->rows_tried == (RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW))
             && shared->core_wanted.load() == CORE_WANTED_DEFAULT && shared->core_status.load() == 0
             && shared->render_core_status.load() == 0 && shared->engine_status.load() == 0
             && shared->tweaks_status.load() == TWEAKS_NONE && shared->tweak_count == 0 && shared->tweaks == nullptr
             && shared->notices.load() == 0 && shared->scales_active.load() == 0 && scales_neutral
             && NEUTRAL_SCALES[SCALE_SPRITE_DISTANCE] == 0.0f && NEUTRAL_SCALES[SCALE_DEAD_UNITS] == 1.0f,
         "v2's initial values");
  Expect(Logged("game build ok") && Logged("PACK_ENTRY 0x3416d50: applied") && Logged("VIEW_DRAW_ENTRY 0x33f6120: applied")
             && Logged("rows: bone flush on, pass draw on, battle core -, unit lod -, model detail -, shadow map -, grass -, "
                       "grass cells -, campaign trees -, sprites -"),
         "the init lines");
  for (const Site& site : SITES) {
    Expect(*reinterpret_cast<const uint8_t*>(site.address) == 0xe9, "each site jumps to the hook");
  }
  plugin.game_started(&api);
  Expect(shared->rows_changed.load() == 0 && Logged("game started: 2 sites unchanged"), "game start finds both sites unchanged");
  return plugin;
}

}  // namespace twattila::attila64_plugin::test
