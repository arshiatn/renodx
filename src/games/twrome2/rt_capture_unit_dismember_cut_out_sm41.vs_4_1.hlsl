/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// rt_capture_unit_dismember_sm41 with the UV (TEXCOORD1, o2.xy: what its pixel shaders' alpha test reads): the dismembered units (sm4.1), the game's 63DB20CA,
// for a draw whose pixel shader clips (rt_core.hpp, SceneShader::cut_out_shader; rt_scene::InstallCutOuts).
#define RT_CAPTURE_UV
#include "./rt_capture_unit_dismember_sm41.vs_4_1.hlsl"
