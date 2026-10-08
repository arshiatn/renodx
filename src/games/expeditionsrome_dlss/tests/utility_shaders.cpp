/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * From an x64 VS developer prompt at the repository root (join these lines):
 * clang-cl /nologo /std:c++20 /EHsc /MTd /DWIN32=1 /D_WINDOWS /D_DEBUG
 *   /D_ITERATOR_DEBUG_LEVEL=0 /DNOMINMAX
 *   src/games/expeditionsrome_dlss/tests/utility_shaders.cpp
 *   /Fobuild/utility_shaders.obj /Febuild/utility_shaders.exe
 *   /link d3d11.lib dxgi.lib d3dcompiler.lib
 * Run build/utility_shaders.exe with the addon source folder as its argument.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>

#include "../native_state.hpp"

namespace {

using Microsoft::WRL::ComPtr;
constexpr uint32_t WIDTH = 13;
constexpr uint32_t HEIGHT = 9;
constexpr uint32_t PIXELS = WIDTH * HEIGHT;

struct Texture {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

struct PreparationConstants {
  std::array<std::array<float, 4>, 4> clip_to_previous = {{{1.f, 0.f, 0.f, 0.f},
                                                           {0.f, 1.f, 0.f, 0.f},
                                                           {0.f, 0.f, 1.f, 0.f},
                                                           {0.f, 0.f, 0.f, 1.f}}};
  std::array<uint32_t, 2> extent = {WIDTH, HEIGHT};
  std::array<float, 2> pixel_jitter = {};
};
static_assert(sizeof(PreparationConstants) == 80);
static_assert(offsetof(PreparationConstants, pixel_jitter) == 72);

void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
}

void ExpectNear(float actual, float expected, const std::string& label, float tolerance = 0.002f) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(label + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
  }
}

Texture CreateTexture(ID3D11Device* device, DXGI_FORMAT format, uint32_t pixel_bytes, const void* initial_data,
                      uint32_t bind_flags, uint32_t width = WIDTH, uint32_t height = HEIGHT) {
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = width,
      .Height = height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = format,
      .SampleDesc = {.Count = 1, .Quality = 0},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = bind_flags,
      .CPUAccessFlags = 0,
      .MiscFlags = 0,
  };
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = initial_data, .SysMemPitch = width * pixel_bytes, .SysMemSlicePitch = 0};
  Texture texture;
  Check(device->CreateTexture2D(&desc, initial_data != nullptr ? &data : nullptr, texture.resource.GetAddressOf()));
  if ((bind_flags & D3D11_BIND_SHADER_RESOURCE) != 0) {
    Check(device->CreateShaderResourceView(texture.resource.Get(), nullptr, texture.srv.GetAddressOf()));
  }
  if ((bind_flags & D3D11_BIND_UNORDERED_ACCESS) != 0) {
    Check(device->CreateUnorderedAccessView(texture.resource.Get(), nullptr, texture.uav.GetAddressOf()));
  }
  return texture;
}

ComPtr<ID3D11ComputeShader> CompileShader(ID3D11Device* device, const std::filesystem::path& path) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  const HRESULT result = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
                                            D3DCOMPILE_ENABLE_STRICTNESS, 0, code.GetAddressOf(), errors.GetAddressOf());
  if (errors != nullptr) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
  Check(result);
  ComPtr<ID3D11ComputeShader> shader;
  Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
  return shader;
}

template <typename Pixel>
std::vector<Pixel> ReadTexture(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
  context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapping;
  Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapping));
  std::vector<Pixel> output(desc.Width * desc.Height);
  for (uint32_t row = 0; row < desc.Height; ++row) {
    std::memcpy(output.data() + row * desc.Width, static_cast<const uint8_t*>(mapping.pData) + row * mapping.RowPitch,
                desc.Width * sizeof(Pixel));
  }
  context->Unmap(staging.Get(), 0);
  return output;
}

void ExpectMotion(const std::array<uint16_t, 2>& actual, float x, float y, const std::string& label) {
  ExpectNear(DirectX::PackedVector::XMConvertHalfToFloat(actual[0]), x, label + " x");
  ExpectNear(DirectX::PackedVector::XMConvertHalfToFloat(actual[1]), y, label + " y");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    const std::filesystem::path shader_folder = (argc > 1 ? argv[1] : L"D:/Dev/renodx/src/games/expeditionsrome_dlss");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const bool debug_available = SUCCEEDED(D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION,
        device.GetAddressOf(), nullptr, context.GetAddressOf()));
    if (!debug_available) {
      Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                              device.ReleaseAndGetAddressOf(), nullptr, context.ReleaseAndGetAddressOf()));
    }
    const auto prepare = CompileShader(device.Get(), shader_folder / "0xB69D8215.cs_5_0.hlsl");
    const auto merge = CompileShader(device.Get(), shader_folder / "0xA3450ADB.cs_5_0.hlsl");
    std::vector<float> depths(PIXELS, 0.1f);
    std::vector<std::array<uint16_t, 2>> velocities(PIXELS, {0, 0});
    auto input_depth = CreateTexture(device.Get(), DXGI_FORMAT_R32_FLOAT, sizeof(float), depths.data(), D3D11_BIND_SHADER_RESOURCE);
    auto input_velocity = CreateTexture(device.Get(), DXGI_FORMAT_R16G16_UNORM, 4, velocities.data(), D3D11_BIND_SHADER_RESOURCE);
    auto output_depth = CreateTexture(device.Get(), DXGI_FORMAT_R32_FLOAT, sizeof(float), nullptr, D3D11_BIND_UNORDERED_ACCESS);
    auto output_motion = CreateTexture(device.Get(), DXGI_FORMAT_R16G16_FLOAT, 4, nullptr, D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11Buffer> constants_buffer;
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = sizeof(PreparationConstants),
                                              .Usage = D3D11_USAGE_DEFAULT,
                                              .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                              .CPUAccessFlags = 0,
                                              .MiscFlags = 0,
                                              .StructureByteStride = 0};
    Check(device->CreateBuffer(&constants_desc, nullptr, constants_buffer.GetAddressOf()));
    const auto run_preparation = [&](const PreparationConstants& constants) {
      D3D11_TEXTURE2D_DESC input_desc = {};
      input_depth.resource->GetDesc(&input_desc);
      context->UpdateSubresource(input_depth.resource.Get(), 0, nullptr, depths.data(), input_desc.Width * sizeof(float), 0);
      context->UpdateSubresource(input_velocity.resource.Get(), 0, nullptr, velocities.data(), input_desc.Width * 4, 0);
      context->UpdateSubresource(constants_buffer.Get(), 0, nullptr, &constants, 0, 0);
      const std::array srvs = {input_depth.srv.Get(), input_velocity.srv.Get()};
      const std::array uavs = {output_depth.uav.Get(), output_motion.uav.Get()};
      context->CSSetShader(prepare.Get(), nullptr, 0);
      context->CSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
      context->CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
      ID3D11Buffer* buffer = constants_buffer.Get();
      context->CSSetConstantBuffers(0, 1, &buffer);
      context->Dispatch((constants.extent[0] + 7) / 8, (constants.extent[1] + 7) / 8, 1);
      context->ClearState();
      return ReadTexture<std::array<uint16_t, 2>>(device.Get(), context.Get(), output_motion.resource.Get());
    };

    PreparationConstants constants;
    auto motion = run_preparation(constants);
    for (uint32_t pixel = 0; pixel < PIXELS; ++pixel) ExpectMotion(motion[pixel], 0.f, 0.f, "stationary identity");
    for (const float depth : ReadTexture<float>(device.Get(), context.Get(), output_depth.resource.Get())) {
      ExpectNear(depth, 0.1f, "center depth copy", 0.f);
    }

    constants.clip_to_previous[3][0] = 2.f / WIDTH;
    constants.clip_to_previous[3][1] = -4.f / HEIGHT;
    motion = run_preparation(constants);
    for (uint32_t pixel = 0; pixel < PIXELS; ++pixel) ExpectMotion(motion[pixel], 1.f, 2.f, "camera motion sign");

    const auto encode_velocity = [](float ndc_x, float ndc_y) {
      return std::array<uint16_t, 2>{
          static_cast<uint16_t>(std::lround((ndc_x + 2.00397754f) / 4.00801611f * 65535.f)),
          static_cast<uint16_t>(std::lround((ndc_y + 2.00397754f) / 4.00801611f * 65535.f))};
    };
    const auto expected_motion = [](const std::array<uint16_t, 2>& packed) {
      return std::array<float, 2>{-(packed[0] / 65535.f * 4.00801611f - 2.00397754f) * WIDTH * 0.5f,
                                  (packed[1] / 65535.f * 4.00801611f - 2.00397754f) * HEIGHT * 0.5f};
    };
    constexpr uint32_t CENTER = 4 * WIDTH + 4;
    velocities[CENTER] = encode_velocity(2.f / WIDTH, -4.f / HEIGHT);
    motion = run_preparation(constants);
    auto expected = expected_motion(velocities[CENTER]);
    ExpectMotion(motion[CENTER], expected[0], expected[1], "object motion overrides camera");

    depths[CENTER] = 0.2f;
    depths[CENTER - WIDTH - 1] = 0.9f;
    velocities[CENTER - WIDTH - 1] = encode_velocity(-4.f / WIDTH, 2.f / HEIGHT);
    motion = run_preparation(constants);
    expected = expected_motion(velocities[CENTER - WIDTH - 1]);
    ExpectMotion(motion[CENTER], expected[0], expected[1], "reverse-Z nearest diagonal");
    ExpectNear(ReadTexture<float>(device.Get(), context.Get(), output_depth.resource.Get())[CENTER], 0.2f,
               "depth is not dilated", 0.f);

    depths[CENTER - WIDTH + 1] = 0.9f;
    depths[CENTER + WIDTH - 1] = 0.9f;
    depths[CENTER + WIDTH + 1] = 0.9f;
    velocities[CENTER + WIDTH + 1] = encode_velocity(6.f / WIDTH, -6.f / HEIGHT);
    motion = run_preparation(constants);
    expected = expected_motion(velocities[CENTER + WIDTH + 1]);
    ExpectMotion(motion[CENTER], expected[0], expected[1], "diagonal tie chooses bottom right");
    depths[CENTER] = 0.9f;
    motion = run_preparation(constants);
    expected = expected_motion(velocities[CENTER]);
    ExpectMotion(motion[CENTER], expected[0], expected[1], "equal foreground keeps center");

    std::fill(velocities.begin(), velocities.end(), std::array<uint16_t, 2>{0, 0});
    depths[CENTER] = 0.2f;
    constants = {};
    constants.clip_to_previous[2][0] = 2.f / WIDTH;
    motion = run_preparation(constants);
    ExpectMotion(motion[CENTER], 0.9f, 0.f, "camera fallback uses selected depth");
    constants = {};
    constants.clip_to_previous[0][3] = 0.25f;
    motion = run_preparation(constants);
    const float current_ndc_x = (4.5f / WIDTH) * 2.f - 1.f;
    const float current_ndc_y = 1.f - (4.5f / HEIGHT) * 2.f;
    const float previous_w = current_ndc_x * 0.25f + 1.f;
    ExpectMotion(motion[CENTER], (current_ndc_x / previous_w - current_ndc_x) * WIDTH * 0.5f,
                 (current_ndc_y / previous_w - current_ndc_y) * HEIGHT * -0.5f,
                 "camera uses center coordinates, not diagonal coordinates");

    std::fill(depths.begin(), depths.end(), 0.2f);
    constants.clip_to_previous = {{{1.05f, 0.07f, 0.f, 0.16f},
                                   {-0.04f, 0.96f, 0.f, -0.11f},
                                   {0.3f, -0.22f, 1.f, 0.025f},
                                   {0.04f, 0.09f, 0.f, 1.f}}};
    for (const std::array<float, 2> jitter : {std::array{0.3125f, -0.375f}, std::array{-0.4375f, 0.388889f}}) {
      constants.pixel_jitter = jitter;
      motion = run_preparation(constants);
      for (uint32_t pixel = 0; pixel < PIXELS; ++pixel) {
        const float ndc_x = ((pixel % WIDTH + 0.5f - jitter[0]) / WIDTH) * 2.f - 1.f;
        const float ndc_y = 1.f - ((pixel / WIDTH + 0.5f - jitter[1]) / HEIGHT) * 2.f;
        std::array<float, 4> previous_clip = {};
        for (uint32_t channel = 0; channel < previous_clip.size(); ++channel) {
          previous_clip[channel] = ndc_x * constants.clip_to_previous[0][channel]
                                   + ndc_y * constants.clip_to_previous[1][channel]
                                   + 0.2f * constants.clip_to_previous[2][channel]
                                   + constants.clip_to_previous[3][channel];
        }
        ExpectMotion(motion[pixel], (previous_clip[0] / previous_clip[3] - ndc_x) * WIDTH * 0.5f,
                     (previous_clip[1] / previous_clip[3] - ndc_y) * HEIGHT * -0.5f,
                     "fractional jitter removed before projective camera reconstruction");
      }
    }

    constants = {};
    constants.clip_to_previous[3][3] = 0.f;
    motion = run_preparation(constants);
    ExpectMotion(motion[CENTER], 0.f, 0.f, "zero clip w guard");
    constants.clip_to_previous[3][0] = std::numeric_limits<float>::quiet_NaN();
    motion = run_preparation(constants);
    ExpectMotion(motion[CENTER], 0.f, 0.f, "nonfinite matrix guard");
    constants = {};
    depths[CENTER] = std::numeric_limits<float>::quiet_NaN();
    motion = run_preparation(constants);
    ExpectMotion(motion[CENTER], 0.f, 0.f, "nonfinite depth guard");
    ExpectNear(ReadTexture<float>(device.Get(), context.Get(), output_depth.resource.Get())[CENTER], 0.f,
               "nonfinite center depth becomes far", 0.f);
    constants.clip_to_previous[3][0] = 1.e8f;
    motion = run_preparation(constants);
    ExpectMotion(motion[CENTER], 65504.f, 0.f, "finite half-float range guard");

    constexpr uint32_t PADDED_WIDTH = 16;
    depths.assign(PADDED_WIDTH * HEIGHT, 0.99f);
    velocities.assign(PADDED_WIDTH * HEIGHT, encode_velocity(-1.5f, 1.5f));
    for (uint32_t row = 0; row < HEIGHT; ++row) {
      std::fill_n(depths.begin() + row * PADDED_WIDTH, WIDTH, 0.2f);
      std::fill_n(velocities.begin() + row * PADDED_WIDTH, WIDTH, std::array<uint16_t, 2>{0, 0});
    }
    input_depth = CreateTexture(device.Get(), DXGI_FORMAT_R32_FLOAT, sizeof(float), depths.data(),
                                D3D11_BIND_SHADER_RESOURCE, PADDED_WIDTH, HEIGHT);
    input_velocity = CreateTexture(device.Get(), DXGI_FORMAT_R16G16_UNORM, 4, velocities.data(),
                                   D3D11_BIND_SHADER_RESOURCE, PADDED_WIDTH, HEIGHT);
    constants = {};
    constants.clip_to_previous[2][0] = 2.f / WIDTH;
    constants.pixel_jitter = {0.3125f, -0.375f};
    motion = run_preparation(constants);
    for (uint32_t pixel = 0; pixel < PIXELS; ++pixel) {
      ExpectMotion(motion[pixel], 0.2f, 0.f, "active-width clamp excludes poison padding motion/depth");
    }
    for (const float depth : ReadTexture<float>(device.Get(), context.Get(), output_depth.resource.Get())) {
      ExpectNear(depth, 0.2f, "active depth copy excludes padded columns", 0.f);
    }

    constexpr uint32_t OUTPUT_WIDTH = 31;
    constexpr uint32_t OUTPUT_HEIGHT = 17;
    constexpr uint32_t OUTPUT_PIXELS = OUTPUT_WIDTH * OUTPUT_HEIGHT;
    std::vector<std::array<uint16_t, 4>> dlaa_color(OUTPUT_PIXELS);
    std::vector<std::array<uint16_t, 4>> stock_color(OUTPUT_PIXELS);
    for (uint32_t pixel = 0; pixel < OUTPUT_PIXELS; ++pixel) {
      dlaa_color[pixel] = {DirectX::PackedVector::XMConvertFloatToHalf(4.f),
                           DirectX::PackedVector::XMConvertFloatToHalf(-0.5f),
                           DirectX::PackedVector::XMConvertFloatToHalf(0.25f),
                           0x7E00};  // SDK alpha is ignored, even when invalid.
      stock_color[pixel] = {DirectX::PackedVector::XMConvertFloatToHalf(0.5f),
                            DirectX::PackedVector::XMConvertFloatToHalf(-0.25f),
                            DirectX::PackedVector::XMConvertFloatToHalf(2.f),
                            DirectX::PackedVector::XMConvertFloatToHalf(pixel / static_cast<float>(OUTPUT_PIXELS))};
      if (pixel % 17 == 0) {
        dlaa_color[pixel][0] = 0x7E00;  // Half quiet NaN.
      } else if (pixel % 17 == 1) {
        dlaa_color[pixel][1] = 0x7C00;  // Half positive infinity.
      } else if (pixel % 17 == 2) {
        dlaa_color[pixel][2] = 0xFC00;  // Half negative infinity.
      }
    }
    dlaa_color.back()[0] = 0x7E00;
    auto input_dlaa = CreateTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 8, dlaa_color.data(),
                                    D3D11_BIND_SHADER_RESOURCE, OUTPUT_WIDTH, OUTPUT_HEIGHT);
    auto input_stock = CreateTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 8, stock_color.data(),
                                     D3D11_BIND_SHADER_RESOURCE, OUTPUT_WIDTH, OUTPUT_HEIGHT);
    auto output_color = CreateTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 8, nullptr,
                                      D3D11_BIND_UNORDERED_ACCESS, OUTPUT_WIDTH, OUTPUT_HEIGHT);
    const std::array merge_srvs = {input_dlaa.srv.Get(), input_stock.srv.Get()};
    context->CSSetShader(merge.Get(), nullptr, 0);
    context->CSSetShaderResources(0, static_cast<UINT>(merge_srvs.size()), merge_srvs.data());
    ID3D11UnorderedAccessView* color_uav = output_color.uav.Get();
    context->CSSetUnorderedAccessViews(0, 1, &color_uav, nullptr);
    context->Dispatch((OUTPUT_WIDTH + 7) / 8, (OUTPUT_HEIGHT + 7) / 8, 1);
    context->ClearState();
    const auto merged = ReadTexture<std::array<uint16_t, 4>>(device.Get(), context.Get(), output_color.resource.Get());
    for (uint32_t pixel = 0; pixel < OUTPUT_PIXELS; ++pixel) {
      const bool finite_rgb = std::isfinite(DirectX::PackedVector::XMConvertHalfToFloat(dlaa_color[pixel][0]))
                              && std::isfinite(DirectX::PackedVector::XMConvertHalfToFloat(dlaa_color[pixel][1]))
                              && std::isfinite(DirectX::PackedVector::XMConvertHalfToFloat(dlaa_color[pixel][2]));
      const auto& expected_color = (finite_rgb ? dlaa_color[pixel] : stock_color[pixel]);
      for (uint32_t channel = 0; channel < 3; ++channel) {
        ExpectNear(DirectX::PackedVector::XMConvertHalfToFloat(merged[pixel][channel]),
                   DirectX::PackedVector::XMConvertHalfToFloat(expected_color[channel]),
                   (finite_rgb ? "merge preserves finite DLAA HDR RGB" : "merge falls back to stock RGB"), 0.f);
      }
      if (merged[pixel][3] != stock_color[pixel][3]) throw std::runtime_error("merge changed stock alpha bits");
    }

    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext1> context1;
    Check(device.As(&device1));
    Check(context.As(&context1));
    ComPtr<ID3DDeviceContextState> private_state;
    const auto feature_level = device->GetFeatureLevel();
    Check(device1->CreateDeviceContextState(0, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                            nullptr, private_state.GetAddressOf()));
    ComPtr<ID3D11Buffer> ranged_buffer;
    const D3D11_BUFFER_DESC ranged_desc = {.ByteWidth = 2048,
                                           .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                           .CPUAccessFlags = 0,
                                           .MiscFlags = 0,
                                           .StructureByteStride = 0};
    Check(device->CreateBuffer(&ranged_desc, nullptr, ranged_buffer.GetAddressOf()));
    ID3D11Buffer* ranged_cb = ranged_buffer.Get();
    const UINT first_constant = 16;
    const UINT constant_count = 16;
    context1->CSSetConstantBuffers1(1, 1, &ranged_cb, &first_constant, &constant_count);
    context1->PSSetConstantBuffers1(1, 1, &ranged_cb, &first_constant, &constant_count);
    context->CSSetShader(prepare.Get(), nullptr, 0);
    ID3D11ShaderResourceView* original_srv = input_depth.srv.Get();
    context->CSSetShaderResources(3, 1, &original_srv);
    ID3D11UnorderedAccessView* original_uav = output_motion.uav.Get();
    context->CSSetUnorderedAccessViews(1, 1, &original_uav, nullptr);
    auto raster_target = CreateTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 8, nullptr, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11RenderTargetView> raster_view;
    Check(device->CreateRenderTargetView(raster_target.resource.Get(), nullptr, raster_view.GetAddressOf()));
    ID3D11RenderTargetView* original_rtv = raster_view.Get();
    context->OMSetRenderTargets(1, &original_rtv, nullptr);
    ComPtr<ID3D11BlendState> blend;
    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0] = {.BlendEnable = TRUE,
                                  .SrcBlend = D3D11_BLEND_ONE,
                                  .DestBlend = D3D11_BLEND_ZERO,
                                  .BlendOp = D3D11_BLEND_OP_ADD,
                                  .SrcBlendAlpha = D3D11_BLEND_ONE,
                                  .DestBlendAlpha = D3D11_BLEND_ZERO,
                                  .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                                  .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL};
    Check(device->CreateBlendState(&blend_desc, blend.GetAddressOf()));
    const std::array<float, 4> blend_factors = {0.125f, 0.25f, 0.5f, 0.75f};
    context->OMSetBlendState(blend.Get(), blend_factors.data(), 0xDEADFACE);
    const D3D11_VIEWPORT viewport = {.TopLeftX = 0.25f, .TopLeftY = 0.5f, .Width = 7.5f, .Height = 6.5f, .MinDepth = 0.2f, .MaxDepth = 0.8f};
    context->RSSetViewports(1, &viewport);
    const D3D11_RECT scissor = {.left = 1, .top = 2, .right = 9, .bottom = 8};
    context->RSSetScissorRects(1, &scissor);
    ComPtr<ID3D11Predicate> predicate;
    const D3D11_QUERY_DESC query_desc = {.Query = D3D11_QUERY_OCCLUSION_PREDICATE, .MiscFlags = 0};
    Check(device->CreatePredicate(&query_desc, predicate.GetAddressOf()));
    context->Begin(predicate.Get());
    context->End(predicate.Get());
    context->SetPredication(predicate.Get(), FALSE);
    {
      expeditionsrome::dlss::ContextStateScope state_scope(context1.Get(), private_state.Get());
      ComPtr<ID3D11ComputeShader> cleared_shader;
      context->CSGetShader(cleared_shader.GetAddressOf(), nullptr, nullptr);
      if (cleared_shader != nullptr) throw std::runtime_error("private state was not cleared");
      context->CSSetShader(merge.Get(), nullptr, 0);
      context->CSSetShaderResources(0, static_cast<UINT>(merge_srvs.size()), merge_srvs.data());
      context->CSSetUnorderedAccessViews(0, 1, &color_uav, nullptr);
      context->CSSetConstantBuffers(0, 1, &ranged_cb);
      context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    }
    ComPtr<ID3D11ComputeShader> restored_shader;
    context->CSGetShader(restored_shader.GetAddressOf(), nullptr, nullptr);
    if (restored_shader.Get() != prepare.Get()) throw std::runtime_error("compute shader not restored");
    ComPtr<ID3D11ShaderResourceView> restored_srv;
    context->CSGetShaderResources(3, 1, restored_srv.GetAddressOf());
    if (restored_srv.Get() != original_srv) throw std::runtime_error("compute SRV not restored");
    ComPtr<ID3D11UnorderedAccessView> restored_uav;
    context->CSGetUnorderedAccessViews(1, 1, restored_uav.GetAddressOf());
    if (restored_uav.Get() != original_uav) throw std::runtime_error("compute UAV not restored");
    for (uint32_t stage = 0; stage < 2; ++stage) {
      ComPtr<ID3D11Buffer> restored_buffer;
      UINT restored_first = 0;
      UINT restored_count = 0;
      if (stage == 0) {
        context1->CSGetConstantBuffers1(1, 1, restored_buffer.GetAddressOf(), &restored_first, &restored_count);
      } else {
        context1->PSGetConstantBuffers1(1, 1, restored_buffer.GetAddressOf(), &restored_first, &restored_count);
      }
      if (restored_buffer.Get() != ranged_cb || restored_first != first_constant || restored_count != constant_count) {
        throw std::runtime_error("constant buffer or D3D11.1 subrange not restored");
      }
    }
    ComPtr<ID3D11RenderTargetView> restored_rtv;
    context->OMGetRenderTargets(1, restored_rtv.GetAddressOf(), nullptr);
    if (restored_rtv.Get() != original_rtv) throw std::runtime_error("render target not restored");
    ComPtr<ID3D11BlendState> restored_blend;
    std::array<float, 4> restored_factors = {};
    UINT restored_mask = 0;
    context->OMGetBlendState(restored_blend.GetAddressOf(), restored_factors.data(), &restored_mask);
    if (restored_blend.Get() != blend.Get() || restored_factors != blend_factors || restored_mask != 0xDEADFACE) {
      throw std::runtime_error("blend state not restored");
    }
    D3D11_VIEWPORT restored_viewport = {};
    UINT viewport_count = 1;
    context->RSGetViewports(&viewport_count, &restored_viewport);
    if (viewport_count != 1 || std::memcmp(&viewport, &restored_viewport, sizeof(viewport)) != 0) {
      throw std::runtime_error("viewport not restored");
    }
    D3D11_RECT restored_scissor = {};
    UINT scissor_count = 1;
    context->RSGetScissorRects(&scissor_count, &restored_scissor);
    if (scissor_count != 1 || std::memcmp(&scissor, &restored_scissor, sizeof(scissor)) != 0) {
      throw std::runtime_error("scissor not restored");
    }
    ComPtr<ID3D11Predicate> restored_predicate;
    BOOL restored_predicate_value = TRUE;
    context->GetPredication(restored_predicate.GetAddressOf(), &restored_predicate_value);
    if (restored_predicate.Get() != predicate.Get() || restored_predicate_value != FALSE) {
      throw std::runtime_error("predication not restored");
    }
    context->ClearState();
    if (debug_available) {
      ComPtr<ID3D11InfoQueue> info_queue;
      Check(device.As(&info_queue));
      for (uint64_t index = 0; index < info_queue->GetNumStoredMessages(); ++index) {
        SIZE_T message_bytes = 0;
        Check(info_queue->GetMessage(index, nullptr, &message_bytes));
        std::vector<uint8_t> message_storage(message_bytes);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(message_storage.data());
        Check(info_queue->GetMessage(index, message, &message_bytes));
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
          throw std::runtime_error(std::string("D3D11 debug warning/error: ") + message->pDescription);
        }
      }
    }
    std::cout << "PASS: utility shaders compile and execute on D3D11 WARP; 13x9 bounds, stationary/camera/object motion, "
                 "reverse-Z dilation/ties, center-depth preservation, nonfinite guards, half range, "
                 "fractional jitter/projective reprojection, 16-wide poison padding excluded from 13-wide inputs, "
                 "31x17 finite HDR RGB/stock-alpha merge and invalid DLAA RGB fallback at every output pixel, "
                 "native shader/SRV/UAV/ranged-CB/RTV/blend/viewport/scissor/predication restoration.\n"
              << (debug_available ? "D3D11 debug layer: no warnings or errors.\n" : "D3D11 debug layer unavailable; hazard diagnostics not verified.\n");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
