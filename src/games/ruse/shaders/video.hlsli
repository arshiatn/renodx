#ifndef RUSE_VIDEO_HLSLI_
#define RUSE_VIDEO_HLSLI_

// The wrappers declare the native Y/chroma constants and s0/s1/s2 samplers.
// Both video shaders use this conversion; their tint/fade composition differs.
float4 RuseDecodeVideo(float2 texcoord) {
  float4 r0;
  float4 r1;
  r0.xy = ChromaDimensions.zw + ChromaDimensions.zw;
  r1.x = 1.f / r0.x;
  r1.y = 1.f / r0.y;
  r0.xy = r1.xy * YDimensions.zw;
  r0.xy *= texcoord;
  r1 = tex2D(VTexture, r0.xy);
  r0 = tex2D(UTexture, r0.xy);
  r0.x -= 0.5f;
  r0.xy = r0.xx * float2(0.391000003f, 2.01799989f);
  r0.z = r1.x - 0.5f;
  r1 = tex2D(YTexture, texcoord);
  r0.w = r1.x - 0.0625f;
  r0.x = r0.w * 1.16400003f - r0.x;
  r1.z = r0.w * 1.16400003f + r0.y;
  r1.y = r0.z * -0.813000023f + r0.x;
  r0.x = r0.z * 1.59599996f;
  r1.x = r0.w * 1.16400003f + r0.x;
  r1.w = 1.f;
  return r1;
}

float4 RuseScaleVideo(float4 color) {
  if (SI.peak_white_nits > 0.f && SI.diffuse_white_nits > 0.f && SI.ui_draw > 0.5f) {
    // Restore native UNORM clipping before scaling the final video draw.
    color = saturate(color);
    color.rgb *= SI.video_scale;
  }
  return color;
}

#endif  // RUSE_VIDEO_HLSLI_
