// R.U.S.E.'s vertex shader 0x7F9C476E for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c7_def = float4(1, 0, 7.14285716e-06, 0);
struct Input {
  float4 v0 : TEXCOORD0;
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
  r0.xy = ((c[5] * v0)).xy;
  r1.xy = (c7_def).xy;
  r0.zw = ((c[4].xxxx * r1.xyxy + r1.xyyx)).zw;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  o0.w = (dot(c[3], r0).xxxx).w;
  return o0;
}
