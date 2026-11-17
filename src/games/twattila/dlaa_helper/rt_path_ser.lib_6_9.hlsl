/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's path-traced Light Bounce as a ray generation shader (DXR 1.2; Path Tracing SER, Experimental):
// rt_path.hlsli's PathTraceTexel with PATH_REORDER (shader execution reordering at a path's first surface, PathRadiance).
// Built as the compute paths are: the general build and the one of one ray a texel (PATH_ONE_SAMPLE). The reflections
// keep their compute builds (one mirror or lobe ray a pixel: nothing to regroup).
// Build (build.ps1): dxc -T lib_6_9 -D OPACITY_MICROMAPS -D PATH_REORDER [-D PATH_ONE_SAMPLE] -Fh rt_path_ser[_one].h
// -Vn RT_PATH_SER[_ONE].
#include "./rt_path.hlsli"

[shader("raygeneration")]
void PathTraceRayGen() {
  PathTraceTexel(DispatchRaysIndex().xy);
}
