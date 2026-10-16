// rigidstandard.hlsl ps30_gbuffer_alpha_test: alpha-tested rigid meshes (plants, bushes) into the G-buffer.
// get_surface_colour (rigidstandardfragments.hlsl): a fading mesh (g_dissolve_enable: LOD changes, distance
// fades) clips by a 64 x 64 dissolve pattern looked up at its jittered clip position x screen_size / 64: with
// DLSS's camera jitter every pixel read another texel each frame (3 texels per render pixel at 33%), so the
// holes flickered. RenoDX: the pattern on the render pixels (SV_Position), as the game without jitter at full
// size. Decompile fixes: TEXCOORD4 / 6 float3 (the original signature), the rigid_config flags compare.

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

cbuffer rigid_config_PS : register(b1)
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

cbuffer rigid_lighting_PS : register(b2)
{
  float g_Ks_gloss_factor : packoffset(c0);
  float g_Exps_gloss_factor : packoffset(c0.y);
  float g_Ks_colour_factor : packoffset(c0.z);
  float g_Ks_colour_saturate_factor : packoffset(c0.w);
  float g_Kf_colour_factor : packoffset(c1);
  float g_Ks : packoffset(c1.y);
  float g_Exps : packoffset(c1.z);
  float g_Kf : packoffset(c1.w);
  float g_Expf : packoffset(c2);
}

SamplerState s_diffuse_map_s : register(s0);
SamplerState s_normal_map_s : register(s1);
SamplerState s_gloss_map_s : register(s2);
SamplerState s_dissolve_map_s : register(s3);
Texture2D<float4> s_diffuse_map : register(t0);
Texture2D<float4> s_dissolve_map : register(t1);
Texture2D<float4> s_gloss_map : register(t2);
Texture2D<float4> s_normal_map : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float3 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float3 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7,
  float4 v9 : COLOR0,
  float4 v10 : COLOR1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(g_Ks_gloss_factor >= 0);
  r0.y = floor(v6.w);
  r0.z = 0.00100000005 * r0.y;
  r0.w = cmp(0 < v6.x);
  r1.x = cmp(v6.x < 0);
  r0.w = (int)-r0.w + (int)r1.x;
  r0.w = (int)r0.w;
  r1.xyzw = s_diffuse_map.Sample(s_diffuse_map_s, v2.xy).wxyz;
  r2.xyz = cmp(float3(0,0,0) != float3(g_unlit, g_dissolve_enable, g_untextured));
  if (r2.y != 0) {
    // RenoDX: the 64 x 64 dissolve pattern on the render pixels (the game's: the jittered clip position x
    // screen_size / 64, so with DLSS's jitter (and 3 texels per pixel at 33%) every pixel read another texel each
    // frame). Without jitter at full size the same texels as the game's.
    r2.yw = float2(0.015625,0.015625) * v0.xy;
    r2.y = s_dissolve_map.Sample(s_dissolve_map_s, r2.yw).x;
    r0.y = -r0.y * 0.00100000005 + 1;
    r1.x = -r2.y * r0.y + r0.z;
  } else {
    r0.y = r2.x ? 1 : r0.z;
    r1.x = r1.x * r0.y;
  }
  r1.xyzw = r2.zzzz ? v10.xyzw : r1.yzwx;
  r0.y = -0.5 + r1.w;
  r0.yw = cmp(r0.yw < float2(0,0));
  if (r0.y != 0) discard;
  r3.xyzw = s_gloss_map.Sample(s_gloss_map_s, v2.xy).xyzw;
  r2.yzw = r1.xyz * abs(v6.xyz) + -r1.xyz;
  r1.xyz = r3.zzz * r2.yzw + r1.xyz;
  r0.y = dot(v4.xyz, v4.xyz);
  r0.y = rsqrt(r0.y);
  r2.yzw = v4.zxy * r0.yyy;
  r0.y = dot(v5.xyz, v5.xyz);
  r0.y = rsqrt(r0.y);
  r4.xyz = v5.zxy * r0.yyy;
  r0.y = dot(v3.xyz, v3.xyz);
  r0.y = rsqrt(r0.y);
  r5.xyz = v3.zxy * r0.yyy;
  if (r0.x != 0) {
    r6.xyz = s_normal_map.Sample(s_normal_map_s, v2.xy).xzw;
    r6.xyz = r6.xzy * float3(2,2,2) + float3(-1,-1,-1);
  } else {
    r0.yz = s_normal_map.Sample(s_normal_map_s, v2.xy).yw;
    r0.yz = float2(0.00196078443,0.00196078443) + r0.zy;
    r6.xy = r0.yz * float2(2,2) + float2(-1,-1);
    r0.y = dot(r6.xy, r6.xy);
    r0.y = 1 + -r0.y;
    r0.y = max(0, r0.y);
    r6.z = sqrt(r0.y);
  }
  r4.xyz = r6.yyy * r4.xyz;
  r2.yzw = r6.xxx * r2.yzw + r4.xyz;
  r2.yzw = r6.zzz * r5.xyz + r2.yzw;
  r0.y = dot(r2.yzw, r2.yzw);
  r0.y = rsqrt(r0.y);
  r4.xyz = r2.yzw * r0.yyy;
  r5.y = r3.y * r3.y;
  r5.z = -r3.x * r3.x + 1;
  r5.w = 1;
  r3.xyz = r0.xxx ? r5.yzw : r3.xyw;
  r0.x = saturate(r2.w * r0.y + -0.600000024);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = r0.y * r0.x;
  r0.x = 1.5 * r0.x;
  r0.y = saturate(1.5 * r3.y);
  r0.x = r0.x * r0.y;
  r0.x = min(1, r0.x);
  r2.yzw = float3(0.5,0.5,0.649999976) + -r1.xyz;
  r0.xyz = r0.xxx * r2.yzw + r1.xyz;
  o1.xyz = saturate(r0.www ? r0.xyz : r1.xyz);
  r0.x = min(abs(r4.x), abs(r4.y));
  r0.y = max(abs(r4.x), abs(r4.y));
  r0.y = 1 / r0.y;
  r0.x = r0.x * r0.y;
  r0.y = r0.x * r0.x;
  r0.z = r0.y * 0.0208350997 + -0.0851330012;
  r0.z = r0.y * r0.z + 0.180141002;
  r0.z = r0.y * r0.z + -0.330299497;
  r0.y = r0.y * r0.z + 0.999866009;
  r0.z = r0.x * r0.y;
  r0.w = cmp(abs(r4.y) < abs(r4.x));
  r0.z = r0.z * -2 + 1.57079637;
  r0.z = r0.w ? r0.z : 0;
  r0.x = r0.x * r0.y + r0.z;
  r0.yz = cmp(r4.yz < -r4.yz);
  r0.y = r0.y ? -3.14159274 : 0;
  r0.x = r0.x + r0.y;
  r0.y = min(r4.x, r4.y);
  r0.w = max(r4.x, r4.y);
  r0.y = cmp(r0.y < -r0.y);
  r0.w = cmp(r0.w >= -r0.w);
  r0.y = r0.w ? r0.y : 0;
  r0.x = r0.y ? -r0.x : r0.x;
  r0.y = 1 + -abs(r4.z);
  r0.y = sqrt(r0.y);
  r0.w = abs(r4.z) * -0.0187292993 + 0.0742610022;
  r0.w = r0.w * abs(r4.z) + -0.212114394;
  r0.w = r0.w * abs(r4.z) + 1.57072878;
  r1.x = r0.w * r0.y;
  r1.x = r1.x * -2 + 3.14159274;
  r0.z = r0.z ? r1.x : 0;
  r0.y = r0.w * r0.y + r0.z;
  r0.y = 3.14159274 + -r0.y;
  r0.x = 3.14159274 + r0.x;
  o0.xy = saturate(float2(0.318309873,0.159154937) * r0.yx);
  o0.zw = saturate(float2(0,0));
  o1.w = 0;
  r3.w = r2.x ? 0 : 0.00392156886;
  o2.xyzw = saturate(r3.xyzw);
  return;
}