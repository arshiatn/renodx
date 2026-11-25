/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

// The 64-bit Attila loader's plugin interface, version 1, declared from its documented layout (the loader is MIT; its
// header is not copied). The loader calls attila64_plugin_init(api) once, after the game is mapped and before any game
// code runs, and attila64_plugin_game_started(api) once the game has set up its settings. Every pointer here is the
// loader's own (Windows calling convention; C, so none throws); game functions use the Linux one (ATTILA64_GAME_CALL).
#define ATTILA64_GAME_CALL __attribute__((sysv_abi))

namespace twattila::attila64_plugin {

inline constexpr uint32_t LOADER_API_VERSION = 1;

struct LoaderApi {
  uint32_t api_version;        // at least the version a plugin was written for
  const char* engine_version;  // the loader's version text
  uintptr_t game_lo;           // the game's code and data in memory
  uintptr_t game_hi;
  const char* plugin_dir;  // the plugins folder, no trailing slash
  void (*log)(const char* format, ...) noexcept;
  // 1 = the len bytes at `at` were exactly `expect` and are now `bytes`; 0 = left alone.
  int (*patch)(uintptr_t at, const uint8_t* expect, const uint8_t* bytes, size_t len) noexcept;
  // The game function at `at` (first len bytes == expect, whole instructions, nothing position-dependent) calls fn
  // instead; returns what to call for the original, nullptr when the bytes differ or there is no memory near the game.
  void* (*hook)(uintptr_t at, const uint8_t* expect, int len, void* fn) noexcept;
  // n bytes within reach of the game's 32-bit jumps; nullptr when there is none left.
  void* (*code_near_game)(size_t n) noexcept;
};
static_assert(offsetof(LoaderApi, api_version) == 0);
static_assert(offsetof(LoaderApi, engine_version) == 8);
static_assert(offsetof(LoaderApi, game_lo) == 16);
static_assert(offsetof(LoaderApi, game_hi) == 24);
static_assert(offsetof(LoaderApi, plugin_dir) == 32);
static_assert(offsetof(LoaderApi, log) == 40);
static_assert(offsetof(LoaderApi, patch) == 48);
static_assert(offsetof(LoaderApi, hook) == 56);
static_assert(offsetof(LoaderApi, code_near_game) == 64);
static_assert(sizeof(LoaderApi) == 72);

}  // namespace twattila::attila64_plugin
