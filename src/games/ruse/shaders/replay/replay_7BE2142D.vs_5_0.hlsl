// R.U.S.E.'s vertex shader 0x7BE2142D for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c193_def = float4(0.5, 3, 1, 0);
static const float4 c194_def = float4(0.99609375, 0, 0.00390625, 0);
struct Input {
  float4 v0 : POSITION0;
  float4 v1 : BLENDINDICES0;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v1 = input.v1 * input_scale[1] + input_bias[1];
#else
  const float4 v0 = input.v0;
  const float4 v1 = input.v1;
#endif
  float4 r0 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xy = ((c193_def.xxxx + v1.yzzw)).xy;
  r0.zw = ((r0.xyxy - floor(r0.xyxy))).zw;
  r0.xy = (((-r0.zwzw) + r0)).xy;
  r0.xy = ((r0 * c193_def.yyyy)).xy;
  a0.xy = int4(round(r0)).xy;
  r0.x = (dot((c[0 + a0.x]).xyz, (v0).xyz).xxxx).x;
  o0.y = ((r0.xxxx + c[0 + a0.x].wwww)).y;
  r0.x = (dot((c[2 + a0.x]).xyz, (v0).xyz).xxxx).x;
  o0.z = ((r0.xxxx + c[2 + a0.x].wwww)).z;
  r0.x = (dot((c[1 + a0.x]).xyz, (v0).xyz).xxxx).x;
  r0.x = ((r0.xxxx + c[1 + a0.x].wwww)).x;
  o0.x = ((-r0.xxxx)).x;
  o0.w = (c193_def.zzzz).w;
  return o0;
}
