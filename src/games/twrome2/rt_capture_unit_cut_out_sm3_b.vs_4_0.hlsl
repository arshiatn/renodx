/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// rt_capture_unit_sm3_b with the UV (TEXCOORD1, o2.xy: what its pixel shaders' alpha test reads): the units (sm3), the game's A30C5E0B,
// for a draw whose pixel shader clips (rt_core.hpp, SceneShader::cut_out_shader; rt_scene::InstallCutOuts).
#define RT_CAPTURE_UV
#include "./rt_capture_unit_sm3_b.vs_4_0.hlsl"
