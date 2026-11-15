// R.U.S.E.'s vertex shader 0x9116CDFF for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c9_def = float4(-0.5, 3, 0.0250000004, 2000);
static const float4 c10_def = float4(0.300000012, 1, 3, 0);
struct Input {
  float4 v0 : POSITION0;
  float4 v1 : NORMAL1;
  float4 v2 : PSIZE0;
  float4 v5 : TEXCOORD1;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v1 = input.v1 * input_scale[1] + input_bias[1];
  const float4 v2 = input.v2 * input_scale[2] + input_bias[2];
  const float4 v5 = input.v5 * input_scale[5] + input_bias[5];
#else
  const float4 v0 = input.v0;
  const float4 v1 = input.v1;
  const float4 v2 = input.v2;
  const float4 v5 = input.v5;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyz = ((c9_def.xxxx + v1)).xyz;
  r0.xyz = ((r0 + r0)).xyz;
  r1.xyz = ((r0 * rsqrt(dot((r0).xyz, (r0).xyz)))).xyz;
  r0.xyz = ((r1 * v2.xxxx)).xyz;
  r0.xyz = ((r0 * v5.xxxx)).xyz;
  r0.xyz = ((r0 * v5.yyyy)).xyz;
  r0.xyz = ((r0 * c9_def.yyyy)).xyz;
  r0.w = ((1.0f / (c[8].zzzz).x).xxxx).w;
  r1.x = ((c[4].zzzz + (-v0.zzzz))).x;
  r0.w = ((r0.wwww * r1.xxxx)).w;
  r1.x = ((r1.xxxx * c9_def.zzzz)).x;
  r1.x = (max(r1.xxxx, c9_def.wwww)).x;
  r1.x = ((r1.xxxx * c10_def.xxxx)).x;
  r1.xyz = ((r1.xxxx * c[5])).xyz;
  r0.w = (max(r0.wwww, c[8].xxxx)).w;
  r0.w = (min(r0.wwww, c[8].yyyy)).w;
  r0.xyz = ((r0.wwww * r0 + r1)).xyz;
  r0.xyz = ((r0 + v0)).xyz;
  r0.w = (c10_def.yyyy).w;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  o0.w = (dot(c[3], r0).xxxx).w;
  return o0;
}
