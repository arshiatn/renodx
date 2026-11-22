// utils/device_proxy.hpp's removed-device path on WARP: the D3D11 proxy device is D3D11On12 over a WARP D3D12 device, so
// ID3D12Device5::RemoveDevice removes it for real (GetDeviceRemovedReason, the swap chain's calls). The host is a mock
// D3D9 device (OnPresent's host path); the proxy's swap chain is the real thing on the same window.
// 1. Control: the device alive, a resize whose ResizeBuffers fails (a back buffer still held: DXGI_ERROR_INVALID_CALL):
//    the old answer (a warning, the resize again next present), the proxy stays on, no "removed" line.
// 2. The device removed, the same resize: one error line naming the removal and its reason, the proxy stops (off, no
//    retries, teardown pending).
// 3. The next present tears the proxy down (its swap chain released, the remove flag cleared): the host presents
//    natively (UseProxyRequested false, device_proxy_creation_failed true: R.U.S.E.'s native-present condition).
// 4. The presents after it do nothing (the mocks throw on any call but get_device): one "removed" line in all.
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
#include <vector>

#include "src/utils/device_proxy.hpp"

using Microsoft::WRL::ComPtr;
namespace proxy = renodx::utils::device_proxy;

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
}

static size_t Logged(const char* part) {
  return static_cast<size_t>(std::ranges::count_if(logged, [part](const std::string& line) { return line.find(part) != std::string::npos; }));
}

namespace mock {
using namespace reshade::api;

constexpr uint32_t SIZE = 64u;

// The game's D3D9 device: OnPresent asks only its api and its back buffer's description.
struct Device : device {
  uint64_t get_native() const override { UNEXPECTED("get_native") }
  device_api get_api() const override { return device_api::d3d9; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { UNEXPECTED("get_private_data") }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { UNEXPECTED("set_private_data") }
  bool check_capability(device_caps capability) const override { UNEXPECTED("check_capability") }
  bool check_format_support(format format, resource_usage usage) const override { UNEXPECTED("check_format_support") }
  bool create_sampler(const sampler_desc& desc, sampler* out_sampler) override { UNEXPECTED("create_sampler") }
  void destroy_sampler(sampler sampler) override { UNEXPECTED("destroy_sampler") }
  bool create_resource(const resource_desc& desc, const subresource_data* initial_data, resource_usage initial_state, resource* out_resource, void** shared_handle = nullptr) override { UNEXPECTED("create_resource") }
  void destroy_resource(resource resource) override { UNEXPECTED("destroy_resource") }
  resource_desc get_resource_desc(resource resource) const override {
    if (resource.handle != 0x100u) UNEXPECTED("get_resource_desc (not the back buffer)")
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

// The game's swap chain: one back buffer (0x100), no clones to deactivate (no back buffers listed for the teardown).
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
    return {0x100u};
  }
  uint32_t get_back_buffer_count() const override {
    if (!host_calls) UNEXPECTED("get_back_buffer_count after the teardown")
    return 0u;
  }
  uint32_t get_current_back_buffer_index() const override {
    if (!host_calls) UNEXPECTED("get_current_back_buffer_index after the teardown")
    return 0u;
  }
  bool check_color_space_support(color_space color_space) const override { UNEXPECTED("check_color_space_support") }
  color_space get_color_space() const override { UNEXPECTED("get_color_space") }
};
}  // namespace mock

static int Run() {
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

  // The proxy's swap chain as GetDeviceProxy makes it (flip discard, the target format), on the D3D12 queue: D3D11On12
  // devices make no swap chains.
  const DXGI_SWAP_CHAIN_DESC1 chain_desc = {
      .Width = mock::SIZE,
      .Height = mock::SIZE,
      .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
      .SampleDesc = {.Count = 1},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .Scaling = DXGI_SCALING_STRETCH,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
  };
  ComPtr<IDXGISwapChain1> chain;
  CHECK(SUCCEEDED(factory->CreateSwapChainForHwnd(queue12.Get(), window, &chain_desc, nullptr, nullptr, &chain)));
  CHECK(SUCCEEDED(chain->Present(0, 0)));

  CHECK(proxy::shared.RegisterModule());
  CHECK(proxy::shared.data != nullptr && proxy::shared.IsEventHandler());
  proxy::use_device_proxy = true;
  proxy::shared.data->use_device_proxy = true;
  proxy::shared.data->settings_dirty = false;
  // The proxy as GetDeviceProxy leaves it (the swap chain owned by device_proxy, released at its teardown).
  proxy::proxy_device = device11.Get();
  proxy::proxy_device_context = context11.Get();
  proxy::proxy_swap_chain = chain.Detach();

  mock::Device host;
  mock::Swapchain host_chain;  // virtual bases: no aggregate
  host_chain.owner = &host;
  host_chain.window = window;
  const auto present = [&host_chain]() {
    proxy::OnPresent(nullptr, &host_chain, nullptr, nullptr, 0, nullptr);
  };

  // 1. Control.
  {
    ComPtr<ID3D12Resource> held;
    CHECK(SUCCEEDED(proxy::proxy_swap_chain->GetBuffer(0, IID_PPV_ARGS(&held))));
    proxy::proxy_device_needs_resize = true;
    present();
    CHECK(Logged("ResizeBuffers failed") == 1u);
    CHECK(Logged("proxy device removed") == 0u);
    CHECK(proxy::proxy_device_needs_resize.load());
    CHECK(proxy::UseProxyRequested() && !proxy::device_proxy_creation_failed);
    CHECK(!proxy::shared.data->remove_device_proxy);
    CHECK(proxy::proxy_swap_chain != nullptr);
  }
  std::printf("1. control: a failed resize on a live device keeps the proxy (resize again next present)\n");

  // 2. Removed.
  device12->RemoveDevice();
  const HRESULT reason = device11->GetDeviceRemovedReason();
  std::printf("   D3D11On12 device removed reason 0x%08lX\n", static_cast<unsigned long>(reason));
  CHECK(reason != S_OK);
  present();
  CHECK(Logged("proxy device removed at ResizeBuffers") == 1u);
  CHECK(Logged("ResizeBuffers failed") == 1u);
  CHECK(proxy::device_proxy_creation_failed);
  CHECK(!proxy::shared.data->use_device_proxy && !proxy::use_device_proxy);
  CHECK(proxy::shared.data->remove_device_proxy);
  std::printf("2. removed: one error line, the proxy off for good, teardown pending\n");

  // 3. Teardown.
  present();
  CHECK(proxy::proxy_swap_chain == nullptr);
  CHECK(!proxy::shared.data->remove_device_proxy);
  CHECK(!proxy::UseProxyRequested() && proxy::device_proxy_creation_failed);
  std::printf("3. teardown: swap chain released, the host presents natively\n");

  // 4. Quiet after.
  host_chain.host_calls = false;
  for (int frame = 0; frame < 4; ++frame) present();
  CHECK(Logged("proxy device removed") == 1u);
  CHECK(Logged("Proxy Present failed") == 0u);
  CHECK(Logged("GetDeviceProxy returned null") == 0u);
  std::printf("4. quiet: 4 presents, no proxy work, one removed line in all\n");

  // Released as OnDestroyDevice would at the host device's end.
  proxy::proxy_device_context = nullptr;
  proxy::proxy_device = nullptr;
  proxy::shared.UnregisterModule();
  DestroyWindow(window);
  return 0;
}

int main() {
  try {
    const int result = Run();
    std::printf(result == 0 ? "PASS\n" : "FAILED\n");
    return result;
  } catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
  }
}
