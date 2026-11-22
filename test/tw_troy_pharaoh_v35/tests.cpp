// PsychoV35's V35 FIT for Troy and Pharaoh on WARP, through the add-on's built tone mappers (build\<game>.include\embed):
// each game's t00 (Troy 0C12E608, Pharaoh 976C1C74: the ACES fit with its lift, black and mid-gray handoff) then its t01
// hdr_to_screen (Troy 48DBD700, Pharaoh 769869A4: PsychoV35 in Rendering Mode 4), the grade neutral (post-processing
// bypassed: the tone mapper on the raw handoff), Gamma Correction on (the 2.2 display's decode), 1000 / 203 nits. The
// game's constants: burn 0.2 (every Troy trace that draws t00, ps b1 c0.y; Pharaoh's battle and siege,
// tools/tw/pharaoh/phase2.md: level A), Pharaoh's post_processing_strength 0 (traced); brightness and exposure cancel (the
// handoff anchors mid gray: both curves are functions of the exposed scene), in-game gamma 2.0 (the games' default 20).
// The recipe: G04's Shogun 2 pilot (test/tw_shogun2_romer_toe, tools\tw\testbin\scratch\g04_\v35_pilot.md): PsychoV35's
// Contrast, Shadows, Saturation and Highlight Contrast minimax-fitted to the game's SDR below white (grey where SDR is
// 0.05 - 1 and 1 - 15 nits, OkLab chroma at equal lightness where SDR is 1 - 30 nits), PsychoV35's highlights guarded.
// Checks: shared.h <GAME>_V35_* within 5 % and within 0.5 % of the fit's best, their highlights within GUARD at 1000 / 203
// (the fit's) and within WIDE_BOUND at 2500 / 275 (reported: guarding both displays breaks the fit, the pilot's rule);
// controls: Musa's own sliders must fail the 5 %, HDR (Extended) must be the game's grey.
// tests <build folder>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace troy {
#include "src/games/twtroy/shared.h"
}  // namespace troy
#undef SRC_TEMPLATE_SHARED_H_
namespace pharaoh {
#include "src/games/twpharaohdynasties/shared.h"
}  // namespace pharaoh

using Microsoft::WRL::ComPtr;
using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;

constexpr UINT WIDTH = 64;   // grey, then 21 hues x 3 saturations
constexpr UINT HEIGHT = 96;  // a quarter stop a row
constexpr float NITS = 203.f;
// The highlight guard (the pilot's rule, G04 2026-11-22): grey within GUARD of Musa's own from 100 nits to 95 % of Peak at
// 1000 / 203 (10 % left no fit in Shogun 2), within WIDE_BOUND at 2500 / 275 (reported, bounded).
constexpr float GUARD = 0.15f;
constexpr float WIDE_BOUND = 0.2f;

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

static ComPtr<ID3D11Buffer> Constants(const void* data, size_t bytes) {
  std::vector<uint8_t> registers(((bytes + 15) / 16) * 16);
  std::memcpy(registers.data(), data, bytes);
  const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(registers.size()), .Usage = D3D11_USAGE_DEFAULT,
                                  .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = registers.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, buffer.GetAddressOf());
  return buffer;
}

static ComPtr<ID3D11ShaderResourceView> Texture(const std::vector<Float4>& texels) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = texels.data(), .SysMemPitch = WIDTH * 16};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, &initial, texture.GetAddressOf());
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf());
  return view;
}

static ComPtr<ID3D11PixelShader> LoadPixelShader(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<char> code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  ComPtr<ID3D11PixelShader> shader;
  if (code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf()))) {
    std::printf("missing %s (build the add-on first)\n", path.string().c_str());
    std::exit(2);
  }
  return shader;
}

static float Luminance(const Float3& c) { return 0.2126390059f * c[0] + 0.7151686788f * c[1] + 0.0721923154f * c[2]; }

static Float3 Hue(float degrees, float saturation) {
  Float3 rgb;
  for (int i = 0; i < 3; ++i) {
    const float k = std::fmod(degrees / 60.f + (i == 0 ? 5.f : (i == 1 ? 3.f : 1.f)), 6.f);
    rgb[i] = 1.f - std::max(0.f, std::min({k, 4.f - k, 1.f}));
  }
  for (float& channel : rgb) channel = 1.f - saturation * (1.f - channel);
  return rgb;
}

// Each column's colour at one luminance a row: white x 2^(first_stop + row / 4).
static std::vector<Float4> Scene(float first_stop, float white) {
  std::vector<Float3> columns = {{1.f, 1.f, 1.f}};
  for (int h = 0; h < 21; ++h) {
    for (const float s : {0.4f, 0.8f, 0.95f}) columns.push_back(Hue(h * 360.f / 21.f, s));
  }
  std::vector<Float4> texels(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    const float brightness = white * std::exp2(first_stop + 0.25f * y);
    for (UINT x = 0; x < WIDTH; ++x) {
      const float scale = brightness / Luminance(columns[x]);
      texels[y * WIDTH + x] = {columns[x][0] * scale, columns[x][1] * scale, columns[x][2] * scale, 1.f};
    }
  }
  return texels;
}

// OkLab (Ottosson) from linear BT.709: {L, a, b}.
static Float3 OkLab(const Float3& c) {
  const float l = 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2];
  const float m = 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2];
  const float s = 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2];
  const float l3 = std::cbrt(l), m3 = std::cbrt(m), s3 = std::cbrt(s);
  return {0.2104542553f * l3 + 0.7936177850f * m3 - 0.0040720468f * s3, 1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3,
          0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3};
}

struct Bindings {
  std::vector<ComPtr<ID3D11ShaderResourceView>> views;          // from t0
  std::vector<std::pair<UINT, ComPtr<ID3D11Buffer>>> constants;  // slot, buffer
};

// The shader over a WIDTH x HEIGHT float target: its raw output.
static std::vector<Float4> Draw(ID3D11PixelShader* shader, const Bindings& bindings) {
  static ComPtr<ID3D11VertexShader> vertex;
  static ComPtr<ID3D11SamplerState> point;
  if (vertex == nullptr) {
    constexpr char SOURCE[] =
        "void main(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0) {"
        "  uv = float2((id << 1) & 2, id & 2); position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }";
    ComPtr<ID3DBlob> code;
    D3DCompile(SOURCE, sizeof(SOURCE) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, code.GetAddressOf(), nullptr);
    device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vertex.GetAddressOf());
    const D3D11_SAMPLER_DESC desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     .MaxLOD = D3D11_FLOAT32_MAX};
    device->CreateSamplerState(&desc, point.GetAddressOf());
  }
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                            .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf());
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());
  std::vector<ID3D11ShaderResourceView*> views;
  for (const auto& view : bindings.views) views.push_back(view.Get());
  std::array<ID3D11SamplerState*, 16> samplers;
  samplers.fill(point.Get());
  const D3D11_VIEWPORT viewport = {.Width = WIDTH, .Height = HEIGHT, .MaxDepth = 1.f};
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex.Get(), nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
  for (const auto& [slot, buffer] : bindings.constants) context->PSSetConstantBuffers(slot, 1, buffer.GetAddressOf());
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  context->ClearState();
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<Float4> output(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    std::memcpy(output.data() + (y * WIDTH), static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(y) * mapped.RowPitch),
                WIDTH * 16);
  }
  context->Unmap(staging.Get(), 0);
  return output;
}

// The add-on's data: Rendering Mode `hdr`, Hue Clip `hue_clip`, PsychoV35's sliders at Musa's defaults, Extended's at
// their identity, Gamma Correction on, HDR10, 1000 / 203 / 203 nits.
template <typename Injection>
static Injection Data(float hdr, float hue_clip) {
  Injection si = {};
  si.peak_white_nits = 1000.f;
  si.diffuse_white_nits = NITS;
  si.graphics_white_nits = NITS;
  si.gamma_correction = 1.f;
  si.hdr = hdr;
  si.bloom = si.godrays = si.filmgrain = 1.f;
  si.exposure_tpm = si.highlights_tpm = si.shadows_tpm = si.contrast_tpm = si.saturation_tpm = 1.f;
  si.hue_clip = hue_clip;
  si.v35_exposure = si.v35_highlights = si.v35_shadows = si.v35_contrast = 1.f;
  si.v35_highlight_contrast = si.v35_shadow_contrast = 1.f;
  si.v35_saturation = si.v35_highlight_saturation = 1.f;
  si.v35_roll_off = 1.f;
  si.v35_gamut_compression = 1.f;
  return si;
}

// One game's chain: t00 on the scene (exposed = scene x 0.25: brightness 1, exposure 2), t01 on its handoff, the
// output decoded at 2.2 (Gamma Correction on) in units of the UI's white.
struct Chain {
  const char* name;
  ComPtr<ID3D11PixelShader> tone_mapper, hdr_to_screen;
  bool pharaoh;  // Pharaoh's cbuffers: exposure 1 / average luminance; t01's frame at t4, its hdr_to_screen at b5
  ComPtr<ID3D11ShaderResourceView> scene;
  ComPtr<ID3D11Buffer> camera, tone_mapping, colorimetry, hdr_to_screen_constants;
};

static Chain MakeChain(const char* name, const std::filesystem::path& embed, uint32_t t00, uint32_t t01, bool pharaoh) {
  char file[32];
  Chain chain = {.name = name, .pharaoh = pharaoh};
  std::snprintf(file, sizeof(file), "0x%08X.cso", t00);
  chain.tone_mapper = LoadPixelShader(embed / file);
  std::snprintf(file, sizeof(file), "0x%08X.cso", t01);
  chain.hdr_to_screen = LoadPixelShader(embed / file);
  // 2^-16 .. 2^7.75 of the exposed scene: SDR from far below 0.05 nits to its white.
  chain.scene = Texture(Scene(-16.f, 4.f));
  std::array<float, 53 * 4> camera = {};
  for (const UINT c : {27u, 29u, 31u}) {
    camera[(c * 4) + 0] = WIDTH;
    camera[(c * 4) + 1] = HEIGHT;
    camera[(c * 4) + 2] = 1.f / WIDTH;
    camera[(c * 4) + 3] = 1.f / HEIGHT;
  }
  chain.camera = Constants(camera.data(), sizeof(camera));
  // Brightness 1, burn 0.2, adaptation 0 (Troy: exposure 1 / 0.5), average luminance 0.5 (Pharaoh: 1 / 0.5);
  // Pharaoh's post_processing_strength 0 (c1.x).
  const std::array<float, 8> tone_mapping = {1.f, 0.2f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.f};
  chain.tone_mapping = Constants(tone_mapping.data(), sizeof(tone_mapping));
  const std::array<float, 4> colorimetry = {1.f, 2.f, 0.5f, 0.f};  // g_brightness, g_gamma_output, g_inv_gamma_output
  chain.colorimetry = Constants(colorimetry.data(), sizeof(colorimetry));
  // hdr_to_screen_PS: an identity colour_matrix (c2 - c5), overscan 1 (c7.x), the post-processing bypass (c8.y: Troy's
  // disable_post_processing, Pharaoh's post_processing_strength 1), lut_strength 0.
  std::array<float, 10 * 4> post = {};
  for (UINT i = 0; i < 4; ++i) post[((2 + i) * 4) + i] = 1.f;
  post[7 * 4] = 1.f;
  post[(8 * 4) + 1] = 1.f;
  chain.hdr_to_screen_constants = Constants(post.data(), sizeof(post));
  return chain;
}

template <typename Injection>
static std::vector<Float3> Run(const Chain& chain, const Injection& si) {
  const auto injection = Constants(&si, sizeof(si));
  const auto handoff = Draw(chain.tone_mapper.Get(), {.views = {chain.scene},
                                                      .constants = {{0, chain.camera}, {1, chain.tone_mapping}, {13, injection}}});
  Bindings second = {.constants = {{0, chain.colorimetry}, {1, chain.camera}, {chain.pharaoh ? 5u : 4u, chain.hdr_to_screen_constants},
                                   {13, injection}}};
  second.views.resize(chain.pharaoh ? 5 : 3);
  second.views.back() = Texture(handoff);
  std::vector<Float3> linear(WIDTH * HEIGHT);
  const auto output = Draw(chain.hdr_to_screen.Get(), second);
  for (size_t i = 0; i < output.size(); ++i) {
    for (int c = 0; c < 3; ++c) linear[i][c] = std::copysign(std::pow(std::abs(output[i][c]), 2.2f), output[i][c]);
  }
  return linear;
}

struct Range {
  float low = 1e9f, high = 0.f;
};

// PsychoV35 over SDR: grey where SDR is 0.05 - 1, 1 - 15 and 15 - 100 nits; colour (OkLab chroma at equal lightness,
// the geometric mean over the 63 colours and each colour's own) where SDR is 1 - 30 and 30 - 150 nits.
struct Comparison {
  std::array<Range, 3> grey;
  std::array<float, 2> chroma = {};
  std::array<Range, 2> per_colour;
};

static Comparison Compare(const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  Comparison comparison;
  auto& grey = comparison.grey;
  for (UINT y = 0; y < HEIGHT; ++y) {
    const float sdr_nits = Luminance(sdr[y * WIDTH]) * NITS;
    const float ratio = Luminance(v35[y * WIDTH]) / std::max(Luminance(sdr[y * WIDTH]), 1e-9f);
    const int band = (sdr_nits >= 0.05f && sdr_nits <= 1.f) ? 0 : ((sdr_nits > 1.f && sdr_nits <= 15.f) ? 1 : ((sdr_nits > 15.f && sdr_nits <= 100.f) ? 2 : -1));
    if (band < 0) continue;
    grey[band].low = std::min(grey[band].low, ratio);
    grey[band].high = std::max(grey[band].high, ratio);
  }
  // Chroma at equal lightness: PsychoV35's chroma interpolated where its lightness is SDR's.
  std::array<double, 2> log_sums = {};
  std::array<int, 2> counts = {};
  auto& per_colour = comparison.per_colour;
  for (UINT column = 1; column < WIDTH; ++column) {
    std::array<double, 2> column_sums = {};
    std::array<int, 2> column_counts = {};
    for (UINT y = 0; y < HEIGHT; ++y) {
      const Float3& game_colour = sdr[y * WIDTH + column];
      const float sdr_nits = Luminance(game_colour) * NITS;
      const int band = (sdr_nits >= 1.f && sdr_nits <= 30.f) ? 0 : ((sdr_nits > 30.f && sdr_nits <= 150.f) ? 1 : -1);
      if (band < 0) continue;
      const Float3 game_lab = OkLab(game_colour);
      const float game_chroma = std::hypot(game_lab[1], game_lab[2]);
      if (game_chroma < 0.01f) continue;
      for (UINT j = 0; j + 1 < HEIGHT; ++j) {
        const Float3 below = OkLab(v35[j * WIDTH + column]);
        const Float3 above = OkLab(v35[(j + 1) * WIDTH + column]);
        if (game_lab[0] < below[0] || game_lab[0] > above[0]) continue;
        const float t = (above[0] > below[0]) ? (game_lab[0] - below[0]) / (above[0] - below[0]) : 0.f;
        const float chroma = std::lerp(std::hypot(below[1], below[2]), std::hypot(above[1], above[2]), t);
        column_sums[band] += std::log(chroma / game_chroma);
        ++column_counts[band];
        break;
      }
    }
    for (int band = 0; band < 2; ++band) {
      if (column_counts[band] == 0) continue;
      const float column_ratio = static_cast<float>(std::exp(column_sums[band] / column_counts[band]));
      per_colour[band].low = std::min(per_colour[band].low, column_ratio);
      per_colour[band].high = std::max(per_colour[band].high, column_ratio);
      log_sums[band] += column_sums[band];
      counts[band] += column_counts[band];
    }
  }
  for (int band = 0; band < 2; ++band) {
    comparison.chroma[band] = static_cast<float>(std::exp(log_sums[band] / std::max(counts[band], 1)));
  }
  return comparison;
}

static void Report(const std::string& name, const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  bool sdr_rises = true, v35_rises = true;
  for (UINT y = 1; y < HEIGHT; ++y) {
    sdr_rises = sdr_rises && Luminance(sdr[y * WIDTH]) >= Luminance(sdr[(y - 1) * WIDTH]) - 1e-7f;
    v35_rises = v35_rises && Luminance(v35[y * WIDTH]) >= Luminance(v35[(y - 1) * WIDTH]) - 1e-7f;
  }
  const float sdr_bottom = Luminance(sdr[0]) * NITS, sdr_top = Luminance(sdr[(HEIGHT - 1) * WIDTH]) * NITS;
  CHECK(sdr_rises && v35_rises, "%s: the grey ramp falls somewhere (SDR %d, PsychoV35 %d)", name.c_str(), sdr_rises, v35_rises);
  CHECK(sdr_top > 150.f && sdr_bottom < 0.05f, "%s: SDR's ramp %g - %g nits (it misses the bands)", name.c_str(), sdr_bottom, sdr_top);
  const Comparison c = Compare(sdr, v35);
  std::printf("%s: PsychoV35 over Rendering Mode SDR below white, 1000 / 203 nits\n"
              "   grey where SDR is 0.05 - 1 nits x%.3f..x%.3f, 1 - 15 nits x%.3f..x%.3f, 15 - 100 nits x%.3f..x%.3f\n"
              "   colour at equal lightness, SDR 1 - 30 nits x%.3f (per colour x%.2f..x%.2f), 30 - 150 nits x%.3f (x%.2f..x%.2f)\n",
              name.c_str(), c.grey[0].low, c.grey[0].high, c.grey[1].low, c.grey[1].high, c.grey[2].low, c.grey[2].high, c.chroma[0],
              c.per_colour[0].low, c.per_colour[0].high, c.chroma[1], c.per_colour[1].low, c.per_colour[1].high);
}

struct Slider {
  const char* name;
  float low, high;
};
constexpr std::array<Slider, 4> SLIDERS = {
    {{"Contrast", 0.8f, 2.f}, {"Shadows", 0.3f, 1.5f}, {"Saturation", 0.7f, 1.3f}, {"Highlight Contrast", 0.6f, 1.2f}}};
using Values = std::array<float, 4>;

template <typename Injection>
static Injection With(Injection si, const Values& values) {
  si.v35_contrast = values[0];
  si.v35_shadows = values[1];
  si.v35_saturation = values[2];
  si.v35_highlight_contrast = values[3];
  return si;
}

static float GreyError(const Comparison& c) {
  return std::max({std::abs(c.grey[0].low - 1.f), std::abs(c.grey[0].high - 1.f), std::abs(c.grey[1].low - 1.f),
                   std::abs(c.grey[1].high - 1.f)});
}

static float BelowWhite(const Comparison& c) { return std::max(GreyError(c), std::abs(c.chroma[0] - 1.f)); }

// The worst |fitted / own - 1| of grey where Musa's own is 100 nits up to 95 % of Peak.
static float Highlights(const std::vector<Float3>& fitted, const std::vector<Float3>& own, float paper_white, float peak) {
  float worst = 0.f;
  for (UINT y = 0; y < HEIGHT; ++y) {
    const float own_nits = Luminance(own[y * WIDTH]) * paper_white;
    if (own_nits < 100.f || own_nits > 0.95f * peak) continue;
    worst = std::max(worst, std::abs((Luminance(fitted[y * WIDTH]) / Luminance(own[y * WIDTH])) - 1.f));
  }
  return worst;
}

struct Fit {
  Values values = {1.f, 1.f, 1.f, 1.f};
  float score = 1e9f;
  int candidates = 0;
};

// Coarse to fine: every 0.1 over the ranges, then 0.05 and 0.01 steps around the best (strict "<": the first best
// stays); a candidate counts only if its highlights stay within `guard` of Musa's own.
template <typename Injection>
static Fit FitSliders(const Chain& chain, const Injection& own, const std::vector<Float3>& sdr, float guard) {
  const auto own_ramp = Run(chain, own);
  Fit best;
  bool first_pass = true;
  const auto start = std::chrono::steady_clock::now();
  for (const auto& [step, reach] : {std::pair{0.1f, 6}, std::pair{0.05f, 2}, std::pair{0.01f, 3}}) {
    const Values centre = (first_pass ? Values{1.4f, 0.9f, 1.f, 0.9f} : best.values);
    first_pass = false;
    std::array<std::vector<float>, 4> grid;
    for (size_t d = 0; d < SLIDERS.size(); ++d) {
      for (int k = -reach; k <= reach; ++k) {
        const float value = std::round((centre[d] + (static_cast<float>(k) * step)) * 100.f) / 100.f;
        if (value >= SLIDERS[d].low - 1e-4f && value <= SLIDERS[d].high + 1e-4f) grid[d].push_back(value);
      }
    }
    for (const float contrast : grid[0]) {
      for (const float shadows : grid[1]) {
        for (const float saturation : grid[2]) {
          for (const float highlight_contrast : grid[3]) {
            const Values values = {contrast, shadows, saturation, highlight_contrast};
            const auto ramp = Run(chain, With(own, values));
            ++best.candidates;
            // Score first, the guard only for a candidate that beats the best.
            if (const float score = BelowWhite(Compare(sdr, ramp)); score < best.score && Highlights(ramp, own_ramp, NITS, 1000.f) <= guard) {
              best.values = values;
              best.score = score;
            }
          }
        }
      }
    }
  }
  std::printf("   minimax fit, highlight guard %.0f %% (%s %.1f - %.1f, %s %.1f - %.1f, %s %.1f - %.1f, %s %.1f - %.1f): "
              "%.2f / %.2f / %.2f / %.2f, worst %.1f %% (%d candidates, %.0f s)\n",
              100.f * guard, SLIDERS[0].name, SLIDERS[0].low, SLIDERS[0].high, SLIDERS[1].name, SLIDERS[1].low, SLIDERS[1].high,
              SLIDERS[2].name, SLIDERS[2].low, SLIDERS[2].high, SLIDERS[3].name, SLIDERS[3].low, SLIDERS[3].high, best.values[0],
              best.values[1], best.values[2], best.values[3], 100.f * best.score, best.candidates,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  return best;
}

// The fit, the checks and the reports for one game; `shipped`: its shared.h <GAME>_V35_*.
template <typename Injection>
static void FitGame(const Chain& chain, float hue_clip, const Values& shipped_values) {
  const auto sdr = Run(chain, Data<Injection>(0.f, hue_clip));
  const auto own = Data<Injection>(4.f, hue_clip);
  std::printf("%s V35 fit, burn 0.2, Hue Clip %.0f:\n", chain.name, 100.f * hue_clip);
  Report(std::string(chain.name) + " Musa's own sliders (before the fit)", sdr, Run(chain, own));
  const Fit best = FitSliders(chain, own, sdr, GUARD);
  const auto shipped = With(own, shipped_values);
  const float own_score = BelowWhite(Compare(sdr, Run(chain, own)));
  const auto shipped_ramp = Run(chain, shipped);
  const float shipped_score = BelowWhite(Compare(sdr, shipped_ramp));
  std::printf("   shared.h (%.2f / %.2f / %.2f / %.2f): worst %.1f %%; Musa's own (control, must fail 5 %%): %.1f %%\n",
              shipped_values[0], shipped_values[1], shipped_values[2], shipped_values[3], 100.f * shipped_score, 100.f * own_score);
  CHECK(shipped_score < 0.05f, "%s's V35 defaults miss the game by %.1f %%", chain.name, 100.f * shipped_score);
  CHECK(shipped_score <= best.score + 0.005f, "%s's V35 defaults (%.1f %%) are not the fit's best (%.1f %%)", chain.name,
        100.f * shipped_score, 100.f * best.score);
  CHECK(own_score > 0.05f, "control: Musa's own sliders pass (%.1f %%): the measure sees nothing", 100.f * own_score);
  Report(std::string(chain.name) + " V35 defaults (after the fit)", sdr, shipped_ramp);
  for (const auto& [peak, paper_white, bound] : {std::tuple{1000.f, 203.f, GUARD}, std::tuple{2500.f, 275.f, WIDE_BOUND}}) {
    auto shipped_at = shipped;
    auto own_at = own;
    shipped_at.peak_white_nits = own_at.peak_white_nits = peak;
    shipped_at.diffuse_white_nits = own_at.diffuse_white_nits = paper_white;
    shipped_at.graphics_white_nits = own_at.graphics_white_nits = paper_white;
    const float worst = Highlights(Run(chain, shipped_at), Run(chain, own_at), paper_white, peak);
    std::printf("   highlights, Peak %.0f / Paper White %.0f: grey within %.1f %% of Musa's own (100 nits - 95 %% of Peak)\n", peak,
                paper_white, 100.f * worst);
    CHECK(worst <= bound, "%s's V35 defaults move PsychoV35's highlights by %.1f %% at %.0f / %.0f", chain.name, 100.f * worst,
          peak, paper_white);
  }
  // The measure's own check: HDR (Extended, its white point's expansion off) is the game's curve below its white (every
  // grey band here), so a wrong decode, white or binding would show.
  const Comparison extended = Compare(sdr, Run(chain, Data<Injection>(1.f, hue_clip)));
  const float extended_grey = GreyError(extended);
  std::printf("   control, HDR (Extended): grey %.2f %% off, chroma x%.3f (must be the game's grey)\n", 100.f * extended_grey,
              extended.chroma[0]);
  CHECK(extended_grey < 0.005f, "%s's HDR (Extended) is %.2f %% off the game's grey below white", chain.name, 100.f * extended_grey);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("tests <build folder>\n");
    return 2;
  }
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    return 2;
  }
  const std::filesystem::path build = argv[1];
  // Each game's Hue Clip default (addon.cpp HUE_CLIP_DEFAULT: Troy 70, Pharaoh 80).
  FitGame<troy::ShaderInjectData>(MakeChain("Troy", build / "twtroy.include/embed", 0x0C12E608, 0x48DBD700, false), 0.7f,
                                  {TROY_V35_CONTRAST, TROY_V35_SHADOWS, TROY_V35_SATURATION, TROY_V35_HIGHLIGHT_CONTRAST});
  FitGame<pharaoh::ShaderInjectData>(
      MakeChain("Pharaoh", build / "twpharaohdynasties.include/embed", 0x976C1C74, 0x769869A4, true), 0.8f,
      {PHARAOH_V35_CONTRAST, PHARAOH_V35_SHADOWS, PHARAOH_V35_SATURATION, PHARAOH_V35_HIGHLIGHT_CONTRAST});
  std::printf(failures == 0 ? "PASS\n" : "FAILED: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
