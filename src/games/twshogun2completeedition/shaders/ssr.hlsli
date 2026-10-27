// environment_specular_light.hlsl / hierarchical_depth_buffer.hlsl: the screen-space reflections' resolution.
// The game traces them at half its render size (2 render pixels per SSR pixel) into buffers half the display size.
// With the DLSS addon's render scale that was half the render size: 640x360 at 33%, noisy and flickering (ATN debug8).
// SSR Resolution Full traces at the buffers' size instead, up to the render size: the render size at 50% or less, half
// the display above (at 67% 1920 instead of 1267 wide; at 100% the game's). Shared by ssrhiz00, ssrray00,
// ssrfilter00 / 01 and ssrresolve00; needs camera_VS_PS's screen_size and g_viewport_dimensions declared.
#include "../shared.h"

// Render pixels per SSR pixel: 1 to 2 (the game's 2).
float SSRFactor()
{
  if (SI.ssr_resolution == 0.f) return 2.f;
  return g_viewport_dimensions.x / min(g_viewport_dimensions.x, screen_size.x * 0.5f);
}
