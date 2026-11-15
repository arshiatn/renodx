// Spark motion (DLSS addon): the game's sprite particle vertex shader 0xF885F191 (3Dmigoto decompile, its sprite
// placement unchanged) outputs only its clip position and, for the particle motion pass, the clip position its corner
// had last frame in this frame's camera. Made by tools\toa\spark_motion\make_motion_vs.py.
Texture3D<float4> t1 : register(t1);

Buffer<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb4 : register(b4)
{
  float4 cb4[23];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[10];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[4];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[124];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[6];
}




#define cmp -


void main(
  float4 v0 : ATTRIBUTE0,
  float4 v1 : ATTRIBUTE1,
  float4 v2 : ATTRIBUTE2,
  float4 v3 : ATTRIBUTE3,
  float4 v4 : ATTRIBUTE5,
  float2 v5 : ATTRIBUTE4,
  uint v6 : SV_VertexID0,
  out float4 motion_position : SV_Position,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_old_clip : TEXCOORD1)
{
  float4 o0, o1, o2, o3, o4, o5, o6;
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = cb2[1].xyz * v0.yyy;
  r0.xyz = v0.xxx * cb2[0].xyz + r0.xyz;
  r0.xyz = v0.zzz * cb2[2].xyz + r0.xyz;
  r0.xyz = cb2[3].xyz + r0.xyz;
  r1.xyz = cb2[1].zxy * v1.yyy;
  r1.xyz = v1.xxx * cb2[0].zxy + r1.xyz;
  r1.xyz = v1.zzz * cb2[2].zxy + r1.xyz;
  r1.xyz = cb2[3].zxy + r1.xyz;
  r2.xyz = cb1[56].zxy + -cb1[54].zxy;
  r2.xyz = cb3[6].www * r2.xyz + cb1[54].zxy;
  r3.xyz = cb1[55].zxy + -cb1[53].zxy;
  r3.xyz = cb3[6].www * r3.xyz + cb1[53].zxy;
  r4.xyz = cb3[0].zxy + -r2.xyz;
  r2.xyz = cb3[0].www * r4.xyz + r2.xyz;
  r4.xyz = cb3[1].zxy + r3.xyz;
  r3.xyz = cb3[1].www * r4.xyz + -r3.xyz;
  r4.xyz = cb1[59].yzx + -r0.yzx;
  r0.w = dot(r4.xyz, r4.xyz);
  r1.w = max(0.00999999978, r0.w);
  r1.w = rsqrt(r1.w);
  r4.xyz = r4.xyz * r1.www;
  r1.xyz = -r1.xyz + r0.zxy;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = max(0.00999999978, r1.w);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.xyz * r1.www;
  r5.xyz = r4.xyz * r1.yzx;
  r5.xyz = r4.zxy * r1.zxy + -r5.xyz;
  r1.w = dot(r5.xyz, r5.xyz);
  r1.w = max(0.00999999978, r1.w);
  r1.w = rsqrt(r1.w);
  r5.xyz = r5.xyz * r1.www;
  r6.xyz = cb3[0].yzx * r4.zxy;
  r6.xyz = cb3[0].xyz * r4.xyz + -r6.xyz;
  r1.w = dot(r6.xyz, r6.xyz);
  r1.w = max(0.00999999978, r1.w);
  r1.w = rsqrt(r1.w);
  r6.xyz = r6.xyz * r1.www;
  r7.xyz = float3(0,0,1) * r4.xyz;
  r7.xyz = r4.zxy * float3(0,1,0) + -r7.xyz;
  r1.w = dot(r7.yz, r7.yz);
  r1.w = max(0.00999999978, r1.w);
  r1.w = rsqrt(r1.w);
  r7.xyz = r7.xyz * r1.www;
  r8.xyz = r7.yzx * r4.xyz;
  r4.xyz = r4.zxy * r7.zxy + -r8.xyz;
  r1.w = cmp(0 < cb3[6].x);
  if (r1.w != 0) {
    r0.w = saturate(r0.w * cb3[6].y + -cb3[6].z);
    r1.w = 1 + -r0.w;
  } else {
    r1.w = cb3[2].x;
    r0.w = cb3[2].w;
  }
  r5.xyz = cb3[2].yyy * r5.xyz;
  r2.xyz = r2.xyz * r1.www + r5.xyz;
  r2.xyz = cb3[0].zxy * cb3[2].zzz + r2.xyz;
  r2.xyz = r7.xyz * r0.www + r2.xyz;
  r1.xyz = cb3[2].yyy * -r1.xyz;
  r1.xyz = r3.xyz * r1.www + r1.xyz;
  r1.xyz = -r6.xyz * cb3[2].zzz + r1.xyz;
  r1.xyz = r4.xyz * r0.www + r1.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r0.w = v2.z * cb3[8].x + cb3[8].y;
  sincos(r0.w, r3.x, r4.x);
  r3.yzw = r4.xxx * r2.yzx;
  r5.xyz = r3.xxx * r1.yzx + r3.yzw;
  r2.xyz = r3.xxx * r2.xyz;
  r1.xyz = r4.xxx * r1.xyz + -r2.xyz;
  r2.xy = cmp(float2(0,0) < v2.xy);
  r2.zw = cmp(v2.xy < float2(0,0));
  r2.xy = (int2)-r2.xy + (int2)r2.zw;
  r2.xy = (int2)r2.xy;
  if (asuint(cb0[3].x) != 0u) {
    r0.w = (uint)v6.x;
    r1.w = asuint(cb0[3].x);
    r0.w = r0.w / r1.w;
    r2.z = cmp(r0.w >= -r0.w);
    r0.w = frac(r0.w);
    r0.w = r2.z ? r0.w : -r0.w;
    r0.w = r0.w * r1.w;
    r0.w = (uint)r0.w;
    r1.w = cb3[5].x * cb3[5].y;
    r2.z = v2.w / r1.w;
    r2.w = cmp(r2.z >= -r2.z);
    r2.z = frac(abs(r2.z));
    r2.z = r2.w ? r2.z : -r2.z;
    r1.w = r2.z * r1.w;
    r1.w = (uint)r1.w;
    r2.z = r2.x * r2.y;
    r2.z = cmp(r2.z < 0);
    r2.w = asint(cb0[3].x) + -1;
    r2.w = (int)-r0.w + (int)r2.w;
    r0.w = r2.z ? r2.w : r0.w;
    r0.w = mad((int)r1.w, asint(cb0[3].x), (int)r0.w);
    r2.zw = t0.Load(r0.w).xy;
    r3.xy = cmp(r2.xy < float2(0,0));
    r3.zw = float2(1,1) + -r2.zw;
    r3.xy = r3.xy ? r3.zw : r2.zw;
  } else {
    r2.xy = cmp(r2.xy < float2(0,0));
    r3.zw = float2(1,1) + -v5.xy;
    r2.zw = r2.xy ? r3.zw : v5.xy;
    r3.xy = v5.xy;
  }
  r4.xyz = cb1[62].xyz + r0.xyz;
  r2.xy = cb3[9].xy + r3.xy;
  r2.xy = abs(v2.xy) * r2.xy;
  r3.xyz = r2.xxx * r5.xyz + r4.xyz;
  r3.xyz = r2.yyy * r1.yzx + r3.xyz;
  r5.w = frac(v2.w);
  r0.w = v2.w + -r5.w;
  r1.w = r0.w / cb3[5].x;
  r2.x = cmp(r1.w >= -r1.w);
  r1.w = frac(abs(r1.w));
  r1.w = r2.x ? r1.w : -r1.w;
  r2.x = cb3[5].x * r1.w;
  r0.w = cb3[5].z * r0.w;
  r2.y = floor(r0.w);
  r2.xy = r2.xy + r2.zw;
  o4.xy = cb3[5].zw * r2.xy;
  r2.xyz = r5.zxy * r1.zxy;
  r2.xyz = r5.yzx * r1.xyz + -r2.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r4.xy = cmp(cb3[8].zz < float2(0.5,1.5));
  r6.xyz = -cb3[3].xyz + r0.xyz;
  r0.w = dot(r6.xyz, r6.xyz);
  r0.w = rsqrt(r0.w);
  r7.xyz = r6.xyz * r0.www;
  r0.w = dot(cb3[4].xyz, r6.xyz);
  r6.xyz = r0.www * cb3[4].xyz + cb3[3].xyz;
  r0.xyz = -r6.xyz + r0.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = r4.yyy ? r7.xyz : r0.xyz;
  r0.xyz = r4.xxx ? r2.xyz : r0.xyz;
  r2.xyz = -cb1[62].xyz + r3.xyz;
  r2.xyz = cb1[59].xyz + -r2.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = sqrt(r0.w);
  r2.xyz = r2.xyz / r0.www;
  r2.xyz = r2.xyz * cb4[22].xxx + r3.xyz;
  r3.xyzw = cb1[1].xyzw * r2.yyyy;
  r3.xyzw = r2.xxxx * cb1[0].xyzw + r3.xyzw;
  r3.xyzw = r2.zzzz * cb1[2].xyzw + r3.xyzw;
  r3.xyzw = cb1[3].xyzw + r3.xyzw;
  r4.xyz = r1.xyz * r0.yzx;
  r1.xyz = r1.zxy * r0.zxy + -r4.xyz;
  r0.w = dot(r1.xyz, r5.xyz);
  r1.x = cmp(0 < r0.w);
  r0.w = cmp(r0.w < 0);
  r0.w = (int)-r1.x + (int)r0.w;
  o1.w = (int)r0.w;
  r0.w = cmp(cb0[5].x != 0.000000);
  if (r0.w != 0) {
    r1.xy = r3.xy / r3.ww;
    r1.xy = r1.xy * float2(0.5,-0.5) + float2(0.5,0.5);
    r1.xy = cb1[122].xy * r1.xy;
    r1.xy = cb1[123].zw * r1.xy;
    r0.w = dot(r2.xyz, r2.xyz);
    r0.w = sqrt(r0.w);
    r1.z = r0.w / cb0[4].z;
    r1.xyzw = t1.SampleLevel(s0_s, r1.xyz, 0).xyzw;
    r0.w = -cb0[4].z + r0.w;
    r2.x = cb0[4].w + -cb0[4].z;
    r0.w = saturate(r0.w / r2.x);
    r2.xyzw = cb0[5].yyyz * r1.xyzw + -r1.xyzw;
    o5.xyzw = r0.wwww * r2.xyzw + r1.xyzw;
  } else {
    o5.xyzw = float4(0,0,0,1);
  }
  o0.xyzw = r5.xyzw;
  o1.xyz = r0.xyz;
  o2.xyzw = v4.xyzw;
  o3.xyzw = v3.xyzw;
  o4.zw = float2(0,0);
  o6.xyzw = r3.xyzw;
  // The particle moved by (Position - OldPosition) through LocalToWorld since last frame: its corner then, seen
  // by this frame's camera (TranslatedWorldToClip rows; a direction, no translation).
  const float3 motion = (v0.x - v1.x) * cb2[0].xyz + (v0.y - v1.y) * cb2[1].xyz + (v0.z - v1.z) * cb2[2].xyz;
  motion_position = o6;
  motion_clip = o6;
  motion_old_clip = o6 - (motion.x * cb1[0] + motion.y * cb1[1] + motion.z * cb1[2]);
  return;
}
