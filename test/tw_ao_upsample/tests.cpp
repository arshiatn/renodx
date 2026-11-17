// twattila/ao_upsample.hlsli's AoUpsample (the half-resolution AO brought up to the game's size: Attila / Thrones / Rome II
// bitmask_ssao, WH1 / WH2 / WH3 line sweep, Shogun 2, Pharaoh / Troy ssao01), on WARP: a probe pixel shader with this
// tree's ao_upsample.hlsli and the upsample as it was (its 3 x 3 around floor(pixel / scale)). Every writer's result texel
// stands for its block's top-left pixel (texel x scale).
// 1. A ramp across x (and one across y) at one distance, scale 2 and 4: the upsampled picture's shift against the ramp
//    (pixels; a centred window reproduces a ramp). Fixed: 0 at every pixel at scale 2 (every caller's), 0 on average at
//    4. CONTROL: the old window's shift (I3: -0.39 px on average at Half, the picture toward the bottom-right).
// 2. A depth edge (10 m against 20 m, each side its own constant result): every pixel its side's value exactly, both
//    shaders (the depth weights unchanged).
// tests.exe <repo>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

constexpr int W = 64, H = 48;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static const char* PROBE = R"(
#include "ao_upsample.hlsli"
Texture2D<float2> results : register(t0);
Texture2D<float> distances : register(t1);
cbuffer probe : register(b0) { float4 probe_scale; };
// The upsample before hfix_ (the 3 x 3 around floor(pixel / scale)).
float OldUpsample(Texture2D<float2> half_results, int2 pixel, float distance, float scale) {
  const int2 center = int2(floor(float2(pixel) / scale));
  float2 sum = 0.f;
  float closest = 1.f;
  float closest_difference = 1e30f;
  [unroll]
  for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
      const int2 texel = center + int2(x, y);
      const float2 result = half_results.Load(int3(texel, 0));
      if (!(result.y > 0.f)) continue;
      const float2 offset = (float2(texel) * scale - float2(pixel)) / scale;
      const float near = exp2(-0.25f * dot(offset, offset));
      const float difference = abs(result.y - distance);
      sum += float2(result.x, 1.f) * (near * saturate(1.f - difference / (0.030005792f * distance)));
      [flatten]
      if (difference < closest_difference) {
        closest = result.x;
        closest_difference = difference;
      }
    }
  }
  return (sum.y > 0.f) ? (sum.x / sum.y) : closest;
}
void main(float4 v0 : SV_Position, out float2 o0 : SV_Target0) {
  const float distance = distances.Load(int3(v0.xy, 0));
  o0 = float2(AoUpsample(results, int2(v0.xy), distance, probe_scale.x), OldUpsample(results, int2(v0.xy), distance, probe_scale.x));
}
)";

static const char* FULL_SCREEN = R"(
float4 main(uint id : SV_VertexID) : SV_Position {
  const float2 corner = float2((id << 1) & 2, id & 2);
  return float4(corner * float2(2, -2) + float2(-1, 1), 0.5, 1);
}
)";

static ComPtr<ID3D11ShaderResourceView> Texture(const void* data, int width, int height, DXGI_FORMAT format, UINT pixel_bytes) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = static_cast<UINT>(width), .Height = static_cast<UINT>(height), .MipLevels = 1, .ArraySize = 1,
                                     .Format = format, .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = data, .SysMemPitch = static_cast<UINT>(width) * pixel_bytes};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, &init, &texture);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
  // Named as if beside ao_upsample.hlsli: the standard include handler resolves it from there.
  const std::string source_name = std::string(argv[1]) + "/src/games/twattila/ao_probe.hlsl";
  ComPtr<ID3DBlob> ps_blob, vs_blob, errors;
  if (FAILED(D3DCompile(PROBE, std::strlen(PROBE), source_name.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &ps_blob, &errors))) {
    std::printf("probe: %s\nFAIL\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return 1;
  }
  D3DCompile(FULL_SCREEN, std::strlen(FULL_SCREEN), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, nullptr);
  ComPtr<ID3D11PixelShader> probe;
  ComPtr<ID3D11VertexShader> full_screen;
  device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &probe);
  device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &full_screen);

  // One draw of the probe over results (`scale`: their texels' size in pixels) and the pixels' distances: new, old a pixel.
  const auto run = [&](const std::vector<std::array<float, 2>>& results, int scale, const std::vector<float>& distances) {
    const int result_width = W / scale, result_height = H / scale;
    const auto results_view = Texture(results.data(), result_width, result_height, DXGI_FORMAT_R32G32_FLOAT, 8);
    const auto distances_view = Texture(distances.data(), W, H, DXGI_FORMAT_R32_FLOAT, 4);
    const std::array<float, 4> constants = {static_cast<float>(scale), 0.f, 0.f, 0.f};
    const D3D11_BUFFER_DESC buffer_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA buffer_init = {.pSysMem = constants.data()};
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&buffer_desc, &buffer_init, &buffer);
    D3D11_TEXTURE2D_DESC desc = {.Width = W, .Height = H, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32_FLOAT,
                                 .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
    ComPtr<ID3D11Texture2D> target_texture;
    ComPtr<ID3D11RenderTargetView> target;
    device->CreateTexture2D(&desc, nullptr, &target_texture);
    device->CreateRenderTargetView(target_texture.Get(), nullptr, &target);
    std::array<ID3D11ShaderResourceView*, 2> views = {results_view.Get(), distances_view.Get()};
    context->PSSetShaderResources(0, 2, views.data());
    context->PSSetConstantBuffers(0, 1, buffer.GetAddressOf());
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    const D3D11_VIEWPORT viewport = {.Width = W, .Height = H, .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(full_screen.Get(), nullptr, 0);
    context->PSSetShader(probe.Get(), nullptr, 0);
    context->Draw(3, 0);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&desc, nullptr, &staging);
    context->CopyResource(staging.Get(), target_texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    std::vector<std::array<float, 2>> out(W * H);
    for (int y = 0; y < H; ++y) std::memcpy(out.data() + y * W, static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, W * 8);
    context->Unmap(staging.Get(), 0);
    context->ClearState();
    return out;
  };

  int failures = 0;
  const auto check = [&](const char* what, bool ok) {
    std::printf("  %s: %s\n", what, ok ? "ok" : "FAIL");
    failures += ok ? 0 : 1;
  };
  // 1. Ramps: a result texel holds the ramp at its pixel (texel x scale, the block's top-left).
  for (const int scale : {2, 4}) {
    for (const bool across_y : {false, true}) {
      std::vector<std::array<float, 2>> results((W / scale) * (H / scale));
      for (int y = 0; y < H / scale; ++y) {
        for (int x = 0; x < W / scale; ++x) results[y * (W / scale) + x] = {0.01f * static_cast<float>((across_y ? y : x) * scale), 10.f};
      }
      const auto out = run(results, scale, std::vector<float>(W * H, 10.f));
      double new_sum = 0.0, old_sum = 0.0, new_most = 0.0, old_most = 0.0;
      int counted = 0;
      // Away from the borders (the window's clamps): 2 texels in.
      for (int y = 2 * scale; y < H - 3 * scale; ++y) {
        for (int x = 2 * scale; x < W - 3 * scale; ++x) {
          const double ramp = 0.01 * (across_y ? y : x);
          const double new_shift = (out[y * W + x][0] - ramp) / 0.01;
          const double old_shift = (out[y * W + x][1] - ramp) / 0.01;
          new_sum += new_shift;
          old_sum += old_shift;
          new_most = std::max(new_most, std::abs(new_shift));
          old_most = std::max(old_most, std::abs(old_shift));
          ++counted;
        }
      }
      std::printf("1 ramp across %s, scale %d (%d pixels): shift new %+.4f px on average (at most %.4f), old %+.4f px (at most %.4f)\n",
                  across_y ? "y" : "x", scale, counted, new_sum / counted, new_most, old_sum / counted, old_most);
      // A 1.5-texel window is centred at every pixel at scale 2 (the callers'); at 4 on average (a pixel at a quarter
      // texel takes 3 texels at -1.25, -0.25, 0.75: up to 0.78 px apart).
      check("new: no shift on average (1e-3 px)", std::abs(new_sum / counted) < 1e-3);
      if (scale == 2) check("new: no shift at any pixel at scale 2 (1e-3 px)", new_most < 1e-3);
      check("CONTROL old: shifted (0.1 px on average or more)", std::abs(old_sum / counted) > 0.1);
    }
  }
  // 2. A depth edge at x 32: 10 m with results 0.2 on the left, 20 m with 0.8 on the right.
  {
    constexpr int SCALE = 2;
    std::vector<std::array<float, 2>> results((W / SCALE) * (H / SCALE));
    std::vector<float> distances(W * H);
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) distances[y * W + x] = (x < 32) ? 10.f : 20.f;
    }
    for (int y = 0; y < H / SCALE; ++y) {
      for (int x = 0; x < W / SCALE; ++x) results[y * (W / SCALE) + x] = (x * SCALE < 32) ? std::array<float, 2>{0.2f, 10.f} : std::array<float, 2>{0.8f, 20.f};
    }
    const auto out = run(results, SCALE, distances);
    int new_apart = 0, old_apart = 0;
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        const float side = (x < 32) ? 0.2f : 0.8f;
        new_apart += (std::abs(out[y * W + x][0] - side) > 1e-6f) ? 1 : 0;
        old_apart += (std::abs(out[y * W + x][1] - side) > 1e-6f) ? 1 : 0;
      }
    }
    std::printf("2 depth edge: %d pixels not their side's value (new), %d (old)\n", new_apart, old_apart);
    check("new: every pixel its side's", new_apart == 0);
    check("old: every pixel its side's (the depth weights as before)", old_apart == 0);
  }
  std::printf(failures == 0 ? "AO UPSAMPLE ALL PASSED\n" : "AO UPSAMPLE: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
