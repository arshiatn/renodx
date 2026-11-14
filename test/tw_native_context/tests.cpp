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
#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdio>
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
  std::printf("PASS\n");
  return 0;
}
