// R.U.S.E.'s vertex shader 0x843001EB for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c195_def = float4(0.5, 3, 1, 0);
static const float4 c196_def = float4(750000, 9.99999997e-07, -0.00100000005, 0.00100000005);
static const float4 c197_def = float4(1.44269502, -1.44269502, 0, 0);
static const float4 c198_def = float4(0.99609375, 0, 0.00390625, 0);
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
  float4 r1 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyz = ((c195_def.xxxx + v1)).xyz;
  r1.xyz = ((r0 - floor(r0))).xyz;
  r0.xyz = ((r0 + (-r1))).xyz;
  r0.xyz = ((r0 * c195_def.yyyy)).xyz;
  a0.x = int4(round(r0.yyyy)).x;
  r0.y = (dot((c[0 + a0.x]).xyz, (v0).xyz).xxxx).y;
  o0.y = ((r0.yyyy + c[0 + a0.x].wwww)).y;
  r0.y = (dot((c[2 + a0.x]).xyz, (v0).xyz).xxxx).y;
  o0.z = ((r0.yyyy + c[2 + a0.x].wwww)).z;
  r0.y = (dot((c[1 + a0.x]).xyz, (v0).xyz).xxxx).y;
  r0.y = ((r0.yyyy + c[1 + a0.x].wwww)).y;
  o0.x = ((-r0.yyyy)).x;
  o0.w = (c195_def.zzzz).w;
  return o0;
}
