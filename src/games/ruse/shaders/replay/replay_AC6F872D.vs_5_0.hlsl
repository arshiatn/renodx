// R.U.S.E.'s vertex shader 0xAC6F872D for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c7_def = float4(3, -0.5, 1, 0);
struct Input {
  float4 v0 : POSITION0;
  float4 v1 : NORMAL0;
  float4 v2 : NORMAL1;
  float4 v3 : PSIZE0;
  float4 v7 : TEXCOORD2;
  float4 v8 : TEXCOORD3;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v1 = input.v1 * input_scale[1] + input_bias[1];
  const float4 v2 = input.v2 * input_scale[2] + input_bias[2];
  const float4 v3 = input.v3 * input_scale[3] + input_bias[3];
  const float4 v7 = input.v7 * input_scale[7] + input_bias[7];
  const float4 v8 = input.v8 * input_scale[8] + input_bias[8];
#else
  const float4 v0 = input.v0;
  const float4 v1 = input.v1;
  const float4 v2 = input.v2;
  const float4 v3 = input.v3;
  const float4 v7 = input.v7;
  const float4 v8 = input.v8;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyz = ((c7_def.yyyy + v1)).xyz;
  r0.xyz = ((r0 + r0)).xyz;
  r1.xyz = ((r0 * rsqrt(dot((r0).xyz, (r0).xyz)))).xyz;
  r0.xy = (v7).xy;
  r0.xy = ((r0.yxzw * c7_def.xxxx + v8.yxzw)).xy;
  r0.xzw = ((r1.xyyz * r0.xxxx)).xzw;
  r0.xzw = ((r0 * v3.xxxx)).xzw;
  r1.xyz = ((c7_def.yyyy + v2)).xyz;
  r1.xyz = ((r1 + r1)).xyz;
  r2.xyz = ((r1 * rsqrt(dot((r1).xyz, (r1).xyz)))).xyz;
  r1.xyz = ((r0.yyyy * r2)).xyz;
  r0.xyz = ((r1 * v3.xxxx + r0.xzww)).xyz;
  r0.xyz = ((r0 + v0)).xyz;
  r0.w = (c7_def.zzzz).w;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  o0.w = (dot(c[3], r0).xxxx).w;
  return o0;
}
