// Path-Traced Torch Light's RTXDI passes (PT DI step 4c, gate W4) on WARP: the helper's own rt_di_initial, rt_di_temporal,
// rt_di_spatial and rt_di_shade (dlaa_helper, compiled as build.ps1 compiles them: PATH_DI, PATH_REGIR, RTXDI's -I) over a
// 64 x 64 floor of DI pixels, 8 x 8 tiles each listing its own set of the 40 lights (1 to 32 of them, in the lights'
// order, as rt_lights' LIGHTS_DI build writes the mask's tile words and the members).
// 1. Initial sampling: every pick is a listed light; with every light at one place (the same target for each) the picks
//    are uniform over the tile's list (chi-square over 16 frames, |z| <= 4) and each reservoir's weight W is the list's
//    size (RIS with uniform candidates: W = sum of target x n / M over target = n).
// 2. Spatial reuse (two neighbours within 32 px, eight at a short history): no pixel's output reservoir holds a light its
//    tile does not list (an unlisted light keeps the game's unshadowed light: RTXDI's target is 0 for it). CONTROL: the
//    members off (every light a member, the lists as before) must put unlisted lights in the output.
// 3. The shading: every pixel's two DI lights finite and >= 0, the slm one > 0 at every pixel (all lights visible), the
//    skinlm one 0 (no skin pixel).
// 0. rt_di_temporal's DXIL traces the temporal bias ray (1 TraceRayInline: the pixel's ray origin is rt_di_initial's,
//    di_origin, so no StartPast of its own; 0 when RTXDI_ALLOWED_BIAS_CORRECTION is left undefined and the pass compiles
//    1/M alone).
// 4. The temporal pass (DI_HISTORY, spatial off) over a last trace whose surface no light reaches (100 m below; the light
//    places the same; its reservoirs empty with M 20, as after a disocclusion's fallback): ray traced and basic bias
//    correction keep the one-frame light (within 1 %); CONTROL: OFF (1/M) divides it by 21. Today's 1/M-only build fails.
// 5. Light colours with a negative channel (the addon sends the game's unclamped: fa1_ B2, fa9_ U3): every pixel's slm
//    light finite and > 0, its green 0 (DiLight takes the colour at 0 and up, as LightArriving). CONTROL: a build before
//    the fix (DiStore zeroed a pixel with any negative channel) fails every pixel.
// tests.exe <repository> <folder of dxcompiler.dll and dxil.dll> <RTXDI Include folder>
#include <windows.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>

#include <Rtxdi/RtxdiUtils.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <random>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t SIDE = 64;  // the floor's pixels a side
constexpr uint32_t TILE = 8;
constexpr uint32_t TILES = SIDE / TILE;
constexpr uint32_t LIGHTS = 40;
constexpr uint32_t FRAMES = 16;
constexpr uint32_t OFFSETS = 1024;
constexpr uint32_t MASK_ROWS = SIDE + (SIDE / TILE) * 2;  // the pixels, then the tiles' lists (dlaa_bridge.h LightTileRows)
constexpr uint32_t RECONSTRUCTED = 256u;
constexpr uint32_t DI_HISTORY = 1u;
constexpr uint32_t DI_BIAS_SHIFT = 1u;
constexpr uint32_t DI_NO_TEMPORAL = 8u;
constexpr uint32_t DI_NO_SPATIAL = 16u;

// A binding by name: its resource, the view's format and, for buffers, its elements and stride.
struct Bound {
  ID3D12Resource* resource = nullptr;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  UINT elements = 1;
  UINT stride = 0;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf("usage: tests.exe <repository> <dxc folder> <RTXDI Include folder>\n");
    return 2;
  }
  const std::string repository = argv[1];
  const std::string dxc_folder = argv[2];
  const std::string rtxdi = argv[3];
  int failures = 0;
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
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = std::max<uint64_t>(size, 256), .Height = 1,
                                      .DepthOrArraySize = 1, .MipLevels = 1, .SampleDesc = {.Count = 1},
                                      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR, .Flags = flags};
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
  const auto texture = [&](uint32_t width, uint32_t height, DXGI_FORMAT format) {
    const D3D12_HEAP_PROPERTIES properties = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = width, .Height = height, .DepthOrArraySize = 1,
                                      .MipLevels = 1, .Format = format, .SampleDesc = {.Count = 1},
                                      .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS};
    ComPtr<ID3D12Resource> resource;
    device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
    return resource;
  };
  std::vector<ComPtr<ID3D12Resource>> staging;
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
  const auto read_texture = [&](ID3D12Resource* source, uint32_t texel_size) {
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
      std::memcpy(texels.data() + y * desc.Width * texel_size, static_cast<const uint8_t*>(mapped) + y * footprint.Footprint.RowPitch, desc.Width * texel_size);
    }
    readback->Unmap(0, nullptr);
    return texels;
  };
  const auto read_buffer = [&](ID3D12Resource* source) {
    const uint64_t size = source->GetDesc().Width;
    ComPtr<ID3D12Resource> readback = buffer(size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    list->CopyBufferRegion(readback.Get(), 0, source, 0, size);
    std::vector<uint8_t> bytes(size);
    if (!execute()) return bytes;
    void* mapped = nullptr;
    readback->Map(0, nullptr, &mapped);
    std::memcpy(bytes.data(), mapped, size);
    readback->Unmap(0, nullptr);
    return bytes;
  };

  // The scene: the floor (y = 0, opaque, the solids' mask).
  const std::array<std::array<float, 3>, 6> floor_vertices = {{{-100.f, 0.f, -100.f}, {100.f, 0.f, -100.f}, {100.f, 0.f, 100.f},
                                                               {-100.f, 0.f, -100.f}, {100.f, 0.f, 100.f}, {-100.f, 0.f, 100.f}}};
  ComPtr<ID3D12Resource> vertices = upload(floor_vertices.data(), sizeof(floor_vertices));
  const D3D12_RAYTRACING_GEOMETRY_DESC geometry = {
      .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES, .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
      .Triangles = {.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT, .VertexCount = 6,
                    .VertexBuffer = {.StartAddress = vertices->GetGPUVirtualAddress(), .StrideInBytes = 12}}};
  const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL, .NumDescs = 1,
                                                                       .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY, .pGeometryDescs = &geometry};
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_sizes = {};
  device->GetRaytracingAccelerationStructurePrebuildInfo(&bottom, &bottom_sizes);
  ComPtr<ID3D12Resource> blas = buffer(bottom_sizes.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  D3D12_RAYTRACING_INSTANCE_DESC instance = {.InstanceID = 0, .InstanceMask = 0x01, .AccelerationStructure = blas->GetGPUVirtualAddress()};
  instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1.f;
  ComPtr<ID3D12Resource> instances = upload(&instance, sizeof(instance));
  const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL, .NumDescs = 1,
                                                                    .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                                                                    .InstanceDescs = instances->GetGPUVirtualAddress()};
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_sizes = {};
  device->GetRaytracingAccelerationStructurePrebuildInfo(&top, &top_sizes);
  ComPtr<ID3D12Resource> tlas = buffer(top_sizes.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ComPtr<ID3D12Resource> scratch = buffer(std::max(bottom_sizes.ScratchDataSizeInBytes, top_sizes.ScratchDataSizeInBytes), D3D12_HEAP_TYPE_DEFAULT,
                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottom_build = {.DestAccelerationStructureData = blas->GetGPUVirtualAddress(), .Inputs = bottom,
                                                                           .ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress()};
  list->BuildRaytracingAccelerationStructure(&bottom_build, 0, nullptr);
  const D3D12_RESOURCE_BARRIER built = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV, .UAV = {.pResource = blas.Get()}};
  list->ResourceBarrier(1, &built);
  const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_build = {.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(), .Inputs = top,
                                                                        .ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress()};
  list->BuildRaytracingAccelerationStructure(&top_build, 0, nullptr);
  CHECK(execute());

  // The floor's pixels: 5 cm apart, the camera 6 m over its middle; the light surface slm everywhere (normal up, gloss
  // 0.5, a grey albedo, reflectance 0.04, white specular).
  const float camera[3] = {1.6f, 6.f, 1.6f};
  std::vector<std::array<float, 4>> positions(SIDE * SIDE);
  std::vector<std::array<uint32_t, 4>> surface(SIDE * SIDE);
  for (uint32_t y = 0; y < SIDE; ++y) {
    for (uint32_t x = 0; x < SIDE; ++x) {
      const float world[3] = {(x + 0.5f) * 0.05f, 0.f, (y + 0.5f) * 0.05f};
      const float depth = std::sqrt((world[0] - camera[0]) * (world[0] - camera[0]) + camera[1] * camera[1] + (world[2] - camera[2]) * (world[2] - camera[2]));
      positions[y * SIDE + x] = {world[0], world[1], world[2], depth};
      surface[y * SIDE + x] = {128u | (255u << 8) | (128u << 16) | (128u << 24), 180u | (180u << 8) | (180u << 16) | (10u << 24), 0x00FFFFFFu, 1u};
    }
  }
  std::vector<std::array<float, 4>> sun(16, {0.f, 0.f, 0.f, 0.f});
  sun[1] = {camera[0], camera[1], camera[2], 1.f};
  sun[2] = {1.f, 1.f, 1.f, 0.004f};  // the game's HDR diffuse scale
  // Each tile's list: 1 to 32 of the lights, drawn without repeats, in the lights' order.
  std::mt19937 random(1117);
  std::vector<std::vector<uint32_t>> tile_lists(TILES * TILES);
  for (auto& tile_list : tile_lists) {
    std::vector<uint32_t> all(LIGHTS);
    for (uint32_t light = 0; light < LIGHTS; ++light) all[light] = light;
    std::shuffle(all.begin(), all.end(), random);
    all.resize(1 + (random() % 32));
    std::sort(all.begin(), all.end());
    tile_list = all;
  }
  // The mask: the pixels' words 0 (DI pixels), each tile's 8 words of bytes below them; the members (128 bits a tile).
  const auto mask_and_members = [&](bool members_off, std::vector<uint32_t>* mask, std::vector<std::array<uint32_t, 4>>* members) {
    mask->assign(size_t{SIDE} * MASK_ROWS, 0u);
    members->assign(TILES * TILES, {0u, 0u, 0u, 0u});
    for (uint32_t ty = 0; ty < TILES; ++ty) {
      for (uint32_t tx = 0; tx < TILES; ++tx) {
        const auto& tile_list = tile_lists[ty * TILES + tx];
        for (uint32_t word = 0; word < 8; ++word) {
          uint32_t bytes = 0xFFFFFFFFu;
          for (uint32_t byte = 0; byte < 4; ++byte) {
            const uint32_t slot = word * 4 + byte;
            if (slot < tile_list.size()) bytes = (bytes & ~(0xFFu << (byte * 8))) | (tile_list[slot] << (byte * 8));
          }
          (*mask)[size_t{SIDE + ty * 2 + (word >> 2)} * SIDE + tx * 4 + (word & 3)] = bytes;
        }
        for (const uint32_t light : tile_list) (*members)[ty * TILES + tx][light >> 5] |= 1u << (light & 31);
        if (members_off) (*members)[ty * TILES + tx] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
      }
    }
  };

  ComPtr<ID3D12Resource> positions_texture = texture(SIDE, SIDE, DXGI_FORMAT_R32G32B32A32_FLOAT);
  ComPtr<ID3D12Resource> last_positions_texture = texture(SIDE, SIDE, DXGI_FORMAT_R32G32B32A32_FLOAT);
  ComPtr<ID3D12Resource> sun_texture = texture(16, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
  ComPtr<ID3D12Resource> surface_texture = texture(SIDE, SIDE, DXGI_FORMAT_R32G32B32A32_UINT);
  ComPtr<ID3D12Resource> previous_surface_texture = texture(SIDE, SIDE, DXGI_FORMAT_R32G32B32A32_UINT);
  ComPtr<ID3D12Resource> mask_texture = texture(SIDE, MASK_ROWS, DXGI_FORMAT_R32_UINT);
  std::array<ComPtr<ID3D12Resource>, 2> history_textures = {texture(SIDE, MASK_ROWS, DXGI_FORMAT_R32_UINT), texture(SIDE, MASK_ROWS, DXGI_FORMAT_R32_UINT)};
  ComPtr<ID3D12Resource> members_texture = texture(TILES, TILES, DXGI_FORMAT_R32G32B32A32_UINT);
  std::array<ComPtr<ID3D12Resource>, 2> light_textures = {texture(SIDE, SIDE, DXGI_FORMAT_R16G16B16A16_FLOAT), texture(SIDE, SIDE, DXGI_FORMAT_R16G16B16A16_FLOAT)};
  // Each DI pixel's ray origin (rt_rtxdi_bridge.hlsli di_origin: rt_di_initial writes this trace's, by parity).
  std::array<ComPtr<ID3D12Resource>, 2> origin_textures = {texture(SIDE, SIDE, DXGI_FORMAT_R16G16B16A16_FLOAT), texture(SIDE, SIDE, DXGI_FORMAT_R16G16B16A16_FLOAT)};
  const RTXDI_ReservoirBufferParameters layout = rtxdi::CalculateReservoirBufferParameters(SIDE, SIDE, rtxdi::CheckerboardMode::Off);
  // The reservoirs' two arrays (rt_di_surface.hlsli DI_*_ARRAY): the initial and temporal passes', the shading's.
  ComPtr<ID3D12Resource> reservoirs = buffer(uint64_t{layout.reservoirArrayPitch} * 2 * sizeof(RTXDI_PackedDIReservoir), D3D12_HEAP_TYPE_DEFAULT,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  std::array<uint8_t, OFFSETS * 2> offset_bytes = {};
  rtxdi::FillNeighborOffsetBuffer(offset_bytes.data(), OFFSETS, ReSTIRDI_SpatialSamplingMode::StratifiedLowDiscrepancy);
  std::array<float, OFFSETS * 2> offset_floats = {};
  for (size_t index = 0; index < offset_bytes.size(); ++index) offset_floats[index] = static_cast<float>(static_cast<int8_t>(offset_bytes[index])) / 127.f;
  ComPtr<ID3D12Resource> offsets = buffer(sizeof(offset_floats), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ComPtr<ID3D12Resource> offsets_upload = upload(offset_floats.data(), sizeof(offset_floats));
  list->CopyBufferRegion(offsets.Get(), 0, offsets_upload.Get(), 0, sizeof(offset_floats));
  const D3D12_RESOURCE_BARRIER offsets_ready = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                                .Transition = {.pResource = offsets.Get(), .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                               .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST, .StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS}};
  list->ResourceBarrier(1, &offsets_ready);
  const std::vector<uint32_t> zero_history(size_t{SIDE} * MASK_ROWS, 0u);
  fill(positions_texture.Get(), positions.data(), 16);
  std::vector<std::array<float, 4>> last_positions = positions;
  for (auto& last : last_positions) {
    last[1] = -100.f;
    last[3] *= 3.f;
  }
  fill(last_positions_texture.Get(), last_positions.data(), 16);
  fill(sun_texture.Get(), sun.data(), 16);
  fill(surface_texture.Get(), surface.data(), 16);
  fill(previous_surface_texture.Get(), surface.data(), 16);
  fill(history_textures[0].Get(), zero_history.data(), 4);
  fill(history_textures[1].Get(), zero_history.data(), 4);
  CHECK(execute());

  // A pass: its shader with PATH_DI as build.ps1 builds it; every binding by its reflection (the root a table of them).
  struct Pass {
    ComPtr<ID3D12ShaderReflection> reflection;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> heap;
    std::map<std::string, ComPtr<ID3D12Resource>> constants;
    std::map<std::string, uint8_t*> mapped;
    std::string disassembly;
  };
  const std::wstring rtxdi_wide(rtxdi.begin(), rtxdi.end());
  const auto make_pass = [&](const wchar_t* file, Pass* pass) {
    const std::wstring path = std::wstring(repository.begin(), repository.end()) + L"\\src\\games\\twattila\\dlaa_helper\\" + file;
    ComPtr<IDxcBlobEncoding> source;
    if (FAILED(utils->LoadFile(path.c_str(), nullptr, &source))) return false;
    const std::vector<LPCWSTR> arguments = {path.c_str(), L"-T", L"cs_6_5", L"-E", L"main", L"-O3", L"-D", L"PATH_DI", L"-D", L"PATH_REGIR", L"-I", rtxdi_wide.c_str()};
    const DxcBuffer source_buffer = {.Ptr = source->GetBufferPointer(), .Size = source->GetBufferSize(), .Encoding = DXC_CP_ACP};
    ComPtr<IDxcResult> result;
    compiler->Compile(&source_buffer, const_cast<LPCWSTR*>(arguments.data()), static_cast<UINT32>(arguments.size()), includes.Get(), IID_PPV_ARGS(&result));
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
    const DxcBuffer object_buffer = {.Ptr = object->GetBufferPointer(), .Size = object->GetBufferSize()};
    ComPtr<IDxcResult> disassembled;
    ComPtr<IDxcBlobUtf8> text;
    if (SUCCEEDED(compiler->Disassemble(&object_buffer, IID_PPV_ARGS(&disassembled)))) disassembled->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&text), nullptr);
    if (text != nullptr) pass->disassembly.assign(text->GetStringPointer(), text->GetStringLength());
    D3D12_SHADER_DESC shader_desc = {};
    pass->reflection->GetDesc(&shader_desc);
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    UINT descriptors = 0;
    for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
      D3D12_SHADER_INPUT_BIND_DESC bind = {};
      pass->reflection->GetResourceBindingDesc(index, &bind);
      if (bind.Type == D3D_SIT_SAMPLER) {
        samplers.push_back({.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                            .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .MaxLOD = D3D12_FLOAT32_MAX, .ShaderRegister = bind.BindPoint,
                            .RegisterSpace = bind.Space});
        continue;
      }
      D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      if (bind.Type == D3D_SIT_CBUFFER) type = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
      if (bind.Type == D3D_SIT_UAV_RWTYPED || bind.Type == D3D_SIT_UAV_RWSTRUCTURED || bind.Type == D3D_SIT_UAV_RWBYTEADDRESS) type = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      ranges.push_back({.RangeType = type, .NumDescriptors = std::max(bind.BindCount, 1u), .BaseShaderRegister = bind.BindPoint, .RegisterSpace = bind.Space,
                        .OffsetInDescriptorsFromTableStart = descriptors});
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
  // The views: `bound` by name (an array's elements as name[i]), the rest null; the cbuffers as zeros.
  const auto bind_pass = [&](Pass* pass, const std::map<std::string, Bound>& bound) {
    D3D12_SHADER_DESC shader_desc = {};
    pass->reflection->GetDesc(&shader_desc);
    const UINT step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = pass->heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
      D3D12_SHADER_INPUT_BIND_DESC bind = {};
      pass->reflection->GetResourceBindingDesc(index, &bind);
      if (bind.Type == D3D_SIT_SAMPLER) continue;
      for (UINT element = 0; element < std::max(bind.BindCount, 1u); ++element) {
        const std::string name = (bind.BindCount > 1) ? std::format("{}[{}]", bind.Name, element) : std::string(bind.Name);
        const auto found = bound.find(name);
        const Bound view_of = (found != bound.end()) ? found->second : Bound{};
        if (bind.Type == D3D_SIT_CBUFFER) {
          ID3D12ShaderReflectionConstantBuffer* cbuffer = pass->reflection->GetConstantBufferByName(bind.Name);
          D3D12_SHADER_BUFFER_DESC cbuffer_desc = {};
          cbuffer->GetDesc(&cbuffer_desc);
          const UINT size = (cbuffer_desc.Size + 255u) & ~255u;
          ComPtr<ID3D12Resource>& constants = pass->constants[bind.Name];
          if (!constants) {
            constants = buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
            constants->Map(0, nullptr, reinterpret_cast<void**>(&pass->mapped[bind.Name]));
            std::memset(pass->mapped[bind.Name], 0, size);
          }
          const D3D12_CONSTANT_BUFFER_VIEW_DESC view = {.BufferLocation = constants->GetGPUVirtualAddress(), .SizeInBytes = size};
          device->CreateConstantBufferView(&view, handle);
        } else if (bind.Type == D3D_SIT_RTACCELERATIONSTRUCTURE) {
          const D3D12_SHADER_RESOURCE_VIEW_DESC view = {.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE,
                                                        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                        .RaytracingAccelerationStructure = {.Location = tlas->GetGPUVirtualAddress()}};
          device->CreateShaderResourceView(nullptr, &view, handle);
        } else if (bind.Type == D3D_SIT_UAV_RWSTRUCTURED) {
          const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                         .Buffer = {.NumElements = view_of.elements, .StructureByteStride = std::max(view_of.stride, 4u)}};
          device->CreateUnorderedAccessView(view_of.resource, nullptr, &view, handle);
        } else if (bind.Type == D3D_SIT_UAV_RWTYPED) {
          const DXGI_FORMAT format = (view_of.format != DXGI_FORMAT_UNKNOWN) ? view_of.format : DXGI_FORMAT_R32_UINT;
          D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
          if (bind.Dimension == D3D_SRV_DIMENSION_BUFFER) {
            view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            view.Buffer.NumElements = view_of.elements;
          }
          device->CreateUnorderedAccessView(view_of.resource, nullptr, &view, handle);
        } else {
          const DXGI_FORMAT format = (view_of.format != DXGI_FORMAT_UNKNOWN) ? view_of.format : DXGI_FORMAT_R32G32B32A32_FLOAT;
          D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                  .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Texture2D = {.MipLevels = 1}};
          if (bind.Dimension == D3D_SRV_DIMENSION_BUFFER) {
            view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            view.Buffer = {.NumElements = 1};
            if (format == DXGI_FORMAT_R32G32B32A32_FLOAT) view.Format = DXGI_FORMAT_R32G32B32A32_UINT;
          } else if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY) {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray = {.MipLevels = 1, .ArraySize = 1};
          }
          device->CreateShaderResourceView(view_of.resource, &view, handle);
        }
        handle.ptr += step;
      }
    }
  };
  // A cbuffer variable by name (an array's element `offset` bytes in); a cbuffer the pass does not read is left out, a
  // variable a read cbuffer does not have ends the test.
  const auto set_bytes = [](Pass* pass, const char* cbuffer, const char* name, const void* value, size_t size, size_t offset = 0) {
    if (!pass->mapped.contains(cbuffer)) return;
    ID3D12ShaderReflectionConstantBuffer* buffer_reflection = pass->reflection->GetConstantBufferByName(cbuffer);
    D3D12_SHADER_VARIABLE_DESC variable = {};
    if (FAILED(buffer_reflection->GetVariableByName(name)->GetDesc(&variable))) {
      std::printf("FAIL: no %s.%s in the pass\n", cbuffer, name);
      std::exit(1);
    }
    std::memcpy(pass->mapped[cbuffer] + variable.StartOffset + offset, value, std::min<size_t>(size, variable.Size - offset));
  };
  const auto set_uint = [&](Pass* pass, const char* cbuffer, const char* name, uint32_t value) { set_bytes(pass, cbuffer, name, &value, 4); };
  const auto dispatch = [&](Pass* pass) {
    ID3D12DescriptorHeap* heaps[] = {pass->heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(pass->root.Get());
    list->SetPipelineState(pass->pipeline.Get());
    list->SetComputeRootDescriptorTable(0, pass->heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch(SIDE / 8, SIDE / 8, 1);
    return execute();
  };

  std::array<Pass, 4> passes;
  const std::array<const wchar_t*, 4> files = {L"rt_di_initial.hlsl", L"rt_di_temporal.hlsl", L"rt_di_spatial.hlsl", L"rt_di_shade.hlsl"};
  for (size_t pass = 0; pass < passes.size(); ++pass) CHECK(make_pass(files[pass], &passes[pass]));
  std::printf("compiled: rt_di_initial, rt_di_temporal, rt_di_spatial, rt_di_shade (PATH_DI, PATH_REGIR)\n");
  uint32_t bias_rays = 0;
  for (size_t at = passes[1].disassembly.find("call void @dx.op.rayQuery_TraceRayInline"); at != std::string::npos;
       at = passes[1].disassembly.find("call void @dx.op.rayQuery_TraceRayInline", at + 1)) {
    ++bias_rays;
  }
  const bool part0 = bias_rays == 1;
  std::printf("%s 0 temporal bias ray built: %u TraceRayInline calls in rt_di_temporal (needs 1)\n", part0 ? "PASS" : "FAIL", bias_rays);
  failures += part0 ? 0 : 1;

  // Every light at one place, 2 m over the floor's middle (each the same target at a pixel), white, smooth, reach 20 m.
  std::array<std::array<float, 4>, 128> lights = {};
  std::array<std::array<float, 4>, 128> colours = {};
  for (uint32_t light = 0; light < LIGHTS; ++light) {
    lights[light] = {1.6f, 2.f, 1.6f, 20.f};
    colours[light] = {1.f, 1.f, 1.f, 1.f};
  }
  std::array<uint32_t, 32> previous = {};
  std::fill(previous.begin(), previous.end(), 0xFFFFFFFFu);
  const auto run = [&](bool members_off, uint32_t frame, uint32_t di_flags, std::vector<RTXDI_PackedDIReservoir>* initial,
                       std::vector<RTXDI_PackedDIReservoir>* shaded, std::vector<uint8_t>* slm, std::vector<uint8_t>* skin) {
    std::vector<uint32_t> mask;
    std::vector<std::array<uint32_t, 4>> members;
    mask_and_members(members_off, &mask, &members);
    fill(mask_texture.Get(), mask.data(), 4);
    fill(members_texture.Get(), members.data(), 16);
    if (!execute()) return false;
    const std::map<std::string, Bound> bound = {
        {"positions", {positions_texture.Get()}},
        {"ambient_positions", {last_positions_texture.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT}},
        {"sun", {sun_texture.Get()}},
        {"light_mask", {mask_texture.Get(), DXGI_FORMAT_R32_UINT}},
        {"light_history[0]", {history_textures[0].Get(), DXGI_FORMAT_R32_UINT}},
        {"light_history[1]", {history_textures[1].Get(), DXGI_FORMAT_R32_UINT}},
        {"di_members", {members_texture.Get(), DXGI_FORMAT_R32G32B32A32_UINT}},
        {"light_surface", {surface_texture.Get(), DXGI_FORMAT_R32G32B32A32_UINT}},
        {"light_surface_previous", {previous_surface_texture.Get(), DXGI_FORMAT_R32G32B32A32_UINT}},
        {"di_reservoirs", {reservoirs.Get(), DXGI_FORMAT_UNKNOWN, layout.reservoirArrayPitch * 2, sizeof(RTXDI_PackedDIReservoir)}},
        {"di_offsets", {offsets.Get(), DXGI_FORMAT_R32G32_FLOAT, OFFSETS}},
        {"di_light[0]", {light_textures[0].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT}},
        {"di_light[1]", {light_textures[1].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT}},
        {"di_origin[0]", {origin_textures[0].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT}},
        {"di_origin[1]", {origin_textures[1].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT}},
    };
    for (Pass& pass : passes) {
      bind_pass(&pass, bound);
      set_uint(&pass, "Settings", "frame", frame);
      set_uint(&pass, "Settings", "flags", RECONSTRUCTED);
      set_uint(&pass, "Settings", "width", SIDE);
      set_uint(&pass, "Settings", "height", SIDE);
      const float light_size = 0.1f;
      set_bytes(&pass, "Settings", "light_size", &light_size, 4);
      set_uint(&pass, "Settings", "light_tile_row", SIDE);
      set_bytes(&pass, "Lights", "lights", lights.data(), sizeof(lights));
      set_uint(&pass, "Lights", "light_count", LIGHTS);
      set_bytes(&pass, "Lights", "light_colours", colours.data(), sizeof(colours));
      set_bytes(&pass, "Lights", "light_previous", previous.data(), sizeof(previous));
      set_uint(&pass, "DiPass", "di_block_row_pitch", layout.reservoirBlockRowPitch);
      set_uint(&pass, "DiPass", "di_array_pitch", layout.reservoirArrayPitch);
      set_uint(&pass, "DiPass", "di_flags", di_flags);
      set_uint(&pass, "DiPass", "di_uniform_random", frame * 2654435761u);
      set_uint(&pass, "DiPass", "di_offset_mask", OFFSETS - 1);
    }
    for (size_t pass = 0; pass < passes.size(); ++pass) {
      if (!dispatch(&passes[pass])) return false;
      if (pass == 0) {
        const std::vector<uint8_t> bytes = read_buffer(reservoirs.Get());
        initial->resize(layout.reservoirArrayPitch);
        std::memcpy(initial->data(), bytes.data(), initial->size() * sizeof(RTXDI_PackedDIReservoir));
      }
    }
    const std::vector<uint8_t> bytes = read_buffer(reservoirs.Get());
    shaded->resize(layout.reservoirArrayPitch);
    std::memcpy(shaded->data(), bytes.data() + uint64_t{layout.reservoirArrayPitch} * sizeof(RTXDI_PackedDIReservoir),
                shaded->size() * sizeof(RTXDI_PackedDIReservoir));
    *slm = read_texture(light_textures[0].Get(), 8);
    *skin = read_texture(light_textures[1].Get(), 8);
    return true;
  };
  // RTXDI's block-linear place of a pixel (ReservoirAddressing.hlsli).
  const auto place = [&](uint32_t x, uint32_t y) {
    return (y / 16) * layout.reservoirBlockRowPitch + (x / 16) * 256 + (y % 16) * 16 + (x % 16);
  };
  const auto listed = [&](uint32_t x, uint32_t y, uint32_t light) {
    const auto& tile_list = tile_lists[(y / TILE) * TILES + (x / TILE)];
    return std::find(tile_list.begin(), tile_list.end(), light) != tile_list.end();
  };
  const auto half = [](const std::vector<uint8_t>& texels, size_t index) {
    uint16_t value;
    std::memcpy(&value, texels.data() + index * 2, 2);
    return static_cast<double>(DirectX::PackedVector::XMConvertHalfToFloat(value));
  };

  // Parts 1-3 with the members as rt_lights writes them; part 2's CONTROL with them off.
  std::vector<std::vector<uint32_t>> picks(TILES * TILES, std::vector<uint32_t>(LIGHTS, 0u));
  uint32_t unlisted_initial = 0;
  uint32_t invalid_initial = 0;
  uint32_t weight_off = 0;
  uint32_t unlisted_spatial = 0;
  uint32_t unlisted_spatial_off = 0;
  uint32_t shade_bad = 0;
  double max_weight_error = 0.0;
  for (uint32_t frame = 1; frame <= FRAMES; ++frame) {
    for (const bool members_off : {false, true}) {
      std::vector<RTXDI_PackedDIReservoir> initial;
      std::vector<RTXDI_PackedDIReservoir> shaded;
      std::vector<uint8_t> slm;
      std::vector<uint8_t> skin;
      CHECK(run(members_off, frame, DI_NO_TEMPORAL, &initial, &shaded, &slm, &skin));
      for (uint32_t y = 0; y < SIDE; ++y) {
        for (uint32_t x = 0; x < SIDE; ++x) {
          const RTXDI_PackedDIReservoir& first = initial[place(x, y)];
          const RTXDI_PackedDIReservoir& last = shaded[place(x, y)];
          const uint32_t first_light = first.lightData & 0x7FFFFFFFu;
          const uint32_t last_light = last.lightData & 0x7FFFFFFFu;
          if (!members_off) {
            if ((first.lightData & 0x80000000u) == 0) {
              ++invalid_initial;
            } else {
              if (!listed(x, y, first_light)) ++unlisted_initial;
              if (first_light < LIGHTS) ++picks[(y / TILE) * TILES + (x / TILE)][first_light];
              const double expected = static_cast<double>(tile_lists[(y / TILE) * TILES + (x / TILE)].size());
              const double error = std::abs(first.weight - expected) / expected;
              max_weight_error = std::max(max_weight_error, error);
              if (error > 1e-4) ++weight_off;
            }
            if ((last.lightData & 0x80000000u) != 0 && !listed(x, y, last_light)) ++unlisted_spatial;
            const double light = half(slm, (size_t{y} * SIDE + x) * 4);
            const double skin_light = half(skin, (size_t{y} * SIDE + x) * 4);
            if (!(std::isfinite(light) && light > 0.0) || skin_light != 0.0) ++shade_bad;
          } else if ((last.lightData & 0x80000000u) != 0 && !listed(x, y, last_light)) {
            ++unlisted_spatial_off;
          }
        }
      }
    }
  }
  // Chi-square of the picks against uniform over each tile's list.
  double chi_square = 0.0;
  double freedom = 0.0;
  for (uint32_t tile = 0; tile < TILES * TILES; ++tile) {
    const auto& tile_list = tile_lists[tile];
    uint32_t total = 0;
    for (const uint32_t light : tile_list) total += picks[tile][light];
    const double expected = static_cast<double>(total) / static_cast<double>(tile_list.size());
    for (const uint32_t light : tile_list) chi_square += (picks[tile][light] - expected) * (picks[tile][light] - expected) / expected;
    freedom += static_cast<double>(tile_list.size() - 1);
  }
  const double z = (freedom > 0.0) ? (chi_square - freedom) / std::sqrt(2.0 * freedom) : 0.0;
  const bool part1 = unlisted_initial == 0 && invalid_initial == 0 && weight_off == 0 && std::abs(z) <= 4.0;
  std::printf("%s 1 initial sampling: %u unlisted picks, %u invalid reservoirs, %u weights off n (largest %.2e), chi-square %.1f over %.0f (z %.2f)\n",
              part1 ? "PASS" : "FAIL", unlisted_initial, invalid_initial, weight_off, max_weight_error, chi_square, freedom, z);
  const bool part2 = unlisted_spatial == 0 && unlisted_spatial_off > 0;
  std::printf("%s 2 spatial reuse: %u unlisted lights in the output; CONTROL members off: %u (must be > 0)\n", part2 ? "PASS" : "FAIL", unlisted_spatial,
              unlisted_spatial_off);
  const bool part3 = shade_bad == 0;
  std::printf("%s 3 shading: %u pixels with the slm light not finite and > 0 or the skinlm light not 0\n", part3 ? "PASS" : "FAIL", shade_bad);
  failures += part1 ? 0 : 1;
  failures += part2 ? 0 : 1;
  failures += part3 ? 0 : 1;

  // 4. The last trace's lights the same (light_previous: each its own place), its reservoirs empty with M 20 (the shading's
  // array, the temporal pass's history); this frame without temporal reuse is the reference.
  for (uint32_t light = 0; light < LIGHTS; ++light) {
    previous[light >> 2] = (previous[light >> 2] & ~(0xFFu << ((light & 3) * 8))) | (light << ((light & 3) * 8));
  }
  const auto mean_slm = [&](uint32_t di_flags, double* mean) {
    std::vector<RTXDI_PackedDIReservoir> history(size_t{layout.reservoirArrayPitch} * 2, RTXDI_PackedDIReservoir{});
    for (RTXDI_PackedDIReservoir& reservoir : history) reservoir.mVisibility = 20u << 18;
    ComPtr<ID3D12Resource> history_upload = upload(history.data(), history.size() * sizeof(RTXDI_PackedDIReservoir));
    std::array<D3D12_RESOURCE_BARRIER, 2> states = {};
    for (size_t at = 0; at < states.size(); ++at) {
      states[at] = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Transition = {.pResource = reservoirs.Get(), .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   .StateBefore = (at == 0) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_DEST,
                                   .StateAfter = (at == 0) ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_UNORDERED_ACCESS}};
    }
    list->ResourceBarrier(1, &states[0]);
    list->CopyBufferRegion(reservoirs.Get(), 0, history_upload.Get(), 0, history.size() * sizeof(RTXDI_PackedDIReservoir));
    list->ResourceBarrier(1, &states[1]);
    std::vector<RTXDI_PackedDIReservoir> initial;
    std::vector<RTXDI_PackedDIReservoir> shaded;
    std::vector<uint8_t> slm;
    std::vector<uint8_t> skin;
    if (!run(false, FRAMES + 1, di_flags | DI_NO_SPATIAL, &initial, &shaded, &slm, &skin)) return false;
    double sum = 0.0;
    for (size_t pixel = 0; pixel < size_t{SIDE} * SIDE; ++pixel) sum += half(slm, pixel * 4);
    *mean = sum / (SIDE * SIDE);
    return true;
  };
  double reference = 0.0;
  double ray_traced = 0.0;
  double basic = 0.0;
  double off = 0.0;
  CHECK(mean_slm(DI_NO_TEMPORAL, &reference));
  CHECK(mean_slm(DI_HISTORY | (0u << DI_BIAS_SHIFT), &ray_traced));
  CHECK(mean_slm(DI_HISTORY | (1u << DI_BIAS_SHIFT), &basic));
  CHECK(mean_slm(DI_HISTORY | (3u << DI_BIAS_SHIFT), &off));
  const auto ratio = [&](double value) { return (reference > 0.0) ? value / reference : 0.0; };
  const bool part4 = reference > 0.0 && std::abs(ratio(ray_traced) - 1.0) <= 0.01 && std::abs(ratio(basic) - 1.0) <= 0.01 && ratio(off) < 0.1;
  std::printf("%s 4 temporal bias correction: the light over the one-frame reference: ray traced %.4f, basic %.4f (each within 1 %%); CONTROL off %.4f "
              "(1/21 = 0.0476, must be < 0.1)\n",
              part4 ? "PASS" : "FAIL", ratio(ray_traced), ratio(basic), ratio(off));
  failures += part4 ? 0 : 1;

  // 5. Every light's colour (2, -0.5, 1): its luminance > 0 (picked), its green negative.
  for (uint32_t light = 0; light < LIGHTS; ++light) colours[light] = {2.f, -0.5f, 1.f, 1.f};
  uint32_t tinted_bad = 0;
  {
    std::vector<RTXDI_PackedDIReservoir> initial;
    std::vector<RTXDI_PackedDIReservoir> shaded;
    std::vector<uint8_t> slm;
    std::vector<uint8_t> skin;
    CHECK(run(false, FRAMES + 2, DI_NO_TEMPORAL, &initial, &shaded, &slm, &skin));
    for (size_t pixel = 0; pixel < size_t{SIDE} * SIDE; ++pixel) {
      const double red = half(slm, pixel * 4);
      const double green = half(slm, pixel * 4 + 1);
      if (!(std::isfinite(red) && red > 0.0) || green != 0.0) ++tinted_bad;
    }
  }
  const bool part5 = tinted_bad == 0;
  std::printf("%s 5 negative colour channel: %u pixels with the slm light not finite and > 0 or its green not 0\n", part5 ? "PASS" : "FAIL", tinted_bad);
  failures += part5 ? 0 : 1;
  std::printf(failures == 0 ? "ALL PASSED\n" : "FAILED\n");
  return failures == 0 ? 0 : 1;
}
