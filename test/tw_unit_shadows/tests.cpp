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
//    registration and drain on a block as the plugin publishes it; 7d the engine values on the port; 7e the start files'
//    reader (start_files.hpp) over files in a temp folder; 7f the scale rows and Shadow Map Size's key on the port.
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
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "src/games/twattila/attila64_plugin_link.hpp"
#include "src/games/twattila/graphics_hooks.hpp"
#include "src/games/twattila/rt_scene.hpp"
#include "src/games/twattila/shadow_pass.hpp"
#include "src/games/twattila/start_files.hpp"
#include "src/games/twrome2/shadow_cascades.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

namespace hooks = twattila::graphics_hooks;
namespace scene = twattila::rt_scene;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Unit shadows test";
// ReShade's log and global config as 7d reads them: the lines logged, and the config as "section|key" -> text.
static std::vector<std::string> logged;
static std::map<std::string, std::string> config;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { logged.emplace_back(message); }
extern "C" __declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char* section, const char* key, char* value,
                                                            size_t* value_size) {
  const auto found = config.find(std::string(section) + "|" + key);
  if (found == config.end()) {
    *value_size = 0;
    return false;
  }
  // As ReShade's (source/addon.cpp): one element and its null copied, the size the count copied.
  const std::string element = found->second + '\0';
  if (value == nullptr) {
    *value_size = element.size() + 1;
  } else if (*value_size != 0) {
    *value_size = element.copy(value, *value_size - 1);
    value[*value_size] = '\0';
  }
  return true;
}
extern "C" __declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char* section, const char* key, const char* value) {
  config[std::string(section) + "|" + key] = value;
}

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
    link::Update(both, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(block.callbacks.load() == nullptr && block.hooks_wanted.load() == 0);
    link::internal::found.store(link::Found::READY);
    const uint32_t epoch_before = link::internal::epoch.load();
    link::Update(plugin::RowBit(plugin::ROW_PASS_DRAW), link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(block.callbacks.load() == &link::internal::CALLBACKS
          && block.hooks_wanted.load() == plugin::RowBit(plugin::ROW_PASS_DRAW)
          && link::internal::module_reference != nullptr && link::internal::epoch.load() != epoch_before);
    const uint32_t epoch_registered = link::internal::epoch.load();
    link::Update(both, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(block.hooks_wanted.load() == both && link::internal::epoch.load() == epoch_registered);
    // The row's core_wanted bit: only CORE_BATTLE changes (another part's bit kept), written once per change of the
    // row (an equal present writes nothing); a v1 block (no core_wanted) gets no write.
    block.core_wanted.store(plugin::CORE_BATTLE | plugin::CORE_RENDER);
    link::Update(both, false, link::RENDER_CORE_DEFAULT);
    CHECK(block.core_wanted.load() == plugin::CORE_RENDER);
    block.core_wanted.store(plugin::CORE_BATTLE | plugin::CORE_RENDER);
    link::Update(both, false, link::RENDER_CORE_DEFAULT);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | plugin::CORE_RENDER));
    block.core_wanted.store(plugin::CORE_RENDER);
    link::Update(both, true, link::RENDER_CORE_DEFAULT);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | plugin::CORE_RENDER));
    // Keep Render Core Free the same way: only CORE_RENDER changes, on and off (the battle part's bit and a bit no row
    // writes kept), an equal present writes nothing.
    block.core_wanted.store(plugin::CORE_BATTLE | plugin::CORE_STEP);
    link::Update(both, true, true);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | plugin::CORE_RENDER | plugin::CORE_STEP));
    block.core_wanted.store(plugin::CORE_RENDER);
    link::Update(both, true, true);
    CHECK(block.core_wanted.load() == plugin::CORE_RENDER);
    block.core_wanted.store(plugin::CORE_BATTLE | plugin::CORE_RENDER | plugin::CORE_STEP);
    link::Update(both, true, false);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | plugin::CORE_STEP));
    block.core_wanted.store(plugin::CORE_STEP);
    link::Update(both, true, true);
    CHECK(block.core_wanted.load() == (plugin::CORE_RENDER | plugin::CORE_STEP));
    link::Update(both, true, false);
    CHECK(block.core_wanted.load() == plugin::CORE_STEP);
    // RenderCore=0 (the plugin's render word OFF_INI): no render write either way.
    block.render_core_status.store(plugin::CORE_OFF_INI);
    link::Update(both, true, true);
    CHECK(block.core_wanted.load() == plugin::CORE_STEP);
    block.core_wanted.store(plugin::CORE_BATTLE | plugin::CORE_RENDER);
    link::Update(both, true, false);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | plugin::CORE_RENDER));
    block.render_core_status.store(0);
    link::Update(both, true, link::RENDER_CORE_DEFAULT);
    block.core_wanted.store(plugin::CORE_BATTLE | plugin::CORE_RENDER);
    block.bytes = static_cast<uint32_t>(plugin::BlockBytes(1));
    link::Update(both, false, !link::RENDER_CORE_DEFAULT);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | plugin::CORE_RENDER)
          && block.hooks_wanted.load() == both);
    block.bytes = sizeof(plugin::Block);
    block.rows_changed.store(plugin::RowBit(plugin::ROW_PASS_DRAW));
    link::Update(both, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(block.hooks_wanted.load() == plugin::RowBit(plugin::ROW_BONE_FLUSH)
          && block.callbacks.load() == &link::internal::CALLBACKS);
    block.rows_changed.store(0);
    link::Update(0, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(block.callbacks.load() == nullptr && block.hooks_wanted.load() == 0
          && link::internal::module_reference == nullptr);
    link::Update(plugin::RowBit(plugin::ROW_BONE_FLUSH), link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(block.callbacks.load() == &link::internal::CALLBACKS && link::internal::epoch.load() != epoch_registered);
    // A hook still inside for 50 ms: the wait ends when it leaves, the reference released.
    block.calls_in_flight.store(1);
    std::thread leaving([&block] {
      Sleep(50);
      block.calls_in_flight.store(0);
    });
    const ULONGLONG short_start = GetTickCount64();
    link::Update(0, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    const ULONGLONG short_wait = GetTickCount64() - short_start;
    leaving.join();
    CHECK(short_wait >= 30 && short_wait < 1000 && block.callbacks.load() == nullptr
          && link::internal::module_reference == nullptr);
    link::Update(plugin::RowBit(plugin::ROW_BONE_FLUSH), link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(block.callbacks.load() == &link::internal::CALLBACKS);
    block.calls_in_flight.store(1);
    const ULONGLONG drain_start = GetTickCount64();
    link::Update(0, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(GetTickCount64() - drain_start >= 1000 && block.callbacks.load() == nullptr
          && link::internal::module_reference != nullptr);
    block.calls_in_flight.store(0);
    FreeLibrary(std::exchange(link::internal::module_reference, nullptr));
    constexpr plugin::Callbacks OTHER_ADDON = {};
    block.callbacks.store(&OTHER_ADDON);
    link::Update(both, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    link::Update(both, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(block.callbacks.load() == &OTHER_ADDON && block.hooks_wanted.load() == 0 && !link::internal::registered
          && link::internal::lost && link::internal::module_reference == nullptr);
    link::Uninstall();
    CHECK(!link::internal::lost && block.callbacks.load() == &OTHER_ADDON && link::Result() == link::Found::DETACHED
          && link::internal::block == &block);
    // A present after the detach (still in flight): no lookup, nothing registered again; the next attach looks up
    // again.
    block.callbacks.store(nullptr);
    link::Lookup(true);
    link::Update(both, link::BATTLE_CORE_DEFAULT, link::RENDER_CORE_DEFAULT);
    CHECK(link::Result() == link::Found::DETACHED && block.callbacks.load() == nullptr
          && block.hooks_wanted.load() == 0);
    link::internal::block = nullptr;
    link::Install();
    CHECK(link::Result() == link::Found::PENDING);
    // After a detach the next READY's first present writes the battle row's bit again, and the render row's only off
    // its default: at it, the plugin's own bit (its RenderCore key) stays until the row changes. Faster Battle Step's
    // CORE_STEP is written at that first present too (the add-on owns it, Off included): these 3-argument calls pass
    // its default (step_default_bit; the stores leave the bit clear, so the write shows), the 5-argument one the other
    // value.
    const uint32_t render_default_bit = (link::RENDER_CORE_DEFAULT ? plugin::CORE_RENDER : 0u);
    const uint32_t render_other_bit = plugin::CORE_RENDER ^ render_default_bit;
    const uint32_t step_default_bit = (link::BATTLE_STEP_DEFAULT ? plugin::CORE_STEP : 0u);
    const uint32_t step_other_bit = plugin::CORE_STEP ^ step_default_bit;
    link::internal::block = &block;
    link::internal::found.store(link::Found::READY);
    block.core_wanted.store(plugin::CORE_BATTLE | render_other_bit | step_other_bit);
    link::Update(0, false, link::RENDER_CORE_DEFAULT);
    CHECK(block.core_wanted.load() == (render_other_bit | step_default_bit));
    link::Update(0, false, !link::RENDER_CORE_DEFAULT);
    link::Update(0, false, link::RENDER_CORE_DEFAULT);
    CHECK(block.core_wanted.load() == (render_default_bit | step_default_bit));
    link::Update(0, false, link::RENDER_CORE_DEFAULT, 0, !link::BATTLE_STEP_DEFAULT);
    CHECK(block.core_wanted.load() == (render_default_bit | step_other_bit));
    link::Uninstall();
    link::Install();
    link::internal::found.store(link::Found::READY);
    block.core_wanted.store(plugin::CORE_BATTLE | render_default_bit | step_other_bit);
    link::Update(0, true, !link::RENDER_CORE_DEFAULT);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | render_other_bit | step_default_bit));
    // Multithreaded Collisions (PK8 Stage B step 6), ROW_CONTACT_SPLIT offered: while the row is hidden (nullopt,
    // the 3- and 5-argument calls) CORE_SPLIT is not written at the first present or later, either polarity (a batch
    // arm's ContactSplit=1 / 0 holds); shown, the bit follows the row once per change; not offered, never written.
    const uint32_t split_row = plugin::RowBit(plugin::ROW_CONTACT_SPLIT);
    block.rows_applied |= split_row;
    for (const uint32_t split_bit : {0u, plugin::CORE_SPLIT}) {
      link::Uninstall();
      link::Install();
      link::internal::found.store(link::Found::READY);
      block.core_wanted.store(plugin::CORE_BATTLE | split_bit);
      link::Update(0, true, link::RENDER_CORE_DEFAULT);
      CHECK((block.core_wanted.load() & plugin::CORE_SPLIT) == split_bit);
      link::Update(0, false, link::RENDER_CORE_DEFAULT, 0, link::BATTLE_STEP_DEFAULT);
      CHECK((block.core_wanted.load() & plugin::CORE_SPLIT) == split_bit);
      link::Update(0, true, link::RENDER_CORE_DEFAULT, 0, !link::BATTLE_STEP_DEFAULT, std::nullopt);
      CHECK((block.core_wanted.load() & plugin::CORE_SPLIT) == split_bit);
    }
    link::Uninstall();
    link::Install();
    link::internal::found.store(link::Found::READY);
    block.core_wanted.store(plugin::CORE_BATTLE);
    link::Update(0, true, link::RENDER_CORE_DEFAULT, 0, link::BATTLE_STEP_DEFAULT, true);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | step_default_bit | plugin::CORE_SPLIT));
    block.core_wanted.fetch_and(~plugin::CORE_SPLIT);
    link::Update(0, true, link::RENDER_CORE_DEFAULT, 0, link::BATTLE_STEP_DEFAULT, true);
    CHECK((block.core_wanted.load() & plugin::CORE_SPLIT) == 0);
    block.core_wanted.fetch_or(plugin::CORE_SPLIT);
    link::Update(0, true, link::RENDER_CORE_DEFAULT, 0, link::BATTLE_STEP_DEFAULT, false);
    CHECK(block.core_wanted.load() == (plugin::CORE_BATTLE | step_default_bit));
    link::Uninstall();
    link::Install();
    block.rows_applied &= ~split_row;
    link::internal::found.store(link::Found::READY);
    for (const bool split_on : {true, false}) {
      block.core_wanted.store(plugin::CORE_BATTLE | (split_on ? 0u : plugin::CORE_SPLIT));
      link::Update(0, true, link::RENDER_CORE_DEFAULT, 0, link::BATTLE_STEP_DEFAULT, split_on);
      CHECK((block.core_wanted.load() & plugin::CORE_SPLIT) == (split_on ? 0u : plugin::CORE_SPLIT));
    }
    link::Uninstall();
    link::internal::block = nullptr;
    link::Install();
    CHECK(link::Result() == link::Found::PENDING);
    std::printf(
        "7c callbacks: the lookup final at the last try, registered while READY, wanted and offered, bits follow, a "
        "changed row not wanted, unregistered and drained (a hook leaving after 50 ms ends the wait); a drain past "
        "1 s keeps the reference; another add-on's callbacks left alone; after the detach nothing registers; Keep "
        "Battle Core Free writes only CORE_BATTLE, once per change, none on a v1 block, again after a detach; Keep "
        "Render Core Free only CORE_RENDER, on and off, none with RenderCore=0 or on a v1 block, its first present only "
        "off its default; Multithreaded Collisions hidden: CORE_SPLIT untouched either way, shown: set / cleared once "
        "per change, not offered: untouched\n");
    // 7d. The engine values on the port (FindTweaks, MirrorRestartValues): each Tweak gets the plugin's checked value
    //     (live: the game's address; restart-only: an add-on cell at the default), a refused one none (the warning line);
    //     the mirror writes [Attila64Plugin.Values] clamped to tweaks.h's range, read once, written on a change with one
    //     line per write; the pending notes (plan 2.3's four cases, Loader Frame Cap's); a v1 block; Uninstall.
    {
      const auto lines = [](std::string_view text) {
        return std::ranges::count_if(logged, [text](const std::string& line) { return line.find(text) != std::string::npos; });
      };
      const auto key = [](std::string_view name) {
        const auto found = config.find(std::format("Attila64Plugin.Values|{}", name));
        return (found == config.end() ? std::string("absent") : found->second);
      };
      float live_value = 2.f;
      float wrong_kind_value = 1.f;
      std::array<plugin::TweakSlot, 10> slots = {
          plugin::TweakSlot{.name = "LIVE_FLOAT", .value_address = reinterpret_cast<uintptr_t>(&live_value),
                            .default_bits = std::bit_cast<uint32_t>(2.f), .written_bits = std::bit_cast<uint32_t>(2.f),
                            .type = plugin::TWEAK_FLOAT, .verdict = plugin::VERDICT_OK},
          plugin::TweakSlot{.name = "ABSENT_ONE", .verdict = plugin::VERDICT_ABSENT},
          plugin::TweakSlot{.name = "CHANGED_ONE", .type = plugin::TWEAK_FLOAT, .verdict = plugin::VERDICT_CHANGED},
          plugin::TweakSlot{.name = "WRONG_KIND", .value_address = reinterpret_cast<uintptr_t>(&wrong_kind_value),
                            .type = plugin::TWEAK_BOOL, .verdict = plugin::VERDICT_OK},
          plugin::TweakSlot{.name = "SOUND_DEFAULT_BUFFER_SIZE_IN_KB", .default_bits = 0x5da4, .written_bits = 0x5da4,
                            .type = plugin::TWEAK_UINT, .verdict = plugin::VERDICT_OK, .restart_only = 1,
                            .restart_state = plugin::RESTART_APPLIED},
          plugin::TweakSlot{.name = "SOUND_DEFAULT_BUFFER_LOW_RATIO", .default_bits = 0x3f666666,
                            .written_bits = 0x3f666666, .type = plugin::TWEAK_FLOAT, .verdict = plugin::VERDICT_OK,
                            .restart_only = 1, .restart_state = plugin::RESTART_APPLIED},
          plugin::TweakSlot{.name = "CHANGED_TWO", .type = plugin::TWEAK_FLOAT, .verdict = plugin::VERDICT_CHANGED},
          // A restart-only value of another type than this add-on's range for it.
          plugin::TweakSlot{.name = "SOUND_LOWER_ENGINE_DEFAULT_BUFFER_SIZE_IN_KB", .type = plugin::TWEAK_INT,
                            .verdict = plugin::VERDICT_OK, .restart_only = 1, .restart_state = plugin::RESTART_APPLIED},
          // A restart-only value this start applied from its saved key, in an option with a refused value.
          plugin::TweakSlot{.name = "SOUND_WAVEFILE_BUFFER_SIZE_IN_KB", .default_bits = 0x15800, .written_bits = 0x20000,
                            .type = plugin::TWEAK_UINT, .verdict = plugin::VERDICT_OK, .restart_only = 1,
                            .restart_state = plugin::RESTART_APPLIED},
          plugin::TweakSlot{.name = "CHANGED_THREE", .type = plugin::TWEAK_FLOAT, .verdict = plugin::VERDICT_CHANGED},
      };
      plugin::TweakSlot& memory_slot = slots[4];
      tw::engine_tweaks::Option live = {.scale = 1.5f, .tweaks = {{.name = "LIVE_FLOAT"}}};
      tw::engine_tweaks::Option others = {
          .tweaks = {{.name = "CHANGED_ONE"}, {.name = "WRONG_KIND"}, {.name = "ABSENT_ONE"}, {.name = "NOT_LISTED"}}};
      tw::engine_tweaks::Option memory = {.scale = 2.f,
                                          .tweaks = {{.name = "SOUND_DEFAULT_BUFFER_SIZE_IN_KB", .integer = true}}};
      tw::engine_tweaks::Option ratio = {.tweaks = {{.name = "SOUND_DEFAULT_BUFFER_LOW_RATIO"}}};
      // Two values, one refused: neither applied (the row hides).
      tw::engine_tweaks::Option half = {.scale = 2.f, .tweaks = {{.name = "LIVE_FLOAT"}, {.name = "CHANGED_TWO"}}};
      tw::engine_tweaks::Option lower = {
          .tweaks = {{.name = "SOUND_LOWER_ENGINE_DEFAULT_BUFFER_SIZE_IN_KB", .integer = true}}};
      // A restart value and a refused one: neither, and its restart cell cleared (its saved key kept, no note).
      tw::engine_tweaks::Option saved = {
          .scale = 2.f, .tweaks = {{.name = "SOUND_WAVEFILE_BUFFER_SIZE_IN_KB", .integer = true}, {.name = "CHANGED_THREE"}}};
      std::array<tw::engine_tweaks::Option*, 7> options = {&live, &others, &memory, &ratio, &half, &lower, &saved};
      bool values_found = true;  // the add-on's graphics_found after FindTweaks: Update only applies
      renodx::utils::settings::Setting values_status;
      const auto present = [&](bool loader_frame_cap) {
        tw::engine_tweaks::Update(L"empire.retail.dll", options, nullptr, &values_found, &values_status);
        link::MirrorRestartValues(loader_frame_cap, 0, true);
      };
      plugin::Block values_block;
      values_block.status.store(plugin::STATUS_READY);
      values_block.tweaks = slots.data();
      values_block.tweak_count = static_cast<uint32_t>(slots.size());
      values_block.tweaks_status.store(plugin::TWEAKS_PENDING);
      link::internal::block = &values_block;
      link::internal::found.store(link::Found::READY);
      config.clear();
      config["Attila64Plugin.Values|SOUND_WAVEFILE_BUFFER_SIZE_IN_KB"] = "00020000";
      logged.clear();
      // The plugin's check pending: not found yet.
      CHECK(!link::FindTweaks(options) && !link::TweaksFound());
      values_block.tweaks_status.store(plugin::TWEAKS_READY);
      CHECK(link::FindTweaks(options) && link::TweaksFound());
      CHECK(live.tweaks[0].value == &live_value && memory.tweaks[0].value != nullptr
            && *static_cast<const uint32_t*>(memory.tweaks[0].value) == 0x5da4 && ratio.tweaks[0].value != nullptr
            && std::ranges::all_of(others.tweaks, [](const auto& tweak) { return tweak.value == nullptr; })
            && half.tweaks[0].value == nullptr && half.tweaks[1].value == nullptr && lower.tweaks[0].value == nullptr
            && saved.tweaks[0].value == nullptr && saved.tweaks[1].value == nullptr
            && link::internal::restart_values[8].slot == nullptr);
      CHECK(link::OptionsRefused() && logged.size() == 1
            && lines("engine options not turned on: rows 0x0, values CHANGED_ONE, WRONG_KIND, CHANGED_TWO, "
                     "SOUND_LOWER_ENGINE_DEFAULT_BUFFER_SIZE_IN_KB, CHANGED_THREE")
                   == 1);
      // The values: live scaled in the game's memory, the restart cell scaled; the mirror writes the changed key only,
      // one line, the frame cap key absent and off (no write).
      logged.clear();
      present(false);
      CHECK(live_value == 3.f && wrong_kind_value == 1.f
            && *static_cast<const uint32_t*>(memory.tweaks[0].value) == 0xbb48);
      CHECK(key("SOUND_DEFAULT_BUFFER_SIZE_IN_KB") == "0000bb48" && key("SOUND_DEFAULT_BUFFER_LOW_RATIO") == "absent"
            && key("SolFrameCap") == "absent" && key("SOUND_WAVEFILE_BUFFER_SIZE_IN_KB") == "00020000"
            && config.size() == 2 && logged.size() == 1
            && lines("engine value SOUND_DEFAULT_BUFFER_SIZE_IN_KB: 0xbb48 at the next start (this start: 0x5da4)") == 1);
      CHECK(link::ValuesPending() && !link::SolFrameCapPending());
      // The next present: nothing written, no line.
      logged.clear();
      present(false);
      CHECK(logged.empty() && config.size() == 2 && link::ValuesPending());
      // The clamp: tweaks.h's range (a hand-edited slider): above the top, a float's NaN to the low end, above the top.
      memory.scale = 10.f;
      present(false);
      CHECK(key("SOUND_DEFAULT_BUFFER_SIZE_IN_KB") == "00017690" && logged.size() == 1);
      ratio.scale = std::nanf("");
      present(false);
      CHECK(key("SOUND_DEFAULT_BUFFER_LOW_RATIO") == "3ee66666" && logged.size() == 2);
      ratio.scale = 5.f;
      present(false);
      CHECK(key("SOUND_DEFAULT_BUFFER_LOW_RATIO") == "3f7fbe77" && logged.size() == 3
            && lines("engine value SOUND_DEFAULT_BUFFER_LOW_RATIO: 0x3f7fbe77 at the next start (this start: 0x3f666666)")
                   == 1);
      // Plan 2.3's four cases. Both back at the game's default: equal to what this start applied, no note.
      memory.scale = 1.f;
      ratio.scale = 1.f;
      present(false);
      CHECK(key("SOUND_DEFAULT_BUFFER_SIZE_IN_KB") == "00005da4" && key("SOUND_DEFAULT_BUFFER_LOW_RATIO") == "3f666666"
            && !link::ValuesPending());
      // (1) This start applied 2x, the row now 1x: the note.
      memory_slot.written_bits = 0xbb48;
      present(false);
      CHECK(link::ValuesPending());
      // (2) The row at 2x again, equal to this start: no note.
      memory.scale = 2.f;
      present(false);
      CHECK(!link::ValuesPending());
      // (3) Out of range at this start (the plugin kept the default), the row at the default: the note.
      memory.scale = 1.f;
      memory_slot.written_bits = 0x5da4;
      memory_slot.restart_state = plugin::RESTART_OUT_OF_RANGE;
      present(false);
      CHECK(link::ValuesPending());
      // (4) Not on DX11 (not applied): the note.
      memory_slot.restart_state = plugin::RESTART_NOT_DX11;
      present(false);
      CHECK(link::ValuesPending());
      memory_slot.restart_state = plugin::RESTART_APPLIED;
      present(false);
      CHECK(!link::ValuesPending());
      // Loader Frame Cap: on writes 1 (one line) and is pending (this start read none); off again writes 0, not pending;
      // a start that read 1 with the row off: pending.
      logged.clear();
      present(true);
      CHECK(key("SolFrameCap") == "1" && logged.size() == 1
            && lines("engine value SolFrameCap: 0x1 at the next start (this start: 0x0)") == 1
            && link::SolFrameCapPending());
      present(true);
      CHECK(logged.size() == 1 && link::SolFrameCapPending());
      present(false);
      CHECK(key("SolFrameCap") == "0" && logged.size() == 2 && !link::SolFrameCapPending());
      link::internal::sol_frame_cap_at_start = true;
      present(false);
      CHECK(link::SolFrameCapPending());
      present(true);
      CHECK(!link::SolFrameCapPending());
      link::internal::sol_frame_cap_at_start = false;
      present(false);
      // A key not 8 hex digits (hand-edited) is written again at the next find; a valid equal one is not.
      config["Attila64Plugin.Values|SOUND_DEFAULT_BUFFER_SIZE_IN_KB"] = "5da4";
      logged.clear();
      CHECK(link::FindTweaks(options));
      logged.clear();
      present(false);
      CHECK(key("SOUND_DEFAULT_BUFFER_SIZE_IN_KB") == "00005da4" && logged.size() == 1
            && lines("SOUND_DEFAULT_BUFFER_SIZE_IN_KB") == 1);
      // Tweaks=0 (NONE): found, no values, nothing refused.
      values_block.tweaks_status.store(plugin::TWEAKS_NONE);
      logged.clear();
      CHECK(link::FindTweaks(options) && live.tweaks[0].value == nullptr && memory.tweaks[0].value == nullptr
            && !link::internal::tweaks_refused.load() && logged.empty());
      // A v1 block: found, no values, no mirror writes.
      values_block.tweaks_status.store(plugin::TWEAKS_READY);
      values_block.bytes = static_cast<uint32_t>(plugin::BlockBytes(1));
      const size_t keys_before = config.size();
      CHECK(link::FindTweaks(options) && live.tweaks[0].value == nullptr && memory.tweaks[0].value == nullptr);
      present(true);
      CHECK(config.size() == keys_before && key("SolFrameCap") == "0" && logged.empty());
      values_block.bytes = sizeof(plugin::Block);
      CHECK(key("SOUND_WAVEFILE_BUFFER_SIZE_IN_KB") == "00020000");
      // Uninstall: found, refused and pending states reset; the next READY finds the values again.
      CHECK(link::FindTweaks(options) && link::TweaksFound());
      memory.scale = 2.f;
      present(true);
      CHECK(link::ValuesPending() && link::SolFrameCapPending());
      link::Uninstall();
      CHECK(!link::TweaksFound() && !link::ValuesPending() && !link::SolFrameCapPending()
            && !link::internal::tweaks_refused.load() && !link::internal::sol_frame_cap_mirrored.has_value());
      link::internal::block = nullptr;
      link::Install();
      CHECK(link::Result() == link::Found::PENDING && !link::FindTweaks(options));
      std::printf(
          "7d engine values: pending check not found; live and restart values matched (absent / changed / wrong type "
          "/ unlisted / a range of another type none, the refused named once; an option with one refused: none of "
          "its values, nothing applied, its restart cell cleared: a saved key kept, no note); the mirror writes a changed key once with one line, clamped (top, "
          "NaN low, float top); plan 2.3's four pending cases; Loader Frame Cap pending / not pending; a bad key "
          "rewritten; Tweaks=0 and a v1 block without values; Uninstall resets\n");
    }
    // 7e. The start files' reader (Sol's settings_get, S28 picker.c:54-70): the last line of a name counts, the name in
    //     either case and followed by '=', the value not trimmed and cut at CR / LF; no file or no line: none. The
    //     add-on's FrameBridge test (FrameBridgeOff: the last addon= line starting with "off" in either case) and the
    //     lines walk.
    {
      namespace start_files = twattila::start_files;
      const std::filesystem::path folder =
          std::filesystem::temp_directory_path() / std::format("renodx_start_files_{}", GetCurrentProcessId());
      std::filesystem::create_directories(folder);
      const auto write = [&folder](const char* name, std::string_view text) {
        std::ofstream(folder / name, std::ios::binary) << text;
        return folder / name;
      };
      const std::filesystem::path settings = write(
          "settings.txt",
          "Task_Waits=fast\r\ninterface_size=150\ntask_waits=Fast\r\nshadow_refresh=2\rx\nupscalingx=1\nupscaling\n"
          "perf_fix= 0 \nflag_wave=\n");
      CHECK(start_files::Value(settings, "task_waits") == "Fast" && start_files::Value(settings, "TASK_WAITS") == "Fast");
      CHECK(start_files::Value(settings, "interface_size") == "150" && start_files::Value(settings, "shadow_refresh") == "2");
      CHECK(start_files::Value(settings, "perf_fix") == " 0 " && start_files::Value(settings, "flag_wave") == "");
      CHECK(!start_files::Value(settings, "upscaling").has_value() && !start_files::Value(settings, "theme").has_value());
      CHECK(!start_files::Value(folder / "none.txt", "addon").has_value());
      CHECK(start_files::StartsWithNoCase("ADDON=OFF", "addon=off") && start_files::StartsWithNoCase("Off", "off")
            && !start_files::StartsWithNoCase("of", "off") && !start_files::StartsWithNoCase("on", "off"));
      // FrameBridge's settings.txt (the add-on's FrameBridgeOff): as the loader writes it, hand-edited, a mixed file
      // the loader leaves alone (its last addon= line is the tick), without the line, and no file.
      CHECK(start_files::FrameBridgeOff(write("fb_off.txt", "addon=off\r\nsuper_resolution=dlss\r\n")));
      CHECK(start_files::FrameBridgeOff(write("fb_upper.txt", "frame_generation=on\nADDON=OFF\n"))
            && start_files::FrameBridgeOff(write("fb_offx.txt", "addon=offx\n")));
      CHECK(start_files::FrameBridgeOff(write("fb_mixed_off.txt", "addon=on\naddon=off\n"))
            && !start_files::FrameBridgeOff(write("fb_mixed_on.txt", "addon=off\naddon=on\n")));
      CHECK(!start_files::FrameBridgeOff(write("fb_on.txt", "addon=on\r\n"))
            && !start_files::FrameBridgeOff(write("fb_of.txt", "addon=of\n"))
            && !start_files::FrameBridgeOff(write("fb_none.txt", "x=1\n"))
            && !start_files::FrameBridgeOff(folder / "none.txt"));
      std::vector<std::string> lines_seen;
      start_files::ForEachLine(write("perffix64.log", "01:37:48.419 flag wave: on (a pack)\r\nlast"),
                               [&lines_seen](std::string_view line) { lines_seen.emplace_back(line); });
      CHECK(lines_seen.size() == 2 && lines_seen[0] == "01:37:48.419 flag wave: on (a pack)" && lines_seen[1] == "last");
      std::filesystem::remove_all(folder);
      std::printf(
          "7e start files: the last line of a name counts (either case, '=' next), the value as written (CR / LF cut, "
          "not trimmed); no line / no file: none; FrameBridge's addon=off in either case; lines walked in order\n");
    }
    // 7f. The scale rows on the port (PublishScales, Shadow Map Size's mirror): a scale stored only when it changed, its
    //     scales_active bit while it is not neutral and its row is offered (a row not offered: no bit, the scale still
    //     stored; the grass scales 19-21 by ROW_GRASS alone, 18 by ROW_CAMPAIGN_TREES, the sprite scales 22-23 by
    //     ROW_SPRITES, a sprite scale not finite and above 0 sent as 0), one line per change of the
    //     bits, nothing on a v1 block; the plugin's notices logged once per new bit; ShadowMapSize written clamped, one
    //     line per write, its own pending flag against the key this start read (out of range: the game's sizes), only while its row is
    //     offered; the scales kept at the detach.
    {
      const auto lines = [](std::string_view text) {
        return std::ranges::count_if(logged, [text](const std::string& line) { return line.find(text) != std::string::npos; });
      };
      const auto shadow_key = [] {
        const auto found = config.find("Attila64Plugin.Values|ShadowMapSize");
        return (found == config.end() ? std::string("absent") : found->second);
      };
      plugin::Block scale_block;
      scale_block.status.store(plugin::STATUS_READY);
      for (uint32_t index = 0; index < plugin::SCALE_COUNT; ++index) {
        scale_block.scales[index].store(plugin::NEUTRAL_SCALES[index]);
      }
      scale_block.rows_applied = plugin::RowBit(plugin::ROW_UNIT_LOD) | plugin::RowBit(plugin::ROW_MODEL_DETAIL)
                                 | plugin::RowBit(plugin::ROW_SHADOW_MAP);
      scale_block.rows_tried = scale_block.rows_applied;
      link::internal::block = &scale_block;
      link::internal::found.store(link::Found::READY);
      config.clear();
      logged.clear();
      std::array<float, plugin::SCALE_COUNT> scales = plugin::NEUTRAL_SCALES;
      // Neutral: no bit, no line.
      link::PublishScales(scales);
      CHECK(scale_block.scales_active.load() == 0 && logged.empty());
      // Unit Detail 2x on foot soldiers and Model Detail 2x (its scale 1 / 4): bits 0 and 17, one line.
      scales[0] = 2.f;
      scales[plugin::SCALE_MODEL_DETAIL] = 0.25f;
      link::PublishScales(scales);
      CHECK(scale_block.scales[0].load() == 2.f && scale_block.scales[plugin::SCALE_MODEL_DETAIL].load() == 0.25f
            && scale_block.scales[1].load() == 1.f && scale_block.scales_active.load() == 0x20001 && logged.size() == 1
            && lines("engine scales active 0x20001") == 1);
      link::PublishScales(scales);
      CHECK(logged.size() == 1);
      // The unit shadows' 0 (casters skipped): bit 16; a grass scale (its row not applied here): stored, no bit.
      scales[16] = 0.f;
      scales[plugin::SCALE_GRASS_LOD] = 4.f;
      link::PublishScales(scales);
      CHECK(scale_block.scales[16].load() == 0.f && scale_block.scales[plugin::SCALE_GRASS_LOD].load() == 4.f
            && scale_block.scales_active.load() == 0x30001 && lines("engine scales active 0x30001") == 1);
      // Model Detail not offered (changed after the plugin; its hook may stay in): its bit clear, Unit Detail's kept.
      scale_block.rows_changed.store(plugin::RowBit(plugin::ROW_MODEL_DETAIL));
      link::PublishScales(scales);
      CHECK(scale_block.scales_active.load() == 0x10001 && lines("engine scales active 0x10001") == 1);
      scale_block.rows_changed.store(0);
      // Back to neutral: no bit, one line.
      scales = plugin::NEUTRAL_SCALES;
      link::PublishScales(scales);
      CHECK(scale_block.scales_active.load() == 0 && scale_block.scales[0].load() == 1.f
            && scale_block.scales[plugin::SCALE_GRASS_LOD].load() == 1.f && lines("engine scales active 0x0") == 1);
      // The grass and campaign tree rows (scales 18-21): Campaign Tree Detail 2x (18 = 4), Grass Detail Distance 2x
      // (19 = 4, 20 = 2) and Grass Density 1.5x (21). Their rows not applied: stored, no bit. ROW_GRASS_CELLS alone (it
      // reads ROW_GRASS's 19 and 20): no bit. ROW_GRASS: 19-21; ROW_CAMPAIGN_TREES: 18; ROW_GRASS changed after the
      // plugin: 18 alone; Grass Density alone: 21; back to neutral: none. (The fold's values and Campaign Tree Detail's
      // value rule live in addon.cpp: F7's plugin_grass, plugin_grass_tree and plugin_tree_refused.)
      {
        const uint32_t scale_rows = scale_block.rows_applied;
        scales[plugin::SCALE_CAMPAIGN_TREES] = 4.f;
        scales[plugin::SCALE_GRASS_LOD] = 4.f;
        scales[plugin::SCALE_GRASS_RADIUS] = 2.f;
        scales[plugin::SCALE_GRASS_DENSITY] = 1.5f;
        logged.clear();
        link::PublishScales(scales);
        CHECK(scale_block.scales[plugin::SCALE_CAMPAIGN_TREES].load() == 4.f
              && scale_block.scales[plugin::SCALE_GRASS_LOD].load() == 4.f
              && scale_block.scales[plugin::SCALE_GRASS_RADIUS].load() == 2.f
              && scale_block.scales[plugin::SCALE_GRASS_DENSITY].load() == 1.5f && scale_block.scales_active.load() == 0
              && logged.empty());
        scale_block.rows_applied |= plugin::RowBit(plugin::ROW_GRASS_CELLS);
        link::PublishScales(scales);
        CHECK(scale_block.scales_active.load() == 0 && logged.empty());
        scale_block.rows_applied |= plugin::RowBit(plugin::ROW_GRASS);
        link::PublishScales(scales);
        CHECK(scale_block.scales_active.load() == 0x380000 && lines("engine scales active 0x380000") == 1);
        scale_block.rows_applied |= plugin::RowBit(plugin::ROW_CAMPAIGN_TREES);
        link::PublishScales(scales);
        CHECK(scale_block.scales_active.load() == 0x3c0000 && lines("engine scales active 0x3c0000") == 1);
        scale_block.rows_changed.store(plugin::RowBit(plugin::ROW_GRASS));
        link::PublishScales(scales);
        CHECK(scale_block.scales_active.load() == 0x40000 && lines("engine scales active 0x40000") == 1);
        scale_block.rows_changed.store(0);
        scales[plugin::SCALE_CAMPAIGN_TREES] = 1.f;
        scales[plugin::SCALE_GRASS_LOD] = 1.f;
        scales[plugin::SCALE_GRASS_RADIUS] = 1.f;
        link::PublishScales(scales);
        CHECK(scale_block.scales_active.load() == 0x200000 && scale_block.scales[plugin::SCALE_GRASS_LOD].load() == 1.f
              && lines("engine scales active 0x200000") == 1);
        scales = plugin::NEUTRAL_SCALES;
        link::PublishScales(scales);
        CHECK(scale_block.scales_active.load() == 0 && scale_block.scales[plugin::SCALE_GRASS_DENSITY].load() == 1.f
              && logged.size() == 5);
        scale_block.rows_applied = scale_rows;
      }
      // The sprite rows (scales 22 and 23, ROW_SPRITES): Small Soldiers as Sprites at 54 (a 1080-pixel focal length /
      // Sprite Size 20) and Dead Unit Detail 0.5x. The row not applied (its rows hidden): stored, no bit, no line.
      // Applied: 22 and 23; changed after the plugin: none. A sprite scale not finite and above 0 (negative, NaN, +inf):
      // sent as 0, its neutral, Dead Unit Detail's bit alone; Small Soldiers as Sprites alone: 22; back to neutral:
      // none.
      {
        const uint32_t scale_rows = scale_block.rows_applied;
        const auto sprite_scale = [&] { return scale_block.scales[plugin::SCALE_SPRITE_DISTANCE].load(); };
        logged.clear();
        scales[plugin::SCALE_SPRITE_DISTANCE] = 54.f;
        scales[plugin::SCALE_DEAD_UNITS] = 0.5f;
        link::PublishScales(scales);
        CHECK(sprite_scale() == 54.f && scale_block.scales[plugin::SCALE_DEAD_UNITS].load() == 0.5f
              && scale_block.scales_active.load() == 0 && logged.empty());
        scale_block.rows_applied |= plugin::RowBit(plugin::ROW_SPRITES);
        link::PublishScales(scales);
        CHECK(scale_block.scales_active.load() == 0xc00000 && lines("engine scales active 0xc00000") == 1);
        scale_block.rows_changed.store(plugin::RowBit(plugin::ROW_SPRITES));
        link::PublishScales(scales);
        CHECK(scale_block.scales_active.load() == 0 && lines("engine scales active 0x0") == 1);
        scale_block.rows_changed.store(0);
        scales[plugin::SCALE_SPRITE_DISTANCE] = -54.f;
        link::PublishScales(scales);
        CHECK(sprite_scale() == 0.f && scale_block.scales_active.load() == 0x800000
              && lines("engine scales active 0x800000") == 1);
        scales[plugin::SCALE_SPRITE_DISTANCE] = std::numeric_limits<float>::quiet_NaN();
        link::PublishScales(scales);
        CHECK(sprite_scale() == 0.f && scale_block.scales_active.load() == 0x800000 && logged.size() == 3);
        scales[plugin::SCALE_SPRITE_DISTANCE] = std::numeric_limits<float>::infinity();
        link::PublishScales(scales);
        CHECK(sprite_scale() == 0.f && scale_block.scales_active.load() == 0x800000 && logged.size() == 3);
        scales[plugin::SCALE_SPRITE_DISTANCE] = 54.f;
        scales[plugin::SCALE_DEAD_UNITS] = 1.f;
        link::PublishScales(scales);
        CHECK(sprite_scale() == 54.f && scale_block.scales_active.load() == 0x400000
              && lines("engine scales active 0x400000") == 1);
        scales = plugin::NEUTRAL_SCALES;
        link::PublishScales(scales);
        CHECK(sprite_scale() == 0.f && scale_block.scales_active.load() == 0 && logged.size() == 5);
        scale_block.rows_applied = scale_rows;
      }
      // A v1 block: nothing stored.
      scale_block.bytes = static_cast<uint32_t>(plugin::BlockBytes(1));
      scales[0] = 3.f;
      link::PublishScales(scales);
      CHECK(scale_block.scales[0].load() == 1.f && scale_block.scales_active.load() == 0);
      scale_block.bytes = sizeof(plugin::Block);
      scales[0] = 1.f;
      // The notices: one line per new bit.
      logged.clear();
      scale_block.notices.fetch_or(plugin::NOTICE_REST_PACK);
      link::PublishScales(scales);
      link::PublishScales(scales);
      CHECK(logged.size() == 1 && lines("engine plugin notices 0x1 (rest pack: yes, model table full: no)") == 1);
      scale_block.notices.fetch_or(plugin::NOTICE_MODEL_TABLE_FULL);
      link::PublishScales(scales);
      CHECK(logged.size() == 2 && lines("engine plugin notices 0x3 (rest pack: yes, model table full: yes)") == 1);
      // Shadow Map Size: this start read 1 (4096). The row at 1: nothing written, not pending; at 2: written, one line,
      // pending (the audio note's flag stays clear); a row value past 2 (a hand-edited ReShade.ini): clamped; back at 1.
      config["Attila64Plugin.Values|ShadowMapSize"] = "1";
      logged.clear();
      link::MirrorRestartValues(false, 1, true);
      CHECK(shadow_key() == "1" && logged.empty() && !link::ShadowMapSizePending());
      link::MirrorRestartValues(false, 2, true);
      CHECK(shadow_key() == "2" && logged.size() == 1
            && lines("engine value ShadowMapSize: 0x2 at the next start (this start: 0x1)") == 1
            && link::ShadowMapSizePending() && !link::ValuesPending() && !link::SolFrameCapPending());
      link::MirrorRestartValues(false, 7, true);
      CHECK(shadow_key() == "2" && logged.size() == 1 && link::ShadowMapSizePending());
      link::MirrorRestartValues(false, 1, true);
      CHECK(shadow_key() == "1" && logged.size() == 2 && !link::ShadowMapSizePending());
      // Its row not offered: the key left alone, no note.
      scale_block.rows_changed.store(plugin::RowBit(plugin::ROW_SHADOW_MAP));
      link::MirrorRestartValues(false, 2, true);
      CHECK(shadow_key() == "1" && logged.size() == 2 && !link::ShadowMapSizePending());
      scale_block.rows_changed.store(0);
      // The detach: the mirror's state reset, the scales kept (a hook reads them for the process).
      scales[0] = 2.f;
      link::PublishScales(scales);
      link::MirrorRestartValues(false, 2, true);
      CHECK(link::ShadowMapSizePending());
      link::Uninstall();
      CHECK(!link::ShadowMapSizePending() && !link::internal::shadow_map_size_mirrored.has_value()
            && link::internal::notices_logged == 0 && scale_block.scales[0].load() == 2.f
            && scale_block.scales_active.load() == 1);
      // A start that read a key out of range (the plugin kept the game's sizes): the row at 0 is this start's; at 1
      // pending.
      link::internal::shadow_map_size_at_start.reset();
      config["Attila64Plugin.Values|ShadowMapSize"] = "5";
      link::internal::found.store(link::Found::READY);
      logged.clear();
      link::MirrorRestartValues(false, 0, true);
      CHECK(shadow_key() == "0" && lines("engine value ShadowMapSize: 0x0 at the next start (this start: 0x0)") == 1
            && !link::ShadowMapSizePending());
      link::MirrorRestartValues(false, 1, true);
      CHECK(shadow_key() == "1" && link::ShadowMapSizePending());
      // A start the plugin saw as not DX11 (a restart-only value with a key: NOT_DX11): nothing patched, so the row at
      // the key's 1 is pending; the same value APPLIED: not pending.
      link::internal::shadow_map_size_at_start = 1;
      link::MirrorRestartValues(false, 1, true);
      CHECK(!link::ShadowMapSizePending());
      plugin::TweakSlot route_slot = {.name = "SOUND_DEFAULT_BUFFER_SIZE_IN_KB", .default_bits = 0x5da4,
                                      .written_bits = 0x5da4, .type = plugin::TWEAK_UINT, .verdict = plugin::VERDICT_OK,
                                      .restart_only = 1, .restart_state = plugin::RESTART_NOT_DX11};
      tw::engine_tweaks::Option route_option = {.tweaks = {{.name = "SOUND_DEFAULT_BUFFER_SIZE_IN_KB", .integer = true}}};
      std::array<tw::engine_tweaks::Option*, 1> route_options = {&route_option};
      scale_block.tweaks = &route_slot;
      scale_block.tweak_count = 1;
      scale_block.tweaks_status.store(plugin::TWEAKS_READY);
      CHECK(link::FindTweaks(route_options));
      logged.clear();
      link::MirrorRestartValues(false, 1, true);
      CHECK(link::ShadowMapSizePending() && logged.empty());
      route_slot.restart_state = plugin::RESTART_APPLIED;
      link::MirrorRestartValues(false, 1, true);
      CHECK(!link::ShadowMapSizePending());
      // Faster Game CPU Code (its row offered; ShadowMapSize's key stays 1, the row's 1: no other line). This start read
      // the key absent (on), once: the row On writes nothing, not pending; Off writes 0 with one line, pending; a hand
      // edit after the read is not read again; On writes 1, not pending; not offered: nothing written, not pending.
      const std::string code_entry = "Attila64Plugin.Values|FasterGameCpuCode";
      const auto code_key = [&code_entry] {
        const auto found = config.find(code_entry);
        return (found == config.end() ? std::string("absent") : found->second);
      };
      // A start that reads the key anew (the detach keeps this start's key: reset by hand) with `text` in the file.
      const auto code_start = [&code_entry](const char* text) {
        link::Uninstall();
        link::internal::game_code_at_start.reset();
        link::internal::found.store(link::Found::READY);
        if (text == nullptr) {
          config.erase(code_entry);
        } else {
          config[code_entry] = text;
        }
        logged.clear();
      };
      scale_block.rows_applied |= plugin::RowBit(plugin::ROW_GAME_CODE);
      scale_block.rows_tried |= plugin::RowBit(plugin::ROW_GAME_CODE);
      logged.clear();
      link::MirrorRestartValues(false, 1, true);
      CHECK(code_key() == "absent" && logged.empty() && !link::GameCodePending()
            && link::internal::game_code_at_start == std::optional<bool>(true));
      link::MirrorRestartValues(false, 1, false);
      CHECK(code_key() == "0" && logged.size() == 1
            && lines("engine value FasterGameCpuCode: 0x0 at the next start (this start: 0x1)") == 1
            && link::GameCodePending() && !link::ShadowMapSizePending() && !link::SolFrameCapPending());
      config[code_entry] = "1";
      link::MirrorRestartValues(false, 1, false);
      CHECK(code_key() == "1" && logged.size() == 1 && link::GameCodePending());
      link::MirrorRestartValues(false, 1, true);
      CHECK(code_key() == "1" && logged.size() == 2
            && lines("engine value FasterGameCpuCode: 0x1 at the next start (this start: 0x1)") == 1
            && !link::GameCodePending());
      scale_block.rows_changed.store(plugin::RowBit(plugin::ROW_GAME_CODE));
      link::MirrorRestartValues(false, 1, false);
      CHECK(code_key() == "1" && logged.size() == 2 && !link::GameCodePending());
      scale_block.rows_changed.store(0);
      // The detach: mirrored and pending reset, this start's key kept (the next READY reads the file again, but not as
      // this start's): text the plugin reads as 0 is written again, against this start's On.
      link::MirrorRestartValues(false, 1, false);
      CHECK(link::GameCodePending());
      link::Uninstall();
      CHECK(!link::GameCodePending() && !link::internal::game_code_read && !link::internal::game_code_mirrored.has_value()
            && link::internal::game_code_at_start == std::optional<bool>(true));
      link::internal::found.store(link::Found::READY);
      config[code_entry] = "abc";
      logged.clear();
      link::MirrorRestartValues(false, 1, true);
      CHECK(code_key() == "1" && lines("engine value FasterGameCpuCode: 0x1 at the next start (this start: 0x1)") == 1
            && !link::GameCodePending());
      // Read as the plugin reads it (GetPrivateProfileIntW): text 0 (off), written again; 0x1 and empty on, written
      // again; a plain number left as it is when the row matches it (0 / Off, 1 / On: no line, not pending).
      code_start("abc");
      link::MirrorRestartValues(false, 1, true);
      CHECK(code_key() == "1" && lines("engine value FasterGameCpuCode: 0x1 at the next start (this start: 0x0)") == 1
            && link::GameCodePending());
      code_start("0x1");
      link::MirrorRestartValues(false, 1, true);
      CHECK(code_key() == "1" && lines("engine value FasterGameCpuCode: 0x1 at the next start (this start: 0x1)") == 1
            && !link::GameCodePending());
      code_start("");
      link::MirrorRestartValues(false, 1, true);
      CHECK(code_key() == "1" && logged.size() == 1 && !link::GameCodePending());
      code_start("0");
      link::MirrorRestartValues(false, 1, false);
      CHECK(code_key() == "0" && logged.empty() && !link::GameCodePending());
      link::MirrorRestartValues(false, 1, true);
      CHECK(code_key() == "1" && lines("engine value FasterGameCpuCode: 0x1 at the next start (this start: 0x0)") == 1
            && link::GameCodePending());
      code_start("1");
      link::MirrorRestartValues(false, 1, true);
      CHECK(code_key() == "1" && logged.empty() && !link::GameCodePending());
      code_start(nullptr);
      link::Uninstall();
      link::internal::block = nullptr;
      link::Install();
      std::printf(
          "7f game code: FasterGameCpuCode read once as the plugin reads it (absent / empty / 0x1: on, text: 0), text "
          "written again, one line per write, its own pending flag in both directions, left alone while its row is not "
          "offered; the detach resets the mirror and keeps this start's key\n");
      std::printf(
          "7f scale rows: a scale stored on a change, its bit while not neutral and its row offered (not offered: none; "
          "grass 19-21 by the grass row alone, campaign trees 18, sprites 22-23, a sprite scale not finite and above 0 sent as 0), "
          "one line per change of the bits, nothing on a v1 "
          "block; notices once per new bit; "
          "ShadowMapSize clamped, one line per write, its own pending flag against this start's key (out of range: the "
          "game's sizes; a NOT_DX11 start: 0), left alone while its row is not offered; the scales kept at the detach\n");
    }
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
