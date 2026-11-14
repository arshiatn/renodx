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
//    before: other colours, zeros as -0, then one light moved); a NaN coordinate. Cost (printed): v160's sort of a
//    16384-light frame, the reading draw now (new places, the frame before's places), a draw that only copies.
#include <windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>
#include "src/games/twattila/rt_scene.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
namespace core = twattila::rt_scene;
namespace action = renodx::utils::command_action;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Native context test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

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
  const auto read_lights = [](const Draws& draws) {
    std::vector<core::LocalLight> lights;
    for (const auto& draw : draws) {
      for (const Instance& instance : draw) {
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
  const auto random_frame = [&](bool distinct) {
    std::vector<Instance> lights;
    std::uniform_int_distribution<int> grid(-40, 40);
    std::uniform_real_distribution<float> any(-2000.f, 2000.f);
    std::uniform_int_distribution<int> reach(0, 30);
    while (lights.size() < 16384) {
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
  frames.push_back({});
  // Each frame read three frames on (then empty frames to read the last three).
  size_t big_checked = 0;
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
  }
  CHECK(big_checked >= 3);
  std::printf("4 instanced lights: %zu frames (ties, -0 / +0, zero reach, 16384 a frame, the 8192 cap, a still camera, NaN): v160's order and lights, "
              "to the bit\n", frames.size());
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
  std::printf("PASS\n");
  return 0;
}
