// Attila's soldier shadows under the rays (twattila/graphics_hooks.hpp Scales::unit_shadows, rt_scene.hpp CastersSkipped,
// the addon's Update: `unit_shadows = CastersSkipped() ? 0 : 1`), through the addon's own UnitDistances hook over a stand-in
// for the game's function (its 18 distances at the highest Unit Detail, ATN's v154 game: 16 = 10000).
// 1. Each state of Ray-Traced Shadows Only, as the present after rt_scene::Update finds it: index 16 is 0 only while the sun's
//    shadow map gets no casters next frame (shadows only on, the rays' occlusion made this frame, the map known, the skip
//    installed); else the game's 10000. Every other index the game's in every state.
// 2. The next frame's skip agrees with it (OnBindTargets with the map bound alone: skipped exactly when CastersSkipped was
//    true at the present before; CONTROL: another depth target bound alone is never skipped).
// 3. Back: a frame without the rays' occlusion, or shadows only off, gives 16 the game's value at the next call (the hook
//    writes over the game's output every call; nothing kept).
// 4. CONTROL: Unit Detail 2x on foot soldiers doubles index 0 alone (the hook runs, it is no pass-through).
// 5. Small Soldiers as Sprites (Scales::sprite_pixels, anti_aliasing::MainFocalPixels) through the ImposterDistance hook over
//    a stand-in for the game's (the size's distance + (random - 0.5) x its fade width, from the 18 the display holds):
//    off, or no camera yet: the game's bits; on: no farther than where a soldier of its size is N pixels tall (h x focal /
//    d = N at random 0.5), the game's spread kept, nearer game distances untouched, sizes past 3 as 0; printed: where
//    each size switches at 4K with a 40 degree vertical field of view.
// 6. Two Shadow Cascades Under Rays (graphics_hooks.hpp shadow_map) over the shadow setup's code as empire.retail.dll ships
//    it (read from disk: tests.exe [empire.retail.dll]; no relocation in these bytes), in a page of ours as the found code:
//    its pattern once; off, Set writes Shadow Map Size's bytes alone, as before the row (2x: the six sizes; 1x: the game's
//    bytes); on, the four cascade immediates (Max's and High's +0x98 and +0x84) become Medium's own (read after the pattern),
//    nothing else; off again and 1x: the game's bytes; CONTROL: Medium's writes not as known, on writes nothing.
// 7. Shadow map work (Render's hook), Attila's and Rome II's, also with the 64-bit loader's Shadow refresh rest frames;
//    7b the 64-bit route (the engine plugin's hook calling attila64_plugin_link.hpp's callbacks); 7c the callbacks'
//    registration and drain on a block as the plugin publishes it.
// 8. Rome II's Two Shadow Cascades Under Rays (twrome2/shadow_cascades.hpp) over its shipped empire.retail.dll.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "src/games/twattila/attila64_plugin_link.hpp"
#include "src/games/twattila/graphics_hooks.hpp"
#include "src/games/twattila/rt_scene.hpp"
#include "src/games/twattila/shadow_pass.hpp"
#include "src/games/twrome2/shadow_cascades.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

namespace hooks = twattila::graphics_hooks;
namespace scene = twattila::rt_scene;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Unit shadows test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

// The game's distances at the highest Unit Detail (graphics_hooks.hpp's list).
constexpr std::array<float, 18> GAME = {90.f, 110.f, 200.f, 1000.f, 22.5f, 27.5f, 50.f, 250.f, 40.f,
                                        40.f, 60.f, 500.f, 100.f, 10.f, 10.f, 25.f, 10000.f, FLT_MAX};
constexpr size_t UNIT_SHADOWS = 16;

static float* __fastcall GameUnitDistances(void*, void*, float* out, uint32_t, uint32_t, uint32_t, uint32_t) {
  std::ranges::copy(GAME, out);
  return out;
}

// What the display copied from UnitDistances (+0x234), and the game's ImposterDistance over it (+0x48a8f0: past 3 the
// jump table's default takes size 0's).
static std::array<float, 18> display_distances = GAME;

static float __fastcall GameImposterDistance(void*, void*, uint32_t size, float random) {
  const uint32_t index = (size <= 3 ? size : 0);
  return (random - 0.5f) * display_distances[4 + index] + display_distances[index];
}

// A device whose views are their resources (the shadow map's depth view is the map).
struct ViewDevice : mock::Device {
  reshade::api::resource get_resource_from_view(reshade::api::resource_view view) const override { return {view.handle}; }
};

// Part 7's stand-ins: a pass (its vtable +0x3c gives its kind) and the model manager's Render, which notes each call and
// binds its targets as the passes do (kind 0 another target; kind 5 with `other_bind_in_cascade` another, then the map).
struct StandInPass {
  const void* const* vtable;
  uint32_t kind;
};

struct StandIn {
  std::vector<std::pair<uint32_t, uint32_t>> calls;  // (kind, the call's number in its kind this frame)
  std::array<uint32_t, 16> numbers = {};
  bool other_bind_in_cascade = false;
};
static StandIn stand_in;

static uint32_t __fastcall StandInKind(StandInPass* self, void*) {
  return self->kind;
}

static void __fastcall StandInRender(uint8_t*, void*, void*, uint8_t* pass_bytes) {
  const auto* pass = reinterpret_cast<const StandInPass*>(pass_bytes);
  // The call's number among its kind's this frame, skipped ones too (the hook's count, where it counts).
  const uint32_t counted = twattila::shadow_pass::internal::kinds[pass->kind].calls;
  stand_in.calls.emplace_back(pass->kind, (counted != 0) ? counted - 1 : stand_in.numbers[pass->kind]++);
  if (pass->kind == 0 || (pass->kind == 5 && stand_in.other_bind_in_cascade)) {
    twattila::rt_scene::internal::skipping_casters.store(false);
    twattila::shadow_pass::internal::OnBindTargets(nullptr, 0, nullptr, {});
    if (pass->kind == 5) {
      twattila::rt_scene::internal::skipping_casters.store(true);
      twattila::shadow_pass::internal::OnBindTargets(nullptr, 0, nullptr, {});
    }
  }
}

// Rome II's Render: the same stand-in without the context.
static void __fastcall StandInRenderRome2(uint8_t* self, void* unused_edx, uint8_t* pass_bytes) {
  StandInRender(self, unused_edx, nullptr, pass_bytes);
}

int main(int argc, char** argv) {
  hooks::internal::original_unit_distances = &GameUnitDistances;
  hooks::internal::hooks_active = true;
  bool installed = true;  // Update only takes the scales (no module to hook)
  renodx::utils::settings::Setting status;
  // One present of Attila's: the rays' Update first, then the hooks' scales from CastersSkipped, then the game's next call.
  const auto present = [&](bool shadows_only, bool occlusion_this_frame, const std::array<float, 4>& unit_detail) {
    auto& runtime = scene::internal::runtime;
    if (occlusion_this_frame) runtime.occlusion_frame = runtime.frame;
    scene::Settings settings = runtime.settings;
    settings.shadows_only = shadows_only;
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.settings = settings;
      ++runtime.frame;  // rt_core Update's
    }
    hooks::Update({.unit_detail = unit_detail, .unit_shadows = (scene::CastersSkipped() ? 0.f : 1.f)}, &installed, &status);
    std::array<float, 18> out = {};
    hooks::internal::OnUnitDistances(nullptr, nullptr, out.data(), 0, 0, 0, 0);
    return out;
  };
  const auto others_game = [](const std::array<float, 18>& out) {
    for (size_t index = 0; index < out.size(); ++index) {
      if (index != UNIT_SHADOWS && out[index] != GAME[index]) return false;
    }
    return true;
  };
  constexpr std::array<float, 4> GAME_DETAIL = {1.f, 1.f, 1.f, 1.f};

  ViewDevice device;
  mock::CommandList cmd_list;
  cmd_list.owner = &device;
  constexpr uint64_t MAP = 0x1000;
  constexpr uint64_t OTHER_DEPTH = 0x2000;
  const auto next_frame_skips = [&](uint64_t depth) {
    scene::OnBindTargets(&cmd_list, 0, nullptr, {depth});
    const bool skipped = scene::OnCasterDrawIndexed(&cmd_list, 0, 0, 0, 0, 0);
    scene::OnBindTargets(&cmd_list, 1, nullptr, {0});  // the frame's next pass: targets again
    return skipped;
  };

  // The skip not installed (a game without InstallShadowsOnly): the game's, whatever else holds.
  scene::internal::shadow_map = MAP;
  auto out = present(true, true, GAME_DETAIL);
  CHECK(!scene::CastersSkipped() && out[UNIT_SHADOWS] == 10000.f && others_game(out));
  scene::internal::shadows_only_installed = true;  // InstallShadowsOnly's flag (its ReShade events not needed here)

  // Shadows only off: the game's.
  out = present(false, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 10000.f && others_game(out));
  CHECK(!next_frame_skips(MAP));
  std::printf("shadows only off: 16 = %.0f, next frame skipped 0\n", out[UNIT_SHADOWS]);

  // On, the rays' occlusion made this frame, the map known: 0, and the next frame skips the map's casters.
  out = present(true, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 0.f && others_game(out));
  CHECK(next_frame_skips(MAP));
  CHECK(!next_frame_skips(OTHER_DEPTH));  // CONTROL
  std::printf("rays replace the map: 16 = %.0f, next frame skipped 1 (another depth target 0)\n", out[UNIT_SHADOWS]);

  // Held over frames while the rays keep tracing.
  for (int frame = 0; frame < 3; ++frame) {
    out = present(true, true, GAME_DETAIL);
    CHECK(out[UNIT_SHADOWS] == 0.f && others_game(out) && next_frame_skips(MAP));
  }

  // A frame whose sun draws made no rays' occlusion: the next frame draws the map's casters (OnBindTargets reads the frame
  // before's), and 16 is the game's at once; tracing again: 0 again.
  out = present(true, false, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 10000.f && others_game(out) && !next_frame_skips(MAP));
  std::printf("occlusion stale: 16 = %.0f, next frame skipped 0\n", out[UNIT_SHADOWS]);
  out = present(true, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 0.f && next_frame_skips(MAP));

  // The map not known (no sun draw learned it): the game's.
  scene::internal::shadow_map = 0;
  out = present(true, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 10000.f && others_game(out));
  scene::internal::shadow_map = MAP;

  // Rays on again, then off: 0, then the game's at the next call.
  out = present(true, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 0.f);
  out = present(false, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 10000.f && others_game(out) && !next_frame_skips(MAP));
  std::printf("rays off again: 16 = %.0f\n", out[UNIT_SHADOWS]);

  // CONTROL: Unit Detail 2x on foot soldiers: index 0 alone doubled.
  out = present(false, true, {2.f, 1.f, 1.f, 1.f});
  CHECK(out[0] == 180.f && out[UNIT_SHADOWS] == 10000.f);
  for (size_t index = 1; index < out.size(); ++index) CHECK(out[index] == GAME[index]);
  std::printf("CONTROL Unit Detail 2x: 0 = %.0f (game 90)\n", out[0]);

  // 5. Small Soldiers as Sprites.
  hooks::internal::original_imposter_distance = &GameImposterDistance;
  auto& aa = twattila::anti_aliasing::internal::runtime;
  CHECK(twattila::anti_aliasing::MainFocalPixels() == 0.f);  // no main camera yet
  constexpr float TAN_HALF_FOV = 0.36397023f;                // tan(20 degrees): 40 degrees vertical
  aa.display_height = 2160;
  aa.camera.projection[5] = 1.f / TAN_HALF_FOV;
  const float focal = twattila::anti_aliasing::MainFocalPixels();
  CHECK(std::abs(focal - 2160.f / (2.f * TAN_HALF_FOV)) < 0.01f);
  const auto settle = [&](const std::array<float, 4>& unit_detail, float pixels, float focal_pixels) {
    hooks::Update({.unit_detail = unit_detail, .sprite_pixels = pixels, .focal_pixels = focal_pixels}, &installed, &status);
    hooks::internal::OnUnitDistances(nullptr, nullptr, display_distances.data(), 0, 0, 0, 0);
  };
  constexpr std::array<float, 9> RANDOMS = {0.f, 0.01f, 0.25f, 0.4999f, 0.5f, 0.5001f, 0.75f, 0.99f, 1.f};
  constexpr std::array<uint32_t, 6> SIZES = {0, 1, 2, 3, 4, 7};
  const auto game = [](uint32_t size, float random) { return GameImposterDistance(nullptr, nullptr, size, random); };
  const auto hooked = [](uint32_t size, float random) { return hooks::internal::OnImposterDistance(nullptr, nullptr, size, random); };
  const auto bits = [](float value) { return std::bit_cast<uint32_t>(value); };
  // Off (0 px), and on without a camera (focal 0): the game's bits, at 1x and 8x.
  for (const float detail : {1.f, 8.f}) {
    for (const auto& [pixels, focal_pixels] : {std::pair{0.f, focal}, std::pair{20.f, 0.f}}) {
      settle({detail, detail, detail, detail}, pixels, focal_pixels);
      for (const uint32_t size : SIZES) {
        for (const float random : RANDOMS) CHECK(bits(hooked(size, random)) == bits(game(size, random)));
      }
    }
  }
  // On at 8x (the game's sprites at 720 / 880 / 1600 / 8000 m): each size's switch where it is 20 px tall, the spread kept.
  settle({8.f, 8.f, 8.f, 8.f}, 20.f, focal);
  std::array<float, 4> switch_at = {};
  for (const uint32_t size : SIZES) {
    const float height = hooks::internal::SOLDIER_HEIGHTS[size <= 3 ? size : 0];
    const float centre = hooked(size, 0.5f);
    CHECK(std::abs(height * focal / centre - 20.f) < 1e-4f * 20.f);  // N pixels tall at the switch
    CHECK(centre < game(size, 0.5f));                                 // CONTROL: the cap took it
    for (const float random : RANDOMS) {
      const float spread = game(size, random) - game(size, 0.5f);
      CHECK(std::abs((hooked(size, random) - centre) - spread) < 1e-3f);
      CHECK(hooked(size, random) <= game(size, random));
    }
    if (size <= 3) switch_at[size] = centre;
  }
  CHECK(bits(hooked(7, 0.3f)) == bits(hooked(0, 0.3f)));  // past 3: size 0's
  // Nearer than the cap (Unit Detail 0.5x: 45 m foot soldiers): the game's bits.
  settle({0.5f, 0.5f, 0.5f, 0.5f}, 20.f, focal);
  for (const float random : RANDOMS) CHECK(bits(hooked(0, random)) == bits(game(0, random)));
  // More pixels, nearer sprites.
  settle({8.f, 8.f, 8.f, 8.f}, 40.f, focal);
  CHECK(std::abs(hooked(0, 0.5f) - switch_at[0] * 0.5f) < 1e-3f * switch_at[0]);
  std::printf("small soldiers as sprites, 4K at 40 degrees (focal %.1f px), 20 px: foot soldiers %.0f m, cavalry %.0f m, "
              "elephants %.0f m, artillery %.0f m (8x: 720 / 880 / 1600 / 8000 m); off and without a camera the game's "
              "bits\n",
              focal, switch_at[0], switch_at[1], switch_at[2], switch_at[3]);
  settle({1.f, 1.f, 1.f, 1.f}, 0.f, 0.f);

  // 6.
  {
    namespace shadow = hooks::shadow_map::internal;
    std::ifstream stream(argc > 1 ? argv[1] : R"(C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll)",
                         std::ios::binary);
    CHECK(stream.good());
    const std::string file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const uint8_t* found = tw::engine_tweaks::FindPattern({file}, shadow::PATTERN);
    CHECK(found != nullptr);
    const auto after = static_cast<size_t>(found - reinterpret_cast<const uint8_t*>(file.data())) + 1;
    CHECK(tw::engine_tweaks::FindPattern({std::string_view(file).substr(after)}, shadow::PATTERN) == nullptr);
    constexpr size_t BYTES = shadow::CODE_SIZE + shadow::MEDIUM_CASCADES.size();
    const std::vector<uint8_t> game(found, found + BYTES);
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(page != nullptr);
    std::memcpy(page, game.data(), BYTES);
    shadow::code = page;
    const auto differing = [&](const std::vector<uint8_t>& expected) {
      std::vector<size_t> at;
      for (size_t index = 0; index < BYTES; ++index) {
        if (page[index] != expected[index]) at.push_back(index);
      }
      return at;
    };
    // Before the row: the six sizes x 2 over the game's bytes.
    std::vector<uint8_t> sizes_2x = game;
    for (size_t index = 0; index < shadow::SIZE_OFFSETS.size(); ++index) {
      const float size = shadow::GAME_SIZES[index] * 2.f;
      std::memcpy(sizes_2x.data() + shadow::SIZE_OFFSETS[index], &size, sizeof(size));
    }
    hooks::shadow_map::Set(1.f, false);
    CHECK(differing(game).empty());
    hooks::shadow_map::Set(2.f, false);
    CHECK(differing(sizes_2x).empty());
    // On: Medium's own immediates (its count, 2, and +0x84, 0) at Max's and High's places, nothing else.
    uint32_t medium_count;
    uint32_t medium_84;
    std::memcpy(&medium_count, game.data() + shadow::CODE_SIZE + 6, sizeof(medium_count));
    std::memcpy(&medium_84, game.data() + shadow::CODE_SIZE + 16, sizeof(medium_84));
    CHECK(medium_count == 2 && medium_84 == 0);
    hooks::shadow_map::Set(2.f, true);
    std::vector<uint8_t> two = sizes_2x;
    for (size_t index = 0; index < shadow::CASCADE_OFFSETS.size(); ++index) {
      const uint32_t value = (index % 2 == 0) ? medium_count : medium_84;
      std::memcpy(two.data() + shadow::CASCADE_OFFSETS[index], &value, sizeof(value));
    }
    CHECK(differing(two).empty());
    const auto changed = differing(sizes_2x);
    CHECK(!changed.empty());
    for (const size_t index : changed) {
      CHECK(std::ranges::any_of(shadow::CASCADE_OFFSETS, [&](size_t offset) { return index >= offset && index < offset + 4; }));
    }
    // Off again, then the game's size: the game's bytes.
    hooks::shadow_map::Set(2.f, false);
    CHECK(differing(sizes_2x).empty());
    hooks::shadow_map::Set(2.f, true);
    hooks::shadow_map::Uninstall();
    CHECK(differing(game).empty());
    // CONTROL: Medium's count not 2 after the pattern (another build or mod): on writes no cascade.
    page[shadow::CODE_SIZE + 6] = 3;
    std::vector<uint8_t> other_medium = game;
    other_medium[shadow::CODE_SIZE + 6] = 3;
    hooks::shadow_map::Set(1.f, true);
    CHECK(differing(other_medium).empty());
    hooks::shadow_map::Set(2.f, true);
    std::vector<uint8_t> other_medium_2x = sizes_2x;
    other_medium_2x[shadow::CODE_SIZE + 6] = 3;
    CHECK(differing(other_medium_2x).empty());
    shadow::code = nullptr;
    VirtualFree(page, 0, MEM_RELEASE);
    std::printf("6 shadow cascades: pattern once in the shipped empire.retail.dll; off: Shadow Map Size's bytes alone; on: %zu bytes "
                "differ, all in Max's and High's cascade immediates (Medium's %u / %u); back: the game's bytes; CONTROL: "
                "another Medium, none written\n", changed.size(), medium_count, medium_84);

    // 7. Skip the Game's Shadow Map Work (shadow_pass.hpp): its Render pattern once in the shipped DLL; then frames of a
    //    stand-in for the model manager's Render (4 cascade calls of kind 5 into the sun's map, 1 main call of kind 0 that
    //    binds its targets, 1 call of kind 9 into the sun's map alone, the frame's only one of its kind).
    namespace pass = twattila::shadow_pass::internal;
    const uint8_t* render = tw::engine_tweaks::FindPattern({file}, pass::RENDER);
    CHECK(render != nullptr);
    const auto past = static_cast<size_t>(render - reinterpret_cast<const uint8_t*>(file.data())) + 1;
    CHECK(tw::engine_tweaks::FindPattern({std::string_view(file).substr(past)}, pass::RENDER) == nullptr);
    std::array<uint8_t, 8> self = {0, 0, 0, 0, 1, 0, 0, 0};  // +4: enabled
    std::array<const void*, 16> pass_vtable = {};
    pass_vtable[twattila::shadow_pass::internal::PASS_KIND_SLOT] = reinterpret_cast<const void*>(&StandInKind);
    std::array<StandInPass, 3> passes = {StandInPass{pass_vtable.data(), 5}, StandInPass{pass_vtable.data(), 0},
                                         StandInPass{pass_vtable.data(), 9}};
    pass::original_render = &StandInRender;
    pass::original_render_rome2 = &StandInRenderRome2;
    bool rome2 = false;              // the calls through Rome II's hook
    bool plugin_route = false;       // the calls through the 64-bit engine plugin's hook (7b)
    uint32_t epoch_bump_kind = UINT32_MAX;  // 7b: a registration between this kind's pass_begin and pass_end
    namespace link = twattila::attila64_plugin_link;
    namespace plugin = twattila::attila64_plugin;
    pass::searched = true;           // no search for empire.retail.dll in the test
    pass::binds_counted = true;
    pass::hooks.emplace_back();      // as installed (never installed here)
    // `shadow_view` false: the 64-bit loader's Shadow refresh rest frame (the shadow view not processed: no cascade
    // call).
    const auto frame = [&](bool row, bool casters_skipped, bool shadow_view = true) {
      twattila::shadow_pass::Update(row, casters_skipped, plugin_route);
      stand_in.calls.clear();
      stand_in.numbers = {};
      scene::internal::skipping_casters.store(true);  // the stage bound the sun's map
      const auto render = [&](StandInPass* drawn) {
        if (plugin_route) {
          // The plugin's pass hook (attila64_plugin/hooks.hpp) on an enabled node: the kind, pass_begin, then SKIP
          // returns, DRAW draws, MEASURED draws and calls pass_end.
          plugin::PassMeasure measure;
          const plugin::PassAction action = link::internal::CALLBACKS.pass_begin(drawn->kind, &measure);
          if (action == plugin::PASS_SKIP) return;
          StandInRender(self.data(), nullptr, nullptr, reinterpret_cast<uint8_t*>(drawn));
          if (drawn->kind == epoch_bump_kind) {
            link::internal::epoch.fetch_add(1);
          }
          if (action == plugin::PASS_DRAW_MEASURED) {
            link::internal::CALLBACKS.pass_end(drawn->kind, measure);
          }
        } else if (rome2) {
          pass::OnRenderRome2(self.data(), nullptr, reinterpret_cast<uint8_t*>(drawn));
        } else {
          pass::OnRender(self.data(), nullptr, nullptr, reinterpret_cast<uint8_t*>(drawn));
        }
      };
      if (shadow_view) {
        for (int cascade = 0; cascade < 4; ++cascade) render(&passes[0]);
      }
      render(&passes[2]);
      render(&passes[1]);
      return stand_in.calls;
    };
    const std::vector<std::pair<uint32_t, uint32_t>> every = {{5, 0}, {5, 1}, {5, 2}, {5, 3}, {9, 0}, {0, 0}};
    // Off, and on without the casters skipped: every call, nothing learned.
    for (int index = 0; index < 20; ++index) CHECK(frame(false, true) == every && frame(true, false) == every);
    CHECK(pass::kinds[5].sun_only == 0 && !pass::kinds[0].mixed);
    // On under the rays: 4 frames learn (16 cascade calls), then one cascade call a frame in turn, the others every call.
    for (int index = 0; index < 4; ++index) CHECK(frame(true, true) == every);
    CHECK(pass::kinds[5].sun_only == 16 && pass::kinds[0].mixed && !pass::kinds[9].mixed);
    std::array<uint32_t, 4> drawn_cascades = {};
    for (int index = 0; index < 40; ++index) {
      const auto calls = frame(true, true);
      CHECK(calls.size() == 3 && calls[0].first == 5 && calls[1] == (std::pair<uint32_t, uint32_t>{9, 0})
            && calls[2] == (std::pair<uint32_t, uint32_t>{0, 0}));
      CHECK(calls[0].second == pass::kinds[5].drawn_frames % 4);
      ++drawn_cascades[calls[0].second];
    }
    CHECK(std::ranges::all_of(drawn_cascades, [](uint32_t count) { return count == 10; }));
    // The 64-bit loader's Shadow refresh 2 (the shadow view rests every other frame): the kept call still turns over
    // every cascade (each 10 of 40 drawn frames; turning with the present count and taking a rest frame's 0 calls kept
    // call 0 only), the other kinds every call on both frames.
    drawn_cascades = {};
    for (int index = 0; index < 40; ++index) {
      const auto rest = frame(true, true, false);
      CHECK(rest.size() == 2 && rest[0] == (std::pair<uint32_t, uint32_t>{9, 0})
            && rest[1] == (std::pair<uint32_t, uint32_t>{0, 0}));
      const auto calls = frame(true, true);
      CHECK(calls.size() == 3 && calls[0].first == 5 && calls[1] == (std::pair<uint32_t, uint32_t>{9, 0}));
      ++drawn_cascades[calls[0].second];
    }
    CHECK(std::ranges::all_of(drawn_cascades, [](uint32_t count) { return count == 10; }));
    // The casters back (rays off): every call at the next frame.
    CHECK(frame(true, false) == every);
    // CONTROL: a cascade call that binds another target ends the skip for good.
    stand_in.other_bind_in_cascade = true;
    frame(true, true);
    stand_in.other_bind_in_cascade = false;
    CHECK(pass::kinds[5].mixed);
    for (int index = 0; index < 8; ++index) CHECK(frame(true, true) == every);
    // Rome II (+0x14b84a0, one argument): its pattern not in this DLL, once in Rome II's (when installed); the same
    // learning and turns through its hook.
    CHECK(tw::engine_tweaks::FindPattern({file}, pass::RENDER_ROME2) == nullptr);
    const char* rome2_status = "Rome II's DLL not found";
    if (std::ifstream rome2_stream(R"(C:\Program Files (x86)\Steam\steamapps\common\Total War Rome II\empire.retail.dll)", std::ios::binary);
        rome2_stream.good()) {
      const std::string rome2_file((std::istreambuf_iterator<char>(rome2_stream)), std::istreambuf_iterator<char>());
      const uint8_t* rome2_render = tw::engine_tweaks::FindPattern({rome2_file}, pass::RENDER_ROME2);
      CHECK(rome2_render != nullptr);
      CHECK(tw::engine_tweaks::FindPattern({std::string_view(rome2_file).substr(static_cast<size_t>(rome2_render - reinterpret_cast<const uint8_t*>(rome2_file.data())) + 1)},
                                           pass::RENDER_ROME2) == nullptr);
      CHECK(tw::engine_tweaks::FindPattern({rome2_file}, pass::RENDER) == nullptr);
      rome2_status = "Rome II's pattern once in its DLL (Attila's none there)";
    }
    pass::kinds = {};
    rome2 = true;
    for (int index = 0; index < 4; ++index) CHECK(frame(true, true) == every);
    CHECK(pass::kinds[5].sun_only == 16 && pass::kinds[0].mixed && !pass::kinds[9].mixed);
    drawn_cascades = {};
    for (int index = 0; index < 40; ++index) {
      const auto calls = frame(true, true);
      CHECK(calls.size() == 3 && calls[0].first == 5 && calls[0].second == pass::kinds[5].drawn_frames % 4);
      ++drawn_cascades[calls[0].second];
    }
    CHECK(std::ranges::all_of(drawn_cascades, [](uint32_t count) { return count == 10; }));
    CHECK(frame(false, true) == every);
    std::printf("7 Rome II: %s; through its one-argument hook the same learning and turns\n", rome2_status);
    // 7b. The 64-bit route: no hook of ours (Update told the plugin hooks the pass draw), the plugin's hook calling the
    //     add-on's callbacks (attila64_plugin_link.hpp): the same learning and turns, a refresh rest frame included;
    //     off: every call, nothing counted; a pass_end of another registration (epoch) learns nothing.
    pass::hooks.clear();
    pass::kinds = {};
    rome2 = false;
    plugin_route = true;
    for (int index = 0; index < 4; ++index) CHECK(frame(true, true) == every);
    CHECK(pass::kinds[5].sun_only == 16 && pass::kinds[0].mixed && !pass::kinds[9].mixed
          && pass::kinds[9].sun_only == 4);
    drawn_cascades = {};
    for (int index = 0; index < 40; ++index) {
      CHECK(frame(true, true, false).size() == 2);
      const auto calls = frame(true, true);
      CHECK(calls.size() == 3 && calls[0].first == 5 && calls[0].second == pass::kinds[5].drawn_frames % 4);
      ++drawn_cascades[calls[0].second];
    }
    CHECK(std::ranges::all_of(drawn_cascades, [](uint32_t count) { return count == 10; }));
    CHECK(frame(false, true) == every && pass::kinds[5].calls == 0);
    const uint32_t lone_sun_only = pass::kinds[9].sun_only;
    epoch_bump_kind = 9;
    CHECK(frame(true, true).size() == 3 && pass::kinds[9].sun_only == lone_sun_only);
    epoch_bump_kind = UINT32_MAX;
    CHECK(frame(true, true).size() == 3 && pass::kinds[9].sun_only == lone_sun_only + 1);
    // CONTROL: without the plugin's word Update keeps nothing active with no hook of ours: every call, nothing counted.
    plugin_route = false;
    twattila::shadow_pass::Update(true, true);
    CHECK(!pass::active.load());
    plugin_route = true;
    link::internal::CALLBACKS.before_pack();  // nothing pending: returns
    CHECK(!link::internal::callback_failed.load());
    std::printf(
        "7b 64-bit route: the same learning and turns through the plugin's callbacks (refresh rest frames too), "
        "a pass_end of another registration ignored; CONTROL: without the plugin's word nothing active\n");
    // 7c. The callbacks' registration on a block as the plugin publishes it: registered while a row is wanted and
    //     offered (a module reference held), the bits follow, a row changed after the plugin's init is not wanted,
    //     unregistered with the drain (reference released); a drain that runs out (a hook left inside) keeps the
    //     reference after 1 s; another add-on's callbacks are left alone and not tried again.
    link::Lookup(false);
    CHECK(link::Result() == link::Found::PENDING);
    link::Lookup(true);
    CHECK(link::Result() == link::Found::NOT_INSTALLED);
    plugin::Block block;
    block.rows_applied = plugin::RowBit(plugin::ROW_BONE_FLUSH) | plugin::RowBit(plugin::ROW_PASS_DRAW);
    block.status.store(plugin::STATUS_READY);
    const uint32_t both = block.rows_applied;
    // Not READY: nothing registered.
    link::internal::block = &block;
    link::Update(both);
    CHECK(block.callbacks.load() == nullptr && block.hooks_wanted.load() == 0);
    link::internal::found.store(link::Found::READY);
    const uint32_t epoch_before = link::internal::epoch.load();
    link::Update(plugin::RowBit(plugin::ROW_PASS_DRAW));
    CHECK(block.callbacks.load() == &link::internal::CALLBACKS
          && block.hooks_wanted.load() == plugin::RowBit(plugin::ROW_PASS_DRAW)
          && link::internal::module_reference != nullptr && link::internal::epoch.load() != epoch_before);
    const uint32_t epoch_registered = link::internal::epoch.load();
    link::Update(both);
    CHECK(block.hooks_wanted.load() == both && link::internal::epoch.load() == epoch_registered);
    block.rows_changed.store(plugin::RowBit(plugin::ROW_PASS_DRAW));
    link::Update(both);
    CHECK(block.hooks_wanted.load() == plugin::RowBit(plugin::ROW_BONE_FLUSH)
          && block.callbacks.load() == &link::internal::CALLBACKS);
    block.rows_changed.store(0);
    link::Update(0);
    CHECK(block.callbacks.load() == nullptr && block.hooks_wanted.load() == 0
          && link::internal::module_reference == nullptr);
    link::Update(plugin::RowBit(plugin::ROW_BONE_FLUSH));
    CHECK(block.callbacks.load() == &link::internal::CALLBACKS && link::internal::epoch.load() != epoch_registered);
    // A hook still inside for 50 ms: the wait ends when it leaves, the reference released.
    block.calls_in_flight.store(1);
    std::thread leaving([&block] {
      Sleep(50);
      block.calls_in_flight.store(0);
    });
    const ULONGLONG short_start = GetTickCount64();
    link::Update(0);
    const ULONGLONG short_wait = GetTickCount64() - short_start;
    leaving.join();
    CHECK(short_wait >= 30 && short_wait < 1000 && block.callbacks.load() == nullptr
          && link::internal::module_reference == nullptr);
    link::Update(plugin::RowBit(plugin::ROW_BONE_FLUSH));
    CHECK(block.callbacks.load() == &link::internal::CALLBACKS);
    block.calls_in_flight.store(1);
    const ULONGLONG drain_start = GetTickCount64();
    link::Update(0);
    CHECK(GetTickCount64() - drain_start >= 1000 && block.callbacks.load() == nullptr
          && link::internal::module_reference != nullptr);
    block.calls_in_flight.store(0);
    FreeLibrary(std::exchange(link::internal::module_reference, nullptr));
    constexpr plugin::Callbacks OTHER_ADDON = {};
    block.callbacks.store(&OTHER_ADDON);
    link::Update(both);
    link::Update(both);
    CHECK(block.callbacks.load() == &OTHER_ADDON && block.hooks_wanted.load() == 0 && !link::internal::registered
          && link::internal::lost && link::internal::module_reference == nullptr);
    link::Uninstall();
    CHECK(!link::internal::lost && block.callbacks.load() == &OTHER_ADDON && link::Result() == link::Found::DETACHED
          && link::internal::block == &block);
    // A present after the detach (still in flight): no lookup, nothing registered again; the next attach looks up
    // again.
    block.callbacks.store(nullptr);
    link::Lookup(true);
    link::Update(both);
    CHECK(link::Result() == link::Found::DETACHED && block.callbacks.load() == nullptr
          && block.hooks_wanted.load() == 0);
    link::internal::block = nullptr;
    link::Install();
    CHECK(link::Result() == link::Found::PENDING);
    std::printf(
        "7c callbacks: the lookup final at the last try, registered while READY, wanted and offered, bits follow, a "
        "changed row not wanted, unregistered and drained (a hook leaving after 50 ms ends the wait); a drain past "
        "1 s keeps the reference; another add-on's callbacks left alone; after the detach nothing registers\n");
    plugin_route = false;
    pass::active.store(false);
    std::printf("7 shadow map work: Render's pattern once in the shipped empire.retail.dll; off / casters drawn: every call; "
                "on: cascade kind learned after 16 calls, then 1 of 4 a frame in turn (each cascade 10 of 40 frames), the "
                "main pass and a lone sun-only kind every call; CONTROL: a cascade call binding another target, every call "
                "after it\n");
  }
  // 8. Rome II's Two Shadow Cascades Under Rays (twrome2/shadow_cascades.hpp) over its empire.retail.dll as shipped (when
  //    installed): the pattern once, its five values the game's; on writes 1-2's (2, 0, 0, 0, 2) at their five places and no
  //    other byte; off: the game's bytes; CONTROL: one value another mod's: not found.
  {
    namespace cascades = twrome2::shadow_cascades;
    const char* status = "Rome II's DLL not found";
    if (std::ifstream rome2_stream(R"(C:\Program Files (x86)\Steam\steamapps\common\Total War Rome II\empire.retail.dll)", std::ios::binary);
        rome2_stream.good()) {
      std::string rome2_file((std::istreambuf_iterator<char>(rome2_stream)), std::istreambuf_iterator<char>());
      const std::string shipped = rome2_file;
      uint8_t* found = cascades::Find({rome2_file});
      CHECK(found != nullptr);
      const size_t at = static_cast<size_t>(found - reinterpret_cast<uint8_t*>(rome2_file.data()));
      CHECK(tw::engine_tweaks::FindPattern({std::string_view(rome2_file).substr(at + 1)}, cascades::CODE) == nullptr);
      cascades::Write(found, true);
      size_t changed = 0;
      for (size_t index = 0; index < rome2_file.size(); ++index) {
        if (rome2_file[index] == shipped[index]) continue;
        ++changed;
        CHECK(std::ranges::any_of(cascades::OFFSETS, [&](size_t offset) { return index >= at + offset && index < at + offset + 4; }));
      }
      for (size_t index = 0; index < cascades::OFFSETS.size(); ++index) {
        uint32_t value;
        std::memcpy(&value, found + cascades::OFFSETS[index], sizeof(value));
        CHECK(value == cascades::TWO[index]);
      }
      CHECK(changed == 5);
      cascades::Write(found, false);
      CHECK(rome2_file == shipped);
      found[cascades::OFFSETS[0]] = 5;
      CHECK(cascades::Find({rome2_file}) == nullptr);
      std::printf("8 Rome II's shadow cascades: once at +0x%zx (file), the game's 3, 1, 2, 3, 4; on: 5 bytes changed, all in the five "
                  "values (2, 0, 0, 0, 2); off: the shipped bytes; CONTROL: 3's count another mod's (5): not found\n", at);
      status = nullptr;
    }
    if (status != nullptr) std::printf("8 Rome II's shadow cascades: %s\n", status);
  }
  std::printf("PASS\n");
  return 0;
}
