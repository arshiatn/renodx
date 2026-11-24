// R.U.S.E.'s V35 FIT on WARP (the recipe every game copies: tools\tw\testbin\scratch\g04_\v35_pilot.md): the add-on's
// final post (src/games/ruse/shaders/postprocess00_0x232EB5DF, D3D9 ps_3_0 in the game, compiled by run.ps1 -V35 as
// ps_5_0 /Gec from the same source, as test/tw_psychov35 runs it) at Rendering Mode SDR (the game's picture: ru_post_vanilla
// proves it the game's bytecode at Paper White 203) against PsychoV35 at Hue Clip 100 (Recommended's), 1000 / 203 nits,
// both decoded as a 2.2 display shows them. The map's grade (TPostProcess Brightness * rgb * luma^(Gamma - 1),
// tools/nontw/ruse/map_postprocess.tsv) at the median map's Gamma 1.25 / Brightness 1.33 for the fit; the menu's and the
// steepest map's as reports. PsychoV35's Contrast, Shadows, Saturation and Highlight Contrast minimax-fitted to the game's
// SDR below white: the worst of grey |V35 / SDR - 1| where SDR is 0.05 - 1 and 1 - 15 nits and |chroma - 1| (OkLab at
// equal lightness, the geometric mean) where SDR is 1 - 30 nits, a candidate counting only if it keeps PsychoV35's
// highlights (grey within 10 % of Musa's own where Musa's own is 100 nits up to 95 % of Peak). Coarse to fine: every 0.1
// over the ranges, then 0.05 and 0.01 steps around the best. R.U.S.E.'s V35 sits after the map's response, and the fit
// (2026-11-22: 1.00 / 1.00 / 1.00 / 0.99, worst 0.0 %) found Musa's own sliders already the game's picture: the add-on
// keeps them. Checks: Musa's own (Recommended's) within 5 % and within 0.5 % of the fit's best; control: HDR (Extended)
// must be the game's grey.
// v35_fit <folder with ruse_post.cso>
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace ruse {
#include "src/games/ruse/shared.h"
}  // namespace ruse

using Microsoft::WRL::ComPtr;
using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;

constexpr UINT WIDTH = 64;   // grey, then 21 hues x 3 saturations
constexpr UINT HEIGHT = 96;  // a quarter stop a row
constexpr float NITS = 203.f;
constexpr float HUE_CLIP = 1.f;  // addon.cpp HUE_CLIP_DEFAULT 100

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11VertexShader> full_screen;
static ComPtr<ID3D11SamplerState> linear_clamp;
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
#define REQUIRE(x)                                       \
  do {                                                   \
    if (!(x)) {                                          \
      std::printf("FAIL line %d: %s\n", __LINE__, #x);   \
      std::exit(2);                                      \
    }                                                    \
  } while (false)

struct Shader {
  ComPtr<ID3D11PixelShader> pixel;
  ComPtr<ID3D11ShaderReflection> reflection;
};

static Shader LoadPixelShader(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<char> code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (code.empty()) {
    std::printf("missing %s (run.ps1 -V35 compiles it)\n", path.string().c_str());
    std::exit(2);
  }
  Shader shader;
  REQUIRE(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.pixel.GetAddressOf())));
  REQUIRE(SUCCEEDED(D3DReflect(code.data(), code.size(), IID_PPV_ARGS(shader.reflection.GetAddressOf()))));
  return shader;
}

static ComPtr<ID3D11ShaderResourceView> Texture2D(UINT width, UINT height, const std::vector<Float4>& texels) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = texels.data(), .SysMemPitch = width * 16};
  ComPtr<ID3D11Texture2D> texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf())));
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf())));
  return view;
}

using Words = std::vector<uint32_t>;
static Words F(std::initializer_list<float> values) {
  Words words;
  for (const float value : values) words.push_back(std::bit_cast<uint32_t>(value));
  return words;
}

// What a draw binds, by the names in the shader's reflection (test/tw_psychov35's): views, cbuffer variables (32-bit
// words from the variable's start) and the injection's bytes for `shader_injection`.
struct Bindings {
  std::map<std::string, ComPtr<ID3D11ShaderResourceView>> views;
  std::map<std::string, Words> words;
  std::vector<uint8_t> injection;
};

// The post over a WIDTH x HEIGHT target, its output decoded at 2.2 (Paper White 1).
static std::vector<Float3> Draw(const Shader& shader, const Bindings& bindings) {
  context->ClearState();
  D3D11_SHADER_DESC desc = {};
  shader.reflection->GetDesc(&desc);
  std::vector<ComPtr<ID3D11Buffer>> buffers;
  for (UINT i = 0; i < desc.BoundResources; ++i) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    shader.reflection->GetResourceBindingDesc(i, &bind);
    const std::string name = bind.Name;
    if (bind.Type == D3D_SIT_CBUFFER) {
      auto* constants = shader.reflection->GetConstantBufferByName(bind.Name);
      D3D11_SHADER_BUFFER_DESC constants_desc = {};
      constants->GetDesc(&constants_desc);
      std::vector<uint8_t> data((constants_desc.Size + 15u) & ~15u, 0);
      if (name == "shader_injection") {
        std::memcpy(data.data(), bindings.injection.data(), std::min(data.size(), bindings.injection.size()));
      } else {
        for (UINT v = 0; v < constants_desc.Variables; ++v) {
          D3D11_SHADER_VARIABLE_DESC variable = {};
          constants->GetVariableByIndex(v)->GetDesc(&variable);
          if (const auto found = bindings.words.find(variable.Name); found != bindings.words.end()) {
            std::memcpy(data.data() + variable.StartOffset, found->second.data(), std::min<size_t>(variable.Size, found->second.size() * 4));
          }
        }
      }
      const D3D11_BUFFER_DESC buffer_desc = {.ByteWidth = static_cast<UINT>(data.size()), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data.data()};
      ComPtr<ID3D11Buffer> buffer;
      REQUIRE(SUCCEEDED(device->CreateBuffer(&buffer_desc, &initial, buffer.GetAddressOf())));
      context->PSSetConstantBuffers(bind.BindPoint, 1, buffer.GetAddressOf());
      buffers.push_back(buffer);
    } else if (bind.Type == D3D_SIT_TEXTURE) {
      const auto found = bindings.views.find(name);
      if (found == bindings.views.end()) {
        std::printf("no view for %s\n", name.c_str());
        std::exit(2);
      }
      context->PSSetShaderResources(bind.BindPoint, 1, found->second.GetAddressOf());
    } else if (bind.Type == D3D_SIT_SAMPLER) {
      context->PSSetSamplers(bind.BindPoint, 1, linear_clamp.GetAddressOf());
    }
  }
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                            .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf())));
  ComPtr<ID3D11RenderTargetView> target_view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf())));
  const D3D11_VIEWPORT viewport = {.Width = WIDTH, .Height = HEIGHT, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  context->PSSetShader(shader.pixel.Get(), nullptr, 0);
  context->Draw(3, 0);
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf())));
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<Float3> linear(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(y) * mapped.RowPitch));
    for (UINT x = 0; x < WIDTH; ++x) {
      for (int i = 0; i < 3; ++i) linear[y * WIDTH + x][i] = std::copysign(std::pow(std::abs(row[x * 4 + i]), 2.2f), row[x * 4 + i]);
    }
  }
  context->Unmap(staging.Get(), 0);
  return linear;
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

// The map's grade (TPostProcess): Brightness * rgb * luma^(Gamma - 1) on the gamma-encoded scene.
struct RuseGrade {
  const char* name;
  float gamma;
  float brightness;
};
constexpr RuseGrade RUSE_MENU_GRADE = {"menu (Gamma 1, Brightness 1)", 1.f, 1.f};
constexpr RuseGrade RUSE_MEDIAN_GRADE = {"median map (Gamma 1.25, Brightness 1.33)", 1.25f, 1.33f};
constexpr RuseGrade RUSE_STEEPEST_GRADE = {"m03_italie HQ (Gamma 1.6, Brightness 2)", 1.6f, 2.f};

// Rendering Mode `mode`, Output scRGB, every other row at its default, PsychoV35 at Musa's own sliders.
static ruse::ShaderInjectData Injection(float mode) {
  ruse::ShaderInjectData si = {};
  si.peak_white_nits = 1000.f;
  si.diffuse_white_nits = NITS;
  si.graphics_white_nits = NITS;
  si.swap_chain_encoding = 1.f;
  si.hdr = mode;
  si.bloom = 1.f;
  si.vignette = 1.f;
  si.motion_blur = 1.f;
  si.depth_of_field = 1.f;
  si.exposure = si.highlights = si.shadows = si.contrast = si.saturation = si.cone_response_exponent = 1.f;
  si.hue_shift = 2.f;
  si.hue_clip = HUE_CLIP;
  si.v35_exposure = si.v35_highlights = si.v35_shadows = si.v35_contrast = 1.f;
  si.v35_highlight_contrast = si.v35_shadow_contrast = 1.f;
  si.v35_saturation = si.v35_highlight_saturation = 1.f;
  si.v35_roll_off = 1.f;
  si.v35_gamut_compression = 1.f;
  return si;
}

// The post, `scene`: the light the game's SDR picture shows with Gamma = Brightness = 1 (linear BT.709), kept
// gamma-encoded and divided by HDRParam.x (its highest value) as the game's 10-bit target holds it. No motion, bloom,
// vignette or tint. Linear BT.709 out in units of 203 nits (test/tw_psychov35's RunRuse).
static std::vector<Float3> RunRuse(const Shader& post, const std::vector<Float4>& scene, const RuseGrade& grade,
                                   const ruse::ShaderInjectData& si) {
  std::vector<Float4> graded;
  float highest = 1.f;
  for (const auto& s : scene) {
    Float4 g = {0.f, 0.f, 0.f, 1.f};
    for (int c = 0; c < 3; ++c) {
      g[c] = std::copysign(std::pow(std::abs(s[c]), 1.f / 2.2f), s[c]);
      highest = std::max(highest, g[c]);
    }
    graded.push_back(g);
  }
  for (auto& g : graded) {
    for (int c = 0; c < 3; ++c) g[c] /= highest;
  }
  Bindings bindings;
  bindings.views["PrincipalMap"] = Texture2D(WIDTH, HEIGHT, graded);
  bindings.views["DownSizeMap"] = Texture2D(1, 1, {Float4{0.f, 0.f, 0.f, 1.f}});
  bindings.views["ShadowMap"] = Texture2D(1, 1, {Float4{1.f, 128.f / 255.f, 128.f / 255.f, 1.f}});
  // PostProcess: NoiseLevel, OverlayColor, MinVignettage, VignettageExtent, BloomFactor, Gamma, Brightness,
  // SkyBrightness, Desaturation, DominantColor, DominantColorProp (a register each).
  bindings.words["PostProcess"] = F({0, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, grade.gamma, 0, 0, 0,
                                     grade.brightness, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  bindings.words["PrincipalMap_HDRParam"] = F({highest, 1.f / highest});
  bindings.injection.resize(sizeof(si));
  std::memcpy(bindings.injection.data(), &si, sizeof(si));
  return Draw(post, bindings);
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

static void Report(const char* name, const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  bool sdr_rises = true, v35_rises = true;
  for (UINT y = 1; y < HEIGHT; ++y) {
    sdr_rises = sdr_rises && Luminance(sdr[y * WIDTH]) >= Luminance(sdr[(y - 1) * WIDTH]) - 1e-7f;
    v35_rises = v35_rises && Luminance(v35[y * WIDTH]) >= Luminance(v35[(y - 1) * WIDTH]) - 1e-7f;
  }
  const float sdr_top = Luminance(sdr[(HEIGHT - 1) * WIDTH]) * NITS;
  CHECK(sdr_rises && v35_rises, "%s: the grey ramp falls somewhere (SDR %d, PsychoV35 %d)", name, sdr_rises, v35_rises);
  CHECK(sdr_top > 150.f, "%s: SDR's top %g nits (the ramp misses the bands)", name, sdr_top);
  const Comparison c = Compare(sdr, v35);
  std::printf("%s: PsychoV35 over Rendering Mode SDR below white, 1000 / 203 nits\n"
              "   grey where SDR is 0.05 - 1 nits x%.3f..x%.3f, 1 - 15 nits x%.3f..x%.3f, 15 - 100 nits x%.3f..x%.3f\n"
              "   colour at equal lightness, SDR 1 - 30 nits x%.3f (per colour x%.2f..x%.2f), 30 - 150 nits x%.3f (x%.2f..x%.2f)\n",
              name, c.grey[0].low, c.grey[0].high, c.grey[1].low, c.grey[1].high, c.grey[2].low, c.grey[2].high, c.chroma[0],
              c.per_colour[0].low, c.per_colour[0].high, c.chroma[1], c.per_colour[1].low, c.per_colour[1].high);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("v35_fit <folder with ruse_post.cso>\n");
    return 2;
  }
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(),
                                      nullptr, context.GetAddressOf())));
  // The post reads TEXCOORD0 (vignette), TEXCOORD1 (scene) and TEXCOORD2 (camera motion, 0 here).
  constexpr char VERTEX_SOURCE[] =
      "struct O { float2 uv0 : TEXCOORD0; float2 uv1 : TEXCOORD1; float2 motion : TEXCOORD2; float4 position : SV_Position; };"
      "O main(uint id : SV_VertexID) { O o; o.uv0 = float2((id << 1) & 2, id & 2); o.uv1 = o.uv0; o.motion = 0;"
      "o.position = float4(o.uv0 * float2(2, -2) + float2(-1, 1), 0, 1); return o; }";
  ComPtr<ID3DBlob> vertex_code;
  REQUIRE(SUCCEEDED(D3DCompile(VERTEX_SOURCE, sizeof(VERTEX_SOURCE) - 1, nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0,
                               vertex_code.GetAddressOf(), nullptr)));
  REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, full_screen.GetAddressOf())));
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxAnisotropy = 1, .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, linear_clamp.GetAddressOf())));
  const Shader post = LoadPixelShader(std::filesystem::path(argv[1]) / "ruse_post.cso");
  const auto scene = Scene(-16.f, 1.f);
  const auto draw_at = [&](const RuseGrade& grade) {
    return [&post, &scene, grade](const ruse::ShaderInjectData& si) { return RunRuse(post, scene, grade, si); };
  };

  struct Slider {
    const char* name;
    float low, high;
  };
  constexpr std::array<Slider, 4> SLIDERS = {
      {{"Contrast", 0.8f, 2.f}, {"Shadows", 0.3f, 1.5f}, {"Saturation", 0.7f, 1.3f}, {"Highlight Contrast", 0.6f, 1.2f}}};
  using Values = std::array<float, 4>;
  const auto with = [](ruse::ShaderInjectData si, const Values& values) {
    si.v35_contrast = values[0];
    si.v35_shadows = values[1];
    si.v35_saturation = values[2];
    si.v35_highlight_contrast = values[3];
    return si;
  };
  const auto grey_error = [](const Comparison& c) {
    return std::max({std::abs(c.grey[0].low - 1.f), std::abs(c.grey[0].high - 1.f), std::abs(c.grey[1].low - 1.f),
                     std::abs(c.grey[1].high - 1.f)});
  };
  const auto below_white = [&](const Comparison& c) { return std::max(grey_error(c), std::abs(c.chroma[0] - 1.f)); };
  const auto highlights = [](const std::vector<Float3>& fitted, const std::vector<Float3>& own, float paper_white, float peak) {
    float worst = 0.f;
    for (UINT y = 0; y < HEIGHT; ++y) {
      const float own_nits = Luminance(own[y * WIDTH]) * paper_white;
      if (own_nits < 100.f || own_nits > 0.95f * peak) continue;
      worst = std::max(worst, std::abs((Luminance(fitted[y * WIDTH]) / Luminance(own[y * WIDTH])) - 1.f));
    }
    return worst;
  };
  struct Fit {
    Values values = {1.f, 1.f, 1.f, 1.f};
    float score = 1e9f;
    int candidates = 0;
  };
  const auto own = Injection(3.f);
  const auto sdr_mode = Injection(0.f);

  const auto draw = draw_at(RUSE_MEDIAN_GRADE);
  const auto sdr = draw(sdr_mode);
  const auto own_ramp = draw(own);
  std::printf("R.U.S.E. V35 fit, %s, Hue Clip 100:\n", RUSE_MEDIAN_GRADE.name);
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
        if (value >= SLIDERS[d].low - 1e-4f && value <= SLIDERS[d].high + 1e-4f) {
          grid[d].push_back(value);
        }
      }
    }
    for (const float contrast : grid[0]) {
      for (const float shadows : grid[1]) {
        for (const float saturation : grid[2]) {
          for (const float highlight_contrast : grid[3]) {
            const Values values = {contrast, shadows, saturation, highlight_contrast};
            const auto ramp = draw(with(own, values));
            ++best.candidates;
            if (highlights(ramp, own_ramp, NITS, 1000.f) > 0.1f) continue;
            if (const float score = below_white(Compare(sdr, ramp)); score < best.score) {
              best.values = values;
              best.score = score;
            }
          }
        }
      }
    }
  }
  std::printf("   minimax fit (%s %.1f - %.1f, %s %.1f - %.1f, %s %.1f - %.1f, %s %.1f - %.1f): %.2f / %.2f / %.2f / %.2f, "
              "worst %.1f %% (%d candidates, %.0f s)\n",
              SLIDERS[0].name, SLIDERS[0].low, SLIDERS[0].high, SLIDERS[1].name, SLIDERS[1].low, SLIDERS[1].high, SLIDERS[2].name,
              SLIDERS[2].low, SLIDERS[2].high, SLIDERS[3].name, SLIDERS[3].low, SLIDERS[3].high, best.values[0], best.values[1],
              best.values[2], best.values[3], 100.f * best.score, best.candidates,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());

  const float own_score = below_white(Compare(sdr, own_ramp));
  std::printf("   Musa's own sliders (the add-on's defaults): worst %.1f %%\n", 100.f * own_score);
  CHECK(own_score < 0.05f, "R.U.S.E.'s V35 defaults miss the game by %.1f %%", 100.f * own_score);
  CHECK(own_score <= best.score + 0.005f, "R.U.S.E.'s V35 defaults (%.1f %%) are not the fit's best (%.1f %%)", 100.f * own_score,
        100.f * best.score);
  Report("R.U.S.E. Musa's own sliders, median map", sdr, own_ramp);
  // The measure's own check: Extended is the SDR picture up to Paper White (every grey band here), so a wrong decode,
  // white or binding would show.
  const Comparison extended_comparison = Compare(sdr, draw(Injection(4.f)));
  const float extended_grey = grey_error(extended_comparison);
  std::printf("   control, HDR (Extended): grey %.2f %% off, chroma x%.3f (must be the game's grey)\n", 100.f * extended_grey,
              extended_comparison.chroma[0]);
  CHECK(extended_grey < 0.005f, "HDR (Extended) is %.2f %% off the game's grey below Paper White", 100.f * extended_grey);
  // The other grades (V35 sits after the map's response: the same defaults for every map).
  for (const RuseGrade& grade : {RUSE_MENU_GRADE, RUSE_STEEPEST_GRADE}) {
    const auto draw_grade = draw_at(grade);
    const auto sdr_grade = draw_grade(sdr_mode);
    Report(("R.U.S.E. Musa's own sliders, " + std::string(grade.name)).c_str(), sdr_grade, draw_grade(own));
  }
  std::printf(failures == 0 ? "PASS\n" : "FAILED: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
