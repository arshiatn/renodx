/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// rt_capture_rigid_sm41_b with the UV (TEXCOORD1, o2.xy: what its pixel shaders' alpha test reads): the rigid objects (sm4.1), the game's F343950E,
// for a draw whose pixel shader clips (rt_core.hpp, SceneShader::cut_out_shader; rt_scene::InstallCutOuts).
#define RT_CAPTURE_UV
#include "./rt_capture_rigid_sm41_b.vs_4_1.hlsl"
