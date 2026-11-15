// R.U.S.E.'s vertex shader 0xEC6DF9C9 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
// tools/nontw/ruse/scripts/ru_vs_transpile.py (proved against it as vs_3_0 on random and traced constants).
#if __SHADER_TARGET_MAJOR >= 4
cbuffer GameVertexConstants : register(b0) { float4 c[256]; };
// The integer vertex formats ReShade reports come in normalized: scale / bias give the game's values back.
cbuffer InputConversion : register(b1) { float4 input_scale[16]; float4 input_bias[16]; };
#define POSITION_OUT SV_Position
#else
float4 c[CONSTANTS] : register(c0);
#define POSITION_OUT POSITION
#endif
static const float4 c200_def = float4(0.5, 3, 1, 0);
struct Input {
  float4 v0 : POSITION0;
  float4 v1 : BLENDWEIGHT0;
  float4 v2 : BLENDINDICES0;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v1 = input.v1 * input_scale[1] + input_bias[1];
  const float4 v2 = input.v2 * input_scale[2] + input_bias[2];
#else
  const float4 v0 = input.v0;
  const float4 v1 = input.v1;
  const float4 v2 = input.v2;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  float4 r3 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyzw = ((c200_def.xxxx + v2)).xyzw;
  r1.xyzw = ((r0 - floor(r0))).xyzw;
  r0.xyzw = ((r0 + (-r1))).xyzw;
  r0.xyzw = ((r0 * c200_def.yyyy)).xyzw;
  a0.x = int4(round(r0.yyyy)).x;
  r1.xyzw = ((v0.xyzx * c200_def.zzzw + c200_def.wwwz)).xyzw;
  r2.x = (dot(c[0 + a0.x], r1).xxxx).x;
  r2.y = (dot(c[1 + a0.x], r1).xxxx).y;
  r2.z = (dot(c[2 + a0.x], r1).xxxx).z;
  r2.xyz = ((r2 * v1.yyyy)).xyz;
  a0.x = int4(round(r0.xxxx)).x;
  r3.x = (dot(c[0 + a0.x], r1).xxxx).x;
  r3.y = (dot(c[1 + a0.x], r1).xxxx).y;
  r3.z = (dot(c[2 + a0.x], r1).xxxx).z;
  r2.xyz = ((r3 * v1.xxxx + r2)).xyz;
  a0.xy = int4(round(r0.zwzw)).xy;
  r0.x = (dot(c[0 + a0.x], r1).xxxx).x;
  r0.y = (dot(c[1 + a0.x], r1).xxxx).y;
  r0.z = (dot(c[2 + a0.x], r1).xxxx).z;
  r0.xyz = ((r0 * v1.zzzz + r2)).xyz;
  r2.x = (dot(c[0 + a0.y], r1).xxxx).x;
  r2.y = (dot(c[1 + a0.y], r1).xxxx).y;
  r2.z = (dot(c[2 + a0.y], r1).xxxx).z;
  r0.xyz = ((r2 * v1.wwww + r0)).xyz;
  r0.w = (c200_def.zzzz).w;
  r1.x = (dot(c[196], r0).xxxx).x;
  r1.y = (dot(c[197], r0).xxxx).y;
  r1.z = (dot(c[198], r0).xxxx).z;
  r1.w = (dot(c[199], r0).xxxx).w;
  o0.x = (dot(c[192], r1).xxxx).x;
  o0.y = (dot(c[193], r1).xxxx).y;
  o0.z = (dot(c[194], r1).xxxx).z;
  o0.w = (dot(c[195], r1).xxxx).w;
  return o0;
}
