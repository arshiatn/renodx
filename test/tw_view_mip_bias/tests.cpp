// talesofarise_dlss/constant_mirror.hpp's Texture LOD bias (ViewMipBias) on a View upload as the game maps it: a WARP
// device's dynamic constant buffer (its desc only; the mapped bytes are this test's page, as ReShade hands the game's
// pointer to map_buffer_region), watched as Tales' TAA watches its View (ViewSizeAndInvSize at c122,
// MaterialTextureMipBias at c133).
// A. The main view's upload: c133.x becomes the game's value plus the bias, for the game's 0, 0.5 and -1.25 (a game or
//    r.MipMapLODBias of its own), bias -1 and -0.415 (render 50% / 75%); every other byte as the game wrote it.
// B. Another view's size (a reflection capture, a shadow view): nothing written. Bias off (width 0): nothing written.
// C. CONTROL: built over the header before the fix (the main checkout's folder first): A fails (the bias replaced the
//    game's value).
// tests.exe; run.ps1 builds it over both headers.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "constant_mirror.hpp"  // run.ps1: this tree's talesofarise_dlss folder, or the control's, first on the include path

using Microsoft::WRL::ComPtr;
namespace mirror = talesofarise::dlss::constants;

extern "C" __declspec(dllexport) const char* const NAME = "View mip bias test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

constexpr uint32_t SIZE_VECTOR = 122;
constexpr uint32_t BIAS_VECTOR = 133;
constexpr UINT VIEW_BYTES = 4096;

int main() {
  ComPtr<ID3D11Device> device;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }
  const D3D11_BUFFER_DESC desc = {.ByteWidth = VIEW_BYTES, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                  .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  ComPtr<ID3D11Buffer> view;
  device->CreateBuffer(&desc, nullptr, view.GetAddressOf());
  mirror::Watch(view.Get(), (SIZE_VECTOR + 1) * 16);
  const reshade::api::resource resource = {reinterpret_cast<uint64_t>(view.Get())};
  // One upload of a view of `width` x `height` with the game's `game_bias` at c133.x: the page after the unmap, and
  // whether its bytes other than c133.x stayed the game's.
  const auto upload = [&](float width, float height, float game_bias, bool* others_kept) {
    std::vector<std::byte> page(VIEW_BYTES);
    for (size_t at = 0; at < page.size(); ++at) page[at] = static_cast<std::byte>((at * 37) & 0xFF);
    const std::array<float, 4> size = {width, height, 1.f / width, 1.f / height};
    std::memcpy(page.data() + SIZE_VECTOR * 16, size.data(), sizeof(size));
    std::memcpy(page.data() + BIAS_VECTOR * 16, &game_bias, sizeof(game_bias));
    const std::vector<std::byte> written = page;
    void* data = page.data();
    mirror::internal::OnMapBufferRegion(nullptr, resource, 0, UINT64_MAX, reshade::api::map_access::write_discard, &data);
    mirror::internal::OnUnmapBufferRegion(nullptr, resource);
    *others_kept = std::memcmp(page.data(), written.data(), BIAS_VECTOR * 16) == 0
                   && std::memcmp(page.data() + BIAS_VECTOR * 16 + 4, written.data() + BIAS_VECTOR * 16 + 4, VIEW_BYTES - BIAS_VECTOR * 16 - 4) == 0;
    float result;
    std::memcpy(&result, page.data() + BIAS_VECTOR * 16, sizeof(result));
    return result;
  };
  int failures = 0;
  constexpr float WIDTH = 1920.f;
  constexpr float HEIGHT = 1080.f;
  for (const float bias : {-1.f, -0.415f}) {
    mirror::SetViewMipBias({.size_vector = SIZE_VECTOR, .bias_vector = BIAS_VECTOR, .width = 1920, .height = 1080, .bias = bias});
    for (const float game : {0.f, 0.5f, -1.25f}) {
      bool kept = false;
      const float result = upload(WIDTH, HEIGHT, game, &kept);
      const bool pass = result == game + bias && kept;
      std::printf("%s A: bias %.3f over the game's %.2f: c133.x %.3f (want %.3f), other bytes %s\n", pass ? "ok  " : "FAIL", bias, game, result,
                  game + bias, kept ? "the game's" : "CHANGED");
      failures += pass ? 0 : 1;
    }
    bool kept = false;
    const float other = upload(1024.f, 1024.f, 0.5f, &kept);
    const bool pass = other == 0.5f && kept;
    std::printf("%s B: another view (1024 x 1024): c133.x %.3f (want the game's 0.500)\n", pass ? "ok  " : "FAIL", other);
    failures += pass ? 0 : 1;
  }
  mirror::SetViewMipBias({});
  bool kept = false;
  const float off = upload(WIDTH, HEIGHT, 0.5f, &kept);
  const bool pass = off == 0.5f && kept;
  std::printf("%s B: bias off: c133.x %.3f (want the game's 0.500)\n", pass ? "ok  " : "FAIL", off);
  failures += pass ? 0 : 1;
  std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
