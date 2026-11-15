#ifndef SRC_TWATTILA_DOF_BLUR_CS_HLSLI_
#define SRC_TWATTILA_DOF_BLUR_CS_HLSLI_

// depth_of_field_cs.hlsl cs_dof_horizontal_blur / cs_dof_vertical_blur (Attila and Thrones C9D036B1 / 83A1A82D,
// WARHAMMER 5D61B743 / 13D9D610), rebuilt (3Dmigoto can't decompile them) as Rome II's dof01 / dof02: 32 x 2 threads,
// 4 pixels each, a 29-tap Gaussian where each tap counts by its circle of confusion (alpha), blended over the pixel by
// its own. The game keeps the taps as 8 bits in group memory, which clips HDR: HDR keeps floats, SDR is the game's.
#include "./dof_blur.hlsli"

Texture2D<float4> input_texture_SM5 : register(t0);
RWTexture2D<float4> output_texture : register(u0);

// Taps -14 .. +14, the game's weights (they include the 1 / 255 of its 8-bit taps).
static const float DOF_BLUR_WEIGHTS[29] = {
  3.02470398e-005, 3.98414777e-005, 5.14191452e-005, 6.50206202e-005, 8.05590244e-005,
  9.77944364e-005, 0.000116319083, 0.000135557828, 0.000154787223, 0.000173173903,
  0.000189830782, 0.000203886128, 0.000214558415, 0.000221228082, 0.000223497074,
  0.000221228082, 0.000214558415, 0.000203886128, 0.000189830782, 0.000173173903,
  0.000154787223, 0.000135557828, 0.000116319083, 9.77944364e-005, 8.05590244e-005,
  6.50206202e-005, 5.14191452e-005, 3.98414777e-005, 3.02470398e-005
};

// 2 lines x (128 pixels + 28 border taps).
groupshared float4 g_dof_taps[2][156];

// A tap premultiplied by its circle of confusion, in the game's 0 - 255 units (SDR: its 8 bits, truncated).
float4 DofBlurTap(float4 color)
{
  const float4 premultiplied = float4(DofTap(color.rgb) * color.a, color.a) * 255.f;
  return (HDR >= 1.f) ? premultiplied : floor(premultiplied);
}

// vertical: along y (groups of 2 columns x 128 rows), else along x (2 rows x 128 columns).
void DofBlurCs(uint3 group, uint3 thread, bool vertical)
{
  const int size = (int)(vertical ? g_screen_size.y : g_screen_size.x);
  const int start = (int)(vertical ? group.y : group.x) * 128;
  const int line_index = (int)(vertical ? group.x : group.y) * 2 + (int)thread.y;
  const int first = start + (int)thread.x * 4;
  const int2 step = (vertical ? int2(0, 1) : int2(1, 0));
  const int2 line_origin = (vertical ? int2(line_index, 0) : int2(0, line_index));

  // Taps from 14 before the group to 14 after it, clamped to the screen.
  [unroll]
  for (int i = 0; i < 4; ++i) {
    g_dof_taps[thread.y][thread.x * 4 + i] =
        DofBlurTap(input_texture_SM5.Load(int3(line_origin + step * clamp(first - 14 + i, 0, size - 1), 0)));
  }
  if (thread.x < 28) {
    g_dof_taps[thread.y][128 + thread.x] =
        DofBlurTap(input_texture_SM5.Load(int3(line_origin + step * min(start + 114 + (int)thread.x, size - 1), 0)));
  }
  GroupMemoryBarrierWithGroupSync();

  if (first >= size) return;
  [unroll]
  for (int k = 0; k < 4; ++k) {
    // In the game's order: the first two taps, then each next one added.
    const uint tap = thread.x * 4 + k;
    float4 sum = g_dof_taps[thread.y][tap] * DOF_BLUR_WEIGHTS[0] + g_dof_taps[thread.y][tap + 1] * DOF_BLUR_WEIGHTS[1];
    [unroll]
    for (int j = 2; j < 29; ++j) {
      sum = g_dof_taps[thread.y][tap + j] * DOF_BLUR_WEIGHTS[j] + sum;
    }
    const float3 blurred = sum.rgb / (sum.a + 1e-7f);

    const int2 pixel = line_origin + step * (first + k);
    const float4 center = input_texture_SM5.Load(int3(pixel, 0));
    output_texture[pixel] = float4(center.a * (blurred - center.rgb) + center.rgb, center.a);
  }
}

#endif  // SRC_TWATTILA_DOF_BLUR_CS_HLSLI_
