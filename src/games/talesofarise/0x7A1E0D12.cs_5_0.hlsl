// Ray-traced effects: per pixel the world position and scene depth (as Bandai's sun shadow mask 0x2084C889 finds them;
// w 0 for the sky), and in sun[0] the direction toward the sun, in sun[1] the camera, in sun[2-7] the camera's
// projection for the next frame's bounce light, for D3D12's ray queries.
#include "./sun_direction.hlsli"

Texture2D<float> depth : register(t0);
RWTexture2D<float4> positions : register(u0);
RWTexture2D<float4> sun : register(u1);

// The sun shadow mask's b0: ScreenToShadow c28-c31 (screen position x depth, depth, 1 -> its shadow map).
cbuffer Shadow : register(b0) {
  float4 shadow[32];
};

// UE 4.21's View uniform buffer (its b1): TranslatedWorldToClip c0-c3, ScreenToWorld c44-c47,
// InvDeviceZToWorldZTransform c57, ScreenPositionScaleBias c58, PreViewTranslation c62, ViewSizeAndInvSize c122,
// BufferSizeAndInvSize c123.
cbuffer View : register(b1) {
  float4 view[124];
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (all(id.xy == 0u)) {
    sun[uint2(0, 0)] = TowardSun(shadow[28], shadow[29], shadow[30], view[44].xyz, view[45].xyz, view[46].xyz);
    sun[uint2(1, 0)] = float4(-view[62].xyz, 0.f);
    // The camera for the next frame's bounce light (rt_surface.hlsli): TranslatedWorldToClip c0-c3 (jittered, as the
    // scene colour it reads), PreViewTranslation c62, ViewSizeAndInvSize c122.
    [unroll]
    for (uint row = 0; row < 4; ++row) {
      sun[uint2(2 + row, 0)] = view[row];
    }
    sun[uint2(6, 0)] = float4(view[62].xyz, 0.f);
    sun[uint2(7, 0)] = view[122];
  }
  uint2 size;
  positions.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float device_z = depth.Load(int3(id.xy, 0));
  // Reversed Z: the sky is 0.
  if (device_z <= 0.f) {
    positions[id.xy] = 0.f;
    return;
  }
  const float scene_depth = device_z * view[57].x + view[57].y + 1.f / (device_z * view[57].z - view[57].w);
  const float2 uv = (float2(id.xy) + 0.5f) * view[123].zw;
  const float2 screen = (uv - view[58].wz) / view[58].xy;
  const float3 world = screen.x * scene_depth * view[44].xyz + screen.y * scene_depth * view[45].xyz
                       + scene_depth * view[46].xyz + view[47].xyz;
  positions[id.xy] = float4(world, scene_depth);
}
