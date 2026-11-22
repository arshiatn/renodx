/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <string_view>

#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "../../utils/settings.hpp"
#include "../twrome2/engine_tweaks.hpp"

// Pharaoh's Dead Unit Detail (Attila's: twattila/graphics_hooks.hpp OnImposterDistance). Pharaoh.exe inlines the
// imposter test in the per-soldier update (+0x6fe162): the soldier's state in r15 (its flags at +0x150, bit 0 set for
// the living: they follow the unit's alpha with ENTITY_TRACKER_ALPHA_CHANGE_SPEED, +0x6fdc94), its imposter distance
// in xmm0 (the display's distance for its size, +0x248, plus (random - 0.5) x the fade width, +0x25c; a mounted
// soldier's from its mount), then comiss xmm0, xmm10 (the camera's distance); jb: a flat sprite. That comiss and jb go
// through Detours to a stub that first multiplies a dead soldier's distance by the slider, then runs them (the
// trampoline Detours builds from them, its jb widened to the sprite path, then back after the jb). As the add-on's other
// code hooks (renodx::utils::detour): the game's threads are stopped while the 6 bytes change, and one inside them is
// moved out, so no thread runs half-written code.
namespace twpharaohdynasties::dead_units {

namespace internal {

constexpr std::string_view PATTERN =
    "49 8B 87 F0 00 00 00 49 8B 97 E0 00 00 00 48 85 C0 75 ?? 41 8B 47 14 F3 41 0F 10 87 08 01 00 00 F3 41 0F 5C C3 "
    "F3 0F 59 84 82 5C 02 00 00 F3 0F 58 84 82 48 02 00 00 EB ?? 48 8B 80 E0 00 00 00 8B 48 14 F3 0F 10 80 08 01 00 "
    "00 F3 41 0F 5C C3 F3 0F 59 84 8A 5C 02 00 00 F3 0F 58 84 8A 48 02 00 00 41 0F 2F C2 72 ??";
// comiss xmm0, xmm10; jb sprite: Detours' jump to the stub.
constexpr size_t TEST_OFFSET = 98;

// The stub (Detours' detour). The trampoline's address follows at TRAMPOLINE_OFFSET (Detours' pointer: the game's comiss
// until the commit, the trampoline after it), the scale at SCALE_OFFSET.
constexpr std::array<uint8_t, 24> STUB = {
    0x41, 0xF6, 0x87, 0x50, 0x01, 0x00, 0x00, 0x01,  // test byte ptr [r15 + 0x150], 1
    0x75, 0x08,                                      // jnz trampoline (living)
    0xF3, 0x0F, 0x59, 0x05, 0x0E, 0x00, 0x00, 0x00,  // mulss xmm0, [rip + 0xe] (the scale)
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,              // trampoline: jmp qword ptr [rip] (the game's comiss; jb)
};
constexpr size_t TRAMPOLINE_OFFSET = 24;
constexpr size_t SCALE_OFFSET = 32;

static uint8_t* stub = nullptr;  // while the hook is in (the memory stays after it: a soldier update may still be in it)
static std::atomic<float>* scale = nullptr;  // in the stub

// The hook as Detours takes it: the pointer it moves to the trampoline is the stub's own slot.
inline std::array<renodx::utils::detour::Function, 1> Hook() {
  return {renodx::utils::detour::Function(reinterpret_cast<void**>(stub + TRAMPOLINE_OFFSET), static_cast<void*>(stub))};
}

inline std::string Install(const tw::engine_tweaks::Sections& sections) {
  uint8_t* match = tw::engine_tweaks::FindPattern(sections.code, PATTERN);
  if (match == nullptr) return "dead units: the game's code was not found";
  uint8_t* game_test = match + TEST_OFFSET;
  DWORD size = 0;
  auto* memory = static_cast<uint8_t*>(DetourAllocateRegionWithinJumpBounds(game_test, &size));
  if (memory == nullptr) return "dead units: no memory near the game's code";
  std::memcpy(memory, STUB.data(), STUB.size());
  std::memcpy(memory + TRAMPOLINE_OFFSET, &game_test, sizeof(game_test));
  scale = new (memory + SCALE_OFFSET) std::atomic<float>(1.f);
  FlushInstructionCache(GetCurrentProcess(), memory, SCALE_OFFSET + sizeof(float));
  stub = memory;
  try {
    renodx::utils::detour::Install(Hook());
  } catch (const std::exception& error) {
    stub = nullptr;
    return std::string("dead units: the game's code could not be patched (") + error.what() + ")";
  }
  return "dead units patched";
}

}  // namespace internal

// Every present: patches Pharaoh.exe once (the result appended to `status`), then the slider.
inline void Update(float dead_scale, bool* installed, renodx::utils::settings::Setting* status) {
  if (!*installed) {
    *installed = true;
    const std::string result = internal::Install(tw::engine_tweaks::ModuleSections(GetModuleHandleW(nullptr)));
    status->label += "; " + result;
    renodx::utils::log::i("[Total War] Graphics: ", result);
  }
  if (internal::scale != nullptr) internal::scale->store(dead_scale, std::memory_order_relaxed);
}

// Addon detach: the game's code back (the stub stays: a soldier update may still be in it).
inline void Uninstall() {
  if (internal::stub == nullptr) return;
  try {
    renodx::utils::detour::Uninstall(internal::Hook());
  } catch (const std::exception& error) {
    renodx::utils::log::w(std::string("[Total War] Graphics: dead units: the game's code could not be put back (") + error.what() + ")");
    return;
  }
  internal::stub = nullptr;
}

}  // namespace twpharaohdynasties::dead_units
