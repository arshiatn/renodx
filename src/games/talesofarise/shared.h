#ifndef SRC_TALESOFARISE_SHARED_H_
#define SRC_TALESOFARISE_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float swap_chain_encoding;  // 0 HDR10, 1 scRGB, 2 SDR (gamma 2.2; Peak, Paper White and UI unused)

  float hdr;  // 0 SDR, 1 HDR (Extended: UE Filmic Extended), 2 HDR (PsychoV31)
  float bloom;
  float vignette;
  float sharpening;
  float film_grain;            // cutscene tonemapper: grain intensity and jitter
  float chromatic_aberration;  // cutscene tonemapper: scene color fringe
  float video_highlights;      // ITM: 0 SDR, 1 video white at Peak
  float video_dithering;       // 1: 1 code value of dither before the ITM

  // PsychoV31
  float exposure;
  float highlights;
  float shadows;
  float contrast;  // x PsychoV31's own (1)
  float saturation;
  float highlight_saturation;
  float roll_off;      // 0 latest, 1 default, 2 soonest
  float game_shadows;  // 0 PsychoV31's, 1 the game's toe below mid gray
  float highlight_contrast;      // above mid gray, 1 PsychoV31's own
  float shadow_contrast;         // below mid gray, 1 PsychoV31's own
  float flare;                   // 0 none, deeper blacks above it
  float dechroma;                // 0 none, bright colors toward white above it
  float gamut_compression;       // 0 none (the display clips), 1 full
  float gamut_compression_mode;  // PsychoV31's target: 0 BT.709, 1 BT.2020, 3 Display P3

  // Extended (PsychoV31 grading before the curve; all 1: skipped)
  float ext_exposure;
  float ext_highlights;
  float ext_shadows;
  float ext_contrast;
  float ext_saturation;
  float ext_highlight_saturation;

  float highlight_hue_shift;  // PsychoV31: hue toward the game's SDR curve above mid gray, 0 (V31's own) - 1
  float highlight_saturation_shift;  // PsychoV31: saturation toward the game's SDR curve, same weighting, 0 - 1
  float ext_curve;            // UE Extended base: 0 the game's curve (legacy: extended), 1 UE Filmic
  float ext_highlight_hue_shift;  // UE Extended with the game's (legacy) curve: hue toward the game's SDR curve above mid gray, 0-1
  float sdr_curve;                // SDR tonemapper: 0 the game's (UE legacy), 1 UE Filmic (r.TonemapperFilm 1)
  float motion_blur;              // motion blur filters: velocity scale and maximum length multiplier, 1 stock
  float ext_filmic_highlight_hue_shift;  // UE Extended with the UE Filmic base curve: as ext_highlight_hue_shift
  float ext_filmic_highlight_saturation; // UE Extended with the UE Filmic base curve: as ext_highlight_saturation (legacy)
  float volumetric_light_smoothing;      // deferred sun lighting, haze's sun visibility (t6): 0 stock (point), 1 soft, 2 along the sun's rays
  float filter_scale;     // DLSS addon: render / output while it upscales (Bandai's screen filters in output pixels), else 1
  float game_sharpening;  // DLSS addon: 0 drops Bandai's sharpening before DLAA / DLSS, else 1
  float ao_halo_fix;      // DLSS addon's AO halo: 0 the game's, 1 reduced (samples far in front left out), 2 + darker
  float sun_shadow_distance;  // DLSS addon: Sun shadow distance / the game's own (1 without it)
  float sun_shadow_smoothing;  // sun shadow mask (cs 0x2084C889) above the game's sun shadow distance: 0 the game's, 1-4 tent filter, sharper
  float shadow_edge_color;     // lighting00-05: the colored line at sun shadow edges (cb0[63] / [64]), 1 the game's, 0 none
  float sun_haze;              // lighting00-02: the sun's haze (in-scattered light and its tint), 1 the game's
  float painterly;             // painterly00-02: the painterly filter and blurs, 1 the game's, 0 none
  float outlines;              // outline00-01: character outline strength, 1 the game's
  float sun_shadow_edge;       // sunshadow00 above the game's sun shadow distance: 0 the filter's edge, 1 as thin on the ground as at the game's distance
  float ssao_mode;             // DLSS addon's Ambient occlusion: 0 the game's, 1 GT-VBAO (bitmask_ao.hlsli)
  float ssao_radius;           // GT-VBAO: metres
  float ssao_strength;         // GT-VBAO: exponent of the visibility, 1 GT-VBAO's own
  float ssao_thickness;        // GT-VBAO: metres behind each depth sample (more far away)
  float ssao_directions;       // GT-VBAO: 2^this directions around the view
  float ssao_quality;          // GT-VBAO: 8 x 2^this samples per side of a direction
  float ssao_frame;            // GT-VBAO: its noise's frame, 0-63
};

#define SI  shader_injection
#define HDR SI.hdr

// SDR output (displays without HDR): the display's white is the game's and the UI's.
#define TOA_SDR_OUTPUT (SI.swap_chain_encoding == 2.f)
// Peak relative to Paper White.
#define TOA_PEAK (TOA_SDR_OUTPUT ? 1.f : (max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 1.f)))
// Paper White relative to UI (the UI target's white).
#define TOA_PAPER_WHITE (TOA_SDR_OUTPUT ? 1.f : (max(SI.diffuse_white_nits, 1.f) / max(SI.graphics_white_nits, 1.f)))
// Paper White / UI as a gain on gamma 2.2 encoded values (the proxy decodes 2.2 x UI nits).
#define TOA_ENCODED_SCALE pow(TOA_PAPER_WHITE, 1.f / 2.2f)

#ifndef __cplusplus
#if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b13, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b13) {
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

#include "../../shaders/renodx.hlsl"

// Bandai's ambient occlusion sums 9 samples of its depth pyramid around each pixel without a range check: a sample on
// something far in front (a character before a bush) adds almost nothing, as if open, so a ring around every
// character and object lost its occlusion (the bright halo, stock game; ATN's Halo test: the AO 20-35% brighter within
// ~50 pixels of the character at 4K). AO Halo Fix: samples more than `limit` in front of the pixel (twice the AO's
// reach at that depth, at least 5% of the depth + 10 cm; v34: 10% + 20 cm left a thin halo where the character stands
// close to the bush) stand for what they hide: AoHaloTerm sets them aside, AoHaloSum adds back the larger of their
// own occlusion and the visible samples' average for each (as if the hidden part looked like the visible rest). The
// average alone (v34-v37) also lightened crevices under overhangs, whose samples on the overhang really shade them
// (ATN, debug7): the AO is now never lighter than the game's.
// The cutoff per choice, x the limit above and the blurs' (AoBlurWeight). Reduced + darker (ATN's pick of tests x 1/2,
// 1/4, 1/16): more set-aside samples add back at least their own occlusion, so shading gets darker. The halo cannot
// go completely: the AO only sees the screen, and what a character hides (leaves behind it) is guessed from what is
// visible (ATN v42: with no screen-space AO at all, no halo anywhere).
static const float AO_HALO_CUTOFF[3] = {0.f, 1.f, 0.25f};

float AoHaloTerm(float term, float offset_z, float limit, inout float kept, inout float excluded) {
  if (SI.ao_halo_fix != 0.f && offset_z < -limit * AO_HALO_CUTOFF[min(uint(SI.ao_halo_fix), 2u)]) {
    excluded += max(0.f, term);
    return 0.f;
  }
  kept += 1.f;
  return term;
}

float AoHaloSum(float kept_sum, float kept, float excluded) {
  const float hidden = (9.f - kept) * kept_sum / max(kept, 1.f);
  return kept_sum + max(excluded, hidden);
}

// Bandai's AO blurs (horizontal, then vertical with the temporal blend) weight each neighbour 0.1 + 1 / (1 + |depth
// difference in cm|): never below 0.1, so a character's own (open) AO bled into a ring around it. AO Halo Fix: a
// neighbour more than 5% of the depth + 10 cm away in depth gets 0 (x AO_HALO_CUTOFF; small ones stop the blur and
// show the AO's sample pattern). `inverse` is 1 / (1 + |difference|).
float AoBlurWeight(float inverse, float center_depth) {
  if (SI.ao_halo_fix != 0.f
      && 1.f / inverse - 1.f > (0.05f * center_depth + 10.f) * AO_HALO_CUTOFF[min(uint(SI.ao_halo_fix), 2u)]) {
    return 0.f;
  }
  return 0.1f + inverse;
}

// Bandai reads its volumetric lighting volumes through a point sampler: every 8-pixel cell is a hard block. t6 (RGBA8
// 480x270x5, the sun's visibility per distance band) draws the stair-step edge of the mountains' shadow in the haze
// (ATN's test). Smooth Volumetric Light filters it in the cell's slice (point-sampled as stock: every call reads a slice
// centre) with bilinear taps from Loads (no linear sampler is bound), in the frame of `shaft` (the sun's direction on
// screen; zero at the sun's point: one bilinear tap):
//   1 Soft: a 3 x 3 tent 0.75 cells apart along and across: milder, no streaks along the rays.
//   2 Along Sun Rays: 11 taps 0.8 cells apart along the shaft (+-4 cells, tent): the stairs average out along the
//     rays while the edges stay as sharp as one bilinear cell (v24-v27's Gaussian in every direction blurred across).
//   (v45's Long Rays, 21 taps, removed: ATN.)
// Both were tuned on the game's 8-pixel cells (output pixels): finer cells (Max volumetric fog: 2) shrank their reach on
// screen and the shadow map's steps showed (ATN, v48). `screen_width` (render pixels) sets the stretch to the game's
// reach, with more taps (at most 25 along the rays, 5 x 5 soft).
// saturate: the game takes log2(1 - visibility); a filtered 1 can round to 1.0000001, and that NaN turned every fully
// shadowed pixel black (v24-v26).
float4 VolumeBilinear(Texture3D<float4> volume, int slice, float2 position, int2 last) {
  const int2 base = int2(floor(position));
  const float2 fraction = position - float2(base);
  const int2 low = clamp(base, 0, last);
  const int2 high = clamp(base + 1, 0, last);
  const float4 top = lerp(volume.Load(int4(low.x, low.y, slice, 0)), volume.Load(int4(high.x, low.y, slice, 0)), fraction.x);
  const float4 bottom = lerp(volume.Load(int4(low.x, high.y, slice, 0)), volume.Load(int4(high.x, high.y, slice, 0)), fraction.x);
  return lerp(top, bottom, fraction.y);
}

float4 SampleVisibility(Texture3D<float4> volume, SamplerState point_sampler, float3 uvw, float2 shaft, float screen_width) {
  if (SI.volumetric_light_smoothing == 0.f) return volume.SampleLevel(point_sampler, uvw, 0);
  uint3 size;
  volume.GetDimensions(size.x, size.y, size.z);
  const int2 last = int2(size.xy) - 1;
  const int slice = min(int(uvw.z * float(size.z)), int(size.z) - 1);
  const float2 texel = uvw.xy * float2(size.xy) - 0.5f;
  float2 along = shaft * float2(size.xy);
  along *= rsqrt(max(dot(along, along), 1e-30f));
  const float stretch = clamp(8.f * SI.filter_scale * float(size.x) / screen_width, 1.f, 4.f);
  float4 sum = 0.f;
  float weights = 0.f;
  [branch]
  if (SI.volumetric_light_smoothing == 1.f) {
    const float2 across = float2(-along.y, along.x);
    const int half_taps = min(int(round(stretch)), 2);
    const float spacing = 0.75f * stretch / float(half_taps);
    [loop]
    for (int a = -half_taps; a <= half_taps; ++a) {
      [loop]
      for (int b = -half_taps; b <= half_taps; ++b) {
        const float weight = float(half_taps + 1 - abs(a)) * float(half_taps + 1 - abs(b));
        sum += weight * VolumeBilinear(volume, slice, texel + spacing * (float(a) * along + float(b) * across), last);
        weights += weight;
      }
    }
  } else {
    const int reach = min(int(ceil(5.f * stretch)), 12);
    const float spacing = 0.8f * 5.f * stretch / float(reach);
    [loop]
    for (int i = -reach; i <= reach; ++i) {
      const float weight = float(reach + 1 - abs(i));
      sum += weight * VolumeBilinear(volume, slice, texel + float(i) * spacing * along, last);
      weights += weight;
    }
  }
  return saturate(sum / weights);
}

// Opaque videos (CriWare UMG materials), stock sRGB-encoded output (1 = video white): shown at Paper White instead
// of UI brightness. HDR: Video Highlights (ITM, Ryse: Son of Rome's): luminance above mid gray stretched (smooth
// start) so white reaches lerp(1, Peak, amount), hue kept, on the 2.2 decode the proxy uses. 8-bit sources band if
// stretched far: Video Dithering adds 1 code value (peak to peak) of interleaved gradient noise, static in screen
// space, first; the stretch scales it like the steps.
float3 VideoOutput(float3 encoded, float2 pixel) {
  if (HDR >= 1.f && SI.video_highlights > 0.f && !TOA_SDR_OUTPUT) {
    if (SI.video_dithering != 0.f) {
      encoded += (frac(52.9829189f * frac(dot(floor(pixel), float2(0.06711056f, 0.00583715f)))) - 0.5f) / 255.f;
    }
    float3 color = renodx::color::gamma::DecodeSafe(encoded);
    float y = renodx::color::y::from::BT709(color);
    if (y > 0.18f) {
      float t = saturate((y - 0.18f) / (1.f - 0.18f));
      float white = lerp(1.f, TOA_PEAK, SI.video_highlights);
      encoded = renodx::color::gamma::EncodeSafe(color * ((y + (white - 1.f) * t * t) / y));
    }
  }
  return encoded * TOA_ENCODED_SCALE;
}

#endif

#endif  // SRC_TALESOFARISE_SHARED_H_
