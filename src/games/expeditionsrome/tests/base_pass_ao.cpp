/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * The trace at Unreal's AO passes (ray_tracing.hpp OnGameAo / OnBasePassAo) on WARP: the traced ambient written into the
 * AO (0x7A1E0D46) before the game's own base pass AO (0x192686A0 from the shader dump, its blend: the scene colour times
 * its alpha) makes the baked light take ours: the scene colour x lerp(1, ours, the static fraction) where a shading
 * model is set, unchanged where none. Control: the base pass AO on the game's AO (the order before, ours went in only at
 * the sun draw) must miss that.
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
#include <DirectXPackedVector.h>
#include <wrl/client.h>

#include <embed/0x7A1E0D46.h>
#include <embed/light_bounce_vs.h>

namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT WIDTH = 13;
constexpr UINT HEIGHT = 9;
constexpr float STATIC_FRACTION = 0.8f;  // the base pass AO's cb0[0].y
constexpr uint8_t GAME_AO = 153;         // what Unreal's AO passes left (0.6)

void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
}

// The traced ambient: R8 values (13 - 237 of 255), which the AO holds exactly.
float Traced(UINT x, UINT y) {
  return static_cast<float>(13 + ((((x * 7) + (y * 3)) % 17) * 14)) / 255.f;
}

// G-buffer B's shading model byte: none, default lit, subsurface with a selective output bit above the low four.
uint8_t ShadingModel(UINT x, UINT y) {
  constexpr std::array<uint8_t, 3> MODELS = {0, 1, 0x12};
  return MODELS[(x + y) % 3];
}

float Scene(UINT x, UINT y, UINT channel) {
  return 0.25f + (0.5f * static_cast<float>(channel)) + (0.01f * static_cast<float>(x)) + (0.02f * static_cast<float>(y));
}

ComPtr<ID3D11Texture2D> MakeTexture(ID3D11Device* device, DXGI_FORMAT format, UINT bind_flags, const void* data, UINT pitch) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind_flags};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = pitch};
  ComPtr<ID3D11Texture2D> texture;
  Check(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()));
  return texture;
}

ComPtr<ID3D11ShaderResourceView> View(ID3D11Device* device, ID3D11Texture2D* texture) {
  ComPtr<ID3D11ShaderResourceView> view;
  Check(device->CreateShaderResourceView(texture, nullptr, view.GetAddressOf()));
  return view;
}

// The scene colour after the base pass AO, ours written into the AO first when `ours`.
std::vector<std::array<float, 4>> Draw(ID3D11Device* device, ID3D11DeviceContext* context, const std::vector<char>& base_pass_ao, bool ours) {
  std::vector<float> traced;
  std::vector<uint8_t> occlusion(static_cast<size_t>(WIDTH) * HEIGHT, GAME_AO);
  std::vector<std::array<uint8_t, 4>> gbuffer_b;
  std::vector<std::array<uint16_t, 4>> scene;
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      traced.push_back(Traced(x, y));
      gbuffer_b.push_back({40, 80, 120, ShadingModel(x, y)});
      std::array<uint16_t, 4> pixel = {};
      for (UINT channel = 0; channel < 4; ++channel) pixel[channel] = DirectX::PackedVector::XMConvertFloatToHalf(Scene(x, y, channel));
      scene.push_back(pixel);
    }
  }
  const auto traced_texture = MakeTexture(device, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, traced.data(), WIDTH * 4);
  const auto occlusion_texture = MakeTexture(device, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
                                             occlusion.data(), WIDTH);
  const auto gbuffer_texture = MakeTexture(device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, gbuffer_b.data(), WIDTH * 4);
  const auto scene_texture = MakeTexture(device, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET, scene.data(), WIDTH * 8);
  ComPtr<ID3D11RenderTargetView> occlusion_target;
  ComPtr<ID3D11RenderTargetView> scene_target;
  Check(device->CreateRenderTargetView(occlusion_texture.Get(), nullptr, occlusion_target.GetAddressOf()));
  Check(device->CreateRenderTargetView(scene_texture.Get(), nullptr, scene_target.GetAddressOf()));
  ComPtr<ID3D11VertexShader> fullscreen;
  ComPtr<ID3D11PixelShader> ambient;
  ComPtr<ID3D11PixelShader> game;
  Check(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, fullscreen.GetAddressOf()));
  Check(device->CreatePixelShader(__0x7A1E0D46.data(), __0x7A1E0D46.size(), nullptr, ambient.GetAddressOf()));
  Check(device->CreatePixelShader(base_pass_ao.data(), base_pass_ao.size(), nullptr, game.GetAddressOf()));
  // cb0[0].y the static fraction; cb1 the View, its row 127 BufferSizeAndInvSize.
  const std::array<float, 4> fraction = {0.f, STATIC_FRACTION, 0.f, 0.f};
  std::array<std::array<float, 4>, 128> view_rows = {};
  view_rows[127] = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT};
  const D3D11_BUFFER_DESC fraction_desc = {.ByteWidth = sizeof(fraction), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC view_desc = {.ByteWidth = sizeof(view_rows), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA fraction_data = {.pSysMem = fraction.data()};
  const D3D11_SUBRESOURCE_DATA view_data = {.pSysMem = view_rows.data()};
  std::array<ComPtr<ID3D11Buffer>, 2> constants;
  Check(device->CreateBuffer(&fraction_desc, &fraction_data, constants[0].GetAddressOf()));
  Check(device->CreateBuffer(&view_desc, &view_data, constants[1].GetAddressOf()));
  const D3D11_SAMPLER_DESC point_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> point;
  Check(device->CreateSamplerState(&point_desc, point.GetAddressOf()));
  // Unreal's base pass AO blend: the scene colour (and alpha) times the shader's alpha.
  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ZERO, .DestBlend = D3D11_BLEND_SRC_ALPHA,
                                .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ZERO, .DestBlendAlpha = D3D11_BLEND_SRC_ALPHA,
                                .BlendOpAlpha = D3D11_BLEND_OP_ADD, .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL};
  ComPtr<ID3D11BlendState> multiply;
  Check(device->CreateBlendState(&blend_desc, multiply.GetAddressOf()));

  const D3D11_VIEWPORT viewport = {.Width = WIDTH, .Height = HEIGHT, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(fullscreen.Get(), nullptr, 0);
  if (ours) {
    const auto traced_view = View(device, traced_texture.Get());
    context->OMSetRenderTargets(1, occlusion_target.GetAddressOf(), nullptr);
    context->PSSetShader(ambient.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, traced_view.GetAddressOf());
    context->Draw(3, 0);
    ID3D11RenderTargetView* const none = nullptr;
    context->OMSetRenderTargets(1, &none, nullptr);
  }
  const std::array<ComPtr<ID3D11ShaderResourceView>, 2> input_views = {View(device, gbuffer_texture.Get()), View(device, occlusion_texture.Get())};
  const std::array<ID3D11ShaderResourceView*, 2> inputs = {input_views[0].Get(), input_views[1].Get()};
  const std::array<ID3D11Buffer*, 2> buffers = {constants[0].Get(), constants[1].Get()};
  const std::array<ID3D11SamplerState*, 2> samplers = {point.Get(), point.Get()};
  context->OMSetRenderTargets(1, scene_target.GetAddressOf(), nullptr);
  context->OMSetBlendState(multiply.Get(), nullptr, UINT_MAX);
  context->PSSetShader(game.Get(), nullptr, 0);
  context->PSSetShaderResources(0, 2, inputs.data());
  context->PSSetConstantBuffers(0, 2, buffers.data());
  context->PSSetSamplers(0, 2, samplers.data());
  context->Draw(3, 0);
  context->ClearState();

  D3D11_TEXTURE2D_DESC staging_desc = {};
  scene_texture->GetDesc(&staging_desc);
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  Check(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()));
  context->CopyResource(staging.Get(), scene_texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
  std::vector<std::array<float, 4>> result;
  for (UINT y = 0; y < HEIGHT; ++y) {
    const auto* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch));
    for (UINT x = 0; x < WIDTH; ++x) {
      std::array<float, 4> pixel = {};
      for (UINT channel = 0; channel < 4; ++channel) pixel[channel] = DirectX::PackedVector::XMConvertHalfToFloat(row[(x * 4) + channel]);
      result.push_back(pixel);
    }
  }
  context->Unmap(staging.Get(), 0);
  return result;
}

// The largest difference from the scene colour x lerp(1, ours, the static fraction) where a shading model is set.
float MaxError(const std::vector<std::array<float, 4>>& result) {
  float error = 0.f;
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const float factor = ((ShadingModel(x, y) & 15) != 0) ? 1.f + ((Traced(x, y) - 1.f) * STATIC_FRACTION) : 1.f;
      for (UINT channel = 0; channel < 4; ++channel) {
        const float expected = DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(Scene(x, y, channel)))
                               * factor;
        const float actual = result[(y * WIDTH) + x][channel];
        error = std::max(error, std::isfinite(actual) ? std::abs(actual - expected) : INFINITY);
      }
    }
  }
  return error;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc < 2) throw std::runtime_error("the add-on's source folder as the first argument");
    const auto dump = std::filesystem::path(argv[1]) / "../../../bin/cmd_Decompiler-1.3.16/dump_shaders_Expeditions_rome/all_shaders_/0x192686A0.ps_5_0.cso";
    std::ifstream file(dump, std::ios::binary);
    if (!file) throw std::runtime_error("no " + dump.string());
    const std::vector<char> base_pass_ao((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                            context.GetAddressOf()));
    // Half floats of 0.25 - 2: 1 / 1024 a step at most.
    constexpr float TOLERANCE = 0.002f;
    constexpr float CONTROL_MIN = 0.05f;
    const float ours_error = MaxError(Draw(device.Get(), context.Get(), base_pass_ao, true));
    const float control_error = MaxError(Draw(device.Get(), context.Get(), base_pass_ao, false));
    std::cout << "base pass AO on ours: max error " << ours_error << " (limit " << TOLERANCE << "); control, on the game's AO: "
              << control_error << " (must exceed " << CONTROL_MIN << ")\n";
    if (!(ours_error <= TOLERANCE)) throw std::runtime_error("the base pass AO did not take ours");
    if (!(control_error > CONTROL_MIN)) throw std::runtime_error("the control was not caught");
    std::cout << "PASS base_pass_ao\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL base_pass_ao: " << error.what() << '\n';
    return 1;
  }
}
