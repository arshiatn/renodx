// Ray tracing's capture versions (rt_core.hpp): an alpha-tested vertex for the stream output, its world position and in w
// its UV as two halves.

// A float as a half's bits (truncated; tiny values 0, huge ones infinite).
uint RtHalfBits(float value)
{
  const uint bits = asuint(value);
  const uint sign = (bits >> 16) & 0x8000;
  const int exponent = int((bits >> 23) & 0xFF) - 112;
  if (exponent <= 0) return sign;
  if (exponent >= 31) return sign | 0x7C00;
  return sign | (uint(exponent) << 10) | ((bits >> 13) & 0x3FF);
}

float4 RtWorldUv(float3 world, float2 uv)
{
  return float4(world, asfloat(RtHalfBits(uv.x) | (RtHalfBits(uv.y) << 16)));
}
