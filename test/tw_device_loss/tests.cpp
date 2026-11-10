// The Total War addons' device-loss handling and output choice (twattila/device_loss.hpp, output_encoding.hpp) on WARP.
// A removed device cannot be made on WARP: the test answers GetDeviceRemovedReason for chosen devices through the
// device's vtable; everything else (ClearState, Flush, the swap chains, the window rules) is the real thing.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "src/games/twattila/device_loss.hpp"

using Microsoft::WRL::ComPtr;
namespace loss = twattila::device_loss;
namespace output = twattila::output_encoding;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}
static size_t Logged(const char* part) {
  const std::unique_lock lock(log_mutex);
  return static_cast<size_t>(std::ranges::count_if(log_lines, [part](const std::string& line) { return line.find(part) != std::string::npos; }));
}

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

struct Swapchain : swapchain {
  Device* owner = nullptr;
  uint64_t get_native() const override { throw std::runtime_error("unexpected swapchain call: get_native"); }
  device* get_device() override { return owner; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { throw std::runtime_error("unexpected swapchain call: get_private_data"); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { throw std::runtime_error("unexpected swapchain call: set_private_data"); }
  void* get_hwnd() const override { throw std::runtime_error("unexpected swapchain call: get_hwnd"); }
  resource get_back_buffer(uint32_t index) override { throw std::runtime_error("unexpected swapchain call: get_back_buffer"); }
  uint32_t get_back_buffer_count() const override { throw std::runtime_error("unexpected swapchain call: get_back_buffer_count"); }
  uint32_t get_current_back_buffer_index() const override { throw std::runtime_error("unexpected swapchain call: get_current_back_buffer_index"); }
  bool check_color_space_support(color_space color_space) const override { throw std::runtime_error("unexpected swapchain call: check_color_space_support"); }
  color_space get_color_space() const override { throw std::runtime_error("unexpected swapchain call: get_color_space"); }
};
}  // namespace mock

// GetDeviceRemovedReason of chosen devices: DXGI_ERROR_DEVICE_HUNG. Who asked, and on which thread.
struct Asked {
  ID3D11Device* device;
  DWORD thread;
};
using RemovedReason = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*);
constexpr size_t FEATURE_LEVEL_SLOT = 37;
constexpr size_t CREATION_FLAGS_SLOT = 38;
constexpr size_t REMOVED_REASON_SLOT = 39;
static std::mutex removed_mutex;
static std::vector<ID3D11Device*> removed_devices;
static std::vector<Asked> asked;
static std::vector<std::pair<void**, RemovedReason>> patched;

static HRESULT STDMETHODCALLTYPE FakeRemovedReason(ID3D11Device* self) {
  const std::unique_lock lock(removed_mutex);
  asked.push_back({self, GetCurrentThreadId()});
  if (std::ranges::find(removed_devices, self) != removed_devices.end()) return DXGI_ERROR_DEVICE_HUNG;
  void** vtable = *reinterpret_cast<void***>(self);
  for (const auto& [table, original] : patched) {
    if (table == vtable) return original(self);
  }
  return S_OK;
}

static bool Patch(ID3D11Device* device) {
  const std::unique_lock lock(removed_mutex);
  void** vtable = *reinterpret_cast<void***>(device);
  for (const auto& [table, original] : patched) {
    if (table == vtable) return true;
  }
  // The slots are counted from d3d11.h: its two neighbours answer as the device does, or the count is wrong.
  using CreationFlags = UINT(STDMETHODCALLTYPE*)(ID3D11Device*);
  using FeatureLevel = D3D_FEATURE_LEVEL(STDMETHODCALLTYPE*)(ID3D11Device*);
  if (reinterpret_cast<CreationFlags>(vtable[CREATION_FLAGS_SLOT])(device) != device->GetCreationFlags()) return false;
  if (reinterpret_cast<FeatureLevel>(vtable[FEATURE_LEVEL_SLOT])(device) != device->GetFeatureLevel()) return false;
  DWORD protection = 0;
  if (VirtualProtect(&vtable[REMOVED_REASON_SLOT], sizeof(void*), PAGE_READWRITE, &protection) == FALSE) return false;
  patched.emplace_back(vtable, reinterpret_cast<RemovedReason>(vtable[REMOVED_REASON_SLOT]));
  vtable[REMOVED_REASON_SLOT] = reinterpret_cast<void*>(&FakeRemovedReason);
  VirtualProtect(&vtable[REMOVED_REASON_SLOT], sizeof(void*), protection, &protection);
  return true;
}

static void Remove(ID3D11Device* device) {
  const std::unique_lock lock(removed_mutex);
  removed_devices.push_back(device);
}

static size_t AskedOffThread(ID3D11Device* device, DWORD thread) {
  const std::unique_lock lock(removed_mutex);
  return static_cast<size_t>(std::ranges::count_if(asked, [=](const Asked& entry) { return entry.device == device && entry.thread != thread; }));
}

static size_t AskedOnThread(ID3D11Device* device, DWORD thread) {
  const std::unique_lock lock(removed_mutex);
  return static_cast<size_t>(std::ranges::count_if(asked, [=](const Asked& entry) { return entry.device == device && entry.thread == thread; }));
}

static ComPtr<IDXGIFactory1> factory;

static bool MakeDevice(mock::Device* device, ComPtr<ID3D11DeviceContext>* context, UINT flags = 0) {
  return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, device->native.GetAddressOf(),
                                     nullptr, context->GetAddressOf()))
         && Patch(device->native.Get());
}

static HWND MakeWindow(int show) {
  HWND window = CreateWindowExW(0, L"STATIC", L"tw_device_loss", WS_OVERLAPPEDWINDOW, 40, 40, 320, 200, nullptr, nullptr, nullptr, nullptr);
  ShowWindow(window, show);
  return window;
}

static HRESULT MakeChain(ID3D11Device* device, HWND window, DXGI_SWAP_EFFECT effect, IDXGISwapChain** chain) {
  DXGI_SWAP_CHAIN_DESC desc = {
      .BufferDesc = {.Width = 64, .Height = 64, .Format = DXGI_FORMAT_R16G16B16A16_FLOAT},
      .SampleDesc = {.Count = 1},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .OutputWindow = window,
      .Windowed = TRUE,
      .SwapEffect = effect,
  };
  return factory->CreateSwapChain(device, &desc, chain);
}

// A game's flip chain after the game let go of it: its back buffer's view still bound, no reference left.
static bool LeaveBoundChain(ID3D11Device* device, ID3D11DeviceContext* context, HWND window) {
  ComPtr<IDXGISwapChain> chain;
  ComPtr<ID3D11Texture2D> back;
  ComPtr<ID3D11RenderTargetView> view;
  if (FAILED(MakeChain(device, window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &chain)) || FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&back)))
      || FAILED(device->CreateRenderTargetView(back.Get(), nullptr, &view))) {
    return false;
  }
  context->OMSetRenderTargets(1, view.GetAddressOf(), nullptr);
  const FLOAT clear[4] = {};
  context->ClearRenderTargetView(view.Get(), clear);
  return SUCCEEDED(chain->Present(0, 0));
}

static reshade::api::swapchain_desc GameDesc(DXGI_SWAP_EFFECT effect) {
  reshade::api::swapchain_desc desc = {};
  desc.back_buffer.texture.width = 64;
  desc.back_buffer.texture.height = 64;
  desc.back_buffer.texture.format = reshade::api::format::r8g8b8a8_unorm;
  desc.back_buffer.usage = reshade::api::resource_usage::render_target;
  desc.back_buffer_count = 2;
  desc.present_mode = effect;
  return desc;
}

// The header's state as a fresh image has it.
static void Reset() {
  loss::StopWatch();
  const std::unique_lock lock(loss::devices_mutex);
  loss::devices.clear();
  loss::removed_logged.clear();
  loss::removal_check_wanted = false;
  loss::presented = false;
  loss::awaiting_frame = false;
  loss::after_loss = false;
  loss::resizing = false;
  loss::keep_game_desc = false;
  loss::native_flip_window = nullptr;
  loss::last_present_tick = 0;
}

static void Pump(DWORD milliseconds) {
  const ULONGLONG end = GetTickCount64() + milliseconds;
  while (GetTickCount64() < end) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
      DispatchMessageW(&message);
    }
    Sleep(20);
  }
}

constexpr int SKIPPED = 77;

// A game after a device loss, its limit 3 s: what its window does decides whether the limit closes it.
static int Child(const std::wstring& mode) {
  loss::no_frame_limit_ms = 3000;
  float encoding = 0.f;
  output::Configure({.shader_encoding = &encoding, .setting = output::SDR});
  int show = SW_SHOWNOACTIVATE;
  if (mode == L"front") show = SW_SHOW;
  if (mode == L"minimised") show = SW_SHOWMINNOACTIVE;
  HWND window = MakeWindow(show);
  if (mode == L"front") SetForegroundWindow(window);
  mock::Device device;
  ComPtr<ID3D11DeviceContext> context;
  if (!MakeDevice(&device, &context)) return 3;
  loss::OnInitDevice(&device);
  Remove(device.native.Get());
  auto desc = GameDesc(DXGI_SWAP_EFFECT_FLIP_DISCARD);
  loss::BeforeSwapchainMod(reshade::api::device_api::d3d11, desc, window);
  if (!loss::awaiting_frame) return 4;
  Pump(1000);
  if (mode == L"front") {
    if (GetForegroundWindow() != window) return SKIPPED;
    Pump(15000);
    return 0;  // not reached: closed with 1
  }
  if (mode == L"hung") {
    Sleep(40000);
    return 0;  // not reached: closed with 1
  }
  if (mode == L"presents") {
    mock::Swapchain chain;
    chain.owner = &device;
    loss::OnPresent(nullptr, &chain, nullptr, nullptr, 0, nullptr);
    Pump(6000);
    return (Logged("the game draws again") == 1 && !loss::awaiting_frame) ? 0 : 5;
  }
  // background, minimised
  for (int second = 0; second < 12; ++second) {
    if (loss::GameInFront()) return SKIPPED;
    Pump(1000);
  }
  return 0;
}

static int Parent(const wchar_t* self) {
  // The limit's children first: they run beside the rest.
  const wchar_t* const modes[] = {L"front", L"background", L"minimised", L"hung", L"presents"};
  HANDLE children[std::size(modes)] = {};
  for (size_t index = 0; index < std::size(modes); ++index) {
    std::wstring command = std::wstring(L"\"") + self + L"\" " + modes[index];
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process = {};
    CHECK(CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE);
    CloseHandle(process.hThread);
    children[index] = process.hProcess;
  }

  // The output for a setting and what Windows says of the display.
  float encoding = -1.f;
  output::Configure({.shader_encoding = &encoding, .setting = output::AUTO});
  CHECK(encoding == 1.f);
  CHECK(renodx::mods::swapchain::target_format == reshade::api::format::r16g16b16a16_float);
  CHECK(output::Encoding(output::AUTO, {.known = true, .on = true}) == output::SCRGB);
  CHECK(output::Encoding(output::AUTO, {.known = true, .on = false}) == output::SDR);
  CHECK(output::Encoding(output::AUTO, {.known = false, .on = false}) == output::SDR);
  CHECK(output::Encoding(output::AUTO, {.known = false, .on = true}) == output::SCRGB);
  CHECK(output::Encoding(output::HDR10, {.known = true, .on = true}) == output::HDR10);
  CHECK(output::Encoding(output::HDR10, {.known = true, .on = false}) == output::SDR);
  CHECK(output::Encoding(output::SCRGB, {.known = true, .on = false}) == output::SDR);
  CHECK(output::Encoding(output::HDR10, {.known = false, .on = false}) == output::HDR10);
  CHECK(output::Encoding(output::SCRGB, {.known = false, .on = false}) == output::SCRGB);
  CHECK(output::Encoding(output::SDR, {.known = true, .on = true}) == output::SDR);
  output::Configure({.shader_encoding = &encoding,
                     .setting = output::AUTO,
                     .hdr_encoding = [](int setting) { return (setting == output::SCRGB) ? output::SCRGB : output::HDR10; }});
  CHECK(encoding == 0.f);
  CHECK(renodx::mods::swapchain::target_format == reshade::api::format::r10g10b10a2_unorm);
  CHECK(renodx::mods::swapchain::target_color_space == reshade::api::color_space::hdr10_st2084);
  CHECK(output::Encoding(output::SCRGB, {.known = true, .on = true}) == output::SCRGB);
  CHECK(output::Encoding(output::AUTO, {.known = true, .on = false}) == output::SDR);
  output::Apply(output::SDR);
  CHECK(encoding == 2.f);
  CHECK(renodx::mods::swapchain::target_format == reshade::api::format::r10g10b10a2_unorm);
  CHECK(renodx::mods::swapchain::target_color_space == reshade::api::color_space::srgb_nonlinear);
  output::Configure({.shader_encoding = &encoding, .setting = output::SCRGB});

  CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
  const DWORD this_thread = GetCurrentThreadId();

  // 1. The new device made beside the removed one (WH3's order).
  {
    HWND window = MakeWindow(SW_SHOWNOACTIVATE);
    HWND other_window = MakeWindow(SW_SHOWNOACTIVATE);
    mock::Device old_device;
    mock::Device new_device;
    ComPtr<ID3D11DeviceContext> old_context;
    ComPtr<ID3D11DeviceContext> new_context;
    CHECK(MakeDevice(&old_device, &old_context));
    CHECK(MakeDevice(&new_device, &new_context));
    old_device.native->AddRef();
    const ULONG references_before = old_device.native->Release();
    loss::OnInitDevice(&old_device);
    loss::OnInitDevice(&new_device);
    CHECK(LeaveBoundChain(old_device.native.Get(), old_context.Get(), window));
    ComPtr<IDXGISwapChain> chain;
    // The fault: the window is still the old chain's.
    CHECK(MakeChain(new_device.native.Get(), window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &chain) == E_ACCESSDENIED);
    // No removed device: the step leaves a device alone.
    auto desc = GameDesc(DXGI_SWAP_EFFECT_FLIP_DISCARD);
    const size_t outputs_before = Logged("[Total War] Output: ");
    CHECK(!loss::BeforeSwapchainMod(reshade::api::device_api::d3d11, desc, window));
    CHECK(Logged("[Total War] Output: ") == outputs_before + 1);
    CHECK(!loss::after_loss && !loss::awaiting_frame && loss::native_flip_window == window);
    CHECK(MakeChain(new_device.native.Get(), window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &chain) == E_ACCESSDENIED);
    // Removed: its bindings go, the window is free.
    Remove(old_device.native.Get());
    CHECK(!loss::BeforeSwapchainMod(reshade::api::device_api::d3d11, desc, window));
    CHECK(loss::after_loss && loss::awaiting_frame && !loss::keep_game_desc);
    CHECK(Logged("GPU reset: the game's device removed (0x887A0006)") == 1);
    CHECK(loss::devices.size() == 1 && loss::devices[0].device == new_device.native.Get());
    CHECK(SUCCEEDED(MakeChain(new_device.native.Get(), window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &chain)));
    old_device.native->AddRef();
    CHECK(old_device.native->Release() == references_before);
    // The game's blt fallback for its flip window stays as asked, the mod's upgrade forgotten, the output SDR.
    auto fallback = GameDesc(DXGI_SWAP_EFFECT_DISCARD);
    CHECK(!loss::BeforeSwapchainMod(reshade::api::device_api::d3d11, fallback, window));
    CHECK(loss::keep_game_desc);
    fallback.present_mode = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    fallback.back_buffer.texture.format = reshade::api::format::r16g16b16a16_float;
    renodx::mods::swapchain::upgraded_swapchain_desc = fallback;
    CHECK(loss::AfterSwapchainMod(reshade::api::device_api::d3d11, fallback, window));
    CHECK(fallback.present_mode == DXGI_SWAP_EFFECT_DISCARD && fallback.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm);
    CHECK(!renodx::mods::swapchain::upgraded_swapchain_desc.has_value());
    CHECK(encoding == 2.f);
    CHECK(!loss::AfterSwapchainMod(reshade::api::device_api::d3d11, fallback, window));
    // A blt request for another window is not that fallback.
    auto other = GameDesc(DXGI_SWAP_EFFECT_DISCARD);
    loss::BeforeSwapchainMod(reshade::api::device_api::d3d11, other, other_window);
    CHECK(!loss::keep_game_desc);
    // A ResizeBuffers of an upgraded chain shows flip without the game having asked for it, and decides no output.
    const size_t outputs = Logged("[Total War] Output: ");
    loss::OnDestroySwapchain(nullptr, true);
    auto resized = GameDesc(DXGI_SWAP_EFFECT_FLIP_DISCARD);
    loss::BeforeSwapchainMod(reshade::api::device_api::d3d11, resized, other_window);
    CHECK(loss::native_flip_window == window && Logged("[Total War] Output: ") == outputs && !loss::resizing);
    // The first frame ends the wait.
    mock::Swapchain presented_chain;
    presented_chain.owner = &new_device;
    loss::OnPresent(nullptr, &presented_chain, nullptr, nullptr, 0, nullptr);
    Sleep(2500);
    CHECK(!loss::awaiting_frame && Logged("GPU reset: the game draws again.") == 1);
    chain.Reset();
    loss::OnDestroyDevice(&new_device);
    loss::OnDestroyDevice(&old_device);
    CHECK(loss::devices.empty() && loss::watch_thread == nullptr);
    DestroyWindow(window);
    DestroyWindow(other_window);
    Reset();
  }

  // 2. The old device destroyed before the new swap chain (Attila's order).
  {
    HWND window = MakeWindow(SW_SHOWNOACTIVATE);
    mock::Device old_device;
    mock::Device new_device;
    ComPtr<ID3D11DeviceContext> old_context;
    ComPtr<ID3D11DeviceContext> new_context;
    CHECK(MakeDevice(&old_device, &old_context));
    CHECK(MakeDevice(&new_device, &new_context));
    old_device.native->AddRef();
    const ULONG references_before = old_device.native->Release();
    CHECK(LeaveBoundChain(old_device.native.Get(), old_context.Get(), window));
    ComPtr<IDXGISwapChain> chain;
    // A device that is not removed is left as it is at its destroy.
    loss::OnInitDevice(&old_device);
    loss::OnDestroyDevice(&old_device);
    CHECK(loss::devices.empty() && loss::watch_thread == nullptr);
    CHECK(MakeChain(new_device.native.Get(), window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &chain) == E_ACCESSDENIED);
    const size_t removals = Logged("GPU reset: the game's device removed");
    loss::OnInitDevice(&old_device);
    Remove(old_device.native.Get());
    loss::OnDestroyDevice(&old_device);
    CHECK(Logged("GPU reset: the game's device removed") == removals + 1);
    CHECK(loss::devices.empty() && loss::removed_logged.empty() && loss::watch_thread == nullptr);
    CHECK(SUCCEEDED(MakeChain(new_device.native.Get(), window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &chain)));
    old_device.native->AddRef();
    CHECK(old_device.native->Release() == references_before);
    chain.Reset();
    DestroyWindow(window);
    Reset();
  }

  // 3. A game that always asks for blt (Attila, were its new device made beside the old one): its request is upgraded as ever.
  {
    HWND window = MakeWindow(SW_SHOWNOACTIVATE);
    mock::Device old_device;
    ComPtr<ID3D11DeviceContext> old_context;
    CHECK(MakeDevice(&old_device, &old_context));
    loss::OnInitDevice(&old_device);
    auto desc = GameDesc(DXGI_SWAP_EFFECT_DISCARD);
    loss::BeforeSwapchainMod(reshade::api::device_api::d3d11, desc, window);
    Remove(old_device.native.Get());
    loss::BeforeSwapchainMod(reshade::api::device_api::d3d11, desc, window);
    CHECK(loss::after_loss && !loss::keep_game_desc && loss::native_flip_window == nullptr);
    CHECK(!loss::AfterSwapchainMod(reshade::api::device_api::d3d11, desc, window));
    DestroyWindow(window);
    Reset();
  }

  // 4. Who asks a device for its removed reason, and the frame thread's place when frames stop.
  {
    mock::Device single_threaded;
    mock::Device multi_threaded;
    ComPtr<ID3D11DeviceContext> single_context;
    ComPtr<ID3D11DeviceContext> multi_context;
    CHECK(MakeDevice(&single_threaded, &single_context, D3D11_CREATE_DEVICE_SINGLETHREADED));
    CHECK(MakeDevice(&multi_threaded, &multi_context));
    loss::OnInitDevice(&single_threaded);
    loss::OnInitDevice(&multi_threaded);
    CHECK(loss::devices[0].single_threaded && !loss::devices[1].single_threaded);
    Sleep(2500);
    CHECK(AskedOffThread(single_threaded.native.Get(), this_thread) == 0);
    CHECK(AskedOffThread(multi_threaded.native.Get(), this_thread) >= 1);
    CHECK(loss::removal_check_wanted);
    const size_t removals = Logged("GPU reset: the game's device removed");
    Remove(single_threaded.native.Get());
    Remove(multi_threaded.native.Get());
    Sleep(2500);
    // The watch found the one it may ask; the other waits for its own thread's present.
    CHECK(Logged("GPU reset: the game's device removed") == removals + 1);
    const size_t asked_before = AskedOnThread(single_threaded.native.Get(), this_thread);
    mock::Swapchain chain;
    chain.owner = &single_threaded;
    loss::OnPresent(nullptr, &chain, nullptr, nullptr, 0, nullptr);
    CHECK(AskedOnThread(single_threaded.native.Get(), this_thread) == asked_before + 1);
    CHECK(Logged("GPU reset: the game's device removed") == removals + 2);
    CHECK(AskedOffThread(single_threaded.native.Get(), this_thread) == 0);
    // No second line for the same device.
    Sleep(1500);
    loss::OnPresent(nullptr, &chain, nullptr, nullptr, 0, nullptr);
    CHECK(Logged("GPU reset: the game's device removed") == removals + 2);
    // No frame for 20 s: one line with this thread's place, a module and an offset.
    Sleep(21500);
    CHECK(Logged("No frame for 2") == 1);
    {
      const std::unique_lock lock(log_mutex);
      const auto line = std::ranges::find_if(log_lines, [](const std::string& text) { return text.find("No frame for 2") != std::string::npos; });
      CHECK(line->find("the frame thread at: ") != std::string::npos && line->find("+0x") != std::string::npos);
      std::printf("%s\n", line->c_str());
    }
    CHECK(AskedOffThread(single_threaded.native.Get(), this_thread) == 0);
    loss::OnDestroyDevice(&single_threaded);
    loss::OnDestroyDevice(&multi_threaded);
    Reset();
  }

  // 5. The limit after a loss: front and not answering are closed (exit 1), background, minimised and drawing games are not.
  const DWORD expected[std::size(modes)] = {1, 0, 0, 1, 0};
  int skipped = 0;
  for (size_t index = 0; index < std::size(modes); ++index) {
    DWORD exit_code = STILL_ACTIVE;
    if (WaitForSingleObject(children[index], 60000) != WAIT_OBJECT_0) TerminateProcess(children[index], 99);
    WaitForSingleObject(children[index], 5000);
    GetExitCodeProcess(children[index], &exit_code);
    CloseHandle(children[index]);
    std::printf("limit, %ls: exit %lu\n", modes[index], exit_code);
    if (exit_code == SKIPPED) {
      ++skipped;
      continue;
    }
    CHECK(exit_code == expected[index]);
  }
  std::printf("PASS (%d limit cases skipped: the window could not be put in front or kept behind)\n", skipped);
  return 0;
}

int wmain(int argc, wchar_t** argv) {
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  if (argc > 1) return Child(argv[1]);
  return Parent(argv[0]);
}
