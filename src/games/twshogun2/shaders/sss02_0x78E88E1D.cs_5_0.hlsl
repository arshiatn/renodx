// screen_space_shadows.hlsl cs_passthrough (enable_shadows, shadows_hardware_gather_5_0): the shadow cascades per
// pixel with the screen-space shadows off. sss.hlsli.
#define SSS_SHADOW_CASCADES
#define SSS_PASSTHROUGH
#include "./sss.hlsli"
