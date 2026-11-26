// NRD's view depth from the helper's NRD inputs (dlaa_helper/rt_nrd_front.hlsl; X19, critic fa4 B1), on WARP: the shader
// compiled from this tree as build.ps1 compiles it, over a 16 x 9 view of a tilted ground plane from a turned camera.
// 1. NRD_FRONT_AMBIENT (b3's camera): IN_VIEWZ = the view's z (clip.w; NRD.hlsli:20-21 "linear view space Z", NRDDescs.h
//    IN_VIEWZ), not the camera distance positions.w holds (rt_positions.cs_5_0.hlsl). Sky texels: SKY_VIEW_Z.
// 2. NRD_FRONT_BOUNCE (b2's camera): the same view z, and REBLUR's normalised hit distance on it,
//    saturate(hit / (3 + 0.1 |viewZ|)) (NRD.hlsli _REBLUR_GetHitDistanceNormalization, HIT_DISTANCE_PARAMETERS, roughness 1).
// 3. NRD_FRONT_SUN and NRD_FRONT_REFLECT compile.
// CONTROL: the same passes built with -D NRD_FRONT_DISTANCE_Z (the code before X19) must fail 1 and 2 at the off-centre
// texels (the corner's distance is >= 1.2 x its view z), so the checks see the bug.
// tests.exe <repository> <folder of dxcompiler.dll and dxil.dll> <NRD's Shaders folder>
#include <windows.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t WIDTH = 16;
constexpr uint32_t HEIGHT = 9;
constexpr float SKY_VIEW_Z = 1.0e7f;  // rt_nrd_front.hlsl
constexpr float HIT = 7.5f;           // metres: the Light Bounce's hit distance at every texel

struct Vec3 {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
};

Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 Normalize(const Vec3& v) { return Scale(v, 1.f / std::sqrt(Dot(v, v))); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf("usage: tests.exe <repository> <dxc folder> <NRD Shaders folder>\n");
    return 2;
  }
  const std::string repository = argv[1];
  const std::string dxc_folder = argv[2];
  const std::wstring nrd_shaders(argv[3], argv[3] + std::strlen(argv[3]));
#define CHECK(x)                                       \
  do {                                                 \
    if (!(x)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #x); \
      return 1;                                        \
    }                                                  \
  } while (false)

  CHECK(LoadLibraryA((dxc_folder + "\\dxil.dll").c_str()) != nullptr);
  const HMODULE dxc_module = LoadLibraryA((dxc_folder + "\\dxcompiler.dll").c_str());
  CHECK(dxc_module != nullptr);
  const auto dxc_create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxc_module, "DxcCreateInstance"));
  CHECK(dxc_create != nullptr);
  ComPtr<IDxcUtils> utils;
  ComPtr<IDxcCompiler3> compiler;
  CHECK(SUCCEEDED(dxc_create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))));
  CHECK(SUCCEEDED(dxc_create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))));
  ComPtr<IDxcIncludeHandler> includes;
  CHECK(SUCCEEDED(utils->CreateDefaultIncludeHandler(&includes)));

  ComPtr<IDXGIFactory4> factory;
  CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
  ComPtr<IDXGIAdapter> warp;
  CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
  ComPtr<ID3D12Device> device;
  CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))));
  ComPtr<ID3D12CommandQueue> queue;
  const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
  ComPtr<ID3D12CommandAllocator> allocator;
  CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
  ComPtr<ID3D12GraphicsCommandList> list;
  CHECK(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
  ComPtr<ID3D12Fence> fence;
  CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
  uint64_t fence_value = 0;
  const auto execute = [&]() {
    list->Close();
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.Get(), ++fence_value);
    fence->SetEventOnCompletion(fence_value, nullptr);
    allocator->Reset();
    list->Reset(allocator.Get(), nullptr);
    return device->GetDeviceRemovedReason() == S_OK;
  };
  const auto buffer = [&](uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
    const D3D12_HEAP_PROPERTIES properties = {.Type = heap};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                      .Width = std::max<uint64_t>(size, 256),
                                      .Height = 1,
                                      .DepthOrArraySize = 1,
                                      .MipLevels = 1,
                                      .SampleDesc = {.Count = 1},
                                      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ComPtr<ID3D12Resource> resource;
    device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));
    return resource;
  };
  const auto texture = [&](uint32_t width, uint32_t height, DXGI_FORMAT format) {
    const D3D12_HEAP_PROPERTIES properties = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                                      .Width = width,
                                      .Height = height,
                                      .DepthOrArraySize = 1,
                                      .MipLevels = 1,
                                      .Format = format,
                                      .SampleDesc = {.Count = 1},
                                      .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS};
    ComPtr<ID3D12Resource> resource;
    device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
    return resource;
  };
  std::vector<ComPtr<ID3D12Resource>> staging;
  // A texture's texels (texel_size bytes each, rows packed) in, or out after the work recorded so far.
  const auto copy = [&](ID3D12Resource* target, void* texels, uint32_t texel_size, bool in) {
    const D3D12_RESOURCE_DESC desc = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    uint64_t total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    staging.push_back(buffer(total, in ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK, in ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST));
    const D3D12_TEXTURE_COPY_LOCATION texture_place = {.pResource = target, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
    const D3D12_TEXTURE_COPY_LOCATION buffer_place = {.pResource = staging.back().Get(), .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = footprint};
    uint8_t* mapped = nullptr;
    const size_t row = static_cast<size_t>(desc.Width) * texel_size;
    if (in) {
      staging.back()->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
      for (uint32_t y = 0; y < desc.Height; ++y) std::memcpy(mapped + y * footprint.Footprint.RowPitch, static_cast<uint8_t*>(texels) + y * row, row);
      staging.back()->Unmap(0, nullptr);
      list->CopyTextureRegion(&texture_place, 0, 0, 0, &buffer_place, nullptr);
      return true;
    }
    list->CopyTextureRegion(&buffer_place, 0, 0, 0, &texture_place, nullptr);
    if (!execute()) return false;
    staging.back()->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < desc.Height; ++y) std::memcpy(static_cast<uint8_t*>(texels) + y * row, mapped + y * footprint.Footprint.RowPitch, row);
    staging.back()->Unmap(0, nullptr);
    return true;
  };

  // The camera: at (5, 10, -3), turned 17 degrees right and 11 down, 60-degree vertical FOV, 16:9; row 3 its forward axis
  // (nrd_d3d12.hpp Run's world_to_view), the projection D3D's reversed-z-free perspective (near 0.5, far 5000).
  const Vec3 camera = {5.f, 10.f, -3.f};
  const Vec3 forward = Normalize({std::sin(0.3f), -std::sin(0.19f), std::cos(0.3f)});
  const Vec3 right = Normalize(Cross({0.f, 1.f, 0.f}, forward));
  const Vec3 up = Cross(forward, right);
  const float tan_y = std::tan(30.f * 3.14159265f / 180.f);
  const float tan_x = tan_y * 16.f / 9.f;
  const float near_z = 0.5f;
  const float far_z = 5000.f;
  const float depth_scale = far_z / (far_z - near_z);
  std::array<float, 16> view_projection = {};
  const auto set_row = [&](size_t index, const Vec3& axis, float scale, float bias) {
    view_projection[index * 4] = axis.x * scale;
    view_projection[index * 4 + 1] = axis.y * scale;
    view_projection[index * 4 + 2] = axis.z * scale;
    view_projection[index * 4 + 3] = -Dot(axis, camera) * scale + bias;
  };
  set_row(0, right, 1.f / tan_x, 0.f);
  set_row(1, up, 1.f / tan_y, 0.f);
  set_row(2, forward, depth_scale, -near_z * depth_scale);
  set_row(3, forward, 1.f, 0.f);
  // Each pixel's ray meets the ground plane y = 0.25 x (tilted); the top row and one pixel are sky (w 0).
  std::vector<std::array<float, 4>> positions(WIDTH * HEIGHT);
  std::vector<float> view_z(WIDTH * HEIGHT);
  std::vector<float> distance(WIDTH * HEIGHT);
  for (uint32_t y = 0; y < HEIGHT; ++y) {
    for (uint32_t x = 0; x < WIDTH; ++x) {
      const size_t index = y * WIDTH + x;
      const float ndc_x = (x + 0.5f) / WIDTH * 2.f - 1.f;
      const float ndc_y = 1.f - (y + 0.5f) / HEIGHT * 2.f;
      const Vec3 direction = Normalize(Add(forward, Add(Scale(right, ndc_x * tan_x), Scale(up, ndc_y * tan_y))));
      const Vec3 plane = {-0.25f, 1.f, 0.f};
      const float t = -Dot(plane, camera) / Dot(plane, direction);
      if (y == 0 || (x == 5 && y == 4) || t <= 0.f) continue;
      const Vec3 point = Add(camera, Scale(direction, t));
      positions[index] = {point.x, point.y, point.z, t};
      view_z[index] = Dot(forward, Add(point, Scale(camera, -1.f)));
      distance[index] = t;
    }
  }
  const size_t corner = (HEIGHT - 1) * WIDTH;  // bottom left
  std::printf("bottom-left texel: distance %.4f, view z %.4f (x%.3f)\n", distance[corner], view_z[corner], distance[corner] / view_z[corner]);
  CHECK(view_z[corner] > 0.f && distance[corner] >= 1.2f * view_z[corner]);

  ComPtr<ID3D12Resource> positions_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT);
  ComPtr<ID3D12Resource> sun_texture = texture(2, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
  std::array<float, 8> sun = {0.f, 1.f, 0.f, 1.f, camera.x, camera.y, camera.z, 1.f};
  CHECK(copy(positions_texture.Get(), positions.data(), 16, true));
  CHECK(copy(sun_texture.Get(), sun.data(), 16, true));
  std::vector<float> hit(WIDTH * HEIGHT, HIT);
  ComPtr<ID3D12Resource> hit_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT);
  CHECK(copy(hit_texture.Get(), hit.data(), 4, true));
  std::vector<std::array<float, 4>> bounce(WIDTH * HEIGHT, {0.5f, 0.5f, 0.5f, 1.f});
  ComPtr<ID3D12Resource> bounce_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT);
  CHECK(copy(bounce_texture.Get(), bounce.data(), 16, true));
  CHECK(execute());

  // One front pass: compiled with `defines`, every binding from its reflection bound by name to `bound` (none may be
  // missing), the cbuffers filled by name; it runs over the view, then its view_z and signal come back.
  struct Result {
    bool ran = false;
    std::vector<float> view_z;
    std::vector<std::array<float, 4>> signal;
  };
  const auto run = [&](const std::vector<std::wstring>& defines, const std::map<std::string, ID3D12Resource*>& bound, bool ambient_pass, bool dispatch) {
    Result out;
    const std::wstring path = std::wstring(repository.begin(), repository.end()) + L"\\src\\games\\twattila\\dlaa_helper\\rt_nrd_front.hlsl";
    ComPtr<IDxcBlobEncoding> source;
    if (FAILED(utils->LoadFile(path.c_str(), nullptr, &source))) return out;
    std::vector<LPCWSTR> arguments = {path.c_str(), L"-T", L"cs_6_5", L"-E", L"main", L"-O3", L"-I", nrd_shaders.c_str()};
    for (const std::wstring& define : defines) arguments.insert(arguments.end(), {L"-D", define.c_str()});
    const DxcBuffer source_buffer = {.Ptr = source->GetBufferPointer(), .Size = source->GetBufferSize(), .Encoding = DXC_CP_ACP};
    ComPtr<IDxcResult> result;
    compiler->Compile(&source_buffer, arguments.data(), static_cast<UINT32>(arguments.size()), includes.Get(), IID_PPV_ARGS(&result));
    HRESULT status = E_FAIL;
    result->GetStatus(&status);
    if (FAILED(status)) {
      ComPtr<IDxcBlobUtf8> errors;
      result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
      std::printf("compile failed: %s\n", (errors != nullptr) ? errors->GetStringPointer() : "");
      return out;
    }
    if (!dispatch) {
      out.ran = true;
      return out;
    }
    ComPtr<IDxcBlob> object;
    ComPtr<IDxcBlob> reflection_blob;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
    result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflection_blob), nullptr);
    const DxcBuffer reflection_buffer = {.Ptr = reflection_blob->GetBufferPointer(), .Size = reflection_blob->GetBufferSize()};
    ComPtr<ID3D12ShaderReflection> reflection;
    if (FAILED(utils->CreateReflection(&reflection_buffer, IID_PPV_ARGS(&reflection)))) return out;
    D3D12_SHADER_DESC shader_desc = {};
    reflection->GetDesc(&shader_desc);
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
      D3D12_SHADER_INPUT_BIND_DESC bind = {};
      reflection->GetResourceBindingDesc(index, &bind);
      const D3D12_DESCRIPTOR_RANGE_TYPE type = (bind.Type == D3D_SIT_CBUFFER) ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV
                                               : (bind.Type == D3D_SIT_UAV_RWTYPED) ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
                                                                                    : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      ranges.push_back({.RangeType = type, .NumDescriptors = 1, .BaseShaderRegister = bind.BindPoint, .RegisterSpace = bind.Space,
                        .OffsetInDescriptorsFromTableStart = index});
    }
    const D3D12_ROOT_PARAMETER table = {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                                        .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}};
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = 1, .pParameters = &table};
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> root_errors;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &root_errors))) return out;
    if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root)))) return out;
    const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {.pRootSignature = root.Get(),
                                                             .CS = {.pShaderBytecode = object->GetBufferPointer(), .BytecodeLength = object->GetBufferSize()}};
    if (FAILED(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline)))) return out;
    ComPtr<ID3D12DescriptorHeap> heap;
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = std::max(shader_desc.BoundResources, 1u),
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)))) return out;
    // Outputs: view_z (R32F) and signal (RGBA32F, or R8 for the ambient's) and the normals (R10G10B10A2), written by the
    // pass; the ambient's input (R8, zeros).
    ComPtr<ID3D12Resource> view_z_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT);
    ComPtr<ID3D12Resource> signal_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> normal_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R10G10B10A2_UNORM);
    ComPtr<ID3D12Resource> ambient_signal = texture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM);
    ComPtr<ID3D12Resource> ambient_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R8_UNORM);
    std::map<std::string, ID3D12Resource*> all = bound;
    all.insert({{"view_z", view_z_texture.Get()}, {"normal_roughness", normal_texture.Get()}, {"ambient", ambient_texture.Get()}});
    all["signal"] = ambient_pass ? ambient_signal.Get() : signal_texture.Get();
    std::vector<ComPtr<ID3D12Resource>> constants;
    const UINT step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < shader_desc.BoundResources; ++index, handle.ptr += step) {
      D3D12_SHADER_INPUT_BIND_DESC bind = {};
      reflection->GetResourceBindingDesc(index, &bind);
      if (bind.Type == D3D_SIT_CBUFFER) {
        ID3D12ShaderReflectionConstantBuffer* cbuffer = reflection->GetConstantBufferByName(bind.Name);
        D3D12_SHADER_BUFFER_DESC cbuffer_desc = {};
        cbuffer->GetDesc(&cbuffer_desc);
        const UINT size = (cbuffer_desc.Size + 255u) & ~255u;
        constants.push_back(buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ));
        uint8_t* mapped = nullptr;
        constants.back()->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memset(mapped, 0, size);
        const auto set = [&](const char* name, const void* value, size_t value_size) {
          D3D12_SHADER_VARIABLE_DESC variable = {};
          if (SUCCEEDED(cbuffer->GetVariableByName(name)->GetDesc(&variable))) std::memcpy(mapped + variable.StartOffset, value, std::min<size_t>(value_size, variable.Size));
        };
        const std::array<uint32_t, 3> sizes = {WIDTH, HEIGHT, 1u};
        set("width", &sizes[0], 4);
        set("height", &sizes[1], 4);
        set("bounce_width", &sizes[0], 4);
        set("bounce_height", &sizes[1], 4);
        set("bounce_scale", &sizes[2], 4);
        set("view_projection", view_projection.data(), sizeof(view_projection));
        const D3D12_CONSTANT_BUFFER_VIEW_DESC view = {.BufferLocation = constants.back()->GetGPUVirtualAddress(), .SizeInBytes = size};
        device->CreateConstantBufferView(&view, handle);
        continue;
      }
      const auto found = all.find(bind.Name);
      if (found == all.end()) {
        std::printf("FAIL: the pass binds %s, which the test does not fill\n", bind.Name);
        return out;
      }
      if (bind.Type == D3D_SIT_UAV_RWTYPED) {
        const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = found->second->GetDesc().Format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
        device->CreateUnorderedAccessView(found->second, nullptr, &view, handle);
      } else {
        const D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = found->second->GetDesc().Format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Texture2D = {.MipLevels = 1}};
        device->CreateShaderResourceView(found->second, &view, handle);
      }
    }
    ID3D12DescriptorHeap* heaps[] = {heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root.Get());
    list->SetPipelineState(pipeline.Get());
    list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    if (!execute()) return out;
    out.view_z.resize(WIDTH * HEIGHT);
    out.signal.resize(WIDTH * HEIGHT);
    out.ran = copy(view_z_texture.Get(), out.view_z.data(), 4, false) && copy(signal_texture.Get(), out.signal.data(), 16, false);
    return out;
  };

  // A pass's view z (and with `bounce` its normalised hit distance) against the exact values: the texels off by more than
  // the float rounding.
  const auto wrong_texels = [&](const Result& result, bool bounce_signal) {
    uint32_t wrong = 0;
    for (size_t index = 0; index < positions.size(); ++index) {
      const bool sky = positions[index][3] <= 0.f;
      const float expected = sky ? SKY_VIEW_Z : view_z[index];
      bool ok = std::abs(result.view_z[index] - expected) <= 2e-5f * expected;
      if (bounce_signal) {
        const float norm = sky ? 0.f : std::clamp(HIT / (3.f + 0.1f * view_z[index]), 0.f, 1.f);
        ok = ok && std::abs(result.signal[index][3] - norm) <= 2e-5f;
      }
      wrong += ok ? 0u : 1u;
    }
    return wrong;
  };
  const std::map<std::string, ID3D12Resource*> ambient_inputs = {{"positions", positions_texture.Get()}, {"sun", sun_texture.Get()}};
  const std::map<std::string, ID3D12Resource*> bounce_inputs = {
      {"positions", positions_texture.Get()}, {"sun", sun_texture.Get()}, {"hit_distance", hit_texture.Get()}, {"bounce", bounce_texture.Get()}};
  int failures = 0;
  for (const bool control : {false, true}) {
    const auto with = [&](const wchar_t* signal) {
      std::vector<std::wstring> defines = {signal};
      if (control) defines.emplace_back(L"NRD_FRONT_DISTANCE_Z");
      return defines;
    };
    const Result ambient = run(with(L"NRD_FRONT_AMBIENT"), ambient_inputs, true, true);
    const Result bounce_result = run(with(L"NRD_FRONT_BOUNCE"), bounce_inputs, false, true);
    CHECK(ambient.ran && bounce_result.ran);
    const uint32_t ambient_wrong = wrong_texels(ambient, false);
    const uint32_t bounce_wrong = wrong_texels(bounce_result, true);
    std::printf("%s: AMBIENT view z wrong at %u of %u texels, BOUNCE view z / hit distance wrong at %u (bottom-left view z %.4f, %.4f)\n",
                control ? "CONTROL (-D NRD_FRONT_DISTANCE_Z)" : "X19", ambient_wrong, WIDTH * HEIGHT, bounce_wrong, ambient.view_z[corner],
                bounce_result.view_z[corner]);
    if (control) {
      if (ambient_wrong == 0 || bounce_wrong == 0) {
        std::printf("FAIL: the control passed (the checks cannot see the camera distance)\n");
        ++failures;
      }
    } else if (ambient_wrong != 0 || bounce_wrong != 0) {
      std::printf("FAIL: view z is not the view's z\n");
      ++failures;
    }
  }
  for (const wchar_t* signal : {L"NRD_FRONT_SUN", L"NRD_FRONT_REFLECT"}) {
    if (!run({signal}, {}, false, false).ran) {
      std::printf("FAIL: %ls does not compile\n", signal);
      ++failures;
    }
  }
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return (failures == 0) ? 0 : 1;
}
