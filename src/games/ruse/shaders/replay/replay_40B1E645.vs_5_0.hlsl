// R.U.S.E.'s vertex shader 0x40B1E645 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c9_def = float4(0.5, -0.5, -1, 1);
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
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xy = ((c[5] * c[5])).xy;
  r0.x = ((r0.yyyy + r0.xxxx)).x;
  r0.x = (rsqrt(abs((r0.xxxx).x)).xxxx).x;
  r0.x = ((1.0f / (r0.xxxx).x).xxxx).x;
  r0.y = ((c[5].zzzz * v0.wwww)).y;
  r1.xy = ((c8_def.wwww * v2.xxxx)).xy;
  r1.z = ((r0.xxxx * r1.xxxx + r0.yyyy)).z;
  r0.xy = ((v2.zwzw * c8_def.xxxx + c8_def.yyyy)).xy;
  r0.zw = ((r0.xyxy * r1.xyyz)).zw;
  r0.zw = ((r0 * c9_def.xyxy)).zw;
  r1.xyz = ((r0.wwww * c[5])).xyz;
  r1.xyz = ((r0.zzzz * c[4] + r1)).xyz;
  r2.xyz = (c[6]).xyz;
  r2.xyz = ((v0 * r2 + c[7])).xyz;
  r1.xyz = ((r1 + r2)).xyz;
  r0.z = (dot((c[2]).xyz, (r1).xyz).xxxx).z;
  r0.z = ((r0.zzzz + c[2].wwww)).z;
  r0.w = ((c[3].wwww + c[3].wwww)).w;
  r0.w = ((1.0f / (r0.wwww).x).xxxx).w;
  r1.w = ((r0.wwww * r0.zzzz)).w;
  o0.z = ((r1.wwww + r1.wwww)).z;
  r2.zw = (c9_def).zw;
  r2.xy = ((r2.zwzw + c[3].wwww)).xy;
  r0.zw = ((r0.wwww * r2.xyxy)).zw;
  r1.w = (dot((c[0]).xyz, (r1).xyz).xxxx).w;
  r1.x = (dot((c[1]).xyz, (r1).xyz).xxxx).x;
  r1.x = ((r1.xxxx + c[1].wwww)).x;
  r2.x = ((-r1.xxxx)).x;
  r1.x = ((r1.wwww + c[0].wwww)).x;
  r2.yw = ((r1.xxxx * r0.xwzz + r0.xzzw)).yw;
  o0.xyw = (r2).xyw;
  return o0;
}
