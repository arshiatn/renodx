// TROY godrays03 - radial accumulation toward the sun (final god-rays pass).
//
// Chain: 00 builds a sky mask from depth (x0.5), 01 and 02 are a separable 5-tap
// Gaussian on it, and 03 marches 8 samples toward sun_position with a radial falloff.
// Low/Medium skip the blurs and load only 00 and 03.
//
// NO MULTIPLIER HERE, DELIBERATELY. This chain emits a bounded [0, 0.5] scalar MASK,
// not colour, so there is nothing to rescale for HDR. The rays only become light where
// they are composited - sun_colour_unscaled * ray * god_rays_strength - in
// t01/t02/t03/t06, or in dof00 when DOF is on. SI.godrays is already applied at both,
// so adding it here would square it.
//
// The one real fix is the rsqrt guard below.

cbuffer hdr_god_rays_PS : register(b0)
{
  float3 sun_position : packoffset(c0);
}

SamplerState rays_sampler_s : register(s0);
Texture2D<float4> rays_texture : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = sun_position.xy + -v1.xy;
  r0.z = dot(r0.xy, r0.xy);

  // rsqrt(0) is +Inf and 0 * Inf is NaN. Only reachable when the pixel lands exactly
  // on the sun, and only harmful now that the target is float.
  r0.z = max(r0.z, 1e-12f);

  r0.w = sqrt(r0.z);
  r0.z = rsqrt(r0.z);
  r0.xy = r0.xy * r0.zz;
  r0.xy = r0.xy * r0.ww;
  r0.xy = float2(0.5,0.5) * r0.xy;
  r0.z = 0;
  r1.x = 0;
  while (true) {
    r1.y = (int)r1.x;
    r1.z = cmp(r1.y >= 8);
    if (r1.z != 0) break;
    r1.y = 0.142857149 * r1.y;
    r1.yz = r0.xy * r1.yy + v1.xy;
    r1.y = rays_texture.SampleLevel(rays_sampler_s, r1.yz, 0).x;
    r0.z = r1.y + r0.z;
    r1.x = (int)r1.x + 1;
  }
  r0.xy = float2(0.125,1.25) * r0.zw;
  r0.y = -r0.y * r0.y + 1;
  r0.y = max(0, r0.y);
  o0.xyzw = r0.xxxx * r0.yyyy;
  return;
}