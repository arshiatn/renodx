// Output Auto's preset switch (twattila/output_encoding.hpp): a first start without Windows HDR takes the SDR preset, the
// Recommended preset once when Windows HDR comes on, the SDR preset again when it goes, and nothing after a row changed;
// a first start with Windows HDR on is never given a preset, nor one whose display query failed (SDR out, no SDR preset).
// Also the first start's key (tonemapper unless the game names its own), the D3D11 proxy's target beside each output, and
// an addon that never configures the output (its own target stays).
// A fake ReShade.ini, the real settings code, restarts between steps. `control`: the first start with Windows HDR on leaves no "off" mark; its sequence must then fail.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>

#include "src/games/twattila/output_encoding.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "tw_output_encoding";

static std::map<std::pair<std::string, std::string>, std::string> ini;
static std::string last_log;

extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { last_log += std::string(message) + "\n"; }
__declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char* section, const char* key, char* value, size_t* size) {
  const auto found = ini.find({section, key});
  if (found == ini.end()) return false;
  const size_t needed = found->second.size() + 1;
  if (value != nullptr && *size > 0) {
    const size_t copied = std::min(needed, *size) - 1;
    std::memcpy(value, found->second.data(), copied);
    value[copied] = '\0';
  }
  *size = needed;
  return true;
}
__declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char* section, const char* key, const char* value) {
  ini[{section, key}] = value;
}
}

namespace output = twattila::output_encoding;
namespace settings = renodx::utils::settings;

static float shader_encoding = -1.f;
static float tonemapper = 0.f;
static float bloom = 0.f;
static float video_autohdr = 0.f;
static int sdr_applied = 0;
static int recommended_applied = 0;

static settings::Settings rows = {
    new settings::Setting{.key = "tonemapper", .binding = &tonemapper, .value_type = settings::SettingValueType::INTEGER, .default_value = 1.f, .labels = {"SDR", "V30", "V31", "V35"}},
    new settings::Setting{.key = "bloom", .binding = &bloom, .default_value = 1.f, .max = 2.f},
    new settings::Setting{.key = "video_autohdr", .binding = &video_autohdr, .default_value = 0.f, .max = 1.f},
    new settings::Setting{.key = "SwapChainEncoding", .value_type = settings::SettingValueType::INTEGER, .default_value = 3.f, .labels = {"HDR10", "scRGB", "SDR", "Auto"}, .is_global = true},
};

static void SdrPreset() {
  ++sdr_applied;
  settings::UpdateSettings({{"tonemapper", 0.f}, {"bloom", 1.f}, {"video_autohdr", 0.f}});
}
static void RecommendedPreset() {
  ++recommended_applied;
  settings::UpdateSettings({{"tonemapper", 3.f}, {"bloom", 0.35f}, {"video_autohdr", 0.2f}});
}

static bool control = false;
constexpr output::WindowsHdr HDR_ON = {.known = true, .on = true};
constexpr output::WindowsHdr HDR_OFF = {.known = true, .on = false};
constexpr output::WindowsHdr NO_ANSWER = {.known = false, .on = true};  // no display path for the window (WindowsHdr's default)
// The display's advanced-colour query failed (Decide(HWND)'s hdr_of): the output falls to SDR, the look must not.
constexpr output::WindowsHdr QUERY_FAILED = {.known = false, .on = false};

// A game start: the addon loads its rows from the ini (settings::Use's order) and configures the output.
static void Start(int output_setting = output::AUTO, const char* first_start_key = output::Config{}.first_start_key) {
  output::decided.reset();
  output::first_start = false;
  for (auto* row : rows) row->Set(row->default_value);
  settings::LoadGlobalSettings();
  settings::LoadSettings(settings::global_name + "-preset1");
  output::Configure({
      .shader_encoding = &shader_encoding,
      .setting = output_setting,
      .first_start_without_hdr = &SdrPreset,
      .first_start_key = first_start_key,
      .recommended_preset = &RecommendedPreset,
  });
}

// What a D3D9 game's D3D11 proxy would present (utils::device_proxy's target, beside the swapchain mod's).
static bool ProxyTarget(reshade::api::format format, reshade::api::color_space color_space) {
  return renodx::utils::device_proxy::target_format == format && renodx::utils::device_proxy::target_color_space == color_space
         && renodx::mods::swapchain::target_format == format && renodx::mods::swapchain::target_color_space == color_space;
}

static void Fresh() {
  ini.clear();
  sdr_applied = 0;
  recommended_applied = 0;
  last_log.clear();
}

static std::string State() { return settings::ReadGlobalString(output::PRESET_SWITCH_KEY); }

static std::string Saved(const char* key) {
  const auto found = ini.find({settings::global_name + "-preset1", key});
  return (found == ini.end()) ? std::string("none") : found->second;
}

// The player moves a row on the page: the overlay saves every row of the preset at once.
static void PlayerSets(const char* key, float value) {
  settings::UpdateSetting(key, value);
  settings::SaveSettings(settings::global_name + "-preset1");
}

static int SdrFirstStart() {
  Fresh();
  Start();
  CHECK(output::first_start);
  output::Decide(HDR_OFF);
  CHECK(shader_encoding == static_cast<float>(output::SDR));
  CHECK(ProxyTarget(reshade::api::format::r10g10b10a2_unorm, reshade::api::color_space::srgb_nonlinear));
  CHECK(sdr_applied == 1 && recommended_applied == 0);
  CHECK(tonemapper == 0.f && Saved("tonemapper") == "0");
  CHECK(State().starts_with("sdr "));
  CHECK(last_log.find("the SDR preset applied") != std::string::npos);
  // A new swap chain in the same session, still without HDR: nothing.
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 1);

  Start();
  CHECK(!output::first_start);
  output::Decide(HDR_ON);
  CHECK(shader_encoding == static_cast<float>(output::SCRGB));
  CHECK(ProxyTarget(reshade::api::format::r16g16b16a16_float, reshade::api::color_space::extended_srgb_linear));
  CHECK(recommended_applied == 1 && sdr_applied == 1);
  CHECK(tonemapper == 3.f && Saved("tonemapper") == "3" && Saved("bloom") == "0.35");
  CHECK(State().starts_with("hdr "));
  CHECK(last_log.find("the Recommended preset applied") != std::string::npos);

  Start();
  output::Decide(HDR_ON);
  output::Decide(HDR_ON);
  CHECK(recommended_applied == 1 && sdr_applied == 1 && tonemapper == 3.f);
  // No answer from Windows: the output stays, and so does the look.
  output::Decide(NO_ANSWER);
  CHECK(recommended_applied == 1 && sdr_applied == 1);

  Start();
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 2 && recommended_applied == 1 && tonemapper == 0.f && Saved("tonemapper") == "0");
  CHECK(State().starts_with("sdr "));

  Start();
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 2 && recommended_applied == 1);
  // Windows HDR on within a session (a display change rebuilds the swap chain): the switch too.
  output::Decide(HDR_ON);
  CHECK(recommended_applied == 2 && tonemapper == 3.f);
  std::printf("PASS SDR first start -> HDR (Recommended once) -> HDR again (nothing) -> SDR (SDR preset) -> SDR (nothing)\n");
  return 0;
}

static int HdrFirstStart() {
  Fresh();
  Start();
  output::Decide(HDR_ON);
  CHECK(sdr_applied == 0 && recommended_applied == 0);
  CHECK(State() == "off");
  CHECK(Saved("tonemapper") == "none");
  if (control) ini.erase({settings::global_name, output::PRESET_SWITCH_KEY});

  Start();
  CHECK(output::first_start);  // still nothing saved
  output::Decide(HDR_OFF);
  CHECK(shader_encoding == static_cast<float>(output::SDR));
  CHECK(sdr_applied == 0 && recommended_applied == 0 && tonemapper == 1.f);

  Start();
  output::Decide(HDR_ON);
  CHECK(sdr_applied == 0 && recommended_applied == 0 && tonemapper == 1.f);
  CHECK(State() == "off");
  std::printf("PASS HDR first start -> SDR -> HDR (never a preset)\n");
  return 0;
}

static int PlayerChanged() {
  Fresh();
  Start();
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 1);
  PlayerSets("bloom", 0.5f);

  Start();
  output::Decide(HDR_ON);
  CHECK(recommended_applied == 0 && sdr_applied == 1 && bloom == 0.5f && tonemapper == 0.f);
  CHECK(State() == "off");
  CHECK(last_log.find("no automatic preset again") != std::string::npos);

  // Back to the preset's value: the key stays cleared.
  PlayerSets("bloom", 1.f);
  Start();
  output::Decide(HDR_OFF);
  Start();
  output::Decide(HDR_ON);
  CHECK(recommended_applied == 0 && sdr_applied == 1);
  std::printf("PASS SDR first start -> player changes a row -> HDR (nothing, key cleared for good)\n");

  // The change after the Recommended preset ends it too.
  Fresh();
  Start();
  output::Decide(HDR_OFF);
  Start();
  output::Decide(HDR_ON);
  CHECK(recommended_applied == 1);
  PlayerSets("video_autohdr", 0.5f);
  Start();
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 1 && video_autohdr == 0.5f && State() == "off");
  std::printf("PASS SDR first start -> HDR (Recommended) -> player changes a row -> SDR (nothing)\n");
  return 0;
}

static int OtherStarts() {
  // A player with saved rows from before (no key): never a preset.
  Fresh();
  ini[{settings::global_name + "-preset1", "tonemapper"}] = "2";
  Start();
  output::Decide(HDR_OFF);
  Start();
  output::Decide(HDR_ON);
  CHECK(sdr_applied == 0 && recommended_applied == 0 && tonemapper == 2.f && State().empty());

  // Output SDR or HDR10 chosen: no preset, at a first start either.
  Fresh();
  ini[{settings::global_name, "SwapChainEncoding"}] = "2";
  Start(output::SDR);
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 0 && State().empty());
  Start(output::HDR10);
  output::Decide(HDR_ON);
  CHECK(recommended_applied == 0 && State().empty());
  CHECK(ProxyTarget(reshade::api::format::r10g10b10a2_unorm, reshade::api::color_space::hdr10_st2084));

  // Windows gave no answer at the first start (HDR assumed): no decision yet, the next first start decides.
  Fresh();
  Start();
  output::Decide(NO_ANSWER);
  CHECK(sdr_applied == 0 && State().empty());
  Start();
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 1 && State().starts_with("sdr "));

  // The query failed at the first start (an HDR display: remote session, driver start): SDR out for that chain, but no
  // SDR preset and no decision; the next first start with Windows HDR on is never given one.
  Fresh();
  Start();
  output::Decide(QUERY_FAILED);
  CHECK(shader_encoding == static_cast<float>(output::SDR));
  CHECK(sdr_applied == 0 && recommended_applied == 0 && State().empty() && Saved("tonemapper") == "none");
  Start();
  CHECK(output::first_start);
  output::Decide(HDR_ON);
  CHECK(sdr_applied == 0 && recommended_applied == 0 && State() == "off");

  // A player who switched Output to SDR after the SDR first start: no switch while it is not Auto, the key stays.
  Fresh();
  Start();
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 1 && State().starts_with("sdr "));
  Start(output::SDR);
  output::Decide(HDR_ON);
  CHECK(recommended_applied == 0 && State().starts_with("sdr "));
  std::printf("PASS saved rows before, chosen Output, no answer and a failed query at the first start\n");
  return 0;
}

static int FirstStartKeys() {
  // The default key stays the Total War add-ons' tonemapper: another add-on's Rendering Mode row is no saved look.
  Fresh();
  ini[{settings::global_name + "-preset1", "rendering_mode"}] = "3";
  Start();
  CHECK(output::first_start);
  ini[{settings::global_name + "-preset1", "tonemapper"}] = "2";
  Start();
  CHECK(!output::first_start);

  // A game whose Rendering Mode row has its own key (R.U.S.E.'s rendering_mode): only that row's saved value counts.
  Fresh();
  ini[{settings::global_name + "-preset1", "tonemapper"}] = "2";
  Start(output::AUTO, "rendering_mode");
  CHECK(output::first_start);
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 1 && State().starts_with("sdr "));

  Fresh();
  ini[{settings::global_name + "-preset1", "rendering_mode"}] = "3";
  Start(output::AUTO, "rendering_mode");
  CHECK(!output::first_start);
  output::Decide(HDR_OFF);
  CHECK(sdr_applied == 0 && recommended_applied == 0 && State().empty());
  std::printf("PASS first-start key: tonemapper by default, a game's own key (rendering_mode)\n");
  return 0;
}

static int NotConfigured() {
  // An addon that never configures the output (Divinity 2 with device_loss): a new chain's Decide and a fallback chain's
  // Apply(SDR) leave the addon's HDR10 target. The same calls on a configured output change it (the check sees a change).
  Fresh();
  Start(output::HDR10);
  output::Decide(HDR_OFF);
  CHECK(renodx::mods::swapchain::target_color_space == reshade::api::color_space::srgb_nonlinear);
  output::config = {};
  output::decided.reset();
  renodx::mods::swapchain::SetUseHDR10(true);
  last_log.clear();
  output::Decide(HDR_OFF);
  output::Apply(output::SDR);
  CHECK(renodx::mods::swapchain::target_format == reshade::api::format::r10g10b10a2_unorm);
  CHECK(renodx::mods::swapchain::target_color_space == reshade::api::color_space::hdr10_st2084);
  CHECK(!output::decided.has_value() && last_log.empty());
  std::printf("PASS not configured: Decide and Apply leave the addon's own output\n");
  return 0;
}

int main(int argc, char** argv) {
  control = (argc > 1 && std::strcmp(argv[1], "control") == 0);
  settings::settings = &rows;
  if (control) {
    const int result = HdrFirstStart();
    std::printf("control: %s\n", (result == 0) ? "passed (the test cannot see the missing mark)" : "failed as it must");
    return result;
  }
  if (SdrFirstStart() != 0 || HdrFirstStart() != 0 || PlayerChanged() != 0 || OtherStarts() != 0 || FirstStartKeys() != 0
      || NotConfigured() != 0) {
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
