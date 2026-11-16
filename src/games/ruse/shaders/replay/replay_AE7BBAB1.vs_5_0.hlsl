// R.U.S.E.'s vertex shader 0xAE7BBAB1 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c4_def = float4(0.99609375, 0, 0.00390625, 1);
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
  r0.x = (dot((c[1]).xyz, (v0).xyz).xxxx).x;
  r0.x = ((r0.xxxx + c[1].wwww)).x;
  o0.x = ((-r0.xxxx)).x;
  r0.x = (dot((c[0]).xyz, (v0).xyz).xxxx).x;
  r0.x = ((r0.xxxx + c[0].wwww)).x;
  r0.y = (dot((c[2]).xyz, (v0).xyz).xxxx).y;
  r0.z = ((r0.yyyy + c[2].wwww)).z;
  o0.yz = (r0.xxzw).yz;
  o0.w = (c4_def.wwww).w;
  return o0;
}
