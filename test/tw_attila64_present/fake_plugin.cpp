/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// renodx_attila64.dll for F7's plugin cases: the 64-bit engine plugin's block alone (plugin_block.h), no hooks. The
// test exe loads it where the loader loads its plugins (after a plugin's test device, before the game's device).
// RENODX_F7_PLUGIN="<status> <rows_applied> <rows_changed>" (attila64_plugin::Status and Row bits; default "1 3 0").

#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "src/games/twattila/attila64_plugin/plugin_block.h"

namespace {

twattila::attila64_plugin::Block block;

}  // namespace

extern "C" __declspec(dllexport) twattila::attila64_plugin::Block* renodx_attila64_block() { return &block; }

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  char value[64] = {};
  unsigned status = twattila::attila64_plugin::STATUS_READY;
  unsigned rows = 3;
  unsigned changed = 0;
  if (GetEnvironmentVariableA("RENODX_F7_PLUGIN", value, sizeof(value)) != 0) {
    std::sscanf(value, "%u %u %u", &status, &rows, &changed);
  }
  block.rows_applied = rows;
  block.rows_changed.store(changed);
  block.status.store(status, std::memory_order_release);
  return TRUE;
}
