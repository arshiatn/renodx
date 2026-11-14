// screen_space_shadows.hlsl cs: the sun's shadow mask, Bend's screen-space shadows (bend_sss_gpu.hlsli), with
// SSS_SHADOW_CASCADES (enable_shadows, shadows_hardware_gather_5_0) combined with the shadow cascades; written from the
// game's source (screen_space_shadows.hlsl, parallel_split_shadowing.hlsl, sm5shadows.hlsl 3x3 filter). sss00 / sss01;
// with SSS_PASSTHROUGH (screen-space shadows off) only the cascades, sss02.
// Render scale (DLSS addon): the game builds the light coordinate and the dispatches for the full-size screen, the 3D
// passes draw into its top-left render scale part. The rays used full-size pixels over the rendered depth: shadows cast
// in the wrong direction and length (debug4: extra shadows over the cascades' at 33%). Now the rays stay in full-size
// pixels, read the rendered depth and write each rendered pixel once (bend_sss_gpu.hlsli "RenoDX"); the render size is
// the camera's g_viewport_dimensions (patched by the DLSS addon). At full size the same as the game's.
// Samples (v20, the DLSS addon's Shadow samples): the game's 120 x 2^0-5, up to 3840 (v25; pixels of reach: 120 at 4K
// reach half as far as at 1080p), at run time (bend_sss_gpu.hlsli's loops); SAMPLE_COUNT, the most, sizes groupshared.
#include "../shared.h"

#define WAVE_SIZE           64
#define SAMPLE_COUNT        3840
#define HARD_SHADOW_SAMPLES 8
#define FADE_OUT_SAMPLES    16
#define MAX_SHADOW_SPLITS   4

cbuffer camera_VS_PS : register(b0)
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
  float g_texel_offset : packoffset(c35.z);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);
  float2 g_viewport_origin : packoffset(c39);
  float4 g_render_target_dimensions : packoffset(c40);
}

#ifdef SSS_SHADOW_CASCADES
cbuffer shadowmap_PS : register(b1)
{
  float4 g_vHardShadowBufferSize : packoffset(c0);
  float4x4 g_amHardSplit[MAX_SHADOW_SPLITS] : packoffset(c1);
  float2 g_fFadeRange : packoffset(c17);
  float g_sample_bias : packoffset(c17.z);
  float g_iSplitCount : packoffset(c17.w);
  float3 g_shadow_light_direction : packoffset(c18);
  float g_split_distances[MAX_SHADOW_SPLITS + 1] : packoffset(c19);
}

cbuffer sss : register(b2)
{
  float g_sss_surface_thickness : packoffset(c0);
  float g_sss_bilinear_threshold : packoffset(c0.y);
  float g_sss_shadow_contrast : packoffset(c0.z);
  float4 g_sss_light_coordinate : packoffset(c1);
  int2 g_sss_wave_offset : packoffset(c2);
}

SamplerComparisonState sShadowBuffer_SM50 : register(s0);
SamplerState s_sss_depth : register(s1);
Texture2DArray g_tShadowBuffer_SM50 : register(t0);
Texture2D<float4> gbuffer_colour_texture : register(t1);
Texture2D<float4> gbuffer_readable_depth_texture : register(t2);
Texture2D<float> t_sss_depth : register(t3);
RWTexture2D<unorm float> t_rw_shadow : register(u0);
#else
cbuffer sss : register(b1)
{
  float g_sss_surface_thickness : packoffset(c0);
  float g_sss_bilinear_threshold : packoffset(c0.y);
  float g_sss_shadow_contrast : packoffset(c0.z);
  float4 g_sss_light_coordinate : packoffset(c1);
  int2 g_sss_wave_offset : packoffset(c2);
}

SamplerState s_sss_depth : register(s0);
Texture2D<float> t_sss_depth : register(t0);
RWTexture2D<unorm float> t_rw_shadow : register(u0);
#endif

#ifndef SSS_PASSTHROUGH
#include "./bend_sss_gpu.hlsli"
#endif
#include "./pcss.hlsli"

#ifdef SSS_SHADOW_CASCADES
// sm5shadows.hlsl, verbatim //////////////////////////////////////////////////////////////////////////////////////////
#define FILTER_SIZE ( 3 )
#define GS          ( FILTER_SIZE )
#define GS2         ( FILTER_SIZE / 2 )

static const float fWeightHard[3][3] = 
                 { { 1,2,1 }, 
			       { 2,3,2 },
			       { 1,2,1 },
			       };
			       
// Precalculating the sum of the weightsoft filter helps reduce compilation time
#define PRECOMPUTED_WEIGHTSOFT_SUM 15.0

// Using a macro for the GetWeight() function speeds up compilation time
#define GetWeight( iRow, iCol ) ( fWeightHard[iRow][iCol] )

//--------------------------------------------------------------------------------------
// Returns filtered shadows using SM5.0
//--------------------------------------------------------------------------------------
float DynamicShadowFiltered_3x3( float3 tc, float4 shadow_buffer_size, Texture2DArray t_shadow_buffer, int split_index)
{
    float4 s = (0.0).xxxx;
    uint   uFullDot=0;
    float  fAccumulatedShadow=0.0;
    float2 stc = ( shadow_buffer_size.xy * tc.xy ) + float2( 0.5, 0.5 );
    float2 tcs = floor( stc );
    float2 fc;
    int    row;
    int    col;
    float  w = 0.0;
    float4 v1[ GS2 + 1 ];
    float2 v0[ GS2 + 1 ];
    fc.xy = stc - tcs;
    tc.xy = tcs / shadow_buffer_size.xy;

#ifndef PRECOMPUTED_WEIGHTSOFT_SUM
    [unroll]
    for( row = 0; row < GS; ++row )
    {
        [unroll]
        for( col = 0; col < GS; ++col )
        {
            w += GetWeight( row, col );
        }
    }
#else
    w = PRECOMPUTED_WEIGHTSOFT_SUM;
#endif

    [unroll]
    for( row = -GS2; row <= GS2; row += 2 )
    {
        [unroll]
        for( col = -GS2; col <= GS2; col += 2 )
        {
	        // Fetch 4 depth comparison results
            v1[(col+GS2)/2] = t_shadow_buffer.GatherCmpRed( sShadowBuffer_SM50, float3(tc.xy, split_index), tc.z, int2( col, row ) );
	        
            if( col == -GS2 )
            {
                s[uFullDot++] = ( 1 - fc.y ) * ( v1[0].w * ( GetWeight( row+GS2, col+GS2 ) - GetWeight( row+GS2, col+GS2 ) * fc.x ) + 
                                     v1[0].z * ( fc.x * ( GetWeight( row+GS2, col+GS2 ) - GetWeight( row+GS2, col+GS2+1 ) ) +  GetWeight( row+GS2, col+GS2+1 ) ) );
                s[uFullDot++] = (     fc.y ) * ( v1[0].x * ( GetWeight( row+GS2, col+GS2 ) - GetWeight( row+GS2, col+GS2 ) * fc.x ) + 
                                     v1[0].y * ( fc.x * ( GetWeight( row+GS2, col+GS2 ) - GetWeight( row+GS2, col+GS2+1 ) ) +  GetWeight( row+GS2, col+GS2+1 ) ) );
                                     
                if (uFullDot==4)
                {
                    fAccumulatedShadow += dot(s,(1.0).xxxx);
                    uFullDot = 0;
                }
                    
                if( row > -GS2 )
                {
                    s[uFullDot++] = ( 1 - fc.y ) * ( v0[0].x * ( GetWeight( row+GS2-1, col+GS2 ) - GetWeight( row+GS2-1, col+GS2 ) * fc.x ) + 
                                        v0[0].y * ( fc.x * ( GetWeight( row+GS2-1, col+GS2 ) - GetWeight( row+GS2-1, col+GS2+1 ) ) +  GetWeight( row+GS2-1, col+GS2+1 ) ) );
                    s[uFullDot++] = (     fc.y ) * ( v1[0].w * ( GetWeight( row+GS2-1, col+GS2 ) - GetWeight( row+GS2-1, col+GS2 ) * fc.x ) + 
                                        v1[0].z * ( fc.x * ( GetWeight( row+GS2-1, col+GS2 ) - GetWeight( row+GS2-1, col+GS2+1 ) ) +  GetWeight( row+GS2-1, col+GS2+1 ) ) );
                                        
                    if (uFullDot==4)
                    {
                        fAccumulatedShadow += dot(s,(1.0).xxxx);
                        uFullDot = 0;
                    }                                        
                }
            }
            else if( col == GS2 )
            {
                s[uFullDot++] = ( 1 - fc.y ) * ( v1[GS2].w * ( fc.x * ( GetWeight( row+GS2, col+GS2-1 ) - GetWeight( row+GS2, col+GS2 ) ) + GetWeight( row+GS2, col+GS2 ) ) + 
                                     v1[GS2].z * fc.x * GetWeight( row+GS2, col+GS2 ) );
                s[uFullDot++] = (     fc.y ) * ( v1[GS2].x * ( fc.x * ( GetWeight( row+GS2, col+GS2-1 ) - GetWeight( row+GS2, col+GS2 ) ) + GetWeight( row+GS2, col+GS2 ) ) + 
                                     v1[GS2].y * fc.x * GetWeight( row+GS2, col+GS2 ) );
                                     
                if (uFullDot==4)
                {
                    fAccumulatedShadow += dot(s,(1.0).xxxx);
                    uFullDot = 0;
                }
                
                if( row > -GS2 )
                {
                    s[uFullDot++]= ( 1 - fc.y ) * ( v0[GS2].x * ( fc.x * ( GetWeight( row+GS2-1, col+GS2-1 ) - GetWeight( row+GS2-1, col+GS2 ) ) + GetWeight( row+GS2-1, col+GS2 ) ) + 
                                        v0[GS2].y * fc.x * GetWeight( row+GS2-1, col+GS2 ) );
                    s[uFullDot++]= (     fc.y ) * ( v1[GS2].w * ( fc.x * ( GetWeight( row+GS2-1, col+GS2-1 ) - GetWeight( row+GS2-1, col+GS2 ) ) + GetWeight( row+GS2-1, col+GS2 ) ) + 
                                        v1[GS2].z * fc.x * GetWeight( row+GS2-1, col+GS2 ) );
                                        
                    if (uFullDot==4)
                    {
                        fAccumulatedShadow += dot(s,(1.0).xxxx);
                        uFullDot = 0;
                    }
                }
            }
            else
            {
                s[uFullDot++] = ( 1 - fc.y ) * ( v1[(col+GS2)/2].w * ( fc.x * ( GetWeight( row+GS2, col+GS2-1 ) - GetWeight( row+GS2, col+GS2+0 ) ) + GetWeight( row+GS2, col+GS2+0 ) ) +
                                     v1[(col+GS2)/2].z * ( fc.x * ( GetWeight( row+GS2, col+GS2-0 ) - GetWeight( row+GS2, col+GS2+1 ) ) + GetWeight( row+GS2, col+GS2+1 ) ) );
                s[uFullDot++] =(     fc.y ) * ( v1[(col+GS2)/2].x * ( fc.x * ( GetWeight( row+GS2, col+GS2-1 ) - GetWeight( row+GS2, col+GS2+0 ) ) + GetWeight( row+GS2, col+GS2+0 ) ) +
                                     v1[(col+GS2)/2].y * ( fc.x * ( GetWeight( row+GS2, col+GS2-0 ) - GetWeight( row+GS2, col+GS2+1 ) ) + GetWeight( row+GS2, col+GS2+1 ) ) );
                if (uFullDot==4)
                {
                    fAccumulatedShadow += dot(s,(1.0).xxxx);
                    uFullDot = 0;
                }
                
                if( row > -GS2 )
                {
                    s[uFullDot++] = ( 1 - fc.y ) * ( v0[(col+GS2)/2].x * ( fc.x * ( GetWeight( row+GS2-1, col+GS2-1 ) - GetWeight( row+GS2-1, col+GS2+0 ) ) + GetWeight( row+GS2-1, col+GS2+0 ) ) +
                                        v0[(col+GS2)/2].y * ( fc.x * ( GetWeight( row+GS2-1, col+GS2-0 ) - GetWeight( row+GS2-1, col+GS2+1 ) ) + GetWeight( row+GS2-1, col+GS2+1 ) ) );
                    s[uFullDot++] = (     fc.y ) * ( v1[(col+GS2)/2].w * ( fc.x * ( GetWeight( row+GS2-1, col+GS2-1 ) - GetWeight( row+GS2-1, col+GS2+0 ) ) + GetWeight( row+GS2-1, col+GS2+0 ) ) +
                                        v1[(col+GS2)/2].z * ( fc.x * ( GetWeight( row+GS2-1, col+GS2-0 ) - GetWeight( row+GS2-1, col+GS2+1 ) ) + GetWeight( row+GS2-1, col+GS2+1 ) ) );
                                        
                    if (uFullDot==4)
                    {
                        fAccumulatedShadow += dot(s,(1.0).xxxx);
                        uFullDot = 0;
                    }                                       
                }
            }
		    
		    if( row != GS2 )
            {
			    v0[(col+GS2)/2] = v1[(col+GS2)/2].xy;
            }
	    }
    }
    
    // Remaining shadows if any
    [unroll]
    for (uint i=0; i<uFullDot; i++)
    {
        fAccumulatedShadow += s[i];
    }

    return fAccumulatedShadow/w;
}

// parallel_split_shadowing.hlsl / sm5shadows.hlsl get_shadow_coverage (ENABLE_SHADOWS) ////////////////////////////////
// Shadow map stability (v31, DLSS addon): normal_offset (the surface normal x texels; 0: the game's) moves the receiver
// off its surface by that many of this split's texels.
// Sun shadow softness (v63, DLSS addon): contact-hardening shadows (pcss.hlsli) in place of the game's 3x3 filter, the
// sun SI.sun_shadow_softness degrees across, its disks turned by `shadow_rotation` (ReadShadowMap's pixel and frame).
static const float PCSS_MAX_TEXELS = 24.f;
static float shadow_rotation = 0.f;

float SampleShadowSplit(float4 world_position, float3 normal_offset, int split_index)
{
  if (split_index >= g_iSplitCount) return 1;

  const float4x4 split = g_amHardSplit[split_index];
  // The split's texel in world units: 1 / (its texture u per world unit (orthographic) x the map's size).
  const float texel_world = 1.f / (length(float3(split._m00, split._m10, split._m20)) * g_vHardShadowBufferSize.x);
  world_position.xyz += normal_offset * texel_world;
  float4 tc = mul(world_position, split);
  tc.xyz /= tc.w;
  if (any(tc.xyz < 0.0f) || any(tc.xyz > 1.0f)) return 1;
  [branch]
  if (SI.sun_shadow_softness > 0.f)
  {
    // World units per unit of the map's depth (orthographic).
    const float depth_world = 1.f / length(float3(split._m02, split._m12, split._m22));
    return PcssShadow(g_tShadowBuffer_SM50, sShadowBuffer_SM50, tc.xyz, split_index, g_vHardShadowBufferSize.xy, texel_world,
                      depth_world, tan(radians(SI.sun_shadow_softness * 0.5f)), PCSS_MAX_TEXELS, shadow_rotation);
  }
  return DynamicShadowFiltered_3x3(tc.xyz, g_vHardShadowBufferSize, g_tShadowBuffer_SM50, split_index);
}

float GetShadowCoverage(float4 world_position, float3 normal_offset, float clip_w)
{
  world_position.xyz -= g_shadow_light_direction * g_sample_bias;

  const float4 split_far_planes = float4(g_split_distances[1], g_split_distances[2], g_split_distances[3],
                                         g_split_distances[4]);
  const float4 beyond = clip_w.xxxx >= split_far_planes;
  const int split_index = floor(dot(beyond, beyond) + 0.5f);

  float shadow = SampleShadowSplit(world_position, normal_offset, split_index);
  const float split_length = g_split_distances[split_index + 1] - g_split_distances[split_index];
  if (split_index != g_iSplitCount - 1)
  {
    // Blend to the next split.
    const float blend_length = split_length * g_fFadeRange.x;
    const float blend_start = g_split_distances[split_index + 1] - blend_length;
    shadow = lerp(shadow, SampleShadowSplit(world_position, normal_offset, split_index + 1),
                  saturate((clip_w - blend_start) / blend_length));
  }
  else
  {
    // Blend out the last split.
    const float blendout_start = g_split_distances[split_index] + split_length * (1.0f - g_fFadeRange.y);
    shadow = lerp(shadow, 1.0f, saturate((clip_w - blendout_start) / (split_length * g_fFadeRange.y)));
  }
  return saturate(shadow);
}

// The world position at a rendered pixel, from the depth over the viewport (gbuffer.hlsl
// gbuffer_compute_view_port_world_position).
float3 WorldPositionAt(int2 pixel)
{
  const float depth = gbuffer_readable_depth_texture.Load(int3(pixel, 0)).x;
  const float2 center_tc = (pixel + 0.5f) * screen_size.zw;
  const float2 viewport_tc = (center_tc * g_render_target_dimensions.xy - g_viewport_origin) * g_viewport_dimensions.zw;
  const float4 clip = float4(viewport_tc * float2(2.f, -2.f) - float2(1.f, -1.f), depth, 1.f);
  const float4 world = mul(clip, inv_view_projection);
  return world.xyz / world.w;
}

// screen_space_shadows.hlsl ReadShadowMap: the cascades at a (rendered) pixel.
float ReadShadowMap(float2 pixel_xy)
{
  uint2 size;
  gbuffer_colour_texture.GetDimensions(size.x, size.y);
  const int2 pixel = clamp(int2(floor(size * (pixel_xy + 0.5f) * screen_size.zw)), 0, int2(size) - 1);
  const float3 world_position = WorldPositionAt(pixel);
  shadow_rotation = PcssRotation(float2(pixel), SI.shadow_noise_frame);

  // Shadow map stability (v31, DLSS addon): surfaces the sun grazes shadowed themselves in patches that change with the
  // camera (the filter reaches across the slope; shadow acne). The receiver moves off the surface along its normal
  // (from the neighbours' positions: on each axis the nearer one, not across depth edges; toward the camera) by half
  // or all the 3x3 filter's radius (about 1.5 texels), scaled by the sine between the normal and the sun.
  float3 normal_offset = 0.f;
  [branch]
  if (SI.shadow_map_stability >= 1.f)
  {
    const int2 last = int2(g_viewport_dimensions.xy) - 1;
    const float3 right = WorldPositionAt(min(pixel + int2(1, 0), last)) - world_position;
    const float3 left = world_position - WorldPositionAt(max(pixel - int2(1, 0), 0));
    const float3 down = WorldPositionAt(min(pixel + int2(0, 1), last)) - world_position;
    const float3 up = world_position - WorldPositionAt(max(pixel - int2(0, 1), 0));
    const float3 along_x = (dot(right, right) < dot(left, left)) ? right : left;
    const float3 along_y = (dot(down, down) < dot(up, up)) ? down : up;
    float3 normal = cross(along_x, along_y);
    normal *= rsqrt(max(dot(normal, normal), 1e-20f));  // 0 at the screen's edge (a neighbour clamped onto the pixel)
    normal *= (dot(normal, camera_position - world_position) < 0.f) ? -1.f : 1.f;
    const float sun_cosine = dot(normal, g_shadow_light_direction);
    const float texels = (SI.shadow_map_stability >= 2.f) ? 1.5f : 0.75f;
    normal_offset = normal * texels * sqrt(saturate(1.f - sun_cosine * sun_cosine));
  }

  return GetShadowCoverage(float4(world_position, 1), normal_offset, mul(float4(world_position, 1), view_projection).w);
}
#else
// No cascades (get_shadow_coverage without ENABLE_SHADOWS).
float ReadShadowMap(float2 pixel_xy)
{
  return 1;
}
#endif

#ifdef SSS_PASSTHROUGH
// cs_passthrough: the game passed coords + 0.5 to ReadShadowMap, which adds 0.5 again: each pixel got the shadow of its
// diagonal neighbour, from that neighbour's depth at its corner (half a pixel off the position the depth belongs to).
// Half a display pixel at full size; at 33% 1.5: shadow acne on grazing rock, broken shadows while moving (ATN debug6,
// debug8; Bend's path was right). The pixel's own centre here.
[numthreads(32, 32, 1)]
void main(uint3 coords : SV_DispatchThreadID)
{
  t_rw_shadow[coords.xy] = ReadShadowMap(coords.xy);
}
#else
[numthreads(WAVE_SIZE, 1, 1)]
void main(int3 group_id : SV_GroupID, int thread_id : SV_GroupThreadID)
{
  DispatchParameters dp;
  dp.SetDefaults();
  // Bend's parameters: the game's, or the DLSS addon's rows (above 0).
  dp.SurfaceThickness = (SI.sss_thickness > 0.f) ? SI.sss_thickness : g_sss_surface_thickness;
  dp.BilinearThreshold = (SI.sss_edge_threshold > 0.f) ? SI.sss_edge_threshold : g_sss_bilinear_threshold;
  const float contrast = (SI.sss_contrast > 0.f) ? SI.sss_contrast : g_sss_shadow_contrast;
  dp.ShadowContrast = contrast;
  dp.UsePrecisionOffset = (SI.sss_precision_offset != 0.f);
#ifdef BEND_SSS_LIGHT_CASTERS
  // Light Through Foliage (foliage_light.hpp's versions of this pass): the depth's foliage pixels negative, this share
  // of the light through what they shadow.
  dp.CasterLight = SI.sss_foliage_light;
#endif
  // Shadow stability (v27, DLSS addon): the game's contrast 4 turns a depth difference into shadow within a quarter of
  // Bend's window, so shadows switch fully on / off between frames; halved (Bend's other recommended value, 2) they
  // fade in. Then Bend's IgnoreEdgePixels: depth edges (thin geometry, grazing surfaces) cast no shadow.
  if (SI.sss_stability >= 1.f) dp.ShadowContrast = max(contrast * 0.5f, 1.f);
  dp.IgnoreEdgePixels = (SI.sss_stability >= 2.f) || (SI.sss_ignore_edges != 0.f);
  // Attila's Bend rows (0: the game's): the hard and fade-out samples (multiples of 4, within the count below), the depth
  // read along the shadow's ray, the screen's edges mirrored.
  if (SI.sss_hard_samples > 0.f) dp.HardShadowSamples = int(SI.sss_hard_samples);
  if (SI.sss_fade_samples > 0.f) dp.FadeOutSamples = int(SI.sss_fade_samples);
  dp.BilinearSamplingOffsetMode = (SI.sss_along_ray != 0.f);
  dp.MirrorEdges = (SI.sss_mirror_edges != 0.f);
  dp.LightCoordinate = g_sss_light_coordinate;
  dp.WaveOffset = g_sss_wave_offset;
  dp.FarDepthValue = 1.0f;
  dp.NearDepthValue = 0.0f;
  dp.InvDepthTextureSize = g_render_target_dimensions.zw;
  dp.FullSize = screen_size.xy;
  dp.RenderSize = min(g_viewport_dimensions.xy, screen_size.xy);
  dp.DepthTexture = t_sss_depth;
  dp.OutputTexture = t_rw_shadow;
  dp.PointBorderSampler = s_sss_depth;

  WriteScreenSpaceShadow(dp, group_id, thread_id, 120 << clamp(int(SI.sss_samples), 0, 5));
}
#endif
