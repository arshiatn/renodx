/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// U6 part 1 gates (unified_pt.md 4.6): the fog local stream's reservoir math (rt_fog_reservoir.hlsli) in fog_probe.hlsl on
// D3D12 WARP (dxc cs_6_5 -O3 as build.ps1; cs_6_9 compiled statically), its trial means against a double quadrature of
// (1 / R) x the integral over the ray of every light's colour x falloff x visibility, and the one-light case against its
// closed form. Every stage must be unbiased (within 4 standard errors) and each scenario's biased controls must be caught
// (beyond 6). The four FOG_RESTIR builds of rt_fog.cs_6_5.hlsl (main / shade, cs_6_5 / cs_6_9 OMM, as build.ps1) must
// compile without a local-memory array (no alloca in their DXIL).
// Usage: tw_fog_restir.exe <repository root> <folder of dxcompiler.dll and dxil.dll>. Exit 0: every gate passed.
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
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct Float4 {
  float x, y, z, w;
};

struct ProbeLight {
  Float4 position_reach;
  Float4 colour_linear;
};

struct ProbeScenario {
  std::array<Float4, 4> origin_range;
  std::array<Float4, 4> direction;
  std::array<uint32_t, 4> list_low;
  std::array<uint32_t, 4> list_high;
  uint32_t count;
  uint32_t seed;
  uint32_t occluder;
  float history_confidence;
  std::array<Float4, 4> view_projection;
  std::array<uint32_t, 4> history_low;
  std::array<uint32_t, 4> history_high;
  uint32_t history_count;
  uint32_t new_light;
};

struct ProbeOutput {
  Float4 initial;
  Float4 temporal;
  Float4 spatial;
  Float4 spatial_two;
  Float4 naive;
  Float4 no_defensive;
  Float4 unit;
  Float4 camera;
  Float4 as_built;
};

constexpr uint32_t TRIALS = 65536;  // 16 x 4096 (the probe's lanes), a multiple of 64
constexpr uint32_t NO_LIGHT = 0xFF;

int failures = 0;

void Gate(const std::string& name, bool passed, const std::string& detail) {
  std::printf("%s %s: %s\n", passed ? "PASS" : "FAIL", name.c_str(), detail.c_str());
  if (!passed) {
    ++failures;
  }
}

struct Vec3 {
  double x, y, z;
};
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Normalize(Vec3 a) { return a * (1.0 / Length(a)); }
Vec3 Of(Float4 v) { return {v.x, v.y, v.z}; }
Float4 F4(Vec3 v, double w) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), static_cast<float>(w)}; }

double Falloff(const ProbeLight& light, Vec3 at) {
  const double f = std::clamp(1.0 - Length(Of(light.position_reach) - at) / std::max<double>(light.position_reach.w, 1e-3), 0.0, 1.0);
  return (light.colour_linear.w != 0.f) ? f : f * f;
}

// (1 / R) x the integral over [0, R] of every light's colour x falloff x visibility along domain 0 (Simpson, 2M intervals:
// the reach's kinks and the plane's step cost under 1e-7 of it).
Vec3 Reference(const ProbeScenario& scenario, const std::vector<ProbeLight>& lights) {
  const Vec3 origin = Of(scenario.origin_range[0]);
  const Vec3 direction = Of(scenario.direction[0]);
  const double range = scenario.origin_range[0].w;
  constexpr int INTERVALS = 2000000;
  const double h = range / INTERVALS;
  Vec3 sum = {0, 0, 0};
  for (int i = 0; i <= INTERVALS; ++i) {
    const Vec3 at = origin + direction * (h * i);
    Vec3 light_sum = {0, 0, 0};
    for (const ProbeLight& light : lights) {
      if (scenario.occluder != 0 && ((at.z > 0) != (light.position_reach.z > 0))) continue;
      light_sum = light_sum + Of(light.colour_linear) * Falloff(light, at);
    }
    sum = sum + light_sum * ((i == 0 || i == INTERVALS) ? 1.0 : ((i & 1) ? 4.0 : 2.0));
  }
  return sum * (h / 3.0 / range);
}

// The one linear light's closed form: (1 / R) x colour x the integral of 1 - sqrt(h^2 + u^2) / reach where it reaches.
Vec3 ClosedForm(const ProbeScenario& scenario, const ProbeLight& light) {
  const Vec3 origin = Of(scenario.origin_range[0]);
  const Vec3 direction = Of(scenario.direction[0]);
  const double range = scenario.origin_range[0].w;
  const double reach = light.position_reach.w;
  const Vec3 to_light = Of(light.position_reach) - origin;
  const double foot = Dot(to_light, direction);
  const double h2 = Dot(to_light, to_light) - foot * foot;
  if (h2 >= reach * reach) return {0, 0, 0};
  const double half = std::sqrt(reach * reach - h2);
  const double t1 = std::max(0.0, foot - half);
  const double t2 = std::min(range, foot + half);
  if (t2 <= t1) return {0, 0, 0};
  const double h = std::sqrt(h2);
  const auto antiderivative = [&](double u) { return 0.5 * (u * std::sqrt(h2 + u * u) + h2 * std::asinh(u / h)); };
  const double integral = (t2 - t1) - (antiderivative(t2 - foot) - antiderivative(t1 - foot)) / reach;
  return Of(light.colour_linear) * (integral / range);
}

template <typename... Values>
std::string Format(const char* format, Values... values) {
  char text[256];
  std::snprintf(text, sizeof(text), format, values...);
  return text;
}

struct Warp {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  uint64_t fence_value = 0;

  ComPtr<ID3D12Resource> Buffer(size_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) const {
    const D3D12_HEAP_PROPERTIES heap_properties = {.Type = heap};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = size, .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1,
                                      .SampleDesc = {.Count = 1}, .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR, .Flags = flags};
    ComPtr<ID3D12Resource> resource;
    device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));
    return resource;
  }

  bool Run(const ProbeScenario& scenario, const std::vector<ProbeLight>& lights, std::vector<ProbeOutput>* outputs) {
    const ComPtr<ID3D12Resource> light_buffer = Buffer(lights.size() * sizeof(ProbeLight), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    const ComPtr<ID3D12Resource> scenario_buffer = Buffer(sizeof(ProbeScenario), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    const size_t output_size = TRIALS * sizeof(ProbeOutput);
    const ComPtr<ID3D12Resource> output = Buffer(output_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const ComPtr<ID3D12Resource> readback = Buffer(output_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    if (!light_buffer || !scenario_buffer || !output || !readback) return false;
    void* mapped = nullptr;
    if (FAILED(light_buffer->Map(0, nullptr, &mapped))) return false;
    std::memcpy(mapped, lights.data(), lights.size() * sizeof(ProbeLight));
    light_buffer->Unmap(0, nullptr);
    if (FAILED(scenario_buffer->Map(0, nullptr, &mapped))) return false;
    std::memcpy(mapped, &scenario, sizeof(scenario));
    scenario_buffer->Unmap(0, nullptr);
    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator.Get(), pipeline.Get()))) return false;
    list->SetComputeRootSignature(root.Get());
    list->SetComputeRootShaderResourceView(0, light_buffer->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(1, scenario_buffer->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
    list->Dispatch(TRIALS / 64, 1, 1);
    const D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                            .Transition = {.pResource = output.Get(), .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                           .StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE}};
    list->ResourceBarrier(1, &barrier);
    list->CopyResource(readback.Get(), output.Get());
    if (FAILED(list->Close())) return false;
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (FAILED(queue->Signal(fence.Get(), ++fence_value)) || FAILED(fence->SetEventOnCompletion(fence_value, nullptr))) return false;
    if (FAILED(device->GetDeviceRemovedReason())) return false;
    outputs->resize(TRIALS);
    const D3D12_RANGE read_range = {.Begin = 0, .End = output_size};
    if (FAILED(readback->Map(0, &read_range, &mapped))) return false;
    std::memcpy(outputs->data(), mapped, output_size);
    const D3D12_RANGE written = {.Begin = 0, .End = 0};
    readback->Unmap(0, &written);
    return true;
  }
};

// Registers of a camera at `camera`: rows 0, 1 and 3 vanish there (the rest arbitrary), CameraOf's check.
void SetCamera(ProbeScenario* scenario, Vec3 camera) {
  const Vec3 rows[4] = {{0.9, 0.1, -0.3}, {-0.2, 1.1, 0.25}, {0.05, -0.1, 1.02}, {0.15, 0.2, 0.97}};
  for (int row = 0; row < 4; ++row) {
    scenario->view_projection[row] = F4(rows[row], (row == 2) ? 0.5 : -Dot(rows[row], camera));
  }
}

ProbeScenario Domains(Vec3 origin, const std::array<Vec3, 4>& directions, const std::array<double, 4>& ranges) {
  ProbeScenario scenario = {};
  for (int domain = 0; domain < 4; ++domain) {
    scenario.origin_range[domain] = F4(origin, ranges[domain]);
    scenario.direction[domain] = F4(Normalize(directions[domain]), 0.0);
  }
  return scenario;
}

ProbeLight Light(Vec3 position, double reach, Vec3 colour, bool linear) {
  return {F4(position, reach), F4(colour, linear ? 1.0 : 0.0)};
}

// The words of a list of lights (a byte each, NO_LIGHT past the end), as this trace's and last frame's list.
void SetLists(ProbeScenario* scenario, const std::vector<uint32_t>& members, uint32_t count, uint32_t new_light) {
  std::array<uint8_t, 32> bytes;
  std::array<uint8_t, 32> history;
  bytes.fill(NO_LIGHT);
  history.fill(NO_LIGHT);
  size_t kept = 0;
  for (size_t slot = 0; slot < members.size(); ++slot) {
    bytes[slot] = static_cast<uint8_t>(members[slot]);
    if (members[slot] != new_light) {
      history[kept++] = static_cast<uint8_t>(members[slot]);
    }
  }
  std::memcpy(scenario->list_low.data(), bytes.data(), 16);
  std::memcpy(scenario->list_high.data(), bytes.data() + 16, 16);
  std::memcpy(scenario->history_low.data(), history.data(), 16);
  std::memcpy(scenario->history_high.data(), history.data() + 16, 16);
  scenario->count = count;
  // Last frame's lights: all but new_light, the last id (FogCandidates' defensive pick draws ids below its count).
  scenario->history_count = (new_light == count - 1) ? count - 1 : count;
  scenario->new_light = new_light;
}

// One scenario's gates. The controls: whether the list misses contributing lights (no_defensive biased), a neighbour's
// range is short (naive biased) and a light is new this trace (as_built biased). Any control not expected biased is
// printed as info.
void Check(const char* name, ProbeScenario scenario, const std::vector<ProbeLight>& lights, uint32_t list_size, bool defensive_control,
           bool naive_control, bool as_built_control, Vec3 camera, Warp* warp, const Vec3* closed_form) {
  std::vector<ProbeOutput> outputs;
  if (!warp->Run(scenario, lights, &outputs)) {
    Gate(std::string(name) + " WARP run", false, "the dispatch failed");
    return;
  }
  const Vec3 reference = Reference(scenario, lights);
  std::printf("  %s reference (1/R) integral: %.6g %.6g %.6g\n", name, reference.x, reference.y, reference.z);
  if (closed_form != nullptr) {
    const double relative = std::abs(reference.x - closed_form->x) / std::max(std::abs(closed_form->x), 1e-30);
    Gate(std::string(name) + " quadrature = closed form", relative < 1e-6, Format("closed %.8g, quadrature %.8g, relative %.2g", closed_form->x, reference.x, relative));
  }
  bool finite = true;
  const struct {
    const char* stage;
    Float4 ProbeOutput::*member;
    bool biased;
  } stages[] = {{"candidates", &ProbeOutput::initial, false},
                {"temporal (still, M 4)", &ProbeOutput::temporal, false},
                {"spatial, 1 final", &ProbeOutput::spatial, false},
                {"spatial, 2 finals", &ProbeOutput::spatial_two, false},
                {"control: M-share weights", &ProbeOutput::naive, naive_control},
                {"control: no defensive pick", &ProbeOutput::no_defensive, defensive_control},
                {"control: history target for new lights", &ProbeOutput::as_built, as_built_control}};
  const double references[3] = {reference.x, reference.y, reference.z};
  for (const auto& stage : stages) {
    // Per channel: the trials' mean and standard error, then the worst |mean - reference| in standard errors (errors
    // floored at 1e-9 of the reference: a zero-variance stage has none).
    double sum[3] = {};
    double square[3] = {};
    for (const ProbeOutput& output : outputs) {
      const float values[3] = {(output.*stage.member).x, (output.*stage.member).y, (output.*stage.member).z};
      for (int c = 0; c < 3; ++c) {
        finite = finite && std::isfinite(values[c]);
        sum[c] += values[c];
        square[c] += static_cast<double>(values[c]) * values[c];
      }
    }
    const double n = static_cast<double>(outputs.size());
    double mean[3];
    double error[3];
    double sigmas = 0.0;
    double relative = 0.0;
    for (int c = 0; c < 3; ++c) {
      mean[c] = sum[c] / n;
      error[c] = std::sqrt(std::max(square[c] / n - mean[c] * mean[c], 0.0) / (n - 1.0));
      const double floor = std::max(1e-9 * std::abs(references[c]), 1e-12);
      sigmas = std::max(sigmas, std::abs(mean[c] - references[c]) / std::max(error[c], floor));
      if (references[c] != 0.0) {
        relative = std::max(relative, std::abs(mean[c] - references[c]) / std::abs(references[c]));
      }
    }
    const double signed_relative = (reference.x != 0.0) ? (mean[0] - reference.x) / reference.x : 0.0;
    const std::string detail = Format("mean %.6g (err %.2g), %.2f sigma, relative %.3g (r %+.2f%%)", mean[0], error[0], sigmas, relative, 100.0 * signed_relative);
    const bool is_control = std::strncmp(stage.stage, "control", 7) == 0;
    if (is_control && !stage.biased) {
      std::printf("  info %s %s: %s\n", name, stage.stage, detail.c_str());
    } else if (stage.biased) {
      Gate(std::string(name) + " " + stage.stage + " caught", sigmas > 6.0, detail);
    } else {
      Gate(std::string(name) + " " + stage.stage + " unbiased", sigmas <= 4.0, detail);
    }
    if (!is_control && reference.x > 0.0) {
      // Power: a 4% bias is beyond 4 standard errors.
      Gate(std::string(name) + " " + stage.stage + " error under 1%", error[0] < 0.01 * reference.x, Format("err / ref %.3g", error[0] / reference.x));
    }
  }
  Gate(std::string(name) + " finite outputs", finite, finite ? "all" : "NaN / Inf");
  // Units: FogTarget against a double, the pack round trip, the list's size, CameraOf.
  double target_error = 0.0;
  bool packs = true;
  bool sizes = true;
  for (uint32_t trial = 0; trial < TRIALS; ++trial) {
    const ProbeOutput& output = outputs[trial];
    const ProbeLight& light = lights[trial % scenario.count];
    const Vec3 at = Of(scenario.origin_range[0]) + Of(scenario.direction[0]) * output.unit.y;
    const Vec3 colour = Of(light.colour_linear);
    const double expected = std::max(0.2126 * colour.x + 0.7152 * colour.y + 0.0722 * colour.z, 0.0) * Falloff(light, at);
    target_error = std::max(target_error, std::abs(output.unit.x - expected) / std::max(expected, 1.0));
    packs = packs && output.unit.z == 1.f;
    sizes = sizes && output.unit.w == static_cast<float>(list_size);
  }
  Gate(std::string(name) + " FogTarget vs double", target_error < 2e-5, Format("max error %.2g", target_error));
  Gate(std::string(name) + " pack round trip exact", packs, packs ? "all trials" : "a mismatch");
  Gate(std::string(name) + " FogListOf size", sizes, Format("expected %u, trial 0 %.0f", list_size, outputs[0].unit.w));
  const double camera_error = Length(Of(outputs[0].camera) - camera) / std::max(Length(camera), 1.0);
  Gate(std::string(name) + " CameraOf finds the camera", camera_error < 1e-5, Format("relative error %.2g", camera_error));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: tw_fog_restir.exe <repository root> <folder of dxcompiler.dll and dxil.dll>\n");
    return 2;
  }
  const std::string root = argv[1];
  const std::string dxc_folder = argv[2];
  const std::wstring probe_path = std::wstring(root.begin(), root.end()) + L"\\test\\tw_fog_restir\\fog_probe.hlsl";
  const auto setup_failed = [](const char* what) {
    std::printf("FAIL setup: %s\n", what);
    return 1;
  };
  if (LoadLibraryA((dxc_folder + "\\dxil.dll").c_str()) == nullptr) return setup_failed("no dxil.dll");
  const HMODULE dxc_module = LoadLibraryA((dxc_folder + "\\dxcompiler.dll").c_str());
  if (dxc_module == nullptr) return setup_failed("no dxcompiler.dll");
  const auto dxc_create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxc_module, "DxcCreateInstance"));
  if (dxc_create == nullptr) return setup_failed("no DxcCreateInstance");
  ComPtr<IDxcUtils> utils;
  ComPtr<IDxcCompiler3> compiler;
  ComPtr<IDxcIncludeHandler> includes;
  ComPtr<IDxcBlobEncoding> source;
  if (FAILED(dxc_create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) || FAILED(dxc_create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))
      || FAILED(utils->CreateDefaultIncludeHandler(&includes)) || FAILED(utils->LoadFile(probe_path.c_str(), nullptr, &source))) {
    return setup_failed("dxc instances or the probe's source");
  }
  const DxcBuffer source_buffer = {.Ptr = source->GetBufferPointer(), .Size = source->GetBufferSize(), .Encoding = DXC_CP_ACP};
  const auto compile = [&](const wchar_t* target) {
    std::vector<LPCWSTR> arguments = {probe_path.c_str(), L"-T", target, L"-E", L"main", L"-O3"};
    ComPtr<IDxcResult> result;
    HRESULT status = E_FAIL;
    ComPtr<IDxcBlob> object;
    if (FAILED(compiler->Compile(&source_buffer, arguments.data(), static_cast<UINT32>(arguments.size()), includes.Get(), IID_PPV_ARGS(&result)))
        || FAILED(result->GetStatus(&status))) {
      return object;
    }
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    const std::string error_text = (errors != nullptr && errors->GetStringLength() > 0) ? errors->GetStringPointer() : "";
    Gate(std::string("dxc ") + (target[5] == L'9' ? "cs_6_9" : "cs_6_5") + " -O3 compiles without warnings", SUCCEEDED(status) && error_text.empty(),
         error_text.empty() ? "clean" : error_text);
    if (SUCCEEDED(status)) result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
    return object;
  };
  if (compile(L"cs_6_9") == nullptr) return setup_failed("dxc cs_6_9");
  const ComPtr<IDxcBlob> object = compile(L"cs_6_5");
  if (object == nullptr) return setup_failed("dxc cs_6_5");

  // The helper's FOG_RESTIR builds: compiled, disassembled, no alloca (a local-memory array).
  {
    const std::wstring fog_path = std::wstring(root.begin(), root.end()) + L"\\src\\games\\twattila\\dlaa_helper\\rt_fog.cs_6_5.hlsl";
    ComPtr<IDxcBlobEncoding> fog_source;
    if (FAILED(utils->LoadFile(fog_path.c_str(), nullptr, &fog_source))) return setup_failed("rt_fog.cs_6_5.hlsl");
    const DxcBuffer fog_buffer = {.Ptr = fog_source->GetBufferPointer(), .Size = fog_source->GetBufferSize(), .Encoding = DXC_CP_ACP};
    for (const auto& [entry, target, omm] : {std::tuple{L"main", L"cs_6_5", false}, std::tuple{L"main", L"cs_6_9", true},
                                              std::tuple{L"shade", L"cs_6_5", false}, std::tuple{L"shade", L"cs_6_9", true}}) {
      std::vector<LPCWSTR> arguments = {fog_path.c_str(), L"-T", target, L"-E", entry, L"-O3", L"-D", L"FOG_RESTIR"};
      if (omm) {
        arguments.insert(arguments.end(), {L"-D", L"OPACITY_MICROMAPS"});
      }
      const std::string label = Format("rt_fog %s %s", entry[0] == L'm' ? "main" : "shade", omm ? "cs_6_9 OMM" : "cs_6_5");
      ComPtr<IDxcResult> result;
      HRESULT status = E_FAIL;
      ComPtr<IDxcBlob> fog_object;
      ComPtr<IDxcResult> disassembly;
      ComPtr<IDxcBlobUtf8> text;
      if (FAILED(compiler->Compile(&fog_buffer, arguments.data(), static_cast<UINT32>(arguments.size()), includes.Get(), IID_PPV_ARGS(&result)))
          || FAILED(result->GetStatus(&status)) || FAILED(status) || FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&fog_object), nullptr))) {
        Gate(label + " compiles", false, "dxc failed");
        continue;
      }
      const DxcBuffer object_buffer = {.Ptr = fog_object->GetBufferPointer(), .Size = fog_object->GetBufferSize(), .Encoding = 0};
      if (FAILED(compiler->Disassemble(&object_buffer, IID_PPV_ARGS(&disassembly))) || FAILED(disassembly->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&text), nullptr))
          || text == nullptr) {
        Gate(label + " disassembles", false, "dxc failed");
        continue;
      }
      // An instruction "%x = alloca ..." (allocateRayQuery is not one).
      const std::string_view listing(text->GetStringPointer(), text->GetStringLength());
      int allocas = 0;
      for (size_t at = listing.find("= alloca "); at != std::string_view::npos; at = listing.find("= alloca ", at + 1)) {
        ++allocas;
      }
      Gate(label + " no local-memory arrays", allocas == 0, Format("%d alloca, %zu bytes", allocas, static_cast<size_t>(fog_object->GetBufferSize())));
    }
  }

  Warp warp;
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter> adapter;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))
      || FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&warp.device)))) {
    return setup_failed("no D3D12 WARP device");
  }
  const std::array<D3D12_ROOT_PARAMETER, 3> parameters = {{
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 20}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 21}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 20}},
  }};
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = static_cast<UINT>(parameters.size()), .pParameters = parameters.data()};
  ComPtr<ID3DBlob> serialized;
  ComPtr<ID3DBlob> root_errors;
  if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &root_errors))
      || FAILED(warp.device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&warp.root)))) {
    return setup_failed("D3D12 root signature");
  }
  const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
      .pRootSignature = warp.root.Get(), .CS = {.pShaderBytecode = object->GetBufferPointer(), .BytecodeLength = object->GetBufferSize()}};
  const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
  if (FAILED(warp.device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&warp.pipeline)))
      || FAILED(warp.device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&warp.queue)))
      || FAILED(warp.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&warp.allocator)))
      || FAILED(warp.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, warp.allocator.Get(), warp.pipeline.Get(), IID_PPV_ARGS(&warp.list)))
      || FAILED(warp.list->Close()) || FAILED(warp.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&warp.fence)))) {
    return setup_failed("D3D12 pipeline, queue, list or fence");
  }

  // S1: one linear light, the closed form; the neighbours and last frame on the same ray.
  {
    const Vec3 direction = {0.1, 0.05, 1.0};
    ProbeScenario scenario = Domains({0, 1, 0}, {direction, direction, direction, direction}, {60, 60, 60, 60});
    const std::vector<ProbeLight> lights = {Light({3, 2, 20}, 8, {1.0, 0.6, 0.3}, true)};
    SetLists(&scenario, {0}, 1, NO_LIGHT);
    scenario.seed = 11;
    scenario.history_confidence = 4.f;
    const Vec3 camera = {0.5, 12.0, -3.25};
    SetCamera(&scenario, camera);
    const Vec3 closed = ClosedForm(scenario, lights[0]);
    Check("S1 one light", scenario, lights, 1, false, false, false, camera, &warp, &closed);
  }

  // S2: 12 lights (both falloffs, both sides of the plane z = 0), a list of 5, neighbours on other rays (one far shorter,
  // one a sky ray), last frame M 4: every stage unbiased, both controls caught.
  {
    const Vec3 origin = {0, 1.5, -5};
    ProbeScenario scenario = Domains(origin, {Vec3{0.2, 0.02, 1}, Vec3{0.23, 0.0, 1}, Vec3{0.17, 0.05, 1}, Vec3{0.2, 0.06, 1}}, {50, 50, 18, 400});
    std::vector<ProbeLight> lights;
    for (int light = 0; light < 12; ++light) {
      const double t = 3.0 + 3.9 * light;
      const Vec3 along = origin + Normalize({0.2, 0.02, 1}) * t;
      const Vec3 offset = {std::sin(light * 2.1) * 3.0, std::cos(light * 1.3) * 2.0, (light == 1) ? -6.0 : 0.0};
      lights.push_back(Light(along + offset, 4.0 + (light % 4) * 2.0, {0.5 + 0.1 * (light % 5), 0.3 + 0.07 * (light % 3), 0.2 + 0.15 * (light % 2)}, (light & 1) == 0));
    }
    SetLists(&scenario, {0, 3, 5, 7, 9}, 12, NO_LIGHT);
    scenario.seed = 23;
    scenario.occluder = 1;
    scenario.history_confidence = 4.f;
    SetCamera(&scenario, origin);
    Check("S2 twelve lights", scenario, lights, 5, true, true, false, origin, &warp, nullptr);
  }

  // S3: 8 smooth lights all in the list (no defensive pick), a neighbour on another ray.
  {
    const Vec3 origin = {2, 0.5, 1};
    ProbeScenario scenario = Domains(origin, {Vec3{-0.1, 0.1, 1}, Vec3{-0.1, 0.1, 1}, Vec3{-0.05, 0.12, 1}, Vec3{-0.1, 0.1, 1}}, {35, 35, 35, 35});
    std::vector<ProbeLight> lights;
    for (int light = 0; light < 8; ++light) {
      const Vec3 along = origin + Normalize({-0.1, 0.1, 1}) * (2.0 + 4.0 * light);
      lights.push_back(Light(along + Vec3{1.5 * std::cos(light * 0.7), 1.0, 1.5 * std::sin(light * 0.7)}, 5.0, {0.9, 0.5 + 0.05 * light, 0.2}, false));
    }
    SetLists(&scenario, {0, 1, 2, 3, 4, 5, 6, 7}, 8, NO_LIGHT);
    scenario.seed = 37;
    scenario.history_confidence = 2.f;
    SetCamera(&scenario, origin);
    Check("S3 eight smooth lights", scenario, lights, 8, false, false, false, origin, &warp, nullptr);
  }

  // S4: no light reaches the ray: every stage exactly 0.
  {
    const Vec3 direction = {0, 0, 1};
    ProbeScenario scenario = Domains({0, 0, 0}, {direction, direction, direction, direction}, {20, 20, 20, 20});
    const std::vector<ProbeLight> lights = {Light({100, 0, 10}, 5, {1, 1, 1}, true), Light({-100, 0, 10}, 5, {1, 1, 1}, false)};
    SetLists(&scenario, {1}, 2, NO_LIGHT);
    scenario.seed = 41;
    scenario.history_confidence = 4.f;
    SetCamera(&scenario, {0, 0, 0});
    std::vector<ProbeOutput> outputs;
    if (!warp.Run(scenario, lights, &outputs)) {
      Gate("S4 WARP run", false, "the dispatch failed");
    } else {
      bool zero = true;
      for (const ProbeOutput& output : outputs) {
        for (const Float4* value : {&output.initial, &output.temporal, &output.spatial, &output.spatial_two}) {
          zero = zero && value->x == 0.f && value->y == 0.f && value->z == 0.f && value->w == 0.f;
        }
      }
      Gate("S4 nothing reaches: every stage 0", zero, zero ? "all trials" : "a non-zero trial");
    }
  }

  // S5: 128 lights, a full list of 32 (ids up to 127: every word of FogList.lights, list slots 16-31), the defensive pick
  // over the 96 others (four dim ones reach the ray), neighbours on other rays.
  {
    const Vec3 origin = {1, 2, -2};
    const Vec3 direction = Normalize({0.1, -0.03, 1});
    ProbeScenario scenario = Domains(origin, {direction, Vec3{0.12, -0.02, 1}, Vec3{0.08, -0.04, 1}, Vec3{0.1, 0.0, 1}}, {60, 60, 30, 400});
    std::vector<uint32_t> members;
    for (uint32_t slot = 0; slot < 31; ++slot) {
      members.push_back((3 + 37 * slot) % 128);
    }
    members.push_back(127);
    std::vector<ProbeLight> lights(128, Light({300, 0, 0}, 4, {1, 1, 1}, true));
    for (uint32_t slot = 0; slot < 32; ++slot) {
      const Vec3 offset = {2.0 * std::sin(slot * 1.7), 1.5 * std::cos(slot * 0.9), 0.0};
      lights[members[slot]] = Light(origin + direction * (1.5 + 1.8 * slot) + offset, 3.0 + (slot % 5), {0.4 + 0.1 * (slot % 4), 0.5, 0.2 + 0.05 * (slot % 3)},
                                    (slot % 3) != 0);
    }
    uint32_t dim = 0;
    for (uint32_t light = 0; light < 128 && dim < 4; ++light) {
      if (std::find(members.begin(), members.end(), light) == members.end()) {
        lights[light] = Light(origin + direction * (8.0 + 14.0 * dim) + Vec3{0.5, -1.0, 0.0}, 5.0, {0.12, 0.1, 0.08}, true);
        ++dim;
      }
    }
    SetLists(&scenario, members, 128, NO_LIGHT);
    scenario.seed = 53;
    scenario.history_confidence = 4.f;
    SetCamera(&scenario, origin);
    Check("S5 128 lights, full list", scenario, lights, 32, true, false, false, origin, &warp, nullptr);
  }

  // S6: a light new this trace (its light_previous byte 255): last frame's reservoirs hold only the old one, its domain
  // gives the new one target 0 (else the new light's share drops to the candidates' M share: the as-built control).
  {
    const Vec3 direction = {0, 0, 1};
    ProbeScenario scenario = Domains({0, 0, 0}, {direction, direction, direction, direction}, {50, 50, 50, 50});
    const std::vector<ProbeLight> lights = {Light({2, 0, 20}, 8, {1, 1, 1}, true), Light({-2, 0, 30}, 8, {1, 1, 1}, true)};
    SetLists(&scenario, {0, 1}, 2, 1);
    scenario.seed = 61;
    scenario.history_confidence = 4.f;
    SetCamera(&scenario, {0, 0, 0});
    Check("S6 new light", scenario, lights, 2, false, false, true, {0, 0, 0}, &warp, nullptr);
  }

  std::printf("RESULT tw_fog_restir %s (%d failed)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
