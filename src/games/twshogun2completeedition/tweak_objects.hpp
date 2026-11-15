/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../twrome2/engine_tweaks.hpp"

// Shogun 2's engine tweaks (empire.retail.x64.dll; Remastered CE's CE.dll). A static initializer builds each object: its name
// into rdx (lea rdx, [name], or lea rdi, [name] and later mov rdx, rdi), its object into rcx, the constructor's call, then
//   lea rax, [vtable]; mov dword ptr [rip + object + 0xb8], <the default>; mov qword ptr [rip + object], rax
// (the value at +0xb8, game-facts.md). tw::engine_tweaks::Find reads an object before lea rdx, [name] with its value at
// +0x60: none here. A tweak counts only with both stores after the call, 0xb8 apart.
// tools\shogun2\pkg_1114\s2_tweaks.py does the same with capstone (s2_tweaks_1114.txt); test\tw_shogun2_tweaks runs this.
namespace twshogun2::tweak_objects {

// Attila's Graphics 2 of 2 rows (twattila/engine_tests.hpp) whose engine values Shogun 2 has (8 of their names, all
// floats: tools\shogun2\pkg_1114\engine_names_1114.txt; Rope Distance's rigid_rope_culling_threshold absent).
inline const std::vector<std::string> ATTILA_TESTS = {"GraphicsPlantSway", "GraphicsRopeDistance", "GraphicsShadowCascadeBlend",
                                                      "GraphicsCampaignSunCameraDepth", "GraphicsCampaignClouds"};

inline void Find(const tw::engine_tweaks::Sections& sections, std::span<tw::engine_tweaks::Option*> options) {
  std::unordered_map<std::string_view, std::vector<tw::engine_tweaks::Tweak*>> by_name;
  std::array<bool, 256> first_letters = {};
  size_t longest = 0;
  for (tw::engine_tweaks::Option* option : options) {
    for (tw::engine_tweaks::Tweak& tweak : option->tweaks) {
      if (tweak.value != nullptr || tweak.name.empty() || !tweak.code_pattern.empty()) continue;
      by_name[tweak.name].push_back(&tweak);
      first_letters[static_cast<uint8_t>(tweak.name.front())] = true;
      longest = std::max(longest, tweak.name.size());
    }
  }
  if (by_name.empty()) return;
  // The names: zero-terminated, after another string's terminator (as tw::engine_tweaks::Find reads them).
  std::unordered_map<const uint8_t*, std::vector<tw::engine_tweaks::Tweak*>*> by_address;
  for (const auto* memories : {&sections.data, &sections.code}) {
    for (const std::string_view memory : *memories) {
      for (size_t at = 1; at < memory.size(); ++at) {
        if (memory[at - 1] != '\0' || !first_letters[static_cast<uint8_t>(memory[at])]) continue;
        const std::string_view rest = memory.substr(at, longest + 1);
        const size_t length = rest.find('\0');
        if (length == std::string_view::npos) continue;
        if (const auto found = by_name.find(rest.substr(0, length)); found != by_name.end()) {
          by_address.emplace(reinterpret_cast<const uint8_t*>(memory.data() + at), &found->second);
        }
        at += length;
      }
    }
  }
  if (by_address.empty()) return;
  for (const std::string_view memory : sections.code) {
    auto* begin = reinterpret_cast<uint8_t*>(const_cast<char*>(memory.data()));
    uint8_t* end = begin + memory.size();
    // Every lea r64, [rip + x] (REX.W, 8D, ModRM mod 0 rm 5) whose x is a name.
    for (uint8_t* lea = begin; lea + 7 <= end; ++lea) {
      if ((lea[0] != 0x48 && lea[0] != 0x4C) || lea[1] != 0x8D || (lea[2] & 0xC7) != 0x05) continue;
      const auto found = by_address.find(tw::engine_tweaks::internal::RipTarget(lea + 3, lea + 7));
      if (found == by_address.end()) continue;
      // The constructor's call within 0x40 bytes; within 0x25 bytes after it the default's store (C7 05) and, 0xb8 before
      // that, the vtable's (48 89 05).
      uint8_t* value = nullptr;
      for (uint8_t* call = lea + 7; value == nullptr && call < lea + 0x40 && call + 5 + 0x25 + 10 <= end; ++call) {
        if (call[0] != 0xE8) continue;
        for (uint8_t* store = call + 5; value == nullptr && store < call + 5 + 0x25; ++store) {
          if (store[0] != 0xC7 || store[1] != 0x05) continue;
          uint8_t* stored = tw::engine_tweaks::internal::RipTarget(store + 2, store + 10);
          for (uint8_t* vtable = call + 5; vtable < call + 5 + 0x25; ++vtable) {
            if (vtable[0] == 0x48 && vtable[1] == 0x89 && vtable[2] == 0x05
                && tw::engine_tweaks::internal::RipTarget(vtable + 3, vtable + 7) + 0xB8 == stored) {
              value = stored;
              break;
            }
          }
        }
      }
      if (value == nullptr) continue;
      for (tw::engine_tweaks::Tweak* tweak : *found->second) {
        if (tweak->value == nullptr) tweak->value = value;
      }
    }
  }
}

}  // namespace twshogun2::tweak_objects
