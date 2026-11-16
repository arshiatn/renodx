// R.U.S.E.'s vertex shader 0xB3C4D711 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c11_def = float4(1, 0, 0.5, 0.159154937);
static const float4 c12_def = float4(6.28318548, -3.14159274, 9.99999975e-05, 32);
static const float4 c13_def = float4(1, -1, 0, 0);
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
  int4 a0 = 0;
  float4 o0 = 0;
  r0.x = ((v2.yyyy * c11_def.wwww + c11_def.zzzz)).x;
  r0.x = ((r0.xxxx - floor(r0.xxxx))).x;
  r0.x = ((r0.xxxx * c12_def.xxxx + c12_def.yyyy)).x;
  r1.xy = (float4(cos((r0.xxxx).x), sin((r0.xxxx).x), 0, 0)).xy;
  r0.x = ((c11_def.zzzz * v2.xxxx)).x;
  r0.xyz = ((r0.xxxx * r1.xyxw)).xyz;
  r0.xzw = ((r0.xyyz * v0.xyxy)).xzw;
  r0.x = (((-r0.yyyy) * v0.yyyy + r0.xxxx)).x;
  r0.y = ((r0.wwww + r0.zzzz)).y;
  r1.xyzw = ((v1.xyzx * c11_def.xxxy + c11_def.yyyx)).xyzw;
  r2.x = (dot(c[4], r1).xxxx).x;
  r3.x = (((-r2.xxxx) + c[7].wwww)).x;
  r2.y = (dot(c[5], r1).xxxx).y;
  r2.z = (dot(c[6], r1).xxxx).z;
  r3.y = (((-r2.yyyy) + c[8].wwww)).y;
  r3.z = (((-r2.zzzz) + c[9].wwww)).z;
  r1.xyz = ((r3 * rsqrt(dot((r3).xyz, (r3).xyz)))).xyz;
  r1.xyz = ((v7.xxxx * r1 + r2)).xyz;
  r1.w = (c11_def.xxxx).w;
  r0.z = (dot(c[3], r1).xxxx).z;
  r0.xy = ((r0.zzzz * r0)).xy;
  r2.xy = ((r0 * c[10])).xy;
  r2.z = (c11_def.yyyy).z;
  r3.x = (dot(c[0], r1).xxxx).x;
  r3.y = (dot(c[1], r1).xxxx).y;
  r3.z = (dot(c[2], r1).xxxx).z;
  o0.xyz = ((r2 + r3)).xyz;
  o0.w = (r0.zzzz).w;
  return o0;
}
