// depth_of_field.hlsl pixel_dof_vertical_blur: the game's, its taps above 1 kept in HDR (dof_blur_ps.hlsli).
#include "../shared.h"
#include "../dof_blur_ps.hlsli"

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0)
{
  o0 = DofBlurVertical(v0, true);
}
