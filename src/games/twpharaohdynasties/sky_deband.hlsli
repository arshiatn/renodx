#ifndef SRC_GAMES_TWPHARAOHDYNASTIES_SKY_DEBAND_HLSLI_
#define SRC_GAMES_TWPHARAOHDYNASTIES_SKY_DEBAND_HLSLI_

// Sky Debanding for the battle skydome materials (Pharaoh's 0x9CF980D0, Troy's 0x934FE258): their sky texture
// (t_diffuse, s_diffuse_s; declared by the includer, with SI) filtered and dithered per SI.sky_deband (0: stock).
// Sample three rings at 16, 32, and 64 base-level texels.
// Higher thresholds admit larger color differences when smoothing the source.
// Enabled filtering uses 25 texture samples including the original center.
// Dither is 4.0 sRGB code values peak-to-peak (approximately +/-2 of an 8-bit
// sRGB code value) for all three enabled presets.
// Noise is static in screen space and applied before the game's HDR scaling.

static const float SKY_DEBAND_RADIUS_TEXELS = 64.0;
static const float SKY_DEBAND_THRESHOLD_LOW_CODES = 6.0;
static const float SKY_DEBAND_THRESHOLD_MEDIUM_CODES = 11.0;
static const float SKY_DEBAND_THRESHOLD_HIGH_CODES = 20.0;
static const float SKY_DITHER_CODES = 4.0;

// The SRGB resource view already decodes t0 RGB to linear on sampling.
// Re-encoding here is ONLY for perceptual comparisons and noise amplitude;
// the filter averages in linear space and returns linear RGB to the game.
float3 SkyEncodeSRGB(float3 c)
{
  c = max(c, 0.0);
  float3 lo = 12.92 * c;
  float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
  return lerp(lo, hi, step(0.0031308, c));
}

float3 SkyDecodeSRGB(float3 c)
{
  c = max(c, 0.0);
  float3 lo = c / 12.92;
  float3 hi = pow((c + 0.055) / 1.055, 2.4);
  return lerp(lo, hi, step(0.04045, c));
}

float3 SkyDebandRGB(float2 uv, float3 center, float2 dx, float2 dy,
                   float thresholdCodes)
{
  uint width, height;
  t_diffuse.GetDimensions(width, height);
  float2 radiusUV = SKY_DEBAND_RADIUS_TEXELS / float2(width, height);

  // Use UV derivatives computed before the runtime branch for every tap.
  // Keep the original bound sampler and texture LOD behavior.
  float3 centerCode = SkyEncodeSRGB(center);
  float threshold = max(thresholdCodes / 255.0, 1e-6);

  // Eight symmetric directions at each of three radii. Nearby taps help
  // smooth narrow bands; wider taps can reach across broader plateaus.
  // Every tap is evaluated against the original center color to avoid
  // progressively admitting unrelated colors while accumulating samples.
  const float2 directions[8] = {
    float2( 1.0,  0.0), float2(-1.0,  0.0),
    float2( 0.0,  1.0), float2( 0.0, -1.0),
    float2( 0.70710678,  0.70710678),
    float2(-0.70710678, -0.70710678),
    float2( 0.70710678, -0.70710678),
    float2(-0.70710678,  0.70710678)
  };
  const float ringScales[3] = { 0.25, 0.5, 1.0 };

  float3 sum = center;
  float totalWeight = 1.0;
  [unroll]
  for (int ring = 0; ring < 3; ++ring)
  {
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
      float3 sampleRGB = t_diffuse.SampleGrad(
          s_diffuse_s,
          uv + directions[i] * (radiusUV * ringScales[ring]), dx, dy).rgb;
      float3 difference = abs(SkyEncodeSRGB(sampleRGB) - centerCode);
      float error = max(difference.r, max(difference.g, difference.b));
      // One weight for all channels avoids separate-channel edge decisions.
      // Full weight below half the threshold; reject above the full threshold.
      float weight = 1.0 - smoothstep(0.5 * threshold, threshold, error);
      sum += sampleRGB * weight;
      totalWeight += weight;
    }
  }
  return sum / totalWeight;
}

float3 SkyDitherRGB(float3 linearRGB, float2 pixel)
{
  // Interleaved gradient noise, fixed to screen pixels (no time uniform).
  float noise = frac(52.9829189 * frac(dot(
      floor(pixel), float2(0.06711056, 0.00583715)))) - 0.5;
  float3 code = SkyEncodeSRGB(linearRGB);
  code += noise * (SKY_DITHER_CODES / 255.0);
  // This clamp acts only on the UNORM source before any HDR scaling.
  // Never apply this clamp to the graded HDR output.
  return SkyDecodeSRGB(saturate(code));
}

// The sky `color` sampled at `uv` (derivatives `dx`, `dy` from before the branch), debanded and dithered at `pixel` per
// SI.sky_deband (0: as it is).
float3 SkyDeband(float3 color, float2 uv, float2 dx, float2 dy, float2 pixel)
{
  [branch]
  if (SI.sky_deband > 0.0)
  {
    float thresholdCodes = (SI.sky_deband >= 3.0)
        ? SKY_DEBAND_THRESHOLD_HIGH_CODES
        : ((SI.sky_deband >= 2.0)
            ? SKY_DEBAND_THRESHOLD_MEDIUM_CODES
            : SKY_DEBAND_THRESHOLD_LOW_CODES);
    color = SkyDebandRGB(uv, color, dx, dy, thresholdCodes);
    color = SkyDitherRGB(color, pixel);
  }
  return color;
}

#endif  // SRC_GAMES_TWPHARAOHDYNASTIES_SKY_DEBAND_HLSLI_
