// Attila's unit motion when the game packs its soldiers' bones more than once a frame (twattila/object_motion.hpp and
// motion_unit_records.cs; graphics_hooks.hpp's PackAnimations calls FlushUnitRecords first) on WARP, through the addon's
// own draw, record and resolve code with its built shaders and the game's weightedcommon vs30_main (0x5F9CD405).
// The game's main pass packs soldiers into its animation pages, draws them, and packs again into the same pages until all
// are drawn (graphics_hooks.hpp). A paused battle: nothing moves, so every soldier's motion must be 0.
// 1. Two packs on one page, soldier A at row 0, then soldier B (another pose) at row 0: A and B 0 px, with the pack hook
//    (A's records recorded before the page is written again) and without it (the write leaves A's records without bones).
// 2. The pack hook on another thread than the draws': nothing recorded there (the write's marking takes over): A and B 0 px.
// 3. A's pose moves 0.2 m between frames 1 and 2 (B's does not), pack hook on: A's motion that move's (-2.56 px in x) in
//    frame 2 and 0 in frame 3, B 0 px.
// 4. 65 pages in one pack, a soldier at row 0 of each, each with its own pose: all 0 px (bone keys never shared).
// Then the fix's cost timed: on the game's thread the texture write check and the pack hook, and a pack round's records on
// 10 pages on the CPU and the GPU (timestamps: real numbers with `hardware` only).
// Built with /DGAME_CALLS_ONLY the game's calls alone reach the addon (no write event, no pack hook), as before the fix.
// tests.exe <the built shaders' folder (motion_unit_*.cso)> <the game's shaders' folder (0x5F9CD405.vs_4_0.cso)> [hardware]
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "src/games/twattila/object_motion.hpp"

using Microsoft::WRL::ComPtr;
namespace om = twattila::object_motion;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

namespace mock {
using namespace reshade::api;

struct CommandList : command_list {
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> native;
  uint64_t get_native() const override { return reinterpret_cast<uint64_t>(native.Get()); }
  device* get_device() override { return nullptr; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { throw std::runtime_error("unexpected mock call: get_private_data"); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { throw std::runtime_error("unexpected mock call: set_private_data"); }
  void barrier(uint32_t count, const resource* resources, const resource_usage* old_states, const resource_usage* new_states) override { throw std::runtime_error("unexpected command list call: barrier"); }
  void begin_render_pass(uint32_t count, const render_pass_render_target_desc* rts, const render_pass_depth_stencil_desc* ds = nullptr) override { throw std::runtime_error("unexpected command list call: begin_render_pass"); }
  void end_render_pass() override { throw std::runtime_error("unexpected command list call: end_render_pass"); }
  void bind_render_targets_and_depth_stencil(uint32_t count, const resource_view* rtvs, resource_view dsv = {0}) override { throw std::runtime_error("unexpected command list call: bind_render_targets_and_depth_stencil"); }
  void bind_pipeline(pipeline_stage stages, pipeline pipeline) override { throw std::runtime_error("unexpected command list call: bind_pipeline"); }
  void bind_pipeline_states(uint32_t count, const dynamic_state* states, const uint32_t* values) override { throw std::runtime_error("unexpected command list call: bind_pipeline_states"); }
  void bind_viewports(uint32_t first, uint32_t count, const viewport* viewports) override { throw std::runtime_error("unexpected command list call: bind_viewports"); }
  void bind_scissor_rects(uint32_t first, uint32_t count, const rect* rects) override { throw std::runtime_error("unexpected command list call: bind_scissor_rects"); }
  void push_constants(shader_stage stages, pipeline_layout layout, uint32_t param, uint32_t first, uint32_t count, const void* values) override { throw std::runtime_error("unexpected command list call: push_constants"); }
  void push_descriptors(shader_stage stages, pipeline_layout layout, uint32_t param, const descriptor_table_update& update) override { throw std::runtime_error("unexpected command list call: push_descriptors"); }
  void bind_descriptor_tables(shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* tables) override { throw std::runtime_error("unexpected command list call: bind_descriptor_tables"); }
  void bind_index_buffer(resource buffer, uint64_t offset, uint32_t index_size) override { throw std::runtime_error("unexpected command list call: bind_index_buffer"); }
  void bind_vertex_buffers(uint32_t first, uint32_t count, const resource* buffers, const uint64_t* offsets, const uint32_t* strides) override { throw std::runtime_error("unexpected command list call: bind_vertex_buffers"); }
  void bind_stream_output_buffers(uint32_t first, uint32_t count, const resource* buffers, const uint64_t* offsets, const uint64_t* max_sizes, const resource* counter_buffers, const uint64_t* counter_offsets) override { throw std::runtime_error("unexpected command list call: bind_stream_output_buffers"); }
  void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) override { throw std::runtime_error("unexpected command list call: draw"); }
  void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) override { throw std::runtime_error("unexpected command list call: draw_indexed"); }
  void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override { throw std::runtime_error("unexpected command list call: dispatch"); }
  void draw_or_dispatch_indirect(indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride) override { throw std::runtime_error("unexpected command list call: draw_or_dispatch_indirect"); }
  void copy_resource(resource source, resource dest) override { throw std::runtime_error("unexpected command list call: copy_resource"); }
  void copy_buffer_region(resource source, uint64_t source_offset, resource dest, uint64_t dest_offset, uint64_t size) override { throw std::runtime_error("unexpected command list call: copy_buffer_region"); }
  void copy_buffer_to_texture(resource source, uint64_t source_offset, uint32_t row_length, uint32_t slice_height, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override { throw std::runtime_error("unexpected command list call: copy_buffer_to_texture"); }
  void copy_texture_region(resource source, uint32_t source_subresource, const subresource_box* source_box, resource dest, uint32_t dest_subresource, const subresource_box* dest_box, filter_mode filter = filter_mode::min_mag_mip_point) override { throw std::runtime_error("unexpected command list call: copy_texture_region"); }
  void copy_texture_to_buffer(resource source, uint32_t source_subresource, const subresource_box* source_box, resource dest, uint64_t dest_offset, uint32_t row_length = 0, uint32_t slice_height = 0) override { throw std::runtime_error("unexpected command list call: copy_texture_to_buffer"); }
  void resolve_texture_region(resource source, uint32_t source_subresource, const subresource_box* source_box, resource dest, uint32_t dest_subresource, uint32_t dest_x, uint32_t dest_y, uint32_t dest_z, format format) override { throw std::runtime_error("unexpected command list call: resolve_texture_region"); }
  void clear_depth_stencil_view(resource_view dsv, const float* depth, const uint8_t* stencil, uint32_t rect_count = 0, const rect* rects = nullptr) override { throw std::runtime_error("unexpected command list call: clear_depth_stencil_view"); }
  void clear_render_target_view(resource_view rtv, const float color[4], uint32_t rect_count = 0, const rect* rects = nullptr) override { throw std::runtime_error("unexpected command list call: clear_render_target_view"); }
  void clear_unordered_access_view_uint(resource_view uav, const uint32_t values[4], uint32_t rect_count = 0, const rect* rects = nullptr) override { throw std::runtime_error("unexpected command list call: clear_unordered_access_view_uint"); }
  void clear_unordered_access_view_float(resource_view uav, const float values[4], uint32_t rect_count = 0, const rect* rects = nullptr) override { throw std::runtime_error("unexpected command list call: clear_unordered_access_view_float"); }
  void generate_mipmaps(resource_view srv) override { throw std::runtime_error("unexpected command list call: generate_mipmaps"); }
  void begin_query(query_heap heap, query_type type, uint32_t index) override { throw std::runtime_error("unexpected command list call: begin_query"); }
  void end_query(query_heap heap, query_type type, uint32_t index) override { throw std::runtime_error("unexpected command list call: end_query"); }
  void copy_query_heap_results(query_heap heap, query_type type, uint32_t first, uint32_t count, resource dest, uint64_t dest_offset, uint32_t stride) override { throw std::runtime_error("unexpected command list call: copy_query_heap_results"); }
  void begin_debug_event(const char* label, const float color[4] = nullptr) override { throw std::runtime_error("unexpected command list call: begin_debug_event"); }
  void end_debug_event() override { throw std::runtime_error("unexpected command list call: end_debug_event"); }
  void insert_debug_marker(const char* label, const float color[4] = nullptr) override { throw std::runtime_error("unexpected command list call: insert_debug_marker"); }
  void dispatch_mesh(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override { throw std::runtime_error("unexpected command list call: dispatch_mesh"); }
  void dispatch_rays(resource raygen, uint64_t raygen_offset, uint64_t raygen_size, resource miss, uint64_t miss_offset, uint64_t miss_size, uint64_t miss_stride, resource hit_group, uint64_t hit_group_offset, uint64_t hit_group_size, uint64_t hit_group_stride, resource callable, uint64_t callable_offset, uint64_t callable_size, uint64_t callable_stride, uint32_t width, uint32_t height, uint32_t depth) override { throw std::runtime_error("unexpected command list call: dispatch_rays"); }
  void copy_acceleration_structure(resource_view source, resource_view dest, acceleration_structure_copy_mode mode) override { throw std::runtime_error("unexpected command list call: copy_acceleration_structure"); }
  void build_acceleration_structure(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input* inputs, resource scratch, uint64_t scratch_offset, resource_view source, resource_view dest, acceleration_structure_build_mode mode) override { throw std::runtime_error("unexpected command list call: build_acceleration_structure"); }
  void query_acceleration_structures(uint32_t count, const resource_view* acceleration_structures, query_heap heap, query_type type, uint32_t first) override { throw std::runtime_error("unexpected command list call: query_acceleration_structures"); }
  void update_buffer_region(const void* data, resource dest, uint64_t dest_offset, uint64_t size) override { throw std::runtime_error("unexpected command list call: update_buffer_region"); }
  void update_texture_region(const subresource_data& data, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override { throw std::runtime_error("unexpected command list call: update_texture_region"); }
};
}  // namespace mock

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static mock::CommandList cmd_list;

constexpr UINT VIEW_SIZE = 256;
constexpr float DEPTH = 10.f;
constexpr UINT PAGE_WIDTH = 512;
constexpr UINT PAGE_ROWS = 8;
constexpr float HALF_QUAD = 0.3f;
// The camera at the origin looking down +z (x right, y up), near 1, far 100: clip = (x, y, z 100 / 99 - 100 / 99, z).
constexpr std::array<float, 16> CAMERA_ROWS = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
                                               0.f, 0.f, 100.f / 99.f, -100.f / 99.f, 0.f, 0.f, 1.f, 0.f};
constexpr uint32_t GAME_VS_HASH = 0x5F9CD405;

static uint64_t current_frame = 0;

static bool MakeView(ID3D11Buffer*, om::View* view) {
  *view = {.frame = current_frame,
           .width = VIEW_SIZE,
           .height = VIEW_SIZE,
           .constants = {.current = CAMERA_ROWS,
                         .previous = CAMERA_ROWS,
                         .times = {0.f, 0.f, static_cast<float>(VIEW_SIZE), static_cast<float>(VIEW_SIZE)}}};
  return true;
}

static std::vector<uint8_t> Load(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static float HalfToFloat(uint32_t half) {
  const uint32_t sign = (half >> 15) & 1u;
  const int32_t exponent = static_cast<int32_t>((half >> 10) & 0x1Fu);
  const uint32_t mantissa = half & 0x3FFu;
  float value = 0.f;
  if (exponent == 0) {
    value = std::ldexp(static_cast<float>(mantissa), -24);
  } else if (exponent == 31) {
    value = INFINITY;
  } else {
    value = std::ldexp(static_cast<float>(mantissa | 0x400u), exponent - 25);
  }
  return (sign != 0u ? -value : value);
}

// A soldier's bone 0: its rows moved by `shift` (metres).
struct Pose {
  float x = 0.f;
  float y = 0.f;
};

struct Page {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
};

// The motion shaders the addon would install (Attila's 0x5F9CD405 entry and its unit shaders).
static std::vector<uint8_t> motion_unit_code;
static std::vector<uint8_t> unit_point_code;
static std::vector<uint8_t> unit_recorded_code;
static std::vector<uint8_t> unit_records_code;
static std::array<om::MotionShader, 1> motion_shaders;

static ComPtr<ID3D11VertexShader> game_vertex_shader;
static ComPtr<ID3D11PixelShader> game_pixel_shader;
static ComPtr<ID3D11InputLayout> layout;
static ComPtr<ID3D11Buffer> vertices;
static ComPtr<ID3D11Buffer> indices;
static ComPtr<ID3D11Buffer> instances;
static ComPtr<ID3D11Buffer> camera;
static ComPtr<ID3D11Buffer> config;
static ComPtr<ID3D11Buffer> animation_size;
static ComPtr<ID3D11Texture2D> color;
static ComPtr<ID3D11RenderTargetView> color_view;
static ComPtr<ID3D11Texture2D> depth;
static ComPtr<ID3D11DepthStencilView> depth_view;
static ComPtr<ID3D11DepthStencilState> depth_less;
static ComPtr<ID3D11RasterizerState> raster;
static UINT instance_stride = 0;
static UINT vertex_stride = 0;
static std::vector<std::array<float, 3>> placements;  // each instance's place (x, y, z)

// The pack hook (graphics_hooks.hpp's OnPackAnimations): on this thread, another, or not at all. The CPU time of its calls
// that recorded records, and of the page writes' events that found a page with records to mark.
enum class Hook : uint8_t { NONE, SAME_THREAD, OTHER_THREAD };
static Hook hook = Hook::NONE;
static double flush_seconds = 0.0;
static uint32_t flushes = 0;
static double marking_seconds = 0.0;
static uint32_t markings = 0;

static void BeforePack() {
#ifndef GAME_CALLS_ONLY
  if (hook == Hook::SAME_THREAD) {
    const bool pending = om::internal::unit_records_pending.load(std::memory_order_relaxed);
    const auto start = std::chrono::steady_clock::now();
    om::FlushUnitRecords();
    if (pending) {
      flush_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      ++flushes;
    }
  } else if (hook == Hook::OTHER_THREAD) {
    std::thread([] { om::FlushUnitRecords(); }).join();
  }
#endif
}

// The game's pack of a page: rows of soldiers (row, pose), written through Map as the game's dynamic pages are; ReShade
// raises map_texture_region after the Map.
static void WritePage(const Page& page, const std::vector<std::pair<uint32_t, Pose>>& rows) {
  BeforePack();
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(page.texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) throw std::runtime_error("page map");
#ifndef GAME_CALLS_ONLY
  const bool pending = om::internal::unit_records_pending.load(std::memory_order_relaxed);
  const auto start = std::chrono::steady_clock::now();
  om::internal::OnMapTextureRegion(nullptr, {reinterpret_cast<uint64_t>(page.texture.Get())}, 0, nullptr,
                                   reshade::api::map_access::write_discard, nullptr);
  if (pending) {
    marking_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ++markings;
  }
#endif
  for (UINT row = 0; row < PAGE_ROWS; ++row) {
    std::memset(static_cast<uint8_t*>(mapped.pData) + (row * mapped.RowPitch), 0, PAGE_WIDTH * 16);
  }
  for (const auto& [row, pose] : rows) {
    auto* texels = reinterpret_cast<float*>(static_cast<uint8_t*>(mapped.pData) + (row * mapped.RowPitch));
    const std::array<float, 12> bone = {1.f, 0.f, 0.f, pose.x, 0.f, 1.f, 0.f, pose.y, 0.f, 0.f, 1.f, 0.f};
    std::memcpy(texels, bone.data(), sizeof(bone));
  }
  context->Unmap(page.texture.Get(), 0);
}

static Page MakePage() {
  Page page;
  const D3D11_TEXTURE2D_DESC desc = {.Width = PAGE_WIDTH, .Height = PAGE_ROWS, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DYNAMIC,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  device->CreateTexture2D(&desc, nullptr, page.texture.GetAddressOf());
  device->CreateShaderResourceView(page.texture.Get(), nullptr, page.view.GetAddressOf());
  return page;
}

// The addon's motion draws' CPU time, from each case's second frame (the first makes the tables).
static double draw_seconds = 0.0;
static uint32_t draws = 0;
static bool timing = false;

// The game's draw of one soldier (instance `instance`, its bones at row 0 of `page`), then the addon's motion draw after it
// (object_motion's post callback).
static void DrawSoldier(const Page& page, UINT instance) {
  context->IASetInputLayout(layout.Get());
  const std::array<ID3D11Buffer*, 2> buffers = {vertices.Get(), instances.Get()};
  const std::array<UINT, 2> strides = {vertex_stride, instance_stride};
  const std::array<UINT, 2> offsets = {0, 0};
  context->IASetVertexBuffers(0, 2, buffers.data(), strides.data(), offsets.data());
  context->IASetIndexBuffer(indices.Get(), DXGI_FORMAT_R16_UINT, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
  const std::array<ID3D11Buffer*, 3> constants = {camera.Get(), config.Get(), animation_size.Get()};
  context->VSSetConstantBuffers(0, 3, constants.data());
  ID3D11ShaderResourceView* bones = page.view.Get();
  context->VSSetShaderResources(0, 1, &bones);
  ID3D11RenderTargetView* target = color_view.Get();
  context->OMSetRenderTargets(1, &target, depth_view.Get());
  context->OMSetDepthStencilState(depth_less.Get(), 0);
  context->RSSetState(raster.Get());
  const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(VIEW_SIZE), static_cast<float>(VIEW_SIZE), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->DrawIndexedInstanced(6, 1, 0, 0, instance);
  renodx::utils::command_action::CommandContext<renodx::utils::command_action::DrawIndexedArguments> command = {
      .cmd_list = &cmd_list,
      .arguments = {.index_count = 6, .instance_count = 1, .first_index = 0, .vertex_offset = 0, .first_instance = instance},
      .callback_data = motion_shaders.data(),
  };
  const auto start = std::chrono::steady_clock::now();
  om::internal::DrawObjectMotion(command, nullptr);
  if (timing) {
    draw_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ++draws;
  }
}

static void BeginFrame() {
  ++current_frame;
  constexpr std::array<float, 4> CLEAR = {};
  context->ClearRenderTargetView(color_view.Get(), CLEAR.data());
  context->ClearDepthStencilView(depth_view.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
}

// The motion written this frame at the soldiers' centres (each instance's place moved by its pose): the largest of a 3 x 3
// block, and whether all of it was written.
struct Motion {
  float x = 0.f;
  float y = 0.f;
  bool written = false;
};

static std::vector<Motion> ReadMotion(const std::vector<std::pair<UINT, Pose>>& soldiers) {
  std::vector<Motion> result(soldiers.size());
  const auto targets = om::Target(current_frame);
  if (!targets.redrawn) return result;
  ComPtr<ID3D11Resource> resource;
  targets.redrawn->GetResource(resource.GetAddressOf());
  const D3D11_TEXTURE2D_DESC desc = {.Width = VIEW_SIZE, .Height = VIEW_SIZE, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32_UINT,
                                     .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), resource.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return result;
  for (size_t soldier = 0; soldier < soldiers.size(); ++soldier) {
    const auto& [instance, pose] = soldiers[soldier];
    const float world_x = placements[instance][0] + pose.x;
    const float world_y = placements[instance][1] + pose.y;
    const float world_z = placements[instance][2];
    const int center_x = static_cast<int>(((world_x / world_z) * 0.5f + 0.5f) * VIEW_SIZE);
    const int center_y = static_cast<int>((0.5f - (world_y / world_z) * 0.5f) * VIEW_SIZE);
    Motion motion = {.written = true};
    for (int y = center_y - 1; y <= center_y + 1; ++y) {
      for (int x = center_x - 1; x <= center_x + 1; ++x) {
        const auto* texel = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch) + (x * 8));
        motion.written = motion.written && texel[1] != 0u;
        const float motion_x = HalfToFloat(texel[0] & 0xFFFFu);
        const float motion_y = HalfToFloat(texel[0] >> 16);
        if (std::abs(motion_x) > std::abs(motion.x)) motion.x = motion_x;
        if (std::abs(motion_y) > std::abs(motion.y)) motion.y = motion_y;
      }
    }
    result[soldier] = motion;
  }
  context->Unmap(staging.Get(), 0);
  return result;
}

// A fresh start of the addon's motion objects (as at a new device), the pack hook as given.
static void Restart(Hook pack_hook) {
  om::internal::state.objects = {};
  om::internal::state.uav_overlap = {.loaded = true};  // no NVIDIA UAV overlap on WARP
  hook = pack_hook;
}

static bool Near(float value, float expected) {
  return std::abs(value - expected) < 0.02f;
}

// A move of `metres` in x at `depth` in render pixels (motion: from now to where it was).
static float MovedPixels(float metres, float depth) {
  return -metres / depth * 0.5f * VIEW_SIZE;
}

// Cases 1-3: two packs on page 0 a frame, A (instance 0) then B (instance 1), both at row 0; A's pose `a_moved` from frame
// 2, both soldiers' poses 0.1 m further in x in frame 4 (their bones were found: not 0 by a missed look-up). A keeps its
// bones only with the pack hook on the draws' thread; else its records record no bones (this frame's pose: 0).
static int TwoPacks(const char* name, Hook pack_hook, Pose a_moved) {
  Restart(pack_hook);
  const Page page = MakePage();
  const bool a_bones = (pack_hook == Hook::SAME_THREAD);
  constexpr Pose B_POSE = {.x = 0.f, .y = 0.5f};
  constexpr float STEP = 0.1f;
  int failures = 0;
  for (int frame = 1; frame <= 4; ++frame) {
    BeginFrame();
    timing = (frame >= 2);
    Pose a_pose = (frame >= 2 ? a_moved : Pose{});
    Pose b_pose = B_POSE;
    if (frame == 4) {
      a_pose.x += STEP;
      b_pose.x += STEP;
    }
    WritePage(page, {{0u, a_pose}});
    DrawSoldier(page, 0);
    WritePage(page, {{0u, b_pose}});
    DrawSoldier(page, 1);
    om::Resolve(context.Get(), current_frame);
    const auto motion = ReadMotion({{0u, a_pose}, {1u, b_pose}});
    if (frame == 1) continue;
    float expected_a = 0.f;
    if (a_bones && frame == 2) {
      expected_a = MovedPixels(a_moved.x, DEPTH);
    } else if (a_bones && frame == 4) {
      expected_a = MovedPixels(STEP, DEPTH);
    }
    const float expected_b = (frame == 4 ? MovedPixels(STEP, DEPTH) : 0.f);
    const bool good = motion[0].written && motion[1].written && Near(motion[0].x, expected_a) && Near(motion[0].y, 0.f)
                      && Near(motion[1].x, expected_b) && Near(motion[1].y, 0.f);
    std::printf("%s frame %d: A (%.3f, %.3f) px expected (%.3f, 0), B (%.3f, %.3f) px expected (%.3f, 0), written %d %d: %s\n", name,
                frame, motion[0].x, motion[0].y, expected_a, motion[1].x, motion[1].y, expected_b, motion[0].written, motion[1].written,
                good ? "OK" : "WRONG");
    failures += (good ? 0 : 1);
  }
  return failures;
}

// Case 4: 65 pages in one pack at the frame's start, soldier k (instance 2 + k) at row 0 of page k with its own pose: frames
// 2 and 3 all 0; frame 4 every pose 0.1 m further in x: each soldier that move's motion (its own bones found, no other's).
static int SixtyFivePages() {
  Restart(Hook::SAME_THREAD);
  constexpr UINT PAGES = 65;
  constexpr float STEP = 0.1f;
  std::vector<Page> pages;
  for (UINT index = 0; index < PAGES; ++index) {
    pages.push_back(MakePage());
  }
  int failures = 0;
  for (int frame = 1; frame <= 4; ++frame) {
    BeginFrame();
    timing = (frame >= 2);
    std::vector<std::pair<UINT, Pose>> soldiers;
    for (UINT index = 0; index < PAGES; ++index) {
      const Pose pose = {.x = 0.004f * static_cast<float>(index) + (frame == 4 ? STEP : 0.f), .y = 0.f};
      WritePage(pages[index], {{0u, pose}});
      soldiers.emplace_back(2 + index, pose);
    }
    for (UINT index = 0; index < PAGES; ++index) {
      DrawSoldier(pages[index], 2 + index);
    }
    om::Resolve(context.Get(), current_frame);
    const auto motion = ReadMotion(soldiers);
    if (frame == 1) continue;
    float worst = 0.f;
    size_t worst_soldier = 0;
    bool written = true;
    for (size_t soldier = 0; soldier < motion.size(); ++soldier) {
      written = written && motion[soldier].written;
      const float expected = (frame == 4 ? MovedPixels(STEP, placements[soldiers[soldier].first][2]) : 0.f);
      const float off = std::max(std::abs(motion[soldier].x - expected), std::abs(motion[soldier].y));
      if (off > worst) {
        worst = off;
        worst_soldier = soldier;
      }
    }
    const bool good = written && worst < 0.02f;
    std::printf("65 pages frame %d: largest error %.3f px (soldier %zu, motion %.3f), all written %d: %s\n", frame, worst, worst_soldier,
                motion[worst_soldier].x, written, good ? "OK" : "WRONG");
    failures += (good ? 0 : 1);
  }
  return failures;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: tests.exe <built shaders folder> <game shaders folder> [hardware]\n");
    return 2;
  }
  const std::string built = argv[1];
  const std::string game = argv[2];
  // "hardware": the same on the GPU's driver, for its CPU timings (WARP's runtime is slower).
  const bool hardware = (argc > 3 && std::string(argv[3]) == "hardware");
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, (hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP), nullptr, 0, &level, 1,
                                    D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf())));
  std::printf("device: %s\n", (hardware ? "hardware" : "WARP"));
  cmd_list.native = context;
  motion_unit_code = Load(built + "\\motion_unit_sm3.cso");
  unit_point_code = Load(built + "\\motion_unit_point.cso");
  unit_recorded_code = Load(built + "\\motion_unit_recorded.cso");
  unit_records_code = Load(built + "\\motion_unit_records.cso");
  const auto game_code = Load(game + "\\0x5F9CD405.vs_4_0.cso");
  CHECK(!motion_unit_code.empty() && !unit_point_code.empty() && !unit_recorded_code.empty() && !unit_records_code.empty() && !game_code.empty());
  motion_shaders[0] = {.hash = GAME_VS_HASH, .motion_shader = motion_unit_code, .kind = om::MotionKind::UNIT, .record_shader = unit_point_code};
  om::internal::state.shaders = {.shaders = motion_shaders, .unit_pixel_shader = unit_recorded_code, .unit_records_compute_shader = unit_records_code};
  om::internal::state.make_view = &MakeView;

  CHECK(SUCCEEDED(device->CreateVertexShader(game_code.data(), game_code.size(), nullptr, game_vertex_shader.GetAddressOf())));
  {
    constexpr char PIXEL[] = "float4 main(float4 position : SV_Position) : SV_Target { return 1.f; }";
    ComPtr<ID3DBlob> blob;
    CHECK(SUCCEEDED(D3DCompile(PIXEL, sizeof(PIXEL) - 1, nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, blob.GetAddressOf(), nullptr)));
    CHECK(SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, game_pixel_shader.GetAddressOf())));
  }
  // The game's inputs: POSITION0-5 per vertex (slot 0), TEXCOORD0-7 per instance (slot 1), all four floats.
  ComPtr<ID3D11ShaderReflection> reflection;
  CHECK(SUCCEEDED(D3DReflect(game_code.data(), game_code.size(), IID_PPV_ARGS(reflection.GetAddressOf()))));
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  std::vector<std::string> names;
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  for (UINT index = 0; index < shader_desc.InputParameters; ++index) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetInputParameterDesc(index, &parameter);
    if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
    names.emplace_back(parameter.SemanticName);
    const bool per_instance = names.back() == "TEXCOORD";
    elements.push_back({.SemanticIndex = parameter.SemanticIndex, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                        .InputSlot = (per_instance ? 1u : 0u), .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT,
                        .InputSlotClass = (per_instance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA),
                        .InstanceDataStepRate = (per_instance ? 1u : 0u)});
    if (per_instance) {
      instance_stride += 16;
    } else {
      vertex_stride += 16;
    }
  }
  for (size_t index = 0; index < elements.size(); ++index) {
    elements[index].SemanticName = names[index].c_str();
  }
  CHECK(vertex_stride == 6 * 16 && instance_stride == 8 * 16);
  CHECK(SUCCEEDED(device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), game_code.data(), game_code.size(),
                                            layout.GetAddressOf())));

  // A quad on bone 0 (POSITION1: weight 1 on the bone in its z), its normals and the rest zero.
  {
    std::vector<float> data(4 * 6 * 4, 0.f);
    const std::array<std::array<float, 2>, 4> corners = {{{-HALF_QUAD, -HALF_QUAD}, {HALF_QUAD, -HALF_QUAD}, {HALF_QUAD, HALF_QUAD}, {-HALF_QUAD, HALF_QUAD}}};
    for (size_t vertex = 0; vertex < 4; ++vertex) {
      float* out = &data[vertex * 24];
      out[0] = corners[vertex][0];
      out[1] = corners[vertex][1];
      out[3] = 1.f;
      out[4] = 1.f;
    }
    const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(data.size() * 4), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
    const D3D11_SUBRESOURCE_DATA init = {.pSysMem = data.data()};
    CHECK(SUCCEEDED(device->CreateBuffer(&desc, &init, vertices.GetAddressOf())));
    const std::array<uint16_t, 6> quad = {0, 1, 2, 0, 2, 3};
    const D3D11_BUFFER_DESC index_desc = {.ByteWidth = sizeof(quad), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_INDEX_BUFFER};
    const D3D11_SUBRESOURCE_DATA index_init = {.pSysMem = quad.data()};
    CHECK(SUCCEEDED(device->CreateBuffer(&index_desc, &index_init, indices.GetAddressOf())));
  }
  // Instances: A at x -3, B at x +3 (cases 1-3), then 65 on a 9 x 8 grid a metre apart, each row 1.5 m further away (case 4:
  // the placement table is by x and z, y up; soldiers stacked at one x and z would share its cells' 8 slots); bones at row 0.
  placements = {{-3.f, 0.f, DEPTH}, {3.f, 0.f, DEPTH}};
  for (UINT index = 0; index < 65; ++index) {
    const auto row = static_cast<float>(index / 9);
    placements.push_back({-4.f + static_cast<float>(index % 9), -3.5f + row, DEPTH + (1.5f * row)});
  }
  {
    std::vector<float> data(placements.size() * 8 * 4, 0.f);
    for (size_t instance = 0; instance < placements.size(); ++instance) {
      float* rows = &data[instance * 32];
      const std::array<float, 12> placed = {1.f, 0.f, 0.f, placements[instance][0], 0.f, 1.f, 0.f, placements[instance][1],
                                            0.f, 0.f, 1.f, placements[instance][2]};
      std::memcpy(rows, placed.data(), sizeof(placed));
    }
    const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(data.size() * 4), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
    const D3D11_SUBRESOURCE_DATA init = {.pSysMem = data.data()};
    CHECK(SUCCEEDED(device->CreateBuffer(&desc, &init, instances.GetAddressOf())));
  }
  const auto constant_buffer = [&](const void* data, UINT bytes, ComPtr<ID3D11Buffer>* buffer) {
    std::vector<uint8_t> padded((bytes + 15) / 16 * 16, 0);
    std::memcpy(padded.data(), data, bytes);
    const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(padded.size()), .Usage = D3D11_USAGE_DEFAULT,
                                    .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA init = {.pSysMem = padded.data()};
    return SUCCEEDED(device->CreateBuffer(&desc, &init, buffer->GetAddressOf()));
  };
  // camera_VS_PS: view_projection at c9-c12; rigid_config_PS zero; variant_animation_size: the page's height and width.
  std::array<float, 36 * 4> camera_values = {};
  std::memcpy(&camera_values[9 * 4], CAMERA_ROWS.data(), sizeof(CAMERA_ROWS));
  const std::array<float, 20> config_values = {};
  const std::array<float, 4> size_values = {static_cast<float>(PAGE_ROWS), static_cast<float>(PAGE_ROWS), static_cast<float>(PAGE_WIDTH),
                                            static_cast<float>(PAGE_WIDTH)};
  CHECK(constant_buffer(camera_values.data(), sizeof(camera_values), &camera));
  CHECK(constant_buffer(config_values.data(), sizeof(config_values), &config));
  CHECK(constant_buffer(size_values.data(), sizeof(size_values), &animation_size));
  {
    const D3D11_TEXTURE2D_DESC color_desc = {.Width = VIEW_SIZE, .Height = VIEW_SIZE, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                             .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
    CHECK(SUCCEEDED(device->CreateTexture2D(&color_desc, nullptr, color.GetAddressOf())));
    CHECK(SUCCEEDED(device->CreateRenderTargetView(color.Get(), nullptr, color_view.GetAddressOf())));
    const D3D11_TEXTURE2D_DESC depth_desc = {.Width = VIEW_SIZE, .Height = VIEW_SIZE, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_D24_UNORM_S8_UINT,
                                             .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_DEPTH_STENCIL};
    CHECK(SUCCEEDED(device->CreateTexture2D(&depth_desc, nullptr, depth.GetAddressOf())));
    CHECK(SUCCEEDED(device->CreateDepthStencilView(depth.Get(), nullptr, depth_view.GetAddressOf())));
    const D3D11_DEPTH_STENCIL_DESC less = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL, .DepthFunc = D3D11_COMPARISON_LESS};
    CHECK(SUCCEEDED(device->CreateDepthStencilState(&less, depth_less.GetAddressOf())));
    const D3D11_RASTERIZER_DESC raster_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
    CHECK(SUCCEEDED(device->CreateRasterizerState(&raster_desc, raster.GetAddressOf())));
  }

  int failures = 0;
  failures += TwoPacks("1a two packs, pack hook", Hook::SAME_THREAD, {});
  failures += TwoPacks("1b two packs, no pack hook", Hook::NONE, {});
  failures += TwoPacks("2  two packs, pack hook on another thread", Hook::OTHER_THREAD, {});
  failures += TwoPacks("3  two packs, A moves 0.2 m", Hook::SAME_THREAD, {.x = 0.2f, .y = 0.f});
  failures += SixtyFivePages();
  std::printf("the addon's unit motion draw: %.2f us a draw (%u draws, the D3D runtime's calls included)\n", draw_seconds / std::max(draws, 1u) * 1e6,
              draws);

#ifndef GAME_CALLS_ONLY
  // The fix's cost on the game's thread: the pack hook when it records (its dispatches and the compute bindings put back),
  // a page's write that marks records, then over many calls a texture write that is no page (mid-frame with 60 pages listed:
  // 6 pack rounds of Attila's 10; and after the frame's end, none) and the pack hook with nothing to record (the common case).
  std::printf("the pack hook recording: %.2f us a call (%u calls, the D3D runtime's calls included); a page write marking records: %.2f us (%u)\n",
              flush_seconds / std::max(flushes, 1u) * 1e6, flushes, marking_seconds / std::max(markings, 1u) * 1e6, markings);
  {
    constexpr int CALLS = 1000000;
    const uint64_t other = reinterpret_cast<uint64_t>(color.Get());
    const auto time_writes = [&] {
      const auto start = std::chrono::steady_clock::now();
      for (int call = 0; call < CALLS; ++call) {
        om::internal::OnMapTextureRegion(nullptr, {other}, 0, nullptr, reshade::api::map_access::write_discard, nullptr);
      }
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / CALLS * 1e9;
    };
    Restart(Hook::SAME_THREAD);
    BeginFrame();
    std::vector<Page> pages;
    for (UINT index = 0; index < 60; ++index) {
      pages.push_back(MakePage());
      WritePage(pages.back(), {{0u, Pose{}}});
      DrawSoldier(pages.back(), 2 + index);
    }
    const double listed_ns = time_writes();
    om::Resolve(context.Get(), current_frame);
    const double empty_ns = time_writes();
    const auto start = std::chrono::steady_clock::now();
    for (int call = 0; call < CALLS; ++call) {
      om::FlushUnitRecords();
    }
    const double idle_ns = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / CALLS * 1e9;
    std::printf("a texture write that is no page: %.1f ns with 60 pages listed, %.1f ns after the frame's end; the pack hook with nothing to record: %.1f ns\n",
                listed_ns, empty_ns, idle_ns);

    // A pack round's records: soldiers drawn on 1 page, then on 10 (a battle's round: a dispatch each), recorded by the pack
    // hook; its CPU time and its GPU time between two timestamps, the GPU idle before, and an empty interval's just before it
    // (medians of 100 frames).
    ComPtr<ID3D11Query> idle;
    ComPtr<ID3D11Query> disjoint;
    std::array<ComPtr<ID3D11Query>, 3> stamps;
    D3D11_QUERY_DESC query = {.Query = D3D11_QUERY_EVENT};
    CHECK(SUCCEEDED(device->CreateQuery(&query, idle.GetAddressOf())));
    query.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    CHECK(SUCCEEDED(device->CreateQuery(&query, disjoint.GetAddressOf())));
    query.Query = D3D11_QUERY_TIMESTAMP;
    for (auto& stamp : stamps) {
      CHECK(SUCCEEDED(device->CreateQuery(&query, stamp.GetAddressOf())));
    }
    const auto median = [](std::vector<double> values) {
      if (values.empty()) return 0.0;
      std::ranges::nth_element(values, values.begin() + (values.size() / 2));
      return values[values.size() / 2];
    };
    for (const UINT page_count : {1u, 10u}) {
      std::vector<double> round_cpu_us;
      std::vector<double> round_gpu_us;
      std::vector<double> empty_gpu_us;
      for (int round = 0; round < 100; ++round) {
        BeginFrame();
        for (UINT index = 0; index < page_count; ++index) {
          DrawSoldier(pages[index], 2 + index);
        }
        context->End(idle.Get());
        BOOL done = FALSE;
        while (context->GetData(idle.Get(), &done, sizeof(done), 0) == S_FALSE) {}
        context->Begin(disjoint.Get());
        context->End(stamps[0].Get());
        context->End(stamps[1].Get());
        const auto round_start = std::chrono::steady_clock::now();
        om::FlushUnitRecords();
        round_cpu_us.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - round_start).count() * 1e6);
        CHECK(om::internal::state.objects.unit_record_draws_recorded == page_count);
        context->End(stamps[2].Get());
        context->End(disjoint.Get());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency = {};
        std::array<uint64_t, 3> ticks = {};
        while (context->GetData(disjoint.Get(), &frequency, sizeof(frequency), 0) == S_FALSE) {}
        for (size_t stamp = 0; stamp < stamps.size(); ++stamp) {
          while (context->GetData(stamps[stamp].Get(), &ticks[stamp], sizeof(ticks[stamp]), 0) == S_FALSE) {}
        }
        if (!frequency.Disjoint && frequency.Frequency != 0) {
          const double tick_us = 1e6 / static_cast<double>(frequency.Frequency);
          empty_gpu_us.push_back(static_cast<double>(ticks[1] - ticks[0]) * tick_us);
          round_gpu_us.push_back(static_cast<double>(ticks[2] - ticks[1]) * tick_us);
        }
        om::Resolve(context.Get(), current_frame);
      }
      std::printf("a pack round's records on %u page%s (a dispatch each): %.2f us CPU, %.2f us GPU (an empty interval: %.2f us; medians of %zu)\n",
                  page_count, (page_count == 1 ? "" : "s"), median(round_cpu_us), median(round_gpu_us), median(empty_gpu_us), round_gpu_us.size());
    }
  }
#endif
  std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
