// A compact draw's cluster set (twattila/rt_core.hpp PrepareClusterSet, bridge v16) when the game writes other indices into
// its index buffer's range: the set is keyed by the buffer and the range, which stay (Rome Remastered's battle resets,
// suspect 3: every one of its scene draws is dynamic, so compact). On WARP through the addon's own PrepareClusterSet and its
// write hooks (ReShade's update_buffer_region, update_buffer_region_command, copy_buffer_region, copy_resource).
// 1. A mesh's set from its indices (A: a 5 x 5 grid's 32 triangles) is MakeClusterSet's of them.
// 2. CONTROL (the behaviour before the hooks): the game writes B (other triangles of the grid) into the range and no hook
//    hears it: the set stays A's while the buffer holds B, 60 frames on (A's triangles laid over B's vertices).
// 3. The hook hears it: the set is not used from the write on (Capture's test), stays A's through the write's frame and the
//    next (that frame's trace sends what its earlier draws used; a deferred context's write lands when its list runs), is
//    then made from B and sent again (sent false).
// 4. Writes that miss: past the range in the same buffer, a constant buffer, a texture; nothing marked.
// 5. copy_buffer_region into the range, copy_resource over the whole buffer and a deferred update: marked as an update is.
// 6. An index buffer the GPU may write (stream output bind): no set (its triangles captured).
// 7. The hook's cost with no index buffer held (one atomic) and for constant buffer updates while one is (two COM calls).
// 8. R.U.S.E.'s draw replay (ruse/bridge_replay.hpp GetBuffer): a mirrored index buffer uploaded again with the native
//    UpdateSubresource, which no hook hears: GetBuffer tells rt_core, the set is marked and made again from the new indices.
// tests.exe
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
// rt_core alone (the cluster sets and their hooks): any game's embed folder that builds it serves. Case 8 with R.U.S.E.'s
// (amd64, run.ps1 defines RENODX_TEST_RUSE_REPLAY): its replay's embeds and rt_core's share one embed\shaders.h.
#include "src/games/twattila/rt_core.hpp"
#ifdef RENODX_TEST_RUSE_REPLAY
#include "src/games/ruse/bridge_replay.hpp"
#endif
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
namespace scene = twattila::rt_scene;
namespace bridge = twattila::dlaa_bridge;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Cluster rewrite test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

namespace {

constexpr UINT GRID = 5;  // vertices a side
constexpr UINT FIRST_INDEX = 6;  // the mesh's range starts past another mesh's two triangles
constexpr UINT TRIANGLES = (GRID - 1) * (GRID - 1) * 2;
constexpr UINT INDEX_COUNT = TRIANGLES * 3;
constexpr UINT BUFFER_INDICES = 256;

// A: the grid's quads split along one diagonal; B: along the other, from the grid's far corner (other triangles over the
// same vertices).
std::vector<uint32_t> GridIndices(bool other) {
  std::vector<uint32_t> indices;
  for (UINT y = 0; y + 1 < GRID; ++y) {
    for (UINT x = 0; x + 1 < GRID; ++x) {
      const uint32_t a = (y * GRID) + x;
      const uint32_t b = a + 1;
      const uint32_t c = a + GRID;
      const uint32_t d = c + 1;
      const std::array<uint32_t, 6> quad = other ? std::array<uint32_t, 6>{b, d, c, b, c, a} : std::array<uint32_t, 6>{a, c, d, a, d, b};
      indices.insert(indices.end(), quad.begin(), quad.end());
    }
  }
  if (other) std::ranges::reverse(indices);
  return indices;
}

}  // namespace

int main() {
  mock::Device device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.native.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    return 2;
  }
  mock::CommandList cmd_list;
  cmd_list.owner = &device;
  cmd_list.native = context;
  auto& runtime = scene::internal::runtime;
  const std::vector<uint32_t> indices_a = GridIndices(false);
  const std::vector<uint32_t> indices_b = GridIndices(true);
  const auto to_r16 = [](const std::vector<uint32_t>& indices) { return std::vector<uint16_t>(indices.begin(), indices.end()); };
  const auto make_buffer = [&](UINT bind) {
    std::vector<uint16_t> initial(BUFFER_INDICES, 0);
    const std::vector<uint16_t> a = to_r16(indices_a);
    std::ranges::copy(a, initial.begin() + FIRST_INDEX);
    const D3D11_BUFFER_DESC desc = {.ByteWidth = BUFFER_INDICES * 2, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = initial.data()};
    ComPtr<ID3D11Buffer> buffer;
    device.native->CreateBuffer(&desc, &data, buffer.GetAddressOf());
    return buffer;
  };
  const auto key_of = [](ID3D11Buffer* buffer) {
    uint64_t key = 14695981039346656037ull;
    for (const uint64_t value : {uint64_t{reinterpret_cast<uintptr_t>(buffer)}, uint64_t{DXGI_FORMAT_R16_UINT} << 32, uint64_t{FIRST_INDEX}, uint64_t{INDEX_COUNT}}) {
      key = (key ^ value) * 1099511628211ull;
    }
    return key;
  };
  // One frame: the mesh's draw asks for its set (Capture: compact and not yet blocks), then the present.
  const auto frame = [&](ID3D11Buffer* buffer) {
    {
      const std::unique_lock lock(runtime.mutex);
      scene::internal::PrepareClusterSet(context.Get(), key_of(buffer), buffer, DXGI_FORMAT_R16_UINT, 0, FIRST_INDEX, INDEX_COUNT);
    }
    context->Flush();
    const std::unique_lock lock(runtime.mutex);
    ++runtime.frame;
    runtime.set_builds = 0;
  };
  // Its set made (ready to go into a trace), else none; Capture's test for its blocks.
  const auto made = [&](ID3D11Buffer* buffer) -> const scene::internal::Runtime::ClusterMesh* {
    const std::unique_lock lock(runtime.mutex);
    const auto mesh = runtime.cluster_meshes.find(key_of(buffer));
    return (mesh != runtime.cluster_meshes.end() && mesh->second.remap) ? &mesh->second : nullptr;
  };
  const auto usable = [&](ID3D11Buffer* buffer) {
    const auto* mesh = made(buffer);
    return mesh != nullptr && mesh->rewritten_frame == 0;
  };
  const auto set_of = [](const std::vector<uint32_t>& indices) { return bridge::MakeClusterSet(indices).data; };
  const auto wait_made = [&](ID3D11Buffer* buffer, int frames) {
    for (int index = 0; index < frames && !usable(buffer); ++index) frame(buffer);
    // The worker's set taken at the next draw.
    for (int index = 0; index < 200 && !usable(buffer); ++index) {
      Sleep(1);
      frame(buffer);
    }
    return usable(buffer);
  };
  const auto write_b = [&](ID3D11Buffer* buffer) {
    const std::vector<uint16_t> b = to_r16(indices_b);
    const D3D11_BOX box = {.left = FIRST_INDEX * 2, .right = (FIRST_INDEX + INDEX_COUNT) * 2, .bottom = 1, .back = 1};
    context->UpdateSubresource(buffer, 0, &box, b.data(), 0, 0);
  };
  const reshade::api::resource no_resource = {0};

  // 1, 2. CONTROL: the write unheard.
  {
    CHECK(!scene::internal::index_buffers_held.load());
    const ComPtr<ID3D11Buffer> buffer = make_buffer(D3D11_BIND_INDEX_BUFFER);
    CHECK(wait_made(buffer.Get(), 40));
    const bool first_a = made(buffer.Get()) != nullptr && made(buffer.Get())->data == set_of(indices_a);
    CHECK(scene::internal::index_buffers_held.load());
    write_b(buffer.Get());
    for (int index = 0; index < 60; ++index) frame(buffer.Get());
    const bool still_a = usable(buffer.Get()) && made(buffer.Get())->data == set_of(indices_a);
    std::printf("1. set from A: %d; 2. CONTROL write unheard, 60 frames on: the set still A's over B's indices %d\n", first_a ? 1 : 0, still_a ? 1 : 0);
    CHECK(first_a);
    CHECK(still_a);
  }

  // 3. The hook hears it.
  {
    const ComPtr<ID3D11Buffer> buffer = make_buffer(D3D11_BIND_INDEX_BUFFER);
    CHECK(wait_made(buffer.Get(), 40));
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.cluster_meshes.at(key_of(buffer.Get())).sent = true;  // the trace took it
    }
    write_b(buffer.Get());
    const std::vector<uint16_t> b = to_r16(indices_b);
    scene::internal::OnUpdateIndices(&device, b.data(), {reinterpret_cast<uint64_t>(buffer.Get())}, FIRST_INDEX * 2, INDEX_COUNT * 2);
    const bool stopped = !usable(buffer.Get());
    const bool kept_a = made(buffer.Get()) != nullptr && made(buffer.Get())->data == set_of(indices_a);  // this frame's trace still sends A
    frame(buffer.Get());
    const bool next_frame_kept = made(buffer.Get()) != nullptr && !usable(buffer.Get());
    const bool remade = wait_made(buffer.Get(), 60);
    const auto* mesh = made(buffer.Get());
    const bool now_b = remade && mesh->data == set_of(indices_b);
    const bool to_send = remade && !mesh->sent;
    std::printf("3. heard: not used from the write %d, A kept for the write's frame %d and the next %d, made again from B %d, sent again %d\n",
                stopped ? 1 : 0, kept_a ? 1 : 0, next_frame_kept ? 1 : 0, now_b ? 1 : 0, to_send ? 1 : 0);
    CHECK(stopped && kept_a && next_frame_kept && now_b && to_send);

    // 4. Writes that miss.
    const std::array<uint16_t, 6> other = {1, 2, 3, 3, 2, 1};
    scene::internal::OnUpdateIndices(&device, other.data(), {reinterpret_cast<uint64_t>(buffer.Get())}, (FIRST_INDEX + INDEX_COUNT) * 2, sizeof(other));
    scene::internal::OnUpdateIndices(&device, other.data(), {reinterpret_cast<uint64_t>(buffer.Get())}, 0, FIRST_INDEX * 2);
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    ComPtr<ID3D11Buffer> constants;
    device.native->CreateBuffer(&constants_desc, nullptr, constants.GetAddressOf());
    scene::internal::OnUpdateIndices(&device, other.data(), {reinterpret_cast<uint64_t>(constants.Get())}, 0, 256);
    const D3D11_TEXTURE2D_DESC texture_desc = {.Width = 4, .Height = 4, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                               .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    ComPtr<ID3D11Texture2D> texture;
    device.native->CreateTexture2D(&texture_desc, nullptr, texture.GetAddressOf());
    scene::internal::OnCopyIndexBuffer(&cmd_list, no_resource, {reinterpret_cast<uint64_t>(texture.Get())});
    const bool missed = usable(buffer.Get());
    std::printf("4. writes past the range, before it, into a constant buffer, a texture: the set kept %d\n", missed ? 1 : 0);
    CHECK(missed);

    // 5. Copies and a deferred update.
    const auto marked_by = [&](const auto& write) {
      write();
      const bool marked = !usable(buffer.Get());
      wait_made(buffer.Get(), 60);
      return marked;
    };
    const bool by_copy = marked_by([&] {
      scene::internal::OnCopyIndices(&cmd_list, no_resource, 0, {reinterpret_cast<uint64_t>(buffer.Get())}, (FIRST_INDEX + 3) * 2, 12);
    });
    const bool by_copy_resource = marked_by([&] { scene::internal::OnCopyIndexBuffer(&cmd_list, no_resource, {reinterpret_cast<uint64_t>(buffer.Get())}); });
    const bool by_deferred = marked_by([&] {
      scene::internal::OnUpdateIndicesCommand(&cmd_list, b.data(), {reinterpret_cast<uint64_t>(buffer.Get())}, 0, UINT64_MAX);
    });
    std::printf("5. marked by copy_buffer_region %d, copy_resource %d, a deferred update %d\n", by_copy ? 1 : 0, by_copy_resource ? 1 : 0,
                by_deferred ? 1 : 0);
    CHECK(by_copy && by_copy_resource && by_deferred);

    // 7. The hook's cost (it runs for every buffer update the game makes).
    constexpr int CALLS = 200000;
    const auto time_calls = [&](ID3D11Buffer* target) {
      const auto started = std::chrono::steady_clock::now();
      for (int index = 0; index < CALLS; ++index) {
        scene::internal::OnUpdateIndices(&device, other.data(), {reinterpret_cast<uint64_t>(target)}, 0, 16);
      }
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() / CALLS;
    };
    const double held_ns = time_calls(constants.Get());
    scene::internal::index_buffers_held.store(false);
    const double idle_ns = time_calls(constants.Get());
    scene::internal::index_buffers_held.store(true);
    std::printf("7. a constant buffer update through the hook: %.1f ns with index buffers held, %.1f ns without\n", held_ns, idle_ns);
  }

  // 6. A GPU-writable index buffer.
  {
    const ComPtr<ID3D11Buffer> buffer = make_buffer(D3D11_BIND_INDEX_BUFFER | D3D11_BIND_STREAM_OUTPUT);
    for (int index = 0; index < 40; ++index) frame(buffer.Get());
    const std::unique_lock lock(runtime.mutex);
    const auto mesh = runtime.cluster_meshes.find(key_of(buffer.Get()));
    const bool none = mesh != runtime.cluster_meshes.end() && mesh->second.failed && !mesh->second.remap;
    std::printf("6. stream-output index buffer: no set %d\n", none ? 1 : 0);
    CHECK(none);
  }

  // 8. R.U.S.E.'s draw replay: the proxy's mirror of a D3D9 index buffer, uploaded again by GetBuffer's native
  //    UpdateSubresource (ReShade hears none of it; case 2 is the control). The game's write (its bytes, its unmap count)
  //    then the next GetBuffer: the same buffer, the set marked, made again from B.
#ifdef RENODX_TEST_RUSE_REPLAY
  {
    renodx::utils::device_proxy::proxy_device_reshade = &device;
    constexpr uint64_t GAME_BUFFER = 1;
    std::vector<uint16_t> initial(BUFFER_INDICES, 0);
    std::ranges::copy(to_r16(indices_a), initial.begin() + FIRST_INDEX);
    ruse::replay::state.mirrors.try_emplace(GAME_BUFFER, ruse::replay::Mirror{
        .bytes = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(initial.data()), reinterpret_cast<const uint8_t*>(initial.data() + initial.size())),
        .written = 1});
    ID3D11Buffer* buffer = ruse::replay::GetBuffer(device.native.Get(), context.Get(), GAME_BUFFER);
    CHECK(buffer != nullptr && wait_made(buffer, 40));
    const std::vector<uint16_t> b = to_r16(indices_b);
    ruse::replay::state.mirrors.modify_if(GAME_BUFFER, [&](auto& entry) {
      std::memcpy(entry.second.bytes.data() + (FIRST_INDEX * 2), b.data(), b.size() * 2);
      ++entry.second.written;
    });
    const bool same_buffer = ruse::replay::GetBuffer(device.native.Get(), context.Get(), GAME_BUFFER) == buffer;
    const bool marked = !usable(buffer);
    const bool remade = wait_made(buffer, 60) && made(buffer)->data == set_of(indices_b);
    std::printf("8. R.U.S.E.'s replay mirror uploaded again: the same buffer %d, the set marked %d, made again from B %d\n", same_buffer ? 1 : 0,
                marked ? 1 : 0, remade ? 1 : 0);
    CHECK(same_buffer && marked && remade);
    ruse::replay::state.mirrors.clear();
    renodx::utils::device_proxy::proxy_device_reshade = nullptr;
  }
#else
  std::printf("8. R.U.S.E.'s replay: not built (x86: no R.U.S.E. target)\n");
#endif

  {
    const std::unique_lock lock(runtime.mutex);
    runtime.cluster_meshes.clear();
    runtime.index_buffer_sets.clear();
  }
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
