/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "../twattila/engine_tests.hpp"

// Graphics 2 of 2's tests: the game's engine values (engine_values.txt lists every one, each by its constructor) that have a
// use as a slider and aren't in Graphics (Experimental). Its flags as switches (Game's, Off, On): Apply writes one only away
// from the game's value, so Game's writes nothing (Find lands on each flag's byte, the address its readers compare: find_emu.py
// against the census's readers). Not here: its preferences (read at start; their value is at + 0x60, where Find doesn't land),
// flags the game's own options set (gfx_cloth_simulation's enable_cloth_sim), and the tree tests whose effect is unknown.
namespace more_tests {

inline std::vector<twattila::engine_tests::EngineTest> MoreTests() {
  using twattila::engine_tests::GRASS;
  using twattila::engine_tests::SHADOWS;
  using twattila::engine_tests::Switch;
  using twattila::engine_tests::UNITS;
  // A shadow map's reach: the game uses the value when it is above 0, else its own table for the shadow quality, so a
  // factor on it would do nothing while it is 0.
  const auto shadow_reach = [](const std::string& key, const std::string& label, std::string_view name, float max) {
    return twattila::engine_tests::EngineTest{
        .section = SHADOWS,
        .key = key,
        .label = label,
        .tooltip = "How far this sun shadow map reaches. Keep each above the one before it. 0: the game's.",
        .min = 0.f,
        .max = max,
        .format = "%.0f m",
        .option = {.scale = 0.f, .tweaks = {{.name = name, .absolute = true}}},
    };
  };
  return {
      {.section = GRASS,
       .key = "GraphicsTest_grass_scale",
       .label = "Grass Size (Experimental)",
       .tooltip = "How big grass is drawn. Changes the look.",
       .max = 4.f,
       .option = {.tweaks = {{.name = "grass_scale"}}}},
      {.section = GRASS,
       .key = "GraphicsTest_grass_scale_y",
       .label = "Grass Height (Experimental)",
       .tooltip = "How tall grass is drawn. Changes the look.",
       .max = 4.f,
       .option = {.tweaks = {{.name = "grass_scale_y"}}}},
      {.section = GRASS,
       .key = "GraphicsTest_gbuffer_blend_edges_amount_grass",
       .label = "Grass Edge Blend",
       .tooltip = "How much the edges of grass blend with what is behind them. 0: none.",
       .min = 0.f,
       .max = 2.f,
       .option = {.tweaks = {{.name = "gbuffer_blend_edges_amount_grass_battle"}, {.name = "gbuffer_blend_edges_amount_grass_campaign"}}}},
      {.section = GRASS,
       .key = "GraphicsTest_gbuffer_blend_edges_amount",
       .label = "Edge Blend",
       .tooltip = "The same for everything but grass. 0: none.",
       .min = 0.f,
       .max = 2.f,
       .option = {.tweaks = {{.name = "gbuffer_blend_edges_amount_battle"}, {.name = "gbuffer_blend_edges_amount_campaign"}}}},
      // Attila's key for its far trees' shadow switch (enable_tree_display_billboard_shadow_capsules there).
      Switch(GRASS, "GraphicsFarTreeShadowShapes", "Far Tree Shadows", "Flat far trees, drawn in blocks, cast shadows.",
             "enable_billboard_block_shadows"),

      shadow_reach("GraphicsTest_battle_shadow_split_0", "Battle Shadow Map 1", "battle_shadow_split_0", 500.f),
      shadow_reach("GraphicsTest_battle_shadow_split_1", "Battle Shadow Map 2", "battle_shadow_split_1", 1000.f),
      shadow_reach("GraphicsTest_battle_shadow_split_2", "Battle Shadow Map 3", "battle_shadow_split_2", 4000.f),
      shadow_reach("GraphicsTest_battle_shadow_split_3", "Battle Shadow Map 4", "battle_shadow_split_3", 8000.f),
      shadow_reach("GraphicsTest_campaign_shadow_split_0", "Campaign Shadow Map 1", "campaign_shadow_split_0", 100.f),
      shadow_reach("GraphicsTest_campaign_shadow_split_1", "Campaign Shadow Map 2", "campaign_shadow_split_1", 200.f),
      shadow_reach("GraphicsTest_campaign_shadow_split_2", "Campaign Shadow Map 3", "campaign_shadow_split_2", 400.f),
      shadow_reach("GraphicsTest_campaign_shadow_split_3", "Campaign Shadow Map 4", "campaign_shadow_split_3", 800.f),
      // Not to 0: the game skips its limit when it is 0, and every light would cast shadows.
      {.section = SHADOWS,
       .key = "GraphicsTest_MAX_NUM_SHADOW_CASTING_POINT_LIGHTS",
       .label = "Point Lights With Shadows",
       .tooltip = "How many of the nearest torches and fires cast shadows. Heavier.",
       .min = 0.5f,
       .max = 2.f,
       .option = {.tweaks = {{.name = "MAX_NUM_SHADOW_CASTING_POINT_LIGHTS", .integer = true}}}},
      Switch(SHADOWS, "GraphicsTest_ENABLE_POINT_LIGHT_SHADOWS", "Point Light Shadow Maps",
             "Torches and fires cast the game's own shadows.", "ENABLE_POINT_LIGHT_SHADOWS"),
      Switch(SHADOWS, "GraphicsTest_ENABLE_DYNAMIC_SHADOWS_FOR_INCIDENTAL_DESTRUCTION", "Debris Shadows",
             "Pieces of destroyed buildings and walls cast moving shadows.", "ENABLE_DYNAMIC_SHADOWS_FOR_INCIDENTAL_DESTRUCTION"),

      {.section = UNITS,
       .key = "GraphicsTest_cloth_smoothing_power",
       .label = "Cloth Smoothing",
       .tooltip = "How much the game smooths cloth's movement.",
       .max = 4.f,
       .option = {.tweaks = {{.name = "cloth_smoothing_power"}}}},
      {.section = UNITS,
       .key = "GraphicsTest_SIMPLE_RAG_DOLL_MAX_ACTIVE",
       .label = "Ragdoll Limit",
       .tooltip = "How many falling bodies move as ragdolls at once. Next battle.",
       .max = 2.f,
       .option = {.tweaks = {{.name = "SIMPLE_RAG_DOLL_MAX_ACTIVE", .integer = true}}}},
      Switch(UNITS, "GraphicsTest_ENABLE_SIMPLE_RAG_DOLL_UPDATE", "Ragdolls", "Falling bodies move as ragdolls.",
             "ENABLE_SIMPLE_RAG_DOLL_UPDATE"),
      // gi_enabled has no row and is never written: back from 0 in a battle, the game's GI stage runs without its textures
      // and the game crashes (game_gi.hpp).
  };
}

}  // namespace more_tests
