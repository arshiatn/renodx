// Ryse: Son of Rome tonemap (CryEngine HDRFinalPass). From 0xB9B672AA.ps_5_0.cso (3Dmigoto).
// Stock: auto exposure x vignette, bloom lerp (HDRBloomColor), saturation + color balance,
// Hable filmic (A, B, E, W = HDRFilmCurve; C 0.1, D 0.2, F 0.3), saturate, sRGB, sun shafts
// screen-blended, 16^3 color chart (256x16, sRGB in and out). Alpha: _tex3.z.
// SDR: stock. HDR (Extended): UC2 Extended on the game's Hable (Neutwo shoulder
// to Peak, SDR's colors). HDR (PsychoV31): PsychoV31
// instead of Hable, anchored on Hable's mid gray. Both keep exposure, bloom, color balance,
// sun shafts and the chart. sRGB-on-2.2 display emulation, gamma 2.2 encoded out. HDR: the shaft
// value past 1 (stock saturate) as light, rolled off below Peak.
#include "../shared.h"
#include "../psycho_test31.hlsli"
#include "../uncharted2extended.hlsli"

cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams1 : packoffset(c0);
  float4 HDRColorBalance : packoffset(c1);
  float4 SunShafts_SunCol : packoffset(c2);
  float4 HDREyeAdaptation : packoffset(c3);
  float4 HDRFilmCurve : packoffset(c4);
  float4 HDRParams0 : packoffset(c5);
  float4 HDRBloomColor : packoffset(c6);
  float4 HDRParams2 : packoffset(c7);
  float4 PS_ScreenSize : packoffset(c8);
}

SamplerState baseMap_s : register(s0);
SamplerState lumMap_s : register(s1);
SamplerState bloomMap0_s : register(s2);
SamplerState _tex3_s : register(s3);
SamplerState vignettingMap_s : register(s7);
SamplerState colorChartMap_s : register(s8);
SamplerState sunshaftsMap_s : register(s9);
Texture2D<float4> baseMap : register(t0);
Texture2D<float4> lumMap : register(t1);
Texture2D<float4> bloomMap0 : register(t2);
Texture2D<float4> _tex3 : register(t3);
Texture2D<float4> vignettingMap : register(t7);
Texture2D<float4> colorChartMap : register(t8);
Texture2D<float4> sunshaftsMap : register(t9);

#define cmp -

// Hable with the stock constants, / curve(W), unclamped.
float3 FilmCurve(float3 x)
{
  float4 c = float4(x, HDRFilmCurve.w);
  float2 k = float2(0.100000001, 0.200000003) * HDRFilmCurve.yz;
  float4 f = (c * (HDRFilmCurve.x * c + k.x) + k.y) / (c * (HDRFilmCurve.x * c + HDRFilmCurve.y) + 0.0599999987);
  f -= HDRFilmCurve.z * 3.33333325;
  return f.xyz / f.w;
}

float FilmCurveY(float x)
{
  return FilmCurve(float3(x, x, x)).x;
}

// 16^3 chart, 256x16 unwrapped, stock sampling.
float3 SampleChart(float3 encoded)
{
  float4 r0, r1;
  r0.yzw = encoded * float3(0.9375, 0.9375, 0.9375) + float3(0.03125, 0.03125, 0);
  r1.x = 16 * r0.w;
  r1.x = frac(r1.x);
  r0.w = r0.w * 16 + -r1.x;
  r0.y = r0.y + r0.w;
  r0.x = 0.0625 * r0.y;
  r0.yw = float2(0.0625, 0) + r0.xz;
  float3 lower = colorChartMap.Sample(colorChartMap_s, r0.xz).xyz;
  float3 upper = colorChartMap.Sample(colorChartMap_s, r0.yw).xyz;
  return (upper - lower) * r1.x + lower;
}

// Sun shafts: the stock screen in sRGB, smooth past white (RyseScreen; main adds the shafts past 1). Chart: colors past 1 are normalized by their max channel, graded, scaled back. Out-of-gamut
// (negative) parts pass through.
float3 ApplyShaftsAndChart(float3 color, float3 shafts)
{
  // Stock: saturate(c + s * (1 - c)). s passes 1 near the sun.
  color = RyseDecodeSRGB(RyseScreen(RyseEncodeSRGB(color), shafts));

  float scale = max(renodx::math::Max(color), 1.f);
  float3 normalized = color / scale;
  float3 inside = saturate(normalized);
  float3 graded = RyseDecodeSRGB(SampleChart(RyseEncodeSRGB(inside)));
  return (graded + normalized - inside) * scale;
}

// SDR's color at HDR luminance: the SDR color scaled to it, never darker than SDR. Only the light HDR
// adds on top of SDR keeps the HDR color's saturation; also where the scaled SDR color would pass Peak.
float3 MatchSDRColor(float3 sdr, float3 hdr, float peak)
{
  float y_sdr = renodx::color::y::from::BT709(sdr);
  float y_hdr = renodx::color::y::from::BT709(hdr);
  if (!(y_sdr > 0.f && y_hdr > 0.f)) return hdr;
  if (y_hdr <= y_sdr) return sdr;
  float3 scaled = sdr * (y_hdr / y_sdr);
  float3 need = max(scaled - max(peak, hdr), 0.f) / max(scaled - hdr, 1e-10f);
  float hdr_share = max(saturate(1.f - y_sdr / y_hdr), saturate(max(need.r, max(need.g, need.b))));
  return lerp(scaled, hdr, hdr_share);
}

// HDR (Extended): UC2 Extended (Musa/Jon): Hable up to its knee (third derivative root), its tangent
// there above it. A Neutwo shoulder to Peak above 1. Then SDR's color.
float3 ApplyExtended(float3 untonemapped, float3 sdr, float peak)
{
  float coeffs[6] = { HDRFilmCurve.x, HDRFilmCurve.y, 0.1f, 0.2f, HDRFilmCurve.z, 0.3f };
  float white_precompute = 1.f / renodx::tonemap::ApplyCurve(HDRFilmCurve.w, coeffs[0], coeffs[1], coeffs[2],
                                                             coeffs[3], coeffs[4], coeffs[5]);
  Uncharted2::Config::Uncharted2ExtendedConfig uc2_config =
      Uncharted2::Config::CreateUncharted2ExtendedConfig(coeffs, white_precompute);
  // No usable knee (odd curve constants): plain Hable, only the shoulder below adds HDR.
  if (!(uc2_config.pivot_point > 0.f && Uncharted2::Derivative(uc2_config.pivot_point, coeffs[0], coeffs[1], coeffs[2],
                                                                coeffs[3], coeffs[4], coeffs[5]) > 0.f)) {
    uc2_config.pivot_point = HDRFilmCurve.w;
  }
  float3 color = max(Uncharted2::ApplyExtended(untonemapped, FilmCurve(untonemapped), uc2_config), 0.f);

  float channel_max = renodx::math::Max(color);
  [branch]
  if (channel_max > 1.f) {
    color *= (1.f + renodx::tonemap::Neutwo(channel_max - 1.f, max(peak, 1.0001f) - 1.f)) / channel_max;
  }
  return MatchSDRColor(sdr, color, peak);
}

// HDR (PsychoV31): the game's grading and PsychoV31 only, anchored on Hable's mid gray. Extended
// where V31 fails.
float3 ApplyPsychoV31(float3 untonemapped, float peak)
{
  float anchor_out = FilmCurveY(0.18f);
  anchor_out = (anchor_out > 0.f) ? anchor_out : 0.18f;
  float y_in = max(renodx::color::y::from::BT709(untonemapped), 1e-7f);
  // Highlight Roll-off: V31's shoulder compression. 0: 1, 1: 1.5 (default), 2: 3.
  float compression = 1.f + 0.5f * SI.roll_off * SI.roll_off;

  float3 result = renodx::tonemap::psychov::custom_psychotm_test31(
      untonemapped, peak, SI.exposure, SI.highlights, SI.shadows,
      SI.contrast, 0.f, 1.f, 1.f, SI.saturation, SI.highlight_saturation, 0.f,
      0.18f, anchor_out, 0.f, 1.f, renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT2020,
      compression, 1.f, 0.5f, 0.f,
      renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 1.f);
  // V31 can return black for saturated blues (its source cage, compiler dependent): redo target-only.
  bool failed = all(result == 0.f) && y_in > 1e-6f;
  [branch]
  if (failed) {
    result = renodx::tonemap::psychov::custom_psychotm_test31(
        untonemapped, peak, SI.exposure, SI.highlights, SI.shadows,
        SI.contrast, 0.f, 1.f, 1.f, SI.saturation, SI.highlight_saturation, 0.f,
        0.18f, anchor_out, 0.f, 1.f, renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT2020,
        compression, 1.f, 0.5f, 0.f,
        renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 0.f);
    failed = all(result == 0.f);
  }
  // V31 still black (high contrast): Extended instead.
  [branch]
  if (failed) {
    float3 exposed = untonemapped * SI.exposure;
    return ApplyExtended(exposed, saturate(FilmCurve(exposed)), peak);
  }

  // Game Shadows: Hable's toe instead of V31's power law below mid gray, faded in over the stop
  // below it. Contrast and Shadows still apply.
  float y_exposed = y_in * SI.exposure;
  [branch]
  if (SI.game_shadows != 0.f && y_exposed < 0.18f) {
    float toe = FilmCurveY(y_exposed) / (anchor_out * y_exposed / 0.18f);
    float weight = smoothstep(0.f, 1.f, log2(0.18f / y_exposed));
    result *= pow(max(toe, 0.f), SI.contrast * weight);
  }
  return result;
}

// Within BT.2020 and Peak: past Peak, desaturate keeping luminance and hue; past it in luminance,
// Peak white.
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

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  const bool hdr = HDR >= 1.f;

  // Exposure (auto, clamped) x vignette.
  r0.x = lumMap.Sample(lumMap_s, v1.xy).x;
  r0.x = -HDRParams1.x + r0.x;
  r0.x = HDRParams0.x * r0.x + HDRParams1.x;
  r0.y = 1 + r0.x;
  r0.y = log2(r0.y);
  r0.y = 2 + r0.y;
  r0.y = 2 / r0.y;
  r0.y = 1.02999997 + -r0.y;
  r0.x = r0.y / r0.x;
  r0.x = max(HDREyeAdaptation.y, r0.x);
  r0.x = min(HDREyeAdaptation.z, r0.x);
  r0.x = HDRParams2.z * r0.x;
  r0.y = vignettingMap.Sample(vignettingMap_s, v1.xy).x;
  if (hdr) r0.y = 1.f - saturate((1.f - r0.y) * SI.vignette);
  r0.x = r0.y * r0.x;
  // Bloom (lerp), saturation, color balance.
  r0.yzw = saturate(HDRBloomColor.xyz);
  if (hdr) r0.yzw = saturate(r0.yzw * SI.bloom);
  r1.xyz = bloomMap0.Sample(bloomMap0_s, v1.xy).xyz;
  r2.xyz = baseMap.Sample(baseMap_s, v3.xy).xyz;
  r1.xyz = -r2.xyz * HDRParams2.yyy + r1.xyz;
  r2.xyz = HDRParams2.yyy * r2.xyz;
  r0.yzw = r0.yzw * r1.xyz + r2.xyz;
  r1.xyz = r0.xxx * r0.yzw;
  r1.x = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.xyz = r0.xxx * r0.yzw + -r1.xxx;
  r0.xyz = HDRColorBalance.www * r0.xyz + r1.xxx;
  r0.xyz = HDRColorBalance.xyz * r0.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz);

  const float alpha = _tex3.Sample(_tex3_s, v3.xy).z;

  [branch]
  if (!hdr) {
    // Stock: Hable, sRGB, sun shafts, chart.
    r1.xy = float2(0.100000001,0.200000003) * HDRFilmCurve.yz;
    r0.w = HDRFilmCurve.w;
    r2.xyzw = HDRFilmCurve.xxxx * r0.xyzw + r1.xxxx;
    r1.xyzw = r0.xyzw * r2.xyzw + r1.yyyy;
    r2.xyzw = HDRFilmCurve.xxxx * r0.xyzw + HDRFilmCurve.yyyy;
    r0.xyzw = r0.xyzw * r2.xyzw + float4(0.0599999987,0.0599999987,0.0599999987,0.0599999987);
    r0.xyzw = r1.xyzw / r0.xyzw;
    r0.xyzw = -HDRFilmCurve.zzzz * float4(3.33333325,3.33333325,3.33333325,3.33333325) + r0.xyzw;
    r0.xyz = saturate(r0.xyz / r0.www);
    r1.xyz = log2(r0.xyz);
    r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
    r2.xyz = cmp(r0.xyz < float3(0.00313080009,0.00313080009,0.00313080009));
    r0.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
    r0.xyz = saturate(r2.xyz ? r0.xyz : r1.xyz);
    r1.xyz = float3(1,1,1) + -r0.xyz;
    r2.xyz = sunshaftsMap.Sample(sunshaftsMap_s, v3.xy).xyz;
    r2.xyz = SunShafts_SunCol.xyz * r2.xyz;
    r1.xyz = r2.xyz * r1.xyz;
    r0.xyz = saturate(r1.xyz * HDRParams2.zzz + r0.xyz);
    o0.xyz = SampleChart(r0.xyz);
    o0.w = alpha;
    return;
  }

  // Peak before the sRGB-on-2.2 display emulation below.
  const float peak = renodx::color::correct::Gamma(RYSE_PEAK, true);
  const float3 shafts = SunShafts_SunCol.xyz * sunshaftsMap.Sample(sunshaftsMap_s, v3.xy).xyz
                        * HDRParams2.z * SI.sun_shafts;

  float3 color;
  [branch]
  if (HDR < 2.f) {
    // HDR (Extended). V31 grading; all at 1: skipped, so below the white point it is SDR.
    float3 untonemapped = r0.xyz;
    [branch]
    if (SI.ext_exposure != 1.f || SI.ext_highlights != 1.f || SI.ext_shadows != 1.f || SI.ext_contrast != 1.f
        || SI.ext_saturation != 1.f || SI.ext_highlight_saturation != 1.f) {
      untonemapped = renodx::tonemap::psychov::custom_psychograde_test31(
          untonemapped, SI.ext_exposure, SI.ext_highlights, SI.ext_shadows, SI.ext_contrast, 0.f, 1.f, 1.f,
          SI.ext_saturation, SI.ext_highlight_saturation, 0.f, 0.18f, 0.18f,
          renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709);
    }
    float3 sdr = saturate(FilmCurve(untonemapped));
    color = renodx::color::correct::GammaSafe(ApplyShaftsAndChart(ApplyExtended(untonemapped, sdr, peak), shafts));
    // SDR's hue (as displayed) for SDR's share of the light; the light HDR adds keeps its own.
    float3 sdr_display = renodx::color::correct::GammaSafe(ApplyShaftsAndChart(sdr, shafts));
    float y_color = renodx::color::y::from::BT709(color);
    color = renodx::color::correct::HueOKLab(
        color, sdr_display,
        (y_color > 0.f) ? saturate(renodx::color::y::from::BT709(sdr_display) / y_color) : 1.f);
  } else {
    color = renodx::color::correct::GammaSafe(ApplyShaftsAndChart(ApplyPsychoV31(r0.xyz, peak), shafts));
  }

  // The shafts past 1 (near the sun), saturated by the stock, added on top (both HDR modes).
  color = RyseAddLight(color, max(shafts - 1.f, 0.f), RYSE_PEAK);
  color = LimitToPeak(color, RYSE_PEAK);
  o0.xyz = renodx::color::gamma::EncodeSafe(color);
  o0.w = alpha;
}
