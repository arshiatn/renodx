/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_security.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <thread>

#include "../../talesofarise_dlss/ngx_backend.hpp"
#include "../dlaa_bridge.h"

// The DLAA helper's frame generation (bridge v5): NVIDIA's DLSS Frame Generation through Streamline (sl.interposer.dll,
// sl.common.dll, sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll and nvngx_dlssg.dll next to the helper) on a D3D12 device of
// its own. Attila is 32-bit and Streamline is 64-bit only, so the helper presents: into a child window it puts over the
// game's window (disabled: the game's window keeps the mouse and keyboard; Streamline counts a window of its parent
// process's foreground window as focused, tools\tw\fg_child), stretched to its client area, with the game's own swap
// chain still presenting underneath. Each request (a game present): the copy queue waits for the addon's fence value,
// copies the frame into the swap chain's back buffer and lets Streamline copy the DLAA depth and motion and the HUD-less
// image with the camera, then signals the next value; the present queue waits for that and presents (frame generation
// adds its frames there), then Reflex paces the next frame. As in talesofarise_dlss/frame_generation.hpp (Phantom FX's
// route by Bustanity, MIT). Multiplier 0 hides the window (the game's own image shows) and turns generation off.
namespace twattila::dlaa_helper {

using Microsoft::WRL::ComPtr;

class FrameGeneration12 {
 public:
  // Streamline from `folder` and a D3D12 device on the game's GPU; false: unavailable (Status() says why).
  bool Initialize(IDXGIAdapter1* adapter, const std::wstring& folder) {
    if (failed_) return false;
    if (device_) return true;
    const std::wstring interposer = folder + L"\\sl.interposer.dll";
    if (GetFileAttributesW(interposer.c_str()) == INVALID_FILE_ATTRIBUTES) return Fail("sl.interposer.dll is not next to the helper");
    if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) return Fail("sl.interposer.dll is not signed by NVIDIA");
    module_ = LoadLibraryExW(interposer.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module_ == nullptr) return Fail("sl.interposer.dll did not load");
    const bool resolved = Resolve(&init_, "slInit") && Resolve(&shutdown_, "slShutdown") && Resolve(&is_supported_, "slIsFeatureSupported")
                          && Resolve(&set_device_, "slSetD3DDevice") && Resolve(&upgrade_, "slUpgradeInterface")
                          && Resolve(&native_, "slGetNativeInterface") && Resolve(&feature_function_, "slGetFeatureFunction")
                          && Resolve(&frame_token_, "slGetNewFrameToken") && Resolve(&set_tags_, "slSetTagForFrame")
                          && Resolve(&set_constants_, "slSetConstants");
    if (!resolved) return Fail("sl.interposer.dll lacks Streamline's functions");

    std::array<const wchar_t*, 1> paths = {folder.c_str()};
    const std::array<sl::Feature, 3> features = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    sl::Preferences preferences = {};
    preferences.pathsToPlugins = paths.data();
    preferences.numPathsToPlugins = static_cast<uint32_t>(paths.size());
    preferences.pathToLogsAndData = paths[0];
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking
                        | sl::PreferenceFlags::eUseDXGIFactoryProxy | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = features.data();
    preferences.numFeaturesToLoad = static_cast<uint32_t>(features.size());
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "1.0";
    preferences.projectId = talesofarise::dlss::PROJECT_ID;
    if (const sl::Result result = init_(preferences, sl::kSDKVersion); result != sl::Result::eOk) {
      return Fail(std::format("Streamline did not start ({})", sl::getResultAsStr(result)));
    }
    initialized_ = true;
    DXGI_ADAPTER_DESC1 adapter_desc = {};
    adapter->GetDesc1(&adapter_desc);
    sl::AdapterInfo info = {};
    info.deviceLUID = reinterpret_cast<uint8_t*>(&adapter_desc.AdapterLuid);
    info.deviceLUIDSizeInBytes = sizeof(LUID);
    if (const sl::Result supported = is_supported_(sl::kFeatureDLSS_G, info); supported != sl::Result::eOk) {
      return Fail(std::format("DLSS Frame Generation is not supported here ({}): an RTX 40 or newer GPU, a recent driver and "
                              "hardware-accelerated GPU scheduling",
                              sl::getResultAsStr(supported)));
    }
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf())))
        || set_device_(device_.Get()) != sl::Result::eOk) {
      return Fail("no D3D12 device for Streamline on the game's GPU");
    }
    if (!Resolve(sl::kFeatureDLSS_G, &set_options_, "slDLSSGSetOptions") || !Resolve(sl::kFeatureDLSS_G, &get_state_, "slDLSSGGetState")
        || !Resolve(sl::kFeatureReflex, &set_reflex_, "slReflexSetOptions") || !Resolve(sl::kFeatureReflex, &sleep_, "slReflexSleep")
        || !Resolve(sl::kFeaturePCL, &set_marker_, "slPCLSetMarker")) {
      return Fail("Streamline lacks frame generation, Reflex or PC Latency");
    }
    sl::ReflexOptions reflex = {};
    reflex.mode = sl::ReflexMode::eLowLatency;
    set_reflex_(reflex);
    // Streamline's proxy of the device and its queue make the swap chain; the copies run on a native queue of their own.
    void* upgraded = device_.Get();
    if (upgrade_(&upgraded) != sl::Result::eOk) return Fail("Streamline did not take the D3D12 device");
    if (upgraded == device_.Get()) device_->AddRef();
    streamline_device_.Attach(static_cast<ID3D12Device*>(upgraded));
    void* native_queue = nullptr;
    if (FAILED(streamline_device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(streamline_queue_.GetAddressOf())))
        || native_(streamline_queue_.Get(), &native_queue) != sl::Result::eOk || native_queue == nullptr) {
      return Fail("no D3D12 present queue");
    }
    present_queue_.Attach(static_cast<ID3D12CommandQueue*>(native_queue));
    if (FAILED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(copy_queue_.GetAddressOf())))
        || FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(own_fence_.GetAddressOf())))) {
      return Fail("no D3D12 copy queue");
    }
    for (Slot& slot : slots_) {
      if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                               IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close())) {
        return Fail("no D3D12 command lists");
      }
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    status_ = "Frame generation ready";
    return true;
  }

  // The addon's frame, HUD-less image, depth and motion (bridge v5; handles still open) and fence; false: not all could
  // be opened.
  bool Open(const dlaa_bridge::Shared& shared) {
    Close();
    if (!device_) return false;
    const auto open = [&](uint64_t handle, REFIID id, void** object) {
      return SUCCEEDED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(handle), id, object));
    };
    if (!open(shared.fg_frame, IID_PPV_ARGS(frame_.GetAddressOf())) || !open(shared.fg_hudless, IID_PPV_ARGS(hudless_.GetAddressOf()))
        || !open(shared.depth, IID_PPV_ARGS(depth_.GetAddressOf())) || !open(shared.motion, IID_PPV_ARGS(motion_.GetAddressOf()))
        || !open(shared.fence, IID_PPV_ARGS(fence_.GetAddressOf()))) {
      Close();
      return false;
    }
    const D3D12_RESOURCE_DESC frame_desc = frame_->GetDesc();
    width_ = static_cast<UINT>(frame_desc.Width);
    height_ = frame_desc.Height;
    format_ = frame_desc.Format;
    return true;
  }

  // The swap chain and window kept (made again at the next frame of another size or format).
  void Close() {
    WaitIdle();
    frame_.Reset();
    hudless_.Reset();
    depth_.Reset();
    motion_.Reset();
    fence_.Reset();
  }

  // One present (the request's value: the frame is written; the next is signalled once copied). False: not presented
  // (the game's own image shows).
  bool Present(dlaa_bridge::Shared* shared) {
    const uint64_t value = shared->value;
    if (!fence_ || !frame_ || shared->fg_window == 0) {
      SignalOnly(value);
      return false;
    }
    const auto game_window = reinterpret_cast<HWND>(static_cast<uintptr_t>(shared->fg_window));
    if (shared->fg_multiplier == 0 || !IsWindow(game_window)) {
      // Off: generation off, the window hidden (the game's image shows); its swap chain kept for later.
      SetGeneration(0, false);
      if (window_ != nullptr) ShowWindowAsync(window_, SW_HIDE);
      SignalOnly(value);
      status_ = "Frame generation off";
      return false;
    }
    if (!PrepareWindow(game_window)) {
      SignalOnly(value);
      return false;
    }
    if (shared->fg_reflex_boost != reflex_boost_ || shared->fg_frame_limit != frame_limit_) {
      reflex_boost_ = shared->fg_reflex_boost;
      frame_limit_ = shared->fg_frame_limit;
      sl::ReflexOptions reflex = {};
      reflex.mode = (reflex_boost_ != 0 ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency);
      reflex.frameLimitUs = (frame_limit_ != 0 ? 1000000u / std::min(frame_limit_, 1000u) : 0u);
      set_reflex_(reflex);
    }
    const uint32_t frame = shared->fg_frame_index;
    sl::FrameToken* token = nullptr;
    uint32_t frame_index = frame;
    frame_token_(token, &frame_index);

    // The copy into the swap chain's back buffer and Streamline's copies of the inputs, after the addon's value.
    Slot& slot = slots_[next_slot_];
    next_slot_ = (next_slot_ + 1) % slots_.size();
    WaitFor(slot.done);
    slot.allocator->Reset();
    slot.list->Reset(slot.allocator.Get(), nullptr);
    ComPtr<ID3D12Resource> target;
    if (FAILED(chain_->GetBuffer(chain_->GetCurrentBackBufferIndex(), IID_PPV_ARGS(target.GetAddressOf())))) {
      slot.list->Close();
      SignalOnly(value);
      return Fail("the swap chain has no back buffer");
    }
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers = {{
        {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
         .Transition = {.pResource = frame_.Get(), .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        .StateBefore = D3D12_RESOURCE_STATE_COMMON, .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE}},
        {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
         .Transition = {.pResource = target.Get(), .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        .StateBefore = D3D12_RESOURCE_STATE_PRESENT, .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST}},
    }};
    slot.list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    slot.list->CopyResource(target.Get(), frame_.Get());
    for (auto& barrier : barriers) {
      std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    slot.list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    const bool inputs = shared->fg_inputs != 0 && token != nullptr && shared->width != 0 && shared->height != 0;
    if (inputs) {
      sl::Resource depth = {sl::ResourceType::eTex2d, depth_.Get(), D3D12_RESOURCE_STATE_COMMON};
      sl::Resource motion = {sl::ResourceType::eTex2d, motion_.Get(), D3D12_RESOURCE_STATE_COMMON};
      sl::Resource hudless = {sl::ResourceType::eTex2d, hudless_.Get(), D3D12_RESOURCE_STATE_COMMON};
      const sl::Extent render = {.top = 0, .left = 0, .width = shared->width, .height = shared->height};
      const std::array<sl::ResourceTag, 3> tags = {
          sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &render),
          sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &render),
          sl::ResourceTag(&hudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow),
      };
      const uint32_t tag_count = (shared->fg_hudless_valid != 0 ? 3u : 2u);
      set_tags_(*token, VIEWPORT, tags.data(), tag_count, slot.list.Get());
    }
    slot.list->Close();
    copy_queue_->Wait(fence_.Get(), value);
    ID3D12CommandList* lists[] = {slot.list.Get()};
    copy_queue_->ExecuteCommandLists(1, lists);
    copy_queue_->Signal(fence_.Get(), value + 1);
    slot.done = ++own_value_;
    copy_queue_->Signal(own_fence_.Get(), slot.done);
    present_queue_->Wait(fence_.Get(), value + 1);

    // What to generate: off on frames without the inputs (menus, loading), keeping its memory meanwhile.
    const uint32_t frames = std::clamp(shared->fg_multiplier, 2u, (max_frames_ != 0 ? max_frames_ : 4u));
    SetGeneration(inputs ? frames - 1 : 0, shared->fg_hudless_valid != 0);
    if (inputs) SetConstants(*shared, *token);
    Marker(sl::PCLMarker::eRenderSubmitEnd, token);
    Marker(sl::PCLMarker::ePresentStart, token);
    const HRESULT presented = chain_->Present(0, tearing_ != 0 ? DXGI_PRESENT_ALLOW_TEARING : 0);
    Marker(sl::PCLMarker::ePresentEnd, token);
    if (!IsWindowVisible(window_)) ShowWindowAsync(window_, SW_SHOWNA);
    // The next frame's Reflex sleep, simulation and submission start (the game waits for this answer).
    uint32_t next_index = frame + 1;
    sl::FrameToken* next = nullptr;
    if (frame_token_(next, &next_index) == sl::Result::eOk && next != nullptr) {
      sleep_(*next);
      Marker(sl::PCLMarker::eSimulationStart, next);
      Marker(sl::PCLMarker::eSimulationEnd, next);
      Marker(sl::PCLMarker::eRenderSubmitStart, next);
    }
    UpdateStatus(inputs, shared->fg_hudless_valid != 0);
    return SUCCEEDED(presented);
  }

  // The window and swap chain gone, Streamline stopped (the helper quits).
  void Shutdown() {
    WaitIdle();
    chain_.Reset();
    if (window_ != nullptr) PostMessageW(window_, WM_CLOSE, 0, 0);
    if (window_thread_.joinable()) window_thread_.join();
    streamline_queue_.Reset();
    streamline_device_.Reset();
    if (initialized_) shutdown_();
    initialized_ = false;
  }

  const std::string& Status() const { return status_; }

 private:
  struct Slot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    uint64_t done = 0;
  };

  inline static const sl::ViewportHandle VIEWPORT = {0u};
  static constexpr DWORD FENCE_TIMEOUT = 10'000;

  bool Fail(const std::string& status) {
    failed_ = true;
    status_ = "Frame generation: " + status;
    return false;
  }

  template <typename Function>
  bool Resolve(Function** function, const char* name) {
    *function = reinterpret_cast<Function*>(GetProcAddress(module_, name));
    return *function != nullptr;
  }

  template <typename Function>
  bool Resolve(sl::Feature feature, Function** function, const char* name) {
    void* address = nullptr;
    if (feature_function_(feature, name, address) != sl::Result::eOk || address == nullptr) return false;
    *function = reinterpret_cast<Function*>(address);
    return true;
  }

  void Marker(sl::PCLMarker marker, sl::FrameToken* token) {
    if (token != nullptr) set_marker_(marker, *token);
  }

  // The copy queue's fence `value` signalled (the game never waits for nothing).
  void SignalOnly(uint64_t value) {
    if (!fence_ || !copy_queue_) return;
    copy_queue_->Wait(fence_.Get(), value);
    copy_queue_->Signal(fence_.Get(), value + 1);
  }

  void WaitFor(uint64_t value) {
    if (!own_fence_ || own_fence_->GetCompletedValue() >= value) return;
    own_fence_->SetEventOnCompletion(value, event_);
    WaitForSingleObject(event_, FENCE_TIMEOUT);
  }

  void WaitIdle() {
    if (!present_queue_ || !own_fence_) return;
    const uint64_t copied = ++own_value_;
    copy_queue_->Signal(own_fence_.Get(), copied);
    present_queue_->Wait(own_fence_.Get(), copied);
    const uint64_t presented = ++own_value_;
    present_queue_->Signal(own_fence_.Get(), presented);
    WaitFor(presented);
  }

  // Frame generation's mode: `generated` frames per rendered frame (0: off), with the HUD-less image or not.
  void SetGeneration(uint32_t generated, bool hudless) {
    if (!set_options_ || (generated == applied_generated_ && hudless == applied_hudless_)) return;
    applied_generated_ = generated;
    applied_hudless_ = hudless;
    sl::DLSSGOptions options = {};
    options.mode = (generated != 0 ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff);
    options.numFramesToGenerate = std::max(generated, 1u);
    options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
    options.hudLessBufferFormat = (hudless ? static_cast<uint32_t>(format_) : 0u);
    set_options_(VIEWPORT, options);
  }

  // The window over the game's client area (made with its swap chain at the frame's size and format; resized with the
  // game's window). False: no window or swap chain.
  bool PrepareWindow(HWND game_window) {
    if (window_ != nullptr && !IsWindow(window_)) {
      window_ = nullptr;
      chain_.Reset();
    }
    if (window_ == nullptr || parent_ != game_window) {
      if (window_ != nullptr) {
        chain_.Reset();
        PostMessageW(window_, WM_CLOSE, 0, 0);
        if (window_thread_.joinable()) window_thread_.join();
        window_ = nullptr;
      }
      parent_ = game_window;
      std::atomic<HWND> made = nullptr;
      std::atomic_bool ready = false;
      // Its own thread keeps its messages flowing: the game's window thread sends to its child (and their input
      // queues are attached), so the helper's waits must never hold them up.
      window_thread_ = std::thread([game_window, &made, &ready] {
        const WNDCLASSW window_class = {.lpfnWndProc = WindowProcedure, .hInstance = GetModuleHandleW(nullptr), .lpszClassName = L"RenoDX_TW_FG"};
        RegisterClassW(&window_class);
        RECT client = {};
        GetClientRect(game_window, &client);
        HWND window = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"RenoDX_TW_FG", L"RenoDX frame generation", WS_CHILD | WS_DISABLED, 0, 0,
                                      std::max(client.right, 1L), std::max(client.bottom, 1L), game_window, nullptr,
                                      window_class.hInstance, nullptr);
        made.store(window);
        ready.store(true);
        if (window == nullptr) return;
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
          DispatchMessageW(&message);
        }
      });
      while (!ready.load()) {
        Sleep(1);
      }
      window_ = made.load();
      if (window_ == nullptr) {
        window_thread_.join();
        return Fail("its window over the game's could not be made");
      }
    }
    // The client area's size: the window follows it (the swap chain stretches the frame to it).
    RECT client = {};
    GetClientRect(parent_, &client);
    const int client_width = std::max(static_cast<int>(client.right), 1);
    const int client_height = std::max(static_cast<int>(client.bottom), 1);
    if (client_width != window_width_ || client_height != window_height_) {
      window_width_ = client_width;
      window_height_ = client_height;
      SetWindowPos(window_, HWND_TOP, 0, 0, client_width, client_height, SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }
    if (chain_ && chain_width_ == width_ && chain_height_ == height_ && chain_format_ == format_) return true;
    if (chain_) {
      // Generation off while the buffers change (NVIDIA: Present can deadlock otherwise).
      SetGeneration(0, false);
      WaitIdle();
      const HRESULT resized = chain_->ResizeBuffers(0, width_, height_, format_, tearing_);
      if (SUCCEEDED(resized)) {
        chain_width_ = width_;
        chain_height_ = height_;
        chain_format_ = format_;
        return true;
      }
      chain_.Reset();
    }
    ComPtr<IDXGIFactory5> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf())))) return Fail("no DXGI factory");
    BOOL tearing = FALSE;
    if (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) {
      tearing_ = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    void* upgraded = factory.Get();
    if (upgrade_(&upgraded) != sl::Result::eOk) return Fail("Streamline did not take the DXGI factory");
    if (upgraded == factory.Get()) factory->AddRef();
    ComPtr<IDXGIFactory2> streamline_factory;
    streamline_factory.Attach(static_cast<IDXGIFactory2*>(upgraded));
    const DXGI_SWAP_CHAIN_DESC1 desc = {
        .Width = width_,
        .Height = height_,
        .Format = format_,
        .SampleDesc = {.Count = 1},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 3,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = tearing_,
    };
    ComPtr<IDXGISwapChain1> created;
    if (FAILED(streamline_factory->CreateSwapChainForHwnd(streamline_queue_.Get(), window_, &desc, nullptr, nullptr, created.GetAddressOf()))
        || FAILED(created.As(&chain_))) {
      return Fail("its swap chain could not be made");
    }
    // HDR10 (R10G10B10A2) and scRGB (RGBA16F) frames as the game's swap chain shows them.
    if (format_ == DXGI_FORMAT_R10G10B10A2_UNORM) chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
    if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT) chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    chain_width_ = width_;
    chain_height_ = height_;
    chain_format_ = format_;
    return true;
  }

  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w_parameter, LPARAM l_parameter) {
    switch (message) {
      // The game's window under it takes the mouse.
      case WM_NCHITTEST:
        return HTTRANSPARENT;
      case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
      case WM_CLOSE:
        DestroyWindow(window);
        return 0;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcW(window, message, w_parameter, l_parameter);
    }
  }

  // The camera as Streamline takes it (row vectors, without the jitter; standard depth, 0 near).
  void SetConstants(const dlaa_bridge::Shared& shared, sl::FrameToken& token) {
    const auto to_matrix = [](const float* values) {
      sl::float4x4 result;
      for (int row = 0; row < 4; ++row) {
        result[row] = sl::float4(values[row * 4], values[(row * 4) + 1], values[(row * 4) + 2], values[(row * 4) + 3]);
      }
      return result;
    };
    // Gauss-Jordan with partial pivoting, in doubles.
    const auto inverse = [](const float* matrix) {
      std::array<std::array<double, 8>, 4> rows = {};
      for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) rows[row][column] = matrix[(row * 4) + column];
        rows[row][4 + row] = 1.;
      }
      for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
          if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) pivot = row;
        }
        std::swap(rows[pivot], rows[column]);
        const double scale = (rows[column][column] != 0. ? 1. / rows[column][column] : 0.);
        for (double& value : rows[column]) value *= scale;
        for (int row = 0; row < 4; ++row) {
          if (row == column) continue;
          const double factor = rows[row][column];
          for (int index = 0; index < 8; ++index) rows[row][index] -= factor * rows[column][index];
        }
      }
      std::array<float, 16> result = {};
      for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) result[(row * 4) + column] = static_cast<float>(rows[row][4 + column]);
      }
      return result;
    };
    const float* projection = shared.fg_view_to_clip;
    constexpr std::array<float, 16> IDENTITY = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    sl::Constants constants = {};
    constants.cameraViewToClip = to_matrix(projection);
    constants.clipToCameraView = to_matrix(inverse(projection).data());
    constants.clipToLensClip = to_matrix(IDENTITY.data());
    // Clip to last frame's clip: back to the world with this frame's camera, through last frame's.
    const auto clip_to_world = inverse(shared.fg_view_projection);
    std::array<float, 16> clip_to_previous_clip = {};
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        for (int k = 0; k < 4; ++k) {
          clip_to_previous_clip[(row * 4) + column] += clip_to_world[(row * 4) + k] * shared.fg_previous_view_projection[(k * 4) + column];
        }
      }
    }
    constants.clipToPrevClip = to_matrix(clip_to_previous_clip.data());
    constants.prevClipToClip = to_matrix(inverse(clip_to_previous_clip.data()).data());
    constants.jitterOffset = sl::float2(shared.jitter_x, shared.jitter_y);
    constants.mvecScale = sl::float2(1.f / static_cast<float>(shared.width), 1.f / static_cast<float>(shared.height));
    constants.cameraPinholeOffset = sl::float2(0.f, 0.f);
    const float* camera = shared.fg_camera;
    constants.cameraPos = sl::float3(camera[0], camera[1], camera[2]);
    constants.cameraRight = sl::float3(camera[3], camera[4], camera[5]);
    constants.cameraUp = sl::float3(camera[6], camera[7], camera[8]);
    constants.cameraFwd = sl::float3(camera[9], camera[10], camera[11]);
    constants.cameraNear = shared.fg_near;
    constants.cameraFar = shared.fg_far;
    constants.cameraFOV = 2.f * std::atan(1.f / projection[5]);
    constants.cameraAspectRatio = projection[5] / projection[0];
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = (shared.reset != 0 ? sl::Boolean::eTrue : sl::Boolean::eFalse);
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    set_constants_(constants, token, VIEWPORT);
  }

  // The status line every 30 presents.
  void UpdateStatus(bool inputs, bool hudless) {
    sl::DLSSGState state = {};
    if (++presents_ % 30 != 0 || get_state_(VIEWPORT, state, nullptr) != sl::Result::eOk) return;
    max_frames_ = state.numFramesToGenerateMax + 1;
    if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT) {
      status_ = "Frame generation: not generating (it takes no scRGB: set Output to HDR10 or SDR)";
    } else if (state.status != sl::DLSSGStatus::eOk) {
      status_ = std::format("Frame generation: not generating (status {:#x})", static_cast<uint32_t>(state.status));
    } else if (!inputs) {
      status_ = "Frame generation: paused (DLAA is not running on this frame)";
    } else {
      status_ = std::format("Frame generation: {} frames presented per rendered frame{}", state.numFramesActuallyPresented,
                            (hudless ? "" : " (no HUD-less image: the UI can warp)"));
    }
  }

  HMODULE module_ = nullptr;
  PFun_slInit* init_ = nullptr;
  PFun_slShutdown* shutdown_ = nullptr;
  PFun_slIsFeatureSupported* is_supported_ = nullptr;
  PFun_slSetD3DDevice* set_device_ = nullptr;
  PFun_slUpgradeInterface* upgrade_ = nullptr;
  PFun_slGetNativeInterface* native_ = nullptr;
  PFun_slGetFeatureFunction* feature_function_ = nullptr;
  PFun_slGetNewFrameToken* frame_token_ = nullptr;
  PFun_slSetTagForFrame* set_tags_ = nullptr;
  PFun_slSetConstants* set_constants_ = nullptr;
  PFun_slDLSSGSetOptions* set_options_ = nullptr;
  PFun_slDLSSGGetState* get_state_ = nullptr;
  PFun_slReflexSetOptions* set_reflex_ = nullptr;
  PFun_slReflexSleep* sleep_ = nullptr;
  PFun_slPCLSetMarker* set_marker_ = nullptr;
  bool initialized_ = false;
  bool failed_ = false;
  std::string status_ = "Frame generation not started";

  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12Device> streamline_device_;
  ComPtr<ID3D12CommandQueue> streamline_queue_;
  ComPtr<ID3D12CommandQueue> present_queue_;
  ComPtr<ID3D12CommandQueue> copy_queue_;
  ComPtr<ID3D12Fence> own_fence_;
  uint64_t own_value_ = 0;
  std::array<Slot, 3> slots_;
  size_t next_slot_ = 0;
  HANDLE event_ = nullptr;

  // The addon's objects.
  ComPtr<ID3D12Resource> frame_;
  ComPtr<ID3D12Resource> hudless_;
  ComPtr<ID3D12Resource> depth_;
  ComPtr<ID3D12Resource> motion_;
  ComPtr<ID3D12Fence> fence_;
  UINT width_ = 0;
  UINT height_ = 0;
  DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;

  // The window over the game's, its thread and swap chain.
  HWND parent_ = nullptr;
  HWND window_ = nullptr;
  std::thread window_thread_;
  int window_width_ = 0;
  int window_height_ = 0;
  ComPtr<IDXGISwapChain4> chain_;
  UINT chain_width_ = 0;
  UINT chain_height_ = 0;
  DXGI_FORMAT chain_format_ = DXGI_FORMAT_UNKNOWN;
  UINT tearing_ = 0;
  uint32_t reflex_boost_ = 0;
  uint32_t frame_limit_ = 0;  // Reflex's frame rate limit as last set (rendered frames per second; 0: none)
  uint32_t applied_generated_ = UINT32_MAX;
  bool applied_hudless_ = false;
  uint32_t max_frames_ = 0;
  uint64_t presents_ = 0;
};

}  // namespace twattila::dlaa_helper
