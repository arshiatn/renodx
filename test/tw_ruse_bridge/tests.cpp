// R.U.S.E.'s D3D9 bridge (src/games/ruse/bridge.hpp) on the GPU: a D3D9Ex and a D3D11 device on one adapter, the addon's
// own bridge shaders (build\ruse.include: build renodx-ruse.addon64 first), the census VS 0xF01502EA (the ground
// pre-pass's; argv[1] the census cso folder) and the B0 trace's constants (u1_trace_200820, trace2 3654: the camera pass's
// c0-c8, the ground VS's c0-c12).
// H1 (no helper):
// 1. Shared pairs: the four textures made by D3D11 (a KMT handle) and opened by D3D9Ex; prepare (D3D9) writes colour /
//    depth / motion that D3D11 reads, D3D11 writes the output that writeback (D3D9) reads.
// 2. INTZ: the ground quad drawn by the census VS into an INTZ depth; prepare's depth (oC1, R32F) read on D3D11 = the
//    CPU's z / w of that plane at each pixel's D3D9 centre (worst 1e-4), 1 (cleared) off the quad.
// 3. 10-bit round trip: every code of the resolved scene (A2R10G10B10 and A2B10G10R10, those that render) through
//    prepare (pow(code x HDRParam, 2.2) in RGBA16F, relative 2e-3) and writeback (output = colour) back exactly, HDRParam
//    1 / 2 / 4.
// 4. Motion: prepare's camera motion from that depth (c4-c7 the inverse view-projection, c0-c3 a previous one with the
//    camera moved) against the CPU's reprojection of the same depth (worst 0.05 px or 0.3%); with the game's velocity
//    (s2 .yz, c9.w 1): an object's patch takes its own, the rest the camera's.
// 5. Jitter: OnDraw on a scene draw writes rows 0 / 1 of c0-c3 + offset x row 3 (read back from the device), the census
//    VS drawn with them moves clip.xy by offset x clip.w (worst 1% of the offset), the next non-scene draw gets the game's
//    c0-c3 back bit for bit.
// H2 (renodx-tw-dlaa64.exe beside this exe; argv[2] dlss / fsr / xess):
// 6. Evaluate on a synthetic aliased frame (the ground quad's hard edges drawn with each frame's jitter) for 24 frames:
//    a result each frame after the first, written back into the scene; flat pixels within 3 codes of the aliased
//    reference (99%), at least half of the edge pixels between the two colours.
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
#include "src/games/ruse/bridge.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

extern "C" __declspec(dllexport) const char* const NAME = "R.U.S.E. bridge test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  std::printf("  log: %s\n", message);
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
// 15,000 units in front of the near plane along the view's centre, its half size a quarter of the view there.
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
  const double half = 0.25 * (DISTANCE + 5000) * 0.414;
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
  host->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, CORNERS.data(), 4 * sizeof(int16_t));
}

constexpr char JITTER_PS[] = R"(
float4 main(float3 current : TEXCOORD1, float3 previous : TEXCOORD2) : COLOR0 {
  return float4((current.xy - previous.xy) / previous.z, previous.z, 1);
})";

constexpr char FLAT_PS[] = R"(
float4 colour : register(c0);
float4 main() : COLOR0 { return colour; })";

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
  REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, DXGI_FORMAT_R16G16B16A16_FLOAT, D3DFMT_A16B16G16R16F, &state.color));
  REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, DXGI_FORMAT_R32_FLOAT, D3DFMT_R32F, &state.depth));
  REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, DXGI_FORMAT_R16G16_FLOAT, D3DFMT_G16R16F, &state.motion));
  REQUIRE(bridge::MakeShared(devices.proxy.Get(), host, DXGI_FORMAT_R16G16B16A16_FLOAT, D3DFMT_A16B16G16R16F, &state.output));

  // 2. INTZ: the ground into the game's (readable) depth, read through prepare.
  const ComPtr<IDirect3DTexture9> depth_texture = MakeTarget(host, INTZ, D3DUSAGE_DEPTHSTENCIL);
  REQUIRE(depth_texture != nullptr);
  const ComPtr<IDirect3DSurface9> depth_surface = Level(depth_texture.Get());
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
      bridge::DrawQuad(host, inputs, state.prepare.Get(), sources, constants);
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
      bridge::DrawQuad(host, target, state.writeback.Get(), result, constants);
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
  state.render_target_is_frame_sized = true;
  state.depth_stencil = reinterpret_cast<uint64_t>(depth_surface.Get());
  state.intz_depths = {state.depth_stencil};
  state.vertex_camera = VIEW_PROJECTION;
  state.jitter = {0.3125f, -0.2f};
  host->SetVertexShaderConstantF(0, VIEW_PROJECTION.data(), 4);
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
    worst_jitter = std::max({worst_jitter, std::abs(Float(moved_pixels, index * 4) - offset_x) / std::abs(offset_x),
                             std::abs(Float(moved_pixels, (index * 4) + 1) - offset_y) / std::abs(offset_y)});
  }
  std::printf("jitter: %zu pixels, worst %.2e of the offset\n", covered, worst_jitter);
  CHECK(covered > 1000);
  CHECK(worst_jitter < 0.01);
  state.render_target_is_frame_sized = false;
  bridge::OnDraw(&command_list);
  host->GetVertexShaderConstantF(0, written.data(), 4);
  CHECK(std::memcmp(written.data(), VIEW_PROJECTION.data(), sizeof(written)) == 0);
  CHECK(!state.jitter_written);

  // 6. H2: the helper's anti-aliasing on an aliased frame drawn with each frame's jitter.
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
    state.scene_depth = reinterpret_cast<uint64_t>(depth_surface.Get());
    state.depth_stencil = state.scene_depth;
    state.intz_depths = {state.scene_depth};
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
    int evaluated = 0;
    for (int frame = 0; frame < 24; ++frame) {
      bridge::OnPresent();
      draw_frame(true);
      bridge::Evaluate(&command_list, __bridge_quad, __bridge_prepare, __bridge_writeback);
      if (!state.reset) ++evaluated;
    }
    std::printf("H2 %s: %d of 24 frames evaluated, ready %d, failed %d\n", upscaler.c_str(), evaluated, state.ready ? 1 : 0, state.failed ? 1 : 0);
    CHECK(evaluated >= 23);
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
    state = bridge::State{};
    twattila::dlaa_bridge::Shutdown();
  }

  renodx::utils::device_proxy::proxy_device_reshade = nullptr;
  std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}
