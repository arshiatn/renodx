// Divinity 2's helper route on WARP (src/games/divinity2originalsindefinitive/dlaa.hpp, ray_tracing.hpp), through the
// module's own hooks and the shaders the addon embeds (build\divinity2originalsindefinitive.include). The camera is R1b's
// PerView b12 (run_r1b_235909, the Arx city frame: View c0-3, Projection c4-7, ViewProjection c8-11 as rows acting on
// column vectors, ViewPos c12, ViewInfo c13 = far 100, near 1, then the size: here 64 x 36, the same 16:9 aspect).
// 1. The main camera's upload (map / unmap): noted (unjittered rows, ViewPos as the eye, near / far), jittered in place
//    (rows 0 and 1 of the projection and view-projection + row 3 x the jitter in clip units; everything else as it was),
//    the inverse of the jittered view-projection right (a point back from its jittered clip position; CONTROL: the
//    unjittered inverse misses it).
// 2. Left alone: an orthographic view, another aspect, a size not the display's, rows 8-11 not rows 4-7 x 0-3, a 192-byte
//    buffer, a read-only map, every row off. A 256-byte buffer is PerView too. UpdateSubresource: the jittered camera
//    uploaded instead (its last 16 bytes the game's), true returned; CONTROL: an orthographic one, false.
// 3. The jitter: 0 unless DLAA ran the frame presented; then Halton (2, 3) over 8 frames, centred, all distinct.
// 4. aa_dlaa_prepare / finish (TW_AA_FRAME_LINEAR: PPApplyLUT's input is the linear scene): a ground 15 m below the eye
//    drawn with the jittered camera, the camera 0.3 m away the frame before: the camera's motion against the CPU's
//    reprojection; the colour into BT.2020 as it is (CONTROL: not its square, Attila's 2.0 frame); finish(prepare) gives
//    the frame back.
// 5. The game's anti-aliasing under DLAA: SMAA's weights not drawn for the DLAA'd picture's size (CONTROL: another size,
//    the row off, no DLAA this frame); FXAA's input copied into its target bit for bit (CONTROL: another format: the
//    game's draw, its target untouched).
// 6. Ray tracing's writes: the traced visibility into the sun mask (R8, exact) and the AO texture (RGB10A2, within half a
//    10-bit step); the game's own PPApplyAO (0xF3A764F3, the census's bytes, argv[1]) reading that AO texture gives
//    1 - visibility in alpha (CONTROL: the texture before the write gives the game's AO); rt_frame_sun takes PerFrame's
//    column-major row 0 (CONTROL: its registers' first three floats point elsewhere).
// 7. The overlays (decals, the ground's surfaces, water) left out of the rays' scene by their shipped pixel shaders'
//    bytes (CONTROL: an opaque material's shader).
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
#include <utility>
#include <vector>
#include "src/games/divinity2originalsindefinitive/dlaa.hpp"
#include "src/games/divinity2originalsindefinitive/ray_tracing.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

// utils/settings.hpp's (the graphics page's modules through dlaa.hpp)
extern "C" __declspec(dllexport) const char* const NAME = "Divinity 2 helper test";

using Microsoft::WRL::ComPtr;
namespace dlaa = divinity2::dlaa;
namespace rays = divinity2::ray_tracing;
using PerView = std::array<float, 60>;

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

constexpr UINT WIDTH = 64;
constexpr UINT HEIGHT = 36;

// R1b's PerView at EoCSight_HDR (traces/scene_hdr.txt line 3180), ViewInfo's size 64 x 36.
constexpr PerView CITY = {-0.896398544f, 0, 0.443248898f, 145.088867f, -0.31779936f, 0.697096646f, -0.642697334f, 256.272064f,
                          -0.308987319f, -0.716977239f, -0.62487644f, 279.228577f, 0, 0, 0, 1,
                          1.35799503f, 0, 0, 0, 0, 2.41421342f, 0, 0, 0, 0, 1.01010096f, -1.01010096f, 0, 0, 1, 0,
                          -1.21730483f, 0, 0.601929784f, 197.029968f, -0.767235458f, 1.68294013f, -1.55160856f, 618.695435f,
                          -0.312108397f, -0.724219382f, -0.631188273f, 281.03894f, -0.308987319f, -0.716977239f, -0.62487644f, 279.228577f,
                          297.778656f, 21.5541191f, 274.878235f, 2, 100, 1, static_cast<float>(WIDTH), static_cast<float>(HEIGHT),
                          4.66596508f, 1, 30, 17};

using Matrix = std::array<double, 16>;
static Matrix Rows(const float* values) {
  Matrix result = {};
  for (int index = 0; index < 16; ++index) result[index] = values[index];
  return result;
}
static Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix result = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      for (int k = 0; k < 4; ++k) result[(row * 4) + column] += a[(row * 4) + k] * b[(k * 4) + column];
    }
  }
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
// Rows acting on a column vector (the game's dot products).
static std::array<double, 4> Apply(const Matrix& m, const std::array<double, 4>& v) {
  std::array<double, 4> result = {};
  for (int row = 0; row < 4; ++row) {
    for (int k = 0; k < 4; ++k) result[row] += m[(row * 4) + k] * v[k];
  }
  return result;
}

static mock::Device mock_device;
static mock::CommandList mock_list;
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
static void Upload(ID3D11Buffer* buffer, PerView* camera, reshade::api::map_access access = reshade::api::map_access::write_discard) {
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
  for (UINT row = 0; row < height; ++row) {
    std::memcpy(&values[static_cast<size_t>(row) * width], static_cast<const std::byte*>(mapped.pData) + (row * mapped.RowPitch), width * sizeof(T));
  }
  context->Unmap(staging.Get(), 0);
  return values;
}

static float Half(uint16_t value) {
  const uint32_t sign = (value & 0x8000u) << 16;
  const uint32_t exponent = (value >> 10) & 0x1Fu;
  const uint32_t mantissa = value & 0x3FFu;
  const float result = (exponent == 0) ? std::ldexp(static_cast<float>(mantissa), -24)
                                       : std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
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

static ComPtr<ID3D11ShaderResourceView> Srv(ID3D11Texture2D* texture) {
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, view.GetAddressOf())));
  return view;
}
static ComPtr<ID3D11UnorderedAccessView> Uav(ID3D11Texture2D* texture) {
  ComPtr<ID3D11UnorderedAccessView> view;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(texture, nullptr, view.GetAddressOf())));
  return view;
}
static ComPtr<ID3D11RenderTargetView> Rtv(ID3D11Texture2D* texture) {
  ComPtr<ID3D11RenderTargetView> view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(texture, nullptr, view.GetAddressOf())));
  return view;
}

int main(int argc, char** argv) {
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                      context.GetAddressOf())));
  mock_device.native = device;
  mock_list.owner = &mock_device;
  mock_list.native = context;
  auto& state = dlaa::internal::state;
  state.display = {WIDTH, HEIGHT};
  dlaa::sliders.method = 5.f;
  const std::array<float, 2> jitter = {0.3f, -0.2f};
  state.jitter = jitter;
  const auto camera_buffer = MakeBuffer(240, D3D11_USAGE_DYNAMIC);

  // 1. The main camera.
  {
    PerView upload = CITY;
    state.camera = {};
    Upload(camera_buffer.Get(), &upload);
    CHECK(state.camera.frame == state.frame);
    CHECK(std::equal(state.camera.view_projection.begin(), state.camera.view_projection.end(), CITY.begin() + 32));
    CHECK(std::equal(state.camera.view.begin(), state.camera.view.end(), CITY.begin()));
    CHECK((state.camera.position == std::array<float, 3>{CITY[48], CITY[49], CITY[50]}));
    CHECK((state.camera.planes == std::array<float, 2>{1.f, 100.f}));
    CHECK((state.camera.size == std::array<UINT, 2>{WIDTH, HEIGHT}));
    CHECK(state.camera.jitter == jitter);
    const float jitter_x = 2.f * jitter[0] / WIDTH;
    const float jitter_y = 2.f * jitter[1] / HEIGHT;
    CHECK(std::equal(upload.begin(), upload.begin() + 16, CITY.begin()));
    CHECK(std::equal(upload.begin() + 48, upload.end(), CITY.begin() + 48));
    for (size_t matrix : {size_t{16}, size_t{32}}) {
      for (size_t column = 0; column < 4; ++column) {
        const float w = CITY[matrix + 12 + column];
        CHECK(std::abs(upload[matrix + column] - (CITY[matrix + column] + (jitter_x * w))) <= 1e-6f * (1.f + std::abs(CITY[matrix + column])));
        CHECK(std::abs(upload[matrix + 4 + column] - (CITY[matrix + 4 + column] + (jitter_y * w))) <= 1e-6f * (1.f + std::abs(CITY[matrix + 4 + column])));
        CHECK(upload[matrix + 8 + column] == CITY[matrix + 8 + column] && upload[matrix + 12 + column] == CITY[matrix + 12 + column]);
      }
    }
    // A point's clip position moves by the jitter (x right, y up) in pixels after the division by w.
    const std::array<double, 4> point = {300., 10., 262., 1.};
    const auto before = Apply(Rows(CITY.data() + 32), point);
    const auto after = Apply(Rows(upload.data() + 32), point);
    const double moved_x = ((after[0] / after[3]) - (before[0] / before[3])) * 0.5 * WIDTH;
    const double moved_y = ((after[1] / after[3]) - (before[1] / before[3])) * 0.5 * HEIGHT;
    std::printf("camera: a point moved %.4f, %.4f px by the jitter %.2f, %.2f\n", moved_x, moved_y, jitter[0], jitter[1]);
    CHECK(std::abs(moved_x - jitter[0]) < 1e-3 && std::abs(moved_y - jitter[1]) < 1e-3);
    // The point back from its jittered clip position through the kept inverse (floats, as the shaders and the helper read
    // it); CONTROL: the unjittered view-projection's inverse misses it by the jitter's footprint.
    const auto recovered_by = [&](const Matrix& inverse) {
      const auto world = Apply(inverse, after);
      double error = 0.;
      for (int k = 0; k < 3; ++k) error = std::max(error, std::abs((world[k] / world[3]) - point[k]));
      return error;
    };
    const double recovered = recovered_by(Rows(state.camera.inverse_view_projection.data()));
    const double unjittered = recovered_by(Inverse(Rows(CITY.data() + 32)));
    std::printf("camera: the point back through the kept inverse within %.2e m; CONTROL the unjittered inverse: %.3f m off\n", recovered,
                unjittered);
    CHECK(recovered < 1e-3);
    CHECK(unjittered > 1e-2);
  }

  // 2. Left alone.
  PerView orthographic = CITY;  // a shadow cascade's: its projection's row 3 is 0, 0, 0, 1
  {
    const auto unchanged = [&](PerView upload, const char* what) {
      const PerView before = upload;
      state.camera = {};
      Upload(camera_buffer.Get(), &upload);
      const bool left = upload == before && state.camera.frame == 0;
      if (!left) std::printf("not left alone: %s\n", what);
      CHECK(left);
    };
    const std::array<float, 16> ortho = {0.02f, 0, 0, 0, 0, 0.02f, 0, 0, 0, 0, 0.01f, 0.5f, 0, 0, 0, 1};
    std::copy(ortho.begin(), ortho.end(), orthographic.begin() + 16);
    const Matrix ortho_vp = Multiply(Rows(ortho.data()), Rows(CITY.data()));
    for (int index = 0; index < 16; ++index) orthographic[32 + index] = static_cast<float>(ortho_vp[index]);
    unchanged(orthographic, "an orthographic view");
    PerView square = CITY;  // another aspect (a 1:1 view, a light probe's face)
    square[16] = square[21];
    const Matrix square_vp = Multiply(Rows(square.data() + 16), Rows(square.data()));
    for (int index = 0; index < 16; ++index) square[32 + index] = static_cast<float>(square_vp[index]);
    unchanged(square, "another aspect");
    PerView portrait = CITY;  // the inventory's portrait: 610 x 900 (its own aspect too)
    portrait[54] = 610.f;
    portrait[55] = 900.f;
    portrait[16] = portrait[21] * 900.f / 610.f;
    const Matrix portrait_vp = Multiply(Rows(portrait.data() + 16), Rows(portrait.data()));
    for (int index = 0; index < 16; ++index) portrait[32 + index] = static_cast<float>(portrait_vp[index]);
    unchanged(portrait, "the portrait's size");
    PerView mismatched = CITY;
    mismatched[44] += 5.f;
    unchanged(mismatched, "rows 8-11 not rows 4-7 x 0-3");
    {
      const auto short_buffer = MakeBuffer(192, D3D11_USAGE_DYNAMIC);
      PerView upload = CITY;
      state.camera = {};
      Upload(short_buffer.Get(), &upload);
      CHECK(upload == CITY && state.camera.frame == 0);
      Upload(camera_buffer.Get(), &upload, reshade::api::map_access::read_only);
      CHECK(upload == CITY && state.camera.frame == 0);
      dlaa::sliders.method = 0.f;
      Upload(camera_buffer.Get(), &upload);
      CHECK(upload == CITY && state.camera.frame == 0);
      dlaa::rays = true;  // the rays alone keep the camera too
      Upload(camera_buffer.Get(), &upload);
      CHECK(state.camera.frame == state.frame);
      dlaa::rays = false;
      dlaa::sliders.method = 5.f;
      const auto large = MakeBuffer(256, D3D11_USAGE_DYNAMIC);
      upload = CITY;
      state.camera = {};
      Upload(large.Get(), &upload);
      CHECK(state.camera.frame == state.frame && upload != CITY);
    }
    // UpdateSubresource: the jittered camera instead, a 256-byte buffer's last 16 bytes as the game gave them.
    const auto default_buffer = MakeBuffer(256, D3D11_USAGE_DEFAULT);
    const reshade::api::resource resource = {reinterpret_cast<uint64_t>(default_buffer.Get())};
    std::array<float, 64> game_upload = {};
    std::copy(CITY.begin(), CITY.end(), game_upload.begin());
    game_upload[60] = 7.f;
    game_upload[63] = 9.f;
    CHECK(dlaa::internal::OnUpdateBufferRegion(&mock_device, game_upload.data(), resource, 0, 256));
    PerView expected = CITY;
    Upload(camera_buffer.Get(), &expected);
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> staging;
    REQUIRE(SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf())));
    context->CopyResource(staging.Get(), default_buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    CHECK(std::memcmp(mapped.pData, expected.data(), 240) == 0);
    CHECK(std::memcmp(static_cast<const float*>(mapped.pData) + 60, game_upload.data() + 60, 16) == 0);
    context->Unmap(staging.Get(), 0);
    CHECK(!dlaa::internal::OnUpdateBufferRegion(&mock_device, orthographic.data(), resource, 0, 240));
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
    dlaa::sliders.method = 0.f;
    state.evaluated_frame = state.frame;
    dlaa::internal::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    CHECK(state.jitter[0] == 0.f && state.jitter[1] == 0.f);
    dlaa::sliders.method = 5.f;
  }

  // 4. The shaders: a ground 15 m below the eye drawn with the jittered camera (points past it or past 90 m: 90 m along
  // the ray; the far plane is 100 m), the camera 0.3 m away the frame before.
  state.jitter = jitter;
  PerView upload = CITY;
  Upload(camera_buffer.Get(), &upload);
  REQUIRE(state.camera.frame == state.frame);
  const Matrix jittered = Rows(upload.data() + 32);
  const Matrix inverse_jittered = Inverse(jittered);
  const Matrix view_projection = Rows(CITY.data() + 32);
  Matrix moved_view = Rows(CITY.data());
  const std::array<double, 3> step = {0.3, 0., -0.1};
  // The eye moved by -step: the view's translation column gains row . step.
  for (int row = 0; row < 3; ++row) {
    for (int k = 0; k < 3; ++k) moved_view[(row * 4) + 3] += moved_view[(row * 4) + k] * step[k];
  }
  const Matrix previous = Multiply(Rows(CITY.data() + 16), moved_view);
  const double ground = CITY[49] - 15.;
  std::vector<float> depth(WIDTH * HEIGHT);
  std::vector<std::array<double, 2>> reference(WIDTH * HEIGHT);
  std::vector<std::array<uint16_t, 4>> frame(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const size_t at = (static_cast<size_t>(y) * WIDTH) + x;
      const double ndc_x = (((x + 0.5) / WIDTH) * 2.) - 1.;
      const double ndc_y = 1. - (((y + 0.5) / HEIGHT) * 2.);
      auto near_point = Apply(inverse_jittered, {ndc_x, ndc_y, 0., 1.});
      auto far_point = Apply(inverse_jittered, {ndc_x, ndc_y, 0.5, 1.});
      for (int k = 0; k < 3; ++k) {
        near_point[k] /= near_point[3];
        far_point[k] /= far_point[3];
      }
      const double ray = std::sqrt(((far_point[0] - near_point[0]) * (far_point[0] - near_point[0])) + ((far_point[1] - near_point[1]) * (far_point[1] - near_point[1]))
                                   + ((far_point[2] - near_point[2]) * (far_point[2] - near_point[2])));
      double along = (ground - near_point[1]) / (far_point[1] - near_point[1]);
      if (along <= 0. || along * ray > 90.) along = 90. / ray;
      std::array<double, 4> world = {};
      for (int k = 0; k < 3; ++k) world[k] = near_point[k] + (along * (far_point[k] - near_point[k]));
      world[3] = 1.;
      const auto drawn = Apply(jittered, world);
      depth[at] = static_cast<float>(drawn[2] / drawn[3]);
      const auto now = Apply(view_projection, world);
      const auto before = Apply(previous, world);
      reference[at] = {((before[0] / before[3]) - (now[0] / now[3])) * 0.5 * WIDTH, ((before[1] / before[3]) - (now[1] / now[3])) * -0.5 * HEIGHT};
      frame[at] = {ToHalf(0.1f + (0.05f * x)), ToHalf(2.5f - (0.05f * y)), ToHalf(0.25f), ToHalf(1.f)};
    }
  }
  const auto frame_texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, frame.data(), WIDTH * 8);
  const auto depth_texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, depth.data(), WIDTH * 4);
  const auto color_out = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
  const auto depth_out = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
  const auto motion_out = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
  const auto result = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
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
  const auto frame_srv = Srv(frame_texture.Get());
  const auto depth_srv = Srv(depth_texture.Get());
  std::array<float, 16> previous_rows = {};
  for (int index = 0; index < 16; ++index) previous_rows[index] = static_cast<float>(previous[index]);
  const auto run_prepare = [&](bool reset) {
    const dlaa::internal::TaaConstants taa = dlaa::internal::MakeTaaConstants(state.camera, previous_rows, !reset);
    context->UpdateSubresource(taa_buffer.Get(), 0, nullptr, &taa, 0, 0);
    const std::array<ID3D11Buffer*, 2> constants = {settings_buffer.Get(), taa_buffer.Get()};
    context->CSSetConstantBuffers(0, 2, constants.data());
    const std::array<ID3D11ShaderResourceView*, 2> views = {frame_srv.Get(), depth_srv.Get()};
    const auto color_uav = Uav(color_out.Get());
    const auto depth_uav = Uav(depth_out.Get());
    const auto motion_uav = Uav(motion_out.Get());
    const std::array<ID3D11UnorderedAccessView*, 3> uavs = {color_uav.Get(), depth_uav.Get(), motion_uav.Get()};
    context->CSSetShader(prepare.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, views.data());
    context->CSSetUnorderedAccessViews(0, 3, uavs.data(), nullptr);
    context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    const std::array<ID3D11UnorderedAccessView*, 3> none = {};
    context->CSSetUnorderedAccessViews(0, 3, none.data(), nullptr);
  };
  run_prepare(false);
  CHECK(ReadBack<float>(depth_out.Get(), WIDTH, HEIGHT) == depth);
  {
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
  }
  {
    const auto color = ReadBack<std::array<uint16_t, 4>>(color_out.Get(), WIDTH, HEIGHT);
    double linear_worst = 0.;
    double squared_best = 1e9;
    for (size_t at = 0; at < color.size(); at += 5) {
      const std::array<double, 3> linear = {Half(frame[at][0]), Half(frame[at][1]), Half(frame[at][2])};
      const auto bt2020 = [](const std::array<double, 3>& c) {
        return std::array<double, 3>{(0.6274040 * c[0]) + (0.3292820 * c[1]) + (0.0433136 * c[2]),
                                     (0.0690970 * c[0]) + (0.9195400 * c[1]) + (0.0113612 * c[2]),
                                     (0.0163916 * c[0]) + (0.0880132 * c[1]) + (0.8955950 * c[2])};
      };
      const auto expected = bt2020(linear);
      const auto squared = bt2020({linear[0] * linear[0], linear[1] * linear[1], linear[2] * linear[2]});
      double squared_error = 0.;
      for (int channel = 0; channel < 3; ++channel) {
        linear_worst = std::max(linear_worst, std::abs(Half(color[at][channel]) - expected[channel]) / std::max(expected[channel], 1e-3));
        squared_error = std::max(squared_error, std::abs(Half(color[at][channel]) - squared[channel]) / std::max(squared[channel], 1e-3));
      }
      squared_best = std::min(squared_best, squared_error);
      CHECK(color[at][3] == frame[at][3]);
    }
    std::printf("colour for NVIDIA: linear worst %.2e relative; CONTROL Attila's square: nearest %.2e\n", linear_worst, squared_best);
    CHECK(linear_worst < 2e-3);
    CHECK(squared_best > 1e-2);
  }
  {
    const auto color_srv = Srv(color_out.Get());
    const auto motion_srv = Srv(motion_out.Get());
    const auto result_uav = Uav(result.Get());
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
    // Relative to the pixel's brightest channel: the BT.2020 frame between is half floats too, so a dim channel next to a bright
    // one carries the bright one's rounding: three roundings (2^-11 each) through the 2.3x BT.2020 to BT.709 rows, 8 steps.
    double round_trip = 0.;
    for (size_t at = 0; at < back.size(); ++at) {
      const float scale = std::max({Half(frame[at][0]), Half(frame[at][1]), Half(frame[at][2]), 0.1f});
      for (int channel = 0; channel < 4; ++channel) {
        round_trip = std::max(round_trip, static_cast<double>(std::abs(Half(back[at][channel]) - Half(frame[at][channel])) / (channel == 3 ? 1.f : scale)));
      }
    }
    std::printf("finish(prepare(frame)): worst %.2e of the pixel's brightest channel off the frame (8 half steps: %.2e)\n", round_trip, 8. / 2048.);
    CHECK(round_trip < 8. / 2048.);
  }
  run_prepare(true);
  const auto reset_motion = ReadBack<std::array<float, 2>>(motion_out.Get(), WIDTH, HEIGHT);
  CHECK(std::ranges::all_of(reset_motion, [](const std::array<float, 2>& value) { return value[0] == 0.f && value[1] == 0.f; }));

  // 5. The game's SMAA and FXAA under DLAA.
  {
    const auto target = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    const auto other = MakeTexture(WIDTH * 2, HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    const auto bind_target = [&](ID3D11Texture2D* texture) {
      const auto view = Rtv(texture);
      context->OMSetRenderTargets(1, view.GetAddressOf(), nullptr);
    };
    state.evaluated_frame = state.frame;
    state.evaluated_size = {WIDTH, HEIGHT};
    bind_target(target.Get());
    const bool skipped = dlaa::SkipSmaaWeights(&mock_list);
    bind_target(other.Get());
    const bool other_skipped = dlaa::SkipSmaaWeights(&mock_list);
    bind_target(target.Get());
    dlaa::sliders.method = 0.f;
    const bool off_skipped = dlaa::SkipSmaaWeights(&mock_list);
    dlaa::sliders.method = 5.f;
    state.evaluated_frame = state.frame - 1;
    const bool stale_skipped = dlaa::SkipSmaaWeights(&mock_list);
    state.evaluated_frame = state.frame;
    std::printf("SMAA weights: skipped %d; CONTROL another size %d, row off %d, no DLAA this frame %d\n", skipped, other_skipped, off_skipped,
                stale_skipped);
    CHECK(skipped && !other_skipped && !off_skipped && !stale_skipped);

    // FXAA: the PQ picture (RGB10A2) copied into its target.
    std::vector<uint32_t> picture(static_cast<size_t>(WIDTH) * HEIGHT);
    for (size_t at = 0; at < picture.size(); ++at) picture[at] = static_cast<uint32_t>((at * 2654435761u) ^ 0x5A5A5A5Au);
    const auto input = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_SHADER_RESOURCE, picture.data(), WIDTH * 4);
    const auto fxaa_target = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    const auto fxaa_other = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    const auto input_srv = Srv(input.Get());
    context->PSSetShaderResources(0, 1, input_srv.GetAddressOf());
    bind_target(fxaa_target.Get());
    const bool copied = dlaa::SkipFxaa(&mock_list);
    const auto copy = ReadBack<uint32_t>(fxaa_target.Get(), WIDTH, HEIGHT);
    bind_target(fxaa_other.Get());
    const bool other_copied = dlaa::SkipFxaa(&mock_list);
    const auto untouched = ReadBack<uint32_t>(fxaa_other.Get(), WIDTH, HEIGHT);
    std::printf("FXAA: copied %d, %s; CONTROL another format: copied %d, target %s\n", copied, (copy == picture ? "bit for bit" : "DIFFERS"),
                other_copied, (std::ranges::all_of(untouched, [](uint32_t value) { return value == 0u; }) ? "untouched" : "WRITTEN"));
    CHECK(copied && copy == picture);
    CHECK(!other_copied && std::ranges::all_of(untouched, [](uint32_t value) { return value == 0u; }));
    ID3D11ShaderResourceView* no_view = nullptr;
    context->PSSetShaderResources(0, 1, &no_view);
    context->OMSetRenderTargets(0, nullptr, nullptr);
  }

  // 6. Ray tracing's writes.
  {
    REQUIRE(rays::internal::MakeObjects(device.Get()));
    std::vector<uint8_t> visibility(static_cast<size_t>(WIDTH) * HEIGHT);
    for (size_t at = 0; at < visibility.size(); ++at) visibility[at] = static_cast<uint8_t>((at * 37) % 256);
    const auto visibility_texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, visibility.data(), WIDTH);
    const auto visibility_srv = Srv(visibility_texture.Get());
    const auto mask = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM, D3D11_BIND_RENDER_TARGET);
    rays::internal::DrawVisibility(context.Get(), Rtv(mask.Get()).Get(), visibility_srv.Get());
    const bool mask_exact = ReadBack<uint8_t>(mask.Get(), WIDTH, HEIGHT) == visibility;

    // The AO texture: the game's own AO in it first (0.3), the game's PPApplyAO reading it before and after the write.
    std::vector<uint32_t> game_ao(static_cast<size_t>(WIDTH) * HEIGHT, 307u | (307u << 10) | (307u << 20) | (3u << 30));
    const auto ao = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, game_ao.data(), WIDTH * 4);
    const auto ao_srv = Srv(ao.Get());
    std::vector<uint8_t> apply_code;
    if (argc >= 2) {
      std::ifstream file(argv[1], std::ios::binary);
      apply_code.assign(std::istreambuf_iterator<char>(file), {});
    }
    REQUIRE(!apply_code.empty());
    ComPtr<ID3D11PixelShader> apply;
    REQUIRE(SUCCEEDED(device->CreatePixelShader(apply_code.data(), apply_code.size(), nullptr, apply.GetAddressOf())));
    // PPApplyAO's vertex shader's outputs: SV_Position, TexCoord0 (its viewport's uv).
    const char* vertex_source =
        "struct V { float4 position : SV_Position0; float2 uv : TexCoord0; };\n"
        "V main(uint id : SV_VertexID) { V v; v.uv = float2((id << 1) & 2, id & 2); v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1); return v; }\n";
    ComPtr<ID3DBlob> vertex_blob;
    REQUIRE(SUCCEEDED(D3DCompile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_blob.GetAddressOf(), nullptr)));
    ComPtr<ID3D11VertexShader> full_screen;
    REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr, full_screen.GetAddressOf())));
    const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
    ComPtr<ID3D11SamplerState> sampler;
    REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf())));
    const auto applied = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET);
    const auto run_apply = [&] {
      const auto target = Rtv(applied.Get());
      const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(WIDTH), .Height = static_cast<float>(HEIGHT), .MaxDepth = 1.f};
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(nullptr);
      context->VSSetShader(full_screen.Get(), nullptr, 0);
      context->PSSetShader(apply.Get(), nullptr, 0);
      context->PSSetShaderResources(0, 1, ao_srv.GetAddressOf());
      context->PSSetSamplers(0, 1, sampler.GetAddressOf());
      context->RSSetViewports(1, &viewport);
      context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
      context->Draw(3, 0);
      ID3D11ShaderResourceView* no_view = nullptr;
      context->PSSetShaderResources(0, 1, &no_view);
      context->OMSetRenderTargets(0, nullptr, nullptr);
      return ReadBack<std::array<uint16_t, 4>>(applied.Get(), WIDTH, HEIGHT);
    };
    const auto before = run_apply();
    rays::internal::DrawVisibility(context.Get(), Rtv(ao.Get()).Get(), visibility_srv.Get());
    const auto written = ReadBack<uint32_t>(ao.Get(), WIDTH, HEIGHT);
    const auto after = run_apply();
    double ao_worst = 0.;
    double apply_worst = 0.;
    double game_worst = 0.;
    for (size_t at = 0; at < visibility.size(); ++at) {
      const double value = visibility[at] / 255.;
      ao_worst = std::max(ao_worst, std::abs(((written[at] & 1023u) / 1023.) - value));
      apply_worst = std::max(apply_worst, std::abs(Half(after[at][3]) - (1. - value)));
      game_worst = std::max(game_worst, std::abs(Half(before[at][3]) - (1. - (307. / 1023.))));
    }
    std::printf("rays: sun mask %s; AO texture within %.5f of the visibility (half a 10-bit step: %.5f); the game's PPApplyAO "
                "alpha 1 - visibility within %.5f; CONTROL before the write: the game's AO within %.5f\n",
                (mask_exact ? "exact" : "DIFFERS"), ao_worst, 0.5 / 1023., apply_worst, game_worst);
    CHECK(mask_exact);
    CHECK(ao_worst <= 0.5 / 1023. + 1e-6);
    CHECK(apply_worst < 1.5e-3);
    CHECK(game_worst < 1.5e-3);

    // rt_frame_sun: PerFrame's LightPropertyMatrix column-major (register i = column i): row 0 = (c0.x, c1.x, c2.x).
    std::array<float, 32> per_frame = {};
    const std::array<float, 3> toward = {0.3f, 0.8f, -0.5f};
    for (int axis = 0; axis < 3; ++axis) per_frame[axis * 4] = toward[axis];
    for (int index = 1; index < 16; ++index) {
      if (index % 4 != 0) per_frame[index] = 0.1f * static_cast<float>(index);
    }
    const auto frame_constants = MakeBuffer(128, D3D11_USAGE_DEFAULT);
    context->UpdateSubresource(frame_constants.Get(), 0, nullptr, per_frame.data(), 0, 0);
    const auto sun = MakeTexture(3, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
    const auto sun_uav = Uav(sun.Get());
    context->CSSetShader(rays::internal::state.objects.sun.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, frame_constants.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, sun_uav.GetAddressOf(), nullptr);
    context->Dispatch(1, 1, 1);
    ID3D11UnorderedAccessView* no_uav = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
    const auto sun_back = ReadBack<std::array<float, 4>>(sun.Get(), 3, 1);
    const double length = std::sqrt((toward[0] * toward[0]) + (toward[1] * toward[1]) + (toward[2] * toward[2]));
    double sun_worst = 0.;
    double row_major_nearest = 0.;
    for (int axis = 0; axis < 3; ++axis) {
      sun_worst = std::max(sun_worst, std::abs(sun_back[0][axis] - (toward[axis] / length)));
    }
    const std::array<float, 3> row_major = {per_frame[0], per_frame[1], per_frame[2]};
    const double row_major_length = std::sqrt((row_major[0] * row_major[0]) + (row_major[1] * row_major[1]) + (row_major[2] * row_major[2]));
    for (int axis = 0; axis < 3; ++axis) {
      row_major_nearest = std::max(row_major_nearest, std::abs(sun_back[0][axis] - (row_major[axis] / row_major_length)));
    }
    std::printf("rt_frame_sun: toward the sun within %.2e, w %.1f; CONTROL the registers' first floats: %.3f off\n", sun_worst, sun_back[0][3],
                row_major_nearest);
    CHECK(sun_worst < 1e-5 && sun_back[0][3] == 1.f);
    CHECK(row_major_nearest > 0.1);
  }

  // 7. The overlays left out of the rays' scene: every OVERLAY_PIXEL_SHADERS hash is a shipped pixel shader whose bytes'
  //    CRC32 it is; the game's surface (RS3_Surface_Source3, 141 of the city's G-buffer draws) and decal shaders made on
  //    WARP are noted by init_pipeline and let go by destroy_pipeline (CONTROL: Base_GM_PBR's, an opaque wall's, not noted).
  {
    const std::string census = std::string(argv[1]).substr(0, std::string(argv[1]).find_last_of("\\/") + 1);
    const auto read = [&](uint32_t hash) {
      char name[32] = {};
      std::snprintf(name, sizeof(name), "0x%08X.ps_5_0.cso", hash);
      std::ifstream file(census + name, std::ios::binary);
      return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    };
    int shipped = 0;
    for (const uint32_t hash : rays::internal::OVERLAY_PIXEL_SHADERS) {
      const auto code = read(hash);
      shipped += (!code.empty() && renodx::utils::hash::ComputeCRC32(code.data(), code.size()) == hash) ? 1 : 0;
    }
    const auto made = [&](uint32_t hash) {
      const auto code = read(hash);
      ComPtr<ID3D11PixelShader> shader;
      if (code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf()))) return shader;
      const reshade::api::shader_desc desc = {.code = code.data(), .code_size = code.size()};
      const reshade::api::pipeline_subobject subobject = {.type = reshade::api::pipeline_subobject_type::pixel_shader, .count = 1, .data = const_cast<reshade::api::shader_desc*>(&desc)};
      rays::internal::OnInitPipeline(&mock_device, {}, 1, &subobject, {reinterpret_cast<uint64_t>(shader.Get())});
      return shader;
    };
    const auto noted = [&](ID3D11PixelShader* shader) {
      return rays::internal::overlay_pixel_shaders.contains(reinterpret_cast<uint64_t>(shader));
    };
    const auto surface = made(0x6DDCE4B0);
    const auto decal = made(0x635DE5BD);
    const auto wall = made(0x81FB65AC);
    REQUIRE(surface && decal && wall);
    const bool surface_noted = noted(surface.Get());
    const bool decal_noted = noted(decal.Get());
    const bool wall_noted = noted(wall.Get());
    rays::internal::OnDestroyPipeline(&mock_device, {reinterpret_cast<uint64_t>(surface.Get())});
    const bool surface_let_go = !noted(surface.Get());
    std::printf("overlays: %d of %zu listed hashes are shipped pixel shaders with that CRC32; surface noted %d, decal noted %d, "
                "let go %d; CONTROL a wall's noted %d\n",
                shipped, rays::internal::OVERLAY_PIXEL_SHADERS.size(), surface_noted, decal_noted, surface_let_go, wall_noted);
    CHECK(shipped == static_cast<int>(rays::internal::OVERLAY_PIXEL_SHADERS.size()));
    CHECK(surface_noted && decal_noted && surface_let_go);
    CHECK(!wall_noted);
  }

  std::printf("%s: %d failure(s)\n", (failures == 0 ? "PASS" : "FAIL"), failures);
  return failures == 0 ? 0 : 1;
}
