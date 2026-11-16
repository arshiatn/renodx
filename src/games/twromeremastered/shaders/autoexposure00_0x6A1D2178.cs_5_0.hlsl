// ROME REMASTERED autoexposure00 - Feral's auto exposure (compute, one thread), after the 64-bin histogram of the
// scene's natural log luminance (2B9B88C3 per 16 x 16 tile, 3C355C35 summed: trace 40 L6476-6478); ResolveMainColor
// (B72063D6, resolve.hlsli) reads u2 at its t1.
// Stock: between the low and high shares of the pixels (cb0[1].z / .w) the weighted mean of the bins' centres (log
// luminance from cb0[1].y to cb0[1].x) -> the average L; x = (1.03 - 2 / (log10(L + 1) + 2)) / L, this frame's alone.
// The luminance at the high share (between its bin and the next) is the bright level, approached from last frame's
// (u2[0].z) at cb0[2].y up / .z down per cb0[2].x, slower below exp(cb0[1].x); w = level^2.2, y = exp(-w / cb0[2].w).
// Modded Auto Exposure (HDR modes, Experimental): each bin's value in the mean at most the mean x 2^Sensitivity, the
// mean found with the cap itself (4 steps from the stock one): Pharaoh's cap of a sample at the average x
// 2^Sensitivity, on the histogram's bins. The bright level stays the game's. Off, and in SDR: the game's code.
// RECONSTRUCTED FROM THE ASSEMBLY (the decompiler gave no source); literals by their bits.

cbuffer cb0 : register(b0) {
  float4 cb0[3];
}

StructuredBuffer<uint> t2 : register(t2);
RWStructuredBuffer<float4> u2 : register(u2);

#include "../shared.h"

// A bin's centre: its natural log luminance.
float BinLog(uint bin, float range) {
  return ((float)bin + 0.5f) * range * asfloat(0x3C820821u) + cb0[1].y;
}

// The game's walk over the bins: the window's weight and weighted sum of logs (each at most `cap`), and the bright
// level.
void Meter(float pixels, float range, float cap, out float weight, out float sum, out float bright) {
  weight = 0.f;
  sum = 0.f;
  bright = 0.f;
  const float low = pixels * cb0[1].z;
  const float high = pixels * cb0[1].w;
  bool in_window = false;
  bool done = false;
  uint before = 0u;
  for (uint bin = 0u; bin < 64u && !done; ++bin) {
    const uint count = t2[bin];
    const uint through = count + before;
    const float through_pixels = (float)through;
    const float count_pixels = (float)count;
    const float value = BinLog(bin, range);
    if (through_pixels >= high) {
      // The window ends in this bin: its share below the high count, and the luminance there.
      const float part = cb0[1].w * pixels - (float)before;
      const float share = part / count_pixels;
      sum = part * min(value, cap) + sum;
      weight = weight + part;
      const float luminance = exp(value);
      const uint next_count = t2[bin + 1u];
      const float next_pixels = (float)next_count;
      const float next_luminance = exp(BinLog(bin + 1u, range));
      const float rest = 1.f - share;
      const float mixed = (count_pixels * luminance) * rest + share * (next_pixels * next_luminance);
      const float mixed_weight = count_pixels * rest + share * next_pixels;
      const float level = (0u < next_count) ? (mixed / mixed_weight) : luminance;
      bright = (bin != 63u) ? level : luminance;
      done = true;
    } else {
      const float capped = min(value, cap);
      const float whole_sum = count_pixels * capped + sum;
      const float whole_weight = weight + count_pixels;
      const bool crossing = through_pixels >= low;
      const float above = 1.f - (cb0[1].z * pixels - (float)before) / count_pixels;
      const float part_sum = (count_pixels * above) * capped + sum;
      const float part_weight = above * count_pixels + weight;
      if (in_window) {
        weight = whole_weight;
        sum = whole_sum;
      } else if (crossing) {
        weight = part_weight;
        sum = part_sum;
        in_window = true;
      }
    }
    before = through;
  }
}

[numthreads(1, 1, 1)]
void main() {
  const float pixels = (float)(asuint(cb0[0].y) * asuint(cb0[0].x));
  const float range = cb0[1].x - cb0[1].y;
  float weight;
  float sum;
  float bright;
  Meter(pixels, range, asfloat(0x7F800000u), weight, sum, bright);
  float mean = sum / weight;

  // ADDED: Modded Auto Exposure.
  if (HDR >= 0.5f && SI.auto_exposure_highlight_protection > 0.f) {
    const float headroom = clamp(SI.auto_exposure_highlight_headroom_stops, 0.f, 16.f) * 0.693147182f;
    [loop]
    for (int step = 0; step < 4; ++step) {
      float capped_bright;
      Meter(pixels, range, mean + headroom, weight, sum, capped_bright);
      mean = sum / weight;
    }
  }

  const float average = exp(mean);
  // log10 as the game's bytes have it: log2 x 0x3E9A209A (fxc's log10 uses 0x3E9A209B).
  const float key = asfloat(0x3F83D70Au) - 2.f / (log2(average + 1.f) * asfloat(0x3E9A209Au) + 2.f);
  const float exposure = key / average;

  float previous = u2[0].z;
  previous = (previous == 0.f) ? bright : previous;
  const float change = bright - previous;
  const float slow = min(max(previous, 0.5f) / exp(cb0[1].x), 1.f);
  const float2 speeds = (cb0[2].yz * cb0[2].x) * (slow * slow);
  const float level = (0.f < change) ? (min(change, speeds.x) + previous) : (max(change, -speeds.y) + previous);
  const float level_power = pow(max(level, 0.f), 2.2f);
  u2[0] = float4(exposure, exp(-level_power / cb0[2].w), level, level_power);
}
