#include "../shared.h"

// ---- Reconstructed from 0xACE33D5D.ps_5_0.cso
// ---- Total War: Attila VFX pixel shader (Shader Model 5.0)
//
// This keeps the stock shader path unchanged. 3Dmigoto's initial decompilation
// mislabeled reads from cb2[0].yzw as the bool at cb2[0].x, emitted incorrect
// dynamic indices for the split-distance array, and omitted early-depth mode.
//
// Required shared.h controls (use 1.0f for stock behavior):
//   VfxBaseBrightness, VfxSnowBrightness, VfxWeatherBrightness,
//   VfxNormalStrength, VfxShadowStrength, VfxLightingStrength,
//   VfxSpecularBrightness, VfxReflectionBrightness,
//   VfxSoftParticleStrength, VfxOpacity, VfxOverallBrightness,
//   VfxFogAmount, VfxFogBrightness.
// VfxFireBrightness is retained in shared.h only to preserve the existing
// injection-buffer layout; this shader now groups all non-weather VFX under
// VfxBaseBrightness.

cbuffer camera_VS_PS : register(b0)
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
  float4 g_render_target_dimensions : packoffset(c30);
  float4 g_camera_temp0 : packoffset(c31);
  float4 g_camera_temp1 : packoffset(c32);
  float4 g_camera_temp2 : packoffset(c33);
  float4 g_clip_rect : packoffset(c34);
  int g_num_of_samples : packoffset(c35);
}

cbuffer shadowmap_PS : register(b1)
{
  float4 g_vHardShadowBufferSize : packoffset(c0);
  float4 g_vSoftShadowBufferSize : packoffset(c1);
  float4x4 g_amHardSplit[4] : packoffset(c2);
  float4x4 g_amSoftSplit[4] : packoffset(c18);
  float2 g_fFadeRange : packoffset(c34);
  float g_sample_bias : packoffset(c34.z);
  float g_iSplitCount : packoffset(c34.w);
  float3 g_shadow_light_direction : packoffset(c35);
  float g_split_distances[5] : packoffset(c36);
  float4 g_split_uv_texture_bounds[4] : packoffset(c41);
}

cbuffer lighting_VS_PS : register(b2)
{
  bool g_apply_environment_specular : packoffset(c0);
  float3 sun_direction : packoffset(c0.y);
  float3 sun_colour : packoffset(c1);
  float3 ambient_cube_lr[2] : packoffset(c2);
  float3 ambient_cube_tb[2] : packoffset(c4);
  float3 ambient_cube_fb[2] : packoffset(c6);
  float3 g_deep_water_colour : packoffset(c8);
  float3 g_shallow_water_colour : packoffset(c9);
  float3 g_sea_bed_light_scatter : packoffset(c10);
  float g_refraction_light_scatter : packoffset(c10.w);
  float g_hdr_on : packoffset(c11);
  bool g_ssr_enabled : packoffset(c11.y);
}

cbuffer fog_VS_PS : register(b3)
{
  float3 g_volume_fog_colour : packoffset(c0);
  float g_fog_distance_start : packoffset(c0.w);
  float g_fog_distance_strength : packoffset(c1);
  float g_fog_distance_scale : packoffset(c1.y);
  float g_fog_height_bottom : packoffset(c1.z);
  float g_fog_height_top : packoffset(c1.w);
  float g_fog_height_strength : packoffset(c2);
  float g_fog_colour_blend : packoffset(c2.y);
  float g_fog_clear_distance : packoffset(c2.z);
}

cbuffer cb_vfx : register(b4)
{
  float3 g_camera_aligned_x_axis : packoffset(c0);
  float3 g_camera_aligned_y_axis : packoffset(c1);
  float3 g_camera_aligned_z_axis : packoffset(c2);
  int g_view_id : packoffset(c2.w);
}

cbuffer cb_vfx_emitter_constants_common : register(b5)
{
  uint4 behaviour_mask[512] : packoffset(c0);
}

cbuffer cb_vfx_emitter_constants_ps : register(b6)
{
  float4 ps_specular_params[512] : packoffset(c0);
  float4 ps_specular_params1[512] : packoffset(c512);
  float4 ps_receive_shadows_high_quality_params[512] : packoffset(c1024);
  float4 ps_lighting_legacy_params[512] : packoffset(c1536);
  float4 ps_lighting_params[512] : packoffset(c2048);
  float4 ps_alpha_crush_params[512] : packoffset(c2560);
  float4 ps_soft_edge_params[512] : packoffset(c3072);
}

SamplerState s_sky_s : register(s0);
SamplerState s_environment_s : register(s1);
SamplerState gbuffer_channel_4_sampler_s : register(s3);
SamplerState g_sam_diffuse_s : register(s4);
SamplerState g_sam_normal_s : register(s5);
SamplerComparisonState sShadowBuffer_SM50_s : register(s2);
TextureCube<float4> s_environment : register(t0);
Texture2D<float4> gbuffer_channel_4_sampler : register(t1);
TextureCube<float4> s_sky : register(t2);
Texture2D<float4> g_tShadowBuffer_SM50 : register(t3);
Texture2DArray<float4> g_tex_diffuse : register(t4);
Texture2DArray<float4> g_tex_normal : register(t5);


// Preserve 3Dmigoto's comparison convention: true becomes numeric -1 so
// subsequent casts and bitwise masks reproduce the untyped DXBC registers.
#define cmp -


[earlydepthstencil]
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  nointerpolation uint4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float2 w3 : LOCAL_POS0,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float3 v7 : TEXCOORD5,
  float3 v8 : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;
  uint4 bitmask, uiDest;
  float4 fDest;
  float3 vfxUnlitColour;

  r0.x = v2.x;
  r0.y = (uint)ps_alpha_crush_params[r0.x].x;
  r1.xyzw = int4(0x20000,0x400000,0x80000,0x800000) & (int4)behaviour_mask[r0.x].xxxx;
  if (r1.x != 0) {
    r2.xyz = ddx_coarse(v3.xyx);
    r3.xyz = ddy_coarse(v3.xyx);
    r4.z = (uint)behaviour_mask[r0.x].y;
    r4.xy = v3.xy;
    // Texture2DArray uses float3 coordinates, but only float2 UV gradients.
    r5.xyzw = g_tex_diffuse.SampleGrad(g_sam_diffuse_s, r4.xyz, r2.xy, r3.xy).xyzw;
    r4.xy = v8.xy;
    r2.xyzw = g_tex_diffuse.SampleGrad(g_sam_diffuse_s, r4.xyz, r2.xy, r3.xy).xyzw;
    r2.xyzw = r2.xyzw + -r5.xyzw;
    r2.xyzw = v8.zzzz * r2.xyzw + r5.xyzw;
    r3.xyz = min(r2.xyz, r2.www);
    r3.xyz = v1.xyz * r3.xyz;
    r3.xyz = v1.www * r3.xyz;
    r3.w = v1.w * r2.w;
    r2.xyz = saturate(r2.xyz + -r2.www);
    r4.xyz = v1.xyz * v1.www;
    r2.xyz = r4.xyz * r2.xyz;
  } else {
    r3.xyzw = v1.xyzw;
    r2.xyz = float3(0,0,0);
  }
  r0.z = cmp(ps_alpha_crush_params[r0.x].y == ps_alpha_crush_params[r0.x].z);
  r0.w = -ps_alpha_crush_params[r0.x].w + 1;
  r1.x = max(ps_alpha_crush_params[r0.x].y, r0.w);
  r4.xy = float2(3276,4099) * v7.xx;
  r4.xy = frac(r4.xy);
  r1.x = -1 + r1.x;
  r1.x = r4.x * r1.x + 1;
  r0.w = ps_alpha_crush_params[r0.x].z * r0.w;
  r0.w = max(ps_alpha_crush_params[r0.x].y, r0.w);
  r0.w = -ps_alpha_crush_params[r0.x].z + r0.w;
  r0.w = r4.y * r0.w + ps_alpha_crush_params[r0.x].z;
  r0.z = r0.z ? r1.x : r0.w;
  r0.z = -ps_alpha_crush_params[r0.x].y + r0.z;
  r0.z = max(0.00100000005, r0.z);
  r0.w = -ps_alpha_crush_params[r0.x].y + v4.w;
  r0.z = saturate(r0.w / r0.z);
  r0.w = saturate(r3.w + -r0.z);
  r1.x = 1.00100005 + -r0.z;
  r0.w = r0.w / r1.x;
  r0.z = r0.w + -r0.z;
  r0.z = cmp(r0.z >= 0);
  r0.z = r0.z ? 1.000000 : 0;
  r0.z = r0.z * r0.w;
  r4.w = r0.y ? r0.z : r0.w;
  r0.y = cmp(r3.w != 0.000000);
  r0.z = r4.w / r3.w;
  r0.y = r0.y ? r0.z : 1;
  r4.xyz = r3.xyz * r0.yyy;
  r3.xyzw = r1.yyyy ? r4.xyzw : r3.xyzw;
  r4.xyz = r3.xyz + r2.xyz;
  r4.w = r3.w;
  r5.xyzw = cmp(r4.xyzw < float4(0.0199999996,0.0199999996,0.0199999996,0.0199999996));
  r0.yz = r5.zw ? r5.xy : 0;
  r0.y = (int)r0.z & (int)r0.y;
  if (r0.y != 0) discard;

  // The texture stores additive/emissive colour in r2 and its alpha-bounded
  // base colour in r3. Keep both components in the same material group so a
  // dedicated snow/weather control cannot leak back into fire or other VFX.
  // Apply controls after the stock discard so brightness does not change the
  // particle silhouette or resurrect pixels the original shader discarded.
  float materialBrightness = VfxBaseBrightness;
  if ((behaviour_mask[v2.x].x & 0x00020000u) != 0u) {
    uint diffuseSlice = behaviour_mask[v2.x].y;
    if (diffuseSlice == 1u) {
      // Screen/camera-following snowflakes.
      materialBrightness = VfxSnowBrightness;
    } else if (diffuseSlice == 7u) {
      // Camera-local fog, snowstorm haze, and sandstorm haze.
      materialBrightness = VfxWeatherBrightness;
    }
  }
  r2.xyz *= materialBrightness;
  r3.xyz *= materialBrightness;
  r4.xyz = r3.xyz + r2.xyz;
  vfxUnlitColour = r4.xyz;

  r0.y = cmp(0.100000001 < r4.w);
  r5.z = (uint)behaviour_mask[r0.x].z;
  r5.xy = v3.xy;
  r5.xyz = g_tex_normal.Sample(g_sam_normal_s, r5.xyz).xyz;
  if (r0.y != 0) {
    r0.yzw = int3(0x40000,0x100000,0x200000) & (int3)behaviour_mask[r0.x].xxx;
    if (r0.y != 0) {
      r5.xyz = r5.xyz * float3(2,2,2) + float3(-1,-1,-1);
    } else {
      r5.xyz = float3(0,0,1);
    }
    if (VfxNormalStrength != 1.0f) {
      r5.xyz = lerp(float3(0,0,1), r5.xyz, VfxNormalStrength);
    }
    if (r0.z != 0) {
      r6.xyz = camera_position.xyz + -v4.xyz;
      r0.y = dot(r6.xyz, r6.xyz);
      r0.y = rsqrt(r0.y);
      r6.xyz = r6.xyz * r0.yyy;
      r0.y = min(v5.x, v5.y);
      r0.y = ps_receive_shadows_high_quality_params[r0.x].y * r0.y;
      r7.xyzw = float4(0.166666672,0.333333343,0.5,0.666666687) * r0.yyyy;
      r8.xyz = v4.xyz;
      r8.w = 1;
      r0.z = dot(r8.xyzw, view_projection._m03_m13_m23_m33);
      r8.x = g_split_distances[1];
      r8.y = g_split_distances[2];
      r8.z = g_split_distances[3];
      r8.w = g_split_distances[4];
      r9.xyzw = cmp(r0.zzzz >= r8.xyzw);
      r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
      r1.x = dot(r9.xyzw, r9.xyzw);
      r1.y = (int)r1.x;
      r2.w = cmp(r1.x < g_iSplitCount);
      if (r2.w != 0) {
        r9.xyz = -g_shadow_light_direction.xyz * g_sample_bias + v4.xyz;
        r2.w = (uint)r1.y << 2;
        r9.w = 1;
        r10.x = dot(r9.xyzw, g_amHardSplit[r2.w/4]._m00_m10_m20_m30);
        r10.y = dot(r9.xyzw, g_amHardSplit[r2.w/4]._m01_m11_m21_m31);
        r10.z = dot(r9.xyzw, g_amHardSplit[r2.w/4]._m02_m12_m22_m32);
        r2.w = dot(r9.xyzw, g_amHardSplit[r2.w/4]._m03_m13_m23_m33);
        r9.xyz = r10.xyz / r2.www;
        r10.xy = cmp(r9.xy < g_split_uv_texture_bounds[r1.y].xy);
        r2.w = (int)r10.y | (int)r10.x;
        r10.xy = cmp(g_split_uv_texture_bounds[r1.y].zw < r9.xy);
        r5.w = (int)r10.y | (int)r10.x;
        r2.w = (int)r2.w | (int)r5.w;
        if (r2.w == 0) {
          r9.xy = r9.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
          r10.xy = floor(r9.xy);
          r10.zw = r10.xy / g_vHardShadowBufferSize.xy;
          r11.xyzw = g_tShadowBuffer_SM50.GatherCmp(sShadowBuffer_SM50_s, r10.zw, r9.z).xyzw;
          r9.xy = -r10.xy + r9.xy;
          r9.zw = r11.zy + -r11.wx;
          r9.xz = r9.xx * r9.zw + r11.wx;
          r2.w = r9.z + -r9.x;
          r2.w = r9.y * r2.w + r9.x;
        } else {
          r2.w = 1;
        }
      } else {
        r2.w = 1;
      }
      r5.w = -1 + g_iSplitCount;
      r1.x = cmp(r1.x == r5.w);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r1.y+1] + -g_split_distances[r1.y];
        r6.w = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r1.y+1];
        r0.z = -r1.x + r0.z;
        r0.z = saturate(r0.z / r6.w);
        r1.x = 1 + -r2.w;
        r2.w = r0.z * r1.x + r2.w;
      }
      r2.w = saturate(r2.w);
      r9.xyz = -r7.xxx * r6.xyz + v4.xyz;
      r9.xyz = view._m00_m10_m20 * float3(0.0143827796,0.0143827796,0.0143827796) + r9.xyz;
      r9.xyz = view._m01_m11_m21 * float3(0.0263274908,0.0263274908,0.0263274908) + r9.xyz;
      r9.w = 1;
      r0.z = dot(r9.xyzw, view_projection._m03_m13_m23_m33);
      r10.xyzw = cmp(r0.zzzz >= r8.xyzw);
      r10.xyzw = r10.xyzw ? float4(1,1,1,1) : 0;
      r1.x = dot(r10.xyzw, r10.xyzw);
      r1.y = (int)r1.x;
      r6.w = cmp(r1.x < g_iSplitCount);
      if (r6.w != 0) {
        r9.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r9.xyz;
        r6.w = (uint)r1.y << 2;
        r9.w = 1;
        r10.x = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m00_m10_m20_m30);
        r10.y = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m01_m11_m21_m31);
        r10.z = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m02_m12_m22_m32);
        r6.w = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m03_m13_m23_m33);
        r9.xyz = r10.xyz / r6.www;
        r10.xy = cmp(r9.xy < g_split_uv_texture_bounds[r1.y].xy);
        r6.w = (int)r10.y | (int)r10.x;
        r10.xy = cmp(g_split_uv_texture_bounds[r1.y].zw < r9.xy);
        r7.x = (int)r10.y | (int)r10.x;
        r6.w = (int)r6.w | (int)r7.x;
        if (r6.w == 0) {
          r9.xy = r9.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
          r10.xy = floor(r9.xy);
          r10.zw = r10.xy / g_vHardShadowBufferSize.xy;
          r11.xyzw = g_tShadowBuffer_SM50.GatherCmp(sShadowBuffer_SM50_s, r10.zw, r9.z).xyzw;
          r9.xy = -r10.xy + r9.xy;
          r9.zw = r11.zy + -r11.wx;
          r9.xz = r9.xx * r9.zw + r11.wx;
          r6.w = r9.z + -r9.x;
          r6.w = r9.y * r6.w + r9.x;
        } else {
          r6.w = 1;
        }
      } else {
        r6.w = 1;
      }
      r1.x = cmp(r5.w == r1.x);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r1.y+1] + -g_split_distances[r1.y];
        r7.x = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r1.y+1];
        r0.z = -r1.x + r0.z;
        r0.z = saturate(r0.z / r7.x);
        r1.x = 1 + -r6.w;
        r6.w = r0.z * r1.x + r6.w;
      }
      r6.w = saturate(r6.w);
      r0.z = r6.w + r2.w;
      r9.xyz = -r7.yyy * r6.xyz + v4.xyz;
      r9.xyz = view._m00_m10_m20 * float3(0.0504882,0.0504882,0.0504882) + r9.xyz;
      r9.xyz = view._m01_m11_m21 * float3(0.0324180014,0.0324180014,0.0324180014) + r9.xyz;
      r9.w = 1;
      r1.x = dot(r9.xyzw, view_projection._m03_m13_m23_m33);
      r10.xyzw = cmp(r1.xxxx >= r8.xyzw);
      r10.xyzw = r10.xyzw ? float4(1,1,1,1) : 0;
      r1.y = dot(r10.xyzw, r10.xyzw);
      r2.w = (int)r1.y;
      r6.w = cmp(r1.y < g_iSplitCount);
      if (r6.w != 0) {
        r9.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r9.xyz;
        r6.w = (uint)r2.w << 2;
        r9.w = 1;
        r10.x = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m00_m10_m20_m30);
        r10.y = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m01_m11_m21_m31);
        r10.z = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m02_m12_m22_m32);
        r6.w = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m03_m13_m23_m33);
        r9.xyz = r10.xyz / r6.www;
        r7.xy = cmp(r9.xy < g_split_uv_texture_bounds[r2.w].xy);
        r6.w = (int)r7.y | (int)r7.x;
        r7.xy = cmp(g_split_uv_texture_bounds[r2.w].zw < r9.xy);
        r7.x = (int)r7.y | (int)r7.x;
        r6.w = (int)r6.w | (int)r7.x;
        if (r6.w == 0) {
          r7.xy = r9.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
          r9.xy = floor(r7.xy);
          r10.xy = r9.xy / g_vHardShadowBufferSize.xy;
          r10.xyzw = g_tShadowBuffer_SM50.GatherCmp(sShadowBuffer_SM50_s, r10.xy, r9.z).xyzw;
          r7.xy = -r9.xy + r7.xy;
          r9.xy = r10.zy + -r10.wx;
          r9.xy = r7.xx * r9.xy + r10.wx;
          r6.w = r9.y + -r9.x;
          r6.w = r7.y * r6.w + r9.x;
        } else {
          r6.w = 1;
        }
      } else {
        r6.w = 1;
      }
      r1.y = cmp(r5.w == r1.y);
      r1.y = cmp((int)r1.y == -1);
      if (r1.y != 0) {
        r1.y = g_split_distances[r2.w+1] + -g_split_distances[r2.w];
        r7.x = g_fFadeRange.y * r1.y;
        r1.y = -r1.y * g_fFadeRange.y + g_split_distances[r2.w+1];
        r1.x = r1.x + -r1.y;
        r1.x = saturate(r1.x / r7.x);
        r1.y = 1 + -r6.w;
        r6.w = r1.x * r1.y + r6.w;
      }
      r6.w = saturate(r6.w);
      r0.z = r6.w + r0.z;
      r7.xyz = -r7.zzz * r6.xyz + v4.xyz;
      r7.xyz = view._m00_m10_m20 * float3(0.089774698,0.089774698,0.089774698) + r7.xyz;
      r9.xyz = view._m01_m11_m21 * float3(0.00636635954,0.00636635954,0.00636635954) + r7.xyz;
      r9.w = 1;
      r1.x = dot(r9.xyzw, view_projection._m03_m13_m23_m33);
      r10.xyzw = cmp(r1.xxxx >= r8.xyzw);
      r10.xyzw = r10.xyzw ? float4(1,1,1,1) : 0;
      r1.y = dot(r10.xyzw, r10.xyzw);
      r2.w = (int)r1.y;
      r6.w = cmp(r1.y < g_iSplitCount);
      if (r6.w != 0) {
        r9.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r9.xyz;
        r6.w = (uint)r2.w << 2;
        r9.w = 1;
        r7.x = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m00_m10_m20_m30);
        r7.y = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m01_m11_m21_m31);
        r7.z = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m02_m12_m22_m32);
        r6.w = dot(r9.xyzw, g_amHardSplit[r6.w/4]._m03_m13_m23_m33);
        r7.xyz = r7.xyz / r6.www;
        r9.xy = cmp(r7.xy < g_split_uv_texture_bounds[r2.w].xy);
        r6.w = (int)r9.y | (int)r9.x;
        r9.xy = cmp(g_split_uv_texture_bounds[r2.w].zw < r7.xy);
        r9.x = (int)r9.y | (int)r9.x;
        r6.w = (int)r6.w | (int)r9.x;
        if (r6.w == 0) {
          r7.xy = r7.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
          r9.xy = floor(r7.xy);
          r9.zw = r9.xy / g_vHardShadowBufferSize.xy;
          r10.xyzw = g_tShadowBuffer_SM50.GatherCmp(sShadowBuffer_SM50_s, r9.zw, r7.z).xyzw;
          r7.xy = -r9.xy + r7.xy;
          r9.xy = r10.zy + -r10.wx;
          r7.xz = r7.xx * r9.xy + r10.wx;
          r6.w = r7.z + -r7.x;
          r6.w = r7.y * r6.w + r7.x;
        } else {
          r6.w = 1;
        }
      } else {
        r6.w = 1;
      }
      r1.y = cmp(r5.w == r1.y);
      r1.y = cmp((int)r1.y == -1);
      if (r1.y != 0) {
        r1.y = g_split_distances[r2.w+1] + -g_split_distances[r2.w];
        r7.x = g_fFadeRange.y * r1.y;
        r1.y = -r1.y * g_fFadeRange.y + g_split_distances[r2.w+1];
        r1.x = r1.x + -r1.y;
        r1.x = saturate(r1.x / r7.x);
        r1.y = 1 + -r6.w;
        r6.w = r1.x * r1.y + r6.w;
      }
      r6.w = saturate(r6.w);
      r0.z = r6.w + r0.z;
      r7.xyz = -r7.www * r6.xyz + v4.xyz;
      r7.xyz = view._m00_m10_m20 * float3(0.109115697,0.109115697,0.109115697) + r7.xyz;
      r7.xyz = view._m01_m11_m21 * float3(-0.049937699,-0.049937699,-0.049937699) + r7.xyz;
      r7.w = 1;
      r1.x = dot(r7.xyzw, view_projection._m03_m13_m23_m33);
      r9.xyzw = cmp(r1.xxxx >= r8.xyzw);
      r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
      r1.y = dot(r9.xyzw, r9.xyzw);
      r2.w = (int)r1.y;
      r6.w = cmp(r1.y < g_iSplitCount);
      if (r6.w != 0) {
        r7.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r7.xyz;
        r6.w = (uint)r2.w << 2;
        r7.w = 1;
        r9.x = dot(r7.xyzw, g_amHardSplit[r6.w/4]._m00_m10_m20_m30);
        r9.y = dot(r7.xyzw, g_amHardSplit[r6.w/4]._m01_m11_m21_m31);
        r9.z = dot(r7.xyzw, g_amHardSplit[r6.w/4]._m02_m12_m22_m32);
        r6.w = dot(r7.xyzw, g_amHardSplit[r6.w/4]._m03_m13_m23_m33);
        r7.xyz = r9.xyz / r6.www;
        r9.xy = cmp(r7.xy < g_split_uv_texture_bounds[r2.w].xy);
        r6.w = (int)r9.y | (int)r9.x;
        r9.xy = cmp(g_split_uv_texture_bounds[r2.w].zw < r7.xy);
        r7.w = (int)r9.y | (int)r9.x;
        r6.w = (int)r6.w | (int)r7.w;
        if (r6.w == 0) {
          r7.xy = r7.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
          r9.xy = floor(r7.xy);
          r9.zw = r9.xy / g_vHardShadowBufferSize.xy;
          r10.xyzw = g_tShadowBuffer_SM50.GatherCmp(sShadowBuffer_SM50_s, r9.zw, r7.z).xyzw;
          r7.xy = -r9.xy + r7.xy;
          r7.zw = r10.zy + -r10.wx;
          r7.xz = r7.xx * r7.zw + r10.wx;
          r6.w = r7.z + -r7.x;
          r6.w = r7.y * r6.w + r7.x;
        } else {
          r6.w = 1;
        }
      } else {
        r6.w = 1;
      }
      r1.y = cmp(r5.w == r1.y);
      r1.y = cmp((int)r1.y == -1);
      if (r1.y != 0) {
        r1.y = g_split_distances[r2.w+1] + -g_split_distances[r2.w];
        r7.x = g_fFadeRange.y * r1.y;
        r1.y = -r1.y * g_fFadeRange.y + g_split_distances[r2.w+1];
        r1.x = r1.x + -r1.y;
        r1.x = saturate(r1.x / r7.x);
        r1.y = 1 + -r6.w;
        r6.w = r1.x * r1.y + r6.w;
      }
      r6.w = saturate(r6.w);
      r0.z = r6.w + r0.z;
      r0.y = 0.833333373 * r0.y;
      r6.xyz = -r0.yyy * r6.xyz + v4.xyz;
      r6.xyz = view._m00_m10_m20 * float3(0.0897708014,0.0897708014,0.0897708014) + r6.xyz;
      r6.xyz = view._m01_m11_m21 * float3(-0.120171599,-0.120171599,-0.120171599) + r6.xyz;
      r6.w = 1;
      r0.y = dot(r6.xyzw, view_projection._m03_m13_m23_m33);
      r7.xyzw = cmp(r0.yyyy >= r8.xyzw);
      r7.xyzw = r7.xyzw ? float4(1,1,1,1) : 0;
      r1.x = dot(r7.xyzw, r7.xyzw);
      r1.y = (int)r1.x;
      r2.w = cmp(r1.x < g_iSplitCount);
      if (r2.w != 0) {
        r6.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r6.xyz;
        r2.w = (uint)r1.y << 2;
        r6.w = 1;
        r7.x = dot(r6.xyzw, g_amHardSplit[r2.w/4]._m00_m10_m20_m30);
        r7.y = dot(r6.xyzw, g_amHardSplit[r2.w/4]._m01_m11_m21_m31);
        r7.z = dot(r6.xyzw, g_amHardSplit[r2.w/4]._m02_m12_m22_m32);
        r2.w = dot(r6.xyzw, g_amHardSplit[r2.w/4]._m03_m13_m23_m33);
        r6.xyz = r7.xyz / r2.www;
        r7.xy = cmp(r6.xy < g_split_uv_texture_bounds[r1.y].xy);
        r2.w = (int)r7.y | (int)r7.x;
        r7.xy = cmp(g_split_uv_texture_bounds[r1.y].zw < r6.xy);
        r6.w = (int)r7.y | (int)r7.x;
        r2.w = (int)r2.w | (int)r6.w;
        if (r2.w == 0) {
          r6.xy = r6.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
          r7.xy = floor(r6.xy);
          r7.zw = r7.xy / g_vHardShadowBufferSize.xy;
          r8.xyzw = g_tShadowBuffer_SM50.GatherCmp(sShadowBuffer_SM50_s, r7.zw, r6.z).xyzw;
          r6.xy = -r7.xy + r6.xy;
          r6.zw = r8.zy + -r8.wx;
          r6.xz = r6.xx * r6.zw + r8.wx;
          r2.w = r6.z + -r6.x;
          r2.w = r6.y * r2.w + r6.x;
        } else {
          r2.w = 1;
        }
      } else {
        r2.w = 1;
      }
      r1.x = cmp(r5.w == r1.x);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r1.y+1] + -g_split_distances[r1.y];
        r5.w = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r1.y+1];
        r0.y = -r1.x + r0.y;
        r0.y = saturate(r0.y / r5.w);
        r1.x = 1 + -r2.w;
        r2.w = r0.y * r1.x + r2.w;
      }
      r2.w = saturate(r2.w);
      r0.y = r2.w + r0.z;
      r0.y = ps_receive_shadows_high_quality_params[r0.x].x * r0.y;
      r0.y = 0.166666672 * r0.y;
    } else {
      r0.y = 1;
    }
    if (VfxShadowStrength != 1.0f) {
      // 0 disables particle shadows; 1 preserves stock shadow visibility.
      r0.y = lerp(1.0f, r0.y, VfxShadowStrength);
    }
    if (r0.w != 0) {
      r0.z = (uint)ps_lighting_legacy_params[r0.x].x;
      r0.w = cmp(r4.w < 0.200000003);
      r1.x = cmp(0 < g_hdr_on);
      r6.xyz = r1.xxx ? float3(360,180,0.00400000019) : float3(1,0.5,1);
      r7.xyz = sun_colour.xyz * float3(0.25,0.25,0.25) + r6.yyy;
      r8.xyz = r7.xyz * r6.zzz;
      r1.x = saturate(ps_lighting_legacy_params[r0.x].z);
      r7.xyz = -r7.xyz * r6.zzz + ps_lighting_legacy_params[r0.x].zzz;
      r7.xyz = r1.xxx * r7.xyz + r8.xyz;
      r8.xyz = ps_lighting_params[r0.x].yyy * r2.xyz;
      r7.xyz = r7.xyz * r3.xyz + r8.xyz;
      r4.xyz = r0.www ? r7.xyz : r3.xyz;
      r6.yw = v6.zw * -r5.yy;
      r6.yw = r5.xx * v6.xy + r6.yw;
      r7.xyz = g_camera_aligned_y_axis.xyz * r6.www;
      r7.xyz = -r6.yyy * g_camera_aligned_x_axis.xyz + -r7.xyz;
      r7.xyz = -r5.zzz * g_camera_aligned_z_axis.xyz + r7.xyz;
      if (r0.z != 0) {
        r0.z = (uint)ps_lighting_legacy_params[r0.x].y;
        r0.w = dot(-sun_direction.xyz, -sun_direction.xyz);
        r0.w = rsqrt(r0.w);
        r9.xyz = -sun_direction.xyz * r0.www;
        r0.w = dot(r9.xyz, r7.xyz);
        r0.w = max(0, r0.w);
        r0.w = -1 + r0.w;
        r0.w = ps_lighting_params[r0.x].y * r0.w;
        r0.w = v5.w * r0.w + 1;
        r9.xyz = ps_lighting_params[r0.x].yyy * r3.xyz;
        r10.xyz = r9.xyz * r0.www;
        r10.xyz = sun_colour.xyz * r10.xyz;
        r10.xyz = r10.xyz * r6.zzz;
        r11.xyz = ambient_cube_tb[0].xyz * r6.xxx;
        r9.xyz = r11.xyz * r9.xyz;
        r9.xyz = r9.xyz * r6.zzz;
        r9.xyz = r0.yyy * r10.xyz + r9.xyz;
        r3.xyz = v5.zzz * r9.xyz;
        r0.z = (uint)r0.z;
        r0.w = r4.w * v5.w + -r3.w;
        r3.w = r0.z * r0.w + r4.w;
      } else {
        r9.xyz = float3(1,4,1) * r7.xyz;
        r0.z = dot(r9.xyz, r9.xyz);
        r0.z = rsqrt(r0.z);
        r9.xyz = r9.xyz * r0.zzz;
        r10.xyz = cmp(r9.xyz < float3(0,0,0));
        r11.xyz = r10.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
        r10.xyw = r10.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
        r12.xyz = r10.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
        r9.xyz = r9.xyz * r9.xyz;
        r10.xyz = r9.yyy * r10.xyw;
        r9.xyw = r9.xxx * r11.xyz + r10.xyz;
        r9.xyz = r9.zzz * r12.xyz + r9.xyw;
        r6.xyw = r9.xyz * r6.xxx;
        r0.z = dot(-sun_direction.xyz, -sun_direction.xyz);
        r0.z = rsqrt(r0.z);
        r9.xyz = -sun_direction.xyz * r0.zzz;
        r0.z = dot(view._m02_m12_m22, view._m02_m12_m22);
        r0.z = rsqrt(r0.z);
        r10.xyz = view._m02_m12_m22 * r0.zzz;
        r0.z = dot(-r9.xyz, r10.xyz);
        r0.w = ps_lighting_params[r0.x].z + ps_lighting_params[r0.x].w;
        r1.y = dot(r9.xyz, r7.xyz);
        r1.y = r1.y * -0.5 + 0.5;
        r1.y = r1.y * -r0.w;
        r1.y = ps_lighting_params[r0.x].x * r1.y;
        r1.y = v5.x * r1.y;
        r1.y = 1.44269502 * r1.y;
        r1.y = exp2(r1.y);
        r2.w = -ps_lighting_legacy_params[r0.x].w + 1;
        r2.w = r2.w * r2.w;
        r2.w = 0.0795774683 * r2.w;
        r5.w = ps_lighting_legacy_params[r0.x].w * ps_lighting_legacy_params[r0.x].w + 1;
        r7.x = dot(r0.zz, ps_lighting_legacy_params[r0.x].ww);
        r5.w = -r7.x + r5.w;
        r5.w = log2(r5.w);
        r5.w = 1.5 * r5.w;
        r5.w = exp2(r5.w);
        r2.w = r2.w / r5.w;
        r2.w = ps_lighting_params[r0.x].w * r2.w;
        r0.z = r0.z * r0.z + 1;
        r0.z = 0.0596831031 * r0.z;
        r0.z = r0.z * ps_lighting_params[r0.x].z + r2.w;
        r0.z = r0.z / r0.w;
        r0.w = 1 + -r1.y;
        r0.z = r0.z * r0.w + r1.y;
        r7.xyz = sun_colour.xyz * r0.zzz;
        r0.yzw = r7.xyz * r0.yyy + r6.xyw;
        r6.xyw = r0.yzw * r6.zzz;
        r0.yzw = -r0.yzw * r6.zzz + ps_lighting_legacy_params[r0.x].zzz;
        r0.yzw = r1.xxx * r0.yzw + r6.xyw;
        r3.xyz = r0.yzw * r3.xyz + r8.xyz;
        r3.w = r4.w;
      }
    } else {
      r4.xyz = r3.xyz;
    }
    r0.y = -0.100000001 + r4.w;
    r0.y = 10 * r0.y;
    r0.y = min(1, r0.y);
    r6.xyzw = -r4.xyzw + r3.xyzw;
    r4.xyzw = r0.yyyy * r6.xyzw + r4.xyzw;
  } else {
    r0.y = 0x00200000 & (int)behaviour_mask[r0.x].x;
    r0.z = cmp(0 < g_hdr_on);
    r0.zw = r0.zz ? float2(180,0.00400000019) : float2(0.5,1);
    r6.xyz = sun_colour.xyz * float3(0.25,0.25,0.25) + r0.zzz;
    r7.xyz = r6.xyz * r0.www;
    r0.z = saturate(ps_lighting_legacy_params[r0.x].z);
    r6.xyz = -r6.xyz * r0.www + ps_lighting_legacy_params[r0.x].zzz;
    r6.xyz = r0.zzz * r6.xyz + r7.xyz;
    r2.xyz = ps_lighting_params[r0.x].yyy * r2.xyz;
    r2.xyz = r6.xyz * r3.xyz + r2.xyz;
    r4.xyz = r0.yyy ? r2.xyz : r3.xyz;
    r5.xyz = float3(0,0,1);
  }
  if (VfxLightingStrength != 1.0f
      && (behaviour_mask[v2.x].x & 0x00200000u) != 0u) {
    // 0 gives the controlled unlit colour; 1 preserves stock particle lighting.
    r4.xyz = lerp(vfxUnlitColour, r4.xyz, VfxLightingStrength);
  }
  if (r1.z != 0) {
    r0.yz = -ps_specular_params1[r0.x].xy + float2(1,1);
    r1.xyz = -camera_position.xyz + v4.xyz;
    r0.w = dot(r1.xyz, r1.xyz);
    r0.w = rsqrt(r0.w);
    r1.xyz = r1.xyz * r0.www;
    r0.w = dot(-r1.xyz, r5.xyz);
    r0.w = r0.w + r0.w;
    r2.xyz = r5.xyz * -r0.www + -r1.xyz;
    r0.w = dot(-sun_direction.xyz, r5.xyz);
    r2.w = cmp(0 < r0.w);
    if (r2.w != 0) {
      r2.w = dot(-sun_direction.xyz, r2.xyz);
      r2.w = max(-1, r2.w);
      r2.w = min(1, r2.w);
      r3.x = 1 + -abs(r2.w);
      r3.x = sqrt(r3.x);
      r3.y = abs(r2.w) * -0.0187292993 + 0.0742610022;
      r3.y = r3.y * abs(r2.w) + -0.212114394;
      r3.y = r3.y * abs(r2.w) + 1.57072878;
      r3.z = r3.y * r3.x;
      r3.z = r3.z * -2 + 3.14159274;
      r2.w = cmp(r2.w < -r2.w);
      r2.w = r2.w ? r3.z : 0;
      r2.w = r3.y * r3.x + r2.w;
      r3.x = cmp(0 < g_hdr_on);
      r3.y = ps_specular_params1[r0.x].y * ps_specular_params1[r0.x].y;
      r3.xz = r3.xx ? float2(0.00400000019,0.995899975) : float2(1,-0.000100016594);
      r3.x = r3.y * r3.z + r3.x;
      r3.x = r3.x * 0.5 + 0.5;
      r3.x = r3.x * 2 + -1;
      r3.y = -r3.x * r3.x + 1;
      r3.y = max(0.00100000005, r3.y);
      r3.y = log2(r3.y);
      r3.z = 4.95061684 * r3.y;
      r3.y = r3.y * 0.346573591 + 4.54688501;
      r3.w = cmp(0 < r3.x);
      r3.x = cmp(r3.x < 0);
      r3.x = (int)-r3.w + (int)r3.x;
      r3.x = (int)r3.x;
      r3.z = r3.y * r3.y + -r3.z;
      r3.z = sqrt(r3.z);
      r3.y = r3.z + -r3.y;
      r3.y = max(0, r3.y);
      r3.y = sqrt(r3.y);
      r3.x = r3.x * r3.y;
      r3.x = 1.41421354 * r3.x;
      r3.x = 0.00872664619 / r3.x;
      r3.x = max(9.99999975e-005, r3.x);
      r3.yz = float2(-0.00872664619,0.00872664619) + r2.ww;
      r2.w = 1 / r3.x;
      r3.xy = r3.yz * r2.ww;
      r3.zw = float2(0.707106769,0.707106769) * r3.xy;
      r3.zw = r3.zw * r3.zw;
      r5.xyzw = r3.zzww * float4(0.140012279,0.140012279,0.140012279,0.140012279) + float4(1.27323949,1,1.27323949,1);
      r5.xy = r5.xz / r5.yw;
      r3.zw = r5.xy * -r3.zw;
      r5.xy = cmp(float2(0,0) < r3.xy);
      r2.w = cmp(r3.x < 0);
      r2.w = (int)-r5.x + (int)r2.w;
      r2.w = (int)r2.w;
      r3.xy = float2(1.44269502,1.44269502) * r3.zw;
      r3.xy = exp2(r3.xy);
      r3.xy = float2(1,1) + -r3.xy;
      r3.xy = sqrt(r3.xy);
      r2.w = r2.w * r3.x + 1;
      r2.w = 0.5 * r2.w;
      r3.x = (int)-r5.y;
      r3.x = r3.x * r3.y + 1;
      r2.w = r3.x * 0.5 + -r2.w;
      r0.w = saturate(r0.w);
      r3.xy = float2(1.57079637,1.53938043) * r0.zz;
      r3.x = sin(r3.x);
      r0.w = -1 + r0.w;
      r0.w = r3.x * r0.w + 1;
      r3.x = dot(-sun_direction.xyz, -sun_direction.xyz);
      r3.x = log2(r3.x);
      r3.x = 10 * r3.x;
      r3.x = exp2(r3.x);
      r3.y = cos(r3.y);
      r3.y = rsqrt(r3.y);
      r3.y = 1 / r3.y;
      r3.z = saturate(ps_specular_params1[r0.x].x * 60);
      r3.x = r3.x * r3.y;
      r3.y = -ps_specular_params1[r0.x].x + r3.z;
      r3.x = r3.x * r3.y + ps_specular_params1[r0.x].x;
      r0.w = abs(r2.w) * r0.w;
      r0.w = r0.w * r3.x;
    } else {
      r0.w = 0;
    }
    r3.xyz = ps_specular_params[r0.x].xyz * r0.www;
    r3.xyz = sun_colour.xyz * r3.xyz;
    r3.xyz = ps_specular_params1[r0.x].zzz * r3.xyz;
    r3.xyz = r3.xyz * r4.www;
    r3.xyz *= VfxSpecularBrightness;
    r3.xyz = r4.xyz * r0.yyy + r3.xyz;
    r5.xyz = s_environment.SampleLevel(s_environment_s, r2.xyz, r0.z).xyz;
    r0.y = dot(r2.xyz, -r1.xyz);
    r0.y = max(0, r0.y);
    r0.y = log2(r0.y);
    r0.yz = float2(10,1.53938043) * r0.yz;
    r0.y = exp2(r0.y);
    r0.z = cos(r0.z);
    r0.z = rsqrt(r0.z);
    r0.z = 1 / r0.z;
    r0.w = saturate(ps_specular_params1[r0.x].x * 60);
    r0.y = r0.y * r0.z;
    r0.z = -ps_specular_params1[r0.x].x + r0.w;
    r0.y = r0.y * r0.z + ps_specular_params1[r0.x].x;
    r0.y = max(ps_specular_params1[r0.x].x, r0.y);
    r0.yzw = r5.xyz * r0.yyy;
    r0.yzw = ps_specular_params[r0.x].xyz * r0.yzw;
    r0.yzw *= VfxReflectionBrightness;
    r4.xyz = r0.yzw * r4.www + r3.xyz;
  }
  if (r1.w != 0) {
    r1.xyz = v4.xyz;
    r1.w = 1;
    r0.y = dot(r1.xyzw, view._m02_m12_m22_m32);
    r0.zw = g_vpos_texel_offset + v0.xy;
    r0.zw = g_render_target_dimensions.zw * r0.zw;
    r1.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.zw, 0).x;
    r1.xy = r0.zw * float2(2,-2) + float2(-1,1);
    r1.w = 1;
    r0.z = dot(r1.xyzw, inv_projection._m02_m12_m22_m32);
    r0.w = dot(r1.xyzw, inv_projection._m03_m13_m23_m33);
    r0.z = r0.z / r0.w;
    r0.y = r0.z + -r0.y;
    r0.y = saturate(r0.y / ps_soft_edge_params[r0.x].x);
    if (VfxSoftParticleStrength != 1.0f) {
      // 0 disables intersection fading; 1 preserves the stock soft edge.
      r0.y = lerp(1.0f, r0.y, VfxSoftParticleStrength);
    }
    r4.xyzw = r4.xyzw * r0.yyyy;
  }

  // Preserve premultiplied-alpha behavior by applying opacity to both RGB and A.
  // Keep the opacity slider in [0, 1]. Overall brightness affects RGB only.
  r4.xyzw *= VfxOpacity;
  r4.xyz *= VfxOverallBrightness;

  r0.x = 0x01000000 & (int)behaviour_mask[r0.x].x;
  r1.xyz = -camera_position.xyz + v4.xyz;
  r1.w = max(0, r1.y);
  r0.yzw = s_sky.Sample(s_sky_s, r1.xwz).xyz;
  if (r0.x != 0) {
    r0.x = dot(r1.xyz, r1.xyz);
    r0.x = sqrt(r0.x);
    r1.y = saturate(g_fog_distance_start);
    r1.y = 1 + -r1.y;
    r1.y = r1.y * 8 + -4;
    r1.w = saturate(g_fog_distance_strength);
    r1.w = 1 + -r1.w;
    r1.w = 1000 * r1.w;
    r0.x = r0.x / r1.w;
    r0.x = r1.y + r0.x;
    r0.x = 1.44269502 * r0.x;
    r0.x = exp2(r0.x);
    r0.x = g_fog_distance_scale / r0.x;
    r0.x = saturate(g_fog_distance_scale + -r0.x);
    r1.y = g_fog_height_top + -v4.y;
    r1.w = g_fog_height_top + -g_fog_height_bottom;
    r1.y = -g_fog_height_bottom + r1.y;
    r1.w = 1 / r1.w;
    r1.y = saturate(r1.y * r1.w);
    r1.w = r1.y * -2 + 3;
    r1.y = r1.y * r1.y;
    r1.x = dot(r1.xz, r1.xz);
    r1.x = sqrt(r1.x);
    r1.z = max(0.00100000005, g_fog_clear_distance);
    r1.z = 1 / r1.z;
    r1.xy = r1.xw * r1.zy;
    r1.x = min(1, r1.x);
    r1.z = r1.x * -2 + 3;
    r1.x = r1.x * r1.x;
    r1.x = r1.z * r1.x;
    r0.x = g_fog_height_strength * r1.y + r0.x;
    r1.yzw = g_volume_fog_colour.xyz * sun_colour.xyz;
    r1.yzw = float3(1.5,1.5,1.5) * r1.yzw;
    r1.yzw = abs(sun_direction.y) * r1.yzw;
    r2.x = cmp(0 < g_hdr_on);
    r2.x = r2.x ? 0.00400000019 : 1;
    r2.yzw = r2.xxx * r1.yzw;
    r3.x = saturate(dot(r0.xx, g_fog_colour_blend));
    r0.yzw = -r1.yzw * r2.xxx + r0.yzw;
    r0.yzw = r3.xxx * r0.yzw + r2.yzw;
    r0.x = saturate(r1.x * r0.x);
    r0.x = saturate(r0.x * VfxFogAmount);
    r0.yzw *= VfxFogBrightness;
    r0.yzw = r0.yzw * r4.www + -r4.xyz;
    r4.xyz = r0.xxx * r0.yzw + r4.xyz;
  }
  // Final VFX output. With every injected control set to 1.0f this is stock.
  o0.xyzw = r4.xyzw;
  return;
}
