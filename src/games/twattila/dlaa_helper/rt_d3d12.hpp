/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <numeric>
#include <source_location>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../dlaa_bridge.h"
#include "./gpu_completion.hpp"
// NVIDIA's NVAPI (MIT; build.ps1 adds it when build\downloads\nvapi holds it): RTX Mega Geometry's cluster operations.
#if __has_include(<nvapi.h>)
#include <nvapi.h>
#define RENODX_NVAPI_CLUSTERS 1
#endif
// NVIDIA's Opacity Micro-Map SDK (NVIDIA RTX SDKs License; build.ps1 adds its header when build\downloads\omm holds its
// Windows release): the leaves' opacity micromaps baked on the CPU (its omm-lib.dll next to the helper, loaded at run
// time; without it, none).
#if __has_include(<omm.h>)
#include <omm.h>
#define RENODX_OMM_SDK 1
#endif
#include "./nrd_d3d12.hpp"
#if defined(RENODX_NRD)
#include "rt_nrd_finish_AMBIENT.h"  // dxc -Fh (build.ps1)
#include "rt_nrd_finish_BOUNCE.h"   // dxc -Fh (build.ps1)
#include "rt_nrd_finish_REFLECT.h"  // dxc -Fh (build.ps1)
#include "rt_nrd_finish_SUN.h"      // dxc -Fh (build.ps1)
#include "rt_nrd_front_AMBIENT.h"   // dxc -Fh (build.ps1)
#include "rt_nrd_front_BOUNCE.h"    // dxc -Fh (build.ps1)
#include "rt_nrd_front_REFLECT.h"   // dxc -Fh (build.ps1)
#include "rt_nrd_front_SUN.h"       // dxc -Fh (build.ps1)
#endif
#include "rt_bounce.h"            // dxc -Fh (build.ps1)
#include "rt_bounce_omm.h"        // dxc -Fh (build.ps1)
#include "rt_cache_fill.h"        // dxc -Fh (build.ps1)
#include "rt_cache_resolve.h"     // dxc -Fh (build.ps1)
#include "rt_compact.h"           // dxc -Fh (build.ps1)
#include "rt_fog.h"               // dxc -Fh (build.ps1)
#include "rt_lights.h"            // dxc -Fh (build.ps1)
#include "rt_lights_omm.h"        // dxc -Fh (build.ps1)
#include "rt_lights_bounce.h"     // dxc -Fh (build.ps1)
#include "rt_lights_bounce_omm.h" // dxc -Fh (build.ps1)
#include "rt_fog_omm.h"           // dxc -Fh (build.ps1)
#include "rt_path.h"              // dxc -Fh (build.ps1)
#include "rt_path_omm.h"          // dxc -Fh (build.ps1)
#include "rt_path_ser.h"          // dxc -Fh (build.ps1)
#include "rt_place.h"             // dxc -Fh (build.ps1)
#include "rt_omm_uvs.h"           // dxc -Fh (build.ps1)
#include "rt_reflect.h"           // dxc -Fh (build.ps1)
#include "rt_reflect_filter.h"    // dxc -Fh (build.ps1)
#include "rt_reflect_omm.h"       // dxc -Fh (build.ps1)
#include "rt_reuse.h"             // dxc -Fh (build.ps1)
#include "rt_reuse_omm.h"         // dxc -Fh (build.ps1)
#include "rt_reuse_lights.h"      // dxc -Fh (build.ps1)
#include "rt_reuse_lights_omm.h"  // dxc -Fh (build.ps1)
#include "rt_reuse_lean.h"        // dxc -Fh (build.ps1)
#include "rt_reuse_lean_omm.h"    // dxc -Fh (build.ps1)
#include "rt_reflect_reservoir.h"      // dxc -Fh (build.ps1)
#include "rt_reflect_reservoir_omm.h"  // dxc -Fh (build.ps1)
#include "rt_scene_data.h"        // dxc -Fh (build.ps1)
#include "rt_soft.h"              // dxc -Fh (build.ps1)
#include "rt_sway.h"              // dxc -Fh (build.ps1)
#include "rt_trace.h"             // dxc -Fh (build.ps1)
#include "rt_trace_omm.h"         // dxc -Fh (build.ps1)
// NVIDIA's SHaRC (NVIDIA RTX SDKs License; build.ps1 compiles its headers into these when build\downloads\sharc holds
// them): SHaRC Radiance Cache.
#if __has_include("rt_path_sharc.h")
#include "rt_path_sharc.h"
#include "rt_sharc_resolve.h"
#include "rt_sharc_update.h"
#define RENODX_SHARC 1
#endif

// The DLAA helper's ray tracing (bridge v4-v6; the addon's rt_scene.hpp): DXR needs a 64-bit process, the Total War
// games with the bridge are 32-bit. Its own D3D12 device keeps a BLAS per captured draw (its triangles in world space,
// copied from the shared vertex texture when the addon captures it; compacted once the GPU says how small it can be),
// refits the frame's units into one BLAS (rebuilt when their draws change, or every UNITS_REBUILD_FRAMES), builds the
// TLAS of the meshes the frame drew (and, but for the terrain, those drawn within RECENT_FRAMES: casters behind the
// camera) with it, and traces rt_trace.cs_6_5.hlsl into the shared visibility textures (soft shadows through
// rt_soft.cs_6_5.hlsl). Placed meshes (bridge v14: rigid objects and trees, keyed by their geometry) have a BLAS from their
// first instance, placed on the GPU (rt_place.cs_6_5.hlsl) where each frame's draws put their instances, each instance
// kept a while after its last draw (off-screen casters). Leaves are alpha-tested against their opacity masks (the addon's atlas, v6): each geometry has a
// record (its instance's InstanceID plus its geometry index) with its UVs and normals in pools (rt_scene_data.cs_6_5.hlsl).
// With the Agility SDK next to the helper (D3D12\D3D12Core.dll) and shader model 6.9, DXR 1.2: the leaves get opacity
// micromaps (baked by NVIDIA's OMM SDK, omm-lib.dll next to the helper; AdvanceMicromaps). With a bridge v7 addon's
// ray-traced Light Bounce a second request each frame traces rt_bounce.cs_6_5.hlsl against that frame's TLAS; bridge v8
// path traces it instead (rt_path.hlsli: a world radiance
// cache and the learned lighting for what the screen never saw), and a third request traces reflections and specular
// occlusion. All run as compute shaders (DXR 1.2's shader execution reordering made each pass slower in the harness at
// 640 x 360 to 4K, with and without clutter in view: the paths' hits need lookups, not divergent shading); Path Tracing
// SER (Experimental, v105) runs the paths and reflections as ray generation shaders that reorder (rt_path_ser) for
// tests in the game's cities. With
// NVAPI's cluster operations (RTX Mega Geometry; set RENODX_TW_NO_CLUSTERS to turn them off) the units are built each
// frame as clusters (CLAS) into one cluster BLAS instead of the refitted BLAS; compact draws (bridge v16: a draw's
// vertices streamed once each, its cluster set sent once and kept in a pool) become clusters on the GPU
// (rt_compact.cs_6_5.hlsl). The partitioned TLAS (PTLAS: each frame
// only the instances that changed written; only with RENODX_TW_PTLAS: it stalled a city siege) takes every change as a
// whole instance write: NVAPI's update operation (a new BLAS for an instance) stalled the GPU 4-5 s a time after BLAS
// compactions (GPU harness, helper19-21). Two PTLASes in turn, the operations in device memory, as NVAPI asks. Its
// queue waits for the addon's fence value and signals the next.
namespace twattila::dlaa_helper {

using Microsoft::WRL::ComPtr;

class RtScene12 {
 public:
  // The device on the game's GPU, with DXR 1.1 (1.2 through the Agility SDK in `folder`\D3D12 when the driver has shader
  // model 6.9); false: unavailable (the status says why).
  // `details` (the addon's Log Details at the device's start): DRED, below. `any_gpu`: another vendor's GPU too (FSR or XeSS
  // chosen, which only Attila's list has: its own denoisers then; no other driver has run this yet, so Experimental);
  // not for good: a later set with it starts here. RENODX_TW_FAKE_VENDOR (tests): another vendor's.
  bool Initialize(IDXGIAdapter1* adapter, const std::filesystem::path& folder, bool details, bool any_gpu = false) {
    if (failed_) return false;
    if (device_) return true;
    folder_ = folder;
    DXGI_ADAPTER_DESC1 adapter_identity = {};
    adapter->GetDesc1(&adapter_identity);
    if (std::array<char, 16> fake = {}; GetEnvironmentVariableA("RENODX_TW_FAKE_VENDOR", fake.data(), static_cast<DWORD>(fake.size())) != 0) {
      adapter_identity.VendorId = std::strtoul(fake.data(), nullptr, 16);
    }
    if (adapter_identity.VendorId != 0x10DE && !any_gpu) {
      status_ = "Ray tracing needs an NVIDIA RTX GPU";
      return false;
    }
    // The Agility SDK through a device factory of its own (no developer mode; frame generation and Ray Reconstruction
    // keep Windows' runtime).
    std::string sdk = "Windows' D3D12";
    const std::filesystem::path agility = folder / L"D3D12";
    ComPtr<ID3D12SDKConfiguration1> configuration;
    ComPtr<ID3D12DeviceFactory> factory;
    // DRED (Device Removed Extended Data): after a GPU fault or hang (ATN 2026-10-28: frozen, then no ray tracing until
    // the game restarts), each command list's operations up to where the GPU stopped, the passes' markers, a page fault's
    // allocations (LostReport).
    const auto record_removal = [details](ID3D12DeviceRemovedExtendedDataSettings1* settings) {
      const D3D12_DRED_ENABLEMENT enablement = (details ? D3D12_DRED_ENABLEMENT_FORCED_ON : D3D12_DRED_ENABLEMENT_FORCED_OFF);
      settings->SetAutoBreadcrumbsEnablement(enablement);
      settings->SetPageFaultEnablement(enablement);
      settings->SetBreadcrumbContextEnablement(enablement);
    };
    if (std::filesystem::exists(agility / L"D3D12Core.dll")
        && SUCCEEDED(D3D12GetInterface(SDK_CONFIGURATION, IID_PPV_ARGS(configuration.GetAddressOf())))
        && SUCCEEDED(configuration->CreateDeviceFactory(AGILITY_SDK_VERSION, (agility.string() + "\\").c_str(),
                                                        IID_PPV_ARGS(factory.GetAddressOf())))) {
      factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
      if (ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
          SUCCEEDED(factory->GetConfigurationInterface(DRED_CONFIGURATION, IID_PPV_ARGS(settings.GetAddressOf())))) {
        record_removal(settings.Get());
      }
      // RENODX_TW_D3D12_DEBUG (tests; D3D12\d3d12SDKLayers.dll next to D3D12Core.dll): D3D12's debug layer, its messages
      // into the log after each request.
      if (GetEnvironmentVariableW(L"RENODX_TW_D3D12_DEBUG", nullptr, 0) != 0) {
        ComPtr<ID3D12Debug> debug;
        debug_result_ = factory->GetConfigurationInterface(DEBUG_CONFIGURATION, IID_PPV_ARGS(debug.GetAddressOf()));
        if (SUCCEEDED(debug_result_)) debug->EnableDebugLayer();
      }
      if (SUCCEEDED(factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf())))) sdk = "Agility SDK";
      if (device_) device_.As(&info_queue_);
    }
    if (ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
        !device_ && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(settings.GetAddressOf())))) {
      record_removal(settings.Get());
    }
#if defined(RENODX_OMM_SDK)
    // Opacity micromaps for new leaves, baked on the CPU by NVIDIA's OMM SDK (the GPU bake it replaced stopped ATN's GPU
    // three times, 2026-10-28): its omm-lib.dll next to the helper, its CPU baker.
    if (HMODULE library = (omm_.library == nullptr) ? LoadLibraryW((folder / L"omm-lib.dll").c_str()) : nullptr; library != nullptr) {
      const auto load = [library](auto* function, const char* name) {
        *function = reinterpret_cast<std::remove_pointer_t<decltype(function)>>(GetProcAddress(library, name));
        return *function != nullptr;
      };
      decltype(&ommCreateBaker) create_baker = nullptr;
      ommBakerCreationDesc creation = ommBakerCreationDescDefault();
      creation.type = ommBakerType_CPU;
      const bool loaded = load(&create_baker, "ommCreateBaker") && load(&omm_.destroy_baker, "ommDestroyBaker")
                          && load(&omm_.create_texture, "ommCpuCreateTexture") && load(&omm_.destroy_texture, "ommCpuDestroyTexture")
                          && load(&omm_.bake, "ommCpuBake") && load(&omm_.result_desc, "ommCpuGetBakeResultDesc")
                          && load(&omm_.destroy_result, "ommCpuDestroyBakeResult");
      if (loaded && std::ranges::all_of(omm_.bakers, [&](ommBaker& baker) { return create_baker(&creation, &baker) == ommResult_SUCCESS; })) {
        omm_.library = library;
        // The GPU baker (GPU Micromap Baking; its pipelines made with the device: MakeGpuBaker).
        ommBakerCreationDesc gpu_creation = ommBakerCreationDescDefault();
        gpu_creation.type = ommBakerType_GPU;
        const ommGpuPipelineConfigDesc gpu_config = ommGpuPipelineConfigDescDefault();
        if (!load(&omm_.gpu_create_pipeline, "ommGpuCreatePipeline") || !load(&omm_.gpu_destroy_pipeline, "ommGpuDestroyPipeline")
            || !load(&omm_.gpu_pipeline_desc, "ommGpuGetPipelineDesc") || !load(&omm_.gpu_pre_dispatch, "ommGpuGetPreDispatchInfo")
            || !load(&omm_.gpu_dispatch, "ommGpuDispatch") || !load(&omm_.gpu_static_data, "ommGpuGetStaticResourceData")
            || create_baker(&gpu_creation, &omm_.gpu_baker) != ommResult_SUCCESS
            || omm_.gpu_create_pipeline(omm_.gpu_baker, &gpu_config, &omm_.gpu_pipeline) != ommResult_SUCCESS) {
          if (omm_.gpu_baker != nullptr) omm_.destroy_baker(omm_.gpu_baker);
          omm_.gpu_baker = nullptr;
          omm_.gpu_pipeline = nullptr;
        }
      } else {
        for (const ommBaker baker : omm_.bakers) {
          if (baker != nullptr) omm_.destroy_baker(baker);
        }
        FreeLibrary(library);
        omm_ = {};
      }
    }
    bake_micromaps_ = omm_.bakers[0] != nullptr;
#endif
    // Not for good: Windows refuses a process new devices for a while after GPU resets (ATN 2026-10-29: after four, "needs
    // DXR 1.1"); tried again at the next set.
    if (const HRESULT made = device_ ? S_OK : D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf())); FAILED(made)) {
      status_ = std::format("Ray tracing: no D3D12 device on the game's GPU (0x{:08X})", static_cast<uint32_t>(made));
      return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))
        || options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
      return Fail("Ray tracing needs a GPU and driver with DXR 1.1");
    }
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {.HighestShaderModel = D3D_SHADER_MODEL_6_9};
    // DXR 1.2's micromaps, reordering and SHaRC on NVIDIA's only for now: the path the harness's no_dxr12 modes run elsewhere.
    dxr12_ = adapter_identity.VendorId == 0x10DE && options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2
             && GetEnvironmentVariableW(L"RENODX_TW_NO_DXR12", nullptr, 0) == 0
             && SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
             && shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_9;
#if defined(RENODX_NVAPI_CLUSTERS)
    // RTX Mega Geometry: the driver's cluster operations, enabled for the pipelines made after this (they trace the
    // units' cluster BLAS).
    NvU32 cluster_caps = 0;
    clusters_ = GetEnvironmentVariableW(L"RENODX_TW_NO_CLUSTERS", nullptr, 0) == 0 && NvAPI_Initialize() == NVAPI_OK
                && NvAPI_D3D12_GetRaytracingCaps(device_.Get(), NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS, &cluster_caps,
                                                 sizeof(cluster_caps)) == NVAPI_OK
                && (cluster_caps & NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_STANDARD) != 0;
    if (clusters_) {
      const NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS options = {
          .version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER,
          .flags = NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT,
      };
      clusters_ = NvAPI_D3D12_SetCreatePipelineStateOptions(device_.Get(), &options) == NVAPI_OK;
    }
#endif
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (GetEnvironmentVariableW(L"RENODX_TW_NO_GPU_TIMES", nullptr, 0) == 0) {
      const D3D12_QUERY_HEAP_DESC heap_desc = {.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = static_cast<UINT>(SLOTS * STAMPS)};
      const D3D12_HEAP_PROPERTIES readback_heap = {.Type = D3D12_HEAP_TYPE_READBACK};
      const D3D12_RESOURCE_DESC readback_desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = SLOTS * STAMPS * sizeof(uint64_t),
                                                 .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1, .SampleDesc = {.Count = 1},
                                                 .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
      if (FAILED(device_->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(timestamps_.GetAddressOf())))
          || FAILED(device_->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                     nullptr, IID_PPV_ARGS(timestamp_readback_.GetAddressOf())))) {
        timestamps_.Reset();
      }
    }
    if (FAILED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue_.GetAddressOf())))
        || FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(own_fence_.GetAddressOf())))) {
      return Fail("Ray tracing: no D3D12 queue");
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_ == nullptr) return Fail("Ray tracing: no completion event");
    for (Slot& slot : slots_) {
      slot.instances = Buffer(uint64_t{MAX_INSTANCES} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                              D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.table = Buffer(uint64_t{dlaa_bridge::RT_CAPTURES} * 32, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.sizes = Buffer(uint64_t{COMPACTIONS} * sizeof(uint64_t), D3D12_HEAP_TYPE_DEFAULT,
                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      slot.readback = Buffer(uint64_t{COMPACTIONS} * sizeof(uint64_t), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                             D3D12_RESOURCE_STATE_COPY_DEST);
      slot.rows_readback = Buffer(uint64_t{ROWS_READBACK} * 64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      slot.jobs = Buffer(PLACE_TABLE_BYTES + uint64_t{PLACE_JOBS} * 32, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                         D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.left_out = Buffer(LEFT_OUT_BYTES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
      slot.left_out_readback = Buffer(LEFT_OUT_BYTES + sizeof(uint32_t), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                               IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close())
          || !slot.instances || !slot.table || !slot.sizes || !slot.readback || !slot.rows_readback || !slot.jobs
          || !slot.left_out || !slot.left_out_readback
          || FAILED(slot.left_out_readback->Map(0, nullptr, reinterpret_cast<void**>(&slot.left_out_data)))
          || FAILED(slot.instances->Map(0, nullptr, reinterpret_cast<void**>(&slot.instance_data)))
          || FAILED(slot.table->Map(0, nullptr, reinterpret_cast<void**>(&slot.table_data)))
          || FAILED(slot.jobs->Map(0, nullptr, reinterpret_cast<void**>(&slot.jobs_data)))) {
        return Fail("Ray tracing: no D3D12 command lists");
      }
      slot.list->SetName(std::format(L"slot {}", &slot - slots_.data()).c_str());
    }
    // The vertex copy (BLAS inputs), the records and pools, the TLAS and its instances' room (PlacedRoom).
    vertex_buffer_ = Buffer(uint64_t{TRANSFER_VERTICES} * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
    // The scene data pass's garbage count starts from these (none, no capture).
    left_out_start_ = Buffer(LEFT_OUT_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (uint32_t* start = nullptr; left_out_start_ && SUCCEEDED(left_out_start_->Map(0, nullptr, reinterpret_cast<void**>(&start)))) {
      const std::array<uint32_t, LEFT_OUT_BYTES / 4> none = {0, 0, 0, UINT32_MAX};
      std::memcpy(start, none.data(), sizeof(none));
      left_out_start_->Unmap(0, nullptr);
    }
    // The smoke's TLAS: its one instance (none: the smoke stays out of the trace).
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS smoke_inputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
        .NumDescs = 1,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
    };
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO smoke_info = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&smoke_inputs, &smoke_info);
    smoke_tlas_ = Buffer(smoke_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    smoke_tlas_scratch_ = Buffer(smoke_info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto pool = [&](uint64_t bytes) {
      return Buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    // A request's alpha-tested corners (grass, swaying leaves): all on GPUs over 20 GB, half over 12 GB, else a quarter
    // (the addon reads it back: rt_dynamic_uvs).
    DXGI_ADAPTER_DESC1 adapter_desc = {};
    adapter->GetDesc1(&adapter_desc);
    // RENODX_TW_TEST_GPU_GB=<GB>: the tiers of a GPU of that size (GPU harness).
    std::array<wchar_t, 16> test_gigabytes = {};
    const size_t video_memory = (GetEnvironmentVariableW(L"RENODX_TW_TEST_GPU_GB", test_gigabytes.data(), static_cast<DWORD>(test_gigabytes.size())) != 0)
                                    ? (static_cast<size_t>(_wtoi(test_gigabytes.data())) << 30)
                                    : adapter_desc.DedicatedVideoMemory;
    dynamic_uvs_ = (video_memory > (size_t{20} << 30))   ? DYNAMIC_UVS
                   : (video_memory > (size_t{12} << 30)) ? DYNAMIC_UVS / 2
                                                         : DYNAMIC_UVS / 4;
    // The TLAS's instances grow up to D3D12's most over 20 GB, 8,388,608 over 12 GB, else 4,194,304 (powers of two: rt_place's table).
    placed_limit_ = (video_memory > (size_t{20} << 30))   ? D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE
                    : (video_memory > (size_t{12} << 30)) ? (1u << 23)
                                                          : (1u << 22);
    uv_pool_ = STATIC_UVS + (SLOTS * dynamic_uvs_);
    records_ = pool(uint64_t{RECORD_COUNT} * 16);
    uvs_ = pool(uint64_t{uv_pool_} * 4);
    normals_ = pool(uint64_t{NORMAL_POOL} * 4);
    // Placed meshes (v14): their build rows by record.
    build_rows_ = Buffer(uint64_t{STATIC_RECORDS} * 48, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (PlacedRoom room; MakePlacedRoom(MAX_INSTANCES, &room)) {
      UsePlacedRoom(&room);
    }
    if (!vertex_buffer_ || !left_out_start_ || !tlas_ || !tlas_scratch_ || !records_ || !uvs_ || !normals_ || !build_rows_
        || !placed_instances_[0] || !placed_instances_[1] || !placed_state_) {
      return Fail("Ray tracing: D3D12 buffers failed");
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    // Mega Geometry: the CLASes (placed by the driver, their addresses in clas_addresses_), the units' cluster BLAS (at
    // cluster_blases_, their addresses in cluster_destination_), the scratch and the arguments, all with room for a frame's
    // clusters (ClusterRoom); the sequential 8-bit indices; a slot's captures' arguments and records written by the CPU
    // (cluster_data), its compact draws' table and new sets (compact_data); the sets' pool. A cluster has at most
    // RT_CLUSTER_TRIANGLES triangles (a compact draw's) over 255 vertices (a capture's sequential ones) and takes ~6 KB of
    // CLAS memory at most: the room starts at CLUSTER_ROOM (ATN's battles drew 8,000-21,000 a frame) and grows while a
    // frame fills 3/4 of it, up to CLUSTER_TIERS by the GPU's memory: all over 28 GB, 3/4 over 20 GB, a quarter over 12 GB,
    // else an eighth (4x unit size battles filled 262,144 with city walls left out).
    if (clusters_) {
      cluster_limit_ = (video_memory > (size_t{28} << 30))   ? CLUSTER_TIERS[0]
                       : (video_memory > (size_t{20} << 30)) ? CLUSTER_TIERS[1]
                       : (video_memory > (size_t{12} << 30)) ? CLUSTER_TIERS[2]
                                                             : CLUSTER_TIERS[3];
      // Of the room a quarter built from triangles (the captures', the compact draws' whose set has no templates yet), the
      // rest instantiated.
      clas_inputs_ = {
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS,
      };
      clas_inputs_.trianglesDesc = {
          .vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
          .maxGeometryIndexValue = cluster_limit_ - 1,
          .maxUniqueGeometryCountPerArg = 1,
          .maxTriangleCountPerArg = dlaa_bridge::RT_CLUSTER_TRIANGLES,
          .maxVertexCountPerArg = CLUSTER_TRIANGLES * 3,
          .maxTotalTriangleCount = DYNAMIC_TRIANGLES,
          .maxTotalVertexCount = TRANSFER_VERTICES,  // each a texel of the request's
          .minPositionTruncateBitCount = 0,
      };
      cluster_blas_inputs_ = {
          .maxArgCount = 1,
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS,
      };
      // Templates (a set's opaque clusters' triangles, built once, explicit places in templates_) and their instantiation
      // each frame (the vertices in the vertex buffer) into the first batch of CLASes.
      template_inputs_ = {
          .maxArgCount = TEMPLATE_BUILDS,
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS,
      };
      template_inputs_.trianglesDesc = {
          .vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
          .maxGeometryIndexValue = cluster_limit_ - 1,
          .maxUniqueGeometryCountPerArg = 1,
          .maxTriangleCountPerArg = dlaa_bridge::RT_CLUSTER_TRIANGLES,
          .maxVertexCountPerArg = dlaa_bridge::RT_CLUSTER_VERTICES,
          .maxTotalTriangleCount = TEMPLATE_BUILDS * dlaa_bridge::RT_CLUSTER_TRIANGLES,
          .maxTotalVertexCount = TEMPLATE_BUILDS * dlaa_bridge::RT_CLUSTER_VERTICES,
          .minPositionTruncateBitCount = 0,
      };
      instantiate_inputs_ = {
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS,
      };
      instantiate_inputs_.trianglesDesc = {
          .vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
          .maxGeometryIndexValue = cluster_limit_ - 1,
          .maxUniqueGeometryCountPerArg = 1,
          .maxTriangleCountPerArg = dlaa_bridge::RT_CLUSTER_TRIANGLES,
          .maxVertexCountPerArg = dlaa_bridge::RT_CLUSTER_VERTICES,
          .maxTotalTriangleCount = DYNAMIC_TRIANGLES,
          .maxTotalVertexCount = TRANSFER_VERTICES,
          .minPositionTruncateBitCount = 0,
      };
      // Grass micromaps: every cluster operation allows them (templates, instantiations and the BLAS from them read them);
      // without the driver's support for that, none.
      const auto allow_micromaps = [&](bool allowed) {
        const auto operation_flags = allowed ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM
                                             : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE;
        for (auto* inputs : {&clas_inputs_, &cluster_blas_inputs_, &template_inputs_, &instantiate_inputs_}) {
          inputs->flags = operation_flags;
        }
        cluster_micromaps_ = allowed;
      };
      // A template's most bytes: its requirements as the only one.
      NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO template_info = {};
      const auto size_clusters = [&] {
        auto one_template = template_inputs_;
        one_template.maxArgCount = 1;
        ClusterRoom made;
        // GPUs over 20 GB start at 262,144 (~2.5 GB): ATN's battle start grew it four times (32 K to 320 K, 2026-11-01),
        // the swaying grass that did not fit left out meanwhile (its shadows flickered).
        const uint32_t room = (cluster_limit_ > CLUSTER_TIERS[2]) ? CLUSTER_TIERS[2] : std::min(cluster_limit_, CLUSTER_ROOM);
        if (!ClusterRequirements(one_template, &template_info) || !SizeClusterRoom(room, room / 4, &made) || !MakeClusterRoom(&made)) {
          return false;
        }
        UseClusterRoom(&made);
        return true;
      };
      allow_micromaps(dxr12_ && bake_micromaps_);
      clusters_ = size_clusters();
      if (!clusters_ && cluster_limit_ > CLUSTER_TIERS[2]) {
        cluster_limit_ = CLUSTER_TIERS[2];
        room_note_ = std::format("No memory: {} clusters at most | ", cluster_limit_);
        clusters_ = size_clusters();
      }
      if (!clusters_ && cluster_micromaps_) {
        allow_micromaps(false);
        clusters_ = size_clusters();
      }
      if (clusters_) {
        template_bytes_ = static_cast<uint32_t>(Align(template_info.resultDataMaxSizeInBytes, NVAPI_D3D12_RAYTRACING_CLUSTER_TEMPLATE_BYTE_ALIGNMENT));
        cluster_destination_ = ClusterBuffer(8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cluster_indices_ = Buffer(256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        set_pool_ = Buffer(uint64_t{SET_POOL_BYTES} + 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        templates_ = ClusterBuffer(TEMPLATE_POOL_BYTES, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        template_destinations_ = ClusterBuffer(uint64_t{TEMPLATE_BUILDS} * 8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        template_geometry_ = Buffer(uint64_t{dlaa_bridge::RT_CLUSTER_TRIANGLES} * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        clusters_ = cluster_destination_ && cluster_indices_ && set_pool_ && templates_ && template_destinations_ && template_geometry_
                    && template_bytes_ != 0;
      }
      for (Slot& slot : slots_) {
        if (!clusters_) break;
        slot.clusters = Buffer(CLUSTER_UPLOAD_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        slot.compact = Buffer(COMPACT_UPLOAD_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        clusters_ = slot.clusters && SUCCEEDED(slot.clusters->Map(0, nullptr, reinterpret_cast<void**>(&slot.cluster_data))) && slot.compact
                    && SUCCEEDED(slot.compact->Map(0, nullptr, reinterpret_cast<void**>(&slot.compact_data)));
        if (clusters_) {
          for (uint32_t index = 0; index < 256; ++index) slot.cluster_data[CLUSTER_INDICES_OFFSET + index] = static_cast<uint8_t>(index);
        }
      }
      free_set_units_.free = {{0, SET_POOL_BYTES / 16}};
      if (clusters_) free_templates_.free = {{0, static_cast<uint32_t>(TEMPLATE_POOL_BYTES / template_bytes_)}};
    }
    // The partitioned TLAS: an instance per static record (its index) and one for the units, PTLAS_PARTITION_SIZE a
    // partition; a slot's write and update arguments, its two operations and their count. Only with RENODX_TW_PTLAS:
    // a city siege in Attila stalled each frame by ~0.3 s with it, the TLAS built whole costs 0.10-0.16 ms (ATN's log
    // 2026-10-29: all a PTLAS could save), and it has no placed meshes (v14: rt_place writes the TLAS's descs only).
    NvU32 ptlas_caps = 0;
    ptlas_ = nullptr;
    if (GetEnvironmentVariableW(L"RENODX_TW_PTLAS", nullptr, 0) != 0 && NvAPI_Initialize() == NVAPI_OK
        && NvAPI_D3D12_GetRaytracingCaps(device_.Get(), NVAPI_D3D12_RAYTRACING_CAPS_TYPE_PARTITIONED_TLAS, &ptlas_caps, sizeof(ptlas_caps)) == NVAPI_OK
        && (ptlas_caps & NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_CAP_STANDARD) != 0) {
      ptlas_inputs_ = {
          .flags = NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_FLAG_FAST_TRACE,
          .instanceCount = PTLAS_INSTANCES,
          .maxInstancePerPartitionCount = PTLAS_PARTITION_SIZE,
          .partitionCount = (PTLAS_INSTANCES + PTLAS_PARTITION_SIZE - 1) / PTLAS_PARTITION_SIZE,
          .maxInstanceInGlobalPartitionCount = 0,
      };
      NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO info = {};
      const NVAPI_GET_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO_PARAMS params = {
          .version = NVAPI_GET_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO_PARAMS_VER, .pInput = &ptlas_inputs_, .pInfo = &info};
      bool made = NvAPI_D3D12_GetRaytracingPartitionedTlasIndirectPrebuildInfo(device_.Get(), &params) == NVAPI_OK;
      // Two partitioned TLASes in turn (each build reads the last one, never in place) and the operations in device memory
      // (NVAPI: indirectOps / indirectOpCount in NON_PIXEL_SHADER_RESOURCE), copied from the slot's upload buffer.
      if (made) {
        for (auto& buffer : ptlas_buffers_) {
          buffer = Buffer(info.resultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          made = made && buffer;
        }
        ptlas_scratch_ = Buffer(std::max<uint64_t>(info.scratchDataSizeInBytes, 256), D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        made = made && ptlas_scratch_;
      }
      for (Slot& slot : slots_) {
        if (!made) break;
        slot.ptlas = Buffer(PTLAS_UPLOAD_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        slot.ptlas_device = Buffer(PTLAS_UPLOAD_BYTES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        made = slot.ptlas && slot.ptlas_device && SUCCEEDED(slot.ptlas->Map(0, nullptr, reinterpret_cast<void**>(&slot.ptlas_data)));
      }
      if (made) {
        ptlas_current_ = 0;
        ptlas_ = ptlas_buffers_[0].Get();
        ptlas_written_.assign(PTLAS_INSTANCES, {});
        ptlas_wanted_.assign(PTLAS_INSTANCES, {});
      }
    }
#endif
    free_triangles_.free = {{0, STATIC_TRIANGLES}};
    free_instances_.free = {{STATIC_RECORDS, MAX_INSTANCES - STATIC_RECORDS}};
    free_uvs_.free = {{0, STATIC_UVS}};
    for (uint32_t record = STATIC_RECORDS; record > 0; --record) {
      free_records_.push_back(record - 1);
    }
    // The passes: t0 the TLAS (root SRV); the table (Descriptor); b0 the settings, b1 a dispatch's (root constants); t8
    // the capture table, u6 / u7 / u8 the micromaps' buffers (root descriptors); s0 linear wrap.
    const std::array<D3D12_DESCRIPTOR_RANGE, 6> ranges = {{
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = SRV_COUNT, .BaseShaderRegister = 1, .OffsetInDescriptorsFromTableStart = 0},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = LIGHT - SRV_COUNT, .BaseShaderRegister = 0,
         .OffsetInDescriptorsFromTableStart = SRV_COUNT},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = 1, .BaseShaderRegister = 9, .OffsetInDescriptorsFromTableStart = LIGHT},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = 1, .BaseShaderRegister = 9, .OffsetInDescriptorsFromTableStart = BOUNCE},
        // Paths (v8): t10-t14, u10-u21; the local lights' mask (v9) u22; ReSTIR GI's reservoirs u23-u26; the fog (v11) u27.
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = CACHE_KEYS - PREVIOUS_COLOR, .BaseShaderRegister = 10,
         .OffsetInDescriptorsFromTableStart = PREVIOUS_COLOR},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = DESCRIPTOR_COUNT - CACHE_KEYS, .BaseShaderRegister = 10,
         .OffsetInDescriptorsFromTableStart = CACHE_KEYS},
    }};
    const std::array<D3D12_ROOT_PARAMETER, 12> parameters = {{
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 0}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
         .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .Num32BitValues = 12}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 1, .Num32BitValues = 8}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 8}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 6}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 7}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 8}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 2, .Num32BitValues = 20}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {.ShaderRegister = 3}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {.ShaderRegister = 4}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 15}},
    }};
    const D3D12_STATIC_SAMPLER_DESC sampler = {
        .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .MaxLOD = D3D12_FLOAT32_MAX,
        .ShaderRegister = 0,
    };
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {
        .NumParameters = static_cast<UINT>(parameters.size()),
        .pParameters = parameters.data(),
        .NumStaticSamplers = 1,
        .pStaticSamplers = &sampler,
    };
    ComPtr<ID3DBlob> serialized;
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = DESCRIPTOR_COUNT,
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), nullptr))
        || FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                               IID_PPV_ARGS(root_signature_.GetAddressOf())))) {
      return Fail("Ray tracing: the root signature failed");
    }
    const auto pipeline = [&](const void* code, size_t bytes, ComPtr<ID3D12PipelineState>* state) {
      const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {.pRootSignature = root_signature_.Get(), .CS = {.pShaderBytecode = code, .BytecodeLength = bytes}};
      return SUCCEEDED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(state->GetAddressOf())));
    };
    // DXR 1.2: the ray queries that use the micromaps; else (or when they fail) DXR 1.1.
    dxr12_ = dxr12_ && pipeline(RT_TRACE_OMM, sizeof(RT_TRACE_OMM), &trace_pipeline_)
             && pipeline(RT_BOUNCE_OMM, sizeof(RT_BOUNCE_OMM), &bounce_pipeline_) && pipeline(RT_PATH_OMM, sizeof(RT_PATH_OMM), &path_pipeline_)
             && pipeline(RT_REFLECT_OMM, sizeof(RT_REFLECT_OMM), &reflect_pipeline_) && pipeline(RT_REUSE_OMM, sizeof(RT_REUSE_OMM), &reuse_pipeline_)
             && pipeline(RT_REFLECT_RESERVOIR_OMM, sizeof(RT_REFLECT_RESERVOIR_OMM), &reflect_reservoir_pipeline_)
             && pipeline(RT_FOG_OMM, sizeof(RT_FOG_OMM), &fog_pipeline_) && pipeline(RT_LIGHTS_OMM, sizeof(RT_LIGHTS_OMM), &lights_pipeline_);
    if (!dxr12_) {
      trace_pipeline_.Reset();
      bounce_pipeline_.Reset();
      path_pipeline_.Reset();
      reflect_pipeline_.Reset();
      reuse_pipeline_.Reset();
      reflect_reservoir_pipeline_.Reset();
      fog_pipeline_.Reset();
      lights_pipeline_.Reset();
    }
    if ((!trace_pipeline_ && !pipeline(RT_TRACE, sizeof(RT_TRACE), &trace_pipeline_))
        || (!bounce_pipeline_ && !pipeline(RT_BOUNCE, sizeof(RT_BOUNCE), &bounce_pipeline_)) || !pipeline(RT_SOFT, sizeof(RT_SOFT), &soft_pipeline_)
        || !pipeline(RT_SCENE_DATA, sizeof(RT_SCENE_DATA), &scene_data_pipeline_) || !pipeline(RT_PLACE, sizeof(RT_PLACE), &place_pipeline_)
        || (!path_pipeline_ && !pipeline(RT_PATH, sizeof(RT_PATH), &path_pipeline_))
        || (!reflect_pipeline_ && !pipeline(RT_REFLECT, sizeof(RT_REFLECT), &reflect_pipeline_))
        || (!reuse_pipeline_ && !pipeline(RT_REUSE, sizeof(RT_REUSE), &reuse_pipeline_))
        || (!reflect_reservoir_pipeline_ && !pipeline(RT_REFLECT_RESERVOIR, sizeof(RT_REFLECT_RESERVOIR), &reflect_reservoir_pipeline_))
        || (!fog_pipeline_ && !pipeline(RT_FOG, sizeof(RT_FOG), &fog_pipeline_))
        || (!lights_pipeline_ && !pipeline(RT_LIGHTS, sizeof(RT_LIGHTS), &lights_pipeline_))
        || !pipeline(RT_CACHE_FILL, sizeof(RT_CACHE_FILL), &cache_fill_pipeline_)
        || !pipeline(RT_CACHE_RESOLVE, sizeof(RT_CACHE_RESOLVE), &cache_resolve_pipeline_)
        || !pipeline(RT_REFLECT_FILTER, sizeof(RT_REFLECT_FILTER), &reflect_filter_pipeline_)
        || FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(descriptors_.GetAddressOf())))) {
      return Fail("Ray tracing: the trace shaders failed (needs shader model 6.5)");
    }
    // Torch and Fire Light in the Light Bounce's builds (TORCHES_IN_BOUNCE; none: its rows do nothing).
    if (!(dxr12_ ? pipeline(RT_REUSE_LIGHTS_OMM, sizeof(RT_REUSE_LIGHTS_OMM), &reuse_lights_pipeline_)
                       && pipeline(RT_LIGHTS_BOUNCE_OMM, sizeof(RT_LIGHTS_BOUNCE_OMM), &lights_bounce_pipeline_)
                 : pipeline(RT_REUSE_LIGHTS, sizeof(RT_REUSE_LIGHTS), &reuse_lights_pipeline_)
                       && pipeline(RT_LIGHTS_BOUNCE, sizeof(RT_LIGHTS_BOUNCE), &lights_bounce_pipeline_))) {
      reuse_lights_pipeline_.Reset();
      lights_bounce_pipeline_.Reset();
    }
    // Lean Reuse's build (REUSE_LEAN; none: its switch does nothing).
    if (!(dxr12_ ? pipeline(RT_REUSE_LEAN_OMM, sizeof(RT_REUSE_LEAN_OMM), &reuse_lean_pipeline_)
                 : pipeline(RT_REUSE_LEAN, sizeof(RT_REUSE_LEAN), &reuse_lean_pipeline_))) {
      reuse_lean_pipeline_.Reset();
    }
    clusters_ = clusters_ && pipeline(RT_COMPACT, sizeof(RT_COMPACT), &compact_pipeline_);
    // DXR 1.2: the paths and reflections as ray generation shaders for Path Tracing SER (none: their compute shaders).
    if (dxr12_) {
      const std::array<D3D12_EXPORT_DESC, 2> exports = {{{.Name = L"PathTraceRayGen"}, {.Name = L"ReflectRayGen"}}};
      const D3D12_DXIL_LIBRARY_DESC library = {.DXILLibrary = {.pShaderBytecode = RT_PATH_SER, .BytecodeLength = sizeof(RT_PATH_SER)},
                                               .NumExports = static_cast<UINT>(exports.size()),
                                               .pExports = const_cast<D3D12_EXPORT_DESC*>(exports.data())};
      const D3D12_RAYTRACING_SHADER_CONFIG shader_config = {.MaxPayloadSizeInBytes = 4, .MaxAttributeSizeInBytes = 8};
      const D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config = {.MaxTraceRecursionDepth = 1,
                                                                 .Flags = D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS};
      const D3D12_GLOBAL_ROOT_SIGNATURE global = {.pGlobalRootSignature = root_signature_.Get()};
      const std::array<D3D12_STATE_SUBOBJECT, 4> subobjects = {{
          {.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &library},
          {.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shader_config},
          {.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1, .pDesc = &pipeline_config},
          {.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &global},
      }};
      const D3D12_STATE_OBJECT_DESC state_desc = {.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
                                                  .NumSubobjects = static_cast<UINT>(subobjects.size()),
                                                  .pSubobjects = subobjects.data()};
      ComPtr<ID3D12StateObjectProperties> properties;
      uint8_t* table = nullptr;
      path_ser_table_ = Buffer(PATH_SER_RECORD * 2, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      if (path_ser_table_ && SUCCEEDED(device_->CreateStateObject(&state_desc, IID_PPV_ARGS(path_ser_.GetAddressOf())))
          && SUCCEEDED(path_ser_.As(&properties)) && properties->GetShaderIdentifier(L"PathTraceRayGen") != nullptr
          && properties->GetShaderIdentifier(L"ReflectRayGen") != nullptr && SUCCEEDED(path_ser_table_->Map(0, nullptr, reinterpret_cast<void**>(&table)))) {
        std::memcpy(table, properties->GetShaderIdentifier(L"PathTraceRayGen"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        std::memcpy(table + PATH_SER_RECORD, properties->GetShaderIdentifier(L"ReflectRayGen"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        path_ser_table_->Unmap(0, nullptr);
      } else {
        path_ser_.Reset();
        path_ser_table_.Reset();
      }
    }
    if (clusters_ && !pipeline(RT_SWAY, sizeof(RT_SWAY), &sway_pipeline_)) sway_pipeline_.Reset();
#if defined(RENODX_SHARC)
    // SHaRC Radiance Cache (shader model 6.9 here: 64-bit atomics, 16-bit types): its pipelines, or none (the status says
    // which failed).
    if (dxr12_) {
      const std::array<std::tuple<const void*, size_t, ComPtr<ID3D12PipelineState>*, const char*>, 3> sharc_pipelines = {{
          {RT_PATH_SHARC, sizeof(RT_PATH_SHARC), &sharc_path_pipeline_, "paths"},
          {RT_SHARC_UPDATE, sizeof(RT_SHARC_UPDATE), &sharc_update_pipeline_, "update"},
          {RT_SHARC_RESOLVE, sizeof(RT_SHARC_RESOLVE), &sharc_resolve_pipeline_, "resolve"},
      }};
      for (const auto& [code, bytes, state, name] : sharc_pipelines) {
        if (sharc_failed_.empty() && !pipeline(code, bytes, state)) sharc_failed_ = name;
      }
      if (!sharc_failed_.empty()) {
        for (const auto& sharc_pipeline : sharc_pipelines) std::get<2>(sharc_pipeline)->Reset();
      }
    }
#endif
    std::string gpu_baker = "no micromaps";
#if defined(RENODX_OMM_SDK)
    if (dxr12_ && bake_micromaps_) gpu_baker = pipeline(RT_OMM_UVS, sizeof(RT_OMM_UVS), &omm_gpu_.clamp) ? std::string() : "rt_omm_uvs";
    gpu_micromaps_ = gpu_baker.empty() && MakeGpuBaker(&gpu_baker);
#endif
    for (Slot& slot : slots_) {
      slot.lights = Buffer(LIGHT_CONSTANT_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.paths = Buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      if (!slot.lights || FAILED(slot.lights->Map(0, nullptr, reinterpret_cast<void**>(&slot.light_data))) || !slot.paths
          || FAILED(slot.paths->Map(0, nullptr, reinterpret_cast<void**>(&slot.paths_data)))) {
        return Fail("Ray tracing: no D3D12 constant buffers");
      }
    }
    descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto buffer_view = [](DXGI_FORMAT format, UINT elements) {
      return D3D12_SHADER_RESOURCE_VIEW_DESC{.Format = format, .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                             .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                             .Buffer = {.NumElements = elements}};
    };
    const auto buffer_uav = [](DXGI_FORMAT format, UINT elements) {
      return D3D12_UNORDERED_ACCESS_VIEW_DESC{.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER, .Buffer = {.NumElements = elements}};
    };
    const auto records_view = buffer_view(DXGI_FORMAT_R32G32B32A32_UINT, RECORD_COUNT);
    const auto uvs_view = buffer_view(DXGI_FORMAT_R32_UINT, uv_pool_);
    const auto normals_view = buffer_view(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
    const auto vertices_view = buffer_view(DXGI_FORMAT_R32G32B32A32_FLOAT, TRANSFER_VERTICES);
    device_->CreateShaderResourceView(records_.Get(), &records_view, Descriptor(RECORDS));
    device_->CreateShaderResourceView(uvs_.Get(), &uvs_view, Descriptor(UVS));
    device_->CreateShaderResourceView(normals_.Get(), &normals_view, Descriptor(NORMALS));
    device_->CreateShaderResourceView(vertex_buffer_.Get(), &vertices_view, Descriptor(VERTICES));
    const auto records_written = buffer_uav(DXGI_FORMAT_R32G32B32A32_UINT, RECORD_COUNT);
    const auto uvs_written = buffer_uav(DXGI_FORMAT_R32_UINT, uv_pool_);
    const auto normals_written = buffer_uav(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
    device_->CreateUnorderedAccessView(records_.Get(), nullptr, &records_written, Descriptor(RECORDS_WRITTEN));
    device_->CreateUnorderedAccessView(uvs_.Get(), nullptr, &uvs_written, Descriptor(UVS_WRITTEN));
    device_->CreateUnorderedAccessView(normals_.Get(), nullptr, &normals_written, Descriptor(NORMALS_WRITTEN));
    WriteNullPathDescriptors();
    status_ = std::format("Ray tracing ready ({}{}{}; {}{})", dxr12_ ? "DXR 1.2: opacity micromaps" : "DXR 1.1",
                          std::string(path_ser_ ? ", shader execution reordering" : "") + (sharc_path_pipeline_ ? ", SHaRC" : (sharc_failed_.empty() ? "" : ", no SHaRC: its " + sharc_failed_ + " failed")),
                          (dxr12_ && bake_micromaps_) ? (gpu_micromaps_ ? "; leaf micromaps: NVIDIA OMM SDK (CPU, GPU)"
                                                                        : "; leaf micromaps: NVIDIA OMM SDK (CPU; GPU off: " + gpu_baker + ")")
                                                      : std::string(),
                          sdk,
                          clusters_ ? std::format("; Mega Geometry: room for {} clusters a frame, up to {} ({} from templates of {} bytes), {} MB",
                                                  cluster_room_, cluster_limit_, cluster_room_ - built_limit_, template_bytes_,
                                                  (cluster_bytes_ + TEMPLATE_POOL_BYTES) >> 20)
                                    : std::string());
    slow_report_ = "[TW ray tracing] " + status_
                   + (info_queue_                ? std::string(" (D3D12 debug layer on).")
                      : (debug_result_ != S_FALSE) ? std::format(" (no D3D12 debug layer: 0x{:08X}).", static_cast<uint32_t>(debug_result_))
                                                   : std::string("."));
    sdk_ = sdk;
    return true;
  }

  // The addon's shared textures and fence (handles still open); false: not all could be opened.
  bool Open(const dlaa_bridge::Shared& shared, uint32_t version) {
    Close();
    if (!device_) return false;
    const auto open = [&](uint64_t handle, ComPtr<ID3D12Resource>* resource) {
      return SUCCEEDED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(handle), IID_PPV_ARGS(resource->GetAddressOf())));
    };
    if (!open(shared.rt_vertices, &vertices_) || !open(shared.rt_positions, &positions_) || !open(shared.rt_sun, &sun_)
        || !open(shared.rt_visibility, &visibility_) || !open(shared.rt_ambient, &ambient_)
        || FAILED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(shared.fence), IID_PPV_ARGS(fence_.GetAddressOf())))) {
      Close();
      return false;
    }
    // The leaves' opacity masks (v6; a texture the addon keeps across sizes) and the soft shadows' rays (its size).
    if (version >= 6 && shared.rt_atlas != 0) open(shared.rt_atlas, &atlas_);
    // The ray-traced Light Bounce's (v7): the prepared light in, the bounce out.
    if (version >= 7 && shared.rt_light != 0 && shared.rt_bounce != 0 && open(shared.rt_light, &light_) && open(shared.rt_bounce, &bounce_)) {
      const D3D12_SHADER_RESOURCE_VIEW_DESC light_view = {
          .Format = DXGI_FORMAT_R11G11B10_FLOAT,
          .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
          .Texture2D = {.MipLevels = 1},
      };
      const D3D12_UNORDERED_ACCESS_VIEW_DESC bounce_view = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateShaderResourceView(light_.Get(), &light_view, Descriptor(LIGHT));
      device_->CreateUnorderedAccessView(bounce_.Get(), nullptr, &bounce_view, Descriptor(BOUNCE));
    } else {
      light_.Reset();
      bounce_.Reset();
    }
    // The local lights' lit bits (v9).
    if (version >= 9 && shared.rt_light_mask != 0 && open(shared.rt_light_mask, &light_mask_)) {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC mask_view = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateUnorderedAccessView(light_mask_.Get(), nullptr, &mask_view, Descriptor(LIGHT_MASK));
    }
    // The fog shadows (v11).
    if (version >= 11 && shared.rt_fog != 0 && open(shared.rt_fog, &fog_)) {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC fog_view = {.Format = DXGI_FORMAT_R8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateUnorderedAccessView(fog_.Get(), nullptr, &fog_view, Descriptor(FOG));
    }
    if (version >= 18 && shared.rt_fog_lights != 0 && open(shared.rt_fog_lights, &fog_lights_)) {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC fog_lights_view = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateUnorderedAccessView(fog_lights_.Get(), nullptr, &fog_lights_view, Descriptor(FOG_LIGHTS));
    }
    if (version >= 19 && shared.rt_tint != 0 && open(shared.rt_tint, &tint_)) {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC tint_view = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateUnorderedAccessView(tint_.Get(), nullptr, &tint_view, Descriptor(TINT));
    }
    // Paths (v8): last frame's scene colour (the DLAA input), the G-buffer guides and the reflections' outputs as the
    // addon shares them (the history of the helper's own made when first used: EnsureHistory).
    if (version >= 8) {
      const auto view = [](DXGI_FORMAT format) {
        return D3D12_SHADER_RESOURCE_VIEW_DESC{.Format = format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                               .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                               .Texture2D = {.MipLevels = 1}};
      };
      if (shared.color != 0 && open(shared.color, &previous_color_)) {
        const auto color_view = view(DXGI_FORMAT_R16G16B16A16_FLOAT);
        device_->CreateShaderResourceView(previous_color_.Get(), &color_view, Descriptor(PREVIOUS_COLOR));
      }
      if (shared.normals != 0 && shared.diffuse_albedo != 0 && shared.roughness != 0 && open(shared.normals, &guide_normals_)
          && open(shared.diffuse_albedo, &guide_albedo_) && open(shared.roughness, &guide_roughness_)) {
        const auto normals_view = view(DXGI_FORMAT_R16G16B16A16_FLOAT);
        const auto albedo_view = view(DXGI_FORMAT_R8G8B8A8_UNORM);
        const auto roughness_view = view(DXGI_FORMAT_R8_UNORM);
        device_->CreateShaderResourceView(guide_normals_.Get(), &normals_view, Descriptor(GUIDE_NORMALS));
        device_->CreateShaderResourceView(guide_albedo_.Get(), &albedo_view, Descriptor(GUIDE_ALBEDO));
        device_->CreateShaderResourceView(guide_roughness_.Get(), &roughness_view, Descriptor(GUIDE_ROUGHNESS));
      } else {
        guide_normals_.Reset();
        guide_albedo_.Reset();
        guide_roughness_.Reset();
      }
      if (shared.rt_reflection != 0 && shared.rt_specular != 0 && open(shared.rt_reflection, &reflection_) && open(shared.rt_specular, &specular_)) {
        Written(reflection_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, REFLECTION);
        Written(specular_.Get(), DXGI_FORMAT_R8_UNORM, SPECULAR_OCCLUSION);
        // Ray Reconstruction's specular hit distance (v17).
        if (version >= 17 && shared.rr_hit_distance != 0 && open(shared.rr_hit_distance, &hit_distance_)) {
          Written(hit_distance_.Get(), DXGI_FORMAT_R16_FLOAT, HIT_DISTANCE);
        }
      } else {
        reflection_.Reset();
        specular_.Reset();
      }
    }
    const D3D12_RESOURCE_DESC size = visibility_->GetDesc();
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC rays_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = size.Width,
        .Height = size.Height,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_R16G16_FLOAT,
        .SampleDesc = {.Count = 1},
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    };
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rays_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(sun_rays_.GetAddressOf())))) {
      Close();
      return false;
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC float4_view = {
        .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2D = {.MipLevels = 1},
    };
    const D3D12_SHADER_RESOURCE_VIEW_DESC atlas_view = {
        .Format = DXGI_FORMAT_R8_UNORM,
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2DArray = {.MipLevels = 1, .ArraySize = dlaa_bridge::RT_ATLAS_SLICES},
    };
    const D3D12_UNORDERED_ACCESS_VIEW_DESC unorm_view = {.Format = DXGI_FORMAT_R8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    const D3D12_UNORDERED_ACCESS_VIEW_DESC rays_view = {.Format = DXGI_FORMAT_R16G16_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    device_->CreateShaderResourceView(positions_.Get(), &float4_view, Descriptor(POSITIONS));
    device_->CreateShaderResourceView(sun_.Get(), &float4_view, Descriptor(SUN));
    device_->CreateShaderResourceView(atlas_.Get(), &atlas_view, Descriptor(ATLAS));
    device_->CreateUnorderedAccessView(visibility_.Get(), nullptr, &unorm_view, Descriptor(VISIBILITY));
    device_->CreateUnorderedAccessView(ambient_.Get(), nullptr, &unorm_view, Descriptor(AMBIENT));
    device_->CreateUnorderedAccessView(sun_rays_.Get(), nullptr, &rays_view, Descriptor(SUN_RAYS));
    return true;
  }

  void Close() {
    WaitForGpuBeforeRelease(own_fence_.Get(), event_, own_value_, FENCE_TIMEOUT, "ray tracing");
    vertices_.Reset();
    positions_.Reset();
    sun_.Reset();
    visibility_.Reset();
    ambient_.Reset();
    atlas_.Reset();
    sun_rays_.Reset();
#if defined(RENODX_NRD)
    for (NrdSignal* signal : {&nrd_sun_, &nrd_ambient_, &nrd_bounce_, &nrd_reflections_}) {
      signal->denoiser.reset();
      signal->failed = false;
    }
    nrd_bounce_hit_distance_.Reset();
#endif
    nrd_off_ = 0;
    light_.Reset();
    bounce_.Reset();
    light_mask_.Reset();
    fog_.Reset();
    fog_lights_.Reset();
    tint_.Reset();
    ambient_history_ = {};
    ambient_positions_.Reset();
    light_history_ = {};
    previous_color_.Reset();
    guide_normals_.Reset();
    guide_albedo_.Reset();
    guide_roughness_.Reset();
    kept_positions_.Reset();
    water_positions_.Reset();
    reflection_.Reset();
    specular_.Reset();
    hit_distance_.Reset();
    specular_accumulated_.Reset();
    specular_history_.Reset();
    specular_meta_.Reset();
    specular_meta_history_.Reset();
    reuse_samples_ = {};
    reuse_data_ = {};
    own_sky_ = {};
    light_ratio_ = {};
    reflect_samples_ = {};
    reflect_data_ = {};
    if (descriptors_) WriteNullPathDescriptors();
    fence_.Reset();
  }

  [[nodiscard]] bool Ready() const { return fence_ != nullptr; }
  // The signals asked for NRD that it did not run on (dlaa_bridge.h rt_nrd_off).
  [[nodiscard]] uint32_t NrdOff() const { return nrd_off_.load(); }

  [[nodiscard]] std::string Status() const {
    // The GPU times right after the label: the bridge's line holds 255 characters, a battle's counts run past them.
    const size_t label = status_.find("): ");
    if (label == std::string::npos) return status_ + cpu_timings_;
    return std::format("{}{}{}{}{}{}", status_.substr(0, label + 3), room_note_, gpu_timings_, gpu_timings_.empty() ? "" : " | ", status_.substr(label + 3), cpu_timings_);
  }
  // A GPU fault or hang (Windows' timeout detection) removed the device: nothing runs on it again.
  [[nodiscard]] HRESULT RemovedReason() const { return device_ ? device_->GetDeviceRemovedReason() : S_OK; }
  [[nodiscard]] bool Removed() const { return FAILED(RemovedReason()); }
  // Removed, or a request could not be recorded: nothing of it ran but the scene's state says it did (new BLASes, cluster
  // sets' data), and a list that failed its Close never resets (debug36: one effect gone for six minutes, then a GPU
  // reset). main.cpp makes a new scene either way.
  [[nodiscard]] bool Lost() const { return request_failed_ || Removed(); }
  // Lost because a request's wait for this queue's own work ran out on a live device (the game's GPU can be seconds
  // behind): main.cpp keeps the scene until Idle instead of freeing it under that work (the destructor's wait would end
  // the helper), and does not count it as a failed request.
  [[nodiscard]] bool TimedOut() const { return timed_out_; }
  // Nothing submitted can still run (done, or the device removed). Read with no request in flight.
  [[nodiscard]] bool Idle() const { return !own_fence_ || own_fence_->GetCompletedValue() >= own_value_; }

  // Why the device was removed and, from DRED, where: each command list the GPU did not finish, its operations around
  // the one it stopped in (">"; the passes' markers by name), and a page fault's address with the allocations there.
  [[nodiscard]] std::string LostReport() const {
    std::string report = std::format("[TW ray tracing] The GPU removed the device (0x{:08X}).",
                                     static_cast<uint32_t>(device_->GetDeviceRemovedReason()));
    const auto narrow = [](const wchar_t* text) {
      std::string narrowed;
      for (; text != nullptr && *text != L'\0'; ++text) narrowed += static_cast<char>(*text);
      return narrowed;
    };
    ComPtr<ID3D12DeviceRemovedExtendedData1> removed;
    if (FAILED(device_.As(&removed))) return report + " No DRED.";
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
    if (SUCCEEDED(removed->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
      for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode; node != nullptr; node = node->pNext) {
        const uint32_t done = (node->pLastBreadcrumbValue != nullptr) ? *node->pLastBreadcrumbValue : 0u;
        if (done >= node->BreadcrumbCount) continue;
        const auto slot = std::ranges::find_if(slots_, [&](const Slot& slot) { return slot.list.Get() == node->pCommandList; });
        report += std::format("\n  {}{}: {} of {} operations done:", narrow(node->pCommandListDebugNameW),
                              (slot != slots_.end()) ? " (" + slot->request + slot->micromap_builds + ")" : std::string(), done, node->BreadcrumbCount);
        const uint32_t first = (done > 12 ? done - 12 : 0u);
        auto marker = static_cast<size_t>(std::count(node->pCommandHistory, node->pCommandHistory + first, D3D12_AUTO_BREADCRUMB_OP_SETMARKER));
        for (uint32_t op = first; op < std::min(node->BreadcrumbCount, done + 4); ++op) {
          std::string name;
          for (uint32_t context = 0; context < node->BreadcrumbContextsCount; ++context) {
            if (node->pBreadcrumbContexts[context].BreadcrumbIndex == op) name = narrow(node->pBreadcrumbContexts[context].pContextString);
          }
          if (node->pCommandHistory[op] == D3D12_AUTO_BREADCRUMB_OP_SETMARKER && slot != slots_.end() && marker < slot->marks.size()) {
            name = narrow(slot->marks[marker++]);
          }
          if (name.empty()) {
            const auto known = std::ranges::find(OPERATION_NAMES, node->pCommandHistory[op], &OperationName::op);
            name = (known != OPERATION_NAMES.end()) ? known->name : std::format("op {}", static_cast<int>(node->pCommandHistory[op]));
          }
          report += std::format(" {}{}", (op == done ? ">" : ""), name);
        }
      }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT1 fault = {};
    if (SUCCEEDED(removed->GetPageFaultAllocationOutput1(&fault))
        && (fault.PageFaultVA != 0 || fault.pHeadExistingAllocationNode != nullptr || fault.pHeadRecentFreedAllocationNode != nullptr)) {
      report += std::format("\n  page fault at 0x{:X}; there:", fault.PageFaultVA);
      for (const D3D12_DRED_ALLOCATION_NODE1* node = fault.pHeadExistingAllocationNode; node != nullptr; node = node->pNext) {
        report += std::format(" {} ({})", narrow(node->ObjectNameW), static_cast<int>(node->AllocationType));
      }
      report += "; freed lately:";
      for (const D3D12_DRED_ALLOCATION_NODE1* node = fault.pHeadRecentFreedAllocationNode; node != nullptr; node = node->pNext) {
        report += std::format(" {} ({})", narrow(node->ObjectNameW), static_cast<int>(node->AllocationType));
      }
    }
    return report;
  }
  [[nodiscard]] bool Dxr12() const { return dxr12_; }
  // The garbage triangles the scene data left out so far (far off; stretched parts: units, grass, cloth; stretched
  // others) and the last capture that had some (its key).
  struct LeftOut {
    uint32_t far_off = 0;
    uint32_t parts = 0;
    uint32_t others = 0;
    uint64_t key = 0;
  };
  [[nodiscard]] const LeftOut& LeftOutSoFar() const { return left_out_; }
  // The log's lines since the last call: a slow request's report and a garbage triangle's example.
  [[nodiscard]] std::string TakeReports() {
    std::string reports = std::exchange(slow_report_, {});
    if (!garbage_example_.empty()) reports += (reports.empty() ? "" : "\n") + std::exchange(garbage_example_, {});
    if (TestBuffers()) {
      reports += std::format("{}[TW ray tracing] Test: {} buffers made since the last report.", (reports.empty() ? "" : "\n"),
                             buffers_made_.exchange(0, std::memory_order_relaxed));
    }
    return reports;
  }

  // Whether it takes compact draws (v16, rt_compact_ok): 0 not, else one more each time the full set pool dropped sets
  // (the addon sends its sets again).
  [[nodiscard]] uint32_t CompactState() const { return clusters_ ? 1u + set_drops_ : 0u; }

  // Its alpha-tested dynamic corners a request (rt_dynamic_uvs).
  [[nodiscard]] uint32_t DynamicUvs() const { return dynamic_uvs_; }

  // The queue its requests go to (main.cpp's frame boundaries for Nsight Graphics).
  [[nodiscard]] ID3D12CommandQueue* Queue() const { return queue_.Get(); }

  // One request: the frame's captures into BLASes, the TLAS of what it drew, the trace; the queue waits for the addon's
  // fence value and signals the next (also when nothing ran). `cluster_data`: its new cluster sets' data (v16, the
  // second mapping's start). False: nothing traced.
  bool Trace(const dlaa_bridge::Shared& shared, uint32_t version, std::span<const uint8_t> cluster_data) {
    if (!fence_) return false;
    // The GPU harness's RENODX_TW_TEST_REMOVAL: the device removed at the process's third trace, as a GPU hang removes it
    // (main.cpp starts a new one).
    static uint32_t test_traces = 0;
    if (++test_traces == 3 && GetEnvironmentVariableW(L"RENODX_TW_TEST_REMOVAL", nullptr, 0) != 0) {
      if (ComPtr<ID3D12Device5> device5; SUCCEEDED(device_.As(&device5))) device5->RemoveDevice();
    }
    // The addon's profile (RT_PROFILE): its file made again at a recording's first trace.
    const bool profiling = (shared.rt_flags & dlaa_bridge::RT_PROFILE) != 0;
    if (profiling && !profiling_) {
      profile_ = std::ofstream(folder_ / L"tw_rt_profile_helper.csv", std::ios::trunc);
      profile_ << "request,frame,kind,cpu_wait_ms,cpu_record_ms,cpu_submit_ms,dynamic_mtriangles,terrain_mtriangles,grass_mtriangles,"
                  "clusters,templated,compact_draws,sets,new_sets,template_builds,meshes,new_blases,micromaps_built,micromaps_waiting,"
                  "instances,placed,cluster_room,static_blas_mb,gpu_scene_ms,gpu_clusters_ms,gpu_tlas_ms,gpu_trace_ms,gpu_paths_ms,"
                  "gpu_a_ms,gpu_b_ms,gpu_c_ms,gpu_d_ms,gpu_bakes,gpu_bakes_ms,cpu_record_parts\n";
    }
    profiling_ = profiling;
    details_ = profiling || (shared.rt_flags & dlaa_bridge::RT_DETAILS) != 0;
    const size_t slot_index = next_slot_;
    Slot& slot = slots_[slot_index];
    next_slot_ = (next_slot_ + 1) % SLOTS;
    LARGE_INTEGER cpu_start;
    QueryPerformanceCounter(&cpu_start);
    WaitFor(slot.value);
    // The last trace done first: recorded over its run the GPU reset at its sway or micromap passes (ATN's debug36). Three
    // requests a frame bring that about (the same slot); with fewer the traces change slots (Light Bounce off).
    WaitFor(trace_value_);
    LARGE_INTEGER cpu_waited;
    QueryPerformanceCounter(&cpu_waited);
    cpu_marks_.clear();
    // The last trace's placed instances (it is done: its slot's readback).
    if (placed_slot_ != SLOTS) {
      placed_written_ = slots_[placed_slot_].left_out_data[LEFT_OUT_BYTES / 4];
      placed_slot_ = SLOTS;
    }
    // Log Details: the placed instances 60 traces after One Model per Place changed, beside those before (one line a change).
    if (const bool one_at_a_place = (shared.rt_flags & dlaa_bridge::RT_ONE_AT_A_PLACE) != 0; one_at_a_place != placed_rule_) {
      placed_rule_ = one_at_a_place;
      placed_before_rule_ = placed_written_;
      placed_rule_traces_ = 60;
    } else if (placed_rule_traces_ != 0 && --placed_rule_traces_ == 0 && details_) {
      slow_report_ += std::format("{}[TW ray tracing] One Model per Place {}: {} placed instances with those kept, {} before.",
                                  (slow_report_.empty() ? "" : "\n"), (placed_rule_ ? "on" : "off"), placed_written_, placed_before_rule_);
    }
    if (!slot.capture_keys.empty()) {
      const uint32_t* found = slot.left_out_data;
      left_out_.far_off += found[0];
      left_out_.parts += found[1];
      left_out_.others += found[2];
      if (found[3] < slot.capture_keys.size()) {
        left_out_.key = slot.capture_keys[found[3]];
        if (const uint64_t now = GetTickCount64(); now >= garbage_example_time_ + 10000) {
          const auto* example = reinterpret_cast<const float*>(found + 4);
          garbage_example_ = std::format("[TW ray tracing] A garbage triangle (capture {:016X}, flags {}): ({:.6g}, {:.6g}, {:.6g}) ({:.6g}, "
                                         "{:.6g}, {:.6g}) ({:.6g}, {:.6g}, {:.6g}); so far {} far off, {} stretched parts, {} others.",
                                         left_out_.key, slot.capture_flags[found[3]], example[0], example[1], example[2], example[4],
                                         example[5], example[6], example[8], example[9], example[10], left_out_.far_off,
                                         left_out_.parts, left_out_.others);
          garbage_example_time_ = now;
        }
      }
      slot.capture_keys.clear();
    }
    ReadTimes(slot_index);
    slot_traced_[slot_index] = true;
    slot_clustered_[slot_index] = false;
    ExpireHistory(shared.rt_frame);
    const uint64_t completed = own_fence_->GetCompletedValue();
    std::erase_if(releases_, [&](const Release& release) {
      if (release.value > completed) return false;
      if (release.units != 0) {
        // A chunk left empty (but a pool's first) lets its buffer go: a burst of builds keeps no memory.
        BlasChunk& chunk = blas_pools_[release.pool][release.chunk];
        chunk.free.Give(release.offset, release.units);
        if (release.chunk != 0 && chunk.free.free.size() == 1 && chunk.free.free.begin()->second == BLAS_CHUNK_UNITS) chunk.buffer.Reset();
      }
      return true;
    });
    std::erase_if(pending_frees_, [&](const PendingFree& pending) {
      if (pending.value > completed) return false;
      free_records_.push_back(pending.record);
      free_triangles_.Give(pending.normal_offset, pending.triangles);
      if (pending.uv_offset != UINT32_MAX) free_uvs_.Give(pending.uv_offset, pending.triangles * 3);
      if (pending.instances != 0) free_instances_.Give(pending.first_instance, pending.instances);
      return true;
    });
    std::erase_if(pending_set_frees_, [&](const PendingSetFree& pending) {
      if (pending.value > completed) return false;
      if (pending.units != 0) free_set_units_.Give(pending.offset, pending.units);
      if (pending.template_first != UINT32_MAX) free_templates_.Give(pending.template_first, pending.templates);
      return true;
    });
    AdvanceMicromaps(completed);
    gpu_timings_.clear();
    cpu_timings_.clear();
    // A removed device's slots keep their last request and passes (LostReport: the lists the GPU stopped in; debug30's
    // named the request after the loss, whose recording failed).
    const bool reset = !Lost() && SUCCEEDED(slot.allocator->Reset()) && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr));
    if (reset) {
      slot.marks.clear();
      slot.micromap_builds.clear();
      if (details_) {
        slot.request = std::format("trace {}: flags 0x{:X}, sway {} m", own_value_ + 1, shared.rt_flags, (version >= 18) ? shared.rt_sway_range : 0.f);
      }
    }
    // The GPU harness's RENODX_TW_TEST_BAD_LIST: an invalid call in the third trace's list.
    if (reset && test_traces == 3 && GetEnvironmentVariableW(L"RENODX_TW_TEST_BAD_LIST", nullptr, 0) != 0) {
      slot.list->CopyBufferRegion(slot.readback.Get(), 0, slot.sizes.Get(), 0, slot.sizes->GetDesc().Width + sizeof(uint64_t));
    }
    const bool recorded = reset && Record(shared, version, cluster_data, slot_index, &slot);
    if (!recorded) slot.capture_keys.clear();
    if (info_queue_) {
      for (UINT64 index = 0; index < std::min<UINT64>(info_queue_->GetNumStoredMessages(), 20); ++index) {
        SIZE_T bytes = 0;
        info_queue_->GetMessage(index, nullptr, &bytes);
        std::vector<uint8_t> message(bytes);
        if (SUCCEEDED(info_queue_->GetMessage(index, reinterpret_cast<D3D12_MESSAGE*>(message.data()), &bytes))) {
          const auto* found = reinterpret_cast<const D3D12_MESSAGE*>(message.data());
          slow_report_ += std::format("{}[TW ray tracing] D3D12: {}", slow_report_.empty() ? "" : "\n",
                                      std::string_view(found->pDescription, found->DescriptionByteLength));
        }
      }
      info_queue_->ClearStoredMessages();
    }
    LARGE_INTEGER cpu_recorded;
    QueryPerformanceCounter(&cpu_recorded);
    if (details_) {
      cpu_timings_ += std::format(" | CPU ms: wait {:.2f}, record {:.2f}", CpuMs(cpu_start, cpu_waited), CpuMs(cpu_waited, cpu_recorded));
    }
    traced_ = traced_ || recorded;
    if (recorded && (shared.rt_flags & dlaa_bridge::RT_SUN_SHADOWS) != 0) sun_traced_frame_ = shared.rt_frame;
    LARGE_INTEGER submit_start;
    QueryPerformanceCounter(&submit_start);
    queue_->Wait(fence_.Get(), shared.value);
    if (recorded) {
      ID3D12CommandList* lists[] = {slot.list.Get()};
      queue_->ExecuteCommandLists(1, lists);
    }
    queue_->Signal(fence_.Get(), shared.value + 1);
    queue_->Signal(own_fence_.Get(), ++own_value_);
    slot.value = own_value_;
    trace_value_ = own_value_;
    LARGE_INTEGER submit_end;
    QueryPerformanceCounter(&submit_end);
    if (details_) {
      cpu_timings_ += std::format(", submit {:.2f}", CpuMs(submit_start, submit_end));
    }
    // The record's CPU time by pass (each from its mark to the next; "start": before the first).
    slot.profile_parts.clear();
    if (profiling_) {
      LARGE_INTEGER part_start = cpu_waited;
      std::string part = "start";
      cpu_marks_.emplace_back(L"", cpu_recorded);
      for (const auto& [pass, marked] : cpu_marks_) {
        slot.profile_parts += std::format("{}{} {:.3f}", (slot.profile_parts.empty() ? "" : ";"), part, CpuMs(part_start, marked));
        part.assign(pass, pass + std::wcslen(pass));
        part_start = marked;
      }
    }
    slot.profile = profiling_ ? std::format("{},{},trace,{:.3f},{:.3f},{:.3f}{}", own_value_, shared.rt_frame, CpuMs(cpu_start, cpu_waited),
                                            CpuMs(cpu_waited, cpu_recorded), CpuMs(submit_start, submit_end),
                                            recorded ? profile_counts_ : std::string(PROFILE_COUNTS, ','))
                              : std::string();
    return recorded;
  }

  // A request after this frame's trace, against its TLAS (the same queue, so after it): the ray-traced Light Bounce
  // (bridge v7, request 2: rt_bounce.cs_6_5.hlsl; v8 with RT_PATH_TRACING path traced, rt_path.hlsli) or the reflections
  // and specular occlusion (v8, request 3, `reflections`). The radiance cache is filled and resolved at the frame's first
  // such request; last frame's positions are kept for the history. The queue waits for the addon's fence value and
  // signals the next (also when nothing ran). False: not traced.
  bool Paths(const dlaa_bridge::Shared& shared, uint32_t version, bool reflections) {
    if (!fence_) return false;
    const size_t slot_index = next_slot_;
    Slot& slot = slots_[slot_index];
    next_slot_ = (next_slot_ + 1) % SLOTS;
    LARGE_INTEGER cpu_start;
    QueryPerformanceCounter(&cpu_start);
    WaitFor(slot.value);
    LARGE_INTEGER cpu_waited;
    QueryPerformanceCounter(&cpu_waited);
    ReadTimes(slot_index);
    slot.profile_parts.clear();
    slot_traced_[slot_index] = false;
    slot_reflected_[slot_index] = reflections;
    const uint32_t frame = shared.rt_frame;
    const uint32_t requested = (version >= 8) ? shared.rt_path_flags : 0u;
    const bool path_traced = !reflections && (requested & dlaa_bridge::RT_PATH_TRACING) != 0;
    const bool wanted = reflections ? (reflection_ && (requested & (dlaa_bridge::RT_REFLECTIONS | dlaa_bridge::RT_SPECULAR_OCCLUSION)) != 0)
                                    : (light_ && bounce_);
    // A Light Bounce without paths that the reflections follow (RT_PATH_CACHE_FILL): the frame's history, radiance cache and
    // learned lighting here, from its prepared light, as path tracing's request does (at the reflections' request the water's
    // positions and guides have replaced the scene's, and last frame's colour is the upscaler's input: ATN's broken water).
    const bool fills_cache = !reflections && !path_traced && version >= 28 && (requested & dlaa_bridge::RT_PATH_CACHE_FILL) != 0;
    const bool uses_paths = path_traced || reflections || fills_cache;
    bool recorded = wanted && traced_ && !Lost()
                    && (!uses_paths
                        || (EnsureCache() && EnsureHistory(HISTORY_PATHS, frame) && (!reflections || EnsureHistory(HISTORY_SPECULAR, frame))))
                    && SUCCEEDED(slot.allocator->Reset()) && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr));
    if (recorded) {
      // (A removed device's slots keep their last request and passes: LostReport.)
      slot.marks.clear();
      if (details_) {
        slot.request = std::format("{} {}: flags 0x{:X}, paths 0x{:X}", reflections ? "reflections" : "Light Bounce", own_value_ + 1, shared.rt_flags,
                                   requested);
      }
      auto* list = slot.list.Get();
      StampSlot(list, slot_index);
      Stamp(list, slot_index * STAMPS + 4);
      const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
      ID3D12DescriptorHeap* heaps[] = {descriptors_.Get()};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootSignature(root_signature_.Get());
      list->SetComputeRootDescriptorTable(ROOT_TABLE, descriptors_->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootShaderResourceView(ROOT_SCENE, (ptlas_ != nullptr ? ptlas_ : tlas_.Get())->GetGPUVirtualAddress());
      // Unused root descriptors still point at a buffer.
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot.table->GetGPUVirtualAddress());
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }
      const float bounce_range = std::clamp(shared.rt_bounce_range, 0.5f, 200.05565f);
      // The Denoiser setting at NRD: REBLUR on this request's light (the Light Bounce at its texels' size; reflections
      // only with the G-buffer's normals and roughness).
      bool nrd_light = false;
      bool nrd_light_ran = false;
#if defined(RENODX_NRD)
      NrdSignal* nrd_signal = nullptr;
      if ((requested & dlaa_bridge::RT_PATH_NRD) != 0) {
        if (reflections) {
          static constexpr std::array<NrdTexture, 4> TEXTURES = {{
              {nrd::ResourceType::IN_VIEWZ, DXGI_FORMAT_R32_FLOAT, NRD_REFLECT_VIEW_Z},
              {nrd::ResourceType::IN_NORMAL_ROUGHNESS, DXGI_FORMAT_R10G10B10A2_UNORM, NRD_REFLECT_NORMAL_ROUGHNESS},
              {nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, DXGI_FORMAT_R16G16B16A16_FLOAT, NRD_REFLECT_SIGNAL},
              {nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, DXGI_FORMAT_R16G16B16A16_FLOAT, 0},
          }};
          if (guide_normals_ && shared.rt_guides != 0 && (requested & dlaa_bridge::RT_REFLECTIONS) != 0
              && EnsureNrd(&nrd_reflections_, nrd::Denoiser::REBLUR_SPECULAR, "NRD REBLUR (reflections)", {RT_NRD_FRONT_REFLECT, sizeof(RT_NRD_FRONT_REFLECT)},
                           {RT_NRD_FINISH_REFLECT, sizeof(RT_NRD_FINISH_REFLECT)}, static_cast<uint32_t>(visibility_->GetDesc().Width), visibility_->GetDesc().Height, TEXTURES, frame)) {
            nrd_signal = &nrd_reflections_;
          }
        } else {
          static constexpr std::array<NrdTexture, 4> TEXTURES = {{
              {nrd::ResourceType::IN_VIEWZ, DXGI_FORMAT_R32_FLOAT, NRD_BOUNCE_VIEW_Z},
              {nrd::ResourceType::IN_NORMAL_ROUGHNESS, DXGI_FORMAT_R10G10B10A2_UNORM, NRD_BOUNCE_NORMAL_ROUGHNESS},
              {nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, DXGI_FORMAT_R16G16B16A16_FLOAT, NRD_BOUNCE_SIGNAL},
              {nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, DXGI_FORMAT_R16G16B16A16_FLOAT, 0},
          }};
          if (!nrd_bounce_hit_distance_ && Texture(DXGI_FORMAT_R16_FLOAT, &nrd_bounce_hit_distance_)) {
            Written(nrd_bounce_hit_distance_.Get(), DXGI_FORMAT_R16_FLOAT, NRD_BOUNCE_HIT_DISTANCE);
          }
          if (nrd_bounce_hit_distance_
              && EnsureNrd(&nrd_bounce_, nrd::Denoiser::REBLUR_DIFFUSE, "NRD REBLUR (Light Bounce)", {RT_NRD_FRONT_BOUNCE, sizeof(RT_NRD_FRONT_BOUNCE)},
                           {RT_NRD_FINISH_BOUNCE, sizeof(RT_NRD_FINISH_BOUNCE)}, shared.rt_bounce_width, shared.rt_bounce_height, TEXTURES, frame)) {
            nrd_signal = &nrd_bounce_;
          }
        }
        nrd_light = nrd_signal != nullptr;
      }
#endif
      const std::array<uint32_t, 12> settings = {
          frame,
          (!reflections && nrd_light) ? TRACE_NRD_BOUNCE : 0u,
          std::bit_cast<uint32_t>(bounce_range),
          std::clamp(shared.rt_bounce_rays, 1u, 32u),
          shared.rt_width,
          shared.rt_height,
          0u,
          1u,
      };
      std::array<uint32_t, 20> bounce = {};
      std::memcpy(bounce.data(), shared.rt_view_projection, sizeof(shared.rt_view_projection));
      bounce[16] = shared.rt_bounce_width;
      bounce[17] = shared.rt_bounce_height;
      bounce[18] = std::max(shared.rt_bounce_scale, 1u);
      list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(settings.size()), settings.data(), 0);
      list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(bounce.size()), bounce.data(), 0);
      std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> textures = {
          {positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
          {sun_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
      };
      if (light_) textures.emplace_back(light_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      if (!reflections) textures.emplace_back(bounce_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (atlas_) textures.emplace_back(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      // Paths: last frame's colour (still the DLAA input), the guides, the frame's sun visibility, the reflections'.
      const bool guides = guide_normals_ && version >= 8 && shared.rt_guides != 0;
      if (uses_paths) {
        textures.emplace_back(visibility_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (previous_color_) textures.emplace_back(previous_color_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (guides) {
          for (ID3D12Resource* guide : {guide_normals_.Get(), guide_albedo_.Get(), guide_roughness_.Get()}) {
            textures.emplace_back(guide, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
          }
        }
        if (reflections) {
          textures.emplace_back(reflection_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          textures.emplace_back(specular_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          if (hit_distance_) textures.emplace_back(hit_distance_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
      }
      std::vector<D3D12_RESOURCE_BARRIER> barriers;
      for (const auto& [resource, used] : textures) barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
#if defined(RENODX_NRD)
      if (nrd_signal != nullptr) nrd_signal->denoiser->ToWritten(list);
#endif

      if (!uses_paths) {
        Mark(list, L"bounce");
        list->SetPipelineState(bounce_pipeline_.Get());
        list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        for (const size_t part : {size_t{0}, size_t{1}, size_t{2}}) ProfileStamp(list, slot_index, part);
      } else {
        // The history at the frame's first request: last frame's positions (copied then) kept, this frame's copied.
        const uint32_t width = std::max(shared.rt_width, 1u);
        const uint32_t height = std::max(shared.rt_height, 1u);
        const bool first_paths_request = frame != paths_frame_;
        if (first_paths_request) {
          history_valid_ = copied_frame_ != UINT32_MAX && copied_frame_ + 1 == frame && previous_color_;
          // The slice that took the last copy is last frame's now, the other takes this frame's: one copy a frame (the two
          // textures before took two, 0.15 ms of a 4K frame each).
          previous_positions_slice_ = copied_slice_;
          copied_slice_ = 1u - copied_slice_;
          const D3D12_BOX region = {.left = 0, .top = 0, .front = 0, .right = width, .bottom = height, .back = 1};
          std::array<D3D12_RESOURCE_BARRIER, 2> to_copy = {
              Transition(kept_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
              Transition(positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
          };
          to_copy[0].Transition.Subresource = copied_slice_;
          list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
          const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = kept_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                  .SubresourceIndex = copied_slice_};
          const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
          list->CopyTextureRegion(&to, 0, 0, 0, &from, &region);
          for (auto& barrier : to_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
          list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
          std::copy_n(current_view_projection_.begin(), 16, previous_view_projection_.begin());
          std::copy_n(shared.rt_view_projection, 16, current_view_projection_.begin());
          copied_frame_ = frame;
          paths_frame_ = frame;
        }
        // Softer Water Reflections (RT_REFLECTIONS_WATER_POSITIONS): the addon wrote the water's surface over the positions
        // after the frame's first paths request took them (the riverbed's: deep water failed every reflection history test).
        // Last frame's at this request go over the slice the earlier requests read (the next frame's copy takes that slice),
        // and this frame's are kept for the next: two copies at the render size (about 0.15 ms each at 4K).
        if (reflections && !first_paths_request && (requested & dlaa_bridge::RT_REFLECTIONS_WATER_POSITIONS) != 0 && water_frame_ != frame
            && EnsureHistory(HISTORY_WATER, frame)) {
          const D3D12_BOX region = {.left = 0, .top = 0, .front = 0, .right = width, .bottom = height, .back = 1};
          const D3D12_TEXTURE_COPY_LOCATION water = {.pResource = water_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
          if (water_frame_ != UINT32_MAX && water_frame_ + 1 == frame) {
            std::array<D3D12_RESOURCE_BARRIER, 2> to_previous_copy = {
                Transition(kept_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
                Transition(water_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
            };
            to_previous_copy[0].Transition.Subresource = previous_positions_slice_;
            list->ResourceBarrier(static_cast<UINT>(to_previous_copy.size()), to_previous_copy.data());
            const D3D12_TEXTURE_COPY_LOCATION previous = {.pResource = kept_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                          .SubresourceIndex = previous_positions_slice_};
            list->CopyTextureRegion(&previous, 0, 0, 0, &water, &region);
            for (auto& barrier : to_previous_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(static_cast<UINT>(to_previous_copy.size()), to_previous_copy.data());
          }
          std::array<D3D12_RESOURCE_BARRIER, 2> to_water_copy = {
              Transition(water_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
              Transition(positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
          };
          list->ResourceBarrier(static_cast<UINT>(to_water_copy.size()), to_water_copy.data());
          const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
          list->CopyTextureRegion(&water, 0, 0, 0, &from, &region);
          for (auto& barrier : to_water_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
          list->ResourceBarrier(static_cast<UINT>(to_water_copy.size()), to_water_copy.data());
          water_frame_ = frame;
        }
        auto to_previous = Transition(kept_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        to_previous.Transition.Subresource = previous_positions_slice_;
        list->ResourceBarrier(1, &to_previous);
        if (!reflections) light_frame_ = frame;
        uint32_t flags = 0;
        if (path_traced) flags |= PATH_TRACING;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS) != 0) flags |= PATH_REFLECTIONS;
        if (reflections && (requested & dlaa_bridge::RT_SPECULAR_OCCLUSION) != 0) flags |= PATH_SPECULAR_OCCLUSION;
        if (history_valid_) flags |= PATH_HISTORY_VALID;
        if (cache_filled_) flags |= PATH_CACHE_VALID;
        // Ray Reconstruction denoises the frame: the reflections' hit distance for it (their history and filter stay: RR
        // alone left them noisy, ATN's debug33).
        const bool raw = reflections && (requested & dlaa_bridge::RT_REFLECTIONS_RAW) != 0;
        if (raw) flags |= PATH_RAW;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS_SQUARED_LOBE) != 0) flags |= PATH_SQUARED_LOBE;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS_FOG) != 0) flags |= PATH_REFLECT_FOG;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS_STEADY) != 0) flags |= PATH_REFLECT_STEADY;
        if (nrd_light) flags |= PATH_NRD;
        if (reflections && specular_history_valid_ && specular_frame_ + 1 == frame) flags |= PATH_SPECULAR_HISTORY;
        // Reflection Reuse from ReSTIR GI (v19): this frame's reservoirs (the Light Bounce's request before this one used set
        // reuse_parity_ then and turned it).
        if (reflections && version >= 19 && (requested & dlaa_bridge::RT_REFLECTIONS_REUSE) != 0 && reuse_frame_ == frame) {
          flags |= PATH_REFLECT_REUSE | (reuse_parity_ == 0 ? PATH_REUSE_PARITY : 0u);
        }
        // Own ReSTIR (v19, helpers 55 on): the reflections' own reservoirs, made once a frame (set reflect_parity_, then
        // turned; a second request that frame takes them as made); last frame's when made at this size.
        const bool reflect_own = reflections && guides && (flags & PATH_REFLECTIONS) != 0 && version >= 19
                                 && (requested & dlaa_bridge::RT_REFLECTIONS_OWN) != 0 && EnsureHistory(HISTORY_REFLECT, frame);
        const bool reflect_made = reflect_own && reflect_frame_ == frame;
        if (reflect_own) {
          const uint32_t reflect_set = (reflect_made ? (reflect_parity_ ^ 1u) : reflect_parity_);
          flags |= PATH_REFLECT_OWN | (reflect_set != 0 ? PATH_REFLECT_PARITY : 0u);
          if (!reflect_made && history_valid_ && reflect_frame_ != UINT32_MAX && reflect_frame_ + 1 == frame
              && reflect_size_ == std::array<uint32_t, 2>{width, height}) {
            flags |= PATH_REFLECT_OWN_HISTORY;
          }
        }
        if (guides) flags |= PATH_GUIDES;
        if (sun_traced_frame_ == frame) flags |= PATH_SUN_TRACED;
        if ((shared.rt_flags & dlaa_bridge::RT_START_PAST) != 0) flags |= PATH_START_PAST;
        // A reflections request's light buffer (RT_REFLECTIONS_LIGHT): what their hits read; without the paths' prepared
        // light this frame the radiance cache's too, at the render pixels.
        const bool final_light = reflections && light_ && (requested & dlaa_bridge::RT_REFLECTIONS_LIGHT) != 0;
        if (light_frame_ == frame || final_light) flags |= PATH_LIGHT;
        if (final_light) flags |= PATH_LIGHT_FINAL;
        const bool render_pixels = final_light && light_frame_ != frame;
        // ReSTIR GI (RENODX_TW_NO_REUSE: off); last frame's reservoirs when it ran with these texels.
        const std::array<uint32_t, 3> reuse_size = {shared.rt_bounce_width, shared.rt_bounce_height, std::max(shared.rt_bounce_scale, 1u)};
        const bool reuse = path_traced && (requested & dlaa_bridge::RT_PATH_REUSE) != 0
                           && GetEnvironmentVariableW(L"RENODX_TW_NO_REUSE", nullptr, 0) == 0 && EnsureHistory(HISTORY_REUSE, frame);
        const bool light_was_kept = path_ambient_frame_ != UINT32_MAX && path_ambient_frame_ + 2 >= frame;
        if (path_traced && (requested & dlaa_bridge::RT_PATH_AMBIENT) != 0) {
          flags |= PATH_AMBIENT;
          if ((requested & dlaa_bridge::RT_PATH_SKY_APART) != 0) flags |= PATH_SKY_APART;
          path_ambient_frame_ = frame;
          path_kept_cube_ = (requested & dlaa_bridge::RT_PATH_KEPT_CUBE) != 0;
          path_sun_light_ = (requested & dlaa_bridge::RT_PATH_SUN_LIGHT) != 0;
          if (!light_was_kept) kept_copy_wanted_ = true;
        }
        // The learned lighting stays while the game's ambient is away (also through the reflections' requests: the radiance
        // cache's resolve, once a frame, can be theirs).
        if (path_ambient_frame_ != UINT32_MAX && path_ambient_frame_ + 2 >= frame) {
          flags |= PATH_KEEP_LIGHT | (path_kept_cube_ ? PATH_KEEP_CUBE : 0u) | (path_sun_light_ ? PATH_SUN_LIGHT : 0u);
        }
        // What the light kept at a freeze was (Path-Traced AO's frames after it read this): its copy done once a slot's list has come round.
        if (kept_copy_frame_ != UINT32_MAX && frame > kept_copy_frame_ + SLOTS) {
          const D3D12_RANGE read = {.Begin = 0, .End = sizeof(std::array<float, 4>) * LIGHT_KEPT_TEXELS};
          void* mapped = nullptr;
          if (SUCCEEDED(kept_readback_->Map(0, &read, &mapped))) {
            std::array<std::array<float, 4>, LIGHT_KEPT_TEXELS> kept = {};
            std::memcpy(kept.data(), mapped, sizeof(kept));
            const D3D12_RANGE written = {};
            kept_readback_->Unmap(0, &written);
            const auto& [shade, sunlit, scale, base, ambient] = kept;
            slow_report_ += std::format("{}[TW ray tracing] Path-Traced AO keeps its learned light: shade {:.4f} {:.4f} {:.4f}, the game's ambient {:.4f} {:.4f} {:.4f}, share {:.3f} {:.3f} {:.3f}, sun {:.3f} {:.3f} {:.3f}, base colour {:.3f}.",
                                        slow_report_.empty() ? "" : "\n", shade[0], shade[1], shade[2], ambient[0], ambient[1], ambient[2],
                                        shade[0] / std::max(ambient[0], 1e-4f), shade[1] / std::max(ambient[1], 1e-4f), shade[2] / std::max(ambient[2], 1e-4f),
                                        sunlit[0], sunlit[1], sunlit[2], (base[0] + base[1] + base[2]) / 3.f);
          }
          kept_copy_frame_ = UINT32_MAX;
        }
        if (reuse) {
          flags |= PATH_REUSE | (reuse_parity_ != 0 ? PATH_REUSE_PARITY : 0u);
          if ((requested & dlaa_bridge::RT_PATH_RECONSTRUCTED) != 0) flags |= PATH_PERMUTE;
          if (history_valid_ && reuse_frame_ != UINT32_MAX && reuse_frame_ + 1 == frame && reuse_size_ == reuse_size) {
            flags |= PATH_REUSE_HISTORY;
          }
        }
        // Path-Traced Ambient's own sky at the bounce texels, with the game's sun light (rt_path.hlsli ScreenSky): this frame's
        // paths write set sky_parity_ (turned after them); the light the screen's surfaces send back reads the other, the
        // last paths', at these texels (a reflections request's at the render pixels: none).
        const std::array<uint32_t, 3> sky_size = {render_pixels ? width : shared.rt_bounce_width, render_pixels ? height : shared.rt_bounce_height,
                                                  render_pixels ? 1u : std::max(shared.rt_bounce_scale, 1u)};
        const bool sky_written = path_traced && (flags & PATH_AMBIENT) != 0 && (flags & PATH_SUN_LIGHT) != 0 && EnsureHistory(HISTORY_SKY, frame);
        if (own_sky_[0] && (flags & PATH_SUN_LIGHT) != 0) {
          if (sky_parity_ != 0) flags |= PATH_SKY_PARITY;
          if (history_valid_ && sky_frame_ != UINT32_MAX && sky_frame_ + 1 >= frame && sky_size_ == sky_size) flags |= PATH_SKY_HISTORY;
        }
        // The local lights with their colours (v11): what the paths meet unseen by the screen lit by them too.
        const uint32_t light_count = ((shared.rt_flags & dlaa_bridge::RT_LOCAL_LIGHTS) != 0) ? SharedLightCount(shared, version) : 0u;
        if (light_count != 0) {
          CopyLights(shared, light_count, slot.light_data);
          flags |= PATH_LOCAL_LIGHTS;
        }
        list->SetComputeRootConstantBufferView(ROOT_LIGHTS, slot.lights->GetGPUVirtualAddress());
        const uint32_t lights_test = LightsInBounceTest();
        const uint32_t lights_asked = ((requested & dlaa_bridge::RT_PATH_LIGHTS_IN_BOUNCE) != 0)
                                          ? (((requested & dlaa_bridge::RT_PATH_LIGHT_RATIO) != 0) ? 3u : std::max(lights_test, 1u))
                                          : lights_test;
        const bool lights_possible = reuse_lights_pipeline_ && path_traced && reuse && light_count != 0;
        const bool ratio_written = lights_possible && lights_asked == 3u && EnsureHistory(HISTORY_RATIO, frame);
        const uint32_t lights_mode = (lights_possible && (lights_asked != 3u || ratio_written)) ? lights_asked : 0u;
        // Their soft shadows under Ray Reconstruction: the lights' radius (rt_path.hlsli ReuseTexel's rays to their disk).
        if (lights_mode != 0 && (requested & dlaa_bridge::RT_PATH_RECONSTRUCTED) != 0) {
          list->SetComputeRoot32BitConstant(ROOT_SETTINGS, std::bit_cast<uint32_t>(std::clamp(shared.rt_light_size, 0.f, 2.f)), 8);
        }
        PathConstants constants = {
            .bounce_width = render_pixels ? width : shared.rt_bounce_width,
            .bounce_height = render_pixels ? height : shared.rt_bounce_height,
            .bounce_scale = render_pixels ? 1u : std::max(shared.rt_bounce_scale, 1u),
            .flags = flags,
            .bounces = std::clamp(shared.rt_path_bounces, 0u, 8u),
            .samples = std::clamp(shared.rt_path_samples, 1u, 16u),
            .bounce_range = bounce_range,
            .reflection_range = std::clamp(shared.rt_reflection_range, 0.5f, 200.02019f),
            .sky_range = std::clamp(shared.rt_ambient_range, 0.1f, 100.000854f),
            .previous_square_root = (version >= 28 && (requested & dlaa_bridge::RT_PATH_COLOR_SQUARE_ROOT) != 0) ? 1u : 0u,
            .previous_positions_slice = previous_positions_slice_,
            // Torch and Fire Light in the Light Bounce: ReuseTexel shades the lights' samples (the paths with ReSTIR GI), or
            // keeps their shadow ratio (ratio_written below).
            .lights_in_bounce = lights_mode | ((ratio_written && ratio_parity_ != 0) ? 16u : 0u)
                                | ((ratio_written && history_valid_ && ratio_frame_ != UINT32_MAX && ratio_frame_ + 1 == frame && ratio_size_ == reuse_size) ? 32u : 0u),
        };
        std::copy_n(shared.rt_view_projection, 16, constants.view_projection.begin());
        std::copy_n(previous_view_projection_.begin(), 16, constants.previous_view_projection.begin());
        std::memcpy(slot.paths_data, &constants, sizeof(constants));
        list->SetComputeRootConstantBufferView(ROOT_PATHS, slot.paths->GetGPUVirtualAddress());
        // The radiance cache once a frame: filled (from the prepared light, else last frame's colour), resolved.
        if (cache_frame_ != frame) {
          const bool from_light = (flags & PATH_LIGHT) != 0;
          const uint32_t fill_width = (from_light ? constants.bounce_width : width) / 2 + 1;
          const uint32_t fill_height = (from_light ? constants.bounce_height : height) / 2 + 1;
          Mark(list, L"radiance cache");
          list->SetPipelineState(cache_fill_pipeline_.Get());
          list->Dispatch((fill_width + 7) / 8, (fill_height + 7) / 8, 1);
          list->ResourceBarrier(1, &uav_barrier);
          list->SetPipelineState(cache_resolve_pipeline_.Get());
          list->Dispatch(CACHE_ENTRIES / 64, 1, 1);
          list->ResourceBarrier(1, &uav_barrier);
          if (kept_copy_wanted_ && kept_readback_) {
            const auto to_copy = Transition(light_kept_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1, &to_copy);
            list->CopyBufferRegion(kept_readback_.Get(), 0, light_kept_.Get(), 0, sizeof(std::array<float, 4>) * LIGHT_KEPT_TEXELS);
            const auto copied = Transition(light_kept_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->ResourceBarrier(1, &copied);
            kept_copy_wanted_ = false;
            kept_copy_frame_ = frame;
          }
          cache_frame_ = frame;
          cache_filled_ = true;
        }
        // SHaRC Radiance Cache (bridge v18, with Path Tracing): its buffers at u6-u8, its update (a bounce texel of each
        // SHARC_BLOCK x SHARC_BLOCK) and resolve, then the paths read it (their SHaRC variant). Not the reflections: their
        // SHaRC variant hung the GPU at 4K in the harness (the 5090).
        bool sharc = sharc_path_pipeline_ && path_traced && version >= 18 && (requested & dlaa_bridge::RT_PATH_SHARC) != 0;
        for (size_t index = 0; index < sharc_buffers_.size() && sharc; ++index) {
          sharc = GrowBuffer(&sharc_buffers_[index], uint64_t{SHARC_ENTRIES} * SHARC_STRIDES[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        if (sharc) {
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, sharc_buffers_[0]->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, sharc_buffers_[1]->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, sharc_buffers_[2]->GetGPUVirtualAddress());
          if (sharc_frame_ != frame) {
            const uint32_t blocks_x = (shared.rt_bounce_width + SHARC_BLOCK - 1) / SHARC_BLOCK;
            const uint32_t blocks_y = (shared.rt_bounce_height + SHARC_BLOCK - 1) / SHARC_BLOCK;
            Mark(list, L"SHaRC");
            list->SetPipelineState(sharc_update_pipeline_.Get());
            list->Dispatch((blocks_x + 7) / 8, (blocks_y + 7) / 8, 1);
            list->ResourceBarrier(1, &uav_barrier);
            list->SetPipelineState(sharc_resolve_pipeline_.Get());
            list->Dispatch(SHARC_ENTRIES / 64, 1, 1);
            list->ResourceBarrier(1, &uav_barrier);
            sharc_frame_ = frame;
          }
        }
        ProfileStamp(list, slot_index, 0);
        Mark(list, L"path tracing");
        // With Path Tracing SER (bridge v18) the ray generation shaders.
        const bool ser = path_ser_ && version >= 18 && (requested & dlaa_bridge::RT_PATH_SER) != 0;
        const auto dispatch_rays = [&](uint64_t record, uint32_t ray_width, uint32_t ray_height) {
          list->SetPipelineState1(path_ser_.Get());
          const D3D12_DISPATCH_RAYS_DESC rays = {
              .RayGenerationShaderRecord = {.StartAddress = path_ser_table_->GetGPUVirtualAddress() + record,
                                            .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
              .Width = ray_width,
              .Height = ray_height,
              .Depth = 1,
          };
          list->DispatchRays(&rays);
        };
        if (path_traced && sharc) {
          list->SetPipelineState(sharc_path_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        } else if (path_traced && ser) {
          dispatch_rays(0, shared.rt_bounce_width, shared.rt_bounce_height);
        } else if (path_traced) {
          list->SetPipelineState(path_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        } else if (fills_cache) {
          Mark(list, L"bounce");
          list->SetPipelineState(bounce_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        }
        if (sky_written) {
          sky_frame_ = frame;
          sky_parity_ ^= 1u;
          sky_size_ = sky_size;
        }
        ProfileStamp(list, slot_index, 1);
        if (reuse) {
          list->ResourceBarrier(1, &uav_barrier);
          Mark(list, L"reuse");
          // Lean ReSTIR Reuse (RT_PATH_REUSE_LEAN, or RENODX_TW_REUSE_LEAN for the harness): the neighbours with fewer
          // fetches (rt_path.hlsli ReuseTexel).
          const bool lean = reuse_lean_pipeline_
                            && ((version >= 31 && (requested & dlaa_bridge::RT_PATH_REUSE_LEAN) != 0)
                                || GetEnvironmentVariableW(L"RENODX_TW_REUSE_LEAN", nullptr, 0) != 0);
          list->SetPipelineState((lights_mode != 0 ? reuse_lights_pipeline_ : (lean ? reuse_lean_pipeline_ : reuse_pipeline_)).Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
          reuse_frame_ = frame;
          reuse_lights_mode_ = lights_mode;
          reuse_parity_ ^= 1u;
          reuse_size_ = reuse_size;
          if (ratio_written) {
            ratio_frame_ = frame;
            ratio_parity_ ^= 1u;
            ratio_size_ = reuse_size;
          }
        }
        ProfileStamp(list, slot_index, 2);
        if (reflect_own && !reflect_made) {
          Mark(list, L"reflection reservoirs");
          list->SetPipelineState(reflect_reservoir_pipeline_.Get());
          list->Dispatch(((width + 1) / 2 + 7) / 8, ((height + 1) / 2 + 7) / 8, 1);
          list->ResourceBarrier(1, &uav_barrier);
          reflect_frame_ = frame;
          reflect_parity_ ^= 1u;
          reflect_size_ = {width, height};
        }
        if (reflections) {
          Mark(list, L"reflections");
          if (ser) {
            dispatch_rays(PATH_SER_RECORD, width, height);
          } else {
            list->SetPipelineState(reflect_pipeline_.Get());
            list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
          }
          list->ResourceBarrier(1, &uav_barrier);
          list->SetPipelineState(reflect_filter_pipeline_.Get());
          list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
          specular_frame_ = frame;
          specular_history_valid_ = true;
        }
        // Unused root descriptors still point at a buffer.
        if (sharc) {
          for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
            list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
          }
        }
        std::swap(to_previous.Transition.StateBefore, to_previous.Transition.StateAfter);
        list->ResourceBarrier(1, &to_previous);
      }
#if defined(RENODX_NRD)
      if (nrd_signal != nullptr) {
        // Its inputs (on this list's bindings), then REBLUR (on its own: nothing below needs these).
        const uint32_t nrd_width = reflections ? std::max(shared.rt_width, 1u) : shared.rt_bounce_width;
        const uint32_t nrd_height = reflections ? std::max(shared.rt_height, 1u) : shared.rt_bounce_height;
        list->ResourceBarrier(1, &uav_barrier);
        Mark(list, L"NRD inputs");
        list->SetPipelineState(nrd_signal->front.Get());
        list->Dispatch((nrd_width + 7) / 8, (nrd_height + 7) / 8, 1);
        list->ResourceBarrier(1, &uav_barrier);
        Mark(list, L"NRD REBLUR");
        const nrd::ReblurSettings reblur = {};
        nrd_light_ran = nrd_signal->denoiser->Run(list, static_cast<UINT>(slot_index), shared.rt_view_projection, frame, nrd_width, nrd_height, &reblur);
        if (nrd_light_ran) {
          ID3D12Resource* const output = reflections ? reflection_.Get() : bounce_.Get();
          nrd_signal->denoiser->Finish(list, reflections ? nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST : nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, positions_.Get(),
                                       POSITIONS_VIEW, output, DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
      }
#endif
      if ((requested & dlaa_bridge::RT_PATH_NRD) != 0) {
        NoteNrd(reflections ? dlaa_bridge::RT_NRD_OFF_REFLECTIONS : dlaa_bridge::RT_NRD_OFF_BOUNCE, nrd_light_ran);
      }
      barriers.clear();
      for (const auto& [resource, used] : textures) barriers.push_back(Transition(resource, used, D3D12_RESOURCE_STATE_COMMON));
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      Stamp(list, slot_index * STAMPS + 5);
      Resolve(list, slot_index);
      recorded = SUCCEEDED(list->Close());
      if (!recorded) {
        request_failed_ = true;
      }
    }
    LARGE_INTEGER submit_start;
    QueryPerformanceCounter(&submit_start);
    queue_->Wait(fence_.Get(), shared.value);
    if (recorded) {
      ID3D12CommandList* lists[] = {slot.list.Get()};
      queue_->ExecuteCommandLists(1, lists);
    }
    queue_->Signal(fence_.Get(), shared.value + 1);
    queue_->Signal(own_fence_.Get(), ++own_value_);
    slot.value = own_value_;
    LARGE_INTEGER submit_end;
    QueryPerformanceCounter(&submit_end);
    slot.profile = profiling_ ? std::format("{},{},{},{:.3f},{:.3f},{:.3f}{}", own_value_, frame, reflections ? "reflections" : "bounce",
                                            CpuMs(cpu_start, cpu_waited), CpuMs(cpu_waited, submit_start), CpuMs(submit_start, submit_end),
                                            std::string(PROFILE_COUNTS, ','))
                              : std::string();
    return recorded;
  }

  ~RtScene12() {
    WaitForGpuBeforeRelease(own_fence_.Get(), event_, own_value_, FENCE_TIMEOUT, "ray tracing");
    if (event_ != nullptr) CloseHandle(event_);
    for (auto& baking : micromap_baking_) {
      if (baking.valid()) baking.wait();
    }
    if (placed_growing_.valid()) placed_growing_.wait();
#if defined(RENODX_NVAPI_CLUSTERS)
    if (cluster_growing_.valid()) cluster_growing_.wait();
#endif
#if defined(RENODX_OMM_SDK)
    for (const ommBaker baker : omm_.bakers) {
      if (baker != nullptr) omm_.destroy_baker(baker);
    }
    if (omm_.gpu_pipeline != nullptr) omm_.gpu_destroy_pipeline(omm_.gpu_baker, omm_.gpu_pipeline);
    if (omm_.gpu_baker != nullptr) omm_.destroy_baker(omm_.gpu_baker);
    // omm-lib.dll stays loaded: the CPU bakes' OpenMP threads idle in the runtime it brought, unloading it under them crashed
    // the helper at the second GPU reset (the first has no CPU bakes before it).
#endif
  }

 private:
  static constexpr UINT AGILITY_SDK_VERSION = 619;
  static constexpr GUID SDK_CONFIGURATION = {0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}};
  static constexpr GUID DRED_CONFIGURATION = {0x4a75bbc4, 0x9ff4, 0x4ad8, {0x9f, 0x18, 0xab, 0xae, 0x84, 0xdc, 0x5f, 0xf2}};
  static constexpr GUID DEBUG_CONFIGURATION = {0xf2352aeb, 0xdd84, 0x49fe, {0xb9, 0x7b, 0xa9, 0xdc, 0xfd, 0xcc, 0x1b, 0x4f}};  // CLSID_D3D12Debug
  // DRED's operations as LostReport names them (the ones recorded here).
  struct OperationName {
    D3D12_AUTO_BREADCRUMB_OP op;
    const char* name;
  };
  static constexpr std::array<OperationName, 12> OPERATION_NAMES = {{
      {D3D12_AUTO_BREADCRUMB_OP_SETMARKER, "marker"},
      {D3D12_AUTO_BREADCRUMB_OP_DISPATCH, "Dispatch"},
      {D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS, "DispatchRays"},
      {D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE, "BuildAS"},
      {D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO, "EmitASInfo"},
      {D3D12_AUTO_BREADCRUMB_OP_COPYRAYTRACINGACCELERATIONSTRUCTURE, "CopyAS"},
      {D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION, "CopyBufferRegion"},
      {D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION, "CopyTextureRegion"},
      {D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER, "Barrier"},
      {D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA, "ResolveQueryData"},
      {D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT, "ExecuteIndirect"},
      {D3D12_AUTO_BREADCRUMB_OP_EXECUTEEXTENSIONCOMMAND, "ExtensionCommand"},
  }};
  static constexpr size_t SLOTS = 3;
  // A slot's GPU timestamps: a trace's start, scene, TLAS, end, its clusters' start and end (a paths request's start and
  // end at 4, 5), then the profile's (RT_PROFILE) parts' ends: a trace's sun, AO, local lights (fog after); a paths
  // request's radiance cache (or the Light Bounce's rays), paths, reuse (reflections after).
  static constexpr size_t STAMPS = 10;
  static constexpr uint32_t TRACE_PART_SUN = 1u << 30;  // rt_trace.cs_6_5.hlsl PART_*
  static constexpr uint32_t TRACE_PART_AMBIENT = 1u << 31;
  // The CPU's instances at most, and the placed ones' room at first (32 MB of descs; grass past its share is placed, a
  // patch an instance: ATN's second profile placed up to 126 K; 524,288 since 262,144 ran out in ATN's 2026-11-01 stress
  // battle: 70-184 K placed a frame, the kept ones past the limit left out, others each frame: shadows flickering).
  static constexpr UINT MAX_INSTANCES = 524288;
  static constexpr UINT TRANSFER_VERTICES = dlaa_bridge::RT_TRANSFER_WIDTH * dlaa_bridge::RT_TRANSFER_HEIGHT;
  // Meshes drawn within this many frames cast (but the terrain only in frames that draw it); unseen longer: released.
  static constexpr uint32_t RECENT_FRAMES = 300;
  static constexpr uint32_t RELEASE_FRAMES = 3600;
  // The units' BLAS is refitted to each frame's poses while their draws stay the same, rebuilt this often.
  static constexpr uint32_t UNITS_REBUILD_FRAMES = 30;
  static constexpr UINT DYNAMIC_TRIANGLES = dlaa_bridge::RT_DYNAMIC_TRIANGLES;
  // Mega Geometry: the units' captured triangles in clusters of this many (three vertices each, up to 256 a cluster), a
  // frame's clusters at most (the compact draws' first; the last of each capture partial), each a geometry with a record
  // of its own (a slot's in turn).
  static constexpr uint32_t CLUSTER_TRIANGLES = dlaa_bridge::RT_CAPTURE_CLUSTER_TRIANGLES;
  static constexpr uint32_t MAX_CLUSTERS = dlaa_bridge::RT_DYNAMIC_CLUSTERS;
  // Mega Geometry's most a frame by the GPU's memory (~9.3 GB at all of it), a failed room's next.
  static constexpr std::array<uint32_t, 4> CLUSTER_TIERS = {MAX_CLUSTERS, MAX_CLUSTERS / 4 * 3, MAX_CLUSTERS / 4, MAX_CLUSTERS / 8};
  static constexpr uint32_t CLUSTER_ROOM = 32768;  // Mega Geometry's room for a frame's clusters at first (~200 MB)
  static constexpr uint32_t CLUSTER_STEP = 8192;   // its runs grow in steps of this
  // Compact draws (v16): their cluster sets' pool (bytes, in 16-byte units; past it a word the compact pass may read),
  // a slot's upload buffer for its draws' table (four uint4 each: rt_compact.cs_6_5.hlsl), then its new sets' data.
  static constexpr uint32_t SET_POOL_BYTES = 64u << 20;
  // Cluster templates (Mega Geometry): an opaque compact draw's clusters instantiated each frame from templates built once
  // per set (its triangles, no positions), a request's template builds at most (their arguments, 72 bytes each, and
  // destinations after the new sets' data), the templates' pool.
  static constexpr uint32_t TEMPLATE_BUILDS = 16384;
  static constexpr uint64_t TEMPLATE_POOL_BYTES = 128ull << 20;  // ~43,000 templates of 3 KB (a battle: ~700 sets)
  // The static BLASes' pools (BlasPlace): chunks of 32 MB in 256-byte units; a BLAS past 8 MB takes a buffer of its own.
  static constexpr uint64_t BLAS_CHUNK_BYTES = 32ull << 20;
  static constexpr uint32_t BLAS_CHUNK_UNITS = static_cast<uint32_t>(BLAS_CHUNK_BYTES / D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  static constexpr uint64_t BLAS_OWN_BYTES = BLAS_CHUNK_BYTES / 4;
  static constexpr uint32_t BUILD_POOL = 0;
  static constexpr uint32_t KEPT_POOL = 1;
  static constexpr uint64_t COMPACT_SETS_OFFSET = uint64_t{dlaa_bridge::RT_COMPACT_DRAWS} * 64;
  static constexpr uint64_t COMPACT_TEMPLATE_ARGS_OFFSET = COMPACT_SETS_OFFSET + dlaa_bridge::RT_CLUSTER_DATA_BYTES;
  static constexpr uint64_t COMPACT_TEMPLATE_DESTINATIONS_OFFSET = COMPACT_TEMPLATE_ARGS_OFFSET + uint64_t{TEMPLATE_BUILDS} * 72;
  static constexpr uint64_t COMPACT_TEMPLATE_COUNT_OFFSET = COMPACT_TEMPLATE_DESTINATIONS_OFFSET + uint64_t{TEMPLATE_BUILDS} * 8;
  static constexpr uint64_t COMPACT_UPLOAD_BYTES = COMPACT_TEMPLATE_COUNT_OFFSET + 16;
  // A slot's upload buffer for the captures' clusters: the CLASes' arguments (64 bytes each, copied into cluster_args_
  // after the compact draws' built from triangles), the cluster BLASes' (16 each: the rest's before and after the
  // grass's, the grass's, the smoke's), the CLAS counts (built from triangles, from templates), the BLASes'
  // destinations, the indices (0-255), the clusters' records (by geometry index).
  static constexpr size_t CLUSTER_BLASES = 4;
  static constexpr uint64_t CLUSTER_ARGS_OFFSET = 0;
  static constexpr uint64_t CLUSTER_BLAS_ARGS_OFFSET = uint64_t{MAX_CLUSTERS} * 64;
  static constexpr uint64_t CLUSTER_COUNT_OFFSET = CLUSTER_BLAS_ARGS_OFFSET + (CLUSTER_BLASES * 16);
  // rt_sway.hlsli's flags; sway_counts_'s bytes (the cluster BLASes' arguments and the CLAS counts, as in that upload, then
  // rt_sway's own: its GROUPS, 16 bytes per 64 clusters).
  static constexpr uint32_t SWAY_CUT = 1;
  static constexpr uint32_t SWAY_IN_VIEW = 2;
  static constexpr uint64_t SWAY_COUNTS_BYTES = 96 + (uint64_t{MAX_CLUSTERS} / 64 * 16) + 16;
  static constexpr uint64_t CLUSTER_DESTINATION_OFFSET = CLUSTER_COUNT_OFFSET + 8;
  static constexpr uint64_t CLUSTER_INDICES_OFFSET = CLUSTER_DESTINATION_OFFSET + 248;
  static constexpr uint64_t CLUSTER_RECORDS_OFFSET = CLUSTER_INDICES_OFFSET + 256;
  static constexpr uint64_t CLUSTER_UPLOAD_BYTES = CLUSTER_RECORDS_OFFSET + uint64_t{MAX_CLUSTERS} * 16;
  // The partitioned TLAS: an instance per static record and the units' last, this many a partition; a slot's upload
  // buffer: the write arguments (104 bytes each), the update arguments (16), the two operations, their count.
  static constexpr uint32_t PTLAS_INSTANCES = MAX_INSTANCES + 1;
  static constexpr uint32_t PTLAS_PARTITION_SIZE = 256;
  static constexpr uint64_t PTLAS_WRITES_OFFSET = 0;
  static constexpr uint64_t PTLAS_OPS_OFFSET = uint64_t{PTLAS_INSTANCES} * 104;
  static constexpr uint64_t PTLAS_COUNT_OFFSET = PTLAS_OPS_OFFSET + 64;
  static constexpr uint64_t PTLAS_UPLOAD_BYTES = PTLAS_COUNT_OFFSET + 16;
  // Records (InstanceID + geometry index): static meshes first, then each slot's units in turn, then each slot's
  // clusters; the pools: a normal per triangle, a UV per leaf vertex (static meshes first, then each slot's units).
  static constexpr UINT STATIC_RECORDS = 16384;
  // Instanced meshes' rows read back: a slot's request at most this many instances.
  static constexpr UINT ROWS_READBACK = 32768;
  // Placed meshes (rt_place.cs_6_5.hlsl): a request's jobs at most (its new captures', its placements'), after the
  // records' BLAS addresses in its upload buffer; the dedupe table's keys.
  static constexpr UINT PLACE_JOBS = dlaa_bridge::RT_CAPTURES + dlaa_bridge::RT_PLACEMENTS;
  static constexpr uint64_t PLACE_TABLE_BYTES = uint64_t{STATIC_RECORDS} * 8;
  static constexpr UINT CLUSTER_RECORDS = STATIC_RECORDS + SLOTS * dlaa_bridge::RT_CAPTURES;
  static constexpr UINT RECORD_COUNT = CLUSTER_RECORDS + SLOTS * MAX_CLUSTERS;
  static constexpr UINT STATIC_TRIANGLES = 8u << 20;
  static constexpr UINT NORMAL_POOL = STATIC_TRIANGLES + SLOTS * DYNAMIC_TRIANGLES;
  static constexpr UINT STATIC_UVS = 8u << 20;
  static constexpr UINT DYNAMIC_UVS = dlaa_bridge::RT_DYNAMIC_UVS;  // a slot's at most (grass, swaying leaves: dynamic_uvs_)
  static constexpr uint32_t RECORD_MASKED = 1;  // rt_scene.hlsli
  static constexpr uint32_t RECORD_THIN = 2;
  static constexpr uint32_t RECORD_SMOKE = 4;
  static constexpr uint32_t TRACE_SMOKE = 1u << 18;  // rt_scene.hlsli SMOKE_MET: the smoke's TLAS holds it
  static constexpr uint32_t RECORD_GRASS = 8;
  static constexpr uint32_t RECORD_LEAF = 16;
  // TLAS instance masks (rt_scene.hlsli; rt_place.cs_6_5.hlsl for the placed meshes): the smoke in an instance of its
  // own (the frame's last dynamic captures, a BLAS apart), which only the rays toward the sun meet; the leaves' meshes
  // their own group (Light Through Leaves counts them apart); everything else solid.
  static constexpr UINT SOLID_INSTANCE = 0x01;
  static constexpr UINT SMOKE_INSTANCE = 0x02;
  static constexpr UINT LEAF_INSTANCE = 0x04;
  static constexpr UINT GRASS_INSTANCE = 0x08;       // placed grass (most rays pass it)
  static constexpr UINT GRASS_SWAY_INSTANCE = 0x10;  // the swaying grass's cluster BLAS
  static constexpr UINT CUT_OUT_INSTANCE = 0x40;     // static solid cut-outs (rt_scene.hlsli CUT_OUTS)
  static constexpr float LEAF_ALPHA_CUTOFF = 0.5f;  // rt_scene_data.cs_6_5.hlsl
  static constexpr uint32_t PLACE_FOLIAGE = 1;  // rt_place.cs_6_5.hlsl
  static constexpr uint32_t PLACE_HIDDEN = 2;
  static constexpr uint32_t PLACE_GRASS = 4;
  static constexpr uint32_t PLACE_CUT_OUT = 8;
  // Opacity micromaps (NVIDIA's OMM SDK): a leaf mesh's data at most (the SDK makes micromaps coarser to fit), its
  // triangles at most, the bakes on their way at most.
  static constexpr uint32_t MICROMAP_MESH_BYTES = 8u << 20;
  static constexpr uint32_t MICROMAP_TRIANGLES = 1u << 18;
  static constexpr size_t MICROMAP_BAKES = 128;
  // The BLASes a request can build for compaction: a new one each capture, one with its micromaps each bake finished.
  static constexpr size_t COMPACTIONS = dlaa_bridge::RT_CAPTURES + MICROMAP_BAKES;
  static constexpr size_t MICROMAP_WORKERS = 4;  // bakes at once (each with the SDK's own threads)
  static constexpr size_t SET_MICROMAP_READS = 32;  // grass sets' micromap reads a request at most
  static constexpr uint64_t MICROMAP_WORKLOAD = 1ull << 28;  // a bake's texel tests at most (coarser micromaps past it)
  // GPU Micromap Baking: bakes a request at most and their triangles (the game waits for them: the rest in the next requests,
  // a battle's few hundred within a second or two), the SDK's descriptors (a ring: about 71 a bake), its scratch.
  static constexpr size_t MICROMAP_GPU_BAKES = 8;
  static constexpr uint32_t MICROMAP_GPU_TRIANGLES = 1u << 15;
  static constexpr UINT OMM_DESCRIPTORS = 65536;
  // Grass's finest micromap level (leaves': 6): new grass's shadows wait for its bakes (ATN's debug28: still a visible
  // delay), and level 6 is four times the work.
  static constexpr int GRASS_MICROMAP_LEVEL = 5;
  // The scene data pass's garbage count (rt_scene_data.cs_6_5.hlsl): three counts, the example's capture, its vertices.
  static constexpr UINT LEFT_OUT_BYTES = 64;
  // A request slower than this on the GPU is reported (ms), at most this many times.
  static constexpr double SLOW_MS = 100.0;
  static constexpr uint32_t SLOW_REPORTS = 50;
  // The effects' history (EnsureHistory): its kinds, released after this many frames unused (~10 s).
  enum History : size_t {
    HISTORY_PATHS,
    HISTORY_REUSE,
    HISTORY_SPECULAR,
    HISTORY_AMBIENT,
    HISTORY_LIGHTS,
    HISTORY_REFLECT,
    HISTORY_WATER,
    HISTORY_SKY,
    HISTORY_RATIO,
    HISTORY_KINDS
  };
  static constexpr uint32_t HISTORY_FRAMES = 600;
  // The descriptor table: SRVs t1-t7, UAVs u0-u5.
  enum Descriptor : UINT {
    POSITIONS,
    SUN,
    RECORDS,
    UVS,
    NORMALS,
    ATLAS,
    VERTICES,
    VISIBILITY,
    AMBIENT,
    SUN_RAYS,
    RECORDS_WRITTEN,
    UVS_WRITTEN,
    NORMALS_WRITTEN,
    LIGHT,   // t9 (the ray-traced Light Bounce's)
    BOUNCE,  // u9
    // Paths (v8): t10-t14, then u10-u21.
    PREVIOUS_COLOR,
    PREVIOUS_POSITIONS,
    GUIDE_NORMALS,
    GUIDE_ALBEDO,
    GUIDE_ROUGHNESS,
    CACHE_KEYS,
    CACHE_RADIANCE,
    CACHE_SUMS,
    CACHE_ALBEDO,
    LIGHT_FIT,
    LIGHT_KEPT,
    SPECULAR_ACCUMULATED,
    SPECULAR_HISTORY,
    SPECULAR_META,
    SPECULAR_META_HISTORY,
    REFLECTION,
    SPECULAR_OCCLUSION,
    LIGHT_MASK,  // u22 (v9)
    REUSE_SAMPLES,  // u23-u24: ReSTIR GI's reservoirs, two sets
    REUSE_DATA = REUSE_SAMPLES + 2,  // u25-u26
    FOG = REUSE_DATA + 2,  // u27 (v11)
    AMBIENT_HISTORY,  // u28-u29: the ambient visibility kept over frames, two sets (v11)
    AMBIENT_POSITIONS = AMBIENT_HISTORY + 2,  // u30: the last trace's positions
    HIT_DISTANCE,  // u31: Ray Reconstruction's specular hit distance (v17)
    LIGHT_HISTORY,  // u32-u33: the local lights' bits kept over frames, two sets
    FOG_LIGHTS = LIGHT_HISTORY + 2,  // u34 (v18): the local lights' light in the fog
    TINT,  // u35 (v19): Coloured Shadows' filter
    REFLECT_SAMPLES,  // u36-u37 (v19, helpers 55 on): Own ReSTIR's reservoirs, two sets
    REFLECT_DATA = REFLECT_SAMPLES + 2,  // u38-u39
    // NRD's inputs (helpers 73 on; rt_nrd_front.hlsl): u40-u43 the sun's, u44-u47 the Light Bounce's, u48-u50 the
    // reflections', u51-u53 the ambient's.
    NRD_SUN_VIEW_Z = REFLECT_DATA + 2,
    NRD_SUN_NORMAL_ROUGHNESS,
    NRD_SUN_PENUMBRA,
    NRD_SUN_TRANSLUCENCY,
    NRD_BOUNCE_HIT_DISTANCE,
    NRD_BOUNCE_VIEW_Z,
    NRD_BOUNCE_NORMAL_ROUGHNESS,
    NRD_BOUNCE_SIGNAL,
    NRD_REFLECT_VIEW_Z,
    NRD_REFLECT_NORMAL_ROUGHNESS,
    NRD_REFLECT_SIGNAL,
    NRD_AMBIENT_VIEW_Z,
    NRD_AMBIENT_NORMAL_ROUGHNESS,
    NRD_AMBIENT_SIGNAL,
    OWN_SKY,  // u54-u55 (helpers 107 on): Path-Traced Ambient's own sky at the bounce texels, two sets
    LIGHT_RATIO = OWN_SKY + 2,  // u56-u57 (helpers 111 on): Torch and Fire Shadows from the Light Bounce's ratio, two sets
    DESCRIPTOR_COUNT = LIGHT_RATIO + 2,
  };
  static constexpr UINT SRV_COUNT = VISIBILITY;
  // rt_path.hlsli's path flags and cache size.
  static constexpr uint32_t PATH_TRACING = 1;
  static constexpr uint32_t PATH_REFLECTIONS = 2;
  static constexpr uint32_t PATH_RAW = 512;
  static constexpr uint32_t PATH_NRD = 268435456;
  static constexpr uint32_t PATH_SQUARED_LOBE = 536870912;
  static constexpr uint32_t PATH_REFLECT_FOG = 16384;
  static constexpr uint32_t PATH_REFLECT_STEADY = 32768;  // rt_reflect_filter (the trace's PATH_AMBIENT_PARITY: another request's word)
  static constexpr uint32_t TRACE_NRD_BOUNCE = 4194304;   // rt_scene.hlsli
  static constexpr uint32_t TRACE_NRD_AMBIENT = 8388608;  // rt_scene.hlsli
  static constexpr uint32_t PATH_SPECULAR_OCCLUSION = 4;
  static constexpr uint32_t PATH_HISTORY_VALID = 8;
  static constexpr uint32_t PATH_CACHE_VALID = 16;
  static constexpr uint32_t PATH_SPECULAR_HISTORY = 32;
  static constexpr uint32_t PATH_GUIDES = 64;
  static constexpr uint32_t PATH_SUN_TRACED = 128;
  static constexpr uint32_t PATH_LIGHT = 256;
  static constexpr uint32_t PATH_REUSE = 1024;
  static constexpr uint32_t PATH_REUSE_HISTORY = 2048;
  static constexpr uint32_t PATH_REUSE_PARITY = 4096;
  static constexpr uint32_t PATH_REFLECT_REUSE = 4194304;
  static constexpr uint32_t PATH_REFLECT_OWN = 8388608;
  static constexpr uint32_t PATH_REFLECT_OWN_HISTORY = 16777216;
  static constexpr uint32_t PATH_REFLECT_PARITY = 33554432;
  static constexpr uint32_t PATH_LIGHT_FINAL = 67108864;
  static constexpr uint32_t PATH_SKY_APART = 134217728;
  static constexpr uint32_t PATH_LOCAL_LIGHTS = 8192;
  static constexpr uint32_t PATH_AMBIENT_HISTORY = 16384;  // rt_camera.hlsli
  static constexpr uint32_t PATH_AMBIENT_PARITY = 32768;
  static constexpr uint32_t PATH_TINT = 1048576;  // rt_camera.hlsli
  static constexpr uint32_t PATH_TINT_GUIDES = 2097152;
  static constexpr uint32_t PATH_LIGHT_HISTORY = 131072;
  static constexpr uint32_t PATH_PERMUTE = 65536;
  static constexpr uint32_t PATH_AMBIENT = 262144;  // rt_path.hlsli
  static constexpr uint32_t PATH_KEEP_LIGHT = 524288;
  static constexpr uint32_t PATH_KEEP_CUBE = 1073741824;
  static constexpr uint32_t PATH_SKY_HISTORY = 131072;  // rt_path.hlsli (the trace's PATH_LIGHT_HISTORY: another request's word)
  static constexpr uint32_t PATH_SKY_PARITY = 1048576;  // rt_path.hlsli (the trace's PATH_TINT)
  static constexpr uint32_t PATH_SUN_LIGHT = 2097152;   // rt_path.hlsli (the trace's PATH_TINT_GUIDES)
  static constexpr uint32_t PATH_START_PAST = 2147483648;  // rt_camera.hlsli
  static constexpr UINT LIGHT_KEPT_TEXELS = 5;  // rt_path.hlsli light_kept
  static constexpr UINT CACHE_ENTRIES = 1u << 20;
  // rt_path.hlsli's b3.
  struct PathConstants {
    std::array<float, 16> view_projection = {};
    std::array<float, 16> previous_view_projection = {};
    uint32_t bounce_width = 0;
    uint32_t bounce_height = 0;
    uint32_t bounce_scale = 1;
    uint32_t flags = 0;
    uint32_t bounces = 0;
    uint32_t samples = 1;
    float bounce_range = 10.f;
    float reflection_range = 10.f;
    float sky_range = 2.f;  // rt_camera.hlsli
    uint32_t previous_square_root = 0;  // RT_PATH_COLOR_SQUARE_ROOT
    uint32_t previous_positions_slice = 0;  // kept_positions_' slice with last frame's
    uint32_t lights_in_bounce = 0;  // rt_camera.hlsli LIGHTS_*: the mode (RT_PATH_LIGHTS_IN_BOUNCE 1, with RT_PATH_LIGHT_RATIO 3), the ratio's set and validity
  };
  // Torch and Fire Light in the Light Bounce for the harness without an addon asking (RENODX_TW_LIGHTS_IN_BOUNCE: 1 as asked,
  // 2 the bounce holding only the lights' light: a measurement, 3 Torch and Fire Shadows from the Light Bounce); 0 none.
  static uint32_t LightsInBounceTest() {
    std::array<wchar_t, 4> value = {};
    if (GetEnvironmentVariableW(L"RENODX_TW_LIGHTS_IN_BOUNCE", value.data(), static_cast<DWORD>(value.size())) == 0) return 0;
    switch (value[0]) {
      case L'2':
        return 2u;
      case L'3':
        return 3u;
      default:
        return 1u;
    }
  }
#if defined(RENODX_NRD)
  // NRD's texture of a signal (nrd_d3d12.hpp), with a place in the trace's descriptors when rt_nrd_front.hlsl writes it (0: none).
  struct NrdTexture {
    nrd::ResourceType type;
    DXGI_FORMAT format;
    UINT descriptor;
  };
  struct NrdSignal {
    std::unique_ptr<Nrd12> denoiser;
    ComPtr<ID3D12PipelineState> front;
    std::array<uint32_t, 2> size = {};
    uint32_t used_frame = 0;
    bool failed = false;
  };
  static constexpr uint32_t NRD_RELEASE_FRAMES = 120;
  static constexpr D3D12_SHADER_RESOURCE_VIEW_DESC POSITIONS_VIEW = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                                     .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Texture2D = {.MipLevels = 1}};
#endif
  enum Root : UINT {
    ROOT_SCENE,
    ROOT_TABLE,
    ROOT_SETTINGS,
    ROOT_DISPATCH,
    ROOT_CAPTURE_TABLE,
    ROOT_MICROMAP_DATA,
    ROOT_MICROMAP_INDICES,
    ROOT_MICROMAP_DESCS,
    ROOT_BOUNCE,  // b2: the ray-traced Light Bounce's constants
    ROOT_PATHS,   // b3: the paths' constants (rt_path.hlsli)
    ROOT_LIGHTS,  // b4: the trace's local lights
    ROOT_SMOKE,   // t15: the smoke's TLAS (rt_scene.hlsli smoke_scene)
  };
  // b4 (rt_scene.hlsli Lights): RT_ALL_LIGHTS of world position and reach, their count, their colours, their places in
  // the last trace's list (a byte each); a CBV's 256-byte multiple.
  static constexpr UINT LIGHT_COUNT_OFFSET = dlaa_bridge::RT_ALL_LIGHTS * 16;
  static constexpr UINT LIGHT_COLOURS_OFFSET = LIGHT_COUNT_OFFSET + 16;
  static constexpr UINT LIGHT_PREVIOUS_OFFSET = LIGHT_COLOURS_OFFSET + (dlaa_bridge::RT_ALL_LIGHTS * 16);
  static constexpr UINT LIGHT_CONSTANT_BYTES = (LIGHT_PREVIOUS_OFFSET + dlaa_bridge::RT_ALL_LIGHTS + 255) / 256 * 256;

  // The addon's lights: at most RT_MAX_LIGHTS before v31; light `index`'s position and reach (or colour), the first
  // RT_MAX_LIGHTS in rt_lights, the others in rt_lights_more.
  static uint32_t SharedLightCount(const dlaa_bridge::Shared& shared, uint32_t version) {
    return std::min(shared.rt_light_count, (version >= 31) ? dlaa_bridge::RT_ALL_LIGHTS : dlaa_bridge::RT_MAX_LIGHTS);
  }
  static const float* SharedLight(const dlaa_bridge::Shared& shared, uint32_t index, bool colour) {
    if (index < dlaa_bridge::RT_MAX_LIGHTS) return colour ? shared.rt_light_colours[index] : shared.rt_lights[index];
    const uint32_t more = index - dlaa_bridge::RT_MAX_LIGHTS;
    return colour ? shared.rt_light_colours_more[more] : shared.rt_lights_more[more];
  }
  // b4's positions, count and colours.
  static void CopyLights(const dlaa_bridge::Shared& shared, uint32_t count, uint8_t* data) {
    for (uint32_t light = 0; light < count; ++light) {
      std::memcpy(data + (light * 16), SharedLight(shared, light, false), 16);
      std::memcpy(data + LIGHT_COLOURS_OFFSET + (light * 16), SharedLight(shared, light, true), 16);
    }
    std::memcpy(data + LIGHT_COUNT_OFFSET, &count, sizeof(count));
  }

  struct Slot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList4> list;
    // Its last request and passes (LostReport).
    std::string request;
    std::vector<const wchar_t*> marks;
    std::string micromap_builds;  // LostReport: the BLASes built with micromaps (debug34: the GPU hung in one's build)
    bool stamped = false;  // its last request wrote GPU times (Resolve)
    uint32_t gpu_bakes = 0;  // GPU Micromap Baking's bakes in its last trace (the profile)
    ComPtr<ID3D12Resource> instances;
    D3D12_RAYTRACING_INSTANCE_DESC* instance_data = nullptr;
    ComPtr<ID3D12Resource> table;  // the scene data pass's capture table
    uint32_t* table_data = nullptr;
    // The compacted sizes of the BLASes it built (written by the builds, copied for the CPU), their meshes and builds.
    ComPtr<ID3D12Resource> sizes;
    ComPtr<ID3D12Resource> readback;
    std::vector<std::pair<uint64_t, uint64_t>> compactions;
    ComPtr<ID3D12Resource> paths;  // its paths' constants (b3)
    uint8_t* paths_data = nullptr;
    std::string profile_parts;  // the profile line's last field (a trace's record by pass)
    ComPtr<ID3D12Resource> lights;  // its trace's local lights (b4: rt_trace.cs_6_5.hlsl)
    // Instanced captures' rows copied for the CPU (read when the slot comes round): mesh key, its build, where, how many.
    ComPtr<ID3D12Resource> rows_readback;
    struct RowRead {
      uint64_t key = 0;
      uint64_t build = 0;
      uint32_t offset = 0;
      uint32_t count = 0;
    };
    std::vector<RowRead> row_reads;
    // The scene data pass's garbage left out (far off, stretched parts, stretched others, the first such capture's
    // index), read when the slot comes round, with that request's capture keys.
    ComPtr<ID3D12Resource> left_out;
    ComPtr<ID3D12Resource> left_out_readback;
    uint32_t* left_out_data = nullptr;
    std::vector<uint64_t> capture_keys;
    std::vector<uint32_t> capture_flags;
    std::string status;  // its request's (a slow one's report)
    std::string profile;  // its request's profile line but its GPU times (WriteProfile); empty: none
    ComPtr<ID3D12Resource> jobs;  // the records' BLAS addresses and its placement jobs (rt_place's t8)
    uint8_t* jobs_data = nullptr;
    uint8_t* light_data = nullptr;
    ComPtr<ID3D12Resource> clusters;  // Mega Geometry's arguments and records (CLUSTER_*_OFFSET)
    uint8_t* cluster_data = nullptr;
    ComPtr<ID3D12Resource> compact;  // its compact draws' table and new cluster sets (COMPACT_*)
    uint8_t* compact_data = nullptr;
    ComPtr<ID3D12Resource> ptlas;  // the partitioned TLAS's operations (PTLAS_*_OFFSET)
    ComPtr<ID3D12Resource> ptlas_device;  // and their copy the build reads (device memory)
    uint8_t* ptlas_data = nullptr;
    uint64_t value = 0;  // own_fence_'s value after its last use
  };
  // A static BLAS's memory (suballocated as NVIDIA's RTXMU does: a committed buffer each cost 64 KB at least and a driver
  // allocation, hundreds of new meshes at once a hitch): a place in one of the pools' chunks (BLAS_CHUNK_BYTES buffers,
  // 256-byte units: the builds' and the compacted BLASes' pools, so short-lived builds leave no holes among the kept), or
  // a buffer of its own past BLAS_OWN_BYTES.
  struct BlasPlace {
    ComPtr<ID3D12Resource> own;
    uint32_t pool = 0;
    uint32_t chunk = 0;
    uint32_t offset = 0;  // units
    uint32_t units = 0;   // 0: none (or its own)
    uint64_t address = 0;  // GPU (0: none)
  };
  struct Mesh {
    BlasPlace blas;
    ComPtr<ID3D12Resource> micromaps;  // its opacity micromap array (DXR 1.2), kept while the BLAS lives
    uint64_t build = 0;
    uint64_t bytes = 0;
    uint32_t drawn_frame = 0;
    uint32_t triangles = 0;
    uint32_t record = 0;
    uint32_t normal_offset = 0;
    uint32_t uv_offset = UINT32_MAX;
    bool terrain = false;
    bool foliage = false;
    bool grass = false;  // RT_GRASS: its instances GRASS_INSTANCE
    bool solid_cut_out = false;  // RT_SOLID_CUT_OUT: alpha-tested in its own group (CUT_OUT_INSTANCE), no light through it
    // Instanced (RT_INSTANCED): each instance's transform from the first one's space (its BLAS's), read back from its
    // capture; their places (instance indices, the partitioned TLAS's) from first_instance on.
    std::vector<std::array<float, 12>> transforms;
    uint32_t first_instance = 0;
    // Placed (v14, RT_INSTANCED): in the scene only where its draws put its instances (rt_place), its build rows kept by
    // record.
    bool placed = false;
    bool moved = false;  // ever drawn moving (rt_place's MOVED_KEPT_FRAMES)
  };
  // A buffer, or a BLAS's place in a pool (`units` past 0), let go once the GPU is past request `value`.
  struct Release {
    uint64_t value = 0;
    ComPtr<ID3D12Resource> resource;
    uint32_t pool = 0;
    uint32_t chunk = 0;
    uint32_t offset = 0;
    uint32_t units = 0;
  };
  // A new leaf mesh's micromaps on their way: its UVs and opacity mask (its atlas slice) read back from the request that
  // built its BLAS, its positions kept; baked on a thread (NVIDIA's OMM SDK), then built into a micromap array with its
  // BLAS again (AdvanceMicromaps, Record).
  struct MicromapBake {
    uint64_t key = 0;
    uint64_t build = 0;  // its mesh's or its cluster set's generation (another: dropped)
    uint64_t value = 0;  // the reading request's
    uint32_t triangles = 0;
    bool set = false;  // a cluster set's (grass: its templates built with the micromaps), not a mesh's
    bool grass = false;  // GRASS_MICROMAP_LEVEL
    // Its request's buffers (one of each for all its reads) and its places in them: its vertices (16 bytes each) for the
    // rebuild; read back: a packed half UV a corner, its atlas slice.
    ComPtr<ID3D12Resource> positions;
    uint64_t positions_offset = 0;
    ComPtr<ID3D12Resource> uvs;
    uint64_t uvs_offset = 0;
    ComPtr<ID3D12Resource> mask;
    uint64_t mask_offset = 0;
    // GPU Micromap Baking's (BakeMicromapsOnGpu): its UVs copied into `uvs` (a GPU buffer then) and its mask into
    // OmmGpu::masks[mask_index] when read (`value` UINT64_MAX while it waits for a request's bakes); its outputs' buffers
    // (its request's bakes share them) and places, its histogram's place in their read-back (gpu_failed: refused).
    bool gpu = false;
    bool gpu_failed = false;
    uint32_t mask_index = 0;
    ComPtr<ID3D12Resource> gpu_arrays;
    uint64_t arrays_offset = 0;
    ComPtr<ID3D12Resource> gpu_descs;
    uint64_t descs_offset = 0;
    ComPtr<ID3D12Resource> gpu_indices;
    uint64_t indices_offset = 0;
    ComPtr<ID3D12Resource> gpu_readback;
    uint64_t histogram_offset = 0;
    uint32_t histogram_bytes = 0;
    std::vector<uint32_t> uv_data;
    std::vector<uint8_t> mask_data;
    // The bake's: the micromaps' data, descs, an index a triangle (32-bit; the special ones negative), their histogram.
    std::vector<uint8_t> data;
    std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_DESC> descs;
    std::vector<uint32_t> indices;
    std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY> histogram;
  };
#if defined(RENODX_OMM_SDK)
  // The OMM SDK's CPU baker and the calls the bakes use (omm-lib.dll's).
  struct OmmSdk {
    HMODULE library = nullptr;
    std::array<ommBaker, MICROMAP_WORKERS> bakers = {};  // a worker's each
    decltype(&ommDestroyBaker) destroy_baker = nullptr;
    decltype(&ommCpuCreateTexture) create_texture = nullptr;
    decltype(&ommCpuDestroyTexture) destroy_texture = nullptr;
    decltype(&ommCpuBake) bake = nullptr;
    decltype(&ommCpuGetBakeResultDesc) result_desc = nullptr;
    decltype(&ommCpuDestroyBakeResult) destroy_result = nullptr;
    // GPU Micromap Baking: the GPU baker, its pipeline and calls.
    ommBaker gpu_baker = nullptr;
    ommGpuPipeline gpu_pipeline = nullptr;
    decltype(&ommGpuCreatePipeline) gpu_create_pipeline = nullptr;
    decltype(&ommGpuDestroyPipeline) gpu_destroy_pipeline = nullptr;
    decltype(&ommGpuGetPipelineDesc) gpu_pipeline_desc = nullptr;
    decltype(&ommGpuGetPreDispatchInfo) gpu_pre_dispatch = nullptr;
    decltype(&ommGpuDispatch) gpu_dispatch = nullptr;
    decltype(&ommGpuGetStaticResourceData) gpu_static_data = nullptr;
  };
  // GPU Micromap Baking (MakeGpuBaker, BakeMicromapsOnGpu): the OMM SDK's compute pipelines (root signatures from its
  // descriptions: a table of its ranges, the global constants' CBV, its own root constants, its static samplers; the
  // raster ones are not made: bakes run ComputeOnly), its descriptors (a ring), the indirect dispatch's signature, the
  // corners' indices (ours are not indexed: 0, 1, 2...), its static buffers, the scratch it asks for (kept, grown), the
  // masks' copies (an atlas slice each: its alpha texture) and the bakes' clamped UVs.
  struct OmmGpu {
    struct Pipeline {
      ComPtr<ID3D12RootSignature> root;
      ComPtr<ID3D12PipelineState> state;
      int table = -1;     // its root parameters (-1: none)
      int constants = -1;
      int global = -1;
      UINT constant_count = 0;
    };
    std::vector<Pipeline> pipelines;
    uint32_t globals_bytes = 0;  // the global constants' largest size
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT next = 0;
    ComPtr<ID3D12CommandSignature> dispatch;
    ComPtr<ID3D12Resource> indices;
    std::array<ComPtr<ID3D12Resource>, 2> statics;  // its static vertex and index buffers
    std::array<ComPtr<ID3D12Resource>, OMM_MAX_TRANSIENT_POOL_BUFFERS> transient;
    std::vector<ComPtr<ID3D12Resource>> masks;  // resting in NON_PIXEL_SHADER_RESOURCE
    ComPtr<ID3D12Resource> uvs;                 // resting in NON_PIXEL_SHADER_RESOURCE
    ComPtr<ID3D12PipelineState> clamp;          // rt_omm_uvs
  };
#endif
  // A pool's free ranges (offset -> count), first fit (as Tales of Arise's rt_shadows.hpp).
  struct Ranges {
    std::map<uint32_t, uint32_t> free;

    bool Take(uint32_t count, uint32_t* offset) {
      for (auto range = free.begin(); range != free.end(); ++range) {
        if (range->second < count) continue;
        *offset = range->first;
        const uint32_t left = range->second - count;
        free.erase(range);
        if (left != 0) free.emplace(*offset + count, left);
        return true;
      }
      return false;
    }

    void Give(uint32_t offset, uint32_t count) {
      auto next = free.lower_bound(offset);
      if (next != free.end() && offset + count == next->first) {
        count += next->second;
        next = free.erase(next);
      }
      if (next != free.begin()) {
        auto previous = std::prev(next);
        if (previous->first + previous->second == offset) {
          previous->second += count;
          return;
        }
      }
      free.emplace(offset, count);
    }
  };
  // Released meshes' records and pool ranges, given back once the GPU is past the requests that read them.
  struct PendingFree {
    uint64_t value = 0;
    uint32_t record = 0;
    uint32_t normal_offset = 0;
    uint32_t triangles = 0;
    uint32_t uv_offset = UINT32_MAX;
    uint32_t first_instance = 0;  // its instances' places (instanced meshes)
    uint32_t instances = 0;
  };
  // A compact draws' cluster set (v16): its data's place in set_pool_ (16-byte units), its block's vertices, its clusters
  // (first vertex, vertices, first triangle, triangles each) and triangles; its templates' first place in templates_
  // (UINT32_MAX: none), a template each cluster, alpha-tested or opaque.
  struct ClusterSet {
    uint32_t offset = 0;
    uint32_t units = 0;
    uint32_t vertices = 0;
    uint32_t clusters = 0;
    uint32_t triangles = 0;
    std::vector<std::array<uint32_t, 4>> descs;
    uint32_t template_first = UINT32_MAX;
    bool template_masked = false;
    uint64_t generation = 0;  // which set this is under its key (a bake for an older one is dropped)
    // Its opacity micromaps (alpha-tested sets, grass: baked once like the leaves'): the array and the upload holding a
    // triangle's index each at `micromap_indices`; its templates are built with them. `micromaps_tried`: read once.
    ComPtr<ID3D12Resource> micromaps;
    ComPtr<ID3D12Resource> micromap_inputs;
    uint64_t micromap_indices = 0;
    bool micromaps_tried = false;
  };
  // A replaced or dropped set's units, templates and micromaps, given back once the GPU is past the requests that read
  // them.
  struct PendingSetFree {
    uint64_t value = 0;
    uint32_t offset = 0;
    uint32_t units = 0;
    uint32_t template_first = UINT32_MAX;
    uint32_t templates = 0;
    ComPtr<ID3D12Resource> micromaps;
    ComPtr<ID3D12Resource> micromap_inputs;
  };
  // A micromap bake's read: a new leaf mesh's (its kept positions too) or an alpha-tested cluster set's (its first
  // instance's UVs this request, in its templates' triangle order).
  struct MicromapRead {
    uint64_t key = 0;
    uint64_t build = 0;
    uint32_t first_vertex = 0;
    uint32_t triangles = 0;
    uint32_t uv_offset = 0;
    uint32_t slice = 0;
    bool set = false;
    bool grass = false;
    bool gpu = false;  // baked on the GPU (BakeMicromapsOnGpu), else read back for the CPU
  };

  bool Fail(const char* status) {
    failed_ = true;
    status_ = status;
    return false;
  }

  static constexpr DWORD FENCE_TIMEOUT = 5000;

  // False: this queue's work up to `value` is not proven done on a live device (WaitForGpu). Nothing may be recorded
  // over it or freed under it: the scene counts as lost (no request records, main.cpp makes a new scene).
  bool WaitFor(uint64_t value) {
    if (WaitForGpu(own_fence_.Get(), event_, value, FENCE_TIMEOUT)) return true;
    timed_out_ = true;
    request_failed_ = true;
    return false;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE Descriptor(UINT index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptors_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptor_size_;
    return handle;
  }

  static constexpr uint64_t Align(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

  // `buffer` (default heap, UAV-capable) of `bytes` at least (in 64 KB steps; the old one released once the GPU is past
  // this request). False: no memory for it. Pass std::addressof(member): WRL's ComPtr operator& hands out a ComPtr* only
  // after releasing what it holds (ComPtrRef's operator T*), so GrowBuffer(&member, ...) made a new buffer at every call
  // (helper107-108: the sway's three, 16 MiB, every frame) and released the old one at once instead of after the GPU.
  bool GrowBuffer(ComPtr<ID3D12Resource>* buffer, uint64_t bytes, D3D12_RESOURCE_STATES state,
                  std::source_location where = std::source_location::current()) {
    if (*buffer && (*buffer)->GetDesc().Width >= bytes) return true;
    if (*buffer) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*buffer)});
    *buffer = Buffer(Align(bytes, 1u << 16), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, state, where);
    return static_cast<bool>(*buffer);
  }

  // Named by the line that makes it (DRED's page fault report).
  ComPtr<ID3D12Resource> Buffer(uint64_t bytes, D3D12_HEAP_TYPE heap_type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                                std::source_location where = std::source_location::current()) const {
    const D3D12_HEAP_PROPERTIES heap = {.Type = heap_type};
    const D3D12_RESOURCE_DESC desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = bytes,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = {.Count = 1},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = flags,
    };
    ComPtr<ID3D12Resource> buffer;
    if (SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(buffer.GetAddressOf())))) {
      buffer->SetName(std::format(L"rt_d3d12.hpp:{}", where.line()).c_str());
      if (TestBuffers()) {
        buffers_made_.fetch_add(1, std::memory_order_relaxed);
      }
    }
    return buffer;
  }

  // RENODX_TW_TEST_BUFFERS (GPU harness): each report counts the buffers Buffer made since the last (warm: none a frame).
  static bool TestBuffers() {
    static const bool on = GetEnvironmentVariableW(L"RENODX_TW_TEST_BUFFERS", nullptr, 0) != 0;
    return on;
  }
  static inline std::atomic<uint32_t> buffers_made_ = 0;

  // RENODX_TW_TEST_ROOM=<count> (GPU harness): no memory for a room over that many placed instances or clusters.
  static uint32_t TestRoomLimit() {
    static const uint32_t limit = [] {
      std::array<wchar_t, 16> value = {};
      return (GetEnvironmentVariableW(L"RENODX_TW_TEST_ROOM", value.data(), static_cast<DWORD>(value.size())) != 0) ? static_cast<uint32_t>(_wtoi(value.data())) : UINT32_MAX;
    }();
    return limit;
  }

  // The placed instances' room: their descs (two in turn, the last one's carried instances copied into the next), the
  // counters and dedupe table (twice the descs), the TLAS and its scratch. A bigger one is made on a worker and put in at a
  // request's start.
  struct PlacedRoom {
    uint32_t capacity = 0;
    std::array<ComPtr<ID3D12Resource>, 2> instances;
    ComPtr<ID3D12Resource> state;
    ComPtr<ID3D12Resource> tlas;
    ComPtr<ID3D12Resource> tlas_scratch;
  };

  // Its buffers (any thread). False: no memory for them.
  bool MakePlacedRoom(uint32_t capacity, PlacedRoom* made) const {
    if (capacity > TestRoomLimit()) return false;
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
        .NumDescs = capacity,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
    };
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    made->capacity = capacity;
    for (auto& placed : made->instances) {
      placed = Buffer(uint64_t{capacity} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    made->state = Buffer(16 + (uint64_t{capacity} * 2 * 4), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    made->tlas = Buffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    made->tlas_scratch = Buffer(info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return made->instances[0] && made->instances[1] && made->state && made->tlas && made->tlas_scratch;
  }

  // In place of the room before (released once the GPU is past this request).
  void UsePlacedRoom(PlacedRoom* made) {
    const std::array<std::pair<ComPtr<ID3D12Resource>*, ComPtr<ID3D12Resource>*>, 5> room = {{
        {std::addressof(placed_instances_[0]), std::addressof(made->instances[0])},
        {std::addressof(placed_instances_[1]), std::addressof(made->instances[1])},
        {std::addressof(placed_state_), std::addressof(made->state)},
        {std::addressof(tlas_), std::addressof(made->tlas)},
        {std::addressof(tlas_scratch_), std::addressof(made->tlas_scratch)},
    }};
    for (const auto& [now, next] : room) {
      if (*now) {
        releases_.push_back({.value = own_value_ + 1, .resource = std::move(*now)});
      }
      *now = std::move(*next);
    }
    placed_capacity_ = made->capacity;
  }

#if defined(RENODX_NVAPI_CLUSTERS)
  // A buffer for Mega Geometry's operations (written by them: unordered access).
  ComPtr<ID3D12Resource> ClusterBuffer(uint64_t bytes, D3D12_RESOURCE_STATES state, std::source_location where = std::source_location::current()) const {
    return Buffer(std::max<uint64_t>(bytes, 256), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, state, where);
  }

  bool ClusterRequirements(const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& inputs,
                           NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO* info) const {
    const NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS params = {
        .version = NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER,
        .pInput = &inputs,
        .pInfo = info,
    };
    return NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo(device_.Get(), &params) == NVAPI_OK;
  }

  // Room for `room` clusters a frame (a quarter of them built from triangles, the rest instantiated from templates): its
  // operations' inputs and its buffers: the CLASes (the instantiated first), the cluster BLASes (the units', the smoke's),
  // the scratch, the CLASes' addresses and arguments (32 and 64 bytes likewise). A bigger one is made on a worker while
  // the scene grows (its CreateCommittedResource calls, up to GBs, stalled the request's thread: hitches panning over the
  // map, ATN 2026-10-29) and put in at a request's start.
  struct ClusterRoom {
    uint32_t room = 0;
    uint32_t built = 0;  // of it
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS clas_inputs = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS instantiate_inputs = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS blas_inputs = {};
    uint64_t instantiated_bytes = 0;
    // The CLASes, the cluster BLASes (CLUSTER_BLASES), the scratch, the CLASes' addresses and arguments.
    std::array<uint64_t, 4 + CLUSTER_BLASES> bytes = {};
    std::array<ComPtr<ID3D12Resource>, 4 + CLUSTER_BLASES> buffers;
  };

  // Its inputs and buffers' bytes (the operations' requirements, on the request's thread) for `room` clusters a frame, of
  // them `built` from triangles. False: none.
  bool SizeClusterRoom(uint32_t room, uint32_t built, ClusterRoom* made) const {
    made->room = room;
    made->built = built;
    made->clas_inputs = clas_inputs_;
    made->clas_inputs.maxArgCount = built;
    made->instantiate_inputs = instantiate_inputs_;
    made->instantiate_inputs.maxArgCount = room - built;
    made->blas_inputs = cluster_blas_inputs_;
    made->blas_inputs.clasDesc = {.maxTotalClasCount = room, .maxClasCountPerArg = room};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO clas_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO blas_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO templates_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO instantiate_info = {};
    if (!ClusterRequirements(made->clas_inputs, &clas_info) || !ClusterRequirements(made->blas_inputs, &blas_info)
        || !ClusterRequirements(template_inputs_, &templates_info) || !ClusterRequirements(made->instantiate_inputs, &instantiate_info)) {
      return false;
    }
    made->instantiated_bytes = Align(instantiate_info.resultDataMaxSizeInBytes, NVAPI_D3D12_RAYTRACING_CLAS_BYTE_ALIGNMENT);
    made->bytes = {
        made->instantiated_bytes + clas_info.resultDataMaxSizeInBytes,
        std::max({clas_info.scratchDataSizeInBytes, blas_info.scratchDataSizeInBytes, templates_info.scratchDataSizeInBytes,
                  instantiate_info.scratchDataSizeInBytes}),
        uint64_t{room} * 8,
        (uint64_t{room - built} * 32) + (uint64_t{built} * 64),
    };
    std::fill(made->bytes.begin() + 4, made->bytes.end(), blas_info.resultDataMaxSizeInBytes);
    return true;
  }

  // Its buffers (any thread). False: no memory for them.
  bool MakeClusterRoom(ClusterRoom* made) const {
    if (made->room > TestRoomLimit()) return false;
    constexpr std::array<D3D12_RESOURCE_STATES, 4> STATES = {
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    };
    for (size_t index = 0; index < made->buffers.size(); ++index) {
      made->buffers[index] = ClusterBuffer(made->bytes[index], (index < STATES.size()) ? STATES[index] : D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
      if (!made->buffers[index]) return false;
    }
    return true;
  }

  // In place of the room before (released once the GPU is past this request).
  void UseClusterRoom(ClusterRoom* made) {
    std::array<ComPtr<ID3D12Resource>*, 4 + CLUSTER_BLASES> room = {std::addressof(clas_), std::addressof(cluster_scratch_), std::addressof(clas_addresses_),
                                                                    std::addressof(cluster_args_)};
    for (size_t blas = 0; blas < CLUSTER_BLASES; ++blas) room[4 + blas] = std::addressof(cluster_blases_[blas]);
    for (size_t index = 0; index < room.size(); ++index) {
      if (*room[index]) {
        releases_.push_back({.value = own_value_ + 1, .resource = std::move(*room[index])});
      }
      *room[index] = std::move(made->buffers[index]);
    }
    clas_inputs_ = made->clas_inputs;
    instantiate_inputs_ = made->instantiate_inputs;
    cluster_blas_inputs_ = made->blas_inputs;
    instantiated_bytes_ = made->instantiated_bytes;
    cluster_room_ = made->room;
    built_limit_ = made->built;
    cluster_bytes_ = std::accumulate(made->bytes.begin(), made->bytes.end(), uint64_t{0});
  }
#endif

  // The kept positions' view without them (two slices, as the paths' shaders declare it).
  static constexpr D3D12_SHADER_RESOURCE_VIEW_DESC NO_KEPT_POSITIONS = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                                                        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                                                                        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                                        .Texture2DArray = {.MipLevels = 1, .ArraySize = 2}};
  // The paths' textures not shared (yet): null views, so their descriptors are always valid (the cache's too until
  // EnsureCache makes it).
  void WriteNullPathDescriptors() {
    const D3D12_SHADER_RESOURCE_VIEW_DESC texture_view = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                          .Texture2D = {.MipLevels = 1}};
    for (const auto descriptor : {PREVIOUS_COLOR, GUIDE_NORMALS, GUIDE_ALBEDO, GUIDE_ROUGHNESS}) {
      device_->CreateShaderResourceView(nullptr, &texture_view, Descriptor(descriptor));
    }
    device_->CreateShaderResourceView(nullptr, &NO_KEPT_POSITIONS, Descriptor(PREVIOUS_POSITIONS));
    const D3D12_UNORDERED_ACCESS_VIEW_DESC texture_written = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    for (UINT set = 0; set < 2; ++set) {
      for (const UINT descriptor : {REUSE_SAMPLES, REUSE_DATA, REFLECT_SAMPLES, REFLECT_DATA, OWN_SKY, LIGHT_RATIO}) {
        device_->CreateUnorderedAccessView(nullptr, nullptr, &texture_written, Descriptor(descriptor + set));
      }
    }
    for (const UINT descriptor : std::initializer_list<UINT>{SPECULAR_ACCUMULATED, SPECULAR_HISTORY, SPECULAR_META, SPECULAR_META_HISTORY,
                                                             REFLECTION, SPECULAR_OCCLUSION, LIGHT_MASK, FOG, AMBIENT_HISTORY,
                                                             AMBIENT_HISTORY + 1, AMBIENT_POSITIONS, HIT_DISTANCE, LIGHT_HISTORY,
                                                             LIGHT_HISTORY + 1, FOG_LIGHTS, TINT}) {
      device_->CreateUnorderedAccessView(nullptr, nullptr, &texture_written, Descriptor(descriptor));
    }
    if (cache_keys_) return;
    const D3D12_UNORDERED_ACCESS_VIEW_DESC buffer_written = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                             .Buffer = {.NumElements = 1}};
    for (const auto descriptor : {CACHE_KEYS, CACHE_RADIANCE, CACHE_SUMS, CACHE_ALBEDO, LIGHT_FIT, LIGHT_KEPT}) {
      device_->CreateUnorderedAccessView(nullptr, nullptr, &buffer_written, Descriptor(descriptor));
    }
  }

  // The world radiance cache and the learned lighting (rt_path.hlsli), made at the first path request (zeroed: empty).
  bool EnsureCache() {
    if (cache_keys_) return true;
    const auto pool = [&](DXGI_FORMAT format, UINT elements, UINT element_bytes, ComPtr<ID3D12Resource>* buffer, UINT descriptor) {
      *buffer = Buffer(uint64_t{elements} * element_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (!*buffer) return false;
      const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER, .Buffer = {.NumElements = elements}};
      device_->CreateUnorderedAccessView(buffer->Get(), nullptr, &view, Descriptor(descriptor));
      return true;
    };
    if (pool(DXGI_FORMAT_R32_UINT, CACHE_ENTRIES, 4, &cache_keys_, CACHE_KEYS)
        && pool(DXGI_FORMAT_R32G32B32A32_FLOAT, CACHE_ENTRIES, 16, &cache_radiance_, CACHE_RADIANCE)
        && pool(DXGI_FORMAT_R32_UINT, CACHE_ENTRIES * 4, 4, &cache_sums_, CACHE_SUMS)
        && pool(DXGI_FORMAT_R32G32_UINT, CACHE_ENTRIES, 8, &cache_albedo_, CACHE_ALBEDO)
        && pool(DXGI_FORMAT_R32_UINT, 16, 4, &light_fit_, LIGHT_FIT)
        && pool(DXGI_FORMAT_R32G32B32A32_FLOAT, LIGHT_KEPT_TEXELS, 16, &light_kept_, LIGHT_KEPT)) {
      kept_readback_ = Buffer(uint64_t{LIGHT_KEPT_TEXELS} * 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      return true;
    }
    cache_keys_.Reset();
    WriteNullPathDescriptors();
    return false;
  }

  // A texture of the helper's own at the shared textures' size (over `divisor`, rounded up; `extra_rows` more), and a
  // UAV of one at `descriptor`.
  bool Texture(DXGI_FORMAT format, ComPtr<ID3D12Resource>* resource, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               UINT divisor = 1, UINT extra_rows = 0) const {
    const D3D12_RESOURCE_DESC positions = positions_->GetDesc();
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = (positions.Width + divisor - 1) / divisor,
                                      .Height = ((positions.Height + divisor - 1) / divisor) + extra_rows,
                                      .DepthOrArraySize = 1, .MipLevels = 1, .Format = format, .SampleDesc = {.Count = 1},
                                      .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
    return SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(resource->GetAddressOf())));
  }
  void Written(ID3D12Resource* resource, DXGI_FORMAT format, UINT descriptor) const {
    const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    device_->CreateUnorderedAccessView(resource, nullptr, &view, Descriptor(descriptor));
  }

#if defined(RENODX_NRD)
  // A signal NRD denoises: made at `width` x `height` when first asked for (and again at a new size), kept while asked
  // for (its memory back NRD_RELEASE_FRAMES after: TraceScene); false (once said in the log): the built-in ways instead.
  bool EnsureNrd(NrdSignal* signal, nrd::Denoiser denoiser, const char* name, D3D12_SHADER_BYTECODE front, D3D12_SHADER_BYTECODE finish, uint32_t width, uint32_t height,
                 std::span<const NrdTexture> textures, uint32_t frame) {
    signal->used_frame = frame;
    if (signal->denoiser && signal->size == std::array<uint32_t, 2>{width, height}) return true;
    if (signal->failed) return false;
    if (signal->denoiser) {
      if (!WaitFor(own_value_)) return false;
      signal->denoiser.reset();
    }
    const auto fail = [&](const std::string& why) {
      signal->failed = true;
      slow_report_ += std::format("{}[TW ray tracing] {}: {}", slow_report_.empty() ? "" : "\n", name, why);
      return false;
    };
    // Test switch: NRD refused (the harness's nrd_refused modes).
    if (GetEnvironmentVariableW(L"RENODX_TW_TEST_NRD_REFUSED", nullptr, 0) != 0) return fail("refused (test)");
    if (!signal->front) {
      const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {.pRootSignature = root_signature_.Get(), .CS = front};
      if (FAILED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(signal->front.GetAddressOf())))) return fail("NRD's inputs' pipeline failed");
    }
    auto made = std::make_unique<Nrd12>();
    std::string error;
    if (!made->Create(device_.Get(), denoiser, SLOTS, width, height, finish.pShaderBytecode, finish.BytecodeLength, &error)) return fail(error);
    // Motion: none but the camera's (a texel: NRD scales what it reads by 0).
    if (made->Add(nrd::ResourceType::IN_MV, DXGI_FORMAT_R16G16_FLOAT, 1u << 16) == nullptr) return fail("no memory");
    for (const NrdTexture& texture : textures) {
      ID3D12Resource* const resource = made->Add(texture.type, texture.format);
      if (resource == nullptr) return fail("no memory");
      if (texture.descriptor != 0) Written(resource, texture.format, texture.descriptor);
    }
    slow_report_ += std::format("{}[TW ray tracing] {}: {} MB at {} x {}.", slow_report_.empty() ? "" : "\n", name, made->Bytes() >> 20, width, height);
    signal->denoiser = std::move(made);
    signal->size = {width, height};
    return true;
  }
#endif

  // An effect's history (textures of its own at the textures' size, ~1.3 GB at 4K for all four), made when it is first
  // used and kept marked by `frame`; false: it could not be made.
  bool EnsureHistory(History kind, uint32_t frame) {
    history_used_[kind] = frame;
    switch (kind) {
      case HISTORY_PATHS:
        // Last frame's positions and this frame's (copied for the next): two slices, taking the copy in turn.
        if (kept_positions_) return true;
        {
          D3D12_RESOURCE_DESC desc = positions_->GetDesc();
          desc.DepthOrArraySize = 2;
          desc.MipLevels = 1;
          desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
          desc.Flags = D3D12_RESOURCE_FLAG_NONE;
          const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
          if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                      IID_PPV_ARGS(kept_positions_.GetAddressOf())))) {
            return false;
          }
          D3D12_SHADER_RESOURCE_VIEW_DESC view = NO_KEPT_POSITIONS;
          view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
          device_->CreateShaderResourceView(kept_positions_.Get(), &view, Descriptor(PREVIOUS_POSITIONS));
        }
        copied_frame_ = UINT32_MAX;
        paths_frame_ = UINT32_MAX;
        return true;
      case HISTORY_REUSE:
        // ReSTIR GI's two sets of reservoirs (the path-traced Light Bounce's).
        if (reuse_samples_[0]) return true;
        for (UINT set = 0; set < 2; ++set) {
          if (!Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &reuse_samples_[set]) || !Texture(DXGI_FORMAT_R32G32B32A32_UINT, &reuse_data_[set])) {
            reuse_samples_ = {};
            reuse_data_ = {};
            return false;
          }
          Written(reuse_samples_[set].Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, REUSE_SAMPLES + set);
          Written(reuse_data_[set].Get(), DXGI_FORMAT_R32G32B32A32_UINT, REUSE_DATA + set);
        }
        reuse_frame_ = UINT32_MAX;
        return true;
      case HISTORY_SKY:
        // Path-Traced Ambient's own sky, two sets (~130 MB at 4K).
        if (own_sky_[0]) return true;
        for (UINT set = 0; set < 2; ++set) {
          if (!Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &own_sky_[set])) {
            own_sky_ = {};
            return false;
          }
          Written(own_sky_[set].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, OWN_SKY + set);
        }
        sky_frame_ = UINT32_MAX;
        return true;
      case HISTORY_RATIO:
        // Torch and Fire Shadows from the Light Bounce's shadow ratio, two sets (~130 MB at 4K, Full).
        if (light_ratio_[0]) return true;
        for (UINT set = 0; set < 2; ++set) {
          if (!Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &light_ratio_[set])) {
            light_ratio_ = {};
            return false;
          }
          Written(light_ratio_[set].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, LIGHT_RATIO + set);
        }
        ratio_frame_ = UINT32_MAX;
        return true;
      case HISTORY_REFLECT:
        // Own ReSTIR's two sets (half the render pixels: ~130 MB at 4K).
        if (reflect_samples_[0]) return true;
        for (UINT set = 0; set < 2; ++set) {
          if (!Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &reflect_samples_[set], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 2)
              || !Texture(DXGI_FORMAT_R32G32B32A32_UINT, &reflect_data_[set], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 2)) {
            reflect_samples_ = {};
            reflect_data_ = {};
            return false;
          }
          Written(reflect_samples_[set].Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, REFLECT_SAMPLES + set);
          Written(reflect_data_[set].Get(), DXGI_FORMAT_R32G32B32A32_UINT, REFLECT_DATA + set);
        }
        reflect_frame_ = UINT32_MAX;
        return true;
      case HISTORY_WATER:
        // Softer Water Reflections: the positions at the last reflections request that had the water's (~130 MB at 4K).
        if (water_positions_) return true;
        {
          D3D12_RESOURCE_DESC desc = positions_->GetDesc();
          desc.DepthOrArraySize = 1;
          desc.MipLevels = 1;
          desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
          desc.Flags = D3D12_RESOURCE_FLAG_NONE;
          const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
          if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                      IID_PPV_ARGS(water_positions_.GetAddressOf())))) {
            return false;
          }
        }
        water_frame_ = UINT32_MAX;
        return true;
      case HISTORY_SPECULAR:
        // The reflections' accumulation and its metadata, this frame's and the last.
        if (specular_accumulated_) return true;
        if (!Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &specular_accumulated_) || !Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &specular_history_)
            || !Texture(DXGI_FORMAT_R16G16_FLOAT, &specular_meta_) || !Texture(DXGI_FORMAT_R16G16_FLOAT, &specular_meta_history_)) {
          for (auto* texture : {std::addressof(specular_accumulated_), std::addressof(specular_history_), std::addressof(specular_meta_),
                                std::addressof(specular_meta_history_)}) {
            texture->Reset();
          }
          return false;
        }
        Written(specular_accumulated_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, SPECULAR_ACCUMULATED);
        Written(specular_history_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, SPECULAR_HISTORY);
        Written(specular_meta_.Get(), DXGI_FORMAT_R16G16_FLOAT, SPECULAR_META);
        Written(specular_meta_history_.Get(), DXGI_FORMAT_R16G16_FLOAT, SPECULAR_META_HISTORY);
        specular_history_valid_ = false;
        return true;
      case HISTORY_AMBIENT:
        // The ambient visibility's (v11): two sets in turn, the last trace's positions.
        if (ambient_positions_) return true;
        if (!Texture(DXGI_FORMAT_R16_FLOAT, &ambient_history_[0]) || !Texture(DXGI_FORMAT_R16_FLOAT, &ambient_history_[1])
            || !Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &ambient_positions_, D3D12_RESOURCE_STATE_COMMON)) {
          ambient_history_ = {};
          ambient_positions_.Reset();
          return false;
        }
        Written(ambient_history_[0].Get(), DXGI_FORMAT_R16_FLOAT, AMBIENT_HISTORY);
        Written(ambient_history_[1].Get(), DXGI_FORMAT_R16_FLOAT, AMBIENT_HISTORY + 1);
        Written(ambient_positions_.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, AMBIENT_POSITIONS);
        ambient_frame_ = UINT32_MAX;
        return true;
      case HISTORY_LIGHTS:
        // The local lights' bits: two sets in turn (the ambient's parity, its positions), their tiles' lists below them
        // (v31, as the light mask's).
        if (light_history_[1]) return true;
        if (const UINT rows = dlaa_bridge::LightTileRows(static_cast<uint32_t>(positions_->GetDesc().Height));
            !Texture(DXGI_FORMAT_R32_UINT, &light_history_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 1, rows)
            || !Texture(DXGI_FORMAT_R32_UINT, &light_history_[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 1, rows)) {
          light_history_ = {};
          return false;
        }
        Written(light_history_[0].Get(), DXGI_FORMAT_R32_UINT, LIGHT_HISTORY);
        Written(light_history_[1].Get(), DXGI_FORMAT_R32_UINT, LIGHT_HISTORY + 1);
        lights_frame_ = UINT32_MAX;
        return true;
      default:
        return false;
    }
  }

  // The history an effect left unused for HISTORY_FRAMES given back once the GPU is past this request (its views null:
  // nothing reads them while unused), logged.
  void ExpireHistory(uint32_t frame) {
    const D3D12_UNORDERED_ACCESS_VIEW_DESC null_written = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    const auto release = [&](const char* effect, std::initializer_list<ComPtr<ID3D12Resource>*> textures, std::initializer_list<UINT> written) {
      for (auto* texture : textures) {
        if (*texture) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*texture)});
      }
      for (const UINT descriptor : written) device_->CreateUnorderedAccessView(nullptr, nullptr, &null_written, Descriptor(descriptor));
      if (details_) {
        slow_report_ += std::format("{}[TW ray tracing] The {} history released (unused for {} frames).", (slow_report_.empty() ? "" : "\n"), effect,
                                    HISTORY_FRAMES);
      }
    };
    const auto unused = [&](History kind) { return history_used_[kind] + HISTORY_FRAMES < frame; };
    if (kept_positions_ && unused(HISTORY_PATHS)) {
      device_->CreateShaderResourceView(nullptr, &NO_KEPT_POSITIONS, Descriptor(PREVIOUS_POSITIONS));
      release("paths' positions", {std::addressof(kept_positions_)}, {});
    }
    if (water_positions_ && unused(HISTORY_WATER)) release("water's positions", {std::addressof(water_positions_)}, {});
    if (reuse_samples_[0] && unused(HISTORY_REUSE)) {
      release("ReSTIR GI", {std::addressof(reuse_samples_[0]), std::addressof(reuse_samples_[1]), std::addressof(reuse_data_[0]), std::addressof(reuse_data_[1])},
              {REUSE_SAMPLES, REUSE_SAMPLES + 1, REUSE_DATA, REUSE_DATA + 1});
    }
    if (own_sky_[0] && unused(HISTORY_SKY)) release("own sky's", {std::addressof(own_sky_[0]), std::addressof(own_sky_[1])}, {OWN_SKY, OWN_SKY + 1});
    if (light_ratio_[0] && unused(HISTORY_RATIO)) {
      release("torch shadow ratio's", {std::addressof(light_ratio_[0]), std::addressof(light_ratio_[1])}, {LIGHT_RATIO, LIGHT_RATIO + 1});
    }
    if (reflect_samples_[0] && unused(HISTORY_REFLECT)) {
      release("Own ReSTIR", {std::addressof(reflect_samples_[0]), std::addressof(reflect_samples_[1]), std::addressof(reflect_data_[0]), std::addressof(reflect_data_[1])},
              {REFLECT_SAMPLES, REFLECT_SAMPLES + 1, REFLECT_DATA, REFLECT_DATA + 1});
    }
    if (specular_accumulated_ && unused(HISTORY_SPECULAR)) {
      release("reflections'", {std::addressof(specular_accumulated_), std::addressof(specular_history_), std::addressof(specular_meta_),
               std::addressof(specular_meta_history_)},
              {SPECULAR_ACCUMULATED, SPECULAR_HISTORY, SPECULAR_META, SPECULAR_META_HISTORY});
    }
    if (ambient_positions_ && unused(HISTORY_AMBIENT)) {
      release("ambient occlusion's", {std::addressof(ambient_history_[0]), std::addressof(ambient_history_[1]), std::addressof(ambient_positions_)},
              {AMBIENT_HISTORY, AMBIENT_HISTORY + 1, AMBIENT_POSITIONS});
    }
    if (light_history_[1] && unused(HISTORY_LIGHTS)) {
      release("local lights'", {std::addressof(light_history_[0]), std::addressof(light_history_[1])}, {LIGHT_HISTORY, LIGHT_HISTORY + 1});
    }
  }

  // A pass's name among the list's DRED breadcrumbs, kept with its slot (LostReport: DRED leaves markers unnamed).
  void Mark(ID3D12GraphicsCommandList* list, const wchar_t* pass) {
    if (!details_) return;
    if (profiling_) QueryPerformanceCounter(&cpu_marks_.emplace_back(pass, LARGE_INTEGER{}).second);
    list->SetMarker(0, pass, static_cast<UINT>((std::wcslen(pass) + 1) * sizeof(wchar_t)));
    for (Slot& slot : slots_) {
      if (slot.list.Get() == list) slot.marks.push_back(pass);
    }
  }

  static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    return {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {.pResource = resource, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, .StateBefore = before, .StateAfter = after},
    };
  }

  // The request's commands into the slot's list (closed); false: nothing usable recorded.
  bool Record(const dlaa_bridge::Shared& shared, uint32_t version, std::span<const uint8_t> cluster_data, size_t slot_index, Slot* slot) {
    auto* list = slot->list.Get();
    // A request that cannot be recorded: its list closed, a new scene next (Lost; unlike Fail: ATN's debug32, one bad request
    // then left ray tracing off once the textures were made again).
    const auto skip = [&](std::string why) {
      list->Close();
      request_failed_ = true;
      status_ = std::move(why);
      return false;
    };
    StampSlot(list, slot_index);
    Stamp(list, slot_index * STAMPS);
    const uint32_t frame = shared.rt_frame;
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    ID3D12DescriptorHeap* heaps[] = {descriptors_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root_signature_.Get());
    list->SetComputeRootDescriptorTable(ROOT_TABLE, descriptors_->GetGPUDescriptorHandleForHeapStart());
    // The last trace's placed instances past 3/4 of their room: twice it or more, up to the GPU's limit, made on a worker and
    // put in at the first request after, with the kept descs and the counters (the last trace is done; paths requests on
    // their way hold the old TLAS until the release's fence value). No memory for it: a limit half as high (the room stays).
    if (placed_growing_.valid() && placed_growing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      if (const std::unique_ptr<PlacedRoom> made = placed_growing_.get(); made) {
        ID3D12Resource* carried = made->instances[placed_last_].Get();
        ID3D12Resource* kept = placed_instances_[placed_last_].Get();
        const std::array<D3D12_RESOURCE_BARRIER, 4> to_carry = {
            Transition(kept, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            Transition(carried, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(placed_state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
            Transition(made->state.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        list->ResourceBarrier(static_cast<UINT>(to_carry.size()), to_carry.data());
        if (placed_written_ != 0) {
          list->CopyBufferRegion(carried, 0, kept, 0, uint64_t{std::min(placed_written_, placed_capacity_)} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
        }
        list->CopyBufferRegion(made->state.Get(), 0, placed_state_.Get(), 0, 16);
        const std::array<D3D12_RESOURCE_BARRIER, 2> carried_back = {
            Transition(carried, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            Transition(made->state.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        };
        list->ResourceBarrier(static_cast<UINT>(carried_back.size()), carried_back.data());
        UsePlacedRoom(made.get());
        if (details_) {
          slow_report_ += std::format("{}[TW ray tracing] Room for {} placed instances now ({} written).", (slow_report_.empty() ? "" : "\n"), placed_capacity_, placed_written_);
        }
      } else {
        placed_limit_ = std::max(placed_capacity_, placed_limit_ / 2);
        room_note_ = std::format("No memory: {} placed instances at most | ", placed_limit_);
        slow_report_ += std::format("{}[TW ray tracing] {}.", (slow_report_.empty() ? "" : "\n"), room_note_.substr(0, room_note_.size() - 3));
      }
    }
    if (!placed_growing_.valid() && uint64_t{placed_written_} * 4 > uint64_t{placed_capacity_} * 3 && placed_capacity_ < placed_limit_) {
      const auto capacity = static_cast<uint32_t>(std::min<uint64_t>(
          std::max<uint64_t>(uint64_t{placed_capacity_} * 2, std::bit_ceil(uint64_t{placed_written_} * 2)), placed_limit_));
      placed_growing_ = std::async(std::launch::async, [this, capacity]() -> std::unique_ptr<PlacedRoom> {
        auto made = std::make_unique<PlacedRoom>();
        if (!MakePlacedRoom(capacity, made.get())) return nullptr;
        return made;
      });
    }
    // Unused root descriptors still point at a buffer.
    list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
    for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
      list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
    }
    // Instanced meshes captured at this slot's last trace: each instance's transform from the first one's space
    // (rows_k times the inverse of rows_0; degenerate rows: the first instance only).
    if (!slot->row_reads.empty()) {
      const D3D12_RANGE read = {.Begin = 0, .End = uint64_t{ROWS_READBACK} * 64};
      void* mapped = nullptr;
      if (SUCCEEDED(slot->rows_readback->Map(0, &read, &mapped))) {
        const auto* rows = static_cast<const std::array<float, 16>*>(mapped);
        for (const auto& row_read : slot->row_reads) {
          const auto mesh = meshes_.find(row_read.key);
          if (mesh == meshes_.end() || mesh->second.build != row_read.build) continue;
          const auto& first = rows[row_read.offset];
          // The first instance's 3 x 4 (rows acting on (position, 1)) inverted.
          const float a = first[0], b = first[1], c = first[2], d = first[4], e = first[5], f = first[6], g = first[8], h = first[9],
                      i = first[10];
          const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
          if (!(std::abs(determinant) > 1.000023e-12f)) continue;
          const std::array<float, 9> inverse = {(e * i - f * h) / determinant, (c * h - b * i) / determinant, (b * f - c * e) / determinant,
                                                (f * g - d * i) / determinant, (a * i - c * g) / determinant, (c * d - a * f) / determinant,
                                                (d * h - e * g) / determinant, (b * g - a * h) / determinant, (a * e - b * d) / determinant};
          const std::array<float, 3> moved = {first[3], first[7], first[11]};
          std::vector<std::array<float, 12>> transforms(row_read.count);
          bool finite = true;
          for (uint32_t instance = 0; instance < row_read.count; ++instance) {
            const auto& row = rows[row_read.offset + instance];
            auto& transform = transforms[instance];
            for (int r = 0; r < 3; ++r) {
              float translation = row[r * 4 + 3];
              for (int column = 0; column < 3; ++column) {
                const float value = row[r * 4] * inverse[column] + row[r * 4 + 1] * inverse[3 + column] + row[r * 4 + 2] * inverse[6 + column];
                transform[r * 4 + column] = value;
                translation -= value * moved[column];
              }
              transform[r * 4 + 3] = translation;
            }
            finite = finite && std::ranges::all_of(transform, [](float value) { return std::isfinite(value); });
          }
          uint32_t first_instance = 0;
          if (!finite || !free_instances_.Take(row_read.count, &first_instance)) continue;
          mesh->second.transforms = std::move(transforms);
          mesh->second.first_instance = first_instance;
        }
        const D3D12_RANGE written = {};
        slot->rows_readback->Unmap(0, &written);
      }
      slot->row_reads.clear();
    }
    // Compactions: the BLASes built at this slot's last trace (done) copied into places their compacted size (the
    // kept pool).
    if (!slot->compactions.empty()) {
      const D3D12_RANGE read = {.Begin = 0, .End = slot->compactions.size() * sizeof(uint64_t)};
      void* mapped = nullptr;
      if (SUCCEEDED(slot->readback->Map(0, &read, &mapped))) {
        const auto* sizes = static_cast<const uint64_t*>(mapped);
        for (size_t index = 0; index < slot->compactions.size(); ++index) {
          const auto mesh = meshes_.find(slot->compactions[index].first);
          if (mesh == meshes_.end() || mesh->second.build != slot->compactions[index].second || sizes[index] == 0
              || sizes[index] >= mesh->second.bytes) {
            continue;
          }
          BlasPlace compacted = PlaceBlas(sizes[index], KEPT_POOL);
          if (compacted.address == 0) break;
          list->CopyRaytracingAccelerationStructure(compacted.address, mesh->second.blas.address, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
          ReleaseBlas(&mesh->second.blas);
          mesh->second.blas = std::move(compacted);
          mesh->second.bytes = sizes[index];
        }
        const D3D12_RANGE written = {};
        slot->readback->Unmap(0, &written);
      }
      slot->compactions.clear();
      list->ResourceBarrier(1, &uav_barrier);
    }
    // A scratch arena for builds recorded together (one barrier after them; grown, the old one released once the GPU
    // is past this request).
    const auto ensure_scratch = [&](uint64_t bytes) {
      if (bytes <= scratch_bytes_) return true;
      if (scratch_) releases_.push_back({.value = own_value_ + 1, .resource = std::move(scratch_)});
      scratch_bytes_ = std::max(bytes, scratch_bytes_ * 2);
      scratch_ = Buffer(scratch_bytes_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (!scratch_) scratch_bytes_ = 0;
      return static_cast<bool>(scratch_);
    };
    // New captures: their vertices copied from the shared texture, a place each (record, pool ranges), the scene data,
    // micromaps for new leaves (DXR 1.2), a BLAS each (built for tracing) and the units' one BLAS (refitted).
    uint32_t built = 0;
    uint32_t micromaps_built = 0;
    uint64_t dynamic_triangles = 0;
    uint64_t grass_triangles = 0;    // of them alpha-tested (grass, swaying leaves)
    uint64_t terrain_triangles = 0;  // and the terrain's
    bool units_built = false;
    bool units_refitted = false;
    bool units_clustered = false;  // Mega Geometry's cluster BLAS built for them
    bool smoke_built = false;      // the smoke's BLAS (SMOKE_INSTANCE)
    bool smoke_clustered = false;  // or its cluster BLAS
    std::array<bool, CLUSTER_BLASES> cluster_built = {};  // the cluster BLASes built (units_clustered: the first three's)
    uint32_t smoke_record = 0;     // its first capture's (or cluster's) record
    uint32_t clusters_built = 0;
    uint32_t templated_built = 0;  // of them instantiated from templates
    const uint32_t capture_count = std::min(shared.rt_capture_count, dlaa_bridge::RT_CAPTURES);
    // v14: this frame's draws of placed meshes in the scene (their instances' rows in the vertex texture).
    const uint32_t placement_count = (version >= 14) ? std::min(shared.rt_placement_count, dlaa_bridge::RT_PLACEMENTS) : 0u;
    // The request's vertices copied from the shared texture: the captures' triangles and rows, the placements' rows.
    uint32_t vertex_end = 0;
    for (uint32_t index = 0; index < capture_count; ++index) {
      const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
      const uint32_t rows = (version >= 10 && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0) ? capture.instances * 4u : 0u;
      vertex_end = std::max(vertex_end, capture.first_vertex + capture.vertex_count + rows);
    }
    for (uint32_t index = 0; index < placement_count; ++index) {
      vertex_end = std::max(vertex_end, shared.rt_placements[index].first_vertex + shared.rt_placements[index].instances * 4u);
    }
    // Compact draws (v16, with Mega Geometry): the new cluster sets into the pool (one sent again replaces its last; the
    // pool full, the sets this request's draws do not use are dropped and the addon sends them again), then the draws
    // whose set is here and that fit this slot's turn: their table for the compact pass, their clusters first in the
    // units' cluster BLAS (records from first_cluster_record), their triangles first in the slot's pools.
    const uint32_t first_cluster_record = CLUSTER_RECORDS + (static_cast<uint32_t>(slot_index) * MAX_CLUSTERS);
    const uint32_t compact_count = (version >= 16 && clusters_) ? std::min(shared.rt_compact_count, dlaa_bridge::RT_COMPACT_DRAWS) : 0u;
    const uint32_t new_set_count = (version >= 16 && clusters_) ? std::min(shared.rt_new_set_count, dlaa_bridge::RT_NEW_SETS) : 0u;
    std::vector<std::array<uint64_t, 3>> set_copies;  // upload offset, pool offset, bytes
    // The sets this request's draws use (kept when a pool is full).
    const auto used_sets = [&] {
      std::unordered_set<uint64_t> used;
      for (uint32_t draw = 0; draw < compact_count; ++draw) used.insert(shared.rt_compact[draw].set);
      return used;
    };
    uint64_t set_upload = COMPACT_SETS_OFFSET;
    bool sets_dropped = false;
    for (uint32_t index = 0; index < new_set_count; ++index) {
      const dlaa_bridge::RtClusterSet& sent = shared.rt_new_sets[index];
      const uint64_t bytes = (uint64_t{sent.clusters} * 16) + (uint64_t{sent.triangles} * 3);
      if (sent.clusters == 0 || sent.vertices == 0 || uint64_t{sent.offset} + bytes > cluster_data.size() || set_upload + bytes > COMPACT_UPLOAD_BYTES) {
        continue;
      }
      // Each cluster within its block and set, each triangle's vertices within its cluster (a bad index would read past
      // the vertex buffer: a GPU fault), the clusters' vertices the block's (the CLASes' vertices: the request's texels).
      const uint8_t* data = cluster_data.data() + sent.offset;
      const uint8_t* corners = data + (uint64_t{sent.clusters} * 16);
      bool valid = true;
      uint64_t block = 0;
      std::vector<std::array<uint32_t, 4>> descs(sent.clusters);
      for (uint32_t cluster = 0; cluster < sent.clusters && valid; ++cluster) {
        std::array<uint32_t, 4>& desc = descs[cluster];
        std::memcpy(desc.data(), data + (uint64_t{cluster} * 16), sizeof(desc));
        const auto [first_vertex, vertices, first_triangle, triangles] = desc;
        valid = vertices != 0 && vertices <= dlaa_bridge::RT_CLUSTER_VERTICES && triangles != 0 && triangles <= dlaa_bridge::RT_CLUSTER_TRIANGLES
                && uint64_t{first_vertex} + vertices <= sent.vertices && uint64_t{first_triangle} + triangles <= sent.triangles
                && std::all_of(corners + (uint64_t{first_triangle} * 3), corners + ((uint64_t{first_triangle} + triangles) * 3),
                               [vertices](uint8_t corner) { return corner < vertices; });
        block += vertices;
      }
      if (!valid || block != sent.vertices) continue;
      const uint32_t units = static_cast<uint32_t>((bytes + 15) / 16);
      uint32_t offset = 0;
      if (!free_set_units_.Take(units, &offset)) {
        if (!sets_dropped) {
          const std::unordered_set<uint64_t> used = used_sets();
          std::erase_if(sets_, [&](const auto& entry) {
            if (used.contains(entry.first)) return false;
            pending_set_frees_.push_back({.value = own_value_ + 1, .offset = entry.second.offset, .units = entry.second.units,
                                          .template_first = entry.second.template_first, .templates = entry.second.clusters,
                                          .micromaps = std::move(entry.second.micromaps),
                                          .micromap_inputs = std::move(entry.second.micromap_inputs)});
            return true;
          });
          ++set_drops_;
          sets_dropped = true;
        }
        continue;
      }
      if (const auto last = sets_.find(sent.key); last != sets_.end()) {
        pending_set_frees_.push_back({.value = own_value_ + 1, .offset = last->second.offset, .units = last->second.units,
                                      .template_first = last->second.template_first, .templates = last->second.clusters,
                                      .micromaps = std::move(last->second.micromaps), .micromap_inputs = std::move(last->second.micromap_inputs)});
      }
      sets_[sent.key] = {.offset = offset, .units = units, .vertices = sent.vertices, .clusters = sent.clusters, .triangles = sent.triangles,
                         .descs = std::move(descs), .generation = ++set_generation_};
      std::memcpy(slot->compact_data + set_upload, data, bytes);
      set_copies.push_back({set_upload, uint64_t{offset} * 16, bytes});
      set_upload = Align(set_upload + bytes, 16);
    }
    if (!set_copies.empty()) {
      const auto to_copy = Transition(set_pool_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &to_copy);
      for (const auto& [from, to, bytes] : set_copies) list->CopyBufferRegion(set_pool_.Get(), to, slot->compact.Get(), from, bytes);
      const auto copied = Transition(set_pool_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      list->ResourceBarrier(1, &copied);
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    // The last request's clusters filled 3/4 of a run's room or more: that run's room half again what it was or a third
    // more than it wanted, so it is under 3/4 after (a quarter more grew it once more: 827 K clusters for 400 K, 7.6 GB in
    // the harness; in steps of CLUSTER_STEP; doubling the whole room made 524 K clusters for 166 K, 4.8 GB, ATN's
    // first profile; half again what it wanted grew it 12 times as the grass came into view, the second; twice the room
    // again 524 K for 186 K, the third), made on a worker and put in at the first request after (no memory for it: the
    // tier below, the room stays).
    const auto clusters_tier_below = [&] {
      const auto below = std::ranges::find_if(CLUSTER_TIERS, [&](uint32_t tier) { return tier < cluster_limit_; });
      cluster_limit_ = std::max(cluster_room_, (below != CLUSTER_TIERS.end()) ? *below : cluster_room_);
      room_note_ = std::format("No memory: {} clusters at most | ", cluster_limit_);
      slow_report_ += std::format("{}[TW ray tracing] {}.", (slow_report_.empty() ? "" : "\n"), room_note_.substr(0, room_note_.size() - 3));
    };
    if (cluster_growing_.valid() && cluster_growing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      if (const std::unique_ptr<ClusterRoom> made = cluster_growing_.get(); made) {
        UseClusterRoom(made.get());
        if (details_) {
          slow_report_ += std::format("{}[TW ray tracing] Mega Geometry: room for {} clusters a frame now ({} MB).", (slow_report_.empty() ? "" : "\n"),
                                      cluster_room_, (cluster_bytes_ + TEMPLATE_POOL_BYTES) >> 20);
        }
      } else {
        clusters_tier_below();
      }
    }
    if (clusters_ && clusters_crowded_ && !cluster_growing_.valid() && cluster_room_ < cluster_limit_) {
      const auto grown = [](uint32_t room, uint64_t wanted) {
        if (wanted * 4 <= uint64_t{room} * 3) return room;
        const uint64_t step = std::max<uint64_t>(uint64_t{room} * 3 / 2, wanted * 4 / 3);
        return static_cast<uint32_t>(std::min<uint64_t>((step + CLUSTER_STEP - 1) / CLUSTER_STEP * CLUSTER_STEP, MAX_CLUSTERS));
      };
      // Within the GPU's limit neither run's room shrinks.
      const uint32_t templated = std::min(grown(cluster_room_ - built_limit_, templated_wanted_), cluster_limit_ - built_limit_);
      const uint32_t built = std::min(grown(built_limit_, built_wanted_), cluster_limit_ - templated);
      auto made = std::make_unique<ClusterRoom>();
      if (templated + built > cluster_room_) {
        if (SizeClusterRoom(templated + built, built, made.get())) {
          cluster_growing_ = std::async(std::launch::async, [this, made = std::move(made)]() mutable -> std::unique_ptr<ClusterRoom> {
            if (!MakeClusterRoom(made.get())) return nullptr;
            return std::move(made);
          });
        } else {
          clusters_tier_below();
        }
      }
    }
    clusters_crowded_ = false;
    templated_wanted_ = 0;
    built_wanted_ = 0;
#endif
    uint32_t dynamic_triangles_used = 0;  // this slot's turn of the normal and UV pools
    uint32_t dynamic_uvs_used = 0;
    uint64_t cluster_vertices = 0;  // the units' CLASes' (at most TRANSFER_VERTICES: clas_inputs_)
    uint32_t compact_drawn = 0;
    uint32_t compact_clusters = 0;
    bool compact_masked = false;
    // Their keys and flags (the garbage count's sources after the captures).
    std::vector<uint64_t> compact_keys;
    std::vector<uint32_t> compact_flags;
    // In four runs: the draws instantiated from their set's templates (a set's made at its first draw while there is room;
    // the pool full, the templates of the sets this request does not use are given back), the grass's last, then those
    // built from triangles (no templates yet; with the captures' clusters at most built_limit_), the grass's first: the
    // grass's clusters together (their own cluster BLAS, GRASS_INSTANCE). The table in that order: the compact pass finds a
    // cluster's draw by its first cluster.
    // Swaying Shadows Range / Off-Screen Sway (v18, rt_sway.hlsli): the compact pass leaves out the swaying copies of the
    // instances it turns down, the place pass shows their placed copies; only for swaying draws whose placement goes in
    // (their rows here: hidden, of a placed mesh, its rows copied). Grass Ray Tracing Range (in v31's padding: older addons
    // leave it 0, none): grass past it leaves both passes' scene, its swaying copies by the same cut.
    const float grass_range = (version >= 31) ? std::clamp(shared.rt_grass_range, 0.f, 1.0e4f) : 0.f;
    uint32_t sway_flags = 0;
    std::unordered_set<uint32_t> sway_rows;
    if (sway_pipeline_ && version >= 18 && (shared.rt_sway_range > 0.f || (shared.rt_flags & dlaa_bridge::RT_SWAY_IN_VIEW) != 0 || grass_range > 0.f)
        && GrowBuffer(std::addressof(sway_arguments_), cluster_args_->GetDesc().Width, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        && GrowBuffer(std::addressof(sway_lists_), clas_addresses_->GetDesc().Width, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        && GrowBuffer(std::addressof(sway_counts_), SWAY_COUNTS_BYTES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
      sway_flags = SWAY_CUT | (((shared.rt_flags & dlaa_bridge::RT_SWAY_IN_VIEW) != 0) ? SWAY_IN_VIEW : 0u);
      for (uint32_t index = 0; index < placement_count; ++index) {
        const dlaa_bridge::RtPlacement& placement = shared.rt_placements[index];
        const auto mesh = meshes_.find(placement.key);
        if ((placement.flags & dlaa_bridge::RT_PLACE_HIDDEN) != 0 && mesh != meshes_.end() && mesh->second.placed
            && uint64_t{placement.first_vertex} + (uint64_t{placement.instances} * 4) <= std::min(vertex_end, TRANSFER_VERTICES)) {
          sway_rows.insert(placement.first_vertex);
        }
      }
    }
    uint32_t templated_clusters = 0;  // the first two runs'
    uint32_t grass_first = 0;         // the grass's clusters
    uint32_t grass_end = 0;
    uint32_t template_builds = 0;     // this request's new templates
    bool templates_dropped = false;
    // Grass micromaps (with the Leaf Micromaps setting and Mega Geometry's micromap support): an alpha-tested set's first
    // instance's UVs read back once, a few sets a request (the bakes' room shared with the leaves').
    std::vector<MicromapRead> set_micromap_reads;
    // GPU Micromap Baking: every read baked on the GPU (none read back for the CPU), waiting for the requests' bakes
    // (at the end of each, within its budget).
    const bool gpu_bakes = gpu_micromaps_ && (shared.rt_flags & dlaa_bridge::RT_GPU_MICROMAPS) != 0;
    // RENODX_TW_OMM_CHURN=<frames>: every set baked again that often, the micromap path under load (finding GPU resets).
    static const uint32_t omm_churn = [] {
      std::array<wchar_t, 16> value = {};
      return (GetEnvironmentVariableW(L"RENODX_TW_OMM_CHURN", value.data(), static_cast<DWORD>(value.size())) != 0) ? static_cast<uint32_t>(_wtoi(value.data())) : 0u;
    }();
    if (omm_churn != 0 && frame % omm_churn == 0) {
      for (auto& [key, set] : sets_) set.micromaps_tried = false;
    }
    size_t set_micromap_room = (cluster_micromaps_ && bake_micromaps_ && dxr12_ && (shared.rt_flags & dlaa_bridge::RT_MICROMAPS) != 0)
                                   ? std::min<size_t>((gpu_bakes ? MICROMAP_GPU_BAKES : 0u) + SET_MICROMAP_READS,
                                                      MICROMAP_BAKES - std::min(MICROMAP_BAKES, micromap_reads_.size() + micromap_queue_.size()
                                                                                                    + MicromapsBaking() + micromaps_baked_.size()))
                                   : 0u;
#if defined(RENODX_NVAPI_CLUSTERS)
    auto* template_args =
        reinterpret_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS*>(slot->compact_data + COMPACT_TEMPLATE_ARGS_OFFSET);
    auto* template_destinations = reinterpret_cast<uint64_t*>(slot->compact_data + COMPACT_TEMPLATE_DESTINATIONS_OFFSET);
#endif
    for (const auto [templated_run, grass_run] : {std::pair{true, false}, std::pair{true, true}, std::pair{false, true}, std::pair{false, false}}) {
      if (templated_run && grass_run) grass_first = compact_clusters;
      for (uint32_t index = 0; index < compact_count; ++index) {
        const dlaa_bridge::RtCompactDraw& draw = shared.rt_compact[index];
        const auto found = sets_.find(draw.set);
        if (found == sets_.end() || draw.instances == 0) continue;
        ClusterSet& set = found->second;
        const bool masked = (draw.flags & dlaa_bridge::RT_FOLIAGE) != 0 && atlas_;
        if ((masked && (draw.flags & dlaa_bridge::RT_GRASS) != 0) != grass_run) continue;
#if defined(RENODX_NVAPI_CLUSTERS)
        uint32_t first_template = 0;
        if (templated_run && set.template_first == UINT32_MAX && template_builds + set.clusters <= TEMPLATE_BUILDS) {
          if (free_templates_.Take(set.clusters, &first_template)) {
            set.template_first = first_template;
            set.template_masked = masked;
            for (uint32_t cluster = 0; cluster < set.clusters; ++cluster) {
              const auto& [desc_vertex, desc_vertices, desc_triangle, desc_triangles] = set.descs[cluster];
              auto& arg = template_args[template_builds];
              arg = {};
              arg.triangleCount = desc_triangles;
              arg.vertexCount = desc_vertices;
              arg.indexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_8BIT;
              arg.baseGeometryIndexAndFlags = masked ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
                                                     : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_OPAQUE;
              arg.indexBufferStride = 1;
              arg.geometryIndexAndFlagsBufferStride = 4;
              arg.geometryIndexAndFlagsBuffer = template_geometry_->GetGPUVirtualAddress();
              arg.indexBuffer = set_pool_->GetGPUVirtualAddress() + ((uint64_t{set.offset} + set.clusters) * 16) + (uint64_t{desc_triangle} * 3);
              if (set.micromaps) {
                arg.opacityMicromapIndexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_32BIT;
                arg.opacityMicromapIndexBufferStride = 4;
                arg.opacityMicromapArray = set.micromaps->GetGPUVirtualAddress();
                arg.opacityMicromapIndexBuffer = set.micromap_inputs->GetGPUVirtualAddress() + set.micromap_indices + (uint64_t{desc_triangle} * 4);
              }
              template_destinations[template_builds] = templates_->GetGPUVirtualAddress() + (uint64_t{first_template + cluster} * template_bytes_);
              ++template_builds;
            }
          } else if (!templates_dropped) {
            const std::unordered_set<uint64_t> used = used_sets();
            for (auto& [key, other] : sets_) {
              if (other.template_first == UINT32_MAX || used.contains(key)) continue;
              pending_set_frees_.push_back({.value = own_value_ + 1, .template_first = other.template_first, .templates = other.clusters});
              other.template_first = UINT32_MAX;
            }
            templates_dropped = true;
          }
        }
#endif
        if ((set.template_first != UINT32_MAX && set.template_masked == masked) != templated_run) continue;
        const uint64_t clusters = uint64_t{set.clusters} * draw.instances;
        const uint64_t triangles = uint64_t{set.triangles} * draw.instances;
        const uint64_t block_end = uint64_t{draw.first_vertex} + (uint64_t{set.vertices} * draw.instances);
        const uint64_t run_clusters = templated_run ? compact_clusters : compact_clusters - templated_clusters;
        const uint64_t run_room = templated_run ? cluster_room_ - built_limit_ : built_limit_;
        const bool cluster_room = run_clusters + clusters <= run_room;
        clusters_crowded_ = clusters_crowded_ || (run_clusters + clusters) * 4 > run_room * 3;
        (templated_run ? templated_wanted_ : built_wanted_) += clusters;
        if (block_end > TRANSFER_VERTICES || !cluster_room || dynamic_triangles_used + triangles > DYNAMIC_TRIANGLES || (masked && dynamic_uvs_used + (triangles * 3) > dynamic_uvs_)
            || cluster_vertices + (uint64_t{set.vertices} * draw.instances) > TRANSFER_VERTICES) {
          continue;
        }
        cluster_vertices += uint64_t{set.vertices} * draw.instances;
        const std::array<uint32_t, 16> entry = {
            compact_clusters, draw.first_vertex, set.offset * 16, set.clusters,
            set.vertices, set.triangles, STATIC_TRIANGLES + (static_cast<uint32_t>(slot_index) * DYNAMIC_TRIANGLES) + dynamic_triangles_used,
            masked ? STATIC_UVS + (static_cast<uint32_t>(slot_index) * dynamic_uvs_) + dynamic_uvs_used : UINT32_MAX,
            draw.slice | (RecordFlags({.flags = draw.flags | dlaa_bridge::RT_DYNAMIC}, masked, version) << 16), capture_count + compact_drawn, draw.flags,
            templated_run ? set.template_first : 0u,
            ((draw.flags & dlaa_bridge::RT_INSTANCED) != 0 && sway_rows.contains(static_cast<uint32_t>(block_end))) ? static_cast<uint32_t>(block_end)
                                                                                                                  : UINT32_MAX,
            0u, 0u, 0u,
        };
        std::memcpy(slot->compact_data + (uint64_t{compact_drawn} * 64), entry.data(), sizeof(entry));
        if (masked && set_micromap_room != 0 && !set.micromaps_tried && set.triangles <= MICROMAP_TRIANGLES) {
          set.micromaps_tried = true;
          --set_micromap_room;
          set_micromap_reads.push_back({.key = draw.set, .build = set.generation, .triangles = set.triangles, .uv_offset = entry[7],
                                        .slice = draw.slice, .set = true, .grass = (draw.flags & dlaa_bridge::RT_GRASS) != 0,
                                        .gpu = gpu_bakes});
        }
        compact_keys.push_back(draw.set);
        compact_flags.push_back(draw.flags);
        ++compact_drawn;
        compact_clusters += static_cast<uint32_t>(clusters);
        dynamic_triangles_used += static_cast<uint32_t>(triangles);
        if (masked) dynamic_uvs_used += static_cast<uint32_t>(triangles * 3);
        compact_masked = compact_masked || (masked && !grass_run);
        dynamic_triangles += triangles;
        if (masked) grass_triangles += triangles;
        if ((draw.flags & dlaa_bridge::RT_TERRAIN) != 0) terrain_triangles += triangles;
        vertex_end = std::max(vertex_end, static_cast<uint32_t>(block_end));
      }
      if (templated_run) templated_clusters = compact_clusters;
      if (!templated_run && grass_run) grass_end = compact_clusters;
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    // The new templates, built from the sets' triangles (their places copied where the build reads them).
    if (template_builds != 0) {
      std::memcpy(slot->compact_data + COMPACT_TEMPLATE_COUNT_OFFSET, &template_builds, sizeof(template_builds));
      const auto to_copy = Transition(template_destinations_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &to_copy);
      list->CopyBufferRegion(template_destinations_.Get(), 0, slot->compact.Get(), COMPACT_TEMPLATE_DESTINATIONS_OFFSET, uint64_t{template_builds} * 8);
      const auto copied = Transition(template_destinations_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &copied);
      const uint64_t upload = slot->compact->GetGPUVirtualAddress();
      const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC templates_desc = {
          .inputs = template_inputs_,
          .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
          .batchResultData = 0,
          .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
          .destinationAddressArray = {.StartAddress = template_destinations_->GetGPUVirtualAddress(), .StrideInBytes = 8},
          .resultSizeArray = {},
          .indirectArgArray = {.StartAddress = upload + COMPACT_TEMPLATE_ARGS_OFFSET,
                               .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS)},
          .indirectArgCount = upload + COMPACT_TEMPLATE_COUNT_OFFSET,
      };
      const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS templates_params = {
          .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &templates_desc};
      Mark(list, L"templates");
      NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &templates_params);
      const D3D12_RESOURCE_BARRIER built_templates = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
      list->ResourceBarrier(1, &built_templates);
    }
#endif
    vertex_end = std::min(vertex_end, TRANSFER_VERTICES);
    if (vertex_end != 0) {
      const UINT rows = (vertex_end + dlaa_bridge::RT_TRANSFER_WIDTH - 1) / dlaa_bridge::RT_TRANSFER_WIDTH;
      const D3D12_TEXTURE_COPY_LOCATION destination = {
          .pResource = vertex_buffer_.Get(),
          .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
          .PlacedFootprint = {.Offset = 0,
                              .Footprint = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .Width = dlaa_bridge::RT_TRANSFER_WIDTH, .Height = rows,
                                            .Depth = 1, .RowPitch = dlaa_bridge::RT_TRANSFER_WIDTH * 16}},
      };
      const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = vertices_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      const D3D12_BOX box = {.left = 0, .top = 0, .front = 0, .right = dlaa_bridge::RT_TRANSFER_WIDTH, .bottom = rows, .back = 1};
      Mark(list, L"vertices");
      const auto to_copy = Transition(vertices_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->ResourceBarrier(1, &to_copy);
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
      const std::array<D3D12_RESOURCE_BARRIER, 2> copied = {
          Transition(vertices_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
          Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
      };
      list->ResourceBarrier(static_cast<UINT>(copied.size()), copied.data());
    }
    // The placed meshes built this request (v14): their captures' rows as placements (first_vertex: the rows' texel).
    std::vector<dlaa_bridge::RtPlacement> built_placed;
    if ((capture_count != 0 || compact_drawn != 0) && vertex_end != 0) {
      list->CopyBufferRegion(slot->left_out.Get(), 0, left_out_start_.Get(), 0, LEFT_OUT_BYTES);
      std::vector<D3D12_RESOURCE_BARRIER> to_scene_data = {
          Transition(records_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(uvs_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(normals_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(slot->left_out.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      };
      if (atlas_) to_scene_data.push_back(Transition(atlas_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
      list->ResourceBarrier(static_cast<UINT>(to_scene_data.size()), to_scene_data.data());

      // Places: static meshes keep theirs (records and ranges from the pools), units take this slot's turn. Leaves
      // without an opacity mask (an older addon, or the atlas full) stay opaque.
      struct Placement {
        uint32_t record = UINT32_MAX;
        uint32_t normal_offset = UINT32_MAX;
        uint32_t uv_offset = UINT32_MAX;
        bool masked = false;
      };
      std::vector<Placement> placements(capture_count);
      uint32_t dynamic_records = 0;
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        auto& placement = placements[index];
        const uint32_t triangles = capture.vertex_count / 3;
        if (triangles == 0 || capture.first_vertex + capture.vertex_count > TRANSFER_VERTICES) continue;
        placement.masked = (capture.flags & dlaa_bridge::RT_FOLIAGE) != 0 && version >= 6 && atlas_;
        // The frame's units in this slot's turn of the records and pools (grass alpha-tested: its UVs at its triangles'
        // place in the slot's UVs, ATN 2026-10-28: blocky grass shadows).
        if ((capture.flags & dlaa_bridge::RT_DYNAMIC) != 0) {
          placement.masked = placement.masked && dynamic_uvs_used + triangles * 3 <= dynamic_uvs_;
          // Smoke without its UVs would be opaque squares among the units: left out.
          if (!placement.masked && (capture.flags & dlaa_bridge::RT_SMOKE) != 0) {
            placement = {};
            continue;
          }
          if (dynamic_triangles_used + triangles > DYNAMIC_TRIANGLES || cluster_vertices + (triangles * 3) > TRANSFER_VERTICES) continue;
          cluster_vertices += triangles * 3;
          placement.record = STATIC_RECORDS + static_cast<uint32_t>(slot_index) * dlaa_bridge::RT_CAPTURES + dynamic_records++;
          placement.normal_offset = STATIC_TRIANGLES + static_cast<uint32_t>(slot_index) * DYNAMIC_TRIANGLES + dynamic_triangles_used;
          if (placement.masked) {
            placement.uv_offset = STATIC_UVS + static_cast<uint32_t>(slot_index) * dynamic_uvs_ + dynamic_uvs_used;
            dynamic_uvs_used += triangles * 3;
          }
          dynamic_triangles_used += triangles;
          continue;
        }
        if (free_records_.empty() || !free_triangles_.Take(triangles, &placement.normal_offset)) {
          placement = {};
          continue;
        }
        if (placement.masked && !free_uvs_.Take(triangles * 3, &placement.uv_offset)) {
          free_triangles_.Give(placement.normal_offset, triangles);
          placement = {};
          continue;
        }
        placement.record = free_records_.back();
        free_records_.pop_back();
      }
      // Each capture's first triangle in the scene data's count (captures start anywhere: placed draws' rows are four
      // texels an instance, so a capture after them is off the vertex texture's threes; read as threes, the grass's UVs
      // came from the wrong vertices, its alpha test changing every frame).
      uint32_t scene_triangles = 0;
      slot->capture_keys.resize(details_ ? capture_count : 0u);
      slot->capture_flags.resize(details_ ? capture_count : 0u);
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        const auto& placement = placements[index];
        const uint32_t slice = (version >= 6) ? shared.rt_capture_slices[index] : 0u;
        const std::array<uint32_t, 8> entry = {capture.first_vertex, capture.vertex_count / 3 * 3, placement.normal_offset, placement.uv_offset,
                                               placement.record, slice | (RecordFlags(capture, placement.masked, version) << 16),
                                               scene_triangles, capture.flags};
        std::memcpy(slot->table_data + index * 8, entry.data(), sizeof(entry));
        if (placement.record != UINT32_MAX) scene_triangles += capture.vertex_count / 3;
        if (details_) {
          slot->capture_keys[index] = capture.key;
          slot->capture_flags[index] = capture.flags;
        }
      }
      if (details_) {
        slot->capture_keys.insert(slot->capture_keys.end(), compact_keys.begin(), compact_keys.end());
        slot->capture_flags.insert(slot->capture_flags.end(), compact_flags.begin(), compact_flags.end());
      }
      // In rows of 65535 groups (D3D12's most along x: a frame of over 4.2 M triangles went past it).
      const std::array<uint32_t, 4> scene_dispatch = {capture_count, scene_triangles, 0, 0};
      const uint32_t scene_groups = (scene_triangles + 63) / 64;
      Mark(list, L"scene data");
      list->SetPipelineState(scene_data_pipeline_.Get());
      list->SetComputeRoot32BitConstants(ROOT_DISPATCH, 4, scene_dispatch.data(), 0);
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, vertex_buffer_->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, slot->left_out->GetGPUVirtualAddress());
      list->Dispatch(std::min(scene_groups, 65535u), (scene_groups + 65534) / 65535, 1);
      // The compact draws' clusters: their arguments and records, their triangles' normals and UVs (a group a cluster, in
      // rows of 65535).
      if (compact_drawn != 0) {
        const uint64_t vertices_address = vertex_buffer_->GetGPUVirtualAddress();
        const uint64_t sets_address = set_pool_->GetGPUVirtualAddress();
        const std::array<uint32_t, 8> compact_dispatch = {
            compact_drawn, compact_clusters, first_cluster_record, 0u, static_cast<uint32_t>(vertices_address),
            static_cast<uint32_t>(vertices_address >> 32), static_cast<uint32_t>(sets_address), static_cast<uint32_t>(sets_address >> 32),
        };
        const auto to_arguments = Transition(cluster_args_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1, &to_arguments);
        Mark(list, L"compact");
        list->SetPipelineState(compact_pipeline_.Get());
        list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(compact_dispatch.size()), compact_dispatch.data(), 0);
        const uint64_t templates_address = templates_->GetGPUVirtualAddress();
        const std::array<uint32_t, 8> templates_dispatch = {
            static_cast<uint32_t>(templates_address), static_cast<uint32_t>(templates_address >> 32), template_bytes_, templated_clusters,
            (cluster_room_ - built_limit_) * 32, sway_flags, std::bit_cast<uint32_t>(shared.rt_sway_range), std::bit_cast<uint32_t>(grass_range),
        };
        list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(templates_dispatch.size()), templates_dispatch.data(), 0);
        // The sway's view: the view-projection's rows 0, 1 and 3.
        list->SetComputeRoot32BitConstants(ROOT_BOUNCE, 8, shared.rt_view_projection, 8);
        list->SetComputeRoot32BitConstants(ROOT_BOUNCE, 4, shared.rt_view_projection + 12, 16);
        list->SetComputeRootShaderResourceView(ROOT_SCENE, sets_address);
        list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->compact->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, cluster_args_->GetGPUVirtualAddress());
        list->Dispatch(std::min(compact_clusters, 65535u), (compact_clusters + 65534) / 65535, 1);
        const auto arguments_written = Transition(cluster_args_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &arguments_written);
        list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, tlas_scratch_->GetGPUVirtualAddress());
      }
      const std::array<D3D12_RESOURCE_BARRIER, 5> scene_written = {
          Transition(records_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(uvs_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(normals_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(slot->left_out.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
      };
      list->ResourceBarrier(static_cast<UINT>(scene_written.size()), scene_written.data());
      if (details_) {
        list->CopyBufferRegion(slot->left_out_readback.Get(), 0, slot->left_out.Get(), 0, LEFT_OUT_BYTES);
      }
      const auto left_out_back = Transition(slot->left_out.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &left_out_back);
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }

      // Opacity micromaps (DXR 1.2) baked for leaves built two rounds of the slots back or more (their first compaction and
      // rows read: that slot's next trace, the third after with two requests a frame): their arrays built now from the bakes'
      // upload buffers (none when every triangle has a special index), their BLASes again below.
      struct MicromapArray {
        std::unique_ptr<MicromapBake> bake;
        ComPtr<ID3D12Resource> array;
        ComPtr<ID3D12Resource> inputs;  // upload: the data, descs, indices (read by the builds alone)
        uint64_t indices_offset = 0;
      };
      std::vector<MicromapArray> micromap_arrays;
      if (dxr12_ && !micromaps_baked_.empty()) {
        std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC> array_descs(micromaps_baked_.size());
        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> array_builds;
        std::vector<std::unique_ptr<MicromapBake>> set_bakes;  // their histograms read by the builds below
        uint64_t array_scratch = 0;
        for (std::unique_ptr<MicromapBake>& baked : micromaps_baked_) {
          ClusterSet* set = nullptr;
          if (baked->set) {
            const auto found = sets_.find(baked->key);
            if (found == sets_.end() || found->second.generation != baked->build) {
              baked.reset();
              continue;
            }
            set = &found->second;
          } else if (const auto mesh = meshes_.find(baked->key); mesh == meshes_.end() || mesh->second.build != baked->build) {
            releases_.push_back({.value = own_value_ + 1, .resource = std::move(baked->positions)});
            baked.reset();
            continue;
          }
          if (own_value_ + 1 < baked->value + (2 * SLOTS)) continue;
          // Its data, descs and indices: a GPU bake's where the bake left them (its request's shared outputs, kept until
          // this request's builds are past), a CPU bake's uploaded.
          ComPtr<ID3D12Resource> inputs;
          uint64_t indices_offset = 0;
          D3D12_GPU_VIRTUAL_ADDRESS data_address = 0;
          D3D12_GPU_VIRTUAL_ADDRESS descs_address = 0;
          if (baked->gpu) {
            inputs = baked->gpu_indices;
            indices_offset = baked->indices_offset;
            data_address = baked->gpu_arrays->GetGPUVirtualAddress() + baked->arrays_offset;
            descs_address = baked->gpu_descs->GetGPUVirtualAddress() + baked->descs_offset;
            releases_.push_back({.value = own_value_ + 1, .resource = baked->gpu_arrays});
            releases_.push_back({.value = own_value_ + 1, .resource = baked->gpu_descs});
          } else {
            const uint64_t descs_offset = Align(baked->data.size(), 256);
            indices_offset = Align(descs_offset + (baked->descs.size() * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)), 256);
            inputs = Buffer(indices_offset + (baked->indices.size() * 4), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                            D3D12_RESOURCE_STATE_GENERIC_READ);
            uint8_t* mapped = nullptr;
            if (!inputs || FAILED(inputs->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) continue;
            std::memcpy(mapped, baked->data.data(), baked->data.size());
            std::memcpy(mapped + descs_offset, baked->descs.data(), baked->descs.size() * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC));
            std::memcpy(mapped + indices_offset, baked->indices.data(), baked->indices.size() * 4);
            inputs->Unmap(0, nullptr);
            data_address = inputs->GetGPUVirtualAddress();
            descs_address = data_address + descs_offset;
          }
          ComPtr<ID3D12Resource> array;
          if (!baked->histogram.empty()) {
            D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC& array_desc = array_descs[array_builds.size()];
            array_desc = {
                .NumOmmHistogramEntries = static_cast<UINT>(baked->histogram.size()),
                .pOmmHistogram = baked->histogram.data(),
                .InputBuffer = data_address,
                .PerOmmDescs = {.StartAddress = descs_address, .StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)},
            };
            const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS array_inputs = {
                .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY,
                .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
                .NumDescs = 1,
                .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                .pOpacityMicromapArrayDesc = &array_desc,
            };
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
            device_->GetRaytracingAccelerationStructurePrebuildInfo(&array_inputs, &info);
            array = Buffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE,
                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            if (!array) continue;
            array_builds.push_back({.DestAccelerationStructureData = array->GetGPUVirtualAddress(), .Inputs = array_inputs,
                                    .ScratchAccelerationStructureData = array_scratch});
            array_scratch = Align(array_scratch + info.ScratchDataSizeInBytes, 256);
          }
          if (set != nullptr) {
            // Its templates again, with the micromaps (the old ones given back once the GPU is past this request); a set
            // whose triangles are all wholly opaque or clear keeps its templates (no array).
            if (array) {
              pending_set_frees_.push_back({.value = own_value_ + 1, .template_first = set->template_first,
                                            .templates = (set->template_first != UINT32_MAX) ? set->clusters : 0u});
              set->template_first = UINT32_MAX;
              // Past this request's followers too: the Light Bounce's and reflections' requests trace its scene.
              for (auto* kept : {std::addressof(set->micromaps), std::addressof(set->micromap_inputs)}) {
                if (*kept) releases_.push_back({.value = own_value_ + 1 + SLOTS, .resource = std::move(*kept)});
              }
              set->micromaps = std::move(array);
              set->micromap_inputs = std::move(inputs);
              set->micromap_indices = indices_offset;
              ++micromap_tally_[3];
            }
            set_bakes.push_back(std::move(baked));
            continue;
          }
          micromap_arrays.push_back({.bake = std::move(baked), .array = std::move(array), .inputs = std::move(inputs), .indices_offset = indices_offset});
        }
        std::erase(micromaps_baked_, nullptr);
        if (!array_builds.empty() && ensure_scratch(array_scratch)) {
          Mark(list, L"micromaps");
          for (auto& build : array_builds) {
            build.ScratchAccelerationStructureData += scratch_->GetGPUVirtualAddress();
            list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
          }
          list->ResourceBarrier(1, &uav_barrier);
        } else if (!array_builds.empty()) {
          std::erase_if(micromap_arrays, [](const MicromapArray& made) { return made.array.Get() != nullptr; });
        }
      }

      // The BLASes, built together: static ones once (for tracing, then compacted), the units' one refitted to this
      // request's poses while its draws are the same (rebuilt every UNITS_REBUILD_FRAMES or when they change).
      std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> triangle_descs(capture_count);
      std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(capture_count);
      std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> unit_geometries;
      std::vector<uint32_t> unit_layout;  // their vertices (the top bit: alpha-tested)
      // Each unit draw's first vertex, triangles, normals, record flags, UVs, atlas slice and flags (Mega Geometry's clusters).
      std::vector<std::array<uint32_t, 7>> unit_sources;
      uint32_t units_record = UINT32_MAX;
      bool units_masked = false;  // grass among them (not forced opaque)
      struct Build {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
        bool compact = false;
      };
      std::vector<Build> builds;
      uint64_t scratch_needed = 0;
      // A static BLAS (built for tracing, then compacted) of `geometry` among the builds: its buffer (null: no room) and
      // size.
      const auto build_static = [&](const D3D12_RAYTRACING_GEOMETRY_DESC* geometry, uint64_t* bytes) {
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                     | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
            .NumDescs = 1,
            .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
            .pGeometryDescs = geometry,
        };
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        BlasPlace blas = PlaceBlas(info.ResultDataMaxSizeInBytes, BUILD_POOL);
        if (blas.address != 0) {
          builds.push_back({.desc = {.DestAccelerationStructureData = blas.address, .Inputs = inputs,
                                     .ScratchAccelerationStructureData = scratch_needed},
                            .compact = true});
          scratch_needed = Align(scratch_needed + info.ScratchDataSizeInBytes, 256);
          *bytes = info.ResultDataMaxSizeInBytes;
        }
        return blas;
      };
      // New leaves whose micromaps are baked (with an addon's RT_MICROMAPS, while there is room): their positions, UVs and
      // mask slice copied after the builds.
      std::vector<MicromapRead> micromap_reads = std::move(set_micromap_reads);
      size_t micromap_room = (bake_micromaps_ && dxr12_ && (shared.rt_flags & dlaa_bridge::RT_MICROMAPS) != 0)
                                 ? MICROMAP_BAKES - std::min(MICROMAP_BAKES, micromap_reads_.size() + micromap_queue_.size()
                                                                                 + MicromapsBaking() + micromaps_baked_.size())
                                 : 0u;
      // Instanced captures' rows to read back (their first texel in the vertex buffer).
      std::vector<Slot::RowRead> row_copies;
      std::vector<uint32_t> row_sources;
      uint32_t rows_used = 0;
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        const auto& placement = placements[index];
        if (placement.record == UINT32_MAX) continue;
        triangle_descs[index] = {
            .IndexFormat = DXGI_FORMAT_UNKNOWN,
            .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
            .VertexCount = capture.vertex_count / 3 * 3,
            .VertexBuffer = {.StartAddress = vertex_buffer_->GetGPUVirtualAddress() + uint64_t{capture.first_vertex} * 16, .StrideInBytes = 16},
        };
        geometries[index] = {
            .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
            .Flags = placement.masked ? D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
            .Triangles = triangle_descs[index],
        };
        if ((capture.flags & dlaa_bridge::RT_DYNAMIC) != 0) {
          if (units_record == UINT32_MAX) units_record = placement.record;
          unit_geometries.push_back(geometries[index]);
          unit_layout.push_back((capture.vertex_count / 3 * 3) | (placement.masked ? 0x80000000u : 0u));
          unit_sources.push_back({capture.first_vertex, capture.vertex_count / 3, placement.normal_offset, RecordFlags(capture, placement.masked, version),
                                  placement.uv_offset, (version >= 6) ? shared.rt_capture_slices[index] : 0u, capture.flags});
          units_masked = units_masked || placement.masked;
          dynamic_triangles += capture.vertex_count / 3;
          if (placement.masked) grass_triangles += capture.vertex_count / 3;
          if ((capture.flags & dlaa_bridge::RT_TERRAIN) != 0) terrain_triangles += capture.vertex_count / 3;
          continue;
        }
        uint64_t bytes = 0;
        BlasPlace blas = build_static(&geometries[index], &bytes);
        if (blas.address == 0) {
          // No room: its place goes back now (nothing references it).
          free_records_.push_back(placement.record);
          free_triangles_.Give(placement.normal_offset, capture.vertex_count / 3);
          if (placement.uv_offset != UINT32_MAX) free_uvs_.Give(placement.uv_offset, capture.vertex_count / 3 * 3);
          continue;
        }
        Mesh& mesh = meshes_[capture.key];
        if (mesh.blas.address != 0) ReleaseMesh(&mesh);
        mesh = {.blas = std::move(blas), .build = ++builds_, .bytes = bytes, .drawn_frame = frame,
                .triangles = capture.vertex_count / 3, .record = placement.record, .normal_offset = placement.normal_offset,
                .uv_offset = placement.uv_offset, .terrain = (capture.flags & dlaa_bridge::RT_TERRAIN) != 0, .foliage = placement.masked,
                .grass = placement.masked && (capture.flags & dlaa_bridge::RT_GRASS) != 0,
                .solid_cut_out = placement.masked && (capture.flags & (dlaa_bridge::RT_GRASS | dlaa_bridge::RT_SOLID_CUT_OUT)) == dlaa_bridge::RT_SOLID_CUT_OUT};
        if (version >= 14 && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0) {
          mesh.placed = true;
          built_placed.push_back({.key = capture.key, .first_vertex = capture.first_vertex + capture.vertex_count, .instances = capture.instances});
        } else if (version >= 10 && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0 && capture.instances > 1
                   && rows_used + capture.instances <= ROWS_READBACK) {
          row_copies.push_back({.key = capture.key, .build = mesh.build, .offset = rows_used, .count = capture.instances});
          row_sources.push_back(capture.first_vertex + capture.vertex_count);
          rows_used += capture.instances;
        }
        if (placement.masked && micromap_room != 0 && mesh.triangles <= MICROMAP_TRIANGLES) {
          micromap_reads.push_back({.key = capture.key, .build = mesh.build, .first_vertex = capture.first_vertex, .triangles = mesh.triangles,
                                    .uv_offset = placement.uv_offset, .slice = shared.rt_capture_slices[index], .grass = mesh.grass,
                                    .gpu = gpu_bakes});
          --micromap_room;
        }
        slot->compactions.emplace_back(capture.key, mesh.build);
        ++built;
      }
      // The leaves whose micromaps were baked: their BLAS built again with them from their kept positions (a new build,
      // compacted in turn); the old one released once the GPU is past this request.
      std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> micromap_triangles(micromap_arrays.size());
      std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> micromap_linkages(micromap_arrays.size());
      std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> micromap_geometries(micromap_arrays.size());
      for (size_t index = 0; index < micromap_arrays.size(); ++index) {
        MicromapArray& made = micromap_arrays[index];
        const auto found = meshes_.find(made.bake->key);
        if (found == meshes_.end() || found->second.build != made.bake->build) continue;
        Mesh& mesh = found->second;
        micromap_triangles[index] = {
            .IndexFormat = DXGI_FORMAT_UNKNOWN,
            .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
            .VertexCount = mesh.triangles * 3,
            .VertexBuffer = {.StartAddress = made.bake->positions->GetGPUVirtualAddress() + made.bake->positions_offset, .StrideInBytes = 16},
        };
        micromap_linkages[index] = {
            .OpacityMicromapIndexBuffer = {.StartAddress = made.inputs->GetGPUVirtualAddress() + made.indices_offset, .StrideInBytes = 4},
            .OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT,
            .OpacityMicromapBaseLocation = 0,
            .OpacityMicromapArray = made.array ? made.array->GetGPUVirtualAddress() : 0,
        };
        micromap_geometries[index] = {
            .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES,
            .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION,
            .OmmTriangles = {.pTriangles = &micromap_triangles[index], .pOmmLinkage = &micromap_linkages[index]},
        };
        uint64_t bytes = 0;
        BlasPlace blas = build_static(&micromap_geometries[index], &bytes);
        if (blas.address == 0) continue;
        if (details_) {
          slot->micromap_builds += std::format("; micromap BLAS {} triangles, {} KB, {} levels, {}", mesh.triangles,
                                               made.array ? (made.array->GetDesc().Width >> 10) : 0u, made.bake->histogram.size(),
                                               made.bake->gpu ? "GPU bake" : "CPU bake");
        }
        ReleaseBlas(&mesh.blas);
        mesh.blas = std::move(blas);
        mesh.bytes = bytes;
        mesh.build = ++builds_;
        mesh.micromaps = std::move(made.array);
        slot->compactions.emplace_back(made.bake->key, mesh.build);
        ++micromaps_built;
      }
      // What they did not keep (their positions and inputs; the arrays of leaves gone or without room) goes once the GPU
      // is past this request.
      for (MicromapArray& made : micromap_arrays) {
        for (ComPtr<ID3D12Resource>* resource : {std::addressof(made.array), std::addressof(made.inputs), std::addressof(made.bake->positions)}) {
          if (*resource) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*resource)});
        }
      }
      // The smoke (the addon's last capture: rt_smoke's quads) in a BLAS of its own, its instance met only by the rays
      // toward the sun: the ambient's, the local lights' and the paths' rays went through its particles, an alpha test each
      // and their traversal (at 4K through 1000 of them 30-40 ms a frame each). Sprite soldiers and far trees (RT_SUN_ONLY)
      // right before it with it. Smoke before other dynamic captures (not their tail) stays in the units'. Never Mega
      // Geometry's clusters: made from the particles in their list's order, each spanned the battlefield and every ray
      // went into hundreds of them (the trace 3.6 -> 194 ms as an arrow volley's smoke came, ATN's 2026-11-01 stress
      // battle); the BLAS build sorts them by place.
      const auto sun_alone = [](const std::array<uint32_t, 7>& source) {
        return (source[3] & RECORD_SMOKE) != 0 || (source[6] & dlaa_bridge::RT_SUN_ONLY) != 0;
      };
      size_t unit_count = unit_sources.size();
      while (unit_count != 0 && sun_alone(unit_sources[unit_count - 1])) {
        --unit_count;
      }
      const size_t smoke_count = unit_sources.size() - unit_count;
      // The smoke's records follow the units' captures' (one each, in capture order).
      smoke_record = units_record + static_cast<uint32_t>(unit_count);
      unit_layout.resize(unit_count);
      // The units: refitted in place while the request's unit draws are those of the last build (without Mega Geometry).
      if (unit_count != 0 && !clusters_) {
        const bool refit = units_blas_ && unit_layout == units_layout_ && units_refits_ < UNITS_REBUILD_FRAMES;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
                     | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
            .NumDescs = static_cast<UINT>(unit_count),
            .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
            .pGeometryDescs = unit_geometries.data(),
        };
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (!refit && info.ResultDataMaxSizeInBytes > units_bytes_) {
          if (units_blas_) releases_.push_back({.value = own_value_ + 1, .resource = std::move(units_blas_)});
          units_bytes_ = info.ResultDataMaxSizeInBytes + info.ResultDataMaxSizeInBytes / 4;
          units_blas_ = Buffer(units_bytes_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          if (!units_blas_) units_bytes_ = 0;
        }
        if (units_blas_) {
          if (refit) inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
          builds.push_back({.desc = {.DestAccelerationStructureData = units_blas_->GetGPUVirtualAddress(), .Inputs = inputs,
                                     .SourceAccelerationStructureData = refit ? units_blas_->GetGPUVirtualAddress() : 0,
                                     .ScratchAccelerationStructureData = scratch_needed}});
          scratch_needed = Align(scratch_needed + (refit ? info.UpdateScratchDataSizeInBytes : info.ScratchDataSizeInBytes), 256);
          units_refits_ = refit ? units_refits_ + 1 : 0;
          units_layout_ = std::move(unit_layout);
          units_built = true;
          units_refitted = refit;
        }
      }
      // The smoke: built anew each request (its particles change), for its rays' traversal.
      if (smoke_count != 0) {
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
            .NumDescs = static_cast<UINT>(smoke_count),
            .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
            .pGeometryDescs = unit_geometries.data() + unit_count,
        };
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (info.ResultDataMaxSizeInBytes > smoke_bytes_) {
          if (smoke_blas_) {
            releases_.push_back({.value = own_value_ + 1, .resource = std::move(smoke_blas_)});
          }
          smoke_bytes_ = info.ResultDataMaxSizeInBytes + (info.ResultDataMaxSizeInBytes / 4);
          smoke_blas_ = Buffer(smoke_bytes_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          if (!smoke_blas_) {
            smoke_bytes_ = 0;
          }
        }
        if (smoke_blas_) {
          builds.push_back({.desc = {.DestAccelerationStructureData = smoke_blas_->GetGPUVirtualAddress(), .Inputs = inputs,
                                     .ScratchAccelerationStructureData = scratch_needed}});
          scratch_needed = Align(scratch_needed + info.ScratchDataSizeInBytes, 256);
          smoke_built = true;
        }
      }
      if (!ensure_scratch(scratch_needed)) return skip("Ray tracing: the BLAS scratch allocation failed");
      Mark(list, L"BLAS");
      size_t compact_slot = 0;
      for (auto& build : builds) {
        build.desc.ScratchAccelerationStructureData += scratch_->GetGPUVirtualAddress();
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild = {
            .DestBuffer = build.compact ? slot->sizes->GetGPUVirtualAddress() + (compact_slot++) * sizeof(uint64_t) : 0,
            .InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE,
        };
        list->BuildRaytracingAccelerationStructure(&build.desc, build.compact ? 1 : 0, build.compact ? &postbuild : nullptr);
      }
#if defined(RENODX_NVAPI_CLUSTERS)
      // Mega Geometry: the units' captures split into clusters (CLAS) after the compact draws' (the compact pass wrote
      // theirs), built with one cluster BLAS from them all; each cluster a geometry whose record points at its normals.
      if (clusters_ && (!unit_sources.empty() || compact_clusters != 0)) {
        uint8_t* data = slot->cluster_data;
        auto* args = reinterpret_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS*>(data + CLUSTER_ARGS_OFFSET);
        auto* cluster_records = reinterpret_cast<std::array<uint32_t, 4>*>(data + CLUSTER_RECORDS_OFFSET);
        uint32_t cluster_count = compact_clusters;
        for (size_t source = 0; source < unit_count; ++source) {
          const auto& [first_vertex, unit_triangles, normal_offset, record_flags, uv_offset, slice, capture_flags] = unit_sources[source];
          const bool masked = (record_flags & RECORD_MASKED) != 0;
          const uint32_t cluster_triangles = (((capture_flags & dlaa_bridge::RT_SCATTERED) != 0) ? 2u : CLUSTER_TRIANGLES);
          built_wanted_ += (unit_triangles + cluster_triangles - 1) / cluster_triangles;
          for (uint32_t first = 0; first < unit_triangles && cluster_count - templated_clusters < built_limit_; first += cluster_triangles) {
            const uint32_t count = std::min(cluster_triangles, unit_triangles - first);
            auto& arg = args[cluster_count - templated_clusters];
            arg = {};
            arg.clusterId = cluster_count;
            arg.triangleCount = count;
            arg.vertexCount = count * 3;
            arg.indexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_8BIT;
            arg.baseGeometryIndexAndFlags = cluster_count
                                            | (masked ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
                                                      : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_OPAQUE);
            arg.indexBufferStride = 1;
            arg.vertexBufferStride = 16;
            arg.indexBuffer = cluster_indices_->GetGPUVirtualAddress();
            arg.vertexBuffer = vertex_buffer_->GetGPUVirtualAddress() + (uint64_t{first_vertex} + uint64_t{first} * 3) * 16;
            cluster_records[cluster_count] = {(masked ? uv_offset + first * 3 : UINT32_MAX), normal_offset + first, slice | (record_flags << 16),
                                              (masked ? std::bit_cast<uint32_t>(LEAF_ALPHA_CUTOFF) : 0u)};
            ++cluster_count;
          }
        }
        const uint32_t units_end = cluster_count;  // the smoke's run stays empty (its own BLAS)
        if ((cluster_count - templated_clusters) * 4 >= built_limit_ * 3) {
          clusters_crowded_ = true;
        }
        // The cluster BLASes' runs of the CLASes' addresses (CLUSTER_BLASES: the rest's before and after the grass's, the
        // grass's, the smoke's; without grass the rest's one).
        if (grass_first == grass_end) {
          grass_first = units_end;
          grass_end = units_end;
        }
        const std::array<std::pair<uint32_t, uint32_t>, CLUSTER_BLASES> blas_runs = {
            {{0u, grass_first}, {grass_first, grass_end}, {grass_end, units_end}, {units_end, cluster_count}}};
        // With the sway (rt_sway) the counts start at 0 in sway_counts_ and the lists are sway_lists_'s.
        const bool sway = sway_flags != 0;
        std::array<uint64_t, CLUSTER_BLASES> destinations = {};
        for (size_t blas = 0; blas < CLUSTER_BLASES; ++blas) {
          const NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS blas_args = {
              .clusterCount = sway ? 0u : blas_runs[blas].second - blas_runs[blas].first, .reserved = 0,
              .clusterVAs = (sway ? sway_lists_ : clas_addresses_)->GetGPUVirtualAddress() + (uint64_t{blas_runs[blas].first} * 8)};
          std::memcpy(data + CLUSTER_BLAS_ARGS_OFFSET + (blas * 16), &blas_args, sizeof(blas_args));
          destinations[blas] = cluster_blases_[blas]->GetGPUVirtualAddress();
        }
        const std::array<uint32_t, 2> clas_counts = {sway ? 0u : cluster_count - templated_clusters, sway ? 0u : templated_clusters};
        std::memcpy(data + CLUSTER_COUNT_OFFSET, clas_counts.data(), sizeof(clas_counts));
        std::memcpy(data + CLUSTER_DESTINATION_OFFSET, destinations.data(), sizeof(destinations));
        // The captures' clusters' arguments and records, the BLAS's destination (and once the indices) copied where the
        // builds read them.
        const uint64_t captured = cluster_count - compact_clusters;
        std::vector<D3D12_RESOURCE_BARRIER> to_copy = {
            Transition(records_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(cluster_destination_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(cluster_args_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
        if (captured != 0) {
          list->CopyBufferRegion(records_.Get(), (uint64_t{first_cluster_record} + compact_clusters) * 16, slot->clusters.Get(),
                                 CLUSTER_RECORDS_OFFSET + (uint64_t{compact_clusters} * 16), captured * 16);
          const uint64_t built_compact = compact_clusters - templated_clusters;
          list->CopyBufferRegion(cluster_args_.Get(), (uint64_t{cluster_room_ - built_limit_} * 32) + (built_compact * 64), slot->clusters.Get(),
                                 CLUSTER_ARGS_OFFSET + (built_compact * 64), captured * 64);
        }
        list->CopyBufferRegion(cluster_destination_.Get(), 0, slot->clusters.Get(), CLUSTER_DESTINATION_OFFSET, CLUSTER_BLASES * 8);
        if (!cluster_indices_written_) list->CopyBufferRegion(cluster_indices_.Get(), 0, slot->clusters.Get(), CLUSTER_INDICES_OFFSET, 256);
        for (auto& barrier : to_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        if (!cluster_indices_written_) {
          to_copy.push_back(Transition(cluster_indices_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          cluster_indices_written_ = true;
        }
        list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
        const uint64_t upload = slot->clusters->GetGPUVirtualAddress();
        const uint32_t templated_room = cluster_room_ - built_limit_;
        const auto sway_pass = [&](uint32_t mode, ID3D12Resource* read, ID3D12Resource* written) {
          const std::array<uint32_t, 8> sway_dispatch = {mode, cluster_count, templated_clusters, templated_room * 32, grass_first, grass_end, units_end, 0u};
          list->SetComputeRootSignature(root_signature_.Get());
          list->SetPipelineState(sway_pipeline_.Get());
          list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(sway_dispatch.size()), sway_dispatch.data(), 0);
          list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, read->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, written->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, sway_counts_->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, clas_addresses_->GetGPUVirtualAddress());
          list->Dispatch(mode == 3 ? 1 : (cluster_count + 63) / 64, 1, 1);
        };
        // The sway's kept clusters: their arguments packed into sway_arguments_ and counted (its counts from 0, the cluster
        // BLASes' list addresses: the upload's).
        if (sway) {
          Mark(list, L"sway");
          std::array<D3D12_RESOURCE_BARRIER, 2> to_pack = {
              Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
              Transition(sway_arguments_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
          list->ResourceBarrier(static_cast<UINT>(to_pack.size()), to_pack.data());
          list->CopyBufferRegion(sway_counts_.Get(), 0, slot->clusters.Get(), CLUSTER_BLAS_ARGS_OFFSET, (CLUSTER_BLASES * 16) + 8);
          const auto counting = Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          list->ResourceBarrier(1, &counting);
          // Which are kept, the kept before each group, then their arguments (rt_sway's MODE_MASKS, MODE_FIRSTS, MODE_KEPT).
          for (const uint32_t mode : {2u, 3u, 0u}) {
            sway_pass(mode, cluster_args_.Get(), sway_arguments_.Get());
            list->ResourceBarrier(1, &uav_barrier);
          }
          to_pack = {Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                     Transition(sway_arguments_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
          list->ResourceBarrier(static_cast<UINT>(to_pack.size()), to_pack.data());
        }
        // The instantiated CLASes into the first batch (their addresses first), those built from triangles after (with the
        // sway: the kept ones, by its counts, the built ones' addresses after the instantiated ones' room).
        const uint64_t arguments = (sway ? sway_arguments_ : cluster_args_)->GetGPUVirtualAddress();
        const uint64_t counts = sway ? sway_counts_->GetGPUVirtualAddress() + (CLUSTER_BLASES * 16) : upload + CLUSTER_COUNT_OFFSET;
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC instantiate_desc = {
            .inputs = instantiate_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = clas_->GetGPUVirtualAddress(),
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = clas_addresses_->GetGPUVirtualAddress(), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = arguments,
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS)},
            .indirectArgCount = counts + 4,
        };
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC clas_desc = {
            .inputs = clas_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = clas_->GetGPUVirtualAddress() + instantiated_bytes_,
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = clas_addresses_->GetGPUVirtualAddress() + (uint64_t{sway ? templated_room : templated_clusters} * 8),
                                        .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = arguments + (uint64_t{templated_room} * 32),
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS)},
            .indirectArgCount = counts,
        };
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC blas_desc = {
            .inputs = cluster_blas_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = 0,
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = cluster_destination_->GetGPUVirtualAddress(), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = sway ? sway_counts_->GetGPUVirtualAddress() : upload + CLUSTER_BLAS_ARGS_OFFSET,
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS)},
            .indirectArgCount = 0,
        };
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS instantiate_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &instantiate_desc};
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS clas_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &clas_desc};
        // Their build timed apart (a trace's slot leaves the paths' two stamps free).
        Mark(list, L"clusters");
        Stamp(list, slot_index * STAMPS + 4);
        bool clas_built = true;
        if (templated_clusters != 0) {
          clas_built = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &instantiate_params) == NVAPI_OK;
          list->ResourceBarrier(1, &uav_barrier);
        }
        if (cluster_count != templated_clusters) {
          clas_built = clas_built && NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &clas_params) == NVAPI_OK;
          list->ResourceBarrier(1, &uav_barrier);
        }
        // The sway's cluster BLASes' lists from the kept CLASes' addresses, counted in their arguments.
        if (sway) {
          const auto counting = Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          list->ResourceBarrier(1, &counting);
          sway_pass(1, sway_arguments_.Get(), sway_lists_.Get());
          const std::array<D3D12_RESOURCE_BARRIER, 2> counted = {
              Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE), uav_barrier};
          list->ResourceBarrier(static_cast<UINT>(counted.size()), counted.data());
          // Unused root descriptors still point at a buffer.
          list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
          for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
            list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
          }
        }
        // The cluster BLASes one after another (the same scratch).
        for (size_t blas = 0; blas < CLUSTER_BLASES; ++blas) {
          NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC run_desc = blas_desc;
          run_desc.destinationAddressArray.StartAddress += blas * 8;
          run_desc.indirectArgArray.StartAddress += blas * 16;
          const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS run_params = {
              .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &run_desc};
          cluster_built[blas] = clas_built && blas_runs[blas].second != blas_runs[blas].first
                                && NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &run_params) == NVAPI_OK;
          list->ResourceBarrier(1, &uav_barrier);
        }
        units_clustered = cluster_built[0] || cluster_built[1] || cluster_built[2];
        smoke_clustered = cluster_built[3];
        Stamp(list, slot_index * STAMPS + 5);
        slot_clustered_[slot_index] = true;
        clusters_built = cluster_count;
        templated_built = templated_clusters;
        units_record = first_cluster_record;
      }
#endif
      list->ResourceBarrier(1, &uav_barrier);
      units_record_ = units_record;
      units_masked_ = units_masked || compact_masked;
      // The new placed meshes' build rows (their first instance's) by record (v14); older addons' instanced captures' rows
      // into the slot's readback (four texels an instance), the CPU's once it comes round; the new leaves' micromap
      // inputs (their positions kept, their UVs and mask slice read back: a slice once a request).
      if (!row_copies.empty() || !built_placed.empty() || !micromap_reads.empty()) {
        std::vector<D3D12_RESOURCE_BARRIER> to_source = {
            Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
        };
        if (!built_placed.empty()) {
          to_source.push_back(Transition(build_rows_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
        }
        if (!micromap_reads.empty()) {
          to_source.push_back(Transition(uvs_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));
          to_source.push_back(Transition(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));
        }
        list->ResourceBarrier(static_cast<UINT>(to_source.size()), to_source.data());
        for (size_t index = 0; index < row_copies.size(); ++index) {
          list->CopyBufferRegion(slot->rows_readback.Get(), uint64_t{row_copies[index].offset} * 64, vertex_buffer_.Get(),
                                 uint64_t{row_sources[index]} * 16, uint64_t{row_copies[index].count} * 64);
        }
        for (const auto& built_rows : built_placed) {
          list->CopyBufferRegion(build_rows_.Get(), uint64_t{meshes_[built_rows.key].record} * 48, vertex_buffer_.Get(),
                                 uint64_t{built_rows.first_vertex} * 16, 48);
        }
        // One buffer of each kind for all of them, their places 256-byte aligned (each read made its own two or three: up to
        // 64 leaves a request, a few ms of allocations, hitches panning over new trees, ATN 2026-10-29).
        constexpr uint64_t MASK_BYTES = uint64_t{dlaa_bridge::RT_ATLAS_SIZE} * dlaa_bridge::RT_ATLAS_SIZE;
        std::map<uint32_t, uint64_t> masks;  // a slice's place in the masks read back
        uint64_t positions_bytes = 0;
        uint64_t uvs_bytes = 0;
        uint64_t gpu_uvs_bytes = 0;
        for (const MicromapRead& read : micromap_reads) {
          if (!read.set) positions_bytes = Align(positions_bytes + (uint64_t{read.triangles} * 48), 256);
          if (read.gpu) {
            gpu_uvs_bytes = Align(gpu_uvs_bytes + (uint64_t{read.triangles} * 12), 256);
            continue;
          }
          uvs_bytes = Align(uvs_bytes + (uint64_t{read.triangles} * 12), 256);
          masks.try_emplace(read.slice, masks.size() * MASK_BYTES);
        }
        std::vector<D3D12_RESOURCE_BARRIER> positions_copied;
        ComPtr<ID3D12Resource> positions;
        ComPtr<ID3D12Resource> uvs;
        ComPtr<ID3D12Resource> mask;
        ComPtr<ID3D12Resource> gpu_uvs;  // GPU Micromap Baking's copies (the sets' UVs are the request's)
        if (!micromap_reads.empty()) {
          if (positions_bytes != 0) {
            positions = Buffer(positions_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
          }
          if (gpu_uvs_bytes != 0) gpu_uvs = Buffer(gpu_uvs_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
          if (uvs_bytes != 0) {
            uvs = Buffer(uvs_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
            mask = Buffer(masks.size() * MASK_BYTES, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
          }
        }
        if ((positions_bytes != 0 && !positions) || (uvs_bytes != 0 && (!uvs || !mask)) || (gpu_uvs_bytes != 0 && !gpu_uvs)) micromap_reads.clear();
        if (!micromap_reads.empty()) {
          const D3D12_RESOURCE_DESC atlas_desc = atlas_->GetDesc();
          for (const auto& [slice, offset] : masks) {
            const D3D12_TEXTURE_COPY_LOCATION destination = {
                .pResource = mask.Get(),
                .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                .PlacedFootprint = {.Offset = offset,
                                    .Footprint = {.Format = atlas_desc.Format, .Width = dlaa_bridge::RT_ATLAS_SIZE, .Height = dlaa_bridge::RT_ATLAS_SIZE,
                                                  .Depth = 1, .RowPitch = dlaa_bridge::RT_ATLAS_SIZE}},
            };
            const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = atlas_.Get(),
                                                        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                        .SubresourceIndex = slice * atlas_desc.MipLevels};
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
          }
          for (const auto& copied : {positions, gpu_uvs}) {
            if (copied) {
              positions_copied.push_back(Transition(copied.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            }
          }
        }
        uint64_t positions_at = 0;
        uint64_t uvs_at = 0;
        uint64_t gpu_uvs_at = 0;
        for (const MicromapRead& read : micromap_reads) {
          auto bake = std::make_unique<MicromapBake>();
          *bake = {.key = read.key, .build = read.build, .value = read.gpu ? UINT64_MAX : own_value_ + 1, .triangles = read.triangles,
                   .set = read.set, .grass = read.grass, .positions = (read.set ? nullptr : positions), .positions_offset = positions_at,
                   .uvs = read.gpu ? gpu_uvs : uvs, .uvs_offset = read.gpu ? gpu_uvs_at : uvs_at, .mask = mask,
                   .mask_offset = read.gpu ? 0u : masks.at(read.slice), .gpu = read.gpu};
          if (!read.set) {
            list->CopyBufferRegion(positions.Get(), positions_at, vertex_buffer_.Get(), uint64_t{read.first_vertex} * 16, uint64_t{read.triangles} * 48);
            positions_at = Align(positions_at + (uint64_t{read.triangles} * 48), 256);
          }
          if (read.gpu) {
            list->CopyBufferRegion(gpu_uvs.Get(), gpu_uvs_at, uvs_.Get(), uint64_t{read.uv_offset} * 4, uint64_t{read.triangles} * 12);
            gpu_uvs_at = Align(gpu_uvs_at + (uint64_t{read.triangles} * 12), 256);
#if defined(RENODX_OMM_SDK)
            // Its mask into a texture of OmmGpu's no other waiting bake holds (one more when they all are).
            auto& gpu_masks = omm_gpu_.masks;
            std::vector<bool> held(gpu_masks.size());
            for (const auto& waiting : micromap_reads_) {
              if (waiting->gpu && waiting->value == UINT64_MAX) held[waiting->mask_index] = true;
            }
            bake->mask_index = static_cast<uint32_t>(std::ranges::find(held, false) - held.begin());
            if (bake->mask_index == gpu_masks.size()) {
              const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
              const D3D12_RESOURCE_DESC texture = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = dlaa_bridge::RT_ATLAS_SIZE,
                                                   .Height = dlaa_bridge::RT_ATLAS_SIZE, .DepthOrArraySize = 1, .MipLevels = 1,
                                                   .Format = DXGI_FORMAT_R8_UNORM, .SampleDesc = {.Count = 1}};
              ComPtr<ID3D12Resource> made;
              if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                          nullptr, IID_PPV_ARGS(made.GetAddressOf())))) {
                continue;
              }
              gpu_masks.push_back(std::move(made));
            }
            ID3D12Resource* gpu_mask = gpu_masks[bake->mask_index].Get();
            const auto to_copy = Transition(gpu_mask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            list->ResourceBarrier(1, &to_copy);
            const D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = gpu_mask, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                             .SubresourceIndex = 0};
            const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = atlas_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                        .SubresourceIndex = read.slice * atlas_->GetDesc().MipLevels};
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            const auto copied = Transition(gpu_mask, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            list->ResourceBarrier(1, &copied);
#endif
          } else {
            list->CopyBufferRegion(uvs.Get(), uvs_at, uvs_.Get(), uint64_t{read.uv_offset} * 4, uint64_t{read.triangles} * 12);
            uvs_at = Align(uvs_at + (uint64_t{read.triangles} * 12), 256);
          }
          micromap_reads_.push_back(std::move(bake));
        }
        for (auto& barrier : to_source) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        to_source.insert(to_source.end(), positions_copied.begin(), positions_copied.end());
        list->ResourceBarrier(static_cast<UINT>(to_source.size()), to_source.data());
        slot->row_reads = std::move(row_copies);
      }
      std::vector<D3D12_RESOURCE_BARRIER> after = {
          Transition(slot->sizes.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
      };
      if (atlas_) after.push_back(Transition(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
      list->ResourceBarrier(static_cast<UINT>(after.size()), after.data());
      list->CopyBufferRegion(slot->readback.Get(), 0, slot->sizes.Get(), 0, slot->compactions.size() * sizeof(uint64_t));
      const auto back = Transition(slot->sizes.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &back);
    }
    // What the frame drew.
    const uint32_t drawn_count = std::min(shared.rt_drawn_count, dlaa_bridge::RT_DRAWN);
    for (uint32_t index = 0; index < drawn_count; ++index) {
      if (const auto mesh = meshes_.find(shared.rt_drawn[index]); mesh != meshes_.end()) mesh->second.drawn_frame = frame;
    }
    // The scene (InstanceID: the record of its first geometry).
    UINT instance_count = 0;
    uint64_t triangles = 0;
    uint64_t bytes = 0;
    uint64_t separate_bytes = 0;  // what the static BLASes took as buffers of their own (64 KB at least each)
    for (const auto& [key, mesh] : meshes_) {
      bytes += mesh.bytes;
      separate_bytes += (mesh.bytes + 0xFFFFu) & ~uint64_t{0xFFFFu};
      if (mesh.placed || instance_count == MAX_INSTANCES) continue;
      const bool current = mesh.drawn_frame == frame;
      const bool recent = !mesh.terrain && mesh.drawn_frame + RECENT_FRAMES >= frame;
      if (!current && !recent) continue;
      if (mesh.transforms.empty()) {
        AddInstance(slot, &instance_count, mesh.blas.address, mesh.record, mesh.foliage, nullptr, mesh.record, InstanceMask(mesh));
      } else {
        for (uint32_t instance = 0; instance < mesh.transforms.size() && instance_count < MAX_INSTANCES; ++instance) {
          AddInstance(slot, &instance_count, mesh.blas.address, mesh.record, mesh.foliage, &mesh.transforms[instance], mesh.first_instance + instance,
                      InstanceMask(mesh));
        }
      }
      triangles += mesh.triangles;
    }
    // The pools' chunks logged when they change, against what separate buffers took.
    const auto held = [](const std::vector<BlasChunk>& chunks) { return std::ranges::count_if(chunks, [](const BlasChunk& chunk) { return chunk.buffer != nullptr; }); };
    if (const auto chunks = static_cast<size_t>(held(blas_pools_[BUILD_POOL]) + held(blas_pools_[KEPT_POOL])); details_ && chunks != blas_chunks_logged_) {
      blas_chunks_logged_ = chunks;
      slow_report_ += std::format("{}[TW ray tracing] Static BLASes: {} meshes, {} MB in {} pool chunks of {} MB (as buffers of their own {} MB).",
                                  (slow_report_.empty() ? "" : "\n"), meshes_.size(), bytes >> 20, chunks, BLAS_CHUNK_BYTES >> 20,
                                  separate_bytes >> 20);
    }
    if (units_clustered) {
      // The rest's cluster BLASes (solid but for swaying leaves) and the grass's.
      for (const size_t blas : {size_t{0}, size_t{1}, size_t{2}}) {
        if (!cluster_built[blas] || instance_count >= MAX_INSTANCES) continue;
        AddInstance(slot, &instance_count, cluster_blases_[blas]->GetGPUVirtualAddress(), units_record_, blas == 1 || units_masked_, nullptr,
                    MAX_INSTANCES, (blas == 1) ? GRASS_SWAY_INSTANCE : SOLID_INSTANCE);
      }
      triangles += dynamic_triangles;
    } else if (units_built && instance_count < MAX_INSTANCES) {
      AddInstance(slot, &instance_count, units_blas_->GetGPUVirtualAddress(), units_record_, units_masked_, nullptr, MAX_INSTANCES, SOLID_INSTANCE);
      triangles += dynamic_triangles;
    }
    // The smoke in a TLAS of its own (built after the scene's), its instance the slot's last desc (the CPU's stay far below):
    // its rays (rt_scene.hlsli SmokeTransmittance) walked the scene's TLAS too, ~150 K placed instances in ATN's archer
    // battle (2026-11-01), and its overlapping particles widened the scene's for every ray.
    const bool smoke_scene = (smoke_built || smoke_clustered) && smoke_tlas_ && smoke_tlas_scratch_ && instance_count < MAX_INSTANCES - 1;
    if (smoke_scene) {
      UINT smoke_desc = MAX_INSTANCES - 1;
      AddInstance(slot, &smoke_desc, (smoke_clustered ? cluster_blases_[3] : smoke_blas_)->GetGPUVirtualAddress(), smoke_record, true, nullptr,
                  MAX_INSTANCES, SMOKE_INSTANCE);
    }
    // Placed meshes (v14; not with the partitioned TLAS): rt_place writes their descs after the CPU's instances, this
    // request's draws' (the new captures', then the placements') and those kept from the last dispatch; the TLAS takes
    // place_end descs (past the written ones inactive).
    D3D12_GPU_VIRTUAL_ADDRESS instance_descs = slot->instances->GetGPUVirtualAddress();
    UINT tlas_instances = instance_count;
    uint32_t placed_drawn = 0;
    if (ptlas_ == nullptr && std::ranges::any_of(meshes_, [](const auto& entry) { return entry.second.placed; })) {
      // The records' BLAS addresses (bit 0: drawn moving now, bit 1: grass, bit 2: drawn this frame: from the drawn list
      // when it holds the placed meshes (RT_DRAWN_PLACED) and is not full, else every one; bit 3: all its instances in this
      // request's placements), then the jobs.
      auto* record_blas = reinterpret_cast<uint64_t*>(slot->jobs_data);
      std::fill_n(record_blas, STATIC_RECORDS, uint64_t{0});
      const bool drawn_known = (shared.rt_flags & dlaa_bridge::RT_DRAWN_PLACED) != 0 && drawn_count < dlaa_bridge::RT_DRAWN;
      for (const auto& [key, mesh] : meshes_) {
        if (mesh.placed) {
          record_blas[mesh.record] = mesh.blas.address | (mesh.grass ? 2u : 0u) | ((!drawn_known || mesh.drawn_frame == frame) ? 4u : 0u) | (mesh.moved ? 16u : 0u)
                                     | (mesh.solid_cut_out ? 32u : 0u);
        }
      }
      uint32_t job_count = 0;
      const auto place = [&](const dlaa_bridge::RtPlacement& draw) {
        const auto found = meshes_.find(draw.key);
        if (found == meshes_.end() || !found->second.placed || draw.instances == 0 || job_count == PLACE_JOBS
            || uint64_t{draw.first_vertex} + (uint64_t{draw.instances} * 4u) > vertex_end) {
          return;
        }
        Mesh& mesh = found->second;
        mesh.drawn_frame = frame;
        record_blas[mesh.record] |= 4u;
        if ((draw.flags & dlaa_bridge::RT_PLACE_MOVING) != 0) {
          mesh.moved = true;
          record_blas[mesh.record] |= 17u;
        }
        if ((draw.flags & dlaa_bridge::RT_PLACE_WHOLE) != 0) record_blas[mesh.record] |= 8u;
        const std::array<uint32_t, 8> job = {placed_drawn, draw.instances, draw.first_vertex,
                                             (mesh.foliage ? PLACE_FOLIAGE : 0u) | ((draw.flags & dlaa_bridge::RT_PLACE_HIDDEN) != 0 ? PLACE_HIDDEN : 0u)
                                                 | (mesh.grass ? PLACE_GRASS : 0u) | (mesh.solid_cut_out ? PLACE_CUT_OUT : 0u),
                                             mesh.record, 0u, 0u, 0u};
        std::memcpy(slot->jobs_data + PLACE_TABLE_BYTES + uint64_t{job_count} * 32, job.data(), sizeof(job));
        ++job_count;
        placed_drawn += draw.instances;
        triangles += uint64_t{mesh.triangles} * draw.instances;
      };
      for (const auto& built_rows : built_placed) place(built_rows);
      uint32_t placed_main = 0;
      for (const bool cascade : {false, true}) {
        if (cascade) placed_main = placed_drawn;
        for (uint32_t index = 0; index < placement_count; ++index) {
          if (((shared.rt_placements[index].flags & dlaa_bridge::RT_PLACE_CASCADE) != 0) == cascade) place(shared.rt_placements[index]);
        }
      }
      // All this TLAS can hold: the CPU's, this request's drawn and the last dispatch's descs (the kept ones come from those,
      // so none is cut); no clear and no build past them.
      const auto place_end = static_cast<uint32_t>(std::min<uint64_t>(uint64_t{instance_count} + placed_drawn + placed_written_, placed_capacity_));
      ID3D12Resource* written = placed_instances_[1 - placed_last_].Get();
      const auto to_copy = Transition(written, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &to_copy);
      if (instance_count != 0) {
        list->CopyBufferRegion(written, 0, slot->instances.Get(), 0, uint64_t{instance_count} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
      }
      const auto to_place = Transition(written, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &to_place);
      Mark(list, L"place");
      list->SetPipelineState(place_pipeline_.Get());
      list->SetComputeRootShaderResourceView(ROOT_SCENE, placed_instances_[placed_last_]->GetGPUVirtualAddress());
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->jobs->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, written->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, build_rows_->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, placed_state_->GetGPUVirtualAddress());
      // The camera (b2: grass outside the view stays out) and the sway's flags and range, then clear, drawn, kept (rt_place's
      // modes). Grass keeps its alpha tests (v108 took its micromaps as 2-state: a micro-triangle part blade, part gap went
      // solid, fields black in ATN's debug31; Fast Leaf Shadows still does that for every ray).
      // rt_place's PLACE_ONE_AT_A_PLACE beside the sway's, then Grass Ray Tracing Range (grass past it stays out too).
      const std::array<uint32_t, 4> place_sway = {sway_flags | (((shared.rt_flags & dlaa_bridge::RT_ONE_AT_A_PLACE) != 0) ? 4u : 0u),
                                                  std::bit_cast<uint32_t>(shared.rt_sway_range), placed_capacity_ * 2,
                                                  std::bit_cast<uint32_t>(grass_range)};
      list->SetComputeRoot32BitConstants(ROOT_BOUNCE, 16, shared.rt_view_projection, 0);
      list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(place_sway.size()), place_sway.data(), 16);
      // Drawn twice: the shadow pass's rows after a barrier, so their dedupe finds the main pass's.
      for (const auto& [mode, threads, first] : {std::tuple{0u, std::max(place_end, placed_capacity_ / 2), instance_count}, std::tuple{1u, placed_main, 0u},
                                                 std::tuple{1u, placed_drawn - placed_main, placed_main}, std::tuple{2u, place_end, instance_count}}) {
        // Clearing also takes the dedupe table, four entries a thread.
        if (threads == 0 || (mode == 2u && (shared.rt_flags & dlaa_bridge::RT_NO_KEPT) != 0)) continue;
        const std::array<uint32_t, 8> place_dispatch = {mode, job_count, placed_drawn, first, frame, place_end,
                                                        (version >= 24 && shared.rt_kept_frames != 0) ? shared.rt_kept_frames : RECENT_FRAMES,
                                                        STATIC_RECORDS};
        list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(place_dispatch.size()), place_dispatch.data(), 0);
        // Rows of 65535 groups (D3D12's most along x; rt_place's index).
        const uint32_t groups = (threads + 63) / 64;
        list->Dispatch(std::min(groups, 65535u), (groups + 65534) / 65535, 1);
        list->ResourceBarrier(1, &uav_barrier);
      }
      // The counter for the next trace (its room above, its place_end).
      const auto to_read = Transition(placed_state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->ResourceBarrier(1, &to_read);
      list->CopyBufferRegion(slot->left_out_readback.Get(), LEFT_OUT_BYTES, placed_state_.Get(), 0, sizeof(uint32_t));
      const auto read_done = Transition(placed_state_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &read_done);
      placed_slot_ = slot_index;
      const auto to_build = Transition(written, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      list->ResourceBarrier(1, &to_build);
      // Unused root descriptors still point at a buffer.
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }
      instance_descs = written->GetGPUVirtualAddress();
      tlas_instances = place_end;
      placed_last_ = 1 - placed_last_;
    }
    if (vertex_end != 0) {
      const auto vertices_written = Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &vertices_written);
    }
    uint32_t ptlas_changes = 0;
    Stamp(list, slot_index * STAMPS + 1);
#if defined(RENODX_NVAPI_CLUSTERS)
    if (ptlas_ != nullptr) {
      // The partitioned TLAS: this request's instances by their place (InstanceContributionToHitGroupIndex); a place whose
      // BLAS, InstanceID, flags or transform changed is written, one last request had and this one not written inactive.
      const uint32_t stamp = ++ptlas_stamp_;
      std::vector<uint32_t> places;
      places.reserve(instance_count);
      for (UINT index = 0; index < instance_count; ++index) {
        const D3D12_RAYTRACING_INSTANCE_DESC& instance = slot->instance_data[index];
        const uint32_t at = std::min<uint32_t>(instance.InstanceContributionToHitGroupIndex, PTLAS_INSTANCES - 1);
        PtlasInstance& want = ptlas_wanted_[at];
        want = {.blas = instance.AccelerationStructure,
                .id = instance.InstanceID,
                .flags = NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE
                         | (((instance.Flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) != 0)
                                ? NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_INSTANCE_FLAG_FORCE_OPAQUE
                                : 0u),
                .stamp = stamp};
        std::memcpy(want.transform.data(), instance.Transform, sizeof(want.transform));
        places.push_back(at);
      }
      auto* writes = reinterpret_cast<NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_WRITE_INSTANCE*>(slot->ptlas_data + PTLAS_WRITES_OFFSET);
      uint32_t write_count = 0;
      const auto write_place = [&](uint32_t at) {
        PtlasInstance& written = ptlas_written_[at];
        const PtlasInstance& want = ptlas_wanted_[at];
        const bool active = want.stamp == stamp;
        const bool changed = active ? (written.blas != want.blas || written.flags != want.flags || written.id != want.id
                                       || written.transform != want.transform)
                                    : (written.flags != UINT32_MAX && written.blas != 0);
        if (!changed) return;
        auto& write = writes[write_count++];
        write = {};
        if (active) {
          std::memcpy(write.transform, want.transform.data(), sizeof(write.transform));
          write.instanceID = want.id;
          write.instanceFlags = want.flags;
          write.accelerationStructure = want.blas;
          written = want;
        } else {
          write.transform[0][0] = 1.f;
          write.transform[1][1] = 1.f;
          write.transform[2][2] = 1.f;
          written = {.blas = 0, .id = 0, .flags = 0};
        }
        write.instanceMask = 0xFF;
        write.instanceIndex = at;
        write.partitionIndex = at / PTLAS_PARTITION_SIZE;
      };
      for (const uint32_t at : places) write_place(at);
      for (const uint32_t at : ptlas_places_) {
        if (ptlas_wanted_[at].stamp != stamp) write_place(at);
      }
      ptlas_places_ = std::move(places);
      ptlas_changes = write_count;
      auto* operations = reinterpret_cast<NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP*>(slot->ptlas_data + PTLAS_OPS_OFFSET);
      const uint64_t upload = slot->ptlas_device->GetGPUVirtualAddress();
      uint32_t operation_count = 0;
      if (write_count != 0) {
        operations[operation_count++] = {.type = NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_WRITE_INSTANCE, .count = write_count,
                                         .data = {.StartAddress = upload + PTLAS_WRITES_OFFSET,
                                                  .StrideInBytes = sizeof(NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_WRITE_INSTANCE)}};
      }
      std::memcpy(slot->ptlas_data + PTLAS_COUNT_OFFSET, &operation_count, sizeof(operation_count));
      if (operation_count != 0 || !ptlas_built_) {
        const auto copy = [&](uint64_t offset, uint64_t bytes) {
          if (bytes != 0) list->CopyBufferRegion(slot->ptlas_device.Get(), offset, slot->ptlas.Get(), offset, bytes);
        };
        copy(PTLAS_WRITES_OFFSET, uint64_t{write_count} * sizeof(NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_WRITE_INSTANCE));
        copy(PTLAS_OPS_OFFSET, PTLAS_UPLOAD_BYTES - PTLAS_OPS_OFFSET);
        const auto to_read = Transition(slot->ptlas_device.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &to_read);
        ID3D12Resource* next = ptlas_buffers_[1 - ptlas_current_].Get();
        const NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_DESC desc = {
            .inputs = ptlas_inputs_,
            .srcAccelerationStructureData = ptlas_built_ ? ptlas_->GetGPUVirtualAddress() : 0,
            .destAccelerationStructureData = next->GetGPUVirtualAddress(),
            .scratchAccelerationStructureData = ptlas_scratch_->GetGPUVirtualAddress(),
            .indirectOpCount = upload + PTLAS_COUNT_OFFSET,
            .indirectOps = upload + PTLAS_OPS_OFFSET,
        };
        const NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS params = {.version = NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS_VER,
                                                                                .pDesc = &desc};
        if (NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect(list, &params) != NVAPI_OK) return skip("Ray tracing: the partitioned TLAS failed");
        const auto to_copy = Transition(slot->ptlas_device.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &to_copy);
        ptlas_current_ = 1 - ptlas_current_;
        ptlas_ = next;
        ptlas_built_ = true;
      }
    }
#endif
    if (ptlas_ == nullptr) {
      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_build = {
          .DestAccelerationStructureData = tlas_->GetGPUVirtualAddress(),
          .Inputs = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
                     .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
                     .NumDescs = tlas_instances,
                     .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                     .InstanceDescs = instance_descs},
          .ScratchAccelerationStructureData = tlas_scratch_->GetGPUVirtualAddress(),
      };
      Mark(list, L"TLAS");
      list->BuildRaytracingAccelerationStructure(&top_build, 0, nullptr);
    }
    if (smoke_scene) {
      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC smoke_build = {
          .DestAccelerationStructureData = smoke_tlas_->GetGPUVirtualAddress(),
          .Inputs = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
                     .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
                     .NumDescs = 1,
                     .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                     .InstanceDescs = slot->instances->GetGPUVirtualAddress() + (uint64_t{MAX_INSTANCES - 1} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC))},
          .ScratchAccelerationStructureData = smoke_tlas_scratch_->GetGPUVirtualAddress(),
      };
      Mark(list, L"smoke TLAS");
      list->BuildRaytracingAccelerationStructure(&smoke_build, 0, nullptr);
    }
    Stamp(list, slot_index * STAMPS + 2);
    // The shared textures: COMMON between the APIs, read or written here.
    std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> textures = {
        {positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {sun_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {visibility_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
        {ambient_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
    };
    if (atlas_) textures.emplace_back(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // The local lights (v9): their positions and reaches to b4, their lit bits out.
    const uint32_t light_count = SharedLightCount(shared, version);
    const bool local_lights = (shared.rt_flags & dlaa_bridge::RT_LOCAL_LIGHTS) != 0 && light_mask_ && light_count != 0;
    // The fog shadows (v11): a quarter of the render pixels each way.
    const bool fog = (shared.rt_flags & dlaa_bridge::RT_FOG_SHADOWS) != 0 && fog_;
    if (fog) textures.emplace_back(fog_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (fog && fog_lights_ && (shared.rt_flags & dlaa_bridge::RT_FOG_LIGHTS) != 0) {
      textures.emplace_back(fog_lights_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    // Coloured Shadows (v19, with Light Through Leaves): the sun's filter through cloth and sails, their colour from Ray
    // Reconstruction's diffuse albedo guide (last frame's: written after this frame's sun draws) where the screen shows them.
    constexpr uint32_t COLOURED = dlaa_bridge::RT_COLOURED_SHADOWS | dlaa_bridge::RT_LEAF_LIGHT;
    const bool tint = tint_ && version >= 19 && (shared.rt_flags & COLOURED) == COLOURED;
    const bool tint_guides = tint && guide_albedo_ && shared.rt_guides != 0;
    if (tint) textures.emplace_back(tint_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (tint_guides) textures.emplace_back(guide_albedo_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Their places in the last trace's list (the addon orders them by importance each frame; the same position and
    // reach: the same light), a byte each after the colours (255: new).
    std::array<uint8_t, dlaa_bridge::RT_ALL_LIGHTS> light_previous = {};
    if (local_lights) {
      textures.emplace_back(light_mask_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      // With their colours for the fog's torch and fire light (rt_fog; the paths' requests copy them too).
      CopyLights(shared, light_count, slot->light_data);
      for (uint32_t light = 0; light < light_count; ++light) {
        const float* position = SharedLight(shared, light, false);
        const auto found = std::ranges::find_if(previous_lights_.begin(), previous_lights_.begin() + previous_light_count_,
                                                [&](const std::array<float, 4>& other) {
                                                  return std::equal(other.begin(), other.end(), position);
                                                });
        light_previous[light] = (found != previous_lights_.begin() + previous_light_count_)
                                    ? static_cast<uint8_t>(found - previous_lights_.begin())
                                    : uint8_t{255};
      }
      std::memcpy(slot->light_data + LIGHT_PREVIOUS_OFFSET, light_previous.data(), light_previous.size());
    }
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {uav_barrier};
    for (const auto& [resource, used] : textures) {
      barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootShaderResourceView(ROOT_SCENE, (ptlas_ != nullptr ? ptlas_ : tlas_.Get())->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(ROOT_SMOKE, (smoke_scene ? smoke_tlas_.Get() : (ptlas_ != nullptr ? ptlas_ : tlas_.Get()))->GetGPUVirtualAddress());
    const float softness = (version >= 6) ? std::clamp(shared.rt_sun_softness, 0.f, 5.000012f) : 0.f;
    const bool soft = (shared.rt_flags & dlaa_bridge::RT_SUN_SHADOWS) != 0 && softness > 0.01f;
    // Under Ray Reconstruction no soft-shadow filter (rt_trace jitters its sun ray). AO only reaches RR multiplied into the
    // game's colour, and RR is no AO denoiser: the ambient keeps its history (the harness's RR test left 19-23% of the raw
    // 4-ray AO's noise, a third of that with it), or REBLUR cleans it first when the addon asks (RT_AO_REBLUR).
    const bool reconstructed = (shared.rt_flags & dlaa_bridge::RT_RECONSTRUCTED) != 0;
    const bool ambient_by_nrd = (shared.rt_flags & (reconstructed ? dlaa_bridge::RT_AO_REBLUR : dlaa_bridge::RT_NRD)) != 0;
    // The Denoiser setting at NRD: SIGMA in place of the soft-shadow filter, REBLUR in place of the ambient's history. Any
    // signal unused for a while gives its memory back.
    bool nrd_sun = false;
    bool nrd_ambient = false;
#if defined(RENODX_NRD)
    for (NrdSignal* signal : {&nrd_sun_, &nrd_ambient_, &nrd_bounce_, &nrd_reflections_}) {
      if (signal->denoiser && signal->used_frame + NRD_RELEASE_FRAMES < frame) {
        if (!WaitFor(own_value_)) break;
        signal->denoiser.reset();
        slow_report_ += std::format("{}[TW ray tracing] An NRD denoiser released (unused for {} frames).", slow_report_.empty() ? "" : "\n", NRD_RELEASE_FRAMES);
      }
    }
    if (ambient_by_nrd) {
      const auto nrd_width = static_cast<uint32_t>(visibility_->GetDesc().Width);
      const uint32_t nrd_height = visibility_->GetDesc().Height;
      static constexpr std::array<NrdTexture, 5> SUN_TEXTURES = {{
          {nrd::ResourceType::IN_VIEWZ, DXGI_FORMAT_R32_FLOAT, NRD_SUN_VIEW_Z},
          {nrd::ResourceType::IN_NORMAL_ROUGHNESS, DXGI_FORMAT_R10G10B10A2_UNORM, NRD_SUN_NORMAL_ROUGHNESS},
          {nrd::ResourceType::IN_PENUMBRA, DXGI_FORMAT_R16_FLOAT, NRD_SUN_PENUMBRA},
          {nrd::ResourceType::IN_TRANSLUCENCY, DXGI_FORMAT_R8G8B8A8_UNORM, NRD_SUN_TRANSLUCENCY},
          {nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY, DXGI_FORMAT_R8G8B8A8_UNORM, 0},
      }};
      static constexpr std::array<NrdTexture, 4> AMBIENT_TEXTURES = {{
          {nrd::ResourceType::IN_VIEWZ, DXGI_FORMAT_R32_FLOAT, NRD_AMBIENT_VIEW_Z},
          {nrd::ResourceType::IN_NORMAL_ROUGHNESS, DXGI_FORMAT_R10G10B10A2_UNORM, NRD_AMBIENT_NORMAL_ROUGHNESS},
          {nrd::ResourceType::IN_DIFF_HITDIST, DXGI_FORMAT_R8_UNORM, NRD_AMBIENT_SIGNAL},
          {nrd::ResourceType::OUT_DIFF_HITDIST, DXGI_FORMAT_R8_UNORM, 0},
      }};
      nrd_sun = !reconstructed && soft && EnsureNrd(&nrd_sun_, nrd::Denoiser::SIGMA_SHADOW_TRANSLUCENCY, "NRD SIGMA", {RT_NRD_FRONT_SUN, sizeof(RT_NRD_FRONT_SUN)},
                                  {RT_NRD_FINISH_SUN, sizeof(RT_NRD_FINISH_SUN)}, nrd_width, nrd_height, SUN_TEXTURES, frame);
      nrd_ambient = (shared.rt_flags & dlaa_bridge::RT_AMBIENT_OCCLUSION) != 0
                    && EnsureNrd(&nrd_ambient_, nrd::Denoiser::REBLUR_DIFFUSE_OCCLUSION, "NRD REBLUR (AO)", {RT_NRD_FRONT_AMBIENT, sizeof(RT_NRD_FRONT_AMBIENT)},
                                 {RT_NRD_FINISH_AMBIENT, sizeof(RT_NRD_FINISH_AMBIENT)}, nrd_width, nrd_height, AMBIENT_TEXTURES, frame);
    }
#endif
    // Torch and Fire Light in the Light Bounce (helper111): the paths take the first 32 lights; rt_lights leaves them dark
    // (only after a bounce that carried them), or with RT_LIGHT_RATIO (Torch and Fire Shadows from the Light Bounce) writes
    // the last bounce's shadow ratio (whenever asked: the game's light shaders read the words as ratios; 1 until one is kept).
    const bool lights_asked = local_lights && lights_bounce_pipeline_ && ((shared.rt_flags & dlaa_bridge::RT_LIGHTS_IN_BOUNCE) != 0 || LightsInBounceTest() != 0);
    const bool light_ratio = lights_asked && ((shared.rt_flags & dlaa_bridge::RT_LIGHT_RATIO) != 0 || LightsInBounceTest() == 3u);
    const bool lights_in_bounce = light_ratio || (lights_asked && reuse_lights_mode_ != 0 && reuse_frame_ != UINT32_MAX && reuse_frame_ + 1 >= frame);
    const uint32_t lights_mode = light_ratio ? 3u : (lights_in_bounce ? 1u : 0u);
    const bool ratio_kept = light_ratio && light_ratio_[0] && EnsureHistory(HISTORY_RATIO, frame) && ratio_frame_ != UINT32_MAX && ratio_frame_ + 1 == frame;
    // v18: rays a pixel toward the sun and each light, the lights' radius for soft shadows (rt_lights: under RR).
    const std::array<uint32_t, 12> settings = {
        frame,
        (shared.rt_flags & ~(dlaa_bridge::RT_LOCAL_LIGHTS | dlaa_bridge::RT_NRD | dlaa_bridge::RT_AO_REBLUR | dlaa_bridge::RT_START_PAST | dlaa_bridge::RT_LIGHTS_IN_BOUNCE
                             | dlaa_bridge::RT_LIGHT_RATIO))
            | (local_lights ? dlaa_bridge::RT_LOCAL_LIGHTS : 0u) | (lights_in_bounce ? dlaa_bridge::RT_LIGHTS_IN_BOUNCE : 0u) | (light_ratio ? dlaa_bridge::RT_LIGHT_RATIO : 0u)
            | (smoke_scene ? TRACE_SMOKE : 0u) | (nrd_sun ? dlaa_bridge::RT_NRD : 0u) | (nrd_ambient ? TRACE_NRD_AMBIENT : 0u),
        std::bit_cast<uint32_t>(std::clamp(shared.rt_ambient_range, 0.1f, 100.02711f)),
        std::clamp(shared.rt_ambient_rays, 1u, 16u),
        shared.rt_width,
        shared.rt_height,
        std::bit_cast<uint32_t>(soft ? std::tan(softness * 3.14159265f / 180.f) : 0.f),
        (version >= 18) ? std::clamp(shared.rt_shadow_rays, 1u, 8u) : 1u,
        std::bit_cast<uint32_t>((version >= 18) ? std::clamp(shared.rt_light_size, 0.f, 2.f) : 0.f),
        std::bit_cast<uint32_t>((version >= 18) ? std::clamp(shared.rt_grass_shadow_range, 0.f, 1.0001666e4f) : 0.f),
        // v31: the light mask's tiles' first row (rt_lights).
        (version >= 31) ? shared.rt_light_tile_row : 0u,
    };
    list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(settings.size()), settings.data(), 0);
    list->SetComputeRootConstantBufferView(ROOT_LIGHTS, slot->lights->GetGPUVirtualAddress());
    // The trace's history: the ambient visibility and the local lights' bits kept over frames, valid when the last trace
    // was the frame before (its positions and camera kept for the projection; the lights' bits also with the same Torch and
    // Fire Light in the Light Bounce: its lights' kept bits were never traced). Its constants bound every trace.
    const bool lights_kept = local_lights && version >= 11 && EnsureHistory(HISTORY_LIGHTS, frame);
    const bool ambient_kept = version >= 11 && (((shared.rt_flags & dlaa_bridge::RT_AMBIENT_OCCLUSION) != 0 && !nrd_ambient) || lights_kept)
                              && EnsureHistory(HISTORY_AMBIENT, frame);
    {
      const bool last_frame = ambient_kept && ambient_frame_ != UINT32_MAX && ambient_frame_ + 1 == frame;
      PathConstants constants = {.flags = (last_frame ? PATH_AMBIENT_HISTORY : 0u) | (((shared.rt_flags & dlaa_bridge::RT_START_PAST) != 0) ? PATH_START_PAST : 0u)
                                          | (lights_kept && last_frame && lights_frame_ + 1 == frame && lights_mode == lights_mode_traced_ ? PATH_LIGHT_HISTORY : 0u)
                                          | (ambient_parity_ != 0 ? PATH_AMBIENT_PARITY : 0u) | (tint ? PATH_TINT : 0u)
                                          | (tint_guides ? PATH_TINT_GUIDES : 0u),
                                 // The last bounce's shadow ratio: its texels, the set it wrote.
                                 .lights_in_bounce = light_ratio ? (3u | ((ratio_parity_ == 0) ? 16u : 0u) | (ratio_kept ? 32u : 0u)) : 0u};
      if (light_ratio) {
        constants.bounce_width = ratio_size_[0];
        constants.bounce_height = ratio_size_[1];
        constants.bounce_scale = ratio_size_[2];
      }
      std::copy_n(shared.rt_view_projection, 16, constants.view_projection.begin());
      constants.previous_view_projection = ambient_view_projection_;
      std::memcpy(slot->paths_data, &constants, sizeof(constants));
      list->SetComputeRootConstantBufferView(ROOT_PATHS, slot->paths->GetGPUVirtualAddress());
    }
    if (ambient_kept) {
      const auto to_uav = Transition(ambient_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &to_uav);
    }
    Mark(list, L"trace");
    list->SetPipelineState(trace_pipeline_.Get());
    if (profiling_) {
      // The profile's split: the sun's part, then the ambient's (rt_trace's PART_*), timed apart.
      list->SetComputeRoot32BitConstant(ROOT_SETTINGS, settings[1] | TRACE_PART_SUN, 1);
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
      ProfileStamp(list, slot_index, 0);
      list->SetComputeRoot32BitConstant(ROOT_SETTINGS, settings[1] | TRACE_PART_AMBIENT, 1);
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
      ProfileStamp(list, slot_index, 1);
      list->SetComputeRoot32BitConstant(ROOT_SETTINGS, settings[1], 1);
    } else {
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    }
    // The local lights' shadows (rt_lights: their own registers; before this trace's positions become the history).
    if (local_lights) {
      Mark(list, L"local lights");
      list->SetPipelineState((lights_in_bounce ? lights_bounce_pipeline_ : lights_pipeline_).Get());
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    }
    if (ambient_kept) {
      // This trace's positions for the next one.
      const std::array<D3D12_RESOURCE_BARRIER, 2> to_copy = {
          Transition(ambient_positions_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
          Transition(positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
      };
      list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
      const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = ambient_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      const D3D12_BOX region = {.left = 0, .top = 0, .front = 0, .right = std::max(shared.rt_width, 1u), .bottom = std::max(shared.rt_height, 1u),
                                .back = 1};
      list->CopyTextureRegion(&to, 0, 0, 0, &from, &region);
      const std::array<D3D12_RESOURCE_BARRIER, 2> back = {
          Transition(ambient_positions_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
          Transition(positions_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
      };
      list->ResourceBarrier(static_cast<UINT>(back.size()), back.data());
      std::copy_n(shared.rt_view_projection, 16, ambient_view_projection_.begin());
      ambient_frame_ = frame;
      ambient_parity_ ^= 1u;
    }
    ProfileStamp(list, slot_index, 2);
    // This trace's lights for the next one's places.
    if (lights_kept) {
      for (uint32_t light = 0; light < light_count; ++light) std::copy_n(SharedLight(shared, light, false), 4, previous_lights_[light].begin());
      previous_light_count_ = light_count;
      lights_frame_ = frame;
      lights_mode_traced_ = lights_mode;
    }
    if (fog) {
      Mark(list, L"fog");
      list->SetPipelineState(fog_pipeline_.Get());
      list->Dispatch((shared.rt_width + 31) / 32, (shared.rt_height + 31) / 32, 1);
    }
    bool nrd_denoised = false;
#if defined(RENODX_NRD)
    // NRD's inputs on this list's bindings; SIGMA and REBLUR take theirs (before the built-in filter only when one failed
    // before touching the list: a camera it cannot take apart, no such frame known).
    if (nrd_sun || nrd_ambient) {
      list->ResourceBarrier(1, &uav_barrier);
      Mark(list, L"NRD inputs");
      for (NrdSignal* signal : {nrd_sun ? &nrd_sun_ : nullptr, nrd_ambient ? &nrd_ambient_ : nullptr}) {
        if (signal == nullptr) continue;
        signal->denoiser->ToWritten(list);
        list->SetPipelineState(signal->front.Get());
        list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
      }
      list->ResourceBarrier(1, &uav_barrier);
    }
    if (nrd_sun) {
      Mark(list, L"NRD SIGMA");
      const nrd::SigmaSettings sigma = {};
      nrd_denoised = nrd_sun_.denoiser->Run(list, static_cast<UINT>(slot_index), shared.rt_view_projection, frame, shared.rt_width, shared.rt_height, &sigma);
      if (nrd_denoised) {
        nrd_sun_.denoiser->Finish(list, nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY, positions_.Get(), POSITIONS_VIEW, visibility_.Get(), DXGI_FORMAT_R8_UNORM);
      }
    }
#endif
    if (soft && !reconstructed && !nrd_denoised) {
      // Soft shadows: the rays' penumbras.
      list->ResourceBarrier(1, &uav_barrier);
      Mark(list, L"soft");
      list->SetPipelineState(soft_pipeline_.Get());
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    }
    bool nrd_ambient_ran = false;
#if defined(RENODX_NRD)
    if (nrd_ambient) {
      Mark(list, L"NRD REBLUR (AO)");
      // Its hit distance is the rays' open share of their range.
      const nrd::ReblurSettings reblur = {.hitDistanceParameters = {.A = std::clamp(shared.rt_ambient_range, 0.1f, 100.f), .B = 0.f, .C = 1.f}};
      nrd_ambient_ran = nrd_ambient_.denoiser->Run(list, static_cast<UINT>(slot_index), shared.rt_view_projection, frame, shared.rt_width, shared.rt_height, &reblur);
      if (nrd_ambient_ran) {
        nrd_ambient_.denoiser->Finish(list, nrd::ResourceType::OUT_DIFF_HITDIST, positions_.Get(), POSITIONS_VIEW, ambient_.Get(), DXGI_FORMAT_R8_UNORM);
      }
    }
#endif
    if (ambient_by_nrd && (shared.rt_flags & dlaa_bridge::RT_AMBIENT_OCCLUSION) != 0) {
      NoteNrd(dlaa_bridge::RT_NRD_OFF_AMBIENT, nrd_ambient_ran);
    }
    barriers.clear();
    for (const auto& [resource, used] : textures) {
      barriers.push_back(Transition(resource, used, D3D12_RESOURCE_STATE_COMMON));
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    Stamp(list, slot_index * STAMPS + 3);
    // The waiting GPU bakes, oldest first, within the request's budget (the SDK's guide asks for one: MICROMAP_GPU_BAKES
    // and MICROMAP_GPU_TRIANGLES, the first whatever its size); one whose mesh or cluster set changed since dropped.
    std::vector<MicromapBake*> gpu_baking;
    uint32_t gpu_bake_triangles = 0;
    for (std::unique_ptr<MicromapBake>& bake : micromap_reads_) {
      if (!bake->gpu || bake->value != UINT64_MAX) continue;
      const auto mesh = meshes_.find(bake->key);
      const auto set = sets_.find(bake->key);
      const bool current = (bake->set ? (set != sets_.end() && set->second.generation == bake->build)
                                      : (mesh != meshes_.end() && mesh->second.build == bake->build));
      if (!current) {
        bake.reset();
        continue;
      }
      if (gpu_baking.size() == MICROMAP_GPU_BAKES || (!gpu_baking.empty() && gpu_bake_triangles + bake->triangles > MICROMAP_GPU_TRIANGLES)) break;
      bake->value = own_value_ + 1;
      gpu_bake_triangles += bake->triangles;
      gpu_baking.push_back(bake.get());
    }
    std::erase(micromap_reads_, nullptr);
    slot->gpu_bakes = static_cast<uint32_t>(gpu_baking.size());
    for (const MicromapBake* bake : gpu_baking) {
      if (details_) {
        slot->micromap_builds += std::format("; GPU bake {} triangles{}", bake->triangles, bake->set ? ", grass set" : "");
      }
    }
    // Before the game's signal, so the game waits for them (MICROMAP_GPU_BAKES keeps that short). In a list of their own after
    // it, beside the game's frame, the GPU reset in 3 runs of 5 at their dispatches; here in none of 8 (ATN's freezes).
#if defined(RENODX_OMM_SDK)
    BakeMicromapsOnGpu(list, gpu_baking);
#endif
    Stamp(list, slot_index * STAMPS + 9);
    Resolve(list, slot_index);
    if (const HRESULT closed = list->Close(); FAILED(closed)) {
      request_failed_ = true;
      status_ = std::format("Ray tracing: a request failed (0x{:08X})", static_cast<uint32_t>(closed));
      // Which passes it held (the one-off E_INVALIDARG of debug32 recorded in 0.01 ms; with RENODX_TW_D3D12_DEBUG the
      // debug layer's messages follow).
      std::string passes;
      for (const wchar_t* pass : slot->marks) {
        passes += passes.empty() ? "" : ", ";
        for (; *pass != L'\0'; ++pass) passes += static_cast<char>(*pass);
      }
      slow_report_ += std::format("{}[TW ray tracing] Trace {} could not be recorded (0x{:08X}; {} captures, {} compact draws, {} placements, {} GPU "
                                  "bakes); its passes: {}.",
                                  slow_report_.empty() ? "" : "\n", own_value_ + 1, static_cast<uint32_t>(closed), shared.rt_capture_count,
                                  shared.rt_compact_count, shared.rt_placement_count, slot->gpu_bakes, passes.empty() ? "none" : passes);
      return false;
    }
    // Meshes unseen for long give their BLAS (and micromaps, record, pool ranges) back once the GPU is past this request.
    std::erase_if(meshes_, [&](auto& entry) {
      const uint64_t release_frames = (entry.second.placed && version >= 24 && shared.rt_kept_frames != 0) ? shared.rt_kept_frames : RELEASE_FRAMES;
      if (entry.second.drawn_frame + release_frames >= frame) return false;
      ReleaseMesh(&entry.second);
      return true;
    });
    if (!details_) return true;
    std::string denoisers;
#if defined(RENODX_NRD)
    denoisers = std::format("{}{}{}{}", nrd_denoised ? ", NRD SIGMA" : "", nrd_ambient ? ", NRD AO" : "", nrd_bounce_.denoiser ? ", NRD Light Bounce" : "",
                            nrd_reflections_.denoiser ? ", NRD reflections" : "");
#endif
    status_ = std::format("Ray tracing ({}{}): every frame {:.2f} M triangles (terrain {:.2f}, grass and leaves {:.2f}; {}) | {} meshes ({} new, {} "
                          "micromaps, {} MB), {} + {} placed instances ({} with those kept){}, {:.1f} M triangles",
                          dxr12_ ? "DXR 1.2" : "DXR 1.1", denoisers, static_cast<double>((units_built || units_clustered) ? dynamic_triangles : 0u) / 1.0e6,
                          static_cast<double>((units_built || units_clustered) ? terrain_triangles : 0u) / 1.0e6,
                          static_cast<double>((units_built || units_clustered) ? grass_triangles : 0u) / 1.0e6,
                          units_clustered ? std::format("{} clusters ({} from templates), {} compact draws of {} sets", clusters_built, templated_built,
                                                        compact_drawn, sets_.size())
                                          : (units_built ? (units_refitted ? "refitted" : "rebuilt") : "none"),
                          meshes_.size(), built, micromaps_built, bytes >> 20, instance_count, placed_drawn, placed_written_,
                          ptlas_ != nullptr ? std::format(" (partitioned TLAS: {} changed)", ptlas_changes) : std::string(),
                          static_cast<double>(triangles) / 1.0e6);
    profile_counts_ = std::format(",{:.3f},{:.3f},{:.3f},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                                  static_cast<double>((units_built || units_clustered) ? dynamic_triangles : 0u) / 1.0e6,
                                  static_cast<double>((units_built || units_clustered) ? terrain_triangles : 0u) / 1.0e6,
                                  static_cast<double>((units_built || units_clustered) ? grass_triangles : 0u) / 1.0e6, clusters_built, templated_built,
                                  compact_drawn, sets_.size(), new_set_count, template_builds, meshes_.size(), built, micromaps_built,
                                  micromap_reads_.size() + micromap_queue_.size() + micromaps_baked_.size(), instance_count, placed_drawn,
                                  cluster_room_, bytes >> 20);
    if (slot->stamped) {
      gpu_timings_ = std::format("GPU ms: scene {:.2f} (clusters {:.2f}), TLAS {:.2f}, trace {:.2f}, bounce {:.2f}, reflections {:.2f}", gpu_ms_[0],
                             gpu_ms_[5], gpu_ms_[1], gpu_ms_[2], gpu_ms_[3], gpu_ms_[4]);
    }
    slot->status = status_;
    return true;
  }

  // QueryPerformanceCounter's ticks from `from` to `to` in milliseconds.
  static double CpuMs(const LARGE_INTEGER& from, const LARGE_INTEGER& to) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(to.QuadPart - from.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
  }

  // A slot's request's profile line (RT_PROFILE) with its GPU times, once they came back.
  void WriteProfile(size_t slot_index, const std::string& gpu_times) {
    std::string& line = slots_[slot_index].profile;
    if (line.empty()) return;
    if (profile_.is_open()) {
      profile_ << line << gpu_times << ',' << slots_[slot_index].profile_parts << '\n';
      if (!profiling_) profile_.flush();
    }
    line.clear();
  }

  // GPU times (with Log Details or the profile, but with RENODX_TW_NO_GPU_TIMES): a timestamp at `index`, a slot's resolved
  // for reading, its last read back.
  void Stamp(ID3D12GraphicsCommandList* list, size_t index) const {
    if (timestamps_ && details_) {
      list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(index));
    }
  }
  // All of a slot's timestamps at its list's start (Resolve reads them all; the debug layer flagged the ones a request
  // never wrote, every request).
  void StampSlot(ID3D12GraphicsCommandList* list, size_t slot_index) const {
    for (size_t index = 0; index < STAMPS; ++index) Stamp(list, (slot_index * STAMPS) + index);
  }
  // A request's profile part's end (RT_PROFILE): after the passes before it (a UAV barrier: they overlap otherwise).
  void ProfileStamp(ID3D12GraphicsCommandList* list, size_t slot_index, size_t part) const {
    if (!profiling_ || !timestamps_) return;
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    list->ResourceBarrier(1, &uav_barrier);
    Stamp(list, (slot_index * STAMPS) + 6 + part);
  }
  void Resolve(ID3D12GraphicsCommandList* list, size_t slot_index) {
    slots_[slot_index].stamped = timestamps_ && details_;
    if (slots_[slot_index].stamped) {
      list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(slot_index * STAMPS), STAMPS, timestamp_readback_.Get(),
                             slot_index * STAMPS * sizeof(uint64_t));
    }
  }
  void ReadTimes(size_t slot_index) {
    if (!slots_[slot_index].stamped) return;
    if (timestamp_frequency_ == 0) queue_->GetTimestampFrequency(&timestamp_frequency_);
    const D3D12_RANGE range = {.Begin = slot_index * STAMPS * sizeof(uint64_t), .End = (slot_index + 1) * STAMPS * sizeof(uint64_t)};
    uint64_t* mapped = nullptr;
    if (timestamp_frequency_ == 0 || FAILED(timestamp_readback_->Map(0, &range, reinterpret_cast<void**>(&mapped)))) return;
    const uint64_t* stamps = mapped + slot_index * STAMPS;
    const auto ms = [&](uint64_t from, uint64_t to) { return (to > from) ? static_cast<double>(to - from) * 1000.0 / timestamp_frequency_ : 0.0; };
    if (slot_traced_[slot_index]) {
      gpu_ms_[0] = ms(stamps[0], stamps[1]);
      gpu_ms_[1] = ms(stamps[1], stamps[2]);
      gpu_ms_[2] = ms(stamps[2], stamps[3]);
      gpu_ms_[5] = slot_clustered_[slot_index] ? ms(stamps[4], stamps[5]) : 0.0;
      WriteProfile(slot_index, std::format(",{:.3f},{:.3f},{:.3f},{:.3f},,{:.3f},{:.3f},{:.3f},{:.3f},{},{:.3f}", gpu_ms_[0], gpu_ms_[5], gpu_ms_[1],
                                           gpu_ms_[2], ms(stamps[2], stamps[6]), ms(stamps[6], stamps[7]), ms(stamps[7], stamps[8]),
                                           ms(stamps[8], stamps[3]), slots_[slot_index].gpu_bakes, ms(stamps[3], stamps[9])));
      // The Light Bounce's and reflections' times only while they run (not their last).
      for (size_t kind = 0; kind < paths_age_.size(); ++kind) {
        if (++paths_age_[kind] > SLOTS * 2) gpu_ms_[3 + kind] = 0.0;
      }
      if (gpu_ms_[0] + gpu_ms_[1] + gpu_ms_[2] > SLOW_MS && slow_reports_ < SLOW_REPORTS) {
        slow_report_ += std::format("{}[TW ray tracing] A slow request, GPU ms: scene {:.1f}, TLAS {:.1f}, trace {:.1f}; {}", slow_report_.empty() ? "" : "\n", gpu_ms_[0], gpu_ms_[1],
                                   gpu_ms_[2], slots_[slot_index].status);
        ++slow_reports_;
      }
    } else {
      const double paths_ms = ms(stamps[4], stamps[5]);
      WriteProfile(slot_index, std::format(",,,,,{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},,", paths_ms, ms(stamps[4], stamps[6]), ms(stamps[6], stamps[7]),
                                           ms(stamps[7], stamps[8]), ms(stamps[8], stamps[5])));
      gpu_ms_[slot_reflected_[slot_index] ? 4 : 3] = paths_ms;
      paths_age_[slot_reflected_[slot_index] ? 1 : 0] = 0;
      if (paths_ms > SLOW_MS && slow_reports_ < SLOW_REPORTS) {
        slow_report_ += std::format("{}[TW ray tracing] A slow {} request: {:.1f} GPU ms", slow_report_.empty() ? "" : "\n", slot_reflected_[slot_index] ? "reflections" : "Light Bounce",
                                   paths_ms);
        ++slow_reports_;
      }
    }
    const D3D12_RANGE written = {};
    timestamp_readback_->Unmap(0, &written);
  }

  // A mesh's BLAS and micromaps released, its record and pool ranges given back, once the GPU is past this request.
  void ReleaseMesh(Mesh* mesh) {
    ReleaseBlas(&mesh->blas);
    if (mesh->micromaps) releases_.push_back({.value = own_value_ + 1, .resource = std::move(mesh->micromaps)});
    pending_frees_.push_back({.value = own_value_ + 1, .record = mesh->record, .normal_offset = mesh->normal_offset,
                              .triangles = mesh->triangles, .uv_offset = mesh->uv_offset, .first_instance = mesh->first_instance,
                              .instances = static_cast<uint32_t>(mesh->transforms.size())});
  }

  // A place of `bytes` for a static BLAS in `pool` (BUILD_POOL, KEPT_POOL): the first chunk with room, else a new chunk;
  // past BLAS_OWN_BYTES a buffer of its own. Its address is 0 when there is no memory.
  BlasPlace PlaceBlas(uint64_t bytes, uint32_t pool) {
    BlasPlace place;
    if (bytes > BLAS_OWN_BYTES) {
      place.own = Buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
      if (place.own) place.address = place.own->GetGPUVirtualAddress();
      return place;
    }
    const auto units = static_cast<uint32_t>((bytes + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1)
                                             / D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    std::vector<BlasChunk>& chunks = blas_pools_[pool];
    uint32_t chunk = 0;
    while (chunk < chunks.size() && !(chunks[chunk].buffer && chunks[chunk].free.Take(units, &place.offset))) ++chunk;
    if (chunk == chunks.size()) {
      // A new chunk, in the place of one let go if there is one.
      chunk = static_cast<uint32_t>(std::ranges::find_if(chunks, [](const BlasChunk& other) { return !other.buffer; }) - chunks.begin());
      if (chunk == chunks.size()) chunks.emplace_back();
      chunks[chunk].buffer = Buffer(BLAS_CHUNK_BYTES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
      if (!chunks[chunk].buffer) return place;
      chunks[chunk].free.free = {{units, BLAS_CHUNK_UNITS - units}};
      place.offset = 0;
    }
    place.pool = pool;
    place.chunk = chunk;
    place.units = units;
    place.address = chunks[chunk].buffer->GetGPUVirtualAddress() + (uint64_t{place.offset} * D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    return place;
  }

  // A static BLAS's place given back once the GPU is past this request.
  void ReleaseBlas(BlasPlace* place) {
    if (place->own) {
      releases_.push_back({.value = own_value_ + 1, .resource = std::move(place->own)});
    } else if (place->units != 0) {
      releases_.push_back({.value = own_value_ + 1, .pool = place->pool, .chunk = place->chunk, .offset = place->offset, .units = place->units});
    }
    *place = {};
  }

  // The leaves' micromaps on their way: read once the GPU is past their request (their readbacks released), baked one at
  // a time on a thread, then waiting for a request to build them (Record).
  // The bakes on their way on the workers.
  size_t MicromapsBaking() const {
    return static_cast<size_t>(std::ranges::count_if(micromap_baking_, [](const auto& baking) { return baking.valid(); }));
  }

  void AdvanceMicromaps(uint64_t completed) {
    // A removed device's fence reads all ones: nothing finished, the bakes on their way are no failures.
    if (completed == UINT64_MAX) return;
    for (std::unique_ptr<MicromapBake>& read : micromap_reads_) {
      if (read->value > completed) continue;
#if defined(RENODX_OMM_SDK)
      // GPU Micromap Baking: its histogram read back (the SDK's usage counts: a count, a level and a format each; the
      // zero ones left out), the rest stays on the GPU.
      if (read->gpu) {
        const D3D12_RANGE range = {.Begin = read->histogram_offset, .End = read->histogram_offset + read->histogram_bytes};
        const uint8_t* mapped = nullptr;
        if (!read->gpu_failed && read->gpu_readback
            && SUCCEEDED(read->gpu_readback->Map(0, &range, reinterpret_cast<void**>(const_cast<uint8_t**>(&mapped))))) {
          const auto* counts = reinterpret_cast<const ommCpuOpacityMicromapUsageCount*>(mapped + range.Begin);
          for (size_t entry = 0; entry < read->histogram_bytes / sizeof(ommCpuOpacityMicromapUsageCount); ++entry) {
            if (counts[entry].count == 0) continue;
            read->histogram.push_back({.Count = counts[entry].count, .SubdivisionLevel = counts[entry].subdivisionLevel,
                                       .Format = static_cast<D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT>(counts[entry].format)});
            // Their data's bytes: 4^level micro-triangles, 1 or 2 bits each.
            micromap_tally_[1] += uint64_t{counts[entry].count}
                                  * ((((uint64_t{1} << (2u * counts[entry].subdivisionLevel)) * counts[entry].format) + 7) / 8);
          }
          const D3D12_RANGE none = {};
          read->gpu_readback->Unmap(0, &none);
          read->gpu_readback.Reset();
          ++micromap_tally_[0];
          micromaps_baked_.push_back(std::move(read));
        } else {
          ++micromap_tally_[2];
          read.reset();
        }
        continue;
      }
#endif
      // Its places in its request's read-backs (Map points at a buffer's start).
      const D3D12_RANGE uv_range = {.Begin = read->uvs_offset, .End = read->uvs_offset + (uint64_t{read->triangles} * 12)};
      const D3D12_RANGE mask_range = {.Begin = read->mask_offset,
                                      .End = read->mask_offset + (uint64_t{dlaa_bridge::RT_ATLAS_SIZE} * dlaa_bridge::RT_ATLAS_SIZE)};
      const D3D12_RANGE written = {};
      const uint8_t* uvs = nullptr;
      const uint8_t* mask = nullptr;
      if (SUCCEEDED(read->uvs->Map(0, &uv_range, reinterpret_cast<void**>(const_cast<uint8_t**>(&uvs))))) {
        const auto* first = reinterpret_cast<const uint32_t*>(uvs + uv_range.Begin);
        read->uv_data.assign(first, first + (uint64_t{read->triangles} * 3));
        read->uvs->Unmap(0, &written);
      }
      if (SUCCEEDED(read->mask->Map(0, &mask_range, reinterpret_cast<void**>(const_cast<uint8_t**>(&mask))))) {
        read->mask_data.assign(mask + mask_range.Begin, mask + mask_range.End);
        read->mask->Unmap(0, &written);
      }
      read->uvs.Reset();
      read->mask.Reset();
      if (!read->uv_data.empty() && !read->mask_data.empty()) micromap_queue_.push_back(std::move(read));
      read.reset();
    }
    std::erase(micromap_reads_, nullptr);
    // MICROMAP_WORKERS bakes at once, each with its baker (one at a time, a battle's new grass sets waited seconds for
    // theirs: without, dense grass met MAX_CANDIDATES and stayed dark, ATN 2026-10-29); grass sets first.
    for (size_t worker = 0; worker < MICROMAP_WORKERS; ++worker) {
      auto& baking = micromap_baking_[worker];
      if (baking.valid() && baking.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::unique_ptr<MicromapBake> baked = baking.get();
        ++micromap_tally_[baked->indices.empty() ? 2 : 0];
        micromap_tally_[1] += baked->data.size();
        if (!baked->indices.empty()) micromaps_baked_.push_back(std::move(baked));
      }
#if defined(RENODX_OMM_SDK)
      if (!baking.valid() && !micromap_queue_.empty()) {
        const auto next = std::ranges::find_if(micromap_queue_, [](const auto& queued) { return queued->set; });
        const auto taken = (next != micromap_queue_.end()) ? next : micromap_queue_.begin();
        baking = std::async(std::launch::async, [this, baker = omm_.bakers[worker], bake = std::move(*taken)]() mutable {
          BakeMicromaps(bake.get(), baker);
          return std::move(bake);
        });
        micromap_queue_.erase(taken);
      }
#endif
    }
    // The bakes' tally at most every 10 s (the log).
    if (details_ && (micromap_tally_[0] + micromap_tally_[2]) != 0 && GetTickCount64() >= micromap_logged_ + 10000) {
      micromap_logged_ = GetTickCount64();
      slow_report_ += std::format("{}[TW ray tracing] Leaf micromaps: {} meshes baked ({} KB), {} failed; {} grass sets given theirs.",
                                  (slow_report_.empty() ? "" : "\n"), micromap_tally_[0], micromap_tally_[1] >> 10, micromap_tally_[2],
                                  micromap_tally_[3]);
      micromap_tally_ = {};
    }
  }

#if defined(RENODX_OMM_SDK)
  // GPU Micromap Baking: the OMM SDK's compute pipelines as D3D12 ones (each a table of its descriptor ranges, the global
  // constants' CBV, the dispatches' own root constants, the SDK's static samplers), the descriptor ring, the indirect
  // dispatch's signature, the corners' indices and the SDK's static buffers; false: it stays off (the CPU bakes go on),
  // `why` says what failed (the status line).
  bool MakeGpuBaker(std::string* why) {
    const ommGpuPipelineInfoDesc* info = nullptr;
    if (omm_.gpu_pipeline == nullptr) {
      *why = "no GPU baker";
      return false;
    }
    if (omm_.gpu_pipeline_desc(omm_.gpu_pipeline, &info) != ommResult_SUCCESS) {
      *why = "no pipeline descriptions";
      return false;
    }
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    for (uint32_t index = 0; index < info->staticSamplersNum; ++index) {
      const ommGpuStaticSamplerDesc& sampler = info->staticSamplers[index];
      D3D12_TEXTURE_ADDRESS_MODE address = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      switch (sampler.desc.addressingMode) {
        case ommTextureAddressMode_Mirror:     address = D3D12_TEXTURE_ADDRESS_MODE_MIRROR; break;
        case ommTextureAddressMode_Clamp:      address = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; break;
        case ommTextureAddressMode_Border:     address = D3D12_TEXTURE_ADDRESS_MODE_BORDER; break;
        case ommTextureAddressMode_MirrorOnce: address = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE; break;
        default:                               break;
      }
      samplers.push_back({.Filter = (sampler.desc.filter == ommTextureFilterMode_Nearest) ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                          .AddressU = address,
                          .AddressV = address,
                          .AddressW = address,
                          .MaxAnisotropy = 1,
                          .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
                          .BorderColor = (sampler.desc.borderAlpha > 0.5f) ? D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK
                                                                           : D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
                          .MaxLOD = D3D12_FLOAT32_MAX,
                          .ShaderRegister = sampler.registerIndex});
    }
    auto& gpu = omm_gpu_;
    gpu.pipelines.resize(info->pipelineNum);
    for (uint32_t index = 0; index < info->pipelineNum; ++index) {
      // The raster ones are not needed: the bakes run ComputeOnly.
      if (info->pipelines[index].type != ommGpuPipelineType_Compute) continue;
      const ommGpuComputePipelineDesc& compute = info->pipelines[index].compute;
      std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
      for (uint32_t range = 0; range < compute.descriptorRangeNum; ++range) {
        const ommGpuDescriptorRangeDesc& desc = compute.descriptorRanges[range];
        ranges.push_back({.RangeType = (desc.descriptorType == ommGpuDescriptorType_RawBufferWrite) ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
                                                                                                    : D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                          .NumDescriptors = desc.descriptorNum,
                          .BaseShaderRegister = desc.baseRegisterIndex,
                          .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND});
      }
      OmmGpu::Pipeline& made = gpu.pipelines[index];
      std::vector<D3D12_ROOT_PARAMETER> parameters;
      if (!ranges.empty()) {
        made.table = static_cast<int>(parameters.size());
        parameters.push_back({.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                              .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}});
      }
      // The global constants for every pipeline (omm_rasterize_cs_r reads them without hasConstantData).
      gpu.globals_bytes = info->globalConstantBufferDesc.maxDataSize;
      if (info->globalConstantBufferDesc.maxDataSize != 0) {
        made.global = static_cast<int>(parameters.size());
        parameters.push_back({.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
                              .Descriptor = {.ShaderRegister = info->globalConstantBufferDesc.registerIndex}});
      }
      if (info->localConstantBufferDesc.maxDataSize >= 4) {
        made.constants = static_cast<int>(parameters.size());
        made.constant_count = info->localConstantBufferDesc.maxDataSize / 4;
        parameters.push_back({.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
                              .Constants = {.ShaderRegister = info->localConstantBufferDesc.registerIndex, .Num32BitValues = made.constant_count}});
      }
      const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = static_cast<UINT>(parameters.size()),
                                                   .pParameters = parameters.data(),
                                                   .NumStaticSamplers = static_cast<UINT>(samplers.size()),
                                                   .pStaticSamplers = samplers.data()};
      ComPtr<ID3DBlob> serialized;
      ComPtr<ID3DBlob> errors;
      if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), errors.GetAddressOf()))
          || FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(made.root.GetAddressOf())))) {
        *why = std::format("pipeline {} ({}): root signature {}", index, compute.shaderFileName != nullptr ? compute.shaderFileName : "?",
                           errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string());
        return false;
      }
      const D3D12_COMPUTE_PIPELINE_STATE_DESC state_desc = {
          .pRootSignature = made.root.Get(), .CS = {.pShaderBytecode = compute.computeShader.data, .BytecodeLength = compute.computeShader.size}};
      if (const HRESULT result = device_->CreateComputePipelineState(&state_desc, IID_PPV_ARGS(made.state.GetAddressOf())); FAILED(result)) {
        *why = std::format("pipeline {} ({}): state 0x{:08X} ({} ranges, global b{} {}, local b{} {} bytes)", index,
                           compute.shaderFileName != nullptr ? compute.shaderFileName : "?", static_cast<uint32_t>(result), ranges.size(),
                           info->globalConstantBufferDesc.registerIndex, compute.hasConstantData ? 1 : 0,
                           info->localConstantBufferDesc.registerIndex, info->localConstantBufferDesc.maxDataSize);
        return false;
      }
    }
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = OMM_DESCRIPTORS,
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    const D3D12_INDIRECT_ARGUMENT_DESC argument = {.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH};
    const D3D12_COMMAND_SIGNATURE_DESC signature = {.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS), .NumArgumentDescs = 1, .pArgumentDescs = &argument};
    gpu.indices = Buffer(uint64_t{MICROMAP_TRIANGLES} * 12, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint32_t* corners = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(gpu.heap.GetAddressOf())))
        || FAILED(device_->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(gpu.dispatch.GetAddressOf()))) || !gpu.indices
        || FAILED(gpu.indices->Map(0, nullptr, reinterpret_cast<void**>(&corners)))) {
      *why = "its descriptors, signature or indices";
      return false;
    }
    std::iota(corners, corners + (uint64_t{MICROMAP_TRIANGLES} * 3), 0u);
    gpu.indices->Unmap(0, nullptr);
    for (size_t kind = 0; kind < gpu.statics.size(); ++kind) {
      const ommGpuResourceType type = (kind == 0) ? ommGpuResourceType_STATIC_VERTEX_BUFFER : ommGpuResourceType_STATIC_INDEX_BUFFER;
      size_t bytes = 0;
      if (omm_.gpu_static_data(type, nullptr, &bytes) != ommResult_SUCCESS || bytes == 0) continue;
      gpu.statics[kind] = Buffer(Align(bytes, 256), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      uint8_t* data = nullptr;
      if (!gpu.statics[kind] || FAILED(gpu.statics[kind]->Map(0, nullptr, reinterpret_cast<void**>(&data)))) {
        *why = "its static buffers";
        return false;
      }
      omm_.gpu_static_data(type, data, &bytes);
      gpu.statics[kind]->Unmap(0, nullptr);
    }
    return true;
  }

  // GPU Micromap Baking at the end of a request: `bakes` (leaves' and grass sets' waiting: their UVs and masks copied when
  // read) baked by the OMM SDK's GPU baker (ComputeOnly, 4-state as the ray queries test them, the CPU bakes'
  // settings): the UVs copied with garbage coordinates zeroed (rt_omm_uvs), then the bakes' dispatch chains stage by stage (all bakes' step k, then a UAV barrier; every resource
  // in the state its descriptor needs), into outputs and transient pools the request's bakes share (256-byte aligned
  // places of their own); the histograms copied to a
  // read-back (AdvanceMicromaps: the bakes' builds wait for them, the rest stays on the GPU). A bake the SDK refuses keeps
  // its leaves alpha-tested.
  void BakeMicromapsOnGpu(ID3D12GraphicsCommandList4* list, const std::vector<MicromapBake*>& bakes) {
    if (bakes.empty()) return;
    auto& gpu = omm_gpu_;
    // A dispatch of a bake's chain, copied.
    struct Step {
      const wchar_t* name = nullptr;
      bool indirect = false;
      uint16_t pipeline_index = 0;
      std::vector<ommGpuResource> resources;
      std::vector<uint8_t> constants;
      uint32_t grid_width = 0;
      uint32_t grid_height = 0;
      ommGpuResource arguments = {};
      uint64_t arguments_offset = 0;
    };
    struct Planned {
      MicromapBake* bake = nullptr;
      ommGpuDispatchConfigDesc config = {};
      ommGpuPreDispatchInfo info = {};
      // The clamped UVs', then the outputs' (array data, descs, desc histogram, indices, index histogram, post-dispatch
      // info): places and sizes; its transient pools' places in the request's (one buffer each).
      std::array<uint64_t, 7> offsets = {};
      std::array<uint64_t, 7> sizes = {};
      std::array<uint64_t, OMM_MAX_TRANSIENT_POOL_BUFFERS> transient_offsets = {};
      std::array<uint64_t, OMM_MAX_TRANSIENT_POOL_BUFFERS> transient_sizes = {};
      std::vector<Step> steps;
    };
    std::vector<Planned> planned;
    std::string why;  // the last failure (logged once a request)
    std::array<uint64_t, 7> totals = {};
    std::array<uint64_t, OMM_MAX_TRANSIENT_POOL_BUFFERS> transient_bytes = {};
    for (MicromapBake* bake : bakes) {
      Planned plan = {.bake = bake, .config = ommGpuDispatchConfigDescDefault(), .info = ommGpuPreDispatchInfoDefault()};
      ommGpuDispatchConfigDesc& config = plan.config;
      config.bakeFlags = ommGpuBakeFlags_PerformSetupAndBake | ommGpuBakeFlags_ComputeOnly | ommGpuBakeFlags_Force32BitIndices
                         | ommGpuBakeFlags_DisableTexCoordDeduplication;
      config.runtimeSamplerDesc = {.addressingMode = ommTextureAddressMode_Wrap, .filter = ommTextureFilterMode_Linear, .borderAlpha = 0.f};
      config.alphaMode = ommAlphaMode_Test;
      config.alphaTextureWidth = dlaa_bridge::RT_ATLAS_SIZE;
      config.alphaTextureHeight = dlaa_bridge::RT_ATLAS_SIZE;
      config.alphaTextureChannel = 0;
      config.texCoordFormat = ommTexCoordFormat_UV16_FLOAT;
      config.texCoordOffsetInBytes = static_cast<uint32_t>(totals[0]);
      config.indexFormat = ommIndexFormat_UINT_32;
      config.indexCount = bake->triangles * 3;
      config.alphaCutoff = LEAF_ALPHA_CUTOFF;
      config.maxSubdivisionLevel = static_cast<uint8_t>(bake->grass ? GRASS_MICROMAP_LEVEL : 6);
      config.maxOutOmmArraySize = MICROMAP_MESH_BYTES;
      config.maxScratchMemorySize = ommGpuScratchMemoryBudget_MB_64;
      if (const ommResult result = omm_.gpu_pre_dispatch(omm_.gpu_pipeline, &config, &plan.info); result != ommResult_SUCCESS) {
        bake->gpu_failed = true;
        why = std::format("pre-dispatch info {}", static_cast<int>(result));
        continue;
      }
      plan.sizes = {uint64_t{bake->triangles} * 12,
                    plan.info.outOmmArraySizeInBytes,
                    plan.info.outOmmDescSizeInBytes,
                    plan.info.outOmmArrayHistogramSizeInBytes,
                    plan.info.outOmmIndexBufferSizeInBytes,
                    plan.info.outOmmIndexHistogramSizeInBytes,
                    plan.info.outOmmPostDispatchInfoSizeInBytes};
      for (size_t kind = 0; kind < totals.size(); ++kind) {
        plan.offsets[kind] = totals[kind];
        totals[kind] = Align(totals[kind] + std::max<uint64_t>(plan.sizes[kind], 4), 256);
      }
      for (uint32_t pool = 0; pool < plan.info.numTransientPoolBuffers && pool < OMM_MAX_TRANSIENT_POOL_BUFFERS; ++pool) {
        plan.transient_offsets[pool] = transient_bytes[pool];
        plan.transient_sizes[pool] = std::max<uint64_t>(plan.info.transientPoolBufferSizeInBytes[pool], 4);
        transient_bytes[pool] = Align(transient_bytes[pool] + plan.transient_sizes[pool], 256);
      }
      planned.push_back(plan);
    }
    const auto fail_all = [&] {
      for (Planned& plan : planned) plan.bake->gpu_failed = true;
    };
    if (planned.empty()) return;
    // The clamped UVs and the scratch (kept, grown), the request's outputs and the histograms' read-back.
    bool made = GrowBuffer(&gpu.uvs, totals[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (size_t pool = 0; pool < transient_bytes.size() && made; ++pool) {
      if (transient_bytes[pool] != 0) made = GrowBuffer(&gpu.transient[pool], transient_bytes[pool], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    std::array<ComPtr<ID3D12Resource>, 6> outputs;
    for (size_t kind = 0; kind < outputs.size() && made; ++kind) {
      outputs[kind] = Buffer(totals[kind + 1], D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      made = static_cast<bool>(outputs[kind]);
    }
    ComPtr<ID3D12Resource> readback;
    if (made) readback = Buffer(totals[3], D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    // The bakes' global constants: one upload buffer, a place each.
    const uint64_t globals_stride = Align(std::max(gpu.globals_bytes, 4u), 256);
    ComPtr<ID3D12Resource> globals;
    uint8_t* globals_data = nullptr;
    if (made) globals = Buffer(globals_stride * planned.size(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!made || !readback || !globals || FAILED(globals->Map(0, nullptr, reinterpret_cast<void**>(&globals_data)))) {
      fail_all();
      slow_report_ += std::format("{}[TW ray tracing] GPU micromap bakes: their buffers failed ({} bakes, {} KB).", slow_report_.empty() ? "" : "\n",
                                  planned.size(), (totals[0] + totals[1] + totals[2] + totals[3] + totals[4] + totals[5] + totals[6]) >> 10);
      return;
    }
    Mark(list, L"micromap bakes");
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    // The UVs clamped (the main root signature: rt_omm_uvs reads t0, the read's copy, writes u6, its count at b1).
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {
        Transition(gpu.uvs.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootSignature(root_signature_.Get());
    list->SetPipelineState(gpu.clamp.Get());
    for (const Planned& plan : planned) {
      const std::array<uint32_t, 8> constants = {plan.bake->triangles};
      list->SetComputeRootShaderResourceView(ROOT_SCENE, plan.bake->uvs->GetGPUVirtualAddress() + plan.bake->uvs_offset);
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, gpu.uvs->GetGPUVirtualAddress() + plan.offsets[0]);
      list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(constants.size()), constants.data(), 0);
      list->Dispatch((plan.bake->triangles + 63) / 64, 1, 1);
    }
    const auto clamped = Transition(gpu.uvs.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(1, &clamped);
    // The SDK's chains (its chain memory is its own again at its next call: each step copied), then recorded stage by
    // stage: every bake's step k together (their resources' states in one barrier, their dispatches, one UAV barrier),
    // as their outputs and transient pools are places of their own (bake after bake, a barrier after each of ~11 small
    // dispatches, left the GPU mostly idle). A step whose states clash with another's in its stage waits for a wave of
    // its own.
    ID3D12DescriptorHeap* heaps[] = {gpu.heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    size_t stages = 0;
    for (size_t index = 0; index < planned.size(); ++index) {
      Planned& plan = planned[index];
      const ommGpuDispatchChain* chain = nullptr;
      if (const ommResult result = omm_.gpu_dispatch(omm_.gpu_pipeline, &plan.config, &chain); result != ommResult_SUCCESS || chain == nullptr) {
        plan.bake->gpu_failed = true;
        why = std::format("dispatch {}", static_cast<int>(result));
        continue;
      }
      if (chain->globalCBufferDataSize > globals_stride) {
        plan.bake->gpu_failed = true;
        why = std::format("its constants ({} bytes)", chain->globalCBufferDataSize);
        continue;
      }
      std::memcpy(globals_data + (index * globals_stride), chain->globalCBufferData, chain->globalCBufferDataSize);
      for (uint32_t step = 0; step < chain->numDispatches; ++step) {
        const ommGpuDispatchDesc& dispatch = chain->dispatches[step];
        if (dispatch.type == ommGpuDispatchType_BeginLabel || dispatch.type == ommGpuDispatchType_EndLabel) continue;
        // The SDK's names are its own again at its next call: kept for the markers (LostReport).
        static std::unordered_set<std::wstring> step_names;
        const auto step_name = [](const char* name) {
          return step_names.emplace(name, name + std::strlen(name)).first->c_str();
        };
        if (dispatch.type == ommGpuDispatchType_ComputeIndirect) {
          const ommGpuComputeIndirectDesc& indirect = dispatch.computeIndirect;
          plan.steps.push_back({.name = step_name(indirect.name), .indirect = true, .pipeline_index = indirect.pipelineIndex,
                                .resources = {indirect.resources, indirect.resources + indirect.resourceNum},
                                .constants = {indirect.localConstantBufferData, indirect.localConstantBufferData + indirect.localConstantBufferDataSize},
                                .arguments = indirect.indirectArg, .arguments_offset = indirect.indirectArgByteOffset});
        } else if (dispatch.type == ommGpuDispatchType_Compute) {
          const ommGpuComputeDesc& compute = dispatch.compute;
          plan.steps.push_back({.name = step_name(compute.name), .pipeline_index = compute.pipelineIndex, .resources = {compute.resources, compute.resources + compute.resourceNum},
                                .constants = {compute.localConstantBufferData, compute.localConstantBufferData + compute.localConstantBufferDataSize},
                                .grid_width = compute.gridWidth, .grid_height = compute.gridHeight});
        } else {
          plan.bake->gpu_failed = true;
          why = std::format("dispatch type {}", static_cast<int>(dispatch.type));
          plan.steps.clear();
          break;
        }
      }
      stages = std::max(stages, plan.steps.size());
    }
    std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> states;  // the tracked ones' states now (upload buffers stay readable)
    for (const auto& output : outputs) states[output.Get()] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    for (const auto& pool : gpu.transient) {
      if (pool) states[pool.Get()] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    for (const Planned& plan : planned) states[gpu.masks[plan.bake->mask_index].Get()] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    states[gpu.uvs.Get()] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = gpu.heap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = gpu.heap->GetGPUDescriptorHandleForHeapStart();
    const auto state_for = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES needed) {
      const auto tracked = states.find(resource);
      if (tracked == states.end() || tracked->second == needed) return;
      barriers.push_back(Transition(resource, tracked->second, needed));
      tracked->second = needed;
    };
    // A resource a chain names: its buffer (or the mask's texture), the bake's place and size there.
    const auto locate = [&](size_t index, const ommGpuResource& resource, uint64_t* offset, uint64_t* bytes) -> ID3D12Resource* {
      const Planned& plan = planned[index];
      const auto output = [&](size_t kind) {
        *offset = plan.offsets[kind + 1];
        *bytes = std::max<uint64_t>(plan.sizes[kind + 1], 4);
        return outputs[kind].Get();
      };
      const auto whole = [&](const ComPtr<ID3D12Resource>& buffer) -> ID3D12Resource* {
        *offset = 0;
        *bytes = buffer ? buffer->GetDesc().Width : 0;
        return buffer.Get();
      };
      switch (resource.type) {
        case ommGpuResourceType_IN_ALPHA_TEXTURE:             return gpu.masks[plan.bake->mask_index].Get();
        case ommGpuResourceType_IN_TEXCOORD_BUFFER:           return whole(gpu.uvs);
        case ommGpuResourceType_IN_INDEX_BUFFER:              return whole(gpu.indices);
        case ommGpuResourceType_OUT_OMM_ARRAY_DATA:           return output(0);
        case ommGpuResourceType_OUT_OMM_DESC_ARRAY:           return output(1);
        case ommGpuResourceType_OUT_OMM_DESC_ARRAY_HISTOGRAM: return output(2);
        case ommGpuResourceType_OUT_OMM_INDEX_BUFFER:         return output(3);
        case ommGpuResourceType_OUT_OMM_INDEX_HISTOGRAM:      return output(4);
        case ommGpuResourceType_OUT_POST_DISPATCH_INFO:       return output(5);
        case ommGpuResourceType_TRANSIENT_POOL_BUFFER:
          if (resource.indexInPool >= gpu.transient.size()) return nullptr;
          *offset = plan.transient_offsets[resource.indexInPool];
          *bytes = plan.transient_sizes[resource.indexInPool];
          return gpu.transient[resource.indexInPool].Get();
        case ommGpuResourceType_STATIC_VERTEX_BUFFER: return whole(gpu.statics[0]);
        case ommGpuResourceType_STATIC_INDEX_BUFFER:  return whole(gpu.statics[1]);
        default:                                      return nullptr;
      }
    };
    for (size_t stage = 0; stage < stages; ++stage) {
      std::vector<size_t> waiting;
      for (size_t index = 0; index < planned.size(); ++index) {
        if (!planned[index].bake->gpu_failed && stage < planned[index].steps.size()) waiting.push_back(index);
      }
      while (!waiting.empty()) {
        // A wave: the waiting steps whose states agree, their descriptors written, then their barrier and dispatches.
        struct Ready {
          size_t index = 0;
          UINT table = 0;
          ID3D12Resource* arguments = nullptr;
          uint64_t arguments_offset = 0;
        };
        struct Located {
          ID3D12Resource* target = nullptr;
          uint64_t offset = 0;
          uint64_t bytes = 0;
          D3D12_RESOURCE_STATES needed = D3D12_RESOURCE_STATE_COMMON;
        };
        std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> wave_states;
        std::vector<Ready> ready;
        std::vector<size_t> later;
        for (const size_t index : waiting) {
          Planned& plan = planned[index];
          const Step& step = plan.steps[stage];
          if (step.pipeline_index >= gpu.pipelines.size() || !gpu.pipelines[step.pipeline_index].state) {
            plan.bake->gpu_failed = true;
            why = std::format("pipeline {}", step.pipeline_index);
            continue;
          }
          std::vector<Located> located;
          for (const ommGpuResource& resource : step.resources) {
            Located place = {.needed = (resource.stateNeeded == ommGpuDescriptorType_RawBufferWrite) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                                                                                     : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
            place.target = locate(index, resource, &place.offset, &place.bytes);
            if (place.target == nullptr) {
              plan.bake->gpu_failed = true;
              why = std::format("resource type {} ({})", static_cast<int>(resource.type), resource.indexInPool);
              break;
            }
            located.push_back(place);
          }
          Located arguments = {.needed = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT};
          if (step.indirect && !plan.bake->gpu_failed) {
            arguments.target = locate(index, step.arguments, &arguments.offset, &arguments.bytes);
            if (arguments.target == nullptr) {
              plan.bake->gpu_failed = true;
              why = std::format("indirect arguments' type {}", static_cast<int>(step.arguments.type));
            }
            arguments.offset += step.arguments_offset;
          }
          if (plan.bake->gpu_failed) continue;
          const auto clashes = [&](const Located& place) {
            const auto found = wave_states.find(place.target);
            return found != wave_states.end() && found->second != place.needed;
          };
          if (std::ranges::any_of(located, clashes) || (arguments.target != nullptr && clashes(arguments))) {
            later.push_back(index);
            continue;
          }
          const UINT resource_count = static_cast<UINT>(located.size());
          if (gpu.next + resource_count > OMM_DESCRIPTORS) gpu.next = 0;
          for (UINT number = 0; number < resource_count; ++number) {
            const auto& [target, offset, bytes, needed] = located[number];
            const D3D12_CPU_DESCRIPTOR_HANDLE handle = {.ptr = cpu_start.ptr + (uint64_t{gpu.next + number} * descriptor_size_)};
            const ommGpuDescriptorType type = step.resources[number].stateNeeded;
            if (type == ommGpuDescriptorType_TextureRead) {
              D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = DXGI_FORMAT_R8_UNORM, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
              view.Texture2D = {.MostDetailedMip = step.resources[number].mipOffset, .MipLevels = std::max<UINT>(step.resources[number].mipNum, 1u)};
              device_->CreateShaderResourceView(target, &view, handle);
            } else if (type == ommGpuDescriptorType_RawBufferWrite) {
              D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER};
              view.Buffer = {.FirstElement = offset / 4, .NumElements = static_cast<UINT>(bytes / 4), .Flags = D3D12_BUFFER_UAV_FLAG_RAW};
              device_->CreateUnorderedAccessView(target, nullptr, &view, handle);
            } else {
              const bool raw = type == ommGpuDescriptorType_RawBufferRead;
              D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_UINT,
                                                      .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                                      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
              view.Buffer = {.FirstElement = offset / 4, .NumElements = static_cast<UINT>(bytes / 4),
                             .Flags = raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE};
              device_->CreateShaderResourceView(target, &view, handle);
            }
            wave_states[target] = needed;
          }
          if (arguments.target != nullptr) wave_states[arguments.target] = arguments.needed;
          ready.push_back({.index = index, .table = gpu.next, .arguments = arguments.target, .arguments_offset = arguments.offset});
          gpu.next += resource_count;
        }
        if (!ready.empty()) Mark(list, planned[ready.front().index].steps[stage].name);
        barriers.clear();
        for (const auto& [resource, needed] : wave_states) state_for(resource, needed);
        if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        for (const Ready& entry : ready) {
          const Step& step = planned[entry.index].steps[stage];
          const OmmGpu::Pipeline& pipeline = gpu.pipelines[step.pipeline_index];
          list->SetComputeRootSignature(pipeline.root.Get());
          list->SetPipelineState(pipeline.state.Get());
          if (pipeline.table >= 0) list->SetComputeRootDescriptorTable(pipeline.table, {.ptr = gpu_start.ptr + (uint64_t{entry.table} * descriptor_size_)});
          if (pipeline.global >= 0) {
            list->SetComputeRootConstantBufferView(pipeline.global, globals->GetGPUVirtualAddress() + (entry.index * globals_stride));
          }
          if (pipeline.constants >= 0 && step.constants.size() >= 4) {
            list->SetComputeRoot32BitConstants(pipeline.constants, std::min(static_cast<UINT>(step.constants.size() / 4), pipeline.constant_count),
                                               step.constants.data(), 0);
          }
          if (step.indirect) {
            list->ExecuteIndirect(gpu.dispatch.Get(), 1, entry.arguments, entry.arguments_offset, nullptr, 0);
          } else {
            list->Dispatch(step.grid_width, step.grid_height, 1);
          }
        }
        if (!ready.empty()) list->ResourceBarrier(1, &uav_barrier);
        waiting = std::move(later);
      }
    }
    // The collapsed triangles' micromaps unknown opaque (rt_omm_uvs: the rays' alpha test decides there).
    barriers.clear();
    state_for(outputs[3].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    state_for(gpu.uvs.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootSignature(root_signature_.Get());
    list->SetPipelineState(gpu.clamp.Get());
    for (const Planned& plan : planned) {
      if (plan.bake->gpu_failed) continue;
      const std::array<uint32_t, 8> constants = {plan.bake->triangles, 1u};
      list->SetComputeRootShaderResourceView(ROOT_SCENE, gpu.uvs->GetGPUVirtualAddress() + plan.offsets[0]);
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, outputs[3]->GetGPUVirtualAddress() + plan.offsets[4]);
      list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(constants.size()), constants.data(), 0);
      list->Dispatch((plan.bake->triangles + 63) / 64, 1, 1);
    }
    list->ResourceBarrier(1, &uav_barrier);
    // What the builds read (array data, descs, indices) readable from now on, the histograms copied to the read-back, the
    // kept ones back to their resting states.
    barriers.clear();
    for (const size_t kind : {0u, 1u, 3u}) {
      if (states[outputs[kind].Get()] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        barriers.push_back(Transition(outputs[kind].Get(), states[outputs[kind].Get()], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
      }
    }
    if (states[outputs[2].Get()] != D3D12_RESOURCE_STATE_COPY_SOURCE) {
      barriers.push_back(Transition(outputs[2].Get(), states[outputs[2].Get()], D3D12_RESOURCE_STATE_COPY_SOURCE));
    }
    for (const auto& pool : gpu.transient) {
      if (pool && states[pool.Get()] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers.push_back(Transition(pool.Get(), states[pool.Get()], D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
      }
    }
    for (const Planned& plan : planned) {
      ID3D12Resource* mask = gpu.masks[plan.bake->mask_index].Get();
      if (states[mask] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        barriers.push_back(Transition(mask, states[mask], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
      }
    }
    if (states[gpu.uvs.Get()] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
      barriers.push_back(Transition(gpu.uvs.Get(), states[gpu.uvs.Get()], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }
    if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->CopyBufferRegion(readback.Get(), 0, outputs[2].Get(), 0, totals[3]);
    if (!why.empty()) {
      slow_report_ += std::format("{}[TW ray tracing] GPU micromap bake failed: {}.", slow_report_.empty() ? "" : "\n", why);
    }
    for (Planned& plan : planned) {
      if (plan.bake->gpu_failed) continue;
      MicromapBake& bake = *plan.bake;
      bake.gpu_arrays = outputs[0];
      bake.arrays_offset = plan.offsets[1];
      bake.gpu_descs = outputs[1];
      bake.descs_offset = plan.offsets[2];
      bake.gpu_indices = outputs[3];
      bake.indices_offset = plan.offsets[4];
      bake.gpu_readback = readback;
      bake.histogram_offset = plan.offsets[3];
      bake.histogram_bytes = static_cast<uint32_t>(plan.sizes[3]);
    }
    // Every output (the histograms and the post-dispatch info too), the read-back and the constants kept until the GPU is
    // past this request.
    globals->Unmap(0, nullptr);
    for (auto& output : outputs) releases_.push_back({.value = own_value_ + 1, .resource = std::move(output)});
    releases_.push_back({.value = own_value_ + 1, .resource = std::move(readback)});
    releases_.push_back({.value = own_value_ + 1, .resource = std::move(globals)});
  }

  // A leaf mesh's micromaps from its UVs and mask (the bake thread): 4-state as the ray queries test them (bilinear,
  // wrapping, LEAF_ALPHA_CUTOFF), a micro-triangle about two texels across (at most level 6, grass's
  // GRASS_MICROMAP_LEVEL), the same ones shared, wholly opaque or clear triangles the special indices (all of them so: no
  // micromaps, no array); none (no indices) when the SDK fails.
  void BakeMicromaps(MicromapBake* bake, ommBaker baker) const {
    ommCpuTextureMipDesc mip = ommCpuTextureMipDescDefault();
    mip.width = dlaa_bridge::RT_ATLAS_SIZE;
    mip.height = dlaa_bridge::RT_ATLAS_SIZE;
    mip.rowPitch = dlaa_bridge::RT_ATLAS_SIZE;
    mip.textureData = bake->mask_data.data();
    ommCpuTextureDesc texture_desc = ommCpuTextureDescDefault();
    texture_desc.format = ommCpuTextureFormat_UNORM8;
    texture_desc.mips = &mip;
    texture_desc.mipCount = 1;
    texture_desc.alphaCutoff = LEAF_ALPHA_CUTOFF;
    ommCpuTexture texture = nullptr;
    if (omm_.create_texture(baker, &texture_desc, &texture) != ommResult_SUCCESS) return;
    std::vector<uint32_t> corners(uint64_t{bake->triangles} * 3);
    std::iota(corners.begin(), corners.end(), 0u);
    ommCpuBakeInputDesc input = ommCpuBakeInputDescDefault();
    input.bakeFlags = ommCpuBakeFlags_EnableInternalThreads | ommCpuBakeFlags_Force32BitIndices;
    input.texture = texture;
    input.runtimeSamplerDesc = {.addressingMode = ommTextureAddressMode_Wrap, .filter = ommTextureFilterMode_Linear, .borderAlpha = 0.f};
    input.alphaMode = ommAlphaMode_Test;
    input.texCoordFormat = ommTexCoordFormat_UV16_FLOAT;
    input.texCoords = bake->uv_data.data();
    input.indexFormat = ommIndexFormat_UINT_32;
    input.indexBuffer = corners.data();
    input.indexCount = static_cast<uint32_t>(corners.size());
    input.alphaCutoff = LEAF_ALPHA_CUTOFF;
    input.maxSubdivisionLevel = bake->grass ? GRASS_MICROMAP_LEVEL : 6;
    input.maxArrayDataSize = MICROMAP_MESH_BYTES;
    input.maxWorkloadSize = MICROMAP_WORKLOAD;
    ommCpuBakeResult result = nullptr;
    const ommCpuBakeResultDesc* desc = nullptr;
    if (omm_.bake(baker, &input, &result) == ommResult_SUCCESS && omm_.result_desc(result, &desc) == ommResult_SUCCESS
        && desc->indexFormat == ommIndexFormat_UINT_32 && desc->indexCount == bake->triangles) {
      static_assert(sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC) == sizeof(ommCpuOpacityMicromapDesc));
      const auto* data = static_cast<const uint8_t*>(desc->arrayData);
      bake->data.assign(data, data + desc->arrayDataSize);
      bake->descs.resize(desc->descArrayCount);
      std::memcpy(bake->descs.data(), desc->descArray, bake->descs.size() * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC));
      for (uint32_t entry = 0; entry < desc->descArrayHistogramCount; ++entry) {
        const ommCpuOpacityMicromapUsageCount& usage = desc->descArrayHistogram[entry];
        bake->histogram.push_back({.Count = usage.count, .SubdivisionLevel = usage.subdivisionLevel,
                                   .Format = static_cast<D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT>(usage.format)});
      }
      const auto* indices = static_cast<const uint32_t*>(desc->indexBuffer);
      bake->indices.assign(indices, indices + desc->indexCount);
    }
    if (result != nullptr) omm_.destroy_result(result);
    omm_.destroy_texture(baker, texture);
  }
#endif

  // A capture's record flags (rt_scene.hlsli): alpha-tested leaves, thin cloth and sails (v11), smoke, grass, a leaves'
  // mesh (static or placed: LEAF_INSTANCE; a compact draw's flags with RT_DYNAMIC: its swaying copy).
  static uint32_t RecordFlags(const dlaa_bridge::RtCapture& capture, bool masked, uint32_t version) {
    return (masked ? RECORD_MASKED : 0u) | ((version >= 11 && (capture.flags & dlaa_bridge::RT_THIN) != 0) ? RECORD_THIN : 0u)
           | ((masked && (capture.flags & dlaa_bridge::RT_SMOKE) != 0) ? RECORD_SMOKE : 0u)
           | ((masked && (capture.flags & (dlaa_bridge::RT_GRASS | dlaa_bridge::RT_SOLID_CUT_OUT)) != 0) ? RECORD_GRASS : 0u)
           | ((masked && (capture.flags & (dlaa_bridge::RT_DYNAMIC | dlaa_bridge::RT_GRASS | dlaa_bridge::RT_SMOKE | dlaa_bridge::RT_SOLID_CUT_OUT)) == 0) ? RECORD_LEAF : 0u);
  }

  // A static mesh's instances' group: grass, leaves (alpha-tested), solid cut-outs (a fence, a thatched wall), or solid.
  static UINT InstanceMask(const Mesh& mesh) {
    if (mesh.grass) return GRASS_INSTANCE;
    if (mesh.solid_cut_out) return CUT_OUT_INSTANCE;
    return mesh.foliage ? LEAF_INSTANCE : SOLID_INSTANCE;
  }

  // A world-space BLAS into the slot's TLAS instances (leaves not opaque: their alpha is tested), its InstanceID its
  // first geometry's record; placed by `transform` (null: where it was captured); `place` its partitioned TLAS index
  // (kept in InstanceContributionToHitGroupIndex: no hit groups here).
  // Grass with micromaps (DXR 1.2): 2-state for every ray (NVIDIA's OMM guide: 2-state for secondary rays), its unknown
  // states opaque or transparent as they lean: no alpha tests (ATN's debug29: sun rays 22-27 ms zoomed into grass and over
  // a river's grassy bed).
  void AddInstance(Slot* slot, UINT* instance_count, D3D12_GPU_VIRTUAL_ADDRESS blas, uint32_t record, bool foliage,
                   const std::array<float, 12>* transform, uint32_t place, UINT mask) const {
    D3D12_RAYTRACING_INSTANCE_DESC& instance = slot->instance_data[(*instance_count)++];
    instance = {
        .InstanceID = record,
        .InstanceMask = mask,
        .InstanceContributionToHitGroupIndex = place,
        .Flags = static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE
                                   | (foliage ? 0u : static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE))),
        .AccelerationStructure = blas,
    };
    if (transform != nullptr) {
      std::memcpy(instance.Transform, transform->data(), sizeof(instance.Transform));
    } else {
      instance.Transform[0][0] = 1.f;
      instance.Transform[1][1] = 1.f;
      instance.Transform[2][2] = 1.f;
    }
  }

  ComPtr<ID3D12Device5> device_;
  bool dxr12_ = false;
  std::string sdk_;
  ComPtr<ID3D12CommandQueue> queue_;
  std::array<Slot, SLOTS> slots_;
  size_t next_slot_ = 0;
  ComPtr<ID3D12Fence> own_fence_;
  uint64_t own_value_ = 0;
  uint64_t trace_value_ = 0;  // own_fence_'s value after the last trace
  HANDLE event_ = nullptr;
  ComPtr<ID3D12Resource> vertex_buffer_;
  ComPtr<ID3D12Resource> scratch_;
  uint64_t scratch_bytes_ = 0;
  ComPtr<ID3D12Resource> tlas_;
  ComPtr<ID3D12Resource> tlas_scratch_;
  ComPtr<ID3D12Resource> records_;
  ComPtr<ID3D12Resource> uvs_;
  ComPtr<ID3D12Resource> normals_;
  // Placed meshes (v14): their build rows (three texels a record), the instances the placement dispatches write in turn
  // (the last one written), their counters and dedupe table (rt_place's u8), the dispatch.
  ComPtr<ID3D12Resource> build_rows_;
  ComPtr<ID3D12Resource> left_out_start_;
  LeftOut left_out_;
  // The leaves' opacity micromaps (NVIDIA's OMM SDK; none without omm-lib.dll), on their way: read back by a request in
  // flight, read (waiting for the bake thread), on it, baked (waiting for a request to build them).
  bool bake_micromaps_ = false;
  std::vector<std::unique_ptr<MicromapBake>> micromap_reads_;
  std::vector<std::unique_ptr<MicromapBake>> micromap_queue_;
  std::array<std::future<std::unique_ptr<MicromapBake>>, MICROMAP_WORKERS> micromap_baking_;
  std::vector<std::unique_ptr<MicromapBake>> micromaps_baked_;
  // Since the last log line: meshes baked, their data's bytes, failed, of them grass sets (their templates rebuilt).
  std::array<uint64_t, 4> micromap_tally_ = {};
  bool cluster_micromaps_ = false;  // Mega Geometry's operations allow micromaps (grass sets' templates)
  uint64_t set_generation_ = 0;
  uint64_t micromap_logged_ = 0;                 // that line's time (GetTickCount64)
#if defined(RENODX_OMM_SDK)
  OmmSdk omm_;
  OmmGpu omm_gpu_;
#endif
  bool gpu_micromaps_ = false;  // GPU Micromap Baking ready (MakeGpuBaker)
  ComPtr<ID3D12InfoQueue> info_queue_;  // RENODX_TW_D3D12_DEBUG
  HRESULT debug_result_ = S_FALSE;
  // For the log: the device's start, a slow request's report (GPU over SLOW_MS; at most SLOW_REPORTS), a garbage
  // triangle's (one in 10 s).
  std::string slow_report_;
  uint32_t slow_reports_ = 0;
  std::string garbage_example_;
  uint64_t garbage_example_time_ = 0;
  std::array<ComPtr<ID3D12Resource>, 2> placed_instances_;
  ComPtr<ID3D12Resource> smoke_tlas_;  // the smoke's TLAS (SmokeTransmittance), its one instance
  ComPtr<ID3D12Resource> smoke_tlas_scratch_;
  uint32_t placed_last_ = 0;
  ComPtr<ID3D12Resource> placed_state_;
  uint32_t placed_capacity_ = MAX_INSTANCES;  // their room now (PlacedRoom)
  uint32_t placed_limit_ = MAX_INSTANCES;     // the most (by the GPU's memory)
  uint32_t placed_written_ = 0;               // the last trace's, fitting or not (rt_place's counter)
  bool placed_rule_ = false;                  // One Model per Place as the last trace had it
  uint32_t placed_before_rule_ = 0;           // placed_written_ when it changed
  uint32_t placed_rule_traces_ = 0;           // traces until the log line after a change (0: said)
  size_t placed_slot_ = SLOTS;                // whose readback holds the counter of the last trace (SLOTS: read already)
  std::future<std::unique_ptr<PlacedRoom>> placed_growing_;
  std::string room_note_;  // the status's: a room that found no memory
  ComPtr<ID3D12PipelineState> place_pipeline_;
  // The units' BLAS (refitted in place), the vertex counts of its draws at its last build, refits since.
  ComPtr<ID3D12Resource> units_blas_;
  uint64_t units_bytes_ = 0;
  // The smoke's BLAS (built each request; Mega Geometry's in cluster_blases_[3]).
  ComPtr<ID3D12Resource> smoke_blas_;
  uint64_t smoke_bytes_ = 0;
  bool clusters_ = false;  // Mega Geometry's cluster operations for the units
  uint32_t cluster_limit_ = MAX_CLUSTERS;  // the most a frame (CLUSTER_TIERS)
  uint32_t cluster_room_ = MAX_CLUSTERS;   // room for them now (ClusterRoom: grown while a frame fills 3/4 of it)
  bool clusters_crowded_ = false;          // the last request's clusters filled 3/4 of a run's room or more
  uint64_t templated_wanted_ = 0;          // and the clusters its runs wanted (fitting or not)
  uint64_t built_wanted_ = 0;
  uint32_t built_limit_ = MAX_CLUSTERS;    // of the room built from triangles (the rest instantiated from templates)
  uint64_t cluster_bytes_ = 0;             // the room's CLASes, cluster BLAS, scratch and arguments (the status)
  uint32_t dynamic_uvs_ = DYNAMIC_UVS;     // a request's alpha-tested corners (by the GPU's memory)
  uint32_t uv_pool_ = STATIC_UVS + (SLOTS * DYNAMIC_UVS);
#if defined(RENODX_NVAPI_CLUSTERS)
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS clas_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS cluster_blas_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS template_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS instantiate_inputs_ = {};
  std::future<std::unique_ptr<ClusterRoom>> cluster_growing_;  // a bigger room being made (a worker's)
#endif
  ComPtr<ID3D12Resource> clas_;
  ComPtr<ID3D12Resource> clas_addresses_;
  std::array<ComPtr<ID3D12Resource>, CLUSTER_BLASES> cluster_blases_;  // CLUSTER_BLASES' (the units' at first, cluster_destination_)
  ComPtr<ID3D12Resource> cluster_destination_;
  ComPtr<ID3D12Resource> cluster_scratch_;
  ComPtr<ID3D12Resource> cluster_indices_;
  bool cluster_indices_written_ = false;
  // The CLASes' arguments a request builds (the compact pass's, then the captures' copied from the slot), the compact
  // draws' cluster sets by key in their pool, its free units, the times a full pool dropped sets (CompactState) and the
  // compact pass.
  ComPtr<ID3D12Resource> cluster_args_;
  ComPtr<ID3D12Resource> set_pool_;
  // The cluster templates (TEMPLATE_POOL_BYTES, template_bytes_ each, a set's together), a request's builds' places, the
  // instantiated CLASes' share of clas_.
  ComPtr<ID3D12Resource> templates_;
  ComPtr<ID3D12Resource> template_destinations_;
  // Zero geometry indices for a template's triangles: an instantiation's geometry index offset is only applied to a
  // template built with them (without, the instantiated triangles' geometry index, their record, came out wrong).
  ComPtr<ID3D12Resource> template_geometry_;
  uint32_t template_bytes_ = 0;
  uint64_t instantiated_bytes_ = 0;
  Ranges free_templates_;
  std::unordered_map<uint64_t, ClusterSet> sets_;
  Ranges free_set_units_;
  std::vector<PendingSetFree> pending_set_frees_;
  uint32_t set_drops_ = 0;
  ComPtr<ID3D12PipelineState> compact_pipeline_;
  // Swaying Shadows Range / Off-Screen Sway (rt_sway.cs_6_5.hlsl; null: unavailable): the kept clusters' arguments
  // (cluster_args_'s size, resting in NON_PIXEL_SHADER_RESOURCE), the cluster BLASes' lists (clas_addresses_'s size, in
  // UNORDERED_ACCESS), their arguments and the CLAS counts (SWAY_COUNTS_BYTES, resting in NON_PIXEL_SHADER_RESOURCE).
  ComPtr<ID3D12PipelineState> sway_pipeline_;
  ComPtr<ID3D12Resource> sway_arguments_;
  ComPtr<ID3D12Resource> sway_lists_;
  ComPtr<ID3D12Resource> sway_counts_;
  // The partitioned TLAS (null: the TLAS is built each frame) and each of its instances as last written (its BLAS, 0
  // inactive; its InstanceID and flags), built once before the first update.
#if defined(RENODX_NVAPI_CLUSTERS)
  NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_INPUTS ptlas_inputs_ = {};
#endif
  ID3D12Resource* ptlas_ = nullptr;  // the last built of ptlas_buffers_
  std::array<ComPtr<ID3D12Resource>, 2> ptlas_buffers_;
  uint32_t ptlas_current_ = 0;
  ComPtr<ID3D12Resource> ptlas_scratch_;
  struct PtlasInstance {
    uint64_t blas = 0;
    uint32_t id = 0;
    uint32_t flags = UINT32_MAX;  // never written
    std::array<float, 12> transform = {};
    uint32_t stamp = 0;  // the request it was wanted in (ptlas_wanted_)
  };
  std::vector<PtlasInstance> ptlas_written_;
  std::vector<PtlasInstance> ptlas_wanted_;
  std::vector<uint32_t> ptlas_places_;  // last request's
  uint32_t ptlas_stamp_ = 0;
  bool ptlas_built_ = false;
  std::vector<uint32_t> units_layout_;
  uint32_t units_refits_ = 0;
  uint32_t units_record_ = 0;
  bool units_masked_ = false;  // grass among them
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> trace_pipeline_;
  ComPtr<ID3D12PipelineState> soft_pipeline_;
#if defined(RENODX_NRD)
  NrdSignal nrd_sun_;
  NrdSignal nrd_ambient_;
  NrdSignal nrd_bounce_;
  NrdSignal nrd_reflections_;
  ComPtr<ID3D12Resource> nrd_bounce_hit_distance_;
#endif
  ComPtr<ID3D12PipelineState> scene_data_pipeline_;
  ComPtr<ID3D12DescriptorHeap> descriptors_;
  UINT descriptor_size_ = 0;
  Ranges free_triangles_;
  Ranges free_uvs_;
  Ranges free_instances_;  // the instanced meshes' places, STATIC_RECORDS to MAX_INSTANCES
  std::vector<uint32_t> free_records_;
  std::vector<PendingFree> pending_frees_;
  // The addon's.
  ComPtr<ID3D12Resource> vertices_;
  ComPtr<ID3D12Resource> positions_;
  ComPtr<ID3D12Resource> sun_;
  ComPtr<ID3D12Resource> visibility_;
  ComPtr<ID3D12Resource> ambient_;
  ComPtr<ID3D12Resource> atlas_;
  ComPtr<ID3D12Resource> light_;   // the ray-traced Light Bounce's prepared light (v7)
  ComPtr<ID3D12Resource> bounce_;  // its output
  ComPtr<ID3D12Resource> light_mask_;  // the local lights' lit bits (v9)
  ComPtr<ID3D12Resource> fog_;         // the fog shadows (v11)
  ComPtr<ID3D12Resource> fog_lights_;  // the local lights' light in the fog (v18)
  ComPtr<ID3D12Resource> tint_;        // Coloured Shadows' filter (v19)
  // The ambient visibility kept over frames (v11): its two sets, the last trace's positions, camera, frame and set.
  std::array<ComPtr<ID3D12Resource>, 2> ambient_history_;
  ComPtr<ID3D12Resource> ambient_positions_;
  std::array<float, 16> ambient_view_projection_ = {};
  uint32_t ambient_frame_ = UINT32_MAX;
  uint32_t ambient_parity_ = 0;
  // The local lights' bits kept over frames (HISTORY_LIGHTS), the last trace that wrote them and its lights.
  std::array<ComPtr<ID3D12Resource>, 2> light_history_;
  uint32_t lights_frame_ = UINT32_MAX;
  uint32_t lights_mode_traced_ = 0;  // the last kept lights' bits' Torch and Fire Light in the Light Bounce mode (0 none, 1, 3 the ratio)
  std::array<std::array<float, 4>, dlaa_bridge::RT_ALL_LIGHTS> previous_lights_ = {};
  uint32_t previous_light_count_ = 0;
  ComPtr<ID3D12PipelineState> fog_pipeline_;
  ComPtr<ID3D12PipelineState> lights_pipeline_;
  // Torch and Fire Light in the Light Bounce (TORCHES_IN_BOUNCE builds): the local lights' trace, the reuse.
  ComPtr<ID3D12PipelineState> lights_bounce_pipeline_;
  ComPtr<ID3D12PipelineState> reuse_lights_pipeline_;
  ComPtr<ID3D12PipelineState> bounce_pipeline_;
  // Paths (v8): last frame's colour (the DLAA input), the G-buffer guides, the reflections' outputs; the history and the
  // cache of the helper's own; the pipelines.
  ComPtr<ID3D12Resource> previous_color_;
  ComPtr<ID3D12Resource> guide_normals_;
  ComPtr<ID3D12Resource> guide_albedo_;
  ComPtr<ID3D12Resource> guide_roughness_;
  ComPtr<ID3D12Resource> reflection_;
  ComPtr<ID3D12Resource> specular_;
  ComPtr<ID3D12Resource> hit_distance_;  // Ray Reconstruction's specular hit distance (v17; u31)
  // Last frame's positions and this frame's for the next (t11), a slice each: copied_slice_ took the last copy,
  // previous_positions_slice_ is what this frame's requests read.
  ComPtr<ID3D12Resource> kept_positions_;
  uint32_t copied_slice_ = 0;
  uint32_t previous_positions_slice_ = 0;
  // Softer Water Reflections: the positions at the reflections request of frame water_frame_ (the water's surface).
  ComPtr<ID3D12Resource> water_positions_;
  uint32_t water_frame_ = UINT32_MAX;
  ComPtr<ID3D12Resource> specular_accumulated_;
  ComPtr<ID3D12Resource> specular_history_;
  ComPtr<ID3D12Resource> specular_meta_;
  ComPtr<ID3D12Resource> specular_meta_history_;
  ComPtr<ID3D12Resource> cache_keys_;
  ComPtr<ID3D12Resource> cache_radiance_;
  ComPtr<ID3D12Resource> cache_sums_;
  ComPtr<ID3D12Resource> cache_albedo_;
  ComPtr<ID3D12Resource> light_fit_;
  ComPtr<ID3D12Resource> light_kept_;
  uint32_t path_ambient_frame_ = UINT32_MAX;  // the last frame the paths took the sky for the game's ambient
  bool path_kept_cube_ = false;               // RT_PATH_KEPT_CUBE of that frame's request (the reflections' requests don't carry it)
  bool path_sun_light_ = false;               // RT_PATH_SUN_LIGHT of that frame's request
  ComPtr<ID3D12Resource> kept_readback_;      // light_kept_ at the last freeze, for the log
  bool kept_copy_wanted_ = false;
  uint32_t kept_copy_frame_ = UINT32_MAX;
  ComPtr<ID3D12PipelineState> path_pipeline_;
  ComPtr<ID3D12PipelineState> reflect_pipeline_;
  // Path Tracing SER (DXR 1.2; null: unavailable): the paths' and reflections' ray generation shaders, their records (the
  // paths' at 0, the reflections' at PATH_SER_RECORD).
  ComPtr<ID3D12StateObject> path_ser_;
  ComPtr<ID3D12Resource> path_ser_table_;
  static constexpr uint64_t PATH_SER_RECORD = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
  // SHaRC Radiance Cache (rt_sharc.hlsli; null pipelines: unavailable): the paths' SHaRC variant, its update and resolve,
  // its buffers (SHARC_ENTRIES each of SHARC_STRIDES: the keys, a frame's sums, the resolved light;
  // made at first use, zeroed as committed buffers are) and the frame its update last ran.
  ComPtr<ID3D12PipelineState> sharc_path_pipeline_;
  ComPtr<ID3D12PipelineState> sharc_update_pipeline_;
  ComPtr<ID3D12PipelineState> sharc_resolve_pipeline_;
  std::array<ComPtr<ID3D12Resource>, 3> sharc_buffers_;
  uint32_t sharc_frame_ = UINT32_MAX;
  std::string sharc_failed_;  // its pipeline that failed
  static constexpr uint32_t SHARC_ENTRIES = 1u << 21;  // rt_sharc.hlsli SHARC_CAPACITY
  static constexpr std::array<uint64_t, 3> SHARC_STRIDES = {8, 16, 16};
  static constexpr uint32_t SHARC_BLOCK = 5;  // rt_sharc_update.hlsl's
  ComPtr<ID3D12PipelineState> reuse_pipeline_;  // ReSTIR GI's spatial reuse
  ComPtr<ID3D12PipelineState> reuse_lean_pipeline_;  // its Lean Reuse build (REUSE_LEAN)
  std::array<ComPtr<ID3D12Resource>, 2> reuse_samples_;
  std::array<ComPtr<ID3D12Resource>, 2> reuse_data_;
  uint32_t reuse_frame_ = UINT32_MAX;  // the frame the reservoirs were last made in
  uint32_t reuse_lights_mode_ = 0;     // its Torch and Fire Light in the Light Bounce mode (rt_camera.hlsli LIGHTS_MODE; 0: none)
  uint32_t reuse_parity_ = 0;          // this frame's set
  std::array<uint32_t, 3> reuse_size_ = {};  // the bounce's size and scale they are at
  std::array<ComPtr<ID3D12Resource>, 2> own_sky_;  // Path-Traced Ambient's own sky at the bounce texels (rt_path.hlsli ScreenSky)
  uint32_t sky_frame_ = UINT32_MAX;          // the frame the paths last wrote it in
  uint32_t sky_parity_ = 0;                  // the set the next paths write
  std::array<uint32_t, 3> sky_size_ = {};    // the bounce's size and scale it is at
  // Torch and Fire Shadows from the Light Bounce (rt_path.hlsli ReuseTexel, rt_lights): the ratio's two sets at the bounce
  // texels, the set the next bounce writes, the frame and texels of the last.
  std::array<ComPtr<ID3D12Resource>, 2> light_ratio_;
  uint32_t ratio_parity_ = 0;
  uint32_t ratio_frame_ = UINT32_MAX;
  std::array<uint32_t, 3> ratio_size_ = {};
  ComPtr<ID3D12PipelineState> reflect_reservoir_pipeline_;  // Own ReSTIR's reservoirs
  std::array<ComPtr<ID3D12Resource>, 2> reflect_samples_;
  std::array<ComPtr<ID3D12Resource>, 2> reflect_data_;
  uint32_t reflect_frame_ = UINT32_MAX;  // the frame they were last made in
  uint32_t reflect_parity_ = 0;          // this frame's set
  std::array<uint32_t, 2> reflect_size_ = {};  // the render size they are at
  ComPtr<ID3D12PipelineState> cache_fill_pipeline_;
  ComPtr<ID3D12PipelineState> cache_resolve_pipeline_;
  ComPtr<ID3D12PipelineState> reflect_filter_pipeline_;
  // GPU times (but with RENODX_TW_NO_GPU_TIMES): each slot's timestamps (a trace: its start, its TLAS, its trace, its end; paths:
  // their start and end), read when the slot comes round again; the last trace's and paths' phases in milliseconds.
  ComPtr<ID3D12QueryHeap> timestamps_;
  ComPtr<ID3D12Resource> timestamp_readback_;
  uint64_t timestamp_frequency_ = 0;
  std::array<bool, SLOTS> slot_traced_ = {};
  std::array<bool, SLOTS> slot_reflected_ = {};  // a path request's: the reflections (else the Light Bounce)
  std::array<bool, SLOTS> slot_clustered_ = {};  // a trace's: its clusters built (timed by the paths' stamps)
  std::array<uint32_t, 2> paths_age_ = {};  // the sun traces since the Light Bounce's, the reflections' last times
  // The scene (captures, BLASes), the TLAS, the trace; the Light Bounce, the reflections; the scene's cluster build.
  std::array<double, 6> gpu_ms_ = {};
  std::array<float, 16> current_view_projection_ = {};
  std::array<float, 16> previous_view_projection_ = {};
  uint32_t paths_frame_ = UINT32_MAX;     // the frame of the last path request (the history copied then)
  uint32_t copied_frame_ = UINT32_MAX;    // the frame kept_positions_' copied_slice_ holds
  uint32_t light_frame_ = UINT32_MAX;     // the frame the prepared light is from
  uint32_t cache_frame_ = UINT32_MAX;     // the frame the cache was last filled
  uint32_t specular_frame_ = UINT32_MAX;  // the frame of the last reflections
  uint32_t sun_traced_frame_ = UINT32_MAX;
  bool history_valid_ = false;
  bool cache_filled_ = false;
  bool specular_history_valid_ = false;
  bool traced_ = false;  // a TLAS was built (the bounce traces it)
  std::array<uint32_t, HISTORY_KINDS> history_used_ = {};  // the frame each history was last used in
  ComPtr<ID3D12Resource> sun_rays_;  // the helper's, the textures' size
  ComPtr<ID3D12Fence> fence_;
  std::unordered_map<uint64_t, Mesh> meshes_;
  uint64_t builds_ = 0;
  std::vector<Release> releases_;
  // The static BLASes' pools (BlasPlace): the builds' and the compacted ones', each its chunks.
  struct BlasChunk {
    ComPtr<ID3D12Resource> buffer;
    Ranges free;
  };
  std::array<std::vector<BlasChunk>, 2> blas_pools_;
  size_t blas_chunks_logged_ = 0;  // their chunks at the last log line
  bool failed_ = false;
  std::atomic_bool request_failed_ = false;  // Lost (the worker sets it, main.cpp reads it)
  std::atomic_bool timed_out_ = false;       // TimedOut (likewise)
  bool details_ = false;  // the addon's Log Details or profile (this request's): GPU times, markers, the status, the log's lines
  void NoteNrd(uint32_t signal, bool ran) {
    if (ran) {
      nrd_off_.fetch_and(~signal);
    } else {
      nrd_off_.fetch_or(signal);
    }
  }

  std::atomic<uint32_t> nrd_off_ = 0;  // published from the helper's worker, read beside it
  std::string status_ = "Ray tracing off";
  // The last trace's times: the GPU's (after the status's label), the CPU's and the submit's (at its end).
  std::string gpu_timings_;
  std::string cpu_timings_;
  // The addon's ray tracing profile (RT_PROFILE, Record Ray Tracing Profile): each request's line into
  // tw_rt_profile_helper.csv in the helper's folder (made again at a recording's first trace), the last Record's counts
  // (PROFILE_COUNTS fields).
  static constexpr size_t PROFILE_COUNTS = 17;
  std::filesystem::path folder_;
  std::ofstream profile_;
  bool profiling_ = false;
  // A recording's trace being recorded: its passes' marks and when the CPU reached them.
  std::vector<std::pair<const wchar_t*, LARGE_INTEGER>> cpu_marks_;
  std::string profile_counts_;
};

}  // namespace twattila::dlaa_helper
