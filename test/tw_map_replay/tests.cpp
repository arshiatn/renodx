// tw_map_replay's WARP tests (run.ps1): the readers, the replay, the parity check and the wait locator, each with
// controls that must fail.
// 1. v3 reader: a written v3 stream (S1's writer format: seeds, Map / Unmap, Set, Set1 windows, a vertex buffer, a
//    staging read, a swap to a private state and back, a ClearState, a dispatch, a failed DO_NOT_WAIT poll, an unmap of
//    a map made before the trace) with tw_map_trace.txt's descriptions beside it: the calls, slots, windows, states,
//    descriptions (one assumed) and the pacing (qpc minus the frame's start minus the native times before); the game's
//    buffers (a v1 map line or a v3 buffer line, through ReShade's proxy; not B, mapped only below it, nor the
//    bound-only S), every buffer without tw_map_trace.txt. CONTROLS: a map line removed (the header's native_calls no
//    longer match), a bad slot: both refused.
//    S1's own v3 output (testbin\tw_map_trace_amd64c, when there): read, its header's counts.
// 2. v2 reader: a written v2 trace and its waits: the draws before each map at its draw index (a dispatch right before
//    a map from last=dispatch), the assumed binds (a once-a-frame CB on a dedicated slot from the start, a per-draw one
//    on a shared slot from its unmap, a vertex buffer at slot 0), the pacing with the ETL waits out, and with the gap
//    cap. W3m's v2 trace (batch 1, when there): frame 1's draws and maps as its frame line says.
// 3. The replay on WARP (parity mode): every strip pixel equals a CPU model of D3D11's constant buffer reads (the
//    written bytes, a NO_OVERWRITE map keeping the bytes, Set1 windows and the zeros past them, a 16 B buffer's zeros past its
//    end, one buffer at two offsets in two slots, a swap's private state and back, ClearState, the compute rows).
//    CONTROL: a model that ignores the windows disagrees.
// 4. Parity: native against native on two fresh devices, equal bit for bit; the plugin seam's callbacks (attach, the
//    game's buffers alone, never B or S, every present, detach). CONTROL: --addon-buffers ring offers every buffer.
//    CONTROLS: upload, window, offset each differ.
// 5. The wait locator: a 2 ms delay inside every CB DISCARD lands in cb_discard (>= 5 ms/f: the mark met); CONTROL:
//    the delay in vertex buffer maps leaves cb_discard under 1 ms/f. The calls a frame per row as the stream has them.
// 6. The trap test (a clock of 1 s a frame): native against the CB victims (A, C) parked in frames 2-4, equal bit for
//    bit, every victim map in the window to a stand-in. CONTROL: the park's end leaving the binds on the stand-ins
//    differs. Keep-warm every frame with a bind (6 DISCARDs), the plugin's row on in its window only; CONTROL:
//    --keep-warm-bind refused in parity mode. The phases of synthetic frames: TRAP yes for waits after the window;
//    CONTROL: a late phase that recovers says no.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "./map_replay.hpp"

namespace {

namespace replay = tw_map_replay;
using replay::Where;

int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

void WriteText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

bool Throws(const std::function<void()>& work) {
  try {
    work();
  } catch (const std::exception& error) {
    std::printf("  (refused as expected: %s)\n", error.what());
    return true;
  }
  return false;
}

// The synthetic v3 stream: frame 0 at qpc 10,000,000, each line 10 us after the one before, every native call 2 us.
struct V3Writer {
  std::string stream;
  uint32_t frame = 0;
  uint32_t line = 0;
  uint64_t sequence = 0;
  uint32_t native_calls = 0;
  uint32_t binds = 0;

  void Call(std::string_view kind, std::string_view rest) {
    const bool draw = (kind == "draw");
    const uint64_t qpc = 10000000 + (frame * 1000000) + (++line * 100);
    stream += std::format("{} seq={} frame={} qpc={} ms=0{} {}\n", kind, sequence++, frame, qpc,
                          (draw ? "" : " native_ms=0.0020 nested=0"), rest);
    if (kind == "map" || kind == "unmap") {
      ++native_calls;
    } else if (!draw) {
      ++binds;
    }
  }
  void Map(std::string_view pointer, std::string_view type, std::string_view flags = "0x0", std::string_view hr = "0x00000000") {
    Call("map", std::format("res={} sub=0 map={} flags={} hr={} bytes=0", pointer, type, flags, hr));
  }
  void Unmap(std::string_view pointer) { Call("unmap", std::format("res={} sub=0 content=-", pointer)); }
  void Draw() { Call("draw", "kind=draw index=0 vs=00000000 ps=00000000 args=3,1,0,0,0 indirect=0x0 state=0x0 cbs=- vbs=-"); }

  std::string Text(uint32_t frames) const {
    std::string text = std::format(
        "v3 setting=0,{0},hash qpc_frequency=10000000 frames={0} end_qpc={1} hook=on native_calls={2}"
        " dropped_native_calls=0 binds={3} dropped_binds=0 draws=0 dropped_draws=0 buffers=2 dropped_buffers=0"
        " first_drop_seq=- hash=on hash_ms=0\n",
        frames, 10000000 + (frames * 1000000), native_calls, binds);
    for (uint32_t index = 0; index <= frames; ++index) text += std::format("present qpc={}\n", 10000000 + (index * 1000000));
    text += "buffer create_qpc=1 thread=1 destroy_qpc=0 destroy_thread=0 res=0x1000 usage=dynamic bind=0x4 cpu=0x10000"
            " misc=0x0 type=buffer bytes=512\n";
    text += "buffer create_qpc=1 thread=1 destroy_qpc=0 destroy_thread=0 res=0x3000 usage=dynamic bind=0x4 cpu=0x10000"
            " misc=0x0 type=buffer bytes=8192\n";
    return text + stream;
  }
};

// A = 0x1000 (512 B), B = 0x2000 (16 B), C = 0x3000 (8 KB) dynamic CBs; V = 0x4000 a dynamic VB; S = 0x5000 bound only;
// T = 0x6000 a staging buffer; state 0x7000 the device's, 0x8000 a private one.
std::string WriteV3(const std::filesystem::path& folder, bool drop_a_map) {
  V3Writer writer;
  writer.Call("seed_cb", "stage=vs slots=0:0x5000,1:0x0");
  writer.Call("seed_cb", "stage=ps slots=0:0x0,1:0x0,2:0x3000@16+16");
  writer.Call("seed_vb", "slots=0:0x0/0/0,1:0x4000/12/0");
  writer.Map("0x1000", "discard");
  writer.Unmap("0x1000");
  writer.Call("set", "stage=ps slots=0:0x1000");
  writer.Call("set", "stage=vs slots=1:0x1000");
  writer.Map("0x2000", "discard");
  writer.Unmap("0x2000");
  writer.Call("set", "stage=ps slots=1:0x2000");
  writer.Map("0x3000", "discard");
  writer.Unmap("0x3000");
  writer.Call("set1", "stage=ps slots=3:0x3000@32+16");
  writer.Draw();  // d0
  writer.Map("0x3000", "discard");
  writer.Unmap("0x3000");
  writer.Draw();  // d1: both C slots see the new bytes
  writer.Call("set1", "stage=ps slots=3:0x3000@0+16");
  writer.Draw();  // d2: slot 3 at another offset of C
  writer.Map("0x4000", "no_overwrite");
  writer.Unmap("0x4000");
  writer.Map("0x1000", "no_overwrite");
  writer.Unmap("0x1000");
  writer.Draw();  // d3: a NO_OVERWRITE map writes nothing: A's bytes from its DISCARD stay
  writer.Map("0x6000", "read");
  writer.Unmap("0x6000");
  writer.Call("swap", "state=0x8000 previous=0x7000");
  writer.Call("set", "stage=ps slots=0:0x2000");
  writer.Draw();  // d4: the private state (only PS b0)
  writer.Call("swap", "state=0x7000 previous=0x8000");
  writer.Draw();  // d5: the device's state again
  writer.Call("clear", "");
  writer.Call("set", "stage=ps slots=0:0x1000");
  writer.Draw();  // d6
  writer.Call("set1", "stage=cs slots=0:0x1000,1:0x3000@16+16");
  writer.Call("draw", "kind=dispatch index=7 vs=00000000 ps=00000000 args=1,1,1,0,0 indirect=0x0 state=0x0 cbs=- vbs=-");
  writer.frame = 1;
  writer.line = 0;
  writer.Map("0x1000", "discard");
  writer.Unmap("0x1000");
  writer.Draw();
  if (!drop_a_map) writer.Map("0x3000", "discard");
  writer.Unmap("0x3000");
  writer.Draw();
  writer.Map("0x1000", "discard", "0x100000", "0x887a000a");  // a DO_NOT_WAIT poll that failed
  writer.Map("0x1000", "discard", "0x100000");
  writer.Unmap("0x1000");
  writer.Unmap("0x9000");  // mapped before the trace
  writer.Draw();
  std::string text = writer.Text(2);
  if (drop_a_map) {
    // The header still counts the removed line.
    const std::string counted = std::format("native_calls={}", writer.native_calls);
    text.replace(text.find(counted), counted.size(), std::format("native_calls={}", writer.native_calls + 1));
  }
  WriteText(folder / "tw_map_trace_v3.txt", text);
  // B mapped only below ReShade's proxy (as ReShade's push buffer), V and T through it; A and C made through it (their
  // v3 buffer lines); S bound only.
  WriteText(folder / "tw_map_trace.txt",
            "native_map frame=0 res=0x2000 usage=dynamic bind=0x4 cpu=0x10000 misc=0x0 type=buffer bytes=16\n"
            "map frame=0 res=0x4000 usage=dynamic bind=0x1 cpu=0x10000 misc=0x0 type=buffer bytes=4096\n"
            "map frame=0 res=0x6000 usage=staging bind=0x0 cpu=0x20000 misc=0x0 type=buffer bytes=256\n");
  return text;
}

uint32_t CountKind(const replay::Stream& stream, replay::Kind kind, uint32_t frame = UINT32_MAX) {
  return static_cast<uint32_t>(std::ranges::count_if(stream.calls, [&](const replay::Call& call) {
    return call.kind == kind && (frame == UINT32_MAX || call.frame == frame);
  }));
}

uint32_t ResourceIndex(const replay::Stream& stream, uint64_t pointer) {
  for (uint32_t index = 0; index < stream.resources.size(); ++index) {
    if (stream.resources[index].trace_pointer == pointer) return index;
  }
  return replay::NO_RESOURCE;
}

// D3D11's constant buffer reads, on the CPU: each draw's expected strip (`windows`: honour Set1 windows; the control
// passes false).
std::vector<std::vector<uint32_t>> ModelStrips(const replay::Stream& stream, bool windows) {
  struct Bound {
    uint32_t resource = replay::NO_RESOURCE;
    uint32_t first = 0;
    uint32_t count = replay::WHOLE_BUFFER_CONSTANTS;
  };
  using StageSlots = std::array<std::array<Bound, replay::CB_SLOTS>, replay::STAGES>;
  std::vector<StageSlots> states(stream.states);
  uint32_t active = 0;
  std::vector<std::vector<uint32_t>> contents(stream.resources.size());
  for (size_t index = 0; index < contents.size(); ++index) contents[index].assign(stream.resources[index].width / 4, 0);
  const auto read = [&](const Bound& bound, uint32_t constant, uint32_t* out) {
    std::fill(out, out + 4, 0u);
    if (bound.resource == replay::NO_RESOURCE) return;
    if (windows && constant >= bound.count) return;
    const size_t word = (static_cast<size_t>(bound.first) + constant) * 4;
    const auto& bytes = contents[bound.resource];
    if (word + 4 > bytes.size()) return;
    std::copy_n(bytes.begin() + static_cast<ptrdiff_t>(word), 4, out);
  };
  std::vector<std::vector<uint32_t>> strips;
  std::vector<uint32_t> frame;
  std::vector<uint32_t> dispatches;
  uint32_t ordinal = 0;
  for (const auto& call : stream.calls) {
    auto& slots = states[active];
    switch (call.kind) {
      case replay::Kind::MAP: {
        auto& bytes = contents[call.resource];
        const uint32_t count = replay::FillWords(stream.resources[call.resource], call.map_type);
        const uint32_t current = ordinal++;
        for (uint32_t word = 0; word < count && word < bytes.size(); ++word) bytes[word] = replay::FillWord(current, word);
        break;
      }
      case replay::Kind::SET:
      case replay::Kind::SET1:
        for (uint32_t index = 0; index < call.count; ++index) {
          const auto& slot = stream.slots[call.first_slot + index];
          const bool window = (call.kind == replay::Kind::SET1 && slot.count_or_offset != 0);
          slots[call.stage][call.start_slot + index] = {.resource = slot.resource,
                                                        .first = (window ? slot.first_or_stride : 0),
                                                        .count = (window ? slot.count_or_offset : replay::WHOLE_BUFFER_CONSTANTS)};
        }
        break;
      case replay::Kind::SWAP:
        active = call.resource;
        break;
      case replay::Kind::CLEAR:
        slots = {};
        break;
      case replay::Kind::DRAW:
      case replay::Kind::DISPATCH: {
        const bool dispatch = (call.kind == replay::Kind::DISPATCH);
        std::vector<uint32_t> strip(replay::STRIP_PIXELS * 4, 0);
        std::array<uint32_t, 4> hash = {2166136261u, 2166136261u, 2166136261u, 2166136261u};
        for (uint32_t slot = 0; slot < replay::CB_SLOTS; ++slot) {
          for (uint32_t index = 0; index < 4; ++index) {
            std::array<uint32_t, 4> value;
            read(slots[replay::VERTEX_STAGE][slot], replay::READ_INDICES[index], value.data());
            for (uint32_t component = 0; component < 4; ++component) {
              hash[component] = (hash[component] ^ value[component]) * 16777619u;
            }
            const uint32_t pixel = 1 + (slot * 4) + index;
            read(slots[dispatch ? 5 : replay::PIXEL_STAGE][slot], replay::READ_INDICES[index], &strip[pixel * 4]);
          }
        }
        if (!dispatch) std::copy(hash.begin(), hash.end(), strip.begin());
        auto& target = (dispatch ? dispatches : frame);
        target.insert(target.end(), strip.begin(), strip.end());
        break;
      }
      case replay::Kind::PRESENT:
        frame.insert(frame.end(), dispatches.begin(), dispatches.end());
        strips.push_back(std::move(frame));
        frame = {};
        dispatches = {};
        break;
      default:
        break;
    }
  }
  return strips;
}

// The seam's test plugin: counts its callbacks.
struct PluginCounts {
  uint32_t attach = 0;
  std::vector<UINT> created;  // each offered buffer's bytes
  uint32_t destroyed = 0;
  uint32_t presents = 0;
  uint32_t on_presents = 0;  // with the row on
  uint32_t detach = 0;
};
PluginCounts plugin_counts;
const replay::RingPlugin COUNTING_PLUGIN = {
    .name = "counting",
    .attach = [](ID3D11Device*, ID3D11DeviceContext*) { return ++plugin_counts.attach != 0; },
    .buffer_created = [](ID3D11Buffer*, const D3D11_BUFFER_DESC& desc, const void*) { plugin_counts.created.push_back(desc.ByteWidth); },
    .buffer_destroyed = [](ID3D11Buffer*) { ++plugin_counts.destroyed; },
    .present =
        [](ID3D11DeviceContext*, bool on) {
          ++plugin_counts.presents;
          plugin_counts.on_presents += (on ? 1 : 0);
        },
    .detach = [] { ++plugin_counts.detach; },
    .stats = [] { return std::format("{} presents", plugin_counts.presents); },
};

}  // namespace

int main() {
  const auto root = std::filesystem::current_path() / "tw_map_replay_data";
  const auto tools = std::filesystem::path(std::getenv("USERPROFILE")) / ".claude/projects/D--Dev-renodx/tools/tw";
  try {
    std::printf("1. v3 reader\n");
    WriteV3(root / "v3", false);
    const auto stream = replay::LoadV3(root / "v3" / "tw_map_trace_v3.txt");
    for (const auto& note : stream.notes) std::printf("  note: %s\n", note.c_str());
    CHECK(stream.version == 3 && stream.frames == 2);
    CHECK(CountKind(stream, replay::Kind::MAP) == 10);  // 11 map lines, the failed poll left out
    CHECK(CountKind(stream, replay::Kind::UNMAP) == 10);  // 11 lines, the unmap of a map before the trace left out
    CHECK(CountKind(stream, replay::Kind::SET) == 5 && CountKind(stream, replay::Kind::SET1) == 5);
    CHECK(CountKind(stream, replay::Kind::VB) == 1 && CountKind(stream, replay::Kind::SWAP) == 2);
    CHECK(CountKind(stream, replay::Kind::CLEAR) == 1 && CountKind(stream, replay::Kind::DISPATCH) == 1);
    CHECK(CountKind(stream, replay::Kind::DRAW, 0) == 7 && CountKind(stream, replay::Kind::DRAW, 1) == 3);
    CHECK(CountKind(stream, replay::Kind::PRESENT) == 2 && stream.calls.back().kind == replay::Kind::PRESENT);
    CHECK(stream.states == 2);
    const uint32_t a = ResourceIndex(stream, 0x1000);
    const uint32_t b = ResourceIndex(stream, 0x2000);
    const uint32_t c = ResourceIndex(stream, 0x3000);
    const uint32_t s = ResourceIndex(stream, 0x5000);
    CHECK(a != replay::NO_RESOURCE && stream.resources[a].width == 512 && stream.resources[a].usage == D3D11_USAGE_DYNAMIC);
    CHECK(b != replay::NO_RESOURCE && stream.resources[b].width == 16);
    CHECK(c != replay::NO_RESOURCE && stream.resources[c].width == 8192);
    CHECK(s != replay::NO_RESOURCE && stream.resources[s].bind == D3D11_BIND_CONSTANT_BUFFER
          && stream.resources[s].usage == D3D11_USAGE_DEFAULT && stream.resources[s].width == 65536);
    CHECK(ResourceIndex(stream, 0x9000) == replay::NO_RESOURCE);
    // H0: a v1 map line or a v3 buffer line (through ReShade's proxy) makes a buffer the game's; B (native_map lines
    // only) and S are not.
    const uint32_t v = ResourceIndex(stream, 0x4000);
    CHECK(stream.resources[a].game && stream.resources[c].game && v != replay::NO_RESOURCE && stream.resources[v].game);
    CHECK(!stream.resources[b].game && !stream.resources[s].game);
    std::printf("  without tw_map_trace.txt: every buffer the game's\n");
    {
      WriteText(root / "v3_alone" / "tw_map_trace_v3.txt", WriteV3(root / "v3_alone_source", false));
      const auto alone = replay::LoadV3(root / "v3_alone" / "tw_map_trace_v3.txt");
      CHECK(std::ranges::all_of(alone.resources, &replay::Resource::game));
      CHECK(std::ranges::any_of(alone.notes, [](const std::string& note) { return note.find("offered every buffer") != std::string::npos; }));
    }
    const auto window = std::ranges::find_if(stream.calls, [](const replay::Call& call) {
      return call.kind == replay::Kind::SET1 && call.stage == replay::PIXEL_STAGE && call.start_slot == 3;
    });
    CHECK(window != stream.calls.end() && stream.slots[window->first_slot].resource == c
          && stream.slots[window->first_slot].first_or_stride == 32 && stream.slots[window->first_slot].count_or_offset == 16);
    // Frame 0's first draw is its 14th line (140 us) after 13 native calls (26 us).
    const auto first_draw = std::ranges::find(stream.calls, replay::Kind::DRAW, &replay::Call::kind);
    CHECK(first_draw != stream.calls.end() && std::abs(first_draw->pace_us - 114.) < 0.01);
    std::printf("  control: a removed map line\n");
    WriteV3(root / "v3_dropped", true);
    CHECK(Throws([&] { replay::LoadV3(root / "v3_dropped" / "tw_map_trace_v3.txt"); }));
    std::printf("  control: a bad slot\n");
    {
      std::string text = WriteV3(root / "v3_bad", false);
      text.replace(text.find("3:0x3000@32+16"), 14, "3:0x3000@32");
      WriteText(root / "v3_bad" / "tw_map_trace_v3.txt", text);
      CHECK(Throws([&] { replay::LoadV3(root / "v3_bad" / "tw_map_trace_v3.txt"); }));
    }
    if (const auto s1 = tools / "testbin/tw_map_trace_amd64c/tw_map_trace_v3.txt"; std::filesystem::exists(s1)) {
      const auto s1_stream = replay::LoadV3(s1);
      std::printf("  S1's v3 output: %zu calls, %zu resources\n", s1_stream.calls.size(), s1_stream.resources.size());
      CHECK(CountKind(s1_stream, replay::Kind::MAP) == 2 && CountKind(s1_stream, replay::Kind::UNMAP) == 2);
      CHECK(CountKind(s1_stream, replay::Kind::SET) + CountKind(s1_stream, replay::Kind::SET1)
                + CountKind(s1_stream, replay::Kind::VB) + CountKind(s1_stream, replay::Kind::SWAP)
                + CountKind(s1_stream, replay::Kind::CLEAR)
            == 12);
      CHECK(CountKind(s1_stream, replay::Kind::DRAW) == 6 && CountKind(s1_stream, replay::Kind::DISPATCH) == 1);
    } else {
      std::printf("  S1's v3 output not found: skipped\n");
    }

    std::printf("2. v2 reader\n");
    {
      const std::string describe = "sub=0 usage=dynamic cpu=0x10000 misc=0x0 type=buffer";
      WriteText(root / "v2" / "tw_map_trace.txt",
                "trace setting=0,2 qpc_frequency=10000000 frames=2 end_qpc=0 draws=0 events=0\n"
                "frame index=0 present_qpc=0 ms=10.000 draws=5 dispatches=1 maps=3 unmaps=3 updates=0 copies=0\n"
                "frame index=1 present_qpc=0 ms=10.000 draws=4 dispatches=0 maps=3 unmaps=3 updates=0 copies=0\n"
                "map frame=0 qpc=0 ms=1.000 gap_ms=- gap_from=- thread=1 draw=0 last=- access=discard unmap_ms=1.010 res=0x100 bind=0x4 bytes=57344 " + describe + "\n"
                "map frame=0 qpc=0 ms=2.000 gap_ms=0.990 gap_from=unmap thread=1 draw=3 last=dispatch/cs:0 access=discard unmap_ms=2.001 res=0x200 bind=0x4 bytes=16 " + describe + "\n"
                "map frame=0 qpc=0 ms=5.000 gap_ms=3.000 gap_from=unmap thread=1 draw=4 last=draw/vs:0/ps:0 access=no_overwrite unmap_ms=5.002 res=0x300 bind=0x1 bytes=4096 " + describe + "\n"
                "map frame=1 qpc=0 ms=1.000 gap_ms=- gap_from=- thread=1 draw=0 last=- access=discard unmap_ms=1.010 res=0x100 bind=0x4 bytes=57344 " + describe + "\n"
                "map frame=1 qpc=0 ms=3.000 gap_ms=1.990 gap_from=unmap thread=1 draw=2 last=draw/vs:0/ps:0 access=discard unmap_ms=3.001 res=0x200 bind=0x4 bytes=16 " + describe + "\n"
                "map frame=1 qpc=0 ms=4.000 gap_ms=0.999 gap_from=unmap thread=1 draw=3 last=draw/vs:0/ps:0 access=discard unmap_ms=4.001 res=0x200 bind=0x4 bytes=16 " + describe + "\n");
      WriteText(root / "v2" / "waits.txt", "  frame 0 +   1.500 ms     0.400 ms  thread 1  x  -> IN map\n");
      const auto v2 = replay::LoadV2(root / "v2" / "tw_map_trace.txt", root / "v2" / "waits.txt", 0.5);
      for (const auto& note : v2.notes) std::printf("  note: %s\n", note.c_str());
      CHECK(CountKind(v2, replay::Kind::DRAW, 0) + CountKind(v2, replay::Kind::DISPATCH, 0) == 6);
      CHECK(CountKind(v2, replay::Kind::DRAW, 1) == 4 && CountKind(v2, replay::Kind::MAP) == 6);
      const uint32_t big = ResourceIndex(v2, 0x100);
      const uint32_t per_draw = ResourceIndex(v2, 0x200);
      // The call order of frame 0: the dedicated binds, map, unmap, draw, draw, dispatch, map 0x200, ...
      std::vector<replay::Kind> kinds;
      for (const auto& call : v2.calls) {
        if (call.frame == 0) kinds.push_back(call.kind);
      }
      using K = replay::Kind;
      const std::vector<K> expected = {K::SET, K::SET, K::MAP, K::UNMAP, K::DRAW, K::DRAW, K::DISPATCH, K::MAP, K::UNMAP,
                                       K::SET, K::SET, K::DRAW, K::MAP, K::UNMAP, K::VB, K::DRAW, K::DRAW, K::PRESENT};
      CHECK(kinds == expected);
      CHECK(v2.slots[v2.calls[0].first_slot].resource == big && v2.calls[0].start_slot == 0);
      CHECK(v2.slots[v2.calls[9].first_slot].resource == per_draw && v2.calls[9].start_slot == 7);
      CHECK(std::abs(v2.calls[7].pace_us - 1600.) < 0.01);  // 2.0 ms minus the 0.4 ms wait
      const auto capped = replay::LoadV2(root / "v2" / "tw_map_trace.txt", {}, 0.5);
      CHECK(std::abs(capped.calls[12].pace_us - 2010.) < 0.01);  // 5.0 ms minus (0.49 + 2.5) ms over the cap
    }
    if (const auto w3m = tools / "bench/runs/b1_W3_wpr_cpu_095607/1_W3m/tw_map_trace.txt"; std::filesystem::exists(w3m)) {
      const auto v2 = replay::LoadV2(w3m, {}, 0.5);
      std::printf("  W3m v2: %zu calls, %zu resources\n", v2.calls.size(), v2.resources.size());
      CHECK(CountKind(v2, replay::Kind::DRAW, 1) + CountKind(v2, replay::Kind::DISPATCH, 1) == 4181 + 43);
      CHECK(CountKind(v2, replay::Kind::MAP, 1) == 1774);
    } else {
      std::printf("  W3m's v2 trace not found: skipped\n");
    }

    std::printf("3. replay on WARP against the CPU model\n");
    replay::Options options = {.pace = false, .parity = true, .target_size = 256};
    const auto off = replay::Replay(stream, options);
    std::printf("  adapter: %s\n", off.adapter.c_str());
    const auto model = ModelStrips(stream, true);
    CHECK(off.strips.size() == 2 && model.size() == 2);
    std::string first_difference;
    for (size_t frame = 0; frame < std::min(off.strips.size(), model.size()); ++frame) {
      CHECK(off.strips[frame].size() == model[frame].size());
      for (size_t word = 0; word < std::min(off.strips[frame].size(), model[frame].size()); ++word) {
        if (off.strips[frame][word] == model[frame][word] || !first_difference.empty()) continue;
        first_difference = std::format("frame {} row {} pixel {} word {}: 0x{:08x}, model 0x{:08x}", frame,
                                       word / (replay::STRIP_PIXELS * 4), (word / 4) % replay::STRIP_PIXELS, word % 4,
                                       off.strips[frame][word], model[frame][word]);
      }
    }
    if (!first_difference.empty()) std::printf("  first difference: %s\n", first_difference.c_str());
    CHECK(first_difference.empty());
    // The cases are really in it: C's window ends at constant 16 (PS b2 reads 0 there), B's 16 B end at constant 1.
    CHECK(!model[0].empty() && model[0][(1 + (2 * 4) + 0) * 4] != 0 && model[0][(1 + (2 * 4) + 2) * 4] == 0);
    CHECK(model[0][(1 + (1 * 4) + 0) * 4] != 0 && model[0][(1 + (1 * 4) + 1) * 4] == 0);
    std::printf("  control: a model without windows\n");
    CHECK(ModelStrips(stream, false) != off.strips);

    std::printf("4. parity\n");
    options.plugin = &COUNTING_PLUGIN;
    const auto on = replay::Replay(stream, options);
    CHECK(replay::CompareStrips(off, on).empty());
    CHECK(plugin_counts.attach == 1 && plugin_counts.detach == 1 && plugin_counts.presents == 2);
    // The game's 4 buffers alone: never B (16 B, ReShade's) nor S (bound only, 64 KB assumed).
    CHECK(plugin_counts.created.size() == 4 && plugin_counts.destroyed == 4 && on.plugin_stats == "2 presents");
    CHECK(std::ranges::count(plugin_counts.created, 16u) == 0 && std::ranges::count(plugin_counts.created, 65536u) == 0);
    std::printf("  control: --addon-buffers ring offers every buffer\n");
    options.addon_buffers = true;
    CHECK(replay::CompareStrips(off, replay::Replay(stream, options)).empty());
    CHECK(plugin_counts.created.size() == 10 && plugin_counts.destroyed == 10 && std::ranges::count(plugin_counts.created, 16u) == 1);
    options.addon_buffers = false;
    for (const auto control : {replay::Control::UPLOAD, replay::Control::WINDOW, replay::Control::OFFSET}) {
      options.plugin = &replay::NATIVE_PLUGIN;
      options.control = control;
      const std::string difference = replay::CompareStrips(off, replay::Replay(stream, options));
      std::printf("  control %d: %s\n", static_cast<int>(control), difference.c_str());
      CHECK(!difference.empty());
    }

    std::printf("5. wait locator\n");
    options = {.pace = false, .target_size = 256, .test_delay_where = Where::CB_DISCARD, .test_delay_ms = 2.};
    const auto delayed = replay::Replay(stream, options);
    const auto summary = replay::Summarize(delayed, 0);
    std::printf("%s", replay::Report(stream, delayed, summary, options.wait_ms).c_str());
    const auto row = [](Where where) { return static_cast<size_t>(where); };
    CHECK(summary.ms[row(Where::CB_DISCARD)] >= 6.9 && summary.waits[row(Where::CB_DISCARD)] == 3.5);
    CHECK(summary.ms[row(Where::CB_DISCARD)] >= 5.);  // the mark
    CHECK(summary.calls[row(Where::CB_OTHER_MAP)] == 0.5 && summary.calls[row(Where::VB_OTHER_MAP)] == 0.5);
    CHECK(summary.calls[row(Where::OTHER_MAP)] == 0.5 && summary.calls[row(Where::UNMAP)] == 5.);
    CHECK(summary.calls[row(Where::BIND)] == 5.5 && summary.calls[row(Where::SWAP_CLEAR)] == 1.5);
    CHECK(summary.calls[row(Where::DRAW)] == 5. && summary.calls[row(Where::DISPATCH)] == 0.5);
    CHECK(summary.calls[row(Where::PRESENT)] == 1. && delayed.longest.front().where == Where::CB_DISCARD);
    std::printf("  control: the delay in vertex buffer maps\n");
    options.test_delay_where = Where::VB_OTHER_MAP;
    const auto moved = replay::Summarize(replay::Replay(stream, options), 0);
    CHECK(moved.ms[row(Where::CB_DISCARD)] < 1. && moved.ms[row(Where::VB_OTHER_MAP)] >= 0.99);

    std::printf("6. trap test\n");
    // The windows' clock in frames of 1 s: 3 loops of the 2 frames, the park in frames 2-4 (frame 0, 1, 0).
    options = {.loops = 3, .pace = false, .parity = true, .target_size = 256, .seconds_per_frame = 1.};
    const auto native = replay::Replay(stream, options);
    options.park = replay::Park::CB;
    options.park_start_s = 2.;
    options.park_length_s = 3.;
    const auto parked = replay::Replay(stream, options);
    const std::string parked_difference = replay::CompareStrips(native, parked);
    std::printf("  park cb,2,3: %u victims, %u maps to their stand-ins: %s\n", parked.victims, parked.parked_maps,
                (parked_difference.empty() ? "EQUAL bit for bit" : parked_difference.c_str()));
    // A and C (B is ReShade's, V a VB); their maps in frame 0 (4) and frame 1 (3).
    CHECK(parked_difference.empty() && parked.victims == 2 && parked.parked_maps == 4 + 3 + 4);
    std::printf("  control: the park's end leaves the binds on the stand-ins\n");
    options.control = replay::Control::PARK;
    const std::string kept = replay::CompareStrips(native, replay::Replay(stream, options));
    std::printf("  %s\n", kept.c_str());
    CHECK(!kept.empty());
    options.control = replay::Control::NONE;
    std::printf("  keep-warm every frame with a bind, the plugin's row on in frames 1-2\n");
    options.parity = false;
    options.keep_warm = 1;
    options.keep_warm_bind = true;
    options.plugin = &COUNTING_PLUGIN;
    options.plugin_start_s = 1.;
    options.plugin_length_s = 2.;
    plugin_counts = {};
    const auto warm = replay::Replay(stream, options);
    CHECK(warm.warm_maps == 2 * 3 && warm.failed_maps == 0);
    CHECK(plugin_counts.presents == 6 && plugin_counts.on_presents == 2);
    CHECK(warm.frames.size() == 6 && warm.frames[5].start_ticks == 5 * warm.frequency);
    std::printf("  control: --keep-warm-bind in parity mode\n");
    options.parity = true;
    CHECK(Throws([&] { replay::Replay(stream, options); }));
    std::printf("  the phases: waits from 15 s on (the window 10 s + 5 s)\n");
    replay::Result phased = {.frequency = 1000};  // a tick a ms
    for (int64_t second = 0; second < 40; ++second) {
      phased.frames.push_back({.start_ticks = second * 1000, .frame_ticks = 100});
      phased.frames.back().wait_ticks[row(Where::CB_DISCARD)] = (second >= 15 ? 3 : 0);
      phased.frames.back().waits[row(Where::CB_DISCARD)] = (second >= 15 ? 1 : 0);
    }
    const std::string trapped = replay::PhaseReport(phased, 10., 5.);
    std::printf("%s", trapped.c_str());
    CHECK(trapped.find("TRAP cb_discard: yes") != std::string::npos);
    CHECK(trapped.find("TRAP vb_discard: no") != std::string::npos);
    std::printf("  control: the late phase (25-35 s) recovers\n");
    for (auto& times : phased.frames) {
      if (times.start_ticks >= 25000) times.wait_ticks[row(Where::CB_DISCARD)] = 0;
    }
    CHECK(replay::PhaseReport(phased, 10., 5.).find("TRAP cb_discard: no") != std::string::npos);
  } catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    ++failures;
  }
  std::printf("%s (%d failures)\n", (failures == 0 ? "PASS" : "FAIL"), failures);
  return (failures == 0 ? 0 : 1);
}
