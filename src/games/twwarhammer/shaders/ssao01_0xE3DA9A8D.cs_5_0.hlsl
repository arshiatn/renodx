// ssao.hlsl cs50_blur_x, the game's code (line_sweep_blur.hlsli); with the Visibility Bitmask the addon dispatches its
// own shader instead (ao_bitmask_blur), so this replacement stays only for that hook.
#define LINE_SWEEP_HALF_SIZE_AO 1
#include "../shared.h"
#include "../../twwarhammer2/line_sweep_blur.hlsli"
