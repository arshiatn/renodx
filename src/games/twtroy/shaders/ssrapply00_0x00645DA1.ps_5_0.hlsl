// ssr_new ps50_apply (the screen-space reflections into the light buffer: its SSR over the cube, by the water's weight):
// the game's 00645DA1 (3Dmigoto decompile), below 100% at the render share as Pharaoh's twin DE4F92CD (v62 / v63), and with
// the ray-traced reflections in its SSR's place where they left from the water (twattila/water_reflections.hpp
// OnWaterApplyDraw; twattila/rt_reflection.hlsli; Pharaoh's v56 form). The quad VS A0EA5C9E's TEXCOORD0 runs 0-1 over the
// viewport (ATN's 2026-11-14 33% traces: vb0 uv (0, 1) at NDC (-1, -1)), and the game reads its display-size textures at
// TEXCOORD0 x their size: below 100% that is TEXCOORD0 x the draw's share, the SSR colour's distortion x the share too, its
// roughness level one lower per halving. Its NDC from TEXCOORD0 alone stays (the view's own). At 100% with the rays unbound
// (cb12, t47 unbound) the game's result: WARP tools\tw\troy\ssr_share and tools\tw\troy\water\ssr_rays.
#include "../../twattila/render_scale.hlsli"
#include "../../twattila/rt_reflection.hlsli"

cbuffer environment_PS : register(b0)
{
  bool g_apply_cubemap_interpolation : packoffset(c0);
  float g_skydome_lerp : packoffset(c0.y);
  float4 g_skydome_params : packoffset(c1);
  float4 g_skydome_shading : packoffset(c2);
  float g_environment_weights[8] : packoffset(c3);
  float g_environment_indices[8] : packoffset(c11);
  float g_environment_count : packoffset(c18.y);
  float g_environment_scale : packoffset(c18.z);
  float g_porthole_env_scale : packoffset(c18.w);
  float g_porthole_pp_saturation : packoffset(c19);
  float g_porthole_pp_contrast : packoffset(c19.y);
}

cbuffer screen_space_refl_params : register(b1)
{
  float4x4 view_projection_matrix : packoffset(c0);
  float4x4 inv_view_projection_matrix : packoffset(c4);
  float3 camera_pos : packoffset(c8);
  float3 camera_dir : packoffset(c9);
  float3 misc : packoffset(c10);
  float4 render_resolution : packoffset(c11);
  float3 fog_colour : packoffset(c12);
  float2 fog_params : packoffset(c13);
}

SamplerState s_environment_array_s : register(s0);
SamplerState s_trilinear_sampler_s : register(s1);
TextureCubeArray<float4> t_environment_array : register(t0);
Texture2D<float4> t_specular_src : register(t1);
Texture2DMS<float4> t_specular_src_ms : register(t2);
Texture2D<uint> t_mask_src : register(t3);
Texture2D<float4> t_depth_src : register(t4);
Texture2DMS<float> t_depth_src_ms : register(t5);
Texture2D<float4> t_colour_src : register(t6);
Texture2D<float4> t_normal_src : register(t7);
Texture2DMS<float4> t_normal_src_ms : register(t8);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out uint oMask : SV_Coverage)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  const float2 share = DrawRenderShare();
  const float2 uv = v1.xy * share;

  t_mask_src.GetDimensions(0, fDest.x, fDest.y, fDest.z);
  r0.xy = fDest.xy;
  r0.zw = uv * r0.xy;
  r1.xy = (int2)r0.zw;
  r1.zw = float2(0,0);
  const int2 rt_render_pixel = (int2)r1.xy;
  r0.z = t_mask_src.Load(r1.xyw).x;
  r0.w = cmp((int)r0.z == 0);
  if (r0.z == 0) {
    if (-1 != 0) discard;
    o0.xyzw = float4(0,0,0,0);
    oMask = 0;
  }
  if (r0.w == 0) {
    r2.xy = cmp(float2(0,0) != misc.zy);
    if (r2.x != 0) {
      r0.w = firstbitlow((uint)r0.z);
      r3.x = t_depth_src_ms.Load(r1.xy, r0.w).x;
    } else {
      r3.x = t_depth_src.Load(r1.xyw).x;
    }
    r3.yz = v1.xy * float2(2,-2) + float2(-1,1);
    r3.w = 1;
    r4.x = dot(r3.yzxw, inv_view_projection_matrix._m00_m10_m20_m30);
    r4.y = dot(r3.yzxw, inv_view_projection_matrix._m01_m11_m21_m31);
    r4.z = dot(r3.yzxw, inv_view_projection_matrix._m02_m12_m22_m32);
    r2.z = dot(r3.yzxw, inv_view_projection_matrix._m03_m13_m23_m33);
    r2.z = 1 / r2.z;
    if (r2.x != 0) {
      r3.yz = t_normal_src_ms.Load(r1.xy, r0.w).xy;
    } else {
      r3.yz = t_normal_src.Load(r1.xyw).xy;
    }
    r5.xy = r3.yz * float2(2,2) + float2(-1,-1);
    r2.w = dot(r5.xy, r5.xy);
    r2.w = 1 + -r2.w;
    r2.w = max(0, r2.w);
    r5.xy = r5.xy + r5.xy;
    r3.w = sqrt(r2.w);
    r5.xz = r5.xy * r3.ww;
    if (r2.y != 0) {
      r3.w = 1.00100005 + -r3.x;
      r3.w = 1 / r3.w;
      r3.w = -1 + r3.w;
      r3.w = 0.0500000007 * r3.w;
      r6.xy = -r4.xz * r2.zz + camera_pos.xz;
      r4.w = dot(r6.xy, r6.xy);
      r4.w = rsqrt(r4.w);
      r6.xy = r6.xy * r4.ww;
      r4.w = dot(r6.xy, r5.xz);
      r6.y = 0.5 * r4.w;
      r6.x = 0;
    } else {
      r3.yz = float2(-0.5,-0.5) + r3.yz;
      r6.xy = float2(0.699999988,0.699999988) * r3.yz;
      r3.x = 1.00119996 + -r3.x;
      r3.x = 1 / r3.x;
      r3.x = -1 + r3.x;
      r3.w = 0.00400000019 * r3.x;
    }
    t_colour_src.GetDimensions(0, fDest.x, fDest.y, fDest.z);
    r3.xy = fDest.xy;
    r0.xy = float2(0.5,0.5) * r0.xy;
    r0.xy = r0.xy / r3.xy;
    r0.xy = uv * r0.xy + r6.xy * share;
    const bool rt_water = RtReflectionsBound() && RtWaterReflections();
    if (rt_water) {
      const float4 rt_traced = t_rt_reflection.Load(int3(rt_render_pixel, 0));
      // At most 25 a channel, as the game's march: its RGBA8 colour x 5, squared. Their share met in place of the SSR's
      // confidence and its fade with distance (the rays reach as far).
      r6.xyz = min(max(rt_traced.rgb, 0.f), 25.f);
      r0.x = saturate(rt_traced.a);
    } else {
      // The roughness' level is in the SSR colour's texels; below 100% the colour has the share of them: log2 of the share lower.
      const float ssr_level = (share.x < 1.f) ? max(r3.w + 0.5f * log2(share.x * share.y), 0.f) : r3.w;
      r6.xyzw = t_colour_src.SampleLevel(s_trilinear_sampler_s, r0.xy, ssr_level).xyzw;
      r3.xyz = float3(5,5,5) * r6.xyz;
      r6.xyz = r3.xyz * r3.xyz;
      r0.x = sqrt(r3.w);
      r0.x = saturate(-r0.x * 0.5 + 1.29999995);
      r0.x = r6.w * r0.x;
      r0.x = r2.y ? r0.x : r6.w;
    }
    r0.y = cmp(r0.x < 1);
    if (r0.y != 0) {
      r5.y = r2.w * 2 + -1;
      r2.yzw = r4.xyz * r2.zzz + -camera_pos.xyz;
      r0.y = dot(r2.yzw, r5.xyz);
      r0.y = r0.y + r0.y;
      r2.yzw = r5.xyz * -r0.yyy + r2.yzw;
      r0.y = dot(r2.yzw, r2.yzw);
      r0.y = rsqrt(r0.y);
      r4.xyz = r2.yzw * r0.yyy;
      r4.w = 0;
      r2.yzw = t_environment_array.SampleLevel(s_environment_array_s, r4.xyzw, 0).xyz;
      if (g_apply_cubemap_interpolation != 0) {
        r4.w = 1;
        r4.xyz = t_environment_array.SampleLevel(s_environment_array_s, r4.xyzw, 0).xyz;
        r4.xyz = r4.xyz + -r2.yzw;
        // DECOMPILER BUG corrected: the asm lerps by cb0[0].y (g_skydome_lerp), not the bool at c0.x.
      r2.yzw = g_skydome_lerp * r4.xyz + r2.yzw;
      }
      r2.yzw = g_environment_scale * r2.yzw;
      if (rt_water) {
        r2.yzw = RtOccludedEnvironment(rt_render_pixel, r2.yzw);
        r3.xyz = r6.xyz + -r2.yzw;
      } else {
        r3.xyz = r3.xyz * r3.xyz + -r2.yzw;
      }
      r6.xyz = r3.xyz * r0.xxx + r2.yzw;
    }
    if (r2.x != 0) {
      r2.xyz = t_specular_src_ms.Load(r1.xy, r0.w).xyz;
    } else {
      r2.xyz = t_specular_src.Load(r1.xyz).xyz;
    }
    o0.xyz = r6.xyz * r2.xyz;
    o0.w = r0.x;
    oMask = r0.z;
  }
  return;
}