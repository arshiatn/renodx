// The ray tracing's context lookup (twattila/rt_core.hpp rt_scene::internal::NativeContext: every captured draw and the rt_scene
// draw callbacks) against v157's (copied below), on WARP with ReShade's command list mocked.
// 1. Same answer: the immediate context; a deferred context (null); another API (null); a command list whose context
//    changes (the new one at once); a null context; back to the first.
// 2. Cost a call, old and new (printed), with the d3d11 runtime's per-call prices our draw callbacks pay (QueryInterface
//    failing and succeeding, Get + Release of a view, AddRef + Release).
#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>
#include "src/games/twattila/rt_core.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
namespace core = twattila::rt_scene;

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
  std::printf("PASS\n");
  return 0;
}
