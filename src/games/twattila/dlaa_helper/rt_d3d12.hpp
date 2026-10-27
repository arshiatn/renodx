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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../dlaa_bridge.h"
// NVIDIA's NVAPI (MIT; build.ps1 adds it when build\downloads\nvapi holds it): RTX Mega Geometry's cluster operations.
#if __has_include(<nvapi.h>)
#include <nvapi.h>
#define RENODX_NVAPI_CLUSTERS 1
#endif
#include "rt_bounce.h"            // dxc -Fh (build.ps1)
#include "rt_bounce_omm.h"        // dxc -Fh (build.ps1)
#include "rt_cache_fill.h"        // dxc -Fh (build.ps1)
#include "rt_cache_resolve.h"     // dxc -Fh (build.ps1)
#include "rt_fog.h"               // dxc -Fh (build.ps1)
#include "rt_fog_omm.h"           // dxc -Fh (build.ps1)
#include "rt_omm.h"               // dxc -Fh (build.ps1)
#include "rt_path.h"              // dxc -Fh (build.ps1)
#include "rt_path_lib.h"          // dxc -Fh (build.ps1)
#include "rt_path_omm.h"          // dxc -Fh (build.ps1)
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
// rt_soft.cs_6_5.hlsl). Leaves are alpha-tested against their opacity masks (the addon's atlas, v6): each geometry has a
// record (its instance's InstanceID plus its geometry index) with its UVs and normals in pools (rt_scene_data.cs_6_5.hlsl).
// With the Agility SDK next to the helper (D3D12\D3D12Core.dll) and shader model 6.9, DXR 1.2: the leaves get opacity
// micromaps (rt_omm.cs_6_5.hlsl). With a bridge v7 addon's ray-traced Light Bounce a second request each frame traces
// rt_bounce.cs_6_5.hlsl against that frame's TLAS; bridge v8 path traces it instead (rt_path.hlsli: a world radiance
// cache and the learned lighting for what the screen never saw), and a third request traces reflections and specular
// occlusion. With DXR 1.2 the paths run as ray generation shaders (DispatchRays) with shader execution reordering. With
// NVAPI's cluster operations (RTX Mega Geometry; set RENODX_TW_NO_CLUSTERS to turn them off) the units are built each
// frame as clusters (CLAS) into one cluster BLAS instead of the refitted BLAS. The partitioned TLAS (PTLAS: each frame
// only the instances that changed written; RENODX_TW_NO_PTLAS turns it off) takes every change as a whole instance
// write: NVAPI's update operation (a new BLAS for an instance) stalled the GPU 4-5 s a time after BLAS compactions (GPU
// harness, helper19-21). Two PTLASes in turn, the operations in device memory, as NVAPI asks. Its queue waits for the
// addon's fence value and signals the next.
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
    if (std::filesystem::exists(agility / L"D3D12Core.dll")
        && SUCCEEDED(D3D12GetInterface(SDK_CONFIGURATION, IID_PPV_ARGS(configuration.GetAddressOf())))
        && SUCCEEDED(configuration->CreateDeviceFactory(AGILITY_SDK_VERSION, (agility.string() + "\\").c_str(),
                                                        IID_PPV_ARGS(factory.GetAddressOf())))) {
      factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
      if (SUCCEEDED(factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf())))) sdk = "Agility SDK";
    }
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
    if (GetEnvironmentVariableW(L"RENODX_TW_GPU_TIMES", nullptr, 0) != 0) {
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
      if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                               IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close()) || !slot.instances || !slot.table || !slot.sizes || !slot.readback || !slot.rows_readback
          || FAILED(slot.instances->Map(0, nullptr, reinterpret_cast<void**>(&slot.instance_data)))
          || FAILED(slot.table->Map(0, nullptr, reinterpret_cast<void**>(&slot.table_data)))) {
        return Fail("Ray tracing: no D3D12 command lists");
      }
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
    vertex_buffer_ = Buffer(uint64_t{TRANSFER_VERTICES} * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
                            D3D12_RESOURCE_STATE_COPY_DEST);
    tlas_ = Buffer(top_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    tlas_scratch_ = Buffer(top_info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto pool = [&](uint64_t bytes) {
      return Buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    records_ = pool(uint64_t{RECORD_COUNT} * 16);
    uvs_ = pool(uint64_t{UV_POOL} * 4);
    normals_ = pool(uint64_t{NORMAL_POOL} * 4);
    if (!vertex_buffer_ || !tlas_ || !tlas_scratch_ || !records_ || !uvs_ || !normals_) return Fail("Ray tracing: D3D12 buffers failed");
#if defined(RENODX_NVAPI_CLUSTERS)
    // Mega Geometry's buffers, sized for the most clusters a frame: the CLASes (placed by the driver, their addresses in
    // clas_addresses_), the units' cluster BLAS (at cluster_blas_, its address in cluster_destination_), the scratch,
    // the sequential 8-bit indices; a slot's arguments and its clusters' records written by the CPU (cluster_data).
    if (clusters_) {
      clas_inputs_ = {
          .maxArgCount = MAX_CLUSTERS,
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS,
      };
      clas_inputs_.trianglesDesc = {
          .vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
          .maxGeometryIndexValue = MAX_CLUSTERS - 1,
          .maxUniqueGeometryCountPerArg = 1,
          .maxTriangleCountPerArg = CLUSTER_TRIANGLES,
          .maxVertexCountPerArg = CLUSTER_TRIANGLES * 3,
          .maxTotalTriangleCount = DYNAMIC_TRIANGLES,
          .maxTotalVertexCount = DYNAMIC_TRIANGLES * 3,
          .minPositionTruncateBitCount = 0,
      };
      cluster_blas_inputs_ = {
          .maxArgCount = 1,
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS,
      };
      cluster_blas_inputs_.clasDesc = {.maxTotalClasCount = MAX_CLUSTERS, .maxClasCountPerArg = MAX_CLUSTERS};
      NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO clas_info = {};
      NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO blas_info = {};
      const auto requirements = [&](const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& inputs,
                                    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO* info) {
        const NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS params = {
            .version = NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER,
            .pInput = &inputs,
            .pInfo = info,
        };
        return NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo(device_.Get(), &params) == NVAPI_OK;
      };
      const auto as_buffer = [&](uint64_t bytes, D3D12_RESOURCE_STATES state) {
        return Buffer(std::max<uint64_t>(bytes, 256), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, state);
      };
      clusters_ = requirements(clas_inputs_, &clas_info) && requirements(cluster_blas_inputs_, &blas_info);
      if (clusters_) {
        clas_ = as_buffer(clas_info.resultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        cluster_blas_ = as_buffer(blas_info.resultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        cluster_scratch_ = as_buffer(std::max(clas_info.scratchDataSizeInBytes, blas_info.scratchDataSizeInBytes),
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        clas_addresses_ = as_buffer(uint64_t{MAX_CLUSTERS} * 8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cluster_destination_ = as_buffer(8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cluster_indices_ = Buffer(256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        clusters_ = clas_ && cluster_blas_ && cluster_scratch_ && clas_addresses_ && cluster_destination_ && cluster_indices_;
      }
      for (Slot& slot : slots_) {
        if (!clusters_) break;
        slot.clusters = Buffer(CLUSTER_UPLOAD_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        clusters_ = slot.clusters && SUCCEEDED(slot.clusters->Map(0, nullptr, reinterpret_cast<void**>(&slot.cluster_data)));
        if (clusters_) {
          for (uint32_t index = 0; index < 256; ++index) slot.cluster_data[CLUSTER_INDICES_OFFSET + index] = static_cast<uint8_t>(index);
        }
      }
    }
    // The partitioned TLAS: an instance per static record (its index) and one for the units, PTLAS_PARTITION_SIZE a
    // partition; a slot's write and update arguments, its two operations and their count.
    NvU32 ptlas_caps = 0;
    ptlas_ = nullptr;
    if (GetEnvironmentVariableW(L"RENODX_TW_NO_PTLAS", nullptr, 0) == 0 && NvAPI_Initialize() == NVAPI_OK
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
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .Num32BitValues = 8}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 1, .Num32BitValues = 4}},
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
    // DXR 1.2: the micromaps' bake and the ray queries that use them; else (or when they fail) DXR 1.1.
    dxr12_ = dxr12_ && pipeline(RT_TRACE_OMM, sizeof(RT_TRACE_OMM), &trace_pipeline_) && pipeline(RT_OMM, sizeof(RT_OMM), &omm_pipeline_)
             && pipeline(RT_BOUNCE_OMM, sizeof(RT_BOUNCE_OMM), &bounce_pipeline_) && pipeline(RT_PATH_OMM, sizeof(RT_PATH_OMM), &path_pipeline_)
             && pipeline(RT_REFLECT_OMM, sizeof(RT_REFLECT_OMM), &reflect_pipeline_) && pipeline(RT_REUSE_OMM, sizeof(RT_REUSE_OMM), &reuse_pipeline_)
             && pipeline(RT_FOG_OMM, sizeof(RT_FOG_OMM), &fog_pipeline_);
    if (!dxr12_) {
      omm_pipeline_.Reset();
      trace_pipeline_.Reset();
      bounce_pipeline_.Reset();
      path_pipeline_.Reset();
      reflect_pipeline_.Reset();
      reuse_pipeline_.Reset();
      fog_pipeline_.Reset();
    }
    if ((!trace_pipeline_ && !pipeline(RT_TRACE, sizeof(RT_TRACE), &trace_pipeline_))
        || (!bounce_pipeline_ && !pipeline(RT_BOUNCE, sizeof(RT_BOUNCE), &bounce_pipeline_)) || !pipeline(RT_SOFT, sizeof(RT_SOFT), &soft_pipeline_)
        || !pipeline(RT_SCENE_DATA, sizeof(RT_SCENE_DATA), &scene_data_pipeline_)
        || (!path_pipeline_ && !pipeline(RT_PATH, sizeof(RT_PATH), &path_pipeline_))
        || (!reflect_pipeline_ && !pipeline(RT_REFLECT, sizeof(RT_REFLECT), &reflect_pipeline_))
        || (!reuse_pipeline_ && !pipeline(RT_REUSE, sizeof(RT_REUSE), &reuse_pipeline_))
        || (!fog_pipeline_ && !pipeline(RT_FOG, sizeof(RT_FOG), &fog_pipeline_))
        || !pipeline(RT_CACHE_FILL, sizeof(RT_CACHE_FILL), &cache_fill_pipeline_)
        || !pipeline(RT_CACHE_RESOLVE, sizeof(RT_CACHE_RESOLVE), &cache_resolve_pipeline_)
        || !pipeline(RT_REFLECT_FILTER, sizeof(RT_REFLECT_FILTER), &reflect_filter_pipeline_)
        || FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(descriptors_.GetAddressOf())))) {
      return Fail("Ray tracing: the trace shaders failed (needs shader model 6.5)");
    }
    // DXR 1.2: the paths as ray generation shaders (shader execution reordering), dispatched with DispatchRays; their
    // compute versions otherwise (or when the state object fails).
    if (dxr12_) {
      const std::array<D3D12_EXPORT_DESC, 2> exports = {{{.Name = L"PathTraceRayGen"}, {.Name = L"ReflectRayGen"}}};
      const D3D12_DXIL_LIBRARY_DESC library = {
          .DXILLibrary = {.pShaderBytecode = RT_PATH_LIB, .BytecodeLength = sizeof(RT_PATH_LIB)},
          .NumExports = static_cast<UINT>(exports.size()),
          .pExports = const_cast<D3D12_EXPORT_DESC*>(exports.data()),
      };
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
      path_shader_table_ = Buffer(2 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                                  D3D12_RESOURCE_STATE_GENERIC_READ);
      uint8_t* table = nullptr;
      const void* path_identifier = nullptr;
      const void* reflect_identifier = nullptr;
      if (FAILED(device_->CreateStateObject(&state_desc, IID_PPV_ARGS(path_state_object_.GetAddressOf())))
          || FAILED(path_state_object_.As(&properties)) || (path_identifier = properties->GetShaderIdentifier(L"PathTraceRayGen")) == nullptr
          || (reflect_identifier = properties->GetShaderIdentifier(L"ReflectRayGen")) == nullptr || !path_shader_table_
          || FAILED(path_shader_table_->Map(0, nullptr, reinterpret_cast<void**>(&table)))) {
        path_state_object_.Reset();
        path_shader_table_.Reset();
      } else {
        std::memcpy(table, path_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        std::memcpy(table + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, reflect_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        path_shader_table_->Unmap(0, nullptr);
      }
    }
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
    const auto uvs_view = buffer_view(DXGI_FORMAT_R32_UINT, UV_POOL);
    const auto normals_view = buffer_view(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
    const auto vertices_view = buffer_view(DXGI_FORMAT_R32G32B32A32_FLOAT, TRANSFER_VERTICES);
    device_->CreateShaderResourceView(records_.Get(), &records_view, Descriptor(RECORDS));
    device_->CreateShaderResourceView(uvs_.Get(), &uvs_view, Descriptor(UVS));
    device_->CreateShaderResourceView(normals_.Get(), &normals_view, Descriptor(NORMALS));
    device_->CreateShaderResourceView(vertex_buffer_.Get(), &vertices_view, Descriptor(VERTICES));
    const auto records_written = buffer_uav(DXGI_FORMAT_R32G32B32A32_UINT, RECORD_COUNT);
    const auto uvs_written = buffer_uav(DXGI_FORMAT_R32_UINT, UV_POOL);
    const auto normals_written = buffer_uav(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
    device_->CreateUnorderedAccessView(records_.Get(), nullptr, &records_written, Descriptor(RECORDS_WRITTEN));
    device_->CreateUnorderedAccessView(uvs_.Get(), nullptr, &uvs_written, Descriptor(UVS_WRITTEN));
    device_->CreateUnorderedAccessView(normals_.Get(), nullptr, &normals_written, Descriptor(NORMALS_WRITTEN));
    WriteNullPathDescriptors();
    status_ = std::format("Ray tracing ready ({}; {})", dxr12_ ? "DXR 1.2: opacity micromaps, shader execution reordering" : "DXR 1.1", sdk);
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
    // addon shares them; the history (positions, reflection accumulation) of its own at the textures' size.
    if (version >= 8) {
      const D3D12_RESOURCE_DESC positions_desc = positions_->GetDesc();
      const auto texture = [&](DXGI_FORMAT format, ComPtr<ID3D12Resource>* resource,
                               D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
        const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = positions_desc.Width,
                                          .Height = positions_desc.Height, .DepthOrArraySize = 1, .MipLevels = 1, .Format = format,
                                          .SampleDesc = {.Count = 1}, .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
        return SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                          IID_PPV_ARGS(resource->GetAddressOf())));
      };
      const auto view = [](DXGI_FORMAT format) {
        return D3D12_SHADER_RESOURCE_VIEW_DESC{.Format = format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                               .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                               .Texture2D = {.MipLevels = 1}};
      };
      const auto written = [](DXGI_FORMAT format) {
        return D3D12_UNORDERED_ACCESS_VIEW_DESC{.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
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
      if (!texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &previous_positions_, D3D12_RESOURCE_STATE_COMMON)
          || !texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &positions_copy_, D3D12_RESOURCE_STATE_COMMON)) {
        Close();
        return false;
      }
      const auto positions_view = view(DXGI_FORMAT_R32G32B32A32_FLOAT);
      device_->CreateShaderResourceView(previous_positions_.Get(), &positions_view, Descriptor(PREVIOUS_POSITIONS));
      copied_frame_ = UINT32_MAX;
      paths_frame_ = UINT32_MAX;
      // ReSTIR GI's two sets of reservoirs (the path-traced Light Bounce's).
      reuse_frame_ = UINT32_MAX;
      for (UINT set = 0; set < 2; ++set) {
        if (!texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &reuse_samples_[set]) || !texture(DXGI_FORMAT_R32G32B32A32_UINT, &reuse_data_[set])) {
          reuse_samples_ = {};
          reuse_data_ = {};
          break;
        }
        const auto samples_view = written(DXGI_FORMAT_R32G32B32A32_FLOAT);
        const auto data_view = written(DXGI_FORMAT_R32G32B32A32_UINT);
        device_->CreateUnorderedAccessView(reuse_samples_[set].Get(), nullptr, &samples_view, Descriptor(REUSE_SAMPLES + set));
        device_->CreateUnorderedAccessView(reuse_data_[set].Get(), nullptr, &data_view, Descriptor(REUSE_DATA + set));
      }
      if (shared.rt_reflection != 0 && shared.rt_specular != 0 && open(shared.rt_reflection, &reflection_) && open(shared.rt_specular, &specular_)
          && texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &specular_accumulated_) && texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &specular_history_)
          && texture(DXGI_FORMAT_R16G16_FLOAT, &specular_meta_) && texture(DXGI_FORMAT_R16G16_FLOAT, &specular_meta_history_)) {
        const auto rgba = written(DXGI_FORMAT_R16G16B16A16_FLOAT);
        const auto rg = written(DXGI_FORMAT_R16G16_FLOAT);
        const auto r8 = written(DXGI_FORMAT_R8_UNORM);
        device_->CreateUnorderedAccessView(specular_accumulated_.Get(), nullptr, &rgba, Descriptor(SPECULAR_ACCUMULATED));
        device_->CreateUnorderedAccessView(specular_history_.Get(), nullptr, &rgba, Descriptor(SPECULAR_HISTORY));
        device_->CreateUnorderedAccessView(specular_meta_.Get(), nullptr, &rg, Descriptor(SPECULAR_META));
        device_->CreateUnorderedAccessView(specular_meta_history_.Get(), nullptr, &rg, Descriptor(SPECULAR_META_HISTORY));
        device_->CreateUnorderedAccessView(reflection_.Get(), nullptr, &rgba, Descriptor(REFLECTION));
        device_->CreateUnorderedAccessView(specular_.Get(), nullptr, &r8, Descriptor(SPECULAR_OCCLUSION));
        specular_history_valid_ = false;
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
    previous_color_.Reset();
    guide_normals_.Reset();
    guide_albedo_.Reset();
    guide_roughness_.Reset();
    previous_positions_.Reset();
    positions_copy_.Reset();
    reflection_.Reset();
    specular_.Reset();
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
  [[nodiscard]] const std::string& Status() const { return status_; }
  [[nodiscard]] bool Dxr12() const { return dxr12_; }

  // One request: the frame's captures into BLASes, the TLAS of what it drew, the trace; the queue waits for the addon's
  // fence value and signals the next (also when nothing ran). False: nothing traced.
  bool Trace(const dlaa_bridge::Shared& shared, uint32_t version) {
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
    slot_traced_[slot_index] = true;
    const uint64_t completed = own_fence_->GetCompletedValue();
    std::erase_if(releases_, [completed](const Release& release) { return release.value <= completed; });
    std::erase_if(pending_frees_, [&](const PendingFree& pending) {
      if (pending.value > completed) return false;
      free_records_.push_back(pending.record);
      free_triangles_.Give(pending.normal_offset, pending.triangles);
      if (pending.uv_offset != UINT32_MAX) free_uvs_.Give(pending.uv_offset, pending.triangles * 3);
      if (pending.instances != 0) free_instances_.Give(pending.first_instance, pending.instances);
      return true;
    });
    const bool recorded = SUCCEEDED(slot.allocator->Reset()) && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr))
                          && Record(shared, version, slot_index, &slot);
    if (timestamps_) {
      LARGE_INTEGER cpu_recorded;
      LARGE_INTEGER cpu_frequency;
      QueryPerformanceCounter(&cpu_recorded);
      QueryPerformanceFrequency(&cpu_frequency);
      status_ = std::format("CPU ms: wait {:.2f}, record {:.2f} | ",
                             static_cast<double>(cpu_waited.QuadPart - cpu_start.QuadPart) * 1000.0 / cpu_frequency.QuadPart,
                             static_cast<double>(cpu_recorded.QuadPart - cpu_waited.QuadPart) * 1000.0 / cpu_frequency.QuadPart)
                + status_;
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
      status_ = std::format("submit {:.2f} | ", static_cast<double>(submit_end.QuadPart - submit_start.QuadPart) * 1000.0 / submit_frequency.QuadPart)
                + status_;
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
    const uint32_t frame = shared.rt_frame;
    const uint32_t requested = (version >= 8) ? shared.rt_path_flags : 0u;
    const bool path_traced = !reflections && (requested & dlaa_bridge::RT_PATH_TRACING) != 0;
    const bool wanted = reflections ? (reflection_ && (requested & (dlaa_bridge::RT_REFLECTIONS | dlaa_bridge::RT_SPECULAR_OCCLUSION)) != 0)
                                    : (light_ && bounce_);
    const bool uses_paths = path_traced || reflections;
    bool recorded = wanted && traced_ && (!uses_paths || EnsureCache()) && SUCCEEDED(slot.allocator->Reset())
                    && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr));
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
      const std::array<uint32_t, 8> settings = {
          frame,
          0u,
          std::bit_cast<uint32_t>(bounce_range),
          std::clamp(shared.rt_bounce_rays, 1u, 32u),
          shared.rt_width,
          shared.rt_height,
          0u,
          0u,
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
        }
      }
      std::vector<D3D12_RESOURCE_BARRIER> barriers;
      for (const auto& [resource, used] : textures) barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

      if (!uses_paths) {
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
        const bool reuse = path_traced && (requested & dlaa_bridge::RT_PATH_REUSE) != 0 && reuse_samples_[0]
                           && GetEnvironmentVariableW(L"RENODX_TW_NO_REUSE", nullptr, 0) == 0;
        if (reuse) {
          flags |= PATH_REUSE | (reuse_parity_ != 0 ? PATH_REUSE_PARITY : 0u);
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
          list->SetPipelineState(cache_fill_pipeline_.Get());
          list->Dispatch((fill_width + 7) / 8, (fill_height + 7) / 8, 1);
          list->ResourceBarrier(1, &uav_barrier);
          list->SetPipelineState(cache_resolve_pipeline_.Get());
          list->Dispatch(CACHE_ENTRIES / 64, 1, 1);
          list->ResourceBarrier(1, &uav_barrier);
          cache_frame_ = frame;
          cache_filled_ = true;
        }
        // The paths: ray generation shaders with shader execution reordering (DXR 1.2), else compute.
        const auto dispatch = [&](ID3D12PipelineState* compute, uint64_t record, uint32_t across, uint32_t down) {
          if (path_state_object_) {
            list->SetPipelineState1(path_state_object_.Get());
            const D3D12_DISPATCH_RAYS_DESC rays = {
                .RayGenerationShaderRecord = {.StartAddress = path_shader_table_->GetGPUVirtualAddress() + record * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT,
                                              .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
                .Width = across,
                .Height = down,
                .Depth = 1,
            };
            list->DispatchRays(&rays);
          } else {
            list->SetPipelineState(compute);
            list->Dispatch((across + 7) / 8, (down + 7) / 8, 1);
          }
        };
        if (path_traced) dispatch(path_pipeline_.Get(), 0, shared.rt_bounce_width, shared.rt_bounce_height);
        if (reuse) {
          list->ResourceBarrier(1, &uav_barrier);
          list->SetPipelineState(reuse_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
          reuse_frame_ = frame;
          reuse_parity_ ^= 1u;
          reuse_size_ = reuse_size;
        }
        if (reflections) {
          dispatch(reflect_pipeline_.Get(), 1, width, height);
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
  }

 private:
  static constexpr UINT AGILITY_SDK_VERSION = 619;
  static constexpr GUID SDK_CONFIGURATION = {0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}};
  static constexpr size_t SLOTS = 3;
  static constexpr UINT MAX_INSTANCES = 65536;
  static constexpr UINT TRANSFER_VERTICES = dlaa_bridge::RT_TRANSFER_WIDTH * dlaa_bridge::RT_TRANSFER_HEIGHT;
  // Meshes drawn within this many frames cast (but the terrain only in frames that draw it); unseen longer: released.
  static constexpr uint32_t RECENT_FRAMES = 300;
  static constexpr uint32_t RELEASE_FRAMES = 3600;
  // The units' BLAS is refitted to each frame's poses while their draws stay the same, rebuilt this often.
  static constexpr uint32_t UNITS_REBUILD_FRAMES = 30;
  static constexpr UINT DYNAMIC_TRIANGLES = (3u << 20) / 3;  // rt_scene.hpp MAX_DYNAMIC_VERTICES
  // Mega Geometry: the units' triangles in clusters of this many (three vertices each, up to 256 a cluster), a frame's
  // clusters at most (the last of each draw partial), each a geometry with a record of its own (a slot's in turn).
  static constexpr uint32_t CLUSTER_TRIANGLES = 85;
  static constexpr uint32_t MAX_CLUSTERS = 16384;
  // A slot's upload buffer for them: the CLASes' arguments (64 bytes each), the cluster BLAS's (16), the CLAS count,
  // the BLAS's destination, the indices (0-255), the clusters' records.
  static constexpr uint64_t CLUSTER_ARGS_OFFSET = 0;
  static constexpr uint64_t CLUSTER_BLAS_ARGS_OFFSET = uint64_t{MAX_CLUSTERS} * 64;
  static constexpr uint64_t CLUSTER_COUNT_OFFSET = CLUSTER_BLAS_ARGS_OFFSET + 16;
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
  static constexpr UINT CLUSTER_RECORDS = STATIC_RECORDS + SLOTS * dlaa_bridge::RT_CAPTURES;
  static constexpr UINT RECORD_COUNT = CLUSTER_RECORDS + SLOTS * MAX_CLUSTERS;
  static constexpr UINT STATIC_TRIANGLES = 8u << 20;
  static constexpr UINT NORMAL_POOL = STATIC_TRIANGLES + SLOTS * DYNAMIC_TRIANGLES;
  static constexpr UINT STATIC_UVS = 8u << 20;
  static constexpr UINT UV_POOL = STATIC_UVS;
  static constexpr uint32_t RECORD_MASKED = 1;  // rt_scene.hlsli
  static constexpr uint32_t RECORD_THIN = 2;
  // Opacity micromaps: a mesh's at most (its subdivision level lowered to fit), all new meshes' a request.
  static constexpr uint64_t MICROMAP_MESH_BYTES = 8u << 20;
  static constexpr uint64_t MICROMAP_FRAME_BYTES = 64u << 20;
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
    DESCRIPTOR_COUNT,
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
    uint8_t* light_data = nullptr;
    ComPtr<ID3D12Resource> clusters;  // Mega Geometry's arguments and records (CLUSTER_*_OFFSET)
    uint8_t* cluster_data = nullptr;
    ComPtr<ID3D12Resource> ptlas;  // the partitioned TLAS's operations (PTLAS_*_OFFSET)
    ComPtr<ID3D12Resource> ptlas_device;  // and their copy the build reads (device memory)
    uint8_t* ptlas_data = nullptr;
    uint64_t value = 0;  // own_fence_'s value after its last use
  };
  struct Mesh {
    ComPtr<ID3D12Resource> blas;
    ComPtr<ID3D12Resource> micromaps;  // its opacity micromap array (DXR 1.2), kept while the BLAS lives
    ComPtr<ID3D12Resource> micromap_indices;
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
  };
  struct Release {
    uint64_t value = 0;
    ComPtr<ID3D12Resource> resource;
  };
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
    for (const auto descriptor :
         {SPECULAR_ACCUMULATED, SPECULAR_HISTORY, SPECULAR_META, SPECULAR_META_HISTORY, REFLECTION, SPECULAR_OCCLUSION, LIGHT_MASK, FOG}) {
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

  static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    return {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {.pResource = resource, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, .StateBefore = before, .StateAfter = after},
    };
  }

  // The request's commands into the slot's list (closed); false: nothing usable recorded.
  bool Record(const dlaa_bridge::Shared& shared, uint32_t version, size_t slot_index, Slot* slot) {
    auto* list = slot->list.Get();
    Stamp(list, slot_index * 6);
    const uint32_t frame = shared.rt_frame;
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    const auto align = [](uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; };
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
    // Compactions: the BLASes this slot built SLOTS requests back (done) copied into buffers their compacted size.
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
          auto compacted = Buffer(sizes[index], D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          if (!compacted) break;
          list->CopyRaytracingAccelerationStructure(compacted->GetGPUVirtualAddress(), mesh->second.blas->GetGPUVirtualAddress(),
                                                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
          releases_.push_back({.value = own_value_ + 1, .resource = std::move(mesh->second.blas)});
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
    bool units_built = false;
    bool units_refitted = false;
    bool units_clustered = false;  // Mega Geometry's cluster BLAS built for them
    uint32_t clusters_built = 0;
    const uint32_t capture_count = std::min(shared.rt_capture_count, dlaa_bridge::RT_CAPTURES);
    if (capture_count != 0) {
      uint32_t vertex_end = 0;
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        const uint32_t rows = (version >= 10 && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0) ? capture.instances * 4u : 0u;
        vertex_end = std::max(vertex_end, capture.first_vertex + capture.vertex_count + rows);
      }
      vertex_end = std::min(vertex_end, TRANSFER_VERTICES);
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
      const auto to_copy = Transition(vertices_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->ResourceBarrier(1, &to_copy);
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
      std::vector<D3D12_RESOURCE_BARRIER> to_scene_data = {
          Transition(vertices_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
          Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(records_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(uvs_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(normals_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
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
      uint32_t dynamic_triangles_used = 0;
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        auto& placement = placements[index];
        const uint32_t triangles = capture.vertex_count / 3;
        if (triangles == 0 || capture.first_vertex + capture.vertex_count > TRANSFER_VERTICES) continue;
        placement.masked = (capture.flags & dlaa_bridge::RT_FOLIAGE) != 0 && version >= 6 && atlas_;
        if ((capture.flags & dlaa_bridge::RT_DYNAMIC) != 0) {
          if (dynamic_triangles_used + triangles > DYNAMIC_TRIANGLES) continue;
          placement.record = STATIC_RECORDS + static_cast<uint32_t>(slot_index) * dlaa_bridge::RT_CAPTURES + dynamic_records++;
          placement.normal_offset = STATIC_TRIANGLES + static_cast<uint32_t>(slot_index) * DYNAMIC_TRIANGLES + dynamic_triangles_used;
          placement.masked = false;
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
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        const auto& placement = placements[index];
        const uint32_t slice = (version >= 6) ? shared.rt_capture_slices[index] : 0u;
        const std::array<uint32_t, 8> entry = {capture.first_vertex, capture.vertex_count / 3 * 3, placement.normal_offset, placement.uv_offset,
                                               placement.record, slice | (RecordFlags(capture, placement.masked, version) << 16), 0, 0};
        std::memcpy(slot->table_data + index * 8, entry.data(), sizeof(entry));
      }
      const std::array<uint32_t, 4> scene_dispatch = {capture_count, vertex_end / 3, 0, 0};
      list->SetPipelineState(scene_data_pipeline_.Get());
      list->SetComputeRoot32BitConstants(ROOT_DISPATCH, 4, scene_dispatch.data(), 0);
      list->Dispatch((vertex_end / 3 + 63) / 64, 1, 1);
      const std::array<D3D12_RESOURCE_BARRIER, 3> scene_written = {
          Transition(records_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(uvs_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(normals_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
      };
      list->ResourceBarrier(static_cast<UINT>(scene_written.size()), scene_written.data());

      // Opacity micromaps (DXR 1.2) for the new leaves: their level (4 - 2) fitting MICROMAP_MESH_BYTES, all within
      // MICROMAP_FRAME_BYTES; baked, then built into micromap arrays together.
      struct Bake {
        uint32_t index = 0;
        uint32_t level = 0;
        uint64_t data_offset = 0;
        uint64_t desc_offset = 0;
        ComPtr<ID3D12Resource> indices;
        ComPtr<ID3D12Resource> array;
      };
      std::vector<Bake> bakes;
      if (dxr12_) {
        uint64_t data_bytes = 0;
        uint64_t desc_bytes = 0;
        for (uint32_t index = 0; index < capture_count; ++index) {
          if (!placements[index].masked || placements[index].record == UINT32_MAX) continue;
          const uint64_t triangles = shared.rt_captures[index].vertex_count / 3;
          const auto bytes_at = [&](uint32_t at) { return triangles * std::max<uint64_t>((1ull << (2 * at)) / 4, 4); };
          uint32_t level = 4;
          while (level > 2 && bytes_at(level) > MICROMAP_MESH_BYTES) --level;
          if (bytes_at(level) > MICROMAP_MESH_BYTES || data_bytes + bytes_at(level) > MICROMAP_FRAME_BYTES) continue;
          bakes.push_back({.index = index, .level = level, .data_offset = data_bytes, .desc_offset = desc_bytes});
          data_bytes = align(data_bytes + bytes_at(level), 256);
          desc_bytes = align(desc_bytes + triangles * 8, 256);
        }
        const auto grow = [&](ComPtr<ID3D12Resource>* buffer, uint64_t bytes) {
          if (*buffer && (*buffer)->GetDesc().Width >= bytes) return true;
          if (*buffer) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*buffer)});
          *buffer = Buffer(std::max<uint64_t>(bytes, 1 << 20), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          return static_cast<bool>(*buffer);
        };
        if (!bakes.empty() && (!grow(&micromap_data_, data_bytes) || !grow(&micromap_descs_, desc_bytes))) bakes.clear();
        if (!bakes.empty()) {
          list->SetPipelineState(omm_pipeline_.Get());
          std::vector<D3D12_RESOURCE_BARRIER> baked;
          for (Bake& bake : bakes) {
            const uint32_t triangles = shared.rt_captures[bake.index].vertex_count / 3;
            bake.indices = Buffer(uint64_t{triangles} * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (!bake.indices) continue;
            const std::array<uint32_t, 4> bake_dispatch = {placements[bake.index].record, triangles, bake.level, 0};
            list->SetComputeRoot32BitConstants(ROOT_DISPATCH, 4, bake_dispatch.data(), 0);
            list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, micromap_data_->GetGPUVirtualAddress() + bake.data_offset);
            list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, bake.indices->GetGPUVirtualAddress());
            list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, micromap_descs_->GetGPUVirtualAddress() + bake.desc_offset);
            list->Dispatch((triangles + 63) / 64, 1, 1);
            baked.push_back(Transition(bake.indices.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          }
          baked.push_back(Transition(micromap_data_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          baked.push_back(Transition(micromap_descs_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          list->ResourceBarrier(static_cast<UINT>(baked.size()), baked.data());
          std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY> histograms(bakes.size());
          std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC> arrays(bakes.size());
          std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> builds;
          uint64_t scratch_needed = 0;
          for (size_t index = 0; index < bakes.size(); ++index) {
            Bake& bake = bakes[index];
            if (!bake.indices) continue;
            histograms[index] = {.Count = shared.rt_captures[bake.index].vertex_count / 3, .SubdivisionLevel = bake.level,
                                 .Format = D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE};
            arrays[index] = {
                .NumOmmHistogramEntries = 1,
                .pOmmHistogram = &histograms[index],
                .InputBuffer = micromap_data_->GetGPUVirtualAddress() + bake.data_offset,
                .PerOmmDescs = {.StartAddress = micromap_descs_->GetGPUVirtualAddress() + bake.desc_offset,
                                .StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)},
            };
            const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
                .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY,
                .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
                .NumDescs = 1,
                .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                .pOpacityMicromapArrayDesc = &arrays[index],
            };
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
            device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
            bake.array = Buffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE,
                                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            if (!bake.array) continue;
            builds.push_back({.DestAccelerationStructureData = bake.array->GetGPUVirtualAddress(), .Inputs = inputs,
                              .ScratchAccelerationStructureData = scratch_needed});
            scratch_needed = align(scratch_needed + info.ScratchDataSizeInBytes, 256);
          }
          const bool arrays_built = !builds.empty() && ensure_scratch(scratch_needed);
          if (arrays_built) {
            for (auto& build : builds) {
              build.ScratchAccelerationStructureData += scratch_->GetGPUVirtualAddress();
              list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
            }
            micromaps_built = static_cast<uint32_t>(builds.size());
            list->ResourceBarrier(1, &uav_barrier);
          }
          // Leaves without both (their buffers stay until the GPU is past this request) get none.
          for (Bake& bake : bakes) {
            if (arrays_built && bake.array && bake.indices) continue;
            for (ComPtr<ID3D12Resource>* resource : {std::addressof(bake.array), std::addressof(bake.indices)}) {
              if (*resource) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*resource)});
            }
          }
        }
      }

      // The BLASes, built together: static ones once (for tracing, then compacted), the units' one refitted to this
      // request's poses while its draws are the same (rebuilt every UNITS_REBUILD_FRAMES or when they change).
      std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> triangle_descs(capture_count);
      std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> linkages(capture_count);
      std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(capture_count);
      std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> unit_geometries;
      std::vector<uint32_t> unit_layout;
      // Each unit draw's first vertex, triangles, normals and record flags (Mega Geometry's clusters).
      std::vector<std::array<uint32_t, 4>> unit_sources;
      uint32_t units_record = UINT32_MAX;
      struct Build {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
        bool compact = false;
      };
      std::vector<Build> builds;
      uint64_t scratch_needed = 0;
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
        const auto bake = std::ranges::find(bakes, index, &Bake::index);
        const bool with_micromaps = bake != bakes.end() && bake->array && bake->indices;
        if (with_micromaps) {
          linkages[index] = {
              .OpacityMicromapIndexBuffer = {.StartAddress = bake->indices->GetGPUVirtualAddress(), .StrideInBytes = 4},
              .OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT,
              .OpacityMicromapBaseLocation = 0,
              .OpacityMicromapArray = bake->array->GetGPUVirtualAddress(),
          };
          geometries[index] = {
              .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES,
              .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE,
              .OmmTriangles = {.pTriangles = &triangle_descs[index], .pOmmLinkage = &linkages[index]},
          };
        } else {
          geometries[index] = {
              .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
              .Flags = placement.masked ? D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
              .Triangles = triangle_descs[index],
          };
        }
        if ((capture.flags & dlaa_bridge::RT_DYNAMIC) != 0) {
          if (units_record == UINT32_MAX) units_record = placement.record;
          unit_geometries.push_back(geometries[index]);
          unit_layout.push_back(capture.vertex_count / 3 * 3);
          unit_sources.push_back({capture.first_vertex, capture.vertex_count / 3, placement.normal_offset, RecordFlags(capture, false, version)});
          dynamic_triangles += capture.vertex_count / 3;
          continue;
        }
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                     | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
            .NumDescs = 1,
            .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
            .pGeometryDescs = &geometries[index],
        };
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        auto blas = Buffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        if (!blas) {
          // No room: its place goes back now (nothing references it).
          free_records_.push_back(placement.record);
          free_triangles_.Give(placement.normal_offset, capture.vertex_count / 3);
          if (placement.uv_offset != UINT32_MAX) free_uvs_.Give(placement.uv_offset, capture.vertex_count / 3 * 3);
          continue;
        }
        builds.push_back({.desc = {.DestAccelerationStructureData = blas->GetGPUVirtualAddress(), .Inputs = inputs,
                                   .ScratchAccelerationStructureData = scratch_needed},
                          .compact = true});
        scratch_needed = align(scratch_needed + info.ScratchDataSizeInBytes, 256);
        Mesh& mesh = meshes_[capture.key];
        if (mesh.blas) ReleaseMesh(&mesh);
        mesh = {.blas = std::move(blas), .build = ++builds_, .bytes = info.ResultDataMaxSizeInBytes, .drawn_frame = frame,
                .triangles = capture.vertex_count / 3, .record = placement.record, .normal_offset = placement.normal_offset,
                .uv_offset = placement.uv_offset, .terrain = (capture.flags & dlaa_bridge::RT_TERRAIN) != 0, .foliage = placement.masked};
        if (version >= 10 && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0 && capture.instances > 1 && rows_used + capture.instances <= ROWS_READBACK) {
          row_copies.push_back({.key = capture.key, .build = mesh.build, .offset = rows_used, .count = capture.instances});
          row_sources.push_back(capture.first_vertex + capture.vertex_count);
          rows_used += capture.instances;
        }
        if (with_micromaps) {
          mesh.micromaps = std::move(bake->array);
          mesh.micromap_indices = std::move(bake->indices);
        }
        slot->compactions.emplace_back(capture.key, mesh.build);
        ++built;
      }
      // The units: refitted in place while the request's unit draws are those of the last build (without Mega Geometry).
      if (!unit_geometries.empty() && !clusters_) {
        const bool refit = units_blas_ && unit_layout == units_layout_ && units_refits_ < UNITS_REBUILD_FRAMES;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
                     | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
            .NumDescs = static_cast<UINT>(unit_geometries.size()),
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
          scratch_needed = align(scratch_needed + (refit ? info.UpdateScratchDataSizeInBytes : info.ScratchDataSizeInBytes), 256);
          units_refits_ = refit ? units_refits_ + 1 : 0;
          units_layout_ = std::move(unit_layout);
          units_built = true;
          units_refitted = refit;
        }
      }
      if (!ensure_scratch(scratch_needed)) return Fail("Ray tracing: the BLAS scratch allocation failed");
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
      // Mega Geometry: the units' draws split into clusters (CLAS), built with one cluster BLAS from them; each cluster a
      // geometry whose record points at its normals.
      if (clusters_ && !unit_sources.empty()) {
        uint8_t* data = slot->cluster_data;
        auto* args = reinterpret_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS*>(data + CLUSTER_ARGS_OFFSET);
        auto* cluster_records = reinterpret_cast<std::array<uint32_t, 4>*>(data + CLUSTER_RECORDS_OFFSET);
        uint32_t cluster_count = 0;
        for (const auto& [first_vertex, unit_triangles, normal_offset, record_flags] : unit_sources) {
          for (uint32_t first = 0; first < unit_triangles && cluster_count < MAX_CLUSTERS; first += CLUSTER_TRIANGLES) {
            const uint32_t count = std::min(CLUSTER_TRIANGLES, unit_triangles - first);
            auto& arg = args[cluster_count];
            arg = {};
            arg.clusterId = cluster_count;
            arg.triangleCount = count;
            arg.vertexCount = count * 3;
            arg.indexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_8BIT;
            arg.baseGeometryIndexAndFlags = cluster_count | NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_OPAQUE;
            arg.indexBufferStride = 1;
            arg.vertexBufferStride = 16;
            arg.indexBuffer = cluster_indices_->GetGPUVirtualAddress();
            arg.vertexBuffer = vertex_buffer_->GetGPUVirtualAddress() + (uint64_t{first_vertex} + uint64_t{first} * 3) * 16;
            cluster_records[cluster_count] = {UINT32_MAX, normal_offset + first, record_flags << 16, 0u};
            ++cluster_count;
          }
        }
        const NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS blas_args = {
            .clusterCount = cluster_count, .reserved = 0, .clusterVAs = clas_addresses_->GetGPUVirtualAddress()};
        const uint64_t destination = cluster_blas_->GetGPUVirtualAddress();
        std::memcpy(data + CLUSTER_BLAS_ARGS_OFFSET, &blas_args, sizeof(blas_args));
        std::memcpy(data + CLUSTER_COUNT_OFFSET, &cluster_count, sizeof(cluster_count));
        std::memcpy(data + CLUSTER_DESTINATION_OFFSET, &destination, sizeof(destination));
        // The clusters' records, the BLAS's destination (and once the indices) copied where the builds read them.
        const uint32_t record_base = CLUSTER_RECORDS + static_cast<uint32_t>(slot_index) * MAX_CLUSTERS;
        std::vector<D3D12_RESOURCE_BARRIER> to_copy = {
            Transition(records_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(cluster_destination_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
        list->CopyBufferRegion(records_.Get(), uint64_t{record_base} * 16, slot->clusters.Get(), CLUSTER_RECORDS_OFFSET, uint64_t{cluster_count} * 16);
        list->CopyBufferRegion(cluster_destination_.Get(), 0, slot->clusters.Get(), CLUSTER_DESTINATION_OFFSET, 8);
        if (!cluster_indices_written_) list->CopyBufferRegion(cluster_indices_.Get(), 0, slot->clusters.Get(), CLUSTER_INDICES_OFFSET, 256);
        for (auto& barrier : to_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        if (!cluster_indices_written_) {
          to_copy.push_back(Transition(cluster_indices_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          cluster_indices_written_ = true;
        }
        list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
        const uint64_t upload = slot->clusters->GetGPUVirtualAddress();
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC clas_desc = {
            .inputs = clas_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = clas_->GetGPUVirtualAddress(),
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = clas_addresses_->GetGPUVirtualAddress(), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = upload + CLUSTER_ARGS_OFFSET,
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
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS clas_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &clas_desc};
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS blas_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &blas_desc};
        units_clustered = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &clas_params) == NVAPI_OK;
        list->ResourceBarrier(1, &uav_barrier);
        units_clustered = units_clustered && NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &blas_params) == NVAPI_OK;
        clusters_built = cluster_count;
        units_record = record_base;
      }
#endif
      list->ResourceBarrier(1, &uav_barrier);
      units_record_ = units_record;
      // The instanced captures' rows into the slot's readback (four texels an instance), the CPU's once it comes round.
      if (!row_copies.empty()) {
        const auto to_source = Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_source);
        for (size_t index = 0; index < row_copies.size(); ++index) {
          list->CopyBufferRegion(slot->rows_readback.Get(), uint64_t{row_copies[index].offset} * 64, vertex_buffer_.Get(),
                                 uint64_t{row_sources[index]} * 16, uint64_t{row_copies[index].count} * 64);
        }
        const auto back_to_read = Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &back_to_read);
        slot->row_reads = std::move(row_copies);
      }
      std::vector<D3D12_RESOURCE_BARRIER> after = {
          Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
          Transition(slot->sizes.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
      };
      if (atlas_) after.push_back(Transition(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
      if (!bakes.empty()) {
        after.push_back(Transition(micromap_data_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        after.push_back(Transition(micromap_descs_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
      }
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
    for (const auto& [key, mesh] : meshes_) {
      bytes += mesh.bytes;
      if (instance_count == MAX_INSTANCES) continue;
      const bool current = mesh.drawn_frame == frame;
      const bool recent = !mesh.terrain && mesh.drawn_frame + RECENT_FRAMES >= frame;
      if (!current && !recent) continue;
      if (mesh.transforms.empty()) {
        AddInstance(slot, &instance_count, mesh.blas.Get(), mesh.record, mesh.foliage, nullptr, mesh.record);
      } else {
        for (uint32_t instance = 0; instance < mesh.transforms.size() && instance_count < MAX_INSTANCES; ++instance) {
          AddInstance(slot, &instance_count, mesh.blas.Get(), mesh.record, mesh.foliage, &mesh.transforms[instance], mesh.first_instance + instance);
        }
      }
      triangles += mesh.triangles;
    }
    if ((units_built || units_clustered) && instance_count < MAX_INSTANCES) {
      AddInstance(slot, &instance_count, units_clustered ? cluster_blas_.Get() : units_blas_.Get(), units_record_, false, nullptr,
                  MAX_INSTANCES);
      triangles += dynamic_triangles;
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
                     .NumDescs = instance_count,
                     .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                     .InstanceDescs = slot->instances->GetGPUVirtualAddress()},
          .ScratchAccelerationStructureData = tlas_scratch_->GetGPUVirtualAddress(),
      };
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
    if (local_lights) {
      textures.emplace_back(light_mask_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      std::memcpy(slot->light_data, shared.rt_lights, light_count * sizeof(shared.rt_lights[0]));
      std::memcpy(slot->light_data + sizeof(shared.rt_lights), &light_count, sizeof(light_count));
    }
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {uav_barrier};
    for (const auto& [resource, used] : textures) {
      barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootShaderResourceView(ROOT_SCENE, (ptlas_ != nullptr ? ptlas_ : tlas_.Get())->GetGPUVirtualAddress());
    const float softness = (version >= 6) ? std::clamp(shared.rt_sun_softness, 0.f, 5.f) : 0.f;
    const bool soft = (shared.rt_flags & dlaa_bridge::RT_SUN_SHADOWS) != 0 && softness > 0.01f;
    const std::array<uint32_t, 8> settings = {
        frame,
        (shared.rt_flags & ~dlaa_bridge::RT_LOCAL_LIGHTS) | (local_lights ? dlaa_bridge::RT_LOCAL_LIGHTS : 0u),
        std::bit_cast<uint32_t>(std::clamp(shared.rt_ambient_range, 0.1f, 100.f)),
        std::clamp(shared.rt_ambient_rays, 1u, 16u),
        shared.rt_width,
        shared.rt_height,
        std::bit_cast<uint32_t>(soft ? std::tan(softness * 3.14159265f / 180.f) : 0.f),
        0u,
    };
    list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(settings.size()), settings.data(), 0);
    list->SetComputeRootConstantBufferView(ROOT_LIGHTS, slot->lights->GetGPUVirtualAddress());
    list->SetPipelineState(trace_pipeline_.Get());
    list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    if (fog) {
      list->SetPipelineState(fog_pipeline_.Get());
      list->Dispatch((shared.rt_width + 31) / 32, (shared.rt_height + 31) / 32, 1);
    }
    if (soft) {
      // Soft shadows: the rays' penumbras.
      list->ResourceBarrier(1, &uav_barrier);
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
    status_ = std::format("Ray tracing ({}): {} meshes ({} new, {} with micromaps, {} MB), {} in the scene{}, {:.1f} M triangles ({:.2f} M "
                          "of units, {})",
                          dxr12_ ? "DXR 1.2" : "DXR 1.1", meshes_.size(), built, micromaps_built, bytes >> 20, instance_count,
                          ptlas_ != nullptr ? std::format(" (partitioned TLAS: {} changed)", ptlas_changes) : std::string(),
                          static_cast<double>(triangles) / 1.0e6, static_cast<double>((units_built || units_clustered) ? dynamic_triangles : 0u) / 1.0e6,
                          units_clustered ? std::format("{} clusters (Mega Geometry)", clusters_built)
                                          : (units_built ? (units_refitted ? "refitted" : "rebuilt") : "none"));
    if (timestamps_) {
      status_ = std::format("GPU ms: scene {:.2f}, TLAS {:.2f}, trace {:.2f}, paths {:.2f} | ", gpu_ms_[0], gpu_ms_[1], gpu_ms_[2], gpu_ms_[3]) + status_;
    }
    return true;
  }

  // GPU times (RENODX_TW_GPU_TIMES): a timestamp at `index`, a slot's resolved for reading, its last read back.
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
    } else {
      gpu_ms_[3] = ms(stamps[4], stamps[5]);
    }
    const D3D12_RANGE written = {};
    timestamp_readback_->Unmap(0, &written);
  }

  // A mesh's BLAS and micromaps released, its record and pool ranges given back, once the GPU is past this request.
  void ReleaseMesh(Mesh* mesh) {
    for (ComPtr<ID3D12Resource>* resource : {std::addressof(mesh->blas), std::addressof(mesh->micromaps), std::addressof(mesh->micromap_indices)}) {
      if (*resource) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*resource)});
    }
    pending_frees_.push_back({.value = own_value_ + 1, .record = mesh->record, .normal_offset = mesh->normal_offset,
                              .triangles = mesh->triangles, .uv_offset = mesh->uv_offset, .first_instance = mesh->first_instance,
                              .instances = static_cast<uint32_t>(mesh->transforms.size())});
  }

  // A capture's record flags (rt_scene.hlsli): alpha-tested leaves, thin cloth and sails (v11).
  static uint32_t RecordFlags(const dlaa_bridge::RtCapture& capture, bool masked, uint32_t version) {
    return (masked ? RECORD_MASKED : 0u) | ((version >= 11 && (capture.flags & dlaa_bridge::RT_THIN) != 0) ? RECORD_THIN : 0u);
  }

  // A world-space BLAS into the slot's TLAS instances (leaves not opaque: their alpha is tested), its InstanceID its
  // first geometry's record; placed by `transform` (null: where it was captured); `place` its partitioned TLAS index
  // (kept in InstanceContributionToHitGroupIndex: no hit groups here).
  static void AddInstance(Slot* slot, UINT* instance_count, ID3D12Resource* blas, uint32_t record, bool foliage,
                          const std::array<float, 12>* transform, uint32_t place) {
    D3D12_RAYTRACING_INSTANCE_DESC& instance = slot->instance_data[(*instance_count)++];
    instance = {
        .InstanceID = record,
        .InstanceMask = 0xFF,
        .InstanceContributionToHitGroupIndex = place,
        .Flags = static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE
                                   | (foliage ? 0u : static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE))),
        .AccelerationStructure = blas->GetGPUVirtualAddress(),
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
  ComPtr<ID3D12Resource> micromap_data_;  // a request's micromap inputs (grown when needed)
  ComPtr<ID3D12Resource> micromap_descs_;
  // The units' BLAS (refitted in place), the vertex counts of its draws at its last build, refits since.
  ComPtr<ID3D12Resource> units_blas_;
  uint64_t units_bytes_ = 0;
  bool clusters_ = false;  // Mega Geometry's cluster operations for the units
#if defined(RENODX_NVAPI_CLUSTERS)
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS clas_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS cluster_blas_inputs_ = {};
#endif
  ComPtr<ID3D12Resource> clas_;
  ComPtr<ID3D12Resource> clas_addresses_;
  ComPtr<ID3D12Resource> cluster_blas_;
  ComPtr<ID3D12Resource> cluster_destination_;
  ComPtr<ID3D12Resource> cluster_scratch_;
  ComPtr<ID3D12Resource> cluster_indices_;
  bool cluster_indices_written_ = false;
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
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> trace_pipeline_;
  ComPtr<ID3D12PipelineState> soft_pipeline_;
  ComPtr<ID3D12PipelineState> scene_data_pipeline_;
  ComPtr<ID3D12PipelineState> omm_pipeline_;
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
  ComPtr<ID3D12PipelineState> fog_pipeline_;
  ComPtr<ID3D12PipelineState> bounce_pipeline_;
  // Paths (v8): last frame's colour (the DLAA input), the G-buffer guides, the reflections' outputs; the history and the
  // cache of the helper's own; the pipelines (DXR 1.2: one state object with both ray generation shaders).
  ComPtr<ID3D12Resource> previous_color_;
  ComPtr<ID3D12Resource> guide_normals_;
  ComPtr<ID3D12Resource> guide_albedo_;
  ComPtr<ID3D12Resource> guide_roughness_;
  ComPtr<ID3D12Resource> reflection_;
  ComPtr<ID3D12Resource> specular_;
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
  // GPU times (RENODX_TW_GPU_TIMES): each slot's timestamps (a trace: its start, its TLAS, its trace, its end; paths:
  // their start and end), read when the slot comes round again; the last trace's and paths' phases in milliseconds.
  ComPtr<ID3D12QueryHeap> timestamps_;
  ComPtr<ID3D12Resource> timestamp_readback_;
  uint64_t timestamp_frequency_ = 0;
  std::array<bool, SLOTS> slot_traced_ = {};
  std::array<double, 4> gpu_ms_ = {};  // the scene (captures, BLASes), the TLAS, the trace; the paths
  ComPtr<ID3D12StateObject> path_state_object_;
  ComPtr<ID3D12Resource> path_shader_table_;  // two ray generation records: the paths, the reflections
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
  ComPtr<ID3D12Resource> sun_rays_;  // the helper's, the textures' size
  ComPtr<ID3D12Fence> fence_;
  std::unordered_map<uint64_t, Mesh> meshes_;
  uint64_t builds_ = 0;
  std::vector<Release> releases_;
  bool failed_ = false;
  std::string status_ = "Ray tracing off";
};

}  // namespace twattila::dlaa_helper
