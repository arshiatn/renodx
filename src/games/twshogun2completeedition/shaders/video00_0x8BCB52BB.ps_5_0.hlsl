// sprite.hlsl pixel_yuv: the game's videos (movies*.pack; BT.601 limited-range YUV -> gamma-encoded RGB) drawn as a UI
// sprite into the UI target (gamma 2.2, 1 = UI white, scaled by UI in the proxy). From 0x8BCB52BB.ps_5_0.cso (3Dmigoto).
// Mod: shown at Paper White instead of UI (as twpharaohdynasties' cinematics00); HDR: Video AutoHDR, an inverse tone
// mapping above mid gray toward Peak (as rysesonofrome's RyseExpandVideo), with Video Dithering against banding.
#include "../shared.h"

SamplerState s_diffuse_map_y_s : register(s0);
SamplerState s_diffuse_map_u_s : register(s1);
SamplerState s_diffuse_map_v_s : register(s2);
Texture2D<float4> t_diffuse_map_y : register(t0);
Texture2D<float4> t_diffuse_map_u : register(t1);
Texture2D<float4> t_diffuse_map_v : register(t2);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float w2 : TEXCOORD7,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  const float y = t_diffuse_map_y.SampleLevel(s_diffuse_map_y_s, v2.xy, 0).w;
  const float u = t_diffuse_map_u.SampleLevel(s_diffuse_map_u_s, v2.xy, 0).w;
  const float v = t_diffuse_map_v.SampleLevel(s_diffuse_map_v_s, v2.xy, 0).w;
  // The game's conversion; its 8-bit target clamped it.
  float3 encoded = saturate(y * 1.16412354f + u * float3(0, -0.391448975f, 2.01782227f)
                            + v * float3(1.59579468f, -0.813476563f, 0) + float3(-0.87065506f, 0.529705048f, -1.08166885f));

  float3 color = renodx::color::gamma::DecodeSafe(encoded);
  if (HDR >= 1.f && SI.video_autohdr > 0.f && !SHOGUN_SDR_OUTPUT)
  {
    if (SI.video_dithering != 0.f)
    {
      encoded += (frac(52.9829189f * frac(dot(floor(v0.xy), float2(0.06711056f, 0.00583715f)))) - 0.5f) / 255.f;
      color = renodx::color::gamma::DecodeSafe(encoded);
    }
    const float luminance = renodx::color::y::from::BT709(color);
    if (luminance > 0.18f)
    {
      const float t = saturate((luminance - 0.18f) / (1.f - 0.18f));
      const float white = lerp(1.f, SHOGUN_PEAK, SI.video_autohdr);
      color *= (luminance + (white - 1.f) * t * t) / luminance;
    }
  }

  o0.rgb = renodx::color::gamma::EncodeSafe(color * SHOGUN_PAPER_WHITE);
  o0.w = 1;
}
