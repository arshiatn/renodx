// GT-VBAO's Exact Depth Samples (twattila/bitmask.hlsli BitmaskPlaneDepth: SI.ssao_exact_depth, R.U.S.E.'s options.y)
// in every shader that has it, on WARP: the built shaders against the builds before the change.
// Per shader: its bindings from its source's reflection (compiled as the build compiles it; its code checked against
// the built one's), every texture a smooth plane (four texels on one plane: the exact path's case; channel 0 the depth),
// the other constant buffers an invertible pattern (1 on each register's diagonal, 0.25 elsewhere), the injection's
// GT-VBAO on. Off (0): every output bit the build before's. CONTROL on (1): its outputs must differ (the row reaches the
// shader and its sample path runs on this input). Printed beside: the first output's channel 0, before and on.
// tests.exe <repository> <before root> (the before root: a copy of each add-on's embed folder, <root>\<game>\*.cso)
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WIDTH = 160;
constexpr UINT HEIGHT = 90;

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;

struct Job {
  const char* game;       // its folder under src/games and its embed folder (<game>.include)
  bool x86;               // built in build32
  const char* source;     // under src/games/<game>
  const char* cso;        // the embed's name
  const char* toggle;     // the switched variable ("name" or "name+bytes")
  const char* overrides;  // "name=v[,v...]" separated by spaces (variables or struct members; "name+bytes" after one)
};

// Every injection's GT-VBAO on, where the game has the field; then each job's own.
constexpr const char* DEFAULTS =
    "ssao_mode=1 ao_method=1 ssao_strength=1 ssao_radius=1 ssao_thickness=0.1 ssao_quality=1 ssao_directions=1 ssao_scale=1";
// R.U.S.E.'s ground march (vbao_settings): P00 / P11 / A / B as a 60-degree camera (depth = A + B / z, z 6-23 over the
// planes), the radius 2 and thickness 0.5 world units, noise frame 0; options.x 1 (this march's occlusion).
// Attila's camera_VS_PS / ssao_resolve_PS (bitmask_ssao.hlsli): the 160 x 90 view, a 60-degree field of view and
// inv_projection's z / w columns (c19 / c20) as R.U.S.E.'s A / B.
constexpr const char* ATTILA =
    "g_screen_size=160,90,0.00625,0.0111111 g_viewport_dimensions=160,90,0.00625,0.0111111 g_vpos_texel_offset=0"
    " g_AOResolution=160,90 g_VerticalFieldOfView=1.0472 inv_projection+32=0,0,0,-5,0,0,1,-1.00005";
// Shogun 2's sweep camera (ao_bitmask_collect): the view, inv_projection's four columns (x / y the field of view's).
constexpr const char* SHOGUN2 =
    "g_viewport_dimensions=160,90,0.00625,0.0111111 inv_projection=-5.13,0,0,0,0,-2.887,0,0,0,0,0,-5,0,0,1,-1.00005";
constexpr const char* RUSE =
    "projection=1,1.7,1.00005,-5 size=160,90,0.00625,0.0111111 range=2,0.5,0,1 options=1,0,0,0";

constexpr std::array<Job, 31> JOBS = {{
    {"twattila", true, "ao_bitmask.ps_5_0.hlsl", "ao_bitmask", "ssao_exact_depth", ATTILA},
    {"twthronesofbritannia", true, "ao_bitmask.ps_5_0.hlsl", "ao_bitmask", "ssao_exact_depth", ATTILA},
    {"twrome2", true, "ao_bitmask01.ps_5_0.hlsl", "ao_bitmask01", "ssao_exact_depth", ATTILA},
    {"twtroy", false, "ao_bitmask.ps_5_0.hlsl", "ao_bitmask", "ssao_exact_depth", ""},
    {"twtroy", false, "ao_bitmask_blur.cs_5_0.hlsl", "ao_bitmask_blur", "ssao_exact_depth", ""},
    {"twpharaohdynasties", false, "ao_bitmask.ps_5_0.hlsl", "ao_bitmask", "ssao_exact_depth", ""},
    {"twpharaohdynasties", false, "ao_bitmask_blur.cs_5_0.hlsl", "ao_bitmask_blur", "ssao_exact_depth", ""},
    {"twwarhammer", false, "ao_bitmask_blur.cs_5_0.hlsl", "ao_bitmask_blur", "ssao_exact_depth", ""},
    {"twwarhammer2", false, "ao_bitmask_collect.cs_5_0.hlsl", "ao_bitmask_collect", "ssao_exact_depth", ""},
    {"twwarhammer3", false, "ao_bitmask_collect01.cs_5_0.hlsl", "ao_bitmask_collect01", "ssao_exact_depth", ""},
    {"twwarhammer3", false, "ao_bitmask_collect02.cs_5_0.hlsl", "ao_bitmask_collect02", "ssao_exact_depth", ""},
    {"twshogun2completeedition", false, "ao_bitmask_collect.cs_5_0.hlsl", "ao_bitmask_collect", "ssao_exact_depth", SHOGUN2},
    {"rysesonofrome", false, "shaders/ssdo00_0xD1106E8B.ps_5_0.hlsl", "0xD1106E8B", "ssao_exact_depth", ""},
    {"rysesonofrome", false, "shaders/ssdo01_0xC17645D2.ps_5_0.hlsl", "0xC17645D2", "ssao_exact_depth", ""},
    {"rysesonofrome", false, "shaders/ssdo02_0xA65B60AF.ps_5_0.hlsl", "0xA65B60AF", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao00_0xE243580F.cs_5_0.hlsl", "0xE243580F", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao01_0x312321CE.cs_5_0.hlsl", "0x312321CE", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao02_0x14A01750.cs_5_0.hlsl", "0x14A01750", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao03_0x2F09AD40.cs_5_0.hlsl", "0x2F09AD40", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao04_0x8AA7DE1A.cs_5_0.hlsl", "0x8AA7DE1A", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao05_0x923152AB.cs_5_0.hlsl", "0x923152AB", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao06_0x506D32BE.ps_5_0.hlsl", "0x506D32BE", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao07_0xB2792BB9.ps_5_0.hlsl", "0xB2792BB9", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao08_0x3A849493.ps_5_0.hlsl", "0x3A849493", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao09_0xA081DC06.ps_5_0.hlsl", "0xA081DC06", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao10_0xE01AB3CF.ps_5_0.hlsl", "0xE01AB3CF", "ssao_exact_depth", ""},
    {"talesofarise", false, "shaders/ao11_0xF3B29224.ps_5_0.hlsl", "0xF3B29224", "ssao_exact_depth", ""},
    {"expeditionsrome", false, "shaders/ao00_0x31A1C68C.ps_5_0.hlsl", "0x31A1C68C", "ssao_exact_depth", ""},
    {"expeditionsrome", false, "shaders/ao01_0xBD381E21.ps_5_0.hlsl", "0xBD381E21", "ssao_exact_depth", ""},
    {"ruse", false, "ground_vbao.cs_5_0.hlsl", "ground_vbao", "options+4", RUSE},
    {"ruse", false, "ground_bounce.cs_5_0.hlsl", "ground_bounce", "options+4", RUSE},
}};

std::vector<char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Includes relative to the including file, as fxc (the standard handler takes the first file's folder for all).
class Includes final : public ID3DInclude {
 public:
  explicit Includes(std::string folder) : root_(std::move(folder)) {}

  HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*type*/, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override {
    std::string folder = root_;
    for (const auto& [text, path] : opened_) {
      if (text->data() == parent) folder = path;
    }
    const std::string path = folder + "\\" + name;
    auto text = std::make_unique<std::vector<char>>(ReadFile(path));
    if (text->empty()) return E_FAIL;
    *data = text->data();
    *bytes = static_cast<UINT>(text->size());
    opened_.emplace_back(std::move(text), path.substr(0, path.find_last_of("\\/")));
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID /*data*/) override { return S_OK; }

 private:
  std::string root_;
  std::vector<std::pair<std::unique_ptr<std::vector<char>>, std::string>> opened_;
};

// A DXBC container's chunk (SHEX / SHDR: the code), empty if none.
std::string Chunk(const void* data, size_t size, const char* fourcc) {
  const auto* bytes = static_cast<const char*>(data);
  if (size < 32 || std::memcmp(bytes, "DXBC", 4) != 0) return {};
  uint32_t count = 0;
  std::memcpy(&count, bytes + 28, 4);
  for (uint32_t i = 0; i < count && 32 + (i * 4) + 4 <= size; ++i) {
    uint32_t offset = 0;
    std::memcpy(&offset, bytes + 32 + (i * 4), 4);
    if (offset + 8 > size) break;
    uint32_t length = 0;
    std::memcpy(&length, bytes + offset + 4, 4);
    if (std::memcmp(bytes + offset, fourcc, 4) == 0 && offset + 8 + length <= size) return {bytes + offset + 8, length};
  }
  return {};
}

std::string Code(const void* data, size_t size) {
  std::string code = Chunk(data, size, "SHEX");
  return code.empty() ? Chunk(data, size, "SHDR") : code;
}

// A constant's byte offset in its buffer: a variable or a struct member (any depth) by name.
std::optional<UINT> MemberOffset(ID3D11ShaderReflectionType* type, const std::string& name, UINT base) {
  D3D11_SHADER_TYPE_DESC desc = {};
  type->GetDesc(&desc);
  for (UINT i = 0; i < desc.Members; ++i) {
    ID3D11ShaderReflectionType* member = type->GetMemberTypeByIndex(i);
    D3D11_SHADER_TYPE_DESC member_desc = {};
    member->GetDesc(&member_desc);
    if (name == type->GetMemberTypeName(i)) return base + member_desc.Offset;
    if (auto found = MemberOffset(member, name, base + member_desc.Offset)) return found;
  }
  return std::nullopt;
}

struct Constants {
  std::string name;
  UINT slot = 0;
  std::vector<float> values;
  bool injection = false;  // has a GT-VBAO field (zeros but the fields set), else the pattern
};

// `name[+bytes]` in every buffer: the buffer's index and the float's index, if found.
std::optional<std::pair<size_t, size_t>> Find(ID3D11ShaderReflection* reflection, const std::vector<Constants>& buffers, std::string name) {
  UINT extra = 0;
  if (const size_t plus = name.find('+'); plus != std::string::npos) {
    extra = static_cast<UINT>(std::atoi(name.c_str() + plus + 1));
    name.resize(plus);
  }
  for (size_t b = 0; b < buffers.size(); ++b) {
    ID3D11ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(buffers[b].name.c_str());
    D3D11_SHADER_BUFFER_DESC desc = {};
    if (FAILED(buffer->GetDesc(&desc))) continue;
    for (UINT v = 0; v < desc.Variables; ++v) {
      ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex(v);
      D3D11_SHADER_VARIABLE_DESC variable_desc = {};
      variable->GetDesc(&variable_desc);
      std::optional<UINT> offset;
      if (name == variable_desc.Name) {
        offset = variable_desc.StartOffset;
      } else if (auto member = MemberOffset(variable->GetType(), name, variable_desc.StartOffset)) {
        offset = member;
      }
      if (offset) return std::pair{b, static_cast<size_t>((*offset + extra) / 4)};
    }
  }
  return std::nullopt;
}

// "name=v[,v...] ..." into the buffers; false if a required name isn't found.
bool Apply(ID3D11ShaderReflection* reflection, std::vector<Constants>* buffers, const char* list, bool required) {
  std::istringstream items(list);
  std::string item;
  while (items >> item) {
    const size_t equals = item.find('=');
    const auto found = Find(reflection, *buffers, item.substr(0, equals));
    if (!found) {
      if (required) {
        std::printf("  no constant %s\n", item.c_str());
        return false;
      }
      continue;
    }
    auto& values = (*buffers)[found->first].values;
    std::istringstream numbers(item.substr(equals + 1));
    std::string number;
    for (size_t i = found->second; std::getline(numbers, number, ',') && i < values.size(); ++i) values[i] = std::strtof(number.c_str(), nullptr);
  }
  return true;
}

struct Output {
  ComPtr<ID3D11Resource> resource;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D11UnorderedAccessView> uav;
  UINT slot = 0;
  UINT texel_bytes = 16;
  bool buffer = false;
  UINT buffer_bytes = 0;
};

std::vector<uint32_t> ReadBack(const Output& output) {
  ComPtr<ID3D11Resource> staging;
  if (output.buffer) {
    const D3D11_BUFFER_DESC desc = {output.buffer_bytes, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ComPtr<ID3D11Buffer> made;
    device->CreateBuffer(&desc, nullptr, &made);
    staging = made;
  } else {
    D3D11_TEXTURE2D_DESC desc = {};
    static_cast<ID3D11Texture2D*>(output.resource.Get())->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING, desc.BindFlags = 0, desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> made;
    device->CreateTexture2D(&desc, nullptr, &made);
    staging = made;
  }
  context->CopyResource(staging.Get(), output.resource.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<uint32_t> words;
  if (output.buffer) {
    words.resize(output.buffer_bytes / 4);
    std::memcpy(words.data(), mapped.pData, output.buffer_bytes);
  } else {
    const UINT row = WIDTH * output.texel_bytes;
    words.resize(size_t{row} / 4 * HEIGHT);
    for (UINT y = 0; y < HEIGHT; ++y) std::memcpy(words.data() + (size_t{y} * row / 4), static_cast<const char*>(mapped.pData) + (size_t{y} * mapped.RowPitch), row);
  }
  context->Unmap(staging.Get(), 0);
  return words;
}

DXGI_FORMAT Format(D3D_RESOURCE_RETURN_TYPE type, UINT components) {
  const bool uint = type == D3D_RETURN_TYPE_UINT;
  const bool sint = type == D3D_RETURN_TYPE_SINT;
  if (components == 1) return uint ? DXGI_FORMAT_R32_UINT : (sint ? DXGI_FORMAT_R32_SINT : DXGI_FORMAT_R32_FLOAT);
  if (components == 2) return uint ? DXGI_FORMAT_R32G32_UINT : (sint ? DXGI_FORMAT_R32G32_SINT : DXGI_FORMAT_R32G32_FLOAT);
  return uint ? DXGI_FORMAT_R32G32B32A32_UINT : (sint ? DXGI_FORMAT_R32G32B32A32_SINT : DXGI_FORMAT_R32G32B32A32_FLOAT);
}

struct Result {
  bool ran = false;
  std::vector<std::vector<uint32_t>> outputs;
};

// One shader on the bindings `reflection` names, the injection's `toggle` at `value`.
Result Run(const std::vector<char>& code, bool compute, ID3D11ShaderReflection* reflection, const std::vector<Constants>& base_buffers,
           const std::pair<size_t, size_t>& toggle, float value) {
  Result result;
  std::vector<Constants> buffers = base_buffers;
  buffers[toggle.first].values[toggle.second] = value;
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  std::vector<ComPtr<ID3D11Buffer>> constant_buffers;
  std::vector<ComPtr<ID3D11ShaderResourceView>> views;
  std::vector<ComPtr<ID3D11SamplerState>> samplers;
  std::vector<Output> outputs;
  std::array<ID3D11Buffer*, 14> cb_slots = {};
  std::array<ID3D11ShaderResourceView*, 128> srv_slots = {};
  std::array<ID3D11SamplerState*, 16> sampler_slots = {};
  UINT cb_count = 0, srv_count = 0, sampler_count = 0;
  for (const auto& constants : buffers) {
    const D3D11_BUFFER_DESC desc = {static_cast<UINT>(constants.values.size() * 4 + 15) & ~15u, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    std::vector<float> bytes(desc.ByteWidth / 4);
    std::ranges::copy(constants.values, bytes.begin());
    const D3D11_SUBRESOURCE_DATA init = {bytes.data(), 0, 0};
    ComPtr<ID3D11Buffer> buffer;
    if (FAILED(device->CreateBuffer(&desc, &init, &buffer))) return result;
    cb_slots[constants.slot] = buffer.Get();
    cb_count = std::max(cb_count, constants.slot + 1);
    constant_buffers.push_back(buffer);
  }
  for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDesc(i, &bind);
    const UINT components = ((bind.uFlags & D3D_SIF_TEXTURE_COMPONENTS) >> 2) + 1;
    if (bind.Type == D3D_SIT_SAMPLER) {
      // Point sampling: the texels as they are (the same for both builds).
      const D3D11_SAMPLER_DESC desc = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                       D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1, D3D11_COMPARISON_NEVER, {}, 0, D3D11_FLOAT32_MAX};
      ComPtr<ID3D11SamplerState> sampler;
      device->CreateSamplerState(&desc, &sampler);
      sampler_slots[bind.BindPoint] = sampler.Get();
      sampler_count = std::max(sampler_count, bind.BindPoint + 1);
      samplers.push_back(sampler);
    } else if (bind.Type == D3D_SIT_TEXTURE) {
      if (bind.Dimension != D3D_SRV_DIMENSION_TEXTURE2D && bind.Dimension != D3D_SRV_DIMENSION_TEXTURE2DARRAY) {
        std::printf("  t%u %s: dimension %d not made\n", bind.BindPoint, bind.Name, bind.Dimension);
        return result;
      }
      // The plane: channel 0 0.2 + 0.05 u + 0.5 v (a depth), the others other planes.
      const bool integer = bind.ReturnType == D3D_RETURN_TYPE_UINT || bind.ReturnType == D3D_RETURN_TYPE_SINT;
      std::vector<float> texels(size_t{WIDTH} * HEIGHT * 4);
      for (UINT y = 0; y < HEIGHT; ++y) {
        for (UINT x = 0; x < WIDTH; ++x) {
          const float u = (x + 0.5f) / WIDTH;
          const float v = (y + 0.5f) / HEIGHT;
          float* texel = texels.data() + ((size_t{y} * WIDTH + x) * 4);
          texel[0] = 0.2f + (0.05f * u) + (0.5f * v), texel[1] = 0.5f + (0.3f * u), texel[2] = 0.5f + (0.3f * v), texel[3] = 0.75f;
          if (integer) {
            const std::array<uint32_t, 4> words = {x, y, 1u, 0u};
            std::memcpy(texel, words.data(), 16);
          }
        }
      }
      const DXGI_FORMAT format = Format(bind.ReturnType, 4);
      const D3D11_TEXTURE2D_DESC desc = {WIDTH, HEIGHT, 1, 1, format, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
      const D3D11_SUBRESOURCE_DATA init = {texels.data(), WIDTH * 16, 0};
      ComPtr<ID3D11Texture2D> texture;
      ComPtr<ID3D11ShaderResourceView> view;
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
      view_desc.Format = format;
      if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY) {
        view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        view_desc.Texture2DArray = {0, 1, 0, 1};
      } else {
        view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view_desc.Texture2D = {0, 1};
      }
      if (FAILED(device->CreateTexture2D(&desc, &init, &texture)) || FAILED(device->CreateShaderResourceView(texture.Get(), &view_desc, &view))) return result;
      srv_slots[bind.BindPoint] = view.Get();
      srv_count = std::max(srv_count, bind.BindPoint + 1);
      views.push_back(view);
    } else if (bind.Type == D3D_SIT_STRUCTURED || bind.Type == D3D_SIT_BYTEADDRESS) {
      const UINT stride = bind.Type == D3D_SIT_STRUCTURED ? std::max(bind.NumSamples, 4u) : 4u;
      const UINT count = 4096;
      std::vector<float> data(size_t{stride} * count / 4, 0.5f);
      D3D11_BUFFER_DESC desc = {stride * count, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE,
                                0, static_cast<UINT>(bind.Type == D3D_SIT_STRUCTURED ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS), stride};
      const D3D11_SUBRESOURCE_DATA init = {data.data(), 0, 0};
      ComPtr<ID3D11Buffer> buffer;
      ComPtr<ID3D11ShaderResourceView> view;
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
      if (bind.Type == D3D_SIT_STRUCTURED) {
        view_desc.Format = DXGI_FORMAT_UNKNOWN, view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER, view_desc.Buffer = {{0}, {count}};
      } else {
        view_desc.Format = DXGI_FORMAT_R32_TYPELESS, view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        view_desc.BufferEx = {0, count, D3D11_BUFFEREX_SRV_FLAG_RAW};
      }
      if (FAILED(device->CreateBuffer(&desc, &init, &buffer)) || FAILED(device->CreateShaderResourceView(buffer.Get(), &view_desc, &view))) return result;
      srv_slots[bind.BindPoint] = view.Get();
      srv_count = std::max(srv_count, bind.BindPoint + 1);
      views.push_back(view);
    } else if (bind.Type == D3D_SIT_UAV_RWTYPED) {
      if (bind.Dimension != D3D_SRV_DIMENSION_TEXTURE2D) {
        std::printf("  u%u %s: dimension %d not made\n", bind.BindPoint, bind.Name, bind.Dimension);
        return result;
      }
      Output output;
      const UINT made_components = components == 3 ? 4 : components;
      output.texel_bytes = made_components * 4;
      output.slot = bind.BindPoint;
      const D3D11_TEXTURE2D_DESC desc = {WIDTH, HEIGHT, 1, 1, Format(bind.ReturnType, made_components), {1, 0}, D3D11_USAGE_DEFAULT,
                                         D3D11_BIND_UNORDERED_ACCESS, 0, 0};
      ComPtr<ID3D11Texture2D> texture;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) || FAILED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &output.uav))) return result;
      output.resource = texture;
      outputs.push_back(output);
    } else if (bind.Type == D3D_SIT_UAV_RWSTRUCTURED || bind.Type == D3D_SIT_UAV_RWBYTEADDRESS) {
      Output output;
      const bool structured = bind.Type == D3D_SIT_UAV_RWSTRUCTURED;
      const UINT stride = structured ? std::max(bind.NumSamples, 4u) : 4u;
      const UINT count = 65536;
      output.buffer = true;
      output.buffer_bytes = stride * count;
      output.slot = bind.BindPoint;
      const D3D11_BUFFER_DESC desc = {output.buffer_bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                      static_cast<UINT>(structured ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS), stride};
      ComPtr<ID3D11Buffer> buffer;
      D3D11_UNORDERED_ACCESS_VIEW_DESC view_desc = {};
      view_desc.Format = structured ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
      view_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      view_desc.Buffer = {0, count, structured ? 0u : static_cast<UINT>(D3D11_BUFFER_UAV_FLAG_RAW)};
      if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)) || FAILED(device->CreateUnorderedAccessView(buffer.Get(), &view_desc, &output.uav))) return result;
      output.resource = buffer;
      outputs.push_back(output);
    } else if (bind.Type != D3D_SIT_CBUFFER) {
      std::printf("  %s: binding type %d not made\n", bind.Name, bind.Type);
      return result;
    }
  }
  ComPtr<ID3D11ComputeShader> cs;
  ComPtr<ID3D11PixelShader> ps;
  ComPtr<ID3D11VertexShader> vs;
  std::vector<Output> targets;
  if (compute) {
    if (FAILED(device->CreateComputeShader(code.data(), code.size(), nullptr, &cs))) return result;
  } else {
    if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &ps))) return result;
    // A full-screen triangle with the pixel shader's inputs (uv in each, in the order and sizes it reads them).
    std::string source = "struct O { float4 position : SV_Position;";
    std::string body;
    for (UINT i = 0; i < shader_desc.InputParameters; ++i) {
      D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
      reflection->GetInputParameterDesc(i, &parameter);
      if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
      const int count = std::popcount(static_cast<unsigned>(parameter.Mask));
      const char* type = parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32 ? "uint" : (parameter.ComponentType == D3D_REGISTER_COMPONENT_SINT32 ? "int" : "float");
      const std::string name = "v" + std::to_string(i);
      source += std::string(" ") + type + (count > 1 ? std::to_string(count) : "") + " " + name + " : " + parameter.SemanticName + std::to_string(parameter.SemanticIndex) + ";";
      body += " o." + name + " = " + (std::strcmp(type, "float") == 0 ? "" : "0 * ") + "(" + type + (count > 1 ? std::to_string(count) : "") + ")float4(uv, 1 - uv);";
    }
    source += " }; O main(uint id : SV_VertexID) { O o; float2 uv = float2((id << 1) & 2, id & 2);"
              " o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);" + body + " return o; }";
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(source.data(), source.size(), "vs", nullptr, nullptr, "main", "vs_5_0", 0, 0, &blob, &errors))
        || FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs))) {
      std::printf("  vertex shader: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
      return result;
    }
    for (UINT i = 0; i < shader_desc.OutputParameters; ++i) {
      D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
      reflection->GetOutputParameterDesc(i, &parameter);
      if (parameter.SystemValueType != D3D_NAME_TARGET) {
        std::printf("  output %s not made\n", parameter.SemanticName);
        return result;
      }
      Output target;
      target.slot = parameter.SemanticIndex;
      const D3D_RESOURCE_RETURN_TYPE type = parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32
                                                ? D3D_RETURN_TYPE_UINT
                                                : (parameter.ComponentType == D3D_REGISTER_COMPONENT_SINT32 ? D3D_RETURN_TYPE_SINT : D3D_RETURN_TYPE_FLOAT);
      const D3D11_TEXTURE2D_DESC desc = {WIDTH, HEIGHT, 1, 1, Format(type, 4), {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
      ComPtr<ID3D11Texture2D> texture;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) || FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &target.rtv))) return result;
      target.resource = texture;
      targets.push_back(target);
    }
  }
  for (const auto& output : outputs) {
    constexpr std::array<UINT, 4> ZERO = {};
    context->ClearUnorderedAccessViewUint(output.uav.Get(), ZERO.data());
  }
  if (compute) {
    context->CSSetConstantBuffers(0, cb_count, cb_slots.data());
    context->CSSetShaderResources(0, srv_count, srv_slots.data());
    context->CSSetSamplers(0, sampler_count, sampler_slots.data());
    for (const auto& output : outputs) context->CSSetUnorderedAccessViews(output.slot, 1, output.uav.GetAddressOf(), nullptr);
    context->CSSetShader(cs.Get(), nullptr, 0);
    UINT x = 1, y = 1, z = 1;
    reflection->GetThreadGroupSize(&x, &y, &z);
    context->Dispatch((WIDTH + x - 1) / x, (HEIGHT + y - 1) / y, 1);
  } else {
    context->PSSetConstantBuffers(0, cb_count, cb_slots.data());
    context->PSSetShaderResources(0, srv_count, srv_slots.data());
    context->PSSetSamplers(0, sampler_count, sampler_slots.data());
    std::array<ID3D11RenderTargetView*, 8> rtvs = {};
    UINT rtv_count = 0;
    for (const auto& target : targets) {
      constexpr std::array<float, 4> CLEAR = {};
      context->ClearRenderTargetView(target.rtv.Get(), CLEAR.data());
      rtvs[target.slot] = target.rtv.Get();
      rtv_count = std::max(rtv_count, target.slot + 1);
    }
    std::array<ID3D11UnorderedAccessView*, 8> uavs = {};
    UINT uav_end = rtv_count;
    for (const auto& output : outputs) {
      uavs[output.slot] = output.uav.Get();
      uav_end = std::max(uav_end, output.slot + 1);
    }
    context->OMSetRenderTargetsAndUnorderedAccessViews(rtv_count, rtvs.data(), nullptr, rtv_count, uav_end - rtv_count, uavs.data() + rtv_count, nullptr);
    const D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->Draw(3, 0);
  }
  context->ClearState();
  for (const auto& target : targets) result.outputs.push_back(ReadBack(target));
  for (const auto& output : outputs) result.outputs.push_back(ReadBack(output));
  result.ran = true;
  return result;
}

size_t Differing(const Result& left, const Result& right) {
  size_t count = 0;
  for (size_t o = 0; o < left.outputs.size() && o < right.outputs.size(); ++o) {
    for (size_t i = 0; i < left.outputs[o].size(); ++i) count += left.outputs[o][i] != right.outputs[o][i] ? 1 : 0;
  }
  return count;
}

// The first output's channel 0 (as floats) over the texels: mean, and the share below 0.999.
std::pair<double, double> Channel0(const Result& result) {
  if (result.outputs.empty()) return {0, 0};
  const auto& words = result.outputs[0];
  const size_t stride = words.size() / (size_t{WIDTH} * HEIGHT);
  double sum = 0;
  size_t under = 0, count = 0;
  for (size_t i = 0; i + stride <= words.size(); i += std::max<size_t>(stride, 1)) {
    float value = 0;
    std::memcpy(&value, &words[i], 4);
    if (!std::isfinite(value)) continue;
    sum += value;
    under += value < 0.999f ? 1 : 0;
    ++count;
  }
  return {sum / std::max<size_t>(count, 1), static_cast<double>(under) / std::max<size_t>(count, 1)};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::printf("usage: tests.exe <repository> <before root>\n");
    return 2;
  }
  const std::string repository = argv[1];
  const std::string before_root = argv[2];
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 3;
  int failures = 0;
  std::printf("%-26s %-22s %6s %9s %9s | %9s %8s %9s %8s\n", "game", "shader", "code", "off diff", "on diff", "before", "<0.999", "on", "<0.999");
  for (const auto& job : JOBS) {
    const std::string source = repository + "\\src\\games\\" + job.game + "\\" + job.source;
    const std::string built = repository + (job.x86 ? "\\build32\\" : "\\build\\") + job.game + ".include\\embed\\" + job.cso + ".cso";
    const std::string previous = before_root + "\\" + job.game + "\\" + job.cso + ".cso";
    const bool compute = std::strstr(job.source, ".cs_5_0.") != nullptr;
    const std::wstring wide(source.begin(), source.end());
    const std::array<D3D_SHADER_MACRO, 3> macros = {{{"__SHADER_TARGET_MAJOR", "5"}, {"__SHADER_TARGET_MINOR", "0"}, {nullptr, nullptr}}};
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    Includes includes(source.substr(0, source.find_last_of("\\/")));
    if (FAILED(D3DCompileFromFile(wide.c_str(), macros.data(), &includes, "main", compute ? "cs_5_0" : "ps_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, 0, &blob, &errors))) {
      std::printf("%-26s %-22s FAIL: source not compiled: %s\n", job.game, job.cso, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
      ++failures;
      continue;
    }
    const auto after_code = ReadFile(built);
    const auto before_code = ReadFile(previous);
    if (after_code.empty() || before_code.empty()) {
      std::printf("%-26s %-22s FAIL: not found (%s: %zu bytes, %s: %zu bytes)\n", job.game, job.cso, built.c_str(), after_code.size(), previous.c_str(),
                  before_code.size());
      ++failures;
      continue;
    }
    // The reflection belongs to the built shader: the same code.
    const bool same_code = Code(blob->GetBufferPointer(), blob->GetBufferSize()) == Code(after_code.data(), after_code.size());
    ComPtr<ID3D11ShaderReflection> reflection;
    D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflection));
    D3D11_SHADER_DESC shader_desc = {};
    reflection->GetDesc(&shader_desc);
    std::vector<Constants> buffers;
    for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
      D3D11_SHADER_INPUT_BIND_DESC bind = {};
      reflection->GetResourceBindingDesc(i, &bind);
      if (bind.Type != D3D_SIT_CBUFFER) continue;
      D3D11_SHADER_BUFFER_DESC desc = {};
      reflection->GetConstantBufferByName(bind.Name)->GetDesc(&desc);
      Constants constants = {.name = bind.Name, .slot = bind.BindPoint, .values = std::vector<float>(desc.Size / 4)};
      for (size_t f = 0; f < constants.values.size(); ++f) constants.values[f] = (f / 4) % 4 == f % 4 ? 1.f : 0.25f;
      buffers.push_back(constants);
    }
    // The injection: zeros, then GT-VBAO's fields.
    const auto toggle = Find(reflection.Get(), buffers, job.toggle);
    if (!toggle) {
      std::printf("%-26s %-22s FAIL: no %s in its constants\n", job.game, job.cso, job.toggle);
      ++failures;
      continue;
    }
    if (std::strcmp(job.toggle, "ssao_exact_depth") == 0) std::ranges::fill(buffers[toggle->first].values, 0.f);
    if (!Apply(reflection.Get(), &buffers, DEFAULTS, false) || !Apply(reflection.Get(), &buffers, job.overrides, true)) {
      ++failures;
      continue;
    }
    const Result before = Run(before_code, compute, reflection.Get(), buffers, *toggle, 0.f);
    const Result off = Run(after_code, compute, reflection.Get(), buffers, *toggle, 0.f);
    const Result on = Run(after_code, compute, reflection.Get(), buffers, *toggle, 1.f);
    if (!before.ran || !off.ran || !on.ran) {
      std::printf("%-26s %-22s FAIL: not run\n", job.game, job.cso);
      ++failures;
      continue;
    }
    const size_t off_diff = Differing(before, off);
    const size_t on_diff = Differing(before, on);
    const auto [before_mean, before_under] = Channel0(before);
    const auto [on_mean, on_under] = Channel0(on);
    const bool pass = same_code && off_diff == 0 && on_diff > 0;
    std::printf("%-26s %-22s %6s %9zu %9zu | %9.5f %8.4f %9.5f %8.4f %s\n", job.game, job.cso, same_code ? "same" : "OTHER", off_diff, on_diff, before_mean,
                before_under, on_mean, on_under, pass ? "PASS" : (on_diff == 0 ? "FAIL (CONTROL: on the same)" : "FAIL"));
    failures += pass ? 0 : 1;
  }
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures;
}
