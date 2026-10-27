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
// soldier's from its mount), then comiss xmm0, xmm10 (the camera's distance); jb: a flat sprite. That comiss and jb
// become a jump to a stub that first multiplies a dead soldier's distance by the slider.
namespace twpharaohdynasties::dead_units {

namespace internal {

constexpr std::string_view PATTERN =
    "49 8B 87 F0 00 00 00 49 8B 97 E0 00 00 00 48 85 C0 75 ?? 41 8B 47 14 F3 41 0F 10 87 08 01 00 00 F3 41 0F 5C C3 "
    "F3 0F 59 84 82 5C 02 00 00 F3 0F 58 84 82 48 02 00 00 EB ?? 48 8B 80 E0 00 00 00 8B 48 14 F3 0F 10 80 08 01 00 "
    "00 F3 41 0F 5C C3 F3 0F 59 84 8A 5C 02 00 00 F3 0F 58 84 8A 48 02 00 00 41 0F 2F C2 72 ??";
// comiss xmm0, xmm10; jb sprite: replaced by jmp stub; nop.
constexpr size_t TEST_OFFSET = 98;
constexpr size_t TEST_SIZE = 6;

// The stub; its two jumps' offsets are filled in, the scale follows at SCALE_OFFSET.
constexpr std::array<uint8_t, 34> STUB = {
    0x41, 0xF6, 0x87, 0x50, 0x01, 0x00, 0x00, 0x01,  // test byte ptr [r15 + 0x150], 1
    0x75, 0x08,                                      // jnz compare (living)
    0xF3, 0x0F, 0x59, 0x05, 0x12, 0x00, 0x00, 0x00,  // mulss xmm0, [rip + 0x12] (the scale)
    0x41, 0x0F, 0x2F, 0xC2,                          // compare: comiss xmm0, xmm10
    0x72, 0x05,                                      // jb sprite_jump
    0xE9, 0x00, 0x00, 0x00, 0x00,                    // jmp back (after the game's jb)
    0xE9, 0x00, 0x00, 0x00, 0x00,                    // sprite_jump: jmp (the game's jb target)
};
constexpr size_t BACK_JUMP = 24;
constexpr size_t SPRITE_JUMP = 29;
constexpr size_t SCALE_OFFSET = 36;

static uint8_t* test = nullptr;  // the game's comiss, while patched
static std::array<uint8_t, TEST_SIZE> original = {};
static std::atomic<float>* scale = nullptr;  // in the stub

// A 5-byte jmp at `code` (executed at `at`) to `target`.
inline void WriteJump(uint8_t* code, const uint8_t* at, const uint8_t* target) {
  const auto offset = static_cast<int32_t>(target - (at + 5));
  code[0] = 0xE9;
  std::memcpy(code + 1, &offset, sizeof(offset));
}

inline std::string Install(const tw::engine_tweaks::Sections& sections) {
  uint8_t* match = tw::engine_tweaks::FindPattern(sections.code, PATTERN);
  if (match == nullptr) return "dead units: the game's code was not found";
  uint8_t* game_test = match + TEST_OFFSET;
  const uint8_t* sprite = game_test + TEST_SIZE + static_cast<int8_t>(game_test[TEST_SIZE - 1]);
  DWORD size = 0;
  auto* stub = static_cast<uint8_t*>(DetourAllocateRegionWithinJumpBounds(game_test, &size));
  if (stub == nullptr) return "dead units: no memory near the game's code";
  std::memcpy(stub, STUB.data(), STUB.size());
  WriteJump(stub + BACK_JUMP, stub + BACK_JUMP, game_test + TEST_SIZE);
  WriteJump(stub + SPRITE_JUMP, stub + SPRITE_JUMP, sprite);
  scale = new (stub + SCALE_OFFSET) std::atomic<float>(1.f);
  FlushInstructionCache(GetCurrentProcess(), stub, STUB.size());

  std::array<uint8_t, TEST_SIZE> jump = {};
  WriteJump(jump.data(), game_test, stub);
  jump[5] = 0x90;  // nop
  DWORD protection;
  if (VirtualProtect(game_test, TEST_SIZE, PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
    return "dead units: the game's code could not be patched";
  }
  std::memcpy(original.data(), game_test, TEST_SIZE);
  std::memcpy(game_test, jump.data(), TEST_SIZE);
  VirtualProtect(game_test, TEST_SIZE, protection, &protection);
  FlushInstructionCache(GetCurrentProcess(), game_test, TEST_SIZE);
  test = game_test;
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
  if (internal::test == nullptr) return;
  DWORD protection;
  if (VirtualProtect(internal::test, internal::TEST_SIZE, PAGE_EXECUTE_READWRITE, &protection) == FALSE) return;
  std::memcpy(internal::test, internal::original.data(), internal::TEST_SIZE);
  VirtualProtect(internal::test, internal::TEST_SIZE, protection, &protection);
  FlushInstructionCache(GetCurrentProcess(), internal::test, internal::TEST_SIZE);
  internal::test = nullptr;
}

}  // namespace twpharaohdynasties::dead_units
