// ---- Created with 3Dmigoto v1.3.16 (RenoDX pack dump, 2026-11-13)

// virtualtexture.hlsl ps30_page_touch (sm_3_0): which terrain texture pages the view needs, from the depth, into a
// display / 4 target. Its uv = (vpos + offset) x g_page_touch_constants.xy spans the target (its viewport, the render
// size, is clipped to it: 3840 into 960 at 100% in ATN's traces), so below 100% it read the depth texture whole while
// the 3D drew its top-left part (tools/tw/rome2/phase2): the depth at that uv x the frame's share (render_scale.hlsli).

cbuffer cbTerrainVirtualTexture : register(b0)
{
  float4 g_terrain_vtex_constants_0 : packoffset(c0);
  float4 g_terrain_vtex_constants_1 : packoffset(c1);
  float4 g_terrain_vtex_constants_2 : packoffset(c2);
}

cbuffer camera_VS_PS : register(b1)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
  float4 g_viewport_dimensions : packoffset(c29);
  float4 g_camera_temp0 : packoffset(c30);
  float4 g_camera_temp1 : packoffset(c31);
  float4 g_camera_temp2 : packoffset(c32);
  float4 g_clip_rect : packoffset(c33);
  float g_hide_foliage : packoffset(c34);
}

cbuffer cbPageTouch : register(b2)
{
  float4 g_page_touch_constants : packoffset(c0);
}

SamplerState depth_sampler_s : register(s0);
Texture2D<float4> depth_sampler : register(t0);

#include "../../twattila/render_scale.hlsli"


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_page_touch_constants.xy * r0.xy;
  // The depth the 3D drew below 100% at the uv x the frame's share; the position from the whole view's uv.
  r1.xyzw = depth_sampler.SampleLevel(depth_sampler_s, r0.xy * FrameRenderShare(), 0).yzxw;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r0.x = cmp(r1.z == 1.000000);
  if (r0.x != 0) discard;
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.z = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xy = r0.xy / r0.zz;
  r0.xy = -g_terrain_vtex_constants_0.xy + r0.xy;
  r0.x = g_terrain_vtex_constants_1.w * r0.x;
  r0.z = -r0.y * g_terrain_vtex_constants_1.w + 1;
  r0.yw = g_terrain_vtex_constants_1.yy * r0.xz;
  r1.xy = ddx(r0.yw);
  r0.yw = ddy(r0.yw);
  r0.y = dot(r0.yw, r0.yw);
  r0.w = dot(r1.xy, r1.xy);
  r0.y = max(r0.w, r0.y);
  r0.y = log2(r0.y);
  r0.y = 0.5 * r0.y;
  r0.y = max(0, r0.y);
  r0.y = floor(r0.y);
  r0.y = g_page_touch_constants.z + r0.y;
  r0.y = max(0, r0.y);
  r0.w = exp2(r0.y);
  r0.y = 1 + r0.y;
  o0.w = 0.00392156886 * r0.y;
  r0.y = g_terrain_vtex_constants_1.x * r0.w;
  r0.xy = r0.xz / r0.yy;
  r0.xy = floor(r0.xy);
  r0.xy = float2(0.00390625,0.00390625) * r0.xy;
  r0.zw = floor(r0.xy);
  r0.xy = frac(r0.xy);
  r0.xy = float2(256,256) * r0.xy;
  r0.xy = floor(r0.xy);
  o0.xy = float2(0.00392156886,0.00392156886) * r0.xy;
  r0.x = r0.w * 16 + r0.z;
  o0.z = 0.00392156886 * r0.x;
  return;
}