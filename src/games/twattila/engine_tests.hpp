/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"
#include "../../utils/settings.hpp"
#include "../twrome2/engine_tweaks.hpp"

// Graphics 2 of 2's test sections (ATN: every graphics value worth a try), for the Total War addons: each slider scales
// some of the game's own engine values (1x: the game's) or switches one of its flags (Game's / Off / On). Attila's list
// (tools\tw\tw_tweak_uses32.py: every graphics engine value its code reads) is Thrones of Britannia's too.
namespace twattila::engine_tests {

struct EngineTest {
  std::string section;
  std::string key;
  std::string label;
  std::string tooltip;
  float min = 0.25f;
  float max = 8.f;
  std::vector<std::string> labels;  // choices instead: 2^(index - 1) x the game's value, the second the game's
  std::string format = "%.2fx";
  bool at_start = false;             // read once as the game starts: written right after the game sets it (restart)
  tw::engine_tweaks::Option option;  // its scale starts as the slider's default
};

inline const std::string GRASS = "Grass & Trees (Tests)";
inline const std::string DISTANCES = "Draw Distance & Detail (Tests)";
inline const std::string SHADOWS = "Shadows (Tests)";
inline const std::string SEA = "Sea (Tests)";
inline const std::string EFFECTS = "Effects (Tests)";
inline const std::string UNITS = "Units (Tests)";
inline const std::string CAMPAIGN = "Campaign Map (Tests)";
inline const std::string AUDIO = "Audio Memory (Tests, restart the game)";

// A switch of one of the game's flags.
inline EngineTest Switch(const std::string& section, const std::string& key, const std::string& label,
                         const std::string& tooltip, std::string_view name) {
  return EngineTest{
      .section = section,
      .key = key,
      .label = label,
      .tooltip = tooltip,
      .option = {.scale = 0.f, .tweaks = {{.name = name, .flag = true, .three_way = true}}},
  };
}

// Attila's (and Thrones of Britannia's) tests, the game's values in the tooltips. Most are read when a battle or the
// campaign loads.
inline std::vector<EngineTest> Attila() {
  return {
      {.section = GRASS,
       .key = "GraphicsSoftGrassDistance",
       .label = "Soft Grass Distance",
       .tooltip = "Grass is drawn soft (blended) up to here, cut out beyond (thin blades vanish).\n"
                  "Heavier. Game: 25 m.",
       .min = 0.f,
       .max = 16.f,
       .option = {.tweaks = {{.name = "OIT_GRASS_MAX_DISTANCE"}, {.name = "AOIT_GRASS_MAX_DISTANCE"}}}},
      {.section = GRASS,
       .key = "GraphicsSoftLeavesDistance",
       .label = "Soft Leaves Distance",
       .tooltip = "Tree leaves are drawn soft (blended) up to here, cut out beyond. Heavier. Game: 25 m.",
       .min = 0.f,
       .max = 16.f,
       .option = {.tweaks = {{.name = "OIT_TREE_MAX_DISTANCE"}, {.name = "AOIT_TREE_MAX_DISTANCE"}}}},
      {.section = GRASS,
       .key = "GraphicsGrassOpacity",
       .label = "Grass Opacity",
       .tooltip = "Cut-out grass: how much of each blade stays. Higher: fuller GRASS far away. Game: 2.5.",
       .max = 4.f,
       .option = {.tweaks = {{.name = "grass_alpha_mul"}}}},
      {.section = GRASS,
       .key = "GraphicsFarGrassOffset",
       .label = "Far Grass Offset",
       .tooltip = "A far GRASS value of the game's. 0: none. Game: -0.5.",
       .min = 0.f,
       .max = 4.f,
       .option = {.tweaks = {{.name = "grass_far_addition"}}}},
      {.section = GRASS,
       .key = "GraphicsGrassCellDistance",
       .label = "Grass Cell Distance",
       .tooltip = "How far GRASS cells are kept. Next battle. Game: 200 m.",
       .option = {.tweaks = {{.name = "scrub_lod_distance_default", .integer = true}}}},
      {.section = GRASS,
       .key = "GraphicsGrassSlopes",
       .label = "Grass on Slopes",
       .tooltip = "How steep ground still gets grass. Game: 0.7.",
       .min = 0.f,
       .max = 2.f,
       .option = {.tweaks = {{.name = "GRASS_ANGLE_CULL_AMOUNT"}}}},
      {.section = GRASS,
       .key = "GraphicsPlantSway",
       .label = "Plant Wind Sway",
       .tooltip = "How much plants and trees bend in the wind. Game: 0.91 / 0.08.",
       .min = 0.f,
       .max = 4.f,
       .option = {.tweaks = {{.name = "vegetation_hstrength"}, {.name = "vegetation_vstrength"}}}},
      {.section = GRASS,
       .key = "GraphicsTreeLodScale",
       .label = "Tree LOD Scale",
       .tooltip = "Trees' detail by their size on screen (with Projected Tree LODs on). Game: 1.35.",
       .option = {.tweaks = {{.name = "tree_lod_scaler"}}}},
      Switch(GRASS, "GraphicsProjectedTreeLods", "Projected Tree LODs",
             "Trees' detail by their size on screen instead of their distance.", "tree_lodding_method"),
      Switch(GRASS, "GraphicsFarTreeBlocks", "Far Tree Blocks", "Far trees drawn as merged flat blocks.",
             "use_tree_billboard_blocks"),
      Switch(GRASS, "GraphicsVegetationBlocks", "Vegetation Blocks", "Plants grouped and drawn in blocks.",
             "vegetation_block_enable"),
      Switch(GRASS, "GraphicsBillboardsLastFrame", "Far Trees From Last Frame",
             "Flat far trees reuse the last frame's.", "vegetation_billboard_use_previous_frame"),
      Switch(GRASS, "GraphicsFarTreeShadowShapes", "Far Tree Shadow Shapes",
             "Flat far trees cast SHADOWS from simple shapes.", "enable_tree_display_billboard_shadow_capsules"),

      {.section = DISTANCES,
       .key = "GraphicsObjectDetailDistances",
       .label = "Object Detail Distances",
       .tooltip = "Battle objects keep their detailed versions farther. Next battle. Game: 5 / 10 / 30 / 100 m.",
       .option = {.tweaks = {{.name = "battle_entity_lod0_distance"},
                             {.name = "battle_entity_lod1_distance"},
                             {.name = "battle_entity_lod2_distance"},
                             {.name = "battle_entity_lod3_distance"}}}},
      {.section = DISTANCES,
       .key = "GraphicsObjectSizeFactors",
       .label = "Object Size Factors",
       .tooltip = "The objects' distance factors, by size. Next battle. Game: 1 / 1 / 1.2 / 30.",
       .option = {.tweaks = {{.name = "battle_entity_distance_scalar"},
                             {.name = "battle_entity_small_distance_scalar"},
                             {.name = "battle_entity_medium_distance_scalar"},
                             {.name = "battle_entity_large_distance_scalar"}}}},
      {.section = DISTANCES,
       .key = "GraphicsBigBuildingDetail",
       .label = "Big Building Detail",
       .tooltip = "Big buildings keep detail farther (their detail DISTANCES grow with size). Next battle.",
       .option = {.tweaks = {{.name = "battle_building_lod_scale_nominal_size", .inverse = true}}}},
      {.section = DISTANCES,
       .key = "GraphicsRopeDistance",
       .label = "Rope Distance",
       .tooltip = "Ropes (ships, siege engines) drawn farther. Game: 200 m.",
       .option = {.tweaks = {{.name = "ROPE_DISTANCE"},
                             {.name = "ROPE_DISTANCE_FADEOUT"},
                             {.name = "rigid_rope_culling_threshold"}}}},
      {.section = DISTANCES,
       .key = "GraphicsClippingPlaneDistance",
       .label = "Clipping Plane Distance",
       .tooltip = "A clipping distance of the game's (use unknown). Game: 120 m.",
       .option = {.tweaks = {{.name = "clipping_plane_distance"}}}},
      Switch(DISTANCES, "GraphicsObjectsNeverVanish", "Objects Never Vanish",
             "Objects stay drawn (their last version) instead of vanishing far away.",
             "battle_entity_always_draw_last_lod"),
      Switch(DISTANCES, "GraphicsBuildingsNeverVanish", "Buildings Never Vanish",
             "Buildings stay drawn (their last version) instead of vanishing far away.",
             "battle_building_always_draw_last_lod"),
      Switch(DISTANCES, "GraphicsBuildingCulling", "Building Distance Culling",
             "Off: buildings are never culled by distance.", "battle_building_no_draw_enable"),
      Switch(DISTANCES, "GraphicsBuildingLodsBySize", "Building Detail by Size",
             "Buildings' detail DISTANCES grow with their size.", "battle_building_enable_lod_size_scale"),
      Switch(DISTANCES, "GraphicsFixedBuildingLods", "Fixed Building Detail Range",
             "A fixed range for buildings' detail distances.", "battle_building_fix_lod_range"),
      Switch(DISTANCES, "GraphicsFarBuildings", "Far Buildings", "The game's far buildings.", "far_buildings"),

      {.section = SHADOWS,
       .key = "GraphicsShadowDepthBias",
       .label = "Shadow Depth Bias",
       .tooltip = "Pushes sun SHADOWS off surfaces. Lower: fuller, closer SHADOWS (may speckle);\n"
                  "higher: detached. Game: 2500.",
       .min = 0.f,
       .max = 4.f,
       .option = {.tweaks = {{.name = "g_new_depth_bias"}}}},
      {.section = SHADOWS,
       .key = "GraphicsShadowSlopeBias",
       .label = "Shadow Slope Bias",
       .tooltip = "The same on slopes. Game: 1.8.",
       .min = 0.f,
       .max = 4.f,
       .option = {.tweaks = {{.name = "g_new_slope_scale_depth_bias"}}}},
      {.section = SHADOWS,
       .key = "GraphicsShadowCascadeBlend",
       .label = "Shadow Cascade Blend",
       .tooltip = "Blend width between the near and far shadow maps. Game: 0.1 / 0.2.",
       .min = 0.f,
       .max = 4.f,
       .option = {.tweaks = {{.name = "shadow_split_blend_fraction"}, {.name = "final_shadow_split_blend_fraction"}}}},
      Switch(SHADOWS, "GraphicsSpriteShadows", "Sprite Soldier Shadows", "Flat far soldiers cast shadows.",
             "entity_display_enable_imposter_shadows"),
      Switch(SHADOWS, "GraphicsWarpedShadows", "Warped Shadows", "The game's warped shadow maps.", "warp_shadows"),
      Switch(SHADOWS, "GraphicsNewShadows", "New Shadows", "The game's newer sun shadows.", "new_shadows"),
      Switch(SHADOWS, "GraphicsTerrainShadows", "Terrain Shadows", "Hills cast shadows.",
             "terrain_enable_shadows"),
      Switch(SHADOWS, "GraphicsCutsceneShadows", "Cutscene Shadows", "Shadows in cutscenes.", "cindy_shadows"),
      Switch(SHADOWS, "GraphicsCutscenePointShadows", "Cutscene Point Light Shadows",
             "Torches and fires cast SHADOWS in cutscenes.", "CINDY_POINT_LIGHT_SHADOWS_ENABLED"),
      Switch(SHADOWS, "GraphicsCutscenePointShadowsHigh", "Cutscene Point Light Shadows: High",
             "Their high quality.", "CINDY_POINT_LIGHT_SHADOW_QUALITY_HIGH"),

      {.section = EFFECTS,
       .key = "GraphicsEffectsDistance",
       .label = "Effects Draw Distance",
       .tooltip = "How far EFFECTS (smoke, fire, dust) are drawn. Game: 80 m.",
       .option = {.tweaks = {{.name = "render_vfx_far_distance"}}}},
      {.section = EFFECTS,
       .key = "GraphicsBloodBuildUp",
       .label = "Blood Build-Up",
       .tooltip = "How fast soldiers get bloody. Game: 0.25.",
       .min = 0.f,
       .max = 4.f,
       .option = {.tweaks = {{.name = "BLOOD_INTENSITY_INCREMENT"}}}},
      Switch(EFFECTS, "GraphicsCollapseEffects", "Building Collapse Effects",
             "Dust and debris when buildings collapse.", "building_destruction_emitters"),
      Switch(EFFECTS, "GraphicsCollapseAnimations", "Building Collapse Animations",
             "Buildings collapse with animations.", "building_destruction_anims"),
      Switch(EFFECTS, "GraphicsSortParticles", "Sort Particles", "Particles drawn back to front.",
             "VFX_SORT_PARTICLES"),
      Switch(EFFECTS, "GraphicsNightLights", "Night Lights", "Lights in buildings at night.",
             "ENABLE_NIGHT_LIGHTS"),
      {.section = EFFECTS,
       .key = "GraphicsTest_VFX_WIND_SCALE_FACTOR",
       .label = "Effects Wind",
       .tooltip = "How strongly the wind pushes smoke, dust and other effects. Game: 20.",
       .min = 0.f,
       .max = 4.f,
       .option = {.tweaks = {{.name = "VFX_WIND_SCALE_FACTOR"}}}},

      {.section = UNITS,
       .key = "GraphicsSharedPoseScale",
       .label = "Shared Pose Scale",
       .tooltip = "A factor on where soldiers start copying each other's moves. Game: 1.",
       .option = {.tweaks = {{.name = "entity_display_shared_pose_scale"}}}},
      Switch(UNITS, "GraphicsSpriteVariety", "Sprite Variety for All",
             "Every unit's flat far soldiers get their varied looks.",
             "entity_display_use_imposter_variants_for_all"),
      Switch(UNITS, "GraphicsHeadTracking", "Head Tracking", "Soldiers turn their heads to look around.",
             "HEAD_TRACKING"),
      Switch(UNITS, "GraphicsFastSoldierCulling", "Fast Soldier Culling",
             "Soldiers outside the view skipped early. Off: heavier.", "enable_entity_fast_cull"),
      Switch(UNITS, "GraphicsTest_force_unit_imposters", "All Soldiers as Sprites",
             "Every soldier drawn as a flat sprite: a pixelated, retro look.", "force_unit_imposters"),

      {.section = CAMPAIGN,
       .key = "GraphicsCampaignSunCameraDepth",
       .label = "Campaign Sun Camera Depth",
       .tooltip = "How far towards the sun CAMPAIGN SHADOWS take in objects. Game: 14.",
       .option = {.tweaks = {{.name = "shadow_cam_depth_offset_campaign"}}}},
      {.section = CAMPAIGN,
       .key = "GraphicsCampaignClouds",
       .label = "Campaign Cloud Distance",
       .tooltip = "The CAMPAIGN clouds' cull factor. Game: 2.",
       .option = {.tweaks = {{.name = "cull_clouds_scale"}}}},
      Switch(CAMPAIGN, "GraphicsCampaignTreesNeverVanish", "Campaign Trees Never Vanish",
             "Campaign trees stay drawn (their last version) far away.", "campaign_tree_always_draw_last_lod"),
      Switch(CAMPAIGN, "GraphicsCampaignPlantsNeverVanish", "Campaign Plants Never Vanish",
             "Campaign plants stay drawn (their last version) far away.", "campaign_grass_always_draw_last_lod"),

      {.section = SEA,
       .key = "GraphicsTest_sea_wind_speed",
       .label = "Sea Wind Speed",
       .tooltip = "The wind that drives the sea's waves. Game: 10.",
       .min = 0.f,
       .max = 4.f,
       .option = {.tweaks = {{.name = "sea_wind_speed"}}}},
      {.section = SEA,
       .key = "GraphicsTest_campaign_sea_transparency_override",
       .label = "Campaign Sea Transparency",
       .tooltip = "Sets the CAMPAIGN sea's transparency. -1: the game's.",
       .min = -1.f,
       .max = 10.f,
       .format = "%.3f",
       .option = {.scale = -1.f, .tweaks = {{.name = "campaign_sea_transparency_override", .absolute = true, .keep = -1.f}}}},
      {.section = SEA,
       .key = "GraphicsTest_campaign_sea_uv_scale",
       .label = "Campaign Sea Pattern Scale",
       .tooltip = "Sets the size of the CAMPAIGN sea's pattern. -1: the game's.",
       .min = -10.f,
       .max = 10.f,
       .format = "%.3f",
       .option = {.scale = -1.f, .tweaks = {{.name = "campaign_sea_uv_scale", .absolute = true, .keep = -1.f}}}},

      // Wwise's memory pools (system RAM in the game's 32-bit address space), read once as the game's AUDIO starts:
      // sizes, and how full a pool gets before Wwise drops sounds (low memory mode).
      {.section = AUDIO,
       .key = "AudioDefaultMemory",
       .label = "Sound Memory",
       .tooltip = "The AUDIO engine's main memory. Restart the game. Game: 23 MB.",
       .min = 1.f,
       .max = 4.f,
       .at_start = true,
       .option = {.tweaks = {{.name = "SOUND_DEFAULT_BUFFER_SIZE_IN_KB", .integer = true}}}},
      {.section = AUDIO,
       .key = "AudioDefaultMemoryLimit",
       .label = "Sound Memory Fill Limit",
       .tooltip = "How full it gets before sounds are dropped. Restart the game. Game: 90%.",
       .min = 0.5f,
       .max = 1.11f,
       .at_start = true,
       .option = {.tweaks = {{.name = "SOUND_DEFAULT_BUFFER_LOW_RATIO"}}}},
      {.section = AUDIO,
       .key = "AudioVoiceMemory",
       .label = "Voice Memory",
       .tooltip = "Memory for the sounds playing at once: too small, and sounds cut out in big fights.\n"
                  "Restart the game. Game: 13 MB.",
       .min = 1.f,
       .max = 8.f,
       .at_start = true,
       .option = {.tweaks = {{.name = "SOUND_LOWER_ENGINE_DEFAULT_BUFFER_SIZE_IN_KB", .integer = true}}}},
      {.section = AUDIO,
       .key = "AudioVoiceMemoryLimit",
       .label = "Voice Memory Fill Limit",
       .tooltip = "How full it gets before playing sounds are dropped. Restart the game. Game: 70%.",
       .min = 0.5f,
       .max = 1.43f,
       .at_start = true,
       .option = {.tweaks = {{.name = "SOUND_LOWER_ENGINE_DEFAULT_BUFFER_LOW_RATIO"}}}},
      {.section = AUDIO,
       .key = "AudioSoundFileMemory",
       .label = "Sound File Memory",
       .tooltip = "Memory for the loaded sound files. Restart the game; too much may crash it at start\n"
                  "(32-bit game). Game: 86 MB.",
       .min = 1.f,
       .max = 2.f,
       .at_start = true,
       .option = {.tweaks = {{.name = "SOUND_WAVEFILE_BUFFER_SIZE_IN_KB", .integer = true}}}},
      {.section = AUDIO,
       .key = "AudioStreamingMemory",
       .label = "Streaming Memory",
       .tooltip = "Memory for sounds streamed from disk (music, speech). Restart the game. Game: 1 MB.",
       .min = 1.f,
       .max = 8.f,
       .at_start = true,
       .option = {.tweaks = {{.name = "SOUND_STREAM_IO_BUFFER_SIZE_IN_KB", .integer = true}}}},
      {.section = AUDIO,
       .key = "AudioStreamingManagerMemory",
       .label = "Streaming Manager Memory",
       .tooltip = "The streaming manager's own memory. Restart the game. Game: 256 KB.",
       .min = 1.f,
       .max = 8.f,
       .at_start = true,
       .option = {.tweaks = {{.name = "SOUND_STREAM_MANAGER_BUFFER_SIZE_IN_KB", .integer = true}}}},
  };
}

// `tests` with only the keys in `keys` (the ones another game has: its more_tests.h).
inline std::vector<EngineTest> Only(std::vector<EngineTest> tests, const std::vector<std::string>& keys) {
  std::erase_if(tests, [&](const EngineTest& test) { return std::ranges::find(keys, test.key) == keys.end(); });
  return tests;
}

// The tests' sliders (`tests` kept as they are: the sliders hold their options), one section each with an empty line
// after it; an at-start test's change calls `on_at_start_change`.
inline void Append(std::vector<renodx::utils::settings::Setting*>* settings, std::vector<EngineTest>* tests,
                   void (*on_at_start_change)()) {
  for (size_t index = 0; index < tests->size(); ++index) {
    EngineTest& test = (*tests)[index];
    auto* setting = new renodx::utils::settings::Setting{
        .key = test.key,
        .binding = &test.option.scale,
        .default_value = test.option.scale,
        .label = test.label,
        .section = test.section,
        .tooltip = test.tooltip,
        .min = test.min,
        .max = test.max,
        .format = test.format,
    };
    if (test.option.tweaks.front().three_way) {
      setting->value_type = renodx::utils::settings::SettingValueType::INTEGER;
      setting->labels = {"Game's", "Off", "On"};
    } else if (!test.labels.empty()) {
      setting->value_type = renodx::utils::settings::SettingValueType::INTEGER;
      setting->labels = test.labels;
      setting->parse = [](float value) { return std::exp2(value - 1.f); };
    }
    if (test.at_start) setting->on_change = on_at_start_change;
    settings->push_back(setting);
    if (index + 1 == tests->size() || (*tests)[index + 1].section != test.section) {
      settings->push_back(new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "",
          .section = test.section,
      });
    }
  }
}

// ReShade.ini [ADDON] LoadFromDllMain lists the addon (`module`) while an at-start test (audio memory) is changed:
// ReShade then loads it as the game starts, before the game's audio reads them. Removed with all of them back at the
// game's.
inline void SyncEarlyLoad(HMODULE module, const std::vector<EngineTest>& tests) {
  const bool needed = std::ranges::any_of(tests, [](const EngineTest& test) {
    return test.at_start && test.option.scale != 1.f;
  });
  std::array<wchar_t, MAX_PATH> path = {};
  GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  const std::string file = std::filesystem::path(path.data()).filename().string();
  std::array<char, 4096> listed = {};
  size_t listed_size = listed.size();
  std::vector<std::string> entries;
  if (reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", listed.data(), &listed_size)) {
    for (size_t offset = 0; offset < listed_size && listed[offset] != '\0'; offset += std::strlen(listed.data() + offset) + 1) {
      entries.emplace_back(listed.data() + offset);
    }
  }
  const auto entry = std::ranges::find(entries, file);
  if (needed == (entry != entries.end())) return;
  if (needed) {
    entries.push_back(file);
  } else {
    entries.erase(entry);
  }
  std::string serialized;
  for (const std::string& name : entries) {
    serialized += name;
    serialized.push_back('\0');
  }
  reshade::set_config_value(nullptr, "ADDON", "LoadFromDllMain", serialized.c_str(), serialized.size());
  renodx::utils::log::i("[Total War] Audio memory: ReShade.ini [ADDON] LoadFromDllMain ",
                        (needed ? "lists this addon (loaded as the game starts)." : "no longer lists this addon."));
}

// The at-start tests (read once as the game starts): written as soon as the game (`dll`) has set its own values, on a
// thread of their own, up to a minute; `done` once written (or given up), `stop` ends it (the addon's detach).
inline void StartAtStart(const wchar_t* dll, std::vector<EngineTest>* tests, std::atomic<bool>* done,
                         std::atomic<bool>* stop) {
  std::vector<tw::engine_tweaks::Option*> options;
  for (EngineTest& test : *tests) {
    if (test.at_start) options.push_back(&test.option);
  }
  *stop = false;
  *done = options.empty();
  if (options.empty()) return;
  std::thread([dll, options = std::move(options), done, stop]() mutable {
    for (int tries = 0; tries < 6000 && !stop->load(); ++tries) {
      if (tw::engine_tweaks::ApplyWhenSet(dll, options)) {
        tw::engine_tweaks::Trace(options);
        break;
      }
      Sleep(10);
    }
    *done = true;
  }).detach();
}

}  // namespace twattila::engine_tests
