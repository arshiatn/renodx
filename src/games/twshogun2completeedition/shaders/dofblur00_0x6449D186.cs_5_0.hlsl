// summedareatable.hlsl cs_blur_hashed: the depth of field blur over the whole image. dofblur.hlsli.
#include "./dofblur.hlsli"

[numthreads(32, 32, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
  const uint2 vpos = dispatch_thread_id.xy;
  float2 screen_size;
  t_blur_output.GetDimensions(screen_size.x, screen_size.y);
  const float2 rendered = floor(screen_size * RenderScale());
  if (any(float2(vpos) >= rendered)) return;

  const float2 uv = (vpos + 0.5f) / screen_size;
  float2 image_size;
  t_image.GetDimensions(image_size.x, image_size.y);
  const float radius = BlurRadius(uv, t_blur_size.SampleLevel(s_blur_size, uv, 0).x, screen_size.x);

  const float pixel_size = 1.0f / max(image_size.x, image_size.y);
  if (radius < pixel_size)
  {
    t_blur_output[vpos] = float4(t_image[vpos].rgb, 1);
    return;
  }

  const float2 circle = radius * float2((screen_size.y / screen_size.x), 1.0);
  float2 random = Rand2D(uv + frac(g_blur_timer));
  // Taps inside the rendered part (its last texel centre).
  const float2 uv_max = (rendered - 0.5f) / screen_size;

  const int num_samples = 30;
  float3 result = 0;
  [loop]
  for (int i = 0; i < num_samples; i++)
  {
    result += t_image.SampleLevel(s_image, min(uv + circle * RandBokeh(random), uv_max), 0).rgb;
  }
  result /= num_samples;

  t_blur_output[vpos] = float4(result.rgb, 1);
}
