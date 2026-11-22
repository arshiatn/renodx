/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * The sun's light the add-on gives the paths (ray_tracing.hpp OnSunDraw: WriteSun's texel 2 = LightColor (c4) x View
 * 129.y / pi, toward the sun = c5 normalized) on WARP against the game's own sun light pass (0xC38C91CD from the shader
 * dump): on default-lit pixels the game's light must be the guides' albedo (base x (1 - metallic)) x texel 2 x N.L x
 * the screen shadow mask's shadow (t8: x squared x z squared), as rt_core.hpp WriteSun states. Specular is off (the
 * light's SpecularScale c5.w 0; diffuse is what texel 2 stands for), source radius and length 0, no distance fade, one
 * shadowed bit (no contact shadows). Controls: texel 2 without the 1 / pi, and the albedo without (1 - metallic), must
 * miss.
 * Built and run by test/tw_expeditions_unit/run.ps1 (after the expeditionsrome target; its argument: the add-on's
 * source folder, the shader dump under the repository's bin).
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT WIDTH = 16;
constexpr UINT HEIGHT = 12;
constexpr std::array<float, 3> LIGHT_COLOR = {3.2f, 2.9f, 2.4f};  // c4
constexpr float OUTPUT_SCALE = 0.37f;                             // View 129.y
constexpr std::array<float, 3> TOWARD_LIGHT = {0.3f, -0.4f, 0.75f};  // c5, not unit length (the add-on normalizes it)
constexpr double INVERSE_PI = 0.318309873;

// A full-screen triangle with Unreal's light pass inputs: TEXCOORD0 the uv, TEXCOORD1 the screen vector (the camera
// looking down -z), the position.
constexpr char VERTEX_SHADER[] = R"(
struct Output { float2 uv : TEXCOORD0; float3 screen_vector : TEXCOORD1; float4 position : SV_Position; };
Output main(uint id : SV_VertexID) {
  Output output;
  output.uv = float2((id << 1) & 2, id & 2);
  output.screen_vector = float3(0.2f * (output.uv.x - 0.5f), 0.2f * (output.uv.y - 0.5f), -1.f);
  output.position = float4((output.uv * float2(2.f, -2.f)) + float2(-1.f, 1.f), 0.f, 1.f);
  return output;
})";

void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
}

struct Pixel {
  std::array<float, 3> normal;  // unit length, toward the camera's side
  std::array<float, 3> base;
  float metallic;
  std::array<float, 2> shadow;  // the screen shadow mask's x and z
};

Pixel PixelAt(UINT x, UINT y) {
  const float angle = 0.45f * static_cast<float>(x);
  const float tilt = 0.12f * static_cast<float>(y);  // up to 1.3 rad: some pixels face away from the light
  std::array<float, 3> normal = {std::sin(tilt) * std::cos(angle), std::sin(tilt) * std::sin(angle), std::cos(tilt)};
  return {
      .normal = normal,
      .base = {0.1f + (0.05f * static_cast<float>(x % 7)), 0.2f + (0.06f * static_cast<float>(y % 5)), 0.6f - (0.03f * static_cast<float>(x % 9))},
      .metallic = 0.1f * static_cast<float>((x + y) % 7),
      .shadow = {1.f - (0.06f * static_cast<float>(x % 11)), 1.f - (0.07f * static_cast<float>(y % 9))},
  };
}

ComPtr<ID3D11ShaderResourceView> Texture(ID3D11Device* device, const std::vector<std::array<float, 4>>& texels) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = texels.data(), .SysMemPitch = WIDTH * 16};
  ComPtr<ID3D11Texture2D> texture;
  Check(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()));
  ComPtr<ID3D11ShaderResourceView> view;
  Check(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
  return view;
}

// The game's sun light pass's output (RGB) per pixel.
std::vector<std::array<float, 4>> Draw(ID3D11Device* device, ID3D11DeviceContext* context, const std::vector<char>& sun_light) {
  std::vector<std::array<float, 4>> depth;
  std::vector<std::array<float, 4>> gbuffer_a;
  std::vector<std::array<float, 4>> gbuffer_b;
  std::vector<std::array<float, 4>> gbuffer_c;
  std::vector<std::array<float, 4>> shadow_mask;
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const Pixel pixel = PixelAt(x, y);
      depth.push_back({0.5f, 0.f, 0.f, 0.f});
      gbuffer_a.push_back({(pixel.normal[0] * 0.5f) + 0.5f, (pixel.normal[1] * 0.5f) + 0.5f, (pixel.normal[2] * 0.5f) + 0.5f, 0.f});
      // Metallic, specular, roughness, the shading model (default lit) in the low four bits.
      gbuffer_b.push_back({pixel.metallic, 0.5f, 0.5f, 1.f / 255.f});
      gbuffer_c.push_back({pixel.base[0], pixel.base[1], pixel.base[2], 1.f});
      shadow_mask.push_back({pixel.shadow[0], 1.f, pixel.shadow[1], 1.f});
    }
  }
  const std::vector<std::array<float, 4>> zeros(static_cast<size_t>(WIDTH) * HEIGHT, std::array<float, 4>{});
  const std::vector<std::array<float, 4>> ones(static_cast<size_t>(WIDTH) * HEIGHT, std::array<float, 4>{1.f, 1.f, 1.f, 1.f});
  // t0 - t9: (unused here), the scene depth, G-buffers A - D, the precomputed shadow factors, the light attenuation,
  // the screen shadow mask, (unused here).
  const std::array<ComPtr<ID3D11ShaderResourceView>, 10> views = {
      Texture(device, zeros), Texture(device, depth), Texture(device, gbuffer_a), Texture(device, gbuffer_b), Texture(device, gbuffer_c),
      Texture(device, zeros), Texture(device, ones), Texture(device, ones), Texture(device, shadow_mask), Texture(device, zeros)};

  const D3D11_TEXTURE2D_DESC target_desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                            .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target_texture;
  Check(device->CreateTexture2D(&target_desc, nullptr, target_texture.GetAddressOf()));
  ComPtr<ID3D11RenderTargetView> target;
  Check(device->CreateRenderTargetView(target_texture.Get(), nullptr, target.GetAddressOf()));

  ComPtr<ID3DBlob> vertex_code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(VERTEX_SHADER, sizeof(VERTEX_SHADER) - 1, "sun_light_vs", nullptr, nullptr, "main", "vs_5_0", 0, 0,
                        vertex_code.GetAddressOf(), errors.GetAddressOf()))) {
    throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "vertex shader");
  }
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> game;
  Check(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf()));
  Check(device->CreatePixelShader(sun_light.data(), sun_light.size(), nullptr, game.GetAddressOf()));

  // b0 the View: 61 InvDeviceZToWorldZTransform (any depth: 1001 units), 129.y the output scale, 130 / 131 the diffuse /
  // specular override parameters at their neutral (x 1 + 0); the rest 0 (no subsurface split, no checkerboard).
  std::array<std::array<float, 4>, 183> view = {};
  view[61] = {0.f, 1000.f, 0.f, -1.f};
  view[127] = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT};
  view[129] = {1.f, OUTPUT_SCALE, 1.f, 1.f};
  view[130] = {0.f, 0.f, 0.f, 1.f};
  view[131] = {0.f, 0.f, 0.f, 1.f};
  // b1 FDeferredLightUniformStruct: no shadow map channel mask (c0), no distance fade (c1), ShadowedBits 1 (c2.x), the
  // colour (c4), toward the light with SpecularScale 0 (c5), source radius and length 0 (c6.w, c7.zw).
  std::array<std::array<float, 4>, 8> light = {};
  const uint32_t shadowed_bits = 1;
  std::memcpy(light[2].data(), &shadowed_bits, 4);
  light[4] = {LIGHT_COLOR[0], LIGHT_COLOR[1], LIGHT_COLOR[2], 0.f};
  light[5] = {TOWARD_LIGHT[0], TOWARD_LIGHT[1], TOWARD_LIGHT[2], 0.f};
  std::array<ComPtr<ID3D11Buffer>, 2> constants;
  for (size_t index = 0; index < constants.size(); ++index) {
    const void* data = (index == 0) ? static_cast<const void*>(view.data()) : static_cast<const void*>(light.data());
    const UINT bytes = (index == 0) ? static_cast<UINT>(sizeof(view)) : static_cast<UINT>(sizeof(light));
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
    Check(device->CreateBuffer(&desc, &initial, constants[index].GetAddressOf()));
  }
  const D3D11_SAMPLER_DESC point_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> point;
  Check(device->CreateSamplerState(&point_desc, point.GetAddressOf()));

  std::array<ID3D11ShaderResourceView*, 10> inputs = {};
  for (size_t index = 0; index < views.size(); ++index) inputs[index] = views[index].Get();
  std::array<ID3D11SamplerState*, 9> samplers = {};
  samplers.fill(point.Get());
  const std::array<ID3D11Buffer*, 2> buffers = {constants[0].Get(), constants[1].Get()};
  const D3D11_VIEWPORT viewport = {.Width = WIDTH, .Height = HEIGHT, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex.Get(), nullptr, 0);
  context->PSSetShader(game.Get(), nullptr, 0);
  context->PSSetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.data());
  context->PSSetConstantBuffers(0, 2, buffers.data());
  context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
  context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  context->ClearState();

  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  Check(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()));
  context->CopyResource(staging.Get(), target_texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
  std::vector<std::array<float, 4>> result;
  for (UINT y = 0; y < HEIGHT; ++y) {
    const auto* row = reinterpret_cast<const std::array<float, 4>*>(static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch));
    result.insert(result.end(), row, row + WIDTH);
  }
  context->Unmap(staging.Get(), 0);
  return result;
}

// The largest difference, relative to the largest light, from albedo x texel 2 x N.L x the shadow: texel 2 as the add-on
// writes it (`inverse_pi` false: without the 1 / pi), the albedo base x (1 - metallic) (`metallic_off` false: the base).
double MaxError(const std::vector<std::array<float, 4>>& result, bool inverse_pi, bool metallic_off, size_t* lit) {
  const double scale = (inverse_pi ? INVERSE_PI : 1.0) * OUTPUT_SCALE;
  const double length = std::sqrt((TOWARD_LIGHT[0] * TOWARD_LIGHT[0]) + (TOWARD_LIGHT[1] * TOWARD_LIGHT[1]) + (TOWARD_LIGHT[2] * TOWARD_LIGHT[2]));
  double error = 0.0;
  double largest = 0.0;
  *lit = 0;
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const Pixel pixel = PixelAt(x, y);
      double n_dot_l = 0.0;
      for (size_t axis = 0; axis < 3; ++axis) n_dot_l += pixel.normal[axis] * (TOWARD_LIGHT[axis] / length);
      n_dot_l = std::clamp(n_dot_l, 0.0, 1.0);
      if (n_dot_l > 0.0) ++*lit;
      const double shadow = static_cast<double>(pixel.shadow[0]) * pixel.shadow[0] * pixel.shadow[1] * pixel.shadow[1];
      for (size_t channel = 0; channel < 3; ++channel) {
        const double albedo = pixel.base[channel] * (metallic_off ? 1.0 - pixel.metallic : 1.0);
        const double expected = albedo * (LIGHT_COLOR[channel] * scale) * n_dot_l * shadow;
        const double actual = result[(y * WIDTH) + x][channel];
        largest = std::max(largest, expected);
        error = std::max(error, std::isfinite(actual) ? std::abs(actual - expected) : INFINITY);
      }
    }
  }
  return error / std::max(largest, 1e-9);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc < 2) throw std::runtime_error("the add-on's source folder as the first argument");
    const auto dump = std::filesystem::path(argv[1]) / "../../../bin/cmd_Decompiler-1.3.16/dump_shaders_Expeditions_rome/all_shaders_/0xC38C91CD.ps_5_0.cso";
    std::ifstream file(dump, std::ios::binary);
    if (!file) throw std::runtime_error("no " + dump.string());
    const std::vector<char> sun_light((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                            context.GetAddressOf()));
    const auto result = Draw(device.Get(), context.Get(), sun_light);
    // Float32 throughout: the shader's own rounding (normalize, rsq) only.
    constexpr double TOLERANCE = 1e-4;
    constexpr double CONTROL_MIN = 0.05;
    size_t lit = 0;
    const double error = MaxError(result, true, true, &lit);
    const double no_inverse_pi = MaxError(result, false, true, &lit);
    const double no_metallic = MaxError(result, true, false, &lit);
    std::cout << "C38C91CD vs albedo x texel 2 x N.L x shadow: max relative error " << error << " (limit " << TOLERANCE << "), "
              << lit << " of " << (WIDTH * HEIGHT) << " pixels lit; controls: without 1 / pi " << no_inverse_pi
              << ", albedo without (1 - metallic) " << no_metallic << " (each must exceed " << CONTROL_MIN << ")\n";
    if (lit == 0 || lit == static_cast<size_t>(WIDTH) * HEIGHT) throw std::runtime_error("the pattern needs lit and unlit pixels");
    if (!(error <= TOLERANCE)) throw std::runtime_error("texel 2 is not the game's diffuse sun light");
    if (!(no_inverse_pi > CONTROL_MIN) || !(no_metallic > CONTROL_MIN)) throw std::runtime_error("a control was not caught");
    std::cout << "PASS sun_light_texel\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL sun_light_texel: " << error.what() << '\n';
    return 1;
  }
}
