// The resident's moments (recorder builds only; rt_d3d12.hpp StartMoments, AddMoments): an output's texel added to its fp32
// Welford planes, frame `count` (1: the first). With s the first frame, `mean` is the mean about s (its sums stay small),
// `lag` sums (x_t - s)(x_t-1 - s) from the second frame on (rt_serve.h's reader takes the lag-1 covariance from it),
// `travel` sums |x_t - x_t-1|.
Texture2D<float4> source : register(t0);
RWStructuredBuffer<float4> mean : register(u0);
RWStructuredBuffer<float4> m2 : register(u1);
RWStructuredBuffer<float4> first : register(u2);
RWStructuredBuffer<float4> lag : register(u3);
RWStructuredBuffer<float4> previous : register(u4);
RWStructuredBuffer<float4> travel : register(u5);

cbuffer Step : register(b0) {
  uint count;
  uint width;
  uint height;
  uint unused;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const uint at = id.y * width + id.x;
  const float4 x = source[id.xy];
  if (count == 1u) {
    mean[at] = 0.0;
    m2[at] = 0.0;
    first[at] = x;
    lag[at] = 0.0;
    previous[at] = x;
    travel[at] = 0.0;
    return;
  }
  const float4 shift = first[at];
  const float4 last = previous[at];
  const float4 centred = x - shift;
  const float4 delta = centred - mean[at];
  const float4 updated = mean[at] + delta / float(count);
  mean[at] = updated;
  m2[at] += delta * (centred - updated);
  lag[at] += centred * (last - shift);
  travel[at] += abs(x - last);
  previous[at] = x;
}
