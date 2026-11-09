#include <windows.h>

#include <detours.h>
#include <cstdio>
#include <stdexcept>

static int fail_stage = 0;
static int detach_calls = 0;
static LONG WINAPI TestBegin() {
  return fail_stage == 1 ? ERROR_INVALID_OPERATION : DetourTransactionBegin();
}
static LONG WINAPI TestDetach(PVOID* original, PVOID replacement) {
  if (fail_stage == 2 && ++detach_calls == 2) return ERROR_INVALID_OPERATION;
  return DetourDetach(original, replacement);
}
static LONG WINAPI TestCommit() {
  if (fail_stage == 3) {
    DetourTransactionAbort();
    return ERROR_INVALID_OPERATION;
  }
  return DetourTransactionCommit();
}
#define DetourTransactionBegin  TestBegin
#define DetourDetach            TestDetach
#define DetourTransactionCommit TestCommit
#include "src/games/twattila/graphics_hooks.hpp"
#undef DetourTransactionBegin
#undef DetourDetach
#undef DetourTransactionCommit

namespace graphics = twattila::graphics_hooks::internal;
extern "C" __declspec(dllexport) int InstallTest(void* distances, void* scrub) {
  if (!graphics::hooks.empty()) return -1;
  graphics::original_unit_distances = reinterpret_cast<graphics::UnitDistances>(distances);
  graphics::original_draw_scrub = reinterpret_cast<graphics::DrawScrub>(scrub);
  graphics::hooks = {
      {&graphics::original_unit_distances, &graphics::OnUnitDistances},
      {&graphics::original_draw_scrub, &graphics::OnDrawScrub},
  };
  for (auto& scale : graphics::imposter_scales) scale.store(2.f);
  graphics::hook_lifetime.Install(graphics::hooks);
  graphics::hooks_active = true;
  return 1;
}
extern "C" __declspec(dllexport) int RemoveTest(int stage) {
  fail_stage = stage;
  detach_calls = 0;
  twattila::graphics_hooks::Uninstall();
  fail_stage = 0;
  return static_cast<int>(graphics::hooks.size());
}
extern "C" __declspec(dllexport) int ReactivateTest() {
  bool installed = false;
  twattila::graphics_hooks::Update({}, &installed, nullptr);
  return installed && graphics::hooks_active.load() && graphics::hooks.size() == 2;
}
#if !defined(_WIN64)
namespace lod = tw::engine_tweaks::lod_pick::internal;
extern "C" __declspec(dllexport) int InstallLod(void* target) {
  lod::original_pick_lod = reinterpret_cast<lod::PickLod>(target);
  lod::hooks = {{&lod::original_pick_lod, &lod::OnPickLod}};
  lod::hook_lifetime.Install(lod::hooks);
  lod::distance_squared_scale = 2.f;
  lod::hooks_active = true;
  return 1;
}
extern "C" __declspec(dllexport) int RemoveLod(int stage) {
  fail_stage = stage;
  detach_calls = 1;  // The LOD owner has just one hook.
  tw::engine_tweaks::Uninstall();
  fail_stage = 0;
  return static_cast<int>(lod::hooks.size());
}
#endif
extern "C" __declspec(dllexport) constexpr const char* NAME = "Native hook lifetime test";
