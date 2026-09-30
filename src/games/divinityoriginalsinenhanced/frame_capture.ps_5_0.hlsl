// Frame capture (addon.cpp): the 16-bit frame for the game's 8-bit copy.
// The 8-bit target clamps to 0-1, like the game's own back buffer.
Texture2D<float4> t0 : register(t0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  return t0.Load(int3(int2(pos.xy), 0));
}
