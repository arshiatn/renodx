// The D3D9 bridge's inputs for the upscaler (bridge.hpp), one full-screen pass before the bloom reads the scene:
// oC0 the scene as light (the resolved main target holds the post's input / HDRParam, in the game's gamma 2.2), oC1 the
// depth (INTZ, 0 near), oC2 the motion in pixels to where each pixel was last frame.
// Motion: the camera pass (0x88292FC6) unprojects at the far plane; here each pixel's own depth goes through its
// matrices (c0-c3 the previous view-projection, c4-c7 the inverse of this one, c8.x RenderDt), and where the game's
// velocity (s2 .yz, x 0.05 / RenderDt + 0.502) disagrees with the far plane's, a moving object wrote it: its own.
sampler2D scene : register(s0);
sampler2D depth : register(s1);
sampler2D velocity : register(s2);
row_major float4x4 previous_view_projection : register(c0);
row_major float4x4 inverse_view_projection : register(c4);
float4 render_dt : register(c8);
float4 frame : register(c9);  // width, height, HDRParam.x, 1: the velocity is the game's

struct Output {
  float4 color : COLOR0;
  float4 depth : COLOR1;
  float4 motion : COLOR2;
};

float2 PreviousNdc(float2 ndc, float z) {
  float4 world = mul(inverse_view_projection, float4(ndc, z, 1.f));
  world /= world.w;
  const float4 previous = mul(previous_view_projection, world);
  return previous.xy / previous.w;
}

Output main(float2 uv : TEXCOORD0) {
  Output output;
  const float3 encoded = tex2Dlod(scene, float4(uv, 0.f, 0.f)).rgb * frame.z;
  output.color = float4(pow(max(encoded, 0.f), 2.2f), 1.f);
  const float z = tex2Dlod(depth, float4(uv, 0.f, 0.f)).r;
  output.depth = float4(z, 0.f, 0.f, 0.f);

  const float2 ndc = uv * float2(2.f, -2.f) + float2(-1.f, 1.f);
  float2 delta = ndc - PreviousNdc(ndc, z);
  if (frame.w > 0.f) {
    const float to_velocity = 0.05f / max(render_dt.x, 1e-6f);
    const float2 game = tex2Dlod(velocity, float4(uv, 0.f, 0.f)).yz - 0.5019608f;
    const float2 far_plane = (ndc - PreviousNdc(ndc, 1.f)) * to_velocity;
    // More than 1.5 code values from the far plane's: an object's (or the ground pre-pass's) own velocity.
    if (any(abs(game - far_plane) > 1.5f / 255.f) && any(abs(game - delta * to_velocity) > 1.5f / 255.f)) {
      delta = game / to_velocity;
    }
  }
  // NDC (y up) to pixels (y down), toward the last frame.
  output.motion = float4(-delta.x * 0.5f * frame.x, delta.y * 0.5f * frame.y, 0.f, 0.f);
  return output;
}
