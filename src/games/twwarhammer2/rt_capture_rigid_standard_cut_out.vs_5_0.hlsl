// rt_capture_rigid_standard with the UV (Troy's and Pharaoh's rt_capture_rigid_standard_cut_out): 16AFAF14's alpha-tested
// materials (CUT_OUT_MASKS in addon.cpp: their G-buffer pixel shaders test the diffuse map's alpha at TEXCOORD1, the vertex's
// TEXCOORD0, against 0.5; rt_core.hpp SceneShader::clip_cut_outs).
#define RT_CAPTURE_UV
#include "./rt_capture_rigid_standard.vs_5_0.hlsl"
