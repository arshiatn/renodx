/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// SHaRC Radiance Cache's resolve (rt_sharc.hlsli; this software contains source code provided by NVIDIA Corporation): a
// thread per entry merges this frame's update with the frames before it (at most SHARC_FRAMES), entries left without
// samples for SHARC_STALE_FRAMES go. Last frame's camera (SHaRC blends a voxel's adjacent level after a move) from last
// frame's view-projection: where its x, y and w are 0.
// Build (build.ps1, with SHaRC): dxc -T cs_6_9 -D OPACITY_MICROMAPS -D SHARC -enable-16bit-types -Fh rt_sharc_resolve.h
// -Vn RT_SHARC_RESOLVE.
#include "./rt_path.hlsli"

static const uint SHARC_FRAMES = 32u;
static const uint SHARC_STALE_FRAMES = 64u;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float3 previous_camera = Camera();
  if ((path_flags & PATH_HISTORY_VALID) != 0u) {
    // The 3 x 3 of rows 0, 1 and 3 inverted (its cofactors' columns over its determinant).
    const float3 row0 = previous_view_projection[0].xyz;
    const float3 row1 = previous_view_projection[1].xyz;
    const float3 row3 = previous_view_projection[3].xyz;
    const float3 cofactor0 = cross(row1, row3);
    const float3 cofactor1 = cross(row3, row0);
    const float3 cofactor2 = cross(row0, row1);
    const float determinant = dot(row0, cofactor0);
    if (abs(determinant) > 1e-12f) {
      previous_camera = mul(transpose(float3x3(cofactor0, cofactor1, cofactor2)),
                            -float3(previous_view_projection[0].w, previous_view_projection[1].w, previous_view_projection[3].w))
                        / determinant;
    }
  }
  SharcResolveParameters resolve;
  resolve.cameraPositionPrev = previous_camera;
  resolve.accumulationFrameNum = SHARC_FRAMES;
  resolve.staleFrameNumMax = SHARC_STALE_FRAMES;
  resolve.frameIndex = frame;
  SharcResolveEntry(id.x, Sharc(), resolve);
}
