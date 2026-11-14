// 0x1E448816 (fx_autoexposurecs): the game's auto exposure, run when the scene's environment turns it on (g_use_auto_exposure;
// in none of 95 traces). Each of up to 32 threads averages the light's luma along a cross at its focal point (the row
// inclusive, the column without its last pixel, as the game's); thread 0 averages those, aims the brightness at
// sqrt(2^target / average) within the tone mapper's brightness +- the largest correction, eased by the sensitivity and
// limited to the adaptation speed. Written from the assembly (3Dmigoto leaves its group memory and UAV out).
// Modded Auto Exposure (SI.auto_exposure_highlight_protection, as Pharaoh's): each sample's luma capped at
// 2^headroom x the average the last brightness was aimed at, so a fire or the sun can't drag the frame dark. The add-on
// draws this copy only with the row on in an HDR mode; otherwise the game's own bytes run (addon.cpp, BuildRuntimeData).
#include "../shared.h"

cbuffer tone_mapping : register(b0) {
  float g_tone_mapping_brightness;
  float g_tone_mapping_burn;
  int g_use_auto_exposure;
};

cbuffer Auto_exposure_cb : register(b1) {
  uint g_used_focal_points;
  float2 g_focal_points[32];
  uint g_focal_cross_radius;
  float g_target_exposure;
  float2 g_adaptation_speed;
  float g_max_relative_correction;
  float g_sensitivity;
  float g_delta_time;
};

struct Auto_exposure_output {
  float tone_mapper_brightness;
};

Texture2D<float4> t_light_accumulation : register(t0);
RWStructuredBuffer<Auto_exposure_output> g_auto_exposure_output_buffer : register(u0);

groupshared float focal_luma[32];

[numthreads(32, 1, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID) {
  if (vThreadID.x < g_used_focal_points) {
    float2 size;
    t_light_accumulation.GetDimensions(size.x, size.y);
    const int2 centre = (int2)floor(size * g_focal_points[vThreadID.x]);
    const int2 first = centre - (int)g_focal_cross_radius;
    const float2 last = min(size - 1.f, (float2)(uint2)(centre + (int)g_focal_cross_radius));
    float cap = 3.402823466e+38f;
    if (SI.auto_exposure_highlight_protection != 0.f) {
      const float previous = g_auto_exposure_output_buffer[0].tone_mapper_brightness;
      if (0.000001f < previous) cap = exp2(g_target_exposure + clamp(SI.auto_exposure_highlight_headroom_stops, 0.f, 4.f)) / (previous * previous);
    }
    float sum = 0.f;
    float count = 0.f;
    for (int x = first.x; !(last.x < (float)x); ++x) {
      sum += min(dot(t_light_accumulation.Load(int3(x, centre.y, 0)).rgb, float3(0.2126f, 0.7152f, 0.0722f)), cap);
      count += 1.f;
    }
    for (int y = first.y; (float)y < last.y; ++y) {
      sum += min(dot(t_light_accumulation.Load(int3(centre.x, y, 0)).rgb, float3(0.2126f, 0.7152f, 0.0722f)), cap);
      count += 1.f;
    }
    focal_luma[vThreadID.x] = sum / max(count, 1.f);
  }
  GroupMemoryBarrierWithGroupSync();
  if (vThreadID.x == 0) {
    float sum = 0.f;
    for (uint i = 0; i < g_used_focal_points; ++i) sum += focal_luma[i];
    const float average = sum / (float)g_used_focal_points;
    const float previous = g_auto_exposure_output_buffer[0].tone_mapper_brightness;
    float aim = sqrt(exp2(g_target_exposure) / average);
    aim = min(g_tone_mapping_brightness + g_max_relative_correction, max(g_tone_mapping_brightness - g_max_relative_correction, aim));
    const float change = aim - previous;
    const float eased = saturate(abs(change) / ((1.f - g_sensitivity) * previous)) * change + previous;
    const float brighter = min(eased, g_delta_time * g_adaptation_speed.y + previous);
    const float darker = max(eased, -g_delta_time * g_adaptation_speed.x + previous);
    g_auto_exposure_output_buffer[0].tone_mapper_brightness = (0.000001f < previous) ? ((previous < eased) ? brighter : darker) : 1.f;
  }
}
