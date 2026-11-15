// R.U.S.E.'s vertex shader 0xFC8676B4 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c12_def = float4(-0.001953125, 14539, 0.5, -0.5);
static const float4 c13_def = float4(1, 0, 750000, 9.99999997e-07);
static const float4 c14_def = float4(-0.00100000005, 0.00100000005, 1.44269502, -1.44269502);
static const float4 c15_def = float4(-1, 1, 0, 0);
struct Input {
  float4 v0 : TEXCOORD0;
  float4 v1 : POSITION0;
  float4 v3 : TEXCOORD1;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v1 = input.v1 * input_scale[1] + input_bias[1];
  const float4 v3 = input.v3 * input_scale[3] + input_bias[3];
#else
  const float4 v0 = input.v0;
  const float4 v1 = input.v1;
  const float4 v3 = input.v3;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  float4 r3 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyz = (c[8]).xyz;
  r0.xyz = ((v1 * r0 + c[9])).xyz;
  r1.xyz = ((r0.zxyw + (-c[10].zxyw))).xyz;
  r0.w = (dot((r1).xyz, (r1).xyz).xxxx).w;
  r0.w = (rsqrt(abs((r0.wwww).x)).xxxx).w;
  r1.xyz = ((r0.wwww * r1)).xyz;
  r2.xyz = ((r1.zxyw * c13_def.xyyw)).xyz;
  r2.xyz = ((r1 * c13_def.yxyw + (-r2))).xyz;
  r3.xyz = ((r1 * r2.yzxw)).xyz;
  r1.xyz = ((r1.zxyw * r2.zxyw + (-r3))).xyz;
  r3.xyz = ((r2 * rsqrt(dot((r2).xyz, (r2).xyz)))).xyz;
  r2.xyz = ((r1 * rsqrt(dot((r1).xyz, (r1).xyz)))).xyz;
  r1.xy = ((c12_def.yyyy * v3)).xy;
  r1.xy = ((r1 * v0)).xy;
  r1.xy = ((r1 * c12_def.zwzw)).xy;
  r1.yzw = ((r2.xxyz * r1.yyyy)).yzw;
  r1.xyz = ((r1.xxxx * r3 + r1.yzww)).xyz;
  r0.xyz = ((r0 + r1)).xyz;
  r0.w = (c13_def.xxxx).w;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  o0.w = (dot(c[3], r0).xxxx).w;
  return o0;
}
