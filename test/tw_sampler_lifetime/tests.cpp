#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <stdexcept>
#include <cstdio>
#include "src/games/twattila/texture_lod_bias.hpp"
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
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
  bool create_sampler(const sampler_desc& desc, sampler* out_sampler) override { D3D11_SAMPLER_DESC native_desc = {};
    native_desc.Filter = D3D11_FILTER_ANISOTROPIC;
    native_desc.AddressU = native_desc.AddressV = native_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    native_desc.MipLODBias = desc.mip_lod_bias; native_desc.MaxAnisotropy = 16;
    native_desc.ComparisonFunc = D3D11_COMPARISON_NEVER; native_desc.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* value = nullptr;
    if (FAILED(native->CreateSamplerState(&native_desc, &value))) return false;
    out_sampler->handle = reinterpret_cast<uint64_t>(value);
    twattila::texture_lod_bias::OnInitSampler(this, desc, *out_sampler);
    return true; }
  void destroy_sampler(sampler sampler) override { if (reinterpret_cast<ID3D11SamplerState*>(sampler.handle)->Release() == 0) {
      twattila::texture_lod_bias::OnDestroySampler(this, sampler);
    } }
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

struct Queue : command_queue {
 CommandList* commands = nullptr;
 uint64_t get_native() const override { return commands->get_native(); }
 device* get_device() override { return commands->owner; }
 void get_private_data(const uint8_t[16], uint64_t*) const override {}
 void set_private_data(const uint8_t[16], uint64_t) override {}
command_queue_type get_type() const override { throw std::runtime_error("unused queue method"); }
void wait_idle() const override { throw std::runtime_error("unused queue method"); }
void flush_immediate_command_list() const override { throw std::runtime_error("unused queue method"); }
command_list *get_immediate_command_list() override { return commands; }
void begin_debug_event(const char *label, const float color[4] = nullptr) override { throw std::runtime_error("unused queue method"); }
void end_debug_event() override { throw std::runtime_error("unused queue method"); }
void insert_debug_marker(const char *label, const float color[4] = nullptr) override { throw std::runtime_error("unused queue method"); }
bool wait(fence fence, uint64_t value) override { throw std::runtime_error("unused queue method"); }
bool signal(fence fence, uint64_t value) override { throw std::runtime_error("unused queue method"); }
uint64_t get_timestamp_frequency() const override { throw std::runtime_error("unused queue method"); }
};
}

void Check(bool good, const char* what) { if (!good) { printf("FAIL: %s\n",what); ExitProcess(1); } }
int main() {
  namespace lod=twattila::texture_lod_bias;
  mock::Device first,second;
  mock::CommandList commands,other;
  commands.owner=&first; other.owner=&second;
  Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&first.native,nullptr,&commands.native)),"first device");
  Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&second.native,nullptr,&other.native)),"second device");
  mock::Queue queue,other_queue;queue.commands=&commands;other_queue.commands=&other;
  reshade::api::sampler_desc desc={};desc.filter=reshade::api::filter_mode::anisotropic;
  reshade::api::sampler original={},other_original={};
  Check(first.create_sampler(desc,&original),"original");Check(second.create_sampler(desc,&other_original),"other original");
  auto* native_original=reinterpret_cast<ID3D11SamplerState*>(original.handle);
  auto* native_other=reinterpret_cast<ID3D11SamplerState*>(other_original.handle);
  commands.native->PSSetSamplers(0,1,&native_original);other.native->PSSetSamplers(0,1,&native_other);
  lod::SetMainView([] {return true;});lod::SetBias(-1.f);
  lod::SetTracking(&queue,false);
  Check(lod::internal::device_slots.size()==0 && lod::internal::tracking_devices.load()==0,"an untracked present makes no slots");
  lod::SetTracking(&queue,true);lod::SetTracking(&other_queue,true);
  Check(lod::internal::tracking_devices.load()==2,"two tracking devices counted");
  lod::OnBindViewports(&commands,0,0,nullptr);lod::OnBindViewports(&other,0,0,nullptr);
  uint64_t clone=0;
  lod::internal::samplers.if_contains(original.handle,[&](const auto& entry){clone=entry.second.clone.handle;});
  Check(clone!=0 && clone!=original.handle,"biased clone created");
  lod::OnInitSampler(&first,desc,original);
  lod::internal::samplers.if_contains(original.handle,[&](const auto& entry){Check(entry.second.clone.handle==clone,"duplicate init preserves clone");});
  first.destroy_sampler(original);second.destroy_sampler(other_original);
  Check(lod::internal::SlotsFor(&first)->game[0].Get()==native_original,"first slot owns original after game release");
  Check(lod::internal::SlotsFor(&second)->game[0].Get()==native_other,"second device has independent slots");
  D3D11_SAMPLER_DESC restored={};native_original->GetDesc(&restored);Check(restored.MipLODBias==0,"original remains alive while clone bound");
  lod::SetTracking(&queue,false);
  Microsoft::WRL::ComPtr<ID3D11SamplerState> bound;commands.native->PSGetSamplers(0,1,&bound);
  Check(bound.Get()==native_original,"tracking off restores original");bound.Reset();
  Check(lod::internal::tracking_devices.load()==1,"tracking off leaves the other device counted");
  commands.native->ClearState();
  lod::OnDestroyDevice(&first);
  Check(lod::SamplerCount()==1 && lod::internal::device_slots.size()==1,"device cleanup leaves other device intact");
  lod::Shutdown();
  Check(lod::SamplerCount()==0 && lod::internal::device_slots.size()==0,"shutdown clears all device caches");
  Check(lod::internal::tracking_devices.load()==0,"shutdown leaves no tracking device: events return on the count");
  other.native->PSGetSamplers(0,1,&bound);Check(bound.Get()==native_other,"shutdown restores other device original");bound.Reset();
  commands.native->ClearState();other.native->ClearState();
  puts("PASS: original ownership, duplicate identity, two-device isolation, tracking restore and shutdown cleanup");
}

