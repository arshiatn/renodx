// particle_light.hlsl ps30_main: light from particles (fire) on the scene.
// Render scale (DLSS addon): the CPU's g_viewport_offset_scale stays the full screen while the 3D passes draw into
// the top-left render scale part: the viewport comes from the camera instead (g_viewport_dimensions, patched to the
// render size by the DLSS addon; the game's own "respect the viewport" formula). At full size the same as the game's.
// HDR: x VFX Brightness: Firelight.
#include "../shared.h"

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

cbuffer shroud_PS : register(b1)
{
  float g_shroud_enabled : packoffset(c0) = {0};
  float g_campaign_autumn_timer : packoffset(c0.y) = {0};
  float4 g_campaign_world_bounds : packoffset(c1) = {0,0,1,1};
  float4 g_campaign_playable_area_bounds : packoffset(c2) = {0,0,1,1};
  float g_snow_height : packoffset(c3) = {1.39999998};
  float g_rock_slope_scale : packoffset(c3.y) = {0.400000006};
  float g_rock_height_min : packoffset(c3.z) = {0.400000006};
  float g_rock_height_max : packoffset(c3.w) = {1.39999998};
}

cbuffer particle_light_PS : register(b2)
{
  float4 g_viewport_offset_scale : packoffset(c0);
}

SamplerState s_shroud_s : register(s0);
Texture2D<float4> s_shroud : register(t0);
Texture2D<float4> gbuffer_normal_texture : register(t1);
Texture2D<float4> gbuffer_colour_texture : register(t2);
Texture2D<float4> gbuffer_constants_texture : register(t3);
Texture2D<float4> gbuffer_readable_depth_texture : register(t4);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_texel_offset + v0.xy;
  r0.xy = r0.xy / screen_size.xy;
  gbuffer_colour_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r1.xy = (uint2)r0.zw;
  r1.xy = r1.xy * r0.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r0.zw);
  r1.zw = float2(0,0);
  r2.xyzw = gbuffer_normal_texture.Load(r1.xyw).xyzw;
  r3.xyzw = gbuffer_colour_texture.Load(r1.xyw).xyzw;
  r0.zw = gbuffer_constants_texture.Load(r1.xyw).xy;
  r1.z = gbuffer_readable_depth_texture.Load(r1.xyz).x;
  r3.xyzw = r3.xyzw * float4(2,2,2,2) + float4(-1,-1,-1,-1);
  r4.x = dot(r3.xyz, r3.xyz);
  r4.x = rsqrt(r4.x);
  r3.xyz = r4.xxx * r3.xyz;
  r4.x = 254.999985 * r0.w;
  r2.w = r2.w * 2 + -1;
  r4.y = cmp(v3.w < 0);
  if (r4.y != 0) discard;
  r4.y = 0.5 * v2.x;
  r5.xyz = v1.xyz * v1.www;
  r6.xyz = v2.www * r5.xyz;
  r4.z = v4.z / v4.w;
  r4.z = -r4.z + r1.z;
  r4.z = cmp(r4.z < 0);
  if (r4.z != 0) discard;
  r0.xy = (r0.xy * g_render_target_dimensions.xy - g_viewport_origin) * g_viewport_dimensions.zw;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r7.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r7.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r7.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.x = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r1.xyz = r7.xyz / r0.xxx;
  r0.x = cmp(0 != g_shroud_enabled);
  if (r0.x != 0) {
    r0.xy = -g_campaign_world_bounds.xy + r1.xz;
    r4.zw = g_campaign_world_bounds.zw + -g_campaign_world_bounds.xy;
    r0.xy = r0.xy / r4.zw;
    r0.x = s_shroud.SampleLevel(s_shroud_s, r0.xy, r0.y).x;
    r0.x = saturate(-0.5 + r0.x);
    r0.x = r0.x + r0.x;
  } else {
    r0.x = 1;
  }
  r0.x = min(1, r0.x);
  r0.x = -0.00392156886 + r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r7.xyz = -camera_position.xyz + r1.xyz;
  r0.x = dot(r7.xyz, r7.xyz);
  r0.x = rsqrt(r0.x);
  r7.xyz = r7.xyz * r0.xxx;
  r1.xyz = -v3.xyz + r1.xyz;
  r0.x = dot(r1.xyz, r1.xyz);
  r0.y = rsqrt(r0.x);
  r1.xyz = r1.xyz * r0.yyy;
  r0.x = sqrt(r0.x);
  r0.x = saturate(r0.x / r4.y);
  r0.x = 1 + -r0.x;
  r0.y = dot(r3.xyz, -r1.xyz);
  r0.y = max(0, r0.y);
  r1.w = dot(r1.xyz, r3.xyz);
  r1.w = r1.w + r1.w;
  r1.xyz = r3.xyz * -r1.www + r1.xyz;
  r1.x = dot(r1.xyz, -r7.xyz);
  r1.x = max(0, r1.x);
  r1.y = max(0.00100000005, r4.x);
  r1.x = log2(r1.x);
  r1.x = r1.y * r1.x;
  r1.x = exp2(r1.x);
  r0.w = r0.w * 254.999985 + 2;
  r0.w = r0.w * r1.x;
  r0.yw = float2(1.90384671e-005,0.5) * r0.yw;
  r0.w = max(0, r0.w);
  r0.xz = r0.xz * r0.yw;
  r0.w = cmp(abs(r2.w) == 1.000000);
  r1.xyz = r0.yyy * r2.xyz;
  r1.xyz = r1.xyz * r6.xyz;
  r1.xyz = r0.www ? r1.xyz : r2.xyz;
  r3.xyz = r5.xyz * v2.www + -r2.xyz;
  r2.xyz = abs(r3.www) * r3.xyz + r2.xyz;
  r0.yzw = r0.zzz * r2.xyz + r1.xyz;
  r0.xyz = r0.xxx * r0.yzw;
  o0.xyz = r0.xyz * abs(r2.www);
  if (HDR >= 1.f) o0.xyz *= SI.vfx_firelight;  // VFX Brightness: Firelight
  o0.w = 1;
  return;
}