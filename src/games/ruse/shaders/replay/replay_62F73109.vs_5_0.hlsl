// R.U.S.E.'s vertex shader 0x62F73109 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c21_def = float4(1, 0, 2, -1);
static const float4 c22_def = float4(0.5625, 0.99609375, 0, 0.00390625);
static const float4 c23_def = float4(0.350000024, -0.0499999523, 0.200000003, 0.649999976);
static const float4 c24_def = float4(0.5, 1, 0, 0);
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
  r0.xyzw = ((v0.xyzx * c21_def.xxxy + c21_def.yyyx)).xyzw;
  r1.x = (dot(c[7], r0).xxxx).x;
  r1.y = (dot(c[8], r0).xxxx).y;
  r1.z = (dot(c[9], r0).xxxx).z;
  r1.w = (c21_def.xxxx).w;
  o0.x = (dot(c[0], r1).xxxx).x;
  o0.y = (dot(c[1], r1).xxxx).y;
  o0.z = (dot(c[2], r1).xxxx).z;
  o0.w = (dot(c[3], r1).xxxx).w;
  return o0;
}
