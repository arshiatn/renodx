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
// 8. Skip the Game's Shadow Maps: the draws into the cascades' atlas (learned at the replaced mask's draw) skipped under
//    the rays' sun (CONTROLS: another depth target, the row off, no trace this frame).
// 9. Path-Traced Ambient and Global Illumination against the game's DeferredSky (0x518005B9, the census's bytes): the
//    paths' sky (rt_sky through OnAmbientPass at the sky's draw) is each axis's light as the game lights a white
//    non-metal surface facing it (CONTROL: the opposite axis's), zeros without Path-Traced Ambient; under the paths' sky the
//    game's draw with the black cube gives no diffuse and the same specular bit for bit, its own cube bound again after
//    (CONTROL: Path-Traced Ambient off, the game's diffuse); rt_bounce_apply adds the resolved light times the game's own
//    diffuse factor (its o1 under a white cube: 0 on the sky and unlit pixels) and the strength, alpha kept, its
//    neighbours weighed in the world normal's plane (CONTROL: the view's rows transposed); the sun at the lighting's
//    display-size target (CONTROL: another size, nothing written); the G-buffer guides at the lighting draw: its diffuse
//    and specular colours the game's own o1 / o0 under white cubes (a per-pixel camera ray, a LUT ramp), the world normal
//    and roughness it lights with, none on the sky / unlit, once a frame (CONTROLS: F0 without the LUT, the rotation
//    instead of its transpose); the rays' reflections over the game's own IBL specular (o0) at the lighting draw: met
//    everywhere at the game's light gives o0 again, a share a of light 2 gives o0 x (1 + a), share 0 leaves it bit for
//    bit, Specular Occlusion alone dims it by the open share (CONTROL: the rotation transposed); DeferredSSR not drawn and
//    DeferredSSRCombine (0x0FA22D5A) replaced by its scene bit for bit under them (CONTROLS: the game's combine changes
//    the scene; the reflections off: both the game's).
// 10. GT-VBAO (screen_space.hpp) against the game's PPSAO_Reinterleave (0x2CA00ABD) and PPSAO_Blur (0x6FFB511E), the
//    census's bytes, on a scene through CITY's camera at 256 x 144 (the ground 15 m below the eye, a wall facing it): PPSAO's
//    four draws skipped, the reinterleave's draw with GT-VBAO's shader writes the game's own depth (green) in every pixel
//    and a visibility near 1 on open ground, dark in the corner; the game's shader and bindings back after; the game's blur
//    keeps them so (CONTROLS: GT-VBAO off, the game's reinterleave writes its slices' AO; the world normal in GBuffer1
//    (no View rotation) darkens open ground; the blur changed the texture).
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
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "src/games/divinity2originalsindefinitive/dlaa.hpp"
#include "src/games/divinity2originalsindefinitive/ray_tracing.hpp"
#include "src/games/divinity2originalsindefinitive/screen_space.hpp"
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

  // 8. Skip the Game's Shadow Maps: the mask's draw (the cascades' atlas at its t1) under the rays' sun writes the
  //    visibility and notes the atlas; then binding the atlas as the depth target skips the draws into it (an indirect
  //    dispatch not). CONTROLS: the scene's depth bound, the row off, no trace this frame: drawn.
  {
    auto& runtime = twattila::rt_scene::internal::runtime;
    auto& bridge = twattila::dlaa_bridge::bridge;
    std::vector<uint8_t> visibility(static_cast<size_t>(WIDTH) * HEIGHT);
    for (size_t at = 0; at < visibility.size(); ++at) visibility[at] = static_cast<uint8_t>((at * 53) % 256);
    const auto visibility_texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, visibility.data(), WIDTH);
    bridge.rt_visibility.srv = Srv(visibility_texture.Get());
    runtime.traced_frame = runtime.frame;
    rays::sliders.sun_shadows = 1.f;
    rays::skip_shadow_maps = 1.f;
    dlaa::rays = true;
    state.depth_size = {WIDTH, HEIGHT};
    const auto make_depth = [&](UINT size) {
      const D3D11_TEXTURE2D_DESC desc = {.Width = size, .Height = size, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R16_TYPELESS,
                                         .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE};
      ComPtr<ID3D11Texture2D> texture;
      REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())));
      return texture;
    };
    const auto atlas = make_depth(128);
    const auto scene_depth = make_depth(64);
    const D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {.Format = DXGI_FORMAT_D16_UNORM, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
    ComPtr<ID3D11DepthStencilView> atlas_dsv;
    ComPtr<ID3D11DepthStencilView> scene_dsv;
    REQUIRE(SUCCEEDED(device->CreateDepthStencilView(atlas.Get(), &dsv_desc, atlas_dsv.GetAddressOf())));
    REQUIRE(SUCCEEDED(device->CreateDepthStencilView(scene_depth.Get(), &dsv_desc, scene_dsv.GetAddressOf())));
    const D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = DXGI_FORMAT_R16_UNORM, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                      .Texture2D = {.MipLevels = 1}};
    ComPtr<ID3D11ShaderResourceView> atlas_srv;
    REQUIRE(SUCCEEDED(device->CreateShaderResourceView(atlas.Get(), &srv_desc, atlas_srv.GetAddressOf())));
    const auto mask = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM, D3D11_BIND_RENDER_TARGET);
    const auto mask_rtv = Rtv(mask.Get());
    context->PSSetShaderResources(1, 1, atlas_srv.GetAddressOf());
    context->OMSetRenderTargets(1, mask_rtv.GetAddressOf(), nullptr);
    const bool replaced = rays::ReplaceSunMask(&mock_list);
    const bool mask_exact = ReadBack<uint8_t>(mask.Get(), WIDTH, HEIGHT) == visibility;
    const bool atlas_noted = rays::internal::shadow_atlas_address.load() == reinterpret_cast<uintptr_t>(atlas.Get());
    ID3D11ShaderResourceView* no_view = nullptr;
    context->PSSetShaderResources(1, 1, &no_view);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    const auto bound = [&](ID3D11DepthStencilView* dsv) {
      rays::internal::OnBindTargets(&mock_list, 0, nullptr, {reinterpret_cast<uint64_t>(dsv)});
      return rays::internal::OnCasterDraw(&mock_list, 3, 1, 0, 0) && rays::internal::OnCasterDrawIndexed(&mock_list, 3, 1, 0, 0, 0);
    };
    const bool skipped = bound(atlas_dsv.Get());
    const bool dispatch_skipped = rays::internal::OnCasterDrawIndirect(&mock_list, reshade::api::indirect_command::dispatch, {}, 0, 1, 0);
    const bool indirect_skipped = rays::internal::OnCasterDrawIndirect(&mock_list, reshade::api::indirect_command::draw_indexed, {}, 0, 1, 0);
    const bool scene_skipped = bound(scene_dsv.Get());
    rays::skip_shadow_maps = 0.f;
    const bool row_off_skipped = bound(atlas_dsv.Get());
    rays::skip_shadow_maps = 1.f;
    runtime.traced_frame = runtime.frame - 1;
    const bool untraced_skipped = bound(atlas_dsv.Get());
    std::printf("shadow maps: mask %s, atlas noted %d; atlas bound: draws skipped %d (indirect draw %d, indirect dispatch %d); CONTROLS "
                "the scene's depth %d, row off %d, no trace this frame %d\n",
                (replaced && mask_exact ? "the rays' visibility" : "NOT REPLACED"), atlas_noted, skipped, indirect_skipped, dispatch_skipped,
                scene_skipped, row_off_skipped, untraced_skipped);
    CHECK(replaced && mask_exact && atlas_noted);
    CHECK(skipped && indirect_skipped && !dispatch_skipped);
    CHECK(!scene_skipped && !row_off_skipped && !untraced_skipped);
    bound(nullptr);
    bridge.rt_visibility.srv.Reset();
    dlaa::rays = false;
  }

  // 9. Path-Traced Ambient and Global Illumination against the game's DeferredSky.
  {
    auto& runtime = twattila::rt_scene::internal::runtime;
    auto& bridge = twattila::dlaa_bridge::bridge;
    auto& ray_state = rays::internal::state;
    const auto& objects = ray_state.objects;
    dlaa::rays = true;
    state.depth_size = {WIDTH, HEIGHT};
    const std::string census = std::string(argv[1]).substr(0, std::string(argv[1]).find_last_of("\\/") + 1);
    std::vector<uint8_t> sky_code;
    {
      std::ifstream file(census + "0x518005B9.ps_5_0.cso", std::ios::binary);
      sky_code.assign(std::istreambuf_iterator<char>(file), {});
    }
    REQUIRE(!sky_code.empty() && renodx::utils::hash::ComputeCRC32(sky_code.data(), sky_code.size()) == 0x518005B9);
    ComPtr<ID3D11PixelShader> sky;
    REQUIRE(SUCCEEDED(device->CreatePixelShader(sky_code.data(), sky_code.size(), nullptr, sky.GetAddressOf())));
    // The view's rotation (world to view, rows acting on column vectors): 45 degrees about x, then 30 about z. DeferredSky's
    // invViewMatrix (c9-c11) is its transpose, the camera's View its rows.
    const double c45 = std::sqrt(.5);
    const double c30 = std::sqrt(3.) / 2.;
    const std::array<std::array<double, 3>, 3> rx = {{{1., 0., 0.}, {0., c45, -c45}, {0., c45, c45}}};
    const std::array<std::array<double, 3>, 3> rz = {{{c30, -.5, 0.}, {.5, c30, 0.}, {0., 0., 1.}}};
    std::array<std::array<double, 3>, 3> rotation = {};
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 3; ++column) {
        for (int k = 0; k < 3; ++k) rotation[row][column] += rz[row][k] * rx[k][column];
      }
    }
    // The camera: at EYE, that rotation, a perspective projection (x / P00, y / P11 on the view's z, near 0.1, far 100).
    constexpr double P00 = 1.2;
    constexpr double P11 = 2.1;
    constexpr std::array<double, 3> EYE = {3., 4., 5.};
    // DeferredSky's vertex shader's outputs: SV_Position, TexCoord0 (uv), TexCoord1 (the pixel's ray in the world: the
    // rotation's transpose times (x / P00, y / P11, 1), linear across the screen).
    char vertex_source[1024] = {};
    std::snprintf(vertex_source, sizeof(vertex_source),
                  "struct V { float4 position : SV_Position0; float2 uv : TexCoord0; float3 ray : TexCoord1; };\n"
                  "V main(uint id : SV_VertexID) { V v; v.uv = float2((id << 1) & 2, id & 2); const float2 ndc = v.uv * float2(2, -2) + float2(-1, 1);"
                  " v.position = float4(ndc, 0, 1); const float3 d = float3(ndc.x / %.17g, ndc.y / %.17g, 1);"
                  " v.ray = d.x * float3(%.17g, %.17g, %.17g) + d.y * float3(%.17g, %.17g, %.17g) + d.z * float3(%.17g, %.17g, %.17g); return v; }\n",
                  P00, P11, rotation[0][0], rotation[0][1], rotation[0][2], rotation[1][0], rotation[1][1], rotation[1][2], rotation[2][0],
                  rotation[2][1], rotation[2][2]);
    ComPtr<ID3DBlob> vertex_blob;
    REQUIRE(SUCCEEDED(D3DCompile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_blob.GetAddressOf(), nullptr)));
    ComPtr<ID3D11VertexShader> sky_vertex;
    REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr, sky_vertex.GetAddressOf())));
    const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
    ComPtr<ID3D11SamplerState> sampler;
    REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf())));
    // An 8 x 8 cube (a direction near a face's centre takes that face alone), each face one colour (+x, -x, +y, -y, +z, -z).
    const auto make_cube = [&](const std::array<std::array<float, 3>, 6>& faces) {
      constexpr UINT FACE = 8;
      std::vector<std::array<uint16_t, 4>> texels(static_cast<size_t>(FACE) * FACE * 6);
      std::array<D3D11_SUBRESOURCE_DATA, 6> data = {};
      for (UINT face = 0; face < 6; ++face) {
        for (UINT at = 0; at < FACE * FACE; ++at) {
          texels[(face * FACE * FACE) + at] = {ToHalf(faces[face][0]), ToHalf(faces[face][1]), ToHalf(faces[face][2]), ToHalf(1.f)};
        }
        data[face] = {.pSysMem = &texels[face * FACE * FACE], .SysMemPitch = FACE * 8};
      }
      const D3D11_TEXTURE2D_DESC desc = {.Width = FACE, .Height = FACE, .MipLevels = 1, .ArraySize = 6, .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                                         .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                         .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
      ComPtr<ID3D11Texture2D> texture;
      REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, data.data(), texture.GetAddressOf())));
      return Srv(texture.Get());
    };
    std::array<std::array<float, 3>, 6> sky_faces = {};
    for (int face = 0; face < 6; ++face) sky_faces[face] = {0.25f * static_cast<float>(face + 1), 0.125f * static_cast<float>(7 - face), 1.f + (0.5f * static_cast<float>(face))};
    const auto diffuse_cube = make_cube(sky_faces);
    const auto white_cube = make_cube({{{1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}}});
    const auto specular_cube = make_cube({{{.5f, .75f, 1.f}, {1.f, .5f, .25f}, {.25f, .25f, .5f}, {2.f, 1.f, .5f}, {.5f, 1.5f, 1.f}, {1.f, 1.f, 3.f}}});
    const auto exposure_of = [&](float value) { return Srv(MakeTexture(1, 1, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, &value, 4).Get()); };
    const auto exposure = exposure_of(2.f);
    const auto exposure_one = exposure_of(1.f);
    // The LUT: x and y ramps over N.V and the roughness (the specular's scale and bias), z 1 (the diffuse's).
    std::vector<std::array<uint16_t, 4>> lut_texels(16 * 16);
    for (int row = 0; row < 16; ++row) {
      for (int column = 0; column < 16; ++column) {
        lut_texels[(row * 16) + column] = {ToHalf((static_cast<float>(column) + .5f) / 16.f), ToHalf(.05f + (.3f * (static_cast<float>(row) + .5f) / 16.f)),
                                           ToHalf(1.f), ToHalf(1.f)};
      }
    }
    const auto lut = Srv(MakeTexture(16, 16, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, lut_texels.data(), 16 * 8).Get());
    std::array<float, 48> sky_globals = {};
    sky_globals[(3 * 4) + 1] = 1.f;    // ProbeProperties.y: the specular's mip scale
    sky_globals[(3 * 4) + 2] = .01f;   // ProbeProperties.z: the intensity
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 3; ++column) sky_globals[((9 + row) * 4) + column] = static_cast<float>(rotation[column][row]);
    }
    const auto globals = MakeBuffer(192, D3D11_USAGE_DEFAULT);
    context->UpdateSubresource(globals.Get(), 0, nullptr, sky_globals.data(), 0, 0);
    const auto per_view = MakeBuffer(240, D3D11_USAGE_DEFAULT);
    const std::array<float, 60> no_view = {};
    context->UpdateSubresource(per_view.Get(), 0, nullptr, no_view.data(), 0, 0);
    // The G-buffer: GBuffer1 the stereographic view normal (/ 3.5554 + 0.5), GBuffer2 base colour, GBuffer3 roughness,
    // reflectance, metal, shading model << 1; LinearDepth 1 the far plane.
    struct GBuffer {
      ComPtr<ID3D11ShaderResourceView> normals, base, surface, depth;
    };
    const auto encode = [](const std::array<double, 3>& view_normal) {
      const double scale = 1. / (1. + view_normal[2]);
      return std::array<uint16_t, 2>{ToHalf(static_cast<float>(((view_normal[0] * scale) + 1.7777) / 3.5554)),
                                     ToHalf(static_cast<float>(((view_normal[1] * scale) + 1.7777) / 3.5554))};
    };
    const auto to_view = [&](const std::array<double, 3>& world) {
      std::array<double, 3> view = {};
      for (int row = 0; row < 3; ++row) view[row] = (rotation[row][0] * world[0]) + (rotation[row][1] * world[1]) + (rotation[row][2] * world[2]);
      return view;
    };
    const auto make_gbuffer = [&](const std::vector<std::array<uint16_t, 2>>& normals, const std::vector<uint32_t>& base, const std::vector<uint32_t>& surface,
                                  const std::vector<float>& depth) {
      return GBuffer{Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE, normals.data(), WIDTH * 4).Get()),
                     Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, base.data(), WIDTH * 4).Get()),
                     Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, surface.data(), WIDTH * 4).Get()),
                     Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, depth.data(), WIDTH * 4).Get())};
    };
    const size_t pixels = static_cast<size_t>(WIDTH) * HEIGHT;
    // Each pixel faces a world axis ((x + y) % 6: +x, -x, +y, -y, +z, -z), white, not metal, shading model 0.
    std::vector<std::array<uint16_t, 2>> axis_normals(pixels);
    std::vector<int> axis_of(pixels);
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) {
        const int axis = static_cast<int>((x + y) % 6);
        std::array<double, 3> world = {};
        world[axis / 2] = (axis % 2 == 0) ? 1. : -1.;
        axis_of[(y * WIDTH) + x] = axis;
        axis_normals[(y * WIDTH) + x] = encode(to_view(world));
      }
    }
    const auto axes = make_gbuffer(axis_normals, std::vector<uint32_t>(pixels, 0xFFFFFFFFu), std::vector<uint32_t>(pixels, 0x00008080u),
                                   std::vector<float>(pixels, .5f));
    const auto specular_target = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_RENDER_TARGET);
    const auto diffuse_target = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_RENDER_TARGET);
    const auto specular_rtv = Rtv(specular_target.Get());
    const auto diffuse_rtv = Rtv(diffuse_target.Get());
    using Texel = std::array<float, 4>;
    // The game's sky draw (`hooked`: OnAmbientPass before it, OnAmbientPassDrawn after): its o0 (specular) and o1 (diffuse).
    const auto& sky_pass = rays::internal::AMBIENT_PASSES[0];
    ID3D11ShaderResourceView* after_cube = nullptr;
    const auto draw_sky = [&](const GBuffer& gbuffer, ID3D11ShaderResourceView* cube, ID3D11ShaderResourceView* specular, ID3D11ShaderResourceView* exposure_view,
                              bool hooked) {
      const std::array<ID3D11ShaderResourceView*, 8> views = {exposure_view, cube, specular, lut.Get(),
                                                              gbuffer.depth.Get(), gbuffer.normals.Get(), gbuffer.base.Get(), gbuffer.surface.Get()};
      const std::array<ID3D11RenderTargetView*, 2> targets = {specular_rtv.Get(), diffuse_rtv.Get()};
      const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(WIDTH), .Height = static_cast<float>(HEIGHT), .MaxDepth = 1.f};
      constexpr std::array<float, 4> CLEAR = {-1.f, -1.f, -1.f, -1.f};
      context->ClearRenderTargetView(specular_rtv.Get(), CLEAR.data());
      context->ClearRenderTargetView(diffuse_rtv.Get(), CLEAR.data());
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(nullptr);
      context->VSSetShader(sky_vertex.Get(), nullptr, 0);
      context->PSSetShader(sky.Get(), nullptr, 0);
      context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
      context->PSSetConstantBuffers(0, 1, globals.GetAddressOf());
      context->PSSetConstantBuffers(12, 1, per_view.GetAddressOf());
      context->PSSetSamplers(0, 1, sampler.GetAddressOf());
      context->RSSetState(nullptr);
      context->RSSetViewports(1, &viewport);
      context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      context->OMSetRenderTargets(2, targets.data(), nullptr);
      if (hooked) rays::OnAmbientPass(&mock_list, sky_pass);
      context->Draw(3, 0);
      if (hooked) rays::OnAmbientPassDrawn(&mock_list);
      context->PSGetShaderResources(1, 1, &after_cube);
      if (after_cube != nullptr) after_cube->Release();
      const std::array<ID3D11ShaderResourceView*, 8> no_views = {};
      context->PSSetShaderResources(0, static_cast<UINT>(no_views.size()), no_views.data());
      context->OMSetRenderTargets(0, nullptr, nullptr);
      return std::pair{ReadBack<Texel>(specular_target.Get(), WIDTH, HEIGHT), ReadBack<Texel>(diffuse_target.Get(), WIDTH, HEIGHT)};
    };
    const auto sun = MakeTexture(16, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
    bridge.rt_sun.uav = Uav(sun.Get());
    const std::vector<uint8_t> visibility(pixels, 255);
    const auto visibility_texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, visibility.data(), WIDTH);
    bridge.rt_visibility.srv = Srv(visibility_texture.Get());
    runtime.traced_frame = runtime.frame;

    // a. The paths' sky: Path-Traced Ambient on, the paths still learning (the game's cubes stay).
    ray_state.settings.path_ambient = true;
    runtime.path_ambient_on = false;
    const auto [plain_specular, plain_diffuse] = draw_sky(axes, diffuse_cube.Get(), specular_cube.Get(), exposure.Get(), false);
    const auto [learning_specular, learning_diffuse] = draw_sky(axes, diffuse_cube.Get(), specular_cube.Get(), exposure.Get(), true);
    const auto sky_texels = ReadBack<Texel>(sun.Get(), 16, 1);
    double sky_worst = 0.;
    double opposite_nearest = 1e30;
    for (size_t at = 0; at < pixels; ++at) {
      const int axis = axis_of[at];
      double worst = 0.;
      double opposite = 0.;
      for (int channel = 0; channel < 3; ++channel) {
        const double game = plain_diffuse[at][channel];
        worst = std::max(worst, std::abs(sky_texels[3 + axis][channel] - game) / std::max(std::abs(game), 1e-6));
        opposite = std::max(opposite, std::abs(sky_texels[3 + (axis ^ 1)][channel] - game) / std::max(std::abs(game), 1e-6));
      }
      sky_worst = std::max(sky_worst, worst);
      opposite_nearest = std::min(opposite_nearest, opposite);
    }
    const bool sky_w = sky_texels[3][3] == 1.f && sky_texels[4][3] == 1.f && sky_texels[5][3] == 0.f && sky_texels[6][3] == 0.f && sky_texels[7][3] == 0.f
                       && sky_texels[8][3] == 0.f;
    const bool learning_same = learning_specular == plain_specular && learning_diffuse == plain_diffuse;

    // b. The paths bring the sky: the black cube in the diffuse's place, the game's own back after the draw.
    runtime.path_ambient_on = true;
    const auto [paths_specular, paths_diffuse] = draw_sky(axes, diffuse_cube.Get(), specular_cube.Get(), exposure.Get(), true);
    const bool cube_back = after_cube == diffuse_cube.Get();
    const bool diffuse_black = std::ranges::all_of(paths_diffuse, [](const Texel& texel) { return texel == Texel{}; });
    const bool specular_same = paths_specular == plain_specular;
    // CONTROL: Path-Traced Ambient off: the game's diffuse, the paths' sky zeros.
    ray_state.settings.path_ambient = false;
    const auto [off_specular, off_diffuse] = draw_sky(axes, diffuse_cube.Get(), specular_cube.Get(), exposure.Get(), true);
    const auto off_texels = ReadBack<Texel>(sun.Get(), 16, 1);
    const bool off_game = off_diffuse == plain_diffuse && off_specular == plain_specular
                          && std::ranges::any_of(off_diffuse, [](const Texel& texel) { return texel[0] > 0.f; });
    const bool off_zeros = std::all_of(off_texels.begin() + 3, off_texels.begin() + 9, [](const Texel& texel) { return texel == Texel{}; });
    std::printf("paths' sky: each axis's light within %.2e of the game's diffuse on a white surface facing it, w %s; CONTROL the opposite "
                "axis's: %.3f off at the nearest; still learning: the game's draw %s\n",
                sky_worst, (sky_w ? "1 1 0 0 0 0" : "WRONG"), opposite_nearest, (learning_same ? "as it is" : "CHANGED"));
    std::printf("paths bring the sky: the game's diffuse %s, specular %s, its cube %s after; CONTROL Path-Traced Ambient off: the game's "
                "diffuse %s, the sky texels %s\n",
                (diffuse_black ? "0 everywhere" : "NOT 0"), (specular_same ? "bit for bit" : "CHANGED"), (cube_back ? "bound again" : "NOT BACK"),
                (off_game ? "as it is" : "CHANGED"), (off_zeros ? "zeros" : "NOT ZEROS"));
    CHECK(sky_worst < 1e-5 && sky_w && learning_same);
    CHECK(opposite_nearest > .1);
    CHECK(diffuse_black && specular_same && cube_back);
    CHECK(off_game && off_zeros);

    // c. rt_bounce_apply: one world normal, base colours, metal, the sky and unlit pixels from a pattern; the game's diffuse
    //    factor its o1 under a white cube (intensity x 100 = 1, exposure 1, LUT z 1).
    const std::array<double, 3> world_normal = [] {
      const double length = std::sqrt((.2 * .2) + (.9 * .9) + (.3 * .3));
      return std::array<double, 3>{.2 / length, .9 / length, .3 / length};
    }();
    const auto view_normal = to_view(world_normal);
    const auto stored = encode(view_normal);
    // The world normal as the apply decodes it (floats), the plane's axes from it.
    std::array<double, 3> decoded = {};
    {
      const float ex = (Half(stored[0]) * 3.5554f) - 1.7777f;
      const float ey = (Half(stored[1]) * 3.5554f) - 1.7777f;
      const float g = 2.f / ((ex * ex) + (ey * ey) + 1.f);
      const std::array<double, 3> normal = {g * ex, g * ey, g - 1.f};
      for (int axis = 0; axis < 3; ++axis) decoded[axis] = (rotation[0][axis] * normal[0]) + (rotation[1][axis] * normal[1]) + (rotation[2][axis] * normal[2]);
      const double length = std::sqrt((decoded[0] * decoded[0]) + (decoded[1] * decoded[1]) + (decoded[2] * decoded[2]));
      for (double& value : decoded) value /= length;
    }
    const auto cross = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
      return std::array<double, 3>{(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
    };
    std::array<double, 3> u = cross(decoded, {0., 0., 1.});
    const double u_length = std::sqrt((u[0] * u[0]) + (u[1] * u[1]) + (u[2] * u[2]));
    for (double& value : u) value /= u_length;
    const std::array<double, 3> v = cross(decoded, u);
    std::vector<uint32_t> base(pixels);
    std::vector<uint32_t> surface(pixels);
    std::vector<float> depth(pixels, .5f);
    std::vector<Texel> positions(pixels);
    std::vector<Texel> resolved_positions(pixels);
    std::vector<std::array<uint16_t, 4>> resolved(pixels);
    const std::array<float, 3> light_a = {.5f, 1.f, 2.f};
    const std::array<float, 3> light_b = {2.f, .25f, 1.f};
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) {
        const size_t at = (static_cast<size_t>(y) * WIDTH) + x;
        const uint32_t seed = static_cast<uint32_t>(at) * 2654435761u;
        base[at] = (seed >> 8) | 0xFF000000u;
        const uint32_t model = ((seed >> 3) % 8 == 0) ? 1u : ((seed >> 5) % 3 == 0 ? 2u : 0u);
        surface[at] = 0x80u | (0x80u << 8) | (((seed >> 13) & 0xFFu) << 16) | (((model << 1) | ((seed >> 21) & 1u)) << 24);
        const bool sky_pixel = (seed >> 17) % 10 == 0;
        if (sky_pixel) depth[at] = 1.f;
        Texel position = {};
        for (int axis = 0; axis < 3; ++axis) {
          position[axis] = static_cast<float>(std::array<double, 3>{100., 5., -40.}[axis] + (((x * u[axis]) + (y * v[axis])) * .5));
        }
        position[3] = 10.f;
        positions[at] = position;
        resolved_positions[at] = position;
        if (sky_pixel) resolved_positions[at][3] = 0.f;
        const auto& light = ((x + y) % 2 == 0) ? light_a : light_b;
        resolved[at] = {ToHalf(light[0]), ToHalf(light[1]), ToHalf(light[2]), 0};
      }
    }
    const auto plane = make_gbuffer(std::vector<std::array<uint16_t, 2>>(pixels, stored), base, surface, depth);
    const auto white = draw_sky(plane, white_cube.Get(), white_cube.Get(), exposure_one.Get(), false);
    const std::vector<Texel>& white_diffuse = white.second;
    const auto resolved_srv = Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, resolved.data(), WIDTH * 8).Get());
    const auto resolved_positions_srv =
        Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, resolved_positions.data(), WIDTH * 16).Get());
    const auto positions_srv = Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, positions.data(), WIDTH * 16).Get());
    const auto scene = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_RENDER_TARGET);
    const auto scene_rtv = Rtv(scene.Get());
    constexpr float STRENGTH = 1.5f;
    constexpr Texel SCENE = {.1f, .2f, .3f, .7f};
    // The apply's constants (ray_tracing.hpp LightBounce's: sizes, settings, the View's rows).
    struct ApplyConstants {
      std::array<uint32_t, 4> sizes;
      std::array<float, 4> settings;
      std::array<float, 16> rows;
    };
    const auto apply = [&](bool transposed) {
      ApplyConstants constants = {.sizes = {WIDTH, HEIGHT, WIDTH, HEIGHT}, .settings = {STRENGTH, 1.f}};
      for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
          constants.rows[(row * 4) + column] = static_cast<float>(transposed ? rotation[column][row] : rotation[row][column]);
        }
      }
      context->UpdateSubresource(objects.bounce_constants.Get(), 0, nullptr, &constants, 0, 0);
      context->ClearRenderTargetView(scene_rtv.Get(), SCENE.data());
      const std::array<ID3D11ShaderResourceView*, 7> views = {resolved_srv.Get(), resolved_positions_srv.Get(), positions_srv.Get(), plane.normals.Get(),
                                                              plane.base.Get(),   plane.surface.Get(),            plane.depth.Get()};
      rays::internal::FullScreen(context.Get(), scene_rtv.Get(), {WIDTH, HEIGHT}, objects.bounce_apply.Get(), views, objects.bounce_constants.Get(),
                                 objects.add.Get());
      return ReadBack<Texel>(scene.Get(), WIDTH, HEIGHT);
    };
    const auto compare = [&](const std::vector<Texel>& out, bool* alpha_kept) {
      double worst = 0.;
      *alpha_kept = true;
      for (UINT y = 0; y < HEIGHT; ++y) {
        for (UINT x = 0; x < WIDTH; ++x) {
          const size_t at = (static_cast<size_t>(y) * WIDTH) + x;
          std::array<double, 3> light = {};
          double weights = 0.;
          for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
              const int tx = static_cast<int>(x) + dx;
              const int ty = static_cast<int>(y) + dy;
              if (tx < 0 || ty < 0 || tx >= static_cast<int>(WIDTH) || ty >= static_cast<int>(HEIGHT)) continue;
              const size_t there = (static_cast<size_t>(ty) * WIDTH) + tx;
              if (resolved_positions[there][3] <= 0.f) continue;
              const double weight = std::exp(-static_cast<double>((dx * dx) + (dy * dy)));
              for (int channel = 0; channel < 3; ++channel) light[channel] += Half(resolved[there][channel]) * weight;
              weights += weight;
            }
          }
          for (int channel = 0; channel < 3; ++channel) {
            const double factor = std::max(0.f, white_diffuse[at][channel]);
            const double expected = (weights > 0. ? light[channel] / weights : 0.) * factor * STRENGTH;
            const double added = static_cast<double>(out[at][channel]) - SCENE[channel];
            worst = std::max(worst, std::abs(added - expected) / std::max(expected, 1e-3));
          }
          *alpha_kept = *alpha_kept && out[at][3] == SCENE[3];
        }
      }
      return worst;
    };
    bool alpha_kept = false;
    const double apply_worst = compare(apply(false), &alpha_kept);
    bool transposed_alpha = false;
    const double transposed_worst = compare(apply(true), &transposed_alpha);
    const size_t unlit = static_cast<size_t>(std::ranges::count_if(white_diffuse, [](const Texel& texel) { return texel[0] == 0.f && texel[1] == 0.f && texel[2] == 0.f; }));
    std::printf("GI apply: the resolved light x the game's diffuse factor (%zu of %zu pixels 0: sky, unlit or black) x %.1f added within "
                "%.2e, alpha %s; CONTROL the view's rows transposed: %.3f off\n",
                unlit, pixels, STRENGTH, apply_worst, (alpha_kept ? "kept" : "CHANGED"), transposed_worst);
    CHECK(apply_worst < 1e-4 && alpha_kept && unlit > pixels / 10);
    CHECK(transposed_worst > .05);

    // d. KeepSun at the lighting draw: its target the display's size (its t0 the 1 x 1 exposure); CONTROL: another size.
    std::array<float, 32> per_frame = {};
    const std::array<float, 3> toward = {-.4f, .7f, .2f};
    for (int axis = 0; axis < 3; ++axis) per_frame[axis * 4] = toward[axis];
    const auto frame_constants = MakeBuffer(128, D3D11_USAGE_DEFAULT);
    context->UpdateSubresource(frame_constants.Get(), 0, nullptr, per_frame.data(), 0, 0);
    const auto keep_sun = [&](UINT width, UINT height) {
      const std::array<Texel, 16> zeros = {};
      const auto sun_texture = MakeTexture(16, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, zeros.data(), 16 * 16);
      bridge.rt_sun.uav = Uav(sun_texture.Get());
      const auto target = MakeTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET);
      const auto target_rtv = Rtv(target.Get());
      context->OMSetRenderTargets(1, target_rtv.GetAddressOf(), nullptr);
      context->PSSetShaderResources(0, 1, exposure.GetAddressOf());
      context->PSSetConstantBuffers(13, 1, frame_constants.GetAddressOf());
      rays::OnLighting(&mock_list);
      ID3D11ShaderResourceView* no_srv = nullptr;
      context->PSSetShaderResources(0, 1, &no_srv);
      context->OMSetRenderTargets(0, nullptr, nullptr);
      return ReadBack<Texel>(sun_texture.Get(), 16, 1)[0];
    };
    const Texel kept = keep_sun(WIDTH, HEIGHT);
    const Texel other = keep_sun(WIDTH / 2, HEIGHT / 2);
    const double toward_length = std::sqrt((toward[0] * toward[0]) + (toward[1] * toward[1]) + (toward[2] * toward[2]));
    double kept_worst = 0.;
    for (int axis = 0; axis < 3; ++axis) kept_worst = std::max(kept_worst, std::abs(kept[axis] - (toward[axis] / toward_length)));
    std::printf("KeepSun: the lighting draw's sun within %.2e; CONTROL another size: %s\n", kept_worst, (other == Texel{} ? "nothing written" : "WRITTEN"));
    CHECK(kept_worst < 1e-5 && kept[3] == 1.f);
    CHECK(other == Texel{});

    // e. The G-buffer guides at the lighting draw (OnLighting, the sky's LUT kept at its draw): against the game's own
    //    DeferredSky under white cubes (its o1 the diffuse colour, its o0 the specular colour times 1), the decoded world
    //    normal and the roughness it lights with. CONTROLS: the specular without the LUT (F0 alone), the normal turned by
    //    the rotation instead of its transpose; written once a frame.
    Matrix view_matrix = {};
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 3; ++column) view_matrix[(row * 4) + column] = rotation[row][column];
      view_matrix[(row * 4) + 3] = -((rotation[row][0] * EYE[0]) + (rotation[row][1] * EYE[1]) + (rotation[row][2] * EYE[2]));
    }
    view_matrix[15] = 1.;
    constexpr double NEAR_PLANE = .1;
    constexpr double FAR_PLANE = 100.;
    const Matrix projection = {P00, 0., 0., 0., 0., P11, 0., 0., 0., 0., FAR_PLANE / (FAR_PLANE - NEAR_PLANE), -NEAR_PLANE * FAR_PLANE / (FAR_PLANE - NEAR_PLANE),
                               0., 0., 1., 0.};
    const Matrix view_projection = Multiply(projection, view_matrix);
    const Matrix inverse_view_projection = Inverse(view_projection);
    dlaa::internal::Camera camera = {};
    for (int index = 0; index < 16; ++index) {
      camera.view[index] = static_cast<float>(view_matrix[index]);
      camera.projection[index] = static_cast<float>(projection[index]);
      camera.view_projection[index] = static_cast<float>(view_projection[index]);
      camera.jittered_view_projection[index] = static_cast<float>(view_projection[index]);
      camera.inverse_view_projection[index] = static_cast<float>(inverse_view_projection[index]);
    }
    camera.position = {static_cast<float>(EYE[0]), static_cast<float>(EYE[1]), static_cast<float>(EYE[2])};
    camera.size = {WIDTH, HEIGHT};
    camera.frame = state.frame;
    state.camera = camera;
    const auto make_guide = [&](DXGI_FORMAT format) {
      twattila::dlaa_bridge::Texture guide;
      guide.texture = MakeTexture(WIDTH, HEIGHT, format, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
      guide.srv = Srv(guide.texture.Get());
      guide.uav = Uav(guide.texture.Get());
      return guide;
    };
    bridge.diffuse_albedo = make_guide(DXGI_FORMAT_R8G8B8A8_UNORM);
    bridge.specular_albedo = make_guide(DXGI_FORMAT_R8G8B8A8_UNORM);
    bridge.normals = make_guide(DXGI_FORMAT_R16G16B16A16_FLOAT);
    bridge.roughness = make_guide(DXGI_FORMAT_R8_UNORM);
    bridge.guides = true;
    dlaa::guides = true;
    rays::internal::guides_written = false;
    const auto white_sky = draw_sky(plane, white_cube.Get(), white_cube.Get(), exposure_one.Get(), true);
    const auto lighting_target = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET);
    const auto lighting_rtv = Rtv(lighting_target.Get());
    const auto lighting = [&] {
      context->OMSetRenderTargets(1, lighting_rtv.GetAddressOf(), nullptr);
      const std::array<ID3D11ShaderResourceView*, 7> lighting_views = {exposure_one.Get(), nullptr, plane.normals.Get(), plane.base.Get(),
                                                                       plane.surface.Get(), nullptr, plane.depth.Get()};
      context->PSSetShaderResources(0, static_cast<UINT>(lighting_views.size()), lighting_views.data());
      context->PSSetConstantBuffers(13, 1, frame_constants.GetAddressOf());
      rays::OnLighting(&mock_list);
      const std::array<ID3D11ShaderResourceView*, 7> no_lighting_views = {};
      context->PSSetShaderResources(0, static_cast<UINT>(no_lighting_views.size()), no_lighting_views.data());
      context->OMSetRenderTargets(0, nullptr, nullptr);
    };
    lighting();
    const bool noted = rays::internal::guides_written.load() && state.guides_frame == state.frame;
    const auto diffuse_guide = ReadBack<uint32_t>(bridge.diffuse_albedo.texture.Get(), WIDTH, HEIGHT);
    const auto specular_guide = ReadBack<uint32_t>(bridge.specular_albedo.texture.Get(), WIDTH, HEIGHT);
    const auto normal_guide = ReadBack<std::array<uint16_t, 4>>(bridge.normals.texture.Get(), WIDTH, HEIGHT);
    const auto roughness_guide = ReadBack<uint8_t>(bridge.roughness.texture.Get(), WIDTH, HEIGHT);
    const auto unorm = [](uint32_t packed, int channel) { return static_cast<double>((packed >> (8 * channel)) & 0xFFu) / 255.; };
    const std::array<double, 3> view_normal_stored = [&] {
      const float ex = (Half(stored[0]) * 3.5554f) - 1.7777f;
      const float ey = (Half(stored[1]) * 3.5554f) - 1.7777f;
      const float g = 2.f / ((ex * ex) + (ey * ey) + 1.f);
      return std::array<double, 3>{g * ex, g * ey, g - 1.f};
    }();
    std::array<double, 3> turned = {};
    for (int axis = 0; axis < 3; ++axis) {
      turned[axis] = (rotation[axis][0] * view_normal_stored[0]) + (rotation[axis][1] * view_normal_stored[1]) + (rotation[axis][2] * view_normal_stored[2]);
    }
    const double turned_length = std::sqrt((turned[0] * turned[0]) + (turned[1] * turned[1]) + (turned[2] * turned[2]));
    double guide_diffuse_worst = 0.;
    double guide_specular_worst = 0.;
    double guide_normal_worst = 0.;
    double guide_roughness_worst = 0.;
    double f0_worst = 0.;
    double turned_nearest = 1e30;
    bool unlit_zero = true;
    size_t lit_pixels = 0;
    for (size_t at = 0; at < pixels; ++at) {
      const uint32_t bits = surface[at] >> 24;
      const bool lit = depth[at] != 1.f && ((bits >> 1) & 3u) != 1u;
      if (!lit) {
        unlit_zero = unlit_zero && diffuse_guide[at] == 0u && specular_guide[at] == 0u && roughness_guide[at] == 255u
                     && normal_guide[at] == std::array<uint16_t, 4>{};
        continue;
      }
      ++lit_pixels;
      const double base_metal = static_cast<double>((surface[at] >> 16) & 0xFFu) / 255.;
      const double reflectance = static_cast<double>((surface[at] >> 8) & 0xFFu) / 255.;
      double f0_off = 0.;
      for (int channel = 0; channel < 3; ++channel) {
        guide_diffuse_worst = std::max(guide_diffuse_worst, std::abs(unorm(diffuse_guide[at], channel) - std::clamp<double>(white.second[at][channel], 0., 1.)));
        const double game_specular = std::clamp<double>(white_sky.first[at][channel], 0., 1.);
        guide_specular_worst = std::max(guide_specular_worst, std::abs(unorm(specular_guide[at], channel) - game_specular));
        const double base_channel = static_cast<double>((base[at] >> (8 * channel)) & 0xFFu) / 255.;
        const double f0 = (base_metal * base_channel) + ((1. - base_metal) * .16 * reflectance * reflectance);
        f0_off = std::max(f0_off, std::abs(f0 - game_specular));
        guide_normal_worst = std::max(guide_normal_worst, std::abs(Half(normal_guide[at][channel]) - decoded[channel]));
      }
      f0_worst = std::max(f0_worst, f0_off);
      double turned_off = 0.;
      for (int channel = 0; channel < 3; ++channel) turned_off = std::max(turned_off, std::abs(Half(normal_guide[at][channel]) - (turned[channel] / turned_length)));
      turned_nearest = std::min(turned_nearest, turned_off);
      const double roughness = std::max(.09, static_cast<double>(surface[at] & 0xFFu) / 255.);
      guide_roughness_worst = std::max(guide_roughness_worst, std::abs((roughness_guide[at] / 255.) - roughness));
    }
    // Once a frame: a second lighting draw leaves them.
    const std::array<float, 4> red = {1.f, 0.f, 0.f, 1.f};
    context->ClearUnorderedAccessViewFloat(bridge.diffuse_albedo.uav.Get(), red.data());
    lighting();
    const bool once = ReadBack<uint32_t>(bridge.diffuse_albedo.texture.Get(), WIDTH, HEIGHT)[0] == 0xFF0000FFu;
    std::printf("guides: %zu lit pixels; diffuse within %.4f of the game's o1, specular within %.4f of its o0 (a unorm step %.4f), normal within "
                "%.2e, roughness within %.4f, sky / unlit %s, noted %d, once a frame %d; CONTROLS F0 without the LUT %.3f off at the worst, "
                "the rotation instead of its transpose %.3f\n",
                lit_pixels, guide_diffuse_worst, guide_specular_worst, 1. / 255., guide_normal_worst, guide_roughness_worst,
                (unlit_zero ? "none" : "WRITTEN"), noted, once, f0_worst, turned_nearest);
    CHECK(lit_pixels > pixels / 2 && unlit_zero && noted && once);
    CHECK(guide_diffuse_worst <= .5 / 255. + 1e-4 && guide_specular_worst <= 1. / 255. && guide_normal_worst < 2e-3 && guide_roughness_worst <= .5 / 255. + 1e-6);
    CHECK(f0_worst > .05 && turned_nearest > .1);
    bridge.guides = false;
    dlaa::guides = false;

    // f. The rays' reflections into the IBL specular at the lighting draw (OnLighting; rt_core's Reflections answering
    //    with this frame's textures as after a trace): over the game's own o0 under a white specular cube (its specular
    //    colour x 1), the rays' light 1 met everywhere gives the game's o0 again (our specular colour = the game's); light 2
    //    met at a share a gives o0 x (1 + a); a share 0 leaves it bit for bit; Specular Occlusion alone (the reflections
    //    off) dims it by the open share. CONTROL: the View's rotation transposed (another normal, another N.V on the LUT).
    const auto& game_specular = white_sky.first;
    const auto specular_ibl = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    const auto specular_ibl_srv = Srv(specular_ibl.Get());
    std::vector<std::array<uint16_t, 4>> reflection_texels(pixels);
    std::vector<uint8_t> open_texels(pixels);
    const auto make_traced = [&] {
      twattila::dlaa_bridge::Texture traced;
      traced.texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, reflection_texels.data(), WIDTH * 8);
      traced.srv = Srv(traced.texture.Get());
      return traced;
    };
    const auto reflect = [&](float light, const std::vector<float>& share, float open, bool reflections) {
      for (size_t at = 0; at < pixels; ++at) {
        reflection_texels[at] = {ToHalf(light), ToHalf(light), ToHalf(light), ToHalf(share[at])};
        open_texels[at] = static_cast<uint8_t>(std::lround(open * 255.f));
      }
      bridge.rt_reflection = make_traced();
      bridge.rt_specular.texture = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, open_texels.data(), WIDTH);
      bridge.rt_specular.srv = Srv(bridge.rt_specular.texture.Get());
      context->UpdateSubresource(specular_ibl.Get(), 0, nullptr, game_specular.data(), WIDTH * 16, 0);
      runtime.settings.reflections = reflections;
      runtime.settings.specular_occlusion = !reflections;
      runtime.reflected_frame = runtime.frame;
      runtime.reflected = true;
      ray_state.settings.reflections = reflections;
      ray_state.settings.specular_occlusion = !reflections;
      ray_state.reflected_frame = UINT64_MAX;
      context->OMSetRenderTargets(1, lighting_rtv.GetAddressOf(), nullptr);
      const std::array<ID3D11ShaderResourceView*, 8> lighting_views = {exposure_one.Get(), nullptr, plane.normals.Get(), plane.base.Get(),
                                                                       plane.surface.Get(), nullptr, plane.depth.Get(), specular_ibl_srv.Get()};
      context->PSSetShaderResources(0, static_cast<UINT>(lighting_views.size()), lighting_views.data());
      context->PSSetConstantBuffers(13, 1, frame_constants.GetAddressOf());
      rays::OnLighting(&mock_list);
      const std::array<ID3D11ShaderResourceView*, 8> no_lighting_views = {};
      context->PSSetShaderResources(0, static_cast<UINT>(no_lighting_views.size()), no_lighting_views.data());
      context->OMSetRenderTargets(0, nullptr, nullptr);
      return ReadBack<Texel>(specular_ibl.Get(), WIDTH, HEIGHT);
    };
    // The worst of |out - expected| relative to the expected (at least 1e-3), RGB, over every pixel.
    const auto off = [&](const std::vector<Texel>& out, const auto& expected) {
      double worst = 0.;
      for (size_t at = 0; at < pixels; ++at) {
        for (int channel = 0; channel < 3; ++channel) {
          const double want = expected(at, channel);
          worst = std::max(worst, std::abs(out[at][channel] - want) / std::max(std::abs(want), 1e-3));
        }
      }
      return worst;
    };
    std::vector<float> shares(pixels);
    for (size_t at = 0; at < pixels; ++at) shares[at] = Half(ToHalf(static_cast<float>((at * 2654435761u) % 1000u) / 999.f));
    const auto game_of = [&](size_t at, int channel) { return static_cast<double>(game_specular[at][channel]); };
    const double met_off = off(reflect(1.f, std::vector<float>(pixels, 1.f), 1.f, true), game_of);
    const double share_off = off(reflect(2.f, shares, 1.f, true), [&](size_t at, int channel) { return game_of(at, channel) * (1. + shares[at]); });
    const bool none_kept = reflect(2.f, std::vector<float>(pixels, 0.f), 1.f, true) == game_specular;
    const float open_value = 64.f / 255.f;
    const double open_off = off(reflect(2.f, shares, open_value, false), [&](size_t at, int channel) { return game_of(at, channel) * open_value; });
    // CONTROL: the View's rotation transposed.
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 3; ++column) state.camera.view[(row * 4) + column] = static_cast<float>(rotation[column][row]);
    }
    const double transposed_off = off(reflect(1.f, std::vector<float>(pixels, 1.f), 1.f, true), game_of);
    state.camera = camera;
    std::printf("reflections: met everywhere at the game's light, the game's o0 within %.2e; light 2 at a share a, o0 x (1 + a) within %.2e; "
                "share 0 %s; Specular Occlusion alone, o0 x the open share within %.2e; CONTROL the rotation transposed %.3f off\n",
                met_off, share_off, (none_kept ? "bit for bit" : "CHANGED"), open_off, transposed_off);
    CHECK(met_off < 1e-4 && share_off < 1e-4 && none_kept && open_off < 1e-4);
    CHECK(transposed_off > .01);

    // g. DeferredSSR and DeferredSSRCombine (0x0FA22D5A, the census's bytes) under the rays' reflections: the march not
    //    drawn, the combine's scene into its target bit for bit in its place. CONTROLS: the game's combine changes the
    //    scene; Ray-Traced Reflections off: both the game's.
    std::vector<uint8_t> combine_code;
    {
      std::ifstream file(census + "0x0FA22D5A.ps_5_0.cso", std::ios::binary);
      combine_code.assign(std::istreambuf_iterator<char>(file), {});
    }
    REQUIRE(!combine_code.empty() && renodx::utils::hash::ComputeCRC32(combine_code.data(), combine_code.size()) == 0x0FA22D5A);
    ComPtr<ID3D11PixelShader> combine;
    REQUIRE(SUCCEEDED(device->CreatePixelShader(combine_code.data(), combine_code.size(), nullptr, combine.GetAddressOf())));
    std::vector<Texel> scene_texels(pixels);
    std::vector<Texel> marched_texels(pixels);
    for (size_t at = 0; at < pixels; ++at) {
      for (int channel = 0; channel < 3; ++channel) {
        scene_texels[at][channel] = static_cast<float>(((at * 7) + (channel * 13)) % 97) / 31.f;
        marched_texels[at][channel] = static_cast<float>(((at * 5) + (channel * 3)) % 89) / 17.f;
      }
      scene_texels[at][3] = 1.f;
      marched_texels[at][3] = 1.f;
    }
    const auto scene_srv = Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, scene_texels.data(), WIDTH * 16).Get());
    const auto marched_srv = Srv(MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, marched_texels.data(), WIDTH * 16).Get());
    const auto combined = MakeTexture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_RENDER_TARGET);
    const auto combined_rtv = Rtv(combined.Get());
    ray_state.settings.reflections = true;
    const auto run_combine = [&](bool hooked, bool* replaced) {
      constexpr std::array<float, 4> CLEAR = {-1.f, -1.f, -1.f, -1.f};
      context->ClearRenderTargetView(combined_rtv.Get(), CLEAR.data());
      const std::array<ID3D11ShaderResourceView*, 4> views = {scene_srv.Get(), marched_srv.Get(), marched_srv.Get(), plane.surface.Get()};
      const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(WIDTH), .Height = static_cast<float>(HEIGHT), .MaxDepth = 1.f};
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(nullptr);
      context->VSSetShader(sky_vertex.Get(), nullptr, 0);
      context->PSSetShader(combine.Get(), nullptr, 0);
      context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
      context->PSSetSamplers(0, 1, sampler.GetAddressOf());
      context->RSSetState(nullptr);
      context->RSSetViewports(1, &viewport);
      context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      context->OMSetRenderTargets(1, combined_rtv.GetAddressOf(), nullptr);
      *replaced = hooked && rays::ReplaceReflectionCombine(&mock_list);
      if (!*replaced) context->Draw(3, 0);
      const std::array<ID3D11ShaderResourceView*, 4> no_views = {};
      context->PSSetShaderResources(0, static_cast<UINT>(no_views.size()), no_views.data());
      context->OMSetRenderTargets(0, nullptr, nullptr);
      return ReadBack<Texel>(combined.Get(), WIDTH, HEIGHT);
    };
    const auto march_skipped = [&] {
      context->PSSetShaderResources(0, 1, plane.normals.GetAddressOf());
      const bool skipped = rays::SkipReflectionMarch(&mock_list);
      ID3D11ShaderResourceView* no_view = nullptr;
      context->PSSetShaderResources(0, 1, &no_view);
      return skipped;
    };
    bool replaced = false;
    const auto copied = run_combine(true, &replaced);
    const bool skipped = march_skipped();
    bool game_replaced = true;
    const auto game_combined = run_combine(false, &game_replaced);
    ray_state.settings.reflections = false;
    bool off_replaced = true;
    run_combine(true, &off_replaced);
    const bool off_skipped = march_skipped();
    const bool copy_exact = std::ranges::equal(copied, scene_texels);
    double game_changed = 0.;
    for (size_t at = 0; at < pixels; ++at) {
      for (int channel = 0; channel < 3; ++channel) game_changed = std::max(game_changed, static_cast<double>(std::abs(game_combined[at][channel] - scene_texels[at][channel])));
    }
    std::printf("screen-space reflections under the rays': march skipped %d, combine replaced %d with its scene %s; CONTROLS the game's combine "
                "changes the scene by up to %.3f, the reflections off: skipped %d, replaced %d\n",
                skipped, replaced, (copy_exact ? "bit for bit" : "DIFFERENT"), game_changed, off_skipped, off_replaced);
    CHECK(skipped && replaced && copy_exact);
    CHECK(!game_replaced && game_changed > .1 && !off_skipped && !off_replaced);
    runtime.settings.reflections = false;
    runtime.settings.specular_occlusion = false;
    runtime.reflected = false;
    ray_state.settings.specular_occlusion = false;
    bridge.rt_reflection = {};
    bridge.rt_specular = {};

    bridge.rt_sun.uav.Reset();
    bridge.rt_visibility.srv.Reset();
    runtime.path_ambient_on = false;
    ray_state.settings.path_ambient = false;
    dlaa::rays = false;
  }

  // 10. GT-VBAO in the game's SSAO's place.
  {
    namespace screen = divinity2::screen_space;
    constexpr UINT AO_WIDTH = 256;
    constexpr UINT AO_HEIGHT = 144;
    constexpr size_t AO_PIXELS = static_cast<size_t>(AO_WIDTH) * AO_HEIGHT;
    const std::string census = std::string(argv[1]).substr(0, std::string(argv[1]).find_last_of("\\/") + 1);
    const auto game_shader = [&](const std::string& name, uint32_t hash) {
      std::vector<uint8_t> code;
      {
        std::ifstream file(census + name, std::ios::binary);
        code.assign(std::istreambuf_iterator<char>(file), {});
      }
      REQUIRE(!code.empty() && renodx::utils::hash::ComputeCRC32(code.data(), code.size()) == hash);
      ComPtr<ID3D11PixelShader> shader;
      REQUIRE(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf())));
      return shader;
    };
    const auto reinterleave = game_shader("0x2CA00ABD.ps_5_0.cso", 0x2CA00ABD);
    const auto blur = game_shader("0x6FFB511E.ps_5_0.cso", 0x6FFB511E);
    // The post passes' vertex outputs as the game's pixel shaders read them: SV_Position, TexCoord0 (uv).
    const char* vertex_source =
        "void main(uint id : SV_VertexID, out float4 position : SV_Position0, out float2 uv : TexCoord0) {"
        " uv = float2((id << 1) & 2, id & 2); position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }";
    ComPtr<ID3DBlob> vertex_blob;
    REQUIRE(SUCCEEDED(D3DCompile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_blob.GetAddressOf(), nullptr)));
    ComPtr<ID3D11VertexShader> post_vertex;
    REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr, post_vertex.GetAddressOf())));
    const D3D11_SAMPLER_DESC point_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
    ComPtr<ID3D11SamplerState> point_clamp;
    REQUIRE(SUCCEEDED(device->CreateSamplerState(&point_desc, point_clamp.GetAddressOf())));
    // The scene through CITY's camera (unjittered): the ground 15 m below the eye, a wall facing the camera 2 m past where
    // the screen's centre ray meets the ground. LinearizeDepth's view z / 100; GBuffer1 as the game's G-buffer writes it
    // (View's rows times the world normal, n.xy / (1 + n.z) x 0.281262308 + 0.5), and the world normal so encoded.
    const Matrix view = Rows(CITY.data());
    const Matrix to_world = Inverse(Rows(CITY.data() + 32));
    const std::array<double, 3> eye = {CITY[48], CITY[49], CITY[50]};
    const double ground_y = eye[1] - 15.;
    const auto ray_of = [&](double x, double y) {
      const auto near_point = Apply(to_world, {x, y, 0., 1.});
      const auto far_point = Apply(to_world, {x, y, 1., 1.});
      std::array<double, 3> direction = {};
      for (int k = 0; k < 3; ++k) direction[k] = (far_point[k] / far_point[3]) - (near_point[k] / near_point[3]);
      const double length = std::sqrt((direction[0] * direction[0]) + (direction[1] * direction[1]) + (direction[2] * direction[2]));
      for (double& value : direction) value /= length;
      return direction;
    };
    const double across_length = std::hypot(CITY[8], CITY[10]);
    const std::array<double, 3> across = {CITY[8] / across_length, 0., CITY[10] / across_length};
    const auto centre_ray = ray_of(0., 0.);
    const double centre_t = (ground_y - eye[1]) / centre_ray[1];
    std::array<double, 3> wall_point = {};
    for (int k = 0; k < 3; ++k) wall_point[k] = eye[k] + (centre_t * centre_ray[k]) + (2. * across[k]);
    const std::array<double, 3> wall_normal = {-across[0], 0., -across[2]};
    const auto dot3 = [](const std::array<double, 3>& a, const std::array<double, 3>& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); };
    std::vector<float> depth_texels(AO_PIXELS, 1.f);
    std::vector<std::array<uint16_t, 2>> normal_texels(AO_PIXELS);
    std::vector<std::array<uint16_t, 2>> world_texels(AO_PIXELS);
    std::vector<int> kind(AO_PIXELS, 0);  // 1 the ground 3 m or more from the wall (open), 2 within 0.25 m of the corner
    for (UINT py = 0; py < AO_HEIGHT; ++py) {
      for (UINT px = 0; px < AO_WIDTH; ++px) {
        const auto direction = ray_of(((px + .5) / AO_WIDTH * 2.) - 1., 1. - ((py + .5) / AO_HEIGHT * 2.));
        double t = 1e30;
        std::array<double, 3> normal = {0., 1., 0.};
        if (direction[1] < 0.) t = (ground_y - eye[1]) / direction[1];
        const double facing = dot3(direction, wall_normal);
        bool on_wall = false;
        if (facing < 0.) {
          const double wall_t = dot3({wall_point[0] - eye[0], wall_point[1] - eye[1], wall_point[2] - eye[2]}, wall_normal) / facing;
          if (wall_t > 0. && wall_t < t) {
            t = wall_t;
            normal = wall_normal;
            on_wall = true;
          }
        }
        if (t > 1e29) continue;
        const std::array<double, 3> hit = {eye[0] + (t * direction[0]), eye[1] + (t * direction[1]), eye[2] + (t * direction[2])};
        const size_t at = (static_cast<size_t>(py) * AO_WIDTH) + px;
        depth_texels[at] = static_cast<float>(((view[8] * hit[0]) + (view[9] * hit[1]) + (view[10] * hit[2]) + view[11]) / 100.);
        const auto encode = [&](bool rotated) {
          std::array<double, 3> n = normal;
          if (rotated) {
            for (int row = 0; row < 3; ++row) n[row] = (view[row * 4] * normal[0]) + (view[(row * 4) + 1] * normal[1]) + (view[(row * 4) + 2] * normal[2]);
          }
          return std::array<uint16_t, 2>{ToHalf(static_cast<float>((n[0] / (1. + n[2]) * 0.281262308) + .5)),
                                          ToHalf(static_cast<float>((n[1] / (1. + n[2]) * 0.281262308) + .5))};
        };
        normal_texels[at] = encode(true);
        world_texels[at] = encode(false);
        if (on_wall) {
          if (hit[1] - ground_y < .25) kind[at] = 2;
        } else {
          const double from_wall = dot3({hit[0] - wall_point[0], hit[1] - wall_point[1], hit[2] - wall_point[2]}, wall_normal);
          kind[at] = (from_wall >= 3.) ? 1 : ((from_wall < .25) ? 2 : 0);
        }
      }
    }
    const auto depth = Srv(MakeTexture(AO_WIDTH, AO_HEIGHT, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, depth_texels.data(), AO_WIDTH * 4).Get());
    const auto normals = Srv(MakeTexture(AO_WIDTH, AO_HEIGHT, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE, normal_texels.data(), AO_WIDTH * 4).Get());
    const auto world_normals = Srv(MakeTexture(AO_WIDTH, AO_HEIGHT, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE, world_texels.data(), AO_WIDTH * 4).Get());
    // PPSAO's four slices (R8, half size): 0.5, what the game's reinterleave writes as its AO.
    const std::vector<uint8_t> slice_texels(static_cast<size_t>(AO_WIDTH / 2) * (AO_HEIGHT / 2), 128);
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> slices;
    for (auto& slice : slices) slice = Srv(MakeTexture(AO_WIDTH / 2, AO_HEIGHT / 2, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, slice_texels.data(), AO_WIDTH / 2).Get());
    PerView ao_view = CITY;
    ao_view[54] = static_cast<float>(AO_WIDTH);
    ao_view[55] = static_cast<float>(AO_HEIGHT);
    const auto constant_buffer = [&](const void* data, UINT bytes) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
      ComPtr<ID3D11Buffer> buffer;
      REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &initial, buffer.GetAddressOf())));
      return buffer;
    };
    const auto per_view = constant_buffer(ao_view.data(), 240);
    const auto other_buffer = constant_buffer(CITY.data(), 240);
    const auto target_of = [&] {
      return MakeTexture(AO_WIDTH, AO_HEIGHT, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    };
    const auto first = target_of();
    const auto second = target_of();
    const auto third = target_of();
    const D3D11_VIEWPORT ao_viewport = {.Width = AO_WIDTH, .Height = AO_HEIGHT, .MaxDepth = 1.f};
    const auto post_pass = [&](ID3D11PixelShader* shader, std::span<ID3D11ShaderResourceView* const> views, ID3D11Texture2D* target) {
      const auto rtv = Rtv(target);
      context->IASetInputLayout(nullptr);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(post_vertex.Get(), nullptr, 0);
      context->PSSetShader(shader, nullptr, 0);
      context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
      context->PSSetSamplers(0, 1, point_clamp.GetAddressOf());
      context->RSSetState(nullptr);
      context->RSSetViewports(1, &ao_viewport);
      context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    };
    const auto unbind = [&] {
      const std::array<ID3D11ShaderResourceView*, 6> no_views = {};
      context->PSSetShaderResources(0, static_cast<UINT>(no_views.size()), no_views.data());
      context->OMSetRenderTargets(0, nullptr, nullptr);
      ID3D11Buffer* no_buffer = nullptr;
      context->PSSetConstantBuffers(12, 1, &no_buffer);
    };
    // PPSAO's four draws (GBuffer1 at t0, PerView at b12) through SkipSao, then the reinterleave's draw (its depth, slices,
    // point sampler and target; another buffer at b12) through OnReinterleave / OnReinterleaveDrawn: the texels, how many
    // PPSAO draws were skipped, and whether the game's shader and bindings are back after.
    struct AoRun {
      std::vector<uint32_t> texels;
      int skipped = 0;
      bool restored = false;
    };
    const auto run_chain = [&](ID3D11ShaderResourceView* gbuffer1, float mode) {
      AoRun run;
      screen::ao.mode = mode;
      context->PSSetShaderResources(0, 1, &gbuffer1);
      context->PSSetConstantBuffers(12, 1, per_view.GetAddressOf());
      for (int draw = 0; draw < 4; ++draw) run.skipped += screen::SkipSao(&mock_list) ? 1 : 0;
      context->PSSetConstantBuffers(12, 1, other_buffer.GetAddressOf());
      const std::array<ID3D11ShaderResourceView*, 5> views = {depth.Get(), slices[0].Get(), slices[1].Get(), slices[2].Get(), slices[3].Get()};
      post_pass(reinterleave.Get(), views, first.Get());
      screen::OnReinterleave(&mock_list);
      context->Draw(3, 0);
      screen::OnReinterleaveDrawn(&mock_list);
      ComPtr<ID3D11PixelShader> bound;
      context->PSGetShader(bound.GetAddressOf(), nullptr, nullptr);
      ComPtr<ID3D11ShaderResourceView> normals_slot;
      context->PSGetShaderResources(5, 1, normals_slot.GetAddressOf());
      ComPtr<ID3D11Buffer> settings_slot;
      ComPtr<ID3D11Buffer> view_slot;
      context->PSGetConstantBuffers(1, 1, settings_slot.GetAddressOf());
      context->PSGetConstantBuffers(12, 1, view_slot.GetAddressOf());
      run.restored = bound.Get() == reinterleave.Get() && !normals_slot && !settings_slot && view_slot.Get() == other_buffer.Get();
      unbind();
      run.texels = ReadBack<uint32_t>(first.Get(), AO_WIDTH, AO_HEIGHT);
      return run;
    };
    const auto red = [](uint32_t texel) { return static_cast<double>(texel & 1023u) / 1023.; };
    // The mean visibility over the open ground and the corner, and the corner's most.
    struct AoStats {
      double open = 0.;
      double corner = 0.;
      double corner_most = 0.;
    };
    const auto stats_of = [&](const std::vector<uint32_t>& texels) {
      AoStats stats;
      int open_count = 0;
      int corner_count = 0;
      for (size_t at = 0; at < AO_PIXELS; ++at) {
        if (kind[at] == 1) {
          stats.open += red(texels[at]);
          ++open_count;
        } else if (kind[at] == 2) {
          stats.corner += red(texels[at]);
          stats.corner_most = std::max(stats.corner_most, red(texels[at]));
          ++corner_count;
        }
      }
      REQUIRE(open_count > 100 && corner_count > 20);
      stats.open /= open_count;
      stats.corner /= corner_count;
      return stats;
    };
    screen::ao = {.radius = 2.f};
    const AoRun ours = run_chain(normals.Get(), 1.f);
    const AoRun game = run_chain(normals.Get(), 0.f);
    const AoRun wrong_frame = run_chain(world_normals.Get(), 1.f);
    size_t depth_differs = 0;
    for (size_t at = 0; at < AO_PIXELS; ++at) depth_differs += ((ours.texels[at] >> 10) & 1023u) != ((game.texels[at] >> 10) & 1023u) ? 1 : 0;
    const AoStats ours_stats = stats_of(ours.texels);
    const AoStats game_stats = stats_of(game.texels);
    const AoStats wrong_stats = stats_of(wrong_frame.texels);
    std::printf("GT-VBAO in the reinterleave's place: PPSAO skipped %d / 4, the game's bindings back %d, its depth (green) the game's in all but %zu "
                "pixels; visibility: open ground %.3f, the corner %.3f (most %.3f); CONTROLS the game's (off: %d skipped) %.3f / %.3f, the "
                "world normal in GBuffer1: open ground %.3f\n",
                ours.skipped, ours.restored, depth_differs, ours_stats.open, ours_stats.corner, ours_stats.corner_most, game.skipped,
                game_stats.open, game_stats.corner, wrong_stats.open);
    CHECK(ours.skipped == 4 && ours.restored && game.skipped == 0 && game.restored && depth_differs == 0);
    CHECK(ours_stats.open >= .97 && ours_stats.corner <= .7);
    CHECK(std::abs(game_stats.open - (128. / 255.)) < 1e-3 && std::abs(game_stats.corner - (128. / 255.)) < 1e-3 && wrong_stats.open < .9);
    // The game's PPSAO_Blur twice over GT-VBAO's target (DeltaUV across, then down): the open ground stays open, the corner
    // dark; CONTROL: the blur changed the texture (it ran on it).
    const std::array<float, 4> across_delta = {1.f / AO_WIDTH, 0.f, 0.f, 0.f};
    const std::array<float, 4> down_delta = {0.f, 1.f / AO_HEIGHT, 0.f, 0.f};
    const auto across_buffer = constant_buffer(across_delta.data(), 16);
    const auto down_buffer = constant_buffer(down_delta.data(), 16);
    const auto blur_pass = [&](ID3D11Texture2D* source, ID3D11Buffer* delta, ID3D11Texture2D* target) {
      const auto source_view = Srv(source);
      ID3D11ShaderResourceView* view_pointer = source_view.Get();
      post_pass(blur.Get(), std::span(&view_pointer, 1), target);
      context->PSSetConstantBuffers(0, 1, &delta);
      context->Draw(3, 0);
      unbind();
    };
    run_chain(normals.Get(), 1.f);
    blur_pass(first.Get(), across_buffer.Get(), second.Get());
    blur_pass(second.Get(), down_buffer.Get(), third.Get());
    const auto blurred = ReadBack<uint32_t>(third.Get(), AO_WIDTH, AO_HEIGHT);
    const AoStats blurred_stats = stats_of(blurred);
    double blur_changed = 0.;
    for (size_t at = 0; at < AO_PIXELS; ++at) blur_changed = std::max(blur_changed, std::abs(red(blurred[at]) - red(ours.texels[at])));
    std::printf("the game's blur over it: open ground %.3f, the corner %.3f; CONTROL it changed the texture by up to %.3f\n", blurred_stats.open,
                blurred_stats.corner, blur_changed);
    CHECK(blurred_stats.open >= .97 && blurred_stats.corner <= .7 && blur_changed > .02);
    screen::ao = {};
    screen::internal::state.device.Reset();
    screen::internal::state.vbao.Reset();
    screen::internal::state.ao_constants.Reset();
  }

  std::printf("%s: %d failure(s)\n", (failures == 0 ? "PASS" : "FAIL"), failures);
  return failures == 0 ? 0 : 1;
}
