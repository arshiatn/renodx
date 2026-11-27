// twattila/ao_skip.hpp GameSsaoOut, the one rule for leaving the game's SSAO chain out (Warhammer, Warhammer II,
// Warhammer III, Troy, Pharaoh), over rt_scene's runtime and the bridge as Update / Trace / TraceLightBounce leave them.
// ClearOpen (the clear) needs a command list: the add-ons' own tests (Warhammer part 15).
// 1. Ray-Traced AO, traced this frame or the last, the bridge up: left out (frames 0, 1, 5, 2^40).
// 2. Path-Traced Ambient taking the game's ambient out (path_ambient_on), the paths traced this frame or the last: left
//    out, the rays' trace old or none (ours replaces the game's, never stacks).
// 3. CONTROLS (the game's SSAO stays):
//    - Ray-Traced AO traced two frames ago (a dropout: the combine has no trace to give), or the bridge gone (its
//      combine has no texture);
//    - Path-Traced Ambient's warm-up (the row on, path_ambient_on not yet: PATH_AMBIENT_FRAMES path frames) with a fresh
//      trace and bounce; its paths two frames old (a dropout) with the rays' trace fresh; the rays tracing without the
//      Light Bounce (bounced_frame UINT64_MAX);
//    - both rows off; nothing traced yet (traced_frame UINT64_MAX) at frames 0, 1 and 7.
// 4. The CONTROLS that must differ: the Warhammer copies' rule (Ray-Traced AO, traced_frame == frame || traced_frame + 1
//    == frame) skips at frame 0 with nothing traced (UINT64_MAX + 1 wraps to 0); rt_scene::AmbientReplaced (30 frames of
//    the rays' trace) leaves it out over a Ray-Traced AO dropout (its frames 2-30: HANDOFF's OnAmbientOcclusionDraw item).
// 5. Attila's, Rome II's and Thrones' rule (rt_scene::AmbientReplaced: their AO pass writes white) under Path-Traced
//    Ambient alone is GameSsaoOut's: the game's AO through the warm-up (a fresh trace and bounce, path_ambient_on not yet,
//    at frame 9 and 9 + 29), a path dropout and the rays without the Light Bounce; out once the paths take the game's
//    ambient out. CONTROL (the rule before, the row and 30 frames of the rays' trace): out through every warm-up, the sun
//    then had no AO at all for PATH_AMBIENT_FRAMES. The row turned off with path_ambient_on still the last bounce's: the
//    game's (CONTROL: path_ambient_on alone left it out that frame). Both terms: the rays' AO with the paths on, and with
//    the paths' warm-up, out in both rules.
// No GPU work. tests.exe
#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "src/games/twattila/ao_skip.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "AO skip test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

int main() {
  namespace rt = twattila::rt_scene;
  auto& runtime = rt::internal::runtime;
  auto& bridge = twattila::dlaa_bridge::bridge;
  struct State {
    bool ambient_occlusion = false;
    bool path_ambient = false;
    bool path_ambient_on = false;
    bool ray_tracing = true;
    uint64_t traced_frame = UINT64_MAX;
    uint64_t bounced_frame = UINT64_MAX;
    uint64_t frame = 0;
  };
  const auto set = [&](const State& state) {
    const std::unique_lock lock(runtime.mutex);
    runtime.settings.ambient_occlusion = state.ambient_occlusion;
    runtime.settings.path_ambient = state.path_ambient;
    runtime.path_ambient_on = state.path_ambient_on;
    runtime.traced_frame = state.traced_frame;
    runtime.bounced_frame = state.bounced_frame;
    runtime.frame = state.frame;
    bridge.ray_tracing = state.ray_tracing;
    rt::internal::PublishAmbientReplaced();
  };
  const auto out = twattila::ao_skip::GameSsaoOut;
  const auto old_rule = [&] {
    const std::unique_lock lock(runtime.mutex);
    return runtime.settings.ambient_occlusion && bridge.ray_tracing
           && (runtime.traced_frame == runtime.frame || runtime.traced_frame + 1 == runtime.frame);
  };
  constexpr uint64_t WINDOW = rt::internal::AMBIENT_REPLACED_FRAMES;

  // 1.
  for (const uint64_t frame : {uint64_t{0}, uint64_t{1}, uint64_t{5}, uint64_t{1} << 40}) {
    set({.ambient_occlusion = true, .traced_frame = frame, .frame = frame});
    CHECK(out());
    if (frame > 0) {
      set({.ambient_occlusion = true, .traced_frame = frame - 1, .frame = frame});
      CHECK(out());
    }
  }
  std::printf("1 Ray-Traced AO, traced this frame or the last, the bridge up: left out: ok\n");

  // 2.
  for (const uint64_t traced : {uint64_t{8}, uint64_t{9}, uint64_t{2}, UINT64_MAX}) {
    set({.path_ambient = true, .path_ambient_on = true, .traced_frame = traced, .bounced_frame = 8, .frame = 9});
    CHECK(out());
  }
  set({.path_ambient = true, .path_ambient_on = true, .traced_frame = 9, .bounced_frame = 9, .frame = 9});
  CHECK(out());
  std::printf("2 Path-Traced Ambient on, the paths traced this frame or the last: left out: ok\n");

  // 3.
  set({.ambient_occlusion = true, .traced_frame = 3, .frame = 5});
  CHECK(!out());
  set({.ambient_occlusion = true, .ray_tracing = false, .traced_frame = 4, .frame = 5});
  CHECK(!out());
  set({.path_ambient = true, .traced_frame = 9, .bounced_frame = 8, .frame = 9});
  CHECK(!out());
  set({.path_ambient = true, .path_ambient_on = true, .traced_frame = 9, .bounced_frame = 7, .frame = 9});
  CHECK(!out());
  set({.path_ambient = true, .path_ambient_on = true, .traced_frame = 9, .frame = 9});
  CHECK(!out());
  set({.traced_frame = 5, .bounced_frame = 5, .frame = 5});
  CHECK(!out());
  for (const uint64_t frame : {uint64_t{0}, uint64_t{1}, uint64_t{7}}) {
    set({.ambient_occlusion = true, .path_ambient = true, .frame = frame});
    CHECK(!out());
  }
  std::printf("3 CONTROLS (a Ray-Traced AO dropout, the bridge gone, the paths' warm-up, a path dropout, the rays without the "
              "Light Bounce, both rows off, nothing traced): the game's: ok\n");

  // 4.
  set({.ambient_occlusion = true, .frame = 0});
  CHECK(old_rule() && !out());
  set({.ambient_occlusion = true, .traced_frame = 3, .frame = 5});
  CHECK(rt::AmbientReplaced() && !out());
  std::printf("4 CONTROLS: the old copies' rule (frame 0) and AmbientReplaced (a Ray-Traced AO dropout) differ: ok\n");

  // 5.
  const auto row_rule = [&] {
    const std::unique_lock lock(runtime.mutex);
    return runtime.settings.path_ambient && runtime.traced_frame != UINT64_MAX && runtime.traced_frame + WINDOW >= runtime.frame;
  };
  struct PathsCase {
    State state;
    bool left_out = false;
  };
  const PathsCase paths_cases[] = {
      {.state = {.path_ambient = true, .traced_frame = 9, .bounced_frame = 8, .frame = 9}},
      {.state = {.path_ambient = true, .traced_frame = 9, .bounced_frame = 9, .frame = 9}},
      {.state = {.path_ambient = true, .traced_frame = 9 + WINDOW - 1, .bounced_frame = 9 + WINDOW - 2, .frame = 9 + WINDOW - 1}},
      {.state = {.path_ambient = true, .path_ambient_on = true, .traced_frame = 9, .bounced_frame = 7, .frame = 9}},
      {.state = {.path_ambient = true, .path_ambient_on = true, .traced_frame = 9, .frame = 9}},
      {.state = {.path_ambient = true, .path_ambient_on = true, .traced_frame = 9, .bounced_frame = 8, .frame = 9}, .left_out = true},
      {.state = {.path_ambient = true, .path_ambient_on = true, .traced_frame = 9, .bounced_frame = 9, .frame = 9}, .left_out = true},
  };
  for (const PathsCase& paths_case : paths_cases) {
    set(paths_case.state);
    CHECK(rt::AmbientReplaced() == paths_case.left_out && out() == paths_case.left_out && row_rule());
  }
  const auto stale_rule = [&] {
    const std::unique_lock lock(runtime.mutex);
    return runtime.path_ambient_on && runtime.bounced_frame != UINT64_MAX && runtime.bounced_frame + 1 >= runtime.frame;
  };
  set({.path_ambient_on = true, .traced_frame = 9, .bounced_frame = 8, .frame = 9});
  CHECK(!rt::AmbientReplaced() && !out() && stale_rule());
  set({.ambient_occlusion = true, .path_ambient = true, .path_ambient_on = true, .traced_frame = 9, .bounced_frame = 9, .frame = 9});
  CHECK(rt::AmbientReplaced() && out());
  set({.ambient_occlusion = true, .path_ambient = true, .traced_frame = 9, .bounced_frame = 8, .frame = 9});
  CHECK(rt::AmbientReplaced() && out());
  std::printf("5 AmbientReplaced under Path-Traced Ambient = GameSsaoOut: the game's AO through the warm-up, a path dropout, "
              "no Light Bounce; out with the paths' ambient; CONTROL the rule before: out in all of them: ok\n");
  std::printf("AO SKIP ALL PASSED\n");
  return 0;
}
