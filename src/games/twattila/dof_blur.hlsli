#ifndef SRC_TWATTILA_DOF_BLUR_HLSLI_
#define SRC_TWATTILA_DOF_BLUR_HLSLI_

// The game's depth of field blurs (depth_of_field.hlsl, depth_of_field_cs.hlsl: Attila, Thrones, WARHAMMER) clip every
// tap to 0 - 1 before averaging the HDR scene (before the tone mapper), so blurred highlights went dull. HDR keeps them
// (below 0 still cut); Rendering Mode SDR clips as the game. Include after the game's shared.h (HDR).

cbuffer camera_VS_PS : register(b0)
{
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
}

float3 DofTap(float3 color)
{
  return (HDR >= 1.f) ? max(color, 0.f) : saturate(color);
}

#endif  // SRC_TWATTILA_DOF_BLUR_HLSLI_
