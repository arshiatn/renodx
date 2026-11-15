// R.U.S.E.'s vertex shader 0x7DF74CEE for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c4_def = float4(0.00999999978, 1, 10000, 0);
struct Input {
  float4 v0 : POSITION0;
  float4 v2 : TEXCOORD0;
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
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xy = (v0).xy;
  r0.xy = (((-r0) + v2)).xy;
  r0.zw = ((r0.xyxy * r0.xyxy)).zw;
  r0.z = ((r0.wwww + r0.zzzz)).z;
  r0.z = (rsqrt(abs((r0.zzzz).x)).xxxx).z;
  r0.z = ((1.0f / (r0.zzzz).x).xxxx).z;
  r0.z = (max(r0.zzzz, c4_def.xxxx)).z;
  r0.z = ((1.0f / (r0.zzzz).x).xxxx).z;
  r0.xy = ((r0.zzzz * r0)).xy;
  r0.xy = ((r0 * c4_def.zzzz + v0)).xy;
  r0.zw = ((v0.zzzz * c4_def.xyyw + c4_def.xywy)).zw;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  o0.w = (dot(c[3], r0).xxxx).w;
  return o0;
}
