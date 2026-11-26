#ifndef BEND_SSS_GPU_H
#define BEND_SSS_GPU_H

// Bend Studio's screen-space shadows as shipped in Total War: SHOGUN 2's shaders.pack (bend_sss_gpu.h), with the
// RenoDX render scale (search "RenoDX"): the game builds the light coordinate and the dispatches for the full-size
// screen, the DLSS addon draws the depth into its top-left render scale part. The rays stay in full-size pixels (the
// same shadow length on screen as at full size), read the depth in the rendered part and write each rendered pixel
// once. At full size the same as the game's. Modified (O-SS5, 11-26): built with BEND_SSS_SCREEN_STEPS (the *_steps
// shaders, the hosts' below 100% render scale) and ScreenStep below 1, the samples are screen pixels between the rendered
// depth's texels (ScreenSample), so the shadows match 100%'s at every render scale; without it Bend's loops as they were
// (100%, Shogun 2: the same code and cost, fx8_ G1: the steps' path in Bend's loops cost +58% at 100%).

// Copyright 2023 Sony Interactive Entertainment.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// If you have feedback, or found this code useful, we'd love to hear from you.
// https://www.bendstudio.com
// https://www.twitter.com/bendstudio
// 
// We are *always* looking for talented graphics and technical programmers!
// https://www.bendstudio.com/careers


// Common screen space shadow projection code (GPU):
//--------------------------------------------------------------

// The main shadow generation function is WriteScreenSpaceShadow(), it will read a depth texture, and write to a shadow texture
// This code is setup to target DX12 DXC shader compiler, but has also been tested on PS5 with appropriate API remapping. 
// It can compile to DX11, but requires some modifications (e.g., early-out's use of wave intrinsics is not supported in DX11).
// Note; you can customize the 'EarlyOutPixel' function to perform custom early-out logic to optimize this shader.

// The following Macros must be defined in the compute shader file before including this header:
//		
//		
//		#define WAVE_SIZE 64						// Wavefront size of the compute shader running this code. 
//													// numthreads[WAVE_SIZE, 1, 1]
//													// Only tested with 64.
//		
//		#define SAMPLE_COUNT 60						// Number of shadow samples per-pixel.
//													// Determines overall cost, as this value controls the length of the shadow (in pixels).
//													// The number of texture-reads performed per-thread will be (SAMPLE_COUNT / WAVE_SIZE + 2) * 2.
//													// Recommended starting value is 60 (This would be 4 reads per thread if WAVE_SIZE is 64). A value of 64 would require 6 reads.
//		
//		// Not all shadow samples are treated the same:
//		//	The bulk of samples will average together in to groups of 4, to produce a slightly smoothed result (so one sample cannot fully show the pixel)
//		//	However, the samples very close to the start pixel can optionally be forced to disable this averaging, so a single sample can fully shadow the pixel (HardShadowSamples)
//		//	Plus, a number of the last (most distant) samples can (for a small cost) apply a fade-out effect to soften a hash shadow cutoff (FadeOutSamples)
//		
//		#define HARD_SHADOW_SAMPLES 4				// Number of initial shadow samples that will produce a hard shadow, and not perform sample-averaging.
//													// This trades aliasing for grounding pixels very close to the shadow caster.
//													// Recommended starting value: 4
//		
//		#define FADE_OUT_SAMPLES 8					// Number of samples that will fade out at the end of the shadow (for a minor cost).
//													// Recommended starting value: 8


// #if defined(__HLSL_VERSION) || defined(__hlsl_dx_compiler)
	
	#define USE_HALF_PIXEL_OFFSET 1		// Apply a 0.5 texel offset when sampling a texture. Toggle this macro if the output shadow has odd, regular grid-like artefacts.

	// HLSL enforces that a pixel offset in a Sample() call must be a compile time constant, which isn't always required - and in some cases can give a small perf boost if used.
	#define USE_UV_PIXEL_BIAS 1			// Use Sample(uv + bias) instead of Sample(uv, bias)

// #endif

// This is the list of runtime properties to pass to the shader
// Wherever possible, it is highly recommended to have these values be compile-time constants
struct DispatchParameters
{
	// Visual configuration:
	// These values will require manual tuning.
	// All shadow computation is performed in non-linear depth space (not in world space), so tuned value choices will depend on scene depth distribution (as determined by the Projection Matrix setup).

	float SurfaceThickness;				// This is the assumed thickness of each pixel for shadow-casting, measured as a percentage of the difference in non-linear depth between the sample and FarDepthValue.
										// Recommended starting value: 0.005 (0.5%)

	float BilinearThreshold;			// Percentage threshold for determining if the difference between two depth values represents an edge, and should not perform interpolation.
										// To tune this value, set 'DebugOutputEdgeMask' to true to visualize where edges are being detected.
										// Recommended starting value: 0.02 (2%)

	float ShadowContrast;				// A contrast boost is applied to the transition in/out of shadow.
										// Recommended starting value: 2 or 4. Values >= 1 are valid.

	bool IgnoreEdgePixels;				// If an edge is detected, the edge pixel will not contribute to the shadow.
										// If a very flat surface is being lit and rendered at an grazing angles, the edge detect may incorrectly detect multiple 'edge' pixels along that flat surface.
										// In these cases, the grazing angle of the light may subsequently produce aliasing artefacts in the shadow where these incorrect edges were detected.
										// Setting this value to true would mean that those pixels would not cast a shadow, however it can also thin out otherwise valid shadows, especially on foliage edges.
										// Recommended starting value: false, unless typical scenes have numerous large flat surfaces, in which case true.

	bool UsePrecisionOffset;			// A small offset is applied to account for an imprecise depth buffer (recommend off)


	bool BilinearSamplingOffsetMode;	// There are two modes to compute bilinear samples for shadow depth:
										// true = sampling points for pixels are offset to the wavefront shared ray, shadow depths and starting depths are the same. Can project more jagged/aliased shadow lines in some cases.
										// false = sampling points for pixels are not offset and start from pixel centers. Shadow depths are biased based on depth gradient across the current pixel bilinear sample. Has more issues in back-face / grazing areas.
										// Both modes have subtle visual differences, which may / may not exaggerate depth buffer aliasing that gets projected in to the shadow.
										// Evaluating the visual difference between each mode is recommended, then hard-coding the mode used to optimize the shader.
										// Recommended starting value: false

	// Debug views
	bool DebugOutputEdgeMask;			// Use this to visualize edges, for tuning the 'BilinearThreshold' value.
	bool DebugOutputThreadIndex;		// Debug output to visualize layout of compute threads
	bool DebugOutputWaveIndex;			// Debug output to visualize layout of compute wavefronts, useful to sanity check the Light Coordinate is being computed correctly.

	// Culling / Early out:
	float2 DepthBounds;					// Depth Bounds (min, max) for the on-screen volume of the light. Typically (0,1) for directional lights. Only used when 'UseEarlyOut' is true.

	bool UseEarlyOut;					// Set to true to early-out when depth values are not within [DepthBounds] - otherwise DepthBounds is unused
										// [Optionally customize the 'EarlyOutPixel()' function to perform your own early-out logic, e.g. skipping pixels that a shadow map indicates are already fully occluded]
										// This can dramatically reduce cost when only a small portion of the pixels need a shadow term (e.g., cull out sky pixels), however it does have some overhead (~15%) in worst-case where nothing early-outs
										// Note; Early-out is most efficient when WAVE_SIZE matches the hardware wavefront size - otherwise cross wave communication is required.


	// RenoDX: HARD_SHADOW_SAMPLES and FADE_OUT_SAMPLES at run time (multiples of 4; their sum within the sample count).
	int HardShadowSamples;
	int FadeOutSamples;
	// RenoDX Mirror Screen Edges: past the left, right and bottom edges the depth just inside, mirrored.
	bool MirrorEdges;
	// RenoDX: the shadow fades out between these shares of the far plane's distance (y <= x: none).
	float2 FarFade;
	// RenoDX render scale (O-SS5): render pixels (groupshared entries) per sample, 1 = Bend's one sample per depth texel.
	// Below 1 the march takes one sample per screen pixel (ScreenSample), so every count of Bend's (the length, hard,
	// fade, its 4 lanes, the near-light window) means on screen what it means at 100%. Read with BEND_SSS_SCREEN_STEPS
	// only.
	float ScreenStep;
#ifdef BEND_SSS_LIGHT_CASTERS
	// RenoDX (Attila's Light Through Foliage): what a depth marked negative shadows keeps this share of the light.
	float CasterLight;
#endif
#ifdef BEND_SSS_EXTENDED
	// RenoDX extended (Attila's sss_bend_extended, with BEND_SSS_LIGHT_CASTERS): Auto Thickness, an occluder's thickness
	// growing by this many windows a sample along its run (0 off); banners where ThinDepth holds their depth (ThinCasters)
	// keep ThinLight of the light; samples nearer the light's screen point than NearLightShare of the pixel's own distance
	// left out (0.5 the sun's; 0 a point light, whose occluders lie anywhere up to it). WriteExtended writes the result,
	// with the first shadowing sample where FindHit and the banner sample shadowing most where FindThin. Each only where
	// compiled in (BEND_SSS_AUTO_THICKNESS, BEND_SSS_FIND: their code even when skipped takes registers Bend's waves need).
	float AutoThicknessScale;
	bool ThinCasters;
	float ThinLight;
	Texture2D<float> ThinDepth;
	float NearLightShare;
	bool FindHit;
	bool FindThin;
#endif

	// Set sensible starting tuning values
	void SetDefaults()
	{
		SurfaceThickness			= 0.005;
		BilinearThreshold			= 0.02;
		ShadowContrast				= 4;
		IgnoreEdgePixels			= false;
		UsePrecisionOffset			= false;
		BilinearSamplingOffsetMode	= false;
		DebugOutputEdgeMask			= false;
		DebugOutputThreadIndex		= false;
		DebugOutputWaveIndex		= false;
		DepthBounds					= float2(0,1);
		UseEarlyOut					= false;
		HardShadowSamples			= HARD_SHADOW_SAMPLES;
		FadeOutSamples				= FADE_OUT_SAMPLES;
		MirrorEdges					= false;
		FarFade						= float2(0, 0);
		ScreenStep					= 1;
#ifdef BEND_SSS_EXTENDED
		AutoThicknessScale			= 0;
		ThinCasters					= false;
		ThinLight					= 0;
		NearLightShare				= 0.5;
		FindHit						= false;
		FindThin					= false;
#endif
	}

	// Runtime data returned from BuildDispatchList():
	float4 LightCoordinate;				// Values stored in DispatchList::LightCoordinate_Shader by BuildDispatchList()
	int2 WaveOffset;					// Values stored in DispatchData::WaveOffset_Shader by BuildDispatchList()

	// Renderer Specific Values:
	float FarDepthValue;				// Set to the Depth Buffer Value for the far clip plane, as determined by renderer projection matrix setup (typically 0).
	float NearDepthValue;				// Set to the Depth Buffer Value for the near clip plane, as determined by renderer projection matrix setup (typically 1).

	// Sampling data:
	float2 InvDepthTextureSize;			// Inverse of the texture dimensions for 'DepthTexture' (used to convert from pixel coordinates to UVs)
										// If 'PointBorderSampler' is an Unnormalized sampler, then this value can be hard-coded to 1.
										// The 'USE_HALF_PIXEL_OFFSET' macro might need to be defined if sampling at exact pixel coordinates isn't precise (e.g., if odd patterns appear in the shadow).

	// RenoDX render scale: the depth's full size and its rendered part (FullSize when not scaled), whose pixels the march
	// covers (marching the full size mapped into it read the same depth texel several times: banded self-shadowing on
	// slopes below 100%, ATN debug14).
	float2 FullSize;
	float2 RenderSize;

	Texture2D<float> DepthTexture;		// Depth Buffer Texture (rasterized non-linear depth)
	RWTexture2D<unorm float> OutputTexture;	// Output screen-space shadow buffer (typically single-channel, 8bit)

	SamplerState PointBorderSampler;	// A point sampler, with Wrap Mode set to Clamp-To-Border-Color (D3D12_TEXTURE_ADDRESS_MODE_BORDER), and Border Color set to "FarDepthValue" (typically zero), or some other far-depth value out of DepthBounds.
										// If you have issues where invalid shadows are appearing from off-screen, it is likely that this sampler is not correctly setup
};
	
	// Forward declare:
	// Generate the shadow
	//	Call this function from a compute shader with thread dimensions: numthreads[WAVE_SIZE, 1, 1]
	// 
	//	(int3)	inGroupID:			Compute shader group id register (SV_GroupID)
	//	(int)	inGroupThreadId:	Compute shader group thread id register (SV_GroupThreadID)
	void WriteScreenSpaceShadow(DispatchParameters inParameters, int3 inGroupID, int inGroupThreadID, int inSampleCount);

	float ReadShadowMap(float2 pixel_xy);

#ifdef BEND_SSS_EXTENDED
	// RenoDX extended: what the march found for a pixel. The first sample that shadows it at all (hit, at hit_xy) and the
	// banner sample that shadows it most (thin, at thin_xy, by thin_weight 0-1), in screen pixels; the pixel's own depth.
	struct ExtendedResult
	{
		bool hit;
		float2 hit_xy;
		bool thin;
		float2 thin_xy;
		float thin_weight;
		float depth;
	};

	// Writes `result` (and what else the includer keeps) for the pixel at `xy`. Declared here, defined by the includer (as
	// ReadShadowMap), which owns the outputs.
	void WriteExtended(DispatchParameters inParameters, int2 xy, float result, ExtendedResult extended);
#endif

#if !defined(WAVE_SIZE) || !defined(SAMPLE_COUNT) || !defined(HARD_SHADOW_SAMPLES) || !defined(FADE_OUT_SAMPLES)
	#error Before including bend_sss_gpu.h, four macros must be defined to configure the shader compile: WAVE_SIZE, SAMPLE_COUNT, HARD_SHADOW_SAMPLES, and FADE_OUT_SAMPLES. See the top of this file for details.
#else

	static bool EarlyOutPixel(struct DispatchParameters inParameters, int2 pixel_xy, float depth)
	{
		//OPTIONAL TODO; customize this function to return true if the pixel should early-out for custom reasons. E.g., A shadow map pass already found the pixel was in shadow / backfaced, etc.
		// Recommended to keep this code very simple!

		// Example:
		// return inParameters.CustomShadowMapTerm[pixel_xy] == 0;

		// (void)pixel_xy;	//unused by this implementation, avoid potential compiler warning.

		// The compiled code will be more optimal if the 'depth' value is not referenced.
		return depth >= inParameters.DepthBounds.y || depth <= inParameters.DepthBounds.x;
	}

	// RenoDX render scale: a rendered pixel's depth UV (off screen: the border, far depth). With MirrorEdges, past the
	// left, right and bottom edges the depth just inside them (ATN, Attila: grass at the border unshadowed when every ray
	// runs off the screen). Not past the top: the far side, where the mirrored ground comes nearer and shadows everything
	// below it (ATN debug10).
	float2 RenderDepthUV(DispatchParameters inParameters, float2 xy)
	{
		float2 uv = xy / inParameters.RenderSize;
		if (inParameters.MirrorEdges)
		{
			uv.x = (uv.x < 0) ? -uv.x : ((uv.x > 1) ? 2 - uv.x : uv.x);
			uv.y = (uv.y > 1) ? 2 - uv.y : uv.y;
		}
		return all(abs(uv - 0.5) < 0.5) ? uv * (inParameters.RenderSize / inParameters.FullSize) : uv;
	}

	// Gets the start pixel coordinates for the pixels in the wavefront
	// Also returns the delta to get to the next pixel after WAVE_COUNT pixels along the ray
	static void ComputeWavefrontExtents(DispatchParameters inParameters, int3 inGroupID, uint inGroupThreadID, out float2 outDeltaXY, out float2 outPixelXY, out float outPixelDistance, out bool outMajorAxisX)
	{
		int2 xy = inGroupID.yz * WAVE_SIZE + inParameters.WaveOffset.xy;

		//integer light position / fractional component
		float2 light_xy = floor(inParameters.LightCoordinate.xy) + 0.5;
		float2 light_xy_fraction = inParameters.LightCoordinate.xy - light_xy;
		bool reverse_direction = inParameters.LightCoordinate.w > 0.0f;

		int2 sign_xy = sign(xy);
		bool horizontal = abs(xy.x + sign_xy.y) < abs(xy.y - sign_xy.x);

		int2 axis;
		axis.x = horizontal ? (+sign_xy.y) : (0);
		axis.y = horizontal ? (0) : (-sign_xy.x);

		// Apply wave offset
		xy = axis * (int)inGroupID.x + xy;
		float2 xy_f = (float2)xy;

		// For interpolation to the light center, we only really care about the larger of the two axis
		bool x_axis_major = abs(xy_f.x) > abs(xy_f.y);
		float major_axis = x_axis_major ? xy_f.x : xy_f.y;

		float major_axis_start = abs(major_axis);
		float major_axis_end = abs(major_axis) - (float)WAVE_SIZE;

		float ma_light_frac = x_axis_major ? light_xy_fraction.x : light_xy_fraction.y;
		ma_light_frac = major_axis > 0 ? -ma_light_frac : ma_light_frac;

		// back in to screen direction
		float2 start_xy = xy_f + light_xy;

		// For the very inner most ring, we need to interpolate to a pixel centered UV, so the UV->pixel rounding doesn't skip output pixels
		float2 end_xy = lerp(inParameters.LightCoordinate.xy, start_xy, (major_axis_end + ma_light_frac) / (major_axis_start + ma_light_frac));

		// The major axis should be a round number
		float2 xy_delta = (start_xy - end_xy);

		// Inverse the read order when reverse direction is true
		float thread_step = (float)(inGroupThreadID ^ (reverse_direction ? 0 : (WAVE_SIZE - 1)));

		float2 pixel_xy = lerp(start_xy, end_xy, thread_step / (float)WAVE_SIZE);
		float pixel_distance = major_axis_start - thread_step + ma_light_frac;

		outPixelXY = pixel_xy;
		outPixelDistance = pixel_distance;
		outDeltaXY = xy_delta;
		outMajorAxisX = x_axis_major;
	}

	// Number of bilinear sample reads performed per-thread
	// RenoDX: groupshared sized for the most samples (SAMPLE_COUNT); WriteScreenSpaceShadow's inSampleCount (Shadow
	// samples, at run time) reads read_count per thread. v25: each read's shadow depth goes to groupshared in the read
	// loop (Bend keeps 4 arrays of READ_COUNT per thread for a second loop: 248 registers at 3840), and the loops run
	// (unrolled, 3840 samples would be tens of thousands of instructions per count); the results are Bend's.
	#define READ_COUNT (SAMPLE_COUNT / WAVE_SIZE + 2)

	// Common shared data
	groupshared float DepthData[READ_COUNT * WAVE_SIZE];
#ifdef BEND_SSS_LIGHT_CASTERS
	// RenoDX: the least a sample's depth delta counts for (0 a solid caster).
	groupshared float LightData[READ_COUNT * WAVE_SIZE];
#endif
	groupshared bool LdsEarlyOut;

	// RenoDX render scale (O-SS5, round_notes plan_1117 designs\sss_scale_thickness.md 2.2): sample `n` of the march,
	// (n + 1) x ScreenStep render pixels from the pixel along the ray (groupshared from `sample_index` on, whose entry
	// k lies k + 1 render pixels out: index -1 is the pixel's own read): x its parallel-space depth (Bend's stored depth),
	// y its light casters' least delta (0 without BEND_SSS_LIGHT_CASTERS). On an entry (ScreenStep 1: always) the entry
	// itself, Bend's to the bit. Between two entries:
	// - on one surface (src: Bend's bilinear edge test, its BilinearThreshold of the remaining depth, here along the ray;
	//   neither entry left out near the light, 1e10; and ours, deviation from the design: a step over one window,
	//   SurfaceThickness of the remaining depth, within a window of the steps before and after it, since Bend's 2%
	//   (/ share) took a caster up to 6 windows before the surface behind it for one surface and the lerp's ramp to it
	//   shadowed what lies behind the caster, tw_screen_space_shadows W2; or a step within one window going the way
	//   of both, a gap that small within Bend's own thickness, since the minor axis's staircase, F7, keeps sloped
	//   ground's steps from equal (grazing ground near the light at 33%, WARP: mean occlusion 0.031 without this, 0.063
	//   with it, 100%'s 0.083), while a thin caster's steps turn back, its lerp a tent narrower than the caster, W4):
	//   ours, the raw depths lerped and projected again with the sample's own distance, exact on planes (z / w and the
	//   distance are affine along the ray: their steps equal; a lerp of the stored depths is off by O(1 / distance^2));
	// - across an edge: ours, the nearer entry (a point-sampled depth of the same edge at screen resolution; a lerp
	//   across it would ramp from the caster to the background, an infinitely thick caster). Its stored depth as it is:
	//   the march tests it against the pixel's own light ray, constant in this space, so it tests that texel as 100%
	//   would (its raw depth at the sample's distance instead moves it off its light ray: near the light's point false
	//   shadows, grazing ground's mean occlusion 0.57 at 33% against 100%'s 0.08).
	// The light casters' delta is the nearer entry's (ours: a sub-sample takes the nearer texel's material, as a full
	// size G-buffer read would).
	// Without BEND_SSS_SCREEN_STEPS: entry n itself (Bend's loops' reads).
	float2 ScreenSample(DispatchParameters inParameters, int sample_index, int n, float first_sample_distance, float direction)
	{
#ifndef BEND_SSS_SCREEN_STEPS
#ifdef BEND_SSS_LIGHT_CASTERS
		return float2(DepthData[sample_index + n], LightData[sample_index + n]);
#else
		return float2(DepthData[sample_index + n], 0);
#endif
#else
		const float q = (float)(n + 1) * inParameters.ScreenStep - 1;
		const float m = floor(q);
		const float f = q - m;
		const int a = sample_index + (int)m;
		float stored = DepthData[a];
#ifdef BEND_SSS_LIGHT_CASTERS
		float light = LightData[a];
#else
		float light = 0;
#endif
		[branch] if (f != 0)
		{
			const float b_stored = DepthData[a + 1];
			const float a_distance = first_sample_distance + (m + 1) * direction;
			const float a_depth = stored * a_distance + inParameters.LightCoordinate.z;
			const float b_depth = b_stored * (a_distance + direction) + inParameters.LightCoordinate.z;
			const float step = b_depth - a_depth;
			const float after_step = DepthData[a + 2] * (a_distance + 2 * direction) + inParameters.LightCoordinate.z - b_depth;
			// The entry before a: the pixel before it on the ray (none before the first wave's own reads: the step after
			// alone).
			const float before_step = (a > 0) ? (a_depth - (DepthData[max(a - 1, 0)] * (a_distance - direction) + inParameters.LightCoordinate.z)) : step;
			const float remaining = abs(inParameters.FarDepthValue - a_depth);
			const float window = inParameters.SurfaceThickness * remaining;
			const bool one_surface = abs(stored) < 1e9 && abs(b_stored) < 1e9 && abs(step) <= inParameters.BilinearThreshold * remaining
			                         && ((abs(after_step - step) <= window && abs(step - before_step) <= window)
			                             || (abs(step) <= window && step * after_step >= 0 && step * before_step >= 0));
#if BEND_SSS_STEP_CONTROL == 1
			// Test only (tw_screen_space_shadows case 11, W5's CONTROL): ATN debug14's staircase, the nearer texel's depth
			// at the sample's own distance.
			stored = (((f >= 0.5) ? b_depth : a_depth) - inParameters.LightCoordinate.z) / (first_sample_distance + (q + 1) * direction);
#else
			if (one_surface)
				stored = (lerp(a_depth, b_depth, f) - inParameters.LightCoordinate.z) / (first_sample_distance + (q + 1) * direction);
			else if (f >= 0.5)
				stored = b_stored;
#endif
#ifdef BEND_SSS_LIGHT_CASTERS
			if (f >= 0.5)
				light = LightData[a + 1];
#endif
		}
		return float2(stored, light);
#endif
	}

#ifdef BEND_SSS_EXTENDED
	// RenoDX extended: what the loops carry between samples. Auto Thickness's run (its first sample, the depth and step of
	// the sample before); the first shadowing sample (FindHit), the banner sample shadowing most and by how much (FindThin):
	// one each, not one a lane (fewer registers: Bend waits on groupshared reads, its speed is how many waves fit).
	struct ExtendedState
	{
		int run_start;
		float previous_depth;
		float previous_step;
		int hit;
		int thin;
		float thin_weight;
	};

	// Samples `first` to `first` + 3 (groupshared from `sample_index` on) against `start_depth` as Bend's loops take them
	// (their depth delta after the light casters' least), the extended march's on top: Auto Thickness (Bjorn Ottosson's "as
	// thick as it is wide", CC0, as bitmask.hlsli's): a run starts where the depth jumps more than a window from the sample
	// before or bends by more than half of one; a sample's back lies 2 (m - 1) windows further than Bend's, m growing along
	// the run, its front edge Bend's; light casters (foliage, banners) stay thin.
	// RenoDX render scale: the samples ScreenSample's (first_sample_distance, direction its ray's).
	float4 ExtendedDeltas(DispatchParameters inParameters, inout ExtendedState state, int first, int sample_index, float start_depth,
	                      float depth_scale, float z_sign, float first_sample_distance, float direction)
	{
		const int4 index = first + int4(0, 1, 2, 3);
		const float2 samples[4] = {ScreenSample(inParameters, sample_index, first, first_sample_distance, direction),
		                           ScreenSample(inParameters, sample_index, first + 1, first_sample_distance, direction),
		                           ScreenSample(inParameters, sample_index, first + 2, first_sample_distance, direction),
		                           ScreenSample(inParameters, sample_index, first + 3, first_sample_distance, direction)};
		const float4 sample_depth = float4(samples[0].x, samples[1].x, samples[2].x, samples[3].x) * depth_scale;
		const float4 light_data = float4(samples[0].y, samples[1].y, samples[2].y, samples[3].y);
		const float4 offset = start_depth - sample_depth;
		float4 depth_delta = abs(offset);
#ifdef BEND_SSS_AUTO_THICKNESS
		[branch] if (inParameters.AutoThicknessScale > 0)
		{
			const float4 step = sample_depth - float4(state.previous_depth, sample_depth.xyz);
			const float4 bend = step - float4(state.previous_step, step.xyz);
			// Each sample's run's first sample: the last break up to it.
			int4 starts = (abs(step) > 1 || abs(bend) > 0.5) ? index : state.run_start;
			starts.y = max(starts.y, starts.x);
			starts.z = max(starts.z, starts.y);
			starts.w = max(starts.w, starts.z);
			state.run_start = starts.w;
			state.previous_depth = sample_depth.w;
			state.previous_step = step.w;
			const float4 m = (light_data != 0) ? 1 : min(1 + (float4)(index - starts) * inParameters.AutoThicknessScale, 16);
			const float4 behind = offset * z_sign;
			depth_delta = max(-behind, behind - 2 * (m - 1));
		}
#endif
#ifdef BEND_SSS_FIND
		[branch] if (inParameters.FindHit && state.hit < 0)
		{
			const bool4 shadows = depth_delta < 1;
			state.hit = shadows.x ? index.x : shadows.y ? index.y : shadows.z ? index.z : shadows.w ? index.w : -1;
		}
		[branch] if (inParameters.FindThin)
		{
			const float4 weight = (light_data < 0 && depth_delta < 1)
			                          ? 1 - saturate(depth_delta * inParameters.ShadowContrast + (1 - inParameters.ShadowContrast))
			                          : 0;
			const float most = max(max(weight.x, weight.y), max(weight.z, weight.w));
			if (most > state.thin_weight)
			{
				state.thin_weight = most;
				state.thin = (weight.x == most) ? index.x : (weight.y == most) ? index.y : (weight.z == most) ? index.z : index.w;
			}
		}
#endif
		return max(depth_delta, abs(light_data));
	}
#endif

	// Generate the shadow
	//	Call this function from a compute shader with thread dimensions: numthreads[WAVE_SIZE, 1, 1]
	// 
	//	(int3)	inGroupID:			Compute shader group id register (SV_GroupID)
	//	(int)	inGroupThreadId:	Compute shader group thread id register (SV_GroupThreadID)
	void WriteScreenSpaceShadow(DispatchParameters inParameters, int3 inGroupID, int inGroupThreadID, int inSampleCount)
	{
#ifdef BEND_SSS_SCREEN_STEPS
		// RenoDX render scale: the reads cover the render pixels the samples span (ScreenStep 1: Bend's).
		const int read_count = (int)((uint)ceil((float)inSampleCount * inParameters.ScreenStep) / WAVE_SIZE) + 2;
#else
		const int read_count = inSampleCount / WAVE_SIZE + 2;
#endif
		float2 xy_delta;
		float2 pixel_xy;
		float pixel_distance;
		bool x_axis_major;	// major axis is x axis? abs(xy_delta.x) > abs(xy_delta.y).

		ComputeWavefrontExtents(inParameters, (int3)inGroupID, inGroupThreadID.x, xy_delta, pixel_xy, pixel_distance, x_axis_major);

		// Read in the depth values (RenoDX: the first read's, the pixel's own, kept for the shadow)
		float first_sampling_depth = 0;
		float first_depth_thickness_scale = 1;
		float first_sample_distance = 1;

		const float direction = -inParameters.LightCoordinate.w;
		const float z_sign = inParameters.NearDepthValue > inParameters.FarDepthValue ? -1 : +1;

		int i;
		bool is_edge = false;
		bool skip_pixel = false;
		float2 write_xy = floor(pixel_xy);
#ifdef BEND_SSS_EXTENDED
		// The groupshared samples' order runs along the ray: index thread + 1 + n lies n + 1 of these steps from the pixel.
		const float2 own_xy = pixel_xy;
		const float2 step_xy = xy_delta * direction / (float)WAVE_SIZE;
#endif

		[loop] for (i = 0; i < read_count; i++)
		{
			// We sample depth twice per pixel per sample, and interpolate with an edge detect filter
			// Interpolation should only occur on the minor axis of the ray - major axis coordinates should be at pixel centers
			float2 read_xy = floor(pixel_xy);
			float minor_axis = x_axis_major ? pixel_xy.y : pixel_xy.x;

			// If a pixel has been detected as an edge, then optionally (inParameters.IgnoreEdgePixels) don't include it in the shadow
			const float edge_skip = 1e20;	// if edge skipping is enabled, apply an extreme value/blend on edge samples to push the value out of range

			float2 depths;
			float bilinear = frac(minor_axis) - 0.5;

		#if USE_HALF_PIXEL_OFFSET
			read_xy += 0.5;
		#endif

		#if USE_UV_PIXEL_BIAS
			float bias = bilinear > 0 ? 1 : -1;
			float2 offset_xy = float2(x_axis_major ? 0 : bias, x_axis_major ? bias : 0);

			// HLSL enforces that a pixel offset is a compile-time constant, which isn't strictly required (and can sometimes be a bit faster)
			// So this fallback will use a manual uv offset instead
			depths.x = inParameters.DepthTexture.SampleLevel(inParameters.PointBorderSampler, RenderDepthUV(inParameters, read_xy), 0);
			depths.y = inParameters.DepthTexture.SampleLevel(inParameters.PointBorderSampler, RenderDepthUV(inParameters, read_xy + offset_xy), 0);
		#else
			int bias = bilinear > 0 ? 1 : -1;
			int2 offset_xy = int2(x_axis_major ? 0 : bias, x_axis_major ? bias : 0);

			depths.x = inParameters.DepthTexture.SampleLevel(inParameters.PointBorderSampler, RenderDepthUV(inParameters, read_xy), 0);
			depths.y = inParameters.DepthTexture.SampleLevel(inParameters.PointBorderSampler, read_xy * inParameters.InvDepthTextureSize, 0, offset_xy);
		#endif

#ifdef BEND_SSS_LIGHT_CASTERS
			const bool light_caster = depths.x < 0;
			depths = abs(depths);
#endif
#ifdef BEND_SSS_EXTENDED
			// A banner's pixel: the depth its own draw left (InstallThin) is the scene's there (within its depth bias).
			bool thin_caster = false;
			if (inParameters.ThinCasters && !light_caster)
			{
				const float thin_depth = inParameters.ThinDepth.SampleLevel(inParameters.PointBorderSampler, RenderDepthUV(inParameters, read_xy), 0);
				const float remaining = abs(inParameters.FarDepthValue - depths.x);
				thin_caster = remaining > 1e-7 && abs(thin_depth - depths.x) <= 4e-7 + 2e-4 * remaining;
			}
#endif

			// Depth thresholds (bilinear/shadow thickness) are based on a fractional ratio of the difference between sampled depth and the far clip depth
			const float depth_thickness_scale = abs(inParameters.FarDepthValue - depths.x);

			// If depth variance is more than a specific threshold, then just use point filtering
			bool use_point_filter = abs(depths.x - depths.y) > depth_thickness_scale * inParameters.BilinearThreshold;

			float sampling_depth;
			float shadowing_depth;

			// Store for debug output when inParameters.DebugOutputEdgeMask is true
			if (i == 0) is_edge = use_point_filter;

			if (inParameters.BilinearSamplingOffsetMode)
			{
				bilinear = use_point_filter ? 0 : bilinear;
				//both shadow depth and starting depth are the same in this mode, unless shadow skipping edges
				sampling_depth = lerp(depths.x, depths.y, abs(bilinear));
				shadowing_depth = (inParameters.IgnoreEdgePixels && use_point_filter) ? edge_skip : sampling_depth;
			}
			else
			{
				// The pixel starts sampling at this depth
				sampling_depth = depths.x;

				float edge_depth = inParameters.IgnoreEdgePixels ? edge_skip : depths.x;
				// Any sample in this wavefront is possibly interpolated towards the bilinear sample
				// So use should use a shadowing depth that is further away, based on the difference between the two samples
				float shadow_depth = depths.x + abs(depths.x - depths.y) * z_sign;

				// Shadows cast from this depth
				shadowing_depth = use_point_filter ? edge_depth : shadow_depth;
			}

			// Store for later
			const float sample_distance = pixel_distance + (WAVE_SIZE * i) * direction;
			if (i == 0)
			{
				first_sampling_depth = sampling_depth;
				first_depth_thickness_scale = depth_thickness_scale;
				first_sample_distance = sample_distance;
			}

			// Write the shadow depth to LDS (Bend's second loop)
			{
				// Perspective correct the shadowing depth, in this space, all light rays are parallel
				float stored_depth = (shadowing_depth - inParameters.LightCoordinate.z) / sample_distance;

				if (i != 0)
				{
					// For pixels within sampling distance of the light, it is possible that sampling will
					// overshoot the light coordinate for extended reads. We want to ignore these samples
					// RenoDX: also those nearer the light's screen point than half the pixel's own distance: there the
					// divide by the distance magnifies the depth's error past the shadow window (ATN, Attila: long shadows
					// towards the sun striped along Bend's 64-pixel rings, moving with the camera).
#ifdef BEND_SSS_EXTENDED
					stored_depth = sample_distance > inParameters.NearLightShare * first_sample_distance ? stored_depth : 1e10;
#else
					stored_depth = sample_distance > 0.5 * first_sample_distance ? stored_depth : 1e10;
#endif
				}

				// Store the depth values in groupshared
				int idx = (i * WAVE_SIZE) + inGroupThreadID.x;
				DepthData[idx] = stored_depth;
#ifdef BEND_SSS_LIGHT_CASTERS
				// The contrast below turns this least delta into the caster's share of the light.
#ifdef BEND_SSS_EXTENDED
				// A banner's negative (its colour told apart from foliage's; above 0 for any ThinLight above 0, Contrast 1 up).
				LightData[idx] = thin_caster    ? -(1 - (1 - inParameters.ThinLight) / inParameters.ShadowContrast)
				                 : light_caster ? 1 - (1 - inParameters.CasterLight) / inParameters.ShadowContrast
				                                : 0;
#else
				LightData[idx] = light_caster ? 1 - (1 - inParameters.CasterLight) / inParameters.ShadowContrast : 0;
#endif
#endif
			}

			// Iterate to the next pixel along the ray. This will be WAVE_SIZE pixels along the ray...
			pixel_xy += xy_delta * direction;
		}

		// MTB-anton.korzhuk: disable as this requires Shader Model 6.0
#if 0
		// Using early out, and no debug mode is enabled?
		if (inParameters.UseEarlyOut && (inParameters.DebugOutputWaveIndex == false && inParameters.DebugOutputThreadIndex == false && inParameters.DebugOutputEdgeMask == false))
		{
			// read the depth of the pixel we are shadowing, and early-out
			// The compiler will typically rearrange this code to put it directly after the first depth read
			skip_pixel = EarlyOutPixel(inParameters, (int2)write_xy, first_sampling_depth);

			// are all threads in this wave out of bounds?
			bool early_out = WaveActiveAnyTrue(!skip_pixel) == false;

			// WaveGetLaneCount returns the hardware wave size
			if (WaveGetLaneCount() == WAVE_SIZE)
			{
				// Optimal case:
				// If each wavefront is just a single wave, then we can trivially early-out.
				if (early_out == true)
					return;
			}
			else
			{
				// This wavefront is made up of multiple small waves, so we need to coordinate them for all to early-out together.
				// Doing this can make the worst case (all pixels drawn) a bit more expensive (~15%), but the best-case (all early-out) is typically 2-3x better.
				LdsEarlyOut = true;

				GroupMemoryBarrierWithGroupSync();

				[branch] if (early_out == false)
					LdsEarlyOut = false;

				GroupMemoryBarrierWithGroupSync();

				[branch] if (LdsEarlyOut)
					return;
			}
		}
#endif

		// RenoDX: a pixel at the far plane (the sky) is no surface, and Bend's thickness scale there (its distance to the
		// far plane) is 0, which divides into garbage below: dark streaks in the sky (ATN). It keeps no shadow.
		skip_pixel = skip_pixel || (abs(inParameters.FarDepthValue - first_sampling_depth) < 1e-7);

		// Sync wavefronts now groupshared DepthData is written
		GroupMemoryBarrierWithGroupSync();

		// If the starting depth isn't in depth bounds, then we don't need a shadow
		if (skip_pixel)
			return;

		float start_depth = first_sampling_depth;

		// lerp away from far depth by a tiny fraction?
		if (inParameters.UsePrecisionOffset)
			start_depth = lerp(start_depth, inParameters.FarDepthValue, -1.0 / 0xFFFF);

		// perspective correct the depth
		start_depth = (start_depth - inParameters.LightCoordinate.z) / first_sample_distance;

		// Start by reading the next value
		int sample_index = inGroupThreadID.x + 1;

		float4 shadow_value = 1;
		float hard_shadow = 1;

		// This is the inverse of how large the shadowing window is for the projected sample data. 
		// All values in the LDS sample list are scaled by 1.0 / sample_distance, such that all light directions become parallel.
		// The multiply by sample_distance[0] here is to compensate for the projection divide in the data.
		// The 1.0 / inParameters.SurfaceThickness is to adjust user selected thickness. So a 0.5% thickness will scale depth values from [0,1] to [0,200]. The shadow window is always 1 wide.
		// 1.0 / depth_thickness_scale[0] is because SurfaceThickness is percentage of remaining depth between the sample and the far clip - not a percentage of the full depth range.
		// The min() function is to make sure the window is a minimum width when very close to the light. The +direction term will bias the result so the pixel at the very center of the light is either fully lit or shadowed
		// RenoDX render scale (deviation from Bend's line, O-SS5 F6): the distance in the minimum in screen pixels (Bend's own
		// pixels at 100%), so the widened window keeps its radius on screen.
#if !defined(BEND_SSS_SCREEN_STEPS) || BEND_SSS_STEP_CONTROL == 2
		// Bend's (test only with BEND_SSS_SCREEN_STEPS: tw_screen_space_shadows case 11, W6's numbers, the distance in
		// render pixels).
		float depth_scale = min(first_sample_distance + direction, 1.0 / inParameters.SurfaceThickness) * first_sample_distance / first_depth_thickness_scale;
#else
		float depth_scale = min(first_sample_distance / inParameters.ScreenStep + direction, 1.0 / inParameters.SurfaceThickness) * first_sample_distance / first_depth_thickness_scale;
#endif

		start_depth = start_depth * depth_scale - z_sign;

#ifdef BEND_SSS_EXTENDED
		// RenoDX extended: Bend's three loops 4 samples at a time (the caller's counts multiples of 4, twattila's sss_bend
		// rounds them: the same minimums into the same lanes), each sample's delta from ExtendedDeltas.
		ExtendedState state;
		state.run_start = 0;
		state.previous_depth = DepthData[sample_index - 1] * depth_scale;
		state.previous_step = 0;
		state.hit = -1;
		state.thin = -1;
		state.thin_weight = 0;
		[loop] for (i = 0; i < inParameters.HardShadowSamples; i += 4)
		{
			const float4 depth_delta = ExtendedDeltas(inParameters, state, i, sample_index, start_depth, depth_scale, z_sign, first_sample_distance, direction);
			hard_shadow = min(hard_shadow, min(min(depth_delta.x, depth_delta.y), min(depth_delta.z, depth_delta.w)));
		}
		[loop] for (i = inParameters.HardShadowSamples; i < inSampleCount - inParameters.FadeOutSamples; i += 4)
		{
			shadow_value = min(shadow_value, ExtendedDeltas(inParameters, state, i, sample_index, start_depth, depth_scale, z_sign, first_sample_distance, direction));
		}
		[loop] for (int fade = 0; fade < inParameters.FadeOutSamples; fade += 4)
		{
			const float4 fade_out = (float4)(fade + int4(1, 2, 3, 4)) / (float)(inParameters.FadeOutSamples + 1) * 0.75;
			const float4 depth_delta = ExtendedDeltas(inParameters, state, inSampleCount - inParameters.FadeOutSamples + fade, sample_index,
			                                          start_depth, depth_scale, z_sign, first_sample_distance, direction);
			shadow_value = min(shadow_value, depth_delta + fade_out);
		}
		const int hit = state.hit;
		const int thin_hit = state.thin;
		float thin_weight = state.thin_weight;
#else
		// The first number of hard shadow samples, a single pixel can produce a full shadow
		// (RenoDX render scale: every sample of the three loops ScreenSample's)
		[loop] for (i = 0; i < inParameters.HardShadowSamples; i++)
		{
			const float2 hard_sample = ScreenSample(inParameters, sample_index, i, first_sample_distance, direction);
			float depth_delta = abs(start_depth - hard_sample.x * depth_scale);
#ifdef BEND_SSS_LIGHT_CASTERS
			depth_delta = max(depth_delta, hard_sample.y);
#endif

			// We want to find the distance of the sample that is closest to the reference depth
			hard_shadow = min(hard_shadow, depth_delta);
		}

		// Brute force go!
		// The main shadow samples, averaged in to a set of 4 shadow values
		// RenoDX: 4 per iteration, sample i into component i & 3 as Bend's (HARD_SHADOW_SAMPLES, FADE_OUT_SAMPLES and
		// the sample counts are multiples of 4).
		[loop] for (i = inParameters.HardShadowSamples; i < inSampleCount - inParameters.FadeOutSamples; i += 4)
		{
			const float2 main_samples[4] = {ScreenSample(inParameters, sample_index, i, first_sample_distance, direction),
			                                ScreenSample(inParameters, sample_index, i + 1, first_sample_distance, direction),
			                                ScreenSample(inParameters, sample_index, i + 2, first_sample_distance, direction),
			                                ScreenSample(inParameters, sample_index, i + 3, first_sample_distance, direction)};
			float4 depth_delta = abs(start_depth - float4(main_samples[0].x, main_samples[1].x, main_samples[2].x, main_samples[3].x) * depth_scale);
#ifdef BEND_SSS_LIGHT_CASTERS
			depth_delta = max(depth_delta, float4(main_samples[0].y, main_samples[1].y, main_samples[2].y, main_samples[3].y));
#endif

			// Do the same as the hard_shadow code above, but this will accumulate to 4 separate values.
			// By using 4 values, the average shadow can be taken, which can help soften single-pixel shadows.
			shadow_value = min(shadow_value, depth_delta);
		}

		// Final fade out samples (RenoDX: counted from the first, fade; Bend's i is inSampleCount - FADE_OUT_SAMPLES + fade)
		[loop] for (int fade = 0; fade < inParameters.FadeOutSamples; fade++)
		{
			const float2 fade_sample = ScreenSample(inParameters, sample_index, inSampleCount - inParameters.FadeOutSamples + fade, first_sample_distance, direction);
			float depth_delta = abs(start_depth - fade_sample.x * depth_scale);
#ifdef BEND_SSS_LIGHT_CASTERS
			depth_delta = max(depth_delta, fade_sample.y);
#endif

			// Add the fade value to these samples
			const float fade_out = (float)(fade + 1) / (float)(inParameters.FadeOutSamples + 1) * 0.75;

			// RenoDX: its component by a mask (a run-time count: no unrolled indexing).
			const bool4 lane = ((fade & 3) == int4(0, 1, 2, 3));
			shadow_value = lane ? min(shadow_value, depth_delta + fade_out) : shadow_value;
		}
#endif

		// Apply the contrast value.
		// A value of 0 indicates a sample was exactly matched to the reference depth (and the result is fully shadowed)
		// We want some boost to this range, so samples don't have to exactly match to produce a full shadow. 
		shadow_value = saturate(shadow_value * (inParameters.ShadowContrast) + (1 - inParameters.ShadowContrast));
		hard_shadow = saturate(hard_shadow * (inParameters.ShadowContrast) + (1 - inParameters.ShadowContrast));

		float result = 0;

		// Take the average of 4 samples, this is useful to reduces aliasing noise in the source depth, especially with long shadows.
		result = dot(shadow_value, 0.25);

		// If the first samples are always producing a hard shadow, then compute this value separately.
		result = min(hard_shadow, result);

		// RenoDX render scale: off-screen pixels are skipped (writes there were out of bounds).
		if (any(int2(write_xy) < 0) || any(int2(write_xy) >= int2(inParameters.RenderSize))) return;

		// RenoDX FarFade: the pixel's distance as a share of the far plane's, (depth_inf - 1) / (depth_inf - depth) with
		// depth_inf the projection's depth at infinity (the light's z). Far away a screen-space shadow resolves nothing, and Attila's
		// campaign backdrop (the clouds past the map's edge, three quarters of the way out, its depth nearer towards the
		// top) shadowed itself (ATN debug21).
		if (inParameters.FarFade.y > inParameters.FarFade.x && inParameters.LightCoordinate.z > 1)
		{
			const float distance_share = (inParameters.LightCoordinate.z - 1) / (inParameters.LightCoordinate.z - first_sampling_depth);
			result = lerp(result, 1, smoothstep(inParameters.FarFade.x, inParameters.FarFade.y, distance_share));
#ifdef BEND_SSS_EXTENDED
			thin_weight *= 1 - smoothstep(inParameters.FarFade.x, inParameters.FarFade.y, distance_share);
#endif
		}

		// Add shadowmap data
		result = min(ReadShadowMap(write_xy), result);

		//write the result
		{
			if (inParameters.DebugOutputEdgeMask)
				result = is_edge ? 1 : 0;
			if (inParameters.DebugOutputThreadIndex)
				result = (inGroupThreadID / (float)WAVE_SIZE);
			if (inParameters.DebugOutputWaveIndex)
				result = frac(inGroupID.x / (float)WAVE_SIZE);
#ifdef BEND_SSS_OCCLUSION
			// RenoDX (Attila, twattila/screen_space_shadows.hpp): the occlusion, 1 - the shadow term, so the texture's
			// unwritten (cleared) or unbound value 0 leaves the light as it was.
			result = 1 - result;
#endif

			// Asking the GPU to write scattered single-byte pixels isn't great,
			// But thankfully the latency is hidden by all the work we're doing...
#ifdef BEND_SSS_EXTENDED
			ExtendedResult extended;
			extended.hit = hit >= 0;
			// RenoDX render scale: a sample is ScreenStep render pixels (step_xy's) along the ray.
#if !defined(BEND_SSS_SCREEN_STEPS) || BEND_SSS_STEP_CONTROL == 3
			// One render pixel a sample (test only with BEND_SSS_SCREEN_STEPS: tw_screen_space_shadows case 11, W8's
			// CONTROL).
			const float sample_step = 1;
#else
			const float sample_step = inParameters.ScreenStep;
#endif
			extended.hit_xy = own_xy + (float)(hit + 1) * sample_step * step_xy;
			extended.thin = thin_hit >= 0 && thin_weight > 0;
			extended.thin_xy = own_xy + (float)(thin_hit + 1) * sample_step * step_xy;
			extended.thin_weight = thin_weight;
			extended.depth = first_sampling_depth;
			WriteExtended(inParameters, (int2)write_xy, result, extended);
#else
			inParameters.OutputTexture[(int2)write_xy] = result;
#ifdef BEND_SSS_RACE_MAP
			RaceRecord((int2)write_xy, result, -1, -1);
#endif
#endif
		}
	}

#endif // macro check

#endif
