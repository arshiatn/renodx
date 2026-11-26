// The screen-space reflections' hits (twattila/ssr_march.hlsli over ffx_sssr/ffx_sssr.h, X26) on WARP. A camera 1.5 m
// above a ground plane, a wall 40 m away and a box, ray-traced on the CPU into the depth (its min pyramid, t10) and the
// normals; every ground pixel's mirror ray marched at a step cap of 8 (and 64, the game's default, for the numbers only).
// tests.exe <repo root>
// - AMD's valid_hit (FFX_SSSR_CONTROL_CAP_ENDED_HITS, the CONTROL): true for every ray; some rays that ran out of steps
//   get a confidence > 0 (a mid-air point taken for a hit). The control must show them.
// - Today's: no ray that ran out of steps has a confidence > 0; every other ray's confidence the control's to the bit.
// - A ray marked as one that found its hit ends where the same march with 512 steps ends.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WIDTH = 512;
constexpr UINT HEIGHT = 256;
constexpr UINT LEVELS = 10;  // 512 x 256 down to 1 x 1, as ssr_hiz.hpp makes them
constexpr float TAN_HALF_Y = 0.57735027f;  // 60 degrees high
constexpr float TAN_HALF_X = TAN_HALF_Y * static_cast<float>(WIDTH) / static_cast<float>(HEIGHT);
constexpr float NEAR_PLANE = 0.25f;
constexpr float FAR_PLANE = 2000.f;
constexpr float DEPTH_A = FAR_PLANE / (FAR_PLANE - NEAR_PLANE);
constexpr float DEPTH_B = -NEAR_PLANE * FAR_PLANE / (FAR_PLANE - NEAR_PLANE);
constexpr float GROUND_Y = -1.5f;

int failures = 0;

void Check(bool condition, const char* what) {
  std::printf("   %s: %s\n", condition ? "ok" : "FAIL", what);
  failures += condition ? 0 : 1;
}

struct Constants {
  float projection[4];
  UINT width;
  UINT height;
  UINT cap;
  UINT long_cap;
};

struct Scene {
  std::vector<std::vector<float>> levels;  // the depth's min pyramid
  std::vector<std::array<float, 4>> normals;
};

// Each pixel's nearest of the ground, the wall (z 40, x -30..30, up to y 6) and a box (x 2..5, z 12..15, up to y 0.5).
Scene MakeScene() {
  Scene scene;
  std::vector<float> depth(WIDTH * HEIGHT, 1.f);
  scene.normals.assign(WIDTH * HEIGHT, {0.f, 0.f, 0.f, 0.f});
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const float dx = ((((static_cast<float>(x) + 0.5f) / WIDTH) * 2.f) - 1.f) * TAN_HALF_X;
      const float dy = (1.f - (((static_cast<float>(y) + 0.5f) / HEIGHT) * 2.f)) * TAN_HALF_Y;
      float nearest = 1e30f;
      std::array<float, 4> normal = {};
      if (dy < 0.f) {
        nearest = GROUND_Y / dy;
        normal = {0.f, 1.f, 0.f, 0.f};
      }
      if (40.f < nearest && std::abs(40.f * dx) <= 30.f && 40.f * dy <= 6.f && 40.f * dy >= GROUND_Y) {
        nearest = 40.f;
        normal = {0.f, 0.f, -1.f, 0.f};
      }
      // The box: slabs along the ray (1 in z per unit of t).
      const std::array<float, 3> direction = {dx, dy, 1.f};
      const std::array<float, 3> low = {2.f, GROUND_Y, 12.f};
      const std::array<float, 3> high = {5.f, 0.5f, 15.f};
      float enter = 0.f;
      float leave = 1e30f;
      int axis = -1;
      for (int a = 0; a < 3; ++a) {
        if (direction[a] == 0.f) continue;
        const float t0 = low[a] / direction[a];
        const float t1 = high[a] / direction[a];
        const float t_near = std::min(t0, t1);
        if (t_near > enter) {
          enter = t_near;
          axis = a;
        }
        leave = std::min(leave, std::max(t0, t1));
      }
      if (axis >= 0 && enter <= leave && enter < nearest) {
        nearest = enter;
        normal = {0.f, 0.f, 0.f, 0.f};
        normal[axis] = (direction[axis] > 0.f) ? -1.f : 1.f;
      }
      if (nearest < 1e29f) {
        depth[(y * WIDTH) + x] = DEPTH_A + (DEPTH_B / nearest);
        scene.normals[(y * WIDTH) + x] = normal;
      }
    }
  }
  scene.levels.push_back(depth);
  for (UINT level = 1; level < LEVELS; ++level) {
    const UINT width = std::max(WIDTH >> level, 1u);
    const UINT height = std::max(HEIGHT >> level, 1u);
    const UINT above_width = std::max(WIDTH >> (level - 1), 1u);
    const UINT above_height = std::max(HEIGHT >> (level - 1), 1u);
    const auto& above = scene.levels.back();
    std::vector<float> next(width * height, 1.f);
    for (UINT y = 0; y < height; ++y) {
      for (UINT x = 0; x < width; ++x) {
        float least = 1.f;
        for (UINT sy = 0; sy < 2; ++sy) {
          for (UINT sx = 0; sx < 2; ++sx) {
            least = std::min(least, above[(std::min((y * 2) + sy, above_height - 1) * above_width) + std::min((x * 2) + sx, above_width - 1)]);
          }
        }
        next[(y * width) + x] = least;
      }
    }
    scene.levels.push_back(next);
  }
  return scene;
}

// Includes relative to the file that includes them (D3D_COMPILE_STANDARD_FILE_INCLUDE takes nested ones from the first
// file's folder).
class RelativeInclude final : public ID3DInclude {
 public:
  explicit RelativeInclude(std::filesystem::path root) : root_(std::move(root)) {}

  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) noexcept override {
    const auto folder = folders_.find(parent);
    const std::filesystem::path path = ((folder != folders_.end()) ? folder->second : root_) / name;
    std::ifstream file(path, std::ios::binary);
    if (!file) return E_FAIL;
    auto* text = new std::string(std::istreambuf_iterator<char>(file), {});
    folders_[text->data()] = path.parent_path();
    *data = text->data();
    *bytes = static_cast<UINT>(text->size());
    texts_[text->data()] = text;
    return S_OK;
  }

  HRESULT __stdcall Close(LPCVOID data) noexcept override {
    delete texts_[data];
    texts_.erase(data);
    return S_OK;
  }

 private:
  std::filesystem::path root_;
  std::map<LPCVOID, std::filesystem::path> folders_;
  std::map<LPCVOID, std::string*> texts_;
};

ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const std::wstring& path, bool control) {
  const std::array<D3D_SHADER_MACRO, 2> macros = {{{"FFX_SSSR_CONTROL_CAP_ENDED_HITS", "1"}, {nullptr, nullptr}}};
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  const std::string source(std::istreambuf_iterator<char>(file), {});
  RelativeInclude include(std::filesystem::path(path).parent_path());
  const HRESULT result = D3DCompile(source.data(), source.size(), "sssr_hits.cs_5_0.hlsl", control ? macros.data() : nullptr, &include, "main",
                                    "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf());
  if (FAILED(result)) {
    std::printf("FAIL: compile: %s\n", (errors != nullptr) ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
    return nullptr;
  }
  ComPtr<ID3D11ComputeShader> shader;
  if (FAILED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()))) return nullptr;
  return shader;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: tests.exe <repo root>\n");
    return 2;
  }
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }
  const std::string root = argv[1];
  const std::wstring path = std::wstring(root.begin(), root.end()) + L"\\test\\tw_sssr_hits\\sssr_hits.cs_5_0.hlsl";
  const auto today = Compile(device.Get(), path, false);
  const auto control = Compile(device.Get(), path, true);
  if (today == nullptr || control == nullptr) return 1;

  const Scene scene = MakeScene();
  std::array<D3D11_SUBRESOURCE_DATA, LEVELS> level_data = {};
  for (UINT index = 0; index < LEVELS; ++index) {
    level_data[index] = {.pSysMem = scene.levels[index].data(), .SysMemPitch = std::max(WIDTH >> index, 1u) * 4};
  }
  const D3D11_TEXTURE2D_DESC depth_desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = LEVELS, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                           .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11ShaderResourceView> depth_view;
  const D3D11_SUBRESOURCE_DATA normal_data = {.pSysMem = scene.normals.data(), .SysMemPitch = WIDTH * 16};
  D3D11_TEXTURE2D_DESC normal_desc = depth_desc;
  normal_desc.MipLevels = 1;
  normal_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  ComPtr<ID3D11Texture2D> normals;
  ComPtr<ID3D11ShaderResourceView> normal_view;
  const D3D11_BUFFER_DESC out_desc = {.ByteWidth = WIDTH * HEIGHT * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 16};
  ComPtr<ID3D11Buffer> out;
  ComPtr<ID3D11UnorderedAccessView> out_view;
  D3D11_BUFFER_DESC staging_desc = {.ByteWidth = out_desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = sizeof(Constants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> constants;
  if (FAILED(device->CreateTexture2D(&depth_desc, level_data.data(), depth.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(depth.Get(), nullptr, depth_view.GetAddressOf()))
      || FAILED(device->CreateTexture2D(&normal_desc, &normal_data, normals.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(normals.Get(), nullptr, normal_view.GetAddressOf()))
      || FAILED(device->CreateBuffer(&out_desc, nullptr, out.GetAddressOf()))
      || FAILED(device->CreateUnorderedAccessView(out.Get(), nullptr, out_view.GetAddressOf()))
      || FAILED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()))
      || FAILED(device->CreateBuffer(&constants_desc, nullptr, constants.GetAddressOf()))) {
    std::printf("FAIL: resources\n");
    return 1;
  }

  const auto run = [&](ID3D11ComputeShader* shader, UINT cap) {
    const Constants values = {.projection = {TAN_HALF_X, TAN_HALF_Y, DEPTH_A, DEPTH_B}, .width = WIDTH, .height = HEIGHT, .cap = cap, .long_cap = 512};
    context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);
    context->CSSetShader(shader, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->CSSetShaderResources(0, 1, normal_view.GetAddressOf());
    context->CSSetShaderResources(10, 1, depth_view.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, out_view.GetAddressOf(), nullptr);
    context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    context->CopyResource(staging.Get(), out.Get());
    std::vector<std::array<float, 4>> result(WIDTH * HEIGHT);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      std::memcpy(result.data(), mapped.pData, result.size() * 16);
      context->Unmap(staging.Get(), 0);
    }
    return result;
  };

  for (const UINT cap : {8u, 64u}) {
    std::printf("step cap %u\n", cap);
    const auto fixed = run(today.Get(), cap);
    const auto amd = run(control.Get(), cap);
    int traced = 0;
    int ran_out = 0;
    int ran_out_moved = 0;
    int amd_valid = 0;
    int amd_taken = 0;
    int fixed_taken = 0;
    int hits_moved = 0;
    int others_apart = 0;
    for (size_t pixel = 0; pixel < fixed.size(); ++pixel) {
      if (fixed[pixel][0] < 0.f) continue;
      ++traced;
      amd_valid += (amd[pixel][3] != 0.f) ? 1 : 0;
      if (fixed[pixel][0] != 0.f) {
        ++ran_out;
        ran_out_moved += (fixed[pixel][2] == 0.f) ? 1 : 0;
        amd_taken += (amd[pixel][1] > 0.f) ? 1 : 0;
        fixed_taken += (fixed[pixel][1] > 0.f) ? 1 : 0;
      } else {
        hits_moved += (fixed[pixel][2] == 0.f) ? 1 : 0;
        others_apart += (std::memcmp(&fixed[pixel][1], &amd[pixel][1], sizeof(float)) != 0) ? 1 : 0;
      }
    }
    std::printf("   %d rays traced; %d (%.1f%%) ran out of steps (%d of them end elsewhere at 512 steps); AMD's valid_hit true for %d\n", traced,
                ran_out, 100.0 * ran_out / std::max(traced, 1), ran_out_moved, amd_valid);
    std::printf("   rays that ran out of steps with confidence > 0: CONTROL (AMD's) %d (%.2f%% of the rays), today's %d\n", amd_taken,
                100.0 * amd_taken / std::max(traced, 1), fixed_taken);
    std::printf("   the other rays: %d end elsewhere at 512 steps, %d confidences apart from the control's\n", hits_moved, others_apart);
    if (cap != 8u) continue;
    Check(traced > 10000, "the ground's rays traced");
    Check(amd_valid == traced, "AMD's valid_hit true for every ray (its check is dead)");
    Check(ran_out > 100, "rays that ran out of steps at cap 8");
    Check(amd_taken > 0, "CONTROL: AMD's takes some of them (must, else the scene shows nothing)");
    Check(fixed_taken == 0, "today's takes none of them");
    Check(hits_moved == 0, "a ray that found its hit ends where the 512-step march ends");
    Check(others_apart == 0, "every other ray's confidence the control's to the bit");
  }
  std::printf("%s\n", (failures == 0) ? "ALL OK" : "FAILED");
  return (failures == 0) ? 0 : 1;
}
