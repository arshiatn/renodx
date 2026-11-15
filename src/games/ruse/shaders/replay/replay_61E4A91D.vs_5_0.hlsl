// R.U.S.E.'s vertex shader 0x61E4A91D for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c8_def = float4(-0.001953125, 14539, 0.5, -0.5);
static const float4 c9_def = float4(-1, 1, 0, 0);
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
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xy = ((c[5] * c[5])).xy;
  r0.x = ((r0.yyyy + r0.xxxx)).x;
  r0.x = (rsqrt(abs((r0.xxxx).x)).xxxx).x;
  r0.x = ((1.0f / (r0.xxxx).x).xxxx).x;
  r0.y = ((c[5].zzzz * v1.wwww)).y;
  r1.xy = ((c8_def.yyyy * v3.xxxx)).xy;
  r1.z = ((r0.xxxx * r1.xxxx + r0.yyyy)).z;
  r0.xy = ((r1.yzzw * v0)).xy;
  r0.xy = ((r0 * c8_def.zwzw)).xy;
  r0.yzw = ((r0.yyyy * c[5].xxyz)).yzw;
  r0.xyz = ((r0.xxxx * c[4] + r0.yzww)).xyz;
  r1.xyz = (c[6]).xyz;
  r1.xyz = ((v1 * r1 + c[7])).xyz;
  r0.xyz = ((r0 + r1)).xyz;
  r0.w = (dot((c[2]).xyz, (r0).xyz).xxxx).w;
  r0.w = ((r0.wwww + c[2].wwww)).w;
  r1.x = ((c[3].wwww + c[3].wwww)).x;
  r1.x = ((1.0f / (r1.xxxx).x).xxxx).x;
  r1.y = ((r0.wwww * r1.xxxx)).y;
  o0.z = ((r1.yyyy + r1.yyyy)).z;
  r0.w = (c[3].wwww).w;
  r1.yz = ((r0.wwww + c9_def.xxyw)).yz;
  r1.xy = ((r1.xxxx * r1.yzzw)).xy;
  r0.w = (dot((c[0]).xyz, (r0).xyz).xxxx).w;
  r0.x = (dot((c[1]).xyz, (r0).xyz).xxxx).x;
  r0.x = ((r0.xxxx + c[1].wwww)).x;
  r2.x = ((-r0.xxxx)).x;
  r0.x = ((r0.wwww + c[0].wwww)).x;
  r2.yw = ((r0.xxxx * r1.xyzx + r1.xxzy)).yw;
  o0.xyw = (r2).xyw;
  return o0;
}
