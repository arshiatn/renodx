/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * The torch shadows' traced branch on WARP: the add-on's deferred local light shader (shaders/locallight00, ps 0x038D003A,
 * compiled from the source folder) lights one G-buffer pixel from a light 1 cm away, with the rays' lit bits at t42 / b9
 * (twattila/rt_local_lights.hlsli) and without them. Where the rays traced the light, the pixel must be exactly the game's
 * own draw at that shadow (lit: its shadow 1, blocked: 0, a ratio r: the game's at sqrt(r)), whatever the game's shadow map
 * says; a traced light more than 25 cm from the drawn one, or nothing bound, leaves the game's. Both mask layouts (a
 * pixel's bits the first 32 lights', and the 8 x 8 tiles' light lists) and the shadow-ratio words. CONTROL: lit and
 * blocked differ, and the shader with the rays' visibility dropped fails. Run by test/tw_tales_unit; its argument is the
 * add-on's source folder.
 */

#include <array>
#include <bit>
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

#include "./warp.hpp"

namespace {

using warp::Check;
using warp::ComPtr;
constexpr UINT EDGE = 8;
constexpr UINT TILE_ROW = EDGE;  // the tiles' lists below the pixels' words
constexpr int PIXEL = 3;

// The light pass's vertex output as the game's light volume gives it: TEXCOORD0 the clip position (the shader divides
// by w), then SV_Position.
constexpr char VERTEX_SHADER[] = R"(
struct Output { float4 position : TEXCOORD0; float4 pixel : SV_Position; };
Output main(uint id : SV_VertexID) {
  const float2 uv = float2((id << 1) & 2, id & 2);
  const float4 clip = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  Output output = { clip, clip };
  return output;
})";

struct Case {
  const char* name;
  float game_shadow = 1.f;     // t5.z, the game's shadow map term
  float shadowed = 1.f;        // cb1[8].x, the light casts the game's shadows
  bool mask = false;           // t42 / b9 bound
  uint32_t pixel_word = 0;     // the pixel's word in t42
  uint32_t count = 1;          // b9 x
  uint32_t ratio = 0;          // b9 w
  bool tiles = false;          // b9 z: TILE_ROW, tile (0, 0)'s list in `list`
  uint32_t list = 0xFFFFFF00u;  // tile (0, 0)'s first word: light indices a byte each
  std::array<float, 3> traced_light = {0.f, 0.f, 0.f};  // b9 light 0 (metres; the drawn light sits at 0)
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc < 2) throw std::runtime_error("the add-on's source folder as the first argument");
    const std::filesystem::path source = argv[1];
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                            context.GetAddressOf()));
    const auto vertex_code = warp::Compile(VERTEX_SHADER, source / "tests" / "light_volume.vs_5_0.hlsl", "vs_5_0");
    const auto shader_file = source / "shaders" / "locallight00_0x038D003A.ps_5_0.hlsl";
    const auto pixel_code = warp::Compile(shader_file, "ps_5_0");
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    Check(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf()));
    Check(device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(), nullptr, pixel_shader.GetAddressOf()));

    const auto texture = [&](DXGI_FORMAT format, UINT height, const void* texels, UINT pitch, UINT bind) {
      const D3D11_TEXTURE2D_DESC desc = {.Width = EDGE, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                         .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = texels, .SysMemPitch = pitch};
      ComPtr<ID3D11Texture2D> created;
      Check(device->CreateTexture2D(&desc, (texels != nullptr) ? &data : nullptr, created.GetAddressOf()));
      return created;
    };
    const auto view = [&](const ComPtr<ID3D11Texture2D>& resource) {
      ComPtr<ID3D11ShaderResourceView> created;
      Check(device->CreateShaderResourceView(resource.Get(), nullptr, created.GetAddressOf()));
      return created;
    };
    // A G-buffer texture of one value everywhere.
    const auto constant = [&](std::array<float, 4> value) {
      const std::vector<std::array<float, 4>> texels(EDGE * EDGE, value);
      return view(texture(DXGI_FORMAT_R32G32B32A32_FLOAT, EDGE, texels.data(), EDGE * 16, D3D11_BIND_SHADER_RESOURCE));
    };
    const auto buffer = [&](const std::vector<std::array<float, 4>>& registers) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(registers.size() * 16), .Usage = D3D11_USAGE_DEFAULT,
                                      .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = registers.data()};
      ComPtr<ID3D11Buffer> created;
      Check(device->CreateBuffer(&desc, &data, created.GetAddressOf()));
      return created;
    };

    // The View (b0): device depth -> scene depth 1 (cb0[57]), screen to UV (cb0[58]), screen-and-depth to world the
    // identity (cb0[44-47]): the pixel sits at (x, y, 1) cm; the camera (cb0[59]) behind it.
    std::vector<std::array<float, 4>> view_constants(175, {0.f, 0.f, 0.f, 0.f});
    view_constants[44] = {1.f, 0.f, 0.f, 0.f};
    view_constants[45] = {0.f, 1.f, 0.f, 0.f};
    view_constants[46] = {0.f, 0.f, 1.f, 0.f};
    view_constants[47] = {0.f, 0.f, 0.f, 1.f};
    view_constants[57] = {0.f, 0.f, 0.f, -1.f};
    view_constants[58] = {0.5f, -0.5f, 0.5f, 0.5f};
    view_constants[59] = {0.f, 0.f, -10.f, 0.f};
    view_constants[127] = {0.f, 0.f, 0.f, 1.f};
    const auto view_buffer = buffer(view_constants);
    // The G-buffer: depth (unused by cb0[57]), the normal toward the light (0, 0, -1), default lit (shading model 1), grey.
    const std::array<ComPtr<ID3D11ShaderResourceView>, 5> gbuffer = {
        constant({0.5f, 0.f, 0.f, 0.f}), constant({0.5f, 0.5f, 0.f, 1.f}), constant({0.5f, 0.5f, 0.5f, 1.f / 255.f}),
        constant({0.5f, 0.5f, 0.5f, 1.f}), constant({0.f, 0.f, 0.f, 0.f})};
    const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
    ComPtr<ID3D11SamplerState> sampler;
    Check(device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf()));
    std::array<ComPtr<ID3D11Texture2D>, 2> targets;
    std::array<ComPtr<ID3D11RenderTargetView>, 2> target_views;
    for (size_t index = 0; index < targets.size(); ++index) {
      targets[index] = texture(DXGI_FORMAT_R32G32B32A32_FLOAT, EDGE, nullptr, 0, D3D11_BIND_RENDER_TARGET);
      Check(device->CreateRenderTargetView(targets[index].Get(), nullptr, target_views[index].GetAddressOf()));
    }
    D3D11_TEXTURE2D_DESC staging_desc = {};
    targets[0]->GetDesc(&staging_desc);
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Check(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()));

    // One light-volume draw; the light pass's two outputs at the tested pixel.
    const auto draw = [&](const Case& test) {
      // The light (b1): at the origin (cm), radius 100 cm, white, a spot cone wide open toward the pixel (-z).
      std::vector<std::array<float, 4>> light(9, {0.f, 0.f, 0.f, 0.f});
      light[0] = {0.f, 0.f, 0.f, 0.01f};
      light[1] = {1.f, 1.f, 1.f, 1.f};
      light[2] = {0.f, 0.f, -1.f, 0.f};
      light[4] = {-2.f, 1.f, 0.f, 0.f};
      light[8] = {test.shadowed, 0.f, 0.f, 0.f};
      const auto light_buffer = buffer(light);
      const auto attenuation = constant({0.f, 0.f, test.game_shadow, 0.f});
      // b9 as rt_local_lights.hlsli declares it: 128 positions, the count word, 128 colours.
      std::vector<std::array<float, 4>> lights(257, {0.f, 0.f, 0.f, 0.f});
      lights[0] = {test.traced_light[0], test.traced_light[1], test.traced_light[2], 1.f};
      lights[1] = {0.f, 0.f, 0.f, 1.f};
      const std::array<uint32_t, 4> count_word = {test.count, 0u, test.tiles ? TILE_ROW : 0u, test.ratio};
      std::memcpy(lights[128].data(), count_word.data(), 16);
      const auto lights_buffer = buffer(lights);
      std::vector<uint32_t> words(EDGE * (TILE_ROW + 2), 0xFFFFFFFFu);
      for (UINT pixel = 0; pixel < EDGE * EDGE; ++pixel) words[pixel] = test.pixel_word;
      words[TILE_ROW * EDGE] = test.list;
      const auto mask = view(texture(DXGI_FORMAT_R32_UINT, TILE_ROW + 2, words.data(), EDGE * 4, D3D11_BIND_SHADER_RESOURCE));

      const std::array<float, 4> clear = {-1.f, -1.f, -1.f, -1.f};
      for (const auto& target : target_views) context->ClearRenderTargetView(target.Get(), clear.data());
      const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(EDGE), .Height = static_cast<float>(EDGE), .MaxDepth = 1.f};
      context->OMSetRenderTargets(2, target_views[0].GetAddressOf(), nullptr);
      context->RSSetViewports(1, &viewport);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vertex_shader.Get(), nullptr, 0);
      context->PSSetShader(pixel_shader.Get(), nullptr, 0);
      for (UINT slot = 0; slot < gbuffer.size(); ++slot) context->PSSetShaderResources(slot, 1, gbuffer[slot].GetAddressOf());
      context->PSSetShaderResources(5, 1, attenuation.GetAddressOf());
      ID3D11ShaderResourceView* const mask_view = test.mask ? mask.Get() : nullptr;
      context->PSSetShaderResources(42, 1, &mask_view);
      for (UINT slot = 0; slot < 6; ++slot) context->PSSetSamplers(slot, 1, sampler.GetAddressOf());
      context->PSSetConstantBuffers(0, 1, view_buffer.GetAddressOf());
      context->PSSetConstantBuffers(1, 1, light_buffer.GetAddressOf());
      context->PSSetConstantBuffers(9, 1, lights_buffer.GetAddressOf());
      context->Draw(3, 0);
      std::array<float, 8> outputs = {};
      for (size_t index = 0; index < targets.size(); ++index) {
        context->CopyResource(staging.Get(), targets[index].Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        std::memcpy(outputs.data() + (index * 4), static_cast<const uint8_t*>(mapped.pData) + (PIXEL * mapped.RowPitch) + (PIXEL * 16), 16);
        context->Unmap(staging.Get(), 0);
      }
      return outputs;
    };

    const auto game_lit = draw({.name = "game lit"});
    const auto game_blocked = draw({.name = "game blocked", .game_shadow = 0.f});
    const auto game_half = draw({.name = "game half", .game_shadow = 0.5f});
    for (const float value : game_lit) {
      if (!std::isfinite(value)) throw std::runtime_error("the light pass wrote a non-finite value");
    }
    const float lit_light = game_lit[0] + game_lit[1] + game_lit[2];
    const float blocked_light = game_blocked[0] + game_blocked[1] + game_blocked[2];
    std::cout << "the game's draw: lit rgb sum " << lit_light << ", blocked " << blocked_light << ", half "
              << (game_half[0] + game_half[1] + game_half[2]) << '\n';
    if (!(lit_light > 1e-4f && blocked_light < lit_light)) throw std::runtime_error("CONTROL: the pixel's light does not follow its shadow");
    if (draw({.name = "unshadowed", .game_shadow = 0.f, .shadowed = 0.f}) != game_lit) {
      throw std::runtime_error("a light without the game's shadow is not lit");
    }

    const uint32_t quarter = std::bit_cast<uint32_t>(0.25f);
    struct Expect {
      Case test;
      const std::array<float, 8>* equals;
    };
    const std::vector<Expect> expects = {
        {{.name = "bits: lit over the game's blocked map", .game_shadow = 0.f, .mask = true, .pixel_word = 1u}, &game_lit},
        {{.name = "bits: blocked over the game's lit map", .mask = true, .pixel_word = 0u}, &game_blocked},
        {{.name = "bits: blocked, a light the game leaves unshadowed", .shadowed = 0.f, .mask = true, .pixel_word = 0u}, &game_blocked},
        {{.name = "bits: another light's bit only", .mask = true, .pixel_word = 2u, .count = 2}, &game_blocked},
        {{.name = "bits: the traced light 20 cm off stands for it", .mask = true, .pixel_word = 0u, .traced_light = {0.2f, 0.f, 0.f}}, &game_blocked},
        {{.name = "bits: the traced light 30 cm off is another (the game's map)", .mask = true, .pixel_word = 0u, .traced_light = {0.3f, 0.f, 0.f}}, &game_lit},
        {{.name = "tiles: slot 0 = light 1, its bit 0 lit", .game_shadow = 0.f, .mask = true, .pixel_word = 1u, .count = 2, .tiles = true,
          .list = 0xFFFFFF01u, .traced_light = {5.f, 0.f, 0.f}}, &game_lit},
        {{.name = "tiles: slot 1 = light 1, bit 0 (light 0's) lit only", .mask = true, .pixel_word = 1u, .count = 2, .tiles = true,
          .list = 0xFFFF0100u, .traced_light = {5.f, 0.f, 0.f}}, &game_blocked},
        {{.name = "tiles: slot 1 = light 1, its bit 1 lit", .game_shadow = 0.f, .mask = true, .pixel_word = 2u, .count = 2, .tiles = true,
          .list = 0xFFFF0100u, .traced_light = {5.f, 0.f, 0.f}}, &game_lit},
        {{.name = "tiles: the list without the light (the game's map)", .game_shadow = 0.f, .mask = true, .pixel_word = 0u, .count = 2,
          .tiles = true, .list = 0xFFFFFF00u, .traced_light = {5.f, 0.f, 0.f}}, &game_blocked},
        {{.name = "ratio: 0.25 = the game's map at 0.5", .mask = true, .pixel_word = quarter, .ratio = 1u}, &game_half},
        {{.name = "ratio: own bits (top bit), lit", .game_shadow = 0.f, .mask = true, .pixel_word = 0x80000001u, .ratio = 1u}, &game_lit},
        {{.name = "ratio: own bits (top bit), blocked", .mask = true, .pixel_word = 0x80000000u, .ratio = 1u}, &game_blocked},
    };
    int failed = 0;
    for (const auto& expect : expects) {
      const bool same = draw(expect.test) == *expect.equals;
      std::cout << (same ? "PASS " : "FAIL ") << expect.test.name << '\n';
      failed += same ? 0 : 1;
    }
    if (failed != 0) throw std::runtime_error(std::to_string(failed) + " of " + std::to_string(expects.size()) + " cases");

    // CONTROL: the shader with the rays' visibility dropped (traced pixels lit) is caught by the blocked case.
    std::ifstream input(shader_file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string traced = "r0.z = traced_visibility;";
    const size_t at = text.find(traced);
    if (at == std::string::npos) throw std::runtime_error("CONTROL: no traced branch in the shader");
    text.replace(at, traced.size(), "r0.z = 1;");
    const auto broken_code = warp::Compile(text, shader_file, "ps_5_0");
    pixel_shader.Reset();
    Check(device->CreatePixelShader(broken_code->GetBufferPointer(), broken_code->GetBufferSize(), nullptr, pixel_shader.GetAddressOf()));
    if (draw(expects[1].test) == game_blocked) throw std::runtime_error("CONTROL: the dropped visibility was not caught");
    std::cout << "CONTROL: the shader without the rays' visibility fails '" << expects[1].test.name << "'\n";
    std::cout << "PASS torch_traced (" << expects.size() << " cases bit-exact)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL torch_traced: " << error.what() << '\n';
    return 1;
  }
}
