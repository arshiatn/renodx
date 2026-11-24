// The ray tracing's context lookup (twattila/rt_core.hpp rt_scene::internal::NativeContext: every captured draw and the rt_scene
// draw callbacks) against v157's (copied below), on WARP with ReShade's command list mocked.
// 1. Same answer: the immediate context; a deferred context (null); another API (null); a command list whose context
//    changes (the new one at once); a null context; back to the first.
// 2. Cost a call, old and new (printed), with the d3d11 runtime's per-call prices our draw callbacks pay (QueryInterface
//    failing and succeeding, Get + Release of a view, AddRef + Release).
// 3. Rome II's point lights from a constant buffer (twattila/rt_scene.hpp InstallPointLights' light_constants: one light a
//    draw in point_light_PS b2, c0 position, c1.xyz colour, c1.w reach), the game's uploads as ReShade reports them
//    (Map: the event with the mapped pointer, the writes, the unmap event, Unmap; UpdateSubresource: its event): a buffer's
//    first light draw reads nothing (watched from then on); then each draw's light is its own upload's, to the bit, through
//    Map and UpdateSubresource; another buffer's uploads never stand for it; untraced, nothing is watched and what was kept
//    is dropped (CONTROL: the first draw traced again reads nothing, not the last light). Cost a call (printed): the map /
//    unmap events of another buffer untraced and traced, the light buffer's, the light draw's read.
// 4. The instanced light draws' lights (Attila's POINT_LIGHTS: colour POSITION1, position and reach POSITION2, one
//    buffer; read three frames on): the frame's lights in v160's order, each position once, to the bit (v160's float sort and
//    unique copied below): ties of one position with other colours, -0 and +0, zero and -0 reach, a coordinate's few
//    values; random frames of 16384 (two draws, repeats, the 8192 cap reached); a still camera (the places of the frame
//    before: other colours, zeros as -0, then one light moved); a NaN coordinate. Twin draws (Attila's two light draws of
//    the same instances, the second cut at the ring's 16384): the second left out (one copy sorted), a bit apart (both),
//    an earlier copy's twin, a prefix, a longer copy (not a twin), NaN (every record), equal keys inside a copy (the same
//    bytes: still left out; other colours, by the sort or by the frame before's order: every record, then for the retry
//    frames). Cost (printed): v160's sort of a 16384-light frame, the reading draw now (new places, the frame before's
//    places), a draw that only copies; a twin frame's.
// 5. Capture's reads (rt_core.hpp: the vertex and index buffers read once for all its parts, the cached
//    ID3D11DeviceContext1 and device) with RENODX_TW_STATE_CHECK=1: a static draw (three vertex buffers, an index buffer at
//    an offset) gets v166's short key and key (their formulas below), its index buffer reaches Host::moving_indexed, and
//    takes the short path the second time (both mark the scene drawn, scene_frame, the main view's frame left as it was); a rows shader's draw from a dynamic vertex buffer (slot 0 read for the dynamic
//    test, slots 1-3 after) and a placed one (slot 0 alone in its key, its other bound slots none) get theirs; meshes with a
//    cut-out version: a static one's key without its mask, then with it once the shadow pass alpha-tests its texture (the
//    lookup in the scene's lock), a placed known one counted (its texture read once, checked again); a new draw
//    into the main view's depth twice (the device prepared, then cached); the depth's description read once a view (the
//    same view again, the main size changed and back, a new view once the held one's other holder let it go, a cascades'
//    array and back, each rejected in the other's place; the first main-view capture after the helper let its scene go logs
//    the address space once and arms the trace's line); with room, a draw captured once, then its key found in the frame's
//    keys; every capture's key its formula's; the keys and captures after every capture hashed (90d7df1b5's the same); no
//    state check line; CONTROLS: a binding
//    changed between two reads logs one, a held depth description changed logs one. Cost (printed): v166's reads for a
//    static draw against now's. Update's loading-screen test (Host::scene_release) from the draw paths: the static draw
//    captured, or marked drawn (MarkDrawn), before each of 100 presents: kept; CONTROL no draw: let go at the 30th.
// 6. Trace's light pick (rt_core.hpp, ALPK: each light scored once, the scores with their places partial_sorted, lights_last
//    put in that order when the helper does not take the trace) against the old pick (29eeab5c's, copied below: partial_sort of the lights, both sides of every
//    comparison scored): 1600 frames of 0..16384 lights (whole-metre places and few reaches and colours for equal scores,
//    earlier lights again with their colour turned, zeros, -0, NaN, a signalling NaN, inf, huge, tiny, negative), Light
//    Count 0..256, Torch and Fire Shadow Range, the Light Bounce's shadows-only cut and 32 cap; each frame traced twice
//    (the helper never takes it: the second from lights_last as the first left it): the lights, colours, kinds, b9's
//    constants and lights_last to the bit. CONTROLS (frames apart): nth_element, the picks by place with lights_last in
//    its own order, ties flipped (>=). Cost (printed): the old pick against now's at 300-8192 lights, 32-128 picked.
// 7. rt_scene::AmbientReplaced (lock-free: what its writers published) against AmbientReplacedNow (under the lock) after
//    each of 20000 writes of what it reads: Update with random rows (the rays' AO and Path-Traced Ambient among them) or
//    the same rows (a new frame), a trace taken (traced_frame set as Trace's taken path sets it, with its publish), a trace
//    not taken (Trace: nothing written); runs of frames past AMBIENT_REPLACED_FRAMES untraced; the state check's own
//    comparison logs nothing. CONTROL: traced_frame written without its publish differs, the state check logs it once. Not
//    Trace's real taken branch: its request needs the helper (the shared block, the fence it signals, its answer event).
//    Cost (printed): the lock's read against the lock-free load, uncontended.
// 8. Ray-Traced Specular Occlusion counts as on only with another ray-traced row (rt_scene.hpp SettingsOf,
//    rt_core.hpp RaysBesideSpecularOcclusion): alone (and beside a Light Bounce row with the Global Illumination at 0) its
//    settings are those of the row off, Configure asks for no ray tracing textures, guides, reflections or Ray
//    Reconstruction, Update leaves Enabled() and `active` false, Trace asks nothing, a scene draw captures nothing, the
//    row's is_enabled is false; CONTROL: the build before's SettingsOf and Configure (copied there) start all of them.
//    With Ray-Traced Shadows, AO, the Light Bounce, Path Tracing, Reflections or Fog Shadows: the settings and all of
//    the above as the build before's, the row enabled. Without an upscaler: off, as before.
#include <windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include "src/games/twattila/rt_scene.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
namespace core = twattila::rt_scene;
namespace action = renodx::utils::command_action;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Native context test";
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { log_lines.emplace_back(message); }

// v157's (rt_core.hpp at 3325e7c5).
static ID3D11DeviceContext* OldNativeContext(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  return (native != nullptr && native->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) ? native : nullptr;
}

struct OtherApiDevice : mock::Device {
  reshade::api::device_api get_api() const override { return reshade::api::device_api::d3d12; }
};

// What Trace's light pick hands on: the lights, colours and kinds of the request, b9's constants (when written).
struct PickedLights {
  std::vector<std::array<float, 4>> lights;
  std::vector<std::array<float, 4>> colours;
  std::vector<uint8_t> kinds;
  std::array<std::array<float, 4>, (twattila::dlaa_bridge::RT_ALL_LIGHTS * 2) + 1> constants = {};
  bool written = false;
};

// The light pick of Trace before ALPK (rt_core.hpp at 29eeab5c), its sort step given: `sort(candidates, count, score)`
// returns the first `count` and leaves `candidates` as lights_last stays (the old one: partial_sort of the lights, both
// sides of every comparison scored). `bounced`: the path-traced Light Bounce ran the frame before.
template <typename Sort>
static PickedLights PickLights(std::vector<core::LocalLight>* lights_last, const core::Settings& settings, const core::Camera& camera,
                               bool bounced, uint32_t light_tile_row, Sort&& sort) {
  namespace bridge = twattila::dlaa_bridge;
  PickedLights picked;
  const bool lights_in_bounce = (settings.lights_in_bounce || settings.light_ratio) && settings.light_bounce && settings.path_tracing && bounced;
  const bool light_ratio = lights_in_bounce && settings.light_ratio;
  if (!settings.local_lights || lights_last->empty()) return picked;
  const auto distance_squared = [&](const core::LocalLight& light) {
    const float x = light.light[0] - camera.position[0];
    const float y = light.light[1] - camera.position[1];
    const float z = light.light[2] - camera.position[2];
    return (x * x) + (y * y) + (z * z);
  };
  const auto score = [&](const core::LocalLight& light) {
    return std::max({light.colour[0], light.colour[1], light.colour[2]}) * light.light[3] * light.light[3]
           / std::max(distance_squared(light), 1.f);
  };
  auto& candidates = *lights_last;
  if (const float range = settings.light_range; range > 0.f) {
    std::erase_if(candidates, [&](const core::LocalLight& light) { return distance_squared(light) > range * range; });
  }
  if (lights_in_bounce) std::erase_if(candidates, [](const core::LocalLight& light) { return light.shadows_only; });
  const size_t count = std::min<size_t>({candidates.size(), settings.light_count, light_ratio ? bridge::RT_MAX_LIGHTS : bridge::RT_ALL_LIGHTS});
  const std::vector<core::LocalLight> chosen = sort(&candidates, count, score);
  static const bool squared_falloff = [] {
    std::array<char, 2> value = {};
    return GetEnvironmentVariableA("RENODX_TW_SQUARED_FALLOFF", value.data(), static_cast<DWORD>(value.size())) == 1 && value[0] == '1';
  }();
  for (size_t index = 0; index < count; ++index) {
    const std::array<float, 4> colour = (chosen[index].shadows_only ? std::array<float, 4>{} : chosen[index].colour);
    const uint8_t kind = (squared_falloff ? bridge::RT_LIGHT_SMOOTH : chosen[index].falloff);
    picked.lights.push_back(chosen[index].light);
    picked.colours.push_back(colour);
    picked.kinds.push_back(kind);
    picked.constants[index] = chosen[index].light;
    picked.constants[bridge::RT_ALL_LIGHTS + 1 + index] = {colour[0], colour[1], colour[2], static_cast<float>(kind)};
  }
  picked.constants[bridge::RT_ALL_LIGHTS] = {std::bit_cast<float>(static_cast<uint32_t>(count)), settings.light_glow,
                                             std::bit_cast<float>(light_tile_row), std::bit_cast<float>(light_ratio ? 1u : 0u)};
  picked.written = true;
  return picked;
}

int main() {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> immediate;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                                    &immediate)));
  ComPtr<ID3D11DeviceContext> deferred;
  CHECK(SUCCEEDED(device->CreateDeferredContext(0, &deferred)));
  ComPtr<ID3D11Device> second_device;
  ComPtr<ID3D11DeviceContext> second_immediate;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &second_device,
                                    nullptr, &second_immediate)));
  mock::Device game_device;
  mock::CommandList cmd_list;
  cmd_list.owner = &game_device;
  const auto same = [&](ID3D11DeviceContext* expected) {
    for (int call = 0; call < 3; ++call) {
      if (core::internal::NativeContext(&cmd_list) != expected || OldNativeContext(&cmd_list) != expected) return false;
    }
    return true;
  };
  // 1.
  cmd_list.native = immediate;
  CHECK(same(immediate.Get()));
  cmd_list.native = deferred;
  CHECK(same(nullptr));
  cmd_list.native = second_immediate;
  CHECK(same(second_immediate.Get()));
  cmd_list.native = nullptr;
  CHECK(same(nullptr));
  cmd_list.native = immediate;
  CHECK(same(immediate.Get()));
  OtherApiDevice other_device;
  mock::CommandList other_list;
  other_list.owner = &other_device;
  other_list.native = immediate;
  CHECK(core::internal::NativeContext(&other_list) == nullptr && OldNativeContext(&other_list) == nullptr);
  CHECK(core::internal::NativeContext(&cmd_list) == immediate.Get());
  std::printf("1 immediate, deferred (null), another device's, null, another API (null), back: old = new every call\n");

  // 2.
  constexpr int CALLS = 2000000;
  volatile uintptr_t sink = 0;
  const auto time = [&](const char* name, auto&& body) {
    const auto start = std::chrono::steady_clock::now();
    for (int call = 0; call < CALLS; ++call) body();
    std::printf("2 %-52s %6.1f ns a call\n", name,
                std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / CALLS);
  };
  time("NativeContext, v157", [&] { sink = reinterpret_cast<uintptr_t>(OldNativeContext(&cmd_list)); });
  time("NativeContext, now", [&] { sink = reinterpret_cast<uintptr_t>(core::internal::NativeContext(&cmd_list)); });
  time("d3d11: QueryInterface failing (the proxy lookup)", [&] {
    ID3D11DeviceContext* context = immediate.Get();
    sink = renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  });
  time("d3d11: QueryInterface(ID3D11DeviceContext1) + Release", [&] {
    ComPtr<ID3D11DeviceContext1> context1;
    sink = SUCCEEDED(immediate.As(&context1));
  });
  time("d3d11: IAGetIndexBuffer + Release (none bound)", [&] {
    ComPtr<ID3D11Buffer> buffer;
    DXGI_FORMAT format;
    UINT offset;
    immediate->IAGetIndexBuffer(buffer.GetAddressOf(), &format, &offset);
    sink = reinterpret_cast<uintptr_t>(buffer.Get());
  });
  time("d3d11: AddRef + Release", [&] {
    immediate->AddRef();
    sink = immediate->Release();
  });

  // 3.
  auto& runtime = core::internal::runtime;
  core::internal::light_constants_slot = 2;
  core::internal::active.store(true);
  const auto trace_lights = [&](bool on) {
    const std::unique_lock lock(runtime.mutex);
    runtime.settings.local_lights = on;
    runtime.lights_drawn.clear();
    core::internal::light_frame.store((runtime.frame << 1) | uint64_t{on});  // as Update stores it
  };
  constexpr UINT LIGHT_BYTES = 64;  // point_light_PS: c0-c3
  const D3D11_BUFFER_DESC mapped_desc = {.ByteWidth = LIGHT_BYTES, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                         .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  const D3D11_BUFFER_DESC updated_desc = {.ByteWidth = LIGHT_BYTES, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> mapped_light;
  ComPtr<ID3D11Buffer> other;
  ComPtr<ID3D11Buffer> updated_light;
  std::array<ComPtr<ID3D11Buffer>, 3> more;
  CHECK(SUCCEEDED(device->CreateBuffer(&mapped_desc, nullptr, &mapped_light)) && SUCCEEDED(device->CreateBuffer(&mapped_desc, nullptr, &other))
        && SUCCEEDED(device->CreateBuffer(&updated_desc, nullptr, &updated_light)));
  for (auto& buffer : more) CHECK(SUCCEEDED(device->CreateBuffer(&mapped_desc, nullptr, &buffer)));
  const auto handle = [](ID3D11Buffer* buffer) { return reshade::api::resource{reinterpret_cast<uint64_t>(buffer)}; };
  // A light's rows: c0 position, c1 colour and reach, c2 the viewport's offset and scale, c3 g_clip.
  const auto rows_of = [](int light) {
    return std::array<float, 16>{10.f * light, 2.5f, -5.f * light, 0.f, 600.f + light, 610.f, 590.f, 15.f + light,
                                 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f};
  };
  const auto map_upload = [&](ID3D11Buffer* buffer, const std::array<float, 16>& rows) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(immediate->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
    core::internal::OnMapLightConstants(&game_device, handle(buffer), 0, UINT64_MAX, reshade::api::map_access::write_discard, &mapped.pData);
    std::memcpy(mapped.pData, rows.data(), sizeof(rows));
    core::internal::OnUnmapLightConstants(&game_device, handle(buffer));
    immediate->Unmap(buffer, 0);
    return true;
  };
  const auto update_upload = [&](ID3D11Buffer* buffer, const std::array<float, 16>& rows) {
    core::internal::OnUpdateLightConstants(&game_device, rows.data(), handle(buffer), 0, UINT64_MAX);
    immediate->UpdateSubresource(buffer, 0, nullptr, rows.data(), 0, 0);
  };
  // The registration's data, as InstallPointLights registers the hook (OnPointLightDraw reads it first).
  action::CommandContext<action::DrawIndexedArguments> light_command = {
      .cmd_list = &cmd_list, .arguments = {.index_count = 792, .instance_count = 1}, .callback_data = &core::POINT_LIGHTS};
  // A light draw: its buffer at b2, rt_scene's hook; the lights added since the last trace_lights.
  const auto light_draw = [&](ID3D11Buffer* buffer) {
    immediate->PSSetConstantBuffers(2, 1, &buffer);
    core::OnPointLightDraw(light_command);
    const std::unique_lock lock(runtime.mutex);
    return runtime.lights_drawn;
  };
  const auto is_light = [&](const core::LocalLight& drawn, int light) {
    const auto rows = rows_of(light);
    return drawn.light == std::array<float, 4>{rows[0], rows[1], rows[2], rows[7]} && drawn.colour == std::array<float, 4>{rows[4], rows[5], rows[6], 0.f};
  };
  const auto only_light = [&](const std::vector<core::LocalLight>& drawn, int light) { return drawn.size() == 1 && is_light(drawn[0], light); };
  trace_lights(true);
  CHECK(map_upload(mapped_light.Get(), rows_of(1)));
  CHECK(light_draw(mapped_light.Get()).empty());  // first seen: watched from now on
  CHECK(map_upload(other.Get(), rows_of(9)));      // a buffer no light draw has bound
  CHECK(map_upload(mapped_light.Get(), rows_of(2)));
  CHECK(only_light(light_draw(mapped_light.Get()), 2));
  trace_lights(true);
  CHECK(light_draw(updated_light.Get()).empty());
  update_upload(updated_light.Get(), rows_of(3));
  CHECK(only_light(light_draw(updated_light.Get()), 3));
  // A frame's lights, one upload and draw each.
  trace_lights(true);
  std::vector<core::LocalLight> frame;
  for (int light = 4; light < 8; ++light) {
    CHECK(map_upload(mapped_light.Get(), rows_of(light)));
    frame = light_draw(mapped_light.Get());
  }
  CHECK(frame.size() == 4 && is_light(frame[0], 4) && is_light(frame[1], 5) && is_light(frame[2], 6) && is_light(frame[3], 7));
  // CONTROL: the buffer whose uploads were never a light draw's reads nothing at its first draw.
  trace_lights(true);
  CHECK(map_upload(other.Get(), rows_of(9)));
  CHECK(light_draw(other.Get()).empty());
  // Untraced: nothing watched, what was kept dropped; traced again, the first draw reads nothing (not light 7, the last kept,
  // nor 10, uploaded unwatched), the next its own.
  trace_lights(false);
  CHECK(light_draw(mapped_light.Get()).empty() && !core::internal::light_uploads_watched.load());
  CHECK(map_upload(mapped_light.Get(), rows_of(10)));
  trace_lights(true);
  CHECK(light_draw(mapped_light.Get()).empty());
  CHECK(map_upload(mapped_light.Get(), rows_of(11)));
  CHECK(only_light(light_draw(mapped_light.Get()), 11));
  // Room for 4 buffers: the fifth takes the oldest's place (mapped_light, read again from its next upload on).
  for (auto& buffer : more) {
    trace_lights(true);
    CHECK(light_draw(buffer.Get()).empty());
    CHECK(map_upload(buffer.Get(), rows_of(12)));
    CHECK(only_light(light_draw(buffer.Get()), 12));
  }
  trace_lights(true);
  CHECK(map_upload(mapped_light.Get(), rows_of(13)));
  CHECK(light_draw(mapped_light.Get()).empty());
  CHECK(map_upload(mapped_light.Get(), rows_of(14)));
  CHECK(only_light(light_draw(mapped_light.Get()), 14));
  std::printf("3 constant-buffer lights: first draw of a buffer none, then each draw its own upload (Map and UpdateSubresource), another "
              "buffer's never, untraced nothing kept, the oldest of 4 buffers given up for a fifth\n");
  std::array<float, 16> rows = rows_of(15);
  void* rows_pointer = rows.data();
  ComPtr<ID3D11Buffer> unwatched;
  CHECK(SUCCEEDED(device->CreateBuffer(&mapped_desc, nullptr, &unwatched)));
  const auto events = [&](ID3D11Buffer* buffer) {
    core::internal::OnMapLightConstants(&game_device, handle(buffer), 0, UINT64_MAX, reshade::api::map_access::write_discard, &rows_pointer);
    core::internal::OnUnmapLightConstants(&game_device, handle(buffer));
  };
  const auto time_lights = [&](const char* name, int calls, auto&& body) {
    const auto start = std::chrono::steady_clock::now();
    for (int call = 0; call < calls; ++call) body();
    std::printf("3 %-60s %6.1f ns a call\n", name, std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls);
  };
  trace_lights(false);
  light_draw(mapped_light.Get());
  time_lights("map + unmap events, untraced", CALLS, [&] { events(unwatched.Get()); });
  trace_lights(true);
  light_draw(mapped_light.Get());
  time_lights("map + unmap events, traced, another buffer", CALLS, [&] { events(unwatched.Get()); });
  time_lights("map + unmap events, traced, the light's (2 rows copied)", CALLS, [&] { events(mapped_light.Get()); });
  time_lights("UpdateSubresource event, traced, the light's", CALLS, [&] {
    core::internal::OnUpdateLightConstants(&game_device, rows.data(), handle(mapped_light.Get()), 0, UINT64_MAX);
  });
  immediate->PSSetConstantBuffers(2, 1, mapped_light.GetAddressOf());
  time_lights("light draw (b2 read, AddLocalLight, BindLocalLights none)", CALLS / 10, [&] { core::OnPointLightDraw(light_command); });

  // 4.
  core::internal::light_constants_slot = core::internal::NO_LIGHT_CONSTANTS;
  constexpr char LIGHT_VS[] = "struct In { float3 mesh : POSITION0; float4 colour : POSITION1; float4 light : POSITION2; };"
                              "float4 main(In input) : SV_Position { return float4(input.mesh, 1) + input.colour + input.light; }";
  ComPtr<ID3DBlob> light_vs;
  CHECK(SUCCEEDED(D3DCompile(LIGHT_VS, sizeof(LIGHT_VS) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &light_vs, nullptr)));
  const std::array<D3D11_INPUT_ELEMENT_DESC, 3> elements = {{
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"POSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
  }};
  ComPtr<ID3D11InputLayout> input_layout;
  CHECK(SUCCEEDED(device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), light_vs->GetBufferPointer(),
                                            light_vs->GetBufferSize(), &input_layout)));
  core::internal::point_light_layouts[reinterpret_cast<uintptr_t>(input_layout.Get())] = {{.semantic_index = 1, .slot = 1, .offset = 0},
                                                                                          {.semantic_index = 2, .slot = 1, .offset = 16}};
  immediate->IASetInputLayout(input_layout.Get());
  struct Instance {
    std::array<float, 4> colour;
    std::array<float, 4> light;
  };
  using Draws = std::vector<std::vector<Instance>>;
  // v160's (rt_scene.hpp OnPointLightDraw at 703aae32), then AddLocalLights' cap and reach test.
  const auto old_order = [](std::vector<core::LocalLight>* lights) {
    std::ranges::sort(*lights, {}, &core::LocalLight::light);
    const auto repeated = std::ranges::unique(*lights, {}, &core::LocalLight::light);
    lights->erase(repeated.begin(), repeated.end());
  };
  // The records the ring holds (POINT_LIGHT_BYTES: the draws' first 16384), in draw order.
  const auto read_lights = [](const Draws& draws) {
    std::vector<core::LocalLight> lights;
    for (const auto& draw : draws) {
      for (const Instance& instance : draw) {
        if (lights.size() == core::internal::POINT_LIGHT_BYTES / sizeof(Instance)) break;
        core::LocalLight light = {.light = instance.light, .colour = instance.colour};
        for (size_t channel = 0; channel < 3; ++channel) light.colour[channel] *= core::internal::host.local_light_scale;
        lights.push_back(light);
      }
    }
    return lights;
  };
  const auto expected_drawn = [&](const Draws& draws) {
    auto lights = read_lights(draws);
    old_order(&lights);
    std::vector<core::LocalLight> drawn;
    for (const core::LocalLight& light : lights) {
      if (drawn.size() >= core::internal::MAX_LOCAL_LIGHTS) break;
      if (light.light[3] > 0.f) drawn.push_back(light);
    }
    return drawn;
  };
  const auto same_bits = [](const std::vector<core::LocalLight>& a, const std::vector<core::LocalLight>& b) {
    return std::ranges::equal(a, b, [](const core::LocalLight& x, const core::LocalLight& y) {
      return std::memcmp(x.light.data(), y.light.data(), sizeof(x.light)) == 0 && std::memcmp(x.colour.data(), y.colour.data(), sizeof(x.colour)) == 0
             && x.shadows_only == y.shadows_only;
    });
  };
  // A frame: its draws (instances at slot 1), the lights read at its first (the frame's three before); ms of that first draw.
  const auto instanced_frame = [&](const Draws& draws, std::vector<core::LocalLight>* drawn) {
    std::vector<ComPtr<ID3D11Buffer>> buffers;
    for (const auto& draw : draws) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(std::max<size_t>(draw.size(), 1) * sizeof(Instance)),
                                      .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = draw.data()};
      if (FAILED(device->CreateBuffer(&desc, draw.empty() ? nullptr : &data, buffers.emplace_back().GetAddressOf()))) return -1.;
    }
    {
      const std::unique_lock lock(runtime.mutex);
      ++runtime.frame;
      runtime.settings.local_lights = true;
      runtime.lights_drawn.clear();
      core::internal::light_frame.store((runtime.frame << 1) | 1u);
    }
    double first_ms = 0.;
    for (size_t index = 0; index <= draws.size(); ++index) {
      // One draw without instances first: the frame's read on its own.
      ID3D11Buffer* buffer = (index == 0) ? buffers.empty() ? nullptr : buffers[0].Get() : buffers[index - 1].Get();
      const UINT stride = sizeof(Instance);
      const UINT offset = 0;
      immediate->IASetVertexBuffers(1, 1, &buffer, &stride, &offset);
      light_command.arguments.instance_count = (index == 0) ? 0 : static_cast<UINT>(draws[index - 1].size());
      const auto start = std::chrono::steady_clock::now();
      core::OnPointLightDraw(light_command);
      if (index == 0) first_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    const std::unique_lock lock(runtime.mutex);
    *drawn = runtime.lights_drawn;
    return first_ms;
  };
  // Frames whose lights tie: every combination of a few values (-0 and +0 in each coordinate, -0 and 0 reach), each three
  // times with its own colour, shuffled.
  std::mt19937 generator(1114);
  std::vector<Draws> frames;
  {
    std::vector<Instance> ties;
    const std::array<float, 4> xs = {-0.f, 0.f, 1.f, -1.f};
    const std::array<float, 3> ys = {0.f, -0.f, 2.f};
    const std::array<float, 2> zs = {-0.f, 0.f};
    const std::array<float, 4> reaches = {0.f, 5.f, -0.f, 7.f};
    for (int copy = 0; copy < 3; ++copy) {
      for (float x : xs) {
        for (float y : ys) {
          for (float z : zs) {
            for (float reach : reaches) {
              ties.push_back({.colour = {static_cast<float>(ties.size()), 1.f, 2.f, static_cast<float>(copy)}, .light = {x, y, z, reach}});
            }
          }
        }
      }
    }
    for (int repeat = 0; repeat < 4; ++repeat) {
      std::ranges::shuffle(ties, generator);
      frames.push_back({ties});
      frames.push_back({std::vector(ties.begin(), ties.begin() + 100), std::vector(ties.begin() + 100, ties.end())});
    }
  }
  // Random frames of 16384 in two draws: positions on a coarse grid (x and y tie often), a third repeats of earlier lights
  // (its colour or another), reach 0 for some; then all distinct (the cap: 8192 of 16384).
  const auto lights_of = [&](size_t count, bool distinct) {
    std::vector<Instance> lights;
    std::uniform_int_distribution<int> grid(-40, 40);
    std::uniform_real_distribution<float> any(-2000.f, 2000.f);
    std::uniform_int_distribution<int> reach(0, 30);
    while (lights.size() < count) {
      if (!distinct && lights.size() > 16 && generator() % 3 == 0) {
        Instance repeat = lights[generator() % lights.size()];
        if (generator() % 2 == 0) repeat.colour[0] += 1.f;
        lights.push_back(repeat);
        continue;
      }
      const float x = distinct ? any(generator) : 0.5f * static_cast<float>(grid(generator));
      const float y = distinct ? any(generator) : static_cast<float>(grid(generator) % 4);
      lights.push_back({.colour = {static_cast<float>(generator() % 256), static_cast<float>(generator() % 256), 0.25f, 0.f},
                        .light = {x, y, any(generator), static_cast<float>(reach(generator))}});
    }
    return lights;
  };
  const auto random_frame = [&](bool distinct) {
    const std::vector<Instance> lights = lights_of(16384, distinct);
    return Draws{std::vector(lights.begin(), lights.begin() + 10000), std::vector(lights.begin() + 10000, lights.end())};
  };
  for (int repeat = 0; repeat < 6; ++repeat) frames.push_back(random_frame(false));
  for (int repeat = 0; repeat < 3; ++repeat) frames.push_back(random_frame(true));
  // A still camera: the places of the frame before with other colours (the order taken again), then the zero coordinates
  // as -0 (the same keys, other bits: still the float sort's order, each light this frame's), then a moved light.
  {
    Draws still = random_frame(false);
    frames.push_back(still);
    for (auto& draw : still) {
      for (Instance& instance : draw) instance.colour[1] += 2.f;
    }
    frames.push_back(still);
    size_t zeros = 0;
    for (auto& draw : still) {
      for (Instance& instance : draw) {
        for (float& coordinate : instance.light) {
          if (coordinate == 0.f) {
            coordinate = -0.f;
            ++zeros;
          }
        }
      }
    }
    CHECK(zeros > 100);
    frames.push_back(still);
    still[1][5].light[0] += 0.25f;
    frames.push_back(still);
  }
  {
    Draws nan_frame = random_frame(false);
    nan_frame[0][77].light[1] = std::numeric_limits<float>::quiet_NaN();
    frames.push_back(nan_frame);
  }
  // Twin draws (Attila's: every light drawn twice, a stencil variant each; night1's 9,593 twice, cut at the ring's 16,384):
  // a frame's records the sort took (the last sorted frame's keys: a twin copy left out, or every record) and its
  // light_order (1 the last sorted frame's order), in order; 0: not checked. `retry_cleared`: the retry frames ended after it.
  struct TwinCheck {
    size_t sorted = 0;
    uint32_t light_order = 0;
    bool retry_cleared = false;
  };
  std::vector<TwinCheck> twin_checks(frames.size());
  const auto add_twins = [&](const Draws& draws, size_t sorted, uint32_t light_order, bool retry_cleared = false) {
    frames.push_back(draws);
    twin_checks.push_back({.sorted = sorted, .light_order = light_order, .retry_cleared = retry_cleared});
  };
  {
    const std::vector<Instance> twin = lights_of(9593, true);
    add_twins({twin, twin}, 9593, 2);  // the second cut at 6,791: its records the first's first, left out
    std::vector<Instance> recoloured = twin;
    for (Instance& instance : recoloured) instance.colour[1] += 2.f;
    add_twins({recoloured, recoloured}, 9593, 1);  // a still camera: the twins' places as before
    std::vector<Instance> one_bit = twin;
    one_bit[100].colour[2] = std::nextafter(one_bit[100].colour[2], 1000.f);
    add_twins({twin, one_bit}, 16384, 2);  // one bit differs: both read, both sorted (the same keys as the twins')
    add_twins({twin, twin}, 9593, 2);
    // A twin of a copy before the last; a shorter twin (a prefix); a longer copy (no twin: more records than the first's).
    const std::vector<Instance> first = lights_of(4000, true);
    const std::vector<Instance> second = lights_of(3000, true);
    add_twins({first, second, first}, 7000, 2);
    const std::vector<Instance> longer = lights_of(5000, true);
    const std::vector<Instance> prefix(longer.begin(), longer.begin() + 3000);
    add_twins({longer, prefix}, 5000, 2);
    add_twins({prefix, longer}, 8000, 2);
    std::vector<Instance> nan_twin = lights_of(9593, true);
    nan_twin[50].light[2] = std::numeric_limits<float>::quiet_NaN();
    add_twins({nan_twin, nan_twin}, 0, 2);  // the float sort of every record
    // Equal keys inside a copy: other colours at one place read every record (then the retry frames ended by hand); the
    // same bytes (a light listed twice) keep the twin left out; then the same places with one such pair's colours apart
    // (the last sorted frame's order taken: its ties checked again) read every record, and so do the next
    // TWIN_RETRY_FRAMES frames: ties with other colours, again (the full read's places as before), a twin frame.
    const std::vector<Instance> ties = lights_of(9593, false);
    add_twins({ties, ties}, 16384, 2, true);
    std::vector<Instance> listed_twice = lights_of(9593, true);
    for (size_t light = 0; light < 100; ++light) listed_twice[200 + light] = listed_twice[light];
    add_twins({listed_twice, listed_twice}, 9593, 2);
    std::vector<Instance> colours_apart = listed_twice;
    colours_apart[200].colour[0] += 1.f;
    add_twins({colours_apart, colours_apart}, 16384, 2);
    add_twins({ties, ties}, 16384, 2);
    add_twins({ties, ties}, 16384, 1);
    add_twins({twin, twin}, 16384, 2);
  }
  frames.push_back({});
  twin_checks.resize(frames.size());
  // Each frame read three frames on (then empty frames to read the last three).
  size_t big_checked = 0;
  size_t twins_checked = 0;
  for (size_t shown = 0; shown < frames.size() + 3; ++shown) {
    std::vector<core::LocalLight> drawn;
    CHECK(instanced_frame((shown < frames.size()) ? frames[shown] : Draws{}, &drawn) >= 0.);
    if (shown < 3) {
      CHECK(drawn.empty());
      continue;
    }
    const auto expected = expected_drawn(frames[shown - 3]);
    CHECK(same_bits(drawn, expected));
    if (expected.size() == core::internal::MAX_LOCAL_LIGHTS) ++big_checked;
    if (const TwinCheck& check = twin_checks[shown - 3]; check.light_order != 0) {
      const std::unique_lock lock(runtime.mutex);
      CHECK(runtime.profile.light_order == check.light_order);
      CHECK(check.sorted == 0 || core::internal::point_light_readback.keys.size() == check.sorted);
      if (check.retry_cleared) {
        CHECK(core::internal::point_light_readback.full_reads == core::internal::TWIN_RETRY_FRAMES);
        core::internal::point_light_readback.full_reads = 0;
      }
      ++twins_checked;
    }
  }
  CHECK(big_checked >= 3 && twins_checked == 14);
  std::printf("4 instanced lights: %zu frames (ties, -0 / +0, zero reach, 16384 a frame, the 8192 cap, a still camera, NaN; twin draws: "
              "one sorted, a bit apart, an earlier copy's twin, a prefix, a longer copy, NaN, a light listed twice, its colours apart, "
              "other colours at one place, the retry frames): v160's order and lights, to the bit\n", frames.size());
  core::internal::point_light_readback.full_reads = 0;  // the twin frames below tried at once
  // Cost: a 16384-light frame (repeats), two of them in turn (new places each read), then one again and again (a still
  // camera).
  std::vector<double> old_ms;
  std::vector<double> moved_ms;
  std::vector<double> still_ms;
  std::vector<double> copying_ms;
  const std::array<Draws, 2> timed = {random_frame(false), random_frame(false)};
  for (int run = 0; run < 18; ++run) {
    const Draws& frame_draws = timed[(run < 9) ? (run % 2) : 0];
    auto lights = read_lights(frame_draws);
    const auto start = std::chrono::steady_clock::now();
    old_order(&lights);
    old_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    std::vector<core::LocalLight> drawn;
    for (int repeat = 0; repeat < 3; ++repeat) copying_ms.push_back(instanced_frame(Draws{}, &drawn));
    CHECK(instanced_frame(frame_draws, &drawn) >= 0.);
    copying_ms.push_back(instanced_frame(Draws{}, &drawn));
    copying_ms.push_back(instanced_frame(Draws{}, &drawn));
    ((run < 9) ? moved_ms : still_ms).push_back(instanced_frame(Draws{}, &drawn));
    CHECK(same_bits(drawn, expected_drawn(frame_draws)));
  }
  const auto median = [](std::vector<double> values) {
    std::ranges::sort(values);
    return values[values.size() / 2];
  };
  std::printf("4 a 16384-light frame: v160's sort + unique %.3f ms; the reading draw now (read, sort, add) %.3f ms, its places as the "
              "frame before %.3f ms; a draw reading nothing %.3f ms (medians)\n",
              median(old_ms), median(moved_ms), median(still_ms), median(copying_ms));
  // Cost: a twin frame (9,593 drawn twice, cut at 16,384), two of them in turn (new places each read).
  std::vector<double> twin_old_ms;
  std::vector<double> twin_ms;
  const std::vector<Instance> twin_a = lights_of(9593, true);
  const std::vector<Instance> twin_b = lights_of(9593, true);
  const std::array<Draws, 2> twin_frames = {Draws{twin_a, twin_a}, Draws{twin_b, twin_b}};
  for (int run = 0; run < 9; ++run) {
    const Draws& frame_draws = twin_frames[run % 2];
    auto lights = read_lights(frame_draws);
    const auto start = std::chrono::steady_clock::now();
    old_order(&lights);
    twin_old_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    std::vector<core::LocalLight> drawn;
    CHECK(instanced_frame(frame_draws, &drawn) >= 0.);
    instanced_frame(Draws{}, &drawn);
    instanced_frame(Draws{}, &drawn);
    twin_ms.push_back(instanced_frame(Draws{}, &drawn));
    CHECK(same_bits(drawn, expected_drawn(frame_draws)));
  }
  std::printf("4 a twin frame (9593 drawn twice, cut at 16384): v160's sort + unique %.3f ms; the reading draw now (read, sort, add) %.3f ms "
              "(medians)\n",
              median(twin_old_ms), median(twin_ms));

  // 5.
  {
    SetEnvironmentVariableA("RENODX_TW_STATE_CHECK", "1");  // before the first capture: StateCheck reads it once
    log_lines.clear();
    const auto buffer_of = [&](UINT bytes, D3D11_USAGE usage, UINT bind) {
      ComPtr<ID3D11Buffer> buffer;
      const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = usage, .BindFlags = bind,
                                      .CPUAccessFlags = (usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0u)};
      return SUCCEEDED(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf())) ? buffer : nullptr;
    };
    const auto vertices = buffer_of(4096, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER);
    const auto instances = buffer_of(4096, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER);
    const auto extra = buffer_of(4096, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER);
    const auto dynamic_vertices = buffer_of(4096, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER);
    const auto indices = buffer_of(4096, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER);
    const auto camera_constants = buffer_of(256, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER);
    CHECK(vertices && instances && extra && dynamic_vertices && indices && camera_constants);
    constexpr UINT INDEX_OFFSET = 12;
    constexpr std::array<UINT, 4> STRIDES = {32, 16, 24, 0};
    constexpr std::array<UINT, 4> OFFSETS = {64, 0, 48, 0};
    const auto bind = [&](ID3D11Buffer* first) {
      const std::array<ID3D11Buffer*, 4> bound = {first, instances.Get(), extra.Get(), nullptr};
      immediate->IASetVertexBuffers(0, 4, bound.data(), STRIDES.data(), OFFSETS.data());
      immediate->IASetIndexBuffer(indices.Get(), DXGI_FORMAT_R16_UINT, INDEX_OFFSET);
      immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    };
    auto& bridge = twattila::dlaa_bridge::bridge;
    bridge.ray_tracing = true;
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.settings = {.sun_shadows = true};
      runtime.frame = 500;
      runtime.traced_frame = 0;
    }
    // The host's mesh-moving hook sees the bound index buffer (none moves).
    static ID3D11Buffer* moving_buffer = nullptr;
    static UINT moving_offset = 0;
    core::internal::host = {.moving_indexed = [](ID3D11Buffer* index_buffer, UINT index_offset, const action::DrawIndexedArguments&) {
      moving_buffer = index_buffer;
      moving_offset = index_offset;
      return false;
    }};
    const action::DrawIndexedArguments arguments = {.index_count = 36, .instance_count = 3, .first_index = 6, .vertex_offset = -2,
                                                    .first_instance = 7};
    action::CommandContext<action::DrawIndexedArguments> command = {.cmd_list = &cmd_list, .arguments = arguments};
    // Each capture, then what it left (the scene key, the main view's frame, the prepared context, the frame's captures and
    // their keys, the drawn and short keys) into one sequence hash: the same as 90d7df1b5's over these cases (run against it).
    // Keys by their order of first appearance (they hash the buffers' addresses: another run, other keys).
    uint64_t sequence = 14695981039346656037ull;
    uint32_t sequence_captures = 0;
    std::vector<uint64_t> keys_seen;
    const auto key_order = [&keys_seen](uint64_t key) -> uint64_t {
      const auto found = std::ranges::find(keys_seen, key);
      if (found != keys_seen.end()) return static_cast<uint64_t>(found - keys_seen.begin());
      keys_seen.push_back(key);
      return keys_seen.size() - 1;
    };
    // Each capture's scene key against its formula (`expected`, full_key's below; every capture here reaches the key).
    uint32_t keys_unexpected = 0;
    const auto capture = [&](const core::SceneShader& shader, uint64_t expected) {
      core::internal::Capture(command, shader);
      const std::unique_lock lock(runtime.mutex);
      if (runtime.last_key != expected) ++keys_unexpected;
      std::vector<uint64_t> values = {key_order(runtime.last_key), runtime.main_view_frame, uint64_t{runtime.objects.context == immediate.Get()},
                                      runtime.captures.size(), runtime.captured_keys.size(), uint64_t{runtime.captured_keys.contains(runtime.last_key)},
                                      runtime.drawn.size(), runtime.short_keys.size(), runtime.captured_vertices};
      for (const auto& captured : runtime.captures) values.insert(values.end(), {key_order(captured.key), captured.first_vertex, captured.vertex_count, captured.flags});
      for (const uint64_t value : values) sequence = (sequence ^ value) * 1099511628211ull;
      ++sequence_captures;
    };
    const auto fnv = [](std::initializer_list<uint64_t> values) {
      uint64_t key = 14695981039346656037ull;
      for (const uint64_t value : values) key = (key ^ value) * 1099511628211ull;
      return key;
    };
    const auto address = [](const void* pointer) { return uint64_t{reinterpret_cast<uintptr_t>(pointer)}; };
    const uint64_t index_word = (uint64_t{DXGI_FORMAT_R16_UINT} << 32) | INDEX_OFFSET;
    // v166's: the shader, the arguments, the first vertex buffer, its stride and offset, the index buffer, its format and offset.
    const auto short_key = [&](uint32_t hash, ID3D11Buffer* first) {
      return fnv({hash, arguments.index_count, arguments.instance_count, arguments.first_index, static_cast<uint32_t>(arguments.vertex_offset),
                  arguments.first_instance, address(first), (uint64_t{STRIDES[0]} << 32) | OFFSETS[0], address(indices.Get()), index_word});
    };
    // v166's: the shader, the arguments (a placed draw's instances 0), VS b1-b4 and their first constants, VS t0-t3, the
    // identity's vertex buffers with their strides and offsets (a placed draw's slot 0 alone), the index buffer, the mask.
    const auto full_key = [&](uint32_t hash, ID3D11Buffer* first, bool placed, ID3D11ShaderResourceView* mask = nullptr) {
      std::vector<uint64_t> values = {hash, arguments.index_count, placed ? 0u : arguments.instance_count, arguments.first_index,
                                      static_cast<uint32_t>(arguments.vertex_offset), placed ? 0u : arguments.first_instance};
      for (int value = 0; value < (4 * 2) + 4; ++value) values.push_back(0);
      const std::array<ID3D11Buffer*, 4> bound = {first, instances.Get(), extra.Get(), nullptr};
      for (size_t slot = 0; slot < bound.size(); ++slot) {
        const bool identity = !placed || slot == 0;
        values.push_back(identity ? address(bound[slot]) : 0u);
        values.push_back(identity ? ((uint64_t{STRIDES[slot]} << 32) | OFFSETS[slot]) : 0u);
      }
      values.push_back(address(indices.Get()));
      values.push_back(index_word);
      values.push_back(address(mask));
      uint64_t key = 14695981039346656037ull;
      for (const uint64_t value : values) key = (key ^ value) * 1099511628211ull;
      return key;
    };
    const auto last_key = [&] {
      const std::unique_lock lock(runtime.mutex);
      return runtime.last_key;
    };
    // A static draw, known: its first capture marks it drawn by its key (the short key learnt), its second by the short key.
    const core::SceneShader rigid = {.hash = 0xA1B2C3D4};
    const uint64_t rigid_key = full_key(rigid.hash, vertices.Get(), false);
    uint64_t main_view_frame = 0;
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.known[rigid_key] = {};
      runtime.last_key = 0;
      runtime.scene_frame = UINT64_MAX;
      main_view_frame = runtime.main_view_frame;
    }
    bind(vertices.Get());
    capture(rigid, rigid_key);
    const bool rigid_keyed = last_key() == rigid_key && moving_buffer == indices.Get() && moving_offset == INDEX_OFFSET;
    bool short_learnt = false;
    // A known mesh's draw returns before the main view's capture: the scene drawn all the same (Update's loading-screen test).
    bool known_scene = false;
    {
      const std::unique_lock lock(runtime.mutex);
      const auto entry = runtime.short_keys.find(short_key(rigid.hash, vertices.Get()));
      short_learnt = entry != runtime.short_keys.end() && entry->second.key == rigid_key && !entry->second.ambiguous;
      known_scene = runtime.scene_frame == runtime.frame && runtime.main_view_frame == main_view_frame;
      runtime.last_key = 0;
      runtime.drawn.clear();
      runtime.known[rigid_key].frame = 0;
      runtime.scene_frame = UINT64_MAX;
    }
    moving_buffer = nullptr;
    capture(rigid, rigid_key);
    bool short_taken = false;
    bool short_scene = false;
    {
      const std::unique_lock lock(runtime.mutex);
      short_taken = runtime.last_key == rigid_key && std::ranges::count(runtime.drawn, rigid_key) == 1;
      short_scene = runtime.scene_frame == runtime.frame && runtime.main_view_frame == main_view_frame;
    }
    // A rows shader's draw from a dynamic vertex buffer (not placed: all four slots in its key) and a placed one (slot 0).
    static constexpr std::array<uint8_t, 4> ROWS = {1, 2, 3, 4};
    const core::SceneShader rows = {.hash = 0x5EED0001, .rows_shader = ROWS};
    bind(dynamic_vertices.Get());
    capture(rows, full_key(rows.hash, dynamic_vertices.Get(), false));
    const bool dynamic_keyed = last_key() == full_key(rows.hash, dynamic_vertices.Get(), false);
    bind(vertices.Get());
    capture(rows, full_key(rows.hash, vertices.Get(), true));
    const bool placed_keyed = last_key() == full_key(rows.hash, vertices.Get(), true);
    // Meshes with a cut-out version and their mask at t0: a static one's key takes the mask only where the game's shadow
    // pass alpha-tests its texture (looked up in the scene's lock), first not, then so; a placed one known, its texture
    // read once for that lookup and its atlas slice's (the state check reads it again each time).
    const auto mask_view = [&] {
      ComPtr<ID3D11Texture2D> texture;
      const D3D11_TEXTURE2D_DESC desc = {.Width = 4, .Height = 4, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                         .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      ComPtr<ID3D11ShaderResourceView> view;
      if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()))) {
        device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf());
      }
      return view;
    }();
    CHECK(mask_view);
    ComPtr<ID3D11Resource> mask_texture;
    mask_view->GetResource(mask_texture.GetAddressOf());
    static constexpr std::array<uint8_t, 4> CUT_OUT = {5, 6, 7, 8};
    immediate->PSSetShaderResources(0, 1, mask_view.GetAddressOf());
    const core::SceneShader solid_static = {.hash = 0x5EED0002, .cut_out_shader = CUT_OUT};
    capture(solid_static, full_key(solid_static.hash, vertices.Get(), false));
    const bool solid_keyed = last_key() == full_key(solid_static.hash, vertices.Get(), false);
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.cut_out_textures.insert(mask_texture.Get());
    }
    const core::SceneShader cut_out_static = {.hash = 0x5EED0003, .cut_out_shader = CUT_OUT};
    capture(cut_out_static, full_key(cut_out_static.hash, vertices.Get(), false, mask_view.Get()));
    const bool cut_out_keyed = last_key() == full_key(cut_out_static.hash, vertices.Get(), false, mask_view.Get());
    const core::SceneShader cut_out_placed = {.hash = 0x5EED0004, .rows_shader = ROWS, .cut_out_shader = CUT_OUT};
    const uint64_t cut_out_placed_key = full_key(cut_out_placed.hash, vertices.Get(), true, mask_view.Get());
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.known[cut_out_placed_key] = {};
    }
    capture(cut_out_placed, cut_out_placed_key);
    bool cut_out_placed_counted = false;
    {
      const std::unique_lock lock(runtime.mutex);
      cut_out_placed_counted = runtime.last_key == cut_out_placed_key && runtime.known[cut_out_placed_key].counted_frame == runtime.frame;
      runtime.cut_out_textures.clear();
    }
    ID3D11ShaderResourceView* const no_view = nullptr;
    immediate->PSSetShaderResources(0, 1, &no_view);
    // A new static draw into the main view's depth (the bridge's size), its camera at b0: its capture prepares the device,
    // the next takes the cached one; the frame's room full, both stop before drawing.
    // A depth of `slices` (more than one: a cascades' array).
    const auto depth_of = [&](UINT width, UINT height, UINT slices) {
      ComPtr<ID3D11Texture2D> texture;
      const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = slices, .Format = DXGI_FORMAT_D32_FLOAT,
                                         .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_DEPTH_STENCIL};
      D3D11_DEPTH_STENCIL_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_D32_FLOAT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY};
      view_desc.Texture2DArray = {.MipSlice = 0, .FirstArraySlice = 0, .ArraySize = slices};
      ComPtr<ID3D11DepthStencilView> view;
      if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()))) {
        device->CreateDepthStencilView(texture.Get(), (slices == 1) ? nullptr : &view_desc, view.GetAddressOf());
      }
      return view;
    };
    auto depth = depth_of(64, 32, 1);
    CHECK(depth);
    const auto kept_size = std::pair{bridge.width, bridge.height};
    bridge.width = 64;
    bridge.height = 32;
    core::internal::host.camera = [](ID3D11Buffer*, core::Camera*) { return true; };
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.captured_vertices = core::internal::MAX_VERTICES;
    }
    const core::SceneShader fresh = {.hash = 0x0F0E0D0C};
    bind(vertices.Get());
    immediate->OMSetRenderTargets(0, nullptr, depth.Get());
    immediate->VSSetConstantBuffers(0, 1, camera_constants.GetAddressOf());
    const auto prepared_for = [&] {
      const std::unique_lock lock(runtime.mutex);
      return runtime.objects.context;
    };
    capture(fresh, full_key(fresh.hash, vertices.Get(), false));
    const bool prepared = prepared_for() == immediate.Get();
    capture(fresh, full_key(fresh.hash, vertices.Get(), false));
    const bool still_prepared = prepared_for() == immediate.Get();
    // The depth's description read once a view (the view held to the frame's end): the main view's again from it, a size
    // change against it, a new view after the last one's only other holder let it go, a cascade's view and back, each
    // rejected where the other's belongs. The main view's frame and the prepared context say how far a capture came.
    CHECK(runtime.depth_seen.view == depth);
    // Did the capture pass the main view's test (its frame noted) / the cascade's (the device's context set again).
    const auto main_passed = [&](const core::SceneShader& shader) {
      {
        const std::unique_lock lock(runtime.mutex);
        runtime.main_view_frame = 0;
      }
      capture(shader, full_key(shader.hash, vertices.Get(), false));
      const std::unique_lock lock(runtime.mutex);
      return runtime.main_view_frame == runtime.frame;
    };
    const auto cascade_passed = [&](const core::SceneShader& shader) {
      {
        const std::unique_lock lock(runtime.mutex);
        runtime.objects.context = nullptr;
      }
      capture(shader, full_key(shader.hash, vertices.Get(), false));
      return prepared_for() == immediate.Get();
    };
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.address_space_capture = true;
    }
    const bool same_view_passed = main_passed(fresh) && main_passed(fresh);
    bool chain_capture = false;
    {
      const std::unique_lock lock(runtime.mutex);
      chain_capture = !runtime.address_space_capture && std::exchange(runtime.address_space_trace, false)
                      && std::ranges::count_if(log_lines, [](const std::string& line) {
                           return line.find("the first main-view capture after it: address space free ") != std::string::npos;
                         }) == 1;
    }
    CHECK(runtime.depth_seen.view == depth);
    bridge.width = 32;
    bridge.height = 16;
    const bool resized_rejected = !main_passed(fresh);
    bridge.width = 64;
    bridge.height = 32;
    const bool size_back_passed = main_passed(fresh);
    const auto second_depth = depth_of(64, 32, 1);
    CHECK(second_depth);
    immediate->OMSetRenderTargets(0, nullptr, second_depth.Get());
    depth.Reset();  // the held view's last holder: replaced (and released) at the next capture
    const bool new_view_passed = main_passed(fresh);
    CHECK(runtime.depth_seen.view == second_depth);
    static constexpr std::array<uint8_t, 1> KEEP = {1};
    const core::SceneShader cascade = {.hash = 0x0CA5CADE, .cascade_keep = KEEP};
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.settings.casters_out_of_view = true;
    }
    const auto cascade_depth = depth_of(32, 32, 4);
    CHECK(cascade_depth);
    const bool cascade_into_main_rejected = !cascade_passed(cascade);
    immediate->OMSetRenderTargets(0, nullptr, cascade_depth.Get());
    const bool cascade_view_passed = cascade_passed(cascade);
    CHECK(runtime.depth_seen.view == cascade_depth);
    const bool main_into_cascade_rejected = !main_passed(fresh);
    immediate->OMSetRenderTargets(0, nullptr, second_depth.Get());
    const bool back_to_main_passed = main_passed(fresh);
    CHECK(runtime.depth_seen.view == second_depth);
    // With room: a new draw's triangles streamed out through the game's own vertex shader (its code kept, as OnInitPipeline
    // keeps it), its capture in the frame's list with its key; the same draw again only finds that key.
    constexpr char STREAMED_VS[] = "float4 main(float4 position : POSITION) : SV_Position { return position; }";
    ComPtr<ID3DBlob> streamed_code;
    CHECK(SUCCEEDED(D3DCompile(STREAMED_VS, sizeof(STREAMED_VS) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &streamed_code, nullptr)));
    ComPtr<ID3D11VertexShader> streamed_shader;
    ComPtr<ID3D11InputLayout> streamed_layout;
    const D3D11_INPUT_ELEMENT_DESC streamed_element = {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    CHECK(SUCCEEDED(device->CreateVertexShader(streamed_code->GetBufferPointer(), streamed_code->GetBufferSize(), nullptr, &streamed_shader)));
    CHECK(SUCCEEDED(device->CreateInputLayout(&streamed_element, 1, streamed_code->GetBufferPointer(), streamed_code->GetBufferSize(), &streamed_layout)));
    const core::SceneShader streamed = {.hash = 0x57EA4ED0};
    {
      const std::unique_lock lock(runtime.mutex);
      const auto* code = static_cast<const uint8_t*>(streamed_code->GetBufferPointer());
      core::internal::game_code[streamed.hash].assign(code, code + streamed_code->GetBufferSize());
      runtime.captured_vertices = 0;
      runtime.captures.clear();
      runtime.slices.clear();
      runtime.captured_keys.clear();
    }
    immediate->VSSetShader(streamed_shader.Get(), nullptr, 0);
    immediate->IASetInputLayout(streamed_layout.Get());
    const auto captured = [&] {
      const std::unique_lock lock(runtime.mutex);
      return runtime.captures.size() == 1 && runtime.captures[0].key == runtime.last_key && runtime.captures[0].vertex_count == 36 * 3
             && runtime.captured_keys.size() == 1 && runtime.captured_keys.contains(runtime.last_key) && runtime.captured_vertices == 36 * 3;
    };
    capture(streamed, full_key(streamed.hash, vertices.Get(), false));
    const bool captured_once = captured();
    capture(streamed, full_key(streamed.hash, vertices.Get(), false));
    const bool captured_key_found = captured();
    immediate->VSSetShader(nullptr, nullptr, 0);
    immediate->IASetInputLayout(nullptr);
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.captured_vertices = core::internal::MAX_VERTICES;
      runtime.settings.casters_out_of_view = false;
    }
    std::printf("5 the depth read once a view: the same view's again %d (the address space's line once %d), the main size changed %d and back %d, a "
                "new view %d, a cascade's into the main depth %d, into its own %d, the main view into the cascade's %d and back %d; with room captured "
                "once %d, again its key found %d\n",
                same_view_passed, chain_capture, resized_rejected, size_back_passed, new_view_passed, cascade_into_main_rejected, cascade_view_passed,
                main_into_cascade_rejected, back_to_main_passed, captured_once, captured_key_found);
    CHECK(same_view_passed && chain_capture && resized_rejected && size_back_passed && new_view_passed && cascade_into_main_rejected
          && cascade_view_passed);
    CHECK(main_into_cascade_rejected && back_to_main_passed && captured_once && captured_key_found);
    std::printf("5 sequence %016llX over %u captures, %u keys not their formula's\n", static_cast<unsigned long long>(sequence), sequence_captures,
                keys_unexpected);
    CHECK(keys_unexpected == 0);
    const size_t check_lines = std::ranges::count_if(log_lines, [](const std::string& line) { return line.find("State check") != std::string::npos; });
    // CONTROL: the description held differs from the view's (a held view's never does): the state check logs it once.
    log_lines.clear();
    runtime.depth_seen.desc.Width += 1;
    capture(fresh, full_key(fresh.hash, vertices.Get(), false));
    const bool depth_control_logged = std::ranges::count_if(log_lines, [](const std::string& line) { return line.find("the depth's description read once differs") != std::string::npos; }) == 1;
    std::printf("5 CONTROL the depth's description held differs: logged %d\n", depth_control_logged);
    CHECK(depth_control_logged);
    // CONTROL: a vertex buffer bound between two reads of one draw.
    log_lines.clear();
    {
      core::internal::BoundInput input(immediate.Get(), 0xC0117201);
      input.ReadVertexBuffers(4);
      bind(extra.Get());
      input.ReadVertexBuffers(1);
    }
    const bool control_logged = std::ranges::count_if(log_lines, [](const std::string& line) {
                                  return line.find("State check: a vertex buffer read once differs") != std::string::npos;
                                }) == 1;
    std::printf("5 Capture's reads: static draw key %d, short key learnt %d, short path taken %d, the scene drawn by both (main view's frame "
                "kept) %d %d, dynamic rows draw key %d, placed draw key %d, cut-out mask: not alpha-tested %d, alpha-tested %d, placed and known "
                "%d, device prepared %d then cached %d, state check lines %zu; CONTROL a binding changed between reads logged %d\n",
                rigid_keyed, short_learnt, short_taken, known_scene, short_scene, dynamic_keyed, placed_keyed, solid_keyed, cut_out_keyed,
                cut_out_placed_counted, prepared, still_prepared, check_lines, control_logged);
    CHECK(rigid_keyed && short_learnt && short_taken && dynamic_keyed && placed_keyed && prepared && still_prepared);
    CHECK(known_scene && short_scene);
    CHECK(solid_keyed && cut_out_keyed && cut_out_placed_counted);
    CHECK(check_lines == 0);
    CHECK(control_logged);
    // Cost: a static draw's reads in v166 (the dynamic test's four slots with each buffer's desc, the topology, MeshKey's
    // index buffer, the short key's first slot and index buffer, each released) against now's (the four slots and the index
    // buffer once).
    bind(vertices.Get());
    constexpr int READS = 1000000;
    const auto time_reads = [&](auto&& body) {
      const auto start = std::chrono::steady_clock::now();
      for (int call = 0; call < READS; ++call) body();
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / READS;
    };
    volatile uint64_t read_sink = 0;
    const double old_reads = time_reads([&] {
      std::array<ID3D11Buffer*, 4> buffers = {};
      std::array<UINT, 4> strides = {};
      std::array<UINT, 4> offsets = {};
      immediate->IAGetVertexBuffers(0, 4, buffers.data(), strides.data(), offsets.data());
      bool dynamic = false;
      for (auto* buffer : buffers) {
        if (buffer == nullptr) continue;
        D3D11_BUFFER_DESC desc = {};
        buffer->GetDesc(&desc);
        dynamic = dynamic || desc.Usage == D3D11_USAGE_DYNAMIC;
        buffer->Release();
      }
      D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      immediate->IAGetPrimitiveTopology(&topology);
      ComPtr<ID3D11Buffer> mesh_indices;
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
      UINT offset = 0;
      immediate->IAGetIndexBuffer(mesh_indices.GetAddressOf(), &format, &offset);
      ID3D11Buffer* first = nullptr;
      ID3D11Buffer* draw_indices = nullptr;
      immediate->IAGetVertexBuffers(0, 1, &first, strides.data(), offsets.data());
      immediate->IAGetIndexBuffer(&draw_indices, &format, &offset);
      read_sink = address(first) ^ address(draw_indices) ^ address(mesh_indices.Get()) ^ (dynamic ? 1u : 0u) ^ topology;
      if (first != nullptr) first->Release();
      if (draw_indices != nullptr) draw_indices->Release();
    });
    const double new_reads = time_reads([&] {
      core::internal::BoundInput input(immediate.Get(), 0);
      input.ReadVertexBuffers(4);
      bool dynamic = false;
      for (auto* buffer : input.vertex_buffers) {
        if (buffer == nullptr) continue;
        D3D11_BUFFER_DESC desc = {};
        buffer->GetDesc(&desc);
        dynamic = dynamic || desc.Usage == D3D11_USAGE_DYNAMIC;
      }
      D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      immediate->IAGetPrimitiveTopology(&topology);
      input.ReadIndexBuffer();
      read_sink = address(input.vertex_buffers[0]) ^ address(input.index_buffer) ^ (dynamic ? 1u : 0u) ^ topology;
    });
    std::printf("5 a static draw's reads: v166 %.1f ns, now %.1f ns (each identity read also skips part 2's QueryInterface + Release, "
                "each capture GetDevice + Release)\n",
                old_reads, new_reads);
    // Update's loading-screen test from the draw paths (Host::scene_release, the foreground test answered here): the static
    // draw known (Capture) or marked drawn (MarkDrawn: R.U.S.E.'s replay) before each present, each present a second after
    // the scene's draw it knew of: kept 100 presents; CONTROL no draw: let go at the 30th.
    core::internal::host.scene_release = true;
    core::internal::game_in_front = [] { return true; };
    core::Settings present_settings;
    bool scene_gone = false;
    {
      const std::unique_lock lock(runtime.mutex);
      present_settings = runtime.settings;
      scene_gone = runtime.scene_gone;
      runtime.known[rigid_key] = {.frame = runtime.frame};
      runtime.release_line_tick = 0;
    }
    const auto present_after = [&](auto&& draw) {
      draw();
      {
        const std::unique_lock lock(runtime.mutex);
        runtime.scene_tick = GetTickCount64() - core::internal::SCENE_GONE_MS;
      }
      core::Update(present_settings);
    };
    const auto rigid_kept = [&] {
      const std::unique_lock lock(runtime.mutex);
      return runtime.known.contains(rigid_key)
             && std::ranges::none_of(log_lines, [](const std::string& line) { return line.find("(a loading screen)") != std::string::npos; });
    };
    bind(vertices.Get());
    for (int present = 0; present < 100; ++present) present_after([&] { core::internal::Capture(command, rigid); });
    const bool captured_kept = rigid_kept();
    bool marked = true;
    for (int present = 0; present < 100; ++present) present_after([&] { marked = core::MarkDrawn(rigid_key) && marked; });
    const bool marked_kept = marked && rigid_kept();
    for (int present = 0; present < 29; ++present) present_after([] {});
    const bool undrawn_kept_29 = rigid_kept();
    present_after([] {});
    const bool undrawn_let_go = !rigid_kept()
                                && std::ranges::count_if(log_lines, [](const std::string& line) { return line.find("(a loading screen)") != std::string::npos; }) == 1;
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.scene_gone = scene_gone;
      runtime.address_space_release = 0;
    }
    core::internal::game_in_front = twattila::device_end::GameInFront;
    core::internal::host.scene_release = false;
    std::printf("5 the loading-screen test from the draw paths, 100 presents each: the static draw captured, kept %d; marked drawn, kept %d; "
                "CONTROL no draw: kept at 29 %d, let go at 30 %d\n",
                captured_kept, marked_kept, undrawn_kept_29, undrawn_let_go);
    CHECK(captured_kept && marked_kept && undrawn_kept_29 && undrawn_let_go);
    bridge.width = kept_size.first;
    bridge.height = kept_size.second;
    bridge.ray_tracing = false;
    immediate->ClearState();
  }

  // 6.
  {
    namespace rt_bridge = twattila::dlaa_bridge;
    auto& bridge = rt_bridge::bridge;
    const auto kept_size = std::pair{bridge.width, bridge.height};
    bridge.ray_tracing = true;
    bridge.failed = true;  // the request stops at once: the trace not taken, so tried again in the same frame
    bridge.width = 64;
    bridge.height = 32;
    bridge.rt_light_tile_row = 32;
    core::internal::host = {.render_size = [] { return std::array<UINT, 2>{64, 32}; }};
    constexpr UINT CONSTANT_BYTES = ((rt_bridge::RT_ALL_LIGHTS * 2) + 1) * 16;
    ComPtr<ID3D11Buffer> constants_read;
    const D3D11_BUFFER_DESC read_desc = {.ByteWidth = CONSTANT_BYTES, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    CHECK(SUCCEEDED(device->CreateBuffer(&read_desc, nullptr, constants_read.GetAddressOf())));
    {
      const std::unique_lock lock(runtime.mutex);
      auto& objects = runtime.objects;
      if (!objects.constants) {
        const D3D11_BUFFER_DESC desc = {.ByteWidth = 96, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
        CHECK(SUCCEEDED(device->CreateBuffer(&desc, nullptr, objects.constants.GetAddressOf())));
      }
      if (!objects.light_constants) {
        const D3D11_BUFFER_DESC desc = {.ByteWidth = CONSTANT_BYTES, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
        CHECK(SUCCEEDED(device->CreateBuffer(&desc, nullptr, objects.light_constants.GetAddressOf())));
      }
      // Nothing captured (part 5's room left full): the trace reaches its lights with no dispatch.
      runtime.captured_vertices = 0;
      runtime.world_vertices = core::internal::WORLD_BASE;
      runtime.tessellated_draws = 0;
      runtime.particles = 0;
      runtime.captures.clear();
      runtime.slices.clear();
      runtime.uv_scales.clear();
      runtime.drawn.clear();
      runtime.placements.clear();
      runtime.compact.clear();
      runtime.new_sets.clear();
      runtime.batch_draws.clear();
      runtime.timed = false;
      runtime.frame = 900;
      runtime.traced_frame = 0;
    }
    const auto old_sort = [](std::vector<core::LocalLight>* candidates, size_t count, const auto& score) {
      std::partial_sort(candidates->begin(), candidates->begin() + static_cast<ptrdiff_t>(count), candidates->end(),
                        [&](const core::LocalLight& a, const core::LocalLight& b) { return score(a) > score(b); });
      return std::vector<core::LocalLight>(candidates->begin(), candidates->begin() + static_cast<ptrdiff_t>(count));
    };
    // CONTROLS (each must differ somewhere): nth_element, then the first `count` sorted; the plan's form, the picks by the
    // scores' places with lights_last left in its own order; ties flipped (>=).
    const auto nth_sort = [](std::vector<core::LocalLight>* candidates, size_t count, const auto& score) {
      const auto by_score = [&](const core::LocalLight& a, const core::LocalLight& b) { return score(a) > score(b); };
      std::nth_element(candidates->begin(), candidates->begin() + static_cast<ptrdiff_t>(count), candidates->end(), by_score);
      std::sort(candidates->begin(), candidates->begin() + static_cast<ptrdiff_t>(count), by_score);
      return std::vector<core::LocalLight>(candidates->begin(), candidates->begin() + static_cast<ptrdiff_t>(count));
    };
    const auto order_kept_sort = [](std::vector<core::LocalLight>* candidates, size_t count, const auto& score) {
      std::vector<std::pair<float, uint32_t>> ranks;
      for (uint32_t index = 0; index < candidates->size(); ++index) ranks.emplace_back(score((*candidates)[index]), index);
      std::partial_sort(ranks.begin(), ranks.begin() + static_cast<ptrdiff_t>(count), ranks.end(),
                        [](const auto& a, const auto& b) { return a.first > b.first; });
      std::vector<core::LocalLight> chosen;
      for (size_t rank = 0; rank < count; ++rank) chosen.push_back((*candidates)[ranks[rank].second]);
      return chosen;
    };
    const auto ties_flipped_sort = [](std::vector<core::LocalLight>* candidates, size_t count, const auto& score) {
      std::partial_sort(candidates->begin(), candidates->begin() + static_cast<ptrdiff_t>(count), candidates->end(),
                        [&](const core::LocalLight& a, const core::LocalLight& b) { return score(a) >= score(b); });
      return std::vector<core::LocalLight>(candidates->begin(), candidates->begin() + static_cast<ptrdiff_t>(count));
    };
    const auto same_lights = [](const std::vector<core::LocalLight>& a, const std::vector<core::LocalLight>& b) {
      if (a.size() != b.size()) return false;
      for (size_t index = 0; index < a.size(); ++index) {
        if (std::memcmp(a[index].light.data(), b[index].light.data(), sizeof(a[index].light)) != 0
            || std::memcmp(a[index].colour.data(), b[index].colour.data(), sizeof(a[index].colour)) != 0
            || a[index].shadows_only != b[index].shadows_only || a[index].falloff != b[index].falloff) {
          return false;
        }
      }
      return true;
    };
    const auto same_picks = [](const PickedLights& a, const PickedLights& b) {
      return a.lights.size() == b.lights.size() && a.colours.size() == b.colours.size() && a.kinds == b.kinds
             && std::memcmp(a.lights.data(), b.lights.data(), a.lights.size() * sizeof(a.lights[0])) == 0
             && std::memcmp(a.colours.data(), b.colours.data(), a.colours.size() * sizeof(a.colours[0])) == 0;
    };

    std::mt19937 random(1118);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    const auto one_of = [&](std::initializer_list<float> values) {
      return *(values.begin() + std::uniform_int_distribution<size_t>(0, values.size() - 1)(random));
    };
    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const float signalling_nan = std::bit_cast<float>(0x7FA00001u);
    const float inf = std::numeric_limits<float>::infinity();
    // `coarse`: places a few metres apart in whole metres, reach and colours of a few values (many equal scores, and
    // the 1 m floor of the distance); else spread over 300 m. 15% an earlier light's place and reach with its colour's
    // channels turned (the same score, another colour), `special` 5% one field zero, -0, NaN, inf, huge, tiny.
    const auto make_lights = [&](size_t light_count, const core::Camera& camera, bool coarse, bool special) {
      std::vector<core::LocalLight> lights;
      for (size_t index = 0; index < light_count; ++index) {
        const float roll = unit(random);
        core::LocalLight light;
        if (index > 0 && roll < 0.15f) {
          light = lights[std::uniform_int_distribution<size_t>(0, index - 1)(random)];
          std::rotate(light.colour.begin(), light.colour.begin() + 1, light.colour.begin() + 3);
        } else if (coarse) {
          for (int axis = 0; axis < 3; ++axis) light.light[axis] = camera.position[axis] + one_of({-2.f, -1.f, 0.f, 1.f, 2.f});
          light.light[3] = one_of({0.f, 1.f, 2.f, 4.f});
          for (int channel = 0; channel < 3; ++channel) light.colour[channel] = one_of({0.f, 0.5f, 1.f});
        } else {
          for (int axis = 0; axis < 3; ++axis) light.light[axis] = camera.position[axis] + ((unit(random) - 0.5f) * 300.f);
          light.light[3] = unit(random) * 30.f;
          for (int channel = 0; channel < 3; ++channel) light.colour[channel] = unit(random) * 4.f;
        }
        light.colour[3] = unit(random);
        if (special && roll > 0.95f) {
          const float value = one_of({0.f, -0.f, quiet_nan, -quiet_nan, signalling_nan, inf, -inf, 3e38f, 1e-30f, -1.f,
                                      std::numeric_limits<float>::denorm_min()});
          const int field = std::uniform_int_distribution<int>(0, 6)(random);
          if (field < 4) {
            light.light[field] = value;
          } else {
            light.colour[field - 4] = value;
          }
        }
        light.shadows_only = unit(random) < 0.2f;
        light.falloff = (unit(random) < 0.5f ? rt_bridge::RT_LIGHT_SMOOTH : rt_bridge::RT_LIGHT_LINEAR);
        lights.push_back(light);
      }
      return lights;
    };
    const auto camera_at = [](std::array<float, 3> position) {
      core::Camera camera;
      camera.position = position;
      return camera;
    };
    const auto trace = [&](const core::Camera& camera) {
      core::Trace(immediate.Get(), nullptr, camera);
      const std::unique_lock lock(runtime.mutex);
      PickedLights picked = {.lights = runtime.lights, .colours = runtime.light_colours, .kinds = runtime.light_kinds};
      immediate->CopyResource(constants_read.Get(), runtime.objects.light_constants.Get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (SUCCEEDED(immediate->Map(constants_read.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        std::memcpy(picked.constants.data(), mapped.pData, CONSTANT_BYTES);
        immediate->Unmap(constants_read.Get(), 0);
        picked.written = true;
      }
      return std::pair{picked, runtime.lights_last};
    };

    // Random frames, each traced twice in the frame (the helper never takes it), against the old pick.
    struct Case {
      std::vector<core::LocalLight> lights;
      core::Settings settings;
      bool bounced = false;
      std::array<core::Camera, 2> cameras;
    };
    std::vector<Case> cases;
    for (int index = 0; index < 1600; ++index) {
      const float size_roll = unit(random);
      size_t light_count = 0;
      if (index < 40) {
        light_count = std::array<size_t, 8>{0, 1, 2, 3, 128, 129, 4096, 16384}[index % 8];
      } else if (size_roll < 0.4f) {
        light_count = std::uniform_int_distribution<size_t>(0, 40)(random);
      } else if (size_roll < 0.75f) {
        light_count = std::uniform_int_distribution<size_t>(0, 400)(random);
      } else if (size_roll < 0.97f) {
        light_count = std::uniform_int_distribution<size_t>(0, 3000)(random);
      } else {
        light_count = std::uniform_int_distribution<size_t>(3000, 16384)(random);
      }
      const bool coarse = unit(random) < 0.5f;
      const auto place = [&] {
        if (coarse) return std::array<float, 3>{one_of({0.f, 10.f, -37.f}), one_of({0.f, 2.f}), one_of({0.f, 5.f, -100.f})};
        return std::array<float, 3>{(unit(random) - 0.5f) * 2000.f, unit(random) * 50.f, (unit(random) - 0.5f) * 2000.f};
      };
      Case frame = {.cameras = {camera_at(place())}};
      // The retry at the same camera half the time (one pass again), else another.
      frame.cameras[1] = (unit(random) < 0.5f ? frame.cameras[0] : camera_at(place()));
      frame.lights = make_lights(light_count, frame.cameras[0], coarse, unit(random) < 0.5f);
      frame.settings.local_lights = true;
      frame.settings.light_count = std::uniform_int_distribution<uint32_t>(0, 256)(random);
      frame.settings.light_range = (unit(random) < 0.3f ? (coarse ? one_of({1.f, 2.f, 3.f}) : 5.f + (unit(random) * 145.f)) : 0.f);
      frame.settings.light_glow = unit(random) * 2.f;
      frame.settings.light_bounce = unit(random) < 0.5f;
      frame.settings.path_tracing = unit(random) < 0.7f;
      frame.settings.lights_in_bounce = unit(random) < 0.5f;
      frame.settings.light_ratio = unit(random) < 0.3f;
      frame.bounced = unit(random) < 0.7f;
      cases.push_back(std::move(frame));
    }
    size_t compared = 0;
    size_t ties = 0;
    size_t specials = 0;
    for (const Case& frame : cases) {
      {
        const std::unique_lock lock(runtime.mutex);
        runtime.settings = frame.settings;
        runtime.bounced_frame = (frame.bounced ? runtime.frame - 1 : UINT64_MAX);
        runtime.lights_last = frame.lights;
      }
      std::vector<core::LocalLight> old_last = frame.lights;
      for (const core::Camera& camera : frame.cameras) {
        const PickedLights old_picked = PickLights(&old_last, frame.settings, camera, frame.bounced, bridge.rt_light_tile_row, old_sort);
        const auto [new_picked, new_last] = trace(camera);
        const bool same = same_picks(new_picked, old_picked) && same_lights(new_last, old_last)
                          && (!old_picked.written || std::memcmp(new_picked.constants.data(), old_picked.constants.data(), CONSTANT_BYTES) == 0);
        if (!same) {
          std::printf("6 FAIL: %zu lights, count %u, range %.1f: %zu picked now, %zu before\n", frame.lights.size(), frame.settings.light_count,
                      frame.settings.light_range, new_picked.lights.size(), old_picked.lights.size());
        }
        CHECK(same);
        ++compared;
        // Ties among the picks: neighbours of one place and reach (an earlier light's, its colour turned).
        for (size_t index = 1; index < old_picked.lights.size(); ++index) {
          if (std::memcmp(old_picked.lights[index].data(), old_picked.lights[index - 1].data(), sizeof(old_picked.lights[index])) == 0) ++ties;
        }
      }
      specials += std::ranges::count_if(frame.lights, [](const core::LocalLight& light) {
        return std::ranges::any_of(light.light, [](float value) { return !std::isfinite(value) || value == 0.f; })
               || std::ranges::any_of(light.colour, [](float value) { return !std::isfinite(value); });
      });
    }
    // The controls on the same frames: a frame differs when either of its two picks does.
    const auto differing = [&](const auto& sort) {
      size_t frames = 0;
      for (const Case& frame : cases) {
        std::vector<core::LocalLight> old_last = frame.lights;
        std::vector<core::LocalLight> other_last = frame.lights;
        bool same = true;
        for (const core::Camera& camera : frame.cameras) {
          const PickedLights old_picked = PickLights(&old_last, frame.settings, camera, frame.bounced, 0, old_sort);
          const PickedLights other_picked = PickLights(&other_last, frame.settings, camera, frame.bounced, 0, sort);
          same = same && same_picks(old_picked, other_picked);
        }
        if (!same) ++frames;
      }
      return frames;
    };
    const size_t nth_differs = differing(nth_sort);
    const size_t order_kept_differs = differing(order_kept_sort);
    const size_t ties_flipped_differs = differing(ties_flipped_sort);
    std::printf("6 the light pick: %zu frames of 0..16384 lights (count 0..256, ranges, shadows-only lights left out), each traced twice "
                "in the frame, %zu picks (%zu tied neighbours among them, %zu lights with a zero, NaN or inf): lights, colours, kinds, b9 "
                "and lights_last the old pick's, to the bit; CONTROLS frames apart: nth_element %zu, lights_last in its own order %zu, "
                "ties flipped %zu\n",
                cases.size(), compared, ties, specials, nth_differs, order_kept_differs, ties_flipped_differs);
    CHECK(ties > 1000 && specials > 1000);
    CHECK(nth_differs > 0 && order_kept_differs > 0 && ties_flipped_differs > 0);

    // Cost: the old pick against Trace's (Trace with the lights less Trace with one; both from the same lights each run;
    // taken: less the reorder a trace not taken adds).
    const auto median = [](std::vector<double> values) {
      std::ranges::sort(values);
      return values[values.size() / 2];
    };
    const core::Camera timed_camera = camera_at({120.f, 10.f, -340.f});
    for (const size_t light_count : {300u, 1000u, 3000u, 8192u}) {
      const std::vector<core::LocalLight> lights = make_lights(light_count, timed_camera, false, false);
      for (const uint32_t picks : {32u, 64u, 128u}) {
        core::Settings settings;
        settings.local_lights = true;
        settings.light_count = picks;
        std::vector<double> old_ms;
        std::vector<double> new_ms;
        std::vector<double> one_ms;
        std::vector<double> reorder_ms;
        for (int run = 0; run < 21; ++run) {
          std::vector<core::LocalLight> old_last = lights;
          const auto old_start = std::chrono::steady_clock::now();
          PickLights(&old_last, settings, timed_camera, false, 0, old_sort);
          old_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - old_start).count());
          const auto timed_trace = [&](const std::vector<core::LocalLight>& last) {
            {
              const std::unique_lock lock(runtime.mutex);
              runtime.settings = settings;
              runtime.lights_last = last;
            }
            const auto start = std::chrono::steady_clock::now();
            core::Trace(immediate.Get(), nullptr, timed_camera);
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
          };
          new_ms.push_back(timed_trace(lights));
          // The not-taken trace's part (all of them here): lights_last put in the pick's order, as Trace does it.
          {
            const std::unique_lock lock(runtime.mutex);
            std::vector<core::LocalLight> ranked;
            ranked.reserve(lights.size());
            const auto start = std::chrono::steady_clock::now();
            for (const auto& rank : runtime.light_ranks) ranked.push_back(lights[rank.index]);
            reorder_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
          }
          one_ms.push_back(timed_trace({lights[0]}));
        }
        const double now_ms = median(new_ms) - median(one_ms);
        std::printf("6 %5zu lights, %3u picked: the old pick %.4f ms, now %.4f ms taken, %.4f ms not taken (Trace %.4f less Trace with one "
                    "light %.4f, its reorder %.4f; medians)\n",
                    light_count, picks, median(old_ms), now_ms - median(reorder_ms), now_ms, median(new_ms), median(one_ms), median(reorder_ms));
      }
    }
    bridge.width = kept_size.first;
    bridge.height = kept_size.second;
    bridge.ray_tracing = false;
    bridge.failed = false;
    bridge.rt_light_tile_row = 0;
    immediate->ClearState();
  }

  // 7.
  {
    auto& bridge = twattila::dlaa_bridge::bridge;
    bridge.failed = true;  // Trace stops at its request: not taken
    std::mt19937 random(1119);
    std::uniform_int_distribution<int> roll(0, 99);
    const auto replaced_now = [&] {
      const std::unique_lock lock(runtime.mutex);
      return core::internal::AmbientReplacedNow();
    };
    // A trace the helper took: traced_frame as Trace's taken path sets it, with its publish.
    const auto trace_taken = [&] {
      const std::unique_lock lock(runtime.mutex);
      runtime.traced_frame = runtime.frame;
      core::internal::PublishAmbientReplaced();
    };
    const auto random_settings = [&] {
      core::Settings settings;
      settings.ambient_occlusion = roll(random) < 50;
      settings.path_ambient = roll(random) < 40;
      settings.sun_shadows = roll(random) < 50;
      settings.light_bounce = roll(random) < 30;
      settings.path_tracing = settings.light_bounce && roll(random) < 50;
      settings.reflections = roll(random) < 20;
      return settings;
    };
    // The test's own writes before (parts 5 and 6) went past the writers: the first Update publishes.
    core::Update(random_settings());
    // The state check (on since part 5) compares each published answer under the lock too: none logged, then the
    // CONTROL's one.
    const auto published_lines = [] {
      return std::ranges::count_if(log_lines, [](const std::string& line) { return line.find("AmbientReplaced's published answer differs") != std::string::npos; });
    };
    log_lines.clear();
    size_t compared = 0;
    std::array<size_t, 2> answers = {};
    size_t long_untraced = 0;
    uint32_t untraced_run = 0;
    for (int step = 0; step < 20000; ++step) {
      const int kind = roll(random);
      if (kind < 4) {
        trace_taken();
        untraced_run = 0;
      } else if (kind < 8) {
        core::Trace(immediate.Get(), nullptr, core::Camera{});
      } else if (kind < 20) {
        core::Update(random_settings());
        ++untraced_run;
      } else {
        // The settings kept, a new frame.
        core::Settings settings;
        {
          const std::unique_lock lock(runtime.mutex);
          settings = runtime.settings;
        }
        core::Update(settings);
        ++untraced_run;
      }
      if (untraced_run > core::internal::AMBIENT_REPLACED_FRAMES) ++long_untraced;
      const bool now = replaced_now();
      CHECK(core::AmbientReplaced() == now);
      ++answers[now ? 1 : 0];
      ++compared;
    }
    const auto loop_lines = published_lines();
    // CONTROL: traced_frame written without its publish: the lock-free answer stays the old one, the state check logs it.
    core::Settings ambient = {.ambient_occlusion = true};
    core::Update(ambient);
    for (uint64_t frame = 0; frame <= core::internal::AMBIENT_REPLACED_FRAMES; ++frame) core::Update(ambient);
    const bool control_before = core::AmbientReplaced() == replaced_now();
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.traced_frame = runtime.frame;
    }
    const bool control_differs = core::AmbientReplaced() != replaced_now();
    const bool control_logged = loop_lines == 0 && published_lines() == 1;
    std::printf("7 AmbientReplaced lock-free against AmbientReplacedNow under the lock: %zu writes (Update with random rows and frames, "
                "traces taken and not), the same each time (%zu replaced, %zu not, %zu past %llu frames untraced), state check lines %zu; "
                "CONTROL traced_frame without its publish differs %d, logged once %d\n",
                compared, answers[1], answers[0], long_untraced, static_cast<unsigned long long>(core::internal::AMBIENT_REPLACED_FRAMES),
                static_cast<size_t>(loop_lines), control_before && control_differs, control_logged);
    CHECK(answers[0] > 1000 && answers[1] > 1000 && long_untraced > 1000);
    CHECK(control_before && control_differs && control_logged);
    {
      const std::unique_lock lock(runtime.mutex);
      core::internal::PublishAmbientReplaced();
    }
    constexpr int CALLS_7 = 2000000;
    const auto time_7 = [&](const char* name, auto&& body) {
      const auto start = std::chrono::steady_clock::now();
      for (int call = 0; call < CALLS_7; ++call) body();
      std::printf("7 %-40s %6.1f ns a call (uncontended)\n", name,
                  std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / CALLS_7);
    };
    volatile bool replaced_sink = false;
    time_7("AmbientReplaced, the lock (before)", [&] { replaced_sink = replaced_now(); });
    // Its load as off the state check (on here: AmbientReplaced itself takes the lock).
    time_7("AmbientReplaced, lock-free (now)", [&] { replaced_sink = core::internal::ambient_replaced.load(std::memory_order_acquire); });
    bridge.failed = false;
  }

  // 8.
  {
    auto& bridge = twattila::dlaa_bridge::bridge;
    bridge.ray_tracing = true;
    bridge.failed = true;  // Trace stops at its request, as in 7
    core::internal::fog_shadows_installed = true;
    const twattila::feature_settings::AaSliders upscaling = {.method = 5.f};
    core::Sliders sliders;
    float strength = 1.f;
    std::vector<renodx::utils::settings::Setting*> rows;
    core::AppendSettings(&rows, &sliders, &upscaling, false, true, twattila::feature_settings::AA_SECTION, &strength);
    const auto row = std::ranges::find_if(rows, [](const renodx::utils::settings::Setting* setting) { return setting->key == "RayTracedSpecularOcclusion"; });
    CHECK(row != rows.end());
    // The build before's SettingsOf (Specular Occlusion with an upscaler alone) and Configure (rt_scene.hpp at fca62ddf9).
    const auto old_settings = [&] {
      core::Settings settings = core::SettingsOf(sliders, upscaling, strength);
      settings.specular_occlusion = sliders.specular_occlusion != 0.f && twattila::feature_settings::Upscaling(upscaling);
      return settings;
    };
    const auto old_configure = [](const core::Settings& settings, twattila::anti_aliasing::Settings* aa) {
      aa->ray_tracing = settings.sun_shadows || settings.ambient_occlusion || settings.light_bounce || settings.reflections
                        || settings.specular_occlusion || settings.local_lights || settings.fog_shadows;
      aa->ray_reconstruction = aa->ray_reconstruction && aa->ray_tracing && !core::internal::CheapFogAlone(settings);
      aa->ray_traced_guides = settings.path_tracing || settings.reflections || settings.specular_occlusion;
      aa->ray_traced_reflections = settings.reflections || settings.specular_occlusion;
    };
    // What a frame with these settings starts: the bridge's textures and Ray Reconstruction (Configure), the rays on
    // (Update: Enabled, active), a trace asked for, a scene draw's capture, the row's is_enabled.
    struct Started {
      bool ray_tracing = false;
      bool ray_reconstruction = false;
      bool guides = false;
      bool reflections = false;
      bool enabled = false;
      bool active = false;
      bool trace_asked = false;
      bool captured = false;

      bool operator==(const Started&) const = default;
    };
    const core::SceneShader scene_shader = {.hash = 0x5EED0010};
    const auto started = [&](const core::Settings& settings, bool configure_old) {
      twattila::anti_aliasing::Settings aa = {.ray_reconstruction = true};
      if (configure_old) {
        old_configure(settings, &aa);
      } else {
        core::Configure(settings, &aa);
      }
      core::Update(settings);
      Started result = {.ray_tracing = aa.ray_tracing, .ray_reconstruction = aa.ray_reconstruction, .guides = aa.ray_traced_guides,
                        .reflections = aa.ray_traced_reflections, .active = core::internal::active.load()};
      {
        const std::unique_lock lock(runtime.mutex);
        result.enabled = core::internal::Enabled();
        runtime.trace_asked = false;
      }
      core::Trace(immediate.Get(), nullptr, core::Camera{});
      {
        const std::unique_lock lock(runtime.mutex);
        result.trace_asked = runtime.trace_asked;
      }
      action::CommandContext<action::DrawIndexedArguments> draw = {.cmd_list = &cmd_list, .arguments = {.index_count = 3, .instance_count = 1},
                                                                   .callback_data = &scene_shader};
      result.captured = core::OnSceneDraw.operator()<core::SceneShader>(draw).post_callback != nullptr;
      return result;
    };
    const auto all_on = [](const Started& s) { return s.ray_tracing && s.ray_reconstruction && s.guides && s.reflections && s.enabled && s.active && s.trace_asked && s.captured; };
    const auto all_off = [](const Started& s) { return !(s.ray_tracing || s.ray_reconstruction || s.guides || s.reflections || s.enabled || s.active || s.trace_asked || s.captured); };
    // Specular Occlusion alone: off, as with the row off (the same settings), nothing started, the row greyed; CONTROL: the
    // build before started all of it. The Light Bounce row with the Global Illumination at 0 traces nothing: alone too.
    struct Case {
      const char* name;
      core::Sliders sliders;
      float strength = 1.f;
    };
    const std::array<Case, 2> alone = {{
        {.name = "alone", .sliders = {.specular_occlusion = 1.f}},
        {.name = "with the Light Bounce row, GI 0", .sliders = {.light_bounce = 1.f, .specular_occlusion = 1.f}, .strength = 0.f},
    }};
    for (const Case& test : alone) {
      sliders = test.sliders;
      strength = test.strength;
      const core::Settings settings = core::SettingsOf(sliders, upscaling, strength);
      core::Sliders without = test.sliders;
      without.specular_occlusion = 0.f;
      CHECK(!settings.specular_occlusion && settings == core::SettingsOf(without, upscaling, strength));
      CHECK(all_off(started(settings, false)));
      CHECK(!(*row)->is_enabled());
      const core::Settings before = old_settings();
      CHECK(before.specular_occlusion && all_on(started(before, true)));
      std::printf("8 Specular Occlusion %s: off (settings as with the row off), no bridge textures, no Ray Reconstruction, Enabled false, "
                  "no trace asked, no capture, the row greyed; CONTROL the build before: all of them on\n", test.name);
    }
    // With another ray-traced row: everything as the build before (settings, Configure, Update), the row enabled.
    const std::array<Case, 6> with_others = {{
        {.name = "Ray-Traced Shadows", .sliders = {.sun_shadows = 1.f, .specular_occlusion = 1.f}},
        {.name = "Ray-Traced AO", .sliders = {.ambient_occlusion = 1.f, .specular_occlusion = 1.f}},
        {.name = "Light Bounce", .sliders = {.light_bounce = 1.f, .specular_occlusion = 1.f}},
        {.name = "Path Tracing", .sliders = {.light_bounce = 2.f, .specular_occlusion = 1.f}},
        {.name = "Reflections", .sliders = {.reflections = 1.f, .specular_occlusion = 1.f}},
        {.name = "Fog Shadows", .sliders = {.fog_shadows = 1.f, .specular_occlusion = 1.f}},
    }};
    for (const Case& test : with_others) {
      sliders = test.sliders;
      strength = test.strength;
      const core::Settings settings = core::SettingsOf(sliders, upscaling, strength);
      const core::Settings before = old_settings();
      CHECK(settings.specular_occlusion && settings == before);
      const Started now = started(settings, false);
      CHECK(all_on(now) && now == started(before, true));
      CHECK((*row)->is_enabled());
      std::printf("8 Specular Occlusion with %s: on, settings / Configure / Update as the build before, the row enabled\n", test.name);
    }
    // Without an upscaler: off with every row, as before.
    sliders = {.sun_shadows = 1.f, .specular_occlusion = 1.f};
    CHECK(!core::SettingsOf(sliders, {}, strength).specular_occlusion);
    core::Update({});
    core::internal::fog_shadows_installed = false;
    bridge.failed = false;
  }
  std::printf("PASS\n");
  return 0;
}
