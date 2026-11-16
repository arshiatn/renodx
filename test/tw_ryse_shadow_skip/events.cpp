// Skip the Game's Sun Shadow Maps (rysesonofrome/shadow_skip.hpp) through ReShade's events and command_action, as the
// add-on runs them with the ray tracing on: the R2 cost run (tools/nontw/ryse/r0/run_r2cost_050544) found the same 5
// shadow-map passes and 862 draws at every Skip value. The frame here is Ryse's from the R0 trace (s1_atn: four 8192 sun
// maps with 51 / 150 / 618 / 18 caster draws, the mask passes sunshadow00-04 reading them at t2, the kept far map read but
// not drawn), its maps real D3D11 textures on WARP (the mask passes read their t2, the bind event's view resolves to them).
// rt_core's scene capture is on while the scene is traced: its command-wide OnAnyDraw (twattila/rt_core.hpp:4390) returns
// a post callback for every draw, so command_action replays each draw on the command list (a draw the GPU runs) and skips
// the original. A stand-in with the same result does that here; every draw the GPU would run is counted: the originals
// ReShade lets through and the replays.
// Frame 1 draws every map (nothing learned yet); frames 2-4 must leave out every caster draw of the skipped maps (All: the
// four, Past the Near Ones: the middle cascade's 618) and keep the others, with the fast dispatch and without it; a frame
// without the rays' shadow draws the maps again. CONTROL: the fix58 form (a draw event of its own returning true) must
// still let every draw through to the GPU under the scene capture, the run's finding.
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/games/rysesonofrome/shadow_skip.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

namespace action = renodx::utils::command_action;
namespace fast = renodx::utils::command_action::fast;
namespace skip = rysesonofrome::shadow_skip;
using api_event = reshade::addon_event;
using Microsoft::WRL::ComPtr;
using namespace reshade::api;

// ---- ReShade: the exports the utilities call, the event lists (all callbacks run, any true skips: addon_manager.hpp).
static std::array<std::vector<void*>, 512> event_lists;
extern "C" __declspec(dllexport) const char* const NAME = "Ryse shadow skip events test";
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

namespace {

int failures = 0;

#define CHECK(condition)                                       \
  do {                                                         \
    if (!(condition)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition); \
      ++failures;                                              \
    }                                                          \
  } while (false)

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

// Draws the GPU runs: the originals ReShade lets through and command_action's replays.
size_t gpu_draws = 0u;

struct TestDevice : mock::Device {
  PrivateData private_data;
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { private_data.Get(guid, data); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { private_data.Set(guid, data); }
  resource get_resource_from_view(resource_view view) const override {
    ComPtr<ID3D11Resource> texture;
    reinterpret_cast<ID3D11View*>(view.handle)->GetResource(texture.GetAddressOf());
    return {reinterpret_cast<uint64_t>(texture.Get())};
  }
};

struct TestList : mock::CommandList {
  PrivateData private_data;
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { private_data.Get(guid, data); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { private_data.Set(guid, data); }
  void draw(uint32_t, uint32_t, uint32_t, uint32_t) override { ++gpu_draws; }
  void draw_indexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override { ++gpu_draws; }
};

// rt_core's OnAnyDraw while the scene is traced: a post callback for every draw (its capture after the draw).
template <typename Context>
void Captured(Context&, const void*) {}

constexpr auto SCENE_CAPTURE = []<typename Context>(Context&) -> action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, action::DrawArguments> && !std::is_same_v<Arguments, action::DrawIndexedArguments>) {
    return {};
  } else {
    return {.post_callback = &Captured<Context>};
  }
};

// CONTROL: fix58's draw events (a skip of their own on the list that bound a skipped map).
bool LegacyDraw(command_list* cmd_list, uint32_t, uint32_t, uint32_t, uint32_t) {
  return skip::internal::skipping.load() == cmd_list;
}
bool LegacyDrawIndexed(command_list* cmd_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  return skip::internal::skipping.load() == cmd_list;
}

struct Map {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11DepthStencilView> depth;
  ComPtr<ID3D11ShaderResourceView> view;
  uint32_t casters = 0u;  // its caster draws a frame (0: kept, not redrawn)
};

// R0's s1_atn frame: the near cascade, the second near one, the middle cascade, the character's own map, the kept far map.
constexpr std::array<uint32_t, 5> CASTERS = {51u, 150u, 618u, 18u, 0u};
// The mask passes in the trace's order and the map each read at t2 (shadow_skip.hpp's MASK_PASSES).
constexpr std::array<std::pair<uint32_t, size_t>, 8> MASK_READS = {{
    {0xE9264ADE, 0}, {0xB5DC6E36, 0}, {0xE9264ADE, 1}, {0xB5DC6E36, 1}, {0xF2B4EF30, 2}, {0xF864A048, 2}, {0xF81C7168, 4}, {0xE9264ADE, 3},
}};

struct Frame {
  std::array<size_t, 5> drawn = {};  // caster draws the GPU ran, per map
};

// One frame on `list` as the game's: each redrawn map bound alone (a depth target, no colour) and its casters drawn, the
// G-buffer bound again, the mask passes (mods::shader calls their on_draw with t2 bound), the tiled lighting with or
// without the rays' sun shadow (`rays`), the present.
Frame RunFrame(ID3D11DeviceContext* context, command_list* list, std::array<Map, 5>& maps, renodx::mods::shader::CustomShaders& shaders,
               resource_view gbuffer_target, bool rays, skip::Choice choice) {
  Frame frame;
  for (size_t index = 0; index < maps.size(); ++index) {
    if (maps[index].casters == 0u) continue;
    context->OMSetRenderTargets(0, nullptr, maps[index].depth.Get());
    InvokeVoid<api_event::bind_render_targets_and_depth_stencil>(list, 0u, static_cast<const resource_view*>(nullptr),
                                                                 resource_view{reinterpret_cast<uint64_t>(maps[index].depth.Get())});
    for (uint32_t draw = 0; draw < maps[index].casters; ++draw) {
      const size_t before = gpu_draws;
      if (!InvokeBool<api_event::draw_indexed>(list, 3792u, 1u, 0u, 0, 0u)) ++gpu_draws;
      frame.drawn[index] += gpu_draws - before;
    }
  }
  context->OMSetRenderTargets(0, nullptr, nullptr);
  InvokeVoid<api_event::bind_render_targets_and_depth_stencil>(list, 1u, &gbuffer_target, resource_view{0u});
  for (const auto& [hash, index] : MASK_READS) {
    ID3D11ShaderResourceView* read = maps[index].view.Get();
    context->PSSetShaderResources(2, 1, &read);
    shaders[hash].on_draw(list);
  }
  ID3D11ShaderResourceView* none = nullptr;
  context->PSSetShaderResources(2, 1, &none);
  if (rays) skip::NoteRays();
  skip::OnPresent(choice);
  return frame;
}

// The fast dispatch's hook, counted (it must take the draws, else its half proves nothing).
size_t fast_taken = 0u;
template <typename Arguments>
bool CountedRun(action::CommandContext<Arguments>* context, bool* skip) {
  const bool taken = fast::internal::Run<Arguments>(context, skip);
  if (taken) ++fast_taken;
  return taken;
}

void UseFast(bool on) {
  action::internal::fast_run_callbacks<action::DrawArguments> = on ? &CountedRun<action::DrawArguments> : nullptr;
  action::internal::fast_run_callbacks<action::DrawIndexedArguments> = on ? &CountedRun<action::DrawIndexedArguments> : nullptr;
}

std::string Drawn(const Frame& frame) {
  return std::format("{} / {} / {} / {} / {}", frame.drawn[0], frame.drawn[1], frame.drawn[2], frame.drawn[3], frame.drawn[4]);
}

}  // namespace

int main() {
  ComPtr<ID3D11Device> native_device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, native_device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }
  std::array<Map, 5> maps;
  for (size_t index = 0; index < maps.size(); ++index) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = 64,
        .Height = 64,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R16_TYPELESS,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
    };
    D3D11_DEPTH_STENCIL_VIEW_DESC depth_desc = {.Format = DXGI_FORMAT_D16_UNORM, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R16_UNORM, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D};
    view_desc.Texture2D.MipLevels = 1;
    maps[index].casters = CASTERS[index];
    if (FAILED(native_device->CreateTexture2D(&desc, nullptr, maps[index].texture.GetAddressOf()))
        || FAILED(native_device->CreateDepthStencilView(maps[index].texture.Get(), &depth_desc, maps[index].depth.GetAddressOf()))
        || FAILED(native_device->CreateShaderResourceView(maps[index].texture.Get(), &view_desc, maps[index].view.GetAddressOf()))) {
      std::printf("FAIL: maps\n");
      return 1;
    }
  }
  // Any non-null view: the G-buffer's colour target (never resolved: a colour target is bound with it).
  const resource_view gbuffer_target = {0x1000u};

  // As the add-on attaches with the ray tracing on at launch: rt_core's scene (its OnAnyDraw), the skip, then
  // mods::shader's command_action and the fast dispatch's opt-in.
  renodx::mods::shader::CustomShaders shaders;
  for (const uint32_t hash : {0xE9264ADEu, 0xB5DC6E36u, 0xF2B4EF30u, 0xF864A048u, 0xF81C7168u}) shaders[hash] = {.crc32 = hash};
  action::Register(SCENE_CAPTURE, {.command_types = action::COMMAND_TYPE_DIRECT_DRAW});
  skip::Install(&shaders);
  action::Use(DLL_PROCESS_ATTACH);
  fast::Use(DLL_PROCESS_ATTACH);

  auto device = std::make_unique<TestDevice>();
  device->native = native_device;
  auto immediate = std::make_unique<TestList>();
  immediate->owner = device.get();
  immediate->native = context;
  InvokeVoid<api_event::init_device>(static_cast<reshade::api::device*>(device.get()));
  InvokeVoid<api_event::init_command_list>(static_cast<command_list*>(immediate.get()));
  auto* list = static_cast<command_list*>(immediate.get());

  constexpr std::array<uint32_t, 5> ALL_LEFT_OUT = {0u, 0u, 0u, 0u, 0u};
  for (const bool fast_path : {true, false}) {
    UseFast(fast_path);
    const char* path = fast_path ? "fast dispatch" : "RunCallbacks";
    // All: the first frame learns the maps, the next ones leave out every sun map's casters.
    const Frame learn = RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::ALL);
    std::printf("%s, All: frame 1 drawn %s (of 51 / 150 / 618 / 18 / 0)\n", path, Drawn(learn).c_str());
    CHECK(learn.drawn[0] == 51u && learn.drawn[1] == 150u && learn.drawn[2] == 618u && learn.drawn[3] == 18u);
    for (int frame = 2; frame <= 4; ++frame) {
      const Frame skipped = RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::ALL);
      std::printf("%s, All: frame %d drawn %s\n", path, frame, Drawn(skipped).c_str());
      CHECK(skipped.drawn[0] == ALL_LEFT_OUT[0] && skipped.drawn[1] == ALL_LEFT_OUT[1] && skipped.drawn[2] == ALL_LEFT_OUT[2]
            && skipped.drawn[3] == ALL_LEFT_OUT[3]);
    }
    // Past the Near Ones: the middle cascade alone (the set changes at the present after the switch).
    RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::MIDDLE_CASCADE);
    for (int frame = 2; frame <= 3; ++frame) {
      const Frame middle = RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::MIDDLE_CASCADE);
      std::printf("%s, Past the Near Ones: frame %d drawn %s\n", path, frame, Drawn(middle).c_str());
      CHECK(middle.drawn[0] == 51u && middle.drawn[1] == 150u && middle.drawn[2] == 0u && middle.drawn[3] == 18u);
    }
    // A frame without the rays' sun shadow: the next one draws every map again.
    RunFrame(context.Get(), list, maps, shaders, gbuffer_target, false, skip::ALL);
    const Frame back = RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::ALL);
    std::printf("%s, after a frame without the rays: drawn %s\n", path, Drawn(back).c_str());
    CHECK(back.drawn[0] == 51u && back.drawn[1] == 150u && back.drawn[2] == 618u && back.drawn[3] == 18u);
    // Off: nothing left out.
    RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::OFF);
    const Frame off = RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::OFF);
    std::printf("%s, Off: drawn %s\n", path, Drawn(off).c_str());
    CHECK(off.drawn[0] == 51u && off.drawn[1] == 150u && off.drawn[2] == 618u && off.drawn[3] == 18u);
    // 11 frames of 837 draws each.
    std::printf("%s: %zu draws taken by the fast dispatch\n", path, fast_taken);
    CHECK(fast_taken == (fast_path ? 11u * 837u : 0u));
    fast_taken = 0u;
  }

  // CONTROL: fix58's form, its own draw events in place of the command_action callback, under the scene capture.
  UseFast(true);
  action::Unregister(skip::internal::OnDraw);
  fast::ClearGate(skip::internal::OnDraw);
  reshade::register_event<api_event::draw>(LegacyDraw);
  reshade::register_event<api_event::draw_indexed>(LegacyDrawIndexed);
  RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::ALL);
  const Frame legacy = RunFrame(context.Get(), list, maps, shaders, gbuffer_target, true, skip::ALL);
  const size_t legacy_total = legacy.drawn[0] + legacy.drawn[1] + legacy.drawn[2] + legacy.drawn[3];
  std::printf("CONTROL fix58's draw events under the scene capture, All: drawn %s (%zu of 837 casters reach the GPU)\n",
              Drawn(legacy).c_str(), legacy_total);
  CHECK(legacy_total == 837u);

  std::printf(failures == 0 ? "PASS\n" : "FAIL: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
