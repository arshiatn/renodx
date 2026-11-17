// twattila/rt_debug.cs_5_0.hlsl's Point Light Shadows view (6) on the light mask's words (rt_local_lights.hlsli), on WARP:
// the shader compiled from the tree given. Torch and Fire Shadows from Path Tracing writes a pixel's word as its shadow ratio
// (a float, top bit clear) or, where no ratio texel serves it, its own bits with the top bit set; rt_core.hpp's
// DrawDebugView flags such frames (rt_debug.y bit 8).
// 1. Flagged: a ratio word red by 1 - ratio (1 grey, 0.5 half, 0 red); a top-bit word by its blocked bits.
// 2. Not flagged (every frame without the row): every word by its bits, as before (bits words, and a ratio word's bits).
// tests.exe <repository> [old]: old: the tree's shader is today's (CONTROL: its flagged ratio words read as bits, red).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT TRACED = 3;  // lights traced (rt_debug.z): bits 0-2
constexpr uint32_t RATIO_WORDS = 256u;
constexpr uint32_t VIEW_6_MADE = 1u << 5;

struct Case {
  const char* name;
  uint32_t word;
  bool ratio;  // the word is a shadow ratio in a flagged frame
};

// The view's colour (rt_debug.cs_5_0.hlsl): grey to red by `red`.
std::array<float, 3> Colour(float red) {
  return {0.8f, 0.8f * (1.f - red), 0.8f * (1.f - red)};
}

float BlockedShare(uint32_t word) {
  const uint32_t traced = (1u << TRACED) - 1u;
  return std::min(static_cast<float>(std::popcount(~word & traced)) / 3.f, 1.f);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const bool old = argc > 2 && std::string(argv[2]) == "old";
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
  const std::wstring path = std::wstring(argv[1], argv[1] + std::strlen(argv[1])) + L"\\src\\games\\twattila\\rt_debug.cs_5_0.hlsl";
  ComPtr<ID3DBlob> blob, errors;
  if (FAILED(D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0", 0, 0, &blob, &errors))) {
    std::printf("compile: %s\nFAIL\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return 1;
  }
  ComPtr<ID3D11ComputeShader> shader;
  device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);

  const std::array<Case, 8> cases = {{
      {"bits: lights 0 and 2 blocked", ~0b101u, false},
      {"bits: none blocked", 0xFFFFFFFFu, false},
      {"ratio 1", std::bit_cast<uint32_t>(1.f), true},
      {"ratio 0.5", std::bit_cast<uint32_t>(0.5f), true},
      {"ratio 0.25", std::bit_cast<uint32_t>(0.25f), true},
      {"ratio 0", std::bit_cast<uint32_t>(0.f), true},
      {"own bits: light 1 blocked", 0xFFFFFFFDu, false},
      {"own bits: none blocked", 0xFFFFFFFFu, false},
  }};
  constexpr UINT W = 8, H = 1;
  std::array<uint32_t, W * H> words = {};
  for (size_t index = 0; index < cases.size(); ++index) words[index] = cases[index].word;
  const D3D11_TEXTURE2D_DESC mask_desc = {.Width = W, .Height = H, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_UINT,
                                          .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA mask_init = {.pSysMem = words.data(), .SysMemPitch = W * 4};
  ComPtr<ID3D11Texture2D> mask;
  device->CreateTexture2D(&mask_desc, &mask_init, &mask);
  ComPtr<ID3D11ShaderResourceView> mask_view;
  device->CreateShaderResourceView(mask.Get(), nullptr, &mask_view);
  D3D11_TEXTURE2D_DESC picture_desc = {.Width = W, .Height = H, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                       .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  ComPtr<ID3D11Texture2D> picture;
  device->CreateTexture2D(&picture_desc, nullptr, &picture);
  ComPtr<ID3D11UnorderedAccessView> picture_view;
  device->CreateUnorderedAccessView(picture.Get(), nullptr, &picture_view);

  // One dispatch of view 6 (flagged or not): the picture's rgb a pixel.
  const auto run = [&](bool flagged) {
    struct Constants {
      std::array<uint32_t, 4> view;
      std::array<float, 4> size;
      std::array<float, 4> ao_mapping;
    };
    const Constants constants = {.view = {6u, VIEW_6_MADE | (flagged ? RATIO_WORDS : 0u), TRACED, 2u}, .size = {W, H, W, H}, .ao_mapping = {}};
    const D3D11_BUFFER_DESC buffer_desc = {.ByteWidth = sizeof(Constants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA buffer_init = {.pSysMem = &constants};
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&buffer_desc, &buffer_init, &buffer);
    ID3D11ShaderResourceView* views[8] = {};
    views[5] = mask_view.Get();
    context->CSSetShaderResources(0, 8, views);
    context->CSSetConstantBuffers(0, 1, buffer.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, picture_view.GetAddressOf(), nullptr);
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->Dispatch(1, 1, 1);
    picture_desc.Usage = D3D11_USAGE_STAGING;
    picture_desc.BindFlags = 0;
    picture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&picture_desc, nullptr, &staging);
    picture_desc.Usage = D3D11_USAGE_DEFAULT;
    picture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    picture_desc.CPUAccessFlags = 0;
    context->CopyResource(staging.Get(), picture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    std::array<std::array<float, 4>, W> out = {};
    std::memcpy(out.data(), mapped.pData, sizeof(out));
    context->Unmap(staging.Get(), 0);
    return out;
  };

  int failures = 0;
  int flagged_apart = 0;
  int unflagged_apart = 0;
  for (const bool flagged : {true, false}) {
    const auto out = run(flagged);
    for (size_t index = 0; index < cases.size(); ++index) {
      const Case& c = cases[index];
      const float red = (flagged && c.ratio) ? (1.f - std::clamp(std::bit_cast<float>(c.word), 0.f, 1.f)) : BlockedShare(c.word);
      const auto expected = Colour(red);
      bool apart = false;
      for (int channel = 0; channel < 3; ++channel) apart = apart || std::abs(out[index][channel] - expected[channel]) > 1e-5f;
      std::printf("%s %-28s word %08X: picture %.3f %.3f %.3f, expected %.3f %.3f %.3f%s\n", flagged ? "flagged    " : "not flagged", c.name, c.word,
                  out[index][0], out[index][1], out[index][2], expected[0], expected[1], expected[2], apart ? " [apart]" : "");
      (flagged ? flagged_apart : unflagged_apart) += apart ? 1 : 0;
    }
  }
  if (old) {
    // CONTROL: today's shader reads the flagged ratio words as bits.
    std::printf("CONTROL (today's shader): %d flagged pixels apart (the ratio words), %d not flagged\n", flagged_apart, unflagged_apart);
    failures += (flagged_apart > 0 && unflagged_apart == 0) ? 0 : 1;
  } else {
    std::printf("flagged: %d pixels apart; not flagged: %d apart\n", flagged_apart, unflagged_apart);
    failures += (flagged_apart == 0 && unflagged_apart == 0) ? 0 : 1;
  }
  std::printf(failures == 0 ? "RT DEBUG VIEW ALL PASSED\n" : "RT DEBUG VIEW FAILED\n");
  return failures == 0 ? 0 : 1;
}
