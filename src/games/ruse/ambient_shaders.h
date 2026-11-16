/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>

#include "./sun_shaders.h"

// Generated from the shader census (tools/nontw/ruse/scripts/ru_ambient_registers.py): the terrain pixel shaders with
// uniAmbientColor (the game's ambient light, gamma 2.2) in c0-c11, by hash, and its register.
namespace ruse::ray_tracing {

inline constexpr std::array<SunRegister, 106> AMBIENT_COLOUR_SHADERS = {{
    {0x02D5FDC4, 6}, {0x0383F2E3, 6}, {0x0653874F, 1}, {0x070F9F0D, 9}, {0x098C9730, 8}, {0x09A11C21, 1},
    {0x09D11B98, 8}, {0x09ED01A2, 8}, {0x0A209062, 7}, {0x0B2D4817, 3}, {0x13B0B4E0, 4}, {0x15A4B8E2, 1},
    {0x15C81075, 9}, {0x17E81D6F, 6}, {0x19F45C9D, 10}, {0x211D996D, 4}, {0x238F5096, 3}, {0x23CE4A5A, 4},
    {0x260797E3, 2}, {0x2742BB0A, 3}, {0x28A27A33, 2}, {0x2E22AC5F, 2}, {0x30305D89, 5}, {0x31797DE7, 4},
    {0x322A8593, 3}, {0x38238218, 3}, {0x39B629CD, 1}, {0x3C023DBB, 7}, {0x3D8090BD, 8}, {0x3E4A3583, 9},
    {0x40F85DBA, 6}, {0x46F15049, 5}, {0x470F7963, 1}, {0x47AC572D, 4}, {0x4B1D1523, 2}, {0x4C825DE6, 4},
    {0x4E0F468A, 4}, {0x4F132323, 8}, {0x5003B85B, 8}, {0x502FF7EC, 5}, {0x50B7C076, 7}, {0x51FB7230, 7},
    {0x545094BE, 9}, {0x560C4F8C, 1}, {0x5AD9EDE4, 7}, {0x5D123F33, 8}, {0x6B884638, 2}, {0x6F16DB3A, 1},
    {0x6F287742, 3}, {0x6FABAE35, 3}, {0x715FCA43, 3}, {0x71D42B68, 5}, {0x72010F61, 7}, {0x7891CE50, 1},
    {0x79033FFF, 3}, {0x7CA61D33, 8}, {0x80454C4A, 6}, {0x85E19118, 6}, {0x8A10FD45, 6}, {0x921F0FBF, 3},
    {0x9398B24F, 5}, {0x9CF80D4F, 3}, {0xA01A6FC5, 1}, {0xA14A07E4, 7}, {0xA55CA4FE, 5}, {0xAA4C6DA5, 8},
    {0xAA920498, 1}, {0xABB6E38C, 1}, {0xAD7BDCC3, 5}, {0xAF8E8B08, 5}, {0xAFCF621C, 10}, {0xAFFF53E4, 2},
    {0xB00C6956, 5}, {0xB172C996, 6}, {0xB39BDD74, 7}, {0xB67900B6, 7}, {0xB7A9B55E, 8}, {0xB8F6D834, 4},
    {0xBCCF4F54, 6}, {0xBEE66EF2, 2}, {0xC140EA9C, 5}, {0xD02A9AE1, 5}, {0xD53D8AB4, 3}, {0xD88B3EA9, 5},
    {0xD8E8C8EA, 1}, {0xD9337B93, 3}, {0xDB910F3A, 5}, {0xDFC5D741, 4}, {0xDFE5E18E, 5}, {0xE202FC70, 5},
    {0xE598B655, 4}, {0xE746950C, 2}, {0xE8564409, 2}, {0xE8E8F9DB, 6}, {0xED69EAF9, 2}, {0xF09F1BE9, 7},
    {0xF13DF8AE, 5}, {0xF1CF0950, 4}, {0xF4BF8DA8, 6}, {0xF4D122A1, 6}, {0xF720B85F, 3}, {0xF75DCA6B, 5},
    {0xF8F2D40A, 7}, {0xF9CDFCE2, 5}, {0xFC388105, 4}, {0xFFEFB075, 7},
}};

}  // namespace ruse::ray_tracing
