// Frame generation's hudless colour without the HDR addon (frame_generation.hpp): the tonemapper's output as it is.
Texture2D<float4> scene : register(t0);

float4 main(float4 position : SV_Position) : SV_Target {
  return scene.Load(int3(position.xy, 0));
}
