// Rome Remastered's DLAA host (src/games/twromeremastered/dlaa.hpp) on WARP, through the module's own camera hooks and
// the shaders the addon embeds (build\twromeremastered.include). The cameras are ATN's 2026-11-14 traces' (VS b5, 192
// bytes: view rows 0-3, projection 4-7, view-projection 8-11): a battle's and the campaign's (its projection with Feral's
// constant (-0.5 / 3840, -0.5 / 2160) in row 7).
// 1. The main camera's upload (map / unmap): noted (unjittered view-projection), jittered in place (each row's x / y + its
//    w x the jitter in clip units, the projection's and the view-projection's; view and z / w as they were), the inverse
//    of the jittered view-projection right (x it: the identity).
// 2. Left alone: a shadow cascade's (orthographic), another aspect's, rows 8-11 not rows 0-3 x 4-7, a 256-byte buffer, a
//    read-only map, the row off. UpdateSubresource: the jittered camera uploaded instead (read back), true returned.
// 3. The jitter: 0 unless DLAA ran the frame presented; then Halton (2, 3) over 8 frames, centred, all distinct.
// 4. aa_dlaa_prepare: the depth passed through, the colour squared into BT.2020, the camera's motion from the depth (a
//    ground plane drawn with the jittered camera, the camera moved 2 m since) against the CPU's reprojection (worst 0.05
//    px, mean 0.01); 0 after a reset. aa_dlaa_finish: NVIDIA's output = its input gives the frame back.
// 5. swap_chain_format (no GPU): the game's Renderer::Resize found by its bytes in the installed exe (argv[1], mapped as an
//    image; skipped when it is not there), not in this test's exe (CONTROL); the hook on a function of its kind gives it the
//    game's 8-bit format for the upgraded R16G16B16A16 / R10G10B10A2 (ATN's crash 2026-11-14), the rest as asked, its
//    return value back; unhooked, the format as asked again.
// 6. Upscale (below 100%): DLSS's result copied into the upscaler's display-size target in its place, the target's size
//    noted; CONTROL: no result this frame, another size, DLAA off: the game's draw.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "src/games/twromeremastered/dlaa.hpp"
#include "src/games/twromeremastered/swap_chain_format.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
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

static_assert(sizeof(dlaa::internal::ShaderSettings) == 80, "aa.hlsli aa_settings");
static_assert(sizeof(dlaa::internal::TaaConstants) == 224, "aa.hlsli aa_taa");

// trace_40_021503 (battle, the AO apply's b5) and trace_09_020346 (campaign, the shadow mask resolve's b5).
constexpr Camera BATTLE = {-0.96002036f, -0.052900452f, -0.27488625f, 0, 0, 0.98198146f, -0.18897732f, 0, 0.27993017f, -0.18142207f,
                           -0.9427222f, 0, -390.28714f, -297.02258f, 43.657784f, 1, 1.0805525f, 0, 0, 0, 0, 1.9209821f, 0, 0, -0.f,
                           -0.f, 1.00004f, 1, 0, 0, -1.00004f, 0, -1.0373523f, -0.10162082f, -0.27489725f, -0.27488625f, 0,
                           1.8863689f, -0.18898489f, -0.18897732f, 0.30247924f, -0.34850857f, -0.94276f, -0.9427222f, -421.72574f,
                           -570.5751f, 42.659492f, 43.657784f};
constexpr Camera CAMPAIGN = {1, 0, 0, 0, 0, 0.72534674f, -0.68838376f, 0, -0.f, 0.68838376f, 0.72534674f, 0, -93.18882f, -50.826363f,
                             -25.869785f, 1, 1.357995f, 0, 0, 0, 0, 2.4142134f, 0, 0, 0, 0, 1.037037f, 1, -0.00013020834f,
                             -0.00023148148f, -3.703703f, 0, 1.357995f, 0, 0, 0, 0, 1.7511418f, -0.71387947f, -0.68838376f, 0,
                             1.6619053f, 0.75221145f, 0.72534674f, -126.55009f, -122.70592f, -30.531628f, -25.869785f};

using Matrix = std::array<double, 16>;
static Matrix Multiply(const double* a, const double* b) {
  Matrix result = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      for (int k = 0; k < 4; ++k) result[(row * 4) + column] += a[(row * 4) + k] * b[(k * 4) + column];
    }
  }
  return result;
}
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
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static ComPtr<ID3D11Buffer> MakeBuffer(UINT bytes, D3D11_USAGE usage) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = usage, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                  .CPUAccessFlags = (usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0u)};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf())));
  return buffer;
}

// An upload through the map / unmap hooks (the data the game wrote: `camera`, changed in place by the module).
static void Upload(ID3D11Buffer* buffer, Camera* camera, reshade::api::map_access access = reshade::api::map_access::write_discard) {
  void* data = camera->data();
  const reshade::api::resource resource = {reinterpret_cast<uint64_t>(buffer)};
  dlaa::internal::OnMapBufferRegion(&mock_device, resource, 0, UINT64_MAX, access, &data);
  dlaa::internal::OnUnmapBufferRegion(&mock_device, resource);
}

static ComPtr<ID3D11Texture2D> MakeTexture(UINT width, UINT height, DXGI_FORMAT format, UINT bind, const void* data = nullptr, UINT pitch = 0) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = pitch};
  ComPtr<ID3D11Texture2D> texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, (data != nullptr ? &initial : nullptr), texture.GetAddressOf())));
  return texture;
}

template <typename T>
static std::vector<T> ReadBack(ID3D11Texture2D* texture, UINT width, UINT height) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
  context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<T> values(static_cast<size_t>(width) * height);
  for (UINT row = 0; row < height; ++row) std::memcpy(&values[static_cast<size_t>(row) * width], static_cast<const std::byte*>(mapped.pData) + (row * mapped.RowPitch), width * sizeof(T));
  context->Unmap(staging.Get(), 0);
  return values;
}

static float Half(uint16_t value) {
  const uint32_t sign = (value & 0x8000u) << 16;
  const uint32_t exponent = (value >> 10) & 0x1Fu;
  const uint32_t mantissa = value & 0x3FFu;
  float result = 0.f;
  if (exponent == 0) {
    result = std::ldexp(static_cast<float>(mantissa), -24);
  } else {
    result = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
  }
  return (sign != 0 ? -result : result);
}
static uint16_t ToHalf(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, 4);
  const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
  const int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127 + 15;
  if (exponent <= 0) return sign;
  return static_cast<uint16_t>(sign | (exponent << 10) | ((bits >> 13) & 0x3FFu));
}

// A function of the game's Renderer::Resize kind for the hook (the formats it was given kept).
static std::vector<uint32_t> resized_formats;
__declspec(noinline) static uint64_t __fastcall TestResize(void*, uint32_t, uint32_t, uint32_t format, uint32_t, uint32_t) {
  resized_formats.push_back(format);
  return 7;
}

int main(int argc, char** argv) {
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                      context.GetAddressOf())));
  mock_device.native = device;
  auto& state = dlaa::internal::state;
  constexpr UINT WIDTH = 64;
  constexpr UINT HEIGHT = 36;
  state.display = {WIDTH, HEIGHT};
  dlaa::setting = 1.f;
  const std::array<float, 2> jitter = {0.3f, -0.2f};
  state.jitter = jitter;
  const auto camera_buffer = MakeBuffer(192, D3D11_USAGE_DYNAMIC);

  // 1. The main camera, battle and campaign.
  for (const auto& [name, original] : std::array<std::pair<const char*, Camera>, 2>{{{"battle", BATTLE}, {"campaign", CAMPAIGN}}}) {
    Camera upload = original;
    state.camera = {};
    Upload(camera_buffer.Get(), &upload);
    CHECK(state.camera.frame == state.frame);
    CHECK(std::equal(state.camera.view_projection.begin(), state.camera.view_projection.end(), original.begin() + 32));
    CHECK(state.camera.jitter == jitter);
    const float jitter_x = 2.f * jitter[0] / WIDTH;
    const float jitter_y = 2.f * jitter[1] / HEIGHT;
    CHECK(std::equal(upload.begin(), upload.begin() + 16, original.begin()));
    for (size_t matrix : {size_t{16}, size_t{32}}) {
      for (size_t row = 0; row < 4; ++row) {
        const size_t at = matrix + (row * 4);
        CHECK(std::abs(upload[at] - (original[at] + (jitter_x * original[at + 3]))) <= 1e-6f * (1.f + std::abs(original[at])));
        CHECK(std::abs(upload[at + 1] - (original[at + 1] + (jitter_y * original[at + 3]))) <= 1e-6f * (1.f + std::abs(original[at + 1])));
        CHECK(original[at + 3] == 0.f || upload[at] != original[at] || original[at] == 0.f);
        CHECK(upload[at + 2] == original[at + 2] && upload[at + 3] == original[at + 3]);
      }
    }
    const Matrix identity = Multiply(Rows(state.camera.inverse_view_projection.data()).data(), Rows(upload.data() + 32).data());
    double worst = 0.;
    for (int index = 0; index < 16; ++index) worst = std::max(worst, std::abs(identity[index] - ((index % 5 == 0) ? 1. : 0.)));
    std::printf("camera %s: inverse x jittered, worst %.2e off the identity\n", name, worst);
    CHECK(worst < 1e-4);
  }

  // 2. Left alone.
  {
    const auto unchanged = [&](Camera upload, const char* what) {
      const Camera before = upload;
      state.camera = {};
      Upload(camera_buffer.Get(), &upload);
      const bool left = upload == before && state.camera.frame == 0;
      if (!left) std::printf("not left alone: %s\n", what);
      CHECK(left);
    };
    Camera cascade = BATTLE;  // an orthographic light camera: its projection's last column 0, 0, 0, 1
    const std::array<float, 16> ortho = {0.01f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0, 0.001f, 0, 0, 0, 0.5f, 1};
    std::copy(ortho.begin(), ortho.end(), cascade.begin() + 16);
    const Matrix cascade_vp = Multiply(Rows(cascade.data()).data(), Rows(ortho.data()).data());
    for (int index = 0; index < 16; ++index) cascade[32 + index] = static_cast<float>(cascade_vp[index]);
    unchanged(cascade, "a shadow cascade");
    Camera square = BATTLE;  // another aspect (a 1:1 view)
    square[16] = square[21];
    const Matrix square_vp = Multiply(Rows(square.data()).data(), Rows(square.data() + 16).data());
    for (int index = 0; index < 16; ++index) square[32 + index] = static_cast<float>(square_vp[index]);
    unchanged(square, "another aspect");
    Camera mismatched = BATTLE;
    mismatched[44] += 5.f;
    unchanged(mismatched, "rows 8-11 not rows 0-3 x 4-7");
    {
      const auto large = MakeBuffer(256, D3D11_USAGE_DYNAMIC);
      Camera upload = BATTLE;
      state.camera = {};
      Upload(large.Get(), &upload);
      CHECK(upload == BATTLE && state.camera.frame == 0);
      Upload(camera_buffer.Get(), &upload, reshade::api::map_access::read_only);
      CHECK(upload == BATTLE && state.camera.frame == 0);
      dlaa::setting = 0.f;
      Upload(camera_buffer.Get(), &upload);
      CHECK(upload == BATTLE && state.camera.frame == 0);
      dlaa::setting = 1.f;
    }
    // UpdateSubresource: the jittered camera instead.
    const auto default_buffer = MakeBuffer(192, D3D11_USAGE_DEFAULT);
    const reshade::api::resource resource = {reinterpret_cast<uint64_t>(default_buffer.Get())};
    CHECK(dlaa::internal::OnUpdateBufferRegion(&mock_device, BATTLE.data(), resource, 0, 192));
    Camera expected = BATTLE;
    Upload(camera_buffer.Get(), &expected);
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = 192, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> staging;
    REQUIRE(SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf())));
    context->CopyResource(staging.Get(), default_buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    CHECK(std::memcmp(mapped.pData, expected.data(), 192) == 0);
    context->Unmap(staging.Get(), 0);
    CHECK(!dlaa::internal::OnUpdateBufferRegion(&mock_device, cascade.data(), resource, 0, 192));
  }

  // 3. The jitter.
  {
    state.evaluated_frame = 0;
    dlaa::internal::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    CHECK(state.jitter[0] == 0.f && state.jitter[1] == 0.f);
    std::vector<std::array<float, 2>> seen;
    for (int frame = 0; frame < 8; ++frame) {
      state.evaluated_frame = state.frame;
      dlaa::internal::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
      CHECK(std::abs(state.jitter[0]) < 0.5f && std::abs(state.jitter[1]) < 0.5f);
      CHECK(std::ranges::find(seen, state.jitter) == seen.end());
      seen.push_back(state.jitter);
    }
    dlaa::setting = 0.f;
    state.evaluated_frame = state.frame;
    dlaa::internal::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    CHECK(state.jitter[0] == 0.f && state.jitter[1] == 0.f);
    dlaa::setting = 1.f;
  }

  // 4. The shaders: a ground plane 30 m below the battle camera, drawn with the jittered camera; the camera 2 m away the
  // frame before.
  state.jitter = jitter;
  Camera upload = BATTLE;
  Upload(camera_buffer.Get(), &upload);
  REQUIRE(state.camera.frame == state.frame);
  const Matrix jittered = Rows(upload.data() + 32);
  const Matrix inverse_jittered = Inverse(jittered);
  const Matrix view_projection = Rows(BATTLE.data() + 32);
  const Matrix view = Rows(BATTLE.data());
  const Matrix inverse_view = Inverse(view);
  const std::array<double, 3> eye = {inverse_view[12], inverse_view[13], inverse_view[14]};
  Matrix moved_view = view;
  const std::array<double, 3> step = {2., 0., 1.};
  for (int column = 0; column < 3; ++column) {
    for (int k = 0; k < 3; ++k) moved_view[12 + column] -= step[k] * view[(k * 4) + column];
  }
  const Matrix previous = Multiply(moved_view.data(), Rows(BATTLE.data() + 16).data());
  const double ground = eye[1] - 30.;
  std::vector<float> depth(WIDTH * HEIGHT);
  std::vector<std::array<double, 2>> reference(WIDTH * HEIGHT);
  std::vector<std::array<uint16_t, 4>> frame(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const size_t at = (static_cast<size_t>(y) * WIDTH) + x;
      const double ndc_x = (((x + 0.5) / WIDTH) * 2.) - 1.;
      const double ndc_y = 1. - (((y + 0.5) / HEIGHT) * 2.);
      auto near_point = Transform({ndc_x, ndc_y, 0., 1.}, inverse_jittered);
      auto far_point = Transform({ndc_x, ndc_y, 0.5, 1.}, inverse_jittered);
      std::array<double, 4> world = {};
      for (int k = 0; k < 3; ++k) {
        near_point[k] /= near_point[3];
        far_point[k] /= far_point[3];
      }
      // The ground, or (above the horizon, or beyond 1.5 km: the game's far plane is 25 km) a point 1.5 km along the ray.
      const double ray = std::sqrt(((far_point[0] - near_point[0]) * (far_point[0] - near_point[0])) + ((far_point[1] - near_point[1]) * (far_point[1] - near_point[1]))
                                   + ((far_point[2] - near_point[2]) * (far_point[2] - near_point[2])));
      double along = (ground - near_point[1]) / (far_point[1] - near_point[1]);
      if (along <= 0. || along * ray > 1500.) along = 1500. / ray;
      for (int k = 0; k < 3; ++k) world[k] = near_point[k] + (along * (far_point[k] - near_point[k]));
      world[3] = 1.;
      const auto drawn = Transform(world, jittered);
      depth[at] = static_cast<float>(drawn[2] / drawn[3]);
      const auto now = Transform(world, view_projection);
      const auto before = Transform(world, previous);
      reference[at] = {((before[0] / before[3]) - (now[0] / now[3])) * 0.5 * WIDTH, ((before[1] / before[3]) - (now[1] / now[3])) * -0.5 * HEIGHT};
      frame[at] = {ToHalf(0.1f + (0.01f * x)), ToHalf(0.5f - (0.01f * y)), ToHalf(0.25f), ToHalf(0.75f)};
    }
  }
  const auto frame_texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, frame.data(), WIDTH * 8);
  const auto depth_texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, depth.data(), WIDTH * 4);
  const auto color_out = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
  const auto depth_out = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
  const auto motion_out = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
  const auto result = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
  const auto srv = [](ID3D11Texture2D* texture) {
    ComPtr<ID3D11ShaderResourceView> view;
    REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, view.GetAddressOf())));
    return view;
  };
  const auto uav = [](ID3D11Texture2D* texture) {
    ComPtr<ID3D11UnorderedAccessView> view;
    REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(texture, nullptr, view.GetAddressOf())));
    return view;
  };
  ComPtr<ID3D11ComputeShader> prepare;
  ComPtr<ID3D11ComputeShader> finish;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(__aa_dlaa_prepare.data(), __aa_dlaa_prepare.size(), nullptr, prepare.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateComputeShader(__aa_dlaa_finish.data(), __aa_dlaa_finish.size(), nullptr, finish.GetAddressOf())));
  const auto settings_buffer = MakeBuffer(sizeof(dlaa::internal::ShaderSettings), D3D11_USAGE_DEFAULT);
  const auto taa_buffer = MakeBuffer(sizeof(dlaa::internal::TaaConstants), D3D11_USAGE_DEFAULT);
  const dlaa::internal::ShaderSettings shader_settings = {
      .metrics = {1.f / WIDTH, 1.f / HEIGHT, static_cast<float>(WIDTH), static_cast<float>(HEIGHT)},
      .dlaa_debug = {0.f, static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 0.f},
  };
  context->UpdateSubresource(settings_buffer.Get(), 0, nullptr, &shader_settings, 0, 0);
  const auto frame_srv = srv(frame_texture.Get());
  const auto depth_srv = srv(depth_texture.Get());
  std::array<float, 16> previous_rows = {};
  for (int index = 0; index < 16; ++index) previous_rows[index] = static_cast<float>(previous[index]);
  const auto run_prepare = [&](bool reset) {
    const dlaa::internal::TaaConstants taa = dlaa::internal::MakeTaaConstants(state.camera, previous_rows, !reset);
    context->UpdateSubresource(taa_buffer.Get(), 0, nullptr, &taa, 0, 0);
    const std::array<ID3D11Buffer*, 2> constants = {settings_buffer.Get(), taa_buffer.Get()};
    context->CSSetConstantBuffers(0, 2, constants.data());
    const std::array<ID3D11ShaderResourceView*, 2> views = {frame_srv.Get(), depth_srv.Get()};
    const auto color_uav = uav(color_out.Get());
    const auto depth_uav = uav(depth_out.Get());
    const auto motion_uav = uav(motion_out.Get());
    const std::array<ID3D11UnorderedAccessView*, 3> uavs = {color_uav.Get(), depth_uav.Get(), motion_uav.Get()};
    context->CSSetShader(prepare.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, views.data());
    context->CSSetUnorderedAccessViews(0, 3, uavs.data(), nullptr);
    context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    const std::array<ID3D11UnorderedAccessView*, 3> none = {};
    context->CSSetUnorderedAccessViews(0, 3, none.data(), nullptr);
  };
  run_prepare(false);
  const auto depth_back = ReadBack<float>(depth_out.Get(), WIDTH, HEIGHT);
  CHECK(depth_back == depth);
  const auto motion = ReadBack<std::array<float, 2>>(motion_out.Get(), WIDTH, HEIGHT);
  double worst = 0.;
  double total = 0.;
  double largest = 0.;
  for (size_t at = 0; at < motion.size(); ++at) {
    const double error = std::hypot(motion[at][0] - reference[at][0], motion[at][1] - reference[at][1]);
    worst = std::max(worst, error);
    total += error;
    largest = std::max(largest, std::hypot(reference[at][0], reference[at][1]));
  }
  std::printf("camera motion: up to %.2f px, against the CPU worst %.4f px, mean %.5f px\n", largest, worst, total / motion.size());
  CHECK(largest > 0.5);
  CHECK(worst < 0.05);
  CHECK(total / motion.size() < 0.01);
  const auto color = ReadBack<std::array<uint16_t, 4>>(color_out.Get(), WIDTH, HEIGHT);
  double colour_worst = 0.;
  for (size_t at = 0; at < color.size(); at += 7) {
    const std::array<double, 3> encoded = {Half(frame[at][0]), Half(frame[at][1]), Half(frame[at][2])};
    const std::array<double, 3> linear = {encoded[0] * encoded[0], encoded[1] * encoded[1], encoded[2] * encoded[2]};
    const std::array<double, 3> bt2020 = {(0.6274040 * linear[0]) + (0.3292820 * linear[1]) + (0.0433136 * linear[2]),
                                          (0.0690970 * linear[0]) + (0.9195400 * linear[1]) + (0.0113612 * linear[2]),
                                          (0.0163916 * linear[0]) + (0.0880132 * linear[1]) + (0.8955950 * linear[2])};
    for (int channel = 0; channel < 3; ++channel) {
      colour_worst = std::max(colour_worst, std::abs(Half(color[at][channel]) - bt2020[channel]) / std::max(bt2020[channel], 1e-3));
    }
    CHECK(color[at][3] == frame[at][3]);
  }
  std::printf("colour for NVIDIA: worst %.2e relative\n", colour_worst);
  CHECK(colour_worst < 2e-3);

  // NVIDIA's output = its input: the frame back.
  {
    const auto color_srv = srv(color_out.Get());
    const auto motion_srv = srv(motion_out.Get());
    const auto result_uav = uav(result.Get());
    const std::array<ID3D11ShaderResourceView*, 3> views = {color_srv.Get(), frame_srv.Get(), motion_srv.Get()};
    context->CSSetShader(finish.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 3, views.data());
    context->CSSetUnorderedAccessViews(0, 1, result_uav.GetAddressOf(), nullptr);
    context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    const std::array<ID3D11ShaderResourceView*, 3> none = {};
    context->CSSetShaderResources(0, 3, none.data());
    ID3D11UnorderedAccessView* no_uav = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
    const auto back = ReadBack<std::array<uint16_t, 4>>(result.Get(), WIDTH, HEIGHT);
    double round_trip = 0.;
    for (size_t at = 0; at < back.size(); ++at) {
      for (int channel = 0; channel < 4; ++channel) {
        round_trip = std::max(round_trip, static_cast<double>(std::abs(Half(back[at][channel]) - Half(frame[at][channel]))));
      }
    }
    std::printf("finish(prepare(frame)): worst %.2e off the frame\n", round_trip);
    CHECK(round_trip < 4e-3);
  }

  run_prepare(true);
  const auto reset_motion = ReadBack<std::array<float, 2>>(motion_out.Get(), WIDTH, HEIGHT);
  CHECK(std::ranges::all_of(reset_motion, [](const std::array<float, 2>& value) { return value[0] == 0.f && value[1] == 0.f; }));

  // 5. swap_chain_format.
  {
    namespace format = twromeremastered::swap_chain_format;
    if (argc >= 2 && GetFileAttributesA(argv[1]) != INVALID_FILE_ATTRIBUTES) {
      HMODULE image = LoadLibraryExA(argv[1], nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
      REQUIRE(image != nullptr);
      auto* base = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(image) & ~uintptr_t{3});
      const uint8_t* found = format::internal::Find(reinterpret_cast<HMODULE>(base));
      std::printf("swap chain format: the game's resize at exe + %#llx\n", (found != nullptr ? static_cast<unsigned long long>(found - base) : 0ull));
      CHECK(found == base + format::internal::RESIZE_RVA);
      FreeLibrary(image);
    } else {
      std::printf("swap chain format: the game's exe not given or not there: its bytes not checked\n");
    }
    CHECK(format::internal::Find(GetModuleHandleW(nullptr)) == nullptr);  // CONTROL: not this exe
    REQUIRE(format::internal::Hook(reinterpret_cast<void*>(&TestResize)));
    auto* volatile resize = &TestResize;
    std::vector<uint64_t> returned;
    for (const uint32_t asked : {87u, 10u, 28u, 24u, 0u, 41u}) returned.push_back(resize(nullptr, 2572, 1446, asked, 0, 1));
    const std::vector<uint32_t> hooked_formats = resized_formats;
    format::Uninstall();
    resize(nullptr, 2572, 1446, 10u, 0, 1);
    std::printf("swap chain format: asked 87 10 28 24 0 41, given %u %u %u %u %u %u; unhooked 10, given %u\n", hooked_formats[0], hooked_formats[1],
                hooked_formats[2], hooked_formats[3], hooked_formats[4], hooked_formats[5], resized_formats.back());
    CHECK((hooked_formats == std::vector<uint32_t>{87u, 87u, 28u, 28u, 0u, 41u}));
    CHECK(std::ranges::all_of(returned, [](uint64_t value) { return value == 7; }));
    CHECK(resized_formats.size() == 7 && resized_formats.back() == 10u);
  }

  // 6. Upscale (below 100%, ATN's 67% / 50% traces: the upscaler's FP16 target at the display's size): DLSS's result this
  // frame copied into the target in the upscaler's place (true: its draw skipped), the target's size noted; CONTROL: no
  // result this frame, another size, DLAA off: the game's draw.
  {
    mock::CommandList upscaler_list;
    upscaler_list.owner = &mock_device;
    upscaler_list.native = context;
    constexpr UINT DISPLAY_WIDTH = WIDTH * 3 / 2;
    constexpr UINT DISPLAY_HEIGHT = HEIGHT * 3 / 2;
    std::vector<std::array<uint16_t, 4>> upscaled(DISPLAY_WIDTH * DISPLAY_HEIGHT);
    for (size_t at = 0; at < upscaled.size(); ++at) upscaled[at] = {ToHalf(0.001f * static_cast<float>(at % 997)), ToHalf(0.5f), ToHalf(0.25f), ToHalf(1.f)};
    dlaa::internal::objects.result = MakeTexture(DISPLAY_WIDTH, DISPLAY_HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                 D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, upscaled.data(), DISPLAY_WIDTH * 8);
    const auto make_target = [&](UINT width, UINT height) {
      auto texture = MakeTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
      ComPtr<ID3D11RenderTargetView> view;
      REQUIRE(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, view.GetAddressOf())));
      constexpr std::array<float, 4> GREY = {0.125f, 0.125f, 0.125f, 1.f};
      context->ClearRenderTargetView(view.Get(), GREY.data());
      return std::pair{texture, view};
    };
    const auto upscale = [&](ID3D11RenderTargetView* view) {
      context->OMSetRenderTargets(1, &view, nullptr);
      const bool skipped = dlaa::Upscale(&upscaler_list);
      context->OMSetRenderTargets(0, nullptr, nullptr);
      return skipped;
    };
    const uint16_t grey = ToHalf(0.125f);
    dlaa::setting = 1.f;
    state.upscaled_frame = 0;
    const auto [unused_target, unused_view] = make_target(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    CHECK(!upscale(unused_view.Get()));  // CONTROL: no result this frame
    CHECK((ReadBack<std::array<uint16_t, 4>>(unused_target.Get(), DISPLAY_WIDTH, DISPLAY_HEIGHT)[5][0] == grey));
    CHECK(state.upscaler[0] == DISPLAY_WIDTH && state.upscaler[1] == DISPLAY_HEIGHT && state.upscaler_frame == state.frame);
    state.upscaled_frame = state.frame;
    const auto [target, view] = make_target(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    CHECK(upscale(view.Get()));
    CHECK((ReadBack<std::array<uint16_t, 4>>(target.Get(), DISPLAY_WIDTH, DISPLAY_HEIGHT) == upscaled));
    const auto [small_target, small_view] = make_target(WIDTH, HEIGHT);
    CHECK(!upscale(small_view.Get()));  // CONTROL: another size
    CHECK((ReadBack<std::array<uint16_t, 4>>(small_target.Get(), WIDTH, HEIGHT)[5][0] == grey));
    dlaa::setting = 0.f;
    const auto [off_target, off_view] = make_target(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    CHECK(!upscale(off_view.Get()));  // CONTROL: DLAA off
    CHECK((ReadBack<std::array<uint16_t, 4>>(off_target.Get(), DISPLAY_WIDTH, DISPLAY_HEIGHT)[5][0] == grey));
    std::printf("upscale: DLSS's result in the upscaler's %ux%u target, the game's draw skipped; no result / another size / off: its draw\n",
                DISPLAY_WIDTH, DISPLAY_HEIGHT);
    dlaa::internal::objects = {};
  }

  std::printf("%s (%d failures)\n", (failures == 0 ? "PASS" : "FAIL"), failures);
  return (failures == 0 ? 0 : 1);
}
