#ifndef SRC_TALESOFARISE_SHARED_H_
#define SRC_TALESOFARISE_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float swap_chain_encoding;  // 0 HDR10, 1 scRGB

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

  // Extended (PsychoV31 grading before the curve; all 1: skipped)
  float ext_exposure;
  float ext_highlights;
  float ext_shadows;
  float ext_contrast;
  float ext_saturation;
  float ext_highlight_saturation;

  float highlight_hue_shift;  // PsychoV31: hue toward the game's SDR curve above mid gray, 0 (V31's own) - 1
  float ext_curve;            // UE Extended base: 0 the game's curve (legacy: extended), 1 UE Filmic
  float ext_highlight_hue_shift;  // UE Extended with the game's (legacy) curve: hue toward the game's SDR curve above mid gray, 0-1
  float sdr_curve;                // SDR tonemapper: 0 the game's (UE legacy), 1 UE Filmic (r.TonemapperFilm 1)
  float motion_blur;              // motion blur filters: velocity scale and maximum length multiplier, 1 stock
  float ext_filmic_highlight_hue_shift;  // UE Extended with the UE Filmic base curve: as ext_highlight_hue_shift
  float volumetric_light_smoothing;      // deferred sun lighting, haze's sun visibility (t6): 0 stock (point), 1 along the sun's rays
};

#define SI  shader_injection
#define HDR SI.hdr

// Peak relative to Paper White.
#define TOA_PEAK (max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 1.f))
// Paper White / UI as a gain on gamma 2.2 encoded values (the proxy decodes 2.2 x UI nits).
#define TOA_ENCODED_SCALE pow(max(SI.diffuse_white_nits, 1.f) / max(SI.graphics_white_nits, 1.f), 1.f / 2.2f)

#ifndef __cplusplus
#if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b13, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b13) {
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

#include "../../shaders/renodx.hlsl"

// Opaque videos (CriWare UMG materials), stock sRGB-encoded output (1 = video white): shown at Paper White instead
// of UI brightness. HDR: Video Highlights (ITM, Ryse: Son of Rome's): luminance above mid gray stretched (smooth
// start) so white reaches lerp(1, Peak, amount), hue kept, on the 2.2 decode the proxy uses. 8-bit sources band if
// stretched far: Video Dithering adds 1 code value (peak to peak) of interleaved gradient noise, static in screen
// space, first; the stretch scales it like the steps.
float3 VideoOutput(float3 encoded, float2 pixel) {
  if (HDR >= 1.f && SI.video_highlights > 0.f) {
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
