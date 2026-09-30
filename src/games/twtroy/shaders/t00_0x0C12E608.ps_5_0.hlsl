// TROY t00 - tone mapping.
//
// DECOMPILER BUG, corrected here. Asm 3 is
//     mul r0.yz, cb1[0].xxyx, l(0, 0.125, 0.25, 0)
// so r0.z = 0.25 * BURN. 3Dmigoto collapses the .xxyx swizzle and uses brightness for
// both, which breaks the lift/black-offset cancellation and greys the frame.
//
// Stock:  gain = 0.125*brightness,  lift = 0.25*burn
//         display = saturate(AcesFit(scene*adapt*gain + lift)) - saturate(AcesFit(lift))
// The subtracted term is the curve at the lift, so black always lands on exactly 0.
// burn is a shadow-lift / contrast knob, not a black level.
//
// MODES
//   0  native SDR, untouched.
//   1  faithful. Stock curve inside a reversible NeuTwo proxy so its saturate() never
//      fires. Below the white point, bit-identical to SDR; above it, expands into HDR
//      headroom instead of the shoulder. burn still works. t01 adds grading + shoulder.
//   2  curve dropped, scene-linear handoff anchored so mid-grey arrives at 0.18 for
//      PsychoV30 in t01. No lift, so shadows behave like burn 0.

cbuffer camera : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
  float4 g_viewport_dimensions : packoffset(c29);
  float2 g_viewport_origin : packoffset(c30);
  float4 g_render_target_dimensions : packoffset(c31);
  float4 g_camera_temp0 : packoffset(c32);
  float4 g_camera_temp1 : packoffset(c33);
  float4 g_camera_temp2 : packoffset(c34);
  float4 g_clip_rect : packoffset(c35);
  float3 g_vr_head_rotation : packoffset(c36);
  int g_num_of_samples : packoffset(c36.w);
  float g_supersampling : packoffset(c37);
  float4 g_mouse_position : packoffset(c38);
  float3 g_frustum_points[8] : packoffset(c39);
  float g_orthographic : packoffset(c46.w);
  float g_overlay_lerp : packoffset(c47);
  float g_overlay_parchment_lerp : packoffset(c47.y);
  float g_overlay_palette_alpha : packoffset(c47.z);
  float g_debug_tonemapping : packoffset(c47.w);
  float4 g_blood_remap : packoffset(c48);
}

cbuffer tone_mapping : register(b1)
{
  float g_tone_mapping_brightness : packoffset(c0);
  float g_tone_mapping_burn : packoffset(c0.y);
  float g_tone_mapping_adaptation : packoffset(c0.z);
  float g_tone_mapping_average_luminance : packoffset(c0.w);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
Texture2D<float4> g_hdr_rgb_texture : register(t0);

// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

// Stock Narkowicz fit, Troy's exact constants.
float3 TroyAcesFit(float3 x)
{
  float3 num = x * (2.50999999f * x + 0.0299999993f);
  float3 den = x * (2.43000007f * x + 0.589999974f) + 0.140000001f;
  return num / den;
}

float TroyAcesFit(float x)
{
  float num = x * (2.50999999f * x + 0.0299999993f);
  float den = x * (2.43000007f * x + 0.589999974f) + 0.140000001f;
  return num / den;
}

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  int hdr_mode = (HDR < 0.5f) ? 0 : ((HDR < 1.5f) ? 1 : 2);

  // HDR. The debug tonemapping overlay is a dev visualisation, skipped here.
  if (hdr_mode != 0)
  {
    // Stock eye adaptation, with a guard on the unguarded stock reciprocal.
    float adaptation_mix = g_tone_mapping_adaptation * (g_tone_mapping_average_luminance - 0.5f) + 0.5f;
    float adaptation_exposure = abs(adaptation_mix) > 0.000001f ? 1.f / adaptation_mix : 1.f;

    // Gain is brightness, lift is BURN. See the decompiler-bug note at the top.
    float curve_gain = 0.125f * g_tone_mapping_brightness;
    float curve_lift = 0.25f * g_tone_mapping_burn;
    float exposure_scale = adaptation_exposure * curve_gain;

    float2 texcoord = g_screen_size.zw * (g_vpos_texel_offset + v0.xy);
    float4 scene_sample = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, texcoord, 0);
    float3 scene_color = max(scene_sample.xyz, 0.f.xxx);

    // What stock fed the fit, before the lift is added.
    float3 exposed = scene_color * exposure_scale;

    // Stock's 0.6275 / 0.6075 are 2.51*0.25 and 2.43*0.25, so its black level is just
    // the curve at the lift. That is why black always lands on exactly 0.
    float black_offset = saturate(TroyAcesFit(curve_lift));

    if (hdr_mode == 1)
    {
      // Faithful: keep the stock curve, remove only its clipping. NeuTwo compresses the
      // max channel into the curve's domain, the curve runs there, the same scalar
      // divides back out. At or below the white point scale == 1, so it is SDR exactly.
      // The lift is added after the scale, so it still cancels the black offset.

      // Where the fit saturates: AcesFit(x) == 1 gives x = (7 + sqrt(56))/2 = 7.2416574,
      // minus the share of the domain the lift has already taken.
      const float kStockCurveCeilingInput = 7.2416574f;
      float ceiling_input = max(kStockCurveCeilingInput - curve_lift, 0.000001f);

      // EXTENDED WHITE POINT. The NeuTwo normaliser IS the white point: below it the
      // output is the SDR image, above it the signal expands into HDR headroom.
      //   Off: the curve's own ceiling, ~5 stops over mid-grey, so almost nothing
      //        reaches it - only what stock actually clipped expands.
      //   On:  3.5 stops lower, pulling ACES' heavily compressed top end back out.
      //        ~32% of the SDR range then lands above paper white.
      // Fixed, not derived from HDR_PEAK, so the look does not change with the display.
      const float kExpansionStops = 3.5f;

      float expansion_stops = (SI.highlight_expansion >= 0.5f) ? kExpansionStops : 0.f;

      float white_input = max(ceiling_input * exp2(-expansion_stops), 0.000001f);

      float3 normalized = exposed / white_input;
      float curve_scale = renodx::tonemap::neutwo::ComputeMaxChannelScale(normalized);

      float3 curve_input = exposed * curve_scale + curve_lift.xxx;

      // Replaces the UNORM write clamp stock used for rounding slop at black.
      float3 graded_proxy = max(TroyAcesFit(curve_input) - black_offset.xxx, 0.f.xxx);

      o0.xyz = renodx::math::DivideSafe(graded_proxy, curve_scale.xxx, graded_proxy);

      // Stock runs alpha through the curve too; matching it is nearly free.
      o0.w = max(TroyAcesFit(scene_sample.w * exposure_scale + curve_lift) - black_offset, 0.f);
      return;
    }

    // Mode 2: scene-linear handoff for PsychoV30, anchored on mid-grey. Solve
    //     AcesFit(x_mid) = 0.18 + black_offset        (a plain quadratic)
    // black_offset is in the target because stock subtracts it at the very end. x_mid is
    // a curve INPUT, so the lift comes back off it to get the exposed value we scale -
    // skipping that leaves the handoff 1.6x to 2.5x too dark. Anchoring at 0.18 also
    // samples t01's matrix + LUT at the coordinate SDR uses.
    const float kMidGrey = 0.18f;

    // The fit asymptotes at 2.51/2.43 = 1.0329, so a target at or above that has no
    // root. Only reachable at burn >= 5.17; this is purely a NaN guard.
    float mid_target = min(kMidGrey + black_offset, 1.02f);

    float qa = 2.50999999f - 2.43000007f * mid_target;
    float qb = 0.0299999993f - 0.589999974f * mid_target;
    float qc = 0.140000001f * mid_target;   // = -c, since c = -0.14 * mid_target

    float disc = qb * qb + 4.f * qa * qc;
    float x_mid = (-qb + sqrt(max(disc, 0.f))) / (2.f * max(qa, 0.000001f));

    // Back off the lift. The curve is monotonic, so x_mid > curve_lift always.
    float exposed_mid = max(x_mid - curve_lift, 0.000001f);

    float handoff_scale = kMidGrey / exposed_mid;

    o0.xyz = exposed * handoff_scale;
    o0.w = scene_sample.w;
    return;
  }

  // SDR: Troy's original tonemapper, decompiler bug corrected.
  r0.x = -0.5 + g_tone_mapping_average_luminance;
  r0.x = g_tone_mapping_adaptation * r0.x + 0.5;
  r0.x = 1 / r0.x;

  // ### CORRECTED ### 3Dmigoto emits brightness for both. The .xxyx swizzle means
  // .z reads cb1[0].y, i.e. burn.
  r0.y = 0.125 * g_tone_mapping_brightness;
  r0.z = 0.25 * g_tone_mapping_burn;

  r1.xy = g_tone_mapping_burn * float2(0.627499998,0.607500017) + float2(0.0299999993,0.589999974);
  r0.w = r1.x * r0.z;
  r1.x = r0.z * r1.y + 0.140000001;
  r0.w = saturate(r0.w / r1.x);
  r1.xy = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r1.xy;
  r1.z = cmp(0 < g_debug_tonemapping);
  if (r1.z != 0) {
    r1.z = -120 + v0.x;
    r1.z = r1.z / g_viewport_dimensions.x;
    r1.z = r1.z + r1.z;
    r1.z = max(0, r1.z);
    r1.z = r1.z * r0.x;
    r1.z = r1.z * r0.y + r0.z;
    r2.xy = r1.zz * float2(2.50999999,2.43000007) + float2(0.0299999993,0.589999974);
    r1.w = r2.x * r1.z;
    r1.z = r1.z * r2.y + 0.140000001;
    r1.z = saturate(r1.w / r1.z);
    r1.z = r1.z + -r0.w;
    r1.w = cmp(v0.x < 120);
    if (r1.w != 0) {
      r2.x = 80 * r1.y;
      r2.x = floor(r2.x);
      r2.x = 0.0250000004 * r2.x;
      r2.y = v0.y * 2 + -g_viewport_dimensions.y;
      r2.y = cmp(abs(r2.y) < 2);
      r2.x = r2.y ? 0 : r2.x;
      r3.x = cmp(v0.x >= 30);
      r3.yz = cmp(v0.xx < float2(60,90));
      r2.yzw = float3(0,0,1);
      r4.xyzw = r3.zzzz ? r2.zzxw : r2.xxxw;
      r4.xyzw = r3.yyyy ? r2.zxzw : r4.xyzw;
      r2.xyzw = r3.xxxx ? r4.xyzw : r2.xyzw;
    }
    r1.z = g_viewport_dimensions.y * r1.z;
    r1.z = -r1.z * 0.5 + v0.y;
    r1.z = cmp(abs(r1.z) < 1);
    r3.y = r1.z ? 0 : 1;
    r3.x = r1.z ? 10.000000 : 0;
    r3.xy = r1.ww ? float2(1,0) : r3.yx;
    r4.xy = r3.yy;
  } else {
    r4.xy = float2(0,0);
    r3.x = 1;
    r1.w = 0;
  }
  if (r1.w == 0) {
    r1.xyzw = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, r1.xy, 0).xyzw;
    r3.y = 1;
    r4.z = 0;
    r2.xyzw = r1.xyzw * r3.yxyy + r4.xzyz;
  }
  r1.xyzw = r2.xyzw * r0.xxxx;
  r1.xyzw = r1.xyzw * r0.yyyy + r0.zzzz;
  r2.xyzw = r1.xyzw * float4(2.50999999,2.50999999,2.50999999,2.50999999) + float4(0.0299999993,0.0299999993,0.0299999993,0.0299999993);
  r2.xyzw = r2.xyzw * r1.xyzw;
  r3.xyzw = r1.xyzw * float4(2.43000007,2.43000007,2.43000007,2.43000007) + float4(0.589999974,0.589999974,0.589999974,0.589999974);
  r1.xyzw = r1.xyzw * r3.xyzw + float4(0.140000001,0.140000001,0.140000001,0.140000001);
  r1.xyzw = saturate(r2.xyzw / r1.xyzw);
  o0.xyzw = r1.xyzw + -r0.wwww;

  // Restore the UNORM write clamp the upgraded float target no longer provides.
  o0.xyzw = saturate(o0.xyzw);
  return;
}