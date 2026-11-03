/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Below 100% (taa_dlss.hpp), after the game's TAA: its output takes this frame's colour as it came in (the TAA's clamp, its
// alpha kept: the history's copy reads it), so the tone mapper works on what the upscaler can resolve; depth and motion into
// the upscaler's inputs. All at the render size, top-left of the display-size textures. Motion as dlss_inputs.
Texture2D<float4> t_depth : register(t0);
Texture2D<float4> t_velocity : register(t1);
Texture2D<float4> t_color : register(t2);
RWTexture2D<float4> u_target : register(u0);
RWTexture2D<float> u_depth : register(u1);
RWTexture2D<float2> u_motion : register(u2);
cbuffer dlss_settings : register(b0) {
  float g_square_root;  // dlss_inputs'
  float g_motion_view;  // dlss_output's
  float2 g_render_size;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(float2(id.xy) >= g_render_size)) return;
  u_target[id.xy] = float4(min(max(t_color[id.xy].rgb, 0.f), 65504.f), u_target[id.xy].a);
  u_depth[id.xy] = t_depth[id.xy].x;
  u_motion[id.xy] = t_velocity[id.xy].xy * float2(-0.5f, 0.5f) * g_render_size;
}
