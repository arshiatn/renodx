// P0.d's map replay (render_mt track A, step S3): the render thread's D3D11 stream of a map trace replayed on one
// single-threaded FL 11_0 device (WARP or the NVIDIA adapter), every replayed call timed with QPC.
//
// Input: map trace v3 (twattila/frame_trace.hpp: tw_map_trace_v3.txt, the call stream in order, with the resource
// descriptions of tw_map_trace.txt beside it), or v2 (tw_map_trace.txt alone: the game's maps at their draw positions,
// no binds; the binds are assumed as analysis_b1 notes.md 10.4 says and each assumption goes into Stream::notes).
// The replay makes the stream's resources (in first-use order), then for each frame its Maps (deterministic bytes),
// Unmaps, Set / Set1 constant buffer binds, IASetVertexBuffers, SwapDeviceContextState, ClearState, draws (one triangle,
// `instances` copies: the GPU load), dispatches and a flip-model Present, paced to the trace's CPU time between calls
// with the native call times left out (the trace's waits do not delay the replay; the replay's own waits do not delay
// the calls after them).
// Parity mode: each draw writes a 64-pixel strip of what it read (the VS's constant buffer hash, then 14 PS slots x
// constant indices 0, 1, 16, 255: in-window, past a 16 B buffer, past a 16-constant window, deep), each dispatch a row;
// read back each frame, compared bit for bit between two runs.
// The trap test (s5r_ design v3 3.3): the victims parked in a time window (Options::park), keep-warm, the plugin in a
// window only; PhaseReport's phases around it and its TRAP lines. PK1's trigger attempts (6.2 H5): a piece cap, the C
// rows' burst, extra push / unit calls, a focus gap (Options), the address space ballast (ReserveBallast); a run
// without a window: ScanReport.
#pragma once

#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "./ring_seam.hpp"

namespace tw_map_replay {

using Microsoft::WRL::ComPtr;

constexpr uint32_t NO_RESOURCE = UINT32_MAX;
constexpr uint32_t STAGES = 6;  // the trace's order: vs hs ds gs ps cs
constexpr std::array<std::string_view, STAGES> STAGE_NAMES = {"vs", "hs", "ds", "gs", "ps", "cs"};
constexpr uint32_t VERTEX_STAGE = 0;
constexpr uint32_t PIXEL_STAGE = 4;
constexpr uint32_t CB_SLOTS = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
constexpr uint32_t STRIP_PIXELS = 64;
constexpr std::array<uint32_t, 4> READ_INDICES = {0, 1, 16, 255};
constexpr uint32_t MAX_DISPATCH_ROWS = 1024;
constexpr uint32_t WHOLE_BUFFER_CONSTANTS = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;

enum class Kind : uint8_t { MAP, UNMAP, SET, SET1, VB, SWAP, CLEAR, DRAW, DISPATCH, PRESENT };

// Where a replayed call's time goes (the wait locator's rows).
enum class Where : uint8_t {
  CB_DISCARD,
  CB_OTHER_MAP,
  VB_DISCARD,
  VB_OTHER_MAP,
  IB_MAP,
  TEXTURE_MAP,
  OTHER_MAP,  // staging reads, structured and raw buffers
  UNMAP,
  BIND,
  SWAP_CLEAR,
  DRAW,
  DISPATCH,
  PLUGIN_PRESENT,  // the ring's present callback
  PRESENT,         // DXGI's Present (the frame-latency throttle)
  COUNT
};
constexpr std::array<std::string_view, static_cast<size_t>(Where::COUNT)> WHERE_NAMES = {
    "cb_discard", "cb_other_map", "vb_discard", "vb_other_map", "ib_map", "texture_map", "other_map",
    "unmap",      "bind",         "swap_clear", "draw",         "dispatch", "plugin_present", "present"};

// A resource as the trace describes it (described false: only bound or mapped, its description assumed).
struct Resource {
  uint64_t trace_pointer = 0;
  bool texture = false;
  uint32_t width = 0;  // a buffer's bytes
  uint32_t height = 1;
  uint32_t array_size = 1;
  uint32_t mips = 1;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
  uint32_t bind = 0;
  uint32_t cpu = 0;
  uint32_t misc = 0;
  bool described = false;
  // The game's (made through ReShade's proxy): the plugin is offered it. A v3 trace with tw_map_trace.txt beside it: a
  // v1 map line (a map through the proxy) or a v3 buffer line (its init_resource); ReShade's own buffers (its 432 B
  // push buffer) have native_map lines only.
  bool game = true;
  // How the stream uses it (for an assumed description).
  bool bound_constant = false;
  bool bound_vertex = false;
  uint32_t mapped_bytes = 0;
  uint32_t window_end = 0;  // constants
};

// A bind slot: a constant buffer with its window in constants (count 0: the whole buffer), or a vertex buffer with
// its stride and offset.
struct Slot {
  uint32_t resource = NO_RESOURCE;
  uint32_t first_or_stride = 0;
  uint32_t count_or_offset = 0;
};

struct Call {
  Kind kind = Kind::DRAW;
  uint32_t frame = 0;
  double pace_us = 0.;  // the trace's CPU time from its frame's start, the native call times before it left out
  uint32_t resource = NO_RESOURCE;  // a map's, an unmap's; a swap's new state
  uint32_t previous_state = 0;
  D3D11_MAP map_type = D3D11_MAP_WRITE_DISCARD;
  uint32_t subresource = 0;
  uint32_t stage = 0;
  uint32_t start_slot = 0;
  uint32_t count = 0;
  uint32_t first_slot = 0;  // in Stream::slots
};

struct Stream {
  uint32_t version = 0;
  uint32_t frames = 0;
  std::vector<Resource> resources;
  std::vector<Call> calls;  // in order, each frame ending in its PRESENT
  std::vector<Slot> slots;
  uint32_t states = 1;  // device context states (0: the device's own)
  std::vector<std::string> notes;
};

inline uint32_t FillWord(uint32_t ordinal, uint32_t word) {
  uint32_t value = (ordinal * 0x9E3779B9u) + (word * 0x85EBCA6Bu) + 1u;
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  value ^= value >> 16;
  return value;
}

// A map of `type` writes these 32-bit words from offset 0: DISCARD the whole buffer; NO_OVERWRITE none (the bytes a
// game writes there are ones no queued draw reads, which the trace does not say: writing any would break that promise
// and leave the GPU's bytes undefined); WRITE and READ_WRITE (staging) the first four.
inline uint32_t FillWords(const Resource& resource, D3D11_MAP type) {
  if (type == D3D11_MAP_READ || type == D3D11_MAP_WRITE_NO_OVERWRITE) return 0;
  if (resource.texture) return 64;
  if (type == D3D11_MAP_WRITE_DISCARD) return resource.width / 4;
  return std::min(resource.width / 4, 4u);
}

inline int64_t Qpc() {
  LARGE_INTEGER value;
  QueryPerformanceCounter(&value);
  return value.QuadPart;
}

inline int64_t QpcFrequency() {
  LARGE_INTEGER value;
  QueryPerformanceFrequency(&value);
  return value.QuadPart;
}

// --ballast-mb: `mb` MB of the address space reserved, nothing committed (the 32-bit process as full as Attila's), in
// blocks of up to 64 MB (the space is fragmented), kept to the process's end. The MB reserved.
inline uint32_t ReserveBallast(uint32_t mb) {
  uint32_t reserved = 0;
  for (uint32_t block = 64; reserved < mb && block != 0;) {
    const uint32_t size = std::min(block, mb - reserved);
    if (VirtualAlloc(nullptr, size_t{size} << 20, MEM_RESERVE, PAGE_NOACCESS) != nullptr) {
      reserved += size;
    } else {
      block /= 2;
    }
  }
  return reserved;
}

namespace internal {

// A trace line: its first word and its key=value fields.
struct Line {
  std::string_view kind;
  std::vector<std::pair<std::string_view, std::string_view>> fields;

  std::optional<std::string_view> Find(std::string_view key) const {
    for (const auto& [name, value] : fields) {
      if (name == key) return value;
    }
    return std::nullopt;
  }

  std::string_view Required(std::string_view key) const {
    const auto value = Find(key);
    if (!value.has_value()) throw std::runtime_error(std::format("{} line without {}=", kind, key));
    return *value;
  }
};

inline Line Split(std::string_view text) {
  Line line;
  size_t position = 0;
  while (position < text.size()) {
    while (position < text.size() && text[position] == ' ') ++position;
    const size_t end = std::min(text.find(' ', position), text.size());
    const std::string_view word = text.substr(position, end - position);
    position = end;
    if (word.empty()) continue;
    if (line.kind.empty()) {
      line.kind = word;
    } else if (const size_t equals = word.find('='); equals != std::string_view::npos) {
      line.fields.emplace_back(word.substr(0, equals), word.substr(equals + 1));
    }
  }
  return line;
}

inline uint64_t Number(std::string_view text) {
  int base = 10;
  if (text.starts_with("0x")) {
    text.remove_prefix(2);
    base = 16;
  }
  uint64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
    throw std::runtime_error(std::format("not a number: '{}'", text));
  }
  return value;
}

inline double Decimal(std::string_view text) {
  double value = 0.;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
    throw std::runtime_error(std::format("not a decimal: '{}'", text));
  }
  return value;
}

// Calls `on_line(text)` for each line; an exception gets the file and line number.
inline void ForEachLine(const std::filesystem::path& path, const std::function<void(std::string_view)>& on_line) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error(std::format("cannot open {}", path.string()));
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  size_t number = 0;
  for (size_t position = 0; position < text.size();) {
    size_t end = text.find('\n', position);
    if (end == std::string::npos) end = text.size();
    std::string_view line(text.data() + position, end - position);
    if (line.ends_with('\r')) line.remove_suffix(1);
    position = end + 1;
    ++number;
    try {
      on_line(line);
    } catch (const std::exception& error) {
      throw std::runtime_error(std::format("{}:{}: {}", path.filename().string(), number, error.what()));
    }
  }
}

struct Reader {
  Stream stream;
  std::unordered_map<uint64_t, uint32_t> by_pointer;

  uint32_t ResourceOf(uint64_t pointer) {
    if (pointer == 0) return NO_RESOURCE;
    const auto [found, inserted] = by_pointer.try_emplace(pointer, static_cast<uint32_t>(stream.resources.size()));
    if (inserted) {
      stream.resources.push_back({.trace_pointer = pointer});
    }
    return found->second;
  }

  // A line's res= usage= bind= cpu= misc= type= bytes= / size= mips= format= (the first description wins).
  void Describe(const Line& line) {
    auto& resource = stream.resources[ResourceOf(Number(line.Required("res")))];
    if (resource.described) return;
    const std::string_view type = line.Required("type");
    const std::string_view usage = line.Required("usage");
    constexpr std::array<std::string_view, 4> USAGES = {"default", "immutable", "dynamic", "staging"};
    const auto usage_found = std::ranges::find(USAGES, usage);
    if (usage_found == USAGES.end()) throw std::runtime_error(std::format("unknown usage {}", usage));
    resource.usage = static_cast<D3D11_USAGE>(usage_found - USAGES.begin());
    resource.bind = static_cast<uint32_t>(Number(line.Required("bind")));
    resource.cpu = static_cast<uint32_t>(Number(line.Required("cpu")));
    resource.misc = static_cast<uint32_t>(Number(line.Required("misc")));
    if (type == "buffer") {
      resource.width = static_cast<uint32_t>(Number(line.Required("bytes")));
    } else if (type == "tex2d") {
      const std::string_view size = line.Required("size");
      const size_t x1 = size.find('x');
      const size_t x2 = size.find('x', x1 + 1);
      if (x1 == std::string_view::npos || x2 == std::string_view::npos) throw std::runtime_error("bad size=");
      resource.texture = true;
      resource.width = static_cast<uint32_t>(Number(size.substr(0, x1)));
      resource.height = static_cast<uint32_t>(Number(size.substr(x1 + 1, x2 - x1 - 1)));
      resource.array_size = static_cast<uint32_t>(Number(size.substr(x2 + 1)));
      resource.mips = static_cast<uint32_t>(Number(line.Required("mips")));
      resource.format = static_cast<DXGI_FORMAT>(Number(line.Required("format")));
    } else {
      stream.notes.push_back(std::format("resource 0x{:x}: type {} not replayed (its calls skipped)",
                                         resource.trace_pointer, type));
      resource.width = 0;
    }
    resource.described = true;
  }

  // Descriptions for the resources the trace only binds or maps (each kind counted in a note).
  void AssumeDescriptions() {
    uint32_t constants = 0;
    uint32_t vertices = 0;
    uint32_t mapped = 0;
    for (auto& resource : stream.resources) {
      if (resource.described) continue;
      resource.described = true;
      if (resource.mapped_bytes != 0) {
        ++mapped;
        const bool constant = resource.bound_constant || (!resource.bound_vertex && resource.mapped_bytes <= 65536);
        resource.usage = D3D11_USAGE_DYNAMIC;
        resource.cpu = D3D11_CPU_ACCESS_WRITE;
        resource.bind = (constant ? D3D11_BIND_CONSTANT_BUFFER : D3D11_BIND_VERTEX_BUFFER);
        resource.width = (constant ? ((resource.mapped_bytes + 15) & ~15u) : resource.mapped_bytes);
      } else if (resource.bound_constant) {
        ++constants;
        resource.bind = D3D11_BIND_CONSTANT_BUFFER;
        resource.width = std::max(16u, resource.window_end) * 16;
      } else {
        ++vertices;
        resource.bind = D3D11_BIND_VERTEX_BUFFER;
        resource.width = 65536;
      }
    }
    if (constants + vertices + mapped == 0) return;
    stream.notes.push_back(std::format(
        "assumed descriptions: {} bound-only constant buffers (default, sized to their windows or 64 KB), {} bound-only"
        " vertex buffers (default 64 KB), {} mapped resources without a description (dynamic, constant if bound so or"
        " <= 64 KB)",
        constants, vertices, mapped));
  }
};

inline D3D11_MAP MapType(std::string_view name) {
  constexpr std::array<std::string_view, 6> MAP_TYPES = {"?", "read", "write", "read_write", "discard", "no_overwrite"};
  const auto found = std::ranges::find(MAP_TYPES, name);
  if (found == MAP_TYPES.end() || found == MAP_TYPES.begin()) throw std::runtime_error(std::format("map type {}", name));
  return static_cast<D3D11_MAP>(found - MAP_TYPES.begin());
}

}  // namespace internal

// Map trace v3: tw_map_trace_v3.txt (descriptions from tw_map_trace.txt beside it when it is there).
inline Stream LoadV3(const std::filesystem::path& path) {
  internal::Reader reader;
  auto& stream = reader.stream;
  stream.version = 3;
  const auto v1_path = path.parent_path() / "tw_map_trace.txt";
  const bool v1 = std::filesystem::exists(v1_path);
  // Through ReShade's proxy: v1's map lines, v3's buffer lines (its init_resource).
  std::set<uint64_t> game_buffers;
  if (v1) {
    internal::ForEachLine(v1_path, [&](std::string_view text) {
      const auto line = internal::Split(text);
      if (line.kind == "map" || line.kind == "native_map" || line.kind == "update" || line.kind == "copy") {
        reader.Describe(line);
      }
      if (line.kind == "map") {
        game_buffers.insert(internal::Number(line.Required("res")));
      }
    });
  } else {
    stream.notes.push_back("no tw_map_trace.txt beside the v3 file: only the v3 buffer lines describe resources, and the"
                           " plugin is offered every buffer");
  }
  double frequency = 0.;
  uint64_t header_calls = 0;
  uint64_t header_binds = 0;
  uint64_t calls_seen = 0;
  uint64_t binds_seen = 0;
  uint32_t skipped_maps = 0;
  uint32_t skipped_unmaps = 0;
  uint32_t do_not_wait = 0;
  std::vector<int64_t> presents;
  std::vector<double> native_us;
  std::set<std::pair<uint32_t, uint32_t>> open_maps;
  std::unordered_map<uint64_t, uint32_t> states;
  uint32_t frame_open = 0;  // the frames before it have their PRESENT
  const auto present_until = [&](uint32_t frame) {
    for (; frame_open < frame; ++frame_open) {
      const double pace = (stream.calls.empty() ? 0. : stream.calls.back().pace_us);
      const bool same_frame = (!stream.calls.empty() && stream.calls.back().frame == frame_open);
      stream.calls.push_back({.kind = Kind::PRESENT, .frame = frame_open, .pace_us = (same_frame ? pace : 0.)});
    }
  };
  const auto state_of = [&](uint64_t pointer) {
    const auto [found, inserted] = states.try_emplace(pointer, static_cast<uint32_t>(states.size()));
    return found->second;
  };
  const auto parse_slots = [&](std::string_view list, bool vertex, uint32_t stage, Call* call) {
    call->first_slot = static_cast<uint32_t>(stream.slots.size());
    if (list == "-") return;
    uint32_t expected = UINT32_MAX;
    for (size_t position = 0; position <= list.size();) {
      const size_t end = std::min(list.find(',', position), list.size());
      const std::string_view item = list.substr(position, end - position);
      position = end + 1;
      const size_t colon = item.find(':');
      if (colon == std::string_view::npos) throw std::runtime_error(std::format("bad slot '{}'", item));
      const auto slot = static_cast<uint32_t>(internal::Number(item.substr(0, colon)));
      std::string_view rest = item.substr(colon + 1);
      Slot bound;
      if (vertex) {
        const size_t slash1 = rest.find('/');
        const size_t slash2 = rest.find('/', slash1 + 1);
        if (slash1 == std::string_view::npos || slash2 == std::string_view::npos) {
          throw std::runtime_error(std::format("bad vertex slot '{}'", item));
        }
        bound = {.resource = reader.ResourceOf(internal::Number(rest.substr(0, slash1))),
                 .first_or_stride = static_cast<uint32_t>(internal::Number(rest.substr(slash1 + 1, slash2 - slash1 - 1))),
                 .count_or_offset = static_cast<uint32_t>(internal::Number(rest.substr(slash2 + 1)))};
      } else {
        const size_t at = rest.find('@');
        bound.resource = reader.ResourceOf(internal::Number(rest.substr(0, at)));
        if (at != std::string_view::npos) {
          const std::string_view window = rest.substr(at + 1);
          const size_t plus = window.find('+');
          if (plus == std::string_view::npos) throw std::runtime_error(std::format("bad window '{}'", item));
          bound.first_or_stride = static_cast<uint32_t>(internal::Number(window.substr(0, plus)));
          bound.count_or_offset = static_cast<uint32_t>(internal::Number(window.substr(plus + 1)));
        }
      }
      if (expected == UINT32_MAX) {
        call->start_slot = slot;
      } else if (slot != expected) {
        throw std::runtime_error(std::format("slots not consecutive at '{}'", item));
      }
      expected = slot + 1;
      if (bound.resource != NO_RESOURCE) {
        auto& resource = stream.resources[bound.resource];
        if (vertex) {
          resource.bound_vertex = true;
        } else {
          resource.bound_constant = true;
          const uint32_t window_end = (bound.count_or_offset == 0 ? 4096u : bound.first_or_stride + bound.count_or_offset);
          resource.window_end = std::max(resource.window_end, window_end);
        }
      }
      stream.slots.push_back(bound);
      ++call->count;
    }
    call->stage = stage;
  };
  internal::ForEachLine(path, [&](std::string_view text) {
    const auto line = internal::Split(text);
    if (line.kind.empty()) return;
    if (line.kind == "v3") {
      frequency = static_cast<double>(internal::Number(line.Required("qpc_frequency")));
      stream.frames = static_cast<uint32_t>(internal::Number(line.Required("frames")));
      header_calls = internal::Number(line.Required("native_calls"));
      header_binds = internal::Number(line.Required("binds"));
      const uint64_t dropped = internal::Number(line.Required("dropped_native_calls"))
                               + internal::Number(line.Required("dropped_binds"));
      if (dropped != 0 || line.Required("first_drop_seq") != "-") {
        stream.notes.push_back(std::format("the trace dropped {} native calls or binds (first_drop_seq={}): they are"
                                           " missing from the replay",
                                           dropped, line.Required("first_drop_seq")));
      }
      native_us.assign(stream.frames, 0.);
      return;
    }
    if (frequency == 0.) throw std::runtime_error("no v3 header line first");
    if (line.kind == "present") {
      presents.push_back(static_cast<int64_t>(internal::Number(line.Required("qpc"))));
      return;
    }
    if (line.kind == "buffer") {
      reader.Describe(line);
      game_buffers.insert(internal::Number(line.Required("res")));
      return;
    }
    if (presents.size() != stream.frames + 1) throw std::runtime_error("the stream starts before every present line");
    const auto frame = static_cast<uint32_t>(internal::Number(line.Required("frame")));
    if (frame >= stream.frames || frame < frame_open) throw std::runtime_error(std::format("frame {} out of order", frame));
    present_until(frame);
    const auto qpc = static_cast<int64_t>(internal::Number(line.Required("qpc")));
    Call call = {.frame = frame,
                 .pace_us = (static_cast<double>(qpc - presents[frame]) * 1e6 / frequency) - native_us[frame]};
    if (line.kind != "draw") {
      native_us[frame] += internal::Decimal(line.Required("native_ms")) * 1000.;
    }
    const auto stage_of = [&] {
      const auto found = std::ranges::find(STAGE_NAMES, line.Required("stage"));
      if (found == STAGE_NAMES.end()) throw std::runtime_error("unknown stage");
      return static_cast<uint32_t>(found - STAGE_NAMES.begin());
    };
    if (line.kind == "map" || line.kind == "unmap") {
      ++calls_seen;
      const uint64_t pointer = internal::Number(line.Required("res"));
      call.subresource = static_cast<uint32_t>(internal::Number(line.Required("sub")));
      if (line.kind == "unmap") {
        const auto found = reader.by_pointer.find(pointer);
        if (found == reader.by_pointer.end() || open_maps.erase({found->second, call.subresource}) == 0) {
          ++skipped_unmaps;
          return;
        }
        call.kind = Kind::UNMAP;
        call.resource = found->second;
      } else {
        call.resource = reader.ResourceOf(pointer);
        const std::pair<uint32_t, uint32_t> key = {call.resource, call.subresource};
        if (internal::Number(line.Required("hr")) != 0) {
          ++skipped_maps;
          return;
        }
        if ((internal::Number(line.Required("flags")) & D3D11_MAP_FLAG_DO_NOT_WAIT) != 0) ++do_not_wait;
        call.kind = Kind::MAP;
        call.map_type = internal::MapType(line.Required("map"));
        auto& resource = stream.resources[call.resource];
        resource.mapped_bytes = std::max(resource.mapped_bytes, static_cast<uint32_t>(internal::Number(line.Required("bytes"))));
        open_maps.insert(key);
      }
    } else if (line.kind == "set" || line.kind == "set1" || line.kind == "seed_cb") {
      ++binds_seen;
      call.kind = (line.kind == "set" ? Kind::SET : Kind::SET1);
      parse_slots(line.Required("slots"), false, stage_of(), &call);
    } else if (line.kind == "vb" || line.kind == "seed_vb") {
      ++binds_seen;
      call.kind = Kind::VB;
      parse_slots(line.Required("slots"), true, 0, &call);
    } else if (line.kind == "swap") {
      ++binds_seen;
      const uint64_t previous = internal::Number(line.Required("previous"));
      if (states.empty()) states[previous] = 0;
      call.kind = Kind::SWAP;
      call.previous_state = state_of(previous);
      call.resource = state_of(internal::Number(line.Required("state")));
    } else if (line.kind == "clear") {
      ++binds_seen;
      call.kind = Kind::CLEAR;
    } else if (line.kind == "draw") {
      call.kind = (line.Required("kind").starts_with("dispatch") ? Kind::DISPATCH : Kind::DRAW);
    } else {
      throw std::runtime_error(std::format("unknown line kind {}", line.kind));
    }
    stream.calls.push_back(call);
  });
  if (frequency == 0.) throw std::runtime_error(std::format("{}: no v3 header", path.filename().string()));
  if (calls_seen != header_calls || binds_seen != header_binds) {
    throw std::runtime_error(std::format("{}: {} map / unmap lines and {} bind lines, the header says {} and {}",
                                         path.filename().string(), calls_seen, binds_seen, header_calls, header_binds));
  }
  present_until(stream.frames);
  stream.states = std::max<uint32_t>(1, static_cast<uint32_t>(states.size()));
  if (skipped_maps + skipped_unmaps != 0) {
    stream.notes.push_back(std::format("{} failed maps (a DO_NOT_WAIT poll's WAS_STILL_DRAWING) and {} unmaps of maps"
                                       " made before the trace not replayed",
                                       skipped_maps, skipped_unmaps));
  }
  if (do_not_wait != 0) {
    stream.notes.push_back(std::format("{} DO_NOT_WAIT maps replayed without the flag", do_not_wait));
  }
  stream.notes.push_back("draws replayed as one triangle (indexed and indirect ones as direct draws), dispatches as"
                         " one group; queries, copies and UpdateSubresource are not in v3's stream");
  reader.AssumeDescriptions();
  if (v1) {
    uint32_t not_game = 0;
    uint32_t dynamic_constants = 0;
    for (auto& resource : stream.resources) {
      resource.game = game_buffers.contains(resource.trace_pointer);
      if (resource.game || resource.texture || resource.width == 0) continue;
      ++not_game;
      dynamic_constants += (resource.usage == D3D11_USAGE_DYNAMIC && resource.bind == D3D11_BIND_CONSTANT_BUFFER ? 1 : 0);
    }
    stream.notes.push_back(std::format("{} buffers neither mapped nor made through ReShade's proxy (ReShade's own, as"
                                       " its push buffer, or bound only; {} of them dynamic constant buffers): the"
                                       " plugin is not offered them (--addon-buffers ring offers them)",
                                       not_game, dynamic_constants));
  }
  return stream;
}

// Map trace v2 (tw_map_trace.txt: the game's v1 maps at their draw positions; no binds). `waits_path`: analysis_b1's
// join (driver waits per frame, from the ETL) taken out of the pacing; without it, a gap over `gap_cap_ms` counts as a
// wait above the cap.
inline Stream LoadV2(const std::filesystem::path& path, const std::filesystem::path& waits_path, double gap_cap_ms) {
  internal::Reader reader;
  auto& stream = reader.stream;
  stream.version = 2;
  struct V2Map {
    double ms = 0.;
    double gap_ms = 0.;
    uint32_t draw = 0;
    bool after_dispatch = false;
    D3D11_MAP type = D3D11_MAP_WRITE_DISCARD;
    uint32_t subresource = 0;
    uint32_t resource = NO_RESOURCE;
    double held_ms = 0.;
  };
  struct V2Frame {
    uint32_t draws = 0;
    double ms = 0.;
    std::vector<V2Map> maps;
    std::vector<std::pair<double, double>> waits;  // start, duration (ms in the frame)
  };
  std::vector<V2Frame> frames;
  uint32_t updates = 0;
  uint32_t copies = 0;
  internal::ForEachLine(path, [&](std::string_view text) {
    const auto line = internal::Split(text);
    if (line.kind == "trace") {
      stream.frames = static_cast<uint32_t>(internal::Number(line.Required("frames")));
      frames.resize(stream.frames);
    } else if (line.kind == "frame") {
      const auto index = static_cast<uint32_t>(internal::Number(line.Required("index")));
      if (index >= frames.size()) throw std::runtime_error("frame index past the header's frames");
      frames[index].draws = static_cast<uint32_t>(internal::Number(line.Required("draws"))
                                                  + internal::Number(line.Required("dispatches")));
      frames[index].ms = internal::Decimal(line.Required("ms"));
    } else if (line.kind == "map") {
      const auto frame = static_cast<uint32_t>(internal::Number(line.Required("frame")));
      if (frame >= frames.size()) throw std::runtime_error("map frame past the header's frames");
      reader.Describe(line);
      const double ms = internal::Decimal(line.Required("ms"));
      const std::string_view gap = line.Required("gap_ms");
      const std::string_view unmap = line.Required("unmap_ms");
      const std::string_view access = line.Required("access");
      frames[frame].maps.push_back({
          .ms = ms,
          .gap_ms = (gap == "-" ? 0. : internal::Decimal(gap)),
          .draw = static_cast<uint32_t>(internal::Number(line.Required("draw"))),
          .after_dispatch = line.Required("last").starts_with("dispatch"),
          .type = internal::MapType(access == "write_discard" ? "discard" : access),
          .subresource = static_cast<uint32_t>(internal::Number(line.Required("sub"))),
          .resource = reader.ResourceOf(internal::Number(line.Required("res"))),
          .held_ms = (unmap == "-" ? 0. : std::max(0., internal::Decimal(unmap) - ms)),
      });
    } else if (line.kind == "update") {
      ++updates;
    } else if (line.kind == "copy") {
      ++copies;
    }
  });
  if (frames.empty()) throw std::runtime_error(std::format("{}: no v2 trace header", path.filename().string()));
  if (!waits_path.empty()) {
    // "  frame 1 +  51.070 ms     0.006 ms  thread ..."
    internal::ForEachLine(waits_path, [&](std::string_view text) {
      std::vector<std::string_view> words;
      for (size_t position = 0; position < text.size() && words.size() < 7;) {
        while (position < text.size() && text[position] == ' ') ++position;
        const size_t end = std::min(text.find(' ', position), text.size());
        if (end > position) words.push_back(text.substr(position, end - position));
        position = end;
      }
      if (words.size() < 7 || words[0] != "frame" || words[2] != "+" || words[4] != "ms" || words[6] != "ms") return;
      const auto frame = static_cast<uint32_t>(internal::Number(words[1]));
      if (frame < frames.size()) {
        frames[frame].waits.emplace_back(internal::Decimal(words[3]), internal::Decimal(words[5]));
      }
    });
    stream.notes.push_back(std::format("pacing: the ETL waits of {} left out", waits_path.filename().string()));
  } else {
    stream.notes.push_back(std::format("pacing: no waits file; a gap over {} ms counts as a wait above it", gap_cap_ms));
  }
  // The assumed binds (notes.md 10.4): a constant buffer is bound from its unmap on, in the VS and the PS; the biggest
  // seven mapped at most once a frame keep slots 0-6 (bound by every draw), the others share slots 7-13 (one each by
  // first use); a vertex buffer goes to input slot 0 (stride 16).
  std::vector<uint32_t> map_counts(stream.resources.size(), 0);
  std::vector<uint32_t> first_use;
  for (const auto& frame : frames) {
    for (const auto& map : frame.maps) {
      if (map_counts[map.resource]++ == 0) first_use.push_back(map.resource);
    }
  }
  std::vector<uint32_t> once_a_frame;
  for (const uint32_t resource : first_use) {
    const auto& described = stream.resources[resource];
    if ((described.bind & D3D11_BIND_CONSTANT_BUFFER) != 0 && map_counts[resource] <= stream.frames) {
      once_a_frame.push_back(resource);
    }
  }
  std::ranges::stable_sort(once_a_frame, std::greater{}, [&](uint32_t resource) { return stream.resources[resource].width; });
  std::vector<uint32_t> slot_of(stream.resources.size(), UINT32_MAX);
  for (uint32_t index = 0; index < std::min<size_t>(7, once_a_frame.size()); ++index) {
    slot_of[once_a_frame[index]] = index;
  }
  uint32_t shared = 0;
  for (const uint32_t resource : first_use) {
    if ((stream.resources[resource].bind & D3D11_BIND_CONSTANT_BUFFER) != 0 && slot_of[resource] == UINT32_MAX) {
      slot_of[resource] = 7 + (shared++ % 7);
    }
  }
  std::array<std::array<uint32_t, CB_SLOTS>, 2> held = {};
  for (auto& stage : held) stage.fill(NO_RESOURCE);
  uint32_t held_vertex = NO_RESOURCE;
  const auto bind_constant = [&](uint32_t resource, uint32_t frame, double pace) {
    const uint32_t slot = slot_of[resource];
    for (uint32_t index = 0; index < 2; ++index) {
      if (held[index][slot] == resource) continue;
      held[index][slot] = resource;
      stream.calls.push_back({.kind = Kind::SET, .frame = frame, .pace_us = pace,
                              .stage = (index == 0 ? VERTEX_STAGE : PIXEL_STAGE), .start_slot = slot, .count = 1,
                              .first_slot = static_cast<uint32_t>(stream.slots.size())});
      stream.slots.push_back({.resource = resource});
      stream.resources[resource].bound_constant = true;
    }
  };
  for (uint32_t index = 0; index < std::min<size_t>(7, once_a_frame.size()); ++index) {
    bind_constant(once_a_frame[index], 0, 0.);
  }
  for (uint32_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
    const auto& frame = frames[frame_index];
    double cap_waits = 0.;
    const auto paced = [&](const V2Map* map, double ms) {
      double waits = 0.;
      for (const auto& [start, duration] : frame.waits) {
        if (start < ms) waits += std::min(duration, ms - start);
      }
      if (waits_path.empty() && map != nullptr) {
        cap_waits += std::max(0., map->gap_ms - gap_cap_ms);
      }
      return (ms - waits - cap_waits) * 1000.;
    };
    uint32_t emitted = 0;
    uint32_t last_draw = 0;
    double last_pace = 0.;
    const auto draws_until = [&](uint32_t draw, double pace, bool dispatch_last) {
      const uint32_t span = draw - last_draw + 1;
      for (; emitted < draw; ++emitted) {
        const double share = static_cast<double>(emitted - last_draw + 1) / static_cast<double>(span);
        const bool dispatch = (dispatch_last && emitted + 1 == draw);
        stream.calls.push_back({.kind = (dispatch ? Kind::DISPATCH : Kind::DRAW), .frame = frame_index,
                                .pace_us = last_pace + ((pace - last_pace) * share)});
      }
    };
    for (const auto& map : frame.maps) {
      const double pace = std::max(last_pace, paced(&map, map.ms));
      const uint32_t draw = std::min(std::max(map.draw, emitted), frame.draws);
      draws_until(draw, pace, map.after_dispatch);
      stream.calls.push_back({.kind = Kind::MAP, .frame = frame_index, .pace_us = pace, .resource = map.resource,
                              .map_type = map.type, .subresource = map.subresource});
      const double unmap_pace = pace + (map.held_ms * 1000.);
      stream.calls.push_back({.kind = Kind::UNMAP, .frame = frame_index, .pace_us = unmap_pace,
                              .resource = map.resource, .subresource = map.subresource});
      const auto& resource = stream.resources[map.resource];
      if ((resource.bind & D3D11_BIND_CONSTANT_BUFFER) != 0) {
        bind_constant(map.resource, frame_index, unmap_pace);
      } else if ((resource.bind & D3D11_BIND_VERTEX_BUFFER) != 0 && held_vertex != map.resource) {
        held_vertex = map.resource;
        stream.calls.push_back({.kind = Kind::VB, .frame = frame_index, .pace_us = unmap_pace, .count = 1,
                                .first_slot = static_cast<uint32_t>(stream.slots.size())});
        stream.slots.push_back({.resource = map.resource, .first_or_stride = 16});
      }
      last_pace = unmap_pace;
      last_draw = emitted;
    }
    const double end_pace = std::max(last_pace, paced(nullptr, frame.ms));
    draws_until(frame.draws, end_pace, false);
    stream.calls.push_back({.kind = Kind::PRESENT, .frame = frame_index, .pace_us = end_pace});
  }
  stream.notes.push_back(std::format(
      "v2 binds assumed (notes.md 10.4): {} once-a-frame constant buffers on dedicated VS+PS slots 0-6, the other {}"
      " from their unmap on in shared slots 7-13; vertex buffers at input slot 0, stride 16",
      std::min<size_t>(7, once_a_frame.size()), shared));
  stream.notes.push_back(std::format("v2: each map's unmap right after it (its held time paced); {} updates and {}"
                                     " copies not replayed; draws as one triangle each, kinds unknown except a dispatch"
                                     " right before a map",
                                     updates, copies));
  reader.AssumeDescriptions();
  return stream;
}

// A file or folder: v3 when tw_map_trace_v3.txt is named or present, else v2's tw_map_trace.txt.
inline Stream Load(const std::filesystem::path& path, const std::filesystem::path& waits_path, double gap_cap_ms) {
  if (std::filesystem::is_directory(path)) {
    if (std::filesystem::exists(path / "tw_map_trace_v3.txt")) return LoadV3(path / "tw_map_trace_v3.txt");
    return LoadV2(path / "tw_map_trace.txt", waits_path, gap_cap_ms);
  }
  if (path.filename().string().ends_with("_v3.txt")) return LoadV3(path);
  return LoadV2(path, waits_path, gap_cap_ms);
}

// Controls for the parity check: each changes what the ON run does the way a broken ring would, so the comparison must
// fail.
enum class Control : uint8_t {
  NONE,
  UPLOAD,  // a wrong word in every fifth constant buffer DISCARD's bytes
  WINDOW,  // Set1 windows 16 constants wider (the out-of-window reads see data)
  OFFSET,  // a buffer bound in two slots of a stage at two offsets: both at the first one's
  PARK,    // the park's end leaves the victims' binds on their stand-ins (the draws after it read old bytes)
};

// The trap test's victims (s5r_ 3.3), all dynamic buffers of the game's that the stream maps: CB the constant buffers
// up to 64 KB (the ring's set), VB the vertex buffers over 64 KB, ALL every one.
enum class Park : uint8_t { NONE, CB, VB, ALL };

struct Options {
  bool warp = true;  // else the first NVIDIA adapter
  uint32_t first_frame = 0;
  uint32_t last_frame = UINT32_MAX;
  uint32_t loops = 1;
  bool pace = true;
  double gpu_ms = 0.;  // GPU load a frame (0: the least)
  bool parity = false;
  uint32_t target_size = 2048;
  Control control = Control::NONE;
  const RingPlugin* plugin = &NATIVE_PLUGIN;
  bool addon_buffers = false;  // the plugin offered ReShade's own buffers too (Resource::game false), as before H0
  double wait_ms = 0.1;  // a call at least this long counts as a wait
  // Test seam: `test_delay_ms` of spin inside every call that lands in `test_delay_where`.
  Where test_delay_where = Where::COUNT;
  double test_delay_ms = 0.;
  // The trap test: inside [park_start_s, + park_length_s) the victims are neither mapped nor bound, as the ring does
  // to the game's buffers: their maps and binds go to stand-ins of the same description (made at the window's start,
  // given the victim's bytes); at its end each victim gets its newest bytes back with a native DISCARD (the ring's
  // write-back) and is bound again where the stand-in was. Every `keep_warm` frames inside it each victim gets one
  // native DISCARD (with `keep_warm_bind`, then one draw with it bound at its last bind's slot, a CB in the PS).
  Park park = Park::NONE;
  double park_start_s = 0.;
  double park_length_s = 0.;
  uint32_t keep_warm = 0;
  bool keep_warm_bind = false;
  // The plugin in only inside [plugin_start_s, + plugin_length_s) (its present told so; negative start: always in).
  double plugin_start_s = -1.;
  double plugin_length_s = 0.;
  // Test seam: the windows' clock in replayed frames of this many seconds (0: QPC from the first replayed frame).
  double seconds_per_frame = 0.;
  // Trigger attempts (PK1 6.2 H5). The plugin's most pieces (RingPlugin::piece_cap; 0: its own). The C rows' shape
  // (burst_ms > 0): each frame's calls unpaced, then idle to its start + burst_ms. Extra calls a frame, spread evenly
  // over its draws, never offered to the plugin: `extra_push` DISCARDs of a 432 B dynamic constant buffer bound in the
  // PS's last slot (ReShade's push buffer), `extra_units` 16 B UpdateSubresources of a default constant buffer bound in
  // the CS's slot 0, each with a Dispatch (RecordUnits' runs). The focus gap: the window full-screen and activatable, a
  // second window of the harness in the foreground inside [focus_start_s, + focus_length_s) (negative start: neither).
  uint32_t piece_cap = 0;
  double burst_ms = 0.;
  uint32_t extra_push = 0;
  uint32_t extra_units = 0;
  double focus_start_s = -1.;
  double focus_length_s = 0.;
};

struct FrameTimes {
  int64_t start_ticks = 0;  // from the first replayed frame's start (the windows' clock)
  std::array<int64_t, static_cast<size_t>(Where::COUNT)> ticks = {};
  std::array<int64_t, static_cast<size_t>(Where::COUNT)> wait_ticks = {};
  std::array<uint32_t, static_cast<size_t>(Where::COUNT)> calls = {};
  std::array<uint32_t, static_cast<size_t>(Where::COUNT)> waits = {};
  int64_t frame_ticks = 0;
};

struct LongCall {
  int64_t ticks = 0;
  uint32_t frame = 0;  // replayed frame
  uint32_t draw = 0;   // draws before it in its frame
  Where where = Where::COUNT;
  uint32_t resource = NO_RESOURCE;
};

struct Result {
  std::string adapter;
  int64_t frequency = 0;
  std::vector<FrameTimes> frames;
  std::vector<LongCall> longest;  // the 16 longest calls, longest first
  std::vector<std::vector<uint32_t>> strips;  // parity: per frame, each draw's strip then each dispatch's row (uint4s)
  std::vector<uint32_t> draws_per_frame;
  std::string plugin_stats;
  uint32_t instances = 1;     // a draw's triangle copies (the GPU load)
  uint32_t region = 8;        // their pixel square
  double expected_gpu_ms = 0.;
  uint32_t failed_resources = 0;
  uint32_t failed_maps = 0;
  uint32_t victims = 0;      // the trap test's
  uint32_t parked_maps = 0;  // victims' maps that went to their stand-ins
  uint32_t warm_maps = 0;    // keep-warm DISCARDs
  // The focus gap: the second window was in the foreground after it took the focus, ours again after the gap.
  bool focus_taken = false;
  bool focus_back = false;
};

inline Result Replay(const Stream& stream, const Options& options) {
  Result result = {.frequency = QpcFrequency()};
  const double ticks_per_us = static_cast<double>(result.frequency) / 1e6;
  std::atomic<int64_t> progress = Qpc();
  // A driver that stops answering: the process ends instead of hanging the run.
  std::jthread watchdog([&](std::stop_token stop) {
    while (!stop.stop_requested()) {
      Sleep(200);
      if (Qpc() - progress.load() > 15 * result.frequency) {
        std::fputs("HANG: no frame for 15 s, the process ends\n", stderr);
        std::fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 3);
      }
    }
  });
  const auto check = [](HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::format("{} failed: 0x{:08x}", what, static_cast<uint32_t>(hr)));
  };

  ComPtr<IDXGIFactory2> factory;
  check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
  ComPtr<IDXGIAdapter1> adapter;
  if (!options.warp) {
    for (UINT index = 0; factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++index) {
      DXGI_ADAPTER_DESC1 description;
      adapter->GetDesc1(&description);
      if (description.VendorId == 0x10DE) break;
      adapter.Reset();
    }
    if (adapter == nullptr) throw std::runtime_error("no NVIDIA adapter");
  }
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(D3D11CreateDevice(adapter.Get(), (adapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_WARP), nullptr,
                          D3D11_CREATE_DEVICE_SINGLETHREADED, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context),
        "D3D11CreateDevice");
  ComPtr<ID3D11Device1> device1;
  ComPtr<ID3D11DeviceContext1> context1;
  check(device.As(&device1), "ID3D11Device1");
  check(context.As(&context1), "ID3D11DeviceContext1");
  {
    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> used_adapter;
    check(device.As(&dxgi_device), "IDXGIDevice");
    check(dxgi_device->GetAdapter(&used_adapter), "GetAdapter");
    DXGI_ADAPTER_DESC description;
    used_adapter->GetDesc(&description);
    char name[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
    result.adapter = name;
  }
  D3D11_FEATURE_DATA_D3D11_OPTIONS caps = {};
  check(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &caps, sizeof(caps)), "CheckFeatureSupport");
  if (caps.ConstantBufferOffsetting == FALSE || caps.MapNoOverwriteOnDynamicConstantBuffer == FALSE) {
    throw std::runtime_error("the device lacks constant buffer offsetting or NO_OVERWRITE on constant buffers");
  }

  // The window and its flip-model swap chain (the game presents flip, sync interval 0 here): a 320 x 180 popup that
  // never takes the focus, or with a focus gap the primary screen's size and in the foreground (the game's window).
  const bool focus = (options.focus_start_s >= 0.);
  const UINT window_width = (focus ? static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN)) : 320);
  const UINT window_height = (focus ? static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN)) : 180);
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class = {.lpfnWndProc = DefWindowProcW, .hInstance = instance, .lpszClassName = L"tw_map_replay"};
  RegisterClassW(&window_class);
  const HWND window = CreateWindowExW((focus ? 0 : WS_EX_NOACTIVATE), L"tw_map_replay", L"tw_map_replay",
                                      WS_POPUP | WS_VISIBLE, 0, 0, static_cast<int>(window_width),
                                      static_cast<int>(window_height), nullptr, nullptr, instance, nullptr);
  if (window == nullptr) throw std::runtime_error("CreateWindowEx failed");
  const auto window_guard = std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>(window, &DestroyWindow);
  if (focus) SetForegroundWindow(window);
  // The focus gap's second window (while it is open).
  auto focus_window = std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>(nullptr, &DestroyWindow);
  ComPtr<IDXGISwapChain1> swapchain;
  const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {.Width = window_width, .Height = window_height,
                                                .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .SampleDesc = {1, 0},
                                                .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, .BufferCount = 2,
                                                .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD};
  check(factory->CreateSwapChainForHwnd(device.Get(), window, &swapchain_desc, nullptr, nullptr, &swapchain),
        "CreateSwapChainForHwnd");
  // --burst's idle on a high-resolution timer (Sleep's tick would add up to 15.6 ms a frame).
  const auto idle_timer = std::unique_ptr<void, decltype(&CloseHandle)>(
      (options.burst_ms > 0.
           ? CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)
           : nullptr),
      &CloseHandle);
  if (options.burst_ms > 0. && idle_timer == nullptr) throw std::runtime_error("CreateWaitableTimerEx failed");

  // The shaders: every stage reads all 14 constant buffer slots (so the driver sees each bound buffer used).
  static constexpr char SHADERS[] = R"(
#define CB(n) cbuffer cb##n : register(b##n) { uint4 c##n[4096]; };
CB(0) CB(1) CB(2) CB(3) CB(4) CB(5) CB(6) CB(7) CB(8) CB(9) CB(10) CB(11) CB(12) CB(13)
static const uint INDICES[4] = {0, 1, 16, 255};
uint4 ReadSlot(uint slot, uint index) {
  switch (slot) {
    case 0: return c0[index]; case 1: return c1[index]; case 2: return c2[index]; case 3: return c3[index];
    case 4: return c4[index]; case 5: return c5[index]; case 6: return c6[index]; case 7: return c7[index];
    case 8: return c8[index]; case 9: return c9[index]; case 10: return c10[index]; case 11: return c11[index];
    case 12: return c12[index]; default: return c13[index];
  }
}
struct VsOut { float4 position : SV_Position; nointerpolation uint4 hash : HASH; nointerpolation uint seed : SEED; };
VsOut VSMain(uint vertex : SV_VertexID, uint v0 : V0, uint v1 : V1, uint v2 : V2, uint v3 : V3) {
  uint4 hash = 2166136261u;
  [unroll] for (uint slot = 0; slot < 14; ++slot) {
    [unroll] for (uint i = 0; i < 4; ++i) hash = (hash ^ ReadSlot(slot, INDICES[i])) * 16777619u;
  }
  VsOut output;
  const float2 uv = float2((vertex << 1) & 2, vertex & 2);
  output.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  output.hash = hash;
  output.seed = v0 ^ v1 ^ v2 ^ v3;
  return output;
}
uint4 PSMain(VsOut input) : SV_Target {
  const uint x = uint(input.position.x) & 63u;
  uint4 value = 0;
  if (x == 0) value = input.hash;
  else if (x <= 56) value = ReadSlot((x - 1) / 4, INDICES[(x - 1) % 4]);
  uint burn = input.seed ^ x;
  [loop] for (uint n = 0; n < LOOP; ++n) burn = burn * 1664525u + 1013904223u;
  if (burn == 0x9E3779B9u) value.w ^= 1u;
  return value;
}
RWStructuredBuffer<uint4> rows : register(u0);
RWByteAddressBuffer counter : register(u1);
[numthreads(1, 1, 1)] void CSMain() {
  uint row;
  counter.InterlockedAdd(0, 1, row);
  row = min(row, MAX_ROWS - 1);
  rows[row * 64] = 0;
  [loop] for (uint x = 1; x < 64; ++x) rows[row * 64 + x] = (x <= 56 ? ReadSlot((x - 1) / 4, INDICES[(x - 1) % 4]) : 0);
}
)";
  const std::string max_rows = std::to_string(MAX_DISPATCH_ROWS);
  const std::array<D3D_SHADER_MACRO, 3> macros = {{{"LOOP", "16"}, {"MAX_ROWS", max_rows.c_str()}, {nullptr, nullptr}}};
  const auto compile = [&](const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(SHADERS, sizeof(SHADERS) - 1, "tw_map_replay", macros.data(), nullptr, entry, profile,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
      throw std::runtime_error(std::format("shader {}: {}", entry,
                                           (errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "?")));
    }
    // The compiler is not the driver: the watchdog's 15 s start again after each shader (x86's compiler takes 3-4 s a
    // shader, more on a busy machine).
    progress = Qpc();
    return code;
  };
  const auto vs_code = compile("VSMain", "vs_5_0");
  const auto ps_code = compile("PSMain", "ps_5_0");
  const auto cs_code = compile("CSMain", "cs_5_0");
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11ComputeShader> compute_shader;
  check(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vertex_shader), "VS");
  check(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &pixel_shader), "PS");
  check(device->CreateComputeShader(cs_code->GetBufferPointer(), cs_code->GetBufferSize(), nullptr, &compute_shader), "CS");
  std::array<D3D11_INPUT_ELEMENT_DESC, 4> elements = {};
  for (UINT index = 0; index < elements.size(); ++index) {
    elements[index] = {.SemanticName = "V", .SemanticIndex = index, .Format = DXGI_FORMAT_R32_UINT, .InputSlot = index,
                       .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA};
  }
  ComPtr<ID3D11InputLayout> layout;
  check(device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), vs_code->GetBufferPointer(),
                                  vs_code->GetBufferSize(), &layout),
        "CreateInputLayout");
  ComPtr<ID3D11RasterizerState> rasterizer;
  const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE,
                                                 .DepthClipEnable = TRUE};
  check(device->CreateRasterizerState(&rasterizer_desc, &rasterizer), "CreateRasterizerState");

  // The target (every draw's strip in parity mode, the load's square otherwise) and the dispatches' rows.
  const uint32_t size = options.target_size;
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = size, .Height = size, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_UINT, .SampleDesc = {1, 0},
                                            .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  check(device->CreateTexture2D(&target_desc, nullptr, &target), "target");
  check(device->CreateRenderTargetView(target.Get(), nullptr, &target_view), "target view");
  const D3D11_BUFFER_DESC rows_desc = {.ByteWidth = MAX_DISPATCH_ROWS * STRIP_PIXELS * 16, .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                       .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 16};
  const D3D11_BUFFER_DESC counter_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT,
                                          .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                          .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
  ComPtr<ID3D11Buffer> rows;
  ComPtr<ID3D11Buffer> counter;
  check(device->CreateBuffer(&rows_desc, nullptr, &rows), "rows");
  check(device->CreateBuffer(&counter_desc, nullptr, &counter), "counter");
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> uavs;
  D3D11_UNORDERED_ACCESS_VIEW_DESC counter_view = {.Format = DXGI_FORMAT_R32_TYPELESS,
                                                   .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
  counter_view.Buffer = {.FirstElement = 0, .NumElements = 4, .Flags = D3D11_BUFFER_UAV_FLAG_RAW};
  check(device->CreateUnorderedAccessView(rows.Get(), nullptr, &uavs[0]), "rows view");
  check(device->CreateUnorderedAccessView(counter.Get(), &counter_view, &uavs[1]), "counter view");
  ComPtr<ID3D11Texture2D> target_readback;
  ComPtr<ID3D11Buffer> rows_readback;
  if (options.parity) {
    D3D11_TEXTURE2D_DESC readback_desc = target_desc;
    readback_desc.Usage = D3D11_USAGE_STAGING;
    readback_desc.BindFlags = 0;
    readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&readback_desc, nullptr, &target_readback), "target readback");
    const D3D11_BUFFER_DESC rows_readback_desc = {.ByteWidth = rows_desc.ByteWidth, .Usage = D3D11_USAGE_STAGING,
                                                  .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    check(device->CreateBuffer(&rows_readback_desc, nullptr, &rows_readback), "rows readback");
  }
  D3D11_VIEWPORT load_viewport = {.Width = 8, .Height = 8, .MaxDepth = 1};
  const auto apply_harness_state = [&] {
    context->IASetInputLayout(layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(pixel_shader.Get(), nullptr, 0);
    context->CSSetShader(compute_shader.Get(), nullptr, 0);
    context->RSSetState(rasterizer.Get());
    context->RSSetViewports(1, &load_viewport);
    context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    const std::array<ID3D11UnorderedAccessView*, 2> views = {uavs[0].Get(), uavs[1].Get()};
    context->CSSetUnorderedAccessViews(0, 2, views.data(), nullptr);
  };
  apply_harness_state();
  const std::array<UINT, 4> zero = {};
  context->ClearUnorderedAccessViewUint(uavs[1].Get(), zero.data());

  // The replayed frames' calls.
  const uint32_t last_frame = std::min(options.last_frame, stream.frames - 1);
  if (stream.frames == 0 || options.first_frame > last_frame) throw std::runtime_error("no frame to replay");
  const auto frame_start = [&](uint32_t frame) {
    return static_cast<size_t>(std::ranges::lower_bound(stream.calls, frame, {}, &Call::frame) - stream.calls.begin());
  };
  const size_t begin = frame_start(options.first_frame);
  const size_t end = frame_start(last_frame + 1);
  const uint32_t replayed_frames = last_frame - options.first_frame + 1;
  // Each stream frame's draws (--extra's calls spread over them).
  std::vector<uint64_t> frame_draws(stream.frames, 0);
  for (size_t index = begin; index < end; ++index) {
    if (stream.calls[index].kind == Kind::DRAW) ++frame_draws[stream.calls[index].frame];
  }
  const double draws_a_frame = std::max(
      1., static_cast<double>(std::accumulate(frame_draws.begin(), frame_draws.end(), uint64_t{0})) / replayed_frames);
  // --extra's buffers, never offered to the plugin.
  if (options.parity && options.extra_push + options.extra_units != 0) {
    throw std::runtime_error("--extra calls: not in parity mode");
  }
  ComPtr<ID3D11Buffer> push_buffer;
  ComPtr<ID3D11Buffer> units_buffer;
  const D3D11_BUFFER_DESC push_desc = {.ByteWidth = 432, .Usage = D3D11_USAGE_DYNAMIC,
                                       .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                       .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  const D3D11_BUFFER_DESC units_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  if (options.extra_push != 0) check(device->CreateBuffer(&push_desc, nullptr, &push_buffer), "the extra push buffer");
  if (options.extra_units != 0) {
    check(device->CreateBuffer(&units_desc, nullptr, &units_buffer), "the extra units buffer");
  }

  // The GPU load: a draw's cost from timestamps at a 256-pixel square, then its copies (or a smaller square).
  if (options.gpu_ms > 0. && !options.parity) {
    D3D11_QUERY_DESC disjoint_desc = {.Query = D3D11_QUERY_TIMESTAMP_DISJOINT};
    D3D11_QUERY_DESC stamp_desc = {.Query = D3D11_QUERY_TIMESTAMP};
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> stamp_begin;
    ComPtr<ID3D11Query> stamp_end;
    check(device->CreateQuery(&disjoint_desc, &disjoint), "query");
    check(device->CreateQuery(&stamp_desc, &stamp_begin), "query");
    check(device->CreateQuery(&stamp_desc, &stamp_end), "query");
    constexpr uint32_t CALIBRATION_INSTANCES = 64;
    const uint32_t region = std::min(256u, size);
    load_viewport.Width = load_viewport.Height = static_cast<float>(region);
    context->RSSetViewports(1, &load_viewport);
    std::vector<double> samples;
    for (int round = 0; round < 5; ++round) {
      context->Begin(disjoint.Get());
      context->End(stamp_begin.Get());
      context->DrawInstanced(3, CALIBRATION_INSTANCES, 0, 0);
      context->End(stamp_end.Get());
      context->End(disjoint.Get());
      context->Flush();
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing = {};
      while (context->GetData(disjoint.Get(), &timing, sizeof(timing), 0) == S_FALSE) {
        progress = Qpc();
        YieldProcessor();
      }
      UINT64 first = 0;
      UINT64 last = 0;
      check(context->GetData(stamp_begin.Get(), &first, sizeof(first), 0), "timestamp");
      check(context->GetData(stamp_end.Get(), &last, sizeof(last), 0), "timestamp");
      if (timing.Disjoint == FALSE && last > first && round > 0) {
        samples.push_back(static_cast<double>(last - first) * 1000. / static_cast<double>(timing.Frequency));
      }
    }
    if (samples.empty()) throw std::runtime_error("GPU load calibration: no valid timestamps");
    std::ranges::sort(samples);
    const double instance_ms = samples[samples.size() / 2] / CALIBRATION_INSTANCES;
    const double copies = options.gpu_ms / (draws_a_frame * instance_ms);
    result.region = region;
    if (copies >= 1.) {
      result.instances = static_cast<uint32_t>(std::lround(copies));
    } else {
      result.region = std::max(8u, static_cast<uint32_t>(region * std::sqrt(copies)));
    }
    result.expected_gpu_ms = draws_a_frame * result.instances * instance_ms
                             * (static_cast<double>(result.region) * result.region / (static_cast<double>(region) * region));
    load_viewport.Width = load_viewport.Height = static_cast<float>(result.region);
    context->RSSetViewports(1, &load_viewport);
  }

  // The stream's resources, then the ring (init_device: nothing bound yet). It gets init_resource / destroy_resource
  // for the game's buffers alone, as in the game (ReShade's own too with `addon_buffers`).
  const RingPlugin& plugin = *options.plugin;
  // The native side (overhead, parity) has no pieces to cap.
  if (options.piece_cap != 0 && &plugin != &NATIVE_PLUGIN) {
    if (plugin.piece_cap == nullptr) throw std::runtime_error(std::format("plugin {} has no piece cap", plugin.name));
    plugin.piece_cap(options.piece_cap);
  }
  if (plugin.attach != nullptr && !plugin.attach(device.Get(), context.Get())) {
    throw std::runtime_error(std::format("plugin {} refused the device", plugin.name));
  }
  const auto offered = [&](size_t index) { return stream.resources[index].game || options.addon_buffers; };
  std::vector<ComPtr<ID3D11Resource>> resources(stream.resources.size());
  for (size_t index = 0; index < stream.resources.size(); ++index) {
    const auto& described = stream.resources[index];
    if (described.width == 0) continue;
    HRESULT hr = E_FAIL;
    const D3D11_USAGE usage = (described.usage == D3D11_USAGE_IMMUTABLE ? D3D11_USAGE_DEFAULT : described.usage);
    if (described.texture) {
      const D3D11_TEXTURE2D_DESC desc = {.Width = described.width, .Height = described.height,
                                         .MipLevels = described.mips, .ArraySize = described.array_size,
                                         .Format = described.format, .SampleDesc = {1, 0}, .Usage = usage,
                                         .BindFlags = described.bind, .CPUAccessFlags = described.cpu,
                                         .MiscFlags = described.misc};
      ComPtr<ID3D11Texture2D> texture;
      hr = device->CreateTexture2D(&desc, nullptr, &texture);
      resources[index] = texture;
    } else {
      const bool structured = (described.misc & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0;
      const D3D11_BUFFER_DESC desc = {.ByteWidth = described.width, .Usage = usage, .BindFlags = described.bind,
                                      .CPUAccessFlags = described.cpu, .MiscFlags = described.misc,
                                      .StructureByteStride = (structured ? 4u : 0u)};
      // Default buffers start zeroed (defined bytes for the parity strips); dynamic and staging ones start unwritten.
      const bool initialized = (usage == D3D11_USAGE_DEFAULT);
      const std::vector<uint8_t> zeros(initialized ? described.width : 0, 0);
      const D3D11_SUBRESOURCE_DATA initial_data = {.pSysMem = zeros.data()};
      ComPtr<ID3D11Buffer> buffer;
      hr = device->CreateBuffer(&desc, (initialized ? &initial_data : nullptr), &buffer);
      if (SUCCEEDED(hr) && plugin.buffer_created != nullptr && offered(index)) {
        plugin.buffer_created(buffer.Get(), desc, (initialized ? zeros.data() : nullptr));
      }
      resources[index] = buffer;
    }
    if (FAILED(hr)) ++result.failed_resources;
  }

  // The trap test's victims (Options::park): the game's dynamic buffers of the class that the replayed frames map.
  if (options.parity && options.keep_warm_bind) throw std::runtime_error("--keep-warm-bind draws: not in parity mode");
  struct Victim {
    uint32_t resource = NO_RESOURCE;
    ID3D11Buffer* buffer = nullptr;  // the game's
    ComPtr<ID3D11Buffer> stand_in;
    uint32_t newest = UINT32_MAX;  // the map ordinal of its newest DISCARD's bytes
    UINT slot = 0;                 // its last bind's
  };
  std::vector<Victim> victims;
  std::vector<uint32_t> victim_index(stream.resources.size(), NO_RESOURCE);
  for (size_t index = begin; index < end && options.park != Park::NONE; ++index) {
    const auto& call = stream.calls[index];
    if (call.kind != Kind::MAP || victim_index[call.resource] != NO_RESOURCE || resources[call.resource] == nullptr) {
      continue;
    }
    const auto& described = stream.resources[call.resource];
    const bool constants = (described.bind & D3D11_BIND_CONSTANT_BUFFER) != 0 && described.width <= 65536;
    const bool vertices = (described.bind & D3D11_BIND_VERTEX_BUFFER) != 0 && described.width > 65536;
    if (described.texture || described.usage != D3D11_USAGE_DYNAMIC || !described.game
        || (options.park == Park::CB && !constants) || (options.park == Park::VB && !vertices)) {
      continue;
    }
    victim_index[call.resource] = static_cast<uint32_t>(victims.size());
    victims.push_back({.resource = call.resource, .buffer = static_cast<ID3D11Buffer*>(resources[call.resource].Get())});
  }
  result.victims = static_cast<uint32_t>(victims.size());
  bool parked = false;
  uint32_t park_changes = 0;
  const auto victim_of = [&](uint32_t resource) -> Victim* {
    if (resource == NO_RESOURCE || victim_index[resource] == NO_RESOURCE) return nullptr;
    return &victims[victim_index[resource]];
  };
  // A victim's maps and binds go to its stand-in while parked.
  const auto actual = [&](uint32_t resource) -> ID3D11Resource* {
    if (auto* victim = victim_of(resource); parked && victim != nullptr) return victim->stand_in.Get();
    return resources[resource].Get();
  };
  const auto buffer_of = [&](uint32_t resource) -> ID3D11Buffer* {
    if (resource == NO_RESOURCE || stream.resources[resource].texture) return nullptr;
    return static_cast<ID3D11Buffer*>(actual(resource));
  };
  std::vector<ID3D11Resource*> mapped_as(resources.size(), nullptr);
  std::vector<ComPtr<ID3DDeviceContextState>> states(stream.states);
  // Each device context state's constant and vertex buffer binds as the replay made them (the OFFSET control's view;
  // the park moves the victims' to and from their stand-ins), and the park changes it has seen.
  struct ConstantBind {
    uint32_t resource = NO_RESOURCE;
    UINT first = 0;
    UINT count = 0;
    bool set1 = false;
  };
  struct VertexBind {
    uint32_t resource = NO_RESOURCE;
    UINT stride = 0;
    UINT offset = 0;
  };
  struct Binds {
    std::array<std::array<ConstantBind, CB_SLOTS>, STAGES> constants = {};
    std::array<VertexBind, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertices = {};
    uint32_t park_changes = 0;
  };
  std::vector<Binds> binds(stream.states);
  uint32_t active_state = 0;
  // A plain Set goes to the plain bodies (the ring hooks Set and Set1 apart).
  const auto set_constants = [&](uint32_t stage, UINT start, UINT count, ID3D11Buffer* const* list, const UINT* firsts,
                                 const UINT* counts, bool set1) {
    if (!set1) {
      switch (stage) {
        case 0: context->VSSetConstantBuffers(start, count, list); break;
        case 1: context->HSSetConstantBuffers(start, count, list); break;
        case 2: context->DSSetConstantBuffers(start, count, list); break;
        case 3: context->GSSetConstantBuffers(start, count, list); break;
        case 4: context->PSSetConstantBuffers(start, count, list); break;
        default: context->CSSetConstantBuffers(start, count, list); break;
      }
      return;
    }
    switch (stage) {
      case 0: context1->VSSetConstantBuffers1(start, count, list, firsts, counts); break;
      case 1: context1->HSSetConstantBuffers1(start, count, list, firsts, counts); break;
      case 2: context1->DSSetConstantBuffers1(start, count, list, firsts, counts); break;
      case 3: context1->GSSetConstantBuffers1(start, count, list, firsts, counts); break;
      case 4: context1->PSSetConstantBuffers1(start, count, list, firsts, counts); break;
      default: context1->CSSetConstantBuffers1(start, count, list, firsts, counts); break;
    }
  };
  // The active state's victim binds again, to what `actual` gives now (the park's start and end, a state swapped in
  // after them).
  const auto rebind = [&] {
    auto& state = binds[active_state];
    state.park_changes = park_changes;
    for (uint32_t stage = 0; stage < STAGES; ++stage) {
      for (UINT slot = 0; slot < CB_SLOTS; ++slot) {
        const auto& bind = state.constants[stage][slot];
        if (victim_of(bind.resource) == nullptr) continue;
        ID3D11Buffer* const buffer = buffer_of(bind.resource);
        set_constants(stage, slot, 1, &buffer, &bind.first, &bind.count, bind.set1);
      }
    }
    for (UINT slot = 0; slot < state.vertices.size(); ++slot) {
      const auto& bind = state.vertices[slot];
      if (victim_of(bind.resource) == nullptr) continue;
      ID3D11Buffer* const buffer = buffer_of(bind.resource);
      context->IASetVertexBuffers(slot, 1, &buffer, &bind.stride, &bind.offset);
    }
  };

  uint32_t map_ordinal = 0;
  uint32_t draw_index = 0;
  uint32_t dispatch_index = 0;
  int64_t frame_begin = Qpc();
  int64_t replay_begin = 0;
  int64_t native_ticks = 0;
  FrameTimes times;
  bool plugin_on = true;
  uint32_t parked_frames = 0;
  const auto timed = [&](Where where, uint32_t resource, auto&& work) {
    const int64_t before = Qpc();
    work();
    if (where == options.test_delay_where) {
      const int64_t until = before + static_cast<int64_t>(options.test_delay_ms * 1000. * ticks_per_us);
      while (Qpc() < until) YieldProcessor();
    }
    const int64_t ticks = Qpc() - before;
    native_ticks += ticks;
    const auto row = static_cast<size_t>(where);
    times.ticks[row] += ticks;
    ++times.calls[row];
    if (ticks >= static_cast<int64_t>(options.wait_ms * 1000. * ticks_per_us)) {
      times.wait_ticks[row] += ticks;
      ++times.waits[row];
    }
    if (result.longest.size() < 16 || ticks > result.longest.back().ticks) {
      const LongCall call = {.ticks = ticks, .frame = static_cast<uint32_t>(result.frames.size()), .draw = draw_index,
                             .where = where, .resource = resource};
      result.longest.insert(std::ranges::upper_bound(result.longest, ticks, std::greater{}, &LongCall::ticks), call);
      if (result.longest.size() > 16) result.longest.pop_back();
    }
  };
  // A native DISCARD of `buffer` with the victim's newest bytes (its stand-in at the park's start; the victim at its
  // end, the ring's write-back, and for keep-warm), timed in plugin_present's row (where the ring would do it).
  const auto write_newest = [&](ID3D11Buffer* buffer, const Victim& victim) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = S_OK;
    timed(Where::PLUGIN_PRESENT, victim.resource,
          [&] { hr = context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped); });
    if (FAILED(hr)) {
      ++result.failed_maps;
      return;
    }
    const uint32_t count = (victim.newest == UINT32_MAX
                                ? 0
                                : FillWords(stream.resources[victim.resource], D3D11_MAP_WRITE_DISCARD));
    auto* target_words = static_cast<uint32_t*>(mapped.pData);
    for (uint32_t word = 0; word < count; ++word) target_words[word] = FillWord(victim.newest, word);
    context->Unmap(buffer, 0);
  };
  // A replayed frame's start: the windows' clock, the plugin's row, the park's start or end, keep-warm.
  const auto begin_frame = [&] {
    times.start_ticks = (options.seconds_per_frame > 0.
                             ? static_cast<int64_t>(static_cast<double>(result.frames.size()) * options.seconds_per_frame
                                                    * static_cast<double>(result.frequency))
                             : frame_begin - replay_begin);
    const double at = static_cast<double>(times.start_ticks) / static_cast<double>(result.frequency);
    const auto inside = [at](double start, double length) { return at >= start && at < start + length; };
    plugin_on = (options.plugin_start_s < 0. || inside(options.plugin_start_s, options.plugin_length_s));
    if (const bool gap = focus && inside(options.focus_start_s, options.focus_length_s);
        gap != (focus_window != nullptr)) {
      if (gap) {
        focus_window.reset(CreateWindowExW(WS_EX_TOPMOST, L"tw_map_replay", L"tw_map_replay focus",
                                           WS_POPUP | WS_VISIBLE, 0, 0, 640, 360, nullptr, nullptr, instance, nullptr));
        if (focus_window == nullptr) throw std::runtime_error("the focus gap's window: CreateWindowEx failed");
        SetForegroundWindow(focus_window.get());
        result.focus_taken = (GetForegroundWindow() == focus_window.get());
      } else {
        focus_window.reset();
        SetForegroundWindow(window);
        result.focus_back = (GetForegroundWindow() == window);
      }
    }
    if (const bool park = !victims.empty() && inside(options.park_start_s, options.park_length_s); park != parked) {
      for (auto& victim : victims) {
        if (park && victim.stand_in == nullptr) {
          D3D11_BUFFER_DESC desc;
          victim.buffer->GetDesc(&desc);
          check(device->CreateBuffer(&desc, nullptr, &victim.stand_in), "a park stand-in");
        }
        write_newest((park ? victim.stand_in.Get() : victim.buffer), victim);
      }
      parked = park;
      ++park_changes;
      if (park || options.control != Control::PARK) rebind();
    }
    if (!parked || options.keep_warm == 0 || ++parked_frames % options.keep_warm != 0) return;
    for (auto& victim : victims) {
      write_newest(victim.buffer, victim);
      ++result.warm_maps;
      if (!options.keep_warm_bind) continue;
      // One draw with it bound at its own slot (a constant buffer in the PS), the slot's bind put back.
      const uint32_t bind = stream.resources[victim.resource].bind;
      ComPtr<ID3D11Buffer> saved;
      timed(Where::PLUGIN_PRESENT, victim.resource, [&] {
        if ((bind & D3D11_BIND_CONSTANT_BUFFER) != 0) {
          UINT first = 0;
          UINT count = 0;
          context1->PSGetConstantBuffers1(victim.slot, 1, &saved, &first, &count);
          context->PSSetConstantBuffers(victim.slot, 1, &victim.buffer);
          context->DrawInstanced(3, 1, 0, 0);
          context1->PSSetConstantBuffers1(victim.slot, 1, saved.GetAddressOf(), &first, &count);
        } else if ((bind & D3D11_BIND_VERTEX_BUFFER) != 0) {
          UINT stride = 0;
          UINT offset = 0;
          context->IAGetVertexBuffers(victim.slot, 1, &saved, &stride, &offset);
          context->IASetVertexBuffers(victim.slot, 1, &victim.buffer, &stride, &offset);
          context->DrawInstanced(3, 1, 0, 0);
          context->IASetVertexBuffers(victim.slot, 1, saved.GetAddressOf(), &stride, &offset);
        }
      });
    }
  };
  // `state_only`: a frame before the first replayed one (its binds, swaps and clears; nothing timed).
  const auto issue = [&](const Call& call, bool state_only) {
    const bool binding = (call.kind == Kind::SET || call.kind == Kind::SET1 || call.kind == Kind::VB
                          || call.kind == Kind::SWAP || call.kind == Kind::CLEAR);
    if (state_only && !binding) return;
    if (!state_only && options.pace && options.burst_ms <= 0.) {
      const auto target = static_cast<int64_t>(call.pace_us * ticks_per_us);
      while (Qpc() - frame_begin - native_ticks < target) YieldProcessor();
    }
    const auto run = [&](Where where, uint32_t resource, auto&& work) {
      if (state_only) {
        work();
      } else {
        timed(where, resource, work);
      }
    };
    switch (call.kind) {
      case Kind::MAP: {
        const auto& described = stream.resources[call.resource];
        ID3D11Resource* resource = actual(call.resource);
        if (resource == nullptr) return;
        Where where = Where::OTHER_MAP;
        if (described.texture) {
          where = Where::TEXTURE_MAP;
        } else if ((described.bind & D3D11_BIND_CONSTANT_BUFFER) != 0) {
          where = (call.map_type == D3D11_MAP_WRITE_DISCARD ? Where::CB_DISCARD : Where::CB_OTHER_MAP);
        } else if ((described.bind & D3D11_BIND_VERTEX_BUFFER) != 0) {
          where = (call.map_type == D3D11_MAP_WRITE_DISCARD ? Where::VB_DISCARD : Where::VB_OTHER_MAP);
        } else if ((described.bind & D3D11_BIND_INDEX_BUFFER) != 0) {
          where = Where::IB_MAP;
        }
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = S_OK;
        run(where, call.resource, [&] { hr = context->Map(resource, call.subresource, call.map_type, 0, &mapped); });
        if (FAILED(hr)) {
          ++result.failed_maps;
          return;
        }
        mapped_as[call.resource] = resource;
        const uint32_t ordinal = map_ordinal++;
        if (auto* victim = victim_of(call.resource); victim != nullptr) {
          if (call.map_type == D3D11_MAP_WRITE_DISCARD) victim->newest = ordinal;
          if (parked) ++result.parked_maps;
        }
        const uint32_t count = (described.texture ? std::min(FillWords(described, call.map_type), mapped.RowPitch / 4)
                                                  : FillWords(described, call.map_type));
        auto* target_words = static_cast<uint32_t*>(mapped.pData);
        for (uint32_t word = 0; word < count; ++word) {
          target_words[word] = FillWord(ordinal, word);
        }
        if (options.control == Control::UPLOAD && where == Where::CB_DISCARD && ordinal % 5 == 2 && count != 0) {
          target_words[0] ^= 1u;
        }
        return;
      }
      case Kind::UNMAP: {
        // What its map went to (a map before the park's start or end unmaps that one).
        ID3D11Resource* const mapped = std::exchange(mapped_as[call.resource], nullptr);
        ID3D11Resource* resource = (mapped != nullptr ? mapped : actual(call.resource));
        if (resource == nullptr) return;
        run(Where::UNMAP, call.resource, [&] { context->Unmap(resource, call.subresource); });
        return;
      }
      case Kind::SET:
      case Kind::SET1: {
        std::array<ID3D11Buffer*, CB_SLOTS> buffers = {};
        std::array<UINT, CB_SLOTS> firsts = {};
        std::array<UINT, CB_SLOTS> counts = {};
        auto& stage_binds = binds[active_state].constants[call.stage];
        for (uint32_t index = 0; index < call.count; ++index) {
          const auto& slot = stream.slots[call.first_slot + index];
          buffers[index] = buffer_of(slot.resource);
          firsts[index] = (slot.count_or_offset == 0 ? 0 : slot.first_or_stride);
          counts[index] = (slot.count_or_offset == 0 ? WHOLE_BUFFER_CONSTANTS : slot.count_or_offset);
          if (call.kind == Kind::SET1 && options.control == Control::WINDOW && slot.count_or_offset != 0) {
            counts[index] = std::min(counts[index] + 16, WHOLE_BUFFER_CONSTANTS - firsts[index]);
          }
          if (call.kind == Kind::SET1 && options.control == Control::OFFSET && slot.resource != NO_RESOURCE) {
            for (uint32_t other = 0; other < CB_SLOTS; ++other) {
              if (other != call.start_slot + index && stage_binds[other].resource == slot.resource) {
                firsts[index] = stage_binds[other].first;
                break;
              }
            }
          }
          stage_binds[call.start_slot + index] = {.resource = slot.resource, .first = firsts[index],
                                                  .count = counts[index], .set1 = (call.kind == Kind::SET1)};
          if (auto* victim = victim_of(slot.resource); victim != nullptr) victim->slot = call.start_slot + index;
        }
        run(Where::BIND, NO_RESOURCE, [&] {
          set_constants(call.stage, call.start_slot, call.count, buffers.data(), firsts.data(), counts.data(),
                        call.kind == Kind::SET1);
        });
        return;
      }
      case Kind::VB: {
        std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> buffers = {};
        std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides = {};
        std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> offsets = {};
        for (uint32_t index = 0; index < call.count; ++index) {
          const auto& slot = stream.slots[call.first_slot + index];
          buffers[index] = buffer_of(slot.resource);
          strides[index] = slot.first_or_stride;
          offsets[index] = slot.count_or_offset;
          binds[active_state].vertices[call.start_slot + index] = {.resource = slot.resource, .stride = strides[index],
                                                                   .offset = offsets[index]};
          if (auto* victim = victim_of(slot.resource); victim != nullptr) victim->slot = call.start_slot + index;
        }
        run(Where::BIND, NO_RESOURCE, [&] {
          context->IASetVertexBuffers(call.start_slot, call.count, buffers.data(), strides.data(), offsets.data());
        });
        return;
      }
      case Kind::SWAP: {
        auto& next = states[call.resource];
        if (next == nullptr) {
          check(device1->CreateDeviceContextState(D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &next),
                "CreateDeviceContextState");
        }
        ComPtr<ID3DDeviceContextState> previous;
        run(Where::SWAP_CLEAR, NO_RESOURCE, [&] { context1->SwapDeviceContextState(next.Get(), &previous); });
        if (states[call.previous_state] == nullptr) states[call.previous_state] = previous;
        active_state = call.resource;
        apply_harness_state();
        if (binds[active_state].park_changes != park_changes) rebind();
        return;
      }
      case Kind::CLEAR:
        run(Where::SWAP_CLEAR, NO_RESOURCE, [&] { context->ClearState(); });
        binds[active_state] = {.park_changes = park_changes};
        apply_harness_state();
        return;
      case Kind::DRAW: {
        if (options.parity) {
          if (draw_index >= size * (size / STRIP_PIXELS)) throw std::runtime_error("more draws than strips");
          const D3D11_VIEWPORT strip = {.TopLeftX = static_cast<float>((draw_index / size) * STRIP_PIXELS),
                                        .TopLeftY = static_cast<float>(draw_index % size),
                                        .Width = static_cast<float>(STRIP_PIXELS), .Height = 1, .MaxDepth = 1};
          context->RSSetViewports(1, &strip);
        }
        // --extra's calls due before this draw (spread evenly over the frame's draws).
        const uint64_t draws = frame_draws[call.frame];
        const uint64_t drawn = draw_index;
        for (uint64_t extra = drawn * options.extra_push / draws; extra < (drawn + 1) * options.extra_push / draws;
             ++extra) {
          D3D11_MAPPED_SUBRESOURCE mapped = {};
          HRESULT hr = S_OK;
          timed(Where::CB_DISCARD, NO_RESOURCE,
                [&] { hr = context->Map(push_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped); });
          if (FAILED(hr)) {
            ++result.failed_maps;
            break;
          }
          auto* target_words = static_cast<uint32_t*>(mapped.pData);
          for (uint32_t word = 0; word < push_desc.ByteWidth / 4; ++word) {
            target_words[word] = FillWord(static_cast<uint32_t>(extra), word);
          }
          timed(Where::UNMAP, NO_RESOURCE, [&] { context->Unmap(push_buffer.Get(), 0); });
          timed(Where::BIND, NO_RESOURCE,
                [&] { context->PSSetConstantBuffers(CB_SLOTS - 1, 1, push_buffer.GetAddressOf()); });
        }
        for (uint64_t extra = drawn * options.extra_units / draws; extra < (drawn + 1) * options.extra_units / draws;
             ++extra) {
          const std::array<uint32_t, 4> range = {static_cast<uint32_t>(extra), 1, 0, 0};
          timed(Where::CB_OTHER_MAP, NO_RESOURCE,
                [&] { context->UpdateSubresource(units_buffer.Get(), 0, nullptr, range.data(), 0, 0); });
          timed(Where::BIND, NO_RESOURCE, [&] { context->CSSetConstantBuffers(0, 1, units_buffer.GetAddressOf()); });
          timed(Where::DISPATCH, NO_RESOURCE, [&] { context->Dispatch(1, 1, 1); });
        }
        const uint32_t copies = (options.parity ? 1 : result.instances);
        run(Where::DRAW, NO_RESOURCE, [&] { context->DrawInstanced(3, copies, 0, 0); });
        ++draw_index;
        return;
      }
      case Kind::DISPATCH:
        if (options.parity && dispatch_index >= MAX_DISPATCH_ROWS) throw std::runtime_error("more dispatches than rows");
        run(Where::DISPATCH, NO_RESOURCE, [&] { context->Dispatch(1, 1, 1); });
        ++dispatch_index;
        return;
      case Kind::PRESENT: {
        if (options.parity) {
          context->CopyResource(target_readback.Get(), target.Get());
          context->CopyResource(rows_readback.Get(), rows.Get());
          std::vector<uint32_t> strip(static_cast<size_t>(draw_index + dispatch_index) * STRIP_PIXELS * 4);
          D3D11_MAPPED_SUBRESOURCE mapped = {};
          check(context->Map(target_readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "target readback map");
          for (uint32_t draw = 0; draw < draw_index; ++draw) {
            const auto* row = static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(draw % size) * mapped.RowPitch)
                              + (static_cast<size_t>(draw / size) * STRIP_PIXELS * 16);
            std::memcpy(strip.data() + (static_cast<size_t>(draw) * STRIP_PIXELS * 4), row, STRIP_PIXELS * 16);
          }
          context->Unmap(target_readback.Get(), 0);
          check(context->Map(rows_readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "rows readback map");
          std::memcpy(strip.data() + (static_cast<size_t>(draw_index) * STRIP_PIXELS * 4), mapped.pData,
                      static_cast<size_t>(dispatch_index) * STRIP_PIXELS * 16);
          context->Unmap(rows_readback.Get(), 0);
          result.strips.push_back(std::move(strip));
          const std::array<float, 4> clear = {};
          context->ClearRenderTargetView(target_view.Get(), clear.data());
          context->ClearUnorderedAccessViewUint(uavs[1].Get(), zero.data());
        }
        if (plugin.present != nullptr) {
          timed(Where::PLUGIN_PRESENT, NO_RESOURCE, [&] { plugin.present(context.Get(), plugin_on); });
        }
        HRESULT hr = S_OK;
        timed(Where::PRESENT, NO_RESOURCE, [&] { hr = swapchain->Present(0, 0); });
        if (FAILED(hr) || FAILED(device->GetDeviceRemovedReason())) {
          throw std::runtime_error(std::format("DEVICE REMOVED or Present failed: present 0x{:08x}, removed reason 0x{:08x}",
                                               static_cast<uint32_t>(hr),
                                               static_cast<uint32_t>(device->GetDeviceRemovedReason())));
        }
        if (!options.parity) context->ClearUnorderedAccessViewUint(uavs[1].Get(), zero.data());
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        // --burst: idle to the frame's start + burst_ms.
        const double idle_us = (options.burst_ms * 1000.) - (static_cast<double>(Qpc() - frame_begin) / ticks_per_us);
        if (options.burst_ms > 0. && idle_us > 0.) {
          const LARGE_INTEGER due = {.QuadPart = -static_cast<LONGLONG>(idle_us * 10.)};  // relative, in 100 ns
          SetWaitableTimer(idle_timer.get(), &due, 0, nullptr, nullptr, FALSE);
          WaitForSingleObject(idle_timer.get(), INFINITE);
        }
        times.frame_ticks = Qpc() - frame_begin;
        result.frames.push_back(times);
        result.draws_per_frame.push_back(draw_index);
        times = {};
        draw_index = 0;
        dispatch_index = 0;
        native_ticks = 0;
        frame_begin = Qpc();
        progress = frame_begin;
        return;
      }
    }
  };
  for (size_t index = 0; index < begin; ++index) issue(stream.calls[index], true);
  frame_begin = Qpc();
  replay_begin = frame_begin;
  for (uint32_t loop = 0; loop < options.loops; ++loop) {
    for (size_t index = begin; index < end; ++index) {
      if (index == begin || stream.calls[index - 1].kind == Kind::PRESENT) begin_frame();
      issue(stream.calls[index], false);
    }
  }
  if (plugin.stats != nullptr) result.plugin_stats = plugin.stats();
  if (plugin.detach != nullptr) plugin.detach();
  for (size_t index = 0; index < resources.size(); ++index) {
    ComPtr<ID3D11Buffer> buffer;
    if (resources[index] != nullptr && plugin.buffer_destroyed != nullptr && offered(index)
        && SUCCEEDED(resources[index].As(&buffer))) {
      plugin.buffer_destroyed(buffer.Get());
    }
  }
  context->ClearState();
  context->Flush();
  return result;
}

// The first difference between two parity runs ("" when every strip is equal bit for bit).
inline std::string CompareStrips(const Result& off, const Result& on) {
  if (off.strips.size() != on.strips.size()) {
    return std::format("{} frames against {}", off.strips.size(), on.strips.size());
  }
  uint64_t rows = 0;
  std::string first;
  for (size_t frame = 0; frame < off.strips.size(); ++frame) {
    const auto& a = off.strips[frame];
    const auto& b = on.strips[frame];
    if (a.size() != b.size()) return std::format("frame {}: {} words against {}", frame, a.size(), b.size());
    for (size_t row = 0; row < a.size() / (STRIP_PIXELS * 4); ++row) {
      const size_t offset = row * STRIP_PIXELS * 4;
      if (std::memcmp(a.data() + offset, b.data() + offset, STRIP_PIXELS * 16) == 0) continue;
      ++rows;
      if (!first.empty()) continue;
      size_t word = offset;
      while (a[word] == b[word]) ++word;
      const size_t pixel = (word - offset) / 4;
      const bool dispatch = row >= off.draws_per_frame[frame];
      first = std::format("frame {} {} {} pixel {} ({}): 0x{:08x} against 0x{:08x}", frame,
                          (dispatch ? "dispatch" : "draw"), (dispatch ? row - off.draws_per_frame[frame] : row), pixel,
                          (pixel == 0 ? std::string("VS hash")
                                      : std::format("slot {} constant {}", (pixel - 1) / 4, READ_INDICES[(pixel - 1) % 4])),
                          a[word], b[word]);
    }
  }
  if (rows == 0) return "";
  return std::format("{} rows differ; first: {}", rows, first);
}

struct Summary {
  std::array<double, static_cast<size_t>(Where::COUNT)> ms = {};  // a frame
  std::array<double, static_cast<size_t>(Where::COUNT)> wait_ms = {};
  std::array<double, static_cast<size_t>(Where::COUNT)> calls = {};
  std::array<double, static_cast<size_t>(Where::COUNT)> waits = {};
  double frame_ms = 0.;
  double call_ms = 0.;  // every replayed call but DXGI's Present: the render thread's D3D11 CPU (overhead)
  // The calls the ring hooks (maps, unmaps, binds, swaps, clears) and its present callback: its added cost without
  // the draws' noise.
  double hooked_ms = 0.;
  uint32_t frames = 0;
};

// The frames from `skip` on.
inline Summary Summarize(const Result& result, uint32_t skip) {
  Summary summary;
  const double ms_per_tick = 1000. / static_cast<double>(result.frequency);
  for (size_t frame = skip; frame < result.frames.size(); ++frame) {
    const auto& times = result.frames[frame];
    for (size_t row = 0; row < summary.ms.size(); ++row) {
      summary.ms[row] += static_cast<double>(times.ticks[row]) * ms_per_tick;
      summary.wait_ms[row] += static_cast<double>(times.wait_ticks[row]) * ms_per_tick;
      summary.calls[row] += times.calls[row];
      summary.waits[row] += times.waits[row];
    }
    summary.frame_ms += static_cast<double>(times.frame_ticks) * ms_per_tick;
    ++summary.frames;
  }
  if (summary.frames == 0) return summary;
  for (size_t row = 0; row < summary.ms.size(); ++row) {
    summary.ms[row] /= summary.frames;
    summary.wait_ms[row] /= summary.frames;
    summary.calls[row] /= summary.frames;
    summary.waits[row] /= summary.frames;
    if (row != static_cast<size_t>(Where::PRESENT)) summary.call_ms += summary.ms[row];
    if (row != static_cast<size_t>(Where::PRESENT) && row != static_cast<size_t>(Where::DRAW)
        && row != static_cast<size_t>(Where::DISPATCH)) {
      summary.hooked_ms += summary.ms[row];
    }
  }
  summary.frame_ms /= summary.frames;
  return summary;
}

// The wait locator: where the render thread's time in D3D11 calls goes, a frame.
inline std::string Report(const Stream& stream, const Result& result, const Summary& summary, double wait_ms) {
  std::string text = std::format("adapter: {}; {} frames summarized; frame {:.3f} ms; D3D11 calls (not Present) {:.3f} ms/f\n",
                                 result.adapter, summary.frames, summary.frame_ms, summary.call_ms);
  if (result.expected_gpu_ms > 0.) {
    text += std::format("GPU load: {} copies of a {} px square a draw, E {:.1f} ms/f\n", result.instances, result.region,
                        result.expected_gpu_ms);
  }
  text += std::format("{:<15} {:>10} {:>10} {:>12} {:>10}\n", "where", "ms/f", "calls/f",
                      std::format("waits>={}ms/f", wait_ms), "wait ms/f");
  for (size_t row = 0; row < summary.ms.size(); ++row) {
    if (summary.calls[row] == 0.) continue;
    text += std::format("{:<15} {:>10.3f} {:>10.1f} {:>12.2f} {:>10.3f}\n", WHERE_NAMES[row], summary.ms[row],
                        summary.calls[row], summary.waits[row], summary.wait_ms[row]);
  }
  text += "longest calls:\n";
  const double ms_per_tick = 1000. / static_cast<double>(result.frequency);
  for (const auto& call : result.longest) {
    std::string resource = "-";
    if (call.resource != NO_RESOURCE) {
      const auto& described = stream.resources[call.resource];
      resource = std::format("0x{:x} {} {} B", described.trace_pointer, (described.texture ? "texture" : "buffer"),
                             described.width);
    }
    text += std::format("  {:8.3f} ms  frame {} after draw {}  {}  {}\n", static_cast<double>(call.ticks) * ms_per_tick,
                        call.frame, call.draw, WHERE_NAMES[static_cast<size_t>(call.where)], resource);
  }
  if (result.failed_resources + result.failed_maps != 0) {
    text += std::format("failed: {} resources, {} maps\n", result.failed_resources, result.failed_maps);
  }
  if (!result.plugin_stats.empty()) text += std::format("plugin: {}\n", result.plugin_stats);
  return text;
}

// The trap test's phases (s5r_ 3.3) around a window [start_s, + length_s), by each frame's start: before (the 10 s
// before it), idle (in it), after (its first 10 s after), late (the next 10 s).
constexpr std::array<std::string_view, 4> PHASES = {"before", "idle", "after", "late"};
constexpr std::array<Where, 2> TRAP_ROWS = {Where::CB_DISCARD, Where::VB_DISCARD};
inline std::array<double, 5> PhaseEdges(double start_s, double length_s) {
  return {start_s - 10., start_s, start_s + length_s, start_s + length_s + 10., start_s + length_s + 20.};
}
inline std::array<Summary, 4> Phases(const Result& result, double start_s, double length_s) {
  const auto edges = PhaseEdges(start_s, length_s);
  std::array<Summary, 4> phases;
  for (size_t phase = 0; phase < PHASES.size(); ++phase) {
    Result part = {.frequency = result.frequency};
    std::ranges::copy_if(result.frames, std::back_inserter(part.frames), [&](const FrameTimes& times) {
      const double at = static_cast<double>(times.start_ticks) / static_cast<double>(result.frequency);
      return at >= edges[phase] && at < edges[phase + 1];
    });
    phases[phase] = Summarize(part, 0);
  }
  return phases;
}

// A DISCARD row's step: the smaller of after - before and late - before (ms/f of waits; -1 without after or late
// frames).
inline double TrapStep(const std::array<Summary, 4>& phases, Where where) {
  const auto row = static_cast<size_t>(where);
  if (phases[2].frames == 0 || phases[3].frames == 0) return -1.;
  return std::min(phases[2].wait_ms[row], phases[3].wait_ms[row]) - phases[0].wait_ms[row];
}

// Per DISCARD row: TRAP yes when the after and late phases both wait at least max(2 ms/f, 5x the before phase's
// waits) (s5r_), or both at least 2 ms/f more than it (s5x_: the first mark is blind for a row that already waits), no
// recovery.
inline std::string PhaseReport(const Result& result, double start_s, double length_s) {
  const auto edges = PhaseEdges(start_s, length_s);
  const auto phases = Phases(result, start_s, length_s);
  std::string text = std::format("phases around {:.1f} s + {:.1f} s\n{:<7} {:>13} {:>6} {:>9}", start_s, length_s,
                                 "phase", "seconds", "frames", "frame ms");
  for (const Where where : TRAP_ROWS) {
    text += std::format(" {:>15} {:>8} {:>10}", std::format("{} ms/f", WHERE_NAMES[static_cast<size_t>(where)]),
                        "waits/f", "wait ms/f");
  }
  text += "\n";
  for (size_t phase = 0; phase < PHASES.size(); ++phase) {
    const auto& summary = phases[phase];
    text += std::format("{:<7} {:>13} {:>6} {:>9.2f}", PHASES[phase],
                        std::format("{:.1f}-{:.1f}", edges[phase], edges[phase + 1]), summary.frames, summary.frame_ms);
    for (const Where where : TRAP_ROWS) {
      const auto row = static_cast<size_t>(where);
      text += std::format(" {:>15.3f} {:>8.2f} {:>10.3f}", summary.ms[row], summary.waits[row], summary.wait_ms[row]);
    }
    text += "\n";
  }
  for (const Where where : TRAP_ROWS) {
    const auto row = static_cast<size_t>(where);
    const double before = phases[0].wait_ms[row];
    const double mark = std::max(2., 5. * before);
    const bool relative = phases[2].wait_ms[row] >= mark && phases[3].wait_ms[row] >= mark;
    const bool trapped = TrapStep(phases, where) >= 0. && (relative || TrapStep(phases, where) >= 2.);
    text += std::format(
        "TRAP {}: {} (DISCARD waits before {:.3f}, after {:.3f} ({:+.3f}), late {:.3f} ({:+.3f}) ms/f; the marks: "
        "max(2, 5x before) {:.3f}, before + 2 {:.3f})\n",
        WHERE_NAMES[row], (trapped ? "yes" : "no"), before, phases[2].wait_ms[row], phases[2].wait_ms[row] - before,
        phases[3].wait_ms[row], phases[3].wait_ms[row] - before, mark, before + 2.);
  }
  return text;
}

// A run without a window (the trigger attempts): the phases around the second (10 s on, 1 s steps, while 20 s of frames
// follow it) with the largest step of a DISCARD row (TrapStep), their TRAP lines as PhaseReport's.
inline std::string ScanReport(const Result& result) {
  if (result.frames.empty()) return "";
  const double last_s = static_cast<double>(result.frames.back().start_ticks) / static_cast<double>(result.frequency);
  double step_at = 10.;
  double largest = -1.;
  for (double start_s = 10.; start_s + 20. <= last_s; start_s += 1.) {
    const auto phases = Phases(result, start_s, 0.);
    for (const Where where : TRAP_ROWS) {
      if (const double step = TrapStep(phases, where); step > largest) {
        largest = step;
        step_at = start_s;
      }
    }
  }
  if (largest < 0.) return std::format("phase scan: {:.1f} s of frames, under the 30 s a scan needs\n", last_s);
  return std::format("phase scan: 10-{:.0f} s, the largest DISCARD wait step {:+.3f} ms/f at {:.1f} s\n{}",
                     last_s - 20., largest, step_at, PhaseReport(result, step_at, 0.));
}

}  // namespace tw_map_replay
