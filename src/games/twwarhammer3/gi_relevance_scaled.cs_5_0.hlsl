// GI voxels' relevance (1810C89D) with the depth read where the picture has it, below 100% (bytes: uv x the whole texture).

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

cbuffer global_illumination : register(b1)
{
  float g_scene_voxel_size[4] : packoffset(c0);
  uint g_debug_gi : packoffset(c3.y);
  float g_scene_moving_average : packoffset(c3.z);
  float3 g_scene_world_offsets[4] : packoffset(c4);
  int g_invalidate_direction : packoffset(c7.w);
  uint g_cascade_idx : packoffset(c8);
  uint g_irradiance_negative_offset : packoffset(c8.y);
  float g_irradiance_moving_average : packoffset(c8.z);
  uint g_number_of_rays : packoffset(c8.w);
  float4x4 g_unit_sphere_jitter_rotation : packoffset(c9);
  float g_irradiance_power : packoffset(c13);
  uint g_voxelization_screen_range : packoffset(c13.y);
  uint g_voxelization_screen_offset : packoffset(c13.z);
}

Texture2D<float4> gbuffer_channel_4_texture : register(t0);
RWTexture3D<float4> t_rw_voxelized_scene_texture : register(u0);
RWTexture3D<uint> t_rw_voxelized_scene_relevance_texture : register(u1);


// 3Dmigoto declarations
#define cmp -


[numthreads(4, 4, 4)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;
  r0.xyz = (uint3)vThreadID.xyz;
  r0.xyz = float3(-64,-32,-64) + r0.xyz;
  r0.w = g_cascade_idx;
  r1.xyz = frac(g_scene_world_offsets[r0.w].xyz);
  r1.xyz = -r1.xyz + r0.xyz;
  r1.xyz = float3(0.5,0.5,0.5) + r1.xyz;
  r1.xyz = r1.xyz * g_scene_voxel_size[r0.w] + camera_position.xyz;
  r1.w = 1;
  r2.x = dot(r1.xyzw, view_projection._m00_m10_m20_m30);
  r2.y = dot(r1.xyzw, view_projection._m01_m11_m21_m31);
  r2.z = dot(r1.xyzw, view_projection._m02_m12_m22_m32);
  r1.x = dot(r1.xyzw, view_projection._m03_m13_m23_m33);
  r1.xyz = r2.xyz / r1.xxx;
  r2.xy = cmp(r1.xy >= g_clip_rect.xy);
  r3.xy = cmp(g_clip_rect.zw >= r1.xy);
  r1.w = r2.x ? r3.x : 0;
  r1.w = r2.y ? r1.w : 0;
  r1.w = r3.y ? r1.w : 0;
  r2.x = cmp(0 < r2.z);
  r1.w = r2.x ? r1.w : 0;
  if (r1.w != 0) {
    r1.xy = r1.xy * float2(0.5,-0.5) + float2(0.5,0.5);
    gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r2.xy = uiDest.xy;
    r2.zw = (uint2)r2.xy;
    r1.xy = r2.zw * r1.xy * (g_viewport_dimensions.xy * g_screen_size.zw);  // the picture fills only this share of the depth
    r1.xy = floor(r1.xy);
    r1.xy = (int2)r1.xy;
    r2.xy = (int2)r2.xy + int2(-1,-1);
    r1.xy = max(int2(0,0), (int2)r1.xy);
    r2.xy = min((int2)r1.xy, (int2)r2.xy);
    r2.zw = float2(0,0);
    r1.x = gbuffer_channel_4_texture.Load(r2.xyz).x;
    r1.x = cmp(r1.z < r1.x);
    if (r1.x != 0) {
      r0.xyz = g_scene_world_offsets[r0.w].xyz + r0.xyz;
      r1.xyz = float3(0.0078125,0.015625,0.0078125) * r0.xyz;
      r1.xyz = floor(r1.xyz);
      r0.xyz = -r1.xyz * float3(128,64,128) + r0.xyz;
      r0.xyz = (uint3)r0.xyz;
      r1.x = g_cascade_idx << 7;
      r0.w = (int)r0.z + (int)r1.x;
      r0.z = t_rw_voxelized_scene_relevance_texture[(uint3)r0.xyw].x;
      if (r0.z != 0) {
        r0.z = (int)r0.z + -1;
      } else {
      t_rw_voxelized_scene_texture[(uint3)r0.xyw] = float4(0,0,0,0);
        r0.z = 0;
      }
      t_rw_voxelized_scene_relevance_texture[(uint3)r0.xyw] = (uint)r0.z;
    }
  }
  return;
}