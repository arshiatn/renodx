// hdr_to_screen.hlsl ps30_main_high / _sm40 (with or without depth of field): no distortion. tonemap.hlsli.
#include "./tonemap.hlsli"

StructuredBuffer<Auto_exposure_output> g_auto_exposure_input_buffer : register(t2);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float2 frame_tex = v0.xy * screen_size.zw;
  float3 scene = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, frame_tex, 0).rgb;
  float3 bloom = hdr_output_texture.SampleLevel(hdr_output_sampler_s, frame_tex, 0).rgb;
  o0 = HdrToScreen(scene, bloom, g_auto_exposure_input_buffer[0].tone_mapper_brightness);
}
