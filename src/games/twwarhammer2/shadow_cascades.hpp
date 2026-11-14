/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../twrome2/engine_tweaks.hpp"

// Two Shadow Cascades Under Rays (Warhammer, Warhammer II), as Attila's (twattila/graphics_hooks.hpp shadow_map): with the
// rays' sun shadows on, the highest Shadows setting prepares the lowest setting's 2 cascades instead of 4. The shadow setup
// writes per Shadows setting (4 to 1) the map's size, its tile (the larger side of a cascade's cell: half the size for 3 or 4
// cascades in 2 x 2 cells, the whole size for 2 cascades side by side), the cascade count and the row of split distances the
// cascades read (+0x140 / +0x168, with the battle or campaign row: Warhammer II +0x7d7760, Warhammer +0x369e58). Setting 4's
// count, split row and tile are code constants written as setting 1's own (2 cascades, row 0, the tile the size) through
// engine_tweaks; the game reads them when it makes its shadows (start-up, Shadows changed). The patterns run from setting 4's
// writes to setting 1's, so setting 1's own values are checked byte for byte; another build or a mod's bytes: nothing found,
// nothing written. test/tw_shadow_cascades runs them over the shipped executables.
namespace twwarhammer2::shadow_cascades {

// Warhammer II (+0x7e157d, Warhammer2.exe): setting 4's size (8192.f, twice: width and height; Warhammer II's Shadow Map
// Size), tile (4096), count (4), split row (3) as immediates; settings 3 to 1 (size from the game's multiplier); setting 1:
// count 2, split row r13d (0), tile = size (both multiplier << 11).
inline constexpr std::string_view WARHAMMER_2_SETUP =
    "C7 83 D8 1D 00 00 ?? ?? ?? ?? C7 83 DC 1D 00 00 ?? ?? ?? ?? C7 83 5C 01 00 00 ?? ?? ?? ?? C7 83 60 01 00 00 ?? ?? ?? ?? "
    "C7 83 40 01 00 00 ?? ?? ?? ?? E9 D2 00 00 00 8B 05 ?? ?? ?? ?? 0F 57 C0 C1 E0 0C F3 48 0F 2A C0 F3 0F 11 83 D8 1D 00 00 "
    "F3 0F 11 83 DC 1D 00 00 8B 05 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? C7 83 60 01 00 00 04 00 00 00 C7 83 40 01 00 00 02 00 "
    "00 00 E9 81 00 00 00 8B 05 ?? ?? ?? ?? 0F 57 C0 C1 E0 0C F3 48 0F 2A C0 F3 0F 11 83 D8 1D 00 00 F3 0F 11 83 DC 1D 00 00 "
    "8B 05 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? C7 83 60 01 00 00 03 00 00 00 89 B3 40 01 00 00 EB 40 8B 05 ?? ?? ?? ?? 0F 57 "
    "C0 C1 E0 0B F3 48 0F 2A C0 F3 0F 11 83 D8 1D 00 00 F3 0F 11 83 DC 1D 00 00 8B 05 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? C7 "
    "83 60 01 00 00 02 00 00 00 44 89 AB 40 01 00 00 C1 E0 0B 89 83 5C 01 00 00";
inline constexpr size_t WARHAMMER_2_WIDTH = 6;
inline constexpr size_t WARHAMMER_2_HEIGHT = 16;

// Warhammer (+0x36d30f, warhammer.exe): setting 4's tile (4096 with the game's shadowmap_extreme_quality and an 8192
// texture limit, else 2048; the size 8192.f / 4096.f from constants), count (4), split row (3); settings 3 to 1 as
// immediates; setting 1: size 1024.f, tile 1024, count 2, split row r15d (0).
inline constexpr std::string_view WARHAMMER_SETUP =
    "41 81 7E 20 00 20 00 00 72 19 41 81 7E 24 00 20 00 00 72 0F F3 0F 10 0D ?? ?? ?? ?? B8 ?? ?? ?? ?? EB 0D F3 0F 10 0D ?? "
    "?? ?? ?? B8 ?? ?? ?? ?? 0F 28 D1 0F 14 D1 F2 0F 11 93 40 1B 00 00 89 83 84 01 00 00 C7 83 88 01 00 00 ?? ?? ?? ?? C7 83 "
    "68 01 00 00 ?? ?? ?? ?? 45 33 FF 48 8D 4D 28 C6 83 A0 01 00 00 00 BF 08 00 00 00 E8 ?? ?? ?? ?? 44 8D 77 32 3B C7 0F 87 "
    "27 01 00 00 4C 8D 05 ?? ?? ?? ?? 48 63 C8 41 8B 94 88 ?? ?? ?? ?? 49 03 D0 FF E2 C7 83 40 1B 00 00 00 00 00 45 C7 83 44 "
    "1B 00 00 00 00 00 45 C7 83 84 01 00 00 00 04 00 00 C7 83 88 01 00 00 04 00 00 00 C7 83 68 01 00 00 02 00 00 00 EB 91 C7 "
    "83 40 1B 00 00 00 00 00 45 C7 83 44 1B 00 00 00 00 00 45 C7 83 84 01 00 00 00 04 00 00 C7 83 88 01 00 00 03 00 00 00 89 "
    "B3 68 01 00 00 E9 5E FF FF FF 45 33 FF C7 83 40 1B 00 00 00 00 80 44 44 89 BB 68 01 00 00 C7 83 44 1B 00 00 00 00 80 44 "
    "C7 83 84 01 00 00 00 04 00 00 C7 83 88 01 00 00 02 00 00 00 E9 2A FF FF FF";

enum class Game : uint8_t {
  WARHAMMER,
  WARHAMMER_2,
};

// Engine options of the game's graphics options (engine_tweaks Update / Uninstall), set by Set every present.
struct Options {
  tw::engine_tweaks::Option count = {.scale = 0.f};       // 2 while on, else the game's (keep 0)
  tw::engine_tweaks::Option split_row = {.scale = -1.f};  // 0 while on, else the game's (keep -1)
  tw::engine_tweaks::Option tile;                         // the game's x 2 while on: as large as the map
};

// Setting 4's constants in `game`'s setup (in ConfigureAddon: the options hold vectors).
inline Options Make(Game game) {
  const bool warhammer_2 = (game == Game::WARHAMMER_2);
  const std::string_view setup = warhammer_2 ? WARHAMMER_2_SETUP : WARHAMMER_SETUP;
  Options options;
  options.count.tweaks = {{.code_pattern = setup, .code_offset = warhammer_2 ? 36u : 74u, .integer = true, .absolute = true, .keep = 0.f}};
  options.split_row.tweaks = {{.code_pattern = setup, .code_offset = warhammer_2 ? 46u : 84u, .integer = true, .absolute = true, .keep = -1.f}};
  if (warhammer_2) {
    options.tile.tweaks = {{.code_pattern = setup, .code_offset = 26, .integer = true}};
  } else {
    options.tile.tweaks = {{.code_pattern = setup, .code_offset = 29, .integer = true}, {.code_pattern = setup, .code_offset = 44, .integer = true}};
  }
  return options;
}

// Every present, before engine_tweaks::Update: `two_cascades` (the row on and the rays' sun shadows alone), `tile_scale`
// the scale of the game's tile without them (Warhammer II's Shadow Map Size; 1 the game's).
inline void Set(Options* options, bool two_cascades, float tile_scale) {
  options->count.scale = two_cascades ? 2.f : 0.f;
  options->split_row.scale = two_cascades ? 0.f : -1.f;
  options->tile.scale = two_cascades ? (tile_scale * 2.f) : tile_scale;
}

}  // namespace twwarhammer2::shadow_cascades
