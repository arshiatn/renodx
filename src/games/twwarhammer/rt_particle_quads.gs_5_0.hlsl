/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's smoke shadows' quad shader (twattila/rt_particle_quads_instancing.hlsli): its lit particles' vertex shader
// 9FD3EC63 (vs50_main_instancing, drawn indirect) with 122F218C's slots (ps_main_high_quality: g_tex_diffuse t2,
// g_sam_diffuse s3, g_constant_buffer_ps t5; fxc -dumpbin of the game's bytes). 9FD3EC63's output signature and camera
// constants are Warhammer II's 1FC776EF's and 122F218C's emitter constants (80 bytes) F32116B4's; 122F218C samples its
// diffuse with its uv's change scaled by g_screen_size.zw x g_render_target_dimensions.xy as F32116B4 does (at half size in
// ATN's 2026-11-14 traces): its mip level is a pixel of the viewport over that.
// CMake's shader rule takes no gs_ target: after a change run from the repository's root
// bin\fxc.exe src\games\twwarhammer\rt_particle_quads.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_particle_quads /Fh src\games\twwarhammer\rt_particle_quads_gs.h

#define RT_PARTICLE_DIFFUSE t2
#define RT_PARTICLE_SAMPLER s3
#define RT_PARTICLE_EMITTERS t5
#define RT_PARTICLE_MIP_VIEWPORT (camera[29].xy / (camera[27].zw * camera[31].xy))
#include "../twattila/rt_particle_quads_instancing.hlsli"
