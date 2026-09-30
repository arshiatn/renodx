// ---- Rebuilt from 02_0x2C31566B.cs_5_0.cso (3Dmigoto can't decompile it).
// ROME II dof01 - DOF High, pass 2: horizontal blur. 32x2 threads, 4 pixels each, 128 per group.
// 29-tap Gaussian where each tap counts by its CoC, blended over the pixel by its own CoC.
// Stock keeps the taps as 8-bit in shared memory, which clips HDR. HDR keeps floats; SDR is stock.

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
  float4 g_viewport_dimensions : packoffset(c29);
  float4 g_camera_temp0 : packoffset(c30);
  float4 g_camera_temp1 : packoffset(c31);
  float4 g_camera_temp2 : packoffset(c32);
  float4 g_clip_rect : packoffset(c33);
  float g_hide_foliage : packoffset(c34);
}

Texture2D<float4> input_texture_SM5 : register(t0);
RWTexture2D<float4> output_texture : register(u0);

#include "../shared.h"

// 29 taps, -14..+14 (stock weights, include the 1/255 of the 8-bit taps).
static const float kWeights[29] = {
  3.02470398e-005, 3.98414777e-005, 5.14191452e-005, 6.50206202e-005, 8.05590244e-005,
  9.77944364e-005, 0.000116319083, 0.000135557828, 0.000154787223, 0.000173173903,
  0.000189830782, 0.000203886113, 0.000214558415, 0.000221228096, 0.000223497074,
  0.000221228096, 0.000214558415, 0.000203886113, 0.000189830782, 0.000173173903,
  0.000154787223, 0.000135557828, 0.000116319083, 9.77944364e-005, 8.05590244e-005,
  6.50206202e-005, 5.14191452e-005, 3.98414777e-005, 3.02470398e-005
};

// 2 lines x (128 pixels + 28 border taps).
groupshared float4 g_taps[2][156];

// Tap premultiplied by its CoC (alpha), in stock 0-255 units.
// SDR: stock 8-bit (clamped and truncated). HDR: float, highlights kept.
float4 Tap(float4 color)
{
  if (HDR == 1.f) return float4(max(color.rgb, 0.f) * color.a, color.a) * 255.f;
  return floor(float4(saturate(color.rgb) * color.a, color.a) * 255.f);
}

// CoC-weighted blur of 4 pixels from the taps, blended over each pixel by its own CoC.
void BlurAndStore(uint row, uint first_tap, int2 first_pixel, int2 step)
{
  [unroll]
  for (int k = 0; k < 4; ++k) {
    float4 sum = 0.f;
    [unroll]
    for (int j = 0; j < 29; ++j) {
      sum += kWeights[j] * g_taps[row][first_tap + k + j];
    }
    float3 blurred = sum.rgb / (sum.a + 1e-7f);

    int2 pixel = first_pixel + step * k;
    float4 center = input_texture_SM5.Load(int3(pixel, 0));
    output_texture[pixel] = float4(lerp(center.rgb, blurred, center.a), center.a);
  }
}

[numthreads(32, 2, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
  const int width = (int)g_screen_size.x;
  const int start = (int)group.x * 128;
  const int y = (int)group.y * 2 + (int)thread.y;
  const int first = start + (int)thread.x * 4;

  // Taps from 14 left of the group to 14 right of it (clamped to the screen).
  [unroll]
  for (int i = 0; i < 4; ++i) {
    int x = clamp(first - 14 + i, 0, width - 1);
    g_taps[thread.y][thread.x * 4 + i] = Tap(input_texture_SM5.Load(int3(x, y, 0)));
  }
  if (thread.x < 28) {
    int x = min(start + 114 + (int)thread.x, width - 1);
    g_taps[thread.y][128 + thread.x] = Tap(input_texture_SM5.Load(int3(x, y, 0)));
  }
  GroupMemoryBarrierWithGroupSync();

  if (first >= width) return;
  BlurAndStore(thread.y, thread.x * 4, int2(first, y), int2(1, 0));
}
