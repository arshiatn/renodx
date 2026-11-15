// Spark motion (DLSS addon): for a sprite particle redrawn with a motion vertex shader (0x7A1E0D20-27), the motion
// DLSS wants per pixel (this frame to last frame, render pixels, without jitter), alpha 1 where written.
// UE 4.21's View uniform buffer (the sprite shaders' b1): ClipToPrevClip c114-c117 (unjittered), TemporalAAJitter c118
// (NDC, this frame in xy), ViewSizeAndInvSize c122.
cbuffer View : register(b1) {
  float4 view[123];
};

float4 main(float4 position : SV_Position, float4 clip : TEXCOORD0, float4 old_clip : TEXCOORD1) : SV_Target {
  const float2 jitter = view[118].xy;
  const float2 current_ndc = clip.xy / clip.w - jitter;
  // Where the corner was last frame, in this frame's camera (unjittered), then in last frame's camera.
  const float4 old_unjittered = float4(old_clip.xy - jitter * old_clip.w, old_clip.zw);
  const float4 previous = old_unjittered.x * view[114] + old_unjittered.y * view[115] + old_unjittered.z * view[116]
                          + old_unjittered.w * view[117];
  const float2 motion = (previous.xy / previous.w - current_ndc) * view[122].xy * float2(0.5f, -0.5f);
  return float4(all(isfinite(motion)) ? motion : 0.f, 0.f, 1.f);
}
