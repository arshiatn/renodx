// GT-VBAO on flat ground (A15): a ground GT-VBAO compute shader (argv[1]; by default R.U.S.E.'s ground_vbao.cs_5_0.hlsl
// over twattila/bitmask.hlsli, fxc /O3) on WARP over a synthetic ground seen from tw_ruse_bridge's trace camera (P00 /
// P11 / A / B from its VIEW_PROJECTION, 256 x 144, the ground's centre 20,000 units away, radius 2,000 and thickness
// 500 as ray_tracing.hpp, noise frame 0 as its test 6 without the helper). Per camera pitch (5 to 90 degrees): a flat
// plane alone (R32F depth, then quantized to 24 bits as the INTZ that test draws into), then test 6's scene (a plateau
// 1,000 units over the middle, half its size) with test 6's "open" (more than 24 pixels from the plateau on screen) and a
// 3D open (no plateau point within the radius, so no sample of it can count). Prints mean / lowest / share under 0.95 of
// the shaded ground; flat ground should read 1. Control: the plateau must darken the ground beside it (else the dispatch
// did nothing and flat ground would read 1 for the wrong reason); thickness 0 (no sample covers a sector) printed beside.
// 2026-11-16, ground_vbao as shipped: flat 0.9638 at 13.6 degrees (25% of pixels under 0.95, lowest 0.80), 0.9965 at 90;
// 24-bit depth the same. The 1-pixel step's rounded texel (int2(sample_pixel)) leaves the slice's line by up to ~26
// degrees on screen; on ground seen at a grazing angle that ground direction reads far above the slice's horizon (its
// front 0.3-0.9 across the hemisphere instead of 1) and, with the thickness, covers sectors: 87% of the first covering
// samples are step 0 on the normal's side. Depth at the exact place on the line (bilinear where the four texels lie on
// one plane) reads 1.0000 at 5-20 and 90 degrees, 0.999 at 30-75, plateau side 0.8431 (0.8365 before).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WIDTH = 256;
constexpr UINT HEIGHT = 144;
constexpr std::array<double, 16> VIEW_PROJECTION = {0.696776032, 1.16561294, 1.01178506e-08, -3488510.,
                                                    0.487397105, -0.291354597, 2.34648299, 56396.8555,
                                                    0.834336698, -0.498746961, -0.235231012, 377480.5,
                                                    0.834253252, -0.498697072, -0.235207483, 382442.75};
constexpr double DISTANCE = 20000;
constexpr float RADIUS = 2000.f;
constexpr float THICKNESS = 500.f;

double RowLength(int row) {
  const double* v = VIEW_PROJECTION.data() + (row * 4);
  return std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
}

struct Scene {
  double pitch_degrees = 13.6;
  bool quantize = false;
  double plateau = 0;  // its height over the ground (0: none; negative: a pit)
};

struct Result {
  size_t shaded = 0;
  double mean = 0;
  double lowest = 1;
  double under = 0;  // share under 0.95
  size_t open24 = 0;
  double open24_mean = 0;
  double open24_lowest = 1;
  size_t open3d = 0;
  double open3d_mean = 0;
  double open3d_lowest = 1;
  size_t beside = 0;
  double beside_mean = 0;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: gtvbao_flat <ground GT-VBAO .hlsl>\n");
    return 2;
  }
  const float p00 = static_cast<float>(RowLength(0));
  const float p11 = static_cast<float>(RowLength(1));
  const double a_double = RowLength(2) / RowLength(3);
  const float a = static_cast<float>(a_double);
  const float b = static_cast<float>(VIEW_PROJECTION[11] - (a_double * VIEW_PROJECTION[15]));
  std::printf("camera: P00 %.6f P11 %.6f A %.8f B %.3f (near %.0f, far %.3g), radius on screen at %.0f: %.1f px\n", p00, p11, a, b, -b / a,
              b / (1. - a), DISTANCE, 0.5 * RADIUS * p11 / DISTANCE * HEIGHT);

  const std::wstring path(argv[1], argv[1] + std::strlen(argv[1]));
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                code.GetAddressOf(), errors.GetAddressOf()))) {
    std::fprintf(stderr, "compile failed: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return 3;
  }
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::fprintf(stderr, "no WARP device\n");
    return 4;
  }
  ComPtr<ID3D11ComputeShader> shader;
  if (FAILED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()))) return 5;

  D3D11_TEXTURE2D_DESC desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                               .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  ComPtr<ID3D11Texture2D> depth_texture;
  ComPtr<ID3D11ShaderResourceView> depth_view;
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  ComPtr<ID3D11Texture2D> output;
  ComPtr<ID3D11UnorderedAccessView> output_view;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()))
      || FAILED(device->CreateUnorderedAccessView(output.Get(), nullptr, output_view.GetAddressOf()))) return 6;
  desc.BindFlags = 0;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) return 7;
  D3D11_BUFFER_DESC buffer_desc = {.ByteWidth = 128, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> constants;
  if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, constants.GetAddressOf()))) return 8;

  const auto run = [&](const Scene& scene, float radius, float thickness) {
    const double pitch = scene.pitch_degrees * 3.14159265358979323846 / 180.;
    const double eye_height = DISTANCE * std::sin(pitch);
    // View space x right, y up, z forward; the world's up there, and the ground's forward axis.
    const std::array<double, 3> up = {0, std::cos(pitch), -std::sin(pitch)};
    const std::array<double, 3> forward = {0, std::sin(pitch), std::cos(pitch)};
    const std::array<double, 3> centre = {0, 0, DISTANCE};
    const double half = 0.25 * (DISTANCE * 0.414 * 2);  // the harness's plateau: half the ground quad's half size
    std::vector<float> depth(static_cast<size_t>(WIDTH) * HEIGHT, 1.f);
    std::vector<uint8_t> on_plateau(depth.size());
    std::vector<std::array<double, 3>> ground_point(depth.size());
    std::vector<uint8_t> is_ground(depth.size());
    const auto dot = [](const std::array<double, 3>& l, const std::array<double, 3>& r) { return (l[0] * r[0]) + (l[1] * r[1]) + (l[2] * r[2]); };
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) {
        const size_t index = (static_cast<size_t>(y) * WIDTH) + x;
        const std::array<double, 3> direction = {((x + 0.5) / WIDTH * 2 - 1) / p00, (1 - ((y + 0.5) / HEIGHT * 2)) / p11, 1};
        const double toward = dot(up, direction);
        if (toward >= 0) continue;
        double z = -eye_height / toward;
        if (scene.plateau != 0) {
          const double top = -(eye_height - scene.plateau) / toward;
          const std::array<double, 3> hit = {direction[0] * top, direction[1] * top, top};
          const std::array<double, 3> offset = {hit[0] - centre[0], hit[1] - centre[1], hit[2] - centre[2]};
          const bool inside = std::abs(offset[0]) < half && std::abs(dot(offset, forward) - (scene.plateau * dot(up, forward))) < half;
          if (inside) {
            z = top;
            on_plateau[index] = 1;
          }
        }
        if (on_plateau[index] == 0) {
          is_ground[index] = 1;
          ground_point[index] = {direction[0] * z, direction[1] * z, z};
        }
        double value = a_double + (b / z);
        if (value >= 1.) continue;
        if (scene.quantize) value = std::round(value * 16777215.) / 16777215.;
        depth[index] = static_cast<float>(value);
      }
    }
    D3D11_TEXTURE2D_DESC depth_desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                       .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = depth.data(), .SysMemPitch = WIDTH * 4};
    depth_texture.Reset();
    depth_view.Reset();
    device->CreateTexture2D(&depth_desc, &data, depth_texture.GetAddressOf());
    device->CreateShaderResourceView(depth_texture.Get(), nullptr, depth_view.GetAddressOf());
    std::array<float, 32> settings = {p00, p11, a, b, WIDTH, HEIGHT, 1.f / WIDTH, 1.f / HEIGHT, radius, thickness, 0.f, 1.f};
    context->UpdateSubresource(constants.Get(), 0, nullptr, settings.data(), 0, 0);
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->CSSetShaderResources(0, 1, depth_view.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
    context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    ID3D11UnorderedAccessView* no_uav = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
    context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    std::vector<float> value(depth.size());
    for (UINT y = 0; y < HEIGHT; ++y) std::memcpy(value.data() + (y * WIDTH), static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch), WIDTH * 4);
    context->Unmap(staging.Get(), 0);

    // RENODX_GTVBAO_DUMP: a file for the raw output of the flat 13.6 degree R32F run (a probe copy's per-pixel values).
    if (const char* dump = std::getenv("RENODX_GTVBAO_DUMP"); dump != nullptr && scene.pitch_degrees == 13.6 && !scene.quantize && scene.plateau == 0
                                                      && thickness == THICKNESS) {
      if (FILE* file = std::fopen(dump, "wb"); file != nullptr) {
        std::fwrite(value.data(), sizeof(float), value.size(), file);
        std::fclose(file);
      }
    }
    Result result;
    size_t under = 0;
    for (int y = 0; y < static_cast<int>(HEIGHT); ++y) {
      for (int x = 0; x < static_cast<int>(WIDTH); ++x) {
        const size_t index = (static_cast<size_t>(y) * WIDTH) + x;
        if (is_ground[index] == 0 || depth[index] >= 1.f) continue;
        const auto& point = ground_point[index];
        const double radius_pixels = 0.5 * radius * p11 / point[2] * HEIGHT;
        if (radius_pixels < 1) continue;  // the shader leaves 1
        const double v = value[index];
        ++result.shaded;
        result.mean += v;
        result.lowest = std::min(result.lowest, v);
        under += (v < 0.95) ? 1 : 0;
        if (scene.plateau == 0) continue;
        int nearest = 1 << 20;
        for (int dy = -24; dy <= 24; ++dy) {
          for (int dx = -24; dx <= 24; ++dx) {
            const int sx = x + dx;
            const int sy = y + dy;
            if (sx < 0 || sy < 0 || sx >= static_cast<int>(WIDTH) || sy >= static_cast<int>(HEIGHT)) continue;
            if (on_plateau[(static_cast<size_t>(sy) * WIDTH) + sx] != 0) nearest = std::min(nearest, std::max(std::abs(dx), std::abs(dy)));
          }
        }
        if (nearest > 24) {
          ++result.open24;
          result.open24_mean += v;
          result.open24_lowest = std::min(result.open24_lowest, v);
        } else if (nearest <= 3) {
          ++result.beside;
          result.beside_mean += v;
        }
        // 3D: the point's distance to the plateau's square (its plane at the plateau's height).
        const std::array<double, 3> offset = {point[0] - centre[0], point[1] - centre[1], point[2] - centre[2]};
        const double across = std::max(0., std::abs(offset[0]) - half);
        const double along = std::max(0., std::abs(dot(offset, forward)) - half);
        const double distance = std::sqrt((across * across) + (along * along) + (scene.plateau * scene.plateau));
        if (distance > radius) {
          ++result.open3d;
          result.open3d_mean += v;
          result.open3d_lowest = std::min(result.open3d_lowest, v);
        }
      }
    }
    result.mean /= std::max<size_t>(result.shaded, 1);
    result.under = static_cast<double>(under) / std::max<size_t>(result.shaded, 1);
    result.open24_mean /= std::max<size_t>(result.open24, 1);
    result.open3d_mean /= std::max<size_t>(result.open3d, 1);
    result.beside_mean /= std::max<size_t>(result.beside, 1);
    return result;
  };

  int failures = 0;
  std::printf("%-6s %-5s %-8s %7s %8s %8s %7s | %-28s | %-28s | %s\n", "pitch", "depth", "plateau", "shaded", "mean", "lowest", "<0.95", "open 24 px (n mean lowest)",
              "open 3D (n mean lowest)", "beside (n mean)");
  for (const double pitch : {5., 8., 13.6, 20., 30., 45., 60., 75., 90.}) {
    for (const bool quantize : {false, true}) {
      for (const double plateau : {0., 1000.}) {
        const Result r = run({.pitch_degrees = pitch, .quantize = quantize, .plateau = plateau}, RADIUS, THICKNESS);
        std::printf("%-6.1f %-5s %-8.0f %7zu %8.4f %8.4f %7.4f | %6zu %8.4f %8.4f    | %6zu %8.4f %8.4f    | %zu %.4f\n", pitch, quantize ? "d24" : "f32", plateau,
                    r.shaded, r.mean, r.lowest, r.under, r.open24, r.open24_mean, r.open24_lowest, r.open3d, r.open3d_mean, r.open3d_lowest, r.beside,
                    r.beside_mean);
      }
    }
  }
  // Control: the plateau must darken the ground beside it (else the dispatch did nothing); thickness 0 beside.
  const Result thin = run({.pitch_degrees = 13.6}, RADIUS, 0.f);
  const Result beside = run({.pitch_degrees = 13.6, .plateau = 1000.}, RADIUS, THICKNESS);
  std::printf("thickness 0, flat 13.6: mean %.4f lowest %.4f; control: plateau beside mean %.4f, open 3D %.4f\n", thin.mean, thin.lowest,
              beside.beside_mean, beside.open3d_mean);
  if (!(beside.beside > 100 && beside.beside_mean < beside.open3d_mean - 0.03)) {
    std::printf("CONTROL FAILED: the plateau doesn't darken its side\n");
    ++failures;
  }
  return failures;
}
