// Two Shadow Cascades Under Rays (Experimental, off; Attila's row): Rome II's shadow quality switch (empire.retail.dll
// +0x142fcdd, one match from `dec esi` to the shared count write; code read, level B: tools/tw/rome2/p2r2_cascades.txt) sets
// the sun atlas's size (+0x924 / +0x928), cascade size (+0x84), cascade count (+0x88) and tier (+0x74) per
// gfx_shadow_quality: 1-2 two cascades tier 0, 3 three tier 1, 4 and 5 four tier 2 / 3. With Skip the Shadow Maps, 3-5's
// counts and tiers as 1-2's (their sizes kept), read when the game sets its shadows up: from the next start.
// Used by the addon (SetShadowCascades) and test/tw_unit_shadows part 8 (over the shipped DLL).
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "./engine_tweaks.hpp"

namespace twrome2::shadow_cascades {

constexpr std::string_view CODE =
    "4E 83 FE 04 0F 87 ?? ?? ?? ?? FF 24 B5 ?? ?? ?? ?? C7 87 24 09 00 00 00 00 00 45 C7 87 28 09 00 00 00 00 80 44 "
    "C7 87 84 00 00 00 00 04 00 00 C7 87 88 00 00 00 02 00 00 00 C7 47 74 00 00 00 00 E9 ?? ?? ?? ?? C7 87 24 09 00 00 "
    "00 00 00 45 C7 87 28 09 00 00 00 00 00 45 C7 87 84 00 00 00 00 04 00 00 C7 87 88 00 00 00 03 00 00 00 C7 47 74 01 "
    "00 00 00 E9 ?? ?? ?? ?? C7 87 24 09 00 00 00 00 80 45 C7 87 28 09 00 00 00 00 80 45 C7 87 84 00 00 00 00 08 00 00 "
    "C7 47 74 02 00 00 00 EB ?? 80 3D ?? ?? ?? ?? 00 74 ?? 84 DB 75 ?? 81 F9 00 20 00 00 72 ?? 3D 00 20 00 00 72 ?? F3 "
    "0F 10 05 ?? ?? ?? ?? B8 00 10 00 00 EB ?? F3 0F 10 05 ?? ?? ?? ?? B8 00 08 00 00 F3 0F 11 87 24 09 00 00 F3 0F 11 "
    "87 28 09 00 00 89 87 84 00 00 00 C7 47 74 03 00 00 00 C7 87 88 00 00 00 04 00 00 00";
constexpr size_t CODE_SIZE = 255;
// 3's count and tier, 4's tier, 5's tier, the count 4 and 5 share.
constexpr std::array<size_t, 5> OFFSETS = {105, 112, 154, 241, 251};
constexpr std::array<uint32_t, 5> GAME = {3, 1, 2, 3, 4};
constexpr std::array<uint32_t, 5> TWO = {2, 0, 0, 0, 2};

// The switch in a module's code sections with the game's five values, else nullptr (not found, or another mod's).
inline uint8_t* Find(const std::vector<std::string_view>& code) {
  uint8_t* found = tw::engine_tweaks::FindPattern(code, CODE);
  for (size_t index = 0; found != nullptr && index < OFFSETS.size(); ++index) {
    uint32_t value;
    std::memcpy(&value, found + OFFSETS[index], sizeof(value));
    if (value != GAME[index]) found = nullptr;
  }
  return found;
}

// 3-5's counts and tiers as 1-2's where `two`, else the game's; `found` writable.
inline void Write(uint8_t* found, bool two) {
  const auto& values = two ? TWO : GAME;
  for (size_t index = 0; index < OFFSETS.size(); ++index) {
    std::memcpy(found + OFFSETS[index], &values[index], sizeof(values[index]));
  }
}

}  // namespace twrome2::shadow_cascades
