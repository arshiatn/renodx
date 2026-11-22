// GT-VBAO's per-sample math variants (twattila/bitmask.hlsli BITMASK_VARIANT; G15's A/B, g15_gtvbao_ab.md): Attila's AO
// pass (ao_bitmask.ps_5_0.hlsl over bitmask_ssao.hlsli, compiled here as the build does: ps_5_0, /O3) at V0 (today), V1
// (the cosine domain, each side's early stop) and V2 (V1, the cosine shares' per-slice terms, z in 2 terms), the same
// picture; V3 (V2, sectors equal in angle weighted at the slice's end: ao_bitmask_fast, the Experimental "Faster GT-VBAO"
// row, a look change) passes with its mean |error| against the reference within 10% of V0's, its flat numbers listed.
// 1. V0 unchanged: the tree's V0 bytes = the base's (run.ps1 -Base: its bitmask.hlsli / bitmask_ssao.hlsli served in
//    place of the tree's).
// 2. Walls, box and poles (warp_tests/ao/gtvbao_test.cpp's scene, 192 x 108, radius 1 m, thickness 0.15, Quality 2,
//    Directions 2), Auto Thickness off and on: per variant, 32 noise frames: mean AO, mean |error| against a CPU
//    reference (512 cosine rays marched against the same depth), mean |delta| against V0, and over every frame's pixels
//    how many lie one sector apart from V0 (1/(32 x slices)) and how many more. PNGs: V0 | variant | |delta| x 8.
// 3. Flat ground over a pitch sweep (5-60 degrees, the plane 2 m below): mean, lowest and the share under 0.95 per
//    variant (the open plane reads ~1; the share under 0.95 must not grow).
// Writes ao_bitmask_v0 / v1 / v2.cso beside the PNGs (for the timing runs).
// Pass (WARP): V0's bytes the base's; per variant mean |delta| < 1e-4 and at most 0.01% of pixel frames one sector
// apart, none more (G15's gate; on WARP, as the 5090's gate runs it: argv[3] "hw" on the GPU); the flat share under 0.95
// not above V0's. CONTROL: V0 against itself, 0 apart (else the frames aren't deterministic and the counts say nothing).
// tests.exe <tree> <base copies folder or -> <png folder> [hw]
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
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace attila {
#include "src/games/twattila/shared.h"
}

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT W = 192, H = 108;
constexpr float THICKNESS = 0.15f;
constexpr int FRAMES = 32;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
int failures = 0;

std::vector<char> ReadAll(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

// The tree's includes, with bitmask.hlsli / bitmask_ssao.hlsli from `base` when set.
class Includes : public ID3DInclude {
 public:
  Includes(std::filesystem::path root, std::filesystem::path base) : root_(std::move(root)), base_(std::move(base)) {}
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override {
    const auto from = (parent != nullptr && folders_.contains(parent)) ? folders_[parent] : root_;
    auto path = (from / name).lexically_normal();
    const auto file = path.filename().string();
    if (!base_.empty() && (file == "bitmask.hlsli" || file == "bitmask_ssao.hlsli")) path = base_ / file;
    auto text = std::make_unique<std::vector<char>>(ReadAll(path));
    if (text->empty()) return E_FAIL;
    *data = text->data();
    *bytes = static_cast<UINT>(text->size());
    folders_[text->data()] = (from / name).lexically_normal().parent_path();
    texts_.push_back(std::move(text));
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID) override { return S_OK; }

 private:
  std::filesystem::path root_, base_;
  std::map<LPCVOID, std::filesystem::path> folders_;
  std::vector<std::unique_ptr<std::vector<char>>> texts_;
};

std::vector<uint8_t> Compile(const std::filesystem::path& tree, const std::filesystem::path& base, int variant) {
  // V3: its own shader (ao_bitmask_fast, the Experimental row's), which sets the variant itself.
  const auto shader = tree / ((variant == 3) ? "src/games/twattila/ao_bitmask_fast.ps_5_0.hlsl" : "src/games/twattila/ao_bitmask.ps_5_0.hlsl");
  const auto source = ReadAll(shader);
  const std::string value = std::to_string(variant);
  const D3D_SHADER_MACRO defines[] = {{"BITMASK_VARIANT", value.c_str()}, {nullptr, nullptr}};
  Includes includes(shader.parent_path(), base);
  ComPtr<ID3DBlob> code, errors;
  if (FAILED(D3DCompile(source.data(), source.size(), shader.string().c_str(), (variant == 0 || variant == 3) ? nullptr : defines, &includes, "main", "ps_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
    std::printf("compile V%d%s failed: %s\n", variant, base.empty() ? "" : " (base)",
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    return {};
  }
  const auto* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
  return {bytes, bytes + code->GetBufferSize()};
}

ComPtr<ID3D11Buffer> Constants(const void* data, UINT size) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = (size + 15u) & ~15u, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> bytes(desc.ByteWidth);
  std::memcpy(bytes.data(), data, size);
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = bytes.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &init, &buffer);
  return buffer;
}

struct V3 {
  float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Normalize(V3 a) { return a * (1.f / std::sqrt(Dot(a, a))); }

struct Scene {
  float tan_half_x = 0.f, tan_half_y = 0.f;
  std::vector<float> depth;  // 0.1 / z, 0 the sky
  float SurfaceZ(int x, int y) const {
    if (x < 0 || y < 0 || x >= static_cast<int>(W) || y >= static_cast<int>(H)) return 0.f;
    const float d = depth[static_cast<size_t>(y) * W + x];
    return (d > 0.f) ? (0.1f / d) : 0.f;
  }
  V3 Position(float px, float py, float z) const { return {(px / W * 2.f - 1.f) * tan_half_x * z, (1.f - py / H * 2.f) * tan_half_y * z, z}; }
};

// gtvbao_test.cpp's: the ground 2 below, a wall, a box's front, three thin poles; or (pitch > 0) the ground alone seen
// `pitch` degrees down.
Scene MakeScene(float pitch) {
  Scene scene;
  const float fov = 60.f * 3.14159265f / 180.f;
  scene.tan_half_y = std::tan(0.5f * fov);
  scene.tan_half_x = scene.tan_half_y * W / H;
  scene.depth.assign(static_cast<size_t>(W) * H, 0.f);
  const float down = pitch * 3.14159265f / 180.f;
  for (UINT y = 0; y < H; ++y) {
    for (UINT x = 0; x < W; ++x) {
      const float dx = ((x + 0.5f) / W * 2.f - 1.f) * scene.tan_half_x;
      const float dy = (1.f - (y + 0.5f) / H * 2.f) * scene.tan_half_y;
      float z = 1e9f;
      if (pitch > 0.f) {
        const float fall = std::sin(down) - dy * std::cos(down);
        if (fall > 0.f) z = 2.f / fall;
        if (z > 1e3f) z = 1e9f;
      } else {
        if (dy < 0.f) z = 2.f / -dy;
        if (dx * 9.f <= -1.2f && 9.f < z) z = 9.f;
        if (dx * 7.f >= 0.f && dx * 7.f <= 1.5f && dy * 7.f >= -2.f && dy * 7.f <= -0.8f && 7.f < z) z = 7.f;
        for (int p = 0; p < 3; ++p) {
          const float px = -0.6f + 0.5f * p;
          if (dx * 5.f >= px && dx * 5.f <= px + 0.05f && dy * 5.f >= -2.f && dy * 5.f <= 0.5f && 5.f < z) z = 5.f;
        }
      }
      scene.depth[static_cast<size_t>(y) * W + x] = (z < 1e8f) ? (0.1f / z) : 0.f;
    }
  }
  return scene;
}

// gtvbao_test.cpp's CPU reference: 512 cosine-weighted rays per pixel, marched to the radius against the depth.
std::vector<double> Reference(const Scene& scene) {
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> uniform(0.f, 1.f);
  std::vector<double> reference(static_cast<size_t>(W) * H, 1.0);
  for (UINT y = 0; y < H; ++y) {
    for (UINT x = 0; x < W; ++x) {
      const float z = scene.SurfaceZ(x, y);
      if (z <= 0.f) continue;
      const V3 p = scene.Position(x + 0.5f, y + 0.5f, z);
      const auto at = [&](int px, int py) {
        const float sz = scene.SurfaceZ(px, py);
        return scene.Position(px + 0.5f, py + 0.5f, (sz > 0.f) ? sz : 1e6f);
      };
      const V3 r = at(x + 1, y), l = at(x - 1, y), d = at(x, y + 1), u = at(x, y - 1);
      const V3 tx = (std::fabs(r.z - p.z) < std::fabs(p.z - l.z)) ? (r - p) : (p - l);
      const V3 ty = (std::fabs(d.z - p.z) < std::fabs(p.z - u.z)) ? (d - p) : (p - u);
      V3 n = Normalize(Cross(tx, ty));
      if (Dot(n, Normalize(p * -1.f)) < 0.f) n = n * -1.f;
      const V3 a = (std::fabs(n.x) < 0.9f) ? V3{1, 0, 0} : V3{0, 1, 0};
      const V3 t = Normalize(Cross(a, n));
      const V3 b = Cross(n, t);
      const float pixel_size = 2.f * z * scene.tan_half_y / H;
      int hits = 0;
      constexpr int RAYS = 512;
      for (int ray = 0; ray < RAYS; ++ray) {
        const float r1 = uniform(rng), r2 = uniform(rng);
        const float sr = std::sqrt(r1);
        const V3 dir = Normalize(t * (sr * std::cos(6.2831853f * r2)) + b * (sr * std::sin(6.2831853f * r2)) + n * std::sqrt(1.f - r1));
        bool hit = false;
        for (float s = 1.5f * pixel_size; s <= 1.f && !hit; s += 0.5f * pixel_size) {
          const V3 q = p + dir * s;
          if (q.z <= 0.f) break;
          const float px = (q.x / (q.z * scene.tan_half_x) * 0.5f + 0.5f) * W;
          const float py = (0.5f - q.y / (q.z * scene.tan_half_y) * 0.5f) * H;
          const float sz = scene.SurfaceZ(static_cast<int>(px), static_cast<int>(py));
          if (sz <= 0.f) continue;
          const float sthick = THICKNESS * (1.f + 0.02f * sz);
          const V3 sp = scene.Position(std::floor(px) + 0.5f, std::floor(py) + 0.5f, sz);
          if (q.z > sz && q.z < sz + sthick && Dot(sp - p, sp - p) <= 1.f) hit = true;
        }
        hits += hit ? 1 : 0;
      }
      reference[static_cast<size_t>(y) * W + x] = 1.0 - static_cast<double>(hits) / RAYS;
    }
  }
  return reference;
}

// `frames` frames of the shader over the scene: each frame's AO (frame-major).
std::vector<float> Draw(const std::vector<uint8_t>& code, const Scene& scene, bool auto_thickness) {
  ComPtr<ID3D11PixelShader> ps;
  if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &ps))) return {};
  const D3D11_TEXTURE2D_DESC depth_desc = {.Width = W, .Height = H, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                           .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA depth_data = {.pSysMem = scene.depth.data(), .SysMemPitch = W * 4};
  ComPtr<ID3D11Texture2D> depth_texture;
  device->CreateTexture2D(&depth_desc, &depth_data, &depth_texture);
  ComPtr<ID3D11ShaderResourceView> depth_view;
  device->CreateShaderResourceView(depth_texture.Get(), nullptr, &depth_view);
  // camera_VS_PS: inv_projection (c17-c20) z = 1 / (0.1 x depth), g_screen_size, g_viewport_dimensions.
  std::array<float, 36 * 4> camera = {};
  camera[19 * 4 + 3] = 0.1f;
  camera[20 * 4 + 2] = 1.f;
  for (const int reg : {27, 29}) {
    camera[reg * 4 + 0] = W;
    camera[reg * 4 + 1] = H;
    camera[reg * 4 + 2] = 1.f / W;
    camera[reg * 4 + 3] = 1.f / H;
  }
  std::array<float, 12> ssao = {};
  ssao[2] = 1.f;
  ssao[6] = W;
  ssao[7] = H;
  ssao[8] = 60.f * 3.14159265f / 180.f;
  ssao[9] = 1.f;
  const std::array<ComPtr<ID3D11Buffer>, 2> buffers = {Constants(camera.data(), sizeof(camera)), Constants(ssao.data(), sizeof(ssao))};
  const char* vs_source =
      "void main(uint id : SV_VertexID, out float4 p : SV_Position0, out float2 t0 : TEXCOORD0, out float2 t1 : TEXCOORD1) {"
      "  float2 uv = float2((id << 1) & 2, id & 2); p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); t0 = uv; t1 = uv; }";
  ComPtr<ID3DBlob> vs_blob;
  D3DCompile(vs_source, std::strlen(vs_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, nullptr);
  ComPtr<ID3D11VertexShader> vs;
  device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs);
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxAnisotropy = 1, .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, &sampler);
  D3D11_TEXTURE2D_DESC target_desc = {.Width = W, .Height = H, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                      .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  device->CreateTexture2D(&target_desc, nullptr, &target);
  ComPtr<ID3D11RenderTargetView> rtv;
  device->CreateRenderTargetView(target.Get(), nullptr, &rtv);
  target_desc.Usage = D3D11_USAGE_STAGING;
  target_desc.BindFlags = 0;
  target_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&target_desc, nullptr, &staging);
  std::vector<float> values;
  for (int frame = 1; frame <= FRAMES; ++frame) {
    attila::ShaderInjectData injection = {};
    injection.ssao_strength = 1.f;
    injection.ssao_radius = 1.f;
    injection.ssao_thickness = THICKNESS;
    injection.ssao_quality = 2.f;
    injection.ssao_directions = 2.f;
    injection.ssao_auto_thickness = auto_thickness ? 1.f : 0.f;
    injection.ssao_frame = static_cast<float>(frame);
    const auto injection_buffer = Constants(&injection, sizeof(injection));
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* raw[2] = {buffers[0].Get(), buffers[1].Get()};
    context->PSSetConstantBuffers(0, 2, raw);
    context->PSSetConstantBuffers(13, 1, injection_buffer.GetAddressOf());
    context->PSSetShaderResources(0, 1, depth_view.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(W), .Height = static_cast<float>(H), .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    context->Draw(3, 0);
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    for (UINT y = 0; y < H; ++y) {
      const float* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + size_t{y} * mapped.RowPitch);
      for (UINT x = 0; x < W; ++x) values.push_back(row[x * 4]);
    }
    context->Unmap(staging.Get(), 0);
  }
  return values;
}

// An 8-bit grey PNG (stored deflate blocks).
void WritePng(const std::filesystem::path& path, const std::vector<uint8_t>& grey, UINT width, UINT height) {
  std::array<uint32_t, 256> table = {};
  for (uint32_t n = 0; n < 256; ++n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = ((c & 1u) != 0u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    table[n] = c;
  }
  std::vector<uint8_t> raw;
  for (UINT y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), grey.begin() + static_cast<ptrdiff_t>(size_t{y} * width), grey.begin() + static_cast<ptrdiff_t>(size_t{y + 1} * width));
  }
  std::vector<uint8_t> zlib = {0x78, 0x01};
  for (size_t at = 0; at < raw.size(); at += 65535) {
    const auto length = static_cast<uint16_t>(std::min<size_t>(65535, raw.size() - at));
    zlib.push_back((at + length >= raw.size()) ? 1 : 0);
    zlib.insert(zlib.end(), {static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(~length),
                             static_cast<uint8_t>(~length >> 8)});
    zlib.insert(zlib.end(), raw.begin() + static_cast<ptrdiff_t>(at), raw.begin() + static_cast<ptrdiff_t>(at + length));
  }
  uint32_t s1 = 1, s2 = 0;
  for (const uint8_t byte : raw) {
    s1 = (s1 + byte) % 65521;
    s2 = (s2 + s1) % 65521;
  }
  const uint32_t adler = (s2 << 16) | s1;
  zlib.insert(zlib.end(), {static_cast<uint8_t>(adler >> 24), static_cast<uint8_t>(adler >> 16), static_cast<uint8_t>(adler >> 8), static_cast<uint8_t>(adler)});
  std::ofstream file(path, std::ios::binary);
  const auto big = [](uint32_t v) { return std::array<char, 4>{static_cast<char>(v >> 24), static_cast<char>(v >> 16), static_cast<char>(v >> 8), static_cast<char>(v)}; };
  const auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
    file.write(big(static_cast<uint32_t>(data.size())).data(), 4);
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    uint32_t crc = 0xFFFFFFFFu;
    for (const uint8_t byte : body) crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    file.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    file.write(big(crc ^ 0xFFFFFFFFu).data(), 4);
  };
  file.write("\x89PNG\r\n\x1a\n", 8);
  std::vector<uint8_t> header(13, 0);
  const auto w = big(width), h = big(height);
  std::memcpy(header.data(), w.data(), 4);
  std::memcpy(header.data() + 4, h.data(), 4);
  header[8] = 8;
  chunk("IHDR", header);
  chunk("IDAT", zlib);
  chunk("IEND", {});
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) return 2;
  const std::filesystem::path tree = argv[1], base = (std::string(argv[2]) == "-") ? std::filesystem::path() : std::filesystem::path(argv[2]),
                              pngs = argv[3];
  const bool hardware = argc > 4 && std::string(argv[4]) == "hw";
  if (FAILED(D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                               nullptr, &context))) {
    return 1;
  }
  std::filesystem::create_directories(pngs);
  std::printf("device: %s\n", hardware ? "hardware" : "WARP");

  // 1.
  const std::array<std::vector<uint8_t>, 4> codes = {Compile(tree, {}, 0), Compile(tree, {}, 1), Compile(tree, {}, 2), Compile(tree, {}, 3)};
  if (std::ranges::any_of(codes, [](const auto& code) { return code.empty(); })) return 1;
  // The variants' bytes beside the PNGs (the timing harness's input).
  for (int variant = 0; variant < 4; ++variant) {
    std::ofstream(pngs / ("ao_bitmask_v" + std::to_string(variant) + ".cso"), std::ios::binary)
        .write(reinterpret_cast<const char*>(codes[variant].data()), static_cast<std::streamsize>(codes[variant].size()));
  }
  if (!base.empty()) {
    const auto base_code = Compile(tree, base, 0);
    const bool same = !base_code.empty() && base_code == codes[0];
    std::printf("1 V0 bytes = the base's: %s (%zu bytes; V1 %zu, V2 %zu)\n", same ? "ok" : "FAIL", codes[0].size(), codes[1].size(), codes[2].size());
    failures += same ? 0 : 1;
  }

  // 2.
  const Scene scene = MakeScene(0.f);
  const auto reference = Reference(scene);
  // One sector of one slice (Directions 2: 4 slices) moves the result by 1 / 128.
  constexpr float SECTOR = 1.f / (32.f * 4.f);
  for (const bool auto_thickness : {false, true}) {
    std::array<std::vector<float>, 4> drawn;
    for (int variant = 0; variant < 4; ++variant) drawn[variant] = Draw(codes[variant], scene, auto_thickness);
    const auto again = Draw(codes[0], scene, auto_thickness);
    const bool deterministic = again == drawn[0];
    std::printf("2 scene, Auto Thickness %s: CONTROL V0 twice bit-equal: %s\n", auto_thickness ? "on" : "off", deterministic ? "ok" : "FAIL");
    failures += deterministic ? 0 : 1;
    double error0 = 0.0;
    for (int variant = 0; variant < 4; ++variant) {
      double error = 0.0, mean = 0.0, delta = 0.0;
      uint64_t pixels = 0, sector = 0, more = 0, frames_pixels = 0;
      std::vector<uint8_t> image(size_t{W} * 3 * H, 0);
      for (UINT y = 0; y < H; ++y) {
        for (UINT x = 0; x < W; ++x) {
          const size_t at = size_t{y} * W + x;
          if (scene.depth[at] <= 0.f) continue;
          double average = 0.0, average0 = 0.0, frame_delta = 0.0;
          for (int frame = 0; frame < FRAMES; ++frame) {
            const size_t index = size_t{static_cast<size_t>(frame)} * W * H + at;
            const float apart = std::fabs(drawn[variant][index] - drawn[0][index]);
            average += drawn[variant][index];
            average0 += drawn[0][index];
            frame_delta += apart;
            ++frames_pixels;
            if (apart > 0.5f * SECTOR && apart <= 1.5f * SECTOR) ++sector;
            if (apart > 1.5f * SECTOR) ++more;
          }
          average /= FRAMES;
          average0 /= FRAMES;
          error += std::fabs(average - reference[at]);
          mean += average;
          delta += frame_delta / FRAMES;
          ++pixels;
          image[size_t{y} * W * 3 + x] = static_cast<uint8_t>(std::clamp(average0, 0.0, 1.0) * 255.0 + 0.5);
          image[size_t{y} * W * 3 + W + x] = static_cast<uint8_t>(std::clamp(average, 0.0, 1.0) * 255.0 + 0.5);
          image[size_t{y} * W * 3 + 2 * W + x] = static_cast<uint8_t>(std::clamp(8.0 * frame_delta / FRAMES, 0.0, 1.0) * 255.0 + 0.5);
        }
      }
      const double share = static_cast<double>(sector) / frames_pixels;
      if (variant == 0) error0 = error / pixels;
      // V1 / V2 the same picture; V3 (a look change) its error against the reference within 10% of V0's.
      const bool ok = (variant == 0) || ((variant == 3) ? (error / pixels <= 1.1 * error0) : (delta / pixels < 1e-4 && share <= 1e-4 && more == 0));
      std::printf("  V%d: mean AO %.4f, |error| vs reference %.4f, mean |delta| vs V0 %.2e, pixel frames one sector apart %llu (%.4f%%), more %llu: %s\n",
                  variant, mean / pixels, error / pixels, delta / pixels, static_cast<unsigned long long>(sector), 100.0 * share,
                  static_cast<unsigned long long>(more), (variant == 0) ? "(V0)" : (ok ? "ok" : "FAIL"));
      failures += ok ? 0 : 1;
      if (variant != 0) {
        WritePng(pngs / ("scene_v" + std::to_string(variant) + (auto_thickness ? "_auto" : "") + ".png"), image, W * 3, H);
      }
    }
  }

  // 3.
  std::printf("3 flat ground (Auto Thickness on): pitch: per variant mean / lowest / share under 0.95\n");
  for (const float pitch : {5.f, 13.6f, 20.f, 30.f, 45.f, 60.f}) {
    const Scene ground = MakeScene(pitch);
    std::printf("  %4.1f:", pitch);
    double under0 = 0.0;
    for (int variant = 0; variant < 4; ++variant) {
      const auto values = Draw(codes[variant], ground, true);
      double mean = 0.0, lowest = 1.0, under = 0.0;
      uint64_t count = 0;
      for (int frame = 0; frame < FRAMES; ++frame) {
        for (size_t at = 0; at < size_t{W} * H; ++at) {
          if (ground.depth[at] <= 0.f) continue;
          const float value = values[size_t{static_cast<size_t>(frame)} * W * H + at];
          mean += value;
          lowest = std::min<double>(lowest, value);
          under += (value < 0.95f) ? 1.0 : 0.0;
          ++count;
        }
      }
      mean /= count;
      under /= count;
      if (variant == 0) under0 = under;
      // V3 listed (a look change: ATN's images).
      const bool ok = variant == 0 || variant == 3 || under <= under0;
      failures += ok ? 0 : 1;
      std::printf("  V%d %.4f / %.4f / %.4f%s", variant, mean, lowest, under, ok ? "" : " FAIL");
    }
    std::printf("\n");
  }
  std::printf(failures == 0 ? "GTVBAO VARIANTS ALL PASSED\n" : "GTVBAO VARIANTS: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
