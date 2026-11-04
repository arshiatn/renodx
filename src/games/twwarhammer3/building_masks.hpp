/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Made by a script from the game's shaders (their names and asm): the pixel shaders of vertex shader 4F35D09F that draw a solid
// surface into the G-buffer: buildings, tents, shrubs, mountains, rocks and other standing props. The same vertex shader also
// draws water, decals, blended effects, and materials that only exist on a terrain: those are not listed, so they stay out of
// the ray-traced scene. Sorted by pixel shader.
#pragma once

#include <array>

#include "../twattila/rt_scene.hpp"

namespace twwarhammer3 {

inline constexpr std::array<twattila::rt_scene::PixelMask, 146> BUILDING_MASKS = {{
    {.pixel_shader = 0x03CACB63},  // rigid_battle_mountain
    {.pixel_shader = 0x04417F79},  // rigid_default_alpha
    {.pixel_shader = 0x04B4F2EA},  // rigid_building_emissive_masked_uv2
    {.pixel_shader = 0x04D17392},  // rigid_campaign_mountain_colour_gradient
    {.pixel_shader = 0x0787F5FE},  // rigid_emissive
    {.pixel_shader = 0x08601804},  // rigid_building_overlay_alpha
    {.pixel_shader = 0x0A704418},  // rigid_random_colours
    {.pixel_shader = 0x0AFAD346},  // rigid_faction_colours_skin
    {.pixel_shader = 0x0BCB76C8},  // end_rigid_battle_blendmask_2tex_iridescence
    {.pixel_shader = 0x0DC98511},  // rigid_building_terrainblend_alpha_mask
    {.pixel_shader = 0x0FD35FAF},  // end_rigid_battle_blendmask_2tex_uv2
    {.pixel_shader = 0x114C88B2},  // end_rigid_battle_blendmask_2tex_uv1_terrain_v02
    {.pixel_shader = 0x1371216C},  // rigid_building_emissive
    {.pixel_shader = 0x137DF948},  // rigid_iridescent
    {.pixel_shader = 0x13D6B05B},  // rigid_building_overlay
    {.pixel_shader = 0x1557EB74},  // end_rigid_battle_blendmask_2tex
    {.pixel_shader = 0x167C04A1},  // test_test_rigid_colouroffset
    {.pixel_shader = 0x16BDED05},  // rigid_tinted_metal_emissive_alpha
    {.pixel_shader = 0x1C76A1AB},  // rigid_battle_rock_emissive_worldspace
    {.pixel_shader = 0x1D74AAF9},  // rigid_campaign_mountain
    {.pixel_shader = 0x1FE4A8C9},  // rigid_building_default
    {.pixel_shader = 0x2175B3B8},  // rigid_building_overlay_alpha
    {.pixel_shader = 0x2470FF3E},  // rigid_battle_shrub
    {.pixel_shader = 0x26051F8D},  // end_rigid_battle_blendmask_2tex_terrain
    {.pixel_shader = 0x2922D5BF},  // end_rigid_battle_blendmask_2tex
    {.pixel_shader = 0x29B05400},  // rigid_building_emissive_masked_vc
    {.pixel_shader = 0x2B2FA028},  // rigid_terrain
    {.pixel_shader = 0x2B350472},  // simple
    {.pixel_shader = 0x2DC854CB},  // rigid_faction_colours_skin
    {.pixel_shader = 0x30F0E836},  // rigid_battle_rock_emissive_worldspace
    {.pixel_shader = 0x31D648AD},  // wh_tiling1st_unique2nd
    {.pixel_shader = 0x329C5AE2},  // basic
    {.pixel_shader = 0x3985BC0C},  // rigid_building_textureblend
    {.pixel_shader = 0x3E52C8EE},  // rigid_emissive_crystal_02
    {.pixel_shader = 0x41A8B097},  // rigid_faction_colours_alpha
    {.pixel_shader = 0x41F98245},  // end_rigid_battle_blendmask_2tex_uv2_iridescence
    {.pixel_shader = 0x4337140A},  // rigid_faction_colours
    {.pixel_shader = 0x45ADD393},  // rigid_building_default_alpha
    {.pixel_shader = 0x45EAD336},  // rigid_battle_pulsating_emissive
    {.pixel_shader = 0x4693CD46},  // rigid_tinted_metal_alpha
    {.pixel_shader = 0x477E677B},  // basic
    {.pixel_shader = 0x4E82C23E},  // battle_wh3_rock_noterrain
    {.pixel_shader = 0x53B92889},  // rigid_faction_colours_skin_alpha
    {.pixel_shader = 0x548D9F0A},  // renown_topper
    {.pixel_shader = 0x572D5E2C},  // end_rigid_battle_blendmask_2tex_uv2_iridescence
    {.pixel_shader = 0x59B4966E},  // rigid_building_emissive_optimized
    {.pixel_shader = 0x5B9DE18E},  // rigid_building_emissive
    {.pixel_shader = 0x5D411624},  // rigid_building_vcol_blend
    {.pixel_shader = 0x5E8D5605},  // wh2_env_battle_rigid_emissive_crystal
    {.pixel_shader = 0x5F15E584},  // rigid_default
    {.pixel_shader = 0x5F3663E5},  // rigid_campaign_rock
    {.pixel_shader = 0x5F3DFF00},  // end_rigid_battle_blendmask_2tex_uv1_terrain_v02
    {.pixel_shader = 0x64237651},  // rigid_emissive
    {.pixel_shader = 0x667DA8DB},  // rigid_faction_colours_alpha
    {.pixel_shader = 0x6BB08B41},  // rigid_building_blend
    {.pixel_shader = 0x6E08F282},  // environment
    {.pixel_shader = 0x6E860DE1},  // rigid_battle_rock
    {.pixel_shader = 0x6EB6F588},  // battle_wh3_rock_noterrain
    {.pixel_shader = 0x70A54CD0},  // rigid_battle_detail_mountain
    {.pixel_shader = 0x71FC5475},  // dwf_cliff_caves_01
    {.pixel_shader = 0x72274E1B},  // rigid_battle_rock
    {.pixel_shader = 0x72ED3670},  // simple
    {.pixel_shader = 0x74AC2178},  // wh2_env_battle_rigid_emissive_crystal
    {.pixel_shader = 0x74DAC17A},  // rigid_random_colours
    {.pixel_shader = 0x7700D30A},  // rigid_campaign_rock_terrain_blend
    {.pixel_shader = 0x77803441},  // rigid_building_ao_01
    {.pixel_shader = 0x79FAB721},  // rigid_battle_rock_terrain_blend
    {.pixel_shader = 0x7D0FDD2C},  // rigid_battle_tent
    {.pixel_shader = 0x7DA59FDE},  // rigid_building_default_alpha
    {.pixel_shader = 0x7EA09CA3},  // rigid_campaign_rock_terrain_blend
    {.pixel_shader = 0x8033127D},  // rigid_building_emissive_masked_uv2
    {.pixel_shader = 0x81C8B88A},  // rigid_building_default
    {.pixel_shader = 0x868A7E64},  // rigid_tinted_metal
    {.pixel_shader = 0x8BB97953},  // renown_topper
    {.pixel_shader = 0x8F0549AE},  // rigid_battle_tent
    {.pixel_shader = 0x90A15294},  // rigid_emissive_crystal_02
    {.pixel_shader = 0x9518AEFA},  // rigid_campaign_mountain
    {.pixel_shader = 0x958B7023},  // rigid_default
    {.pixel_shader = 0x98CEF2FF},  // end_rigid_battle_blendmask_2tex_uv1_terrain
    {.pixel_shader = 0x998D3334},  // rigid_campaign_mountain_emissive
    {.pixel_shader = 0x99E7D053},  // rigid_crystal
    {.pixel_shader = 0x9AC5F3CE},  // rigid_campaign_rock
    {.pixel_shader = 0x9D45ECF8},  // rigid_campaign_building_emissive
    {.pixel_shader = 0x9FE30136},  // rigid_random_overlay
    {.pixel_shader = 0xA0F188E3},  // rigid_building_emissive_masked
    {.pixel_shader = 0xA2102725},  // rigid_tinted_metal_alpha
    {.pixel_shader = 0xA3F7C267},  // rigid_tinted_metal_emissive
    {.pixel_shader = 0xA4B188FC},  // rigid_battle_mountain
    {.pixel_shader = 0xA646C789},  // rigid_crystal
    {.pixel_shader = 0xA6617E5E},  // rigid_campaign_mountain_emissive
    {.pixel_shader = 0xA7FD2ECB},  // rigid_battle_detail_mountain
    {.pixel_shader = 0xA83EE2D4},  // rigid_building_emissive_alpha
    {.pixel_shader = 0xA91A893A},  // rigid_campaign_mountain_colour_gradient
    {.pixel_shader = 0xAA6AD754},  // rigid_campaign_mountain_noblend
    {.pixel_shader = 0xAE96A5A4},  // rigid_random_overlay_faction
    {.pixel_shader = 0xB18594D1},  // battle_wh3_rock_01
    {.pixel_shader = 0xB6E16031},  // environment
    {.pixel_shader = 0xB93E5F40},  // rigid_battle_pulsating_emissive
    {.pixel_shader = 0xB9A893A2},  // rigid_campaign_building_emissive
    {.pixel_shader = 0xBB37C186},  // rigid_building_emissive_alpha
    {.pixel_shader = 0xBF040FE8},  // rigid_building_overlay
    {.pixel_shader = 0xC00C38FD},  // rigid_building_emissive_masked
    {.pixel_shader = 0xC1F59A94},  // wh_tiling1st_unique2nd
    {.pixel_shader = 0xC245EDAF},  // rigid_terrain
    {.pixel_shader = 0xC29D1AA8},  // rigid_random_overlay
    {.pixel_shader = 0xC2BAF859},  // rigid_building_emissive_masked_vc
    {.pixel_shader = 0xC32745F6},  // rigid_faction_colours_skin_alpha
    {.pixel_shader = 0xC6618C8F},  // zup_blend_01
    {.pixel_shader = 0xC75AA335},  // rigid_building_terrainblend_alpha_mask
    {.pixel_shader = 0xC8D75F0A},  // rigid_building_terrainblend
    {.pixel_shader = 0xC9337343},  // rigid_random_overlay_faction
    {.pixel_shader = 0xCB80D1A8},  // test_test_rigid_colouroffset
    {.pixel_shader = 0xCDCB684C},  // rigid_battle_rock_emissive
    {.pixel_shader = 0xCE666ACC},  // rigid_building_vcol_blend
    {.pixel_shader = 0xCEACD82B},  // end_rigid_battle_blendmask_2tex_uv1_terrain_vertex_colour
    {.pixel_shader = 0xCFE6C1B0},  // rigid_iridescence
    {.pixel_shader = 0xD0B3703E},  // dwf_cliff_caves_01
    {.pixel_shader = 0xD235E520},  // rigid_iridescent
    {.pixel_shader = 0xD2E6E524},  // rigid_building_blend
    {.pixel_shader = 0xD37DE792},  // wh2_battle_terrain_props_wh2_rockset_01
    {.pixel_shader = 0xD5D94479},  // rigid_building_ao_01
    {.pixel_shader = 0xD682378D},  // rigid_battle_rock_emissive
    {.pixel_shader = 0xDB5F0BC2},  // rigid_building_emissive_optimized
    {.pixel_shader = 0xE1D68CFF},  // end_rigid_battle_blendmask_2tex_uv2
    {.pixel_shader = 0xE3251E2C},  // rigid_iridescence
    {.pixel_shader = 0xE3DF5B93},  // rigid_building_textureblend
    {.pixel_shader = 0xE45EBA1C},  // wh2_battle_terrain_props_wh2_rockset_01
    {.pixel_shader = 0xE496192F},  // rigid_battle_shrub
    {.pixel_shader = 0xE71A6555},  // rigid_building_uvscroll
    {.pixel_shader = 0xE7D5C8E6},  // end_rigid_battle_blendmask_2tex_uv1_terrain_vertex_colour
    {.pixel_shader = 0xE9F269AE},  // rigid_default_alpha
    {.pixel_shader = 0xEF122592},  // zup_blend_01
    {.pixel_shader = 0xF31342FD},  // end_rigid_battle_blendmask_2tex_iridescence
    {.pixel_shader = 0xF334B82F},  // rigid_building_masked_uv2
    {.pixel_shader = 0xF3C7372C},  // rigid_building_uvscroll
    {.pixel_shader = 0xF523A5BE},  // rigid_faction_colours
    {.pixel_shader = 0xF7298E26},  // rigid_tinted_metal_emissive
    {.pixel_shader = 0xF7F11FBF},  // rigid_building_terrainblend
    {.pixel_shader = 0xF814F576},  // rigid_campaign_mountain_noblend
    {.pixel_shader = 0xFA5D0ACC},  // rigid_tinted_metal
    {.pixel_shader = 0xFC3C2BC3},  // rigid_building_masked_uv2
    {.pixel_shader = 0xFC4B9E66},  // end_rigid_battle_blendmask_2tex_uv1_terrain
    {.pixel_shader = 0xFD9F2AAE},  // rigid_tinted_metal_emissive_alpha
    {.pixel_shader = 0xFDD317DB},  // battle_wh3_rock_01
    {.pixel_shader = 0xFFC172B7},  // rigid_battle_rock_terrain_blend
    {.pixel_shader = 0xFFE58ACF},  // end_rigid_battle_blendmask_2tex_terrain
}};

}  // namespace twwarhammer3
