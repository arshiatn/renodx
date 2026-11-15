/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

#include "../../utils/log.hpp"
#include "./engine.hpp"

// Shadow map resolution (graphics option): the sun's shadow cascades are 4096 x 4096 at the highest Shadows setting
// (2048 / 1500 below), from 4096 x the engine's tweak shadow_resolution_multiplier (int, 1; ShadowMapView.cpp). The
// game reads it when it creates the shadow map (+0x1a77b90: start-up, Shadows changed in its options): one depth
// texture array of 4 cascades. The highest setting's width and height are written into that code as a constant, so
// any size works, not only 4096 x n. Too large and the game quits ("Insufficient video memory to run game,
// terminating") and its next start takes the GPU database's defaults (1920 x 1080, TAA): 16384 (D3D11 cannot make the
// 4 GB array) and 14336 (ATN; D3D11 alone makes it), so 12288 at most.
namespace twshogun2::shadow_resolution {

namespace internal {

// mov eax, [rip + shadow_resolution_multiplier]; xorps xmm0, xmm0; shl eax, 12; cvtsi2ss xmm0, rax; call round;
// mov eax, [rip + shadow_resolution_multiplier]; movaps xmm6, xmm0; shl eax, 12; xorps xmm0, xmm0; cvtsi2ss xmm0, rax
// (the highest setting's width, then its height). +0x1a77c27 (2026-10-13 build).
constexpr std::string_view HIGHEST_SIZES =
    "8B 05 ?? ?? ?? ?? 0F 57 C0 C1 E0 0C F3 48 0F 2A C0 E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 0F 28 F0 C1 E0 0C 0F 57 C0 "
    "F3 48 0F 2A C0";
constexpr size_t CODE_SIZE = 42;
// The width: mov eax, size; xorps xmm0, xmm0; 4 nop (for the load, the xorps and the shift).
constexpr size_t WIDTH_LOAD = 0;
// The height: mov eax, size; nop (for the load), and 3 nop for the shift after the movaps.
constexpr size_t HEIGHT_LOAD = 22;
constexpr size_t HEIGHT_SHIFT = 31;

static uint8_t* code = nullptr;
static std::array<uint8_t, CODE_SIZE> original = {};
static uint32_t written = 0;  // 0: the game's code
static std::string status = "not searched";

}  // namespace internal

// Device creation (the engine module is loaded by then, before the game creates its shadow map).
inline void Install() {
  if (internal::code != nullptr) return;
  internal::code = engine::FindPattern(internal::HIGHEST_SIZES);
  if (internal::code != nullptr) {
    std::memcpy(internal::original.data(), internal::code, internal::CODE_SIZE);
    internal::status = std::format("found (+{:#x})", internal::code - reinterpret_cast<uint8_t*>(engine::Module()));
  } else {
    internal::status = "the game's shadow map code was not found";
  }
  renodx::utils::log::i("[Shogun 2 DLSS] Shadow map resolution: ", internal::status);
}

// The highest Shadows setting's shadow map size (0: the game's 4096) from the next time the game creates it.
inline void Set(uint32_t size) {
  if (internal::code == nullptr || size == internal::written) return;
  std::array<uint8_t, internal::CODE_SIZE> patched = internal::original;
  if (size != 0) {
    constexpr std::array<uint8_t, 7> WIDTH_TAIL = {0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90};  // xorps xmm0, xmm0; nop
    patched[internal::WIDTH_LOAD] = 0xB8;  // mov eax, imm32
    std::memcpy(&patched[internal::WIDTH_LOAD + 1], &size, sizeof(size));
    std::memcpy(&patched[internal::WIDTH_LOAD + 5], WIDTH_TAIL.data(), WIDTH_TAIL.size());
    patched[internal::HEIGHT_LOAD] = 0xB8;
    std::memcpy(&patched[internal::HEIGHT_LOAD + 1], &size, sizeof(size));
    patched[internal::HEIGHT_LOAD + 5] = 0x90;
    std::memset(&patched[internal::HEIGHT_SHIFT], 0x90, 3);
  }
  DWORD protection;
  if (VirtualProtect(internal::code, internal::CODE_SIZE, PAGE_EXECUTE_READWRITE, &protection) == FALSE) return;
  std::memcpy(internal::code, patched.data(), internal::CODE_SIZE);
  VirtualProtect(internal::code, internal::CODE_SIZE, protection, &protection);
  FlushInstructionCache(GetCurrentProcess(), internal::code, internal::CODE_SIZE);
  internal::written = size;
}

// Addon detach: the game's code back.
inline void Uninstall() {
  Set(0);
}

inline const std::string& Status() {
  return internal::status;
}

}  // namespace twshogun2::shadow_resolution
