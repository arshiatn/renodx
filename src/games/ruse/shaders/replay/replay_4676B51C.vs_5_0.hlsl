// R.U.S.E.'s vertex shader 0x4676B51C for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c5_def = float4(-100000, 9.99999975e-06, 1, 0);
static const float4 c6_def = float4(0.5, -0.5, 0, 0);
struct Input {
  float4 v0 : POSITION0;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
#else
  const float4 v0 = input.v0;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.x = ((c[4].zzzz + (-v0.zzzz))).x;
  r0.x = ((r0.xxxx + c5_def.xxxx)).x;
  r0.x = (saturate((r0.xxxx * c5_def.yyyy))).x;
  r1.z = ((v0.zzzz + r0.xxxx * (v0.wwww - v0.zzzz))).z;
  r1.xyw = ((v0.xyzx * c5_def.zzzw + c5_def.wwzz)).xyw;
  o0.x = (dot(c[0], r1).xxxx).x;
  o0.y = (dot(c[1], r1).xxxx).y;
  o0.z = (dot(c[2], r1).xxxx).z;
  o0.w = (dot(c[3], r1).xxxx).w;
  return o0;
}
