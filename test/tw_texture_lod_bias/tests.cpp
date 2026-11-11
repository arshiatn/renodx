// Texture LOD Bias on WARP (texture_lod_bias.hpp): which of D3D11's sample instructions follow a sampler's MipLODBias
// (Warhammer III's G-buffer shaders: sample, sample_l, sample_b, sample_d), and which of the game's samplers the addon's
// clones replace in the main view (anisotropic always, wrapping trilinear with SetWrappingTrilinear, others never).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
#include "test/tw_sampler_lifetime/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;

void Check(bool good, const char* what) {
  if (!good) {
    printf("FAIL: %s\n", what);
    fflush(stdout);
    ExitProcess(1);
  }
}

ComPtr<ID3DBlob> Compile(const char* source, const char* target) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, &code, &errors))) {
    printf("FAIL: compile %s: %s\n", target, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    ExitProcess(1);
  }
  return code;
}

int main() {
  namespace lod = twattila::texture_lod_bias;
  mock::Device device;
  mock::CommandList commands;
  commands.owner = &device;
  Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device.native, nullptr, &commands.native)), "device");
  mock::Queue queue;
  queue.commands = &commands;
  ID3D11Device* native = device.native.Get();
  ID3D11DeviceContext* context = commands.native.Get();

  // A 64 x 64 texture whose mip m holds m everywhere; an 8 x 8 target over its whole width: level 3.
  constexpr UINT SIZE = 64;
  constexpr UINT MIPS = 7;
  constexpr UINT TARGET = 8;
  std::vector<std::vector<float>> levels;
  std::vector<D3D11_SUBRESOURCE_DATA> initial;
  for (UINT mip = 0; mip < MIPS; ++mip) {
    levels.emplace_back((SIZE >> mip) * (SIZE >> mip), static_cast<float>(mip));
  }
  for (UINT mip = 0; mip < MIPS; ++mip) {
    initial.push_back({.pSysMem = levels[mip].data(), .SysMemPitch = (SIZE >> mip) * 4});
  }
  const D3D11_TEXTURE2D_DESC texture_desc = {.Width = SIZE, .Height = SIZE, .MipLevels = MIPS, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                             .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> texture_srv;
  Check(SUCCEEDED(native->CreateTexture2D(&texture_desc, initial.data(), &texture)), "texture");
  Check(SUCCEEDED(native->CreateShaderResourceView(texture.Get(), nullptr, &texture_srv)), "texture view");
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = TARGET, .Height = TARGET, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                            .SampleDesc = {1, 0}, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_rtv;
  Check(SUCCEEDED(native->CreateTexture2D(&target_desc, nullptr, &target)), "target");
  Check(SUCCEEDED(native->CreateRenderTargetView(target.Get(), nullptr, &target_rtv)), "target view");
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.BindFlags = 0;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  Check(SUCCEEDED(native->CreateTexture2D(&staging_desc, nullptr, &staging)), "staging");

  const auto vertex_code = Compile(
      "void main(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0) {"
      "  uv = float2((id << 1) & 2, id & 2); position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }",
      "vs_5_0");
  ComPtr<ID3D11VertexShader> vertex_shader;
  Check(SUCCEEDED(native->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &vertex_shader)), "vertex shader");
  struct Op {
    const char* name;
    const char* body;
    ComPtr<ID3D11PixelShader> shader;
  };
  std::array<Op, 4> ops = {{{"sample", "t.Sample(s, uv)"},
                            {"sample_l (level 3)", "t.SampleLevel(s, uv, 3)"},
                            {"sample_b (bias 0)", "t.SampleBias(s, uv, 0)"},
                            {"sample_d", "t.SampleGrad(s, uv, ddx(uv), ddy(uv))"}}};
  for (auto& op : ops) {
    const std::string source = std::string("Texture2D<float> t : register(t0); SamplerState s : register(s0);"
                                           "float main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return ")
                               + op.body + "; }";
    const auto code = Compile(source.c_str(), "ps_5_0");
    Check(SUCCEEDED(native->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &op.shader)), "pixel shader");
  }
  // The level the sampler bound at s0 gives an op (no sampler bound here: the slot as it is).
  const auto level = [&](ID3D11PixelShader* shader) {
    const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(TARGET), static_cast<float>(TARGET), 0.f, 1.f};
    context->OMSetRenderTargets(1, target_rtv.GetAddressOf(), nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(shader, nullptr, 0);
    context->PSSetShaderResources(0, 1, texture_srv.GetAddressOf());
    context->Draw(3, 0);
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map");
    const float value = static_cast<const float*>(mapped.pData)[(TARGET / 2) * (mapped.RowPitch / 4) + TARGET / 2];
    context->Unmap(staging.Get(), 0);
    return value;
  };

  // 1. D3D11 itself: each op with the game's two filters, the sampler's MipLODBias 0 and -2.
  struct Native {
    const char* name;
    D3D11_FILTER filter;
  };
  for (const Native& filter : {Native{"anisotropic", D3D11_FILTER_ANISOTROPIC}, Native{"trilinear", D3D11_FILTER_MIN_MAG_MIP_LINEAR}}) {
    for (const auto& op : ops) {
      std::array<float, 2> got = {};
      for (const int biased : {0, 1}) {
        const D3D11_SAMPLER_DESC desc = {.Filter = filter.filter, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP, .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
                                         .AddressW = D3D11_TEXTURE_ADDRESS_WRAP, .MipLODBias = biased ? -2.f : 0.f, .MaxAnisotropy = 16,
                                         .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
        ComPtr<ID3D11SamplerState> sampler;
        Check(SUCCEEDED(native->CreateSamplerState(&desc, &sampler)), "native sampler");
        context->PSSetSamplers(0, 1, sampler.GetAddressOf());
        got[biased] = level(op.shader.Get());
      }
      printf("%-11s %-20s level %.2f, with MipLODBias -2: %.2f\n", filter.name, op.name, got[0], got[1]);
      Check(std::abs(got[0] - 3.f) < 0.01f, "the op's own level is 3");
      // The spec: the sampler's bias joins the computed level (sample, sample_b, sample_d) and the given one (sample_l).
      Check(std::abs(got[1] - 1.f) < 0.01f, "MipLODBias -2 gives level 1");
    }
  }

  // 2. The addon: the game's samplers bound through ReShade's events in the main view at a bias of -2.
  const auto make = [&](reshade::api::filter_mode filter, reshade::api::texture_address_mode address) {
    reshade::api::sampler_desc desc = {};
    desc.filter = filter;
    desc.address_u = desc.address_v = desc.address_w = address;
    desc.max_anisotropy = 16.f;
    reshade::api::sampler sampler = {};
    Check(device.create_sampler(desc, &sampler), "the game's sampler");
    return sampler;
  };
  using reshade::api::filter_mode;
  using reshade::api::texture_address_mode;
  const auto anisotropic = make(filter_mode::anisotropic, texture_address_mode::wrap);
  const auto trilinear_wrap = make(filter_mode::min_mag_mip_linear, texture_address_mode::wrap);
  const auto trilinear_clamp = make(filter_mode::min_mag_mip_linear, texture_address_mode::clamp);
  const auto point_wrap = make(filter_mode::min_mag_mip_point, texture_address_mode::wrap);
  lod::SetMainView([] { return true; });
  lod::SetBias(-2.f);
  lod::SetTracking(&queue, true);
  // The game's PSSetSamplers, then ReShade's push_descriptors; the level the bound sampler gives sample.
  const auto drawn_with = [&](reshade::api::sampler sampler) {
    auto* game = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
    context->PSSetSamplers(0, 1, &game);
    lod::OnPushDescriptors(&commands, reshade::api::shader_stage::pixel, {0}, 0,
                           {.binding = 0, .count = 1, .type = reshade::api::descriptor_type::sampler, .descriptors = &sampler});
    return level(ops[0].shader.Get());
  };
  Check(std::abs(drawn_with(anisotropic) - 1.f) < 0.01f, "an anisotropic sampler is biased");
  Check(std::abs(drawn_with(trilinear_wrap) - 3.f) < 0.01f, "a wrapping trilinear sampler stays the game's by default");
  lod::SetWrappingTrilinear(true);
  Check(std::abs(drawn_with(trilinear_wrap) - 1.f) < 0.01f, "with SetWrappingTrilinear a wrapping trilinear sampler is biased");
  Check(std::abs(drawn_with(trilinear_clamp) - 3.f) < 0.01f, "a clamping trilinear sampler (post passes) never");
  Check(std::abs(drawn_with(point_wrap) - 3.f) < 0.01f, "a point sampler never");
  const std::string report = lod::Report();
  printf("%s\n", report.c_str());
  Check(report.find("1 anisotropic, 1 trilinear wrapping (biased), 2 others; 2 of them with a biased copy now") != std::string::npos,
        "Log Details' line counts the samplers");
  lod::SetWrappingTrilinear(false);
  Check(std::abs(drawn_with(trilinear_wrap) - 3.f) < 0.01f, "SetWrappingTrilinear off: the game's sampler again");
  lod::SetMainView([] { return false; });
  lod::OnBindViewports(&commands, 0, 0, nullptr);
  Check(std::abs(drawn_with(anisotropic) - 3.f) < 0.01f, "outside the main view the game's sampler");
  lod::SetTracking(&queue, false);
  for (const auto sampler : {anisotropic, trilinear_wrap, trilinear_clamp, point_wrap}) device.destroy_sampler(sampler);
  context->ClearState();
  lod::Shutdown();
  Check(lod::SamplerCount() == 0, "shutdown clears the samplers");
  puts("PASS: MipLODBias moves sample, sample_l, sample_b and sample_d alike (anisotropic and trilinear); the clones replace anisotropic samplers, wrapping trilinear ones only with SetWrappingTrilinear, clamping and point ones never; Log Details' counts");
}
