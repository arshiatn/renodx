// ---- Created with 3Dmigoto v1.3.16 on Sat Sep 05 18:57:00 2026

cbuffer _Globals : register(b0)
{
  float ToneMapLuminanceApply : packoffset(c0);
  float RedCorrectionScale : packoffset(c0.y);
  float2 HighlightsFixAmount : packoffset(c0.z);
  float4 ColorCorrectionParams[20] : packoffset(c1);
  float ColorCorrectionShadowsRangeEnd : packoffset(c21);
  float ColorCorrectionHighlightsRangeBegin : packoffset(c21.y);
  row_major float4x4 WhiteBalanceMatrix : packoffset(c22);
  float4 Params[7] : packoffset(c26);
}

RWTexture3D<unorm float4> LUT : register(u0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"
#include "../psycho_test30.hlsli"
#include "./vanilla.hlsli"
#include "./vanillaplus.hlsli"

groupshared float dos2_aces_gray_exposure;

// Helper function to apply the POST-MATRIX
float3 ApplyNativePostMatrix(float3 color_ap1, float2 highlights_fix) {
  float3 corrected_ap1 = float3(
      dot(float3(1.06537485f, 1.44673368e-06f, -0.0653710067f), color_ap1),
      dot(float3(-3.4558721e-07f, 1.20366347f, -0.203667715f), color_ap1),
      dot(float3(1.98354986e-08f, 2.12240607e-08f, 0.999999583f), color_ap1));

  return lerp(color_ap1, corrected_ap1, highlights_fix.x * highlights_fix.y);
}

// Preserve the native scalar/RGB blend and output stages, using the captured
// reference spline instead of the former RenoDX ODT replacement.
float3 ApplyVanillaToneCurve(float3 color_ap1, float luminance_amount, float2 highlights_fix) {
  float3 mapped_ap1 = dos2_vanilla::ToneMap(color_ap1);
  float luminance = dot(color_ap1, float3(0.272228718f, 0.674081743f, 0.0536895171f));
  if (luminance_amount != 0.f && luminance > 1e-10f) {
    float mapped_luminance = dos2_vanilla::ToneMap(luminance);
    float3 mapped_by_luminance = color_ap1 * (mapped_luminance / luminance);
    mapped_ap1 = lerp(mapped_ap1, mapped_by_luminance, luminance_amount);
  }

  mapped_ap1 = max(mapped_ap1, dos2_vanilla::REFERENCE_BLACK_NITS);
  mapped_ap1 = ApplyNativePostMatrix(mapped_ap1, highlights_fix);
  return (mapped_ap1 - dos2_vanilla::REFERENCE_BLACK_NITS) /
         (dos2_vanilla::REFERENCE_PEAK_NITS - dos2_vanilla::REFERENCE_BLACK_NITS);
}

float3 VanillaAP1ToHDR10(float3 color_ap1, float paper_white, float peak_nits) {
  // Exact matrix from the stock outputDevice == 1 branch below.
  float3 color_bt2020 = float3(
      dot(float3(1.0258249f, -0.0200528856f, -0.00577135477f), color_ap1),
      dot(float3(-0.00223499862f, 1.00458491f, -0.00235229917f), color_ap1),
      dot(float3(-0.00501333317f, -0.0252900254f, 1.03030288f), color_ap1));
  // Native output at final Contrast 1 / Brightness 1, before PQ encoding.
  float3 native_nits = min(max(color_bt2020, 0.f) * dos2_vanilla::REFERENCE_PEAK_NITS, 65504.f);
  return dos2_vanilla::Calibrate(native_nits, paper_white, peak_nits);
}

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  // I had to use asuint to fix decompilation issues:
  uint outputDevice = asuint(Params[6].z);
  // FIX: Only activate RenoDX HDR mods if the engine is actually requesting an HDR LUT!
  // This took me 67 hours. GAME BUILDS SDR LUT FOR SDR THINGS THAT ARE IN INVENTORY AND ETC.
  bool is_hdr = outputDevice != 0u;
  bool use_aces_fixed = (SI.tonemap_type == 1.f) && is_hdr;
  bool use_aces_tweaked = (SI.tonemap_type == 2.f) && is_hdr;
  bool use_psycho = (SI.tonemap_type == 3.f) && is_hdr;

  // The condition depends only on uniform constant-buffer values. Every
  // thread in a Tweaked ACES group reaches the barrier before any return.
  if (use_aces_tweaked) {
    if (group_index == 0u) {
      dos2_aces_gray_exposure = dos2_vanillaplus::ACESGrayExposure(
          max(SI.diffuse_white_nits, 0.0001f), DOS2_PEAK_NITS);
    }
    GroupMemoryBarrierWithGroupSync();
  }

  r0.xyz = (uint3)vThreadID.xyz;
  r0.xyz = float3(0.0322580636,0.0322580636,0.0322580636) * r0.xyz;

  // Freeze only the modified HDR shaper. SDR/inventory keeps its native
  // domain as well as the native tone-curve branch below.
  float2 lut_shaper_domain = (DOS2_CORRECT_HDR && is_hdr)
                                 ? DOS2_HDR_LUT_SHAPER_DOMAIN
                                 : float2(Params[5].w, Params[6].x);
  r0.w = lut_shaper_domain.y - lut_shaper_domain.x;
  r0.w = 10000 / r0.w;
  r1.x = lut_shaper_domain.x * -r0.w;

  r0.xyz = log2(r0.xyz);
  r0.xyz = float3(0.0126833133,0.0126833133,0.0126833133) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r1.yzw = float3(-0.8359375,-0.8359375,-0.8359375) + r0.xyz;
  r1.yzw = max(float3(0,0,0), r1.yzw);
  r0.xyz = -r0.xyz * float3(18.6875,18.6875,18.6875) + float3(18.8515625,18.8515625,18.8515625);
  r0.xyz = r1.yzw / r0.xyz;
  r0.xyz = log2(abs(r0.xyz));
  r0.xyz = float3(6.27739477,6.27739477,6.27739477) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r0.xyz = r0.xyz * float3(10000,10000,10000) + -r1.xxx;
  r0.xyz = r0.xyz / r0.www;
  r1.x = dot(float3(0.412456393,0.357576102,0.180437505), r0.xyz);
  r1.y = dot(float3(0.212672904,0.715152204,0.0721750036), r0.xyz);
  r1.z = dot(float3(0.0193339009,0.119191997,0.950304091), r0.xyz);
  r0.x = dot(WhiteBalanceMatrix._m00_m01_m02, r1.xyz);
  r0.y = dot(WhiteBalanceMatrix._m10_m11_m12, r1.xyz);
  r0.z = dot(WhiteBalanceMatrix._m20_m21_m22, r1.xyz);
  r1.x = dot(float3(1.6605773,-0.315296024,-0.241509631), r0.xyz);
  r1.y = dot(float3(-0.659922779,1.60839415,0.0172986612), r0.xyz);
  r1.z = dot(float3(0.00900251884,-0.0035668863,0.913644135), r0.xyz);
  r0.x = dot(r1.xyz, float3(0.272228718,0.674081743,0.0536895171));
  r2.xyzw = ColorCorrectionParams[5].xyzw * ColorCorrectionParams[0].xyzw;
  r3.xyzw = ColorCorrectionParams[6].xyzw * ColorCorrectionParams[1].xyzw;
  r4.xyzw = ColorCorrectionParams[7].xyzw * ColorCorrectionParams[2].xyzw;
  r5.xyzw = ColorCorrectionParams[8].xyzw * ColorCorrectionParams[3].xyzw;
  r6.xyzw = ColorCorrectionParams[9].xyzw + ColorCorrectionParams[4].xyzw;
  r0.yzw = r2.xyz * r2.www;
  r1.xyz = r1.xyz + -r0.xxx;
  r0.yzw = r0.yzw * r1.xyz + r0.xxx;
  r0.yzw = max(float3(0,0,0), r0.yzw);
  r0.yzw = float3(9.99999972e-010,9.99999972e-010,9.99999972e-010) + r0.yzw;
  r0.yzw = log2(r0.yzw);
  r0.yzw = r0.yzw * float3(0.693147182,0.693147182,0.693147182) + float3(1.71479845,1.71479845,1.71479845);
  r0.yzw = r0.yzw * r3.xyz;
  r0.yzw = r0.yzw * r3.www + float3(-1.71479845,-1.71479845,-1.71479845);
  r2.xyz = r4.xyz * r4.www;
  r2.xyz = max(float3(1.00000001e-010,1.00000001e-010,1.00000001e-010), r2.xyz);
  r0.yzw = r0.yzw / r2.xyz;
  r0.yzw = float3(1.44269502,1.44269502,1.44269502) * r0.yzw;
  r0.yzw = exp2(r0.yzw);
  r0.yzw = float3(-1.00000001e-010,-1.00000001e-010,-1.00000001e-010) + r0.yzw;
  r0.yzw = max(float3(0,0,0), r0.yzw);
  r2.xyz = r5.xyz * r5.www;
  r3.xyz = r6.xyz + r6.www;
  r0.yzw = r0.yzw * r2.xyz + r3.xyz;
  r1.w = 1 / ColorCorrectionShadowsRangeEnd;
  r1.w = saturate(r1.w * r0.x);
  r2.x = r1.w * -2 + 3;
  r1.w = r1.w * r1.w;
  r1.w = -r2.x * r1.w + 1;
  r2.xyzw = ColorCorrectionParams[15].xyzw * ColorCorrectionParams[0].xyzw;
  r3.xyzw = ColorCorrectionParams[16].xyzw * ColorCorrectionParams[1].xyzw;
  r4.xyzw = ColorCorrectionParams[17].xyzw * ColorCorrectionParams[2].xyzw;
  r5.xyzw = ColorCorrectionParams[18].xyzw * ColorCorrectionParams[3].xyzw;
  r6.xyzw = ColorCorrectionParams[19].xyzw + ColorCorrectionParams[4].xyzw;
  r2.xyz = r2.xyz * r2.www;
  r2.xyz = r2.xyz * r1.xyz + r0.xxx;
  r2.xyz = max(float3(0,0,0), r2.xyz);
  r2.xyz = float3(9.99999972e-010,9.99999972e-010,9.99999972e-010) + r2.xyz;
  r2.xyz = log2(r2.xyz);
  r2.xyz = r2.xyz * float3(0.693147182,0.693147182,0.693147182) + float3(1.71479845,1.71479845,1.71479845);
  r2.xyz = r2.xyz * r3.xyz;
  r2.xyz = r2.xyz * r3.www + float3(-1.71479845,-1.71479845,-1.71479845);
  r3.xyz = r4.xyz * r4.www;
  r3.xyz = max(float3(1.00000001e-010,1.00000001e-010,1.00000001e-010), r3.xyz);
  r2.xyz = r2.xyz / r3.xyz;
  r2.xyz = float3(1.44269502,1.44269502,1.44269502) * r2.xyz;
  r2.xyz = exp2(r2.xyz);
  r2.xyz = float3(-1.00000001e-010,-1.00000001e-010,-1.00000001e-010) + r2.xyz;
  r2.xyz = max(float3(0,0,0), r2.xyz);
  r3.xyz = r5.xyz * r5.www;
  r4.xyz = r6.xyz + r6.www;
  r2.xyz = r2.xyz * r3.xyz + r4.xyz;
  r2.w = 1 + -ColorCorrectionHighlightsRangeBegin;
  r3.x = -ColorCorrectionHighlightsRangeBegin + r0.x;
  r2.w = 1 / r2.w;
  r2.w = saturate(r3.x * r2.w);
  r3.x = r2.w * -2 + 3;
  r2.w = r2.w * r2.w;
  r3.y = r3.x * r2.w;
  r4.xyzw = ColorCorrectionParams[10].xyzw * ColorCorrectionParams[0].xyzw;
  r5.xyzw = ColorCorrectionParams[11].xyzw * ColorCorrectionParams[1].xyzw;
  r6.xyzw = ColorCorrectionParams[12].xyzw * ColorCorrectionParams[2].xyzw;
  r7.xyzw = ColorCorrectionParams[13].xyzw * ColorCorrectionParams[3].xyzw;
  r8.xyzw = ColorCorrectionParams[14].xyzw + ColorCorrectionParams[4].xyzw;
  r4.xyz = r4.xyz * r4.www;
  r1.xyz = r4.xyz * r1.xyz + r0.xxx;
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r1.xyz = float3(9.99999972e-010,9.99999972e-010,9.99999972e-010) + r1.xyz;
  r1.xyz = log2(r1.xyz);
  r1.xyz = r1.xyz * float3(0.693147182,0.693147182,0.693147182) + float3(1.71479845,1.71479845,1.71479845);
  r1.xyz = r1.xyz * r5.xyz;
  r1.xyz = r1.xyz * r5.www + float3(-1.71479845,-1.71479845,-1.71479845);
  r4.xyz = r6.xyz * r6.www;
  r4.xyz = max(float3(1.00000001e-010,1.00000001e-010,1.00000001e-010), r4.xyz);
  r1.xyz = r1.xyz / r4.xyz;
  r1.xyz = float3(1.44269502,1.44269502,1.44269502) * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r1.xyz = float3(-1.00000001e-010,-1.00000001e-010,-1.00000001e-010) + r1.xyz;
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r4.xyz = r7.xyz * r7.www;
  r5.xyz = r8.xyz + r8.www;
  r1.xyz = r1.xyz * r4.xyz + r5.xyz;
  r0.x = 1 + -r1.w;
  r0.x = -r3.x * r2.w + r0.x;
  r1.xyz = r1.xyz * r0.xxx;
  r0.xyz = r0.yzw * r1.www + r1.xyz;
  r0.xyz = r2.xyz * r3.yyy + r0.xyz;


  // Native white balance and regional grading above are common to all modes.
  // Vanilla+ replaces the complete native rendering transform below them.
  if (use_aces_tweaked || use_psycho) {
    const float diffuse_white_nits = max(SI.diffuse_white_nits, 0.0001f);
    const float peak_white_nits = DOS2_PEAK_NITS;
    const float relative_peak = peak_white_nits / diffuse_white_nits;

    // custom user Grading (in BT.709 space)
    float3 graded_bt709 = renodx::color::bt709::from::AP1(r0.xyz);

    // Apply user colorgrading with the sliders and etc. It needs to be in bt709
    if (use_aces_tweaked &&
        (SI.aces_tweaked_exposure != 1.f || SI.aces_tweaked_highlights != 1.f ||
         SI.aces_tweaked_shadows != 1.f || SI.aces_tweaked_contrast != 1.f ||
         SI.aces_tweaked_saturation != 1.f || SI.aces_tweaked_highlight_saturation != 1.f)) {
      renodx::color::grade::Config aces_grade = renodx::color::grade::config::Create();
      aces_grade.exposure = SI.aces_tweaked_exposure;
      aces_grade.highlights = SI.aces_tweaked_highlights;
      aces_grade.shadows = SI.aces_tweaked_shadows;
      aces_grade.contrast = SI.aces_tweaked_contrast;
      aces_grade.saturation = SI.aces_tweaked_saturation;
      aces_grade.hue_correction_strength = 0.f;
      aces_grade.blowout = 1.f - SI.aces_tweaked_highlight_saturation;
      graded_bt709 = renodx::color::grade::config::ApplyUserColorGrading(graded_bt709, aces_grade);
    }

    float3 final_bt2020;
    float3 exposed_scene_bt709 = graded_bt709;
    if (use_aces_tweaked) {
      float3 tonemapped_ap1 = dos2_vanillaplus::ACES(
          graded_bt709 * dos2_aces_gray_exposure, diffuse_white_nits, peak_white_nits);
      final_bt2020 = renodx::color::bt2020::from::AP1(tonemapped_ap1);
    } else {
      float3 psycho_output_bt709 = ApplyPsychoV30(graded_bt709);
      final_bt2020 = renodx::color::bt2020::from::BT709(psycho_output_bt709);
      exposed_scene_bt709 *= SI.exposure;
    }

    final_bt2020 = dos2_vanillaplus::FitBT2020(final_bt2020, relative_peak);
    float fire_strength = use_aces_tweaked ? SI.aces_tweaked_fire_color_strength : SI.fire_color_strength;
    float fire_hue = use_aces_tweaked ? SI.aces_tweaked_fire_color_hue : SI.fire_color_hue;
    final_bt2020 = dos2_vanillaplus::ApplyFireColor(
        final_bt2020, exposed_scene_bt709, relative_peak, fire_strength, fire_hue);
    LUT[vThreadID.xyz] = float4(
        renodx::color::pq::EncodeSafe(final_bt2020, diffuse_white_nits), DOS2_HDR_LUT_ALPHA);
    return;
  }


  // For use_aces_fixed only 
  //Do the user custom settings here:
  if (use_aces_fixed &&
      (SI.aces_exposure != 1.f || SI.aces_highlights != 1.f ||
       SI.aces_shadows != 1.f || SI.aces_contrast != 1.f ||
       SI.aces_saturation != 1.f || SI.aces_highlight_saturation != 1.f)) {
    // Neutral settings bypass this conversion/grading round trip entirely.
    // Apply user colorgrading with the sliders and etc. It needs to be in bt709
    float3 aces_input_bt709 = renodx::color::bt709::from::AP1(r0.xyz);
    renodx::color::grade::Config aces_grade = renodx::color::grade::config::Create();
    aces_grade.exposure = SI.aces_exposure;
    aces_grade.highlights = SI.aces_highlights;
    aces_grade.shadows = SI.aces_shadows;
    aces_grade.contrast = SI.aces_contrast;
    aces_grade.saturation = SI.aces_saturation;
    aces_grade.hue_correction_strength = 0.f;
    aces_grade.blowout = 1.f - SI.aces_highlight_saturation;
    aces_input_bt709 = renodx::color::grade::config::ApplyUserColorGrading(aces_input_bt709, aces_grade);

    //pass it on as ap1 again for the game to its grading
    r0.xyz = renodx::color::ap1::from::BT709(aces_input_bt709);
  }

  // Keep the graded scene colour for the optional Vanilla highlight mask.
  // Capture it before native ACES colour processing, in the same scene units
  // used by the other modes' masks. Off and SDR do not use this correction.
  float3 vanilla_fire_scene_bt709 = float3(0.f, 0.f, 0.f);
  if (use_aces_fixed && SI.vanilla_fire_color_strength > 0.f) {
    vanilla_fire_scene_bt709 = renodx::color::bt709::from::AP1(r0.xyz);
  }

  // apply a chromatic matrix controlled by HighlightsFixAmount.x
  r1.y = dot(float3(0.695452213,0.140678704,0.163869068), r0.xyz);
  r1.z = dot(float3(0.0447945632,0.859671116,0.0955343172), r0.xyz);
  r1.w = dot(float3(-0.00552588282,0.00402521016,1.00150073), r0.xyz);
  r0.y = dot(float3(0.940437257,-0.0183068793,0.077869609), r1.yzw);
  r0.z = dot(float3(0.00837869663,0.828660011,0.162961304), r1.yzw);
  r0.w = dot(float3(0.00054712611,-0.000883374596,1.00033629), r1.yzw);
  r0.xyz = r0.yzw + -r1.yzw;
  r0.xyz = HighlightsFixAmount.xxx * r0.xyz + r1.yzw;

  // apply the standard ACES glow operation.
  r0.w = min(r0.x, r0.y);
  r0.w = min(r0.w, r0.z);
  r1.x = max(r0.x, r0.y);
  r1.x = max(r1.x, r0.z);
  r1.xy = max(float2(1.00000001e-010,0.00999999978), r1.xx);
  r0.w = max(1.00000001e-010, r0.w);
  r0.w = r1.x + -r0.w;
  r0.w = r0.w / r1.y;
  r1.xyz = r0.zyx + -r0.yxz;
  r1.xy = r1.xy * r0.zy;
  r1.x = r1.x + r1.y;
  r1.x = r0.x * r1.z + r1.x;
  r1.x = sqrt(r1.x);
  r1.y = r0.z + r0.y;
  r1.y = r1.y + r0.x;
  r1.x = r1.x * 1.75 + r1.y;
  r1.z = -0.400000006 + r0.w;
  r1.yw = float2(0.333333343,2.5) * r1.xz;
  r1.w = 1 + -abs(r1.w);
  r1.w = max(0, r1.w);
  r2.x = cmp(0 < r1.z);
  r1.z = cmp(r1.z < 0);
  r1.z = (int)-r2.x + (int)r1.z;
  r1.z = (int)r1.z;
  r1.w = -r1.w * r1.w + 1;
  r1.z = r1.z * r1.w + 1;
  r1.z = 0.0250000004 * r1.z;
  r1.w = cmp(0.159999996 >= r1.x);
  r1.x = cmp(r1.x >= 0.479999989);
  r1.y = 0.0799999982 / r1.y;
  r1.y = -0.5 + r1.y;
  r1.y = r1.z * r1.y;
  r1.x = r1.x ? 0 : r1.y;
  r1.x = r1.w ? r1.z : r1.x;
  r1.x = 1 + r1.x;
  r2.yzw = r1.xxx * r0.xyz;

  // calculate hue and apply a red-sector modifier.
  r1.yz = cmp(r2.zw == r2.yz);
  r1.y = r1.z ? r1.y : 0;
  r0.y = r0.y * r1.x + -r2.w;
  r0.y = 1.73205078 * r0.y;
  r1.z = r2.y * 2 + -r2.z;
  r0.z = -r0.z * r1.x + r1.z;
  r1.z = min(abs(r0.y), abs(r0.z));
  r1.w = max(abs(r0.y), abs(r0.z));
  r1.w = 1 / r1.w;
  r1.z = r1.z * r1.w;
  r1.w = r1.z * r1.z;
  r3.x = r1.w * 0.0208350997 + -0.0851330012;
  r3.x = r1.w * r3.x + 0.180141002;
  r3.x = r1.w * r3.x + -0.330299497;
  r1.w = r1.w * r3.x + 0.999866009;
  r3.x = r1.z * r1.w;
  r3.y = cmp(abs(r0.z) < abs(r0.y));
  r3.x = r3.x * -2 + 1.57079637;
  r3.x = r3.y ? r3.x : 0;
  r1.z = r1.z * r1.w + r3.x;
  r1.w = cmp(r0.z < -r0.z);
  r1.w = r1.w ? -3.141593 : 0;
  r1.z = r1.z + r1.w;
  r1.w = min(r0.y, r0.z);
  r0.y = max(r0.y, r0.z);
  r0.z = cmp(r1.w < -r1.w);
  r0.y = cmp(r0.y >= -r0.y);
  r0.y = r0.y ? r0.z : 0;
  r0.y = r0.y ? -r1.z : r1.z;
  r0.y = 57.2957802 * r0.y;
  r0.y = r1.y ? 180 : r0.y;
  r0.z = cmp(r0.y < 0);
  r1.y = 360 + r0.y;
  r0.y = r0.z ? r1.y : r0.y;
  r0.y = max(0, r0.y);
  r0.y = min(360, r0.y);
  r0.z = cmp(180 < r0.y);
  r1.y = -360 + r0.y;
  r0.y = r0.z ? r1.y : r0.y;
  r0.z = cmp(-67.5 < r0.y);
  r1.y = cmp(r0.y < 67.5);
  r0.z = r0.z ? r1.y : 0;
  if (r0.z != 0) {
    r0.y = 67.5 + r0.y;
    r0.z = 0.0296296291 * r0.y;
    r1.y = (int)r0.z;
    r0.z = trunc(r0.z);
    r0.y = r0.y * 0.0296296291 + -r0.z;
    r0.z = r0.y * r0.y;
    r1.z = r0.z * r0.y;
    r3.xyz = float3(-0.166666672,-0.5,0.166666672) * r1.zzz;
    r3.xy = r0.zz * float2(0.5,0.5) + r3.xy;
    r3.xy = r0.yy * float2(-0.5,0.5) + r3.xy;
    r0.y = r1.z * 0.5 + -r0.z;
    r0.y = 0.666666687 + r0.y;
    r4.xyz = cmp((int3)r1.yyy == int3(3,2,1));
    r1.zw = float2(0.166666672,0.166666672) + r3.xy;
    r0.z = r1.y ? 0 : r3.z;
    r0.z = r4.z ? r1.w : r0.z;
    r0.y = r4.y ? r0.y : r0.z;
    r0.y = r4.x ? r1.z : r0.y;
  } else {
    r0.y = 0;
  }
  // red channel modification:
  r0.y = RedCorrectionScale * r0.y;
  r0.y = r0.w * r0.y;
  r0.y = 1.5 * r0.y;
  r0.x = -r0.x * r1.x + 0.0299999993;
  r0.x = r0.y * r0.x;
  r2.x = r0.x * 0.180000007 + r2.y;

  //convert back to AP1 and apply ACES’s 0.96 global saturation.
  r0.xyz = max(float3(0,0,0), r2.xzw);
  r0.xyz = min(float3(65504,65504,65504), r0.xyz);
  r1.x = dot(float3(1.45143926,-0.236510754,-0.214928567), r0.xyz);
  r1.y = dot(float3(-0.0765537769,1.17622972,-0.0996759236), r0.xyz);
  r1.z = dot(float3(0.00831614807,-0.00603244966,0.997716308), r0.xyz);
  r0.x = dot(r1.xyz, float3(0.272228718,0.674081743,0.0536895171));
  r0.yzw = r1.xyz + -r0.xxx;
  r0.xyz = r0.yzw * float3(0.959999979,0.959999979,0.959999979) + r0.xxx;

  // Vanilla HDR: evaluate the captured native curve and calibrate its output.
  if (use_aces_fixed) {
    // This branch is HDR-only. SDR/inventory must reach the stock block below.
    const float diffuse_white_nits = max(SI.diffuse_white_nits, 0.0001f);
    const float peak_white_nits = DOS2_PEAK_NITS;
    // c0..c25 are slider-independent in the supplied captures. Preserve all
    // native colour controls, while freezing only the slider-dependent curve.
    r0.xyz = ApplyVanillaToneCurve(r0.xyz, ToneMapLuminanceApply, HighlightsFixAmount);
    r0.xyz = VanillaAP1ToHDR10(r0.xyz, diffuse_white_nits, peak_white_nits);
    // The native output is already nonnegative and peak-bounded. Apply the
    // same luminance-preserving correction directly in absolute nits, after
    // the native matrices and peak calibration. Zero strength bypasses it.
    if (SI.vanilla_fire_color_strength > 0.f) {
      r0.xyz = dos2_vanillaplus::ApplyFireColor(
          r0.xyz, vanilla_fire_scene_bt709, peak_white_nits,
          SI.vanilla_fire_color_strength, SI.vanilla_fire_color_hue);
    }
    // The calibrated result is already in absolute BT.2020 nits.
    LUT[vThreadID.xyz] = float4(renodx::color::pq::EncodeSafe(r0.xyz, 1.f), DOS2_HDR_LUT_ALPHA);
    return;
  }
  
  // STOCK TONEMAPPER. If we are selecting the game's vanilla tonemapper, this still runs.
  // tonemap_type == 0:
  // Actually this is being used for SDR stuff too! So the check had to be changed
  if (!is_hdr || !DOS2_CORRECT_HDR) {
    r0.w = dot(r0.xyz, float3(0.272228718, 0.674081743, 0.0536895171));
    r1.x = max(6.10351563e-005, r0.w);
    r1.x = log2(r1.x);
    r1.y = 0.30103001 * r1.x;
    r1.z = cmp(Params[0].x >= r1.y);
    if (r1.z != 0) {
      r1.z = -Params[0].z * Params[0].x + Params[0].y;
      r1.z = r1.y * Params[0].z + r1.z;
    } else {
      r1.w = cmp(r1.y < Params[0].w);
      if (r1.w != 0) {
        r1.w = r1.x * 0.30103001 + -Params[0].x;
        r1.w = 3 * r1.w;
        r2.x = Params[0].w + -Params[0].x;
        r1.w = r1.w / r2.x;
        r2.x = (int)r1.w;
        r2.y = trunc(r1.w);
        r3.y = -r2.y + r1.w;
        r4.xyzw = cmp((int4)r2.xxxx == int4(0, 1, 2, 3));
        r2.yz = cmp((int2)r2.xx == int2(4, 5));
        r4.xyzw = r4.xyzw ? float4(1, 1, 1, 1) : 0;
        r2.yz = r2.yz ? float2(1, 1) : 0;
        r1.w = dot(r4.xyz, Params[2].yzw);
        r1.w = r4.w * Params[3].x + r1.w;
        r1.w = r2.y * Params[3].y + r1.w;
        r4.x = r2.z * Params[3].z + r1.w;
        r2.xyzw = (int4)r2.xxxx + int4(1, 1, 2, 2);
        r5.xyzw = cmp((int4)r2.yyyy == int4(0, 1, 2, 3));
        r6.xyzw = cmp((int4)r2.xyzw == int4(4, 5, 4, 5));
        r5.xyzw = r5.xyzw ? float4(1, 1, 1, 1) : 0;
        r6.xyzw = r6.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.w = dot(r5.xyz, Params[2].yzw);
        r1.w = r5.w * Params[3].x + r1.w;
        r1.w = r6.x * Params[3].y + r1.w;
        r4.y = r6.y * Params[3].z + r1.w;
        r2.xyzw = cmp((int4)r2.wwww == int4(0, 1, 2, 3));
        r2.xyzw = r2.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.w = dot(r2.xyz, Params[2].yzw);
        r1.w = r2.w * Params[3].x + r1.w;
        r1.w = r6.z * Params[3].y + r1.w;
        r4.z = r6.w * Params[3].z + r1.w;
        r3.x = r3.y * r3.y;
        r2.x = dot(r4.xzy, float3(0.5, 0.5, -1));
        r2.y = dot(r4.xy, float2(-1, 1));
        r2.z = dot(r4.xy, float2(0.5, 0.5));
        r3.z = 1;
        r1.z = dot(r3.xyz, r2.xyz);
      } else {
        r1.w = cmp(r1.y < Params[1].z);
        r1.x = r1.x * 0.30103001 + -Params[0].w;
        r1.x = 3 * r1.x;
        r2.x = Params[1].z + -Params[0].w;
        r1.x = r1.x / r2.x;
        r2.x = (int)r1.x;
        r2.y = trunc(r1.x);
        r3.y = -r2.y + r1.x;
        r4.xyzw = cmp((int4)r2.xxxx == int4(0, 1, 2, 3));
        r2.yz = cmp((int2)r2.xx == int2(4, 5));
        r4.xyzw = r4.xyzw ? float4(1, 1, 1, 1) : 0;
        r2.yz = r2.yz ? float2(1, 1) : 0;
        r1.x = Params[4].x * r4.y;
        r1.x = r4.x * Params[3].w + r1.x;
        r1.x = r4.z * Params[4].y + r1.x;
        r1.x = r4.w * Params[4].z + r1.x;
        r1.x = r2.y * Params[4].w + r1.x;
        r4.x = r2.z * Params[5].x + r1.x;
        r2.xyzw = (int4)r2.xxxx + int4(1, 1, 2, 2);
        r5.xyzw = cmp((int4)r2.yyyy == int4(0, 1, 2, 3));
        r6.xyzw = cmp((int4)r2.xyzw == int4(4, 5, 4, 5));
        r5.xyzw = r5.xyzw ? float4(1, 1, 1, 1) : 0;
        r6.xyzw = r6.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.x = Params[4].x * r5.y;
        r1.x = r5.x * Params[3].w + r1.x;
        r1.x = r5.z * Params[4].y + r1.x;
        r1.x = r5.w * Params[4].z + r1.x;
        r1.x = r6.x * Params[4].w + r1.x;
        r4.y = r6.y * Params[5].x + r1.x;
        r2.xyzw = cmp((int4)r2.wwww == int4(0, 1, 2, 3));
        r2.xyzw = r2.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.x = Params[4].x * r2.y;
        r1.x = r2.x * Params[3].w + r1.x;
        r1.x = r2.z * Params[4].y + r1.x;
        r1.x = r2.w * Params[4].z + r1.x;
        r1.x = r6.z * Params[4].w + r1.x;
        r4.z = r6.w * Params[5].x + r1.x;
        r3.x = r3.y * r3.y;
        r2.x = dot(r4.xzy, float3(0.5, 0.5, -1));
        r2.y = dot(r4.xy, float2(-1, 1));
        r2.z = dot(r4.xy, float2(0.5, 0.5));
        r3.z = 1;
        r1.x = dot(r3.xyz, r2.xyz);
        r2.x = -Params[2].x * Params[1].z + Params[1].w;
        r1.y = r1.y * Params[2].x + r2.x;
        r1.z = r1.w ? r1.x : r1.y;
      }
    }
    r1.x = 3.32192802 * r1.z;
    r1.x = exp2(r1.x);
    r0.w = r1.x / r0.w;
    r1.xyz = max(float3(6.10351563e-005, 6.10351563e-005, 6.10351563e-005), r0.xyz);
    r1.xyz = log2(r1.xyz);
    r2.xyz = float3(0.30103001, 0.30103001, 0.30103001) * r1.xyz;
    r3.xyz = cmp(Params[0].xxx >= r2.xyz);
    if (r3.x != 0) {
      r1.w = -Params[0].z * Params[0].x + Params[0].y;
      r1.w = r2.x * Params[0].z + r1.w;
    } else {
      r2.w = cmp(r2.x < Params[0].w);
      if (r2.w != 0) {
        r2.w = r1.x * 0.30103001 + -Params[0].x;
        r2.w = 3 * r2.w;
        r3.x = Params[0].w + -Params[0].x;
        r2.w = r2.w / r3.x;
        r3.x = (int)r2.w;
        r3.w = trunc(r2.w);
        r4.y = -r3.w + r2.w;
        r5.xyzw = cmp((int4)r3.xxxx == int4(0, 1, 2, 3));
        r6.xy = cmp((int2)r3.xx == int2(4, 5));
        r5.xyzw = r5.xyzw ? float4(1, 1, 1, 1) : 0;
        r6.xy = r6.xy ? float2(1, 1) : 0;
        r2.w = dot(r5.xyz, Params[2].yzw);
        r2.w = r5.w * Params[3].x + r2.w;
        r2.w = r6.x * Params[3].y + r2.w;
        r5.x = r6.y * Params[3].z + r2.w;
        r6.xyzw = (int4)r3.xxxx + int4(1, 1, 2, 2);
        r7.xyzw = cmp((int4)r6.yyyy == int4(0, 1, 2, 3));
        r8.xyzw = cmp((int4)r6.xyzw == int4(4, 5, 4, 5));
        r7.xyzw = r7.xyzw ? float4(1, 1, 1, 1) : 0;
        r8.xyzw = r8.xyzw ? float4(1, 1, 1, 1) : 0;
        r2.w = dot(r7.xyz, Params[2].yzw);
        r2.w = r7.w * Params[3].x + r2.w;
        r2.w = r8.x * Params[3].y + r2.w;
        r5.y = r8.y * Params[3].z + r2.w;
        r6.xyzw = cmp((int4)r6.wwww == int4(0, 1, 2, 3));
        r6.xyzw = r6.xyzw ? float4(1, 1, 1, 1) : 0;
        r2.w = dot(r6.xyz, Params[2].yzw);
        r2.w = r6.w * Params[3].x + r2.w;
        r2.w = r8.z * Params[3].y + r2.w;
        r5.z = r8.w * Params[3].z + r2.w;
        r4.x = r4.y * r4.y;
        r6.x = dot(r5.xzy, float3(0.5, 0.5, -1));
        r6.y = dot(r5.xy, float2(-1, 1));
        r6.z = dot(r5.xy, float2(0.5, 0.5));
        r4.z = 1;
        r1.w = dot(r4.xyz, r6.xyz);
      } else {
        r2.w = cmp(r2.x < Params[1].z);
        r1.x = r1.x * 0.30103001 + -Params[0].w;
        r1.x = 3 * r1.x;
        r3.x = Params[1].z + -Params[0].w;
        r1.x = r1.x / r3.x;
        r3.x = (int)r1.x;
        r3.w = trunc(r1.x);
        r4.y = -r3.w + r1.x;
        r5.xyzw = cmp((int4)r3.xxxx == int4(0, 1, 2, 3));
        r6.xy = cmp((int2)r3.xx == int2(4, 5));
        r5.xyzw = r5.xyzw ? float4(1, 1, 1, 1) : 0;
        r6.xy = r6.xy ? float2(1, 1) : 0;
        r1.x = Params[4].x * r5.y;
        r1.x = r5.x * Params[3].w + r1.x;
        r1.x = r5.z * Params[4].y + r1.x;
        r1.x = r5.w * Params[4].z + r1.x;
        r1.x = r6.x * Params[4].w + r1.x;
        r5.x = r6.y * Params[5].x + r1.x;
        r6.xyzw = (int4)r3.xxxx + int4(1, 1, 2, 2);
        r7.xyzw = cmp((int4)r6.yyyy == int4(0, 1, 2, 3));
        r8.xyzw = cmp((int4)r6.xyzw == int4(4, 5, 4, 5));
        r7.xyzw = r7.xyzw ? float4(1, 1, 1, 1) : 0;
        r8.xyzw = r8.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.x = Params[4].x * r7.y;
        r1.x = r7.x * Params[3].w + r1.x;
        r1.x = r7.z * Params[4].y + r1.x;
        r1.x = r7.w * Params[4].z + r1.x;
        r1.x = r8.x * Params[4].w + r1.x;
        r5.y = r8.y * Params[5].x + r1.x;
        r6.xyzw = cmp((int4)r6.wwww == int4(0, 1, 2, 3));
        r6.xyzw = r6.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.x = Params[4].x * r6.y;
        r1.x = r6.x * Params[3].w + r1.x;
        r1.x = r6.z * Params[4].y + r1.x;
        r1.x = r6.w * Params[4].z + r1.x;
        r1.x = r8.z * Params[4].w + r1.x;
        r5.z = r8.w * Params[5].x + r1.x;
        r4.x = r4.y * r4.y;
        r6.x = dot(r5.xzy, float3(0.5, 0.5, -1));
        r6.y = dot(r5.xy, float2(-1, 1));
        r6.z = dot(r5.xy, float2(0.5, 0.5));
        r4.z = 1;
        r1.x = dot(r4.xyz, r6.xyz);
        r3.x = -Params[2].x * Params[1].z + Params[1].w;
        r2.x = r2.x * Params[2].x + r3.x;
        r1.w = r2.w ? r1.x : r2.x;
      }
    }
    r1.x = 3.32192802 * r1.w;
    r4.x = exp2(r1.x);
    if (r3.y != 0) {
      r1.x = -Params[0].z * Params[0].x + Params[0].y;
      r1.x = r2.y * Params[0].z + r1.x;
    } else {
      r1.w = cmp(r2.y < Params[0].w);
      if (r1.w != 0) {
        r1.w = r1.y * 0.30103001 + -Params[0].x;
        r1.w = 3 * r1.w;
        r2.x = Params[0].w + -Params[0].x;
        r1.w = r1.w / r2.x;
        r2.x = (int)r1.w;
        r2.w = trunc(r1.w);
        r5.y = -r2.w + r1.w;
        r6.xyzw = cmp((int4)r2.xxxx == int4(0, 1, 2, 3));
        r3.xy = cmp((int2)r2.xx == int2(4, 5));
        r6.xyzw = r6.xyzw ? float4(1, 1, 1, 1) : 0;
        r3.xy = r3.xy ? float2(1, 1) : 0;
        r1.w = dot(r6.xyz, Params[2].yzw);
        r1.w = r6.w * Params[3].x + r1.w;
        r1.w = r3.x * Params[3].y + r1.w;
        r6.x = r3.y * Params[3].z + r1.w;
        r7.xyzw = (int4)r2.xxxx + int4(1, 1, 2, 2);
        r8.xyzw = cmp((int4)r7.yyyy == int4(0, 1, 2, 3));
        r9.xyzw = cmp((int4)r7.xyzw == int4(4, 5, 4, 5));
        r8.xyzw = r8.xyzw ? float4(1, 1, 1, 1) : 0;
        r9.xyzw = r9.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.w = dot(r8.xyz, Params[2].yzw);
        r1.w = r8.w * Params[3].x + r1.w;
        r1.w = r9.x * Params[3].y + r1.w;
        r6.y = r9.y * Params[3].z + r1.w;
        r7.xyzw = cmp((int4)r7.wwww == int4(0, 1, 2, 3));
        r7.xyzw = r7.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.w = dot(r7.xyz, Params[2].yzw);
        r1.w = r7.w * Params[3].x + r1.w;
        r1.w = r9.z * Params[3].y + r1.w;
        r6.z = r9.w * Params[3].z + r1.w;
        r5.x = r5.y * r5.y;
        r7.x = dot(r6.xzy, float3(0.5, 0.5, -1));
        r7.y = dot(r6.xy, float2(-1, 1));
        r7.z = dot(r6.xy, float2(0.5, 0.5));
        r5.z = 1;
        r1.x = dot(r5.xyz, r7.xyz);
      } else {
        r1.w = cmp(r2.y < Params[1].z);
        r1.y = r1.y * 0.30103001 + -Params[0].w;
        r1.y = 3 * r1.y;
        r2.x = Params[1].z + -Params[0].w;
        r1.y = r1.y / r2.x;
        r2.x = (int)r1.y;
        r2.w = trunc(r1.y);
        r5.y = -r2.w + r1.y;
        r6.xyzw = cmp((int4)r2.xxxx == int4(0, 1, 2, 3));
        r3.xy = cmp((int2)r2.xx == int2(4, 5));
        r6.xyzw = r6.xyzw ? float4(1, 1, 1, 1) : 0;
        r3.xy = r3.xy ? float2(1, 1) : 0;
        r1.y = Params[4].x * r6.y;
        r1.y = r6.x * Params[3].w + r1.y;
        r1.y = r6.z * Params[4].y + r1.y;
        r1.y = r6.w * Params[4].z + r1.y;
        r1.y = r3.x * Params[4].w + r1.y;
        r6.x = r3.y * Params[5].x + r1.y;
        r7.xyzw = (int4)r2.xxxx + int4(1, 1, 2, 2);
        r8.xyzw = cmp((int4)r7.yyyy == int4(0, 1, 2, 3));
        r9.xyzw = cmp((int4)r7.xyzw == int4(4, 5, 4, 5));
        r8.xyzw = r8.xyzw ? float4(1, 1, 1, 1) : 0;
        r9.xyzw = r9.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.y = Params[4].x * r8.y;
        r1.y = r8.x * Params[3].w + r1.y;
        r1.y = r8.z * Params[4].y + r1.y;
        r1.y = r8.w * Params[4].z + r1.y;
        r1.y = r9.x * Params[4].w + r1.y;
        r6.y = r9.y * Params[5].x + r1.y;
        r7.xyzw = cmp((int4)r7.wwww == int4(0, 1, 2, 3));
        r7.xyzw = r7.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.y = Params[4].x * r7.y;
        r1.y = r7.x * Params[3].w + r1.y;
        r1.y = r7.z * Params[4].y + r1.y;
        r1.y = r7.w * Params[4].z + r1.y;
        r1.y = r9.z * Params[4].w + r1.y;
        r6.z = r9.w * Params[5].x + r1.y;
        r5.x = r5.y * r5.y;
        r7.x = dot(r6.xzy, float3(0.5, 0.5, -1));
        r7.y = dot(r6.xy, float2(-1, 1));
        r7.z = dot(r6.xy, float2(0.5, 0.5));
        r5.z = 1;
        r1.y = dot(r5.xyz, r7.xyz);
        r2.x = -Params[2].x * Params[1].z + Params[1].w;
        r2.x = r2.y * Params[2].x + r2.x;
        r1.x = r1.w ? r1.y : r2.x;
      }
    }
    r1.x = 3.32192802 * r1.x;
    r4.y = exp2(r1.x);
    if (r3.z != 0) {
      r1.x = -Params[0].z * Params[0].x + Params[0].y;
      r1.x = r2.z * Params[0].z + r1.x;
    } else {
      r1.y = cmp(r2.z < Params[0].w);
      if (r1.y != 0) {
        r1.y = r1.z * 0.30103001 + -Params[0].x;
        r1.y = 3 * r1.y;
        r1.w = Params[0].w + -Params[0].x;
        r1.y = r1.y / r1.w;
        r1.w = (int)r1.y;
        r2.x = trunc(r1.y);
        r3.y = -r2.x + r1.y;
        r5.xyzw = cmp((int4)r1.wwww == int4(0, 1, 2, 3));
        r2.xy = cmp((int2)r1.ww == int2(4, 5));
        r5.xyzw = r5.xyzw ? float4(1, 1, 1, 1) : 0;
        r2.xy = r2.xy ? float2(1, 1) : 0;
        r1.y = dot(r5.xyz, Params[2].yzw);
        r1.y = r5.w * Params[3].x + r1.y;
        r1.y = r2.x * Params[3].y + r1.y;
        r5.x = r2.y * Params[3].z + r1.y;
        r6.xyzw = (int4)r1.wwww + int4(1, 1, 2, 2);
        r7.xyzw = cmp((int4)r6.yyyy == int4(0, 1, 2, 3));
        r8.xyzw = cmp((int4)r6.xyzw == int4(4, 5, 4, 5));
        r7.xyzw = r7.xyzw ? float4(1, 1, 1, 1) : 0;
        r8.xyzw = r8.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.y = dot(r7.xyz, Params[2].yzw);
        r1.y = r7.w * Params[3].x + r1.y;
        r1.y = r8.x * Params[3].y + r1.y;
        r5.y = r8.y * Params[3].z + r1.y;
        r6.xyzw = cmp((int4)r6.wwww == int4(0, 1, 2, 3));
        r6.xyzw = r6.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.y = dot(r6.xyz, Params[2].yzw);
        r1.y = r6.w * Params[3].x + r1.y;
        r1.y = r8.z * Params[3].y + r1.y;
        r5.z = r8.w * Params[3].z + r1.y;
        r3.x = r3.y * r3.y;
        r6.x = dot(r5.xzy, float3(0.5, 0.5, -1));
        r6.y = dot(r5.xy, float2(-1, 1));
        r6.z = dot(r5.xy, float2(0.5, 0.5));
        r3.z = 1;
        r1.x = dot(r3.xyz, r6.xyz);
      } else {
        r1.y = cmp(r2.z < Params[1].z);
        r1.z = r1.z * 0.30103001 + -Params[0].w;
        r1.z = 3 * r1.z;
        r1.w = Params[1].z + -Params[0].w;
        r1.z = r1.z / r1.w;
        r1.w = (int)r1.z;
        r2.x = trunc(r1.z);
        r3.y = -r2.x + r1.z;
        r5.xyzw = cmp((int4)r1.wwww == int4(0, 1, 2, 3));
        r2.xy = cmp((int2)r1.ww == int2(4, 5));
        r5.xyzw = r5.xyzw ? float4(1, 1, 1, 1) : 0;
        r2.xy = r2.xy ? float2(1, 1) : 0;
        r1.z = Params[4].x * r5.y;
        r1.z = r5.x * Params[3].w + r1.z;
        r1.z = r5.z * Params[4].y + r1.z;
        r1.z = r5.w * Params[4].z + r1.z;
        r1.z = r2.x * Params[4].w + r1.z;
        r5.x = r2.y * Params[5].x + r1.z;
        r6.xyzw = (int4)r1.wwww + int4(1, 1, 2, 2);
        r7.xyzw = cmp((int4)r6.yyyy == int4(0, 1, 2, 3));
        r8.xyzw = cmp((int4)r6.xyzw == int4(4, 5, 4, 5));
        r7.xyzw = r7.xyzw ? float4(1, 1, 1, 1) : 0;
        r8.xyzw = r8.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.z = Params[4].x * r7.y;
        r1.z = r7.x * Params[3].w + r1.z;
        r1.z = r7.z * Params[4].y + r1.z;
        r1.z = r7.w * Params[4].z + r1.z;
        r1.z = r8.x * Params[4].w + r1.z;
        r5.y = r8.y * Params[5].x + r1.z;
        r6.xyzw = cmp((int4)r6.wwww == int4(0, 1, 2, 3));
        r6.xyzw = r6.xyzw ? float4(1, 1, 1, 1) : 0;
        r1.z = Params[4].x * r6.y;
        r1.z = r6.x * Params[3].w + r1.z;
        r1.z = r6.z * Params[4].y + r1.z;
        r1.z = r6.w * Params[4].z + r1.z;
        r1.z = r8.z * Params[4].w + r1.z;
        r5.z = r8.w * Params[5].x + r1.z;
        r3.x = r3.y * r3.y;
        r6.x = dot(r5.xzy, float3(0.5, 0.5, -1));
        r6.y = dot(r5.xy, float2(-1, 1));
        r6.z = dot(r5.xy, float2(0.5, 0.5));
        r3.z = 1;
        r1.z = dot(r3.xyz, r6.xyz);
        r1.w = -Params[2].x * Params[1].z + Params[1].w;
        r1.w = r2.z * Params[2].x + r1.w;
        r1.x = r1.y ? r1.z : r1.w;
      }
    }
    r1.x = 3.32192802 * r1.x;
    r4.z = exp2(r1.x);
    r0.xyz = r0.xyz * r0.www + -r4.xyz;
    r0.xyz = ToneMapLuminanceApply * r0.xyz + r4.xyz;
    r0.xyz = max(Params[5].yyy, r0.xyz);
    r1.x = dot(float3(1.06537485, 1.44673368e-006, -0.0653710067), r0.xyz);
    r1.y = dot(float3(-3.4558721e-007, 1.20366347, -0.203667715), r0.xyz);
    r1.z = dot(float3(1.98354986e-008, 2.12240607e-008, 0.999999583), r0.xyz);
    r0.w = HighlightsFixAmount.x * HighlightsFixAmount.y;
    r1.xyz = r1.xyz + -r0.xyz;
    r0.xyz = r0.www * r1.xyz + r0.xyz;
    r0.xyz = -Params[5].yyy + r0.xyz;
    r0.w = Params[5].z + -Params[5].y;
    r0.xyz = r0.xyz / r0.www;
  }

  // THIS HAD TO GET FIXED BECAUSE OF DECOMPILING ERRORS
  // Check outputDevice on top
  r0.w = cmp(outputDevice == 1u);
  r1.xyz = r0.www ? float3(1.0258249,-0.0200528856,-0.00577135477) : float3(1,0,0);
  r2.xyz = r0.www ? float3(-0.00223499862,1.00458491,-0.00235229917) : float3(0,1,0);
  r3.xyz = r0.www ? float3(-0.00501333317,-0.0252900254,1.03030288) : float3(0,0,1);
  r1.xyz = outputDevice != 0u ? r1.xyz : float3(1.70505154,-0.621790707,-0.0832583979);
  r2.xyz = outputDevice != 0u ? r2.xyz : float3(-0.130257145,1.14080286,-0.0105485283);
  r3.xyz = outputDevice != 0u ? r3.xyz : float3(-0.0240032747,-0.128968775,1.15297174);
  r1.x = dot(r1.xyz, r0.xyz);
  r1.y = dot(r2.xyz, r0.xyz);
  r1.z = dot(r3.xyz, r0.xyz);
  r0.xyz = max(float3(0,0,0), r1.xyz);
  r1.xyz = min(float3(65504,65504,65504), r0.xyz);
  if (outputDevice == 0u) {
    r0.xyz = float3(12.9200001,12.9200001,12.9200001) * r1.xyz;
    r2.xyz = log2(r1.xyz);
    r2.xyz = float3(0.416666657,0.416666657,0.416666657) * r2.xyz;
    r2.xyz = exp2(r2.xyz);
    r2.xyz = r2.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
    r3.xyz = cmp(float3(0.00313080009,0.00313080009,0.00313080009) >= r1.xyz);
    r1.xyz = r3.xyz ? r0.xyz : r2.xyz;
  } else {
    if (r0.w != 0) {
      r0.x = cmp(Params[6].y >= 1);
      r0.yzw = log2(r1.xyz);

      // shitty contrast:
      r0.yzw = Params[6].yyy * r0.yzw;

      r0.yzw = exp2(r0.yzw);
      r2.xyz = float3(2, 2, 2) + -r1.xyz;
      r3.xyz = r2.xyz * r2.xyz;
      r2.xyz = saturate(r3.xyz * r2.xyz);
      r3.xyz = float3(1, 1, 1) + -r2.xyz;
      r2.xyz = r2.xyz * r0.yzw;
      r2.xyz = r1.xyz * r3.xyz + r2.xyz;
      r0.xyz = r0.xxx ? r0.yzw : r2.xyz;
      
      // brightness mutliplier
      r0.xyz = Params[1].yyy * r0.xyz;

      // Params[5].z is 1000.f for contrast 1.f and brightness 1.f
      // => Peak around 1000nits
      r0.xyz = Params[5].zzz * r0.xyz;
      r0.xyz = max(float3(0, 0, 0), r0.xyz);
      
      r0.xyz = min(float3(65504,65504,65504), r0.xyz);
      r0.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) * r0.xyz;
      r0.xyz = log2(r0.xyz);
      r0.xyz = float3(0.159301758,0.159301758,0.159301758) * r0.xyz;
      r0.xyz = exp2(r0.xyz);
      r2.xyz = r0.xyz * float3(18.8515625,18.8515625,18.8515625) + float3(0.8359375,0.8359375,0.8359375);
      r0.xyz = r0.xyz * float3(18.6875,18.6875,18.6875) + float3(1,1,1);
      r0.xyz = r2.xyz / r0.xyz;
      r0.xyz = log2(r0.xyz);
      r0.xyz = float3(78.84375,78.84375,78.84375) * r0.xyz;
      r1.xyz = exp2(r0.xyz);
    }
  }
  r1.w = 1;
  LUT[vThreadID.xyz] = r1.xyzw;
  return;
}
