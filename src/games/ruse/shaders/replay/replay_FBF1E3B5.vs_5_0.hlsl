// R.U.S.E.'s vertex shader 0xFBF1E3B5 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
  float4 r3 = 0;
  float4 r4 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.z = (c12_def.yyyy).z;
  r0.w = ((v2.yyyy * c12_def.wwww + c12_def.zzzz)).w;
  r0.w = ((r0.wwww - floor(r0.wwww))).w;
  r0.w = ((r0.wwww * c13_def.xxxx + c13_def.yyyy)).w;
  r1.xy = (float4(cos((r0.wwww).x), sin((r0.wwww).x), 0, 0)).xy;
  r0.w = ((c12_def.zzzz * v2.xxxx)).w;
  r1.xyz = ((r0.wwww * r1.xyxw)).xyz;
  r1.xzw = ((r1.xyyz * v0.xyxy)).xzw;
  r1.x = (((-r1.yyyy) * v0.yyyy + r1.xxxx)).x;
  r1.y = ((r1.wwww + r1.zzzz)).y;
  r2.xyzw = ((v1.xyzx * c12_def.xxxy + c12_def.yyyx)).xyzw;
  r3.x = (dot(c[4], r2).xxxx).x;
  r4.x = (((-r3.xxxx) + c[7].wwww)).x;
  r3.y = (dot(c[5], r2).xxxx).y;
  r3.z = (dot(c[6], r2).xxxx).z;
  r4.y = (((-r3.yyyy) + c[8].wwww)).y;
  r4.z = (((-r3.zzzz) + c[9].wwww)).z;
  r0.w = (dot((r4).xyz, (r4).xyz).xxxx).w;
  r0.w = (rsqrt(abs((r0.wwww).x)).xxxx).w;
  r2.xyz = ((r0.wwww * r4)).xyz;
  r2.xyz = ((v7.xxxx * r2 + r3)).xyz;
  r2.w = (c12_def.xxxx).w;
  r1.z = (dot(c[3], r2).xxxx).z;
  r1.xy = ((r1.zzzz * r1)).xy;
  r0.xy = ((r1 * c[11])).xy;
  r3.x = (dot(c[0], r2).xxxx).x;
  r3.y = (dot(c[1], r2).xxxx).y;
  r3.z = (dot(c[2], r2).xxxx).z;
  o0.xyz = ((r0 + r3)).xyz;
  o0.w = (r1.zzzz).w;
  return o0;
}
