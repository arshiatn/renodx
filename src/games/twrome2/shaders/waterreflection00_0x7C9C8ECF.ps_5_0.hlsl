// ---- Created with 3Dmigoto v1.3.16 on Mon Oct 26 08:23:01 2026
// The water's screen-space reflections (helpers.hlsl). RenoDX: with Reflection Method Hierarchical, the march is
// Attila's hierarchical one (twattila/ssr_march.hlsli, in world space here) instead of the game's fixed 200 x quality
// steps: no gaps, no length limit.
#include "../shared.h"

cbuffer screen_space_refl_params : register(b0)
{
  float4x4 view_projection_matrix : packoffset(c0);
  float4x4 inv_view_projection_matrix : packoffset(c4);
  float4 camera_position_and_reflection_height : packoffset(c8);
  float min_water_height : packoffset(c9);
  float max_water_height : packoffset(c9.y);
  float tiles_per_dim : packoffset(c9.z);
  float playable_area_size : packoffset(c9.w);
  float quality_factor : packoffset(c10);
}

SamplerState scene_sam_s : register(s0);
SamplerState depth_sam_s : register(s1);
SamplerState water_height_sam_s : register(s2);
Texture2D<float4> scene_tex : register(t0);
Texture2D<float4> depth_tex : register(t1);
Texture2D<float4> water_height_tex : register(t2);

// The march's space: world space (the shader has the camera's view_projection only). No normals here: never a back face
// (the ray's own direction reversed).
static float3 water_ray_direction;

float3 FFX_SSSR_LoadWorldSpaceNormal(int2 pixel)
{
  return -water_ray_direction;
}

float3 FFX_SSSR_ScreenSpaceToViewSpace(float3 screen_position)
{
  const float4 world = mul(float4(screen_position.xy * float2(2.f, -2.f) + float2(-1.f, 1.f), screen_position.z, 1.f),
                           inv_view_projection_matrix);
  return world.xyz / world.w;
}

#include "../../twattila/ssr_march.hlsli"

// A world position's screen position: uv and depth (0 near, 1 far).
float3 WaterProject(float3 world_position)
{
  const float4 clip = mul(float4(world_position, 1.f), view_projection_matrix);
  return float3(clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f, clip.z / clip.w);
}

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v1.xy * float2(1,-1) + float2(0,1);
  r0.xyzw = depth_tex.SampleLevel(depth_sam_s, r0.xy, 0).yzxw;
  r0.xy = v1.xy * float2(2,2) + float2(-1,-1);
  r0.w = 1;
  r1.x = dot(r0.xyzw, inv_view_projection_matrix._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, inv_view_projection_matrix._m01_m11_m21_m31);
  r1.z = dot(r0.xyzw, inv_view_projection_matrix._m02_m12_m22_m32);
  r0.x = dot(r0.xyzw, inv_view_projection_matrix._m03_m13_m23_m33);
  r0.x = 1 / r0.x;
  r0.yz = r1.xz * r0.xx;
  r0.w = 0.5 * playable_area_size;
  r0.yz = r0.yz / r0.ww;
  r0.yz = float2(1,1) + r0.yz;
  r0.yz = float2(0.5,0.5) * r0.yz;
  r2.xyzw = water_height_tex.Sample(water_height_sam_s, r0.yz).xyzw;
  r0.y = -0.5 + r2.w;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;
  r0.y = max_water_height + -min_water_height;
  r0.y = r2.x * r0.y + min_water_height;
  r1.xzw = r1.xyz * r0.xxx + -camera_position_and_reflection_height.xyz;
  r0.y = camera_position_and_reflection_height.y + -r0.y;
  r1.xzw = r1.xzw * r0.yyy;
  r0.x = -r1.y * r0.x + camera_position_and_reflection_height.y;
  r0.xzw = r1.xzw / r0.xxx;
  r1.xyz = camera_position_and_reflection_height.xyz + r0.xzw;

  // RenoDX: from the water's surface along the view ray mirrored in it (r1.xyz; r0.y: the camera's height above it,
  // the game's fade from 150 m up kept).
  [branch]
  if (SsrHierarchicalReady()) {
    water_ray_direction = normalize(r0.xzw) * float3(1.f, -1.f, 1.f);
    // Its end in front of the near plane (clip z 0): the screen-space line is then the projection of the straight one.
    const float start_z = dot(float4(r1.xyz, 1.f), view_projection_matrix._m02_m12_m22_m32);
    const float z_step = dot(water_ray_direction, view_projection_matrix._m02_m12_m22_m32.xyz);
    float ray_length = 10000.f;
    if (z_step < 0.f) ray_length = min(ray_length, 0.99f * start_z / -z_step);
    const float3 origin = WaterProject(r1.xyz);
    float3 hit;
    const float confidence =
        SsrTrace(origin, WaterProject(r1.xyz + water_ray_direction * ray_length) - origin, water_ray_direction,
                 (uint)clamp(128.f * quality_factor, 32.f, 512.f), camera_position_and_reflection_height.xyz, hit);
    o0.xyz = confidence > 0.f ? scene_tex.SampleLevel(scene_sam_s, hit.xy, 0).xyz : 0.f;
    o0.w = confidence * saturate(0.01f * (250.f - r0.y));
    return;
  }

  r2.xz = r1.xz * float2(2,2) + -camera_position_and_reflection_height.xz;
  r2.y = camera_position_and_reflection_height.y;
  r2.w = 1;
  r3.x = dot(r2.xyzw, view_projection_matrix._m00_m10_m20_m30);
  r3.y = dot(r2.xyzw, view_projection_matrix._m01_m11_m21_m31);
  r0.x = dot(r2.xyzw, view_projection_matrix._m02_m12_m22_m32);
  r0.z = dot(r2.xyzw, view_projection_matrix._m03_m13_m23_m33);
  r0.z = 1 / r0.z;
  r2.xy = r3.xy * r0.zz;
  r2.xy = r2.yx * float2(-0.5,0.5) + float2(0.5,0.5);
  r2.zw = float2(0.00499999989,0.00300000003) / quality_factor;
  r0.w = 1 + r0.y;
  r0.w = r2.z / r0.w;
  r1.w = 1;
  r3.x = dot(r1.xyzw, view_projection_matrix._m00_m10_m20_m30);
  r3.y = dot(r1.xyzw, view_projection_matrix._m01_m11_m21_m31);
  r3.z = dot(r1.xyzw, view_projection_matrix._m02_m12_m22_m32);
  r1.x = dot(r1.xyzw, view_projection_matrix._m03_m13_m23_m33);
  r1.x = 1 / r1.x;
  r1.xyz = r3.xyz * r1.xxx;
  r1.xy = r1.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  r1.w = 200 * quality_factor;
  r2.xy = r2.xy + -r1.yx;
  r2.x = 1 / r2.x;
  r2.y = -r2.w * r2.y;
  r0.x = r0.x * r0.z + -r1.z;
  r0.x = -r2.w * r0.x;
  r3.xyz = r1.xyz;
  r3.w = 0;
  while (true) {
    r0.z = cmp(r3.w >= r1.w);
    if (r0.z != 0) break;
    r4.xyzw = depth_tex.SampleLevel(depth_sam_s, r3.xy, 0).xyzw;
    r0.z = cmp(r4.x == 1.000000);
    r0.z = r0.z ? 10000 : r4.x;
    r2.z = cmp(r0.z < r3.z);
    r4.x = r3.z + -r0.w;
    r0.z = cmp(r4.x < r0.z);
    r0.z = r0.z ? r2.z : 0;
    if (r0.z != 0) {
      break;
    }
    r3.x = r2.y * r2.x + r3.x;
    r3.y = r3.y + -r2.w;
    r3.z = r0.x * r2.x + r3.z;
    r3.w = 1 + r3.w;
  }
  r0.xz = float2(1,1) + -r3.yx;
  r0.xz = min(r3.yx, r0.xz);
  r0.xz = r0.xz * float2(20,30) + float2(0.200000003,0.200000003);
  r0.x = saturate(min(r0.x, r0.z));
  r0.z = 7 / r1.w;
  r0.z = -r3.w * r0.z + 7;
  r0.x = min(r0.x, r0.z);
  r0.y = 250 + -r0.y;
  r0.y = saturate(0.00999999978 * r0.y);
  o0.w = min(r0.x, r0.y);
  r0.xyzw = scene_tex.Sample(scene_sam_s, r3.xy).xyzw;
  o0.xyz = r0.xyz;
  return;
}
