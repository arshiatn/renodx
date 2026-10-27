// hdr_to_screen.hlsl ps30_main_high / _sm40 with ENABLE_DISTORTION (with or without depth of field): the scene read
// through the distortion offsets (heat haze), then the radial blur and its exposure. HDR: x Heat Haze.
// Render scale (DLSS addon): the scene here is DLSS's full-size output, but the distortion texture was drawn with the
// 3D passes into their top-left render scale part: it is read there (SI.render_scale_x / y). tonemap.hlsli.
// v26: below 100% its texels cover blocks of display pixels; the game's point sampler gave each block one offset (ATN
// debug17: a mosaic over fires). v28: it is drawn with DLSS's jitter but read over DLSS's steady image, so the haze
// moved by up to 1.5 display pixels every frame at 33% (ATN: it moves left and right): read where this pixel's scene
// point was drawn (the jitter from the DLSS addon), with a cubic B-spline (smooth at any scale). At 100% the game's.
#include "./tonemap.hlsli"

SamplerState distortion_sampler_s : register(s2);
Texture2D<float4> distortion_texture : register(t2);
StructuredBuffer<Auto_exposure_output> g_auto_exposure_input_buffer : register(t3);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  // depthoffieldshared.hlsl distort_uv: 0.01 x the offset, from the pixel's corner.
  const float2 half_pixel = 0.5f * screen_size.zw;
  float2 frame_tex = v0.xy * screen_size.zw - half_pixel;
  const float heat_haze = (HDR >= 1.f) ? SI.heat_haze : 1.f;
  float2 render_scale = float2(SI.render_scale_x, SI.render_scale_y);
  render_scale = (render_scale.x > 0.f && render_scale.y > 0.f) ? min(render_scale, 1.f) : float2(1.f, 1.f);
  const float2 jitter = float2(SI.render_jitter_x, SI.render_jitter_y);
  float2 offset;
  [branch]
  if (all(render_scale >= 1.f)) {
    offset = distortion_texture.SampleLevel(distortion_sampler_s, frame_tex, 0).xy;
  } else {
    float2 size;
    distortion_texture.GetDimensions(size.x, size.y);
    const float2 rendered = render_scale * size;
    const int2 last = int2(rendered + 0.5f) - 1;
    // Texel coordinates (centers at integers): a rendered pixel shows the scene at its center + jitter.
    const float2 position = v0.xy * screen_size.zw * rendered - 0.5f - jitter;
    const int2 base = int2(floor(position));
    const float2 t = position - base;
    const float2 t2 = t * t;
    const float2 t3 = t2 * t;
    const float2 weights[4] = {
      (1.f - 3.f * t + 3.f * t2 - t3) / 6.f,
      (4.f - 6.f * t2 + 3.f * t3) / 6.f,
      (1.f + 3.f * t + 3.f * t2 - 3.f * t3) / 6.f,
      t3 / 6.f,
    };
    offset = 0.f;
    [unroll]
    for (int row = 0; row < 4; ++row) {
      [unroll]
      for (int column = 0; column < 4; ++column) {
        const int2 texel = clamp(base + int2(column - 1, row - 1), 0, last);
        offset += weights[column].x * weights[row].y * distortion_texture.Load(int3(texel, 0)).xy;
      }
    }
  }
  frame_tex += heat_haze * 0.01f * offset + half_pixel;

  float3 colour = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, frame_tex, 0).rgb;
  float3 bloom = hdr_output_texture.SampleLevel(hdr_output_sampler_s, frame_tex, 0).rgb;

  // radial_blur_pixel: its 10 taps all read this pixel (the game's loop samples frame_tex, not its offset), each
  // saturated: no blur, highlights pulled toward 1 away from the blur centre.
  const float strength = radial_blur_strength;
  float2 centre = radial_blur_position * 0.5f + 0.5f;
  centre.y = 1.f - centre.y;
  float d = length((0.5f - frame_tex) - (0.5f - centre));
  float3 sum = (colour + 10.f * saturate(colour)) / 11.f;
  colour = lerp(colour, sum, saturate(d * 2.2f * strength));
  colour *= 1.f + strength * 0.75f;

  o0 = HdrToScreen(colour, bloom, g_auto_exposure_input_buffer[0].tone_mapper_brightness);
}
