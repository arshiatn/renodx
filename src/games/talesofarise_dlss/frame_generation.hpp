/*
 * Copyright (C) 2026 ArshiaTN
 * Copyright (c) 2026 Bustanity (Phantom FX: the stand-in swap chain and the Streamline route this is ported from)
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_security.h>

#include <deps/imgui/imgui.h>
#include <embed/shaders.h>
#include <include/reshade.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "./matrix.hpp"
#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "./d3d12_device.hpp"
#include "./native_state.hpp"
#include "./ngx_backend.hpp"

// DLSS Frame Generation through NVIDIA Streamline, ported from Phantom FX by Bustanity (MIT, PHANTOM_FX_LICENSE.txt).
// Frame generation presents through D3D12: the game gets a stand-in swap chain whose back buffer is a D3D11 texture
// shared with D3D12, and each present copies it into a D3D12 swap chain made through Streamline. The hook is DXGI's own
// CreateSwapChain, behind ReShade: ReShade and the HDR addon wrap the stand-in as the game's swap chain (overlay,
// effects, HDR output) and still see a D3D11 game. Streamline (sl.interposer.dll and its plugins sl.common, sl.dlss_g,
// sl.reflex, sl.pcl, with nvngx_dlssg.dll) is loaded from the addon's folder, only when the setting was on at launch.
// Shared by the DLSS addons that present this way (Tales, Expeditions: UE; Ryse: CryEngine), each with its camera's
// conventions (Camera) and its swap chain call (CreateSwapChain or CreateSwapChainForHwnd).
namespace talesofarise::frame_generation {

using Microsoft::WRL::ComPtr;

// Frames presented per rendered frame: 2-6 (the GPU's maximum limits it); 0 lets frame generation choose them for the
// display's refresh rate (Dynamic, where the GPU has it); 1 generates none (Reflex only). Applies at the next present.
static std::atomic<uint32_t> multiplier = 2;
// NVIDIA Reflex (always on with frame generation, which paces its frames with it): Boost also keeps the GPU's clocks up.
static std::atomic_bool reflex_boost = false;
// Debug (ReShade.ini FrameGenerationReflexSleep, default on): Reflex's sleep after each present. Off keeps its markers.
static std::atomic_bool reflex_sleep = true;
// Reflex's frame rate limit: rendered frames per second (frame generation multiplies them); 0: none. Steady frames keep
// a VRR display's refresh steady (OLEDs flicker as it changes).
static std::atomic<uint32_t> frame_limit = 0;

// The camera as Streamline takes it: matrices for row vectors (v x M, as UE's), without the jitter; its near and far
// planes and whether depth is reversed (UE: near from the projection, UE's world size as far, reversed).
struct Camera {
  std::array<float, 16> view_to_clip = {};
  std::array<float, 16> clip_to_previous_clip = {};
  std::array<float, 3> position = {};
  std::array<float, 3> right = {};
  std::array<float, 3> up = {};
  std::array<float, 3> forward = {};
  float near_plane = 0.f;
  float far_plane = 0.f;
  bool depth_inverted = true;
};

// The DLSS evaluation's inputs for the frame about to be presented: depth (device Z) and motion (render pixels
// toward the last frame, without the jitter) at the render size, and the jitter in render pixels (+y down, as DLSS takes
// it).
struct Inputs {
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11Texture2D> motion;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  std::array<float, 2> jitter = {};
  // The motion's scale to render pixels (as DLSS's MVScale; Shogun 2's game DLSS passes its own).
  std::array<float, 2> motion_scale = {1.f, 1.f};
  bool reset = false;
  Camera camera;
};

namespace internal {

// IDXGIFactory::CreateSwapChain and IDXGIFactory2::CreateSwapChainForHwnd in DXGI's factory function table (UE 4.21
// makes its swap chain with the first).
constexpr size_t CREATE_SWAP_CHAIN_SLOT = 10;
constexpr size_t CREATE_SWAP_CHAIN_FOR_HWND_SLOT = 15;
// Command allocators in rotation; the swap chain lets at most three frames queue.
constexpr size_t FRAMES_IN_FLIGHT = 4;
constexpr DWORD FENCE_TIMEOUT = 10'000;
// RenoDX's shader injection constants (the HDR addon's shared.h), bound for its replaced shaders' draws.
constexpr UINT INJECTION_SLOT = 13;
const sl::ViewportHandle VIEWPORT = {0u};

using CreateSwapChainFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                                            IDXGISwapChain** swap_chain);
using CreateSwapChainForHwndFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2* factory, IUnknown* device, HWND window,
                                                                  const DXGI_SWAP_CHAIN_DESC1* desc,
                                                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
                                                                  IDXGIOutput* output, IDXGISwapChain1** swap_chain);

// Streamline's functions, from sl.interposer.dll and the plugins it loads.
struct Api {
  HMODULE module = nullptr;
  PFun_slInit* init = nullptr;
  PFun_slShutdown* shutdown = nullptr;
  PFun_slIsFeatureSupported* is_feature_supported = nullptr;
  PFun_slSetD3DDevice* set_device = nullptr;
  PFun_slUpgradeInterface* upgrade_interface = nullptr;
  PFun_slGetNativeInterface* get_native_interface = nullptr;
  PFun_slGetFeatureFunction* get_feature_function = nullptr;
  PFun_slGetNewFrameToken* get_frame_token = nullptr;
  PFun_slSetTagForFrame* set_tags = nullptr;
  PFun_slSetConstants* set_constants = nullptr;
  PFun_slDLSSGSetOptions* set_options = nullptr;
  PFun_slDLSSGGetState* get_state = nullptr;
  PFun_slReflexSetOptions* set_reflex_options = nullptr;
  PFun_slReflexSleep* sleep = nullptr;
  PFun_slPCLSetMarker* set_marker = nullptr;
};

// A D3D11 texture shared with D3D12, and D3D12's view of it.
struct SharedTexture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D12Resource> resource;
};

// What Streamline was last told to generate.
struct Mode {
  bool on = false;
  bool dynamic = false;
  uint32_t generated_frames = 1;
  DXGI_FORMAT hudless_format = DXGI_FORMAT_UNKNOWN;  // the back buffer's
  bool operator==(const Mode&) const = default;
};

// The frame's inputs, from the addon's present event to the stand-in's Present (both on the presenting thread).
struct Submitted {
  uint32_t frame = 0;  // Streamline's frame index; 0: nothing submitted
  bool valid = false;  // DLSS ran on this frame: its depth and motion are copied
  bool hudless = false;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  std::array<float, 2> jitter = {};
  std::array<float, 2> motion_scale = {1.f, 1.f};
  bool reset = false;
  Camera camera;
};

struct State {
  std::shared_mutex mutex;
  std::string status;  // empty until Install()
  // The stand-in's devices while it lives, its back buffer's format and size, and the inputs shared with D3D12.
  ComPtr<ID3D11Device5> device11;
  ComPtr<ID3D12Device> device12;
  DXGI_FORMAT back_buffer_format = DXGI_FORMAT_UNKNOWN;
  uint32_t width = 0;
  uint32_t height = 0;
  SharedTexture depth;
  SharedTexture motion;
  SharedTexture hudless;
  uint64_t hudless_frame = UINT64_MAX;
  // The hudless colour's pass: the HDR addon's swap chain shader (0x7A1E0D40 / 41), or a copy (0x7A1E0D42).
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> encode_shader;
  ComPtr<ID3D11PixelShader> copy_shader;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11Texture2D> scene;
  ComPtr<ID3D11ShaderResourceView> scene_srv;
  Submitted submitted;
  // Streamline's state, as last reported.
  Mode applied;
  uint32_t max_multiplier = 0;
  bool dynamic_supported = false;
  uint64_t presents = 0;
};

static std::filesystem::path addon_directory;
// The game's name in the log ("[<name> frame generation]").
static std::string game_name = "Tales";
static Api api;
static bool streamline_initialized = false;
static std::atomic_bool attached = false;
static std::atomic_bool installed = false;
static std::atomic_bool presenting = false;
// DXGI's own CreateSwapChain and CreateSwapChainForHwnd, past the detours.
static CreateSwapChainFunction original_create_swap_chain = nullptr;
static CreateSwapChainForHwndFunction original_create_swap_chain_for_hwnd = nullptr;
static State state;

inline void SetStatus(std::string text) {
  renodx::utils::log::i("[", game_name, " frame generation] ", text);
  const std::unique_lock lock(state.mutex);
  state.status = std::move(text);
}

// A failed D3D call, with its result for the game when it happens inside a swap chain method.
struct Failure : std::runtime_error {
  HRESULT result;
  Failure(const std::string& what, HRESULT result) : std::runtime_error(what), result(result) {}
};

inline void Check(HRESULT result, const char* what) {
  if (FAILED(result)) throw Failure(std::format("{} failed ({:08X}).", what, static_cast<uint32_t>(result)), result);
}

inline void Check(sl::Result result, const char* what) {
  if (result != sl::Result::eOk) throw std::runtime_error(std::format("{} failed ({}).", what, sl::getResultAsStr(result)));
}

// A failure in a swap chain method, logged once: some repeat every frame.
inline void LogFailure(const std::exception& failure) {
  static std::atomic_bool logged = false;
  if (!logged.exchange(true)) renodx::utils::log::w("[", game_name, " frame generation] ", failure.what());
}

// A per-frame Streamline call's failure, logged once per call.
inline void Report(sl::Result result, const char* what) {
  if (result == sl::Result::eOk) return;
  static std::shared_mutex logged_mutex;
  static std::vector<const char*> logged;
  const std::unique_lock lock(logged_mutex);
  if (std::ranges::find(logged, what) != logged.end()) return;
  logged.push_back(what);
  renodx::utils::log::w("[", game_name, " frame generation] ", what, " failed (", sl::getResultAsStr(result), ").");
}

template <typename Function>
void Resolve(Function** function, const char* name) {
  *function = reinterpret_cast<Function*>(GetProcAddress(api.module, name));
  if (*function == nullptr) throw std::runtime_error(std::format("sl.interposer.dll lacks {}.", name));
}

template <typename Function>
void Resolve(sl::Feature feature, Function** function, const char* name) {
  void* address = nullptr;
  Check(api.get_feature_function(feature, name, address), name);
  *function = reinterpret_cast<Function*>(address);
}

inline sl::FrameToken* Token(uint32_t frame) {
  sl::FrameToken* token = nullptr;
  return (api.get_frame_token(token, &frame) == sl::Result::eOk ? token : nullptr);
}

inline void Marker(sl::PCLMarker marker, uint32_t frame) {
  if (sl::FrameToken* token = Token(frame)) Report(api.set_marker(marker, *token), "Setting a PC Latency marker");
}

// Streamline's proxy of a native device or factory, through which the stand-in makes its queue and swap chain.
template <typename Interface>
ComPtr<Interface> Upgraded(IUnknown* native, const char* what) {
  // The proxy holds its own reference to the native object; when Streamline makes none, the object comes back as it
  // is and gets one for the caller.
  void* object = native;
  if (api.upgrade_interface(&object) != sl::Result::eOk) throw std::runtime_error(std::format("Streamline did not take {}.", what));
  if (object == native) native->AddRef();
  ComPtr<IUnknown> proxy;
  proxy.Attach(static_cast<IUnknown*>(object));
  ComPtr<Interface> result;
  if (FAILED(proxy.As(&result))) throw std::runtime_error(std::format("Streamline did not take {}.", what));
  return result;
}

// Releases Streamline, before the devices it was given go.
inline void Stop() {
  attached = false;
  if (streamline_initialized) api.shutdown();
  streamline_initialized = false;
  const std::unique_lock lock(state.mutex);
  state.applied = {};
}

// Flip model swap chains take no sRGB formats; the stand-in's back buffer keeps the game's, and the copy moves the same
// bits, so the real swap chain holds what the game's would.
inline DXGI_FORMAT Presentable(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:                              return format;
  }
}

// What the game holds in place of its swap chain: it renders into the shared back buffer; calls that do not concern the
// back buffer pass on to the real swap chain (Streamline's proxy).
class SwapChain final : public IDXGISwapChain4 {
 public:
  SwapChain(IDXGIFactory* factory, IUnknown* device, const DXGI_SWAP_CHAIN_DESC& requested)
      : window_(requested.OutputWindow), desc_(requested) {
    // ReShade passes its proxy's device on unwrapped; without ReShade's DXGI the game's own comes.
    ComPtr<IUnknown> native_device;
    if (FAILED(device->QueryInterface(d3d12_device::UNWRAPPED_OBJECT, reinterpret_cast<void**>(native_device.GetAddressOf())))) {
      native_device = device;
    }
    Check(native_device.As(&device11_), "Finding D3D11.4");
    ComPtr<ID3D11DeviceContext> context;
    device11_->GetImmediateContext(context.GetAddressOf());
    Check(context.As(&context11_), "Finding the D3D11.4 context");
    Check(device11_->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(fence11_.GetAddressOf())), "Creating the shared fence");

    d3d12_device::Device d3d12;
    if (const std::string reason = d3d12_device::Create(device11_.Get(), &d3d12); !reason.empty()) throw std::runtime_error(reason);
    device12_wrapper_ = d3d12.wrapper;
    device12_ = d3d12.native;

    // Streamline gets the native device; frame generation paces its frames with Reflex.
    Check(api.set_device(device12_.Get()), "Giving Streamline the D3D12 device");
    Resolve(sl::kFeatureDLSS_G, &api.set_options, "slDLSSGSetOptions");
    Resolve(sl::kFeatureDLSS_G, &api.get_state, "slDLSSGGetState");
    Resolve(sl::kFeatureReflex, &api.set_reflex_options, "slReflexSetOptions");
    Resolve(sl::kFeatureReflex, &api.sleep, "slReflexSleep");
    Resolve(sl::kFeaturePCL, &api.set_marker, "slPCLSetMarker");
    reflex_boost_ = reflex_boost.load();
    frame_limit_ = frame_limit.load();
    Check(api.set_reflex_options(ReflexOptions(reflex_boost_, frame_limit_)), "Turning on Reflex");
    attached = true;

    // Streamline's proxies of the device and queue make its swap chain. The copies run on a queue of their own: frame
    // generation blocks the presenting queue while it works, and D3D11's next frame waits only for its copy.
    streamline_device_ = Upgraded<ID3D12Device>(device12_.Get(), "the D3D12 device");
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    Check(streamline_device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(streamline_queue_.GetAddressOf())), "Creating the D3D12 queue");
    void* native_queue = nullptr;
    if (api.get_native_interface(streamline_queue_.Get(), &native_queue) != sl::Result::eOk || native_queue == nullptr) {
      throw std::runtime_error("Finding the native D3D12 queue failed.");
    }
    ComPtr<IUnknown> queue_object;
    queue_object.Attach(static_cast<IUnknown*>(native_queue));
    Check(queue_object.As(&present_queue_), "Finding the native D3D12 queue");
    Check(device12_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(copy_queue_.GetAddressOf())), "Creating the D3D12 copy queue");

    HANDLE fence_handle = nullptr;
    Check(fence11_->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle), "Sharing the fence");
    const HRESULT fence_opened = device12_->OpenSharedHandle(fence_handle, IID_PPV_ARGS(fence12_.GetAddressOf()));
    CloseHandle(fence_handle);
    Check(fence_opened, "Opening the shared fence");
    for (auto& allocator : allocators_) {
      Check(device12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf())), "Creating a command allocator");
    }
    Check(device12_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr, IID_PPV_ARGS(list_.GetAddressOf())),
          "Creating the command list");
    Check(list_->Close(), "Closing the command list");
    fence_event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!fence_event_) throw std::runtime_error("Creating the fence event failed.");

    // D3D12 presents only with flip model, and the copies need nothing beyond output. Frame generation presents its
    // frames with tearing when the display allows it (else every present fails).
    ComPtr<IDXGIFactory2> factory2;
    Check(factory->QueryInterface(IID_PPV_ARGS(factory2.GetAddressOf())), "Finding IDXGIFactory2");
    if (ComPtr<IDXGIFactory5> factory5; SUCCEEDED(factory2.As(&factory5))) {
      BOOL tearing = FALSE;
      if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) {
        tearing_flag_ = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
      }
    }
    const DXGI_SWAP_CHAIN_DESC1 real = {
        .Width = desc_.BufferDesc.Width,
        .Height = desc_.BufferDesc.Height,
        .Format = Presentable(desc_.BufferDesc.Format),
        .Stereo = FALSE,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = std::max(desc_.BufferCount, 2u),
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = desc_.Flags | tearing_flag_,
    };
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen = {
        .RefreshRate = desc_.BufferDesc.RefreshRate,
        .ScanlineOrdering = desc_.BufferDesc.ScanlineOrdering,
        .Scaling = desc_.BufferDesc.Scaling,
        .Windowed = desc_.Windowed,
    };
    const auto streamline_factory = Upgraded<IDXGIFactory2>(factory2.Get(), "the DXGI factory");
    ComPtr<IDXGISwapChain1> created;
    Check(streamline_factory->CreateSwapChainForHwnd(streamline_queue_.Get(), window_, &real, &fullscreen, nullptr, created.GetAddressOf()),
          "Creating the D3D12 swap chain");
    Check(created.As(&chain_), "Finding IDXGISwapChain4");

    // A width or height of 0 takes the window's.
    DXGI_SWAP_CHAIN_DESC1 actual = {};
    Check(chain_->GetDesc1(&actual), "Reading the swap chain's size");
    desc_.BufferDesc.Width = actual.Width;
    desc_.BufferDesc.Height = actual.Height;
    CreateBackBuffer();
  }

  SwapChain(const SwapChain&) = delete;
  SwapChain& operator=(const SwapChain&) = delete;

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
    if (object == nullptr) return E_POINTER;
    if (id == __uuidof(IUnknown) || id == __uuidof(IDXGIObject) || id == __uuidof(IDXGIDeviceSubObject) || id == __uuidof(IDXGISwapChain)
        || id == __uuidof(IDXGISwapChain1) || id == __uuidof(IDXGISwapChain2) || id == __uuidof(IDXGISwapChain3)
        || id == __uuidof(IDXGISwapChain4)) {
      AddRef();
      *object = static_cast<IDXGISwapChain4*>(this);
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = --references_;
    if (left == 0) delete this;
    return left;
  }

  // IDXGIObject, IDXGIDeviceSubObject
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT size, const void* data) override { return chain_->SetPrivateData(name, size, data); }
  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* data) override { return chain_->SetPrivateDataInterface(name, data); }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* size, void* data) override { return chain_->GetPrivateData(name, size, data); }
  HRESULT STDMETHODCALLTYPE GetParent(REFIID id, void** parent) override { return chain_->GetParent(id, parent); }
  HRESULT STDMETHODCALLTYPE GetDevice(REFIID id, void** device) override { return device11_->QueryInterface(id, device); }

  // IDXGISwapChain
  HRESULT STDMETHODCALLTYPE Present(UINT interval, UINT flags) override { return PresentFrame(interval, flags); }
  HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID id, void** surface) override {
    if (surface == nullptr) return DXGI_ERROR_INVALID_CALL;
    *surface = nullptr;
    // D3D11 shows the current back buffer as buffer 0.
    if (index != 0 || !back_buffer_) return DXGI_ERROR_INVALID_CALL;
    return back_buffer_->QueryInterface(id, surface);
  }
  HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput* target) override { return chain_->SetFullscreenState(fullscreen, target); }
  HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen, IDXGIOutput** target) override { return chain_->GetFullscreenState(fullscreen, target); }
  HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* result) override {
    if (result == nullptr) return DXGI_ERROR_INVALID_CALL;
    BOOL fullscreen = FALSE;
    if (const HRESULT status = chain_->GetFullscreenState(&fullscreen, nullptr); FAILED(status)) return status;
    *result = desc_;
    result->Windowed = !fullscreen;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) override {
    try {
      // Generation off while the buffers change (NVIDIA: Present can deadlock otherwise); the next present turns it on.
      if (attached) {
        const sl::DLSSGOptions off = {};
        Report(api.set_options(VIEWPORT, off), "Turning frame generation off for a resize");
        const std::unique_lock lock(state.mutex);
        state.applied = {};
      }
      // Back on two presents later: off and on in one frame race Present in Streamline (its "repeated
      // slDLSSGSetOptions" warning at every resize; the screen then kept showing old frames).
      presents_after_resize_ = 2;
      // D3D12 resizes only once nothing uses the old buffers; ReShade has released its own.
      WaitIdle();
      back_buffer_.Reset();
      shared_back_buffer_.Reset();
      // Streamline sizes its own buffers from these: zeros (the window's size, the current count and format) resolved.
      RECT client = {};
      GetClientRect(window_, &client);
      const UINT new_width = (width != 0 ? width : static_cast<UINT>(std::max(client.right - client.left, 1L)));
      const UINT new_height = (height != 0 ? height : static_cast<UINT>(std::max(client.bottom - client.top, 1L)));
      const UINT new_count = (count != 0 ? count : desc_.BufferCount);
      const DXGI_FORMAT new_format = (format != DXGI_FORMAT_UNKNOWN ? format : desc_.BufferDesc.Format);
      const HRESULT result = chain_->ResizeBuffers(std::max(new_count, 2u), new_width, new_height, Presentable(new_format), flags | tearing_flag_);
      if (SUCCEEDED(result)) {
        DXGI_SWAP_CHAIN_DESC1 actual = {};
        Check(chain_->GetDesc1(&actual), "Reading the swap chain's size");
        desc_.BufferDesc.Width = actual.Width;
        desc_.BufferDesc.Height = actual.Height;
        desc_.BufferCount = new_count;
        desc_.BufferDesc.Format = new_format;
        desc_.Flags = flags;
      }
      // The game gets a back buffer either way, at the old size when resizing failed.
      CreateBackBuffer();
      return result;
    } catch (const Failure& failure) {
      LogFailure(failure);
      return failure.result;
    }
  }
  HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* mode) override { return chain_->ResizeTarget(mode); }
  HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** output) override { return chain_->GetContainingOutput(output); }
  // Frame generation presents several times per game frame: the counts are the game's own presents.
  HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* statistics) override {
    if (statistics == nullptr) return DXGI_ERROR_INVALID_CALL;
    if (FAILED(chain_->GetFrameStatistics(statistics))) *statistics = {};
    statistics->PresentCount = game_presents_;
    statistics->SyncQPCTime.QuadPart = last_present_time_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* count) override {
    if (count == nullptr) return DXGI_ERROR_INVALID_CALL;
    *count = game_presents_;
    return S_OK;
  }

  // IDXGISwapChain1
  HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* result) override {
    if (result == nullptr) return DXGI_ERROR_INVALID_CALL;
    *result = {
        .Width = desc_.BufferDesc.Width,
        .Height = desc_.BufferDesc.Height,
        .Format = desc_.BufferDesc.Format,
        .SampleDesc = desc_.SampleDesc,
        .BufferUsage = desc_.BufferUsage,
        .BufferCount = desc_.BufferCount,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = desc_.SwapEffect,
        .Flags = desc_.Flags,
    };
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* result) override { return chain_->GetFullscreenDesc(result); }
  HRESULT STDMETHODCALLTYPE GetHwnd(HWND* result) override {
    if (result == nullptr) return DXGI_ERROR_INVALID_CALL;
    *result = window_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID id, void** result) override { return chain_->GetCoreWindow(id, result); }
  // Every present copies the whole frame: dirty rectangles and scrolling do not apply.
  HRESULT STDMETHODCALLTYPE Present1(UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS*) override { return PresentFrame(interval, flags); }
  BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return chain_->IsTemporaryMonoSupported(); }
  HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** output) override { return chain_->GetRestrictToOutput(output); }
  HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* color) override { return chain_->SetBackgroundColor(color); }
  HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* color) override { return chain_->GetBackgroundColor(color); }
  HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override { return chain_->SetRotation(rotation); }
  HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override { return chain_->GetRotation(rotation); }

  // IDXGISwapChain2
  HRESULT STDMETHODCALLTYPE SetSourceSize(UINT width, UINT height) override { return chain_->SetSourceSize(width, height); }
  HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* width, UINT* height) override { return chain_->GetSourceSize(width, height); }
  HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency) override { return chain_->SetMaximumFrameLatency(latency); }
  HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override { return chain_->GetMaximumFrameLatency(latency); }
  HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return chain_->GetFrameLatencyWaitableObject(); }
  HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* matrix) override { return chain_->SetMatrixTransform(matrix); }
  HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* matrix) override { return chain_->GetMatrixTransform(matrix); }

  // IDXGISwapChain3: the game sees one back buffer.
  UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return 0; }
  HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE space, UINT* support) override {
    return chain_->CheckColorSpaceSupport(space, support);
  }
  HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE space) override { return chain_->SetColorSpace1(space); }
  HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT*,
                                           IUnknown* const*) override {
    return ResizeBuffers(count, width, height, format, flags);
  }

  // IDXGISwapChain4
  HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata) override {
    return chain_->SetHDRMetaData(type, size, metadata);
  }

 private:
  ~SwapChain() {
    try {
      WaitIdle();
    } catch (const Failure& failure) {
      LogFailure(failure);
    }
    // DXGI refuses to release a swap chain in exclusive fullscreen.
    BOOL fullscreen = FALSE;
    if (SUCCEEDED(chain_->GetFullscreenState(&fullscreen, nullptr)) && fullscreen) {
      chain_->SetFullscreenState(FALSE, nullptr);
    }
    {
      const std::unique_lock lock(state.mutex);
      state.device11.Reset();
      state.device12.Reset();
      state.depth = {};
      state.motion = {};
      state.hudless = {};
      state.hudless_frame = UINT64_MAX;
      state.private_state.Reset();
      state.vertex_shader.Reset();
      state.encode_shader.Reset();
      state.copy_shader.Reset();
      state.sampler.Reset();
      state.scene.Reset();
      state.scene_srv.Reset();
      state.submitted = {};
    }
    // Streamline's proxies go before Streamline, and Streamline before the devices it was given.
    chain_.Reset();
    streamline_queue_.Reset();
    streamline_device_.Reset();
    Stop();
    presenting = false;
    SetStatus("The game released its swap chain.");
  }

  HRESULT PresentFrame(UINT interval, UINT flags) {
    // A test present only asks whether the window is visible: nothing was rendered for it.
    if ((flags & DXGI_PRESENT_TEST) != 0) return chain_->Present(interval, flags);

    // ReShade has drawn its overlay by now, after the addon's present event submitted the frame.
    Submitted submitted;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> motion;
    ComPtr<ID3D12Resource> hudless;
    DXGI_FORMAT hudless_format = DXGI_FORMAT_UNKNOWN;
    {
      const std::unique_lock lock(state.mutex);
      submitted = std::exchange(state.submitted, {});
      depth = state.depth.resource;
      motion = state.motion.resource;
      hudless = state.hudless.resource;
      hudless_format = state.back_buffer_format;
    }
    const bool tagged = attached && submitted.valid && depth && motion;

    // The frame's copy into Streamline's back buffer, with Streamline's copies of the inputs (they change with the next
    // frame), once D3D11 rendered it; D3D11's next frame waits for the copy, the present for both.
    try {
      const uint64_t rendered = ++fence_value_;
      Check(context11_->Signal(fence11_.Get(), rendered), "Signalling the rendered frame");
      context11_->Flush();
      const size_t slot = frames_++ % FRAMES_IN_FLIGHT;
      WaitFor(submitted_[slot]);
      Check(allocators_[slot]->Reset(), "Resetting a command allocator");
      Check(list_->Reset(allocators_[slot].Get(), nullptr), "Resetting the command list");
      // Streamline chooses the back buffer; frame generation keeps some of them.
      ComPtr<ID3D12Resource> target;
      Check(chain_->GetBuffer(chain_->GetCurrentBackBufferIndex(), IID_PPV_ARGS(target.GetAddressOf())), "Finding the swap chain's back buffer");
      std::array<D3D12_RESOURCE_BARRIER, 2> barriers = {{
          {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
           .Transition = {.pResource = shared_back_buffer_.Get(), .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          .StateBefore = D3D12_RESOURCE_STATE_COMMON, .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE}},
          {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
           .Transition = {.pResource = target.Get(), .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          .StateBefore = D3D12_RESOURCE_STATE_PRESENT, .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST}},
      }};
      list_->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      list_->CopyResource(target.Get(), shared_back_buffer_.Get());
      for (auto& barrier : barriers) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      list_->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      if (sl::FrameToken* token = (tagged ? Token(submitted.frame) : nullptr)) {
        sl::Resource depth_resource = {sl::ResourceType::eTex2d, depth.Get(), D3D12_RESOURCE_STATE_COMMON};
        sl::Resource motion_resource = {sl::ResourceType::eTex2d, motion.Get(), D3D12_RESOURCE_STATE_COMMON};
        sl::Resource hudless_resource = {sl::ResourceType::eTex2d, hudless.Get(), D3D12_RESOURCE_STATE_COMMON};
        const sl::Extent render = {.top = 0, .left = 0, .width = submitted.render_width, .height = submitted.render_height};
        const std::array<sl::ResourceTag, 3> tags = {
            sl::ResourceTag(&depth_resource, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &render),
            sl::ResourceTag(&motion_resource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &render),
            sl::ResourceTag(&hudless_resource, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow),
        };
        const uint32_t tag_count = ((submitted.hudless && hudless) ? 3u : 2u);
        Report(api.set_tags(*token, VIEWPORT, tags.data(), tag_count, list_.Get()), "Tagging the frame's inputs");
      }
      Check(list_->Close(), "Recording the copy");
      Check(copy_queue_->Wait(fence12_.Get(), rendered), "Waiting for the rendered frame");
      std::array<ID3D12CommandList*, 1> lists = {list_.Get()};
      copy_queue_->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
      const uint64_t copied = ++fence_value_;
      Check(copy_queue_->Signal(fence12_.Get(), copied), "Signalling the copied frame");
      submitted_[slot] = copied;
      Check(context11_->Wait(fence11_.Get(), copied), "Waiting for the copied frame");
      Check(present_queue_->Wait(fence12_.Get(), copied), "Waiting to present the copied frame");
    } catch (const Failure& failure) {
      LogFailure(failure);
      return failure.result;
    }

    if (attached) {
      if (const bool boost = reflex_boost.load(); boost != reflex_boost_ || frame_limit.load() != frame_limit_) {
        reflex_boost_ = boost;
        frame_limit_ = frame_limit.load();
        Report(api.set_reflex_options(ReflexOptions(reflex_boost_, frame_limit_)), "Setting the Reflex mode");
      }
      // What to generate: off on frames without DLSS (menus, loading), keeping its memory meanwhile.
      const uint32_t wanted_multiplier = multiplier.load();
      uint32_t most = 0;
      bool dynamic_supported = false;
      {
        const std::shared_lock lock(state.mutex);
        most = state.max_multiplier;
        dynamic_supported = state.dynamic_supported;
      }
      Mode wanted = {.on = (submitted.valid && wanted_multiplier != 1),
                     .dynamic = (wanted_multiplier == 0 && dynamic_supported),
                     .hudless_format = hudless_format};
      if (!wanted.dynamic) {
        const uint32_t frames = (wanted_multiplier == 0 ? std::max(most, 2u) : std::max(wanted_multiplier, 2u));
        wanted.generated_frames = (most != 0 ? std::min(frames, most) : frames) - 1;
      }
      if (presents_after_resize_ != 0) {
        --presents_after_resize_;
        wanted = {};
      }
      if (!wanted.on) wanted = {};
      bool changed = false;
      {
        const std::unique_lock lock(state.mutex);
        changed = wanted != state.applied;
        state.applied = wanted;
      }
      if (changed) {
        sl::DLSSGOptions options = {};
        if (!wanted.on) {
          options.mode = sl::DLSSGMode::eOff;
        } else if (wanted.dynamic) {
          options.mode = sl::DLSSGMode::eDynamic;
        } else {
          options.mode = sl::DLSSGMode::eOn;
        }
        options.numFramesToGenerate = wanted.generated_frames;
        options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
        options.hudLessBufferFormat = static_cast<uint32_t>(wanted.hudless_format);
        // Dynamic generation follows the display's refresh rate.
        options.dynamicTargetFrameRate = 0;
        Report(api.set_options(VIEWPORT, options), "Setting the frame generation mode");
      }

      // The overlay's line, every 30 presents.
      if (sl::DLSSGState generation = {}; ++presents_ % 30 == 0 && api.get_state(VIEWPORT, generation, nullptr) == sl::Result::eOk) {
        std::string reasons;
        const auto add = [&](sl::DLSSGStatus flag, const char* reason) {
          if (!(generation.status & flag)) return;
          reasons += (reasons.empty() ? "" : "; ");
          reasons += reason;
        };
        add(sl::DLSSGStatus::eFailResolutionTooLow, "the resolution is too low");
        add(sl::DLSSGStatus::eFailReflexNotDetectedAtRuntime, "Reflex is not detected");
        add(sl::DLSSGStatus::eFailHDRFormatNotSupported, "the HDR format is not supported");
        add(sl::DLSSGStatus::eFailCommonConstantsInvalid, "the camera constants are invalid");
        add(sl::DLSSGStatus::eFailGetCurrentBackBufferIndexNotCalled, "the back buffer index is not queried");
        std::string text;
        if (hudless_format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
          // NVIDIA's frame generation takes no 16-bit (scRGB) output.
          text = "Not generating: set the HDR addon's Output to HDR10 or Auto (frame generation does not take scRGB).";
        } else if (generation.status != sl::DLSSGStatus::eOk) {
          text = std::format("Not generating: {}.", (reasons.empty() ? std::string("unknown reason") : reasons));
        } else if (wanted_multiplier == 1) {
          text = "Off (Reflex only).";
        } else if (!wanted.on) {
          text = "Paused: DLSS or DLAA is not running on this frame.";
        } else {
          text = std::format("{}{} frames presented per rendered frame{}.", (wanted.dynamic ? "Dynamic: " : "Generating: "),
                             generation.numFramesActuallyPresented, (submitted.hudless ? "" : " (no HUD-less image: the HUD can warp)"));
        }
        bool changed_text = false;
        {
          const std::unique_lock lock(state.mutex);
          state.max_multiplier = generation.numFramesToGenerateMax + 1;
          state.dynamic_supported = generation.bIsDynamicMFGSupported == sl::Boolean::eTrue;
          changed_text = text != state.status;
          state.status = text;
        }
        // The log keeps each change (for reports without the overlay).
        if (changed_text) renodx::utils::log::i("[", game_name, " frame generation] ", text);
      }

      if (submitted.valid && submitted.frame != 0) {
        if (sl::FrameToken* token = Token(submitted.frame)) {
          const auto& camera = submitted.camera;
          const auto to_matrix = [](const std::array<float, 16>& values) {
            sl::float4x4 result;
            for (int row = 0; row < 4; ++row) {
              result[row] = sl::float4(values[row * 4], values[(row * 4) + 1], values[(row * 4) + 2], values[(row * 4) + 3]);
            }
            return result;
          };
          // A perspective projection: [0] 1 / tan(horizontal half angle), [5] 1 / tan(vertical).
          const auto& projection = camera.view_to_clip;
          sl::Constants constants = {};
          constants.cameraViewToClip = to_matrix(projection);
          constants.clipToCameraView = to_matrix(Inverse(projection));
          constants.clipToLensClip = to_matrix({1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f});
          constants.clipToPrevClip = to_matrix(camera.clip_to_previous_clip);
          constants.prevClipToClip = to_matrix(Inverse(camera.clip_to_previous_clip));
          constants.jitterOffset = sl::float2(submitted.jitter[0], submitted.jitter[1]);
          constants.mvecScale = sl::float2(submitted.motion_scale[0] / static_cast<float>(submitted.render_width),
                                           submitted.motion_scale[1] / static_cast<float>(submitted.render_height));
          constants.cameraPinholeOffset = sl::float2(0.f, 0.f);
          constants.cameraPos = sl::float3(camera.position[0], camera.position[1], camera.position[2]);
          constants.cameraRight = sl::float3(camera.right[0], camera.right[1], camera.right[2]);
          constants.cameraUp = sl::float3(camera.up[0], camera.up[1], camera.up[2]);
          constants.cameraFwd = sl::float3(camera.forward[0], camera.forward[1], camera.forward[2]);
          constants.cameraNear = camera.near_plane;
          constants.cameraFar = camera.far_plane;
          constants.cameraFOV = 2.f * std::atan(1.f / projection[5]);
          constants.cameraAspectRatio = projection[5] / projection[0];
          constants.depthInverted = (camera.depth_inverted ? sl::Boolean::eTrue : sl::Boolean::eFalse);
          constants.cameraMotionIncluded = sl::Boolean::eTrue;
          constants.motionVectors3D = sl::Boolean::eFalse;
          constants.reset = (submitted.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse);
          constants.motionVectorsJittered = sl::Boolean::eFalse;
          Report(api.set_constants(constants, *token, VIEWPORT), "Setting the camera constants");
        }
      }
      if (submitted.frame != 0) {
        Marker(sl::PCLMarker::eRenderSubmitEnd, submitted.frame);
        Marker(sl::PCLMarker::ePresentStart, submitted.frame);
      }
    }

    const HRESULT result = chain_->Present(interval, flags);
    LARGE_INTEGER presented = {};
    QueryPerformanceCounter(&presented);
    last_present_time_ = presented.QuadPart;
    ++game_presents_;

    if (attached && submitted.frame != 0) {
      Marker(sl::PCLMarker::ePresentEnd, submitted.frame);
      // Without a hook in the game loop, the next frame's Reflex sleep, simulation and submission start are marked
      // here (the render thread waits; the game thread a frame ahead waits for it).
      const uint32_t next = submitted.frame + 1;
      if (sl::FrameToken* token = Token(next); token != nullptr && reflex_sleep.load(std::memory_order_relaxed)) {
        Report(api.sleep(*token), "Reflex sleep");
      }
      Marker(sl::PCLMarker::eSimulationStart, next);
      Marker(sl::PCLMarker::eSimulationEnd, next);
      Marker(sl::PCLMarker::eRenderSubmitStart, next);
    }
    return result;
  }

  void CreateBackBuffer() {
    D3D11_TEXTURE2D_DESC texture = {
        .Width = desc_.BufferDesc.Width,
        .Height = desc_.BufferDesc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = desc_.BufferDesc.Format,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET,
        .MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE,
    };
    if ((desc_.BufferUsage & DXGI_USAGE_SHADER_INPUT) != 0) texture.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if ((desc_.BufferUsage & DXGI_USAGE_UNORDERED_ACCESS) != 0) texture.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    Check(device11_->CreateTexture2D(&texture, nullptr, back_buffer_.ReleaseAndGetAddressOf()), "Creating the shared back buffer");
    Check(d3d12_device::Open(device12_.Get(), back_buffer_.Get(), &shared_back_buffer_), "Opening the shared back buffer");
    // The hudless colour follows the back buffer's format and size.
    const std::unique_lock lock(state.mutex);
    state.device11 = device11_;
    state.device12 = device12_;
    state.back_buffer_format = desc_.BufferDesc.Format;
    state.width = desc_.BufferDesc.Width;
    state.height = desc_.BufferDesc.Height;
    state.hudless = {};
    state.hudless_frame = UINT64_MAX;
  }

  // Both devices and all queues finish everything given so far.
  void WaitIdle() {
    const uint64_t rendered = ++fence_value_;
    Check(context11_->Signal(fence11_.Get(), rendered), "Signalling D3D11's work");
    context11_->Flush();
    Check(copy_queue_->Wait(fence12_.Get(), rendered), "Waiting for D3D11's work");
    const uint64_t copied = ++fence_value_;
    Check(copy_queue_->Signal(fence12_.Get(), copied), "Signalling the copy queue's work");
    Check(present_queue_->Wait(fence12_.Get(), copied), "Waiting for the copy queue's work");
    const uint64_t done = ++fence_value_;
    Check(present_queue_->Signal(fence12_.Get(), done), "Signalling D3D12's work");
    WaitFor(done);
  }

  void WaitFor(uint64_t value) {
    if (fence12_->GetCompletedValue() >= value) return;
    Check(fence12_->SetEventOnCompletion(value, fence_event_.get()), "Waiting for D3D12");
    if (WaitForSingleObject(fence_event_.get(), FENCE_TIMEOUT) != WAIT_OBJECT_0) {
      throw Failure("D3D12 did not finish a frame within 10 seconds.", DXGI_ERROR_DEVICE_HUNG);
    }
  }

  // Reflex's options: low latency (with Boost), the frame rate limit (frames per second; 0: none).
  static sl::ReflexOptions ReflexOptions(bool boost, uint32_t limit) {
    sl::ReflexOptions options = {};
    options.mode = (boost ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency);
    options.frameLimitUs = (limit != 0 ? 1000000u / limit : 0u);
    return options;
  }

  std::atomic<ULONG> references_ = 1;
  HWND window_ = nullptr;
  UINT tearing_flag_ = 0;  // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING when the display allows it
  bool reflex_boost_ = false;  // as Reflex was last set
  uint32_t frame_limit_ = 0;
  uint32_t presents_after_resize_ = 0;  // frame generation stays off for these presents
  DXGI_SWAP_CHAIN_DESC desc_ = {};  // as the game sees it

  ComPtr<ID3D11Device5> device11_;
  ComPtr<ID3D11DeviceContext4> context11_;
  ComPtr<ID3D11Fence> fence11_;
  ComPtr<ID3D12Device> device12_wrapper_;  // ReShade's proxy (when it wraps), kept alive while the native one is used
  ComPtr<ID3D12Device> device12_;
  ComPtr<ID3D12Device> streamline_device_;
  ComPtr<ID3D12CommandQueue> streamline_queue_;
  ComPtr<ID3D12CommandQueue> present_queue_;
  ComPtr<ID3D12CommandQueue> copy_queue_;
  ComPtr<ID3D12Fence> fence12_;
  std::array<ComPtr<ID3D12CommandAllocator>, FRAMES_IN_FLIGHT> allocators_;
  std::array<uint64_t, FRAMES_IN_FLIGHT> submitted_ = {};
  ComPtr<ID3D12GraphicsCommandList> list_;
  std::unique_ptr<void, decltype(&CloseHandle)> fence_event_ = {nullptr, &CloseHandle};
  uint64_t fence_value_ = 0;
  uint64_t frames_ = 0;
  uint64_t presents_ = 0;
  std::atomic<UINT> game_presents_ = 0;
  std::atomic<LONGLONG> last_present_time_ = 0;

  ComPtr<IDXGISwapChain4> chain_;  // Streamline's proxy of the real swap chain
  ComPtr<ID3D11Texture2D> back_buffer_;
  ComPtr<ID3D12Resource> shared_back_buffer_;
};

// DXGI's own function behind an entry of its factories' function table. Some setups replace the entry in memory and
// call the function it held, so it is read from the entry in dxgi.dll's file and relocated to where dxgi.dll is loaded.
inline void* OriginalEntry(void* const* slot) {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(slot), &module)) {
    throw std::runtime_error("Finding the module that holds DXGI's function table failed.");
  }
  std::wstring path(32768, L'\0');
  path.resize(GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())));
  if (path.empty()) throw std::runtime_error("Finding dxgi.dll's file failed.");
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  const std::vector<char> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const auto read = [&](size_t offset, auto* value) {
    if (offset > image.size() || image.size() - offset < sizeof(*value)) throw std::runtime_error("dxgi.dll's file is shorter than its headers say.");
    std::memcpy(value, image.data() + offset, sizeof(*value));
  };
  IMAGE_DOS_HEADER dos = {};
  IMAGE_NT_HEADERS64 nt = {};
  read(0, &dos);
  read(static_cast<size_t>(dos.e_lfanew), &nt);
  const auto base = reinterpret_cast<uintptr_t>(module);
  const auto* loaded = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
  if (dos.e_magic != IMAGE_DOS_SIGNATURE || nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
      || nt.FileHeader.TimeDateStamp != loaded->FileHeader.TimeDateStamp) {
    throw std::runtime_error("dxgi.dll's file is not the one loaded.");
  }
  const uintptr_t rva = reinterpret_cast<uintptr_t>(slot) - base;
  const size_t sections = static_cast<size_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
  for (WORD index = 0; index < nt.FileHeader.NumberOfSections; ++index) {
    IMAGE_SECTION_HEADER section = {};
    read(sections + (index * sizeof(section)), &section);
    if (rva < section.VirtualAddress || rva + sizeof(uint64_t) > section.VirtualAddress + section.SizeOfRawData) continue;
    uint64_t stored = 0;
    read(section.PointerToRawData + (rva - section.VirtualAddress), &stored);
    const uintptr_t entry = static_cast<uintptr_t>(stored - nt.OptionalHeader.ImageBase) + base;
    if (entry < base || entry >= base + loaded->OptionalHeader.SizeOfImage) break;
    return reinterpret_cast<void*>(entry);
  }
  throw std::runtime_error("DXGI's CreateSwapChain is not in dxgi.dll's file.");
}

// Behind ReShade: ReShade wraps what this returns as the game's swap chain.
inline HRESULT STDMETHODCALLTYPE CreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                                 IDXGISwapChain** swap_chain) {
  // Only the game's D3D11 swap chain is replaced; any other while it lives passes on.
  ComPtr<ID3D11Device> game;
  if (presenting.load() || device == nullptr || desc == nullptr || swap_chain == nullptr
      || FAILED(device->QueryInterface(IID_PPV_ARGS(game.GetAddressOf())))) {
    return original_create_swap_chain(factory, device, desc, swap_chain);
  }
  try {
    // Streamline loads only plugins NVIDIA signed; its loader is checked the same way.
    const auto interposer = addon_directory / L"sl.interposer.dll";
    if (!std::filesystem::exists(interposer)) throw std::runtime_error("sl.interposer.dll is missing next to the addon.");
    if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) throw std::runtime_error("sl.interposer.dll is not signed by NVIDIA.");
    if (api.module == nullptr) api.module = LoadLibraryExW(interposer.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (api.module == nullptr) throw std::runtime_error("Loading sl.interposer.dll failed.");
    Resolve(&api.init, "slInit");
    Resolve(&api.shutdown, "slShutdown");
    Resolve(&api.is_feature_supported, "slIsFeatureSupported");
    Resolve(&api.set_device, "slSetD3DDevice");
    Resolve(&api.upgrade_interface, "slUpgradeInterface");
    Resolve(&api.get_native_interface, "slGetNativeInterface");
    Resolve(&api.get_feature_function, "slGetFeatureFunction");
    Resolve(&api.get_frame_token, "slGetNewFrameToken");
    Resolve(&api.set_tags, "slSetTagForFrame");
    Resolve(&api.set_constants, "slSetConstants");

    // Manual hooking: Streamline sees only the device, queue and swap chain the stand-in gives it. Its NGX project is
    // the one DLSS runs under (they must match).
    const std::wstring plugins = addon_directory.wstring();
    std::array<const wchar_t*, 1> paths = {plugins.c_str()};
    const std::array<sl::Feature, 3> features = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    sl::Preferences preferences = {};
    preferences.pathsToPlugins = paths.data();
    preferences.numPathsToPlugins = static_cast<uint32_t>(paths.size());
    preferences.pathToLogsAndData = paths[0];
    preferences.logMessageCallback = [](sl::LogType type, const char* message) {
      // Warnings and errors, up to a limit (some repeat every frame); sl.log next to the addon keeps all of them.
      static std::atomic<int> forwarded = 0;
      if (type == sl::LogType::eInfo || ++forwarded > 50) return;
      std::string text = message;
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
      renodx::utils::log::w("[", game_name, " frame generation] Streamline: ", text);
    };
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking
                        | sl::PreferenceFlags::eUseDXGIFactoryProxy | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = features.data();
    preferences.numFeaturesToLoad = static_cast<uint32_t>(features.size());
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "1.0";
    preferences.projectId = talesofarise::dlss::PROJECT_ID;
    Check(api.init(preferences, sl::kSDKVersion), "Starting Streamline");
    streamline_initialized = true;

    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapter_desc = {};
    if (FAILED(game.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf())) || FAILED(adapter->GetDesc(&adapter_desc))) {
      throw std::runtime_error("Reading the GPU's description failed.");
    }
    sl::AdapterInfo info = {};
    info.deviceLUID = reinterpret_cast<uint8_t*>(&adapter_desc.AdapterLuid);
    info.deviceLUIDSizeInBytes = sizeof(LUID);
    if (const sl::Result supported = api.is_feature_supported(sl::kFeatureDLSS_G, info); supported != sl::Result::eOk) {
      throw std::runtime_error(std::format("DLSS Frame Generation is not supported here ({}): it needs an RTX 40 or newer GPU, a recent driver "
                                           "and hardware-accelerated GPU scheduling.",
                                           sl::getResultAsStr(supported)));
    }
    *swap_chain = new SwapChain(factory, device, *desc);
  } catch (const std::exception& error) {
    Stop();
    SetStatus(std::format("Did not start: {}", error.what()));
    return original_create_swap_chain(factory, device, desc, swap_chain);
  }
  presenting = true;
  SetStatus("Presenting through D3D12 for frame generation.");
  return S_OK;
}

// The same for a game making its swap chain with CreateSwapChainForHwnd: the stand-in from the equivalent description.
inline HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window,
                                                       const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
                                                       IDXGIOutput* output, IDXGISwapChain1** swap_chain) {
  ComPtr<ID3D11Device> game;
  if (presenting.load() || device == nullptr || desc == nullptr || swap_chain == nullptr || output != nullptr
      || FAILED(device->QueryInterface(IID_PPV_ARGS(game.GetAddressOf())))) {
    return original_create_swap_chain_for_hwnd(factory, device, window, desc, fullscreen, output, swap_chain);
  }
  DXGI_SWAP_CHAIN_DESC legacy = {
      .BufferDesc = {.Width = desc->Width, .Height = desc->Height, .Format = desc->Format},
      .SampleDesc = desc->SampleDesc,
      .BufferUsage = desc->BufferUsage,
      .BufferCount = desc->BufferCount,
      .OutputWindow = window,
      .Windowed = TRUE,
      .SwapEffect = desc->SwapEffect,
      .Flags = desc->Flags,
  };
  if (fullscreen != nullptr) {
    legacy.BufferDesc.RefreshRate = fullscreen->RefreshRate;
    legacy.BufferDesc.ScanlineOrdering = fullscreen->ScanlineOrdering;
    legacy.BufferDesc.Scaling = fullscreen->Scaling;
    legacy.Windowed = fullscreen->Windowed;
  }
  ComPtr<IDXGISwapChain> created;
  if (const HRESULT result = CreateSwapChain(factory, device, &legacy, created.GetAddressOf()); FAILED(result)) return result;
  return created->QueryInterface(IID_PPV_ARGS(swap_chain));
}

// The private context state and shaders of the D3D11 passes (hudless colour, depth copy); state.mutex held.
inline bool CreatePasses() {
  if (state.private_state) return true;
  ID3D11Device5* device = state.device11.Get();
  const D3D_FEATURE_LEVEL feature_level = device->GetFeatureLevel();
  const UINT flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0 ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0);
  const D3D11_SAMPLER_DESC sampler_desc = {
      .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
      .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
      .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
      .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
      .ComparisonFunc = D3D11_COMPARISON_NEVER,
      .MaxLOD = D3D11_FLOAT32_MAX,
  };
  if (FAILED(device->CreateDeviceContextState(flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                              state.private_state.GetAddressOf()))
      || FAILED(device->CreateVertexShader(__0x7A1E0D40.data(), __0x7A1E0D40.size(), nullptr, state.vertex_shader.GetAddressOf()))
      || FAILED(device->CreatePixelShader(__0x7A1E0D41.data(), __0x7A1E0D41.size(), nullptr, state.encode_shader.GetAddressOf()))
      || FAILED(device->CreatePixelShader(__0x7A1E0D42.data(), __0x7A1E0D42.size(), nullptr, state.copy_shader.GetAddressOf()))
      || FAILED(device->CreateSamplerState(&sampler_desc, state.sampler.GetAddressOf()))) {
    state.private_state.Reset();
    return false;
  }
  return true;
}

}  // namespace internal

// At addon load, before the game makes its swap chain (once per process: the module stays loaded, as the hook does).
// Takes over the game's next D3D11 swap chain, with Streamline from `directory`; `name`: the game's, for the log.
inline void Install(const std::filesystem::path& directory, const std::string& name) {
  using internal::state;
  if (internal::installed.exchange(true)) return;
  internal::addon_directory = directory;
  internal::game_name = name;
  try {
    // The stand-in may outlive any load of the addon, and the hook stays: the module is never unloaded.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&internal::CreateSwapChain), &pinned)) {
      throw std::runtime_error("Keeping the addon loaded failed.");
    }
    // ReShade gives the game proxies of DXGI's factories, which call the real factories' shared function table.
    // Detouring DXGI's function behind that table puts the hook behind ReShade.
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    using CreateFactoryFunction = HRESULT(WINAPI*)(REFIID id, void** factory);
    const auto create_factory = reinterpret_cast<CreateFactoryFunction>((dxgi != nullptr) ? GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr);
    if (create_factory == nullptr) throw std::runtime_error("DXGI is not available.");
    ComPtr<IDXGIFactory> proxy;
    internal::Check(create_factory(IID_PPV_ARGS(proxy.GetAddressOf())), "Creating a DXGI factory");
    ComPtr<IDXGIFactory> factory;
    if (FAILED(proxy->QueryInterface(d3d12_device::UNWRAPPED_OBJECT, reinterpret_cast<void**>(factory.GetAddressOf())))) factory = proxy;
    void* const* table = *reinterpret_cast<void***>(factory.Get());
    internal::original_create_swap_chain =
        reinterpret_cast<internal::CreateSwapChainFunction>(internal::OriginalEntry(&table[internal::CREATE_SWAP_CHAIN_SLOT]));
    internal::original_create_swap_chain_for_hwnd = reinterpret_cast<internal::CreateSwapChainForHwndFunction>(
        internal::OriginalEntry(&table[internal::CREATE_SWAP_CHAIN_FOR_HWND_SLOT]));
    renodx::utils::detour::Install(std::array{
        renodx::utils::detour::Function(&internal::original_create_swap_chain, &internal::CreateSwapChain),
        renodx::utils::detour::Function(&internal::original_create_swap_chain_for_hwnd, &internal::CreateSwapChainForHwnd)});
    internal::SetStatus("Waiting for the game's swap chain.");
  } catch (const std::exception& error) {
    internal::SetStatus(std::format("Did not start: {}", error.what()));
  }
}

// The game presents through D3D12 with frame generation.
inline bool Presenting() {
  return internal::presenting.load(std::memory_order_relaxed);
}

// One line for the overlay: why frame generation did not start, or what it generates. Empty when not installed.
inline std::string Status() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.status;
}

// The frame before the HUD (frame generation's hudless colour), after the tonemapper's draw at the output size into
// `scene` (the HDR addon's constants still bound at b13): through the HDR addon's own swap chain shader, so it is
// encoded as the back buffer will be; without the HDR addon as it is.
inline void DrawHudless(ID3D11DeviceContext* context, ID3D11RenderTargetView* scene, uint64_t frame) {
  using internal::state;
  // The first few reasons the game's image was not taken (the log of a report).
  const auto skipped = [](const std::string& reason) {
    static std::atomic_uint32_t logged = 0;
    if (logged.fetch_add(1) < 4) renodx::utils::log::i("[", internal::game_name, " frame generation] HUD-less image skipped: ", reason);
  };
  if (!Presenting()) return;
  if (scene == nullptr) return skipped("no render target");
  ComPtr<ID3D11DeviceContext1> context1;
  ComPtr<ID3D11Resource> scene_resource;
  scene->GetResource(scene_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> scene_texture;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()))) || FAILED(scene_resource.As(&scene_texture))) {
    return skipped("not a 2D texture");
  }
  D3D11_TEXTURE2D_DESC scene_desc = {};
  scene_texture->GetDesc(&scene_desc);
  D3D11_RENDER_TARGET_VIEW_DESC scene_view = {};
  scene->GetDesc(&scene_view);
  const std::unique_lock lock(state.mutex);
  if (!state.device11 || scene_desc.Width != state.width || scene_desc.Height != state.height || scene_desc.SampleDesc.Count != 1
      || scene_view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D) {
    if (state.device11) {
      skipped(std::format("{}x{} ({} samples, view {}) for a {}x{} back buffer", scene_desc.Width, scene_desc.Height,
                          scene_desc.SampleDesc.Count, static_cast<int>(scene_view.ViewDimension), state.width, state.height));
    }
    return;
  }
  ID3D11Device5* device = state.device11.Get();
  if (!internal::CreatePasses()) return skipped("its shaders failed");
  if (!state.hudless.texture) {
    const D3D11_TEXTURE2D_DESC hudless_desc = {
        .Width = state.width,
        .Height = state.height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = state.back_buffer_format,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET,
        .MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE,
    };
    if (FAILED(device->CreateTexture2D(&hudless_desc, nullptr, state.hudless.texture.GetAddressOf()))
        || FAILED(device->CreateRenderTargetView(state.hudless.texture.Get(), nullptr, state.hudless.rtv.GetAddressOf()))
        || FAILED(d3d12_device::Open(state.device12.Get(), state.hudless.texture.Get(), &state.hudless.resource))) {
      state.hudless = {};
      return skipped("its texture failed");
    }
  }
  // The scene is copied: it may not be readable, and the draw's target stays bound in the game's state.
  if (D3D11_TEXTURE2D_DESC copy_desc = {}; state.scene) {
    state.scene->GetDesc(&copy_desc);
    if (copy_desc.Format != scene_desc.Format || copy_desc.Width != scene_desc.Width || copy_desc.Height != scene_desc.Height) {
      state.scene.Reset();
      state.scene_srv.Reset();
    }
  }
  if (!state.scene) {
    D3D11_TEXTURE2D_DESC copy_desc = scene_desc;
    copy_desc.MipLevels = 1;
    copy_desc.ArraySize = 1;
    copy_desc.Usage = D3D11_USAGE_DEFAULT;
    copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy_desc.CPUAccessFlags = 0;
    copy_desc.MiscFlags = 0;
    const D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
        .Format = scene_view.Format,
        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
    };
    if (scene_desc.MipLevels != 1 || scene_desc.ArraySize != 1
        || FAILED(device->CreateTexture2D(&copy_desc, nullptr, state.scene.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(state.scene.Get(), &srv_desc, state.scene_srv.GetAddressOf()))) {
      state.scene.Reset();
      state.scene_srv.Reset();
      return skipped("the scene copy failed");
    }
  }
  context->CopyResource(state.scene.Get(), scene_texture.Get());
  ComPtr<ID3D11Buffer> injection;
  UINT first_constant = 0;
  UINT constant_count = 0;
  context1->PSGetConstantBuffers1(internal::INJECTION_SLOT, 1, injection.GetAddressOf(), &first_constant, &constant_count);
  {
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(state.vertex_shader.Get(), nullptr, 0);
    ID3D11Buffer* injection_buffer = injection.Get();
    if (injection_buffer == nullptr) {
      context->PSSetShader(state.copy_shader.Get(), nullptr, 0);
    } else if (first_constant == 0) {
      // A whole-buffer binding reads back as offset 0 and 4096 constants, more than a small buffer holds.
      context->PSSetShader(state.encode_shader.Get(), nullptr, 0);
      context->PSSetConstantBuffers(internal::INJECTION_SLOT, 1, &injection_buffer);
    } else {
      context->PSSetShader(state.encode_shader.Get(), nullptr, 0);
      context1->PSSetConstantBuffers1(internal::INJECTION_SLOT, 1, &injection_buffer, &first_constant, &constant_count);
    }
    ID3D11ShaderResourceView* scene_srv = state.scene_srv.Get();
    ID3D11SamplerState* sampler = state.sampler.Get();
    ID3D11RenderTargetView* target = state.hudless.rtv.Get();
    context->PSSetShaderResources(0, 1, &scene_srv);
    context->PSSetSamplers(0, 1, &sampler);
    context->OMSetRenderTargets(1, &target, nullptr);
    const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(state.width), .Height = static_cast<float>(state.height), .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    context->Draw(3, 0);
  }
  if (state.hudless_frame == UINT64_MAX) renodx::utils::log::i("[", internal::game_name, " frame generation] HUD-less image taken.");
  state.hudless_frame = frame;
}

// The frame about to be presented, from the addon's present event (just before the stand-in's Present): DLSS's depth
// and motion are copied into textures D3D12 opens. Without them (DLSS did not run on it) generation pauses.
inline void Submit(ID3D11DeviceContext* context, const Inputs& inputs, uint64_t frame) {
  using internal::state;
  if (!Presenting()) return;
  const std::unique_lock lock(state.mutex);
  if (!state.device11) return;
  // Depth-stencil formats do not share with D3D12: their depth is drawn into an R32 float copy (the view to read it).
  const auto depth_view_format = [](DXGI_FORMAT format) {
    switch (format) {
      case DXGI_FORMAT_R24G8_TYPELESS:
      case DXGI_FORMAT_D24_UNORM_S8_UINT:    return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
      case DXGI_FORMAT_R32G8X24_TYPELESS:
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
      case DXGI_FORMAT_R32_TYPELESS:
      case DXGI_FORMAT_D32_FLOAT:            return DXGI_FORMAT_R32_FLOAT;
      case DXGI_FORMAT_R16_TYPELESS:
      case DXGI_FORMAT_D16_UNORM:            return DXGI_FORMAT_R16_UNORM;
      default:                               return DXGI_FORMAT_UNKNOWN;
    }
  };
  const auto copy = [&](ID3D11Texture2D* source, internal::SharedTexture* shared) {
    D3D11_TEXTURE2D_DESC desc = {};
    source->GetDesc(&desc);
    const DXGI_FORMAT view_format = depth_view_format(desc.Format);
    const DXGI_FORMAT shared_format = (view_format != DXGI_FORMAT_UNKNOWN ? DXGI_FORMAT_R32_FLOAT : desc.Format);
    if (D3D11_TEXTURE2D_DESC shared_desc = {}; shared->texture) {
      shared->texture->GetDesc(&shared_desc);
      if (shared_desc.Format != shared_format || shared_desc.Width != desc.Width || shared_desc.Height != desc.Height) *shared = {};
    }
    if (!shared->texture) {
      desc.Format = shared_format;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.SampleDesc = {.Count = 1, .Quality = 0};
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (view_format != DXGI_FORMAT_UNKNOWN ? D3D11_BIND_RENDER_TARGET : 0u);
      desc.CPUAccessFlags = 0;
      desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
      if (FAILED(state.device11->CreateTexture2D(&desc, nullptr, shared->texture.GetAddressOf()))
          || (view_format != DXGI_FORMAT_UNKNOWN && FAILED(state.device11->CreateRenderTargetView(shared->texture.Get(), nullptr, shared->rtv.GetAddressOf())))
          || FAILED(d3d12_device::Open(state.device12.Get(), shared->texture.Get(), &shared->resource))) {
        static std::atomic_bool logged = false;
        if (!logged.exchange(true)) {
          renodx::utils::log::w("[", internal::game_name, " frame generation] A ", desc.Width, "x", desc.Height, " input (format ",
                                static_cast<int>(desc.Format), ") cannot be shared with D3D12.");
        }
        *shared = {};
        return false;
      }
    }
    if (view_format == DXGI_FORMAT_UNKNOWN) {
      context->CopyResource(shared->texture.Get(), source);
      return true;
    }
    // The depth through a view of it, drawn by the copy pass.
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3D11ShaderResourceView> source_srv;
    const D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
        .Format = view_format,
        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
    };
    if (!internal::CreatePasses() || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))
        || FAILED(state.device11->CreateShaderResourceView(source, &srv_desc, source_srv.GetAddressOf()))) {
      return false;
    }
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(state.vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(state.copy_shader.Get(), nullptr, 0);
    ID3D11ShaderResourceView* view = source_srv.Get();
    ID3D11RenderTargetView* target = shared->rtv.Get();
    context->PSSetShaderResources(0, 1, &view);
    context->OMSetRenderTargets(1, &target, nullptr);
    const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(desc.Width), .Height = static_cast<float>(desc.Height), .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    context->Draw(3, 0);
    return true;
  };
  const bool valid = inputs.depth && inputs.motion && copy(inputs.depth.Get(), &state.depth) && copy(inputs.motion.Get(), &state.motion);
  state.submitted = {
      // Streamline's frame indices start at 1.
      .frame = static_cast<uint32_t>(frame + 1),
      .valid = valid,
      .hudless = valid && state.hudless_frame == frame,
      .render_width = inputs.render_width,
      .render_height = inputs.render_height,
      .jitter = inputs.jitter,
      .motion_scale = inputs.motion_scale,
      .reset = inputs.reset,
      .camera = inputs.camera,
  };
}

// The overlay's settings (`section` of ReShade.ini; `enabled`: frame generation at the next start).
inline void DrawSettings(const char* section, bool* enabled) {
  if (ImGui::Checkbox("Frame generation (Experimental, restart)", enabled)) {
    reshade::set_config_value(nullptr, section, "FrameGeneration", *enabled);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("NVIDIA's DLSS Frame Generation and Reflex with DLSS / DLAA (after restarting the game). Needs an\n"
                      "RTX 40 or newer GPU, NVIDIA's Streamline files next to the addon, and HDR10 or SDR output.");
  }
  if (*enabled) {
    constexpr std::array<std::pair<uint32_t, const char*>, 7> FRAME_GENERATION_MODES = {
        {{2u, "2x"}, {3u, "3x"}, {4u, "4x"}, {5u, "5x"}, {6u, "6x"}, {0u, "Dynamic (up to the display's refresh rate)"}, {1u, "Off (Reflex only)"}}};
    const uint32_t current = multiplier.load();
    const auto selected = std::ranges::find(FRAME_GENERATION_MODES, current, &std::pair<uint32_t, const char*>::first);
    if (ImGui::BeginCombo("Frame generation mode", selected != FRAME_GENERATION_MODES.end() ? selected->second : FRAME_GENERATION_MODES[0].second)) {
      for (const auto& [value, label] : FRAME_GENERATION_MODES) {
        if (ImGui::Selectable(label, value == current)) {
          multiplier.store(value);
          reshade::set_config_value(nullptr, section, "FrameGenerationMultiplier", value);
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Frames shown per rendered frame (limited to what the GPU supports).");
    }
    if (bool boost = reflex_boost.load(); ImGui::Checkbox("Reflex Boost", &boost)) {
      reflex_boost.store(boost);
      reshade::set_config_value(nullptr, section, "ReflexBoost", boost);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("NVIDIA Reflex is always on with frame generation (lower latency). Boost also keeps the GPU's\n"
                        "clocks up when the CPU limits the frame rate.");
    }
    if (int limit = static_cast<int>(frame_limit.load()); ImGui::SliderInt("Frame rate limit", &limit, 0, 240, limit == 0 ? "Off" : "%d fps")) {
      frame_limit.store(static_cast<uint32_t>(std::clamp(limit, 0, 240)));
      reshade::set_config_value(nullptr, section, "FrameGenerationFrameLimit", frame_limit.load());
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Caps the rendered frame rate with NVIDIA Reflex (frame generation multiplies it). A cap the\n"
                        "game always reaches keeps the frame rate steady: less VRR flicker on OLED displays.");
    }
    if (const std::string status = Status(); !status.empty()) {
      ImGui::TextWrapped("Frame generation: %s", status.c_str());
    } else {
      ImGui::TextWrapped("Frame generation starts after restarting the game.");
    }
  }
}

// The saved settings (`section` of ReShade.ini), at addon load.
inline void LoadSettings(const char* section, bool* enabled) {
  reshade::get_config_value(nullptr, section, "FrameGeneration", *enabled);
  if (uint32_t generated = 2; reshade::get_config_value(nullptr, section, "FrameGenerationMultiplier", generated)) {
    multiplier.store(generated <= 6 ? generated : 2u);
  }
  if (bool boost = false; reshade::get_config_value(nullptr, section, "ReflexBoost", boost)) {
    reflex_boost.store(boost);
  }
  if (bool sleep = true; reshade::get_config_value(nullptr, section, "FrameGenerationReflexSleep", sleep)) {
    reflex_sleep.store(sleep);
  }
  if (uint32_t limit = 0; reshade::get_config_value(nullptr, section, "FrameGenerationFrameLimit", limit)) {
    frame_limit.store(std::min(limit, 240u));
  }
}

}  // namespace talesofarise::frame_generation
