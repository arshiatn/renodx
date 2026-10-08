/*
 * Copyright (C) 2026 RenoDX contributors
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "../performance.hpp"

namespace {

using expeditionsrome::dlss::Performance;
using expeditionsrome::dlss::PerformanceStatistics;
using Microsoft::WRL::ComPtr;

}  // namespace

int main() {
  try {
    const auto check = [](HRESULT result) {
      if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
    };
    const auto expect = [](bool condition, const std::string& label) {
      if (!condition) throw std::runtime_error(label);
    };
    PerformanceStatistics statistics;
    expect(statistics.GpuSample(1000, {0, 1, 10, 12, 20, 23, 30, 34}, false), "first deterministic GPU sample");
    expect(statistics.GpuSample(1000, {10, 13, 30, 35, 50, 57, 70, 79}, false), "second deterministic GPU sample");
    expect(statistics.summary.gpu_samples == 2
               && statistics.summary.gpu_ms == std::array<double, 4>{2, 3.5, 5, 6.5},
           "GPU phase means/milliseconds exclude unmeasured interphase gaps");
    expect(!statistics.GpuSample(0, {0, 1, 2, 3, 4, 5, 6, 7}, false), "zero-frequency rejection");
    expect(!statistics.GpuSample(1000, {0, 1, 2, 3, 4, 5, 6, 7}, true), "disjoint rejection");
    expect(!statistics.GpuSample(1000, {0, 2, 1, 3, 4, 5, 6, 7}, false), "nonmonotonic timestamp rejection");
    expect(statistics.summary.gpu_samples == 2 && statistics.summary.rejected == 3,
           "malformed samples cannot contaminate GPU means");
    expect(statistics.GpuSample(1000, {5, 5, 5, 5, 5, 5, 5, 5}, false), "equal-resolution timestamps accepted");
    expect(statistics.CpuSample(4, 2) && statistics.CpuSample(8, 4), "deterministic CPU samples");
    expect(statistics.summary.cpu_samples == 2 && statistics.summary.cpu_readback_ms == 6
               && statistics.summary.cpu_evaluate_ms == 3,
           "CPU wait and submission means separate from GPU means");
    expect(!statistics.CpuSample(std::numeric_limits<double>::quiet_NaN(), 1)
               && !statistics.CpuSample(1, std::numeric_limits<double>::infinity())
               && !statistics.CpuSample(-1, 1),
           "invalid CPU sample rejection");
    expect(statistics.summary.cpu_samples == 2 && statistics.summary.cpu_readback_ms == 6,
           "malformed CPU samples cannot contaminate means");

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                            device.GetAddressOf(), nullptr, immediate.GetAddressOf()));
    ComPtr<ID3D11DeviceContext> deferred;
    check(device->CreateDeferredContext(0, deferred.GetAddressOf()));
    Performance performance;
    expect(!performance.Begin(nullptr) && !performance.Begin(deferred.Get()), "null/deferred captures rejected");
    performance.Mark(nullptr);
    performance.End(nullptr);
    performance.Poll(nullptr);
    performance.Poll(deferred.Get());
    expect(performance.Snapshot().pending == 0 && !performance.Snapshot().available,
           "invalid contexts cannot create a capture");

    constexpr char SHADER[] =
        "RWBuffer<uint> output : register(u0);"
        "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { output[id.x] = id.x; }";
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    check(D3DCompile(SHADER, sizeof(SHADER) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0,
                     code.GetAddressOf(), errors.GetAddressOf()));
    ComPtr<ID3D11ComputeShader> shader;
    check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
    const D3D11_BUFFER_DESC buffer_desc = {
        .ByteWidth = 256 * sizeof(uint32_t),
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
        .CPUAccessFlags = 0,
        .MiscFlags = 0,
        .StructureByteStride = 0,
    };
    ComPtr<ID3D11Buffer> buffer;
    check(device->CreateBuffer(&buffer_desc, nullptr, buffer.GetAddressOf()));
    const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
        .Format = DXGI_FORMAT_R32_UINT,
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = 256, .Flags = 0},
    };
    ComPtr<ID3D11UnorderedAccessView> uav;
    check(device->CreateUnorderedAccessView(buffer.Get(), &uav_desc, uav.GetAddressOf()));
    immediate->CSSetShader(shader.Get(), nullptr, 0);
    immediate->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);

    const auto capture = [&](Performance* target) {
      expect(target->Begin(immediate.Get()), "capture query creation/ring admission");
      for (uint32_t phase = 0; phase < 4; ++phase) {
        if (phase != 0) target->Mark(immediate.Get());
        immediate->Dispatch(4, 1, 1);
        if (phase < 3) target->Mark(immediate.Get());
      }
      target->End(immediate.Get());
      target->End(immediate.Get());
    };
    const auto drain = [&](Performance* target) {
      // Only the fixture submits queued WARP work. Production Poll never flushes
      // or waits; every invocation below still returns on not-ready query data.
      immediate->Flush();
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (target->Snapshot().pending != 0 && std::chrono::steady_clock::now() < deadline) {
        target->Poll(immediate.Get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      expect(target->Snapshot().pending == 0, "WARP query completion bounded to ten seconds");
    };
    capture(&performance);
    expect(performance.Snapshot().pending == 1 && performance.Snapshot().available, "real query enqueued");
    drain(&performance);
    expect(performance.Snapshot().gpu_samples == 1 && performance.Snapshot().rejected == 0,
           "real timestamp/disjoint sample accepted");
    for (const double duration : performance.Snapshot().gpu_ms) {
      expect(std::isfinite(duration) && duration >= 0, "GPU interval finite and nonnegative");
    }
    ComPtr<ID3D11ComputeShader> current_shader;
    ComPtr<ID3D11UnorderedAccessView> current_uav;
    immediate->CSGetShader(current_shader.GetAddressOf(), nullptr, nullptr);
    immediate->CSGetUnorderedAccessViews(0, 1, current_uav.GetAddressOf());
    expect(current_shader == shader && current_uav == uav, "profiling leaves CS bindings unchanged");

    const uint64_t prior_gpu_samples = performance.Snapshot().gpu_samples;
    for (uint32_t index = 0; index < 8; ++index) capture(&performance);
    expect(!performance.Begin(immediate.Get()), "full eight-slot ring drops instead of waiting");
    expect(performance.Snapshot().pending == 8 && performance.Snapshot().dropped == 1,
           "ring pressure preserves all pending captures");
    drain(&performance);
    expect(performance.Snapshot().gpu_samples == prior_gpu_samples + 8, "all ring samples retained");

    uint64_t rejected = performance.Snapshot().rejected;
    expect(performance.Begin(immediate.Get()), "partial capture begins");
    performance.Mark(immediate.Get());
    performance.End(immediate.Get());
    expect(performance.Snapshot().rejected == ++rejected, "partial capture rejected immediately");
    drain(&performance);
    expect(performance.Snapshot().gpu_samples == prior_gpu_samples + 8, "partial capture never averaged");
    expect(performance.Begin(immediate.Get()), "overmarked capture begins");
    for (uint32_t index = 0; index < 7; ++index) performance.Mark(immediate.Get());
    performance.End(immediate.Get());
    expect(performance.Snapshot().rejected == ++rejected, "extra phase mark rejection");
    drain(&performance);
    expect(performance.Begin(immediate.Get()), "wrong-context capture begins");
    performance.Mark(deferred.Get());
    performance.End(deferred.Get());
    expect(performance.Snapshot().rejected == ++rejected, "wrong context closes on owned context");
    drain(&performance);
    expect(performance.Begin(immediate.Get()), "abandoned capture begins");
    expect(performance.Begin(immediate.Get()), "new capture closes/rejects abandoned previous capture");
    performance.End(nullptr);
    rejected += 2;
    expect(performance.Snapshot().rejected == rejected, "abandoned/finalizer closures each rejected once");
    drain(&performance);

    expect(performance.Begin(immediate.Get()), "active capture before runtime move");
    performance.Mark(immediate.Get());
    Performance moved(std::move(performance));
    expect(performance.Snapshot().pending == 0 && moved.Snapshot().pending == 1,
           "moving profiler transfers active query ownership");
    for (uint32_t index = 0; index < 5; ++index) moved.Mark(immediate.Get());
    moved.End(immediate.Get());
    drain(&moved);
    expect(moved.Snapshot().gpu_samples == prior_gpu_samples + 9 && moved.Snapshot().rejected == rejected,
           "active capture survives runtime move without spurious rejection");
    Performance assigned;
    expect(assigned.Begin(immediate.Get()), "move-assignment closes destination partial capture");
    assigned = std::move(moved);
    capture(&assigned);
    drain(&assigned);
    expect(assigned.Snapshot().gpu_samples == prior_gpu_samples + 10, "move assignment retains measurements");
    assigned.CpuSample(5, 2);
    expect(assigned.Snapshot().cpu_readback_ms == 5 && assigned.Snapshot().cpu_evaluate_ms == 2,
           "runtime CPU sampling");

    ComPtr<ID3D11Device> other_device;
    ComPtr<ID3D11DeviceContext> other_context;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                            other_device.GetAddressOf(), nullptr, other_context.GetAddressOf()));
    const auto before_wrong_poll = assigned.Snapshot();
    assigned.Poll(other_context.Get());
    expect(assigned.Snapshot().gpu_samples == before_wrong_poll.gpu_samples, "foreign-device poll ignored");
    expect(assigned.Begin(other_context.Get()), "device change starts fresh query generation");
    expect(assigned.Snapshot().gpu_samples == 0 && assigned.Snapshot().cpu_samples == 0,
           "device change clears old timing statistics");
    assigned.End(nullptr);
    other_context->Flush();

    std::cout << "PASS: deterministic GPU/CPU averages; malformed/disjoint samples; WARP query execution; "
                 "eight-slot pressure; partial/overmarked/wrong-context/abandoned captures; active moves; "
                 "device changes; CS bindings unchanged. Production polls use DONOTFLUSH without waits.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
