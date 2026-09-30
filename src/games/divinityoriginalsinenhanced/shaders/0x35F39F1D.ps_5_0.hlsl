// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 29 11:18:11 2026

cbuffer PerView : register(b12)
{
  row_major float4x4 global_View : packoffset(c0);
  row_major float4x4 global_Projection : packoffset(c4);
  row_major float4x4 global_ViewProjection : packoffset(c8);
  float4 global_ViewPos : packoffset(c12);
  float4 global_ViewInfo : packoffset(c13);
}

SamplerState LinearSampler_s : register(s0);
Texture2D<float4> Base : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  float4 v2 : TexCoord1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = Base.SampleLevel(LinearSampler_s, v1.xy, 0).xyz;
  r1.xyz = Base.Gather(LinearSampler_s, v1.xy).xyz;
  r2.xyz = Base.Gather(LinearSampler_s, v1.xy, int2(-1, -1)).xzw;
  r0.w = max(r1.x, r0.y);
  r1.w = min(r1.x, r0.y);
  r0.w = max(r1.z, r0.w);
  r1.w = min(r1.z, r1.w);
  r2.w = max(r2.y, r2.x);
  r3.x = min(r2.y, r2.x);
  r0.w = max(r2.w, r0.w);
  r1.w = min(r3.x, r1.w);
  r2.w = 0.166666672 * r0.w;
  r0.w = -r1.w + r0.w;
  r1.w = max(0, r2.w);
  r1.w = cmp(r0.w >= r1.w);
  if (r1.w != 0) {
    r3.xy = float2(1,1) / global_ViewInfo.zw;
    r1.w = Base.SampleLevel(LinearSampler_s, v1.xy, 0, int2(1, -1)).y;
    r2.w = Base.SampleLevel(LinearSampler_s, v1.xy, 0, int2(-1, 1)).y;
    r3.zw = r2.yx + r1.xz;
    r0.w = 1 / r0.w;
    r4.x = r3.z + r3.w;
    r3.zw = r0.yy * float2(-2,-2) + r3.zw;
    r4.y = r1.w + r1.y;
    r1.w = r2.z + r1.w;
    r4.z = r1.z * -2 + r4.y;
    r1.w = r2.y * -2 + r1.w;
    r2.z = r2.z + r2.w;
    r1.y = r2.w + r1.y;
    r2.w = abs(r3.z) * 2 + abs(r4.z);
    r1.w = abs(r3.w) * 2 + abs(r1.w);
    r3.z = r2.x * -2 + r2.z;
    r1.y = r1.x * -2 + r1.y;
    r2.w = abs(r3.z) + r2.w;
    r1.y = abs(r1.y) + r1.w;
    r1.w = r2.z + r4.y;
    r1.y = cmp(r2.w >= r1.y);
    r1.w = r4.x * 2 + r1.w;
    r2.x = r1.y ? r2.y : r2.x;
    r1.x = r1.y ? r1.x : r1.z;
    r1.z = r1.y ? r3.y : r3.x;
    r1.w = r1.w * 0.0833333358 + -r0.y;
    r2.y = r2.x + -r0.y;
    r2.z = r1.x + -r0.y;
    r2.x = r2.x + r0.y;
    r1.x = r1.x + r0.y;
    r2.w = cmp(abs(r2.y) >= abs(r2.z));
    r2.y = max(abs(r2.y), abs(r2.z));
    r1.z = r2.w ? -r1.z : r1.z;
    r0.w = saturate(abs(r1.w) * r0.w);
    r1.w = r1.y ? r3.x : 0;
    r2.z = r1.y ? 0 : r3.y;
    r3.xy = r1.zz * float2(0.5,0.5) + v1.xy;
    r3.x = r1.y ? v1.x : r3.x;
    r3.y = r1.y ? r3.y : v1.y;
    r4.x = r3.x + -r1.w;
    r4.y = r3.y + -r2.z;
    r5.x = r3.x + r1.w;
    r5.y = r3.y + r2.z;
    r3.x = r0.w * -2 + 3;
    r3.y = Base.SampleLevel(LinearSampler_s, r4.xy, 0).y;
    r0.w = r0.w * r0.w;
    r3.z = Base.SampleLevel(LinearSampler_s, r5.xy, 0).y;
    r1.x = r2.w ? r2.x : r1.x;
    r2.x = 0.25 * r2.y;
    r2.y = -r1.x * 0.5 + r0.y;
    r0.w = r3.x * r0.w;
    r2.y = cmp(r2.y < 0);
    r3.x = -r1.x * 0.5 + r3.y;
    r3.y = -r1.x * 0.5 + r3.z;
    r3.zw = cmp(abs(r3.xy) >= r2.xx);
    r2.w = -r1.w * 1.5 + r4.x;
    r4.x = r3.z ? r4.x : r2.w;
    r2.w = -r2.z * 1.5 + r4.y;
    r4.z = r3.z ? r4.y : r2.w;
    r4.yw = ~(int2)r3.zw;
    r2.w = (int)r4.w | (int)r4.y;
    r4.y = r1.w * 1.5 + r5.x;
    r4.y = r3.w ? r5.x : r4.y;
    r5.x = r2.z * 1.5 + r5.y;
    r4.w = r3.w ? r5.y : r5.x;
    if (r2.w != 0) {
      if (r3.z == 0) {
        r3.x = Base.SampleLevel(LinearSampler_s, r4.xz, 0).y;
      }
      if (r3.w == 0) {
        r3.y = Base.SampleLevel(LinearSampler_s, r4.yw, 0).y;
      }
      r2.w = -r1.x * 0.5 + r3.x;
      r3.x = r3.z ? r3.x : r2.w;
      r2.w = -r1.x * 0.5 + r3.y;
      r3.y = r3.w ? r3.y : r2.w;
      r3.zw = cmp(abs(r3.xy) >= r2.xx);
      r2.w = -r1.w * 2 + r4.x;
      r4.x = r3.z ? r4.x : r2.w;
      r2.w = -r2.z * 2 + r4.z;
      r4.z = r3.z ? r4.z : r2.w;
      r5.xy = ~(int2)r3.zw;
      r2.w = (int)r5.y | (int)r5.x;
      r5.x = r1.w * 2 + r4.y;
      r4.y = r3.w ? r4.y : r5.x;
      r5.x = r2.z * 2 + r4.w;
      r4.w = r3.w ? r4.w : r5.x;
      if (r2.w != 0) {
        if (r3.z == 0) {
          r3.x = Base.SampleLevel(LinearSampler_s, r4.xz, 0).y;
        }
        if (r3.w == 0) {
          r3.y = Base.SampleLevel(LinearSampler_s, r4.yw, 0).y;
        }
        r2.w = -r1.x * 0.5 + r3.x;
        r3.x = r3.z ? r3.x : r2.w;
        r2.w = -r1.x * 0.5 + r3.y;
        r3.y = r3.w ? r3.y : r2.w;
        r3.zw = cmp(abs(r3.xy) >= r2.xx);
        r2.w = -r1.w * 4 + r4.x;
        r4.x = r3.z ? r4.x : r2.w;
        r2.w = -r2.z * 4 + r4.z;
        r4.z = r3.z ? r4.z : r2.w;
        r5.xy = ~(int2)r3.zw;
        r2.w = (int)r5.y | (int)r5.x;
        r5.x = r1.w * 4 + r4.y;
        r4.y = r3.w ? r4.y : r5.x;
        r5.x = r2.z * 4 + r4.w;
        r4.w = r3.w ? r4.w : r5.x;
        if (r2.w != 0) {
          if (r3.z == 0) {
            r3.x = Base.SampleLevel(LinearSampler_s, r4.xz, 0).y;
          }
          if (r3.w == 0) {
            r3.y = Base.SampleLevel(LinearSampler_s, r4.yw, 0).y;
          }
          r2.w = -r1.x * 0.5 + r3.x;
          r3.x = r3.z ? r3.x : r2.w;
          r1.x = -r1.x * 0.5 + r3.y;
          r3.y = r3.w ? r3.y : r1.x;
          r2.xw = cmp(abs(r3.xy) >= r2.xx);
          r1.x = -r1.w * 12 + r4.x;
          r4.x = r2.x ? r4.x : r1.x;
          r1.x = -r2.z * 12 + r4.z;
          r4.z = r2.x ? r4.z : r1.x;
          r1.x = r1.w * 12 + r4.y;
          r4.y = r2.w ? r4.y : r1.x;
          r1.x = r2.z * 12 + r4.w;
          r4.w = r2.w ? r4.w : r1.x;
        }
      }
    }
    r1.x = v1.x + -r4.x;
    r1.w = -v1.x + r4.y;
    r2.x = v1.y + -r4.z;
    r1.x = r1.y ? r1.x : r2.x;
    r2.x = -v1.y + r4.w;
    r1.w = r1.y ? r1.w : r2.x;
    r2.xz = cmp(r3.xy < float2(0,0));
    r2.w = r1.w + r1.x;
    r2.xy = cmp((int2)r2.xz != (int2)r2.yy);
    r2.z = 1 / r2.w;
    r2.w = cmp(r1.x < r1.w);
    r1.x = min(r1.x, r1.w);
    r1.w = r2.w ? r2.x : r2.y;
    r0.w = r0.w * r0.w;
    r1.x = r1.x * -r2.z + 0.5;
    r0.w = 0.75 * r0.w;
    r1.x = (int)r1.x & (int)r1.w;
    r0.w = max(r1.x, r0.w);
    r1.xz = r0.ww * r1.zz + v1.xy;
    r2.x = r1.y ? v1.x : r1.x;
    r2.y = r1.y ? r1.z : v1.y;
    r0.xyz = Base.SampleLevel(LinearSampler_s, r2.xy, 0).xyz;
  }
  o0.xyz = r0.xyz;
  o0.w = 1;
  return;
}