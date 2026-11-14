// Rome Remastered's ray tracing host (src/games/twromeremastered/ray_tracing.hpp) on WARP, through the module's own code
// and the shaders the addon embeds (build\twromeremastered.include), the game's own forward PBR pixel shader (ps
// 52170167, the battle's most drawn: its DXBC from the dump, argv[1]) and the cameras and frame constants of ATN's
// 2026-11-14 traces. tests.exe <0x52170167.ps_5_0.cso>
// 1. SceneDraw: a prepass draw into the scene's depth (no target, depth written) is in the scene, captured every frame;
//    out: a unit's, the grass's, a terrain overlay's, a banner's vertex shader, a draw with a target, another depth (the
//    cascades'), depth not written (the forward pass's EQUAL); the battle's kinds only as Battle Objects lets them in
//    (Tested: none, Solid: solid ones, All: an alpha-tested one too).
// 2. The trace's camera: rt_positions (rt_core's bytes) with the jittered battle camera given transposed gives each
//    pixel's world position on a ground plane (against the CPU), the sky 0, the eye where the view says; CONTROL: the
//    camera untransposed is far off.
// 3. Trace without the helper: nothing with the rows off; its objects made, no trace (the helper missing). rt_frame_sun:
//    the frame constants' c2 (toward the sun) normalised into the sun texture's texel 0, texel 1 (the camera) kept;
//    CONTROL: not c1.
// 4. DrawMultiply: the mask's red x the visibility (to 1 / 255), its green kept, 1 keeps the red to the bit; ASSAO's R8 the
//    same; the game's state (targets, viewport, blend, shaders, a view, the topology) the same after.
// 5. The game's bytes: the PBR pixel shader's output is affine in the mask's red (the sun's term x it; 1 is the game's
//    unshadowed light), the mask's green changes nothing; the AO changes it; the mask and the AO multiplied by the module
//    (4) light it as the products would.
// 6. contact_shadows::Draw (Bend's march, twattila's sss_setup / sss_bend, into the mask): a pillar's shadow from a sun at
//    the camera's left darkens the ground the CPU finds shadowed, the sunlit ground and the mask's green kept; CONTROL: the
//    sun at the right (the setup's sign the other way) leaves it lit.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>
#include "src/games/twromeremastered/contact_shadows.hpp"
#include "src/games/twromeremastered/ray_tracing.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

// utils/settings.hpp's (the graphics page's modules through dlaa.hpp)
extern "C" __declspec(dllexport) const char* const NAME = "Rome Remastered test";

using Microsoft::WRL::ComPtr;
namespace rays = twromeremastered::ray_tracing;
namespace dlaa = twromeremastered::dlaa;
using Camera = std::array<float, 48>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

// trace_40_021503 (battle): VS b5 at the AO apply, PS b8 there.
constexpr Camera BATTLE = {-0.96002036f, -0.052900452f, -0.27488625f, 0, 0, 0.98198146f, -0.18897732f, 0, 0.27993017f, -0.18142207f,
                           -0.9427222f, 0, -390.28714f, -297.02258f, 43.657784f, 1, 1.0805525f, 0, 0, 0, 0, 1.9209821f, 0, 0, -0.f,
                           -0.f, 1.00004f, 1, 0, 0, -1.00004f, 0, -1.0373523f, -0.10162082f, -0.27489725f, -0.27488625f, 0,
                           1.8863689f, -0.18898489f, -0.18897732f, 0.30247924f, -0.34850857f, -0.94276f, -0.9427222f, -421.72574f,
                           -570.5751f, 42.659492f, 43.657784f};
constexpr std::array<uint32_t, 2> FRAME_BITS = {0x00000F00u, 0x00000870u};  // c0.xy as the trace printed them (5.381e-42, 3.027e-42)
constexpr std::array<float, 14> FRAME_REST = {0.00026041668f, 0.00046296295f, -378.39532f, 299.92102f, 96.52386f, 20.f, 0.03954813f,
                                              0.68519264f, 0.7272873f, 0.f, 0.15909064f, 0.15909027f, 0.1590898f, 9360.f};

using Matrix = std::array<double, 16>;
static Matrix Rows(const float* values) {
  Matrix result = {};
  for (int index = 0; index < 16; ++index) result[index] = values[index];
  return result;
}
static Matrix Inverse(Matrix m) {
  Matrix inverse = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) {
      if (std::abs(m[(row * 4) + column]) > std::abs(m[(pivot * 4) + column])) pivot = row;
    }
    for (int index = 0; index < 4; ++index) {
      std::swap(m[(pivot * 4) + index], m[(column * 4) + index]);
      std::swap(inverse[(pivot * 4) + index], inverse[(column * 4) + index]);
    }
    const double scale = 1. / m[(column * 4) + column];
    for (int index = 0; index < 4; ++index) {
      m[(column * 4) + index] *= scale;
      inverse[(column * 4) + index] *= scale;
    }
    for (int row = 0; row < 4; ++row) {
      if (row == column) continue;
      const double factor = m[(row * 4) + column];
      for (int index = 0; index < 4; ++index) {
        m[(row * 4) + index] -= factor * m[(column * 4) + index];
        inverse[(row * 4) + index] -= factor * inverse[(column * 4) + index];
      }
    }
  }
  return inverse;
}
static std::array<double, 4> Transform(const std::array<double, 4>& v, const Matrix& m) {
  std::array<double, 4> result = {};
  for (int column = 0; column < 4; ++column) {
    for (int k = 0; k < 4; ++k) result[column] += v[k] * m[(k * 4) + column];
  }
  return result;
}

static mock::Device mock_device;
static mock::CommandList command_list;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static ComPtr<ID3D11Texture2D> MakeTexture(UINT width, UINT height, DXGI_FORMAT format, UINT bind, const void* data = nullptr, UINT pitch = 0,
                                           UINT array_size = 1, UINT misc = 0) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = array_size, .Format = format,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind, .MiscFlags = misc};
  std::vector<D3D11_SUBRESOURCE_DATA> initial(array_size, {.pSysMem = data, .SysMemPitch = pitch});
  ComPtr<ID3D11Texture2D> texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, (data != nullptr ? initial.data() : nullptr), texture.GetAddressOf())));
  return texture;
}
static ComPtr<ID3D11ShaderResourceView> Srv(ID3D11Resource* resource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN) {
  ComPtr<ID3D11ShaderResourceView> view;
  if (format == DXGI_FORMAT_UNKNOWN) {
    REQUIRE(SUCCEEDED(device->CreateShaderResourceView(resource, nullptr, view.GetAddressOf())));
  } else {
    const D3D11_SHADER_RESOURCE_VIEW_DESC desc = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, .Texture2D = {.MipLevels = 1}};
    REQUIRE(SUCCEEDED(device->CreateShaderResourceView(resource, &desc, view.GetAddressOf())));
  }
  return view;
}
static ComPtr<ID3D11RenderTargetView> Rtv(ID3D11Resource* resource) {
  ComPtr<ID3D11RenderTargetView> view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(resource, nullptr, view.GetAddressOf())));
  return view;
}
static ComPtr<ID3D11Buffer> ConstantBuffer(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &initial, buffer.GetAddressOf())));
  return buffer;
}
template <typename T>
static std::vector<T> ReadBack(ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  const UINT width = desc.Width;
  const UINT height = desc.Height;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
  context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<T> values(static_cast<size_t>(width) * height);
  for (UINT row = 0; row < height; ++row) {
    std::memcpy(&values[static_cast<size_t>(row) * width], static_cast<const std::byte*>(mapped.pData) + (row * mapped.RowPitch), width * sizeof(T));
  }
  context->Unmap(staging.Get(), 0);
  return values;
}
static float Half(uint16_t value) {
  const uint32_t exponent = (value >> 10) & 0x1Fu;
  const float magnitude = (exponent == 0) ? std::ldexp(static_cast<float>(value & 0x3FFu), -24)
                                          : std::ldexp(static_cast<float>((value & 0x3FFu) | 0x400u), static_cast<int>(exponent) - 25);
  return ((value & 0x8000u) != 0 ? -magnitude : magnitude);
}

int main(int argc, char** argv) {
  REQUIRE(argc >= 2);
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                      context.GetAddressOf())));
  mock_device.native = device;
  command_list.owner = &mock_device;
  command_list.native = context;
  constexpr UINT WIDTH = 64;
  constexpr UINT HEIGHT = 36;
  auto& camera_state = dlaa::internal::state;
  camera_state.display = {WIDTH, HEIGHT};

  // The scene's depth (the prepass's D32) and the cascades' atlas (D16).
  const auto scene_depth = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
  const auto atlas = MakeTexture(128, 128, DXGI_FORMAT_R16_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
  ComPtr<ID3D11DepthStencilView> scene_dsv;
  ComPtr<ID3D11DepthStencilView> atlas_dsv;
  const D3D11_DEPTH_STENCIL_VIEW_DESC d32 = {.Format = DXGI_FORMAT_D32_FLOAT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
  const D3D11_DEPTH_STENCIL_VIEW_DESC d16 = {.Format = DXGI_FORMAT_D16_UNORM, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
  REQUIRE(SUCCEEDED(device->CreateDepthStencilView(scene_depth.Get(), &d32, scene_dsv.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateDepthStencilView(atlas.Get(), &d16, atlas_dsv.GetAddressOf())));
  camera_state.depth = Srv(scene_depth.Get(), DXGI_FORMAT_R32_FLOAT);

  // 1. SceneDraw.
  {
    ComPtr<ID3D11DepthStencilState> writes;
    ComPtr<ID3D11DepthStencilState> equal;
    const D3D11_DEPTH_STENCIL_DESC writes_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL, .DepthFunc = D3D11_COMPARISON_LESS_EQUAL};
    const D3D11_DEPTH_STENCIL_DESC equal_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO, .DepthFunc = D3D11_COMPARISON_EQUAL};
    REQUIRE(SUCCEEDED(device->CreateDepthStencilState(&writes_desc, writes.GetAddressOf())));
    REQUIRE(SUCCEEDED(device->CreateDepthStencilState(&equal_desc, equal.GetAddressOf())));
    const auto colour = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET);
    const auto colour_rtv = Rtv(colour.Get());
    const auto scene_draw = [&](uint32_t hash, ID3D11RenderTargetView* target, ID3D11DepthStencilView* depth, ID3D11DepthStencilState* depth_state) {
      context->OMSetRenderTargets((target != nullptr ? 1u : 0u), &target, depth);
      context->OMSetDepthStencilState(depth_state, 0);
      twattila::rt_scene::SceneShader shader = {.hash = hash};
      const bool in = rays::internal::SceneDraw(context.Get(), &shader);
      return std::pair{in, shader.dynamic};
    };
    // The campaign's tested kinds in; the battle's only as Battle Objects lets them in (Solid: no pixel shader here,
    // solid; All: an alpha-tested one too); the banners never.
    const auto [pbr, pbr_dynamic] = scene_draw(0x9594545F, nullptr, scene_dsv.Get(), writes.Get());
    CHECK(pbr && pbr_dynamic);
    CHECK(scene_draw(0x8A00B527, nullptr, scene_dsv.Get(), writes.Get()).first);   // the campaign's terrain
    CHECK(!scene_draw(0xC35BD3D6, nullptr, scene_dsv.Get(), writes.Get()).first);  // a battle prop, Tested
    CHECK(!scene_draw(0x7DD754DA, nullptr, scene_dsv.Get(), writes.Get()).first);  // the battle's terrain, Tested
    rays::battle_scene = 1.f;
    CHECK(scene_draw(0xC35BD3D6, nullptr, scene_dsv.Get(), writes.Get()).first);
    CHECK(scene_draw(0x7DD754DA, nullptr, scene_dsv.Get(), writes.Get()).first);
    {
      // An alpha-tested pixel shader (rt_core's classification, as the game's D2F340DB): out at Solid, in at All.
      ComPtr<ID3D11PixelShader> masked;
      ComPtr<ID3DBlob> masked_code;
      constexpr char MASKED_SOURCE[] = "Texture2D t : register(t0); SamplerState s; void main(float4 p : SV_Position, float2 uv : TEXCOORD0) {"
                                       " clip(t.Sample(s, uv).a - 0.5); }";
      REQUIRE(SUCCEEDED(D3DCompile(MASKED_SOURCE, sizeof(MASKED_SOURCE) - 1, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0,
                                   masked_code.GetAddressOf(), nullptr)));
      REQUIRE(SUCCEEDED(device->CreatePixelShader(masked_code->GetBufferPointer(), masked_code->GetBufferSize(), nullptr, masked.GetAddressOf())));
      twattila::rt_scene::internal::pixel_coverage[reinterpret_cast<uint64_t>(masked.Get())] =
          twattila::rt_scene::internal::ClassifyCoverage({static_cast<const uint8_t*>(masked_code->GetBufferPointer()), masked_code->GetBufferSize()});
      context->PSSetShader(masked.Get(), nullptr, 0);
      const auto alpha_tested = [&] {
        twattila::rt_scene::SceneShader shader = {.hash = 0xC35BD3D6};
        context->OMSetRenderTargets(0, nullptr, scene_dsv.Get());
        context->OMSetDepthStencilState(writes.Get(), 0);
        const bool in = rays::internal::SceneDraw(context.Get(), &shader);
        return std::pair{in, shader.foliage};
      };
      CHECK(!alpha_tested().first);
      rays::battle_scene = 2.f;
      const auto [in, foliage] = alpha_tested();
      CHECK(in && foliage);
      context->PSSetShader(nullptr, nullptr, 0);
    }
    CHECK(!scene_draw(0xB6E1E42E, nullptr, scene_dsv.Get(), writes.Get()).first);  // a banner, All
    CHECK(!scene_draw(0xB31241CA, nullptr, scene_dsv.Get(), writes.Get()).first);  // a unit
    CHECK(!scene_draw(0x826801BF, nullptr, scene_dsv.Get(), writes.Get()).first);  // grass
    CHECK(!scene_draw(0x125EEF55, nullptr, scene_dsv.Get(), writes.Get()).first);  // a terrain overlay
    CHECK(!scene_draw(0x877C08D1, nullptr, scene_dsv.Get(), writes.Get()).first);  // a far tree's card
    CHECK(!scene_draw(0xC35BD3D6, colour_rtv.Get(), scene_dsv.Get(), writes.Get()).first);  // a target
    CHECK(!scene_draw(0xC35BD3D6, nullptr, atlas_dsv.Get(), writes.Get()).first);  // the cascades
    CHECK(!scene_draw(0xC35BD3D6, colour_rtv.Get(), scene_dsv.Get(), equal.Get()).first);  // the forward pass
    CHECK(!scene_draw(0xC35BD3D6, nullptr, scene_dsv.Get(), equal.Get()).first);
    context->ClearState();
  }

  // 2. The trace's camera through rt_positions.
  dlaa::rays = true;
  camera_state.jitter = {0.3f, -0.2f};
  {
    Camera upload = BATTLE;
    void* data = upload.data();
    const D3D11_BUFFER_DESC desc = {.ByteWidth = 192, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    ComPtr<ID3D11Buffer> camera_buffer;
    REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, camera_buffer.GetAddressOf())));
    const reshade::api::resource resource = {reinterpret_cast<uint64_t>(camera_buffer.Get())};
    dlaa::internal::OnMapBufferRegion(&mock_device, resource, 0, UINT64_MAX, reshade::api::map_access::write_discard, &data);
    dlaa::internal::OnUnmapBufferRegion(&mock_device, resource);
  }
  dlaa::internal::Camera camera;
  REQUIRE(dlaa::MainCamera(&camera));
  {
    const Matrix inverse_view = Inverse(Rows(BATTLE.data()));
    double eye_error = 0.;
    for (int axis = 0; axis < 3; ++axis) eye_error = std::max(eye_error, std::abs(camera.position[axis] - inverse_view[12 + axis]));
    std::printf("eye: %.3f %.3f %.3f, %.2e off the view's inverse\n", camera.position[0], camera.position[1], camera.position[2], eye_error);
    CHECK(eye_error < 1e-3);
    const Matrix jittered = Rows(camera.jittered_view_projection.data());
    const Matrix inverse_jittered = Inverse(jittered);
    const double ground = inverse_view[13] - 30.;
    std::vector<float> depth(WIDTH * HEIGHT);
    std::vector<std::array<double, 3>> world(WIDTH * HEIGHT);
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) {
        const size_t at = (static_cast<size_t>(y) * WIDTH) + x;
        const double ndc_x = (((x + 0.5) / WIDTH) * 2.) - 1.;
        const double ndc_y = 1. - (((y + 0.5) / HEIGHT) * 2.);
        auto near_point = Transform({ndc_x, ndc_y, 0., 1.}, inverse_jittered);
        auto far_point = Transform({ndc_x, ndc_y, 0.5, 1.}, inverse_jittered);
        for (int k = 0; k < 3; ++k) {
          near_point[k] /= near_point[3];
          far_point[k] /= far_point[3];
        }
        const double along = (ground - near_point[1]) / (far_point[1] - near_point[1]);
        if (along <= 0. || along > 2000.) {
          depth[at] = 1.f;  // the sky: the depth's cleared value
          world[at] = {0., 0., 0.};
          continue;
        }
        std::array<double, 4> point = {0., 0., 0., 1.};
        for (int k = 0; k < 3; ++k) point[k] = near_point[k] + (along * (far_point[k] - near_point[k]));
        const auto drawn = Transform(point, jittered);
        depth[at] = static_cast<float>(drawn[2] / drawn[3]);
        world[at] = {point[0], point[1], point[2]};
      }
    }
    const auto depth_texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, depth.data(), WIDTH * 4);
    const auto depth_srv = Srv(depth_texture.Get());
    const auto positions = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
    const auto sun = MakeTexture(2, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11UnorderedAccessView> positions_uav;
    ComPtr<ID3D11UnorderedAccessView> sun_uav;
    REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(positions.Get(), nullptr, positions_uav.GetAddressOf())));
    REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(sun.Get(), nullptr, sun_uav.GetAddressOf())));
    ComPtr<ID3D11ComputeShader> positions_shader;
    REQUIRE(SUCCEEDED(device->CreateComputeShader(__rt_positions.data(), __rt_positions.size(), nullptr, positions_shader.GetAddressOf())));
    const auto run = [&](const std::array<float, 16>& inverse) {
      const twattila::rt_scene::internal::PositionConstants constants = {
          .inverse_view_projection = inverse,
          .camera = {camera.position[0], camera.position[1], camera.position[2], 1.f},
          .size = {WIDTH, HEIGHT, 0, 0},
      };
      const auto buffer = ConstantBuffer(&constants, sizeof(constants));
      const std::array<ID3D11UnorderedAccessView*, 2> uavs = {positions_uav.Get(), sun_uav.Get()};
      context->CSSetShader(positions_shader.Get(), nullptr, 0);
      context->CSSetShaderResources(0, 1, depth_srv.GetAddressOf());
      context->CSSetUnorderedAccessViews(0, 2, uavs.data(), nullptr);
      context->CSSetConstantBuffers(0, 1, buffer.GetAddressOf());
      context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
      context->ClearState();
      const auto values = ReadBack<std::array<float, 4>>(positions.Get());
      double worst = 0.;
      bool sky_zero = true;
      for (size_t at = 0; at < values.size(); ++at) {
        if (depth[at] == 1.f) {
          sky_zero = sky_zero && values[at][0] == 0.f && values[at][3] == 0.f;
          continue;
        }
        const double distance = std::hypot(world[at][0] - camera.position[0], world[at][1] - camera.position[1], world[at][2] - camera.position[2]);
        const double error = std::hypot(values[at][0] - world[at][0], values[at][1] - world[at][1], values[at][2] - world[at][2]);
        worst = std::max(worst, error / (1. + distance));
      }
      return std::pair{worst, sky_zero};
    };
    const auto [worst, sky_zero] = run(dlaa::internal::Transposed(camera.inverse_view_projection));
    const auto [control, control_sky] = run(camera.inverse_view_projection);
    std::printf("rt_positions: worst %.2e of the distance (CONTROL untransposed: %.2e)\n", worst, control);
    CHECK(worst < 1e-3);
    CHECK(sky_zero);
    CHECK(control > 0.1);
  }

  // 3. Trace without the helper; rt_frame_sun.
  std::array<float, 16> frame_constants = {};
  std::memcpy(frame_constants.data(), FRAME_BITS.data(), 8);
  std::copy(FRAME_REST.begin(), FRAME_REST.end(), frame_constants.begin() + 2);
  const auto frame_buffer = ConstantBuffer(frame_constants.data(), 64);
  {
    context->PSSetConstantBuffers(8, 1, frame_buffer.GetAddressOf());
    dlaa::rays = false;
    rays::Trace(&command_list);
    CHECK(!rays::internal::state.objects.device);
    dlaa::rays = true;
    rays::Trace(&command_list);
    CHECK(rays::internal::state.objects.device && !rays::internal::state.objects.failed && rays::internal::state.objects.sun);
    CHECK(twattila::dlaa_bridge::bridge.failed);  // no helper next to the test: no trace
    context->ClearState();
    const std::array<float, 8> marker = {9.f, 9.f, 9.f, 9.f, 7.f, 7.f, 7.f, 7.f};
    const auto sun = MakeTexture(2, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, marker.data(), 32);
    ComPtr<ID3D11UnorderedAccessView> sun_uav;
    REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(sun.Get(), nullptr, sun_uav.GetAddressOf())));
    context->CSSetShader(rays::internal::state.objects.sun.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, frame_buffer.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, sun_uav.GetAddressOf(), nullptr);
    context->Dispatch(1, 1, 1);
    context->ClearState();
    const auto texels = ReadBack<std::array<float, 4>>(sun.Get());
    const double length = std::sqrt((FRAME_REST[6] * FRAME_REST[6]) + (FRAME_REST[7] * FRAME_REST[7]) + (FRAME_REST[8] * FRAME_REST[8]));
    std::printf("sun texel 0: %.5f %.5f %.5f %.1f (toward the sun, c2 / %.5f)\n", texels[0][0], texels[0][1], texels[0][2], texels[0][3], length);
    for (int axis = 0; axis < 3; ++axis) CHECK(std::abs(texels[0][axis] - (FRAME_REST[6 + axis] / length)) < 1e-5);
    CHECK(texels[0][3] == 1.f);
    CHECK(texels[1][0] == 7.f && texels[1][3] == 7.f);
    CHECK(std::abs(texels[0][0] - FRAME_REST[2]) > 1.f);  // CONTROL: not c1 (the camera)
  }

  // 4. DrawMultiply, the game's state kept.
  ComPtr<ID3D11DeviceContext1> context1;
  REQUIRE(SUCCEEDED(context.As(&context1)));
  std::vector<uint8_t> visibility_bytes(WIDTH * HEIGHT);
  for (size_t at = 0; at < visibility_bytes.size(); ++at) visibility_bytes[at] = static_cast<uint8_t>((at * 37) % 256);
  visibility_bytes[0] = 255;
  const auto visibility = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, visibility_bytes.data(), WIDTH);
  const auto visibility_srv = Srv(visibility.Get());
  {
    std::vector<std::array<uint8_t, 2>> mask_bytes(WIDTH * HEIGHT, {230, 77});
    const auto mask = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8G8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, mask_bytes.data(), WIDTH * 2);
    const auto mask_rtv = Rtv(mask.Get());
    std::vector<uint8_t> ao_bytes(WIDTH * HEIGHT, 204);
    const auto ao = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, ao_bytes.data(), WIDTH);
    const auto ao_rtv = Rtv(ao.Get());
    // The game's state at the draw.
    const auto game_target = MakeTexture(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    const auto game_rtv = Rtv(game_target.Get());
    const auto game_view = Srv(atlas.Get(), DXGI_FORMAT_R16_UNORM);
    const D3D11_VIEWPORT game_viewport = {.TopLeftX = 1.f, .Width = 10.f, .Height = 10.f, .MaxDepth = 1.f};
    D3D11_BLEND_DESC game_blend_desc = {};
    game_blend_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ONE, .DestBlend = D3D11_BLEND_ONE, .BlendOp = D3D11_BLEND_OP_ADD,
                                       .SrcBlendAlpha = D3D11_BLEND_ONE, .DestBlendAlpha = D3D11_BLEND_ONE, .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                                       .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL};
    ComPtr<ID3D11BlendState> game_blend;
    REQUIRE(SUCCEEDED(device->CreateBlendState(&game_blend_desc, game_blend.GetAddressOf())));
    context->OMSetRenderTargets(1, game_rtv.GetAddressOf(), scene_dsv.Get());
    context->RSSetViewports(1, &game_viewport);
    context->OMSetBlendState(game_blend.Get(), nullptr, 0xFFFFFFFF);
    context->PSSetShaderResources(0, 1, game_view.GetAddressOf());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    rays::internal::DrawMultiply(context1.Get(), mask_rtv.Get(), visibility_srv.Get(), false);
    rays::internal::DrawMultiply(context1.Get(), ao_rtv.Get(), visibility_srv.Get(), false);
    ComPtr<ID3D11RenderTargetView> after_rtv;
    ComPtr<ID3D11DepthStencilView> after_dsv;
    context->OMGetRenderTargets(1, after_rtv.GetAddressOf(), after_dsv.GetAddressOf());
    D3D11_VIEWPORT after_viewport = {};
    UINT viewports = 1;
    context->RSGetViewports(&viewports, &after_viewport);
    ComPtr<ID3D11BlendState> after_blend;
    context->OMGetBlendState(after_blend.GetAddressOf(), nullptr, nullptr);
    ComPtr<ID3D11ShaderResourceView> after_view;
    context->PSGetShaderResources(0, 1, after_view.GetAddressOf());
    D3D11_PRIMITIVE_TOPOLOGY after_topology = {};
    context->IAGetPrimitiveTopology(&after_topology);
    CHECK(after_rtv == game_rtv && after_dsv == scene_dsv && after_viewport.Width == 10.f && after_viewport.TopLeftX == 1.f);
    CHECK(after_blend == game_blend && after_view == game_view && after_topology == D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    context->ClearState();
    const auto mask_after = ReadBack<std::array<uint8_t, 2>>(mask.Get());
    const auto ao_after = ReadBack<uint8_t>(ao.Get());
    int worst = 0;
    bool green_kept = true;
    for (size_t at = 0; at < mask_after.size(); ++at) {
      worst = std::max({worst, std::abs(static_cast<int>(mask_after[at][0]) - static_cast<int>(std::lround(230. * visibility_bytes[at] / 255.))),
                        std::abs(static_cast<int>(ao_after[at]) - static_cast<int>(std::lround(204. * visibility_bytes[at] / 255.)))});
      green_kept = green_kept && mask_after[at][1] == 77;
    }
    std::printf("multiply: worst %d / 255 off the product, green kept %d, visibility 1 keeps %d and %d\n", worst, green_kept ? 1 : 0, mask_after[0][0], ao_after[0]);
    CHECK(worst <= 1);
    CHECK(green_kept);
    CHECK(mask_after[0][0] == 230 && ao_after[0] == 204);
  }

  // 5. The game's PBR pixel shader.
  {
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<char> code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(code.size() == 7732);
    ComPtr<ID3D11PixelShader> pbr;
    REQUIRE(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, pbr.GetAddressOf())));
    // Its inputs (the dump's signature): SV_POSITION, NORMAL, POSITION, TEXCOORD0, SV_InstanceID; a ground plane below the
    // trace's camera facing the sun (its normal map's frame comes from the position's and UV's derivatives: both vary).
    const char* vertex_source = R"(
struct Output { float4 position : SV_POSITION; float3 normal : NORMAL; float3 world : POSITION; float2 uv : TEXCOORD0; nointerpolation uint instance : SV_InstanceID; };
Output main(uint id : SV_VertexID) {
  Output output;
  const float2 uv = float2((id << 1) & 2, id & 2);
  output.position = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.5f, 1.f);
  output.normal = float3(0.f, 1.f, 0.f);
  output.world = float3(-380.f + 20.f * uv.x, 270.f, 90.f + 20.f * uv.y);
  output.uv = uv * 4.f;
  output.instance = 0u;
  return output;
})";
    ComPtr<ID3DBlob> vertex_code;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_code.GetAddressOf(), errors.GetAddressOf()))) {
      std::printf("%s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "vs compile failed");
      REQUIRE(false);
    }
    ComPtr<ID3D11VertexShader> vertex_shader;
    REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf())));
    const std::array<uint8_t, 4> albedo = {160, 140, 120, 255};
    const std::array<uint8_t, 4> normal_map = {128, 128, 255, 255};
    const std::array<uint8_t, 4> material = {128, 64, 255, 255};
    const auto t0 = MakeTexture(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, albedo.data(), 4);
    const auto t1 = MakeTexture(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, normal_map.data(), 4);
    const auto t2 = MakeTexture(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, material.data(), 4);
    const std::array<uint16_t, 4> sky = {0x3400, 0x3400, 0x3800, 0x3C00};  // 0.25, 0.25, 0.5, 1 (half)
    const auto t14 = MakeTexture(1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, sky.data(), 8, 6, D3D11_RESOURCE_MISC_TEXTURECUBE);
    const D3D11_SHADER_RESOURCE_VIEW_DESC cube_desc = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE, .TextureCube = {.MipLevels = 1}};
    ComPtr<ID3D11ShaderResourceView> t14_srv;
    REQUIRE(SUCCEEDED(device->CreateShaderResourceView(t14.Get(), &cube_desc, t14_srv.GetAddressOf())));
    const std::array<uint16_t, 4> ambient = {0x3400, 0x3400, 0x3400, 0x3C00};
    const auto t15 = MakeTexture(1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, ambient.data(), 8);
    // The Forward+ lists: none (every tile's count 0).
    const std::vector<uint32_t> zeros(28 * 512 / 4, 0u);
    const auto structured = [&](UINT stride) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = stride * 512, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = stride};
      const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = zeros.data()};
      ComPtr<ID3D11Buffer> buffer;
      REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &initial, buffer.GetAddressOf())));
      return Srv(buffer.Get());
    };
    const auto t16 = structured(4);
    const auto t17 = structured(28);
    // b4: the light tiles (the trace's 240 x 135 ...), b0: the material's (ones), b8: the trace's frame constants.
    const std::array<uint32_t, 4> tiles = {240, 135, 230, 33};
    const auto b4 = ConstantBuffer(tiles.data(), 16);
    const std::array<float, 8> material_constants = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    const auto b0 = ConstantBuffer(material_constants.data(), 32);
    ComPtr<ID3D11SamplerState> sampler;
    const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
    REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf())));
    const auto output = MakeTexture(8, 8, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET);
    const auto output_rtv = Rtv(output.Get());
    const auto t0_srv = Srv(t0.Get());
    const auto t1_srv = Srv(t1.Get());
    const auto t2_srv = Srv(t2.Get());
    const auto t15_srv = Srv(t15.Get());
    // The pixel's lit colour with the mask (`mask_red`, `mask_green`) and the AO (`ao`) as 8-bit values, or their textures.
    const auto shade = [&](ID3D11ShaderResourceView* mask_view, ID3D11ShaderResourceView* ao_view) {
      const std::array<ID3D11ShaderResourceView*, 18> views = {t0_srv.Get(), t1_srv.Get(), t2_srv.Get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                               nullptr, nullptr, nullptr, mask_view, ao_view, t14_srv.Get(), t15_srv.Get(), t16.Get(), t17.Get()};
      const std::array<ID3D11SamplerState*, 6> samplers = {sampler.Get(), sampler.Get(), sampler.Get(), sampler.Get(), sampler.Get(), sampler.Get()};
      const std::array<ID3D11Buffer*, 9> constants = {b0.Get(), nullptr, nullptr, nullptr, b4.Get(), nullptr, nullptr, nullptr, frame_buffer.Get()};
      const D3D11_VIEWPORT viewport = {.Width = 8.f, .Height = 8.f, .MaxDepth = 1.f};
      context->ClearState();
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vertex_shader.Get(), nullptr, 0);
      context->PSSetShader(pbr.Get(), nullptr, 0);
      context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
      context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
      context->PSSetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
      context->RSSetViewports(1, &viewport);
      context->OMSetRenderTargets(1, output_rtv.GetAddressOf(), nullptr);
      context->Draw(3, 0);
      context->ClearState();
      const auto pixel = ReadBack<std::array<uint16_t, 4>>(output.Get())[(4 * 8) + 4];
      return std::array<double, 3>{Half(pixel[0]), Half(pixel[1]), Half(pixel[2])};
    };
    const auto uniform = [&](uint8_t red, uint8_t green, bool two_channels) {
      const std::vector<std::array<uint8_t, 2>> texels(16, {red, green});
      const std::vector<uint8_t> single(16, red);
      const auto texture = (two_channels ? MakeTexture(4, 4, DXGI_FORMAT_R8G8_UNORM, D3D11_BIND_SHADER_RESOURCE, texels.data(), 8)
                                         : MakeTexture(4, 4, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, single.data(), 4));
      return Srv(texture.Get());
    };
    const auto ao_full = uniform(255, 0, false);
    const auto lit = [&](uint8_t red, uint8_t green) { return shade(uniform(red, green, true).Get(), ao_full.Get()); };
    const auto none = lit(0, 0);
    const auto full = lit(255, 0);
    double affine = 0.;
    for (const uint8_t red : {51, 128, 204}) {
      const auto partial = lit(red, 0);
      for (int channel = 0; channel < 3; ++channel) {
        const double expected = none[channel] + ((red / 255.) * (full[channel] - none[channel]));
        affine = std::max(affine, std::abs(partial[channel] - expected) / std::max(std::abs(full[channel]), 1e-3));
      }
    }
    const auto green = lit(255, 255);
    const double sun_share = (full[1] - none[1]) / std::max(full[1], 1e-6);
    std::printf("PBR 52170167: unshadowed %.4f %.4f %.4f, the sun's share %.3f, affine in the mask's red within %.2e, green %.4f\n", full[0], full[1],
                full[2], sun_share, affine, green[1]);
    CHECK(sun_share > 0.05);
    CHECK(affine < 4e-3);
    CHECK(green == full);
    const auto mask_08 = uniform(204, 0, true);
    const auto ao_04 = uniform(102, 0, false);
    const auto darker = shade(mask_08.Get(), ao_04.Get());
    const auto lighter = shade(mask_08.Get(), ao_full.Get());
    std::printf("AO 0.4 against 1: %.4f against %.4f\n", darker[1], lighter[1]);
    CHECK(darker[1] < lighter[1]);
    // The module's products lit as the products: the mask 0.9 x 0.5, the AO 0.8 x 0.5.
    std::vector<std::array<uint8_t, 2>> mask_bytes(16, {230, 77});
    const auto mask = MakeTexture(4, 4, DXGI_FORMAT_R8G8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, mask_bytes.data(), 8);
    std::vector<uint8_t> ao_bytes(16, 204);
    const auto ao = MakeTexture(4, 4, DXGI_FORMAT_R8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, ao_bytes.data(), 4);
    const std::vector<uint8_t> half_open(16, 128);
    const auto half_visibility = MakeTexture(4, 4, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, half_open.data(), 4);
    rays::internal::DrawMultiply(context1.Get(), Rtv(mask.Get()).Get(), Srv(half_visibility.Get()).Get(), false);
    rays::internal::DrawMultiply(context1.Get(), Rtv(ao.Get()).Get(), Srv(half_visibility.Get()).Get(), false);
    context->ClearState();
    const auto multiplied = shade(Srv(mask.Get()).Get(), Srv(ao.Get()).Get());
    const auto products = shade(uniform(static_cast<uint8_t>(std::lround(230. * 128. / 255.)), 77, true).Get(),
                                uniform(static_cast<uint8_t>(std::lround(204. * 128. / 255.)), 0, false).Get());
    std::printf("multiplied by the module: %.4f, the products: %.4f\n", multiplied[1], products[1]);
    for (int channel = 0; channel < 3; ++channel) CHECK(std::abs(multiplied[channel] - products[channel]) <= 2e-3 * std::max(products[channel], 1e-3));
  }

  // 6. contact_shadows::Draw: a 3 x 8 x 3 m pillar on the ground 64 m in front of the battle camera (unjittered), the
  //    sun from the camera's left (c2 toward it). The ground the CPU finds in the pillar's shadow darkened in the mask's
  //    red (more of it with a thicker Thickness), the ground in the sun kept, the green kept; CONTROL: the sun from the
  //    right leaves that shadow lit.
  {
    constexpr UINT SIZE_X = 480;
    constexpr UINT SIZE_Y = 270;
    dlaa::internal::Camera battle;
    std::copy_n(BATTLE.begin(), 16, battle.view.begin());
    std::copy_n(BATTLE.begin() + 16, 16, battle.projection.begin());
    std::copy_n(BATTLE.begin() + 32, 16, battle.view_projection.begin());
    battle.jittered_view_projection = battle.view_projection;
    const Matrix view_projection = Rows(battle.view_projection.data());
    const Matrix inverse = Inverse(view_projection);
    const Matrix inverse_view = Inverse(Rows(BATTLE.data()));
    const std::array<double, 3> eye = {inverse_view[12], inverse_view[13], inverse_view[14]};
    const std::array<double, 3> forward = {BATTLE[2], BATTLE[6], BATTLE[10]};
    const std::array<double, 3> right = {BATTLE[0], BATTLE[4], BATTLE[8]};
    const double ground = eye[1] - 12.;
    const double reach = 12. / -forward[1];
    const std::array<double, 3> base = {eye[0] + (reach * forward[0]), ground, eye[2] + (reach * forward[2])};
    const std::array<double, 3> box_min = {base[0] - 1.5, ground, base[2] - 1.5};
    const std::array<double, 3> box_max = {base[0] + 1.5, ground + 8., base[2] + 1.5};
    // The nearest positive distance along `direction` from `origin` into the pillar (slabs), or -1.
    const auto box_hit = [&](const std::array<double, 3>& origin, const std::array<double, 3>& direction) {
      double enter = 0.;
      double leave = 1e30;
      for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-12) {
          if (origin[axis] < box_min[axis] || origin[axis] > box_max[axis]) return -1.;
          continue;
        }
        double near_t = (box_min[axis] - origin[axis]) / direction[axis];
        double far_t = (box_max[axis] - origin[axis]) / direction[axis];
        if (near_t > far_t) std::swap(near_t, far_t);
        enter = std::max(enter, near_t);
        leave = std::min(leave, far_t);
      }
      return (enter <= leave && leave > 0.) ? enter : -1.;
    };
    std::vector<float> depth(SIZE_X * SIZE_Y, 1.f);
    std::vector<int> kind(SIZE_X * SIZE_Y, 0);  // 0 sky, 1 the pillar, 2 ground in the sun, 3 ground in the shadow
    const auto sun_from = [](double x, double z) { return std::array<double, 3>{x, 1., z}; };
    const auto normalized = [](std::array<double, 3> v) {
      const double length = std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
      for (double& value : v) value /= length;
      return v;
    };
    const auto toward_sun = normalized(sun_from(-right[0], -right[2]));  // from the camera's left
    for (UINT y = 0; y < SIZE_Y; ++y) {
      for (UINT x = 0; x < SIZE_X; ++x) {
        const size_t at = (static_cast<size_t>(y) * SIZE_X) + x;
        const double ndc_x = (((x + 0.5) / SIZE_X) * 2.) - 1.;
        const double ndc_y = 1. - (((y + 0.5) / SIZE_Y) * 2.);
        auto near_point = Transform({ndc_x, ndc_y, 0., 1.}, inverse);
        auto far_point = Transform({ndc_x, ndc_y, 0.5, 1.}, inverse);
        std::array<double, 3> direction = {};
        for (int k = 0; k < 3; ++k) {
          near_point[k] /= near_point[3];
          far_point[k] /= far_point[3];
          direction[k] = far_point[k] - near_point[k];
        }
        const std::array<double, 3> origin = {near_point[0], near_point[1], near_point[2]};
        const double pillar = box_hit(origin, direction);
        const double plane = (direction[1] < 0.) ? (ground - origin[1]) / direction[1] : -1.;
        double along = -1.;
        if (pillar > 0. && (plane < 0. || pillar <= plane)) {
          along = pillar;
          kind[at] = 1;
        } else if (plane > 0. && plane < 400.) {
          along = plane;
          const std::array<double, 3> point = {origin[0] + (plane * direction[0]), ground, origin[2] + (plane * direction[2])};
          kind[at] = (box_hit(point, toward_sun) > 0. ? 3 : 2);
        }
        if (along < 0.) continue;
        const auto drawn = Transform({origin[0] + (along * direction[0]), origin[1] + (along * direction[1]), origin[2] + (along * direction[2]), 1.},
                                     view_projection);
        depth[at] = static_cast<float>(drawn[2] / drawn[3]);
      }
    }
    const auto depth_texture = MakeTexture(SIZE_X, SIZE_Y, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, depth.data(), SIZE_X * 4);
    const auto depth_srv = Srv(depth_texture.Get());
    // The red of the mask after the screen-space shadows with the sun toward `sun`.
    const auto shadowed = [&](const std::array<double, 3>& sun) {
      std::array<float, 16> constants = frame_constants;
      for (int axis = 0; axis < 3; ++axis) constants[8 + axis] = static_cast<float>(sun[axis]);
      const auto frame = ConstantBuffer(constants.data(), 64);
      const std::vector<std::array<uint8_t, 2>> mask_bytes(SIZE_X * SIZE_Y, {255, 77});
      const auto mask = MakeTexture(SIZE_X, SIZE_Y, DXGI_FORMAT_R8G8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, mask_bytes.data(), SIZE_X * 2);
      twromeremastered::contact_shadows::internal::Draw(context1.Get(), depth_srv.Get(), battle, frame.Get(), Rtv(mask.Get()).Get());
      context->ClearState();
      return ReadBack<std::array<uint8_t, 2>>(mask.Get());
    };
    // Shares of the shadow's and of the sun's ground darker than half (the sun's ground 3 pixels clear of the shadow).
    const auto shares = [&](const std::vector<std::array<uint8_t, 2>>& red) {
      size_t shadow_count = 0;
      size_t shadow_dark = 0;
      size_t sun_count = 0;
      size_t sun_dark = 0;
      bool green_kept = true;
      for (UINT y = 0; y < SIZE_Y; ++y) {
        for (UINT x = 0; x < SIZE_X; ++x) {
          const size_t at = (static_cast<size_t>(y) * SIZE_X) + x;
          green_kept = green_kept && red[at][1] == 77;
          if (kind[at] == 3) {
            ++shadow_count;
            shadow_dark += (red[at][0] < 128) ? 1 : 0;
          } else if (kind[at] == 2) {
            bool clear = true;
            for (int dy = -3; dy <= 3 && clear; ++dy) {
              for (int dx = -3; dx <= 3 && clear; ++dx) {
                const int sx = std::clamp(static_cast<int>(x) + dx, 0, static_cast<int>(SIZE_X) - 1);
                const int sy = std::clamp(static_cast<int>(y) + dy, 0, static_cast<int>(SIZE_Y) - 1);
                clear = kind[(static_cast<size_t>(sy) * SIZE_X) + sx] != 3 && kind[(static_cast<size_t>(sy) * SIZE_X) + sx] != 1;
              }
            }
            if (clear) {
              ++sun_count;
              sun_dark += (red[at][0] < 128) ? 1 : 0;
            }
          }
        }
      }
      return std::tuple{shadow_count, static_cast<double>(shadow_dark) / std::max<size_t>(shadow_count, 1),
                        static_cast<double>(sun_dark) / std::max<size_t>(sun_count, 1), green_kept};
    };
    // Bend takes each pixel as thin (Thickness x its distance, ~0.3 m here at the default): where the CPU's shadow ray
    // passes deep behind the pillar's front face it stays lit, so the default covers part of the shadow, a thicker row more.
    twromeremastered::contact_shadows::rows = {.enabled = 1.f};
    const auto hard = shadowed(toward_sun);
    const auto [shadow_pixels, shadow_dark, sun_dark, green_kept] = shares(hard);
    const auto [control_pixels, control_dark, control_sun_dark, control_green] = shares(shadowed(normalized(sun_from(right[0], right[2]))));
    twromeremastered::contact_shadows::rows.thickness = 0.05f;
    const auto [thick_pixels, thick_dark, thick_sun_dark, thick_green] = shares(shadowed(toward_sun));
    // Softness 2 px (sss_blur): more pixels between lit and shadowed, the shadow's dark share and the sun's ground kept.
    twromeremastered::contact_shadows::rows = {.enabled = 1.f, .softness = 2.f};
    const auto soft = shadowed(toward_sun);
    const auto [soft_pixels, soft_dark, soft_sun_dark, soft_green] = shares(soft);
    twromeremastered::contact_shadows::rows = {};
    const auto partial = [](const std::vector<std::array<uint8_t, 2>>& red) {
      return std::ranges::count_if(red, [](const std::array<uint8_t, 2>& texel) { return texel[0] > 25 && texel[0] < 230; });
    };
    std::printf("contact shadows: %zu shadow pixels, %.3f of them dark (Thickness 0.05: %.3f; Softness 2: %.3f), %.3f of the sun's ground dark "
                "(%.3f; %.3f), green kept %d; partial pixels %td, Softness 2: %td; CONTROL sun from the right: %.3f of them dark\n",
                shadow_pixels, shadow_dark, thick_dark, soft_dark, sun_dark, thick_sun_dark, soft_sun_dark, green_kept ? 1 : 0, partial(hard),
                partial(soft), control_dark);
    CHECK(shadow_pixels > 50);
    CHECK(shadow_dark > 0.3);
    CHECK(thick_dark > shadow_dark + 0.1);
    CHECK(sun_dark < 0.02 && thick_sun_dark < 0.05 && soft_sun_dark < 0.02);
    CHECK(green_kept && control_green && thick_green && soft_green);
    CHECK(control_dark < 0.05);
    CHECK(partial(soft) > partial(hard));
    CHECK(std::abs(soft_dark - shadow_dark) < 0.15);
  }

  std::printf("%s (%d failures)\n", (failures == 0 ? "PASS" : "FAIL"), failures);
  return (failures == 0 ? 0 : 1);
}
