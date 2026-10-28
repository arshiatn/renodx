// Frame generation's UI layer (fg_d3d12.hpp, user interface recomposition): where the presented frame differs from its
// HUD-less image the frame is UI (alpha 1, its colour premultiplied), elsewhere nothing. DLSS-G then lays the UI over the
// frames it generates instead of warping it with the scene (Final = UI + (1 - alpha) x HUD-less holds everywhere).
Texture2D<float4> frame : register(t0);
Texture2D<float4> hudless : register(t1);
RWTexture2D<float4> ui : register(u0);

cbuffer Constants : register(b0) {
  uint2 size;
  float threshold;  // in the swap chain format's steps: differences below it (rounding) are the scene's
  float padding;
};

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID) {
  if (any(pixel >= size)) return;
  const float3 presented = frame[pixel].rgb;
  const bool drawn = any(abs(presented - hudless[pixel].rgb) > threshold);
  ui[pixel] = drawn ? float4(presented, 1.f) : float4(0.f, 0.f, 0.f, 0.f);
}
