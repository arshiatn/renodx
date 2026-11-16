// The game's AO blurs (ssao.hlsl blur_x, blur_y: Attila's and Thrones' B4026AF2 / 6203C824, Rome II's 41894C1F / 7DDD84A3)
// on WARP, the game's bytes from its dump, bound by their reflected names: under Path-Traced Ambient the AO pass writes
// white, and rt_scene.hpp's OnAmbientBlurDraw leaves both blurs out and clears blur_y's target to 1 instead. That is the
// same picture only if the blurs of white give exactly 1 in their R16F targets for every depth: their weights sum the same
// way above and below the division, the centre tap weighs 1 (no 0 / 0).
// A. white in: blur_x's and blur_y's R16F results exactly 1.0 (0x3C00) at every texel, for blur sharpness 0, 4 and 1000 and
//    a depth with edges, sky (1) and near texels; their R32F results' largest distance from 1 printed.
// B. CONTROL: an AO with shading in: blur_y's R16F result is not 1 everywhere (the blurs ran on it).
// tests.exe <game csos folder> <blur_x hash> <blur_y hash> [<another folder> <blur_x> <blur_y> ...]
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

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

constexpr UINT WIDTH = 256;  // the AO pass's (and the depth's) size; the blurs draw half of it
constexpr UINT HEIGHT = 128;

static std::vector<char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static float Hash(UINT x, UINT y, UINT salt) {
  UINT h = x * 73856093u ^ y * 19349663u ^ salt * 83492791u;
  h ^= h >> 13;
  h *= 0x5BD1E995u;
  h ^= h >> 15;
  return static_cast<float>(h & 0xFFFFu) / 65535.f;
}

static ComPtr<ID3D11ShaderResourceView> FloatTexture(UINT width, UINT height, const std::vector<float>& texels) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = texels.data(), .SysMemPitch = width * 4};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateTexture2D(&desc, &initial, texture.GetAddressOf());
  device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf());
  return view;
}

struct Target {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D11ShaderResourceView> srv;
};

static Target MakeTarget(DXGI_FORMAT format) {
  Target target;
  const D3D11_TEXTURE2D_DESC desc = {.Width = WIDTH / 2, .Height = HEIGHT / 2, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE};
  device->CreateTexture2D(&desc, nullptr, target.texture.GetAddressOf());
  device->CreateRenderTargetView(target.texture.Get(), nullptr, target.rtv.GetAddressOf());
  device->CreateShaderResourceView(target.texture.Get(), nullptr, target.srv.GetAddressOf());
  constexpr std::array<float, 4> CLEAR = {-3.f, -3.f, -3.f, -3.f};
  context->ClearRenderTargetView(target.rtv.Get(), CLEAR.data());
  return target;
}

// A target's texels as bytes (2 a texel for R16F, 4 for R32F).
static std::vector<uint8_t> ReadBack(const Target& target, UINT texel_bytes) {
  D3D11_TEXTURE2D_DESC desc = {};
  target.texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), target.texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<uint8_t> bytes(static_cast<size_t>(desc.Width) * desc.Height * texel_bytes);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(&bytes[static_cast<size_t>(y) * desc.Width * texel_bytes], static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch,
                desc.Width * texel_bytes);
  }
  context->Unmap(staging.Get(), 0);
  return bytes;
}

// One of the game's blurs, its slots and constants by the names its reflection gives.
struct Blur {
  ComPtr<ID3D11PixelShader> shader;
  UINT ao_slot = UINT_MAX;     // resolve_target_sampler
  UINT depth_slot = UINT_MAX;  // gbuffer_channel_4_sampler
  UINT camera_slot = UINT_MAX;
  UINT blur_slot = UINT_MAX;
  UINT camera_bytes = 0;
  UINT blur_bytes = 0;
  UINT inv_projection = 0;  // byte offsets in their buffers
  UINT screen_size = 0;
  UINT texel_offset = 0;
  UINT sharpness = 0;
  UINT resolution = 0;
};

static bool LoadBlur(const std::string& path, Blur* blur) {
  const auto code = ReadFile(path);
  if (code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, blur->shader.GetAddressOf()))) {
    std::printf("  %s: %zu bytes, no shader\n", path.c_str(), code.size());
    return false;
  }
  ComPtr<ID3D11ShaderReflection> reflection;
  // IID_ID3D11ShaderReflection (dxguid): the interface has no __declspec(uuid) for IID_PPV_ARGS.
  if (FAILED(D3DReflect(code.data(), code.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf())))) {
    std::printf("  %s: no reflection\n", path.c_str());
    return false;
  }
  // A texture and its sampler share a name in the game's bytes: the binding of that name and type.
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  const auto slot_of = [&](const char* name, D3D_SHADER_INPUT_TYPE type) {
    for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
      D3D11_SHADER_INPUT_BIND_DESC bind = {};
      reflection->GetResourceBindingDesc(index, &bind);
      if (bind.Type == type && std::strcmp(bind.Name, name) == 0) return bind.BindPoint;
    }
    return UINT_MAX;
  };
  // blur_x reads the AO pass's target (resolve_target_sampler), blur_y blur_x's (blur_x_target_sampler).
  blur->ao_slot = std::min(slot_of("resolve_target_sampler", D3D_SIT_TEXTURE), slot_of("blur_x_target_sampler", D3D_SIT_TEXTURE));
  blur->depth_slot = slot_of("gbuffer_channel_4_sampler", D3D_SIT_TEXTURE);
  blur->camera_slot = slot_of("camera_VS_PS", D3D_SIT_CBUFFER);
  blur->blur_slot = slot_of("ssao_blur_PS", D3D_SIT_CBUFFER);
  const auto offset_of = [&](const char* buffer, const char* name, UINT* bytes) {
    auto* constants = reflection->GetConstantBufferByName(buffer);
    D3D11_SHADER_BUFFER_DESC buffer_desc = {};
    constants->GetDesc(&buffer_desc);
    *bytes = buffer_desc.Size;
    D3D11_SHADER_VARIABLE_DESC variable = {};
    constants->GetVariableByName(name)->GetDesc(&variable);
    return variable.StartOffset;
  };
  blur->inv_projection = offset_of("camera_VS_PS", "inv_projection", &blur->camera_bytes);
  blur->screen_size = offset_of("camera_VS_PS", "g_screen_size", &blur->camera_bytes);
  blur->texel_offset = offset_of("camera_VS_PS", "g_vpos_texel_offset", &blur->camera_bytes);
  blur->sharpness = offset_of("ssao_blur_PS", "g_BlurSharpness", &blur->blur_bytes);
  blur->resolution = offset_of("ssao_blur_PS", "g_BlurResolution", &blur->blur_bytes);
  std::printf("  %s: t%u AO, t%u depth, b%u camera, b%u blur\n", path.c_str(), blur->ao_slot, blur->depth_slot, blur->camera_slot,
              blur->blur_slot);
  return blur->ao_slot != UINT_MAX && blur->depth_slot != UINT_MAX && blur->camera_slot != UINT_MAX && blur->blur_slot != UINT_MAX;
}

static ComPtr<ID3D11Buffer> Constants(const std::vector<uint8_t>& bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>((bytes.size() + 15) / 16 * 16), .Usage = D3D11_USAGE_IMMUTABLE,
                                  .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> padded(desc.ByteWidth);
  std::memcpy(padded.data(), bytes.data(), bytes.size());
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, buffer.GetAddressOf());
  return buffer;
}

static void Draw(const Blur& blur, ID3D11ShaderResourceView* ao, ID3D11ShaderResourceView* depth, float sharpness, const Target& target) {
  static ComPtr<ID3D11VertexShader> vertex_shader;
  static std::array<ComPtr<ID3D11SamplerState>, 2> samplers;  // point, linear (clamp)
  if (!vertex_shader) {
    constexpr char SOURCE[] =
        "void main(uint id : SV_VertexID, out float4 p : SV_Position, out float2 t : TEXCOORD0) {"
        " t = float2((id << 1) & 2, id & 2); p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); }";
    ComPtr<ID3DBlob> code;
    D3DCompile(SOURCE, sizeof(SOURCE) - 1, nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, code.GetAddressOf(), nullptr);
    device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf());
    for (size_t index = 0; index < samplers.size(); ++index) {
      const D3D11_SAMPLER_DESC desc = {.Filter = (index == 0) ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                                       .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                       .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxAnisotropy = 1, .ComparisonFunc = D3D11_COMPARISON_NEVER,
                                       .MaxLOD = D3D11_FLOAT32_MAX};
      device->CreateSamplerState(&desc, samplers[index].GetAddressOf());
    }
  }
  // The camera: a 60 degree perspective (row vectors), its inverse at inv_projection; the screen the AO pass's size.
  const double ys = 1.0 / std::tan(30.0 * 3.14159265358979323846 / 180.0);
  const double xs = ys * HEIGHT / WIDTH;
  const double q = 1000.0 / (1000.0 - 0.5);
  // inverse of {{xs,0,0,0},{0,ys,0,0},{0,0,q,1},{0,0,-0.5q,0}}
  const double inverse[4][4] = {{1 / xs, 0, 0, 0}, {0, 1 / ys, 0, 0}, {0, 0, 0, -1 / (0.5 * q)}, {0, 0, 1, 1 / 0.5}};
  std::vector<uint8_t> camera(blur.camera_bytes);
  for (UINT column = 0; column < 4; ++column) {
    for (UINT row = 0; row < 4; ++row) {
      const auto value = static_cast<float>(inverse[row][column]);
      std::memcpy(&camera[blur.inv_projection + (column * 4 + row) * 4], &value, 4);
    }
  }
  const std::array<float, 4> screen = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT};
  std::memcpy(&camera[blur.screen_size], screen.data(), 16);
  std::memset(&camera[blur.texel_offset], 0, 4);
  std::vector<uint8_t> parameters(blur.blur_bytes);
  const std::array<float, 2> resolution = {static_cast<float>(WIDTH / 2), static_cast<float>(HEIGHT / 2)};
  std::memcpy(&parameters[blur.sharpness], &sharpness, 4);
  std::memcpy(&parameters[blur.resolution], resolution.data(), 8);
  const auto camera_buffer = Constants(camera);
  const auto blur_buffer = Constants(parameters);
  ID3D11RenderTargetView* bound = target.rtv.Get();
  context->OMSetRenderTargets(1, &bound, nullptr);
  const D3D11_VIEWPORT viewport = {.Width = WIDTH / 2.f, .Height = HEIGHT / 2.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(blur.shader.Get(), nullptr, 0);
  context->PSSetShaderResources(blur.ao_slot, 1, &ao);
  context->PSSetShaderResources(blur.depth_slot, 1, &depth);
  ID3D11Buffer* buffers[] = {camera_buffer.Get(), blur_buffer.Get()};
  context->PSSetConstantBuffers(blur.camera_slot, 1, &buffers[0]);
  context->PSSetConstantBuffers(blur.blur_slot, 1, &buffers[1]);
  ID3D11SamplerState* states[] = {samplers[0].Get(), samplers[1].Get()};
  context->PSSetSamplers(0, 2, states);  // s0 the depth's (point), s1 the AO's (linear), as the game's names order them
  context->Draw(3, 0);
  ID3D11ShaderResourceView* none = nullptr;
  context->PSSetShaderResources(blur.ao_slot, 1, &none);
  context->PSSetShaderResources(blur.depth_slot, 1, &none);
  context->OMSetRenderTargets(0, nullptr, nullptr);
}

int main(int argc, char** argv) {
  if (argc < 4 || (argc - 1) % 3 != 0) {
    std::printf("usage: tests.exe <game csos folder> <blur_x hash> <blur_y hash> [...]\n");
    return 2;
  }
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }
  // The depth (non-linear, 0 near): 8 x 8 blocks of far and near surfaces, a sky (1) and texels at the near plane.
  std::vector<float> depth(WIDTH * HEIGHT);
  std::vector<float> white(WIDTH * HEIGHT, 1.f);
  std::vector<float> shaded(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const float block = Hash(x / 8, y / 8, 1);
      float value = (block < 0.1f) ? 1.f : (block < 0.15f ? 0.f : 0.2f + 0.7999f * Hash(x / 8, y / 8, 2) + 1e-4f * Hash(x, y, 3));
      depth[y * WIDTH + x] = value;
      shaded[y * WIDTH + x] = 0.2f + 0.8f * Hash(x, y, 4);
    }
  }
  const auto depth_view = FloatTexture(WIDTH, HEIGHT, depth);
  const auto white_view = FloatTexture(WIDTH, HEIGHT, white);
  const auto shaded_view = FloatTexture(WIDTH, HEIGHT, shaded);
  constexpr uint16_t HALF_ONE = 0x3C00;
  for (int argument = 1; argument + 2 < argc; argument += 3) {
    const std::string folder = argv[argument];
    Blur blur_x;
    Blur blur_y;
    const std::string x_name = std::string("0x") + argv[argument + 1] + ".ps_4_0.cso";
    const std::string y_name = std::string("0x") + argv[argument + 2] + ".ps_4_0.cso";
    if (!LoadBlur(folder + "\\" + x_name, &blur_x) || !LoadBlur(folder + "\\" + y_name, &blur_y)) {
      std::printf("FAIL %s: %s / %s not loaded or not the AO blur's bindings\n", folder.c_str(), x_name.c_str(), y_name.c_str());
      ++failures;
      continue;
    }
    for (const float sharpness : {0.f, 4.f, 1000.f}) {
      UINT not_one_x = 0;
      UINT not_one_y = 0;
      float worst = 0.f;
      for (const DXGI_FORMAT format : {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R32_FLOAT}) {
        const Target x_target = MakeTarget(format);
        const Target y_target = MakeTarget(format);
        Draw(blur_x, white_view.Get(), depth_view.Get(), sharpness, x_target);
        Draw(blur_y, x_target.srv.Get(), depth_view.Get(), sharpness, y_target);
        if (format == DXGI_FORMAT_R16_FLOAT) {
          for (const auto& [target, count] : {std::pair{&x_target, &not_one_x}, std::pair{&y_target, &not_one_y}}) {
            const auto bytes = ReadBack(*target, 2);
            for (size_t texel = 0; texel < bytes.size() / 2; ++texel) {
              uint16_t value = 0;
              std::memcpy(&value, &bytes[texel * 2], 2);
              *count += (value != HALF_ONE) ? 1u : 0u;
            }
          }
        } else {
          for (const Target* target : {&x_target, &y_target}) {
            const auto bytes = ReadBack(*target, 4);
            for (size_t texel = 0; texel < bytes.size() / 4; ++texel) {
              float value = 0.f;
              std::memcpy(&value, &bytes[texel * 4], 4);
              worst = std::max(worst, std::isfinite(value) ? std::fabs(value - 1.f) : INFINITY);
            }
          }
        }
      }
      std::printf("%s %s / %s, sharpness %g: A white in, R16F texels not 1: blur_x %u, blur_y %u of %u; R32F largest |x - 1| %g\n",
                  folder.c_str(), argv[argument + 1], argv[argument + 2], sharpness, not_one_x, not_one_y, WIDTH / 2 * HEIGHT / 2, worst);
      CHECK(not_one_x == 0 && not_one_y == 0, "the blurs of white are not exactly 1 in R16F");
      const Target x_target = MakeTarget(DXGI_FORMAT_R16_FLOAT);
      const Target y_target = MakeTarget(DXGI_FORMAT_R16_FLOAT);
      Draw(blur_x, shaded_view.Get(), depth_view.Get(), sharpness, x_target);
      Draw(blur_y, x_target.srv.Get(), depth_view.Get(), sharpness, y_target);
      const auto bytes = ReadBack(y_target, 2);
      UINT control = 0;
      for (size_t texel = 0; texel < bytes.size() / 2; ++texel) {
        uint16_t value = 0;
        std::memcpy(&value, &bytes[texel * 2], 2);
        control += (value != HALF_ONE) ? 1u : 0u;
      }
      std::printf("  B CONTROL shaded in: blur_y texels not 1: %u\n", control);
      CHECK(control > 0, "control B does not fail");
    }
  }
  std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
