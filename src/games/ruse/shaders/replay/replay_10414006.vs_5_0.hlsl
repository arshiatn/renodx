// R.U.S.E.'s vertex shader 0x10414006 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c11_def = float4(1, 0, 9.99999975e-05, 32);
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
  r0.xyzw = ((v0.xyzx * c11_def.xxxy + c11_def.yyyx)).xyzw;
  r1.x = (dot(c[4], r0).xxxx).x;
  r1.y = (dot(c[5], r0).xxxx).y;
  r1.z = (dot(c[6], r0).xxxx).z;
  r0.xyz = ((c11_def.xxyw * v1.xyxw)).xyz;
  r2.x = (dot((c[7]).xyz, (r0).xyz).xxxx).x;
  r2.y = (dot((c[8]).xyz, (r0).xyz).xxxx).y;
  r2.z = (dot((c[9]).xyz, (r0).xyz).xxxx).z;
  r0.xyz = ((r1 + r2)).xyz;
  r1.x = (((-r0.xxxx) + c[7].wwww)).x;
  r1.y = (((-r0.yyyy) + c[8].wwww)).y;
  r1.z = (((-r0.zzzz) + c[9].wwww)).z;
  r2.xyz = ((r1 * rsqrt(dot((r1).xyz, (r1).xyz)))).xyz;
  r0.xyz = ((v6.xxxx * r2 + r0)).xyz;
  r0.w = (c11_def.xxxx).w;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  r0.x = (dot(c[3], r0).xxxx).x;
  o0.w = (r0.xxxx).w;
  return o0;
}
