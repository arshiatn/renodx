/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

// NVIDIA's DLSS runs only in 64-bit processes; Attila, Rome II and Thrones are 32-bit. The DLAA bridge: the addon
// (dlaa_bridge.hpp) shares its inputs, output and a fence with a 64-bit helper (dlaa_helper/main.cpp) on the same GPU,
// through this block of shared memory ("Local\RenoDX_TW_DLAA_<game pid>") and two events (its name + "_request" /
// "_done"). Handles are the helper's own (the addon duplicates them into it); 64-bit fields so both builds agree.
namespace twattila::dlaa_bridge {

constexpr uint32_t VERSION = 31;
// The helper still answers addons of this version (v20 grew RT_CAPTURES, RT_DRAWN and RT_PLACEMENTS in place: older
// addons' blocks no longer match).
constexpr uint32_t OLDEST_VERSION = 20;

// Ray tracing (v4, rt_scene.hpp / dlaa_helper/rt_d3d12.hpp): the captured triangles' world positions, RT_TRANSFER_WIDTH
// per row of the shared vertex texture (RT_TRANSFER_HEIGHT rows; 4096 since the units' and the terrain's shares were
// full in a battle, 5120 with swaying trees, 8192 since city walls were left out in a 4x battle; a helper of either
// height reads the rows an addon fills); new captures and drawn meshes a frame at most (v20: 1024 and 16384, from 256
// and 8192: Constantinople's grass shadows flickered, ATN 2026-11-01; a grass draw that is not placed takes a capture,
// what did not fit changed every frame).
constexpr uint32_t RT_TRANSFER_WIDTH = 4096;
constexpr uint32_t RT_TRANSFER_HEIGHT = 8192;
constexpr uint32_t RT_CAPTURES = 1024;
constexpr uint32_t RT_DRAWN = 16384;
// The leaves' opacity masks (v6): an R8 texture array of RT_ATLAS_SLICES slices RT_ATLAS_SIZE square.
constexpr uint32_t RT_ATLAS_SIZE = 512;
constexpr uint32_t RT_ATLAS_SLICES = 256;
// Local lights' shadows (v9): at most this many lights a request (a bit each in the lit mask); v31: RT_ALL_LIGHTS, each
// 8 x 8 tile of the mask its own RT_MAX_LIGHTS (their list in the mask's rows from rt_light_tile_row on).
constexpr uint32_t RT_MAX_LIGHTS = 32;
constexpr uint32_t RT_ALL_LIGHTS = 128;
// The lit mask's tiles (v31): a tile's RT_MAX_LIGHTS lights a byte each (RT_NO_LIGHT past the last), its eight words at
// column tile x * 4 + (word & 3), row rt_light_tile_row + tile y * 2 + (word >> 2); the mask that many rows taller.
constexpr uint32_t RT_LIGHT_TILE = 8;
constexpr uint32_t RT_NO_LIGHT = 0xFF;
constexpr uint32_t LightTileRows(uint32_t height) { return ((height + RT_LIGHT_TILE - 1) / RT_LIGHT_TILE) * 2; }
// Placed draws (v14) a frame at most, besides the new captures (v20: 16384).
constexpr uint32_t RT_PLACEMENTS = 16384;

// A draw captured this frame (its key, its triangles' vertices in the vertex texture) or drawn this frame (key only).
struct RtCapture {
  uint64_t key = 0;
  uint32_t first_vertex = 0;
  uint32_t vertex_count = 0;
  uint32_t flags = 0;      // RT_TERRAIN, RT_DYNAMIC, RT_FOLIAGE, RT_INSTANCED
  uint32_t instances = 0;  // v10, with RT_INSTANCED
};
// Terrain: only in frames that draw it (its older levels of detail would shadow this one). Dynamic (units): captured
// every frame, in that frame's scene only. Foliage (leaves): alpha-tested against its opacity mask (v6: the vertex
// texture's w its UV as two halves, rt_capture_slices its atlas slice; older addons: opaque).
constexpr uint32_t RT_TERRAIN = 1;
constexpr uint32_t RT_DYNAMIC = 2;
constexpr uint32_t RT_FOLIAGE = 4;
// Instanced (v10): the vertices are the draw's first instance's; its `instances` instances' world rows follow them (four
// texels each: rows 0-2 acting on (position, 1), a fourth unused). The helper builds one BLAS and places it once per
// instance (each instance's rows times the first one's inverse). v14: keyed by the draw's geometry alone, `instances`
// from 1, placed where its draws put its instances (this capture's rows, then RtPlacement's); the helper keeps each
// instance a while after its last draw (off-screen casters), but those of meshes drawn moving.
constexpr uint32_t RT_INSTANCED = 8;
// A placed draw (v14) whose BLAS the helper has: its instances' rows this frame in the vertex texture (as RT_INSTANCED's).
// The game repacks its per-instance streams every frame it culls: keyed by those, every draw was a new mesh every frame.
struct RtPlacement {
  uint64_t key = 0;
  uint32_t first_vertex = 0;
  uint32_t instances = 0;
  uint32_t flags = 0;  // RT_PLACE_MOVING
  uint32_t padding = 0;
};
// The mesh was seen moving lately: its instances where they are now only (none kept from earlier draws).
constexpr uint32_t RT_PLACE_MOVING = 1;
// Its instances also drawn this frame as a compact draw (swaying leaves): placed hidden (no ray meets them), kept for
// when they are off-screen (shown again then).
constexpr uint32_t RT_PLACE_HIDDEN = 2;
// All of the mesh's instances this frame are in this frame's placements (helpers 59 on): a kept instance in view that was
// not placed again is gone (the game drew another level of detail there).
constexpr uint32_t RT_PLACE_WHOLE = 4;
// Rows from the shadow pass: rt_place takes them after every other draw's, so an instance both passes drew is the main
// pass's (its swaying copy's hidden one), the same every frame.
constexpr uint32_t RT_PLACE_CASCADE = 8;
// Compact draws (v16, to a helper that says rt_compact_ok: RTX Mega Geometry's clusters): a dynamic indexed draw's
// vertices streamed once each, in its mesh's cluster order: `instances` blocks of its set's `vertices` texels from
// `first_vertex`. Its cluster set (the mesh's index list split into clusters of at most RT_CLUSTER_TRIANGLES triangles
// over at most RT_CLUSTER_VERTICES vertices of the block) is sent once (rt_new_sets; its data in the second mapping,
// "<name>_clusters", RT_CLUSTER_DATA_BYTES). A soldier's triangle took three texels; its vertex now about one (4x unit
// size battles: the soldiers needed several times the frame's texels, ATN 2026-10-28).
constexpr uint32_t RT_COMPACT_DRAWS = 16384;
constexpr uint32_t RT_NEW_SETS = 512;
constexpr uint32_t RT_CLUSTER_DATA_BYTES = 16u << 20;
constexpr uint32_t RT_CLUSTER_TRIANGLES = 128;
constexpr uint32_t RT_CLUSTER_VERTICES = 128;
// The frame's dynamic triangles and clusters at most (every-frame captures and compact draws; a helper on a GPU of 28 GB
// or less takes 3/4 of the clusters, of 20 GB or less a quarter, of 12 GB or less an eighth).
constexpr uint32_t RT_DYNAMIC_TRIANGLES = 32u << 20;
constexpr uint32_t RT_DYNAMIC_CLUSTERS = 1u << 20;
// An every-frame capture's clusters: this many triangles each (the last partial).
constexpr uint32_t RT_CAPTURE_CLUSTER_TRIANGLES = 85;
// The frame's alpha-tested dynamic triangles' corners at most (their UVs: grass's and swaying leaves'); a helper on a GPU
// of 20 GB or less takes half, of 12 GB or less a quarter, and says how many (rt_dynamic_uvs). Doubled since grass was
// left out by the share (~70M grass vertices a frame over it, ATN 2026-10-29).
constexpr uint32_t RT_DYNAMIC_UVS = 32u << 20;
struct RtCompactDraw {
  uint64_t set = 0;  // its cluster set's key
  uint32_t first_vertex = 0;
  uint32_t instances = 0;
  // RT_TERRAIN, RT_FOLIAGE (alpha-tested), RT_THIN, RT_GRASS; RT_INSTANCED (v18): a placed draw's swaying copy, its
  // instances' rows right after its blocks.
  uint32_t flags = 0;
  uint32_t slice = 0;  // a foliage draw's atlas slice
};
// A new cluster set's data at `offset` in the second mapping: its clusters (four uint32 each: its first vertex in the
// block, its vertices, its first triangle in the set, its triangles), then its triangles (three bytes each: vertices of
// their cluster).
struct RtClusterSet {
  uint64_t key = 0;
  uint32_t vertices = 0;  // a block's
  uint32_t clusters = 0;
  uint32_t triangles = 0;
  uint32_t offset = 0;
};
// Thin (v11): cloth and sails; with RT_LEAF_LIGHT a share of the sun passes them as it passes leaves.
constexpr uint32_t RT_THIN = 16;
// Smoke (with RT_DYNAMIC | RT_FOLIAGE): particles as quads facing the sun, one radius toward it, each corner's UV (+-1)
// times (1 + the particle's opacity, its radius); the sun's rays pass them dimmed, nothing else meets them.
constexpr uint32_t RT_SMOKE = 32;
// Grass (with RT_FOLIAGE): its own instances (placed) and clusters (swaying) apart from the leaves'; the sun's and the
// ambient's rays meet it (a blade blocks the sun), the others (point lights, fog, Light Bounce, path tracing,
// reflections) pass it (most of a frame's rays crossed whole fields of it, ATN's 2026-10-29 profile).
constexpr uint32_t RT_GRASS = 64;
// Met by the sun's rays alone (with RT_DYNAMIC | RT_FOLIAGE | RT_GRASS): sprite soldiers and far trees, in the smoke's BLAS
// (among the units every ray crossed them: far trees took bounce from 5.5 to 11 ms, reflections from 1.9 to 3.8).
constexpr uint32_t RT_SUN_ONLY = 128;
// Its quads scattered over the map (far trees): a cluster each (85 triangles' boxes reached across whole tiles, trace 2.4 ->
// 6.2 ms).
constexpr uint32_t RT_SCATTERED = 256;
// A unit's or a rigid mesh's alpha-tested draw (with RT_FOLIAGE): where its test passes it blocks as a solid does (RECORD_GRASS), no
// light through it; a static one's instances are a group of their own (helper100: among the leaves before, counted as leaves).
constexpr uint32_t RT_SOLID_CUT_OUT = 512;
// Request flags.
constexpr uint32_t RT_SUN_SHADOWS = 1;
constexpr uint32_t RT_AMBIENT_OCCLUSION = 2;
// The ambient visibility at a quarter of the rays: one pixel of each 2 x 2 block traced (another each frame) for the block.
constexpr uint32_t RT_AMBIENT_QUARTER = 4;
// The local lights' shadows (v9): rt_lights into rt_light_mask.
constexpr uint32_t RT_LOCAL_LIGHTS = 8;
// Light through leaves (v11): leaves on the way to the sun let a share of it through each (else they block it).
constexpr uint32_t RT_LEAF_LIGHT = 16;
// Fog shadows (v11): the sun's mean visibility along each camera ray into rt_fog (a quarter of the textures' size each
// way, R8: the texel's centre at render pixel 4 x + 2).
constexpr uint32_t RT_FOG_SHADOWS = 32;
// The Debug View's scene test (v14): placed instances only where this frame's draws put them (none kept).
constexpr uint32_t RT_NO_KEPT = 64;
// Leaf micromaps: new leaves' opacity micromaps baked (NVIDIA's OMM SDK in the helper) and built with their BLAS.
constexpr uint32_t RT_MICROMAPS = 128;
// Ray Reconstruction denoises the frame: soft sun shadows one ray toward a point of the sun's disk instead of the rays'
// soft-shadow filter (the ambient keeps its history: RR alone left ~20% of the AO's noise).
constexpr uint32_t RT_RECONSTRUCTED = 256;
// Torch and fire light in fog (v18, with RT_FOG_SHADOWS): along each camera ray the local lights' light too (one light a
// point, picked by what it gives there; through a shadow ray the smoke dims) into rt_fog_lights.
constexpr uint32_t RT_FOG_LIGHTS = 512;
// The addon's ray tracing profile (Record Ray Tracing Profile): the helper writes each request's costs into
// tw_rt_profile_helper.csv next to it (older helpers ignore it).
constexpr uint32_t RT_PROFILE = 1024;
// GPU Micromap Baking (Experimental): new leaves' and grass sets' micromaps baked on the GPU by NVIDIA's OMM SDK in the
// trace's own commands (no read-back of their UVs and masks, no CPU bakes; helpers before 38 ignore it).
constexpr uint32_t RT_GPU_MICROMAPS = 2048;
// Off-Screen Sway off (v18): swaying placed draws' instances away from the view keep their placed (still) copies only.
constexpr uint32_t RT_SWAY_IN_VIEW = 4096;
// Fast Leaf Shadows (v18): the rays toward the sun, the ambient's and the lights' take micromapped leaves' unknown states
// as known (2-state: no alpha tests for them).
constexpr uint32_t RT_FAST_LEAVES = 8192;
// Importance-Sampled Torch Shadows (v18): two shadow rays a pixel toward the local lights, picked by what they give it
// (ReSTIR DI's candidates); the others keep their last bits.
constexpr uint32_t RT_LIGHT_IMPORTANCE = 16384;
// Coloured Shadows (v19, with RT_LEAF_LIGHT): the sun's light through cloth and sails takes their colour (where the
// screen shows them: Ray Reconstruction's diffuse albedo guide) into rt_tint.
constexpr uint32_t RT_COLOURED_SHADOWS = 32768;
// The drawn list (rt_drawn) holds the placed meshes drawn this frame too, also those not placed in it (helpers 58 on):
// a placed mesh the frame did not draw has its kept instances in view gone at once (the game drew another level of
// detail there; they darkened it for 16 frames).
constexpr uint32_t RT_DRAWN_PLACED = 65536;
// Log Details: the helper keeps its GPU times, status and log lines (none without), and its D3D12 device, made with this
// set (the flag is written then too), DRED's record of where a GPU reset stopped.
constexpr uint32_t RT_DETAILS = 131072;
// Smoke Shadow Quality (0-2, two bits): the particles a sun ray counts (rt_scene.hlsli).
constexpr uint32_t RT_SMOKE_QUALITY_SHIFT = 19;
// Denoiser NRD (without Ray Reconstruction; helpers without NRD or before 73 keep the built-in ones): soft sun shadows one
// ray toward a point of the sun's disk, denoised by NVIDIA's NRD (SIGMA) instead of the rays' soft-shadow filter; the
// ambient visibility denoised by its REBLUR instead of the helper's history (the addon then leaves its AO filter out).
constexpr uint32_t RT_NRD = 1u << 21;
// AO under Ray Reconstruction at REBLUR (v26, with RT_RECONSTRUCTED): the ambient visibility denoised by REBLUR instead of the
// history; the sun stays RR's. Helpers before 83 ignore it. (Bits 22, 23: the helper's to its shaders.)
constexpr uint32_t RT_AO_REBLUR = 1u << 24;
// Rays Start Past Hidden Shapes (v27): a pixel's rays start past what the scene holds between it and the camera (rt_camera.hlsli StartPast).
constexpr uint32_t RT_START_PAST = 1u << 25;
// Banners Without Self-Shadows (v29): cloth and sails don't shade themselves in the sun's rays (rt_trace.cs_6_5.hlsl SunRay). Helpers before 88 ignore it.
constexpr uint32_t RT_SHEETS_UNSELFED = 1u << 26;
// One Level of Detail Out of View (helpers before 98 ignore it): a kept placed instance goes when the frame placed another mesh where it
// stands and not it (rt_place.cs_6_5.hlsl PLACE_ONE_AT_A_PLACE): the game draws another level of detail of the thing, or its ruin.
constexpr uint32_t RT_ONE_AT_A_PLACE = 1u << 27;
// Steady Torch Shadows (Experimental; helpers before 106 ignore it): rt_lights traces every light a pixel reaches every frame (no
// turns, Importance-Sampled picks or kept bits: nothing changing frame to frame), hard from helper108 on (106 / 107: each soft ray
// to the same point of the light's disk, a fixed dither: black dots).
constexpr uint32_t RT_LIGHT_STEADY = 1u << 28;
// Torch and Fire Light in the Light Bounce (Experimental; helpers before 111 ignore it, with RT_PATH_LIGHTS_IN_BOUNCE the same
// frame): rt_lights writes the first 32 lights' bits dark untraced (the game's light shaders add none of their light) and the
// paths' ReSTIR GI reservoir takes those lights as candidates, their shadowed light in the bounce (rt_path.hlsli).
constexpr uint32_t RT_LIGHTS_IN_BOUNCE = 1u << 29;
// With RT_LIGHTS_IN_BOUNCE, Torch and Fire Shadows from the Light Bounce (Experimental; helpers before 111 ignore it, with
// RT_PATH_LIGHT_RATIO the frames before): the game keeps shading those lights; each pixel's mask word is a float, the last
// bounce's shadow ratio (shadowed over unshadowed torch light, the reservoir's light samples), for the light shaders to
// multiply when b9's w is 1 (rt_local_lights.hlsli). The addon sends at most 32 lights with it.
constexpr uint32_t RT_LIGHT_RATIO = 1u << 22;
// Path flags (v8).
constexpr uint32_t RT_PATH_TRACING = 1;
constexpr uint32_t RT_REFLECTIONS = 2;
constexpr uint32_t RT_SPECULAR_OCCLUSION = 4;
// Ray Reconstruction denoises the frame: the reflections' samples go out as traced (no accumulation, no filter).
constexpr uint32_t RT_REFLECTIONS_RAW = 8;
// The path-traced Light Bounce's samples reused over time and between neighbours (ReSTIR GI; helpers before 21 ignore it).
constexpr uint32_t RT_PATH_REUSE = 16;
// Ray Reconstruction denoises the frame (v17): ReSTIR GI takes last frame's reservoir from a neighbouring texel, a
// different one each frame (it takes the samples as independent: NVIDIA's DLSS-RR integration guide).
constexpr uint32_t RT_PATH_RECONSTRUCTED = 32;
// Path-Traced Ambient: the bounce texels' paths that escape bring the sky's light too (the addon takes the game's
// ambient away; helpers before 37 ignore it).
constexpr uint32_t RT_PATH_AMBIENT = 64;
// Path Tracing SER (v18, with the paths or the reflections): DXR 1.2's shader execution reordering for their rays.
constexpr uint32_t RT_PATH_SER = 128;
// SHaRC Radiance Cache (v18, with the paths): NVIDIA's SHaRC in place of the helper's own world radiance cache for what
// the screen does not know (the reflections keep the helper's).
constexpr uint32_t RT_PATH_SHARC = 256;
// Reflection Reuse from ReSTIR GI (v19, a reflections request's, with the paths' ReSTIR GI this frame): the reflections'
// rougher lobes resample the Light Bounce's reservoir at their texel with their own rays' light.
constexpr uint32_t RT_REFLECTIONS_REUSE = 512;
// Own ReSTIR (v19, a reflections request's; helpers before 55 ignore it): the reflections' rougher lobes resample their
// own reservoirs (half resolution, reused over frames and between neighbours) with their own rays' light.
constexpr uint32_t RT_REFLECTIONS_OWN = 1024;
// A reflections request's: `rt_light` holds the light buffer as the reflection pass reads it, at the render pixels.
constexpr uint32_t RT_REFLECTIONS_LIGHT = 2048;
// Own Sky Rays (with RT_PATH_AMBIENT; older helpers ignore it): Path-Traced AO's sky past ReSTIR GI's reservoirs, the
// plain mean of each texel's rays.
constexpr uint32_t RT_PATH_SKY_APART = 4096;
// Denoiser NRD (a Light Bounce's or reflections request's; helpers without NRD or before 73 ignore it): NVIDIA's REBLUR on
// its light (the addon then leaves its Light Bounce denoiser out, or its reflections' SSR denoiser).
constexpr uint32_t RT_PATH_NRD = 8192;
// Reflections: GGX with alpha = roughness^2 (WH3's own BRDF: Schlick's k = roughness^2 / 2), not Attila's narrow lobe.
constexpr uint32_t RT_REFLECTIONS_SQUARED_LOBE = 16384;
// With RT_PATH_AMBIENT (v27): the light standing in for the game's ambient from the game's cube, not the one learned before it.
constexpr uint32_t RT_PATH_KEPT_CUBE = 32768;
// A Light Bounce request's without path tracing (v28; reflections after it): the paths' history, radiance cache and learned
// lighting from its prepared light, before the water's positions and guides replace the scene's, as path tracing does.
constexpr uint32_t RT_PATH_CACHE_FILL = 65536;
// The colour (`color`, last frame's when the paths read it) holds the light's square root (DLSS L, M, Ray Reconstruction).
constexpr uint32_t RT_PATH_COLOR_SQUARE_ROOT = 131072;
// Fog in Reflections (a reflections request's; helpers before 97 ignore it): the game's fog between a pixel and what its rays
// met, by the numbers the addon wrote into the sun texture's texels 9-13 (rt_leg_fog.hlsli; texel 9's w 0: none).
constexpr uint32_t RT_REFLECTIONS_FOG = 262144;
// Steadier Water Reflections (a reflections request's; helpers before 105 ignore it): a mirror's reflection (one ray, no
// history, no filter: calm water) within the range its neighbours on the same surface show.
constexpr uint32_t RT_REFLECTIONS_STEADY = 524288;
// Softer Water Reflections (a reflections request's; helpers before 105 ignore it): the positions hold the water's surface,
// written since the frame's first paths request took them (the riverbed's): the reflections' history from last frame's at
// this request.
constexpr uint32_t RT_REFLECTIONS_WATER_POSITIONS = 1048576;
// With RT_PATH_AMBIENT (helpers before 107 ignore it): the sun texture's texel 2 holds the game's sun light (its sun colour
// at its diffuse scale, rt_sun.cs_5_0.hlsl): the light a path's hit on the screen sends back is its base colour lit by that
// and the paths' own sky, as an unseen one's (not the screen's light, with the game's specular toward the camera), and the
// light learned from the screen stays as learned.
constexpr uint32_t RT_PATH_SUN_LIGHT = 2097152;
// A Light Bounce request's with path tracing and RT_PATH_REUSE (helpers before 111 ignore it): the trace's RT_LIGHTS_IN_BOUNCE
// lights as ReSTIR GI candidates.
constexpr uint32_t RT_PATH_LIGHTS_IN_BOUNCE = 1u << 22;
// With RT_PATH_LIGHTS_IN_BOUNCE (the trace's RT_LIGHT_RATIO): the lights' samples left out of the bounce (the game shades
// them), their shadow ratio kept for the next trace.
constexpr uint32_t RT_PATH_LIGHT_RATIO = 1u << 23;
constexpr uint32_t RT_NRD_OFF_AMBIENT = 1;
constexpr uint32_t RT_NRD_OFF_BOUNCE = 2;
constexpr uint32_t RT_NRD_OFF_REFLECTIONS = 4;
// The upscaler (v22): NVIDIA's DLSS and Ray Reconstruction, AMD's FSR or Intel's XeSS (dlaa_helper/upscale_d3d12.hpp).
constexpr uint32_t UPSCALER_DLSS = 0;
constexpr uint32_t UPSCALER_FSR = 1;
constexpr uint32_t UPSCALER_XESS = 2;
// Shared::fg_backend, fg_running. Auto is the helper's choice: DLSS on NVIDIA's GPUs that have it, XeSS on Intel's, else FSR.
constexpr uint32_t FG_BACKEND_DLSS = 0;
constexpr uint32_t FG_BACKEND_FSR = 1;
constexpr uint32_t FG_BACKEND_XESS = 2;
constexpr uint32_t FG_BACKEND_AUTO = 3;

// Ray Reconstruction's optional inputs, as bits of Prepare's `rr_extra` and `rr_extra_written`, as places in `rr_extra`.
// RGBA16F: the particle layer (rgb premultiplied, alpha the blend) and the colour before particles (as the colour input).
// R16F: the depth of field guide.
enum RrExtraInput : uint32_t { RR_EXTRA_PARTICLE_LAYER, RR_EXTRA_COLOR_BEFORE, RR_EXTRA_DOF_GUIDE, RR_EXTRA_INPUTS };

struct Shared {
  uint32_t version = VERSION;
  // The addon's.
  uint32_t generation = 0;  // a new set of textures to open (size changed)
  uint64_t color = 0;       // RGBA16F: this frame, linear (in)
  uint64_t depth = 0;       // R32F: the scene's depth, 0 near (in)
  uint64_t motion = 0;      // RG16F: pixels to where each pixel was last frame (in)
  uint64_t output = 0;      // RGBA16F (out)
  uint64_t fence = 0;       // the inputs are written at `value`; the helper signals `value` + 1
  uint64_t value = 0;
  uint32_t width = 0;   // the input's active size (its top-left; DLSS: the render size)
  uint32_t height = 0;
  float jitter_x = 0.f;  // pixels, x right, y down
  float jitter_y = 0.f;
  uint32_t reset = 1;    // 1: no history (first frame, a cut)
  uint32_t preset = 0;   // NVSDK_NGX_DLSS_Hint_Render_Preset
  uint32_t adapter_luid_low = 0;
  int32_t adapter_luid_high = 0;
  // The helper's.
  uint32_t ready = 0;       // 1: its device is up
  uint32_t evaluated = 0;   // 1: the last request ran
  uint64_t done_value = 0;  // the last request it answered
  char status[256] = {};
  // The addon's (v2): the output's size (the textures'); above `width` x `height`: DLSS upscales to it.
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  // The addon's (v3): Ray Reconstruction (NVIDIA's denoising upscaler in place of DLAA / DLSS) when `ray_reconstruction`
  // is 1: its guides at the input's size (made with the other textures, opened with them: diffuse and specular albedo
  // RGBA8, world normals RGBA16F, roughness R8), its preset, and the camera (row vectors; view x right, y up, z forward).
  uint64_t diffuse_albedo = 0;
  uint64_t specular_albedo = 0;
  uint64_t normals = 0;
  uint64_t roughness = 0;
  uint32_t ray_reconstruction = 0;
  uint32_t ray_reconstruction_preset = 0;
  float world_to_view[16] = {};
  float view_to_clip[16] = {};
  // The addon's (v4): ray tracing in the helper (DXR on its own D3D12 device), a request of its own (`rt_request` 1, no
  // DLAA): its shared textures (opened with the others; 0: off), at the textures' size with the active `rt_width` x
  // `rt_height`: world positions (RGBA32F: x, y, z metres, view depth; 0: sky), the sun (RGBA32F 2 x 1: toward the sun,
  // w 1 valid; the camera) in, the sun's visibility and the ambient visibility (R8) out; the vertex texture (RGBA32F,
  // RT_TRANSFER_WIDTH x RT_TRANSFER_HEIGHT). The frame's new captures and the meshes it drew (both by key).
  uint64_t rt_vertices = 0;
  uint64_t rt_positions = 0;
  uint64_t rt_sun = 0;
  uint64_t rt_visibility = 0;
  uint64_t rt_ambient = 0;
  uint32_t rt_request = 0;
  uint32_t rt_flags = 0;  // RT_SUN_SHADOWS, RT_AMBIENT_OCCLUSION, RT_AMBIENT_QUARTER
  float rt_ambient_range = 0.f;  // metres
  uint32_t rt_ambient_rays = 0;
  uint32_t rt_frame = 0;
  uint32_t rt_width = 0;
  uint32_t rt_height = 0;
  uint32_t rt_capture_count = 0;
  uint32_t rt_drawn_count = 0;
  uint32_t rt_padding = 0;
  RtCapture rt_captures[RT_CAPTURES] = {};
  uint64_t rt_drawn[RT_DRAWN] = {};
  // The helper's (v4): what the last trace found (a one-line status).
  char rt_status[256] = {};
  // The addon's (v5): frame generation (NVIDIA's DLSS Frame Generation through Streamline in the helper, presenting
  // into a child window over the game's: dlaa_helper/fg_d3d12.hpp), a request of its own at each present
  // (`fg_request` 1): the game's window, the frame as it is presented and the same frame before the UI (the HUD-less
  // image), both in `fg_format` (the swap chain's; HDR10 for an scRGB one: frame_generation.hpp) at the textures' size
  // (opened with the others: 0, none); the
  // depth and motion are the frame's DLAA inputs (`fg_inputs` 1: written this frame, their top-left `width` x `height`,
  // `jitter_x` / `jitter_y`, `reset`) and its camera (row vectors, without the jitter: view to clip, this frame's and
  // last frame's view-projection; position, right, up, forward; near and far, metres).
  uint64_t fg_window = 0;
  uint64_t fg_frame = 0;
  uint64_t fg_hudless = 0;
  uint32_t fg_format = 0;  // DXGI_FORMAT
  uint32_t fg_request = 0;
  uint32_t fg_multiplier = 0;  // frames presented per rendered frame (2-6; 1: Dynamic); 0: off, the window hidden
  uint32_t fg_inputs = 0;
  uint32_t fg_hudless_valid = 0;
  uint32_t fg_reflex_boost = 0;
  uint32_t fg_frame_index = 0;  // counts up by one each present
  uint32_t fg_frame_limit = 0;  // Reflex's frame rate limit, rendered frames per second (0: none; older addons 0)
  float fg_view_to_clip[16] = {};
  float fg_view_projection[16] = {};
  float fg_previous_view_projection[16] = {};
  float fg_camera[12] = {};
  float fg_near = 0.f;
  float fg_far = 0.f;
  // The helper's (v5): 1 when frame generation cannot run here (the addon stops asking), and its state (one line).
  uint32_t fg_failed = 0;
  // The addon's (v21, in v5's padding): 1 the frames are SDR (gamma 2.2), in HDR10's format all the same.
  uint32_t fg_sdr = 0;
  char fg_status[256] = {};
  // The addon's (v6): soft shadows (the sun's disk, degrees; 0 hard) and the leaves' opacity masks (rt_atlas: the R8
  // texture array, shared again with every set of textures; 0: none) with each capture's slice in it.
  float rt_sun_softness = 0.f;
  // The addon's (v18, in v6's padding): Grass Shadow Range: pixels farther (view depth, metres) trace without grass (0:
  // every pixel with it).
  float rt_grass_shadow_range = 0.f;
  uint64_t rt_atlas = 0;
  uint32_t rt_capture_slices[RT_CAPTURES] = {};
  // The addon's (v7): the ray-traced Light Bounce (light_bounce.hpp), a request of its own after the frame's trace
  // (`rt_request` 2, its scene): the prepared light (R11G11B10F, a texel for each `rt_bounce_scale` x `rt_bounce_scale`
  // render pixels, in its top-left `rt_bounce_width` x `rt_bounce_height`) in, the bounce (RGBA16F: the light arriving,
  // the texel's distance from the camera; 0 the sky) out, both at the textures' size (opened with the others; 0: off);
  // this frame's view-projection (the camera's registers: c + j column j of a row-vector matrix) to find where a ray's hit
  // is on screen, the rays per texel and their reach (metres).
  uint64_t rt_light = 0;
  uint64_t rt_bounce = 0;
  float rt_view_projection[16] = {};
  uint32_t rt_bounce_width = 0;
  uint32_t rt_bounce_height = 0;
  uint32_t rt_bounce_scale = 0;
  uint32_t rt_bounce_rays = 0;
  float rt_bounce_range = 0.f;
  uint32_t rt_v7_padding = 0;
  // The addon's (v8): path tracing, reflections and specular occlusion (dlaa_helper/rt_path.hlsli), RT_PATH_* in
  // `rt_path_flags` (0: v7's Light Bounce trace). With RT_PATH_TRACING the Light Bounce request (2) path traces its
  // texels (`rt_path_bounces` surfaces past the first, `rt_path_samples` paths a texel). A request of its own
  // (`rt_request` 3) traces the reflections (GGX samples of each render pixel's lobe, `rt_path_samples` of them) and the
  // specular occlusion (the share of the lobe open within `rt_reflection_range` metres) into `rt_reflection` (RGBA16F:
  // reflected light, the share of samples that met a surface) and `rt_specular` (R8: 1 open), both at the textures'
  // size (opened with the others; 0: off). Both read the Ray Reconstruction guides (`rt_guides` 1: written this frame)
  // and the DLAA input `color` (it still holds last frame's scene colour: hits last frame saw take theirs); the helper
  // keeps a world radiance cache and the learned lighting for what the screen never saw.
  uint64_t rt_reflection = 0;
  uint64_t rt_specular = 0;
  uint32_t rt_path_flags = 0;
  uint32_t rt_path_bounces = 0;
  uint32_t rt_path_samples = 0;
  float rt_reflection_range = 0.f;
  uint32_t rt_guides = 0;
  uint32_t rt_v8_padding = 0;
  // The addon's (v9): the local lights' shadows with RT_LOCAL_LIGHTS in a trace request's flags: `rt_light_count` lights
  // (world position, reach in metres), a ray from each render pixel within a light's reach toward it; out in
  // `rt_light_mask` (R32_UINT at the textures' size, opened with the others; bit i: light i reaches the pixel unblocked).
  float rt_lights[RT_MAX_LIGHTS][4] = {};
  uint32_t rt_light_count = 0;
  uint32_t rt_v9_padding = 0;
  uint64_t rt_light_mask = 0;
  // The addon's (v11): the local lights' colours (as rt_lights), which light what the paths meet unseen by the screen.
  float rt_light_colours[RT_MAX_LIGHTS][4] = {};
  uint64_t rt_fog = 0;  // RT_FOG_SHADOWS' output
  // The addon's (v12): frame generation's preset (NVIDIA's A-C as 1-3, written into the driver profile of the helper's
  // exe, which DLSS-G reads when the helper starts it; 0: the driver's own) and the HUD's recomposition (1: the frame's
  // UI, where it differs from the HUD-less image, laid over the generated frames instead of warped with the scene).
  uint32_t fg_preset = 0;
  uint32_t fg_ui = 0;
  // The addon's (v13): frame generation's debug view (1: the HUD-less image presented, without a UI layer; 2: the UI
  // layer tinted magenta over the frame; 0: off).
  uint32_t fg_debug = 0;
  // The helper's (in v13's padding; a helper before 102 leaves it 0: unknown): what its frame generation backend last really did.
  // Bits 0-7: frames presented per rendered frame (0: none generated); bits 8-15: the most it presents here (FSR 2, XeSS and
  // DLSS by the GPU and driver; 0: not known yet).
  uint32_t fg_presented = 0;
  // The addon's (v14): a trace request's placed draws of known BLASes (RT_INSTANCED's every frame they are drawn).
  uint32_t rt_placement_count = 0;
  // The addon's (v18, in v14's padding): how far from the camera along the view swaying placed draws' instances keep
  // their swaying copies (metres; 0: everywhere); past it their placed (still) copies are traced.
  float rt_sway_range = 0.f;
  RtPlacement rt_placements[RT_PLACEMENTS] = {};
  // The helper's (v15): its ray tracing devices so far, one more each time a GPU fault or hang removed the last (the new
  // one's scene is empty: the addon captures every mesh again); the triangles it left out so far (far off or stretched
  // past any part's size: garbage, which spread the scene's bounds until every ray met every instance) and the last
  // capture that had some (its key).
  uint32_t rt_device = 0;
  uint32_t rt_left_out = 0;
  uint64_t rt_left_out_key = 0;
  // The addon's (v16): a trace request's compact draws and the cluster sets new to the helper (their data in the second
  // mapping); the helper's: nonzero when it takes compact draws, one higher each time its full set pool dropped sets
  // (the addon sends the sets it draws again).
  uint32_t rt_compact_count = 0;
  uint32_t rt_new_set_count = 0;
  RtCompactDraw rt_compact[RT_COMPACT_DRAWS] = {};
  RtClusterSet rt_new_sets[RT_NEW_SETS] = {};
  uint32_t rt_compact_ok = 0;
  uint32_t rt_dynamic_uvs = 0;  // the helper's alpha-tested corners a request (by its GPU's memory; 0: RT_DYNAMIC_UVS / 2)
  // The addon's (v17): Ray Reconstruction's specular hit distance (R16F at the textures' size, made with the reflections'
  // textures, opened with the others; 0: none), which a reflections request with RT_REFLECTIONS_RAW writes (metres to
  // what each pixel's reflection rays met; 65504, NVIDIA's value for the sky, where none met or none were traced). An
  // evaluation with `rr_hit_distance_written` 1 (written this frame) gives it Ray Reconstruction.
  uint64_t rr_hit_distance = 0;
  uint32_t rr_hit_distance_written = 0;
  uint32_t rr_v17_padding = 0;
  // The addon's (v18): the local lights' radius in metres for soft shadows (with Ray Reconstruction each frame a ray to
  // a point of the light's disk; 0: hard), the rays a pixel toward the sun and each light (Ray Tracing at Output
  // Resolution below 100% render scale: the output pixels a render pixel stands for; 0 or 1: one).
  float rt_light_size = 0.f;
  uint32_t rt_shadow_rays = 0;
  uint64_t rt_fog_lights = 0;  // RT_FOG_LIGHTS' output (RGBA16F at rt_fog's size)
  // The addon's (v19): RT_COLOURED_SHADOWS' output (RGBA8 at rt_visibility's size: 1 - the sun light's colour filter, 0
  // none), which a trace with it writes every pixel.
  uint64_t rt_tint = 0;
  // The addon's (v22): which upscaler serves this set of textures (UPSCALER_*; a change makes a new set), which frame
  // generation backend (FG_BACKEND_*, v23: a change makes a new set; before it Streamline's). FSR and XeSS read `view_to_clip`
  // of every evaluation.
  uint32_t upscaler = 0;
  uint32_t fg_backend = 0;
  // The helper's (v23): the frame generation backend that serves this set (Auto's choice).
  uint32_t fg_running = 0;
  // The helper's (in v23's padding, an older helper leaves it 0): the signals asked for NRD that it could not run on
  // (RT_NRD_OFF_*): their built-in ways instead, the addon's own denoisers stay on them.
  uint32_t rt_nrd_off = 0;
  // v24: how many frames a placed mesh's undrawn instances stay and its BLAS is held, for one trace (0: RECENT_FRAMES and
  // RELEASE_FRAMES, the games without Host::keep_placed; 1 with a new scene: the old one's go at once).
  uint32_t rt_kept_frames = 0;
  // v25: Ray Reconstruction's optional inputs, by RR_EXTRA_* (shared at the textures' size, made only when the addon asks; 0:
  // none), and those written for this evaluation (the rest go to it as null).
  uint64_t rr_extra[RR_EXTRA_INPUTS] = {};
  uint32_t rr_extra_written = 0;
  // The helper's (in v25's padding; a helper before 108 leaves it 0): 1 when its last evaluation was asked for Ray
  // Reconstruction and ran DLSS without it (RR couldn't start: no nvngx_dlssd.dll next to nvngx_dlss.dll), so nothing
  // denoises what the rays leave for RR.
  uint32_t rr_refused = 0;
  // The helper's (v30): the set it has open (`generation` as it opened it) and that set's fence as it last read it (at
  // each request and each second without one; all ones: none to read). The addon's watcher follows the helper's GPU work
  // by these (dlaa_bridge.hpp): it has no device of its own in the game.
  uint32_t fence_generation = 0;
  uint32_t fence_v30_padding = 0;
  uint64_t fence_completed = 0;
  // The addon's (v31): the local lights past the first RT_MAX_LIGHTS (rt_light_count up to RT_ALL_LIGHTS), and the lit
  // mask's first tile row (its set's: the textures' height; 0: no tiles, a bit a light as before v31).
  float rt_lights_more[RT_ALL_LIGHTS - RT_MAX_LIGHTS][4] = {};
  float rt_light_colours_more[RT_ALL_LIGHTS - RT_MAX_LIGHTS][4] = {};
  uint32_t rt_light_tile_row = 0;
  // The addon's (in v31's padding; an older v31 addon leaves it 0: none): Grass Ray Tracing Range, placed grass whose
  // instance lies farther along the view (metres) out of the scene, its still and swaying copies alike.
  float rt_grass_range = 0.f;
};

// The block's size for a version (an older addon's ends earlier).
constexpr size_t SharedBytes(uint32_t version) {
  if (version >= 31) return sizeof(Shared);
  if (version == 30) return offsetof(Shared, rt_lights_more);
  if (version >= 25) return offsetof(Shared, fence_generation);
  if (version == 24) return offsetof(Shared, rr_extra);
  if (version == 23) return offsetof(Shared, rt_kept_frames);
  if (version == 22) return offsetof(Shared, fg_running);
  if (version >= 19) return offsetof(Shared, upscaler);
  if (version == 18) return offsetof(Shared, rt_tint);
  if (version == 17) return offsetof(Shared, rt_light_size);
  if (version == 16) return offsetof(Shared, rr_hit_distance);
  if (version == 15) return offsetof(Shared, rt_compact_count);
  if (version == 14) return offsetof(Shared, rt_device);
  if (version == 13) return offsetof(Shared, rt_placement_count);
  if (version == 12) return offsetof(Shared, fg_debug);
  if (version == 11) return offsetof(Shared, fg_preset);
  if (version >= 9) return offsetof(Shared, rt_light_colours);
  if (version == 8) return offsetof(Shared, rt_lights);
  if (version == 7) return offsetof(Shared, rt_reflection);
  if (version == 6) return offsetof(Shared, rt_light);
  if (version == 5) return offsetof(Shared, rt_sun_softness);
  if (version == 4) return offsetof(Shared, fg_window);
  return version == 3 ? offsetof(Shared, rt_vertices) : offsetof(Shared, diffuse_albedo);
}

}  // namespace twattila::dlaa_bridge
