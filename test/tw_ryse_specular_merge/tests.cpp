// Ryse's ray-traced specular occlusion merged into the game's screen-space reflections (rt_specular_merge, the add-on's
// built bytes) on WARP. The tiled lighting (cs 0x897232B5) takes its specular as r7 = r8.w * (r8.xyz - r18) + r18: r8
// its t6 sampled at uv = pixel / render size (trilinear), r18 the probes' (its bytes, decompiled). That use is emulated
// here: with the merged texture bound for t6 the result must be lerp(probes x occlusion, t6.rgb, t6.a), the occluded
// probes under the game's own reflections. Checked: smooth reflections (ramps) with a constant occlusion (the merge's
// sampling positions can't matter) and fully open (1: the game's own result). Reported: an occlusion ramp and noisy
// inputs (the merged texture holds its pixels' centres, the lighting reads its corners: four texels' mean, half a pixel
// up-left). Control: a merge without the colour's rescale must fail.
// Argument: the repository (build\rysesonofrome.include\embed\rt_specular_merge.cso); only read.
#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <DirectXPackedVector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

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

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

constexpr UINT WIDTH = 64;
constexpr UINT HEIGHT = 36;

// The lighting's two instructions with t6 = `merged` (u0.x: ours) and with the game's t6 and the probes occluded (u0.y:
// the intended), per channel; the third the control merge (no rescale) through the same use (u0.z). b0.x: 1 for the
// occlusion as given, 0 for open.
constexpr char LIGHTING_SHADER[] = R"(
Texture2D<float4> t_reflections : register(t0);
Texture2D<float4> t_merged : register(t1);
Texture2D<float4> t_control : register(t2);
Texture2D<float> t_occlusion : register(t3);
Texture2D<float4> t_probes : register(t4);
SamplerState s_trilinear : register(s0);
RWTexture2D<float4> u_ours : register(u0);
RWTexture2D<float4> u_intended : register(u1);
RWTexture2D<float4> u_control : register(u2);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width, height;
  u_ours.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  const float2 uv = float2(id.xy) / float2(width, height);
  const float3 probes = t_probes[id.xy].rgb;
  const float occlusion = t_occlusion[id.xy];
  const float4 game = t_reflections.SampleLevel(s_trilinear, uv, 0);
  const float4 merged = t_merged.SampleLevel(s_trilinear, uv, 0);
  const float4 control = t_control.SampleLevel(s_trilinear, uv, 0);
  const float3 occluded = probes * occlusion;
  u_intended[id.xy] = float4(game.w * (game.xyz - occluded) + occluded, 1.f);
  u_ours[id.xy] = float4(merged.w * (merged.xyz - probes) + probes, 1.f);
  u_control[id.xy] = float4(control.w * (control.xyz - probes) + probes, 1.f);
}
)";

// The control: the share merged as ours, the colour not rescaled.
constexpr char CONTROL_SHADER[] = R"(
Texture2D<float4> t_reflections : register(t0);
Texture2D<float> t_occlusion : register(t1);
SamplerState s_linear : register(s0);
RWTexture2D<float4> u_merged : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width, height;
  u_merged.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  const float4 reflected = t_reflections.SampleLevel(s_linear, (float2(id.xy) + 0.5f) / float2(width, height), 0);
  const float share = saturate(reflected.a);
  u_merged[id.xy] = float4(reflected.rgb, share + (1.f - saturate(t_occlusion[id.xy])) * (1.f - share));
}
)";

struct Texture {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

Texture Make(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, const void* data, UINT row_bytes, bool writable) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width,
                                     .Height = height,
                                     .MipLevels = 1,
                                     .ArraySize = 1,
                                     .Format = format,
                                     .SampleDesc = {.Count = 1},
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0u)};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = row_bytes};
  Texture texture;
  if (FAILED(device->CreateTexture2D(&desc, (data != nullptr ? &initial : nullptr), texture.resource.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(texture.resource.Get(), nullptr, texture.srv.GetAddressOf()))
      || (writable && FAILED(device->CreateUnorderedAccessView(texture.resource.Get(), nullptr, texture.uav.GetAddressOf())))) {
    std::printf("texture %ux%u f%d could not be made\n", width, height, static_cast<int>(format));
    std::exit(2);
  }
  return texture;
}

ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const char* source, size_t length) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, length, "cs", nullptr, nullptr, "main", "cs_5_0", 0, 0, code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("cs: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(2);
  }
  ComPtr<ID3D11ComputeShader> shader;
  device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf());
  return shader;
}

std::vector<float> ReadBack(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), texture);
  std::vector<float> values(size_t{desc.Width} * desc.Height * 4);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    for (UINT y = 0; y < desc.Height; ++y) {
      const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch));
      std::copy_n(row, desc.Width * 4, values.begin() + (size_t{y} * desc.Width * 4));
    }
    context->Unmap(staging.Get(), 0);
  }
  return values;
}

float Noise(uint32_t index, uint32_t salt) {
  uint32_t hash = (index * 2654435761u) ^ (salt * 0x9E3779B9u);
  hash ^= hash >> 15;
  hash *= 0x2C1B3C6Du;
  hash ^= hash >> 12;
  return static_cast<float>(hash & 0xFFFFu) / 65535.f;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: ryse_specular_merge.exe <repository>\n");
    return 2;
  }
  const auto built = ReadFile(std::filesystem::path(argv[1]) / "build/rysesonofrome.include/embed/rt_specular_merge.cso");
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("no WARP device\n");
    return 2;
  }
  ComPtr<ID3D11ComputeShader> merge;
  if (built.empty() || FAILED(device->CreateComputeShader(built.data(), built.size(), nullptr, merge.GetAddressOf()))) {
    std::printf("rt_specular_merge.cso missing or not made (%zu bytes)\n", built.size());
    return 2;
  }
  const auto control_merge = Compile(device.Get(), CONTROL_SHADER, sizeof(CONTROL_SHADER) - 1);
  const auto lighting = Compile(device.Get(), LIGHTING_SHADER, sizeof(LIGHTING_SHADER) - 1);
  const D3D11_SAMPLER_DESC linear_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                                          .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                          .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                          .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                          .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> linear;
  device->CreateSamplerState(&linear_desc, linear.GetAddressOf());

  using DirectX::PackedVector::XMConvertFloatToHalf;
  constexpr UINT HALF_WIDTH = WIDTH / 2;
  constexpr UINT HALF_HEIGHT = HEIGHT / 2;
  std::vector<float> probes(size_t{WIDTH} * HEIGHT * 4);
  for (size_t index = 0; index < size_t{WIDTH} * HEIGHT; ++index) {
    for (uint32_t channel = 0; channel < 3; ++channel) probes[(index * 4) + channel] = 0.05f + (2.f * Noise(static_cast<uint32_t>(index), 10 + channel));
    probes[(index * 4) + 3] = 1.f;
  }
  const auto probes_texture = Make(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, probes.data(), WIDTH * 16, false);

  // One case: the reflections smooth or noisy, the occlusion constant, open, a ramp or random; the largest difference of
  // ours and of the control from the intended result, relative to it (above 0.01 absolute).
  enum Occlusion { CONSTANT, OPEN, RAMP, RANDOM };
  const auto run_case = [&](bool smooth, Occlusion kind) {
    std::vector<uint16_t> reflections(size_t{HALF_WIDTH} * HALF_HEIGHT * 4);
    for (UINT y = 0; y < HALF_HEIGHT; ++y) {
      for (UINT x = 0; x < HALF_WIDTH; ++x) {
        const uint32_t index = (y * HALF_WIDTH) + x;
        const float u = static_cast<float>(x) / (HALF_WIDTH - 1);
        const float v = static_cast<float>(y) / (HALF_HEIGHT - 1);
        const std::array<float, 4> texel = smooth ? std::array<float, 4>{0.2f + (3.f * u), 0.5f + v, 2.f - u, 0.1f + (0.8f * v)}
                                                  : std::array<float, 4>{4.f * Noise(index, 1), 4.f * Noise(index, 2), 4.f * Noise(index, 3), Noise(index, 4)};
        for (uint32_t channel = 0; channel < 4; ++channel) reflections[(size_t{index} * 4) + channel] = XMConvertFloatToHalf(texel[channel]);
      }
    }
    std::vector<float> occlusion(size_t{WIDTH} * HEIGHT);
    for (size_t index = 0; index < occlusion.size(); ++index) {
      const float ramp = 0.1f + (0.9f * static_cast<float>(index % WIDTH) / (WIDTH - 1));
      occlusion[index] = (kind == CONSTANT ? 0.55f : (kind == OPEN ? 1.f : (kind == RAMP ? ramp : Noise(static_cast<uint32_t>(index), 7))));
    }
    const auto reflections_texture = Make(device.Get(), HALF_WIDTH, HALF_HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, reflections.data(), HALF_WIDTH * 8, false);
    const auto occlusion_texture = Make(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, occlusion.data(), WIDTH * 4, false);
    const auto merged = Make(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr, 0, true);
    const auto control = Make(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr, 0, true);
    std::array<Texture, 3> results = {Make(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, true),
                                      Make(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, true),
                                      Make(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, true)};
    ID3D11SamplerState* samplers[] = {linear.Get()};
    for (const auto& [shader, output] : {std::pair{merge.Get(), merged.uav.Get()}, std::pair{control_merge.Get(), control.uav.Get()}}) {
      context->ClearState();
      ID3D11ShaderResourceView* inputs[] = {reflections_texture.srv.Get(), occlusion_texture.srv.Get()};
      context->CSSetShader(shader, nullptr, 0);
      context->CSSetShaderResources(0, 2, inputs);
      context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
      context->CSSetSamplers(0, 1, samplers);
      context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    }
    context->ClearState();
    ID3D11ShaderResourceView* inputs[] = {reflections_texture.srv.Get(), merged.srv.Get(), control.srv.Get(), occlusion_texture.srv.Get(),
                                          probes_texture.srv.Get()};
    ID3D11UnorderedAccessView* outputs[] = {results[0].uav.Get(), results[1].uav.Get(), results[2].uav.Get()};
    context->CSSetShader(lighting.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 5, inputs);
    context->CSSetUnorderedAccessViews(0, 3, outputs, nullptr);
    context->CSSetSamplers(0, 1, samplers);
    context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    const auto ours = ReadBack(device.Get(), context.Get(), results[0].resource.Get());
    const auto intended = ReadBack(device.Get(), context.Get(), results[1].resource.Get());
    const auto controlled = ReadBack(device.Get(), context.Get(), results[2].resource.Get());
    std::array<double, 2> worst = {};
    // Away from the last row and column (the lighting's corner sample reaches past the merged texture there, clamped).
    for (UINT y = 1; y + 1 < HEIGHT; ++y) {
      for (UINT x = 1; x + 1 < WIDTH; ++x) {
        for (uint32_t channel = 0; channel < 3; ++channel) {
          const size_t index = (((size_t{y} * WIDTH) + x) * 4) + channel;
          const double scale = std::max(std::abs(static_cast<double>(intended[index])), 0.01);
          worst[0] = std::max(worst[0], std::abs(static_cast<double>(ours[index]) - intended[index]) / scale);
          worst[1] = std::max(worst[1], std::abs(static_cast<double>(controlled[index]) - intended[index]) / scale);
        }
      }
    }
    return worst;
  };

  const auto smooth = run_case(true, CONSTANT);
  const auto smooth_open = run_case(true, OPEN);
  const auto smooth_ramp = run_case(true, RAMP);
  const auto noisy = run_case(false, RANDOM);
  const auto noisy_open = run_case(false, OPEN);
  std::printf("smooth reflections, occlusion 0.55: ours %.5f apart (relative, worst texel), CONTROL (no rescale) %.5f\n", smooth[0], smooth[1]);
  std::printf("smooth reflections, open (1): ours %.5f apart from the game's own lighting\n", smooth_open[0]);
  std::printf("smooth reflections, occlusion ramp 0.1-1: ours %.5f apart (half a pixel's shift of the ramp), CONTROL %.5f\n",
              smooth_ramp[0], smooth_ramp[1]);
  std::printf("noisy reflections and occlusion: ours %.5f apart, CONTROL %.5f (four texels' mean at the lighting's corners)\n", noisy[0], noisy[1]);
  std::printf("noisy reflections, open (1): ours %.5f apart from the game's own lighting\n", noisy_open[0]);
  // The half-float merged texture: 2^-11 a value, a few through the lerp and the four texels' mean of smooth ramps.
  CHECK(smooth[0] < 0.01);
  CHECK(smooth_open[0] < 0.005);
  CHECK(smooth[1] > 0.05);
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
