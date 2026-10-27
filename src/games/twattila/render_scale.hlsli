#ifndef SRC_GAMES_TWATTILA_RENDER_SCALE_HLSLI_
#define SRC_GAMES_TWATTILA_RENDER_SCALE_HLSLI_

// DLSS below 100% (view_share.hpp binds it for the shaders that declare it): xy the draw's viewport over its target,
// zw the frame's render size over the display's. Unbound (100%) it reads 0.
cbuffer render_scale : register(b12) {
  float4 g_render_scale : packoffset(c0);
}

// The part of a display-size target the 3D draws into (after DLSS: of the textures it drew before).
float2 FrameRenderShare() {
  return (g_render_scale.z > 0.f && g_render_scale.w > 0.f) ? g_render_scale.zw : float2(1.f, 1.f);
}

// The part of its target this draw's viewport covers.
float2 DrawRenderShare() {
  return (g_render_scale.x > 0.f && g_render_scale.y > 0.f) ? g_render_scale.xy : float2(1.f, 1.f);
}

// A texel of a texture the 3D drew (Load coordinates, mip / sample index kept) read at the display's size.
int2 RenderShareTexel(float2 texel) {
  return int2(texel * FrameRenderShare());
}

int3 RenderShareTexel(float3 texel) {
  return int3(int2(texel.xy * FrameRenderShare()), int(texel.z));
}

#endif  // SRC_GAMES_TWATTILA_RENDER_SCALE_HLSLI_
