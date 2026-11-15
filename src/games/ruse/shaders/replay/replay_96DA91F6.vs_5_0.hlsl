// R.U.S.E.'s vertex shader 0x96DA91F6 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c12_def = float4(1, 0, 0.5, 0.159154937);
static const float4 c13_def = float4(6.28318548, -3.14159274, 9.99999975e-05, 32);
static const float4 c14_def = float4(1, -1, 9.99999997e-07, -0.00100000005);
static const float4 c15_def = float4(1.44269502, -1.44269502, 0, 0);
struct Input {
  float4 v0 : TEXCOORD0;
  float4 v1 : POSITION0;
  float4 v2 : TEXCOORD1;
  float4 v7 : PSIZE1;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v1 = input.v1 * input_scale[1] + input_bias[1];
  const float4 v2 = input.v2 * input_scale[2] + input_bias[2];
  const float4 v7 = input.v7 * input_scale[7] + input_bias[7];
#else
  const float4 v0 = input.v0;
  const float4 v1 = input.v1;
  const float4 v2 = input.v2;
  const float4 v7 = input.v7;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.x = ((v2.yyyy * c12_def.wwww + c12_def.zzzz)).x;
  r0.x = ((r0.xxxx - floor(r0.xxxx))).x;
  r0.x = ((r0.xxxx * c13_def.xxxx + c13_def.yyyy)).x;
  r1.xy = (float4(cos((r0.xxxx).x), sin((r0.xxxx).x), 0, 0)).xy;
  r0.x = ((c12_def.zzzz * v2.xxxx)).x;
  r0.xyz = ((r0.xxxx * r1.xyxw)).xyz;
  r0.xzw = ((r0.xyyz * v0.xyxy)).xzw;
  r1.x = (((-r0.yyyy) * v0.yyyy + r0.xxxx)).x;
  r1.y = ((r0.wwww + r0.zzzz)).y;
  r1.z = (c12_def.yyyy).z;
  r0.x = (dot((c[7]).xyz, (r1).xyz).xxxx).x;
  r0.y = (dot((c[8]).xyz, (r1).xyz).xxxx).y;
  r0.z = (dot((c[9]).xyz, (r1).xyz).xxxx).z;
  r1.xyzw = ((v1.xyzx * c12_def.xxxy + c12_def.yyyx)).xyzw;
  r2.x = (dot(c[4], r1).xxxx).x;
  r2.y = (dot(c[5], r1).xxxx).y;
  r2.z = (dot(c[6], r1).xxxx).z;
  r0.xyz = ((r0 + r2)).xyz;
  r1.x = (((-r0.xxxx) + c[7].wwww)).x;
  r1.y = (((-r0.yyyy) + c[8].wwww)).y;
  r1.z = (((-r0.zzzz) + c[9].wwww)).z;
  r1.w = (dot((r1).xyz, (r1).xyz).xxxx).w;
  r1.w = (rsqrt(abs((r1.wwww).x)).xxxx).w;
  r2.xyz = ((r1.wwww * r1)).xyz;
  r2.xyz = ((v7.xxxx * r2 + r0)).xyz;
  r2.w = (c12_def.xxxx).w;
  o0.x = (dot(c[0], r2).xxxx).x;
  o0.y = (dot(c[1], r2).xxxx).y;
  o0.z = (dot(c[2], r2).xxxx).z;
  r0.x = (dot(c[3], r2).xxxx).x;
  o0.w = (r0.xxxx).w;
  return o0;
}
