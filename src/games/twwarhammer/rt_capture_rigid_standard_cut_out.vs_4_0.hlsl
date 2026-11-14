/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// rt_capture_rigid_standard with the UV (TEXCOORD1, o2.xy: what its pixel shaders' alpha test reads): a 0x51EDFD6E draw whose
// pixel shader clips at its diffuse map's alpha (rt_core.hpp, SceneShader::cut_out_shader; CUT_OUT_MASKS).
#define RT_CAPTURE_UV
#include "./rt_capture_rigid_standard.vs_4_0.hlsl"
