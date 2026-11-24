/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The DevKit's shader dumper (src/utils/shader_dump.hpp) as the Expeditions dump run uses it, with ReShade's events and
// objects mocked (real DXBC from D3DCompile, no GPU): every stage's shader (vertex, hull, domain, geometry, pixel,
// compute) is dumped as <hash>.<kind>_5_0.cso, and the dump folder gets the stage record (dump_stages.txt). Built and
// run by test/tw_expeditions_unit/run.ps1; the dump goes to a folder of its own next to the exe, removed at the end.
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "src/utils/hash.hpp"
#include "src/utils/shader_dump.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
using api_event = reshade::addon_event;
using namespace reshade::api;  // NOLINT(google-build-using-namespace)

// ---- ReShade: the exports the utilities call, the event lists. No base path: the dumper writes next to the exe.
static std::array<std::vector<void*>, 512> event_lists;
extern "C" __declspec(dllexport) const char* const NAME = "Shader dump stages test";
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
__declspec(dllexport) void ReShadeGetBasePath(char*, size_t* path_size) { *path_size = 0; }
__declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char*, const char*, char*, size_t*) { return false; }
__declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char*, const char*, const char*) {}
__declspec(dllexport) void ReShadeRegisterEvent(api_event ev, void* callback) { event_lists[static_cast<size_t>(ev)].push_back(callback); }
__declspec(dllexport) void ReShadeRegisterEventForAddon(void*, api_event ev, void* callback) {
  event_lists[static_cast<size_t>(ev)].push_back(callback);
}
__declspec(dllexport) void ReShadeUnregisterEvent(api_event ev, void* callback) { std::erase(event_lists[static_cast<size_t>(ev)], callback); }
__declspec(dllexport) void ReShadeUnregisterEventForAddon(void*, api_event ev, void* callback) {
  std::erase(event_lists[static_cast<size_t>(ev)], callback);
}
}

template <api_event ev>
using Decl = typename reshade::addon_event_traits<ev>::decl;

template <api_event ev, typename... Args>
void InvokeVoid(Args... args) {
  const auto list = event_lists[static_cast<size_t>(ev)];
  for (void* callback : list) reinterpret_cast<Decl<ev>>(callback)(args...);
}

template <api_event ev, typename... Args>
bool InvokeBool(Args... args) {
  bool skip = false;
  const auto list = event_lists[static_cast<size_t>(ev)];
  for (void* callback : list) skip = reinterpret_cast<Decl<ev>>(callback)(args...) || skip;
  return skip;
}

struct TestDevice : mock::Device {
  std::unordered_map<std::string, uint64_t> private_data;
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override {
    const auto found = private_data.find(std::string(reinterpret_cast<const char*>(guid), 16));
    *data = (found != private_data.end()) ? found->second : 0u;
  }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override {
    private_data[std::string(reinterpret_cast<const char*>(guid), 16)] = data;
  }
};

int main() {
  // A dump folder of this run's own (a folder left by an earlier run would hold its shaders as dumped already).
  const std::string folder = std::format("dump_stages_test_{}", GetCurrentProcessId());
  renodx::utils::shader::dump::default_dump_folder = folder;
  renodx::utils::shader::dump::Use(DLL_PROCESS_ATTACH);
  auto device = std::make_unique<TestDevice>();
  InvokeVoid<api_event::init_device>(static_cast<reshade::api::device*>(device.get()));

  constexpr std::string_view STAGE_TYPES =
      "struct P { float4 p : SV_Position; };\n"
      "struct C { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n";
  struct Stage {
    pipeline_subobject_type type;
    const char* target;
    std::string source;
  };
  const std::array<Stage, 6> stages = {{
      {pipeline_subobject_type::vertex_shader, "vs_5_0", "float4 main(float3 p : POSITION) : SV_Position { return float4(p, 1); }"},
      {pipeline_subobject_type::hull_shader, "hs_5_0",
       std::format("{}C Patch(InputPatch<P, 3> ip) {{ C c; c.e[0] = 1; c.e[1] = 1; c.e[2] = 1; c.i = 1; return c; }}\n"
                   "[domain(\"tri\")] [partitioning(\"integer\")] [outputtopology(\"triangle_cw\")] [outputcontrolpoints(3)]\n"
                   "[patchconstantfunc(\"Patch\")] P main(InputPatch<P, 3> ip, uint i : SV_OutputControlPointID) {{ return ip[i]; }}",
                   STAGE_TYPES)},
      {pipeline_subobject_type::domain_shader, "ds_5_0",
       std::format("{}[domain(\"tri\")] P main(C c, float3 uvw : SV_DomainLocation, const OutputPatch<P, 3> patch) {{\n"
                   "  P o; o.p = (patch[0].p * uvw.x) + (patch[1].p * uvw.y) + (patch[2].p * uvw.z); return o; }}",
                   STAGE_TYPES)},
      {pipeline_subobject_type::geometry_shader, "gs_5_0",
       std::format("{}[maxvertexcount(3)] void main(triangle P input[3], inout TriangleStream<P> stream) {{\n"
                   "  for (uint i = 0; i < 3; ++i) stream.Append(input[i]); }}",
                   STAGE_TYPES)},
      {pipeline_subobject_type::pixel_shader, "ps_5_0", "float4 main() : SV_Target { return 1; }"},
      {pipeline_subobject_type::compute_shader, "cs_5_0", "RWBuffer<float> o : register(u0); [numthreads(1, 1, 1)] void main() { o[0] = 1; }"},
  }};

  int failures = 0;
  std::vector<std::filesystem::path> expected_files;
  uint64_t handle = 0x1000;
  for (const auto& stage : stages) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(stage.source.data(), stage.source.size(), nullptr, nullptr, nullptr, "main", stage.target, 0, 0,
                          blob.GetAddressOf(), errors.GetAddressOf()))) {
      std::printf("FAIL: %s not compiled: %s\n", stage.target,
                  (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?"));
      return 1;
    }
    shader_desc desc = {.code = blob->GetBufferPointer(), .code_size = blob->GetBufferSize()};
    pipeline_subobject subobject = {.type = stage.type, .count = 1, .data = &desc};
    InvokeBool<api_event::create_pipeline>(static_cast<reshade::api::device*>(device.get()), pipeline_layout{0u}, 1u, &subobject);
    InvokeVoid<api_event::init_pipeline>(static_cast<reshade::api::device*>(device.get()), pipeline_layout{0u}, 1u, &subobject,
                                         pipeline{handle});
    handle += 0x40;
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(static_cast<const uint8_t*>(blob->GetBufferPointer()), blob->GetBufferSize());
    expected_files.push_back(std::format("0x{:08X}.{}.cso", hash, stage.target));
  }

  if (renodx::utils::shader::dump::GetPendingDumpCount() != stages.size()) {
    std::printf("FAIL: %zu shaders pending, expected %zu\n", renodx::utils::shader::dump::GetPendingDumpCount(), stages.size());
    ++failures;
  }
  renodx::utils::shader::dump::DumpAllPending();
  const auto dump_folder = renodx::utils::path::GetOutputPath() / folder;
  for (const auto& name : expected_files) {
    if (!std::filesystem::exists(dump_folder / name)) {
      std::printf("FAIL: not dumped: %s\n", name.string().c_str());
      ++failures;
    }
  }
  std::stringstream record;
  record << std::ifstream(dump_folder / "dump_stages.txt").rdbuf();
  if (record.str() != "vs hs ds gs ps cs\n") {
    std::printf("FAIL: stage record \"%s\"\n", record.str().c_str());
    ++failures;
  }

  InvokeVoid<api_event::destroy_device>(static_cast<reshade::api::device*>(device.get()));
  renodx::utils::shader::dump::Use(DLL_PROCESS_DETACH);
  std::filesystem::remove_all(dump_folder);
  if (failures != 0) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("shader_dump_stages: all passed\n");
  return 0;
}
