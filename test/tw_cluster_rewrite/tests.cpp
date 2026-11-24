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
// 9. A loading screen (Update with Host::scene_release: SCENE_GONE_FRAMES presents in front without a scene draw, and
//    SCENE_GONE_MS since it), the game's foreground test answered by the test (internal::game_in_front; the real foreground
//    never taken). The scene held: a mesh's set (the game's index buffer), a mask's atlas slice (the game's texture), a
//    cut-out texture, a known mesh, this frame's new set.
//    a. In front: nothing at 29 presents; at 30 all let go (the buffer's and the texture's references one fewer, the slice
//       free again, the cut-out pointer no longer cut out, scene_gone), one line saying so with the free address space
//       before it (32-bit) and the address space with its largest free block 30 presents later (the release's one walk);
//       drawn again, the set made again and to be sent.
//    b. Out of front for 100 presents: all kept, no line, not scene_gone. CONTROL and an alt-tab across present 30: 20 in
//       front, 20 out, 9 in front kept; the 10th lets go.
//    c. Static-only frames (every draw a known mesh's, so no main-view capture): a scene draw each frame (scene_frame,
//       main_view_frame left behind), 100 presents in front: all kept.
//    d. Host::keep_placed (WH3) without scene_release: out of front, at 30 presents its known meshes forgotten (scene_gone),
//       the rest kept, also 100 presents in front on; with scene_release, in front: all let go at 30.
//    e. The rays off: 100 presents in front, all kept.
//    f. R.U.S.E.'s replayed frames: MarkDrawn each present, no capture, 100 presents in front: all kept; CONTROL none marked.
//    g. Without Host::scene_release (the other hosts, RENODX_TW_SCENE_RELEASE=0): 100 presents in front, all kept, the
//       foreground not asked.
//    h. The clock: 40 presents in front within SCENE_GONE_MS of the scene's draw: kept, counted to 30 (the foreground asked
//       30 times, not after); a second on, let go.
//    i. Nothing held: let go without a line.
//    j. The line's floor (the last line's time put 9 s back before each screen: no run's clock decides): a line, then 7
//       loading screens each let go without one; CONTROL 10 s back: a line.
//    k. The release's later walk and the chain's L3 walk due in one present (30 presents after a release): L3's then, the
//       later line one present on ("31 presents after", 32-bit), one walk a present.
//    l. The game's device gone (OnDestroyDevice's ForgetScene): this frame's new sets and compact draws go with the sets.
// 10. The address space's chain (rt_core.hpp): armed at the helper's let go only once its present callback ran
//     (OnPresentAddressSpace: the host registered it) and not mid-chain (32-bit; 64-bit never); its last lines (armed by a
//     trace two presents on): over 100 presents, once each and in order, that frame's present callbacks done
//     (OnPresentAddressSpace, the chain's one walk: the largest free block), then the next present's (Update, the free
//     total alone).
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
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { log_lines.emplace_back(message); }

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
    // Its failed entry goes with the buffer (it holds none): a later buffer at its address (case 9's on x86) is another mesh.
    runtime.cluster_meshes.erase(key_of(buffer.Get()));
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

  // 9. A loading screen: Update's foreground test answered here (the real foreground never asked or taken).
  {
    static bool in_front = true;
    static int front_asked = 0;
    scene::internal::game_in_front = [] {
      ++front_asked;
      return in_front;
    };
    scene::internal::host.scene_release = true;
    constexpr uint64_t KNOWN_KEY = 0x9001;
    constexpr uint32_t SLICE = 7;
    scene::Settings settings = {.ambient_occlusion = true};
    const ComPtr<ID3D11Buffer> buffer = make_buffer(D3D11_BIND_INDEX_BUFFER);
    const D3D11_TEXTURE2D_DESC mask_desc = {.Width = 4, .Height = 4, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                            .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    ComPtr<ID3D11Texture2D> mask;
    device.native->CreateTexture2D(&mask_desc, nullptr, mask.GetAddressOf());
    const auto references = [](IUnknown* object) {
      object->AddRef();
      return object->Release();
    };
    // `count` presents, the game in front or not; a scene draw before each (Capture's scene_frame) or none.
    const auto presents = [&](int count, bool front, bool scene_drawn) {
      in_front = front;
      for (int index = 0; index < count; ++index) {
        if (scene_drawn) {
          const std::unique_lock lock(runtime.mutex);
          runtime.scene_frame = runtime.frame;
        }
        scene::Update(settings);
      }
    };
    // The scene held: the mesh's set made, the mask's atlas slice, the mask a cut-out texture, a known mesh; then a present
    // that drew it, put SCENE_GONE_MS back (the presents alone decide); the release line allowed again or not.
    const auto hold_scene = [&](bool new_line) {
      const bool set_made = wait_made(buffer.Get(), 40);
      {
        const std::unique_lock lock(runtime.mutex);
        std::erase(runtime.free_slices, SLICE);
        runtime.atlas_slices.emplace(mask.Get(), std::pair{ComPtr<ID3D11Resource>(mask), SLICE});
        runtime.cut_out_textures.insert(mask.Get());
        runtime.known[KNOWN_KEY] = {.frame = runtime.frame};
        runtime.short_keys[KNOWN_KEY] = {.key = KNOWN_KEY};
        runtime.scene_gone = false;
        if (new_line) runtime.release_line_tick = 0;
      }
      presents(1, true, true);
      const std::unique_lock lock(runtime.mutex);
      runtime.scene_tick -= scene::internal::SCENE_GONE_MS;
      return set_made;
    };
    // The set, the slice, the cut-out texture, the known mesh.
    const auto held = [&] {
      const std::unique_lock lock(runtime.mutex);
      return std::array{runtime.cluster_meshes.contains(key_of(buffer.Get())), runtime.atlas_slices.contains(mask.Get()),
                        runtime.cut_out_textures.contains(mask.Get()), runtime.known.contains(KNOWN_KEY)};
    };
    constexpr std::array ALL = {true, true, true, true};
    constexpr std::array NONE = {false, false, false, false};
    const auto lines_with = [](const char* text) {
      return std::ranges::count_if(log_lines, [&](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    const auto loading_lines = [&] { return lines_with("(a loading screen)"); };

    // a. In front.
    const bool a_made = hold_scene(true);
    const ULONG buffer_references = references(buffer.Get());
    const ULONG mask_references = references(mask.Get());
    presents(29, true, false);
    const bool kept_29 = held() == ALL && loading_lines() == 0;
    std::string counts;
    {
      const std::unique_lock lock(runtime.mutex);
      counts = std::format("let go {} known meshes, {} cluster sets, {} atlas slices, {} cut-out textures", runtime.known.size(),
                           runtime.cluster_meshes.size(), runtime.atlas_slices.size(), runtime.cut_out_textures.size());
    }
    presents(1, true, false);
    bool let_go = held() == NONE;
    {
      const std::unique_lock lock(runtime.mutex);
      let_go = let_go && std::ranges::count(runtime.free_slices, SLICE) == 1 && runtime.scene_gone && runtime.short_keys.empty()
               && runtime.index_buffer_sets.empty() && !scene::internal::index_buffers_held.load();
    }
    const ULONG buffer_references_after = references(buffer.Get());
    const ULONG mask_references_after = references(mask.Get());
    const bool references_down = buffer_references_after + 1 == buffer_references && mask_references_after + 1 == mask_references;
    const auto line = std::ranges::find_if(log_lines, [](const std::string& text) { return text.find("(a loading screen)") != std::string::npos; });
    const bool line_counts = line != log_lines.end() && line->find(counts) != std::string::npos;
#if defined(_WIN64)
    const bool address_space = line != log_lines.end() && line->find("address space") == std::string::npos;
    constexpr long long LATER_LINES = 0;
#else
    const bool address_space = line != log_lines.end() && line->find("; before it: address space free ") != std::string::npos
                               && line->find("after") == std::string::npos && line->find("largest free block") == std::string::npos;
    constexpr long long LATER_LINES = 1;
#endif
    const auto later_lines = [&] { return lines_with("presents after the loading screen's release: address space free "); };
    const auto walked_lines = [&] { return lines_with("largest free block"); };
    const long long walks_before = walked_lines();
    presents(29, true, false);
    const bool not_yet = later_lines() == 0;
    presents(11, true, false);
    const bool once = loading_lines() == 1 && later_lines() == LATER_LINES && walked_lines() - walks_before == LATER_LINES;
    const bool remade = wait_made(buffer.Get(), 40) && !made(buffer.Get())->sent;
    std::printf("9a. in front: the set made %d, kept at 29 presents %d, let go at 30 %d (references one fewer %d: the buffer's %lu -> %lu, the "
                "mask's %lu -> %lu), one line with the counts %d and the address space before it %d, none 29 presents on %d, once each "
                "(the address space 30 presents on: 32-bit) %d; drawn again: the set made again to send %d\n",
                a_made ? 1 : 0, kept_29 ? 1 : 0, let_go ? 1 : 0, references_down ? 1 : 0, buffer_references, buffer_references_after,
                mask_references, mask_references_after, line_counts ? 1 : 0, address_space ? 1 : 0, not_yet ? 1 : 0, once ? 1 : 0,
                remade ? 1 : 0);
    CHECK(a_made && kept_29 && let_go && references_down && line_counts && address_space && not_yet && once && remade);

    // b. Out of front; CONTROL: an alt-tab across present 30.
    const bool b_made = hold_scene(true);
    presents(100, false, false);
    const bool kept_out = held() == ALL && loading_lines() == 1 && !runtime.scene_gone;
    presents(20, true, false);
    presents(20, false, false);
    presents(9, true, false);
    const bool kept_alt_tab = held() == ALL && loading_lines() == 1;
    presents(1, true, false);
    const bool alt_tab_let_go = held() == NONE && loading_lines() == 2;
    std::printf("9b. out of front 100 presents: kept %d; CONTROL 20 in, 20 out, 9 in: kept %d, the 10th lets go %d\n", kept_out ? 1 : 0,
                kept_alt_tab ? 1 : 0, alt_tab_let_go ? 1 : 0);
    CHECK(b_made && kept_out && kept_alt_tab && alt_tab_let_go);

    // c. Static-only frames: every draw a known mesh's (no main-view capture), the scene drawn each frame.
    const bool c_made = hold_scene(true);
    presents(100, true, true);
    const bool kept_static = held() == ALL && loading_lines() == 2 && runtime.frame - runtime.main_view_frame > 100;
    std::printf("9c. static-only frames (main_view_frame left behind), 100 presents in front: kept %d\n", kept_static ? 1 : 0);
    CHECK(c_made && kept_static);

    // d. Host::keep_placed (WH3: no scene_release); then with scene_release.
    const float one = 1.f;
    scene::internal::host.keep_placed = true;
    scene::internal::host.placed_row = &one;
    scene::internal::host.scene_release = false;
    settings.casters_out_of_view = true;
    const bool d_made = hold_scene(true);
    presents(29, false, false);
    const bool placed_kept_29 = held() == ALL;
    presents(1, false, false);
    constexpr std::array KNOWN_FORGOTTEN = {true, true, true, false};
    bool forgotten = held() == KNOWN_FORGOTTEN && loading_lines() == 2;
    {
      const std::unique_lock lock(runtime.mutex);
      forgotten = forgotten && runtime.short_keys.empty() && runtime.scene_gone;
    }
    presents(100, true, false);
    const bool rest_kept = held() == KNOWN_FORGOTTEN && loading_lines() == 2;
    scene::internal::host.scene_release = true;
    const bool d_made_again = hold_scene(true);
    presents(30, true, false);
    const bool placed_let_go = held() == NONE && loading_lines() == 3;
    scene::internal::host.keep_placed = false;
    scene::internal::host.placed_row = nullptr;
    settings.casters_out_of_view = false;
    std::printf("9d. keep_placed: kept at 29 %d, at 30 (out of front) its known meshes forgotten, the rest kept %d, also 100 presents in "
                "front on %d; with scene_release, in front: all let go at 30 %d\n",
                placed_kept_29 ? 1 : 0, forgotten ? 1 : 0, rest_kept ? 1 : 0, placed_let_go ? 1 : 0);
    CHECK(d_made && placed_kept_29 && forgotten && rest_kept && d_made_again && placed_let_go);

    // e. The rays off: nothing counted.
    settings = {};
    const bool e_made = hold_scene(true);
    presents(100, true, false);
    const bool kept_off = held() == ALL && loading_lines() == 3 && runtime.sceneless_presents == 0;
    settings = {.ambient_occlusion = true};
    std::printf("9e. the rays off, 100 presents in front: kept %d\n", kept_off ? 1 : 0);
    CHECK(e_made && kept_off);

    // f. R.U.S.E.'s replayed frames: every mesh marked drawn (MarkDrawn), none captured, each present SCENE_GONE_MS after the
    //    last scene draw it knew of (the presents alone decide); CONTROL none marked.
    const bool f_made = hold_scene(true);
    in_front = true;
    bool marked = true;
    for (int present = 0; present < 100; ++present) {
      marked = scene::MarkDrawn(KNOWN_KEY) && marked;
      {
        const std::unique_lock lock(runtime.mutex);
        runtime.scene_tick = GetTickCount64() - scene::internal::SCENE_GONE_MS;
      }
      scene::Update(settings);
    }
    const bool kept_marked = marked && held() == ALL && loading_lines() == 3 && runtime.sceneless_presents == 0;
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.scene_tick -= scene::internal::SCENE_GONE_MS;
    }
    presents(30, true, false);
    const bool unmarked_let_go = held() == NONE && loading_lines() == 4;
    std::printf("9f. MarkDrawn each present, 100 presents in front: kept %d; CONTROL none marked: let go at 30 %d\n", kept_marked ? 1 : 0,
                unmarked_let_go ? 1 : 0);
    CHECK(f_made && kept_marked && unmarked_let_go);

    // g. Without Host::scene_release (the other hosts; RENODX_TW_SCENE_RELEASE=0): nothing counted, the foreground not asked.
    const bool g_made = hold_scene(true);
    scene::internal::host.scene_release = false;
    front_asked = 0;
    presents(100, true, false);
    const bool kept_without = held() == ALL && loading_lines() == 4 && front_asked == 0;
    scene::internal::host.scene_release = true;
    std::printf("9g. without Host::scene_release, 100 presents in front: kept, the foreground not asked %d\n", kept_without ? 1 : 0);
    CHECK(g_made && kept_without);

    // h. The clock: presents within SCENE_GONE_MS of the scene's draw keep it (its time put 60 s ahead: no run's clock passes
    //    it), counted to 30 and no further (the foreground not asked after); a second after the draw, the next present lets go.
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.scene_tick = GetTickCount64() + 60000;
    }
    front_asked = 0;
    presents(40, true, false);
    const bool kept_by_clock = held() == ALL && loading_lines() == 4 && runtime.sceneless_presents == scene::internal::SCENE_GONE_FRAMES
                               && front_asked == static_cast<int>(scene::internal::SCENE_GONE_FRAMES);
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.scene_tick = GetTickCount64() - scene::internal::SCENE_GONE_MS;
    }
    presents(1, true, false);
    const bool clock_let_go = held() == NONE && loading_lines() == 5;
    std::printf("9h. 40 presents in front within a second of the scene: kept, counted to 30, the foreground asked 30 times %d; a second "
                "on: let go %d\n",
                kept_by_clock ? 1 : 0, clock_let_go ? 1 : 0);
    CHECK(kept_by_clock && clock_let_go);

    // i. Nothing held (the front end before the helper's first trace): let go without a line.
    bool empty = false;
    {
      const std::unique_lock lock(runtime.mutex);
      empty = runtime.known.empty() && runtime.cluster_meshes.empty() && runtime.atlas_slices.empty() && runtime.cut_out_textures.empty();
      runtime.release_line_tick = 0;
      runtime.scene_gone = false;
    }
    presents(1, true, true);
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.scene_tick -= scene::internal::SCENE_GONE_MS;
    }
    presents(30, true, false);
    const bool no_empty_line = empty && runtime.scene_gone && loading_lines() == 5;
    std::printf("9i. nothing held, 30 presents in front: let go, no line %d\n", no_empty_line ? 1 : 0);
    CHECK(no_empty_line);

    // j. The line's floor: a loading screen's line (its time kept), then 7 screens each let go without one while the last
    //    line is 9 s back (put there before each: no run's clock decides); CONTROL 10 s back: a line.
    const long long lines_before = loading_lines();
    const uint64_t first_tick = GetTickCount64();
    bool each_let_go = hold_scene(true);
    presents(30, true, false);
    each_let_go = each_let_go && held() == NONE;
    bool line_tick_kept = false;
    {
      const std::unique_lock lock(runtime.mutex);
      line_tick_kept = runtime.release_line_tick >= first_tick && runtime.release_line_tick <= GetTickCount64();
    }
    const auto screen_after_line = [&](uint64_t line_ago) {
      const bool screen_made = hold_scene(false);
      {
        const std::unique_lock lock(runtime.mutex);
        runtime.release_line_tick = GetTickCount64() - line_ago;
      }
      presents(30, true, false);
      return screen_made && held() == NONE;
    };
    for (int screen = 1; screen < 8; ++screen) each_let_go = screen_after_line(scene::internal::RELEASE_LINE_MS - 1000) && each_let_go;
    const bool one_line = line_tick_kept && loading_lines() - lines_before == 1;
    const bool control_let_go = screen_after_line(scene::internal::RELEASE_LINE_MS);
    const bool control_line = loading_lines() - lines_before == 2;
    std::printf("9j. a loading screen's line (its time kept %d), 7 more 9 s after it: each let go %d, one line %d; CONTROL 10 s after: a "
                "line %d\n",
                line_tick_kept ? 1 : 0, each_let_go ? 1 : 0, one_line ? 1 : 0, control_let_go && control_line ? 1 : 0);
    CHECK(each_let_go && one_line && control_let_go && control_line);

    // k. The release's later walk and the chain's L3 walk due in one present (the scene back 30 presents after a release,
    //    the helper's chain running).
    const bool k_made = hold_scene(true);
    presents(30, true, false);
    const bool k_let_go = held() == NONE;
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.address_space_present = runtime.frame + scene::internal::SCENE_GONE_FRAMES + 1;
    }
    const auto l3_lines = [&] { return lines_with("that frame's present callbacks done: "); };
    const auto later_31_lines = [&] { return lines_with("31 presents after the loading screen's release: "); };
    const long long later_31 = later_31_lines();
    long long most_walks = 0;
    int l3_present = 0;
    int later_present = 0;
    for (int present = 1; present <= 32; ++present) {
      const long long walks = walked_lines();
      const long long l3 = l3_lines();
      const long long later = later_lines();
      scene::Update(settings);
      scene::OnPresentAddressSpace(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
      most_walks = std::max(most_walks, walked_lines() - walks);
      if (l3_lines() != l3) l3_present = present;
      if (later_lines() != later) later_present = present;
    }
#if defined(_WIN64)
    constexpr int LATER_PRESENT = 0;  // no later line
#else
    constexpr int LATER_PRESENT = 31;
#endif
    const bool later_text = later_31_lines() - later_31 == (LATER_PRESENT == 0 ? 0 : 1);
    const bool apart = k_made && k_let_go && l3_present == 30 && later_present == LATER_PRESENT && later_text && most_walks == 1
                       && runtime.address_space_present == 0 && runtime.address_space_release == 0;
    std::printf("9k. the later walk and L3's due in one present: L3's at 30 (%d), the later line at 31 (32-bit; %d), one walk a present "
                "(%lld) %d\n",
                l3_present, later_present, most_walks, apart ? 1 : 0);
    CHECK(apart);

    // l. The game's device gone mid-frame.
    const bool l_made = wait_made(buffer.Get(), 40);
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.new_sets.push_back(key_of(buffer.Get()));
      runtime.new_set_bytes = 64;
      runtime.compact.emplace_back();
    }
    scene::OnDestroyDevice(&device);
    bool device_gone = false;
    {
      const std::unique_lock lock(runtime.mutex);
      device_gone = runtime.new_sets.empty() && runtime.new_set_bytes == 0 && runtime.compact.empty() && runtime.cluster_meshes.empty();
    }
    std::printf("9l. the device gone: the frame's new sets and compact draws gone with the sets %d\n", device_gone ? 1 : 0);
    CHECK(l_made && device_gone);
    scene::internal::host.scene_release = false;
    scene::internal::game_in_front = twattila::device_end::GameInFront;
  }

  // 10. The address space's chain.
  {
#if defined(_WIN64)
    constexpr bool ARMED = false;  // no chain
#else
    constexpr bool ARMED = true;
#endif
    // The helper's scene let go (its ray tracing device other than the one seen: RayTracingDevice is 0 without a helper), a
    // chain's present waiting (0: none): the chain armed?
    const auto armed_at_let_go = [&](uint64_t chain_present) {
      {
        const std::unique_lock lock(runtime.mutex);
        runtime.helper_device = 1;
        runtime.address_space_capture = false;
        runtime.address_space_present = chain_present;
      }
      scene::Update({.ambient_occlusion = true});
      const std::unique_lock lock(runtime.mutex);
      runtime.address_space_present = 0;
      return std::exchange(runtime.address_space_capture, false);
    };
    scene::internal::address_space_callback.store(false);
    const bool without_callback = !armed_at_let_go(0);
    scene::OnPresentAddressSpace(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    const bool with_callback = armed_at_let_go(0) == ARMED;
    const bool mid_chain = !armed_at_let_go(runtime.frame + 5);
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.address_space_present = runtime.frame + 2;
    }
    const size_t first_line = log_lines.size();
    for (int present = 0; present < 100; ++present) {
      scene::Update({});
      scene::OnPresentAddressSpace(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    }
    std::vector<std::string> chain;
    for (const std::string& line : std::span(log_lines).subspan(first_line)) {
      if (line.find("address space free ") != std::string::npos) chain.push_back(line);
    }
    const bool in_order = chain.size() == 2 && chain[0].find("that frame's present callbacks done: ") != std::string::npos
                          && chain[1].find("the next present: ") != std::string::npos && runtime.address_space_present == 0;
    // The chain's one walk: its last lines, the present callbacks' with the largest free block, the next present's without.
    const bool one_walk = in_order && chain[0].find("largest free block") != std::string::npos
                          && chain[1].find("largest free block") == std::string::npos;
    std::printf("10. the chain at the helper's let go: not armed before its present callback ran %d, armed after (32-bit only) %d, not "
                "mid-chain %d; its last lines over 100 presents: the present callbacks' line, then the next present's, once each %d, "
                "the walk in the first alone %d\n",
                without_callback ? 1 : 0, with_callback ? 1 : 0, mid_chain ? 1 : 0, in_order ? 1 : 0, one_walk ? 1 : 0);
    CHECK(without_callback && with_callback && mid_chain && in_order && one_walk);
  }

  {
    const std::unique_lock lock(runtime.mutex);
    runtime.cluster_meshes.clear();
    runtime.index_buffer_sets.clear();
  }
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
