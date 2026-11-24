/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * Ray Tracing in the Helper's inputs (ray_tracing.hpp) on WARP and the CPU: TowardSun on the CPU against
 * sun_direction.hlsli's TowardSun (0x7A1E0D12's, the in-process rays' sun) on WARP over random ScreenToShadow /
 * ScreenToWorld rows; ViewCamera's metres against TranslatedWorldToClip on (centimetres + PreViewTranslation). Controls:
 * the direction negated (what TowardSun without its sign test gives in about half the cases), the camera without
 * PreViewTranslation, both caught.
 * Run by test/tw_tales_unit (after the talesofarise target), or from an x64 VS developer prompt at the repository root
 * (join these lines):
 * clang-cl /nologo /std:c++20 /EHsc /MTd /DWIN32=1 /D_WINDOWS /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=0 /DNOMINMAX /w
 *   /I. /Ibuild/talesofarise.include /Iexternal/reshade /Iexternal/reshade/include /Iexternal/Detours/include
 *   src/games/talesofarise/tests/helper_rays.cpp /Febuild/helper_rays.exe
 *   /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib advapi32.lib ole32.lib shell32.lib
 * Run build\helper_rays.exe with the add-on's source folder (src\games\talesofarise) as its argument.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "../ray_tracing.hpp"

// utils/settings.hpp (through ray_tracing.hpp) names the add-on.
extern "C" __declspec(dllexport) const char* const NAME = "Tales helper_rays test";

namespace {

using Microsoft::WRL::ComPtr;
using Rows = std::array<std::array<float, 4>, 3>;
constexpr UINT CASES = 64;

void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
}

// sun_direction.hlsli's TowardSun for each case's six rows (ScreenToShadow 0-2, ScreenToWorld 0-2) on WARP.
std::vector<std::array<float, 4>> GpuTowardSun(const std::filesystem::path& folder, const std::vector<std::array<std::array<float, 4>, 6>>& cases) {
  const std::string source =
      "#include \"sun_direction.hlsli\"\n"
      "StructuredBuffer<float4> rows : register(t0);\n"
      "RWStructuredBuffer<float4> output : register(u0);\n"
      "[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
      "  const uint at = id.x * 6;\n"
      "  output[id.x] = TowardSun(rows[at], rows[at + 1], rows[at + 2], rows[at + 3].xyz, rows[at + 4].xyz, rows[at + 5].xyz);\n"
      "}\n";
  const std::string name = (folder / "helper_rays_test.hlsl").string();
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  const HRESULT compiled = D3DCompile(source.data(), source.size(), name.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
                                      D3DCOMPILE_IEEE_STRICTNESS, 0, code.GetAddressOf(), errors.GetAddressOf());
  if (errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
  Check(compiled);
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                          context.GetAddressOf()));
  ComPtr<ID3D11ComputeShader> shader;
  Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
  const D3D11_BUFFER_DESC rows_desc = {.ByteWidth = static_cast<UINT>(cases.size() * sizeof(cases[0])), .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = D3D11_BIND_SHADER_RESOURCE, .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                       .StructureByteStride = 16};
  const D3D11_SUBRESOURCE_DATA rows_data = {.pSysMem = cases.data()};
  const D3D11_BUFFER_DESC output_desc = {.ByteWidth = static_cast<UINT>(cases.size() * 16), .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_UNORDERED_ACCESS, .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                         .StructureByteStride = 16};
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = output_desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> rows;
  ComPtr<ID3D11Buffer> output;
  ComPtr<ID3D11Buffer> staging;
  ComPtr<ID3D11ShaderResourceView> rows_view;
  ComPtr<ID3D11UnorderedAccessView> output_view;
  Check(device->CreateBuffer(&rows_desc, &rows_data, rows.GetAddressOf()));
  Check(device->CreateBuffer(&output_desc, nullptr, output.GetAddressOf()));
  Check(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()));
  Check(device->CreateShaderResourceView(rows.Get(), nullptr, rows_view.GetAddressOf()));
  Check(device->CreateUnorderedAccessView(output.Get(), nullptr, output_view.GetAddressOf()));
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, rows_view.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
  context->Dispatch(static_cast<UINT>(cases.size()), 1, 1);
  context->CopyResource(staging.Get(), output.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
  std::vector<std::array<float, 4>> result(cases.size());
  std::memcpy(result.data(), mapped.pData, cases.size() * 16);
  context->Unmap(staging.Get(), 0);
  return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc < 2) throw std::runtime_error("the add-on's source folder as the first argument");
    std::mt19937 random(20261116);
    std::uniform_real_distribution<float> unit(-1.f, 1.f);
    // The sun: ScreenToShadow's xyz columns a rotation and scale of the world's (as a shadow map's), ScreenToWorld a
    // camera's; plus a degenerate case (no sun).
    std::vector<std::array<std::array<float, 4>, 6>> cases(CASES);
    for (auto& rows : cases) {
      for (auto& row : rows) row = {unit(random), unit(random), unit(random), unit(random)};
    }
    cases.back()[3] = cases.back()[4] = cases.back()[5] = {};
    const auto gpu = GpuTowardSun(std::filesystem::path(argv[1]), cases);
    float sun_error = 0.f;
    float control_error = 0.f;
    for (size_t index = 0; index < cases.size(); ++index) {
      const Rows shadow = {cases[index][0], cases[index][1], cases[index][2]};
      const Rows world = {cases[index][3], cases[index][4], cases[index][5]};
      std::array<float, 3> toward = {};
      const bool valid = talesofarise::dlss::ray_tracing::internal::TowardSun(shadow, world, &toward);
      if (valid != (gpu[index][3] == 1.f)) throw std::runtime_error("TowardSun's validity differs at case " + std::to_string(index));
      if (!valid) continue;
      // The control: every other direction negated.
      std::array<float, 3> control = toward;
      const float sign = (index % 2 == 0) ? -1.f : 1.f;
      for (size_t component = 0; component < 3; ++component) {
        sun_error = std::max(sun_error, std::abs(toward[component] - gpu[index][component]));
        control_error = std::max(control_error, std::abs((control[component] * sign) - gpu[index][component]));
      }
    }
    // The camera: TranslatedWorldToClip (row vectors on centimetres + PreViewTranslation) against ViewCamera's rows on metres.
    std::vector<std::array<float, 4>> view(talesofarise::dlss::ray_tracing::internal::VIEW_ROWS);
    for (size_t row = 0; row < 4; ++row) view[row] = {unit(random), unit(random), unit(random), unit(random)};
    view[talesofarise::dlss::ray_tracing::internal::VIEW_PRE_VIEW_TRANSLATION] = {-123456.f, 65432.f, -2345.f, 0.f};
    const auto camera = talesofarise::dlss::ray_tracing::internal::ViewCamera(view);
    float camera_error = 0.f;
    float camera_control = 0.f;
    for (UINT point = 0; point < CASES; ++point) {
      const std::array<double, 3> metres = {1234.56 + (100.0 * unit(random)), -654.32 + (100.0 * unit(random)), 23.45 + (10.0 * unit(random))};
      for (size_t row = 0; row < 4; ++row) {
        double expected = view[3][row];
        double untranslated = view[3][row];
        double ours = camera.view_projection[(row * 4) + 3];
        for (size_t column = 0; column < 3; ++column) {
          expected += ((metres[column] * 100.0) + view[62][column]) * view[column][row];
          untranslated += metres[column] * 100.0 * view[column][row];
          ours += camera.view_projection[(row * 4) + column] * metres[column];
        }
        const double scale = std::max(1.0, std::abs(expected));
        camera_error = std::max(camera_error, static_cast<float>(std::abs(ours - expected) / scale));
        camera_control = std::max(camera_control, static_cast<float>(std::abs(untranslated - expected) / scale));
      }
    }
    const bool position = std::abs(camera.position[0] - 1234.56f) < 0.01f && std::abs(camera.position[1] + 654.32f) < 0.01f;
    std::cout << "TowardSun CPU vs WARP: max error " << sun_error << " (limit 0.0001), control " << control_error
              << "; camera relative error " << camera_error << " (limit 0.0001), control " << camera_control << "; position "
              << camera.position[0] << " " << camera.position[1] << " " << camera.position[2] << "\n";
    if (!(sun_error <= 1e-4f) || !(camera_error <= 1e-4f) || !position) throw std::runtime_error("the helper's inputs differ");
    if (!(control_error > 0.5f) || !(camera_control > 0.01f)) throw std::runtime_error("a control was not caught");
    std::cout << "PASS helper_rays\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL helper_rays: " << error.what() << '\n';
    return 1;
  }
}
