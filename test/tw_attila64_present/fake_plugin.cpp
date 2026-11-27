/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// renodx_attila64.dll for F7's plugin cases: the 64-bit engine plugin's block (plugin_block.h) and, in place of its
// game hooks, one thread that calls the add-on's callbacks through the hooks' guard while any row is wanted (no game
// code: pass_begin finds nothing counted and answers PASS_DRAW). The test exe loads it where the loader loads its
// plugins (after a plugin's test device, before the game's device).
// RENODX_F7_PLUGIN="<status> <rows_applied> <rows_changed> [<version> [<rows_tried>]]" (attila64_plugin::Status and Row
// bits; default "1 3 0", version 2 with rows_tried = rows_applied; version 1: a v1 plugin's block, its 40 bytes).

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "src/games/twattila/attila64_plugin/plugin_block.h"

namespace {

namespace plugin = twattila::attila64_plugin;

plugin::Block block;
std::atomic<unsigned> callback_calls = 0;

// The hooks' guard (hooks.hpp): the wanted bits relaxed, calls_in_flight up, callbacks loaded, the call, down; pass_end
// only while callbacks holds the pointer of its pass_begin.
DWORD WINAPI Hooks(void*) {
  for (;;) {
    if (block.hooks_wanted.load(std::memory_order_relaxed) != 0) {
      block.calls_in_flight.fetch_add(1);
      if (const plugin::Callbacks* callbacks = block.callbacks.load(); callbacks != nullptr) {
        callbacks->before_pack();
        plugin::PassMeasure measure = {};
        if (callbacks->pass_begin(5, &measure) == plugin::PASS_DRAW_MEASURED && block.callbacks.load() == callbacks) {
          callbacks->pass_end(5, measure);
        }
        callback_calls.fetch_add(1);
      }
      block.calls_in_flight.fetch_sub(1);
    }
    Sleep(1);
  }
}

}  // namespace

extern "C" __declspec(dllexport) plugin::Block* renodx_attila64_block() { return &block; }

// F7's own: the calls made into the add-on's callbacks so far.
extern "C" __declspec(dllexport) unsigned renodx_f7_callback_calls() { return callback_calls.load(); }

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  char value[64] = {};
  unsigned status = plugin::STATUS_READY;
  unsigned rows = 3;
  unsigned changed = 0;
  unsigned version = plugin::VERSION;
  unsigned tried = ~0u;
  if (GetEnvironmentVariableA("RENODX_F7_PLUGIN", value, sizeof(value)) != 0) {
    std::sscanf(value, "%u %u %u %u %u", &status, &rows, &changed, &version, &tried);
  }
  block.version = version;
  block.bytes = static_cast<uint32_t>(plugin::BlockBytes(version));
  block.rows_tried = ((tried == ~0u) ? rows : tried);
  block.rows_applied = rows;
  block.rows_changed.store(changed);
  block.status.store(status, std::memory_order_release);
  // Runs once DllMain returns (the loader lock); never waited for, ends with the process.
  if (HANDLE thread = CreateThread(nullptr, 0, Hooks, nullptr, 0, nullptr); thread != nullptr) CloseHandle(thread);
  return TRUE;
}
