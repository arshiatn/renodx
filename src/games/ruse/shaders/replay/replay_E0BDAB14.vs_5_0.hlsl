// R.U.S.E.'s vertex shader 0xE0BDAB14 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c11_def = float4(1, 0, -0.5, 0.5);
static const float4 c12_def = float4(2, -2, 0, 0);
struct Input {
  float4 v0 : POSITION0;
  float4 v3 : TEXCOORD0;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v3 = input.v3 * input_scale[3] + input_bias[3];
#else
  const float4 v0 = input.v0;
  const float4 v3 = input.v3;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyzw = ((v0.xyzx * c11_def.xxxy + c11_def.yyyx)).xyzw;
  r1.x = (dot(c[4], r0).xxxx).x;
  r1.y = (dot(c[5], r0).xxxx).y;
  r1.z = (dot(c[6], r0).xxxx).z;
  r1.w = (dot(c[7], r0).xxxx).w;
  o0.z = (dot(c[2], r1).xxxx).z;
  o0.w = (dot(c[3], r1).xxxx).w;
  r0.x = (dot(c[0], r1).xxxx).x;
  r0.y = (dot(c[1], r1).xxxx).y;
  r0.zw = ((c11_def.zzzz + v3.xyxy)).zw;
  r0.zw = ((r0 + r0)).zw;
  r1.xy = ((r0.zwzw * c[10].xxxx)).xy;
  r1.xy = ((r1 * c[8])).xy;
  o0.xy = ((r1 * c12_def + r0)).xy;
  return o0;
}
