// R.U.S.E.'s vertex shader 0x3094BC87 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c11_def = float4(1, 0, 0.99609375, 0.00390625);
static const float4 c12_def = float4(9.99999997e-07, -0.00100000005, 0.00100000005, 1.44269502);
struct Input {
  float4 v0 : POSITION0;
  float4 v4 : PSIZE1;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v4 = input.v4 * input_scale[4] + input_bias[4];
#else
  const float4 v0 = input.v0;
  const float4 v4 = input.v4;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyzw = ((v0.xyzx * c11_def.xxxy + c11_def.yyyx)).xyzw;
  r1.x = (dot(c[4], r0).xxxx).x;
  r2.x = (((-r1.xxxx) + c[7].wwww)).x;
  r1.y = (dot(c[5], r0).xxxx).y;
  r1.z = (dot(c[6], r0).xxxx).z;
  r2.y = (((-r1.yyyy) + c[8].wwww)).y;
  r2.z = (((-r1.zzzz) + c[9].wwww)).z;
  r0.x = (dot((r2).xyz, (r2).xyz).xxxx).x;
  r0.x = (rsqrt(abs((r0.xxxx).x)).xxxx).x;
  r0.yzw = ((r0.xxxx * r2.xxyz)).yzw;
  r1.xyz = ((v4.xxxx * r0.yzww + r1)).xyz;
  r1.w = (c11_def.xxxx).w;
  o0.x = (dot(c[0], r1).xxxx).x;
  o0.y = (dot(c[1], r1).xxxx).y;
  o0.z = (dot(c[2], r1).xxxx).z;
  o0.w = (dot(c[3], r1).xxxx).w;
  return o0;
}
