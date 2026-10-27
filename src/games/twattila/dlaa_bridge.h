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

constexpr uint32_t VERSION = 10;
// The helper still answers addons of this version (their block ends before v3's fields).
constexpr uint32_t OLDEST_VERSION = 2;

// Ray tracing (v4, rt_scene.hpp / dlaa_helper/rt_d3d12.hpp): the captured triangles' world positions, RT_TRANSFER_WIDTH
// per row of the shared vertex texture (RT_TRANSFER_HEIGHT rows); new captures and drawn meshes a frame at most.
constexpr uint32_t RT_TRANSFER_WIDTH = 4096;
constexpr uint32_t RT_TRANSFER_HEIGHT = 1024;
constexpr uint32_t RT_CAPTURES = 256;
constexpr uint32_t RT_DRAWN = 8192;
// The leaves' opacity masks (v6): an R8 texture array of RT_ATLAS_SLICES slices RT_ATLAS_SIZE square.
constexpr uint32_t RT_ATLAS_SIZE = 512;
constexpr uint32_t RT_ATLAS_SLICES = 256;
// Local lights' shadows (v9): at most this many lights a request (a bit each in the lit mask).
constexpr uint32_t RT_MAX_LIGHTS = 32;

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
// instance (each instance's rows times the first one's inverse).
constexpr uint32_t RT_INSTANCED = 8;
// Request flags.
constexpr uint32_t RT_SUN_SHADOWS = 1;
constexpr uint32_t RT_AMBIENT_OCCLUSION = 2;
// The ambient visibility at a quarter of the rays: one pixel of each 2 x 2 block traced (another each frame) for the block.
constexpr uint32_t RT_AMBIENT_QUARTER = 4;
// The local lights' shadows (v9): rt_lights into rt_light_mask.
constexpr uint32_t RT_LOCAL_LIGHTS = 8;
// Path flags (v8).
constexpr uint32_t RT_PATH_TRACING = 1;
constexpr uint32_t RT_REFLECTIONS = 2;
constexpr uint32_t RT_SPECULAR_OCCLUSION = 4;
// Ray Reconstruction denoises the frame: the reflections' samples go out as traced (no accumulation, no filter).
constexpr uint32_t RT_REFLECTIONS_RAW = 8;
// The path-traced Light Bounce's samples reused over time and between neighbours (ReSTIR GI; helpers before 21 ignore it).
constexpr uint32_t RT_PATH_REUSE = 16;

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
  // image), both in the swap chain's format (`fg_format`) at the textures' size (opened with the others: 0, none); the
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
  uint32_t fg_helper_padding = 0;
  char fg_status[256] = {};
  // The addon's (v6): soft shadows (the sun's disk, degrees; 0 hard) and the leaves' opacity masks (rt_atlas: the R8
  // texture array, shared again with every set of textures; 0: none) with each capture's slice in it.
  float rt_sun_softness = 0.f;
  uint32_t rt_v6_padding = 0;
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
};

// The block's size for a version (an older addon's ends earlier).
constexpr size_t SharedBytes(uint32_t version) {
  if (version >= 9) return sizeof(Shared);
  if (version == 8) return offsetof(Shared, rt_lights);
  if (version == 7) return offsetof(Shared, rt_reflection);
  if (version == 6) return offsetof(Shared, rt_light);
  if (version == 5) return offsetof(Shared, rt_sun_softness);
  if (version == 4) return offsetof(Shared, fg_window);
  return version == 3 ? offsetof(Shared, rt_vertices) : offsetof(Shared, diffuse_albedo);
}

}  // namespace twattila::dlaa_bridge
