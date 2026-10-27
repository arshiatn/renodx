#ifndef SRC_RYSESONOFROME_SHARED_H_
#define SRC_RYSESONOFROME_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float swap_chain_encoding;  // 0 HDR10, 1 scRGB, 2 SDR (gamma 2.2; Peak, Paper White and UI unused)

  float hdr;  // 0 SDR, 1 HDR (Extended), 2 HDR (PsychoV31)
  float bloom;
  float vignette;
  float sun_shafts;
  float lens_flare;
  float chromatic_aberration;  // the game's full-screen one, x 12 (2 x r_FlaresChromaShift); the game's: 0
  float film_grain;
  float sharpening;
  float motion_blur_camera;
  float motion_blur_object;
  float depth_of_field;
  float video_highlights;  // ITM: 0 SDR, 1 video white at Peak
  float video_dithering;   // 1: 1 code value of dither before the ITM

  // PsychoV31
  float exposure;
  float highlights;
  float shadows;
  float contrast;  // x PsychoV31's own (1)
  float saturation;
  float highlight_saturation;
  float roll_off;      // 0 latest, 1 default, 2 soonest
  float game_shadows;  // 0 PsychoV31's, 1 Hable's toe below mid gray

  // Extended
  float ext_exposure;
  float ext_highlights;
  float ext_shadows;
  float ext_contrast;
  float ext_saturation;
  float ext_highlight_saturation;

  // PsychoV31, appended.
  float highlight_contrast;          // above mid gray, 1 V31's own
  float shadow_contrast;             // below mid gray, 1 V31's own
  float flare;                       // 0 none, deeper blacks above it
  float dechroma;                    // 0 none, bright colors toward white above it
  float gamut_compression;           // 0 none (the display clips), 1 full
  float gamut_compression_mode;      // V31's target: 0 BT.709, 1 BT.2020, 3 Display P3
  float highlight_hue_shift;         // hue toward the game's SDR curve above mid gray, 0 - 1
  float highlight_saturation_shift;  // saturation toward it, as the hue shift, 0 - 1
  float fire_hue;                    // warm colors' hue above mid gray: - toward red, + toward yellow, 0 none
  float fire_hue_focus;              // warm colors toward one orange above mid gray, 0 none - 1
  float ext_fire_hue;                // Extended's, as fire_hue
  float ext_fire_hue_focus;          // Extended's, as fire_hue_focus
  float ext_highlight_hue_shift;         // Extended's, as highlight_hue_shift
  float ext_highlight_saturation_shift;  // Extended's, as highlight_saturation_shift
  // Ambient occlusion (appended): the SSDO pass (ssdo00) as GT-VBAO.
  float ssao_mode;        // 0 the game's SSDO, 1 GT-VBAO
  float ssao_radius;      // metres
  float ssao_strength;    // an exponent on the visibility
  float ssao_thickness;   // metres
  float ssao_quality;     // 0-3: 8 x 2^n samples per side of a slice
  float ssao_directions;  // 0-3: 2^n slices
  float ssao_frame;       // the noise's frame
  // Contact shadows (appended; contact_shadows.hpp, read on the CPU).
  float contact_shadows;           // strength, 0 off
  float contact_shadow_length;     // metres
  float contact_shadow_thickness;  // metres
  // Light Bounce (appended; light_bounce.hpp, read on the CPU).
  float light_bounce;              // strength, 0 off
  float light_bounce_radius;       // metres
  float light_bounce_quality;      // 0-2: 8 x 2^n samples per side of a slice
  float light_bounce_directions;   // 0-3: 2^n slices
  float light_bounce_resolution;   // 0-2: the trace at 1 / 2^n of the resolution
  float light_bounce_multi;        // 0-1: the last frame's bounce bouncing again
  float light_bounce_denoise;      // 1: smoothed along surfaces
  // Sun Shadow Softness (appended; sunshadow00-04, sun_shadow_pcss.hlsli).
  float sun_shadow_softness;  // the sun's size in degrees; 0 the game's filter
};

#define SI  shader_injection
#define HDR SI.hdr

// SDR output (displays without HDR): the display's white is the game's and the UI's.
#define RYSE_SDR_OUTPUT (SI.swap_chain_encoding == 2.f)
// Peak relative to Paper White.
#define RYSE_PEAK \
  (RYSE_SDR_OUTPUT ? 1.f : (max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 1.f)))
// Paper White / UI as a gain on gamma 2.2 encoded values (the proxy decodes 2.2 x UI nits).
#define RYSE_ENCODED_SCALE \
  (RYSE_SDR_OUTPUT ? 1.f : pow(max(SI.diffuse_white_nits, 1.f) / max(SI.graphics_white_nits, 1.f), 1.f / 2.2f))

#ifndef __cplusplus
#if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b13, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b13) {
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

#include "../../shaders/renodx.hlsl"

// The game's sRGB encode (CSO constants), signed, unclamped.
float RyseEncodeSRGB(float x) {
  float a = abs(x);
  float e = (a < 0.00313080009f) ? (12.9200001f * a)
                                 : (exp2(log2(a) * 0.416666657f) * 1.05499995f - 0.0549999997f);
  return (x < 0.f) ? -e : e;
}

float3 RyseEncodeSRGB(float3 x) {
  return float3(RyseEncodeSRGB(x.r), RyseEncodeSRGB(x.g), RyseEncodeSRGB(x.b));
}

// What the game's sRGB views returned, signed, unclamped.
float RyseDecodeSRGB(float x) {
  float a = abs(x);
  float d = (a <= 0.04045f) ? (a / 12.92f) : pow((a + 0.055f) / 1.055f, 2.4f);
  return (x < 0.f) ? -d : d;
}

float3 RyseDecodeSRGB(float3 x) {
  return float3(RyseDecodeSRGB(x.r), RyseDecodeSRGB(x.g), RyseDecodeSRGB(x.b));
}

// Bilinear (clamp) of decoded texels, like an sRGB view: decode, then filter. rgb / encoded_scale.
float4 RyseSampleSRGB(Texture2D<float4> tex, float2 uv, float encoded_scale) {
  uint width, height;
  tex.GetDimensions(width, height);
  float2 position = uv * float2(width, height) - 0.5f;
  float2 base = floor(position);
  float2 weight = position - base;
  int2 last = int2(width, height) - 1;
  int2 p0 = clamp(int2(base), 0, last);
  int2 p1 = clamp(int2(base) + 1, 0, last);
  float4 c00 = tex.Load(int3(p0.x, p0.y, 0));
  float4 c10 = tex.Load(int3(p1.x, p0.y, 0));
  float4 c01 = tex.Load(int3(p0.x, p1.y, 0));
  float4 c11 = tex.Load(int3(p1.x, p1.y, 0));
  c00.rgb = RyseDecodeSRGB(c00.rgb / encoded_scale);
  c10.rgb = RyseDecodeSRGB(c10.rgb / encoded_scale);
  c01.rgb = RyseDecodeSRGB(c01.rgb / encoded_scale);
  c11.rgb = RyseDecodeSRGB(c11.rgb / encoded_scale);
  return lerp(lerp(c00, c10, weight.x), lerp(c01, c11, weight.x), weight.y);
}

// The game's screen blend (base + blend x (1 - base), encoded) for HDR bases. Stock: (1 - base) saturated, flat at
// white and a crease where the base passes 1 (rings around HDR bloom). Here: 1 - base up to 0.5, then a C1
// exponential tail (0.5 e^(-2 (base - 0.5))) that never reaches 0: smooth past white.
float3 RyseScreen(float3 base, float3 blend) {
  float3 room = (base < 0.5f) ? saturate(1.f - base) : 0.5f * exp2(-2.885390f * (base - 0.5f));
  return base + room * saturate(blend);
}

// The luminance of light an SDR blend lost past white, added back to a display-linear color in that color's own
// hue (SDR's: per channel, the lost light is mostly one channel, e.g. orange shafts), rolled off (Neutwo, by
// the max channel) into the headroom left below peak.
float3 RyseAddLight(float3 color, float3 lost, float peak) {
  float lost_y = renodx::color::y::from::BT709(lost);
  float color_y = renodx::color::y::from::BT709(color);
  if (!(lost_y > 0.f && color_y > 0.f)) return color;
  float color_max = renodx::math::Max(color);
  float headroom = max(peak - color_max, 0.f);
  return color * (1.f + renodx::tonemap::Neutwo(color_max * lost_y / color_y, headroom) / color_max);
}

// Video Highlights (ITM): SDR video, gamma 2.2 encoded (1 = video white). Luminance above mid gray stretched
// (smooth start) so white reaches lerp(1, Peak, amount); hue kept. 8-bit sources band if stretched far: Video
// Dithering adds 1 code value (peak to peak) of interleaved gradient noise, static in screen space, first; the
// stretch scales it like the steps.
float3 RyseExpandVideo(float3 encoded, float2 pixel) {
  if (SI.video_dithering != 0.f) {
    encoded += (frac(52.9829189f * frac(dot(floor(pixel), float2(0.06711056f, 0.00583715f)))) - 0.5f) / 255.f;
  }
  float3 color = renodx::color::gamma::DecodeSafe(encoded);
  float y = renodx::color::y::from::BT709(color);
  if (!(y > 0.18f)) return encoded;
  float t = saturate((y - 0.18f) / (1.f - 0.18f));
  float white = lerp(1.f, RYSE_PEAK, SI.video_highlights);
  return renodx::color::gamma::EncodeSafe(color * ((y + (white - 1.f) * t * t) / y));
}

#endif

#endif  // SRC_RYSESONOFROME_SHARED_H_
