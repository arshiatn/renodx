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
#include <format>
#include <string>
#include <string_view>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"

// The game's Resolution Scale is one float in its renderer, the one the menu's slider sets: preferences id -> its getter and
// setter in the renderer's vtable, each a single movss. The scene reads it at each draw, so a write acts from the next frame; no
// exe patch, no fixed address: everything is found by the code that uses it, and anything else found hides the row.
namespace twwarhammer3::resolution_scale {

namespace internal {

constexpr const char* CONFIG_SECTION = "renodx";
constexpr const char* CONFIG_KEY = "UpscalingGameScale";

static uint8_t** renderer_global = nullptr;
static size_t getter_offset = 0;
static size_t setter_offset = 0;
static uint8_t* checked_vtable = nullptr;
static uint32_t value_offset = 0;
static std::atomic_bool found = false;
static bool searched = false;
// What the game had before the first write: in ReShade.ini, so it survives a crash or a mode kept over a restart.
static bool has_kept = false;
static float kept = 0.f;

// Bytes as FindPattern reads them.
inline std::string PatternOf(uint32_t value) {
  return std::format("{:02X} {:02X} {:02X} {:02X}", value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, value >> 24);
}

// The first `lea rdx, [rip + name]` followed within 64 bytes by `mov r9d, id` (the preference's constructor): its id, else 0.
inline uint32_t PreferenceId(std::string_view code, const uint8_t* name) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(code.data());
  for (size_t at = 0; at + 7 + 64 + 6 <= code.size(); ++at) {
    if (bytes[at] != 0x48 || bytes[at + 1] != 0x8D || bytes[at + 2] != 0x15) continue;
    int32_t displacement = 0;
    std::memcpy(&displacement, bytes + at + 3, sizeof(displacement));
    if (bytes + at + 7 + displacement != name) continue;
    for (size_t follow = at + 7; follow < at + 7 + 64; ++follow) {
      if (bytes[follow] != 0x41 || bytes[follow + 1] != 0xB9) continue;
      uint32_t id = 0;
      std::memcpy(&id, bytes + follow + 2, sizeof(id));
      return id;
    }
  }
  return 0;
}

inline void Find() {
  searched = true;
  std::array<char, 8> no_scale = {};
  if (GetEnvironmentVariableA("RENODX_TW_NO_SCALE", no_scale.data(), static_cast<DWORD>(no_scale.size())) != 0) {
    renodx::utils::log::i("[Warhammer III] Resolution Scale: not looked for (RENODX_TW_NO_SCALE).");
    return;
  }
  const auto sections = tw::engine_tweaks::ModuleSections(GetModuleHandleW(nullptr));
  if (sections.code.empty()) return;
  // The game's plain code is the first; the protection's section stays unread here.
  const std::vector<std::string_view> code = {sections.code.front()};
  const auto read_u32 = [](const uint8_t* at) {
    uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
  };
  const std::string fail = "[Warhammer III] Resolution Scale: not found, no Upscaling Mode: ";
  // "gfx_resolution_scale" with its end, as the preference's name.
  const auto* name = tw::engine_tweaks::FindPattern(sections.data, "67 66 78 5F 72 65 73 6F 6C 75 74 69 6F 6E 5F 73 63 61 6C 65 00");
  const uint32_t id = (name != nullptr) ? PreferenceId(code.front(), name) : 0;
  if (id == 0) {
    renodx::utils::log::i(fail, "its name or id");
    return;
  }
  const std::string id_bytes = PatternOf(id);
  // The save path: call [rax+getter]; movaps xmm6, xmm0; mov rax, [rsi]; mov edx, id; mov rcx, rsi; call [rax+78h]; movss [rax], xmm6.
  const auto* save = tw::engine_tweaks::FindPattern(code, "FF 90 ?? ?? ?? ?? 0F 28 F0 48 8B 06 BA " + id_bytes + " 48 8B CE FF 50 78 F3 0F 11 30");
  // The load path: mov rbx, [rax+setter]; mov rcx, [rip+?]; add rcx, ?; mov rax, [rcx]; mov edx, id; call [rax+78h]; movss xmm1, [rax].
  const auto* load = tw::engine_tweaks::FindPattern(code, "48 8B 98 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 81 C1 ?? ?? ?? ?? 48 8B 01 BA " + id_bytes + " FF 50 78 F3 0F 10 08");
  if (save == nullptr || load == nullptr) {
    renodx::utils::log::i(fail, "its save or load");
    return;
  }
  const uint32_t getter = read_u32(save + 2);
  const uint32_t setter = read_u32(load + 3);
  // The scene's read: mov rcx, [rip+renderer]; mov rax, [rcx]; call [rax+getter]; comiss xmm0, [rip+?]; movaps xmm1, xmm0; ja.
  auto* consumer = tw::engine_tweaks::FindPattern(code, "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 " + PatternOf(getter) + " 0F 2F 05 ?? ?? ?? ?? 0F 28 C8 77");
  if (consumer == nullptr) {
    renodx::utils::log::i(fail, "its reader");
    return;
  }
  int32_t displacement = 0;
  std::memcpy(&displacement, consumer + 3, sizeof(displacement));
  renderer_global = reinterpret_cast<uint8_t**>(consumer + 7 + displacement);
  getter_offset = getter;
  setter_offset = setter;
  found.store(true, std::memory_order_relaxed);
  float saved = 0.f;
  has_kept = reshade::get_config_value(nullptr, CONFIG_SECTION, CONFIG_KEY, saved);
  if (has_kept) kept = saved;
}

// The live float; null: no renderer yet, or its methods aren't the plain ones (the row hides).
inline float* Value() {
  if (!found.load(std::memory_order_relaxed)) return nullptr;
  auto* renderer = *renderer_global;
  if (renderer == nullptr) return nullptr;
  auto* vtable = *reinterpret_cast<uint8_t**>(renderer);
  if (vtable != checked_vtable) {
    auto* module = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
    const auto in_module = [&](const uint8_t* at) { return at >= module && at < module + nt->OptionalHeader.SizeOfImage; };
    const uint8_t* get = in_module(vtable) ? *reinterpret_cast<uint8_t**>(vtable + getter_offset) : nullptr;
    const uint8_t* set = in_module(vtable) ? *reinterpret_cast<uint8_t**>(vtable + setter_offset) : nullptr;
    constexpr std::array<uint8_t, 4> GET_BYTES = {0xF3, 0x0F, 0x10, 0x81};  // movss xmm0, [rcx+offset]; ret
    constexpr std::array<uint8_t, 4> SET_BYTES = {0xF3, 0x0F, 0x11, 0x89};  // movss [rcx+offset], xmm1; ret
    const bool plain = in_module(get) && in_module(set) && std::memcmp(get, GET_BYTES.data(), 4) == 0 && std::memcmp(set, SET_BYTES.data(), 4) == 0
                       && get[8] == 0xC3 && set[8] == 0xC3 && std::memcmp(get + 4, set + 4, 4) == 0;
    if (!plain) {
      found.store(false, std::memory_order_relaxed);
      renodx::utils::log::i("[Warhammer III] Resolution Scale: not found, no Upscaling Mode: its getter and setter aren't plain.");
      return nullptr;
    }
    std::memcpy(&value_offset, get + 4, sizeof(value_offset));
    checked_vtable = vtable;
    renodx::utils::log::i(std::format("[Warhammer III] Resolution Scale: found, renderer + {:X}: {}.", value_offset, *reinterpret_cast<float*>(renderer + value_offset)));
  }
  return reinterpret_cast<float*>(renderer + value_offset);
}

}  // namespace internal

// Whether the row can work.
inline bool Found() {
  return internal::found.load(std::memory_order_relaxed);
}

// Every present. `wanted` above 0: the scale, one write only if it differs; true when written. 0: the game's own: what it had back,
// once, then nothing is touched.
inline bool Drive(float wanted) {
  using namespace internal;
  if (!searched) Find();
  float* value = Value();
  if (value == nullptr) return false;
  if (wanted <= 0.f) {
    if (!has_kept) return false;
    const bool changed = *value != kept;
    *value = kept;
    has_kept = false;
    reshade::set_config_value(nullptr, CONFIG_SECTION, CONFIG_KEY, static_cast<const char*>(nullptr));
    renodx::utils::log::i(std::format("[Warhammer III] Resolution Scale: back to the game's {}.", kept));
    return changed;
  }
  if (!has_kept) {
    has_kept = true;
    kept = *value;
    reshade::set_config_value(nullptr, CONFIG_SECTION, CONFIG_KEY, kept);
    renodx::utils::log::i(std::format("[Warhammer III] Resolution Scale: the game's {} kept.", kept));
  }
  if (*value == wanted) return false;
  *value = wanted;
  return true;
}

// The live value; 0: not known.
inline float Current() {
  const float* value = internal::Value();
  return (value != nullptr) ? *value : 0.f;
}

// The game is closing: what it had, in case it saves the value after this.
inline void OnDestroyDevice(reshade::api::device*) {
  if (!internal::has_kept) return;
  if (float* value = internal::Value(); value != nullptr) *value = internal::kept;
}

}  // namespace twwarhammer3::resolution_scale
