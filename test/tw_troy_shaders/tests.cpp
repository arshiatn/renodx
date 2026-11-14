// Troy's replaced shaders (src/games/twtroy/shaders) against the game's on WARP.
// tests.exe <the game's dumped csos> <the add-on's built csos (build\twtroy.include\embed)>
// 1. Fog Strength: battlefog00 / 01 (the battle fog 018FED0C and its MSAA twin D9A3B04F, 4 samples) and their shader
//    model 4.1 twins battlefog02 / 03 (4ED4200E, 78E00DBB: feature level 10.1 devices) with the camera and
//    fog constants of the fog draw in ATN's session 3 trace_04 over a synthetic depth ramp (each sample its own depth):
//    at Fog Strength 1 with nothing at b12 the game's bits; 0 no fog (rgb 0, alpha 1); 2 alpha T^2 and rgb x (1 + T) (the
//    optical depth doubled); CONTROL 0.5 differs. Below 100% (b12 xy 0.5, the draw's viewport the target's top-left
//    quarter, the screen size the target's) the game's on a target of that size; CONTROL without b12 it differs.
// 2. The campaign vegetation's capture (rt_capture_campaign_vegetation) against the game's 04C7C807, both streamed out from
//    256 random vertices with that trace's camera and a blowing wind: the capture's world position x the view-projection is
//    the game's SV_Position (to the float rounding of its terms), its w the UV (POSITION0.w, NORMAL0.w) as two halves.
//    CONTROL: the capture a second later differs (the wind is in play).
// 2b. The fur-clad units' capture (rt_capture_unit_4_bones_fur) against the game's EE95696C: the same world position as
//    its TEXCOORD0 from random skinned vertices; CONTROL 26A41B7D's capture (no fur sway) differs.
// 3. VFX Brightness (src/games/twtroy/vfx_brightness.hpp's Patch) on the game's particle pixel shaders 563BA3E8 and 9C8987DD:
//    the patched program is the game's plus dcl_constantbuffer cb7 and one mad (disassembly), WARP takes it, and drawn over a
//    grid of random interpolants, emitters, textures and constants it gives the game's bits with cb7 unbound and with cb7.x 0
//    (the slider at 1); CONTROL cb7.x 1 (the slider at 2) differs.
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
#include "../../src/games/twtroy/vfx_brightness.hpp"

extern "C" __declspec(dllexport) const char* const NAME = "Troy shader test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }

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
  std::vector<char> game;
  for (const char* model : {".ps_5_0.cso", ".ps_4_1.cso"}) {
    if (game.empty()) game = ReadFile(game_folder + "\\0x" + hash + model);
  }
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

// A vertex shader's output `semantic` (index 0, 4 floats) for each of `vertices` points, streamed out through a geometry shader
// made from the vertex shader's own bytes: its inputs (its signature's, system values aside) each 4 floats of `vertices` in
// order, from one buffer; `constants` at b0.., `textures` at t0...
static std::vector<float> StreamOut(const std::vector<char>& code, const char* semantic, const std::vector<float>& vertices,
                                    std::span<const ComPtr<ID3D11Buffer>> constants,
                                    std::span<const ComPtr<ID3D11ShaderResourceView>> textures = {}) {
  ComPtr<ID3D11VertexShader> shader;
  CHECK(SUCCEEDED(device->CreateVertexShader(code.data(), code.size(), nullptr, &shader)), "%s: vertex shader", semantic);
  const D3D11_SO_DECLARATION_ENTRY entry = {.SemanticName = semantic, .ComponentCount = 4};
  const UINT stride = 16;
  ComPtr<ID3D11GeometryShader> stream;
  CHECK(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), &entry, 1, &stride, 1,
                                                               D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &stream)),
        "%s: stream output", semantic);
  ComPtr<ID3D11ShaderReflection> reflection;
  D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection));
  if (shader == nullptr || stream == nullptr || reflection == nullptr) return {};
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  for (UINT parameter = 0; parameter < shader_desc.InputParameters; ++parameter) {
    D3D11_SIGNATURE_PARAMETER_DESC input = {};
    reflection->GetInputParameterDesc(parameter, &input);
    if (input.SystemValueType != D3D_NAME_UNDEFINED) continue;
    const DXGI_FORMAT format = (input.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ? DXGI_FORMAT_R32G32B32A32_UINT
                               : (input.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ? DXGI_FORMAT_R32G32B32A32_SINT
                                                                                         : DXGI_FORMAT_R32G32B32A32_FLOAT;
    elements.push_back({input.SemanticName, input.SemanticIndex, format, 0, static_cast<UINT>(16 * elements.size()), D3D11_INPUT_PER_VERTEX_DATA, 0});
  }
  ComPtr<ID3D11InputLayout> layout;
  CHECK(SUCCEEDED(device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), code.data(), code.size(), &layout)),
        "%s: input layout", semantic);
  const UINT input_stride = static_cast<UINT>(16 * elements.size());
  const UINT count = static_cast<UINT>(vertices.size() * 4 / input_stride);
  const D3D11_BUFFER_DESC input_desc = {.ByteWidth = static_cast<UINT>(vertices.size() * 4), .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA input_data = {.pSysMem = vertices.data()};
  ComPtr<ID3D11Buffer> input;
  device->CreateBuffer(&input_desc, &input_data, &input);
  const D3D11_BUFFER_DESC out_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
  ComPtr<ID3D11Buffer> out;
  device->CreateBuffer(&out_desc, nullptr, &out);
  context->ClearState();
  context->IASetInputLayout(layout.Get());
  const UINT offset = 0;
  context->IASetVertexBuffers(0, 1, input.GetAddressOf(), &input_stride, &offset);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  for (UINT slot = 0; slot < constants.size(); ++slot) context->VSSetConstantBuffers(slot, 1, constants[slot].GetAddressOf());
  for (UINT slot = 0; slot < textures.size(); ++slot) context->VSSetShaderResources(slot, 1, textures[slot].GetAddressOf());
  context->VSSetShader(shader.Get(), nullptr, 0);
  context->GSSetShader(stream.Get(), nullptr, 0);
  context->SOSetTargets(1, out.GetAddressOf(), &offset);
  context->Draw(count, 0);
  ID3D11Buffer* no_target = nullptr;
  context->SOSetTargets(1, &no_target, &offset);
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), out.Get());
  std::vector<float> values(count * 4, 0.f);
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    std::memcpy(values.data(), mapped.pData, values.size() * 4);
    context->Unmap(staging.Get(), 0);
  }
  return values;
}

static float HalfToFloat(uint32_t bits) {
  const uint32_t exponent = (bits >> 10) & 0x1Fu;
  const uint32_t value = (exponent == 0) ? ((bits & 0x8000u) << 16) : (((bits & 0x8000u) << 16) | ((exponent + 112u) << 23) | ((bits & 0x3FFu) << 13));
  float result;
  std::memcpy(&result, &value, 4);
  return result;
}

static void TestVegetation(const std::string& game_folder, const std::string& built_folder) {
  const auto game = ReadFile(game_folder + "\\0x04C7C807.vs_5_0.cso");
  const auto capture = ReadFile(built_folder + "\\rt_capture_campaign_vegetation.cso");
  CHECK(!game.empty() && !capture.empty(), "vegetation: the csos");
  if (game.empty() || capture.empty()) return;
  // Random trees and plants around the trace's camera: each vertex 20 floats (POSITION0 the local offset with the UV's u in w,
  // NORMAL0 with v in w, COLOR0 with the wind weight in w, POSITION1 the place's x / z, POSITION2 its height, turn, scale, fade).
  uint32_t seed = 12345;
  const auto random = [&seed](float low, float high) {
    seed = seed * 1664525u + 1013904223u;
    return low + (high - low) * static_cast<float>(seed >> 8) / 16777216.f;
  };
  constexpr int VERTICES = 256;
  std::vector<float> vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    const float values[20] = {random(-3.f, 3.f), random(0.f, 8.f), random(-3.f, 3.f), random(0.f, 1.f),
                              random(-1.f, 1.f), random(-1.f, 1.f), random(-1.f, 1.f), random(0.f, 1.f),
                              random(0.f, 1.f), random(0.f, 1.f), random(0.f, 1.f), random(0.7f, 1.f),
                              CAMERA[0] + random(-150.f, 150.f), CAMERA[2] + random(-150.f, 150.f), 0.f, 0.f,
                              CAMERA[1] - random(20.f, 120.f), random(0.f, 6.28f), random(0.6f, 1.4f), random(0.f, 1.f)};
    vertices.insert(vertices.end(), std::begin(values), std::end(values));
  }
  // cb_environment: the wind's direction, vector, gust start, multiplier, horizontal and vertical strength.
  const std::array<float, 20> wind = {0.8f, 0.6f, 1.f, 0.5f, 0.f, 1.f, 1.3f, 0.9f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f};
  auto camera = CAMERA;
  const std::array<ComPtr<ID3D11Buffer>, 2> now = {ConstantBuffer(camera.data(), sizeof(camera)), ConstantBuffer(wind.data(), sizeof(wind))};
  const auto positions = StreamOut(game, "SV_POSITION", vertices, now);
  const auto worlds = StreamOut(capture, "TEXCOORD", vertices, now);
  camera[104] += 1.f;  // time_in_sec (c26.x)
  const std::array<ComPtr<ID3D11Buffer>, 2> later = {ConstantBuffer(camera.data(), sizeof(camera)), ConstantBuffer(wind.data(), sizeof(wind))};
  const auto worlds_later = StreamOut(capture, "TEXCOORD", vertices, later);
  if (positions.size() != VERTICES * 4 || worlds.size() != VERTICES * 4 || worlds_later.size() != VERTICES * 4) {
    CHECK(false, "vegetation: stream output sizes");
    return;
  }
  // SV_Position = dot(world, c9 .. c12) (view_projection, column-major), in double from the capture's world; the error over
  // the sum of the dot's terms' sizes (world positions some 600 m out: each term rounds in float, and the depth row's terms
  // cancel to a small w).
  const auto place_error = [](const std::vector<float>& world, const std::vector<float>& game_clip) {
    double worst = 0.0;
    for (int vertex = 0; vertex < VERTICES; ++vertex) {
      for (int row = 0; row < 4; ++row) {
        double clip = 0.0;
        double size = 0.0;
        for (int column = 0; column < 4; ++column) {
          const double term = ((column == 3) ? 1.0 : static_cast<double>(world[vertex * 4 + column])) * CAMERA[36 + row * 4 + column];
          clip += term;
          size += std::abs(term);
        }
        worst = std::max(worst, std::abs(clip - game_clip[vertex * 4 + row]) / std::max(size, 1e-6));
      }
    }
    return worst;
  };
  const double now_error = place_error(worlds, positions);
  const double later_error = place_error(worlds_later, positions);
  double uv_error = 0.0;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    uint32_t bits;
    std::memcpy(&bits, &worlds[vertex * 4 + 3], 4);
    uv_error = std::max(uv_error, static_cast<double>(std::abs(HalfToFloat(bits & 0xFFFFu) - vertices[vertex * 20 + 3])));
    uv_error = std::max(uv_error, static_cast<double>(std::abs(HalfToFloat(bits >> 16) - vertices[vertex * 20 + 7])));
  }
  CHECK(now_error < 1e-6, "vegetation: the capture's world x the camera %g off the game's SV_Position (over its terms' size)", now_error);
  CHECK(uv_error < 1e-3, "vegetation: the UV off by %g", uv_error);
  CHECK(later_error > 1e-4, "vegetation: CONTROL a second later = now (%g): the wind not in play", later_error);
  std::printf("04C7C807: the capture's world x the camera within %.2g of the game's SV_Position (over its terms' size), its UV within %.2g; CONTROL a second later %.2g\n",
              now_error, uv_error, later_error);
}

// The fur-clad units' capture (rt_capture_unit_4_bones_fur) against the game's EE95696C, both streamed out from the same random
// skinned vertices (bone indices into a random animation map, weights, the fur's length in POSITION3.w): its world position is
// the game's TEXCOORD0 (the skinned position plus the fur's sway, before the camera). CONTROL: the 4-bone capture of 26A41B7D
// (no sway) differs.
static void TestFur(const std::string& game_folder, const std::string& built_folder) {
  const auto game = ReadFile(game_folder + "\\0xEE95696C.vs_5_0.cso");
  const auto capture = ReadFile(built_folder + "\\rt_capture_unit_4_bones_fur.cso");
  const auto control = ReadFile(built_folder + "\\rt_capture_unit_4_bones.cso");
  CHECK(!game.empty() && !capture.empty() && !control.empty(), "fur: the csos");
  if (game.empty() || capture.empty() || control.empty()) return;
  uint32_t seed = 4242;
  const auto random = [&seed](float low, float high) {
    seed = seed * 1664525u + 1013904223u;
    return low + (high - low) * static_cast<float>(seed >> 8) / 16777216.f;
  };
  // The animation map (64 x 64 texels of random rows) and the selection ids.
  constexpr UINT MAP = 64;
  std::vector<float> rows(MAP * MAP * 4);
  for (float& value : rows) value = random(-1.f, 1.f);
  std::vector<uint32_t> ids(MAP * MAP);
  for (uint32_t& id : ids) id = static_cast<uint32_t>(random(0.f, 65536.f));
  const auto make_view = [](DXGI_FORMAT format, const void* data, UINT pitch) {
    const D3D11_TEXTURE2D_DESC desc = {.Width = MAP, .Height = MAP, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = pitch};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    device->CreateTexture2D(&desc, &initial, &texture);
    if (texture) device->CreateShaderResourceView(texture.Get(), nullptr, &view);
    return view;
  };
  const std::array<ComPtr<ID3D11ShaderResourceView>, 2> textures = {make_view(DXGI_FORMAT_R32G32B32A32_FLOAT, rows.data(), MAP * 16),
                                                                     make_view(DXGI_FORMAT_R32_UINT, ids.data(), MAP * 4)};
  // Each vertex 15 inputs x 4 floats: POSITION0 the bind pose, POSITION1 four bone indices (x 255), POSITION2 their weights,
  // POSITION3 a normal with the fur's length in w, the rest (tangents, the instance's rows, the base row in TEXCOORD3.x) random.
  constexpr int VERTICES = 256;
  std::vector<float> vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    for (int input = 0; input < 15; ++input) {
      for (int component = 0; component < 4; ++component) {
        float value = random(-1.f, 1.f);
        if (input == 1) value = std::floor(random(0.f, 6.f)) / 255.f;
        if (input == 2) value = random(0.f, 0.33f);
        if (input == 3 && component == 3) value = random(0.f, 1.f);
        if (input == 10 && component == 0) value = std::floor(random(0.f, 40.f));
        vertices.push_back(value);
      }
    }
  }
  const std::array<float, 20> rigid_config = {};
  const std::array<float, 4> animation_size = {MAP, 1.f / MAP, MAP, 1.f / MAP};
  const std::array<ComPtr<ID3D11Buffer>, 3> constants = {ConstantBuffer(CAMERA.data(), sizeof(CAMERA)), ConstantBuffer(rigid_config.data(), sizeof(rigid_config)),
                                                         ConstantBuffer(animation_size.data(), sizeof(animation_size))};
  const auto reference = StreamOut(game, "TEXCOORD", vertices, constants, textures);
  const auto worlds = StreamOut(capture, "TEXCOORD", vertices, constants, textures);
  const auto unswayed = StreamOut(control, "TEXCOORD", vertices, constants, textures);
  if (reference.size() != VERTICES * 4 || worlds.size() != VERTICES * 4 || unswayed.size() != VERTICES * 4) {
    CHECK(false, "fur: stream output sizes");
    return;
  }
  const auto worst = [&reference](const std::vector<float>& world) {
    double error = 0.0;
    for (size_t index = 0; index < reference.size(); ++index) {
      error = std::max(error, std::abs(static_cast<double>(world[index]) - reference[index]) / std::max(1.0, std::abs(static_cast<double>(reference[index]))));
    }
    return error;
  };
  int moved = 0;  // vertices the skinning placed off the origin (the animation map's rows in play)
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    if (std::abs(reference[vertex * 4]) + std::abs(reference[vertex * 4 + 1]) + std::abs(reference[vertex * 4 + 2]) > 1e-3) ++moved;
  }
  const double error = worst(worlds);
  const double control_error = worst(unswayed);
  CHECK(moved > VERTICES / 2, "fur: only %d of %d vertices skinned off the origin", moved, VERTICES);
  CHECK(error < 1e-6, "fur: the capture %g off the game's world position", error);
  CHECK(control_error > 1e-3, "fur: CONTROL the 4-bone capture without the sway = the game's (%g)", control_error);
  std::printf("EE95696C: the capture's world within %.2g of the game's (%d of %d vertices skinned off the origin); CONTROL 26A41B7D's "
              "capture %.2g\n",
              error, moved, VERTICES, control_error);
}

// A shader's instructions and declarations (D3DDisassemble), comments and its version line left out.
static std::array<std::vector<std::string>, 2> Disassembly(std::span<const uint8_t> code) {
  ComPtr<ID3DBlob> text;
  std::array<std::vector<std::string>, 2> lines;  // declarations, instructions
  if (FAILED(D3DDisassemble(code.data(), code.size(), 0, nullptr, &text))) return lines;
  std::string line;
  for (const char* at = static_cast<const char*>(text->GetBufferPointer()); *at != '\0'; ++at) {
    if (*at != '\n') {
      line += *at;
      continue;
    }
    const size_t start = line.find_first_not_of(" \r");
    const std::string trimmed = (start == std::string::npos) ? "" : line.substr(start, line.find_last_not_of(" \r") - start + 1);
    if (!trimmed.empty() && !trimmed.starts_with("//") && !trimmed.starts_with("ps_")) {
      lines[trimmed.starts_with("dcl_") ? 0 : 1].push_back(trimmed);
    }
    line.clear();
  }
  return lines;
}

static void TestVfxBrightness(const std::string& game_folder) {
  for (const uint32_t hash : twtroy::vfx_brightness::HASHES) {
    char name[32];
    std::snprintf(name, sizeof(name), "%08X", hash);
    std::vector<char> file;
    for (const char* model : {".ps_5_0.cso", ".ps_4_1.cso"}) {
      if (file.empty()) file = ReadFile(game_folder + "\\0x" + name + model);
    }
    CHECK(!file.empty(), "%s: the cso", name);
    if (file.empty()) continue;
    const std::span<const uint8_t> game(reinterpret_cast<const uint8_t*>(file.data()), file.size());
    const auto patched = twtroy::vfx_brightness::Patch(game);
    CHECK(!patched.empty(), "%s: not patched", name);
    if (patched.empty()) continue;

    // The program: the game's, one declaration and one instruction more: mad rC.xyz on the colour the movc before it wrote,
    // right before the fog test.
    const auto before = Disassembly(game);
    const auto after = Disassembly(patched);
    const std::string slot = std::to_string(twtroy::vfx_brightness::SLOT);
    auto declarations = after[0];
    std::erase(declarations, "dcl_constantbuffer CB" + slot + "[1], immediateIndexed");
    auto instructions = after[1];
    const auto inserted = std::ranges::find_if(
        instructions, [&](const std::string& line) { return line.starts_with("mad ") && line.find("cb" + slot + "[0]") != std::string::npos; });
    std::string mad;
    bool placed = false;
    if (inserted != instructions.end()) {
      mad = *inserted;
      const std::string colour = mad.substr(4, mad.find('.') - 4);
      auto movc = inserted;
      while (movc != instructions.begin() && (*(movc - 1) == "endif" || *(movc - 1) == "else")) --movc;
      placed = mad == "mad " + colour + ".xyz, " + colour + ".xyzx, cb" + slot + "[0].xxxx, " + colour + ".xyzx" && movc != instructions.begin()
               && (movc - 1)->starts_with("movc " + colour + ".xyz,") && (inserted + 1) != instructions.end()
               && (inserted + 1)->starts_with("and r0.x, r0.w, cb");
      instructions.erase(inserted);
    }
    CHECK(declarations == before[0] && after[0].size() == before[0].size() + 1, "%s: declarations", name);
    CHECK(placed && instructions == before[1], "%s: instructions: the game's and '%s' after the colour's movc, before the fog test",
          name, mad.c_str());

    // On WARP: a grid of quads over random interpolants (each quad its own emitter), every resource the shader binds random.
    ComPtr<ID3D11ShaderReflection> reflection;
    CHECK(SUCCEEDED(D3DReflect(game.data(), game.size(), IID_PPV_ARGS(&reflection))), "%s: reflection", name);
    if (reflection == nullptr) continue;
    D3D11_SHADER_DESC desc = {};
    reflection->GetDesc(&desc);
    std::string source = "void main(uint id : SV_VertexID, out float4 position : SV_Position";
    std::string body;
    for (UINT index = 0; index < desc.InputParameters; ++index) {
      D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
      reflection->GetInputParameterDesc(index, &parameter);
      if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
      const std::string out = "o" + std::to_string(index);
      const bool integer = parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32;
      source += std::string(integer ? ", nointerpolation out uint4 " : ", out float4 ") + out + " : " + parameter.SemanticName
                + std::to_string(parameter.SemanticIndex);
      body += integer ? "  " + out + " = uint4(quad % 4u, quad, 3u, 7u);\n"
                      : "  " + out + " = frac(sin(float4(id, id + 1, id + 2, id + 3) * 12.9898 + " + std::to_string(index)
                            + " * 78.233) * 43758.5453) * 3.0 - 1.0;\n";
    }
    source += ") {\n  const uint quad = id / 6u;\n  const uint corner = id % 6u;\n"
              "  const float2 offset = float2((corner == 1u || corner == 4u || corner == 5u) ? 1.0 : 0.0, (corner == 2u || corner == 3u || corner == 5u) ? 1.0 : 0.0);\n"
              "  const float2 cell = (float2(quad % 8u, quad / 8u) + offset) / 8.0;\n"
              "  position = float4(cell * float2(2, -2) + float2(-1, 1), 0.5, 1);\n" + body + "}\n";
    const auto vs = CompileSource(source.c_str(), "vs_5_0");
    ComPtr<ID3D11VertexShader> vertex_shader;
    device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_shader);
    ComPtr<ID3D11PixelShader> game_shader;
    ComPtr<ID3D11PixelShader> patched_shader;
    CHECK(SUCCEEDED(device->CreatePixelShader(game.data(), game.size(), nullptr, &game_shader)), "%s: the game's on WARP", name);
    CHECK(SUCCEEDED(device->CreatePixelShader(patched.data(), patched.size(), nullptr, &patched_shader)), "%s: patched on WARP", name);
    if (game_shader == nullptr || patched_shader == nullptr) continue;

    uint32_t seed = hash;
    const auto random = [&seed]() {
      seed = seed * 1664525u + 1013904223u;
      return static_cast<float>(seed >> 8) / 16777216.f;
    };
    std::vector<ComPtr<ID3D11ShaderResourceView>> views(16);
    std::vector<ComPtr<ID3D11SamplerState>> samplers(16);
    std::vector<ComPtr<ID3D11Buffer>> buffers(14);
    for (UINT index = 0; index < desc.BoundResources; ++index) {
      D3D11_SHADER_INPUT_BIND_DESC bind = {};
      reflection->GetResourceBindingDesc(index, &bind);
      if (bind.Type == D3D_SIT_CBUFFER || bind.Type == D3D_SIT_STRUCTURED) {
        D3D11_SHADER_BUFFER_DESC buffer_desc = {};
        reflection->GetConstantBufferByName(bind.Name)->GetDesc(&buffer_desc);
        // Constants in 0..4 (floats from 2 up set bit 30, the flags' fog bit, so the paths vary); 8 emitters.
        const UINT count = (bind.Type == D3D_SIT_STRUCTURED) ? 8 : 1;
        std::vector<float> values(buffer_desc.Size * count / 4);
        for (float& value : values) value = random() * 4.f;
        if (bind.Type == D3D_SIT_CBUFFER) {
          buffers[bind.BindPoint] = ConstantBuffer(values.data(), static_cast<UINT>(values.size() * 4));
          continue;
        }
        const D3D11_BUFFER_DESC structured = {.ByteWidth = static_cast<UINT>(values.size() * 4), .Usage = D3D11_USAGE_DEFAULT,
                                              .BindFlags = D3D11_BIND_SHADER_RESOURCE, .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                              .StructureByteStride = buffer_desc.Size};
        const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = values.data()};
        ComPtr<ID3D11Buffer> buffer;
        device->CreateBuffer(&structured, &initial, &buffer);
        device->CreateShaderResourceView(buffer.Get(), nullptr, &views[bind.BindPoint]);
      } else if (bind.Type == D3D_SIT_SAMPLER) {
        const bool comparison = (bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0;
        const D3D11_SAMPLER_DESC sampler = {.Filter = comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                                            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP, .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
                                            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP, .ComparisonFunc = D3D11_COMPARISON_LESS, .MaxLOD = D3D11_FLOAT32_MAX};
        device->CreateSamplerState(&sampler, &samplers[bind.BindPoint]);
      } else if (bind.Type == D3D_SIT_TEXTURE) {
        // 2D textures single-channel R32 (comparison samplers take it), the others RGBA; 16 texels a side, 4 layers or slices.
        const bool flat = bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2D;
        const DXGI_FORMAT format = flat ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
        const UINT channels = flat ? 1 : 4;
        const bool cube = bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE;
        const UINT layers = cube ? 6 : ((bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY) ? 4 : 1);
        std::vector<float> texels(16 * 16 * channels * layers * ((bind.Dimension == D3D_SRV_DIMENSION_TEXTURE3D) ? 16 : 1));
        for (float& texel : texels) texel = random();
        if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE3D) {
          const D3D11_TEXTURE3D_DESC volume = {.Width = 16, .Height = 16, .Depth = 16, .MipLevels = 1, .Format = format,
                                               .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
          const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = texels.data(), .SysMemPitch = 16 * channels * 4, .SysMemSlicePitch = 256 * channels * 4};
          ComPtr<ID3D11Texture3D> texture;
          device->CreateTexture3D(&volume, &initial, &texture);
          device->CreateShaderResourceView(texture.Get(), nullptr, &views[bind.BindPoint]);
          continue;
        }
        std::vector<D3D11_SUBRESOURCE_DATA> initial(layers);
        for (UINT layer = 0; layer < layers; ++layer) {
          initial[layer] = {.pSysMem = texels.data() + (layer * 256 * channels), .SysMemPitch = 16 * channels * 4};
        }
        const D3D11_TEXTURE2D_DESC surface = {.Width = 16, .Height = 16, .MipLevels = 1, .ArraySize = layers, .Format = format,
                                              .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                              .MiscFlags = cube ? static_cast<UINT>(D3D11_RESOURCE_MISC_TEXTURECUBE) : 0u};
        ComPtr<ID3D11Texture2D> texture;
        device->CreateTexture2D(&surface, initial.data(), &texture);
        device->CreateShaderResourceView(texture.Get(), nullptr, &views[bind.BindPoint]);
      }
    }

    constexpr UINT SIZE = 64;
    const auto target = Texture(SIZE, SIZE, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11RenderTargetView> target_view;
    device->CreateRenderTargetView(target.Get(), nullptr, &target_view);
    const D3D11_TEXTURE2D_DESC staging_desc = {.Width = SIZE, .Height = SIZE, .MipLevels = 1, .ArraySize = 1,
                                               .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                               .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&staging_desc, nullptr, &staging);
    const auto draw = [&](ID3D11PixelShader* pixel_shader, const float* brightness_constant) {
      context->ClearState();
      const float clear[4] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
      context->ClearRenderTargetView(target_view.Get(), clear);
      context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
      const D3D11_VIEWPORT viewport = {.Width = SIZE, .Height = SIZE, .MaxDepth = 1.f};
      context->RSSetViewports(1, &viewport);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vertex_shader.Get(), nullptr, 0);
      context->PSSetShader(pixel_shader, nullptr, 0);
      std::vector<ID3D11ShaderResourceView*> bound_views;
      for (const auto& view : views) bound_views.push_back(view.Get());
      context->PSSetShaderResources(0, static_cast<UINT>(bound_views.size()), bound_views.data());
      std::vector<ID3D11SamplerState*> bound_samplers;
      for (const auto& sampler : samplers) bound_samplers.push_back(sampler.Get());
      context->PSSetSamplers(0, static_cast<UINT>(bound_samplers.size()), bound_samplers.data());
      auto bound_buffers = buffers;
      if (brightness_constant != nullptr) bound_buffers[twtroy::vfx_brightness::SLOT] = ConstantBuffer(brightness_constant, 16);
      std::vector<ID3D11Buffer*> raw;
      for (const auto& buffer : bound_buffers) raw.push_back(buffer.Get());
      context->PSSetConstantBuffers(0, static_cast<UINT>(raw.size()), raw.data());
      context->Draw(64 * 6, 0);
      context->CopyResource(staging.Get(), target.Get());
      std::vector<float> values(SIZE * SIZE * 4);
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        for (UINT row = 0; row < SIZE; ++row) {
          std::memcpy(&values[row * SIZE * 4], static_cast<const uint8_t*>(mapped.pData) + (row * mapped.RowPitch), SIZE * 16);
        }
        context->Unmap(staging.Get(), 0);
      }
      return values;
    };
    const float at_one[4] = {0.f, 0.f, 0.f, 0.f};
    const float at_two[4] = {1.f, 0.f, 0.f, 0.f};
    const auto reference = draw(game_shader.Get(), nullptr);
    size_t drawn = 0;
    for (size_t i = 0; i < reference.size(); i += 4) drawn += (reference[i + 3] != SENTINEL) ? 1 : 0;
    size_t differing = 0;
    const bool unbound = SameBits(draw(patched_shader.Get(), nullptr), reference, &differing);
    CHECK(unbound, "%s: cb7 unbound: %zu of %zu floats off the game's", name, differing, reference.size());
    const bool one = SameBits(draw(patched_shader.Get(), at_one), reference, &differing);
    CHECK(one, "%s: at 1: %zu of %zu floats off the game's", name, differing, reference.size());
    SameBits(draw(patched_shader.Get(), at_two), reference, &differing);
    CHECK(drawn > 0 && differing > 0, "%s: CONTROL at 2 the game's (%zu of %zu pixels drawn)", name, drawn, reference.size() / 4);
    std::printf("%s: patched = the game's + cb7 and '%s'; the game's bits at 1 and unbound over %zu drawn pixels; CONTROL at 2: %zu "
                "floats differ\n", name, mad.c_str(), drawn, differing);
  }
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
  TestFog(argv[1], argv[2], "4ED4200E", false);
  TestFog(argv[1], argv[2], "78E00DBB", true);
  TestVegetation(argv[1], argv[2]);
  TestFur(argv[1], argv[2]);
  TestVfxBrightness(argv[1]);
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
