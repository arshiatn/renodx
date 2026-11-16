// R.U.S.E.'s vertex shader 0x52ED445B for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c10_def = float4(750000, 9.99999997e-07, -0.00100000005, 0.00100000005);
static const float4 c11_def = float4(2, -1, -0, -5);
static const float4 c12_def = float4(1.44269502, -1.44269502, 0.100000001, 0.199999988);
static const float4 c13_def = float4(0.800000012, 0, -1, 1);
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
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyzw = ((v0.xyzx * (-c11_def.yyyz) + (-c11_def.zzwy))).xyzw;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  r0.x = (dot(c[3], r0).xxxx).x;
  o0.w = ((r0.xxxx + (-c11_def.yyyy))).w;
  return o0;
}
