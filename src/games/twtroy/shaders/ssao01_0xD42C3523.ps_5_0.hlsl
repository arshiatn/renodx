// ssao2.hlsl ps50_generate (Troy, Pharaoh: their default AO, ssao_algorithm 1): HBAO on the half-size linear depth,
// its radius / strength / limit by distance (g_RadiusAndPower1-3 over g_Distances). Ambient Occlusion: Game's (the
// 3Dmigoto decompile, unchanged: the replacement) or, with AO_BITMASK, Visibility Bitmask (Warhammer II's
// bitmask_ao.hlsli) with the game's radius, strength and limit there (the addon's own shader). Output as the game's: the AO and the depth for its blur.
// Below 100% (DLSS, FSR, XeSS at a render scale) the pass draws the top-left share of its target, and its uv (TEXCOORD0,
// the full-screen vertex shader 31C49ED2's) runs 0-1 over that share: the depth is read at uv x the share, its pixels
// (g_Resolution, the game's 100% size) are the share's (render_scale.hlsli: view_share.hpp binds it; unbound: 1).
#include "../shared.h"
#include "../../twattila/render_scale.hlsli"

cbuffer ssao2_PS : register(b0)
{
  float2 g_DepthLinearization : packoffset(c0);
  float4 g_Resolution : packoffset(c1);
  float4 g_ToViewTransform : packoffset(c2);
  float2 g_CameraFovXY : packoffset(c3);
  float2 g_AngleBias : packoffset(c3.z);
  float3 g_RadiusAndPower1 : packoffset(c4);
  float3 g_RadiusAndPower2 : packoffset(c5);
  float3 g_RadiusAndPower3 : packoffset(c6);
  float3 g_Distances : packoffset(c7);
  float g_MaxRadius : packoffset(c7.w);
  float NUM_DIRS : packoffset(c8);
  float NUM_STEPS : packoffset(c8.y);
  float BLUR_STEPS : packoffset(c8.z);
}

SamplerState s_point_clamp_sampler_s : register(s0);
SamplerState s_point_wrap_sampler_s : register(s1);
Texture2D<float> ssao2_linear_depth_texture : register(t0);
Texture2D<float3> ssao2_random_texture : register(t1);

#define cmp -

// The linear depth at the view's uv (the depth texture's top-left share below 100%), kept on the share's texels as the
// point clamp sampler keeps it on the texture's at 100% (g_Resolution.zw: the texture's texel).
float AoDepth(float2 uv)
{
  const float2 share = FrameRenderShare();
  const float2 edge = 0.5f * g_Resolution.zw;
  return ssao2_linear_depth_texture.SampleLevel(s_point_clamp_sampler_s, clamp(uv * share, edge, share - edge), 0).x;
}

float2 GameAmbientOcclusion(float4 v0, float2 v1)
{
  float2 o0;
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  // The game's pixel size over the view's share (its 100% size below 100%).
  const float4 resolution = float4(g_Resolution.xy * FrameRenderShare(), g_Resolution.zw / FrameRenderShare());

  r0.w = AoDepth(v1.xy);
  r1.x = saturate(g_Distances.y * r0.w);
  r1.y = -g_Distances.x + r0.w;
  r1.y = saturate(g_Distances.z * r1.y);
  r2.xyz = g_RadiusAndPower2.xyz + -g_RadiusAndPower1.xyz;
  r1.xzw = r1.xxx * r2.xyz + g_RadiusAndPower1.xyz;
  r2.xyz = g_RadiusAndPower3.xyz + -r1.xzw;
  r1.xyz = r1.yyy * r2.xyz + r1.xzw;
  r1.w = g_CameraFovXY.x * r1.x;
  r1.w = 0.5 * r1.w;
  r1.w = r1.w / r0.w;
  r1.w = resolution.x * r1.w;
  r2.x = cmp(r1.w < 1);
  if (r2.x != 0) {
    return float2(1,1);
  }
  r2.xy = g_ToViewTransform.xy * v1.xy + g_ToViewTransform.zw;
  r0.yz = r2.xy * r0.ww;
  r2.xy = float2(0.25,0.25) * v0.xy;
  r2.xyz = ssao2_random_texture.Sample(s_point_wrap_sampler_s, r2.xy).xyz;
  r2.xy = r2.yx * float2(2,2) + float2(-1,-1);
  r1.x = r1.x * r1.x;
  r2.w = -1 / r1.x;
  r3.xy = float2(6.28318501,2) / NUM_DIRS;
  r4.x = min(NUM_STEPS, r1.w);
  r3.z = 1 + r4.x;
  r4.y = r1.w / r3.z;
  r1.w = g_MaxRadius / r4.y;
  r3.z = cmp(r1.w < r4.x);
  r1.w = r1.w + r2.z;
  r1.w = floor(r1.w);
  r5.x = max(1, r1.w);
  r5.y = g_MaxRadius / r5.x;
  r3.zw = r3.zz ? r5.xy : r4.xy;
  r4.xy = resolution.zw * r3.ww;
  r1.w = 0.5 * NUM_DIRS;
  r3.w = g_AngleBias.y * g_AngleBias.y + 1;
  r3.w = rsqrt(r3.w);
  r3.w = g_AngleBias.y * r3.w;
  r3.z = -1 + r3.z;
  r4.zw = float2(0,0);
  while (true) {
    r5.x = cmp(r4.w >= r1.w);
    if (r5.x != 0) break;
    r5.x = r4.w * r3.x;
    sincos(r5.x, r5.x, r6.x);
    r5.xy = r5.xx * r2.xy;
    r7.x = r6.x * r2.y + -r5.x;
    r7.y = r6.x * r2.x + r5.y;
    r5.xy = r7.xy * r4.xy;
    r5.zw = r5.xy * r2.zz;
    r6.xy = r7.xy * resolution.zw + r5.zw;
    r6.xy = resolution.xy * r6.xy;
    r6.xy = round(r6.xy);
    r6.zw = r6.xy * resolution.zw + v1.xy;
    r7.z = AoDepth(r6.zw);
    r6.zw = g_ToViewTransform.xy * r6.zw + g_ToViewTransform.zw;
    r7.xy = r6.zw * r7.zz;
    r7.w = -r7.z + r0.w;
    r6.zw = r6.zw * r7.zz + -r0.yz;
    r6.z = dot(r6.zw, r6.zw);
    r6.z = rsqrt(r6.z);
    r6.z = r7.w * r6.z;
    r7.xyz = r7.xyz + -r0.yzw;
    r6.w = dot(r7.xyz, r7.xyz);
    r7.x = cmp(r6.w < r1.x);
    r7.y = cmp(g_AngleBias.y < r6.z);
    r7.x = r7.y ? r7.x : 0;
    if (r7.x != 0) {
      r7.x = r6.z * r6.z + 1;
      r7.x = rsqrt(r7.x);
      r6.w = r6.w * r2.w + 1;
      r6.z = r6.z * r7.x + -r3.w;
      r6.z = r6.w * r6.z;
    } else {
      r6.z = 0;
    }
    r6.xy = -r6.xy * resolution.zw + v1.xy;
    r7.z = AoDepth(r6.xy);
    r6.xy = g_ToViewTransform.xy * r6.xy + g_ToViewTransform.zw;
    r7.xy = r6.xy * r7.zz;
    r6.w = -r7.z + r0.w;
    r6.xy = r6.xy * r7.zz + -r0.yz;
    r6.x = dot(r6.xy, r6.xy);
    r6.x = rsqrt(r6.x);
    r6.x = r6.w * r6.x;
    r7.xyz = r7.xyz + -r0.yzw;
    r6.y = dot(r7.xyz, r7.xyz);
    r6.w = cmp(r6.y < r1.x);
    r7.x = cmp(g_AngleBias.y < r6.x);
    r6.w = r6.w ? r7.x : 0;
    if (r6.w != 0) {
      r6.w = r6.x * r6.x + 1;
      r6.w = rsqrt(r6.w);
      r6.y = r6.y * r2.w + 1;
      r6.x = r6.x * r6.w + -r3.w;
      r6.z = r6.y * r6.x + r6.z;
    }
    r6.x = r6.z * 0.5 + -1;
    r6.x = max(0, r6.x);
    r5.xyzw = resolution.xyxy * r5.xyzw;
    r5.xyzw = round(r5.xyzw);
    r5.zw = r5.zw * resolution.zw + v1.xy;
    r6.yz = resolution.zw * r5.xy;
    r7.xy = r5.zw;
    r6.w = 0;
    r7.z = g_AngleBias.y;
    r7.w = r3.w;
    r8.x = 1;
    while (true) {
      r8.y = cmp(r3.z < r8.x);
      if (r8.y != 0) break;
      r7.xy = r5.xy * resolution.zw + r7.xy;
      r8.w = AoDepth(r7.xy);
      r9.xy = g_ToViewTransform.xy * r7.xy + g_ToViewTransform.zw;
      r8.yz = r9.xy * r8.ww;
      r9.z = -r8.w + r0.w;
      r9.xy = r9.xy * r8.ww + -r0.yz;
      r9.x = dot(r9.xy, r9.xy);
      r9.x = rsqrt(r9.x);
      r9.x = r9.z * r9.x;
      r8.yzw = r8.yzw + -r0.yzw;
      r8.y = dot(r8.yzw, r8.yzw);
      r8.z = cmp(r8.y < r1.x);
      r8.w = cmp(r7.z < r9.x);
      r8.z = r8.w ? r8.z : 0;
      if (r8.z != 0) {
        r8.z = r9.x * r9.x + 1;
        r8.z = rsqrt(r8.z);
        r8.w = r9.x * r8.z;
        r8.y = r8.y * r2.w + 1;
        r8.z = r9.x * r8.z + -r7.w;
        r6.w = r8.y * r8.z + r6.w;
        r7.z = r9.x;
        r7.w = r8.w;
      }
      r8.x = 1 + r8.x;
    }
    r5.zw = -r6.yz * r2.zz;
    r5.zw = resolution.xy * r5.zw;
    r5.zw = round(r5.zw);
    r5.zw = r5.zw * resolution.zw + v1.xy;
    r6.yz = r5.zw;
    r7.x = r6.w;
    r7.y = g_AngleBias.y;
    r7.z = r3.w;
    r7.w = 1;
    while (true) {
      r8.x = cmp(r3.z < r7.w);
      if (r8.x != 0) break;
      r6.yz = -r5.xy * resolution.zw + r6.yz;
      r8.z = AoDepth(r6.yz);
      r9.xy = g_ToViewTransform.xy * r6.yz + g_ToViewTransform.zw;
      r8.xy = r9.xy * r8.zz;
      r8.w = -r8.z + r0.w;
      r9.xy = r9.xy * r8.zz + -r0.yz;
      r9.x = dot(r9.xy, r9.xy);
      r9.x = rsqrt(r9.x);
      r8.w = r9.x * r8.w;
      r8.xyz = r8.xyz + -r0.yzw;
      r8.x = dot(r8.xyz, r8.xyz);
      r8.y = cmp(r8.x < r1.x);
      r8.z = cmp(r7.y < r8.w);
      r8.y = r8.z ? r8.y : 0;
      if (r8.y != 0) {
        r8.y = r8.w * r8.w + 1;
        r8.y = rsqrt(r8.y);
        r8.z = r8.w * r8.y;
        r8.x = r8.x * r2.w + 1;
        r8.y = r8.w * r8.y + -r7.z;
        r7.x = r8.x * r8.y + r7.x;
        r7.y = r8.w;
        r7.z = r8.z;
      }
      r7.w = 1 + r7.w;
    }
    r5.x = r7.x * 0.5 + -1;
    r5.x = max(r5.x, r6.x);
    r4.z = r5.x + r4.z;
    r4.w = 1 + r4.w;
  }
  r0.y = r4.z * r3.y;
  r0.y = min(r0.y, r1.z);
  r0.x = saturate(-r0.y * r1.y + 1);
  o0.xy = r0.xw;
  return o0;
}

// The game's: view xy at depth 1 from the uv, times the linear depth.
float3 AoViewPosition(float2 uv, float depth)
{
  return float3((g_ToViewTransform.xy * uv + g_ToViewTransform.zw) * depth, depth);
}

#ifdef AO_BITMASK
// The addon's own shader (ao_bitmask.ps_5_0.hlsl, twattila/ao_pass.hpp draws it in this pass's place when the
// Visibility Bitmask is on).
#include "../../twwarhammer2/bitmask_ao.hlsli"

// Half resolution (SI.ssao_resolution): this AO pixel stands for the top-left pixel of its 2 x 2.
float2 BitmaskAmbientOcclusion(float4 v0, float2 v1)
{
  const float scale = (SI.ssao_resolution != 0.f) ? 2.f : 1.f;
  // One of the game's AO pixels in the view's uv (below 100%: of its share).
  const float2 texel = g_Resolution.zw / FrameRenderShare();
  const float2 uv = v1 - 0.5f * (scale - 1.f) * texel;
  const float depth = AoDepth(uv);
  // GT-VBAO's own radius (metres) and strength, not the game's.
  const float visibility = BitmaskVisibility(uv, uint2(v0.xy), texel, texel, SI.ssao_radius);
  return float2(exp2(log2(max(visibility, 1.0000598e-4f)) * SI.ssao_strength), depth);
}

#ifdef AO_UPSAMPLE
// Half resolution: the pass at the game's size from the half-size results at t40 (t_ao_half: their AO and depth;
// ao_upsample.ps_5_0.hlsl), with this pixel's depth for the game's blur.

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float2 o0 : SV_TARGET0)
{
  const float depth = AoDepth(v1);
  o0 = float2((depth > 0.f) ? AoUpsample(t_ao_half, int2(v0.xy), depth, 2.f) : 1.f, depth);
}
#else
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float2 o0 : SV_TARGET0)
{
  o0 = BitmaskAmbientOcclusion(v0, v1);
}
#endif
#else
// The replacement: the game's code only (Ambient Occlusion Game's costs what it did).
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float2 o0 : SV_TARGET0)
{
  o0 = GameAmbientOcclusion(v0, v1);
}
#endif
