// The reference build's samplers (reference suite step 1, G1 / M1 / M7: src/games/twattila/dlaa_helper/rt_reference.hlsli)
// on WARP, through samplers.hlsl: each against its own closed form, computed here in double precision.
// 1. RefRandom (pcg4d): uniform (mean, variance, 64 bins), no correlation between its dimensions, neighbouring texels or
//    consecutive samples.
// 2. RefConcentricDisk: the unit disk's moments from a 1024 x 1024 grid of u, equal areas (8 rings x 16 sectors: chi-square)
//    from 2^22 random u.
// 3. RefCosineDirection over six normals (the basis's z = -1 corner too): unit, above the surface, E[n.d] = 2/3,
//    E[d d^T] = (I + n n^T) / 4, the cube faces' cosine means (1 + n_a^2) / 8 +- n_a / 4 (rt_reference.hlsli's
//    RefCubeCosineMean, rt_path.hlsli SkyCalibration), cos^2 uniform.
// 4. RefBasis: orthonormal and right-handed over 200000 normals.
// 5. RefVisibleNormal with RefVisibleNormalWeight (Heitz 2018, G2 / G1) over GGX widths and views: E[G2 / G1] equals the
//    lobe's albedo integrated over D (in D sampling's variable, tan theta = alpha tan s, an 8192 x 2048 grid: the most
//    different measure), and the microfacet normals of random u fall in each band of xi = sin^2 s as the visible-normal
//    density says. CONTROL: the share of samples above the horizon (the helper's estimator before I5) must miss the
//    albedo for rough lobes.
// 6. RefStratified: the strata's centres; past the square's count, the jitter itself.
// tests.exe <repository> <folder of dxcompiler.dll and dxil.dll> [<dump folder>]: the dump folder gets each sampler's inputs
// and outputs (disk.bin, cosine.bin, vndf.bin: float32 records) for tools/tw/reference/xcheck.
#include <windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr double PI = 3.14159265358979323846;

struct TestInput {
  std::array<float, 4> u = {};
  std::array<float, 4> frame = {};
};

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

double Dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Normalize(const Vec3& v) {
  const double length = std::sqrt(Dot(v, v));
  return {v.x / length, v.y / length, v.z / length};
}

// Smith's Lambda for GGX of width alpha (the same closed form as RefSmithLambda, in double).
double Lambda(double cosine, double alpha) {
  const double c2 = std::max(cosine * cosine, 1e-12);
  return 0.5 * (-1.0 + std::sqrt(1.0 + alpha * alpha * (1.0 - c2) / c2));
}

// Integrals over the GGX lobe of width alpha seen from `view` by the midpoint rule in (s, phi), tan theta = alpha tan s
// (D sampling's own variable: xi = sin^2 s, its density D (n.h) uniform in (xi, phi); the grid's density follows the
// lobe at every width): the albedo, the integral of D G2 (v.h) / (n.v) where the reflected direction is above the surface
// (E[G2 / G1] under visible normals), and the visible-normal density's mass G1 (v.h)+ D / (n.v) in 8 bands of xi.
struct LobeIntegrals {
  double albedo = 0.0;
  std::array<double, 8> bands = {};
};
LobeIntegrals IntegrateLobe(const Vec3& view, double alpha) {
  constexpr int S_STEPS = 8192;
  constexpr int PHI_STEPS = 2048;
  LobeIntegrals integrals;
  const double lambda_view = Lambda(view.z, alpha);
  const double cell = (PI / 2.0 / S_STEPS) * (2.0 * PI / PHI_STEPS);
  for (int i = 0; i < S_STEPS; ++i) {
    const double s = (i + 0.5) / S_STEPS * (PI / 2.0);
    const double t = std::tan(s);
    const double cos_theta = 1.0 / std::sqrt(1.0 + alpha * alpha * t * t);
    const double sin_theta = alpha * t * cos_theta;
    // D sin(theta) d(theta) / ds: D = 1 / (pi alpha^2 cos^4 (1 + t^2)^2), d(theta) / ds = alpha (1 + t^2) / (1 + alpha^2 t^2).
    const double d_term = 1.0 / (PI * alpha * alpha * std::pow(cos_theta, 4.0) * (1.0 + t * t) * (1.0 + t * t)) * sin_theta
                          * (alpha * (1.0 + t * t) / (1.0 + alpha * alpha * t * t)) * cell;
    const int band = std::min(static_cast<int>(std::sin(s) * std::sin(s) * 8.0), 7);
    for (int j = 0; j < PHI_STEPS; ++j) {
      const double phi = 2.0 * PI * (j + 0.5) / PHI_STEPS;
      const Vec3 half_vector = {sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
      const double view_half = Dot(view, half_vector);
      if (view_half <= 0.0) continue;
      const double weight = d_term * view_half / view.z;
      integrals.bands[band] += weight / (1.0 + lambda_view);
      const double light_z = 2.0 * view_half * cos_theta - view.z;
      if (light_z <= 0.0) continue;
      integrals.albedo += weight / (1.0 + lambda_view + Lambda(light_z, alpha));
    }
  }
  return integrals;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: tests.exe <repository> <dxc folder> [<dump folder>]\n");
    return 2;
  }
  const std::string repository = argv[1];
  const std::string dxc_folder = argv[2];
  const std::string dump_folder = (argc > 3) ? argv[3] : "";
#define CHECK(x)                                       \
  do {                                                 \
    if (!(x)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #x); \
      return 1;                                        \
    }                                                  \
  } while (false)

  // The compiler: dxil.dll first, so dxcompiler.dll signs with the copy beside it.
  CHECK(LoadLibraryA((dxc_folder + "\\dxil.dll").c_str()) != nullptr);
  const HMODULE dxc_module = LoadLibraryA((dxc_folder + "\\dxcompiler.dll").c_str());
  CHECK(dxc_module != nullptr);
  const auto dxc_create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxc_module, "DxcCreateInstance"));
  CHECK(dxc_create != nullptr);
  ComPtr<IDxcUtils> utils;
  ComPtr<IDxcCompiler3> compiler;
  CHECK(SUCCEEDED(dxc_create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))));
  CHECK(SUCCEEDED(dxc_create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))));
  ComPtr<IDxcIncludeHandler> includes;
  CHECK(SUCCEEDED(utils->CreateDefaultIncludeHandler(&includes)));
  const std::wstring wide_repository(repository.begin(), repository.end());
  const std::wstring path = wide_repository + L"\\test\\tw_rt_reference\\samplers.hlsl";
  const std::wstring helper_folder = wide_repository + L"\\src\\games\\twattila\\dlaa_helper";
  ComPtr<IDxcBlobEncoding> source;
  CHECK(SUCCEEDED(utils->LoadFile(path.c_str(), nullptr, &source)));
  std::vector<LPCWSTR> arguments = {path.c_str(), L"-T", L"cs_6_5", L"-E", L"main", L"-O3", L"-I", helper_folder.c_str()};
  const DxcBuffer source_buffer = {.Ptr = source->GetBufferPointer(), .Size = source->GetBufferSize(), .Encoding = DXC_CP_ACP};
  ComPtr<IDxcResult> result;
  CHECK(SUCCEEDED(compiler->Compile(&source_buffer, arguments.data(), static_cast<UINT32>(arguments.size()), includes.Get(), IID_PPV_ARGS(&result))));
  HRESULT status = E_FAIL;
  result->GetStatus(&status);
  if (FAILED(status)) {
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    std::printf("compile failed: %s\n", (errors != nullptr) ? errors->GetStringPointer() : "");
    return 1;
  }
  ComPtr<IDxcBlob> object;
  result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);

  ComPtr<IDXGIFactory4> factory;
  CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
  ComPtr<IDXGIAdapter> warp;
  CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
  ComPtr<ID3D12Device> device;
  CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device))));
  ComPtr<ID3D12CommandQueue> queue;
  const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
  ComPtr<ID3D12CommandAllocator> allocator;
  CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
  ComPtr<ID3D12GraphicsCommandList> list;
  CHECK(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
  ComPtr<ID3D12Fence> fence;
  CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
  uint64_t fence_value = 0;

  // b1 the Reference cbuffer's eight words, t100 the inputs, u100 the outputs.
  const std::array<D3D12_ROOT_PARAMETER, 3> parameters = {{
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 1, .Num32BitValues = 8}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 100}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 100}},
  }};
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = static_cast<UINT>(parameters.size()), .pParameters = parameters.data()};
  ComPtr<ID3DBlob> serialized;
  CHECK(SUCCEEDED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr)));
  ComPtr<ID3D12RootSignature> root;
  CHECK(SUCCEEDED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root))));
  const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {.pRootSignature = root.Get(),
                                                           .CS = {.pShaderBytecode = object->GetBufferPointer(), .BytecodeLength = object->GetBufferSize()}};
  ComPtr<ID3D12PipelineState> pipeline;
  CHECK(SUCCEEDED(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline))));

  const auto buffer = [&](uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
    const D3D12_HEAP_PROPERTIES properties = {.Type = heap};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                      .Width = std::max<uint64_t>(size, 256),
                                      .Height = 1,
                                      .DepthOrArraySize = 1,
                                      .MipLevels = 1,
                                      .SampleDesc = {.Count = 1},
                                      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                      .Flags = flags};
    ComPtr<ID3D12Resource> resource;
    device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));
    return resource;
  };
  // One sampler over the inputs (word: the sampler, extra: ref_b): its outputs, one float4 an input.
  const auto run = [&](uint32_t word, uint32_t extra, const std::vector<TestInput>& inputs) {
    const auto count = static_cast<uint32_t>(inputs.size());
    ComPtr<ID3D12Resource> input_buffer = buffer(inputs.size() * sizeof(TestInput), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    void* mapped = nullptr;
    input_buffer->Map(0, nullptr, &mapped);
    std::memcpy(mapped, inputs.data(), inputs.size() * sizeof(TestInput));
    input_buffer->Unmap(0, nullptr);
    const uint64_t output_size = uint64_t{count} * 16;
    ComPtr<ID3D12Resource> output_buffer = buffer(output_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> readback = buffer(output_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    const std::array<uint32_t, 8> words = {word, count, extra, 0u, 0u, 0u, 0u, 0u};
    list->SetComputeRootSignature(root.Get());
    list->SetPipelineState(pipeline.Get());
    list->SetComputeRoot32BitConstants(0, 8, words.data(), 0);
    list->SetComputeRootShaderResourceView(1, input_buffer->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(2, output_buffer->GetGPUVirtualAddress());
    list->Dispatch((count + 63) / 64, 1, 1);
    const D3D12_RESOURCE_BARRIER to_copy = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                            .Transition = {.pResource = output_buffer.Get(),
                                                           .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                           .StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                           .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE}};
    list->ResourceBarrier(1, &to_copy);
    list->CopyBufferRegion(readback.Get(), 0, output_buffer.Get(), 0, output_size);
    list->Close();
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.Get(), ++fence_value);
    fence->SetEventOnCompletion(fence_value, nullptr);
    allocator->Reset();
    list->Reset(allocator.Get(), nullptr);
    std::vector<std::array<float, 4>> outputs(count);
    readback->Map(0, nullptr, &mapped);
    std::memcpy(outputs.data(), mapped, output_size);
    readback->Unmap(0, nullptr);
    return outputs;
  };
  const auto dump = [&](const char* name, const std::vector<TestInput>& inputs, const std::vector<std::array<float, 4>>& outputs) {
    if (dump_folder.empty()) return;
    std::ofstream file(dump_folder + "\\" + name, std::ios::binary | std::ios::app);
    for (size_t index = 0; index < inputs.size(); ++index) {
      file.write(reinterpret_cast<const char*>(&inputs[index]), sizeof(TestInput));
      file.write(reinterpret_cast<const char*>(outputs[index].data()), 16);
    }
  };
  if (!dump_folder.empty()) {
    for (const char* name : {"disk.bin", "cosine.bin", "vndf.bin"}) std::remove((dump_folder + "\\" + name).c_str());
  }
  int failures = 0;
  const auto expect = [&](const char* what, double measured, double expected, double tolerance) {
    const bool ok = std::abs(measured - expected) <= tolerance;
    failures += ok ? 0 : 1;
    std::printf("  %-58s %+.6f expected %+.6f (+- %.6f) %s\n", what, measured, expected, tolerance, ok ? "ok" : "FAIL");
  };
  // A grid of side x side uniform points (the strata's centres): deterministic quadrature for the samplers' moments.
  const auto grid = [](int side, const std::array<float, 4>& frame, float alpha) {
    std::vector<TestInput> inputs(static_cast<size_t>(side) * side);
    for (int y = 0; y < side; ++y) {
      for (int x = 0; x < side; ++x) {
        inputs[static_cast<size_t>(y) * side + x] = {.u = {(x + 0.5f) / side, (y + 0.5f) / side, alpha, 0.f}, .frame = frame};
      }
    }
    return inputs;
  };

  // 1. RefRandom: 512 x 512 texels, sample 7 and 8.
  {
    std::printf("RefRandom (pcg4d), 262144 texels\n");
    const std::vector<TestInput> none(512 * 512);
    const auto first = run(0, 7, none);
    const auto second = run(0, 8, none);
    const double n = static_cast<double>(first.size());
    const auto correlation = [&](const auto& a_of, const auto& b_of, size_t count) {
      double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
      for (size_t index = 0; index < count; ++index) {
        const double a = a_of(index);
        const double b = b_of(index);
        sa += a;
        sb += b;
        saa += a * a;
        sbb += b * b;
        sab += a * b;
      }
      const double c = static_cast<double>(count);
      return (sab / c - (sa / c) * (sb / c)) / std::sqrt((saa / c - (sa / c) * (sa / c)) * (sbb / c - (sb / c) * (sb / c)));
    };
    for (int dimension = 0; dimension < 4; ++dimension) {
      double sum = 0.0, squares = 0.0;
      std::array<double, 64> bins = {};
      for (const auto& value : first) {
        sum += value[dimension];
        squares += value[dimension] * value[dimension];
        bins[std::min(static_cast<int>(value[dimension] * 64.f), 63)] += 1.0;
      }
      double chi = 0.0;
      for (const double bin : bins) chi += (bin - n / 64.0) * (bin - n / 64.0) / (n / 64.0);
      const std::string label = "dimension " + std::to_string(dimension);
      expect((label + ": mean").c_str(), sum / n, 0.5, 4.0 * std::sqrt(1.0 / 12.0 / n));
      expect((label + ": variance").c_str(), squares / n - (sum / n) * (sum / n), 1.0 / 12.0, 4.0 * std::sqrt(1.0 / 180.0 / n));
      expect((label + ": chi-square of 64 bins (63 dof)").c_str(), chi, 63.0, 4.0 * std::sqrt(2.0 * 63.0));
    }
    const double limit = 4.0 / std::sqrt(n);
    expect("dimensions 0 / 1 correlation", correlation([&](size_t i) { return first[i][0]; }, [&](size_t i) { return first[i][1]; }, first.size()), 0.0, limit);
    expect("dimensions 0 / 3 correlation", correlation([&](size_t i) { return first[i][0]; }, [&](size_t i) { return first[i][3]; }, first.size()), 0.0, limit);
    expect("neighbouring texels' correlation", correlation([&](size_t i) { return first[i][0]; }, [&](size_t i) { return first[i + 1][0]; }, first.size() - 1), 0.0, limit);
    expect("samples 7 / 8 correlation", correlation([&](size_t i) { return first[i][0]; }, [&](size_t i) { return second[i][0]; }, first.size()), 0.0, limit);
  }

  // 2. RefConcentricDisk.
  {
    std::printf("RefConcentricDisk, 1024 x 1024 grid\n");
    const std::vector<TestInput> inputs = grid(1024, {}, 0.f);
    const auto outputs = run(1, 0, inputs);
    dump("disk.bin", inputs, outputs);
    double mean_x = 0.0, mean_y = 0.0, r2 = 0.0, x2 = 0.0, largest = 0.0;
    for (const auto& value : outputs) {
      const double radius2 = double{value[0]} * value[0] + double{value[1]} * value[1];
      mean_x += value[0];
      mean_y += value[1];
      r2 += radius2;
      x2 += double{value[0]} * value[0];
      largest = std::max(largest, radius2);
    }
    const double n = static_cast<double>(outputs.size());
    expect("E[x]", mean_x / n, 0.0, 1e-5);
    expect("E[y]", mean_y / n, 0.0, 1e-5);
    expect("E[r^2]", r2 / n, 0.5, 1e-5);
    expect("E[x^2]", x2 / n, 0.25, 1e-5);
    expect("largest r^2 (inside the disk)", std::min(largest, 1.0), largest, 1e-6);
    std::mt19937 generator(2026u);
    std::uniform_real_distribution<float> uniform(0.f, 1.f);
    std::vector<TestInput> random_inputs(size_t{1} << 22);
    for (TestInput& input : random_inputs) input.u = {uniform(generator), uniform(generator), 0.f, 0.f};
    const auto random_outputs = run(1, 0, random_inputs);
    std::array<double, 128> areas = {};
    for (const auto& value : random_outputs) {
      const double radius2 = double{value[0]} * value[0] + double{value[1]} * value[1];
      const double angle = std::atan2(double{value[1]}, double{value[0]}) + PI;
      areas[std::min(static_cast<int>(radius2 * 8.0), 7) * 16 + std::min(static_cast<int>(angle / (2.0 * PI) * 16.0), 15)] += 1.0;
    }
    const double expected_count = static_cast<double>(random_outputs.size()) / 128.0;
    double chi = 0.0;
    for (const double area : areas) chi += (area - expected_count) * (area - expected_count) / expected_count;
    expect("8 rings x 16 sectors: chi-square (127 dof)", chi, 127.0, 4.0 * std::sqrt(2.0 * 127.0));
  }

  // 3. RefCosineDirection.
  {
    const std::array<Vec3, 6> normals = {{{0, 0, 1}, {0, 0, -1}, {0, 1, 0}, Normalize({1, 1, 1}), Normalize({0.3, -0.2, -0.93}), Normalize({1e-4, 0, -1})}};
    for (const Vec3& normal : normals) {
      std::printf("RefCosineDirection around (%.4f, %.4f, %.4f), 1024 x 1024 grid\n", normal.x, normal.y, normal.z);
      const std::vector<TestInput> inputs = grid(1024, {static_cast<float>(normal.x), static_cast<float>(normal.y), static_cast<float>(normal.z), 0.f}, 0.f);
      const auto outputs = run(2, 0, inputs);
      dump("cosine.bin", inputs, outputs);
      const double n = static_cast<double>(outputs.size());
      const std::array<double, 3> axis = {normal.x, normal.y, normal.z};
      double length_error = 0.0, lowest = 1.0, cosine = 0.0, cosine2 = 0.0;
      std::array<double, 9> moments = {};
      std::array<double, 6> faces = {};
      std::array<double, 9> below = {};
      for (const auto& value : outputs) {
        const std::array<double, 3> d = {value[0], value[1], value[2]};
        const double c = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
        length_error = std::max(length_error, std::abs(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - 1.0));
        lowest = std::min(lowest, c);
        cosine += c;
        cosine2 += c * c;
        for (int row = 0; row < 3; ++row) {
          for (int column = 0; column < 3; ++column) moments[row * 3 + column] += d[row] * d[column];
          faces[row * 2 + ((d[row] < 0.0) ? 1 : 0)] += d[row] * d[row];
        }
        for (int step = 1; step <= 9; ++step) below[step - 1] += (c * c < step / 10.0) ? 1.0 : 0.0;
      }
      expect("|d| - 1 (largest)", length_error, 0.0, 1e-5);
      expect("lowest n.d (above the surface)", std::min(lowest, 0.0), 0.0, 1e-6);
      expect("E[n.d]", cosine / n, 2.0 / 3.0, 1e-4);
      expect("E[(n.d)^2]", cosine2 / n, 0.5, 1e-4);
      double worst_moment = 0.0;
      double worst_face = 0.0;
      double worst_cdf = 0.0;
      for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
          const double expected = (((row == column) ? 1.0 : 0.0) + axis[row] * axis[column]) / 4.0;
          worst_moment = std::max(worst_moment, std::abs(moments[row * 3 + column] / n - expected));
        }
        const double base = (1.0 + axis[row] * axis[row]) / 8.0;
        worst_face = std::max({worst_face, std::abs(faces[row * 2] / n - (base + axis[row] / 4.0)), std::abs(faces[row * 2 + 1] / n - (base - axis[row] / 4.0))});
      }
      for (int step = 1; step <= 9; ++step) worst_cdf = std::max(worst_cdf, std::abs(below[step - 1] / n - step / 10.0));
      expect("E[d d^T] - (I + n n^T) / 4 (largest entry)", worst_moment, 0.0, 1e-4);
      expect("cube faces' cosine means - (1 + n_a^2) / 8 +- n_a / 4", worst_face, 0.0, 1e-4);
      expect("P(cos^2 < c) - c, c = 0.1 .. 0.9 (largest)", worst_cdf, 0.0, 2e-3);
    }
  }

  // 4. RefBasis.
  {
    std::printf("RefBasis, 200000 normals\n");
    std::vector<TestInput> inputs;
    uint32_t seed = 12345u;
    const auto random = [&seed]() {
      seed = seed * 1664525u + 1013904223u;
      return static_cast<double>(seed >> 8) / 16777216.0;
    };
    for (const Vec3& corner : {Vec3{0, 0, 1}, Vec3{0, 0, -1}, Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0}, Normalize({1e-6, 0, -1}), Normalize({0, 1e-6, 1})}) {
      inputs.push_back({.frame = {static_cast<float>(corner.x), static_cast<float>(corner.y), static_cast<float>(corner.z), 0.f}});
    }
    while (inputs.size() < 200000) {
      const double z = 2.0 * random() - 1.0;
      const double angle = 2.0 * PI * random();
      const double radius = std::sqrt(std::max(1.0 - z * z, 0.0));
      inputs.push_back({.frame = {static_cast<float>(radius * std::cos(angle)), static_cast<float>(radius * std::sin(angle)), static_cast<float>(z), 0.f}});
    }
    const auto outputs = run(3, 0, inputs);
    double worst_dot = 0.0;
    double worst_handed = 0.0;
    for (const auto& value : outputs) {
      worst_dot = std::max({worst_dot, std::abs(double{value[0]}), std::abs(double{value[1]}), std::abs(double{value[2]})});
      worst_handed = std::max(worst_handed, std::abs(double{value[3]} - 1.0));
    }
    expect("largest |t.b|, |t.n|, |b.n|", worst_dot, 0.0, 1e-5);
    expect("largest |(t x b).n - 1| (right-handed)", worst_handed, 0.0, 1e-5);
  }

  // 5. RefVisibleNormal with its G2 / G1 weight.
  {
    for (const double alpha : {0.0064, 0.05, 0.25, 0.6, 1.0}) {
      for (const double view_cosine : {0.05, 0.3, 0.7, 1.0}) {
        const Vec3 view = {std::sqrt(std::max(1.0 - view_cosine * view_cosine, 0.0)), 0.0, view_cosine};
        std::printf("RefVisibleNormal alpha %.4f, n.v %.2f, 512 x 512 grid\n", alpha, view_cosine);
        const std::vector<TestInput> inputs = grid(512, {static_cast<float>(view.x), static_cast<float>(view.y), static_cast<float>(view.z), 0.f}, static_cast<float>(alpha));
        const auto outputs = run(4, 0, inputs);
        dump("vndf.bin", inputs, outputs);
        const LobeIntegrals integrals = IntegrateLobe(view, alpha);
        const double n = static_cast<double>(outputs.size());
        double weight = 0.0, above = 0.0, lowest_view_half = 1.0, lowest_z = 1.0;
        for (const auto& value : outputs) {
          const Vec3 half_vector = {value[0], value[1], value[2]};
          const double view_half = Dot(view, half_vector);
          weight += value[3];
          above += (2.0 * view_half * half_vector.z - view.z > 0.0) ? 1.0 : 0.0;
          lowest_view_half = std::min(lowest_view_half, view_half);
          lowest_z = std::min(lowest_z, half_vector.z);
        }
        expect("E[G2 / G1] - the albedo by D sampling", weight / n, integrals.albedo, 1e-3);
        expect("lowest v.h (visible normals only)", std::min(lowest_view_half, 0.0), 0.0, 1e-4);
        expect("lowest h.z", std::min(lowest_z, 0.0), 0.0, 1e-6);
        // The density: random u (a band's edge crosses the grid's cells unevenly at grazing views), each band's share within
        // 4 standard errors of the multinomial's plus the quadrature's 2e-4.
        std::mt19937 generator(static_cast<uint32_t>(alpha * 1e4 + view_cosine * 1e2));
        std::uniform_real_distribution<float> uniform(0.f, 1.f);
        std::vector<TestInput> random_inputs = inputs;
        for (TestInput& input : random_inputs) input.u = {uniform(generator), uniform(generator), static_cast<float>(alpha), 0.f};
        const auto random_outputs = run(4, 0, random_inputs);
        std::array<double, 8> bands = {};
        for (const auto& value : random_outputs) {
          const double cos2 = double{value[2]} * value[2];
          const double tan2 = (1.0 - cos2) / std::max(cos2, 1e-30);
          bands[std::min(static_cast<int>(tan2 / (alpha * alpha + tan2) * 8.0), 7)] += 1.0;
        }
        double worst_band = 0.0;
        double total_mass = 0.0;
        for (int band = 0; band < 8; ++band) {
          const double share = integrals.bands[band];
          total_mass += share;
          worst_band = std::max(worst_band, std::abs(bands[band] / n - share) / (4.0 * std::sqrt(std::max(share * (1.0 - share), 1e-12) / n) + 2e-4));
        }
        expect("visible-normal density: total mass (quadrature)", total_mass, 1.0, 1e-4);
        expect("visible-normal mass in 8 bands of xi (worst, in 4 sigma)", worst_band, 0.0, 1.0);
        // CONTROL: the share above the horizon stands for the albedo only where the lobe is narrow or the view steep.
        if (alpha >= 0.6 && view_cosine <= 0.3) {
          const bool caught = std::abs(above / n - integrals.albedo) > 1e-2;
          failures += caught ? 0 : 1;
          std::printf("  CONTROL: share above the horizon %.4f vs albedo %.4f: %s\n", above / n, integrals.albedo, caught ? "caught (ok)" : "NOT CAUGHT (FAIL)");
        }
      }
    }
  }

  // 6. RefStratified.
  {
    std::printf("RefStratified\n");
    std::vector<TestInput> inputs(70, {.u = {0.5f, 0.25f, 0.f, 0.f}});
    const auto outputs = run(5, 70, inputs);
    double worst = 0.0;
    for (uint32_t index = 0; index < 70; ++index) {
      const bool in_square = index < 64;
      const double x = in_square ? ((index % 8) + 0.5) / 8.0 : 0.5;
      const double y = in_square ? ((index / 8) + 0.25) / 8.0 : 0.25;
      worst = std::max({worst, std::abs(outputs[index][0] - x), std::abs(outputs[index][1] - y)});
    }
    expect("70 points: 8 x 8 strata, then the jitter (largest error)", worst, 0.0, 1e-6);
  }
  std::printf("%s (%d failed)\n", (failures == 0) ? "ALL PASSED" : "SOME FAILED", failures);
  return (failures == 0) ? 0 : 1;
}
