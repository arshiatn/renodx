// depth_of_field_cs.hlsl cs_dof_horizontal_blur: the game's, its taps above 1 kept in HDR (twattila/dof_blur_cs.hlsli).
#include "../shared.h"
#include "../../twattila/dof_blur_cs.hlsli"

[numthreads(32, 2, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
  DofBlurCs(group, thread, false);
}
