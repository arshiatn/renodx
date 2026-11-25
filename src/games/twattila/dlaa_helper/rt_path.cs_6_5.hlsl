/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's path-traced Light Bounce (rt_path.hlsli PathTraceTexel) as a compute shader (with DXR 1.2 its opacity
// micromap version). With -D REFLECT the reflections' pass instead (ReflectPixel, at the render pixels); with -D REUSE
// ReSTIR GI's spatial reuse (ReuseTexel); with -D REFLECT_RESERVOIR Own ReSTIR's reservoirs (ReflectReservoirTexel); with
// -D REUSE_BOIL the boiling filter on ReSTIR GI's reservoirs (BoilTexel; -I RTXDI-Library's Include); the reference
// build's RTXDI GI contest with -D REUSE_RTXDI_PACK (RtxdiPackTexel) and -D REUSE -D REUSE_RTXDI (ReuseTexel with RTXDI's).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_path.h -Vn RT_PATH; -D REFLECT -Vn RT_REFLECT; with -T cs_6_9
// -D OPACITY_MICROMAPS the _omm variants.
#include "./rt_path.hlsli"

[numthreads(8, 8, 1)]
#if defined(REUSE_BOIL)
void main(uint3 id : SV_DispatchThreadID, uint3 local : SV_GroupThreadID) {
  BoilTexel(id.xy, local.xy);
}
#elif defined(REUSE_RTXDI_PACK)
void main(uint3 id : SV_DispatchThreadID) {
  RtxdiPackTexel(id.xy);
}
#else
void main(uint3 id : SV_DispatchThreadID) {
#if defined(REFLECT)
  ReflectPixel(id.xy);
#elif defined(REFLECT_RESERVOIR)
  ReflectReservoirTexel(id.xy);
#elif defined(REUSE)
  ReuseTexel(id.xy);
#elif defined(PATH_PROBE)
  PathTraceTexel(id.xy);
  ProbeTexel(id.xy);
#else
  PathTraceTexel(id.xy);
#endif
}
#endif
