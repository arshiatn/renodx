// R.U.S.E.'s D3D9 bridge (src/games/ruse/bridge.hpp) on the GPU: a D3D9Ex and a D3D11 device on one adapter, the addon's
// own bridge shaders (build\ruse.include: build renodx-ruse.addon64 first), the census VS 0xF01502EA (the ground
// pre-pass's; argv[1] the census cso folder) and the B0 trace's constants (u1_trace_200820, trace2 3654: the camera pass's
// c0-c8, the ground VS's c0-c12).
// H1 (no helper):
// 1. Shared pairs: the four textures made by D3D9Ex (a KMT handle) and opened by D3D11; prepare (D3D9) writes colour /
//    depth / motion that D3D11 reads, D3D11 writes the output that writeback (D3D9) reads.
// 2. INTZ: the ground quad drawn by the census VS into an INTZ depth; prepare's depth (oC1, R32F) read on D3D11 = the
//    CPU's z / w of that plane at each pixel's D3D9 centre (worst 1e-4), 1 (cleared) off the quad.
// 3. 10-bit round trip: every code of the resolved scene (A2R10G10B10 and A2B10G10R10, those that render) through
//    prepare (pow(code x HDRParam, 2.2) in RGBA16F, relative 2e-3) and writeback (output = colour) back exactly, HDRParam
//    1 / 2 / 4.
// 4. Motion: prepare's camera motion from that depth (c4-c7 the inverse view-projection, c0-c3 a previous one with the
//    camera moved) against the CPU's reprojection of the same depth (worst 0.05 px or 0.3%); with the game's velocity
//    (s2 .yz, c9.w 1): an object's patch takes its own, the rest the camera's.
// 5. Jitter: OnDraw on a scene draw (its INTZ depth registered as ReShade's init_resource_view reports it: the bound
//    level-0 surface, not the texture) writes rows 0 / 1 of c0-c3 + offset x row 3 (read back from the device), the census
//    VS drawn with them moves clip.xy by offset x clip.w (worst 1% of the offset), the next non-scene draw gets the game's
//    c0-c3 back bit for bit.
// 6. GT-VBAO (ray_tracing.hpp ShadeGround with the Screen-Space Ground Ambient Occlusion row alone: no helper, the
//    anti-aliasing off): the ground quad and a plateau 1,000 units over its middle into the INTZ depth; the occlusion the
//    terrain shaders read (the shared R32F, read back on D3D9) near 1 on open ground (mean over 0.95: seen at the trace
//    camera's 13.6 degree pitch about one sector of 32 at the horizon goes, 0.969; 0.9975 facing the camera), darker on the
//    ground beside the plateau on screen, and the same shader over the same depth and constants on WARP within one
//    sector's share (1 / 128) at 99% of the pixels; released as at a D3D9 Reset and made again: the same occlusion. Then
//    Bend's screen-space ground shadows (no helper) with a low sun: ground beside the plateau shadowed, open ground lit.
// 9. Guides (ray_tracing.hpp BindGuides: the rays' bounce and reflections): the three guide targets made by D3D9Ex and
//    opened by D3D11 (B8G8R8A8, RGBA16F x 2) as render targets 1-3 beside a 10-bit scene target: ColorFill clears them
//    (RGBA16F too), each target's write mask holds, a blended draw (SRCALPHA / INVSRCALPHA: D3D9's one blend state)
//    lerps the normal by the alpha (its w oC0's) and keeps the water's position whole (rgb / w, oC3.w 1), D3D11 reads
//    what D3D9 wrote; unbound again after.
// 10. The trace point (ray_tracing.hpp ShadeBeforeMainPass, GT-VBAO as its row): with the game's AA off the pre-pass and
//    the main pass bind one depth and the main pass clears it after the mask's copy (run_rn2_rt_084045's frame trace,
//    0x0C1E65C0): a main-pass draw (OnSceneDraw) never shades, the clear (OnClearDepthStencilView, outside a scene) does
//    once over the ground's depth (test 6's occlusion exactly), nothing after D3D9's clear does again; with a main depth of
//    its own (the B0 trace's layout) its clear doesn't, its first draw does.
// 11. The rays' gate (ray_tracing.hpp rays_frame / SceneWanted, the replay's recording) with no helper (rt_core never
//    traces): 125 frames try at 0, 60, 120 and record 0, 59, 60, 119, 120 (a try's frame and the one before), GT-VBAO in
//    every frame; GT-VBAO off: the ground's depth copied in the try's frame alone; a due frame without a trace point
//    tries nothing and moves the try 60 frames on; one "not tracing" log line in all of it.
// H2 (renodx-tw-dlaa64.exe beside this exe; argv[2] dlss / fsr / xess):
// 7. Evaluate on a synthetic aliased frame (the ground quad's hard edges drawn with each frame's jitter) for 24 frames:
//    a result each frame after the first, written back into the scene; flat pixels within 3 codes of the aliased
//    reference (99%), at least half of the edge pixels between the two colours.
// 8. Frame generation (argv[2] dlss: the helper's Auto backend, DLSS-G here) over this window, 300 frames at about 60 a
//    second (the D3D9Ex device presenting under it as the game's does): the final
//    post's FP16 target into the shared HUD-less texture at the next draw (KeepHudless), the frame ready at the present,
//    KeepFrame's HUD-less pass through the proxy's swap chain shader (HDR10: its PQ code of the post's grey within 2), the
//    inputs Evaluate kept (near 5,000 and far 5e7 from the trace camera, its forward axis), and the requests the
//    proxy's present makes (frame_generation's OnReshadePresent, without ReShade's runtime) all answered once the first
//    is, frame generation not failed. The frames DLSS-G shows a rendered one are printed, not checked: over this
//    small test window it showed 2 in 7 of 11 runs and 1 ("no generated frames presented yet", no failure) in 4.
#include <windows.h>
#include <d3d11.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <embed/bridge_prepare.h>
#include <embed/bridge_quad.h>
#include <embed/bridge_writeback.h>
#include <embed/fg_hdr10.h>
#include <embed/swap_chain_proxy_pixel_shader_dx11.h>
#include <embed/swap_chain_proxy_vertex_shader_dx11.h>
#include "src/games/ruse/bridge.hpp"
#include "src/games/ruse/ray_tracing.hpp"
#include "src/games/ruse/shared.h"
#include "test/tw_particle_history/mock_reshade.hpp"

static std::vector<std::string> logged;
extern "C" __declspec(dllexport) const char* const NAME = "R.U.S.E. bridge test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  std::printf("  log: %s\n", message);
  logged.emplace_back(message);
}

using Microsoft::WRL::ComPtr;
namespace bridge = ruse::bridge;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

constexpr UINT WIDTH = 256;
constexpr UINT HEIGHT = 144;
constexpr D3DFORMAT INTZ = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));

// The ground VS's c0-c3 (uniModelViewProj, the terrain's model the identity) and the camera pass's c8 (RenderDt).
constexpr std::array<float, 16> VIEW_PROJECTION = {0.696776032f, 1.16561294f, 1.01178506e-08f, -3488510.f,
                                                   0.487397105f, -0.291354597f, 2.34648299f, 56396.8555f,
                                                   0.834336698f, -0.498746961f, -0.235231012f, 377480.5f,
                                                   0.834253252f, -0.498697072f, -0.235207483f, 382442.75f};
constexpr float RENDER_DT = 0.00631099986f;

using Matrix = std::array<double, 16>;  // row-major: clip_i = row_i . position
using Vector = std::array<double, 4>;

static Vector Transform(const Matrix& m, const Vector& v) {
  Vector result = {};
  for (int row = 0; row < 4; ++row) {
    for (int k = 0; k < 4; ++k) result[row] += m[(row * 4) + k] * v[k];
  }
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
  Matrix result = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) {
      if (std::abs(m[(row * 4) + column]) > std::abs(m[(pivot * 4) + column])) pivot = row;
    }
    for (int k = 0; k < 4; ++k) {
      std::swap(m[(column * 4) + k], m[(pivot * 4) + k]);
      std::swap(result[(column * 4) + k], result[(pivot * 4) + k]);
    }
    const double scale = 1.0 / m[(column * 4) + column];
    for (int k = 0; k < 4; ++k) {
      m[(column * 4) + k] *= scale;
      result[(column * 4) + k] *= scale;
    }
    for (int row = 0; row < 4; ++row) {
      if (row == column) continue;
      const double factor = m[(row * 4) + column];
      for (int k = 0; k < 4; ++k) {
        m[(row * 4) + k] -= factor * m[(column * 4) + k];
        result[(row * 4) + k] -= factor * result[(column * 4) + k];
      }
    }
  }
  return result;
}

// Float constants as the shader sees them, and back as doubles for the CPU's side of a check.
static std::array<float, 16> ToFloat(const Matrix& m) {
  std::array<float, 16> result = {};
  std::ranges::transform(m, result.begin(), [](double value) { return static_cast<float>(value); });
  return result;
}

static Matrix ToDouble(const std::array<float, 16>& m) {
  Matrix result = {};
  std::ranges::copy(m, result.begin());
  return result;
}

static Vector Unproject(const Matrix& inverse, double x, double y, double z) {
  const Vector world = Transform(inverse, {x, y, z, 1});
  return {world[0] / world[3], world[1] / world[3], world[2] / world[3], 1};
}

// The ground quad (the census VS maps SHORT4 0..32767 into WorldBoundingBox c11-c12): a square on the plane z = height,
// 15,000 units in front of the near plane along the view's centre, its half size the view's half height there (seen
// at the camera's 13.6 degree pitch it covers about a sixth of the frame).
struct Ground {
  std::array<float, 4> box_min = {};
  std::array<float, 4> box_max = {};
  double height = 0;
};

static Ground MakeGround(const Matrix& inverse) {
  const Vector near_point = Unproject(inverse, 0, 0, 0);
  const Vector far_point = Unproject(inverse, 0, 0, 1);
  std::array<double, 3> direction = {far_point[0] - near_point[0], far_point[1] - near_point[1], far_point[2] - near_point[2]};
  const double length = std::sqrt((direction[0] * direction[0]) + (direction[1] * direction[1]) + (direction[2] * direction[2]));
  for (double& axis : direction) axis /= length;
  constexpr double DISTANCE = 15000;
  const double half = (DISTANCE + 5000) * 0.414;
  const std::array<double, 3> hit = {near_point[0] + (DISTANCE * direction[0]), near_point[1] + (DISTANCE * direction[1]),
                                     near_point[2] + (DISTANCE * direction[2])};
  return {
      .box_min = {static_cast<float>(hit[0] - half), static_cast<float>(hit[1] - half), static_cast<float>(hit[2]), 1.f},
      .box_max = {static_cast<float>(hit[0] + half), static_cast<float>(hit[1] + half), static_cast<float>(hit[2]), 1.f},
      .height = static_cast<float>(hit[2]),
  };
}

// Where a D3D9 pixel centre's ray (ndc = 2 x pixel / size - 1) meets the ground's plane, and how far inside the quad
// that is in parts of its size (negative: outside; near 0 the coverage is the rasterizer's call).
struct GroundHit {
  Vector point = {};
  double inside = -1;
};

static GroundHit HitGround(const Matrix& inverse, const Ground& ground, UINT x, UINT y) {
  const double ndc_x = (2.0 * x / WIDTH) - 1.0;
  const double ndc_y = 1.0 - (2.0 * y / HEIGHT);
  const Vector a = Unproject(inverse, ndc_x, ndc_y, 0);
  const Vector b = Unproject(inverse, ndc_x, ndc_y, 1);
  const double s = (ground.height - a[2]) / (b[2] - a[2]);
  if (s < 0 || s > 1) return {};
  const Vector point = {a[0] + (s * (b[0] - a[0])), a[1] + (s * (b[1] - a[1])), ground.height, 1};
  const double size_x = ground.box_max[0] - ground.box_min[0];
  const double size_y = ground.box_max[1] - ground.box_min[1];
  return {
      .point = point,
      .inside = std::min({(point[0] - ground.box_min[0]) / size_x, (ground.box_max[0] - point[0]) / size_x,
                          (point[1] - ground.box_min[1]) / size_y, (ground.box_max[1] - point[1]) / size_y}),
  };
}

struct Devices {
  HWND window = nullptr;
  ComPtr<IDirect3D9Ex> d3d9;
  ComPtr<IDirect3DDevice9Ex> host;
  ComPtr<ID3D11Device> proxy;
  ComPtr<ID3D11DeviceContext> context;
};

static Devices MakeDevices() {
  Devices devices;
  const WNDCLASSW window_class = {.lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(nullptr), .lpszClassName = L"ruse_bridge_test"};
  RegisterClassW(&window_class);
  devices.window = CreateWindowExW(0, window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                                   window_class.hInstance, nullptr);
  REQUIRE(devices.window != nullptr);
  REQUIRE(SUCCEEDED(Direct3DCreate9Ex(D3D_SDK_VERSION, devices.d3d9.GetAddressOf())));
  D3DPRESENT_PARAMETERS parameters = {.BackBufferWidth = 64, .BackBufferHeight = 64, .BackBufferFormat = D3DFMT_A8R8G8B8,
                                      .BackBufferCount = 1, .SwapEffect = D3DSWAPEFFECT_DISCARD, .hDeviceWindow = devices.window,
                                      .Windowed = TRUE};
  REQUIRE(SUCCEEDED(devices.d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, devices.window,
                                                 D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
                                                 &parameters, nullptr, devices.host.GetAddressOf())));
  // Shared handles open only on the adapter that made them.
  LUID luid = {};
  REQUIRE(SUCCEEDED(devices.d3d9->GetAdapterLUID(D3DADAPTER_DEFAULT, &luid)));
  ComPtr<IDXGIFactory1> factory;
  REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))));
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0; factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++index) {
    DXGI_ADAPTER_DESC1 desc = {};
    adapter->GetDesc1(&desc);
    if (desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) break;
  }
  REQUIRE(adapter != nullptr);
  constexpr D3D_FEATURE_LEVEL LEVEL = D3D_FEATURE_LEVEL_11_1;
  REQUIRE(SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &LEVEL, 1, D3D11_SDK_VERSION,
                                      devices.proxy.GetAddressOf(), nullptr, devices.context.GetAddressOf())));
  return devices;
}

static ComPtr<ID3DBlob> Compile(const char* source, const char* profile) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, std::strlen(source), "test", nullptr, nullptr, "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("%s\n", (errors != nullptr) ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed");
    std::exit(1);
  }
  return code;
}

static std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// A D3D9 render target's pixels (GetRenderTargetData into system memory), 4 bytes or `pixel_bytes` each.
static std::vector<uint8_t> ReadHost(IDirect3DDevice9Ex* host, IDirect3DSurface9* surface, UINT pixel_bytes) {
  D3DSURFACE_DESC desc = {};
  surface->GetDesc(&desc);
  ComPtr<IDirect3DSurface9> copy;
  REQUIRE(SUCCEEDED(host->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, copy.GetAddressOf(), nullptr)));
  REQUIRE(SUCCEEDED(host->GetRenderTargetData(surface, copy.Get())));
  D3DLOCKED_RECT locked = {};
  REQUIRE(SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY)));
  std::vector<uint8_t> pixels(static_cast<size_t>(desc.Width) * desc.Height * pixel_bytes);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(pixels.data() + (static_cast<size_t>(y) * desc.Width * pixel_bytes), static_cast<const uint8_t*>(locked.pBits) + (y * locked.Pitch),
                static_cast<size_t>(desc.Width) * pixel_bytes);
  }
  copy->UnlockRect();
  return pixels;
}

// A D3D11 texture's pixels through a staging copy, `pixel_bytes` each.
static std::vector<uint8_t> ReadProxy(const Devices& devices, ID3D11Texture2D* texture, UINT pixel_bytes) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(devices.proxy->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
  devices.context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(devices.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<uint8_t> pixels(static_cast<size_t>(desc.Width) * desc.Height * pixel_bytes);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(pixels.data() + (static_cast<size_t>(y) * desc.Width * pixel_bytes), static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch),
                static_cast<size_t>(desc.Width) * pixel_bytes);
  }
  devices.context->Unmap(staging.Get(), 0);
  return pixels;
}

static float Half(const std::vector<uint8_t>& pixels, size_t index) {
  uint16_t value = 0;
  std::memcpy(&value, pixels.data() + (index * 2), 2);
  return DirectX::PackedVector::XMConvertHalfToFloat(value);
}

static float Float(const std::vector<uint8_t>& pixels, size_t index) {
  float value = 0;
  std::memcpy(&value, pixels.data() + (index * 4), 4);
  return value;
}

// 10-bit codes of a packed pixel in the order red, green, blue (A2R10G10B10: red in bits 20-29; A2B10G10R10: in 0-9).
static std::array<uint32_t, 3> Codes(uint32_t pixel, D3DFORMAT format) {
  const std::array<uint32_t, 3> fields = {(pixel >> 20) & 1023u, (pixel >> 10) & 1023u, pixel & 1023u};
  if (format == D3DFMT_A2R10G10B10) return fields;
  return {fields[2], fields[1], fields[0]};
}

static uint32_t Pack(const std::array<uint32_t, 3>& codes, D3DFORMAT format) {
  if (format == D3DFMT_A2R10G10B10) return (3u << 30) | (codes[0] << 20) | (codes[1] << 10) | codes[2];
  return (3u << 30) | (codes[2] << 20) | (codes[1] << 10) | codes[0];
}

static ComPtr<IDirect3DTexture9> MakeTarget(IDirect3DDevice9Ex* host, D3DFORMAT format, UINT usage = D3DUSAGE_RENDERTARGET) {
  ComPtr<IDirect3DTexture9> texture;
  if (FAILED(host->CreateTexture(WIDTH, HEIGHT, 1, usage, format, D3DPOOL_DEFAULT, texture.GetAddressOf(), nullptr))) return nullptr;
  return texture;
}

static ComPtr<IDirect3DSurface9> Level(IDirect3DTexture9* texture) {
  ComPtr<IDirect3DSurface9> surface;
  REQUIRE(SUCCEEDED(texture->GetSurfaceLevel(0, surface.GetAddressOf())));
  return surface;
}

// 4-byte pixels into a render-target texture through a system-memory copy.
static void Fill(IDirect3DDevice9Ex* host, IDirect3DTexture9* texture, D3DFORMAT format, const std::vector<uint32_t>& pixels) {
  ComPtr<IDirect3DTexture9> upload;
  REQUIRE(SUCCEEDED(host->CreateTexture(WIDTH, HEIGHT, 1, 0, format, D3DPOOL_SYSTEMMEM, upload.GetAddressOf(), nullptr)));
  D3DLOCKED_RECT locked = {};
  REQUIRE(SUCCEEDED(upload->LockRect(0, &locked, nullptr, 0)));
  for (UINT y = 0; y < HEIGHT; ++y) {
    std::memcpy(static_cast<uint8_t*>(locked.pBits) + (y * locked.Pitch), pixels.data() + (static_cast<size_t>(y) * WIDTH), WIDTH * 4);
  }
  upload->UnlockRect(0);
  REQUIRE(SUCCEEDED(host->UpdateTexture(upload.Get(), texture)));
}

// The game's ground draw: the census VS with `vertex_camera` in c0-c3 (or what OnDraw left there when null), c8-c10 rows
// 0, 1 and 3 of `unjittered` (the jitter test's PS reads texcoord1 - texcoord2 over texcoord2.z = clip.w), the box in
// c11-c12; depth tested and written when a depth is bound.
struct GroundDraw {
  ComPtr<IDirect3DVertexShader9> shader;
  ComPtr<IDirect3DVertexDeclaration9> layout;
};

static void DrawGround(IDirect3DDevice9Ex* host, const GroundDraw& draw, IDirect3DPixelShader9* pixel_shader, const Ground& ground,
                       const std::array<float, 16>* vertex_camera, const std::array<float, 16>& unjittered) {
  host->SetVertexShader(draw.shader.Get());
  host->SetPixelShader(pixel_shader);
  host->SetVertexDeclaration(draw.layout.Get());
  if (vertex_camera != nullptr) host->SetVertexShaderConstantF(0, vertex_camera->data(), 4);
  constexpr std::array<float, 16> SUN_VIEW = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  host->SetVertexShaderConstantF(4, SUN_VIEW.data(), 4);
  host->SetVertexShaderConstantF(8, unjittered.data(), 1);
  host->SetVertexShaderConstantF(9, unjittered.data() + 4, 1);
  host->SetVertexShaderConstantF(10, unjittered.data() + 12, 1);
  host->SetVertexShaderConstantF(11, ground.box_min.data(), 1);
  host->SetVertexShaderConstantF(12, ground.box_max.data(), 1);
  host->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  host->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  constexpr std::array<int16_t, 16> CORNERS = {0, 0, 0, 1, 32767, 0, 0, 1, 0, 32767, 0, 1, 32767, 32767, 0, 1};
  // D3D9 draws only inside a scene (D3DERR_INVALIDCALL outside): in the game the bridge's passes are in the game's.
  host->BeginScene();
  host->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, CORNERS.data(), 4 * sizeof(int16_t));
  host->EndScene();
}

constexpr char JITTER_PS[] = R"(
float4 main(float3 current : TEXCOORD1, float3 previous : TEXCOORD2) : COLOR0 {
  return float4((current.xy - previous.xy) / previous.z, previous.z, 1);
})";

constexpr char FLAT_PS[] = R"(
float4 colour : register(c0);
float4 main() : COLOR0 { return colour; })";

constexpr char GUIDE_PS[] = R"(
float4 outputs[4] : register(c0);
struct Output {
  float4 colour : COLOR0;
  float4 albedo : COLOR1;
  float4 normal : COLOR2;
  float4 water : COLOR3;
};
Output main() {
  Output output;
  output.colour = outputs[0];
  output.albedo = outputs[1];
  output.normal = outputs[2];
  output.water = outputs[3];
  return output;
})";

// The bridge's per-frame constants as Evaluate lays them out: the camera pass's c0-c8, then the size, HDRParam and
// whether s2 holds the game's velocity.
static std::array<float, 40> PrepareConstants(const std::array<float, 16>& previous, const std::array<float, 16>& inverse, float hdr_param,
                                              bool game_velocity) {
  std::array<float, 40> constants = {};
  std::ranges::copy(previous, constants.begin());
  std::ranges::copy(inverse, constants.begin() + 16);
  constants[32] = RENDER_DT;
  constants[36] = static_cast<float>(WIDTH);
  constants[37] = static_cast<float>(HEIGHT);
  constants[38] = hdr_param;
  constants[39] = game_velocity ? 1.f : 0.f;
  return constants;
}

int main(int argc, char** argv) {
  const std::string census = (argc > 1) ? argv[1] : R"(C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\nontw\ruse\census\cso)";
  const std::string upscaler = (argc > 2) ? argv[2] : "dlss";
  Devices devices = MakeDevices();
  auto* host = devices.host.Get();

  mock::Device proxy_device;
  proxy_device.native = devices.proxy;
  renodx::utils::device_proxy::proxy_device_reshade = &proxy_device;
  struct HostDevice : mock::Device {
    IDirect3DDevice9Ex* host = nullptr;
    uint64_t get_native() const override { return reinterpret_cast<uint64_t>(host); }
    reshade::api::device_api get_api() const override { return reshade::api::device_api::d3d9; }
  } host_device;
  host_device.host = host;
  mock::CommandList command_list;
  command_list.owner = &host_device;

  auto& state = bridge::state;
  state.width = WIDTH;
  state.height = HEIGHT;
  REQUIRE(SUCCEEDED(host->CreateVertexShader(reinterpret_cast<const DWORD*>(__bridge_quad.data()), state.quad.GetAddressOf())));
  REQUIRE(SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(__bridge_prepare.data()), state.prepare.GetAddressOf())));
  REQUIRE(SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(__bridge_writeback.data()), state.writeback.GetAddressOf())));
  constexpr std::array<D3DVERTEXELEMENT9, 2> QUAD_LAYOUT = {{{0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()}};
  REQUIRE(SUCCEEDED(host->CreateVertexDeclaration(QUAD_LAYOUT.data(), state.quad_layout.GetAddressOf())));

  GroundDraw ground_draw;
  const std::vector<uint8_t> ground_vs = ReadFile(census + R"(\0xF01502EA.vs_3_0.cso)");
  REQUIRE(!ground_vs.empty());
  REQUIRE(SUCCEEDED(host->CreateVertexShader(reinterpret_cast<const DWORD*>(ground_vs.data()), ground_draw.shader.GetAddressOf())));
  constexpr std::array<D3DVERTEXELEMENT9, 2> GROUND_LAYOUT = {{{0, 0, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()}};
  REQUIRE(SUCCEEDED(host->CreateVertexDeclaration(GROUND_LAYOUT.data(), ground_draw.layout.GetAddressOf())));
  ComPtr<IDirect3DPixelShader9> jitter_ps;
  ComPtr<IDirect3DPixelShader9> flat_ps;
  const ComPtr<ID3DBlob> jitter_code = Compile(JITTER_PS, "ps_3_0");
  const ComPtr<ID3DBlob> flat_code = Compile(FLAT_PS, "ps_3_0");
  REQUIRE(SUCCEEDED(host->CreatePixelShader(static_cast<const DWORD*>(jitter_code->GetBufferPointer()), jitter_ps.GetAddressOf())));
  REQUIRE(SUCCEEDED(host->CreatePixelShader(static_cast<const DWORD*>(flat_code->GetBufferPointer()), flat_ps.GetAddressOf())));

  const Matrix view_projection = ToDouble(VIEW_PROJECTION);
  const std::array<float, 16> inverse_float = ToFloat(Inverse(view_projection));
  const Matrix inverse = ToDouble(inverse_float);
  const Ground ground = MakeGround(inverse);

  // 1. Shared pairs.
  REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, D3DFMT_A16B16G16R16F, &state.color));
  REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, D3DFMT_R32F, &state.depth));
  REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, D3DFMT_G16R16F, &state.motion));
  REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, D3DFMT_A16B16G16R16F, &state.output));

  // 2. INTZ: the ground into the game's (readable) depth, read through prepare.
  const ComPtr<IDirect3DTexture9> depth_texture = MakeTarget(host, INTZ, D3DUSAGE_DEPTHSTENCIL);
  REQUIRE(depth_texture != nullptr);
  const ComPtr<IDirect3DSurface9> depth_surface = Level(depth_texture.Get());
  // Made readable as ReShade reports it: init_resource_view with the texture as the resource and its level-0 surface as
  // the view, the handle the binds give.
  const auto register_depth = [&] {
    bridge::OnInitDepthView(&host_device, {reinterpret_cast<uint64_t>(depth_texture.Get())}, reshade::api::resource_usage::depth_stencil,
                            reshade::api::resource_view_desc(reshade::api::resource_view_type::texture_2d, reshade::api::format::intz, 0, 1, 0, 1),
                            {reinterpret_cast<uint64_t>(depth_surface.Get())});
  };
  const ComPtr<IDirect3DTexture9> scratch = MakeTarget(host, D3DFMT_A8R8G8B8);
  const ComPtr<IDirect3DSurface9> scratch_surface = Level(scratch.Get());
  host->SetRenderTarget(0, scratch_surface.Get());
  host->SetDepthStencilSurface(depth_surface.Get());
  REQUIRE(SUCCEEDED(host->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0)));
  host->SetRenderState(D3DRS_ZENABLE, TRUE);
  host->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
  host->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
  const std::array<float, 4> white = {1, 1, 1, 1};
  host->SetPixelShaderConstantF(0, white.data(), 1);
  DrawGround(host, ground_draw, flat_ps.Get(), ground, &VIEW_PROJECTION, VIEW_PROJECTION);
  host->SetDepthStencilSurface(nullptr);

  const ComPtr<IDirect3DTexture9> velocity = MakeTarget(host, D3DFMT_A8R8G8B8);
  {
    // The far plane's value everywhere (still: 128 = 0.502), an object's own velocity (+40, -24 codes) in a patch.
    std::vector<uint32_t> pixels(static_cast<size_t>(WIDTH) * HEIGHT, 0xFF808080u);
    for (UINT y = 40; y < 60; ++y) {
      for (UINT x = 100; x < 140; ++x) pixels[(y * WIDTH) + x] = 0xFF00A868u;  // G 168, B 104
    }
    Fill(host, velocity.Get(), D3DFMT_A8R8G8B8, pixels);
  }

  // The camera moved 60 units along x and 20 along y since the last frame: the previous view-projection of a translated world.
  const Matrix moved = Multiply(view_projection, {1, 0, 0, 60, 0, 1, 0, 20, 0, 0, 1, 0, 0, 0, 0, 1});
  const std::array<float, 16> previous_float = ToFloat(moved);
  const Matrix previous = ToDouble(previous_float);

  for (const D3DFORMAT format : {D3DFMT_A2R10G10B10, D3DFMT_A2B10G10R10}) {
    const ComPtr<IDirect3DTexture9> scene = MakeTarget(host, format);
    if (scene == nullptr) {
      std::printf("scene format %d: not a render-target texture here, skipped\n", static_cast<int>(format));
      continue;
    }
    std::vector<uint32_t> pattern(static_cast<size_t>(WIDTH) * HEIGHT);
    for (size_t index = 0; index < pattern.size(); ++index) {
      pattern[index] = Pack({static_cast<uint32_t>(index % 1024), static_cast<uint32_t>((index * 7) % 1024),
                             static_cast<uint32_t>((index * 13) % 1024)},
                            format);
    }
    for (const float hdr_param : {1.f, 2.f, 4.f}) {
      Fill(host, scene.Get(), format, pattern);
      const bool game_velocity = (hdr_param == 2.f);
      const std::array<float, 40> constants = PrepareConstants(previous_float, inverse_float, hdr_param, game_velocity);
      const std::array<IDirect3DSurface9*, 3> inputs = {state.color.host_surface.Get(), state.depth.host_surface.Get(), state.motion.host_surface.Get()};
      const std::array<IDirect3DBaseTexture9*, 3> sources = {scene.Get(), depth_texture.Get(), velocity.Get()};
      host->BeginScene();
      bridge::DrawQuad(host, inputs, state.prepare.Get(), sources, constants);
      host->EndScene();
      bridge::WaitHost(host);

      // 3. The colour as light (D3D11 reads what D3D9 wrote).
      const std::vector<uint8_t> color = ReadProxy(devices, state.color.proxy.Get(), 8);
      double worst_color = 0;
      for (size_t index = 0; index < pattern.size(); ++index) {
        const std::array<uint32_t, 3> codes = Codes(pattern[index], format);
        for (int channel = 0; channel < 3; ++channel) {
          const double expected = std::pow(codes[channel] / 1023.0 * hdr_param, 2.2);
          const double got = Half(color, (index * 4) + channel);
          worst_color = std::max(worst_color, std::abs(got - expected) / std::max(expected, 1e-3));
        }
      }
      std::printf("format %d HDRParam %.0f: colour worst relative %.2e\n", static_cast<int>(format), hdr_param, worst_color);
      CHECK(worst_color < 2e-3);

      if (format == D3DFMT_A2R10G10B10 && hdr_param == 1.f) {
        // 2. The depth.
        const std::vector<uint8_t> depth = ReadProxy(devices, state.depth.proxy.Get(), 4);
        double worst_depth = 0;
        size_t on_ground = 0;
        size_t off_wrong = 0;
        for (UINT y = 0; y < HEIGHT; ++y) {
          for (UINT x = 0; x < WIDTH; ++x) {
            const GroundHit hit = HitGround(inverse, ground, x, y);
            const float got = Float(depth, (y * WIDTH) + x);
            if (hit.inside > 0.01) {
              ++on_ground;
              const Vector clip = Transform(view_projection, hit.point);
              worst_depth = std::max(worst_depth, std::abs(got - (clip[2] / clip[3])));
            } else if (hit.inside < -0.01 && got != 1.f) {
              ++off_wrong;
            }
          }
        }
        std::printf("INTZ: %zu ground pixels, worst %.2e; off the quad not 1: %zu\n", on_ground, worst_depth, off_wrong);
        CHECK(on_ground > 1000);
        CHECK(worst_depth < 1e-4);
        CHECK(off_wrong == 0);
      }

      if (format == D3DFMT_A2R10G10B10 && hdr_param <= 2.f) {
        // 4. The motion against the CPU's reprojection of the same depth (pixel centres as the shader's uv).
        const std::vector<uint8_t> depth = ReadProxy(devices, state.depth.proxy.Get(), 4);
        const std::vector<uint8_t> motion = ReadProxy(devices, state.motion.proxy.Get(), 4);
        double worst_camera = 0;
        double worst_object = 0;
        for (UINT y = 0; y < HEIGHT; ++y) {
          for (UINT x = 0; x < WIDTH; ++x) {
            const size_t index = (y * WIDTH) + x;
            const double ndc_x = (2.0 * (x + 0.5) / WIDTH) - 1.0;
            const double ndc_y = 1.0 - (2.0 * (y + 0.5) / HEIGHT);
            const Vector world = Unproject(inverse, ndc_x, ndc_y, Float(depth, index));
            const Vector last = Transform(previous, world);
            std::array<double, 2> expected = {-(ndc_x - (last[0] / last[3])) * 0.5 * WIDTH, (ndc_y - (last[1] / last[3])) * 0.5 * HEIGHT};
            const bool object = game_velocity && x >= 100 && x < 140 && y >= 40 && y < 60;
            if (object) {
              const double to_velocity = 0.05 / RENDER_DT;
              expected = {-((168 / 255.0) - 0.5019608) / to_velocity * 0.5 * WIDTH, ((104 / 255.0) - 0.5019608) / to_velocity * 0.5 * HEIGHT};
            }
            for (int axis = 0; axis < 2; ++axis) {
              const double error = std::abs(Half(motion, (index * 2) + axis) - expected[axis]);
              const double allowed = std::max(0.05, 3e-3 * std::abs(expected[axis]));
              double& worst = object ? worst_object : worst_camera;
              worst = std::max(worst, error / allowed);
            }
          }
        }
        std::printf("motion (game velocity %d): camera worst %.2f of allowed, object %.2f\n", game_velocity ? 1 : 0, worst_camera, worst_object);
        CHECK(worst_camera <= 1.0);
        CHECK(worst_object <= 1.0);
      }

      // 1 + 3. Output = colour on D3D11, written back into the scene by D3D9: every code as it was.
      devices.context->CopyResource(state.output.proxy.Get(), state.color.proxy.Get());
      bridge::WaitProxy(devices.proxy.Get(), devices.context.Get());
      const ComPtr<IDirect3DSurface9> scene_surface = Level(scene.Get());
      const std::array<IDirect3DSurface9*, 1> target = {scene_surface.Get()};
      const std::array<IDirect3DBaseTexture9*, 1> result = {state.output.host.Get()};
      host->BeginScene();
      bridge::DrawQuad(host, target, state.writeback.Get(), result, constants);
      host->EndScene();
      const std::vector<uint8_t> back = ReadHost(host, scene_surface.Get(), 4);
      size_t differing = 0;
      for (size_t index = 0; index < pattern.size(); ++index) {
        uint32_t pixel = 0;
        std::memcpy(&pixel, back.data() + (index * 4), 4);
        if (Codes(pixel, format) != Codes(pattern[index], format)) ++differing;
      }
      std::printf("format %d HDRParam %.0f: round trip, %zu of %zu pixels differ\n", static_cast<int>(format), hdr_param, differing, pattern.size());
      CHECK(differing == 0);
    }
  }

  // 5. Jitter: a scene draw (frame-sized target, an INTZ depth) after the game set its c0-c3.
  state.enabled = true;
  state.failed = false;
  state.ready = true;
  state.reset = false;  // a frame after a result
  state.render_target_is_frame_sized = true;
  state.depth_stencil = reinterpret_cast<uint64_t>(depth_surface.Get());
  register_depth();
  state.vertex_camera = VIEW_PROJECTION;
  state.jitter = {0.3125f, -0.2f};
  host->SetVertexShaderConstantF(0, VIEW_PROJECTION.data(), 4);
  // The sprite atlas's draws (and the bloom's first pass) with the game's AA off: the pre-pass's frame-sized target and the
  // scene's depth, a smaller viewport (run_rn2_rt_084045's frame trace: 2048 x 2048, 1920 x 1080): no scene draws, the
  // game's c0-c3.
  state.viewport_is_frame_sized = false;
  CHECK(!bridge::IsSceneDraw());
  bridge::OnDraw(&command_list);
  std::array<float, 16> atlas_rows = {};
  host->GetVertexShaderConstantF(0, atlas_rows.data(), 4);
  CHECK(atlas_rows == VIEW_PROJECTION && !state.jitter_written);
  state.viewport_is_frame_sized = true;
  CHECK(bridge::IsSceneDraw());
  bridge::OnDraw(&command_list);
  std::array<float, 16> written = {};
  host->GetVertexShaderConstantF(0, written.data(), 4);
  const float offset_x = 2.f * state.jitter[0] / WIDTH;
  const float offset_y = -2.f * state.jitter[1] / HEIGHT;
  std::array<float, 16> expected_rows = VIEW_PROJECTION;
  for (int column = 0; column < 4; ++column) {
    expected_rows[column] += offset_x * VIEW_PROJECTION[12 + column];
    expected_rows[4 + column] += offset_y * VIEW_PROJECTION[12 + column];
  }
  CHECK(written == expected_rows);
  CHECK(state.jitter_written);
  const ComPtr<IDirect3DTexture9> offsets = MakeTarget(host, D3DFMT_A32B32G32R32F);
  REQUIRE(offsets != nullptr);
  const ComPtr<IDirect3DSurface9> offsets_surface = Level(offsets.Get());
  host->SetRenderTarget(0, offsets_surface.Get());
  host->SetDepthStencilSurface(nullptr);
  host->SetRenderState(D3DRS_ZENABLE, FALSE);
  REQUIRE(SUCCEEDED(host->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0)));
  DrawGround(host, ground_draw, jitter_ps.Get(), ground, nullptr, VIEW_PROJECTION);
  const std::vector<uint8_t> moved_pixels = ReadHost(host, offsets_surface.Get(), 16);
  double worst_jitter = 0;
  size_t covered = 0;
  for (size_t index = 0; index < static_cast<size_t>(WIDTH) * HEIGHT; ++index) {
    if (Float(moved_pixels, (index * 4) + 3) != 1.f) continue;
    ++covered;
    worst_jitter = std::max({worst_jitter, static_cast<double>(std::abs(Float(moved_pixels, index * 4) - offset_x) / std::abs(offset_x)),
                             static_cast<double>(std::abs(Float(moved_pixels, (index * 4) + 1) - offset_y) / std::abs(offset_y))});
  }
  std::printf("jitter: %zu pixels, worst %.2e of the offset\n", covered, worst_jitter);
  CHECK(covered > 1000);
  CHECK(worst_jitter < 0.01);
  state.render_target_is_frame_sized = false;
  bridge::OnDraw(&command_list);
  host->GetVertexShaderConstantF(0, written.data(), 4);
  CHECK(std::memcmp(written.data(), VIEW_PROJECTION.data(), sizeof(written)) == 0);
  CHECK(!state.jitter_written);
  // The scene's depth goes at the present (none left for the next frame's Evaluate), an INTZ depth with its view.
  state.render_target_is_frame_sized = true;
  bridge::OnDraw(&command_list);
  CHECK(state.scene_depth == state.depth_stencil);
  bridge::OnPresent(host);
  CHECK(state.scene_depth == 0);
  bridge::OnDestroyDepthView(&host_device, {state.depth_stencil});
  CHECK(!bridge::IsSceneDraw());
  register_depth();
  // No scene draws from the bloom (Evaluate, here returning at once) to the present, none with the final post's depth
  // (another INTZ depth: the back buffer's), the scene's own depth never the post's.
  CHECK(bridge::IsSceneDraw());
  state.camera_valid = false;
  bridge::Evaluate(&command_list, __bridge_quad, __bridge_prepare, __bridge_writeback);
  CHECK(!bridge::IsSceneDraw());
  bridge::OnPresent(host);
  CHECK(bridge::IsSceneDraw());
  const uint64_t scene_depth_handle = state.depth_stencil;
  constexpr uint64_t POST_DEPTH = 0x7E57D00Du;
  bridge::OnInitDepthView(&host_device, {}, reshade::api::resource_usage::depth_stencil,
                          reshade::api::resource_view_desc(reshade::api::resource_view_type::texture_2d, reshade::api::format::intz, 0, 1, 0, 1),
                          {POST_DEPTH});
  bridge::OnDraw(&command_list);
  state.depth_stencil = POST_DEPTH;
  bridge::OnFinalPost(&command_list);
  CHECK(!bridge::IsSceneDraw());
  state.depth_stencil = scene_depth_handle;
  bridge::OnFinalPost(&command_list);
  CHECK(state.post_depth == POST_DEPTH && bridge::IsSceneDraw());
  bridge::OnDestroyDepthView(&host_device, {POST_DEPTH});
  CHECK(state.post_depth == 0);
  state.render_target_is_frame_sized = false;
  bridge::OnDraw(&command_list);
  // After a reset (no result yet, or the helper lost) the next frame's jitter is 0 and the scene draws keep the game's c0-c3.
  state.reset = true;
  bridge::OnPresent(host);
  CHECK(state.jitter[0] == 0.f && state.jitter[1] == 0.f);
  state.render_target_is_frame_sized = true;
  host->SetVertexShaderConstantF(0, VIEW_PROJECTION.data(), 4);
  bridge::OnDraw(&command_list);
  host->GetVertexShaderConstantF(0, written.data(), 4);
  CHECK(std::memcmp(written.data(), VIEW_PROJECTION.data(), sizeof(written)) == 0 && !state.jitter_written);
  state.render_target_is_frame_sized = false;

  // 6. GT-VBAO without the helper.
  {
    state = bridge::State{};
    state.width = WIDTH;
    state.height = HEIGHT;
    std::ranges::copy(VIEW_PROJECTION, state.camera.begin());
    std::ranges::copy(inverse_float, state.camera.begin() + 16);
    state.camera[32] = RENDER_DT;
    state.camera_valid = true;
    register_depth();
    auto& rays = ruse::ray_tracing::state;
    rays.vbao = true;
    rays.prepass_depth = reinterpret_cast<uint64_t>(depth_surface.Get());
    rays.ground_mask = reinterpret_cast<uint64_t>(scratch.Get());
    const float quarter_x = 0.25f * (ground.box_max[0] - ground.box_min[0]);
    const float quarter_y = 0.25f * (ground.box_max[1] - ground.box_min[1]);
    const Ground plateau = {
        .box_min = {ground.box_min[0] + quarter_x, ground.box_min[1] + quarter_y, ground.box_min[2] + 1000.f, 1.f},
        .box_max = {ground.box_max[0] - quarter_x, ground.box_max[1] - quarter_y, ground.box_max[2] + 1000.f, 1.f},
        .height = ground.height + 1000.0,
    };
    host->SetRenderTarget(0, scratch_surface.Get());
    host->SetDepthStencilSurface(depth_surface.Get());
    REQUIRE(SUCCEEDED(host->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0)));
    host->SetRenderState(D3DRS_ZENABLE, TRUE);
    host->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    DrawGround(host, ground_draw, flat_ps.Get(), ground, &VIEW_PROJECTION, VIEW_PROJECTION);
    DrawGround(host, ground_draw, flat_ps.Get(), plateau, &VIEW_PROJECTION, VIEW_PROJECTION);
    host->SetDepthStencilSurface(nullptr);
    host->BeginScene();
    const bool shaded = ruse::ray_tracing::ShadeGround(&command_list);
    host->EndScene();
    REQUIRE(shaded);
    const std::vector<uint8_t> occlusion = ReadHost(host, rays.occlusion.host_surface.Get(), 4);

    // Plateau pixels (its top seen first), then each ground pixel's screen distance to the nearest of them.
    std::vector<uint8_t> on_plateau(static_cast<size_t>(WIDTH) * HEIGHT);
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) on_plateau[(y * WIDTH) + x] = HitGround(inverse, plateau, x, y).inside > -0.01 ? 1 : 0;
    }
    double open_sum = 0;
    double open_lowest = 1;
    size_t open = 0;
    double beside_sum = 0;
    size_t beside = 0;
    for (int y = 0; y < static_cast<int>(HEIGHT); ++y) {
      for (int x = 0; x < static_cast<int>(WIDTH); ++x) {
        const size_t index = (static_cast<size_t>(y) * WIDTH) + x;
        if (on_plateau[index] != 0 || HitGround(inverse, ground, x, y).inside < 0.01) continue;
        int nearest = 1 << 20;
        for (int dy = -24; dy <= 24; ++dy) {
          for (int dx = -24; dx <= 24; ++dx) {
            const int sx = x + dx;
            const int sy = y + dy;
            if (sx < 0 || sy < 0 || sx >= static_cast<int>(WIDTH) || sy >= static_cast<int>(HEIGHT)) continue;
            if (on_plateau[(static_cast<size_t>(sy) * WIDTH) + sx] != 0) nearest = std::min(nearest, std::max(std::abs(dx), std::abs(dy)));
          }
        }
        const double value = Float(occlusion, index);
        if (nearest > 24) {
          ++open;
          open_sum += value;
          open_lowest = std::min(open_lowest, value);
        } else if (nearest <= 3) {
          ++beside;
          beside_sum += value;
        }
      }
    }
    const double open_mean = open_sum / std::max<size_t>(open, 1);
    const double beside_mean = beside_sum / std::max<size_t>(beside, 1);
    std::printf("GT-VBAO: open ground %zu pixels, mean %.4f, lowest %.4f; beside the plateau %zu pixels, mean %.4f\n", open, open_mean, open_lowest,
                beside, beside_mean);
    CHECK(open > 1000 && open_mean > 0.95);
    CHECK(beside > 100 && beside_mean < open_mean - 0.03);

    // The same shader on WARP over the depth and constants this GPU had.
    ComPtr<ID3D11Device> warp;
    ComPtr<ID3D11DeviceContext> warp_context;
    REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, warp.GetAddressOf(), nullptr,
                                        warp_context.GetAddressOf())));
    const std::vector<uint8_t> start_depth = ReadProxy(devices, state.depth.proxy.Get(), 4);
    D3D11_BUFFER_DESC settings_desc = {};
    rays.vbao_settings->GetDesc(&settings_desc);
    settings_desc.Usage = D3D11_USAGE_STAGING;
    settings_desc.BindFlags = 0;
    settings_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> settings_copy;
    REQUIRE(SUCCEEDED(devices.proxy->CreateBuffer(&settings_desc, nullptr, settings_copy.GetAddressOf())));
    devices.context->CopyResource(settings_copy.Get(), rays.vbao_settings.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    REQUIRE(SUCCEEDED(devices.context->Map(settings_copy.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    std::vector<uint8_t> settings(settings_desc.ByteWidth);
    std::memcpy(settings.data(), mapped.pData, settings.size());
    devices.context->Unmap(settings_copy.Get(), 0);
    const D3D11_TEXTURE2D_DESC depth_desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                             .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA depth_data = {.pSysMem = start_depth.data(), .SysMemPitch = WIDTH * 4};
    D3D11_TEXTURE2D_DESC output_desc = depth_desc;
    output_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = settings_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA constants_data = {.pSysMem = settings.data()};
    ComPtr<ID3D11Texture2D> warp_depth;
    ComPtr<ID3D11ShaderResourceView> warp_depth_view;
    ComPtr<ID3D11Texture2D> warp_output;
    ComPtr<ID3D11UnorderedAccessView> warp_output_view;
    ComPtr<ID3D11Buffer> warp_constants;
    ComPtr<ID3D11ComputeShader> warp_shader;
    REQUIRE(SUCCEEDED(warp->CreateTexture2D(&depth_desc, &depth_data, warp_depth.GetAddressOf()))
            && SUCCEEDED(warp->CreateShaderResourceView(warp_depth.Get(), nullptr, warp_depth_view.GetAddressOf()))
            && SUCCEEDED(warp->CreateTexture2D(&output_desc, nullptr, warp_output.GetAddressOf()))
            && SUCCEEDED(warp->CreateUnorderedAccessView(warp_output.Get(), nullptr, warp_output_view.GetAddressOf()))
            && SUCCEEDED(warp->CreateBuffer(&constants_desc, &constants_data, warp_constants.GetAddressOf()))
            && SUCCEEDED(warp->CreateComputeShader(__ground_vbao.data(), __ground_vbao.size(), nullptr, warp_shader.GetAddressOf())));
    warp_context->CSSetShader(warp_shader.Get(), nullptr, 0);
    warp_context->CSSetShaderResources(0, 1, warp_depth_view.GetAddressOf());
    warp_context->CSSetUnorderedAccessViews(0, 1, warp_output_view.GetAddressOf(), nullptr);
    warp_context->CSSetConstantBuffers(0, 1, warp_constants.GetAddressOf());
    warp_context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    const std::vector<uint8_t> reference = ReadProxy({.proxy = warp, .context = warp_context}, warp_output.Get(), 4);
    double difference_sum = 0;
    double difference_most = 0;
    size_t within = 0;
    for (size_t index = 0; index < static_cast<size_t>(WIDTH) * HEIGHT; ++index) {
      const double difference = std::abs(Float(occlusion, index) - Float(reference, index));
      difference_sum += difference;
      difference_most = std::max(difference_most, difference);
      if (difference <= (1.0 / 128) + 1e-4) ++within;
    }
    const size_t pixels = static_cast<size_t>(WIDTH) * HEIGHT;
    std::printf("GT-VBAO against WARP: mean difference %.2e, largest %.4f, %zu of %zu within 1 / 128\n", difference_sum / pixels, difference_most, within,
                pixels);
    CHECK(within * 100 >= pixels * 99);

    // The game's swap chain ending (a D3D9 Reset: addon.cpp's destroy_swapchain): the bridge's and the rays' objects go,
    // and after the next frame's camera pass and mask copy ShadeGround makes them again (the same size here: the same
    // occlusion).
    ruse::ray_tracing::ReleaseHost();
    bridge::Release();
    CHECK(rays.occlusion.host == nullptr && rays.shadows == nullptr && state.output.host == nullptr && !state.camera_valid);
    state.camera_valid = true;
    rays.prepass_depth = reinterpret_cast<uint64_t>(depth_surface.Get());
    rays.ground_mask = reinterpret_cast<uint64_t>(scratch.Get());
    host->BeginScene();
    const bool shaded_again = ruse::ray_tracing::ShadeGround(&command_list);
    host->EndScene();
    CHECK(shaded_again && rays.occlusion.host != nullptr && state.output.host != nullptr);
    if (shaded_again) {
      const bool same = ReadHost(host, rays.occlusion.host_surface.Get(), 4) == occlusion;
      std::printf("GT-VBAO after the objects' release: made again, the same occlusion %d\n", same ? 1 : 0);
      CHECK(same);
    }

    // Bend's ground shadows over the same depth (its march needs no helper), a low sun over +x: the floating plateau
    // shades the ground on its -x side. Its combine reads the march's occlusion, which D3D11 bound as null while the
    // march still held it as its output (no pixel shadowed then).
    ruse::ray_tracing::ReleaseHost();
    rays.bend = true;
    rays.toward_sun = {0.894427f, 0.f, 0.447214f};
    rays.prepass_depth = reinterpret_cast<uint64_t>(depth_surface.Get());
    rays.ground_mask = reinterpret_cast<uint64_t>(scratch.Get());
    host->BeginScene();
    const bool bent = ruse::ray_tracing::ShadeGround(&command_list);
    host->EndScene();
    CHECK(bent);
    if (bent) {
      const std::vector<uint8_t> visibility = ReadHost(host, rays.visibility.host_surface.Get(), 4);
      size_t shadowed = 0;
      size_t lit = 0;
      double lowest = 1;
      for (UINT y = 0; y < HEIGHT; ++y) {
        for (UINT x = 0; x < WIDTH; ++x) {
          const size_t index = (static_cast<size_t>(y) * WIDTH) + x;
          if (on_plateau[index] != 0 || HitGround(inverse, ground, x, y).inside < 0.01) continue;
          const double value = Float(visibility, index);
          lowest = std::min(lowest, value);
          if (value < 0.5) ++shadowed;
          if (value > 0.99) ++lit;
        }
      }
      // RTX 5090 2026-11-16: 41-43 shadowed, lowest 0; the passes' old order: 0, lowest 1.
      std::printf("Bend: ground pixels shadowed %zu, lit %zu, lowest visibility %.3f\n", shadowed, lit, lowest);
      CHECK(shadowed >= 20 && lit > 1000);
    }
    rays.bend = false;

    // 10. The trace point (GT-VBAO alone as its ground row).
    ruse::ray_tracing::ReleaseHost();
    const ComPtr<IDirect3DTexture9> main_target = MakeTarget(host, D3DFMT_A2R10G10B10);
    const ComPtr<IDirect3DTexture9> main_depth = MakeTarget(host, INTZ, D3DUSAGE_DEPTHSTENCIL);
    REQUIRE(main_target != nullptr && main_depth != nullptr);
    const ComPtr<IDirect3DSurface9> main_surface = Level(main_target.Get());
    const ComPtr<IDirect3DSurface9> main_depth_surface = Level(main_depth.Get());
    bridge::OnInitDepthView(&host_device, {reinterpret_cast<uint64_t>(main_depth.Get())}, reshade::api::resource_usage::depth_stencil,
                            reshade::api::resource_view_desc(reshade::api::resource_view_type::texture_2d, reshade::api::format::intz, 0, 1, 0, 1),
                            {reinterpret_cast<uint64_t>(main_depth_surface.Get())});
    const auto prepass_depth = reinterpret_cast<uint64_t>(depth_surface.Get());
    const float far_depth = 1.f;
    // A frame's pre-pass as the game draws it: its target and depth bound, the camera pass, the mask's copy, then the
    // main pass's target bound with `main_depth_handle`.
    const auto prepass = [&](uint64_t main_depth_handle) {
      bridge::OnPresent(host);
      rays.sun_frame = state.frame;  // the sun's shaders aren't drawn here
      state.render_target = reinterpret_cast<uint64_t>(scratch_surface.Get());
      state.depth_stencil = prepass_depth;
      ruse::ray_tracing::OnCameraPass();
      ruse::ray_tracing::OnCopyTextureRegion(&command_list, {state.render_target}, 0, nullptr, {reinterpret_cast<uint64_t>(scratch.Get())}, 0, nullptr,
                                             reshade::api::filter_mode::min_mag_mip_linear);
      state.render_target = reinterpret_cast<uint64_t>(main_surface.Get());
      state.depth_stencil = main_depth_handle;
    };
    const auto main_draw = [&] {
      host->BeginScene();
      ruse::ray_tracing::OnSceneDraw(&command_list);
      host->EndScene();
    };
    const auto occlusion_now = [&] { return ReadHost(host, rays.occlusion.host_surface.Get(), 4); };
    // The game's AA off: one depth for both passes, the main pass's draws after its clear of it. Its draws never shade
    // (the build of run_rn2_rt_084045 had only them: no trace); its clear (outside a scene here) does once, over the
    // ground's depth; nothing after the clear does again.
    prepass(prepass_depth);
    main_draw();
    const bool drawn_shaded = rays.traced_frame == state.frame;
    // The game's main pass bound: its target and the depth the clear is about to clear (the trace point samples it: its
    // passes unbind it and give it back).
    host->SetRenderTarget(0, main_surface.Get());
    host->SetDepthStencilSurface(depth_surface.Get());
    ruse::ray_tracing::OnClearDepthStencilView(&command_list, {prepass_depth}, &far_depth, nullptr, 0, nullptr);
    const bool cleared_shaded = rays.traced_frame == state.frame && rays.occlusion_frame == state.frame;
    const bool same_at_clear = cleared_shaded && occlusion_now() == occlusion;
    ComPtr<IDirect3DSurface9> bound_target;
    ComPtr<IDirect3DSurface9> bound_depth;
    host->GetRenderTarget(0, bound_target.GetAddressOf());
    host->GetDepthStencilSurface(bound_depth.GetAddressOf());
    CHECK(bound_target == main_surface && bound_depth == depth_surface);
    REQUIRE(SUCCEEDED(host->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, far_depth, 0)));
    host->SetDepthStencilSurface(nullptr);
    const uint32_t shaded_frame = rays.occlusion_frame;
    ruse::ray_tracing::OnClearDepthStencilView(&command_list, {prepass_depth}, &far_depth, nullptr, 0, nullptr);
    main_draw();
    const bool once = rays.occlusion_frame == shaded_frame && occlusion_now() == occlusion;
    std::printf("Trace point, one depth for both passes: main-pass draw shaded %d, the clear shaded %d (the occlusion test 6's: %d), "
                "nothing after it %d\n", drawn_shaded ? 1 : 0, cleared_shaded ? 1 : 0, same_at_clear ? 1 : 0, once ? 1 : 0);
    CHECK(!drawn_shaded && cleared_shaded && same_at_clear && once);
    // A main pass with its own depth (the B0 trace's layout): the clear of that depth isn't the trace point, its first
    // draw is (the ground's depth drawn again: the clear above took it).
    host->SetRenderTarget(0, scratch_surface.Get());
    host->SetDepthStencilSurface(depth_surface.Get());
    REQUIRE(SUCCEEDED(host->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, far_depth, 0)));
    host->SetRenderState(D3DRS_ZENABLE, TRUE);
    host->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    DrawGround(host, ground_draw, flat_ps.Get(), ground, &VIEW_PROJECTION, VIEW_PROJECTION);
    DrawGround(host, ground_draw, flat_ps.Get(), plateau, &VIEW_PROJECTION, VIEW_PROJECTION);
    host->SetDepthStencilSurface(nullptr);
    const auto own_depth = reinterpret_cast<uint64_t>(main_depth_surface.Get());
    prepass(own_depth);
    ruse::ray_tracing::OnClearDepthStencilView(&command_list, {own_depth}, &far_depth, nullptr, 0, nullptr);
    const bool own_cleared_shaded = rays.traced_frame == state.frame;
    main_draw();
    const bool own_drawn_shaded = rays.traced_frame == state.frame && rays.occlusion_frame == state.frame && occlusion_now() == occlusion;
    std::printf("Trace point, the main pass's own depth: its clear shaded %d, its first draw shaded (the same occlusion) %d\n",
                own_cleared_shaded ? 1 : 0, own_drawn_shaded ? 1 : 0);
    CHECK(!own_cleared_shaded && own_drawn_shaded);
    bridge::OnDestroyDepthView(&host_device, {own_depth});

    // 11. The rays' gate with a helper that never traces (none here: rt_core's Trace answers false).
    struct ProxyQueue : reshade::api::command_queue {
      reshade::api::command_list* list = nullptr;
      uint64_t get_native() const override { return 0; }
      void get_private_data(const uint8_t[16], uint64_t*) const override {}
      void set_private_data(const uint8_t[16], const uint64_t) override {}
      reshade::api::device* get_device() override { return nullptr; }
      reshade::api::command_queue_type get_type() const override { return reshade::api::command_queue_type::graphics; }
      void wait_idle() const override {}
      void flush_immediate_command_list() const override {}
      reshade::api::command_list* get_immediate_command_list() override { return list; }
      void begin_debug_event(const char*, const float[4]) override {}
      void end_debug_event() override {}
      void insert_debug_marker(const char*, const float[4]) override {}
      bool wait(reshade::api::fence, uint64_t) override { return false; }
      bool signal(reshade::api::fence, uint64_t) override { return false; }
      uint64_t get_timestamp_frequency() const override { return 0; }
    } proxy_queue;
    proxy_queue.list = &command_list;
    auto& replay = ruse::replay::state;
    rays.ground = true;
    rays.proxy_queue = &proxy_queue;
    state.ready = true;  // the bridge running, as the rays' rows need it
    replay.enabled = true;
    replay.rays = true;
    replay.recording = true;
    logged.clear();
    // The game's present (rt_core's frame, the replay, this frame's recording as addon.cpp sets it), the pre-pass, then the
    // trace point at the main pass's clear unless `trace_point` is false (a menu, the map table). The shared depth filled
    // with 0.25 before the trace point: true when the trace point's passes copied the ground's depth into it.
    const auto frame = [&](bool trace_point) {
      prepass(prepass_depth);
      ruse::ray_tracing::OnPresent();
      ruse::replay::Replay({});
      replay.recording = ruse::ray_tracing::SceneWanted();
      CHECK(SUCCEEDED(host->ColorFill(state.depth.host_surface.Get(), nullptr, D3DCOLOR_COLORVALUE(0.25f, 0.f, 0.f, 0.f))));
      if (trace_point) {
        ruse::ray_tracing::OnClearDepthStencilView(&command_list, {prepass_depth}, &far_depth, nullptr, 0, nullptr);
      }
      bridge::WaitHost(host);
      const std::vector<uint8_t> shared_depth = ReadHost(host, state.depth.host_surface.Get(), 4);
      bool copied = false;
      for (size_t index = 0; index < shared_depth.size() / 4 && !copied; ++index) copied = std::abs(Float(shared_depth, index) - 0.25f) > 0.01f;
      return copied;
    };
    // GT-VBAO on: its pass each frame whatever the rays do.
    std::vector<uint32_t> tries;
    std::vector<uint32_t> recorded;
    uint32_t occluded = 0;
    const uint32_t first = state.frame + 1;
    rays.rays_frame = first;  // the first try (the game's presents before this test never ran rt_core's frame)
    rays.tried_frame = UINT32_MAX;
    for (uint32_t index = 0; index < 125; ++index) {
      frame(true);
      if (rays.tried_frame == state.frame) tries.push_back(state.frame - first);
      if (replay.recording) recorded.push_back(state.frame - first);
      occluded += (rays.occlusion_frame == state.frame) ? 1u : 0u;
    }
    // A try's own frame and the frame before each later one recorded (the try takes the last frame's draws).
    const bool tries_right = tries == std::vector<uint32_t>{0, 60, 120};
    const bool recorded_right = recorded == std::vector<uint32_t>{0, 59, 60, 119, 120};
    std::printf("Rays' gate, a helper that never traces: %zu tries in 125 frames (frames %u %u %u), %zu frames recorded, GT-VBAO in %u\n",
                tries.size(), tries.empty() ? 0u : tries[0], tries.size() > 1 ? tries[1] : 0u, tries.size() > 2 ? tries[2] : 0u, recorded.size(),
                occluded);
    CHECK(tries_right && recorded_right && occluded == 125);
    // GT-VBAO off: between tries nothing runs (the shared depth untouched), a try copies the ground's depth.
    rays.vbao = false;
    std::vector<uint32_t> copied;
    for (uint32_t index = 125; index < 185; ++index) {
      if (frame(true)) copied.push_back(state.frame - first);
    }
    std::printf("Rays' gate without another ground row: the depth copied in frames %zu (frame %u)\n", copied.size(), copied.empty() ? 0u : copied[0]);
    CHECK(copied == std::vector<uint32_t>{180});
    // A frame due to try without a trace point: no try, the next RAYS_RETRY_FRAMES on; one "not tracing" line in all of
    // it (the first answer; no change after it).
    for (uint32_t index = 185; index < 240; ++index) frame(true);
    REQUIRE(rays.rays_frame == state.frame + 1);
    frame(false);
    const bool skipped = rays.tried_frame != state.frame;
    frame(true);
    const bool pushed = rays.rays_frame == state.frame - 1 + ruse::ray_tracing::RAYS_RETRY_FRAMES && rays.tried_frame != state.frame;
    const auto not_tracing = std::ranges::count_if(logged, [](const std::string& line) { return line.find("[R.U.S.E.] Ray tracing: not tracing") == 0; });
    std::printf("Rays' gate, no trace point at a due frame: no try %d, the next try %u frames on %d; \"not tracing\" lines %td\n", skipped ? 1 : 0,
                ruse::ray_tracing::RAYS_RETRY_FRAMES, pushed ? 1 : 0, not_tracing);
    CHECK(skipped && pushed && not_tracing == 1);
    rays.ground = false;
    rays.proxy_queue = nullptr;
    state.ready = false;
    replay.enabled = false;
    replay.rays = false;
    replay.recording = true;
    twattila::rt_scene::Update({});
  }

  // 9. Guides.
  {
    auto& rays = ruse::ray_tracing::state;
    REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, D3DFMT_A8R8G8B8, &rays.guide_albedo));
    REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, D3DFMT_A16B16G16R16F, &rays.guide_normal));
    REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, D3DFMT_A16B16G16R16F, &rays.guide_water));
    const ComPtr<IDirect3DTexture9> scene = MakeTarget(host, D3DFMT_A2R10G10B10);
    REQUIRE(scene != nullptr);
    const ComPtr<IDirect3DSurface9> scene_surface = Level(scene.Get());
    // Something in them first, then the clear the trace point makes.
    for (IDirect3DSurface9* guide : {rays.guide_albedo.host_surface.Get(), rays.guide_normal.host_surface.Get(), rays.guide_water.host_surface.Get()}) {
      CHECK(SUCCEEDED(host->ColorFill(guide, nullptr, D3DCOLOR_ARGB(255, 255, 255, 255))));
      CHECK(SUCCEEDED(host->ColorFill(guide, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0))));
    }
    const ComPtr<ID3DBlob> guide_code = Compile(GUIDE_PS, "ps_3_0");
    ComPtr<IDirect3DPixelShader9> guide_ps;
    REQUIRE(SUCCEEDED(host->CreatePixelShader(static_cast<const DWORD*>(guide_code->GetBufferPointer()), guide_ps.GetAddressOf())));
    const auto draw = [&](const std::array<float, 16>& outputs, std::array<DWORD, 3> masks, bool blend) {
      host->SetRenderTarget(0, scene_surface.Get());
      ruse::ray_tracing::BindGuides(host, true);
      host->SetDepthStencilSurface(nullptr);
      const D3DVIEWPORT9 viewport = {.X = 0, .Y = 0, .Width = WIDTH, .Height = HEIGHT, .MinZ = 0.f, .MaxZ = 1.f};
      host->SetViewport(&viewport);
      for (const auto [render_state, value] : std::initializer_list<std::pair<D3DRENDERSTATETYPE, DWORD>>{
               {D3DRS_ZENABLE, FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_CULLMODE, D3DCULL_NONE},
               {D3DRS_STENCILENABLE, FALSE}, {D3DRS_SRGBWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, blend ? TRUE : FALSE},
               {D3DRS_SEPARATEALPHABLENDENABLE, FALSE}, {D3DRS_BLENDOP, D3DBLENDOP_ADD}, {D3DRS_SRCBLEND, D3DBLEND_SRCALPHA},
               {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA}, {D3DRS_COLORWRITEENABLE, 0xF}, {D3DRS_COLORWRITEENABLE1, masks[0]},
               {D3DRS_COLORWRITEENABLE2, masks[1]}, {D3DRS_COLORWRITEENABLE3, masks[2]}}) {
        host->SetRenderState(render_state, value);
      }
      const std::array<float, 4> half_pixel = {-1.f / WIDTH, 1.f / HEIGHT, 0.f, 0.f};
      host->SetVertexShader(state.quad.Get());
      host->SetVertexShaderConstantF(0, half_pixel.data(), 1);
      host->SetPixelShader(guide_ps.Get());
      host->SetPixelShaderConstantF(0, outputs.data(), 4);
      host->SetVertexDeclaration(state.quad_layout.Get());
      constexpr std::array<float, 8> STRIP = {-1.f, 1.f, 1.f, 1.f, -1.f, -1.f, 1.f, -1.f};
      CHECK(SUCCEEDED(host->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, STRIP.data(), 2 * sizeof(float))));
    };
    // An opaque terrain draw (oC1 / oC2 written, oC3 masked), then a blended water draw (oC1 masked, oC2 / oC3 written).
    draw({0.5f, 0.5f, 0.5f, 1.f, 0.25f, 0.5f, 0.75f, 1.f, 0.f, 0.f, 1.f, 1.f, 3.f, 4.f, 5.f, 1.f}, {0xF, 0xF, 0}, false);
    draw({1.f, 1.f, 1.f, 0.25f, 1.f, 1.f, 1.f, 0.25f, 1.f, 0.f, 0.f, 0.25f, 8.f, 8.f, 8.f, 1.f}, {0, 0xF, 0xF}, true);
    ruse::ray_tracing::BindGuides(host, false);
    std::array<ComPtr<IDirect3DSurface9>, 3> bound;
    for (DWORD index = 0; index < bound.size(); ++index) host->GetRenderTarget(index + 1, bound[index].GetAddressOf());
    CHECK(bound[0] == nullptr && bound[1] == nullptr && bound[2] == nullptr);
    bridge::WaitHost(host);
    const std::vector<uint8_t> albedo = ReadProxy(devices, rays.guide_albedo.proxy.Get(), 4);
    const std::vector<uint8_t> normal = ReadProxy(devices, rays.guide_normal.proxy.Get(), 8);
    const std::vector<uint8_t> water = ReadProxy(devices, rays.guide_water.proxy.Get(), 8);
    size_t right = 0;
    for (size_t pixel = 0; pixel < static_cast<size_t>(WIDTH) * HEIGHT; ++pixel) {
      const uint8_t* bgra = albedo.data() + (pixel * 4);
      const bool albedo_kept = std::abs(bgra[2] - 64) <= 1 && std::abs(bgra[1] - 128) <= 1 && std::abs(bgra[0] - 191) <= 1 && bgra[3] == 255;
      const bool normal_lerped = std::abs(Half(normal, pixel * 4) - 0.25f) < 0.01f && std::abs(Half(normal, (pixel * 4) + 1)) < 0.01f
                                 && std::abs(Half(normal, (pixel * 4) + 2) - 0.75f) < 0.01f;
      const float water_w = Half(water, (pixel * 4) + 3);
      const bool water_whole = water_w > 0.f && std::abs((Half(water, pixel * 4) / water_w) - 8.f) < 0.05f;
      if (albedo_kept && normal_lerped && water_whole) ++right;
    }
    std::printf("Guides: %zu of %zu pixels right (albedo kept by its mask, normal lerped by the alpha, water rgb / w 8); water w %.3f "
                "(1: each target's own alpha, 0.25: oC0's)\n",
                right, static_cast<size_t>(WIDTH) * HEIGHT, Half(water, 3));
    CHECK(right == static_cast<size_t>(WIDTH) * HEIGHT);
    rays.guide_albedo = {};
    rays.guide_normal = {};
    rays.guide_water = {};
  }

  // 7. H2: the helper's anti-aliasing on an aliased frame drawn with each frame's jitter.
  if (!twattila::dlaa_bridge::HelperInstalled()) {
    std::printf("H2: no renodx-tw-dlaa64.exe beside the test: skipped\n");
  } else {
    // A fresh bridge as the addon starts it (Ready makes the shaders, textures and the helper's set on the first Evaluate).
    state = bridge::State{};
    state.width = WIDTH;
    state.height = HEIGHT;
    state.enabled = true;
    state.upscaler = (upscaler == "fsr") ? twattila::dlaa_bridge::UPSCALER_FSR
                                         : ((upscaler == "xess") ? twattila::dlaa_bridge::UPSCALER_XESS : twattila::dlaa_bridge::UPSCALER_DLSS);
    const ComPtr<IDirect3DTexture9> scene = MakeTarget(host, D3DFMT_A2R10G10B10);
    REQUIRE(scene != nullptr);
    const ComPtr<IDirect3DSurface9> scene_surface = Level(scene.Get());
    std::ranges::copy(VIEW_PROJECTION, state.camera.begin());
    std::ranges::copy(inverse_float, state.camera.begin() + 16);
    state.camera[32] = RENDER_DT;
    state.camera_valid = true;
    state.pixel_texture_0 = reinterpret_cast<uint64_t>(scene.Get());
    state.depth_stencil = reinterpret_cast<uint64_t>(depth_surface.Get());
    state.viewport_is_frame_sized = true;
    register_depth();
    constexpr float BACKGROUND = 0.2f;
    constexpr float FOREGROUND = 0.8f;
    const auto draw_frame = [&](bool jittered) {
      host->SetRenderTarget(0, scene_surface.Get());
      host->SetDepthStencilSurface(depth_surface.Get());
      host->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_COLORVALUE(BACKGROUND, BACKGROUND, BACKGROUND, 1.f), 1.f, 0);
      host->SetRenderState(D3DRS_ZENABLE, TRUE);
      host->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
      host->SetVertexShaderConstantF(0, VIEW_PROJECTION.data(), 4);
      state.vertex_camera = VIEW_PROJECTION;
      state.render_target_is_frame_sized = jittered;
      bridge::OnDraw(&command_list);
      const std::array<float, 4> foreground = {FOREGROUND, FOREGROUND, FOREGROUND, 1.f};
      host->SetPixelShaderConstantF(0, foreground.data(), 1);
      DrawGround(host, ground_draw, flat_ps.Get(), ground, nullptr, VIEW_PROJECTION);
      host->SetDepthStencilSurface(nullptr);
    };
    draw_frame(false);
    const std::vector<uint8_t> aliased = ReadHost(host, scene_surface.Get(), 4);
    // The helper starts in the background (its D3D12 device, the upscaler): frames until its first result (at most 20 s),
    // then every frame must have one.
    const ULONGLONG started = GetTickCount64();
    int waited = 0;
    int evaluated = 0;
    int missed = 0;
    while (evaluated + missed < 24 && GetTickCount64() - started < 20000) {
      bridge::OnPresent(host);
      draw_frame(true);
      host->BeginScene();
      bridge::Evaluate(&command_list, __bridge_quad, __bridge_prepare, __bridge_writeback);
      host->EndScene();
      if (!state.reset) {
        ++evaluated;
      } else if (evaluated > 0) {
        ++missed;
      } else {
        ++waited;
        Sleep(10);
      }
    }
    std::printf("H2 %s: first result after %d frames (%llu ms), then %d evaluated, %d missed; ready %d, failed %d, %s\n", upscaler.c_str(), waited,
                GetTickCount64() - started, evaluated, missed, state.ready ? 1 : 0, state.failed ? 1 : 0, twattila::dlaa_bridge::bridge.status.c_str());
    CHECK(evaluated == 24 && missed == 0);
    const std::vector<uint8_t> result = ReadHost(host, scene_surface.Get(), 4);
    const auto red = [](const std::vector<uint8_t>& pixels, int x, int y) {
      uint32_t pixel = 0;
      std::memcpy(&pixel, pixels.data() + ((static_cast<size_t>(y) * WIDTH + x) * 4), 4);
      return static_cast<int>(Codes(pixel, D3DFMT_A2R10G10B10)[0]);
    };
    const int background = static_cast<int>(std::lround(BACKGROUND * 1023));
    const int foreground = static_cast<int>(std::lround(FOREGROUND * 1023));
    size_t flat = 0;
    size_t flat_kept = 0;
    size_t edge = 0;
    size_t edge_between = 0;
    for (int y = 3; y < static_cast<int>(HEIGHT) - 3; ++y) {
      for (int x = 3; x < static_cast<int>(WIDTH) - 3; ++x) {
        int lowest = 1023;
        int highest = 0;
        for (int dy = -3; dy <= 3; ++dy) {
          for (int dx = -3; dx <= 3; ++dx) {
            lowest = std::min(lowest, red(aliased, x + dx, y + dy));
            highest = std::max(highest, red(aliased, x + dx, y + dy));
          }
        }
        const int got = red(result, x, y);
        if (lowest == highest) {
          ++flat;
          if (std::abs(got - red(aliased, x, y)) <= 3) ++flat_kept;
        } else if (std::abs(red(aliased, x + 1, y) - red(aliased, x, y)) > 100 || std::abs(red(aliased, x, y + 1) - red(aliased, x, y)) > 100) {
          ++edge;
          if (got > background + 8 && got < foreground - 8) ++edge_between;
        }
      }
    }
    std::printf("H2 %s: flat %zu (kept %zu), edge %zu (between the colours %zu)\n", upscaler.c_str(), flat, flat_kept, edge, edge_between);
    CHECK(flat > 1000 && flat_kept >= flat * 99 / 100);
    CHECK(edge > 50 && edge_between * 2 >= edge);

    // 8. Frame generation through R.U.S.E.'s hand-over (frame_generation.hpp's Install and present event need ReShade:
    // their state set here, the present's request made as OnReshadePresent makes it).
    if (upscaler == "dlss") {
      namespace generation = twattila::frame_generation;
      auto& generation_state = generation::internal::state;
      auto& helper = twattila::dlaa_bridge::bridge;
      const ShaderInjectData injection = {.peak_white_nits = 1000.f, .diffuse_white_nits = 203.f, .graphics_white_nits = 203.f, .swap_chain_encoding = 0.f};
      generation_state.vertex_code = __swap_chain_proxy_vertex_shader_dx11;
      generation_state.pixel_code = __swap_chain_proxy_pixel_shader_dx11;
      generation_state.hdr10_pixel_code = __fg_hdr10;
      generation_state.injection = &injection;
      generation_state.injection_bytes = sizeof(injection);
      generation_state.format = DXGI_FORMAT_R10G10B10A2_UNORM;  // the proxy's HDR10 back buffer, as its first present sets it
      generation::Update({.multiplier = 2});
      RECT frame_rect = {0, 0, static_cast<LONG>(WIDTH), static_cast<LONG>(HEIGHT)};
      AdjustWindowRect(&frame_rect, WS_OVERLAPPEDWINDOW, FALSE);
      // Topmost while it runs: covered by another window, DLSS-G presents no generated frames.
      SetWindowPos(devices.window, HWND_TOPMOST, 40, 40, frame_rect.right - frame_rect.left, frame_rect.bottom - frame_rect.top, SWP_NOACTIVATE);
      ShowWindow(devices.window, SW_SHOW);
      SetForegroundWindow(devices.window);
      const ComPtr<IDirect3DTexture9> post = MakeTarget(host, D3DFMT_A16B16G16R16F);
      REQUIRE(post != nullptr);
      const ComPtr<IDirect3DSurface9> post_surface = Level(post.Get());
      const D3D11_TEXTURE2D_DESC back_desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R10G10B10A2_UNORM,
                                              .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
      ComPtr<ID3D11Texture2D> back;
      ComPtr<ID3D11RenderTargetView> back_view;
      REQUIRE(SUCCEEDED(devices.proxy->CreateTexture2D(&back_desc, nullptr, back.GetAddressOf()))
              && SUCCEEDED(devices.proxy->CreateRenderTargetView(back.Get(), nullptr, back_view.GetAddressOf())));
      auto* context = devices.context.Get();
      uint32_t frame_index = 0;
      int ready_frames = 0;
      int requests = 0;
      int presented = 0;
      int refused = 0;  // after the first presented
      for (int frame = 0; frame < 300; ++frame) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        draw_frame(true);
        host->BeginScene();
        bridge::Evaluate(&command_list, __bridge_quad, __bridge_prepare, __bridge_writeback);
        host->EndScene();
        // The final post into the back buffer's FP16 clone (grey, gamma 2.2), then the UI's first draw, then the present.
        host->SetRenderTarget(0, post_surface.Get());
        host->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_COLORVALUE(0.5f, 0.5f, 0.5f, 1.f), 1.f, 0);
        // In the game both run inside its scene (the UI draw's event): StretchRect there.
        host->BeginScene();
        bridge::OnFinalPost(&command_list);
        bridge::KeepHudless(host);
        host->EndScene();
        bridge::OnPresent(host);
        host->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
        if (state.generation_ready) ++ready_frames;
        // The proxy's present: KeepFrame, then frame_generation's present event with the proxy's back buffer.
        bridge::KeepFrame(context);
        const std::array<float, 4> back_colour = {0.25f, 0.3f, 0.35f, 1.f};
        context->ClearRenderTargetView(back_view.Get(), back_colour.data());
        const bool kept = std::exchange(generation_state.kept, false);
        if (!helper.fg_frame || helper.fg_format != generation_state.format || helper.width != WIDTH || helper.height != HEIGHT) continue;
        twattila::dlaa_bridge::BeforeSharedWrites(context);
        context->CopyResource(helper.fg_frame.Get(), back.Get());
        const generation::Inputs& inputs = generation_state.inputs;
        twattila::dlaa_bridge::FrameRequest request = {.window = devices.window, .multiplier = 2, .hud_recomposition = true,
                                                       .hudless = kept && std::exchange(generation_state.hudless, false), .inputs = kept,
                                                       .frame_index = ++frame_index};
        if (kept) {
          request.width = inputs.width;
          request.height = inputs.height;
          request.jitter_x = inputs.jitter_x;
          request.jitter_y = inputs.jitter_y;
          request.reset = inputs.reset;
          request.view_to_clip = inputs.view_to_clip;
          request.view_projection = inputs.view_projection;
          request.previous_view_projection = inputs.previous_view_projection;
          request.camera = inputs.camera;
          request.near_plane = inputs.near_plane;
          request.far_plane = inputs.far_plane;
        }
        ++requests;
        if (twattila::dlaa_bridge::PresentFrame(context, request)) {
          ++presented;
        } else if (presented > 0) {
          ++refused;
        }
        if (frame % 60 == 59) {
          std::printf("FG frame %d: %u shown a rendered frame; %s\n", frame + 1, twattila::dlaa_bridge::FrameGenerationPresented() & 0xFFu,
                      twattila::dlaa_bridge::FrameGenerationStatus().c_str());
        }
        Sleep(16);
      }
      // The HUD-less image the helper got: the post's grey (128 / 255) through the swap chain shader's HDR10 (gamma 2.2,
      // 203 nits, PQ).
      const double light = std::pow(128.0 / 255.0, 2.2) * 203.0 / 10000.0;
      const double m1 = 0.1593017578125;
      const double m2 = 78.84375;
      const double powered = std::pow(light, m1);
      const double expected_code = std::pow((0.8359375 + (18.8515625 * powered)) / (1.0 + (18.6875 * powered)), m2) * 1023.0;
      const std::vector<uint8_t> hudless = ReadProxy(devices, helper.fg_hudless.Get(), 4);
      double worst_code = 0;
      for (size_t index = 0; index < hudless.size() / 4; ++index) {
        uint32_t pixel = 0;
        std::memcpy(&pixel, hudless.data() + (index * 4), 4);
        for (const uint32_t code : {pixel & 1023u, (pixel >> 10) & 1023u, (pixel >> 20) & 1023u}) {
          worst_code = std::max(worst_code, std::abs(static_cast<double>(code) - expected_code));
        }
      }
      const generation::Inputs& inputs = generation_state.inputs;
      std::printf("FG: %d frames ready, %d requests, %d presented, %d refused after, failed %d, %u shown a rendered frame; HUD-less worst %.1f codes from %.1f; near %.1f, "
                  "far %.4g, eye (%.0f, %.0f, %.0f), forward (%.3f, %.3f, %.3f); %s\n",
                  ready_frames, requests, presented, refused, twattila::dlaa_bridge::FrameGenerationFailed() ? 1 : 0,
                  twattila::dlaa_bridge::FrameGenerationPresented() & 0xFFu, worst_code, expected_code, inputs.near_plane, inputs.far_plane, inputs.camera[0],
                  inputs.camera[1], inputs.camera[2], inputs.camera[9], inputs.camera[10], inputs.camera[11],
                  twattila::dlaa_bridge::FrameGenerationStatus().c_str());
      const Vector eye = Unproject(inverse, 0, 0, 0);
      const Vector far_eye = Unproject(inverse, 0, 0, 1);
      std::array<double, 3> forward = {far_eye[0] - eye[0], far_eye[1] - eye[1], far_eye[2] - eye[2]};
      const double forward_length = std::sqrt((forward[0] * forward[0]) + (forward[1] * forward[1]) + (forward[2] * forward[2]));
      double forward_error = 0;
      for (int axis = 0; axis < 3; ++axis) forward_error = std::max(forward_error, std::abs((forward[axis] / forward_length) - inputs.camera[9 + axis]));
      CHECK(ready_frames >= 298);
      CHECK(requests >= 298 && presented >= 280 && refused == 0);
      CHECK(!twattila::dlaa_bridge::FrameGenerationFailed());
      CHECK(worst_code <= 2.0);
      CHECK(std::abs(inputs.near_plane - 5000.0) < 5.0 && std::abs(inputs.far_plane / 5e7 - 1.0) < 0.05);
      CHECK(forward_error < 1e-3);
      generation::Update({});
      SetWindowPos(devices.window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      ShowWindow(devices.window, SW_HIDE);
    }
    state = bridge::State{};
    twattila::dlaa_bridge::Shutdown();
  }

  renodx::utils::device_proxy::proxy_device_reshade = nullptr;
  std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}
