// Path tracing and reflections at the lighting pass (rt_path.hlsli) as a compute shader: GPUs and drivers without
// shader model 6.9 (no shader execution reordering, no opacity micromaps; 0x7A1E0D1C otherwise).
#include "./rt_path.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  PathTracePixel(id.xy);
}
