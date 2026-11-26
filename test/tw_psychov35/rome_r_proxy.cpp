// Rome Remastered's swap chain proxy (twromeremastered/swap_chain_proxy_pixel_shader.ps_5_x.hlsl) on WARP, compiled by
// rome_r_proxy.ps1 from this tree and from its -Before with the build's flags. rome_r_proxy.exe <before.cso> <after.cso>
// O-CO16: the 8 other TW proxies' target-gamut negative clamp and Clamp Peak (twattila's :70-81), at HDR10 and scRGB:
// - Rendering Mode 3 (Peak 1000 nits, UI 203): white at 10x the UI's white leaves at Peak; at 2x, unchanged;
// - Rendering Mode 0 (SDR): the ceiling is max(Paper White, UI) = 203 nits, so 2x leaves at 203;
// - a colour with a BT.2020 channel under 0 ((-1, 0.2, 0) BT.709) leaves with that channel at 0.
// Control that must fail: -Before's proxy (both clamps commented out) passes the over-Peak whites, and at scRGB the
// negative channel (HDR10's PQ encode drops it itself).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace romer {
#include "src/games/twromeremastered/shared.h"
}  // namespace romer

using Microsoft::WRL::ComPtr;
using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

struct Case {
  Float3 bt709;  // linear, x the UI's white = nits
  float mode;
  Float3 expected_bt2020_nits;
};

static Float3 Multiply(const float m[3][3], const Float3& c) {
  return {m[0][0] * c[0] + m[0][1] * c[1] + m[0][2] * c[2], m[1][0] * c[0] + m[1][1] * c[1] + m[1][2] * c[2],
          m[2][0] * c[0] + m[2][1] * c[1] + m[2][2] * c[2]};
}

constexpr float BT709_TO_BT2020[3][3] = {
    {0.6274040f, 0.3292820f, 0.0433136f}, {0.0690970f, 0.9195400f, 0.0113612f}, {0.0163916f, 0.0880132f, 0.8955950f}};
constexpr float BT2020_TO_BT709[3][3] = {
    {1.6604910f, -0.5876411f, -0.0728499f}, {-0.1245505f, 1.1328999f, -0.0083494f}, {-0.0181508f, -0.1005789f, 1.1187297f}};

static float PQToNits(float code) {
  const float p = std::pow(std::max(code, 0.f), 1.f / 78.84375f);
  return 10000.f * std::pow(std::max(p - 0.8359375f, 0.f) / (18.8515625f - 18.6875f * p), 1.f / 0.1593017578f);
}

// The proxy's output as BT.2020 nits: HDR10 decoded from PQ, scRGB (BT.709, 1 = 80 nits) converted.
static std::vector<Float3> Run(const std::filesystem::path& path, const std::vector<Case>& cases, float encoding) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<char> code{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  REQUIRE(!code.empty());
  ComPtr<ID3D11PixelShader> pixel;
  REQUIRE(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &pixel)));
  const char* vertex_source =
      "struct O { float4 position : SV_Position; float2 uv : TEXCOORD0; };"
      "O main(uint id : SV_VertexID) { O o; o.uv = float2((id << 1) & 2, id & 2);"
      "o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }";
  ComPtr<ID3DBlob> vertex_code;
  REQUIRE(SUCCEEDED(D3DCompile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertex_code,
                               nullptr)));
  ComPtr<ID3D11VertexShader> vertex;
  REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &vertex)));
  const UINT width = static_cast<UINT>(cases.size());
  std::vector<Float4> frame;
  // The frame is gamma 2.2 (the proxy's gamma::DecodeSafe), sign-safe.
  for (const auto& c : cases) {
    Float4 texel = {0.f, 0.f, 0.f, 1.f};
    for (int i = 0; i < 3; ++i) texel[i] = std::copysign(std::pow(std::abs(c.bt709[i]), 1.f / 2.2f), c.bt709[i]);
    frame.push_back(texel);
  }
  const D3D11_TEXTURE2D_DESC input_desc = {width, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA input_init = {frame.data(), width * 16, 0};
  ComPtr<ID3D11Texture2D> input;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&input_desc, &input_init, &input)));
  ComPtr<ID3D11ShaderResourceView> input_view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(input.Get(), nullptr, &input_view)));
  const D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                           D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1, D3D11_COMPARISON_NEVER, {}, 0, D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, &sampler)));
  D3D11_TEXTURE2D_DESC target_desc = input_desc;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&target_desc, nullptr, &target)));
  ComPtr<ID3D11RenderTargetView> target_view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view)));
  D3D11_TEXTURE2D_DESC staging_desc = input_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)));

  std::vector<Float3> nits;
  // One draw per case: the mode is a constant.
  for (UINT x = 0; x < width; ++x) {
    romer::ShaderInjectData si = {};
    si.peak_white_nits = 1000.f;
    si.diffuse_white_nits = 203.f;
    si.graphics_white_nits = 203.f;
    si.swap_chain_encoding = encoding;
    si.hdr = cases[x].mode;
    std::vector<uint8_t> bytes((sizeof(si) + 15u) & ~size_t{15}, 0);
    std::memcpy(bytes.data(), &si, sizeof(si));
    const D3D11_BUFFER_DESC constants_desc = {static_cast<UINT>(bytes.size()), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    const D3D11_SUBRESOURCE_DATA constants_init = {bytes.data(), 0, 0};
    ComPtr<ID3D11Buffer> constants;
    REQUIRE(SUCCEEDED(device->CreateBuffer(&constants_desc, &constants_init, &constants)));
    context->ClearState();
    context->PSSetConstantBuffers(13, 1, constants.GetAddressOf());
    context->PSSetShaderResources(0, 1, input_view.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(width), 1.f, 0.f, 1.f};
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetShader(pixel.Get(), nullptr, 0);
    context->Draw(3, 0);
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    Float4 out;
    std::memcpy(out.data(), static_cast<const uint8_t*>(mapped.pData) + x * 16, 16);
    context->Unmap(staging.Get(), 0);
    nits.push_back(encoding == 0.f ? Float3{PQToNits(out[0]), PQToNits(out[1]), PQToNits(out[2])}
                                   : Multiply(BT709_TO_BT2020, Float3{out[0] * 80.f, out[1] * 80.f, out[2] * 80.f}));
  }
  return nits;
}

static int Errors(const char* name, const std::vector<Case>& cases, const std::vector<Float3>& nits, float encoding) {
  int errors = 0;
  for (size_t i = 0; i < cases.size(); ++i) {
    const Float3& e = cases[i].expected_bt2020_nits;
    const float scale = std::max({std::abs(e[0]), std::abs(e[1]), std::abs(e[2]), 1.f});
    bool ok = true;
    // PQ through float32 near 0 is about 1e-3 nits off; the scale bound 2e-3.
    for (int c = 0; c < 3; ++c) ok = ok && std::abs(nits[i][c] - e[c]) <= 2e-3f * scale;
    if (!ok) {
      std::printf("  %s output %g case %zu: %.6g %.6g %.6g nits, expected %.6g %.6g %.6g\n", name, encoding, i, nits[i][0], nits[i][1],
                  nits[i][2], e[0], e[1], e[2]);
      ++errors;
    }
  }
  return errors;
}

int main(int argc, char** argv) {
  REQUIRE(argc >= 3);
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context)));
  const auto bt2020_nits = [](const Float3& bt709, float ui_nits) {
    const Float3 c = Multiply(BT709_TO_BT2020, bt709);
    return Float3{c[0] * ui_nits, c[1] * ui_nits, c[2] * ui_nits};
  };
  const Float3 negative = bt2020_nits({-1.f, 0.2f, 0.f}, 203.f);
  const std::vector<Case> cases = {
      {{10.f, 10.f, 10.f}, 3.f, {1000.f, 1000.f, 1000.f}},
      {{2.f, 2.f, 2.f}, 3.f, bt2020_nits({2.f, 2.f, 2.f}, 203.f)},
      {{2.f, 2.f, 2.f}, 0.f, {203.f, 203.f, 203.f}},
      {{-1.f, 0.2f, 0.f}, 3.f, {0.f, negative[1], negative[2]}},
      {{0.5f, 0.3f, 0.1f}, 3.f, bt2020_nits({0.5f, 0.3f, 0.1f}, 203.f)},
  };
  for (const float encoding : {0.f, 1.f}) {
    // Control: -Before passes everything unclamped (HDR10: PQ's EncodeSafe drops the negative channel itself, so there
    // only the two Peak cases tell the builds apart).
    std::vector<Case> unclamped = cases;
    for (auto& c : unclamped) {
      c.expected_bt2020_nits = bt2020_nits(c.bt709, 203.f);
      if (encoding == 0.f) for (float& channel : c.expected_bt2020_nits) channel = std::max(channel, 0.f);
    }
    const int after = Errors("after", cases, Run(argv[2], cases, encoding), encoding);
    const int before_clamped = Errors("before (clamps expected: the control)", cases, Run(argv[1], cases, encoding), encoding);
    const int before_unclamped = Errors("before (unclamped expected)", unclamped, Run(argv[1], unclamped, encoding), encoding);
    std::printf("Rome R proxy, output %s: %d cases off the clamps (control, -Before: %d off the clamps, %d off unclamped)\n",
                encoding == 0.f ? "HDR10" : "scRGB", after, before_clamped, before_unclamped);
    CHECK(after == 0);
    CHECK(before_clamped == (encoding == 0.f ? 2 : 3));
    CHECK(before_unclamped == 0);
  }
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
