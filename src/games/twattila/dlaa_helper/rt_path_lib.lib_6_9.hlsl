/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's path-traced Light Bounce and reflections (rt_path.hlsli) as DXR 1.2 ray generation shaders (dispatched
// with DispatchRays): shader execution reordering after each path's first hit, opacity micromaps in its ray queries
// (shader model 6.9).
// Build (build.ps1): dxc -T lib_6_9 -D OPACITY_MICROMAPS -Fh rt_path_lib.h -Vn RT_PATH_LIB.
#define PATH_REORDER
#include "./rt_path.hlsli"

[shader("raygeneration")]
void PathTraceRayGen() {
  PathTraceTexel(DispatchRaysIndex().xy);
}

[shader("raygeneration")]
void ReflectRayGen() {
  ReflectPixel(DispatchRaysIndex().xy);
}
