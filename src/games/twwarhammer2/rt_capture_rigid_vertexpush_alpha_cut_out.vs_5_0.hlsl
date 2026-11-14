// rt_capture_rigid_vertexpush_alpha with the UV: its draws whose G-buffer pixel shader clips (CUT_OUT_MASKS in addon.cpp:
// the diffuse map's alpha at TEXCOORD1 below 0.5; rt_core.hpp SceneShader::clip_cut_outs).
#define RT_CAPTURE_UV
#include "./rt_capture_rigid_vertexpush_alpha.vs_5_0.hlsl"
