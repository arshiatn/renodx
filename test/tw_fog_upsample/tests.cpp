// twattila/rt_fog.hlsli's upsample of the helper's quarter-size fog texels (bug4_, DEBUG44 (d): a light rim in whole fog
// texels around cypresses against the sky and the far hills under Torch and Fire Light in Fog). WARP, a probe pixel shader
// with rt_fog.hlsli and the upsample as it was (its RtFogSun / RtFogLights before bug4_), a 64 x 48 render view: the sky
// at the top, a cypress (50 m) across it and the hills (2000 m) below; the fog texels (16 x 12) as the helper writes them
// from their centre pixel (4 x + 2): the sky's sun 1 and no glow, the tree's and the hills' their own random values.
// 1. A sky pixel: the sky's values exactly (sun 1, glow 0). CONTROL: the old upsample gives sky pixels the tree's glow (the rim).
// 2. A surface pixel: its corners of its kind (within a factor of 2 of its distance) alone, renormalized, else all four as
//    before: equal (1e-5) to that weighting on the CPU; where all four are its kind, to the old bilinear (1e-5).
// 3. t45 unbound (a game without the positions): the old upsample (1e-6) everywhere.
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
#include <random>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

constexpr int W = 64, H = 48, FW = W / 4, FH = H / 4;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static const char* PROBE = R"(
#include "rt_fog.hlsli"
// The upsample before bug4_ (bilinear, no depth).
float OldSun(float2 render_pixel) {
  uint width, height;
  t_rt_fog.GetDimensions(width, height);
  if (width == 0u) return 1.f;
  const float2 texel = render_pixel / 4.f - 0.625f;
  const int2 base = int2(floor(texel));
  const float2 blend = texel - base;
  const int2 last = int2(width, height) - 1;
  float corners[4];
  [unroll] for (uint corner = 0u; corner < 4u; ++corner) corners[corner] = t_rt_fog.Load(int3(clamp(base + int2(corner & 1u, corner >> 1), 0, last), 0));
  return lerp(RT_FOG_SHADE, 1.f, lerp(lerp(corners[0], corners[1], blend.x), lerp(corners[2], corners[3], blend.x), blend.y));
}
float3 OldLights(float2 render_pixel) {
  uint width, height;
  t_rt_fog_lights.GetDimensions(width, height);
  if (width == 0u) return 0.f;
  const float2 texel = render_pixel / 4.f - 0.625f;
  const int2 base = int2(floor(texel));
  const float2 blend = texel - base;
  const int2 last = int2(width, height) - 1;
  float3 corners[4];
  [unroll] for (uint corner = 0u; corner < 4u; ++corner) corners[corner] = t_rt_fog_lights.Load(int3(clamp(base + int2(corner & 1u, corner >> 1), 0, last), 0)).rgb;
  return lerp(lerp(corners[0], corners[1], blend.x), lerp(corners[2], corners[3], blend.x), blend.y) * asfloat(rt_light_count.y);
}
void main(float4 v0 : SV_Position, out float4 o0 : SV_Target0, out float4 o1 : SV_Target1) {
  o0 = float4(RtFogLights(v0.xy), RtFogSun(v0.xy));
  o1 = float4(OldLights(v0.xy), OldSun(v0.xy));
}
)";

static const char* FULL_SCREEN = R"(
float4 main(uint id : SV_VertexID) : SV_Position {
  const float2 corner = float2((id << 1) & 2, id & 2);
  return float4(corner * float2(2, -2) + float2(-1, 1), 0.5, 1);
}
)";

// The scene's distance at a render pixel: the cypress (x 24..31) 50 m, the sky above y 16, the hills 2000 m below.
static float Distance(int x, int y) {
  if (x >= 24 && x < 32) return 50.f;
  return (y < 16) ? 0.f : 2000.f;
}

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
  // Named as if beside rt_fog.hlsli: the standard include handler resolves its own include from there.
  const std::string source_name = std::string(argv[1]) + "/src/games/twattila/fog_probe.hlsl";
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

  // The fog texels from their centre pixel, as the helper's rt_fog.
  std::mt19937 rng(44);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  std::vector<float> sun(FW * FH);
  std::vector<std::array<float, 4>> lights(FW * FH);
  for (int y = 0; y < FH; ++y) {
    for (int x = 0; x < FW; ++x) {
      const float at = Distance(std::min(x * 4 + 2, W - 1), std::min(y * 4 + 2, H - 1));
      const float glow = (at == 50.f) ? 0.6f + 0.4f * unit(rng) : 0.1f + 0.1f * unit(rng);
      sun[y * FW + x] = (at <= 0.f) ? 1.f : unit(rng);
      lights[y * FW + x] = (at <= 0.f) ? std::array<float, 4>{} : std::array<float, 4>{glow, glow * 0.7f, glow * 0.4f, 0.f};
    }
  }
  std::vector<std::array<float, 4>> positions(W * H);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) positions[y * W + x] = {0.f, 0.f, 0.f, Distance(x, y)};
  }
  const auto sun_view = Texture(sun.data(), FW, FH, DXGI_FORMAT_R32_FLOAT, 4);
  const auto lights_view = Texture(lights.data(), FW, FH, DXGI_FORMAT_R32G32B32A32_FLOAT, 16);
  const auto positions_view = Texture(positions.data(), W, H, DXGI_FORMAT_R32G32B32A32_FLOAT, 16);
  std::vector<uint8_t> light_constants(257 * 16 + 128 * 16, 0);
  const float glow_setting = 1.f;
  std::memcpy(light_constants.data() + 128 * 16 + 4, &glow_setting, 4);
  const D3D11_BUFFER_DESC buffer_desc = {.ByteWidth = static_cast<UINT>(light_constants.size()), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA buffer_init = {.pSysMem = light_constants.data()};
  ComPtr<ID3D11Buffer> light_buffer;
  device->CreateBuffer(&buffer_desc, &buffer_init, &light_buffer);

  const auto run = [&](bool with_positions) {
    D3D11_TEXTURE2D_DESC desc = {.Width = W, .Height = H, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                 .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
    std::array<ComPtr<ID3D11Texture2D>, 2> textures;
    std::array<ComPtr<ID3D11RenderTargetView>, 2> targets;
    for (int index = 0; index < 2; ++index) {
      device->CreateTexture2D(&desc, nullptr, &textures[index]);
      device->CreateRenderTargetView(textures[index].Get(), nullptr, &targets[index]);
    }
    std::array<ID3D11RenderTargetView*, 2> raw = {targets[0].Get(), targets[1].Get()};
    std::array<ID3D11ShaderResourceView*, 3> views = {sun_view.Get(), lights_view.Get(), with_positions ? positions_view.Get() : nullptr};
    context->PSSetShaderResources(43, 3, views.data());
    context->PSSetConstantBuffers(9, 1, light_buffer.GetAddressOf());
    context->OMSetRenderTargets(2, raw.data(), nullptr);
    const D3D11_VIEWPORT viewport = {.Width = W, .Height = H, .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(full_screen.Get(), nullptr, 0);
    context->PSSetShader(probe.Get(), nullptr, 0);
    context->Draw(3, 0);
    std::array<std::vector<float>, 2> out;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int index = 0; index < 2; ++index) {
      ComPtr<ID3D11Texture2D> staging;
      device->CreateTexture2D(&desc, nullptr, &staging);
      context->CopyResource(staging.Get(), textures[index].Get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
      out[index].resize(W * H * 4);
      for (int y = 0; y < H; ++y) std::memcpy(out[index].data() + y * W * 4, static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, W * 16);
      context->Unmap(staging.Get(), 0);
    }
    context->ClearState();
    return out;
  };
  const auto with = run(true);
  const auto without = run(false);
  int failures = 0;
  uint64_t sky = 0, sky_wrong = 0, rim = 0, surface = 0, surface_apart = 0, whole = 0, whole_apart = 0, unbound_apart = 0;
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const float* now = with[0].data() + (y * W + x) * 4;
      const float* old = with[1].data() + (y * W + x) * 4;
      for (int channel = 0; channel < 4; ++channel) {
        unbound_apart += (std::fabs(without[0][(y * W + x) * 4 + channel] - without[1][(y * W + x) * 4 + channel]) > 1e-6f) ? 1 : 0;
      }
      const float here = Distance(x, y);
      if (here <= 0.f) {
        ++sky;
        sky_wrong += (now[0] != 0.f || now[1] != 0.f || now[2] != 0.f || now[3] != 1.f) ? 1 : 0;
        rim += (old[0] > 1e-4f) ? 1 : 0;
        continue;
      }
      ++surface;
      // The CPU's weighting (rt_fog.hlsli RtFogCorners).
      const float tx = (x + 0.5f) / 4.f - 0.625f, ty = (y + 0.5f) / 4.f - 0.625f;
      const int bx = static_cast<int>(std::floor(tx)), by = static_cast<int>(std::floor(ty));
      const float fx = tx - bx, fy = ty - by;
      std::array<float, 4> bilinear = {}, weights = {};
      std::array<int, 4> index = {};
      float kept = 0.f;
      for (int corner = 0; corner < 4; ++corner) {
        const int cx = std::clamp(bx + (corner & 1), 0, FW - 1), cy = std::clamp(by + (corner >> 1), 0, FH - 1);
        index[corner] = cy * FW + cx;
        bilinear[corner] = ((corner & 1) ? fx : 1.f - fx) * ((corner >> 1) ? fy : 1.f - fy);
        const float there = Distance(std::min(cx * 4 + 2, W - 1), std::min(cy * 4 + 2, H - 1));
        weights[corner] = (there > 0.5f * here && there < 2.f * here) ? bilinear[corner] : 0.f;
        kept += weights[corner];
      }
      const bool all_kept = std::fabs(kept - 1.f) < 1e-6f;
      std::array<float, 4> expected = {0.f, 0.f, 0.f, 0.f};
      for (int corner = 0; corner < 4; ++corner) {
        const float weight = (kept > 0.f) ? weights[corner] / kept : bilinear[corner];
        for (int channel = 0; channel < 3; ++channel) expected[channel] += weight * lights[index[corner]][channel];
        expected[3] += weight * sun[index[corner]];
      }
      expected[3] = 0.35f + (1.f - 0.35f) * expected[3];
      bool apart = false, apart_old = false;
      for (int channel = 0; channel < 4; ++channel) {
        apart = apart || std::fabs(now[channel] - expected[channel]) > 1e-5f;
        apart_old = apart_old || std::fabs(now[channel] - old[channel]) > 1e-5f;
      }
      surface_apart += apart ? 1 : 0;
      if (all_kept) {
        ++whole;
        whole_apart += apart_old ? 1 : 0;
      }
    }
  }
  const auto check = [&](const char* what, bool ok) {
    std::printf("  %s: %s\n", what, ok ? "ok" : "FAIL");
    failures += ok ? 0 : 1;
  };
  std::printf("1 sky pixels %llu: %llu not the sky's values\n", static_cast<unsigned long long>(sky), static_cast<unsigned long long>(sky_wrong));
  check("sky pixels the sky's values", sky != 0 && sky_wrong == 0);
  std::printf("  CONTROL: the old upsample gives %llu sky pixels glow (the rim)\n", static_cast<unsigned long long>(rim));
  check("CONTROL the old rim exists", rim != 0);
  std::printf("2 surface pixels %llu: %llu apart from the CPU's weighting; %llu with all four of their kind, %llu apart from the old bilinear\n",
              static_cast<unsigned long long>(surface), static_cast<unsigned long long>(surface_apart), static_cast<unsigned long long>(whole),
              static_cast<unsigned long long>(whole_apart));
  check("surface pixels as the CPU's weighting", surface_apart == 0);
  check("all four of their kind: the old bilinear", whole != 0 && whole_apart == 0);
  std::printf("3 t45 unbound: %llu values apart from the old upsample\n", static_cast<unsigned long long>(unbound_apart));
  check("unbound: the old upsample", unbound_apart == 0);
  std::printf(failures == 0 ? "FOG UPSAMPLE ALL PASSED\n" : "FOG UPSAMPLE: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
