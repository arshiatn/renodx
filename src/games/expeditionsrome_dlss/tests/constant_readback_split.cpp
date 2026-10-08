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
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "../constant_readback.hpp"

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
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                            device.GetAddressOf(), nullptr, context.GetAddressOf()));
    ComPtr<ID3D11DeviceContext1> context1;
    Check(context.As(&context1));
    const auto pattern = [](uint32_t seed) {
      std::vector<std::byte> result(4096);
      for (size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>((seed + index * 13) % 251);
      }
      return result;
    };
    const auto create_buffer = [](ID3D11Device* owner, const std::vector<std::byte>& contents,
                                  UINT bindings, D3D11_USAGE usage = D3D11_USAGE_DEFAULT) {
      const D3D11_BUFFER_DESC desc = {
          .ByteWidth = static_cast<UINT>(contents.size()),
          .Usage = usage,
          .BindFlags = bindings,
          .CPUAccessFlags = (usage == D3D11_USAGE_STAGING ? D3D11_CPU_ACCESS_READ : 0u),
      };
      const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = contents.data()};
      ComPtr<ID3D11Buffer> result;
      Check(owner->CreateBuffer(&desc, &initial, result.GetAddressOf()));
      return result;
    };
    std::array<std::vector<std::byte>, 2> original = {pattern(1), pattern(2)};
    std::array<ComPtr<ID3D11Buffer>, 2> sources = {
        create_buffer(device.Get(), original[0], D3D11_BIND_CONSTANT_BUFFER),
        create_buffer(device.Get(), original[1], D3D11_BIND_CONSTANT_BUFFER),
    };
    const std::array<uint32_t, 2> first = {16, 32};
    const std::array<uint32_t, 2> counts = {32, 144};
    const std::array<size_t, 2> sizes = {27 * 16, 130 * 16};
    std::array<std::vector<std::byte>, 2> output = {
        std::vector<std::byte>(sizes[0]), std::vector<std::byte>(sizes[1]),
    };
    const std::array<std::span<std::byte>, 2> destinations = {output[0], output[1]};
    expeditionsrome::dlss::ConstantReadback readback;
    const auto expect_contents = [&](const std::array<std::vector<std::byte>, 2>& expected,
                                      const std::string& label) {
      for (uint32_t slot = 0; slot < output.size(); ++slot) {
        Expect(std::equal(output[slot].begin(), output[slot].end(), expected[slot].begin() + first[slot] * 16),
               label + ": wrong snapshot at b" + std::to_string(slot));
      }
    };
    Expect(!readback.Complete(context.Get(), destinations), "completion without queue accepted");
    Expect(readback.Queue(context.Get(), sources, first, counts, sizes), "initial queue failed: " + readback.error);

    constexpr char SHADER[] =
        "cbuffer First : register(b0) { uint4 first[256]; };"
        "cbuffer Second : register(b1) { uint4 second[256]; };"
        "RWBuffer<uint4> output : register(u0);"
        "[numthreads(1, 1, 1)] void main() { output[0] = first[0] ^ second[0]; }";
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    Check(D3DCompile(SHADER, sizeof(SHADER) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0,
                     code.GetAddressOf(), errors.GetAddressOf()));
    ComPtr<ID3D11ComputeShader> shader;
    Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
    const auto stock_output = create_buffer(device.Get(), std::vector<std::byte>(16), D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11UnorderedAccessView> stock_uav;
    const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
        .Format = DXGI_FORMAT_R32G32B32A32_UINT,
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = 1, .Flags = 0},
    };
    Check(device->CreateUnorderedAccessView(stock_output.Get(), &uav_desc, stock_uav.GetAddressOf()));
    auto* uav = stock_uav.Get();
    context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    context->CSSetShader(shader.Get(), nullptr, 0);
    for (uint32_t slot = 0; slot < sources.size(); ++slot) {
      auto* buffer = sources[slot].Get();
      context1->CSSetConstantBuffers1(slot, 1, &buffer, &first[slot], &counts[slot]);
    }
    context->Dispatch(1, 1, 1);

    // Both changes occur after the snapshots and after stock-like GPU work.
    // Completion must not return the new contents of the same buffer objects.
    std::array<std::vector<std::byte>, 2> latest = {pattern(31), pattern(32)};
    context->UpdateSubresource(sources[0].Get(), 0, nullptr, latest[0].data(), 0, 0);
    const auto donor = create_buffer(device.Get(), latest[1], D3D11_BIND_CONSTANT_BUFFER);
    context->CopyResource(sources[1].Get(), donor.Get());
    Expect(readback.Complete(context.Get(), destinations), "completion after intervening GPU work failed: " + readback.error);
    expect_contents(original, "queued version survives UpdateSubresource and CopyResource");
    Expect(!readback.pending_context && readback.pending_sizes == std::array<size_t, 2>{}, "completion kept pending metadata");
    Expect(!readback.Complete(context.Get(), destinations), "same snapshot completed twice");

    ComPtr<ID3D11ComputeShader> bound_shader;
    context->CSGetShader(bound_shader.GetAddressOf(), nullptr, nullptr);
    Expect(bound_shader.Get() == shader.Get(), "completion changed the stock compute shader");
    ComPtr<ID3D11UnorderedAccessView> bound_uav;
    context->CSGetUnorderedAccessViews(0, 1, bound_uav.GetAddressOf());
    Expect(bound_uav.Get() == stock_uav.Get(), "completion changed the stock UAV");
    for (uint32_t slot = 0; slot < sources.size(); ++slot) {
      ComPtr<ID3D11Buffer> bound;
      UINT bound_first = 0;
      UINT bound_count = 0;
      context1->CSGetConstantBuffers1(slot, 1, bound.GetAddressOf(), &bound_first, &bound_count);
      Expect(bound == sources[slot] && bound_first == first[slot] && bound_count == counts[slot],
             "completion changed stock constant bindings");
    }
    const auto stock_staging = create_buffer(device.Get(), std::vector<std::byte>(16), 0, D3D11_USAGE_STAGING);
    context->CopyResource(stock_staging.Get(), stock_output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Check(context->Map(stock_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    std::array<std::byte, 16> stock_result = {};
    std::memcpy(stock_result.data(), mapped.pData, stock_result.size());
    context->Unmap(stock_staging.Get(), 0);
    for (size_t index = 0; index < stock_result.size(); ++index) {
      Expect(stock_result[index] == (original[0][first[0] * 16 + index] ^ original[1][first[1] * 16 + index]),
             "stock dispatch did not use the queued version");
    }
    Expect(readback.Read(context.Get(), sources, first, counts, destinations), "Read wrapper failed");
    expect_contents(latest, "next read observes subsequent GPU updates");

    ComPtr<ID3D11DeviceContext> deferred;
    Check(device->CreateDeferredContext(0, deferred.GetAddressOf()));
    const std::array<std::vector<std::byte>, 2> recorded = {pattern(61), pattern(62)};
    for (uint32_t slot = 0; slot < sources.size(); ++slot) {
      deferred->UpdateSubresource(sources[slot].Get(), 0, nullptr, recorded[slot].data(), 0, 0);
    }
    ComPtr<ID3D11CommandList> list;
    Check(deferred->FinishCommandList(FALSE, list.GetAddressOf()));
    Expect(readback.Queue(context.Get(), sources, first, counts, sizes), "queue before deferred execution failed");
    context->ExecuteCommandList(list.Get(), TRUE);
    Expect(readback.Complete(context.Get(), destinations), "completion after deferred execution failed");
    expect_contents(latest, "queued version survives later deferred execution");
    Expect(readback.Read(context.Get(), sources, first, counts, destinations), "new deferred version read failed");
    expect_contents(recorded, "next read observes executed deferred updates");

    const auto unchanged = output;
    Expect(readback.Queue(context.Get(), sources, first, counts, sizes), "queue for double-queue rejection failed");
    Expect(!readback.Queue(context.Get(), sources, first, counts, sizes), "double queue accepted");
    Expect(!readback.pending_context, "double queue left previous snapshot pending");
    Expect(!readback.Complete(context.Get(), destinations), "double-queue cancellation exposed stale snapshot");
    Expect(output == unchanged, "double queue or completion error wrote destinations");
    Expect(readback.Queue(context.Get(), sources, first, counts, sizes), "requeue after cancellation failed");
    Expect(!readback.Complete(context.Get(), {destinations[0].first(16), destinations[1]}), "completion size mismatch accepted");
    Expect(!readback.pending_context && output == unchanged, "size mismatch kept pending or modified output");
    Expect(readback.Queue(context.Get(), sources, first, counts, sizes), "queue before null completion failed");
    Expect(!readback.Complete(nullptr, destinations), "null completion context accepted");
    Expect(!readback.pending_context && output == unchanged, "null completion kept pending or modified output");
    Expect(!readback.Queue(deferred.Get(), sources, first, counts, sizes), "deferred queue accepted");
    Expect(!readback.Queue(context.Get(), sources, first, {0, counts[1]}, sizes), "unbound range accepted");
    Expect(!readback.Queue(context.Get(), {sources[0], {}}, first, counts, sizes), "null source accepted");
    Expect(!readback.Queue(context.Get(), sources, first, counts, {0, sizes[1]}), "empty destination accepted");
    Expect(readback.Queue(context.Get(), sources, first, counts, sizes), "queue before explicit cancellation failed");
    readback.Cancel();
    Expect(!readback.Complete(context.Get(), destinations), "explicit cancellation exposed stale snapshot");
    Expect(readback.Read(context.Get(), sources, first, counts, destinations), "Read after explicit cancellation failed");
    expect_contents(recorded, "cancelled staging is overwritten by a fresh queue");

    ComPtr<ID3D11Device> other_device;
    ComPtr<ID3D11DeviceContext> other_context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                            other_device.GetAddressOf(), nullptr, other_context.GetAddressOf()));
    Expect(readback.Queue(context.Get(), sources, first, counts, sizes), "queue before foreign completion failed");
    Expect(!readback.Complete(other_context.Get(), destinations), "foreign completion context accepted");
    Expect(!readback.pending_context, "foreign completion kept pending snapshot");
    Expect(readback.Read(context.Get(), sources, first, counts, destinations), "Read after foreign completion failed");
    expect_contents(recorded, "foreign completion does not poison later snapshots");
    context->ClearState();
    std::cout << "PASS: split constant readback; current-dispatch snapshots survive stock GPU work, "
                 "same-buffer updates/copies and later deferred execution; ranged offsets, exact sizes, "
                 "CS state, cancellation, double queue/completion and foreign/deferred rejection.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
