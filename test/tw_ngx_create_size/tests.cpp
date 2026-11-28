// DLSS Super Resolution's creation size (talesofarise_dlss/ngx_backend.hpp CreatesAtActive; critic X38): presets L / M
// above NVIDIA's optimal size are created at the active size (DLSS guide 3.2.2.1 (3): an input above the creation size
// reallocates), every other case keeps today's size. CONTROL = the size choice before X38 (the active size only outside
// NVIDIA's range): it must fail the L / M cases above the optimal.
#include <cstdint>
#include <cstdio>
#include "src/games/talesofarise_dlss/ngx_backend.hpp"
using talesofarise::dlss::CreatesAtActive;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
struct Case {
  const char* name;
  bool inside_range;
  uint32_t preset;
  uint32_t active_width;
  uint32_t active_height;
  bool expected_at_active;
};
int main() {
  int failures = 0;
  constexpr uint32_t L = NVSDK_NGX_DLSS_Hint_Render_Preset_L;
  constexpr uint32_t M = NVSDK_NGX_DLSS_Hint_Render_Preset_M;
  constexpr uint32_t K = NVSDK_NGX_DLSS_Hint_Render_Preset_K;
  constexpr uint32_t DEFAULT = NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
  // Quality's optimal size at 3840x2160 output (66.7 %).
  constexpr unsigned int OPTIMAL_WIDTH = 2560;
  constexpr unsigned int OPTIMAL_HEIGHT = 1440;
  constexpr Case CASES[] = {
      {.name = "L 77 %", .inside_range = true, .preset = L, .active_width = 2957, .active_height = 1663, .expected_at_active = true},
      {.name = "M 77 %", .inside_range = true, .preset = M, .active_width = 2957, .active_height = 1663, .expected_at_active = true},
      {.name = "L ~72 %", .inside_range = true, .preset = L, .active_width = 2765, .active_height = 1555, .expected_at_active = true},
      {.name = "M ~72 %", .inside_range = true, .preset = M, .active_width = 2765, .active_height = 1555, .expected_at_active = true},
      {.name = "L one row above", .inside_range = true, .preset = L, .active_width = 2560, .active_height = 1441, .expected_at_active = true},
      {.name = "L 66.7 %", .inside_range = true, .preset = L, .active_width = 2560, .active_height = 1440, .expected_at_active = false},
      {.name = "M 66.7 %", .inside_range = true, .preset = M, .active_width = 2560, .active_height = 1440, .expected_at_active = false},
      {.name = "L 64 % (below)", .inside_range = true, .preset = L, .active_width = 2458, .active_height = 1382, .expected_at_active = false},
      {.name = "K 77 %", .inside_range = true, .preset = K, .active_width = 2957, .active_height = 1663, .expected_at_active = false},
      {.name = "Default 77 %", .inside_range = true, .preset = DEFAULT, .active_width = 2957, .active_height = 1663, .expected_at_active = false},
      {.name = "L outside (33 %)", .inside_range = false, .preset = L, .active_width = 1268, .active_height = 713, .expected_at_active = true},
      {.name = "K outside (33 %)", .inside_range = false, .preset = K, .active_width = 1268, .active_height = 713, .expected_at_active = true},
  };
  int control_misses = 0;
  for (const auto& test : CASES) {
    const bool at_active = CreatesAtActive(test.inside_range, test.preset, test.active_width, test.active_height,
                                           OPTIMAL_WIDTH, OPTIMAL_HEIGHT);
    const uint32_t created_width = (at_active ? test.active_width : OPTIMAL_WIDTH);
    const uint32_t created_height = (at_active ? test.active_height : OPTIMAL_HEIGHT);
    const bool control_at_active = !test.inside_range;
    const uint32_t control_width = (control_at_active ? test.active_width : OPTIMAL_WIDTH);
    const uint32_t control_height = (control_at_active ? test.active_height : OPTIMAL_HEIGHT);
    const bool lm = test.preset == L || test.preset == M;
    std::printf("%-18s created %ux%u (control %ux%u)\n", test.name, created_width, created_height, control_width,
                control_height);
    CHECK(at_active == test.expected_at_active);
    // The rule itself: no L / M input above its creation size; every other preset keeps today's size.
    if (lm) {
      CHECK(test.active_width <= created_width && test.active_height <= created_height);
    } else {
      CHECK(created_width == control_width && created_height == control_height);
    }
    if (lm && (test.active_width > control_width || test.active_height > control_height)) ++control_misses;
  }
  // The control must fail: today's choice leaves L / M above the optimal reallocating (5 cases).
  std::printf("control: %d L / M cases above the creation size\n", control_misses);
  CHECK(control_misses == 5);
  std::printf(failures == 0 ? "PASS\n" : "FAILED %d\n", failures);
  return failures == 0 ? 0 : 1;
}
