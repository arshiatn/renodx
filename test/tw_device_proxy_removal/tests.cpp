// utils/device_proxy.hpp's removed-device path on WARP, attached as a game attaches it (mods::swapchain v2's Use). The
// D3D11 proxy device is D3D11On12 over a WARP D3D12 device, so ID3D12Device5::RemoveDevice removes it for real
// (GetDeviceRemovedReason, the swap chain's calls). The game's device, swap chain and queue and the proxy's ReShade device
// are mocks that record what the proxy copies and destroys; the proxy's swap chain is the real thing on the same window.
// One mode a run (argv[1]), each the site where the removal is first seen:
//   resize  - ResizeBuffers (a resize pending). Control: a live device whose ResizeBuffers fails (a back buffer held)
//             keeps the old answer (a warning, the resize again next present).
//   test    - Present TEST (pending after a reset, flags ALLOW_TEARING). Control: a live TEST and tearing present pass.
//   present - Present (flags 0: no TEST). Control: a live present passes.
// Then in every mode:
// 1. The removal: one error line naming the site, the proxy stops (off, no retries, teardown pending).
// 2. The next present tears it down: the game's back buffer's clone off and destroyed, the shared pair destroyed on both
//    devices, the proxy's own resource destroyed, its swap chain released; UseProxyRequested false and
//    device_proxy_creation_failed true (R.U.S.E.'s native-present condition).
// 3. The presents after it do nothing (the mocks throw on any call but get_device): one "removed" line in all.
// 4. The swap chain mod's DETACH detaches device_proxy although the stop turned the proxy off: its module record goes
//    (not the event handler any more) and its destroy-info callback leaves utils::resource's list.
#include <windows.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "src/mods/swapchain_v2.hpp"

using Microsoft::WRL::ComPtr;
namespace proxy = renodx::utils::device_proxy;
namespace swapchain_mod = renodx::mods::swapchain::v2;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)
#define UNEXPECTED(what) { throw std::runtime_error("unexpected mock call: " what); }

extern "C" __declspec(dllexport) const char* const NAME = "tw_device_proxy_removal";

static std::vector<std::string> logged;
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  logged.emplace_back(message);
  std::printf("  log: %s\n", message);
}
__declspec(dllexport) void ReShadeRegisterEvent(reshade::addon_event, void*) {}
__declspec(dllexport) void ReShadeUnregisterEvent(reshade::addon_event, void*) {}
__declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char*, const char*, char*, size_t*) { return false; }
__declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char*, const char*, const char*) {}
}

static size_t Logged(const char* part) {
  return static_cast<size_t>(std::ranges::count_if(logged, [part](const std::string& line) { return line.find(part) != std::string::npos; }));
}

namespace mock {
using namespace reshade::api;

constexpr uint32_t SIZE = 64u;
constexpr uint64_t BACK_BUFFER = 0x100u;
constexpr uint64_t CLONE = 0x200u;
constexpr uint64_t HOST_SHARED = 0x300u;
constexpr uint64_t PROXY_SHARED = 0x400u;
constexpr uint64_t PROXY_RESOURCE = 0x500u;

// A device that answers its back buffer's description and records what it destroys: the game's (d3d11 api: its
// handoff copy is the command list's copy_resource) and the proxy's ReShade device.
struct Device : device {
  std::vector<uint64_t> destroyed;
  uint64_t get_native() const override { UNEXPECTED("get_native") }
  device_api get_api() const override { return device_api::d3d11; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { UNEXPECTED("get_private_data") }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { UNEXPECTED("set_private_data") }
  bool check_capability(device_caps capability) const override { UNEXPECTED("check_capability") }
  bool check_format_support(format format, resource_usage usage) const override { UNEXPECTED("check_format_support") }
  bool create_sampler(const sampler_desc& desc, sampler* out_sampler) override { UNEXPECTED("create_sampler") }
  void destroy_sampler(sampler sampler) override { UNEXPECTED("destroy_sampler") }
  bool create_resource(const resource_desc& desc, const subresource_data* initial_data, resource_usage initial_state, resource* out_resource, void** shared_handle = nullptr) override { UNEXPECTED("create_resource") }
  void destroy_resource(resource resource) override { destroyed.push_back(resource.handle); }
  resource_desc get_resource_desc(resource resource) const override {
    if (resource.handle != BACK_BUFFER) UNEXPECTED("get_resource_desc (not the back buffer)")
    return resource_desc(SIZE, SIZE, 1, 1, format::b8g8r8a8_unorm, 1, memory_heap::gpu_only, resource_usage::render_target);
  }
  bool create_resource_view(resource resource, resource_usage usage_type, const resource_view_desc& desc, resource_view* out_view) override { UNEXPECTED("create_resource_view") }
  void destroy_resource_view(resource_view view) override { UNEXPECTED("destroy_resource_view") }
  resource get_resource_from_view(resource_view view) const override { UNEXPECTED("get_resource_from_view") }
  resource_view_desc get_resource_view_desc(resource_view view) const override { UNEXPECTED("get_resource_view_desc") }
  bool map_buffer_region(resource resource, uint64_t offset, uint64_t size, map_access access, void** out_data) override { UNEXPECTED("map_buffer_region") }
  void unmap_buffer_region(resource resource) override { UNEXPECTED("unmap_buffer_region") }
  bool map_texture_region(resource resource, uint32_t subresource, const subresource_box* box, map_access access, subresource_data* out_data) override { UNEXPECTED("map_texture_region") }
  void unmap_texture_region(resource resource, uint32_t subresource) override { UNEXPECTED("unmap_texture_region") }
  void update_buffer_region(const void* data, resource dest, uint64_t dest_offset, uint64_t size) override { UNEXPECTED("update_buffer_region") }
  void update_texture_region(const subresource_data& data, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override { UNEXPECTED("update_texture_region") }
  bool create_pipeline(pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject* subobjects, pipeline* out_pipeline) override { UNEXPECTED("create_pipeline") }
  void destroy_pipeline(pipeline pipeline) override { UNEXPECTED("destroy_pipeline") }
  bool create_pipeline_layout(uint32_t param_count, const pipeline_layout_param* params, pipeline_layout* out_layout) override { UNEXPECTED("create_pipeline_layout") }
  void destroy_pipeline_layout(pipeline_layout layout) override { UNEXPECTED("destroy_pipeline_layout") }
  bool allocate_descriptor_tables(uint32_t count, pipeline_layout layout, uint32_t param, descriptor_table* out_tables) override { UNEXPECTED("allocate_descriptor_tables") }
  void free_descriptor_tables(uint32_t count, const descriptor_table* tables) override { UNEXPECTED("free_descriptor_tables") }
  void get_descriptor_heap_offset(descriptor_table table, uint32_t binding, uint32_t array_offset, descriptor_heap* out_heap, uint32_t* out_offset) const override { UNEXPECTED("get_descriptor_heap_offset") }
  void copy_descriptor_tables(uint32_t count, const descriptor_table_copy* copies) override { UNEXPECTED("copy_descriptor_tables") }
  void update_descriptor_tables(uint32_t count, const descriptor_table_update* updates) override { UNEXPECTED("update_descriptor_tables") }
  bool create_query_heap(query_type type, uint32_t count, query_heap* out_heap) override { UNEXPECTED("create_query_heap") }
  void destroy_query_heap(query_heap heap) override { UNEXPECTED("destroy_query_heap") }
  bool get_query_heap_results(query_heap heap, uint32_t first, uint32_t count, void* results, uint32_t stride) override { UNEXPECTED("get_query_heap_results") }
  void set_resource_name(resource resource, const char* name) override { UNEXPECTED("set_resource_name") }
  void set_resource_view_name(resource_view view, const char* name) override { UNEXPECTED("set_resource_view_name") }
  bool create_fence(uint64_t initial_value, fence_flags flags, fence* out_fence, void** shared_handle = nullptr) override { UNEXPECTED("create_fence") }
  void destroy_fence(fence fence) override { UNEXPECTED("destroy_fence") }
  uint64_t get_completed_fence_value(fence fence) const override { UNEXPECTED("get_completed_fence_value") }
  bool wait(fence fence, uint64_t value, uint64_t timeout = UINT64_MAX) override { UNEXPECTED("wait") }
  bool signal(fence fence, uint64_t value) override { UNEXPECTED("signal") }
  bool get_property(device_properties property, void* data) const override { UNEXPECTED("get_property") }
  uint64_t get_resource_view_gpu_address(resource_view view) const override { UNEXPECTED("get_resource_view_gpu_address") }
  void get_acceleration_structure_size(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input* inputs, uint64_t* out_size, uint64_t* out_build_scratch_size, uint64_t* out_update_scratch_size) const override { UNEXPECTED("get_acceleration_structure_size") }
  bool get_pipeline_shader_group_handles(pipeline pipeline, uint32_t first, uint32_t count, void* out_handles) override { UNEXPECTED("get_pipeline_shader_group_handles") }
};

// The game's immediate command list: records the handoff copy.
struct CommandList : command_list {
  Device* owner = nullptr;
  std::vector<std::pair<uint64_t, uint64_t>> copies;
  uint64_t get_native() const override { UNEXPECTED("command list get_native") }
  device* get_device() override { return owner; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { UNEXPECTED("command list get_private_data") }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { UNEXPECTED("command list set_private_data") }
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
  void update_buffer_region(const void* data, resource dest, uint64_t dest_offset, uint64_t size) override { UNEXPECTED("command list update_buffer_region") }
  void update_texture_region(const subresource_data& data, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override { UNEXPECTED("command list update_texture_region") }
};

struct Queue : command_queue {
  CommandList* commands = nullptr;
  uint64_t get_native() const override { UNEXPECTED("queue get_native") }
  device* get_device() override { return commands->owner; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { UNEXPECTED("queue get_private_data") }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { UNEXPECTED("queue set_private_data") }
  command_queue_type get_type() const override { UNEXPECTED("get_type") }
  void wait_idle() const override { UNEXPECTED("wait_idle") }
  void flush_immediate_command_list() const override {}
  command_list* get_immediate_command_list() override { return commands; }
  void begin_debug_event(const char* label, const float color[4] = nullptr) override { UNEXPECTED("queue begin_debug_event") }
  void end_debug_event() override { UNEXPECTED("queue end_debug_event") }
  void insert_debug_marker(const char* label, const float color[4] = nullptr) override { UNEXPECTED("queue insert_debug_marker") }
  bool wait(fence fence, uint64_t value) override { UNEXPECTED("queue wait") }
  bool signal(fence fence, uint64_t value) override { UNEXPECTED("queue signal") }
  uint64_t get_timestamp_frequency() const override { UNEXPECTED("get_timestamp_frequency") }
};

// The game's swap chain: one back buffer, whose clone the proxy copies.
struct Swapchain : swapchain {
  Device* owner = nullptr;
  HWND window = nullptr;
  bool host_calls = true;  // false: only get_device may be called
  uint64_t get_native() const override { UNEXPECTED("swapchain get_native") }
  device* get_device() override { return owner; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { UNEXPECTED("swapchain get_private_data") }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { UNEXPECTED("swapchain set_private_data") }
  void* get_hwnd() const override {
    if (!host_calls) UNEXPECTED("get_hwnd after the teardown")
    return window;
  }
  resource get_back_buffer(uint32_t index) override {
    if (!host_calls) UNEXPECTED("get_back_buffer after the teardown")
    return {BACK_BUFFER};
  }
  uint32_t get_back_buffer_count() const override {
    if (!host_calls) UNEXPECTED("get_back_buffer_count after the teardown")
    return 1u;
  }
  uint32_t get_current_back_buffer_index() const override {
    if (!host_calls) UNEXPECTED("get_current_back_buffer_index after the teardown")
    return 0u;
  }
  bool check_color_space_support(color_space color_space) const override { UNEXPECTED("check_color_space_support") }
  color_space get_color_space() const override { UNEXPECTED("get_color_space") }
};
}  // namespace mock

static int Run(const std::string& mode) {
  if (mode != "resize" && mode != "test" && mode != "present") {
    std::printf("usage: device_proxy_removal resize|test|present\n");
    return 2;
  }
  HWND window = CreateWindowExW(0, L"STATIC", L"tw_device_proxy_removal", WS_OVERLAPPEDWINDOW, 0, 0, mock::SIZE, mock::SIZE,
                                nullptr, nullptr, nullptr, nullptr);
  CHECK(window != nullptr);

  ComPtr<IDXGIFactory4> factory;
  CHECK(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
  ComPtr<IDXGIAdapter> warp;
  CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
  ComPtr<ID3D12Device5> device12;
  CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12))));
  ComPtr<ID3D12CommandQueue> queue12;
  const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(SUCCEEDED(device12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue12))));
  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11DeviceContext> context11;
  IUnknown* queues[] = {queue12.Get()};
  CHECK(SUCCEEDED(D3D11On12CreateDevice(device12.Get(), D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0, queues, 1, 0,
                                        &device11, &context11, nullptr)));

  // The proxy's swap chain as GetDeviceProxy makes it where tearing is supported (flip discard, the target format, the
  // tearing flag), on the D3D12 queue: D3D11On12 devices make no swap chains.
  ComPtr<IDXGIFactory5> factory5;
  BOOL tearing = FALSE;
  CHECK(SUCCEEDED(factory.As(&factory5)));
  CHECK(SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing != FALSE);
  const DXGI_SWAP_CHAIN_DESC1 chain_desc = {
      .Width = mock::SIZE,
      .Height = mock::SIZE,
      .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
      .SampleDesc = {.Count = 1},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .Scaling = DXGI_SCALING_STRETCH,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
      .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
  };
  ComPtr<IDXGISwapChain1> chain;
  CHECK(SUCCEEDED(factory->CreateSwapChainForHwnd(queue12.Get(), window, &chain_desc, nullptr, nullptr, &chain)));
  CHECK(SUCCEEDED(chain->Present(0, 0)));

  // Attached as a D3D9 game attaches it (R.U.S.E.: the swap chain mod with the proxy requested).
  swapchain_mod::use_device_proxy = true;
  swapchain_mod::Use(DLL_PROCESS_ATTACH);
  CHECK(proxy::shared.data != nullptr && proxy::shared.IsEventHandler());
  CHECK(proxy::shared.data->use_device_proxy);
  proxy::shared.data->settings_dirty = false;

  mock::Device host;
  mock::Device proxy_reshade;
  mock::CommandList host_commands;
  host_commands.owner = &host;
  mock::Queue host_queue;
  host_queue.commands = &host_commands;
  mock::Swapchain host_chain;  // virtual bases: no aggregate
  host_chain.owner = &host;
  host_chain.window = window;

  // The proxy as GetDeviceProxy and the first presents leave it: the game's back buffer cloned for the proxy, the clone's
  // shared pair, the proxy's own resource; the swap chain owned by device_proxy (released at its teardown).
  proxy::proxy_device = device11.Get();
  proxy::proxy_device_context = context11.Get();
  proxy::proxy_swap_chain = chain.Detach();
  proxy::proxy_device_reshade = &proxy_reshade;
  const auto make_proxy_resources = [] {
    proxy::proxy_device_resource = {mock::PROXY_RESOURCE};
    proxy::proxy_shared_resources_by_clone[mock::CLONE] = {
        .host_shared_resource = {mock::HOST_SHARED},
        .proxy_shared_resource = {mock::PROXY_SHARED},
    };
  };
  make_proxy_resources();
  CHECK(renodx::utils::resource::UpsertResourceInfo({mock::BACK_BUFFER}, [&](renodx::utils::resource::ResourceInfo* info, bool) {
    info->device = &host;
    info->desc = host.get_resource_desc({mock::BACK_BUFFER});
    info->clone = {mock::CLONE};
    info->clone_enabled = true;
    info->clone_can_deactivate = true;
    info->clone_target = &proxy::proxy_clone_target;
  }));
  const auto present = [&host_chain](mock::Queue* queue) {
    proxy::OnPresent(queue, &host_chain, nullptr, nullptr, 0, nullptr);
  };

  // Control on the live device.
  if (mode == "resize") {
    ComPtr<ID3D12Resource> held;
    CHECK(SUCCEEDED(proxy::proxy_swap_chain->GetBuffer(0, IID_PPV_ARGS(&held))));
    proxy::proxy_device_needs_resize = true;
    present(&host_queue);
    CHECK(Logged("ResizeBuffers failed") == 1u);
    CHECK(proxy::proxy_device_needs_resize.load());
    // The resize destroyed the pair and the proxy's resource before ResizeBuffers: made again as the next present would.
    CHECK(proxy::proxy_shared_resources_by_clone.empty() && proxy::proxy_device_resource.handle == 0u);
    make_proxy_resources();
  } else {
    proxy::proxy_present_flags = (mode == "test") ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    present(&host_queue);
    CHECK(host_commands.copies.size() == 1u);
    CHECK(host_commands.copies[0] == std::make_pair(mock::CLONE, mock::HOST_SHARED));
    CHECK(proxy::last_device_proxy_shared_resource.handle == mock::PROXY_SHARED);
    CHECK(Logged("Proxy Present") == 0u);
    CHECK(proxy::proxy_present_test_pending.load() == (mode == "present"));  // flags 0: no TEST ran
    proxy::proxy_present_test_pending = true;  // as after a reset: the next present TESTs first (flags 0: skipped)
  }
  CHECK(proxy::proxied_device_reshade == &host);
  CHECK(Logged("proxy device removed") == 0u);
  CHECK(proxy::UseProxyRequested() && !proxy::device_proxy_creation_failed);
  CHECK(!proxy::shared.data->remove_device_proxy);
  CHECK(proxy::proxy_swap_chain != nullptr);
  std::printf("control (%s): a live device's answer as before, the proxy stays on\n", mode.c_str());

  // 1. Removed.
  device12->RemoveDevice();
  const HRESULT reason = device11->GetDeviceRemovedReason();
  std::printf("   D3D11On12 device removed reason 0x%08lX\n", static_cast<unsigned long>(reason));
  CHECK(reason != S_OK);
  host.destroyed.clear();
  proxy_reshade.destroyed.clear();
  present(&host_queue);
  const char* site = (mode == "resize" ? "proxy device removed at ResizeBuffers:"
                                       : (mode == "test" ? "proxy device removed at Present TEST:" : "proxy device removed at Present:"));
  CHECK(Logged(site) == 1u);
  CHECK(Logged("proxy device removed") == 1u);
  CHECK(proxy::device_proxy_creation_failed);
  CHECK(!proxy::shared.data->use_device_proxy && !proxy::use_device_proxy);
  CHECK(proxy::shared.data->remove_device_proxy);
  // Only the resize path destroys anything before the stop (the pair and the proxy's resource, before ResizeBuffers).
  const std::vector<uint64_t> host_before_teardown = (mode == "resize") ? std::vector<uint64_t>{mock::HOST_SHARED} : std::vector<uint64_t>{};
  const std::vector<uint64_t> proxy_before_teardown =
      (mode == "resize") ? std::vector<uint64_t>{mock::PROXY_SHARED, mock::PROXY_RESOURCE} : std::vector<uint64_t>{};
  CHECK(host.destroyed == host_before_teardown);
  CHECK(proxy_reshade.destroyed == proxy_before_teardown);
  std::printf("1. removed: one error line (%s), the proxy off for good, teardown pending\n", site);

  // 2. Teardown (a present without a queue: no RTV restore to check here). By its end each resource destroyed once.
  present(nullptr);
  CHECK(proxy::proxy_swap_chain == nullptr);
  CHECK(!proxy::shared.data->remove_device_proxy);
  CHECK(!proxy::UseProxyRequested() && proxy::device_proxy_creation_failed);
  CHECK((host.destroyed == std::vector<uint64_t>{mock::HOST_SHARED, mock::CLONE}));
  CHECK((proxy_reshade.destroyed == std::vector<uint64_t>{mock::PROXY_SHARED, mock::PROXY_RESOURCE}));
  CHECK(proxy::proxy_shared_resources_by_clone.empty() && proxy::proxy_device_resource.handle == 0u);
  bool clone_left = true;
  CHECK(renodx::utils::resource::GetResourceInfo({mock::BACK_BUFFER}, [&](const renodx::utils::resource::ResourceInfo& info) {
    clone_left = info.clone_enabled || info.clone.handle != 0u;
  }));
  CHECK(!clone_left);
  std::printf("2. teardown: clone off and destroyed, shared pair and proxy resource destroyed, swap chain released\n");

  // 3. Quiet after.
  host_chain.host_calls = false;
  for (int frame = 0; frame < 4; ++frame) present(&host_queue);
  CHECK(Logged("proxy device removed") == 1u);
  CHECK(Logged("GetDeviceProxy returned null") == 0u);
  std::printf("3. quiet: 4 presents, no proxy work, one removed line in all\n");

  // 4. The swap chain mod's detach (an add-on unload) detaches device_proxy although UseProxyRequested is false now.
  const auto& destroy_callbacks = renodx::utils::resource::shared.data->on_destroy_resource_info_callbacks;
  CHECK(std::ranges::find(destroy_callbacks, &proxy::OnDestroyTrackedResourceInfo) != destroy_callbacks.end());
  swapchain_mod::Use(DLL_PROCESS_DETACH);
  CHECK(!proxy::shared.IsEventHandler());
  CHECK(std::ranges::find(destroy_callbacks, &proxy::OnDestroyTrackedResourceInfo) == destroy_callbacks.end());
  std::printf("4. detach: device_proxy's record and destroy-info callback gone\n");

  // Released as OnDestroyDevice would at the game device's end.
  proxy::proxy_device_context = nullptr;
  proxy::proxy_device = nullptr;
  proxy::proxy_device_reshade = nullptr;
  DestroyWindow(window);
  return 0;
}

int main(int argc, char** argv) {
  try {
    const int result = Run(argc > 1 ? argv[1] : "");
    std::printf(result == 0 ? "PASS\n" : "FAILED\n");
    return result;
  } catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
  }
}
