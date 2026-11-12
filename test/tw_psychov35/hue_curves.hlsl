// Hue Clip's default for the games without a chain here (tests.cpp part 5): each game's kind of stock SDR curve on the
// scene, PsychoV35 (twattila/psycho35.hlsli, Attila's SI: its own sliders at their defaults) after Hue Clip toward that
// curve, as the games' shaders call them. run.ps1 compiles it with Attila's folder as the include path (/I).
// curve: 0 luminance Extended Reinhard (white 4) then each channel clipped (WH2; WH3's form), 1 luminance Hable then
// the clip (Rome Remastered), 2 the clip alone (RUSE), 3 Hable on each channel (Ryse), 4 UE's film curve on each AP1
// channel (Tales of Arise, Expeditions: Rome, Shogun 2's), 5 the ACES fit on each channel (Troy, Pharaoh).
// sdr 1: the game's SDR picture (its curve, each channel clipped at 1).
#include "shared.h"
#include "psycho35.hlsli"

Texture2D<float4> scene : register(t0);

cbuffer curve_params : register(b0) {
  float curve;
  float strength;
  float sdr;
  float unused;
};

static const float HABLE_W = 11.2f;

float3 Hable(float3 x) {
  return ((x * (0.15f * x + 0.05f) + 0.004f) / (x * (0.15f * x + 0.5f) + 0.06f)) - 0.02f / 0.3f;
}

// UE4's film curve with UE's defaults: slope 0.88, toe 0.55, shoulder 0.26, black clip 0, white clip 0.04 (Shogun 2's
// FilmToneMap is the same with its toe point's x slope).
float3 UEFilm(float3 colour) {
  const float black_clip = 0.f;
  const float toe = 0.55f;
  const float slope = 0.88f;
  const float shoulder = 0.26f;
  const float white_clip = 0.04f;
  colour = max(colour, 1e-10f);
  const float toe_scale = 1.f + black_clip - toe;
  const float shoulder_scale = 1.f + white_clip - shoulder;
  const float bt = (0.18f + black_clip) / toe_scale - 1.f;
  const float toe_point = log10(0.18f) - 0.5f * log((1 + bt) / (1 - bt)) * (toe_scale / slope);
  const float straight_point = (1.f - toe) / slope - toe_point;
  const float shoulder_point = shoulder / slope - straight_point;
  const float3 log10_colour = log10(colour);
  const float3 straight_colour = slope * (log10_colour + straight_point);
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

float3 AcesFit(float3 x) {
  return x * (2.51f * x + 0.03f) / (x * (2.43f * x + 0.59f) + 0.14f);
}

// The luminance curves' value for luminance `y` (curves 0 - 2).
float LuminanceCurve(float y, int kind) {
  if (kind == 0) return y * (1.f + y / 16.f) / (1.f + y);
  if (kind == 1) return Hable(y.xxx).x / Hable(HABLE_W.xxx).x;
  return y;
}

// The per-channel curves (3 - 5), unclipped.
float3 ChannelCurve(float3 c, int kind) {
  if (kind == 3) return Hable(max(c, 0.f)) / Hable(HABLE_W.xxx).x;
  if (kind == 4) return renodx::color::bt709::from::AP1(UEFilm(renodx::color::ap1::from::BT709(c)));
  return AcesFit(max(c, 0.f));
}

float4 main(float4 position : SV_Position) : SV_Target {
  const float3 c = scene.Load(int3(position.xy, 0)).xyz;
  const int kind = int(curve + 0.5f);
  const float y = renodx::color::y::from::BT709(c);
  float3 hued;
  float mid_gray = 0.18f;
  [branch]
  if (kind <= 2) {
    const float clip_scale = (y > 0.f) ? LuminanceCurve(y, kind) / y : 0.f;
    if (sdr != 0.f) return float4(saturate(c * clip_scale), 1.f);
    hued = renodx::tonemap::psychov::HueClip(c, clip_scale, strength);
  } else {
    if (sdr != 0.f) return float4(saturate(ChannelCurve(c, kind)), 1.f);
    hued = renodx::tonemap::psychov::HueTransfer(c, saturate(ChannelCurve(c, kind)), strength);
    // Ryse, Tales, Expeditions and Shogun 2 anchor PsychoV35 on their curve's mid gray.
    if (kind != 5) mid_gray = renodx::color::y::from::BT709(ChannelCurve(0.18f.xxx, kind));
  }
  return float4(ApplyPsychoV35(hued, 1000.f / 203.f, false, mid_gray), 1.f);
}
