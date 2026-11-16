// The D3D9 bridge's full-screen quad (bridge.hpp): a triangle strip in clip space with D3D9's half-pixel offset
// (c0.xy = -1 / width, 1 / height) so each pixel samples its own texel.
float4 half_pixel : register(c0);

struct Output {
  float4 position : POSITION;
  float2 uv : TEXCOORD0;
};

Output main(float2 corner : POSITION) {
  Output output;
  output.position = float4(corner + half_pixel.xy, 0.f, 1.f);
  output.uv = corner * float2(0.5f, -0.5f) + 0.5f;
  return output;
}
