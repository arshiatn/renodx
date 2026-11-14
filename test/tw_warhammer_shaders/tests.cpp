// Total War: WARHAMMER's sun light and screen-space reflection shaders (src/games/twwarhammer) on WARP.
// tests <repo root> <the game's shader dump folder (cso: 0x230F2241.ps_4_1.cso)>
// 1. The game's own sun light shader 230F2241 (directionallight slm highdef; its bytes, read only) at one G-buffer pixel:
//    normal +x, sun from +x, lit (no cascade, no cloud), no material flags, no specular colour, AO 1. Its diffuse light per
//    unit albedo and sun colour (two albedos' difference) with g_hdr_on 1 and 0, its ambient per unit albedo and cube +x;
//    WH1's rt_sun (its wrapper) on the same lighting buffer: texel 2 the sun colour x that diffuse factor (g_hdr_on 1),
//    texel 4's w the ambient's, texel 0 the way to the sun, texels 3-8 the cube. CONTROL: the shared file's defaults
//    (Attila's switch on: float 44 is g_hdr_on here) give 0.004 and 1.44, not the game's.
// 2. The SSR resolve replacement (shaders/ssr00_0x6B80E35B.cs_5_0.hlsl) at 100% on 64 x 64 targets and at 50% on 128 x 128
//    targets whose top-left 64 x 64 hold the same texels (cb12's frame share 0.5): the same output there with the march's
//    hit at the pixel (the position from the view's uv), and the march's offset (a hit 2.1 texels away at 100%) the same
//    direction (the projected uv in the targets' share; its length in uv as the game's patched bytes keep it).
//    CONTROLS: the source without the position's share, without the projected uv's share, and cb12 unbound at 50% differ.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static int failures = 0;

#define CHECK(x, ...)                                         \
  do {                                                        \
    if (!(x)) {                                               \
      std::printf("FAIL line %d: %s: ", __LINE__, #x);        \
      std::printf(__VA_ARGS__);                               \
      std::printf("\n");                                      \
      ++failures;                                             \
    }                                                         \
  } while (false)

using Float4 = std::array<float, 4>;

static std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::printf("cannot read %s\n", path.c_str());
    std::exit(2);
  }
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static ComPtr<ID3DBlob> CompileText(const std::string& source, const std::string& name, const char* entry, const char* target,
                                    const D3D_SHADER_MACRO* macros = nullptr) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source.data(), source.size(), name.c_str(), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("compile failed (%s): %s\n", name.c_str(), errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(2);
  }
  return code;
}

static ComPtr<ID3D11ComputeShader> ComputeShader(const std::string& source, const std::string& name, const D3D_SHADER_MACRO* macros = nullptr) {
  const auto code = CompileText(source, name, "main", "cs_5_0", macros);
  ComPtr<ID3D11ComputeShader> shader;
  device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf());
  return shader;
}

// The source with `from` replaced by `to` (once; it must be there).
static std::string Without(std::string source, const std::string& from, const std::string& to) {
  const auto at = source.find(from);
  if (at == std::string::npos) {
    std::printf("control text not found: %s\n", from.c_str());
    std::exit(2);
  }
  return source.replace(at, from.size(), to);
}

static ComPtr<ID3D11Buffer> Constants(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, buffer.GetAddressOf());
  return buffer;
}

static ComPtr<ID3D11Texture2D> Texture(UINT width, UINT height, const void* data, UINT bind, UINT array_size = 1, UINT misc = 0) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = array_size, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind, .MiscFlags = misc};
  std::vector<D3D11_SUBRESOURCE_DATA> initial(array_size);
  for (UINT slice = 0; slice < array_size; ++slice) {
    initial[slice] = {.pSysMem = static_cast<const uint8_t*>(data) + (static_cast<size_t>(slice) * width * height * 16), .SysMemPitch = width * 16};
  }
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, (data != nullptr) ? initial.data() : nullptr, texture.GetAddressOf());
  return texture;
}

static ComPtr<ID3D11ShaderResourceView> View(UINT width, UINT height, const std::vector<Float4>& texels) {
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(Texture(width, height, texels.data(), D3D11_BIND_SHADER_RESOURCE).Get(), nullptr, view.GetAddressOf());
  return view;
}

static std::vector<Float4> Read(ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<Float4> texels(static_cast<size_t>(desc.Width) * desc.Height);
  for (UINT row = 0; row < desc.Height; ++row) {
    std::memcpy(texels.data() + (static_cast<size_t>(row) * desc.Width), static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(row) * mapped.RowPitch),
                static_cast<size_t>(desc.Width) * 16);
  }
  context->Unmap(staging.Get(), 0);
  return texels;
}

static bool Near(float a, float b, float relative) {
  return std::fabs(a - b) <= (relative * std::max(std::fabs(a), std::fabs(b))) + 1e-30f;
}

// The game's sun shader's lighting_VS_PS (18 registers): sun_direction c0.yzw, sun_colour c1, the ambient cube c2-c7 (+x, -x,
// +y, -y, +z, -z), g_hdr_on c11.x, g_use_spherical_harmonics c11.z (0: the cube), the cloud uv's scale c12.w.
struct Lighting {
  std::array<float, 72> c = {};
};

static Lighting SunLighting(const Float4& sun_colour, const Float4& cube_x, float hdr_on) {
  Lighting lighting;
  auto& c = lighting.c;
  c[1] = -1.f;  // the sun shines along -x: the way to it +x
  std::copy_n(sun_colour.begin(), 3, c.begin() + 4);
  std::copy_n(cube_x.begin(), 3, c.begin() + 8);
  for (size_t side = 1; side < 6; ++side) c[8 + (side * 4)] = 0.01f * static_cast<float>(side);  // the other sides, faced away
  c[44] = hdr_on;
  c[51] = 1.f;
  return lighting;
}

// The game's sun shader at G-buffer pixel (1, 1) of 4 x 4: normal +x (t5 0.5, 0.5), the albedo in t6, depth 0.5 (t8.x),
// AO 1 (t2); everything else unbound (0: no specular colour, no material flags, no cloud, no cascade, no environment).
static Float4 RunGameSun(ID3D11PixelShader* shader, ID3D11VertexShader* vertex, const Lighting& lighting, float albedo) {
  const auto target = Texture(4, 4, nullptr, D3D11_BIND_RENDER_TARGET);
  ComPtr<ID3D11RenderTargetView> rtv;
  device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf());
  const auto ao = View(1, 1, {Float4{1.f, 1.f, 1.f, 1.f}});
  const auto normal = View(4, 4, std::vector<Float4>(16, Float4{0.5f, 0.5f, 0.f, 0.f}));
  const auto colour = View(4, 4, std::vector<Float4>(16, Float4{albedo, albedo, albedo, 0.f}));
  const auto depth = View(4, 4, std::vector<Float4>(16, Float4{0.5f, 0.f, 0.f, 0.f}));
  std::array<float, 4> colorimetry = {0.f, 1.f, 0.f, 0.f};
  std::array<float, 64> environment = {};
  std::array<float, 31 * 4> camera = {};
  camera[12 * 4 + 2] = 1.f;  // the view depth: z
  for (size_t row = 0; row < 4; ++row) camera[(21 + row) * 4 + row] = 1.f;  // inverse view-projection: identity
  camera[0 * 4 + 2] = -10.f;                                               // the camera behind
  for (const size_t reg : {27u, 29u}) {
    camera[reg * 4 + 0] = 4.f;
    camera[reg * 4 + 1] = 4.f;
    camera[reg * 4 + 2] = 0.25f;
    camera[reg * 4 + 3] = 0.25f;
  }
  std::array<float, 45 * 4> shadowmap = {};
  std::array<float, 2 * 4> shroud = {};
  std::array<float, 5 * 4> directional = {};
  const std::array<ID3D11Buffer*, 7> buffers = {
      Constants(colorimetry.data(), 16).Detach(), Constants(environment.data(), 256).Detach(), Constants(camera.data(), 31 * 16).Detach(),
      Constants(shadowmap.data(), 45 * 16).Detach(), Constants(shroud.data(), 32).Detach(), Constants(lighting.c.data(), 18 * 16).Detach(),
      Constants(directional.data(), 80).Detach()};
  context->PSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
  context->PSSetShaderResources(2, 1, ao.GetAddressOf());
  const std::array<ID3D11ShaderResourceView*, 4> gbuffer = {normal.Get(), colour.Get(), nullptr, depth.Get()};
  context->PSSetShaderResources(5, static_cast<UINT>(gbuffer.size()), gbuffer.data());
  context->VSSetShader(vertex, nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D11_VIEWPORT viewport = {.Width = 4.f, .Height = 4.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  context->ClearState();
  for (auto* buffer : buffers) buffer->Release();
  return Read(target.Get())[5];
}

static std::array<Float4, 9> RunSun(ID3D11ComputeShader* shader, const Lighting& lighting) {
  // rt_scene.hpp's offsets: sun_direction 1, the ambient cube 8, the sun colour 4 (InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4)).
  constexpr std::array<uint32_t, 8> OFFSETS = {1, 8, 0, 0, 4, 0, 0, 0};
  const std::vector<Float4> initial(9, Float4{7.f, 7.f, 7.f, 7.f});
  const auto texture = Texture(9, 1, initial.data(), D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> uav;
  device->CreateUnorderedAccessView(texture.Get(), nullptr, uav.GetAddressOf());
  const std::array<ID3D11Buffer*, 2> buffers = {Constants(lighting.c.data(), 256).Detach(), Constants(OFFSETS.data(), 32).Detach()};
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(0, 2, buffers.data());
  context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
  context->Dispatch(1, 1, 1);
  context->ClearState();
  for (auto* buffer : buffers) buffer->Release();
  std::array<Float4, 9> texels = {};
  std::memcpy(texels.data(), Read(texture.Get()).data(), sizeof(texels));
  return texels;
}

static void TestSun(const std::string& games, const std::string& dump) {
  const std::string bytes = ReadText(dump + "/0x230F2241.ps_4_1.cso");
  ComPtr<ID3D11PixelShader> game;
  CHECK(SUCCEEDED(device->CreatePixelShader(bytes.data(), bytes.size(), nullptr, game.GetAddressOf())), "the game's 230F2241");
  const auto vertex_code = CompileText("float4 main(uint id : SV_VertexID) : SV_Position { const float2 uv = float2((id << 1) & 2, id & 2);"
                                       " return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }",
                                       "triangle", "main", "vs_5_0");
  ComPtr<ID3D11VertexShader> vertex;
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf());
  const std::string wrapper = games + "/twwarhammer/rt_sun.cs_5_0.hlsl";
  const auto sun = ComputeShader(ReadText(wrapper), wrapper);
  const std::string shared = games + "/twattila/rt_sun.cs_5_0.hlsl";
  const auto shared_defaults = ComputeShader(ReadText(shared), shared);
  const Float4 sun_colour = {84112.55f, 71577.57f, 49356.4f, 0.f};
  const Float4 cube_x = {0.4870255f, 0.5041848f, 0.5121719f, 0.f};
  constexpr float ALBEDO_A = 0.2f;
  constexpr float ALBEDO_B = 0.6f;
  std::array<float, 2> diffuse = {};
  for (const float hdr_on : {1.f, 0.f}) {
    const auto sun_a = RunGameSun(game.Get(), vertex.Get(), SunLighting(sun_colour, {}, hdr_on), ALBEDO_A);
    const auto sun_b = RunGameSun(game.Get(), vertex.Get(), SunLighting(sun_colour, {}, hdr_on), ALBEDO_B);
    const auto sky_a = RunGameSun(game.Get(), vertex.Get(), SunLighting({}, cube_x, hdr_on), ALBEDO_A);
    const auto sky_b = RunGameSun(game.Get(), vertex.Get(), SunLighting({}, cube_x, hdr_on), ALBEDO_B);
    std::array<float, 3> sun_factor = {};
    std::array<float, 3> sky_factor = {};
    for (size_t channel = 0; channel < 3; ++channel) {
      sun_factor[channel] = (sun_b[channel] - sun_a[channel]) / ((ALBEDO_B - ALBEDO_A) * sun_colour[channel]);
      sky_factor[channel] = (sky_b[channel] - sky_a[channel]) / ((ALBEDO_B - ALBEDO_A) * cube_x[channel]);
      CHECK(std::isfinite(sun_b[channel]) && std::isfinite(sky_b[channel]), "g_hdr_on %g: not finite", hdr_on);
    }
    const float expected = (hdr_on > 0.f) ? 0.0031831f : 0.318310f;
    for (size_t channel = 0; channel < 3; ++channel) {
      CHECK(Near(sun_factor[channel], expected, 1e-4f), "g_hdr_on %g: the game's sun %zu x %.8g (%.8g)", hdr_on, channel, sun_factor[channel], expected);
      CHECK(Near(sky_factor[channel], 1.f, 1e-4f), "g_hdr_on %g: the game's ambient %zu x %.8g", hdr_on, channel, sky_factor[channel]);
    }
    diffuse[(hdr_on > 0.f) ? 0 : 1] = sun_factor[0];
    std::printf("230F2241 g_hdr_on %g: diffuse x %.8g %.8g %.8g of the sun colour, ambient x %.6g %.6g %.6g of the cube\n", hdr_on, sun_factor[0],
                sun_factor[1], sun_factor[2], sky_factor[0], sky_factor[1], sky_factor[2]);
  }
  Lighting lighting = SunLighting(sun_colour, cube_x, 1.f);
  const auto texels = RunSun(sun.Get(), lighting);
  for (size_t channel = 0; channel < 3; ++channel) {
    CHECK(Near(texels[2][channel], sun_colour[channel] * diffuse[0], 1e-4f), "rt_sun sun %zu %.8g vs the game's %.8g", channel, texels[2][channel],
          sun_colour[channel] * diffuse[0]);
    CHECK(texels[3][channel] == cube_x[channel], "rt_sun cube +x %zu %g", channel, texels[3][channel]);
    for (size_t side = 1; side < 6; ++side) {
      CHECK(texels[3 + side][channel] == lighting.c[8 + (side * 4) + channel], "rt_sun cube %zu.%zu %g", side, channel, texels[3 + side][channel]);
    }
  }
  CHECK(texels[0][0] == 1.f && texels[0][1] == 0.f && texels[0][2] == 0.f && texels[0][3] == 1.f, "rt_sun the way to the sun %g %g %g", texels[0][0],
        texels[0][1], texels[0][2]);
  CHECK(texels[2][3] == 1.f && texels[4][3] == 1.f && texels[1][0] == 7.f, "rt_sun w %g, sky w %g, texel 1 %g", texels[2][3], texels[4][3], texels[1][0]);
  const auto defaults = RunSun(shared_defaults.Get(), lighting);
  CHECK(!Near(defaults[2][0], texels[2][0], 0.05f), "CONTROL the shared defaults' sun %.8g matched", defaults[2][0]);
  CHECK(!Near(defaults[4][3], texels[4][3], 0.05f), "CONTROL the shared defaults' sky w %g matched", defaults[4][3]);
  std::printf("rt_sun: sun %.6g %.6g %.6g (x %.8g), sky w %g; the shared defaults: sun x %.6g, sky w %g\n", texels[2][0], texels[2][1], texels[2][2],
              texels[2][0] / sun_colour[0], texels[4][3], defaults[2][0] / sun_colour[0], defaults[4][3]);
}

// The resolve's inputs at `size` x `size` (`view` x `view` of them the view's: the same texels; the rest 7s, the radiance's
// ramp running on): normal (theta 0, phi -3 pi / 8: the reflection away from the camera), roughness 0.5, specular colour 0.5 with the SSR flag, depth 0.1 (10 m),
// the march's hit (confidence 1, `hit`), the scene's radiance a ramp (x + 0.5, y + 0.5, 1).
struct ResolveRun {
  UINT size = 64;
  float share = 0.f;  // cb12's frame share (0: unbound)
  float hit = 65504.f;
};

static std::vector<Float4> RunResolve(ID3D11ComputeShader* shader, const ResolveRun& run) {
  constexpr UINT VIEW = 64;
  const UINT size = run.size;
  std::vector<Float4> normal(size * size, Float4{7.f, 7.f, 7.f, 7.f});
  std::vector<Float4> specular = normal;
  std::vector<Float4> depth = normal;
  std::vector<Float4> radiance(size * size);
  for (UINT y = 0; y < size; ++y) {
    for (UINT x = 0; x < size; ++x) {
      const size_t at = (static_cast<size_t>(y) * size) + x;
      radiance[at] = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 1.f, 1.f};
      if (x >= VIEW || y >= VIEW) continue;
      normal[at] = {0.5f, 0.3125f, 0.5f, 0.f};
      specular[at] = {0.5f, 0.5f, 0.5f, 1.f / 255.f};
      depth[at] = {0.1f, 0.f, 0.f, 0.f};
    }
  }
  const std::vector<Float4> cube(6, Float4{0.25f, 0.25f, 0.25f, 1.f});
  const auto cube_texture = Texture(1, 1, cube.data(), D3D11_BIND_SHADER_RESOURCE, 6, D3D11_RESOURCE_MISC_TEXTURECUBE);
  ComPtr<ID3D11ShaderResourceView> cube_view;
  const D3D11_SHADER_RESOURCE_VIEW_DESC cube_desc = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY,
                                                     .TextureCubeArray = {.MostDetailedMip = 0, .MipLevels = 1, .First2DArrayFace = 0, .NumCubes = 1}};
  device->CreateShaderResourceView(cube_texture.Get(), &cube_desc, cube_view.GetAddressOf());
  const std::array<ID3D11ShaderResourceView*, 7> views = {
      cube_view.Get(), View(size, size, normal).Detach(), View(size, size, specular).Detach(), View(size, size, depth).Detach(),
      View(64, 64, std::vector<Float4>(64 * 64, Float4{1.f, 0.5f, 0.5f, 0.f})).Detach(),
      View(size / 2, size / 2, std::vector<Float4>((size / 2) * (size / 2), Float4{1.f, run.hit, 0.f, 0.f})).Detach(), View(size, size, radiance).Detach()};
  const auto output = Texture(size, size, nullptr, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> uav;
  device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf());
  std::array<float, 16 * 4> environment = {};
  environment[0] = 1.f;       // weight 0
  environment[15 * 4 + 1] = 1.f;  // g_environment_count
  std::array<float, 39 * 4> camera = {};
  for (const size_t row : {0u, 1u, 2u}) camera[(1 + row) * 4 + row] = 1.f;  // view: identity (the camera at the origin)
  camera[4 * 4 + 3] = 1.f;
  camera[5 * 4 + 0] = 1.f;  // projection: x, y over z
  camera[6 * 4 + 1] = 1.f;
  camera[8 * 4 + 2] = 1.f;
  camera[21 * 4 + 0] = 1.f;  // inverse view-projection: (x, y, 1) over depth
  camera[22 * 4 + 1] = 1.f;
  camera[23 * 4 + 3] = 1.f;
  camera[24 * 4 + 2] = 1.f;
  camera[25 * 4 + 0] = 9.5f;  // near, far: around the surface (10 m), the march's end on screen near its pixel
  camera[25 * 4 + 1] = 10.5f;
  const auto extent = static_cast<float>(size);
  camera[27 * 4 + 0] = extent;  // g_screen_size: the targets'
  camera[27 * 4 + 1] = extent;
  camera[27 * 4 + 2] = 1.f / extent;
  camera[27 * 4 + 3] = 1.f / extent;
  const std::array<float, 4> render_scale = {1.f, 1.f, run.share, run.share};
  const std::array<ID3D11Buffer*, 2> buffers = {Constants(environment.data(), 256).Detach(), Constants(camera.data(), 39 * 16).Detach()};
  const auto scale = Constants(render_scale.data(), 16);
  const D3D11_SAMPLER_DESC point = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                    .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  D3D11_SAMPLER_DESC linear = point;
  linear.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  ComPtr<ID3D11SamplerState> point_sampler;
  ComPtr<ID3D11SamplerState> linear_sampler;
  device->CreateSamplerState(&point, point_sampler.GetAddressOf());
  device->CreateSamplerState(&linear, linear_sampler.GetAddressOf());
  const std::array<ID3D11SamplerState*, 5> samplers = {linear_sampler.Get(), point_sampler.Get(), point_sampler.Get(), point_sampler.Get(), linear_sampler.Get()};
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(0, 2, buffers.data());
  if (run.share > 0.f) context->CSSetConstantBuffers(12, 1, scale.GetAddressOf());
  context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  context->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
  context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
  context->Dispatch((size + 31) / 32, (size + 31) / 32, 1);
  context->ClearState();
  for (auto* buffer : buffers) buffer->Release();
  for (size_t index = 1; index < views.size(); ++index) views[index]->Release();
  return Read(output.Get());
}

// Where the march's read landed (texels from the pixel's centre): the output is (the read - the cube) x F with the read's z 1.
static std::array<float, 2> Offset(const Float4& texel, UINT x, UINT y) {
  const float f = texel[2] / 0.75f;
  return {(texel[0] / f) + 0.25f - (static_cast<float>(x) + 0.5f), (texel[1] / f) + 0.25f - (static_cast<float>(y) + 0.5f)};
}

static void TestResolve(const std::string& games) {
  const std::string path = games + "/twwarhammer/shaders/ssr00_0x6B80E35B.cs_5_0.hlsl";
  const std::string source = ReadText(path);
  const auto fixed = ComputeShader(source, path);
  const auto no_position = ComputeShader(Without(source, "(r0.zw / FrameRenderShare())", "r0.zw"), path);
  const auto no_projected = ComputeShader(Without(source, "(r2.xz * float2(0.5,-0.5) + float2(0.5,0.5)) * FrameRenderShare()",
                                                  "(r2.xz * float2(0.5,-0.5) + float2(0.5,0.5))"),
                                          path);
  constexpr UINT VIEW = 64;
  constexpr float MARCH = 64000.f;  // -log2(64000 x 1.52662433e-005): 0.0335 of the uv
  const auto full = RunResolve(fixed.Get(), {.size = VIEW});
  const auto half = RunResolve(fixed.Get(), {.size = VIEW * 2, .share = 0.5f});
  const auto unbound = RunResolve(fixed.Get(), {.size = VIEW * 2});
  const auto half_no_position = RunResolve(no_position.Get(), {.size = VIEW * 2, .share = 0.5f});
  const auto full_march = RunResolve(fixed.Get(), {.size = VIEW, .hit = MARCH});
  const auto half_march = RunResolve(fixed.Get(), {.size = VIEW * 2, .share = 0.5f, .hit = MARCH});
  const auto half_march_no_projected = RunResolve(no_projected.Get(), {.size = VIEW * 2, .share = 0.5f, .hit = MARCH});
  int same = 0;
  int unbound_differs = 0;
  int position_differs = 0;
  int directions = 0;
  int projected_differs = 0;
  float worst_cos = 1.f;
  float worst_ratio = 0.f;
  float output_sum = 0.f;
  for (UINT y = 0; y < VIEW; ++y) {
    for (UINT x = 0; x < VIEW; ++x) {
      const auto& a = full[(y * VIEW) + x];
      const auto& b = half[(y * VIEW * 2) + x];
      const auto& c = unbound[(y * VIEW * 2) + x];
      const auto& d = half_no_position[(y * VIEW * 2) + x];
      bool equal = true;
      bool c_differs = false;
      bool d_differs = false;
      for (size_t channel = 0; channel < 3; ++channel) {
        equal = equal && Near(a[channel], b[channel], 1e-4f);
        c_differs = c_differs || !Near(a[channel], c[channel], 1e-3f);
        d_differs = d_differs || !Near(a[channel], d[channel], 1e-3f);
        output_sum += std::fabs(a[channel]);
      }
      same += equal ? 1 : 0;
      unbound_differs += c_differs ? 1 : 0;
      position_differs += d_differs ? 1 : 0;
      if (x < 8 || y < 8 || x >= VIEW - 8 || y >= VIEW - 8) continue;
      const auto o_full = Offset(full_march[(y * VIEW) + x], x, y);
      const auto o_half = Offset(half_march[(y * VIEW * 2) + x], x, y);
      const auto o_none = Offset(half_march_no_projected[(y * VIEW * 2) + x], x, y);
      const float l_full = std::hypot(o_full[0], o_full[1]);
      const float l_half = std::hypot(o_half[0], o_half[1]);
      const float l_none = std::hypot(o_none[0], o_none[1]);
      if (l_full < 0.5f) continue;
      ++directions;
      const float cos_half = ((o_full[0] * o_half[0]) + (o_full[1] * o_half[1])) / (l_full * l_half);
      const float cos_none = ((o_full[0] * o_none[0]) + (o_full[1] * o_none[1])) / (l_full * l_none);
      worst_cos = std::min(worst_cos, cos_half);
      worst_ratio = std::max(worst_ratio, std::fabs((l_half / l_full) - 2.f));
      projected_differs += (cos_none < 0.999f) ? 1 : 0;
    }
  }
  constexpr int PIXELS = VIEW * VIEW;
  CHECK(output_sum > 1.f, "the resolve wrote nothing (%g)", output_sum);
  CHECK(same == PIXELS, "50%% vs 100%%: %d of %d the same", same, PIXELS);
  CHECK(directions > 1000, "the march's offsets: %d pixels", directions);
  CHECK(worst_cos > 0.9999f, "the march's direction at 50%%: worst cos %.6f", worst_cos);
  CHECK(worst_ratio < 0.01f, "the march's length at 50%%: 2 x in texels (uv the same) off by %.4f", worst_ratio);
  CHECK(unbound_differs > PIXELS / 2, "CONTROL cb12 unbound at 50%%: %d of %d differ", unbound_differs, PIXELS);
  CHECK(position_differs > PIXELS / 2, "CONTROL without the position's share: %d of %d differ", position_differs, PIXELS);
  CHECK(projected_differs > directions / 2, "CONTROL without the projected uv's share: %d of %d directions differ", projected_differs, directions);
  std::printf("SSR resolve: 50%% vs 100%% %d of %d the same; the march's direction %d pixels, worst cos %.6f, length 2 x off by %.4f; CONTROLS "
              "unbound %d, no position share %d, no projected share %d of %d\n",
              same, PIXELS, directions, worst_cos, worst_ratio, unbound_differs, position_differs, projected_differs, directions);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("tests <repo root> <the game's shader dump folder>\n");
    return 2;
  }
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    return 2;
  }
  const std::string games = std::string(argv[1]) + "/src/games";
  TestSun(games, argv[2]);
  TestResolve(games);
  std::printf(failures == 0 ? "PASS\n" : "FAILED: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
