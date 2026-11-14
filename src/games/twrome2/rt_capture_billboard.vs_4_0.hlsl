/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's far trees turned to the sun (rigidtreebillboard vs30_main): Rome II's D2EC24BD, its sm41 twin 2C1F0C31 and lq
// 60DAA886 take Attila's 4D50E1C7 inputs (POSITION0 xy, TEXCOORD0 xy, TEXCOORD1 position and fade, TEXCOORD2 facing and
// size), card and view choice (o2: the same 4-view angle); no TEXCOORD3 here. D2EC24BD's lean toward a camera looking
// down (inv_view c14.z) is the camera's, not the sun's: left out, as Thrones'.
#define RT_BILLBOARD_WITHOUT_TEXCOORD3
#include "../twattila/rt_capture_billboard.vs_4_0.hlsl"
