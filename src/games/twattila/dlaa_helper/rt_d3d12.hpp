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
#include "rt_bounce.h"      // dxc -Fh (build.ps1)
#include "rt_bounce_omm.h"  // dxc -Fh (build.ps1)
#include "rt_omm.h"         // dxc -Fh (build.ps1)
#include "rt_scene_data.h"  // dxc -Fh (build.ps1)
#include "rt_soft.h"        // dxc -Fh (build.ps1)
#include "rt_trace.h"       // dxc -Fh (build.ps1)
#include "rt_trace_omm.h"   // dxc -Fh (build.ps1)

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
// rt_bounce.cs_6_5.hlsl against that frame's TLAS. Its queue waits for the addon's fence value and signals the next.
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
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
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
      if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                               IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close()) || !slot.instances || !slot.table || !slot.sizes || !slot.readback
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
    free_triangles_.free = {{0, STATIC_TRIANGLES}};
    free_uvs_.free = {{0, STATIC_UVS}};
    for (uint32_t record = STATIC_RECORDS; record > 0; --record) {
      free_records_.push_back(record - 1);
    }
    // The passes: t0 the TLAS (root SRV); the table (Descriptor); b0 the settings, b1 a dispatch's (root constants); t8
    // the capture table, u6 / u7 / u8 the micromaps' buffers (root descriptors); s0 linear wrap.
    const std::array<D3D12_DESCRIPTOR_RANGE, 4> ranges = {{
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = SRV_COUNT, .BaseShaderRegister = 1, .OffsetInDescriptorsFromTableStart = 0},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = LIGHT - SRV_COUNT, .BaseShaderRegister = 0,
         .OffsetInDescriptorsFromTableStart = SRV_COUNT},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = 1, .BaseShaderRegister = 9, .OffsetInDescriptorsFromTableStart = LIGHT},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = 1, .BaseShaderRegister = 9, .OffsetInDescriptorsFromTableStart = BOUNCE},
    }};
    const std::array<D3D12_ROOT_PARAMETER, 9> parameters = {{
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
             && pipeline(RT_BOUNCE_OMM, sizeof(RT_BOUNCE_OMM), &bounce_pipeline_);
    if (!dxr12_) {
      omm_pipeline_.Reset();
      trace_pipeline_.Reset();
      bounce_pipeline_.Reset();
    }
    if ((!trace_pipeline_ && !pipeline(RT_TRACE, sizeof(RT_TRACE), &trace_pipeline_))
        || (!bounce_pipeline_ && !pipeline(RT_BOUNCE, sizeof(RT_BOUNCE), &bounce_pipeline_)) || !pipeline(RT_SOFT, sizeof(RT_SOFT), &soft_pipeline_)
        || !pipeline(RT_SCENE_DATA, sizeof(RT_SCENE_DATA), &scene_data_pipeline_)
        || FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(descriptors_.GetAddressOf())))) {
      return Fail("Ray tracing: the trace shaders failed (needs shader model 6.5)");
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
    status_ = std::format("Ray tracing ready ({}; {})", dxr12_ ? "DXR 1.2: opacity micromaps" : "DXR 1.1", sdk);
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
    WaitFor(slot.value);
    const uint64_t completed = own_fence_->GetCompletedValue();
    std::erase_if(releases_, [completed](const Release& release) { return release.value <= completed; });
    std::erase_if(pending_frees_, [&](const PendingFree& pending) {
      if (pending.value > completed) return false;
      free_records_.push_back(pending.record);
      free_triangles_.Give(pending.normal_offset, pending.triangles);
      if (pending.uv_offset != UINT32_MAX) free_uvs_.Give(pending.uv_offset, pending.triangles * 3);
      return true;
    });
    const bool recorded = SUCCEEDED(slot.allocator->Reset()) && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr))
                          && Record(shared, version, slot_index, &slot);
    traced_ = traced_ || recorded;
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

  // The ray-traced Light Bounce (bridge v7): rt_bounce.cs_6_5.hlsl against the TLAS this frame's trace built (the same
  // queue, so after it); the queue waits for the addon's fence value and signals the next (also when nothing ran).
  // False: not traced.
  bool Bounce(const dlaa_bridge::Shared& shared) {
    if (!fence_) return false;
    const size_t slot_index = next_slot_;
    Slot& slot = slots_[slot_index];
    next_slot_ = (next_slot_ + 1) % SLOTS;
    WaitFor(slot.value);
    bool recorded = light_ && bounce_ && traced_ && SUCCEEDED(slot.allocator->Reset())
                    && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr));
    if (recorded) {
      auto* list = slot.list.Get();
      ID3D12DescriptorHeap* heaps[] = {descriptors_.Get()};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootSignature(root_signature_.Get());
      list->SetComputeRootDescriptorTable(ROOT_TABLE, descriptors_->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootShaderResourceView(ROOT_SCENE, tlas_->GetGPUVirtualAddress());
      // Unused root descriptors still point at a buffer.
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot.table->GetGPUVirtualAddress());
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }
      const std::array<uint32_t, 8> settings = {
          shared.rt_frame,
          0u,
          std::bit_cast<uint32_t>(std::clamp(shared.rt_bounce_range, 0.5f, 200.f)),
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
          {light_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
          {bounce_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
      };
      if (atlas_) textures.emplace_back(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      std::vector<D3D12_RESOURCE_BARRIER> barriers;
      for (const auto& [resource, used] : textures) barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      list->SetPipelineState(bounce_pipeline_.Get());
      list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
      barriers.clear();
      for (const auto& [resource, used] : textures) barriers.push_back(Transition(resource, used, D3D12_RESOURCE_STATE_COMMON));
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
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
  static constexpr UINT MAX_INSTANCES = 16384;
  static constexpr UINT TRANSFER_VERTICES = dlaa_bridge::RT_TRANSFER_WIDTH * dlaa_bridge::RT_TRANSFER_HEIGHT;
  // Meshes drawn within this many frames cast (but the terrain only in frames that draw it); unseen longer: released.
  static constexpr uint32_t RECENT_FRAMES = 300;
  static constexpr uint32_t RELEASE_FRAMES = 3600;
  // The units' BLAS is refitted to each frame's poses while their draws stay the same, rebuilt this often.
  static constexpr uint32_t UNITS_REBUILD_FRAMES = 30;
  // Records (InstanceID + geometry index): static meshes first, then each slot's units in turn; the pools: a normal per
  // triangle, a UV per leaf vertex (static meshes first, then each slot's units).
  static constexpr UINT STATIC_RECORDS = MAX_INSTANCES;
  static constexpr UINT RECORD_COUNT = STATIC_RECORDS + SLOTS * dlaa_bridge::RT_CAPTURES;
  static constexpr UINT STATIC_TRIANGLES = 8u << 20;
  static constexpr UINT DYNAMIC_TRIANGLES = (3u << 20) / 3;  // rt_scene.hpp MAX_DYNAMIC_VERTICES
  static constexpr UINT NORMAL_POOL = STATIC_TRIANGLES + SLOTS * DYNAMIC_TRIANGLES;
  static constexpr UINT STATIC_UVS = 8u << 20;
  static constexpr UINT UV_POOL = STATIC_UVS;
  static constexpr uint32_t RECORD_MASKED = 1;  // rt_scene.hlsli
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
    DESCRIPTOR_COUNT,
  };
  static constexpr UINT SRV_COUNT = VISIBILITY;
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
  };

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

  static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    return {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {.pResource = resource, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, .StateBefore = before, .StateAfter = after},
    };
  }

  // The request's commands into the slot's list (closed); false: nothing usable recorded.
  bool Record(const dlaa_bridge::Shared& shared, uint32_t version, size_t slot_index, Slot* slot) {
    auto* list = slot->list.Get();
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
    const uint32_t capture_count = std::min(shared.rt_capture_count, dlaa_bridge::RT_CAPTURES);
    if (capture_count != 0) {
      uint32_t vertex_end = 0;
      for (uint32_t index = 0; index < capture_count; ++index) {
        vertex_end = std::max(vertex_end, shared.rt_captures[index].first_vertex + shared.rt_captures[index].vertex_count);
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
                                               placement.record, slice | ((placement.masked ? RECORD_MASKED : 0u) << 16), 0, 0};
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
      uint32_t units_record = UINT32_MAX;
      struct Build {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
        bool compact = false;
      };
      std::vector<Build> builds;
      uint64_t scratch_needed = 0;
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
        if (with_micromaps) {
          mesh.micromaps = std::move(bake->array);
          mesh.micromap_indices = std::move(bake->indices);
        }
        slot->compactions.emplace_back(capture.key, mesh.build);
        ++built;
      }
      // The units: refitted in place while the request's unit draws are those of the last build.
      if (!unit_geometries.empty()) {
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
      list->ResourceBarrier(1, &uav_barrier);
      units_record_ = units_record;
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
      AddInstance(slot, &instance_count, mesh.blas.Get(), mesh.record, mesh.foliage);
      triangles += mesh.triangles;
    }
    if (units_built && instance_count < MAX_INSTANCES) {
      AddInstance(slot, &instance_count, units_blas_.Get(), units_record_, false);
      triangles += dynamic_triangles;
    }
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
    // The shared textures: COMMON between the APIs, read or written here.
    std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> textures = {
        {positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {sun_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {visibility_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
        {ambient_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
    };
    if (atlas_) textures.emplace_back(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {uav_barrier};
    for (const auto& [resource, used] : textures) {
      barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootShaderResourceView(ROOT_SCENE, tlas_->GetGPUVirtualAddress());
    const float softness = (version >= 6) ? std::clamp(shared.rt_sun_softness, 0.f, 5.f) : 0.f;
    const bool soft = (shared.rt_flags & dlaa_bridge::RT_SUN_SHADOWS) != 0 && softness > 0.01f;
    const std::array<uint32_t, 8> settings = {
        frame,
        shared.rt_flags,
        std::bit_cast<uint32_t>(std::clamp(shared.rt_ambient_range, 0.1f, 100.f)),
        std::clamp(shared.rt_ambient_rays, 1u, 16u),
        shared.rt_width,
        shared.rt_height,
        std::bit_cast<uint32_t>(soft ? std::tan(softness * 3.14159265f / 180.f) : 0.f),
        0u,
    };
    list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(settings.size()), settings.data(), 0);
    list->SetPipelineState(trace_pipeline_.Get());
    list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
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
    if (FAILED(list->Close())) return Fail("Ray tracing: D3D12 stopped");
    // Meshes unseen for long give their BLAS (and micromaps, record, pool ranges) back once the GPU is past this request.
    std::erase_if(meshes_, [&](auto& entry) {
      if (entry.second.drawn_frame + RELEASE_FRAMES >= frame) return false;
      ReleaseMesh(&entry.second);
      return true;
    });
    status_ = std::format("Ray tracing ({}): {} meshes ({} new, {} with micromaps, {} MB), {} in the scene, {:.1f} M triangles ({:.2f} M of "
                          "units, {})",
                          dxr12_ ? "DXR 1.2" : "DXR 1.1", meshes_.size(), built, micromaps_built, bytes >> 20, instance_count,
                          static_cast<double>(triangles) / 1.0e6, static_cast<double>(units_built ? dynamic_triangles : 0u) / 1.0e6,
                          units_built ? (units_refitted ? "refitted" : "rebuilt") : "none");
    return true;
  }

  // A mesh's BLAS and micromaps released, its record and pool ranges given back, once the GPU is past this request.
  void ReleaseMesh(Mesh* mesh) {
    for (ComPtr<ID3D12Resource>* resource : {std::addressof(mesh->blas), std::addressof(mesh->micromaps), std::addressof(mesh->micromap_indices)}) {
      if (*resource) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*resource)});
    }
    pending_frees_.push_back({.value = own_value_ + 1, .record = mesh->record, .normal_offset = mesh->normal_offset,
                              .triangles = mesh->triangles, .uv_offset = mesh->uv_offset});
  }

  // A world-space BLAS into the slot's TLAS instances (leaves not opaque: their alpha is tested), its InstanceID its
  // first geometry's record.
  static void AddInstance(Slot* slot, UINT* instance_count, ID3D12Resource* blas, uint32_t record, bool foliage) {
    D3D12_RAYTRACING_INSTANCE_DESC& instance = slot->instance_data[(*instance_count)++];
    instance = {
        .InstanceID = record,
        .InstanceMask = 0xFF,
        .InstanceContributionToHitGroupIndex = 0,
        .Flags = static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE
                                   | (foliage ? 0u : static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE))),
        .AccelerationStructure = blas->GetGPUVirtualAddress(),
    };
    instance.Transform[0][0] = 1.f;
    instance.Transform[1][1] = 1.f;
    instance.Transform[2][2] = 1.f;
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
  ComPtr<ID3D12PipelineState> bounce_pipeline_;
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
