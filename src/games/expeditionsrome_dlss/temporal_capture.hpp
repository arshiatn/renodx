/*
 * Copyright (C) 2026 RenoDX contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <d3d11_1.h>
#include <wrl/client.h>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"

namespace expeditionsrome::dlss::temporal_capture {

using Clock = std::chrono::steady_clock;
using Microsoft::WRL::ComPtr;

struct Target {
  uint32_t hash;
  bool is_compute;
};

static constexpr std::array TARGETS = {
    Target{.hash = 0x29C140B2, .is_compute = true},
    Target{.hash = 0x201C3D3D, .is_compute = true},
    Target{.hash = 0xCCB4A156, .is_compute = true},
    Target{.hash = 0xD8C07EEE, .is_compute = true},
    Target{.hash = 0x4B43383D, .is_compute = true},
    Target{.hash = 0xB266ECB6, .is_compute = true},
    Target{.hash = 0x8763A5F8, .is_compute = false},
    Target{.hash = 0x1CC13002, .is_compute = false},
    Target{.hash = 0xAFD833AA, .is_compute = false},
    Target{.hash = 0x3351BD21, .is_compute = false},
    Target{.hash = 0x6827D9DD, .is_compute = false},
    Target{.hash = 0x57192B01, .is_compute = false},
};
static constexpr auto CAPTURE_DURATION = std::chrono::seconds(15);
static constexpr auto SAMPLE_INTERVAL = std::chrono::seconds(2);
static constexpr auto READBACK_TIMEOUT = std::chrono::seconds(2);
static constexpr size_t MAX_PENDING = 8;
static constexpr uint32_t MAX_SAMPLES = 4;

struct Progress {
  uint64_t commands = 0;
  uint32_t samples = 0;
  uint32_t completed = 0;
  uint32_t failed = 0;
  uint32_t buffers = 0;
  uint64_t last_present = UINT64_MAX;
  Clock::time_point next_sample = {};
  bool deferred_logged = false;
};

struct BufferReadback {
  ComPtr<ID3D11Buffer> staging;
  uint32_t first_constant = 0;
  uint32_t vectors = 0;
};

struct Sample {
  uint64_t generation = 0;
  size_t target = 0;
  uint32_t index = 0;
  uint64_t present = 0;
  Clock::time_point queued_at = {};
  ComPtr<ID3D11Query> query;
  std::array<BufferReadback, 2> buffers;
  ComPtr<ID3D11Texture2D> exposure;
  bool failed = false;
};

struct CaptureState {
  std::shared_mutex mutex;
  uint64_t generation = 0;
  bool active = false;
  bool finished = true;
  bool polling = false;
  uint64_t device = 0;
  uintptr_t context = 0;
  uint64_t presents = 0;
  size_t pending_count = 0;
  Clock::time_point started_at = {};
  std::array<Progress, TARGETS.size()> progress;
  std::vector<Sample> pending;
};

static CaptureState state;

static bool QueueCapture() {
  uint64_t generation;
  {
    const std::unique_lock lock(state.mutex);
    if (!state.finished || state.pending_count != 0) return false;
    generation = ++state.generation;
    state.active = true;
    state.finished = false;
    state.device = 0;
    state.context = 0;
    state.presents = 0;
    state.started_at = Clock::now();
    state.progress = {};
    state.pending.reserve(MAX_PENDING);
  }
  renodx::utils::log::i("[Expeditions Rome AA capture] generation ", generation,
                        " queued: 15 seconds, 4 samples/hash spaced 2 seconds; stock passes unchanged."
                        " Asynchronous readbacks only; wait 20 seconds before saving the log.");
  return true;
}

static void Reset() {
  std::vector<Sample> cancelled;
  {
    const std::unique_lock lock(state.mutex);
    ++state.generation;
    state.active = false;
    state.finished = true;
    state.polling = false;
    state.device = 0;
    state.context = 0;
    state.pending_count = 0;
    state.progress = {};
    cancelled.swap(state.pending);
  }
}

static std::string Snapshot() {
  const std::shared_lock lock(state.mutex);
  uint32_t samples = 0;
  uint32_t completed = 0;
  uint32_t failed = 0;
  for (const auto& progress : state.progress) {
    samples += progress.samples;
    completed += progress.completed;
    failed += progress.failed;
  }
  return std::format("{}: {} samples, {} completed, {} failed, {} pending",
                     (state.finished ? "Capture idle/finished" : (state.active ? "Capturing" : "Finishing readbacks")),
                     samples, completed, failed, state.pending_count);
}

static constexpr auto OnCommand = []<typename Context>(Context& command)
    -> renodx::utils::command_action::CallbackResult<Context> {
  {
    const std::shared_lock lock(state.mutex);
    if (!state.active) return {};
  }
  if (command.cmd_list == nullptr || command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  const bool is_compute = command.IsDispatch();
  const auto target = std::ranges::find_if(TARGETS, [&](const Target& item) {
    return item.hash == command.matched_shader_hash && item.is_compute == is_compute;
  });
  if (target == TARGETS.end()) return {};
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  if (context == nullptr) return {};
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  const bool immediate = context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
  const auto device_handle = command.cmd_list->get_device()->get_native();
  const auto now = Clock::now();
  Sample sample{.target = static_cast<size_t>(target - TARGETS.begin()), .queued_at = now};
  {
    const std::unique_lock lock(state.mutex);
    if (!state.active || now - state.started_at >= CAPTURE_DURATION) return {};
    if (state.device != 0 && state.device != device_handle) return {};
    if (immediate && state.context != 0 && state.context != reinterpret_cast<uintptr_t>(context)) return {};
    auto& progress = state.progress[sample.target];
    ++progress.commands;
    state.device = device_handle;
    if (!immediate) {
      if (progress.deferred_logged) return {};
      progress.deferred_logged = true;
    } else {
      if (progress.samples >= MAX_SAMPLES || progress.last_present == state.presents
          || now < progress.next_sample || state.pending_count >= MAX_PENDING) return {};
      state.context = reinterpret_cast<uintptr_t>(context);
      ++state.pending_count;
      sample.index = ++progress.samples;
      progress.last_present = state.presents;
      progress.next_sample = now + SAMPLE_INTERVAL;
    }
    sample.generation = state.generation;
    sample.present = state.presents;
  }
  const auto prefix = std::format("[Expeditions Rome AA capture] generation {} hash 0x{:08X} sample {} present {} ",
                                  sample.generation, target->hash, sample.index, sample.present);
  if (!immediate) {
    renodx::utils::log::i(prefix, "deferred-context command observed; exact readback unavailable, no bindings queried.");
    return {};
  }

  std::ostringstream metadata;
  const auto describe_resource = [&metadata](ID3D11Resource* resource) {
    if (resource == nullptr) {
      metadata << " unbound";
      return;
    }
    metadata << " resource=" << renodx::utils::log::AsPtr(resource);
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
      D3D11_TEXTURE2D_DESC desc = {};
      texture->GetDesc(&desc);
      metadata << " allocated=" << desc.Width << 'x' << desc.Height
               << " resource_dxgi=" << desc.Format << " mips=" << desc.MipLevels
               << " array=" << desc.ArraySize << " samples=" << desc.SampleDesc.Count
               << " bind=0x" << std::hex << desc.BindFlags << std::dec;
    } else {
      D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
      resource->GetType(&dimension);
      metadata << " resource_dimension=" << dimension;
    }
  };
  metadata << prefix << (is_compute ? "CS" : "PS") << " context=" << renodx::utils::log::AsPtr(context);
  if constexpr (std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DispatchArguments>) {
    metadata << " dispatch=" << command.arguments.group_count_x << 'x' << command.arguments.group_count_y
             << 'x' << command.arguments.group_count_z;
  }
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  ComPtr<ID3D11Predicate> predicate;
  context->GetPredication(predicate.GetAddressOf(), nullptr);
  if (predicate) {
    metadata << " predicated: CB copies skipped";
    sample.failed = true;
  } else {
    const D3D11_QUERY_DESC desc = {.Query = D3D11_QUERY_EVENT};
    if (const HRESULT result = device->CreateQuery(&desc, sample.query.GetAddressOf()); FAILED(result)) {
      metadata << " event-query creation failed=" << renodx::utils::log::AsHex(result);
      sample.failed = true;
    }
  }
  const bool spatial = target->hash == 0x57192B01;
  std::array<ID3D11ShaderResourceView*, 5> input_pointers = {};
  std::array<ComPtr<ID3D11ShaderResourceView>, 5> inputs;
  const UINT input_count = (spatial ? 1 : 5);
  if (is_compute) {
    context->CSGetShaderResources(0, input_count, input_pointers.data());
  } else {
    context->PSGetShaderResources(0, input_count, input_pointers.data());
  }
  for (UINT slot = 0; slot < input_count; ++slot) {
    inputs[slot].Attach(input_pointers[slot]);
  }
  for (UINT slot = 0; slot < input_count; ++slot) {
    metadata << "\n"
             << prefix << "SRV t" << slot;
    ComPtr<ID3D11Resource> resource;
    if (inputs[slot]) {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
      inputs[slot]->GetDesc(&desc);
      inputs[slot]->GetResource(resource.GetAddressOf());
      metadata << " view_dxgi=" << desc.Format << " view_dimension=" << desc.ViewDimension;
      if (desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
        metadata << " first_mip=" << desc.Texture2D.MostDetailedMip << " view_mips=" << desc.Texture2D.MipLevels;
      }
    }
    describe_resource(resource.Get());
    if (slot == 0 && !spatial && resource && sample.query) {
      ComPtr<ID3D11Texture2D> texture;
      if (SUCCEEDED(resource.As(&texture))) {
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        if (desc.Width == 1 && desc.Height == 1 && desc.ArraySize == 1 && desc.MipLevels == 1
            && desc.SampleDesc.Count == 1 && desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
          desc.Usage = D3D11_USAGE_STAGING;
          desc.BindFlags = 0;
          desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
          desc.MiscFlags = 0;
          if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, sample.exposure.GetAddressOf()))) {
            context->CopyResource(sample.exposure.Get(), texture.Get());
          }
        }
      }
      metadata << " exposure_texel_readback=" << bool(sample.exposure);
    }
  }
  if (is_compute) {
    ComPtr<ID3D11UnorderedAccessView> output;
    context->CSGetUnorderedAccessViews(0, 1, output.GetAddressOf());
    ComPtr<ID3D11Resource> resource;
    metadata << "\n"
             << prefix << "UAV u0";
    if (output) {
      D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
      output->GetDesc(&desc);
      output->GetResource(resource.GetAddressOf());
      metadata << " view_dxgi=" << desc.Format << " view_dimension=" << desc.ViewDimension;
      if (desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D) {
        metadata << " mip=" << desc.Texture2D.MipSlice;
      }
    }
    describe_resource(resource.Get());
  } else {
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> output_pointers = {};
    std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> outputs;
    context->OMGetRenderTargets(static_cast<UINT>(output_pointers.size()), output_pointers.data(), nullptr);
    for (size_t slot = 0; slot < outputs.size(); ++slot) {
      outputs[slot].Attach(output_pointers[slot]);
    }
    for (size_t slot = 0; slot < outputs.size(); ++slot) {
      if (!outputs[slot]) continue;
      D3D11_RENDER_TARGET_VIEW_DESC desc = {};
      outputs[slot]->GetDesc(&desc);
      ComPtr<ID3D11Resource> resource;
      outputs[slot]->GetResource(resource.GetAddressOf());
      metadata << "\n"
               << prefix << "RTV " << slot << " view_dxgi=" << desc.Format << " view_dimension=" << desc.ViewDimension;
      if (desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) {
        metadata << " mip=" << desc.Texture2D.MipSlice;
      }
      describe_resource(resource.Get());
    }
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports;
    UINT count = static_cast<UINT>(viewports.size());
    context->RSGetViewports(&count, viewports.data());
    for (UINT index = 0; index < count; ++index) {
      const auto& viewport = viewports[index];
      metadata << "\n"
               << prefix << "viewport " << index << "=(" << viewport.TopLeftX << ',' << viewport.TopLeftY
               << ',' << viewport.Width << ',' << viewport.Height << ',' << viewport.MinDepth << ',' << viewport.MaxDepth << ')';
    }
  }

  ComPtr<ID3D11DeviceContext1> context1;
  context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()));
  for (UINT slot = 0; slot < (spatial ? 1u : 2u); ++slot) {
    ComPtr<ID3D11Buffer> buffer;
    UINT first = 0;
    UINT count = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;
    if (context1) {
      if (is_compute) {
        context1->CSGetConstantBuffers1(slot, 1, buffer.GetAddressOf(), &first, &count);
      } else {
        context1->PSGetConstantBuffers1(slot, 1, buffer.GetAddressOf(), &first, &count);
      }
    } else {
      if (is_compute) {
        context->CSGetConstantBuffers(slot, 1, buffer.GetAddressOf());
      } else {
        context->PSGetConstantBuffers(slot, 1, buffer.GetAddressOf());
      }
    }
    metadata << "\n"
             << prefix << "CB b" << slot << " first=" << first << " count=" << count;
    if (!buffer) {
      metadata << " unbound";
      sample.failed = true;
      continue;
    }
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    metadata << " resource=" << renodx::utils::log::AsPtr(buffer.Get()) << " bytes=" << desc.ByteWidth;
    if (!sample.query) continue;
    const UINT vectors = (slot == 1 ? 130 : (spatial ? 1 : (is_compute ? 27 : 24)));
    ComPtr<ID3D11Device> source_device;
    buffer->GetDevice(source_device.GetAddressOf());
    auto* native_source_device = source_device.Get();
    renodx::utils::directx::NativeFromReShadeProxy(&native_source_device);
    const uint64_t end = (uint64_t{first} + vectors) * 16;
    if (native_source_device != device.Get() || (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0
        || desc.ByteWidth == 0 || desc.ByteWidth > 64 * 1024 || end > desc.ByteWidth || vectors > count) {
      metadata << " unsupported bound range/device";
      sample.failed = true;
      continue;
    }
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    auto& readback = sample.buffers[slot];
    if (const HRESULT result = device->CreateBuffer(&staging_desc, nullptr, readback.staging.GetAddressOf()); FAILED(result)) {
      metadata << " staging creation failed=" << renodx::utils::log::AsHex(result);
      sample.failed = true;
      continue;
    }
    readback.first_constant = first;
    readback.vectors = vectors;
    // Whole-buffer copies preserve the currently bound bytes before UE reuses them.
    context->CopyResource(readback.staging.Get(), buffer.Get());
  }
  renodx::utils::log::i(metadata.str());
  const bool copied = sample.exposure || sample.buffers[0].staging || sample.buffers[1].staging;
  if (copied) {
    context->End(sample.query.Get());
  }
  {
    const std::unique_lock lock(state.mutex);
    if (state.generation == sample.generation) {
      if (copied) {
        state.pending.push_back(std::move(sample));
      } else {
        --state.pending_count;
        ++state.progress[sample.target].failed;
      }
    }
  }
  return {};
};

static void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  {
    const std::shared_lock lock(state.mutex);
    if (state.finished || state.polling) return;
  }
  if (queue == nullptr || queue->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_native());
  if (context == nullptr) return;
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  const auto device_handle = queue->get_device()->get_native();
  const auto now = Clock::now();
  uint64_t generation;
  std::vector<Sample> pending;
  {
    const std::unique_lock lock(state.mutex);
    if (state.finished || state.polling || (state.device != 0 && state.device != device_handle)
        || (state.context != 0 && state.context != reinterpret_cast<uintptr_t>(context))) return;
    state.polling = true;
    generation = state.generation;
    ++state.presents;
    if (now - state.started_at >= CAPTURE_DURATION) {
      state.active = false;
    }
    pending.swap(state.pending);
  }
  std::vector<Sample> retry;
  std::array<uint32_t, TARGETS.size()> completed = {};
  std::array<uint32_t, TARGETS.size()> failed = {};
  std::array<uint32_t, TARGETS.size()> buffers = {};
  size_t retired = 0;
  for (auto& sample : pending) {
    const auto prefix = std::format("[Expeditions Rome AA capture] generation {} hash 0x{:08X} sample {} present {} ",
                                    generation, TARGETS[sample.target].hash, sample.index, sample.present);
    BOOL ready = FALSE;
    const HRESULT result = context->GetData(sample.query.Get(), &ready, sizeof(ready), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (now - sample.queued_at >= READBACK_TIMEOUT || FAILED(result)) {
      renodx::utils::log::i(prefix, "readback failed/timed out, query HRESULT=", renodx::utils::log::AsHex(result));
      ++failed[sample.target];
      ++retired;
      continue;
    }
    if (result != S_OK || !ready) {
      retry.push_back(std::move(sample));
      continue;
    }
    for (size_t slot = 0; slot < sample.buffers.size(); ++slot) {
      auto& buffer = sample.buffers[slot];
      if (!buffer.staging) continue;
      std::array<std::array<float, 4>, 130> values;
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      const HRESULT mapped_result = context->Map(buffer.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
      if (mapped_result == DXGI_ERROR_WAS_STILL_DRAWING) continue;
      if (SUCCEEDED(mapped_result)) {
        if (mapped.pData != nullptr) {
          std::memcpy(values.data(), static_cast<const uint8_t*>(mapped.pData) + uint64_t{buffer.first_constant} * 16,
                      uint64_t{buffer.vectors} * 16);
        }
        context->Unmap(buffer.staging.Get(), 0);
      }
      buffer.staging.Reset();
      if (FAILED(mapped_result) || mapped.pData == nullptr) {
        renodx::utils::log::i(prefix, "b", slot, " nonblocking Map failed=", renodx::utils::log::AsHex(mapped_result));
        sample.failed = true;
        continue;
      }
      ++buffers[sample.target];
      std::ostringstream output;
      output << prefix << "GPU cb" << slot << " vectors=" << buffer.vectors;
      for (UINT index = 0; index < buffer.vectors; ++index) {
        if (slot == 1 && !(index >= 28 && index <= 39) && index != 61 && !(index >= 118 && index <= 129)) continue;
        std::array<uint32_t, 4> bits;
        std::memcpy(bits.data(), values[index].data(), 16);
        output << "\n"
               << prefix << "GPU cb" << slot << '[' << index << "]=("
               << values[index][0] << ',' << values[index][1] << ',' << values[index][2] << ',' << values[index][3]
               << ") bits=(0x" << std::hex << bits[0] << ",0x" << bits[1] << ",0x" << bits[2] << ",0x" << bits[3] << std::dec << ')';
      }
      renodx::utils::log::i(output.str());
    }
    if (sample.exposure) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      std::array<float, 4> values = {};
      const HRESULT mapped_result = context->Map(sample.exposure.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
      if (mapped_result != DXGI_ERROR_WAS_STILL_DRAWING) {
        if (SUCCEEDED(mapped_result)) {
          if (mapped.pData) {
            std::memcpy(values.data(), mapped.pData, sizeof(values));
          }
          context->Unmap(sample.exposure.Get(), 0);
        }
        sample.exposure.Reset();
        if (SUCCEEDED(mapped_result) && mapped.pData) {
          std::array<uint32_t, 4> bits;
          std::memcpy(bits.data(), values.data(), sizeof(bits));
          renodx::utils::log::i(prefix, "GPU exposure t0=(", values[0], ',', values[1], ',', values[2], ',', values[3],
                                ") bits=(", renodx::utils::log::AsHex(bits[0]), ',', renodx::utils::log::AsHex(bits[1]), ',',
                                renodx::utils::log::AsHex(bits[2]), ',', renodx::utils::log::AsHex(bits[3]), ')');
        } else {
          sample.failed = true;
          renodx::utils::log::i(prefix, "exposure Map failed=", renodx::utils::log::AsHex(mapped_result));
        }
      }
    }
    if (sample.exposure || sample.buffers[0].staging || sample.buffers[1].staging) {
      retry.push_back(std::move(sample));
    } else {
      if (sample.failed) {
        ++failed[sample.target];
      } else {
        ++completed[sample.target];
      }
      ++retired;
    }
  }
  bool report = false;
  std::array<Progress, TARGETS.size()> progress;
  uint64_t presents = 0;
  {
    const std::unique_lock lock(state.mutex);
    if (state.generation == generation) {
      state.polling = false;
      state.pending_count -= retired;
      for (auto& sample : retry) {
        state.pending.push_back(std::move(sample));
      }
      for (size_t index = 0; index < TARGETS.size(); ++index) {
        state.progress[index].completed += completed[index];
        state.progress[index].failed += failed[index];
        state.progress[index].buffers += buffers[index];
      }
      if (!state.active && state.pending_count == 0) {
        state.finished = true;
        report = true;
        progress = state.progress;
        presents = state.presents;
      }
    }
  }
  if (report) {
    renodx::utils::log::i("[Expeditions Rome AA capture] generation ", generation, " finished: presents=", presents);
    for (size_t index = 0; index < TARGETS.size(); ++index) {
      renodx::utils::log::i("[Expeditions Rome AA capture] hash ", renodx::utils::log::AsHex(TARGETS[index].hash),
                            " commands=", progress[index].commands, " samples=", progress[index].samples,
                            " completed=", progress[index].completed, " failed=", progress[index].failed,
                            " CB_readbacks=", progress[index].buffers, " deferred=", progress[index].deferred_logged);
    }
  }
}

}  // namespace expeditionsrome::dlss::temporal_capture
