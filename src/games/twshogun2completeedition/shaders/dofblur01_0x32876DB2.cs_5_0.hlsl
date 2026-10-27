// summedareatable.hlsl cs_masked_blur_hashed: the depth of field blur over a masked layer (alpha: coverage, divided
// out). dofblur.hlsli.
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
  const float radius = BlurRadius(uv, t_blur_size.SampleLevel(s_blur_size, uv, 0).r, screen_size.x);

  const float pixel_size = 1.0f / max(image_size.x, image_size.y);
  if (radius < pixel_size)
  {
    const float4 center = t_image[vpos];
    if (center.a != 0)
    {
      t_blur_output[vpos] = float4(center.rgb / center.a, 1);
    }
    return;
  }

  const float2 circle = radius * float2((screen_size.y / screen_size.x), 1.0);
  float2 random = Rand2D(uv + g_blur_timer);
  const float2 uv_max = (rendered - 0.5f) / screen_size;

  const int num_samples = 30;
  float4 result = 0;
  [loop]
  for (int i = 0; i < num_samples - 1; i++)
  {
    result += t_image.SampleLevel(s_image, min(uv + circle * RandBokeh(random), uv_max), 0);
  }
  if (result.a == 0)
  {
    return;
  }
  result.rgb /= result.a;

  t_blur_output[vpos] = float4(result.rgb, 1);
}
