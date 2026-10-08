/*
 * Copyright (C) 2026 RenoDX contributors
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#if defined(__clang__)
#pragma clang diagnostic push
// The readback transitively includes unrelated private DirectX entry points.
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "../constant_readback.hpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace {

using Microsoft::WRL::ComPtr;

void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
}

void Expect(bool condition, const std::string& label) {
  if (!condition) throw std::runtime_error(label);
}

}  // namespace

int main() {
  try {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                            device.GetAddressOf(), nullptr, immediate.GetAddressOf()));
    ComPtr<ID3D11DeviceContext1> immediate1;
    Check(immediate.As(&immediate1));
    const auto pattern = [](size_t bytes, uint32_t seed) {
      std::vector<std::byte> result(bytes);
      for (size_t index = 0; index < bytes; ++index) {
        result[index] = static_cast<std::byte>((seed + index * 13) % 251);
      }
      return result;
    };
    const auto create_buffer = [](ID3D11Device* owner, uint32_t bytes, D3D11_USAGE usage, UINT bindings,
                                  const std::vector<std::byte>& contents) {
      const D3D11_BUFFER_DESC desc = {
          .ByteWidth = bytes,
          .Usage = usage,
          .BindFlags = bindings,
          .CPUAccessFlags = (usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0u),
          .MiscFlags = 0,
          .StructureByteStride = 0,
      };
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = contents.data(), .SysMemPitch = 0, .SysMemSlicePitch = 0};
      ComPtr<ID3D11Buffer> buffer;
      Check(owner->CreateBuffer(&desc, contents.empty() ? nullptr : &data, buffer.GetAddressOf()));
      return buffer;
    };
    const auto upload = [](ID3D11DeviceContext* context, ID3D11Buffer* buffer,
                           const std::vector<std::byte>& contents) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      Check(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
      std::memcpy(mapped.pData, contents.data(), contents.size());
      context->Unmap(buffer, 0);
    };

    std::array<std::vector<std::byte>, 2> executed = {pattern(2048, 1), pattern(256, 2)};
    std::array<ComPtr<ID3D11Buffer>, 2> buffers = {
        create_buffer(device.Get(), 2048, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, {}),
        create_buffer(device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, {}),
    };
    for (uint32_t slot = 0; slot < buffers.size(); ++slot) upload(immediate.Get(), buffers[slot].Get(), executed[slot]);
    std::array<uint32_t, 2> first_constants = {16, 0};
    std::array<uint32_t, 2> constant_counts = {16, 16};
    std::array<std::vector<std::byte>, 2> output = {std::vector<std::byte>(128), std::vector<std::byte>(64)};
    const std::array<std::span<std::byte>, 2> destinations = {output[0], output[1]};
    expeditionsrome::dlss::ConstantReadback readback;

    constexpr char SHADER[] =
        "cbuffer Constants : register(b0) { uint4 value; };"
        "RWBuffer<uint4> output : register(u0);"
        "[numthreads(1, 1, 1)] void main() { output[0] = value; }";
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    Check(D3DCompile(SHADER, sizeof(SHADER) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0,
                     code.GetAddressOf(), errors.GetAddressOf()));
    ComPtr<ID3D11ComputeShader> shader;
    Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
    const auto shader_output = create_buffer(device.Get(), 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, {});
    ComPtr<ID3D11UnorderedAccessView> uav;
    const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
        .Format = DXGI_FORMAT_R32G32B32A32_UINT,
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = 1, .Flags = 0},
    };
    Check(device->CreateUnorderedAccessView(shader_output.Get(), &uav_desc, uav.GetAddressOf()));
    const auto srv_buffer = create_buffer(device.Get(), 16, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, pattern(16, 3));
    ComPtr<ID3D11ShaderResourceView> srv;
    const D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
        .Format = DXGI_FORMAT_R32G32B32A32_UINT,
        .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = 1},
    };
    Check(device->CreateShaderResourceView(srv_buffer.Get(), &srv_desc, srv.GetAddressOf()));
    const auto bind_state = [&]() {
      immediate->CSSetShader(shader.Get(), nullptr, 0);
      ID3D11UnorderedAccessView* bound_uav = uav.Get();
      ID3D11ShaderResourceView* bound_srv = srv.Get();
      immediate->CSSetUnorderedAccessViews(0, 1, &bound_uav, nullptr);
      immediate->CSSetShaderResources(3, 1, &bound_srv);
      for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
        ID3D11Buffer* buffer = buffers[slot].Get();
        immediate1->CSSetConstantBuffers1(slot, 1, &buffer, &first_constants[slot], &constant_counts[slot]);
      }
    };
    const auto expect_state = [&]() {
      ComPtr<ID3D11ComputeShader> bound_shader;
      immediate->CSGetShader(bound_shader.GetAddressOf(), nullptr, nullptr);
      Expect(bound_shader.Get() == shader.Get(), "readback changed compute shader");
      ComPtr<ID3D11UnorderedAccessView> bound_uav;
      immediate->CSGetUnorderedAccessViews(0, 1, bound_uav.GetAddressOf());
      Expect(bound_uav.Get() == uav.Get(), "readback changed compute UAV");
      ComPtr<ID3D11ShaderResourceView> bound_srv;
      immediate->CSGetShaderResources(3, 1, bound_srv.GetAddressOf());
      Expect(bound_srv.Get() == srv.Get(), "readback changed compute SRV");
      for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
        ComPtr<ID3D11Buffer> bound_buffer;
        UINT first = 0;
        UINT count = 0;
        immediate1->CSGetConstantBuffers1(slot, 1, bound_buffer.GetAddressOf(), &first, &count);
        Expect(bound_buffer.Get() == buffers[slot].Get() && first == first_constants[slot] && count == constant_counts[slot],
               "ranged constant binding mismatch at slot " + std::to_string(slot)
                   + ": expected first/count=" + std::to_string(first_constants[slot]) + "/" + std::to_string(constant_counts[slot])
                   + ", actual=" + std::to_string(first) + "/" + std::to_string(count));
      }
    };
    const auto expect_read = [&](const std::string& label) {
      Expect(readback.Read(immediate.Get(), buffers, first_constants, constant_counts, destinations), label + ": read failed");
      for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
        const size_t offset = static_cast<size_t>(first_constants[slot]) * 16;
        Expect(std::equal(output[slot].begin(), output[slot].end(), executed[slot].begin() + offset),
               label + ": wrong GPU bytes at slot " + std::to_string(slot));
      }
      expect_state();
    };

    bind_state();
    expect_state();
    expect_read("initial immediate upload and subrange");
    const auto initial_staging = readback.staging;
    expect_read("same CB widths reuse staging");
    Expect(readback.staging == initial_staging, "same widths recreated staging buffers");

    ComPtr<ID3D11DeviceContext> deferred_a;
    ComPtr<ID3D11DeviceContext> deferred_b;
    Check(device->CreateDeferredContext(0, deferred_a.GetAddressOf()));
    Check(device->CreateDeferredContext(0, deferred_b.GetAddressOf()));
    const std::array<std::vector<std::byte>, 2> recorded_a = {pattern(2048, 31), pattern(256, 32)};
    const std::array<std::vector<std::byte>, 2> recorded_b = {pattern(2048, 61), pattern(256, 62)};
    for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
      upload(deferred_a.Get(), buffers[slot].Get(), recorded_a[slot]);
      ID3D11Buffer* buffer = buffers[slot].Get();
      deferred_a->CSSetConstantBuffers(slot, 1, &buffer);
    }
    ComPtr<ID3D11CommandList> list_a;
    Check(deferred_a->FinishCommandList(FALSE, list_a.GetAddressOf()));
    expect_read("recording/finishing A does not execute deferred uploads");
    for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
      upload(deferred_b.Get(), buffers[slot].Get(), recorded_b[slot]);
      ID3D11Buffer* buffer = buffers[slot].Get();
      deferred_b->CSSetConstantBuffers(slot, 1, &buffer);
    }
    ComPtr<ID3D11CommandList> list_b;
    Check(deferred_b->FinishCommandList(FALSE, list_b.GetAddressOf()));
    expect_read("newest CPU-recorded B is not the executed GPU version");
    const auto before_rejected_read = output;
    Expect(!readback.Read(deferred_a.Get(), buffers, first_constants, constant_counts, destinations), "accepted deferred read");
    Expect(output == before_rejected_read, "rejected deferred read wrote output");
    expect_read("rejecting deferred read did not execute its work");

    immediate->ExecuteCommandList(list_a.Get(), TRUE);
    executed = recorded_a;
    expect_read("executed A wins over newer CPU-recorded B");
    immediate->ExecuteCommandList(list_b.Get(), TRUE);
    executed = recorded_b;
    expect_read("executed B becomes latest GPU version");

    for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
      const uint32_t bytes = static_cast<uint32_t>(executed[slot].size());
      executed[slot] = pattern(bytes, 91 + slot);
      const auto donor = create_buffer(device.Get(), bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, executed[slot]);
      immediate->CopyResource(buffers[slot].Get(), donor.Get());
    }
    expect_read("GPU copy overwrites the last mapped CPU upload on the same resource");

    // A device-side copy does not update a map/upload cache. Readback must still see the copied bytes.
    for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
      const uint32_t bytes = static_cast<uint32_t>(executed[slot].size());
      buffers[slot] = create_buffer(device.Get(), bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, executed[slot]);
      executed[slot] = pattern(bytes, 101 + slot);
      const auto donor = create_buffer(device.Get(), bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, executed[slot]);
      immediate->CopyResource(buffers[slot].Get(), donor.Get());
    }
    bind_state();
    expect_read("GPU copy invalidates the old CPU contents");
    Expect(readback.staging == initial_staging, "same-width replacement resource recreated staging");

    const auto expect_rejected = [&](ID3D11DeviceContext* context,
                                     const std::array<ComPtr<ID3D11Buffer>, 2>& sources,
                                     const std::array<uint32_t, 2>& first,
                                     const std::array<uint32_t, 2>& counts,
                                     const std::array<std::span<std::byte>, 2>& target,
                                     const std::string& label) {
      const auto previous_output = output;
      Expect(!readback.Read(context, sources, first, counts, target), label + ": unexpectedly accepted");
      Expect(output == previous_output, label + ": modified destination on rejected read");
      expect_state();
    };
    expect_rejected(nullptr, buffers, first_constants, constant_counts, destinations, "null context");
    expect_rejected(immediate.Get(), {buffers[0], {}}, first_constants, constant_counts, destinations, "null buffer");
    expect_rejected(immediate.Get(), buffers, first_constants, constant_counts, {destinations[0], {}}, "empty destination");
    expect_rejected(immediate.Get(), buffers, {129, 0}, constant_counts, destinations, "offset past buffer");
    expect_rejected(immediate.Get(), buffers, {128, 0}, constant_counts, destinations, "offset at end with nonempty destination");
    expect_rejected(immediate.Get(), buffers, {127, 0}, constant_counts, destinations, "destination exceeds physical buffer");
    expect_rejected(immediate.Get(), buffers, {(std::numeric_limits<uint32_t>::max)(), 0}, constant_counts, destinations,
                    "offset overflow must not wrap");
    expect_rejected(immediate.Get(), buffers, first_constants, {7, 16}, destinations, "destination exceeds bound constant count");
    expect_rejected(immediate.Get(), buffers, first_constants, {0, 16}, destinations, "zero constant count");
    expect_rejected(immediate.Get(), {srv_buffer, buffers[1]}, {0, 0}, constant_counts, destinations, "nonconstant buffer");

    buffers = {create_buffer(device.Get(), 4096, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, pattern(4096, 151)),
               create_buffer(device.Get(), 512, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, pattern(512, 152))};
    executed = {pattern(4096, 151), pattern(512, 152)};
    first_constants = {192, 16};
    constant_counts = {16, 16};
    bind_state();
    expect_read("resized buffers and high ranged offsets");
    Expect(readback.staging[0] != initial_staging[0] && readback.staging[1] != initial_staging[1], "resize did not replace staging");
    Expect(readback.byte_widths == std::array<uint32_t, 2>{4096, 512}, "cached widths do not match resized sources");

    ComPtr<ID3D11Device> second_device;
    ComPtr<ID3D11DeviceContext> second_context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                            second_device.GetAddressOf(), nullptr, second_context.GetAddressOf()));
    const auto foreign = create_buffer(second_device.Get(), 2048, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, pattern(2048, 171));
    expect_rejected(immediate.Get(), {foreign, buffers[1]}, {0, 0}, constant_counts, destinations, "foreign-device source");
    const std::array<std::vector<std::byte>, 2> second_data = {pattern(256, 201), pattern(256, 202)};
    const std::array<ComPtr<ID3D11Buffer>, 2> second_buffers = {
        create_buffer(second_device.Get(), 256, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, second_data[0]),
        create_buffer(second_device.Get(), 256, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, second_data[1]),
    };
    Expect(readback.Read(second_context.Get(), second_buffers, {0, 0}, {16, 16}, destinations), "device change read failed");
    for (uint32_t slot = 0; slot < second_buffers.size(); ++slot) {
      Expect(std::equal(output[slot].begin(), output[slot].end(), second_data[slot].begin()), "device change returned stale bytes");
    }
    Expect(readback.device.Get() == second_device.Get(), "readback device did not change");
    immediate->ClearState();
    second_context->ClearState();
    std::cout << "PASS: D3D11 WARP constant readback; real deferred Map/Unmap/Finish/Execute ordering, "
                 "no premature execution, shared-resource CPU/GPU version separation, ranged offsets/counts, "
                 "bounds and deferred rejection, staging reuse/resize, GPU-copy freshness, CS-state preservation, "
                 "foreign-device rejection and device migration.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
