#ifndef RUSE_DOF_BLUR_HLSLI_
#define RUSE_DOF_BLUR_HLSLI_

#include "../shared.h"

float4 InputTexelDimensions : register(c0);
float NativeDepthOfFieldIntensity : register(c1);
sampler2D InputTexture : register(s0);

// Native horizontal/vertical five-tap kernels. These passes write RGB and alpha 1.
half4 main(float2 texcoord : TEXCOORD0) : COLOR0 {
  float strength = (SI.peak_white_nits > 0.f && SI.diffuse_white_nits > 0.f)
                       ? saturate(SI.depth_of_field) : 1.f;
  [branch]
  if (strength <= 0.f) return float4(tex2D(InputTexture, texcoord).rgb, 1.f);

  half4 o;
  float4 r0;
  float4 r1;
  float4 r2;
  float4 r3;
  r0.x = 1.f;
  r0.x -= NativeDepthOfFieldIntensity;
  r0.x = r0.x * 0.800000012f + 2.f;
  r0.x = -r0.x + 1.25f;
  r0 = r0.x * InputTexelDimensions.xyxy;
  r0 *= strength;
#if RUSE_DOF_VERTICAL
  r1.xy = r0.ww * float2(2.f, 1.f) + 3.f;
  r1.xy = (-r0.ww >= 0.f) ? r1.xy : 3.f;
  r2 = r0.zwzw * float4(0.f, -2.f, 0.f, -1.f) + texcoord.xyxy;
#else
  r1.xy = r0.zz * float2(2.f, 1.f) + 3.f;
  r1.xy = (-r0.zz >= 0.f) ? r1.xy : 3.f;
  r2 = r0.zwzw * float4(-2.f, 0.f, -1.f, 0.f) + texcoord.xyxy;
#endif
  r3 = tex2D(InputTexture, r2.zw);
  r2 = tex2D(InputTexture, r2.xy);
  r3.xyz = r1.yyy * r3.xyz;
  r2.xyz = r1.xxx * r2.xyz + r3.xyz;
  r1.x = r1.y + r1.x;
  r1.x += 3.f;
  r3 = tex2D(InputTexture, texcoord);
  r1.yzw = r3.xyz * 3.f + r2.xyz;
#if RUSE_DOF_VERTICAL
  r2 = r0 * float4(0.f, 1.f, 0.f, 2.f) + texcoord.xyxy;
#else
  r2 = r0 * float4(1.f, 0.f, 2.f, 0.f) + texcoord.xyxy;
#endif
  r3 = tex2D(InputTexture, r2.xy);
  r2 = tex2D(InputTexture, r2.zw);
#if RUSE_DOF_VERTICAL
  r0.xy = r0.yw * float2(-1.f, -2.f) + 3.f;
  r0.xy = (r0.ww >= 0.f) ? r0.xy : 3.f;
#else
  r0.xy = r0.xz * float2(-1.f, -2.f) + 3.f;
  r0.xy = (r0.zz >= 0.f) ? r0.xy : 3.f;
#endif
  r1.yzw = r0.xxx * r3.xyz + r1.yzw;
  r1.yzw = r0.yyy * r2.xyz + r1.yzw;
  r0.x += r1.x;
  r0.x = r0.y + r0.x;
  r0.x = 1.f / r0.x;
  o.xyz = r0.xxx * r1.yzw;
  o.w = 1.f;
  return o;
}

#endif  // RUSE_DOF_BLUR_HLSLI_
