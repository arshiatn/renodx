// rigidbranch_v3.hlsl ps30_gbuffer: the trees' trunks and branches on the G-buffer.
// Motion vectors of the tree format (rigidleafcommon_v3 / rigidbranchcommon_v3): the vertex shader blends the
// two instance transforms with the vertex's half-precision bone weights and position.w, s = position.w x
// (weight0 + weight1), not exactly 1: it draws the homogeneous (s p, s) (= p) and passes the previous position as
// (s p_prev, s), which calc_motion_vector read with w = 1: s p_prev, off by (s - 1) p (p world: hundreds of meters),
// so a still tree moved by pixels (ATN debug15, paused: up to 1000 px, signs per branch) and DLSS dropped its
// history every frame (flowering bushes and trunks flicker below 100%). RenoDX: the previous position with its w.
// Decompile fixes: input widths, the flags compare (g_untextured, snow).

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

cbuffer wind_PS : register(b1)
{
  float2 wind_direction_PS : packoffset(c0) = {0,0};
}

cbuffer rigid_config_PS : register(b2)
{
  float g_unlit : packoffset(c0);
  float g_dissolve_enable : packoffset(c0.y);
  float g_untextured : packoffset(c0.z);
  float snow : packoffset(c0.w);
  float rain : packoffset(c1);
  float g_tesselation_factor : packoffset(c1.y);
  float g_tesselation_displacement_scale : packoffset(c1.z);
  float g_rain_rigid_roughness_multiplier : packoffset(c1.w);
  float g_rain_vegetation_roughness_multiplier : packoffset(c2);
}

SamplerState s_diffuse_map_s : register(s0);
SamplerState s_normal_map_s : register(s1);
SamplerState s_gloss_map_s : register(s2);
Texture2D<float4> s_diffuse_map : register(t0);
Texture2D<float4> s_gloss_map : register(t1);
Texture2D<float4> s_normal_map : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float3 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  float3 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  float4 v8 : TEXCOORD6,
  float4 v9 : TEXCOORD7,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float2 o3 : SV_Target3,
  out float o4 : SV_Target4)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = dot(v5.xyz, v5.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v5.zxy * r0.xxx;
  r0.w = dot(v6.xyz, v6.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v6.zxy * r0.www;
  r2.xy = s_normal_map.Sample(s_normal_map_s, v3.xy).yw;
  r2.xy = float2(0.00196078443,0.00196078443) + r2.yx;
  r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
  r1.xyz = r2.yyy * r1.xyz;
  r0.xyz = r2.xxx * r0.xyz + r1.xyz;
  r0.w = dot(r2.xy, r2.xy);
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = sqrt(r0.w);
  r1.x = dot(v4.xyz, v4.xyz);
  r1.x = rsqrt(r1.x);
  r1.xyz = v4.zxy * r1.xxx;
  r0.xyz = r0.www * r1.xyz + r0.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.w = max(abs(r0.x), abs(r0.y));
  r0.w = 1 / r0.w;
  r1.x = min(abs(r0.x), abs(r0.y));
  r0.w = r1.x * r0.w;
  r1.x = r0.w * r0.w;
  r1.y = r1.x * 0.0208350997 + -0.0851330012;
  r1.y = r1.x * r1.y + 0.180141002;
  r1.y = r1.x * r1.y + -0.330299497;
  r1.x = r1.x * r1.y + 0.999866009;
  r1.y = r1.x * r0.w;
  r1.y = r1.y * -2 + 1.57079637;
  r1.z = cmp(abs(r0.y) < abs(r0.x));
  r1.y = r1.z ? r1.y : 0;
  r0.w = r0.w * r1.x + r1.y;
  r1.xy = cmp(r0.yz < -r0.yz);
  r1.x = r1.x ? -3.14159274 : 0;
  r0.w = r1.x + r0.w;
  r1.x = min(r0.x, r0.y);
  r1.x = cmp(r1.x < -r1.x);
  r1.z = max(r0.x, r0.y);
  r1.z = cmp(r1.z >= -r1.z);
  r1.x = r1.z ? r1.x : 0;
  r0.w = r1.x ? -r0.w : r0.w;
  r0.w = 3.14159274 + r0.w;
  o0.y = saturate(0.159154937 * r0.w);
  r0.w = abs(r0.z) * -0.0187292993 + 0.0742610022;
  r0.w = r0.w * abs(r0.z) + -0.212114394;
  r0.w = r0.w * abs(r0.z) + 1.57072878;
  r1.x = 1 + -abs(r0.z);
  r1.x = sqrt(r1.x);
  r1.z = r1.x * r0.w;
  r1.z = r1.z * -2 + 3.14159274;
  r1.y = r1.y ? r1.z : 0;
  r0.w = r0.w * r1.x + r1.y;
  r0.w = 3.14159274 + -r0.w;
  o0.x = saturate(0.318309873 * r0.w);
  o0.zw = saturate(float2(0,0));
  r1.xz = wind_direction_PS.xy;
  r1.y = 1;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r0.x = dot(r0.yzx, r1.xyz);
  r0.x = saturate(4 * r0.x);
  r0.x = 1.20000005 * r0.x;
  r1.xyz = s_diffuse_map.Sample(s_diffuse_map_s, v3.xy).xyz;
  r0.yw = cmp(float2(0,0) != float2(g_untextured, snow));
  r1.xyz = r0.yyy ? float3(1,1,1) : r1.xyz;
  r2.xyzw = s_gloss_map.Sample(s_gloss_map_s, v3.xy).xyzw;
  r1.xyz = r2.zzz * -r1.xyz + r1.xyz;
  r3.xy = saturate(float2(5,20) * r1.xx);
  r0.y = r0.z * 0.5 + r3.x;
  r0.z = 1 + -r3.y;
  r0.y = r0.y + -r0.z;
  r0.x = saturate(r0.x * r0.y + r0.z);
  r3.xyz = float3(0.600000024,0.600000024,0.649999976) + -r1.xyz;
  r3.xyz = r0.xxx * r3.xyz + r1.xyz;
  o1.xyz = saturate(r0.www ? r3.xyz : r1.xyz);
  o1.w = 0;
  r0.y = saturate(r2.y + r2.y);
  r0.y = r0.y + -r2.y;
  r1.y = r0.x * r0.y + r2.y;
  r1.x = r0.x * -r2.x + r2.x;
  o2.xy = saturate(r0.ww ? r1.xy : r2.xy);
  o2.z = saturate(r2.w);
  o2.w = saturate(0.0196078438);
  r0.xyzw = v9.xyzw;  // RenoDX: was (.xyz, 1)
  r1.x = dot(r0.xyzw, prev_view_projection._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, prev_view_projection._m01_m11_m21_m31);
  r0.x = dot(r0.xyzw, prev_view_projection._m03_m13_m23_m33);
  r0.xy = r1.xy / r0.xx;
  r0.xy = g_camera_jitter.xy + r0.xy;
  r0.zw = -g_viewport_origin.xy + v0.xy;
  r0.zw = g_viewport_dimensions.zw * r0.zw;
  r1.xy = r0.zw * float2(2,2) + float2(-1,-1);
  r1.z = -r1.y;
  r0.xy = r1.xz + -r0.xy;
  r0.xy = g_prev_camera_jitter.xy + r0.xy;
  r0.xy = g_viewport_dimensions.xy * r0.xy;
  o3.xy = float2(0.5,0.5) * r0.xy;
  return;
}