// Expeditions: Rome tonemapper: bloom, sharpen, film grain + jitter, chromatic aberration. From 0x2501C25F.ps_5_0.cso.
#define EXR_GRAIN_JITTER 1
#define EXR_COLOR_FRINGE 1
#define EXR_SHARPEN 1
#define EXR_VIGNETTE 0
#define EXR_GRAIN_INTENSITY 1
#include "../tonemap.hlsli"
