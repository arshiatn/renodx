// filter_blur (DB5D3C54) for the march at one to one: clamps at the viewport, not half of it.
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

Texture2D<float4> t_in_convolution : register(t0);
RWTexture2D<float4> t_out_convolution : register(u0);


// 3Dmigoto declarations
#define cmp -


[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.zw = float2(0,0);
  r1.xyzw = g_viewport_dimensions.xyxy;
  r1.xyzw = (int4)r1.xyzw;
  r1.xyzw = (int4)r1.xyzw + int4(-1,-1,-1,-1);
  r2.xyzw = (int4)vThreadID.yyxx + int4(-2,-1,0,0);
  r2.xy = max(int2(0,0), (int2)r2.xy);
  r0.xy = min((int2)r2.zx, (int2)r1.zw);
  r2.xy = min((int2)r2.wy, (int2)r1.zw);
  r0.xy = t_in_convolution.Load(r0.xyz).xy;
  r0.z = 0.0613600016 + r0.x;
  r0.xy = r0.xy * r0.zz;
  r3.xy = vThreadID.xy;
  r3.zw = float2(0,0);
  r3.xy = t_in_convolution.Load(r3.xyz).xy;
  r0.w = 0.387739986 + r3.x;
  r0.xy = r3.xy * r0.ww + r0.xy;
  r0.z = r0.w + r0.z;
  r2.zw = float2(0,0);
  r2.xy = t_in_convolution.Load(r2.xyz).xy;
  r0.w = 0.244770005 + r2.x;
  r0.xy = r2.xy * r0.ww + r0.xy;
  r0.z = r0.z + r0.w;
  r2.xyzw = (int4)vThreadID.xyxy + int4(0,1,0,2);
  r1.xyzw = min((int4)r2.zwxy, (int4)r1.zwxy);
  r2.xy = r1.zw;
  r2.zw = float2(0,0);
  r2.xy = t_in_convolution.Load(r2.xyz).xy;
  r0.w = 0.244770005 + r2.x;
  r0.xy = r2.xy * r0.ww + r0.xy;
  r0.z = r0.z + r0.w;
  r1.zw = float2(0,0);
  r1.xy = t_in_convolution.Load(r1.xyz).xy;
  r0.w = 0.0613600016 + r1.x;
  r0.xy = r1.xy * r0.ww + r0.xy;
  r0.z = r0.z + r0.w;
  r0.xy = r0.xy / r0.zz;
  r1.x = r3.y + -r0.y;
  r0.z = r3.x * r1.x + r0.y;
  r0.w = 0;
  t_out_convolution[vThreadID.xy] = r0.xzww;
  return;
}