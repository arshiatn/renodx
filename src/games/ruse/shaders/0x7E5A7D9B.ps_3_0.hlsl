// Native coloured DOF mask; retain HDR scaling and scale only its output alpha.
#include "../shared.h"

float2 NativeHDRParam : register(c0);
float NativeDepthOfFieldIntensity : register(c1);
float2 SubLocalPassIndexAndCount : register(c2);

struct PS_IN {
  float texcoord : TEXCOORD0;
  float2 texcoord1 : TEXCOORD1;
  float4 color : COLOR0;
};

half4 main(PS_IN i) : COLOR0 {
  half4 o;
  float4 r0;
  float2 r1;
  r0.zw = float2(-0.199999988f, 0.5f);
  r0.xy = SubLocalPassIndexAndCount.xx * r0.zw + r0.ww;
  r0.yzw = half3(r0.yyy * i.color.xyz);
  o.xyz = r0.yzw * NativeHDRParam.yyy;
  r1.x = ddx(i.texcoord);
  r1.y = ddy(i.texcoord);
  r0.y = dot(r1, r1);
  r0.y = rsqrt(r0.y);
  r0.y = 1.f / r0.y;
  r1.x = max(0.0000999999975f, r0.y);
  r0.yz = r1.xx * float2(-0.699999988f, 0.699999988f) + i.texcoord.xx;
  r0.w = r1.x * 1.39999998f;
  r1.x = r0.x + r0.x;
  r1.x = 1.f / r1.x;
  r0.yz *= r1.xx;
  r0.xy = saturate(r1.xx * r0.xx + r0.yz);
  r0.z = r0.w * r1.x;
  r0.z = 1.f / r0.z;
  r0.x = -r0.x + r0.y;
  r0.x = saturate(r0.z * r0.x);
  r0.x *= i.color.w;
  r0.x *= NativeDepthOfFieldIntensity;
  r0.x = half(r0.x * 0.699999988f);
  r0.yz = 1.f - i.texcoord1.xy;
  r0.z = (r0.z >= 0.f) ? 1.f : 0.f;
  r0.y = (r0.y >= 0.f) ? r0.z : 0.f;
  r0.y = (i.texcoord1.y >= 0.f) ? r0.y : 0.f;
  r0.y = (i.texcoord1.x >= 0.f) ? r0.y : 0.f;
  o.w = r0.y * r0.x;
  if (SI.peak_white_nits > 0.f && SI.diffuse_white_nits > 0.f) {
    o.w *= saturate(SI.depth_of_field);
  }
  return o;
}
