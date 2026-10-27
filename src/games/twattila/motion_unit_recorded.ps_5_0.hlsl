/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion with its records drawn apart (object_motion.hpp: a point per soldier, motion_unit_record.ps):
// motion_unit.ps's motion alone, so the redraw's hidden pixels are skipped early (recording from every pixel cost ~6 ms
// at 4K with a screen of soldiers).
#define MOTION_UNIT_RECORDED
#include "./motion_unit.ps_5_0.hlsl"
