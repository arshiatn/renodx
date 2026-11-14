/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// rt_capture_unit with the UV (TEXCOORD1, o2.xy: what its pixel shaders' alpha test reads): a 0x20A9E5FD draw whose pixel
// shader clips (rt_core.hpp, SceneShader::cut_out_shader; cut_out_masks.hpp).
#define RT_CAPTURE_UV
#include "./rt_capture_unit.vs_4_0.hlsl"
