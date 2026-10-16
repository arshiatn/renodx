/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private DLSS input preparation, not a game replacement registration. Drawn with the game's TAA
// (0x121C45E4) vertex shader, inputs, PER_BATCH and viewport (the whole textures, at the Rendering Resolution).
cbuffer PER_BATCH : register(b0) {
  row_major float4x4 mViewProjPrev : packoffset(c0);  // world -> previous uv (x, y, w columns)
  float4 WorldViewPos : packoffset(c4);
  float4 PS_NearFarClipDist : packoffset(c5);
}

cbuffer PassConstants : register(b12) {
  uint ColorEncoding;  // 0: the view decodes sRGB, 1: stored values are encoded (gamma 2.2 round trip)
  uint HasCamera;
  uint DepthReversed;  // the camera's z / w falls with distance (reverse-Z)
  float4 CurrentUvX;  // this frame's camera, world -> uv: x, y, w coefficients (world x, y, z, 1)
  float4 CurrentUvY;
  float4 CurrentUvW;  // also its clip w
  float4 CurrentZ;    // its clip z
}

Texture2D<float4> LinearDepth : register(t2);  // x * far = view depth
Texture2D<float4> ObjectVelocity : register(t3);
Texture2D<float4> SceneColor : register(t4);

// uv of a camera-relative position for a world -> uv matrix (its x, y, w coefficients). The camera position is folded
// into the translation in double: world coordinates are large, and in float the cancellation left ~0.1 pixel of noise
// near the camera (v21).
float2 Uv(float4 x, float4 y, float4 w, float3 relative_position, double3 eye) {
  const float3 origin = float3(eye.x * double3(x.x, y.x, w.x) + eye.y * double3(x.y, y.y, w.y) + eye.z * double3(x.z, y.z, w.z)
                               + double3(x.w, y.w, w.w));
  const float3 clip = float3(dot(x.xyz, relative_position), dot(y.xyz, relative_position), dot(w.xyz, relative_position)) + origin;
  return clip.xy / clip.z;
}

// Depth (z / w) of a camera-relative position for a world -> clip z and w, as Uv().
float Depth(float4 z, float4 w, float3 relative_position, double3 eye) {
  const float2 origin = float2(eye.x * double2(z.x, w.x) + eye.y * double2(z.y, w.y) + eye.z * double2(z.z, w.z)
                               + double2(z.w, w.w));
  return (dot(z.xyz, relative_position) + origin.x) / (dot(w.xyz, relative_position) + origin.y);
}

// The uv shift of a world -> uv matrix's x / y part along w (view forward): the projection's jitter (and any off-center
// shift), removed the same way for both frames.
float2 Jitter(float4 x, float4 y, float4 w) {
  return float2(dot(x.xyz, w.xyz), dot(y.xyz, w.xyz)) / dot(w.xyz, w.xyz) - 0.5f;
}

void main(
    float4 v0 : SV_Position0,
    float4 v1 : TEXCOORD0,
    float4 v2 : TEXCOORD1,
    out float o0 : SV_Target0,
    out float2 o1 : SV_Target1,
    out float4 o2 : SV_Target2) {
  const int3 pixel = int3(v0.xy, 0);
  const float linear_depth = LinearDepth.Load(pixel).x;

  // Motion in render pixels (previous - current), uv x texture size. Camera: the surface the game reconstructs for this
  // pixel (its view ray x depth) projected with the previous and this frame's camera, both without jitter. (v22 took
  // the TAA's texture coordinates as the current position: the view ray holds this frame's jitter, a sub-pixel motion
  // that changed every frame, DLSS's wobble when still.) Objects: the packed velocity (x == 0: none), uv (the G-buffer
  // writes pixel / texture size, its vertex shader the previous position x g_VS_HPosScale).
  float2 texture_size;
  SceneColor.GetDimensions(texture_size.x, texture_size.y);
  const float3 relative_position = v2.xyz * linear_depth;
  const double3 eye = double3(WorldViewPos.xyz);
  const float4 previous_x = mViewProjPrev._m00_m10_m20_m30;
  const float4 previous_y = mViewProjPrev._m01_m11_m21_m31;
  const float4 previous_w = mViewProjPrev._m03_m13_m23_m33;
  const float2 previous_uv = Uv(previous_x, previous_y, previous_w, relative_position, eye) - Jitter(previous_x, previous_y, previous_w);
  // This frame's camera puts the surface on this pixel (within its jitter); else (not that camera) the TAA's uv.
  const float2 current_jittered_uv = Uv(CurrentUvX, CurrentUvY, CurrentUvW, relative_position, eye);
  const bool camera = HasCamera != 0 && all(abs(current_jittered_uv - v1.xy) * texture_size < 1.f);
  const float2 current_uv = camera ? current_jittered_uv - Jitter(CurrentUvX, CurrentUvY, CurrentUvW) : v1.xy;

  // DLSS depth, reverse-Z (flag DepthInverted): the depth buffer's value for the surface with this frame's camera.
  // v99: PER_BATCH's PS_NearFarClipDist (near / view depth before) is not set for the game's TAA, which never reads it:
  // every pixel had depth 1, the near plane (ATN debugForClaude38). Without the camera, that as before.
  if (camera) {
    const float depth = Depth(CurrentZ, CurrentUvW, relative_position, eye);
    o0 = saturate((DepthReversed != 0) ? depth : (1.f - depth));
  } else {
    const float view_depth = linear_depth * PS_NearFarClipDist.y;
    o0 = (view_depth > PS_NearFarClipDist.x) ? (PS_NearFarClipDist.x / view_depth) : 1.f;
  }
  float2 motion = previous_uv - current_uv;
  const float2 packed_velocity = ObjectVelocity.Load(pixel).xy;
  if (packed_velocity.x != 0.f) {
    const float2 velocity = (packed_velocity - 0.498039216f) * 2.f;
    motion = velocity * velocity * float2(velocity.x > 0.f ? 1.f : -1.f, velocity.y > 0.f ? 1.f : -1.f);
  }
  const float2 motion_pixels = motion * texture_size;
  o1 = all(isfinite(motion_pixels)) ? clamp(motion_pixels, -65504.f, 65504.f) : 0.f;

  // Linear for DLSS (HDR flag); the copy pass encodes back the same way.
  float3 color = SceneColor.Load(pixel).rgb;
  if (ColorEncoding != 0) {
    color = sign(color) * pow(abs(color), 2.2f);
  }
  o2 = float4(all(isfinite(color)) ? color : 0.f, 0.f);
}
