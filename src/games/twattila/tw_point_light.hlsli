/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's point light: what one of its 40 deferred point-light pixel shaders (shaders/pointlight00-39) adds to a pixel
// with its point shadow lit (visibility 1). Transcribed from the shaders' DXBC (data.pack, the cmd_Decompiler dumps)
// instruction by instruction, in the same operations and order, so that fxc and dxc builds give the game's bits on
// WARP (test/tw_point_light: every variant against the game's own DXBC on the same inputs). That is a test property:
// the game's 4.1 / 5.0 DXBC carries refactoringAllowed, so a GPU driver may fuse or reorder the game's ops (not ours,
// which are precise). On a GPU what precise buys is the same value for the same sample in every pass that calls this.
// The 40 differ by:
// - the model, by the pixel: the game draws every light twice under a stencil test (ref 1, then ref 3, read mask 3;
//   O1's census), so a pixel's stencil & 3 picks it: 1 = "slm" (lit by SSAO), 3 = "skinlm" (skin: wrap light);
//   0 and 2 are lit by neither;
// - the falloff over the distance d and reach r, by the light (its draw's shader): smooth (1 - d / r)^2 or linear
//   1 - d / r;
// - highdef / lowdef: lowdef saturates what the draw writes, in slm AFTER the SSAO: saturate(lit x ssao). A caller
//   that passes ssao 1 and multiplies the SSAO in later gets saturate(lit) x ssao instead (lit 2, SSAO 0.5: the game
//   1.0, that 0.5). lowdef also has no transport to the helper yet (v32's rt_light_kinds holds the falloff only); 4b /
//   4c choose: a second bit, no DI for lowdef lights, or a stated delta;
// - ps_4_0 / gather 4.1 / 5.0, with and without "shadow": the same light; the gather ones also sample the game's point
//   shadow map V, which the game puts inside the falloff ((light x V) x falloff in slm, light x (V x falloff) in
//   skinlm). This function leaves V out, so a caller can only multiply V onto its result (after lowdef's saturate):
//   exact for a V of 0 or 1 (a ray's); a fractional V (the game's PCF) can be 1-2 ulp off, and more with lowdef.
//   Pass the light's centre as `light.xyz` (it sets the falloff distance too), not a sample on its disk.
// fxc (SM4 / SM5) and dxc (SM6) alike: fp32 only, no wave intrinsics, no infinity needed. Every DXBC mad is a mad()
// and acos is written out, and the result is precise: so fxc cannot regroup a product (it did in skinlm's wrap light)
// and dxc keeps no fast fmul / fadd or Acos of its own (both cost bit-exactness). The precise reaches back into the
// caller's lines that compute the inputs: write their mads as mad() where they must match the game.
#ifndef SRC_GAMES_TWATTILA_TW_POINT_LIGHT_HLSLI_
#define SRC_GAMES_TWATTILA_TW_POINT_LIGHT_HLSLI_

// A pixel's stencil & 3 (TwPointLightSurface::model).
static const uint TW_POINT_LIGHT_SLM = 1u;
static const uint TW_POINT_LIGHT_SKIN = 3u;

// The G-buffer texels the light shaders read at the pixel, as stored, and the pixel's world position.
struct TwPointLightSurface {
  float4 channel_1;  // the normal x 0.5 + 0.5, gloss
  float4 channel_2;  // base colour, specular reflectance (slm: its floor; skinlm: its scale)
  float4 channel_3;  // slm: specular colour; skinlm: y the wrap light's share
  float3 position;
  uint model;  // stencil & 3
};

// One light's rgb at the pixel: `light` its position (xyz) and reach (w), `colour` as the add-on sends it (unscaled),
// `linear_falloff` its draw's falloff, `diffuse_scale` the sun texture's texel 2 w (0.004: the game's HDR, g_hdr_on > 0;
// 1: its SDR constants; 0: unknown, the sun texel not written yet: do not shade, as any value outside (0, 1) takes the
// SDR constants, 250x / 273x too bright for an HDR game), `ssao` the slm pixel's SSAO (1 = without SSAO; the caller
// multiplies it later, see lowdef above), `low_def` the lowdef shaders' saturate. 0 where the model is neither slm nor
// skinlm.
float3 TwPointLight(TwPointLightSurface surface, float4 light, float3 colour, bool linear_falloff, float3 camera_position,
                    float diffuse_scale, float ssao, bool low_def) {
  const bool hdr = diffuse_scale > 0.f && diffuse_scale < 1.f;
  const float3 from_light = surface.position - light.xyz;
  const float distance_squared = dot(from_light, from_light);
  const float3 light_to_pixel = from_light * rsqrt(distance_squared);
  const float reach_left = 1.f - saturate(sqrt(distance_squared) / light.w);
  const float falloff = linear_falloff ? reach_left : (reach_left * reach_left);
  float3 normal = mad(surface.channel_1.xyz, 2.f, -1.f);
  normal = normal * rsqrt(dot(normal, normal));
  float3 camera_to_pixel = surface.position - camera_position;
  camera_to_pixel = camera_to_pixel * rsqrt(dot(camera_to_pixel, camera_to_pixel));
  const float n_dot_l = dot(normal, -light_to_pixel);
  const float lit_n_dot_l = max(n_dot_l, 0.f);
  // The game's diffuse x 0.004 and specular x 1 with HDR, x 1 and x 273 without.
  const float diffuse_hdr = hdr ? 0.004f : 1.f;
  precise float3 lit = 0.f;
  if (surface.model == TW_POINT_LIGHT_SLM) {
    const float3 albedo = surface.channel_2.rgb * surface.channel_2.rgb;
    const float reflectance = surface.channel_2.w;
    const float gloss = surface.channel_1.w;
    float specular = 0.f;
    if (0.f < n_dot_l) {
      const float roughness = 1.f - gloss;
      const float view_dot_normal = dot(camera_to_pixel, normal);
      const float3 reflected = mad(normal, -(view_dot_normal + view_dot_normal), camera_to_pixel);
      // acos as fxc expands it (dxc has its own Acos).
      const float cosine = clamp(dot(-light_to_pixel, reflected), -1.f, 1.f);
      const float root = sqrt(1.f - abs(cosine));
      const float poly = mad(mad(mad(abs(cosine), -0.0187292993f, 0.0742610022f), abs(cosine), -0.212114394f),
                             abs(cosine), 1.57072878f);
      const float angle = mad(poly, root, (cosine < -cosine) ? mad(root * poly, -2.f, 3.14159274f) : 0.f);
      // The lobe's width in gloss^2: lerp(diffuse_hdr, 0.9999, gloss^2) as the DXBC folds it; through [0, 1] and back,
      // then sqrt(2) erfinv(t) (Winitzki's form, a 0.140012).
      float t = mad(gloss * gloss, hdr ? 0.995899975f : -0.000100016594f, diffuse_hdr);
      t = mad(t, 0.5f, 0.5f);
      t = mad(t, 2.f, -1.f);
      const float log_q = log2(max(mad(-t, t, 1.f), 0.001f));
      const float erfinv_b = mad(log_q, 0.346573591f, 4.54688501f);
      const float erfinv_root = sqrt(max(sqrt(mad(erfinv_b, erfinv_b, -(log_q * 4.95061684f))) - erfinv_b, 0.f));
      const float lobe_sigma = max(0.00872664619f / (erfinv_root * (float)sign(t) * 1.41421354f), 9.99999975e-05f);
      // The share of the lobe over the light's half-degree disk: (erf(b) - erf(a)) / 2, erf by Winitzki's form.
      // a and b as the DXBC's float2 pairs; for b it writes itof -(0 < b), equal to sign(b) as b > 0 always.
      const float2 edges = (1.f / lobe_sigma) * (angle + float2(-0.00872664619f, 0.00872664619f));
      const float2 scaled = edges * 0.707106769f;
      const float2 squared = scaled * scaled;
      const float2 exponent = -squared * (mad(squared, 0.140012279f, 1.27323949f) / mad(squared, 0.140012279f, 1.f));
      const float2 erf = mad((float2)sign(edges), sqrt(1.f - exp(exponent)), 1.f);
      const float lobe = mad(erf.y, 0.5f, -(erf.x * 0.5f));
      const float shadowing = mad(sin(roughness * 1.57079637f), min(lit_n_dot_l, 1.f) - 1.f, 1.f);
      // Its Fresnel-like rise: (-l.v)^10 x sqrt(cos(roughness x 1.539)), towards saturate(60 x reflectance).
      const float back_light = exp2(log2(max(dot(-light_to_pixel, camera_to_pixel), 0.f)) * 10.f);
      const float rise = 1.f / rsqrt(cos(roughness * 1.53938043f)) * back_light;
      specular = mad(rise, saturate(reflectance * 60.f) - reflectance, reflectance);
      specular = specular * (shadowing * abs(lobe));
    }
    const float3 specular_light = saturate(specular * (hdr ? 1.f : 273.f)) * surface.channel_3.rgb;
    const float3 diffuse_light = (1.f - max(specular, reflectance)) * (albedo * lit_n_dot_l * colour) * diffuse_hdr;
    lit = mad(specular_light, colour, diffuse_light) * falloff * ssao;
  } else if (surface.model == TW_POINT_LIGHT_SKIN) {
    // Phong (R.V)^(63 gloss + 1.6) x reflectance x N.L, and wrap light by channel 3's y (the albedo not squared).
    const float exponent = mad(surface.channel_1.w, 63.f, 1.6f);
    const float light_dot_normal = dot(light_to_pixel, normal);
    const float3 reflected = mad(normal, -(light_dot_normal + light_dot_normal), light_to_pixel);
    const float phong = min(exp2(exponent * log2(max(dot(reflected, -camera_to_pixel), 0.f))), 1.f);
    const float3 specular_light = lit_n_dot_l * (phong * surface.channel_2.w) * colour;
    const float3 wrap = saturate(mad(n_dot_l, float3(0.8f, 0.9f, 0.3f), float3(0.3f, 0.5f, 0.3f))
                                 * float3(0.694444418f, 0.694444418f, 0.444444448f));
    const float wrap_back = saturate(1.f - mad(n_dot_l, wrap.x, 0.3f));
    const float3 wrap_tint = wrap.y * float3(0.8f, 0.7f, 0.2f);
    const float3 wrap_front = mad(n_dot_l, wrap.x, wrap_back * wrap_tint);
    const float3 wrap_deep = mad(-n_dot_l, wrap.x, 1.f) * (wrap.z * float3(0.6f, 0.2f, 0.1f));
    const float3 wrapped = mad(wrap_deep, mad(-wrap_tint, wrap_back, 1.f), wrap_front) - n_dot_l;
    const float3 skin_diffuse = saturate(mad(surface.channel_3.y, wrapped, n_dot_l)) * surface.channel_2.rgb * colour;
    lit = mad(specular_light, diffuse_hdr, diffuse_hdr * skin_diffuse * 0.9f) * falloff;
  }
  return low_def ? saturate(lit) : lit;
}

#endif  // SRC_GAMES_TWATTILA_TW_POINT_LIGHT_HLSLI_
