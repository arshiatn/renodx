// Directional Light Bounce (S1a: twattila/bitmask_sh.hlsli and the light_bounce_*.hlsli passes): shg_ design section 8's
// V1, items 1-6, on WARP. Attila's passes built as its light_bounce.hlsli builds them with Directional Light Bounce
// (LIGHT_BOUNCE_DIRECTIONAL: "on" below), drawn as light_bounce.hpp draws them, on a synthetic G-buffer and camera (view
// space = world space, the camera at the origin looking along +z; depth buffer value 1 / z):
//   1 row off: every add-on's built Light Bounce shaders against the builds before (their code; dcl_temps before / after
//     and with the define); Attila's chain with the define and the row off against the build before: every target's
//     bits; the row on: they differ (CONTROL).
//   2 a flat room (a lit side wall, a floor, a back wall) through prepare -> levels -> trace -> resolve -> denoise ->
//     apply at Quarter: on = off within ~1e-3 where each pixel's block lies on one plane.
//   3 the uniform sky injected (c0 0.5, c1 n0 / 2, Y0 1): the apply's light at normals tilted 15-75 degrees off the
//     texel's = (1 + cos t) / 2.
//   4 the chain check (S0 12.2's patch): normal maps m 0.15 / 0.35 / 0.6, blocks b = 2 / 4 / 8, 4 named + 24 random
//     fields; each texel's exact moments at its normal (the test's own quadrature) injected where the trace writes
//     them, the resolve (32 static frames) -> denoiser (off; beta 0 / 0.5 / 1) -> apply against the exact per-pixel F:
//     mean shift, error, detail, beside the scalar (the row off); the helper's pinned form (A 0, g 1.2 m1) after a
//     wide filter and its rewrite (NRD and rt_nrd_finish's stand-in), denoiser off (B2 / B4's chain). Picks beta at
//     b = 4, m 0.35 (the largest within 1%; none: 0, numbers printed as look-risk).
//   5 the rewrite last: a history with another g and Y: after the resolve and after the denoiser Y(n0) = Y0.
//   6 bumpy normals (Full scalar vs Quarter scalar and Quarter + SH, printed); guards: rgb0 0, huge g, back-facing pixel
//     normals, grazing light (the gain cap), a thin pole in front of the lit wall (the trace exact at n0); the _ms
//     prepare and apply on a 4x G-buffer.
// tests.exe <repository> <before root> [items] (the before root: each add-on's shaders that include Light Bounce files
// built before, <root>\<game>\*.cso; items: which to run, e.g. 24, all by default)
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "../../src/games/twattila/light_bounce.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr double PI = std::numbers::pi;
constexpr UINT SAMPLES = 4;          // the _ms G-buffer's
constexpr double FOV_TAN = 0.57735;  // tan(30 degrees): a 60-degree vertical field of view

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
std::string repository;
std::string before_root;
int failures = 0;

void Verdict(bool pass, const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::vprintf(format, args);
  va_end(args);
  std::printf("  %s\n", pass ? "PASS" : "FAIL");
  failures += pass ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Vectors.

struct V3 {
  double x = 0;
  double y = 0;
  double z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double Dot(V3 a, V3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }
V3 Unit(V3 a) {
  const double length = std::sqrt(Dot(a, a));
  return length > 1e-30 ? a * (1 / length) : a;
}
// The fields' frame (z the plane's normal toward the camera, y down the rows) to view space and back (its own inverse).
V3 ToView(V3 a) { return {a.x, -a.y, -a.z}; }
double Luminance(const std::array<double, 3>& rgb) { return (0.25 * rgb[0]) + (0.5 * rgb[1]) + (0.25 * rgb[2]); }
double Luminance(const std::array<float, 4>& rgb) { return (0.25 * rgb[0]) + (0.5 * rgb[1]) + (0.25 * rgb[2]); }

// ---------------------------------------------------------------------------------------------------------------------
// Files and shaders.

std::vector<char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Includes relative to the including file, as fxc (the standard handler takes the first file's folder for all).
class Includes final : public ID3DInclude {
 public:
  explicit Includes(std::string folder) : root_(std::move(folder)) {}

  HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*type*/, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override {
    std::string folder = root_;
    for (const auto& [text, path] : opened_) {
      if (text->data() == parent) folder = path;
    }
    const std::string path = folder + "\\" + name;
    auto text = std::make_unique<std::vector<char>>(ReadFile(path));
    if (text->empty()) return E_FAIL;
    *data = text->data();
    *bytes = static_cast<UINT>(text->size());
    opened_.emplace_back(std::move(text), path.substr(0, path.find_last_of("\\/")));
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID /*data*/) override { return S_OK; }

 private:
  std::string root_;
  std::vector<std::pair<std::unique_ptr<std::vector<char>>, std::string>> opened_;
};

using Defines = std::vector<std::pair<std::string, std::string>>;

// As the build compiles it (fxc /O3 /Gec), includes from `folder`.
std::vector<char> Compile(const std::string& text, const std::string& name, const std::string& folder, const Defines& defines,
                          const char* profile) {
  std::vector<D3D_SHADER_MACRO> macros = {{"__SHADER_TARGET_MAJOR", "5"}, {"__SHADER_TARGET_MINOR", "0"}};
  for (const auto& [key, value] : defines) macros.push_back({key.c_str(), value.c_str()});
  macros.push_back({nullptr, nullptr});
  Includes includes(folder);
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(text.data(), text.size(), name.c_str(), macros.data(), &includes, "main", profile,
                        D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, 0, &blob, &errors))) {
    std::printf("  %s: not compiled: %s\n", name.c_str(), errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return {};
  }
  const auto* bytes = static_cast<const char*>(blob->GetBufferPointer());
  return {bytes, bytes + blob->GetBufferSize()};
}

std::vector<char> CompileFile(const std::string& path, const Defines& defines, const char* profile) {
  const auto text = ReadFile(path);
  return Compile(std::string(text.begin(), text.end()), path, path.substr(0, path.find_last_of("\\/")), defines, profile);
}

// A DXBC container's chunk (SHEX / SHDR: the code), empty if none.
std::string Chunk(const std::vector<char>& data, const char* fourcc) {
  const char* bytes = data.data();
  const size_t size = data.size();
  if (size < 32 || std::memcmp(bytes, "DXBC", 4) != 0) return {};
  uint32_t count = 0;
  std::memcpy(&count, bytes + 28, 4);
  for (uint32_t i = 0; i < count && 32 + (i * 4) + 4 <= size; ++i) {
    uint32_t offset = 0;
    std::memcpy(&offset, bytes + 32 + (i * 4), 4);
    if (offset + 8 > size) break;
    uint32_t length = 0;
    std::memcpy(&length, bytes + offset + 4, 4);
    if (std::memcmp(bytes + offset, fourcc, 4) == 0 && offset + 8 + length <= size) return {bytes + offset + 8, length};
  }
  return {};
}

std::string Code(const std::vector<char>& data) {
  std::string code = Chunk(data, "SHEX");
  return code.empty() ? Chunk(data, "SHDR") : code;
}

// Its temporary registers (dcl_temps; -1: not disassembled).
int Temps(const std::vector<char>& data) {
  ComPtr<ID3DBlob> text;
  if (data.empty() || FAILED(D3DDisassemble(data.data(), data.size(), 0, nullptr, &text))) return -1;
  const std::string listing(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize());
  const size_t at = listing.find("dcl_temps ");
  return (at == std::string::npos) ? 0 : std::atoi(listing.c_str() + at + 10);
}

ComPtr<ID3D11PixelShader> PixelShader(const std::vector<char>& code) {
  ComPtr<ID3D11PixelShader> shader;
  if (code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader))) return nullptr;
  return shader;
}

// ---------------------------------------------------------------------------------------------------------------------
// Textures, constants, draws.

float HalfToFloat(uint16_t half) {
  const uint32_t exponent = (half >> 10) & 31u;
  const uint32_t mantissa = half & 1023u;
  float value = 0.f;
  if (exponent == 0) {
    value = std::ldexp(static_cast<float>(mantissa), -24);
  } else if (exponent == 31) {
    value = (mantissa != 0) ? NAN : INFINITY;
  } else {
    value = std::ldexp(static_cast<float>(mantissa | 1024u), static_cast<int>(exponent) - 25);
  }
  return ((half >> 15) != 0) ? -value : value;
}

UINT TexelBytes(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:        return 8;
    case DXGI_FORMAT_R16_FLOAT:          return 2;
    default:                             return 4;  // R11G11B10_FLOAT, R32_FLOAT
  }
}

struct Texture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;                     // every level
  std::vector<ComPtr<ID3D11RenderTargetView>> rtvs;          // per level
  std::vector<ComPtr<ID3D11ShaderResourceView>> level_srvs;  // per level
  UINT width = 0;
  UINT height = 0;
  UINT samples = 1;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

Texture Make(UINT width, UINT height, DXGI_FORMAT format, UINT levels = 1, UINT samples = 1, const void* data = nullptr) {
  Texture made = {.width = width, .height = height, .samples = samples, .format = format};
  const D3D11_TEXTURE2D_DESC desc = {width, height, levels, 1, format, {samples, 0}, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
  const D3D11_SUBRESOURCE_DATA init = {data, width * TexelBytes(format), 0};
  if (FAILED(device->CreateTexture2D(&desc, (data != nullptr) ? &init : nullptr, &made.texture))
      || FAILED(device->CreateShaderResourceView(made.texture.Get(), nullptr, &made.srv))) {
    std::printf("texture %u x %u format %d not made\n", width, height, format);
    std::exit(4);
  }
  for (UINT level = 0; level < levels; ++level) {
    D3D11_RENDER_TARGET_VIEW_DESC target = {.Format = format};
    D3D11_SHADER_RESOURCE_VIEW_DESC view = {.Format = format};
    if (samples > 1) {
      target.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
      view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    } else {
      target.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      target.Texture2D.MipSlice = level;
      view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      view.Texture2D = {level, 1};
    }
    made.rtvs.emplace_back();
    made.level_srvs.emplace_back();
    device->CreateRenderTargetView(made.texture.Get(), &target, &made.rtvs.back());
    device->CreateShaderResourceView(made.texture.Get(), &view, &made.level_srvs.back());
    constexpr float ZERO[4] = {};
    if (data == nullptr) context->ClearRenderTargetView(made.rtvs.back().Get(), ZERO);
  }
  return made;
}

// A level's texels, 4 words each: floats (16-bit ones widened), integers as they are; other formats' bytes in the
// first. Multisampled ones resolved first.
std::vector<std::array<uint32_t, 4>> ReadBack(const Texture& from, UINT level = 0) {
  ComPtr<ID3D11Texture2D> source = from.texture;
  UINT subresource = level;
  if (from.samples > 1) {
    D3D11_TEXTURE2D_DESC desc = {};
    from.texture->GetDesc(&desc);
    desc.SampleDesc = {1, 0};
    ComPtr<ID3D11Texture2D> resolved;
    device->CreateTexture2D(&desc, nullptr, &resolved);
    context->ResolveSubresource(resolved.Get(), 0, from.texture.Get(), 0, from.format);
    source = resolved;
    subresource = 0;
  }
  const UINT width = std::max(from.width >> level, 1u);
  const UINT height = std::max(from.height >> level, 1u);
  const D3D11_TEXTURE2D_DESC desc = {width, height, 1, 1, from.format, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, &staging);
  context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source.Get(), subresource, nullptr);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  const UINT bytes = TexelBytes(from.format);
  const bool halves = (from.format == DXGI_FORMAT_R16G16B16A16_FLOAT || from.format == DXGI_FORMAT_R16_FLOAT);
  std::vector<std::array<uint32_t, 4>> texels(size_t{width} * height);
  for (UINT y = 0; y < height; ++y) {
    const auto* row = static_cast<const uint8_t*>(mapped.pData) + (size_t{y} * mapped.RowPitch);
    for (UINT x = 0; x < width; ++x) {
      auto& texel = texels[(size_t{y} * width) + x];
      const uint8_t* at = row + (size_t{x} * bytes);
      if (halves) {
        for (UINT c = 0; c < bytes / 2; ++c) {
          uint16_t half = 0;
          std::memcpy(&half, at + (c * 2), 2);
          texel[c] = std::bit_cast<uint32_t>(HalfToFloat(half));
        }
      } else {
        std::memcpy(texel.data(), at, bytes);
      }
    }
  }
  context->Unmap(staging.Get(), 0);
  return texels;
}

std::vector<std::array<float, 4>> ReadFloats(const Texture& from) {
  std::vector<std::array<float, 4>> floats;
  for (const auto& texel : ReadBack(from)) {
    floats.push_back({std::bit_cast<float>(texel[0]), std::bit_cast<float>(texel[1]), std::bit_cast<float>(texel[2]),
                      std::bit_cast<float>(texel[3])});
  }
  return floats;
}

size_t Differing(const Texture& left, const Texture& right, UINT level = 0) {
  const auto a = ReadBack(left, level);
  const auto b = ReadBack(right, level);
  size_t count = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) count += (a[i] != b[i]) ? 1 : 0;
  return count + ((a.size() != b.size()) ? 1 : 0);
}

ComPtr<ID3D11VertexShader> full_screen;
std::array<ComPtr<ID3D11Buffer>, 13> constant_buffers;  // b0 the test's inject, b10 / b11 the cameras, b12 LB

void SetConstants(UINT slot, const void* data, UINT bytes) {
  auto& buffer = constant_buffers[slot];
  const UINT size = (bytes + 15) & ~15u;
  if (!buffer) {
    const D3D11_BUFFER_DESC desc = {size, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    device->CreateBuffer(&desc, nullptr, &buffer);
  }
  std::vector<uint8_t> padded(size);
  std::memcpy(padded.data(), data, bytes);
  context->UpdateSubresource(buffer.Get(), 0, nullptr, padded.data(), 0, 0);
}

using Views = std::vector<std::pair<UINT, ID3D11ShaderResourceView*>>;

// A full-screen triangle into `targets` (no blend), every other view slot empty.
void Draw(ID3D11PixelShader* shader, const std::vector<ID3D11RenderTargetView*>& targets, const Views& views, UINT width, UINT height) {
  std::array<ID3D11ShaderResourceView*, 64> none = {};
  context->PSSetShaderResources(0, static_cast<UINT>(none.size()), none.data());
  context->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), nullptr);
  for (const auto& [slot, view] : views) context->PSSetShaderResources(slot, 1, &view);
  std::array<ID3D11Buffer*, 13> buffers = {};
  for (size_t i = 0; i < buffers.size(); ++i) buffers[i] = constant_buffers[i].Get();
  context->PSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
  const D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
  context->RSSetViewports(1, &viewport);
  context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->Draw(3, 0);
  context->OMSetRenderTargets(0, nullptr, nullptr);
}

// ---------------------------------------------------------------------------------------------------------------------
// The camera (light_bounce.hlsli's camera_VS_PS; the last frame's the same: nothing moves) and the scenes.

struct Camera {
  UINT width = 0;
  UINT height = 0;
  double a = 0;  // tan of half the horizontal field of view
  double b = 0;  // of the vertical one
};

void SetCamera(const Camera& camera) {
  std::array<float, 120> c = {};
  const auto set = [&](int index, double x, double y, double z, double w) {
    c[index * 4] = static_cast<float>(x), c[(index * 4) + 1] = static_cast<float>(y);
    c[(index * 4) + 2] = static_cast<float>(z), c[(index * 4) + 3] = static_cast<float>(w);
  };
  // Each matrix as its columns (HLSL's column-major packing): view and inv_view the identity; projection's m11 1 / b;
  // view_projection (x / a, y / b, 1, z); inv_projection (ndc x a, ndc y b, 1, depth): position = that / depth.
  for (int base : {1, 13}) {
    set(base, 1, 0, 0, 0), set(base + 1, 0, 1, 0, 0), set(base + 2, 0, 0, 1, 0), set(base + 3, 0, 0, 0, 1);
  }
  for (int base : {5, 9}) {
    set(base, 1 / camera.a, 0, 0, 0), set(base + 1, 0, 1 / camera.b, 0, 0), set(base + 2, 0, 0, 0, 1), set(base + 3, 0, 0, 1, 0);
  }
  set(17, camera.a, 0, 0, 0), set(18, 0, camera.b, 0, 0), set(19, 0, 0, 0, 1), set(20, 0, 0, 1, 0);
  const double width = camera.width;
  const double height = camera.height;
  set(27, width, height, 1 / width, 1 / height);
  set(29, width, height, 1 / width, 1 / height);
  SetConstants(10, c.data(), sizeof(c));
  SetConstants(11, c.data(), sizeof(c));
}

// A pixel's view-space position at a depth buffer value (light_bounce.hlsli BounceViewPositionAt).
V3 Position(const Camera& camera, double x, double y, double depth) {
  const double ndc_x = ((x + 0.5) / camera.width * 2) - 1;
  const double ndc_y = 1 - ((y + 0.5) / camera.height * 2);
  return V3{camera.a * ndc_x, camera.b * ndc_y, 1} * (1 / depth);
}

V3 Facing(V3 normal, V3 position) { return (Dot(normal, position) > 0) ? normal * -1 : normal; }

// The prepared geometry's normal (light_bounce_prepare.hlsli's octahedron; light_bounce_geometry.hlsli's decode).
uint32_t EncodeNormal(V3 n) {
  const double sum = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
  double x = n.x / sum;
  double y = n.y / sum;
  if (n.z < 0) {
    const double folded_x = (1 - std::abs(y)) * ((x >= 0) ? 1 : -1);
    y = (1 - std::abs(x)) * ((y >= 0) ? 1 : -1);
    x = folded_x;
  }
  const auto encode = [](double v) { return static_cast<uint32_t>(std::round(std::clamp((v * 0.5) + 0.5, 0.0, 1.0) * 65535)); };
  return encode(x) | (encode(y) << 16);
}

V3 DecodeNormal(uint32_t packed) {
  V3 n = {(static_cast<float>(packed & 0xFFFFu) / 65535.f * 2.f) - 1.f, (static_cast<float>(packed >> 16) / 65535.f * 2.f) - 1.f, 0};
  n.z = 1 - std::abs(n.x) - std::abs(n.y);
  const double fold = std::clamp(-n.z, 0.0, 1.0);
  n.x += (n.x >= 0) ? -fold : fold;
  n.y += (n.y >= 0) ? -fold : fold;
  return Unit(n);
}

// A normal map (S0's normal_map: Gaussian heights smoothed with sigma `correlation` px, wrapped; their slopes scaled to
// RMS `strength`), in the fields' frame, per pixel.
std::vector<V3> NormalMap(uint64_t seed, UINT width, UINT height, double strength, double correlation) {
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> normal(0, 1);
  std::vector<double> heights(size_t{width} * height);
  for (double& h : heights) h = normal(rng);
  const int radius = static_cast<int>(std::ceil(4 * correlation));
  std::vector<double> smooth(heights.size());
  double kernel_sum = 0;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) kernel_sum += std::exp(-((dx * dx) + (dy * dy)) / (2 * correlation * correlation));
  }
  const int w = static_cast<int>(width);
  const int h = static_cast<int>(height);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      double sum = 0;
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          sum += heights[(size_t(((y + dy) % h + h) % h) * width) + size_t(((x + dx) % w + w) % w)]
                 * std::exp(-((dx * dx) + (dy * dy)) / (2 * correlation * correlation));
        }
      }
      smooth[(size_t(y) * width) + x] = sum / kernel_sum;
    }
  }
  const auto at = [&](int x, int y) { return smooth[(size_t(y) * width) + x]; };
  std::vector<double> gx(smooth.size());
  std::vector<double> gy(smooth.size());
  double squares = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const size_t i = (size_t(y) * width) + x;
      gx[i] = (x == 0) ? at(1, y) - at(0, y) : ((x == w - 1) ? at(x, y) - at(x - 1, y) : 0.5 * (at(x + 1, y) - at(x - 1, y)));
      gy[i] = (y == 0) ? at(x, 1) - at(x, 0) : ((y == h - 1) ? at(x, y) - at(x, y - 1) : 0.5 * (at(x, y + 1) - at(x, y - 1)));
      squares += (gx[i] * gx[i]) + (gy[i] * gy[i]);
    }
  }
  const double scale = strength / std::sqrt(squares / static_cast<double>(smooth.size()));
  std::vector<V3> normals(smooth.size());
  for (size_t i = 0; i < normals.size(); ++i) normals[i] = Unit({-scale * gx[i], -scale * gy[i], 1});
  return normals;
}

// Attila's G-buffer as its sun light draw binds it (light_bounce.hlsli): per pixel the light, the world (= view) normal
// x 0.5 + 0.5, the albedo (gamma 2, metalness 0), the depth buffer value; which surface each pixel shows.
struct Scene {
  Camera camera;
  std::vector<std::array<float, 4>> light;
  std::vector<std::array<float, 4>> normal;
  std::vector<std::array<float, 4>> colour;
  std::vector<std::array<float, 4>> depth;
  std::vector<int> surface;
};

void SetPixel(Scene* scene, size_t i, V3 normal, const std::array<float, 3>& light, float albedo, double z, int surface) {
  scene->light[i] = {light[0], light[1], light[2], 1.f};
  scene->normal[i] = {static_cast<float>((normal.x * 0.5) + 0.5), static_cast<float>((normal.y * 0.5) + 0.5),
                      static_cast<float>((normal.z * 0.5) + 0.5), 0.f};
  const float gamma = std::sqrt(albedo);
  scene->colour[i] = {gamma, gamma, gamma, 0.f};
  scene->depth[i] = {static_cast<float>(1 / z), 0.f, 0.f, 0.f};
  scene->surface[i] = surface;
}

Scene Blank(UINT width, UINT height) {
  Scene scene;
  scene.camera = {width, height, FOV_TAN * width / height, FOV_TAN};
  const size_t count = size_t{width} * height;
  scene.light.resize(count), scene.normal.resize(count), scene.colour.resize(count), scene.depth.resize(count);
  scene.surface.resize(count);
  return scene;
}

// A room: the back wall (z 6), a lit side wall on the left (x -1), a floor (y -1.2); `bump`: the back wall and the floor
// normal-mapped; `pole`: a thin dark pole in front of the side wall (a gap in the lit wall's fresh sectors).
Scene Room(UINT width, UINT height, double bump, bool pole) {
  Scene scene = Blank(width, height);
  const auto bumps = NormalMap(77, width, height, std::max(bump, 1e-6), 1.5);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const V3 ray = Position(scene.camera, x, y, 1);
      double nearest = 6;
      int surface = 0;
      if (ray.x < 0 && -1 / ray.x < nearest && ray.y * (-1 / ray.x) >= -1.2) nearest = -1 / ray.x, surface = 1;
      if (ray.y < 0 && -1.2 / ray.y < nearest && ray.x * (-1.2 / ray.y) >= -1) nearest = -1.2 / ray.y, surface = 2;
      if (pole && 3.9 < nearest && ray.x * 3.9 >= -0.75 && ray.x * 3.9 <= -0.65 && ray.y * 3.9 >= -1.2 && ray.y * 3.9 <= 0.8) {
        nearest = 3.9, surface = 3;
      }
      const size_t i = (size_t{y} * width) + x;
      const V3 b = bumps[i];
      switch (surface) {
        case 0:  SetPixel(&scene, i, (bump > 0) ? ToView(b) : V3{0, 0, -1}, {0.08f, 0.1f, 0.12f}, 0.7f, nearest, 0); break;
        case 1:  SetPixel(&scene, i, {1, 0, 0}, {2.f, 1.2f, 0.6f}, 0.7f, nearest, 1); break;
        case 2:  SetPixel(&scene, i, (bump > 0) ? V3{b.x, b.z, b.y} : V3{0, 1, 0}, {0.25f, 0.25f, 0.25f}, 0.7f, nearest, 2); break;
        default: SetPixel(&scene, i, {0, 0, -1}, {0.02f, 0.02f, 0.02f}, 0.7f, nearest, 3); break;
      }
    }
  }
  return scene;
}

// A plane facing the camera (z 4) with each pixel's normal from `normals` (the fields' frame); white, unlit.
Scene Patch(UINT size, const std::vector<V3>& normals) {
  Scene scene = Blank(size, size);
  for (size_t i = 0; i < normals.size(); ++i) SetPixel(&scene, i, ToView(normals[i]), {0.f, 0.f, 0.f}, 1.f, 4, 0);
  return scene;
}

// ---------------------------------------------------------------------------------------------------------------------
// The passes as light_bounce.hpp draws them.

struct Passes {
  ComPtr<ID3D11PixelShader> prepare, prepare_ms, levels, trace, resolve, denoise, apply, apply_ms, apply_ms_pixel;
};

ComPtr<ID3D11PixelShader> inject;  // the test's trace stand-in (INJECT)
ComPtr<ID3D11PixelShader> copy;    // a texture into a multisampled target

// The test's trace: each texel's light and moments (t0, t1) stored as the trace stores them (BitmaskShFromMoments at
// the texel's normal: mode 0 ZH3 with the memo's step, 1 without; mode 2: t1.xyz a g pinned with A 0, the helper's).
constexpr const char* INJECT = R"(
#include "./light_bounce.hlsli"
#include "./bitmask_sh.hlsli"
#include "./light_bounce_geometry.hlsli"
Texture2D<float4> t_inject_light : register(t0);
Texture2D<float4> t_inject_moments : register(t1);
cbuffer inject : register(b0) { float4 inject_mode; }
void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0, out float4 o1 : SV_Target1, out float o2 : SV_Target2) {
  const int2 texel = int2(v0.xy);
  const BounceGeometry geometry = LoadGeometry(texel, 0u, 0u);
  const float distance = length(geometry.position);
  const float3 normal = BitmaskShFacing(geometry.normal, -geometry.position / distance);
  const float3 light = t_inject_light.Load(int3(texel, 0)).rgb;
  const float4 moments = t_inject_moments.Load(int3(texel, 0));
  BitmaskShLight stored;
  if (inject_mode.x < 1.5f) {
    stored = BitmaskShFromMoments(dot(light, BITMASK_SH_LUMINANCE), moments.w, moments.xyz, normal, inject_mode.x < 0.5f);
  } else {
    stored = BitmaskShPinned(moments.xyz, 0.f, normal);
  }
  o0 = float4(light, distance);
  o1 = float4(stored.g, stored.w);
  o2 = stored.zonal;
}
)";

constexpr const char* COPY = R"(
Texture2D<float4> t_source : register(t0);
float4 main(float4 v0 : SV_Position0) : SV_Target0 { return t_source.Load(int3(v0.xy, 0)); }
)";

struct Chain {
  Camera camera;
  UINT scale = 1;
  UINT trace_width = 0;
  UINT trace_height = 0;
  UINT levels = 1;
  Texture light, normal, colour, depth, normal_ms, colour_ms, depth_ms;
  Texture prepared_light, prepared_geometry;
  Texture traced, traced_sh, traced_zonal;
  std::array<Texture, 2> history, frames, history_sh, history_zonal;
  Texture denoised, denoised_sh, denoised_zonal;
  Texture output, output_ms;
  size_t index = 0;
  LightBounceConstants lb = {};
  bool sh = false;  // Directional Light Bounce's targets bound (the row on)
};

Texture Multisampled(const Texture& single) {
  Texture made = Make(single.width, single.height, single.format, 1, SAMPLES);
  Draw(copy.Get(), {made.rtvs[0].Get()}, {{0, single.srv.Get()}}, single.width, single.height);
  return made;
}

void ResetHistory(Chain* chain) {
  constexpr float ZERO[4] = {};
  for (size_t i = 0; i < 2; ++i) {
    for (const Texture* target : {&chain->history[i], &chain->frames[i], &chain->history_sh[i], &chain->history_zonal[i]}) {
      context->ClearRenderTargetView(target->rtvs[0].Get(), ZERO);
    }
  }
  chain->index = 0;
}

Chain MakeChain(const Scene& scene, UINT scale) {
  Chain chain = {.camera = scene.camera, .scale = scale};
  const UINT width = scene.camera.width;
  const UINT height = scene.camera.height;
  chain.trace_width = (width + scale - 1) / scale;
  chain.trace_height = (height + scale - 1) / scale;
  chain.levels = std::min<UINT>(6, std::bit_width(std::max(chain.trace_width, chain.trace_height)));
  constexpr DXGI_FORMAT RGBA32 = DXGI_FORMAT_R32G32B32A32_FLOAT;
  constexpr DXGI_FORMAT RGBA16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
  constexpr DXGI_FORMAT R16 = DXGI_FORMAT_R16_FLOAT;
  chain.light = Make(width, height, RGBA32, 1, 1, scene.light.data());
  chain.normal = Make(width, height, RGBA32, 1, 1, scene.normal.data());
  chain.colour = Make(width, height, RGBA32, 1, 1, scene.colour.data());
  chain.depth = Make(width, height, RGBA32, 1, 1, scene.depth.data());
  chain.normal_ms = Multisampled(chain.normal);
  chain.colour_ms = Multisampled(chain.colour);
  chain.depth_ms = Multisampled(chain.depth);
  const UINT tw = chain.trace_width;
  const UINT th = chain.trace_height;
  chain.prepared_light = Make(tw, th, DXGI_FORMAT_R11G11B10_FLOAT, chain.levels);
  chain.prepared_geometry = Make(tw, th, DXGI_FORMAT_R32G32_UINT, chain.levels);
  chain.traced = Make(tw, th, RGBA16);
  chain.traced_sh = Make(tw, th, RGBA16);
  chain.traced_zonal = Make(tw, th, R16);
  for (size_t i = 0; i < 2; ++i) {
    chain.history[i] = Make(tw, th, RGBA16);
    chain.frames[i] = Make(tw, th, R16);
    chain.history_sh[i] = Make(tw, th, RGBA16);
    chain.history_zonal[i] = Make(tw, th, R16);
  }
  chain.denoised = Make(tw, th, RGBA16);
  chain.denoised_sh = Make(tw, th, RGBA16);
  chain.denoised_zonal = Make(tw, th, R16);
  chain.output = Make(width, height, RGBA32);
  chain.output_ms = Make(width, height, RGBA32, 1, SAMPLES);
  chain.lb = {
      .strength = 1.f,
      .radius = 3.f,
      .thickness = 0.15f,
      .steps = 8.f,
      .slices = 2.f,
      .scale = static_cast<float>(scale),
      .history = 0.95f,
      .auto_thickness = 1.f,
      .region_width = static_cast<float>(tw),
      .region_height = static_cast<float>(th),
      .level0_width = static_cast<float>(tw),
      .transport = 0.f,  // light_bounce.hpp's DIRECTIONAL_TRANSPORT
  };
  return chain;
}

Views GBuffer(const Chain& chain, bool msaa) {
  return {{42, (msaa ? chain.normal_ms : chain.normal).srv.Get()},
          {43, (msaa ? chain.colour_ms : chain.colour).srv.Get()},
          {45, (msaa ? chain.depth_ms : chain.depth).srv.Get()}};
}

void Use(const Chain& chain) {
  SetCamera(chain.camera);
  SetConstants(12, &chain.lb, sizeof(chain.lb));
}

void PrepareStep(const Chain& chain, const Passes& passes, bool msaa) {
  Use(chain);
  Views views = GBuffer(chain, msaa);
  views.insert(views.end(), {{40, chain.light.srv.Get()}, {41, chain.history[chain.index].srv.Get()}});
  Draw((msaa ? passes.prepare_ms : passes.prepare).Get(), {chain.prepared_light.rtvs[0].Get(), chain.prepared_geometry.rtvs[0].Get()},
       views, chain.trace_width, chain.trace_height);
  for (UINT level = 1; level < chain.levels; ++level) {
    Draw(passes.levels.Get(), {chain.prepared_light.rtvs[level].Get(), chain.prepared_geometry.rtvs[level].Get()},
         {{46, chain.prepared_light.level_srvs[level - 1].Get()}, {47, chain.prepared_geometry.level_srvs[level - 1].Get()}},
         std::max(chain.trace_width >> level, 1u), std::max(chain.trace_height >> level, 1u));
  }
}

// The trace (`stand_in`: the test's INJECT with its `inputs` at t0-t1) and the resolve.
void TraceStep(Chain* chain, const Passes& passes, ID3D11PixelShader* stand_in = nullptr, const Views& inputs = {}) {
  Use(*chain);
  const size_t previous = chain->index;
  chain->index ^= 1;
  const size_t current = chain->index;
  Views views = GBuffer(*chain, false);
  views.insert(views.end(), {{41, chain->history[previous].srv.Get()}, {46, chain->prepared_light.srv.Get()},
                             {47, chain->prepared_geometry.srv.Get()}});
  std::vector<ID3D11RenderTargetView*> targets = {chain->traced.rtvs[0].Get()};
  if (chain->sh) {
    views.insert(views.end(), {{58, chain->history_sh[previous].srv.Get()}, {59, chain->history_zonal[previous].srv.Get()}});
    targets.insert(targets.end(), {chain->traced_sh.rtvs[0].Get(), chain->traced_zonal.rtvs[0].Get()});
  }
  if (stand_in != nullptr) views.insert(views.end(), inputs.begin(), inputs.end());
  Draw((stand_in != nullptr) ? stand_in : passes.trace.Get(), targets, views, chain->trace_width, chain->trace_height);

  views = GBuffer(*chain, false);
  views.insert(views.end(), {{40, chain->traced.srv.Get()}, {41, chain->history[previous].srv.Get()},
                             {46, chain->prepared_light.srv.Get()}, {47, chain->prepared_geometry.srv.Get()},
                             {48, chain->frames[previous].srv.Get()}});
  targets = {chain->history[current].rtvs[0].Get(), chain->frames[current].rtvs[0].Get()};
  if (chain->sh) {
    views.insert(views.end(), {{56, chain->traced_sh.srv.Get()}, {57, chain->traced_zonal.srv.Get()},
                               {58, chain->history_sh[previous].srv.Get()}, {59, chain->history_zonal[previous].srv.Get()}});
    targets.insert(targets.end(), {chain->history_sh[current].rtvs[0].Get(), chain->history_zonal[current].rtvs[0].Get()});
  }
  Draw(passes.resolve.Get(), targets, views, chain->trace_width, chain->trace_height);
}

void DenoiseStep(const Chain& chain, const Passes& passes) {
  Use(chain);
  Views views = GBuffer(chain, false);
  views.insert(views.end(), {{40, chain.history[chain.index].srv.Get()}, {46, chain.prepared_light.srv.Get()},
                             {47, chain.prepared_geometry.srv.Get()}});
  std::vector<ID3D11RenderTargetView*> targets = {chain.denoised.rtvs[0].Get()};
  if (chain.sh) {
    views.insert(views.end(), {{56, chain.history_sh[chain.index].srv.Get()}, {57, chain.history_zonal[chain.index].srv.Get()}});
    targets.insert(targets.end(), {chain.denoised_sh.rtvs[0].Get(), chain.denoised_zonal.rtvs[0].Get()});
  }
  Draw(passes.denoise.Get(), targets, views, chain.trace_width, chain.trace_height);
}

// `msaa`: 0 the apply into `output`; 1 its _ms build per sample, 2 its _ms_pixel build into `output_ms`.
void ApplyStep(const Chain& chain, const Passes& passes, bool denoised, int msaa = 0) {
  Use(chain);
  Views views = GBuffer(chain, msaa != 0);
  views.push_back({41, (denoised ? chain.denoised : chain.history[chain.index]).srv.Get()});
  if (chain.sh) {
    views.insert(views.end(), {{47, chain.prepared_geometry.srv.Get()},
                               {56, (denoised ? chain.denoised_sh : chain.history_sh[chain.index]).srv.Get()},
                               {57, (denoised ? chain.denoised_zonal : chain.history_zonal[chain.index]).srv.Get()}});
  }
  const auto& shader = (msaa == 0) ? passes.apply : ((msaa == 1) ? passes.apply_ms : passes.apply_ms_pixel);
  Draw(shader.Get(), {((msaa != 0) ? chain.output_ms : chain.output).rtvs[0].Get()}, views, chain.camera.width, chain.camera.height);
}

void RunFrame(Chain* chain, const Passes& passes, bool denoise, int msaa = 0) {
  PrepareStep(*chain, passes, msaa != 0);
  TraceStep(chain, passes);
  if (denoise) DenoiseStep(*chain, passes);
  ApplyStep(*chain, passes, denoise, msaa);
}

// ---------------------------------------------------------------------------------------------------------------------
// The fields (S0's s0_sh.py named_field / random_field_spec, retyped; the random ones from this test's generator) on a
// Fibonacci sphere of 16384 directions in the fields' frame; radiance 0 below the plane.

struct Spec {
  struct Cap {
    V3 centre;
    double cos_radius = 1;
    std::array<double, 3> colour = {};
  };
  std::vector<Cap> caps;
  bool band = false;
  double band_sin = 0;
  std::array<double, 3> band_colour = {};
};

std::array<double, 3> HueColour(double hue, double saturation, double scale) {
  std::array<double, 3> colour = {};
  constexpr std::array<double, 3> OFFSETS = {0, 4, 2};
  for (size_t c = 0; c < 3; ++c) {
    const double rgb = std::clamp(std::abs(std::fmod((hue * 6) + OFFSETS[c], 6.0) - 3) - 1, 0.0, 1.0);
    colour[c] = (1 - saturation + (saturation * rgb)) * scale;
  }
  return colour;
}

Spec RandomSpec(std::mt19937_64* rng) {
  std::uniform_real_distribution<double> uniform(0, 1);
  std::normal_distribution<double> normal(0, 1);
  Spec spec;
  const int caps = 1 + static_cast<int>(uniform(*rng) * 6);
  for (int i = 0; i < caps; ++i) {
    V3 centre = Unit({normal(*rng), normal(*rng), normal(*rng)});
    centre.z = std::abs(centre.z);
    const double hue = uniform(*rng);
    const double saturation = uniform(*rng) * 0.8;
    const double scale = std::exp(-0.5 + normal(*rng));
    spec.caps.push_back({centre, std::cos((5 + (55 * uniform(*rng))) * PI / 180), HueColour(hue, saturation, scale)});
  }
  if (uniform(*rng) < 0.5) {
    const double hue = uniform(*rng);
    const double saturation = uniform(*rng) * 0.6;
    spec.band = true;
    spec.band_sin = std::sin((10 + (30 * uniform(*rng))) * PI / 180);
    spec.band_colour = HueColour(hue, saturation, std::exp(-1 + (0.7 * normal(*rng))));
  }
  return spec;
}

std::array<double, 3> Radiance(const std::string& name, const Spec& spec, V3 d) {
  if (d.z <= 0) return {};
  const double elevation = std::asin(std::clamp(d.z, -1.0, 1.0)) * 180 / PI;
  const auto cap = [&](double elevation_deg, double radius_deg) {
    const double e = elevation_deg * PI / 180;
    return Dot(d, {std::cos(e), 0, std::sin(e)}) >= std::cos(radius_deg * PI / 180);
  };
  if (name == "random") {
    std::array<double, 3> radiance = {};
    if (spec.band && d.z < spec.band_sin) radiance = spec.band_colour;
    for (const auto& c : spec.caps) {
      if (Dot(d, c.centre) >= c.cos_radius) radiance = c.colour;
    }
    return radiance;
  }
  if (name == "uniform") return {1, 1, 1};
  if (name == "cap10_e45") return cap(45, 10) ? std::array<double, 3>{10, 9, 8} : std::array<double, 3>{};
  if (name == "cap10_e5") return cap(5, 10) ? std::array<double, 3>{10, 9, 8} : std::array<double, 3>{};
  if (name == "wall_side") return (d.x < -0.2 && elevation < 60) ? std::array<double, 3>{0.8, 0.25, 0.15} : std::array<double, 3>{};
  if (name == "corridor") {
    if (d.x < -0.3) return {0.8, 0.2, 0.1};
    if (d.x > 0.3) return {0.15, 0.7, 0.2};
    return {};
  }
  if (name == "courtyard") return (elevation < 35) ? std::array<double, 3>{0.5, 0.45, 0.35} : std::array<double, 3>{};
  return {};
}

struct Field {
  std::string name;
  std::vector<V3> direction;  // only those with light
  std::vector<std::array<double, 3>> radiance;
  std::vector<double> y;
  double dw = 0;
};

Field MakeField(const std::string& name, const Spec& spec) {
  constexpr int COUNT = 16384;
  Field field = {.name = name, .dw = 4 * PI / COUNT};
  for (int i = 0; i < COUNT; ++i) {
    const double z = 1 - (2 * (i + 0.5) / COUNT);
    const double r = std::sqrt(std::max(0.0, 1 - (z * z)));
    const double a = (i + 0.5) * PI * (3 - std::sqrt(5.0));
    const V3 d = {r * std::cos(a), r * std::sin(a), z};
    const auto radiance = Radiance(name, spec, d);
    if (Luminance(radiance) <= 0) continue;
    field.direction.push_back(d);
    field.radiance.push_back(radiance);
    field.y.push_back(Luminance(radiance));
  }
  return field;
}

// At normal `n` (the fields' frame): the light F0 (rgb), c0, c1 (only the light above n's horizon: the trace sees its
// own hemisphere), m1 = the cosine-weighted mean direction x Y.
struct Moments {
  std::array<double, 3> f0 = {};
  double c0 = 0;
  V3 c1;
  V3 m1;
};

Moments Project(const Field& field, V3 n) {
  Moments moments;
  for (size_t k = 0; k < field.direction.size(); ++k) {
    const double c = Dot(n, field.direction[k]);
    if (c <= 0) continue;
    for (size_t channel = 0; channel < 3; ++channel) moments.f0[channel] += field.radiance[k][channel] * c;
    moments.c0 += field.y[k];
    moments.c1 = moments.c1 + (field.direction[k] * field.y[k]);
    moments.m1 = moments.m1 + (field.direction[k] * (field.y[k] * c));
  }
  for (double& channel : moments.f0) channel *= field.dw / PI;
  moments.c0 *= field.dw / (4 * PI);
  moments.c1 = moments.c1 * (field.dw / (2 * PI));
  moments.m1 = moments.m1 * (field.dw / PI);
  return moments;
}

double Truth(const Field& field, V3 n) {
  double sum = 0;
  for (size_t k = 0; k < field.direction.size(); ++k) sum += field.y[k] * std::max(0.0, Dot(n, field.direction[k]));
  return sum * field.dw / PI;
}

// ---------------------------------------------------------------------------------------------------------------------
// Statistics.

struct Stats {
  std::vector<double> shift, error, detail;
  void Add(const std::vector<double>& estimate, const std::vector<double>& truth) {
    double mean_e = 0;
    double mean_t = 0;
    for (size_t i = 0; i < truth.size(); ++i) mean_e += estimate[i], mean_t += truth[i];
    mean_e /= truth.size(), mean_t /= truth.size();
    if (mean_t <= 1e-9) return;
    double squares = 0;
    double covariance = 0;
    double variance_e = 0;
    double variance_t = 0;
    for (size_t i = 0; i < truth.size(); ++i) {
      squares += (estimate[i] - truth[i]) * (estimate[i] - truth[i]);
      covariance += (estimate[i] - mean_e) * (truth[i] - mean_t);
      variance_e += (estimate[i] - mean_e) * (estimate[i] - mean_e);
      variance_t += (truth[i] - mean_t) * (truth[i] - mean_t);
    }
    shift.push_back((mean_e / mean_t) - 1);
    error.push_back(std::sqrt(squares / truth.size()) / mean_t);
    if (variance_t > 1e-18 && variance_e > 1e-18) detail.push_back(covariance / std::sqrt(variance_e * variance_t));
  }
  static double Mean(const std::vector<double>& values) {
    double sum = 0;
    for (double v : values) sum += v;
    return values.empty() ? 0 : sum / values.size();
  }
};

std::vector<double> OutputLuminance(const Chain& chain, bool* finite) {
  std::vector<double> luminance;
  for (const auto& texel : ReadFloats(chain.output)) {
    *finite = *finite && std::isfinite(texel[0]) && std::isfinite(texel[1]) && std::isfinite(texel[2]);
    luminance.push_back(Luminance(texel));
  }
  return luminance;
}

// ---------------------------------------------------------------------------------------------------------------------
// The items.

struct Game {
  const char* name;
  bool x86;
};
constexpr std::array<Game, 16> GAMES = {{
    {"twattila", true},
    {"ruse", false},
    {"twthronesofbritannia", true},
    {"twrome2", true},
    {"twromeremastered", false},
    {"twshogun2completeedition", false},
    {"twtroy", false},
    {"twpharaohdynasties", false},
    {"twwarhammer", false},
    {"twwarhammer2", false},
    {"twwarhammer3", false},
    {"divinityoriginalsinenhanced", false},
    {"divinity2originalsindefinitive", false},
    {"expeditionsrome", false},
    {"rysesonofrome", false},
    {"talesofarise", false},
}};
constexpr std::array<const char*, 9> PASS_NAMES = {"light_bounce_prepare", "light_bounce_prepare_ms", "light_bounce_levels",
                                                   "light_bounce_trace", "light_bounce_resolve", "light_bounce_denoise",
                                                   "light_bounce_apply", "light_bounce_apply_ms", "light_bounce_apply_ms_pixel"};

std::string Attila(const std::string& file) { return repository + "\\src\\games\\twattila\\" + file; }

Passes MakePasses(const std::array<std::vector<char>, 9>& code) {
  Passes passes;
  ComPtr<ID3D11PixelShader>* slots[] = {&passes.prepare, &passes.prepare_ms, &passes.levels, &passes.trace, &passes.resolve,
                                        &passes.denoise, &passes.apply, &passes.apply_ms, &passes.apply_ms_pixel};
  for (size_t i = 0; i < code.size(); ++i) *slots[i] = PixelShader(code[i]);
  return passes;
}

// Item 1: the built add-ons' shaders that include Light Bounce files against the builds before; Attila's chain with
// the define.
void RowOff(const Passes& before, const Passes& on, const std::array<std::vector<char>, 9>& on_code) {
  std::printf("\n== 1. Row off: every add-on's shaders that include Light Bounce files: the same code as the build before\n");
  std::printf("     (Attila's passes' dcl_temps before / built / with the define)\n");
  for (const auto& game : GAMES) {
    for (const char* build : {"\\build\\", "\\build32\\"}) {
      if (!game.x86 && std::strcmp(build, "\\build32\\") == 0) continue;
      const std::string embed = repository + build + game.name + ".include\\embed\\";
      std::error_code error;
      size_t files = 0;
      size_t same = 0;
      std::string temps;
      for (const auto& entry : std::filesystem::directory_iterator(before_root + "\\" + game.name, error)) {
        const std::string name = entry.path().filename().string();
        const auto previous = ReadFile(entry.path().string());
        const auto after = ReadFile(embed + name);
        ++files;
        const bool identical = !after.empty() && Code(after) == Code(previous);
        same += identical ? 1 : 0;
        if (!identical) std::printf("   %s%s%s: %s\n", game.name, build, name.c_str(), after.empty() ? "not built" : "code differs from the build before");
        for (size_t i = 0; std::strcmp(game.name, "twattila") == 0 && i < PASS_NAMES.size(); ++i) {
          if (name == std::string(PASS_NAMES[i]) + ".cso") {
            temps += " " + std::string(PASS_NAMES[i] + 13) + " " + std::to_string(Temps(previous)) + "/" + std::to_string(Temps(after)) + "/"
                     + std::to_string(Temps(on_code[i]));
          }
        }
      }
      Verdict(files > 0 && same == files, "  %-31s %-7s %2zu shaders, %2zu the same code%s%s", game.name, build + 1, files, same,
              temps.empty() ? "" : " |", temps.c_str());
    }
  }

  // Attila's passes with the define, the row off, against the build before: a bumpy room with the pole, Half, 3 frames
  // with Checkerboard, Multi-Bounce, Back-Face Rejection and the denoiser; then the _ms builds.
  const Scene scene = Room(96, 64, 0.35, true);
  struct Run {
    Chain chain;
    std::vector<size_t> differing;
  };
  const auto run = [&](const Passes& passes, bool row, int msaa) {
    Chain chain = MakeChain(scene, 2);
    chain.lb.checkerboard = 1.f, chain.lb.bounces = 0.5f, chain.lb.backface = 1.f;
    chain.lb.directional = row ? 1.f : 0.f;
    chain.sh = row;
    for (int frame = 0; frame < 3; ++frame) {
      chain.lb.frame = static_cast<float>(frame);
      RunFrame(&chain, passes, true, msaa);
    }
    return chain;
  };
  for (int msaa = 0; msaa <= 2; ++msaa) {
    const Chain previous = run(before, false, msaa);
    const Chain off = run(on, false, msaa);
    const Chain row = run(on, true, msaa);
    size_t differing = 0;
    for (UINT level = 0; level < previous.levels; ++level) {
      differing += Differing(previous.prepared_light, off.prepared_light, level) + Differing(previous.prepared_geometry, off.prepared_geometry, level);
    }
    for (size_t i = 0; i < 2; ++i) {
      differing += Differing(previous.history[i], off.history[i]) + Differing(previous.frames[i], off.frames[i]);
    }
    differing += Differing(previous.traced, off.traced) + Differing(previous.denoised, off.denoised);
    const Texture& out_before = (msaa != 0) ? previous.output_ms : previous.output;
    const Texture& out_off = (msaa != 0) ? off.output_ms : off.output;
    const Texture& out_on = (msaa != 0) ? row.output_ms : row.output;
    differing += Differing(out_before, out_off);
    const size_t control = Differing(out_before, out_on) + Differing(previous.prepared_geometry, row.prepared_geometry);
    Verdict(differing == 0 && control > 0, "  Attila with the define (%s): row off %zu texels differ from the build before; row on %zu (CONTROL)",
            (msaa == 0) ? "prepare, apply" : ((msaa == 1) ? "_ms prepare, _ms apply" : "_ms prepare, _ms_pixel apply"), differing, control);
  }
}

// Each texel's normal as the passes take it (decoded, turned toward the view).
std::vector<V3> TexelNormals(const Chain& chain) {
  std::vector<V3> normals;
  const auto geometry = ReadBack(chain.prepared_geometry);
  for (size_t i = 0; i < geometry.size(); ++i) {
    const double x = static_cast<double>(i % chain.trace_width) * chain.scale;
    const double y = static_cast<double>(i / chain.trace_width) * chain.scale;
    const V3 position = Position(chain.camera, x, y, std::bit_cast<float>(geometry[i][0]));
    normals.push_back(Facing(DecodeNormal(geometry[i][1]), position));
  }
  return normals;
}

// Item 2: a flat room, Quarter, one frame: on = off where the pixel normal = the trace normal, there and in every texel
// the denoiser and the apply reach (5 texels each way; the block normal at the room's edges, the prepare's, changes their
// trace: printed).
void FlatRoom(const Passes& on) {
  std::printf("\n== 2. Flat room, Quarter, the real trace: on vs off where pixel normal = trace normal\n");
  const Scene scene = Room(192, 128, 0, false);
  constexpr UINT SCALE = 4;
  std::vector<double> results[2];
  std::vector<Chain> chains;
  for (int row = 0; row < 2; ++row) {
    Chain chain = MakeChain(scene, SCALE);
    chain.lb.directional = static_cast<float>(row);
    chain.sh = (row == 1);
    RunFrame(&chain, on, true);
    bool finite = true;
    results[row] = OutputLuminance(chain, &finite);
    chains.push_back(std::move(chain));
  }
  // Each pass's bounce on vs off (texels differing, the largest relative difference).
  const auto stage = [&](const char* name, const Texture& off_texture, const Texture& on_texture) {
    const auto a = ReadFloats(off_texture);
    const auto b = ReadFloats(on_texture);
    size_t count = 0;
    double most = 0;
    for (size_t i = 0; i < a.size(); ++i) {
      const double y = Luminance(a[i]);
      const double difference = std::abs(Luminance(b[i]) - y) / std::max(y, 1e-6);
      count += (difference > 1e-6) ? 1 : 0;
      most = std::max(most, difference);
    }
    std::printf("   %s: %zu of %zu texels differ, the largest by %.2e\n", name, count, a.size(), most);
  };
  stage("traced", chains[0].traced, chains[1].traced);
  stage("resolved", chains[0].history[chains[0].index], chains[1].history[chains[1].index]);
  stage("denoised", chains[0].denoised, chains[1].denoised);
  // Where the pixel's normal is the trace normal of every texel within 5 of its own and their trace the row off's.
  const UINT tw = chains[1].trace_width;
  const UINT th = chains[1].trace_height;
  const UINT width = scene.camera.width;
  const auto n0 = TexelNormals(chains[1]);
  const auto traced_off = ReadBack(chains[0].traced);
  const auto traced_on = ReadBack(chains[1].traced);
  double brightest = 0;
  for (double v : results[0]) brightest = std::max(brightest, v);
  std::vector<double> relative;
  size_t lit = 0;
  size_t darker = 0;
  size_t brighter = 0;
  for (size_t i = 0; i < results[0].size(); ++i) {
    if (results[0][i] < 1e-3 * brightest) continue;
    ++lit;
    darker += (results[1][i] < 0.5 * results[0][i]) ? 1 : 0;
    brighter += (results[1][i] > 2 * results[0][i]) ? 1 : 0;
    const V3 normal = {(scene.normal[i][0] * 2.0) - 1, (scene.normal[i][1] * 2.0) - 1, (scene.normal[i][2] * 2.0) - 1};
    const int tx = static_cast<int>(i % width / SCALE);
    const int ty = static_cast<int>(i / width / SCALE);
    bool all = true;
    for (int dy = -5; dy <= 5; ++dy) {
      for (int dx = -5; dx <= 5; ++dx) {
        const size_t t = (size_t(std::clamp(ty + dy, 0, static_cast<int>(th) - 1)) * tw) + std::clamp(tx + dx, 0, static_cast<int>(tw) - 1);
        all = all && Dot(n0[t], Facing(normal, n0[t] * -1)) > 1 - 1e-6 && traced_off[t] == traced_on[t];
      }
    }
    if (all) relative.push_back(std::abs(results[1][i] - results[0][i]) / results[0][i]);
  }
  std::printf("   all lit pixels (the room's creases too): of %zu, %zu under half the row off's, %zu over twice\n", lit, darker, brighter);
  std::ranges::sort(relative);
  const double most = relative.empty() ? 1 : relative.back();
  const double p999 = relative.empty() ? 1 : relative[relative.size() * 999 / 1000];
  const double mean = Stats::Mean(relative);
  Verdict(!relative.empty() && p999 <= 1e-3 && most <= 3e-3, "  %zu pixels: |on - off| / off mean %.2e, 99.9%% %.2e, max %.2e (~1e-3)",
          relative.size(), mean, p999, most);
}

// The prepared geometry made here (`n0` per texel, depth 1 / 4: the patch's plane).
void SetGeometry(Chain* chain, const std::vector<V3>& n0) {
  std::vector<std::array<uint32_t, 2>> packed(n0.size());
  for (size_t i = 0; i < n0.size(); ++i) packed[i] = {std::bit_cast<uint32_t>(0.25f), EncodeNormal(n0[i])};
  chain->prepared_geometry = Make(chain->trace_width, chain->trace_height, DXGI_FORMAT_R32G32_UINT, 1, 1, packed.data());
}


// Injects per texel `light` (rgb) and `moments` (c1 view xyz, c0; or g) in `mode`, then the resolve `frames` times.
void Inject(Chain* chain, const Passes& passes, const std::vector<std::array<float, 4>>& light,
            const std::vector<std::array<float, 4>>& moments, float mode, int frames) {
  const Texture light_texture = Make(chain->trace_width, chain->trace_height, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1, light.data());
  const Texture moments_texture = Make(chain->trace_width, chain->trace_height, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1, moments.data());
  const std::array<float, 4> inject_mode = {mode, 0, 0, 0};
  for (int frame = 0; frame < frames; ++frame) {
    SetConstants(0, inject_mode.data(), sizeof(inject_mode));
    chain->lb.frame = static_cast<float>(frame % 64);
    TraceStep(chain, passes, inject.Get(), {{0, light_texture.srv.Get()}, {1, moments_texture.srv.Get()}});
  }
}

std::array<float, 4> Float4(V3 v, double w) {
  return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), static_cast<float>(w)};
}

// Item 3: the uniform sky at tilted pixel normals.
void UniformSky(const Passes& on) {
  std::printf("\n== 3. Uniform sky injected (c0 0.5, c1 n0 / 2, Y0 1), pixel normals tilted 15-75 degrees off n0\n");
  constexpr UINT SIZE = 64;
  std::vector<V3> normals(size_t{SIZE} * SIZE);
  for (UINT y = 0; y < SIZE; ++y) {
    for (UINT x = 0; x < SIZE; ++x) {
      const double tilt = (15.0 + (15.0 * (y * 5 / SIZE))) * PI / 180;
      const double azimuth = 2 * PI * (x + 0.5) / SIZE;
      normals[(size_t{y} * SIZE) + x] = {std::sin(tilt) * std::cos(azimuth), std::sin(tilt) * std::sin(azimuth), std::cos(tilt)};
    }
  }
  const Scene scene = Patch(SIZE, normals);
  for (int denoise = 0; denoise <= 1; ++denoise) {
    Chain chain = MakeChain(scene, 2);
    chain.sh = true;
    chain.lb.directional = 1.f;
    SetGeometry(&chain, std::vector<V3>(size_t{chain.trace_width} * chain.trace_height, V3{0, 0, -1}));
    const std::vector<std::array<float, 4>> light(size_t{chain.trace_width} * chain.trace_height, {1.f, 1.f, 1.f, 0.f});
    std::vector<std::array<float, 4>> moments;
    for (const V3& n0 : TexelNormals(chain)) moments.push_back(Float4(n0 * 0.5, 0.5));
    Inject(&chain, on, light, moments, 0.f, 4);
    if (denoise != 0) DenoiseStep(chain, on);
    ApplyStep(chain, on, denoise != 0);
    const auto output = ReadFloats(chain.output);
    double most = 0;
    std::array<double, 5> band_most = {};
    size_t away = 0;
    for (UINT y = 0; y < SIZE; ++y) {
      for (UINT x = 0; x < SIZE; ++x) {
        const size_t i = (size_t{y} * SIZE) + x;
        const V3 position = Position(scene.camera, x, y, 0.25);
        const V3 n = Facing(ToView(normals[i]), position);
        // Turned toward the view past 90 degrees from n0 (grazing at the patch's sides): the bounce as without it.
        const double cosine = Dot(n, V3{0, 0, -1});
        const double expected = (cosine > 0) ? (1 + cosine) / 2 : 1;
        away += (cosine > 0) ? 0 : 1;
        const double error = std::abs(Luminance(output[i]) - expected) / expected;
        most = std::max(most, error);
        band_most[y * 5 / SIZE] = std::max(band_most[y * 5 / SIZE], error);
      }
    }
    Verdict(most <= 2e-3,
            "  denoiser %s: max |Y(n) / Y0 - (1 + cos t) / 2| / that %.2e (15 / 30 / 45 / 60 / 75: %.1e %.1e %.1e %.1e %.1e; %zu pixels"
            " turned past 90 degrees: Y0)",
            (denoise != 0) ? "on " : "off", most, band_most[0], band_most[1], band_most[2], band_most[3], band_most[4], away);
  }
}

// Item 4's configurations: the scalar (the row off) without / with the denoiser; ZH3 + memo without / with it at each
// beta (LB.transport: the denoiser's and the apply's); the helper's form after each filter radius at each beta; ZH3 +
// memo at each pixel from its own texel alone (the test's own arithmetic: the form's error without the chain, S0's).
constexpr std::array<float, 3> BETAS = {0.f, 0.5f, 1.f};
constexpr std::array<double, 3> HELPER_RADII = {0.0, 4.0, 8.0};
constexpr size_t SCALAR = 0;
constexpr size_t SCALAR_DENOISED = 1;
constexpr size_t Sh(size_t beta) { return 2 + beta; }
constexpr size_t ShDenoised(size_t beta) { return 5 + beta; }
constexpr size_t Helper(size_t radius, size_t beta) { return 8 + (radius * 3) + beta; }
constexpr size_t OWN_TEXEL = 17;
constexpr size_t CONFIGS = 18;

std::string ConfigName(size_t config) {
  char name[64];
  if (config == OWN_TEXEL) return "ZH3 + memo, own texel only (no chain)";
  if (config < Sh(0)) return (config == SCALAR) ? "scalar (row off), no denoiser" : "scalar (row off), denoiser";
  if (config < ShDenoised(0)) std::snprintf(name, sizeof(name), "ZH3 + memo, no denoiser, beta %.1f", BETAS[config - Sh(0)]);
  else if (config < Helper(0, 0)) std::snprintf(name, sizeof(name), "ZH3 + memo, denoiser, beta %.1f", BETAS[config - ShDenoised(0)]);
  else {
    const size_t index = config - Helper(0, 0);
    std::snprintf(name, sizeof(name), "helper, filter r %.0f + rewrite, beta %.1f", HELPER_RADII[index / 3], BETAS[index % 3]);
  }
  return name;
}

struct ChainResult {
  std::array<Stats, CONFIGS> stats;
  bool finite = true;
};

// Item 4: the chain check, one normal map strength and block size.
ChainResult ChainCheck(const Passes& on, double strength, UINT scale, const std::vector<Field>& fields) {
  constexpr UINT SIZE = 64;
  const auto normals = NormalMap(static_cast<uint64_t>(std::lround(strength * 1000)), SIZE, SIZE, strength, 1.5);
  const Scene scene = Patch(SIZE, normals);
  Chain chain = MakeChain(scene, scale);
  chain.sh = true;
  chain.lb.directional = 1.f;
  PrepareStep(chain, on, false);
  const auto n0 = TexelNormals(chain);
  const size_t texels = n0.size();
  // The texels' centres in pixels, for the helper's wide filter.
  std::vector<std::array<double, 2>> centre(texels);
  for (size_t t = 0; t < texels; ++t) {
    centre[t] = {(static_cast<double>(t % chain.trace_width) + 0.5) * scale, (static_cast<double>(t / chain.trace_width) + 0.5) * scale};
  }
  ChainResult result;
  for (const Field& field : fields) {
    std::vector<double> truth(normals.size());
    for (size_t i = 0; i < normals.size(); ++i) truth[i] = Truth(field, normals[i]);
    std::vector<Moments> moments(texels);
    for (size_t t = 0; t < texels; ++t) moments[t] = Project(field, ToView(n0[t]));
    std::vector<std::array<float, 4>> light(texels);
    std::vector<std::array<float, 4>> stored(texels);
    for (size_t t = 0; t < texels; ++t) {
      light[t] = {static_cast<float>(moments[t].f0[0]), static_cast<float>(moments[t].f0[1]), static_cast<float>(moments[t].f0[2]), 0.f};
      stored[t] = Float4(ToView(moments[t].c1), moments[t].c0);
    }
    const auto measure = [&](size_t config, bool denoised, float directional, float beta) {
      chain.lb.directional = directional;
      chain.lb.transport = beta;
      if (denoised) DenoiseStep(chain, on);
      ApplyStep(chain, on, denoised);
      result.stats[config].Add(OutputLuminance(chain, &result.finite), truth);
    };
    // Each pixel from its own texel's form (bitmask_sh.hlsli BitmaskShFromMoments with the memo; the apply's gain).
    std::vector<double> own(normals.size());
    for (size_t i = 0; i < normals.size(); ++i) {
      const UINT x = static_cast<UINT>(i % SIZE);
      const UINT y = static_cast<UINT>(i / SIZE);
      const size_t t = (size_t{y / scale} * chain.trace_width) + (x / scale);
      const V3 c1 = ToView(moments[t].c1);
      const double c0 = moments[t].c0;
      const double y0 = Luminance(moments[t].f0);
      const double c1_length = std::sqrt(Dot(c1, c1));
      const double p = 0.866025404 * c1_length / std::max(c0, 1e-12);
      const double zonal = 0.279508497 * c0 * (-1.052823 + (p * (0.518202 + (p * 0.805396))));
      const auto zonal_term = [&](V3 direction, V3 n) {
        const double length = std::sqrt(Dot(direction, direction));
        const double cosine = (length > 0) ? Dot(n, direction) / length : 0;
        return zonal * ((3 * cosine * cosine) - 1);
      };
      const V3 g = c1 + (n0[t] * (1.5 * (y0 - (c0 + Dot(c1, n0[t]) + zonal_term(c1, n0[t])))));
      const double w = Dot(g, n0[t]) + zonal_term(g, n0[t]);
      const V3 n = Facing(ToView(normals[i]), Position(scene.camera, x, y, 0.25));
      own[i] = y0 * std::clamp((y0 - w + Dot(g, n) + zonal_term(g, n)) / std::max(y0, (0.01 * y0) + 1e-6), 0.0, 4.0);
    }
    result.stats[OWN_TEXEL].Add(own, truth);
    ResetHistory(&chain);
    chain.lb.directional = 1.f;
    Inject(&chain, on, light, stored, 0.f, 32);
    measure(SCALAR, false, 0.f, 0.f);
    measure(SCALAR_DENOISED, true, 0.f, 0.f);
    for (size_t beta = 0; beta < BETAS.size(); ++beta) {
      measure(Sh(beta), false, 1.f, BETAS[beta]);
      measure(ShDenoised(beta), true, 1.f, BETAS[beta]);
    }
    // The helper's pinned form: rgb0 and g = 1.2 m1 filtered (Gaussian sigma r / 2 px within r px, normals' facing^8;
    // NRD's stand-in), pinned at each texel's normal (rt_nrd_finish's rewrite), the resolve's, the apply (no denoiser:
    // NRD replaces it).
    for (size_t index = 0; index < HELPER_RADII.size(); ++index) {
      const double radius = HELPER_RADII[index];
      std::vector<std::array<float, 4>> filtered_light(texels);
      std::vector<std::array<float, 4>> filtered_g(texels);
      for (size_t t = 0; t < texels; ++t) {
        std::array<double, 3> rgb = {};
        V3 g;
        double weights = 0;
        for (size_t j = 0; j < texels; ++j) {
          const double dx = centre[j][0] - centre[t][0];
          const double dy = centre[j][1] - centre[t][1];
          const double d2 = (dx * dx) + (dy * dy);
          if (d2 > (radius * radius) + 1e-9) continue;
          const double sigma = std::max(radius / 2, 1e-3);
          const double weight = std::exp(-d2 / (2 * sigma * sigma)) * std::pow(std::max(Dot(n0[t], n0[j]), 0.0), 8);
          for (size_t c = 0; c < 3; ++c) rgb[c] += moments[j].f0[c] * weight;
          g = g + (ToView(moments[j].m1) * (1.2 * weight));
          weights += weight;
        }
        filtered_light[t] = {static_cast<float>(rgb[0] / weights), static_cast<float>(rgb[1] / weights), static_cast<float>(rgb[2] / weights), 0.f};
        filtered_g[t] = Float4(g * (1 / weights), 0);
      }
      ResetHistory(&chain);
      chain.lb.directional = 1.f;
      Inject(&chain, on, filtered_light, filtered_g, 2.f, 32);
      for (size_t beta = 0; beta < BETAS.size(); ++beta) measure(Helper(index, beta), false, 1.f, BETAS[beta]);
    }
  }
  return result;
}

void ChainChecks(const Passes& on) {
  std::printf("\n== 4. Chain check: exact moments injected -> resolve (32 frames) -> denoiser -> apply vs the exact per-pixel F\n");
  std::printf("     (mean over the fields of the brightness shift [min, max], error = RMSE / mean, detail = correlation)\n");
  std::vector<Field> fields;
  for (const char* name : {"cap10_e45", "wall_side", "corridor", "courtyard"}) fields.push_back(MakeField(name, {}));
  std::mt19937_64 rng(404);
  for (int i = 0; i < 24; ++i) fields.push_back(MakeField("random", RandomSpec(&rng)));
  int chosen = -1;
  std::array<double, 3> beta_shift = {};
  std::array<double, 3> helper_worst = {};
  bool finite = true;
  for (double strength : {0.15, 0.35, 0.6}) {
    for (UINT scale : {2u, 4u, 8u}) {
      const ChainResult result = ChainCheck(on, strength, scale, fields);
      finite = finite && result.finite;
      std::printf("   m %.2f, b %u%s\n", strength, scale, (strength > 0.5) ? " (printed, no pass rule)" : "");
      for (size_t c = 0; c < CONFIGS; ++c) {
        const Stats& stats = result.stats[c];
        const auto [low, high] = std::ranges::minmax(stats.shift);
        std::printf("     %-42s shift %+6.2f%% [%+6.2f, %+6.2f]  error %.3f  detail %.3f\n", ConfigName(c).c_str(),
                    100 * Stats::Mean(stats.shift), 100 * low, 100 * high, Stats::Mean(stats.error), Stats::Mean(stats.detail));
      }
      for (size_t beta = 0; beta < BETAS.size(); ++beta) {
        if (std::abs(strength - 0.35) < 1e-9 && scale == 4) {
          beta_shift[beta] = Stats::Mean(result.stats[ShDenoised(beta)].shift);
          if (std::abs(beta_shift[beta]) <= 0.01) chosen = static_cast<int>(beta);
        }
        if (std::abs(strength - 0.35) < 1e-9 && scale == 2) {
          helper_worst[beta] = std::max(std::abs(Stats::Mean(result.stats[Helper(1, beta)].shift)),
                                        std::abs(Stats::Mean(result.stats[Helper(2, beta)].shift)));
        }
      }
    }
  }
  Verdict(finite, "  every output finite");
  std::printf("   beta at b 4, m 0.35 (denoiser on): shift %+.2f%% / %+.2f%% / %+.2f%% at 0 / 0.5 / 1 -> ", 100 * beta_shift[0],
              100 * beta_shift[1], 100 * beta_shift[2]);
  if (chosen >= 0) {
    std::printf("chosen beta %.1f (the largest within 1%%)\n", BETAS[chosen]);
  } else {
    std::printf("none within 1%%: beta 0, LOOK-RISK\n");
  }
  const size_t at = (chosen >= 0) ? static_cast<size_t>(chosen) : 0;
  std::printf("   helper (B2 / B4) at b 2, m 0.35, wide filter r 4 / 8 + rewrite, beta %.1f: |shift| up to %.2f%% (%s)\n", BETAS[at],
              100 * helper_worst[at], (helper_worst[at] > 0.01) ? "over 1%" : "within 1%");
}

// Item 5: the rewrite last (a history with another g and Y).
void RewriteOrder(const Passes& on) {
  std::printf("\n== 5. Rewrite last: Y(n0) = Y0 after the resolve and after the denoiser, the history's g and Y blended in\n");
  constexpr UINT SIZE = 64;
  const Scene scene = Patch(SIZE, std::vector<V3>(size_t{SIZE} * SIZE, V3{0, 0, 1}));
  Chain chain = MakeChain(scene, 2);
  chain.sh = true;
  chain.lb.directional = 1.f;
  const size_t texels = size_t{chain.trace_width} * chain.trace_height;
  // Each texel's normal tilted 30 degrees, its azimuth turning across the patch.
  std::vector<V3> tilted(texels);
  for (size_t t = 0; t < texels; ++t) {
    const double azimuth = 2 * PI * static_cast<double>(t % chain.trace_width) / chain.trace_width;
    tilted[t] = ToView({0.5 * std::cos(azimuth), 0.5 * std::sin(azimuth), 0.866});
  }
  SetGeometry(&chain, tilted);
  const auto n0 = TexelNormals(chain);
  const Field field = MakeField("cap10_e45", {});
  std::vector<std::array<float, 4>> light(texels);
  std::vector<std::array<float, 4>> moments(texels);
  for (size_t t = 0; t < texels; ++t) {
    const Moments m = Project(field, ToView(n0[t]));
    light[t] = {static_cast<float>(m.f0[0]), static_cast<float>(m.f0[1]), static_cast<float>(m.f0[2]), 0.f};
    moments[t] = Float4(ToView(m.c1), m.c0);
  }
  // The last frame's: twice the light, another direction and zonal amplitude, 31 frames, the same surface.
  std::vector<std::array<float, 4>> history(texels);
  std::vector<std::array<float, 4>> history_sh(texels);
  std::vector<float> history_zonal(texels, 0.3f);
  std::vector<float> frames(texels, 31.f);
  for (size_t t = 0; t < texels; ++t) {
    const V3 position = Position(chain.camera, static_cast<double>(t % chain.trace_width) * 2, static_cast<double>(t / chain.trace_width) * 2, 0.25);
    history[t] = {2 * light[t][0], 2 * light[t][1], 2 * light[t][2], static_cast<float>(std::sqrt(Dot(position, position)))};
    history_sh[t] = {0.6f, -0.4f, 0.2f, 0.f};
  }
  chain.history[0] = Make(chain.trace_width, chain.trace_height, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1, history.data());
  chain.history_sh[0] = Make(chain.trace_width, chain.trace_height, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1, history_sh.data());
  chain.history_zonal[0] = Make(chain.trace_width, chain.trace_height, DXGI_FORMAT_R32_FLOAT, 1, 1, history_zonal.data());
  chain.frames[0] = Make(chain.trace_width, chain.trace_height, DXGI_FORMAT_R32_FLOAT, 1, 1, frames.data());
  chain.index = 0;
  Inject(&chain, on, light, moments, 0.f, 1);
  const auto traced = ReadFloats(chain.traced);
  const auto traced_sh = ReadFloats(chain.traced_sh);
  const auto check = [&](const Texture& colour, const Texture& sh, const Texture& zonal, const char* after) {
    const auto rgb = ReadFloats(colour);
    const auto g = ReadFloats(sh);
    const auto a = ReadFloats(zonal);
    double most = 0;
    size_t moved = 0;
    for (size_t t = 0; t < texels; ++t) {
      const double y0 = Luminance(rgb[t]);
      const V3 direction = {g[t][0], g[t][1], g[t][2]};
      const V3 axis = Unit(direction);
      const double at_n0 = y0 - g[t][3] + Dot(direction, n0[t]) + (a[t][0] * ((3 * Dot(n0[t], axis) * Dot(n0[t], axis)) - 1));
      most = std::max(most, std::abs(at_n0 - y0) / std::max({y0, std::abs(static_cast<double>(g[t][3])), 1e-6}));
      const bool other = std::abs(Luminance(rgb[t]) - Luminance(traced[t])) > 1e-3 * Luminance(traced[t])
                         && std::abs(g[t][0] - traced_sh[t][0]) + std::abs(g[t][1] - traced_sh[t][1]) > 1e-3;
      moved += other ? 1 : 0;
    }
    Verdict(most <= 2e-3 && moved > texels / 2, "  after the %s: max |Y(n0) - Y0| / max(Y0, |w|) %.2e; %zu of %zu texels took the history's Y and g (CONTROL)",
            after, most, moved, texels);
  };
  check(chain.history[chain.index], chain.history_sh[chain.index], chain.history_zonal[chain.index], "resolve");
  // The shipped beta and one that moves each tap (the transport's path).
  for (const float beta : {chain.lb.transport, 0.5f}) {
    chain.lb.transport = beta;
    DenoiseStep(chain, on);
    char after[32];
    std::snprintf(after, sizeof(after), "denoiser (beta %.1f)", beta);
    check(chain.denoised, chain.denoised_sh, chain.denoised_zonal, after);
  }
}

// Item 6: bumpy normals and the guards.
void Bumpy(const Passes& on) {
  std::printf("\n== 6. Bumpy normals (m 0.35) through the real trace, 32 frames: vs the Full-resolution scalar (printed; Full: the row's own change there)\n");
  const Scene scene = Room(192, 128, 0.35, false);
  const auto run = [&](UINT scale, bool row) {
    Chain chain = MakeChain(scene, scale);
    chain.lb.directional = row ? 1.f : 0.f;
    chain.sh = row;
    for (int frame = 0; frame < 32; ++frame) {
      chain.lb.frame = static_cast<float>(frame);
      RunFrame(&chain, on, true);
    }
    bool finite = true;
    return OutputLuminance(chain, &finite);
  };
  const auto full = run(1, false);
  double brightest = 0;
  for (double v : full) brightest = std::max(brightest, v);
  for (UINT scale : {1u, 2u, 4u}) {
    const auto scalar = (scale == 1) ? full : run(scale, false);
    const auto sh = run(scale, true);
    std::vector<double> truth, a, b;
    for (size_t i = 0; i < full.size(); ++i) {
      if (scene.surface[i] == 1 || full[i] < 0.01 * brightest) continue;  // the receivers (back wall, floor)
      truth.push_back(full[i]), a.push_back(scalar[i]), b.push_back(sh[i]);
    }
    // Also with each mean scaled to Full's (the trace's own brightness at its resolution aside).
    const auto scaled_error = [&](const std::vector<double>& estimate) {
      double mean_e = 0;
      double mean_t = 0;
      for (size_t i = 0; i < truth.size(); ++i) mean_e += estimate[i], mean_t += truth[i];
      double squares = 0;
      for (size_t i = 0; i < truth.size(); ++i) {
        const double difference = (estimate[i] * mean_t / mean_e) - truth[i];
        squares += difference * difference;
      }
      return std::sqrt(squares / truth.size()) / (mean_t / truth.size());
    };
    Stats scalar_stats, sh_stats;
    scalar_stats.Add(a, truth);
    sh_stats.Add(b, truth);
    std::printf("   %s: scalar error %.3f (scaled %.3f, shift %+.2f%%, detail %.3f); + SH error %.3f (scaled %.3f, shift %+.2f%%,"
                " detail %.3f): %s\n",
                (scale == 1) ? "Full   " : (scale == 2) ? "Half   " : "Quarter", Stats::Mean(scalar_stats.error), scaled_error(a), 100 * Stats::Mean(scalar_stats.shift),
                Stats::Mean(scalar_stats.detail), Stats::Mean(sh_stats.error), scaled_error(b), 100 * Stats::Mean(sh_stats.shift),
                Stats::Mean(sh_stats.detail), (scaled_error(b) < scaled_error(a)) ? "closer" : "NOT closer");
  }

  std::printf("\n== 6. Guards\n");
  constexpr UINT SIZE = 64;
  const auto normals = NormalMap(350, SIZE, SIZE, 0.35, 1.5);
  const Scene patch = Patch(SIZE, normals);
  const auto patch_chain = [&](const Scene& from) {
    Chain chain = MakeChain(from, 2);
    chain.sh = true;
    chain.lb.directional = 1.f;
    PrepareStep(chain, on, false);
    return chain;
  };
  const auto max_of = [](const std::vector<std::array<float, 4>>& texels, bool* finite) {
    double most = 0;
    for (const auto& t : texels) {
      for (size_t c = 0; c < 3; ++c) {
        *finite = *finite && std::isfinite(t[c]);
        most = std::max(most, std::abs(static_cast<double>(t[c])));
      }
    }
    return most;
  };
  {
    Chain chain = patch_chain(patch);
    const size_t texels = size_t{chain.trace_width} * chain.trace_height;
    Inject(&chain, on, std::vector<std::array<float, 4>>(texels, {0.f, 0.f, 0.f, 0.f}),
           std::vector<std::array<float, 4>>(texels, {0.3f, 0.1f, -0.2f, 0.2f}), 0.f, 2);
    DenoiseStep(chain, on);
    ApplyStep(chain, on, true);
    bool finite = true;
    const double most = max_of(ReadFloats(chain.output), &finite);
    Verdict(finite && most == 0, "  rgb0 0 (g from the memo's step not 0): apply output max %.2e, finite", most);
  }
  {
    Chain chain = patch_chain(patch);
    const size_t texels = size_t{chain.trace_width} * chain.trace_height;
    Inject(&chain, on, std::vector<std::array<float, 4>>(texels, {1.f, 1.f, 1.f, 0.f}),
           std::vector<std::array<float, 4>>(texels, {1e4f, -5e3f, 2e3f, 0.f}), 2.f, 2);
    DenoiseStep(chain, on);
    bool finite = true;
    const double denoised = max_of(ReadFloats(chain.denoised), &finite);
    ApplyStep(chain, on, false);
    const double applied = max_of(ReadFloats(chain.output), &finite);
    ApplyStep(chain, on, true);
    const double both = max_of(ReadFloats(chain.output), &finite);
    const double denoiser_cap = 1.0 + (3.0 * chain.lb.transport);
    Verdict(finite && denoised <= denoiser_cap + 1e-4 && applied <= 4.0001 && both <= (4.0 * denoiser_cap) + 1e-4,
            "  huge g (1e4), rgb0 1: denoiser max %.3f (cap 1 + 3 beta = %.1f), apply alone %.3f (cap 4), both %.3f (%.1f), finite",
            denoised, denoiser_cap, applied, both, 4.0 * denoiser_cap);
  }
  {
    // Back-facing pixel normals (the G-buffer's turned away from the camera): the passes turn them back.
    Scene flipped = patch;
    for (auto& n : flipped.normal) n = {1.f - n[0], 1.f - n[1], 1.f - n[2], n[3]};
    std::vector<std::array<float, 4>> outputs[2];
    for (int side = 0; side < 2; ++side) {
      Chain chain = patch_chain((side == 0) ? patch : flipped);
      const size_t texels = size_t{chain.trace_width} * chain.trace_height;
      const Field field = MakeField("wall_side", {});
      const auto n0 = TexelNormals(chain);
      std::vector<std::array<float, 4>> light(texels);
      std::vector<std::array<float, 4>> moments(texels);
      for (size_t t = 0; t < texels; ++t) {
        const Moments m = Project(field, ToView(n0[t]));
        light[t] = {static_cast<float>(m.f0[0]), static_cast<float>(m.f0[1]), static_cast<float>(m.f0[2]), 0.f};
        moments[t] = Float4(ToView(m.c1), m.c0);
      }
      Inject(&chain, on, light, moments, 0.f, 2);
      DenoiseStep(chain, on);
      ApplyStep(chain, on, true);
      outputs[side] = ReadFloats(chain.output);
    }
    double most = 0;
    for (size_t i = 0; i < outputs[0].size(); ++i) {
      for (size_t c = 0; c < 3; ++c) {
        most = std::max(most, static_cast<double>(std::abs(outputs[0][i][c] - outputs[1][i][c]) / std::max(std::abs(outputs[0][i][c]), 1e-3f)));
      }
    }
    Verdict(most <= 1e-3, "  back-facing pixel normals: max relative difference to the front-facing ones %.2e", most);
  }
  {
    // Grazing light (a 10-degree cap 5 degrees above the plane) on the bumps: the gain within 0-4.
    Chain chain = patch_chain(patch);
    const size_t texels = size_t{chain.trace_width} * chain.trace_height;
    const Field field = MakeField("cap10_e5", {});
    const auto n0 = TexelNormals(chain);
    std::vector<std::array<float, 4>> light(texels);
    std::vector<std::array<float, 4>> moments(texels);
    for (size_t t = 0; t < texels; ++t) {
      const Moments m = Project(field, ToView(n0[t]));
      light[t] = {static_cast<float>(m.f0[0]), static_cast<float>(m.f0[1]), static_cast<float>(m.f0[2]), 0.f};
      moments[t] = Float4(ToView(m.c1), m.c0);
    }
    Inject(&chain, on, light, moments, 0.f, 2);
    ApplyStep(chain, on, false);
    const auto with = ReadFloats(chain.output);
    chain.lb.directional = 0.f;
    ApplyStep(chain, on, false);
    const auto without = ReadFloats(chain.output);
    double most = 0;
    size_t capped = 0;
    bool finite = true;
    for (size_t i = 0; i < with.size(); ++i) {
      finite = finite && std::isfinite(with[i][0]) && std::isfinite(with[i][1]) && std::isfinite(with[i][2]);
      if (Luminance(without[i]) <= 1e-6) continue;
      const double gain = Luminance(with[i]) / Luminance(without[i]);
      most = std::max(most, gain);
      capped += (gain > 3.999) ? 1 : 0;
    }
    Verdict(finite && most <= 4.0001, "  grazing light: the largest gain %.3f (cap 4; %zu pixels at it), finite", most, capped);
  }
  {
    // A thin pole in front of the lit side wall (fresh sectors with a gap): the trace's stored form exact at n0.
    Chain chain = MakeChain(Room(96, 64, 0, true), 2);
    chain.sh = true;
    chain.lb.directional = 1.f;
    RunFrame(&chain, on, false);
    const auto n0 = TexelNormals(chain);
    const auto rgb = ReadFloats(chain.traced);
    const auto g = ReadFloats(chain.traced_sh);
    const auto a = ReadFloats(chain.traced_zonal);
    double brightest = 0;
    for (const auto& t : rgb) brightest = std::max(brightest, Luminance(t));
    double most = 0;
    bool finite = true;
    for (size_t t = 0; t < rgb.size(); ++t) {
      finite = finite && std::isfinite(g[t][0]) && std::isfinite(g[t][3]) && std::isfinite(a[t][0]);
      const double y0 = Luminance(rgb[t]);
      if (y0 < 1e-3 * brightest) continue;
      const V3 direction = {g[t][0], g[t][1], g[t][2]};
      const V3 axis = Unit(direction);
      const double at_n0 = y0 - g[t][3] + Dot(direction, n0[t]) + (a[t][0] * ((3 * Dot(n0[t], axis) * Dot(n0[t], axis)) - 1));
      most = std::max(most, std::abs(at_n0 - y0) / std::max(y0, std::abs(static_cast<double>(g[t][3]))));
    }
    Verdict(finite && most <= 2e-3, "  a pole in front of the lit wall: the trace's form at n0, max |Y(n0) - Y0| / max(Y0, |w|) %.2e, finite", most);
  }
  {
    // The _ms builds on a 4x G-buffer (each sample the pixel's): the same as the single-sampled ones.
    const Scene room = Room(96, 64, 0.35, false);
    Chain single = MakeChain(room, 2);
    single.sh = true;
    single.lb.directional = 1.f;
    RunFrame(&single, on, true);
    const auto expected = ReadFloats(single.output);
    for (int msaa = 1; msaa <= 2; ++msaa) {
      Chain chain = MakeChain(room, 2);
      chain.sh = true;
      chain.lb.directional = 1.f;
      RunFrame(&chain, on, true, msaa);
      const auto got = ReadFloats(chain.output_ms);
      double most = 0;
      for (size_t i = 0; i < got.size(); ++i) {
        for (size_t c = 0; c < 3; ++c) {
          most = std::max(most, static_cast<double>(std::abs(got[i][c] - expected[i][c]) / std::max(std::abs(expected[i][c]), 1e-3f)));
        }
      }
      const size_t prepared = Differing(single.prepared_geometry, chain.prepared_geometry) + Differing(single.prepared_light, chain.prepared_light);
      Verdict(most <= 1e-6 && prepared == 0, "  %s: max relative difference to the single-sampled %.2e; prepared texels differing %zu",
              (msaa == 1) ? "_ms prepare + _ms apply (per sample)" : "_ms prepare + _ms_pixel apply", most, prepared);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: tests.exe <repository> <before root> [items, e.g. 24; all: 123456]\n");
    return 2;
  }
  repository = argv[1];
  before_root = argv[2];
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 3;
  const auto vertex = CompileFile(Attila("light_bounce_vs.vs_5_0.hlsl"), {}, "vs_5_0");
  if (vertex.empty() || FAILED(device->CreateVertexShader(vertex.data(), vertex.size(), nullptr, &full_screen))) return 3;
  const std::string attila_folder = repository + "\\src\\games\\twattila";
  inject = PixelShader(Compile(INJECT, "inject", attila_folder, {{"LIGHT_BOUNCE_DIRECTIONAL", "1"}}, "ps_5_0"));
  copy = PixelShader(Compile(COPY, "copy", attila_folder, {}, "ps_5_0"));
  if (!inject || !copy) return 3;

  // Attila's passes: the build before's, these sources as built (checked against the build's), with the define.
  std::array<std::vector<char>, 9> before_code;
  std::array<std::vector<char>, 9> on_code;
  size_t same_as_built = 0;
  for (size_t i = 0; i < PASS_NAMES.size(); ++i) {
    const std::string source = Attila(std::string(PASS_NAMES[i]) + ".ps_5_0.hlsl");
    before_code[i] = ReadFile(before_root + "\\twattila\\" + PASS_NAMES[i] + ".cso");
    on_code[i] = CompileFile(source, {{"LIGHT_BOUNCE_DIRECTIONAL", "1"}}, "ps_5_0");
    const auto plain = CompileFile(source, {}, "ps_5_0");
    const auto built = ReadFile(repository + "\\build32\\twattila.include\\embed\\" + PASS_NAMES[i] + ".cso");
    same_as_built += (!plain.empty() && Code(plain) == Code(built)) ? 1 : 0;
    if (before_code[i].empty() || on_code[i].empty()) {
      std::printf("%s: before %zu bytes, with the define %zu bytes\n", PASS_NAMES[i], before_code[i].size(), on_code[i].size());
      return 3;
    }
  }
  Verdict(same_as_built == PASS_NAMES.size(), "the test's compile of Attila's passes (no define) = the build's code: %zu of %zu", same_as_built,
          PASS_NAMES.size());
  const Passes before = MakePasses(before_code);
  const Passes on = MakePasses(on_code);

  const std::string items = (argc > 3) ? argv[3] : "123456";
  if (items.find('1') != std::string::npos) RowOff(before, on, on_code);
  if (items.find('2') != std::string::npos) FlatRoom(on);
  if (items.find('3') != std::string::npos) UniformSky(on);
  if (items.find('4') != std::string::npos) ChainChecks(on);
  if (items.find('5') != std::string::npos) RewriteOrder(on);
  if (items.find('6') != std::string::npos) Bumpy(on);
  std::printf(failures == 0 ? "\nPASS\n" : "\n%d FAILED\n", failures);
  return failures;
}
