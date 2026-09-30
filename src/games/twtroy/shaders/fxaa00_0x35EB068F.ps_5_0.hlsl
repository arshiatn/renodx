// ---- Created with 3Dmigoto v1.3.16 on Sun Sep 20 01:31:59 2026
//
// TROY - FXAA (3.11 quality path, green-as-luma), runs after t02
//
// ===========================================================================
// THIS SHADER CANNOT CLIP BRIGHTNESS - THE CLIP IS ELSEWHERE
// ===========================================================================
// Both of its output paths are raw texture reads:
//
//     o0.xyzw = t_initial_texture.SampleLevel(s_default_s, r3.xy, 0).xyzw;   // edge
//     o0.xyzw = r1.xyzw;                                                     // no edge
//
// There is no arithmetic on the result at all - FXAA only decides WHERE to sample,
// never what value to emit. The single saturate() in the file is on the subpixel
// blend FACTOR, a ratio, not on colour. A bilinear read of HDR texels stays HDR.
//
// So if brightness is being clipped around this pass, it is a RESOURCE FORMAT
// problem, not a shader one, and the fix belongs in the addon:
//
//   - t_initial_texture: the copy of t02's output that FXAA reads. If that
//     intermediate is still R8G8B8A8_UNORM, everything above 1.0 is already gone
//     before this shader runs, and nothing written here can bring it back.
//   - FXAA's own render target, for the same reason on the way out.
//
// Check both in the RenoDX devkit. The addon currently upgrades
// r8g8b8a8_unorm -> r16g16b16a16_float with aspect_ratio = BACK_BUFFER and
// ignore_size = false, so an intermediate that is not exactly back-buffer sized
// (a half-res or padded FXAA buffer, for instance) would be skipped and would clip.
//
// ===========================================================================
// DECOMPILER BUG - Gather() gathers RED, this needs GREEN
// ===========================================================================
// 3Dmigoto emitted plain Gather(), which returns the RED component of the four
// texels. That cannot be what the shader does, and the code proves it:
//
//     r0.z = max(r2.x, r1.y);
//
// r2.x comes from a gather, r1.y is the centre texel's GREEN. Taking max() of a red
// sample and a green sample is meaningless - FXAA is building lumaMax from a
// neighbourhood, so both operands must be the same channel. Every other luma read in
// the file is .y as well, which is FXAA 3.11 with FXAA_GREEN_AS_LUMA. The gathers
// are therefore GatherGreen, and 3Dmigoto dropped the channel selector.
//
// Left as Gather(), FXAA would detect edges from the red channel instead of green -
// it would still run and still look plausible, just with wrong edge decisions.
//
// ===========================================================================
// HDR: EDGE THRESHOLD NORMALISATION
// ===========================================================================
// FXAA's edge test is the one place that assumes luma lives in 0..1:
//
//     threshold = max(0.0833, 0.166 * lumaMax)
//     if (lumaRange >= threshold) -> antialias
//
// After t02 the frame is gamma-encoded and, in HDR, carries values well above 1.0
// (up to ~3.1 encoded at HDR_PEAK 12.3). lumaMax then scales the threshold with it:
// at lumaMax 3.1 the bar rises from 0.166 to 0.515, so FXAA quietly stops
// antialiasing bright edges - sky against rooftops, sunlit armour, exactly where
// aliasing is most visible.
//
// Clamping only the THRESHOLD term restores the designed 0.0833..0.166 window. The
// edge detection itself still runs on the true values. This is a no-op in SDR,
// where lumaMax never exceeds 1.0, so it needs no HDR branch.
//
// Everything else in FXAA is already scale-invariant: the subpixel blend is
// |average - centre| / lumaRange, and the gradient test is 0.25 * localGradient.
// Both are ratios and were left alone.

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
  float g_overlay_parchment_lerp : packoffset(c47.y);
  float g_overlay_palette_alpha : packoffset(c47.z);
  float g_debug_tonemapping : packoffset(c47.w);
  float4 g_blood_remap : packoffset(c48);
}

SamplerState s_default_s : register(s0);
Texture2D<float4> t_initial_texture : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_screen_size.zw * v0.xy;
  r1.xyzw = t_initial_texture.SampleLevel(s_default_s, r0.xy, 0).xyzw;

  // GatherGreen, not Gather - see the header. Plain Gather() would return red and
  // the max/min below would be mixing channels.
  r2.xyz = t_initial_texture.GatherGreen(s_default_s, r0.xy).xyz;
  r3.xyz = t_initial_texture.GatherGreen(s_default_s, r0.xy, int2(-1, -1)).xzw;

  r0.z = max(r2.x, r1.y);
  r0.w = min(r2.x, r1.y);
  r0.z = max(r2.z, r0.z);
  r0.w = min(r2.z, r0.w);
  r2.w = max(r3.y, r3.x);
  r3.w = min(r3.y, r3.x);
  r0.z = max(r2.w, r0.z);
  r0.w = min(r3.w, r0.w);

  // Edge threshold. min(lumaMax, 1.0) keeps this in FXAA's designed
  // 0.0833..0.166 window once the frame carries HDR values; no-op in SDR.
  r2.w = 0.165999994 * min(r0.z, 1.f);

  r0.z = r0.z + -r0.w;
  r0.w = max(0.0833000019, r2.w);
  r0.w = cmp(r0.z >= r0.w);
  if (r0.w != 0) {
    r0.w = t_initial_texture.SampleLevel(s_default_s, r0.xy, 0, int2(1, -1)).y;
    r2.w = t_initial_texture.SampleLevel(s_default_s, r0.xy, 0, int2(-1, 1)).y;
    r4.xy = r3.yx + r2.xz;
    r0.z = 1 / r0.z;
    r3.w = r4.x + r4.y;
    r4.xy = r1.yy * float2(-2,-2) + r4.xy;
    r4.z = r0.w + r2.y;
    r0.w = r3.z + r0.w;
    r4.w = r2.z * -2 + r4.z;
    r0.w = r3.y * -2 + r0.w;
    r3.z = r3.z + r2.w;
    r2.y = r2.w + r2.y;
    r2.w = abs(r4.x) * 2 + abs(r4.w);
    r0.w = abs(r4.y) * 2 + abs(r0.w);
    r4.x = r3.x * -2 + r3.z;
    r2.y = r2.x * -2 + r2.y;
    r2.w = abs(r4.x) + r2.w;
    r0.w = abs(r2.y) + r0.w;
    r2.y = r3.z + r4.z;
    r0.w = cmp(r2.w >= r0.w);
    r2.y = r3.w * 2 + r2.y;
    r2.w = r0.w ? r3.y : r3.x;
    r2.x = r0.w ? r2.x : r2.z;
    r2.z = r0.w ? g_screen_size.w : g_screen_size.z;
    r2.y = r2.y * 0.0833333358 + -r1.y;
    r3.xy = r2.wx + -r1.yy;
    r2.xw = r2.xw + r1.yy;
    r3.z = cmp(abs(r3.x) >= abs(r3.y));
    r3.x = max(abs(r3.x), abs(r3.y));
    r2.z = r3.z ? -r2.z : r2.z;
    r0.z = saturate(abs(r2.y) * r0.z);
    r2.y = r0.w ? g_screen_size.z : 0;
    r3.y = r0.w ? 0 : g_screen_size.w;
    r4.xy = r2.zz * float2(0.5,0.5) + r0.xy;
    r3.w = r0.w ? r0.x : r4.x;
    r4.x = r0.w ? r4.y : r0.y;
    r5.x = r3.w + -r2.y;
    r5.y = r4.x + -r3.y;
    r6.x = r3.w + r2.y;
    r6.y = r4.x + r3.y;
    r3.w = r0.z * -2 + 3;
    r4.x = t_initial_texture.SampleLevel(s_default_s, r5.xy, 0).y;
    r0.z = r0.z * r0.z;
    r4.y = t_initial_texture.SampleLevel(s_default_s, r6.xy, 0).y;
    r2.x = r3.z ? r2.w : r2.x;
    r2.w = 0.25 * r3.x;
    r3.x = -r2.x * 0.5 + r1.y;
    r0.z = r3.w * r0.z;
    r3.x = cmp(r3.x < 0);
    r3.z = -r2.x * 0.5 + r4.x;
    r3.w = -r2.x * 0.5 + r4.y;
    r4.xy = cmp(abs(r3.zw) >= r2.ww);
    r4.z = -r2.y * 1.5 + r5.x;
    r4.z = r4.x ? r5.x : r4.z;
    r5.x = -r3.y * 1.5 + r5.y;
    r4.w = r4.x ? r5.y : r5.x;
    r5.xy = ~(int2)r4.xy;
    r5.x = (int)r5.y | (int)r5.x;
    r5.y = r2.y * 1.5 + r6.x;
    r5.w = r3.y * 1.5 + r6.y;
    r5.yz = r4.yy ? r6.xy : r5.yw;
    if (r5.x != 0) {
      if (r4.x == 0) {
        r3.z = t_initial_texture.SampleLevel(s_default_s, r4.zw, 0).y;
      }
      if (r4.y == 0) {
        r3.w = t_initial_texture.SampleLevel(s_default_s, r5.yz, 0).y;
      }
      r5.x = -r2.x * 0.5 + r3.z;
      r3.z = r4.x ? r3.z : r5.x;
      r4.x = -r2.x * 0.5 + r3.w;
      r3.w = r4.y ? r3.w : r4.x;
      r4.xy = cmp(abs(r3.zw) >= r2.ww);
      r5.x = -r2.y * 2 + r4.z;
      r4.z = r4.x ? r4.z : r5.x;
      r5.x = -r3.y * 2 + r4.w;
      r4.w = r4.x ? r4.w : r5.x;
      r5.xw = ~(int2)r4.xy;
      r5.x = (int)r5.w | (int)r5.x;
      r5.w = r2.y * 2 + r5.y;
      r5.y = r4.y ? r5.y : r5.w;
      r5.w = r3.y * 2 + r5.z;
      r5.z = r4.y ? r5.z : r5.w;
      if (r5.x != 0) {
        if (r4.x == 0) {
          r3.z = t_initial_texture.SampleLevel(s_default_s, r4.zw, 0).y;
        }
        if (r4.y == 0) {
          r3.w = t_initial_texture.SampleLevel(s_default_s, r5.yz, 0).y;
        }
        r5.x = -r2.x * 0.5 + r3.z;
        r3.z = r4.x ? r3.z : r5.x;
        r4.x = -r2.x * 0.5 + r3.w;
        r3.w = r4.y ? r3.w : r4.x;
        r4.xy = cmp(abs(r3.zw) >= r2.ww);
        r5.x = -r2.y * 4 + r4.z;
        r4.z = r4.x ? r4.z : r5.x;
        r5.x = -r3.y * 4 + r4.w;
        r4.w = r4.x ? r4.w : r5.x;
        r5.xw = ~(int2)r4.xy;
        r5.x = (int)r5.w | (int)r5.x;
        r5.w = r2.y * 4 + r5.y;
        r5.y = r4.y ? r5.y : r5.w;
        r5.w = r3.y * 4 + r5.z;
        r5.z = r4.y ? r5.z : r5.w;
        if (r5.x != 0) {
          if (r4.x == 0) {
            r3.z = t_initial_texture.SampleLevel(s_default_s, r4.zw, 0).y;
          }
          if (r4.y == 0) {
            r3.w = t_initial_texture.SampleLevel(s_default_s, r5.yz, 0).y;
          }
          r5.x = -r2.x * 0.5 + r3.z;
          r3.z = r4.x ? r3.z : r5.x;
          r2.x = -r2.x * 0.5 + r3.w;
          r3.w = r4.y ? r3.w : r2.x;
          r2.xw = cmp(abs(r3.zw) >= r2.ww);
          r4.x = -r2.y * 12 + r4.z;
          r4.z = r2.x ? r4.z : r4.x;
          r4.x = -r3.y * 12 + r4.w;
          r4.w = r2.x ? r4.w : r4.x;
          r2.x = r2.y * 12 + r5.y;
          r5.y = r2.w ? r5.y : r2.x;
          r2.x = r3.y * 12 + r5.z;
          r5.z = r2.w ? r5.z : r2.x;
        }
      }
    }
    r2.x = v0.x * g_screen_size.z + -r4.z;
    r2.y = -v0.x * g_screen_size.z + r5.y;
    r2.w = v0.y * g_screen_size.w + -r4.w;
    r2.x = r0.w ? r2.x : r2.w;
    r2.w = -v0.y * g_screen_size.w + r5.z;
    r2.y = r0.w ? r2.y : r2.w;
    r3.yz = cmp(r3.zw < float2(0,0));
    r2.w = r2.y + r2.x;
    r3.xy = cmp((int2)r3.xx != (int2)r3.yz);
    r2.w = 1 / r2.w;
    r3.z = cmp(r2.x < r2.y);
    r2.x = min(r2.x, r2.y);
    r2.y = r3.z ? r3.x : r3.y;
    r0.z = r0.z * r0.z;
    r2.x = r2.x * -r2.w + 0.5;
    r0.z = 0.75 * r0.z;
    r2.x = (int)r2.x & (int)r2.y;
    r0.z = max(r2.x, r0.z);
    r2.xy = r0.zz * r2.zz + r0.xy;
    r3.x = r0.w ? r0.x : r2.x;
    r3.y = r0.w ? r2.y : r0.y;
    o0.xyzw = t_initial_texture.SampleLevel(s_default_s, r3.xy, 0).xyzw;
  } else {
    o0.xyzw = r1.xyzw;
  }
  o0.w = saturate(o0.w);
  return;
}