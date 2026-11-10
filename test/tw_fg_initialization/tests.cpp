#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include "src/games/twattila/frame_generation.hpp"
static int log_lines = 0;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) { ++log_lines; }
using Microsoft::WRL::ComPtr;
namespace fg = twattila::frame_generation::internal;
#define CHECK(x)                                  \
  do {                                            \
    if (!(x)) {                                   \
      std::printf("FAIL %d: %s\n", __LINE__, #x); \
      std::abort();                               \
    }                                             \
  } while (false)
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
  ComPtr<ID3D11Device1> device1;
  CHECK(SUCCEEDED(device.As(&device1)));
  const char* vs = "float4 main(uint id:SV_VertexID):SV_Position{return float4((id==2?3:-1),(id==1?3:-1),0,1);}";
  const char* ps = "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
  ComPtr<ID3DBlob> vertex, pixel;
  CHECK(SUCCEEDED(D3DCompile(vs, strlen(vs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vertex, nullptr)));
  CHECK(SUCCEEDED(D3DCompile(ps, strlen(ps), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &pixel, nullptr)));
  fg::state.vertex_code = {static_cast<const uint8_t*>(vertex->GetBufferPointer()), vertex->GetBufferSize()};
  fg::state.pixel_code = {static_cast<const uint8_t*>(pixel->GetBufferPointer()), pixel->GetBufferSize()};
  fg::state.hdr10_pixel_code = fg::state.pixel_code;
  std::array<float, 4> constants = {};
  fg::state.injection = constants.data();
  fg::state.injection_bytes = sizeof(constants);
  D3D11_TEXTURE2D_DESC desc = {.Width = 64, .Height = 64, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target, output;
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &target)));
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &output)));
  auto& bridge = twattila::dlaa_bridge::bridge;
  bridge.width = bridge.height = 64;
  CHECK(SUCCEEDED(device->CreateRenderTargetView(output.Get(), nullptr, &bridge.fg_hudless_rtv)));
  const auto valid_vertex = fg::state.vertex_code;
  const auto valid_pixel = fg::state.pixel_code;
  const uint8_t invalid = 0;
  for (int failure = 0; failure < 4; ++failure) {
    fg::state.objects = {};
    if (failure == 0) fg::state.vertex_code = {&invalid, 1};
    if (failure == 1) fg::state.pixel_code = {&invalid, 1};
    if (failure == 2) fg::state.hdr10_pixel_code = {&invalid, 1};
    if (failure == 3) fg::state.injection_bytes = 0;
    CHECK(!fg::DrawHudless(context.Get(), target.Get()));
    CHECK(!fg::state.objects.Ready() && !fg::state.objects.device);
    CHECK(!fg::state.objects.vertex_shader && !fg::state.objects.context_state && !fg::state.objects.sampler);
    fg::state.vertex_code = valid_vertex;
    fg::state.pixel_code = valid_pixel;
    fg::state.hdr10_pixel_code = valid_pixel;
    fg::state.injection_bytes = sizeof(constants);
    // One warning a failure streak, and no creation while it backs off.
    CHECK(log_lines == failure + 1 && fg::state.setup_failed && fg::state.setup_skips == fg::SETUP_RETRY_CALLS);
    CHECK(!fg::DrawHudless(context.Get(), target.Get()));
    CHECK(fg::state.setup_skips == fg::SETUP_RETRY_CALLS - 1 && !fg::state.objects.Ready());
    fg::state.setup_skips = 0;
    fg::state.vertex_code = {&invalid, 1};
    CHECK(!fg::DrawHudless(context.Get(), target.Get()));
    CHECK(log_lines == failure + 1);
    fg::state.vertex_code = valid_vertex;
    fg::state.setup_skips = 0;
    CHECK(fg::DrawHudless(context.Get(), target.Get()));
    CHECK(fg::state.objects.Ready() && fg::state.hudless && !fg::state.setup_failed);
  }
  fg::state.scrgb = true;
  CHECK(fg::DrawHudless(context.Get(), target.Get()));
  CHECK(fg::state.hudless && fg::state.objects.scrgb.texture && fg::state.objects.scrgb.srv);
  D3D11_TEXTURE2D_DESC read_desc = desc;
  read_desc.Usage = D3D11_USAGE_STAGING;
  read_desc.BindFlags = 0;
  read_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> readback;
  CHECK(SUCCEEDED(device->CreateTexture2D(&read_desc, nullptr, &readback)));
  context->CopyResource(readback.Get(), output.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  CHECK(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  const auto* color = static_cast<const uint8_t*>(mapped.pData);
  CHECK(color[0] == 64 && color[1] == 128 && color[2] == 191 && color[3] == 255);
  context->Unmap(readback.Get(), 0);
  context->ClearState();
  context->Flush();
  fg::state.objects = {};
  bridge.fg_hudless_rtv.Reset();
  std::puts("PASS: four real setup failures logged once a streak, backoff, same-device retry, successful HUD-less draw and scRGB intermediate on WARP");
}
