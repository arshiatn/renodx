// mtbcampaignterrainoverlay.hlsl ps50_main (the campaign map's terrain overlay: region colours from a lookup and a palette
// at each pixel's map position), written from the game's D7E27B35 disassembly (3Dmigoto dropped its four lookups' texel
// offsets). Render scale (DLSS addon): it took the pixel's NDC over the whole target (SV_Position x
// g_inv_screen_size_minus_one, the display size) while the map draws into the top-left render part (the 2026-11-14 campaign
// trace at 33%: viewport 1280 x 720 on a 3840 x 2160 target), so the overlay's map positions came from the wrong NDC. The NDC
// is taken over the viewport (g_viewport_dimensions, which the DLSS addon sets to the render size) when that is smaller; the
// depth texel is the game's (its uv is right for the top-left part). At full size the game's, bit for bit.
cbuffer camera_VS_PS : register(b0)
{
  float4 cb0[43];
}

cbuffer shroud_PS : register(b1)
{
  float4 cb1[3];  // [2]: g_campaign_playable_area_bounds
}

cbuffer campaign_terrain_overlay_cb : register(b2)
{
  float4 cb2[1];  // [0].x: g_cto_blend_factor
}

Texture2D<float4> gbuffer_colour_texture : register(t0);
Texture2D<float4> gbuffer_readable_depth_texture : register(t1);
Texture2D<uint> t_lookup_texture : register(t2);
Texture2D<float4> t_palette_texture : register(t3);

float4 main(float4 v0 : SV_Position0) : SV_Target0
{
  const float2 pixel = (v0.xy + cb0[35].zz) + float2(-0.5f, -0.5f);
  const float2 uv = pixel * cb0[42].xy;
  uint2 colour_size;
  gbuffer_colour_texture.GetDimensions(colour_size.x, colour_size.y);
  const int2 texel = min(int2(colour_size) + int2(-1, -1), max(int2(0, 0), int2(floor(uv * float2(colour_size)))));
  // cb0[38].xy: the viewport (render size below 100%), cb0[37].xy: the display size.
  const float2 ndc_uv = all(cb0[38].xy >= cb0[37].xy) ? uv : pixel / (cb0[38].xy - 1.f);
  const float4 position = float4(ndc_uv * float2(2.f, -2.f) + float2(-1.f, 1.f),
                                 gbuffer_readable_depth_texture.Load(int3(texel, 0)).x, 1.f);
  const float3 map = float3(dot(position, cb0[30]), dot(position, cb0[32]), dot(position, cb0[33]));
  const float2 area = ((map.xy / map.z) - cb1[2].xy) / (cb1[2].zw - cb1[2].xy);
  uint2 lookup_size;
  t_lookup_texture.GetDimensions(lookup_size.x, lookup_size.y);
  const float2 lookup = float2(area.x, 1.f - area.y) * float2(lookup_size) + float2(0.f, -0.5f);
  const float2 blend = frac(lookup);
  const int3 base = int3(uint2(lookup), 0);
  const float4 corner11 = t_palette_texture.Load(int3(t_lookup_texture.Load(base, int2(1, 1)), 0, 0));
  const float4 corner01 = t_palette_texture.Load(int3(t_lookup_texture.Load(base, int2(0, 1)), 0, 0));
  const float4 bottom = blend.x * (corner11 - corner01) + corner01;
  const float4 corner10 = t_palette_texture.Load(int3(t_lookup_texture.Load(base, int2(1, 0)), 0, 0));
  const float4 corner00 = t_palette_texture.Load(int3(t_lookup_texture.Load(base), 0, 0));
  const float4 top = blend.x * (corner10 - corner00) + corner00;
  const float4 colour = blend.y * (bottom - top) + top;
  return float4(colour.xyz, colour.w * cb2[0].x);
}
