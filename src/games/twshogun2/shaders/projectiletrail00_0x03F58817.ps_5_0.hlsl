// projectiletrail.hlsl ps30_main: the trails of arrows, bullets and artillery, drawn into the scene; written from the
// game's source (the decompile merges the packed inputs). Drawn again with render_normal_map (the trail's normal map
// into the distortion buffer).
// HDR: the color x VFX Brightness: Trails (not the distortion pass).
#include "../shared.h"

cbuffer camera_VS_PS : register(b0)
{
  float time_in_sec : packoffset(c35);
}

cbuffer projectile_trail_PS : register(b1)
{
  float render_normal_map : packoffset(c0);
}

SamplerState s_normal_map_s : register(s0);
Texture2D<float4> s_normal_map : register(t0);

void main(
  float4 position : SV_Position0,
  float4 colour : COLOR0,
  float u : TEXCOORD0,
  float fade_out_distance : TEXCOORD1,
  float distance : TEXCOORD2,
  float t : TEXCOORD3,
  float4 proj_pos : TEXCOORD4,
  float cross_fade_start_distance : TEXCOORD5,
  float cross_fade_range : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  const float dist_fade_out = saturate(fade_out_distance / distance);
  const float width_fade_out = 1.f - abs(u);
  float4 result = colour;
  float cross_fade_t = saturate((distance - cross_fade_start_distance) / cross_fade_range);
  if (render_normal_map != 0.f) {
    result.rgb = s_normal_map.Sample(s_normal_map_s, proj_pos.xy / 30.f - float2(0.f, time_in_sec * 0.05f)).rgb;
    cross_fade_t = 1.f - cross_fade_t;
  } else if (HDR >= 1.f) {
    result.rgb *= SI.vfx_trails;  // VFX Brightness: Trails
  }
  result.a *= width_fade_out * dist_fade_out * cross_fade_t;
  o0 = result;
}
