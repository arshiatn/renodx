// The depth of field blurs of Attila / Thrones / WARHAMMER (twattila/dof_blur_ps.hlsli, dof_blur_cs.hlsli) on WARP
// against the game's own bytes.
// tests.exe {<label> <ps_h|ps_v|cs_h|cs_v> <game .cso> <ours .cso> <mutant .cso>}...: per case, on a 300 x 260
// RGBA16F scene of random colours (-0.5 to 3) and circles of confusion (alpha: 0, 1 or random), with a second mip of
// other noise, the game's draw (or dispatch) as the game makes it (camera c27 = size, c28 = texel offset):
// 1. Rendering Mode SDR (the injection all 0): ours = the game's, bit for bit.
//    CONTROL: the mutant (run.ps1: our SDR tap not clipped at 1) differs.
// 2. HDR (the injection all 4): ours keeps a flat (3, 2, 0.5) scene at full CoC (within 1e-3), the game's clips it to
//    (1, 1, 0.5) (within 3e-3: 8-bit taps). CONTROL: on the random scene ours differs from the game's.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

constexpr UINT WIDTH = 300;
constexpr UINT HEIGHT = 260;
constexpr float SENTINEL = -7.f;

static std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static uint16_t ToHalf(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  const uint32_t sign = (bits >> 16) & 0x8000u;
  const int exponent = static_cast<int>((bits >> 23) & 0xff) - 127 + 15;
  const uint32_t mantissa = bits & 0x7fffffu;
  if (exponent <= 0) return static_cast<uint16_t>(sign);
  if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
  return static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
}

// The scene and its second mip (other noise: Attila's pixel blurs read the mip of each tap's v): a deterministic noise
// of colours and circles of confusion, or one flat colour.
constexpr UINT MIP0_TEXELS = WIDTH * HEIGHT;
constexpr UINT TEXELS = MIP0_TEXELS + (WIDTH / 2) * (HEIGHT / 2);

static std::vector<uint16_t> Scene(bool flat) {
  std::vector<uint16_t> texels(TEXELS * 4);
  uint32_t state = 0x2545F491u;
  const auto next = [&]() {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.f;
  };
  for (UINT i = 0; i < TEXELS; ++i) {
    float rgba[4] = {3.f, 2.f, 0.5f, 1.f};
    if (!flat) {
      for (int c = 0; c < 3; ++c) rgba[c] = next() * 3.5f - 0.5f;
      const float pick = next();
      rgba[3] = (pick < 0.2f) ? 0.f : ((pick < 0.4f) ? 1.f : next());
    }
    for (int c = 0; c < 4; ++c) texels[i * 4 + c] = ToHalf(rgba[c]);
  }
  return texels;
}

static ComPtr<ID3D11Buffer> ConstantBuffer(const std::vector<float>& values) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = static_cast<UINT>(values.size() * sizeof(float));
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  D3D11_SUBRESOURCE_DATA data = {values.data()};
  ComPtr<ID3D11Buffer> buffer;
  CHECK(SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer)), "constant buffer");
  return buffer;
}

// One draw (ps_*) or dispatch (cs_*) of `code` on `scene`, the injection's every float at `injection`; the RGBA32F result.
static std::vector<float> Run(const std::string& kind, const std::vector<uint8_t>& code, const std::vector<uint16_t>& scene,
                              float injection) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = WIDTH;
  desc.Height = HEIGHT;
  desc.MipLevels = 2;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA data[2] = {{scene.data(), WIDTH * 8}, {scene.data() + MIP0_TEXELS * 4, WIDTH / 2 * 8}};
  ComPtr<ID3D11Texture2D> input;
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc, data, &input)), "input");
  ComPtr<ID3D11ShaderResourceView> input_view;
  device->CreateShaderResourceView(input.Get(), nullptr, &input_view);

  const bool compute = (kind[0] == 'c');
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.BindFlags = compute ? D3D11_BIND_UNORDERED_ACCESS : D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> output;
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &output)), "output");

  std::vector<float> camera(64 * 4, 0.f);
  camera[27 * 4 + 0] = static_cast<float>(WIDTH);
  camera[27 * 4 + 1] = static_cast<float>(HEIGHT);
  camera[27 * 4 + 2] = 1.f / WIDTH;
  camera[27 * 4 + 3] = 1.f / HEIGHT;
  camera[28 * 4 + 0] = 0.f;  // g_vpos_texel_offset (D3D11: pixel centres)
  const ComPtr<ID3D11Buffer> camera_buffer = ConstantBuffer(camera);
  const ComPtr<ID3D11Buffer> injection_buffer = ConstantBuffer(std::vector<float>(1024, injection));
  ID3D11Buffer* buffers[14] = {camera_buffer.Get()};
  buffers[13] = injection_buffer.Get();
  const float clear[4] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};

  if (compute) {
    ComPtr<ID3D11ComputeShader> shader;
    CHECK(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)), "compute shader");
    ComPtr<ID3D11UnorderedAccessView> output_view;
    device->CreateUnorderedAccessView(output.Get(), nullptr, &output_view);
    context->ClearUnorderedAccessViewFloat(output_view.Get(), clear);
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, input_view.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
    context->CSSetConstantBuffers(0, 14, buffers);
    if (kind == "cs_h") {
      context->Dispatch((WIDTH + 127) / 128, HEIGHT / 2, 1);
    } else {
      context->Dispatch(WIDTH / 2, (HEIGHT + 127) / 128, 1);
    }
    ID3D11UnorderedAccessView* no_view = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &no_view, nullptr);
  } else {
    static ComPtr<ID3D11VertexShader> vertex_shader;
    if (vertex_shader == nullptr) {
      const char* source =
          "float4 main(uint id : SV_VertexID) : SV_Position {"
          "  float2 uv = float2((id << 1) & 2, id & 2);"
          "  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }";
      ComPtr<ID3DBlob> blob;
      ComPtr<ID3DBlob> errors;
      CHECK(SUCCEEDED(D3DCompile(source, std::strlen(source), "vs", nullptr, nullptr, "main", "vs_4_0", 0, 0, &blob, &errors)),
            "vertex shader");
      device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vertex_shader);
    }
    ComPtr<ID3D11PixelShader> shader;
    CHECK(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader)), "pixel shader");
    ComPtr<ID3D11RenderTargetView> output_view;
    device->CreateRenderTargetView(output.Get(), nullptr, &output_view);
    context->ClearRenderTargetView(output_view.Get(), clear);
    D3D11_SAMPLER_DESC sampler_desc = {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    device->CreateSamplerState(&sampler_desc, &sampler);
    const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 0.f, 1.f};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(shader.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, input_view.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 14, buffers);
    context->OMSetRenderTargets(1, output_view.GetAddressOf(), nullptr);
    context->Draw(3, 0);
    context->OMSetRenderTargets(0, nullptr, nullptr);
  }

  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, &staging);
  context->CopyResource(staging.Get(), output.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  std::vector<float> result(WIDTH * HEIGHT * 4);
  if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    for (UINT y = 0; y < HEIGHT; ++y) {
      std::memcpy(&result[y * WIDTH * 4], static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, WIDTH * 16);
    }
    context->Unmap(staging.Get(), 0);
  }
  return result;
}

struct Difference {
  size_t floats = 0;  // not the same bits
  double max = 0.0;
};

static Difference Compare(const std::vector<float>& a, const std::vector<float>& b) {
  Difference d;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::memcmp(&a[i], &b[i], 4) != 0) {
      ++d.floats;
      d.max = std::max(d.max, std::abs(static_cast<double>(a[i]) - b[i]));
    }
  }
  return d;
}

int main(int argc, char** argv) {
  if (argc < 6 || (argc - 1) % 5 != 0) {
    std::printf("usage: tests.exe {<label> <ps_h|ps_v|cs_h|cs_v> <game .cso> <ours .cso> <mutant .cso>}...\n");
    return 2;
  }
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr,
                               &context))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }
  const std::vector<uint16_t> noise = Scene(false);
  const std::vector<uint16_t> flat = Scene(true);
  for (int i = 1; i < argc; i += 5) {
    const char* label = argv[i];
    const std::string kind = argv[i + 1];
    const std::vector<uint8_t> game = ReadFile(argv[i + 2]);
    const std::vector<uint8_t> ours = ReadFile(argv[i + 3]);
    const std::vector<uint8_t> mutant = ReadFile(argv[i + 4]);
    CHECK(!game.empty() && !ours.empty() && !mutant.empty(), "%s: a .cso missing", label);
    if (game.empty() || ours.empty() || mutant.empty()) continue;
    const int before = failures;

    const std::vector<float> game_sdr = Run(kind, game, noise, 0.f);
    const Difference sdr = Compare(game_sdr, Run(kind, ours, noise, 0.f));
    CHECK(sdr.floats == 0, "%s SDR: %zu floats differ from the game's (max %g)", label, sdr.floats, sdr.max);
    const Difference control = Compare(game_sdr, Run(kind, mutant, noise, 0.f));
    CHECK(control.floats > 0 && control.max > 1e-3, "%s CONTROL mutant: %zu floats differ (max %g)", label, control.floats,
          control.max);

    const std::vector<float> ours_flat = Run(kind, ours, flat, 4.f);
    const std::vector<float> game_flat = Run(kind, game, flat, 4.f);
    double ours_error = 0.0;
    double game_error = 0.0;
    const float expected_ours[4] = {3.f, 2.f, 0.5f, 1.f};
    const float expected_game[4] = {1.f, 1.f, 0.5f, 1.f};
    for (size_t p = 0; p < ours_flat.size(); ++p) {
      ours_error = std::max(ours_error, std::abs(static_cast<double>(ours_flat[p]) - expected_ours[p % 4]));
      game_error = std::max(game_error, std::abs(static_cast<double>(game_flat[p]) - expected_game[p % 4]));
    }
    CHECK(ours_error < 1e-3, "%s HDR flat: ours off (3, 2, 0.5, 1) by %g", label, ours_error);
    // (The compute blurs' 8-bit taps: 0.5 = 127 / 255.)
    CHECK(game_error < 3e-3, "%s HDR flat: the game's off (1, 1, 0.5, 1) by %g", label, game_error);
    const Difference hdr = Compare(game_sdr, Run(kind, ours, noise, 4.f));
    CHECK(hdr.floats > 0 && hdr.max > 1e-2, "%s CONTROL HDR: %zu floats differ (max %g)", label, hdr.floats, hdr.max);

    std::printf("%s %s: SDR %zu floats differ (max %g); mutant %zu (max %g); HDR flat ours %.2g, game %.2g; HDR noise %zu "
                "(max %.3g) -> %s\n",
                failures == before ? "PASS" : "FAIL", label, sdr.floats, sdr.max, control.floats, control.max, ours_error,
                game_error, hdr.floats, hdr.max, failures == before ? "ok" : "failed");
  }
  std::printf("%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
