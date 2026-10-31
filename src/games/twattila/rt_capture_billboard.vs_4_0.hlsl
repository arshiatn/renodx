// Attila's far trees (the game's rigidtreebillboard vs30_main, 0x4D50E1C7) with their world position and UV for ray
// tracing's stream output (rt_core.hpp, SceneShader::sun_facing): the card turned to the sun instead of the camera and
// showing the tree from there, so its shadow keeps its shape and does not swing with the view. t0: rt_sun, its first
// texel the direction toward the sun.
#include "./rt_capture.hlsli"

Texture2D<float4> t_rt_sun : register(t0);

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float v4 : TEXCOORD3,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0)
{
  const float3 toward_sun = t_rt_sun.Load(int3(0, 0, 0)).xyz;
  const float across = length(toward_sun.xz);
  const float2 from_tree = (across > 1e-3f) ? toward_sun.xz / across : float2(0.f, 1.f);

  // The game's card, its right vector from the sun's direction.
  const float3 right = float3(from_tree.y, 0.f, -from_tree.x);
  const float3 world = (right * v0.x + float3(0.f, v0.y, 0.f)) * v3.w + v2.xyz;

  // Which of the four views shows the tree from there (the game's angle to the tree's facing), the nearest.
  const float2 facing = normalize(v3.xz);
  const float side = facing.x * from_tree.y - facing.y * from_tree.x;
  const float angle = acos(clamp(dot(facing, from_tree), -1.f, 1.f));
  const float view = 4.f * frac(((side < 0.f) ? angle : (6.28318548f - angle)) * 0.159154937f);
  const float2 uv = float2(v1.x + 0.25f * fmod(floor(view + 0.5f), 4.f), v1.y);

  // A card the game has faded out away (v2.w 0: its pixel shader discards all of it): a NaN, which DXR skips.
  world_uv = RtWorldUv((v2.w > 0.f) ? world : asfloat(0x7FC00000u).xxx, uv);
  position = 0.f;
}
