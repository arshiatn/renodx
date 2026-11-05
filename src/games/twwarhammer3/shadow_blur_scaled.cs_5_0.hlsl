// Shadow blur (DDB14D2C) with positions from the picture's uv, below 100% (bytes: the texture's uv as NDC).

cbuffer camera : register(b0)
{
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
  float real_time_in_sec : packoffset(c35.z);
  float update_time_in_sec : packoffset(c35.w);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float g_aspect_ratio : packoffset(c36.w);
  float4 g_screen_size : packoffset(c37);
  float g_vpos_texel_offset : packoffset(c38);
  float4 g_viewport_dimensions : packoffset(c39);
  float2 g_viewport_origin : packoffset(c40);
  float4 g_render_target_dimensions : packoffset(c41);
  float4 g_camera_temp0 : packoffset(c42);
  float4 g_camera_temp1 : packoffset(c43);
  float4 g_camera_temp2 : packoffset(c44);
  float4 g_clip_rect : packoffset(c45);
  float3 g_vr_head_rotation : packoffset(c46);
  int g_num_of_samples : packoffset(c46.w);
  float g_supersampling : packoffset(c47);
  float4 g_mouse_position : packoffset(c48);
  float3 g_frustum_points[8] : packoffset(c49);
  float4 g_frustum_planes[6] : packoffset(c57);
  float g_orthographic : packoffset(c63);
  float g_overlay_lerp : packoffset(c63.y);
  float g_overlay_parchment_lerp : packoffset(c63.z);
  float g_overlay_show_details : packoffset(c63.w);
  float g_amount_shadow_in_far_distance : packoffset(c64);
  float2 g_camera_jitter : packoffset(c64.y);
  float2 g_prev_camera_jitter : packoffset(c65);
  uint g_debug_visualization_flags : packoffset(c65.z);
  bool g_taa_is_enabled : packoffset(c65.w);
}

cbuffer sss : register(b1)
{
  float sss_thickness_threshold_base : packoffset(c0);
  float sss_thickness_threshold_start : packoffset(c0.y);
  float sss_thickness_threshold_scale : packoffset(c0.z);
  int sss_blur_amount : packoffset(c0.w);
  float sss_blur_distance_cutoff : packoffset(c1);
  int sss_blur_step : packoffset(c1.y);
  float terrain_shadow_bias : packoffset(c1.z);
  bool sss_screen_space_shadows_enabled : packoffset(c1.w);
  bool sss_direction_light_shadows_enabled : packoffset(c2);
  bool sss_terrain_shadows_enabled : packoffset(c2.y);
}

Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<unorm float> t_unblurred_shadow : register(t1);
RWTexture2D<unorm float> t_rw_shadow : register(u0);


// 3Dmigoto declarations
#define cmp -


[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;
  t_unblurred_shadow.GetDimensions(0, fDest.x, fDest.y, fDest.z);
  r0.xy = fDest.xy;
  r0.xy = (int2)r0.xy;
  r0.zw = cmp((int2)vThreadID.xy >= (int2)r0.xy);
  r0.z = (int)r0.w | (int)r0.z;
  if (r0.z != 0) {
    return;
  }
  r0.zw = (uint2)vThreadID.xy;
  r0.zw = float2(0.5,0.5) + r0.zw;
  r0.zw = g_screen_size.zw * r0.zw;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (uint2)r1.xy;
  r2.xy = r1.zw * r0.zw;
  r2.xy = floor(r2.xy);
  r2.xy = (int2)r2.xy;
  r1.xy = (int2)r1.xy + int2(-1,-1);
  r2.xy = max(int2(0,0), (int2)r2.xy);
  r2.xy = min((int2)r2.xy, (int2)r1.xy);
  r2.zw = float2(0,0);
  r2.z = gbuffer_channel_4_texture.Load(r2.xyz).x;
  r2.xy = r0.zw / (g_viewport_dimensions.xy * g_screen_size.zw) * float2(2,-2) + float2(-1,1);
  r2.w = 1;
  r3.x = dot(r2.xyzw, inv_view_projection._m00_m10_m20_m30);
  r3.y = dot(r2.xyzw, inv_view_projection._m01_m11_m21_m31);
  r3.z = dot(r2.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.z = dot(r2.xyzw, inv_view_projection._m03_m13_m23_m33);
  r2.xyz = r3.xyz / r0.zzz;
  r0.zw = (int2)vThreadID.yx + -sss_blur_amount;
  r3.xy = (int2)vThreadID.yx + sss_blur_amount;
  r4.zw = float2(0,0);
  r5.w = 1;
  r6.zw = float2(0,0);
  r3.zw = float2(0,0);
  r2.w = r0.z;
  while (true) {
    r7.x = cmp((int)r2.w >= (int)r3.x);
    if (r7.x != 0) break;
    r7.xy = r3.zw;
    r6.y = r2.w;
    r6.x = r0.w;
    while (true) {
      r7.z = cmp((int)r6.x >= (int)r3.y);
      if (r7.z != 0) break;
      r7.zw = cmp((int2)r6.xy < int2(0,0));
      r7.z = (int)r7.w | (int)r7.z;
      r8.xy = cmp((int2)r6.xy >= (int2)r0.xy);
      r7.z = (int)r7.z | (int)r8.x;
      r7.z = (int)r8.y | (int)r7.z;
      if (r7.z != 0) {
        r8.y = (int)r6.x + sss_blur_step;
        r8.x = r6.y;
        r6.xy = r8.yx;
        continue;
      }
      r7.zw = (uint2)r6.xy;
      r7.zw = float2(0.5,0.5) + r7.zw;
      r7.zw = g_screen_size.zw * r7.zw;
      r8.xy = r7.zw * r1.zw;
      r8.xy = floor(r8.xy);
      r8.xy = (int2)r8.xy;
      r8.xy = max(int2(0,0), (int2)r8.xy);
      r4.xy = min((int2)r8.xy, (int2)r1.xy);
      r5.z = gbuffer_channel_4_texture.Load(r4.xyz).x;
      r5.xy = r7.zw / (g_viewport_dimensions.xy * g_screen_size.zw) * float2(2,-2) + float2(-1,1);
      r8.x = dot(r5.xyzw, inv_view_projection._m00_m10_m20_m30);
      r8.y = dot(r5.xyzw, inv_view_projection._m01_m11_m21_m31);
      r8.z = dot(r5.xyzw, inv_view_projection._m02_m12_m22_m32);
      r4.x = dot(r5.xyzw, inv_view_projection._m03_m13_m23_m33);
      r5.xyz = r8.xyz / r4.xxx;
      r5.xyz = -r5.xyz + r2.xyz;
      r4.x = dot(r5.xyz, r5.xyz);
      r4.x = sqrt(r4.x);
      r4.x = cmp(sss_blur_distance_cutoff < r4.x);
      if (r4.x != 0) {
        r4.y = (int)r6.x + sss_blur_step;
        r4.x = r6.y;
        r6.xy = r4.yx;
        continue;
      }
      r7.x = (int)r7.x + 1;
      r4.x = t_unblurred_shadow.Load(r6.xyz).x;
      r7.y = r7.y + r4.x;
      r6.x = (int)r6.x + sss_blur_step;
    }
    r3.zw = r7.xy;
    r2.w = (int)r2.w + sss_blur_step;
  }
  r0.x = cmp(0 < (int)r3.z);
  if (r0.x != 0) {
    r0.x = (int)r3.z;
    r0.x = r3.w / r0.x;
    t_rw_shadow[vThreadID.xy] = r0.x;
  } else {
    t_rw_shadow[vThreadID.xy] = 1.f;
  }
  return;
}