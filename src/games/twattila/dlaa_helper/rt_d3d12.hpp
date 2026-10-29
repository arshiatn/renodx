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
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <future>
#include <map>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../dlaa_bridge.h"
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
#include "rt_bounce.h"            // dxc -Fh (build.ps1)
#include "rt_bounce_omm.h"        // dxc -Fh (build.ps1)
#include "rt_cache_fill.h"        // dxc -Fh (build.ps1)
#include "rt_cache_resolve.h"     // dxc -Fh (build.ps1)
#include "rt_compact.h"           // dxc -Fh (build.ps1)
#include "rt_fog.h"               // dxc -Fh (build.ps1)
#include "rt_lights.h"            // dxc -Fh (build.ps1)
#include "rt_lights_omm.h"        // dxc -Fh (build.ps1)
#include "rt_fog_omm.h"           // dxc -Fh (build.ps1)
#include "rt_path.h"              // dxc -Fh (build.ps1)
#include "rt_path_omm.h"          // dxc -Fh (build.ps1)
#include "rt_place.h"             // dxc -Fh (build.ps1)
#include "rt_reflect.h"           // dxc -Fh (build.ps1)
#include "rt_reflect_filter.h"    // dxc -Fh (build.ps1)
#include "rt_reflect_omm.h"       // dxc -Fh (build.ps1)
#include "rt_reuse.h"             // dxc -Fh (build.ps1)
#include "rt_reuse_omm.h"         // dxc -Fh (build.ps1)
#include "rt_scene_data.h"        // dxc -Fh (build.ps1)
#include "rt_soft.h"              // dxc -Fh (build.ps1)
#include "rt_trace.h"             // dxc -Fh (build.ps1)
#include "rt_trace_omm.h"         // dxc -Fh (build.ps1)

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
// 640 x 360 to 4K, with and without clutter in view: the paths' hits need lookups, not divergent shading). With
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
  bool Initialize(IDXGIAdapter1* adapter, const std::filesystem::path& folder) {
    if (failed_) return false;
    if (device_) return true;
    // The Agility SDK through a device factory of its own (no developer mode; frame generation and Ray Reconstruction
    // keep Windows' runtime).
    std::string sdk = "Windows' D3D12";
    const std::filesystem::path agility = folder / L"D3D12";
    ComPtr<ID3D12SDKConfiguration1> configuration;
    ComPtr<ID3D12DeviceFactory> factory;
    // DRED (Device Removed Extended Data): after a GPU fault or hang (ATN 2026-10-28: frozen, then no ray tracing until
    // the game restarts), each command list's operations up to where the GPU stopped, the passes' markers, a page fault's
    // allocations (LostReport).
    const auto record_removal = [](ID3D12DeviceRemovedExtendedDataSettings1* settings) {
      settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
      settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
      settings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
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
      if (SUCCEEDED(factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf())))) sdk = "Agility SDK";
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
      if (load(&create_baker, "ommCreateBaker") && load(&omm_.destroy_baker, "ommDestroyBaker")
          && load(&omm_.create_texture, "ommCpuCreateTexture") && load(&omm_.destroy_texture, "ommCpuDestroyTexture")
          && load(&omm_.bake, "ommCpuBake") && load(&omm_.result_desc, "ommCpuGetBakeResultDesc")
          && load(&omm_.destroy_result, "ommCpuDestroyBakeResult") && create_baker(&creation, &omm_.baker) == ommResult_SUCCESS) {
        omm_.library = library;
      } else {
        FreeLibrary(library);
        omm_ = {};
      }
    }
    bake_micromaps_ = omm_.baker != nullptr;
#endif
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if ((!device_ && FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf()))))
        || FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))
        || options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
      return Fail("Ray tracing needs a GPU and driver with DXR 1.1");
    }
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {.HighestShaderModel = D3D_SHADER_MODEL_6_9};
    dxr12_ = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2
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
      const D3D12_QUERY_HEAP_DESC heap_desc = {.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = static_cast<UINT>(SLOTS * 6)};
      const D3D12_HEAP_PROPERTIES readback_heap = {.Type = D3D12_HEAP_TYPE_READBACK};
      const D3D12_RESOURCE_DESC readback_desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = SLOTS * 6 * sizeof(uint64_t),
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
    for (Slot& slot : slots_) {
      slot.instances = Buffer(uint64_t{MAX_INSTANCES} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                              D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.table = Buffer(uint64_t{dlaa_bridge::RT_CAPTURES} * 32, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.sizes = Buffer(uint64_t{dlaa_bridge::RT_CAPTURES} * sizeof(uint64_t), D3D12_HEAP_TYPE_DEFAULT,
                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      slot.readback = Buffer(uint64_t{dlaa_bridge::RT_CAPTURES} * sizeof(uint64_t), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                             D3D12_RESOURCE_STATE_COPY_DEST);
      slot.rows_readback = Buffer(uint64_t{ROWS_READBACK} * 64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      slot.jobs = Buffer(PLACE_TABLE_BYTES + uint64_t{PLACE_JOBS} * 32, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                         D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.left_out = Buffer(LEFT_OUT_BYTES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
      slot.left_out_readback = Buffer(LEFT_OUT_BYTES, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                               IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close()) || !slot.instances || !slot.table || !slot.sizes || !slot.readback || !slot.rows_readback || !slot.jobs
          || !slot.left_out || !slot.left_out_readback
          || FAILED(slot.left_out_readback->Map(0, nullptr, reinterpret_cast<void**>(&slot.left_out_data)))
          || FAILED(slot.instances->Map(0, nullptr, reinterpret_cast<void**>(&slot.instance_data)))
          || FAILED(slot.table->Map(0, nullptr, reinterpret_cast<void**>(&slot.table_data)))
          || FAILED(slot.jobs->Map(0, nullptr, reinterpret_cast<void**>(&slot.jobs_data)))) {
        return Fail("Ray tracing: no D3D12 command lists");
      }
      slot.list->SetName(std::format(L"slot {}", &slot - slots_.data()).c_str());
    }
    // The vertex copy (BLAS inputs), the TLAS for the most instances, the records and pools.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top_inputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
        .NumDescs = MAX_INSTANCES,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
    };
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_info = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&top_inputs, &top_info);
    vertex_buffer_ = Buffer(uint64_t{TRANSFER_VERTICES} * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
    // The scene data pass's garbage count starts from these (none, no capture).
    left_out_start_ = Buffer(LEFT_OUT_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (uint32_t* start = nullptr; left_out_start_ && SUCCEEDED(left_out_start_->Map(0, nullptr, reinterpret_cast<void**>(&start)))) {
      const std::array<uint32_t, LEFT_OUT_BYTES / 4> none = {0, 0, 0, UINT32_MAX};
      std::memcpy(start, none.data(), sizeof(none));
      left_out_start_->Unmap(0, nullptr);
    }
    tlas_ = Buffer(top_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    tlas_scratch_ = Buffer(top_info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto pool = [&](uint64_t bytes) {
      return Buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    // A request's alpha-tested corners (grass, swaying leaves): all on GPUs over 20 GB, half over 12 GB, else a quarter
    // (the addon reads it back: rt_dynamic_uvs).
    DXGI_ADAPTER_DESC1 adapter_desc = {};
    adapter->GetDesc1(&adapter_desc);
    dynamic_uvs_ = (adapter_desc.DedicatedVideoMemory > (size_t{20} << 30))   ? DYNAMIC_UVS
                   : (adapter_desc.DedicatedVideoMemory > (size_t{12} << 30)) ? DYNAMIC_UVS / 2
                                                                              : DYNAMIC_UVS / 4;
    uv_pool_ = STATIC_UVS + (SLOTS * dynamic_uvs_);
    records_ = pool(uint64_t{RECORD_COUNT} * 16);
    uvs_ = pool(uint64_t{uv_pool_} * 4);
    normals_ = pool(uint64_t{NORMAL_POOL} * 4);
    // Placed meshes (v14): their build rows by record; the TLAS's instances when some are placed, two in turn (the last
    // one's carried instances copied into the next).
    build_rows_ = Buffer(uint64_t{STATIC_RECORDS} * 48, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (auto& placed : placed_instances_) {
      placed = Buffer(uint64_t{MAX_INSTANCES} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    placed_state_ = Buffer(16 + uint64_t{PLACE_TABLE} * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!vertex_buffer_ || !left_out_start_ || !tlas_ || !tlas_scratch_ || !records_ || !uvs_ || !normals_ || !build_rows_
        || !placed_instances_[0] || !placed_instances_[1] || !placed_state_) {
      return Fail("Ray tracing: D3D12 buffers failed");
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    // Mega Geometry: the CLASes (placed by the driver, their addresses in clas_addresses_), the units' cluster BLAS (at
    // cluster_blas_, its address in cluster_destination_), the scratch and the arguments, all with room for a frame's
    // clusters (SizeClusters); the sequential 8-bit indices; a slot's captures' arguments and records written by the CPU
    // (cluster_data), its compact draws' table and new sets (compact_data); the sets' pool. A cluster has at most
    // RT_CLUSTER_TRIANGLES triangles (a compact draw's) over 255 vertices (a capture's sequential ones) and takes ~6 KB of
    // CLAS memory at most: the room starts at CLUSTER_ROOM (ATN's battles drew 8,000-21,000 a frame) and doubles while a
    // frame has more, up to MAX_CLUSTERS, half on GPUs of 20 GB or less, a quarter on 12 GB or less (4x unit size battles
    // filled 262,144 with city walls left out).
    if (clusters_) {
      cluster_limit_ = (adapter_desc.DedicatedVideoMemory > (size_t{20} << 30))   ? MAX_CLUSTERS
                       : (adapter_desc.DedicatedVideoMemory > (size_t{12} << 30)) ? MAX_CLUSTERS / 2
                                                                                  : MAX_CLUSTERS / 4;
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
        return ClusterRequirements(one_template, &template_info) && SizeClusters(std::min(cluster_limit_, CLUSTER_ROOM));
      };
      allow_micromaps(dxr12_ && bake_micromaps_);
      clusters_ = size_clusters();
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
    const std::array<D3D12_ROOT_PARAMETER, 11> parameters = {{
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
             && pipeline(RT_FOG_OMM, sizeof(RT_FOG_OMM), &fog_pipeline_) && pipeline(RT_LIGHTS_OMM, sizeof(RT_LIGHTS_OMM), &lights_pipeline_);
    if (!dxr12_) {
      trace_pipeline_.Reset();
      bounce_pipeline_.Reset();
      path_pipeline_.Reset();
      reflect_pipeline_.Reset();
      reuse_pipeline_.Reset();
      fog_pipeline_.Reset();
      lights_pipeline_.Reset();
    }
    if ((!trace_pipeline_ && !pipeline(RT_TRACE, sizeof(RT_TRACE), &trace_pipeline_))
        || (!bounce_pipeline_ && !pipeline(RT_BOUNCE, sizeof(RT_BOUNCE), &bounce_pipeline_)) || !pipeline(RT_SOFT, sizeof(RT_SOFT), &soft_pipeline_)
        || !pipeline(RT_SCENE_DATA, sizeof(RT_SCENE_DATA), &scene_data_pipeline_) || !pipeline(RT_PLACE, sizeof(RT_PLACE), &place_pipeline_)
        || (!path_pipeline_ && !pipeline(RT_PATH, sizeof(RT_PATH), &path_pipeline_))
        || (!reflect_pipeline_ && !pipeline(RT_REFLECT, sizeof(RT_REFLECT), &reflect_pipeline_))
        || (!reuse_pipeline_ && !pipeline(RT_REUSE, sizeof(RT_REUSE), &reuse_pipeline_))
        || (!fog_pipeline_ && !pipeline(RT_FOG, sizeof(RT_FOG), &fog_pipeline_))
        || (!lights_pipeline_ && !pipeline(RT_LIGHTS, sizeof(RT_LIGHTS), &lights_pipeline_))
        || !pipeline(RT_CACHE_FILL, sizeof(RT_CACHE_FILL), &cache_fill_pipeline_)
        || !pipeline(RT_CACHE_RESOLVE, sizeof(RT_CACHE_RESOLVE), &cache_resolve_pipeline_)
        || !pipeline(RT_REFLECT_FILTER, sizeof(RT_REFLECT_FILTER), &reflect_filter_pipeline_)
        || FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(descriptors_.GetAddressOf())))) {
      return Fail("Ray tracing: the trace shaders failed (needs shader model 6.5)");
    }
    clusters_ = clusters_ && pipeline(RT_COMPACT, sizeof(RT_COMPACT), &compact_pipeline_);
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
    status_ = std::format("Ray tracing ready ({}{}; {}{})", dxr12_ ? "DXR 1.2: opacity micromaps" : "DXR 1.1",
                          (dxr12_ && bake_micromaps_) ? "; leaf micromaps: NVIDIA OMM SDK" : "", sdk,
                          clusters_ ? std::format("; Mega Geometry: room for {} clusters a frame, up to {} ({} from templates of {} bytes), {} MB",
                                                  cluster_room_, cluster_limit_, cluster_room_ - built_limit_, template_bytes_,
                                                  (cluster_bytes_ + TEMPLATE_POOL_BYTES) >> 20)
                                    : std::string());
    slow_report_ = "[TW ray tracing] " + status_ + ".";
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
    if (own_fence_) WaitFor(own_value_);
    vertices_.Reset();
    positions_.Reset();
    sun_.Reset();
    visibility_.Reset();
    ambient_.Reset();
    atlas_.Reset();
    sun_rays_.Reset();
    light_.Reset();
    bounce_.Reset();
    light_mask_.Reset();
    fog_.Reset();
    ambient_history_ = {};
    ambient_positions_.Reset();
    light_history_ = {};
    previous_color_.Reset();
    guide_normals_.Reset();
    guide_albedo_.Reset();
    guide_roughness_.Reset();
    previous_positions_.Reset();
    positions_copy_.Reset();
    reflection_.Reset();
    specular_.Reset();
    hit_distance_.Reset();
    specular_accumulated_.Reset();
    specular_history_.Reset();
    specular_meta_.Reset();
    specular_meta_history_.Reset();
    reuse_samples_ = {};
    reuse_data_ = {};
    if (descriptors_) WriteNullPathDescriptors();
    fence_.Reset();
  }

  [[nodiscard]] bool Ready() const { return fence_ != nullptr; }
  [[nodiscard]] std::string Status() const {
    // The GPU times right after the label: the bridge's line holds 255 characters, a battle's counts run past them.
    const size_t label = status_.find("): ");
    if (gpu_timings_.empty() || label == std::string::npos) return status_ + cpu_timings_;
    return std::format("{}{} | {}{}", status_.substr(0, label + 3), gpu_timings_, status_.substr(label + 3), cpu_timings_);
  }
  // A GPU fault or hang (Windows' timeout detection) removed the device: nothing runs on it again (main.cpp makes a new
  // one).
  [[nodiscard]] bool Lost() const { return device_ && FAILED(device_->GetDeviceRemovedReason()); }

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
        report += std::format("\n  {}: {} of {} operations done:", narrow(node->pCommandListDebugNameW), done, node->BreadcrumbCount);
        for (uint32_t op = (done > 12 ? done - 12 : 0u); op < std::min(node->BreadcrumbCount, done + 4); ++op) {
          std::string name;
          for (uint32_t context = 0; context < node->BreadcrumbContextsCount; ++context) {
            if (node->pBreadcrumbContexts[context].BreadcrumbIndex == op) name = narrow(node->pBreadcrumbContexts[context].pContextString);
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
    if (SUCCEEDED(removed->GetPageFaultAllocationOutput1(&fault)) && fault.PageFaultVA != 0) {
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
    return reports;
  }

  // Whether it takes compact draws (v16, rt_compact_ok): 0 not, else one more each time the full set pool dropped sets
  // (the addon sends its sets again).
  [[nodiscard]] uint32_t CompactState() const { return clusters_ ? 1u + set_drops_ : 0u; }

  // Its alpha-tested dynamic corners a request (rt_dynamic_uvs).
  [[nodiscard]] uint32_t DynamicUvs() const { return dynamic_uvs_; }

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
    const size_t slot_index = next_slot_;
    Slot& slot = slots_[slot_index];
    next_slot_ = (next_slot_ + 1) % SLOTS;
    LARGE_INTEGER cpu_start;
    QueryPerformanceCounter(&cpu_start);
    WaitFor(slot.value);
    LARGE_INTEGER cpu_waited;
    QueryPerformanceCounter(&cpu_waited);
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
    const bool recorded = SUCCEEDED(slot.allocator->Reset()) && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr))
                          && Record(shared, version, cluster_data, slot_index, &slot);
    if (!recorded) slot.capture_keys.clear();
    if (timestamps_) {
      LARGE_INTEGER cpu_recorded;
      LARGE_INTEGER cpu_frequency;
      QueryPerformanceCounter(&cpu_recorded);
      QueryPerformanceFrequency(&cpu_frequency);
      cpu_timings_ += std::format(" | CPU ms: wait {:.2f}, record {:.2f}",
                              static_cast<double>(cpu_waited.QuadPart - cpu_start.QuadPart) * 1000.0 / cpu_frequency.QuadPart,
                              static_cast<double>(cpu_recorded.QuadPart - cpu_waited.QuadPart) * 1000.0 / cpu_frequency.QuadPart);
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
    if (timestamps_) {
      LARGE_INTEGER submit_end;
      LARGE_INTEGER submit_frequency;
      QueryPerformanceCounter(&submit_end);
      QueryPerformanceFrequency(&submit_frequency);
      cpu_timings_ += std::format(", submit {:.2f}", static_cast<double>(submit_end.QuadPart - submit_start.QuadPart) * 1000.0 / submit_frequency.QuadPart);
    }
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
    WaitFor(slot.value);
    ReadTimes(slot_index);
    slot_traced_[slot_index] = false;
    slot_reflected_[slot_index] = reflections;
    const uint32_t frame = shared.rt_frame;
    const uint32_t requested = (version >= 8) ? shared.rt_path_flags : 0u;
    const bool path_traced = !reflections && (requested & dlaa_bridge::RT_PATH_TRACING) != 0;
    const bool wanted = reflections ? (reflection_ && (requested & (dlaa_bridge::RT_REFLECTIONS | dlaa_bridge::RT_SPECULAR_OCCLUSION)) != 0)
                                    : (light_ && bounce_);
    const bool uses_paths = path_traced || reflections;
    bool recorded = wanted && traced_
                    && (!uses_paths
                        || (EnsureCache() && EnsureHistory(HISTORY_PATHS, frame) && (!reflections || EnsureHistory(HISTORY_SPECULAR, frame))))
                    && SUCCEEDED(slot.allocator->Reset()) && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr));
    if (recorded) {
      auto* list = slot.list.Get();
      Stamp(list, slot_index * 6 + 4);
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
      const float bounce_range = std::clamp(shared.rt_bounce_range, 0.5f, 200.f);
      const std::array<uint32_t, 12> settings = {
          frame,
          0u,
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

      if (!uses_paths) {
        Mark(list, L"bounce");
        list->SetPipelineState(bounce_pipeline_.Get());
        list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
      } else {
        // The history at the frame's first request: last frame's positions (copied then) kept, this frame's copied.
        const uint32_t width = std::max(shared.rt_width, 1u);
        const uint32_t height = std::max(shared.rt_height, 1u);
        if (frame != paths_frame_) {
          history_valid_ = copied_frame_ != UINT32_MAX && copied_frame_ + 1 == frame && previous_color_;
          const D3D12_BOX region = {.left = 0, .top = 0, .front = 0, .right = width, .bottom = height, .back = 1};
          const auto copy = [&](ID3D12Resource* destination, ID3D12Resource* source) {
            const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = destination, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = source, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            list->CopyTextureRegion(&to, 0, 0, 0, &from, &region);
          };
          std::array<D3D12_RESOURCE_BARRIER, 3> to_copy = {
              Transition(previous_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
              Transition(positions_copy_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
              Transition(positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
          };
          list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
          copy(previous_positions_.Get(), positions_copy_.Get());
          std::array<D3D12_RESOURCE_BARRIER, 2> swap = {
              Transition(positions_copy_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
              Transition(previous_positions_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
          };
          list->ResourceBarrier(static_cast<UINT>(swap.size()), swap.data());
          copy(positions_copy_.Get(), positions_.Get());
          std::array<D3D12_RESOURCE_BARRIER, 2> back = {
              Transition(positions_copy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
              Transition(positions_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          };
          list->ResourceBarrier(static_cast<UINT>(back.size()), back.data());
          std::copy_n(current_view_projection_.begin(), 16, previous_view_projection_.begin());
          std::copy_n(shared.rt_view_projection, 16, current_view_projection_.begin());
          copied_frame_ = frame;
          paths_frame_ = frame;
        }
        const auto to_previous = Transition(previous_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &to_previous);
        if (!reflections) light_frame_ = frame;
        uint32_t flags = 0;
        if (path_traced) flags |= PATH_TRACING;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS) != 0) flags |= PATH_REFLECTIONS;
        if (reflections && (requested & dlaa_bridge::RT_SPECULAR_OCCLUSION) != 0) flags |= PATH_SPECULAR_OCCLUSION;
        if (history_valid_) flags |= PATH_HISTORY_VALID;
        if (cache_filled_) flags |= PATH_CACHE_VALID;
        // Ray Reconstruction denoises the frame: the reflections as traced (no history, no filter).
        const bool raw = reflections && (requested & dlaa_bridge::RT_REFLECTIONS_RAW) != 0;
        if (raw) flags |= PATH_RAW;
        if (reflections && !raw && specular_history_valid_ && specular_frame_ + 1 == frame) flags |= PATH_SPECULAR_HISTORY;
        if (guides) flags |= PATH_GUIDES;
        if (sun_traced_frame_ == frame) flags |= PATH_SUN_TRACED;
        if (light_frame_ == frame) flags |= PATH_LIGHT;
        // ReSTIR GI (RENODX_TW_NO_REUSE: off); last frame's reservoirs when it ran with these texels.
        const std::array<uint32_t, 3> reuse_size = {shared.rt_bounce_width, shared.rt_bounce_height, std::max(shared.rt_bounce_scale, 1u)};
        const bool reuse = path_traced && (requested & dlaa_bridge::RT_PATH_REUSE) != 0
                           && GetEnvironmentVariableW(L"RENODX_TW_NO_REUSE", nullptr, 0) == 0 && EnsureHistory(HISTORY_REUSE, frame);
        if (reuse) {
          flags |= PATH_REUSE | (reuse_parity_ != 0 ? PATH_REUSE_PARITY : 0u);
          if ((requested & dlaa_bridge::RT_PATH_RECONSTRUCTED) != 0) flags |= PATH_PERMUTE;
          if (history_valid_ && reuse_frame_ != UINT32_MAX && reuse_frame_ + 1 == frame && reuse_size_ == reuse_size) {
            flags |= PATH_REUSE_HISTORY;
          }
        }
        // The local lights with their colours (v11): what the paths meet unseen by the screen lit by them too.
        const uint32_t light_count = (version >= 11 && (shared.rt_flags & dlaa_bridge::RT_LOCAL_LIGHTS) != 0)
                                         ? std::min(shared.rt_light_count, dlaa_bridge::RT_MAX_LIGHTS)
                                         : 0u;
        if (light_count != 0) {
          std::memcpy(slot.light_data, shared.rt_lights, light_count * sizeof(shared.rt_lights[0]));
          std::memcpy(slot.light_data + sizeof(shared.rt_lights), &light_count, sizeof(light_count));
          std::memcpy(slot.light_data + sizeof(shared.rt_lights) + 16, shared.rt_light_colours, light_count * sizeof(shared.rt_light_colours[0]));
          flags |= PATH_LOCAL_LIGHTS;
        }
        list->SetComputeRootConstantBufferView(ROOT_LIGHTS, slot.lights->GetGPUVirtualAddress());
        PathConstants constants = {
            .bounce_width = shared.rt_bounce_width,
            .bounce_height = shared.rt_bounce_height,
            .bounce_scale = std::max(shared.rt_bounce_scale, 1u),
            .flags = flags,
            .bounces = std::clamp(shared.rt_path_bounces, 0u, 8u),
            .samples = std::clamp(shared.rt_path_samples, 1u, 16u),
            .bounce_range = bounce_range,
            .reflection_range = std::clamp(shared.rt_reflection_range, 0.5f, 200.f),
        };
        std::copy_n(shared.rt_view_projection, 16, constants.view_projection.begin());
        std::copy_n(previous_view_projection_.begin(), 16, constants.previous_view_projection.begin());
        std::memcpy(slot.paths_data, &constants, sizeof(constants));
        list->SetComputeRootConstantBufferView(ROOT_PATHS, slot.paths->GetGPUVirtualAddress());
        // The radiance cache once a frame: filled (from the prepared light, else last frame's colour), resolved.
        if (cache_frame_ != frame) {
          const bool from_light = (flags & PATH_LIGHT) != 0;
          const uint32_t fill_width = (from_light ? shared.rt_bounce_width : width) / 2 + 1;
          const uint32_t fill_height = (from_light ? shared.rt_bounce_height : height) / 2 + 1;
          Mark(list, L"radiance cache");
          list->SetPipelineState(cache_fill_pipeline_.Get());
          list->Dispatch((fill_width + 7) / 8, (fill_height + 7) / 8, 1);
          list->ResourceBarrier(1, &uav_barrier);
          list->SetPipelineState(cache_resolve_pipeline_.Get());
          list->Dispatch(CACHE_ENTRIES / 64, 1, 1);
          list->ResourceBarrier(1, &uav_barrier);
          cache_frame_ = frame;
          cache_filled_ = true;
        }
        Mark(list, L"path tracing");
        if (path_traced) {
          list->SetPipelineState(path_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        }
        if (reuse) {
          list->ResourceBarrier(1, &uav_barrier);
          Mark(list, L"reuse");
          list->SetPipelineState(reuse_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
          reuse_frame_ = frame;
          reuse_parity_ ^= 1u;
          reuse_size_ = reuse_size;
        }
        if (reflections) {
          Mark(list, L"reflections");
          list->SetPipelineState(reflect_pipeline_.Get());
          list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
          list->ResourceBarrier(1, &uav_barrier);
          list->SetPipelineState(reflect_filter_pipeline_.Get());
          list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
          specular_frame_ = frame;
          specular_history_valid_ = true;
        }
        const auto from_previous = Transition(previous_positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        list->ResourceBarrier(1, &from_previous);
      }
      barriers.clear();
      for (const auto& [resource, used] : textures) barriers.push_back(Transition(resource, used, D3D12_RESOURCE_STATE_COMMON));
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      Stamp(list, slot_index * 6 + 5);
      Resolve(list, slot_index);
      recorded = SUCCEEDED(list->Close());
    }
    queue_->Wait(fence_.Get(), shared.value);
    if (recorded) {
      ID3D12CommandList* lists[] = {slot.list.Get()};
      queue_->ExecuteCommandLists(1, lists);
    }
    queue_->Signal(fence_.Get(), shared.value + 1);
    queue_->Signal(own_fence_.Get(), ++own_value_);
    slot.value = own_value_;
    return recorded;
  }

  ~RtScene12() {
    if (own_fence_) WaitFor(own_value_);
    if (event_ != nullptr) CloseHandle(event_);
    if (micromap_baking_.valid()) micromap_baking_.wait();
#if defined(RENODX_OMM_SDK)
    if (omm_.baker != nullptr) omm_.destroy_baker(omm_.baker);
    if (omm_.library != nullptr) FreeLibrary(omm_.library);
#endif
  }

 private:
  static constexpr UINT AGILITY_SDK_VERSION = 619;
  static constexpr GUID SDK_CONFIGURATION = {0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}};
  static constexpr GUID DRED_CONFIGURATION = {0x4a75bbc4, 0x9ff4, 0x4ad8, {0x9f, 0x18, 0xab, 0xae, 0x84, 0xdc, 0x5f, 0xf2}};
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
  static constexpr UINT MAX_INSTANCES = 65536;
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
  static constexpr uint32_t CLUSTER_ROOM = 32768;  // Mega Geometry's room for a frame's clusters at first (~200 MB)
  // Compact draws (v16): their cluster sets' pool (bytes, in 16-byte units; past it a word the compact pass may read),
  // a slot's upload buffer for its draws' table (three uint4 each: rt_compact.cs_6_5.hlsl), then its new sets' data.
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
  static constexpr uint64_t COMPACT_SETS_OFFSET = uint64_t{dlaa_bridge::RT_COMPACT_DRAWS} * 48;
  static constexpr uint64_t COMPACT_TEMPLATE_ARGS_OFFSET = COMPACT_SETS_OFFSET + dlaa_bridge::RT_CLUSTER_DATA_BYTES;
  static constexpr uint64_t COMPACT_TEMPLATE_DESTINATIONS_OFFSET = COMPACT_TEMPLATE_ARGS_OFFSET + uint64_t{TEMPLATE_BUILDS} * 72;
  static constexpr uint64_t COMPACT_TEMPLATE_COUNT_OFFSET = COMPACT_TEMPLATE_DESTINATIONS_OFFSET + uint64_t{TEMPLATE_BUILDS} * 8;
  static constexpr uint64_t COMPACT_UPLOAD_BYTES = COMPACT_TEMPLATE_COUNT_OFFSET + 16;
  // A slot's upload buffer for the captures' clusters: the CLASes' arguments (64 bytes each, copied into cluster_args_
  // after the compact draws' built from triangles), the cluster BLAS's (16), the CLAS counts (built from triangles, from
  // templates), the BLASes' destinations (the units', the smoke's) and the smoke's BLAS's (16), the indices (0-255), the
  // clusters' records (by geometry index).
  static constexpr uint64_t CLUSTER_ARGS_OFFSET = 0;
  static constexpr uint64_t CLUSTER_BLAS_ARGS_OFFSET = uint64_t{MAX_CLUSTERS} * 64;
  static constexpr uint64_t CLUSTER_COUNT_OFFSET = CLUSTER_BLAS_ARGS_OFFSET + 16;
  static constexpr uint64_t CLUSTER_DESTINATION_OFFSET = CLUSTER_COUNT_OFFSET + 8;
  static constexpr uint64_t CLUSTER_SMOKE_ARGS_OFFSET = CLUSTER_DESTINATION_OFFSET + 24;
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
  static constexpr UINT PLACE_TABLE = 1u << 18;
  static constexpr UINT CLUSTER_RECORDS = STATIC_RECORDS + SLOTS * dlaa_bridge::RT_CAPTURES;
  static constexpr UINT RECORD_COUNT = CLUSTER_RECORDS + SLOTS * MAX_CLUSTERS;
  static constexpr UINT STATIC_TRIANGLES = 8u << 20;
  static constexpr UINT NORMAL_POOL = STATIC_TRIANGLES + SLOTS * DYNAMIC_TRIANGLES;
  static constexpr UINT STATIC_UVS = 8u << 20;
  static constexpr UINT DYNAMIC_UVS = dlaa_bridge::RT_DYNAMIC_UVS;  // a slot's at most (grass, swaying leaves: dynamic_uvs_)
  static constexpr uint32_t RECORD_MASKED = 1;  // rt_scene.hlsli
  static constexpr uint32_t RECORD_THIN = 2;
  static constexpr uint32_t RECORD_SMOKE = 4;
  // TLAS instance masks (rt_scene.hlsli): the smoke in an instance of its own (the frame's last dynamic captures, a BLAS
  // apart), which only the rays toward the sun meet.
  static constexpr UINT ALL_INSTANCES = 0xFF;
  static constexpr UINT SMOKE_INSTANCE = 0x02;
  static constexpr float LEAF_ALPHA_CUTOFF = 0.5f;  // rt_scene_data.cs_6_5.hlsl
  static constexpr uint32_t PLACE_FOLIAGE = 1;  // rt_place.cs_6_5.hlsl
  static constexpr uint32_t PLACE_HIDDEN = 2;
  // Opacity micromaps (NVIDIA's OMM SDK): a leaf mesh's data at most (the SDK makes micromaps coarser to fit), its
  // triangles at most, the bakes on their way at most.
  static constexpr uint32_t MICROMAP_MESH_BYTES = 8u << 20;
  static constexpr uint32_t MICROMAP_TRIANGLES = 1u << 18;
  static constexpr size_t MICROMAP_BAKES = 64;
  static constexpr size_t SET_MICROMAP_READS = 16;  // grass sets' micromap reads a request at most
  static constexpr uint64_t MICROMAP_WORKLOAD = 1ull << 28;  // a bake's texel tests at most (coarser micromaps past it)
  // The scene data pass's garbage count (rt_scene_data.cs_6_5.hlsl): three counts, the example's capture, its vertices.
  static constexpr UINT LEFT_OUT_BYTES = 64;
  // A request slower than this on the GPU is reported (ms), at most this many times.
  static constexpr double SLOW_MS = 100.0;
  static constexpr uint32_t SLOW_REPORTS = 50;
  // The effects' history (EnsureHistory): its kinds, released after this many frames unused (~10 s).
  enum History : size_t { HISTORY_PATHS, HISTORY_REUSE, HISTORY_SPECULAR, HISTORY_AMBIENT, HISTORY_LIGHTS, HISTORY_KINDS };
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
    DESCRIPTOR_COUNT = LIGHT_HISTORY + 2,
  };
  static constexpr UINT SRV_COUNT = VISIBILITY;
  // rt_path.hlsli's path flags and cache size.
  static constexpr uint32_t PATH_TRACING = 1;
  static constexpr uint32_t PATH_REFLECTIONS = 2;
  static constexpr uint32_t PATH_RAW = 512;
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
  static constexpr uint32_t PATH_LOCAL_LIGHTS = 8192;
  static constexpr uint32_t PATH_AMBIENT_HISTORY = 16384;  // rt_camera.hlsli
  static constexpr uint32_t PATH_AMBIENT_PARITY = 32768;
  static constexpr uint32_t PATH_LIGHT_HISTORY = 131072;
  static constexpr uint32_t PATH_PERMUTE = 65536;
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
  };
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
  };
  // b4: RT_MAX_LIGHTS of world position and reach, then their count (a CBV's 256-byte multiple).
  static constexpr UINT LIGHT_CONSTANT_BYTES = 1280;  // positions, count, colours (v11)

  struct Slot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList4> list;
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
    // Instanced (RT_INSTANCED): each instance's transform from the first one's space (its BLAS's), read back from its
    // capture; their places (instance indices, the partitioned TLAS's) from first_instance on.
    std::vector<std::array<float, 12>> transforms;
    uint32_t first_instance = 0;
    // Placed (v14, RT_INSTANCED): in the scene only where its draws put its instances (rt_place), its build rows kept by
    // record.
    bool placed = false;
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
    ComPtr<ID3D12Resource> positions;  // its vertices (16 bytes each), for the rebuild
    ComPtr<ID3D12Resource> uvs;        // read back: a packed half UV a corner
    ComPtr<ID3D12Resource> mask;       // read back: its atlas slice
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
    ommBaker baker = nullptr;
    decltype(&ommDestroyBaker) destroy_baker = nullptr;
    decltype(&ommCpuCreateTexture) create_texture = nullptr;
    decltype(&ommCpuDestroyTexture) destroy_texture = nullptr;
    decltype(&ommCpuBake) bake = nullptr;
    decltype(&ommCpuGetBakeResultDesc) result_desc = nullptr;
    decltype(&ommCpuDestroyBakeResult) destroy_result = nullptr;
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
  };

  bool Fail(const char* status) {
    failed_ = true;
    status_ = status;
    return false;
  }

  void WaitFor(uint64_t value) {
    if (own_fence_->GetCompletedValue() >= value) return;
    own_fence_->SetEventOnCompletion(value, event_);
    WaitForSingleObject(event_, 5000);
  }

  D3D12_CPU_DESCRIPTOR_HANDLE Descriptor(UINT index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptors_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptor_size_;
    return handle;
  }

  static constexpr uint64_t Align(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

  ComPtr<ID3D12Resource> Buffer(uint64_t bytes, D3D12_HEAP_TYPE heap_type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) const {
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
    device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(buffer.GetAddressOf()));
    return buffer;
  }

#if defined(RENODX_NVAPI_CLUSTERS)
  // A buffer for Mega Geometry's operations (written by them: unordered access).
  ComPtr<ID3D12Resource> ClusterBuffer(uint64_t bytes, D3D12_RESOURCE_STATES state) const {
    return Buffer(std::max<uint64_t>(bytes, 256), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, state);
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

  // Room for `room` clusters a frame (a quarter of them built from triangles, the rest instantiated from templates): the
  // CLASes (the instantiated first), the cluster BLASes (the units', the smoke's), the scratch, the CLASes' addresses and arguments (32 and 64 bytes
  // likewise); those they replace released once the GPU is past this request. False: no memory for them (the old kept).
  bool SizeClusters(uint32_t room) {
    const uint32_t built = room / 4;
    auto clas_inputs = clas_inputs_;
    clas_inputs.maxArgCount = built;
    auto instantiate_inputs = instantiate_inputs_;
    instantiate_inputs.maxArgCount = room - built;
    auto blas_inputs = cluster_blas_inputs_;
    blas_inputs.clasDesc = {.maxTotalClasCount = room, .maxClasCountPerArg = room};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO clas_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO blas_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO templates_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO instantiate_info = {};
    if (!ClusterRequirements(clas_inputs, &clas_info) || !ClusterRequirements(blas_inputs, &blas_info)
        || !ClusterRequirements(template_inputs_, &templates_info) || !ClusterRequirements(instantiate_inputs, &instantiate_info)) {
      return false;
    }
    const uint64_t instantiated_bytes = Align(instantiate_info.resultDataMaxSizeInBytes, NVAPI_D3D12_RAYTRACING_CLAS_BYTE_ALIGNMENT);
    const uint64_t scratch_bytes = std::max({clas_info.scratchDataSizeInBytes, blas_info.scratchDataSizeInBytes,
                                             templates_info.scratchDataSizeInBytes, instantiate_info.scratchDataSizeInBytes});
    ComPtr<ID3D12Resource> clas = ClusterBuffer(instantiated_bytes + clas_info.resultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    ComPtr<ID3D12Resource> blas = ClusterBuffer(blas_info.resultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    ComPtr<ID3D12Resource> smoke_blas = ClusterBuffer(blas_info.resultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    ComPtr<ID3D12Resource> scratch = ClusterBuffer(scratch_bytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> addresses = ClusterBuffer(uint64_t{room} * 8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> args = ClusterBuffer((uint64_t{room - built} * 32) + (uint64_t{built} * 64), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!clas || !blas || !smoke_blas || !scratch || !addresses || !args) return false;
    for (ComPtr<ID3D12Resource>* old : {std::addressof(clas_), std::addressof(cluster_blas_), std::addressof(smoke_cluster_blas_),
                                        std::addressof(cluster_scratch_), std::addressof(clas_addresses_), std::addressof(cluster_args_)}) {
      if (*old) {
        releases_.push_back({.value = own_value_ + 1, .resource = std::move(*old)});
      }
    }
    clas_ = std::move(clas);
    cluster_blas_ = std::move(blas);
    smoke_cluster_blas_ = std::move(smoke_blas);
    cluster_scratch_ = std::move(scratch);
    clas_addresses_ = std::move(addresses);
    cluster_args_ = std::move(args);
    clas_inputs_ = clas_inputs;
    instantiate_inputs_ = instantiate_inputs;
    cluster_blas_inputs_ = blas_inputs;
    instantiated_bytes_ = instantiated_bytes;
    cluster_room_ = room;
    built_limit_ = built;
    cluster_bytes_ = instantiated_bytes + clas_info.resultDataMaxSizeInBytes + (blas_info.resultDataMaxSizeInBytes * 2) + scratch_bytes
                     + (uint64_t{room} * 8) + (uint64_t{room - built} * 32) + (uint64_t{built} * 64);
    return true;
  }
#endif

  // The paths' textures not shared (yet): null views, so their descriptors are always valid (the cache's too until
  // EnsureCache makes it).
  void WriteNullPathDescriptors() {
    const D3D12_SHADER_RESOURCE_VIEW_DESC texture_view = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                          .Texture2D = {.MipLevels = 1}};
    for (const auto descriptor : {PREVIOUS_COLOR, PREVIOUS_POSITIONS, GUIDE_NORMALS, GUIDE_ALBEDO, GUIDE_ROUGHNESS}) {
      device_->CreateShaderResourceView(nullptr, &texture_view, Descriptor(descriptor));
    }
    const D3D12_UNORDERED_ACCESS_VIEW_DESC texture_written = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    for (UINT set = 0; set < 2; ++set) {
      device_->CreateUnorderedAccessView(nullptr, nullptr, &texture_written, Descriptor(REUSE_SAMPLES + set));
      device_->CreateUnorderedAccessView(nullptr, nullptr, &texture_written, Descriptor(REUSE_DATA + set));
    }
    for (const UINT descriptor : std::initializer_list<UINT>{SPECULAR_ACCUMULATED, SPECULAR_HISTORY, SPECULAR_META, SPECULAR_META_HISTORY,
                                                             REFLECTION, SPECULAR_OCCLUSION, LIGHT_MASK, FOG, AMBIENT_HISTORY,
                                                             AMBIENT_HISTORY + 1, AMBIENT_POSITIONS, HIT_DISTANCE, LIGHT_HISTORY,
                                                             LIGHT_HISTORY + 1}) {
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
        && pool(DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 16, &light_kept_, LIGHT_KEPT)) {
      return true;
    }
    cache_keys_.Reset();
    WriteNullPathDescriptors();
    return false;
  }

  // A texture of the helper's own at the shared textures' size, and a UAV of one at `descriptor`.
  bool Texture(DXGI_FORMAT format, ComPtr<ID3D12Resource>* resource, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS) const {
    const D3D12_RESOURCE_DESC positions = positions_->GetDesc();
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = positions.Width, .Height = positions.Height,
                                      .DepthOrArraySize = 1, .MipLevels = 1, .Format = format, .SampleDesc = {.Count = 1},
                                      .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
    return SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(resource->GetAddressOf())));
  }
  void Written(ID3D12Resource* resource, DXGI_FORMAT format, UINT descriptor) const {
    const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    device_->CreateUnorderedAccessView(resource, nullptr, &view, Descriptor(descriptor));
  }

  // An effect's history (textures of its own at the textures' size, ~1.3 GB at 4K for all four), made when it is first
  // used and kept marked by `frame`; false: it could not be made.
  bool EnsureHistory(History kind, uint32_t frame) {
    history_used_[kind] = frame;
    switch (kind) {
      case HISTORY_PATHS:
        // Last frame's positions and this frame's (copied for the next).
        if (previous_positions_) return true;
        if (!Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &previous_positions_, D3D12_RESOURCE_STATE_COMMON)
            || !Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &positions_copy_, D3D12_RESOURCE_STATE_COMMON)) {
          previous_positions_.Reset();
          positions_copy_.Reset();
          return false;
        }
        {
          const D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                        .Texture2D = {.MipLevels = 1}};
          device_->CreateShaderResourceView(previous_positions_.Get(), &view, Descriptor(PREVIOUS_POSITIONS));
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
        // The local lights' bits: two sets in turn (the ambient's parity, its positions).
        if (light_history_[1]) return true;
        if (!Texture(DXGI_FORMAT_R32_UINT, &light_history_[0]) || !Texture(DXGI_FORMAT_R32_UINT, &light_history_[1])) {
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
      slow_report_ += std::format("{}[TW ray tracing] The {} history released (unused for {} frames).", (slow_report_.empty() ? "" : "\n"), effect,
                                  HISTORY_FRAMES);
    };
    const auto unused = [&](History kind) { return history_used_[kind] + HISTORY_FRAMES < frame; };
    if (previous_positions_ && unused(HISTORY_PATHS)) {
      const D3D12_SHADER_RESOURCE_VIEW_DESC null_view = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                         .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                         .Texture2D = {.MipLevels = 1}};
      device_->CreateShaderResourceView(nullptr, &null_view, Descriptor(PREVIOUS_POSITIONS));
      release("paths' positions", {std::addressof(previous_positions_), std::addressof(positions_copy_)}, {});
    }
    if (reuse_samples_[0] && unused(HISTORY_REUSE)) {
      release("ReSTIR GI", {std::addressof(reuse_samples_[0]), std::addressof(reuse_samples_[1]), std::addressof(reuse_data_[0]), std::addressof(reuse_data_[1])},
              {REUSE_SAMPLES, REUSE_SAMPLES + 1, REUSE_DATA, REUSE_DATA + 1});
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

  // A pass's name among the list's DRED breadcrumbs (LostReport).
  static void Mark(ID3D12GraphicsCommandList* list, const wchar_t* pass) {
    list->SetMarker(0, pass, static_cast<UINT>((std::wcslen(pass) + 1) * sizeof(wchar_t)));
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
    Stamp(list, slot_index * 6);
    const uint32_t frame = shared.rt_frame;
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    ID3D12DescriptorHeap* heaps[] = {descriptors_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root_signature_.Get());
    list->SetComputeRootDescriptorTable(ROOT_TABLE, descriptors_->GetGPUDescriptorHandleForHeapStart());
    // Unused root descriptors still point at a buffer.
    list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
    for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
      list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
    }
    // Instanced meshes this slot captured SLOTS requests back: each instance's transform from the first one's space
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
          if (!(std::abs(determinant) > 1e-12f)) continue;
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
    // Compactions: the BLASes this slot built SLOTS requests back (done) copied into places their compacted size (the
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
    // The last request's clusters had no room: twice the room for this one (no memory for it: the room stays).
    if (clusters_ && clusters_short_ && cluster_room_ < cluster_limit_) {
      if (SizeClusters(std::min(cluster_limit_, cluster_room_ * 2))) {
        slow_report_ += std::format("{}[TW ray tracing] Mega Geometry: room for {} clusters a frame now ({} MB).", (slow_report_.empty() ? "" : "\n"),
                                    cluster_room_, (cluster_bytes_ + TEMPLATE_POOL_BYTES) >> 20);
      } else {
        cluster_limit_ = cluster_room_;
      }
    }
    clusters_short_ = false;
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
    // In two runs: the draws instantiated from their set's templates (a set's made at its first draw while there is room;
    // the pool full, the templates of the sets this request does not use are given back), then those built from triangles
    // (no templates yet; with the captures' clusters at most built_limit_). The table in that order: the compact pass finds
    // a cluster's draw by its first cluster.
    uint32_t templated_clusters = 0;  // the first run's
    uint32_t template_builds = 0;     // this request's new templates
    bool templates_dropped = false;
    // Grass micromaps (with the Leaf Micromaps setting and Mega Geometry's micromap support): an alpha-tested set's first
    // instance's UVs read back once, a few sets a request (the bakes' room shared with the leaves').
    std::vector<MicromapRead> set_micromap_reads;
    size_t set_micromap_room = (cluster_micromaps_ && bake_micromaps_ && dxr12_ && (shared.rt_flags & dlaa_bridge::RT_MICROMAPS) != 0)
                                   ? std::min<size_t>(SET_MICROMAP_READS, MICROMAP_BAKES - std::min(MICROMAP_BAKES, micromap_reads_.size() + micromap_queue_.size()
                                                                                                      + (micromap_baking_.valid() ? 1u : 0u) + micromaps_baked_.size()))
                                   : 0u;
#if defined(RENODX_NVAPI_CLUSTERS)
    auto* template_args =
        reinterpret_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS*>(slot->compact_data + COMPACT_TEMPLATE_ARGS_OFFSET);
    auto* template_destinations = reinterpret_cast<uint64_t*>(slot->compact_data + COMPACT_TEMPLATE_DESTINATIONS_OFFSET);
#endif
    for (const bool templated_run : {true, false}) {
      for (uint32_t index = 0; index < compact_count; ++index) {
        const dlaa_bridge::RtCompactDraw& draw = shared.rt_compact[index];
        const auto found = sets_.find(draw.set);
        if (found == sets_.end() || draw.instances == 0) continue;
        ClusterSet& set = found->second;
        const bool masked = (draw.flags & dlaa_bridge::RT_FOLIAGE) != 0 && atlas_;
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
        const bool cluster_room = run_clusters + clusters <= (templated_run ? cluster_room_ - built_limit_ : built_limit_);
        clusters_short_ = clusters_short_ || !cluster_room;
        if (block_end > TRANSFER_VERTICES || !cluster_room || dynamic_triangles_used + triangles > DYNAMIC_TRIANGLES || (masked && dynamic_uvs_used + (triangles * 3) > dynamic_uvs_)
            || cluster_vertices + (uint64_t{set.vertices} * draw.instances) > TRANSFER_VERTICES) {
          continue;
        }
        cluster_vertices += uint64_t{set.vertices} * draw.instances;
        const std::array<uint32_t, 12> entry = {
            compact_clusters, draw.first_vertex, set.offset * 16, set.clusters,
            set.vertices, set.triangles, STATIC_TRIANGLES + (static_cast<uint32_t>(slot_index) * DYNAMIC_TRIANGLES) + dynamic_triangles_used,
            masked ? STATIC_UVS + (static_cast<uint32_t>(slot_index) * dynamic_uvs_) + dynamic_uvs_used : UINT32_MAX,
            draw.slice | (RecordFlags({.flags = draw.flags}, masked, version) << 16), capture_count + compact_drawn, draw.flags,
            templated_run ? set.template_first : 0u,
        };
        std::memcpy(slot->compact_data + (uint64_t{compact_drawn} * 48), entry.data(), sizeof(entry));
        if (masked && set_micromap_room != 0 && !set.micromaps_tried && set.triangles <= MICROMAP_TRIANGLES) {
          set.micromaps_tried = true;
          --set_micromap_room;
          set_micromap_reads.push_back({.key = draw.set, .build = set.generation, .triangles = set.triangles, .uv_offset = entry[7],
                                        .slice = draw.slice, .set = true});
        }
        compact_keys.push_back(draw.set);
        compact_flags.push_back(draw.flags);
        ++compact_drawn;
        compact_clusters += static_cast<uint32_t>(clusters);
        dynamic_triangles_used += static_cast<uint32_t>(triangles);
        if (masked) dynamic_uvs_used += static_cast<uint32_t>(triangles * 3);
        compact_masked = compact_masked || masked;
        dynamic_triangles += triangles;
        if (masked) grass_triangles += triangles;
        if ((draw.flags & dlaa_bridge::RT_TERRAIN) != 0) terrain_triangles += triangles;
        vertex_end = std::max(vertex_end, static_cast<uint32_t>(block_end));
      }
      if (templated_run) templated_clusters = compact_clusters;
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
          if (dynamic_triangles_used + triangles > DYNAMIC_TRIANGLES || cluster_vertices + (triangles * 3) > TRANSFER_VERTICES) continue;
          cluster_vertices += triangles * 3;
          placement.record = STATIC_RECORDS + static_cast<uint32_t>(slot_index) * dlaa_bridge::RT_CAPTURES + dynamic_records++;
          placement.normal_offset = STATIC_TRIANGLES + static_cast<uint32_t>(slot_index) * DYNAMIC_TRIANGLES + dynamic_triangles_used;
          placement.masked = placement.masked && dynamic_uvs_used + triangles * 3 <= dynamic_uvs_;
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
      slot->capture_keys.resize(capture_count);
      slot->capture_flags.resize(capture_count);
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        const auto& placement = placements[index];
        const uint32_t slice = (version >= 6) ? shared.rt_capture_slices[index] : 0u;
        const std::array<uint32_t, 8> entry = {capture.first_vertex, capture.vertex_count / 3 * 3, placement.normal_offset, placement.uv_offset,
                                               placement.record, slice | (RecordFlags(capture, placement.masked, version) << 16),
                                               scene_triangles, capture.flags};
        std::memcpy(slot->table_data + index * 8, entry.data(), sizeof(entry));
        if (placement.record != UINT32_MAX) scene_triangles += capture.vertex_count / 3;
        slot->capture_keys[index] = capture.key;
        slot->capture_flags[index] = capture.flags;
      }
      slot->capture_keys.insert(slot->capture_keys.end(), compact_keys.begin(), compact_keys.end());
      slot->capture_flags.insert(slot->capture_flags.end(), compact_flags.begin(), compact_flags.end());
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
        const std::array<uint32_t, 5> templates_dispatch = {
            static_cast<uint32_t>(templates_address), static_cast<uint32_t>(templates_address >> 32), template_bytes_, templated_clusters,
            (cluster_room_ - built_limit_) * 32,
        };
        list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(templates_dispatch.size()), templates_dispatch.data(), 0);
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
      list->CopyBufferRegion(slot->left_out_readback.Get(), 0, slot->left_out.Get(), 0, LEFT_OUT_BYTES);
      const auto left_out_back = Transition(slot->left_out.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &left_out_back);
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }

      // Opacity micromaps (DXR 1.2) baked for leaves built SLOTS requests back or more (their first compaction and rows
      // read): their arrays built now from the bakes' upload buffers (none when every triangle has a special index), their
      // BLASes again below.
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
          if (own_value_ + 1 < baked->value + SLOTS) continue;
          const uint64_t descs_offset = Align(baked->data.size(), 256);
          const uint64_t indices_offset = Align(descs_offset + (baked->descs.size() * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)), 256);
          auto inputs = Buffer(indices_offset + (baked->indices.size() * 4), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                               D3D12_RESOURCE_STATE_GENERIC_READ);
          uint8_t* mapped = nullptr;
          if (!inputs || FAILED(inputs->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) continue;
          std::memcpy(mapped, baked->data.data(), baked->data.size());
          std::memcpy(mapped + descs_offset, baked->descs.data(), baked->descs.size() * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC));
          std::memcpy(mapped + indices_offset, baked->indices.data(), baked->indices.size() * 4);
          inputs->Unmap(0, nullptr);
          ComPtr<ID3D12Resource> array;
          if (!baked->descs.empty()) {
            D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC& array_desc = array_descs[array_builds.size()];
            array_desc = {
                .NumOmmHistogramEntries = static_cast<UINT>(baked->histogram.size()),
                .pOmmHistogram = baked->histogram.data(),
                .InputBuffer = inputs->GetGPUVirtualAddress(),
                .PerOmmDescs = {.StartAddress = inputs->GetGPUVirtualAddress() + descs_offset,
                                .StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)},
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
      // Each unit draw's first vertex, triangles, normals, record flags, UVs and atlas slice (Mega Geometry's clusters).
      std::vector<std::array<uint32_t, 6>> unit_sources;
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
                                                                                 + (micromap_baking_.valid() ? 1u : 0u) + micromaps_baked_.size())
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
                                  placement.uv_offset, (version >= 6) ? shared.rt_capture_slices[index] : 0u});
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
                .uv_offset = placement.uv_offset, .terrain = (capture.flags & dlaa_bridge::RT_TERRAIN) != 0, .foliage = placement.masked};
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
                                    .uv_offset = placement.uv_offset, .slice = shared.rt_capture_slices[index]});
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
            .VertexBuffer = {.StartAddress = made.bake->positions->GetGPUVirtualAddress(), .StrideInBytes = 16},
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
      // The smoke (the addon's last capture: rt_smoke's quads) in BLASes of its own, its instance met only by the rays
      // toward the sun: the ambient's, the local lights' and the paths' rays went through its particles, an alpha test each
      // and their traversal (at 4K through 1000 of them 30-40 ms a frame each). Smoke before other dynamic captures (not
      // their tail) stays in the units'.
      size_t unit_count = unit_sources.size();
      while (unit_count != 0 && (unit_sources[unit_count - 1][3] & RECORD_SMOKE) != 0) {
        --unit_count;
      }
      const size_t smoke_count = unit_sources.size() - unit_count;
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
      // The smoke: built anew each request (its particles change).
      if (smoke_count != 0 && !clusters_) {
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
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
      if (!ensure_scratch(scratch_needed)) return Fail("Ray tracing: the BLAS scratch allocation failed");
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
        uint32_t units_end = 0;  // the clusters before the smoke's
        for (size_t source = 0; source < unit_sources.size(); ++source) {
          if (source == unit_count) {
            units_end = cluster_count;
          }
          const auto& [first_vertex, unit_triangles, normal_offset, record_flags, uv_offset, slice] = unit_sources[source];
          const bool masked = (record_flags & RECORD_MASKED) != 0;
          for (uint32_t first = 0; first < unit_triangles && cluster_count - templated_clusters < built_limit_; first += CLUSTER_TRIANGLES) {
            const uint32_t count = std::min(CLUSTER_TRIANGLES, unit_triangles - first);
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
        if (smoke_count == 0) {
          units_end = cluster_count;
        }
        if (cluster_count - templated_clusters >= built_limit_) {
          clusters_short_ = true;
        }
        const NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS blas_args = {
            .clusterCount = units_end, .reserved = 0, .clusterVAs = clas_addresses_->GetGPUVirtualAddress()};
        const NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS smoke_args = {
            .clusterCount = cluster_count - units_end, .reserved = 0, .clusterVAs = clas_addresses_->GetGPUVirtualAddress() + (uint64_t{units_end} * 8)};
        const std::array<uint64_t, 2> destinations = {cluster_blas_->GetGPUVirtualAddress(), smoke_cluster_blas_->GetGPUVirtualAddress()};
        const std::array<uint32_t, 2> clas_counts = {cluster_count - templated_clusters, templated_clusters};
        std::memcpy(data + CLUSTER_BLAS_ARGS_OFFSET, &blas_args, sizeof(blas_args));
        std::memcpy(data + CLUSTER_SMOKE_ARGS_OFFSET, &smoke_args, sizeof(smoke_args));
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
        list->CopyBufferRegion(cluster_destination_.Get(), 0, slot->clusters.Get(), CLUSTER_DESTINATION_OFFSET, 16);
        if (!cluster_indices_written_) list->CopyBufferRegion(cluster_indices_.Get(), 0, slot->clusters.Get(), CLUSTER_INDICES_OFFSET, 256);
        for (auto& barrier : to_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        if (!cluster_indices_written_) {
          to_copy.push_back(Transition(cluster_indices_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          cluster_indices_written_ = true;
        }
        list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
        // The instantiated CLASes into the first batch (their addresses first), those built from triangles after.
        const uint64_t upload = slot->clusters->GetGPUVirtualAddress();
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC instantiate_desc = {
            .inputs = instantiate_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = clas_->GetGPUVirtualAddress(),
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = clas_addresses_->GetGPUVirtualAddress(), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = cluster_args_->GetGPUVirtualAddress(),
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS)},
            .indirectArgCount = upload + CLUSTER_COUNT_OFFSET + 4,
        };
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC clas_desc = {
            .inputs = clas_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = clas_->GetGPUVirtualAddress() + instantiated_bytes_,
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = clas_addresses_->GetGPUVirtualAddress() + (uint64_t{templated_clusters} * 8), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = cluster_args_->GetGPUVirtualAddress() + (uint64_t{cluster_room_ - built_limit_} * 32),
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS)},
            .indirectArgCount = upload + CLUSTER_COUNT_OFFSET,
        };
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC blas_desc = {
            .inputs = cluster_blas_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = 0,
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = cluster_destination_->GetGPUVirtualAddress(), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = upload + CLUSTER_BLAS_ARGS_OFFSET,
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS)},
            .indirectArgCount = 0,
        };
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS instantiate_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &instantiate_desc};
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS clas_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &clas_desc};
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS blas_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &blas_desc};
        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC smoke_desc = blas_desc;
        smoke_desc.destinationAddressArray.StartAddress += 8;
        smoke_desc.indirectArgArray.StartAddress = upload + CLUSTER_SMOKE_ARGS_OFFSET;
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS smoke_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &smoke_desc};
        // Their build timed apart (a trace's slot leaves the paths' two stamps free).
        Mark(list, L"clusters");
        Stamp(list, slot_index * 6 + 4);
        units_clustered = true;
        if (templated_clusters != 0) {
          units_clustered = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &instantiate_params) == NVAPI_OK;
          list->ResourceBarrier(1, &uav_barrier);
        }
        if (cluster_count != templated_clusters) {
          units_clustered = units_clustered && NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &clas_params) == NVAPI_OK;
          list->ResourceBarrier(1, &uav_barrier);
        }
        const bool clas_built = units_clustered;
        units_clustered = clas_built && units_end != 0 && NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &blas_params) == NVAPI_OK;
        // The smoke's after it: the same scratch.
        list->ResourceBarrier(1, &uav_barrier);
        smoke_clustered = clas_built && cluster_count != units_end
                          && NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &smoke_params) == NVAPI_OK;
        list->ResourceBarrier(1, &uav_barrier);
        Stamp(list, slot_index * 6 + 5);
        slot_clustered_[slot_index] = true;
        clusters_built = cluster_count;
        templated_built = templated_clusters;
        units_record = first_cluster_record;
      }
#endif
      list->ResourceBarrier(1, &uav_barrier);
      units_record_ = units_record;
      units_masked_ = units_masked || compact_masked;
      // The smoke's records follow the units' captures' (its clusters' geometry indices are the frame's: the same base).
      smoke_record = (smoke_clustered ? first_cluster_record : units_record + static_cast<uint32_t>(unit_count));
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
        std::map<uint32_t, ComPtr<ID3D12Resource>> masks;
        std::vector<D3D12_RESOURCE_BARRIER> positions_copied;
        const D3D12_RESOURCE_DESC atlas_desc = micromap_reads.empty() ? D3D12_RESOURCE_DESC{} : atlas_->GetDesc();
        for (const MicromapRead& read : micromap_reads) {
          auto bake = std::make_unique<MicromapBake>();
          *bake = {.key = read.key, .build = read.build, .value = own_value_ + 1, .triangles = read.triangles, .set = read.set};
          if (!read.set) {
            bake->positions = Buffer(uint64_t{read.triangles} * 48, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
          }
          bake->uvs = Buffer(uint64_t{read.triangles} * 12, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
          ComPtr<ID3D12Resource>& mask = masks[read.slice];
          if (!mask) {
            mask = Buffer(uint64_t{dlaa_bridge::RT_ATLAS_SIZE} * dlaa_bridge::RT_ATLAS_SIZE, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_COPY_DEST);
            if (mask) {
              const D3D12_TEXTURE_COPY_LOCATION destination = {
                  .pResource = mask.Get(),
                  .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                  .PlacedFootprint = {.Offset = 0,
                                      .Footprint = {.Format = atlas_desc.Format, .Width = dlaa_bridge::RT_ATLAS_SIZE, .Height = dlaa_bridge::RT_ATLAS_SIZE,
                                                    .Depth = 1, .RowPitch = dlaa_bridge::RT_ATLAS_SIZE}},
              };
              const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = atlas_.Get(),
                                                          .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                          .SubresourceIndex = read.slice * atlas_desc.MipLevels};
              list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
          }
          bake->mask = mask;
          if ((!read.set && !bake->positions) || !bake->uvs || !bake->mask) continue;
          if (!read.set) {
            list->CopyBufferRegion(bake->positions.Get(), 0, vertex_buffer_.Get(), uint64_t{read.first_vertex} * 16, uint64_t{read.triangles} * 48);
            positions_copied.push_back(Transition(bake->positions.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          }
          list->CopyBufferRegion(bake->uvs.Get(), 0, uvs_.Get(), uint64_t{read.uv_offset} * 4, uint64_t{read.triangles} * 12);
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
        AddInstance(slot, &instance_count, mesh.blas.address, mesh.record, mesh.foliage, nullptr, mesh.record, ALL_INSTANCES);
      } else {
        for (uint32_t instance = 0; instance < mesh.transforms.size() && instance_count < MAX_INSTANCES; ++instance) {
          AddInstance(slot, &instance_count, mesh.blas.address, mesh.record, mesh.foliage, &mesh.transforms[instance], mesh.first_instance + instance,
                      ALL_INSTANCES);
        }
      }
      triangles += mesh.triangles;
    }
    // The pools' chunks logged when they change, against what separate buffers took.
    const auto held = [](const std::vector<BlasChunk>& chunks) { return std::ranges::count_if(chunks, [](const BlasChunk& chunk) { return chunk.buffer != nullptr; }); };
    if (const auto chunks = static_cast<size_t>(held(blas_pools_[BUILD_POOL]) + held(blas_pools_[KEPT_POOL])); chunks != blas_chunks_logged_) {
      blas_chunks_logged_ = chunks;
      slow_report_ += std::format("{}[TW ray tracing] Static BLASes: {} meshes, {} MB in {} pool chunks of {} MB (as buffers of their own {} MB).",
                                  (slow_report_.empty() ? "" : "\n"), meshes_.size(), bytes >> 20, chunks, BLAS_CHUNK_BYTES >> 20,
                                  separate_bytes >> 20);
    }
    if ((units_built || units_clustered) && instance_count < MAX_INSTANCES) {
      AddInstance(slot, &instance_count, (units_clustered ? cluster_blas_ : units_blas_)->GetGPUVirtualAddress(), units_record_, units_masked_, nullptr,
                  MAX_INSTANCES, ALL_INSTANCES);
      triangles += dynamic_triangles;
    }
    if ((smoke_built || smoke_clustered) && instance_count < MAX_INSTANCES) {
      AddInstance(slot, &instance_count, (smoke_clustered ? smoke_cluster_blas_ : smoke_blas_)->GetGPUVirtualAddress(), smoke_record, true, nullptr,
                  MAX_INSTANCES, SMOKE_INSTANCE);
    }
    // Placed meshes (v14; not with the partitioned TLAS): rt_place writes their descs after the CPU's instances, this
    // request's draws' (the new captures', then the placements') and those kept from the last dispatch; the TLAS takes
    // MAX_INSTANCES descs (past the written ones inactive).
    D3D12_GPU_VIRTUAL_ADDRESS instance_descs = slot->instances->GetGPUVirtualAddress();
    UINT tlas_instances = instance_count;
    uint32_t placed_drawn = 0;
    if (ptlas_ == nullptr && std::ranges::any_of(meshes_, [](const auto& entry) { return entry.second.placed; })) {
      // The records' BLAS addresses (bit 0: drawn moving now), then the jobs.
      auto* record_blas = reinterpret_cast<uint64_t*>(slot->jobs_data);
      std::fill_n(record_blas, STATIC_RECORDS, uint64_t{0});
      for (const auto& [key, mesh] : meshes_) {
        if (mesh.placed) record_blas[mesh.record] = mesh.blas.address;
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
        if ((draw.flags & dlaa_bridge::RT_PLACE_MOVING) != 0) record_blas[mesh.record] |= 1u;
        const std::array<uint32_t, 8> job = {placed_drawn, draw.instances, draw.first_vertex,
                                             (mesh.foliage ? PLACE_FOLIAGE : 0u) | ((draw.flags & dlaa_bridge::RT_PLACE_HIDDEN) != 0 ? PLACE_HIDDEN : 0u),
                                             mesh.record, 0u, 0u, 0u};
        std::memcpy(slot->jobs_data + PLACE_TABLE_BYTES + uint64_t{job_count} * 32, job.data(), sizeof(job));
        ++job_count;
        placed_drawn += draw.instances;
        triangles += uint64_t{mesh.triangles} * draw.instances;
      };
      for (const auto& built_rows : built_placed) place(built_rows);
      for (uint32_t index = 0; index < placement_count; ++index) place(shared.rt_placements[index]);
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
      // Clear, drawn, kept (rt_place's modes).
      for (const uint32_t mode : {0u, 1u, 2u}) {
        const uint32_t threads = (mode == 1u) ? placed_drawn : MAX_INSTANCES;
        if (threads == 0 || (mode == 2u && (shared.rt_flags & dlaa_bridge::RT_NO_KEPT) != 0)) continue;
        const std::array<uint32_t, 8> place_dispatch = {mode, job_count, placed_drawn, instance_count, frame, MAX_INSTANCES, RECENT_FRAMES,
                                                        STATIC_RECORDS};
        list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(place_dispatch.size()), place_dispatch.data(), 0);
        list->Dispatch((threads + 63) / 64, 1, 1);
        list->ResourceBarrier(1, &uav_barrier);
      }
      const auto to_build = Transition(written, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      list->ResourceBarrier(1, &to_build);
      // Unused root descriptors still point at a buffer.
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }
      instance_descs = written->GetGPUVirtualAddress();
      tlas_instances = MAX_INSTANCES;
      placed_last_ = 1 - placed_last_;
    }
    if (vertex_end != 0) {
      const auto vertices_written = Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &vertices_written);
    }
    uint32_t ptlas_changes = 0;
    Stamp(list, slot_index * 6 + 1);
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
        if (NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect(list, &params) != NVAPI_OK) return Fail("Ray tracing: the partitioned TLAS failed");
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
    Stamp(list, slot_index * 6 + 2);
    // The shared textures: COMMON between the APIs, read or written here.
    std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> textures = {
        {positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {sun_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {visibility_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
        {ambient_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
    };
    if (atlas_) textures.emplace_back(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // The local lights (v9): their positions and reaches to b4, their lit bits out.
    const uint32_t light_count = (version >= 9) ? std::min(shared.rt_light_count, dlaa_bridge::RT_MAX_LIGHTS) : 0u;
    const bool local_lights = (shared.rt_flags & dlaa_bridge::RT_LOCAL_LIGHTS) != 0 && light_mask_ && light_count != 0;
    // The fog shadows (v11): a quarter of the render pixels each way.
    const bool fog = (shared.rt_flags & dlaa_bridge::RT_FOG_SHADOWS) != 0 && fog_;
    if (fog) textures.emplace_back(fog_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // Their places in the last trace's list (the addon orders them by importance each frame; the same position and
    // reach: the same light), a byte each after the colours (255: new).
    std::array<uint8_t, dlaa_bridge::RT_MAX_LIGHTS> light_previous = {};
    if (local_lights) {
      textures.emplace_back(light_mask_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      std::memcpy(slot->light_data, shared.rt_lights, light_count * sizeof(shared.rt_lights[0]));
      std::memcpy(slot->light_data + sizeof(shared.rt_lights), &light_count, sizeof(light_count));
      for (uint32_t light = 0; light < light_count; ++light) {
        const auto found = std::ranges::find_if(previous_lights_.begin(), previous_lights_.begin() + previous_light_count_,
                                                [&](const std::array<float, 4>& other) {
                                                  return std::equal(other.begin(), other.end(), shared.rt_lights[light]);
                                                });
        light_previous[light] = (found != previous_lights_.begin() + previous_light_count_)
                                    ? static_cast<uint8_t>(found - previous_lights_.begin())
                                    : uint8_t{255};
      }
      std::memcpy(slot->light_data + sizeof(shared.rt_lights) + 16 + sizeof(shared.rt_light_colours), light_previous.data(),
                  light_previous.size());
    }
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {uav_barrier};
    for (const auto& [resource, used] : textures) {
      barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootShaderResourceView(ROOT_SCENE, (ptlas_ != nullptr ? ptlas_ : tlas_.Get())->GetGPUVirtualAddress());
    const float softness = (version >= 6) ? std::clamp(shared.rt_sun_softness, 0.f, 5.f) : 0.f;
    const bool soft = (shared.rt_flags & dlaa_bridge::RT_SUN_SHADOWS) != 0 && softness > 0.01f;
    // Under Ray Reconstruction no soft-shadow filter (rt_trace jitters its sun ray). The ambient keeps its history: the
    // harness's RR test (HARNESS_RR) left 19-23% of the raw 4-ray AO's noise; with this history and the AO's spread rays
    // (rt_trace) a third of what RR alone left.
    const bool reconstructed = (shared.rt_flags & dlaa_bridge::RT_RECONSTRUCTED) != 0;
    // v18: rays a pixel toward the sun and each light, the lights' radius for soft shadows (rt_lights: under RR).
    const std::array<uint32_t, 12> settings = {
        frame,
        (shared.rt_flags & ~dlaa_bridge::RT_LOCAL_LIGHTS) | (local_lights ? dlaa_bridge::RT_LOCAL_LIGHTS : 0u),
        std::bit_cast<uint32_t>(std::clamp(shared.rt_ambient_range, 0.1f, 100.f)),
        std::clamp(shared.rt_ambient_rays, 1u, 16u),
        shared.rt_width,
        shared.rt_height,
        std::bit_cast<uint32_t>(soft ? std::tan(softness * 3.14159265f / 180.f) : 0.f),
        (version >= 18) ? std::clamp(shared.rt_shadow_rays, 1u, 4u) : 1u,
        std::bit_cast<uint32_t>((version >= 18) ? std::clamp(shared.rt_light_size, 0.f, 2.f) : 0.f),
    };
    list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(settings.size()), settings.data(), 0);
    list->SetComputeRootConstantBufferView(ROOT_LIGHTS, slot->lights->GetGPUVirtualAddress());
    // The trace's history: the ambient visibility and the local lights' bits kept over frames, valid when the last trace
    // was the frame before (its positions and camera kept for the projection). Its constants bound every trace.
    const bool lights_kept = local_lights && version >= 11 && EnsureHistory(HISTORY_LIGHTS, frame);
    const bool ambient_kept = version >= 11 && ((shared.rt_flags & dlaa_bridge::RT_AMBIENT_OCCLUSION) != 0 || lights_kept)
                              && EnsureHistory(HISTORY_AMBIENT, frame);
    {
      const bool last_frame = ambient_kept && ambient_frame_ != UINT32_MAX && ambient_frame_ + 1 == frame;
      PathConstants constants = {.flags = (last_frame ? PATH_AMBIENT_HISTORY : 0u)
                                          | (lights_kept && last_frame && lights_frame_ + 1 == frame ? PATH_LIGHT_HISTORY : 0u)
                                          | (ambient_parity_ != 0 ? PATH_AMBIENT_PARITY : 0u)};
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
    list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    // The local lights' shadows (rt_lights: their own registers; before this trace's positions become the history).
    if (local_lights) {
      Mark(list, L"local lights");
      list->SetPipelineState(lights_pipeline_.Get());
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
    // This trace's lights for the next one's places.
    if (lights_kept) {
      for (uint32_t light = 0; light < light_count; ++light) std::copy_n(shared.rt_lights[light], 4, previous_lights_[light].begin());
      previous_light_count_ = light_count;
      lights_frame_ = frame;
    }
    if (fog) {
      Mark(list, L"fog");
      list->SetPipelineState(fog_pipeline_.Get());
      list->Dispatch((shared.rt_width + 31) / 32, (shared.rt_height + 31) / 32, 1);
    }
    if (soft && !reconstructed) {
      // Soft shadows: the rays' penumbras.
      list->ResourceBarrier(1, &uav_barrier);
      Mark(list, L"soft");
      list->SetPipelineState(soft_pipeline_.Get());
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    }
    barriers.clear();
    for (const auto& [resource, used] : textures) {
      barriers.push_back(Transition(resource, used, D3D12_RESOURCE_STATE_COMMON));
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    Stamp(list, slot_index * 6 + 3);
    Resolve(list, slot_index);
    if (FAILED(list->Close())) return Fail("Ray tracing: D3D12 stopped");
    // Meshes unseen for long give their BLAS (and micromaps, record, pool ranges) back once the GPU is past this request.
    std::erase_if(meshes_, [&](auto& entry) {
      if (entry.second.drawn_frame + RELEASE_FRAMES >= frame) return false;
      ReleaseMesh(&entry.second);
      return true;
    });
    status_ = std::format("Ray tracing ({}): every frame {:.2f} M triangles (terrain {:.2f}, grass and leaves {:.2f}; {}) | {} meshes ({} new, {} "
                          "micromaps, {} MB), {} + {} placed instances{}, {:.1f} M triangles",
                          dxr12_ ? "DXR 1.2" : "DXR 1.1", static_cast<double>((units_built || units_clustered) ? dynamic_triangles : 0u) / 1.0e6,
                          static_cast<double>((units_built || units_clustered) ? terrain_triangles : 0u) / 1.0e6,
                          static_cast<double>((units_built || units_clustered) ? grass_triangles : 0u) / 1.0e6,
                          units_clustered ? std::format("{} clusters ({} from templates), {} compact draws of {} sets", clusters_built, templated_built,
                                                        compact_drawn, sets_.size())
                                          : (units_built ? (units_refitted ? "refitted" : "rebuilt") : "none"),
                          meshes_.size(), built, micromaps_built, bytes >> 20, instance_count, placed_drawn,
                          ptlas_ != nullptr ? std::format(" (partitioned TLAS: {} changed)", ptlas_changes) : std::string(),
                          static_cast<double>(triangles) / 1.0e6);
    if (timestamps_) {
      gpu_timings_ = std::format("GPU ms: scene {:.2f} (clusters {:.2f}), TLAS {:.2f}, trace {:.2f}, bounce {:.2f}, reflections {:.2f}", gpu_ms_[0],
                             gpu_ms_[5], gpu_ms_[1], gpu_ms_[2], gpu_ms_[3], gpu_ms_[4]);
    }
    slot->status = status_;
    return true;
  }

  // GPU times (but with RENODX_TW_NO_GPU_TIMES): a timestamp at `index`, a slot's resolved for reading, its last read back.
  void Stamp(ID3D12GraphicsCommandList* list, size_t index) const {
    if (timestamps_) list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(index));
  }
  void Resolve(ID3D12GraphicsCommandList* list, size_t slot_index) const {
    if (timestamps_) {
      list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(slot_index * 6), 6, timestamp_readback_.Get(),
                             slot_index * 6 * sizeof(uint64_t));
    }
  }
  void ReadTimes(size_t slot_index) {
    if (!timestamps_) return;
    if (timestamp_frequency_ == 0) queue_->GetTimestampFrequency(&timestamp_frequency_);
    const D3D12_RANGE range = {.Begin = slot_index * 6 * sizeof(uint64_t), .End = (slot_index + 1) * 6 * sizeof(uint64_t)};
    uint64_t* mapped = nullptr;
    if (timestamp_frequency_ == 0 || FAILED(timestamp_readback_->Map(0, &range, reinterpret_cast<void**>(&mapped)))) return;
    const uint64_t* stamps = mapped + slot_index * 6;
    const auto ms = [&](uint64_t from, uint64_t to) { return (to > from) ? static_cast<double>(to - from) * 1000.0 / timestamp_frequency_ : 0.0; };
    if (slot_traced_[slot_index]) {
      gpu_ms_[0] = ms(stamps[0], stamps[1]);
      gpu_ms_[1] = ms(stamps[1], stamps[2]);
      gpu_ms_[2] = ms(stamps[2], stamps[3]);
      gpu_ms_[5] = slot_clustered_[slot_index] ? ms(stamps[4], stamps[5]) : 0.0;
      // The Light Bounce's and reflections' times only while they run (not their last).
      for (size_t kind = 0; kind < paths_age_.size(); ++kind) {
        if (++paths_age_[kind] > SLOTS * 2) gpu_ms_[3 + kind] = 0.0;
      }
      if (gpu_ms_[0] + gpu_ms_[1] + gpu_ms_[2] > SLOW_MS && slow_reports_ < SLOW_REPORTS) {
        slow_report_ = std::format("[TW ray tracing] A slow request, GPU ms: scene {:.1f}, TLAS {:.1f}, trace {:.1f}; {}", gpu_ms_[0], gpu_ms_[1],
                                   gpu_ms_[2], slots_[slot_index].status);
        ++slow_reports_;
      }
    } else {
      const double paths_ms = ms(stamps[4], stamps[5]);
      gpu_ms_[slot_reflected_[slot_index] ? 4 : 3] = paths_ms;
      paths_age_[slot_reflected_[slot_index] ? 1 : 0] = 0;
      if (paths_ms > SLOW_MS && slow_reports_ < SLOW_REPORTS) {
        slow_report_ = std::format("[TW ray tracing] A slow {} request: {:.1f} GPU ms", slot_reflected_[slot_index] ? "reflections" : "Light Bounce",
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
  void AdvanceMicromaps(uint64_t completed) {
    for (std::unique_ptr<MicromapBake>& read : micromap_reads_) {
      if (read->value > completed) continue;
      const D3D12_RANGE uv_range = {.Begin = 0, .End = uint64_t{read->triangles} * 12};
      const D3D12_RANGE mask_range = {.Begin = 0, .End = uint64_t{dlaa_bridge::RT_ATLAS_SIZE} * dlaa_bridge::RT_ATLAS_SIZE};
      const D3D12_RANGE written = {};
      const uint32_t* uvs = nullptr;
      const uint8_t* mask = nullptr;
      if (SUCCEEDED(read->uvs->Map(0, &uv_range, reinterpret_cast<void**>(const_cast<uint32_t**>(&uvs))))) {
        read->uv_data.assign(uvs, uvs + (uint64_t{read->triangles} * 3));
        read->uvs->Unmap(0, &written);
      }
      if (SUCCEEDED(read->mask->Map(0, &mask_range, reinterpret_cast<void**>(const_cast<uint8_t**>(&mask))))) {
        read->mask_data.assign(mask, mask + mask_range.End);
        read->mask->Unmap(0, &written);
      }
      read->uvs.Reset();
      read->mask.Reset();
      if (!read->uv_data.empty() && !read->mask_data.empty()) micromap_queue_.push_back(std::move(read));
      read.reset();
    }
    std::erase(micromap_reads_, nullptr);
    if (micromap_baking_.valid() && micromap_baking_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      std::unique_ptr<MicromapBake> baked = micromap_baking_.get();
      ++micromap_tally_[baked->indices.empty() ? 2 : 0];
      micromap_tally_[1] += baked->data.size();
      if (!baked->indices.empty()) micromaps_baked_.push_back(std::move(baked));
    }
#if defined(RENODX_OMM_SDK)
    if (!micromap_baking_.valid() && !micromap_queue_.empty()) {
      micromap_baking_ = std::async(std::launch::async, [this, bake = std::move(micromap_queue_.front())]() mutable {
        BakeMicromaps(bake.get());
        return std::move(bake);
      });
      micromap_queue_.erase(micromap_queue_.begin());
    }
#endif
    // The bakes' tally at most every 10 s (the log).
    if ((micromap_tally_[0] + micromap_tally_[2]) != 0 && GetTickCount64() >= micromap_logged_ + 10000) {
      micromap_logged_ = GetTickCount64();
      slow_report_ += std::format("{}[TW ray tracing] Leaf micromaps: {} meshes baked ({} KB), {} failed; {} grass sets given theirs.",
                                  (slow_report_.empty() ? "" : "\n"), micromap_tally_[0], micromap_tally_[1] >> 10, micromap_tally_[2],
                                  micromap_tally_[3]);
      micromap_tally_ = {};
    }
  }

#if defined(RENODX_OMM_SDK)
  // A leaf mesh's micromaps from its UVs and mask (the bake thread): 4-state as the ray queries test them (bilinear,
  // wrapping, LEAF_ALPHA_CUTOFF), a micro-triangle about two texels across (at most level 6), the same ones shared, wholly
  // opaque or clear triangles the special indices (all of them so: no micromaps, no array); none (no indices) when the
  // SDK fails.
  void BakeMicromaps(MicromapBake* bake) const {
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
    if (omm_.create_texture(omm_.baker, &texture_desc, &texture) != ommResult_SUCCESS) return;
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
    input.maxSubdivisionLevel = 6;
    input.maxArrayDataSize = MICROMAP_MESH_BYTES;
    input.maxWorkloadSize = MICROMAP_WORKLOAD;
    ommCpuBakeResult result = nullptr;
    const ommCpuBakeResultDesc* desc = nullptr;
    if (omm_.bake(omm_.baker, &input, &result) == ommResult_SUCCESS && omm_.result_desc(result, &desc) == ommResult_SUCCESS
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
    omm_.destroy_texture(omm_.baker, texture);
  }
#endif

  // A capture's record flags (rt_scene.hlsli): alpha-tested leaves, thin cloth and sails (v11), smoke.
  static uint32_t RecordFlags(const dlaa_bridge::RtCapture& capture, bool masked, uint32_t version) {
    return (masked ? RECORD_MASKED : 0u) | ((version >= 11 && (capture.flags & dlaa_bridge::RT_THIN) != 0) ? RECORD_THIN : 0u)
           | ((masked && (capture.flags & dlaa_bridge::RT_SMOKE) != 0) ? RECORD_SMOKE : 0u);
  }

  // A world-space BLAS into the slot's TLAS instances (leaves not opaque: their alpha is tested), its InstanceID its
  // first geometry's record; placed by `transform` (null: where it was captured); `place` its partitioned TLAS index
  // (kept in InstanceContributionToHitGroupIndex: no hit groups here).
  static void AddInstance(Slot* slot, UINT* instance_count, D3D12_GPU_VIRTUAL_ADDRESS blas, uint32_t record, bool foliage,
                          const std::array<float, 12>* transform, uint32_t place, UINT mask) {
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
  std::future<std::unique_ptr<MicromapBake>> micromap_baking_;
  std::vector<std::unique_ptr<MicromapBake>> micromaps_baked_;
  // Since the last log line: meshes baked, their data's bytes, failed, of them grass sets (their templates rebuilt).
  std::array<uint64_t, 4> micromap_tally_ = {};
  bool cluster_micromaps_ = false;  // Mega Geometry's operations allow micromaps (grass sets' templates)
  uint64_t set_generation_ = 0;
  uint64_t micromap_logged_ = 0;                 // that line's time (GetTickCount64)
#if defined(RENODX_OMM_SDK)
  OmmSdk omm_;
#endif
  // For the log: the device's start, a slow request's report (GPU over SLOW_MS; at most SLOW_REPORTS), a garbage
  // triangle's (one in 10 s).
  std::string slow_report_;
  uint32_t slow_reports_ = 0;
  std::string garbage_example_;
  uint64_t garbage_example_time_ = 0;
  std::array<ComPtr<ID3D12Resource>, 2> placed_instances_;
  uint32_t placed_last_ = 0;
  ComPtr<ID3D12Resource> placed_state_;
  ComPtr<ID3D12PipelineState> place_pipeline_;
  // The units' BLAS (refitted in place), the vertex counts of its draws at its last build, refits since.
  ComPtr<ID3D12Resource> units_blas_;
  uint64_t units_bytes_ = 0;
  // The smoke's BLAS (built each request; Mega Geometry's in smoke_cluster_blas_).
  ComPtr<ID3D12Resource> smoke_blas_;
  uint64_t smoke_bytes_ = 0;
  bool clusters_ = false;  // Mega Geometry's cluster operations for the units
  uint32_t cluster_limit_ = MAX_CLUSTERS;  // the most a frame (half on smaller GPUs)
  uint32_t cluster_room_ = MAX_CLUSTERS;   // room for them now (SizeClusters: grown while a frame has more)
  bool clusters_short_ = false;            // the last request's clusters had no room
  uint32_t built_limit_ = MAX_CLUSTERS;    // of the room built from triangles (the rest instantiated from templates)
  uint64_t cluster_bytes_ = 0;             // the room's CLASes, cluster BLAS, scratch and arguments (the status)
  uint32_t dynamic_uvs_ = DYNAMIC_UVS;     // a request's alpha-tested corners (by the GPU's memory)
  uint32_t uv_pool_ = STATIC_UVS + (SLOTS * DYNAMIC_UVS);
#if defined(RENODX_NVAPI_CLUSTERS)
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS clas_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS cluster_blas_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS template_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS instantiate_inputs_ = {};
#endif
  ComPtr<ID3D12Resource> clas_;
  ComPtr<ID3D12Resource> clas_addresses_;
  ComPtr<ID3D12Resource> cluster_blas_;
  ComPtr<ID3D12Resource> smoke_cluster_blas_;
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
  // The ambient visibility kept over frames (v11): its two sets, the last trace's positions, camera, frame and set.
  std::array<ComPtr<ID3D12Resource>, 2> ambient_history_;
  ComPtr<ID3D12Resource> ambient_positions_;
  std::array<float, 16> ambient_view_projection_ = {};
  uint32_t ambient_frame_ = UINT32_MAX;
  uint32_t ambient_parity_ = 0;
  // The local lights' bits kept over frames (HISTORY_LIGHTS), the last trace that wrote them and its lights.
  std::array<ComPtr<ID3D12Resource>, 2> light_history_;
  uint32_t lights_frame_ = UINT32_MAX;
  std::array<std::array<float, 4>, dlaa_bridge::RT_MAX_LIGHTS> previous_lights_ = {};
  uint32_t previous_light_count_ = 0;
  ComPtr<ID3D12PipelineState> fog_pipeline_;
  ComPtr<ID3D12PipelineState> lights_pipeline_;
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
  ComPtr<ID3D12Resource> previous_positions_;  // last frame's positions (t11)
  ComPtr<ID3D12Resource> positions_copy_;      // this frame's, for the next
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
  ComPtr<ID3D12PipelineState> path_pipeline_;
  ComPtr<ID3D12PipelineState> reflect_pipeline_;
  ComPtr<ID3D12PipelineState> reuse_pipeline_;  // ReSTIR GI's spatial reuse
  std::array<ComPtr<ID3D12Resource>, 2> reuse_samples_;
  std::array<ComPtr<ID3D12Resource>, 2> reuse_data_;
  uint32_t reuse_frame_ = UINT32_MAX;  // the frame the reservoirs were last made in
  uint32_t reuse_parity_ = 0;          // this frame's set
  std::array<uint32_t, 3> reuse_size_ = {};  // the bounce's size and scale they are at
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
  uint32_t copied_frame_ = UINT32_MAX;    // the frame positions_copy_ holds
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
  std::string status_ = "Ray tracing off";
  // The last trace's times: the GPU's (after the status's label), the CPU's and the submit's (at its end).
  std::string gpu_timings_;
  std::string cpu_timings_;
};

}  // namespace twattila::dlaa_helper
