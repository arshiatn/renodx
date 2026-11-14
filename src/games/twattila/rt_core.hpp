/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/state.hpp"
#include "./dlaa_bridge.hpp"
#include "./dxbc.hpp"
#include "./frame_history.hpp"
#include "./frame_trace.hpp"

// Ray tracing through the DLAA helper, the part any D3D11 game can use (rt_scene.hpp adds the Total War games' passes and
// settings; Ryse, Expeditions and Shogun 2 use this alone): DXR runs in the 64-bit helper (dlaa_bridge.h v4-v8,
// dlaa_helper/rt_d3d12.hpp), which keeps the scene and traces it. The scene is the game's own geometry as its main view
// draws it: each draw of a scene shader not yet in it (and every dynamic draw each frame, up to MAX_DYNAMIC_VERTICES:
// the helper builds them into that frame's scene only; bridge v16: indexed ones as their meshes' vertices once each, the
// helper given each mesh's cluster set once) is drawn once more into a stream-output buffer, through its
// capture version (the game's code giving the world position: the TW games' rt_capture_*.vs) or the game's own shader
// (its SV_Position, made world positions with the frame's camera), keyed by its buffers, ranges, textures and constant
// buffers (but the camera's); the helper keeps a BLAS per key and builds the frame's TLAS from what it drew. The trace
// (Trace, at a pass with the main view's depth and camera bound): the captures into the shared vertex texture
// (rt_vertices), each pixel's world position (rt_positions, with the camera's position), the request; back come the
// sun's and the ambient visibility, and on request the Light Bounce's light (path traced with bridge v8), reflections and
// specular occlusion. The sun's direction: the game's (WriteSun, or rt_sun from a lighting buffer). The game's view
// (camera, render size, guides) comes from the addon's anti-aliasing through a Host. D3D11.
namespace twattila::rt_scene {

using Microsoft::WRL::ComPtr;

// A local light a light draw had: world position, reach (metres); its colour (rgb). `shadows_only`: its colour only ranks
// it, the paths, the fog and the glow take none of its light (a spot light, lit all around there; its cone is its shader's).
struct LocalLight {
  std::array<float, 4> light = {};
  std::array<float, 4> colour = {};
  bool shadows_only = false;
};

struct Settings {
  bool sun_shadows = false;
  bool ambient_occlusion = false;
  float ambient_range = 2.f;  // metres
  uint32_t ambient_rays = 2;
  bool ambient_quarter = false;  // a quarter of the rays (one pixel of each 2 x 2 traced a frame, in turn)
  bool ambient_alone = false;    // A game's combine (RT_COMBINE_SUN_MAPPING): the rays in place of its AO, not on top
  float sun_softness = 0.5f;  // degrees of the sun's disk (0: hard shadows)
  float light_size = 0.f;     // the local lights' radius (metres) for soft shadows under Ray Reconstruction (bridge v18)
  // Rays a pixel toward the sun and each light (bridge v18): below 100% render scale with Ray Tracing at Output
  // Resolution the output pixels a render pixel stands for (the ambient's and the reflections' rays likewise).
  uint32_t shadow_rays = 1;
  float light_glow = 1.f;  // torch and fire light in fog (RT_FOG_LIGHTS) and on the particles near them (b9's y)
  // The Light Bounce's light found with rays (light_bounce.hpp's trace replaced; it needs the Light Bounce on).
  bool light_bounce = false;
  uint32_t light_bounce_rays = 4;
  float light_bounce_range = 10.f;  // metres
  float light_bounce_resolution = 1.f;  // its texels at 1 / 2^this of the render size
  // Path tracing (bridge v8, dlaa_helper/rt_path.hlsli): the ray-traced Light Bounce's rays become paths going on past
  // what the screen sees (`path_bounces` surfaces past the first), shaded from a world radiance cache and the game's
  // lighting learned from the screen; the reflections' paths too.
  bool path_tracing = false;
  uint32_t path_bounces = 2;
  // Path-Traced Ambient (with path_tracing): the paths bring the sky's light, the sun light draws lose the game's
  // ambient cube (rt_scene.hpp OnPathAmbientDraw), the ray-traced AO is off.
  bool path_ambient = false;
  bool path_sky_apart = false;  // its sky averaged from each texel's rays, past ReSTIR GI's reservoirs
  bool path_kept_cube = false;  // dlaa_bridge.h RT_PATH_KEPT_CUBE
  // Its sky from the game's environment maps as the game's light probes see them, where its Global Illumination is on
  // (Warhammer III's rt_sun; the sun draws' SunDraw::environment_slot).
  bool path_probe_sky = false;
  // Ray-traced reflections and specular occlusion (bridge v8; the game's reflection pass reads them: Reflections()).
  bool reflections = false;
  bool specular_occlusion = false;
  // The water's reflections from its own depth and normal (the reflection pass's; else it keeps the screen-space march).
  bool water_reflections = false;
  uint32_t reflection_samples = 1;
  // Reflection Reuse (bridge v19, with path tracing): the reflections' rougher lobes resample ReSTIR GI's reservoirs.
  bool reflection_reuse = false;
  // Own ReSTIR (bridge v19, helpers 55 on): their own reservoirs instead.
  bool reflection_own = false;
  // Steadier Water Reflections (RT_REFLECTIONS_STEADY): a mirror's within the range its neighbours show.
  bool water_steady = false;
  // Softer Water Reflections: the water's guide roughness at least rt_scene's WATER_SOFT_ROUGHNESS (the anti-aliasing's
  // guides, Configure), its reflections' history from its own positions (RT_REFLECTIONS_WATER_POSITIONS).
  bool water_soft = false;
  // RT_REFLECTIONS_SQUARED_LOBE.
  bool reflection_squared_lobe = false;
  bool reflection_fog = false;  // RT_REFLECTIONS_FOG (the host writes the fog: rt_leg_fog.hlsli)
  // The local lights' shadows (bridge v9): of the lights the game's light draws had (AddLocalLight) the last frame's
  // nearest and brightest go with the trace; their lit bits and positions for the light draws: BindLocalLights. At most
  // `light_count` of them (v31: up to RT_ALL_LIGHTS, each 8 x 8 tile its own RT_MAX_LIGHTS), those within `light_range`
  // metres of the camera (0: any distance).
  bool local_lights = false;
  uint32_t light_count = dlaa_bridge::RT_MAX_LIGHTS;
  float light_range = 0.f;
  // Light through leaves (bridge v11): the sun's rays pass each leaf at a share (else leaves block it).
  bool leaf_light = false;
  // Coloured Shadows (bridge v19, with leaf_light): the sun's light through cloth and sails takes their colour (the sun
  // light draws' t50: Occlusion).
  bool coloured_shadows = false;
  // Placed grass (Attila) sways within its share (its swaying copies traced; off: all of it placed still).
  bool grass_sway = true;
  // Swaying Shadows Range (Experimental): swaying placed instances keep their swaying copies within these metres of the
  // camera (0: everywhere), placed still past them; Off-Screen Sway (Experimental) off: also only near the view.
  float sway_range = 0.f;
  bool sway_off_screen = true;
  // Fast Leaf Shadows (Experimental, with Leaf Micromaps): micromapped leaves without alpha tests (2-state); Grass Shadow
  // Range (Experimental): pixels farther than these metres trace without grass (0: every pixel with it); Grass Ray Tracing
  // Range (Experimental): placed grass farther than these metres is out of the scene (0: all of it in).
  bool fast_leaves = false;
  float grass_shadow_range = 0.f;
  float grass_range = 0.f;
  // Path Tracing SER (Experimental): the paths' and reflections' rays with DXR 1.2's shader execution reordering.
  bool path_ser = false;
  // SHaRC Radiance Cache (Experimental, with Path Tracing): NVIDIA's SHaRC for what the screen does not know.
  bool sharc = false;
  // Lean ReSTIR Reuse (Experimental, with Path Tracing; helper112, bridge RT_PATH_REUSE_LEAN): ReSTIR GI's reuse with fewer fetches.
  bool reuse_lean = false;
  // Importance-Sampled Torch Shadows (Experimental): two shadow rays a pixel toward the local lights, picked by their light.
  bool light_importance = false;
  // Steady Torch Shadows (Experimental): every light a pixel reaches traced every frame, each soft ray to the same disk point.
  bool light_steady = false;
  // With path-traced Light Bounce (helper111, bridge RT_LIGHTS_IN_BOUNCE): Torch and Fire Light in the Light Bounce (the
  // first 32 lights' light and shadow from ReSTIR GI's reuse, one light a bounce texel, the game's light shaders adding none of it), or
  // with light_ratio Torch and Fire Shadows from the Light Bounce (the game's light shaders keep their light, its shadow the
  // bounce's ratio; at most 32 lights). Both Experimental, off.
  bool lights_in_bounce = false;
  bool light_ratio = false;
  // Fog shadows (bridge v11): the games' fog loses the sun's light where the camera's ray runs through shadow
  // (BindFogShadows for their fog pass).
  bool fog_shadows = false;
  // Fog shadows as the only ray-traced effect pay for nothing else (Warhammer III; false: as before): no Ray Reconstruction (no
  // ray-traced light on surfaces to denoise: DLSS's own model, rt_scene.hpp Configure) and the terrain's count a frame late
  // (Settings::terrain_count_later's way: the fog's shadow of a hill a frame or two short after a camera cut, never a wait).
  bool cheap_fog_alone = false;
  // Leaf micromaps: the helper bakes new leaves' opacity micromaps (NVIDIA's OMM SDK): fewer alpha tests per ray.
  bool micromaps = false;
  // GPU Micromap Baking: on the GPU (the SDK's GPU baker), not the helper's CPU threads.
  bool gpu_micromaps = false;
  // Smoke shadows: the lit particles drawn since the last trace (InstallSmokeShadows' shaders: smoke and dust) dim the
  // ray-traced sun, a frame late (CaptureParticles, rt_smoke).
  bool smoke_shadows = false;
  // Smoke Shadow Quality (dlaa_bridge.h RT_SMOKE_QUALITY_SHIFT): 3, Ultra: High's rays, more room; 4 and 5, Extreme and
  // Maximum: twice the rays, room and particles twice and four times Ultra's.
  uint32_t smoke_quality = 1;
  bool nrd = false;            // Ray Tracing Denoiser: NRD without Ray Reconstruction (dlaa_bridge.h RT_NRD)
  bool ao_reblur = false;      // AO under Ray Reconstruction: REBLUR, not the history (dlaa_bridge.h RT_AO_REBLUR)
  bool start_past = false;     // dlaa_bridge.h RT_START_PAST
  bool sheets_unselfed = false;  // dlaa_bridge.h RT_SHEETS_UNSELFED
  // The lit particles streamed out with no ray tracing (smoke_map.hpp: any GPU, no helper).
  bool stream_particles = false;
  // The game's sun shadow map gets no casters (InstallShadowsOnly; with the ray-traced sun shadows): the rays alone cast.
  bool shadows_only = false;
  // The Debug View (DrawDebugView): 0 off, 1 sun shadows, 2 ambient occlusion, 3 Light Bounce, 4 reflections, 5 specular
  // occlusion, 6 point light shadows, 7 fog shadows, 8 the game's AO pass (screen space; a host's hook keeps it), 9 all
  // eight tiled.
  uint32_t debug_view = 0;
  // Its scene test (with the Debug View on): 0 everything, 1 the terrain alone, 2 objects and trees alone, 3 no leaves,
  // 4 none kept off-screen.
  uint32_t scene_test = 0;
  // Log Details: the numbers in the log every 10 s, the clocks and GPU timing queries behind them.
  bool debug_log = false;
  // Sprite Soldier Shadows: the flat far soldiers in the scene (SceneShader::sun_facing).
  bool sprite_shadows = false;
  bool far_tree_shadows = false;
  bool terrain_outside_view = false;
  // The tessellated draws' triangle count read when the GPU has it, a frame or more late, in place of waiting for it at the trace.
  bool terrain_count_later = false;
  // The indirect draws' numbers (CaptureIndirect: the game's GPU-culled grass) from an earlier frame's copy, read without a
  // wait, in place of this frame's read with one (Warhammer III: 1.2 ms a frame where its GPU grass is drawn).
  bool indirect_counts_later = false;
  // Casters out of the view from the shadow maps' draws (SceneShader::cascade_keep).
  bool casters_out_of_view = false;
  bool one_at_a_place = false;  // dlaa_bridge.h RT_ONE_AT_A_PLACE

  bool operator==(const Settings&) const = default;
};

// A game vertex shader whose main-view draws are in the scene, and its capture version; the terrain's draws are in it
// only in the frames that draw them, dynamic ones (units) are captured every frame, foliage (leaves: their capture
// version also gives the UV) is alpha-tested against its diffuse map's alpha (the pixel shader's t0, or `mask_slot`;
// dlaa_bridge.h RT_*).
// Without a capture version (`capture_shader` empty) the game's own vertex shader is streamed out (its SV_Position, made
// world positions with the frame's camera before the trace; Warhammer III); its foliage also streams out the output
// holding the UV (`uv_semantic`, `uv_semantic_index`: its xy).
// A vertex shader serving materials whose opacity lies in different slots lists them (`pixel_masks`): only its draws with a
// listed pixel shader are in the scene.
// A vertex shader that only gives control points (`tessellated`, 4 a patch): its draw's bound domain shader is streamed out,
// through `tessellated_keep` if given: a geometry shader taking that output, the draw's pixel shader's texture at `pixel_masks`'
// slot at its t0 and the domain shader's b1 at its b1, for the triangles that pixel shader would keep.
struct PixelMask {
  uint32_t pixel_shader = 0;
  UINT slot = 0;
  UINT uv_semantic_index = 0;  // cut_out_masks: the row's uv_semantic_index it needs
  const float* row = nullptr;  // cut_out_masks: only with this row on (null: always)
};

// A lit particle pixel shader (InstallSmokeShadows). With a quad shader its draw's views and samplers reach the quad shader
// `view_shift` and `sampler_shift` slots higher than the pixel shader binds them: one quad shader for pixel shaders binding
// the same resources at other slots (0: the same slots).
struct SmokePixelShader {
  uint32_t hash = 0;
  UINT view_shift = 0;
  UINT sampler_shift = 0;
};

// A particle vertex shader (InstallSmokeShadows) and how its draws stream their particles out: one a vertex, as it writes
// them (no quad shader: its own code; Attila); or one an instance of a quad (indexed, six indices an instance: the geometry
// shader `quad_shader` makes one record of an instance's first triangle; Pharaoh, Rome II). `indirect`: its indexed indirect
// draws too, one a list of the game's GPU-sorted particles (Warhammer II), its count on the GPU: INDIRECT_PARTICLES
// records emptied for it first, those past its count staying empty (no opacity: no shadow).
struct SmokeVertexShader {
  uint32_t hash = 0;
  std::span<const uint8_t> quad_shader;
  bool indirect = false;
};

struct SceneShader {
  uint32_t hash = 0;
  std::span<const uint8_t> capture_shader;
  // Placed draws (rigid objects and trees: each instance's world rows in their per-instance stream): the game's record
  // version, one point per instance with its rows at TEXCOORD0-2 (object_motion's): the mesh captured once (its first
  // instance; RT_INSTANCED), each draw's instances placed in the helper every frame (bridge v14 placements).
  std::span<const uint8_t> rows_shader;
  // The rows version for a draw whose pixel shader is in Host::faded_pixel_shaders, with Host::faded_row on: it leaves out the instances
  // that shader fades out (rt_place: a translation past MAX_TRANSLATION) and gives the fade in the record's w. A new mesh's first rows go
  // through `rows_shader` (its BLAS's build rows are its first instance's, faded or not).
  std::span<const uint8_t> faded_rows_shader;
  // Its vertices blend two sets of instance rows (Warhammer III's trees), and its rows version gives in the record how far the second set
  // lies from the first (x: turned and scaled, the difference's size over the first's scale; y: moved, metres). Log Details reads them:
  // a placed mesh is one instance's shape, right at another's place only where both sets differ alike.
  bool second_rows = false;
  // Captured every frame as drawn (as `dynamic`) while this row is on, placed otherwise.
  const float* dynamic_row = nullptr;
  // A mesh the game's shadow pass alpha-tests (its diffuse map in Runtime::cut_out_textures: palm and pine cards, fences) is
  // captured by this version, with the UV, cut out as leaves are but still. A unit (dynamic): its draws whose G-buffer pixel
  // shader clips (cut_out_pixel_shaders: beards, fur, leaves on a tree unit; solid cards in the scene shaded them black).
  std::span<const uint8_t> cut_out_shader;
  // Without a capture version (the game's own vertex shader streamed out): its draws whose pixel shader is in cut_out_masks are
  // cut out by that mask and `uv_semantic`, the others stay solid (a pixel shader's clip nobody read).
  bool clip_cut_outs = false;
  const float* cut_out_row = nullptr;  // clip_cut_outs only with this row on (null: always)
  // Placed, and the game draws it moving (last rows in its vertex shader: a soldier's weapons and armour): its rows every frame, none kept.
  bool moving = false;
  bool terrain = false;
  bool dynamic = false;
  bool foliage = false;
  bool grass = false;  // within the grass's share (half the helper's alpha-tested corners)
  bool thin = false;   // cloth and sails (RT_THIN)
  const float* thin_row = nullptr;  // thin only with this row on (Warhammer III's banners among other unit parts)
  // With this row on, its draws from a dynamic vertex buffer that the game draws from both sides (no back-face culling) or with
  // one of `thin_sheets_pixel_shaders` are thin: Attila's flags are rigid meshes the CPU moves, by the shader its buildings
  // have (its capture-point flag culled, by its alpha-tested G-buffer pixel shader).
  const float* thin_sheets_row = nullptr;
  std::span<const uint32_t> thin_sheets_pixel_shaders;
  bool sun_facing = false;  // sprite soldiers, far trees: never compact (a quad each), their capture version reads rt_sun at sun_slot
  // The capture version's rt_sun slot: past the game vertex shader's own textures, which its capture version reads as bound (Troy's
  // campaign billboards: their shroud at t0).
  UINT sun_slot = 0;
  bool far_tree = false;  // a far tree's billboard (with sun_facing)
  bool still = false;       // placed foliage without wind (the campaign's trees): no swaying copies
  bool tessellated = false;
  bool indirect = false;  // its indirect draws too (the game's GPU-culled grass): CaptureIndirect
  const char* uv_semantic = nullptr;
  UINT uv_semantic_index = 0;
  UINT mask_slot = 0;
  std::span<const PixelMask> pixel_masks;
  std::span<const uint8_t> tessellated_keep;
  // A hull shader taking the draw's control points that keeps only the patches out of the view (Settings::terrain_outside_view).
  std::span<const uint8_t> tessellated_outside;
  // A vertex shader of the game's shadow pass: units the main pass culled still cast from behind the camera. Its draws into the
  // cascades (Settings::casters_out_of_view) are streamed out through this geometry shader, with the main view's matrix at its
  // cb0[10-13]; it collapses what the main pass has. Their count stays, so never compact. With a rows_shader: a placed draw
  // instead, by its rows (cb0[10-13] is the main view, all 0 for a new mesh's first rows); the capture and rows shaders read no
  // state but the draw's vertex buffers (the game's shadow pass binds other resources than its main pass). With Host::cascade_world
  // the draw keeps its constants, this reads the world positions the vertex shader writes and streams them out (with a foliage draw's
  // UV in w); a terrain's (`terrain`) under Settings::terrain_outside_view instead.
  std::span<const uint8_t> cascade_keep;
  // The same with the draw's UV passed on (uv_semantic), for a cascade draw cut out by its pixel shader's mask (cut_out_masks).
  std::span<const uint8_t> cascade_keep_uv;
  // Host::cascade_world for this shader alone (its draws' constants stay, cascade_keep takes the main view at its b0), with its
  // capture version (if any) in place of the game's vertex shader; in a clip-space game what it streams out (world positions)
  // goes to the captured buffer's world part, as placed meshes do (Shogun 2's cloth and sprite soldiers out of view).
  bool cascade_world = false;
  // A mesh's identity in place of `hash`: shaders that draw one mesh (the main pass's and the shadow pass's) share its BLAS and its
  // instances (0: its hash).
  uint32_t key_family = 0;
};

// The main view's camera as ray tracing needs it: its view-projection and inverse (rows acting on column vectors; with
// the jitter, as its depth was drawn) and its position.
struct Camera {
  std::array<float, 16> view_projection = {};
  std::array<float, 16> inverse_view_projection = {};
  std::array<float, 3> position = {};
};

// The game's main view as ray tracing needs it, from the addon's anti-aliasing (the TW games' anti_aliasing.hpp,
// Warhammer III's taa_dlss.hpp, the other games' DLSS addons): the camera when `buffer` holds the main view's (the scene
// shaders' at `camera_slot`), the render size, whether the G-buffer guides were written this frame, and optionally
// whether a static draw's mesh was seen moving lately (it stays out of the scene) and whether a draw is the main view's.
// Games whose scene shaders are too many to list (Ryse) give `scene_draw` instead: every draw is asked whether it is a
// scene draw (an opaque one into the G-buffer), which fills in its shader (its vertex shader's hash: the game's own,
// streamed out; whether skinned: `skinned_vertex_shader`), and `capture_camera` reads the draw's camera from the
// context (`camera` is then not needed). A shader it gives a capture version (SceneShader::capture_shader: in clip space
// with the draw's own constants, as the game's SV_Position and UV) is streamed out through that version (Shogun 2's
// sun-facing cards); none: the game's own, as before.
struct Host {
  bool (*camera)(ID3D11Buffer* buffer, Camera* camera) = nullptr;
  std::array<UINT, 2> (*render_size)() = nullptr;
  bool (*guides_written)() = nullptr;
  UINT camera_slot = 0;
  bool (*capture_camera)(ID3D11DeviceContext* native, Camera* camera) = nullptr;
  bool (*scene_draw)(ID3D11DeviceContext* native, SceneShader* shader) = nullptr;
  // Whether a vertex shader's code has bone inputs (null: BLENDINDICES in its input signature).
  bool (*skinned_code)(std::span<const uint8_t> code) = nullptr;
  bool (*moving)(ID3D11DeviceContext* native, const renodx::utils::command_action::DrawArguments& arguments) = nullptr;
  bool (*moving_indexed)(ID3D11DeviceContext* native, const renodx::utils::command_action::DrawIndexedArguments& arguments) = nullptr;
  // Whether a draw is the main view's (null: every draw into its depth is).
  bool (*main_view)(ID3D11DeviceContext* native) = nullptr;
  // The G-buffer guides again from the reflection pass's G-buffer (null: none; the water's reflections need it).
  void (*rewrite_guides)(ID3D11DeviceContext* native) = nullptr;
  // rt_light_copy's code: Reflections' `light_buffer` as R16G16B16A16F into the helper's R11G11B10F light (Warhammer III,
  // Pharaoh). Empty: only an R11G11B10F light buffer is read (copied as it is).
  std::span<const uint8_t> light_copy;
  // A row (0: off) for placed draws in a clip-space game; off, they are captured every frame. Null: always placed.
  const float* placed_row = nullptr;
  // With placed_row on: what was placed stays until the scene ends, bounded by KEPT_TRIANGLES and KEPT_MESHES, not RECENT_FRAMES.
  bool keep_placed = false;
  // A light draw's colour to light in the rays (the paths): times the game's light shader's own scale of its diffuse.
  float local_light_scale = 1.f;
  // A metre in the campaign map's world units, for the rays' ranges (frame_history::campaign_frames; 1: as in a battle).
  float campaign_metre = 1.f;
  // Pixel shaders (sorted) that discard by their instance's fade, and the row (0: off) that leaves such instances out of the
  // rays (SceneShader::faded_rows_shader). Null: no such rule.
  const float* faded_row = nullptr;
  std::span<const uint32_t> faded_pixel_shaders;
  // SceneShader::cascade_keep's draws. False: the main view's matrix put in at the shadow vertex shader's cb0[10-13] (Warhammer III), its
  // clip space streamed out. True (Pharaoh: its shadow vertex shaders keep their own view-projection at cb0[9-12] and write each corner's
  // world position to an output): the game's constants stay, the geometry shader takes the main view's matrix at its b0[10-13] and
  // streams out world positions (a foliage draw's UV in w), so the scene stays a world-space one.
  bool cascade_world = false;
  // Whether a cascade draw's depth target is the game's per-frame sun shadow map (null: a square array of 4, Warhammer III's cascades;
  // the same shaders may draw a UI's small view). Also the map Ray-Traced Shadows Only skips the casters of (null: the sun draws'
  // last square depth texture).
  bool (*cascade_target)(const D3D11_TEXTURE2D_DESC& desc) = nullptr;
  // A game whose shadow pass comes before its main pass (Shogun 2): its cascade draws test against the last frame's main
  // view (they wait for this frame's otherwise, which never comes first: none captured).
  bool cascade_last_view = false;
};

namespace internal {

constexpr UINT MAX_VERTICES = dlaa_bridge::RT_TRANSFER_WIDTH * dlaa_bridge::RT_TRANSFER_HEIGHT;
// What is captured every frame, at most (it was full in a large battle with grass: what did not fit changed every frame,
// grass and units' shadows pulsed, ATN 2026-10-28): units, grass and the rest at most MAX_UNIT_VERTICES (grass at most
// its share of them), so the terrain keeps the rest. Doubled with the transfer texture (half the soldiers' draws
// were left out: holes in their shadows, ATN 2026-10-28); grass 4M (3.5M drawn in ATN's battle: what fit changed as the
// view did, flickering grass shadows).
// 17M of the 20M texels since swaying trees (MAX_TREE_VERTICES: their leaves where drawn every frame), 29M of 32M since
// city walls were left out in a 4x battle (ATN 2026-10-29). Units (skinned soldiers and horses) take at most three
// quarters of the frame's texels, triangles and clusters: what comes after them (walls and buildings in dynamic buffers,
// the terrain, banners) keeps a quarter. Grass takes half the helper's alpha-tested corners (16M on GPUs over 20 GB, 4x
// the 4M that left ~70M grass vertices a frame out), swaying leaves the rest.
constexpr UINT MAX_DYNAMIC_VERTICES = 29u << 20;
// A game streaming out clip-space positions with placed meshes (Warhammer III): their world-space triangles and rows take the
// captured buffer's top, a frame's new meshes at most these, and go right after the clip-space vertices at the trace
// (rt_vertices must not unproject them; a mesh not yet in stays out until a later frame).
constexpr UINT WORLD_TEXELS = 8u << 20;
constexpr UINT WORLD_BASE = MAX_VERTICES - WORLD_TEXELS;
constexpr UINT MAX_TREE_VERTICES = 3u << 20;
// As the helper's: meshes unseen this long are released there, and forgotten here.
constexpr uint64_t RELEASE_FRAMES = 3600;
// Host::keep_placed. The kept meshes' room, oldest out first: the helper's STATIC_TRIANGLES and STATIC_RECORDS less a quarter for
// what the frame draws new.
constexpr uint64_t KEPT_TRIANGLES = 6u << 20;
constexpr size_t KEPT_MESHES = 12288;
constexpr uint32_t KEPT_FOREVER = 0xFFFFFF;  // rt_place's frames wrap there
// Frames without the main view: a loading screen. The scene it left is forgotten (its buffers' addresses come back as other meshes).
constexpr uint64_t SCENE_GONE_FRAMES = 30;
// Smoke Shadows: the particles streamed out between traces at most (48 bytes each; a draw holds its whole list, nearly all
// without opacity: 742 of a battle's 47884 had some, and at 65536 a list drawn first took the place of the catapults' smoke
// for good, ATN's debug36; Smoke Shadow Quality Extreme twice these, Maximum four times: ATN's 2026-11-12 siege filled
// them every frame, burning smoke flickered at Ultra), the quads for those with opacity at most (rt_smoke; High twice these,
// Ultra four times, Extreme 8, Maximum 16), their capture's key.
constexpr UINT MAX_PARTICLES = 1u << 19;
// An indirect particle draw's room (SmokeVertexShader::indirect; Warhammer II's sieges drew about 20000).
constexpr UINT INDIRECT_PARTICLES = 1u << 16;
constexpr UINT MAX_SMOKE_QUADS = 1u << 15;
constexpr float SMOKE_LEAST = 0.05f;  // the opacity over area a particle needs for a quad, at the least
constexpr uint64_t SMOKE_KEY = 0x534D4F4B45ull;
// Tessellated draws: their triangles a frame at most (the hull shader decides how many, so past the draw), the draws (a
// count each), their capture's key.
constexpr UINT MAX_TESSELLATED_VERTICES = 6u << 20;
// A GPU that stopped must not hold the game's thread at the terrain's count. Long: traces of 100 ms were seen over a siege's
// forest, and a frame past this one goes without the terrain in its rays.
constexpr auto TERRAIN_COUNT_LIMIT = std::chrono::milliseconds(250);
constexpr size_t MAX_TESSELLATED_DRAWS = 16;
// Settings::terrain_count_later: the frames whose counts may be on their way at once, and the empty vertices one copy puts under a
// frame's tessellated triangles.
constexpr size_t TERRAIN_COUNT_FRAMES = 6;
constexpr UINT TERRAIN_EMPTY_VERTICES = 1u << 19;
constexpr uint64_t TESSELLATED_KEY = 0x54455353ull;
// A placed mesh not seen moving is placed every this many frames, in turn (Capture; the helper keeps its instances).
constexpr uint64_t PLACE_TURNS = 4;
// The local lights a frame keeps for the next trace to pick from.
constexpr size_t MAX_LOCAL_LIGHTS = 8192;
// Compact draws (bridge v16): the cluster sets made a frame at most (a battle's first frames bring hundreds of meshes).
constexpr uint32_t MAX_SET_BUILDS = 16;
// The frames a set waits for its first instance's corners (its triangles' order) before it is made in the index order
// (a grass patch's clusters each over the whole patch: a 4K harness field with ATN's settings traced 30 ms, sorted 7.6).
constexpr uint64_t CORNER_FRAMES = 30;
// Record Ray Tracing Profile's length.
constexpr uint64_t PROFILE_MS = 30000;

// The lit particles' stream output, made at the first draw from the particle vertex shader's own code, apart from ray
// tracing's objects (smoke_map.hpp reads it with RT off). Three float4 a particle: centre and age, colour and opacity, size
// and scale and emitter; behaviours: their cb_vfx_emitter_constants_common at the frame's first draw.
struct ParticleStream {
  ComPtr<ID3D11Device> device;
  UINT capacity = 0;  // particles (MAX_PARTICLES, by Smoke Shadow Quality)
  ComPtr<ID3D11Buffer> particles;
  ComPtr<ID3D11ShaderResourceView> srv;
  std::vector<ComPtr<ID3D11GeometryShader>> outputs;  // as smoke_vertex_shaders (null: none)
  ComPtr<ID3D11Buffer> behaviours;
  ComPtr<ID3D11Buffer> empty;  // INDIRECT_PARTICLES empty records (where a vertex shader is indirect)
};

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  std::vector<ComPtr<ID3D11VertexShader>> capture_shaders;  // as the scene shaders
  std::vector<ComPtr<ID3D11VertexShader>> rows_shaders;     // as the scene shaders (placed ones)
  std::vector<ComPtr<ID3D11VertexShader>> cut_out_shaders;  // as the scene shaders (SceneShader::cut_out_shader)
  std::vector<ComPtr<ID3D11GeometryShader>> cut_out_outputs;
  std::vector<ComPtr<ID3D11GeometryShader>> rows_outputs;
  std::vector<ComPtr<ID3D11VertexShader>> faded_rows_shaders;  // as the scene shaders (SceneShader::faded_rows_shader)
  std::vector<ComPtr<ID3D11GeometryShader>> faded_rows_outputs;
  std::vector<ComPtr<ID3D11GeometryShader>> capture_outputs;
  std::unordered_map<uint32_t, ComPtr<ID3D11GeometryShader>> game_outputs;  // the game's own, by their hash
  std::unordered_map<uint32_t, ComPtr<ID3D11GeometryShader>> game_uv_outputs;  // with the UV (their foliage)
  std::unordered_map<uint32_t, ComPtr<ID3D11VertexShader>> game_capture_shaders;  // Host::scene_draw's capture versions, by the game's hash
  ComPtr<ID3D11Buffer> cascade_camera;  // SceneShader::cascade_keep's cb0 (Host::cascade_world: its b0): the main view's matrix at [10-13]
  ComPtr<ID3D11Buffer> cascade_no_view;  // All 0: a new mesh's first rows, none left out
  ComPtr<ID3D11Buffer> captured;  // stream output: world positions, three per triangle
  ComPtr<ID3D11ShaderResourceView> captured_srv;
  ComPtr<ID3D11Buffer> captured_uv;  // the game's own shaders' foliage UVs, at their vertices' places
  // Tessellated draws' triangles, one after the other, and each draw's count: copied to `captured` at the trace.
  ComPtr<ID3D11Buffer> tessellated;
  ComPtr<ID3D11HullShader> tessellated_outside;
  std::array<ComPtr<ID3D11Query>, MAX_TESSELLATED_DRAWS> tessellated_queries;
  // Settings::terrain_count_later: a frame's counts until the GPU has them (`draws` 0: free), and vertices with a NaN x (the helper
  // skips such triangles) to lay under a frame's tessellated triangles: fewer written than the last count known, the rest stays empty.
  struct TerrainCount {
    std::array<ComPtr<ID3D11Query>, MAX_TESSELLATED_DRAWS> queries;
    uint32_t draws = 0;
    uint64_t frame = 0;
  };
  std::array<TerrainCount, TERRAIN_COUNT_FRAMES> terrain_counts;
  TerrainCount* terrain_count = nullptr;  // this frame's (null: none was free)
  ComPtr<ID3D11Buffer> tessellated_empty;
  ComPtr<ID3D11Buffer> indirect_probe;  // the indirect draws' numbers of an earlier frame (CaptureIndirect)
  ComPtr<ID3D11ShaderResourceView> captured_uv_srv;
  ComPtr<ID3D11ComputeShader> vertices_shader;
  ComPtr<ID3D11ComputeShader> skies_shader;
  ComPtr<ID3D11ComputeShader> debug_shader;  // the Debug View's, into its picture (the tone mapper's output's size)
  ComPtr<ID3D11Texture2D> debug_texture;
  ComPtr<ID3D11UnorderedAccessView> debug_uav;
  ComPtr<ID3D11Resource> debug_ambient_resource;  // the game's AO pass's target (the host's hook), its copy and a view of it
  ComPtr<ID3D11Texture2D> debug_ambient_copy;
  ComPtr<ID3D11ShaderResourceView> debug_ambient;
  ComPtr<ID3D11ComputeShader> positions_shader;
  ComPtr<ID3D11ComputeShader> sun_shader;
  ComPtr<ID3D11ComputeShader> occlusion_shader;
  ComPtr<ID3D11ComputeShader> atlas_shader;
  ComPtr<ID3D11SamplerState> linear_wrap;
  ComPtr<ID3D11VertexShader> fullscreen_shader;
  ComPtr<ID3D11PixelShader> ambient_shader;
  ComPtr<ID3D11Buffer> constants;  // the compute's and the apply's, 96 bytes
  ComPtr<ID3D11Buffer> sun_constants;  // rt_sun's b1: where the draw's sun_direction starts
  ComPtr<ID3D11BlendState> multiply;
  ComPtr<ID3D11SamplerState> linear_clamp;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ComPtr<ID3D11DepthStencilState> no_depth;
  // Smoke Shadows: rt_smoke's quads from the lit particles (ParticleStream) and its count of those taken.
  ComPtr<ID3D11ComputeShader> smoke_shader;
  ComPtr<ID3D11Buffer> smoke_quads;
  ComPtr<ID3D11UnorderedAccessView> smoke_quads_uav;
  ComPtr<ID3D11Buffer> smoke_quads_read;  // that count read back
  bool smoke_read_waiting = false;
  float smoke_least = SMOKE_LEAST;  // raised while more want a quad than there is room for
  // The sun's occlusion for the patched sun shaders (the display's size).
  ComPtr<ID3D11Texture2D> occlusion;
  ComPtr<ID3D11ShaderResourceView> occlusion_srv;
  ComPtr<ID3D11UnorderedAccessView> occlusion_uav;
  // Tracing at the sun draws: their AO times the traced ambient visibility (the display's size, RGBA16F).
  ComPtr<ID3D11ComputeShader> combine_shader;
  // The traced local lights' positions (RT_MAX_LIGHTS, then their count: rt_local_lights.hlsli's b12).
  ComPtr<ID3D11Buffer> light_constants;
  ComPtr<ID3D11Buffer> water_flags;  // rt_reflection.hlsli's rt_reflection_flags: the water traced
  ComPtr<ID3D11Buffer> ambient_lighting;  // Path-Traced Ambient: the sun light draws' lighting constants, no ambient cube
  UINT ambient_lighting_bytes = 0;
  ComPtr<ID3D11Buffer> no_ambient;  // the ambient cube's 96 bytes of zeros
  ComPtr<ID3D11ShaderResourceView> ambient_off;  // Path-Traced Ambient: a texel of 1 (the patched sun shaders' t51)
  ComPtr<ID3D11ShaderResourceView> surface_depth;  // the reflection pass's depth (the water's in it) as depth
  ID3D11Resource* surface_depth_resource = nullptr;
  ComPtr<ID3D11ComputeShader> light_copy_shader;  // Host::light_copy's, made at its first R16G16B16A16F light buffer
  ComPtr<ID3D11Texture2D> combined;
  ComPtr<ID3D11ShaderResourceView> combined_srv;
  ComPtr<ID3D11UnorderedAccessView> combined_uav;
  // The cost report's GPU timestamps: a disjoint query and three stamps a frame (the trace's start, its request, the
  // helper's answer), four frames in turn, read back when done.
  struct Timing {
    ComPtr<ID3D11Query> disjoint;
    std::array<ComPtr<ID3D11Query>, 3> stamps;
    bool pending = false;
    uint64_t frame = 0;  // its (the profile)
  };
  std::array<Timing, 4> timings;
  uint32_t timing_next = 0;
};

// The addon's own ray tracing cost since the last report (Update, every 10 s; ATN 2026-10-28: "RT shadows destroy the
// performance", the helper's GPU time a few milliseconds): its CPU time in Capture and Trace, its GPU passes before the
// request and the game's wait for the helper's answer; the vertices captured every frame and those their shares left out.
struct Costs {
  int64_t capture_ticks = 0;
  int64_t trace_ticks = 0;
  int64_t terrain_wait_ticks = 0;  // of trace_ticks: waiting for the GPU to count the terrain's triangles
  uint32_t frames = 0;
  double passes_ms = 0.0;
  double waiting_ms = 0.0;
  uint32_t timed = 0;  // frames whose timestamps came back
  uint64_t dynamic_vertices = 0;
  uint64_t left_out = 0;  // their share full
  uint64_t no_room = 0;   // the frame's captures or vertex texture full
  uint64_t compact_draws = 0;
  uint64_t particles = 0;  // Smoke Shadows' streamed out
  uint32_t most_smoke_quads = 0;  // the most rt_smoke wanted for a trace (past its room: some left out)
  // What was left out by vertex shader: its texels by the first of its shares that was full (SHARE_NAMES).
  std::unordered_map<uint32_t, std::array<uint64_t, 5>> left_out_shaders;
};
// Record Ray Tracing Profile (StartProfile): a line a frame into tw_rt_profile.csv next to the exe for PROFILE_MS, the
// helper writing its requests' into tw_rt_profile_helper.csv (RT_PROFILE). A frame's line: its CPU time present to
// present; the addon's on the game's thread (capturing, tracing, of it waiting for the helper's answers); the cluster sets
// made on workers; its scene's counts; the last GPU times that came back (their frame: the addon's passes, the game's
// GPU waiting for the helper). The running totals as at the last line (Costs' since its report, the bridge's, the
// workers') give the frame's share.
struct Profile {
  std::ofstream file;
  std::filesystem::path path;
  uint64_t end_time = 0;  // GetTickCount64's
  uint32_t frames = 0;
  LARGE_INTEGER started = {};
  LARGE_INTEGER presented = {};
  int64_t capture_ticks = 0;
  int64_t trace_ticks = 0;
  int64_t answer_ticks = 0;
  int64_t present_answer_ticks = 0;
  int64_t set_ticks = 0;
  uint64_t left_out = 0;
  uint64_t no_room = 0;
  uint64_t gpu_frame = 0;
  double passes_ms = 0.0;
  double waiting_ms = 0.0;
  // The frame's trace's (Trace empties its lists): captures, compact draws, placements, new sets, texels captured, lights.
  std::array<uint64_t, 6> counts = {};
};
// The shares a dynamic draw needs room in (Capture): all dynamic draws' texels, the helper's triangles and clusters, the
// grass's corners, the units' three quarters of those.
constexpr std::array<const char*, 5> SHARE_NAMES = {"dynamic texels", "triangles", "clusters", "grass corners", "units' three quarters"};

// Adds its scope's CPU time to `ticks` (the cost report; null: Log Details off, no clock read: a capture runs per draw).
class CpuTime {
 public:
  explicit CpuTime(int64_t* ticks) : ticks_(ticks) {
    if (ticks_ != nullptr) QueryPerformanceCounter(&start_);
  }
  ~CpuTime() {
    if (ticks_ == nullptr) return;
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    *ticks_ += end.QuadPart - start_.QuadPart;
  }
  CpuTime(const CpuTime&) = delete;
  CpuTime& operator=(const CpuTime&) = delete;

 private:
  int64_t* ticks_;
  LARGE_INTEGER start_ = {};
};

// Path-Traced Ambient: the paths' frames with the game's ambient before it goes (the helper learns its sky's scale). 30 kept a battle's very first
// frames: 30 % darker than the same paused view 60 s later (R1's log); what's wrong in them isn't known.
constexpr uint32_t PATH_AMBIENT_FRAMES = 150;

// rt_positions.cs's constants: world positions from a depth.
struct PositionConstants {
  std::array<float, 16> inverse_view_projection;
  std::array<float, 4> camera;
  std::array<uint32_t, 4> size;
};

struct Runtime {
  std::mutex mutex;
  Costs costs;  // the render thread's (as `frame`)
  Profile profile;
  std::atomic<int64_t> set_ticks = 0;  // the cluster sets' making on workers so far (CPU ticks; the profile)
  Settings settings;
  uint64_t frame = 0;
  // This frame's new captures (consecutive dynamic draws joined) and their draws' keys, the scene meshes it drew; the
  // keys in the scene (the frame they were last drawn).
  std::vector<dlaa_bridge::RtCapture> captures;
  std::unordered_set<uint64_t> captured_keys;
  std::array<float, 16> main_view_projection = {};  // from this frame's main-view captures, for the cascades' (they come later)
  uint64_t main_view_frame = 0;
  uint64_t cascade_camera_frame = 0;
  std::vector<uint32_t> slices;  // each capture's opacity mask's atlas slice (leaves)
  uint32_t captured_vertices = 0;
  uint32_t world_vertices = WORLD_BASE;  // their end in the captured buffer
  uint32_t tessellated_draws = 0;  // this frame's so far (DeviceObjects::tessellated)
  // The buffer the frame's indirect draws read their numbers from (CaptureIndirect): read when a draw needs it, once a frame; a copy
  // read without waiting a frame or more later says which draws were empty.
  std::vector<uint32_t> indirect_arguments;
  ID3D11Buffer* indirect_arguments_buffer = nullptr;
  uint64_t indirect_arguments_frame = 0;
  std::vector<uint32_t> indirect_probe_values;
  ID3D11Buffer* indirect_probe_buffer = nullptr;
  uint64_t indirect_probe_frame = 0;
  bool indirect_probe_pending = false;
  // The room the frame's other captures leave them: they join last, but the ground counts for more than what a full frame
  // drops instead. Last frame's corners, an eighth over for a moving camera; 0 in a game without such draws.
  uint32_t tessellated_reserved = 0;
  // Settings::terrain_count_later: the newest count the GPU gave (the corners the draws wanted to write, and whose frame's), and the
  // corners this frame's draws were given (emptied before the first of them).
  uint64_t terrain_known_corners = 0;
  uint64_t terrain_known_frame = 0;
  uint32_t tessellated_expected = 0;
  // Whether this frame's count is read later (decided at its first tessellated draw): with the row on and a count known from the
  // last TERRAIN_COUNT_FRAMES frames. Else the frame waits for its own, as with the row off: a battle's first frames, the first
  // after a menu or a pause of the terrain's draws had no terrain in the rays or what an old count left room for.
  bool terrain_later = false;
  uint64_t tessellated_room_log_tick = 0;
  uint64_t terrain_count_log_tick = 0;
  uint32_t dynamic_vertices = 0;
  uint32_t unit_vertices = 0;   // of them the units' (skinned soldiers and horses), their triangles and clusters
  uint32_t unit_triangles = 0;
  uint32_t unit_clusters = 0;
  uint32_t grass_vertices = 0;  // and grass's (its swaying copies' with rows: placed grass)
  uint32_t alpha_corners = dlaa_bridge::RT_DYNAMIC_UVS / 2;  // the helper's alpha-tested corners a frame (grass, swaying leaves)
  std::vector<uint64_t> drawn;
  // This frame's draws of placed meshes in the scene: their instances' rows in the captured vertices (bridge v14).
  std::vector<dlaa_bridge::RtPlacement> placements;
  // Compact draws (bridge v16, with the helper's Mega Geometry): an indexed dynamic draw's mesh (its index buffer's
  // range; the buffer kept, so its key stays its own) gets a cluster set once its indices are read back (a copy read in
  // a later frame, never waited for); its vertices then stream once each, as points through the remap index buffer (a
  // block's vertices), in place of its triangles, and its set goes with the first trace that draws it (again after the
  // helper's new device or when its full pool dropped sets: CompactState). Until then, and without a set, its triangles
  // are captured.
  struct ClusterMesh {
    ComPtr<ID3D11Buffer> indices;
    ComPtr<ID3D11Buffer> staging;  // its range's copy until read
    uint64_t copied_frame = 0;
    ComPtr<ID3D11Buffer> remap;  // R32_UINT
    std::vector<uint8_t> data;   // its set's
    uint32_t vertices = 0;       // a block's
    uint32_t clusters = 0;
    uint32_t triangles = 0;
    uint64_t used_frame = 0;
    bool failed = false;  // no set: its triangles captured
    bool sent = false;
    std::future<dlaa_bridge::ClusterSetData> making;  // its set on a worker (PrepareClusterSet)
    ComPtr<ID3D11Buffer> corners;  // its first instance's captured corners, copied at a full capture (the set's order)
    bool unsorted = false;         // its set made without them
  };
  std::unordered_map<uint64_t, ClusterMesh> cluster_meshes;
  // This frame's compact draws (consecutive ones of a mesh joined), the sets its trace sends and their bytes, its
  // dynamic triangles and clusters (captures' and compact draws': the helper's RT_DYNAMIC_*), the sets made.
  std::vector<dlaa_bridge::RtCompactDraw> compact;
  std::vector<uint64_t> new_sets;
  uint32_t new_set_bytes = 0;
  uint32_t dynamic_triangles = 0;
  uint32_t dynamic_clusters = 0;
  uint32_t tree_vertices = 0;  // swaying leaves' texels and corners (their UVs)
  uint32_t tree_corners = 0;
  uint32_t set_builds = 0;
  uint32_t compact_state = 0;  // the helper's CompactState as last seen
  // Smoke Shadows: the particles streamed out since frame particles_frame began (the lit particles come after its
  // trace: the next one takes them).
  uint32_t particles = 0;
  uint64_t particles_frame = 0;
  ParticleStream particle_stream;
  struct Known {
    uint64_t frame = 0;  // last drawn
    // A placed mesh's instances over a frame's draws: this frame's so far, those placed, the last frame's; a frame after
    // they changed places all its draws.
    uint64_t counted_frame = 0;
    uint32_t instances = 0;
    uint32_t placed = 0;
    uint32_t last_instances = 0;
    bool all_draws = false;
    // Its instances as the frame's trace went out, and whether the last traced frame drew none after that: its count at
    // the trace is then the frame's whole count.
    uint64_t traced_frame = 0;
    uint32_t traced_instances = 0;
    bool drawn_before_trace = false;
    uint32_t triangles = 0;
    bool placed_mesh = false;
  };
  std::unordered_map<uint64_t, Known> known;
  uint32_t kept_frames = 0;  // TraceRequest::kept_frames
  bool scene_gone = false;   // the next trace's kept_frames is 1
  // A static draw's short key (its shader, arguments, first vertex buffer and index buffer) and the full key it stood
  // for; ambiguous once it stood for two (instances told apart by their constants): Capture's fast path.
  struct ShortKey {
    uint64_t key = 0;
    bool ambiguous = false;
  };
  std::unordered_map<uint64_t, ShortKey> short_keys;
  // New static meshes captured per vertex shader since the last report (Update: every 10 s), the report's time (ATN
  // 2026-10-28: the helper's 16384 static records filled within a minute, 93 meshes in the scene).
  std::unordered_map<uint32_t, uint32_t> new_static;
  uint64_t new_static_time = 0;
  // The helper's status logged this long after the settings changed (each effect's GPU time as they are tried).
  uint64_t status_log_time = 0;
  std::unordered_map<uint32_t, std::vector<std::string>> new_static_examples;  // the last three each
  std::unordered_map<uint32_t, uint32_t> dynamic_buffers;  // draws captured every frame for their dynamic vertex buffers
  // The placed draws logged per vertex shader (their instancing and vertex buffers, the first two each; ATN 2026-10-28:
  // no tree shadows).
  std::unordered_map<uint32_t, uint32_t> logged_draws;
  // Log Details: the rows read back (Capture's ROWS_READS) and the draws among them worth a line, for faded draws and second-rows draws.
  std::array<std::array<uint32_t, 2>, 2> rows_reads = {};
  std::unordered_set<uint32_t> left_out_logged;  // shaders whose draws the capture can't take, each said once
  std::unordered_set<std::string> thin_sheets_logged;  // Log Details: each kind of the banners' row's dynamic draws, said once
  // The local lights the light draws had this frame and last; those this frame's trace took (their positions also in
  // objects.light_constants, b12) and whether it did.
  std::vector<LocalLight> lights_drawn;
  std::vector<LocalLight> lights_last;
  std::vector<std::array<float, 4>> lights;
  std::vector<std::array<float, 4>> light_colours;
  bool lights_traced = false;
  // This frame's trace's Torch and Fire Light in the Light Bounce: 0 none, 1 the lights' light in the bounce, 3 the
  // bounce's shadow ratio (the Light Bounce request asks the same).
  uint32_t lights_in_bounce = 0;
  uint32_t helper_device = 0;             // the helper's ray tracing device (a new one: its scene empty)
  uint64_t wanted_tick = 0;               // when ray tracing was last on (GetTickCount64)
  // The garbage triangles the helper left out (far off or stretched) as last read, the captures' vertex shaders by key
  // (since the last report) and what was left out since then by vertex shader (Update's report).
  uint32_t helper_left_out = 0;
  std::unordered_map<uint64_t, uint32_t> key_shaders;
  std::unordered_map<uint32_t, uint32_t> left_out_shaders;
  // Under Ray-Traced Shadows Only, one frame every 10 s: the pixel shaders reading the game's sun shadow map that are
  // not the patched sun draws (forward-lit grass and the like: lit everywhere, the map has no casters).
  uint64_t readers_frame = UINT64_MAX;
  bool timed = false;  // the cost report's clocks and GPU queries run (Log Details, a profile)
  std::unordered_set<uint32_t> sun_draw_shaders;
  std::unordered_set<uint32_t> reader_shaders;  // the forward passes given the rays' shadows (InstallShadowReaders)
  std::unordered_set<uint64_t> shadow_readers;  // pixel shader << 32 | its draw's vertex shader
  uint64_t traced_frame = UINT64_MAX;     // the frame the visibilities are from
  uint64_t occlusion_frame = UINT64_MAX;  // the frame `occlusion` was made in
  uint64_t sun_frame = UINT64_MAX;        // the frame the sun was written in
  uint64_t combined_frame = UINT64_MAX;   // the frame `combined` was made in
  uint64_t ambient_frame = UINT64_MAX;    // the frame an ambient occlusion pass took the traced ambient visibility
  uint64_t debug_ambient_frame = UINT64_MAX;  // the frame the game's AO pass drew into objects.debug_ambient
  float debug_ambient_mapping = 1.f;          // where its sun draws read it (KeepDebugAmbient), against a pixel's share of the picture
  uint64_t bounced_frame = UINT64_MAX;    // the frame the Light Bounce was traced (a texel each `bounce_scale` pixels)
  UINT bounce_scale = 1;
  uint64_t reflected_frame = UINT64_MAX;  // the frame the reflections were asked for
  bool reflected = false;                 // and whether they were traced
  bool water_traced = false;              // the water's from its own depth and normal (the guides and positions again)
  bool water_traced_logged = false;       // water_traced as last logged (with `reflected`)
  bool light_copy_logged = false;
  // Path-Traced Ambient: on once the paths ran PATH_AMBIENT_FRAMES in a row with the game's ambient (the helper learns
  // the scale from its ambient cube to the screen's light meanwhile), the paths' frames so far.
  bool path_ambient_on = false;
  uint32_t path_frames = 0;
  uint64_t ambient_off_frame = UINT64_MAX;     // Path-Traced Ambient: the frame objects.ambient_lighting was copied in
  ID3D11Buffer* ambient_off_source = nullptr;  // from this buffer of the game's (compared only)
  ComPtr<ID3D11Buffer> ambient_game_lighting;  // the game's, put back after the draw at ambient_slot
  UINT ambient_slot = 0;
  PositionConstants positions = {};       // the trace's positions pass's (the reflection pass runs it again for the water)
  ID3D11ShaderResourceView* game_ao = nullptr;  // the AO it was made from (compared only)
  std::array<UINT, 2> traced_size = {};   // the render size traced
  std::array<float, 16> traced_view_projection = {};  // its camera's (the ray-traced Light Bounce's)
  std::array<float, 3> traced_position = {};          // its camera's place
  bool camera_moving = false;                         // it moved at the last trace: every placed mesh is placed every frame
  bool logged_bounce = false;  // the ray-traced Light Bounce's last state, as logged
  // The atlas's textures (kept alive while they have a slice; for the bridge's atlas `atlas_generation`), its free
  // slices and the frame each was last used.
  std::unordered_map<ID3D11Resource*, std::pair<ComPtr<ID3D11Resource>, uint32_t>> atlas_slices;
  // The diffuse maps of the meshes the game's shadow pass alpha-tests (only compared).
  std::unordered_set<ID3D11Resource*> cut_out_textures;
  std::vector<uint32_t> free_slices;
  std::array<uint64_t, dlaa_bridge::RT_ATLAS_SLICES> slice_used = {};
  uint32_t atlas_generation = 0;
  DeviceObjects objects;
};

static Runtime runtime;
// The addon's (Install), kept while installed.
static std::span<const SceneShader> scene_shaders;
static std::span<const uint32_t> cut_out_pixel_shaders;  // sorted
static std::span<const PixelMask> cut_out_masks;  // sorted by pixel shader (SceneShader::clip_cut_outs)
static Host host;
// Where the game's lighting buffer holds its sun colour, the paths' sun light with Path-Traced Ambient (rt_scene.hpp
// InstallSunDraws; 0: none).
static UINT sun_colour = 0;
// The code of the scene shaders without a capture version, as the game made them (every vertex shader with
// host.scene_draw; runtime.mutex), and those with bone inputs (BLENDINDICES: skinned, captured every frame).
static std::unordered_map<uint32_t, std::vector<uint8_t>> game_code;
// Smoke Shadows' particle shaders (InstallSmokeShadows; empty: none): the vertex shaders (their code kept) and the lit
// pixel shaders their smoke is drawn with.
static std::span<const SmokeVertexShader> smoke_vertex_shaders;
static std::span<const SmokePixelShader> smoke_pixel_shaders;
static std::unordered_map<uint32_t, bool> skinned_code;
// A scene draw's pixel shader (host.scene_draw: SceneCoverage): solid (none, or its discards dither only), alpha-tested
// against its t0 at the input `uv_semantic` (xy: an opacity mask), or left out (t0 read otherwise: a tiled or computed
// mask); by the native shader (ReShade's pipeline handle; runtime.mutex).
enum class Coverage : uint8_t {
  SOLID,
  MASKED,
  LEFT_OUT,
};
struct PixelCoverage {
  Coverage coverage = Coverage::SOLID;
  std::string uv_semantic;
  UINT uv_index = 0;
};
static std::unordered_map<uint64_t, PixelCoverage> pixel_coverage;
static bool clip_captures = false;  // the scene shaders stream out their SV_Position
static bool world_captures = false;  // and some are placed (or SceneShader::cascade_world): those in world space (WORLD_BASE)
// Either effect on (else every callback returns at once).
static std::atomic_bool active = false;

inline float RangeScale() {
  return (frame_history::campaign_frames.load(std::memory_order_relaxed) != 0 ? host.campaign_metre : 1.f);
}

inline bool Enabled() {
  return runtime.settings.sun_shadows || runtime.settings.ambient_occlusion || runtime.settings.light_bounce || runtime.settings.reflections
         || runtime.settings.specular_occlusion || runtime.settings.local_lights || runtime.settings.fog_shadows;
}

// Fog shadows the only ray-traced effect, with Settings::cheap_fog_alone.
inline bool CheapFogAlone(const Settings& settings) {
  return settings.cheap_fog_alone && settings.fog_shadows
         && !(settings.sun_shadows || settings.ambient_occlusion || settings.light_bounce || settings.reflections || settings.specular_occlusion
              || settings.local_lights);
}

// Whether any ray this frame meets grass (the helper's GRASS and GRASS_STILL instance masks: the sun's rays, the AO's and Path-Traced
// AO's short ones; the fog's, the lights', the Light Bounce's, the paths' and the reflections' pass it): else grass is not captured
// (nor its indirect draws' numbers read back), the same picture. Units' cut-outs are not grass (SceneShader::grass).
inline bool GrassMet() {
  return runtime.settings.sun_shadows || runtime.settings.ambient_occlusion || runtime.settings.path_ambient;
}

// The immediate context under ReShade's command list (null: another API, a deferred context). ReShade's command list hands
// out the game's own context, which is no proxy: that answer is kept per thread while the pointer stays (the proxy
// lookup, a failed QueryInterface, was asked again at every captured draw); a proxy is looked up every call.
inline ID3D11DeviceContext* NativeContext(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  thread_local uint64_t not_proxy = 0;
  const uint64_t current = cmd_list->get_native();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(current);
  if (current != not_proxy && !renodx::utils::directx::TryNativeFromReShadeProxy(&native)) {
    not_proxy = current;
  }
  return (native != nullptr && native->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) ? native : nullptr;
}

// The device's shaders and buffers (once); false: not available.
inline bool PrepareDevice(ID3D11Device* device) {
  auto& objects = runtime.objects;
  if (objects.device.Get() == device) return static_cast<bool>(objects.constants);
  objects = {};
  objects.device = device;
  const D3D11_SO_DECLARATION_ENTRY output = {.SemanticName = "TEXCOORD", .SemanticIndex = 0, .ComponentCount = 4};
  constexpr UINT STRIDE = 16;
  const auto compute = [&](std::span<const uint8_t> code, ComPtr<ID3D11ComputeShader>* shader) {
    return SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
  };
  const D3D11_BUFFER_DESC quads_desc = {.ByteWidth = 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  const D3D11_BUFFER_DESC quads_read_desc = {.ByteWidth = 4, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  D3D11_UNORDERED_ACCESS_VIEW_DESC quads_view = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
  quads_view.Buffer.NumElements = 1;
  const D3D11_BUFFER_DESC captured_desc = {.ByteWidth = MAX_VERTICES * 16, .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE};
  D3D11_SHADER_RESOURCE_VIEW_DESC captured_view = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  captured_view.Buffer.NumElements = MAX_VERTICES;
  const D3D11_BUFFER_DESC captured_uv_desc = {.ByteWidth = MAX_VERTICES * 8, .Usage = D3D11_USAGE_DEFAULT,
                                              .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE};
  D3D11_SHADER_RESOURCE_VIEW_DESC captured_uv_view = {.Format = DXGI_FORMAT_R32G32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  captured_uv_view.Buffer.NumElements = MAX_VERTICES;
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 96, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC sun_desc = {.ByteWidth = 32, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC light_constants_desc = {.ByteWidth = ((dlaa_bridge::RT_ALL_LIGHTS * 2) + 1) * 16, .Usage = D3D11_USAGE_DEFAULT,
                                                  .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC water_flags_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  constexpr std::array<uint32_t, 4> WATER_FLAGS = {1u, 0u, 0u, 0u};
  const D3D11_SUBRESOURCE_DATA water_flags_data = {.pSysMem = WATER_FLAGS.data()};
  D3D11_BLEND_DESC multiply_desc = {};
  multiply_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ZERO, .DestBlend = D3D11_BLEND_SRC_COLOR,
                                   .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ZERO,
                                   .DestBlendAlpha = D3D11_BLEND_ONE, .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                                   .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED};
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxLOD = D3D11_FLOAT32_MAX};
  const D3D11_SAMPLER_DESC wrap_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                                        .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
                                        .MaxLOD = D3D11_FLOAT32_MAX};
  const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE};
  const D3D11_DEPTH_STENCIL_DESC depth_desc = {.DepthEnable = FALSE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
                                               .DepthFunc = D3D11_COMPARISON_ALWAYS};
  // The placed draws' rows: four texels a point (rows 0-2, the record).
  const std::array<D3D11_SO_DECLARATION_ENTRY, 4> rows_output = {{
      {.SemanticName = "TEXCOORD", .SemanticIndex = 0, .ComponentCount = 4},
      {.SemanticName = "TEXCOORD", .SemanticIndex = 1, .ComponentCount = 4},
      {.SemanticName = "TEXCOORD", .SemanticIndex = 2, .ComponentCount = 4},
      {.SemanticName = "TEXCOORD", .SemanticIndex = 3, .ComponentCount = 4},
  }};
  constexpr UINT ROWS_STRIDE = 64;
  // The game's own shaders get their stream output at their first capture (game_outputs).
  bool made = std::ranges::all_of(scene_shaders, [&](const SceneShader& shader) {
    auto& vertex_shader = objects.capture_shaders.emplace_back();
    auto& stream_output = objects.capture_outputs.emplace_back();
    auto& rows_shader = objects.rows_shaders.emplace_back();
    auto& rows_output_shader = objects.rows_outputs.emplace_back();
    auto& cut_out_shader = objects.cut_out_shaders.emplace_back();
    auto& cut_out_output = objects.cut_out_outputs.emplace_back();
    auto& faded_rows_shader = objects.faded_rows_shaders.emplace_back();
    auto& faded_rows_output = objects.faded_rows_outputs.emplace_back();
    return (shader.faded_rows_shader.empty()
            || (SUCCEEDED(device->CreateVertexShader(shader.faded_rows_shader.data(), shader.faded_rows_shader.size(), nullptr, faded_rows_shader.GetAddressOf()))
                && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(shader.faded_rows_shader.data(), shader.faded_rows_shader.size(), rows_output.data(),
                                                                          static_cast<UINT>(rows_output.size()), &ROWS_STRIDE, 1,
                                                                          D3D11_SO_NO_RASTERIZED_STREAM, nullptr, faded_rows_output.GetAddressOf()))))
           && (shader.cut_out_shader.empty()
            || (SUCCEEDED(device->CreateVertexShader(shader.cut_out_shader.data(), shader.cut_out_shader.size(), nullptr, cut_out_shader.GetAddressOf()))
                && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(shader.cut_out_shader.data(), shader.cut_out_shader.size(), &output, 1, &STRIDE,
                                                                          1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, cut_out_output.GetAddressOf()))))
           && (shader.capture_shader.empty()
            || (SUCCEEDED(device->CreateVertexShader(shader.capture_shader.data(), shader.capture_shader.size(), nullptr,
                                                     vertex_shader.GetAddressOf()))
                && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(shader.capture_shader.data(), shader.capture_shader.size(), &output,
                                                                          1, &STRIDE, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                                          stream_output.GetAddressOf()))))
           && (shader.rows_shader.empty()
               || (SUCCEEDED(device->CreateVertexShader(shader.rows_shader.data(), shader.rows_shader.size(), nullptr, rows_shader.GetAddressOf()))
                   && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(shader.rows_shader.data(), shader.rows_shader.size(), rows_output.data(),
                                                                             static_cast<UINT>(rows_output.size()), &ROWS_STRIDE, 1,
                                                                             D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                                             rows_output_shader.GetAddressOf()))));
  });
  made = made && SUCCEEDED(device->CreateBuffer(&captured_desc, nullptr, objects.captured.GetAddressOf()))
         && SUCCEEDED(device->CreateShaderResourceView(objects.captured.Get(), &captured_view, objects.captured_srv.GetAddressOf()))
         && (!clip_captures
             || (SUCCEEDED(device->CreateBuffer(&captured_uv_desc, nullptr, objects.captured_uv.GetAddressOf()))
                 && SUCCEEDED(device->CreateShaderResourceView(objects.captured_uv.Get(), &captured_uv_view,
                                                               objects.captured_uv_srv.GetAddressOf()))))
         && compute(__rt_vertices, &objects.vertices_shader) && compute(__rt_skies, &objects.skies_shader)
         && compute(__rt_smoke, &objects.smoke_shader)
         && SUCCEEDED(device->CreateBuffer(&quads_desc, nullptr, objects.smoke_quads.GetAddressOf()))
         && SUCCEEDED(device->CreateUnorderedAccessView(objects.smoke_quads.Get(), &quads_view, objects.smoke_quads_uav.GetAddressOf()))
         && SUCCEEDED(device->CreateBuffer(&quads_read_desc, nullptr, objects.smoke_quads_read.GetAddressOf()))
         && compute(__rt_debug, &objects.debug_shader) && compute(__rt_positions, &objects.positions_shader)
         && compute(__rt_sun, &objects.sun_shader) && compute(__rt_occlusion, &objects.occlusion_shader)
         && compute(__rt_atlas, &objects.atlas_shader) && compute(__rt_ambient_combine, &objects.combine_shader)
         && SUCCEEDED(device->CreateSamplerState(&wrap_desc, objects.linear_wrap.GetAddressOf()))
         && SUCCEEDED(device->CreateBuffer(&light_constants_desc, nullptr, objects.light_constants.GetAddressOf()))
         && SUCCEEDED(device->CreateBuffer(&water_flags_desc, &water_flags_data, objects.water_flags.GetAddressOf()))
         && SUCCEEDED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr,
                                                 objects.fullscreen_shader.GetAddressOf()))
         && SUCCEEDED(device->CreatePixelShader(__rt_ambient_apply.data(), __rt_ambient_apply.size(), nullptr,
                                                objects.ambient_shader.GetAddressOf()))
         && SUCCEEDED(device->CreateBlendState(&multiply_desc, objects.multiply.GetAddressOf()))
         && SUCCEEDED(device->CreateSamplerState(&sampler_desc, objects.linear_clamp.GetAddressOf()))
         && SUCCEEDED(device->CreateRasterizerState(&rasterizer_desc, objects.rasterizer.GetAddressOf()))
         && SUCCEEDED(device->CreateDepthStencilState(&depth_desc, objects.no_depth.GetAddressOf()))
         && SUCCEEDED(device->CreateBuffer(&constants_desc, nullptr, objects.constants.GetAddressOf()))
         && SUCCEEDED(device->CreateBuffer(&sun_desc, nullptr, objects.sun_constants.GetAddressOf()));
  if (std::ranges::any_of(scene_shaders, &SceneShader::tessellated)) {
    const D3D11_BUFFER_DESC tessellated_desc = {.ByteWidth = MAX_TESSELLATED_VERTICES * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
    const D3D11_QUERY_DESC statistics_desc = {.Query = D3D11_QUERY_SO_STATISTICS};
    const auto make_query = [&](ComPtr<ID3D11Query>& query) { return SUCCEEDED(device->CreateQuery(&statistics_desc, query.GetAddressOf())); };
    made = made && SUCCEEDED(device->CreateBuffer(&tessellated_desc, nullptr, objects.tessellated.GetAddressOf()))
           && std::ranges::all_of(objects.tessellated_queries, make_query);
    for (auto& count : objects.terrain_counts) {
      made = made && std::ranges::all_of(count.queries, make_query);
    }
    for (const SceneShader& shader : scene_shaders) {
      if (!shader.tessellated_outside.empty()) {
        made = made && SUCCEEDED(device->CreateHullShader(shader.tessellated_outside.data(), shader.tessellated_outside.size(), nullptr, objects.tessellated_outside.GetAddressOf()));
      }
    }
  }
  const D3D11_QUERY_DESC disjoint_desc = {.Query = D3D11_QUERY_TIMESTAMP_DISJOINT};
  const D3D11_QUERY_DESC stamp_desc = {.Query = D3D11_QUERY_TIMESTAMP};
  for (auto& timing : objects.timings) {
    device->CreateQuery(&disjoint_desc, timing.disjoint.GetAddressOf());
    for (auto& stamp : timing.stamps) device->CreateQuery(&stamp_desc, stamp.GetAddressOf());
  }
  if (!made) {
    renodx::utils::log::w("[Total War] Ray tracing: could not create its shaders or buffers.");
    objects = {};
    objects.device = device;
  }
  return made;
}

// The compute stage's bindings this module uses (t0-t1, u0-u1, b0), kept and put back.
struct ComputeState {
  ComPtr<ID3D11ComputeShader> shader;
  std::array<ID3D11ShaderResourceView*, 2> views = {};
  std::array<ID3D11UnorderedAccessView*, 2> uavs = {};
  ComPtr<ID3D11Buffer> constants;

  explicit ComputeState(ID3D11DeviceContext* native) : native_(native) {
    native->CSGetShader(shader.GetAddressOf(), nullptr, nullptr);
    native->CSGetShaderResources(0, 2, views.data());
    native->CSGetUnorderedAccessViews(0, 2, uavs.data());
    native->CSGetConstantBuffers(0, 1, constants.GetAddressOf());
  }
  ~ComputeState() {
    constexpr std::array<UINT, 2> KEEP_COUNTS = {UINT_MAX, UINT_MAX};
    native_->CSSetShader(shader.Get(), nullptr, 0);
    native_->CSSetUnorderedAccessViews(0, 2, uavs.data(), KEEP_COUNTS.data());
    native_->CSSetShaderResources(0, 2, views.data());
    native_->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
    for (auto* view : views) {
      if (view != nullptr) view->Release();
    }
    for (auto* uav : uavs) {
      if (uav != nullptr) uav->Release();
    }
  }
  ComputeState(const ComputeState&) = delete;
  ComputeState& operator=(const ComputeState&) = delete;

 private:
  ID3D11DeviceContext* native_;
};

// One dispatch with its views and constants (`constants`: up to 96 bytes).
inline void Dispatch(ID3D11DeviceContext* native, ID3D11ComputeShader* shader, std::array<ID3D11ShaderResourceView*, 2> views,
                     std::array<ID3D11UnorderedAccessView*, 2> uavs, const void* constants, UINT constant_bytes, UINT groups_x,
                     UINT groups_y) {
  constexpr std::array<ID3D11ShaderResourceView*, 2> NO_VIEWS = {};
  constexpr std::array<UINT, 2> KEEP_COUNTS = {UINT_MAX, UINT_MAX};
  std::array<uint8_t, 96> padded = {};
  std::memcpy(padded.data(), constants, std::min<UINT>(constant_bytes, 96));
  native->UpdateSubresource(runtime.objects.constants.Get(), 0, nullptr, padded.data(), 0, 0);
  native->CSSetShader(shader, nullptr, 0);
  native->CSSetShaderResources(0, 2, NO_VIEWS.data());
  native->CSSetUnorderedAccessViews(0, 2, uavs.data(), KEEP_COUNTS.data());
  native->CSSetShaderResources(0, 2, views.data());
  native->CSSetConstantBuffers(0, 1, runtime.objects.constants.GetAddressOf());
  native->Dispatch(groups_x, groups_y, 1);
}

// The atlas slice holding the opacity mask `texture` (copied into a free slice when new; the least recently used one
// unused for RELEASE_FRAMES reused); false: none free, or no atlas.
inline bool AtlasSlice(ID3D11DeviceContext* native, ID3D11ShaderResourceView* texture, uint32_t* slice) {
  auto& objects = runtime.objects;
  auto& atlas = dlaa_bridge::bridge.rt_atlas;
  if (!atlas.uav) return false;
  // A new atlas (another device): its slices are empty.
  if (runtime.atlas_generation != dlaa_bridge::bridge.rt_atlas_generation) {
    runtime.atlas_slices.clear();
    runtime.free_slices.clear();
    for (uint32_t free = dlaa_bridge::RT_ATLAS_SLICES; free > 0; --free) runtime.free_slices.push_back(free - 1);
    runtime.atlas_generation = dlaa_bridge::bridge.rt_atlas_generation;
  }
  ComPtr<ID3D11Resource> resource;
  texture->GetResource(resource.GetAddressOf());
  if (const auto found = runtime.atlas_slices.find(resource.Get()); found != runtime.atlas_slices.end()) {
    *slice = found->second.second;
    runtime.slice_used[*slice] = runtime.frame;
    return true;
  }
  if (runtime.free_slices.empty()) {
    const auto oldest = std::ranges::min_element(runtime.atlas_slices, {}, [](const auto& entry) { return runtime.slice_used[entry.second.second]; });
    if (oldest == runtime.atlas_slices.end() || runtime.slice_used[oldest->second.second] + RELEASE_FRAMES >= runtime.frame) return false;
    runtime.free_slices.push_back(oldest->second.second);
    runtime.atlas_slices.erase(oldest);
  }
  *slice = runtime.free_slices.back();
  runtime.free_slices.pop_back();
  runtime.atlas_slices.emplace(resource.Get(), std::pair{resource, *slice});
  runtime.slice_used[*slice] = runtime.frame;
  float mip = 0.f;
  if (ComPtr<ID3D11Texture2D> texture2d; SUCCEEDED(resource.As(&texture2d))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture2d->GetDesc(&desc);
    mip = std::max(std::log2(static_cast<float>(std::max(desc.Width, desc.Height)) / dlaa_bridge::RT_ATLAS_SIZE), 0.f);
  }
  const ComputeState kept(native);
  ComPtr<ID3D11SamplerState> game_sampler;
  native->CSGetSamplers(0, 1, game_sampler.GetAddressOf());
  native->CSSetSamplers(0, 1, objects.linear_wrap.GetAddressOf());
  const std::array<uint32_t, 4> copy = {*slice, std::bit_cast<uint32_t>(mip), dlaa_bridge::RT_ATLAS_SIZE, 0};
  Dispatch(native, objects.atlas_shader.Get(), {texture, nullptr}, {atlas.uav.Get(), nullptr}, copy.data(), sizeof(copy),
           dlaa_bridge::RT_ATLAS_SIZE / 8, dlaa_bridge::RT_ATLAS_SIZE / 8);
  native->CSSetSamplers(0, 1, game_sampler.GetAddressOf());
  return true;
}

// A compact draw's mesh (v16) without its cluster set yet: its index range copied for the CPU (new; Capture copies its
// first instance's captured corners after), those copies read (two frames on, never waiting; MAX_SET_BUILDS a frame) and
// its set made on a worker from its triangles in order of place (SortTriangles; MakeClusterSet took up to milliseconds a
// mesh on the game's busy thread), at a later draw its set and remap index buffer taken. runtime.mutex held.
inline void PrepareClusterSet(ID3D11DeviceContext* native, uint64_t set_key, ID3D11Buffer* index_buffer, DXGI_FORMAT format, UINT offset,
                              UINT first_index, UINT index_count) {
  auto& mesh = runtime.cluster_meshes[set_key];
  mesh.used_frame = runtime.frame;
  if (mesh.failed || mesh.remap) return;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (mesh.making.valid()) {
    if (mesh.making.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    dlaa_bridge::ClusterSetData set = mesh.making.get();
    const D3D11_BUFFER_DESC remap_desc = {.ByteWidth = static_cast<UINT>(set.remap.size() * 4), .Usage = D3D11_USAGE_IMMUTABLE,
                                          .BindFlags = D3D11_BIND_INDEX_BUFFER};
    const D3D11_SUBRESOURCE_DATA remap_data = {.pSysMem = set.remap.data()};
    mesh.failed = set.triangles == 0 || set.data.size() > dlaa_bridge::RT_CLUSTER_DATA_BYTES
                  || FAILED(device->CreateBuffer(&remap_desc, &remap_data, mesh.remap.GetAddressOf()));
    if (mesh.failed) return;
    mesh.data = std::move(set.data);
    mesh.vertices = static_cast<uint32_t>(set.remap.size());
    mesh.clusters = set.clusters;
    mesh.triangles = set.triangles;
    return;
  }
  const UINT index_bytes = (format == DXGI_FORMAT_R16_UINT) ? 2u : 4u;
  if (!mesh.staging) {
    D3D11_BUFFER_DESC desc = {};
    index_buffer->GetDesc(&desc);
    const uint64_t first = uint64_t{offset} + (uint64_t{first_index} * index_bytes);
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = index_count * index_bytes, .Usage = D3D11_USAGE_STAGING,
                                            .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    mesh.failed = (format != DXGI_FORMAT_R16_UINT && format != DXGI_FORMAT_R32_UINT) || desc.Usage == D3D11_USAGE_DYNAMIC
                  || first + staging_desc.ByteWidth > desc.ByteWidth
                  || FAILED(device->CreateBuffer(&staging_desc, nullptr, mesh.staging.GetAddressOf()));
    if (mesh.failed) return;
    mesh.indices = index_buffer;
    const D3D11_BOX range = {.left = static_cast<UINT>(first), .top = 0, .front = 0, .right = static_cast<UINT>(first) + staging_desc.ByteWidth,
                             .bottom = 1, .back = 1};
    native->CopySubresourceRegion(mesh.staging.Get(), 0, 0, 0, 0, index_buffer, 0, &range);
    mesh.copied_frame = runtime.frame;
    return;
  }
  if (runtime.frame < mesh.copied_frame + 2 || runtime.set_builds >= MAX_SET_BUILDS) return;
  if (!mesh.corners && runtime.frame < mesh.copied_frame + CORNER_FRAMES) return;
  // Its corners (copied with its indices or at a later full capture; not yet read back: the next frame).
  std::vector<std::array<float, 4>> corners;
  if (mesh.corners) {
    D3D11_MAPPED_SUBRESOURCE mapped_corners = {};
    const HRESULT read = native->Map(mesh.corners.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped_corners);
    if (read == DXGI_ERROR_WAS_STILL_DRAWING) return;
    if (SUCCEEDED(read)) {
      const auto* first_corner = static_cast<const std::array<float, 4>*>(mapped_corners.pData);
      corners.assign(first_corner, first_corner + index_count);
      native->Unmap(mesh.corners.Get(), 0);
    }
  }
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  const HRESULT result = native->Map(mesh.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
  if (result == DXGI_ERROR_WAS_STILL_DRAWING) return;
  ++runtime.set_builds;
  std::vector<uint32_t> indices(index_count);
  if (SUCCEEDED(result)) {
    if (index_bytes == 2) {
      std::copy_n(static_cast<const uint16_t*>(mapped.pData), index_count, indices.begin());
    } else {
      std::memcpy(indices.data(), mapped.pData, size_t{index_count} * 4);
    }
    native->Unmap(mesh.staging.Get(), 0);
  }
  mesh.staging.Reset();
  mesh.corners.Reset();
  mesh.unsorted = corners.size() < index_count;
  mesh.failed = FAILED(result);
  if (mesh.failed) return;
  mesh.making = std::async(std::launch::async, [indices = std::move(indices), corners = std::move(corners)]() mutable {
    LARGE_INTEGER started;
    LARGE_INTEGER ended;
    QueryPerformanceCounter(&started);
    dlaa_bridge::SortTriangles(indices, corners);
    dlaa_bridge::ClusterSetData set = dlaa_bridge::MakeClusterSet(indices);
    QueryPerformanceCounter(&ended);
    runtime.set_ticks += ended.QuadPart - started.QuadPart;
    return set;
  });
}

// After a scene draw of the main view: a draw not yet in the scene is drawn once more into the stream-output buffer.
// A cascade's draw before it (OnSceneDraw): it needs nothing the draw does, and a later callback's skip of that draw
// would drop a post callback.
template <typename Context>
void Capture(Context& command, const SceneShader& shader) {
  if (frame_trace::Tracing()) {
    frame_trace::Mark(std::format("ray tracing capture: vs {:08X} ps {:08X}", shader.hash,
                                  renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader,
                                                                             &command.bound_state_cache)));
  }
  auto* native = NativeContext(command.cmd_list);
  if (native == nullptr) return;
  const CpuTime timed(runtime.timed ? &runtime.costs.capture_ticks : nullptr);
  // Draws after this frame's trace (past its pass: forward and transparent ones) stay out (Trace and Update run on this
  // thread too).
  if (runtime.traced_frame == runtime.frame) return;
  // The Debug View's scene test: a part of the scene alone.
  const uint32_t test = runtime.settings.scene_test;
  if ((test == 1 && !shader.terrain) || (test == 3 && shader.foliage)) return;
  // Objects and trees: the placed rows; a table without rows (WH3's clip-space captures) has no such mark.
  if (test == 2) {
    const bool has_rows = std::ranges::any_of(scene_shaders, [](const SceneShader& entry) { return !entry.rows_shader.empty(); });
    if (has_rows ? shader.rows_shader.empty() : (shader.terrain || shader.dynamic)) return;
  }
  // Placed grass and leaves are drawn twice a frame by one vertex shader, depth prefill (no target 0) then G-buffer. The
  // G-buffer draw alone: with both, the share paid twice, and past it an instance hidden by one (its swaying copy traced)
  // and shown by the other raced in rt_place's dedupe (ATN's LotR grass: shadows flickering near the camera). Cloth and sails
  // too, should the game prefill them: their texels paid twice for one sheet.
  if (shader.grass && !GrassMet()) return;
  bool thin = shader.thin || (shader.thin_row != nullptr && *shader.thin_row != 0.f);
  const bool cascade_draw = !shader.cascade_keep.empty();
  if (cascade_draw && !(shader.terrain ? runtime.settings.terrain_outside_view : runtime.settings.casters_out_of_view)) return;
  if (!cascade_draw && ((shader.foliage && !shader.rows_shader.empty()) || thin)) {
    ComPtr<ID3D11RenderTargetView> colour_target;
    native->OMGetRenderTargets(1, colour_target.GetAddressOf(), nullptr);
    if (!colour_target) return;
  }
  if (shader.sun_facing && !(shader.far_tree ? runtime.settings.far_tree_shadows : runtime.settings.sprite_shadows)) return;
  const auto& arguments = command.arguments;
  constexpr bool INDEXED = std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DrawIndexedArguments>;
  // A placed draw (a shader with a record version, v14: rigid objects, tree branches and leaves): its BLAS keyed by its
  // geometry alone and built once from its first instance's triangles, placed every frame it is drawn from its
  // instances' rows (the game's per-instance stream, which it repacks every frame it culls: keyed by that, every draw
  // was a new mesh every frame while the camera moved, ATN 2026-10-28).
  // A draw from a dynamic vertex buffer (rewritten whenever the game likes) is captured every frame, as units are: a mesh
  // with a record version by its first one (by its geometry it was a new mesh again and again; its instances' stream is
  // placed), any other by any (the terrain draws its pieces from a dynamic instance stream refilled in the same places:
  // keyed by it, pieces of earlier views stayed in the scene, black bands, pulsing shadows, ATN 2026-10-28).
  bool dynamic = shader.dynamic || (shader.dynamic_row != nullptr && *shader.dynamic_row != 0.f)
                 || (host.placed_row != nullptr && *host.placed_row == 0.f && !shader.rows_shader.empty());
  if (!dynamic) {
    std::array<ID3D11Buffer*, 4> buffers = {};
    std::array<UINT, 4> buffer_strides = {};
    std::array<UINT, 4> buffer_offsets = {};
    const UINT slots = (shader.rows_shader.empty() ? static_cast<UINT>(buffers.size()) : 1u);
    native->IAGetVertexBuffers(0, slots, buffers.data(), buffer_strides.data(), buffer_offsets.data());
    for (UINT slot = 0; slot < slots; ++slot) {
      if (buffers[slot] == nullptr) continue;
      D3D11_BUFFER_DESC buffer_desc = {};
      buffers[slot]->GetDesc(&buffer_desc);
      dynamic = dynamic || buffer_desc.Usage == D3D11_USAGE_DYNAMIC;
      buffers[slot]->Release();
    }
  }
  if (!thin && dynamic && !cascade_draw && shader.thin_sheets_row != nullptr && (*shader.thin_sheets_row != 0.f || runtime.settings.debug_log)) {
    ComPtr<ID3D11RasterizerState> rasterizer;
    native->RSGetState(rasterizer.GetAddressOf());
    D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_BACK};  // none bound: D3D11's default
    if (rasterizer) rasterizer->GetDesc(&rasterizer_desc);
    const uint32_t pixel_shader =
        renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader, &command.bound_state_cache);
    const bool both_sides = rasterizer_desc.CullMode == D3D11_CULL_NONE;
    const bool listed = std::ranges::find(shader.thin_sheets_pixel_shaders, pixel_shader) != shader.thin_sheets_pixel_shaders.end();
    // Log Details: each kind of these draws once (its shaders, its t0's size, its culling), with the row on or off: whether a
    // flag or banner comes from a dynamic vertex buffer, and how it is culled.
    if (runtime.settings.debug_log) {
      ComPtr<ID3D11ShaderResourceView> texture_view;
      native->PSGetShaderResources(0, 1, texture_view.GetAddressOf());
      D3D11_TEXTURE2D_DESC texture_desc = {};
      if (ComPtr<ID3D11Resource> resource; texture_view) {
        texture_view->GetResource(resource.GetAddressOf());
        if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource.As(&texture))) texture->GetDesc(&texture_desc);
      }
      const std::string line = std::format(
          "[Total War] Ray tracing: the banners' row's vs {:08X} from a dynamic vertex buffer with ps {:08X}, t0 {} x {}, culled {}.", shader.hash,
          pixel_shader, texture_desc.Width, texture_desc.Height,
          both_sides ? "none" : ((rasterizer_desc.CullMode == D3D11_CULL_FRONT) ? "front" : "back"));
      const std::unique_lock lock(runtime.mutex);
      if (runtime.thin_sheets_logged.insert(line).second) renodx::utils::log::i(line);
    }
    thin = *shader.thin_sheets_row != 0.f && (both_sides || listed);
    if (thin) {
      // Its G-buffer draw alone, as the cloth above.
      ComPtr<ID3D11RenderTargetView> colour_target;
      native->OMGetRenderTargets(1, colour_target.GetAddressOf(), nullptr);
      if (!colour_target) return;
      // Log Details, once a vertex shader: that the row made its draws thin (its hash's bits turned: beside the left-out lines' marks).
      const std::unique_lock lock(runtime.mutex);
      if (runtime.settings.debug_log && runtime.left_out_logged.insert(shader.hash ^ 0x5448494Eu).second) {
        renodx::utils::log::i(std::format("[Total War] Ray tracing: thin by the banners' row: vs {:08X} with ps {:08X}, from a dynamic vertex buffer, {}.",
                                          shader.hash, pixel_shader, both_sides ? "drawn from both sides" : "its pixel shader listed"));
      }
    }
  }
  const bool placed = !shader.rows_shader.empty() && !dynamic;
  // Placed by its rows or not at all: its capture shader isn't the game's, and streamed out every frame is far too much (61 M indices on the replay).
  if (cascade_draw && !shader.rows_shader.empty() && !placed) return;
  const UINT instances = placed ? std::max(arguments.instance_count, 1u) : 1u;
  UINT vertex_count = 0;
  if constexpr (INDEXED) {
    vertex_count = arguments.index_count * (placed ? 1u : std::max(arguments.instance_count, 1u));
  } else {
    vertex_count = arguments.vertex_count * (placed ? 1u : std::max(arguments.instance_count, 1u));
  }
  const UINT row_texels = placed ? instances * 4u : 0u;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  native->IAGetPrimitiveTopology(&topology);
  if (topology != (shader.tessellated ? D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
      || vertex_count == 0 || vertex_count > MAX_VERTICES) {
    if (vertex_count != 0) {
      const std::unique_lock lock(runtime.mutex);
      if (runtime.left_out_logged.insert(shader.hash).second) {
        renodx::utils::log::i(std::format("[Total War] Ray tracing: left out: vs {:08X}, topology {}, {} vertices.", shader.hash,
                                          static_cast<int>(topology), vertex_count));
      }
    }
    return;
  }
  // Rigid objects seen moving stay out (a mesh's shadow where it stood); placed ones go where they are drawn, every frame.
  bool moving = false;
  if constexpr (INDEXED) {
    moving = !shader.terrain && !dynamic && (shader.moving || (host.moving_indexed != nullptr && host.moving_indexed(native, arguments)));
  } else {
    moving = !shader.terrain && !dynamic && (shader.moving || (host.moving != nullptr && host.moving(native, arguments)));
  }
  if (moving && !placed) return;
  // The fast path (CPU): a static mesh drawn before, known by its short key while that stood for it alone, is only
  // marked drawn (no camera, depth or full identity check). Units, leaves and placed draws take their own paths.
  const bool short_path = !dynamic && !shader.foliage && !placed;
  uint64_t short_key = 14695981039346656037ull;
  if (short_path) {
    ID3D11Buffer* first_vertices = nullptr;
    UINT first_stride = 0;
    UINT first_offset = 0;
    ID3D11Buffer* draw_indices = nullptr;
    DXGI_FORMAT draw_index_format = DXGI_FORMAT_UNKNOWN;
    UINT draw_index_offset = 0;
    native->IAGetVertexBuffers(0, 1, &first_vertices, &first_stride, &first_offset);
    native->IAGetIndexBuffer(&draw_indices, &draw_index_format, &draw_index_offset);
    const auto mix_short = [&short_key](uint64_t value) { short_key = (short_key ^ value) * 1099511628211ull; };
    mix_short(shader.hash);
    if constexpr (INDEXED) {
      for (const uint64_t value : {uint64_t{arguments.index_count}, uint64_t{arguments.instance_count}, uint64_t{arguments.first_index},
                                   uint64_t{static_cast<uint32_t>(arguments.vertex_offset)}, uint64_t{arguments.first_instance}}) {
        mix_short(value);
      }
    } else {
      for (const uint64_t value : {uint64_t{arguments.vertex_count}, uint64_t{arguments.instance_count}, uint64_t{arguments.first_vertex},
                                   uint64_t{arguments.first_instance}}) {
        mix_short(value);
      }
    }
    mix_short(reinterpret_cast<uintptr_t>(first_vertices));
    mix_short((uint64_t{first_stride} << 32) | first_offset);
    mix_short(reinterpret_cast<uintptr_t>(draw_indices));
    mix_short((uint64_t{static_cast<uint32_t>(draw_index_format)} << 32) | draw_index_offset);
    if (first_vertices != nullptr) first_vertices->Release();
    if (draw_indices != nullptr) draw_indices->Release();
    const std::unique_lock lock(runtime.mutex);
    if (const auto entry = runtime.short_keys.find(short_key); entry != runtime.short_keys.end() && !entry->second.ambiguous) {
      if (const auto known = runtime.known.find(entry->second.key); known != runtime.known.end()) {
        if (known->second.frame != runtime.frame) {
          known->second.frame = runtime.frame;
          if (runtime.drawn.size() < dlaa_bridge::RT_DRAWN) runtime.drawn.push_back(entry->second.key);
        }
        return;
      }
    }
  }
  // A leaf's opacity mask: its pixel shader's diffuse map; a rigid mesh's, when the game's shadow pass alpha-tests it.
  ComPtr<ID3D11ShaderResourceView> mask;
  // A sheet made thin by its row stays whole: cut out, it would block the sun as a fence does.
  const bool sheet_by_row = thin && !shader.thin && shader.thin_row == nullptr;
  if ((shader.foliage || !shader.cut_out_shader.empty()) && !shader.clip_cut_outs && !sheet_by_row) native->PSGetShaderResources(shader.mask_slot, 1, mask.GetAddressOf());
  bool cut_out = false;
  if (shader.clip_cut_outs && (shader.cut_out_row == nullptr || *shader.cut_out_row != 0.f)) {
    const uint32_t pixel_shader = renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader, &command.bound_state_cache);
    const auto found = std::ranges::lower_bound(cut_out_masks, pixel_shader, {}, &PixelMask::pixel_shader);
    if (found != cut_out_masks.end() && found->pixel_shader == pixel_shader && found->uv_semantic_index == shader.uv_semantic_index
        && (found->row == nullptr || *found->row != 0.f)) {
      native->PSGetShaderResources(found->slot, 1, mask.GetAddressOf());
      cut_out = static_cast<bool>(mask);
      if (runtime.settings.debug_log) {
        const std::unique_lock lock(runtime.mutex);
        if (runtime.left_out_logged.insert(pixel_shader).second) {
          renodx::utils::log::i(std::format("[Total War] Ray tracing: cut out: vs {:08X} with ps {:08X} by its t{}.", shader.hash, pixel_shader, found->slot));
        }
      }
    }
  } else if (!shader.foliage && mask) {
    if (shader.dynamic) {
      cut_out = std::ranges::binary_search(
          cut_out_pixel_shaders,
          renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader, &command.bound_state_cache));
    } else {
      ComPtr<ID3D11Resource> mask_resource;
      mask->GetResource(mask_resource.GetAddressOf());
      const std::unique_lock lock(runtime.mutex);
      cut_out = runtime.cut_out_textures.contains(mask_resource.Get());
    }
    if (!cut_out) mask.Reset();
  }
  bool foliage = shader.foliage || cut_out;
  // The draw's identity: its buffers, ranges, textures and constant buffers (but the camera's at b0); a placed draw's its
  // geometry alone (its ranges, first vertex buffer and index buffer) and its mask (one grass mesh might be drawn with
  // more than one: its BLAS's alpha test is its first capture's).
  std::array<ID3D11Buffer*, 4> constant_buffers = {};
  std::array<ID3D11ShaderResourceView*, 4> textures = {};
  std::array<ID3D11Buffer*, 4> vertex_buffers = {};
  std::array<UINT, 4> strides = {};
  std::array<UINT, 4> offsets = {};
  ID3D11Buffer* index_buffer = nullptr;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  // Their ranges too (D3D11.1: one buffer can hold many draws' constants).
  std::array<UINT, 4> first_constants = {};
  std::array<UINT, 4> constant_counts = {};
  if (!placed) {
    if (ComPtr<ID3D11DeviceContext1> native1; SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
      native1->VSGetConstantBuffers1(1, static_cast<UINT>(constant_buffers.size()), constant_buffers.data(), first_constants.data(),
                                     constant_counts.data());
    } else {
      native->VSGetConstantBuffers(1, static_cast<UINT>(constant_buffers.size()), constant_buffers.data());
    }
    native->VSGetShaderResources(0, static_cast<UINT>(textures.size()), textures.data());
  }
  native->IAGetVertexBuffers(0, (placed ? 1u : static_cast<UINT>(vertex_buffers.size())), vertex_buffers.data(), strides.data(), offsets.data());
  native->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
  uint64_t key = 14695981039346656037ull;
  const auto mix = [&key](uint64_t value) { key = (key ^ value) * 1099511628211ull; };
  mix(shader.key_family != 0u ? shader.key_family : shader.hash);
  if constexpr (INDEXED) {
    for (const uint64_t value : {uint64_t{arguments.index_count}, uint64_t{(placed ? 0u : arguments.instance_count)}, uint64_t{arguments.first_index},
                                 uint64_t{static_cast<uint32_t>(arguments.vertex_offset)}, uint64_t{(placed ? 0u : arguments.first_instance)}}) {
      mix(value);
    }
  } else {
    for (const uint64_t value : {uint64_t{arguments.vertex_count}, uint64_t{(placed ? 0u : arguments.instance_count)}, uint64_t{arguments.first_vertex},
                                 uint64_t{(placed ? 0u : arguments.first_instance)}}) {
      mix(value);
    }
  }
  for (size_t slot = 0; slot < constant_buffers.size(); ++slot) {
    mix(reinterpret_cast<uintptr_t>(constant_buffers[slot]));
    mix(first_constants[slot]);
  }
  for (auto* texture : textures) mix(reinterpret_cast<uintptr_t>(texture));
  for (size_t slot = 0; slot < vertex_buffers.size(); ++slot) {
    mix(reinterpret_cast<uintptr_t>(vertex_buffers[slot]));
    mix((uint64_t{strides[slot]} << 32) | offsets[slot]);
  }
  mix(reinterpret_cast<uintptr_t>(index_buffer));
  mix((uint64_t{static_cast<uint32_t>(index_format)} << 32) | index_offset);
  mix(reinterpret_cast<uintptr_t>(mask.Get()));
  // A dynamic indexed draw can be compact (v16; the terrain's too: a vertex a texel, not three a triangle), so can a placed
  // draw of leaves (its trees sway: their leaves where drawn also captured every frame, in the wind): its mesh's key (its
  // index buffer and range) and that buffer.
  const bool compactable = INDEXED
                           && (placed ? (shader.foliage && !shader.capture_shader.empty() && !shader.still && !(shader.grass && !runtime.settings.grass_sway))
                                      : dynamic)
                           && !shader.sun_facing && !shader.tessellated && !cascade_draw && index_buffer != nullptr && dlaa_bridge::CompactState() != 0;
  uint64_t set_key = 14695981039346656037ull;
  ComPtr<ID3D11Buffer> compact_indices;
  if constexpr (INDEXED) {
    for (const uint64_t value : {uint64_t{reinterpret_cast<uintptr_t>(index_buffer)}, (uint64_t{static_cast<uint32_t>(index_format)} << 32) | index_offset,
                                 uint64_t{arguments.first_index}, uint64_t{arguments.index_count}}) {
      set_key = (set_key ^ value) * 1099511628211ull;
    }
    if (compactable) compact_indices = index_buffer;
  }
  // The Get calls added references; only compared.
  const auto release = [](auto* object) {
    if (object != nullptr) object->Release();
  };
  std::ranges::for_each(constant_buffers, release);
  std::ranges::for_each(textures, release);
  std::ranges::for_each(vertex_buffers, release);
  release(index_buffer);

  // A mesh in the scene is marked drawn; a placed one (also one captured earlier this frame) is placed where this draw's
  // instances are (their rows only: a placement; marked drawn once placed, so one left out is forgotten and captured
  // again as the helper releases it), one not seen moving only in its turn, every PLACE_TURNS frames (the helper keeps
  // its instances between), every frame while the camera moves: its level of detail changes then, and a mesh placed in its
  // turn dropped the old level up to 3 frames before the new one's turn (ATN: shadows gone and back at each change).
  bool place = false;
  size_t full_share = SHARE_NAMES.size();  // the first of its shares that is full (none: their count)
  const bool unit = shader.dynamic && !shader.grass && !thin && !shader.tessellated;  // skinned soldiers and horses
  // Compact (v16): its mesh's blocks in place of its triangles, once its set is made and can go with this frame's trace;
  // what it takes of the vertex texture and the helper's frame. A placed draw of leaves sways (`sway`): its blocks besides
  // its rows, within the trees' share.
  bool blocks = false;
  bool compact = false;
  bool sway = false;
  const UINT draw_instances = std::max(arguments.instance_count, 1u);
  UINT block_texels = 0;
  UINT block_triangles = 0;
  UINT block_clusters = 0;
  UINT texels = vertex_count;
  UINT triangles = vertex_count / 3;
  UINT clusters = (triangles + dlaa_bridge::RT_CAPTURE_CLUSTER_TRIANGLES - 1) / dlaa_bridge::RT_CAPTURE_CLUSTER_TRIANGLES;
  {
    const std::unique_lock lock(runtime.mutex);
    if (!Enabled() || !dlaa_bridge::bridge.ray_tracing) return;
    if (short_path) {
      const auto [entry, added] = runtime.short_keys.try_emplace(short_key, Runtime::ShortKey{.key = key});
      if (!added && entry->second.key != key) entry->second.ambiguous = true;
    }
    if (const auto mesh = (compactable ? runtime.cluster_meshes.find(set_key) : runtime.cluster_meshes.end());
        mesh != runtime.cluster_meshes.end() && mesh->second.remap) {
      mesh->second.used_frame = runtime.frame;
      blocks = mesh->second.sent || std::ranges::find(runtime.new_sets, set_key) != runtime.new_sets.end()
               || (runtime.new_sets.size() < dlaa_bridge::RT_NEW_SETS
                   && runtime.new_set_bytes + ((mesh->second.data.size() + 15) / 16 * 16) <= dlaa_bridge::RT_CLUSTER_DATA_BYTES);
      block_texels = mesh->second.vertices * draw_instances;
      block_triangles = mesh->second.triangles * draw_instances;
      block_clusters = mesh->second.clusters * draw_instances;
    }
    compact = blocks && dynamic;
    if (compact) {
      texels = block_texels;
      triangles = block_triangles;
      clusters = block_clusters;
    }
    if (dynamic) {
      const std::array<bool, SHARE_NAMES.size()> full = {
          runtime.dynamic_vertices + texels + runtime.tessellated_reserved > MAX_DYNAMIC_VERTICES,
          runtime.dynamic_triangles + triangles + (runtime.tessellated_reserved / 3) > dlaa_bridge::RT_DYNAMIC_TRIANGLES,
          runtime.dynamic_clusters + clusters + (runtime.tessellated_reserved / 3 / dlaa_bridge::RT_CAPTURE_CLUSTER_TRIANGLES) > dlaa_bridge::RT_DYNAMIC_CLUSTERS,
          shader.grass && runtime.grass_vertices + (triangles * 3) > runtime.alpha_corners / 2,
          unit
              && (runtime.unit_vertices + texels > MAX_DYNAMIC_VERTICES / 4 * 3
                  || runtime.unit_triangles + triangles > dlaa_bridge::RT_DYNAMIC_TRIANGLES / 4 * 3
                  || runtime.unit_clusters + clusters > dlaa_bridge::RT_DYNAMIC_CLUSTERS / 4 * 3),
      };
      full_share = static_cast<size_t>(std::ranges::find(full, true) - full.begin());
    }
    if (const auto known = runtime.known.find(key); !dynamic && known != runtime.known.end()) {
      // A leaf's slice stays in use while it is drawn.
      if (mask) {
        ComPtr<ID3D11Resource> mask_resource;
        mask->GetResource(mask_resource.GetAddressOf());
        if (const auto found = runtime.atlas_slices.find(mask_resource.Get()); found != runtime.atlas_slices.end()) {
          runtime.slice_used[found->second.second] = runtime.frame;
        }
      }
      if (!placed) {
        if (known->second.frame != runtime.frame) {
          known->second.frame = runtime.frame;
          if (runtime.drawn.size() < dlaa_bridge::RT_DRAWN) runtime.drawn.push_back(key);
        }
        return;
      }
      Runtime::Known& seen = known->second;
      if (seen.counted_frame != runtime.frame) {
        seen.drawn_before_trace = seen.traced_frame == seen.counted_frame && seen.traced_instances == seen.instances;
        seen.all_draws = seen.instances != seen.last_instances;
        seen.last_instances = seen.instances;
        seen.instances = 0;
        seen.placed = 0;
        seen.counted_frame = runtime.frame;
      }
      seen.instances += instances;
      // Its leaves sway within the trees' share, grass within its own (placed every frame then, hidden where drawn; past it
      // placed still: grass was left out, whole fields without shadows with Grass Always Drawn, ATN 2026-10-29); else not
      // its turn, its instances no more than last frame's (none came into sight), they did not change the frame before.
      sway = blocks
             && (shader.grass ? runtime.settings.grass_sway && runtime.grass_vertices + (block_triangles * 3) <= runtime.alpha_corners / 2
                              : (runtime.tree_vertices + block_texels <= MAX_TREE_VERTICES
                                 && runtime.tree_corners + (block_triangles * 3) <= runtime.alpha_corners - (runtime.alpha_corners / 2)))
             && runtime.dynamic_vertices + block_texels <= MAX_DYNAMIC_VERTICES
             && runtime.dynamic_triangles + block_triangles <= dlaa_bridge::RT_DYNAMIC_TRIANGLES
             && runtime.dynamic_clusters + block_clusters <= dlaa_bridge::RT_DYNAMIC_CLUSTERS;
      // One whose cluster set waits for its triangles' order (first captured before the helper took compact draws: its set
      // made unsorted, each cluster over a whole grass patch) is captured once more in full (its corners: PrepareClusterSet).
      const auto set = compactable ? runtime.cluster_meshes.find(set_key) : runtime.cluster_meshes.end();
      const bool corners_wanted = set != runtime.cluster_meshes.end() && set->second.staging && !set->second.corners;
      if (!sway && !moving && !corners_wanted && !seen.all_draws && !runtime.camera_moving && seen.instances <= seen.last_instances
          && (key + runtime.frame) % PLACE_TURNS != 0) {
        // Drawn all the same (RT_DRAWN_PLACED: its kept instances stay).
        if (seen.frame != runtime.frame && runtime.drawn.size() < dlaa_bridge::RT_DRAWN) runtime.drawn.push_back(key);
        seen.frame = runtime.frame;
        return;
      }
      place = !corners_wanted;
    } else if (!shader.tessellated && runtime.captured_keys.contains(key)) {
      if (!placed) return;
      place = true;
    }
  }
  // The main view (not the UI's small 3D views, whose models stand elsewhere in the world): its camera and its depth at
  // the display's size, single-sampled.
  if (!cascade_draw && host.main_view != nullptr && !host.main_view(native)) return;
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(0, nullptr, depth_view.GetAddressOf());
  Camera camera;
  if (!depth_view) return;
  // A cascade's draw takes the main view's matrix from this frame's main-view captures, below.
  if (!cascade_draw) {
    if (host.capture_camera != nullptr) {
      if (!host.capture_camera(native, &camera)) return;
    } else {
      ComPtr<ID3D11Buffer> camera_buffer;
      native->VSGetConstantBuffers(host.camera_slot, 1, camera_buffer.GetAddressOf());
      if (!camera_buffer || !host.camera(camera_buffer.Get(), &camera)) return;
    }
  }
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  if (FAILED(depth_resource.As(&depth_texture))) return;
  depth_texture->GetDesc(&depth_desc);
  if (cascade_draw) {
    const bool cascades = (host.cascade_target != nullptr) ? host.cascade_target(depth_desc)
                                                           : (depth_desc.ArraySize == 4 && depth_desc.Width == depth_desc.Height);
    if (!cascades) return;
  } else if (depth_desc.SampleDesc.Count != 1 || depth_desc.Width != dlaa_bridge::bridge.width || depth_desc.Height != dlaa_bridge::bridge.height) {
    return;
  }

  const std::unique_lock lock(runtime.mutex);
  if (cascade_draw) {
    if (runtime.main_view_frame != runtime.frame && !(host.cascade_last_view && runtime.main_view_frame + 1 == runtime.frame)) return;
  } else {
    runtime.main_view_projection = camera.view_projection;
    runtime.main_view_frame = runtime.frame;
  }
  if (full_share < SHARE_NAMES.size()) {
    runtime.costs.left_out += texels;
    runtime.costs.left_out_shaders[shader.hash][full_share] += texels;
    return;
  }
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!PrepareDevice(device.Get())) return;
  uint32_t slice = 0;
  // A unit stays solid (left out, it would lose its shadow) without room for its mask, or with Leaf Micromaps off (rays that
  // skip alpha tests would pass through it for good), and leaves a quarter of the atlas to leaves and rigid cut-outs (a slice
  // in use is never reused; without one they leave the scene).
  if (cut_out && (shader.dynamic || shader.clip_cut_outs)) {
    ComPtr<ID3D11Resource> mask_resource;
    mask->GetResource(mask_resource.GetAddressOf());
    if (!runtime.settings.micromaps || runtime.tree_corners + (triangles * 3) > runtime.alpha_corners - (runtime.alpha_corners / 2)
        || (!runtime.atlas_slices.contains(mask_resource.Get()) && runtime.atlas_slices.size() >= dlaa_bridge::RT_ATLAS_SLICES / 4 * 3)
        || !AtlasSlice(native, mask.Get(), &slice)) {
      cut_out = false;
      foliage = false;
      mask.Reset();
      // Log Details, once a vertex shader (its hash's complement: beside the left-out lines' own marks).
      if (runtime.settings.debug_log && runtime.left_out_logged.insert(~shader.hash).second) {
        renodx::utils::log::i(std::format("[Total War] Ray tracing: solid, its cut-outs without room: vs {:08X}, {} triangles ({} mask textures held of {}).",
                                          shader.hash, triangles, runtime.atlas_slices.size(), dlaa_bridge::RT_ATLAS_SLICES));
      }
    }
  }
  // A mesh without its cluster set (v16): its indices read back for one, its triangles captured meanwhile.
  if constexpr (INDEXED) {
    if (compactable && !blocks) {
      PrepareClusterSet(native, set_key, compact_indices.Get(), index_format, index_offset, arguments.first_index, arguments.index_count);
    }
  }
  if (runtime.settings.debug_log && runtime.logged_draws[shader.hash]++ < 2) {
    std::array<ID3D11Buffer*, 3> buffers = {};
    std::array<UINT, 3> buffer_strides = {};
    std::array<UINT, 3> buffer_offsets = {};
    native->IAGetVertexBuffers(0, static_cast<UINT>(buffers.size()), buffers.data(), buffer_strides.data(), buffer_offsets.data());
    std::string described;
    for (size_t slot = 0; slot < buffers.size(); ++slot) {
      if (buffers[slot] == nullptr) continue;
      D3D11_BUFFER_DESC buffer_desc = {};
      buffers[slot]->GetDesc(&buffer_desc);
      described += std::format(", VB{} stride {} offset {} {}", slot, buffer_strides[slot], buffer_offsets[slot],
                               (buffer_desc.Usage == D3D11_USAGE_DYNAMIC ? "dynamic" : "static"));
      buffers[slot]->Release();
    }
    // A leaf's opacity mask (the atlas takes its alpha).
    if (ComPtr<ID3D11Resource> mask_resource; mask) {
      mask->GetResource(mask_resource.GetAddressOf());
      if (ComPtr<ID3D11Texture2D> mask_texture; SUCCEEDED(mask_resource.As(&mask_texture))) {
        D3D11_TEXTURE2D_DESC mask_desc = {};
        mask_texture->GetDesc(&mask_desc);
        described += std::format(", mask {}x{} format {}", mask_desc.Width, mask_desc.Height, static_cast<int>(mask_desc.Format));
      }
    }
    renodx::utils::log::i(std::format("[Total War] Ray tracing: placed vs {:08X}: {} vertices x {} instances from {}{}{}.", shader.hash,
                                      vertex_count, arguments.instance_count, arguments.first_instance, described,
                                      (moving ? ", moving" : "")));
  }
  // A dynamic draw right after one of its kind (with its opacity mask) joins its capture, the helper takes them as one (the
  // frame's RT_CAPTURES ran out with the terrain, units and grass all captured every frame).
  const uint32_t flags = (shader.terrain ? dlaa_bridge::RT_TERRAIN : 0u) | (dynamic ? dlaa_bridge::RT_DYNAMIC : 0u)
                         | (foliage ? dlaa_bridge::RT_FOLIAGE : 0u) | (placed ? dlaa_bridge::RT_INSTANCED : 0u)
                         | (thin ? dlaa_bridge::RT_THIN : 0u)
                         // Sprite soldiers and far trees block the sun's rays as grass does (leaves let some through).
                         | ((shader.grass || shader.sun_facing) ? dlaa_bridge::RT_GRASS : 0u)
                         | (shader.sun_facing ? dlaa_bridge::RT_SUN_ONLY : 0u) | (shader.far_tree ? dlaa_bridge::RT_SCATTERED : 0u)
                         // A rigid mesh the game's shadow pass alpha-tests too (a fence, a thatched house): as leaves a house let a
                         // quarter of the sun through under Light Through Leaves.
                         | (cut_out ? dlaa_bridge::RT_SOLID_CUT_OUT : 0u);
  // Placed meshes, and SceneShader::cascade_world's world positions, in a clip-space game: the world part.
  const bool world_part = clip_captures && (placed || (cascade_draw && shader.cascade_world));
  uint32_t& next_vertex = (world_part ? runtime.world_vertices : runtime.captured_vertices);
  const UINT end_vertex = ((world_captures && !world_part) ? WORLD_BASE : MAX_VERTICES);
  dlaa_bridge::RtCapture* joined = (runtime.captures.empty() ? nullptr : &runtime.captures.back());
  if (!dynamic || joined == nullptr || joined->flags != flags || joined->first_vertex + joined->vertex_count != next_vertex
      || joined->vertex_count % 3 != 0) {
    joined = nullptr;
  }
  // No room for the swaying copy: its placed copies alone.
  sway = sway && runtime.compact.size() < dlaa_bridge::RT_COMPACT_DRAWS && next_vertex + block_texels + row_texels <= end_vertex;
  if (place ? (runtime.placements.size() >= dlaa_bridge::RT_PLACEMENTS || next_vertex + row_texels > end_vertex)
            : (((compact ? runtime.compact.size() >= dlaa_bridge::RT_COMPACT_DRAWS
                         : ((joined == nullptr || foliage) && runtime.captures.size() >= dlaa_bridge::RT_CAPTURES)))
               || next_vertex + texels + row_texels + (world_part ? 0u : runtime.tessellated_reserved) > end_vertex)) {
    if (dynamic) runtime.costs.no_room += texels;
    return;
  }
  auto& objects = runtime.objects;
  if (shader.tessellated && (!objects.tessellated || runtime.tessellated_draws == objects.tessellated_queries.size())) return;
  // A tessellated draw through the game's own geometry shader isn't the surface: WH3's second pass of the terrain (HS C51FEA61,
  // GS 33D642AB) tessellates coarsely and its GS only emits skirts down from the patches' edges. Streamed out past that GS, its
  // coarse hills lay over the real ones: black patches in every ray.
  if (shader.tessellated && renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::geometry_shader, &command.bound_state_cache) != 0u) return;
  // The capture version (by the shader's place in the list: listed shaders have them, and record versions), else the
  // game's own: its stream output (SV_Position; its foliage's UV into the second buffer) made once from its code (null:
  // it could not be).
  const bool clip_space = shader.capture_shader.empty() || (clip_captures && !placed);
  const bool clip_uv = clip_space && foliage && shader.uv_semantic != nullptr;
  // By its hash where CaptureDraw made a copy (a list of pixel shaders).
  const size_t listed_index = (shader.pixel_masks.empty() ? static_cast<size_t>(&shader - scene_shaders.data())
                                                         : static_cast<size_t>(std::ranges::find(scene_shaders, shader.hash, &SceneShader::hash) - scene_shaders.begin()));
  const size_t index = ((placed || !clip_space) ? listed_index : 0u);
  if ((placed && index >= objects.rows_shaders.size()) || (!clip_space && index >= objects.capture_shaders.size())) return;
  // Leaves wait for a slice of the atlas (none: not in the scene this frame).
  if (!place && foliage && (!mask || !AtlasSlice(native, mask.Get(), &slice))) return;
  sway = sway && mask && AtlasSlice(native, mask.Get(), &slice);
  if (joined != nullptr && foliage && runtime.slices.back() != slice) {
    joined = nullptr;
  }
  ID3D11VertexShader* capture_vertex_shader = nullptr;
  ID3D11GeometryShader* stream_output = nullptr;
  // Host::scene_draw's capture version, in clip space as the game's; a SceneShader::cascade_world cascade draw's, into its
  // cascade_keep.
  const bool own_capture = !shader.capture_shader.empty() && !shader.tessellated && (cascade_draw ? shader.cascade_world : host.scene_draw != nullptr);
  const bool cascade_world = cascade_draw && (host.cascade_world || shader.cascade_world);
  if (!place && clip_space) {
    const uint32_t streamed_shader =
        shader.tessellated ? renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::domain_shader, &command.bound_state_cache)
                           : shader.hash;
    const auto [made, first] = (clip_uv ? objects.game_uv_outputs : objects.game_outputs).try_emplace(streamed_shader);
    if (first) {
      const auto code = game_code.find(streamed_shader);
      const std::array<D3D11_SO_DECLARATION_ENTRY, 2> entries = {{
          {.SemanticName = "SV_POSITION", .SemanticIndex = 0, .ComponentCount = 4},
          {.SemanticName = shader.uv_semantic, .SemanticIndex = shader.uv_semantic_index, .ComponentCount = 2, .OutputSlot = 1},
      }};
      constexpr std::array<UINT, 2> STRIDES = {16, 8};
      const UINT outputs = clip_uv ? 2 : 1;
      std::span<const uint8_t> streamed_code = shader.tessellated_keep;
      if (cascade_draw) {
        streamed_code = (clip_uv ? shader.cascade_keep_uv : shader.cascade_keep);
      }
      if (own_capture) {
        if (!cascade_draw) streamed_code = shader.capture_shader;
        if (FAILED(device->CreateVertexShader(shader.capture_shader.data(), shader.capture_shader.size(), nullptr,
                                              objects.game_capture_shaders[streamed_shader].ReleaseAndGetAddressOf()))) {
          streamed_code = {};
        }
      }
      if (streamed_code.empty() && code != game_code.end() && !own_capture) streamed_code = code->second;
      if (streamed_code.empty()
          || FAILED(device->CreateGeometryShaderWithStreamOutput(streamed_code.data(), streamed_code.size(), entries.data(), outputs,
                                                                  STRIDES.data(), outputs, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                                  made->second.GetAddressOf()))) {
        renodx::utils::log::w(std::format("[Total War] Ray tracing: no stream output for {} shader {:08X}{}.", shader.tessellated ? "domain" : "vertex", streamed_shader,
                                          clip_uv ? " with its UV" : ""));
      } else if (shader.tessellated) {
        renodx::utils::log::i(std::format("[Total War] Ray tracing: vs {:08X} streamed out through domain shader {:08X}.", shader.hash, streamed_shader));
      }
    }
    if (!made->second) return;
    stream_output = made->second.Get();
    if (own_capture) capture_vertex_shader = objects.game_capture_shaders[streamed_shader].Get();
  } else if (!place || sway) {
    capture_vertex_shader = (cut_out ? objects.cut_out_shaders : objects.capture_shaders)[index].Get();
    stream_output = (cut_out ? objects.cut_out_outputs : objects.capture_outputs)[index].Get();
  }

  // Before anything of the game's is unbound: these returns leave its state alone.
  ComPtr<ID3D11DeviceContext1> native1;
  if (cascade_draw) {
    if (FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
    if (!objects.cascade_camera) {
      // To c35: the flags' time, 0 here (a still sheet casts).
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 36 * 4 * sizeof(float), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      if (FAILED(device->CreateBuffer(&desc, nullptr, objects.cascade_no_view.GetAddressOf()))
          || FAILED(device->CreateBuffer(&desc, nullptr, objects.cascade_camera.GetAddressOf()))) {
        return;
      }
      runtime.cascade_camera_frame = 0;
    }
  }

  // The same draw into the stream output only (not rasterized): a new mesh's triangles (a placed one's first instance's;
  // a compact or swaying draw's blocks), then a placed draw's instances' rows (one point per instance: its first vertex);
  // the game's shaders and outputs put back.
  ComPtr<ID3D11VertexShader> game_vertex_shader;
  ComPtr<ID3D11GeometryShader> game_geometry_shader;
  ComPtr<ID3D11PixelShader> game_pixel_shader;
  ComPtr<ID3D11Buffer> game_output;
  native->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
  native->GSGetShader(game_geometry_shader.GetAddressOf(), nullptr, nullptr);
  native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
  native->SOGetTargets(1, game_output.GetAddressOf());
  native->PSSetShader(nullptr, nullptr, 0);
  ComPtr<ID3D11Buffer> game_camera;
  UINT game_camera_first = 0;
  UINT game_camera_count = 0;
  if (cascade_draw) {
    if (runtime.cascade_camera_frame != runtime.frame) {
      std::array<float, 36 * 4> cascade_constants = {};
      std::ranges::copy(runtime.main_view_projection, cascade_constants.begin() + (10 * 4));
      native->UpdateSubresource(objects.cascade_camera.Get(), 0, nullptr, cascade_constants.data(), 0, 0);
      runtime.cascade_camera_frame = runtime.frame;
    }
    // Host::cascade_world: the game's shadow vertex shader as bound, the main view's matrix for the geometry shader (the game's b0 there
    // put back too, though its shadow pass draws without one).
    if (cascade_world) {
      native1->GSGetConstantBuffers1(0, 1, game_camera.GetAddressOf(), &game_camera_first, &game_camera_count);
      native->GSSetConstantBuffers(0, 1, objects.cascade_camera.GetAddressOf());
    } else {
      native1->VSGetConstantBuffers1(0, 1, game_camera.GetAddressOf(), &game_camera_first, &game_camera_count);
      native->VSSetConstantBuffers(0, 1, objects.cascade_camera.GetAddressOf());
    }
  }
  if (!place || sway) {
    // A tessellated draw's triangles: how many is known only after it, so into their own buffer, after the frame's others.
    const std::array<ID3D11Buffer*, 2> targets = {shader.tessellated ? objects.tessellated.Get() : objects.captured.Get(), objects.captured_uv.Get()};
    const std::array<UINT, 2> stream_offsets = {shader.tessellated ? (runtime.tessellated_draws == 0 ? 0u : UINT_MAX) : (next_vertex * 16),
                                                next_vertex * 8};
    if (!clip_space || own_capture) native->VSSetShader(capture_vertex_shader, nullptr, 0);
    native->GSSetShader(stream_output, nullptr, 0);
    native->SOSetTargets(clip_uv ? 2 : 1, targets.data(), stream_offsets.data());
    // A capture version turned to the sun reads rt_sun; the game's shadow vertex shader (a cascade draw) turns its cards to the sun itself,
    // and its t0 stays as the game set it for the next draw.
    if (shader.sun_facing && !cascade_draw) native->VSSetShaderResources(shader.sun_slot, 1, dlaa_bridge::bridge.rt_sun.srv.GetAddressOf());
    ComPtr<ID3D11ShaderResourceView> game_keep_view;
    ComPtr<ID3D11Buffer> game_keep_constants;
    if (!shader.tessellated_keep.empty()) {
      native->GSGetShaderResources(0, 1, game_keep_view.GetAddressOf());
      native->GSGetConstantBuffers(1, 1, game_keep_constants.GetAddressOf());
      ComPtr<ID3D11ShaderResourceView> keep_view;
      ComPtr<ID3D11Buffer> keep_constants;
      native->PSGetShaderResources(shader.mask_slot, 1, keep_view.GetAddressOf());
      native->DSGetConstantBuffers(1, 1, keep_constants.GetAddressOf());
      native->GSSetShaderResources(0, 1, keep_view.GetAddressOf());
      native->GSSetConstantBuffers(1, 1, keep_constants.GetAddressOf());
    }
    // The count read later: this frame's triangles land on empty ones, as many as the last count known and an eighth (the capture's
    // size at the trace, which no longer knows how many were written), and their counts go to a set of queries the GPU has answered.
    if (shader.tessellated && runtime.tessellated_draws == 0) {
      runtime.terrain_later = (runtime.settings.terrain_count_later || CheapFogAlone(runtime.settings)) && runtime.terrain_known_corners != 0
                              && runtime.terrain_known_frame + TERRAIN_COUNT_FRAMES >= runtime.frame;
    }
    if (shader.tessellated && runtime.terrain_later && runtime.tessellated_draws == 0) {
      if (!objects.tessellated_empty) {
        const std::vector<uint32_t> empty(size_t{TERRAIN_EMPTY_VERTICES} * 4, 0x7FC00000u);
        const D3D11_BUFFER_DESC empty_desc = {.ByteWidth = TERRAIN_EMPTY_VERTICES * 16, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
        const D3D11_SUBRESOURCE_DATA empty_data = {.pSysMem = empty.data()};
        device->CreateBuffer(&empty_desc, &empty_data, objects.tessellated_empty.GetAddressOf());
      }
      runtime.tessellated_expected =
          objects.tessellated_empty ? static_cast<uint32_t>(std::min<uint64_t>(runtime.terrain_known_corners / 24 * 27, MAX_TESSELLATED_VERTICES)) : 0u;
      native->SOSetTargets(0, nullptr, nullptr);
      for (UINT first = 0; first < runtime.tessellated_expected; first += TERRAIN_EMPTY_VERTICES) {
        const D3D11_BOX emptied = {.left = 0, .top = 0, .front = 0, .right = std::min(TERRAIN_EMPTY_VERTICES, runtime.tessellated_expected - first) * 16, .bottom = 1, .back = 1};
        native->CopySubresourceRegion(objects.tessellated.Get(), 0, first * 16, 0, 0, objects.tessellated_empty.Get(), 0, &emptied);
      }
      native->SOSetTargets(clip_uv ? 2 : 1, targets.data(), stream_offsets.data());
      const auto free_count = std::ranges::find(objects.terrain_counts, 0u, &DeviceObjects::TerrainCount::draws);
      objects.terrain_count = (free_count != objects.terrain_counts.end()) ? &*free_count : nullptr;
      if (objects.terrain_count != nullptr) {
        objects.terrain_count->frame = runtime.frame;
      }
    }
    // Null: counted later, and the GPU still owes every set's answers (this frame's count is then never known; the next known one serves).
    const auto count_query = [&]() -> ID3D11Query* {
      if (!shader.tessellated) return nullptr;
      if (!runtime.terrain_later) return objects.tessellated_queries[runtime.tessellated_draws].Get();
      if (objects.terrain_count == nullptr) return nullptr;
      objects.terrain_count->draws = runtime.tessellated_draws + 1;
      return objects.terrain_count->queries[runtime.tessellated_draws].Get();
    };
    // The GPU hung here now and then with the terrain's stream output in one batch with the compute before it (tree simulation, WH3's siege:
    // the game's device removed, nvlddmkm 153; the helper idle; none with this Flush, none without the capture).
    if (shader.tessellated) native->Flush();
    ID3D11Query* draw_query = count_query();
    if (draw_query != nullptr) {
      native->Begin(draw_query);
    }
    if constexpr (INDEXED) {
      if (compact || sway) {
        // Each block's vertices once: points through the mesh's remap indices (the game's index buffer and topology put
        // back).
        native->IASetIndexBuffer(runtime.cluster_meshes.at(set_key).remap.Get(), DXGI_FORMAT_R32_UINT, 0);
        native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        native->DrawIndexedInstanced(block_texels / draw_instances, draw_instances, 0, arguments.vertex_offset, arguments.first_instance);
        native->IASetIndexBuffer(compact_indices.Get(), index_format, index_offset);
        native->IASetPrimitiveTopology(topology);
      } else {
        native->DrawIndexedInstanced(arguments.index_count, placed ? 1u : std::max(arguments.instance_count, 1u), arguments.first_index,
                                     arguments.vertex_offset, arguments.first_instance);
      }
    } else {
      native->DrawInstanced(arguments.vertex_count, placed ? 1u : std::max(arguments.instance_count, 1u), arguments.first_vertex,
                            arguments.first_instance);
    }
    if (draw_query != nullptr) {
      native->End(draw_query);
    }
    if constexpr (INDEXED) {
      if (shader.tessellated && runtime.settings.terrain_outside_view && objects.tessellated_outside && runtime.tessellated_draws + 1 < objects.tessellated_queries.size()) {
        ComPtr<ID3D11HullShader> game_hull_shader;
        native->HSGetShader(game_hull_shader.GetAddressOf(), nullptr, nullptr);
        native->HSSetShader(objects.tessellated_outside.Get(), nullptr, 0);
        ++runtime.tessellated_draws;
        ID3D11Query* outside_query = count_query();
        if (outside_query != nullptr) {
          native->Begin(outside_query);
        }
        native->DrawIndexedInstanced(arguments.index_count, std::max(arguments.instance_count, 1u), arguments.first_index, arguments.vertex_offset, arguments.first_instance);
        if (outside_query != nullptr) {
          native->End(outside_query);
        }
        native->HSSetShader(game_hull_shader.Get(), nullptr, 0);
      }
    }
    if (!shader.tessellated_keep.empty()) {
      native->GSSetShaderResources(0, 1, game_keep_view.GetAddressOf());
      native->GSSetConstantBuffers(1, 1, game_keep_constants.GetAddressOf());
    }
    if (shader.sun_facing && !cascade_draw) {
      ID3D11ShaderResourceView* no_view = nullptr;
      native->VSSetShaderResources(shader.sun_slot, 1, &no_view);
    }
  }
  const UINT rows_vertex = next_vertex + (sway ? block_texels : (place ? 0u : vertex_count));
  // A placement of a draw whose pixel shader fades its instances out: those rows left out. Not a new mesh's (its build rows), not a cascade's
  // draw (the shadow pass has no such pixel shader: what it draws casts in the game too).
  const uint32_t faded_pixel_shader =
      (place && !cascade_draw && !shader.faded_rows_shader.empty() && host.faded_row != nullptr && *host.faded_row != 0.f)
          ? renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader, &command.bound_state_cache)
          : 0u;
  const bool faded = faded_pixel_shader != 0u && std::ranges::binary_search(host.faded_pixel_shaders, faded_pixel_shader);
  if (placed) {
    // As points (under the game's triangle list one vertex an instance made no primitive: nothing was streamed, the
    // helper placed every placed mesh by stale texels: giant shapes, long lines, no shadows, ATN 2026-10-28).
    ID3D11Buffer* rows_target = objects.captured.Get();
    const UINT rows_offset = rows_vertex * 16;
    // A new mesh's first rows go whole: its BLAS's build rows are its first instance's, in the view or not.
    if (cascade_draw && !place) native->VSSetConstantBuffers(0, 1, objects.cascade_no_view.GetAddressOf());
    native->VSSetShader((faded ? objects.faded_rows_shaders : objects.rows_shaders)[index].Get(), nullptr, 0);
    native->GSSetShader((faded ? objects.faded_rows_outputs : objects.rows_outputs)[index].Get(), nullptr, 0);
    native->SOSetTargets(1, &rows_target, &rows_offset);
    native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    if constexpr (INDEXED) {
      native->DrawIndexedInstanced(1, instances, arguments.first_index, arguments.vertex_offset, arguments.first_instance);
    } else {
      native->DrawInstanced(1, instances, arguments.first_vertex, arguments.first_instance);
    }
    native->IASetPrimitiveTopology(topology);
  }
  // Log Details: what the rows' records say, read back (a wait each; ROWS_READS draws of a kind a session, ROWS_LINES lines). A faded
  // draw's (w: the fade of 1000): those with an instance left out. A second-rows draw's (SceneShader::second_rows): those whose instances'
  // second rows lie apart from their first, and how far from one instance to the next (alike: one shape fits all its places).
  constexpr uint32_t ROWS_READS = 512;
  constexpr uint32_t ROWS_LINES = 24;
  if (auto& reads = runtime.rows_reads[faded ? 0 : 1];
      (faded || (shader.second_rows && !cascade_draw)) && placed && runtime.settings.debug_log && reads[0] < ROWS_READS) {
    ++reads[0];
    const D3D11_BUFFER_DESC read_desc = {.ByteWidth = instances * 64, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    const D3D11_BOX range = {.left = rows_vertex * 16, .top = 0, .front = 0, .right = (rows_vertex * 16) + (instances * 64), .bottom = 1, .back = 1};
    ComPtr<ID3D11Buffer> read;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    native->SOSetTargets(0, nullptr, nullptr);
    if (SUCCEEDED(device->CreateBuffer(&read_desc, nullptr, read.GetAddressOf()))) {
      native->CopySubresourceRegion(read.Get(), 0, 0, 0, 0, objects.captured.Get(), 0, &range);
      if (SUCCEEDED(native->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        const std::span<const std::array<float, 16>> rows(static_cast<const std::array<float, 16>*>(mapped.pData), instances);
        const auto left_out = std::ranges::count_if(rows, [](const std::array<float, 16>& row) { return row[3] > 1e29f; });
        const auto range_of = [&rows](size_t component) {
          const auto [least, most] = std::ranges::minmax(rows, {}, [component](const std::array<float, 16>& row) { return row[component]; });
          return std::pair{least[component], most[component]};
        };
        if (faded) {
          if (const auto [least, most] = range_of(15); left_out != 0 && reads[1]++ < ROWS_LINES) {
            renodx::utils::log::i(std::format("[Total War] Ray tracing: faded rows: vs {:08X} with ps {:08X}, {} indices: {} of {} instances left out (fade {:.0f} to {:.0f} of 1000).",
                                              shader.hash, faded_pixel_shader, vertex_count, left_out, instances, least, most));
          }
        } else if (const auto [turn_least, turn_most] = range_of(12); turn_most > 1e-4f || range_of(13).second > 1e-4f) {
          if (reads[1]++ < ROWS_LINES) {
            renodx::utils::log::i(std::format("[Total War] Ray tracing: second rows: vs {:08X}, {} indices, {} instances: turned and scaled apart {:.4f} to {:.4f}, moved apart {:.4f} to {:.4f} m.",
                                              shader.hash, vertex_count, instances, turn_least, turn_most, range_of(13).first, range_of(13).second));
          }
        }
        native->Unmap(read.Get(), 0);
      }
    }
    if (reads[0] == ROWS_READS) {
      renodx::utils::log::i(std::format("[Total War] Ray tracing: {} rows: {} draws read, {} {}.", (faded ? "faded" : "second"), ROWS_READS, reads[1],
                                        (faded ? "with an instance left out" : "with second rows apart from the first")));
    }
  }
  if (cascade_world) {
    if (game_camera_first == 0) {
      native->GSSetConstantBuffers(0, 1, game_camera.GetAddressOf());
    } else {
      native1->GSSetConstantBuffers1(0, 1, game_camera.GetAddressOf(), &game_camera_first, &game_camera_count);
    }
  } else if (cascade_draw) {
    if (game_camera_first == 0) {
      native->VSSetConstantBuffers(0, 1, game_camera.GetAddressOf());
    } else {
      native1->VSSetConstantBuffers1(0, 1, game_camera.GetAddressOf(), &game_camera_first, &game_camera_count);
    }
  }
  ID3D11Buffer* output = game_output.Get();
  constexpr UINT APPEND = UINT_MAX;
  native->SOSetTargets(1, &output, &APPEND);
  native->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
  native->GSSetShader(game_geometry_shader.Get(), nullptr, 0);
  native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
  if (shader.tessellated) {
    ++runtime.tessellated_draws;
    return;
  }
  // A mesh whose cluster set is on its way (its indices copied): its first instance's corners just captured copied too
  // (PrepareClusterSet orders the set's triangles by place), at the first full capture since.
  if constexpr (INDEXED) {
    if (const auto mesh = (compactable && !blocks && !place) ? runtime.cluster_meshes.find(set_key) : runtime.cluster_meshes.end();
        mesh != runtime.cluster_meshes.end() && mesh->second.staging && !mesh->second.corners) {
      const D3D11_BUFFER_DESC corners_desc = {.ByteWidth = arguments.index_count * 16, .Usage = D3D11_USAGE_STAGING,
                                              .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
      if (SUCCEEDED(device->CreateBuffer(&corners_desc, nullptr, mesh->second.corners.GetAddressOf()))) {
        const D3D11_BOX range = {.left = next_vertex * 16, .top = 0, .front = 0,
                                 .right = (next_vertex + arguments.index_count) * 16, .bottom = 1, .back = 1};
        native->CopySubresourceRegion(mesh->second.corners.Get(), 0, 0, 0, 0, objects.captured.Get(), 0, &range);
      }
    }
  }
  // A compact draw's blocks (from the captured vertices' end): after the last compact draw of its mesh (its blocks right
  // after), more of its instances; its set sent with this frame's trace when the helper lacks it.
  const auto add_blocks = [&] {
    // A placed draw's swaying copy marked (its rows follow its blocks: never joined to another draw's).
    const uint32_t compact_flags = (flags & (dlaa_bridge::RT_TERRAIN | dlaa_bridge::RT_FOLIAGE | dlaa_bridge::RT_THIN | dlaa_bridge::RT_GRASS | dlaa_bridge::RT_SOLID_CUT_OUT))
                                   | (place ? dlaa_bridge::RT_INSTANCED : 0u);
    if (auto* last = (runtime.compact.empty() ? nullptr : &runtime.compact.back());
        last != nullptr && last->set == set_key && last->flags == compact_flags && last->slice == slice
        && last->first_vertex + (last->instances * (block_texels / draw_instances)) == next_vertex) {
      last->instances += draw_instances;
    } else {
      runtime.compact.push_back({.set = set_key, .first_vertex = next_vertex, .instances = draw_instances, .flags = compact_flags,
                                 .slice = slice});
    }
    if (!runtime.cluster_meshes.at(set_key).sent && std::ranges::find(runtime.new_sets, set_key) == runtime.new_sets.end()) {
      runtime.new_sets.push_back(set_key);
      runtime.new_set_bytes += static_cast<uint32_t>((runtime.cluster_meshes.at(set_key).data.size() + 15) / 16 * 16);
    }
    if (runtime.settings.debug_log) {
      runtime.key_shaders[set_key] = shader.hash;
    }
  };
  if (place) {
    if (sway) {
      add_blocks();
      next_vertex += block_texels;
      if (shader.grass) {
        runtime.grass_vertices += block_triangles * 3;
      } else {
        runtime.tree_vertices += block_texels;
        runtime.tree_corners += block_triangles * 3;
      }
      runtime.dynamic_vertices += block_texels;
      runtime.dynamic_triangles += block_triangles;
      runtime.dynamic_clusters += block_clusters;
    }
    runtime.placements.push_back({.key = key, .first_vertex = rows_vertex, .instances = instances,
                                  .flags = (moving ? dlaa_bridge::RT_PLACE_MOVING : 0u) | (sway ? dlaa_bridge::RT_PLACE_HIDDEN : 0u)
                                           | (cascade_draw ? dlaa_bridge::RT_PLACE_CASCADE : 0u)});
    next_vertex += row_texels;
    if (const auto known = runtime.known.find(key); known != runtime.known.end()) {
      known->second.frame = runtime.frame;
      known->second.placed += instances;
    }
    return;
  }
  if (compact) {
    add_blocks();
  } else if (joined != nullptr) {
    joined->vertex_count += vertex_count;
  } else {
    runtime.captures.push_back({.key = key, .first_vertex = next_vertex, .vertex_count = vertex_count, .flags = flags,
                                .instances = (placed ? instances : 0u)});
    runtime.slices.push_back(slice);
    if (runtime.settings.debug_log) {
      runtime.key_shaders[key] = shader.hash;
    }
  }
  runtime.captured_keys.insert(key);
  next_vertex += texels + row_texels;
  if (dynamic) {
    runtime.dynamic_vertices += texels;
    if (unit) {
      runtime.unit_vertices += texels;
      runtime.unit_triangles += triangles;
      runtime.unit_clusters += clusters;
      if (cut_out) runtime.tree_corners += triangles * 3;
    }
    if (shader.grass) runtime.grass_vertices += triangles * 3;
    runtime.dynamic_triangles += triangles;
    runtime.dynamic_clusters += clusters;
    if (!shader.dynamic && runtime.settings.debug_log) {
      ++runtime.dynamic_buffers[shader.hash];
    }
  } else if (runtime.settings.debug_log) {
    ++runtime.new_static[shader.hash];
    // Its geometry (Update's report: what changed between captures of a mesh that came again).
    uint32_t first = 0;
    int32_t base = 0;
    if constexpr (INDEXED) {
      first = arguments.first_index;
      base = arguments.vertex_offset;
    } else {
      first = arguments.first_vertex;
    }
    auto& examples = runtime.new_static_examples[shader.hash];
    if (examples.size() == 3) examples.erase(examples.begin());
    examples.push_back(std::format("{} vertices at {} / {} x{} from {}, VB0 {:X} +{}, IB {:X} +{}", vertex_count, first, base,
                                   arguments.instance_count, arguments.first_instance, reinterpret_cast<uintptr_t>(vertex_buffers[0]),
                                   offsets[0], reinterpret_cast<uintptr_t>(index_buffer), index_offset));
  }
}

// A lit particle draw (Smoke Shadows: one of InstallSmokeShadows' vertex shaders with a lit pixel shader, blended over the
// scene, not added to it: smoke and dust, not fire's glow): its particles streamed out for the next trace: centre, colour
// and opacity, size. One a vertex, as the vertex shader writes them; or one an instance of a quad (its quad shader),
// which reads the pixel shader's views and samplers (shifted as its SmokePixelShader says) and the vertex shader's
// constants at their own slots.
template <typename Context>
void CaptureParticles(Context& command) {
  constexpr bool INDIRECT = std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::IndirectArguments>;
  // Through the quad shader (indexed, direct or indirect).
  constexpr bool INDEXED = std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DrawIndexedArguments> || INDIRECT;
  const auto vertex_shader = std::ranges::find(smoke_vertex_shaders, command.matched_shader_hash, &SmokeVertexShader::hash);
  if (vertex_shader == smoke_vertex_shaders.end()) return;
  UINT particle_count = 0;
  UINT first_vertex = 0;
  if constexpr (std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DrawArguments>) {
    if (vertex_shader->quad_shader.empty()) {
      particle_count = command.arguments.vertex_count;
      first_vertex = command.arguments.first_vertex;
    }
  } else if constexpr (INDIRECT) {
    if (vertex_shader->indirect && !vertex_shader->quad_shader.empty() && command.arguments.draw_count == 1
        && command.arguments.command == reshade::api::indirect_command::draw_indexed) {
      particle_count = INDIRECT_PARTICLES;
    }
  } else if constexpr (INDEXED) {
    if (!vertex_shader->quad_shader.empty() && command.arguments.index_count == 6) {
      particle_count = command.arguments.instance_count;
    }
  }
  auto* native = NativeContext(command.cmd_list);
  const bool wanted = runtime.settings.stream_particles || (runtime.settings.smoke_shadows && dlaa_bridge::bridge.ray_tracing);
  if (particle_count == 0 || !wanted || native == nullptr) return;
  const uint32_t pixel_shader = renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader, &command.bound_state_cache);
  const auto lit_shader = std::ranges::find(smoke_pixel_shaders, pixel_shader, &SmokePixelShader::hash);
  if (lit_shader == smoke_pixel_shaders.end()) return;
  ComPtr<ID3D11BlendState> blend;
  std::array<FLOAT, 4> blend_factor = {};
  UINT sample_mask = 0;
  native->OMGetBlendState(blend.GetAddressOf(), blend_factor.data(), &sample_mask);
  D3D11_BLEND_DESC blend_desc = {};
  if (blend) blend->GetDesc(&blend_desc);
  if (blend_desc.RenderTarget[0].BlendEnable == FALSE || blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND_ONE) return;
  const bool first_draw = runtime.particles_frame != runtime.frame;
  if (first_draw) {
    runtime.particles = 0;
    runtime.particles_frame = runtime.frame;
  }
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& stream = runtime.particle_stream;
  const UINT capacity = MAX_PARTICLES << (std::max(runtime.settings.smoke_quality, 3u) - 3u);
  if (stream.device.Get() != device.Get() || stream.capacity != capacity) {
    stream = {.device = device, .capacity = capacity, .outputs = std::vector<ComPtr<ID3D11GeometryShader>>(smoke_vertex_shaders.size())};
    runtime.particles = 0;
    const D3D11_BUFFER_DESC particles_desc = {.ByteWidth = capacity * 48, .Usage = D3D11_USAGE_DEFAULT,
                                              .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE};
    D3D11_SHADER_RESOURCE_VIEW_DESC particles_view = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
    particles_view.Buffer.NumElements = capacity * 3;
    const D3D11_BUFFER_DESC behaviours_desc = {.ByteWidth = 8192, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const std::array<uint32_t, 2048> no_behaviours = {};
    const D3D11_SUBRESOURCE_DATA behaviours_data = {.pSysMem = no_behaviours.data()};
    // TEXCOORD3's w is always 0: the emitter takes its place, the stride and rt_smoke stay.
    const std::array<D3D11_SO_DECLARATION_ENTRY, 4> entries = {{
        {.SemanticName = "SV_POSITION", .SemanticIndex = 0, .ComponentCount = 4},
        {.SemanticName = "COLOR", .SemanticIndex = 0, .ComponentCount = 4},
        {.SemanticName = "TEXCOORD", .SemanticIndex = 3, .ComponentCount = 3},
        {.SemanticName = "TEXCOORD", .SemanticIndex = 0, .ComponentCount = 1},
    }};
    constexpr UINT STRIDE = 48;
    const bool buffers = SUCCEEDED(device->CreateBuffer(&particles_desc, nullptr, stream.particles.GetAddressOf()))
                         && SUCCEEDED(device->CreateShaderResourceView(stream.particles.Get(), &particles_view, stream.srv.GetAddressOf()))
                         && SUCCEEDED(device->CreateBuffer(&behaviours_desc, &behaviours_data, stream.behaviours.GetAddressOf()));
    if (buffers && std::ranges::any_of(smoke_vertex_shaders, &SmokeVertexShader::indirect)) {
      const std::vector<uint8_t> empty_records(INDIRECT_PARTICLES * STRIDE);
      const D3D11_BUFFER_DESC empty_desc = {.ByteWidth = INDIRECT_PARTICLES * STRIDE, .Usage = D3D11_USAGE_IMMUTABLE,
                                            .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      const D3D11_SUBRESOURCE_DATA empty_data = {.pSysMem = empty_records.data()};
      device->CreateBuffer(&empty_desc, &empty_data, stream.empty.GetAddressOf());
    }
    for (size_t index = 0; index < smoke_vertex_shaders.size(); ++index) {
      const SmokeVertexShader& shader = smoke_vertex_shaders[index];
      std::span<const uint8_t> output_code = shader.quad_shader;
      if (const auto code = game_code.find(shader.hash); output_code.empty() && code != game_code.end()) {
        output_code = code->second;
      }
      if (!buffers || output_code.empty()
          || FAILED(device->CreateGeometryShaderWithStreamOutput(output_code.data(), output_code.size(), entries.data(),
                                                                  static_cast<UINT>(entries.size()), &STRIDE, 1, D3D11_SO_NO_RASTERIZED_STREAM,
                                                                  nullptr, stream.outputs[index].GetAddressOf()))) {
        renodx::utils::log::w(std::format("[Total War] Ray tracing: no stream output for the particles' vertex shader {:08X} (Smoke Shadows).",
                                          shader.hash));
      } else if (!shader.quad_shader.empty()) {
        renodx::utils::log::i(std::format("[Total War] Ray tracing: Smoke Shadows: one particle a quad of vertex shader {:08X} (room for {}).",
                                          shader.hash, capacity));
      }
    }
  }
  ID3D11GeometryShader* particle_output = stream.outputs[vertex_shader - smoke_vertex_shaders.begin()].Get();
  const UINT count = std::min(particle_count, capacity - runtime.particles);
  if (particle_output == nullptr || count == 0 || (INDIRECT && !stream.empty)) return;
  // The quad shader reads the vertex shader's constants with their ranges (D3D11.1).
  ComPtr<ID3D11DeviceContext1> native1;
  if (INDEXED && FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  if (first_draw && runtime.settings.stream_particles) {
    // cb_vfx_emitter_constants_common (the particle vertex shader's cb3): smoke_map_list tells the weather by it.
    ComPtr<ID3D11Buffer> behaviours;
    native->VSGetConstantBuffers(3, 1, behaviours.GetAddressOf());
    D3D11_BUFFER_DESC behaviours_bound = {};
    if (behaviours) behaviours->GetDesc(&behaviours_bound);
    if (behaviours_bound.ByteWidth >= 8192) {
      const D3D11_BOX box = {.left = 0, .top = 0, .front = 0, .right = 8192, .bottom = 1, .back = 1};
      native->CopySubresourceRegion(stream.behaviours.Get(), 0, 0, 0, 0, behaviours.Get(), 0, &box);
    }
  }
  ComPtr<ID3D11GeometryShader> game_geometry_shader;
  ComPtr<ID3D11PixelShader> game_pixel_shader;
  ComPtr<ID3D11Buffer> game_output;
  native->GSGetShader(game_geometry_shader.GetAddressOf(), nullptr, nullptr);
  native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
  native->SOGetTargets(1, game_output.GetAddressOf());
  ID3D11Buffer* target = stream.particles.Get();
  const UINT offset = runtime.particles * 48;
  if constexpr (INDIRECT) {
    const D3D11_BOX room = {.left = 0, .top = 0, .front = 0, .right = count * 48, .bottom = 1, .back = 1};
    native->CopySubresourceRegion(target, 0, offset, 0, 0, stream.empty.Get(), 0, &room);
  }
  native->GSSetShader(particle_output, nullptr, 0);
  native->PSSetShader(nullptr, nullptr, 0);
  native->SOSetTargets(1, &target, &offset);
  if constexpr (INDEXED) {
    // The quad shader's inputs at the slots the draw's own shaders have them, the pixel shader's shifted as listed (the
    // game's geometry stage's put back).
    constexpr UINT VIEWS = 16;
    constexpr UINT SAMPLERS = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
    constexpr UINT CONSTANTS = 2;
    std::array<ID3D11ShaderResourceView*, VIEWS> game_views = {};
    std::array<ID3D11SamplerState*, SAMPLERS> game_samplers = {};
    std::array<ID3D11Buffer*, CONSTANTS> game_constants = {};
    std::array<UINT, CONSTANTS> game_first_constants = {};
    std::array<UINT, CONSTANTS> game_constant_counts = {};
    std::array<ID3D11ShaderResourceView*, VIEWS> pixel_views = {};
    std::array<ID3D11SamplerState*, SAMPLERS> pixel_samplers = {};
    std::array<ID3D11Buffer*, CONSTANTS> vertex_constants = {};
    std::array<UINT, CONSTANTS> vertex_first_constants = {};
    std::array<UINT, CONSTANTS> vertex_constant_counts = {};
    native->GSGetShaderResources(0, VIEWS, game_views.data());
    native->GSGetSamplers(0, SAMPLERS, game_samplers.data());
    native1->GSGetConstantBuffers1(0, CONSTANTS, game_constants.data(), game_first_constants.data(), game_constant_counts.data());
    native->PSGetShaderResources(0, VIEWS, pixel_views.data());
    native->PSGetSamplers(0, SAMPLERS, pixel_samplers.data());
    native1->VSGetConstantBuffers1(0, CONSTANTS, vertex_constants.data(), vertex_first_constants.data(), vertex_constant_counts.data());
    native->GSSetShaderResources(lit_shader->view_shift, VIEWS - lit_shader->view_shift, pixel_views.data());
    native->GSSetSamplers(lit_shader->sampler_shift, SAMPLERS - lit_shader->sampler_shift, pixel_samplers.data());
    native1->GSSetConstantBuffers1(0, CONSTANTS, vertex_constants.data(), vertex_first_constants.data(), vertex_constant_counts.data());
    const auto& arguments = command.arguments;
    if constexpr (INDIRECT) {
      native->DrawIndexedInstancedIndirect(reinterpret_cast<ID3D11Buffer*>(arguments.buffer.handle), static_cast<UINT>(arguments.offset));
    } else {
      native->DrawIndexedInstanced(arguments.index_count, count, arguments.first_index, arguments.vertex_offset, arguments.first_instance);
    }
    native->GSSetShaderResources(0, VIEWS, game_views.data());
    native->GSSetSamplers(0, SAMPLERS, game_samplers.data());
    native1->GSSetConstantBuffers1(0, CONSTANTS, game_constants.data(), game_first_constants.data(), game_constant_counts.data());
    // The Get calls added references.
    const auto release = [](auto* object) {
      if (object != nullptr) {
        object->Release();
      }
    };
    std::ranges::for_each(game_views, release);
    std::ranges::for_each(game_samplers, release);
    std::ranges::for_each(game_constants, release);
    std::ranges::for_each(pixel_views, release);
    std::ranges::for_each(pixel_samplers, release);
    std::ranges::for_each(vertex_constants, release);
  } else {
    native->Draw(count, first_vertex);
  }
  ID3D11Buffer* output = game_output.Get();
  constexpr UINT APPEND = UINT_MAX;
  native->SOSetTargets(1, &output, &APPEND);
  native->GSSetShader(game_geometry_shader.Get(), nullptr, 0);
  native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
  runtime.particles += count;
  runtime.costs.particles += count;
}

// A listed scene shader's draw (its SceneShader the callback's data).
template <typename Context>
void CaptureDraw(Context& command, const void*) {
  const auto& listed = command.template GetCallbackData<SceneShader>();
  if (listed.pixel_masks.empty()) {
    Capture(command, listed);
    return;
  }
  const uint32_t pixel_shader =
      renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader, &command.bound_state_cache);
  const auto material = std::ranges::find(listed.pixel_masks, pixel_shader, &PixelMask::pixel_shader);
  if (material == listed.pixel_masks.end()) {
    const std::unique_lock lock(runtime.mutex);
    if (runtime.left_out_logged.insert(pixel_shader).second) {
      renodx::utils::log::i(std::format("[Total War] Ray tracing: left out: vs {:08X} with ps {:08X}, its opacity not known.", listed.hash, pixel_shader));
    }
    return;
  }
  // A copy with this material's slot (Capture finds its place in the table by its hash).
  SceneShader masked = listed;
  masked.mask_slot = material->slot;
  Capture(command, masked);
}

// An indirect draw of a listed shader (SceneShader::indirect): the GPU has made its numbers, the whole buffer read once a frame (a
// wait for the GPU, 1.2 ms on the replay at 4K, so not for a draw that was empty lately: it joins a frame or so late), then it is
// captured as a direct draw with them: compact, by its mesh, in the shares. Streamed without a mesh's cluster sets, a field of grass
// made clusters over the whole field: a trace of 225 ms against 5. Settings::indirect_counts_later: the numbers of the copy an
// earlier frame made (the probe), never a wait: a draw whose count changed is captured with the earlier one for a frame.
template <typename Context>
void CaptureIndirect(Context& command, const void*) {
  const auto& arguments = command.arguments;
  if (arguments.command != reshade::api::indirect_command::draw_indexed) return;
  if (command.template GetCallbackData<SceneShader>().grass && !GrassMet()) return;
  auto* native = NativeContext(command.cmd_list);
  if (native == nullptr || runtime.traced_frame == runtime.frame) return;
  auto* buffer = reinterpret_cast<ID3D11Buffer*>(arguments.buffer.handle);
  std::array<uint32_t, 5> numbers = {};
  {
    const std::unique_lock lock(runtime.mutex);
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    auto& probe = runtime.objects.indirect_probe;
    if (runtime.indirect_probe_frame != runtime.frame) {
      runtime.indirect_probe_frame = runtime.frame;
      D3D11_BUFFER_DESC probe_desc = {};
      if (probe) probe->GetDesc(&probe_desc);
      if (probe_desc.ByteWidth != desc.ByteWidth || runtime.indirect_probe_buffer != buffer) {
        const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
        ComPtr<ID3D11Device> device;
        native->GetDevice(device.GetAddressOf());
        probe.Reset();
        device->CreateBuffer(&staging_desc, nullptr, probe.GetAddressOf());
        runtime.indirect_probe_buffer = buffer;
        runtime.indirect_probe_values.clear();
        runtime.indirect_probe_pending = false;
      }
      if (probe && runtime.indirect_probe_pending) {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(native->Map(probe.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
          const auto* values = static_cast<const uint32_t*>(mapped.pData);
          runtime.indirect_probe_values.assign(values, values + (desc.ByteWidth / 4));
          native->Unmap(probe.Get(), 0);
          runtime.indirect_probe_pending = false;
        }
      }
      if (probe && !runtime.indirect_probe_pending) {
        native->CopySubresourceRegion(probe.Get(), 0, 0, 0, 0, buffer, 0, nullptr);
        runtime.indirect_probe_pending = true;
      }
    }
    const auto first = static_cast<size_t>(arguments.offset / 4);
    const bool probed = runtime.indirect_probe_buffer == buffer && first + numbers.size() <= runtime.indirect_probe_values.size();
    if (probed && (runtime.indirect_probe_values[first] == 0 || runtime.indirect_probe_values[first + 1] == 0)) return;
    if (runtime.settings.indirect_counts_later) {
      if (!probed) return;
      std::copy_n(runtime.indirect_probe_values.begin() + first, numbers.size(), numbers.begin());
    } else {
      if (runtime.indirect_arguments_frame != runtime.frame || runtime.indirect_arguments_buffer != buffer) {
        runtime.indirect_arguments = frame_trace::ReadBuffer<uint32_t>(native, buffer, 0, desc.ByteWidth / 4);
        runtime.indirect_arguments_buffer = buffer;
        runtime.indirect_arguments_frame = runtime.frame;
      }
      if (first + numbers.size() > runtime.indirect_arguments.size()) return;
      std::copy_n(runtime.indirect_arguments.begin() + first, numbers.size(), numbers.begin());
    }
  }
  if (numbers[0] == 0 || numbers[1] == 0) return;
  renodx::utils::command_action::CommandContext<renodx::utils::command_action::DrawIndexedArguments> direct = {
      .cmd_list = command.cmd_list,
      .arguments = {.index_count = numbers[0], .instance_count = numbers[1], .first_index = numbers[2], .vertex_offset = static_cast<int32_t>(numbers[3]), .first_instance = numbers[4]},
      .callback_data = command.callback_data,
      .bound_state_cache = command.bound_state_cache,
  };
  CaptureDraw(direct, nullptr);
}

// Any draw when the host tells scene draws apart (host.scene_draw).
template <typename Context>
void CaptureAnyDraw(Context& command, const void*) {
  auto* native = NativeContext(command.cmd_list);
  SceneShader shader = {.hash = renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::vertex_shader,
                                                                           &command.bound_state_cache)};
  if (native != nullptr && shader.hash != 0u && host.scene_draw(native, &shader)) Capture(command, shader);
}

// A frame's trace not made, and why: logged when the reason changes, else at most every 5 s (ATN 2026-10-28: every
// effect magenta in the Debug View, so no trace was made).
inline void NotTraced(const std::string& reason) {
  static std::string last;
  static uint64_t logged = 0;
  const uint64_t now = GetTickCount64();
  if (last == reason && now < logged + 5000) return;
  last = reason;
  logged = now;
  renodx::utils::log::i("[Total War] Ray tracing: no trace this frame (", reason, ").");
}

// This frame's trace from the main view's `depth` and `camera_buffer` (the ambient occlusion pass's, or the first sun
// draw's): the captures, the positions, the request; false: none.
// `depth` null: the bridge's positions (and the camera in the sun texture's texel 1) were written by the addon's own pass
// (Ryse's, from its linear depth).
inline bool Trace(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth, const Camera& camera) {
  auto& objects = runtime.objects;
  if (!Enabled() || runtime.traced_frame == runtime.frame) return false;
  const CpuTime timed(runtime.timed ? &runtime.costs.trace_ticks : nullptr);
  if (!dlaa_bridge::bridge.ray_tracing || !objects.constants) {
    NotTraced(dlaa_bridge::bridge.ray_tracing ? "its objects were not made" : "DLAA's bridge has no ray tracing textures");
    return false;
  }
  if (depth != nullptr) {
    D3D11_SHADER_RESOURCE_VIEW_DESC depth_desc = {};
    depth->GetDesc(&depth_desc);
    if (depth_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) {
      NotTraced("the depth is no 2D texture");
      return false;
    }
  }
  const auto render_size = host.render_size();
  if (render_size[0] == 0 || render_size[0] > dlaa_bridge::bridge.width || render_size[1] > dlaa_bridge::bridge.height) {
    NotTraced(std::format("render size {} x {}, the bridge's {} x {}", render_size[0], render_size[1], dlaa_bridge::bridge.width,
                          dlaa_bridge::bridge.height));
    return false;
  }
  // The tessellated draws' triangles: their counts wait for the GPU to pass those draws, which it mostly has by now (they
  // come early in the frame), then they join the captures. Past the frame's room the last ones stay out.
  if (runtime.tessellated_draws == 0) {
    runtime.tessellated_reserved = 0;
  } else {
    uint64_t triangles = 0;
    uint64_t triangles_needed = 0;
    HRESULT counted = S_OK;
    if (runtime.terrain_later) {
      const CpuTime read(runtime.timed ? &runtime.costs.terrain_wait_ticks : nullptr);
      // The earlier frames' counts the GPU has by now (no wait, no Flush: a present has submitted them); the newest serves.
      for (auto& count : objects.terrain_counts) {
        if (count.draws == 0 || &count == objects.terrain_count) continue;
        uint64_t corners_needed = 0;
        bool ready = true;
        for (uint32_t draw = 0; draw < count.draws && ready; ++draw) {
          D3D11_QUERY_DATA_SO_STATISTICS statistics = {};
          ready = native->GetData(count.queries[draw].Get(), &statistics, sizeof(statistics), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
          corners_needed += statistics.PrimitivesStorageNeeded * 3;
        }
        if (!ready) continue;
        count.draws = 0;
        if (count.frame >= runtime.terrain_known_frame) {
          runtime.terrain_known_frame = count.frame;
          runtime.terrain_known_corners = corners_needed;
        }
      }
      objects.terrain_count = nullptr;
      triangles = runtime.tessellated_expected / 3;
      triangles_needed = runtime.terrain_known_corners / 3;
    } else {
      const CpuTime waited(runtime.timed ? &runtime.costs.terrain_wait_ticks : nullptr);
      // Submits the last End: the reads below don't, and without it they'd never finish.
      native->Flush();
      const auto deadline = std::chrono::steady_clock::now() + TERRAIN_COUNT_LIMIT;
      for (uint32_t draw = 0; draw < runtime.tessellated_draws && counted == S_OK; ++draw) {
        D3D11_QUERY_DATA_SO_STATISTICS statistics = {};
        do {
          counted = native->GetData(objects.tessellated_queries[draw].Get(), &statistics, sizeof(statistics), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        } while (counted == S_FALSE && std::chrono::steady_clock::now() < deadline);
        triangles += statistics.NumPrimitivesWritten;
        triangles_needed += statistics.PrimitivesStorageNeeded;
      }
      // Settings::terrain_count_later switched on starts from this.
      if (counted == S_OK) {
        runtime.terrain_known_corners = triangles_needed * 3;
        runtime.terrain_known_frame = runtime.frame;
      }
    }
    if (counted != S_OK) {
      if (const uint64_t tick = GetTickCount64(); runtime.terrain_count_log_tick == 0 || tick >= runtime.terrain_count_log_tick + 10000) {
        runtime.terrain_count_log_tick = tick;
        renodx::utils::log::w(std::format("[Total War] Ray tracing: the GPU gave no terrain count in {} ms (0x{:08X}): a frame's rays without the terrain.",
                                          TERRAIN_COUNT_LIMIT.count(), static_cast<uint32_t>(counted)));
      }
      // Begun again, a query with its answer still open reads that old answer next frame (WARP warp_tests/terrain_wait).
      ComPtr<ID3D11Device> query_device;
      native->GetDevice(query_device.GetAddressOf());
      const D3D11_QUERY_DESC statistics_desc = {.Query = D3D11_QUERY_SO_STATISTICS};
      for (auto& query : objects.tessellated_queries) {
        if (ComPtr<ID3D11Query> fresh; SUCCEEDED(query_device->CreateQuery(&statistics_desc, fresh.GetAddressOf()))) query = fresh;
      }
    } else {
      const UINT room = std::min({(world_captures ? WORLD_BASE : MAX_VERTICES) - runtime.captured_vertices, MAX_DYNAMIC_VERTICES - std::min(runtime.dynamic_vertices, MAX_DYNAMIC_VERTICES),
                                  (dlaa_bridge::RT_DYNAMIC_TRIANGLES - std::min(runtime.dynamic_triangles, dlaa_bridge::RT_DYNAMIC_TRIANGLES)) * 3,
                                  (dlaa_bridge::RT_DYNAMIC_CLUSTERS - std::min(runtime.dynamic_clusters, dlaa_bridge::RT_DYNAMIC_CLUSTERS)) * dlaa_bridge::RT_CAPTURE_CLUSTER_TRIANGLES * 3});
      const UINT corners = static_cast<UINT>(std::min<uint64_t>(triangles * 3, room / 3 * 3));
      if (corners != 0 && runtime.captures.size() < dlaa_bridge::RT_CAPTURES) {
        const D3D11_BOX written = {.left = 0, .top = 0, .front = 0, .right = corners * 16, .bottom = 1, .back = 1};
        native->CopySubresourceRegion(objects.captured.Get(), 0, runtime.captured_vertices * 16, 0, 0, objects.tessellated.Get(), 0, &written);
        runtime.captures.push_back({.key = TESSELLATED_KEY, .first_vertex = runtime.captured_vertices, .vertex_count = corners,
                                    .flags = dlaa_bridge::RT_TERRAIN | dlaa_bridge::RT_DYNAMIC});
        runtime.slices.push_back(0);
        runtime.captured_vertices += corners;
        runtime.dynamic_vertices += corners;
        runtime.dynamic_triangles += corners / 3;
        runtime.dynamic_clusters += ((corners / 3) + dlaa_bridge::RT_CAPTURE_CLUSTER_TRIANGLES - 1) / dlaa_bridge::RT_CAPTURE_CLUSTER_TRIANGLES;
      }
      runtime.tessellated_reserved = static_cast<uint32_t>(std::min<uint64_t>(triangles_needed * 3 / 8 * 9, MAX_TESSELLATED_VERTICES));
      if (const uint64_t tick = GetTickCount64();
          corners / 3 < triangles_needed && (runtime.tessellated_room_log_tick == 0 || (runtime.settings.debug_log && tick >= runtime.tessellated_room_log_tick + 10000))) {
        runtime.tessellated_room_log_tick = tick;
        renodx::utils::log::i(std::format("[Total War] Ray tracing: tessellated triangles without room: {} of {} ({} written, room for {}).", corners / 3, triangles_needed, triangles, room / 3));
      }
    }
  }
  // The world-space part goes after the clip-space vertices in the vertex texture (rt_vertices); what points at it follows.
  const UINT world_first = runtime.captured_vertices;
  if (runtime.world_vertices != WORLD_BASE) {
    const auto moved = [world_first](uint32_t* first_vertex) {
      if (*first_vertex >= WORLD_BASE) *first_vertex = *first_vertex - WORLD_BASE + world_first;
    };
    for (auto& capture : runtime.captures) moved(&capture.first_vertex);
    for (auto& draw : runtime.compact) moved(&draw.first_vertex);
    for (auto& placement : runtime.placements) moved(&placement.first_vertex);
    runtime.captured_vertices += runtime.world_vertices - WORLD_BASE;
    runtime.world_vertices = WORLD_BASE;
  }
  // The cost report's timestamps (a set still pending: this frame without).
  auto& timing = objects.timings[objects.timing_next % objects.timings.size()];
  const bool stamped = runtime.timed && timing.disjoint && timing.stamps[2] && !timing.pending;
  if (stamped) {
    native->Begin(timing.disjoint.Get());
    native->End(timing.stamps[0].Get());
  }
  {
    const ComputeState kept(native);
    if (runtime.captured_vertices != 0) {
      // Clip-space captures become world positions with the camera they were drawn with (this frame's).
      struct VertexConstants {
        std::array<uint32_t, 4> counts;
        std::array<float, 16> inverse_view_projection;
        std::array<uint32_t, 4> world_part;  // its place in the captured buffer
      };
      const VertexConstants constants = {
          .counts = {runtime.captured_vertices, dlaa_bridge::RT_TRANSFER_WIDTH, clip_captures ? 1u : 0u, world_first},
          .inverse_view_projection = camera.inverse_view_projection,
          .world_part = {WORLD_BASE, 0, 0, 0},
      };
      const UINT vertex_groups = (runtime.captured_vertices + 255) / 256;
      Dispatch(native, objects.vertices_shader.Get(), {objects.captured_srv.Get(), objects.captured_uv_srv.Get()},
               {dlaa_bridge::bridge.rt_vertices.uav.Get(), nullptr}, &constants, sizeof(constants), std::min(vertex_groups, 65535u),
               (vertex_groups + 65534) / 65535);
      // Skies (rt_skies): the new static captures that hold the camera and are larger than the view reaches (the farther
      // of its near and far planes' centres; unbounded with an infinite far plane) or centred on it (domes the game
      // keeps around the camera: placed ones followed it, a circle of shadow under the camera, ATN 2026-10-28) go empty.
      const auto& inverse = camera.inverse_view_projection;
      float reach = 0.f;
      for (const float depth : {0.f, 1.f}) {
        const float w = (inverse[14] * depth) + inverse[15];
        if (std::abs(w) < 9.997584e-13f) {
          reach = std::numeric_limits<float>::max();
          break;
        }
        const float x = (((inverse[2] * depth) + inverse[3]) / w) - camera.position[0];
        const float y = (((inverse[6] * depth) + inverse[7]) / w) - camera.position[1];
        const float z = (((inverse[10] * depth) + inverse[11]) / w) - camera.position[2];
        reach = std::max(reach, std::sqrt((x * x) + (y * y) + (z * z)));
      }
      struct SkyConstants {
        std::array<uint32_t, 4> capture;
        std::array<float, 16> inverse_view_projection;
        std::array<float, 4> camera;
      };
      for (const auto& capture : runtime.captures) {
        // Not a world-space part in a clip-space game: rt_skies reads the captured buffer at the capture's place.
        if ((capture.flags & (dlaa_bridge::RT_TERRAIN | dlaa_bridge::RT_DYNAMIC | dlaa_bridge::RT_FOLIAGE)) != 0 || (world_captures && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0)) continue;
        const SkyConstants sky = {
            .capture = {capture.first_vertex, capture.vertex_count, clip_captures ? 1u : 0u, dlaa_bridge::RT_TRANSFER_WIDTH},
            .inverse_view_projection = camera.inverse_view_projection,
            .camera = {camera.position[0], camera.position[1], camera.position[2], reach},
        };
        Dispatch(native, objects.skies_shader.Get(), {objects.captured_srv.Get(), nullptr}, {dlaa_bridge::bridge.rt_vertices.uav.Get(), nullptr},
                 &sky, sizeof(sky), 1, 1);
      }
    }
    // Sprite soldiers' and far trees' captures (RT_SUN_ONLY) last, only the smoke's after them: the helper takes the dynamic captures' tail
    // for the instance that the sun's rays alone meet. One followed by a unit's draw or the terrain's capture (Warhammer III: both, every
    // frame) stayed among the units': its card stood in the ambient's, the lights' and the paths' rays too (a dark wedge at each tree's
    // foot), and which cards did changed with the frame's draw order (ATN's debug9: far tree shadows flicker).
    if (const auto sun_only = [](const dlaa_bridge::RtCapture& capture) { return (capture.flags & dlaa_bridge::RT_SUN_ONLY) != 0u; };
        !std::ranges::is_partitioned(runtime.captures, std::not_fn(sun_only))) {
      std::vector<dlaa_bridge::RtCapture> ordered;
      std::vector<uint32_t> ordered_slices;
      ordered.reserve(runtime.captures.size());
      ordered_slices.reserve(runtime.slices.size());
      for (const bool last : {false, true}) {
        for (size_t index = 0; index < runtime.captures.size(); ++index) {
          if (sun_only(runtime.captures[index]) != last) continue;
          ordered.push_back(runtime.captures[index]);
          ordered_slices.push_back(runtime.slices[index]);
        }
      }
      runtime.captures = std::move(ordered);
      runtime.slices = std::move(ordered_slices);
    }
    // Smoke Shadows: the lit particles streamed out since the last trace, those with opacity as quads facing the sun after
    // the captures (rt_smoke; its second dispatch empties the quads left): a capture whose particles dim the helper's sun rays.
    const UINT smoke_room = MAX_SMOKE_QUADS << (std::max(runtime.settings.smoke_quality, 1u) - 1u);
    const UINT smoke_quads = std::min(runtime.particles, smoke_room);
    if (runtime.settings.smoke_shadows && smoke_quads != 0 && runtime.particles_frame + 1 >= runtime.frame
        && runtime.captures.size() < dlaa_bridge::RT_CAPTURES && runtime.captured_vertices + (smoke_quads * 6) <= MAX_VERTICES) {
      const auto smoke_pass = [&](UINT particles, UINT items) {
        const std::array<uint32_t, 5> smoke = {runtime.captured_vertices, particles, dlaa_bridge::RT_TRANSFER_WIDTH, smoke_quads, std::bit_cast<uint32_t>(objects.smoke_least)};
        Dispatch(native, objects.smoke_shader.Get(), {runtime.particle_stream.srv.Get(), dlaa_bridge::bridge.rt_sun.srv.Get()},
                 {dlaa_bridge::bridge.rt_vertices.uav.Get(), objects.smoke_quads_uav.Get()}, smoke.data(), sizeof(smoke), (items + 63) / 64, 1);
      };
      constexpr std::array<UINT, 4> NO_QUADS = {};
      // Past the room the smallest shadows give way: the first come took it, and which came first changes each frame (ATN
      // 2026-11-03: 70-84 K wanted a quad under volleys, burning houses' shadows pulsed).
      D3D11_MAPPED_SUBRESOURCE quads_read = {};
      if (objects.smoke_read_waiting && SUCCEEDED(native->Map(objects.smoke_quads_read.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &quads_read))) {
        const uint32_t wanted = *static_cast<const uint32_t*>(quads_read.pData);
        native->Unmap(objects.smoke_quads_read.Get(), 0);
        objects.smoke_read_waiting = false;
        runtime.costs.most_smoke_quads = std::max(runtime.costs.most_smoke_quads, wanted);
        const float room = static_cast<float>(smoke_room) * 0.875f;
        if (static_cast<float>(wanted) > room) {
          objects.smoke_least *= std::min(std::sqrt(static_cast<float>(wanted) / room), 1.5f);
        } else if (static_cast<float>(wanted) < room * 0.75f) {
          objects.smoke_least = std::max(objects.smoke_least * 0.98f, SMOKE_LEAST);
        }
      }
      native->ClearUnorderedAccessViewUint(objects.smoke_quads_uav.Get(), NO_QUADS.data());
      smoke_pass(runtime.particles, runtime.particles);
      smoke_pass(0, smoke_quads);
      if (!objects.smoke_read_waiting) {
        native->CopyResource(objects.smoke_quads_read.Get(), objects.smoke_quads.Get());
        objects.smoke_read_waiting = true;
      }
      runtime.captures.push_back({.key = SMOKE_KEY, .first_vertex = runtime.captured_vertices, .vertex_count = smoke_quads * 6,
                                  .flags = dlaa_bridge::RT_DYNAMIC | dlaa_bridge::RT_FOLIAGE | dlaa_bridge::RT_SMOKE});
      runtime.slices.push_back(0);
      runtime.captured_vertices += smoke_quads * 6;
      runtime.dynamic_vertices += smoke_quads * 6;
      runtime.particles = 0;
    }
    runtime.positions = {
        .inverse_view_projection = camera.inverse_view_projection,
        .camera = {camera.position[0], camera.position[1], camera.position[2], 1.f},
        .size = {render_size[0], render_size[1], (frame_history::campaign_frames.load(std::memory_order_relaxed) != 0 ? 1u : 0u), 0},
    };
    if (depth != nullptr) {
      Dispatch(native, objects.positions_shader.Get(), {depth, nullptr},
               {dlaa_bridge::bridge.rt_positions.uav.Get(), dlaa_bridge::bridge.rt_sun.uav.Get()}, &runtime.positions,
               sizeof(runtime.positions), (render_size[0] + 7) / 8, (render_size[1] + 7) / 8);
    }
  }
  // Torch and Fire Light in the Light Bounce: only while the path-traced Light Bounce ran the frame before (its request
  // comes after the game's light draws, which read this trace's bits or ratio).
  const bool lights_in_bounce = (runtime.settings.lights_in_bounce || runtime.settings.light_ratio) && runtime.settings.light_bounce
                                && runtime.settings.path_tracing && runtime.bounced_frame + 1 == runtime.frame;
  const bool light_ratio = lights_in_bounce && runtime.settings.light_ratio;
  // The local lights: last frame's nearest and brightest (their reach over their distance, squared, by their colour's
  // brightest channel) within Torch and Fire Shadow Range.
  runtime.lights.clear();
  runtime.light_colours.clear();
  if (runtime.settings.local_lights && !runtime.lights_last.empty()) {
    const auto distance_squared = [&](const LocalLight& light) {
      const float x = light.light[0] - camera.position[0];
      const float y = light.light[1] - camera.position[1];
      const float z = light.light[2] - camera.position[2];
      return (x * x) + (y * y) + (z * z);
    };
    const auto score = [&](const LocalLight& light) {
      return std::max({light.colour[0], light.colour[1], light.colour[2]}) * light.light[3] * light.light[3]
             / std::max(distance_squared(light), 1.f);
    };
    auto& candidates = runtime.lights_last;
    if (const float range = runtime.settings.light_range; range > 0.f) {
      std::erase_if(candidates, [&](const LocalLight& light) { return distance_squared(light) > range * range; });
    }
    // The shadow ratio covers the lights the bounce samples: the first 32.
    const size_t count = std::min<size_t>({candidates.size(), runtime.settings.light_count, light_ratio ? dlaa_bridge::RT_MAX_LIGHTS : dlaa_bridge::RT_ALL_LIGHTS});
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<ptrdiff_t>(count), candidates.end(),
                      [&](const LocalLight& a, const LocalLight& b) { return score(a) > score(b); });
    // b9 (rt_local_lights.hlsli): positions and reach, the count, the glow (torch and fire light in fog and smoke), the
    // lit mask's first tile row (bridge v31) and whether its words are shadow ratios, the colours.
    std::array<std::array<float, 4>, (dlaa_bridge::RT_ALL_LIGHTS * 2) + 1> constants = {};
    for (size_t index = 0; index < count; ++index) {
      const std::array<float, 4> colour = (candidates[index].shadows_only ? std::array<float, 4>{} : candidates[index].colour);
      runtime.lights.push_back(candidates[index].light);
      runtime.light_colours.push_back(colour);
      constants[index] = candidates[index].light;
      constants[dlaa_bridge::RT_ALL_LIGHTS + 1 + index] = colour;
    }
    constants[dlaa_bridge::RT_ALL_LIGHTS] = {std::bit_cast<float>(static_cast<uint32_t>(count)), runtime.settings.light_glow,
                                             std::bit_cast<float>(dlaa_bridge::bridge.rt_light_tile_row), std::bit_cast<float>(light_ratio ? 1u : 0u)};
    native->UpdateSubresource(objects.light_constants.Get(), 0, nullptr, constants.data(), 0, 0);
  }
  const bool lights = !runtime.lights.empty();
  // v16: the cluster sets this frame's compact draws need that the helper lacks.
  std::vector<dlaa_bridge::RtClusterSet> new_sets;
  std::vector<std::span<const uint8_t>> new_set_data;
  for (const uint64_t set_key : runtime.new_sets) {
    const auto& mesh = runtime.cluster_meshes.at(set_key);
    new_sets.push_back({.key = set_key, .vertices = mesh.vertices, .clusters = mesh.clusters, .triangles = mesh.triangles});
    new_set_data.emplace_back(mesh.data);
  }
  // A mesh whose instances this frame all went in: as many as last frame's (its draws after the trace are not in yet), or
  // fewer of a mesh the last frame drew wholly before its trace. Fewer never counted: while the camera zoomed, a level of
  // detail losing its places to the next kept its copies there beside the new ones, twice the grass in the sun's rays
  // until the count stood still (ATN's debug41: blades' shadows dark for half a second after a zoom).
  for (dlaa_bridge::RtPlacement& placement : runtime.placements) {
    const auto known = runtime.known.find(placement.key);
    if (known == runtime.known.end() || known->second.counted_frame != runtime.frame) continue;
    Runtime::Known& seen = known->second;
    if (seen.placed >= seen.instances && (seen.instances >= seen.last_instances || seen.drawn_before_trace)) {
      placement.flags |= dlaa_bridge::RT_PLACE_WHOLE;
    }
    seen.traced_frame = runtime.frame;
    seen.traced_instances = seen.instances;
  }
  const dlaa_bridge::TraceRequest request = {
      .flags = (runtime.settings.sun_shadows ? dlaa_bridge::RT_SUN_SHADOWS : 0u)
               | (runtime.settings.ambient_occlusion ? dlaa_bridge::RT_AMBIENT_OCCLUSION : 0u)
               | (runtime.settings.ambient_quarter ? dlaa_bridge::RT_AMBIENT_QUARTER : 0u)
               | (lights ? dlaa_bridge::RT_LOCAL_LIGHTS : 0u)
               | (runtime.settings.leaf_light ? dlaa_bridge::RT_LEAF_LIGHT : 0u)
               | (runtime.settings.coloured_shadows ? dlaa_bridge::RT_COLOURED_SHADOWS : 0u)
               | (runtime.settings.fog_shadows ? dlaa_bridge::RT_FOG_SHADOWS : 0u)
               | ((runtime.settings.fog_shadows && lights && runtime.settings.light_glow > 0.f) ? dlaa_bridge::RT_FOG_LIGHTS : 0u)
               | (runtime.settings.micromaps ? dlaa_bridge::RT_MICROMAPS : 0u)
               | ((runtime.settings.micromaps && runtime.settings.gpu_micromaps) ? dlaa_bridge::RT_GPU_MICROMAPS : 0u)
               | (runtime.settings.sway_off_screen ? 0u : dlaa_bridge::RT_SWAY_IN_VIEW)
               | ((runtime.settings.micromaps && runtime.settings.fast_leaves) ? dlaa_bridge::RT_FAST_LEAVES : 0u)
               | (runtime.settings.light_importance ? dlaa_bridge::RT_LIGHT_IMPORTANCE : 0u)
               | (runtime.settings.light_steady ? dlaa_bridge::RT_LIGHT_STEADY : 0u)
               | ((lights && lights_in_bounce) ? dlaa_bridge::RT_LIGHTS_IN_BOUNCE : 0u) | ((lights && light_ratio) ? dlaa_bridge::RT_LIGHT_RATIO : 0u)
               | (dlaa_bridge::bridge.reconstructing ? dlaa_bridge::RT_RECONSTRUCTED : 0u)
               | ((runtime.settings.nrd && !dlaa_bridge::bridge.reconstructing) ? dlaa_bridge::RT_NRD : 0u)
               | ((runtime.settings.ao_reblur && dlaa_bridge::bridge.reconstructing) ? dlaa_bridge::RT_AO_REBLUR : 0u)
               | (runtime.settings.start_past ? dlaa_bridge::RT_START_PAST : 0u)
               | (runtime.settings.sheets_unselfed ? dlaa_bridge::RT_SHEETS_UNSELFED : 0u)
               | (runtime.settings.one_at_a_place ? dlaa_bridge::RT_ONE_AT_A_PLACE : 0u)
               | (runtime.settings.scene_test == 4 ? dlaa_bridge::RT_NO_KEPT : 0u) | dlaa_bridge::RT_DRAWN_PLACED
               | (runtime.profile.file.is_open() ? dlaa_bridge::RT_PROFILE : 0u)
               | (runtime.settings.debug_log ? dlaa_bridge::RT_DETAILS : 0u)
               | (std::array<uint32_t, 6>{0u, 1u, 2u, 2u, 3u, 3u}[std::min(runtime.settings.smoke_quality, 5u)] << dlaa_bridge::RT_SMOKE_QUALITY_SHIFT),
      .kept_frames = (runtime.scene_gone ? 1u : runtime.kept_frames),
      .ambient_range = runtime.settings.ambient_range * RangeScale(),
      .ambient_rays = runtime.settings.ambient_rays,
      .sun_softness = runtime.settings.sun_softness,
      .frame = static_cast<uint32_t>(runtime.frame),
      .width = render_size[0],
      .height = render_size[1],
      .captures = runtime.captures,
      .slices = runtime.slices,
      .drawn = runtime.drawn,
      .lights = runtime.lights,
      .light_colours = runtime.light_colours,
      .view_projection = camera.view_projection,
      .placements = runtime.placements,
      .compact = runtime.compact,
      .new_sets = new_sets,
      .new_set_data = new_set_data,
      .light_size = runtime.settings.light_size,
      .shadow_rays = runtime.settings.shadow_rays,
      .sway_range = runtime.settings.sway_range,
      .grass_shadow_range = runtime.settings.grass_shadow_range,
      .grass_range = runtime.settings.grass_range,
  };
  if (stamped) native->End(timing.stamps[1].Get());
  const bool traced = dlaa_bridge::TraceScene(native, request);
  if (stamped) {
    native->End(timing.stamps[2].Get());
    native->End(timing.disjoint.Get());
    timing.pending = true;
    timing.frame = runtime.frame;
    ++objects.timing_next;
  }
  if (!traced) NotTraced("the helper did not trace: " + dlaa_bridge::TraceStatus());
  runtime.lights_traced = traced && lights;
  runtime.lights_in_bounce = (runtime.lights_traced && lights_in_bounce) ? (light_ratio ? 3u : 1u) : 0u;
  if (traced) {
    runtime.scene_gone = false;
    for (const auto& capture : runtime.captures) {
      if ((capture.flags & dlaa_bridge::RT_DYNAMIC) == 0) {
        Runtime::Known& seen = runtime.known[capture.key];
        seen.frame = runtime.frame;
        seen.triangles = capture.vertex_count / 3;
        seen.placed_mesh = (capture.flags & dlaa_bridge::RT_INSTANCED) != 0;
      }
    }
    for (const uint64_t set_key : runtime.new_sets) runtime.cluster_meshes.at(set_key).sent = true;
    runtime.traced_frame = runtime.frame;
    runtime.traced_size = render_size;
    runtime.traced_view_projection = camera.view_projection;
    runtime.camera_moving = camera.position != runtime.traced_position;
    runtime.traced_position = camera.position;
  }
  runtime.costs.compact_draws += runtime.compact.size();
  if (runtime.profile.file.is_open()) {
    runtime.profile.counts = {runtime.captures.size(), runtime.compact.size(), runtime.placements.size(), runtime.new_sets.size(),
                              runtime.captured_vertices, runtime.lights.size()};
  }
  runtime.captures.clear();
  runtime.captured_keys.clear();
  runtime.slices.clear();
  runtime.captured_vertices = 0;
  runtime.world_vertices = WORLD_BASE;
  runtime.tessellated_draws = 0;
  runtime.drawn.clear();
  runtime.placements.clear();
  runtime.compact.clear();
  runtime.new_sets.clear();
  runtime.new_set_bytes = 0;
  return traced;
}

// The same with the camera in `camera_buffer` (host.camera); false: not the main view's.
inline bool Trace(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth, ID3D11Buffer* camera_buffer) {
  if (!Enabled() || runtime.traced_frame == runtime.frame) return false;
  Camera camera;
  if (depth == nullptr || camera_buffer == nullptr || !host.camera(camera_buffer, &camera)) {
    NotTraced(depth == nullptr ? "no depth bound at its pass" : "no main camera in its pass's camera buffer");
    return false;
  }
  return Trace(native, depth, camera);
}

// The Light Bounce's trace through rays (light_bounce.hpp SetRayTracedTrace): its prepared light (its first level's
// top-left `width` x `height`, a texel each `scale` render pixels) to the helper, the bounce into `traced`; false: not
// this frame (the screen-space trace then).
inline bool TraceLightBounce(ID3D11DeviceContext* native, ID3D11Texture2D* prepared_light, UINT width, UINT height, UINT scale,
                             ID3D11Texture2D* traced) {
  const std::unique_lock lock(runtime.mutex);
  auto& bridge = dlaa_bridge::bridge;
  if (!runtime.settings.light_bounce || runtime.traced_frame != runtime.frame || !bridge.ray_tracing || !bridge.rt_light.texture
      || width > bridge.width || height > bridge.height) {
    return false;
  }
  const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = width, .bottom = height, .back = 1};
  native->CopySubresourceRegion(bridge.rt_light.texture.Get(), 0, 0, 0, 0, prepared_light, 0, &region);
  runtime.path_frames = (runtime.settings.path_tracing && runtime.bounced_frame + 1 == runtime.frame) ? runtime.path_frames + 1 : 0u;
  runtime.path_ambient_on = runtime.settings.path_ambient && runtime.path_frames >= PATH_AMBIENT_FRAMES;
  const dlaa_bridge::BounceRequest request = {
      .frame = static_cast<uint32_t>(runtime.frame),
      .width = runtime.traced_size[0],
      .height = runtime.traced_size[1],
      .bounce_width = width,
      .bounce_height = height,
      .scale = scale,
      .rays = runtime.settings.light_bounce_rays,
      .range = runtime.settings.light_bounce_range * RangeScale(),
      .view_projection = runtime.traced_view_projection,
      .path_flags = runtime.settings.path_tracing ? (dlaa_bridge::RT_PATH_TRACING | dlaa_bridge::RT_PATH_REUSE
                                                     | ((dlaa_bridge::bridge.reconstructing && !dlaa_bridge::bridge.nrd_ask_bounce) ? dlaa_bridge::RT_PATH_RECONSTRUCTED : 0u)
                                                     | (runtime.path_ambient_on ? dlaa_bridge::RT_PATH_AMBIENT : 0u)
                                                     | ((runtime.path_ambient_on && runtime.settings.path_sky_apart) ? dlaa_bridge::RT_PATH_SKY_APART : 0u)
                                                     | ((runtime.path_ambient_on && runtime.settings.path_kept_cube) ? dlaa_bridge::RT_PATH_KEPT_CUBE : 0u)
                                                     | ((runtime.path_ambient_on && sun_colour != 0) ? dlaa_bridge::RT_PATH_SUN_LIGHT : 0u)
                                                     | (runtime.settings.path_ser ? dlaa_bridge::RT_PATH_SER : 0u)
                                                     | (runtime.settings.sharc ? dlaa_bridge::RT_PATH_SHARC : 0u)
                                                     | (runtime.settings.reuse_lean ? dlaa_bridge::RT_PATH_REUSE_LEAN : 0u)
                                                     | ((runtime.lights_in_bounce != 0) ? dlaa_bridge::RT_PATH_LIGHTS_IN_BOUNCE : 0u)
                                                     | ((runtime.lights_in_bounce == 3) ? dlaa_bridge::RT_PATH_LIGHT_RATIO : 0u)
                                                     | (dlaa_bridge::bridge.nrd_ask_bounce ? dlaa_bridge::RT_PATH_NRD : 0u))
                                                  : ((dlaa_bridge::bridge.nrd_ask_bounce ? dlaa_bridge::RT_PATH_NRD : 0u)
                                                     | (runtime.settings.reflections ? dlaa_bridge::RT_PATH_CACHE_FILL : 0u)),
      .path_bounces = runtime.settings.path_bounces,
      .path_samples = runtime.settings.light_bounce_rays,
      .guides = host.guides_written(),
  };
  const bool bounced = dlaa_bridge::TraceBounce(native, request);
  if (bounced != runtime.logged_bounce) {
    runtime.logged_bounce = bounced;
    renodx::utils::log::i("[Total War] Ray-traced Light Bounce: ", (bounced ? ((request.path_flags & dlaa_bridge::RT_PATH_TRACING) != 0 ? "path tracing" : "tracing")
                                                                            : "not traced (the screen-space one)"),
                          " (", width, " x ", height, ", ", request.rays, " rays).");
  }
  if (!bounced) return false;
  native->CopySubresourceRegion(traced, 0, 0, 0, 0, bridge.rt_bounce.texture.Get(), 0, &region);
  runtime.bounced_frame = runtime.frame;
  runtime.bounce_scale = scale;
  return true;
}

// A pixel shader's coverage: whether it discards, and where it samples t0.
inline PixelCoverage ClassifyCoverage(std::span<const uint8_t> code) {
  constexpr uint32_t OP_DISCARD = 13, OP_SAMPLE = 69, OP_SAMPLE_B = 74;
  constexpr uint32_t OPERAND_INPUT = 1, OPERAND_RESOURCE = 7;
  const dxbc::Program program = dxbc::Read(code);
  const auto& tokens = program.tokens;
  if (tokens.empty()) return {.coverage = Coverage::LEFT_OUT};
  // An operand's tokens at `at` (0: relative indexing, which these operands do not use, or malformed).
  const auto operand_length = [&](size_t at) -> size_t {
    if (at >= tokens.size()) return 0;
    const uint32_t token = tokens[at];
    size_t length = 1 + (token >> 31);
    if (((token >> 12) & 0xFF) == 4) return length + (((token & 3) == 2) ? 4 : 1);  // immediate32
    for (uint32_t dimension = 0; dimension < ((token >> 20) & 3); ++dimension) {
      if (((token >> (22 + (3 * dimension))) & 7) != 0) return 0;
      ++length;
    }
    return length;
  };
  bool discards = false;
  bool sampled_elsewhere = false;
  uint32_t uv_register = UINT32_MAX;
  for (size_t at = 2; at < tokens.size();) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {.coverage = Coverage::LEFT_OUT};
    discards = discards || opcode == OP_DISCARD;
    if (opcode >= OP_SAMPLE && opcode <= OP_SAMPLE_B) {
      // Past the extended opcode tokens: the destination, the address, the resource.
      size_t operand = at + 1;
      for (bool extended = (tokens[at] >> 31) != 0; extended && operand < at + length; ++operand) extended = (tokens[operand] >> 31) != 0;
      const size_t address = operand + operand_length(operand);
      const size_t resource = address + operand_length(address);
      if (address > operand && resource > address && resource + 1 < at + length && ((tokens[resource] >> 12) & 0xFF) == OPERAND_RESOURCE
          && tokens[resource + 1 + (tokens[resource] >> 31)] == 0) {
        const bool input = ((tokens[address] >> 12) & 0xFF) == OPERAND_INPUT;
        if (input && uv_register == UINT32_MAX) {
          uv_register = tokens[address + 1 + (tokens[address] >> 31)];
        } else if (!input) {
          sampled_elsewhere = true;
        }
      }
    }
    at += length;
  }
  if (!discards || (uv_register == UINT32_MAX && !sampled_elsewhere)) return {};
  dxbc::SignatureElement uv;
  if (sampled_elsewhere || uv_register == UINT32_MAX
      || !dxbc::FindSignatureElement(code, program, "ISGN", [&](const dxbc::SignatureElement&, uint32_t found) { return found == uv_register; },
                                     &uv)) {
    return {.coverage = Coverage::LEFT_OUT};
  }
  return {.coverage = Coverage::MASKED, .uv_semantic = std::string(uv.name), .uv_index = uv.index};
}

// A pipeline going away: its pixel shader's coverage with it (its handle may come back).
inline void OnDestroyPipeline(reshade::api::device*, reshade::api::pipeline pipeline) {
  const std::unique_lock lock(runtime.mutex);
  pixel_coverage.erase(pipeline.handle);
}

// A new pipeline: the code of a scene shader without a capture version kept (its stream output is made from it).
inline void OnInitPipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                           const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline pipeline) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const auto* code = static_cast<const uint8_t*>(desc->code);
    if (subobjects[index].type == reshade::api::pipeline_subobject_type::pixel_shader && host.scene_draw != nullptr) {
      PixelCoverage coverage = ClassifyCoverage({code, desc->code_size});
      const std::unique_lock lock(runtime.mutex);
      pixel_coverage[pipeline.handle] = std::move(coverage);
      continue;
    }
    if (subobjects[index].type == reshade::api::pipeline_subobject_type::domain_shader && std::ranges::any_of(scene_shaders, &SceneShader::tessellated)) {
      const uint32_t domain_hash = renodx::utils::hash::ComputeCRC32(code, desc->code_size);
      const std::unique_lock lock(runtime.mutex);
      game_code[domain_hash].assign(code, code + desc->code_size);
      continue;
    }
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::vertex_shader) continue;
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(code, desc->code_size);
    if (host.scene_draw == nullptr && std::ranges::find(smoke_vertex_shaders, hash, &SmokeVertexShader::hash) == smoke_vertex_shaders.end()
        && std::ranges::none_of(scene_shaders, [hash](const SceneShader& shader) { return shader.hash == hash && (shader.capture_shader.empty() || world_captures); })) {
      continue;
    }
    // Bone inputs: the host's test, else the input signature's semantic names (plain text in the code).
    constexpr std::string_view BONES = "BLENDINDICES";
    const std::string_view bytes(reinterpret_cast<const char*>(code), desc->code_size);
    const bool skinned = (host.skinned_code != nullptr) ? host.skinned_code({code, desc->code_size}) : bytes.find(BONES) != std::string_view::npos;
    const std::unique_lock lock(runtime.mutex);
    game_code[hash].assign(code, code + desc->code_size);
    skinned_code[hash] = skinned;
  }
}

}  // namespace internal

// The reflections and specular occlusion (the reflection texture: reflected light, the share of samples that met a
// surface; the occlusion: 1 open), traced once a frame at the game's reflection pass (after this frame's trace: its
// G-buffer guides and positions); both null when off or not traced this frame (the pass keeps the game's then).
// `surface_depth` (with the water's reflections on): the pass's depth, the water's in it: the positions and the guides
// again first (the water drawn since: its rays left from the riverbed with its normal, ATN's debug28). `light_buffer`:
// the pass's, for the rays' hits to read this frame's light as the game's own march does, in every Light Bounce mode.
inline std::array<ID3D11ShaderResourceView*, 2> Reflections(ID3D11DeviceContext* native, ID3D11ShaderResourceView* surface_depth = nullptr,
                                                            ID3D11ShaderResourceView* light_buffer = nullptr) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& bridge = dlaa_bridge::bridge;
  if (!(runtime.settings.reflections || runtime.settings.specular_occlusion) || runtime.traced_frame != runtime.frame
      || !bridge.rt_reflection.texture) {
    return {};
  }
  if (runtime.reflected_frame != runtime.frame) {
    runtime.reflected_frame = runtime.frame;
    runtime.water_traced = false;
    if (runtime.settings.reflections && runtime.settings.water_reflections && surface_depth != nullptr
        && internal::host.rewrite_guides != nullptr) {
      auto& objects = runtime.objects;
      ComPtr<ID3D11Resource> resource;
      surface_depth->GetResource(resource.GetAddressOf());
      if (objects.surface_depth_resource != resource.Get()) {
        objects.surface_depth.Reset();
        objects.surface_depth_resource = resource.Get();
        ComPtr<ID3D11Texture2D> texture;
        D3D11_TEXTURE2D_DESC desc = {};
        if (SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        switch (desc.Format) {
          case DXGI_FORMAT_R24G8_TYPELESS:    format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
          case DXGI_FORMAT_R32G8X24_TYPELESS: format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
          case DXGI_FORMAT_R32_TYPELESS:      format = DXGI_FORMAT_R32_FLOAT; break;
          case DXGI_FORMAT_R16_TYPELESS:      format = DXGI_FORMAT_R16_UNORM; break;
          default:                            break;
        }
        ComPtr<ID3D11Device> device;
        native->GetDevice(device.GetAddressOf());
        D3D11_SHADER_RESOURCE_VIEW_DESC view = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D};
        view.Texture2D.MipLevels = 1;
        if (format != DXGI_FORMAT_UNKNOWN && desc.SampleDesc.Count == 1) {
          device->CreateShaderResourceView(resource.Get(), &view, objects.surface_depth.GetAddressOf());
        }
      }
      if (objects.surface_depth) {
        internal::Dispatch(native, objects.positions_shader.Get(), {objects.surface_depth.Get(), nullptr},
                           {bridge.rt_positions.uav.Get(), bridge.rt_sun.uav.Get()}, &runtime.positions, sizeof(runtime.positions),
                           (runtime.positions.size[0] + 7) / 8, (runtime.positions.size[1] + 7) / 8);
        internal::host.rewrite_guides(native);
        runtime.water_traced = true;
      }
    }
    bool light_copied = false;
    if (runtime.settings.reflections && light_buffer != nullptr && bridge.rt_light.texture) {
      ComPtr<ID3D11Resource> light_resource;
      light_buffer->GetResource(light_resource.GetAddressOf());
      ComPtr<ID3D11Texture2D> light_texture;
      D3D11_TEXTURE2D_DESC light_desc = {};
      if (SUCCEEDED(light_resource.As(&light_texture))) light_texture->GetDesc(&light_desc);
      const bool readable = light_desc.SampleDesc.Count == 1 && light_desc.Width >= runtime.traced_size[0] && light_desc.Height >= runtime.traced_size[1];
      if (readable && light_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT) {
        const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = runtime.traced_size[0], .bottom = runtime.traced_size[1], .back = 1};
        native->CopySubresourceRegion(bridge.rt_light.texture.Get(), 0, 0, 0, 0, light_texture.Get(), 0, &region);
        light_copied = true;
      } else if (readable && light_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT && !internal::host.light_copy.empty()) {
        auto& shader = runtime.objects.light_copy_shader;
        if (!shader) {
          ComPtr<ID3D11Device> device;
          native->GetDevice(device.GetAddressOf());
          device->CreateComputeShader(internal::host.light_copy.data(), internal::host.light_copy.size(), nullptr, shader.GetAddressOf());
        }
        if (shader) {
          const std::array<uint32_t, 4> traced_size = {runtime.traced_size[0], runtime.traced_size[1], 0, 0};
          internal::Dispatch(native, shader.Get(), {light_buffer, nullptr}, {bridge.rt_light.uav.Get(), nullptr}, traced_size.data(),
                             sizeof(traced_size), (traced_size[0] + 7) / 8, (traced_size[1] + 7) / 8);
          light_copied = true;
          if (!std::exchange(runtime.light_copy_logged, true)) {
            renodx::utils::log::i(std::format("[Total War] Ray-traced reflections: the hits read the pass's light (R16G16B16A16F, copied), {} x {}.",
                                              traced_size[0], traced_size[1]));
          }
        }
      }
    }
    const dlaa_bridge::ReflectRequest request = {
        .frame = static_cast<uint32_t>(runtime.frame),
        .width = runtime.traced_size[0],
        .height = runtime.traced_size[1],
        .flags = (runtime.settings.reflections ? dlaa_bridge::RT_REFLECTIONS : 0u)
                 | (runtime.settings.specular_occlusion ? dlaa_bridge::RT_SPECULAR_OCCLUSION : 0u)
                 | (dlaa_bridge::bridge.reconstructing ? dlaa_bridge::RT_REFLECTIONS_RAW : 0u)
                 | (dlaa_bridge::bridge.nrd_ask_reflections ? dlaa_bridge::RT_PATH_NRD : 0u)
                 | (runtime.settings.path_ser ? dlaa_bridge::RT_PATH_SER : 0u)
                 | (runtime.settings.reflection_reuse ? dlaa_bridge::RT_REFLECTIONS_REUSE : 0u)
                 | (runtime.settings.reflection_own ? dlaa_bridge::RT_REFLECTIONS_OWN : 0u)
                 | (runtime.settings.water_steady ? dlaa_bridge::RT_REFLECTIONS_STEADY : 0u)
                 | ((runtime.settings.water_soft && runtime.water_traced) ? dlaa_bridge::RT_REFLECTIONS_WATER_POSITIONS : 0u)
                 | (runtime.settings.reflection_squared_lobe ? dlaa_bridge::RT_REFLECTIONS_SQUARED_LOBE : 0u)
                 | (light_copied ? dlaa_bridge::RT_REFLECTIONS_LIGHT : 0u)
                 // Only over the scene's light before its fog (this pass's buffer, or the Light Bounce's prepared light): last
                 // frame's picture has its fog in it.
                 | ((runtime.settings.reflection_fog && (light_copied || runtime.bounced_frame == runtime.frame)) ? dlaa_bridge::RT_REFLECTIONS_FOG : 0u),
        .samples = runtime.settings.reflection_samples,
        .bounces = runtime.settings.path_tracing ? runtime.settings.path_bounces : 0u,
        .range = runtime.settings.light_bounce_range * internal::RangeScale(),
        .reflection_range = 10.f * internal::RangeScale(),
        .view_projection = runtime.traced_view_projection,
        .guides = internal::host.guides_written(),
    };
    const bool reflected = dlaa_bridge::TraceReflections(native, request);
    if (reflected != runtime.reflected || (reflected && runtime.water_traced != runtime.water_traced_logged)) {
      renodx::utils::log::i("[Total War] Ray-traced reflections: ", (reflected ? "tracing" : "not traced (the game's)"),
                            ((reflected && runtime.water_traced) ? ", the water from its own surface" : ""), ".");
      runtime.water_traced_logged = reflected && runtime.water_traced;
    }
    runtime.reflected = reflected;
  }
  if (!runtime.reflected) return {};
  return {bridge.rt_reflection.srv.Get(), bridge.rt_specular.srv.Get()};
}

// Binds the ray-traced reflections (t47, with the reflections on) and the specular occlusion (t46, with it on) for the game's
// reflection pass's draw or dispatch (`compute`), or unbinds them (`bind` false); a draw's b10 says whether the water
// was traced from its own surfaces (`surface_depth`: Reflections). True: the reflections are bound (the pass leaves out
// its screen-space march; rt_reflection.hlsli).
inline bool BindReflections(reshade::api::command_list* cmd_list, bool compute, bool bind, ID3D11ShaderResourceView* surface_depth = nullptr,
                            ID3D11ShaderResourceView* light_buffer = nullptr) {
  ID3D11DeviceContext* native = internal::NativeContext(cmd_list);
  if (native == nullptr) return false;
  const auto views = (bind && internal::active.load(std::memory_order_relaxed)) ? Reflections(native, surface_depth, light_buffer)
                                                                                 : std::array<ID3D11ShaderResourceView*, 2>{};
  bool reflected = false;
  bool occluded = false;
  ID3D11Buffer* water = nullptr;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    reflected = views[0] != nullptr && internal::runtime.settings.reflections;
    occluded = views[1] != nullptr && internal::runtime.settings.specular_occlusion;
    if (reflected && internal::runtime.water_traced) water = internal::runtime.objects.water_flags.Get();
  }
  const std::array<ID3D11ShaderResourceView*, 2> bound = {occluded ? views[1] : nullptr, reflected ? views[0] : nullptr};
  if (compute) {
    native->CSSetShaderResources(46, static_cast<UINT>(bound.size()), bound.data());
  } else {
    native->PSSetShaderResources(46, static_cast<UINT>(bound.size()), bound.data());
    native->PSSetConstantBuffers(10, 1, &water);
  }
  return reflected;
}

// The game's screen-space reflection march and its blurs (Warhammer, Warhammer II: environment_specular_light raycast,
// filter_blur): left out while the ray-traced reflections replace it in the resolve. True: dispatch.
inline bool OnReflectionMarch(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::NativeContext(cmd_list);
  if (native == nullptr || !internal::active.load(std::memory_order_relaxed)) return true;
  const auto views = Reflections(native);
  const std::unique_lock lock(internal::runtime.mutex);
  return views[0] == nullptr || !internal::runtime.settings.reflections;
}

constexpr auto OnSceneDraw = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (std::is_same_v<Arguments, renodx::utils::command_action::IndirectArguments>) {
    if (!internal::active.load(std::memory_order_relaxed) || !dlaa_bridge::bridge.ray_tracing) return {};
    return {.post_callback = internal::CaptureIndirect<Context>};
  } else if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                       && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    // Nothing without the bridge's ray tracing textures (menus: the helper not started; ATN's debug28 menu at 140 fps).
    if (!internal::active.load(std::memory_order_relaxed) || !dlaa_bridge::bridge.ray_tracing) return {};
    if (!command.template GetCallbackData<SceneShader>().cascade_keep.empty()) {
      internal::CaptureDraw<Context>(command, nullptr);
      return {};
    }
    return {.post_callback = internal::CaptureDraw<Context>};
  }
};

// Rays cast the sun's shadow and did last frame: a pass before this frame's trace may count on them (a frame with no trace: at most one).
inline bool SunShadowsTraced() {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  return runtime.settings.sun_shadows && runtime.traced_frame + 1 >= runtime.frame;
}

// The sun light's occlusion for the patched sun shaders this frame (the screen-space shadows' `screen_space` mask, or
// null without it, and the traced sun's, the darker); null: no traced sun this frame (the screen-space mask alone).
inline ID3D11ShaderResourceView* Occlusion(ID3D11DeviceContext* native, ID3D11ShaderResourceView* screen_space) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& objects = runtime.objects;
  // Coloured Shadows' filter at t50 (screen_space_shadows' TINT_SLOT) with this frame's trace, else none.
  ID3D11ShaderResourceView* tint = (runtime.settings.coloured_shadows && runtime.traced_frame == runtime.frame) ? dlaa_bridge::bridge.rt_tint.srv.Get() : nullptr;
  native->PSSetShaderResources(50, 1, &tint);
  if (!runtime.settings.sun_shadows || runtime.traced_frame != runtime.frame) return nullptr;
  if (runtime.occlusion_frame == runtime.frame) return objects.occlusion_srv.Get();
  D3D11_TEXTURE2D_DESC current = {};
  if (objects.occlusion) objects.occlusion->GetDesc(&current);
  if (current.Width != dlaa_bridge::bridge.width || current.Height != dlaa_bridge::bridge.height) {
    objects.occlusion.Reset();
    objects.occlusion_srv.Reset();
    objects.occlusion_uav.Reset();
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = dlaa_bridge::bridge.width,
        .Height = dlaa_bridge::bridge.height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R8_UNORM,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
    };
    if (FAILED(objects.device->CreateTexture2D(&desc, nullptr, objects.occlusion.GetAddressOf()))
        || FAILED(objects.device->CreateShaderResourceView(objects.occlusion.Get(), nullptr, objects.occlusion_srv.GetAddressOf()))
        || FAILED(objects.device->CreateUnorderedAccessView(objects.occlusion.Get(), nullptr, objects.occlusion_uav.GetAddressOf()))) {
      objects.occlusion.Reset();
      objects.occlusion_srv.Reset();
      objects.occlusion_uav.Reset();
      return nullptr;
    }
  }
  // Not bound for the pixel shaders while written.
  ID3D11ShaderResourceView* none = nullptr;
  native->PSSetShaderResources(49, 1, &none);
  {
    const internal::ComputeState kept(native);
    const std::array<uint32_t, 4> size = {runtime.traced_size[0], runtime.traced_size[1], 0, 0};
    internal::Dispatch(native, objects.occlusion_shader.Get(), {dlaa_bridge::bridge.rt_visibility.srv.Get(), screen_space},
                       {objects.occlusion_uav.Get(), nullptr}, size.data(), sizeof(size), (dlaa_bridge::bridge.width + 7) / 8,
                       (dlaa_bridge::bridge.height + 7) / 8);
  }
  runtime.occlusion_frame = runtime.frame;
  return objects.occlusion_srv.Get();
}

// The Debug View (Settings.debug_view; 9: all eight tiled) over `target`, the tone mapper's output (the bridge
// textures' size, RGBA16F): that effect's result this frame as the picture (rt_debug; magenta where it made none).
inline void DrawDebugView(ID3D11DeviceContext* native, ID3D11Texture2D* target) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& objects = runtime.objects;
  const auto& bridge = dlaa_bridge::bridge;
  const uint32_t view = runtime.settings.debug_view;
  if (view == 0 || !objects.debug_shader || !bridge.ray_tracing) return;
  D3D11_TEXTURE2D_DESC desc = {};
  target->GetDesc(&desc);
  if (desc.Width != bridge.width || desc.Height != bridge.height || desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1
      || (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS)) {
    return;
  }
  D3D11_TEXTURE2D_DESC current = {};
  if (objects.debug_texture) objects.debug_texture->GetDesc(&current);
  if (current.Width != desc.Width || current.Height != desc.Height) {
    objects.debug_uav.Reset();
    objects.debug_texture.Reset();
    D3D11_TEXTURE2D_DESC debug_desc = desc;
    debug_desc.Usage = D3D11_USAGE_DEFAULT;
    debug_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    debug_desc.CPUAccessFlags = 0;
    debug_desc.MiscFlags = 0;
    const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
    if (FAILED(objects.device->CreateTexture2D(&debug_desc, nullptr, objects.debug_texture.GetAddressOf()))
        || FAILED(objects.device->CreateUnorderedAccessView(objects.debug_texture.Get(), &uav_desc, objects.debug_uav.GetAddressOf()))) {
      objects.debug_uav.Reset();
      objects.debug_texture.Reset();
      return;
    }
  }
  // The views' results (rt_debug's t0-t6) and which this frame made.
  const bool traced = runtime.traced_frame == runtime.frame;
  const bool reflected = runtime.reflected && runtime.reflected_frame == runtime.frame;
  const std::array<bool, 8> made = {traced && runtime.settings.sun_shadows,
                                    traced && runtime.settings.ambient_occlusion,
                                    runtime.bounced_frame == runtime.frame,
                                    reflected && runtime.settings.reflections,
                                    reflected && runtime.settings.specular_occlusion,
                                    traced && runtime.lights_traced,
                                    traced && runtime.settings.fog_shadows,
                                    runtime.debug_ambient_frame == runtime.frame && objects.debug_ambient};
  uint32_t made_bits = 0;
  for (size_t index = 0; index < made.size(); ++index) made_bits |= made[index] ? (1u << index) : 0u;
  struct DebugConstants {
    std::array<uint32_t, 4> view;
    std::array<float, 4> size;
    std::array<float, 4> ao_mapping;
  };
  const DebugConstants constants = {
      .view = {view, made_bits, static_cast<uint32_t>(runtime.lights.size()), runtime.bounce_scale},
      .size = {static_cast<float>(runtime.traced_size[0]), static_cast<float>(runtime.traced_size[1]), static_cast<float>(desc.Width),
               static_cast<float>(desc.Height)},
      .ao_mapping = {runtime.debug_ambient_mapping, 0.f, 0.f, 0.f},
  };
  {
    const internal::ComputeState kept(native);
    // t2-t7 here (put back after), t0-t1 by Dispatch.
    std::array<ID3D11ShaderResourceView*, 6> game_views = {};
    native->CSGetShaderResources(2, static_cast<UINT>(game_views.size()), game_views.data());
    const std::array<ID3D11ShaderResourceView*, 6> results = {bridge.rt_bounce.srv.Get(),     bridge.rt_reflection.srv.Get(),
                                                              bridge.rt_specular.srv.Get(),   bridge.rt_light_mask.srv.Get(),
                                                              bridge.rt_fog.srv.Get(),        objects.debug_ambient.Get()};
    native->CSSetShaderResources(2, static_cast<UINT>(results.size()), results.data());
    internal::Dispatch(native, objects.debug_shader.Get(), {bridge.rt_visibility.srv.Get(), bridge.rt_ambient.srv.Get()},
                       {objects.debug_uav.Get(), nullptr}, &constants, sizeof(constants), (desc.Width + 7) / 8, (desc.Height + 7) / 8);
    native->CSSetShaderResources(2, static_cast<UINT>(game_views.size()), game_views.data());
    for (auto* game_view : game_views) {
      if (game_view != nullptr) game_view->Release();
    }
  }
  native->CopyResource(target, objects.debug_texture.Get());
}

// Every present: the settings and a new frame; scene keys unseen for RELEASE_FRAMES forgotten (the helper released them).
inline void Update(const Settings& settings) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (!(runtime.settings == settings)) runtime.status_log_time = GetTickCount64() + 3000;
  runtime.settings = settings;
  const bool nrd = settings.nrd && !dlaa_bridge::bridge.reconstructing;
  // Path-Traced AO is the Light Bounce's light: with AO under Ray Reconstruction at REBLUR it cleans that too, as RTAO.
  const bool nrd_path_ambient = settings.ao_reblur && dlaa_bridge::bridge.reconstructing && settings.path_tracing && settings.path_ambient;
  dlaa_bridge::bridge.nrd_ask_bounce = (nrd || nrd_path_ambient) && settings.light_bounce;
  dlaa_bridge::bridge.nrd_ask_reflections = nrd && settings.reflections;
  const uint32_t nrd_off = (dlaa_bridge::bridge.shared != nullptr) ? dlaa_bridge::bridge.shared->rt_nrd_off : 0u;
  dlaa_bridge::bridge.nrd_ambient = nrd && settings.ambient_occlusion && (nrd_off & dlaa_bridge::RT_NRD_OFF_AMBIENT) == 0;
  dlaa_bridge::bridge.nrd_bounce = dlaa_bridge::bridge.nrd_ask_bounce && (nrd_off & dlaa_bridge::RT_NRD_OFF_BOUNCE) == 0;
  dlaa_bridge::bridge.nrd_reflections = dlaa_bridge::bridge.nrd_ask_reflections && (nrd_off & dlaa_bridge::RT_NRD_OFF_REFLECTIONS) == 0;
  if (!settings.smoke_shadows && !settings.stream_particles) {
    runtime.particle_stream = {};
    runtime.particles = 0;
  }
  dlaa_bridge::bridge.details = settings.debug_log;
  internal::active.store(internal::Enabled(), std::memory_order_relaxed);
  ++runtime.frame;
  // The helper started its ray tracing again (a GPU fault or hang removed its device, or a request failed) or, with the rays
  // off, released its scene after 10 s: every mesh captured again.
  if (const uint32_t device = dlaa_bridge::RayTracingDevice(); device != runtime.helper_device) {
    runtime.helper_device = device;
    runtime.known.clear();
    runtime.short_keys.clear();
    for (auto& [set_key, mesh] : runtime.cluster_meshes) mesh.sent = false;
    if (internal::Enabled() || dlaa_bridge::bridge.ray_tracing) {
      renodx::utils::log::w("[Total War] Ray tracing: the helper started again (why: renodx-tw-dlaa64.log), the scene is captured again.");
    }
  }
  // Off for longer than the helper's IDLE_RELEASE: its scene is gone and nothing points at these, the addon's memory and
  // its hold on the game's mask textures back.
  if (const uint64_t tick = GetTickCount64(); internal::Enabled() || dlaa_bridge::bridge.ray_tracing) {
    runtime.wanted_tick = tick;
  } else if (runtime.objects.device && tick > runtime.wanted_tick + 15000) {
    runtime.objects = {};
    runtime.atlas_slices.clear();
    dlaa_bridge::bridge.rt_atlas = {};
  }
  // Its full set pool dropped sets (v16): each sent again as it is drawn.
  if (const uint32_t state = dlaa_bridge::CompactState(); state != runtime.compact_state) {
    runtime.compact_state = state;
    for (auto& [set_key, mesh] : runtime.cluster_meshes) mesh.sent = false;
  }
  if (const auto [left_out, key] = dlaa_bridge::RayTracingLeftOut(); left_out != runtime.helper_left_out) {
    const auto shader = runtime.key_shaders.find(key);
    runtime.left_out_shaders[(shader != runtime.key_shaders.end()) ? shader->second : 0u] +=
        (left_out > runtime.helper_left_out ? left_out - runtime.helper_left_out : left_out);
    runtime.helper_left_out = left_out;
  }
  auto& costs = runtime.costs;
  if (internal::Enabled()) {
    ++costs.frames;
    costs.dynamic_vertices += runtime.dynamic_vertices;
  }
  const bool timed = runtime.timed;
  runtime.timed = settings.debug_log || runtime.profile.file.is_open();
  if (auto& objects = runtime.objects; timed && objects.device) {
    ComPtr<ID3D11DeviceContext> context;
    objects.device->GetImmediateContext(context.GetAddressOf());
    for (auto& timing : objects.timings) {
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
      std::array<UINT64, 3> stamps = {};
      if (!timing.pending || context->GetData(timing.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
        continue;
      }
      bool ready = true;
      for (size_t index = 0; index < stamps.size(); ++index) {
        ready = ready && context->GetData(timing.stamps[index].Get(), &stamps[index], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
      }
      if (!ready) continue;
      timing.pending = false;
      if (disjoint.Disjoint || disjoint.Frequency == 0 || stamps[1] < stamps[0] || stamps[2] < stamps[1]) continue;
      const double passes_ms = static_cast<double>(stamps[1] - stamps[0]) * 1000.0 / static_cast<double>(disjoint.Frequency);
      const double waiting_ms = static_cast<double>(stamps[2] - stamps[1]) * 1000.0 / static_cast<double>(disjoint.Frequency);
      costs.passes_ms += passes_ms;
      costs.waiting_ms += waiting_ms;
      ++costs.timed;
      runtime.profile.gpu_frame = timing.frame;
      runtime.profile.passes_ms = passes_ms;
      runtime.profile.waiting_ms = waiting_ms;
    }
  }
  // The profile's line for the frame that ended (runtime.frame - 1).
  if (auto& profile = runtime.profile; profile.file.is_open()) {
    LARGE_INTEGER now;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const auto ms = [&](int64_t ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency.QuadPart); };
    const int64_t set_ticks = runtime.set_ticks.load();
    profile.file << std::format("{},{:.2f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{},{},{},{},{},{:.3f},{:.3f},{:.3f},{:.3f},{},{},{},{:.3f},{:.3f},{:.3f}\n",
                                runtime.frame - 1, ms(now.QuadPart - profile.started.QuadPart), ms(now.QuadPart - profile.presented.QuadPart),
                                ms(costs.capture_ticks - profile.capture_ticks), ms(costs.trace_ticks - profile.trace_ticks),
                                ms(dlaa_bridge::bridge.answer_ticks - profile.answer_ticks), ms(set_ticks - profile.set_ticks), runtime.set_builds,
                                profile.counts[0], profile.counts[1], profile.counts[2], profile.counts[3], static_cast<double>(profile.counts[4]) / 1.0e6,
                                runtime.dynamic_vertices / 1.0e6, static_cast<double>(costs.left_out - profile.left_out) / 1.0e6,
                                static_cast<double>(costs.no_room - profile.no_room) / 1.0e6, profile.counts[5],
                                (runtime.traced_frame + 1 == runtime.frame) ? 1 : 0, profile.gpu_frame, profile.passes_ms, profile.waiting_ms,
                                ms(dlaa_bridge::bridge.present_answer_ticks - profile.present_answer_ticks));
    profile.counts = {};
    profile.presented = now;
    profile.capture_ticks = costs.capture_ticks;
    profile.trace_ticks = costs.trace_ticks;
    profile.answer_ticks = dlaa_bridge::bridge.answer_ticks;
    profile.present_answer_ticks = dlaa_bridge::bridge.present_answer_ticks;
    profile.set_ticks = set_ticks;
    profile.left_out = costs.left_out;
    profile.no_room = costs.no_room;
    ++profile.frames;
    if (GetTickCount64() >= profile.end_time) {
      profile.file.close();
      renodx::utils::log::i(std::format("[Total War] Ray tracing profile: {} frames saved to {} (the helper's requests: tw_rt_profile_helper.csv).",
                                        profile.frames, profile.path.string()));
    }
  }
  if (runtime.status_log_time != 0 && GetTickCount64() >= runtime.status_log_time) {
    runtime.status_log_time = 0;
    std::string effects;
    for (const auto& [on, name] : {std::pair{settings.sun_shadows, "sun shadows"}, std::pair{settings.ambient_occlusion, "AO"},
                                   std::pair{settings.light_bounce, "Light Bounce"}, std::pair{settings.path_tracing, "path tracing"},
                                   std::pair{settings.reflections, "reflections"}, std::pair{settings.specular_occlusion, "specular occlusion"},
                                   std::pair{settings.local_lights, "point lights"}, std::pair{settings.fog_shadows, "fog shadows"},
                                   std::pair{settings.micromaps, "leaf micromaps"}, std::pair{settings.smoke_shadows, "smoke shadows"}}) {
      if (on) effects += (effects.empty() ? "" : ", ") + std::string(name);
    }
    constexpr std::array<const char*, 5> TESTS = {"", " (scene test: the terrain alone)", " (scene test: objects and trees alone)",
                                                  " (scene test: no leaves)", " (scene test: none kept off-screen)"};
    // The Light Bounce's rays and bounces too: its GPU time follows them (ATN's 2026-11-13 log: 19-40 ms for a minute
    // between two settings changes, 5-7 ms around them, the rays not in the log).
    renodx::utils::log::i("[Total War] Ray tracing with ", (effects.empty() ? std::string("nothing") : effects),
                          TESTS[std::min<size_t>(settings.scene_test, TESTS.size() - 1)],
                          settings.light_bounce ? std::format(" (Light Bounce {} rays, {} bounces)", settings.light_bounce_rays,
                                                              settings.path_tracing ? settings.path_bounces : 0u)
                                                : std::string(),
                          ": ", dlaa_bridge::TraceStatus());
  }
  if (const uint64_t now = GetTickCount64(); now >= runtime.new_static_time + 10000) {
    if (!settings.debug_log) {
      runtime.left_out_shaders.clear();
      runtime.shadow_readers.clear();
    }
    if (!runtime.new_static.empty()) {
      std::vector<std::pair<uint32_t, uint32_t>> counts(runtime.new_static.begin(), runtime.new_static.end());
      std::ranges::sort(counts, std::ranges::greater{}, &std::pair<uint32_t, uint32_t>::second);
      std::string line;
      for (size_t index = 0; index < std::min<size_t>(counts.size(), 6); ++index) {
        line += std::format("{}{:08X} x{}", (index == 0 ? "" : ", "), counts[index].first, counts[index].second);
      }
      renodx::utils::log::i("[Total War] Ray tracing: new static meshes in 10 s by vertex shader: ", line, " (", runtime.known.size(), " known).");
      for (const auto& example : runtime.new_static_examples[counts.front().first]) {
        renodx::utils::log::i(std::format("[Total War] Ray tracing: new {:08X}: {}.", counts.front().first, example));
      }
      runtime.new_static.clear();
      runtime.new_static_examples.clear();
    }
    for (const auto& [hash, triangles] : runtime.left_out_shaders) {
      renodx::utils::log::w(std::format("[Total War] Ray tracing: {} garbage triangles (far off or stretched) left out in 10 s, the last in "
                                        "a capture of vertex shader {:08X}.",
                                        triangles, hash));
    }
    runtime.left_out_shaders.clear();
    runtime.key_shaders.clear();
    if (!runtime.shadow_readers.empty()) {
      std::string readers;
      for (const uint64_t pair : runtime.shadow_readers) {
        readers += std::format("{}{:08X} (vs {:08X})", (readers.empty() ? "" : ", "), pair >> 32, pair & 0xFFFFFFFFu);
      }
      renodx::utils::log::i("[Total War] Ray tracing: pixel shaders reading the game's sun shadow map without the rays' shadows "
                            "(lit everywhere under Ray-Traced Shadows Only): ", readers, ".");
      runtime.shadow_readers.clear();
    }
    if (runtime.settings.shadows_only && settings.debug_log) runtime.readers_frame = runtime.frame + 1;
    for (const auto& [hash, draws] : runtime.dynamic_buffers) {
      renodx::utils::log::i(std::format("[Total War] Ray tracing: {:08X} in dynamic vertex buffers, captured every frame: {} draws in 10 s.",
                                        hash, draws));
    }
    runtime.dynamic_buffers.clear();
    if (costs.frames != 0 && settings.debug_log) {
      LARGE_INTEGER frequency;
      QueryPerformanceFrequency(&frequency);
      const double frame_ms = 1000.0 / static_cast<double>(frequency.QuadPart) / costs.frames;
      renodx::utils::log::i(std::format("[Total War] Ray tracing's own cost a frame: CPU {:.2f} ms capturing, {:.2f} ms tracing ({:.2f} of it the terrain's count); GPU "
                                        "{:.2f} ms its passes, {:.2f} ms waiting for the helper; every frame {:.2f} M vertices captured "
                                        "({:.0f} compact draws; {} cluster sets, {} unsorted), {:.2f} M left out (their share full), {:.2f} M "
                                        "without room (captures or vertex texture full).",
                                        costs.capture_ticks * frame_ms, costs.trace_ticks * frame_ms, costs.terrain_wait_ticks * frame_ms,
                                        (costs.timed != 0 ? costs.passes_ms / costs.timed : 0.0),
                                        (costs.timed != 0 ? costs.waiting_ms / costs.timed : 0.0),
                                        static_cast<double>(costs.dynamic_vertices) / 1.0e6 / costs.frames,
                                        static_cast<double>(costs.compact_draws) / costs.frames,
                                        std::ranges::count_if(runtime.cluster_meshes, [](const auto& entry) { return static_cast<bool>(entry.second.remap); }),
                                        std::ranges::count_if(runtime.cluster_meshes, [](const auto& entry) { return entry.second.remap && entry.second.unsorted; }),
                                        static_cast<double>(costs.left_out) / 1.0e6 / costs.frames,
                                        static_cast<double>(costs.no_room) / 1.0e6 / costs.frames));
      // What was left out, and which share was full (the largest first).
      std::vector<std::tuple<uint64_t, uint32_t, size_t>> left_out;
      for (const auto& [hash, shares] : costs.left_out_shaders) {
        for (size_t share = 0; share < shares.size(); ++share) {
          if (shares[share] != 0) left_out.emplace_back(shares[share], hash, share);
        }
      }
      std::ranges::sort(left_out, std::greater<>());
      std::string shaders;
      for (const auto& [texels, hash, share] : std::span(left_out).first(std::min<size_t>(left_out.size(), 8))) {
        shaders += std::format("{}{:08X} {:.2f} M ({} full)", shaders.empty() ? "" : ", ", hash,
                               static_cast<double>(texels) / 1.0e6 / costs.frames, internal::SHARE_NAMES[share]);
      }
      if (!shaders.empty()) renodx::utils::log::i("[Total War] Ray tracing: left out a frame by vertex shader: ", shaders, ".");
      if (costs.particles != 0) {
        renodx::utils::log::i(std::format("[Total War] Ray tracing: Smoke Shadows, {:.0f} particles a frame, most quads {}, least {:.2f}.",
                                          static_cast<double>(costs.particles) / costs.frames, costs.most_smoke_quads, runtime.objects.smoke_least));
      }
    }
    costs = {};
    runtime.profile.capture_ticks = 0;
    runtime.profile.trace_ticks = 0;
    runtime.profile.left_out = 0;
    runtime.profile.no_room = 0;
    runtime.new_static_time = now;
    // Meshes not drawn for RELEASE_FRAMES give their sets (and the game's index buffers) back.
    std::erase_if(runtime.cluster_meshes, [&](const auto& entry) { return entry.second.used_frame + internal::RELEASE_FRAMES < runtime.frame; });
    // Mask textures not used for as long give their atlas slices (and the textures themselves) back.
    std::erase_if(runtime.atlas_slices, [&](const auto& entry) {
      const uint32_t slice = entry.second.second;
      if (runtime.slice_used[slice] + internal::RELEASE_FRAMES >= runtime.frame) return false;
      runtime.free_slices.push_back(slice);
      return true;
    });
  }
  runtime.captures.clear();
  runtime.captured_keys.clear();
  runtime.slices.clear();
  runtime.captured_vertices = 0;
  runtime.world_vertices = internal::WORLD_BASE;
  runtime.tessellated_draws = 0;
  runtime.dynamic_vertices = 0;
  runtime.unit_vertices = 0;
  runtime.unit_triangles = 0;
  runtime.unit_clusters = 0;
  runtime.grass_vertices = 0;
  runtime.alpha_corners = dlaa_bridge::DynamicUvs();
  runtime.dynamic_triangles = 0;
  runtime.dynamic_clusters = 0;
  runtime.tree_vertices = 0;
  runtime.tree_corners = 0;
  runtime.set_builds = 0;
  runtime.drawn.clear();
  runtime.placements.clear();
  runtime.compact.clear();
  runtime.new_sets.clear();
  runtime.new_set_bytes = 0;
  std::swap(runtime.lights_last, runtime.lights_drawn);
  runtime.lights_drawn.clear();
  // Host::keep_placed: as long as there's room. Then the oldest meshes go, here and in the helper by the one number it's sent.
  if (internal::host.keep_placed && runtime.settings.casters_out_of_view && internal::host.placed_row != nullptr && *internal::host.placed_row != 0.f) {
    if (internal::Enabled() && runtime.frame - runtime.main_view_frame == internal::SCENE_GONE_FRAMES) {
      runtime.known.clear();
      runtime.short_keys.clear();
      runtime.scene_gone = true;
    } else if (runtime.kept_frames == 0 || (runtime.frame % 64) == 0) {
      std::vector<std::pair<uint64_t, uint32_t>> kept;
      for (const auto& [key, seen] : runtime.known) {
        if (seen.placed_mesh) kept.emplace_back(seen.frame, seen.triangles);
      }
      std::ranges::sort(kept, std::ranges::greater{});
      uint64_t kept_triangles = 0;
      runtime.kept_frames = internal::KEPT_FOREVER;
      for (size_t mesh = 0; mesh < kept.size(); ++mesh) {
        kept_triangles += kept[mesh].second;
        if (kept_triangles > internal::KEPT_TRIANGLES || mesh >= internal::KEPT_MESHES) {
          runtime.kept_frames = static_cast<uint32_t>(std::max<uint64_t>(runtime.frame - kept[mesh].first, internal::PLACE_TURNS * 2));
          break;
        }
      }
    }
  } else {
    runtime.kept_frames = 0;
  }
  const uint64_t kept_release = (runtime.kept_frames != 0 ? runtime.kept_frames : internal::RELEASE_FRAMES);
  if (std::erase_if(runtime.known, [&](const auto& entry) { return entry.second.frame + (entry.second.placed_mesh ? kept_release : internal::RELEASE_FRAMES) < runtime.frame; }) != 0) {
    std::erase_if(runtime.short_keys, [&](const auto& entry) { return !runtime.known.contains(entry.second.key); });
  }
}

// Record Ray Tracing Profile: PROFILE_MS of frames from the next present (Profile).
inline void StartProfile() {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& profile = runtime.profile;
  if (profile.file.is_open()) return;
  profile = {};
  profile.path = renodx::utils::platform::GetCurrentProcessPath().parent_path() / "tw_rt_profile.csv";
  profile.file.open(profile.path, std::ios::trunc);
  if (!profile.file) {
    renodx::utils::log::w("[Total War] Ray tracing profile: ", profile.path.string(), " could not be written.");
    return;
  }
  profile.file << "frame,time_ms,frame_ms,capture_ms,trace_ms,answer_wait_ms,set_worker_ms,sets_started,captures,compact_draws,placements,"
                  "new_sets,captured_mtexels,dynamic_mtexels,left_out_mtexels,no_room_mtexels,lights,traced,gpu_frame,gpu_passes_ms,"
                  "gpu_helper_wait_ms,present_pacing_ms\n";
  profile.end_time = GetTickCount64() + internal::PROFILE_MS;
  QueryPerformanceCounter(&profile.started);
  profile.presented = profile.started;
  profile.capture_ticks = runtime.costs.capture_ticks;
  profile.trace_ticks = runtime.costs.trace_ticks;
  profile.answer_ticks = dlaa_bridge::bridge.answer_ticks;
  profile.present_answer_ticks = dlaa_bridge::bridge.present_answer_ticks;
  profile.set_ticks = runtime.set_ticks.load();
  profile.left_out = runtime.costs.left_out;
  profile.no_room = runtime.costs.no_room;
  renodx::utils::log::i("[Total War] Ray tracing profile: recording 30 s into ", profile.path.string(), ".");
}

// Whether ray tracing wants the bridge's textures (the anti-aliasing's DLAA makes them).
inline bool Wanted() {
  const std::unique_lock lock(internal::runtime.mutex);
  return internal::Enabled();
}

// The helper's ray tracing status (one line).
inline std::string Status() {
  return dlaa_bridge::TraceStatus();
}

// This frame's trace from the main view's `depth` (null: the addon wrote the bridge's positions) and `camera` at a pass
// of the game's (the addons' own hooks); false: not traced (off, already traced this frame).
inline bool Trace(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth, const Camera& camera) {
  const std::unique_lock lock(internal::runtime.mutex);
  if (!internal::active.load(std::memory_order_relaxed)) return false;
  internal::Trace(native, depth, camera);
  return internal::runtime.traced_frame == internal::runtime.frame;
}

// The sun's direction for the next trace (toward the sun, world space, unit length), where the game's lighting buffers
// do not hold it in the TW games' layout (rt_sun).
inline void WriteSun(ID3D11DeviceContext* native, const std::array<float, 3>& toward_sun) {
  auto& bridge = dlaa_bridge::bridge;
  if (!bridge.rt_sun.texture) return;
  const std::array<float, 4> sun = {toward_sun[0], toward_sun[1], toward_sun[2], 1.f};
  const D3D11_BOX texel = {.left = 0, .top = 0, .front = 0, .right = 1, .bottom = 1, .back = 1};
  native->UpdateSubresource(bridge.rt_sun.texture.Get(), 0, &texel, sun.data(), 16, 0);
}

// This frame's traced sun and ambient visibility (R8 at the render size, the bridge's textures' top-left; 1 open); null
// when not traced this frame.
inline std::array<ID3D11ShaderResourceView*, 2> Visibilities() {
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.traced_frame != internal::runtime.frame) return {};
  return {dlaa_bridge::bridge.rt_visibility.srv.Get(), dlaa_bridge::bridge.rt_ambient.srv.Get()};
}

// A local light a light draw had (one drawn twice counts once): the next frame's trace picks the nearest and brightest of
// this frame's.
inline void AddLocalLight(const LocalLight& light) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.settings.local_lights || !(light.light[3] > 0.f) || runtime.lights_drawn.size() >= internal::MAX_LOCAL_LIGHTS) return;
  if (std::ranges::any_of(runtime.lights_drawn, [&](const LocalLight& drawn) { return drawn.light == light.light; })) return;
  runtime.lights_drawn.push_back(light);
}

// Many at once (an instanced light draw's: each once).
inline void AddLocalLights(std::span<const LocalLight> lights) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.settings.local_lights) return;
  for (const LocalLight& light : lights) {
    if (runtime.lights_drawn.size() >= internal::MAX_LOCAL_LIGHTS) break;
    if (light.light[3] > 0.f) runtime.lights_drawn.push_back(light);
  }
}

// For a light draw (its pixel shader with rt_local_lights.hlsli): this frame's lit bits at t42 and the traced lights'
// positions at b9; false: none traced this frame (nothing bound).
inline bool BindLocalLights(ID3D11DeviceContext* native) {
  ID3D11ShaderResourceView* mask = nullptr;
  ID3D11Buffer* constants = nullptr;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    if (internal::runtime.traced_frame != internal::runtime.frame || !internal::runtime.lights_traced) return false;
    mask = dlaa_bridge::bridge.rt_light_mask.srv.Get();
    constants = internal::runtime.objects.light_constants.Get();
  }
  native->PSSetShaderResources(42, 1, &mask);
  native->PSSetConstantBuffers(9, 1, &constants);
  return true;
}

// For a fog draw (its pixel shader with rt_fog.hlsli): this frame's fog shadows at t43; false: not traced this frame
// (nothing bound).
inline bool BindFogShadows(ID3D11DeviceContext* native) {
  std::array<ID3D11ShaderResourceView*, 2> fog = {};
  ID3D11Buffer* lights = nullptr;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    if (internal::runtime.traced_frame != internal::runtime.frame || !internal::runtime.settings.fog_shadows) return false;
    fog[0] = dlaa_bridge::bridge.rt_fog.srv.Get();
    // The local lights' light in the fog (t44) and its strength (b9's y) where they were traced with it.
    if (internal::runtime.lights_traced && internal::runtime.settings.light_glow > 0.f) {
      fog[1] = dlaa_bridge::bridge.rt_fog_lights.srv.Get();
      lights = internal::runtime.objects.light_constants.Get();
    }
  }
  native->PSSetShaderResources(43, 2, fog.data());
  native->PSSetConstantBuffers(9, 1, &lights);
  return fog[0] != nullptr;
}

inline void UnbindFogShadows(ID3D11DeviceContext* native) {
  constexpr std::array<ID3D11ShaderResourceView*, 2> NO_FOG = {};
  ID3D11Buffer* no_lights = nullptr;
  native->PSSetShaderResources(43, 2, NO_FOG.data());
  native->PSSetConstantBuffers(9, 1, &no_lights);
}

inline void UnbindLocalLights(ID3D11DeviceContext* native) {
  ID3D11ShaderResourceView* no_mask = nullptr;
  ID3D11Buffer* no_constants = nullptr;
  native->PSSetShaderResources(42, 1, &no_mask);
  native->PSSetConstantBuffers(9, 1, &no_constants);
}

// A scene draw's pixel shader (the one bound; host.scene_draw's): its alpha test into `shader` (foliage: the UV's
// input); false: left out of the scene (a mask read some other way).
inline bool SceneCoverage(ID3D11DeviceContext* native, SceneShader* shader) {
  ComPtr<ID3D11PixelShader> pixel_shader;
  native->PSGetShader(pixel_shader.GetAddressOf(), nullptr, nullptr);
  if (!pixel_shader) return true;
  const std::unique_lock lock(internal::runtime.mutex);
  const auto found = internal::pixel_coverage.find(reinterpret_cast<uint64_t>(pixel_shader.Get()));
  if (found == internal::pixel_coverage.end() || found->second.coverage == internal::Coverage::LEFT_OUT) return false;
  if (found->second.coverage == internal::Coverage::MASKED) {
    shader->foliage = true;
    shader->uv_semantic = found->second.uv_semantic.c_str();
    shader->uv_semantic_index = found->second.uv_index;
  }
  return true;
}

// Whether a game vertex shader (its hash) has bone inputs (host.scene_draw's skinned draws); false: unknown.
inline bool SkinnedVertexShader(uint32_t hash) {
  const std::unique_lock lock(internal::runtime.mutex);
  const auto found = internal::skinned_code.find(hash);
  return found != internal::skinned_code.end() && found->second;
}

// Any draw (host.scene_draw tells the scene's apart).
constexpr auto OnAnyDraw = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (!internal::active.load(std::memory_order_relaxed)) return {};
    return {.post_callback = internal::CaptureAnyDraw<Context>};
  }
};

// The scene's shaders (`shaders`, the addon's, kept while installed; or every draw host.scene_draw accepts) and the
// game's view (`host`).
inline void InstallScene(std::span<const SceneShader> shaders, const Host& host) {
  internal::scene_shaders = shaders;
  internal::host = host;
  // A cascade draw streaming out world positions (Host::cascade_world, SceneShader::cascade_world) is no clip-space capture.
  internal::clip_captures = host.scene_draw != nullptr || std::ranges::any_of(shaders, [&host](const SceneShader& shader) {
                              return shader.capture_shader.empty() && !((host.cascade_world || shader.cascade_world) && !shader.cascade_keep.empty());
                            });
  internal::world_captures = internal::clip_captures && std::ranges::any_of(shaders, [](const SceneShader& shader) {
                               return !shader.rows_shader.empty() || shader.cascade_world;
                             });
  if (internal::clip_captures) reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  if (host.scene_draw != nullptr) reshade::register_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  for (const SceneShader& shader : shaders) {
    renodx::utils::command_action::Register(
        OnSceneDraw, {.shader_hash = shader.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW | (shader.indirect ? renodx::utils::command_action::COMMAND_TYPE_INDIRECT : 0u)}, &shader);
  }
  if (host.scene_draw != nullptr) {
    renodx::utils::command_action::Register(OnAnyDraw, {.shader_hash = 0u, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void UninstallScene() {
  if (internal::clip_captures) reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  if (internal::host.scene_draw != nullptr) reshade::unregister_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  renodx::utils::command_action::Unregister(OnSceneDraw);
  renodx::utils::command_action::Unregister(OnAnyDraw);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::scene_shaders = {};
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.particle_stream.device.Get() == native) internal::runtime.particle_stream = {};
  if (internal::runtime.objects.device.Get() == native) {
    internal::runtime.objects = {};
    // The game's mask textures held for their slices, and the atlas the slices are in (as when ray tracing goes off).
    internal::runtime.atlas_slices.clear();
    dlaa_bridge::bridge.rt_atlas = {};
  }
  internal::runtime.known.clear();
  internal::runtime.short_keys.clear();
  internal::runtime.cluster_meshes.clear();
}

}  // namespace twattila::rt_scene
