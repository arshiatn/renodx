// hierarchical_depth_buffer.hlsl cs_hiz_copy: the HiZ base level (min depth of 2x2, linear thickness). SSR
// Resolution Full: the render texels under each SSR texel (ssr.hlsli).

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
  float g_texel_offset : packoffset(c35.z);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);
  float2 g_viewport_origin : packoffset(c39);
  float4 g_render_target_dimensions : packoffset(c40);
  float4 g_camera_temp0 : packoffset(c41);
  float2 g_inv_screen_size_minus_one : packoffset(c42);
  float2 g_camera_jitter : packoffset(c42.z);
  float2 g_prev_camera_jitter : packoffset(c43);
}

Texture2D<float4> t_depth_buffer : register(t0);
RWTexture2D<float2> t_target : register(u0);


// 3Dmigoto declarations
#define cmp -
#include "./ssr.hlsli"


[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // The render texels under this SSR texel (from floor(texel x SSRFactor()), 2x2 when above 1; the game's 2x2 at 2).
  const float factor = SSRFactor();
  const int2 base = int2(floor(float2(vThreadID.xy) * factor));
  r0.x = t_depth_buffer.Load(int3(base, 0)).x;
  if (factor > 1.f) {
    r0.y = min(t_depth_buffer.Load(int3(base + int2(1, 0), 0)).x, t_depth_buffer.Load(int3(base + int2(0, 1), 0)).x);
    r0.x = min(min(r0.x, r0.y), t_depth_buffer.Load(int3(base + int2(1, 1), 0)).x);
  }
  r0.xzw = r0.xxx;
  r1.x = -projection._m22 + r0.w;
  r1.y = projection._m32 * r1.x;
  r1.x = r1.x * 5 + projection._m32;
  r1.x = r1.y / r1.x;
  r0.y = projection._m22 + r1.x;
  t_target[vThreadID.xy] = r0.xy;
  return;
}