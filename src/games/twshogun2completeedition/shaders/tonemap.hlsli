// Total War: Shogun 2 Complete Edition tonemap: hdr_to_screen.hlsl ps30_main, written from the game's own source in
// data\shaders.pack (the 3Dmigoto decompile collapses swizzles: it drops the white clip and the shoulder). Shared by
// tonemap00 (0xEF060C7C) and tonemap01 (0xBEDBEFD8, distortion); each declares its textures first.
// Stock: scene + bloom -> XYZ x brightness^2 (auto exposure, camera fade) -> white balance (cg_color_temp, then to
// 6000 K) -> AP1 -> UE4's filmic curve per channel (slope, toe, shoulder, black / white clip) -> 3-way color grading
// by luma (shadows / mids / highlights: saturation, contrast, gamma, gain, offset) -> D65 -> BT.709, saturate,
// x g_brightness (menu Brightness / 10), gamma 1 / 2.2. The game's HDR (g_hdr_display) stretched that SDR result from mid gray up
// (Y 1 -> peak / scene nits) into PQ, its UI blended in PQ: not used.
// The game's constants (data.pack *.lighting TONE_MAPPING): slope 0.88, toe 0.5, shoulder 0.13-0.14, black clip 0,
// white clip 0.04. With the toe's comma operator (kept) mid gray 0.18 -> 0.087 (UE4: 0.18).
// SDR: stock. HDR (Extended): marat569's UE Filmic Extended (as in Expeditions: Rome / Tales of Arise) on the game's
// curve, then the grading, then a per-channel shoulder to Peak. HDR (PsychoV31): PsychoV31 instead of the curve,
// anchored on its mid gray (its own contrast); then the grading. Both HDR modes show the game's gamma setting as SDR
// does (gamma 1 / g_gamma_output on a 2.2 display). All modes: Brightness at 10 (x 1, ATN: the
// menu's 5-20 is an exposure the mod's Exposure and Paper White replace).
// Out: gamma 2.2 x Paper White / UI (the proxy decodes it and scales by UI).
// Output SDR (displays without HDR): white = the display's (no Paper White / UI); HDR (Extended) keeps the game's
// curve (no room above white to extend into), PsychoV31 maps to that white within BT.709; clipped as the game.
#include "../shared.h"
#include "../psycho_test31.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float4 screen_size : packoffset(c37);
}

cbuffer colorimetry_VS_PS : register(b1)
{
  float g_inv_gamma_output : packoffset(c0.z);
}

cbuffer tone_mapping : register(b2)
{
  float g_tone_mapping_brightness : packoffset(c0);
  float g_tone_mapping_brightness_fade : packoffset(c0.y);
  float g_tone_mapping_film_slope : packoffset(c4.y);
  float g_tone_mapping_film_toe : packoffset(c4.z);
  float g_tone_mapping_film_shoulder : packoffset(c4.w);
  float g_tone_mapping_film_black_clip : packoffset(c5);
  float g_tone_mapping_film_white_clip : packoffset(c5.y);
  int g_use_auto_exposure : packoffset(c5.z);
}

cbuffer hdr_to_screen_PS : register(b3)
{
  float radial_blur_strength : packoffset(c0);
  float2 radial_blur_position : packoffset(c0.y);
  float4 cg_global_saturation : packoffset(c2);
  float4 cg_global_contrast : packoffset(c3);
  float4 cg_global_gamma : packoffset(c4);
  float4 cg_global_gain : packoffset(c5);
  float4 cg_global_offset : packoffset(c6);
  float4 cg_shadows_saturation : packoffset(c7);
  float4 cg_shadows_contrast : packoffset(c8);
  float4 cg_shadows_gamma : packoffset(c9);
  float4 cg_shadows_gain : packoffset(c10);
  float4 cg_shadows_offset : packoffset(c11);
  float4 cg_mids_saturation : packoffset(c12);
  float4 cg_mids_contrast : packoffset(c13);
  float4 cg_mids_gamma : packoffset(c14);
  float4 cg_mids_gain : packoffset(c15);
  float4 cg_mids_offset : packoffset(c16);
  float4 cg_highlights_saturation : packoffset(c17);
  float4 cg_highlights_contrast : packoffset(c18);
  float4 cg_highlights_gamma : packoffset(c19);
  float4 cg_highlights_gain : packoffset(c20);
  float4 cg_highlights_offset : packoffset(c21);
  float cg_shadows_max : packoffset(c22);
  float cg_mids_max : packoffset(c22.y);
  float cg_highlights_max : packoffset(c22.z);
  float cg_color_temp : packoffset(c22.w);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState hdr_output_sampler_s : register(s1);
Texture2D<float4> g_hdr_rgb_texture : register(t0);
Texture2D<float4> hdr_output_texture : register(t1);

struct Auto_exposure_output
{
  float tone_mapper_brightness;
  float tone_mapper_debug_average_ev;
  float tone_mapper_debug_raw_target_brightness;
  float tone_mapper_debug_unused;
};

// colorimetry.hlsl
static const float3x3 BT709_TO_XYZ = {
  0.41238656, 0.35759149, 0.18045049,
  0.21263682, 0.71518298, 0.0721802,
  0.01933062, 0.11919716, 0.95037259,
};
static const float3x3 XYZ_TO_BT709 = {
  3.24100326f, -1.53739899f, -0.49861587f,
  -0.96922426f, 1.87592999f, 0.04155422f,
  0.05563942f, -0.2040112f, 1.05714897f,
};
static const float3x3 AP1_TO_XYZ = {
  0.6624541811f, 0.1340042065f, 0.1561876870f,
  0.2722287168f, 0.6740817658f, 0.0536895174f,
  -0.0055746495f, 0.0040607335f, 1.0103391003f,
};
static const float3x3 XYZ_TO_AP1 = {
  1.6410233797f, -0.3248032942f, -0.2364246952f,
  -0.6636628587f, 1.6153315917f, 0.0167563477f,
  0.0117218943f, -0.0082844420f, 0.9883948585f,
};
static const float3 AP1_TO_LUMA = float3(0.2722287168f, 0.6740817658f, 0.0536895174f);

// CIE daylight chromaticity of a color temperature (K).
float2 DChromaticity(float cct)
{
  float t_adj = 1000.f / cct;
  float t_adj2 = t_adj * t_adj;
  float t_adj3 = t_adj2 * t_adj;
  float x = (cct < 7000.f) ? (0.244063f + 0.09911f * t_adj + 2.9678f * t_adj2 + 4.6070f * t_adj3)
                           : (0.237040f + 0.24748f * t_adj + 1.9018f * t_adj2 + 2.0064f * t_adj3);
  return float2(x, -3.f * x * x + 2.87f * x - 0.275f);
}

// Bradford (von Kries) adaptation of XYZ from one daylight temperature to another.
float3 AdaptDIlluminant(float3 xyz, float src_temp, float dst_temp)
{
  const float3x3 cone_response = {
    0.89510f, 0.26640f, -0.16140f,
    -0.75020f, 1.71350f, 0.03670f,
    0.03890f, -0.06850f, 1.02960f,
  };
  const float3x3 inv_cone_response = {
    0.98699f, -0.14705f, 0.15996f,
    0.43230f, 0.51836f, 0.04929f,
    -0.00852f, 0.04004f, 0.96848f,
  };
  float2 src_xy = DChromaticity(src_temp * 1.4388f / 1.438f);
  float2 dst_xy = DChromaticity(dst_temp * 1.4388f / 1.438f);
  float3 src_xyz = float3(src_xy.x, src_xy.y, 1.f - src_xy.x - src_xy.y) / max(src_xy.y, 1e-10f);
  float3 dst_xyz = float3(dst_xy.x, dst_xy.y, 1.f - dst_xy.x - dst_xy.y) / max(dst_xy.y, 1e-10f);
  float3 gain = mul(cone_response, dst_xyz) / mul(cone_response, src_xyz);
  return mul(inv_cone_response, gain * mul(cone_response, xyz));
}

// UE4's FilmToneMap in AP1, per channel, with the game's constants; white_clip sets the shoulder's asymptote
// (1 + white_clip).
float3 FilmToneMap(float3 colour, float white_clip)
{
  const float black_clip = g_tone_mapping_film_black_clip;
  const float toe = g_tone_mapping_film_toe;
  const float slope = g_tone_mapping_film_slope;
  const float shoulder = g_tone_mapping_film_shoulder;

  colour = max(colour, 0.f);
  float toe_scale = 1.f + black_clip - toe;
  float shoulder_scale = 1.f + white_clip - shoulder;
  // Mid gray to itself.
  const float in_match = 0.18f;
  const float out_match = 0.18f;

  float toe_point;
  if (toe < 0.8f) {
    float bt = (out_match + black_clip) / toe_scale - 1.f;
    // The game's "(toe_scale, slope)" is a comma operator: x slope (UE4: x toe_scale / slope). Kept.
    toe_point = -0.5f * log((1 + bt) / (1 - bt)) * slope + log10(in_match);
  } else {
    toe_point = (1 - toe - out_match) / slope + log10(in_match);
  }
  float straight_point = (1.f - toe) / slope - toe_point;
  float shoulder_point = shoulder / slope - straight_point;

  float3 log10_colour = log10(colour);
  float3 straight_colour = slope * (log10_colour + straight_point);
  float3 toe_colour = -black_clip + (2.f * toe_scale) / (1 + exp((-2.f * slope / toe_scale) * (log10_colour - toe_point)));
  float3 shoulder_colour =
      (1.f + white_clip) - (2.f * shoulder_scale) / (1 + exp((2.f * slope / shoulder_scale) * (log10_colour - shoulder_point)));

  toe_colour = (log10_colour < toe_point) ? toe_colour : straight_colour;
  shoulder_colour = (log10_colour > shoulder_point) ? shoulder_colour : straight_colour;

  float3 t = saturate((log10_colour - toe_point) / (shoulder_point - toe_point));
  t = (shoulder_point < toe_point) ? (1.f - t) : t;
  t = (3.f - 2.f * t) * t * t;
  return max(lerp(toe_colour, shoulder_colour, t), 0.f);
}

float3 ColourCorrect(float3 colour, float luma, float3 saturation, float3 contrast, float3 gamma, float3 gain,
                     float3 offset)
{
  colour = max(0.f, lerp(luma.xxx, colour, saturation));
  // 0.18 is mid gray.
  colour = pow(colour * (1.f / 0.18f), contrast) * 0.18f;
  colour = pow(colour, 1.f / gamma);
  return colour * gain + offset;
}

// The 3-way grading, on the tonemapped AP1 color.
float3 GradeAP1(float3 colour)
{
  float luma = dot(colour, AP1_TO_LUMA);
  float shadows_t = 1.f - smoothstep(0.f, cg_shadows_max, luma);
  float highlights_t = smoothstep(cg_mids_max, cg_highlights_max, luma);
  float mids_t = 1.f - shadows_t - highlights_t;
  float3 shadows = ColourCorrect(
      colour, luma,
      cg_shadows_saturation.rgb * cg_shadows_saturation.a * cg_global_saturation.rgb * cg_global_saturation.a,
      cg_shadows_contrast.rgb * cg_shadows_contrast.a * cg_global_contrast.rgb * cg_global_contrast.a,
      cg_shadows_gamma.rgb * cg_shadows_gamma.a * cg_global_gamma.rgb * cg_global_gamma.a,
      cg_shadows_gain.rgb * cg_shadows_gain.a * cg_global_gain.rgb * cg_global_gain.a,
      cg_shadows_offset.rgb * cg_shadows_offset.a + cg_global_offset.rgb * cg_global_offset.a);
  float3 mids = ColourCorrect(
      colour, luma,
      cg_mids_saturation.rgb * cg_mids_saturation.a * cg_global_saturation.rgb * cg_global_saturation.a,
      cg_mids_contrast.rgb * cg_mids_contrast.a * cg_global_contrast.rgb * cg_global_contrast.a,
      cg_mids_gamma.rgb * cg_mids_gamma.a * cg_global_gamma.rgb * cg_global_gamma.a,
      cg_mids_gain.rgb * cg_mids_gain.a * cg_global_gain.rgb * cg_global_gain.a,
      cg_mids_offset.rgb * cg_mids_offset.a + cg_global_offset.rgb * cg_global_offset.a);
  float3 highlights = ColourCorrect(
      colour, luma,
      cg_highlights_saturation.rgb * cg_highlights_saturation.a * cg_global_saturation.rgb * cg_global_saturation.a,
      cg_highlights_contrast.rgb * cg_highlights_contrast.a * cg_global_contrast.rgb * cg_global_contrast.a,
      cg_highlights_gamma.rgb * cg_highlights_gamma.a * cg_global_gamma.rgb * cg_global_gamma.a,
      cg_highlights_gain.rgb * cg_highlights_gain.a * cg_global_gain.rgb * cg_global_gain.a,
      cg_highlights_offset.rgb * cg_highlights_offset.a + cg_global_offset.rgb * cg_global_offset.a);
  return shadows * shadows_t + mids * mids_t + highlights * highlights_t;
}

// AP1 (D60) to BT.709 (D65).
float3 AP1ToBT709(float3 colour)
{
  return mul(XYZ_TO_BT709, AdaptDIlluminant(mul(AP1_TO_XYZ, colour), 6000.f, 6500.f));
}

// HDR (Extended): marat569's UE Filmic Extended (RenoDX UE Extended) without UE's parts this game lacks (RRT, blue
// correction): the curve below mid gray, its tangent at mid gray above it, 37.5% back to the curve (Blend Factor 50).
float3 ExtendedFilm(float3 untonemapped, float mid_gray, float mid_slope)
{
  float3 vanilla = FilmToneMap(untonemapped, g_tone_mapping_film_white_clip);
  float3 tail = mid_gray + mid_slope * (untonemapped - 0.18f);
  return (untonemapped < 0.18f) ? vanilla : lerp(tail, vanilla, 0.375f);
}

// Per channel: unchanged up to the tone mapped mid gray, then a C-infinity shoulder to peak (UE Extended's
// ApplyAnchoredCInfinityShoulder, compression 1.5).
float3 DisplayMap(float3 colour, float anchor, float peak)
{
  const float range = peak - anchor;
  const float3 distance = max(colour - anchor, 0.f);
  const float3 flat_weight = exp2(-range / (1.5f * distance));
  return range * (distance / (distance * flat_weight + range)) + (colour - distance);
}

// BT.709 back to the game's AP1 (white at 6000 K): AP1ToBT709's inverse.
float3 BT709ToAP1(float3 colour)
{
  return mul(XYZ_TO_AP1, AdaptDIlluminant(mul(BT709_TO_XYZ, colour), 6500.f, 6000.f));
}

// PsychoV31's target gamut: Gamut Compression Mode (default BT.2020); SDR output: BT.709.
int TargetGamut()
{
  return (SHOGUN_SDR_OUTPUT ? renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT709 : int(SI.gamut_compression_mode));
}

// PsychoV31 on the untonemapped scene (BT.709), anchored on the curve's mid gray, with its own contrast (v23, ATN:
// v22's match of the curve's, x 1.40 at mid gray, lifted the highlights and needed V31's highlight contrast at 0.86 to
// undo it); target-only where it returns black, then Extended.
// Contrast and Cone Response Exponent as psychotm_test31's (ruse): the cone response's power is their product, and
// the purity is divided by Contrast (Contrast keeps the saturation, the exponent does not). This custom V31's tonal
// power is that response power; its purity is applied before it.
float3 ApplyPsychoV31(float3 untonemapped_ap1, float peak, float mid_gray, float mid_slope)
{
  float3 untonemapped = AP1ToBT709(untonemapped_ap1);
  // Each map's contrast: its curve's slope over the usual 0.88 (every Shogun / Rise of the Samurai map; Fall of the
  // Samurai's campaign map 0.52 in winter - 0.68 in summer, a much flatter curve that PsychoV31 had shown at full
  // contrast: bright snow, dark forest, ATN debug22). On the response power like the cone exponent (the saturation
  // follows, as the game's per-channel curve; the map's grading adds its own back). At 0.88 unchanged.
  const float map_contrast = g_tone_mapping_film_slope / 0.88f;
  const int target_gamut = TargetGamut();
  float response_power = SI.contrast * SI.cone_response_exponent * map_contrast;
  float purity = SI.saturation / max(SI.contrast, 0.01f);
  // Highlight Roll-off: V31's shoulder compression. 0: 1, 1: 1.5 (default), 2: 3.
  float compression = 1.f + 0.5f * SI.roll_off * SI.roll_off;
  float3 result = renodx::tonemap::psychov::custom_psychotm_test31(
      untonemapped, peak, SI.exposure, SI.highlights, SI.shadows,
      response_power, SI.flare, SI.highlight_contrast, SI.shadow_contrast, purity, SI.highlight_saturation,
      SI.dechroma, 0.18f, mid_gray, 0.f, SI.gamut_compression, target_gamut,
      compression, 1.f, 0.5f, 0.f,
      renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 1.f);
  bool failed = all(result == 0.f) && renodx::color::y::from::BT709(untonemapped) > 1e-6f;
  [branch]
  if (failed) {
    result = renodx::tonemap::psychov::custom_psychotm_test31(
        untonemapped, peak, SI.exposure, SI.highlights, SI.shadows,
        response_power, SI.flare, SI.highlight_contrast, SI.shadow_contrast, purity, SI.highlight_saturation,
        SI.dechroma, 0.18f, mid_gray, 0.f, SI.gamut_compression, target_gamut,
        compression, 1.f, 0.5f, 0.f,
        renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 0.f);
    failed = all(result == 0.f);
  }
  [branch]
  if (failed) {
    return DisplayMap(ExtendedFilm(untonemapped_ap1, mid_gray, mid_slope), mid_gray, peak);
  }
  return BT709ToAP1(result);
}

// The game's gamma setting as SDR shows it: encoded with 1 / g_gamma_output (menu Gamma / 10, 2.0 by default), shown
// on a 2.2 display: x^(2.2 / gamma) up to 1, its tangent above.
float3 ApplyGameGamma(float3 colour)
{
  const float exponent = 2.2f * g_inv_gamma_output;
  const float3 magnitude = abs(colour);
  const float3 shown = (magnitude < 1.f) ? pow(magnitude, exponent) : (1.f + exponent * (magnitude - 1.f));
  return sign(colour) * shown;
}

// Within BT.2020 and Peak: past Peak, desaturate keeping luminance and hue; past it in luminance, Peak white.
float3 LimitToPeak(float3 color, float peak)
{
  float3 color_bt2020 = max(renodx::color::bt2020::from::BT709(color), 0.f);
  float y = min(renodx::color::y::from::BT2020(color_bt2020), peak);
  float channel_max = max(color_bt2020.r, max(color_bt2020.g, color_bt2020.b));
  [branch]
  if (channel_max > peak) {
    color_bt2020 = y + (color_bt2020 - y) * saturate((peak - y) / (channel_max - y));
  }
  return renodx::color::bt709::from::BT2020(color_bt2020);
}

// hdr_linear_colour: the scene after distortion / radial blur; bloom: hdr_output_texture (bloom and lens flare
// ghosts, each scaled by its slider in hdrflare00).
float4 HdrToScreen(float3 hdr_linear_colour, float3 bloom, float tone_mapper_brightness)
{
  const bool hdr = HDR >= 1.f;
  hdr_linear_colour += bloom;

  float3 xyz = mul(BT709_TO_XYZ, hdr_linear_colour);
  float ax_brightness = g_use_auto_exposure ? tone_mapper_brightness : g_tone_mapping_brightness;
  ax_brightness *= g_tone_mapping_brightness_fade;
  xyz *= ax_brightness * ax_brightness;
  xyz = AdaptDIlluminant(xyz, 6500.f, cg_color_temp);
  xyz = AdaptDIlluminant(xyz, 6500.f, 6000.f);
  const float3 untonemapped = mul(XYZ_TO_AP1, xyz);

  [branch]
  if (!hdr) {
    float3 colour = AP1ToBT709(GradeAP1(FilmToneMap(untonemapped, g_tone_mapping_film_white_clip)));
    colour = pow(saturate(colour), g_inv_gamma_output);
    colour = renodx::color::gamma::DecodeSafe(colour) * SHOGUN_PAPER_WHITE;
    return float4(renodx::color::gamma::EncodeSafe(colour), 1.f);
  }

  const float peak = SHOGUN_PEAK;
  const float white_clip = g_tone_mapping_film_white_clip;
  const float mid_gray = FilmToneMap(float3(0.18f, 0.18f, 0.18f), white_clip).x;
  const float eps = 0.18f / 1024.f;
  const float mid_slope = (FilmToneMap((0.18f + eps).xxx, white_clip).x - FilmToneMap((0.18f - eps).xxx, white_clip).x)
                          / (2.f * eps);
  float3 colour;
  float3 scene;  // the curve's input (AP1), for the hue shift
  float hue_shift;
  float saturation_shift = 0.f;
  [branch]
  if (HDR < 2.f) {
    // User grading (Extended sliders, as rysesonofrome): PsychoV31's grade on the untonemapped scene (all at 1:
    // skipped, the game's look), the game's curve, its grading and the shoulder.
    float3 graded = untonemapped;
    [branch]
    if (SI.ext_exposure != 1.f || SI.ext_highlights != 1.f || SI.ext_shadows != 1.f || SI.ext_contrast != 1.f
        || SI.ext_saturation != 1.f || SI.ext_highlight_saturation != 1.f) {
      graded = BT709ToAP1(renodx::tonemap::psychov::custom_psychograde_test31(
          AP1ToBT709(untonemapped), SI.ext_exposure, SI.ext_highlights, SI.ext_shadows, SI.ext_contrast, 0.f, 1.f, 1.f,
          SI.ext_saturation, SI.ext_highlight_saturation, 0.f, 0.18f, 0.18f,
          renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709));
    }
    [branch]
    if (SHOGUN_SDR_OUTPUT) {
      colour = GradeAP1(FilmToneMap(graded, white_clip));
    } else {
      colour = DisplayMap(GradeAP1(ExtendedFilm(graded, mid_gray, mid_slope)), mid_gray, peak);
    }
    scene = graded;
    hue_shift = (SHOGUN_SDR_OUTPUT ? 0.f : SI.ext_highlight_hue_shift);
  } else {
    colour = GradeAP1(ApplyPsychoV31(untonemapped, peak, mid_gray, mid_slope));
    scene = untonemapped * SI.exposure;
    hue_shift = SI.highlight_hue_shift;
    saturation_shift = SI.highlight_saturation_shift;
  }
  colour = AP1ToBT709(colour);
  // Highlight Hue Shift (as Tales of Arise): the game's SDR curve works per channel and turns bright colors toward
  // orange / yellow early; the HDR curves keep the scene's hue much longer (pinkish fire). Above mid gray, fully from
  // scene white (1.0), the hue moves toward the SDR image's (curve and grading) by the slider (OkLab; brightness and
  // chroma kept).
  const float scene_y = dot(scene, AP1_TO_LUMA);
  [branch]
  if ((hue_shift > 0.f || saturation_shift > 0.f) && scene_y > 0.18f) {
    const float t = saturate(log2(scene_y / 0.18f) / log2(1.f / 0.18f));
    const float weight = t * t * (3.f - 2.f * t);
    const float3 sdr = AP1ToBT709(GradeAP1(FilmToneMap(scene, white_clip)));
    colour = renodx::color::correct::Hue(colour, sdr, hue_shift * weight);
    // Highlight Saturation Shift (PsychoV31): the SDR image's saturation as well (SDR fire stays saturated orange /
    // yellow up to its brightest, V31 fades its cores to white).
    [branch]
    if (saturation_shift > 0.f) {
      colour = renodx::tonemap::psychov::ShiftSaturationToward(colour, sdr, saturation_shift * weight);
    }
  }
  colour = ApplyGameGamma(colour);
  // PsychoV31: the hue shift and the game's grading come after its gamut compression and can leave the target again.
  [branch]
  if (HDR >= 2.f && SI.gamut_compression > 0.f) {
    colour = renodx::tonemap::psychov::FitToTargetGamut(colour, TargetGamut());
  }
  [branch]
  if (SHOGUN_SDR_OUTPUT) {
    colour = saturate(colour);
  } else {
    colour = LimitToPeak(colour, SHOGUN_PEAK) * SHOGUN_PAPER_WHITE;
  }
  return float4(renodx::color::gamma::EncodeSafe(colour), 1.f);
}
