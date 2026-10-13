#ifndef SRC_EXPEDITIONSROME_UE_EXTENDED_HLSLI_
#define SRC_EXPEDITIONSROME_UE_EXTENDED_HLSLI_

// HDR (Extended): UE Filmic Extended from marat569's UE Extended (src/games/ue-extended:
// lutbuilder/filmtonemap.hlsli, filmiclutbuilder.hlsli, lutbuildercommon.hlsli, common.hlsli) at its defaults:
// Tonemap Scaling AP1, Blend Factor 50, film black clip 0, Expand Gamut 0 (lut00), blue correction 0.6 with its exact
// inverse, post-LUT grading off (identity at its defaults), SDR EOTF emulation 2.2 (lut00).

namespace ue_extended {

static const float BLUE_CORRECTION = 0.6f;  // their FORCE_BLUE_CORRECT
static const float VANILLA_BLEND = 0.375f;  // Blend Factor 50: lerp(0.75, 0, 0.5)

// Pumbo's blue correction (red and green toward blue); BlueCorrectionPost is its exact inverse.
float3 BlueCorrectionPre(float3 ap1) {
  const float r = lerp(1.f, 0.9387864139f, BLUE_CORRECTION);
  const float g = lerp(1.f, 0.8308209395f, BLUE_CORRECTION);
  return float3(lerp(ap1.b, ap1.r, r), lerp(ap1.b, ap1.g, g), ap1.b);
}

float3 BlueCorrectionPost(float3 ap1) {
  const float r = lerp(1.f, 0.9387864139f, BLUE_CORRECTION);
  const float g = lerp(1.f, 0.8308209395f, BLUE_CORRECTION);
  return float3(lerp(ap1.b, ap1.r, rcp(r)), lerp(ap1.b, ap1.g, rcp(g)), ap1.b);
}

// UE's film curve (FilmSlope, FilmToe, FilmShoulder, FilmBlackClip, FilmWhiteClip) and its derived thresholds.
struct FilmCurve {
  float slope;
  float toe;
  float shoulder;
  float black_clip;
  float white_clip;
  float toe_width;
  float shoulder_width;
  float log_toe_threshold;
  float log_mid_anchor;
  float log_shoulder_threshold;
};

FilmCurve CreateFilmCurve(float slope, float toe, float shoulder, float black_clip, float white_clip) {
  FilmCurve curve;
  curve.slope = slope;
  curve.toe = toe;
  curve.shoulder = shoulder;
  curve.black_clip = black_clip;
  curve.white_clip = white_clip;
  curve.toe_width = (black_clip + 1.f) - toe;
  curve.shoulder_width = (white_clip + 1.f) - shoulder;
  if (toe > 0.8f) {
    curve.log_toe_threshold = ((0.82f - toe) / slope) - 0.7447274923324585f;
  } else {
    const float toe_norm = (black_clip + 0.18f) / curve.toe_width;
    curve.log_toe_threshold = -0.7447274923324585f
                              - (log2(toe_norm / (2.f - toe_norm)) * 0.3465735912322998f) * (curve.toe_width / slope);
  }
  curve.log_mid_anchor = ((1.f - toe) / slope) - curve.log_toe_threshold;
  curve.log_shoulder_threshold = (shoulder / slope) - curve.log_mid_anchor;
  return curve;
}

float3 ApplyFilmCurve(float3 x, FilmCurve c) {
  const float3 x_log = log2(x) * 0.3010300099849701f;
  const float3 straight = c.slope * (x_log + c.log_mid_anchor);
  const float3 toe_offset = x_log - c.log_toe_threshold;
  const float3 toe = (x_log < c.log_toe_threshold)
                         ? ((c.toe_width * 2.f) / (exp2((toe_offset * 1.4426950216293335f) * ((c.slope * -2.f) / c.toe_width)) + 1.f))
                               - c.black_clip
                         : straight;
  const float3 shoulder_offset = x_log - c.log_shoulder_threshold;
  const float3 shoulder =
      (x_log > c.log_shoulder_threshold)
          ? (1.f + c.white_clip)
                - ((c.shoulder_width * 2.f)
                   / (exp2((shoulder_offset * 1.4426950216293335f) * ((c.slope * 2.f) / c.shoulder_width)) + 1.f))
          : straight;
  const float3 t_linear = saturate(toe_offset / (c.log_shoulder_threshold - c.log_toe_threshold));
  const float3 t = (c.log_shoulder_threshold < c.log_toe_threshold) ? (1.f - t_linear) : t_linear;
  return ((t * t) * (shoulder - toe)) * (3.f - (t * 2.f)) + toe;
}

// The film curve below mid gray; above it, its tangent at mid gray (output there taken as 0.18): no shoulder.
float3 ApplyFilmCurveExtended(float3 x, float3 vanilla, FilmCurve c) {
  const float eps = 0.18f / 1024.f;
  const float slope = (ApplyFilmCurve((0.18f + eps).xxx, c).x - ApplyFilmCurve((0.18f - eps).xxx, c).x) / (2.f * eps);
  const float3 tail = slope * (x - 0.18f) + 0.18f;
  return (x < 0.18f) ? vanilla : tail;
}

// Graded AP1 -> tone mapped linear BT.709 (their ApplyFilmicToneMap, AP1 path; no ToneCurveAmount in this game).
float3 ToneMap(float3 graded_ap1, FilmCurve curve) {
  const float3 corrected = BlueCorrectionPre(max(graded_ap1, 0.f));
  const float3 rrt = renodx::tonemap::aces::RRT(mul(renodx::color::AP1_TO_AP0_MAT, corrected));
  const float3 vanilla = ApplyFilmCurve(rrt, curve);
  float3 tonemapped = lerp(ApplyFilmCurveExtended(rrt, vanilla, curve), vanilla, VANILLA_BLEND);
  tonemapped = max(0.f, lerp(renodx::color::y::from::AP1(tonemapped), tonemapped, 0.93f));
  return renodx::color::bt709::from::AP1(max(0.f, BlueCorrectionPost(tonemapped)));
}

// Per channel in blue-corrected AP1: identity up to the tone mapped mid gray, then a C-infinity shoulder to `peak`
// (their ApplyAnchoredCInfinityShoulder, compression 1.5); then each BT.2020 channel at most `peak`.
float3 DisplayMap(float3 color_bt709, float3 mid_gray_bt709, float peak) {
  const float3 color = max(0.f, BlueCorrectionPre(renodx::color::ap1::from::BT709(color_bt709)));
  const float3 anchor = max(0.f, BlueCorrectionPre(renodx::color::ap1::from::BT709(mid_gray_bt709)));
  const float3 range = peak - anchor;
  const float3 distance = max(color - anchor, 0.f);
  const float3 flat_weight = exp2(-range / (1.5f * distance));
  const float3 mapped = range * (distance / (distance * flat_weight + range)) + (color - distance);
  const float3 mapped_bt709 = renodx::color::bt709::from::AP1(BlueCorrectionPost(mapped));
  return renodx::color::bt709::from::BT2020(min(renodx::color::bt2020::from::BT709(mapped_bt709), peak));
}

}  // namespace ue_extended

#endif  // SRC_EXPEDITIONSROME_UE_EXTENDED_HLSLI_
