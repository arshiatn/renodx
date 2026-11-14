// Pharaoh's (src/games/twpharaohdynasties) shadow-map table and ray-traced captures against the game's own bytes.
// tests.exe <the game's dumped csos> <the add-on's built csos (build\twpharaohdynasties.include\embed)> [Pharaoh.exe]
// 1. The campaign's trees (rigidcampaigntrees vs40_main 16D14106, Troy's 07321725 line for line): the capture
//    (rt_capture_campaign_trees, Troy's) from random instanced vertices: its world the game's TEXCOORD0, its UV TEXCOORD0's
//    (o3.xy) as halves; CONTROL an instance row a metre off. Their shadow pass 81696642 through Troy's outside-view geometry
//    shader (RT_WORLD_UV + RT_COLOUR) with every triangle outside the main view: each corner its TEXCOORD0 world and the UV;
//    CONTROL Warhammer II's RT_WORLD_UV (no COLOR0 member) on the same vertex shader differs.
// 2. Three Shadow Cascades Under Rays (shadow_map.hpp) over the shadow setup's table as Pharaoh.exe ships it (read from disk,
//    its sections mapped by hand; no relocation in these bytes), in a page of ours as the found rows: the patterns once; the
//    setup stores a row's first field at +0x168 and the cascade getter reads the same table's first field (the count); off,
//    Set writes Shadow Map Size's bytes alone; on, the three rows above Medium (qualities 3-5, 4 cascades each) take Medium's
//    count (3), nothing else; off again and the game's size: the game's bytes; CONTROL: a Medium of 4 or of 0, none written.
//    Printed: the last split of the battle's and the campaign's highest quality (the shadow distance) with 4 and with 3.
// 3. The placed ropes (rope vs40_main_rigid 655AB38D): the capture (rt_capture_rope_rigid, make_wind_motion_vs.py :world_view)
//    from random ropes near the camera: its world through the camera's view_projection is the game's SV_Position within 1e-4
//    of w; CONTROL: the rope's middle (the game's TEXCOORD5, before its camera-facing offset) misses that tolerance, every one.
// 4. The campaign's vegetation (rigidcampaignvegetation vs40_main E7792B60, Troy's 04C7C807 with its wind constants at other
//    offsets): the capture (rt_capture_campaign_vegetation) from 256 random vertices with the camera and a blowing wind: its
//    world x the view-projection is the game's SV_Position (to the float rounding of its terms), its w the UV (POSITION0.w,
//    NORMAL0.w) as two halves; CONTROL the capture a second later differs (the wind is in play).
// 5. The fur-clad units (weighted4_fur_alpha vs40_main_4_bones_fur 621E86DD): the capture (rt_capture_unit_4_bones_fur) from
//    random skinned vertices is the game's TEXCOORD0 (the skinned position plus the fur's sway); CONTROL 4A716936's capture
//    (no sway) differs.
// 6. The 8-bone models (weightedcommon vs40_main_8_bones F81881EB): from random 8-bone vertices (bone bytes 0-5, some weights
//    under 1/256) the capture (rt_capture_unit_8_bones) is the game's TEXCOORD0 and the motion shader's
//    (motion_unit_weightedcommon_f818) SV_Position the game's; CONTROL the game's with every bone byte 0 differs.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/games/twpharaohdynasties/shadow_map.hpp"
#include "../../src/games/twtroy/rt_outside_view_colour_world_uv_gs.h"
#include "../../src/games/twwarhammer2/rt_outside_view_world_uv_gs.h"

extern "C" __declspec(dllexport) const char* const NAME = "Pharaoh test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static int failures = 0;

#define CHECK(x, ...)                                  \
  do {                                                 \
    if (!(x)) {                                        \
      std::printf("FAIL line %d: %s: ", __LINE__, #x); \
      std::printf(__VA_ARGS__);                        \
      std::printf("\n");                               \
      ++failures;                                      \
    }                                                  \
  } while (false)

// The camera (b0, 784 bytes) of a fog draw in Troy's tools\tw\troy\runs\ATN_session3_030818\trace_04_031153.txt: Pharaoh's camera
// buffer has the same layout (camera_position c0, view c1, projection c5, view_projection c9, inv_view c13).
constexpr std::array<float, 196> CAMERA = {
    625.85455f, 126.21127f, -607.9252f, 6.6291106e-10f, 0.6160624f, 5.1151194e-09f, 0.7876973f, 93.29556f,
    -0.27021432f, 0.9393195f, 0.21133609f, 179.0387f, -0.7398995f, -0.34304333f, 0.5786795f, 858.15924f, 0.f, 0.f,
    0.f, 1.f, 0.88294804f, 0.f, 0.00022786458f, 0.f, 0.f, 1.5696855f, 0.00033150436f, 0.f, 0.f, 0.f, 1.000077f,
    -1.000077f, 0.f, 0.f, 1.f, 0.f, 0.5437825f, -7.8162906e-05f, 0.6956276f, 82.57068f, -0.42439675f, 1.4743224f,
    0.33192304f, 281.3189f, -0.7399565f, -0.34306973f, 0.5787241f, 857.2253f, -0.7398995f, -0.34304333f, 0.5786795f,
    858.15924f, 0.6160624f, -0.27021432f, -0.7398995f, 625.85455f, 5.1151208e-09f, 0.93931973f, -0.34304342f,
    126.21129f, 0.7876973f, 0.21133609f, 0.5786795f, -607.9252f, 0.f, 0.f, 0.f, 1.f, 1.1325696f, 0.f, 0.f,
    -0.00025807245f, 0.f, 0.63707036f, 0.f, -0.00021119158f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -0.99992305f, 1.f,
    0.6977302f, -0.17213511f, -625.8183f, 625.12665f, 1.1008985e-05f, 0.59840715f, -126.20397f, 125.870476f,
    0.89212406f, 0.13463713f, 607.8901f, -607.35846f, 5.375481e-09f, 1.0750962e-08f, -0.9999423f, 1.0000193f, 1.f,
    13000.f, 7.6929e-05f, 7.6929e-05f, 24.614f, 1.1325694f, 0.6370703f, 1.134464f, 3840.f, 2160.f, 0.00026041668f,
    0.00046296295f, 0.f, 6.26474e-10f, 2.6254088e-06f, 2.5607402e-12f, 1920.f, 1080.f, 0.00052083336f, 0.0009259259f,
    0.f, 0.f, 6.4094063e-10f, 6.8242946e-07f, 3840.f, 2160.f, 0.00026041668f, 0.00046296295f, 0.f, 0.f, 0.f, 0.f, 0.f,
    0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, -1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1e-45f, 1.f, 3.4444333e-12f,
    7.0366745e+22f, 6.8192746e-10f, 0.f, 0.f, 0.f, 0.f, 624.589f, 125.269806f, -608.3733f, 6.8192746e-10f, 625.9845f,
    125.26984f, -606.589f, 1.0313943e-08f, 625.64026f, 126.46669f, -606.3197f, 6.8192746e-10f, 624.24475f, 126.46666f,
    -608.104f, 1.06222515e-05f, -15825.483f, -12112.719f, -6432.9443f, 6.8192746e-10f, 2315.5881f, -12112.719f,
    16762.225f, 1.6691486e-07f, -2160.1956f, 3446.0122f, 20262.76f, 6.8192746e-10f, -20301.268f, 3446.0122f,
    -2932.4094f, 0.f, 0.f, 0.f, 1.3f, 0.f, 0.f, 0.f, 0.f, 0.f,
};

static std::vector<char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static ComPtr<ID3D11Buffer> ConstantBuffer(const void* data, UINT size) {
  std::vector<char> padded((size + 15) & ~15u, 0);
  std::memcpy(padded.data(), data, size);
  const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(padded.size()), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, &buffer);
  return buffer;
}

static float HalfToFloat(uint32_t bits) {
  const uint32_t exponent = (bits >> 10) & 0x1Fu;
  const uint32_t value = (exponent == 0) ? ((bits & 0x8000u) << 16) : (((bits & 0x8000u) << 16) | ((exponent + 112u) << 23) | ((bits & 0x3FFu) << 13));
  float result;
  std::memcpy(&result, &value, 4);
  return result;
}

// The vertex shader's inputs (its signature's, system values aside) each 4 floats of `vertices` in order, from one buffer.
static ComPtr<ID3D11InputLayout> InputLayout(const std::vector<char>& code, UINT* stride) {
  ComPtr<ID3D11ShaderReflection> reflection;
  D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection));
  if (reflection == nullptr) return nullptr;
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  for (UINT parameter = 0; parameter < shader_desc.InputParameters; ++parameter) {
    D3D11_SIGNATURE_PARAMETER_DESC input = {};
    reflection->GetInputParameterDesc(parameter, &input);
    if (input.SystemValueType != D3D_NAME_UNDEFINED) continue;
    const DXGI_FORMAT format = (input.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ? DXGI_FORMAT_R32G32B32A32_UINT
                               : (input.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ? DXGI_FORMAT_R32G32B32A32_SINT
                                                                                         : DXGI_FORMAT_R32G32B32A32_FLOAT;
    elements.push_back({input.SemanticName, input.SemanticIndex, format, 0, static_cast<UINT>(16 * elements.size()), D3D11_INPUT_PER_VERTEX_DATA, 0});
  }
  ComPtr<ID3D11InputLayout> layout;
  device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), code.data(), code.size(), &layout);
  *stride = static_cast<UINT>(16 * elements.size());
  return layout;
}

// What `geometry` streams out (4 floats a vertex) of `vertices` drawn as `topology` with the vertex shader `vertex_code` and
// `constants` at the vertex stage's b0.., `textures` at its t0..; `geometry_constant` at the geometry stage's b0.
static std::vector<float> Run(const std::vector<char>& vertex_code, ID3D11GeometryShader* geometry, D3D11_PRIMITIVE_TOPOLOGY topology,
                              const std::vector<float>& vertices, std::span<const ComPtr<ID3D11Buffer>> constants,
                              ID3D11Buffer* geometry_constant, std::span<const ComPtr<ID3D11ShaderResourceView>> textures = {}) {
  ComPtr<ID3D11VertexShader> shader;
  device->CreateVertexShader(vertex_code.data(), vertex_code.size(), nullptr, &shader);
  UINT input_stride = 0;
  const ComPtr<ID3D11InputLayout> layout = InputLayout(vertex_code, &input_stride);
  CHECK(shader != nullptr && geometry != nullptr && layout != nullptr, "stream output: the shaders");
  if (shader == nullptr || geometry == nullptr || layout == nullptr) return {};
  const UINT count = static_cast<UINT>(vertices.size() * 4 / input_stride);
  const D3D11_BUFFER_DESC input_desc = {.ByteWidth = static_cast<UINT>(vertices.size() * 4), .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA input_data = {.pSysMem = vertices.data()};
  ComPtr<ID3D11Buffer> input;
  device->CreateBuffer(&input_desc, &input_data, &input);
  const UINT stride = 16;
  const D3D11_BUFFER_DESC out_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
  ComPtr<ID3D11Buffer> out;
  device->CreateBuffer(&out_desc, nullptr, &out);
  context->ClearState();
  context->IASetInputLayout(layout.Get());
  const UINT offset = 0;
  context->IASetVertexBuffers(0, 1, input.GetAddressOf(), &input_stride, &offset);
  context->IASetPrimitiveTopology(topology);
  for (UINT slot = 0; slot < constants.size(); ++slot) context->VSSetConstantBuffers(slot, 1, constants[slot].GetAddressOf());
  for (UINT slot = 0; slot < textures.size(); ++slot) context->VSSetShaderResources(slot, 1, textures[slot].GetAddressOf());
  if (geometry_constant != nullptr) context->GSSetConstantBuffers(0, 1, &geometry_constant);
  context->VSSetShader(shader.Get(), nullptr, 0);
  context->GSSetShader(geometry, nullptr, 0);
  context->SOSetTargets(1, out.GetAddressOf(), &offset);
  context->Draw(count, 0);
  ID3D11Buffer* no_target = nullptr;
  context->SOSetTargets(1, &no_target, &offset);
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), out.Get());
  std::vector<float> values(count * 4, 0.f);
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    std::memcpy(values.data(), mapped.pData, values.size() * 4);
    context->Unmap(staging.Get(), 0);
  }
  return values;
}

// A vertex shader's output `semantic` (`semantic_index`) for each point of `vertices`, streamed out through a geometry shader made
// from its own bytes; `textures` at t0...
static std::vector<float> StreamOut(const std::vector<char>& code, const char* semantic, const std::vector<float>& vertices,
                                    std::span<const ComPtr<ID3D11Buffer>> constants, UINT semantic_index = 0,
                                    std::span<const ComPtr<ID3D11ShaderResourceView>> textures = {}) {
  const D3D11_SO_DECLARATION_ENTRY entry = {.SemanticName = semantic, .SemanticIndex = semantic_index, .ComponentCount = 4};
  const UINT stride = 16;
  ComPtr<ID3D11GeometryShader> stream;
  device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), &entry, 1, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM,
                                               nullptr, &stream);
  return Run(code, stream.Get(), D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, vertices, constants, nullptr, textures);
}

// Each triangle corner's SV_Position out of the geometry shader `geometry_code` over `vertices` drawn with `vertex_code`;
// `main_view` at the geometry stage's b0.
static std::vector<float> StreamOutThrough(const std::vector<char>& vertex_code, std::span<const uint8_t> geometry_code,
                                           const std::vector<float>& vertices, std::span<const ComPtr<ID3D11Buffer>> constants,
                                           ID3D11Buffer* main_view) {
  const D3D11_SO_DECLARATION_ENTRY entry = {.SemanticName = "SV_Position", .ComponentCount = 4};
  const UINT stride = 16;
  ComPtr<ID3D11GeometryShader> geometry;
  device->CreateGeometryShaderWithStreamOutput(geometry_code.data(), geometry_code.size(), &entry, 1, &stride, 1,
                                               D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &geometry);
  return Run(vertex_code, geometry.Get(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, vertices, constants, main_view);
}

static void TestShadowCascades(const std::string& exe_path) {
  namespace shadow = twpharaohdynasties::shadow_map::internal;
  std::ifstream stream(exe_path, std::ios::binary);
  CHECK(stream.good(), "shadow cascades: %s", exe_path.c_str());
  const std::string file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (file.size() < 4096) return;
  const auto* base = reinterpret_cast<const uint8_t*>(file.data());
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
      reinterpret_cast<const uint8_t*>(&nt->OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader);
  const std::span<const IMAGE_SECTION_HEADER> section_list(sections, nt->FileHeader.NumberOfSections);
  // File offset <-> RVA through the section headers (0 / SIZE_MAX outside them).
  const auto rva_of = [&](const uint8_t* at) -> uint32_t {
    const auto offset = static_cast<uint32_t>(at - base);
    for (const auto& section : section_list) {
      if (offset >= section.PointerToRawData && offset < section.PointerToRawData + section.SizeOfRawData) {
        return section.VirtualAddress + (offset - section.PointerToRawData);
      }
    }
    return 0;
  };
  const auto at_rva = [&](uint32_t rva) -> const uint8_t* {
    for (const auto& section : section_list) {
      if (rva >= section.VirtualAddress && rva < section.VirtualAddress + section.SizeOfRawData) {
        return base + section.PointerToRawData + (rva - section.VirtualAddress);
      }
    }
    return nullptr;
  };
  // A pattern's one place in the file (nullptr: none or more than one); `table_at`: the rip-relative lea at `lea` bytes in.
  const auto once = [&](std::string_view pattern) -> const uint8_t* {
    const uint8_t* found = tw::engine_tweaks::FindPattern({file}, pattern);
    if (found == nullptr) return nullptr;
    const auto after = static_cast<size_t>(found - base) + 1;
    return (tw::engine_tweaks::FindPattern({std::string_view(file).substr(after)}, pattern) == nullptr) ? found : nullptr;
  };
  const auto lea_target = [&](const uint8_t* lea) {
    int32_t displacement;
    std::memcpy(&displacement, lea + 3, sizeof(displacement));
    return rva_of(lea) + 7 + displacement;
  };
  const uint8_t* read = once(shadow::TABLE_READ);
  const uint8_t* extreme = once(shadow::EXTREME);
  CHECK(read != nullptr && extreme != nullptr, "shadow cascades: the setup's patterns once each");
  if (read == nullptr || extreme == nullptr) return;
  const uint32_t table = lea_target(read);
  // The setup: mov eax, [rdx] (the row's first field); mov [rbx + 0x168], eax, right after the size's and the tile's.
  constexpr std::array<uint8_t, 8> COUNT_STORE = {0x8B, 0x02, 0x89, 0x83, 0x68, 0x01, 0x00, 0x00};
  CHECK(std::memcmp(read + 0x2B, COUNT_STORE.data(), COUNT_STORE.size()) == 0, "shadow cascades: the count's store at +0x168");
  // The cascade getter: movsxd rax, ecx; xorps; add rax, rax; lea rcx, [table]; mov eax, [rcx + rax * 8]; cvtsi2ss; ret.
  const uint8_t* getter = once("48 63 C1 0F 57 C0 48 03 C0 48 8D 0D ?? ?? ?? ?? 8B 04 C1 F3 48 0F 2A C0 C3");
  CHECK(getter != nullptr && lea_target(getter + 9) == table, "shadow cascades: the getter reads the same table's first field");
  // The shadow distance: mov r8d, [rcx + 0x168]; mov rbx, rcx; mov edx, [rcx + 0x148]; dec r8d (the last cascade's split).
  CHECK(once("44 8B 81 68 01 00 00 48 8B D9 8B 91 48 01 00 00 41 FF C8") != nullptr, "shadow cascades: the distance's read");
  const uint8_t* rows = at_rva(table + static_cast<uint32_t>(shadow::MEDIUM_ROW * shadow::ROW_SIZE));
  CHECK(rows != nullptr, "shadow cascades: the table in the file");
  if (rows == nullptr) return;
  const std::vector<uint8_t> game(rows, rows + shadow::ROWS_SIZE);
  const auto count = [](const uint8_t* row_bytes, size_t row) {
    int32_t value;
    std::memcpy(&value, row_bytes + ((row - shadow::MEDIUM_ROW) * shadow::ROW_SIZE), sizeof(value));
    return value;
  };
  CHECK(count(game.data(), 2) == 3 && count(game.data(), 3) == 4 && count(game.data(), 4) == 4 && count(game.data(), 5) == 4,
        "shadow cascades: the game's counts (Medium 3, the rest 4): %d %d %d %d", count(game.data(), 2), count(game.data(), 3),
        count(game.data(), 4), count(game.data(), 5));
  // The splits after the table (6 qualities x 7 floats, battle then campaign; the split function's lea).
  const auto split = [&](int map, int quality, int cascade) {
    float value;
    std::memcpy(&value, at_rva(table + 0x60) + (((map * 6 + quality) * 7 + cascade) * 4), sizeof(value));
    return value;
  };
  auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(page != nullptr, "shadow cascades: a page");
  if (page == nullptr) return;
  std::memcpy(page, game.data(), game.size());
  std::memcpy(page + 128, extreme, shadow::EXTREME_SIZE);
  const std::vector<uint8_t> game_extreme(extreme, extreme + shadow::EXTREME_SIZE);
  shadow::rows = page;
  shadow::extreme = page + 128;
  std::memcpy(shadow::original_rows.data(), page, shadow::ROWS_SIZE);
  std::memcpy(shadow::original_extreme.data(), page + 128, shadow::EXTREME_SIZE);
  const auto differing = [&](const std::vector<uint8_t>& expected_rows) {
    std::vector<size_t> at;
    for (size_t index = 0; index < shadow::ROWS_SIZE; ++index) {
      if (page[index] != expected_rows[index]) at.push_back(index);
    }
    return at;
  };
  const auto field = [](std::vector<uint8_t>* bytes, size_t row, size_t offset, uint32_t value) {
    std::memcpy(bytes->data() + ((row - shadow::MEDIUM_ROW) * shadow::ROW_SIZE) + offset, &value, sizeof(value));
  };
  // Off: Shadow Map Size alone (16384: rows 4 and 5's size and tile; the immediates' floats and tile).
  std::vector<uint8_t> sized = game;
  for (const size_t row : shadow::SIZE_ROWS) {
    field(&sized, row, shadow::SIZE_FIELD, 16384);
    field(&sized, row, shadow::TILE_FIELD, 8192);
  }
  twpharaohdynasties::shadow_map::Set(0, false);
  CHECK(differing(game).empty(), "shadow cascades: the game's size, off: the game's bytes");
  twpharaohdynasties::shadow_map::Set(16384, false);
  CHECK(differing(sized).empty(), "shadow cascades: 16384, off: Shadow Map Size's bytes alone");
  float extreme_size;
  std::memcpy(&extreme_size, page + 128 + shadow::EXTREME_FLOATS[0], sizeof(extreme_size));
  CHECK(extreme_size == 16384.f, "shadow cascades: 16384 at the immediates (%g)", extreme_size);
  // On: the three cascade counts above Medium become Medium's, nothing else.
  twpharaohdynasties::shadow_map::Set(16384, true);
  std::vector<uint8_t> three = sized;
  for (const size_t row : shadow::CASCADE_ROWS) field(&three, row, shadow::CASCADES_FIELD, 3);
  CHECK(differing(three).empty(), "shadow cascades: on: Medium's count in rows 3-5");
  const auto changed = differing(sized);
  CHECK(changed.size() == 3, "shadow cascades: on: %zu bytes differ from off (3 expected)", changed.size());
  for (const size_t index : changed) {
    CHECK(std::ranges::any_of(shadow::CASCADE_ROWS, [&](size_t row) { return index == (row - shadow::MEDIUM_ROW) * shadow::ROW_SIZE; }),
          "shadow cascades: on: byte %zu outside the cascade counts", index);
  }
  twpharaohdynasties::shadow_map::Set(0, true);
  std::vector<uint8_t> three_game = game;
  for (const size_t row : shadow::CASCADE_ROWS) field(&three_game, row, shadow::CASCADES_FIELD, 3);
  CHECK(differing(three_game).empty(), "shadow cascades: the game's size, on: the counts alone");
  CHECK(std::memcmp(page + 128, game_extreme.data(), shadow::EXTREME_SIZE) == 0, "shadow cascades: the game's immediates");
  // Off again, and Uninstall: the game's bytes.
  twpharaohdynasties::shadow_map::Set(0, false);
  CHECK(differing(game).empty(), "shadow cascades: off again: the game's bytes");
  twpharaohdynasties::shadow_map::Set(8192, true);
  twpharaohdynasties::shadow_map::Uninstall();
  CHECK(differing(game).empty() && std::memcmp(page + 128, game_extreme.data(), shadow::EXTREME_SIZE) == 0,
        "shadow cascades: Uninstall: the game's bytes");
  // CONTROL: a Medium of 4 (as the rows above it) or of 0 (no cascades): on writes no count.
  for (const uint32_t medium : {4u, 0u}) {
    std::vector<uint8_t> other = game;
    field(&other, 2, shadow::CASCADES_FIELD, medium);
    std::memcpy(page, other.data(), other.size());
    std::memcpy(shadow::original_rows.data(), other.data(), other.size());
    twpharaohdynasties::shadow_map::Set(0, true);
    CHECK(differing(other).empty(), "shadow cascades: CONTROL Medium %u: a count written", medium);
    twpharaohdynasties::shadow_map::Set(0, false);
  }
  shadow::rows = nullptr;
  shadow::extreme = nullptr;
  VirtualFree(page, 0, MEM_RELEASE);
  std::printf("2 shadow cascades: patterns once in Pharaoh.exe (table +%#x, the getter and the distance's read too); counts "
              "Medium 3, High / Ultra / Extreme 4; off: Shadow Map Size's bytes alone; on: %zu bytes differ (rows 3-5's counts "
              "to 3); back: the game's bytes; CONTROL Medium 4 / 0: none written. Shadow distance at the highest quality: battle "
              "%g m -> %g m, campaign %g -> %g\n",
              table, changed.size(), split(0, 5, 3), split(0, 5, 2), split(1, 5, 3), split(1, 5, 2));
}

// The campaign's trees: the capture against the game's 16D14106 from random instanced vertices, their shadow pass 81696642
// through Troy's RT_WORLD_UV + RT_COLOUR geometry shader with every triangle outside the main view.
static void TestCampaignTrees(const std::string& game_folder, const std::string& built_folder) {
  const auto game = ReadFile(game_folder + "\\0x16D14106.vs_5_0.cso");
  const auto shadow = ReadFile(game_folder + "\\0x81696642.vs_5_0.cso");
  const auto capture = ReadFile(built_folder + "\\rt_capture_campaign_trees.cso");
  CHECK(!game.empty() && !shadow.empty() && !capture.empty(), "campaign trees: the csos");
  if (game.empty() || shadow.empty() || capture.empty()) return;
  uint32_t seed = 777;
  const auto random = [&seed](float low, float high) {
    seed = seed * 1664525u + 1013904223u;
    return low + (high - low) * static_cast<float>(seed >> 8) / 16777216.f;
  };
  // 13 inputs x 4 floats: POSITION0 the model's point (w 1), TEXCOORD0 its UV, the normals, colour, the instance's rows in
  // POSITION1-3 (a turn and scale, the place near the camera in w), the rest random. 85 triangles.
  constexpr int VERTICES = 255;
  std::vector<float> vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    const float turn = random(0.f, 6.28f);
    const float scale = random(0.6f, 1.4f);
    for (int input = 0; input < 13; ++input) {
      std::array<float, 4> value = {random(-1.f, 1.f), random(-1.f, 1.f), random(-1.f, 1.f), random(0.f, 1.f)};
      if (input == 0) value = {random(-4.f, 4.f), random(0.f, 12.f), random(-4.f, 4.f), 1.f};
      if (input == 1) value = {random(0.f, 1.f), random(0.f, 1.f), 0.f, 0.f};
      if (input == 7) value = {scale * std::cos(turn), 0.f, scale * std::sin(turn), CAMERA[0] + random(-300.f, 300.f)};
      if (input == 8) value = {0.f, scale, 0.f, CAMERA[1] - random(20.f, 200.f)};
      if (input == 9) value = {-scale * std::sin(turn), 0.f, scale * std::cos(turn), CAMERA[2] + random(-300.f, 300.f)};
      vertices.insert(vertices.end(), value.begin(), value.end());
    }
  }
  const std::array<ComPtr<ID3D11Buffer>, 1> constants = {ConstantBuffer(CAMERA.data(), sizeof(CAMERA))};
  const auto reference = StreamOut(game, "TEXCOORD", vertices, constants);
  const auto worlds = StreamOut(capture, "TEXCOORD", vertices, constants);
  auto moved = vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) moved[vertex * 52 + 7 * 4 + 3] += 1.f;
  const auto moved_worlds = StreamOut(capture, "TEXCOORD", moved, constants);
  const auto shadow_worlds = StreamOut(shadow, "TEXCOORD", vertices, constants);
  // Every triangle outside the main view (w -1 everywhere).
  std::array<float, 56> main_view = {};
  main_view[13 * 4 + 3] = -1.f;
  const auto view = ConstantBuffer(main_view.data(), sizeof(main_view));
  const auto kept = StreamOutThrough(shadow, std::span<const uint8_t>(__rt_outside_view_colour_world_uv, sizeof(__rt_outside_view_colour_world_uv)),
                                     vertices, constants, view.Get());
  const auto kept_control = StreamOutThrough(shadow, std::span<const uint8_t>(__rt_outside_view_world_uv, sizeof(__rt_outside_view_world_uv)),
                                             vertices, constants, view.Get());
  if (reference.size() != VERTICES * 4 || worlds.size() != VERTICES * 4 || moved_worlds.size() != VERTICES * 4
      || shadow_worlds.size() != VERTICES * 4 || kept.size() != VERTICES * 4 || kept_control.size() != VERTICES * 4) {
    CHECK(false, "campaign trees: stream output sizes");
    return;
  }
  // The world against `expected` (x / w), relative to the size; the UV halves in w against TEXCOORD0.
  const auto errors = [&vertices](const std::vector<float>& world, const std::vector<float>& expected) {
    double place = 0.0;
    double uv = 0.0;
    for (int vertex = 0; vertex < VERTICES; ++vertex) {
      for (int axis = 0; axis < 3; ++axis) {
        const double want = static_cast<double>(expected[vertex * 4 + axis]) / expected[vertex * 4 + 3];
        place = std::max(place, std::abs(world[vertex * 4 + axis] - want) / std::max(1.0, std::abs(want)));
      }
      uint32_t bits;
      std::memcpy(&bits, &world[vertex * 4 + 3], 4);
      uv = std::max(uv, static_cast<double>(std::abs(HalfToFloat(bits & 0xFFFFu) - vertices[vertex * 52 + 4])));
      uv = std::max(uv, static_cast<double>(std::abs(HalfToFloat(bits >> 16) - vertices[vertex * 52 + 5])));
    }
    return std::array<double, 2>{place, uv};
  };
  const auto capture_errors = errors(worlds, reference);
  const auto moved_errors = errors(moved_worlds, reference);
  const auto kept_errors = errors(kept, shadow_worlds);
  const auto control_errors = errors(kept_control, shadow_worlds);
  CHECK(capture_errors[0] < 1e-6 && capture_errors[1] < 1e-3, "campaign trees: the capture %g off the game's world, its UV %g",
        capture_errors[0], capture_errors[1]);
  CHECK(moved_errors[0] > 1e-3, "campaign trees: CONTROL an instance row a metre off = the game's (%g)", moved_errors[0]);
  CHECK(kept_errors[0] < 1e-6 && kept_errors[1] < 1e-3, "campaign trees' shadow pass: kept corners %g off its world, the UV %g",
        kept_errors[0], kept_errors[1]);
  CHECK(control_errors[0] > 1e-3, "campaign trees' shadow pass: CONTROL without COLOR0 = its world (%g)", control_errors[0]);
  std::printf("1 16D14106: the capture's world within %.2g of the game's, its UV within %.2g; CONTROL a row a metre off %.2g. "
              "81696642 kept outside the view: its world within %.2g, the UV within %.2g; CONTROL without COLOR0 %.2g\n",
              capture_errors[0], capture_errors[1], moved_errors[0], kept_errors[0], kept_errors[1], control_errors[0]);
}

// The placed ropes: the capture against the game's 655AB38D from random ropes 10-60 m from the camera.
static void TestPlacedRopes(const std::string& game_folder, const std::string& built_folder) {
  const auto game = ReadFile(game_folder + "\\0x655AB38D.vs_5_0.cso");
  const auto capture = ReadFile(built_folder + "\\rt_capture_rope_rigid.cso");
  CHECK(!game.empty() && !capture.empty(), "placed ropes: the csos");
  if (game.empty() || capture.empty()) return;
  uint32_t seed = 4242;
  const auto random = [&seed](float low, float high) {
    seed = seed * 1664525u + 1013904223u;
    return low + (high - low) * static_cast<float>(seed >> 8) / 16777216.f;
  };
  // 11 inputs x 4 floats (POSITION0, NORMAL0, TEXCOORD0-2, POSITION1-6): the rope's point and direction, its UV, its width
  // (TEXCOORD1.x), the instance's rows (a turn and scale, the place 10-60 m in front of the camera) in POSITION1-3.
  constexpr int VERTICES = 300;
  const std::array<float, 3> forward = {CAMERA[12], CAMERA[13], CAMERA[14]};  // view's third row: the camera's axis (sign either way)
  std::vector<float> vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    const float turn = random(0.f, 6.28f);
    const float scale = random(0.8f, 1.2f);
    const float distance = -random(10.f, 60.f);
    for (int input = 0; input < 11; ++input) {
      std::array<float, 4> value = {random(-1.f, 1.f), random(-1.f, 1.f), random(-1.f, 1.f), random(0.f, 1.f)};
      if (input == 0) value = {random(-2.f, 2.f), random(0.f, 6.f), random(-2.f, 2.f), 1.f};
      if (input == 3) value = {random(0.05f, 0.3f), random(0.f, 1.f), 0.f, 0.f};
      if (input == 5) value = {scale * std::cos(turn), 0.f, scale * std::sin(turn), CAMERA[0] + forward[0] * distance};
      if (input == 6) value = {0.f, scale, 0.f, CAMERA[1] + forward[1] * distance};
      if (input == 7) value = {-scale * std::sin(turn), 0.f, scale * std::cos(turn), CAMERA[2] + forward[2] * distance};
      vertices.insert(vertices.end(), value.begin(), value.end());
    }
  }
  const std::array<ComPtr<ID3D11Buffer>, 1> constants = {ConstantBuffer(CAMERA.data(), sizeof(CAMERA))};
  const auto clips = StreamOut(game, "SV_Position", vertices, constants);
  const auto middles = StreamOut(game, "TEXCOORD", vertices, constants, 5);
  const auto worlds = StreamOut(capture, "TEXCOORD", vertices, constants);
  if (clips.size() != VERTICES * 4 || middles.size() != VERTICES * 4 || worlds.size() != VERTICES * 4) {
    CHECK(false, "placed ropes: stream output sizes");
    return;
  }
  // `world` through view_projection (c9-12, rows acting on column vectors) against the game's clip position, over |w|.
  const auto off = [&](const std::vector<float>& world, int vertex) {
    double worst = 0.0;
    for (int axis = 0; axis < 4; ++axis) {
      double clip = 0.0;
      for (int component = 0; component < 4; ++component) {
        const double value = (component == 3) ? 1.0 : world[vertex * 4 + component];
        clip += value * CAMERA[(9 + axis) * 4 + component];
      }
      worst = std::max(worst, std::abs(clip - clips[vertex * 4 + axis]) / std::abs(clips[vertex * 4 + 3]));
    }
    return worst;
  };
  double capture_worst = 0.0;
  int middles_missing = 0;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    capture_worst = std::max(capture_worst, off(worlds, vertex));
    if (off(middles, vertex) > 1e-4) ++middles_missing;  // outside the tolerance the capture passes
  }
  CHECK(capture_worst < 1e-4, "placed ropes: the capture's world %g of w off the game's clip position", capture_worst);
  CHECK(middles_missing == VERTICES, "placed ropes: CONTROL the rope's middle: only %d of %d miss", middles_missing, VERTICES);
  std::printf("3 655AB38D: the capture's world through view_projection within %.2g of w of the game's SV_Position (%d ropes' "
              "points 10-60 m away); CONTROL the rope's middle: %d of %d miss\n",
              capture_worst, VERTICES, middles_missing, VERTICES);
}

// The campaign's vegetation: the capture against the game's E7792B60 from random trees and plants around the camera.
static void TestCampaignVegetation(const std::string& game_folder, const std::string& built_folder) {
  const auto game = ReadFile(game_folder + "\\0xE7792B60.vs_5_0.cso");
  const auto capture = ReadFile(built_folder + "\\rt_capture_campaign_vegetation.cso");
  CHECK(!game.empty() && !capture.empty(), "vegetation: the csos");
  if (game.empty() || capture.empty()) return;
  uint32_t seed = 12345;
  const auto random = [&seed](float low, float high) {
    seed = seed * 1664525u + 1013904223u;
    return low + (high - low) * static_cast<float>(seed >> 8) / 16777216.f;
  };
  // Each vertex 20 floats: POSITION0 the local offset with the UV's u in w, NORMAL0 with v in w, COLOR0 with the wind weight
  // in w, POSITION1 the place's x / z, POSITION2 its height, turn, scale, fade.
  constexpr int VERTICES = 256;
  std::vector<float> vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    const float values[20] = {random(-3.f, 3.f), random(0.f, 8.f), random(-3.f, 3.f), random(0.f, 1.f),
                              random(-1.f, 1.f), random(-1.f, 1.f), random(-1.f, 1.f), random(0.f, 1.f),
                              random(0.f, 1.f), random(0.f, 1.f), random(0.f, 1.f), random(0.7f, 1.f),
                              CAMERA[0] + random(-150.f, 150.f), CAMERA[2] + random(-150.f, 150.f), 0.f, 0.f,
                              CAMERA[1] - random(20.f, 120.f), random(0.f, 6.28f), random(0.6f, 1.4f), random(0.f, 1.f)};
    vertices.insert(vertices.end(), std::begin(values), std::end(values));
  }
  // Pharaoh's cb_environment: the wind's direction (c0.xy), vector (c0.zw), main vector (c1.xy), gust start (c1.z),
  // multiplier (c1.w), horizontal and vertical strength (c2.xy); the cube map's sun scale (c5.w), the tint scale (c6.x).
  std::array<float, 28> wind = {0.8f, 0.6f, 1.f, 0.5f, 1.f, 0.5f, 0.f, 1.f, 1.3f, 0.9f};
  wind[23] = 1.f;
  wind[24] = 1.f;
  auto camera = CAMERA;
  const std::array<ComPtr<ID3D11Buffer>, 2> now = {ConstantBuffer(camera.data(), sizeof(camera)), ConstantBuffer(wind.data(), sizeof(wind))};
  const auto positions = StreamOut(game, "SV_Position", vertices, now);
  const auto worlds = StreamOut(capture, "TEXCOORD", vertices, now);
  camera[104] += 1.f;  // time_in_sec (c26.x)
  const std::array<ComPtr<ID3D11Buffer>, 2> later = {ConstantBuffer(camera.data(), sizeof(camera)), ConstantBuffer(wind.data(), sizeof(wind))};
  const auto worlds_later = StreamOut(capture, "TEXCOORD", vertices, later);
  if (positions.size() != VERTICES * 4 || worlds.size() != VERTICES * 4 || worlds_later.size() != VERTICES * 4) {
    CHECK(false, "vegetation: stream output sizes");
    return;
  }
  // SV_Position = dot(world, c9 .. c12) in double from the capture's world; the error over the sum of the dot's terms' sizes.
  const auto place_error = [&positions](const std::vector<float>& world) {
    double worst = 0.0;
    for (int vertex = 0; vertex < VERTICES; ++vertex) {
      for (int row = 0; row < 4; ++row) {
        double clip = 0.0;
        double size = 0.0;
        for (int column = 0; column < 4; ++column) {
          const double term = ((column == 3) ? 1.0 : static_cast<double>(world[vertex * 4 + column])) * CAMERA[36 + row * 4 + column];
          clip += term;
          size += std::abs(term);
        }
        worst = std::max(worst, std::abs(clip - positions[vertex * 4 + row]) / std::max(size, 1e-6));
      }
    }
    return worst;
  };
  const double now_error = place_error(worlds);
  const double later_error = place_error(worlds_later);
  double uv_error = 0.0;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    uint32_t bits;
    std::memcpy(&bits, &worlds[vertex * 4 + 3], 4);
    uv_error = std::max(uv_error, static_cast<double>(std::abs(HalfToFloat(bits & 0xFFFFu) - vertices[vertex * 20 + 3])));
    uv_error = std::max(uv_error, static_cast<double>(std::abs(HalfToFloat(bits >> 16) - vertices[vertex * 20 + 7])));
  }
  CHECK(now_error < 1e-6, "vegetation: the capture's world x the camera %g off the game's SV_Position (over its terms' size)", now_error);
  CHECK(uv_error < 1e-3, "vegetation: the UV off by %g", uv_error);
  CHECK(later_error > 1e-4, "vegetation: CONTROL a second later = now (%g): the wind not in play", later_error);
  std::printf("4 E7792B60: the capture's world x the camera within %.2g of the game's SV_Position (over its terms' size), its UV "
              "within %.2g; CONTROL a second later %.2g\n",
              now_error, uv_error, later_error);
}

// The units' random numbers (a linear congruential generator, each test's own from 4242).
struct Random {
  uint32_t seed = 4242;
  float operator()(float low, float high) {
    seed = seed * 1664525u + 1013904223u;
    return low + (high - low) * static_cast<float>(seed >> 8) / 16777216.f;
  }
};

// The units' vertex-stage bindings: the camera, rigid_config (zeros) and the animation map's size at b0-b2; the animation
// map (64 x 64 texels of random rows) at t0 and the selection ids at t1.
struct UnitBindings {
  std::array<ComPtr<ID3D11Buffer>, 3> constants;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> textures;
};

static UnitBindings MakeUnitBindings(Random* random) {
  constexpr UINT MAP = 64;
  std::vector<float> rows(MAP * MAP * 4);
  for (float& value : rows) value = (*random)(-1.f, 1.f);
  std::vector<uint32_t> ids(MAP * MAP);
  for (uint32_t& id : ids) id = static_cast<uint32_t>((*random)(0.f, 65536.f));
  const auto make_view = [](DXGI_FORMAT format, const void* data, UINT pitch) {
    const D3D11_TEXTURE2D_DESC desc = {.Width = MAP, .Height = MAP, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = pitch};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    device->CreateTexture2D(&desc, &initial, &texture);
    if (texture) device->CreateShaderResourceView(texture.Get(), nullptr, &view);
    return view;
  };
  const std::array<float, 20> rigid_config = {};
  const std::array<float, 4> animation_size = {MAP, 1.f / MAP, MAP, 1.f / MAP};
  return {.constants = {ConstantBuffer(CAMERA.data(), sizeof(CAMERA)), ConstantBuffer(rigid_config.data(), sizeof(rigid_config)),
                        ConstantBuffer(animation_size.data(), sizeof(animation_size))},
          .textures = {make_view(DXGI_FORMAT_R32G32B32A32_FLOAT, rows.data(), MAP * 16), make_view(DXGI_FORMAT_R32_UINT, ids.data(), MAP * 4)}};
}

// The worst difference of `values` from `reference` (relative past 1).
static double Worst(const std::vector<float>& reference, const std::vector<float>& values) {
  double error = 0.0;
  for (size_t index = 0; index < reference.size(); ++index) {
    error = std::max(error, std::abs(static_cast<double>(values[index]) - reference[index]) / std::max(1.0, std::abs(static_cast<double>(reference[index]))));
  }
  return error;
}

// The points of `reference` (4 floats each) the skinning placed off the origin (the animation map's rows in play).
static int OffOrigin(const std::vector<float>& reference) {
  int moved = 0;
  for (size_t point = 0; point + 3 < reference.size(); point += 4) {
    if (std::abs(reference[point]) + std::abs(reference[point + 1]) + std::abs(reference[point + 2]) > 1e-3) ++moved;
  }
  return moved;
}

// The fur-clad units: the capture against the game's 621E86DD, both streamed out from the same random skinned vertices.
static void TestFur(const std::string& game_folder, const std::string& built_folder) {
  const auto game = ReadFile(game_folder + "\\0x621E86DD.vs_5_0.cso");
  const auto capture = ReadFile(built_folder + "\\rt_capture_unit_4_bones_fur.cso");
  const auto control = ReadFile(built_folder + "\\rt_capture_unit_4_bones.cso");
  CHECK(!game.empty() && !capture.empty() && !control.empty(), "fur: the csos");
  if (game.empty() || capture.empty() || control.empty()) return;
  Random random;
  const UnitBindings bindings = MakeUnitBindings(&random);
  // Each vertex 15 inputs x 4 floats: POSITION0 the bind pose, POSITION1 four bone indices (x 255), POSITION2 their weights,
  // POSITION3 a normal with the fur's length in w, the rest (tangents, the instance's rows, the base row in TEXCOORD3.x) random.
  constexpr int VERTICES = 256;
  std::vector<float> vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    for (int input = 0; input < 15; ++input) {
      for (int component = 0; component < 4; ++component) {
        float value = random(-1.f, 1.f);
        if (input == 1) value = std::floor(random(0.f, 6.f)) / 255.f;
        if (input == 2) value = random(0.f, 0.33f);
        if (input == 3 && component == 3) value = random(0.f, 1.f);
        if (input == 10 && component == 0) value = std::floor(random(0.f, 40.f));
        vertices.push_back(value);
      }
    }
  }
  const auto reference = StreamOut(game, "TEXCOORD", vertices, bindings.constants, 0, bindings.textures);
  const auto worlds = StreamOut(capture, "TEXCOORD", vertices, bindings.constants, 0, bindings.textures);
  const auto unswayed = StreamOut(control, "TEXCOORD", vertices, bindings.constants, 0, bindings.textures);
  if (reference.size() != VERTICES * 4 || worlds.size() != VERTICES * 4 || unswayed.size() != VERTICES * 4) {
    CHECK(false, "fur: stream output sizes");
    return;
  }
  const int moved = OffOrigin(reference);
  const double error = Worst(reference, worlds);
  const double control_error = Worst(reference, unswayed);
  CHECK(moved > VERTICES / 2, "fur: only %d of %d vertices skinned off the origin", moved, VERTICES);
  CHECK(error < 1e-6, "fur: the capture %g off the game's world position", error);
  CHECK(control_error > 1e-3, "fur: CONTROL the 4-bone capture without the sway = the game's (%g)", control_error);
  std::printf("5 621E86DD: the capture's world within %.2g of the game's (%d of %d vertices skinned off the origin); CONTROL "
              "4A716936's capture %.2g\n",
              error, moved, VERTICES, control_error);
}

// The 8-bone models: the capture and the motion shader against the game's F81881EB, all streamed out from the same random
// 8-bone vertices.
static void TestEightBones(const std::string& game_folder, const std::string& built_folder) {
  const auto game = ReadFile(game_folder + "\\0xF81881EB.vs_5_0.cso");
  const auto capture = ReadFile(built_folder + "\\rt_capture_unit_8_bones.cso");
  const auto motion = ReadFile(built_folder + "\\motion_unit_weightedcommon_f818.cso");
  CHECK(!game.empty() && !capture.empty() && !motion.empty(), "8 bones: the csos");
  if (game.empty() || capture.empty() || motion.empty()) return;
  Random random;
  const UnitBindings bindings = MakeUnitBindings(&random);
  // Each vertex 16 inputs x 4 floats: POSITION1 eight bone indices (0-5) two bytes to a uint, POSITION2-3 the weights (the
  // shader drops those under 1/256 and takes the first bone's index for theirs), TEXCOORD3.x the base row, the rest random.
  constexpr int VERTICES = 256;
  constexpr int INPUTS = 16;
  std::vector<float> vertices;
  int dropped = 0;  // weights under 1/256 (POSITION2.xyz, POSITION3.xyz)
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    for (int input = 0; input < INPUTS; ++input) {
      for (int component = 0; component < 4; ++component) {
        float value = random(-1.f, 1.f);
        if (input == 1) value = std::bit_cast<float>(static_cast<uint32_t>(random(0.f, 6.f)) | (static_cast<uint32_t>(random(0.f, 6.f)) << 8));
        if (input == 2 || input == 3) value = random(0.f, 0.12f);
        if ((input == 2 || input == 3) && component < 3 && value < 1.f / 256.f) ++dropped;
        if (input == 11 && component == 0) value = std::floor(random(0.f, 40.f));
        vertices.push_back(value);
      }
    }
  }
  // CONTROL: every bone byte 0 (what fxc made of the translation with float registers).
  std::vector<float> boneless = vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) std::fill_n(boneless.begin() + (vertex * INPUTS + 1) * 4, 4, 0.f);
  const auto reference = StreamOut(game, "TEXCOORD", vertices, bindings.constants, 0, bindings.textures);
  const auto worlds = StreamOut(capture, "TEXCOORD", vertices, bindings.constants, 0, bindings.textures);
  const auto positions = StreamOut(game, "SV_Position", vertices, bindings.constants, 0, bindings.textures);
  const auto motion_positions = StreamOut(motion, "SV_Position", vertices, bindings.constants, 0, bindings.textures);
  const auto boneless_reference = StreamOut(game, "TEXCOORD", boneless, bindings.constants, 0, bindings.textures);
  if (reference.size() != VERTICES * 4 || worlds.size() != VERTICES * 4 || positions.size() != VERTICES * 4
      || motion_positions.size() != VERTICES * 4 || boneless_reference.size() != VERTICES * 4) {
    CHECK(false, "8 bones: stream output sizes");
    return;
  }
  const int moved = OffOrigin(reference);
  const double error = Worst(reference, worlds);
  const double motion_error = Worst(positions, motion_positions);
  const double control_error = Worst(reference, boneless_reference);
  CHECK(moved > VERTICES / 2, "8 bones: only %d of %d vertices skinned off the origin", moved, VERTICES);
  CHECK(dropped > 0, "8 bones: no weight under 1/256 (the dropped bones' index not in play)");
  CHECK(error < 1e-6, "8 bones: the capture %g off the game's world position", error);
  CHECK(motion_error < 1e-5, "8 bones: the motion shader's SV_Position %g off the game's", motion_error);
  CHECK(control_error > 1e-3, "8 bones: CONTROL every bone byte 0 = the game's (%g)", control_error);
  std::printf("6 F81881EB: the capture's world within %.2g of the game's, the motion shader's SV_Position within %.2g (%d of %d "
              "vertices skinned off the origin, %d weights dropped); CONTROL every bone byte 0 %.2g\n",
              error, motion_error, moved, VERTICES, dropped, control_error);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: pharaoh.exe <the game's csos> <the add-on's built csos> [Pharaoh.exe]\n");
    return 2;
  }
  TestShadowCascades((argc > 3) ? argv[3] : R"(C:\Program Files (x86)\Steam\steamapps\common\Total War PHARAOH DYNASTIES\Pharaoh.exe)");
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("no WARP device\n");
    return 1;
  }
  TestCampaignTrees(argv[1], argv[2]);
  TestPlacedRopes(argv[1], argv[2]);
  TestCampaignVegetation(argv[1], argv[2]);
  TestFur(argv[1], argv[2]);
  TestEightBones(argv[1], argv[2]);
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
