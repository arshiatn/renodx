/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// renodx_attila64.dll for F7's plugin cases: the 64-bit engine plugin's block (plugin_block.h) and, in place of its
// game hooks, one thread that calls the add-on's callbacks through the hooks' guard while any row is wanted (no game
// code: pass_begin finds nothing counted and answers PASS_DRAW). The test exe loads it where the loader loads its
// plugins (after a plugin's test device, before the game's device).
// RENODX_F7_PLUGIN="<status> <rows_applied> <rows_changed> [<version> [<rows_tried> [<notices> [<tree_value>
// [<core_wanted> <render_core_status> [<effects_slot> [<engine_status>]]]]]]]" (attila64_plugin::Status, Row and
// NOTICE_ bits; default "1 3 0", version 2 with rows_tried = rows_applied, no notices; version 1: a v1 plugin's block,
// its 40 bytes). tree_value 1: one engine value slot, Campaign Tree Detail's rigid_campaign_tree_lod (live, the game's
// 100 m), its values ready; the hooks' thread notes any write to it. core_wanted: its initial bits (default
// CORE_WANTED_DEFAULT); render_core_status: the render part's word as the plugin's init publishes it (default 0, not
// built). effects_slot: the loader's effect value slot VFX_PARALLEL_UPDATER (PK9 7.3) after the game's start check,
// tweaks ready (0 none, 1 verdict OK, 2 CHANGED); engine_status: the engine rows' lanes (default 0, none built).

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "src/games/twattila/attila64_plugin/plugin_block.h"

namespace {

namespace plugin = twattila::attila64_plugin;

plugin::Block block;
std::atomic<unsigned> callback_calls = 0;
// Campaign Tree Detail's engine value (tree_value 1): the game's 100 m, and whether anything wrote another value.
constexpr uint32_t TREE_LOD_GAME_BITS = 0x42c80000;
std::atomic<uint32_t> tree_lod_bits = TREE_LOD_GAME_BITS;
std::atomic_bool tree_lod_written = false;
plugin::TweakSlot tree_lod_slot = {.name = "rigid_campaign_tree_lod",
                                   .value_address = reinterpret_cast<uintptr_t>(&tree_lod_bits),
                                   .default_bits = TREE_LOD_GAME_BITS,
                                   .written_bits = TREE_LOD_GAME_BITS,
                                   .type = plugin::TWEAK_FLOAT,
                                   .verdict = plugin::VERDICT_OK};
// The block's slots: Campaign Tree Detail's, then the loader's effect value (no address: the add-on reads its verdict
// only).
plugin::TweakSlot slots[2] = {};

// The hooks' guard (hooks.hpp): the wanted bits relaxed, calls_in_flight up, callbacks loaded, the call, down; pass_end
// only while callbacks holds the pointer of its pass_begin.
DWORD WINAPI Hooks(void*) {
  for (;;) {
    if (tree_lod_bits.load(std::memory_order_relaxed) != TREE_LOD_GAME_BITS) {
      tree_lod_written.store(true, std::memory_order_relaxed);
    }
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

// F7's own: whether Campaign Tree Detail's engine value was ever written (tree_value 1).
extern "C" __declspec(dllexport) bool renodx_f7_tree_lod_written() {
  return tree_lod_written.load() || tree_lod_bits.load() != TREE_LOD_GAME_BITS;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  char value[64] = {};
  unsigned status = plugin::STATUS_READY;
  unsigned rows = 3;
  unsigned changed = 0;
  unsigned version = plugin::VERSION;
  unsigned tried = ~0u;
  unsigned notices = 0;
  unsigned tree_value = 0;
  unsigned core_wanted = plugin::CORE_WANTED_DEFAULT;
  unsigned render_core_status = 0;
  unsigned effects_slot = 0;
  unsigned engine_status = 0;
  if (GetEnvironmentVariableA("RENODX_F7_PLUGIN", value, sizeof(value)) != 0) {
    std::sscanf(value, "%u %u %u %u %u %u %u %u %u %u %u", &status, &rows, &changed, &version, &tried, &notices,
                &tree_value, &core_wanted, &render_core_status, &effects_slot, &engine_status);
  }
  block.version = version;
  block.bytes = static_cast<uint32_t>(plugin::BlockBytes(version));
  block.rows_tried = ((tried == ~0u) ? rows : tried);
  block.rows_applied = rows;
  block.rows_changed.store(changed);
  block.notices.store(notices);
  block.core_wanted.store(core_wanted);
  block.render_core_status.store(render_core_status);
  block.engine_status.store(engine_status);
  uint32_t slot_count = 0;
  if (tree_value != 0) {
    slots[slot_count++] = tree_lod_slot;
  }
  if (effects_slot != 0) {
    slots[slot_count++] = {.name = "VFX_PARALLEL_UPDATER",
                           .type = plugin::TWEAK_BOOL,
                           .verdict = (effects_slot == 1 ? plugin::VERDICT_OK : plugin::VERDICT_CHANGED)};
  }
  if (slot_count != 0) {
    block.tweaks = slots;
    block.tweak_count = slot_count;
    block.tweaks_status.store(plugin::TWEAKS_READY, std::memory_order_release);
  }
  block.status.store(status, std::memory_order_release);
  // Runs once DllMain returns (the loader lock); never waited for, ends with the process.
  if (HANDLE thread = CreateThread(nullptr, 0, Hooks, nullptr, 0, nullptr); thread != nullptr) CloseHandle(thread);
  return TRUE;
}
