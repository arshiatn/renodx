// The D3D9 bridge's result back into the resolved main target (bridge.hpp), in the game's own encoding: gamma 2.2 light
// over HDRParam.x, as the bloom and the final post read it (bridge_prepare's inverse).
sampler2D upscaled : register(s0);
float4 frame : register(c9);  // width, height, HDRParam.x, -

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
  const float3 light = max(tex2Dlod(upscaled, float4(uv, 0.f, 0.f)).rgb, 0.f);
  return float4(saturate(pow(light, 1.f / 2.2f) / max(frame.z, 1e-6f)), 1.f);
}
