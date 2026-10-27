/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <embed/shaders.h>

#include "./d3d12_device.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "./native_state.hpp"

// Ray tracing (Experimental): DXR on a D3D12 device beside the game's D3D11 one. The sharing follows Phantom FX by
// Bustanity (MIT): textures made in D3D11 and a fence made in D3D12, opened on both sides, and ReShade's
// IID_UnwrappedObject for its D3D12 device. With the Agility SDK next to the add-on (D3D12\D3D12Core.dll, Microsoft's
// redistributable) and a driver with shader model 6.9 the device is DXR 1.2: opacity micromaps for the alpha-tested
// meshes and shader execution reordering for the path tracing; without it DXR 1.1 runs the same effects.
//
// Tales has no scene to read from memory, so the scene comes from the game's own depth prepass: each new draw (opaque
// ones without a pixel shader; alpha-tested ones with a known masked pixel shader, through a stream output made from
// their own vertex shader that also keeps the UV) is drawn once more into a stream-output buffer, turned into world
// positions at the sun shadow mask and built into a BLAS that later frames reuse while the draw keeps its buffers
// (compacted once the GPU says how small it can be). Alpha-tested materials' opacity masks are copied into an atlas;
// every triangle's normal and every alpha-tested vertex's UV go into pools D3D12 keeps, each mesh's place in them into a
// record (its instances' InstanceID). Skinned characters are captured every frame and their BLASes refitted (rebuilt
// every CHARACTER_REBUILD_FRAMES frames). At the sun shadow mask (cs 0x2084C889) one pass traces the sun shadows (t40),
// ambient occlusion (t41) and the bounce light; at the lighting pass the reflections, specular occlusion and path tracing
// (t42 - t44). A world radiance cache (rt_cache.hlsli) gives the light of what is off screen.
namespace talesofarise::rt_shadows {

using Microsoft::WRL::ComPtr;

// The game's opaque depth prepass vertex shaders (debug20 frame trace; their only output is SV_Position) and the slot of
// their View uniform buffer; skinned characters are dynamic: captured and refitted every frame.
struct DepthShader {
  uint32_t hash = 0;
  UINT view_slot = 0;
  bool landscape = false;
  bool dynamic = false;
};
constexpr std::array<DepthShader, 6> DEPTH_SHADERS = {{
    {.hash = 0xC81C5E1F, .view_slot = 0},                     // static meshes
    {.hash = 0x287D5CA8, .view_slot = 1},                     // instanced static meshes
    {.hash = 0x344F8ACF, .view_slot = 1},                     // instanced static meshes (another fade)
    {.hash = 0x5F4BCA38, .view_slot = 1, .landscape = true},  // landscape
    {.hash = 0xF1D731D5, .view_slot = 1},                     // spline meshes
    {.hash = 0x2C264050, .view_slot = 0, .dynamic = true},    // skinned characters
}};
// The masked prepass's skinned vertex shaders (bone indices in ATTRIBUTE3): characters' alpha-tested parts and fades.
constexpr std::array<uint32_t, 6> SKINNED_MASKED_SHADERS = {0x00E012F5, 0x75224FEC, 0x993DFBAE, 0xA64BD212, 0xDBFEAC9E, 0xFAC3D190};

// The masked depth prepass's pixel shaders (tools\toa\rt_tests\scan_masked.py over the 3Dmigoto dumps): alpha-tested
// against t0's alpha at a TEXCOORD (scaled by a material register), times a material opacity factor, below 0.3333
// discarded; dithered LOD fades only (opaque for rays); one never draws.
enum class Opacity : uint8_t {
  MASK,
  DITHER,
  NEVER,
};
constexpr uint8_t NO_REGISTER = 0xFF;
struct MaskedShader {
  uint32_t hash = 0;
  Opacity opacity = Opacity::MASK;
  uint8_t uv_index = 0;
  uint8_t uv_slot = NO_REGISTER;
  uint8_t uv_register = 0;
  uint8_t uv_components = 0;  // u's component (x 0 - w 3), v's << 2
  uint8_t alpha_slot = NO_REGISTER;
  uint8_t alpha_register = 0;
  uint8_t alpha_component = 0;
};
constexpr std::array<MaskedShader, 89> MASKED_SHADERS = {{
    {.hash = 0x0008DC5F, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 9, .alpha_component = 1},
    {.hash = 0x0014F7CE, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 14, .alpha_component = 1},
    {.hash = 0x0226F779, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0x05119046, .uv_index = 0, .uv_slot = 1, .uv_register = 11, .uv_components = 10, .alpha_slot = 1, .alpha_register = 14, .alpha_component = 0},
    {.hash = 0x07F803CB, .uv_index = 0},
    {.hash = 0x0DDE8604, .opacity = Opacity::DITHER},
    {.hash = 0x0FCABB5A, .opacity = Opacity::DITHER},
    {.hash = 0x13CDC0CE, .opacity = Opacity::DITHER},
    {.hash = 0x140F37A4, .uv_index = 0, .alpha_slot = 2, .alpha_register = 12, .alpha_component = 3},
    {.hash = 0x14D57477, .opacity = Opacity::DITHER},
    {.hash = 0x14F939B3, .uv_index = 0, .alpha_slot = 1, .alpha_register = 8, .alpha_component = 2},
    {.hash = 0x25C6FD40, .uv_index = 0, .alpha_slot = 2, .alpha_register = 14, .alpha_component = 0},
    {.hash = 0x271C09A5, .opacity = Opacity::DITHER},
    {.hash = 0x289D61AB, .uv_index = 0, .alpha_slot = 1, .alpha_register = 15, .alpha_component = 1},
    {.hash = 0x2AF5074A, .uv_index = 0, .uv_slot = 1, .uv_register = 11, .uv_components = 10, .alpha_slot = 1, .alpha_register = 14, .alpha_component = 0},
    {.hash = 0x38837162, .opacity = Opacity::DITHER},
    {.hash = 0x3A28441F, .uv_index = 0, .uv_slot = 2, .uv_register = 8, .uv_components = 10, .alpha_slot = 2, .alpha_register = 11, .alpha_component = 1},
    {.hash = 0x43FA1072, .opacity = Opacity::NEVER},
    {.hash = 0x448EA110, .opacity = Opacity::DITHER},
    {.hash = 0x494B7371, .uv_index = 0},
    {.hash = 0x49CFE3DD, .opacity = Opacity::DITHER},
    {.hash = 0x4E3B5F01, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 1},
    {.hash = 0x4F2CF089, .uv_index = 0, .alpha_slot = 2, .alpha_register = 15, .alpha_component = 1},
    {.hash = 0x501CA4C9, .opacity = Opacity::DITHER},
    {.hash = 0x50293663, .uv_index = 0},
    {.hash = 0x50ABC826, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0x54BEB679, .opacity = Opacity::DITHER},
    {.hash = 0x58C47F7E, .opacity = Opacity::DITHER},
    {.hash = 0x5BF1BB94, .uv_index = 0, .uv_slot = 2, .uv_register = 3, .uv_components = 4, .alpha_slot = 2, .alpha_register = 13, .alpha_component = 0},
    {.hash = 0x5C39A783, .uv_index = 0, .alpha_slot = 1, .alpha_register = 12, .alpha_component = 3},
    {.hash = 0x5EFDBB5F, .uv_index = 0, .alpha_slot = 2, .alpha_register = 12, .alpha_component = 3},
    {.hash = 0x619DA6AF, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0x63075590, .uv_index = 0},
    {.hash = 0x645FAB59, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 9, .alpha_component = 1},
    {.hash = 0x65C1D32E, .uv_index = 0, .uv_slot = 2, .uv_register = 12, .uv_components = 10, .alpha_slot = 2, .alpha_register = 15, .alpha_component = 1},
    {.hash = 0x665F2310, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0x6CD79653, .opacity = Opacity::DITHER},
    {.hash = 0x6E3B9F09, .opacity = Opacity::DITHER},
    {.hash = 0x6E4D6998, .opacity = Opacity::DITHER},
    {.hash = 0x6FA62573, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 8, .alpha_component = 0},
    {.hash = 0x7363B362, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 9, .alpha_component = 1},
    {.hash = 0x757450FD, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0x76D2308E, .opacity = Opacity::DITHER},
    {.hash = 0x7A310992, .opacity = Opacity::DITHER},
    {.hash = 0x83783196, .opacity = Opacity::DITHER},
    {.hash = 0x86F98187, .opacity = Opacity::DITHER},
    {.hash = 0x88473ADB, .opacity = Opacity::DITHER},
    {.hash = 0x8B92FC8D, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 1},
    {.hash = 0x8BC68F9E, .opacity = Opacity::DITHER},
    {.hash = 0x8CA4E46D, .uv_index = 0, .alpha_slot = 1, .alpha_register = 8, .alpha_component = 2},
    {.hash = 0x8CC58B5E, .uv_index = 0, .uv_slot = 1, .uv_register = 3, .uv_components = 4, .alpha_slot = 1, .alpha_register = 16, .alpha_component = 3},
    {.hash = 0x9027DB17, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0x987A2EA4, .uv_index = 0, .alpha_slot = 1, .alpha_register = 12, .alpha_component = 3},
    {.hash = 0x98EC55A5, .opacity = Opacity::DITHER},
    {.hash = 0x9A5A1D71, .uv_index = 0, .alpha_slot = 1, .alpha_register = 8, .alpha_component = 2},
    {.hash = 0x9E6606BD, .uv_index = 0, .uv_slot = 2, .uv_register = 11, .uv_components = 10, .alpha_slot = 2, .alpha_register = 14, .alpha_component = 0},
    {.hash = 0xA506B05A, .uv_index = 0, .uv_slot = 2, .uv_register = 11, .uv_components = 10, .alpha_slot = 2, .alpha_register = 14, .alpha_component = 0},
    {.hash = 0xA5762F5F, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 8, .alpha_component = 0},
    {.hash = 0xB05C06FB, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 8, .alpha_component = 0},
    {.hash = 0xB0B93022, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0xB0BEAD2B, .opacity = Opacity::DITHER},
    {.hash = 0xB20B6C42, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0xB359D992, .opacity = Opacity::DITHER},
    {.hash = 0xB4D98995, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0xB69E1817, .opacity = Opacity::DITHER},
    {.hash = 0xB6B48B98, .uv_index = 0, .uv_slot = 2, .uv_register = 3, .uv_components = 4, .alpha_slot = 2, .alpha_register = 16, .alpha_component = 3},
    {.hash = 0xB6DF58CC, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 14, .alpha_component = 1},
    {.hash = 0xC3FCD603, .uv_index = 0, .uv_slot = 2, .uv_register = 1, .uv_components = 4, .alpha_slot = 2, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0xC93531B6, .opacity = Opacity::DITHER},
    {.hash = 0xCCCBE0DF, .uv_index = 0, .alpha_slot = 2, .alpha_register = 14, .alpha_component = 0},
    {.hash = 0xCE450C5F, .opacity = Opacity::DITHER},
    {.hash = 0xCEB16558, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 9, .alpha_component = 1},
    {.hash = 0xCFC69E83, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0xD47EC68C, .uv_index = 0, .uv_slot = 1, .uv_register = 1, .uv_components = 4, .alpha_slot = 1, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0xD6BA3544, .opacity = Opacity::DITHER},
    {.hash = 0xD802F711, .uv_index = 0, .uv_slot = 2, .uv_register = 3, .uv_components = 4, .alpha_slot = 2, .alpha_register = 13, .alpha_component = 0},
    {.hash = 0xD9ECC055, .opacity = Opacity::DITHER},
    {.hash = 0xDD01507D, .uv_index = 0, .alpha_slot = 1, .alpha_register = 12, .alpha_component = 3},
    {.hash = 0xDFBEDB20, .opacity = Opacity::DITHER},
    {.hash = 0xE20AC0C5, .opacity = Opacity::DITHER},
    {.hash = 0xE27035A0, .uv_index = 0, .alpha_slot = 2, .alpha_register = 12, .alpha_component = 3},
    {.hash = 0xE52450E7, .uv_index = 0, .uv_slot = 2, .uv_register = 12, .uv_components = 10, .alpha_slot = 2, .alpha_register = 15, .alpha_component = 1},
    {.hash = 0xE5F8BD78, .uv_index = 0, .uv_slot = 3, .uv_register = 1, .uv_components = 4, .alpha_slot = 3, .alpha_register = 9, .alpha_component = 2},
    {.hash = 0xE9EB4595, .opacity = Opacity::DITHER},
    {.hash = 0xF6CE23A1, .opacity = Opacity::DITHER},
    {.hash = 0xF9ACC492, .uv_index = 0, .uv_slot = 2, .uv_register = 8, .uv_components = 10, .alpha_slot = 2, .alpha_register = 11, .alpha_component = 1},
    {.hash = 0xFA3AAFCA, .uv_index = 0, .alpha_slot = 2, .alpha_register = 15, .alpha_component = 1},
    {.hash = 0xFAB2E75D, .uv_index = 0, .uv_slot = 2, .uv_register = 3, .uv_components = 4, .alpha_slot = 2, .alpha_register = 16, .alpha_component = 3},
    {.hash = 0xFBBAE1F9, .opacity = Opacity::DITHER},
}};

// Bandai's sun shadow mask (the HDR addon's sunshadow00): t0 scene depth, b0 ScreenToShadow (c28-c31), b1 View.
constexpr uint32_t SUN_SHADOW_SHADER = 0x2084C889;
// Where the HDR addon's sun shadow mask reads the visibility.
constexpr UINT VISIBILITY_SLOT = 40;
// The sun texture (0x7A1E0D12): toward the sun, the camera, TranslatedWorldToClip (4), PreViewTranslation, view size.
constexpr UINT SUN_TEXELS = 8;

// Ray-traced sun shadows (Experimental): the HDR addon's sun shadow mask keeps the darker of its shadow map and these
// (t40). Soft: the sun's disk angular radius (degrees; 0 hard): penumbras from each ray's blocker distance.
static std::atomic_bool enabled = false;
static std::atomic<float> sun_softness = 0.5f;
// Characters cast ray-traced sun shadows on the world (not on themselves: the game's shadow maps shade them).
static std::atomic_bool character_shadows = true;
// Ray-traced ambient occlusion (Experimental): rays around each pixel's normal, as far as the range (UE centimetres);
// the HDR addon's lighting pass multiplies it into its own (t41).
static std::atomic_bool ambient_occlusion = false;
static std::atomic<float> ambient_range = 200.f;
static std::atomic<uint32_t> ambient_rays = 2;
// Ray-traced bounce light (Experimental): rays around each pixel's normal find the nearest surface within the range
// (UE centimetres), whose light is last frame's scene colour there (CaptureSceneColor); accumulated over frames and
// filtered, the HDR addon's lighting pass adds it times the base colour (t42).
static std::atomic_bool bounce_light = false;
static std::atomic<float> bounce_range = 1000.f;
static std::atomic<uint32_t> bounce_rays = 1;
static std::atomic<float> bounce_strength = 1.f;
// Ray-traced reflections (Experimental): samples of each pixel's own GGX lobe at the lighting pass (a mirror ray for
// smooth surfaces); the HDR addon's lighting pass uses their light in place of the game's screen reflection (t43).
static std::atomic_bool reflections = false;
// Ray-traced specular occlusion (Experimental): the share of a pixel's lobe open for the reflection range; the HDR
// addon's lighting pass dims the game's cubemap reflection by it (t44).
static std::atomic_bool specular_occlusion = false;
// Path tracing (Experimental): the bounce light as paths of up to `path_bounces` surfaces past the first,
// `path_samples` paths a pixel (also the reflections' samples), shading what the screen never saw with the game's own
// lighting learned from the screen (rt_path.hlsli).
static std::atomic_bool path_tracing = false;
static std::atomic<uint32_t> path_bounces = 2;
static std::atomic<uint32_t> path_samples = 1;
// The add-on's Agility SDK folder (D3D12Core.dll), set at load; empty or missing: Windows' D3D12 (DXR 1.1).
static std::string agility_folder;

// Ray Reconstruction denoised the last frame (addon.cpp): the bounce and reflection filters pass their accumulation
// through and the reflections keep no history (its noise is its to remove).
static std::atomic<bool> reconstructing = false;

// Any ray-traced effect on: the scene is captured and traced.
inline bool Active() {
  return enabled.load(std::memory_order_relaxed) || ambient_occlusion.load(std::memory_order_relaxed)
         || bounce_light.load(std::memory_order_relaxed) || reflections.load(std::memory_order_relaxed)
         || specular_occlusion.load(std::memory_order_relaxed) || path_tracing.load(std::memory_order_relaxed);
}

// The effects that read last frame's scene (the bounce light, the reflections, the radiance cache feeding them).
inline bool UsesHistory() {
  return bounce_light.load(std::memory_order_relaxed) || reflections.load(std::memory_order_relaxed)
         || path_tracing.load(std::memory_order_relaxed);
}

// The effects traced at the lighting pass (from the G-buffer).
inline bool UsesLightingPass() {
  return reflections.load(std::memory_order_relaxed) || specular_occlusion.load(std::memory_order_relaxed)
         || path_tracing.load(std::memory_order_relaxed);
}

namespace internal {

// The shared vertex texture: rows of 4096 float4 (64 KiB, a valid D3D12 copy pitch), 1024 rows: 4M vertices a frame.
constexpr UINT TRANSFER_WIDTH = 4096;
constexpr UINT TRANSFER_HEIGHT = 1024;
constexpr UINT MAX_VERTICES = TRANSFER_WIDTH * TRANSFER_HEIGHT;
constexpr UINT VERTEX_BYTES = 16;
// The stream output: 16 bytes a vertex (clip position), 24 for alpha-tested draws (and their UV).
constexpr UINT CAPTURED_BYTES = 80u << 20;
constexpr UINT MASKED_STRIDE = 24;
// New meshes a frame captures at most (and characters' draws); the rest follow in the next frames.
constexpr size_t CAPTURES_PER_FRAME = 256;
constexpr UINT MAX_INSTANCES = 16384;
constexpr uint32_t FRAMES_IN_FLIGHT = 3;
// Meshes the prepass drew within this many frames cast shadows (casters behind the camera keep theirs a while; the
// landscape only in frames that draw it, its older LODs would shadow the current one). Unseen longer: BLAS released.
constexpr uint64_t RECENT_FRAMES = 300;
constexpr uint64_t RELEASE_FRAMES = 3600;
constexpr DWORD FENCE_TIMEOUT = 10'000;
// Dynamic (skinned) vertices a frame at most, so new static meshes keep streaming in beside them.
constexpr UINT MAX_DYNAMIC_VERTICES = MAX_VERTICES * 3 / 4;
// A character's BLAS is refitted to its pose each frame and rebuilt every this many (refits loosen it).
constexpr uint32_t CHARACTER_REBUILD_FRAMES = 30;
constexpr uint64_t CHARACTER_RELEASE_FRAMES = 120;
// The radiance cache (rt_cache.hlsli): entries; keys, radiance (rgb, frame), sums (r, g, b, count) and base colours.
constexpr UINT CACHE_ENTRIES = 1u << 20;
// Records (InstanceID): static meshes first, then each frame's characters in turn.
constexpr UINT STATIC_RECORDS = MAX_INSTANCES;
constexpr UINT RECORD_COUNT = STATIC_RECORDS + FRAMES_IN_FLIGHT * CAPTURES_PER_FRAME;
// The pools D3D12 keeps: a normal per triangle, a UV per alpha-tested vertex; static meshes' first, then each frame's
// characters in turn.
constexpr UINT STATIC_TRIANGLES = 8u << 20;
constexpr UINT DYNAMIC_TRIANGLES = MAX_DYNAMIC_VERTICES / 3;
constexpr UINT NORMAL_POOL = STATIC_TRIANGLES + FRAMES_IN_FLIGHT * DYNAMIC_TRIANGLES;
constexpr UINT STATIC_UVS = 8u << 20;
constexpr UINT DYNAMIC_UVS = 1u << 20;
constexpr UINT UV_POOL = STATIC_UVS + FRAMES_IN_FLIGHT * DYNAMIC_UVS;
constexpr uint32_t RECORD_MASKED = 1;  // rt_scene.hlsli's flags
// The opacity mask atlas: slices of ATLAS_SIZE square (R8), one per alpha-tested material texture.
constexpr UINT ATLAS_SIZE = 512;
constexpr UINT ATLAS_SLICES = 256;
// Opacity micromaps: a mesh's at most (its subdivision level lowered to fit), all new meshes' a frame (others wait).
constexpr uint64_t MICROMAP_MESH_BYTES = 8u << 20;
constexpr uint64_t MICROMAP_FRAME_BYTES = 64u << 20;
// Specular occlusion: how far a reflection's lobe must be open (centimetres).
constexpr float REFLECTION_RANGE = 1000.f;
// 0x7A1E0D11's capture flags.
constexpr uint32_t CAPTURE_MASKED = 1;
constexpr uint32_t CAPTURE_UV_SCALE = 2;
constexpr uint32_t CAPTURE_ALPHA_SCALE = 64;
// The D3D12 passes' descriptor table: SRVs t1-t15, UAVs u0-u11, u16-u20, u22-u25 (u12, u13, u21 and t16 are root
// descriptors: the opacity micromaps' per-frame buffers and the capture table).
enum Descriptor : UINT {
  POSITIONS,
  SUN,
  PREVIOUS_COLOR,
  PREVIOUS_POSITIONS,
  PREVIOUS_SUN,
  NORMALS,
  RECORDS,
  UVS,
  TRIANGLE_NORMALS,
  ALPHA_ATLAS,
  VERTICES,
  CAPTURE_PARAMS,
  MATERIAL,
  BASE_COLOR,
  SUN_VISIBILITY,
  VISIBILITY,
  AMBIENT,
  ACCUMULATED,
  HISTORY,
  BOUNCE_LIGHT,
  CACHE_KEYS,
  CACHE_RADIANCE,
  CACHE_SUMS,
  REFLECTION,
  RECORDS_WRITTEN,
  UVS_WRITTEN,
  TRIANGLE_NORMALS_WRITTEN,
  SPECULAR_ACCUMULATED,
  SPECULAR_HISTORY,
  SPECULAR_OCCLUSION,
  CACHE_ALBEDO,
  LIGHT_FIT,
  LIGHT_KEPT,
  SPECULAR_META,
  SPECULAR_META_HISTORY,
  SUN_RAYS,
  DESCRIPTOR_COUNT,
};
constexpr UINT SRV_COUNT = VISIBILITY;

// Root parameters.
enum Root : UINT {
  ROOT_SCENE,
  ROOT_TABLE,
  ROOT_SETTINGS,
  ROOT_DISPATCH,
  ROOT_CAPTURE_TABLE,
  ROOT_MICROMAP_DATA,
  ROOT_MICROMAP_INDICES,
  ROOT_MICROMAP_DESCS,
  ROOT_COUNT,
};

struct Capture {
  uint64_t key = 0;
  uint32_t first_vertex = 0;  // in this frame's vertices
  uint32_t vertex_count = 0;
  uint32_t byte_offset = 0;  // in the stream output
  uint32_t stride = 0;
  ComPtr<ID3D11Buffer> view;  // the draw's View uniform buffer: converts its clip positions
  uint32_t flags = 0;         // CAPTURE_* for 0x7A1E0D11
  uint32_t slice = 0;         // its opacity mask's atlas slice (alpha-tested)
  bool landscape = false;
  bool dynamic = false;
  bool masked = false;
};

struct Mesh {
  ComPtr<ID3D12Resource> blas;
  ComPtr<ID3D12Resource> micromaps;         // its opacity micromap array (DXR 1.2), kept while the BLAS lives
  ComPtr<ID3D12Resource> micromap_indices;  // a micromap or special index per triangle
  uint64_t drawn_frame = 0;
  uint32_t triangles = 0;
  uint32_t record = 0;
  uint32_t normal_offset = 0;
  uint32_t uv_offset = UINT32_MAX;  // none: opaque
  uint32_t slice = 0;
  bool landscape = false;
  bool compacted = false;
};

// A character's BLAS (by its geometry and its turn among this frame's draws of it), refitted each frame.
struct Character {
  ComPtr<ID3D12Resource> blas;
  uint64_t bytes = 0;
  uint32_t vertex_count = 0;
  uint32_t refits = 0;  // since its last full build
  uint64_t seen_frame = 0;
  bool masked = false;
};

// A frame's new static BLASes' compacted sizes (written by the builds, copied for the CPU) and their meshes.
struct Compaction {
  ComPtr<ID3D12Resource> sizes;     // UNORDERED_ACCESS, CAPTURES_PER_FRAME x 8 bytes
  ComPtr<ID3D12Resource> readback;  // READBACK heap
  std::vector<uint64_t> keys;       // by size slot (none: nothing pending)
  uint64_t fence_value = 0;
};

// A texture made in D3D11 with an NT handle and opened in D3D12.
struct SharedTexture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ComPtr<ID3D12Resource> resource;
};

struct Release {
  uint64_t fence_value = 0;
  ComPtr<ID3D12Resource> resource;
};

// A pool's free ranges (offset -> count), first fit.
struct Ranges {
  std::map<uint32_t, uint32_t> free;

  bool Take(uint32_t count, uint32_t* offset) {
    for (auto range = free.begin(); range != free.end(); ++range) {
      if (range->second < count) continue;
      *offset = range->first;
      const uint32_t left = range->second - count;
      free.erase(range);
      if (left != 0) free.emplace(*offset + count, left);
      return true;
    }
    return false;
  }

  void Give(uint32_t offset, uint32_t count) {
    auto next = free.lower_bound(offset);
    if (next != free.end() && offset + count == next->first) {
      count += next->second;
      next = free.erase(next);
    }
    if (next != free.begin()) {
      auto previous = std::prev(next);
      if (previous->first + previous->second == offset) {
        previous->second += count;
        return;
      }
    }
    free.emplace(offset, count);
  }
};

// Pool ranges and records of released meshes, given back once the GPU is past the frames that read them.
struct PendingFree {
  uint64_t fence_value = 0;
  uint32_t record = 0;
  uint32_t normal_offset = 0;
  uint32_t triangles = 0;
  uint32_t uv_offset = UINT32_MAX;
};

// A new alpha-tested static mesh's opacity micromaps this frame.
struct MicromapBake {
  uint64_t key = 0;
  uint32_t level = 0;
  uint64_t data_offset = 0;  // in this frame's data buffer
  uint64_t desc_offset = 0;  // in this frame's desc buffer
};

// Everything made for the GPU, and the scene; reset as a whole when the device goes or creation fails.
struct Gpu {
  ComPtr<ID3D11Device5> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11GeometryShader> capture_shader;  // opaque draws: SV_Position only (0x7A1E0D10)
  // Alpha-tested draws: a stream output made from each vertex shader (by its hash and the UV's TEXCOORD, or none).
  std::unordered_map<uint64_t, ComPtr<ID3D11GeometryShader>> masked_capture_shaders;
  ComPtr<ID3D11ComputeShader> vertices_shader;
  ComPtr<ID3D11ComputeShader> prepare_shader;
  ComPtr<ID3D11ComputeShader> atlas_shader;
  ComPtr<ID3D11Buffer> captured;  // stream output (raw)
  ComPtr<ID3D11ShaderResourceView> captured_srv;
  ComPtr<ID3D11Buffer> range_constants;
  ComPtr<ID3D11Buffer> capture_table;     // 0x7A1E0D11's b2
  ComPtr<ID3D11Buffer> material_rows;     // 0x7A1E0D11's b3: two of each capture's material registers
  ComPtr<ID3D11Buffer> atlas_constants;
  ComPtr<ID3D11SamplerState> linear_wrap;
  ComPtr<ID3D11Fence> fence11;
  SharedTexture vertices;
  SharedTexture capture_params;
  SharedTexture sun;
  SharedTexture positions;
  SharedTexture visibility;
  SharedTexture ambient;
  // The opacity mask atlas (a texture array; made at the first alpha-tested capture).
  ComPtr<ID3D11Texture2D> atlas;
  ComPtr<ID3D11UnorderedAccessView> atlas_uav;
  ComPtr<ID3D12Resource> atlas12;
  // Bounce light and reflections: last frame's camera, positions and scene colour (made when one is first on); the
  // bounce light; the reflections, specular occlusion and the G-buffer they start from.
  SharedTexture previous_sun;
  SharedTexture previous_positions;
  SharedTexture previous_color;
  SharedTexture bounce_light;
  SharedTexture reflection;
  SharedTexture specular_occlusion_texture;
  std::array<SharedTexture, 3> gbuffer;  // A normals, B metallic / specular / roughness, C base colour
  ComPtr<ID3D11Texture2D> sun_readback;
  UINT width = 0;
  UINT height = 0;

  ComPtr<ID3D12Device> device12_wrapper;  // keeps ReShade's proxy (when it wraps) alive
  ComPtr<ID3D12Device5> device12;
  bool dxr12 = false;  // DXR 1.2 and shader model 6.9: opacity micromaps, shader execution reordering
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence12;
  HANDLE fence_event = nullptr;
  uint64_t fence_value = 0;
  std::array<ComPtr<ID3D12CommandAllocator>, FRAMES_IN_FLIGHT> allocators;
  std::array<uint64_t, FRAMES_IN_FLIGHT> allocator_values = {};
  std::array<ComPtr<ID3D12Resource>, FRAMES_IN_FLIGHT> instance_uploads;
  std::array<D3D12_RAYTRACING_INSTANCE_DESC*, FRAMES_IN_FLIGHT> instance_data = {};
  std::array<ComPtr<ID3D12Resource>, FRAMES_IN_FLIGHT> table_uploads;  // the scene data pass's capture table
  std::array<uint32_t*, FRAMES_IN_FLIGHT> table_data = {};
  ComPtr<ID3D12GraphicsCommandList4> list;
  uint64_t submissions = 0;
  ComPtr<ID3D12Resource> vertex_buffer;
  ComPtr<ID3D12Resource> scratch;
  uint64_t scratch_bytes = 0;
  ComPtr<ID3D12Resource> tlas;
  ComPtr<ID3D12Resource> tlas_scratch;
  ComPtr<ID3D12Resource> records;
  ComPtr<ID3D12Resource> uvs;
  ComPtr<ID3D12Resource> triangle_normals;
  ComPtr<ID3D12Resource> micromap_data;  // this frame's micromap inputs (grown when needed)
  ComPtr<ID3D12Resource> micromap_descs;
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline;               // 0x7A1E0D13 (0x7A1E0D33 with DXR 1.2)
  ComPtr<ID3D12PipelineState> filter_pipeline;        // 0x7A1E0D14
  ComPtr<ID3D12PipelineState> cache_fill_pipeline;    // 0x7A1E0D15
  ComPtr<ID3D12PipelineState> cache_resolve_pipeline;  // 0x7A1E0D16
  ComPtr<ID3D12PipelineState> scene_data_pipeline;    // 0x7A1E0D19
  ComPtr<ID3D12PipelineState> micromap_pipeline;      // 0x7A1E0D1A (DXR 1.2)
  ComPtr<ID3D12PipelineState> path_pipeline;          // 0x7A1E0D1B (without DXR 1.2)
  ComPtr<ID3D12PipelineState> learn_pipeline;         // 0x7A1E0D1D
  ComPtr<ID3D12PipelineState> specular_pipeline;      // 0x7A1E0D1E
  ComPtr<ID3D12PipelineState> sun_pipeline;           // 0x7A1E0D1F
  ComPtr<ID3D12StateObject> path_state_object;        // 0x7A1E0D1C (DXR 1.2)
  ComPtr<ID3D12Resource> path_shader_table;           // its ray generation record
  ComPtr<ID3D12Resource> accumulated;                 // bounce light, D3D12 only (always in the UAV state)
  ComPtr<ID3D12Resource> history;
  ComPtr<ID3D12Resource> sun_rays;  // soft shadows' rays (UAV state)
  std::array<ComPtr<ID3D12Resource>, 4> specular;  // accumulated, history, meta, meta history (UAV state)
  // The radiance cache and the learned lighting (D3D12 only, always in the UAV state; zeroed when made).
  ComPtr<ID3D12Resource> cache_keys;
  ComPtr<ID3D12Resource> cache_radiance;
  ComPtr<ID3D12Resource> cache_sums;
  ComPtr<ID3D12Resource> cache_albedo;
  ComPtr<ID3D12Resource> light_fit;
  ComPtr<ID3D12Resource> light_kept;
  ComPtr<ID3D12DescriptorHeap> descriptors;
  UINT descriptor_size = 0;
  std::vector<Release> releases;
  std::unordered_map<uint64_t, Mesh> meshes;
  std::unordered_map<uint64_t, Character> characters;
  // By frame (FRAMES_IN_FLIGHT in turn): the fence value of the frame's last work, the compactions.
  std::array<uint64_t, FRAMES_IN_FLIGHT> frame_fences = {};
  std::array<Compaction, FRAMES_IN_FLIGHT> compactions;
  // Static pools and records.
  Ranges free_triangles;
  Ranges free_uvs;
  std::vector<uint32_t> free_records;
  std::vector<PendingFree> pending_frees;
  // The atlas's textures (kept alive while they have a slice), its free slices and when each was last used.
  std::unordered_map<ID3D11Resource*, std::pair<ComPtr<ID3D11Resource>, uint32_t>> atlas_slices;
  std::vector<uint32_t> free_slices;
  std::array<uint64_t, ATLAS_SLICES> slice_used = {};
};

struct State {
  std::shared_mutex mutex;
  bool failed = false;
  std::string status = "Off.";
  std::string logged_status = "Off.";  // the log keeps each change (reports without the overlay)
  Gpu gpu;
  std::vector<Capture> captures;
  uint32_t captured_vertices = 0;
  uint32_t captured_bytes = 0;
  uint32_t dynamic_vertices = 0;  // of them, the characters'
  std::unordered_map<uint64_t, uint32_t> occurrences;  // this frame's draws of each character geometry
  uint64_t frame = 0;
  uint64_t active_frame = 0;  // the last frame an effect was on
  uint64_t traced_frame = UINT64_MAX;     // the last frame the scene was traced (the ambient occlusion is this frame's)
  uint64_t bounce_frame = UINT64_MAX;     // the last frame the bounce light was traced (its history)
  uint64_t color_frame = UINT64_MAX;      // the frame the previous colour was captured in
  uint64_t lighting_frame = UINT64_MAX;   // the last frame the lighting pass traced
  uint64_t specular_frame = UINT64_MAX;   // the last frame the reflections / specular occlusion were accumulated
  uint64_t cache_fills = 0;               // frames the radiance cache was filled
  ID3D11Resource* scene_depth = nullptr;  // compared only: the sun shadow mask's depth (last frame)
  std::array<uint32_t, 16> settings = {};  // the frame's passes'
  uint32_t lighting_outputs = 0;  // this frame's lighting pass results (LightingResult)
  bool trace_requested = false;
  bool tracing = false;
  bool sun_read = false;
  std::string trace;
};
static State state;

inline ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t bytes, D3D12_HEAP_TYPE heap_type,
                                           D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial_state) {
  const D3D12_HEAP_PROPERTIES heap = {.Type = heap_type};
  const D3D12_RESOURCE_DESC desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Width = bytes,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .SampleDesc = {.Count = 1},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
      .Flags = flags,
  };
  ComPtr<ID3D12Resource> buffer;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initial_state, nullptr,
                                             IID_PPV_ARGS(buffer.GetAddressOf())))) {
    return nullptr;
  }
  return buffer;
}

// A D3D12-only screen-size texture in the UAV state (histories, accumulations).
inline ComPtr<ID3D12Resource> CreateTexture(DXGI_FORMAT format) {
  const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
  const D3D12_RESOURCE_DESC desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = state.gpu.width,
      .Height = state.gpu.height,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = format,
      .SampleDesc = {.Count = 1},
      .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
  };
  ComPtr<ID3D12Resource> texture;
  if (FAILED(state.gpu.device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                         nullptr, IID_PPV_ARGS(texture.GetAddressOf())))) {
    return nullptr;
  }
  return texture;
}

inline D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  return {
      .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
      .Transition = {
          .pResource = resource,
          .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          .StateBefore = before,
          .StateAfter = after,
      },
  };
}

// All UAV writes (BLAS / TLAS builds, scratch) done before the next command reads them.
inline D3D12_RESOURCE_BARRIER UavBarrier() {
  return {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
}

inline bool CreateShared(DXGI_FORMAT format, UINT width, UINT height, SharedTexture* shared) {
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = width,
      .Height = height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = format,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
      .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
  };
  Gpu& gpu = state.gpu;
  *shared = {};
  if (FAILED(gpu.device->CreateTexture2D(&desc, nullptr, shared->texture.GetAddressOf()))
      || FAILED(gpu.device->CreateShaderResourceView(shared->texture.Get(), nullptr, shared->srv.GetAddressOf()))
      || FAILED(gpu.device->CreateUnorderedAccessView(shared->texture.Get(), nullptr, shared->uav.GetAddressOf()))
      || FAILED(d3d12_device::Open(gpu.device12.Get(), shared->texture.Get(), &shared->resource))) {
    *shared = {};
    return false;
  }
  return true;
}

// The CPU waits until D3D12 passed `value` (rarely: the D3D11 context waits for every frame's work on the GPU).
inline bool WaitFor(uint64_t value) {
  Gpu& gpu = state.gpu;
  if (gpu.fence12->GetCompletedValue() >= value) return true;
  if (FAILED(gpu.fence12->SetEventOnCompletion(value, gpu.fence_event))) return false;
  return WaitForSingleObject(gpu.fence_event, FENCE_TIMEOUT) == WAIT_OBJECT_0;
}

inline D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAt(Descriptor index) {
  D3D12_CPU_DESCRIPTOR_HANDLE handle = state.gpu.descriptors->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += index * state.gpu.descriptor_size;
  return handle;
}

inline D3D12_SHADER_RESOURCE_VIEW_DESC TextureView(DXGI_FORMAT format) {
  return {
      .Format = format,
      .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
      .Texture2D = {.MipLevels = 1},
  };
}

inline D3D12_SHADER_RESOURCE_VIEW_DESC BufferSrv(DXGI_FORMAT format, UINT elements) {
  return {
      .Format = format,
      .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
      .Buffer = {.NumElements = elements},
  };
}

inline D3D12_UNORDERED_ACCESS_VIEW_DESC BufferView(DXGI_FORMAT format, UINT elements) {
  return {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER, .Buffer = {.NumElements = elements}};
}

inline D3D12_UNORDERED_ACCESS_VIEW_DESC TextureUav(DXGI_FORMAT format) {
  return {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
}

// The descriptors of what the effects make when first on (the shaders read them only while their effect runs).
inline void NullDescriptors() {
  Gpu& gpu = state.gpu;
  const D3D12_SHADER_RESOURCE_VIEW_DESC color_view = TextureView(DXGI_FORMAT_R16G16B16A16_FLOAT);
  for (const Descriptor index : {PREVIOUS_COLOR, PREVIOUS_POSITIONS, NORMALS, MATERIAL, BASE_COLOR}) {
    gpu.device12->CreateShaderResourceView(nullptr, &color_view, DescriptorAt(index));
  }
  if (!gpu.atlas12) {
    const D3D12_SHADER_RESOURCE_VIEW_DESC atlas_view = {
        .Format = DXGI_FORMAT_R8_UNORM,
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2DArray = {.MipLevels = 1, .ArraySize = 1},
    };
    gpu.device12->CreateShaderResourceView(nullptr, &atlas_view, DescriptorAt(ALPHA_ATLAS));
  }
  const D3D12_UNORDERED_ACCESS_VIEW_DESC texture_view = TextureUav(DXGI_FORMAT_R16G16B16A16_FLOAT);
  for (const Descriptor index : {ACCUMULATED, HISTORY, BOUNCE_LIGHT, REFLECTION, SPECULAR_ACCUMULATED, SPECULAR_HISTORY,
                                 SPECULAR_OCCLUSION, SPECULAR_META, SPECULAR_META_HISTORY, SUN_RAYS}) {
    gpu.device12->CreateUnorderedAccessView(nullptr, nullptr, &texture_view, DescriptorAt(index));
  }
  const D3D12_UNORDERED_ACCESS_VIEW_DESC uint_view = BufferView(DXGI_FORMAT_R32_UINT, 1);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC float_view = BufferView(DXGI_FORMAT_R32G32B32A32_FLOAT, 1);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC pair_view = BufferView(DXGI_FORMAT_R32G32_UINT, 1);
  gpu.device12->CreateUnorderedAccessView(nullptr, nullptr, &uint_view, DescriptorAt(CACHE_KEYS));
  gpu.device12->CreateUnorderedAccessView(nullptr, nullptr, &float_view, DescriptorAt(CACHE_RADIANCE));
  gpu.device12->CreateUnorderedAccessView(nullptr, nullptr, &uint_view, DescriptorAt(CACHE_SUMS));
  gpu.device12->CreateUnorderedAccessView(nullptr, nullptr, &pair_view, DescriptorAt(CACHE_ALBEDO));
  gpu.device12->CreateUnorderedAccessView(nullptr, nullptr, &uint_view, DescriptorAt(LIGHT_FIT));
  gpu.device12->CreateUnorderedAccessView(nullptr, nullptr, &float_view, DescriptorAt(LIGHT_KEPT));
}

// The sun visibility as the lighting pass's learning reads it (t15).
inline void VisibilityDescriptor() {
  const D3D12_SHADER_RESOURCE_VIEW_DESC view = TextureView(DXGI_FORMAT_R8_UNORM);
  state.gpu.device12->CreateShaderResourceView(state.gpu.visibility.resource.Get(), &view, DescriptorAt(SUN_VISIBILITY));
}

// Last frame's positions (screen size) and the radiance cache with the learned lighting (kept across sizes) for the
// bounce light, reflections and path tracing (D3D12 idle: the descriptors change).
inline bool CreateHistory() {
  Gpu& gpu = state.gpu;
  if (!CreateShared(DXGI_FORMAT_R32G32B32A32_FLOAT, gpu.width, gpu.height, &gpu.previous_positions)) return false;
  const D3D12_SHADER_RESOURCE_VIEW_DESC positions_view = TextureView(DXGI_FORMAT_R32G32B32A32_FLOAT);
  gpu.device12->CreateShaderResourceView(gpu.previous_positions.resource.Get(), &positions_view, DescriptorAt(PREVIOUS_POSITIONS));
  if (!gpu.cache_keys) {
    const auto buffer = [&](uint64_t bytes) {
      return CreateBuffer(gpu.device12.Get(), bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    };
    gpu.cache_keys = buffer(uint64_t{CACHE_ENTRIES} * 4);
    gpu.cache_radiance = buffer(uint64_t{CACHE_ENTRIES} * 16);
    gpu.cache_sums = buffer(uint64_t{CACHE_ENTRIES} * 16);
    gpu.cache_albedo = buffer(uint64_t{CACHE_ENTRIES} * 8);
    gpu.light_fit = buffer(16 * 4);
    gpu.light_kept = buffer(2 * 16);
    state.cache_fills = 0;
    if (!gpu.cache_keys || !gpu.cache_radiance || !gpu.cache_sums || !gpu.cache_albedo || !gpu.light_fit || !gpu.light_kept) {
      gpu.cache_keys.Reset();
      gpu.cache_radiance.Reset();
      gpu.cache_sums.Reset();
      gpu.cache_albedo.Reset();
      gpu.light_fit.Reset();
      gpu.light_kept.Reset();
      gpu.previous_positions = {};
      return false;
    }
  }
  const D3D12_UNORDERED_ACCESS_VIEW_DESC keys_view = BufferView(DXGI_FORMAT_R32_UINT, CACHE_ENTRIES);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC radiance_view = BufferView(DXGI_FORMAT_R32G32B32A32_FLOAT, CACHE_ENTRIES);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC sums_view = BufferView(DXGI_FORMAT_R32_UINT, CACHE_ENTRIES * 4);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC albedo_view = BufferView(DXGI_FORMAT_R32G32_UINT, CACHE_ENTRIES);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC fit_view = BufferView(DXGI_FORMAT_R32_UINT, 16);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC kept_view = BufferView(DXGI_FORMAT_R32G32B32A32_FLOAT, 2);
  gpu.device12->CreateUnorderedAccessView(gpu.cache_keys.Get(), nullptr, &keys_view, DescriptorAt(CACHE_KEYS));
  gpu.device12->CreateUnorderedAccessView(gpu.cache_radiance.Get(), nullptr, &radiance_view, DescriptorAt(CACHE_RADIANCE));
  gpu.device12->CreateUnorderedAccessView(gpu.cache_sums.Get(), nullptr, &sums_view, DescriptorAt(CACHE_SUMS));
  gpu.device12->CreateUnorderedAccessView(gpu.cache_albedo.Get(), nullptr, &albedo_view, DescriptorAt(CACHE_ALBEDO));
  gpu.device12->CreateUnorderedAccessView(gpu.light_fit.Get(), nullptr, &fit_view, DescriptorAt(LIGHT_FIT));
  gpu.device12->CreateUnorderedAccessView(gpu.light_kept.Get(), nullptr, &kept_view, DescriptorAt(LIGHT_KEPT));
  return true;
}

// The bounce light's screen-size textures (D3D12 idle: the descriptors change).
inline bool CreateBounceTextures() {
  Gpu& gpu = state.gpu;
  gpu.accumulated = CreateTexture(DXGI_FORMAT_R16G16B16A16_FLOAT);
  gpu.history = CreateTexture(DXGI_FORMAT_R16G16B16A16_FLOAT);
  if (!gpu.accumulated || !gpu.history || !CreateShared(DXGI_FORMAT_R16G16B16A16_FLOAT, gpu.width, gpu.height, &gpu.bounce_light)) {
    gpu.accumulated.Reset();
    gpu.history.Reset();
    gpu.bounce_light = {};
    return false;
  }
  const D3D12_UNORDERED_ACCESS_VIEW_DESC bounce_view = TextureUav(DXGI_FORMAT_R16G16B16A16_FLOAT);
  gpu.device12->CreateUnorderedAccessView(gpu.accumulated.Get(), nullptr, &bounce_view, DescriptorAt(ACCUMULATED));
  gpu.device12->CreateUnorderedAccessView(gpu.history.Get(), nullptr, &bounce_view, DescriptorAt(HISTORY));
  gpu.device12->CreateUnorderedAccessView(gpu.bounce_light.resource.Get(), nullptr, &bounce_view, DescriptorAt(BOUNCE_LIGHT));
  state.bounce_frame = UINT64_MAX;
  return true;
}

// Soft shadows' rays: lit, blocker distance (D3D12 idle: the descriptor changes).
inline bool CreateSunTextures() {
  Gpu& gpu = state.gpu;
  gpu.sun_rays = CreateTexture(DXGI_FORMAT_R16G16_FLOAT);
  if (!gpu.sun_rays) return false;
  const D3D12_UNORDERED_ACCESS_VIEW_DESC view = TextureUav(DXGI_FORMAT_R16G16_FLOAT);
  gpu.device12->CreateUnorderedAccessView(gpu.sun_rays.Get(), nullptr, &view, DescriptorAt(SUN_RAYS));
  return true;
}

// The reflections' and specular occlusion's textures (D3D12 idle: the descriptors change).
inline bool CreateSpecularTextures() {
  Gpu& gpu = state.gpu;
  constexpr std::array<DXGI_FORMAT, 4> FORMATS = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                  DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT};
  constexpr std::array<Descriptor, 4> DESCRIPTORS = {SPECULAR_ACCUMULATED, SPECULAR_HISTORY, SPECULAR_META, SPECULAR_META_HISTORY};
  for (size_t index = 0; index < FORMATS.size(); ++index) {
    gpu.specular[index] = CreateTexture(FORMATS[index]);
    if (!gpu.specular[index]) {
      gpu.specular = {};
      return false;
    }
    const D3D12_UNORDERED_ACCESS_VIEW_DESC view = TextureUav(FORMATS[index]);
    gpu.device12->CreateUnorderedAccessView(gpu.specular[index].Get(), nullptr, &view, DescriptorAt(DESCRIPTORS[index]));
  }
  if (!CreateShared(DXGI_FORMAT_R16G16B16A16_FLOAT, gpu.width, gpu.height, &gpu.reflection)
      || !CreateShared(DXGI_FORMAT_R8_UNORM, gpu.width, gpu.height, &gpu.specular_occlusion_texture)) {
    gpu.specular = {};
    gpu.reflection = {};
    gpu.specular_occlusion_texture = {};
    return false;
  }
  const D3D12_UNORDERED_ACCESS_VIEW_DESC reflection_view = TextureUav(DXGI_FORMAT_R16G16B16A16_FLOAT);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC occlusion_view = TextureUav(DXGI_FORMAT_R8_UNORM);
  gpu.device12->CreateUnorderedAccessView(gpu.reflection.resource.Get(), nullptr, &reflection_view, DescriptorAt(REFLECTION));
  gpu.device12->CreateUnorderedAccessView(gpu.specular_occlusion_texture.resource.Get(), nullptr, &occlusion_view,
                                          DescriptorAt(SPECULAR_OCCLUSION));
  state.specular_frame = UINT64_MAX;
  return true;
}

// The opacity mask atlas, shared both ways (D3D12 idle: the descriptor changes).
inline bool CreateAtlas() {
  Gpu& gpu = state.gpu;
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = ATLAS_SIZE,
      .Height = ATLAS_SIZE,
      .MipLevels = 1,
      .ArraySize = ATLAS_SLICES,
      .Format = DXGI_FORMAT_R8_UNORM,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
      .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
  };
  if (FAILED(gpu.device->CreateTexture2D(&desc, nullptr, gpu.atlas.GetAddressOf()))
      || FAILED(gpu.device->CreateUnorderedAccessView(gpu.atlas.Get(), nullptr, gpu.atlas_uav.GetAddressOf()))
      || FAILED(d3d12_device::Open(gpu.device12.Get(), gpu.atlas.Get(), &gpu.atlas12))) {
    gpu.atlas.Reset();
    gpu.atlas_uav.Reset();
    gpu.atlas12.Reset();
    return false;
  }
  const D3D12_SHADER_RESOURCE_VIEW_DESC view = {
      .Format = DXGI_FORMAT_R8_UNORM,
      .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
      .Texture2DArray = {.MipLevels = 1, .ArraySize = ATLAS_SLICES},
  };
  gpu.device12->CreateShaderResourceView(gpu.atlas12.Get(), &view, DescriptorAt(ALPHA_ATLAS));
  gpu.free_slices.clear();
  for (uint32_t slice = ATLAS_SLICES; slice > 0; --slice) {
    gpu.free_slices.push_back(slice - 1);
  }
  return true;
}

// The D3D12 device, BLASes, caches and shared textures freed (the mutex held); made again when needed.
inline void ReleaseGpu() {
  if (state.gpu.fence12) WaitFor(state.gpu.fence_value);
  if (state.gpu.fence_event != nullptr) CloseHandle(state.gpu.fence_event);
  state.gpu = {};
  state.captures.clear();
  state.captured_vertices = 0;
  state.captured_bytes = 0;
  state.dynamic_vertices = 0;
  state.occurrences.clear();
  state.scene_depth = nullptr;
}

inline bool Fail(const std::string& reason) {
  state.failed = true;
  state.status = "Failed: " + reason;
  return false;
}

// The D3D11 and D3D12 objects, made on first use (false and the status say why when the GPU or driver cannot).
inline bool Initialize(ID3D11Device* game_device) {
  Gpu& gpu = state.gpu;
  if (state.failed) return false;
  if (gpu.device) return true;
  ComPtr<ID3D11Device5> device;
  ComPtr<ID3D11Device1> device1;
  if (FAILED(game_device->QueryInterface(IID_PPV_ARGS(device.GetAddressOf()))) || FAILED(device.As(&device1))) {
    return Fail("needs Windows 10 or newer (D3D11.4 fences).");
  }
  // d3d12.dll is loaded only when ray tracing is turned on; the Agility SDK when it is next to the add-on.
  d3d12_device::Device d3d12;
  std::string agility_reason = "no Agility SDK next to the add-on";
  if (!agility_folder.empty() && std::filesystem::exists(std::filesystem::path(agility_folder) / "D3D12Core.dll")) {
    agility_reason = d3d12_device::CreateAgility(device.Get(), agility_folder, &d3d12);
  }
  const bool agility = agility_reason.empty();
  if (!agility) {
    d3d12 = {};
    if (const std::string reason = d3d12_device::Create(device.Get(), &d3d12); !reason.empty()) return Fail(reason);
  }
  ComPtr<ID3D12Device5> device12;
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  if (FAILED(d3d12.native.As(&device12))
      || FAILED(device12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))
      || options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
    return Fail("needs a GPU and driver with DXR 1.1.");
  }
  D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {.HighestShaderModel = D3D_SHADER_MODEL_6_9};
  gpu.dxr12 = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2
              && SUCCEEDED(device12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
              && shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_9;
  gpu.device = device;
  gpu.device12_wrapper = d3d12.wrapper;
  gpu.device12 = device12;
  const auto release = [] {
    if (state.gpu.fence_event != nullptr) {
      CloseHandle(state.gpu.fence_event);
    }
    state.gpu = {};
  };

  // Queue, fence (made in D3D12, opened in D3D11), command lists, upload buffers.
  const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
  HANDLE fence_handle = nullptr;
  if (FAILED(device12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(gpu.queue.GetAddressOf())))
      || FAILED(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(gpu.fence12.GetAddressOf())))
      || FAILED(device12->CreateSharedHandle(gpu.fence12.Get(), nullptr, GENERIC_ALL, nullptr, &fence_handle))) {
    Fail("D3D12 queue or fence creation failed.");
    release();
    return false;
  }
  const HRESULT fence_opened = device->OpenSharedFence(fence_handle, IID_PPV_ARGS(gpu.fence11.GetAddressOf()));
  CloseHandle(fence_handle);
  gpu.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (FAILED(fence_opened) || gpu.fence_event == nullptr) {
    Fail("the D3D12 fence cannot be shared with D3D11.");
    release();
    return false;
  }
  for (uint32_t slot = 0; slot < FRAMES_IN_FLIGHT; ++slot) {
    gpu.instance_uploads[slot] = CreateBuffer(device12.Get(), uint64_t{MAX_INSTANCES} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
                                              D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    gpu.table_uploads[slot] = CreateBuffer(device12.Get(), CAPTURES_PER_FRAME * 32, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                                           D3D12_RESOURCE_STATE_GENERIC_READ);
    if (FAILED(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(gpu.allocators[slot].GetAddressOf())))
        || !gpu.instance_uploads[slot] || !gpu.table_uploads[slot]
        || FAILED(gpu.instance_uploads[slot]->Map(0, nullptr, reinterpret_cast<void**>(&gpu.instance_data[slot])))
        || FAILED(gpu.table_uploads[slot]->Map(0, nullptr, reinterpret_cast<void**>(&gpu.table_data[slot])))) {
      Fail("D3D12 command or upload allocation failed.");
      release();
      return false;
    }
  }
  if (FAILED(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocators[0].Get(), nullptr,
                                         IID_PPV_ARGS(gpu.list.GetAddressOf())))
      || FAILED(gpu.list->Close())) {
    Fail("D3D12 command list creation failed.");
    release();
    return false;
  }

  // The vertex copy (BLAS inputs), the TLAS sized for the most instances, the records and pools.
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top_inputs = {
      .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
      .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
      .NumDescs = MAX_INSTANCES,
      .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
  };
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_info = {};
  device12->GetRaytracingAccelerationStructurePrebuildInfo(&top_inputs, &top_info);
  gpu.vertex_buffer = CreateBuffer(device12.Get(), uint64_t{MAX_VERTICES} * VERTEX_BYTES, D3D12_HEAP_TYPE_DEFAULT,
                                   D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  gpu.tlas = CreateBuffer(device12.Get(), top_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
  gpu.tlas_scratch = CreateBuffer(device12.Get(), top_info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const auto pool = [&](uint64_t bytes) {
    return CreateBuffer(device12.Get(), bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  };
  gpu.records = pool(uint64_t{RECORD_COUNT} * 16);
  gpu.uvs = pool(uint64_t{UV_POOL} * 4);
  gpu.triangle_normals = pool(uint64_t{NORMAL_POOL} * 4);
  if (!gpu.vertex_buffer || !gpu.tlas || !gpu.tlas_scratch || !gpu.records || !gpu.uvs || !gpu.triangle_normals) {
    Fail("D3D12 buffer allocation failed.");
    release();
    return false;
  }
  gpu.free_triangles.free = {{0, STATIC_TRIANGLES}};
  gpu.free_uvs.free = {{0, STATIC_UVS}};
  gpu.free_records.clear();
  for (uint32_t record = STATIC_RECORDS; record > 0; --record) {
    gpu.free_records.push_back(record - 1);
  }

  // The passes: t0 the TLAS (root SRV); the descriptor table (Descriptor); b0 their settings, b1 a dispatch's (root
  // constants); t16 the capture table, u12 / u13 / u21 the micromaps' buffers (root descriptors); s0 linear wrap.
  const std::array<D3D12_DESCRIPTOR_RANGE, 4> ranges = {{
      {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = SRV_COUNT, .BaseShaderRegister = 1, .OffsetInDescriptorsFromTableStart = POSITIONS},
      {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = SPECULAR_ACCUMULATED - VISIBILITY, .BaseShaderRegister = 0,
       .OffsetInDescriptorsFromTableStart = VISIBILITY},
      {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = LIGHT_KEPT - SPECULAR_ACCUMULATED, .BaseShaderRegister = 16,
       .OffsetInDescriptorsFromTableStart = SPECULAR_ACCUMULATED},
      {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = DESCRIPTOR_COUNT - LIGHT_KEPT, .BaseShaderRegister = 22,
       .OffsetInDescriptorsFromTableStart = LIGHT_KEPT},
  }};
  const std::array<D3D12_ROOT_PARAMETER, ROOT_COUNT> parameters = {{
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 0}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
       .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .Num32BitValues = 16}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 1, .Num32BitValues = 4}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 16}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 12}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 13}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 21}},
  }};
  const D3D12_STATIC_SAMPLER_DESC sampler = {
      .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .MaxLOD = D3D12_FLOAT32_MAX,
      .ShaderRegister = 0,
  };
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {
      .NumParameters = static_cast<UINT>(parameters.size()),
      .pParameters = parameters.data(),
      .NumStaticSamplers = 1,
      .pStaticSamplers = &sampler,
  };
  ComPtr<ID3DBlob> serialized;
  ComPtr<ID3DBlob> errors;
  const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = DESCRIPTOR_COUNT,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
  };
  if (FAILED(d3d12.serialize_root_signature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), errors.GetAddressOf()))
      || FAILED(device12->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                              IID_PPV_ARGS(gpu.root_signature.GetAddressOf())))) {
    Fail("the ray query root signature failed.");
    release();
    return false;
  }
  const auto pipeline = [&](std::span<const uint8_t> code, ComPtr<ID3D12PipelineState>* state) {
    const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {
        .pRootSignature = gpu.root_signature.Get(),
        .CS = {.pShaderBytecode = code.data(), .BytecodeLength = code.size()},
    };
    return SUCCEEDED(device12->CreateComputePipelineState(&desc, IID_PPV_ARGS(state->GetAddressOf())));
  };
  if (!pipeline(gpu.dxr12 ? std::span<const uint8_t>(__0x7A1E0D33) : std::span<const uint8_t>(__0x7A1E0D13), &gpu.pipeline)
      || !pipeline(__0x7A1E0D14, &gpu.filter_pipeline) || !pipeline(__0x7A1E0D15, &gpu.cache_fill_pipeline)
      || !pipeline(__0x7A1E0D16, &gpu.cache_resolve_pipeline) || !pipeline(__0x7A1E0D19, &gpu.scene_data_pipeline)
      || !pipeline(__0x7A1E0D1D, &gpu.learn_pipeline) || !pipeline(__0x7A1E0D1E, &gpu.specular_pipeline)
      || !pipeline(__0x7A1E0D1F, &gpu.sun_pipeline)
      || (!gpu.dxr12 && !pipeline(__0x7A1E0D1B, &gpu.path_pipeline))
      || FAILED(device12->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(gpu.descriptors.GetAddressOf())))) {
    Fail("the ray query shaders failed (needs shader model 6.5).");
    release();
    return false;
  }
  // DXR 1.2: the opacity micromaps' bake, and the path tracing as a ray generation shader (shader execution
  // reordering); without them, the compute path tracing and no micromaps.
  if (gpu.dxr12) {
    const D3D12_EXPORT_DESC export_desc = {.Name = L"PathTraceRayGen"};
    const D3D12_DXIL_LIBRARY_DESC library = {
        .DXILLibrary = {.pShaderBytecode = __0x7A1E0D1C.data(), .BytecodeLength = __0x7A1E0D1C.size()},
        .NumExports = 1,
        .pExports = const_cast<D3D12_EXPORT_DESC*>(&export_desc),
    };
    const D3D12_RAYTRACING_SHADER_CONFIG shader_config = {.MaxPayloadSizeInBytes = 4, .MaxAttributeSizeInBytes = 8};
    const D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config = {
        .MaxTraceRecursionDepth = 1,
        .Flags = D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS,
    };
    const D3D12_GLOBAL_ROOT_SIGNATURE global = {.pGlobalRootSignature = gpu.root_signature.Get()};
    const std::array<D3D12_STATE_SUBOBJECT, 4> subobjects = {{
        {.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &library},
        {.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shader_config},
        {.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1, .pDesc = &pipeline_config},
        {.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &global},
    }};
    const D3D12_STATE_OBJECT_DESC state_desc = {
        .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
        .NumSubobjects = static_cast<UINT>(subobjects.size()),
        .pSubobjects = subobjects.data(),
    };
    ComPtr<ID3D12StateObjectProperties> properties;
    void* identifier = nullptr;
    gpu.path_shader_table = CreateBuffer(device12.Get(), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, D3D12_HEAP_TYPE_UPLOAD,
                                         D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* table = nullptr;
    if (!pipeline(__0x7A1E0D1A, &gpu.micromap_pipeline)
        || FAILED(device12->CreateStateObject(&state_desc, IID_PPV_ARGS(gpu.path_state_object.GetAddressOf())))
        || FAILED(gpu.path_state_object.As(&properties))
        || (identifier = properties->GetShaderIdentifier(L"PathTraceRayGen")) == nullptr || !gpu.path_shader_table
        || FAILED(gpu.path_shader_table->Map(0, nullptr, &table))) {
      // The driver says 6.9 but the pipeline failed: DXR 1.1 instead.
      gpu.dxr12 = false;
      gpu.path_state_object.Reset();
      gpu.micromap_pipeline.Reset();
      gpu.path_shader_table.Reset();
      gpu.pipeline.Reset();
      if (!pipeline(__0x7A1E0D13, &gpu.pipeline) || !pipeline(__0x7A1E0D1B, &gpu.path_pipeline)) {
        Fail("the ray query shaders failed (needs shader model 6.5).");
        release();
        return false;
      }
    } else {
      std::memcpy(table, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
      gpu.path_shader_table->Unmap(0, nullptr);
    }
  }
  gpu.descriptor_size = device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // D3D11: the private context state, the stream-output stage, the conversion passes, the shared textures.
  const auto feature_level = device->GetFeatureLevel();
  const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
  const D3D11_SO_DECLARATION_ENTRY stream_output = {
      .Stream = 0,
      .SemanticName = "SV_Position",
      .SemanticIndex = 0,
      .StartComponent = 0,
      .ComponentCount = 4,
      .OutputSlot = 0,
  };
  const UINT stream_stride = VERTEX_BYTES;
  const D3D11_BUFFER_DESC captured_desc = {
      .ByteWidth = CAPTURED_BYTES,
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE,
      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
  };
  D3D11_SHADER_RESOURCE_VIEW_DESC captured_view = {
      .Format = DXGI_FORMAT_R32_TYPELESS,
      .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX,
  };
  captured_view.BufferEx = {.FirstElement = 0, .NumElements = CAPTURED_BYTES / 4, .Flags = D3D11_BUFFEREX_SRV_FLAG_RAW};
  const auto constants = [&](UINT bytes, ComPtr<ID3D11Buffer>* buffer) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    return SUCCEEDED(device->CreateBuffer(&desc, nullptr, buffer->GetAddressOf()));
  };
  const D3D11_TEXTURE2D_DESC readback_desc = {
      .Width = SUN_TEXELS,
      .Height = 1,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_STAGING,
      .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
  };
  const D3D11_SAMPLER_DESC sampler_desc = {
      .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
      .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
      .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
      .MaxLOD = D3D11_FLOAT32_MAX,
  };
  if (FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                               nullptr, gpu.private_state.GetAddressOf()))
      || FAILED(device->CreateGeometryShaderWithStreamOutput(__0x7A1E0D10.data(), __0x7A1E0D10.size(), &stream_output, 1,
                                                             &stream_stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                             gpu.capture_shader.GetAddressOf()))
      || FAILED(device->CreateComputeShader(__0x7A1E0D11.data(), __0x7A1E0D11.size(), nullptr, gpu.vertices_shader.GetAddressOf()))
      || FAILED(device->CreateComputeShader(__0x7A1E0D12.data(), __0x7A1E0D12.size(), nullptr, gpu.prepare_shader.GetAddressOf()))
      || FAILED(device->CreateComputeShader(__0x7A1E0D18.data(), __0x7A1E0D18.size(), nullptr, gpu.atlas_shader.GetAddressOf()))
      || FAILED(device->CreateBuffer(&captured_desc, nullptr, gpu.captured.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(gpu.captured.Get(), &captured_view, gpu.captured_srv.GetAddressOf()))
      || !constants(16, &gpu.range_constants) || !constants(CAPTURES_PER_FRAME * 32, &gpu.capture_table)
      || !constants(CAPTURES_PER_FRAME * 32, &gpu.material_rows) || !constants(16, &gpu.atlas_constants)
      || FAILED(device->CreateSamplerState(&sampler_desc, gpu.linear_wrap.GetAddressOf()))
      || FAILED(device->CreateTexture2D(&readback_desc, nullptr, gpu.sun_readback.GetAddressOf()))
      || !CreateShared(DXGI_FORMAT_R32G32B32A32_FLOAT, TRANSFER_WIDTH, TRANSFER_HEIGHT, &gpu.vertices)
      || !CreateShared(DXGI_FORMAT_R32G32B32A32_FLOAT, CAPTURES_PER_FRAME, 1, &gpu.capture_params)
      || !CreateShared(DXGI_FORMAT_R32G32B32A32_FLOAT, SUN_TEXELS, 1, &gpu.sun)
      || !CreateShared(DXGI_FORMAT_R32G32B32A32_FLOAT, SUN_TEXELS, 1, &gpu.previous_sun)) {
    Fail("D3D11 resource creation failed.");
    release();
    return false;
  }
  const D3D12_SHADER_RESOURCE_VIEW_DESC float4_view = TextureView(DXGI_FORMAT_R32G32B32A32_FLOAT);
  device12->CreateShaderResourceView(gpu.sun.resource.Get(), &float4_view, DescriptorAt(SUN));
  device12->CreateShaderResourceView(gpu.previous_sun.resource.Get(), &float4_view, DescriptorAt(PREVIOUS_SUN));
  device12->CreateShaderResourceView(gpu.capture_params.resource.Get(), &float4_view, DescriptorAt(CAPTURE_PARAMS));
  const D3D12_SHADER_RESOURCE_VIEW_DESC vertices_view = BufferSrv(DXGI_FORMAT_R32G32B32A32_FLOAT, MAX_VERTICES);
  const D3D12_SHADER_RESOURCE_VIEW_DESC records_view = BufferSrv(DXGI_FORMAT_R32G32B32A32_UINT, RECORD_COUNT);
  const D3D12_SHADER_RESOURCE_VIEW_DESC uvs_view = BufferSrv(DXGI_FORMAT_R32_UINT, UV_POOL);
  const D3D12_SHADER_RESOURCE_VIEW_DESC normals_view = BufferSrv(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
  device12->CreateShaderResourceView(gpu.vertex_buffer.Get(), &vertices_view, DescriptorAt(VERTICES));
  device12->CreateShaderResourceView(gpu.records.Get(), &records_view, DescriptorAt(RECORDS));
  device12->CreateShaderResourceView(gpu.uvs.Get(), &uvs_view, DescriptorAt(UVS));
  device12->CreateShaderResourceView(gpu.triangle_normals.Get(), &normals_view, DescriptorAt(TRIANGLE_NORMALS));
  const D3D12_UNORDERED_ACCESS_VIEW_DESC records_written = BufferView(DXGI_FORMAT_R32G32B32A32_UINT, RECORD_COUNT);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC uvs_written = BufferView(DXGI_FORMAT_R32_UINT, UV_POOL);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC normals_written = BufferView(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
  device12->CreateUnorderedAccessView(gpu.records.Get(), nullptr, &records_written, DescriptorAt(RECORDS_WRITTEN));
  device12->CreateUnorderedAccessView(gpu.uvs.Get(), nullptr, &uvs_written, DescriptorAt(UVS_WRITTEN));
  device12->CreateUnorderedAccessView(gpu.triangle_normals.Get(), nullptr, &normals_written, DescriptorAt(TRIANGLE_NORMALS_WRITTEN));
  // The effects' own textures and the radiance cache are made when first on; until then their descriptors are null.
  NullDescriptors();
  state.status = std::format("Running ({}; {}).", gpu.dxr12 ? "DXR 1.2: opacity micromaps, shader execution reordering" : "DXR 1.1",
                             agility ? "Agility SDK" : agility_reason);
  return true;
}

// The atlas slice holding the opacity mask `texture` (copied into a free slice when new; the least recently used one
// whose meshes all went unseen for RELEASE_FRAMES is reused); false: none free.
inline bool AtlasSlice(ID3D11DeviceContext* context, ID3D11ShaderResourceView* texture, uint32_t* slice) {
  Gpu& gpu = state.gpu;
  ComPtr<ID3D11Resource> resource;
  texture->GetResource(resource.GetAddressOf());
  if (const auto found = gpu.atlas_slices.find(resource.Get()); found != gpu.atlas_slices.end()) {
    *slice = found->second.second;
    gpu.slice_used[*slice] = state.frame;
    return true;
  }
  if (!gpu.atlas && (!WaitFor(gpu.fence_value) || !CreateAtlas())) return false;
  if (gpu.free_slices.empty()) {
    const auto oldest = std::ranges::min_element(gpu.atlas_slices, {}, [&](const auto& entry) { return gpu.slice_used[entry.second.second]; });
    if (oldest == gpu.atlas_slices.end() || gpu.slice_used[oldest->second.second] + RELEASE_FRAMES >= state.frame) return false;
    gpu.free_slices.push_back(oldest->second.second);
    gpu.atlas_slices.erase(oldest);
  }
  *slice = gpu.free_slices.back();
  gpu.free_slices.pop_back();
  gpu.atlas_slices.emplace(resource.Get(), std::pair{resource, *slice});
  gpu.slice_used[*slice] = state.frame;

  // The copy: from the mip nearest ATLAS_SIZE, in the private context state (the game's restored after).
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
  texture->GetDesc(&view_desc);
  float mip = 0.f;
  if (ComPtr<ID3D11Texture2D> texture2d; view_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && SUCCEEDED(resource.As(&texture2d))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture2d->GetDesc(&desc);
    mip = std::max(std::log2(static_cast<float>(std::max(desc.Width, desc.Height)) / ATLAS_SIZE), 0.f);
  }
  ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return false;
  const talesofarise::dlss::ContextStateScope scope(context1.Get(), gpu.private_state.Get());
  const std::array<uint32_t, 4> copy = {*slice, std::bit_cast<uint32_t>(mip), ATLAS_SIZE, 0};
  context->UpdateSubresource(gpu.atlas_constants.Get(), 0, nullptr, copy.data(), 0, 0);
  ID3D11Buffer* copy_constants = gpu.atlas_constants.Get();
  ID3D11SamplerState* sampler = gpu.linear_wrap.Get();
  ID3D11UnorderedAccessView* atlas = gpu.atlas_uav.Get();
  context->CSSetShader(gpu.atlas_shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, &texture);
  context->CSSetSamplers(0, 1, &sampler);
  context->CSSetUnorderedAccessViews(0, 1, &atlas, nullptr);
  context->CSSetConstantBuffers(0, 1, &copy_constants);
  context->Dispatch(ATLAS_SIZE / 8, ATLAS_SIZE / 8, 1);
  return true;
}

// The stream output of an alpha-tested draw's own vertex shader (`vertex_code`, read only when first needed): its clip
// position and, with `uv_index` < 0xFF, that TEXCOORD's UV.
template <typename VertexCode>
ID3D11GeometryShader* MaskedCaptureShader(uint32_t vertex_hash, uint8_t uv_index, VertexCode&& vertex_code) {
  Gpu& gpu = state.gpu;
  const uint64_t key = (uint64_t{vertex_hash} << 8) | uv_index;
  if (const auto found = gpu.masked_capture_shaders.find(key); found != gpu.masked_capture_shaders.end()) return found->second.Get();
  ComPtr<ID3D11GeometryShader>& shader = gpu.masked_capture_shaders[key];
  const std::optional<std::vector<uint8_t>> code = vertex_code();
  if (!code || code->empty()) return nullptr;
  const std::array<D3D11_SO_DECLARATION_ENTRY, 2> entries = {{
      {.Stream = 0, .SemanticName = "SV_Position", .SemanticIndex = 0, .StartComponent = 0, .ComponentCount = 4, .OutputSlot = 0},
      {.Stream = 0, .SemanticName = "TEXCOORD", .SemanticIndex = uv_index, .StartComponent = 0, .ComponentCount = 2, .OutputSlot = 0},
  }};
  const UINT stride = (uv_index == NO_REGISTER) ? VERTEX_BYTES : MASKED_STRIDE;
  if (FAILED(gpu.device->CreateGeometryShaderWithStreamOutput(code->data(), code->size(), entries.data(), (uv_index == NO_REGISTER) ? 1 : 2,
                                                              &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, shader.GetAddressOf()))) {
    shader.Reset();
  }
  return shader.Get();
}

// The released meshes' records and pool ranges, given back once the GPU is past the frames that read them.
inline void GiveBack(uint64_t completed) {
  Gpu& gpu = state.gpu;
  std::erase_if(gpu.pending_frees, [&](const PendingFree& pending) {
    if (pending.fence_value > completed) return false;
    gpu.free_records.push_back(pending.record);
    gpu.free_triangles.Give(pending.normal_offset, pending.triangles);
    if (pending.uv_offset != UINT32_MAX) gpu.free_uvs.Give(pending.uv_offset, pending.triangles * 3);
    return true;
  });
}

}  // namespace internal
// After a depth prepass draw (post callback, the game's bindings still set): a draw not yet in the scene (a
// character's every frame) is drawn once more into the stream-output buffer (`replay` issues it on `context`).
// `vertex_hash` / `pixel_hash` its shaders (pixel 0: none bound), `arguments` its draw arguments, `vertex_count` the
// vertices it outputs (indices or vertices, times instances; triangle lists only), `vertex_code` its vertex shader's
// bytecode (std::optional<std::vector<uint8_t>>; read only for alpha-tested draws' first capture).
template <typename VertexCode, typename Replay>
void Capture(ID3D11DeviceContext* context, uint32_t vertex_hash, uint32_t pixel_hash, std::span<const uint32_t> arguments,
             uint32_t vertex_count, VertexCode&& vertex_code, Replay&& replay) {
  using internal::state;
  internal::Gpu& gpu = state.gpu;
  if (!Active() || vertex_count == 0 || vertex_count > internal::MAX_VERTICES || vertex_count % 3 != 0) return;
  const auto depth_shader = std::ranges::find(DEPTH_SHADERS, vertex_hash, &DepthShader::hash);
  const auto masked_shader = std::ranges::find(MASKED_SHADERS, pixel_hash, &MaskedShader::hash);
  const bool masked_draw = pixel_hash != 0;
  if (masked_draw ? (masked_shader == MASKED_SHADERS.end() || masked_shader->opacity == Opacity::NEVER)
                  : depth_shader == DEPTH_SHADERS.end()) {
    return;
  }
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  context->IAGetPrimitiveTopology(&topology);
  ComPtr<ID3D11PixelShader> pixel_shader;
  context->PSGetShader(pixel_shader.GetAddressOf(), nullptr, nullptr);
  ComPtr<ID3D11DepthStencilView> depth_view;
  context->OMGetRenderTargets(0, nullptr, depth_view.GetAddressOf());
  if (topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST || static_cast<bool>(pixel_shader) != masked_draw || !depth_view) return;
  ComPtr<ID3D11Resource> depth;
  depth_view->GetResource(depth.GetAddressOf());
  {
    const std::shared_lock lock(state.mutex);
    if (state.failed || depth.Get() != state.scene_depth) return;
  }
  const bool alpha_tested = masked_draw && masked_shader->opacity == Opacity::MASK;
  const bool dynamic = masked_draw ? std::ranges::find(SKINNED_MASKED_SHADERS, vertex_hash) != SKINNED_MASKED_SHADERS.end()
                                   : depth_shader->dynamic;

  // The draw's identity: its buffers, ranges, textures and uniform buffers (all but the per-frame View); a character's
  // geometry alone (its pose buffers change).
  std::array<ID3D11Buffer*, 6> constant_buffers = {};
  std::array<ID3D11ShaderResourceView*, 4> textures = {};
  std::array<ID3D11Buffer*, 2> vertex_buffers = {};
  std::array<UINT, 2> strides = {};
  std::array<UINT, 2> offsets = {};
  ID3D11Buffer* index_buffer = nullptr;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  context->VSGetConstantBuffers(0, static_cast<UINT>(constant_buffers.size()), constant_buffers.data());
  context->VSGetShaderResources(0, static_cast<UINT>(textures.size()), textures.data());
  context->IAGetVertexBuffers(0, static_cast<UINT>(vertex_buffers.size()), vertex_buffers.data(), strides.data(), offsets.data());
  context->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
  // The View: the prepass shader's slot; a masked draw's vertex shader's largest uniform buffer (UE's View, over 2 KB).
  size_t view_slot = masked_draw ? constant_buffers.size() : depth_shader->view_slot;
  if (masked_draw) {
    for (size_t slot = 0; slot < constant_buffers.size(); ++slot) {
      D3D11_BUFFER_DESC desc = {};
      if (constant_buffers[slot] != nullptr) constant_buffers[slot]->GetDesc(&desc);
      if (desc.ByteWidth >= 63 * 16) {
        view_slot = slot;
        break;
      }
    }
  }
  const ComPtr<ID3D11Buffer> view = (view_slot < constant_buffers.size()) ? constant_buffers[view_slot] : nullptr;
  uint64_t key = 14695981039346656037ull;
  const auto mix = [](uint64_t* hash, uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
      *hash = (*hash ^ ((value >> (byte * 8)) & 0xFF)) * 1099511628211ull;
    }
  };
  mix(&key, (uint64_t{pixel_hash} << 32) | vertex_hash);
  for (const uint32_t argument : arguments) {
    mix(&key, argument);
  }
  for (size_t slot = 0; slot < vertex_buffers.size(); ++slot) {
    mix(&key, reinterpret_cast<uintptr_t>(vertex_buffers[slot]));
    mix(&key, (uint64_t{strides[slot]} << 32) | offsets[slot]);
  }
  mix(&key, reinterpret_cast<uintptr_t>(index_buffer));
  mix(&key, (uint64_t{static_cast<uint32_t>(index_format)} << 32) | index_offset);
  const uint64_t geometry_key = key;
  for (size_t slot = 0; slot < constant_buffers.size(); ++slot) {
    if (slot != view_slot) {
      mix(&key, reinterpret_cast<uintptr_t>(constant_buffers[slot]));
    }
  }
  for (ID3D11ShaderResourceView* texture : textures) {
    mix(&key, reinterpret_cast<uintptr_t>(texture));
  }
  // The Get calls added references; the View is kept above, the rest only compared (never used).
  const auto release = [](auto* object) {
    if (object != nullptr) {
      object->Release();
    }
  };
  std::ranges::for_each(constant_buffers, release);
  std::ranges::for_each(textures, release);
  std::ranges::for_each(vertex_buffers, release);
  release(index_buffer);
  if (!view) return;
  // An alpha-tested material: its opacity mask (t0) and the uniform buffers of its UV scale and opacity factor.
  ComPtr<ID3D11ShaderResourceView> mask;
  std::array<ComPtr<ID3D11Buffer>, 2> material_buffers;
  if (alpha_tested) {
    context->PSGetShaderResources(0, 1, mask.GetAddressOf());
    if (masked_shader->uv_slot != NO_REGISTER) context->PSGetConstantBuffers(masked_shader->uv_slot, 1, material_buffers[0].GetAddressOf());
    if (masked_shader->alpha_slot != NO_REGISTER) context->PSGetConstantBuffers(masked_shader->alpha_slot, 1, material_buffers[1].GetAddressOf());
  }

  const std::unique_lock lock(state.mutex);
  const auto note = [&](std::string_view decision) {
    if (state.tracing) {
      state.trace += std::format("draw vs {:08X} ps {:08X} {} vertices: {}\n", vertex_hash, pixel_hash, vertex_count, decision);
    }
  };
  if (state.failed || depth.Get() != state.scene_depth) return;
  if (dynamic) {
    // Each draw of the same character geometry this frame is its own character.
    key = geometry_key;
    mix(&key, state.occurrences[geometry_key]++);
  } else if (const auto mesh = gpu.meshes.find(key); mesh != gpu.meshes.end()) {
    mesh->second.drawn_frame = state.frame;
    if (mesh->second.uv_offset != UINT32_MAX) gpu.slice_used[mesh->second.slice] = state.frame;
    note("in the scene");
    return;
  }
  if (std::ranges::find(state.captures, key, &internal::Capture::key) != state.captures.end()) return;
  const UINT stride = alpha_tested ? internal::MASKED_STRIDE : internal::VERTEX_BYTES;
  if (state.captures.size() >= internal::CAPTURES_PER_FRAME || state.captured_vertices + vertex_count > internal::MAX_VERTICES
      || state.captured_bytes + uint64_t{vertex_count} * stride > internal::CAPTURED_BYTES
      || (dynamic && state.dynamic_vertices + vertex_count > internal::MAX_DYNAMIC_VERTICES)) {
    note(dynamic ? "left out this frame (budget)" : "next frame (budget)");
    return;
  }
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (!internal::Initialize(device.Get())) return;
  uint32_t slice = 0;
  uint32_t flags = 0;
  const size_t index = state.captures.size();
  if (alpha_tested) {
    if (!mask || !internal::AtlasSlice(context, mask.Get(), &slice)) {
      note("left out (no opacity mask slice)");
      return;
    }
    flags = internal::CAPTURE_MASKED;
    // The two material registers into this capture's rows of 0x7A1E0D11's b3.
    const auto copy_row = [&](ID3D11Buffer* source, uint8_t source_register, size_t row) {
      D3D11_BUFFER_DESC desc = {};
      source->GetDesc(&desc);
      if ((source_register + 1u) * 16u > desc.ByteWidth) return false;
      const D3D11_BOX box = {.left = source_register * 16u, .top = 0, .front = 0, .right = source_register * 16u + 16u, .bottom = 1, .back = 1};
      context->CopySubresourceRegion(gpu.material_rows.Get(), 0, static_cast<UINT>(row * 16), 0, 0, source, 0, &box);
      return true;
    };
    if (material_buffers[0] && copy_row(material_buffers[0].Get(), masked_shader->uv_register, index * 2)) {
      flags |= internal::CAPTURE_UV_SCALE | (uint32_t{masked_shader->uv_components} << 2);
    }
    if (material_buffers[1] && copy_row(material_buffers[1].Get(), masked_shader->alpha_register, index * 2 + 1)) {
      flags |= internal::CAPTURE_ALPHA_SCALE | (uint32_t{masked_shader->alpha_component} << 7);
    }
  }
  ID3D11GeometryShader* capture_shader =
      masked_draw ? internal::MaskedCaptureShader(vertex_hash, alpha_tested ? masked_shader->uv_index : NO_REGISTER, vertex_code)
                  : gpu.capture_shader.Get();
  if (capture_shader == nullptr) {
    note("left out (no stream output for its vertex shader)");
    return;
  }

  // The same draw into the stream output only (no rasterization), the game's geometry stage restored after.
  ComPtr<ID3D11GeometryShader> game_geometry_shader;
  context->GSGetShader(game_geometry_shader.GetAddressOf(), nullptr, nullptr);
  ID3D11Buffer* target = gpu.captured.Get();
  const UINT target_offset = state.captured_bytes;
  context->GSSetShader(capture_shader, nullptr, 0);
  context->SOSetTargets(1, &target, &target_offset);
  replay();
  context->SOSetTargets(0, nullptr, nullptr);
  context->GSSetShader(game_geometry_shader.Get(), nullptr, 0);
  state.captures.push_back({
      .key = key,
      .first_vertex = state.captured_vertices,
      .vertex_count = vertex_count,
      .byte_offset = state.captured_bytes,
      .stride = stride,
      .view = view,
      .flags = flags,
      .slice = slice,
      .landscape = !masked_draw && depth_shader->landscape,
      .dynamic = dynamic,
      .masked = alpha_tested,
  });
  state.captured_vertices += vertex_count;
  state.captured_bytes += vertex_count * stride;
  if (dynamic) state.dynamic_vertices += vertex_count;
  note(dynamic ? (alpha_tested ? "captured (character, alpha-tested)" : "captured (character)")
               : (alpha_tested ? "captured (alpha-tested)" : "captured"));
}

// Before Bandai's sun shadow mask (pre callback; its depth and uniform buffers bound): the frame's captures become
// BLASes (alpha-tested ones with opacity micromaps on DXR 1.2; characters refitted), the scene a TLAS, and the sun
// shadows, ambient occlusion and bounce light are traced; the visibility is bound at t40. False: nothing bound.
inline bool Trace(ID3D11DeviceContext* context) {
  using internal::state;
  internal::Gpu& gpu = state.gpu;
  if (!Active()) return false;
  ComPtr<ID3D11ShaderResourceView> depth;
  ComPtr<ID3D11Buffer> shadow_constants;
  ComPtr<ID3D11Buffer> view;
  context->CSGetShaderResources(0, 1, depth.GetAddressOf());
  context->CSGetConstantBuffers(0, 1, shadow_constants.GetAddressOf());
  context->CSGetConstantBuffers(1, 1, view.GetAddressOf());
  ComPtr<ID3D11DeviceContext1> context1;
  ComPtr<ID3D11DeviceContext4> context4;
  if (!depth || !shadow_constants || !view || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))
      || FAILED(context->QueryInterface(IID_PPV_ARGS(context4.GetAddressOf())))) {
    return false;
  }
  ComPtr<ID3D11Resource> depth_resource;
  depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return false;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());

  const std::unique_lock lock(state.mutex);
  state.scene_depth = depth_resource.Get();
  if (!internal::Initialize(device.Get())) return false;
  auto* device12 = gpu.device12.Get();

  // Release what the GPU is done with: old BLASes, outgrown scratch, released meshes' records and pool ranges.
  const uint64_t completed = gpu.fence12->GetCompletedValue();
  std::erase_if(gpu.releases, [completed](const internal::Release& release) { return release.fence_value <= completed; });
  internal::GiveBack(completed);

  // Screen-size textures follow the depth's size (D3D12 idle first: its descriptors point at the old ones).
  if (depth_desc.Width != gpu.width || depth_desc.Height != gpu.height) {
    if (!internal::WaitFor(gpu.fence_value)
        || !internal::CreateShared(DXGI_FORMAT_R32G32B32A32_FLOAT, depth_desc.Width, depth_desc.Height, &gpu.positions)
        || !internal::CreateShared(DXGI_FORMAT_R8_UNORM, depth_desc.Width, depth_desc.Height, &gpu.visibility)
        || !internal::CreateShared(DXGI_FORMAT_R8_UNORM, depth_desc.Width, depth_desc.Height, &gpu.ambient)) {
      gpu.width = 0;
      gpu.height = 0;
      return internal::Fail("screen-size textures failed.");
    }
    gpu.width = depth_desc.Width;
    gpu.height = depth_desc.Height;
    const D3D12_SHADER_RESOURCE_VIEW_DESC positions_view = internal::TextureView(DXGI_FORMAT_R32G32B32A32_FLOAT);
    const D3D12_UNORDERED_ACCESS_VIEW_DESC visibility_view = internal::TextureUav(DXGI_FORMAT_R8_UNORM);
    device12->CreateShaderResourceView(gpu.positions.resource.Get(), &positions_view, internal::DescriptorAt(internal::POSITIONS));
    device12->CreateUnorderedAccessView(gpu.visibility.resource.Get(), nullptr, &visibility_view, internal::DescriptorAt(internal::VISIBILITY));
    device12->CreateUnorderedAccessView(gpu.ambient.resource.Get(), nullptr, &visibility_view, internal::DescriptorAt(internal::AMBIENT));
    internal::VisibilityDescriptor();
    gpu.accumulated.Reset();
    gpu.history.Reset();
    gpu.sun_rays.Reset();
    gpu.specular = {};
    gpu.previous_positions = {};
    gpu.bounce_light = {};
    gpu.previous_color = {};
    gpu.reflection = {};
    gpu.specular_occlusion_texture = {};
    gpu.gbuffer = {};
    internal::NullDescriptors();
  }
  const bool pt = path_tracing.load(std::memory_order_relaxed);
  const bool bounce = bounce_light.load(std::memory_order_relaxed) && !pt;
  const bool reflect = reflections.load(std::memory_order_relaxed);
  const bool sun_shadows = enabled.load(std::memory_order_relaxed);
  const float softness = std::clamp(sun_softness.load(std::memory_order_relaxed), 0.f, 5.f);
  const bool soft = sun_shadows && softness > 0.01f;
  const bool uses_history = UsesHistory();
  if (uses_history && !gpu.previous_positions.texture && (!internal::WaitFor(gpu.fence_value) || !internal::CreateHistory())) {
    return internal::Fail("last frame's positions or the radiance cache failed.");
  }
  if ((bounce || pt) && !gpu.bounce_light.texture && (!internal::WaitFor(gpu.fence_value) || !internal::CreateBounceTextures())) {
    return internal::Fail("the bounce light's textures failed.");
  }
  if (soft && !gpu.sun_rays && (!internal::WaitFor(gpu.fence_value) || !internal::CreateSunTextures())) {
    return internal::Fail("the soft shadows' textures failed.");
  }
  // Last frame's positions (the positions texture before this frame's) and scene colour are last frame's when both
  // ran last frame; the accumulations when they ran too. The cache is usable once filled.
  const bool history_valid = uses_history && gpu.previous_color.texture && state.traced_frame + 1 == state.frame
                             && state.color_frame + 1 == state.frame;
  const bool bounce_history = bounce && history_valid && state.bounce_frame + 1 == state.frame;
  const bool cache_valid = uses_history && (history_valid || state.cache_fills != 0);

  const std::vector<internal::Capture> captures = std::move(state.captures);
  const uint32_t captured_vertices = state.captured_vertices;
  state.captures.clear();
  state.captured_vertices = 0;
  state.captured_bytes = 0;
  state.dynamic_vertices = 0;
  const size_t other_views = std::ranges::count_if(captures, [&](const internal::Capture& capture) { return capture.view != view; });

  // D3D11 (in its own context state, the game's restored after): world positions (and UVs) of the captures and of the
  // pixels, the sun; then a signal D3D12 waits for.
  {
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), gpu.private_state.Get());
    if (!captures.empty()) {
      std::array<uint32_t, internal::CAPTURES_PER_FRAME * 8> table = {};
      for (size_t index = 0; index < captures.size(); ++index) {
        const auto& capture = captures[index];
        table[index * 8] = capture.first_vertex;
        table[index * 8 + 1] = capture.vertex_count;
        table[index * 8 + 2] = capture.byte_offset;
        table[index * 8 + 3] = capture.stride;
        table[index * 8 + 4] = capture.flags;
      }
      context->UpdateSubresource(gpu.capture_table.Get(), 0, nullptr, table.data(), 0, 0);
      ID3D11ShaderResourceView* input = gpu.captured_srv.Get();
      ID3D11UnorderedAccessView* outputs[] = {gpu.vertices.uav.Get(), gpu.capture_params.uav.Get()};
      context->CSSetShader(gpu.vertices_shader.Get(), nullptr, 0);
      context->CSSetShaderResources(0, 1, &input);
      context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
      // One pass per run of captures with the same View (normally the whole frame).
      for (size_t first = 0; first < captures.size();) {
        size_t last = first;
        while (last + 1 < captures.size() && captures[last + 1].view == captures[first].view) {
          ++last;
        }
        const uint32_t first_vertex = captures[first].first_vertex;
        const uint32_t vertex_count = captures[last].first_vertex + captures[last].vertex_count - first_vertex;
        const std::array<uint32_t, 4> range = {static_cast<uint32_t>(first), static_cast<uint32_t>(last - first + 1), first_vertex, vertex_count};
        context->UpdateSubresource(gpu.range_constants.Get(), 0, nullptr, range.data(), 0, 0);
        ID3D11Buffer* constants[] = {gpu.range_constants.Get(), captures[first].view.Get(), gpu.capture_table.Get(), gpu.material_rows.Get()};
        context->CSSetConstantBuffers(0, 4, constants);
        context->Dispatch((vertex_count + 255) / 256, 1, 1);
        first = last + 1;
      }
    }
    context->CopyResource(gpu.previous_sun.texture.Get(), gpu.sun.texture.Get());
    if (uses_history) {
      context->CopyResource(gpu.previous_positions.texture.Get(), gpu.positions.texture.Get());
    }
    ID3D11ShaderResourceView* input = depth.Get();
    ID3D11UnorderedAccessView* outputs[] = {gpu.positions.uav.Get(), gpu.sun.uav.Get()};
    ID3D11Buffer* constants[] = {shadow_constants.Get(), view.Get()};
    context->CSSetShader(gpu.prepare_shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, &input);
    context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
    context->CSSetConstantBuffers(0, 2, constants);
    context->Dispatch((gpu.width + 7) / 8, (gpu.height + 7) / 8, 1);
    if (state.tracing) {
      context->CopyResource(gpu.sun_readback.Get(), gpu.sun.texture.Get());
      state.sun_read = true;
    }
    context4->Signal(gpu.fence11.Get(), ++gpu.fence_value);
  }
  const uint64_t inputs_ready = gpu.fence_value;
  context->Flush();

  // D3D12: the BLAS inputs copied, compactions, the scene data, micromaps, BLASes, the TLAS, the radiance cache, the
  // ray queries.
  const uint32_t slot = gpu.submissions % internal::FRAMES_IN_FLIGHT;
  const size_t ring = state.frame % internal::FRAMES_IN_FLIGHT;
  // The frame FRAMES_IN_FLIGHT back is done: its characters' records and pool ranges are free, its compacted sizes
  // readable.
  if (!internal::WaitFor(gpu.allocator_values[slot]) || !internal::WaitFor(gpu.frame_fences[ring])
      || FAILED(gpu.allocators[slot]->Reset())
      || FAILED(gpu.list->Reset(gpu.allocators[slot].Get(), nullptr))) {
    return internal::Fail("the D3D12 queue stopped.");
  }
  auto* list = gpu.list.Get();
  const auto uav_barrier = internal::UavBarrier();
  ID3D12DescriptorHeap* heaps[] = {gpu.descriptors.Get()};
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootSignature(gpu.root_signature.Get());
  list->SetComputeRootDescriptorTable(internal::ROOT_TABLE, gpu.descriptors->GetGPUDescriptorHandleForHeapStart());
  // Unused root descriptors still point at a buffer.
  list->SetComputeRootShaderResourceView(internal::ROOT_CAPTURE_TABLE, gpu.table_uploads[slot]->GetGPUVirtualAddress());
  for (const internal::Root root : {internal::ROOT_MICROMAP_DATA, internal::ROOT_MICROMAP_INDICES, internal::ROOT_MICROMAP_DESCS}) {
    list->SetComputeRootUnorderedAccessView(root, gpu.tlas_scratch->GetGPUVirtualAddress());
  }
  // A scratch arena for builds recorded together (no barrier between them): grown when this frame's need more.
  const auto ensure_scratch = [&](uint64_t bytes) {
    if (bytes <= gpu.scratch_bytes) return true;
    if (gpu.scratch) {
      gpu.releases.push_back({.fence_value = gpu.fence_value + 1, .resource = std::move(gpu.scratch)});
    }
    gpu.scratch_bytes = std::max(bytes, gpu.scratch_bytes * 2);
    gpu.scratch = internal::CreateBuffer(device12, gpu.scratch_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!gpu.scratch) gpu.scratch_bytes = 0;
    return static_cast<bool>(gpu.scratch);
  };
  const auto align = [](uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; };

  // Compactions: the static BLASes built FRAMES_IN_FLIGHT frames ago copied into buffers their compacted size.
  auto& compaction = gpu.compactions[ring];
  uint64_t compacted_bytes = 0;
  if (!compaction.keys.empty()) {
    const D3D12_RANGE read = {.Begin = 0, .End = compaction.keys.size() * sizeof(uint64_t)};
    void* mapped = nullptr;
    if (SUCCEEDED(compaction.readback->Map(0, &read, &mapped))) {
      const auto* sizes = static_cast<const uint64_t*>(mapped);
      for (size_t index = 0; index < compaction.keys.size(); ++index) {
        const auto mesh = gpu.meshes.find(compaction.keys[index]);
        if (mesh == gpu.meshes.end() || mesh->second.compacted || sizes[index] == 0
            || sizes[index] >= mesh->second.blas->GetDesc().Width) {
          continue;
        }
        auto compacted = internal::CreateBuffer(device12, sizes[index], D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        if (!compacted) break;
        list->CopyRaytracingAccelerationStructure(compacted->GetGPUVirtualAddress(), mesh->second.blas->GetGPUVirtualAddress(),
                                                  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
        compacted_bytes += mesh->second.blas->GetDesc().Width - sizes[index];
        gpu.releases.push_back({.fence_value = gpu.fence_value + 1, .resource = std::move(mesh->second.blas)});
        mesh->second.blas = std::move(compacted);
        mesh->second.compacted = true;
      }
      const D3D12_RANGE written = {};
      compaction.readback->Unmap(0, &written);
    }
    compaction.keys.clear();
    list->ResourceBarrier(1, &uav_barrier);
  }

  // This frame's captures' places: a record and pool ranges (static meshes keep theirs; characters take this frame's
  // turn of the dynamic ones). Captures without room wait (static) or are left out (characters).
  struct Placement {
    uint32_t record = UINT32_MAX;
    uint32_t normal_offset = UINT32_MAX;
    uint32_t uv_offset = UINT32_MAX;
  };
  std::vector<Placement> placements(captures.size());
  uint32_t dynamic_records = 0;
  uint32_t dynamic_triangles_used = 0;
  uint32_t dynamic_uvs_used = 0;
  for (size_t index = 0; index < captures.size(); ++index) {
    const auto& capture = captures[index];
    const uint32_t triangles = capture.vertex_count / 3;
    auto& placement = placements[index];
    if (capture.dynamic) {
      if (dynamic_triangles_used + triangles > internal::DYNAMIC_TRIANGLES
          || (capture.masked && dynamic_uvs_used + capture.vertex_count > internal::DYNAMIC_UVS)) {
        continue;
      }
      placement.record = internal::STATIC_RECORDS + static_cast<uint32_t>(ring * internal::CAPTURES_PER_FRAME) + dynamic_records++;
      placement.normal_offset = internal::STATIC_TRIANGLES + static_cast<uint32_t>(ring) * internal::DYNAMIC_TRIANGLES + dynamic_triangles_used;
      dynamic_triangles_used += triangles;
      if (capture.masked) {
        placement.uv_offset = internal::STATIC_UVS + static_cast<uint32_t>(ring) * internal::DYNAMIC_UVS + dynamic_uvs_used;
        dynamic_uvs_used += capture.vertex_count;
      }
      continue;
    }
    if (gpu.free_records.empty() || !gpu.free_triangles.Take(triangles, &placement.normal_offset)) {
      placement = {};
      continue;
    }
    if (capture.masked && !gpu.free_uvs.Take(capture.vertex_count, &placement.uv_offset)) {
      gpu.free_triangles.Give(placement.normal_offset, triangles);
      placement = {};
      continue;
    }
    placement.record = gpu.free_records.back();
    gpu.free_records.pop_back();
  }

  uint32_t built = 0;
  uint64_t built_triangles = 0;
  uint32_t masked_built = 0;
  uint32_t micromaps_built = 0;
  uint64_t micromap_bytes = 0;
  uint32_t characters_refitted = 0;
  uint32_t characters_rebuilt = 0;
  uint64_t dynamic_triangles = 0;
  // This frame's characters' instances (BLAS, record, masked).
  struct CharacterInstance {
    ID3D12Resource* blas = nullptr;
    uint32_t record = 0;
    bool masked = false;
  };
  std::vector<CharacterInstance> character_instances;
  if (!captures.empty()) {
    const UINT rows = (captured_vertices + internal::TRANSFER_WIDTH - 1) / internal::TRANSFER_WIDTH;
    const D3D12_TEXTURE_COPY_LOCATION destination = {
        .pResource = gpu.vertex_buffer.Get(),
        .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint = {
            .Offset = 0,
            .Footprint = {
                .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                .Width = internal::TRANSFER_WIDTH,
                .Height = rows,
                .Depth = 1,
                .RowPitch = internal::TRANSFER_WIDTH * internal::VERTEX_BYTES,
            },
        },
    };
    const D3D12_TEXTURE_COPY_LOCATION source = {
        .pResource = gpu.vertices.resource.Get(),
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        .SubresourceIndex = 0,
    };
    const D3D12_BOX box = {.left = 0, .top = 0, .front = 0, .right = internal::TRANSFER_WIDTH, .bottom = rows, .back = 1};
    const auto to_copy = internal::Transition(gpu.vertices.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->ResourceBarrier(1, &to_copy);
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
    std::vector<D3D12_RESOURCE_BARRIER> to_scene_data = {
        internal::Transition(gpu.vertices.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
        internal::Transition(gpu.vertex_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        internal::Transition(gpu.capture_params.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        internal::Transition(gpu.records.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        internal::Transition(gpu.uvs.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        internal::Transition(gpu.triangle_normals.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };
    if (gpu.atlas12) {
      to_scene_data.push_back(internal::Transition(gpu.atlas12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }
    list->ResourceBarrier(static_cast<UINT>(to_scene_data.size()), to_scene_data.data());

    // The scene data: normals, UVs, records (0x7A1E0D19); captures without a place are skipped.
    uint32_t* table = gpu.table_data[slot];
    for (size_t index = 0; index < captures.size(); ++index) {
      const auto& capture = captures[index];
      const auto& placement = placements[index];
      const uint32_t record_flags = (capture.masked ? internal::RECORD_MASKED : 0u) << 16;
      const std::array<uint32_t, 8> entry = {capture.first_vertex, capture.vertex_count, placement.normal_offset, placement.uv_offset,
                                             placement.record, capture.slice | record_flags, 0, 0};
      std::memcpy(table + index * 8, entry.data(), sizeof(entry));
    }
    const std::array<uint32_t, 4> scene_dispatch = {static_cast<uint32_t>(captures.size()), captured_vertices / 3, 0, 0};
    list->SetPipelineState(gpu.scene_data_pipeline.Get());
    list->SetComputeRoot32BitConstants(internal::ROOT_DISPATCH, 4, scene_dispatch.data(), 0);
    list->Dispatch((captured_vertices / 3 + 63) / 64, 1, 1);
    const std::array<D3D12_RESOURCE_BARRIER, 3> scene_written = {
        internal::Transition(gpu.records.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        internal::Transition(gpu.uvs.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        internal::Transition(gpu.triangle_normals.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    list->ResourceBarrier(static_cast<UINT>(scene_written.size()), scene_written.data());

    // Opacity micromaps (DXR 1.2) for the new alpha-tested static meshes: their level (4 - 2) fitting
    // MICROMAP_MESH_BYTES, all within MICROMAP_FRAME_BYTES; baked (0x7A1E0D1A), then built into micromap arrays.
    std::vector<internal::MicromapBake> bakes;
    std::unordered_map<uint64_t, std::pair<ComPtr<ID3D12Resource>, ComPtr<ID3D12Resource>>> micromaps;  // array, indices
    if (gpu.dxr12) {
      uint64_t data_bytes = 0;
      uint64_t desc_bytes = 0;
      for (size_t index = 0; index < captures.size(); ++index) {
        const auto& capture = captures[index];
        if (capture.dynamic || !capture.masked || placements[index].record == UINT32_MAX) continue;
        const uint64_t triangles = capture.vertex_count / 3;
        uint32_t level = 4;
        const auto bytes_at = [&](uint32_t at) { return triangles * std::max<uint64_t>((1ull << (2 * at)) / 4, 4); };
        while (level > 2 && bytes_at(level) > internal::MICROMAP_MESH_BYTES) {
          --level;
        }
        if (bytes_at(level) > internal::MICROMAP_MESH_BYTES || data_bytes + bytes_at(level) > internal::MICROMAP_FRAME_BYTES) continue;
        bakes.push_back({.key = capture.key, .level = level, .data_offset = data_bytes, .desc_offset = desc_bytes});
        data_bytes = align(data_bytes + bytes_at(level), 256);
        desc_bytes = align(desc_bytes + triangles * 8, 256);
      }
      const auto grow = [&](ComPtr<ID3D12Resource>* buffer, uint64_t bytes) {
        if (*buffer && (*buffer)->GetDesc().Width >= bytes) return true;
        if (*buffer) gpu.releases.push_back({.fence_value = gpu.fence_value + 1, .resource = std::move(*buffer)});
        *buffer = internal::CreateBuffer(device12, std::max<uint64_t>(bytes, 1 << 20), D3D12_HEAP_TYPE_DEFAULT,
                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return static_cast<bool>(*buffer);
      };
      if (!bakes.empty() && (!grow(&gpu.micromap_data, data_bytes) || !grow(&gpu.micromap_descs, desc_bytes))) bakes.clear();
      if (!bakes.empty()) {
        list->SetPipelineState(gpu.micromap_pipeline.Get());
        std::vector<D3D12_RESOURCE_BARRIER> baked;
        for (auto bake = bakes.begin(); bake != bakes.end();) {
          const auto capture = std::ranges::find(captures, bake->key, &internal::Capture::key);
          const auto& placement = placements[capture - captures.begin()];
          const uint32_t triangles = capture->vertex_count / 3;
          auto indices = internal::CreateBuffer(device12, uint64_t{triangles} * 4, D3D12_HEAP_TYPE_DEFAULT,
                                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          if (!indices) {
            bake = bakes.erase(bake);
            continue;
          }
          const std::array<uint32_t, 4> bake_dispatch = {placement.record, triangles, bake->level, 0};
          list->SetComputeRoot32BitConstants(internal::ROOT_DISPATCH, 4, bake_dispatch.data(), 0);
          list->SetComputeRootUnorderedAccessView(internal::ROOT_MICROMAP_DATA, gpu.micromap_data->GetGPUVirtualAddress() + bake->data_offset);
          list->SetComputeRootUnorderedAccessView(internal::ROOT_MICROMAP_INDICES, indices->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(internal::ROOT_MICROMAP_DESCS, gpu.micromap_descs->GetGPUVirtualAddress() + bake->desc_offset);
          list->Dispatch((triangles + 63) / 64, 1, 1);
          baked.push_back(internal::Transition(indices.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          micromaps[bake->key].second = std::move(indices);
          ++bake;
        }
        baked.push_back(internal::Transition(gpu.micromap_data.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        baked.push_back(internal::Transition(gpu.micromap_descs.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        list->ResourceBarrier(static_cast<UINT>(baked.size()), baked.data());
        // The micromap arrays, built together.
        std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY> histograms(bakes.size());
        std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC> arrays(bakes.size());
        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> builds;
        uint64_t scratch_needed = 0;
        for (size_t index = 0; index < bakes.size(); ++index) {
          const auto& bake = bakes[index];
          const auto capture = std::ranges::find(captures, bake.key, &internal::Capture::key);
          histograms[index] = {.Count = capture->vertex_count / 3, .SubdivisionLevel = bake.level,
                               .Format = D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE};
          arrays[index] = {
              .NumOmmHistogramEntries = 1,
              .pOmmHistogram = &histograms[index],
              .InputBuffer = gpu.micromap_data->GetGPUVirtualAddress() + bake.data_offset,
              .PerOmmDescs = {.StartAddress = gpu.micromap_descs->GetGPUVirtualAddress() + bake.desc_offset,
                              .StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)},
          };
          const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
              .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY,
              .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
              .NumDescs = 1,
              .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
              .pOpacityMicromapArrayDesc = &arrays[index],
          };
          D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
          device12->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
          auto result = internal::CreateBuffer(device12, info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE,
                                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          if (!result) {
            micromaps.erase(bake.key);
            continue;
          }
          builds.push_back({.DestAccelerationStructureData = result->GetGPUVirtualAddress(), .Inputs = inputs,
                            .ScratchAccelerationStructureData = scratch_needed});
          scratch_needed = align(scratch_needed + info.ScratchDataSizeInBytes, 256);
          micromap_bytes += info.ResultDataMaxSizeInBytes;
          micromaps[bake.key].first = std::move(result);
        }
        const bool arrays_built = !builds.empty() && ensure_scratch(scratch_needed);
        if (arrays_built) {
          for (auto& build : builds) {
            build.ScratchAccelerationStructureData += gpu.scratch->GetGPUVirtualAddress();
            list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
          }
          micromaps_built = static_cast<uint32_t>(builds.size());
          list->ResourceBarrier(1, &uav_barrier);
        }
        // Meshes without both (their buffers stay until the GPU is past this frame's bake) get none.
        std::erase_if(micromaps, [&](auto& entry) {
          auto& [array, indices] = entry.second;
          if (arrays_built && array && indices) return false;
          for (ComPtr<ID3D12Resource>* resource : {std::addressof(array), std::addressof(indices)}) {
            if (*resource) gpu.releases.push_back({.fence_value = gpu.fence_value + 1, .resource = std::move(*resource)});
          }
          return true;
        });
      }
    }

    // The compacted sizes of this frame's static builds (made at the first).
    if (!compaction.sizes) {
      const uint64_t bytes = internal::CAPTURES_PER_FRAME * sizeof(uint64_t);
      compaction.sizes = internal::CreateBuffer(device12, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      compaction.readback = internal::CreateBuffer(device12, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                                                   D3D12_RESOURCE_STATE_COPY_DEST);
      if (!compaction.sizes || !compaction.readback) compaction = {};
    }
    // The BLASes, built together: static ones once (for tracing, then compacted), characters refitted to this frame's
    // pose (rebuilt every CHARACTER_REBUILD_FRAMES frames or when their vertices change).
    std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> triangle_descs(captures.size());
    std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> linkages(captures.size());
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(captures.size());
    struct Build {
      D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
      bool compact = false;
    };
    std::vector<Build> builds;
    uint64_t scratch_needed = 0;
    for (size_t index = 0; index < captures.size(); ++index) {
      const auto& capture = captures[index];
      const auto& placement = placements[index];
      if (placement.record == UINT32_MAX) continue;
      triangle_descs[index] = {
          .IndexFormat = DXGI_FORMAT_UNKNOWN,
          .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
          .VertexCount = capture.vertex_count,
          .VertexBuffer = {
              .StartAddress = gpu.vertex_buffer->GetGPUVirtualAddress() + uint64_t{capture.first_vertex} * internal::VERTEX_BYTES,
              .StrideInBytes = internal::VERTEX_BYTES,
          },
      };
      const auto micromap = micromaps.find(capture.key);
      if (micromap != micromaps.end()) {
        linkages[index] = {
            .OpacityMicromapIndexBuffer = {.StartAddress = micromap->second.second->GetGPUVirtualAddress(), .StrideInBytes = 4},
            .OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT,
            .OpacityMicromapBaseLocation = 0,
            .OpacityMicromapArray = micromap->second.first->GetGPUVirtualAddress(),
        };
        geometries[index] = {
            .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES,
            .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE,
            .OmmTriangles = {.pTriangles = &triangle_descs[index], .pOmmLinkage = &linkages[index]},
        };
      } else {
        geometries[index] = {
            .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
            .Flags = capture.masked ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
            .Triangles = triangle_descs[index],
        };
      }
      const bool compact = !capture.dynamic && compaction.sizes;
      D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
          .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
          .Flags = capture.dynamic ? (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
                                      | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE)
                                   : (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                                      | (compact ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION
                                                 : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE)),
          .NumDescs = 1,
          .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
          .pGeometryDescs = &geometries[index],
      };
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
      device12->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
      if (capture.dynamic) {
        auto& character = gpu.characters[capture.key];
        const bool refit = character.blas && character.vertex_count == capture.vertex_count && character.masked == capture.masked
                           && character.refits < internal::CHARACTER_REBUILD_FRAMES;
        if (!refit && character.bytes < info.ResultDataMaxSizeInBytes) {
          if (character.blas) gpu.releases.push_back({.fence_value = gpu.fence_value + 1, .resource = std::move(character.blas)});
          character.bytes = info.ResultDataMaxSizeInBytes + info.ResultDataMaxSizeInBytes / 4;
          character.blas = internal::CreateBuffer(device12, character.bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          if (!character.blas) {
            character = {};
            continue;
          }
        }
        if (refit) inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        builds.push_back({.desc = {.DestAccelerationStructureData = character.blas->GetGPUVirtualAddress(),
                                   .Inputs = inputs,
                                   .SourceAccelerationStructureData = refit ? character.blas->GetGPUVirtualAddress() : 0,
                                   .ScratchAccelerationStructureData = scratch_needed}});
        scratch_needed = align(scratch_needed + (refit ? info.UpdateScratchDataSizeInBytes : info.ScratchDataSizeInBytes), 256);
        character.refits = refit ? character.refits + 1 : 0;
        character.vertex_count = capture.vertex_count;
        character.masked = capture.masked;
        character.seen_frame = state.frame;
        (refit ? characters_refitted : characters_rebuilt) += 1;
        dynamic_triangles += capture.vertex_count / 3;
        character_instances.push_back({.blas = character.blas.Get(), .record = placement.record, .masked = capture.masked});
        continue;
      }
      auto blas = internal::CreateBuffer(device12, info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
      if (!blas) {
        // No room: its place goes back now (nothing references it).
        gpu.free_records.push_back(placement.record);
        gpu.free_triangles.Give(placement.normal_offset, capture.vertex_count / 3);
        if (placement.uv_offset != UINT32_MAX) gpu.free_uvs.Give(placement.uv_offset, capture.vertex_count);
        continue;
      }
      builds.push_back({.desc = {.DestAccelerationStructureData = blas->GetGPUVirtualAddress(), .Inputs = inputs,
                                 .ScratchAccelerationStructureData = scratch_needed},
                        .compact = compact});
      scratch_needed = align(scratch_needed + info.ScratchDataSizeInBytes, 256);
      if (compact) compaction.keys.push_back(capture.key);
      internal::Mesh mesh = {
          .blas = std::move(blas),
          .drawn_frame = state.frame,
          .triangles = capture.vertex_count / 3,
          .record = placement.record,
          .normal_offset = placement.normal_offset,
          .uv_offset = placement.uv_offset,
          .slice = capture.slice,
          .landscape = capture.landscape,
      };
      if (micromap != micromaps.end()) {
        mesh.micromaps = std::move(micromap->second.first);
        mesh.micromap_indices = std::move(micromap->second.second);
      }
      gpu.meshes[capture.key] = std::move(mesh);
      ++built;
      built_triangles += capture.vertex_count / 3;
      masked_built += capture.masked ? 1 : 0;
    }
    if (!ensure_scratch(scratch_needed)) {
      return internal::Fail("the BLAS scratch allocation failed.");
    }
    size_t compact_slot = 0;
    for (auto& build : builds) {
      build.desc.ScratchAccelerationStructureData += gpu.scratch->GetGPUVirtualAddress();
      const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild = {
          .DestBuffer = build.compact ? compaction.sizes->GetGPUVirtualAddress() + (compact_slot++) * sizeof(uint64_t) : 0,
          .InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE,
      };
      list->BuildRaytracingAccelerationStructure(&build.desc, build.compact ? 1 : 0, build.compact ? &postbuild : nullptr);
    }
    list->ResourceBarrier(1, &uav_barrier);
    std::vector<D3D12_RESOURCE_BARRIER> after_builds = {
        internal::Transition(gpu.vertex_buffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
        internal::Transition(gpu.capture_params.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
    };
    if (gpu.atlas12) {
      after_builds.push_back(internal::Transition(gpu.atlas12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
    }
    if (!bakes.empty()) {
      after_builds.push_back(internal::Transition(gpu.micromap_data.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
      after_builds.push_back(internal::Transition(gpu.micromap_descs.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    }
    if (!compaction.keys.empty()) {
      after_builds.push_back(internal::Transition(compaction.sizes.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
    }
    list->ResourceBarrier(static_cast<UINT>(after_builds.size()), after_builds.data());
    if (!compaction.keys.empty()) {
      list->CopyBufferRegion(compaction.readback.Get(), 0, compaction.sizes.Get(), 0, compaction.keys.size() * sizeof(uint64_t));
      const auto back = internal::Transition(compaction.sizes.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &back);
    }
  }

  // The scene: meshes drawn this frame, recently drawn ones (not the landscape), and this frame's characters.
  UINT instance_count = 0;
  uint64_t scene_triangles = 0;
  D3D12_RAYTRACING_INSTANCE_DESC* instances = gpu.instance_data[slot];
  // Masks as rt_surface.hlsli's: 1 static meshes, 2 characters, 4 alpha-tested static meshes; InstanceID the record.
  const auto add_instance = [&](ID3D12Resource* blas, uint64_t triangles, UINT mask, uint32_t record, bool masked) {
    if (instance_count == internal::MAX_INSTANCES) return;
    instances[instance_count] = {
        .InstanceID = record,
        .InstanceMask = mask,
        .InstanceContributionToHitGroupIndex = 0,
        .Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | (masked ? 0u : static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE)),
        .AccelerationStructure = blas->GetGPUVirtualAddress(),
    };
    instances[instance_count].Transform[0][0] = 1.f;
    instances[instance_count].Transform[1][1] = 1.f;
    instances[instance_count].Transform[2][2] = 1.f;
    ++instance_count;
    scene_triangles += triangles;
  };
  for (const auto& [key, mesh] : gpu.meshes) {
    const bool current = mesh.drawn_frame == state.frame;
    const bool recent = !mesh.landscape && mesh.drawn_frame + internal::RECENT_FRAMES >= state.frame;
    const bool masked = mesh.uv_offset != UINT32_MAX;
    if (current || recent) add_instance(mesh.blas.Get(), mesh.triangles, masked ? 4 : 1, mesh.record, masked);
  }
  for (const auto& character : character_instances) {
    add_instance(character.blas, 0, 2, character.record, character.masked);
  }
  scene_triangles += dynamic_triangles;
  const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_build = {
      .DestAccelerationStructureData = gpu.tlas->GetGPUVirtualAddress(),
      .Inputs = {
          .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
          .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
          .NumDescs = instance_count,
          .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
          .InstanceDescs = gpu.instance_uploads[slot]->GetGPUVirtualAddress(),
      },
      .ScratchAccelerationStructureData = gpu.tlas_scratch->GetGPUVirtualAddress(),
  };
  list->BuildRaytracingAccelerationStructure(&top_build, 0, nullptr);
  // Shared textures: COMMON between the APIs, read or written here (last frame's only while an effect reads them).
  std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> shared = {
      {gpu.positions.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
      {gpu.sun.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
      {gpu.previous_sun.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
      {gpu.visibility.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
      {gpu.ambient.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
  };
  if (gpu.atlas12) shared.emplace_back(gpu.atlas12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  if (uses_history) {
    shared.emplace_back(gpu.previous_positions.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (gpu.previous_color.resource) {
      shared.emplace_back(gpu.previous_color.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
  }
  if (bounce) {
    shared.emplace_back(gpu.bounce_light.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  std::vector<D3D12_RESOURCE_BARRIER> to_trace = {internal::UavBarrier()};
  for (const auto& [resource, used] : shared) {
    to_trace.push_back(internal::Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
  }
  list->ResourceBarrier(static_cast<UINT>(to_trace.size()), to_trace.data());
  list->SetComputeRootShaderResourceView(internal::ROOT_SCENE, gpu.tlas->GetGPUVirtualAddress());
  const float range = std::clamp(ambient_range.load(std::memory_order_relaxed), 10.f, 10000.f);
  const float reach = std::clamp(bounce_range.load(std::memory_order_relaxed), 10.f, 100000.f);
  const float strength = std::clamp(bounce_strength.load(std::memory_order_relaxed), 0.f, 16.f);
  const float cone = soft ? std::tan(softness * 3.14159265f / 180.f) : 0.f;
  state.settings = {
      static_cast<uint32_t>(state.frame),
      (sun_shadows ? 1u : 0u) | (ambient_occlusion.load(std::memory_order_relaxed) ? 2u : 0u) | (bounce ? 4u : 0u)
          | (history_valid ? 8u : 0u) | (reflect ? 16u : 0u) | (cache_valid ? 32u : 0u) | (bounce_history ? 64u : 0u)
          | (character_shadows.load(std::memory_order_relaxed) ? 256u : 0u) | (pt ? 512u : 0u)
          | (specular_occlusion.load(std::memory_order_relaxed) ? 2048u : 0u)
          | (reconstructing.load(std::memory_order_relaxed) ? 8192u : 0u),
      std::bit_cast<uint32_t>(range),
      std::clamp(ambient_rays.load(std::memory_order_relaxed), 1u, 16u),
      std::bit_cast<uint32_t>(reach),
      std::clamp(bounce_rays.load(std::memory_order_relaxed), 1u, 16u),
      std::bit_cast<uint32_t>(strength),
      std::bit_cast<uint32_t>(cone),
      pt ? std::clamp(path_bounces.load(std::memory_order_relaxed), 1u, 8u) : 0u,
      pt ? std::clamp(path_samples.load(std::memory_order_relaxed), 1u, 8u) : 1u,
      std::bit_cast<uint32_t>(internal::REFLECTION_RANGE),
      0u,
      0u,
      0u,
      0u,
      0u,
  };
  list->SetComputeRoot32BitConstants(internal::ROOT_SETTINGS, static_cast<UINT>(state.settings.size()), state.settings.data(), 0);
  // The radiance cache: last frame's screen into its cells, then each cell's mean (before the rays read it).
  if (history_valid) {
    list->SetPipelineState(gpu.cache_fill_pipeline.Get());
    list->Dispatch((gpu.width + 15) / 16, (gpu.height + 15) / 16, 1);
    list->ResourceBarrier(1, &uav_barrier);
  }
  if (uses_history && gpu.cache_keys) {
    list->SetPipelineState(gpu.cache_resolve_pipeline.Get());
    list->Dispatch(internal::CACHE_ENTRIES / 64, 1, 1);
    list->ResourceBarrier(1, &uav_barrier);
    if (history_valid) ++state.cache_fills;
  }
  list->SetPipelineState(gpu.pipeline.Get());
  list->Dispatch((gpu.width + 7) / 8, (gpu.height + 7) / 8, 1);
  if (bounce || soft) list->ResourceBarrier(1, &uav_barrier);
  if (bounce) {
    // The filter reads the accumulation the trace wrote.
    list->SetPipelineState(gpu.filter_pipeline.Get());
    list->Dispatch((gpu.width + 7) / 8, (gpu.height + 7) / 8, 1);
    state.bounce_frame = state.frame;
  }
  if (soft) {
    // Soft shadows: the rays' penumbras (0x7A1E0D1F).
    list->SetPipelineState(gpu.sun_pipeline.Get());
    list->Dispatch((gpu.width + 7) / 8, (gpu.height + 7) / 8, 1);
  }
  std::vector<D3D12_RESOURCE_BARRIER> to_common;
  for (const auto& [resource, used] : shared) {
    to_common.push_back(internal::Transition(resource, used, D3D12_RESOURCE_STATE_COMMON));
  }
  list->ResourceBarrier(static_cast<UINT>(to_common.size()), to_common.data());
  if (FAILED(list->Close())) return internal::Fail("recording the D3D12 work failed.");
  ID3D12CommandList* lists[] = {list};
  gpu.queue->Wait(gpu.fence12.Get(), inputs_ready);
  gpu.queue->ExecuteCommandLists(1, lists);
  gpu.queue->Signal(gpu.fence12.Get(), ++gpu.fence_value);
  gpu.allocator_values[slot] = gpu.fence_value;
  gpu.frame_fences[ring] = gpu.fence_value;
  compaction.fence_value = gpu.fence_value;
  ++gpu.submissions;

  // Meshes unseen for long give their BLAS (and micromaps, record, pool ranges) back once the GPU is past this frame;
  // so do characters gone for CHARACTER_RELEASE_FRAMES.
  uint64_t blas_bytes = 0;
  std::erase_if(gpu.meshes, [&](auto& entry) {
    auto& [key, mesh] = entry;
    if (mesh.drawn_frame + internal::RELEASE_FRAMES >= state.frame) {
      blas_bytes += mesh.blas->GetDesc().Width;
      return false;
    }
    gpu.releases.push_back({.fence_value = gpu.fence_value, .resource = std::move(mesh.blas)});
    if (mesh.micromaps) gpu.releases.push_back({.fence_value = gpu.fence_value, .resource = std::move(mesh.micromaps)});
    if (mesh.micromap_indices) gpu.releases.push_back({.fence_value = gpu.fence_value, .resource = std::move(mesh.micromap_indices)});
    gpu.pending_frees.push_back({.fence_value = gpu.fence_value, .record = mesh.record, .normal_offset = mesh.normal_offset,
                                 .triangles = mesh.triangles, .uv_offset = mesh.uv_offset});
    return true;
  });
  std::erase_if(gpu.characters, [&](auto& entry) {
    auto& [key, character] = entry;
    if (character.seen_frame + internal::CHARACTER_RELEASE_FRAMES >= state.frame) return false;
    if (character.blas) gpu.releases.push_back({.fence_value = gpu.fence_value, .resource = std::move(character.blas)});
    return true;
  });
  if (state.tracing) {
    state.trace += std::format(
        "sun shadow mask: {}x{}, {} captures ({} with a View buffer other than the mask's), {} BLAS built ({} triangles, {} "
        "alpha-tested, {} with opacity micromaps: {:.1f} MB), characters {} refitted / {} rebuilt ({} triangles), {:.1f} MB "
        "saved by compaction; scene {} instances, {} triangles, {} meshes kept ({:.1f} MB of BLAS), {} atlas slices, radiance "
        "cache filled {} frames; {}\n",
        gpu.width, gpu.height, captures.size(), other_views, built, built_triangles, masked_built, micromaps_built,
        static_cast<double>(micromap_bytes) / (1024. * 1024.), characters_refitted, characters_rebuilt, dynamic_triangles,
        static_cast<double>(compacted_bytes) / (1024. * 1024.), instance_count, scene_triangles, gpu.meshes.size(),
        static_cast<double>(blas_bytes) / (1024. * 1024.), gpu.atlas_slices.size(), state.cache_fills, state.status);
  }

  // The game's dispatch reads the visibility once D3D12 is done (the lighting pass the ambient occlusion, later).
  context4->Wait(gpu.fence11.Get(), gpu.fence_value);
  state.traced_frame = state.frame;
  ID3D11ShaderResourceView* visibility = gpu.visibility.srv.Get();
  context->CSSetShaderResources(VISIBILITY_SLOT, 1, &visibility);
  return true;
}

// What the lighting pass traced this frame for the HDR addon (null: off, not traced this frame, or failed).
struct LightingResult {
  ComPtr<ID3D11ShaderResourceView> bounce;              // path tracing's bounce light (t42)
  ComPtr<ID3D11ShaderResourceView> reflection;          // t43
  ComPtr<ID3D11ShaderResourceView> specular_occlusion;  // t44
};

// At the lighting pass (pre callback, the G-buffer bound: `gbuffer` its A, B, C at t1-t3): the reflections, specular
// occlusion and path tracing against the scene the sun shadow mask traced, from copies of the G-buffer shared with
// D3D12, for the HDR addon's lighting pass.
inline LightingResult TraceLighting(ID3D11DeviceContext* context, const std::array<ID3D11ShaderResourceView*, 3>& gbuffer) {
  using internal::state;
  internal::Gpu& gpu = state.gpu;
  if (!UsesLightingPass() || std::ranges::find(gbuffer, nullptr) != gbuffer.end()) return {};
  const bool pt = path_tracing.load(std::memory_order_relaxed);
  const bool specular = reflections.load(std::memory_order_relaxed) || specular_occlusion.load(std::memory_order_relaxed);
  std::array<ComPtr<ID3D11Texture2D>, 3> textures;
  std::array<D3D11_TEXTURE2D_DESC, 3> descs = {};
  for (size_t index = 0; index < gbuffer.size(); ++index) {
    ComPtr<ID3D11Resource> resource;
    gbuffer[index]->GetResource(resource.GetAddressOf());
    if (FAILED(resource.As(&textures[index]))) return {};
    textures[index]->GetDesc(&descs[index]);
    if (descs[index].SampleDesc.Count != 1 || descs[index].ArraySize != 1) return {};
  }
  ComPtr<ID3D11DeviceContext4> context4;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context4.GetAddressOf())))) return {};
  const std::unique_lock lock(state.mutex);
  const auto results = [&]() -> LightingResult {
    return {
        .bounce = (pt && state.bounce_frame == state.frame) ? gpu.bounce_light.srv : nullptr,
        .reflection = (reflections.load(std::memory_order_relaxed) && state.specular_frame == state.frame) ? gpu.reflection.srv : nullptr,
        .specular_occlusion = (specular_occlusion.load(std::memory_order_relaxed) && state.specular_frame == state.frame)
                                  ? gpu.specular_occlusion_texture.srv
                                  : nullptr,
    };
  };
  if (state.failed || state.traced_frame != state.frame || descs[0].Width != gpu.width || descs[0].Height != gpu.height
      || (UsesHistory() && !gpu.previous_positions.texture) || (pt && !gpu.bounce_light.texture)) {
    return {};
  }
  if (state.lighting_frame == state.frame) return results();
  // The G-buffer's copies shared with D3D12, and the reflections' textures (D3D12 idle when made: descriptors change).
  constexpr std::array<internal::Descriptor, 3> GBUFFER_DESCRIPTORS = {internal::NORMALS, internal::MATERIAL, internal::BASE_COLOR};
  for (size_t index = 0; index < gbuffer.size(); ++index) {
    auto& copy = gpu.gbuffer[index];
    if (D3D11_TEXTURE2D_DESC current = {}; copy.texture) {
      copy.texture->GetDesc(&current);
      if (current.Format != descs[index].Format || current.Width != descs[index].Width || current.Height != descs[index].Height
          || current.MipLevels != descs[index].MipLevels) {
        copy = {};
      }
    }
    if (copy.texture) continue;
    if (!internal::WaitFor(gpu.fence_value)) return {};
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    gbuffer[index]->GetDesc(&view_desc);
    const D3D11_TEXTURE2D_DESC shared_desc = {
        .Width = descs[index].Width,
        .Height = descs[index].Height,
        .MipLevels = descs[index].MipLevels,
        .ArraySize = 1,
        .Format = descs[index].Format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
    };
    if (FAILED(gpu.device->CreateTexture2D(&shared_desc, nullptr, copy.texture.GetAddressOf()))
        || FAILED(d3d12_device::Open(gpu.device12.Get(), copy.texture.Get(), &copy.resource))) {
      copy = {};
      state.status = "Lighting pass: the G-buffer could not be shared.";
      return {};
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC view = internal::TextureView(view_desc.Format);
    gpu.device12->CreateShaderResourceView(copy.resource.Get(), &view, internal::DescriptorAt(GBUFFER_DESCRIPTORS[index]));
  }
  if (specular && !gpu.reflection.texture && (!internal::WaitFor(gpu.fence_value) || !internal::CreateSpecularTextures())) {
    internal::Fail("the reflections' textures failed.");
    return {};
  }
  for (size_t index = 0; index < gbuffer.size(); ++index) {
    context->CopyResource(gpu.gbuffer[index].texture.Get(), textures[index].Get());
  }
  context4->Signal(gpu.fence11.Get(), ++gpu.fence_value);
  const uint64_t inputs_ready = gpu.fence_value;
  context->Flush();

  const uint32_t slot = gpu.submissions % internal::FRAMES_IN_FLIGHT;
  if (!internal::WaitFor(gpu.allocator_values[slot]) || FAILED(gpu.allocators[slot]->Reset())
      || FAILED(gpu.list->Reset(gpu.allocators[slot].Get(), nullptr))) {
    internal::Fail("the D3D12 queue stopped.");
    return {};
  }
  auto* list = gpu.list.Get();
  std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> shared = {
      {gpu.positions.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
      {gpu.sun.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
      {gpu.previous_sun.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
      {gpu.visibility.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
  };
  if (gpu.previous_positions.resource) {
    shared.emplace_back(gpu.previous_positions.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  for (const auto& copy : gpu.gbuffer) {
    shared.emplace_back(copy.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  if (gpu.previous_color.resource) shared.emplace_back(gpu.previous_color.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  if (gpu.atlas12) shared.emplace_back(gpu.atlas12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  if (pt) shared.emplace_back(gpu.bounce_light.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (specular) {
    shared.emplace_back(gpu.reflection.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    shared.emplace_back(gpu.specular_occlusion_texture.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  for (const auto& [shared_resource, used] : shared) {
    barriers.push_back(internal::Transition(shared_resource, D3D12_RESOURCE_STATE_COMMON, used));
  }
  list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
  ID3D12DescriptorHeap* heaps[] = {gpu.descriptors.Get()};
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootSignature(gpu.root_signature.Get());
  list->SetComputeRootShaderResourceView(internal::ROOT_SCENE, gpu.tlas->GetGPUVirtualAddress());
  list->SetComputeRootDescriptorTable(internal::ROOT_TABLE, gpu.descriptors->GetGPUDescriptorHandleForHeapStart());
  list->SetComputeRootShaderResourceView(internal::ROOT_CAPTURE_TABLE, gpu.table_uploads[slot]->GetGPUVirtualAddress());
  for (const internal::Root root : {internal::ROOT_MICROMAP_DATA, internal::ROOT_MICROMAP_INDICES, internal::ROOT_MICROMAP_DESCS}) {
    list->SetComputeRootUnorderedAccessView(root, gpu.tlas_scratch->GetGPUVirtualAddress());
  }
  std::array<uint32_t, 16> settings = state.settings;
  if (pt && state.bounce_frame + 1 == state.frame) settings[1] |= 64u;  // BOUNCE_HISTORY
  if (specular && state.specular_frame + 1 == state.frame && !reconstructing.load(std::memory_order_relaxed)) {
    settings[1] |= 1024u;  // SPECULAR_HISTORY
  }
  list->SetComputeRoot32BitConstants(internal::ROOT_SETTINGS, static_cast<UINT>(settings.size()), settings.data(), 0);
  const auto uav_barrier = internal::UavBarrier();
  // Learning (base colours into the cache, the learned lighting's sums), then the paths (SER on DXR 1.2).
  list->SetPipelineState(gpu.learn_pipeline.Get());
  list->Dispatch((gpu.width / 2 + 7) / 8, (gpu.height / 2 + 7) / 8, 1);
  list->ResourceBarrier(1, &uav_barrier);
  if (gpu.dxr12) {
    list->SetPipelineState1(gpu.path_state_object.Get());
    const D3D12_DISPATCH_RAYS_DESC rays = {
        .RayGenerationShaderRecord = {.StartAddress = gpu.path_shader_table->GetGPUVirtualAddress(),
                                      .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
        .Width = gpu.width,
        .Height = gpu.height,
        .Depth = 1,
    };
    list->DispatchRays(&rays);
  } else {
    list->SetPipelineState(gpu.path_pipeline.Get());
    list->Dispatch((gpu.width + 7) / 8, (gpu.height + 7) / 8, 1);
  }
  list->ResourceBarrier(1, &uav_barrier);
  if (pt) {
    list->SetPipelineState(gpu.filter_pipeline.Get());
    list->Dispatch((gpu.width + 7) / 8, (gpu.height + 7) / 8, 1);
  }
  if (specular) {
    list->SetPipelineState(gpu.specular_pipeline.Get());
    list->Dispatch((gpu.width + 7) / 8, (gpu.height + 7) / 8, 1);
  }
  barriers.clear();
  for (const auto& [shared_resource, used] : shared) {
    barriers.push_back(internal::Transition(shared_resource, used, D3D12_RESOURCE_STATE_COMMON));
  }
  list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
  if (FAILED(list->Close())) {
    internal::Fail("recording the D3D12 work failed.");
    return {};
  }
  ID3D12CommandList* lists[] = {list};
  gpu.queue->Wait(gpu.fence12.Get(), inputs_ready);
  gpu.queue->ExecuteCommandLists(1, lists);
  gpu.queue->Signal(gpu.fence12.Get(), ++gpu.fence_value);
  gpu.allocator_values[slot] = gpu.fence_value;
  gpu.frame_fences[state.frame % internal::FRAMES_IN_FLIGHT] = gpu.fence_value;
  ++gpu.submissions;
  context4->Wait(gpu.fence11.Get(), gpu.fence_value);
  state.lighting_frame = state.frame;
  if (pt) state.bounce_frame = state.frame;
  if (specular) state.specular_frame = state.frame;
  return results();
}

// This frame's ray-traced ambient occlusion for the lighting pass (t41), or null (off, or not traced this frame).
inline ComPtr<ID3D11ShaderResourceView> AmbientOcclusion() {
  using internal::state;
  if (!ambient_occlusion.load(std::memory_order_relaxed)) return nullptr;
  const std::shared_lock lock(state.mutex);
  if (state.traced_frame != state.frame) return nullptr;
  return state.gpu.ambient.srv;
}

// This frame's ray-traced bounce light (not path traced) for the lighting pass (t42), or null (off, or not traced this
// frame).
inline ComPtr<ID3D11ShaderResourceView> BounceLight() {
  using internal::state;
  if (!bounce_light.load(std::memory_order_relaxed) || path_tracing.load(std::memory_order_relaxed)) return nullptr;
  const std::shared_lock lock(state.mutex);
  if (state.bounce_frame != state.frame || state.traced_frame != state.frame) return nullptr;
  return state.gpu.bounce_light.srv;
}

// The scene colour (the TAA's input, after the lighting) for the next frame's bounce light, reflections and path
// tracing: where their rays hit, the light is this colour (and the radiance cache keeps it by world cell).
inline void CaptureSceneColor(ID3D11DeviceContext* context, ID3D11Texture2D* color) {
  using internal::state;
  internal::Gpu& gpu = state.gpu;
  if (!UsesHistory() || color == nullptr) return;
  D3D11_TEXTURE2D_DESC desc = {};
  color->GetDesc(&desc);
  const std::unique_lock lock(state.mutex);
  if (!gpu.device12 || !gpu.previous_positions.texture || desc.MipLevels != 1 || desc.ArraySize != 1 || desc.SampleDesc.Count != 1) return;
  if (D3D11_TEXTURE2D_DESC current = {}; gpu.previous_color.texture) {
    gpu.previous_color.texture->GetDesc(&current);
    if (current.Width != desc.Width || current.Height != desc.Height || current.Format != desc.Format) {
      gpu.previous_color = {};
    }
  }
  if (!gpu.previous_color.texture) {
    // D3D12 idle: the descriptor changes.
    if (!internal::WaitFor(gpu.fence_value)) return;
    const D3D11_TEXTURE2D_DESC shared_desc = {
        .Width = desc.Width,
        .Height = desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = desc.Format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
    };
    if (FAILED(gpu.device->CreateTexture2D(&shared_desc, nullptr, gpu.previous_color.texture.GetAddressOf()))
        || FAILED(d3d12_device::Open(gpu.device12.Get(), gpu.previous_color.texture.Get(), &gpu.previous_color.resource))) {
      gpu.previous_color = {};
      state.status = "Bounce light / reflections: the scene colour could not be shared.";
      return;
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC color_view = internal::TextureView(desc.Format);
    gpu.device12->CreateShaderResourceView(gpu.previous_color.resource.Get(), &color_view, internal::DescriptorAt(internal::PREVIOUS_COLOR));
  }
  context->CopyResource(gpu.previous_color.texture.Get(), color);
  state.color_frame = state.frame;
}

// After the sun shadow mask: the visibility unbound (D3D12 writes it next frame).
inline void Unbind(ID3D11DeviceContext* context) {
  ID3D11ShaderResourceView* none = nullptr;
  context->CSSetShaderResources(VISIBILITY_SLOT, 1, &none);
}

// One frame of capture decisions and scene sizes, written to tales_rt_trace.txt next to the exe.
inline void RequestTrace() {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.trace_requested = true;
}

// Each presented frame: captures no sun shadow mask used are dropped; a requested trace starts or is written.
inline void OnPresent(ID3D11DeviceContext* context) {
  using internal::state;
  internal::Gpu& gpu = state.gpu;
  const std::unique_lock lock(state.mutex);
  if (state.status != state.logged_status) {
    state.logged_status = state.status;
    renodx::utils::log::i("[Tales ray tracing] ", state.status);
  }
  ++state.frame;
  state.captures.clear();
  state.captured_vertices = 0;
  state.captured_bytes = 0;
  state.dynamic_vertices = 0;
  state.occurrences.clear();
  // All effects off for 10 s: their memory freed.
  if (Active()) {
    state.active_frame = state.frame;
  } else if (gpu.device12 && state.frame > state.active_frame + 600) {
    internal::ReleaseGpu();
  }
  if (state.tracing) {
    if (state.sun_read && context != nullptr) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (SUCCEEDED(context->Map(gpu.sun_readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        const auto* sun = static_cast<const float*>(mapped.pData);
        state.trace += std::format("toward the sun {:.3f} {:.3f} {:.3f} (valid {:.0f}), camera {:.0f} {:.0f} {:.0f}\n",
                                   sun[0], sun[1], sun[2], sun[3], sun[4], sun[5], sun[6]);
        context->Unmap(gpu.sun_readback.Get(), 0);
      }
    }
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    const auto path = std::filesystem::path(exe).parent_path() / L"tales_rt_trace.txt";
    std::ofstream(path, std::ios::binary) << state.trace;
    state.status = std::format("Trace saved to {}.", path.filename().string());
    state.tracing = false;
    state.sun_read = false;
    state.trace.clear();
  }
  if (state.trace_requested) {
    state.trace_requested = false;
    state.tracing = true;
    state.trace = std::format("Tales ray tracing, one frame (sun shadows {}, status: {})\n", enabled.load() ? "on" : "off", state.status);
  }
}

inline std::string Status() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.status;
}

// Whether the ray-traced reflections ran within the last half second (the game's screen-space reflections are left off
// meanwhile: the lighting pass reads the rays' instead).
inline bool ReflectingRecently() {
  constexpr uint64_t RECENT_FRAMES = 30;
  const std::shared_lock lock(internal::state.mutex);
  return reflections.load(std::memory_order_relaxed) && !internal::state.failed && internal::state.specular_frame != UINT64_MAX
         && internal::state.specular_frame + RECENT_FRAMES >= internal::state.frame;
}

// Turned on again after a failure: try again.
inline void Retry() {
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.failed) {
    internal::state.failed = false;
    internal::state.status = "Off.";
  }
}

// The game's device goes away: D3D12 finishes, everything is released.
inline void Release() {
  const std::unique_lock lock(internal::state.mutex);
  internal::ReleaseGpu();
}

}  // namespace talesofarise::rt_shadows
