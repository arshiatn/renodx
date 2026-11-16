// R.U.S.E.'s vertex shader 0x36741B0B for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c204_def = float4(0.5, 3, 1, 0);
static const float4 c205_def = float4(2, -1, 0.5625, 0);
static const float4 c206_def = float4(0.99609375, 0, 0.00390625, 0);
static const float4 c207_def = float4(0.350000024, -0.0499999523, 0.200000003, 0.649999976);
struct Input {
  float4 v0 : POSITION0;
  float4 v2 : BLENDINDICES0;
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
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xy = ((c204_def.xxxx + v2.xzzw)).xy;
  r0.zw = ((r0.xyxy - floor(r0.xyxy))).zw;
  r0.xy = (((-r0.zwzw) + r0)).xy;
  r0.xy = ((r0 * c204_def.yyyy)).xy;
  a0.xy = int4(round(r0.yxzw)).xy;
  r0.xyzw = ((v0.xyzx * c204_def.zzzw + c204_def.wwwz)).xyzw;
  r1.x = (dot(c[0 + a0.y], r0).xxxx).x;
  r1.y = (dot(c[1 + a0.y], r0).xxxx).y;
  r1.z = (dot(c[2 + a0.y], r0).xxxx).z;
  r1.w = (c204_def.zzzz).w;
  o0.x = (dot(c[192], r1).xxxx).x;
  o0.y = (dot(c[193], r1).xxxx).y;
  o0.z = (dot(c[194], r1).xxxx).z;
  o0.w = (dot(c[195], r1).xxxx).w;
  return o0;
}
