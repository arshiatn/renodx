/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// rt_capture_unit_4_bones_sm3_a with the UV (TEXCOORD1, o2.xy: what its pixel shaders' alpha test reads): the 4-bone units (sm3), the game's 0F1FBD46,
// for a draw whose pixel shader clips (rt_core.hpp, SceneShader::cut_out_shader; rt_scene::InstallCutOuts).
#define RT_CAPTURE_UV
#include "./rt_capture_unit_4_bones_sm3_a.vs_4_0.hlsl"
