// R.U.S.E.'s vertex shader 0xAED34F9C for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c12_def = float4(2, -1, -0, -5);
static const float4 c13_def = float4(750000, 9.99999997e-07, -0.00100000005, 0.00100000005);
static const float4 c14_def = float4(1.44269502, -1.44269502, 0, 0);
struct Input {
  float4 v0 : POSITION0;
  float4 v6 : PSIZE1;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v6 = input.v6 * input_scale[6] + input_bias[6];
#else
  const float4 v0 = input.v0;
  const float4 v6 = input.v6;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyzw = ((v0.xyzx * (-c12_def.yyyz) + (-c12_def.zzwy))).xyzw;
  r1.x = (dot(c[3], r0).xxxx).x;
  o0.w = ((r1.xxxx + (-c12_def.yyyy))).w;
  r1.x = (dot(c[0], r0).xxxx).x;
  r2.x = (((-r1.xxxx) + c[4].wwww)).x;
  r1.y = (dot(c[1], r0).xxxx).y;
  r1.z = (dot(c[2], r0).xxxx).z;
  r2.y = (((-r1.yyyy) + c[5].wwww)).y;
  r2.z = (((-r1.zzzz) + c[6].wwww)).z;
  r0.xyz = ((r2 * rsqrt(dot((r2).xyz, (r2).xyz)))).xyz;
  o0.xyz = ((v6.xxxx * r0 + r1)).xyz;
  return o0;
}
