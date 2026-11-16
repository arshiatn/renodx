// Ray-Traced AO for every reader of the game's SSAO (ray_tracing.hpp CopyAmbient, after the frame's first sun light
// draw): the traced visibility (t0, render pixels from the top-left, as the sun light draws read it at t41) into the
// game's SSAO target (u0: ssao01's R8 output, whose collect writes the same pixels) for the readers after the sun light
// draws (terrain decals, rain skirts, burning panels, rivers, terrain tiles). The game's line sweep and collect are
// left out meanwhile.
Texture2D<float> t_rt_ambient : register(t0);
RWTexture2D<unorm float> u_game_ambient : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID) {
  uint width, height, target_width, target_height;
  t_rt_ambient.GetDimensions(width, height);
  u_game_ambient.GetDimensions(target_width, target_height);
  if (pixel.x >= min(width, target_width) || pixel.y >= min(height, target_height)) return;
  u_game_ambient[pixel.xy] = t_rt_ambient.Load(int3(pixel.xy, 0));
}
