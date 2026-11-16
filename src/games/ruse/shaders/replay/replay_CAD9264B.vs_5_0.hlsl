// R.U.S.E.'s vertex shader 0xCAD9264B for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c205_def = float4(0.5, 3, 1, 0);
static const float4 c206_def = float4(0.99609375, 0, 0.00390625, 9.99999997e-07);
static const float4 c207_def = float4(-1, 1, 2, 750000);
static const float4 c208_def = float4(-0.00100000005, 0.00100000005, 1.44269502, -1.44269502);
struct Input {
  float4 v0 : POSITION0;
  float4 v2 : BLENDWEIGHT0;
  float4 v3 : BLENDINDICES0;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
  const float4 v2 = input.v2 * input_scale[2] + input_bias[2];
  const float4 v3 = input.v3 * input_scale[3] + input_bias[3];
#else
  const float4 v0 = input.v0;
  const float4 v2 = input.v2;
  const float4 v3 = input.v3;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  float4 r2 = 0;
  float4 r3 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyzw = ((c205_def.xxxx + v3)).xyzw;
  r1.xyzw = ((r0 - floor(r0))).xyzw;
  r0.xyzw = ((r0 + (-r1))).xyzw;
  r0.xyzw = ((r0 * c205_def.yyyy)).xyzw;
  a0.xyzw = int4(round(r0.yxzw)).xyzw;
  r1.xyzw = ((v0.xyzx * c205_def.zzzw + c205_def.wwwz)).xyzw;
  r2.x = (dot(c[0 + a0.x], r1).xxxx).x;
  r2.y = (dot(c[1 + a0.x], r1).xxxx).y;
  r2.z = (dot(c[2 + a0.x], r1).xxxx).z;
  r2.xyz = ((r2 * v2.yyyy)).xyz;
  r3.x = (dot(c[0 + a0.y], r1).xxxx).x;
  r3.y = (dot(c[1 + a0.y], r1).xxxx).y;
  r3.z = (dot(c[2 + a0.y], r1).xxxx).z;
  r2.xyz = ((r3 * v2.xxxx + r2)).xyz;
  r0.x = (dot(c[0 + a0.z], r1).xxxx).x;
  r0.y = (dot(c[1 + a0.z], r1).xxxx).y;
  r0.z = (dot(c[2 + a0.z], r1).xxxx).z;
  r0.xyz = ((r0 * v2.zzzz + r2)).xyz;
  r2.x = (dot(c[0 + a0.w], r1).xxxx).x;
  r2.y = (dot(c[1 + a0.w], r1).xxxx).y;
  r2.z = (dot(c[2 + a0.w], r1).xxxx).z;
  r0.xyz = ((r2 * v2.wwww + r0)).xyz;
  r0.w = (c205_def.zzzz).w;
  r1.x = (dot(c[200], r0).xxxx).x;
  r1.y = (dot(c[201], r0).xxxx).y;
  r1.z = (dot(c[202], r0).xxxx).z;
  r1.w = (c205_def.zzzz).w;
  o0.x = (dot(c[192], r1).xxxx).x;
  o0.y = (dot(c[193], r1).xxxx).y;
  o0.z = (dot(c[194], r1).xxxx).z;
  o0.w = (dot(c[195], r1).xxxx).w;
  return o0;
}
