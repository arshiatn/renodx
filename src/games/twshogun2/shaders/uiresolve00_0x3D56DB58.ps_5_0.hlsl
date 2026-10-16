// ui_resolve.hlsl ps40_copy: the UI render target onto the frame. With the game's HDR on (g_hdr_display) it went
// to PQ at the game's UI nits and was blended in PQ; here always the SDR copy (gamma 2.2, 1 = UI white): the proxy
// scales it by UI.
SamplerState s_point_sampler_s : register(s0);
Texture2D<float4> t_in_UI_RT : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0 = t_in_UI_RT.SampleLevel(s_point_sampler_s, v1.xy, 0);
}
