// The D3D11 pass speed round's replay (d3d11_pass_speed.md 3.3): a pass dump's frame (pass_dump.hpp's folder: manifest.json,
// blobs\, done.txt) replayed through the modules' own internal entry points, each event from its own dumped inputs, on
// the hardware GPU (--warp: WARP).
//   replay.exe --self-test <folder> [--warp] [--race-map <csos>]   dumps its own frames through pass_dump, replays them
//   replay.exe --exact <frame> [--race-map <csos>] [--out <dir>] [--against <dir>] [--skip <event>[,<event>]] [--warp]
//   replay.exe --time <n> <frame> --out <block.json> [--warp]
//   replay.exe --settings <matrix.json> <frame> --out <dir> [--warp]
//   replay.exe --profile [<exe name> ...]                            the driver profile's filtering keys (NVAPI DRS, read only)
// Replayed: sss.compute (screen_space_shadows' Compute), ssr.build / ssr.resolve (ssr_hiz), ao_filter.filter. Listed, not
// replayed: sss.lights, smoke.map (its list's order differs run to run: an input), ao.draw and bounce.sun_draw (draw-hosted:
// their befores and afters are in the dump for a later replay with the dumped vertex shader).
// Before each pass the constant bytes it builds must be the dumped ones (pass_dump::Bytes against the expected): else
// that event's case is void and the field is named. Bend's race pixels (two waves of one dispatch writing different
// values to a pixel) come from the race-map build (run.ps1 compiles sss_bend*.cs_5_0.hlsl with BEND_SSS_RACE_MAP; outside
// the race pixels its outputs must equal the real build's dump, the proof it computes the same): outside them every byte
// must be equal; at them each output's value must be one writer's, all of its channels from that writer (the joint
// rule, design M4: tint's RGB is one store). Not one writer across outputs: occlusion, penumbra and tint are separate
// stores to separate UAVs, which two waves may interleave (WARP, 11-25: 7 of 665 race pixels held one writer's
// occlusion and another's penumbra). Unorm 8 and half: within 1 step, D3D's conversions may round or truncate.
// Overflow (more than 4 writers): passes only if one of the 4 kept gives it; lane against base (--against) only if both
// overflow, else the writers' multisets must match.
// An event this build can replay that is not replayed (an input not made, a module not prepared) fails the frame, unless
// --skip <name>[,<name>] names it; the events listed above as not replayed are listed, not failed. A blob whose size is
// not the one its name and its rows give refuses the frame.
// Downstream of Bend (sss_blur / sss_soft) runs again from the dump's occlusion and penumbra (injection): byte equal.
// A texture an event makes (the first frame after a setting turns it on) is not one of its inputs: compared from the
// next frame on.
// Exit 0: every replayed event equal (and, for --self-test, every check and control as it must be).
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#if __has_include(<nvapi.h>)
#include <nvapi.h>
#include <NvApiDriverSettings.h>
#endif
#include "src/games/twattila/ao_filter.hpp"
#include "src/games/twattila/screen_space_lights.hpp"
#include "src/games/twattila/ssr_hiz.hpp"

extern "C" __declspec(dllexport) const char* const NAME = "D3D11 pass replay";
// The exe is its own ReShade for the modules' logs (pass_dump's lines printed).
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }
}

using Microsoft::WRL::ComPtr;
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace sss = twattila::screen_space_shadows;
namespace ssr = twattila::ssr_hiz;
namespace aof = twattila::ao_filter;
namespace dump = twattila::pass_dump;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static int failures = 0;

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition);              \
      ++failures;                                                           \
    }                                                                       \
  } while (false)

// ---- The dump's frame -------------------------------------------------------------------------------------------------

struct Subresource {
  UINT index = 0;
  UINT width = 1;
  UINT height = 1;
  UINT depth = 1;
  UINT row_bytes = 0;
  UINT rows = 1;
  std::vector<uint8_t> data;
};

struct Copy {
  std::string kind;
  json desc;
  bool multisampled = false;
  std::vector<Subresource> subresources;
};

struct Frame {
  fs::path folder;
  json manifest;
  std::vector<Copy> copies;
};

static std::vector<uint8_t> ReadFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// A frame folder (void without done.txt).
static bool LoadFrame(const fs::path& folder, Frame* frame) {
  if (!fs::exists(folder / "done.txt") || !fs::exists(folder / "manifest.json")) {
    std::printf("%s: no done.txt (void)\n", folder.string().c_str());
    return false;
  }
  frame->folder = folder;
  frame->manifest = json::parse(std::ifstream(folder / "manifest.json"));
  for (const auto& entry : frame->manifest["copies"]) {
    Copy copy = {.kind = entry["kind"], .desc = entry["desc"], .multisampled = entry["multisampled"]};
    for (const auto& sub : entry["subresources"]) {
      Subresource subresource = {.index = sub["index"], .width = sub["width"], .height = sub["height"], .depth = sub["depth"],
                                 .row_bytes = sub["row_bytes"], .rows = sub["rows"]};
      const std::string blob = sub["blob"];
      const size_t want = size_t{subresource.row_bytes} * subresource.rows * subresource.depth;
      if (blob.empty() && want != 0) {
        std::printf("%s: a subresource of copy %zu has no blob (refused)\n", folder.string().c_str(), frame->copies.size());
        return false;
      }
      if (!blob.empty()) {
        subresource.data = ReadFile(folder / "blobs" / blob);
        const size_t named = std::stoull(blob.substr(blob.rfind('_') + 1));
        if (subresource.data.size() != named || subresource.data.size() != want) {
          std::printf("%s: blob %s is %zu bytes (its name %zu, its rows %zu): refused\n", folder.string().c_str(), blob.c_str(),
                      subresource.data.size(), named, want);
          return false;
        }
      }
      copy.subresources.push_back(std::move(subresource));
    }
    frame->copies.push_back(std::move(copy));
  }
  return true;
}

// An event's copy (before / after) of `name`; null: none.
static const Copy* Find(const Frame& frame, const json& event, const char* side, const std::string& name) {
  if (!event[side].contains(name)) return nullptr;
  const int index = event[side][name];
  return (index < 0) ? nullptr : &frame.copies[static_cast<size_t>(index)];
}

static double Value(const json& event, const std::string& name, double fallback = 0.0) {
  if (!event["values"].contains(name)) return fallback;
  const auto& value = event["values"][name];
  return value.is_boolean() ? (value.get<bool>() ? 1.0 : 0.0) : value.get<double>();
}

static std::vector<uint8_t> Bytes(const Frame& frame, const json& event, const std::string& name) {
  if (!event["bytes"].contains(name)) return {};
  const int index = event["bytes"][name];
  return frame.copies[static_cast<size_t>(index)].subresources[0].data;
}

// ---- Resources from copies --------------------------------------------------------------------------------------------

static ComPtr<ID3D11Resource> Make(const Copy& copy) {
  const auto& d = copy.desc;
  ComPtr<ID3D11Resource> made;
  std::vector<D3D11_SUBRESOURCE_DATA> initial;
  for (const auto& subresource : copy.subresources) {
    if (subresource.data.empty()) return nullptr;
    initial.push_back({subresource.data.data(), subresource.row_bytes, subresource.row_bytes * subresource.rows});
  }
  if (copy.kind == "buffer") {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = d["ByteWidth"], .Usage = D3D11_USAGE_DEFAULT, .BindFlags = d["BindFlags"],
                                    .MiscFlags = d["MiscFlags"].get<UINT>() & (D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS
                                                                               | D3D11_RESOURCE_MISC_BUFFER_STRUCTURED),
                                    .StructureByteStride = d["StructureByteStride"]};
    ComPtr<ID3D11Buffer> buffer;
    if (SUCCEEDED(device->CreateBuffer(&desc, initial.data(), buffer.GetAddressOf()))) made = buffer;
  } else if (copy.kind == "texture2d") {
    if (copy.multisampled) return nullptr;
    // D3D11 takes no initial data for a depth-stencil texture: the replay only reads it through a view.
    UINT bind = d["BindFlags"];
    if ((bind & D3D11_BIND_DEPTH_STENCIL) != 0) {
      bind = (bind & ~static_cast<UINT>(D3D11_BIND_DEPTH_STENCIL)) | D3D11_BIND_SHADER_RESOURCE;
    }
    const D3D11_TEXTURE2D_DESC desc = {.Width = d["Width"], .Height = d["Height"], .MipLevels = d["MipLevels"], .ArraySize = d["ArraySize"],
                                       .Format = static_cast<DXGI_FORMAT>(d["Format"].get<int>()), .SampleDesc = {.Count = 1},
                                       .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind,
                                       .MiscFlags = d["MiscFlags"].get<UINT>() & D3D11_RESOURCE_MISC_TEXTURECUBE};
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(device->CreateTexture2D(&desc, initial.data(), texture.GetAddressOf()))) made = texture;
  } else if (copy.kind == "texture3d") {
    const D3D11_TEXTURE3D_DESC desc = {.Width = d["Width"], .Height = d["Height"], .Depth = d["Depth"], .MipLevels = d["MipLevels"],
                                       .Format = static_cast<DXGI_FORMAT>(d["Format"].get<int>()), .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = d["BindFlags"]};
    ComPtr<ID3D11Texture3D> texture;
    if (SUCCEEDED(device->CreateTexture3D(&desc, initial.data(), texture.GetAddressOf()))) made = texture;
  }
  return made;
}

// A dumped view's resource with the view's format (the event's "<name>.view_format"); typeless depth read as its depth.
static ComPtr<ID3D11ShaderResourceView> MakeView(const Frame& frame, const json& event, const std::string& name, ComPtr<ID3D11Resource>* kept) {
  const Copy* copy = Find(frame, event, "before", name);
  if (copy == nullptr) return nullptr;
  *kept = Make(*copy);
  if (!*kept) return nullptr;
  const auto format = static_cast<DXGI_FORMAT>(static_cast<int>(Value(event, name + ".view_format", -1)));
  const auto dimension = static_cast<D3D11_SRV_DIMENSION>(static_cast<int>(Value(event, name + ".view_dimension", D3D11_SRV_DIMENSION_TEXTURE2D)));
  D3D11_SHADER_RESOURCE_VIEW_DESC desc = {.Format = format, .ViewDimension = dimension};
  desc.Texture2D = {.MostDetailedMip = 0, .MipLevels = UINT_MAX};
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(kept->Get(), (static_cast<int>(format) < 0) ? nullptr : &desc, view.GetAddressOf());
  return view;
}

// `resource`'s contents set to `copy`'s (each subresource; the replay's own object of the same desc).
static void Fill(ID3D11Resource* resource, const Copy* copy) {
  if (resource == nullptr || copy == nullptr) return;
  for (const auto& subresource : copy->subresources) {
    if (subresource.data.empty()) continue;
    context->UpdateSubresource(resource, subresource.index, nullptr, subresource.data.data(), subresource.row_bytes, subresource.row_bytes * subresource.rows);
  }
}

static ID3D11Resource* Of(ID3D11View* view) {
  if (view == nullptr) return nullptr;
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  return resource.Get();  // the view holds it
}

// `resource` read back in `like`'s packing (its subresources' rows).
static std::vector<std::vector<uint8_t>> Read(ID3D11Resource* resource, const Copy& like) {
  std::vector<std::vector<uint8_t>> out;
  D3D11_RESOURCE_DIMENSION dimension = {};
  resource->GetType(&dimension);
  ComPtr<ID3D11Resource> staging;
  if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
    D3D11_BUFFER_DESC desc = {};
    static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
    desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf());
    staging = buffer;
  } else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
    D3D11_TEXTURE2D_DESC desc = {};
    static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
    desc.Format = static_cast<DXGI_FORMAT>(like.desc["Format"].get<int>());
    const auto typeless = static_cast<DXGI_FORMAT>(reshade::api::format_to_typeless(static_cast<reshade::api::format>(desc.Format)));
    if (desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || desc.Format == DXGI_FORMAT_D32_FLOAT || desc.Format == DXGI_FORMAT_D16_UNORM
        || desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
      desc.Format = typeless;
    }
    desc = {.Width = desc.Width, .Height = desc.Height, .MipLevels = desc.MipLevels, .ArraySize = desc.ArraySize, .Format = desc.Format,
            .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Texture2D> texture;
    device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf());
    staging = texture;
  }
  if (!staging) return out;
  context->CopyResource(staging.Get(), resource);
  for (const auto& subresource : like.subresources) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    std::vector<uint8_t> data(size_t{subresource.row_bytes} * subresource.rows * subresource.depth);
    if (SUCCEEDED(context->Map(staging.Get(), subresource.index, D3D11_MAP_READ, 0, &mapped))) {
      for (UINT slice = 0; slice < subresource.depth; ++slice) {
        for (UINT row = 0; row < subresource.rows; ++row) {
          std::memcpy(data.data() + ((size_t{slice} * subresource.rows) + row) * subresource.row_bytes,
                      static_cast<const uint8_t*>(mapped.pData) + (size_t{slice} * mapped.DepthPitch) + (size_t{row} * mapped.RowPitch), subresource.row_bytes);
        }
      }
      context->Unmap(staging.Get(), subresource.index);
    }
    out.push_back(std::move(data));
  }
  return out;
}

// ---- Bend's race map --------------------------------------------------------------------------------------------------

struct RaceTuple {
  float occlusion;
  float penumbra;
  std::array<float, 3> tint;
  uint32_t dispatch;
};
static_assert(sizeof(RaceTuple) == 24);

// The race-map build's Bend shaders (run.ps1's csos) and its buffers (bound at u4 / u5 through the compute).
struct RaceMap {
  bool loaded = false;
  ComPtr<ID3D11ComputeShader> bend;
  std::array<ComPtr<ID3D11ComputeShader>, 4> bend_extended;
  UINT width = 0;
  UINT height = 0;
  ComPtr<ID3D11Buffer> count;
  ComPtr<ID3D11UnorderedAccessView> count_uav;
  ComPtr<ID3D11Buffer> tuples;
  ComPtr<ID3D11UnorderedAccessView> tuples_uav;
};
static RaceMap race_map;

static bool LoadRaceMap(const fs::path& folder) {
  const auto shader = [&](const char* name, ComPtr<ID3D11ComputeShader>* made) {
    const auto code = ReadFile(folder / (std::string(name) + ".race_map.cso"));
    return !code.empty() && SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, made->GetAddressOf()));
  };
  race_map.loaded = shader("sss_bend", &race_map.bend);
  for (size_t index = 0; race_map.loaded && index < 4; ++index) race_map.loaded = shader(sss::internal::EXTENDED_NAMES[index], &race_map.bend_extended[index]);
  if (!race_map.loaded) std::printf("race map: its csos are missing in %s\n", folder.string().c_str());
  return race_map.loaded;
}

// Per pixel: its writers (count) and up to 4 tuples.
struct RaceResult {
  std::vector<uint32_t> count;
  std::vector<RaceTuple> tuples;
  // Two writers of one dispatch that wrote different values, or more than 4: the pixel's value is either one (writers
  // of different dispatches are ordered: the later dispatch's value is the pixel's).
  bool Raced(size_t pixel) const {
    const uint32_t writers = count[pixel];
    if (writers > 4) return true;
    for (uint32_t a = 0; a < writers; ++a) {
      for (uint32_t b = a + 1; b < writers; ++b) {
        const RaceTuple& first = tuples[(pixel * 4) + a];
        const RaceTuple& second = tuples[(pixel * 4) + b];
        if (first.dispatch == second.dispatch && std::memcmp(&first, &second, sizeof(RaceTuple)) != 0) return true;
      }
    }
    return false;
  }
};

static void BindRaceMap(UINT width, UINT height) {
  if (race_map.width != width || race_map.height != height) {
    race_map.width = width;
    race_map.height = height;
    const auto buffer = [&](UINT stride, UINT count, ComPtr<ID3D11Buffer>* made, ComPtr<ID3D11UnorderedAccessView>* uav) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = stride * count, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = stride};
      device->CreateBuffer(&desc, nullptr, made->ReleaseAndGetAddressOf());
      device->CreateUnorderedAccessView(made->Get(), nullptr, uav->ReleaseAndGetAddressOf());
    };
    buffer(4, width * height, &race_map.count, &race_map.count_uav);
    buffer(sizeof(RaceTuple), width * height * 4, &race_map.tuples, &race_map.tuples_uav);
  }
  constexpr std::array<UINT, 4> ZERO = {};
  context->ClearUnorderedAccessViewUint(race_map.count_uav.Get(), ZERO.data());
  const std::array<ID3D11UnorderedAccessView*, 2> uavs = {race_map.count_uav.Get(), race_map.tuples_uav.Get()};
  constexpr std::array<UINT, 2> KEEP = {UINT_MAX, UINT_MAX};
  context->CSSetUnorderedAccessViews(4, 2, uavs.data(), KEEP.data());
}

static RaceResult ReadRaceMap() {
  constexpr std::array<ID3D11UnorderedAccessView*, 2> NONE = {};
  context->CSSetUnorderedAccessViews(4, 2, NONE.data(), nullptr);
  const auto read = [](ID3D11Buffer* buffer, auto* out) {
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    const Copy like = {.kind = "buffer", .subresources = {{.width = desc.ByteWidth, .row_bytes = desc.ByteWidth}}};
    const auto data = Read(buffer, like)[0];
    out->resize(data.size() / sizeof((*out)[0]));
    std::memcpy(out->data(), data.data(), data.size());
  };
  RaceResult result;
  read(race_map.count.Get(), &result.count);
  read(race_map.tuples.Get(), &result.tuples);
  return result;
}

// A written value as the texture keeps it: unorm 8 (round to nearest) or half.
static uint32_t Quantize(float value, DXGI_FORMAT format) {
  if (format == DXGI_FORMAT_R16_FLOAT) return DirectX::PackedVector::XMConvertFloatToHalf(value);
  return static_cast<uint32_t>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
}

// ---- Compare ----------------------------------------------------------------------------------------------------------

struct Compared {
  size_t bytes = 0;      // differing bytes outside the race pixels
  size_t first = SIZE_MAX;
};

// `got` against `want` byte for byte; with a race map for a Bend output (`channel`: 0 occlusion R8, 1 penumbra R16F,
// 2 tint RGBA8), its race pixels by its writers instead: each race pixel's bit of `matching` (one bit a writer slot)
// stays set only for the writers whose value, every channel of it, this output's `want` holds (the joint rule).
static Compared Compare(const std::vector<std::vector<uint8_t>>& got, const Copy& want, const RaceResult* race = nullptr, int channel = -1,
                        std::vector<uint8_t>* matching = nullptr) {
  Compared result;
  for (size_t sub = 0; sub < want.subresources.size() && sub < got.size(); ++sub) {
    const auto& expected = want.subresources[sub].data;
    const auto& actual = got[sub];
    const size_t texel = (want.subresources[sub].width == 0) ? 1 : std::max<size_t>(1, want.subresources[sub].row_bytes / want.subresources[sub].width);
    for (size_t byte = 0; byte < std::min(expected.size(), actual.size()); ++byte) {
      if (expected[byte] == actual[byte]) continue;
      const size_t pixel = byte / texel;
      if (race != nullptr && sub == 0 && pixel < race->count.size() && race->Raced(pixel)) continue;
      ++result.bytes;
      if (result.first == SIZE_MAX) result.first = byte;
    }
    if (race != nullptr && sub == 0 && matching != nullptr) {
      const auto format = (channel == 1) ? DXGI_FORMAT_R16_FLOAT : DXGI_FORMAT_R8_UNORM;
      for (size_t pixel = 0; pixel < race->count.size() && (pixel + 1) * texel <= expected.size(); ++pixel) {
        if (!race->Raced(pixel)) continue;
        for (uint32_t slot = 0; slot < std::min<uint32_t>(race->count[pixel], 4); ++slot) {
          const RaceTuple& tuple = race->tuples[(pixel * 4) + slot];
          bool holds = true;
          for (size_t component = 0; component < ((channel == 2) ? 3 : 1); ++component) {
            float value = tuple.tint[component];
            if (channel == 0) {
              value = tuple.occlusion;
            } else if (channel == 1) {
              value = tuple.penumbra;
            }
            if (value < 0.f) continue;  // not written by this writer
            uint32_t stored = 0;
            std::memcpy(&stored, expected.data() + (pixel * texel) + ((channel == 2) ? component : 0), (channel == 1) ? 2 : 1);
            const uint32_t quantized = Quantize(value, format);
            holds = holds && (std::max(stored, quantized) - std::min(stored, quantized)) <= 1;
          }
          if (!holds) {
            (*matching)[pixel] &= static_cast<uint8_t>(~(1u << slot));
          }
        }
      }
    }
  }
  return result;
}

// ---- The events -------------------------------------------------------------------------------------------------------

// Its constant buffers' expected bytes (pass_dump::Bytes checks them before the upload).
static void Expect(const Frame& frame, const json& event, std::initializer_list<const char*> names) {
  auto& state = dump::internal::state;
  state.expected.clear();
  state.mismatches.clear();
  for (const char* name : names) {
    const auto bytes = Bytes(frame, event, name);
    if (!bytes.empty()) state.expected[name] = bytes;
  }
  dump::internal::checking.store(true);
}

// An event's outputs after the replay, by name (the replay's own objects).
using Outputs = std::vector<std::pair<std::string, ID3D11Resource*>>;

struct Replayed {
  bool replayed = false;
  std::string why;            // not replayed / void
  Outputs outputs;
  RaceResult race;            // sss.compute with the race map
  bool raced = false;
  bool injected = false;      // sss.soft from the dump's occlusion (and penumbra)
  std::vector<std::string> mismatches;  // constant bytes
};

// Overrides for --settings: sss's Settings fields by name.
static json settings_override;

// --time's timestamps, ended right before and after an event's module entry (its inputs' uploads not timed).
static std::array<ID3D11Query*, 2> stamps = {};

static void Stamp(size_t index) {
  if (stamps[index] != nullptr) context->End(stamps[index]);
}

static void ApplySettings(sss::Settings* settings) {
  if (!settings_override.contains("sss")) return;
  const std::map<std::string, float*> floats = {{"samples", &settings->samples}, {"thickness", &settings->thickness}, {"contrast", &settings->contrast},
                                                {"edge_threshold", &settings->edge_threshold}, {"hard_samples", &settings->hard_samples},
                                                {"fade_samples", &settings->fade_samples}, {"softness", &settings->softness},
                                                {"foliage_light", &settings->foliage_light}, {"sun_disk", &settings->sun_disk},
                                                {"thin_light", &settings->thin_light}, {"light_range", &settings->light_range}};
  const std::map<std::string, bool*> bools = {{"ignore_edges", &settings->ignore_edges}, {"precision_offset", &settings->precision_offset},
                                              {"sample_along_ray", &settings->sample_along_ray}, {"mirror_edges", &settings->mirror_edges},
                                              {"auto_thickness", &settings->auto_thickness}, {"coloured", &settings->coloured}};
  for (const auto& [key, value] : settings_override["sss"].items()) {
    if (const auto found = floats.find(key); found != floats.end()) *found->second = value.get<float>();
    if (const auto found = bools.find(key); found != bools.end()) *found->second = value.get<bool>();
    if (key == "debug_view") settings->debug_view = value.get<int>();
  }
}

// Inputs kept alive through an event's replay.
struct Kept {
  std::vector<ComPtr<ID3D11Resource>> resources;
  std::vector<ComPtr<ID3D11ShaderResourceView>> views;
  ID3D11Resource* Add(ComPtr<ID3D11Resource> resource) {
    resources.push_back(std::move(resource));
    return resources.back().Get();
  }
};

static Replayed ReplaySss(const Frame& frame, const json& event, Kept* kept, bool with_race_map) {
  Replayed out;
  auto& runtime = sss::internal::runtime;
  auto& objects = runtime.objects;
  const auto settings_bytes = Bytes(frame, event, "sss.settings_cpu");
  if (settings_bytes.size() != sizeof(sss::Settings)) return {.why = "sss.settings_cpu missing or of another build's size"};
  std::memcpy(&runtime.settings, settings_bytes.data(), sizeof(sss::Settings));
  ApplySettings(&runtime.settings);
  runtime.frame = static_cast<uint64_t>(Value(event, "sss.frame"));
  runtime.thin_frame = static_cast<uint64_t>(Value(event, "sss.thin_frame", -1.0));
  runtime.before_foliage_frame = static_cast<uint64_t>(Value(event, "sss.before_foliage_frame", -1.0));
  runtime.after_foliage_frame = static_cast<uint64_t>(Value(event, "sss.after_foliage_frame", -1.0));
  runtime.albedo_power = static_cast<float>(Value(event, "sss.albedo_power", 2.0));
  sss::foliage_material_bit = static_cast<uint32_t>(Value(event, "sss.foliage_material_bit"));
  twattila::frame_history::render_width.store(static_cast<uint32_t>(Value(event, "frame_history.render_width")));
  twattila::frame_history::render_height.store(static_cast<uint32_t>(Value(event, "frame_history.render_height")));
  twattila::frame_history::effect_share.store(static_cast<float>(Value(event, "frame_history.effect_share", 1.0)));
  const auto width = static_cast<UINT>(Value(event, "sss.width"));
  const auto height = static_cast<UINT>(Value(event, "sss.height"));
  if (!sss::internal::Prepare(device.Get(), width, height)) return {.why = "Prepare failed"};
  objects.uploaded = {};
  objects.uploaded_extended = {};

  ComPtr<ID3D11Resource> depth_resource;
  auto depth_view = MakeView(frame, event, "sss.depth_view", &depth_resource);
  if (!depth_view) return {.why = "no depth"};
  kept->Add(depth_resource);
  kept->views.push_back(depth_view);
  const auto buffer = [&](const char* name) -> ID3D11Buffer* {
    const Copy* copy = Find(frame, event, "before", name);
    return (copy == nullptr) ? nullptr : static_cast<ID3D11Buffer*>(kept->Add(Make(*copy)));
  };
  ID3D11Buffer* camera = buffer("sss.camera");
  ID3D11Buffer* lighting = buffer("sss.lighting");
  const auto view = [&](const char* name) -> ID3D11ShaderResourceView* {
    ComPtr<ID3D11Resource> resource;
    auto made = MakeView(frame, event, name, &resource);
    if (!made) return nullptr;
    kept->Add(resource);
    kept->views.push_back(made);
    return made.Get();
  };
  ID3D11ShaderResourceView* colour = view("sss.colour");
  ID3D11ShaderResourceView* material = view("sss.material");
  // The module's own inputs: the foliage's depth copies, the banners' depth (their objects as the frame had them).
  const auto own_texture = [&](const char* name, ComPtr<ID3D11Texture2D>* texture, ComPtr<ID3D11ShaderResourceView>* srv, DXGI_FORMAT format) {
    const Copy* copy = Find(frame, event, "before", name);
    if (copy == nullptr) return;
    ComPtr<ID3D11Resource> resource = Make(*copy);
    if (!resource || FAILED(resource.As(texture))) return;
    const D3D11_SHADER_RESOURCE_VIEW_DESC desc = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, .Texture2D = {.MipLevels = 1}};
    device->CreateShaderResourceView(texture->Get(), (format == DXGI_FORMAT_UNKNOWN) ? nullptr : &desc, srv->ReleaseAndGetAddressOf());
  };
  const auto depth_read = [](const Copy* copy) {
    if (copy == nullptr) return DXGI_FORMAT_UNKNOWN;
    switch (static_cast<DXGI_FORMAT>(copy->desc["Format"].get<int>())) {
      case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
      case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
      case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
      case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
      default: return DXGI_FORMAT_UNKNOWN;
    }
  };
  own_texture("sss.before_foliage", &objects.before_foliage, &objects.before_foliage_srv, depth_read(Find(frame, event, "before", "sss.before_foliage")));
  own_texture("sss.after_foliage", &objects.after_foliage, &objects.after_foliage_srv, depth_read(Find(frame, event, "before", "sss.after_foliage")));
  own_texture("sss.thin", &objects.thin_texture, &objects.thin_srv, DXGI_FORMAT_UNKNOWN);
  // Its outputs as they were before the event (Bend leaves the pixels it skips as they were).
  if (runtime.settings.sun_disk > 0.f && !objects.penumbra_uav) {
    sss::internal::MakeTexture(device.Get(), width, height, DXGI_FORMAT_R16_FLOAT, &objects.penumbra_uav, &objects.penumbra_srv);
  }
  if (runtime.settings.coloured && !objects.tint_uav) {
    sss::internal::MakeTexture(device.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM, &objects.tint_uav, &objects.tint_srv);
  }
  out.outputs = {{"sss.depth", Of(objects.depth_srv.Get())},         {"sss.arguments", objects.arguments.Get()},
                 {"sss.parameters", objects.parameters.Get()},       {"sss.occlusion", Of(objects.occlusion_srv.Get())},
                 {"sss.penumbra", Of(objects.penumbra_srv.Get())},   {"sss.tint", Of(objects.tint_srv.Get())},
                 {"sss.soft", Of(objects.soft_srv.Get())},           {"sss.lights_camera", objects.lights.camera.Get()}};
  for (const auto& [name, resource] : out.outputs) Fill(resource, Find(frame, event, "before", name));

  const bool racing = with_race_map && race_map.loaded;
  std::array<ComPtr<ID3D11ComputeShader>, 5> real = {objects.bend, objects.bend_extended[0], objects.bend_extended[1], objects.bend_extended[2],
                                                     objects.bend_extended[3]};
  if (racing) {
    objects.bend = race_map.bend;
    objects.bend_extended = race_map.bend_extended;
    BindRaceMap(width, height);
  }
  if (settings_override.contains("sss")) {
    dump::internal::checking.store(false);
  } else {
    Expect(frame, event, {"sss.shader_settings", "sss.extended_settings"});
  }
  Stamp(0);
  sss::internal::Compute(context.Get(), depth_view.Get(), Value(event, "sss.msaa") != 0.0, camera, lighting,
                         static_cast<UINT>(Value(event, "sss.sun_direction", 1.0)), colour, material);
  Stamp(1);
  dump::internal::checking.store(false);
  out.mismatches = dump::internal::state.mismatches;
  if (racing) {
    out.race = ReadRaceMap();
    out.raced = true;
    objects.bend = real[0];
    for (size_t index = 0; index < 4; ++index) objects.bend_extended[index] = real[index + 1];
  }
  // A texture made by this Compute (none before) is an output too.
  out.outputs[4].second = Of(objects.penumbra_srv.Get());
  out.outputs[5].second = Of(objects.tint_srv.Get());
  out.outputs[7].second = objects.lights.camera.Get();
  // Injection (downstream of Bend's race pixels): the smoothing pass again from the dump's occlusion and penumbra, so its
  // output compares byte for byte; Bend's own outputs compare from snapshots taken first.
  const bool soft = runtime.settings.sun_disk > 0.f && objects.penumbra_srv;
  if (soft || runtime.settings.softness > 0.f) {
    for (const size_t index : {size_t{3}, size_t{4}}) {
      ID3D11Resource* resource = out.outputs[index].second;
      const Copy* after = Find(frame, event, "after", out.outputs[index].first);
      if (resource == nullptr || after == nullptr) continue;
      ComPtr<ID3D11Texture2D> snapshot;
      D3D11_TEXTURE2D_DESC desc = {};
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      device->CreateTexture2D(&desc, nullptr, snapshot.GetAddressOf());
      context->CopyResource(snapshot.Get(), resource);
      Fill(resource, after);
      out.outputs[index].second = kept->Add(snapshot);
    }
    const auto& smooth = soft ? objects.soft : objects.blur;
    sss::internal::ComputeState::Bind(context.Get(), smooth.Get(),
                                      {objects.depth_srv.Get(), objects.occlusion_srv.Get(), soft ? objects.penumbra_srv.Get() : nullptr},
                                      {objects.soft_uav.Get()}, {nullptr, nullptr, objects.settings.Get(), nullptr, objects.extended_settings.Get()});
    context->Dispatch((objects.width + 7) / 8, (objects.height + 7) / 8, 1);
    sss::internal::ComputeState::Bind(context.Get(), nullptr, {}, {}, {});
    out.injected = true;
  }
  out.replayed = true;
  return out;
}

static Replayed ReplaySsrBuild(const Frame& frame, const json& event, Kept* kept) {
  ComPtr<ID3D11Resource> depth_resource;
  auto depth_view = MakeView(frame, event, "ssr.depth", &depth_resource);
  if (!depth_view) return {.why = "no depth"};
  kept->Add(depth_resource);
  kept->views.push_back(depth_view);
  D3D11_TEXTURE2D_DESC desc = {};
  ComPtr<ID3D11Texture2D> texture;
  depth_resource.As(&texture);
  texture->GetDesc(&desc);
  if (!ssr::internal::Prepare(device.Get(), desc.Width, desc.Height)) return {.why = "Prepare failed"};
  Replayed out;
  out.outputs = {{"ssr.pyramid", Of(ssr::internal::runtime.objects.pyramid.Get())}};
  Fill(out.outputs[0].second, Find(frame, event, "before", "ssr.pyramid"));
  Stamp(0);
  ssr::internal::Build(context.Get(), depth_view.Get());
  Stamp(1);
  out.replayed = true;
  return out;
}

static Replayed ReplaySsrResolve(const Frame& frame, const json& event, Kept* kept) {
  auto& runtime = ssr::internal::runtime;
  auto& objects = runtime.objects;
  const Copy* target_copy = Find(frame, event, "before", "ssr.target");
  ComPtr<ID3D11Resource> depth_resource;
  auto depth_view = MakeView(frame, event, "ssr.depth", &depth_resource);
  if (target_copy == nullptr || !depth_view) return {.why = "no target or depth"};
  kept->Add(depth_resource);
  kept->views.push_back(depth_view);
  ID3D11Resource* target = kept->Add(Make(*target_copy));
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC desc = {};
  if (target == nullptr || FAILED(target->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) return {.why = "target not made"};
  texture->GetDesc(&desc);
  D3D11_TEXTURE2D_DESC depth_desc = {};
  ComPtr<ID3D11Texture2D> depth_texture;
  depth_resource.As(&depth_texture);
  depth_texture->GetDesc(&depth_desc);
  if (!ssr::internal::Prepare(device.Get(), depth_desc.Width, depth_desc.Height) || !ssr::internal::PrepareResolve(device.Get(), desc)) {
    return {.why = "Prepare failed"};
  }
  runtime.frame = static_cast<uint64_t>(Value(event, "ssr.frame"));
  runtime.resolved_frame = static_cast<uint64_t>(Value(event, "ssr.resolved_frame", -1.0));
  runtime.history_index = static_cast<size_t>(Value(event, "ssr.history_index"));
  runtime.history_valid = Value(event, "ssr.history_valid") != 0.0;
  runtime.reset = Value(event, "ssr.reset", 1.0) != 0.0;
  runtime.denoise = Value(event, "ssr.denoise") != 0.0;
  twattila::frame_history::render_width.store(static_cast<uint32_t>(Value(event, "frame_history.render_width")));
  twattila::frame_history::render_height.store(static_cast<uint32_t>(Value(event, "frame_history.render_height")));
  const auto own_buffer = [&](const char* name, ComPtr<ID3D11Buffer>* buffer) {
    const Copy* copy = Find(frame, event, "before", name);
    buffer->Reset();
    if (copy == nullptr) return;
    ComPtr<ID3D11Resource> made = Make(*copy);
    if (made) made.As(buffer);
  };
  own_buffer("ssr.last_camera", &objects.last_camera);
  own_buffer("ssr.previous_camera", &objects.previous_camera);
  const Copy* camera_copy = Find(frame, event, "before", "ssr.camera");
  auto* camera = (camera_copy == nullptr) ? nullptr : static_cast<ID3D11Buffer*>(kept->Add(Make(*camera_copy)));
  Replayed out;
  out.outputs = {{"ssr.target", target},
                 {"ssr.hit", Of(objects.hit_srv.Get())},
                 {"ssr.reflection_copy", objects.reflection_copy.Get()},
                 {"ssr.result", objects.result.Get()},
                 {"ssr.history_0", objects.histories[0].Get()},
                 {"ssr.history_1", objects.histories[1].Get()}};
  for (const auto& [name, resource] : out.outputs) {
    if (name != "ssr.target") Fill(resource, Find(frame, event, "before", name));
  }
  Expect(frame, event, {"ssr.resolve_settings"});
  Stamp(0);
  ssr::internal::Resolve(context.Get(), target, depth_view.Get(), camera);
  Stamp(1);
  dump::internal::checking.store(false);
  out.mismatches = dump::internal::state.mismatches;
  out.outputs.push_back({"ssr.last_camera", objects.last_camera.Get()});
  out.outputs.push_back({"ssr.previous_camera", objects.previous_camera.Get()});
  out.replayed = true;
  return out;
}

static Replayed ReplayAoFilter(const Frame& frame, const json& event, Kept* kept) {
  auto& runtime = aof::internal::runtime;
  auto& objects = runtime.objects;
  const auto settings_bytes = Bytes(frame, event, "ao_filter.settings_cpu");
  if (settings_bytes.size() != sizeof(aof::Settings)) return {.why = "ao_filter.settings_cpu missing"};
  std::memcpy(&runtime.settings, settings_bytes.data(), sizeof(aof::Settings));
  ComPtr<ID3D11Resource> ao_resource;
  ComPtr<ID3D11Resource> depth_resource;
  auto game_ao = MakeView(frame, event, "ao_filter.game_ao", &ao_resource);
  auto depth = MakeView(frame, event, "ao_filter.depth", &depth_resource);
  const Copy* output = Find(frame, event, "before", "ao_filter.output");
  if (!game_ao || !depth || output == nullptr) return {.why = "no AO, depth or output"};
  kept->Add(ao_resource);
  kept->Add(depth_resource);
  kept->views.push_back(game_ao);
  kept->views.push_back(depth);
  if (!aof::internal::Prepare(device.Get(), output->desc["Width"], output->desc["Height"])) return {.why = "Prepare failed"};
  objects.uploaded = {};
  runtime.frame = static_cast<uint64_t>(Value(event, "ao_filter.frame"));
  runtime.filtered_frame = static_cast<uint64_t>(Value(event, "ao_filter.filtered_frame", -1.0));
  objects.history_index = static_cast<size_t>(Value(event, "ao_filter.history_index"));
  objects.filtered_size = {static_cast<UINT>(Value(event, "ao_filter.filtered_width")), static_cast<UINT>(Value(event, "ao_filter.filtered_height"))};
  twattila::frame_history::render_width.store(static_cast<uint32_t>(Value(event, "frame_history.render_width")));
  twattila::frame_history::render_height.store(static_cast<uint32_t>(Value(event, "frame_history.render_height")));
  objects.previous_camera.Reset();
  if (const Copy* copy = Find(frame, event, "before", "ao_filter.previous_camera"); copy != nullptr) Make(*copy).As(&objects.previous_camera);
  const auto buffer = [&](const char* name) -> ID3D11Buffer* {
    const Copy* copy = Find(frame, event, "before", name);
    return (copy == nullptr) ? nullptr : static_cast<ID3D11Buffer*>(kept->Add(Make(*copy)));
  };
  ID3D11Buffer* camera = buffer("ao_filter.camera");
  ID3D11Buffer* sun_mapping = buffer("ao_filter.sun_mapping");
  Replayed out;
  out.outputs = {{"ao_filter.history_0", objects.history[0].texture.Get()},
                 {"ao_filter.history_1", objects.history[1].texture.Get()},
                 {"ao_filter.output", objects.output.texture.Get()}};
  for (const auto& [name, resource] : out.outputs) Fill(resource, Find(frame, event, "before", name));
  Expect(frame, event, {"ao_filter.shader_settings"});
  Stamp(0);
  aof::internal::Filter(context.Get(), game_ao.Get(), depth.Get(), Value(event, "ao_filter.msaa") != 0.0, camera, sun_mapping);
  Stamp(1);
  dump::internal::checking.store(false);
  out.mismatches = dump::internal::state.mismatches;
  out.outputs.push_back({"ao_filter.previous_camera", objects.previous_camera.Get()});
  out.replayed = true;
  return out;
}

static Replayed Replay(const Frame& frame, const json& event, Kept* kept, bool with_race_map) {
  const std::string name = event["name"];
  if (name == "sss.compute") return ReplaySss(frame, event, kept, with_race_map);
  if (name == "ssr.build") return ReplaySsrBuild(frame, event, kept);
  if (name == "ssr.resolve") return ReplaySsrResolve(frame, event, kept);
  if (name == "ao_filter.filter") return ReplayAoFilter(frame, event, kept);
  if (name == "smoke.map") return {.why = "not replayed: its list's order differs run to run (an input, out of H0)"};
  if (name == "ao.draw" || name == "bounce.sun_draw") return {.why = "not replayed: draw-hosted (its befores and afters are dumped)"};
  return {.why = "not replayed yet"};
}

// One event's outputs against `want` (the dump's after, or another run's): per output its differing bytes.
static bool Check(const std::string& label, const Frame& frame, const json& event, const Replayed& replayed,
                  const std::function<const Copy*(const std::string&)>& want, const RaceResult* race) {
  bool equal = true;
  if (!replayed.mismatches.empty()) {
    for (const auto& mismatch : replayed.mismatches) std::printf("  %s: constant bytes differ: %s (void)\n", label.c_str(), mismatch.c_str());
    return false;
  }
  // The joint rule: in each Bend output a race pixel's value is one writer's (Compare clears the slots whose value it
  // does not hold); `no_writer` marks the pixels where an output held none.
  std::vector<uint8_t> matching;
  std::vector<bool> no_writer;
  if (race != nullptr) {
    matching.resize(race->count.size());
    no_writer.resize(race->count.size());
  }
  for (const auto& [name, resource] : replayed.outputs) {
    const Copy* expected = want(name);
    if (expected == nullptr || resource == nullptr) continue;
    int channel = -1;
    if (name == "sss.occlusion") {
      channel = 0;
    } else if (name == "sss.penumbra") {
      channel = 1;
    } else if (name == "sss.tint") {
      channel = 2;
    }
    if (race != nullptr && channel >= 0) {
      for (size_t pixel = 0; pixel < matching.size(); ++pixel) {
        matching[pixel] = static_cast<uint8_t>((1u << std::min<uint32_t>(race->count[pixel], 4)) - 1u);
      }
    }
    const auto compared = Compare(Read(resource, *expected), *expected, (channel >= 0) ? race : nullptr, channel, &matching);
    if (race != nullptr && channel >= 0) {
      for (size_t pixel = 0; pixel < matching.size(); ++pixel) {
        if (race->Raced(pixel) && matching[pixel] == 0) {
          no_writer[pixel] = true;
        }
      }
    }
    equal = equal && compared.bytes == 0;
    std::printf("  %s %s%s: %s", label.c_str(), name.c_str(), (replayed.injected && name == "sss.soft") ? " (from the dump's occlusion)" : "",
                (compared.bytes == 0) ? "equal" : "DIFFERS");
    if (compared.bytes != 0) std::printf(", %zu bytes apart (first at byte %zu)", compared.bytes, compared.first);
    std::printf("\n");
  }
  if (race != nullptr) {
    size_t raced = 0;
    size_t bad = 0;
    for (size_t pixel = 0; pixel < matching.size(); ++pixel) {
      if (!race->Raced(pixel)) continue;
      ++raced;
      if (no_writer[pixel]) ++bad;
    }
    equal = equal && bad == 0;
    std::printf("  %s race pixels: %zu, %zu with an output no writer gives\n", label.c_str(), raced, bad);
  }
  return equal;
}

// The events this build does not replay (the header's list): listed, not failed.
constexpr std::array<const char*, 4> NOT_REPLAYED = {"sss.lights", "smoke.map", "ao.draw", "bounce.sun_draw"};
// --skip's events: not replayed without failing the frame.
static std::vector<std::string> skipped_events;

// Every event of a frame replayed against its afters (H0); `out`: the replay's outputs written there (raw, per event).
static bool Exact(const Frame& frame, const fs::path& out, const fs::path& against) {
  bool all = true;
  size_t replayed_count = 0;
  for (const auto& event : frame.manifest["events"]) {
    Kept kept;
    const std::string label = std::format("{}#{}", event["name"].get<std::string>(), event["index"].get<int>());
    // The race set first (the race-map build: its outputs kept), then the real build's outputs.
    RaceResult race;
    bool have_race = false;
    std::map<std::string, Copy> race_outputs;
    if (event["name"] == "sss.compute" && race_map.loaded) {
      Kept race_kept;
      const auto racing = Replay(frame, event, &race_kept, true);
      if (racing.replayed) {
        race = racing.race;
        have_race = true;
        for (const auto& [name, resource] : racing.outputs) {
          const Copy* like = Find(frame, event, "after", name);
          if (like == nullptr || resource == nullptr) continue;
          Copy copy = *like;
          const auto data = Read(resource, *like);
          for (size_t sub = 0; sub < data.size(); ++sub) copy.subresources[sub].data = data[sub];
          race_outputs[name] = std::move(copy);
        }
      }
    }
    const auto replayed = Replay(frame, event, &kept, false);
    if (!replayed.replayed) {
      const std::string name = event["name"];
      const bool listed = std::ranges::find(NOT_REPLAYED, name) != NOT_REPLAYED.end() || std::ranges::find(skipped_events, name) != skipped_events.end();
      std::printf("  %s: %s%s\n", label.c_str(), replayed.why.c_str(), listed ? "" : " (FAIL: replayable here; --skip names it)");
      all = all && listed;
      continue;
    }
    ++replayed_count;
    const bool compare_other = !against.empty();
    // Against the dump (H0); --settings' sss cases changed what the dump has: shown, not counted.
    const bool dump_counts = !(event["name"] == "sss.compute" && settings_override.contains("sss"));
    if (!Check(dump_counts ? label : label + " (settings changed: not counted)", frame, event, replayed,
               [&](const std::string& name) { return Find(frame, event, "after", name); }, have_race ? &race : nullptr)) {
      all = all && !dump_counts;
    }
    // The race-map build computes what the real build does: equal outside the race pixels, a writer's value at them.
    if (have_race && !Check(label + " against the race-map build", frame, event, replayed, [&](const std::string& name) {
          const auto found = race_outputs.find(name);
          return (found == race_outputs.end()) ? nullptr : &found->second;
        }, &race)) {
      all = false;
    }
    if (!out.empty()) {
      const fs::path folder = out / label;
      fs::create_directories(folder);
      for (const auto& [name, resource] : replayed.outputs) {
        const Copy* like = Find(frame, event, "after", name);
        if (like == nullptr || resource == nullptr) continue;
        const auto data = Read(resource, *like);
        for (size_t sub = 0; sub < data.size(); ++sub) {
          std::ofstream(folder / std::format("{}.{}.bin", name, sub), std::ios::binary).write(reinterpret_cast<const char*>(data[sub].data()), data[sub].size());
        }
      }
      if (have_race) {
        std::ofstream(folder / "race_count.bin", std::ios::binary).write(reinterpret_cast<const char*>(race.count.data()), race.count.size() * 4);
        std::ofstream(folder / "race_tuples.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(race.tuples.data()), race.tuples.size() * sizeof(RaceTuple));
      }
    }
    if (compare_other) {
      // Lane against base: the other exe's outputs (its --out), its race map with this one's (the joint rule).
      const fs::path folder = against / label;
      RaceResult other_race;
      if (have_race && fs::exists(folder / "race_count.bin")) {
        const auto count = ReadFile(folder / "race_count.bin");
        const auto tuples = ReadFile(folder / "race_tuples.bin");
        other_race.count.resize(count.size() / 4);
        std::memcpy(other_race.count.data(), count.data(), count.size());
        other_race.tuples.resize(tuples.size() / sizeof(RaceTuple));
        std::memcpy(other_race.tuples.data(), tuples.data(), tuples.size());
        // The race sets and each race pixel's writers (as a multiset) must be the same; an overflow on both sides passes.
        size_t apart = 0;
        for (size_t pixel = 0; pixel < std::min(race.count.size(), other_race.count.size()); ++pixel) {
          const bool raced = race.Raced(pixel);
          if (raced != other_race.Raced(pixel)) {
            ++apart;
            continue;
          }
          if (!raced || (race.count[pixel] > 4 && other_race.count[pixel] > 4)) continue;
          auto key = [](const RaceTuple& t) { return std::make_tuple(t.dispatch, t.occlusion, t.penumbra, t.tint[0], t.tint[1], t.tint[2]); };
          std::vector<decltype(key(RaceTuple{}))> mine;
          std::vector<decltype(key(RaceTuple{}))> theirs;
          for (uint32_t slot = 0; slot < race.count[pixel]; ++slot) mine.push_back(key(race.tuples[(pixel * 4) + slot]));
          for (uint32_t slot = 0; slot < other_race.count[pixel]; ++slot) theirs.push_back(key(other_race.tuples[(pixel * 4) + slot]));
          std::ranges::sort(mine);
          std::ranges::sort(theirs);
          if (mine != theirs) ++apart;
        }
        std::printf("  %s against %s: %zu race pixels whose writers differ\n", label.c_str(), against.string().c_str(), apart);
        all = all && apart == 0;
      }
      std::vector<Copy> theirs;
      for (const auto& [name, resource] : replayed.outputs) {
        const Copy* like = Find(frame, event, "after", name);
        if (like == nullptr) continue;
        Copy copy = *like;
        for (auto& subresource : copy.subresources) subresource.data = ReadFile(folder / std::format("{}.{}.bin", name, subresource.index));
        theirs.push_back(std::move(copy));
      }
      size_t index = 0;
      std::map<std::string, size_t> by_name;
      for (const auto& [name, resource] : replayed.outputs) {
        if (Find(frame, event, "after", name) != nullptr) by_name[name] = index++;
      }
      if (!Check(label + " against", frame, event, replayed, [&](const std::string& name) {
            const auto found = by_name.find(name);
            return (found == by_name.end()) ? nullptr : &theirs[found->second];
          }, have_race ? &other_race : nullptr)) {
        all = false;
      }
    }
  }
  std::printf("%s: %zu events replayed, %s\n", frame.folder.string().c_str(), replayed_count, all ? "all equal" : "NOT all equal");
  return all && replayed_count != 0;
}

// --time: each replayed event `n` times on the GPU (timestamps around its entry; its befores set again each time), one
// block of lab rows (ans.schema.json's block: rows by event, median / mad / trimmed mean / min / max in ms).
static bool Time(const Frame& frame, int n, const fs::path& out) {
  D3D11_QUERY_DESC disjoint_desc = {.Query = D3D11_QUERY_TIMESTAMP_DISJOINT};
  D3D11_QUERY_DESC stamp_desc = {.Query = D3D11_QUERY_TIMESTAMP};
  json rows = json::object();
  for (const auto& event : frame.manifest["events"]) {
    std::vector<double> ms;
    const std::string label = std::format("{}#{}", event["name"].get<std::string>(), event["index"].get<int>());
    for (int run = 0; run < n + 3; ++run) {
      ComPtr<ID3D11Query> disjoint;
      ComPtr<ID3D11Query> begin;
      ComPtr<ID3D11Query> end;
      device->CreateQuery(&disjoint_desc, disjoint.GetAddressOf());
      device->CreateQuery(&stamp_desc, begin.GetAddressOf());
      device->CreateQuery(&stamp_desc, end.GetAddressOf());
      Kept kept;
      // Replay fills the befores first (not timed), the stamps bracket the module's entry alone.
      context->Begin(disjoint.Get());
      stamps = {begin.Get(), end.Get()};
      const auto replayed = Replay(frame, event, &kept, false);
      stamps = {};
      context->End(disjoint.Get());
      if (!replayed.replayed) break;
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data = {};
      while (context->GetData(disjoint.Get(), &data, sizeof(data), 0) == S_FALSE) std::this_thread::yield();
      UINT64 a = 0;
      UINT64 b = 0;
      while (context->GetData(begin.Get(), &a, sizeof(a), 0) == S_FALSE) std::this_thread::yield();
      while (context->GetData(end.Get(), &b, sizeof(b), 0) == S_FALSE) std::this_thread::yield();
      if (run >= 3 && !data.Disjoint) ms.push_back(static_cast<double>(b - a) * 1000.0 / static_cast<double>(data.Frequency));
    }
    if (ms.empty()) continue;
    std::ranges::sort(ms);
    const double median = ms[ms.size() / 2];
    std::vector<double> deviations;
    for (double value : ms) deviations.push_back(std::fabs(value - median));
    std::ranges::sort(deviations);
    const double mad = deviations[deviations.size() / 2];
    const size_t trim = ms.size() / 10;
    double sum = 0.0;
    for (size_t index = trim; index < ms.size() - trim; ++index) sum += ms[index];
    size_t outliers = 0;
    for (double value : ms) outliers += (std::fabs(value - median) > 5.0 * std::max(mad, 1e-6)) ? 1 : 0;
    rows[label] = {{"median", median}, {"mad", mad}, {"trimmed_mean", sum / static_cast<double>(ms.size() - (2 * trim))}, {"min", ms.front()},
                   {"max", ms.back()}, {"n", ms.size()}, {"outliers", outliers}};
    std::printf("  %s: %.4f ms (median of %zu)\n", label.c_str(), median, ms.size());
  }
  // The block's other fields: what the replay knows (the lab's runner adds arm, t and other_gpu).
  DXGI_QUERY_VIDEO_MEMORY_INFO memory = {};
  ComPtr<IDXGIDevice> dxgi;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIAdapter3> adapter3;
  if (SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(adapter.GetAddressOf())) && SUCCEEDED(adapter.As(&adapter3))) {
    adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory);
  }
  const json block = {{"rows", rows}, {"gpu_clock_mhz", 1}, {"vram_usage_mb", memory.CurrentUsage >> 20}, {"vram_budget_mb", memory.Budget >> 20},
                      {"reset", "none"}, {"timing", "passes"}, {"frames", n}, {"warm", 3}, {"raw", nullptr}, {"source", frame.folder.string()}};
  std::ofstream(out) << block.dump(1) << "\n";
  return !rows.empty();
}

// ---- The driver profile (S14, read only) ------------------------------------------------------------------------------

static void Profile(const std::vector<std::wstring>& exes) {
#if __has_include(<nvapi.h>)
  if (NvAPI_Initialize() != NVAPI_OK) {
    std::printf("profile: no NVAPI\n");
    return;
  }
  NvDRSSessionHandle session = nullptr;
  if (NvAPI_DRS_CreateSession(&session) != NVAPI_OK || NvAPI_DRS_LoadSettings(session) != NVAPI_OK) {
    std::printf("profile: the driver's settings could not be read\n");
    return;
  }
  const std::array<std::pair<NvU32, const char*>, 8> KEYS = {{{ANISO_MODE_SELECTOR_ID, "anisotropy mode"},
                                                             {ANISO_MODE_LEVEL_ID, "anisotropy level"},
                                                             {QUALITY_ENHANCEMENTS_ID, "texture filtering quality"},
                                                             {LODBIASADJUST_ID, "LOD bias"},
                                                             {AUTO_LODBIASADJUST_ID, "driver-controlled LOD bias"},
                                                             {PS_TEXFILTER_NO_NEG_LODBIAS_ID, "negative LOD bias clamp"},
                                                             {PS_TEXFILTER_ANISO_OPTS2_ID, "anisotropic sample optimization"},
                                                             {PS_TEXFILTER_BILINEAR_IN_ANISO_ID, "trilinear optimization"}}};
  for (const auto& exe : exes) {
    NVDRS_APPLICATION application = {.version = NVDRS_APPLICATION_VER};
    NvDRSProfileHandle profile = nullptr;
    NvAPI_UnicodeString name = {};
    for (size_t index = 0; index < exe.size() && index + 1 < NVAPI_UNICODE_STRING_MAX; ++index) name[index] = static_cast<NvU16>(exe[index]);
    const bool own = NvAPI_DRS_FindApplicationByName(session, name, &profile, &application) == NVAPI_OK;
    if (!own) NvAPI_DRS_GetBaseProfile(session, &profile);
    std::printf("profile of %ls (%s):\n", exe.c_str(), own ? "its own" : "none: the base profile");
    for (const auto& [key, label] : KEYS) {
      NVDRS_SETTING setting = {.version = NVDRS_SETTING_VER};
      if (NvAPI_DRS_GetSetting(session, profile, key, &setting) == NVAPI_OK) {
        std::printf("  %s: %#x (%s)\n", label, setting.u32CurrentValue, setting.settingLocation == NVDRS_CURRENT_PROFILE_LOCATION ? "set there" : "inherited");
      } else {
        std::printf("  %s: the driver's default\n", label);
      }
    }
  }
  NvAPI_DRS_DestroySession(session);
#else
  std::printf("profile: built without nvapi.h\n");
#endif
}

// ---- The self test ----------------------------------------------------------------------------------------------------

// A synthetic frame for each module: a ground plane and boxes seen from above at an angle (the depth), a camera in
// Attila's layout, the sun's direction, a reflection target and an AO texture of noise.
struct Synthetic {
  UINT width = 320;
  UINT height = 180;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11ShaderResourceView> depth_srv;
  // The same depth in a depth-stencil texture (R24G8, as the game's): SSR reads it, so its dump input is one D3D11 takes
  // no initial data for (Make drops the binding).
  ComPtr<ID3D11Texture2D> depth_stencil;
  ComPtr<ID3D11ShaderResourceView> depth_stencil_srv;
  ComPtr<ID3D11Buffer> camera;
  ComPtr<ID3D11Buffer> lighting;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11Texture2D> ao;
  ComPtr<ID3D11ShaderResourceView> ao_srv;
};

static Synthetic MakeSynthetic(uint32_t seed) {
  Synthetic s;
  std::mt19937 random(seed);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  // Depth: standard (near 0); a plane rising toward the top with boxes standing on it.
  std::vector<float> depth(size_t{s.width} * s.height);
  for (UINT y = 0; y < s.height; ++y) {
    for (UINT x = 0; x < s.width; ++x) {
      float d = 0.9f + 0.09f * (1.f - static_cast<float>(y) / static_cast<float>(s.height));
      for (int box = 0; box < 6; ++box) {
        const float bx = 40.f + 45.f * static_cast<float>(box);
        const float by = 60.f + 15.f * static_cast<float>(box % 3);
        if (std::fabs(static_cast<float>(x) - bx) < 12.f && static_cast<float>(y) > by && static_cast<float>(y) < by + 50.f) d -= 0.03f;
      }
      depth[(y * s.width) + x] = d;
    }
  }
  const D3D11_TEXTURE2D_DESC depth_desc = {s.width, s.height, 1, 1, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA depth_data = {depth.data(), s.width * 4, 0};
  device->CreateTexture2D(&depth_desc, &depth_data, s.depth.GetAddressOf());
  device->CreateShaderResourceView(s.depth.Get(), nullptr, s.depth_srv.GetAddressOf());
  // The depth-stencil copy: 24-bit depth packed into an R24G8 texture made with it, copied into the depth-stencil one.
  std::vector<uint32_t> packed(depth.size());
  for (size_t texel = 0; texel < depth.size(); ++texel) packed[texel] = static_cast<uint32_t>(std::lround(depth[texel] * 16777215.f));
  ComPtr<ID3D11Texture2D> packed_depth;
  const D3D11_TEXTURE2D_DESC packed_desc = {s.width, s.height, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA packed_data = {packed.data(), s.width * 4, 0};
  device->CreateTexture2D(&packed_desc, &packed_data, packed_depth.GetAddressOf());
  const D3D11_TEXTURE2D_DESC stencil_desc = {s.width, s.height, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT,
                                             D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL, 0, 0};
  device->CreateTexture2D(&stencil_desc, nullptr, s.depth_stencil.GetAddressOf());
  context->CopyResource(s.depth_stencil.Get(), packed_depth.Get());
  const D3D11_SHADER_RESOURCE_VIEW_DESC stencil_view = {.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
  device->CreateShaderResourceView(s.depth_stencil.Get(), &stencil_view, s.depth_stencil_srv.GetAddressOf());
  // Camera: projection (60 degrees high) at c5, its inverse at c17, view projection = projection (view identity) at c9 / c21,
  // inv_view identity at c13, the position 0, near / far at c25.
  std::array<float, 30 * 4> constants = {};
  const float tan_half = std::tan(30.f * 3.14159265f / 180.f);
  const float aspect = static_cast<float>(s.width) / static_cast<float>(s.height);
  const float n = 0.5f;
  const float f = 3000.f;
  const std::array<std::array<float, 4>, 4> projection = {{{1.f / (tan_half * aspect), 0, 0, 0}, {0, 1.f / tan_half, 0, 0}, {0, 0, f / (f - n), 1}, {0, 0, -n * f / (f - n), 0}}};
  const std::array<std::array<float, 4>, 4> inverse = {{{tan_half * aspect, 0, 0, 0}, {0, tan_half, 0, 0}, {0, 0, 0, (n - f) / (n * f)}, {0, 0, 1, 1.f / n}}};
  const std::array<std::array<float, 4>, 4> identity = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  const auto put = [&](UINT base, const auto& m) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) constants[((base + j) * 4) + i] = m[i][j];
    }
  };
  put(1, identity);
  put(5, projection);
  put(9, projection);
  put(13, identity);
  put(17, inverse);
  put(21, inverse);
  constants[25 * 4] = n;
  constants[(25 * 4) + 1] = f;
  constants[27 * 4] = static_cast<float>(s.width);
  constants[(27 * 4) + 1] = static_cast<float>(s.height);
  const auto buffer = [&](const void* data, UINT bytes, ComPtr<ID3D11Buffer>* made) {
    const D3D11_BUFFER_DESC desc = {bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    const D3D11_SUBRESOURCE_DATA initial = {data, 0, 0};
    device->CreateBuffer(&desc, &initial, made->GetAddressOf());
  };
  buffer(constants.data(), sizeof(constants), &s.camera);
  // The sun's travel direction at float 1 (Attila's lighting_VS_PS): from the upper left, down into the screen.
  std::array<float, 16> lighting = {};
  const float sun[3] = {0.5f, -0.6f, 0.62f};
  std::copy(std::begin(sun), std::end(sun), lighting.begin() + 1);
  buffer(lighting.data(), sizeof(lighting), &s.lighting);
  // The reflection target (RGBA16F noise) and the AO (R8 noise).
  std::vector<uint16_t> colour(size_t{s.width} * s.height * 4);
  for (auto& value : colour) value = DirectX::PackedVector::XMConvertFloatToHalf(unit(random));
  const D3D11_TEXTURE2D_DESC target_desc = {s.width, s.height, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
  const D3D11_SUBRESOURCE_DATA target_data = {colour.data(), s.width * 8, 0};
  device->CreateTexture2D(&target_desc, &target_data, s.target.GetAddressOf());
  std::vector<uint8_t> ao(size_t{s.width} * s.height);
  for (auto& value : ao) value = static_cast<uint8_t>(unit(random) * 255.f);
  const D3D11_TEXTURE2D_DESC ao_desc = {s.width, s.height, 1, 1, DXGI_FORMAT_R8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA ao_data = {ao.data(), s.width, 0};
  device->CreateTexture2D(&ao_desc, &ao_data, s.ao.GetAddressOf());
  device->CreateShaderResourceView(s.ao.Get(), nullptr, s.ao_srv.GetAddressOf());
  return s;
}

// The module calls one frame makes (sss: plain with Softness in frame 1, extended with Softer and Auto Thickness from
// frame 2: its penumbra, made in frame 2, is an event input from frame 3 on).
static void RunSynthetic(const Synthetic& s, int frame) {
  auto& sss_runtime = sss::internal::runtime;
  ++sss_runtime.frame;
  sss_runtime.settings = {.enabled = true, .samples = 120.f, .softness = (frame == 1) ? 1.5f : 0.f};
  if (frame >= 2) {
    sss_runtime.settings.auto_thickness = true;
    sss_runtime.settings.sun_disk = 2.f;
  }
  if (sss::internal::Prepare(device.Get(), s.width, s.height)) {
    sss::internal::Compute(context.Get(), s.depth_srv.Get(), false, s.camera.Get(), s.lighting.Get(), 1, nullptr, nullptr);
    sss_runtime.computed_frame = sss_runtime.frame;
  }
  auto& ssr_runtime = ssr::internal::runtime;
  ++ssr_runtime.frame;
  ssr_runtime.enabled = true;
  ssr_runtime.denoise = true;
  D3D11_TEXTURE2D_DESC target_desc = {};
  s.target->GetDesc(&target_desc);
  if (ssr::internal::Prepare(device.Get(), s.width, s.height) && ssr::internal::PrepareResolve(device.Get(), target_desc)) {
    ssr::internal::Build(context.Get(), s.depth_stencil_srv.Get());
    constexpr FLOAT HIT[4] = {0.25f, 0.75f, 0.f, 0.f};
    context->ClearRenderTargetView(ssr_runtime.objects.hit_rtv.Get(), HIT);
    ssr::internal::Resolve(context.Get(), s.target.Get(), s.depth_stencil_srv.Get(), s.camera.Get());
  }
  auto& ao_runtime = aof::internal::runtime;
  ++ao_runtime.frame;
  ao_runtime.settings = {.temporal = true, .edge_fix = true, .denoise = true};
  if (aof::internal::Prepare(device.Get(), s.width, s.height)) {
    aof::internal::Filter(context.Get(), s.ao_srv.Get(), s.depth_srv.Get(), false, s.camera.Get(), nullptr);
    ao_runtime.filtered_frame = ao_runtime.frame;
  }
}

static bool SelfTest(const fs::path& folder) {
  // Dump two frames through pass_dump (as the game's presents would), then replay each.
  fs::create_directories(folder);
  const fs::path label = folder / "self";
  std::error_code ignored;
  fs::remove_all(label, ignored);
  dump::internal::Start(folder);
  {
    const std::unique_lock lock(dump::internal::state.mutex);
    dump::internal::Arm("3 self");
  }
  const Synthetic synthetic = MakeSynthetic(7);
  for (int frame = 1; frame <= 3; ++frame) {
    dump::internal::Present(context.Get());  // closes the last, starts this one
    RunSynthetic(synthetic, frame);
  }
  for (int present = 0; present < 3; ++present) dump::internal::Present(context.Get());  // closes 3, reads all
  std::vector<fs::path> frames;
  for (int wait = 0; wait < 600 && frames.size() < 3; ++wait) {
    frames.clear();
    if (fs::exists(label)) {
      for (const auto& entry : fs::directory_iterator(label)) {
        if (fs::exists(entry.path() / "done.txt")) frames.push_back(entry.path());
      }
    }
    if (frames.size() < 3) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  CHECK(dump::internal::Stop(60000));
  CHECK(frames.size() == 3);
  std::ranges::sort(frames);
  bool all = !frames.empty();
  for (const auto& path : frames) {
    Frame frame;
    CHECK(LoadFrame(path, &frame));
    // Every module's event is there, in order, with its names; uses.tsv has its marks' rows.
    std::vector<std::string> names;
    for (const auto& event : frame.manifest["events"]) names.push_back(event["name"]);
    CHECK((names == std::vector<std::string>{"sss.compute", "ssr.build", "ssr.resolve", "ao_filter.filter"}));
    const auto uses = ReadFile(path / "uses.tsv");
    const std::string uses_text(uses.begin(), uses.end());
    CHECK(uses_text.find("\tsss_setup\tuav\tu0:0\t") != std::string::npos && uses_text.find("\tssr_resolve\tsrv\tt2:0\t") != std::string::npos);
    // SSR's depth is a depth-stencil texture (the R24G8 case): its copy keeps the binding, and Exact replays it.
    const Copy* ssr_depth = Find(frame, frame.manifest["events"][1], "before", "ssr.depth");
    CHECK(ssr_depth != nullptr && (ssr_depth->desc["BindFlags"].get<UINT>() & D3D11_BIND_DEPTH_STENCIL) != 0
          && ssr_depth->desc["Format"].get<int>() == DXGI_FORMAT_R24G8_TYPELESS);
    std::printf("self test: %s (%zu copies)\n", path.string().c_str(), frame.copies.size());
    all = Exact(frame, path / "replay_out", {}) && all;
    // The replay against its own outputs (the --against path, its race maps the same): equal.
    all = Exact(frame, {}, path / "replay_out") && all;
    // Controls (each must fail): Bend one sample fewer (sss.compute's outputs differ: --settings' path, no byte check),
    // and a constant byte changed (the pass must not run: void).
    const json& sss_event = frame.manifest["events"][0];
    sss::Settings settings = {};
    const auto settings_bytes = Bytes(frame, sss_event, "sss.settings_cpu");
    std::memcpy(&settings, settings_bytes.data(), sizeof(settings));
    settings_override = {{"sss", {{"samples", settings.samples - 4.f}}}};
    {
      Kept kept;
      const auto fewer = Replay(frame, sss_event, &kept, false);
      const bool control = Check("CONTROL fewer samples", frame, sss_event, fewer, [&](const std::string& name) { return Find(frame, sss_event, "after", name); }, nullptr);
      CHECK(!control);
    }
    settings_override = json();
    {
      Frame changed = frame;
      const int index = changed.manifest["events"][2]["bytes"]["ssr.resolve_settings"];
      changed.copies[static_cast<size_t>(index)].subresources[0].data[0] ^= 1;
      Kept kept;
      const auto voided = Replay(changed, changed.manifest["events"][2], &kept, false);
      CHECK(voided.replayed && voided.mismatches.size() == 1 && voided.mismatches[0].starts_with("ssr.resolve_settings: word 0"));
      std::printf("  CONTROL changed constant byte: %s\n", voided.mismatches.empty() ? "not seen" : voided.mismatches[0].c_str());
    }
  }
  CHECK(all);
  return failures == 0;
}

// ---- Main -------------------------------------------------------------------------------------------------------------

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::vector<std::string> arguments(argv + 1, argv + argc);
  const auto option = [&](const char* name) -> std::string {
    const auto found = std::ranges::find(arguments, name);
    return (found == arguments.end() || found + 1 == arguments.end()) ? std::string() : *(found + 1);
  };
  const bool warp = std::ranges::find(arguments, "--warp") != arguments.end();
  if (std::ranges::find(arguments, "--profile") != arguments.end()) {
    std::vector<std::wstring> exes = {L"attila64.exe", L"Attila.exe"};
    for (auto name = std::ranges::find(arguments, "--profile") + 1; name != arguments.end() && !name->starts_with("--"); ++name) {
      exes.push_back(fs::path(*name).wstring());
    }
    wchar_t own[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, own, MAX_PATH);
    exes.push_back(fs::path(own).filename().wstring());
    Profile(exes);
    return 0;
  }
  constexpr D3D_FEATURE_LEVEL LEVEL = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &LEVEL, 1, D3D11_SDK_VERSION,
                               device.GetAddressOf(), nullptr, context.GetAddressOf()))) {
    std::printf("no D3D11 device\n");
    return 2;
  }
  if (const auto csos = option("--race-map"); !csos.empty()) LoadRaceMap(csos);
  for (std::string skip = option("--skip"); !skip.empty();) {
    const size_t comma = skip.find(',');
    skipped_events.push_back(skip.substr(0, comma));
    skip = (comma == std::string::npos) ? std::string() : skip.substr(comma + 1);
  }
  if (const auto folder = option("--self-test"); !folder.empty()) {
    const bool passed = SelfTest(folder);
    std::printf("%s (%d failures)\n", passed ? "PASS" : "FAILED", failures);
    return passed ? 0 : 1;
  }
  if (const auto path = option("--exact"); !path.empty()) {
    Frame frame;
    if (!LoadFrame(path, &frame)) return 1;
    return Exact(frame, option("--out"), option("--against")) ? 0 : 1;
  }
  if (const auto n = option("--time"); !n.empty()) {
    const auto found = std::ranges::find(arguments, "--time");
    Frame frame;
    if (found + 2 >= arguments.end() || !LoadFrame(*(found + 2), &frame)) return 1;
    return Time(frame, std::stoi(n), option("--out")) ? 0 : 1;
  }
  if (const auto matrix = option("--settings"); !matrix.empty()) {
    const auto found = std::ranges::find(arguments, "--settings");
    Frame frame;
    if (found + 2 >= arguments.end() || !LoadFrame(*(found + 2), &frame)) return 1;
    // Each case's outputs into <out>\<case>\ (no constant-byte check, and the dump's afters shown, not counted: the
    // settings changed them); with --against <the other exe's out>, each case against its own there (counted).
    const std::string against = option("--against");
    bool all = true;
    for (const auto& item : json::parse(std::ifstream(matrix))) {
      settings_override = item;
      const std::string name = item["name"];
      std::printf("case %s\n", name.c_str());
      const bool equal = Exact(frame, fs::path(option("--out")) / name, against.empty() ? fs::path() : fs::path(against) / name);
      all = all && (against.empty() || equal);
    }
    return all ? 0 : 1;
  }
  std::printf("usage: replay.exe --self-test <folder> | --exact <frame> | --time <n> <frame> --out <file> | --settings <matrix> <frame> --out <dir> | --profile\n");
  return 2;
}
