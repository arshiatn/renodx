/*
 * Copyright (C) 2026 RenoDX contributors
 * SPDX-License-Identifier: MIT
 *
 * From an x64 VS developer prompt at the repository root, after configuring
 * the addon target (join the compiler lines into one command):
 * clang-cl /nologo /std:c++20 /EHsc /MTd /DWIN32=1 /D_WINDOWS /D_DEBUG
 *   /D_ITERATOR_DEBUG_LEVEL=0 /DNOMINMAX /Ibuild/expeditionsrome_dlss.include
 *   /Iexternal/Detours/include /Iexternal/json/include /Iexternal/frozen/include
 *   /Iexternal/gtl/include /Iexternal/DLSS/include /Iexternal/reshade
 *   src/games/expeditionsrome_dlss/tests/addon_on_command.cpp
 *   /Fobuild/addon_on_command.obj /Febuild/addon_on_command.exe
 *   /link d3d11.lib dxgi.lib user32.lib advapi32.lib
 *   external/DLSS/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg_iterator0.lib
 * Run: build\addon_on_command.exe
 * No game launch, ReShade registration, or NVIDIA initialization.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include <d3d11_1.h>
#include <wrl/client.h>

// Exercise the actual pre/post/finalize callback with real WARP resources.
// ReShade interfaces below are typed mocks, not fabricated vtables. This host
// never attaches the addon, runs a game shader, or initializes NVIDIA.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "../addon.cpp"

namespace mock {
using namespace reshade::api;

struct Device : device {
  Microsoft::WRL::ComPtr<ID3D11Device> native;
  uint64_t get_native() const override { return reinterpret_cast<uint64_t>(native.Get()); }
  device_api get_api() const override { return device_api::d3d11; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { throw std::runtime_error("unexpected mock call: get_private_data"); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { throw std::runtime_error("unexpected mock call: set_private_data"); }
  bool check_capability(device_caps capability) const override { throw std::runtime_error("unexpected device call: check_capability"); }
  bool check_format_support(format format, resource_usage usage) const override { throw std::runtime_error("unexpected device call: check_format_support"); }
  bool create_sampler(const sampler_desc& desc, sampler* out_sampler) override { throw std::runtime_error("unexpected device call: create_sampler"); }
  void destroy_sampler(sampler sampler) override { throw std::runtime_error("unexpected device call: destroy_sampler"); }
  bool create_resource(const resource_desc& desc, const subresource_data* initial_data, resource_usage initial_state, resource* out_resource, void** shared_handle = nullptr) override { throw std::runtime_error("unexpected device call: create_resource"); }
  void destroy_resource(resource resource) override { throw std::runtime_error("unexpected device call: destroy_resource"); }
  resource_desc get_resource_desc(resource resource) const override { throw std::runtime_error("unexpected device call: get_resource_desc"); }
  bool create_resource_view(resource resource, resource_usage usage_type, const resource_view_desc& desc, resource_view* out_view) override { throw std::runtime_error("unexpected device call: create_resource_view"); }
  void destroy_resource_view(resource_view view) override { throw std::runtime_error("unexpected device call: destroy_resource_view"); }
  resource get_resource_from_view(resource_view view) const override { throw std::runtime_error("unexpected device call: get_resource_from_view"); }
  resource_view_desc get_resource_view_desc(resource_view view) const override { throw std::runtime_error("unexpected device call: get_resource_view_desc"); }
  bool map_buffer_region(resource resource, uint64_t offset, uint64_t size, map_access access, void** out_data) override { throw std::runtime_error("unexpected device call: map_buffer_region"); }
  void unmap_buffer_region(resource resource) override { throw std::runtime_error("unexpected device call: unmap_buffer_region"); }
  bool map_texture_region(resource resource, uint32_t subresource, const subresource_box* box, map_access access, subresource_data* out_data) override { throw std::runtime_error("unexpected device call: map_texture_region"); }
  void unmap_texture_region(resource resource, uint32_t subresource) override { throw std::runtime_error("unexpected device call: unmap_texture_region"); }
  void update_buffer_region(const void* data, resource dest, uint64_t dest_offset, uint64_t size) override { throw std::runtime_error("unexpected device call: update_buffer_region"); }
  void update_texture_region(const subresource_data& data, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override { throw std::runtime_error("unexpected device call: update_texture_region"); }
  bool create_pipeline(pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject* subobjects, pipeline* out_pipeline) override { throw std::runtime_error("unexpected device call: create_pipeline"); }
  void destroy_pipeline(pipeline pipeline) override { throw std::runtime_error("unexpected device call: destroy_pipeline"); }
  bool create_pipeline_layout(uint32_t param_count, const pipeline_layout_param* params, pipeline_layout* out_layout) override { throw std::runtime_error("unexpected device call: create_pipeline_layout"); }
  void destroy_pipeline_layout(pipeline_layout layout) override { throw std::runtime_error("unexpected device call: destroy_pipeline_layout"); }
  bool allocate_descriptor_tables(uint32_t count, pipeline_layout layout, uint32_t param, descriptor_table* out_tables) override { throw std::runtime_error("unexpected device call: allocate_descriptor_tables"); }
  void free_descriptor_tables(uint32_t count, const descriptor_table* tables) override { throw std::runtime_error("unexpected device call: free_descriptor_tables"); }
  void get_descriptor_heap_offset(descriptor_table table, uint32_t binding, uint32_t array_offset, descriptor_heap* out_heap, uint32_t* out_offset) const override { throw std::runtime_error("unexpected device call: get_descriptor_heap_offset"); }
  void copy_descriptor_tables(uint32_t count, const descriptor_table_copy* copies) override { throw std::runtime_error("unexpected device call: copy_descriptor_tables"); }
  void update_descriptor_tables(uint32_t count, const descriptor_table_update* updates) override { throw std::runtime_error("unexpected device call: update_descriptor_tables"); }
  bool create_query_heap(query_type type, uint32_t count, query_heap* out_heap) override { throw std::runtime_error("unexpected device call: create_query_heap"); }
  void destroy_query_heap(query_heap heap) override { throw std::runtime_error("unexpected device call: destroy_query_heap"); }
  bool get_query_heap_results(query_heap heap, uint32_t first, uint32_t count, void* results, uint32_t stride) override { throw std::runtime_error("unexpected device call: get_query_heap_results"); }
  void set_resource_name(resource resource, const char* name) override { throw std::runtime_error("unexpected device call: set_resource_name"); }
  void set_resource_view_name(resource_view view, const char* name) override { throw std::runtime_error("unexpected device call: set_resource_view_name"); }
  bool create_fence(uint64_t initial_value, fence_flags flags, fence* out_fence, void** shared_handle = nullptr) override { throw std::runtime_error("unexpected device call: create_fence"); }
  void destroy_fence(fence fence) override { throw std::runtime_error("unexpected device call: destroy_fence"); }
  uint64_t get_completed_fence_value(fence fence) const override { throw std::runtime_error("unexpected device call: get_completed_fence_value"); }
  bool wait(fence fence, uint64_t value, uint64_t timeout = UINT64_MAX) override { throw std::runtime_error("unexpected device call: wait"); }
  bool signal(fence fence, uint64_t value) override { throw std::runtime_error("unexpected device call: signal"); }
  bool get_property(device_properties property, void* data) const override { throw std::runtime_error("unexpected device call: get_property"); }
  uint64_t get_resource_view_gpu_address(resource_view view) const override { throw std::runtime_error("unexpected device call: get_resource_view_gpu_address"); }
  void get_acceleration_structure_size(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input* inputs, uint64_t* out_size, uint64_t* out_build_scratch_size, uint64_t* out_update_scratch_size) const override { throw std::runtime_error("unexpected device call: get_acceleration_structure_size"); }
  bool get_pipeline_shader_group_handles(pipeline pipeline, uint32_t first, uint32_t count, void* out_handles) override { throw std::runtime_error("unexpected device call: get_pipeline_shader_group_handles"); }
};

struct CommandList : command_list {
  Device* owner = nullptr;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> native;
  uint64_t get_native() const override { return reinterpret_cast<uint64_t>(native.Get()); }
  device* get_device() override { return owner; }
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
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace {
std::atomic_uint64_t registration_calls = 0;
}

extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) {
  ++registration_calls;
  return false;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*) {
  ++registration_calls;
}

int main() {
  try {
    const auto check = [](HRESULT result) {
      if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
    };
    const auto expect = [](bool condition, const std::string& label) {
      if (!condition) throw std::runtime_error(label);
    };
    mock::Device device;
    mock::CommandList command_list;
    command_list.owner = &device;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                            device.native.GetAddressOf(), nullptr, command_list.native.GetAddressOf()));
    ComPtr<ID3D11DeviceContext1> context;
    check(command_list.native.As(&context));
    uint32_t checked = 0;

    enum class Fault { NONE,
                       MISSING_INPUT,
                       MISMATCHED_INPUT,
                       EXCESSIVE_SOURCE_PADDING,
                       BAD_TARGET,
                       BAD_CONSTANTS,
                       BAD_OUTPUT_RECT,
                       BAD_JITTER,
                       BAD_DISPATCH };
    struct Case {
      const char* label;
      uint32_t hash;
      uint32_t percentage;
      AAMode mode = AAMode::DLSS_CUSTOM;
      Fault fault = Fault::NONE;
      bool enters = true;
      bool validated = true;
    };
    constexpr std::array CASES = {
        Case{.label = "Ultra native hash Custom 71", .hash = 0x29C140B2u, .percentage = 71},
        Case{.label = "Ultra native hash Custom 85 capture", .hash = 0x29C140B2u, .percentage = 85},
        Case{.label = "Ultra native hash Custom 99", .hash = 0x29C140B2u, .percentage = 99},
        Case{.label = "Ultra native hash Custom 100", .hash = 0x29C140B2u, .percentage = 100},
        Case{.label = "Fast native hash Custom 71", .hash = 0x201C3D3Du, .percentage = 71},
        Case{.label = "Fast native hash Custom 85 capture", .hash = 0x201C3D3Du, .percentage = 85},
        Case{.label = "Fast native hash Custom 99", .hash = 0x201C3D3Du, .percentage = 99},
        Case{.label = "Fast native hash Custom 100", .hash = 0x201C3D3Du, .percentage = 100},
        Case{.label = "Ultra DLAA 100", .hash = 0x29C140B2u, .percentage = 100, .mode = AAMode::DLAA},
        Case{.label = "Fast DLAA 100", .hash = 0x201C3D3Du, .percentage = 100, .mode = AAMode::DLAA},
        Case{.label = "Ultra DLAA rejects 85", .hash = 0x29C140B2u, .percentage = 85, .mode = AAMode::DLAA, .validated = false},
        Case{.label = "Fast DLAA rejects 85", .hash = 0x201C3D3Du, .percentage = 85, .mode = AAMode::DLAA, .validated = false},
        Case{.label = "Ultra native hash Quality 67", .hash = 0x29C140B2u, .percentage = 67, .mode = AAMode::DLSS_QUALITY},
        Case{.label = "Fast native hash Quality 67", .hash = 0x201C3D3Du, .percentage = 67, .mode = AAMode::DLSS_QUALITY},
        Case{.label = "Quality rejects 100", .hash = 0x29C140B2u, .percentage = 100, .mode = AAMode::DLSS_QUALITY, .validated = false},
        Case{.label = "Fast captured Custom 33", .hash = 0x4B43383Du, .percentage = 33},
        Case{.label = "Ultra Custom 33", .hash = 0xCCB4A156u, .percentage = 33},
        Case{.label = "Fast Custom 40", .hash = 0x4B43383Du, .percentage = 40},
        Case{.label = "Ultra Custom 40", .hash = 0xCCB4A156u, .percentage = 40},
        Case{.label = "Fast Custom 49", .hash = 0x4B43383Du, .percentage = 49},
        Case{.label = "Ultra Custom 49", .hash = 0xCCB4A156u, .percentage = 49},
        Case{.label = "Custom rejects below33", .hash = 0x4B43383Du, .percentage = 32, .validated = false},
        Case{.label = "Fast captured Ultra Performance 33", .hash = 0x4B43383Du, .percentage = 33, .mode = AAMode::DLSS_ULTRA_PERFORMANCE},
        Case{.label = "Ultra Performance 33 Ultra contract", .hash = 0xCCB4A156u, .percentage = 33, .mode = AAMode::DLSS_ULTRA_PERFORMANCE},
        Case{.label = "Fast Ultra Performance 34", .hash = 0x4B43383Du, .percentage = 34, .mode = AAMode::DLSS_ULTRA_PERFORMANCE},
        Case{.label = "Ultra Performance 34 Ultra contract", .hash = 0xCCB4A156u, .percentage = 34, .mode = AAMode::DLSS_ULTRA_PERFORMANCE},
        Case{.label = "Ultra Performance rejects below band", .hash = 0x4B43383Du, .percentage = 31, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .validated = false},
        Case{.label = "Ultra Performance rejects above band", .hash = 0x4B43383Du, .percentage = 36, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .validated = false},
        Case{.label = "Ultra Performance rejects50", .hash = 0x4B43383Du, .percentage = 50, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .validated = false},
        Case{.label = "Ultra Performance rejects100", .hash = 0x201C3D3Du, .percentage = 100, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .validated = false},
        Case{.label = "Ultra Performance33 mismatched source input", .hash = 0x4B43383Du, .percentage = 33, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .fault = Fault::MISMATCHED_INPUT, .enters = false, .validated = false},
        Case{.label = "Ultra Performance33 excessive source padding", .hash = 0x4B43383Du, .percentage = 33, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .fault = Fault::EXCESSIVE_SOURCE_PADDING, .validated = false},
        Case{.label = "Ultra Performance33 wrong output extent", .hash = 0x4B43383Du, .percentage = 33, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .fault = Fault::BAD_TARGET, .enters = false, .validated = false},
        Case{.label = "Ultra Performance33 nonzero temporal origin", .hash = 0x4B43383Du, .percentage = 33, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .fault = Fault::BAD_CONSTANTS, .validated = false},
        Case{.label = "Ultra Performance33 wrong output rectangle", .hash = 0x4B43383Du, .percentage = 33, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .fault = Fault::BAD_OUTPUT_RECT, .validated = false},
        Case{.label = "Ultra Performance33 inconsistent projection jitter", .hash = 0x4B43383Du, .percentage = 33, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .fault = Fault::BAD_JITTER, .validated = false},
        Case{.label = "Unknown shader", .hash = 0xC0DEC0DEu, .percentage = 85, .enters = false, .validated = false},
        Case{.label = "Missing depth input", .hash = 0x29C140B2u, .percentage = 85, .fault = Fault::MISSING_INPUT, .enters = false, .validated = false},
        Case{.label = "Wrong output extent", .hash = 0x201C3D3Du, .percentage = 85, .fault = Fault::BAD_TARGET, .enters = false, .validated = false},
        Case{.label = "Unsupported temporal CB origin", .hash = 0x29C140B2u, .percentage = 85, .fault = Fault::BAD_CONSTANTS, .validated = false},
        Case{.label = "Wrong dispatch extent", .hash = 0x201C3D3Du, .percentage = 85, .fault = Fault::BAD_DISPATCH, .enters = false, .validated = false},
        Case{.label = "Off ignores candidate", .hash = 0x29C140B2u, .percentage = 85, .mode = AAMode::OFF, .enters = false, .validated = false},
    };
    constexpr std::array<uint32_t, 2> OUTPUT = {3840, 2160};
    for (const auto& test : CASES) {
      context->ClearState();
      Reset();
      requested_mode.store(test.mode);
      state.device = &device;
      state.width = OUTPUT[0];
      state.height = OUTPUT[1];
      state.last_mode = test.mode;
      ++state.frame;
      const std::array<uint32_t, 2> active = {
          (OUTPUT[0] * test.percentage + 99) / 100,
          (OUTPUT[1] * test.percentage + 99) / 100,
      };
      const std::array<uint32_t, 2> allocated = {
          (active[0] + 3) / 4 * 4 + (test.fault == Fault::EXCESSIVE_SOURCE_PADDING ? 12u : 0u),
          (active[1] + 3) / 4 * 4,
      };
      if (test.percentage == 33 && test.fault == Fault::NONE) {
        expect(active == std::array<uint32_t, 2>{1268, 713}
                   && allocated == std::array<uint32_t, 2>{1268, 716},
               std::string(test.label) + ": fixture matches captured active input and padded source");
      }
      std::array<std::array<float, 4>, 27> taa = {};
      std::array<std::array<float, 4>, 130> view = {};
      taa[19] = {static_cast<float>(allocated[0]), static_cast<float>(allocated[1]), 0, 0};
      taa[20] = {static_cast<float>(OUTPUT[0]), static_cast<float>(OUTPUT[1]), 0, 0};
      taa[22] = taa[20];
      taa[23] = {0, 0, static_cast<float>(OUTPUT[0]), static_cast<float>(OUTPUT[1])};
      if (test.fault == Fault::BAD_OUTPUT_RECT) {
        taa[23][2] -= 4.f;
      }
      taa[24][0] = (test.fault == Fault::BAD_CONSTANTS ? 1.f : 0.f);
      taa[25] = {static_cast<float>(active[0]), static_cast<float>(active[1]), 0, 0};
      taa[26] = {.125f, .2777778f, static_cast<float>(active[0]) / OUTPUT[0], static_cast<float>(OUTPUT[0]) / active[0]};
      view[122] = {2 * taa[26][0] / active[0], -2 * taa[26][1] / active[1], 0, 0};
      if (test.fault == Fault::BAD_JITTER) {
        view[122][0] += .01f;
      }
      view[129] = {0, 1, 1, 0};
      for (uint32_t row = 0; row < 4; ++row) view[118 + row][row] = 1;
      const std::array<std::span<const std::byte>, 2> contents = {
          std::as_bytes(std::span(taa)),
          std::as_bytes(std::span(view)),
      };
      std::array<ComPtr<ID3D11Buffer>, 2> buffers;
      for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
        const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<uint32_t>(contents[slot].size()),
                                        .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
        const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = contents[slot].data()};
        check(device.native->CreateBuffer(&desc, &initial, buffers[slot].GetAddressOf()));
        ID3D11Buffer* buffer = buffers[slot].Get();
        context->CSSetConstantBuffers(slot, 1, &buffer);
      }
      std::array<ComPtr<ID3D11Texture2D>, 3> sources;
      std::array<ComPtr<ID3D11ShaderResourceView>, 3> inputs;
      constexpr std::array RESOURCE_FORMATS = {
          DXGI_FORMAT_R16G16B16A16_FLOAT,
          DXGI_FORMAT_R32G8X24_TYPELESS,
          DXGI_FORMAT_R16G16_UNORM,
      };
      constexpr std::array VIEW_FORMATS = {
          DXGI_FORMAT_R16G16B16A16_FLOAT,
          DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
          DXGI_FORMAT_R16G16_UNORM,
      };
      for (uint32_t slot = 0; slot < sources.size(); ++slot) {
        const D3D11_TEXTURE2D_DESC desc = {
            .Width = allocated[0] + (slot == 2 && test.fault == Fault::MISMATCHED_INPUT ? 4u : 0u),
            .Height = allocated[1],
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = RESOURCE_FORMATS[slot],
            .SampleDesc = {.Count = 1},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };
        check(device.native->CreateTexture2D(&desc, nullptr, sources[slot].GetAddressOf()));
        const D3D11_SHADER_RESOURCE_VIEW_DESC srv = {
            .Format = VIEW_FORMATS[slot],
            .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
            .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
        };
        check(device.native->CreateShaderResourceView(sources[slot].Get(), &srv, inputs[slot].GetAddressOf()));
        ID3D11ShaderResourceView* input = (slot == 1 && test.fault == Fault::MISSING_INPUT ? nullptr : inputs[slot].Get());
        context->CSSetShaderResources(slot + 1, 1, &input);
      }
      ComPtr<ID3D11Texture2D> target;
      ComPtr<ID3D11UnorderedAccessView> output;
      const D3D11_TEXTURE2D_DESC target_desc = {
          .Width = OUTPUT[0] - (test.fault == Fault::BAD_TARGET ? 4u : 0u),
          .Height = OUTPUT[1],
          .MipLevels = 1,
          .ArraySize = 1,
          .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
          .SampleDesc = {.Count = 1},
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
      };
      check(device.native->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf()));
      check(device.native->CreateUnorderedAccessView(target.Get(), nullptr, output.GetAddressOf()));
      ID3D11UnorderedAccessView* target_uav = output.Get();
      context->CSSetUnorderedAccessViews(0, 1, &target_uav, nullptr);

      Context command = {
          .cmd_list = &command_list,
          .arguments = {.group_count_x = (OUTPUT[0] + 7) / 8 - (test.fault == Fault::BAD_DISPATCH ? 1u : 0u),
                        .group_count_y = (OUTPUT[1] + 7) / 8,
                        .group_count_z = 1},
          .matched_shader_hash = test.hash,
      };
      auto* original_runtime = state.runtime.get();
      auto result = OnCommand(command);
      expect(!result.bypass, std::string(test.label) + ": original stock dispatch remains enabled");
      expect((result.post_callback != nullptr) == test.enters
                 && (result.finalize_callback != nullptr) == test.enters,
             std::string(test.label) + ": actual pre-callback gate outcome");
      if (test.enters) {
        auto* frame = const_cast<Frame*>(static_cast<const Frame*>(result.post_data));
        expect(frame != nullptr && frame->runtime.get() == original_runtime
                   && state.runtime == nullptr && state.claimed_frame == state.frame
                   && frame->runtime->constant_readback.pending_context,
               std::string(test.label) + ": actual callback claims and queues current GPU constants");
        expect(frame->temporal_shader_hash == test.hash
                   && frame->source_width == allocated[0] && frame->source_height == allocated[1],
               std::string(test.label) + ": actual resource capture keeps hash and allocated extent");
        // Do not initialize a private pipeline or NGX in this adapter test.
        // Prefill stable runtime identity only after proving the actual pre-gate
        // accepted and queued the candidate (its fatal latch is clear on entry).
        auto& runtime = *frame->runtime;
        runtime.device = device.native;
        runtime.width = OUTPUT[0];
        runtime.height = OUTPUT[1];
        runtime.render_width = active[0];
        runtime.render_height = active[1];
        runtime.mode = test.mode;
        runtime.temporal_shader_hash = test.hash;
        runtime.temporal_upscaling = active != OUTPUT;
        runtime.backend_failed = true;
        result.post_callback(command, result.post_data);
        expect(frame->values_validated == test.validated && !frame->committed,
               std::string(test.label) + ": actual post-callback validates dimensions without SDK or commit");
        if (test.validated) {
          expect(frame->render_width == active[0] && frame->render_height == active[1]
                     && frame->temporal_upscaling == (active != OUTPUT),
                 std::string(test.label) + ": preparation convention follows active size, never shader identity");
        }
        expect(!runtime.private_state && !runtime.prepare_shader && !runtime.output.resource,
               std::string(test.label) + ": private pipeline and NVIDIA remain uninitialized");
        result.finalize_callback(command, result.post_data);
        expect(state.runtime.get() == original_runtime
                   && state.claimed_frame == (test.validated ? state.frame : UINT64_MAX),
               std::string(test.label) + ": finalizer restores exact runtime and correct claim ownership");
      } else {
        expect(state.runtime.get() == original_runtime && state.claimed_frame == UINT64_MAX,
               std::string(test.label) + ": rejected pre-gate preserves runtime without claim");
      }
      expect(state.completed == 0 && registration_calls == 0,
             std::string(test.label) + ": no SDK commit or addon registration");
      ++checked;
    }
    context->ClearState();
    Reset();
    state.device = nullptr;
    requested_mode.store(AAMode::OFF);
    std::cout << "PASS: " << checked
              << " actual OnCommand WARP adapter cases; no game interception or NVIDIA evaluation\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
