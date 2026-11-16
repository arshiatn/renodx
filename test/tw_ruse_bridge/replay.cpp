// H3 (tools/nontw/ruse/plan.md 11b): R.U.S.E.'s Scene Replay (src/games/ruse/bridge_replay.hpp) on a D3D11 device,
// argv[3] warp or gpu. The cases (argv[1], tools/nontw/ruse/scripts/ru_replay_check.py prepare: census vertex shaders
// with a B0 trace push of their constants, a D3D9 declaration, its streams' and indices' bytes, the draw's ranges) go
// through the replay's own events (the declaration, the game's map / unmap of each buffer, the binds, the constants),
// each recorded as Record records a scene draw, all replayed in one frame into the stream output. argv[2]: the replay's
// log line, then its clip positions in draw order, for the script's check against the game's vs_3_0 in the SM3
// interpreter on the same bytes.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/games/ruse/bridge_replay.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

static std::string replay_log;
extern "C" __declspec(dllexport) const char* const NAME = "R.U.S.E. replay test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  std::printf("  log: %s\n", message);
  replay_log = message;
}

using Microsoft::WRL::ComPtr;
namespace replay = ruse::replay;

// The game's device as the replay's buffer events see it: each buffer's size.
struct GameDevice : mock::Device {
  std::unordered_map<uint64_t, uint64_t> sizes;
  reshade::api::device_api get_api() const override { return reshade::api::device_api::d3d9; }
  reshade::api::resource_desc get_resource_desc(reshade::api::resource resource) const override {
    return {sizes.at(resource.handle), reshade::api::memory_heap::cpu_to_gpu, reshade::api::resource_usage::vertex_buffer};
  }
};

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf("usage: ruse_replay.exe <cases.bin> <positions.bin> warp|gpu\n");
    return 2;
  }
  std::ifstream input(argv[1], std::ios::binary);
  const std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  size_t at = 0;
  const auto read = [&]<typename T>(T* value) {
    std::memcpy(value, bytes.data() + at, sizeof(T));
    at += sizeof(T);
  };
  const auto read_bytes = [&](size_t count) {
    std::vector<uint8_t> result(bytes.begin() + at, bytes.begin() + at + count);
    at += count;
    return result;
  };

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  const bool warp = std::string(argv[3]) == "warp";
  if (FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                               device.GetAddressOf(), nullptr, context.GetAddressOf()))) {
    std::printf("no %s device\n", argv[3]);
    return 1;
  }
  mock::Device proxy;
  proxy.native = device;
  renodx::utils::device_proxy::proxy_device_reshade = &proxy;
  GameDevice game;
  mock::CommandList command_list;
  command_list.owner = &game;
  auto& state = replay::state;
  state.enabled = true;
  state.log_next = true;

  // Each buffer's bytes stay alive until the replay's unmap copied them (the game's Lock pointer).
  uint32_t case_count = 0;
  read(&case_count);
  for (uint32_t index = 0; index < case_count; ++index) {
    uint32_t hash = 0;
    uint32_t constant_count = 0;
    read(&hash);
    read(&constant_count);
    std::vector<float> constants(constant_count);
    std::memcpy(constants.data(), bytes.data() + at, constant_count * sizeof(float));
    at += constant_count * sizeof(float);
    uint32_t element_count = 0;
    read(&element_count);
    std::vector<std::string> semantics(element_count);
    std::vector<reshade::api::input_element> elements(element_count);
    for (uint32_t element = 0; element < element_count; ++element) {
      uint32_t length = 0;
      read(&length);
      semantics[element].assign(bytes.data() + at, length);
      at += length;
      uint32_t format = 0;
      read(&elements[element].semantic_index);
      read(&elements[element].buffer_binding);
      read(&elements[element].offset);
      read(&format);
      elements[element].format = static_cast<reshade::api::format>(format);
    }
    for (uint32_t element = 0; element < element_count; ++element) elements[element].semantic = semantics[element].c_str();
    std::array<uint64_t, 4> handles = {};
    for (auto& handle : handles) read(&handle);
    const reshade::api::pipeline_subobject layout = {.type = reshade::api::pipeline_subobject_type::input_layout, .count = element_count,
                                                     .data = elements.data()};
    replay::OnInitPipeline(&game, {}, 1, &layout, {handles[0]});
    std::array<reshade::api::resource, replay::STREAMS> streams = {};
    std::array<uint64_t, replay::STREAMS> offsets = {};
    std::array<uint32_t, replay::STREAMS> strides = {};
    const auto upload = [&](uint64_t handle, std::vector<uint8_t> data) {
      game.sizes[handle] = data.size();
      void* pointer = data.data();
      replay::OnMapBuffer(&game, {handle}, 0, UINT64_MAX, reshade::api::map_access::write_only, &pointer);
      replay::OnUnmapBuffer(&game, {handle});
    };
    for (uint32_t stream = 0; stream < 2; ++stream) {
      uint32_t stride = 0;
      uint32_t offset = 0;
      uint32_t size = 0;
      read(&stride);
      read(&offset);
      read(&size);
      if (stride == 0) continue;
      upload(handles[1 + stream], read_bytes(size));
      streams[stream] = {handles[1 + stream]};
      offsets[stream] = offset;
      strides[stream] = stride;
    }
    uint32_t index_offset = 0;
    uint32_t index_size = 0;
    read(&index_offset);
    read(&index_size);
    upload(handles[3], read_bytes(index_size));
    uint32_t index_count = 0;
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
    read(&index_count);
    read(&first_index);
    read(&vertex_offset);

    replay::OnBindPipeline(&command_list, reshade::api::pipeline_stage::input_assembler, {handles[0]});
    replay::OnBindVertexBuffers(&command_list, 0, replay::STREAMS, streams.data(), offsets.data(), strides.data());
    replay::OnBindIndexBuffer(&command_list, {handles[3]}, index_offset, 2);
    const reshade::api::dynamic_state topology = reshade::api::dynamic_state::primitive_topology;
    const uint32_t triangle_list = static_cast<uint32_t>(reshade::api::primitive_topology::triangle_list);
    replay::OnBindPipelineStates(&command_list, 1, &topology, &triangle_list);
    replay::OnPushConstants(2, 0, constant_count, constants.data());
    // Record's draw (its shader found by hash: GetCurrentShaderHash needs ReShade's pipeline tracking).
    const auto found = std::ranges::lower_bound(replay::SHADERS, hash, {}, &replay::ReplayShader::hash);
    if (found == replay::SHADERS.end() || found->hash != hash) {
      std::printf("0x%08X: not in replay_shaders.h\n", hash);
      return 1;
    }
    ++state.counts.recorded;
    const size_t recorded = state.constants.size();
    state.constants.insert(state.constants.end(), state.vertex_constants.begin(), state.vertex_constants.begin() + (found->registers * 4));
    state.draws.push_back({
        .shader = &*found,
        .declaration = state.declaration,
        .streams = state.streams,
        .offsets = state.offsets,
        .strides = state.strides,
        .index_buffer = state.index_buffer,
        .index_offset = state.index_offset,
        .index_size = state.index_size,
        .index_count = index_count,
        .first_index = first_index,
        .vertex_offset = vertex_offset,
        .topology = state.topology,
        .constants = recorded,
    });
  }
  const size_t draws = state.draws.size();
  replay::Replay({});

  // The stream output: up to the cases' vertices (each 3 per triangle, 16 bytes).
  D3D11_BUFFER_DESC desc = {};
  state.positions->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  if (FAILED(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()))) return 1;
  context->CopyResource(staging.Get(), state.positions.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 1;
  // The primitive count from the replay's log line ("... replayed (N triangles").
  const size_t triangles_at = replay_log.find(" triangles");
  const size_t number_at = replay_log.rfind('(', triangles_at);
  const size_t triangles = (triangles_at == std::string::npos) ? 0 : std::stoull(replay_log.substr(number_at + 1, triangles_at - number_at - 1));
  std::ofstream output(argv[2], std::ios::binary);
  const auto length = static_cast<uint32_t>(replay_log.size());
  output.write(reinterpret_cast<const char*>(&length), sizeof(length));
  output.write(replay_log.data(), length);
  output.write(static_cast<const char*>(mapped.pData), static_cast<std::streamsize>(triangles * 3 * 16));
  context->Unmap(staging.Get(), 0);
  std::printf("%s: %zu draws recorded, %zu triangles streamed out\n", argv[3], draws, triangles);
  renodx::utils::device_proxy::proxy_device_reshade = nullptr;
  return 0;
}
