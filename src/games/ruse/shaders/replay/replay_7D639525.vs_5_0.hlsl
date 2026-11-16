// R.U.S.E.'s vertex shader 0x7D639525 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c6_def = float4(3.05185094e-05, 1, -1, 0);
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
  float4 r2 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyz = ((c6_def.xxxx * v0)).xyz;
  r0.xyz = ((r0 * c[5])).xyz;
  r1.xyz = ((v0 * (-c6_def.xxxx) + c6_def.yyyy)).xyz;
  r0.xyz = ((r1 * c[4] + r0)).xyz;
  r0.w = (dot((c[2]).xyz, (r0).xyz).xxxx).w;
  r0.w = ((r0.wwww + c[2].wwww)).w;
  r1.x = ((c[3].wwww + c[3].wwww)).x;
  r1.x = ((1.0f / (r1.xxxx).x).xxxx).x;
  r1.y = ((r0.wwww * r1.xxxx)).y;
  o0.z = ((r1.yyyy + r1.yyyy)).z;
  r1.yz = (c6_def).yz;
  r1.yz = ((r1.xzyw + c[3].wwww)).yz;
  r1.xy = ((r1.xxxx * r1.yzzw)).xy;
  r0.w = (dot((c[0]).xyz, (r0).xyz).xxxx).w;
  r0.x = (dot((c[1]).xyz, (r0).xyz).xxxx).x;
  r0.x = ((r0.xxxx + c[1].wwww)).x;
  r2.x = ((-r0.xxxx)).x;
  r0.x = ((r0.wwww + c[0].wwww)).x;
  r2.yw = ((r0.xxxx * r1.xyzx + r1.xxzy)).yw;
  o0.xyw = (r2).xyw;
  return o0;
}
