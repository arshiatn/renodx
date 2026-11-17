// The helper's rays on surfaces nothing stands over and on the sky (reference suite step 0, review I5 and I4; DEBUG44 (d)),
// on WARP: its own shaders (dlaa_helper/rt_path.cs_6_5.hlsl and rt_fog.cs_6_5.hlsl compiled as build.ps1 compiles them)
// over planes in a scene of their own.
// 1. Specular occlusion (ReflectPixel, -D REFLECT, Specular Occlusion without reflections: every roughness) on an open
//    ground plane, per roughness and view (n.v), with both lobes (ReflectAlpha; PATH_SQUARED_LOBE). Today: the share of
//    the lobe's samples above the horizon, P(n.l > 0) under GGX visible normals (expected from Heitz 2018's sampler, as
//    pbrt-v4's TrowbridgeReitzDistribution::Sample_wm, on a 2048 x 2048 grid, and checked by a quadrature over the GGX
//    distribution itself). Exact: 1.
// 2. Under a roof (a plane 1 m above the ground): Specular Occlusion 0 where it is traced (roughness > 0.75; CONTROL: the
//    rays meet what stands over them), and the reflections' share of samples that met a surface (roughness <= 0.75),
//    today P(n.l > 0), exact 1 (the game's sky fills the rest under the roof).
// 3. Path-Traced Ambient's sky (PathTraceTexel, Path-Traced Ambient with the kept cube) on open surfaces of 30 normals: the
//    bounce over OpenSky(n), under the harness's cube (quality_mode.hpp), a uniform cube (CONTROL: 1 either way),
//    HARNESS_SKY's coloured cube and one brighter on +x (a sun side). Today: E_cos[cube] / Q16[cube], the cosine mean over
//    SkyCalibration's 16 fixed directions; E_cos in closed form ((1 + n_a^2) / 8 +- n_a / 4 of each face, checked by a
//    stratified integral). Exact: 1.
// 4. The fog's sky texels (dlaa_helper/rt_fog.cs_6_5.hlsl with FOG_LIGHTS): the same view all sky, then all a wall 1000 m
//    away (past FOG_RANGE), a roof shading part of each march from the sun and two lights. Today: a sky texel sun 1, no
//    glow. Exact: the wall's values (the same march), within the storage's rounding.
// Each check: |measured - expected| within 4 standard errors (texel-frames independent) + the storage's rounding.
// tests.exe <repository> <folder of dxcompiler.dll and dxil.dll> [now | exact]
// now (default): today's values must hold, and the exact ones must fail somewhere in each part (CONTROL: the bias is
// seen). exact: the exact values must hold (a fixed helper), and today's must fail somewhere (CONTROL).
#include <windows.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr uint32_t BLOCK = 32;        // a patch's pixels a side (its outer ring left out of the sums)
constexpr float SPACING = 0.001f;     // metres between a patch's pixels
constexpr uint32_t SO_SAMPLES = 8;    // path_samples of the specular occlusion's runs
constexpr uint32_t SO_FRAMES = 16;
constexpr uint32_t SKY_SAMPLES = 8;   // path_samples of the sky's runs
constexpr uint32_t SKY_FRAMES = 8;
constexpr float CAMERA_HEIGHT = 10.f;  // over the ground (part 1 and 2)

// The helper's GGX width of a roughness (rt_path.hlsli ReflectAlpha), for both lobes.
double ReflectAlpha(double roughness, bool squared) {
  if (squared) return std::max(roughness * roughness, 0.0064);
  const double gloss = 1.0 - roughness;
  return 0.0064 / std::max(gloss * gloss, 0.0625);
}

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

Vec3 Normalize(const Vec3& v) {
  const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return {v.x / length, v.y / length, v.z / length};
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double Dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

// A cube's six faces (+x, -x, +y, -y, +z, -z) of one channel by a direction (rt_path.hlsli AmbientCube).
double CubeAt(const std::array<double, 6>& faces, const Vec3& d) {
  return d.x * d.x * faces[(d.x < 0.0) ? 1 : 0] + d.y * d.y * faces[(d.y < 0.0) ? 3 : 2] + d.z * d.z * faces[(d.z < 0.0) ? 5 : 4];
}

struct Stats {
  double sum = 0.0;
  double squares = 0.0;
  double count = 0.0;
  double Mean() const { return sum / count; }
  // The mean's standard error over independent texel-frames.
  double Error() const { return std::sqrt(std::max(squares / count - Mean() * Mean(), 0.0) / std::max(count - 1.0, 1.0)); }
  void Add(double value) {
    sum += value;
    squares += value * value;
    count += 1.0;
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: tests.exe <repository> <dxc folder> [now | exact]\n");
    return 2;
  }
  const std::string repository = argv[1];
  const std::string dxc_folder = argv[2];
  const bool expect_exact = argc > 3 && std::string(argv[3]) == "exact";
  int failures = 0;
#define CHECK(x)                                                \
  do {                                                          \
    if (!(x)) {                                                 \
      std::printf("FAIL line %d: %s\n", __LINE__, #x);          \
      return 1;                                                 \
    }                                                           \
  } while (false)

  // The compiler: dxil.dll first, so dxcompiler.dll signs with the copy beside it.
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

  // WARP with inline ray tracing.
  ComPtr<IDXGIFactory4> factory;
  CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
  ComPtr<IDXGIAdapter> warp;
  CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
  ComPtr<ID3D12Device5> device;
  CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device))));
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  CHECK(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))));
  CHECK(options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1);
  ComPtr<ID3D12CommandQueue> queue;
  const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
  ComPtr<ID3D12CommandAllocator> allocator;
  CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
  ComPtr<ID3D12GraphicsCommandList4> list;
  CHECK(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
  ComPtr<ID3D12Fence> fence;
  CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
  uint64_t fence_value = 0;
  // The list recorded so far run to its end, then a new one open.
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

  const auto buffer = [&](uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
    const D3D12_HEAP_PROPERTIES properties = {.Type = heap};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                      .Width = std::max<uint64_t>(size, 256),
                                      .Height = 1,
                                      .DepthOrArraySize = 1,
                                      .MipLevels = 1,
                                      .SampleDesc = {.Count = 1},
                                      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                      .Flags = flags};
    ComPtr<ID3D12Resource> resource;
    device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));
    return resource;
  };
  const auto upload = [&](const void* data, size_t size) {
    ComPtr<ID3D12Resource> resource = buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    void* mapped = nullptr;
    resource->Map(0, nullptr, &mapped);
    std::memcpy(mapped, data, size);
    resource->Unmap(0, nullptr);
    return resource;
  };
  // Textures share every state by promotion (simultaneous access): each list uses one write state of a texture at most.
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
  std::vector<ComPtr<ID3D12Resource>> staging;  // upload buffers kept until their copies ran
  const auto fill = [&](ID3D12Resource* target, const void* data, uint32_t texel_size) {
    const D3D12_RESOURCE_DESC desc = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    uint64_t total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    std::vector<uint8_t> rows(total);
    for (uint32_t y = 0; y < desc.Height; ++y) {
      std::memcpy(rows.data() + y * footprint.Footprint.RowPitch, static_cast<const uint8_t*>(data) + y * desc.Width * texel_size, desc.Width * texel_size);
    }
    staging.push_back(upload(rows.data(), rows.size()));
    const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = target, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
    const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = staging.back().Get(), .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = footprint};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  };
  // A texture's texels (texel_size bytes each, rows packed), after the work recorded so far.
  const auto read = [&](ID3D12Resource* source, uint32_t texel_size) {
    const D3D12_RESOURCE_DESC desc = source->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    uint64_t total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    ComPtr<ID3D12Resource> readback = buffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = readback.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = footprint};
    const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = source, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    std::vector<uint8_t> texels(static_cast<size_t>(desc.Width) * desc.Height * texel_size);
    if (!execute()) return texels;
    void* mapped = nullptr;
    readback->Map(0, nullptr, &mapped);
    for (uint32_t y = 0; y < desc.Height; ++y) {
      std::memcpy(texels.data() + y * desc.Width * texel_size, static_cast<const uint8_t*>(mapped) + y * footprint.Footprint.RowPitch,
                  desc.Width * texel_size);
    }
    readback->Unmap(0, nullptr);
    return texels;
  };

  // A scene of opaque triangles (solids' instance mask 1, as rt_d3d12.hpp's): its BLAS and TLAS.
  struct Scene {
    ComPtr<ID3D12Resource> vertices;
    ComPtr<ID3D12Resource> blas;
    ComPtr<ID3D12Resource> instances;
    ComPtr<ID3D12Resource> tlas;
    ComPtr<ID3D12Resource> scratch;
  };
  const auto build_scene = [&](const std::vector<std::array<float, 3>>& triangle_vertices) {
    Scene scene;
    scene.vertices = upload(triangle_vertices.data(), triangle_vertices.size() * 12);
    const D3D12_RAYTRACING_GEOMETRY_DESC geometry = {
        .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
        .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
        .Triangles = {.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
                      .VertexCount = static_cast<UINT>(triangle_vertices.size()),
                      .VertexBuffer = {.StartAddress = scene.vertices->GetGPUVirtualAddress(), .StrideInBytes = 12}}};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
                                                                   .NumDescs = 1,
                                                                   .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                                                                   .pGeometryDescs = &geometry};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_sizes = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&bottom, &bottom_sizes);
    scene.blas = buffer(bottom_sizes.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RAYTRACING_INSTANCE_DESC instance = {.InstanceID = 0, .InstanceMask = 0x01, .AccelerationStructure = scene.blas->GetGPUVirtualAddress()};
    instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1.f;
    scene.instances = upload(&instance, sizeof(instance));
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
                                                                .NumDescs = 1,
                                                                .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                                                                .InstanceDescs = scene.instances->GetGPUVirtualAddress()};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_sizes = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&top, &top_sizes);
    scene.tlas = buffer(top_sizes.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    scene.scratch = buffer(std::max(bottom_sizes.ScratchDataSizeInBytes, top_sizes.ScratchDataSizeInBytes), D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottom_build = {.DestAccelerationStructureData = scene.blas->GetGPUVirtualAddress(),
                                                                             .Inputs = bottom,
                                                                             .ScratchAccelerationStructureData = scene.scratch->GetGPUVirtualAddress()};
    list->BuildRaytracingAccelerationStructure(&bottom_build, 0, nullptr);
    const D3D12_RESOURCE_BARRIER built = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV, .UAV = {.pResource = scene.blas.Get()}};
    list->ResourceBarrier(1, &built);
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_build = {.DestAccelerationStructureData = scene.tlas->GetGPUVirtualAddress(),
                                                                          .Inputs = top,
                                                                          .ScratchAccelerationStructureData = scene.scratch->GetGPUVirtualAddress()};
    list->BuildRaytracingAccelerationStructure(&top_build, 0, nullptr);
    execute();
    return scene;
  };
  // Two triangles of the quad around `centre` spanned by +-`u` and +-`v`.
  const auto quad = [](std::vector<std::array<float, 3>>* triangle_vertices, const Vec3& centre, const Vec3& u, const Vec3& v) {
    const auto corner = [&](double a, double b) {
      return std::array<float, 3>{static_cast<float>(centre.x + a * u.x + b * v.x), static_cast<float>(centre.y + a * u.y + b * v.y),
                                  static_cast<float>(centre.z + a * u.z + b * v.z)};
    };
    triangle_vertices->insert(triangle_vertices->end(), {corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, -1), corner(1, 1), corner(-1, 1)});
  };

  // A pass of the helper's: its shader `file` (dlaa_helper's) with `defines`, its bindings from its reflection; the named
  // ones bound to the given resources, the others null.
  struct Pass {
    ComPtr<ID3D12ShaderReflection> reflection;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> heap;
    std::map<std::string, ComPtr<ID3D12Resource>> constants;  // by cbuffer name, persistently mapped upload buffers
    std::map<std::string, uint8_t*> mapped;
  };
  const auto make_pass = [&](const wchar_t* file, const std::vector<std::wstring>& defines, Pass* pass) {
    const std::wstring path = std::wstring(repository.begin(), repository.end()) + L"\\src\\games\\twattila\\dlaa_helper\\" + file;
    ComPtr<IDxcBlobEncoding> source;
    if (FAILED(utils->LoadFile(path.c_str(), nullptr, &source))) return false;
    std::vector<LPCWSTR> arguments = {path.c_str(), L"-T", L"cs_6_5", L"-E", L"main", L"-O3"};
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
      return false;
    }
    ComPtr<IDxcBlob> object;
    ComPtr<IDxcBlob> reflection_blob;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
    result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflection_blob), nullptr);
    const DxcBuffer reflection_buffer = {.Ptr = reflection_blob->GetBufferPointer(), .Size = reflection_blob->GetBufferSize()};
    if (FAILED(utils->CreateReflection(&reflection_buffer, IID_PPV_ARGS(&pass->reflection)))) return false;
    D3D12_SHADER_DESC shader_desc = {};
    pass->reflection->GetDesc(&shader_desc);
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    UINT descriptors = 0;
    for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
      D3D12_SHADER_INPUT_BIND_DESC bind = {};
      pass->reflection->GetResourceBindingDesc(index, &bind);
      if (bind.Type == D3D_SIT_SAMPLER) {
        samplers.push_back({.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                            .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                            .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                            .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                            .MaxLOD = D3D12_FLOAT32_MAX,
                            .ShaderRegister = bind.BindPoint,
                            .RegisterSpace = bind.Space});
        continue;
      }
      D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      if (bind.Type == D3D_SIT_CBUFFER) type = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
      if (bind.Type == D3D_SIT_UAV_RWTYPED || bind.Type == D3D_SIT_UAV_RWSTRUCTURED || bind.Type == D3D_SIT_UAV_RWBYTEADDRESS) {
        type = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      }
      ranges.push_back({.RangeType = type, .NumDescriptors = std::max(bind.BindCount, 1u), .BaseShaderRegister = bind.BindPoint,
                        .RegisterSpace = bind.Space, .OffsetInDescriptorsFromTableStart = descriptors});
      descriptors += std::max(bind.BindCount, 1u);
    }
    const D3D12_ROOT_PARAMETER table = {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                                        .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}};
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = 1, .pParameters = &table, .NumStaticSamplers = static_cast<UINT>(samplers.size()),
                                                 .pStaticSamplers = samplers.data()};
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> root_errors;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &root_errors))) return false;
    if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&pass->root)))) return false;
    const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {.pRootSignature = pass->root.Get(),
                                                             .CS = {.pShaderBytecode = object->GetBufferPointer(), .BytecodeLength = object->GetBufferSize()}};
    if (FAILED(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pass->pipeline)))) return false;
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = std::max(descriptors, 1u),
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    return SUCCEEDED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&pass->heap)));
  };
  // The views: `bound` by name (a texture, or for an acceleration structure its TLAS), the rest null of their kind; the
  // cbuffers as zeros (set with set_constant).
  const auto bind_pass = [&](Pass* pass, const std::map<std::string, ID3D12Resource*>& bound) {
    D3D12_SHADER_DESC shader_desc = {};
    pass->reflection->GetDesc(&shader_desc);
    const UINT step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = pass->heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
      D3D12_SHADER_INPUT_BIND_DESC bind = {};
      pass->reflection->GetResourceBindingDesc(index, &bind);
      if (bind.Type == D3D_SIT_SAMPLER) continue;
      const auto found = bound.find(bind.Name);
      ID3D12Resource* resource = (found != bound.end()) ? found->second : nullptr;
      const UINT components = ((bind.uFlags & D3D_SIF_TEXTURE_COMPONENTS) >> 2) + 1;
      DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      if (bind.ReturnType == D3D_RETURN_TYPE_UINT) {
        format = (components == 1) ? DXGI_FORMAT_R32_UINT : ((components == 2) ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_UINT);
      } else if (bind.ReturnType == D3D_RETURN_TYPE_UNORM) {
        format = DXGI_FORMAT_R8_UNORM;
      } else if (components == 1) {
        format = DXGI_FORMAT_R32_FLOAT;
      } else if (components == 2) {
        format = DXGI_FORMAT_R32G32_FLOAT;
      }
      if (resource != nullptr && resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) format = resource->GetDesc().Format;
      for (UINT element = 0; element < std::max(bind.BindCount, 1u); ++element) {
        if (bind.Type == D3D_SIT_CBUFFER) {
          ID3D12ShaderReflectionConstantBuffer* cbuffer = pass->reflection->GetConstantBufferByName(bind.Name);
          D3D12_SHADER_BUFFER_DESC cbuffer_desc = {};
          cbuffer->GetDesc(&cbuffer_desc);
          const UINT size = (cbuffer_desc.Size + 255u) & ~255u;
          ComPtr<ID3D12Resource>& constants = pass->constants[bind.Name];
          constants = buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
          constants->Map(0, nullptr, reinterpret_cast<void**>(&pass->mapped[bind.Name]));
          std::memset(pass->mapped[bind.Name], 0, size);
          const D3D12_CONSTANT_BUFFER_VIEW_DESC view = {.BufferLocation = constants->GetGPUVirtualAddress(), .SizeInBytes = size};
          device->CreateConstantBufferView(&view, handle);
        } else if (bind.Type == D3D_SIT_RTACCELERATIONSTRUCTURE) {
          const D3D12_SHADER_RESOURCE_VIEW_DESC view = {.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE,
                                                        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                        .RaytracingAccelerationStructure = {.Location = bound.at("scene")->GetGPUVirtualAddress()}};
          device->CreateShaderResourceView(nullptr, &view, handle);
        } else if (bind.Type == D3D_SIT_UAV_RWTYPED) {
          D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
          if (bind.Dimension == D3D_SRV_DIMENSION_BUFFER) {
            view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            view.Buffer.NumElements = 1;
          }
          device->CreateUnorderedAccessView(resource, nullptr, &view, handle);
        } else {
          D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                  .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Texture2D = {.MipLevels = 1}};
          if (bind.Dimension == D3D_SRV_DIMENSION_BUFFER) {
            view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            view.Buffer = {.NumElements = 1};
          } else if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY) {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray = {.MipLevels = 1, .ArraySize = 1};
          }
          device->CreateShaderResourceView(resource, &view, handle);
        }
        handle.ptr += step;
      }
    }
  };
  // A cbuffer variable by name (its bytes); a name the shader does not have ends the test.
  const auto set_constant = [](Pass* pass, const char* cbuffer, const char* name, const void* value, size_t size) {
    D3D12_SHADER_VARIABLE_DESC variable = {};
    if (FAILED(pass->reflection->GetConstantBufferByName(cbuffer)->GetVariableByName(name)->GetDesc(&variable)) || pass->mapped[cbuffer] == nullptr) {
      std::printf("FAIL: no %s.%s in the pass\n", cbuffer, name);
      std::exit(1);
    }
    std::memcpy(pass->mapped[cbuffer] + variable.StartOffset, value, std::min<size_t>(size, variable.Size));
  };
  const auto set_uint = [&](Pass* pass, const char* cbuffer, const char* name, uint32_t value) { set_constant(pass, cbuffer, name, &value, 4); };
  const auto set_float = [&](Pass* pass, const char* cbuffer, const char* name, float value) { set_constant(pass, cbuffer, name, &value, 4); };
  const auto dispatch = [&](Pass* pass, uint32_t width, uint32_t height) {
    ID3D12DescriptorHeap* heaps[] = {pass->heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(pass->root.Get());
    list->SetPipelineState(pass->pipeline.Get());
    list->SetComputeRootDescriptorTable(0, pass->heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    return execute();
  };
  const auto half_at = [](const std::vector<uint8_t>& texels, size_t index) {
    uint16_t value;
    std::memcpy(&value, texels.data() + index * 2, 2);
    return static_cast<double>(DirectX::PackedVector::XMConvertHalfToFloat(value));
  };
  // A check's line: measured against today's and the exact value; `expected_now` / `exact` judged within `tolerance`.
  struct Verdict {
    bool now_holds = true;
    bool exact_holds = true;
  };
  const auto judge = [&](Verdict* verdict, const char* line, double measured, double now, double exact, double tolerance) {
    const bool now_ok = std::abs(measured - now) <= tolerance;
    const bool exact_ok = std::abs(measured - exact) <= tolerance;
    verdict->now_holds = verdict->now_holds && now_ok;
    verdict->exact_holds = verdict->exact_holds && exact_ok;
    std::printf("%s measured %.4f today %.4f exact %.4f (+-%.4f)%s%s\n", line, measured, now, exact, tolerance, now_ok ? "" : " [not today's]",
                exact_ok ? "" : " [not exact]");
  };
  // Each part: the expected set must hold everywhere, the other must fail somewhere (CONTROL).
  const auto conclude = [&](const char* part, const Verdict& verdict, bool other_must_fail) {
    const bool holds = expect_exact ? verdict.exact_holds : verdict.now_holds;
    const bool other = expect_exact ? verdict.now_holds : verdict.exact_holds;
    std::printf("%s %s: %s values hold %s; CONTROL the %s values %s\n", holds ? "PASS" : "FAIL", part, expect_exact ? "exact" : "today's",
                holds ? "everywhere" : "NOT everywhere", expect_exact ? "today's" : "exact", other ? "hold everywhere" : "fail somewhere");
    if (!holds || (other_must_fail && other)) ++failures;
  };

  // P(n.l > 0) of a GGX lobe of width `alpha` seen at n.v `view_cosine` under visible-normal sampling: Heitz 2018's
  // sampler (pbrt-v4 Sample_wm's) over a midpoint grid of (u1, u2), which it maps onto the visible normals' density.
  const auto above_horizon = [](double alpha, double view_cosine) {
    constexpr int GRID = 2048;
    const Vec3 view = {std::sqrt(1.0 - view_cosine * view_cosine), 0.0, view_cosine};
    const Vec3 stretched = Normalize({alpha * view.x, alpha * view.y, view.z});
    const double length_squared = stretched.x * stretched.x + stretched.y * stretched.y;
    const Vec3 t1 = (length_squared > 0.0) ? Vec3{-stretched.y / std::sqrt(length_squared), stretched.x / std::sqrt(length_squared), 0.0} : Vec3{1.0, 0.0, 0.0};
    const Vec3 t2 = Cross(stretched, t1);
    const double s = 0.5 * (1.0 + stretched.z);
    int64_t above = 0;
    for (int i = 0; i < GRID; ++i) {
      const double r = std::sqrt((i + 0.5) / GRID);
      for (int j = 0; j < GRID; ++j) {
        const double phi = 2.0 * PI * (j + 0.5) / GRID;
        const double p1 = r * std::cos(phi);
        const double p2 = (1.0 - s) * std::sqrt(1.0 - p1 * p1) + s * r * std::sin(phi);
        const double pz = std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2));
        const Vec3 h = Normalize({alpha * (p1 * t1.x + p2 * t2.x + pz * stretched.x), alpha * (p1 * t1.y + p2 * t2.y + pz * stretched.y),
                                  std::max(0.0, p1 * t1.z + p2 * t2.z + pz * stretched.z)});
        above += 2.0 * Dot(view, h) * h.z - view.z > 0.0;
      }
    }
    return static_cast<double>(above) / (double{GRID} * GRID);
  };
  // The same over the GGX distribution itself: tan^2(theta_h) = alpha^2 xi / (1 - xi) makes D(h) cos(theta_h) uniform in
  // (xi, phi); the visible normals' density over it G1(v) max(v.h, 0) / (v.z h.z); xi = 1 - (1 - t)^2 takes out its
  // 1 / sqrt(1 - xi) end. Returns the probability and the density's integral (1).
  const auto above_horizon_quadrature = [](double alpha, double view_cosine) {
    constexpr int GRID = 2048;
    const Vec3 view = {std::sqrt(1.0 - view_cosine * view_cosine), 0.0, view_cosine};
    const double g1 = 2.0 * view.z / (view.z + std::sqrt(alpha * alpha + (1.0 - alpha * alpha) * view.z * view.z));
    double above = 0.0;
    double total = 0.0;
    for (int i = 0; i < GRID; ++i) {
      const double t = (i + 0.5) / GRID;
      const double xi = 1.0 - (1.0 - t) * (1.0 - t);
      const double tan_theta = alpha * std::sqrt(xi / (1.0 - xi));
      const double cos_theta = 1.0 / std::sqrt(1.0 + tan_theta * tan_theta);
      const double sin_theta = tan_theta * cos_theta;
      for (int j = 0; j < GRID; ++j) {
        const double phi = 2.0 * PI * (j + 0.5) / GRID;
        const Vec3 h = {sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
        const double weight = g1 * std::max(Dot(view, h), 0.0) / (view.z * h.z) * 2.0 * (1.0 - t);
        total += weight;
        if (2.0 * Dot(view, h) * h.z - view.z > 0.0) above += weight;
      }
    }
    return std::array<double, 2>{above / (double{GRID} * GRID), total / (double{GRID} * GRID)};
  };

  // Parts 1 and 2: ReflectPixel. Columns: roughness; rows: n.v. A patch a cell, on the ground (y 0) around the camera's
  // foot at the distance that gives its n.v, each at its own azimuth.
  const std::array<double, 6> roughnesses = {0.0, 0.25, 0.5, 0.75, 0.85, 1.0};
  const std::array<double, 7> view_cosines = {0.999, 0.7, 0.5, 0.3, 0.2, 0.1, 0.05};
  {
    const uint32_t width = BLOCK * static_cast<uint32_t>(roughnesses.size());
    const uint32_t height = BLOCK * static_cast<uint32_t>(view_cosines.size());
    std::vector<std::array<float, 4>> positions(size_t{width} * height);
    std::vector<std::array<float, 4>> normals(positions.size(), {0.f, 1.f, 0.f, 0.f});
    std::vector<float> roughness(positions.size());
    const Vec3 camera = {0.0, CAMERA_HEIGHT, 0.0};
    for (uint32_t row = 0; row < view_cosines.size(); ++row) {
      const double reach = CAMERA_HEIGHT * std::sqrt(1.0 / (view_cosines[row] * view_cosines[row]) - 1.0);
      for (uint32_t column = 0; column < roughnesses.size(); ++column) {
        const double azimuth = (column + 0.5) * 2.0 * PI / roughnesses.size() + row * 0.1;
        const Vec3 radial = {std::cos(azimuth), 0.0, std::sin(azimuth)};
        const Vec3 tangent = {-std::sin(azimuth), 0.0, std::cos(azimuth)};
        for (uint32_t y = 0; y < BLOCK; ++y) {
          for (uint32_t x = 0; x < BLOCK; ++x) {
            const double a = (x - (BLOCK - 1) * 0.5) * SPACING;
            const double b = (y - (BLOCK - 1) * 0.5) * SPACING;
            const Vec3 at = {radial.x * reach + tangent.x * a + radial.x * b, 0.0, radial.z * reach + tangent.z * a + radial.z * b};
            const size_t index = size_t{row * BLOCK + y} * width + column * BLOCK + x;
            const Vec3 to_camera = {camera.x - at.x, camera.y - at.y, camera.z - at.z};
            positions[index] = {static_cast<float>(at.x), 0.f, static_cast<float>(at.z), static_cast<float>(std::sqrt(Dot(to_camera, to_camera)))};
            roughness[index] = static_cast<float>(roughnesses[column]);
          }
        }
      }
    }
    std::vector<std::array<float, 4>> sun(16, {0.f, 0.f, 0.f, 0.f});
    sun[1] = {static_cast<float>(camera.x), static_cast<float>(camera.y), static_cast<float>(camera.z), 1.f};
    ComPtr<ID3D12Resource> positions_texture = texture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> normals_texture = texture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> roughness_texture = texture(width, height, DXGI_FORMAT_R32_FLOAT);
    ComPtr<ID3D12Resource> sun_texture = texture(16, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> meta_texture = texture(width, height, DXGI_FORMAT_R16G16_FLOAT);             // the helper's format
    ComPtr<ID3D12Resource> accumulated_texture = texture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    fill(positions_texture.Get(), positions.data(), 16);
    fill(normals_texture.Get(), normals.data(), 16);
    fill(roughness_texture.Get(), roughness.data(), 4);
    fill(sun_texture.Get(), sun.data(), 16);
    CHECK(execute());
    // The open ground, and with a roof 1 m over it.
    std::vector<std::array<float, 3>> ground;
    quad(&ground, {0.0, 0.0, 0.0}, {5000.0, 0.0, 0.0}, {0.0, 0.0, 5000.0});
    std::vector<std::array<float, 3>> roofed = ground;
    quad(&roofed, {0.0, 1.0, 0.0}, {5000.0, 0.0, 0.0}, {0.0, 0.0, 5000.0});
    const Scene open_scene = build_scene(ground);
    const Scene roofed_scene = build_scene(roofed);

    // The expectations per lobe, roughness and view: P(n.l > 0), its two ways within 5e-4 of each other.
    std::array<std::array<std::array<double, 7>, 6>, 2> above = {};
    for (int squared = 0; squared < 2; ++squared) {
      for (size_t column = 0; column < roughnesses.size(); ++column) {
        for (size_t row = 0; row < view_cosines.size(); ++row) {
          if (roughnesses[column] < 0.05) {
            above[squared][column][row] = 1.0;  // a mirror's one ray: n.l = n.v
            continue;
          }
          const double alpha = ReflectAlpha(roughnesses[column], squared != 0);
          const double sampled = above_horizon(alpha, view_cosines[row]);
          const auto [integrated, density] = above_horizon_quadrature(alpha, view_cosines[row]);
          CHECK(std::abs(density - 1.0) < 1e-3 && std::abs(sampled - integrated) < 5e-4);
          above[squared][column][row] = sampled;
        }
      }
    }
    std::printf("expectations: Heitz 2018's sampler and the GGX quadrature agree within 5e-4 at every cell; densities 1 within 1e-3\n");

    Pass reflect;
    CHECK(make_pass(L"rt_path.cs_6_5.hlsl", {L"REFLECT"}, &reflect));
    constexpr uint32_t PATH_REFLECTIONS = 2u;
    constexpr uint32_t PATH_SPECULAR_OCCLUSION = 4u;
    constexpr uint32_t PATH_GUIDES = 64u;
    constexpr uint32_t PATH_SQUARED_LOBE = 536870912u;
    Verdict open_verdict;
    Verdict roof_verdict;
    for (int run = 0; run < 4; ++run) {
      const bool roof = run >= 2;
      const bool squared = (run & 1) != 0;
      bind_pass(&reflect, {{"scene", (roof ? roofed_scene : open_scene).tlas.Get()},
                           {"positions", positions_texture.Get()},
                           {"sun", sun_texture.Get()},
                           {"guide_normals", normals_texture.Get()},
                           {"guide_roughness", roughness_texture.Get()},
                           {"specular_meta", meta_texture.Get()},
                           {"specular_accumulated", accumulated_texture.Get()}});
      set_uint(&reflect, "Settings", "width", width);
      set_uint(&reflect, "Settings", "height", height);
      set_uint(&reflect, "Paths", "path_flags", PATH_GUIDES | PATH_SPECULAR_OCCLUSION | (roof ? PATH_REFLECTIONS : 0u) | (squared ? PATH_SQUARED_LOBE : 0u));
      set_uint(&reflect, "Paths", "path_samples", SO_SAMPLES);
      set_uint(&reflect, "Paths", "path_bounces", 0);
      set_float(&reflect, "Paths", "bounce_range", 10.f);
      // The harness's 10 m open; under the roof every ray up meets it.
      set_float(&reflect, "Paths", "reflection_range", roof ? 1.0e4f : 10.f);
      std::array<std::array<Stats, 7>, 6> cells = {};
      for (uint32_t frame = 1; frame <= SO_FRAMES; ++frame) {
        set_uint(&reflect, "Settings", "frame", frame);
        CHECK(dispatch(&reflect, width, height));
        const std::vector<uint8_t> meta = read(meta_texture.Get(), 4);
        const std::vector<uint8_t> accumulated = read(accumulated_texture.Get(), 8);
        for (uint32_t y = 0; y < height; ++y) {
          for (uint32_t x = 0; x < width; ++x) {
            if (x % BLOCK == 0 || x % BLOCK == BLOCK - 1 || y % BLOCK == 0 || y % BLOCK == BLOCK - 1) continue;
            const size_t index = size_t{y} * width + x;
            const size_t column = x / BLOCK;
            // Traced (roughness <= 0.75 with reflections): the share that met a surface; else the open share.
            const bool traced = roof && roughnesses[column] <= 0.75;
            cells[column][y / BLOCK].Add(traced ? half_at(accumulated, index * 4 + 3) : half_at(meta, index * 2));
          }
        }
      }
      for (size_t column = 0; column < roughnesses.size(); ++column) {
        for (size_t row = 0; row < view_cosines.size(); ++row) {
          const bool traced = roof && roughnesses[column] <= 0.75;
          const double p = above[squared ? 1 : 0][column][row];
          char line[160];
          std::snprintf(line, sizeof(line), "%s %s lobe r %.2f n.v %.3f alpha %.4f %s:", roof ? "roof" : "open", squared ? "squared" : "default",
                        roughnesses[column], view_cosines[row], (roughnesses[column] < 0.05) ? 0.0 : ReflectAlpha(roughnesses[column], squared),
                        traced ? "share met" : "specular occlusion");
          const double tolerance = 4.0 * cells[column][row].Error() + 1e-3;
          if (!roof) {
            judge(&open_verdict, line, cells[column][row].Mean(), p, 1.0, tolerance);
          } else if (traced) {
            judge(&roof_verdict, line, cells[column][row].Mean(), p, 1.0, tolerance);
          } else {
            // CONTROL: under the roof nothing is open, today and exact.
            Verdict control;
            judge(&control, line, cells[column][row].Mean(), 0.0, 0.0, tolerance);
            if (!control.now_holds) ++failures;
          }
        }
      }
    }
    conclude("1 specular occlusion on the open ground", open_verdict, true);
    conclude("2 the reflections' share under a roof", roof_verdict, true);
  }

  // Part 3: PathTraceTexel's sky on open surfaces. A patch a normal, 100 m from the camera against its normal, on its own
  // quad (2 m), the patches apart beyond the 10 m the paths look.
  {
    std::vector<Vec3> surface_normals = {{1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {0.0, -1.0, 0.0}};
    for (const double tilt : {15.0, 30.0, 45.0, 60.0, 75.0}) {
      for (const double azimuth : {0.0, 30.0, 45.0, 60.0, 90.0}) {
        const double t = tilt * PI / 180.0;
        const double a = azimuth * PI / 180.0;
        surface_normals.push_back({std::sin(t) * std::cos(a), std::cos(t), std::sin(t) * std::sin(a)});
      }
    }
    constexpr uint32_t COLUMNS = 8;
    const uint32_t rows = (static_cast<uint32_t>(surface_normals.size()) + COLUMNS - 1) / COLUMNS;
    const uint32_t width = BLOCK * COLUMNS;
    const uint32_t height = BLOCK * rows;
    std::vector<std::array<float, 4>> positions(size_t{width} * height, {0.f, 0.f, 0.f, 0.f});
    std::vector<std::array<float, 4>> normals(positions.size(), {0.f, 0.f, 0.f, 0.f});
    std::vector<std::array<float, 3>> quads;
    std::vector<Vec3> shown_normals;  // the guide normals as the shader normalizes them (float)
    for (size_t patch = 0; patch < surface_normals.size(); ++patch) {
      const Vec3 n = surface_normals[patch];
      const Vec3 u = Normalize(Cross((std::abs(n.y) < 0.9) ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0}, n));
      const Vec3 v = Cross(n, u);
      const Vec3 centre = {-100.0 * n.x, -100.0 * n.y, -100.0 * n.z};
      quad(&quads, centre, {u.x, u.y, u.z}, {v.x, v.y, v.z});
      const std::array<float, 4> normal = {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z), 0.f};
      const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
      shown_normals.push_back({normal[0] / length, normal[1] / length, normal[2] / length});
      for (uint32_t y = 0; y < BLOCK; ++y) {
        for (uint32_t x = 0; x < BLOCK; ++x) {
          const double s = (x - (BLOCK - 1) * 0.5) * SPACING;
          const double r = (y - (BLOCK - 1) * 0.5) * SPACING;
          const Vec3 at = {centre.x + u.x * s + v.x * r, centre.y + u.y * s + v.y * r, centre.z + u.z * s + v.z * r};
          const size_t index = size_t{(patch / COLUMNS) * BLOCK + y} * width + (patch % COLUMNS) * BLOCK + x;
          positions[index] = {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z), static_cast<float>(std::sqrt(Dot(at, at)))};
          normals[index] = normal;
        }
      }
    }
    ComPtr<ID3D12Resource> positions_texture = texture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> normals_texture = texture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> sun_texture = texture(16, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> bounce_texture = texture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);  // the helper's format
    fill(positions_texture.Get(), positions.data(), 16);
    fill(normals_texture.Get(), normals.data(), 16);
    CHECK(execute());
    const Scene scene = build_scene(quads);
    Pass paths;
    CHECK(make_pass(L"rt_path.cs_6_5.hlsl", {}, &paths));
    bind_pass(&paths, {{"scene", scene.tlas.Get()},
                       {"positions", positions_texture.Get()},
                       {"sun", sun_texture.Get()},
                       {"guide_normals", normals_texture.Get()},
                       {"bounce", bounce_texture.Get()}});
    constexpr uint32_t PATH_GUIDES = 64u;
    constexpr uint32_t PATH_AMBIENT = 262144u;
    constexpr uint32_t PATH_KEEP_CUBE = 1073741824u;
    set_uint(&paths, "Settings", "width", width);
    set_uint(&paths, "Settings", "height", height);
    set_uint(&paths, "Paths", "bounce_width", width);
    set_uint(&paths, "Paths", "bounce_height", height);
    set_uint(&paths, "Paths", "bounce_scale", 1);
    set_uint(&paths, "Paths", "path_flags", PATH_GUIDES | PATH_AMBIENT | PATH_KEEP_CUBE);
    set_uint(&paths, "Paths", "path_samples", SKY_SAMPLES);
    set_uint(&paths, "Paths", "path_bounces", 1);
    set_float(&paths, "Paths", "bounce_range", 10.f);
    set_float(&paths, "Paths", "sky_range", 10.f);

    // The cubes (+x, -x, +y, -y, +z, -z) a channel each, and their scale; Attila's y x 4 stretch on (texel 6's w 1).
    struct Cube {
      const char* name;
      std::array<std::array<double, 6>, 3> faces;
      double scale;
      bool control;  // every face the same: no quadrature error
    };
    const std::array<Cube, 4> cubes = {{
        {"harness (quality_mode.hpp)", {{{6, 6, 10, 2, 6, 6}, {6, 6, 10, 2, 6, 6}, {6, 6, 10, 2, 6, 6}}}, 1.44, false},
        {"uniform (CONTROL)", {{{6, 6, 6, 6, 6, 6}, {6, 6, 6, 6, 6, 6}, {6, 6, 6, 6, 6, 6}}}, 1.44, true},
        {"HARNESS_SKY", {{{0.5, 0.5, 0.4, 0.3, 0.5, 0.5}, {0.6, 0.6, 0.6, 0.25, 0.6, 0.6}, {0.8, 0.8, 1.2, 0.2, 0.8, 0.8}}}, 1.0, false},
        {"sun side (+x 8, -x 4)", {{{8, 4, 10, 2, 6, 6}, {8, 4, 10, 2, 6, 6}, {8, 4, 10, 2, 6, 6}}}, 1.44, false},
    }};
    Verdict sky_verdict;
    for (const Cube& cube : cubes) {
      std::vector<std::array<float, 4>> sun(16, {0.f, 0.f, 0.f, 0.f});
      sun[1] = {0.f, 0.f, 0.f, 1.f};  // the camera
      for (int face = 0; face < 6; ++face) {
        sun[3 + face] = {static_cast<float>(cube.faces[0][face]), static_cast<float>(cube.faces[1][face]), static_cast<float>(cube.faces[2][face]), 1.f};
      }
      sun[4][3] = static_cast<float>(cube.scale);
      fill(sun_texture.Get(), sun.data(), 16);
      CHECK(execute());
      std::vector<std::array<Stats, 3>> measured(surface_normals.size());
      for (uint32_t frame = 1; frame <= SKY_FRAMES; ++frame) {
        set_uint(&paths, "Settings", "frame", frame);
        CHECK(dispatch(&paths, width, height));
        const std::vector<uint8_t> bounce = read(bounce_texture.Get(), 8);
        for (size_t patch = 0; patch < surface_normals.size(); ++patch) {
          for (uint32_t y = 1; y + 1 < BLOCK; ++y) {
            for (uint32_t x = 1; x + 1 < BLOCK; ++x) {
              const size_t index = size_t{(patch / COLUMNS) * BLOCK + y} * width + (patch % COLUMNS) * BLOCK + x;
              for (int channel = 0; channel < 3; ++channel) measured[patch][channel].Add(half_at(bounce, index * 4 + channel));
            }
          }
        }
      }
      for (size_t patch = 0; patch < surface_normals.size(); ++patch) {
        const Vec3 n = shown_normals[patch];
        // SkyCalibration's 16 directions (rt_scene.hlsli CosineDirection, in float as the shader).
        const std::array<float, 3> nf = {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)};
        const std::array<float, 3> helper = (std::abs(nf[1]) < 0.999f) ? std::array<float, 3>{0.f, 1.f, 0.f} : std::array<float, 3>{1.f, 0.f, 0.f};
        std::array<float, 3> tangent = {helper[1] * nf[2] - helper[2] * nf[1], helper[2] * nf[0] - helper[0] * nf[2], helper[0] * nf[1] - helper[1] * nf[0]};
        const float tangent_length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
        for (float& component : tangent) component /= tangent_length;
        const std::array<float, 3> bitangent = {nf[1] * tangent[2] - nf[2] * tangent[1], nf[2] * tangent[0] - nf[0] * tangent[2], nf[0] * tangent[1] - nf[1] * tangent[0]};
        std::array<double, 3> quadrature = {};
        for (uint32_t ray = 0; ray < 16; ++ray) {
          const float u = (ray + 0.5f) / 16.f;
          const float v = ray * 0.618034f - std::floor(ray * 0.618034f);
          const float radius = std::sqrt(u);
          const float angle = 6.2831853f * v;
          const float up = std::sqrt(std::clamp(1.f - u, 0.f, 1.f));
          const Vec3 direction = Normalize({tangent[0] * radius * std::cos(angle) + bitangent[0] * radius * std::sin(angle) + nf[0] * up,
                                            tangent[1] * radius * std::cos(angle) + bitangent[1] * radius * std::sin(angle) + nf[1] * up,
                                            tangent[2] * radius * std::cos(angle) + bitangent[2] * radius * std::sin(angle) + nf[2] * up});
          for (int channel = 0; channel < 3; ++channel) quadrature[channel] += CubeAt(cube.faces[channel], direction) / 16.0;
        }
        const Vec3 stretched = Normalize({n.x, 4.0 * n.y, n.z});
        for (int channel = 0; channel < 3; ++channel) {
          const auto& faces = cube.faces[channel];
          // The cosine mean over the hemisphere: face a+ by (1 + n_a^2) / 8 + n_a / 4, a- by (1 + n_a^2) / 8 - n_a / 4.
          const auto share = [](double component, double sign) { return (1.0 + component * component) / 8.0 + sign * component / 4.0; };
          const double cosine_mean = faces[0] * share(n.x, 1.0) + faces[1] * share(n.x, -1.0) + faces[2] * share(n.y, 1.0) + faces[3] * share(n.y, -1.0)
                                     + faces[4] * share(n.z, 1.0) + faces[5] * share(n.z, -1.0);
          if (patch < 5 && channel == 0) {
            // The closed form against a stratified cosine integral (512 x 512).
            double integral = 0.0;
            const Vec3 helper_axis = (std::abs(n.y) < 0.9) ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0};
            const Vec3 t = Normalize(Cross(helper_axis, n));
            const Vec3 b = Cross(n, t);
            for (int i = 0; i < 512; ++i) {
              for (int j = 0; j < 512; ++j) {
                const double radius = std::sqrt((i + 0.5) / 512.0);
                const double angle = 2.0 * PI * (j + 0.5) / 512.0;
                const double up = std::sqrt(1.0 - radius * radius);
                integral += CubeAt(faces, {t.x * radius * std::cos(angle) + b.x * radius * std::sin(angle) + n.x * up,
                                           t.y * radius * std::cos(angle) + b.y * radius * std::sin(angle) + n.y * up,
                                           t.z * radius * std::cos(angle) + b.z * radius * std::sin(angle) + n.z * up});
              }
            }
            CHECK(std::abs(integral / (512.0 * 512.0) - cosine_mean) < 1e-4 * cosine_mean);
          }
          const double open_sky = CubeAt(faces, stretched) * cube.scale;
          const double now = cosine_mean / quadrature[channel];
          const Stats& stats = measured[patch][channel];
          char line[200];
          std::snprintf(line, sizeof(line), "sky %s n (%.3f %.3f %.3f) channel %d: bounce / OpenSky", cube.name, n.x, n.y, n.z, channel);
          const double tolerance = 4.0 * stats.Error() / open_sky + 1e-3;
          if (cube.control) {
            Verdict control;
            judge(&control, line, stats.Mean() / open_sky, now, 1.0, tolerance);
            if (!control.now_holds || !control.exact_holds) ++failures;
          } else {
            judge(&sky_verdict, line, stats.Mean() / open_sky, now, 1.0, tolerance);
          }
        }
      }
    }
    std::printf("sky: E_cos closed form within 1e-4 of the stratified integral at the five axis normals\n");
    conclude("3 Path-Traced Ambient's sky on open surfaces", sky_verdict, true);
  }

  // Part 4: the fog's sky texels (rt_fog.cs_6_5.hlsl: the sun's shafts and, with FOG_LIGHTS, the torches' light along each
  // camera ray, FOG_RANGE 400 m). The same view twice: every pixel the sky, then every pixel a wall 1000 m away (past
  // FOG_RANGE: the same march). A roof 30 m up over the first 300 m shades part of each march from the sun and the two
  // lights. Today: a sky texel sun 1, no glow. Exact: the wall's values (the same rays), within the storage's rounding.
  {
    constexpr uint32_t WIDTH = 64;
    constexpr uint32_t HEIGHT = 32;
    constexpr uint32_t FOG_FRAMES = 4;
    const uint32_t fog_width = (WIDTH + 3) / 4;
    const uint32_t fog_height = (HEIGHT + 3) / 4;
    // The camera 2 m up, turned 20 degrees left and 10 up, 60 degrees high, jittered 0.3 px; its registers as rows on
    // column vectors (rt_camera.hlsli ClipOf), w the view depth.
    const Vec3 camera = {0.0, 2.0, 0.0};
    const double yaw = -20.0 * PI / 180.0;
    const double pitch = 10.0 * PI / 180.0;
    const Vec3 forward = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const Vec3 right = Normalize(Cross({0.0, 1.0, 0.0}, forward));
    const Vec3 up = Cross(forward, right);
    const double focal = 1.0 / std::tan(30.0 * PI / 180.0);
    const double aspect = double{WIDTH} / HEIGHT;
    const double jitter_x = 0.3 * 2.0 / WIDTH;
    const auto row = [&](const Vec3& axis, double scale) {
      return std::array<float, 4>{static_cast<float>(axis.x * scale), static_cast<float>(axis.y * scale), static_cast<float>(axis.z * scale),
                                  static_cast<float>(-Dot(axis, camera) * scale)};
    };
    std::array<std::array<float, 4>, 4> view_projection = {row(right, focal / aspect), row(up, focal), row(forward, 0.5), row(forward, 1.0)};
    for (int component = 0; component < 4; ++component) view_projection[0][component] += static_cast<float>(jitter_x) * view_projection[3][component];
    // The wall's positions: each pixel's ray (through its centre, as rt_positions unprojects) 1000 m out.
    std::vector<std::array<float, 4>> wall(size_t{WIDTH} * HEIGHT);
    for (uint32_t y = 0; y < HEIGHT; ++y) {
      for (uint32_t x = 0; x < WIDTH; ++x) {
        const double ndc_x = (x + 0.5) / WIDTH * 2.0 - 1.0;
        const double ndc_y = 1.0 - (y + 0.5) / HEIGHT * 2.0;
        const double view_x = (ndc_x - jitter_x) * aspect / focal;
        const double view_y = ndc_y / focal;
        const Vec3 direction = Normalize({right.x * view_x + up.x * view_y + forward.x, right.y * view_x + up.y * view_y + forward.y,
                                          right.z * view_x + up.z * view_y + forward.z});
        const std::array<float, 3> at = {static_cast<float>(camera.x + direction.x * 1000.0), static_cast<float>(camera.y + direction.y * 1000.0),
                                         static_cast<float>(camera.z + direction.z * 1000.0)};
        const float dx = at[0] - static_cast<float>(camera.x);
        const float dy = at[1] - static_cast<float>(camera.y);
        const float dz = at[2] - static_cast<float>(camera.z);
        wall[size_t{y} * WIDTH + x] = {at[0], at[1], at[2], std::sqrt(dx * dx + dy * dy + dz * dz)};
      }
    }
    const std::vector<std::array<float, 4>> sky(wall.size(), {0.f, 0.f, 0.f, 0.f});
    ComPtr<ID3D12Resource> wall_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> sky_texture = texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> sun_texture = texture(16, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
    ComPtr<ID3D12Resource> fog_texture = texture(fog_width, fog_height, DXGI_FORMAT_R8_UNORM);              // the helper's formats
    ComPtr<ID3D12Resource> fog_lights_texture = texture(fog_width, fog_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    std::vector<std::array<float, 4>> sun(16, {0.f, 0.f, 0.f, 0.f});
    const Vec3 toward_sun = Normalize({0.3, 1.0, 0.2});
    sun[0] = {static_cast<float>(toward_sun.x), static_cast<float>(toward_sun.y), static_cast<float>(toward_sun.z), 1.f};
    sun[1] = {static_cast<float>(camera.x), static_cast<float>(camera.y), static_cast<float>(camera.z), 1.f};
    fill(wall_texture.Get(), wall.data(), 16);
    fill(sky_texture.Get(), sky.data(), 16);
    fill(sun_texture.Get(), sun.data(), 16);
    CHECK(execute());
    std::vector<std::array<float, 3>> roof;
    quad(&roof, {0.0, 30.0, 150.0}, {40.0, 0.0, 0.0}, {0.0, 0.0, 150.0});
    const Scene roof_scene = build_scene(roof);
    Pass fog;
    CHECK(make_pass(L"rt_fog.cs_6_5.hlsl", {}, &fog));
    constexpr uint32_t FOG_LIGHTS = 512u;
    const std::array<std::array<float, 4>, 2> lights = {{{0.f, 25.f, 80.f, 120.f}, {-20.f, 5.f, 200.f, 60.f}}};
    const std::array<std::array<float, 4>, 2> light_colours = {{{8.f, 5.f, 2.f, 0.f}, {3.f, 3.f, 6.f, 0.f}}};
    // One frame of the fog over `positions`: its sun share (0-1) and the glow's red, green, blue a texel.
    const auto fog_frame = [&](ID3D12Resource* positions, uint32_t frame) {
      bind_pass(&fog, {{"scene", roof_scene.tlas.Get()}, {"positions", positions}, {"sun", sun_texture.Get()}, {"fog", fog_texture.Get()},
                       {"fog_lights", fog_lights_texture.Get()}});
      set_uint(&fog, "Settings", "width", WIDTH);
      set_uint(&fog, "Settings", "height", HEIGHT);
      set_uint(&fog, "Settings", "frame", frame);
      set_uint(&fog, "Settings", "flags", FOG_LIGHTS);
      set_constant(&fog, "Lights", "lights", lights.data(), sizeof(lights));
      set_uint(&fog, "Lights", "light_count", static_cast<uint32_t>(lights.size()));
      set_constant(&fog, "Lights", "light_colours", light_colours.data(), sizeof(light_colours));
      // (Today's fog has no camera registers: its sky needs none.)
      if (fog.mapped.contains("Paths")) set_constant(&fog, "Paths", "view_projection", view_projection.data(), sizeof(view_projection));
      std::vector<std::array<double, 4>> values(size_t{fog_width} * fog_height);
      if (!dispatch(&fog, fog_width, fog_height)) return values;
      const std::vector<uint8_t> shares = read(fog_texture.Get(), 1);
      const std::vector<uint8_t> glow = read(fog_lights_texture.Get(), 8);
      for (size_t texel = 0; texel < values.size(); ++texel) {
        values[texel] = {shares[texel] / 255.0, half_at(glow, texel * 4), half_at(glow, texel * 4 + 1), half_at(glow, texel * 4 + 2)};
      }
      return values;
    };
    Verdict fog_verdict;
    double darkest_wall = 1.0;
    double brightest_wall = 0.0;
    double sun_apart = 0.0;    // the sky texels' sun share from the wall's, at most
    double glow_apart = 0.0;   // the glow's, relative to the wall's
    size_t not_today = 0;
    size_t not_exact = 0;
    for (uint32_t frame = 1; frame <= FOG_FRAMES; ++frame) {
      const auto at_wall = fog_frame(wall_texture.Get(), frame);
      const auto at_sky = fog_frame(sky_texture.Get(), frame);
      for (size_t texel = 0; texel < at_wall.size(); ++texel) {
        darkest_wall = std::min(darkest_wall, at_wall[texel][0]);
        brightest_wall = std::max(brightest_wall, at_wall[texel][1]);
        // The share: a step of R8_UNORM; the glow: two of FP16's steps.
        bool today = std::abs(at_sky[texel][0] - 1.0) <= 0.5 / 255.0;
        bool exact = std::abs(at_sky[texel][0] - at_wall[texel][0]) <= 1.5 / 255.0;
        sun_apart = std::max(sun_apart, std::abs(at_sky[texel][0] - at_wall[texel][0]));
        for (int channel = 1; channel < 4; ++channel) {
          today = today && at_sky[texel][channel] == 0.0;
          exact = exact && std::abs(at_sky[texel][channel] - at_wall[texel][channel]) <= 2e-3 * at_wall[texel][channel] + 1e-5;
          glow_apart = std::max(glow_apart, std::abs(at_sky[texel][channel] - at_wall[texel][channel]) / std::max(at_wall[texel][channel], 1e-5));
        }
        not_today += today ? 0 : 1;
        not_exact += exact ? 0 : 1;
      }
    }
    // The scene shades and lights the marches (else the parts' values would agree by nothing there).
    CHECK(darkest_wall < 0.9 && brightest_wall > 0.01);
    fog_verdict.now_holds = not_today == 0;
    fog_verdict.exact_holds = not_exact == 0;
    std::printf("fog: %u x %u texels x %u frames; the wall's sun share down to %.4f, its glow (red) up to %.4f; sky texels against the wall's: "
                "sun share %.4f apart at most, glow %.5f (relative); %zu texel-frames not today's (sun 1, no glow), %zu not the wall's\n",
                fog_width, fog_height, FOG_FRAMES, darkest_wall, brightest_wall, sun_apart, glow_apart, not_today, not_exact);
    conclude("4 the fog's sky texels", fog_verdict, true);
  }

  std::printf("%s\n", (failures == 0) ? "ALL PASSED" : "SOME FAILED");
  return (failures == 0) ? 0 : 1;
}
