// The kept camera copies (twattila/frame_history.hpp KeepCamera, AttilaCamera) as their callers hand them their buffers, on
// WARP. WRL's ComPtr operator& gives a ComPtr* only after releasing what it holds, so `KeepCamera(..., &member)` (v157's
// callers) made a new buffer at every call; std::addressof(member) keeps it.
// 1. Ten frames of a changing camera: KeepCamera keeps one buffer (std::addressof) and holds this frame's camera after each
//    call, byte for byte the same as the v157 form's new buffer (CONTROL: the v157 form's buffer changes every call).
// 2. AttilaCamera with Warhammer III's kind of runs: one buffer kept, each frame's remap = the v157 form's, byte for byte.
// 3. Cost a call (printed): v157's form against the kept buffer.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "src/games/twattila/frame_history.hpp"

using Microsoft::WRL::ComPtr;
namespace history = twattila::frame_history;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Kept buffers test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static std::vector<uint8_t> Read(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  if (FAILED(device->CreateBuffer(&staging_desc, nullptr, &staging))) return {};
  context->CopyResource(staging.Get(), buffer);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
  std::vector<uint8_t> bytes(desc.ByteWidth);
  std::memcpy(bytes.data(), mapped.pData, desc.ByteWidth);
  context->Unmap(staging.Get(), 0);
  return bytes;
}

int main() {
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }
  // The game's camera constants: 30 rows (Attila's camera_VS_PS), rewritten each frame.
  constexpr UINT CAMERA_BYTES = 30 * 16;
  const D3D11_BUFFER_DESC camera_desc = {.ByteWidth = CAMERA_BYTES, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> camera;
  CHECK(SUCCEEDED(device->CreateBuffer(&camera_desc, nullptr, &camera)));
  const auto write_camera = [&](int frame) {
    std::array<float, CAMERA_BYTES / 4> values = {};
    for (size_t index = 0; index < values.size(); ++index) values[index] = static_cast<float>(frame * 1000 + static_cast<int>(index));
    context->UpdateSubresource(camera.Get(), 0, nullptr, values.data(), 0, 0);
  };

  // 1.
  ComPtr<ID3D11Buffer> kept;
  ComPtr<ID3D11Buffer> v157;
  ID3D11Buffer* first_kept = nullptr;
  uint32_t v157_buffers = 0;
  for (int frame = 0; frame < 10; ++frame) {
    write_camera(frame);
    history::KeepCamera(device.Get(), context.Get(), camera.Get(), std::addressof(kept));
    // The last buffer held here, so a new one can't take its address.
    const ComPtr<ID3D11Buffer> held = v157;
    history::KeepCamera(device.Get(), context.Get(), camera.Get(), &v157);
    if (frame == 0) first_kept = kept.Get();
    CHECK(kept.Get() == first_kept);
    v157_buffers += (v157.Get() != held.Get()) ? 1u : 0u;
    const auto camera_bytes = Read(camera.Get());
    CHECK(!camera_bytes.empty() && Read(kept.Get()) == camera_bytes && Read(v157.Get()) == camera_bytes);
  }
  CHECK(v157_buffers == 10);  // CONTROL: the v157 form's buffer is new every call
  std::printf("1 KeepCamera, 10 frames: kept 1 buffer, v157 form %u buffers; both = this frame's camera after each call\n", v157_buffers);

  // 2. A layout of another game's: three runs into Attila's rows (WH3's kind), the rest of Attila's rows not written.
  const std::array<history::CameraRun, 3> runs = {{{.destination = 0, .source = 64, .bytes = 64},
                                                    {.destination = 64, .source = 0, .bytes = 64},
                                                    {.destination = 256, .source = 128, .bytes = 96}}};
  ComPtr<ID3D11Buffer> remapped;
  ComPtr<ID3D11Buffer> remapped_v157;
  ID3D11Buffer* first_remapped = nullptr;
  for (int frame = 0; frame < 10; ++frame) {
    write_camera(frame + 20);
    ID3D11Buffer* result = history::AttilaCamera(device.Get(), context.Get(), camera.Get(), runs, std::addressof(remapped));
    ID3D11Buffer* result_v157 = history::AttilaCamera(device.Get(), context.Get(), camera.Get(), runs, &remapped_v157);
    if (frame == 0) first_remapped = remapped.Get();
    CHECK(result == remapped.Get() && remapped.Get() == first_remapped && result_v157 == remapped_v157.Get());
    CHECK(Read(result) == Read(result_v157));
  }
  std::printf("2 AttilaCamera (3 runs), 10 frames: kept 1 buffer; its remap = the v157 form's (a new buffer each call)\n");

  // 3.
  constexpr int CALLS = 20000;
  const auto time = [&](const char* name, auto&& body) {
    const auto start = std::chrono::steady_clock::now();
    for (int call = 0; call < CALLS; ++call) body();
    context->Flush();
    std::printf("3 %-40s %7.2f us a call\n", name, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / CALLS);
  };
  time("KeepCamera, v157 form (&member)", [&] { history::KeepCamera(device.Get(), context.Get(), camera.Get(), &v157); });
  time("KeepCamera, kept buffer", [&] { history::KeepCamera(device.Get(), context.Get(), camera.Get(), std::addressof(kept)); });
  time("AttilaCamera, v157 form (&member)", [&] { history::AttilaCamera(device.Get(), context.Get(), camera.Get(), runs, &remapped_v157); });
  time("AttilaCamera, kept buffer", [&] { history::AttilaCamera(device.Get(), context.Get(), camera.Get(), runs, std::addressof(remapped)); });
  std::printf("PASS\n");
  return 0;
}
