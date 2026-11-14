// Troy's replaced shaders (src/games/twtroy/shaders) against the game's on WARP.
// tests.exe <the game's dumped csos> <the add-on's built csos (build\twtroy.include\embed)>
// 1. Fog Strength: battlefog00 / 01 (the battle fog 018FED0C and its MSAA twin D9A3B04F, 4 samples) with the camera and
//    fog constants of the fog draw in ATN's session 3 trace_04 over a synthetic depth ramp (each sample its own depth):
//    at Fog Strength 1 with nothing at b12 the game's bits; 0 no fog (rgb 0, alpha 1); 2 alpha T^2 and rgb x (1 + T) (the
//    optical depth doubled); CONTROL 0.5 differs. Below 100% (b12 xy 0.5, the draw's viewport the target's top-left
//    quarter, the screen size the target's) the game's on a target of that size; CONTROL without b12 it differs.
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
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../../src/games/twtroy/shared.h"

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static int failures = 0;

#define CHECK(x, ...)                                  \
  do {                                                 \
    if (!(x)) {                                        \
      std::printf("FAIL line %d: %s: ", __LINE__, #x); \
      std::printf(__VA_ARGS__);                        \
      std::printf("\n");                               \
      ++failures;                                      \
    }                                                  \
  } while (false)

// The fog draw's ps b0 (camera, 784 bytes) and b1 (fog, 112 bytes) in tools\tw\troy\runs\ATN_session3_030818\trace_04_031153.txt.
constexpr std::array<float, 196> CAMERA = {
    625.85455f, 126.21127f, -607.9252f, 6.6291106e-10f, 0.6160624f, 5.1151194e-09f, 0.7876973f, 93.29556f,
    -0.27021432f, 0.9393195f, 0.21133609f, 179.0387f, -0.7398995f, -0.34304333f, 0.5786795f, 858.15924f, 0.f, 0.f,
    0.f, 1.f, 0.88294804f, 0.f, 0.00022786458f, 0.f, 0.f, 1.5696855f, 0.00033150436f, 0.f, 0.f, 0.f, 1.000077f,
    -1.000077f, 0.f, 0.f, 1.f, 0.f, 0.5437825f, -7.8162906e-05f, 0.6956276f, 82.57068f, -0.42439675f, 1.4743224f,
    0.33192304f, 281.3189f, -0.7399565f, -0.34306973f, 0.5787241f, 857.2253f, -0.7398995f, -0.34304333f, 0.5786795f,
    858.15924f, 0.6160624f, -0.27021432f, -0.7398995f, 625.85455f, 5.1151208e-09f, 0.93931973f, -0.34304342f,
    126.21129f, 0.7876973f, 0.21133609f, 0.5786795f, -607.9252f, 0.f, 0.f, 0.f, 1.f, 1.1325696f, 0.f, 0.f,
    -0.00025807245f, 0.f, 0.63707036f, 0.f, -0.00021119158f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -0.99992305f, 1.f,
    0.6977302f, -0.17213511f, -625.8183f, 625.12665f, 1.1008985e-05f, 0.59840715f, -126.20397f, 125.870476f,
    0.89212406f, 0.13463713f, 607.8901f, -607.35846f, 5.375481e-09f, 1.0750962e-08f, -0.9999423f, 1.0000193f, 1.f,
    13000.f, 7.6929e-05f, 7.6929e-05f, 24.614f, 1.1325694f, 0.6370703f, 1.134464f, 3840.f, 2160.f, 0.00026041668f,
    0.00046296295f, 0.f, 6.26474e-10f, 2.6254088e-06f, 2.5607402e-12f, 1920.f, 1080.f, 0.00052083336f, 0.0009259259f,
    0.f, 0.f, 6.4094063e-10f, 6.8242946e-07f, 3840.f, 2160.f, 0.00026041668f, 0.00046296295f, 0.f, 0.f, 0.f, 0.f, 0.f,
    0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, -1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1e-45f, 1.f, 3.4444333e-12f,
    7.0366745e+22f, 6.8192746e-10f, 0.f, 0.f, 0.f, 0.f, 624.589f, 125.269806f, -608.3733f, 6.8192746e-10f, 625.9845f,
    125.26984f, -606.589f, 1.0313943e-08f, 625.64026f, 126.46669f, -606.3197f, 6.8192746e-10f, 624.24475f, 126.46666f,
    -608.104f, 1.06222515e-05f, -15825.483f, -12112.719f, -6432.9443f, 6.8192746e-10f, 2315.5881f, -12112.719f,
    16762.225f, 1.6691486e-07f, -2160.1956f, 3446.0122f, 20262.76f, 6.8192746e-10f, -20301.268f, 3446.0122f,
    -2932.4094f, 0.f, 0.f, 0.f, 1.3f, 0.f, 0.f, 0.f, 0.f, 0.f,
};
constexpr std::array<float, 28> FOG = {
    0.216225f, 0.f, 114.6838f, 4.684798f, 281.f, 0.f, 2.13e-43f, 0.f, 0.f, 0.f, 0.011765f, 1e-45f, 1.f, 130.f, 1.f,
    1.f, 120.f, 0.f, 0.f, 0.f, 0.788235f, 0.898039f, 1.f, 0.01f, 0.f, 0.f, 3.4941e-41f, 0.f,
};
constexpr UINT WIDTH = 256;
constexpr UINT HEIGHT = 128;
constexpr UINT SAMPLES = 4;
constexpr float SENTINEL = -7.f;

static std::vector<char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static ComPtr<ID3DBlob> CompileSource(const char* source, const char* target) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, &code, &errors))) {
    std::printf("compile %s: %s\n", target, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(1);
  }
  return code;
}

static ComPtr<ID3D11Buffer> ConstantBuffer(const void* data, UINT size) {
  std::vector<char> padded((size + 15) & ~15u, 0);
  std::memcpy(padded.data(), data, size);
  const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(padded.size()), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, &buffer);
  return buffer;
}

static ComPtr<ID3D11Texture2D> Texture(UINT width, UINT height, DXGI_FORMAT format, UINT samples, UINT bind) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                     .SampleDesc = {.Count = samples}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, nullptr, &texture);
  return texture;
}

static ComPtr<ID3D11VertexShader> full_screen;
static ComPtr<ID3D11PixelShader> depth_ramp;
static ComPtr<ID3D11ComputeShader> samples_out;

// Each sample's depth: view distance 2^(0..13.5) across the ramp (1.000077 (1 - 1/z), the trace's projection; near 1),
// a function of the pixel alone (the same at any target size).
constexpr char DEPTH_RAMP[] =
    "float main(float4 p : SV_Position, uint s : SV_SampleIndex) : SV_Target {\n"
    "  const float z = exp2((floor(p.x) + 0.37 * floor(p.y) + 0.25 * s) * (13.5 / 256.0));\n"
    "  return 1.000077 * (1 - 1 / z);\n"
    "}\n";

// The fog shader's output, every sample in order, read back (SENTINEL where the draw left the target).
static std::vector<float> Run(const std::vector<char>& cso, bool msaa, UINT width, UINT height, UINT viewport_width,
                              UINT viewport_height, float strength, const float* render_scale) {
  const UINT samples = msaa ? SAMPLES : 1;
  ComPtr<ID3D11PixelShader> fog;
  CHECK(SUCCEEDED(device->CreatePixelShader(cso.data(), cso.size(), nullptr, &fog)), "fog shader");
  if (fog == nullptr) return {};
  const auto depth = Texture(width, height, DXGI_FORMAT_R32_FLOAT, samples, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
  const auto target = Texture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, samples, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
  ComPtr<ID3D11RenderTargetView> depth_view;
  ComPtr<ID3D11ShaderResourceView> depth_read;
  ComPtr<ID3D11RenderTargetView> target_view;
  ComPtr<ID3D11ShaderResourceView> target_read;
  device->CreateRenderTargetView(depth.Get(), nullptr, &depth_view);
  device->CreateShaderResourceView(depth.Get(), nullptr, &depth_read);
  device->CreateRenderTargetView(target.Get(), nullptr, &target_view);
  device->CreateShaderResourceView(target.Get(), nullptr, &target_read);

  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  const D3D11_VIEWPORT whole = {.Width = static_cast<float>(width), .Height = static_cast<float>(height), .MaxDepth = 1.f};
  context->RSSetViewports(1, &whole);
  context->OMSetRenderTargets(1, depth_view.GetAddressOf(), nullptr);
  context->PSSetShader(depth_ramp.Get(), nullptr, 0);
  context->Draw(3, 0);

  auto camera = CAMERA;
  camera[108] = static_cast<float>(width);  // g_screen_size (c27): the target's, as the game's camera at its own size
  camera[109] = static_cast<float>(height);
  camera[110] = 1.f / width;
  camera[111] = 1.f / height;
  camera[116] = static_cast<float>(viewport_width);  // g_viewport_dimensions (c29)
  camera[117] = static_cast<float>(viewport_height);
  camera[118] = 1.f / viewport_width;
  camera[119] = 1.f / viewport_height;
  ShaderInjectData injection = {};
  injection.fog_strength = strength;
  const std::array<ComPtr<ID3D11Buffer>, 2> constants = {ConstantBuffer(camera.data(), sizeof(camera)), ConstantBuffer(FOG.data(), sizeof(FOG))};
  const auto injected = ConstantBuffer(&injection, sizeof(injection));
  const auto share = (render_scale != nullptr) ? ConstantBuffer(render_scale, 4 * sizeof(float)) : nullptr;
  ID3D11Buffer* bound[2] = {constants[0].Get(), constants[1].Get()};
  context->PSSetConstantBuffers(0, 2, bound);
  ID3D11Buffer* slots_12_13[2] = {share.Get(), injected.Get()};
  context->PSSetConstantBuffers(12, 2, slots_12_13);
  const float clear[4] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
  context->ClearRenderTargetView(target_view.Get(), clear);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(viewport_width), .Height = static_cast<float>(viewport_height), .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->PSSetShaderResources(0, 1, depth_read.GetAddressOf());
  context->PSSetShader(fog.Get(), nullptr, 0);
  context->Draw(3, 0);
  ID3D11RenderTargetView* no_target = nullptr;
  context->OMSetRenderTargets(1, &no_target, nullptr);
  ID3D11ShaderResourceView* no_read = nullptr;
  context->PSSetShaderResources(0, 1, &no_read);

  // Every sample (or texel) into a buffer: the compute shader reads the target at t0 (Texture2DMS) or t1 (without MSAA).
  const UINT count = width * height * samples * 4;
  const D3D11_BUFFER_DESC out_desc = {.ByteWidth = count * 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 16};
  ComPtr<ID3D11Buffer> out;
  device->CreateBuffer(&out_desc, nullptr, &out);
  ComPtr<ID3D11UnorderedAccessView> out_view;
  device->CreateUnorderedAccessView(out.Get(), nullptr, &out_view);
  std::vector<float> values(count, 0.f);
  const std::array<UINT, 4> sizes = {width, height, samples, msaa ? 1u : 0u};
  const auto size_buffer = ConstantBuffer(sizes.data(), sizeof(sizes));
  context->CSSetShader(samples_out.Get(), nullptr, 0);
  context->CSSetConstantBuffers(0, 1, size_buffer.GetAddressOf());
  ID3D11ShaderResourceView* reads[2] = {msaa ? target_read.Get() : nullptr, msaa ? nullptr : target_read.Get()};
  context->CSSetShaderResources(0, 2, reads);
  context->CSSetUnorderedAccessViews(0, 1, out_view.GetAddressOf(), nullptr);
  context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
  ID3D11ShaderResourceView* no_reads[2] = {};
  context->CSSetShaderResources(0, 2, no_reads);
  ID3D11UnorderedAccessView* no_uav = nullptr;
  context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = count * 4, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
                                          .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 16};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), out.Get());
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    std::memcpy(values.data(), mapped.pData, count * 4);
    context->Unmap(staging.Get(), 0);
  }
  return values;
}

constexpr char SAMPLES_OUT[] =
    "Texture2DMS<float4> ms : register(t0);\n"
    "Texture2D<float4> single : register(t1);\n"
    "RWStructuredBuffer<float4> o : register(u0);\n"
    "cbuffer sizes : register(b0) { uint w; uint h; uint n; uint multisampled; }\n"
    "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
    "  if (id.x >= w || id.y >= h) return;\n"
    "  for (uint s = 0; s < n; ++s) o[(id.y * w + id.x) * n + s] = multisampled ? ms.Load(id.xy, s) : single.Load(int3(id.xy, 0));\n"
    "}\n";

static bool SameBits(const std::vector<float>& a, const std::vector<float>& b, size_t* differing) {
  *differing = 0;
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
    *differing += (std::memcmp(&a[i], &b[i], 4) != 0) ? 1 : 0;
  }
  return a.size() == b.size() && *differing == 0;
}

static void TestFog(const std::string& game_folder, const std::string& built_folder, const char* hash, bool msaa) {
  const auto game = ReadFile(game_folder + "\\0x" + hash + ".ps_5_0.cso");
  const auto built = ReadFile(built_folder + "\\0x" + hash + ".cso");
  CHECK(!game.empty() && !built.empty(), "%s: the csos", hash);
  if (game.empty() || built.empty()) return;
  const UINT samples = msaa ? SAMPLES : 1;
  const auto reference = Run(game, msaa, WIDTH, HEIGHT, WIDTH, HEIGHT, 1.f, nullptr);
  if (reference.empty()) return;
  // The ramp reaches thin and thick fog.
  float thinnest = 0.f;
  float thickest = 1.f;
  for (size_t i = 3; i < reference.size(); i += 4) {
    thinnest = std::max(thinnest, reference[i]);
    thickest = std::min(thickest, reference[i]);
  }
  std::printf("%s: the game's transmittance %.4f .. %.4f over the ramp\n", hash, thickest, thinnest);
  CHECK(thickest < 0.5f && thinnest > 0.9f, "%s: the ramp misses thick or thin fog", hash);

  size_t differing = 0;
  CHECK(SameBits(Run(built, msaa, WIDTH, HEIGHT, WIDTH, HEIGHT, 1.f, nullptr), reference, &differing),
        "%s: Fog Strength 1, nothing at b12: %zu of %zu floats differ from the game's", hash, differing, reference.size());
  CHECK(!SameBits(Run(built, msaa, WIDTH, HEIGHT, WIDTH, HEIGHT, 0.5f, nullptr), reference, &differing), "%s: CONTROL 0.5 = the game's", hash);

  const auto none = Run(built, msaa, WIDTH, HEIGHT, WIDTH, HEIGHT, 0.f, nullptr);
  size_t fogged = 0;
  for (size_t i = 0; i < none.size(); i += 4) {
    fogged += (none[i] != 0.f || none[i + 1] != 0.f || none[i + 2] != 0.f || none[i + 3] != 1.f) ? 1 : 0;
  }
  CHECK(fogged == 0, "%s: Fog Strength 0: %zu samples with fog", hash, fogged);

  const auto doubled = Run(built, msaa, WIDTH, HEIGHT, WIDTH, HEIGHT, 2.f, nullptr);
  double worst = 0.0;
  for (size_t i = 0; i < doubled.size(); i += 4) {
    const double transmittance = reference[i + 3];
    const double scale = (1.0 - transmittance > 1e-6) ? 1.0 + transmittance : 2.0;
    worst = std::max(worst, std::abs(doubled[i + 3] - transmittance * transmittance));
    for (int c = 0; c < 3; ++c) {
      worst = std::max(worst, std::abs(doubled[i + c] - reference[i + c] * scale) / std::max(1.0, std::abs(reference[i + c] * scale)));
    }
  }
  CHECK(worst < 1e-4, "%s: Fog Strength 2: off T^2 / rgb x (1 + T) by %g", hash, worst);
  std::printf("%s: Fog Strength 1 the game's bits; 0 no fog; 2 within %.2g of T^2, rgb x (1 + T)\n", hash, worst);

  // Below 100%: the draw in the top-left quarter of a target twice its size, the screen size the target's.
  const float half[4] = {0.5f, 0.5f, 0.5f, 0.5f};
  const auto game_half = Run(game, msaa, WIDTH / 2, HEIGHT / 2, WIDTH / 2, HEIGHT / 2, 1.f, nullptr);
  const auto scaled = Run(built, msaa, WIDTH, HEIGHT, WIDTH / 2, HEIGHT / 2, 1.f, half);
  const auto unscaled = Run(built, msaa, WIDTH, HEIGHT, WIDTH / 2, HEIGHT / 2, 1.f, nullptr);
  size_t off = 0;
  size_t control = 0;
  size_t outside = 0;
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      for (UINT s = 0; s < samples; ++s) {
        const size_t at = ((static_cast<size_t>(y) * WIDTH + x) * samples + s) * 4;
        if (x >= WIDTH / 2 || y >= HEIGHT / 2) {
          outside += (scaled[at] != SENTINEL) ? 1 : 0;
          continue;
        }
        const size_t half_at = ((static_cast<size_t>(y) * (WIDTH / 2) + x) * samples + s) * 4;
        off += (std::memcmp(&scaled[at], &game_half[half_at], 16) != 0) ? 1 : 0;
        control += (std::memcmp(&unscaled[at], &game_half[half_at], 16) != 0) ? 1 : 0;
      }
    }
  }
  CHECK(off == 0 && outside == 0, "%s: 50%%: %zu samples off the game's at that size, %zu outside the viewport", hash, off, outside);
  CHECK(control > 0, "%s: CONTROL 50%% without b12 = the game's at that size", hash);
  std::printf("%s: 50%%: the game's bits at that size (CONTROL without b12: %zu samples differ)\n", hash, control);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: troy_shaders.exe <the game's csos> <the add-on's built csos>\n");
    return 2;
  }
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("no WARP device\n");
    return 1;
  }
  UINT quality = 0;
  device->CheckMultisampleQualityLevels(DXGI_FORMAT_R32G32B32A32_FLOAT, SAMPLES, &quality);
  CHECK(quality > 0, "WARP without %u samples of R32G32B32A32_FLOAT", SAMPLES);
  const auto vs = CompileSource(
      "float4 main(uint id : SV_VertexID) : SV_Position {\n"
      "  const float2 uv = float2((id << 1) & 2, id & 2);\n"
      "  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
      "}\n",
      "vs_5_0");
  device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &full_screen);
  const auto ramp = CompileSource(DEPTH_RAMP, "ps_5_0");
  device->CreatePixelShader(ramp->GetBufferPointer(), ramp->GetBufferSize(), nullptr, &depth_ramp);
  const auto out = CompileSource(SAMPLES_OUT, "cs_5_0");
  device->CreateComputeShader(out->GetBufferPointer(), out->GetBufferSize(), nullptr, &samples_out);

  TestFog(argv[1], argv[2], "018FED0C", false);
  TestFog(argv[1], argv[2], "D9A3B04F", true);
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
