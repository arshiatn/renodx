/*
 * Copyright (C) 2026 RenoDX contributors
 * SPDX-License-Identifier: MIT
 *
 * From an x64 VS developer prompt at the repository root, after configuring
 * the addon target:
 * clang-cl /nologo /std:c++20 /EHsc /MTd /DWIN32=1 /D_WINDOWS /D_DEBUG
 *   /D_ITERATOR_DEBUG_LEVEL=0 /DNOMINMAX /Ibuild/expeditionsrome_dlss.include
 *   /Iexternal/Detours/include /Iexternal/json/include /Iexternal/frozen/include
 *   /Iexternal/gtl/include /Iexternal/reshade
 *   src/games/expeditionsrome_dlss/tests/temporal_capture_smoke.cpp
 *   /Fobuild/temporal_capture_smoke.obj /Febuild/temporal_capture_smoke.exe
 *   /link d3d11.lib dxgi.lib user32.lib
 * (Join the command lines above into one line.)
 * No game launch or NGX initialization.
 * Blocking maps below are test assertions, never part of the capture API.
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../temporal_capture.hpp"

extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  std::cout << message << '\n';
}

struct TestDevice {
  ID3D11Device* native;
  reshade::api::device_api get_api() const { return reshade::api::device_api::d3d11; }
  uint64_t get_native() const { return reinterpret_cast<uint64_t>(native); }
};

struct TestCommandList {
  TestDevice* device;
  ID3D11DeviceContext* native;
  TestDevice* get_device() const { return device; }
  uint64_t get_native() const { return reinterpret_cast<uint64_t>(native); }
};

template <typename Arguments>
struct TestCommand {
  using ArgumentType = Arguments;
  TestCommandList* cmd_list;
  Arguments arguments = {};
  uint32_t matched_shader_hash;
  bool IsDispatch() const { return std::is_same_v<Arguments, renodx::utils::command_action::DispatchArguments>; }
};

int main() {
  namespace capture = expeditionsrome::dlss::temporal_capture;
  using Microsoft::WRL::ComPtr;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                               device.GetAddressOf(), nullptr, context.GetAddressOf()))) return 1;
  TestDevice test_device{.native = device.Get()};
  TestCommandList test_list{.device = &test_device, .native = context.Get()};
  std::array<std::array<float, 4>, 130> source;
  for (size_t index = 0; index < source.size(); ++index) {
    source[index] = {static_cast<float>(index), .25f, .5f, .75f};
  }
  std::array<ComPtr<ID3D11Buffer>, 2> buffers;
  for (size_t slot = 0; slot < buffers.size(); ++slot) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = (slot == 0 ? 27u : 130u) * 16,
                                    .Usage = D3D11_USAGE_DEFAULT,
                                    .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = source.data()};
    if (FAILED(device->CreateBuffer(&desc, &initial, buffers[slot].GetAddressOf()))) return 2;
    auto* buffer = buffers[slot].Get();
    context->CSSetConstantBuffers(static_cast<UINT>(slot), 1, &buffer);
    context->PSSetConstantBuffers(static_cast<UINT>(slot), 1, &buffer);
  }
  const std::array<float, 4> exposure_value = {.5f, 1.f, 2.f, 3.f};
  const D3D11_TEXTURE2D_DESC exposure_desc = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA exposure_initial = {.pSysMem = exposure_value.data(), .SysMemPitch = 16};
  ComPtr<ID3D11Texture2D> exposure;
  ComPtr<ID3D11ShaderResourceView> exposure_view;
  if (FAILED(device->CreateTexture2D(&exposure_desc, &exposure_initial, exposure.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(exposure.Get(), nullptr, exposure_view.GetAddressOf()))) return 3;
  auto* exposure_pointer = exposure_view.Get();
  context->CSSetShaderResources(0, 1, &exposure_pointer);
  context->PSSetShaderResources(0, 1, &exposure_pointer);
  capture::Reset();
  if (!capture::QueueCapture() || capture::QueueCapture()) return 4;
  for (const auto& target : capture::TARGETS) {
    if (target.is_compute) {
      TestCommand<renodx::utils::command_action::DispatchArguments> command{
          .cmd_list = &test_list, .arguments = {.group_count_x = 60, .group_count_y = 34, .group_count_z = 1}, .matched_shader_hash = target.hash};
      if (capture::OnCommand(command).bypass) return 5;
      capture::OnCommand(command);
    } else {
      TestCommand<renodx::utils::command_action::DrawArguments> command{
          .cmd_list = &test_list, .matched_shader_hash = target.hash};
      if (capture::OnCommand(command).bypass) return 6;
      capture::OnCommand(command);
    }
  }
  if (capture::state.pending_count != capture::MAX_PENDING || capture::state.pending.size() != capture::MAX_PENDING) return 7;
  // UE can reuse its source CB after the observed command. This snapshot must
  // retain the command's bytes, not that subsequent upload.
  auto changed_source = source;
  changed_source[26][0] = 999.f;
  context->UpdateSubresource(buffers[0].Get(), 0, nullptr, changed_source.data(), 0, 0);
  for (auto& sample : capture::state.pending) {
    for (size_t slot = 0; slot < sample.buffers.size(); ++slot) {
      const auto& readback = sample.buffers[slot];
      if (!readback.staging) continue;
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(context->Map(readback.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 8;
      const bool matches = std::memcmp(mapped.pData, source.data(), readback.vectors * 16) == 0;
      context->Unmap(readback.staging.Get(), 0);
      if (!matches) return 9;
    }
    if (sample.exposure) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(context->Map(sample.exposure.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 10;
      const bool matches = std::memcmp(mapped.pData, exposure_value.data(), sizeof(exposure_value)) == 0;
      context->Unmap(sample.exposure.Get(), 0);
      if (!matches) return 11;
    }
  }
  {
    const std::unique_lock lock(capture::state.mutex);
    ++capture::state.presents;
    for (auto& progress : capture::state.progress) {
      progress.next_sample = capture::Clock::time_point{};
    }
  }
  TestCommand<renodx::utils::command_action::DispatchArguments> saturated_command{
      .cmd_list = &test_list, .matched_shader_hash = capture::TARGETS[0].hash};
  capture::OnCommand(saturated_command);
  if (capture::state.pending_count != capture::MAX_PENDING || capture::state.progress[0].samples != 1) return 12;
  const auto generation = capture::state.generation;
  capture::Reset();
  if (!capture::state.pending.empty() || capture::state.pending_count != 0 || capture::state.generation <= generation
      || capture::state.active || !capture::state.finished || capture::state.progress[0].samples != 0) return 13;
  if (!capture::QueueCapture()) return 14;
  capture::Reset();
  // A fresh generation per target covers hashes beyond the bounded queue,
  // rather than merely calling their callbacks after all eight slots are full.
  for (size_t target_index = 0; target_index < capture::TARGETS.size(); ++target_index) {
    const auto& target = capture::TARGETS[target_index];
    context->UpdateSubresource(buffers[0].Get(), 0, nullptr, source.data(), 0, 0);
    if (!capture::QueueCapture()) return 15;
    if (target.is_compute) {
      TestCommand<renodx::utils::command_action::DispatchArguments> command{
          .cmd_list = &test_list, .arguments = {.group_count_x = 60, .group_count_y = 34, .group_count_z = 1}, .matched_shader_hash = target.hash};
      if (capture::OnCommand(command).bypass) return 16;
    } else {
      TestCommand<renodx::utils::command_action::DrawArguments> command{
          .cmd_list = &test_list, .matched_shader_hash = target.hash};
      if (capture::OnCommand(command).bypass) return 17;
    }
    if (capture::state.pending_count != 1 || capture::state.pending.size() != 1
        || capture::state.progress[target_index].samples != 1
        || capture::state.pending[0].target != target_index) return 18;
    context->UpdateSubresource(buffers[0].Get(), 0, nullptr, changed_source.data(), 0, 0);
    const auto& sample = capture::state.pending[0];
    const bool spatial = target.hash == 0x57192B01u;
    uint32_t buffer_count = 0;
    for (const auto& readback : sample.buffers) {
      if (!readback.staging) continue;
      ++buffer_count;
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(context->Map(readback.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 20;
      const bool matches = std::memcmp(mapped.pData, source.data(), readback.vectors * 16) == 0;
      context->Unmap(readback.staging.Get(), 0);
      if (!matches) return 21;
    }
    if (buffer_count != (spatial ? 1u : 2u)) return 19;
    if (spatial) {
      if (sample.exposure) return 22;
    } else {
      if (!sample.exposure) return 22;
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(context->Map(sample.exposure.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 23;
      const bool matches = std::memcmp(mapped.pData, exposure_value.data(), sizeof(exposure_value)) == 0;
      context->Unmap(sample.exposure.Get(), 0);
      if (!matches) return 24;
    }
    const auto target_generation = capture::state.generation;
    capture::Reset();
    if (!capture::state.pending.empty() || capture::state.pending_count != 0
        || capture::state.generation <= target_generation || capture::state.active
        || !capture::state.finished || capture::state.progress[target_index].samples != 0) return 25;
  }
  std::cout << "PASS: all " << capture::TARGETS.size()
            << " stock targets individually queued; CS/PS CB/exposure snapshots before reuse; duplicate-frame suppression; "
               "eight-slot queue pressure; reset/requeue ownership.\n";
}
