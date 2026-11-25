/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * A copy of the finished frame into an 8-bit texture (a capture or fade), on the CPU: the add-on's copy_resource
 * handler (addon.cpp OnCopyResource) and then utils' (resource_upgrade.hpp OnCopyResource), in ReShade's order, on
 * resource records as the swap chain module leaves them. The command list records the copy utils issues.
 *  scrgb    scRGB: buffer 0 is the real RGBA16F buffer, its clone (the swap chain pass's copy) off. The capture's clone
 *           turns on and buffer 0 is copied into it. CONTROL: before the fix the capture's clone stays off and utils
 *           drops the copy as mismatched (no copy: the capture stays stale).
 *  hdr10    HDR10: buffer 0's clone on; its clone is copied into the capture's clone (as before the fix).
 *  early    before the tone mapper: the capture's clone stays off.
 *  into_bb  a copy into buffer 0 in scRGB: its clone stays off (the pass would overwrite it: black).
 *  stock    an 8-bit texture without an active clone: nothing turns on, the plain copy.
 * Run by test/tw_tales_unit (after the talesofarise target); exit code 0 when every check holds.
 */

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

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
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// The ReShade exports the utilities call (no ReShade in this process).
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
__declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char*, const char*, char*, size_t*) { return false; }
__declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char*, const char*, const char*) {}
__declspec(dllexport) void ReShadeRegisterEvent(reshade::addon_event, void*) {}
__declspec(dllexport) void ReShadeRegisterEventForAddon(void*, reshade::addon_event, void*) {}
__declspec(dllexport) void ReShadeUnregisterEvent(reshade::addon_event, void*) {}
__declspec(dllexport) void ReShadeUnregisterEventForAddon(void*, reshade::addon_event, void*) {}
}

namespace copy_test {

using namespace reshade::api;  // NOLINT(google-build-using-namespace)
namespace res = renodx::utils::resource;

#define UNEXPECTED(what) { throw std::runtime_error("unexpected command list call: " what); }

// Records the copies utils issues; any other call is a test failure.
struct CommandList : command_list {
  std::vector<std::pair<uint64_t, uint64_t>> copies;
  uint64_t get_native() const override { UNEXPECTED("get_native") }
  device* get_device() override { UNEXPECTED("get_device") }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { UNEXPECTED("get_private_data") }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { UNEXPECTED("set_private_data") }
  void barrier(uint32_t count, const resource* resources, const resource_usage* old_states, const resource_usage* new_states) override { UNEXPECTED("barrier") }
  void begin_render_pass(uint32_t count, const render_pass_render_target_desc* rts, const render_pass_depth_stencil_desc* ds = nullptr) override { UNEXPECTED("begin_render_pass") }
  void end_render_pass() override { UNEXPECTED("end_render_pass") }
  void bind_render_targets_and_depth_stencil(uint32_t count, const resource_view* rtvs, resource_view dsv = {0}) override { UNEXPECTED("bind_render_targets_and_depth_stencil") }
  void bind_pipeline(pipeline_stage stages, pipeline pipeline) override { UNEXPECTED("bind_pipeline") }
  void bind_pipeline_states(uint32_t count, const dynamic_state* states, const uint32_t* values) override { UNEXPECTED("bind_pipeline_states") }
  void bind_viewports(uint32_t first, uint32_t count, const viewport* viewports) override { UNEXPECTED("bind_viewports") }
  void bind_scissor_rects(uint32_t first, uint32_t count, const rect* rects) override { UNEXPECTED("bind_scissor_rects") }
  void push_constants(shader_stage stages, pipeline_layout layout, uint32_t param, uint32_t first, uint32_t count, const void* values) override { UNEXPECTED("push_constants") }
  void push_descriptors(shader_stage stages, pipeline_layout layout, uint32_t param, const descriptor_table_update& update) override { UNEXPECTED("push_descriptors") }
  void bind_descriptor_tables(shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* tables) override { UNEXPECTED("bind_descriptor_tables") }
  void bind_index_buffer(resource buffer, uint64_t offset, uint32_t index_size) override { UNEXPECTED("bind_index_buffer") }
  void bind_vertex_buffers(uint32_t first, uint32_t count, const resource* buffers, const uint64_t* offsets, const uint32_t* strides) override { UNEXPECTED("bind_vertex_buffers") }
  void bind_stream_output_buffers(uint32_t first, uint32_t count, const resource* buffers, const uint64_t* offsets, const uint64_t* max_sizes, const resource* counter_buffers, const uint64_t* counter_offsets) override { UNEXPECTED("bind_stream_output_buffers") }
  void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) override { UNEXPECTED("draw") }
  void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) override { UNEXPECTED("draw_indexed") }
  void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override { UNEXPECTED("dispatch") }
  void draw_or_dispatch_indirect(indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride) override { UNEXPECTED("draw_or_dispatch_indirect") }
  void copy_resource(resource source, resource dest) override { copies.emplace_back(source.handle, dest.handle); }
  void copy_buffer_region(resource source, uint64_t source_offset, resource dest, uint64_t dest_offset, uint64_t size) override { UNEXPECTED("copy_buffer_region") }
  void copy_buffer_to_texture(resource source, uint64_t source_offset, uint32_t row_length, uint32_t slice_height, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override { UNEXPECTED("copy_buffer_to_texture") }
  void copy_texture_region(resource source, uint32_t source_subresource, const subresource_box* source_box, resource dest, uint32_t dest_subresource, const subresource_box* dest_box, filter_mode filter = filter_mode::min_mag_mip_point) override { UNEXPECTED("copy_texture_region") }
  void copy_texture_to_buffer(resource source, uint32_t source_subresource, const subresource_box* source_box, resource dest, uint64_t dest_offset, uint32_t row_length = 0, uint32_t slice_height = 0) override { UNEXPECTED("copy_texture_to_buffer") }
  void resolve_texture_region(resource source, uint32_t source_subresource, const subresource_box* source_box, resource dest, uint32_t dest_subresource, uint32_t dest_x, uint32_t dest_y, uint32_t dest_z, format format) override { UNEXPECTED("resolve_texture_region") }
  void clear_depth_stencil_view(resource_view dsv, const float* depth, const uint8_t* stencil, uint32_t rect_count = 0, const rect* rects = nullptr) override { UNEXPECTED("clear_depth_stencil_view") }
  void clear_render_target_view(resource_view rtv, const float color[4], uint32_t rect_count = 0, const rect* rects = nullptr) override { UNEXPECTED("clear_render_target_view") }
  void clear_unordered_access_view_uint(resource_view uav, const uint32_t values[4], uint32_t rect_count = 0, const rect* rects = nullptr) override { UNEXPECTED("clear_unordered_access_view_uint") }
  void clear_unordered_access_view_float(resource_view uav, const float values[4], uint32_t rect_count = 0, const rect* rects = nullptr) override { UNEXPECTED("clear_unordered_access_view_float") }
  void generate_mipmaps(resource_view srv) override { UNEXPECTED("generate_mipmaps") }
  void begin_query(query_heap heap, query_type type, uint32_t index) override { UNEXPECTED("begin_query") }
  void end_query(query_heap heap, query_type type, uint32_t index) override { UNEXPECTED("end_query") }
  void copy_query_heap_results(query_heap heap, query_type type, uint32_t first, uint32_t count, resource dest, uint64_t dest_offset, uint32_t stride) override { UNEXPECTED("copy_query_heap_results") }
  void begin_debug_event(const char* label, const float color[4] = nullptr) override { UNEXPECTED("begin_debug_event") }
  void end_debug_event() override { UNEXPECTED("end_debug_event") }
  void insert_debug_marker(const char* label, const float color[4] = nullptr) override { UNEXPECTED("insert_debug_marker") }
  void dispatch_mesh(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override { UNEXPECTED("dispatch_mesh") }
  void dispatch_rays(resource raygen, uint64_t raygen_offset, uint64_t raygen_size, resource miss, uint64_t miss_offset, uint64_t miss_size, uint64_t miss_stride, resource hit_group, uint64_t hit_group_offset, uint64_t hit_group_size, uint64_t hit_group_stride, resource callable, uint64_t callable_offset, uint64_t callable_size, uint64_t callable_stride, uint32_t width, uint32_t height, uint32_t depth) override { UNEXPECTED("dispatch_rays") }
  void copy_acceleration_structure(resource_view source, resource_view dest, acceleration_structure_copy_mode mode) override { UNEXPECTED("copy_acceleration_structure") }
  void build_acceleration_structure(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input* inputs, resource scratch, uint64_t scratch_offset, resource_view source, resource_view dest, acceleration_structure_build_mode mode) override { UNEXPECTED("build_acceleration_structure") }
  void query_acceleration_structures(uint32_t count, const resource_view* acceleration_structures, query_heap heap, query_type type, uint32_t first) override { UNEXPECTED("query_acceleration_structures") }
  void update_buffer_region(const void* data, resource dest, uint64_t dest_offset, uint64_t size) override { UNEXPECTED("update_buffer_region") }
  void update_texture_region(const subresource_data& data, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override { UNEXPECTED("update_texture_region") }
};

// A 1280x720 texture's record: its clone already made (no device call), enabled or not.
struct Texture {
  uint64_t handle = 0u;
  format format = format::unknown;
  bool swap_chain = false;
  res::ResourceUpgradeInfo* clone_target = nullptr;
  bool clone_enabled = false;
};

constexpr uint64_t CLONE = 0x1000u;  // a texture's clone handle: its handle + CLONE

void Record(const Texture& texture) {
  res::UpsertResourceInfo({texture.handle}, [&](res::ResourceInfo* info, bool) {
    *info = {
        .desc = resource_desc(1280u, 720u, 1u, 1u, texture.format, 1u, memory_heap::gpu_only,
                              resource_usage::render_target | resource_usage::shader_resource | resource_usage::copy_dest),
        .clone_desc = resource_desc(1280u, 720u, 1u, 1u, format::r16g16b16a16_float, 1u, memory_heap::gpu_only,
                                    resource_usage::render_target | resource_usage::shader_resource | resource_usage::copy_dest),
        .resource = {texture.handle},
        .clone = {texture.handle + CLONE},
        .clone_enabled = texture.clone_enabled,
        .is_swap_chain = texture.swap_chain,
        .clone_target = texture.clone_target,
    };
  });
}

bool CloneEnabled(uint64_t handle) {
  bool enabled = false;
  res::GetResourceInfo({handle}, [&enabled](const res::ResourceInfo& info) { enabled = info.clone_enabled; });
  return enabled;
}

struct Result {
  bool dest_clone_enabled = false;
  std::vector<std::pair<uint64_t, uint64_t>> copies;
};

// One game copy through both handlers, as ReShade calls them (the add-on registered before utils).
Result Copy(const Texture& source, const Texture& dest, bool tonemapped) {
  Record(source);
  Record(dest);
  frame_tonemapped.store(tonemapped);
  CommandList commands;
  OnCopyResource(&commands, {source.handle}, {dest.handle});
  renodx::utils::resource::upgrade::OnCopyResource(&commands, {source.handle}, {dest.handle});
  return {.dest_clone_enabled = CloneEnabled(dest.handle), .copies = commands.copies};
}

}  // namespace copy_test

int main() {
  using namespace copy_test;  // NOLINT(google-build-using-namespace)
  renodx::utils::resource::upgrade::Use(DLL_PROCESS_ATTACH);
  // The swap chain module's targets: a back buffer's clone, and the post-tone-mapper frame's (addon.cpp ConfigureAddon).
  res::ResourceUpgradeInfo swap_chain_clone = {.old_format = format::r10g10b10a2_unorm, .new_format = format::r16g16b16a16_float};
  res::ResourceUpgradeInfo frame_clone = {.old_format = format::r8g8b8a8_unorm, .new_format = format::r16g16b16a16_float,
                                          .use_resource_view_cloning = true, .use_resource_view_hot_swap = true};
  const Texture scrgb_buffer = {.handle = 0x10000u, .format = format::r16g16b16a16_float, .swap_chain = true,
                                .clone_target = &swap_chain_clone, .clone_enabled = false};
  const Texture hdr10_buffer = {.handle = 0x20000u, .format = format::r10g10b10a2_unorm, .swap_chain = true,
                                .clone_target = &swap_chain_clone, .clone_enabled = true};
  const Texture capture = {.handle = 0x30000u, .format = format::r8g8b8a8_unorm, .clone_target = &frame_clone};
  const Texture capture_on = {.handle = 0x30000u, .format = format::r8g8b8a8_unorm, .clone_target = &frame_clone, .clone_enabled = true};
  const Texture stock = {.handle = 0x40000u, .format = format::r8g8b8a8_unorm};

  int failures = 0;
  const auto check = [&failures](bool condition, const char* label) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
    failures += condition ? 0 : 1;
  };
  using Copies = std::vector<std::pair<uint64_t, uint64_t>>;

  const Result scrgb = Copy(scrgb_buffer, capture, true);
  check(scrgb.dest_clone_enabled, "scrgb: the capture's clone turns on");
  check(scrgb.copies == Copies{{scrgb_buffer.handle, capture.handle + CLONE}}, "scrgb: buffer 0 copied into the capture's clone");

  const Result hdr10 = Copy(hdr10_buffer, capture, true);
  check(hdr10.dest_clone_enabled, "hdr10: the capture's clone turns on");
  check(hdr10.copies == Copies{{hdr10_buffer.handle + CLONE, capture.handle + CLONE}}, "hdr10: buffer 0's clone copied into the capture's clone");

  const Result early = Copy(scrgb_buffer, capture, false);
  check(!early.dest_clone_enabled, "early: before the tone mapper the capture's clone stays off");

  const Result into_bb = Copy(capture_on, scrgb_buffer, true);
  check(!into_bb.dest_clone_enabled, "into_bb: buffer 0's clone stays off");

  const Result stock_copy = Copy(stock, capture, true);
  check(!stock_copy.dest_clone_enabled, "stock: an 8-bit source turns no clone on");
  check(stock_copy.copies == Copies{{stock.handle, capture.handle}}, "stock: the plain copy");

  std::printf("%s (%d failed)\n", failures == 0 ? "All passed" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
