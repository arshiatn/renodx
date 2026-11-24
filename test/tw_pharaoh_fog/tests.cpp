// WARP: Pharaoh's battle fog below 100% (render scale 33%: "the picture in the top-left of a 3x texture against
// 100%"), the game's bytes against the add-on's replacements (built from src/games/twpharaohdynasties/shaders):
// (G) the fog's shadow ranges 99164788 (shader model 5) and 0CB1C821 (its gather 4.1 build): at 100% (cb12 unbound) ours
//     is the game's; its 5x5 taps spread (range 100): ours at 33% against 100% (the fix), the game's at 33% apart (CONTROL).
// (H) the fog's sun scan DB53BDDC: at 100% ours is the game's (u0.xyz bit for bit, the fade within 1e-6); at 33% ours
//     keeps the visibility, the sun's texture uv (x 3) and now the screen-edge fade of 100%; the game's fade at 33% apart
//     (CONTROL: the sun at the view's uv 0.3, 0.3).
// Derived from tools/tw/warp_tests/pharaoh/scale_v58 (G) / (H).
// tests <folder with ours: 0x99164788.cso 0x0CB1C821.cso 0xDB53BDDC.cso> (run.ps1: build/twpharaohdynasties.include/embed)
#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using Texel = std::array<float, 4>;

static constexpr UINT W = 64, H = 48, S = 3;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static const std::string PACK = "D:/Dev/renodx/bin/cmd_Decompiler-1.3.16/dump_shader_twpharaohdynasties_pack/cso/";
static int failures = 0;

static std::vector<uint8_t> Load(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

static ComPtr<ID3D11Buffer> Constants(const std::vector<float>& data) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>((data.size() * 4 + 15) / 16 * 16), .Usage = D3D11_USAGE_DEFAULT,
                                  .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> padded(desc.ByteWidth, 0);
  std::memcpy(padded.data(), data.data(), data.size() * 4);
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &init, &buffer);
  return buffer;
}

// `picture` in the top-left of a texture `scale` times its size, the rest its edge texels (as a clamp at 100% reads).
static ComPtr<ID3D11ShaderResourceView> Screen(const std::vector<Texel>& picture, UINT scale) {
  const UINT full_width = W * scale, full_height = H * scale;
  std::vector<Texel> texels(full_width * full_height);
  for (UINT y = 0; y < full_height; ++y) {
    for (UINT x = 0; x < full_width; ++x) texels[y * full_width + x] = picture[std::min(y, H - 1) * W + std::min(x, W - 1)];
  }
  const D3D11_TEXTURE2D_DESC desc = {.Width = full_width, .Height = full_height, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = texels.data(), .SysMemPitch = full_width * 16};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, &init, &texture);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view;
}

struct FullScreen {
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11Buffer> quad;
};
static FullScreen full_screen;

// `shader` with the game's full-screen VS 31C49ED2 into a (W x scale) x (H x scale) target, its viewport W x H at 0,0:
// the top-left W x H.
static std::vector<float> Draw(ID3D11PixelShader* shader, UINT scale, const std::function<void()>& bind) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = W * scale, .Height = H * scale, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> texture, staging;
  device->CreateTexture2D(&desc, nullptr, &texture);
  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  device->CreateTexture2D(&staging_desc, nullptr, &staging);
  ComPtr<ID3D11RenderTargetView> rtv;
  device->CreateRenderTargetView(texture.Get(), nullptr, &rtv);
  const float clear[4] = {-7.f, -7.f, -7.f, -7.f};
  context->ClearState();
  context->ClearRenderTargetView(rtv.Get(), clear);
  context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(W), .Height = static_cast<float>(H), .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  const UINT stride = 12, offset = 0;
  context->IASetInputLayout(full_screen.layout.Get());
  context->IASetVertexBuffers(0, 1, full_screen.quad.GetAddressOf(), &stride, &offset);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  context->VSSetShader(full_screen.vertex.Get(), nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  bind();
  context->Draw(4, 0);
  context->CopyResource(staging.Get(), texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<float> out(W * H * 4);
  for (UINT y = 0; y < H; ++y) std::memcpy(out.data() + y * W * 4, static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, W * 16);
  context->Unmap(staging.Get(), 0);
  context->ClearState();
  return out;
}

// Values apart beyond `tolerance` (relative to max(1, |a|)) and bit for bit; `equal`: none may be, else some must.
static void Expect(const char* what, const std::vector<float>& a, const std::vector<float>& b, double tolerance, bool equal) {
  uint64_t apart = 0, bits = 0;
  double largest = 0.0;
  for (size_t at = 0; at < a.size(); ++at) {
    if (std::memcmp(&a[at], &b[at], 4) != 0) ++bits;
    const double difference = std::fabs(static_cast<double>(a[at]) - b[at]);
    if (!(difference <= tolerance * std::max(1.0, std::fabs(static_cast<double>(a[at]))))) ++apart;
    if (std::isfinite(difference)) largest = std::max(largest, difference);
  }
  const bool ok = equal ? (apart == 0) : (apart != 0);
  std::printf("  %s: %zu values, %llu apart (%llu not bit-equal, largest %.6g): %s\n", what, a.size(), static_cast<unsigned long long>(apart),
              static_cast<unsigned long long>(bits), largest, ok ? (equal ? "ok" : "apart as it must") : (equal ? "FAIL" : "A CONTROL PASSED"));
  failures += ok ? 0 : 1;
}

// Pharaoh's camera layout: g_screen_size c27 and g_render_target_dimensions c31 the texture's, g_viewport_dimensions c29
// the viewport's; one sample; `rows` (register, values); the rest random.
static std::vector<float> Camera(UINT scale, const std::vector<std::pair<uint32_t, Texel>>& rows) {
  std::mt19937 rng(53);
  std::vector<float> camera(64 * 4);
  for (float& value : camera) value = std::uniform_real_distribution<float>(-1.f, 1.f)(rng);
  const float width = static_cast<float>(W * scale), height = static_cast<float>(H * scale);
  const Texel screen = {width, height, 1.f / width, 1.f / height};
  const Texel view = {static_cast<float>(W), static_cast<float>(H), 1.f / W, 1.f / H};
  std::copy(screen.begin(), screen.end(), camera.begin() + 27 * 4);
  std::copy(screen.begin(), screen.end(), camera.begin() + 31 * 4);
  std::copy(view.begin(), view.end(), camera.begin() + 29 * 4);
  camera[28 * 4] = 0.f;
  camera[30 * 4] = camera[30 * 4 + 1] = 0.f;
  const int samples = 0;
  std::memcpy(&camera[36 * 4 + 3], &samples, 4);
  for (const auto& [reg, values] : rows) std::copy(values.begin(), values.end(), camera.begin() + reg * 4);
  return camera;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const std::string ours_folder = std::string(argv[1]) + "/";
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
  ComPtr<ID3D11SamplerState> point_clamp;
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxLOD = D3D11_FLOAT32_MAX};
  device->CreateSamplerState(&sampler_desc, &point_clamp);
  const float third = 1.f / 3.f;
  const auto share_3d = Constants({third, third, third, third});  // cb12 in the 3D: the viewport a third of the target

  const auto vs_code = Load(PACK + "0x31C49ED2.vs_5_0.cso");
  device->CreateVertexShader(vs_code.data(), vs_code.size(), nullptr, &full_screen.vertex);
  const D3D11_INPUT_ELEMENT_DESC element = {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
  device->CreateInputLayout(&element, 1, vs_code.data(), vs_code.size(), &full_screen.layout);
  const std::array<float, 12> quad = {-1.f, 1.f, 0.f, 1.f, 1.f, 0.f, -1.f, -1.f, 0.f, 1.f, -1.f, 0.f};
  const D3D11_BUFFER_DESC quad_desc = {.ByteWidth = sizeof(quad), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA quad_data = {.pSysMem = quad.data()};
  device->CreateBuffer(&quad_desc, &quad_data, &full_screen.quad);
  if (!full_screen.vertex || !full_screen.layout) {
    std::printf("VS 31C49ED2 missing\n");
    return 1;
  }

  // (G) A perspective camera, the sun ahead; t2 .x near / distance (near 0.5: distances 0.1-0.9), .y the range along the
  // sun (0: the taps within the pixel's texel; 100: ~5 texels apart at 100%).
  for (const char* hash : {"0x99164788.ps_5_0", "0x0CB1C821.ps_4_1"}) {
    std::printf("(G) the fog's shadow ranges %s\n", hash);
    const auto pixel = [](const std::vector<uint8_t>& code) {
      ComPtr<ID3D11PixelShader> shader;
      if (code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader))) return ComPtr<ID3D11PixelShader>();
      return shader;
    };
    const auto game = pixel(Load(PACK + hash + ".cso"));
    const auto ours = pixel(Load(ours_folder + std::string(hash).substr(0, 10) + ".cso"));
    if (!game || !ours) {
      std::printf("  missing a shader\n");
      ++failures;
      continue;
    }
    const Texel x_row = {1.2f, 0.f, 0.f, 0.f}, y_row = {0.f, 1.6f, 0.f, 0.f}, z_row = {0.f, 0.f, 1.0001f, -0.1f}, w_row = {0.f, 0.f, 1.f, 0.f};
    const Texel inv_x = {1.f / 1.2f, 0.f, 0.f, 0.f}, inv_y = {0.f, 1.f / 1.6f, 0.f, 0.f}, inv_z = {0.f, 0.f, 0.f, 1.f},
                inv_w = {0.f, 0.f, -10.f, 10.001f};
    const std::vector<std::pair<uint32_t, Texel>> rows = {
        {1, {1.f, 0.f, 0.f, 0.f}}, {2, {0.f, 1.f, 0.f, 0.f}}, {3, {0.f, 0.f, 1.f, 0.f}}, {4, {0.f, 0.f, 0.f, 1.f}},
        {5, x_row}, {6, y_row}, {7, z_row}, {8, w_row}, {17, inv_x}, {18, inv_y}, {19, inv_z}, {20, inv_w},
        {25, {0.5f, 2000.f, 0.f, 0.f}}};
    std::vector<float> lighting(3 * 4, 0.f);
    lighting[2 * 4] = 0.2f;
    lighting[2 * 4 + 1] = -0.3f;
    lighting[2 * 4 + 2] = -0.93f;
    const auto lighting_buffer = Constants(lighting);
    std::vector<float> fog(11 * 4, 0.f);
    fog[10 * 4 + 3] = 1.f;  // g_fog_resolution
    const auto fog_buffer = Constants(fog);
    std::vector<Texel> distance(W * H);
    const auto fill = [&](float along) {
      for (UINT y = 0; y < H; ++y) {
        for (UINT x = 0; x < W; ++x) {
          const float value = 0.3f + 0.004f * static_cast<float>(y) + 0.1f * std::sin(0.3f * static_cast<float>(x)) + ((((x / 5) + (y / 4)) % 3 == 0) ? -0.15f : 0.f);
          distance[y * W + x] = {value, along, 0.f, 0.f};
        }
      }
    };
    const auto run = [&](ID3D11PixelShader* shader, UINT scale, bool share) {
      const auto camera = Constants(Camera(scale, rows));
      const auto t1 = Screen(distance, scale), t2 = Screen(distance, scale);
      return Draw(shader, scale, [&] {
        ID3D11ShaderResourceView* views[] = {nullptr, t1.Get(), t2.Get()};
        context->PSSetShaderResources(0, 3, views);
        ID3D11Buffer* buffers[] = {camera.Get(), lighting_buffer.Get(), fog_buffer.Get()};
        context->PSSetConstantBuffers(0, 3, buffers);
        if (share) context->PSSetConstantBuffers(12, 1, share_3d.GetAddressOf());
      });
    };
    for (const float along : {0.f, 100.f}) {
      fill(along);
      std::printf(" range %g\n", along);
      const auto game100 = run(game.Get(), 1, false);
      Expect("100%, ours (cb12 unbound) against the game's bytes", game100, run(ours.Get(), 1, false), 0.0, true);
      Expect("33%, ours against the game's at 100%", game100, run(ours.Get(), S, true), 1e-5, true);
      if (along > 0.f) Expect("CONTROL 33%, the game's bytes against 100% (taps a third as far)", game100, run(game.Get(), S, true), 1e-5, false);
    }
  }

  // (H) 32 x 32 rays around the sun, each visible where the depth at its projected uv x (c29.xy x c31.zw) x the depth's
  // size is the sky; u0 = (the visible rays' texture uv, the visibility, the screen-edge fade). The view along +z, the
  // depth the sky (1) or not (0.5) in 3 x 3 blocks; the sun just off the view's middle, and at its uv 0.3, 0.3 (100%: no
  // fade; the game's 33% reads 0.1, 0.1 there: faded).
  {
    std::printf("(H) the fog's sun scan DB53BDDC\n");
    const auto compute = [](const std::vector<uint8_t>& bytes) {
      ComPtr<ID3D11ComputeShader> shader;
      if (bytes.empty() || FAILED(device->CreateComputeShader(bytes.data(), bytes.size(), nullptr, &shader))) return ComPtr<ID3D11ComputeShader>();
      return shader;
    };
    const auto game = compute(Load(PACK + "0xDB53BDDC.cs_5_0.cso"));
    const auto ours = compute(Load(ours_folder + "0xDB53BDDC.cso"));
    if (!game || !ours) {
      std::printf("  missing a shader\n");
      return 1;
    }
    const std::vector<std::pair<uint32_t, Texel>> rows = {
        {1, {1.f, 0.f, 0.f, 0.f}}, {2, {0.f, 1.f, 0.f, 0.f}}, {3, {0.f, 0.f, 1.f, 0.f}}, {4, {0.f, 0.f, 0.f, 0.f}}, {7, {0.f, 0.f, 1.f, 0.f}},
        {9, {1.f, 0.f, 0.f, 0.f}}, {10, {0.f, 1.f, 0.f, 0.f}}, {11, {0.f, 0.f, 1.f, 0.f}}, {12, {0.f, 0.f, 1.f, 0.f}}};
    const auto environment = Constants({0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
    std::vector<float> fog(12 * 4, 0.f);
    fog[10 * 4 + 2] = 3.f;  // the disk: 3000 over 10000
    fog[11 * 4] = 1.f;
    const auto fog_buffer = Constants(fog);
    std::vector<Texel> depth(W * H), sky_picture(W * H);
    std::mt19937 rng(7);
    std::vector<float> blocks((W / 3 + 1) * (H / 3 + 1));
    for (float& value : blocks) value = (rng() & 1) ? 1.f : 0.5f;
    for (UINT y = 0; y < H; ++y) {
      for (UINT x = 0; x < W; ++x) {
        const float value = blocks[(y / 3) * (W / 3 + 1) + x / 3];
        depth[y * W + x] = {value, 0.f, value, value};
        for (float& channel : sky_picture[y * W + x]) channel = std::uniform_real_distribution<float>(0.f, 1.f)(rng);
      }
    }
    const auto sky = Screen(sky_picture, 1);
    const std::array<float, 4> zero = {};
    const auto run = [&](ID3D11ComputeShader* shader, UINT scale, const Texel& sun) {
      const auto camera = Constants(Camera(scale, rows));
      std::vector<float> lighting(3 * 4, 0.f);
      std::copy(sun.begin(), sun.begin() + 3, lighting.begin() + 2 * 4);
      const auto lighting_buffer = Constants(lighting);
      const auto t1 = Screen(depth, scale);
      ComPtr<ID3D11Buffer> previous, out, staging;
      const D3D11_SUBRESOURCE_DATA init = {.pSysMem = zero.data()};
      const D3D11_BUFFER_DESC srv_buffer = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      const D3D11_BUFFER_DESC uav_buffer = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
      device->CreateBuffer(&srv_buffer, &init, &previous);
      device->CreateBuffer(&uav_buffer, &init, &out);
      const D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
                                                        .Buffer = {.FirstElement = 0, .NumElements = 1}};
      ComPtr<ID3D11ShaderResourceView> t3;
      device->CreateShaderResourceView(previous.Get(), &srv_desc, &t3);
      const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                         .Buffer = {.FirstElement = 0, .NumElements = 1}};
      ComPtr<ID3D11UnorderedAccessView> u0;
      device->CreateUnorderedAccessView(out.Get(), &uav_desc, &u0);
      context->ClearState();
      context->CSSetShader(shader, nullptr, 0);
      ID3D11ShaderResourceView* views[] = {nullptr, t1.Get(), sky.Get(), t3.Get()};
      context->CSSetShaderResources(0, 4, views);
      context->CSSetSamplers(0, 1, point_clamp.GetAddressOf());
      ID3D11Buffer* buffers[] = {camera.Get(), environment.Get(), lighting_buffer.Get(), fog_buffer.Get()};
      context->CSSetConstantBuffers(0, 4, buffers);
      context->CSSetUnorderedAccessViews(0, 1, u0.GetAddressOf(), nullptr);
      context->Dispatch(1, 1, 1);
      const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
      device->CreateBuffer(&staging_desc, nullptr, &staging);
      context->CopyResource(staging.Get(), out.Get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
      std::vector<float> result(4);
      std::memcpy(result.data(), mapped.pData, 16);
      context->Unmap(staging.Get(), 0);
      context->ClearState();
      return result;
    };
    const auto normalized = [](float x, float y, float z) {
      const float length = std::sqrt(x * x + y * y + z * z);
      return Texel{x / length, y / length, z / length, 0.f};
    };
    for (const auto& [name, sun] : {std::pair{"the sun just off the middle", normalized(0.1f, 0.05f, -1.f)},
                                    std::pair{"the sun at the view's uv 0.3, 0.3", normalized(0.4f, -0.4f, -1.f)}}) {
      std::printf(" %s\n", name);
      const auto game100 = run(game.Get(), 1, sun);
      const auto ours100 = run(ours.Get(), 1, sun);
      const auto game33 = run(game.Get(), S, sun);
      const auto ours33 = run(ours.Get(), S, sun);
      std::printf("  u0 100%%: game %g %g %g %g, ours %g %g %g %g; 33%%: game %g %g %g %g, ours %g %g %g %g\n", game100[0], game100[1],
                  game100[2], game100[3], ours100[0], ours100[1], ours100[2], ours100[3], game33[0], game33[1], game33[2], game33[3],
                  ours33[0], ours33[1], ours33[2], ours33[3]);
      Expect("100%, ours against the game's bytes: the sun's uv and visibility (u0.xyz)", {game100[0], game100[1], game100[2]},
             {ours100[0], ours100[1], ours100[2]}, 0.0, true);
      Expect("100%, ours against the game's bytes: the edge fade (u0.w)", {game100[3]}, {ours100[3]}, 1e-6, true);
      Expect("33%, ours: the visibility (u0.z) against 100%", {game100[2]}, {ours33[2]}, 1e-5, true);
      Expect("33%, ours: the sun's texture uv x 3 (u0.xy) against 100%", {game100[0], game100[1]}, {ours33[0] * 3.f, ours33[1] * 3.f}, 1e-4, true);
      Expect("33%, ours: the edge fade (u0.w) against 100%", {game100[3]}, {ours33[3]}, 1e-5, true);
      if (sun[0] > 0.2f) Expect("CONTROL 33%, the game's edge fade (from the texture uv) against 100%", {game100[3]}, {game33[3]}, 1e-5, false);
    }
  }

  std::printf(failures == 0 ? "PASS\n" : "FAIL (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
