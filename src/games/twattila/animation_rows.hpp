/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

// Animation Rows on Workers (Attila, 32-bit): the animation update's call of the animation texture fill, done on the worker
// pool's threads (worker_pool.hpp, User::ANIMATION_ROWS) with the same results as the game's own code. Engine code
// (engine_code.hpp) puts a stub at that call; the stub reaches `Dispatch` through a word in its data page, 0 while the row is
// off or the site is coming out, and then runs the game's own call.
// Pass-through for now: `Dispatch` runs the game's fill, so a row that is on changes nothing in the picture.
namespace twattila::animation_rows {

// The row's stage.
enum class Stage : uint8_t { OFF, CHECK, ON };

#if !defined(_WIN64)
namespace internal {

// The game's fill: `cache` is its this (ecx), the arguments are the seven the animation update pushes (callee pops them).
using Fill = uint32_t(__thiscall*)(void* cache, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end,
                                   uint32_t* out);
static Fill game_fill = nullptr;

}  // namespace internal

// Engine code's Prepare: where the game's fill is (found and checked there).
inline void SetGame(const uint8_t* fill) { internal::game_fill = reinterpret_cast<internal::Fill>(reinterpret_cast<uintptr_t>(fill)); }

// The stub's target (engine_code.hpp): the stub pushes the seven arguments again and calls it with ecx = the cache, edx unused;
// the callee pops them (ret 0x1c) as the game's own fill does, and the result is the game's (the animation update reads al).
inline uint32_t __fastcall Dispatch(void* cache, void*, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row,
                                    uint32_t end, uint32_t* out) {
  return internal::game_fill(cache, device, force, list, texture, row, end, out);
}
#endif

}  // namespace twattila::animation_rows
