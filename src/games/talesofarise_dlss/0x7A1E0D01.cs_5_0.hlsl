/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private input-preparation shader, not a game replacement registration. Expeditions: Rome's (0xB69D8215) with
// Tales of Arise's (UE 4.21) TAA dilation: nearest depth among the diagonals 2 texels away.
Texture2D<float> SceneDepth : register(t0);
Texture2D<float2> PackedVelocity : register(t1);
Texture2D<float4> SceneColor : register(t2);
// Spark trails fix: UE's separate translucency (composited before the TAA: rgb the light particles add, alpha the
// background's share) and last frame's particle mask.
Texture2D<float4> Translucency : register(t3);
Texture2D<float> PreviousParticleMask : register(t4);
// Spark motion: the sprites' own motion this frame (render pixels to last frame; alpha 1 where a sprite was drawn), at
// the separate translucency's size.
Texture2D<float4> ParticleMotion : register(t5);
RWTexture2D<float> OutputDepth : register(u0);
RWTexture2D<float2> OutputMotion : register(u1);
RWTexture2D<float4> OutputColor : register(u2);
RWTexture2D<float> OutputParticleMask : register(u3);

cbuffer PreparationConstants : register(b0) {
  float4 ClipToPrevious[4];
  uint2 Extent;
  float2 PixelJitter;
  uint CopyColor;
  uint ParticleMask;          // 1: Translucency bound (Spark trails fix)
  uint PreviousParticleMaskValid;
  uint ParticleMotionValid;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  if (any(dispatch_id.xy >= Extent)) return;

  const int2 pixel = int2(dispatch_id.xy);
  const int2 last_pixel = int2(Extent) - 1;
  if (CopyColor != 0) {
    // Replacing TAA, NVIDIA writes the game's output: clean its input instead,
    // as non-finite pixels would also stay in NVIDIA's history.
    const float3 color = SceneColor.Load(int3(pixel, 0)).rgb;
    OutputColor[pixel] = float4(all(isfinite(color)) ? color : 0.f, 0.f);
  }
  const float center_depth = SceneDepth.Load(int3(pixel, 0));
  OutputDepth[pixel] = (isfinite(center_depth) ? center_depth : 0.f);

  // Match scene TAA's reverse-Z foreground dilation (2 texels in UE 4.21), including its diagonal tie order.
  const int2 top_left = clamp(pixel + int2(-2, -2), int2(0, 0), last_pixel);
  const int2 top_right = clamp(pixel + int2(2, -2), int2(0, 0), last_pixel);
  const int2 bottom_left = clamp(pixel + int2(-2, 2), int2(0, 0), last_pixel);
  const int2 bottom_right = clamp(pixel + int2(2, 2), int2(0, 0), last_pixel);
  const float4 diagonal_samples = float4(
      SceneDepth.Load(int3(top_left, 0)), SceneDepth.Load(int3(top_right, 0)),
      SceneDepth.Load(int3(bottom_left, 0)), SceneDepth.Load(int3(bottom_right, 0)));
  const float4 diagonal_depth = float4(
      (isfinite(diagonal_samples.x) ? diagonal_samples.x : -1.f),
      (isfinite(diagonal_samples.y) ? diagonal_samples.y : -1.f),
      (isfinite(diagonal_samples.z) ? diagonal_samples.z : -1.f),
      (isfinite(diagonal_samples.w) ? diagonal_samples.w : -1.f));
  const int2 top_pixel = (diagonal_depth.y < diagonal_depth.x ? top_left : top_right);
  const int2 bottom_pixel = (diagonal_depth.w < diagonal_depth.z ? bottom_left : bottom_right);
  const float top_depth = max(diagonal_depth.x, diagonal_depth.y);
  const float bottom_depth = max(diagonal_depth.z, diagonal_depth.w);
  const float foreground_depth = max(top_depth, bottom_depth);
  int2 velocity_pixel = pixel;
  float camera_depth = (isfinite(center_depth) ? center_depth : 0.f);
  if (foreground_depth > camera_depth) {
    camera_depth = foreground_depth;
    velocity_pixel = (bottom_depth < top_depth ? top_pixel : bottom_pixel);
  }

  // Input pixels include projection jitter; the camera matrix operates in unjittered clip space.
  const float2 current_ndc = (float2(pixel) + 0.5f - PixelJitter) / float2(Extent) * float2(2.f, -2.f) + float2(-1.f, 1.f);
  const float2 ndc_to_pixels = float2(Extent) * float2(0.5f, -0.5f);
  float2 motion_pixels = float2(0.f, 0.f);
  const float2 packed_velocity = PackedVelocity.Load(int3(velocity_pixel, 0));
  if (packed_velocity.x > 0.f && all(isfinite(packed_velocity))) {
    // UE stores current-minus-previous NDC; DLSS wants current-to-previous pixels.
    motion_pixels = -(packed_velocity * 4.00801611f - 2.00397754f) * ndc_to_pixels;
  } else {
    // ClipToPrevious must be unjittered. SDK evaluation receives pixel jitter separately.
    const float4 previous_clip = current_ndc.x * ClipToPrevious[0]
                                 + current_ndc.y * ClipToPrevious[1]
                                 + camera_depth * ClipToPrevious[2]
                                 + ClipToPrevious[3];
    if (all(isfinite(previous_clip)) && abs(previous_clip.w) > 1.e-6f) {
      motion_pixels = (previous_clip.xy / previous_clip.w - current_ndc) * ndc_to_pixels;
    }
  }

  // RG16F must remain finite even for invalid matrices or extreme disocclusions.
  if (!all(isfinite(motion_pixels))) {
    motion_pixels = float2(0.f, 0.f);
  }

  // Spark trails fix: small bright particles have no motion vectors, so DLSS (preset L most) keeps them in its history
  // where they were. Where a particle adds more light than its background has (sparks, embers, flames; not smoke or
  // haze), and where one was last frame (its mask moved with the motion, x 3/4 a frame), the motion points off the
  // screen: DLSS finds no history there and shows this frame (NVIDIA's bias current color mask does nothing for
  // presets L and M).
  if (ParticleMask != 0) {
    uint2 translucency_size;
    Translucency.GetDimensions(translucency_size.x, translucency_size.y);
    const uint2 translucency_pixel = min(uint2((float2(pixel) + 0.5f) * float2(translucency_size) / float2(Extent)),
                                         translucency_size - 1u);
    const float3 added = max(Translucency.Load(int3(translucency_pixel, 0)).rgb, 0.f);
    const float3 luma = float3(0.2126f, 0.7152f, 0.0722f);
    const float particle = dot(added, luma);
    const float background = dot(max(SceneColor.Load(int3(pixel, 0)).rgb - added, 0.f), luma);
    const float current_mask = (all(isfinite(added)) ? saturate(particle / (background + 1e-3f) - 1.f) : 0.f);
    float mask = current_mask;
    if (PreviousParticleMaskValid != 0) {
      const int2 previous_pixel = clamp(int2(floor(float2(pixel) + 0.5f + motion_pixels)), int2(0, 0), last_pixel);
      mask = max(mask, 0.75f * PreviousParticleMask.Load(int3(previous_pixel, 0)));
    }
    OutputParticleMask[pixel] = mask;
    // Spark motion: where a sprite is this frame and has its own motion, DLSS follows it (the spark stays sharp); where
    // one only was, and for other particle kinds, no history as before.
    float4 particle_motion = 0.f;
    if (ParticleMotionValid != 0) {
      particle_motion = ParticleMotion.Load(int3(translucency_pixel, 0));
    }
    if (current_mask > 0.25f && particle_motion.w > 0.f && all(isfinite(particle_motion.xy))) {
      motion_pixels = particle_motion.xy;
    } else if (mask > 0.25f) {
      motion_pixels = 4.f * float2(Extent);
    }
  }
  OutputMotion[pixel] = clamp(motion_pixels, -65504.f, 65504.f);
}
