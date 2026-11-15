// R.U.S.E.'s vertex shader 0xCEB9F819 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c12_def = float4(1, 0, 9.99999975e-05, 32);
static const float4 c13_def = float4(9.99999997e-07, -0.00100000005, 0.00100000005, 1.44269502);
struct Input {
  float4 v0 : POSITION0;
  float4 v1 : POSITION1;
  float4 v6 : PSIZE1;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v1 = input.v1 * input_scale[1] + input_bias[1];
  const float4 v6 = input.v6 * input_scale[6] + input_bias[6];
#else
  const float4 v0 = input.v0;
  const float4 v1 = input.v1;
  const float4 v6 = input.v6;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyzw = ((v0.xyzx * c12_def.xxxy + c12_def.yyyx)).xyzw;
  r1.x = (dot(c[4], r0).xxxx).x;
  r1.y = (dot(c[5], r0).xxxx).y;
  r1.z = (dot(c[6], r0).xxxx).z;
  r0.xyz = ((c12_def.xxyw * v1.xyxw)).xyz;
  r2.x = (dot((c[7]).xyz, (r0).xyz).xxxx).x;
  r2.y = (dot((c[8]).xyz, (r0).xyz).xxxx).y;
  r2.z = (dot((c[9]).xyz, (r0).xyz).xxxx).z;
  r0.xyz = ((r1 + r2)).xyz;
  r1.x = (((-r0.xxxx) + c[7].wwww)).x;
  r1.y = (((-r0.yyyy) + c[8].wwww)).y;
  r1.z = (((-r0.zzzz) + c[9].wwww)).z;
  r0.w = (dot((r1).xyz, (r1).xyz).xxxx).w;
  r0.w = (rsqrt(abs((r0.wwww).x)).xxxx).w;
  r1.xyw = ((r0.wwww * r1.xyzz)).xyw;
  r2.xyz = ((v6.xxxx * r1.xyww + r0)).xyz;
  r2.w = (c12_def.xxxx).w;
  o0.x = (dot(c[0], r2).xxxx).x;
  o0.y = (dot(c[1], r2).xxxx).y;
  o0.z = (dot(c[2], r2).xxxx).z;
  r0.x = (dot(c[3], r2).xxxx).x;
  o0.w = (r0.xxxx).w;
  return o0;
}
