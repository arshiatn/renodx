// PHARAOH autoexposure00 - luma accumulation for auto exposure (compute).
//
// Samples a 512x512 grid, takes BT.709 luma, quantises to 14 bits and atomically sums
// it. 0x242CA153 clears the buffer just before this runs. Pharaoh meters on the SUM
// only - there is no measured min or max anywhere - so a bright object hurts by
// inflating the average, not by moving a white point.
//
// THE FIX: cap how much a single bright sample can contribute. AutoExposureHighlight-
// Protection 0 is stock. The cap is relative to g_avg_luma_scene, the previous frame's
// average, which the centring below already uses as its reference.
//
// RECONSTRUCTED FROM THE ASSEMBLY. 3Dmigoto emits "void main)" and leaves the UAV,
// TGSM, thread group, store_raw, ld_raw and both atomic_iadd instructions as comments.
// From the bytecode: thread group 8x8x1, dcl_tgsm_raw g0 = 12 bytes (3 uints), byte
// offset 8 = index 2, sync flags = threads + tgsm, dcl_temps 1.

cbuffer camera : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
  float4 g_viewport_dimensions : packoffset(c29);
  float2 g_viewport_origin : packoffset(c30);
  float4 g_render_target_dimensions : packoffset(c31);
  float4 g_camera_temp0 : packoffset(c32);
  float4 g_camera_temp1 : packoffset(c33);
  float4 g_camera_temp2 : packoffset(c34);
  float4 g_clip_rect : packoffset(c35);
  float3 g_vr_head_rotation : packoffset(c36);
  int g_num_of_samples : packoffset(c36.w);
  float g_supersampling : packoffset(c37);
  float4 g_mouse_position : packoffset(c38);
  float3 g_frustum_points[8] : packoffset(c39);
  float g_orthographic : packoffset(c46.w);
  float g_overlay_lerp : packoffset(c47);
  float g_overlay_text_lerp : packoffset(c47.y);
  float g_overlay_parchment_lerp : packoffset(c47.z);
  float g_overlay_palette_alpha : packoffset(c47.w);
  float4 g_overlay_outline_colour : packoffset(c48);
  float g_overlay_outline_border_width : packoffset(c49);
  float g_ui_overlay_brightness : packoffset(c49.y);
  float g_debug_tonemapping : packoffset(c49.z);
  float4 g_blood_remap : packoffset(c50);
  int g_editor_mode : packoffset(c51);
  float4 g_spec_gloss_tweaker : packoffset(c52);
}

cbuffer luma_stats_buffer : register(b1)
{
  float g_min_luma_black_point : packoffset(c0);
  float g_min_luma_white_point : packoffset(c0.y);
  float g_max_luma_black_point : packoffset(c0.z);
  float g_max_luma_white_point : packoffset(c0.w);
  float g_avg_luma_scene : packoffset(c1);
}

SamplerState s_bilinear_s : register(s0);
Texture2D<float4> t_input : register(t0);
RWByteAddressBuffer t_rw_luma_stats : register(u0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// dcl_tgsm_raw g0, 12 - three uints. Only index 2 (byte offset 8) is ever used; the
// other two match the layout 0x242CA153 clears.
groupshared uint g_luma_stats[3];


[numthreads(8, 8, 1)]
void main(
  uint3 vThreadID : SV_DispatchThreadID,
  uint3 vThreadIDInGroup : SV_GroupThreadID)
{
  float4 r0;

  // store_raw g0.x, l(8), l(0). Stock has NO barrier between this and the atomic_iadd
  // below - the only sync is after it. That is a latent race in the original; kept as
  // stock, because adding a barrier here would change metering.
  g_luma_stats[2] = 0;

  r0.xy = (float2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r0.xy = float2(0.001953125,0.001953125) * r0.xy;     // 1/512 sample grid
  r0.zw = g_viewport_dimensions.xy * g_render_target_dimensions.zw;
  r0.xy = r0.zw * r0.xy;
  r0.xyz = t_input.SampleLevel(s_bilinear_s, r0.xy, 0).xyz;
  r0.x = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));

  // ---------------------------------------------------------------------------
  // ADDED: highlight protection. Limits a single bright sample's contribution to
  // the metered sum, so fire or a sun disc cannot drag the whole frame dark.
  // saturate/clamp mean a garbage setting value can never destabilise metering.
  // ---------------------------------------------------------------------------
  if (AutoExposureHighlightProtection > 0.f && g_avg_luma_scene > 0.f)
  {
    float protection = saturate(AutoExposureHighlightProtection);
    float headroom_stops = clamp(AutoExposureHighlightHeadroomStops, 0.f, 16.f);
    float capped = min(r0.x, g_avg_luma_scene * exp2(headroom_stops));
    r0.x = lerp(r0.x, capped, protection);
  }

  // Stock: once a previous average exists, accumulate the deviation centred on 0.5.
  r0.y = cmp(0 < g_avg_luma_scene);
  r0.z = -g_avg_luma_scene + r0.x;
  r0.z = 0.5 + r0.z;
  r0.x = r0.y ? r0.z : r0.x;

  r0.x = max(g_min_luma_black_point, r0.x);
  r0.x = min(g_max_luma_white_point, r0.x);
  r0.x = -g_min_luma_black_point + r0.x;
  r0.y = g_max_luma_white_point + -g_min_luma_black_point;
  r0.x = r0.x / r0.y;

  // ftou / utof / ftou: quantise to 14 bits. 2^18 samples x 2^14 fills a uint32.
  r0.x = 4.2949673e+009 * r0.x;
  uint quantised = (uint)r0.x;
  r0.x = (float)quantised;
  r0.x = 3.81469727e-006 * r0.x;
  quantised = (uint)r0.x;

  uint previous;
  InterlockedAdd(g_luma_stats[2], quantised, previous);   // atomic_iadd g0, l(8)

  GroupMemoryBarrierWithGroupSync();                      // sync_t_g

  if (((int)vThreadIDInGroup.y | (int)vThreadIDInGroup.x) == 0)
  {
    uint group_total = g_luma_stats[2];                   // ld_raw r0.x, l(8), g0
    t_rw_luma_stats.InterlockedAdd(8, group_total);       // atomic_iadd u0, l(8)
  }
  return;
}
