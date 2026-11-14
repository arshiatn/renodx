/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWSHOGUN2COMPLETEEDITION_RT_RIGID_SHADOW_HLSLI_
#define SRC_GAMES_TWSHOGUN2COMPLETEEDITION_RT_RIGID_SHADOW_HLSLI_

// Shogun 2's rigid shadow casters out of view (ray_tracing.hpp SCENE_SHADERS: twattila/rt_core.hpp, SceneShader::cascade_keep with a
// rows_shader, as Warhammer III's): the shadow pass's rigidterrainblend vs30_shadow (AC25DAB8) input signature, by its decompile. Its
// instances' world rows are v6-v8 (each a row of the object-to-world matrix, the translation in w).
#define RT_RIGID_SHADOW_INPUTS \
  float4 v0 : POSITION0,       \
  float4 v1 : TEXCOORD0,       \
  float4 v2 : NORMAL0,         \
  float4 v3 : NORMAL1,         \
  float4 v4 : NORMAL2,         \
  float4 v5 : COLOR0,          \
  float4 v6 : POSITION1,       \
  float4 v7 : POSITION2,       \
  float4 v8 : POSITION3,       \
  float4 v9 : POSITION4,

#endif  // SRC_GAMES_TWSHOGUN2COMPLETEEDITION_RT_RIGID_SHADOW_HLSLI_
