#ifndef SRC_TALESOFARISE_LUTBUILDER_HLSLI_
#define SRC_TALESOFARISE_LUTBUILDER_HLSLI_

// Tales of Arise (UE4 4.21) CombineLUTs (32^3 volume LUT), shared by shaders/lut00-04 (0-4 SDR LUTs). Same code as
// Expeditions: Rome's builders except two HDR output-device branches (not used here). Helpers extracted from the
// stock code (3Dmigoto).
// Stock: white balance -> grading (AP1) -> film/legacy curve -> SDR LUTs -> post grade -> sRGB / 1.05.
// SDR mode: stock LUT headroom; the tonemappers apply the stock target clip and Paper White / UI.
// HDR modes (sRGB output device only, alpha tag 1 for the tonemappers): the curve is replaced on the
// graded AP1 color (without UE's Expand Gamut), the SDR LUTs and post grade (incl. fades) are kept:
//   Extended: marat569's UE Filmic Extended from RenoDX UE Extended (./ue_extended.hlsli).
//   PsychoV31, like Pharaoh: Dynasties: the artist's grading + PsychoV31 only (optionally the game's toe).
// Both: sRGB-on-2.2 display emulation (GammaSafe), gamma 2.2 encoded.

#include "./shared.h"
#include "./psycho_test31.hlsli"
#include "../twattila/psycho35.hlsli"
#include "./ue_extended.hlsli"

cbuffer cb0 : register(b0)
{
  float4 cb0[67];
}

#define cmp -

// Film path before the curve (AP1 in and out): blue correction (cb0[66].x), RRT glow and red
// modifier (AP0), max 0, pre-desaturation 0.96. Stock code.
float3 FilmWorkingAP1(float3 ap1)
{
  float4 r0,r1,r2,r3,r4;
  r0.xyz = ap1;
  r0.w = 0;
  r1 = 0; r2 = 0; r3 = 0; r4 = 0;

  r3.x = dot(float3(0.938639402,1.02359565e-010,0.0613606237), r0.xyz);
  r3.y = dot(float3(8.36008554e-011,0.830794156,0.169205874), r0.xyz);
  r3.z = dot(float3(2.13187367e-012,-5.63307213e-012,1), r0.xyz);
  r3.xyz = r3.xyz + -r0.xyz;
  r0.xyz = cb0[66].xxx * r3.xyz + r0.xyz;
  r3.y = dot(float3(0.695452213,0.140678704,0.163869068), r0.xyz);
  r3.z = dot(float3(0.0447945632,0.859671116,0.0955343172), r0.xyz);
  r3.w = dot(float3(-0.00552588236,0.00402521016,1.00150073), r0.xyz);
  r0.x = min(r3.y, r3.z);
  r0.x = min(r0.x, r3.w);
  r0.y = max(r3.y, r3.z);
  r0.y = max(r0.y, r3.w);
  r0.xyz = max(float3(1.00000001e-010,1.00000001e-010,0.00999999978), r0.xyy);
  r0.x = r0.y + -r0.x;
  r0.x = r0.x / r0.z;
  r0.yzw = r3.wzy + -r3.zyw;
  r0.yz = r3.wz * r0.yz;
  r0.y = r0.y + r0.z;
  r0.y = r3.y * r0.w + r0.y;
  r0.y = sqrt(r0.y);
  r0.z = r3.w + r3.z;
  r0.z = r0.z + r3.y;
  r0.y = r0.y * 1.75 + r0.z;
  r0.w = -0.400000006 + r0.x;
  r1.w = 2.5 * r0.w;
  r1.w = 1 + -abs(r1.w);
  r1.w = max(0, r1.w);
  r2.w = cmp(0 < r0.w);
  r0.w = cmp(r0.w < 0);
  r0.w = (int)-r2.w + (int)r0.w;
  r0.w = (int)r0.w;
  r1.w = -r1.w * r1.w + 1;
  r0.w = r0.w * r1.w + 1;
  r0.zw = float2(0.333333343,0.0250000004) * r0.yw;
  r1.w = cmp(0.159999996 >= r0.y);
  r0.y = cmp(r0.y >= 0.479999989);
  r0.z = 0.0799999982 / r0.z;
  r0.z = -0.5 + r0.z;
  r0.z = r0.w * r0.z;
  r0.y = r0.y ? 0 : r0.z;
  r0.y = r1.w ? r0.w : r0.y;
  r0.y = 1 + r0.y;
  r4.yzw = r3.yzw * r0.yyy;
  r0.zw = cmp(r4.zw == r4.yz);
  r0.z = r0.w ? r0.z : 0;
  r0.w = r3.z * r0.y + -r4.w;
  r0.w = 1.73205078 * r0.w;
  r1.w = r4.y * 2 + -r4.z;
  r1.w = -r3.w * r0.y + r1.w;
  r2.w = min(abs(r1.w), abs(r0.w));
  r3.x = max(abs(r1.w), abs(r0.w));
  r3.x = 1 / r3.x;
  r2.w = r3.x * r2.w;
  r3.x = r2.w * r2.w;
  r3.z = r3.x * 0.0208350997 + -0.0851330012;
  r3.z = r3.x * r3.z + 0.180141002;
  r3.z = r3.x * r3.z + -0.330299497;
  r3.x = r3.x * r3.z + 0.999866009;
  r3.z = r3.x * r2.w;
  r3.w = cmp(abs(r1.w) < abs(r0.w));
  r3.z = r3.z * -2 + 1.57079637;
  r3.z = r3.w ? r3.z : 0;
  r2.w = r2.w * r3.x + r3.z;
  r3.x = cmp(r1.w < -r1.w);
  r3.x = r3.x ? -3.141593 : 0;
  r2.w = r3.x + r2.w;
  r3.x = min(r1.w, r0.w);
  r0.w = max(r1.w, r0.w);
  r1.w = cmp(r3.x < -r3.x);
  r0.w = cmp(r0.w >= -r0.w);
  r0.w = r0.w ? r1.w : 0;
  r0.w = r0.w ? -r2.w : r2.w;
  r0.w = 57.2957802 * r0.w;
  r0.z = r0.z ? 0 : r0.w;
  r0.w = cmp(r0.z < 0);
  r1.w = 360 + r0.z;
  r0.z = r0.w ? r1.w : r0.z;
  r0.z = max(0, r0.z);
  r0.z = min(360, r0.z);
  r0.w = cmp(180 < r0.z);
  r1.w = -360 + r0.z;
  r0.z = r0.w ? r1.w : r0.z;
  r0.z = 0.0148148146 * r0.z;
  r0.z = 1 + -abs(r0.z);
  r0.z = max(0, r0.z);
  r0.w = r0.z * -2 + 3;
  r0.z = r0.z * r0.z;
  r0.z = r0.w * r0.z;
  r0.z = r0.z * r0.z;
  r0.x = r0.z * r0.x;
  r0.y = -r3.y * r0.y + 0.0299999993;
  r0.x = r0.x * r0.y;
  r4.x = r0.x * 0.180000007 + r4.y;
  r0.x = dot(float3(1.45143926,-0.236510754,-0.214928567), r4.xzw);
  r0.y = dot(float3(-0.0765537769,1.17622972,-0.0996759236), r4.xzw);
  r0.z = dot(float3(0.00831614807,-0.00603244966,0.997716308), r4.xzw);
  r0.xyz = max(float3(0,0,0), r0.xyz);
  r0.w = dot(r0.xyz, float3(0.272228718,0.674081743,0.0536895171));
  r0.xyz = r0.xyz + -r0.www;
  r0.xyz = r0.xyz * float3(0.959999979,0.959999979,0.959999979) + r0.www;
  return r0.xyz;
}

// Film path after the curve: post-desaturation 0.93, inverse blue correction (cb0[66].x),
// AP1 -> linear BT.709. Stock code, without the final sRGB gamut clip.
float3 FilmOutputBT709(float3 tone_ap1)
{
  float4 r0,r3;
  r0.xyz = tone_ap1;
  r0.w = 0;
  r3 = 0;

  r0.w = dot(r0.xyz, float3(0.272228718,0.674081743,0.0536895171));
  r0.xyz = r0.xyz + -r0.www;
  r0.xyz = r0.xyz * float3(0.930000007,0.930000007,0.930000007) + r0.www;
  r0.xyz = max(float3(0,0,0), r0.xyz);
  r3.x = dot(float3(1.06537485,1.44678506e-006,-0.0653710067), r0.xyz);
  r3.y = dot(float3(-3.45525592e-007,1.20366347,-0.203667715), r0.xyz);
  r3.z = dot(float3(1.9865448e-008,2.12079581e-008,0.999999583), r0.xyz);
  r3.xyz = r3.xyz + -r0.xyz;
  r0.xyz = cb0[66].xxx * r3.xyz + r0.xyz;
  r3.x = dot(float3(1.70505154,-0.621790707,-0.0832583979), r0.xyz);
  r3.y = dot(float3(-0.130257145,1.14080286,-0.0105485283), r0.xyz);
  r3.z = dot(float3(-0.0240032747,-0.128968775,1.15297174), r0.xyz);
  return r3.xyz;
}

// Graded AP1 -> linear BT.709. Film curve (cb0[36], cb0[37].x, blue correction cb0[66].x)
// or the legacy one (`film` false). Stock code.
float3 ToneCurve(float3 ap1, bool film)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  r0.xyz = ap1;
  r0.w = 0;
  r1 = 0; r2 = 0; r3 = 0; r4 = 0; r5 = 0; r6 = 0; r7 = 0; r8 = 0;

  r1.x = dot(float3(1.70505154,-0.621790707,-0.0832583979), r0.xyz);
  r1.y = dot(float3(-0.130257145,1.14080286,-0.0105485283), r0.xyz);
  r1.z = dot(float3(-0.0240032747,-0.128968775,1.15297174), r0.xyz);
  [branch]
  if (!film) {
    r2.x = dot(r1.xyz, cb0[28].xyz);
    r2.y = dot(r1.xyz, cb0[29].xyz);
    r2.z = dot(r1.xyz, cb0[30].xyz);
    r0.w = dot(r1.xyz, cb0[33].xyz);
    r0.w = 1 + r0.w;
    r0.w = rcp(r0.w);
    r3.xyz = cb0[35].xyz * r0.www + cb0[34].xyz;
    r2.xyz = r3.xyz * r2.xyz;
    r2.xyz = max(float3(0,0,0), r2.xyz);
    r3.xyz = cb0[31].xxx + -r2.xyz;
    r3.xyz = max(float3(0,0,0), r3.xyz);
    r4.xyz = max(cb0[31].zzz, r2.xyz);
    r2.xyz = max(cb0[31].xxx, r2.xyz);
    r2.xyz = min(cb0[31].zzz, r2.xyz);
    r5.xyz = r4.xyz * cb0[32].xxx + cb0[32].yyy;
    r4.xyz = cb0[31].www + r4.xyz;
    r4.xyz = rcp(r4.xyz);
    r6.xyz = cb0[28].www * r3.xyz;
    r3.xyz = cb0[31].yyy + r3.xyz;
    r3.xyz = rcp(r3.xyz);
    r3.xyz = r6.xyz * r3.xyz + cb0[29].www;
    r2.xyz = r2.xyz * cb0[30].www + r3.xyz;
    r2.xyz = r5.xyz * r4.xyz + r2.xyz;
    r2.xyz = float3(-0.00200000009,-0.00200000009,-0.00200000009) + r2.xyz;
  } else {
    r0.xyz = FilmWorkingAP1(r0.xyz);
    r3.xy = float2(1,0.180000007) + cb0[36].ww;
    r0.w = -cb0[36].y + r3.x;
    r1.w = 1 + cb0[37].x;
    r2.w = -cb0[36].z + r1.w;
    r3.x = cmp(0.800000012 < cb0[36].y);
    r3.zw = float2(0.819999993,1) + -cb0[36].yy;
    r3.zw = r3.zw / cb0[36].xx;
    r3.z = -0.744727492 + r3.z;
    r3.y = r3.y / r0.w;
    r4.x = -1 + r3.y;
    r4.x = 1 + -r4.x;
    r3.y = r3.y / r4.x;
    r3.y = log2(r3.y);
    r3.y = 0.346573591 * r3.y;
    r4.x = r0.w / cb0[36].x;
    r3.y = -r3.y * r4.x + -0.744727492;
    r3.x = r3.x ? r3.z : r3.y;
    r3.y = r3.w + -r3.x;
    r3.z = cb0[36].z / cb0[36].x;
    r3.z = r3.z + -r3.y;
    r0.xyz = log2(r0.xyz);
    r4.xyz = float3(0.30103001,0.30103001,0.30103001) * r0.xyz;
    r5.xyz = r0.xyz * float3(0.30103001,0.30103001,0.30103001) + r3.yyy;
    r5.xyz = cb0[36].xxx * r5.xyz;
    r3.y = r0.w + r0.w;
    r3.w = -2 * cb0[36].x;
    r0.w = r3.w / r0.w;
    r6.xyz = r0.xyz * float3(0.30103001,0.30103001,0.30103001) + -r3.xxx;
    r7.xyz = r6.xyz * r0.www;
    r7.xyz = float3(1.44269502,1.44269502,1.44269502) * r7.xyz;
    r7.xyz = exp2(r7.xyz);
    r7.xyz = float3(1,1,1) + r7.xyz;
    r7.xyz = r3.yyy / r7.xyz;
    r7.xyz = -cb0[36].www + r7.xyz;
    r0.w = r2.w + r2.w;
    r3.y = cb0[36].x + cb0[36].x;
    r2.w = r3.y / r2.w;
    r0.xyz = r0.xyz * float3(0.30103001,0.30103001,0.30103001) + -r3.zzz;
    r0.xyz = r2.www * r0.xyz;
    r0.xyz = float3(1.44269502,1.44269502,1.44269502) * r0.xyz;
    r0.xyz = exp2(r0.xyz);
    r0.xyz = float3(1,1,1) + r0.xyz;
    r0.xyz = r0.www / r0.xyz;
    r0.xyz = r1.www + -r0.xyz;
    r8.xyz = cmp(r4.xyz < r3.xxx);
    r7.xyz = r8.xyz ? r7.xyz : r5.xyz;
    r4.xyz = cmp(r3.zzz < r4.xyz);
    r0.xyz = r4.xyz ? r0.xyz : r5.xyz;
    r0.w = r3.z + -r3.x;
    r4.xyz = saturate(r6.xyz / r0.www);
    r0.w = cmp(r3.z < r3.x);
    r3.xyz = float3(1,1,1) + -r4.xyz;
    r3.xyz = r0.www ? r3.xyz : r4.xyz;
    r4.xyz = -r3.xyz * float3(2,2,2) + float3(3,3,3);
    r3.xyz = r3.xyz * r3.xyz;
    r3.xyz = r3.xyz * r4.xyz;
    r0.xyz = r0.xyz + -r7.xyz;
    r0.xyz = r3.xyz * r0.xyz + r7.xyz;
    r2.xyz = max(float3(0,0,0), FilmOutputBT709(r0.xyz));
  }
  return r2.xyz;
}

// The game's curve: UE's film tonemapper or, cb0[35].w == 0, its legacy one.
float3 ToneCurve(float3 ap1)
{
  return ToneCurve(ap1, cb0[35].w != 0.f);
}

// UE's film tonemapper (r.TonemapperFilm 1) with its gamut expansion: the game's choice (cb0[35].w != 0), or SDR's
// Tonemapper option (UE Filmic) over the game's legacy one. HDR keeps the game's curve as its reference.
bool UseFilmCurve(bool hdr_lut)
{
  // Needs the film settings, which UE fills either way (slope 0: missing).
  return cb0[35].w != 0.f || (!hdr_lut && SI.sdr_curve != 0.f && cb0[36].x > 0.f);
}

// UE's legacy tonemapper (r.TonemapperFilm 0; FilmPostProcess: color matrix, shadow tint, then a darks / midtones /
// highlights curve per channel), extended: its highlight segment (from Cm1, cb0[31].z) replaced by the midtone line
// (slope Cm2, cb0[30].w) continued: identical to the game's SDR curve below Cm1, unbounded above. Graded AP1 in,
// linear BT.709 out (as ToneCurve).
float3 LegacyCurveExtended(float3 ap1)
{
  const float3 bt709 = renodx::color::bt709::from::AP1(ap1);
  float3 matrix_color = float3(dot(bt709, cb0[28].xyz), dot(bt709, cb0[29].xyz), dot(bt709, cb0[30].xyz));
  matrix_color = max(0.f, matrix_color * (cb0[35].xyz * rcp(1.f + dot(bt709, cb0[33].xyz)) + cb0[34].xyz));
  const float3 darks_input = max(0.f, cb0[31].x - matrix_color);
  const float3 darks = cb0[28].w * darks_input * rcp(cb0[31].y + darks_input) + cb0[29].w;
  const float highlights_at_start = (cb0[31].z * cb0[32].x + cb0[32].y) * rcp(cb0[31].w + cb0[31].z);
  return highlights_at_start + max(matrix_color, cb0[31].x) * cb0[30].w + darks - 0.002f;
}

// Highlight Hue Shift (PsychoV31, UE Extended): the game's SDR curve works per channel and shifts bright colors early
// (fire toward orange / yellow); V31 and the extended curves keep the source hue much longer (pinkish fire). Above mid
// gray, fully from scene white (1.0), the hue is moved toward the SDR curve's hue by `amount` (OkLab; brightness and
// chroma kept). `graded_ap1`: the curve input, `scene_y` its luminance.
// Highlight Saturation Shift (PsychoV31; ATN's experiment, debug21): the same for the saturation (OkLab chroma /
// lightness): SDR fire stays saturated yellow up to its brightest (blue 0), V31 fades its cores to white at their own
// hue (pink). Chroma / lightness, as V31 is brighter.
float3 ShiftHighlightHue(float3 color, float3 graded_ap1, float scene_y, float amount, float saturation_amount)
{
  [branch]
  if ((amount <= 0.f && saturation_amount <= 0.f) || scene_y <= 0.18f) return color;
  const float t = saturate(log2(scene_y / 0.18f) / log2(1.f / 0.18f));
  const float weight = t * t * (3.f - 2.f * t);
  const float3 sdr = ToneCurve(graded_ap1);
  float3 shifted = renodx::color::correct::Hue(color, sdr, amount * weight);
  [branch]
  if (saturation_amount > 0.f) {
    float3 lab = renodx::color::oklab::from::BT709(shifted);
    const float3 sdr_lab = renodx::color::oklab::from::BT709(sdr);
    const float saturation = length(lab.yz) / max(lab.x, 1e-6f);
    const float sdr_saturation = length(sdr_lab.yz) / max(sdr_lab.x, 1e-6f);
    // At most 4x: a near-gray V31 color has no hue of its own to scale.
    const float scale = min(renodx::math::DivideSafe(sdr_saturation, saturation, 1.f), 4.f);
    lab.yz *= lerp(1.f, scale, saturation_amount * weight);
    shifted = renodx::color::bt709::from::OkLab(lab);
  }
  return shifted;
}

// Neutral input (AP1 neutral = BT.709 neutral).
float GameCurveY(float x)
{
  return renodx::color::y::from::BT709(ToneCurve(x.xxx));
}

// Stock post grade without its [0, 1] / sRGB gamut assumptions: polynomial cb0[26] (linear
// beyond [0, 1]), color scale cb0[42], overlay cb0[43] (fades), signed inverse gamma cb0[27].y.
float3 PostGradeHDR(float3 color)
{
  float3 x = saturate(color);
  float3 mapped = (cb0[26].x * x + cb0[26].y) * x + cb0[26].z
                  + cb0[26].y * min(color, 0.f)
                  + (2.f * cb0[26].x + cb0[26].y) * max(color - 1.f, 0.f);
  mapped = lerp(mapped * cb0[42].yzw, cb0[43].xyz, cb0[43].w);
  return renodx::math::SignPow(mapped, cb0[27].y);
}

// Extended's grading sliders: PsychoV31 grading on the graded AP1 color before the curve; all at 1: skipped.
// Highlight Saturation per base curve (ATN: legacy 0.92, UE Filmic 1.05).
float3 ApplyExtendedGrading(float3 ap1, float highlight_saturation)
{
  [branch]
  if (SI.ext_exposure == 1.f && SI.ext_highlights == 1.f && SI.ext_shadows == 1.f && SI.ext_contrast == 1.f
      && SI.ext_saturation == 1.f && highlight_saturation == 1.f) {
    return ap1;
  }
  return renodx::color::ap1::from::BT709(renodx::tonemap::psychov::custom_psychograde_test31(
      renodx::color::bt709::from::AP1(ap1), SI.ext_exposure, SI.ext_highlights, SI.ext_shadows, SI.ext_contrast,
      0.f, 1.f, 1.f, SI.ext_saturation, highlight_saturation, 0.f, 0.18f, 0.18f,
      renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_AP1));
}

// The game's film curve (cb0[36], cb0[37].x) with black clip 0, UE Extended's override. UE's default film settings
// if they are missing (slope 0).
ue_extended::FilmCurve GameFilmCurve()
{
  if (!(cb0[36].x > 0.f)) return ue_extended::CreateFilmCurve(0.88f, 0.55f, 0.26f, 0.f, 0.04f);
  return ue_extended::CreateFilmCurve(cb0[36].x, cb0[36].y, cb0[36].z, 0.f, cb0[37].x);
}

// PsychoV31's target gamut: Gamut Compression Mode (default BT.2020); SDR output: BT.709 (the proxy clips per
// channel as the game).
int TargetGamut()
{
  return (TOA_SDR_OUTPUT ? renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT709 : int(SI.gamut_compression_mode));
}

// Where the game's curve puts mid gray (PsychoV31's and PsychoV35's anchor).
float GameMidGray()
{
  const float mid_gray = GameCurveY(0.18f);
  return (mid_gray > 0.f) ? mid_gray : 0.18f;
}

// HDR (PsychoV31), like Pharaoh: Dynasties: the artist's grading and PsychoV31 only, anchored on the
// game curve's mid gray, with its own contrast, shadows and colors. Extended where V31 fails.
float3 ApplyPsychoV31(float3 untonemapped_ap1, float peak)
{
  float3 untonemapped_bt709 = renodx::color::bt709::from::AP1(untonemapped_ap1);
  const float anchor_out = GameMidGray();
  float y_in = max(renodx::color::y::from::BT709(untonemapped_bt709), 1e-7f);
  // Highlight Roll-off: V31's shoulder compression. 0: 1, 1: 1.5 (default), 2: 3.
  float compression = 1.f + 0.5f * SI.roll_off * SI.roll_off;
  const int target_gamut = TargetGamut();

  float3 result = renodx::tonemap::psychov::custom_psychotm_test31(
      untonemapped_bt709, peak, SI.exposure, SI.highlights, SI.shadows,
      SI.contrast, SI.flare, SI.highlight_contrast, SI.shadow_contrast, SI.saturation, SI.highlight_saturation,
      SI.dechroma, 0.18f, anchor_out, 0.f, SI.gamut_compression, target_gamut,
      compression, 1.f, 0.5f, 0.f,
      renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_AP1, 1.f);
  // V31 can return black for saturated blues (its source cage, compiler dependent): redo target-only.
  bool failed = all(result == 0.f) && y_in > 1e-6f;
  [branch]
  if (failed) {
    result = renodx::tonemap::psychov::custom_psychotm_test31(
        untonemapped_bt709, peak, SI.exposure, SI.highlights, SI.shadows,
        SI.contrast, SI.flare, SI.highlight_contrast, SI.shadow_contrast, SI.saturation, SI.highlight_saturation,
        SI.dechroma, 0.18f, anchor_out, 0.f, SI.gamut_compression, target_gamut,
        compression, 1.f, 0.5f, 0.f,
        renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_AP1, 0.f);
    failed = all(result == 0.f);
  }
  // V31 still black (high contrast): Extended instead, its post grade after the display mapping here (legacy
  // curve: SDR).
  [branch]
  if (failed) {
    if (cb0[35].w == 0.f) return ToneCurve(untonemapped_ap1 * SI.exposure);
    const ue_extended::FilmCurve curve = GameFilmCurve();
    return ue_extended::DisplayMap(ue_extended::ToneMap(untonemapped_ap1 * SI.exposure, curve),
                                   ue_extended::ToneMap(0.18f.xxx, curve), peak);
  }

  // Highlight Hue Shift: V31 keeps hues until its shoulder compresses.
  float y_exposed = y_in * SI.exposure;
  result = ShiftHighlightHue(result, untonemapped_ap1 * SI.exposure, y_exposed, SI.highlight_hue_shift,
                             SI.highlight_saturation_shift);

  // Game Shadows: the game's toe instead of V31's power law below mid gray, faded in over the stop
  // below it (the curve is steeper at mid gray). Contrast and Shadows still apply.
  [branch]
  if (SI.game_shadows != 0.f && y_exposed < 0.18f) {
    float toe = GameCurveY(y_exposed) / (anchor_out * y_exposed / 0.18f);
    float weight = smoothstep(0.f, 1.f, log2(0.18f / y_exposed));
    result *= pow(max(toe, 0.f), SI.contrast * weight);
  }
  return result;
}

// Within BT.2020 and Peak (the hue match can pass both for saturated colors, grading sliders the top
// input): past Peak, desaturate keeping luminance and hue; past it in luminance, Peak white.
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

// UE Extended's post grade (its ApplyMappingAndOverlay): mapping polynomial cb0[26], color scale cb0[42], overlay
// cb0[43] (fades), before its display mapping. Plus the game's gamma cb0[27].y, which UE Extended leaves out.
float3 UEExtendedPostGrade(float3 color)
{
  float3 mapped = cb0[42].yzw * ((cb0[26].y + cb0[26].x * color) * color + cb0[26].z);
  mapped = lerp(mapped, cb0[43].xyz, cb0[43].w);
  return renodx::math::SignPow(mapped, cb0[27].y);
}

// The artist's SDR LUTs (post process volume Color Grading LUTs; TOA_SDR_LUT_COUNT 1-4 in the 01-04 builders): 16^3
// unwrapped 256x16 textures, blended by LUTWeights (cb0[38..42].x) on the sRGB-encoded [0, 1] tone mapped color.
#ifndef TOA_SDR_LUT_COUNT
#define TOA_SDR_LUT_COUNT 0
#endif

#if TOA_SDR_LUT_COUNT > 0
Texture2D<float4> t0 : register(t0);
SamplerState s0_s : register(s0);
#endif
#if TOA_SDR_LUT_COUNT > 1
Texture2D<float4> t1 : register(t1);
SamplerState s1_s : register(s1);
#endif
#if TOA_SDR_LUT_COUNT > 2
Texture2D<float4> t2 : register(t2);
SamplerState s2_s : register(s2);
#endif
#if TOA_SDR_LUT_COUNT > 3
Texture2D<float4> t3 : register(t3);
SamplerState s3_s : register(s3);
#endif

#if TOA_SDR_LUT_COUNT > 0
float3 SampleSDRLut(Texture2D<float4> lut, SamplerState lut_sampler, float2 uv, float slice_blend)
{
  return lerp(lut.Sample(lut_sampler, uv).xyz, lut.Sample(lut_sampler, uv + float2(0.0625, 0)).xyz, slice_blend);
}

// Stock blend on linear [0, 1] input, linear output.
float3 BlendSDRLuts(float3 color)
{
  const float3 encoded = renodx::color::srgb::Encode(saturate(color));
  const float3 uvw = encoded * 0.9375 + 0.03125;
  const float slice = uvw.z * 16 - 0.5;
  const float slice_floor = floor(slice);
  const float2 uv = float2((uvw.x + slice_floor) * 0.0625, uvw.y);
  const float slice_blend = slice - slice_floor;
  float3 blended = cb0[38].x * encoded + cb0[39].x * SampleSDRLut(t0, s0_s, uv, slice_blend);
#if TOA_SDR_LUT_COUNT > 1
  blended += cb0[40].x * SampleSDRLut(t1, s1_s, uv, slice_blend);
#endif
#if TOA_SDR_LUT_COUNT > 2
  blended += cb0[41].x * SampleSDRLut(t2, s2_s, uv, slice_blend);
#endif
#if TOA_SDR_LUT_COUNT > 3
  blended += cb0[42].x * SampleSDRLut(t3, s3_s, uv, slice_blend);
#endif
  return renodx::color::srgb::Decode(max(6.10351999e-005, blended));
}
#endif

#if TOA_SDR_LUT_COUNT > 0
// LUT Scaling, ported from marat569's UE Extended (filmiclutbuilder.hlsli) at its defaults (Hue Preserving, 100%, gamma
// correction 2.2): the LUTs' raised black (their output at black) removed, their common raise, less where tinted,
// released quickly with brightness; never below the input.
float3 CompensateLUTBlackRaise(float3 graded, float3 source, float3 black_output)
{
  const float common_offset = max(renodx::math::Min(black_output), 0.f);
  const float3 channel_residual = black_output - common_offset;
  const float source_magnitude = sqrt(dot(source, source) / 3.f);
  const float source_half_weight_units = renodx::math::DivideSafe(source_magnitude, common_offset, 0.f) * 2.f;
  const float source_release_denominator = 1.f + 0.5f * source_half_weight_units * (1.f + source_half_weight_units);
  const float common_offset_squared_magnitude = 3.f * common_offset * common_offset;
  const float compensation_weight = renodx::math::DivideSafe(
      common_offset_squared_magnitude,
      source_release_denominator * (common_offset_squared_magnitude + dot(channel_residual, channel_residual)),
      0.f);
  const float removable_common_offset = max(renodx::math::Min(graded - source), 0.f);
  return graded - min(common_offset * compensation_weight, removable_common_offset);
}
#endif

// HDR: the LUTs on a tone mapped HDR color, as RenoDX's UE Extended applies them at its defaults (LUT Strength and LUT
// Scaling 100%, Hue Preserving): its adaptive D65 gamut compression into BT.709 (and back), then above 1 the color is
// divided by its max channel (Ryse's chart), hue kept. Identity without LUTs.
float3 ApplySDRLuts(float3 color)
{
#if TOA_SDR_LUT_COUNT > 0
  const float3 adaptive_state_lms = renodx::color::lms::from::BT709(0.18f.xxx);
  const float gamut_scale =
      renodx::color::gamut::ComputeGamutCompressionScaleBT709AdaptiveD65(color, adaptive_state_lms, 1.f);
  color = renodx::color::gamut::GamutCompressBT709AdaptiveD65(color, adaptive_state_lms, gamut_scale);
  const float max_channel = max(renodx::math::Max(color), 1.f);
  const float3 lut_input = color / max_channel;
  float3 graded = BlendSDRLuts(lut_input);
  const float3 black_output = BlendSDRLuts(0.f.xxx);
  [branch]
  if (renodx::color::y::from::BT709(black_output) > 0.f) {
    graded = renodx::color::correct::GammaSafe(
        CompensateLUTBlackRaise(renodx::color::correct::GammaSafe(graded), renodx::color::correct::GammaSafe(lut_input),
                                renodx::color::correct::GammaSafe(black_output)),
        true);
  }
  return renodx::color::gamut::GamutDecompressBT709AdaptiveD65(graded * max_channel, adaptive_state_lms, gamut_scale);
#else
  return color;
#endif
}

float4 GenerateHDRLut(float3 untonemapped_ap1)
{
  // Peak before the 2.2 display emulation below.
  float peak = renodx::color::correct::Gamma(TOA_PEAK, true);

  float3 color;
  [branch]
  if (HDR < 2.f) {
    // HDR (UE Extended): the grading sliders, then UE Extended with the game's post grade before its display
    // mapping; the mid gray anchor takes the same path (Exposure moves it too, as UE Extended's).
    // Base Curve: UE's film curve (cb0[36], cb0[37].x), which RenoDX's UE Extended addon always uses (UE fills the
    // film settings either way); or, when the game's SDR uses UE's legacy tonemapper (cb0[35].w == 0), that curve
    // extended (LegacyCurveExtended), blended back like UE Extended's (Blend Factor 50).
    const bool legacy_curve = cb0[35].w == 0.f && SI.ext_curve == 0.f;
    const float highlight_saturation = (legacy_curve ? SI.ext_highlight_saturation : SI.ext_filmic_highlight_saturation);
    const float3 graded_ap1 = ApplyExtendedGrading(untonemapped_ap1, highlight_saturation);
    const float3 mid_gray_ap1 = ApplyExtendedGrading(0.18f.xxx, highlight_saturation);
    float3 tonemapped;
    float3 mid_gray_tonemapped;
    [branch]
    if (legacy_curve) {
      tonemapped = lerp(LegacyCurveExtended(graded_ap1), ToneCurve(graded_ap1), ue_extended::VANILLA_BLEND);
      mid_gray_tonemapped =
          lerp(LegacyCurveExtended(mid_gray_ap1), ToneCurve(mid_gray_ap1), ue_extended::VANILLA_BLEND);
    } else {
      const ue_extended::FilmCurve curve = GameFilmCurve();
      tonemapped = ue_extended::ToneMap(graded_ap1, curve);
      mid_gray_tonemapped = ue_extended::ToneMap(mid_gray_ap1, curve);
    }
    // Each base curve its own amount (ATN: legacy 0.20, UE Filmic 0.34).
    tonemapped = ShiftHighlightHue(tonemapped, graded_ap1, renodx::color::y::from::AP1(graded_ap1),
                                   legacy_curve ? SI.ext_highlight_hue_shift : SI.ext_filmic_highlight_hue_shift, 0.f);
    float3 graded = UEExtendedPostGrade(ApplySDRLuts(tonemapped));
    float3 mid_gray = UEExtendedPostGrade(ApplySDRLuts(mid_gray_tonemapped));
    color = renodx::color::correct::GammaSafe(ue_extended::DisplayMap(graded, mid_gray, peak));
  } else {
    // HDR (PsychoV31, PsychoV35): the game's post grade after the complete tone curve (fades must not be undone by the
    // HDR shoulder).
    float3 tonemapped;
    float gamut_compression;
    int target_gamut;
    [branch]
    if (HDR >= 3.f) {
    // PsychoV35 (twattila/psycho35.hlsli) on the game curve's mid gray; Hue Clip Emulation toward the hue the game's
    // curve gives each channel, clipped (it replaces V31's highlight shifts).
    float3 untonemapped_bt709 = renodx::color::bt709::from::AP1(untonemapped_ap1);
    [branch]
    if (SI.hue_clip > 0.f) {
      untonemapped_bt709 = renodx::tonemap::psychov::HueTransfer(untonemapped_bt709, saturate(ToneCurve(untonemapped_ap1)),
                                                                 SI.hue_clip);
    }
      tonemapped = ApplyPsychoV35(untonemapped_bt709, peak, TOA_SDR_OUTPUT, GameMidGray());
      gamut_compression = SI.v35_gamut_compression;
      target_gamut = (TOA_SDR_OUTPUT ? renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT709
                                     : renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT2020);
    } else {
      tonemapped = ApplyPsychoV31(untonemapped_ap1, peak);
      gamut_compression = SI.gamut_compression;
      target_gamut = TargetGamut();
    }
    color = renodx::color::correct::GammaSafe(PostGradeHDR(ApplySDRLuts(tonemapped)));
    // Highlight Hue Shift and the game's grading come after the gamut compression and can leave the target again:
    // such colors are brought back to its edge (luminance and hue kept); colors inside stay as they are.
    [branch]
    if (gamut_compression > 0.f) {
      float3x3 target_to_lms = renodx::color::macleod_boynton::BT709_TO_LMS_WEIGHTED_MAT;
      switch (target_gamut) {
        case renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT2020:
          target_to_lms = renodx::color::macleod_boynton::BT2020_TO_LMS_WEIGHTED_MAT;
          break;
        case renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_DISPLAY_P3:
          target_to_lms = mul(renodx::color::macleod_boynton::BT709_TO_LMS_WEIGHTED_MAT,
                              renodx::color::DISPLAYP3_TO_BT709_MAT);
          break;
      }
      color = mul(renodx::color::macleod_boynton::LMS_WEIGHTED_TO_BT709_MAT,
                  renodx::color::gamut::GamutCompressWeightedLMSCoreRGBBoundFromWeightedInput(
                      mul(renodx::color::macleod_boynton::BT709_TO_LMS_WEIGHTED_MAT, color), target_to_lms, 1.f));
    }
  }

  color = LimitToPeak(color, TOA_PEAK);
  color *= TOA_PAPER_WHITE;
  // / 1.05; alpha 1 tags the HDR LUT for its consumers.
  return float4(renodx::color::gamma::EncodeSafe(color) * 0.952381015f, 1.f);
}

#endif  // SRC_TALESOFARISE_LUTBUILDER_HLSLI_
