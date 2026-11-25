/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * Output SDR's encode on WARP: the add-on's swap chain shaders (swap_chain_proxy_*.hlsl, compiled from the source
 * folder with shared.h's constants at b13) draw a ramp of the game's gamma-encoded frame (its 16-bit clone: -0.25 to 1.25,
 * per channel apart) into the SDR chain's R10G10B10A2. The game alone writes that frame to its 8-bit back buffer, so every
 * texel must be the game's value clamped to 0 - 1 within the 10-bit step, and within half an 8-bit step plus that of the
 * game's own 8-bit output. CONTROL: the same ramp through the HDR10 branch (PQ of UI white) is caught. The 10-bit limit is
 * WARP's exact round to nearest: a GPU may convert float to UNORM within D3D's looser tolerance, so this runs on WARP only.
 * Run by test/tw_tales_unit (after the talesofarise target); its argument is the add-on's source folder.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "../shared.h"
#include "./warp.hpp"

namespace {

using warp::Check;
using warp::ComPtr;
constexpr UINT WIDTH = 1024;

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc < 2) throw std::runtime_error("the add-on's source folder as the first argument");
    const std::filesystem::path source = argv[1];
    ComPtr<ID3D11DeviceContext> context;
    const auto device = warp::Device(&context);
    const auto vertex_code = warp::Compile(source / "swap_chain_proxy_vertex_shader.vs_5_x.hlsl", "vs_5_0");
    const auto pixel_code = warp::Compile(source / "swap_chain_proxy_pixel_shader.ps_5_x.hlsl", "ps_5_0");
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    Check(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf()));
    Check(device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(), nullptr, pixel_shader.GetAddressOf()));

    // IEEE half from a float, rounded to nearest through its bits (normal range only: |value| in [2^-14, 65504] or 0).
    const auto float_to_half = [](float value) -> uint16_t {
      if (value == 0.f) return 0;
      uint32_t bits = 0;
      std::memcpy(&bits, &value, 4);
      const uint32_t sign = (bits >> 16) & 0x8000u;
      const int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127 + 15;
      const uint32_t mantissa = bits & 0x7FFFFFu;
      if (exponent <= 0) return static_cast<uint16_t>(sign);
      uint32_t half = (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
      if ((mantissa & 0x1FFFu) > 0x1000u || ((mantissa & 0x1FFFu) == 0x1000u && (half & 1u) != 0)) {
        ++half;
      }
      return static_cast<uint16_t>(sign | half);
    };
    // The ramp: red -0.25 .. 1.25, green 1.25 .. -0.25, blue half of red's (the clone's half floats), and the frame as
    // the clone holds it (each half back to a float).
    std::vector<uint16_t> ramp;
    std::vector<std::array<float, 3>> frame;
    for (UINT x = 0; x < WIDTH; ++x) {
      const float t = -0.25f + (1.5f * static_cast<float>(x) / static_cast<float>(WIDTH - 1));
      const std::array<uint16_t, 4> texel = {float_to_half(t), float_to_half(1.f - t), float_to_half(t * 0.5f), float_to_half(1.f)};
      ramp.insert(ramp.end(), texel.begin(), texel.end());
      std::array<float, 3> held = {};
      for (size_t channel = 0; channel < held.size(); ++channel) {
        const uint32_t exponent = (texel[channel] >> 10) & 0x1Fu;
        const uint32_t mantissa = texel[channel] & 0x3FFu;
        const float value = (exponent == 0) ? std::ldexp(static_cast<float>(mantissa), -24)
                                            : std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
        held[channel] = ((texel[channel] & 0x8000u) != 0) ? -value : value;
      }
      frame.push_back(held);
    }
    const D3D11_TEXTURE2D_DESC ramp_desc = {.Width = WIDTH, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                                            .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA ramp_data = {.pSysMem = ramp.data(), .SysMemPitch = WIDTH * 8};
    ComPtr<ID3D11Texture2D> ramp_texture;
    ComPtr<ID3D11ShaderResourceView> ramp_view;
    Check(device->CreateTexture2D(&ramp_desc, &ramp_data, ramp_texture.GetAddressOf()));
    Check(device->CreateShaderResourceView(ramp_texture.Get(), nullptr, ramp_view.GetAddressOf()));
    const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
    ComPtr<ID3D11SamplerState> sampler;
    Check(device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf()));
    const D3D11_TEXTURE2D_DESC target_desc = {.Width = WIDTH, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R10G10B10A2_UNORM,
                                              .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    Check(device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf()));
    Check(device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf()));
    D3D11_TEXTURE2D_DESC staging_desc = target_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Check(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()));

    // One draw of the ramp with Output `encoding` (shared.h: 0 HDR10, 2 SDR); each texel's largest channel distance from
    // the game's frame clamped to 0 - 1 (`ten_bit`), and from the game's own 8-bit output (`eight_bit`).
    struct Distance {
      float ten_bit = 0.f;
      float eight_bit = 0.f;
    };
    const auto draw = [&](float encoding) {
      // The constant buffer as the add-on fills it (b13: the whole struct, padded to 16 bytes).
      std::vector<uint8_t> constants((sizeof(ShaderInjectData) + 15) / 16 * 16, 0);
      ShaderInjectData injection = {};
      injection.peak_white_nits = 1000.f;
      injection.diffuse_white_nits = 203.f;
      injection.graphics_white_nits = 203.f;
      injection.swap_chain_encoding = encoding;
      injection.hdr = 1.f;
      std::memcpy(constants.data(), &injection, sizeof(injection));
      const D3D11_BUFFER_DESC buffer_desc = {.ByteWidth = static_cast<UINT>(constants.size()), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA buffer_data = {.pSysMem = constants.data()};
      ComPtr<ID3D11Buffer> buffer;
      Check(device->CreateBuffer(&buffer_desc, &buffer_data, buffer.GetAddressOf()));
      const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(WIDTH), .Height = 1.f, .MaxDepth = 1.f};
      context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
      context->RSSetViewports(1, &viewport);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vertex_shader.Get(), nullptr, 0);
      context->PSSetShader(pixel_shader.Get(), nullptr, 0);
      context->PSSetShaderResources(0, 1, ramp_view.GetAddressOf());
      context->PSSetSamplers(0, 1, sampler.GetAddressOf());
      context->PSSetConstantBuffers(13, 1, buffer.GetAddressOf());
      context->Draw(3, 0);
      context->CopyResource(staging.Get(), target.Get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
      std::vector<uint32_t> texels(WIDTH);
      std::memcpy(texels.data(), mapped.pData, WIDTH * 4);
      context->Unmap(staging.Get(), 0);
      Distance distance;
      for (UINT x = 0; x < WIDTH; ++x) {
        for (UINT channel = 0; channel < 3; ++channel) {
          const float written = static_cast<float>((texels[x] >> (10 * channel)) & 0x3FFu) / 1023.f;
          const float game = std::clamp(frame[x][channel], 0.f, 1.f);
          const float game_8_bit = std::round(game * 255.f) / 255.f;
          distance.ten_bit = std::max(distance.ten_bit, std::abs(written - game));
          distance.eight_bit = std::max(distance.eight_bit, std::abs(written - game_8_bit));
        }
      }
      return distance;
    };
    const Distance sdr = draw(2.f);
    const Distance hdr10 = draw(0.f);
    const float ten_bit_step = 0.5f / 1023.f;
    const float eight_bit_step = 0.5f / 255.f;
    std::cout << "SDR: " << sdr.ten_bit * 1023.f << " 10-bit steps from the game's frame (limit 0.5 + 1e-3), "
              << sdr.eight_bit * 255.f << " 8-bit steps from the game's 8-bit output (limit " << (eight_bit_step + ten_bit_step) * 255.f
              << "); CONTROL HDR10: " << hdr10.ten_bit * 1023.f << " 10-bit steps\n";
    if (!(sdr.ten_bit <= ten_bit_step + 1e-3f / 1023.f)) throw std::runtime_error("the SDR output is not the game's frame clamped");
    if (!(sdr.eight_bit <= eight_bit_step + ten_bit_step + 1e-6f)) throw std::runtime_error("the SDR output is off the game's 8-bit output");
    if (!(hdr10.ten_bit > 0.05f)) throw std::runtime_error("the control was not caught");
    std::cout << "PASS sdr_ramp\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL sdr_ramp: " << error.what() << '\n';
    return 1;
  }
}
