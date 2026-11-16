// Ryse's tonemap00 (HDRFinalPass, 0xB9B672AA) on WARP: Rendering Mode SDR (the add-on's built shader, hdr 0) against the
// game's own bytes over an exposure ramp of grey and coloured rows, with the same constants and textures: the SDR encode
// (Hable, sRGB, the sun shafts' screen, the 16^3 colour chart) must be the game's. Control: HDR (Extended) on the same
// inputs must differ (else the test could not see a change). Then PsychoV35 against the game below white (grey, chroma):
// Ryse's slider defaults within 5 % and the minimax fit's best, Musa's own must fail (Extended must match the game
// there). Arguments: the repository (its
// build's embedded csos) and the game's dumped 0xB9B672AA.ps_5_0.cso; both only read.
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
#include <vector>

#include "src/games/rysesonofrome/shared.h"

using Microsoft::WRL::ComPtr;

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition);               \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The game's VS outputs as the pixel shader reads them: TEXCOORD0's xy the full-screen uv (the luminance, bloom and
// vignette), TEXCOORD2's xy too (the scene, shafts and alpha).
constexpr char VERTEX_SHADER[] = R"(
struct Output {
  float4 position : SV_Position;
  float4 uv0 : TEXCOORD0;
  float4 uv1 : TEXCOORD1;
  float4 uv2 : TEXCOORD2;
};
Output main(uint id : SV_VertexID) {
  const float2 uv = float2((id << 1) & 2, id & 2);
  Output output;
  output.position = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
  output.uv0 = float4(uv, 0.f, 0.f);
  output.uv1 = 0.f;
  output.uv2 = float4(uv, 0.f, 0.f);
  return output;
}
)";

constexpr UINT WIDTH = 256;
constexpr UINT HEIGHT = 4;

struct PerBatch {
  std::array<float, 4> hdr_params1;
  std::array<float, 4> color_balance;
  std::array<float, 4> sun_colour;
  std::array<float, 4> eye_adaptation;
  std::array<float, 4> film_curve;
  std::array<float, 4> hdr_params0;
  std::array<float, 4> bloom_colour;
  std::array<float, 4> hdr_params2;
  std::array<float, 4> screen_size;
};

ComPtr<ID3D11ShaderResourceView> Texture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, const void* data, UINT row_bytes) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width,
                                     .Height = height,
                                     .MipLevels = 1,
                                     .ArraySize = 1,
                                     .Format = format,
                                     .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_IMMUTABLE,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = row_bytes};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  if (FAILED(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
    std::printf("texture %ux%u f%d could not be made\n", width, height, static_cast<int>(format));
    std::exit(2);
  }
  return view;
}

ComPtr<ID3D11Buffer> Constants(ID3D11Device* device, const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = (bytes + 15u) & ~15u, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> padded(desc.ByteWidth, 0);
  std::memcpy(padded.data(), data, bytes);
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device->CreateBuffer(&desc, &initial, buffer.GetAddressOf()))) std::exit(2);
  return buffer;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: ryse_tonemap.exe <repository> <the game's 0xB9B672AA.ps_5_0.cso>\n");
    return 2;
  }
  const std::filesystem::path repository = argv[1];
  const auto built = ReadFile(repository / "build/rysesonofrome.include/embed/0xB9B672AA.cso");
  const auto game = ReadFile(argv[2]);
  if (built.empty() || game.empty()) {
    std::printf("missing: %s%s\n", built.empty() ? "the build's 0xB9B672AA.cso " : "", game.empty() ? "the game's cso" : "");
    return 2;
  }

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("no WARP device\n");
    return 2;
  }
  ComPtr<ID3DBlob> vertex_code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(VERTEX_SHADER, sizeof(VERTEX_SHADER) - 1, "vs", nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_code.GetAddressOf(),
                        errors.GetAddressOf()))) {
    std::printf("vs: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return 2;
  }
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> game_shader;
  ComPtr<ID3D11PixelShader> built_shader;
  if (FAILED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf()))
      || FAILED(device->CreatePixelShader(game.data(), game.size(), nullptr, game_shader.GetAddressOf()))
      || FAILED(device->CreatePixelShader(built.data(), built.size(), nullptr, built_shader.GetAddressOf()))) {
    std::printf("shaders could not be made\n");
    return 2;
  }

  // The scene: 256 steps from 2^-10 to 2^8 per row, rows grey, warm (fire), cold (sky), green.
  constexpr std::array<std::array<float, 3>, HEIGHT> HUES = {{{1.f, 1.f, 1.f}, {1.f, 0.35f, 0.08f}, {0.12f, 0.3f, 1.f}, {0.2f, 1.f, 0.3f}}};
  std::vector<float> scene(WIDTH * HEIGHT * 4);
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const float level = std::exp2(-10.f + (18.f * static_cast<float>(x) / static_cast<float>(WIDTH - 1)));
      for (UINT channel = 0; channel < 3; ++channel) scene[((y * WIDTH + x) * 4) + channel] = level * HUES[y][channel];
      scene[((y * WIDTH + x) * 4) + 3] = 1.f;
    }
  }
  // The colour chart: 16^3 unwrapped into 256 x 16 (red across a slice, green down, blue by slice), a slight grade on it
  // (the chart's sampling, not identity, is what is compared).
  std::vector<uint8_t> chart(256 * 16 * 4);
  for (UINT y = 0; y < 16; ++y) {
    for (UINT x = 0; x < 256; ++x) {
      const float red = static_cast<float>(x % 16) / 15.f;
      const float green = static_cast<float>(y) / 15.f;
      const float blue = static_cast<float>(x / 16) / 15.f;
      const std::array<float, 3> graded = {std::pow(red, 1.1f), green, std::min(blue * 1.05f, 1.f)};
      for (UINT channel = 0; channel < 3; ++channel) chart[((y * 256 + x) * 4) + channel] = static_cast<uint8_t>(std::lround(graded[channel] * 255.f));
      chart[((y * 256 + x) * 4) + 3] = 255;
    }
  }
  const std::array<float, 4> luminance = {0.25f, 0.f, 0.f, 0.f};
  const std::array<float, 4> bloom = {0.04f, 0.03f, 0.02f, 1.f};
  const std::array<float, 4> alpha = {0.f, 0.f, 1.f, 1.f};
  const std::array<float, 4> vignette = {0.9f, 0.9f, 0.9f, 1.f};
  const std::array<float, 4> shafts = {0.2f, 0.18f, 0.12f, 1.f};
  std::array<ComPtr<ID3D11ShaderResourceView>, 10> views = {
      Texture(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, scene.data(), WIDTH * 16),
      Texture(device.Get(), 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, luminance.data(), 16),
      Texture(device.Get(), 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, bloom.data(), 16),
      Texture(device.Get(), 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, alpha.data(), 16),
      nullptr,
      nullptr,
      nullptr,
      Texture(device.Get(), 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, vignette.data(), 16),
      Texture(device.Get(), 256, 16, DXGI_FORMAT_R8G8B8A8_UNORM, chart.data(), 256 * 4),
      Texture(device.Get(), 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, shafts.data(), 16),
  };
  // Hable's own constants (A, B, E, W in HDRFilmCurve), the eye adaptation's clamp, the bloom's lerp, full sun shafts.
  const PerBatch per_batch = {
      .hdr_params1 = {0.1f, 0.f, 0.f, 0.f},
      .color_balance = {1.f, 0.98f, 0.95f, 1.1f},
      .sun_colour = {1.f, 0.85f, 0.6f, 1.f},
      .eye_adaptation = {0.f, 0.25f, 8.f, 0.f},
      .film_curve = {0.22f, 0.3f, 0.01f, 11.2f},
      .hdr_params0 = {1.f, 0.f, 0.f, 0.f},
      .bloom_colour = {0.15f, 0.15f, 0.15f, 0.f},
      .hdr_params2 = {0.f, 1.f, 1.f, 0.f},
      .screen_size = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT},
  };
  const auto batch_buffer = Constants(device.Get(), &per_batch, sizeof(per_batch));

  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                                           .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = WIDTH,
                                            .Height = HEIGHT,
                                            .MipLevels = 1,
                                            .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                            .SampleDesc = {.Count = 1},
                                            .BindFlags = D3D11_BIND_RENDER_TARGET};
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11Texture2D> staging;
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf());
  device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
  device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());

  // One draw of `shader` with the add-on's settings `injection` at b13 (the game's shader ignores it): its RGBA out.
  const auto draw = [&](ID3D11PixelShader* shader, const ShaderInjectData& injection) {
    const auto injection_buffer = Constants(device.Get(), &injection, sizeof(injection));
    std::array<ID3D11ShaderResourceView*, 10> bound = {};
    for (size_t slot = 0; slot < views.size(); ++slot) bound[slot] = views[slot].Get();
    std::array<ID3D11SamplerState*, 10> samplers = {};
    samplers.fill(sampler.Get());
    context->ClearState();
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(shader, nullptr, 0);
    context->PSSetShaderResources(0, static_cast<UINT>(bound.size()), bound.data());
    context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
    ID3D11Buffer* batch = batch_buffer.Get();
    ID3D11Buffer* settings = injection_buffer.Get();
    context->PSSetConstantBuffers(0, 1, &batch);
    context->PSSetConstantBuffers(13, 1, &settings);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(WIDTH), .Height = static_cast<float>(HEIGHT), .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    ID3D11RenderTargetView* targets[] = {target_view.Get()};
    context->OMSetRenderTargets(1, targets, nullptr);
    context->Draw(3, 0);
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    std::vector<float> pixels(WIDTH * HEIGHT * 4);
    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      for (UINT y = 0; y < HEIGHT; ++y) std::memcpy(&pixels[y * WIDTH * 4], static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch), WIDTH * 16);
      context->Unmap(staging.Get(), 0);
    }
    return pixels;
  };
  // The page's defaults; Rendering Mode and Output per run.
  ShaderInjectData injection = {};
  injection.peak_white_nits = 1000.f;
  injection.diffuse_white_nits = 203.f;
  injection.graphics_white_nits = 203.f;
  for (float* multiplier : {&injection.bloom, &injection.vignette, &injection.sun_shafts, &injection.lens_flare, &injection.film_grain,
                            &injection.ext_exposure, &injection.ext_highlights, &injection.ext_shadows, &injection.ext_contrast,
                            &injection.ext_saturation, &injection.ext_highlight_saturation}) {
    *multiplier = 1.f;
  }
  const auto game_pixels = draw(game_shader.Get(), injection);
  const auto max_difference = [&](const std::vector<float>& pixels, float* at_level) {
    float worst = 0.f;
    for (size_t index = 0; index < pixels.size(); ++index) {
      const float difference = std::abs(pixels[index] - game_pixels[index]);
      if (difference > worst) {
        worst = difference;
        *at_level = std::exp2(-10.f + (18.f * static_cast<float>((index / 4) % WIDTH) / static_cast<float>(WIDTH - 1)));
      }
    }
    return worst;
  };
  // Rendering Mode SDR with each output (the SDR branch reads neither Output nor Peak).
  for (const float output : {2.f, 1.f, 0.f}) {
    injection.hdr = 0.f;
    injection.swap_chain_encoding = output;
    float level = 0.f;
    const float worst = max_difference(draw(built_shader.Get(), injection), &level);
    std::printf("SDR, Output %.0f: largest difference from the game's %.3g (scene %.4g; 1 code value of 8 bits is %.3g)\n", output, worst, level, 1.f / 255.f);
    CHECK(worst <= 1e-6f);
  }
  {
    injection.hdr = 1.f;
    injection.swap_chain_encoding = 1.f;
    float level = 0.f;
    const float worst = max_difference(draw(built_shader.Get(), injection), &level);
    std::printf("Control, HDR (Extended), Output scRGB: largest difference %.3g (scene %.4g): must be large\n", worst, level);
    CHECK(worst > 1e-2f);
  }
  // PsychoV35 against the game (hdr-review-rules.md "PsychoV calls": V35 fitted to the game's chain): Rendering Mode
  // PsychoV35, Output scRGB, 1000 / 203, Hue Clip off, both outputs decoded with 2.2 (the game's SDR white at Paper White).
  // Grey (row 0): the largest |V35 / game - 1| where the game puts it at 0.05 - 1 and 1 - 15 nits; colour (rows 1-3): the
  // mean |OkLab chroma ratio - 1| where the game's luminance is 1 - 30 nits. The shipped defaults (shared.h RYSE_V35_*)
  // must sit within 5 % on all three and be the minimax grid's best of Contrast / Shadows / Saturation (Highlight Contrast
  // 1: these ranges sit below Hable's mid grey); control: Musa's own sliders (1, 1, 1) must fail the 5 %.
  // The curve alone: no bloom and no sun shafts here (their adds lift the game's black to ~5 nits with this test's
  // values). Level B: the constants are this test's, not traced ones (the frame trace logs the game's PER_BATCH).
  {
    const std::array<float, 4> nothing = {0.f, 0.f, 0.f, 1.f};
    views[2] = Texture(device.Get(), 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, nothing.data(), 16);
    views[9] = Texture(device.Get(), 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, nothing.data(), 16);
    const auto game_curve = draw(game_shader.Get(), injection);
    const auto decode = [](float value) { return std::pow(std::max(value, 0.f), 2.2f); };
    const auto oklab = [](float r, float g, float b) {
      const float l = std::cbrt((0.4122214708f * r) + (0.5363325363f * g) + (0.0514459929f * b));
      const float m = std::cbrt((0.2119034982f * r) + (0.6806995451f * g) + (0.1073969566f * b));
      const float s = std::cbrt((0.0883024619f * r) + (0.2817188376f * g) + (0.6299787005f * b));
      return std::array<float, 3>{(0.2104542553f * l) + (0.7936177850f * m) - (0.0040720468f * s),
                                  (1.9779984951f * l) - (2.4285922050f * m) + (0.4505937099f * s),
                                  (0.0259040371f * l) + (0.7827717662f * m) - (0.8086757660f * s)};
    };
    // {shadows (grey 0.05 - 1 nits), mid tones (grey 1 - 15 nits), colour (mean over rows 1-3)} of `pixels` against the game.
    const auto errors = [&](const std::vector<float>& pixels) {
      std::array<double, 3> worst = {};
      double chroma_sum = 0.0;
      int chroma_count = 0;
      for (UINT y = 0; y < HEIGHT; ++y) {
        for (UINT x = 0; x < WIDTH; ++x) {
          const size_t index = ((y * WIDTH) + x) * 4;
          const std::array<float, 3> game = {decode(game_curve[index]), decode(game_curve[index + 1]), decode(game_curve[index + 2])};
          const std::array<float, 3> ours = {decode(pixels[index]), decode(pixels[index + 1]), decode(pixels[index + 2])};
          const float game_nits = 203.f * ((0.2126f * game[0]) + (0.7152f * game[1]) + (0.0722f * game[2]));
          const float ours_nits = 203.f * ((0.2126f * ours[0]) + (0.7152f * ours[1]) + (0.0722f * ours[2]));
          if (y == 0 && game_nits >= 0.05f && game_nits <= 15.f) {
            const size_t range = (game_nits < 1.f) ? 0u : 1u;
            worst[range] = std::max(worst[range], std::abs((static_cast<double>(ours_nits) / game_nits) - 1.0));
          } else if (y != 0 && game_nits >= 1.f && game_nits <= 30.f) {
            const auto game_lab = oklab(game[0], game[1], game[2]);
            const auto ours_lab = oklab(ours[0], ours[1], ours[2]);
            const double game_chroma = std::hypot(game_lab[1], game_lab[2]);
            if (game_chroma > 0.01) {
              chroma_sum += std::abs((std::hypot(ours_lab[1], ours_lab[2]) / game_chroma) - 1.0);
              ++chroma_count;
            }
          }
        }
      }
      worst[2] = (chroma_count != 0) ? chroma_sum / chroma_count : 0.0;
      return worst;
    };
    ShaderInjectData v35 = injection;
    v35.hdr = 3.f;
    v35.swap_chain_encoding = 1.f;
    v35.hue_clip = 0.f;
    v35.v35_exposure = 1.f;
    v35.v35_highlights = 1.f;
    v35.v35_shadows = 1.f;
    v35.v35_contrast = 1.f;
    v35.v35_highlight_contrast = 1.f;
    v35.v35_shadow_contrast = 1.f;
    v35.v35_flare = 0.f;
    v35.v35_saturation = 1.f;
    v35.v35_dechroma = 0.f;
    v35.v35_highlight_saturation = 1.f;
    v35.v35_roll_off = 1.f;
    v35.v35_gamut_compression = 1.f;
    ShaderInjectData shipped_v35 = v35;
    shipped_v35.v35_contrast = RYSE_V35_CONTRAST;
    shipped_v35.v35_shadows = RYSE_V35_SHADOWS;
    shipped_v35.v35_saturation = RYSE_V35_SATURATION;
    const auto own = errors(draw(built_shader.Get(), v35));
    const auto shipped = errors(draw(built_shader.Get(), shipped_v35));
    for (const auto& [name, measured] : {std::pair{"Musa's own sliders (before)", own}, std::pair{"Ryse's defaults (after)", shipped}}) {
      std::printf("PsychoV35 at %s against the game: grey %.1f %% (0.05 - 1 nits), %.1f %% (1 - 15 nits) worst; colour "
                  "chroma %.1f %% mean (1 - 30 nits)\n",
                  name, 100.0 * measured[0], 100.0 * measured[1], 100.0 * measured[2]);
      CHECK(std::isfinite(measured[0]) && std::isfinite(measured[1]) && std::isfinite(measured[2]));
    }
    const double shipped_score = std::max({shipped[0], shipped[1], shipped[2]});
    CHECK(shipped_score < 0.05);
    std::printf("Control, Musa's own sliders: worst %.1f %%: must fail the 5 %%\n", 100.0 * std::max({own[0], own[1], own[2]}));
    CHECK(std::max({own[0], own[1], own[2]}) > 0.05);
    double best_score = std::max({own[0], own[1], own[2]});
    std::array<float, 3> best = {1.f, 1.f, 1.f};
    for (int contrast = 0; contrast <= 12; ++contrast) {
      for (int shadows = 0; shadows <= 20; ++shadows) {
        for (int saturation = 0; saturation <= 12; ++saturation) {
          ShaderInjectData fitted = v35;
          fitted.v35_contrast = 0.7f + (0.05f * contrast);
          fitted.v35_shadows = 0.5f + (0.05f * shadows);
          fitted.v35_saturation = 0.8f + (0.05f * saturation);
          const auto fit = errors(draw(built_shader.Get(), fitted));
          if (const double score = std::max({fit[0], fit[1], fit[2]}); score < best_score) {
            best_score = score;
            best = {fitted.v35_contrast, fitted.v35_shadows, fitted.v35_saturation};
          }
        }
      }
    }
    std::printf("  minimax fit (Contrast 0.7 - 1.3, Shadows 0.5 - 1.5, Saturation 0.8 - 1.4, steps 0.05): Contrast %.2f, Shadows "
                "%.2f, Saturation %.2f, worst %.1f %% (the shipped defaults: %.1f %%)\n",
                best[0], best[1], best[2], 100.0 * best_score, 100.0 * shipped_score);
    // The defaults are the grid's best (within 0.5 %: a tie a step away).
    CHECK(shipped_score <= best_score + 0.005);
    // V35 / game on the grey row where the game first reaches 0.1, 1, 5 and 15 nits (above 1: V35 brighter).
    for (const auto& [name, pixels] : {std::pair{"Musa's own", draw(built_shader.Get(), v35)}, std::pair{"Ryse's defaults", draw(built_shader.Get(), shipped_v35)}}) {
      std::printf("  grey V35 / game, %s:", name);
      for (const float nits : {0.1f, 1.f, 5.f, 15.f}) {
        for (UINT x = 0; x < WIDTH; ++x) {
          const float game_nits = 203.f * decode(game_curve[(x * 4) + 1]);
          if (game_nits >= nits) {
            std::printf(" %.3g at %.3g nits", decode(pixels[(x * 4) + 1]) * 203.f / game_nits, game_nits);
            break;
          }
        }
      }
      std::printf("\n");
    }
    // The measure's own check: Extended (below white the game's SDR, its grading skipped at 1) must sit within the 5 % the
    // rule asks of the grey; a wrong decode or white would put it off.
    ShaderInjectData extended = injection;
    extended.hdr = 1.f;
    extended.swap_chain_encoding = 1.f;
    const auto extended_errors = errors(draw(built_shader.Get(), extended));
    std::printf("  Extended (the game's SDR below white) by the same measure: grey %.1f %% / %.1f %%, colour %.1f %%\n",
                100.0 * extended_errors[0], 100.0 * extended_errors[1], 100.0 * extended_errors[2]);
    CHECK(extended_errors[0] < 0.05 && extended_errors[1] < 0.05);
  }
  // A few of the game's values, for the record (grey row: encoded sRGB after the chart).
  for (const UINT x : {0u, 64u, 128u, 192u, 255u}) {
    std::printf("  grey %.4g -> %.5f %.5f %.5f, alpha %.3f\n", std::exp2(-10.f + (18.f * x / (WIDTH - 1))), game_pixels[x * 4], game_pixels[(x * 4) + 1],
                game_pixels[(x * 4) + 2], game_pixels[(x * 4) + 3]);
  }
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
