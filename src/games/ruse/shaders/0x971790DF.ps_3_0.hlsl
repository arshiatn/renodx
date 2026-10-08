// Native procedural DOF mask; preserve RGB/coverage and scale only its output alpha.
#include "../shared.h"

float NativeDepthOfFieldIntensity : register(c0);

struct PS_IN {
  float2 texcoord : TEXCOORD0;
  float2 texcoord1 : TEXCOORD1;
  float4 color : COLOR0;
};

half4 main(PS_IN i) : COLOR0 {
  half4 o;
  float4 r0;
  float2 r1;
  r0.xy = ddx(i.texcoord.xy);
  r0.zw = ddy(i.texcoord.xy);
  r0.x = dot(r0.xz, r0.xz);
  r0.y = dot(r0.yw, r0.yw);
  r0.y = rsqrt(r0.y);
  r0.y = 1.f / r0.y;
  r1.y = max(0.0000999999975f, r0.y);
  r0.x = rsqrt(r0.x);
  r0.x = 1.f / r0.x;
  r1.x = max(0.0000999999975f, r0.x);
  r0.xy = r1.xy * 1.20000005f;
  r0.zw = r1.xx * float2(-0.600000024f, 0.600000024f) + i.texcoord.xx;
  r0.zw = saturate(r0.zw + 0.5f);
  r0.z = -r0.z + r0.w;
  r1 = r1.yy * float2(-0.600000024f, 0.600000024f) + i.texcoord.yy;
  r1 = saturate(r1 + 0.5f);
  r0.w = -r1.x + r1.y;
  r0.x = 1.f / r0.x;
  r0.y = 1.f / r0.y;
  r0.xy = saturate(r0.xy * r0.zw);
  r0.x = r0.y * r0.x;
  r0.x *= i.color.w;
  r0.x *= NativeDepthOfFieldIntensity;
  r0.x = half(r0.x * 0.349999994f);
  r0.yz = 1.f - i.texcoord1.xy;
  r0.z = (r0.z >= 0.f) ? 1.f : 0.f;
  r0.y = (r0.y >= 0.f) ? r0.z : 0.f;
  r0.y = (i.texcoord1.y >= 0.f) ? r0.y : 0.f;
  r0.y = (i.texcoord1.x >= 0.f) ? r0.y : 0.f;
  o.w = r0.y * r0.x;
  o.xyz = 0.f;
  if (SI.peak_white_nits > 0.f && SI.diffuse_white_nits > 0.f) {
    o.w *= saturate(SI.depth_of_field);
  }
  return o;
}
