// Frame generation's UI layer (fg_d3d12.hpp, user interface recomposition): where the presented frame differs from its
// HUD-less image the frame is UI (alpha 1), elsewhere nothing (0). DLSS-G takes it as its UI alpha (the UI's colour is
// then the frame's: Final = UI + (1 - alpha) x HUD-less holds everywhere) and lays that UI over the frames it
// generates instead of warping it with the scene. With `debug`, the frame with that layer tinted magenta too (the
// debug view the helper presents).
Texture2D<float4> frame : register(t0);
Texture2D<float4> hudless : register(t1);
RWTexture2D<unorm float> ui : register(u0);
RWTexture2D<unorm float4> debug_view : register(u1);

cbuffer Constants : register(b0) {
  uint2 size;
  float threshold;  // in the swap chain format's steps: differences below it (rounding) are the scene's
  uint debug;
};

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID) {
  if (any(pixel >= size)) return;
  const float3 presented = frame[pixel].rgb;
  const bool drawn = any(abs(presented - hudless[pixel].rgb) > threshold);
  ui[pixel] = (drawn ? 1.f : 0.f);
  if (debug != 0) debug_view[pixel] = float4(drawn ? lerp(presented, float3(0.6f, 0.f, 0.6f), 0.6f) : presented, 1.f);
}
