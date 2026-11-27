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
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../loader_api.h"
#include "../plugin_block.h"
#include "../sites.h"
#include "../tweaks.h"

// The tests' stand-in for the 64-bit Attila loader: its plugin interface v1 written from its documented behaviour (an
// entry hook copies the first bytes to code near the game, jumps back after them, and enters through
// mov rax, imm64; jmp rax), its stack-check rewrite, the game's address range, the engine value objects as the game's
// static constructors leave them, and the plugin DLL's whole init.
namespace twattila::attila64_plugin::test {

inline constexpr uintptr_t GAME_RESERVE_END = 0x527d000;  // the last load segment's end, page-rounded

enum HookMode : uint8_t { HOOK_NORMAL, HOOK_NO_MEMORY, HOOK_WRITE_FAILS };

static int checks = 0;
static int failures = 0;
// The battle core's watchdog logs from a timer thread.
static std::shared_mutex log_mutex;
static std::vector<std::string> log_lines;
static HookMode hook_mode = HOOK_NORMAL;
// FakePatch refuses this call of the ones still to come (1 = the next); 0 = none.
static int failing_patch_call = 0;

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
  const std::scoped_lock lock(log_mutex);
  log_lines.emplace_back(line);
}

static bool Logged(const char* text) {
  const std::shared_lock lock(log_mutex);
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
  if (failing_patch_call != 0 && --failing_patch_call == 0) return 0;
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

// Every tweak object of tweaks.h in the reserved game range, as the game builds it: its type's table pointer, its
// name as a counted UTF-16 string, its default.
static void WriteTweakObjects() {
  static std::vector<std::u16string> names;  // the objects point into these
  names.clear();
  names.reserve(TWEAKS.size());
  for (const TweakEntry& entry : TWEAKS) {
    if (entry.object == 0) continue;
    const TweakKind& kind = TWEAK_KINDS[entry.type];
    auto* object = reinterpret_cast<uint8_t*>(uintptr_t{entry.object});
    const std::string_view narrow = entry.name;
    const std::u16string& name = names.emplace_back(narrow.begin(), narrow.end());
    const uint64_t table = kind.table;
    const auto length = static_cast<uint32_t>(name.size());
    const char16_t* units = name.data();
    std::memcpy(object, &table, sizeof(table));
    std::memcpy(object + TWEAK_NAME_LENGTH_OFFSET, &length, sizeof(length));
    std::memcpy(object + TWEAK_NAME_UNITS_OFFSET, &units, sizeof(units));
    std::memcpy(object + kind.value_offset, &entry.default_bits, kind.value_bytes);
  }
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

// The plugin DLL's init and game start as the loader runs them, on a game image that holds every site's window and
// every tweak object (WriteTweakObjects), with no ReShade.ini keys and no route note beside the test: every row with a
// site goes in, and no patch site has other bytes to write.
static Plugin ExpectPluginDll(const LoaderApi& api, const wchar_t* dll_path) {
  const Plugin plugin = LoadPlugin(dll_path);
  if (plugin.init == nullptr) return plugin;

  {
    const std::scoped_lock lock(log_mutex);
    log_lines.clear();
  }
  Expect(plugin.init(&api) == 0, "the plugin's init accepts the game");
  const Block* shared = plugin.block;
  Expect(shared->version == VERSION && shared->bytes == BlockBytes(VERSION), "the block's version and size");
  Expect(shared->status.load(std::memory_order_acquire) == STATUS_READY, "the block is READY after init");
  uint32_t rows = 0;
  for (const Site& site : SITES) {
    rows |= RowBit(static_cast<Row>(site.row));
  }
  Expect(shared->rows_applied == rows && shared->rows_refused == 0, "every row with a site applied");
  // v2's initial values: the rows tried, the core default (no RenderCore key here), the battle core waiting, the render
  // core without its thread yet, the engine values waiting for the game's start, the other status words not built, the
  // scales neutral.
  bool scales_neutral = true;
  for (size_t scale = 0; scale < SCALE_COUNT; ++scale) {
    scales_neutral = scales_neutral && shared->scales[scale].load() == NEUTRAL_SCALES[scale];
  }
  Expect(shared->rows_tried == rows && shared->core_wanted.load() == CORE_WANTED_DEFAULT
             && shared->core_status.load() == (uint64_t{CORE_WAITING} << CORE_STATE_SHIFT)
             && shared->render_core_status.load() == (uint32_t{CORE_NO_THREAD} << CORE_STATE_SHIFT)
             && shared->engine_status.load() == 0
             && shared->tweaks_status.load() == TWEAKS_PENDING && shared->tweak_count == 0 && shared->tweaks == nullptr
             && shared->notices.load() == 0 && shared->scales_active.load() == 0 && scales_neutral
             && NEUTRAL_SCALES[SCALE_SPRITE_DISTANCE] == 0.0f && NEUTRAL_SCALES[SCALE_DEAD_UNITS] == 1.0f,
         "v2's initial values");
  Expect(Logged("renodx_attila64 2: game build ok") && Logged(", graphics ") && Logged("renodx_attila64: loader frame cap: ")
             && Logged("Changes drawing, sound memory and CPU core use only; battles, replays and multiplayer stay the same.")
             && Logged("PACK_ENTRY 0x3416d50: applied") && Logged("VIEW_DRAW_ENTRY 0x33f6120: applied")
             && Logged("STEP_ENTRY 0xbb4e00: applied") && Logged("renodx_attila64: shadow map: game's size")
             && Logged("rows: bone flush on, pass draw on, battle core on, unit lod on, model detail on, shadow map on, "
                       "grass -, grass cells -, campaign trees -, sprites -"),
         "the init lines");
  for (const Site& site : SITES) {
    char applied_line[96];
    std::snprintf(applied_line, sizeof(applied_line), "renodx_attila64: site %s %#x: applied", LABEL_WORDS[site.label],
                  site.address);
    Expect(Logged(applied_line), "each site has its applied line");
    if (site.kind == SITE_HOOK) {
      Expect(*reinterpret_cast<const uint8_t*>(site.address) == 0xe9, "each hook site jumps to the hook");
    } else {
      Expect(std::memcmp(reinterpret_cast<const void*>(uintptr_t{site.address}), site.window.data() + SITE_MARGIN, site.length) == 0,
             "each patch site keeps the game's bytes without a key");
    }
  }
  plugin.game_started(&api);
  // The count follows the sites table (a later site needs no edit here).
  char started[64];
  std::snprintf(started, sizeof(started), "game started: %zu sites unchanged", SITES.size());
  Expect(shared->rows_changed.load() == 0 && Logged(started), "game start finds every site unchanged");
  // Every engine value checked once and published; nothing restart-only without its key.
  char tweaks_line[96];
  std::snprintf(tweaks_line, sizeof(tweaks_line), "renodx_attila64: tweaks: %zu checked, %zu ok, 2 absent, restart-only: 0 applied",
                TWEAKS.size() - 2, TWEAKS.size() - 2);
  Expect(shared->tweaks_status.load(std::memory_order_acquire) == TWEAKS_READY && shared->tweak_count == TWEAKS.size()
             && shared->tweaks != nullptr && Logged(tweaks_line) && !Logged(": refused ("),
         "game start checks and publishes the engine values");
  // The thread that runs the game's start is the render thread: captured once, with its own line.
  char render_thread[64];
  std::snprintf(render_thread, sizeof(render_thread), "renodx_attila64: render thread %lu", GetCurrentThreadId());
  const auto line_count = [] {
    const std::shared_lock lock(log_mutex);
    return log_lines.size();
  };
  const size_t lines_before = line_count();
  plugin.game_started(&api);
  const bool sites_line_last = [] {
    const std::shared_lock lock(log_mutex);
    return std::string_view(log_lines.back()).starts_with("renodx_attila64: game started: ");
  }();
  Expect(shared->render_core_status.load() == (uint32_t{CORE_WAITING} << CORE_STATE_SHIFT) && Logged(render_thread)
             && !Logged("render core:") && sites_line_last && line_count() == lines_before + 1,
         "game start captures the render thread once: a second start logs only its sites line (the engine values too)");
  return plugin;
}

}  // namespace twattila::attila64_plugin::test
