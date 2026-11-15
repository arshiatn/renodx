// R.U.S.E.'s vertex shader 0x76EC5531 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c8_def = float4(3.05185094e-05, 1, 750000, 9.99999997e-07);
static const float4 c9_def = float4(0, -50, -0.00100000005, 0.00100000005);
static const float4 c10_def = float4(1.44269502, -1.44269502, 0, 0);
struct Input {
  float4 v0 : POSITION0;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
#else
  const float4 v0 = input.v0;
#endif
  float4 r1 = 0;
  float4 r2 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r1.xyzw = ((c8_def.xxxx * v0)).xyzw;
  r1.xyzw = ((r1 * c[7].xyzz)).xyzw;
  r2.xyzw = ((v0 * (-c8_def.xxxx) + c8_def.yyyy)).xyzw;
  r1.xyzw = ((r2 * c[6].xyzz + r1)).xyzw;
  r2.xyz = ((r1.xyww + c9_def.xxyw)).xyz;
  r2.w = (c8_def.yyyy).w;
  o0.x = (dot(c[0], r2).xxxx).x;
  o0.y = (dot(c[1], r2).xxxx).y;
  o0.z = (dot(c[2], r2).xxxx).z;
  o0.w = (dot(c[3], r2).xxxx).w;
  return o0;
}
