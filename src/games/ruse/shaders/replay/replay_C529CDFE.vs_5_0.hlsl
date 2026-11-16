// R.U.S.E.'s vertex shader 0xC529CDFE for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c8_def = float4(2, -1, -0.001953125, 14539);
static const float4 c9_def = float4(0.5, -0.5, 1, 0);
static const float4 c10_def = float4(750000, 9.99999997e-07, -0.00100000005, 0.00100000005);
static const float4 c11_def = float4(1.44269502, -1.44269502, 0, 0);
struct Input {
  float4 v0 : POSITION0;
  float4 v2 : COLOR1;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v2 = input.v2 * input_scale[2] + input_bias[2];
#else
  const float4 v0 = input.v0;
  const float4 v2 = input.v2;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  float4 r3 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyz = (c[4]).xyz;
  r0.xyz = ((v0 * r0 + c[5])).xyz;
  r1.xyz = ((r0.zxyw + (-c[6].zxyw))).xyz;
  r0.w = (dot((r1).xyz, (r1).xyz).xxxx).w;
  r0.w = (rsqrt(abs((r0.wwww).x)).xxxx).w;
  r1.xyz = ((r0.wwww * r1)).xyz;
  r2.xyz = ((r1.zxyw * c9_def.zwww)).xyz;
  r2.xyz = ((r1 * c9_def.wzww + (-r2))).xyz;
  r3.xyz = ((r1 * r2.yzxw)).xyz;
  r1.xyz = ((r1.zxyw * r2.zxyw + (-r3))).xyz;
  r3.xyz = ((r2 * rsqrt(dot((r2).xyz, (r2).xyz)))).xyz;
  r2.xyz = ((r1 * rsqrt(dot((r1).xyz, (r1).xyz)))).xyz;
  r1.xy = ((c8_def.wwww * v2)).xy;
  r1.zw = ((v2 * c8_def.xxxx + c8_def.yyyy)).zw;
  r1.xy = ((r1.zwzw * r1)).xy;
  r1.xy = ((r1 * c9_def)).xy;
  r2.xyz = ((r2 * r1.yyyy)).xyz;
  r2.xyz = ((r1.xxxx * r3 + r2)).xyz;
  r0.xyz = ((r0 + r2)).xyz;
  r0.w = ((-c8_def.yyyy)).w;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  o0.w = (dot(c[3], r0).xxxx).w;
  return o0;
}
