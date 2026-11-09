#include "../../src/games/twattila/motion.hlsli"
cbuffer test_input : register(b0) { float4 sample; }
RWBuffer<uint> keys : register(u0);
RWBuffer<uint4> placements : register(u1);
[numthreads(1,1,1)]
void main() {
  float4 rows[3] = {float4(1,0,0,sample.x),float4(0,1,0,0),float4(0,0,1,sample.y)};
  MotionRecord(keys, placements, 1u, sample.xy, rows, uint4(7,0,0,0), sample.z);
}
