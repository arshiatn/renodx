/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// rt_capture_unit_4_bones_sm41_a with the UV (TEXCOORD1, o2.xy: what its pixel shaders' alpha test reads): the 4-bone units (sm4.1), the game's 1CD702E3,
// for a draw whose pixel shader clips (rt_core.hpp, SceneShader::cut_out_shader; rt_scene::InstallCutOuts).
#define RT_CAPTURE_UV
#include "./rt_capture_unit_4_bones_sm41_a.vs_4_1.hlsl"
