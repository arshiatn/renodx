// Part 6 (Divinity 2): the add-on's PsychoV35 evaluated directly on each input colour (no LUT), the game's SDR picture Hue
// Clip aims at, and the stacked-chain control (V35 fed the game's own tone curve's output, Divinity 1's mistake).
// Compiled by part6_divinity2.ps1 with the Divinity 2 folder as the include path.
#include "shared.h"
#include "../twattila/psycho35.hlsli"
#include "shaders/vanilla.hlsli"
#include "shaders/vanillaplus.hlsli"

cbuffer Direct : register(b1) {
  // 0 V35 (BT.2020 nits), 1 stacked control (BT.2020 nits), 2 V35 + the SDR reference (BT.709, Paper White 1),
  // 3 V35 + the earlier stand-in for the reference (RenoDX's ACES at 0.02 / 48 nits, a control)
  uint direct_mode;
  float luminance_apply;  // t00's ToneMapLuminanceApply / HighlightsFixAmount as traced
  float2 highlights_fix;
};

StructuredBuffer<float4> inputs : register(t0);
RWStructuredBuffer<float4> outputs : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const float3 scene_bt709 = inputs[id.x].xyz;
  const float relative_peak = DOS2_PEAK_NITS / max(SI.diffuse_white_nits, 0.0001f);
  float3 v35_input = scene_bt709;
  if (direct_mode == 1u) {
    // The game's own 1000-nit curve first, its nits over the 203 reference as Psycho's input.
    v35_input = renodx::color::bt709::from::AP1(
        dos2_vanilla::ToneMap(renodx::color::ap1::from::BT709(scene_bt709)) / dos2_vanilla::REFERENCE_PAPER_WHITE);
  }
  if (SI.hue_clip > 0.f) {
    v35_input = renodx::tonemap::psychov::HueTransfer(
        v35_input, dos2_vanillaplus::SdrReference(v35_input, luminance_apply, highlights_fix), SI.hue_clip);
  }
  const float mid_gray = (SI.v35_mid_gray != 0.f)
                             ? renodx::color::y::from::BT709(dos2_vanillaplus::SdrReference(
                                   float3(DOS2_GRAY_INPUT, DOS2_GRAY_INPUT, DOS2_GRAY_INPUT), luminance_apply,
                                   highlights_fix))
                             : DOS2_GRAY_OUTPUT;
  const float3 v35_bt709 = ApplyPsychoV35(v35_input, relative_peak, false, mid_gray);
  if (direct_mode >= 2u) {
    const float3 sdr_reference = (direct_mode == 2u)
                                     ? dos2_vanillaplus::SdrReference(scene_bt709, luminance_apply, highlights_fix)
                                     : saturate(renodx::tonemap::aces::RRTAndODT(scene_bt709, 0.02f, 48.f) / 48.f);
    outputs[id.x * 2u] = float4(v35_bt709, 0.f);
    outputs[id.x * 2u + 1u] = float4(sdr_reference, mid_gray);
    return;
  }
  outputs[id.x * 2u] = float4(renodx::color::bt2020::from::BT709(v35_bt709) * SI.diffuse_white_nits, mid_gray);
  outputs[id.x * 2u + 1u] = 0.f;
}
