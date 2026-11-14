// Hue Clip's default for the games without a chain here (tests.cpp part 5): each game's kind of stock SDR curve on the
// scene, PsychoV35 (twattila/psycho35.hlsli, Attila's SI: its own sliders at their defaults) after Hue Clip toward that
// curve, as the games' shaders call them. run.ps1 compiles it with Attila's folder as the include path (/I).
// curve: 0 luminance Extended Reinhard (white 4) then each channel clipped (WH2; WH3's form), 1 luminance Hable then
// the clip (Rome Remastered), 2 the clip alone (RUSE), 3 Hable on each channel (Ryse), 4 UE's film curve on each AP1
// channel (Tales of Arise, Expeditions: Rome, Shogun 2's), 5 the ACES fit on each channel (Troy, Pharaoh), 6 Warhammer
// II's t00 (2333E142) as it runs: its Extended Reinhard x(1 + x / W) / (1 + x) with W = 1 / burn - 0.999 (`white`), the
// handoff anchored at its mid gray and log-log slope, Hue Clip toward the stock luminance / handoff, PsychoV35 at 0.18
// (its hdr_to_screen with the grade at identity), 7 Warhammer's tone mappers (twwarhammer/tone_mapper.hlsli: t00 D275C403,
// t09 7759B956, t10 9C8E40E6) as they run with the exposure's black 0 and white 1 and the S-curve at identity: Hue Clip
// toward the luminance clamped at white (Attila's engine form without its highlight limit), PsychoV35 at 0.18; their SDR
// picture the stock path: the whiteout toward white from white + `whiteout_start` to white + `whiteout_end`, the
// luminance clamped at white, each channel clipped. 8 Pharaoh's t00 (976C1C74) as it runs, `white` its burn: the ACES fit
// with its lift and black, the handoff anchored at mid gray, Hue Clip toward the stock.
// sdr 1: the game's SDR picture (its curve, each channel clipped at 1).
#include "shared.h"
#include "psycho35.hlsli"

Texture2D<float4> scene : register(t0);

cbuffer curve_params : register(b0) {
  float curve;
  float strength;
  float sdr;
  float white;  // curve 6: W
  float whiteout_start;  // curve 7: g_whiteout_start, g_whiteout_end
  float whiteout_end;
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
  if (kind == 6) {
    // twwarhammer2/shaders/t00_0x2333E142.ps_5_0.hlsl's HDR path (exposure 1).
    const float inv_w = 1.f / max(white, 0.00010000763f);
    const float stock_y = y * (1.f + y * inv_w) / (1.f + y);
    if (sdr != 0.f) return float4(saturate(c * ((y > 0.f) ? stock_y / y : 0.f)), 1.f);
    const float x_mid = 0.36f / (0.82f + sqrt(0.6724f + 0.72f * inv_w));
    const float slope = 1.f + (x_mid * inv_w) / (1.f + x_mid * inv_w) - x_mid / (1.f + x_mid);
    const float handoff_y = 0.18f * pow(max(y / x_mid, 0.f), slope);
    hued = c * ((y > 0.f) ? handoff_y / y : 0.f);
    if (strength > 0.f && handoff_y > 0.f) hued = renodx::tonemap::psychov::HueClip(hued, stock_y / handoff_y, strength);
  } else if (kind == 7) {
    if (sdr != 0.f) {
      // t00_0xD275C403.ps_4_0.hlsl's stock path (bloom in c).
      const float high = max(max(c.r, c.g), max(c.b, 0.00100000005f));
      const float whiteout = saturate((y - (1.f + whiteout_start)) / (whiteout_end - whiteout_start));
      const float3 whited = (1.f - c / high) * whiteout * y + c;
      const float whited_y = renodx::color::y::from::BT709(whited);
      return float4(saturate(whited * ((whited_y > 0.f) ? clamp(whited_y, 0.f, 1.f) / whited_y : 1.f)), 1.f);
    }
    hued = (strength > 0.f) ? renodx::tonemap::psychov::AttilaEngineHueClip(c, y, c, 0.f, 0.f, 1.f, false, strength) : c;
  } else if (kind == 8) {
    // Pharaoh's t00 (twpharaohdynasties/shaders/t00_0x976C1C74.ps_5_0.hlsl) as it runs, exposure 1 and
    // post_processing_strength 0 (both traced): its ACES fit on each channel of the scene plus the lift (0.25 x burn,
    // `white`), its black at the lift taken off; the handoff anchored where the fit reaches mid gray plus that black; Hue Clip
    // toward the stock; PsychoV35 at 0.18 (t01 with the grade at identity).
    const float lift = 0.25f * white;
    const float black = saturate(AcesFit(lift.xxx).x);
    const float3 stock = saturate(AcesFit(max(c, 0.f) + lift)) - black;
    if (sdr != 0.f) return float4(saturate(stock), 1.f);
    const float mid_target = min(0.18f + black, 1.02f);
    const float qa = 2.51f - 2.43f * mid_target;
    const float qb = 0.03f - 0.59f * mid_target;
    const float qc = 0.14f * mid_target;
    const float x_mid = (-qb + sqrt(max(qb * qb + 4.f * qa * qc, 0.f))) / (2.f * max(qa, 0.000001f));
    hued = c * (0.18f / max(x_mid - lift, 0.000001f));
    if (strength > 0.f) hued = renodx::tonemap::psychov::HueTransfer(hued, saturate(stock), strength);
  } else if (kind <= 2) {
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
