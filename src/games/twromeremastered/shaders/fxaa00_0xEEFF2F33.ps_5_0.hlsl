// ---- Created with 3Dmigoto v1.3.16 on Thu Sep 24 16:15:48 2026
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[2];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = asuint(cb0[0].xy);
  r0.xy = float2(1,1) / r0.xy;
  r0.zw = v0.xy * r0.xy;
  r1.xyzw = t0.SampleLevel(s0_s, r0.zw, 0).xyzw;
  r2.xyz = t0.Gather(s0_s, r0.zw).xyz;
  r3.xyz = t0.Gather(s0_s, r0.zw, int2(-1, -1)).xzw;
  r2.w = max(r2.x, r1.w);
  r3.w = min(r2.x, r1.w);
  r2.w = max(r2.z, r2.w);
  r3.w = min(r3.w, r2.z);
  r4.x = max(r3.y, r3.x);
  r4.y = min(r3.y, r3.x);
  r2.w = max(r4.x, r2.w);
  r3.w = min(r4.y, r3.w);
  r4.x = cb0[1].y * r2.w;
  r2.w = -r3.w + r2.w;
  r3.w = max(cb0[1].z, r4.x);
  r3.w = cmp(r2.w >= r3.w);
  if (r3.w != 0) {
    r3.w = t0.SampleLevel(s0_s, r0.zw, 0, int2(1, -1)).w;
    r4.x = t0.SampleLevel(s0_s, r0.zw, 0, int2(-1, 1)).w;
    r4.yz = r3.yx + r2.xz;
    r2.w = 1 / r2.w;
    r4.w = r4.y + r4.z;
    r4.yz = r1.ww * float2(-2,-2) + r4.yz;
    r5.x = r3.w + r2.y;
    r3.w = r3.z + r3.w;
    r5.y = r2.z * -2 + r5.x;
    r3.w = r3.y * -2 + r3.w;
    r3.z = r4.x + r3.z;
    r2.y = r4.x + r2.y;
    r4.x = abs(r4.y) * 2 + abs(r5.y);
    r3.w = abs(r4.z) * 2 + abs(r3.w);
    r4.y = r3.x * -2 + r3.z;
    r2.y = r2.x * -2 + r2.y;
    r4.x = abs(r4.y) + r4.x;
    r2.y = abs(r2.y) + r3.w;
    r3.z = r3.z + r5.x;
    r2.y = cmp(r4.x >= r2.y);
    r3.z = r4.w * 2 + r3.z;
    r3.x = r2.y ? r3.y : r3.x;
    r2.x = r2.y ? r2.x : r2.z;
    r2.z = r2.y ? r0.y : r0.x;
    r3.y = r3.z * 0.0833333358 + -r1.w;
    r3.z = r3.x + -r1.w;
    r3.w = r2.x + -r1.w;
    r3.x = r3.x + r1.w;
    r2.x = r2.x + r1.w;
    r4.x = cmp(abs(r3.z) >= abs(r3.w));
    r3.z = max(abs(r3.z), abs(r3.w));
    r2.z = r4.x ? -r2.z : r2.z;
    r2.w = saturate(abs(r3.y) * r2.w);
    r3.y = r2.y ? r0.x : 0;
    r3.w = r2.y ? 0 : r0.y;
    r4.yz = r2.zz * float2(0.5,0.5) + r0.zw;
    r4.y = r2.y ? r0.z : r4.y;
    r4.z = r2.y ? r4.z : r0.w;
    r5.xy = r4.yz + -r3.yw;
    r6.xy = r4.yz + r3.yw;
    r4.y = r2.w * -2 + 3;
    r4.z = t0.SampleLevel(s0_s, r5.xy, 0).w;
    r2.w = r2.w * r2.w;
    r4.w = t0.SampleLevel(s0_s, r6.xy, 0).w;
    r2.x = r4.x ? r3.x : r2.x;
    r3.x = 0.25 * r3.z;
    r1.w = -r2.x * 0.5 + r1.w;
    r2.w = r4.y * r2.w;
    r1.w = cmp(r1.w < 0);
    r4.x = -r2.x * 0.5 + r4.z;
    r4.y = -r2.x * 0.5 + r4.w;
    r4.zw = cmp(abs(r4.xy) >= r3.xx);
    r3.z = -r3.y * 1.5 + r5.x;
    r5.x = r4.z ? r5.x : r3.z;
    r3.z = -r3.w * 1.5 + r5.y;
    r5.z = r4.z ? r5.y : r3.z;
    r5.yw = ~(int2)r4.zw;
    r3.z = (int)r5.w | (int)r5.y;
    r5.y = r3.y * 1.5 + r6.x;
    r5.y = r4.w ? r6.x : r5.y;
    r6.x = r3.w * 1.5 + r6.y;
    r5.w = r4.w ? r6.y : r6.x;
    if (r3.z != 0) {
      if (r4.z == 0) {
        r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
      }
      if (r4.w == 0) {
        r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
      }
      r3.z = -r2.x * 0.5 + r4.x;
      r4.x = r4.z ? r4.x : r3.z;
      r3.z = -r2.x * 0.5 + r4.y;
      r4.y = r4.w ? r4.y : r3.z;
      r4.zw = cmp(abs(r4.xy) >= r3.xx);
      r3.z = -r3.y * 2 + r5.x;
      r5.x = r4.z ? r5.x : r3.z;
      r3.z = -r3.w * 2 + r5.z;
      r5.z = r4.z ? r5.z : r3.z;
      r6.xy = ~(int2)r4.zw;
      r3.z = (int)r6.y | (int)r6.x;
      r6.x = r3.y * 2 + r5.y;
      r5.y = r4.w ? r5.y : r6.x;
      r6.x = r3.w * 2 + r5.w;
      r5.w = r4.w ? r5.w : r6.x;
      if (r3.z != 0) {
        if (r4.z == 0) {
          r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
        }
        if (r4.w == 0) {
          r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
        }
        r3.z = -r2.x * 0.5 + r4.x;
        r4.x = r4.z ? r4.x : r3.z;
        r3.z = -r2.x * 0.5 + r4.y;
        r4.y = r4.w ? r4.y : r3.z;
        r4.zw = cmp(abs(r4.xy) >= r3.xx);
        r3.z = -r3.y * 2 + r5.x;
        r5.x = r4.z ? r5.x : r3.z;
        r3.z = -r3.w * 2 + r5.z;
        r5.z = r4.z ? r5.z : r3.z;
        r6.xy = ~(int2)r4.zw;
        r3.z = (int)r6.y | (int)r6.x;
        r6.x = r3.y * 2 + r5.y;
        r5.y = r4.w ? r5.y : r6.x;
        r6.x = r3.w * 2 + r5.w;
        r5.w = r4.w ? r5.w : r6.x;
        if (r3.z != 0) {
          if (r4.z == 0) {
            r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
          }
          if (r4.w == 0) {
            r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
          }
          r3.z = -r2.x * 0.5 + r4.x;
          r4.x = r4.z ? r4.x : r3.z;
          r3.z = -r2.x * 0.5 + r4.y;
          r4.y = r4.w ? r4.y : r3.z;
          r4.zw = cmp(abs(r4.xy) >= r3.xx);
          r3.z = -r3.y * 2 + r5.x;
          r5.x = r4.z ? r5.x : r3.z;
          r3.z = -r3.w * 2 + r5.z;
          r5.z = r4.z ? r5.z : r3.z;
          r6.xy = ~(int2)r4.zw;
          r3.z = (int)r6.y | (int)r6.x;
          r6.x = r3.y * 2 + r5.y;
          r5.y = r4.w ? r5.y : r6.x;
          r6.x = r3.w * 2 + r5.w;
          r5.w = r4.w ? r5.w : r6.x;
          if (r3.z != 0) {
            if (r4.z == 0) {
              r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
            }
            if (r4.w == 0) {
              r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
            }
            r3.z = -r2.x * 0.5 + r4.x;
            r4.x = r4.z ? r4.x : r3.z;
            r3.z = -r2.x * 0.5 + r4.y;
            r4.y = r4.w ? r4.y : r3.z;
            r4.zw = cmp(abs(r4.xy) >= r3.xx);
            r3.z = -r3.y * 2 + r5.x;
            r5.x = r4.z ? r5.x : r3.z;
            r3.z = -r3.w * 2 + r5.z;
            r5.z = r4.z ? r5.z : r3.z;
            r6.xy = ~(int2)r4.zw;
            r3.z = (int)r6.y | (int)r6.x;
            r6.x = r3.y * 2 + r5.y;
            r5.y = r4.w ? r5.y : r6.x;
            r6.x = r3.w * 2 + r5.w;
            r5.w = r4.w ? r5.w : r6.x;
            if (r3.z != 0) {
              if (r4.z == 0) {
                r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
              }
              if (r4.w == 0) {
                r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
              }
              r3.z = -r2.x * 0.5 + r4.x;
              r4.x = r4.z ? r4.x : r3.z;
              r3.z = -r2.x * 0.5 + r4.y;
              r4.y = r4.w ? r4.y : r3.z;
              r4.zw = cmp(abs(r4.xy) >= r3.xx);
              r3.z = -r3.y * 2 + r5.x;
              r5.x = r4.z ? r5.x : r3.z;
              r3.z = -r3.w * 2 + r5.z;
              r5.z = r4.z ? r5.z : r3.z;
              r6.xy = ~(int2)r4.zw;
              r3.z = (int)r6.y | (int)r6.x;
              r6.x = r3.y * 2 + r5.y;
              r5.y = r4.w ? r5.y : r6.x;
              r6.x = r3.w * 2 + r5.w;
              r5.w = r4.w ? r5.w : r6.x;
              if (r3.z != 0) {
                if (r4.z == 0) {
                  r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
                }
                if (r4.w == 0) {
                  r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
                }
                r3.z = -r2.x * 0.5 + r4.x;
                r4.x = r4.z ? r4.x : r3.z;
                r3.z = -r2.x * 0.5 + r4.y;
                r4.y = r4.w ? r4.y : r3.z;
                r4.zw = cmp(abs(r4.xy) >= r3.xx);
                r3.z = -r3.y * 2 + r5.x;
                r5.x = r4.z ? r5.x : r3.z;
                r3.z = -r3.w * 2 + r5.z;
                r5.z = r4.z ? r5.z : r3.z;
                r6.xy = ~(int2)r4.zw;
                r3.z = (int)r6.y | (int)r6.x;
                r6.x = r3.y * 2 + r5.y;
                r5.y = r4.w ? r5.y : r6.x;
                r6.x = r3.w * 2 + r5.w;
                r5.w = r4.w ? r5.w : r6.x;
                if (r3.z != 0) {
                  if (r4.z == 0) {
                    r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
                  }
                  if (r4.w == 0) {
                    r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
                  }
                  r3.z = -r2.x * 0.5 + r4.x;
                  r4.x = r4.z ? r4.x : r3.z;
                  r3.z = -r2.x * 0.5 + r4.y;
                  r4.y = r4.w ? r4.y : r3.z;
                  r4.zw = cmp(abs(r4.xy) >= r3.xx);
                  r3.z = -r3.y * 2 + r5.x;
                  r5.x = r4.z ? r5.x : r3.z;
                  r3.z = -r3.w * 2 + r5.z;
                  r5.z = r4.z ? r5.z : r3.z;
                  r6.xy = ~(int2)r4.zw;
                  r3.z = (int)r6.y | (int)r6.x;
                  r6.x = r3.y * 2 + r5.y;
                  r5.y = r4.w ? r5.y : r6.x;
                  r6.x = r3.w * 2 + r5.w;
                  r5.w = r4.w ? r5.w : r6.x;
                  if (r3.z != 0) {
                    if (r4.z == 0) {
                      r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
                    }
                    if (r4.w == 0) {
                      r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
                    }
                    r3.z = -r2.x * 0.5 + r4.x;
                    r4.x = r4.z ? r4.x : r3.z;
                    r3.z = -r2.x * 0.5 + r4.y;
                    r4.y = r4.w ? r4.y : r3.z;
                    r4.zw = cmp(abs(r4.xy) >= r3.xx);
                    r3.z = -r3.y * 2 + r5.x;
                    r5.x = r4.z ? r5.x : r3.z;
                    r3.z = -r3.w * 2 + r5.z;
                    r5.z = r4.z ? r5.z : r3.z;
                    r6.xy = ~(int2)r4.zw;
                    r3.z = (int)r6.y | (int)r6.x;
                    r6.x = r3.y * 2 + r5.y;
                    r5.y = r4.w ? r5.y : r6.x;
                    r6.x = r3.w * 2 + r5.w;
                    r5.w = r4.w ? r5.w : r6.x;
                    if (r3.z != 0) {
                      if (r4.z == 0) {
                        r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
                      }
                      if (r4.w == 0) {
                        r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
                      }
                      r3.z = -r2.x * 0.5 + r4.x;
                      r4.x = r4.z ? r4.x : r3.z;
                      r3.z = -r2.x * 0.5 + r4.y;
                      r4.y = r4.w ? r4.y : r3.z;
                      r4.zw = cmp(abs(r4.xy) >= r3.xx);
                      r3.z = -r3.y * 4 + r5.x;
                      r5.x = r4.z ? r5.x : r3.z;
                      r3.z = -r3.w * 4 + r5.z;
                      r5.z = r4.z ? r5.z : r3.z;
                      r6.xy = ~(int2)r4.zw;
                      r3.z = (int)r6.y | (int)r6.x;
                      r6.x = r3.y * 4 + r5.y;
                      r5.y = r4.w ? r5.y : r6.x;
                      r6.x = r3.w * 4 + r5.w;
                      r5.w = r4.w ? r5.w : r6.x;
                      if (r3.z != 0) {
                        if (r4.z == 0) {
                          r4.x = t0.SampleLevel(s0_s, r5.xz, 0).w;
                        }
                        if (r4.w == 0) {
                          r4.y = t0.SampleLevel(s0_s, r5.yw, 0).w;
                        }
                        r3.z = -r2.x * 0.5 + r4.x;
                        r4.x = r4.z ? r4.x : r3.z;
                        r2.x = -r2.x * 0.5 + r4.y;
                        r4.y = r4.w ? r4.y : r2.x;
                        r3.xz = cmp(abs(r4.xy) >= r3.xx);
                        r2.x = -r3.y * 8 + r5.x;
                        r5.x = r3.x ? r5.x : r2.x;
                        r2.x = -r3.w * 8 + r5.z;
                        r5.z = r3.x ? r5.z : r2.x;
                        r2.x = r3.y * 8 + r5.y;
                        r5.y = r3.z ? r5.y : r2.x;
                        r2.x = r3.w * 8 + r5.w;
                        r5.w = r3.z ? r5.w : r2.x;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    r2.x = v0.x * r0.x + -r5.x;
    r0.x = -v0.x * r0.x + r5.y;
    r3.x = v0.y * r0.y + -r5.z;
    r2.x = r2.y ? r2.x : r3.x;
    r0.y = -v0.y * r0.y + r5.w;
    r0.x = r2.y ? r0.x : r0.y;
    r3.xy = cmp(r4.xy < float2(0,0));
    r0.y = r0.x + r2.x;
    r3.xy = cmp((int2)r1.ww != (int2)r3.xy);
    r0.y = 1 / r0.y;
    r1.w = cmp(r2.x < r0.x);
    r0.x = min(r2.x, r0.x);
    r1.w = r1.w ? r3.x : r3.y;
    r2.x = r2.w * r2.w;
    r0.x = r0.x * -r0.y + 0.5;
    r0.y = cb0[1].x * r2.x;
    r0.x = (int)r0.x & (int)r1.w;
    r0.x = max(r0.x, r0.y);
    r0.xy = r0.xx * r2.zz + r0.zw;
    r3.x = r2.y ? r0.z : r0.x;
    r3.y = r2.y ? r0.y : r0.w;
    r1.xyz = t0.SampleLevel(s0_s, r3.xy, 0).xyz;
  }
  o0.xyz = r1.xyz;
  o0.w = 1;
  return;
}