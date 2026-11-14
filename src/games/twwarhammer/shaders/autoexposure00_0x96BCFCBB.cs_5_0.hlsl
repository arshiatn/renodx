// Warhammer's luma statistics for its auto exposure (compute_luma_stats, 0x96BCFCBB; the game's code from its 3Dmigoto
// decompile, the atomics written back from its assembly): 512 x 512 nine-tap lumas of the scene into the bloom's 512 x 512
// source (u0) and, between the black and white limits, quantised to 32 bits: their minimum, maximum and the sum of their top
// 14 bits into u1 (bytes 0, 4, 8; clear_luma_stats 166E783A sets -1, 0, 0 first). The game copies them out and meters its
// exposure on the CPU.
//
// RenoDX, Modded Auto Exposure (Attila's rows): the metered maximum capped at last frame's average luma x 2^Sensitivity, so an
// isolated highlight (the sun, fire) moves the exposure less; the minimum, the sum and the bloom's source stay the game's. Last
// frame's sum: the add-on copies it out of u1 before the game's clear (it is the same buffer every frame) and binds it at t1.
// Off (or nothing at t1): the game's bits (test/tw_warhammer_shaders part 25).

cbuffer camera : register(b0)
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
  float2 g_viewport_origin : packoffset(c30);
  float4 g_render_target_dimensions : packoffset(c31);
}

cbuffer luma_stats_buffer : register(b1)
{
  float g_min_luma_black_point : packoffset(c0);
  float g_min_luma_white_point : packoffset(c0.y);
  float g_max_luma_black_point : packoffset(c0.z);
  float g_max_luma_white_point : packoffset(c0.w);
}

SamplerState s_bilinear_s : register(s0);
Texture2D<float4> t_input : register(t0);
ByteAddressBuffer t_luma_last_sum : register(t1);  // RenoDX: last frame's sum (byte 0)
RWTexture2D<float4> t_rw_downsampled_lab : register(u0);
RWByteAddressBuffer t_rw_luma_stats : register(u1);

#include "../shared.h"

groupshared uint g_luma_stats[3];  // minimum, maximum, sum

[numthreads(8, 8, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID, uint3 vThreadIDInGroup : SV_GroupThreadID)
{
  float4 r0,r1,r2,r3,r4,r5;

  // The game's: every thread sets them, no barrier before the atomics.
  g_luma_stats[0] = 0xFFFFFFFFu;
  g_luma_stats[1] = 0u;
  g_luma_stats[2] = 0u;
  r0.xyzw = (int4)vThreadID.xyxy;
  r0.xyzw = float4(0.5,0.5,0.5,0.5) + r0.xyzw;
  r0.xyzw = float4(0.001953125,0.001953125,0.001953125,0.001953125) * r0.xyzw;
  r1.xyzw = g_render_target_dimensions.zwzw * g_viewport_dimensions.xyxy;
  r2.xy = r1.zw * r0.zw;
  r3.xyzw = r0.zwzw * r1.zwzw + float4(-0.000651041686,-0.000651041686,0,-0.000651041686);
  r4.xyz = t_input.SampleLevel(s_bilinear_s, r3.xy, 0).xyz;
  r3.xyz = t_input.SampleLevel(s_bilinear_s, r3.zw, 0).xyz;
  r3.xyz = r4.xyz + r3.xyz;
  r4.xyzw = r0.zwzw * r1.zwzw + float4(0.000651041686,-0.000651041686,-0.000651041686,0);
  r5.xyz = t_input.SampleLevel(s_bilinear_s, r4.xy, 0).xyz;
  r3.xyz = r5.xyz + r3.xyz;
  r4.xyz = t_input.SampleLevel(s_bilinear_s, r4.zw, 0).xyz;
  r3.xyz = r4.xyz + r3.xyz;
  r2.xyz = t_input.SampleLevel(s_bilinear_s, r2.xy, 0).xyz;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyzw = r0.zwzw * r1.zwzw + float4(0.000651041686,0,-0.000651041686,0.000651041686);
  r4.xyz = t_input.SampleLevel(s_bilinear_s, r3.xy, 0).xyz;
  r2.xyz = r4.xyz + r2.xyz;
  r3.xyz = t_input.SampleLevel(s_bilinear_s, r3.zw, 0).xyz;
  r2.xyz = r3.xyz + r2.xyz;
  r0.xyzw = r0.xyzw * r1.xyzw + float4(0,0.000651041686,0.000651041686,0.000651041686);
  r1.xyz = t_input.SampleLevel(s_bilinear_s, r0.xy, 0).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r0.xyz = t_input.SampleLevel(s_bilinear_s, r0.zw, 0).xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r0.xyz = float3(0.111111112,0.111111112,0.111111112) * r0.xyz;
  r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.x = max(g_min_luma_black_point, r0.w);
  r1.x = min(g_max_luma_white_point, r1.x);
  t_rw_downsampled_lab[vThreadID.xy] = r0.xyzw;
  r0.x = -g_min_luma_black_point + r1.x;
  r0.y = g_max_luma_white_point + -g_min_luma_black_point;
  r0.x = r0.x / r0.y;
  r0.x = 4.2949673e+009 * r0.x;
  const uint quantised = (uint)r0.x;
  uint metered_max = quantised;
  // RenoDX: last frame's sum is 2^32 x its average between the limits (2^18 samples of 14 bits).
  if (AutoExposureHighlightProtection > 0.f) {
    const uint last_sum = t_luma_last_sum.Load(0);
    if (last_sum != 0u) {
      const float last_average = g_min_luma_black_point + (r0.y * ((float)last_sum / 4294967296.f));
      const float ceiling = last_average * exp2(clamp(AutoExposureHighlightHeadroomStops, 0.f, 16.f));
      metered_max = min(quantised, (uint)(4.2949673e+009 * (max(ceiling - g_min_luma_black_point, 0.f) / r0.y)));
    }
  }
  InterlockedMin(g_luma_stats[0], quantised);
  InterlockedMax(g_luma_stats[1], metered_max);
  InterlockedAdd(g_luma_stats[2], quantised >> 18);
  GroupMemoryBarrierWithGroupSync();
  if ((vThreadIDInGroup.y | vThreadIDInGroup.x) == 0u) {
    const uint3 group = uint3(g_luma_stats[0], g_luma_stats[1], g_luma_stats[2]);
    t_rw_luma_stats.InterlockedMin(0, group.x);
    t_rw_luma_stats.InterlockedMax(4, group.y);
    t_rw_luma_stats.InterlockedAdd(8, group.z);
  }
}
