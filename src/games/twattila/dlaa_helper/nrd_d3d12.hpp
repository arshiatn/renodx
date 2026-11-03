/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// NVIDIA's NRD (NVIDIA RTX SDKs License, github.com/NVIDIA-RTX/NRD: its v4.18.0 tag, its DXIL shaders in a static library;
// build.ps1 adds it when build\downloads\nrd holds it, built by its comment there; without it, the built-in denoisers).
#if __has_include(<NRD.h>)
#include <NRD.h>
#define RENODX_NRD 1
#endif

#if defined(RENODX_NRD)
// One NRD denoiser on the helper's D3D12 device, through NRD's own API (its compute dispatches on the caller's list, no
// NRI, nothing at run time beyond the helper): its pipelines, its pools, its inputs and output as textures of its own at
// the shared textures' size, one root signature (NRD's recommended shape: a root CBV, its samplers, a table of its most
// SRVs then its most UAVs). The camera is the bridge's view-projection (the game's registers), taken apart here: the addon
// has no separate matrices when it asks for a trace. Motion: none but the camera's (NRD reprojects by these matrices).
namespace twattila::dlaa_helper {

class Nrd12 {
 public:
  template <typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  ~Nrd12() {
    if (instance_ != nullptr) nrd::DestroyInstance(*instance_);
  }

  // The denoiser, its pools and pipelines at `width` x `height`, for `slots` requests at once; `finish_code`: a compute
  // shader of the caller's on this root signature (t and u registers from 0, no constants). `error`: why not.
  bool Create(ID3D12Device* device, nrd::Denoiser denoiser, UINT slots, uint32_t width, uint32_t height, const void* finish_code, size_t finish_bytes,
              std::string* error) {
    device_ = device;
    slots_ = slots;
    width_ = width;
    height_ = height;
    const nrd::DenoiserDesc denoiser_desc = {.identifier = IDENTIFIER, .denoiser = denoiser};
    const nrd::InstanceCreationDesc creation = {.denoisers = &denoiser_desc, .denoisersNum = 1};
    if (nrd::CreateInstance(creation, instance_) != nrd::Result::SUCCESS) {
      *error = "NRD: its instance failed";
      return false;
    }
    const nrd::InstanceDesc& desc = *nrd::GetInstanceDesc(*instance_);
    srv_count_ = desc.descriptorPoolDesc.perSetTexturesMaxNum;
    uav_count_ = desc.descriptorPoolDesc.perSetStorageTexturesMaxNum;
    constants_size_ = (desc.constantBufferMaxDataSize + 255u) & ~255u;
    std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges = {{
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = srv_count_, .BaseShaderRegister = desc.resourcesBaseRegisterIndex,
         .RegisterSpace = desc.resourcesSpaceIndex, .OffsetInDescriptorsFromTableStart = 0},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = uav_count_, .BaseShaderRegister = desc.resourcesBaseRegisterIndex,
         .RegisterSpace = desc.resourcesSpaceIndex, .OffsetInDescriptorsFromTableStart = srv_count_},
    }};
    const std::array<D3D12_ROOT_PARAMETER, 2> parameters = {{
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
         .Descriptor = {.ShaderRegister = desc.constantBufferRegisterIndex, .RegisterSpace = desc.constantBufferAndSamplersSpaceIndex}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
         .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}},
    }};
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    for (uint32_t index = 0; index < desc.samplersNum; ++index) {
      const bool linear = desc.samplers[index] == nrd::Sampler::LINEAR_CLAMP;
      samplers.push_back({
          .Filter = linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT,
          .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
          .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
          .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
          .MaxLOD = D3D12_FLOAT32_MAX,
          .ShaderRegister = desc.samplersBaseRegisterIndex + index,
          .RegisterSpace = desc.constantBufferAndSamplersSpaceIndex,
      });
    }
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {
        .NumParameters = static_cast<UINT>(parameters.size()),
        .pParameters = parameters.data(),
        .NumStaticSamplers = static_cast<UINT>(samplers.size()),
        .pStaticSamplers = samplers.data(),
    };
    ComPtr<ID3DBlob> serialized;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), nullptr))
        || FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(root_.GetAddressOf())))) {
      *error = "NRD: its root signature failed";
      return false;
    }
    const auto pipeline = [&](const void* code, size_t bytes, ComPtr<ID3D12PipelineState>* state) {
      const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {.pRootSignature = root_.Get(), .CS = {.pShaderBytecode = code, .BytecodeLength = bytes}};
      return SUCCEEDED(device_->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(state->GetAddressOf())));
    };
    pipelines_.resize(desc.pipelinesNum);
    for (uint32_t index = 0; index < desc.pipelinesNum; ++index) {
      const nrd::ComputeShaderDesc& shader = desc.pipelines[index].computeShaderDXIL;
      if (shader.bytecode == nullptr || !pipeline(shader.bytecode, shader.size, &pipelines_[index])) {
        *error = "NRD: pipeline " + std::string(desc.pipelines[index].shaderIdentifier) + " failed";
        return false;
      }
    }
    if (!pipeline(finish_code, finish_bytes, &finish_)) {
      *error = "NRD: the finishing pass's pipeline failed";
      return false;
    }
    const auto pool = [&](const nrd::TextureDesc* textures, uint32_t count, std::vector<Texture>* made) {
      made->resize(count);
      for (uint32_t index = 0; index < count; ++index) {
        if (!Make(FORMATS[static_cast<size_t>(textures[index].format)], textures[index].downsampleFactor, &(*made)[index])) return false;
      }
      return true;
    };
    if (!pool(desc.permanentPool, desc.permanentPoolSize, &permanent_) || !pool(desc.transientPool, desc.transientPoolSize, &transient_)) {
      *error = "NRD: its textures failed (no memory)";
      return false;
    }
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = slots_ * MAX_TABLES * (srv_count_ + uav_count_),
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    const D3D12_HEAP_PROPERTIES upload = {.Type = D3D12_HEAP_TYPE_UPLOAD};
    const D3D12_RESOURCE_DESC constants_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = uint64_t{slots_} * MAX_TABLES * constants_size_,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = {.Count = 1},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    };
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(heap_.GetAddressOf())))
        || FAILED(device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &constants_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(constants_.GetAddressOf())))
        || FAILED(constants_->Map(0, nullptr, reinterpret_cast<void**>(&constants_data_)))) {
      *error = "NRD: its descriptors or constants failed";
      return false;
    }
    descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
  }

  // An input or output of the denoiser's, a texture of this object's: the size over `downsample`. Null: no memory.
  ID3D12Resource* Add(nrd::ResourceType type, DXGI_FORMAT format, uint32_t downsample = 1) {
    Texture& texture = own_[type];
    return Make(format, downsample, &texture) ? texture.resource.Get() : nullptr;
  }

  // This frame's dispatches on `list` (they take its descriptor heap and root signature: the caller sets its own again).
  // `view_projection`: the camera's registers (clip = dot(position, row)); `settings`: the denoiser's. `Add`ed inputs
  // hold this frame's data, in UAV or SRV state as this object last left them (all textures here start as UAVs).
  bool Run(ID3D12GraphicsCommandList* list, UINT slot, const float* view_projection, uint32_t frame, uint32_t width, uint32_t height, const void* settings) {
    const auto row = [&](size_t index) { return std::array<float, 4>{view_projection[index * 4], view_projection[index * 4 + 1], view_projection[index * 4 + 2], view_projection[index * 4 + 3]}; };
    const auto dot3 = [](const std::array<float, 4>& a, const std::array<float, 4>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    // clip.w is the view's z (a rigid view, column vectors: the rows' xyz are the view's axes times the projection's
    // scales), what clip.x and y hold of it is the jitter's shear.
    const std::array<float, 4> forward = row(3);
    if (std::abs(dot3(forward, forward) - 1.f) > 0.01f) return false;
    const auto axis = [&](const std::array<float, 4>& line, float* scale, float* shear, float* to_view_translation, std::array<float, 3>* direction) {
      *shear = dot3(line, forward);
      const std::array<float, 4> along = {line[0] - *shear * forward[0], line[1] - *shear * forward[1], line[2] - *shear * forward[2], 0.f};
      *scale = std::sqrt(dot3(along, along));
      *direction = {along[0] / *scale, along[1] / *scale, along[2] / *scale};
      *to_view_translation = (line[3] - *shear * forward[3]) / *scale;
    };
    float x_scale = 0.f;
    float y_scale = 0.f;
    float x_shear = 0.f;
    float y_shear = 0.f;
    float x_translation = 0.f;
    float y_translation = 0.f;
    std::array<float, 3> right = {};
    std::array<float, 3> up = {};
    axis(row(0), &x_scale, &x_shear, &x_translation, &right);
    axis(row(1), &y_scale, &y_shear, &y_translation, &up);
    const std::array<float, 4> depth_row = row(2);
    const float depth_scale = dot3(depth_row, forward);
    nrd::CommonSettings common = {};
    const std::array<float, 16> world_to_view = {right[0],      up[0],         forward[0], 0.f,  //
                                                 right[1],      up[1],         forward[1], 0.f,  //
                                                 right[2],      up[2],         forward[2], 0.f,  //
                                                 x_translation, y_translation, forward[3], 1.f};
    const std::array<float, 16> view_to_clip = {x_scale, 0.f, 0.f, 0.f,  //
                                                0.f, y_scale, 0.f, 0.f,  //
                                                0.f, 0.f, depth_scale, 1.f,  //
                                                0.f, 0.f, depth_row[3] - depth_scale * forward[3], 0.f};
    const bool first = frame_index_ == 0;
    // (A second request in a frame continues too.)
    const bool continued = !first && (frame == last_frame_ + 1 || frame == last_frame_) && width == last_width_ && height == last_height_;
    std::memcpy(common.viewToClipMatrix, view_to_clip.data(), sizeof(common.viewToClipMatrix));
    std::memcpy(common.worldToViewMatrix, world_to_view.data(), sizeof(common.worldToViewMatrix));
    std::memcpy(common.viewToClipMatrixPrev, (first ? view_to_clip : last_view_to_clip_).data(), sizeof(common.viewToClipMatrixPrev));
    std::memcpy(common.worldToViewMatrixPrev, (first ? world_to_view : last_world_to_view_).data(), sizeof(common.worldToViewMatrixPrev));
    const std::array<float, 2> jitter = {-x_shear * 0.5f, y_shear * 0.5f};
    common.motionVectorScale[0] = 0.f;
    common.motionVectorScale[1] = 0.f;
    common.motionVectorScale[2] = 0.f;
    common.isMotionVectorInWorldSpace = true;
    common.cameraJitter[0] = jitter[0];
    common.cameraJitter[1] = jitter[1];
    common.cameraJitterPrev[0] = first ? jitter[0] : last_jitter_[0];
    common.cameraJitterPrev[1] = first ? jitter[1] : last_jitter_[1];
    common.resourceSize[0] = static_cast<uint16_t>(width_);
    common.resourceSize[1] = static_cast<uint16_t>(height_);
    common.resourceSizePrev[0] = static_cast<uint16_t>(width_);
    common.resourceSizePrev[1] = static_cast<uint16_t>(height_);
    common.rectSize[0] = static_cast<uint16_t>(width);
    common.rectSize[1] = static_cast<uint16_t>(height);
    common.rectSizePrev[0] = static_cast<uint16_t>(first ? width : last_width_);
    common.rectSizePrev[1] = static_cast<uint16_t>(first ? height : last_height_);
    common.denoisingRange = DENOISING_RANGE;
    common.frameIndex = frame_index_;
    common.accumulationMode = first ? nrd::AccumulationMode::CLEAR_AND_RESTART : (continued ? nrd::AccumulationMode::CONTINUE : nrd::AccumulationMode::RESTART);
    if (nrd::SetCommonSettings(*instance_, common) != nrd::Result::SUCCESS || nrd::SetDenoiserSettings(*instance_, IDENTIFIER, settings) != nrd::Result::SUCCESS) return false;
    const nrd::DispatchDesc* dispatches = nullptr;
    uint32_t dispatch_count = 0;
    const nrd::Identifier identifier = IDENTIFIER;
    if (nrd::GetComputeDispatches(*instance_, &identifier, 1, dispatches, dispatch_count) != nrd::Result::SUCCESS || dispatch_count + 1 > MAX_TABLES) return false;
    ++frame_index_;
    last_frame_ = frame;
    last_width_ = width;
    last_height_ = height;
    last_view_to_clip_ = view_to_clip;
    last_world_to_view_ = world_to_view;
    last_jitter_ = jitter;
    next_table_ = 0;
    slot_ = slot;
    ID3D12DescriptorHeap* heaps[] = {heap_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root_.Get());
    const nrd::InstanceDesc& desc = *nrd::GetInstanceDesc(*instance_);
    D3D12_GPU_VIRTUAL_ADDRESS constants_address = constants_->GetGPUVirtualAddress();
    for (uint32_t index = 0; index < dispatch_count; ++index) {
      const nrd::DispatchDesc& dispatch = dispatches[index];
      const nrd::PipelineDesc& pipeline = desc.pipelines[dispatch.pipelineIndex];
      const UINT table = TableStart();
      std::vector<D3D12_RESOURCE_BARRIER> barriers;
      uint32_t resource = 0;
      UINT srv = 0;
      UINT uav = 0;
      for (uint32_t range = 0; range < pipeline.resourceRangesNum; ++range) {
        for (uint32_t count = 0; count < pipeline.resourceRanges[range].descriptorsNum; ++count) {
          const nrd::ResourceDesc& item = dispatch.resources[resource++];
          Texture& texture = (item.type == nrd::ResourceType::PERMANENT_POOL)   ? permanent_[item.indexInPool]
                             : (item.type == nrd::ResourceType::TRANSIENT_POOL) ? transient_[item.indexInPool]
                                                                                : own_[item.type];
          if (item.descriptorType == nrd::DescriptorType::TEXTURE) {
            Shown(&texture, table, srv++, &barriers);
          } else {
            Written(&texture, table, uav++, &barriers);
          }
        }
      }
      if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      if (!dispatch.constantBufferDataMatchesPreviousDispatch && dispatch.constantBufferDataSize != 0) {
        const uint64_t offset = (uint64_t{slot_} * MAX_TABLES + index) * constants_size_;
        std::memcpy(constants_data_ + offset, dispatch.constantBufferData, dispatch.constantBufferDataSize);
        constants_address = constants_->GetGPUVirtualAddress() + offset;
      }
      list->SetComputeRootConstantBufferView(0, constants_address);
      list->SetComputeRootDescriptorTable(1, Gpu(table));
      list->SetPipelineState(pipelines_[dispatch.pipelineIndex].Get());
      list->Dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
    }
    return true;
  }

  // The caller's finishing pass after Run, on its root signature: t0 this object's `output`, t1 the caller's `shown` as
  // the format `shown_view` says (in a state it can be read in), u0 the caller's `written` (a UAV now), over the rect.
  void Finish(ID3D12GraphicsCommandList* list, nrd::ResourceType output, ID3D12Resource* shown, const D3D12_SHADER_RESOURCE_VIEW_DESC& shown_view, ID3D12Resource* written, DXGI_FORMAT written_format) {
    const UINT table = TableStart();
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    Shown(&own_[output], table, 0, &barriers);
    if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    device_->CreateShaderResourceView(shown, &shown_view, Cpu(table + 1));
    const D3D12_UNORDERED_ACCESS_VIEW_DESC written_view = {.Format = written_format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    device_->CreateUnorderedAccessView(written, nullptr, &written_view, Cpu(table + srv_count_));
    list->SetComputeRootDescriptorTable(1, Gpu(table));
    list->SetPipelineState(finish_.Get());
    list->Dispatch((last_width_ + 7) / 8, (last_height_ + 7) / 8, 1);
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    list->ResourceBarrier(1, &uav_barrier);
  }

  // The inputs (and output: its contents stay) back to UAVs for the caller's passes before the next Run.
  void ToWritten(ID3D12GraphicsCommandList* list) {
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    for (auto& [type, texture] : own_) {
      if (texture.state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) barriers.push_back(Transitioned(&texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    }
    if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
  }

  // This denoiser's textures' memory (pools and own), for the status.
  uint64_t Bytes() const {
    uint64_t bytes = 0;
    const auto add = [&](const Texture& texture) {
      if (!texture.resource) return;
      const D3D12_RESOURCE_DESC desc = texture.resource->GetDesc();
      bytes += device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    };
    for (const Texture& texture : permanent_) add(texture);
    for (const Texture& texture : transient_) add(texture);
    for (const auto& [type, texture] : own_) add(texture);
    return bytes;
  }

 private:
  struct Texture {
    ComPtr<ID3D12Resource> resource;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  };
  static constexpr nrd::Identifier IDENTIFIER = 1;
  // At most this many dispatches a request (with its clearing frame and the finishing pass).
  static constexpr UINT MAX_TABLES = 96;
  // Metres: past every surface; the sky's view depth (the caller's) past this.
  static constexpr float DENOISING_RANGE = 1.0e6f;
  // nrd::Format's order.
  static constexpr std::array<DXGI_FORMAT, static_cast<size_t>(nrd::Format::MAX_NUM)> FORMATS = {
      DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_SNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT,
      DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_SNORM, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_SINT,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
      DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_SNORM, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16_FLOAT,
      DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16_FLOAT,
      DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_R16G16B16A16_FLOAT,
      DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32_FLOAT,
      DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32_FLOAT,
      DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R32G32B32_FLOAT,
      DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_SINT, DXGI_FORMAT_R32G32B32A32_FLOAT,
      DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UINT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
  };

  bool Make(DXGI_FORMAT format, uint32_t downsample, Texture* texture) const {
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = std::max((width_ + downsample - 1) / downsample, 1u),
        .Height = std::max((height_ + downsample - 1) / downsample, 1u),
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    };
    texture->format = format;
    return SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                      IID_PPV_ARGS(texture->resource.GetAddressOf())));
  }
  static D3D12_RESOURCE_BARRIER Transitioned(Texture* texture, D3D12_RESOURCE_STATES after) {
    const D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                            .Transition = {.pResource = texture->resource.Get(),
                                                           .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                           .StateBefore = texture->state,
                                                           .StateAfter = after}};
    texture->state = after;
    return barrier;
  }
  UINT TableStart() { return ((slot_ * MAX_TABLES) + next_table_++) * (srv_count_ + uav_count_); }
  D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT index) const { return {heap_->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T{index} * descriptor_size_)}; }
  D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT index) const { return {heap_->GetGPUDescriptorHandleForHeapStart().ptr + (uint64_t{index} * descriptor_size_)}; }
  void Shown(Texture* texture, UINT table, UINT place, std::vector<D3D12_RESOURCE_BARRIER>* barriers) const {
    if (texture->state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) barriers->push_back(Transitioned(texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    const D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = texture->format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                  .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Texture2D = {.MipLevels = 1}};
    device_->CreateShaderResourceView(texture->resource.Get(), &view, Cpu(table + place));
  }
  void Written(Texture* texture, UINT table, UINT place, std::vector<D3D12_RESOURCE_BARRIER>* barriers) const {
    if (texture->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
      barriers->push_back({.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV, .UAV = {.pResource = texture->resource.Get()}});
    } else {
      barriers->push_back(Transitioned(texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    }
    const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = texture->format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    device_->CreateUnorderedAccessView(texture->resource.Get(), nullptr, &view, Cpu(table + srv_count_ + place));
  }

  ID3D12Device* device_ = nullptr;
  nrd::Instance* instance_ = nullptr;
  ComPtr<ID3D12RootSignature> root_;
  std::vector<ComPtr<ID3D12PipelineState>> pipelines_;
  ComPtr<ID3D12PipelineState> finish_;
  ComPtr<ID3D12DescriptorHeap> heap_;
  ComPtr<ID3D12Resource> constants_;
  uint8_t* constants_data_ = nullptr;
  std::vector<Texture> permanent_;
  std::vector<Texture> transient_;
  std::map<nrd::ResourceType, Texture> own_;
  UINT slots_ = 0;
  UINT slot_ = 0;
  UINT next_table_ = 0;
  UINT srv_count_ = 0;
  UINT uav_count_ = 0;
  UINT descriptor_size_ = 0;
  uint32_t constants_size_ = 0;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t frame_index_ = 0;
  uint32_t last_frame_ = 0;
  uint32_t last_width_ = 0;
  uint32_t last_height_ = 0;
  std::array<float, 16> last_view_to_clip_ = {};
  std::array<float, 16> last_world_to_view_ = {};
  std::array<float, 2> last_jitter_ = {};
};

}  // namespace twattila::dlaa_helper
#endif  // RENODX_NRD
