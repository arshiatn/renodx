#pragma once
// The resident (recorder builds only: main.cpp's "0 serve <recording>\frame_<N> <control folder> [--nsight]"): a loaded
// recording kept on the GPU and played on the lab's commands, resident protocol v1 (tools/tw/lab/resident_protocol.md and
// its schemas; gto_design §2 S2 and addendum B). After the first loop (more while bakes or rooms are on their way) the
// scene's temporal state is kept (the snapshot); a reset gives it back, a structure change since loads the recording again.

#include <bcrypt.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "../dlaa_bridge.h"

#pragma comment(lib, "bcrypt.lib")

namespace twattila::dlaa_helper::serve {

inline constexpr std::string_view PROTOCOL = "renodx-resident v1";

// The snapshot's classes (gto_design §2 X1). Every RecordedFields() field is temporal (given back at a reset) but these
// structural ones, which a reset must find unchanged (else a full reload). FRAME_FIELDS hold played frames (uint32_t;
// UINT32_MAX: none): a seed's offset moves them.
inline constexpr std::array<std::string_view, 12> STRUCTURAL_FIELDS = {
    "builds", "set_generation", "units_record", "units_masked", "clusters", "dxr12", "cluster_room", "built_limit", "cluster_limit", "template_bytes",
    "dynamic_uvs", "set_drops"};
inline constexpr std::array<std::string_view, 18> FRAME_FIELDS = {
    "ambient_frame", "lights_frame",  "water_frame", "path_ambient_frame", "kept_copy_frame", "reuse_frame",    "sky_frame",
    "ratio_frame",   "reflect_frame", "sharc_frame", "paths_frame",        "copied_frame",    "paths_copied_frame", "light_frame",
    "cache_frame",   "specular_frame", "sun_traced_frame", "history_used"};
// A ResourceRows() row is temporal by its own tags or by a temporal index field (the index rule), else structural: then
// it must be listed here.
inline constexpr std::array<std::string_view, 5> STRUCTURAL_RESOURCES = {"records", "uvs", "normals", "build_rows", "set_pool"};

constexpr bool Listed(const auto& list, std::string_view name) { return std::ranges::find(list, name) != list.end(); }

template <typename Row>
constexpr bool TemporalResource(const Row& row) {
  return row.Tagged() || std::ranges::any_of(row.index, [](const char* field) { return field != nullptr && !Listed(STRUCTURAL_FIELDS, field); });
}

template <typename Scene>
constexpr auto FieldNames() {
  return std::apply([](const auto&... field) { return std::array<std::string_view, sizeof...(field)>{field.first...}; }, Scene::RecordedFields());
}

// The classes fit the scene: every listed field is one of its fields (a frame field never structural), every index field
// is one and has its row's class, every structural row is listed and every listed row is structural.
template <typename Scene>
constexpr bool Classified() {
  constexpr auto fields = FieldNames<Scene>();
  for (const std::string_view name : STRUCTURAL_FIELDS) {
    if (!Listed(fields, name)) return false;
  }
  for (const std::string_view name : FRAME_FIELDS) {
    if (!Listed(fields, name) || Listed(STRUCTURAL_FIELDS, name)) return false;
  }
  constexpr auto rows = Scene::ResourceRows();
  for (const auto& row : rows) {
    const bool temporal = TemporalResource(row);
    for (const char* field : row.index) {
      if (field != nullptr && (!Listed(fields, field) || Listed(STRUCTURAL_FIELDS, field) == temporal)) return false;
    }
    if (temporal == Listed(STRUCTURAL_RESOURCES, row.name)) return false;
  }
  return std::ranges::all_of(STRUCTURAL_RESOURCES, [&](std::string_view name) {
    return std::ranges::any_of(rows, [&](const auto& row) { return name == row.name; });
  });
}

template <typename Scene>
std::vector<std::string_view> TemporalResources() {
  std::vector<std::string_view> names;
  for (const auto& row : Scene::ResourceRows()) {
    if (TemporalResource(row)) names.emplace_back(row.name);
  }
  return names;
}

// A scene's snapshot (TakeSnapshot) and the resets back to it (ResetTo). Scene: RtScene12, or the tests' fake (FieldSpans,
// SaveResources, RestoreResources, Structure, SaveObjects, RestoreObjects, RecordedFields, ResourceRows).
template <typename Scene>
struct Snapshot {
  std::vector<std::vector<std::byte>> fields;  // FieldSpans' order
  std::vector<typename Scene::SavedResource> resources;
  typename Scene::Objects objects;
  uint64_t structure = 0;
  uint64_t bytes = 0;
  bool vram = false;
};

template <typename Scene>
bool TakeSnapshot(Scene* scene, bool vram, Snapshot<Scene>* snapshot, std::string* why) {
  *snapshot = {};
  // Its objects first: they take the last trace's placed counter into its field.
  snapshot->objects = scene->SaveObjects();
  for (const auto& [name, bytes] : scene->FieldSpans()) snapshot->fields.emplace_back(bytes.begin(), bytes.end());
  snapshot->structure = scene->Structure();
  snapshot->vram = vram;
  return scene->SaveResources(TemporalResources<Scene>(), vram, &snapshot->resources, &snapshot->bytes, why);
}

enum class ResetResult { DONE, FULL_RELOAD, FAILED };
// The scene back to `snapshot`, its frame fields `shift` frames later. FULL_RELOAD: a structural field or the structure
// changed (`why` says which; nothing given back). FAILED: the copies failed.
template <typename Scene>
ResetResult ResetTo(Scene* scene, const Snapshot<Scene>& snapshot, uint32_t shift, std::string* why) {
  const auto spans = scene->FieldSpans();
  for (size_t index = 0; index < spans.size(); ++index) {
    if (Listed(STRUCTURAL_FIELDS, spans[index].first) && !std::ranges::equal(spans[index].second, snapshot.fields[index])) {
      *why = std::format("{} changed since the snapshot", spans[index].first);
      return ResetResult::FULL_RELOAD;
    }
  }
  if (scene->Structure() != snapshot.structure) {
    *why = "the meshes, cluster sets, pools or GPU resources changed since the snapshot";
    return ResetResult::FULL_RELOAD;
  }
  for (size_t index = 0; index < spans.size(); ++index) {
    const auto& [name, bytes] = spans[index];
    if (Listed(STRUCTURAL_FIELDS, name)) continue;
    std::ranges::copy(snapshot.fields[index], bytes.begin());
    for (size_t at = 0; Listed(FRAME_FIELDS, name) && at + sizeof(uint32_t) <= bytes.size(); at += sizeof(uint32_t)) {
      uint32_t frame = 0;
      std::memcpy(&frame, bytes.data() + at, sizeof(frame));
      if (frame != UINT32_MAX) frame += shift;
      std::memcpy(bytes.data() + at, &frame, sizeof(frame));
    }
  }
  if (!scene->RestoreResources(snapshot.resources, why)) return ResetResult::FAILED;
  scene->RestoreObjects(snapshot.objects, shift);
  return ResetResult::DONE;
}

// A row's numbers over a block's frames: median, MAD, the mean without the outliers (past 5 MAD), min, max.
struct RowStats {
  double median = 0.0;
  double mad = 0.0;
  double trimmed_mean = 0.0;
  double min = 0.0;
  double max = 0.0;
  uint32_t n = 0;
  uint32_t outliers = 0;
};
inline RowStats Summarize(std::vector<double> values) {
  const auto median_of = [](std::vector<double>* sorted) {
    std::ranges::sort(*sorted);
    const size_t half = sorted->size() / 2;
    return (sorted->size() % 2 != 0) ? (*sorted)[half] : ((*sorted)[half - 1] + (*sorted)[half]) / 2.0;
  };
  RowStats stats = {.n = static_cast<uint32_t>(values.size())};
  if (values.empty()) return stats;
  stats.median = median_of(&values);
  std::vector<double> deviations;
  for (const double value : values) deviations.push_back(std::abs(value - stats.median));
  stats.mad = median_of(&deviations);
  double kept = 0.0;
  for (const double value : values) {
    if (stats.mad > 0.0 && std::abs(value - stats.median) > 5.0 * stats.mad) {
      ++stats.outliers;
    } else {
      kept += value;
    }
  }
  stats.trimmed_mean = kept / (stats.n - stats.outliers);
  stats.min = values.front();
  stats.max = values.back();
  return stats;
}

// raw_<n>.bin (a v1.1 proposal): "TWRW", version 1, rows, frames (u32 each), then each row's name length (u32), name and a
// float64 ms a frame (NaN: not in that frame). Little-endian.
inline std::vector<uint8_t> RawFile(const std::vector<std::pair<std::string, std::vector<double>>>& rows, uint32_t frames) {
  std::vector<uint8_t> file;
  const auto put = [&](const void* data, size_t bytes) { file.insert(file.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes); };
  const std::array<uint32_t, 4> header = {0x57525754u, 1u, static_cast<uint32_t>(rows.size()), frames};
  put(header.data(), sizeof(header));
  for (const auto& [name, values] : rows) {
    const auto length = static_cast<uint32_t>(name.size());
    put(&length, sizeof(length));
    put(name.data(), name.size());
    put(values.data(), values.size() * sizeof(double));
  }
  return file;
}

// moments_<output>.bin (gtl_'s v1.1 proposal b): this header, then MOMENTS_FILE_PLANES planes of width x height float4
// (rows from the top): mean, M2, the last frame, sum of |x_t - x_t-1|, the lag-1 co-moment about the pairs' means (pairs
// (x_t, x_t-1), t = 2..k), the pairs' means of x_t and of x_t-1. variance = M2 / (k - 1); lag-1 covariance = co-moment /
// (k - 2).
struct MomentsHeader {
  std::array<char, 4> magic = {'T', 'W', 'M', 'O'};
  uint32_t version = 1;
  uint32_t frames = 0;  // k
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t active_width = 0;
  uint32_t active_height = 0;
  uint32_t channels = 4;
  uint32_t dtype = 1;   // 1: float32
  uint32_t format = 0;  // the output's DXGI format
  uint32_t planes = 0;
};
inline constexpr uint32_t MOMENTS_FILE_PLANES = 7;
// A moments file from rt_moments.hlsl's planes after `frames` frames (mean, M2, the first frame s, C = sum over t >= 2 of
// (x_t - s)(x_t-1 - s), the last frame, travel; the shift by s keeps fp32's sums small): with Sx = k (mean - s) and
// Sy = Sx - (last - s) the pairs' sums about s, the co-moment is C - Sx Sy / (k - 1).
inline std::vector<uint8_t> MomentsFile(MomentsHeader header, std::span<const std::vector<uint8_t>> planes) {
  header.planes = MOMENTS_FILE_PLANES;
  const size_t values = planes[0].size() / sizeof(float);
  std::vector<uint8_t> file(sizeof(header) + (MOMENTS_FILE_PLANES * values * sizeof(float)));
  std::memcpy(file.data(), &header, sizeof(header));
  const auto in = [&](size_t plane, size_t at) {
    float value = 0.f;
    std::memcpy(&value, planes[plane].data() + (at * sizeof(float)), sizeof(value));
    return static_cast<double>(value);
  };
  const auto out = [&](size_t plane, size_t at, double value) {
    const auto stored = static_cast<float>(value);
    std::memcpy(file.data() + sizeof(header) + (((plane * values) + at) * sizeof(float)), &stored, sizeof(stored));
  };
  const double pairs = std::max<double>(header.frames, 2.0) - 1.0;
  for (size_t at = 0; at < values; ++at) {
    const double mean = in(0, at);
    const double first = in(2, at);
    const double last = in(4, at);
    const double sum_x = header.frames * (mean - first);
    const double sum_y = sum_x - (last - first);
    out(0, at, mean);
    out(1, at, in(1, at));
    out(2, at, last);
    out(3, at, in(5, at));
    out(4, at, in(3, at) - (sum_x * sum_y / pairs));
    out(5, at, first + (sum_x / pairs));
    out(6, at, first + (sum_y / pairs));
  }
  return file;
}

// `bytes`' SHA-256 in hex (empty: BCrypt failed).
inline std::string Sha256(std::span<const uint8_t> bytes) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  std::array<UCHAR, 32> digest = {};
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return {};
  const bool hashed = BCRYPT_SUCCESS(BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), digest.data(),
                                                static_cast<ULONG>(digest.size())));
  BCryptCloseAlgorithmProvider(algorithm, 0);
  std::string hex;
  for (const UCHAR byte : digest) hex += std::format("{:02x}", byte);
  return hashed ? hex : std::string();
}

// A stem's file: a signed DXIL container (dxc -Fo) of a compute shader: "DXBC", a digest, its size the file's, its parts
// inside it, a DXIL part whose program is a compute shader (kind 5). False: `why` says what is wrong.
inline bool CheckStem(std::span<const uint8_t> bytes, std::string* why) {
  const auto u32 = [&](size_t at) {
    uint32_t value = 0;
    if (at + 4 <= bytes.size()) std::memcpy(&value, bytes.data() + at, 4);
    return value;
  };
  if (bytes.size() < 32 || std::memcmp(bytes.data(), "DXBC", 4) != 0) {
    *why = "not a DXIL container (no DXBC header)";
    return false;
  }
  if (std::all_of(bytes.begin() + 4, bytes.begin() + 20, [](uint8_t byte) { return byte == 0; })) {
    *why = "not signed (its digest is zero: dxc without its validator)";
    return false;
  }
  if (u32(24) != bytes.size()) {
    *why = std::format("its size field {} is not the file's {} bytes", u32(24), bytes.size());
    return false;
  }
  const uint32_t parts = u32(28);
  for (uint32_t part = 0; part < parts && 32 + (part * 4) + 4 <= bytes.size(); ++part) {
    const uint32_t at = u32(32 + (part * 4));
    if (at + 8 > bytes.size() || at + 8 + u32(at + 4) > bytes.size()) {
      *why = std::format("part {} is past the file's end", part);
      return false;
    }
    if (std::memcmp(bytes.data() + at, "DXIL", 4) == 0) {
      if ((u32(at + 8) >> 16) != 5) {
        *why = std::format("its program is not a compute shader (kind {})", u32(at + 8) >> 16);
        return false;
      }
      return true;
    }
  }
  *why = "no DXIL part";
  return false;
}

// A command's file written atomically: `<path>.tmp` closed, then renamed over `path` (tried again while a reader holds it).
inline bool WriteAtomic(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::path temporary = path;
  temporary += L".tmp";
  if (std::ofstream file(temporary, std::ios::binary | std::ios::trunc); !file.write(text.data(), static_cast<std::streamsize>(text.size()))) return false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    Sleep(10);
  }
  return false;
}

// Now as ISO 8601 local time with its offset ("2026-11-22T14:03:07+01:00").
inline std::string NowIso() {
  SYSTEMTIME local = {};
  GetLocalTime(&local);
  TIME_ZONE_INFORMATION zone = {};
  const DWORD kind = GetTimeZoneInformation(&zone);
  const LONG bias = -(zone.Bias + ((kind == TIME_ZONE_ID_DAYLIGHT) ? zone.DaylightBias : ((kind == TIME_ZONE_ID_STANDARD) ? zone.StandardBias : 0)));
  return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}{}{:02}:{:02}", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
                     (bias < 0) ? '-' : '+', std::abs(bias) / 60, std::abs(bias) % 60);
}

// A light animation's frame (light_anim; a v1.1 proposal: {"schema": "renodx-light-anim v1", "frames": [{"sun": [x, y, z] or
// null, "lights": [{"index", "position", "intensity", "on"}]}]}): toward the sun, and lights changed by their place in the
// block's list (position as given, colour times intensity, off: no reach and no colour).
struct LightChange {
  uint32_t index = 0;
  std::optional<std::array<float, 3>> position;
  float intensity = 1.f;
  bool on = true;
};
struct LightFrame {
  std::optional<std::array<float, 3>> sun;
  std::vector<LightChange> lights;
};
// `text`'s frames. False: `why` says what does not fit the schema.
inline bool ParseLightAnimation(std::string_view text, std::vector<LightFrame>* frames, std::string* why) {
  try {
    const nlohmann::json parsed = nlohmann::json::parse(text);
    if (parsed.at("schema") != "renodx-light-anim v1" || !parsed.at("frames").is_array() || parsed.at("frames").empty()) {
      *why = "not a renodx-light-anim v1 file with frames";
      return false;
    }
    frames->clear();
    for (const nlohmann::json& frame : parsed.at("frames")) {
      LightFrame& made = frames->emplace_back();
      if (frame.contains("sun") && !frame.at("sun").is_null()) made.sun = frame.at("sun").get<std::array<float, 3>>();
      for (const nlohmann::json& light : frame.value("lights", nlohmann::json::array())) {
        LightChange& change = made.lights.emplace_back(LightChange{.index = light.at("index").get<uint32_t>()});
        if (light.contains("position")) change.position = light.at("position").get<std::array<float, 3>>();
        change.intensity = light.value("intensity", 1.f);
        change.on = light.value("on", true);
        if (change.index >= dlaa_bridge::RT_ALL_LIGHTS) {
          *why = std::format("light index {} is past {}", change.index, dlaa_bridge::RT_ALL_LIGHTS);
          return false;
        }
      }
    }
  } catch (const nlohmann::json::exception& error) {
    *why = std::format("light animation: {}", error.what());
    return false;
  }
  return true;
}
// `frame`'s light changes in `block` (lights past its count left alone).
inline void ApplyLights(const LightFrame& frame, dlaa_bridge::Shared* block) {
  for (const LightChange& change : frame.lights) {
    if (change.index >= std::min(block->rt_light_count, dlaa_bridge::RT_ALL_LIGHTS)) continue;
    const bool first = change.index < dlaa_bridge::RT_MAX_LIGHTS;
    float* light = first ? block->rt_lights[change.index] : block->rt_lights_more[change.index - dlaa_bridge::RT_MAX_LIGHTS];
    float* colour = first ? block->rt_light_colours[change.index] : block->rt_light_colours_more[change.index - dlaa_bridge::RT_MAX_LIGHTS];
    if (change.position) std::copy(change.position->begin(), change.position->end(), light);
    for (size_t channel = 0; channel < 3; ++channel) colour[channel] = change.on ? colour[channel] * change.intensity : 0.f;
    if (!change.on) light[3] = 0.f;
  }
}

// config's fields: the numeric Shared fields a lab arm may set (rt_light_count: a cap on the recorded count). A name may end
// in "@trace", "@bounce" or "@reflections": that request kind's alone.
using SharedMember = std::variant<uint32_t dlaa_bridge::Shared::*, float dlaa_bridge::Shared::*>;
inline constexpr std::array<std::pair<std::string_view, SharedMember>, 15> SHARED_FIELDS = {{
    {"rt_shadow_rays", &dlaa_bridge::Shared::rt_shadow_rays},
    {"rt_sun_softness", &dlaa_bridge::Shared::rt_sun_softness},
    {"rt_light_size", &dlaa_bridge::Shared::rt_light_size},
    {"rt_ambient_range", &dlaa_bridge::Shared::rt_ambient_range},
    {"rt_ambient_rays", &dlaa_bridge::Shared::rt_ambient_rays},
    {"rt_grass_shadow_range", &dlaa_bridge::Shared::rt_grass_shadow_range},
    {"rt_bounce_rays", &dlaa_bridge::Shared::rt_bounce_rays},
    {"rt_bounce_range", &dlaa_bridge::Shared::rt_bounce_range},
    {"rt_path_bounces", &dlaa_bridge::Shared::rt_path_bounces},
    {"rt_path_samples", &dlaa_bridge::Shared::rt_path_samples},
    {"rt_reflection_range", &dlaa_bridge::Shared::rt_reflection_range},
    {"rt_kept_frames", &dlaa_bridge::Shared::rt_kept_frames},
    {"rt_nrd_off", &dlaa_bridge::Shared::rt_nrd_off},
    {"rt_scene_flags", &dlaa_bridge::Shared::rt_scene_flags},
    {"rt_light_count", &dlaa_bridge::Shared::rt_light_count},
}};
inline constexpr std::array<std::string_view, 3> REQUEST_KINDS = {"trace", "bounce", "reflections"};  // rt_request 1, 2, 3
struct FieldEdit {
  SharedMember member;
  double value = 0.0;
  uint32_t request = 0;  // 0: every kind
  bool cap = false;
};
// `name` (config's) as an edit. False: `why` says it is unknown.
inline bool ParseField(std::string_view name, double value, FieldEdit* edit, std::string* why) {
  const size_t at = name.find('@');
  const std::string_view field = name.substr(0, at);
  const auto found = std::ranges::find(SHARED_FIELDS, field, &std::pair<std::string_view, SharedMember>::first);
  const auto kind = (at == std::string_view::npos) ? REQUEST_KINDS.end() : std::ranges::find(REQUEST_KINDS, name.substr(at + 1));
  if (found == SHARED_FIELDS.end() || (at != std::string_view::npos && kind == REQUEST_KINDS.end()) || value < 0.0) {
    *why = std::format("unknown field {} (or a negative value)", name);
    return false;
  }
  *edit = {.member = found->second, .value = value, .request = (kind == REQUEST_KINDS.end()) ? 0u : static_cast<uint32_t>(kind - REQUEST_KINDS.begin()) + 1,
           .cap = field == "rt_light_count"};
  return true;
}
inline void ApplyField(const FieldEdit& edit, uint32_t request, dlaa_bridge::Shared* block) {
  if (edit.request != 0 && edit.request != request) return;
  std::visit(
      [&](auto member) {
        using Value = std::remove_reference_t<decltype(block->*member)>;
        const auto value = static_cast<Value>(edit.value);
        block->*member = edit.cap ? std::min(block->*member, value) : value;
      },
      edit.member);
}

}  // namespace twattila::dlaa_helper::serve

#if defined(RENODX_RECORDER)
namespace twattila::dlaa_helper::serve {

static_assert(Classified<RtScene12>(), "rt_serve.h's classes do not fit RtScene12's RecordedFields / ResourceRows (a new field or row?)");

// The resident: Serve answers the control folder's commands until "stop", a second device loss or 10 minutes without one.
class Resident {
 public:
  struct Options {
    std::filesystem::path recording;
    std::filesystem::path control;
    std::wstring helper_folder;
    bool nsight = false;
    std::function<void(const std::string&)> log;
    std::function<void(ID3D12CommandQueue*)> frame_boundary;  // Nsight Graphics' (trace)
  };
  explicit Resident(Options options) : options_(std::move(options)) {}

  // The exit code: 0 stopped or idle, 1 the recording could not be loaded, 4 the device lost twice.
  int Serve() {
    FILETIME creation = {};
    FILETIME unused = {};
    GetProcessTimes(GetCurrentProcess(), &creation, &unused, &unused, &unused);
    start_ = (uint64_t{creation.dwHighDateTime} << 32) | creation.dwLowDateTime;
    std::wstring exe(MAX_PATH, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), MAX_PATH));
    std::ifstream exe_file(exe, std::ios::binary);
    const std::vector<uint8_t> exe_bytes((std::istreambuf_iterator<char>(exe_file)), std::istreambuf_iterator<char>());
    build_hash_ = Sha256(exe_bytes);
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))) || FAILED(factory->EnumAdapters1(0, adapter_.GetAddressOf()))
        || FAILED(adapter_.As(&memory_adapter_))) {
      options_.log("[TW resident] FAILED: no DXGI adapter");
      return 1;
    }
    FindClock();
    std::jthread heartbeat([this](const std::stop_token& stop) {
      std::mutex wait;
      std::condition_variable_any woken;
      for (std::unique_lock lock(wait); !stop.stop_requested(); woken.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; })) {
        WriteAtomic(options_.control / L"alive.json", Alive().dump());
      }
    });
    std::string why;
    if (!Load(&why)) {
      options_.log(std::format("[TW resident] FAILED to load {}: {}", options_.recording.string(), why));
      return 1;
    }
    SetState("idle");
    auto last_command = std::chrono::steady_clock::now();
    for (uint32_t expect = 1;;) {
      const std::filesystem::path path = options_.control / std::format("cmd_{}.json", expect);
      std::error_code error;
      if (!std::filesystem::exists(path, error)) {
        // A later command with this one missing: answered REFUSED, nothing runs.
        uint32_t later = UINT32_MAX;
        for (const auto& entry : std::filesystem::directory_iterator(options_.control, error)) {
          uint32_t n = 0;
          if (const std::string name = entry.path().filename().string(); sscanf_s(name.c_str(), "cmd_%u.json", &n) == 1 && name == std::format("cmd_{}.json", n)
                                                                          && n > expect) {
            later = std::min(later, n);
          }
        }
        if (later != UINT32_MAX) {
          Answer(later, "status", NowIso(), {.status = "REFUSED", .error = std::format("cmd_{} skips {}", later, expect)});
          expect = later + 1;
          continue;
        }
        if (std::chrono::steady_clock::now() - last_command > std::chrono::minutes(10)) {
          options_.log("[TW resident] Stopped: 10 minutes without a command.");
          return 0;
        }
        Sleep(10);
        continue;
      }
      const std::string started = NowIso();
      nlohmann::json command;
      for (int attempt = 0; attempt < 50 && command.is_null(); ++attempt) {
        std::ifstream file(path, std::ios::binary);
        command = nlohmann::json::parse(file, nullptr, false);
        if (command.is_discarded()) {
          command = nullptr;
          Sleep(10);
        }
      }
      const uint32_t n = expect++;
      last_command = std::chrono::steady_clock::now();
      {
        std::scoped_lock lock(mutex_);
        last_n_ = n;
        last_cmd_time_ = started;
      }
      const std::string name = (command.is_object() && command.contains("cmd") && command["cmd"].is_string()) ? command["cmd"].get<std::string>() : "";
      const bool known = Listed(COMMANDS, name);
      if (!command.is_object() || command.value("protocol", "") != PROTOCOL || command.value("n", 0u) != n || !known || !command.contains("args")
          || !command["args"].is_object()) {
        Answer(n, known ? name : "status", started, {.status = "REFUSED", .error = std::format("cmd_{} is not a {} command numbered {}", n, PROTOCOL, n)});
        continue;
      }
      SetState("running " + name);
      Reply reply;
      try {
        reply = Handle(name, command["args"], n);
      } catch (const nlohmann::json::exception& bad) {
        reply = {.status = "REFUSED", .error = std::format("bad args: {}", bad.what())};
      }
      if (reply.lost) {
        losses_.fetch_add(1);
        options_.log(std::format("[TW resident] The device was lost at cmd_{} ({}): {}", n, name, reply.error));
        if (losses_.load() >= 2) {
          Answer(n, name, started, {.status = "FAILED", .error = "device lost twice, STOP", .lost = true});
          return 4;
        }
        std::string restart;
        if (!Load(&restart)) {
          Answer(n, name, started, {.status = "FAILED", .error = "device lost, the restart failed: " + restart, .lost = true});
          return 4;
        }
        reply = {.status = "FAILED", .error = "device lost, restarted", .lost = true};
      }
      Answer(n, name, started, reply);
      SetState("idle");
      last_command = std::chrono::steady_clock::now();
      if (name == "stop" && reply.status == "OK") {
        options_.log("[TW resident] Stopped by the lab.");
        return 0;
      }
    }
  }

 private:
  static constexpr std::array<std::string_view, 9> COMMANDS = {"config", "run", "dump", "trace", "moments", "seed", "light_anim", "status", "stop"};
  // The lab's config (config): flags as played (none: as recorded), grass micromaps' level at load (-1: as recorded), Shared
  // fields, swapped stems (their DXIL and SHA-256), the burn.
  struct Config {
    std::optional<uint32_t> trace_flags;
    std::optional<uint32_t> path_flags;
    int micromap_level = -1;
    std::vector<FieldEdit> fields;
    std::map<std::string, std::vector<uint8_t>> stems;
    std::map<std::string, std::string> stem_hashes;
    std::string burn_pass;
    uint32_t burn_loops = 0;
  };
  struct Reply {
    std::string status = "OK";
    std::string error;
    nlohmann::json result = nlohmann::json::object();
    bool lost = false;
  };

  void SetState(std::string state) {
    std::scoped_lock lock(mutex_);
    state_ = std::move(state);
  }

  // The heartbeat's fields (alive.json, status).
  nlohmann::json Alive() {
    DXGI_QUERY_VIDEO_MEMORY_INFO memory = {};
    memory_adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory);
    std::scoped_lock lock(mutex_);
    return {
        {"protocol", PROTOCOL},
        {"pid", GetCurrentProcessId()},
        {"start", start_},
        {"recording", options_.recording.string()},
        {"build_hash", build_hash_},
        {"state", state_},
        {"vram", {{"usage_mb", static_cast<double>(memory.CurrentUsage) / (1 << 20)}, {"budget_mb", static_cast<double>(memory.Budget) / (1 << 20)}}},
        {"fence", fence_ ? fence_->GetCompletedValue() : 0},
        {"last_n", last_n_},
        {"last_cmd_time", last_cmd_time_.empty() ? nlohmann::json(nullptr) : nlohmann::json(last_cmd_time_)},
        {"time", NowIso()},
        {"losses", losses_.load()},
        {"snapshot", snapshot_kind_},
        {"snapshot_mb", snapshot_mb_},
    };
  }

  void Answer(uint32_t n, std::string_view name, const std::string& started, const Reply& reply) {
    const nlohmann::json answer = {
        {"protocol", PROTOCOL},
        {"n", n},
        {"cmd", name},
        {"status", reply.status},
        {"error", reply.error.empty() ? nlohmann::json(nullptr) : nlohmann::json(reply.error)},
        {"started", started},
        {"ended", NowIso()},
        {"device_lost", reply.lost},
        {"result", (reply.status == "OK") ? reply.result : nlohmann::json(nullptr)},
    };
    if (!WriteAtomic(options_.control / std::format("ans_{}.json", n), answer.dump())) options_.log(std::format("[TW resident] ans_{}.json could not be written", n));
  }

  // The GPU's graphics clock (NVAPI, the adapter's GPU by its LUID; 0: none).
  void FindClock() {
#if __has_include(<nvapi.h>)
    DXGI_ADAPTER_DESC1 desc = {};
    adapter_->GetDesc1(&desc);
    std::array<NvPhysicalGpuHandle, NVAPI_MAX_PHYSICAL_GPUS> gpus = {};
    NvU32 count = 0;
    if (NvAPI_Initialize() != NVAPI_OK || NvAPI_EnumPhysicalGPUs(gpus.data(), &count) != NVAPI_OK) return;
    for (NvU32 index = 0; index < count; ++index) {
      LUID luid = {};
      NvLogicalGpuHandle logical = nullptr;
      NV_LOGICAL_GPU_DATA data = {.version = NV_LOGICAL_GPU_DATA_VER1, .pOSAdapterId = &luid};
      if (NvAPI_GetLogicalGPUFromPhysicalGPU(gpus[index], &logical) == NVAPI_OK && NvAPI_GPU_GetLogicalGpuInfo(logical, &data) == NVAPI_OK
          && luid.LowPart == desc.AdapterLuid.LowPart
          && luid.HighPart == desc.AdapterLuid.HighPart) {
        clock_gpu_ = gpus[index];
      }
    }
#endif
  }
  [[nodiscard]] double ClockMhz() const {
#if __has_include(<nvapi.h>)
    NV_GPU_CLOCK_FREQUENCIES clocks = {.version = NV_GPU_CLOCK_FREQUENCIES_VER, .ClockType = NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ};
    if (clock_gpu_ != nullptr && NvAPI_GPU_GetAllClockFrequencies(clock_gpu_, &clocks) == NVAPI_OK && clocks.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].bIsPresent != 0) {
      return clocks.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency / 1000.0;
    }
#endif
    return 0.0;
  }
  [[nodiscard]] DXGI_QUERY_VIDEO_MEMORY_INFO Memory() const {
    DXGI_QUERY_VIDEO_MEMORY_INFO memory = {};
    memory_adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory);
    return memory;
  }

  // The recording on a new scene (the last one let go first), its first loop played (more while it has not settled, at most
  // 5), the snapshot taken (in video memory when twice its size is free, else read back), the config's stems and burn in
  // it, the seed's offset on it. False: `why` says why.
  bool Load(std::string* why) {
    {
      std::scoped_lock lock(mutex_);
      fence_.Reset();
      snapshot_kind_ = "none";
      snapshot_mb_ = 0.0;
    }
    snapshot_ = {};
    scene_.reset();
    SetState("loading 0 %");
    scene_ = std::make_unique<RtScene12>();
    if (!scene_->Initialize(adapter_.Get(), options_.helper_folder, false)) {
      *why = scene_->Status();
      return false;
    }
    {
      std::scoped_lock lock(mutex_);
      fence_ = scene_->OwnFence();
    }
    if (!scene_->LoadPlayback(options_.recording, {config_.micromap_level, -1},
                              [this](uint32_t percent) { SetState(std::format("loading {} %", std::min(percent, 99u))); }, why)) {
      return false;
    }
    options_.log("[TW resident] Loaded: " + *why);
    loaded_level_ = config_.micromap_level;
    const auto [first_frame, last_frame] = scene_->PlaybackFrames();
    span_ = last_frame - first_frame + 1;
    frames_.clear();
    for (size_t request = 0; request < scene_->PlaybackRequests(); ++request) {
      if (frames_.empty() || scene_->PlaybackHeader(frames_.back().front()).frame != scene_->PlaybackHeader(request).frame) frames_.emplace_back();
      frames_.back().push_back(request);
    }
    for (const auto& [stem, code] : config_.stems) {
      if (!scene_->SwapStem(stem, code, why)) return false;
    }
    if (!scene_->SetBurn(config_.burn_pass, config_.burn_loops, why)) return false;
    seed_ = 0;
    position_ = 0;
    std::string settling;
    for (loops_ = 0; loops_ == 0 || !scene_->Settled(&settling); ++loops_) {
      if (loops_ == 5) {
        *why = "not settled after 5 loops: " + settling;
        return false;
      }
      for (size_t frame = 0; frame < frames_.size(); ++frame) {
        if (!PlayFrame(frame, loops_ * span_, nullptr, why)) return false;
      }
    }
    if (!TakeSnapshot(scene_.get(), false, &snapshot_, why)) return false;
    const DXGI_QUERY_VIDEO_MEMORY_INFO memory = Memory();
    if (memory.Budget > memory.CurrentUsage && memory.Budget - memory.CurrentUsage >= snapshot_.bytes * 2) {
      std::string vram_why;
      if (Snapshot<RtScene12> in_vram; TakeSnapshot(scene_.get(), true, &in_vram, &vram_why)) {
        snapshot_ = std::move(in_vram);
      } else {
        options_.log("[TW resident] The snapshot stays read back: " + vram_why);
      }
    }
    {
      std::scoped_lock lock(mutex_);
      snapshot_kind_ = snapshot_.vram ? "vram" : "host";
      snapshot_mb_ = static_cast<double>(snapshot_.bytes) / (1 << 20);
    }
    options_.log(std::format("[TW resident] Snapshot after {} loops of {} frames ({} requests): {:.0f} MB {}", loops_, frames_.size(), scene_->PlaybackRequests(),
                             static_cast<double>(snapshot_.bytes) / (1 << 20), snapshot_.vram ? "in video memory" : "read back"));
    played_since_reset_ = 0;
    if (pending_seed_ != 0) {
      seed_ = pending_seed_;
      return ResetTo(scene_.get(), snapshot_, seed_, why) == ResetResult::DONE;
    }
    return true;
  }

  // The scene back at the snapshot, the pending seed's offset on it: `*mode` "reset", or "full_reload" (the structure
  // changed, or the config's micromap level is not the loaded one). False: `why` says why.
  bool Reset(std::string* mode, std::string* why) {
    if (loaded_level_ != config_.micromap_level) {
      *mode = "full_reload";
      return Load(why);
    }
    std::string changed;
    switch (ResetTo(scene_.get(), snapshot_, pending_seed_, &changed)) {
      case ResetResult::DONE:
        *mode = "reset";
        seed_ = pending_seed_;
        position_ = 0;
        played_since_reset_ = 0;
        return true;
      case ResetResult::FULL_RELOAD:
        options_.log("[TW resident] Full reload: " + changed);
        *mode = "full_reload";
        return Load(why);
      default:
        *why = changed;
        return false;
    }
  }

  // Recorded frame `frame`'s requests `offset` frames later, the config's and light animation's edits in their blocks; with
  // `rows` their GPU times added there. False: `why` says why.
  bool PlayFrame(size_t frame, uint32_t offset, std::vector<std::pair<std::string, double>>* rows, std::string* why) {
    const LightFrame* lights = light_animation_.empty() ? nullptr : &light_animation_[played_since_reset_ % light_animation_.size()];
    const std::optional<std::array<float, 4>> sun =
        (lights != nullptr && lights->sun) ? std::optional(std::array<float, 4>{(*lights->sun)[0], (*lights->sun)[1], (*lights->sun)[2], 1.f}) : std::nullopt;
    for (const size_t request : frames_[frame]) {
      const uint32_t kind = scene_->PlaybackHeader(request).request;
      const auto edit = [&](dlaa_bridge::Shared* block) {
        if (config_.trace_flags) block->rt_flags = *config_.trace_flags;
        if (config_.path_flags) block->rt_path_flags = *config_.path_flags;
        for (const FieldEdit& field : config_.fields) ApplyField(field, kind, block);
        if (lights != nullptr) ApplyLights(*lights, block);
      };
      const auto boundary = [&] {
        if (tracing_) options_.frame_boundary(scene_->Queue());
      };
      if (!scene_->PlayRequest(request, offset, edit, sun ? &*sun : nullptr, boundary, why)) return false;
      for (const auto& [row, ms] : scene_->ReadServeTimes()) {
        if (rows == nullptr) continue;
        const auto found = std::ranges::find(*rows, row, &std::pair<std::string, double>::first);
        if (found == rows->end()) {
          rows->emplace_back(row, ms);
        } else {
          found->second += ms;
        }
      }
    }
    ++played_since_reset_;
    return true;
  }
  // The next frame since the snapshot (the frames in order, each loop `span_` frames after the last; the seed's offset).
  bool Advance(std::vector<std::pair<std::string, double>>* rows, std::string* why) {
    const auto frames = static_cast<uint32_t>(frames_.size());
    const bool played = PlayFrame(position_ % frames, (span_ * (loops_ + (position_ / frames))) + seed_, rows, why);
    ++position_;
    return played;
  }
  // A command's failure: the device lost (the scene's), or another.
  Reply Failed(const std::string& why) { return {.status = "FAILED", .error = why, .lost = !scene_ || scene_->Lost()}; }
  // `args`' keys all among `keys` (the schema's additionalProperties false); else `why`.
  static bool OnlyKeys(const nlohmann::json& args, std::initializer_list<std::string_view> keys, std::string* why) {
    for (const auto& [key, value] : args.items()) {
      if (std::ranges::find(keys, key) == keys.end()) {
        *why = std::format("unknown argument {}", key);
        return false;
      }
    }
    return true;
  }
  // A folder argument under the control folder (made). False: outside it.
  bool ControlFolder(const std::string& folder, std::filesystem::path* path, std::string* why) const {
    std::error_code error;
    const std::filesystem::path control = std::filesystem::weakly_canonical(options_.control, error);
    *path = std::filesystem::weakly_canonical(std::filesystem::path(folder).is_absolute() ? std::filesystem::path(folder) : options_.control / folder, error);
    const auto [end, rest] = std::ranges::mismatch(control, *path);
    if (error || end != control.end() || *path == control) {
      *why = std::format("folder {} is not under the control folder", folder);
      return false;
    }
    std::filesystem::create_directories(*path, error);
    return true;
  }

  Reply Handle(const std::string& name, const nlohmann::json& args, uint32_t n) {
    std::string why;
    if (name == "status" || name == "stop") {
      if (!OnlyKeys(args, {}, &why)) return {.status = "REFUSED", .error = why};
      return {.result = (name == "status") ? Alive() : nlohmann::json::object()};
    }
    if (name == "seed") {
      if (!OnlyKeys(args, {"offset"}, &why) || args.at("offset").get<int64_t>() < 0) return {.status = "REFUSED", .error = why.empty() ? "a negative offset" : why};
      pending_seed_ = args.at("offset").get<uint32_t>();
      return {};
    }
    if (name == "light_anim") {
      if (!OnlyKeys(args, {"path"}, &why)) return {.status = "REFUSED", .error = why};
      const std::filesystem::path path = std::filesystem::path(args.at("path").get<std::string>()).is_absolute() ? std::filesystem::path(args.at("path").get<std::string>())
                                                                                                               : options_.control / args.at("path").get<std::string>();
      std::ifstream file(path, std::ios::binary);
      const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      std::vector<LightFrame> frames;
      if (!file || !ParseLightAnimation(text, &frames, &why)) return {.status = "REFUSED", .error = why.empty() ? std::format("{} could not be read", path.string()) : why};
      light_animation_ = std::move(frames);
      return {};
    }
    if (name == "config") return Configure(args);
    if (name == "run") return Run(args, n);
    if (name == "dump") {
      std::filesystem::path folder;
      if (!OnlyKeys(args, {"folder", "frames"}, &why) || !ControlFolder(args.at("folder").get<std::string>(), &folder, &why)) return {.status = "REFUSED", .error = why};
      // `frames` (gtl_'s v1.1 proposal a; 1 without it): the first frames after the reset, named as recorded (their played
      // frames are their offset later), within one loop.
      const auto count = args.value("frames", 1u);
      if (count == 0 || count > frames_.size()) return {.status = "REFUSED", .error = std::format("dump frames {} is not 1 to {}", count, frames_.size())};
      std::string mode;
      if (!Reset(&mode, &why)) return Failed(why);
      const uint32_t offset = span_ * loops_ + seed_;
      scene_->RecordFrames(folder, scene_->PlaybackHeader(frames_.front().front()).frame + offset, scene_->PlaybackHeader(frames_[count - 1].front()).frame + offset,
                           false, true, offset);
      bool played = true;
      for (uint32_t frame = 0; frame < count && played; ++frame) played = Advance(nullptr, &why);
      const std::string summary = scene_->FinishRecording();
      if (!played) return Failed(why);
      options_.log(std::format("[TW resident] cmd_{} dump ({}): {}", n, mode, summary));
      return {.result = FolderResult(folder)};
    }
    if (name == "moments") {
      std::filesystem::path folder;
      if (!OnlyKeys(args, {"k", "outputs", "folder"}, &why) || !ControlFolder(args.at("folder").get<std::string>(), &folder, &why)) return {.status = "REFUSED", .error = why};
      const auto k = args.at("k").get<uint32_t>();
      const DXGI_QUERY_VIDEO_MEMORY_INFO memory = Memory();
      if (k < 2 || !scene_->StartMoments(args.at("outputs").get<std::vector<std::string>>(), (memory.Budget > memory.CurrentUsage) ? memory.Budget - memory.CurrentUsage : 0,
                                         &why)) {
        return {.status = "REFUSED", .error = (k < 2) ? "k below 2" : why};
      }
      std::string mode;
      if (!Reset(&mode, &why)) return Failed(why);
      for (uint32_t frame = 0; frame < k; ++frame) {
        if (!Advance(nullptr, &why) || !scene_->AddMoments(&why)) return Failed(why);
      }
      std::vector<std::string> files;
      uint64_t bytes = 0;
      const auto file_of = [](const RtScene12::MomentOutput& output, uint32_t frames, const RtScene12::MomentPlanes& planes) {
        return MomentsFile({.frames = frames, .width = output.width, .height = output.height, .active_width = output.width, .active_height = output.height,
                            .format = static_cast<uint32_t>(output.format)},
                           planes);
      };
      if (!scene_->WriteMoments(folder, file_of, &files, &bytes, &why)) return Failed(why);
      return {.result = {{"folder", folder.string()}, {"files", files}, {"bytes", bytes}}};
    }
    // trace (S4): frames played with Nsight Graphics' frame boundary before each trace, never timed.
    if (!options_.nsight) return {.status = "REFUSED", .error = "not an injected (--nsight) resident"};
    if (!OnlyKeys(args, {"frames"}, &why)) return {.status = "REFUSED", .error = why};
    tracing_ = true;
    for (uint32_t frame = 0; frame < args.at("frames").get<uint32_t>(); ++frame) {
      if (!Advance(nullptr, &why)) {
        tracing_ = false;
        return Failed(why);
      }
    }
    tracing_ = false;
    return {};
  }

  // dump's and moments' result: the folder, its files and their bytes.
  static nlohmann::json FolderResult(const std::filesystem::path& folder) {
    std::vector<std::string> files;
    uint64_t bytes = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
      if (!entry.is_regular_file(error)) continue;
      files.push_back(entry.path().filename().string());
      bytes += entry.file_size(error);
    }
    std::ranges::sort(files);
    return {{"folder", folder.string()}, {"files", files}, {"bytes", bytes}};
  }

  Reply Configure(const nlohmann::json& args) {
    std::string why;
    if (!OnlyKeys(args, {"trace_flags", "path_flags", "micromap_level", "fields", "stems", "burn"}, &why)) return {.status = "REFUSED", .error = why};
    Config made;
    if (!args.at("trace_flags").is_null()) made.trace_flags = args.at("trace_flags").get<uint32_t>();
    if (!args.at("path_flags").is_null()) made.path_flags = args.at("path_flags").get<uint32_t>();
    if (!args.at("micromap_level").is_null()) made.micromap_level = std::clamp(args.at("micromap_level").get<int>(), 0, 12);
    for (const auto& [field, value] : args.at("fields").items()) {
      if (!ParseField(field, value.get<double>(), &made.fields.emplace_back(), &why)) return {.status = "REFUSED", .error = why};
    }
    for (const auto& [stem, path] : args.at("stems").items()) {
      if (path.get<std::string>() == "base") continue;
      std::ifstream file(path.get<std::string>(), std::ios::binary);
      std::vector<uint8_t> code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      if (!file || !CheckStem(code, &why)) return {.status = "REFUSED", .error = std::format("stem {} ({}): {}", stem, path.get<std::string>(), why.empty() ? "unreadable" : why)};
      made.stem_hashes[stem] = Sha256(code);
      made.stems[stem] = std::move(code);
    }
    if (!args.at("burn").is_null()) {
      const nlohmann::json& burn = args.at("burn");
      if (!OnlyKeys(burn, {"pass", "loops"}, &why)) return {.status = "REFUSED", .error = why};
      made.burn_pass = burn.at("pass").get<std::string>();
      made.burn_loops = burn.at("loops").get<uint32_t>();
    }
    // The stems the last config swapped and this one does not: the base build's again; then this one's (any failure: the
    // last config's back).
    std::map<std::string, std::vector<uint8_t>> applied = made.stems;
    for (const auto& [stem, code] : config_.stems) applied.try_emplace(stem);
    for (const auto& [stem, code] : applied) {
      if (!scene_->SwapStem(stem, code, &why)) {
        for (const auto& [kept, kept_code] : applied) scene_->SwapStem(kept, config_.stems.contains(kept) ? config_.stems[kept] : std::vector<uint8_t>(), &why);
        return {.status = "REFUSED", .error = std::format("stem {}: {}", stem, why)};
      }
    }
    if (!scene_->SetBurn(made.burn_pass, made.burn_loops, &why)) {
      scene_->SetBurn(config_.burn_pass, config_.burn_loops, &why);
      for (const auto& [stem, code] : applied) scene_->SwapStem(stem, config_.stems.contains(stem) ? config_.stems[stem] : std::vector<uint8_t>(), &why);
      return {.status = "REFUSED", .error = why};
    }
    for (const auto& [stem, hash] : made.stem_hashes) options_.log(std::format("[TW resident] Stem {}: SHA-256 {}", stem, hash));
    config_ = std::move(made);
    return {.result = {{"stems", config_.stem_hashes}, {"dxr12", scene_->Dxr12()}}};
  }

  Reply Run(const nlohmann::json& args, uint32_t n) {
    std::string why;
    if (!OnlyKeys(args, {"frames", "warm", "reset", "timing", "decide", "tag", "log_uses"}, &why)) return {.status = "REFUSED", .error = why};
    const auto frames = args.at("frames").get<uint32_t>();
    const auto warm = args.at("warm").get<uint32_t>();
    const std::string timing = args.at("timing").get<std::string>();
    if (frames == 0 || (timing != "passes" && timing != "total")) return {.status = "REFUSED", .error = "frames below 1, or timing not passes / total"};
    if (args.at("log_uses").get<bool>()) return {.status = "REFUSED", .error = "log_uses: the resource-use log (M10) is not in this build"};
    std::string mode = "none";
    if ((args.at("reset").get<bool>() || loaded_level_ != config_.micromap_level) && !Reset(&mode, &why)) return Failed(why);
    for (uint32_t frame = 0; frame < warm; ++frame) {
      if (!Advance(nullptr, &why)) return Failed(why);
    }
    if (!scene_->ServeTiming((timing == "passes") ? RtScene12::SERVE_TIMING_PASSES : RtScene12::SERVE_TIMING_TOTAL)) return Failed("no GPU timestamps");
    std::vector<std::pair<std::string, std::vector<double>>> table;  // each row's ms a frame (NaN: not in it)
    std::vector<double> clocks;
    double usage = 0.0;
    double budget = 0.0;
    for (uint32_t frame = 0; frame < frames; ++frame) {
      std::vector<std::pair<std::string, double>> rows;
      if (!Advance(&rows, &why)) {
        scene_->ServeTiming(RtScene12::SERVE_TIMING_OFF);
        return Failed(why);
      }
      for (const auto& [row, ms] : rows) {
        auto found = std::ranges::find(table, row, &std::pair<std::string, std::vector<double>>::first);
        if (found == table.end()) found = table.insert(table.end(), {row, std::vector<double>(frames, std::numeric_limits<double>::quiet_NaN())});
        found->second[frame] = ms;
      }
      if (const double clock = ClockMhz(); clock > 0.0) clocks.push_back(clock);
      const DXGI_QUERY_VIDEO_MEMORY_INFO memory = Memory();
      usage = std::max(usage, static_cast<double>(memory.CurrentUsage) / (1 << 20));
      budget = static_cast<double>(memory.Budget) / (1 << 20);
    }
    scene_->ServeTiming(RtScene12::SERVE_TIMING_OFF);
    if (clocks.empty()) return {.status = "FAILED", .error = "no GPU clock (NVAPI)"};
    nlohmann::json rows = nlohmann::json::object();
    for (const auto& [row, values] : table) {
      std::vector<double> present;
      std::ranges::copy_if(values, std::back_inserter(present), [](double value) { return !std::isnan(value); });
      if (present.empty()) continue;
      const RowStats stats = Summarize(present);
      rows[row] = {{"median", stats.median}, {"mad", stats.mad}, {"trimmed_mean", stats.trimmed_mean}, {"min", stats.min},
                   {"max", stats.max},       {"n", stats.n},     {"outliers", stats.outliers}};
    }
    const std::string raw = std::format("raw_{}.bin", n);
    const std::vector<uint8_t> raw_bytes = RawFile(table, frames);
    const bool raw_written = static_cast<bool>(std::ofstream(options_.control / raw, std::ios::binary | std::ios::trunc)
                                                   .write(reinterpret_cast<const char*>(raw_bytes.data()), static_cast<std::streamsize>(raw_bytes.size())));
    const std::array<uint32_t, 2> size = scene_->PlaybackSize();
    return {.result = {{"rows", rows},
                       {"gpu_clock_mhz", Summarize(clocks).median},
                       {"vram_usage_mb", usage},
                       {"vram_budget_mb", budget},
                       {"reset", mode},
                       {"timing", timing},
                       {"frames", frames},
                       {"warm", warm},
                       {"raw", raw_written ? nlohmann::json(raw) : nlohmann::json(nullptr)},
                       {"uses", nullptr},
                       {"stems", config_.stem_hashes},
                       {"tag", args.at("tag")},
                       {"seed", seed_},
                       {"resolution", {size[0], size[1]}}}};
  }

  Options options_;
  ComPtr<IDXGIAdapter1> adapter_;
  ComPtr<IDXGIAdapter3> memory_adapter_;
#if __has_include(<nvapi.h>)
  NvPhysicalGpuHandle clock_gpu_ = nullptr;
#endif
  std::unique_ptr<RtScene12> scene_;
  Snapshot<RtScene12> snapshot_;
  Config config_;
  int loaded_level_ = -1;
  std::vector<std::vector<size_t>> frames_;  // each recorded frame's requests
  uint32_t span_ = 1;                        // the recording's frames, first to last
  uint32_t loops_ = 1;                       // the loops played before the snapshot
  uint32_t position_ = 0;                    // frames played since the snapshot or reset
  uint32_t seed_ = 0;                        // the offset in play (a reset takes the pending one)
  uint32_t pending_seed_ = 0;
  uint32_t played_since_reset_ = 0;  // the light animation's frame
  std::vector<LightFrame> light_animation_;
  bool tracing_ = false;
  std::atomic<uint32_t> losses_ = 0;
  uint64_t start_ = 0;
  std::string build_hash_;
  // The heartbeat's (mutex_).
  std::mutex mutex_;
  std::string state_ = "loading 0 %";
  uint32_t last_n_ = 0;
  std::string last_cmd_time_;
  ComPtr<ID3D12Fence> fence_;
  std::string snapshot_kind_ = "none";
  double snapshot_mb_ = 0.0;
};

}  // namespace twattila::dlaa_helper::serve
#endif
