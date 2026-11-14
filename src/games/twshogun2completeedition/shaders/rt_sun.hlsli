// Ray tracing (ray_tracing.hpp; twattila/rt_core.hpp through the Total War helper): the traced sun's and ambient
// visibility at t40 / t41 (render pixels; bound at the sun light draws while traced this frame, else nothing): the
// darker of each and the game's shadow mask / ambient occlusion.
#ifndef SRC_GAMES_TWSHOGUN2COMPLETEEDITION_SHADERS_RT_SUN_HLSLI_
#define SRC_GAMES_TWSHOGUN2COMPLETEEDITION_SHADERS_RT_SUN_HLSLI_

Texture2D<float> t_rt_sun : register(t40);
Texture2D<float> t_rt_ambient : register(t41);

float RtDarker(Texture2D<float> traced, float2 pixel, float game) {
  uint width, height;
  traced.GetDimensions(width, height);
  return (width != 0u) ? min(game, traced.Load(int3(pixel, 0))) : game;
}

// Coloured Shadows (ray_tracing.hpp OnSunDraw, with this frame's trace): at t50 the share of the sun's colour that cloth and
// sails took out on its way (1 - the colour they let through); unbound reads 0, the sun's colour as it is.
Texture2D<float4> t_rt_tint : register(t50);

float3 RtSunColour(float3 sun_colour, float2 pixel) {
  return sun_colour * (1.f - t_rt_tint.Load(int3(pixel, 0)).rgb);
}

#endif  // SRC_GAMES_TWSHOGUN2COMPLETEEDITION_SHADERS_RT_SUN_HLSLI_
