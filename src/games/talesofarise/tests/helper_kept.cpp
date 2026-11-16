/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * Ray Tracing in the Helper's copies (ray_tracing.hpp Keep) on WARP: a traced texture at the bridge's size (the render
 * pixels at its top-left) copied to the scene depth's size, then read as the lighting passes read t41 / t43 / t44
 * (rt_ambient.hlsli: by the buffer UV), gives each render pixel its own traced texel; the in-process rays' contract.
 * Control: the bridge's texture read the same way (no copy) misses them. Run by test/tw_tales_unit (after the
 * talesofarise target); its argument is the add-on's source folder.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "../ray_tracing.hpp"

namespace {

using Microsoft::WRL::ComPtr;
constexpr std::array<UINT, 2> BRIDGE = {16, 12};
constexpr std::array<UINT, 2> DEPTH = {11, 8};
constexpr std::array<UINT, 2> RENDER = {10, 7};

void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
}

float Traced(UINT x, UINT y) {
  return static_cast<float>(1 + x + (y * BRIDGE[0]));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc < 2) throw std::runtime_error("the add-on's source folder as the first argument");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                            context.GetAddressOf()));
    // The bridge's traced texture (R32F here: Keep copies any format).
    std::vector<float> traced;
    for (UINT y = 0; y < BRIDGE[1]; ++y) {
      for (UINT x = 0; x < BRIDGE[0]; ++x) traced.push_back(Traced(x, y));
    }
    const D3D11_TEXTURE2D_DESC bridge_desc = {.Width = BRIDGE[0], .Height = BRIDGE[1], .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                              .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA bridge_data = {.pSysMem = traced.data(), .SysMemPitch = BRIDGE[0] * 4};
    ComPtr<ID3D11Texture2D> bridge;
    ComPtr<ID3D11ShaderResourceView> bridge_view;
    Check(device->CreateTexture2D(&bridge_desc, &bridge_data, bridge.GetAddressOf()));
    Check(device->CreateShaderResourceView(bridge.Get(), nullptr, bridge_view.GetAddressOf()));
    auto& state = talesofarise::dlss::ray_tracing::internal::state;
    state.render_size = RENDER;
    state.depth_size = DEPTH;
    talesofarise::dlss::ray_tracing::internal::Kept kept;
    if (!talesofarise::dlss::ray_tracing::internal::Keep(context.Get(), bridge_view.Get(), 5, &kept)) throw std::runtime_error("Keep failed");
    if (talesofarise::dlss::ray_tracing::internal::ThisFrame(kept, 5) != kept.srv || talesofarise::dlss::ray_tracing::internal::ThisFrame(kept, 6)) {
      throw std::runtime_error("ThisFrame: the copy's frame");
    }
    // rt_ambient.hlsli's read at each render pixel's buffer UV ((pixel + 0.5) / the scene depth's size).
    const std::string source =
        "#include \"rt_ambient.hlsli\"\n"
        "RWStructuredBuffer<float> output : register(u0);\n"
        "cbuffer Size : register(b0) { uint2 render; float2 inverse_buffer; };\n"
        "[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  output[id.x + id.y * render.x] = RayTracedAmbientOcclusion((float2(id.xy) + 0.5f) * inverse_buffer, -1.f);\n"
        "}\n";
    const std::string name = (std::filesystem::path(argv[1]) / "helper_kept_test.hlsl").string();
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT compiled = D3DCompile(source.data(), source.size(), name.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0", 0, 0,
                                        code.GetAddressOf(), errors.GetAddressOf());
    if (errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
    Check(compiled);
    ComPtr<ID3D11ComputeShader> shader;
    Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
    struct Size {
      std::array<UINT, 2> render;
      std::array<float, 2> inverse_buffer;
    };
    const Size size = {.render = RENDER, .inverse_buffer = {1.f / DEPTH[0], 1.f / DEPTH[1]}};
    const D3D11_BUFFER_DESC size_desc = {.ByteWidth = sizeof(Size), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA size_data = {.pSysMem = &size};
    ComPtr<ID3D11Buffer> size_buffer;
    Check(device->CreateBuffer(&size_desc, &size_data, size_buffer.GetAddressOf()));
    const UINT pixels = RENDER[0] * RENDER[1];
    const D3D11_BUFFER_DESC output_desc = {.ByteWidth = pixels * 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                           .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 4};
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = pixels * 4, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    // The largest difference from each render pixel's own traced texel, reading `view` as t41.
    const auto read = [&](ID3D11ShaderResourceView* view) {
      ComPtr<ID3D11Buffer> output;
      ComPtr<ID3D11Buffer> staging;
      ComPtr<ID3D11UnorderedAccessView> output_view;
      Check(device->CreateBuffer(&output_desc, nullptr, output.GetAddressOf()));
      Check(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()));
      Check(device->CreateUnorderedAccessView(output.Get(), nullptr, output_view.GetAddressOf()));
      context->CSSetShader(shader.Get(), nullptr, 0);
      context->CSSetShaderResources(41, 1, &view);
      context->CSSetConstantBuffers(0, 1, size_buffer.GetAddressOf());
      context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
      context->Dispatch(RENDER[0], RENDER[1], 1);
      context->CopyResource(staging.Get(), output.Get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
      std::vector<float> values(pixels);
      std::memcpy(values.data(), mapped.pData, pixels * 4);
      context->Unmap(staging.Get(), 0);
      float error = 0.f;
      for (UINT y = 0; y < RENDER[1]; ++y) {
        for (UINT x = 0; x < RENDER[0]; ++x) error = std::max(error, std::abs(values[x + (y * RENDER[0])] - Traced(x, y)));
      }
      return error;
    };
    const float kept_error = read(kept.srv.Get());
    const float control_error = read(bridge_view.Get());
    std::cout << "the kept copy read by UV: max error " << kept_error << " (exact); control, the bridge's texture: " << control_error << "\n";
    if (kept_error != 0.f) throw std::runtime_error("the kept copy misses the render pixels");
    if (!(control_error >= 1.f)) throw std::runtime_error("the control was not caught");
    std::cout << "PASS helper_kept\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL helper_kept: " << error.what() << '\n';
    return 1;
  }
}
