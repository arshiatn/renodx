// Ryse's videos (Scaleform YUV video00 0x37ABF6CF, YUVA video01 0xC1F889A4) on WARP: Video Highlights (AutoHDR) and
// Video Dithering saved on, Rendering Mode HDR, on an SDR output (swap chain encoding 2): the add-on's built shaders must
// draw the game's bytes (no ITM, no dither on SDR). Control: the same on an HDR10 output with Peak = Paper White = UI (the
// ITM's stretch 1, the encode's scale 1) differs from the game's by the dither, so the test sees it.
// Arguments: the repository (its build's embedded csos) and the folder of the game's dumped csos; both only read.
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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "src/games/rysesonofrome/shared.h"

using Microsoft::WRL::ComPtr;

namespace {

int failures = 0;

#define CHECK(condition)                                       \
  do {                                                         \
    if (!(condition)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition); \
      ++failures;                                              \
    }                                                          \
  } while (false)

constexpr UINT EDGE = 64;

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Scaleform's vertex outputs as the videos read them: the full-screen uv, a white vertex colour.
constexpr char VERTEX[] = R"(
void main(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0, out float4 colour : COLOR0) {
  const float2 corner = float2((id << 1) & 2, id & 2);
  position = float4(corner * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
  uv = corner;
  colour = 1.f;
}
)";

ComPtr<ID3D11Buffer> Constants(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = data};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &init, buffer.GetAddressOf());
  return buffer;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: ryse_video.exe <repository> <the folder of the game's dumped csos>\n");
    return 2;
  }
  const std::filesystem::path embed = std::filesystem::path(argv[1]) / "build/rysesonofrome.include/embed";
  const std::filesystem::path dump = argv[2];
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("no WARP device\n");
    return 2;
  }
  ComPtr<ID3DBlob> vertex_blob;
  ComPtr<ID3D11VertexShader> vertex;
  if (FAILED(D3DCompile(VERTEX, sizeof(VERTEX) - 1, "video_vertex", nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_blob.GetAddressOf(), nullptr))
      || FAILED(device->CreateVertexShader(vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr, vertex.GetAddressOf()))) {
    std::printf("the vertex shader could not be made\n");
    return 2;
  }
  // Y, U, V, A planes (their alpha read): a ramp of luma up to white, chroma around neutral.
  std::array<ComPtr<ID3D11ShaderResourceView>, 4> planes;
  for (UINT plane = 0; plane < planes.size(); ++plane) {
    std::vector<uint32_t> texels(EDGE * EDGE);
    for (UINT index = 0; index < texels.size(); ++index) {
      const UINT x = index % EDGE;
      const UINT y = index / EDGE;
      const uint32_t value = (plane == 0) ? (16u + (219u * x / (EDGE - 1))) : (plane == 3) ? 255u : (112u + ((plane * 13u + y) % 32u));
      texels[index] = value << 24;
    }
    const D3D11_TEXTURE2D_DESC desc = {.Width = EDGE,
                                       .Height = EDGE,
                                       .MipLevels = 1,
                                       .ArraySize = 1,
                                       .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                       .SampleDesc = {.Count = 1},
                                       .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = texels.data(), .SysMemPitch = EDGE * 4};
    ComPtr<ID3D11Texture2D> texture;
    device->CreateTexture2D(&desc, &data, texture.GetAddressOf());
    device->CreateShaderResourceView(texture.Get(), nullptr, planes[plane].GetAddressOf());
  }
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
                                           .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxAnisotropy = 1,
                                           .ComparisonFunc = D3D11_COMPARISON_NEVER,
                                           .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  // PER_BATCH: the colour transform's multiply 1, add 0; not premultiplied.
  const std::array<float, 12> batch = {1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
  const auto batch_buffer = Constants(batch.data(), sizeof(batch));

  // `code` drawn with the settings' output encoding `encoding` and Peak `peak` (Paper White and UI 203 nits).
  const auto draw = [&](const std::vector<char>& code, float encoding, float peak) {
    std::vector<float> result;
    ComPtr<ID3D11PixelShader> shader;
    if (code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf()))) return result;
    ShaderInjectData injection = {};
    injection.peak_white_nits = peak;
    injection.diffuse_white_nits = 203.f;
    injection.graphics_white_nits = 203.f;
    injection.swap_chain_encoding = encoding;
    injection.hdr = 1.f;
    injection.video_highlights = 1.f;
    injection.video_dithering = 1.f;
    std::array<float, 96> injection_floats = {};
    static_assert(sizeof(ShaderInjectData) <= sizeof(injection_floats));
    std::memcpy(injection_floats.data(), &injection, sizeof(injection));
    const auto injection_buffer = Constants(injection_floats.data(), sizeof(injection_floats));
    const D3D11_TEXTURE2D_DESC target_desc = {.Width = EDGE,
                                              .Height = EDGE,
                                              .MipLevels = 1,
                                              .ArraySize = 1,
                                              .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                              .SampleDesc = {.Count = 1},
                                              .BindFlags = D3D11_BIND_RENDER_TARGET};
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf());
    device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());
    context->ClearState();
    ID3D11RenderTargetView* output = target_view.Get();
    context->OMSetRenderTargets(1, &output, nullptr);
    const D3D11_VIEWPORT viewport = {.Width = EDGE, .Height = EDGE, .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetShader(shader.Get(), nullptr, 0);
    const std::array<ID3D11Buffer*, 1> batch_constants = {batch_buffer.Get()};
    ID3D11Buffer* injection_constants = injection_buffer.Get();
    context->PSSetConstantBuffers(0, 1, batch_constants.data());
    context->PSSetConstantBuffers(13, 1, &injection_constants);
    const std::array<ID3D11ShaderResourceView*, 4> inputs = {planes[0].Get(), planes[1].Get(), planes[2].Get(), planes[3].Get()};
    context->PSSetShaderResources(0, 4, inputs.data());
    const std::array<ID3D11SamplerState*, 4> samplers = {sampler.Get(), sampler.Get(), sampler.Get(), sampler.Get()};
    context->PSSetSamplers(0, 4, samplers.data());
    context->Draw(3, 0);
    D3D11_TEXTURE2D_DESC read_desc = target_desc;
    read_desc.Usage = D3D11_USAGE_STAGING;
    read_desc.BindFlags = 0;
    read_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&read_desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      result.resize(static_cast<size_t>(EDGE) * EDGE * 4);
      for (UINT row = 0; row < EDGE; ++row) {
        std::memcpy(&result[static_cast<size_t>(row) * EDGE * 4], static_cast<const uint8_t*>(mapped.pData) + (row * mapped.RowPitch), EDGE * 16);
      }
      context->Unmap(staging.Get(), 0);
    }
    return result;
  };
  const auto difference = [](const std::vector<float>& a, const std::vector<float>& b) {
    float largest = 0.f;
    for (size_t index = 0; index < a.size(); ++index) largest = std::max(largest, std::abs(a[index] - b[index]));
    return largest;
  };

  for (const auto* hash : {"0x37ABF6CF", "0xC1F889A4"}) {
    const auto ours_code = ReadFile(embed / (std::string(hash) + ".cso"));
    const auto game_code = ReadFile(dump / (std::string(hash) + ".ps_5_0.cso"));
    const auto game = draw(game_code, 2.f, 1000.f);
    const auto sdr = draw(ours_code, 2.f, 1000.f);
    const auto hdr10 = draw(ours_code, 0.f, 203.f);
    if (game.empty() || sdr.empty() || hdr10.empty()) {
      std::printf("%s: a shader did not load\n", hash);
      return 2;
    }
    const float sdr_difference = difference(sdr, game);
    const float hdr10_difference = difference(hdr10, game);
    std::printf("%s, Video Highlights and Dithering saved on: SDR output against the game's bytes, largest difference %.3g; "
                "control, HDR10 at Peak = Paper White (the dither alone), %.3g\n",
                hash, sdr_difference, hdr10_difference);
    CHECK(sdr_difference < 1e-5f);
    CHECK(hdr10_difference > 1e-3f);
  }
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
