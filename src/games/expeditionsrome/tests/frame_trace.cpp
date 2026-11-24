/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The one-frame tracer (src/games/talesofarise_dlss/frame_trace.hpp) as Tales and Expeditions run it, on WARP with
// ReShade's events and objects mocked (the utilities attached as the DevKit attaches them; real DXBC from D3DCompile).
// Built and run by test/tw_expeditions_unit/run.ps1; the trace files land next to the exe.
// 1. Tales (no options): a draw's line exactly as before, no mesh part (VS constant buffers and resources bound); the
//    view from UE 4.21's View rows (c122 / c123), none from 4.23's.
// 2. Expeditions (4.23's rows, c126 / c127): the view from them, none from 4.21's.
// 3. Expeditions' mesh identity: an indexed draw's, a draw's and an indirect draw's mesh part (arguments, then the index,
//    vertex and VS constant buffers and the VS resources by address), the header's mesh paragraph (not in Tales').
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "src/games/talesofarise_dlss/frame_trace.hpp"
#include "src/utils/hash.hpp"
#include "src/utils/shader.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
using api_event = reshade::addon_event;
using namespace reshade::api;  // NOLINT(google-build-using-namespace)

// ---- ReShade: the exports the utilities call, the event lists.
static std::array<std::vector<void*>, 512> event_lists;
extern "C" __declspec(dllexport) const char* const NAME = "Frame trace test";
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
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

struct PrivateData {
  std::unordered_map<std::string, uint64_t> values;
  void Get(const uint8_t guid[16], uint64_t* data) const {
    const auto found = values.find(std::string(reinterpret_cast<const char*>(guid), 16));
    *data = (found != values.end()) ? found->second : 0u;
  }
  void Set(const uint8_t guid[16], uint64_t data) {
    const std::string key(reinterpret_cast<const char*>(guid), 16);
    if (data != 0u) {
      values[key] = data;
    } else {
      values.erase(key);
    }
  }
};

struct TestDevice : mock::Device {
  PrivateData private_data;
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { private_data.Get(guid, data); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { private_data.Set(guid, data); }
};

struct TestList : mock::CommandList {
  PrivateData private_data;
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { private_data.Get(guid, data); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { private_data.Set(guid, data); }
};

static int failures = 0;

static void Expect(bool condition, std::string_view label) {
  if (condition) return;
  std::printf("FAIL: %.*s\n", static_cast<int>(label.size()), label.data());
  ++failures;
}

static void ExpectLine(const std::string& actual, const std::string& expected, std::string_view label) {
  if (actual == expected) return;
  std::printf("FAIL: %.*s\n  expected: %s\n  actual:   %s\n", static_cast<int>(label.size()), label.data(), expected.c_str(),
              actual.c_str());
  ++failures;
}

static std::vector<uint8_t> Compile(std::string_view source, const char* target) {
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", target, 0, 0, &blob, &errors))) return {};
  const auto* bytes = static_cast<const uint8_t*>(blob->GetBufferPointer());
  return {bytes, bytes + blob->GetBufferSize()};
}

int main() {
  ComPtr<ID3D11Device> native_device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                               native_device.GetAddressOf(), nullptr, context.GetAddressOf()))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }

  // As the DevKit attaches: shader hashes tracked, every pipeline's subobjects kept.
  renodx::utils::shader::use_shader_cache = true;
  renodx::utils::shader::Use(DLL_PROCESS_ATTACH);
  auto device = std::make_unique<TestDevice>();
  device->native = native_device;
  auto list = std::make_unique<TestList>();
  list->owner = device.get();
  list->native = context;
  auto* cmd_list = static_cast<command_list*>(list.get());
  InvokeVoid<api_event::init_device>(static_cast<reshade::api::device*>(device.get()));
  InvokeVoid<api_event::init_command_list>(cmd_list);

  const auto create_pipeline = [&](uint64_t handle, std::vector<pipeline_subobject> subobjects) {
    InvokeBool<api_event::create_pipeline>(static_cast<reshade::api::device*>(device.get()), pipeline_layout{0u},
                                           static_cast<uint32_t>(subobjects.size()), subobjects.data());
    InvokeVoid<api_event::init_pipeline>(static_cast<reshade::api::device*>(device.get()), pipeline_layout{0u},
                                         static_cast<uint32_t>(subobjects.size()), subobjects.data(), pipeline{handle});
  };
  const auto bind = [&](pipeline_stage stage, uint64_t handle) { InvokeVoid<api_event::bind_pipeline>(cmd_list, stage, pipeline{handle}); };

  const std::vector<uint8_t> vertex_code = Compile("float4 main(float3 p : POSITION) : SV_Position { return float4(p, 1); }", "vs_5_0");
  const std::vector<uint8_t> pixel_code = Compile("float4 main() : SV_Target { return 1; }", "ps_5_0");
  if (vertex_code.empty() || pixel_code.empty()) {
    std::printf("FAIL: shaders not compiled\n");
    return 1;
  }
  const uint32_t vertex_hash = renodx::utils::hash::ComputeCRC32(vertex_code.data(), vertex_code.size());
  const uint32_t pixel_hash = renodx::utils::hash::ComputeCRC32(pixel_code.data(), pixel_code.size());
  shader_desc vertex_desc = {.code = vertex_code.data(), .code_size = vertex_code.size()};
  shader_desc pixel_desc = {.code = pixel_code.data(), .code_size = pixel_code.size()};
  constexpr uint64_t VERTEX_PIPELINE = 0x1000;
  constexpr uint64_t PIXEL_PIPELINE = 0x2000;
  create_pipeline(VERTEX_PIPELINE, {{.type = pipeline_subobject_type::vertex_shader, .count = 1, .data = &vertex_desc}});
  create_pipeline(PIXEL_PIPELINE, {{.type = pipeline_subobject_type::pixel_shader, .count = 1, .data = &pixel_desc}});
  bind(pipeline_stage::vertex_shader, VERTEX_PIPELINE);
  bind(pipeline_stage::pixel_shader, PIXEL_PIPELINE);

  // The draw's state: a 64x32 RGBA8 target, viewport and scissor; positions (slot 0) and a dynamic stride-4 stream
  // (slot 1, from its second uint); 16-bit indices from byte 6.
  const auto make_buffer = [&](UINT bytes, D3D11_USAGE usage, UINT bindings, const void* contents) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = usage, .BindFlags = bindings,
                                    .CPUAccessFlags = (usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0u)};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = contents};
    ComPtr<ID3D11Buffer> buffer;
    native_device->CreateBuffer(&desc, &data, buffer.GetAddressOf());
    return buffer;
  };
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = 64, .Height = 32, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {.Count = 1},
                                            .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  native_device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf());
  native_device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const D3D11_VIEWPORT viewport = {.TopLeftX = 0.f, .TopLeftY = 0.f, .Width = 64.f, .Height = 32.f, .MinDepth = 0.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  const D3D11_RECT scissor = {.left = 0, .top = 0, .right = 64, .bottom = 32};
  context->RSSetScissorRects(1, &scissor);
  const std::array<float, 24> positions = {};
  const std::array<uint32_t, 5> ids = {7u, 9u, 11u, 13u, 15u};
  const std::array<uint16_t, 36> indices = {};
  const ComPtr<ID3D11Buffer> position_buffer = make_buffer(sizeof(positions), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, positions.data());
  const ComPtr<ID3D11Buffer> id_buffer = make_buffer(sizeof(ids), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, ids.data());
  const ComPtr<ID3D11Buffer> index_buffer = make_buffer(sizeof(indices), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, indices.data());
  const std::array<ID3D11Buffer*, 2> vertex_buffers = {position_buffer.Get(), id_buffer.Get()};
  const std::array<UINT, 2> strides = {12u, 4u};
  const std::array<UINT, 2> offsets = {0u, 4u};
  context->IASetVertexBuffers(0, 2, vertex_buffers.data(), strides.data(), offsets.data());
  context->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R16_UINT, 6);
  // The VS's own inputs: a constant buffer at b2 (a primitive's uniform buffer), a buffer at t3 (bones).
  const std::array<float, 16> primitive = {};
  const ComPtr<ID3D11Buffer> primitive_buffer = make_buffer(sizeof(primitive), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, primitive.data());
  context->VSSetConstantBuffers(2, 1, primitive_buffer.GetAddressOf());
  const ComPtr<ID3D11Buffer> bone_buffer = make_buffer(sizeof(primitive), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, primitive.data());
  D3D11_SHADER_RESOURCE_VIEW_DESC bone_view_desc = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  bone_view_desc.Buffer.NumElements = 4;
  ComPtr<ID3D11ShaderResourceView> bone_view;
  native_device->CreateShaderResourceView(bone_buffer.Get(), &bone_view_desc, bone_view.GetAddressOf());
  context->VSSetShaderResources(3, 1, bone_view.GetAddressOf());

  // A View uniform buffer (128 rows): the view size at `row`, the buffer size at the next.
  const auto make_view = [&](uint32_t row) {
    std::array<float, 128 * 4> rows = {};
    for (const auto& [at, width, height] : {std::tuple{row, 1920.f, 1080.f}, std::tuple{row + 1, 1920.f, 1088.f}}) {
      rows[at * 4] = width;
      rows[(at * 4) + 1] = height;
      rows[(at * 4) + 2] = 1.f / width;
      rows[(at * 4) + 3] = 1.f / height;
    }
    return make_buffer(sizeof(rows), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, rows.data());
  };
  const ComPtr<ID3D11Buffer> view_421 = make_view(122);
  const ComPtr<ID3D11Buffer> view_423 = make_view(126);

  // One traced frame (`draws` between the present that starts it and the one that writes <game>_frame_trace.txt): its
  // header and its draw lines.
  struct Trace {
    std::string header;
    std::vector<std::string> lines;
  };
  const auto trace = [&](std::string_view game, const talesofarise::frame_trace::Options& options, const std::function<void()>& draws) {
    talesofarise::frame_trace::Request();
    talesofarise::frame_trace::OnPresent(game, options);
    draws();
    talesofarise::frame_trace::OnPresent(game, options);
    std::ifstream file(talesofarise::frame_trace::internal::ExecutableFolder() / std::format("{}_frame_trace.txt", game));
    Trace result;
    for (std::string line; std::getline(file, line);) {
      if (line.starts_with("draw") || line.starts_with("indexed")) {
        result.lines.push_back(line);
      } else {
        result.header += line + "\n";
      }
    }
    return result;
  };
  const auto draw_indexed = [&] { InvokeBool<api_event::draw_indexed>(cmd_list, 36u, 2u, 3u, -1, 1u); };
  // A line's state up to its vertex buffers.
  const auto line_state = [&](std::string_view head) {
    return std::format(
        "{} vs {:08X} ps {:08X} | rt0 {} 64x32 f28 | vp 64x32 at 0,0 scissor 0,0-64,32 | | vb0 96 bytes stride 12 offset 0 "
        "vb1 20 bytes stride 4 offset 4 dynamic",
        head, vertex_hash, pixel_hash, static_cast<const void*>(target.Get()));
  };
  const std::string indexed_state = line_state("indexed 72");
  const std::string view_line = " | view 1920x1080 buffer 1920x1088";

  // 1. Tales.
  context->PSSetConstantBuffers(1, 1, view_421.GetAddressOf());
  auto traced = trace("trc_tales", {}, draw_indexed);
  Expect(traced.lines.size() == 1, "tales: one line");
  if (!traced.lines.empty()) ExpectLine(traced.lines[0], indexed_state + view_line, "tales: the line as before, its view from c122 / c123");
  Expect(traced.header.find("Mesh") == std::string::npos, "tales: no mesh paragraph in the header");
  context->PSSetConstantBuffers(1, 1, view_423.GetAddressOf());
  traced = trace("trc_tales", {}, draw_indexed);
  if (!traced.lines.empty()) ExpectLine(traced.lines[0], indexed_state, "tales: no view from c126 / c127");

  // 2. Expeditions' view rows.
  const talesofarise::frame_trace::Options view_423_only = {.view_size_row = 126};
  traced = trace("trc_expeditions", view_423_only, draw_indexed);
  Expect(traced.lines.size() == 1, "expeditions: one line");
  if (!traced.lines.empty()) ExpectLine(traced.lines[0], indexed_state + view_line, "expeditions: the view from c126 / c127");
  context->PSSetConstantBuffers(1, 1, view_421.GetAddressOf());
  traced = trace("trc_expeditions", view_423_only, draw_indexed);
  if (!traced.lines.empty()) ExpectLine(traced.lines[0], indexed_state, "expeditions: no view from c122 / c123");

  // 3. Expeditions' mesh identity (as dlss_graphics.hpp's OnPresent passes it).
  const talesofarise::frame_trace::Options expeditions = {.view_size_row = 126, .mesh_identity = true};
  const std::string buffers = std::format(" vb0 {}+0 vb1 {}+4 cb2 {} vt3 {}", static_cast<const void*>(position_buffer.Get()),
                                          static_cast<const void*>(id_buffer.Get()), static_cast<const void*>(primitive_buffer.Get()),
                                          static_cast<const void*>(bone_buffer.Get()));
  context->PSSetConstantBuffers(1, 1, view_423.GetAddressOf());
  traced = trace("trc_expeditions", expeditions, [&] {
    draw_indexed();
    InvokeBool<api_event::draw>(cmd_list, 6u, 4u, 2u, 1u);
    InvokeBool<api_event::draw_or_dispatch_indirect>(cmd_list, indirect_command::draw_indexed, resource{0u}, uint64_t{0}, 1u, 0u);
  });
  Expect(traced.lines.size() == 3, "expeditions mesh: three lines");
  Expect(traced.header.find("Mesh, after the vertex buffers") != std::string::npos, "expeditions mesh: the header's mesh paragraph");
  if (traced.lines.size() == 3) {
    ExpectLine(traced.lines[0],
               indexed_state
                   + std::format(" | mesh indices 36 first 3 base -1 instances 2 at 1 ib {}+6 f57", static_cast<const void*>(index_buffer.Get()))
                   + buffers + view_line,
               "expeditions mesh: an indexed draw");
    ExpectLine(traced.lines[1], line_state("draw 24") + " | mesh vertices 6 first 2 instances 4 at 1" + buffers, "expeditions mesh: a draw");
    ExpectLine(traced.lines[2], line_state("draw indirect 0") + " | mesh ?" + buffers, "expeditions mesh: an indirect draw");
  }

  talesofarise::frame_trace::Stop();
  if (failures != 0) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("frame_trace: all passed\n");
  return 0;
}
