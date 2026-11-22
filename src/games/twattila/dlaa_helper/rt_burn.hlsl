// The resident's burn (recorder builds only; rt_d3d12.hpp SetBurn): a fixed chain of work in a pass's span, made with the
// scene's root signature. xorshift never reaches 0 from a state that is not 0, so nothing is written, but the compiler must
// keep the chain.
RWTexture2D<unorm float> visibility : register(u0);

static const uint BURN_STEPS = 256;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint state = id.x + 1u;
  [loop]
  for (uint step = 0; step < BURN_STEPS; ++step) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
  }
  if (state == 0u) visibility[uint2(0, 0)] = 0.0;
}
