/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Pharaoh's smoke shadows' quad shader (twattila/rt_particle_quads_instancing.hlsli): its lit particles' vertex shader
// 109B79CE with 3F1B86E1's slots (the shared file's own; E8524BB1's views and samplers arrive shifted to them).
// CMake's shader rule takes no gs_ target: after a change run from the repository's root
// bin\fxc.exe src\games\twpharaohdynasties\rt_particle_quads.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_particle_quads /Fh src\games\twpharaohdynasties\rt_particle_quads_gs.h
// Troy (its lit particles 563BA3E8 bind the diffuse array, the emitters and their sampler at t7 / t9 / s5): the same with
// /D RT_PARTICLE_DIFFUSE=t7 /D RT_PARTICLE_EMITTERS=t9 /D RT_PARTICLE_SAMPLER=s5 /Fh src\games\twtroy\rt_particle_quads_gs.h

#include "../twattila/rt_particle_quads_instancing.hlsli"
