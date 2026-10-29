/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's path-traced Light Bounce and reflections as ray generation shaders (DXR 1.2; Path Tracing SER,
// Experimental): rt_path.hlsli's PathTraceTexel and ReflectPixel with PATH_REORDER (shader execution reordering: threads
// regroup by their first hit before shading it, as Tales of Arise's paths).
// Build (build.ps1): dxc -T lib_6_9 -D OPACITY_MICROMAPS -D PATH_REORDER -Fh rt_path_ser.h -Vn RT_PATH_SER.
#include "./rt_path.hlsli"

[shader("raygeneration")]
void PathTraceRayGen() {
  PathTraceTexel(DispatchRaysIndex().xy);
}

[shader("raygeneration")]
void ReflectRayGen() {
  ReflectPixel(DispatchRaysIndex().xy);
}
