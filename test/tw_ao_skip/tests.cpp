// twattila/ao_skip.hpp's rule, rt_scene::AmbientReplaced (Attila's), for leaving the game's SSAO chain out in every Total
// War add-on (Warhammer, Warhammer II, Warhammer III; Troy and Pharaoh since ho_g03), over rt_scene's runtime as Update /
// Trace leave it (published as they do: PublishAmbientReplaced). ClearOpen (the clear) needs a command list: the add-ons'
// own tests (Warhammer part 15).
// 1. Ray-Traced AO on, traced this frame, the last, or within AMBIENT_REPLACED_FRAMES: left out (frames 0, 1, 5, 2^40).
// 2. Path-Traced Ambient alone, traced this frame: left out (ours replaces the game's, never stacks).
// 3. CONTROLS (the game's SSAO stays): traced more than AMBIENT_REPLACED_FRAMES ago; both rows off; nothing traced yet
//    (traced_frame UINT64_MAX) at frame 0 and later.
// 4. The CONTROLS that must differ: the rule the Warhammer / Warhammer III copies had (Ray-Traced AO, traced_frame ==
//    frame || traced_frame + 1 == frame) skips at frame 0 with nothing traced (UINT64_MAX + 1 wraps to 0) and keeps the
//    game's SSAO under Path-Traced Ambient alone (stacked on the paths); the shared rule does neither.
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
  const auto set = [&](bool ambient_occlusion, bool path_ambient, uint64_t traced_frame, uint64_t frame) {
    const std::unique_lock lock(runtime.mutex);
    runtime.settings.ambient_occlusion = ambient_occlusion;
    runtime.settings.path_ambient = path_ambient;
    runtime.traced_frame = traced_frame;
    runtime.frame = frame;
    bridge.ray_tracing = true;
    rt::internal::PublishAmbientReplaced();
  };
  const auto old_rule = [&] {
    const std::unique_lock lock(runtime.mutex);
    return runtime.settings.ambient_occlusion && bridge.ray_tracing
           && (runtime.traced_frame == runtime.frame || runtime.traced_frame + 1 == runtime.frame);
  };
  constexpr uint64_t WINDOW = rt::internal::AMBIENT_REPLACED_FRAMES;

  // 1.
  for (const uint64_t frame : {uint64_t{0}, uint64_t{1}, uint64_t{5}, uint64_t{1} << 40}) {
    set(true, false, frame, frame);
    CHECK(rt::AmbientReplaced());
    if (frame >= WINDOW) {
      set(true, false, frame - WINDOW, frame);
      CHECK(rt::AmbientReplaced());
    }
  }
  std::printf("1 Ray-Traced AO, traced within %llu frames: left out: ok\n", static_cast<unsigned long long>(WINDOW));

  // 2.
  set(false, true, 9, 9);
  CHECK(rt::AmbientReplaced());
  std::printf("2 Path-Traced Ambient alone: left out: ok\n");

  // 3.
  set(true, false, 3, 3 + WINDOW + 1);
  CHECK(!rt::AmbientReplaced());
  set(false, false, 5, 5);
  CHECK(!rt::AmbientReplaced());
  for (const uint64_t frame : {uint64_t{0}, uint64_t{1}, uint64_t{7}}) {
    set(true, true, UINT64_MAX, frame);
    CHECK(!rt::AmbientReplaced());
  }
  std::printf("3 CONTROLS (an older trace, both rows off, nothing traced): the game's: ok\n");

  // 4.
  set(true, false, UINT64_MAX, 0);
  CHECK(old_rule() && !rt::AmbientReplaced());
  set(false, true, 9, 9);
  CHECK(!old_rule() && rt::AmbientReplaced());
  std::printf("4 CONTROLS: the old copies' rule skips at frame 0 with nothing traced and stacks under the paths; the shared one neither: ok\n");
  std::printf("AO SKIP ALL PASSED\n");
  return 0;
}
