/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/pipeline.hpp"
#include "./dxbc.hpp"
#include "./frame_history.hpp"

// Object motion (DLSS's motion vectors of what moves on its own): swaying grass, leaves and branches, units, moving
// rigid objects, deformed meshes and the ocean. Each of their draws is drawn again with its motion vertex shader
// (tools\tw\attila_motion\make_wind_motion_vs.py: the game's code at this frame's and last frame's time for the wind, at
// this frame's and last frame's placement for units and rigid objects, which a placement table carries from one frame
// to the next: motion.hlsli) into the motion target (motion, depth, written), which the host uses where its depth is the
// final one (Attila: DLAA's preparation, Shogun 2: merged into the game's motion vectors).
// Rigid objects (buildings, rocks, ships, siege engines, projectiles: mostly standing) are recorded (their instances'
// rows through stream output; motion_rigid.cs matches them at the frame's end: Resolve) two frames when first seen and
// every RIGID_SAMPLE_PERIOD frames, and while moving, which their records' flags (read back) tell; only moving ones are
// drawn again. The host gives each draw its view (MakeView: the frame, the main depth's size, the cameras).
// In-draw (a shader with its motion_draw_*.vs, Attila): the game's own draw writes the motion instead, with that vertex
// shader (the game's outputs and both frames' clip positions) and the game's pixel shader patched to write them
// (dxbc.hpp AddMotionTargets) into targets 6 (motion) and 7 (depth): nothing drawn again, the motion exactly where the
// game's pixels are. The game's pixel shaders that can follow such a vertex shader are kept as the game creates them;
// draws whose pixel shader cannot be patched are drawn again.
namespace twattila::object_motion {

using Microsoft::WRL::ComPtr;

enum class MotionKind : uint8_t {
  WIND = 0,      // swaying: its wind at camera_VS_PS's time_in_sec
  UNIT = 1,      // skinned: its instance's world rows in TEXCOORD0-2
  RIGID = 2,     // its instance's world rows in POSITION1-3
  DEFORMED = 3,  // cloth, sails, ropes: vertices the CPU moves each frame
  HISTORY = 4,   // the ocean: displaced by textures the game remakes each frame
};

// A game's vertex shader whose draws move on their own and its motion version; rigid: and its record version
// (motion_record_*.vs: its instances' rows for stream output), deformed: its capture version (motion_capture_*.vs);
// history: the game's vertex shader texture slots (bits) whose last frame's copies the motion shader reads (at
// HISTORY_SRV_SLOT + slot); its in-draw version (motion_draw_*.vs), if any.
struct MotionShader {
  uint32_t hash = 0;
  std::span<const uint8_t> motion_shader;
  MotionKind kind = MotionKind::WIND;
  std::span<const uint8_t> record_shader;
  uint8_t history_textures = 0;
  std::span<const uint8_t> draw_shader;
};

// The game's moving shaders, the motion pixel shaders (motion_wind.ps, motion_unit.ps, motion_capture.ps) and the rigid
// objects' matching (motion_rigid.cs).
struct Shaders {
  std::span<const MotionShader> shaders;
  std::span<const uint8_t> wind_pixel_shader;
  std::span<const uint8_t> unit_pixel_shader;
  // With it (motion_unit_records.cs) each unit draw streams its soldiers out (a point per instance: their
  // MotionShader::record_shader), recorded at the frame's end (Resolve), and their redraw (unit_pixel_shader without
  // records: motion_unit_recorded.ps) skips hidden pixels early.
  std::span<const uint8_t> unit_records_compute_shader;
  std::span<const uint8_t> capture_pixel_shader;
  std::span<const uint8_t> rigid_compute_shader;
};

// motion.hlsli's motion_VS (b12): the cameras' view-projection rows, last frame's time, this frame's, the render size;
// the draw's mesh key, whether last frame's placements were recorded, its animation texture's key (units) or slot
// (rigid, deformed); deformed: its first position, positions and first vertex; the cameras' jitter in clip space
// (this frame's xy, last frame's zw) when their rows have it, else 0.
struct MotionConstants {
  std::array<float, 16> current = {};
  std::array<float, 16> previous = {};
  std::array<float, 4> times = {};
  std::array<uint32_t, 4> unit = {};
  std::array<uint32_t, 4> draw = {};
  std::array<float, 4> jitter = {};
  std::array<float, 4> game_jitter = {};
};

// A draw's view (the host's): the frame, the main view's depth size (and its depth texture when known: only draws
// against it, compared only), the motion constants' cameras, times and jitter (the rest the draw's), and parts of the
// draw's camera constants (its vertex shader's b0) copied over them on the GPU (Shogun 2: its camera has last frame's).
struct View {
  uint64_t frame = 0;
  UINT width = 0;
  UINT height = 0;
  ID3D11Resource* depth = nullptr;
  MotionConstants constants = {};
  std::span<const frame_history::CameraRun> camera_copies = {};
};

// The host's view for a draw with the camera constants `camera`; false: no motion for it.
using MakeView = bool (*)(ID3D11Buffer* camera, View* view);

// A draw's mesh: its index buffer and range (a soldier's body, shield, horse... each its own).
template <typename Arguments>
uint32_t MeshKey(ID3D11DeviceContext* native, const Arguments& arguments) {
  ComPtr<ID3D11Buffer> index_buffer;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  native->IAGetIndexBuffer(index_buffer.GetAddressOf(), &index_format, &index_offset);
  uint64_t hashed = reinterpret_cast<uintptr_t>(index_buffer.Get()) ^ (uint64_t{index_offset} << 32);
  uint32_t vertex_count = 0;
  if constexpr (std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    hashed = hashed * 1099511628211ull ^ arguments.first_index;
    hashed = hashed * 1099511628211ull ^ static_cast<uint32_t>(arguments.vertex_offset);
    vertex_count = arguments.index_count;
  } else {
    vertex_count = arguments.vertex_count;
  }
  hashed = hashed * 1099511628211ull ^ vertex_count;
  return static_cast<uint32_t>(hashed ^ (hashed >> 32));
}

namespace internal {

constexpr UINT MOTION_SLOT = 12;
// motion.hlsli's placement table: cells (a power of two) x slots, each slot a key and four rows; and its bone pool
// (BONE_SOLDIERS x BONE_TEXELS), found by bone key (BONE_SLOTS keys, then the pool's count; and their entries). Last
// frame's bone keys, entries, pool, placement keys and rows at t11-t15 for the units' motion vertex shaders, this frame's
// at u4, u5, u3, u1, u2 for motion_unit.ps, which reads the draw's animation texture at t15.
constexpr UINT PLACEMENT_CELLS = 1u << 16;
constexpr UINT PLACEMENT_SLOTS = 8;
constexpr UINT BONE_TEXELS = 288;
constexpr UINT BONE_SOLDIERS = 16384;
constexpr UINT BONE_SLOTS = 32768;
constexpr UINT PLACEMENT_SRV_SLOT = 11;
constexpr UINT ANIMATION_SRV_SLOT = 15;
// Rigid objects: a frame's records (an instance's rows and (mesh key, draw slot), 64 bytes) and draws (each a slot of
// flags: motion_rigid.cs), how often a standing mesh is sampled, how long a mesh seen moving is kept moving, the flags'
// copies read back in turn, the meshes remembered.
constexpr UINT RIGID_RECORDS = 1u << 16;
constexpr UINT RIGID_RECORD_BYTES = 64;
constexpr UINT RIGID_DRAWS = 8192;
constexpr uint64_t RIGID_SAMPLE_PERIOD = 32;
constexpr uint64_t RIGID_MOVING_FRAMES = 600;
constexpr size_t RIGID_READBACKS = 3;
constexpr size_t RIGID_MESHES = 1u << 16;
// Deformed meshes: a frame's positions (motion.hlsli: last frame's at t9, this frame's anchors at t10) and draws.
constexpr UINT DEFORMED_POSITIONS = 1u << 20;
constexpr UINT DEFORMED_DRAWS = 4096;
constexpr UINT DEFORMED_SRV_SLOT = 9;
// The ocean: last frame's displacement textures at t16 + their slot.
constexpr UINT HISTORY_SRV_SLOT = 16;
constexpr UINT HISTORY_SLOTS = 8;
// The motion draws' vertex shader views (t9-t23), kept for the game.
constexpr UINT MOTION_VIEWS = HISTORY_SRV_SLOT + HISTORY_SLOTS - DEFORMED_SRV_SLOT;
// Units (motion_unit_records.cs): a frame's records (an instance's rows, then its bone base, the animation texture's
// width, the draw's mesh and animation texture keys: 64 bytes), streamed out by motion_unit_point.vs (TEXCOORD2-5).
constexpr UINT UNIT_RECORDS = 1u << 16;
constexpr UINT UNIT_RECORD_BYTES = 64;
constexpr std::array<D3D11_SO_DECLARATION_ENTRY, 4> UNIT_RECORD_OUTPUT = {{
    {.SemanticName = "TEXCOORD", .SemanticIndex = 2, .ComponentCount = 4},
    {.SemanticName = "TEXCOORD", .SemanticIndex = 3, .ComponentCount = 4},
    {.SemanticName = "TEXCOORD", .SemanticIndex = 4, .ComponentCount = 4},
    {.SemanticName = "TEXCOORD", .SemanticIndex = 5, .ComponentCount = 4},
}};
// The stream output of motion_record_*.vs: TEXCOORD0-3.
constexpr std::array<D3D11_SO_DECLARATION_ENTRY, 4> RIGID_RECORD_OUTPUT = {{
    {.SemanticName = "TEXCOORD", .SemanticIndex = 0, .ComponentCount = 4},
    {.SemanticName = "TEXCOORD", .SemanticIndex = 1, .ComponentCount = 4},
    {.SemanticName = "TEXCOORD", .SemanticIndex = 2, .ComponentCount = 4},
    {.SemanticName = "TEXCOORD", .SemanticIndex = 3, .ComponentCount = 4},
}};

// A unit draw's records this frame (motion_unit_records.cs at the frame's end) and the animation texture they read.
struct UnitRecordDraw {
  UINT first = 0;
  UINT count = 0;
  ComPtr<ID3D11ShaderResourceView> animation_map;
};

// A rigid mesh: recorded until `sample_until` (first seen), recorded and drawn again until `moving_until`.
struct RigidMesh {
  uint64_t sample_until = 0;
  uint64_t moving_until = 0;
};

// A texture the game remakes each frame (the ocean's displacement), copied at its first motion draw each frame, in turn.
struct TextureHistory {
  std::array<ComPtr<ID3D11Texture2D>, 2> copies;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> views;
  uint64_t frame = UINT64_MAX;  // the frame copies[frame & 1] is from
  bool continued = false;       // and the other is from the frame before
};

// A frame's draw flags copied for the CPU, and its draws' meshes (none: read).
struct RigidReadback {
  ComPtr<ID3D11Buffer> flags;
  std::vector<uint32_t> meshes;
  uint64_t frame = 0;
};

// An in-draw vertex shader's outputs (the game's; MOTION0 / MOTION1 at `motion_register` and the next): the game's
// pixel shaders that can follow it read only these.
struct DrawOutput {
  std::string name;
  uint32_t index = 0;
  uint32_t reg = 0;
  uint8_t mask = 0;
};
struct DrawSignature {
  std::vector<DrawOutput> outputs;
  uint32_t motion_register = 0;  // 0: no in-draw version
};

// A game pixel shader that can follow an in-draw vertex shader: its code and its patched versions by MOTION register
// (null: not patchable there).
struct GamePixelShader {
  std::vector<uint8_t> code;
  std::vector<std::pair<uint32_t, ComPtr<ID3D11PixelShader>>> patched;
};

// The game's bindings an in-draw motion draw changes, put back after it (FinishInDraw).
struct InDraw {
  bool pending = false;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
  ComPtr<ID3D11DepthStencilView> depth_stencil;
  ComPtr<ID3D11BlendState> blend;
  std::array<float, 4> blend_factor = {};
  UINT sample_mask = 0;
  ComPtr<ID3D11Buffer> vs_constants;
  bool views_changed = false;  // t9-t23 (all but the wind)
  std::array<ComPtr<ID3D11ShaderResourceView>, MOTION_VIEWS> motion_views;
};

// The device's motion objects: the shaders, their constants, the target (the main depth's size; R32G32_UINT: the motion
// as two halves and the depth, motion.hlsli MotionTexel), the depth-stencil states (the game's comparison, no writes) and rasterizer states (the game's, biased toward
// the camera) it draws with; the units' placement tables (this frame's and last frame's, in turn).
struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  bool made = false;
  std::vector<ComPtr<ID3D11VertexShader>> motion_shaders;  // as Shaders::shaders
  ComPtr<ID3D11PixelShader> wind_pixel_shader;
  ComPtr<ID3D11PixelShader> unit_pixel_shader;
  ComPtr<ID3D11Buffer> motion_constants;
  ComPtr<ID3D11Texture2D> motion;
  ComPtr<ID3D11RenderTargetView> motion_rtv;
  ComPtr<ID3D11ShaderResourceView> motion_srv;
  std::array<ComPtr<ID3D11DepthStencilState>, 9> motion_depth_states;  // by D3D11_COMPARISON_FUNC
  std::vector<std::pair<D3D11_RASTERIZER_DESC, ComPtr<ID3D11RasterizerState>>> motion_rasterizer_states;
  uint64_t motion_frame = UINT64_MAX;  // the frame the target was cleared and written in
  MotionConstants motion_uploaded = {};
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> placement_key_srvs;
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> placement_key_uavs;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> placement_srvs;
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> placement_uavs;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> bone_srvs;
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> bone_uavs;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> bone_key_srvs;
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> bone_key_uavs;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> bone_entry_srvs;
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> bone_entry_uavs;
  // The animation textures the units' draws read (their position + 1: the bone pool's key with the bone base). Compared
  // only.
  std::vector<ID3D11Resource*> animation_textures;
  size_t placement_index = 0;            // this frame's table
  uint64_t placement_frame = UINT64_MAX;  // the frame it was cleared in
  bool previous_placements = false;      // last frame's table was written last frame
  // Rigid objects: the record shaders (as Shaders::shaders; the vertex shader and its stream output), the matching, its
  // constants, this frame's records and draws (their meshes), their flags and copies, the meshes seen.
  std::vector<ComPtr<ID3D11VertexShader>> record_shaders;
  std::vector<ComPtr<ID3D11GeometryShader>> record_outputs;
  ComPtr<ID3D11ComputeShader> rigid_compute;
  ComPtr<ID3D11Buffer> rigid_constants;
  ComPtr<ID3D11Buffer> rigid_records;
  ComPtr<ID3D11ShaderResourceView> rigid_records_srv;
  ComPtr<ID3D11UnorderedAccessView> rigid_flags_uav;
  std::array<RigidReadback, RIGID_READBACKS> rigid_readbacks;
  UINT rigid_record_count = 0;
  std::vector<uint32_t> rigid_draws;
  std::unordered_map<uint32_t, RigidMesh> rigid_meshes;
  // Deformed meshes: the capture's pixel shader and states (no depth, every point drawn), the positions (this frame's
  // and last frame's, with the tables), this frame's anchors, positions and draws.
  ComPtr<ID3D11PixelShader> capture_pixel_shader;
  ComPtr<ID3D11DepthStencilState> capture_depth_state;
  ComPtr<ID3D11RasterizerState> capture_rasterizer;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> deformed_position_srvs;
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> deformed_position_uavs;
  ComPtr<ID3D11ShaderResourceView> anchor_srv;
  ComPtr<ID3D11UnorderedAccessView> anchor_uav;
  UINT deformed_position_count = 0;
  UINT deformed_draw_count = 0;
  // The ocean: its displacement textures by slot.
  std::array<TextureHistory, HISTORY_SLOTS> texture_histories;
  // Units' streamed records (Shaders::unit_records_compute_shader): the recording, its range, this frame's records and
  // the draws they came from.
  ComPtr<ID3D11ComputeShader> unit_records_compute;
  ComPtr<ID3D11Buffer> unit_record_constants;
  ComPtr<ID3D11Buffer> unit_records;
  ComPtr<ID3D11ShaderResourceView> unit_records_srv;
  UINT unit_record_count = 0;
  std::vector<UnitRecordDraw> unit_record_draws;
  // In-draw: the vertex shaders (as Shaders::shaders; null: none), the targets (R16G16_FLOAT: the motion in render
  // pixels; R32_FLOAT: the depth, 0 not written; the motion target's size, cleared with it) and the blend states (the
  // game's, targets 6 and 7 without blending: by the game's state, kept so no other takes its address).
  std::vector<ComPtr<ID3D11VertexShader>> draw_shaders;
  ComPtr<ID3D11Texture2D> drawn_motion;
  ComPtr<ID3D11RenderTargetView> drawn_motion_rtv;
  ComPtr<ID3D11ShaderResourceView> drawn_motion_srv;
  ComPtr<ID3D11Texture2D> drawn_depth;
  ComPtr<ID3D11RenderTargetView> drawn_depth_rtv;
  ComPtr<ID3D11ShaderResourceView> drawn_depth_srv;
  std::vector<std::pair<ComPtr<ID3D11BlendState>, ComPtr<ID3D11BlendState>>> drawn_blend_states;
};

// NVIDIA's UAV overlap (nvapi's NvAPI_D3D11_BeginUAVOverlapEx without a wait for idle, NvAPI_D3D11_EndUAVOverlap),
// loaded at the first record draw; null: unavailable.
struct UavOverlap {
  bool loaded = false;
  int(__cdecl* begin)(IUnknown* context, uint32_t wait_flags) = nullptr;
  int(__cdecl* end)(IUnknown* context) = nullptr;
};

struct State {
  std::shared_mutex mutex;
  Shaders shaders;              // the addon's (Install), kept while installed
  MakeView make_view = nullptr;  // the host's
  uint64_t frame = 0;           // the last view's
  DeviceObjects objects;
  // In-draw: the vertex shaders' outputs (as Shaders::shaders), the game's pixel shaders that can follow one (by their
  // pipeline handle: the shader), the draw in progress.
  std::vector<DrawSignature> draw_signatures;
  std::unordered_map<uint64_t, GamePixelShader> pixel_shaders;
  InDraw in_draw;
  UavOverlap uav_overlap;
};

static State state;
// The host draws motion this frame (else the moving draws return at once).
static std::atomic_bool active = false;
// Motion Test (debug, SetTest): 0 normal, 1 no records or captures, 2 units only, 3 plants only.
static std::atomic_uint32_t test = 0;
// Debug (temporary): the moving draws' CPU time a frame (before and after them) and their counts, logged every 1000
// frames. The render thread's alone.
struct MotionTiming {
  std::chrono::steady_clock::duration spent = {};
  uint64_t draws = 0;
  uint64_t drawn = 0;
  uint64_t frame = 0;
  uint64_t frames = 0;
};
static MotionTiming timing;

// The device's shaders and constants; false: not available.
inline bool PrepareDevice(ID3D11Device* device) {
  auto& objects = state.objects;
  if (objects.device.Get() == device) return objects.made;
  objects = {};
  objects.device = device;
  const auto pixel = [&](std::span<const uint8_t> code, ComPtr<ID3D11PixelShader>* shader) {
    return code.empty() || SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
  };
  const D3D11_BUFFER_DESC motion_desc = {.ByteWidth = sizeof(MotionConstants), .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC rigid_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  // A motion shader, its record or capture shader (rigid: with its stream output) and its in-draw version.
  const auto motion = [&](const MotionShader& shader) {
    auto& record_shader = objects.record_shaders.emplace_back();
    auto& record_output = objects.record_outputs.emplace_back();
    auto& draw_shader = objects.draw_shaders.emplace_back();
    constexpr UINT STRIDE = RIGID_RECORD_BYTES;
    constexpr UINT UNIT_STRIDE = UNIT_RECORD_BYTES;
    return SUCCEEDED(device->CreateVertexShader(shader.motion_shader.data(), shader.motion_shader.size(), nullptr,
                                                objects.motion_shaders.emplace_back().GetAddressOf()))
           && (shader.record_shader.empty()
               || SUCCEEDED(device->CreateVertexShader(shader.record_shader.data(), shader.record_shader.size(), nullptr,
                                                       record_shader.GetAddressOf())))
           && (shader.draw_shader.empty()
               || SUCCEEDED(device->CreateVertexShader(shader.draw_shader.data(), shader.draw_shader.size(), nullptr,
                                                       draw_shader.GetAddressOf())))
           && (shader.kind != MotionKind::RIGID
               || SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(
                   shader.record_shader.data(), shader.record_shader.size(), RIGID_RECORD_OUTPUT.data(),
                   static_cast<UINT>(RIGID_RECORD_OUTPUT.size()), &STRIDE, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                   record_output.GetAddressOf())))
           && (shader.kind != MotionKind::UNIT || shader.record_shader.empty()
               || SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(
                   shader.record_shader.data(), shader.record_shader.size(), UNIT_RECORD_OUTPUT.data(),
                   static_cast<UINT>(UNIT_RECORD_OUTPUT.size()), &UNIT_STRIDE, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                   record_output.GetAddressOf())));
  };
  // Deformed meshes' capture: no depth, every point drawn.
  const D3D11_DEPTH_STENCIL_DESC capture_depth_desc = {.DepthEnable = FALSE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
                                                       .DepthFunc = D3D11_COMPARISON_ALWAYS};
  const D3D11_RASTERIZER_DESC capture_rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE};
  const auto& shaders = state.shaders;
  objects.made = std::ranges::all_of(shaders.shaders, motion)
                 && pixel(shaders.wind_pixel_shader, &objects.wind_pixel_shader)
                 && pixel(shaders.unit_pixel_shader, &objects.unit_pixel_shader)
                 && (shaders.unit_records_compute_shader.empty()
                     || (SUCCEEDED(device->CreateComputeShader(shaders.unit_records_compute_shader.data(),
                                                               shaders.unit_records_compute_shader.size(), nullptr,
                                                               objects.unit_records_compute.GetAddressOf()))
                         && SUCCEEDED(device->CreateBuffer(&rigid_desc, nullptr, objects.unit_record_constants.GetAddressOf()))))
                 && (shaders.capture_pixel_shader.empty()
                     || (pixel(shaders.capture_pixel_shader, &objects.capture_pixel_shader)
                         && SUCCEEDED(device->CreateDepthStencilState(&capture_depth_desc, objects.capture_depth_state.GetAddressOf()))
                         && SUCCEEDED(device->CreateRasterizerState(&capture_rasterizer_desc, objects.capture_rasterizer.GetAddressOf()))))
                 && (shaders.rigid_compute_shader.empty()
                     || (SUCCEEDED(device->CreateComputeShader(shaders.rigid_compute_shader.data(), shaders.rigid_compute_shader.size(),
                                                               nullptr, objects.rigid_compute.GetAddressOf()))
                         && SUCCEEDED(device->CreateBuffer(&rigid_desc, nullptr, objects.rigid_constants.GetAddressOf()))))
                 && SUCCEEDED(device->CreateBuffer(&motion_desc, nullptr, objects.motion_constants.GetAddressOf()));
  if (!objects.made) {
    renodx::utils::log::w("[Total War] Object motion: could not create its shaders.");
    objects = {};
    objects.device = device;
  }
  return objects.made;
}

// A moving draw of the main view (the host's view). `in_draw` (before the draw: true when it then writes the motion
// itself, its bindings set, FinishInDraw putting the game's back after it; false: nothing done yet, drawn again after
// it): the draw's in-draw vertex shader and its pixel shader patched, targets 6 and 7 for the motion. Else (after the
// draw) the same draw again with its motion vertex shader into the motion target, against the game's depth (not
// written), every other binding the game's. Units, rigid objects and deformed meshes with last frame's placement
// table; units record theirs for the next frame (streamed out, recorded at the frame's end where the addon has
// Shaders::unit_records_compute_shader; else from every pixel of the redraw). Rigid objects: their instances recorded
// through stream output while sampled or moving, drawn again only while moving. Deformed meshes: captured first (their
// vertices' world positions and anchor), then drawn with last frame's. The ocean: its displacement textures copied
// (last frame's for its motion shader).
template <typename Context>
bool DrawObjectMotion(Context& command, bool in_draw) {
  const auto& shader = command.template GetCallbackData<MotionShader>();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
  ComPtr<ID3D11Buffer> camera;
  native->VSGetConstantBuffers(0, 1, camera.GetAddressOf());
  MakeView make_view = nullptr;
  {
    const std::shared_lock lock(state.mutex);
    make_view = state.make_view;
  }
  View view;
  if (!camera || make_view == nullptr || !make_view(camera.Get(), &view)) return false;
  const std::unique_lock lock(state.mutex);
  auto& objects = state.objects;
  if (&shader < state.shaders.shaders.data() || &shader >= state.shaders.shaders.data() + state.shaders.shaders.size()) return false;
  const size_t index = static_cast<size_t>(&shader - state.shaders.shaders.data());
  state.frame = view.frame;
  if (timing.frame != view.frame) {
    timing.frame = view.frame;
    if (++timing.frames == 1000) {
      renodx::utils::log::i(std::format(
          "[Total War] Object motion (debug): {:.0f} moving draws a frame ({:.0f} in-draw), {:.0f} us of CPU a frame.",
          timing.draws / 1000.0, timing.drawn / 1000.0,
          std::chrono::duration<double, std::micro>(timing.spent).count() / 1000.0));
      timing = {.frame = view.frame};
    }
  }
  const uint64_t frame = view.frame;
  constexpr bool INDEXED = std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DrawIndexedArguments>;
  const auto& arguments = command.arguments;
  UINT vertex_count = 0;
  UINT first_vertex = 0;
  if constexpr (INDEXED) {
    vertex_count = arguments.index_count;
  } else {
    vertex_count = arguments.vertex_count;
    first_vertex = arguments.first_vertex;
  }
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!PrepareDevice(device.Get())) return false;
  if (in_draw && !objects.draw_shaders[index]) return false;
  const bool tabled = (shader.kind == MotionKind::UNIT || shader.kind == MotionKind::RIGID || shader.kind == MotionKind::DEFORMED);
  // Units, rigid objects and deformed meshes: the mesh.
  const uint32_t mesh_key = (tabled ? MeshKey(native, arguments) : 0u);
  // Rigid objects: recorded while sampled (first seen, every RIGID_SAMPLE_PERIOD frames) or moving, drawn again while
  // moving.
  bool record = false;
  bool redraw = true;
  if (shader.kind == MotionKind::RIGID) {
    if (!objects.rigid_compute) return false;
    if (objects.rigid_meshes.size() >= RIGID_MESHES) {
      objects.rigid_meshes.clear();
    }
    const auto& mesh = objects.rigid_meshes.try_emplace(mesh_key, RigidMesh{.sample_until = frame + 2}).first->second;
    redraw = frame < mesh.moving_until;
    record = redraw || frame < mesh.sample_until || (frame + mesh_key) % RIGID_SAMPLE_PERIOD < 2;
    if (!record) return false;
  }
  // Deformed meshes: one instance (its vertices by vertex).
  if (shader.kind == MotionKind::DEFORMED && (arguments.instance_count != 1 || !objects.capture_pixel_shader)) return false;
  // In-draw: only draws with motion (rigid objects: moving; the rest recorded after the draw).
  if (in_draw && !redraw) return false;

  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets = {};
  ComPtr<ID3D11DepthStencilView> depth_stencil;
  native->OMGetRenderTargets(static_cast<UINT>(game_targets.size()), game_targets.data(), depth_stencil.GetAddressOf());
  const auto release_targets = [&] {
    for (auto* target : game_targets) {
      if (target != nullptr) {
        target->Release();
      }
    }
  };
  ComPtr<ID3D11DepthStencilState> game_depth_state;
  UINT stencil_reference = 0;
  native->OMGetDepthStencilState(game_depth_state.GetAddressOf(), &stencil_reference);
  D3D11_DEPTH_STENCIL_DESC depth_desc = {};
  if (game_depth_state) {
    game_depth_state->GetDesc(&depth_desc);
  }
  ComPtr<ID3D11Resource> depth_resource;
  if (depth_stencil) {
    depth_stencil->GetResource(depth_resource.GetAddressOf());
  }
  ComPtr<ID3D11Texture2D> depth_texture;
  D3D11_TEXTURE2D_DESC texture_desc = {};
  if (!depth_resource || !game_depth_state || !depth_desc.DepthEnable || (view.depth != nullptr && depth_resource.Get() != view.depth)
      || FAILED(depth_resource.As(&depth_texture))) {
    release_targets();
    return false;
  }
  depth_texture->GetDesc(&texture_desc);
  ID3D11PixelShader* pixel_shader = (shader.kind == MotionKind::UNIT ? objects.unit_pixel_shader : objects.wind_pixel_shader).Get();
  if (pixel_shader == nullptr || texture_desc.Width != view.width || texture_desc.Height != view.height
      || texture_desc.SampleDesc.Count != 1) {
    release_targets();
    return false;
  }
  // In-draw: the game's pixel shader patched for the vertex shader's MOTION register (made at its first such draw), the
  // draw's targets 6 and 7 free.
  ComPtr<ID3D11PixelShader> game_pixel_shader;
  native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
  ComPtr<ID3D11PixelShader> drawn_pixel_shader;
  if (in_draw) {
    const auto game = state.pixel_shaders.find(reinterpret_cast<uint64_t>(game_pixel_shader.Get()));
    if (game == state.pixel_shaders.end() || game_targets[dxbc::MOTION_TARGET] != nullptr
        || game_targets[dxbc::MOTION_DEPTH_TARGET] != nullptr) {
      release_targets();
      return false;
    }
    const uint32_t motion_register = state.draw_signatures[index].motion_register;
    auto& patched = game->second.patched;
    auto known = std::ranges::find(patched, motion_register, &std::pair<uint32_t, ComPtr<ID3D11PixelShader>>::first);
    if (known == patched.end()) {
      const std::vector<uint8_t> code = dxbc::AddMotionTargets(game->second.code, motion_register);
      ComPtr<ID3D11PixelShader> created;
      if (!code.empty() && FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, created.GetAddressOf()))) {
        renodx::utils::log::w("[Total War] Object motion: a patched pixel shader was refused (its draws drawn again).");
        created.Reset();
      }
      known = patched.insert(patched.end(), {motion_register, std::move(created)});
    }
    drawn_pixel_shader = known->second;
    if (!drawn_pixel_shader) {
      release_targets();
      return false;
    }
  }
  // What this draw does besides the game's: the motion in the draw itself or drawn again after it; the units' records
  // streamed out (Attila) or written from every pixel of the redraw; the deformed meshes' captures (Motion Test 1: no
  // records or captures). The pixel stage's motion constants serve the redraw and the captures, which alone change the
  // rasterizer and depth states.
  const bool drawn = static_cast<bool>(drawn_pixel_shader);
  const bool no_records = (test.load(std::memory_order_relaxed) == 1);
  const bool captures = shader.kind == MotionKind::DEFORMED && !no_records;
  const bool unit_stream = shader.kind == MotionKind::UNIT && objects.unit_records_compute && objects.record_outputs[index];
  const bool unit_table = shader.kind == MotionKind::UNIT && !unit_stream && !no_records;
  const bool pixel_constants = !drawn || captures;
  if (D3D11_TEXTURE2D_DESC current = {}; objects.motion) {
    objects.motion->GetDesc(&current);
    if (current.Width != texture_desc.Width || current.Height != texture_desc.Height) {
      objects.motion.Reset();
      objects.motion_rtv.Reset();
      objects.motion_srv.Reset();
    }
  }
  // The motion target, and the in-draw ones where the addon has in-draw shaders.
  const auto make_target = [&](DXGI_FORMAT format, ComPtr<ID3D11Texture2D>* texture, ComPtr<ID3D11RenderTargetView>* rtv,
                               ComPtr<ID3D11ShaderResourceView>* srv) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = texture_desc.Width,
        .Height = texture_desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };
    return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->GetAddressOf()))
           && SUCCEEDED(device->CreateRenderTargetView(texture->Get(), nullptr, rtv->GetAddressOf()))
           && SUCCEEDED(device->CreateShaderResourceView(texture->Get(), nullptr, srv->GetAddressOf()));
  };
  if (!objects.motion) {
    const bool has_in_draw = std::ranges::any_of(objects.draw_shaders, [](const auto& draw_shader) { return draw_shader != nullptr; });
    objects.drawn_motion.Reset();
    objects.drawn_motion_rtv.Reset();
    objects.drawn_motion_srv.Reset();
    objects.drawn_depth.Reset();
    objects.drawn_depth_rtv.Reset();
    objects.drawn_depth_srv.Reset();
    if (!make_target(DXGI_FORMAT_R32G32_UINT, &objects.motion, &objects.motion_rtv, &objects.motion_srv)
        || (has_in_draw
            && (!make_target(DXGI_FORMAT_R16G16_FLOAT, &objects.drawn_motion, &objects.drawn_motion_rtv, &objects.drawn_motion_srv)
                || !make_target(DXGI_FORMAT_R32_FLOAT, &objects.drawn_depth, &objects.drawn_depth_rtv, &objects.drawn_depth_srv)))) {
      objects.motion.Reset();
      objects.motion_rtv.Reset();
      objects.motion_srv.Reset();
      release_targets();
      return false;
    }
    objects.motion_frame = UINT64_MAX;
  }
  if (in_draw && !objects.drawn_motion_rtv) {
    release_targets();
    return false;
  }
  if (objects.motion_frame != frame) {
    constexpr std::array<float, 4> NONE = {};
    native->ClearRenderTargetView(objects.motion_rtv.Get(), NONE.data());
    if (objects.drawn_motion_rtv) {
      native->ClearRenderTargetView(objects.drawn_motion_rtv.Get(), NONE.data());
      native->ClearRenderTargetView(objects.drawn_depth_rtv.Get(), NONE.data());
    }
    objects.motion_frame = frame;
  }
  // The placement tables (made at the first such draw; rigid objects: and their records and flags; deformed meshes: and
  // their positions and anchors), this frame's cleared at the frame's first.
  uint32_t draw_word = 0;  // units: the animation texture's key; rigid objects, deformed meshes: the draw's slot
  std::array<uint32_t, 4> draw_range = {};
  ComPtr<ID3D11ShaderResourceView> animation_map;
  if (tabled) {
    if (!objects.placement_key_uavs[0]) {
      const auto make = [&](DXGI_FORMAT format, UINT elements, UINT element_bytes, ComPtr<ID3D11ShaderResourceView>* srv,
                            ComPtr<ID3D11UnorderedAccessView>* uav) {
        const D3D11_BUFFER_DESC desc = {.ByteWidth = elements * element_bytes, .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
        srv_desc.Buffer.NumElements = elements;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = format, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
        uav_desc.Buffer.NumElements = elements;
        ComPtr<ID3D11Buffer> buffer;
        return SUCCEEDED(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()))
               && (srv == nullptr || SUCCEEDED(device->CreateShaderResourceView(buffer.Get(), &srv_desc, srv->GetAddressOf())))
               && SUCCEEDED(device->CreateUnorderedAccessView(buffer.Get(), &uav_desc, uav->GetAddressOf()));
      };
      // The units' bone pool only when the addon has units; full floats (half ones moved a still horse's armour by ~1 mm:
      // DLSS shifted it every frame).
      const bool units = static_cast<bool>(objects.unit_pixel_shader);
      bool made = true;
      for (size_t table = 0; table < 2 && made; ++table) {
        made = make(DXGI_FORMAT_R32_UINT, PLACEMENT_CELLS * PLACEMENT_SLOTS, 4, &objects.placement_key_srvs[table],
                    &objects.placement_key_uavs[table])
               && make(DXGI_FORMAT_R32G32B32A32_UINT, PLACEMENT_CELLS * PLACEMENT_SLOTS * 4, 16, &objects.placement_srvs[table],
                       &objects.placement_uavs[table])
               && (!units
                   || (make(DXGI_FORMAT_R32G32B32A32_FLOAT, BONE_SOLDIERS * BONE_TEXELS, 16, &objects.bone_srvs[table],
                            &objects.bone_uavs[table])
                       && make(DXGI_FORMAT_R32_UINT, BONE_SLOTS + 1, 4, &objects.bone_key_srvs[table], &objects.bone_key_uavs[table])
                       && make(DXGI_FORMAT_R32_UINT, BONE_SLOTS, 4, &objects.bone_entry_srvs[table], &objects.bone_entry_uavs[table])))
               && (!objects.capture_pixel_shader
                   || make(DXGI_FORMAT_R32G32B32A32_FLOAT, DEFORMED_POSITIONS, 16, &objects.deformed_position_srvs[table],
                           &objects.deformed_position_uavs[table]));
      }
      if (made && objects.capture_pixel_shader) {
        made = make(DXGI_FORMAT_R32G32B32A32_FLOAT, DEFORMED_DRAWS, 16, &objects.anchor_srv, &objects.anchor_uav);
      }
      if (made && objects.unit_records_compute) {
        const D3D11_BUFFER_DESC records_desc = {.ByteWidth = UNIT_RECORDS * UNIT_RECORD_BYTES, .Usage = D3D11_USAGE_DEFAULT,
                                                .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE};
        D3D11_SHADER_RESOURCE_VIEW_DESC records_view = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                                        .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
        records_view.Buffer.NumElements = UNIT_RECORDS * UNIT_RECORD_BYTES / 16;
        made = SUCCEEDED(device->CreateBuffer(&records_desc, nullptr, objects.unit_records.GetAddressOf()))
               && SUCCEEDED(device->CreateShaderResourceView(objects.unit_records.Get(), &records_view,
                                                             objects.unit_records_srv.GetAddressOf()));
      }
      if (made && objects.rigid_compute) {
        const D3D11_BUFFER_DESC records_desc = {.ByteWidth = RIGID_RECORDS * RIGID_RECORD_BYTES, .Usage = D3D11_USAGE_DEFAULT,
                                                .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE};
        D3D11_SHADER_RESOURCE_VIEW_DESC records_view = {.Format = DXGI_FORMAT_R32G32B32A32_UINT,
                                                        .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
        records_view.Buffer.NumElements = RIGID_RECORDS * RIGID_RECORD_BYTES / 16;
        const D3D11_BUFFER_DESC readback_desc = {.ByteWidth = RIGID_DRAWS * 4, .Usage = D3D11_USAGE_STAGING,
                                                 .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
        made = SUCCEEDED(device->CreateBuffer(&records_desc, nullptr, objects.rigid_records.GetAddressOf()))
               && SUCCEEDED(device->CreateShaderResourceView(objects.rigid_records.Get(), &records_view,
                                                             objects.rigid_records_srv.GetAddressOf()))
               && make(DXGI_FORMAT_R32_UINT, RIGID_DRAWS, 4, nullptr, &objects.rigid_flags_uav)
               && std::ranges::all_of(objects.rigid_readbacks, [&](RigidReadback& readback) {
                    return SUCCEEDED(device->CreateBuffer(&readback_desc, nullptr, readback.flags.GetAddressOf()));
                  });
      }
      if (!made) {
        objects.placement_key_srvs = {};
        objects.placement_key_uavs = {};
        objects.placement_srvs = {};
        objects.placement_uavs = {};
        objects.bone_srvs = {};
        objects.bone_uavs = {};
        objects.bone_key_srvs = {};
        objects.bone_key_uavs = {};
        objects.bone_entry_srvs = {};
        objects.bone_entry_uavs = {};
        objects.deformed_position_srvs = {};
        objects.deformed_position_uavs = {};
        objects.anchor_srv.Reset();
        objects.anchor_uav.Reset();
        objects.unit_records.Reset();
        objects.unit_records_srv.Reset();
        objects.rigid_records.Reset();
        objects.rigid_records_srv.Reset();
        objects.rigid_flags_uav.Reset();
        objects.rigid_readbacks = {};
        release_targets();
        return false;
      }
      objects.placement_frame = UINT64_MAX;
    }
    if (objects.placement_frame != frame) {
      objects.previous_placements = (objects.placement_frame + 1 == frame);
      objects.placement_index ^= 1;
      constexpr std::array<UINT, 4> EMPTY = {};
      native->ClearUnorderedAccessViewUint(objects.placement_key_uavs[objects.placement_index].Get(), EMPTY.data());
      if (objects.bone_key_uavs[0]) {
        native->ClearUnorderedAccessViewUint(objects.bone_key_uavs[objects.placement_index].Get(), EMPTY.data());
        native->ClearUnorderedAccessViewUint(objects.bone_entry_uavs[objects.placement_index].Get(), EMPTY.data());
      }
      objects.rigid_record_count = 0;
      objects.rigid_draws.clear();
      objects.deformed_position_count = 0;
      objects.deformed_draw_count = 0;
      objects.unit_record_count = 0;
      objects.unit_record_draws.clear();
      objects.placement_frame = frame;
    }
    if (shader.kind == MotionKind::UNIT) {
      // The draw's animation texture (its bones), and its key for the pool.
      native->VSGetShaderResources(0, 1, animation_map.GetAddressOf());
      if (animation_map) {
        ComPtr<ID3D11Resource> animation_texture;
        animation_map->GetResource(animation_texture.GetAddressOf());
        auto known = std::ranges::find(objects.animation_textures, animation_texture.Get());
        if (known == objects.animation_textures.end()) {
          if (objects.animation_textures.size() >= 64) {
            objects.animation_textures.clear();
          }
          known = objects.animation_textures.insert(objects.animation_textures.end(), animation_texture.Get());
        }
        draw_word = static_cast<uint32_t>(known - objects.animation_textures.begin()) + 1;
      }
    }
  }
  // Rigid objects: a slot for the draw's flags and room for its records, else not recorded this frame.
  if (record) {
    record = objects.rigid_draws.size() < RIGID_DRAWS
             && objects.rigid_record_count + uint64_t{arguments.instance_count} <= RIGID_RECORDS;
    if (record) {
      draw_word = static_cast<uint32_t>(objects.rigid_draws.size());
    }
    if (!record && !redraw) {
      release_targets();
      return false;
    }
  }
  // Deformed meshes: room for a position per index (or vertex) and a slot for the anchor, else no motion this frame.
  if (shader.kind == MotionKind::DEFORMED) {
    if (objects.deformed_draw_count >= DEFORMED_DRAWS
        || objects.deformed_position_count + uint64_t{vertex_count} > DEFORMED_POSITIONS) {
      release_targets();
      return false;
    }
    draw_word = objects.deformed_draw_count++;
    draw_range = {objects.deformed_position_count, vertex_count, first_vertex, 0};
    objects.deformed_position_count += vertex_count;
  }
  // The ocean: its displacement textures copied at the frame's first draw, last frame's copies for the motion shader.
  std::array<ID3D11ShaderResourceView*, HISTORY_SLOTS> history_views = {};
  if (shader.kind == MotionKind::HISTORY) {
    std::array<ID3D11ShaderResourceView*, HISTORY_SLOTS> game_textures = {};
    native->VSGetShaderResources(0, HISTORY_SLOTS, game_textures.data());
    for (UINT slot = 0; slot < HISTORY_SLOTS; ++slot) {
      auto& history = objects.texture_histories[slot];
      ComPtr<ID3D11ShaderResourceView> texture_view;
      texture_view.Attach(game_textures[slot]);
      if ((shader.history_textures & (1u << slot)) == 0 || !texture_view) continue;
      history_views[slot] = texture_view.Get();
      ComPtr<ID3D11Resource> resource;
      texture_view->GetResource(resource.GetAddressOf());
      ComPtr<ID3D11Texture2D> texture;
      if (FAILED(resource.As(&texture))) continue;
      if (history.frame != frame) {
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        if (D3D11_TEXTURE2D_DESC copy = {}; history.copies[0]) {
          history.copies[0]->GetDesc(&copy);
          if (copy.Width != desc.Width || copy.Height != desc.Height || copy.MipLevels != desc.MipLevels
              || copy.ArraySize != desc.ArraySize || copy.Format != desc.Format) {
            history = {};
          }
        }
        if (!history.copies[0]) {
          desc.Usage = D3D11_USAGE_DEFAULT;
          desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
          desc.CPUAccessFlags = 0;
          desc.MiscFlags = 0;
          D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
          texture_view->GetDesc(&view_desc);
          for (size_t copy = 0; copy < 2; ++copy) {
            if (FAILED(device->CreateTexture2D(&desc, nullptr, history.copies[copy].GetAddressOf()))
                || FAILED(device->CreateShaderResourceView(history.copies[copy].Get(), &view_desc,
                                                           history.views[copy].GetAddressOf()))) {
              history = {};
              break;
            }
          }
          if (!history.copies[0]) continue;
        }
        history.continued = (history.frame + 1 == frame);
        native->CopyResource(history.copies[frame & 1].Get(), texture.Get());
        history.frame = frame;
      }
      if (history.continued) {
        history_views[slot] = history.views[(frame - 1) & 1].Get();
      }
    }
  }
  MotionConstants constants = view.constants;
  constants.unit = {mesh_key, (objects.previous_placements ? 1u : 0u), 0u, draw_word};
  constants.draw = draw_range;
  if (std::memcmp(&constants, &objects.motion_uploaded, sizeof(constants)) != 0 || !view.camera_copies.empty()) {
    native->UpdateSubresource(objects.motion_constants.Get(), 0, nullptr, &constants, 0, 0);
    objects.motion_uploaded = constants;
    for (const auto& run : view.camera_copies) {
      const D3D11_BOX box = {.left = run.source, .top = 0, .front = 0, .right = run.source + run.bytes, .bottom = 1, .back = 1};
      native->CopySubresourceRegion(objects.motion_constants.Get(), 0, run.destination, 0, 0, camera.Get(), 0, &box);
    }
  }
  // The redraw's: the game's comparison made inclusive, without writes or stencil; its rasterizer state moved 4 depth
  // units toward the camera (the redrawn depth can differ from the game's in the last bits).
  ComPtr<ID3D11RasterizerState> game_rasterizer;
  if (!drawn || captures) {
    native->RSGetState(game_rasterizer.GetAddressOf());
  }
  ID3D11DepthStencilState* motion_depth_state = nullptr;
  ID3D11RasterizerState* motion_rasterizer = nullptr;
  if (redraw && !drawn_pixel_shader) {
    const bool reversed = depth_desc.DepthFunc == D3D11_COMPARISON_GREATER || depth_desc.DepthFunc == D3D11_COMPARISON_GREATER_EQUAL;
    D3D11_COMPARISON_FUNC comparison = depth_desc.DepthFunc;
    if (comparison == D3D11_COMPARISON_LESS) {
      comparison = D3D11_COMPARISON_LESS_EQUAL;
    }
    if (comparison == D3D11_COMPARISON_GREATER) {
      comparison = D3D11_COMPARISON_GREATER_EQUAL;
    }
    auto& depth_state = objects.motion_depth_states[std::clamp(static_cast<int>(comparison), 0, 8)];
    if (!depth_state) {
      const D3D11_DEPTH_STENCIL_DESC desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO, .DepthFunc = comparison};
      if (FAILED(device->CreateDepthStencilState(&desc, depth_state.GetAddressOf()))) {
        release_targets();
        return false;
      }
    }
    D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_BACK, .DepthClipEnable = TRUE};
    if (game_rasterizer) {
      game_rasterizer->GetDesc(&rasterizer_desc);
    }
    rasterizer_desc.DepthBias = (reversed ? 4 : -4);
    rasterizer_desc.SlopeScaledDepthBias = 0.f;
    rasterizer_desc.DepthBiasClamp = 0.f;
    auto rasterizer = std::ranges::find_if(objects.motion_rasterizer_states, [&](const auto& entry) {
      return std::memcmp(&entry.first, &rasterizer_desc, sizeof(rasterizer_desc)) == 0;
    });
    if (rasterizer == objects.motion_rasterizer_states.end()) {
      ComPtr<ID3D11RasterizerState> rasterizer_state;
      if (FAILED(device->CreateRasterizerState(&rasterizer_desc, rasterizer_state.GetAddressOf()))) {
        release_targets();
        return false;
      }
      rasterizer = objects.motion_rasterizer_states.insert(objects.motion_rasterizer_states.end(),
                                                           {rasterizer_desc, std::move(rasterizer_state)});
    }
    motion_depth_state = depth_state.Get();
    motion_rasterizer = rasterizer->second.Get();
  }

  // The game's bindings this changes, put back after (the in-draw's by FinishInDraw).
  ComPtr<ID3D11VertexShader> game_vertex_shader;
  native->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
  ComPtr<ID3D11Buffer> game_vs_constants;
  ComPtr<ID3D11Buffer> game_ps_constants;
  native->VSGetConstantBuffers(MOTION_SLOT, 1, game_vs_constants.GetAddressOf());
  if (pixel_constants) {
    native->PSGetConstantBuffers(MOTION_SLOT, 1, game_ps_constants.GetAddressOf());
  }
  ComPtr<ID3D11BlendState> game_blend;
  std::array<float, 4> blend_factor = {};
  UINT sample_mask = 0;
  native->OMGetBlendState(game_blend.GetAddressOf(), blend_factor.data(), &sample_mask);
  std::array<ID3D11ShaderResourceView*, MOTION_VIEWS> game_motion_views = {};
  if (shader.kind != MotionKind::WIND) {
    native->VSGetShaderResources(DEFORMED_SRV_SLOT, MOTION_VIEWS, game_motion_views.data());
  }
  ComPtr<ID3D11ShaderResourceView> game_animation_view;
  if (unit_table) {
    native->PSGetShaderResources(ANIMATION_SRV_SLOT, 1, game_animation_view.GetAddressOf());
  }
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  native->IAGetPrimitiveTopology(&topology);

  const size_t last = objects.placement_index ^ 1;
  const size_t current = objects.placement_index;
  ID3D11Buffer* motion_constants = objects.motion_constants.Get();
  native->VSSetConstantBuffers(MOTION_SLOT, 1, &motion_constants);
  if (pixel_constants) {
    native->PSSetConstantBuffers(MOTION_SLOT, 1, &motion_constants);
  }
  // The deformed meshes' captures write the same tables draw after draw (and the units' records at the frame's end,
  // dispatch after dispatch): NVIDIA's UAV overlap (DLSS: an NVIDIA GPU) keeps the driver from waiting for the GPU to
  // idle before each. nvapi loaded at the first.
  auto& overlap = state.uav_overlap;
  if (!overlap.loaded && (shader.kind == MotionKind::UNIT || shader.kind == MotionKind::DEFORMED)) {
    overlap.loaded = true;
    using Query = void*(__cdecl*)(uint32_t id);
    using Initialize = int(__cdecl*)();
    Query query = nullptr;
    Initialize initialize = nullptr;
    if (HMODULE nvapi = LoadLibraryW(sizeof(void*) == 8 ? L"nvapi64.dll" : L"nvapi.dll"); nvapi != nullptr) {
      query = reinterpret_cast<Query>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    }
    if (query != nullptr) {
      initialize = reinterpret_cast<Initialize>(query(0x0150E828));
    }
    if (initialize != nullptr && initialize() == 0) {
      overlap.begin = reinterpret_cast<decltype(overlap.begin)>(query(0xBA08208A));
      overlap.end = reinterpret_cast<decltype(overlap.end)>(query(0x2216A357));
    }
    if (overlap.begin == nullptr || overlap.end == nullptr) {
      overlap.begin = nullptr;
      overlap.end = nullptr;
    }
    renodx::utils::log::i("[Total War] Object motion: NVIDIA's UAV overlap ", (overlap.begin != nullptr ? "used." : "unavailable."));
  }
  // Records (rigid objects, units): one point per instance (its first vertex) through its record shader's stream output
  // into `records` at `offset`, not rasterized (the game's targets stay bound: a pixel shader writing them cost ~4 ms a
  // frame with a unit of cavalry on screen).
  const auto stream_records = [&](ID3D11Buffer* records, UINT offset) {
    ComPtr<ID3D11GeometryShader> game_geometry_shader;
    native->GSGetShader(game_geometry_shader.GetAddressOf(), nullptr, nullptr);
    ComPtr<ID3D11Buffer> game_output;
    native->SOGetTargets(1, game_output.GetAddressOf());
    native->VSSetShader(objects.record_shaders[index].Get(), nullptr, 0);
    native->GSSetShader(objects.record_outputs[index].Get(), nullptr, 0);
    native->PSSetShader(nullptr, nullptr, 0);
    native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    native->SOSetTargets(1, &records, &offset);
    if constexpr (INDEXED) {
      native->DrawIndexedInstanced(1, arguments.instance_count, arguments.first_index, arguments.vertex_offset, arguments.first_instance);
    } else {
      native->DrawInstanced(1, arguments.instance_count, arguments.first_vertex, arguments.first_instance);
    }
    ID3D11Buffer* output = game_output.Get();
    constexpr UINT APPEND = UINT_MAX;
    native->SOSetTargets(1, &output, &APPEND);
    native->IASetPrimitiveTopology(topology);
    native->GSSetShader(game_geometry_shader.Get(), nullptr, 0);
  };
  if (record && !no_records) {
    // Its rows and (mesh key, slot) after the frame's records.
    stream_records(objects.rigid_records.Get(), objects.rigid_record_count * RIGID_RECORD_BYTES);
    objects.rigid_record_count += arguments.instance_count;
    objects.rigid_draws.push_back(mesh_key);
  }
  if (captures) {
    // One point per index on one pixel (no depth, no target): the vertices' world positions and the anchor.
    const std::array<ID3D11UnorderedAccessView*, 4> capture = {objects.deformed_position_uavs[current].Get(),
                                                               objects.anchor_uav.Get(), objects.placement_key_uavs[current].Get(),
                                                               objects.placement_uavs[current].Get()};
    native->VSSetShader(objects.record_shaders[index].Get(), nullptr, 0);
    native->PSSetShader(objects.capture_pixel_shader.Get(), nullptr, 0);
    native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    native->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, static_cast<UINT>(capture.size()), capture.data(), nullptr);
    native->OMSetDepthStencilState(objects.capture_depth_state.Get(), 0);
    native->RSSetState(objects.capture_rasterizer.Get());
    if (overlap.begin != nullptr) {
      overlap.begin(native, 0);
    }
    if constexpr (INDEXED) {
      native->DrawIndexedInstanced(vertex_count, 1, arguments.first_index, arguments.vertex_offset, arguments.first_instance);
    } else {
      native->DrawInstanced(vertex_count, 1, arguments.first_vertex, arguments.first_instance);
    }
    if (overlap.end != nullptr) {
      overlap.end(native);
    }
  }
  native->IASetPrimitiveTopology(topology);
  if (tabled) {
    // t9-t15: last frame's positions, this frame's anchors, last frame's bone keys, entries, pool, placement keys and
    // rows.
    const std::array<ID3D11ShaderResourceView*, 7> previous_table = {
        objects.deformed_position_srvs[last].Get(), objects.anchor_srv.Get(),     objects.bone_key_srvs[last].Get(),
        objects.bone_entry_srvs[last].Get(),        objects.bone_srvs[last].Get(), objects.placement_key_srvs[last].Get(),
        objects.placement_srvs[last].Get()};
    native->VSSetShaderResources(DEFORMED_SRV_SLOT, static_cast<UINT>(previous_table.size()), previous_table.data());
  }
  if (shader.kind == MotionKind::HISTORY) {
    native->VSSetShaderResources(HISTORY_SRV_SLOT, HISTORY_SLOTS, history_views.data());
  }
  // Units: their records (placement, bones) for this frame's table. Streamed out where the addon records them at the
  // frame's end (Shaders::unit_records_compute_shader: the draw's soldiers after the frame's records, with the
  // animation texture they read), else written from every pixel of the redraw (the table bound with it, the animation
  // texture at ANIMATION_SRV_SLOT).
  const std::array<ID3D11UnorderedAccessView*, 5> table = {
      objects.placement_key_uavs[current].Get(), objects.placement_uavs[current].Get(), objects.bone_uavs[current].Get(),
      objects.bone_key_uavs[current].Get(), objects.bone_entry_uavs[current].Get()};
  constexpr std::array<ID3D11UnorderedAccessView*, 5> NO_TABLE = {};
  if (unit_table) {
    ID3D11ShaderResourceView* bones = animation_map.Get();
    native->PSSetShaderResources(ANIMATION_SRV_SLOT, 1, &bones);
  }
  if (unit_stream && !no_records && animation_map
      && objects.unit_record_count + uint64_t{arguments.instance_count} <= UNIT_RECORDS) {
    stream_records(objects.unit_records.Get(), objects.unit_record_count * UNIT_RECORD_BYTES);
    objects.unit_record_draws.push_back(
        {.first = objects.unit_record_count, .count = arguments.instance_count, .animation_map = animation_map});
    objects.unit_record_count += arguments.instance_count;
  }
  if (drawn_pixel_shader) {
    // The game's draw writes the motion: its in-draw vertex shader, its pixel shader patched, targets 6 and 7 with
    // blending off there (the game's for the rest), the motion constants and tables kept bound; FinishInDraw puts the
    // game's back after it.
    auto& pending = state.in_draw;
    pending = {
        .pending = true,
        .vertex_shader = game_vertex_shader,
        .pixel_shader = game_pixel_shader,
        .depth_stencil = depth_stencil,
        .blend = game_blend,
        .blend_factor = blend_factor,
        .sample_mask = sample_mask,
        .vs_constants = game_vs_constants,
        .views_changed = (shader.kind != MotionKind::WIND),
    };
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> drawn_targets = game_targets;
    for (size_t slot = 0; slot < game_targets.size(); ++slot) {
      pending.targets[slot] = game_targets[slot];
    }
    for (size_t slot = 0; slot < game_motion_views.size(); ++slot) {
      pending.motion_views[slot] = game_motion_views[slot];
    }
    drawn_targets[dxbc::MOTION_TARGET] = objects.drawn_motion_rtv.Get();
    drawn_targets[dxbc::MOTION_DEPTH_TARGET] = objects.drawn_depth_rtv.Get();
    auto drawn_blend = std::ranges::find(objects.drawn_blend_states, game_blend.Get(),
                                         [](const auto& entry) { return entry.first.Get(); });
    if (drawn_blend == objects.drawn_blend_states.end()) {
      D3D11_BLEND_DESC desc = {};
      desc.RenderTarget[0] = {.SrcBlend = D3D11_BLEND_ONE, .DestBlend = D3D11_BLEND_ZERO, .BlendOp = D3D11_BLEND_OP_ADD,
                              .SrcBlendAlpha = D3D11_BLEND_ONE, .DestBlendAlpha = D3D11_BLEND_ZERO,
                              .BlendOpAlpha = D3D11_BLEND_OP_ADD, .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL};
      if (game_blend) {
        game_blend->GetDesc(&desc);
      }
      if (!desc.IndependentBlendEnable) {
        std::fill(std::begin(desc.RenderTarget) + 1, std::end(desc.RenderTarget), desc.RenderTarget[0]);
        desc.IndependentBlendEnable = TRUE;
      }
      for (const uint32_t target : {dxbc::MOTION_TARGET, dxbc::MOTION_DEPTH_TARGET}) {
        desc.RenderTarget[target] = {.BlendEnable = FALSE, .SrcBlend = D3D11_BLEND_ONE, .DestBlend = D3D11_BLEND_ZERO,
                                     .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ONE,
                                     .DestBlendAlpha = D3D11_BLEND_ZERO, .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                                     .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL};
      }
      ComPtr<ID3D11BlendState> blend_state;
      if (FAILED(device->CreateBlendState(&desc, blend_state.GetAddressOf()))) {
        blend_state = game_blend;
      }
      if (objects.drawn_blend_states.size() >= 256) {
        objects.drawn_blend_states.clear();
      }
      drawn_blend = objects.drawn_blend_states.insert(objects.drawn_blend_states.end(), {game_blend, std::move(blend_state)});
    }
    native->OMSetRenderTargets(static_cast<UINT>(drawn_targets.size()), drawn_targets.data(), depth_stencil.Get());
    native->OMSetBlendState(drawn_blend->second.Get(), blend_factor.data(), sample_mask);
    if (captures) {
      native->OMSetDepthStencilState(game_depth_state.Get(), stencil_reference);
      native->RSSetState(game_rasterizer.Get());
    }
    native->VSSetShader(objects.draw_shaders[index].Get(), nullptr, 0);
    native->PSSetShader(drawn_pixel_shader.Get(), nullptr, 0);
  } else if (redraw) {
    ID3D11RenderTargetView* motion_target = objects.motion_rtv.Get();
    native->VSSetShader(objects.motion_shaders[index].Get(), nullptr, 0);
    native->PSSetShader(pixel_shader, nullptr, 0);
    if (unit_table) {
      native->OMSetRenderTargetsAndUnorderedAccessViews(1, &motion_target, depth_stencil.Get(), 1, static_cast<UINT>(table.size()),
                                                        table.data(), nullptr);
    } else {
      native->OMSetRenderTargets(1, &motion_target, depth_stencil.Get());
    }
    native->OMSetDepthStencilState(motion_depth_state, 0);
    native->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    native->RSSetState(motion_rasterizer);
    if constexpr (INDEXED) {
      native->DrawIndexedInstanced(vertex_count, arguments.instance_count, arguments.first_index, arguments.vertex_offset,
                                   arguments.first_instance);
    } else {
      native->DrawInstanced(vertex_count, arguments.instance_count, arguments.first_vertex, arguments.first_instance);
    }
    if (unit_table) {
      native->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 1,
                                                        static_cast<UINT>(NO_TABLE.size()), NO_TABLE.data(), nullptr);
    }
  }
  // The game's bindings back, but for the in-draw's (FinishInDraw's).
  if (unit_table) {
    ID3D11ShaderResourceView* game_animation = game_animation_view.Get();
    native->PSSetShaderResources(ANIMATION_SRV_SLOT, 1, &game_animation);
  }
  if (!drawn && (redraw || shader.kind == MotionKind::DEFORMED)) {
    native->OMSetRenderTargets(static_cast<UINT>(game_targets.size()), game_targets.data(), depth_stencil.Get());
    native->OMSetDepthStencilState(game_depth_state.Get(), stencil_reference);
    native->OMSetBlendState(game_blend.Get(), blend_factor.data(), sample_mask);
    native->RSSetState(game_rasterizer.Get());
  }
  if (shader.kind != MotionKind::WIND) {
    if (!drawn) {
      native->VSSetShaderResources(DEFORMED_SRV_SLOT, MOTION_VIEWS, game_motion_views.data());
    }
    for (auto* game_view : game_motion_views) {
      if (game_view != nullptr) {
        game_view->Release();
      }
    }
  }
  if (!drawn) {
    native->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
    native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
    ID3D11Buffer* vs_constants = game_vs_constants.Get();
    native->VSSetConstantBuffers(MOTION_SLOT, 1, &vs_constants);
  }
  if (pixel_constants) {
    ID3D11Buffer* ps_constants = game_ps_constants.Get();
    native->PSSetConstantBuffers(MOTION_SLOT, 1, &ps_constants);
  }
  release_targets();
  return drawn;
}

// After a moving draw that wrote its motion itself (or was skipped): the game's bindings back.
template <typename Context>
void FinishInDraw(Context& command, const void*) {
  const auto started = std::chrono::steady_clock::now();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  const std::unique_lock lock(state.mutex);
  auto& pending = state.in_draw;
  if (!pending.pending) return;
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets = {};
  for (size_t slot = 0; slot < targets.size(); ++slot) {
    targets[slot] = pending.targets[slot].Get();
  }
  std::array<ID3D11ShaderResourceView*, MOTION_VIEWS> views = {};
  for (size_t slot = 0; slot < views.size(); ++slot) {
    views[slot] = pending.motion_views[slot].Get();
  }
  ID3D11Buffer* vs_constants = pending.vs_constants.Get();
  native->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), pending.depth_stencil.Get());
  native->OMSetBlendState(pending.blend.Get(), pending.blend_factor.data(), pending.sample_mask);
  native->VSSetShader(pending.vertex_shader.Get(), nullptr, 0);
  native->PSSetShader(pending.pixel_shader.Get(), nullptr, 0);
  native->VSSetConstantBuffers(MOTION_SLOT, 1, &vs_constants);
  if (pending.views_changed) {
    native->VSSetShaderResources(DEFORMED_SRV_SLOT, MOTION_VIEWS, views.data());
  }
  pending = {};
  timing.spent += std::chrono::steady_clock::now() - started;
}

// After a moving draw that did not write its motion: drawn again with it.
template <typename Context>
void RedrawObjectMotion(Context& command, const void*) {
  const auto started = std::chrono::steady_clock::now();
  DrawObjectMotion(command, false);
  timing.spent += std::chrono::steady_clock::now() - started;
}

// A moving draw: its motion written by the draw itself where it can (its bindings set now, put back after it: also
// when another callback skips the draw), else drawn again after it.
constexpr auto OnMovingDraw = []<typename T, typename Context>(
                                  Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (!active.load(std::memory_order_relaxed)) return {};
    const uint32_t test_mode = test.load(std::memory_order_relaxed);
    const MotionKind kind = command.template GetCallbackData<MotionShader>().kind;
    if ((test_mode == 2 && kind != MotionKind::UNIT) || (test_mode == 3 && kind != MotionKind::WIND)) return {};
    const auto started = std::chrono::steady_clock::now();
    const bool drawn = DrawObjectMotion(command, true);
    timing.spent += std::chrono::steady_clock::now() - started;
    ++timing.draws;
    if (drawn) {
      ++timing.drawn;
      return {.finalize_callback = FinishInDraw<Context>};
    }
    return {.post_callback = RedrawObjectMotion<Context>};
  }
};

// The game's pixel shaders as it creates them: kept where one can follow an in-draw vertex shader (its inputs, but
// system values, among that shader's outputs: same semantic, register, components within).
inline void OnInitPipeline(renodx::utils::pipeline::PipelineInfo* info, uint32_t subobject_count,
                           const reshade::api::pipeline_subobject* subobjects) {
  for (uint32_t subobject = 0; subobject < subobject_count; ++subobject) {
    if (subobjects[subobject].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
    const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[subobject].data);
    if (desc == nullptr || desc->code == nullptr) continue;
    const std::span<const uint8_t> code(static_cast<const uint8_t*>(desc->code), desc->code_size);
    const dxbc::Program program = dxbc::Read(code);
    if (program.tokens.empty()) continue;
    std::vector<std::pair<dxbc::SignatureElement, uint32_t>> inputs;
    dxbc::SignatureElement unused;
    dxbc::FindSignatureElement(code, program, "ISGN", [&](const dxbc::SignatureElement& element, uint32_t reg) {
      if (element.system_value == 0) {
        inputs.emplace_back(element, reg);
      }
      return false;
    }, &unused);
    const auto same_name = [](std::string_view a, std::string_view b) {
      return std::ranges::equal(a, b, [](char x, char y) { return (x | 0x20) == (y | 0x20); });
    };
    const std::unique_lock lock(state.mutex);
    const bool follows = std::ranges::any_of(state.draw_signatures, [&](const DrawSignature& signature) {
      return signature.motion_register != 0 && std::ranges::all_of(inputs, [&](const auto& input) {
               const auto& [element, reg] = input;
               return std::ranges::any_of(signature.outputs, [&](const DrawOutput& output) {
                 return output.index == element.index && output.reg == reg && (element.mask & ~output.mask) == 0
                        && same_name(output.name, element.name);
               });
             });
    });
    if (follows) {
      state.pixel_shaders[info->pipeline.handle] = {.code = {code.begin(), code.end()}};
    }
  }
}

inline void OnDestroyPipeline(const renodx::utils::pipeline::PipelineInfo& info) {
  const std::unique_lock lock(state.mutex);
  state.pixel_shaders.erase(info.pipeline.handle);
}

}  // namespace internal

// The host draws motion from now on (true) or not.
inline void SetActive(bool active) {
  internal::active.store(active, std::memory_order_relaxed);
}

// Motion Test (debug): 0 normal, 1 no records or captures, 2 units only, 3 plants only.
inline void SetTest(uint32_t test) {
  internal::test.store(test, std::memory_order_relaxed);
}

// The frame's end, after its last motion draw: the rigid objects' flags of a few frames back read (which meshes move),
// the units' streamed records into this frame's table (motion_unit_records.cs), then the rigid objects' records matched
// with last frame's table into this frame's (motion_rigid.cs), their draws' flags copied to be read back. The compute
// bindings it changes are put back.
inline void Resolve(ID3D11DeviceContext* native, uint64_t frame) {
  using namespace internal;
  const std::unique_lock lock(state.mutex);
  auto& objects = state.objects;
  for (auto& readback : objects.rigid_readbacks) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (readback.meshes.empty() || readback.frame + 2 > frame
        || FAILED(native->Map(readback.flags.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
      continue;
    }
    const auto* flags = static_cast<const uint32_t*>(mapped.pData);
    for (size_t slot = 0; slot < readback.meshes.size(); ++slot) {
      if ((flags[slot] & 1u) == 0) continue;
      if (auto mesh = objects.rigid_meshes.find(readback.meshes[slot]); mesh != objects.rigid_meshes.end()) {
        mesh->second.moving_until = frame + RIGID_MOVING_FRAMES;
      }
    }
    native->Unmap(readback.flags.Get(), 0);
    readback.meshes.clear();
  }
  if (objects.placement_frame != frame || (objects.unit_record_draws.empty() && objects.rigid_record_count == 0)) return;
  const size_t current = objects.placement_index;
  // The game's compute bindings: the shader, b0, t0, t14-t15 and u0-u5.
  ComPtr<ID3D11ComputeShader> game_shader;
  native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  ComPtr<ID3D11Buffer> game_constants;
  native->CSGetConstantBuffers(0, 1, game_constants.GetAddressOf());
  ID3D11ShaderResourceView* game_records_view = nullptr;
  native->CSGetShaderResources(0, 1, &game_records_view);
  std::array<ID3D11ShaderResourceView*, 2> game_table_views = {};
  native->CSGetShaderResources(PLACEMENT_SRV_SLOT + 3, 2, game_table_views.data());
  std::array<ID3D11UnorderedAccessView*, 6> game_uavs = {};
  native->CSGetUnorderedAccessViews(0, 6, game_uavs.data());

  if (!objects.unit_record_draws.empty()) {
    // The units' records (streamed out by their draws) into this frame's table and bone pool (motion_unit_records.cs:
    // u1-u5 as motion_unit_records.hlsli), consecutive draws on one animation texture (as it is now: the game writes each
    // once a frame) in one dispatch, under NVIDIA's UAV overlap.
    const std::array<ID3D11UnorderedAccessView*, 6> unit_outputs = {
        nullptr, objects.placement_key_uavs[current].Get(), objects.placement_uavs[current].Get(), objects.bone_uavs[current].Get(),
        objects.bone_key_uavs[current].Get(), objects.bone_entry_uavs[current].Get()};
    ID3D11Buffer* unit_constants = objects.unit_record_constants.Get();
    ID3D11ShaderResourceView* unit_records = objects.unit_records_srv.Get();
    native->CSSetShader(objects.unit_records_compute.Get(), nullptr, 0);
    native->CSSetConstantBuffers(0, 1, &unit_constants);
    native->CSSetShaderResources(0, 1, &unit_records);
    native->CSSetUnorderedAccessViews(0, static_cast<UINT>(unit_outputs.size()), unit_outputs.data(), nullptr);
    const auto& overlap = state.uav_overlap;
    if (overlap.begin != nullptr) {
      overlap.begin(native, 0);
    }
    const auto& draws = objects.unit_record_draws;
    for (size_t first = 0; first < draws.size();) {
      size_t next = first + 1;
      UINT count = draws[first].count;
      while (next < draws.size() && draws[next].animation_map == draws[first].animation_map
             && draws[next].first == draws[first].first + count) {
        count += draws[next].count;
        ++next;
      }
      const std::array<uint32_t, 4> range = {draws[first].first, count, 0u, 0u};
      native->UpdateSubresource(unit_constants, 0, nullptr, range.data(), 0, 0);
      ID3D11ShaderResourceView* animation_map = draws[first].animation_map.Get();
      native->CSSetShaderResources(ANIMATION_SRV_SLOT, 1, &animation_map);
      native->Dispatch((count + 63) / 64, 1, 1);
      first = next;
    }
    if (overlap.end != nullptr) {
      overlap.end(native);
    }
    constexpr std::array<ID3D11UnorderedAccessView*, 6> NO_UNIT_OUTPUTS = {};
    native->CSSetUnorderedAccessViews(0, static_cast<UINT>(NO_UNIT_OUTPUTS.size()), NO_UNIT_OUTPUTS.data(), nullptr);
  }

  if (objects.rigid_record_count != 0) {
    const std::array<uint32_t, 4> rigid = {objects.rigid_record_count, (objects.previous_placements ? 1u : 0u), 0u, 0u};
    native->UpdateSubresource(objects.rigid_constants.Get(), 0, nullptr, rigid.data(), 0, 0);
    constexpr std::array<UINT, 4> EMPTY = {};
    native->ClearUnorderedAccessViewUint(objects.rigid_flags_uav.Get(), EMPTY.data());
    const std::array<ID3D11ShaderResourceView*, 2> previous_table = {objects.placement_key_srvs[current ^ 1].Get(),
                                                                     objects.placement_srvs[current ^ 1].Get()};
    ID3D11ShaderResourceView* records = objects.rigid_records_srv.Get();
    const std::array<ID3D11UnorderedAccessView*, 3> outputs = {
        objects.placement_key_uavs[current].Get(), objects.placement_uavs[current].Get(), objects.rigid_flags_uav.Get()};
    ID3D11Buffer* rigid_constants = objects.rigid_constants.Get();
    native->CSSetShader(objects.rigid_compute.Get(), nullptr, 0);
    native->CSSetConstantBuffers(0, 1, &rigid_constants);
    native->CSSetShaderResources(0, 1, &records);
    native->CSSetShaderResources(PLACEMENT_SRV_SLOT + 3, 2, previous_table.data());
    native->CSSetUnorderedAccessViews(0, 3, outputs.data(), nullptr);
    native->Dispatch((objects.rigid_record_count + 63) / 64, 1, 1);
    constexpr std::array<ID3D11UnorderedAccessView*, 3> NO_OUTPUTS = {};
    native->CSSetUnorderedAccessViews(0, 3, NO_OUTPUTS.data(), nullptr);
  }

  native->CSSetShader(game_shader.Get(), nullptr, 0);
  ID3D11Buffer* constants = game_constants.Get();
  native->CSSetConstantBuffers(0, 1, &constants);
  native->CSSetShaderResources(0, 1, &game_records_view);
  native->CSSetShaderResources(PLACEMENT_SRV_SLOT + 3, 2, game_table_views.data());
  native->CSSetUnorderedAccessViews(0, static_cast<UINT>(game_uavs.size()), game_uavs.data(), nullptr);
  if (game_records_view != nullptr) {
    game_records_view->Release();
  }
  for (auto* game_view : game_table_views) {
    if (game_view != nullptr) {
      game_view->Release();
    }
  }
  for (auto* game_uav : game_uavs) {
    if (game_uav != nullptr) {
      game_uav->Release();
    }
  }

  if (objects.rigid_record_count != 0) {
    auto& readback = objects.rigid_readbacks[frame % RIGID_READBACKS];
    ComPtr<ID3D11Resource> flags;
    objects.rigid_flags_uav->GetResource(flags.GetAddressOf());
    native->CopyResource(readback.flags.Get(), flags.Get());
    readback.meshes = objects.rigid_draws;
    readback.frame = frame;
  }
}

// The motion targets written in `frame` (none: nothing moved): the redrawn draws' (motion.hlsli MotionTexel), the
// in-draw ones' (the motion in render pixels, the depth: 0 not written).
struct Targets {
  ComPtr<ID3D11ShaderResourceView> redrawn;
  ComPtr<ID3D11ShaderResourceView> drawn_motion;
  ComPtr<ID3D11ShaderResourceView> drawn_depth;
};

inline Targets Target(uint64_t frame) {
  const std::shared_lock lock(internal::state.mutex);
  const auto& objects = internal::state.objects;
  if (objects.motion_frame != frame) return {};
  return {.redrawn = objects.motion_srv, .drawn_motion = objects.drawn_motion_srv, .drawn_depth = objects.drawn_depth_srv};
}

// Whether a rigid mesh (MeshKey) was seen moving lately.
inline bool MovingMesh(uint32_t mesh_key) {
  const std::shared_lock lock(internal::state.mutex);
  const auto& meshes = internal::state.objects.rigid_meshes;
  const auto mesh = meshes.find(mesh_key);
  return mesh != meshes.end() && internal::state.frame < mesh->second.moving_until;
}

// The moving draws' motion (`shaders`: the addon's, kept while installed) with the host's views; with in-draw shaders,
// the game's pixel shaders that can follow them kept as the game creates them.
inline void Install(const Shaders& shaders, MakeView make_view) {
  std::vector<internal::DrawSignature> signatures;
  for (const auto& shader : shaders.shaders) {
    auto& signature = signatures.emplace_back();
    const dxbc::Program program = dxbc::Read(shader.draw_shader);
    if (program.tokens.empty()) continue;
    dxbc::SignatureElement unused;
    dxbc::FindSignatureElement(shader.draw_shader, program, "OSGN", [&](const dxbc::SignatureElement& element, uint32_t reg) {
      if (element.name == "MOTION" && element.index == 0) {
        signature.motion_register = reg;
      }
      signature.outputs.push_back({.name = std::string(element.name), .index = element.index, .reg = reg, .mask = element.mask});
      return false;
    }, &unused);
  }
  const bool in_draw = std::ranges::any_of(signatures, [](const auto& signature) { return signature.motion_register != 0; });
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.shaders = shaders;
    internal::state.make_view = make_view;
    internal::state.draw_signatures = std::move(signatures);
  }
  for (const auto& shader : shaders.shaders) {
    renodx::utils::command_action::Register(
        internal::OnMovingDraw,
        {.shader_hash = shader.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &shader);
  }
  if (in_draw) {
    renodx::utils::pipeline::RegisterOnInitCallback(internal::OnInitPipeline);
    renodx::utils::pipeline::RegisterOnDestroyCallback(internal::OnDestroyPipeline);
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnMovingDraw);
  renodx::utils::pipeline::UnregisterOnInitCallback(internal::OnInitPipeline);
  renodx::utils::pipeline::UnregisterOnDestroyCallback(internal::OnDestroyPipeline);
  SetActive(false);
  const std::unique_lock lock(internal::state.mutex);
  internal::state.shaders = {};
  internal::state.make_view = nullptr;
  internal::state.objects = {};
  internal::state.draw_signatures.clear();
  internal::state.pixel_shaders.clear();
  internal::state.in_draw = {};
}

// The game's device is going away: the motion objects on it with it.
inline void OnDestroyDevice(ID3D11Device* device) {
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.objects.device.Get() == device) {
    internal::state.objects = {};
  }
}

}  // namespace twattila::object_motion
