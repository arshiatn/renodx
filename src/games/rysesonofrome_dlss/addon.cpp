/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <d3d11_1.h>
#include <wrl/client.h>

#include <embed/shaders.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "../../utils/command_action.hpp"
#include "../../utils/data.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/png.hpp"
#include "../../utils/state.hpp"
#include "./constant_mirror.hpp"
#include "./engine_memory.hpp"
#include "./native_state.hpp"
#include "./ngx_backend.hpp"
#include "./performance.hpp"

static_assert(_ITERATOR_DEBUG_LEVEL == 0, "DLSS must match the shared STL layout of Release RenoDX addons");

namespace {

using Microsoft::WRL::ComPtr;
namespace command_action = renodx::utils::command_action;

// CryEngine's TAA (SMAA 1TX style): t2 linear depth, t3 object velocity, t4 color, t5 history, PER_BATCH
// b0 (mViewProjPrev, WorldViewPos, PS_NearFarClipDist, PS_ScreenSize). After the tonemap and SMAA, before post and
// the native upscale; drawn at the Rendering Resolution (below).
constexpr uint32_t TAA_SHADER = 0x121C45E4;
// The game's SMAA right before its TAA (the spatial part of r_PostAAEdgeFilter 3, SMAA 2TX): edge detection, blend
// weights, neighborhood blending (t1: the frame). While DLSS runs, DLSS gets the frame without SMAA's edge blur: the
// first two are skipped, the blend becomes a copy of its color input (0x52594503).
constexpr uint32_t SMAA_EDGES_SHADER = 0x15D2F34D;
constexpr uint32_t SMAA_WEIGHTS_SHADER = 0x5DC96409;
constexpr uint32_t SMAA_BLEND_SHADER = 0xF2D692D1;

// After the TAA: the post pass (sharpening, film grain; lens flares t5 in the second two; t0: the TAA's result) and,
// below 100% Rendering Resolution, the native upscale (t0: the post's result, into the back buffer; the UI follows at
// the display size). DLSS upscaling: the post pass is drawn again at the display size from NVIDIA's result, and the
// native upscale copies that 1:1 instead.
constexpr std::array<uint32_t, 4> POST_SHADERS = {0xD0B247FF, 0xB022AFEA, 0xC3A469C1, 0xCC6DF78E};
constexpr uint32_t UPSCALE_SHADER = 0xCD8548F6;

// The game's Rendering Resolution (its menu: 50-100% in steps of 10; the cvars take any size): r_Width / r_Height with
// Native Upscaling (r_FullscreenNativeRes). The game renders into targets of that size; its native upscale stretches
// the result to the back buffer. The renderer (the object RT_RenderScene's start passes, lea at 20) keeps the cvars
// (ICVar*) at +0xb828 / +0xb830 / +0xf540; its display check reads them every frame and resizes the targets.
// ICVar: GetIVal vtable +0x10, Set(int) +0x38. (v13-v19 used the game's dynamic resolution, osm_*, which renders
// into a corner of full-size targets: the PC game's motion blur, depth of field and particles break with it.)
constexpr std::string_view RENDER_SCENE =
    "48 8B 05 ?? ?? ?? ?? 44 8B F2 48 8B 15 ?? ?? ?? ?? 4C 8B E1 48 8D 0D ?? ?? ?? ?? 4D 8B F9 49 8B F0 FF 90 50 0C 00 00";
constexpr size_t WIDTH_CVAR = 0xb828;
constexpr size_t HEIGHT_CVAR = 0xb830;
constexpr size_t NATIVE_RES_CVAR = 0xf540;
constexpr size_t CVAR_GET_INT = 0x10;
constexpr size_t CVAR_SET_INT = 0x38;
// Presents a mode's resolution stays unchanged before it is set (the Custom slider; each change resizes the targets).
constexpr uint32_t SETTLE_PRESENTS = 30;
// The game renders smaller Rendering Resolutions at this size (v20: 228x128 rendered at 512x300).
constexpr uint32_t MIN_RENDER_WIDTH = 512;
constexpr uint32_t MIN_RENDER_HEIGHT = 300;

// Values stay as saved in ReShade.ini; new modes are appended.
enum class AAMode { OFF,
                    DLAA,
                    DLSS_QUALITY,
                    DLSS_BALANCED,
                    DLSS_PERFORMANCE,
                    DLSS_ULTRA_PERFORMANCE,
                    DLSS_ULTRA_QUALITY,
                    DLSS_CUSTOM };

struct ModeSettings {
  const char* label;
  const char* name;
  float scale;  // render / display; DLSS Custom: its slider
  rysesonofrome::dlss::Mode backend;
};

constexpr std::array<ModeSettings, 8> AA_MODES = {{
    {.label = "Off (game TAA)", .name = "Off", .scale = 1.f, .backend = rysesonofrome::dlss::Mode::DLAA},
    {.label = "DLAA (100%)", .name = "DLAA", .scale = 1.f, .backend = rysesonofrome::dlss::Mode::DLAA},
    {.label = "DLSS Quality (67%)", .name = "DLSS Quality", .scale = 2.f / 3.f, .backend = rysesonofrome::dlss::Mode::DLSS_QUALITY},
    {.label = "DLSS Balanced (58%)", .name = "DLSS Balanced", .scale = .58f, .backend = rysesonofrome::dlss::Mode::DLSS_BALANCED},
    {.label = "DLSS Performance (50%)", .name = "DLSS Performance", .scale = .5f, .backend = rysesonofrome::dlss::Mode::DLSS_PERFORMANCE},
    {.label = "DLSS Ultra Performance (33%)", .name = "DLSS Ultra Performance", .scale = 1.f / 3.f,
     .backend = rysesonofrome::dlss::Mode::DLSS_ULTRA_PERFORMANCE},
    // Not an NVIDIA SDK mode: the common 77%, created on NVIDIA's Quality model.
    {.label = "DLSS Ultra Quality (77%)", .name = "DLSS Ultra Quality", .scale = .77f, .backend = rysesonofrome::dlss::Mode::DLSS_ULTRA_QUALITY},
    {.label = "DLSS Custom (14-100%)", .name = "DLSS Custom", .scale = .5f, .backend = rysesonofrome::dlss::Mode::DLSS_CUSTOM},
}};

// Menu order; AAMode values stay as saved in ReShade.ini.
constexpr std::array<AAMode, 8> MENU_ORDER = {
    AAMode::OFF, AAMode::DLAA, AAMode::DLSS_ULTRA_QUALITY, AAMode::DLSS_QUALITY, AAMode::DLSS_BALANCED,
    AAMode::DLSS_PERFORMANCE, AAMode::DLSS_ULTRA_PERFORMANCE, AAMode::DLSS_CUSTOM};

struct PresetSettings {
  uint32_t value;
  const char* label;
};

constexpr std::array<PresetSettings, 4> PRESETS = {{
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_E, .label = "E (CNN, Fastest, Worst looking, not recommended)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_K, .label = "K (Transformer 1, Slower but better than E)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_M, .label = "M (Transformer 2, recommended, default)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_L, .label = "L (Transformer 2, Slower but better than M)"},
}};

struct Texture {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

// One mip; shader resource plus `bind_flags` (render target, unordered access), each with its view.
bool CreateTexture(ID3D11Device* device, DXGI_FORMAT format, uint32_t bind_flags, uint32_t width, uint32_t height, Texture* texture) {
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = width,
      .Height = height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = format,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | bind_flags,
  };
  return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->resource.GetAddressOf()))
         && SUCCEEDED(device->CreateShaderResourceView(texture->resource.Get(), nullptr, texture->srv.GetAddressOf()))
         && ((bind_flags & D3D11_BIND_RENDER_TARGET) == 0
             || SUCCEEDED(device->CreateRenderTargetView(texture->resource.Get(), nullptr, texture->rtv.GetAddressOf())))
         && ((bind_flags & D3D11_BIND_UNORDERED_ACCESS) == 0
             || SUCCEEDED(device->CreateUnorderedAccessView(texture->resource.Get(), nullptr, texture->uav.GetAddressOf())));
}

// CryEngine's jitter: the renderer's sub-pixel offset (NDC, at +0xcd0) is added to the projection's m20 / m21 while
// r_PostAAMode > 0 (the game sets 1, 2x, from its settings; the addon writes GAME_JITTER_MODE while DLSS is on). Its
// setup zeroes the offset, then computes one only while the view allows it, r_PostAAEdgeFilter is 3, every
// time-scale channel runs at 1 or more (no slow motion, hit stop or pause) and a frame counter moved (frozen while
// paused). With the addon's own jitter, a jump at the setup's start (0) goes to a stub that keeps the view's flag,
// stores the addon's offset and continues where the game's "no jitter" path does (the je at 19).
constexpr std::string_view JITTER_SETUP =
    "0F B6 8D B0 00 00 00 33 C0 49 89 84 24 D0 0C 00 00 84 C9 0F 84 ?? ?? ?? ?? 39 05 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? "
    "84 DB 0F 84 ?? ?? ?? ??";
constexpr size_t STUB_OFFSET = 64;  // the stub's offset value (two floats), after its code
// Where the game adds the offset to a camera's projection, if r_PostAA is on, the scene allows post processing,
// the recursion level is 1 or lower, it is no render-to-texture / shadow pass, stereo is off, r_PostAAMode > 0 and
// bit 27 of the renderer's per-frame flags (+0x4524) is clear: set while the game is paused. With the addon's own
// jitter that last check's "and cl, al" (at 84) becomes two nops: the per-camera checks stay.
constexpr std::string_view JITTER_APPLY =
    "83 3D ?? ?? ?? ?? 00 74 21 F6 87 88 52 00 00 20 74 18 48 63 87 A0 37 00 00 48 8D 0D ?? ?? ?? ?? 83 3C 81 01 "
    "7F 04 B2 01 EB 02 32 D2 48 63 87 A0 37 00 00 48 8D 0C 40 8B 87 24 45 00 00 48 C1 E1 08 F7 84 39 A8 37 00 00 "
    "00 00 11 00 0F 94 C1 C1 E8 1B F6 D0 22 C8 83 BF B8 5C 00 00 00 0F 94 C0 22 C8 83 3D ?? ?? ?? ?? 00 0F 9F C0 "
    "22 C8 22 CA F6 C1 01 74 30";
constexpr size_t PAUSE_FLAG_CHECK = 84;
constexpr int GAME_JITTER_MODE = 6;  // r_PostAAMode: Halton 8x
// The addon's own jitter: Halton (2, 3), NVIDIA's at least 8 positions per (output / input)^2; DLAA: 8.
constexpr uint32_t JITTER_PHASES = 8;
constexpr uint32_t MAX_JITTER_PHASES = 1 << 16;

// Must match PassConstants (b12) in the private shaders.
struct PassConstants {
  uint32_t color_encoding;  // 0: the view decodes sRGB, 1: stored values are encoded (gamma 2.2 round trip)
  uint32_t has_camera;      // the current camera below (else motion from the TAA's texture coordinates)
  std::array<uint32_t, 2> padding;
  // This frame's camera (g_VS_ViewProjMatr) as world -> uv: x, y and w coefficients (world x, y, z, 1).
  std::array<std::array<float, 4>, 3> current_uv;
};
static_assert(sizeof(PassConstants) == 64);

struct Runtime {
  rysesonofrome::dlss::Backend backend;
  rysesonofrome::dlss::Performance performance;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11PixelShader> prepare_shader;
  ComPtr<ID3D11PixelShader> copy_shader;
  ComPtr<ID3D11PixelShader> smaa_shader;
  ComPtr<ID3D11VertexShader> fullscreen_shader;
  ComPtr<ID3D11PixelShader> capture_shader;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11Buffer> capture_constants;
  Texture depth;
  Texture motion;
  Texture color;
  Texture output;  // NVIDIA's result: render size (DLAA) or the display size (upscaling)
  ComPtr<ID3D11Texture2D> exposure;  // 1x1 R32_FLOAT 1: the input is already exposed (the game's tonemap)
  // Upscaling, at the display size: NVIDIA's result encoded like the game's TAA target (its format and view; the
  // view the post pass reads), and the post pass drawn from it (its target's format) for the native upscale.
  Texture taa_full;
  Texture post_full;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  uint64_t last_frame = UINT64_MAX;
};

// Evaluation runs on the render thread, Reset from swap chain events and Present.
std::mutex runtime_mutex;
Runtime runtime;

struct State {
  std::shared_mutex mutex;
  reshade::api::device* device = nullptr;
  uint64_t frame = 0;
  uint64_t claimed_frame = UINT64_MAX;
  uint64_t completed = 0;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  uint32_t display_width = 0;  // the back buffer
  uint32_t display_height = 0;
  uint32_t output_width = 0;  // NVIDIA's output at the last success
  uint32_t output_height = 0;
  AAMode last_mode = AAMode::OFF;
  rysesonofrome::dlss::PerformanceSummary performance;
  std::string status = "Off — original game TAA";
} state;

std::atomic<AAMode> requested_mode = AAMode::OFF;
std::atomic<uint32_t> preset = NVSDK_NGX_DLSS_Hint_Render_Preset_M;  // L looks the same in Ryse and costs more
rysesonofrome::dlss::DlssLibrary dlss_library;
bool advanced = false;
// The addon's own jitter (JITTER_SETUP, JITTER_APPLY), or the game's (stops in slow motion and pauses).
std::atomic_bool own_jitter = true;
// Percent; below 14 the game renders at its minimum anyway (512x300; 544x306 at 4K in the display's ratio).
constexpr float MIN_CUSTOM_SCALE = 14.f;
std::atomic<float> custom_scale = 50.f;
// The frame whose TAA DLSS upscaled, whose post pass was drawn again at the display size, and whose native upscale
// copied that (shown); a frame upscaled but not shown shows the game's own (soft) frame.
std::atomic<uint64_t> upscaled_frame = UINT64_MAX;
std::atomic<uint64_t> post_drawn_frame = UINT64_MAX;
std::atomic<uint64_t> shown_frame = UINT64_MAX;
std::atomic<uint64_t> unshown_frames = 0;
// The last frame DLSS ran at the game's TAA: SMAA is skipped only while DLSS runs (never before a stock TAA alone).
std::atomic<uint64_t> last_dlss_frame = UINT64_MAX;
// The game's own Rendering Resolution while the addon sets it (ReShade.ini; 0: not set), restored on the next start
// if the game ended with the addon's.
std::array<int, 2> saved_game_size = {};
std::atomic<uint64_t> overlay_frame = 0;
std::string saved_image;  // debug: where the game's image was saved (overlay only)
// Debug (Advanced button): one frame's passes in ReShade.log: fullscreen-sized draws (6 vertices or fewer) before the
// TAA, every draw after it, and copies; with their shaders, first target, viewport and pixel shader inputs t0-t7.
// The frame after the first upscaled one is traced once per session.
std::atomic_bool trace_requested = false;
std::atomic_bool upscale_traced = false;
std::atomic<uint64_t> trace_frame = UINT64_MAX;
std::atomic<uint64_t> taa_frame = UINT64_MAX;  // the last frame whose TAA draw was seen
std::atomic<uint32_t> trace_lines = 0;
// Debug (Advanced button): the stages of the next upscaled frame (DLSS input, motion, output, NVIDIA's result as the
// post pass reads it, the lens flares, the post pass at the display size) and of the frame after it (input, output, lens
// flares, post pass: what changes between two frames), saved as PNGs next to the exe at the second one's Present.
std::atomic_bool capture_requested = false;
std::atomic<uint64_t> capture_frame = UINT64_MAX;
struct Capture {
  std::string name;
  Texture texture;  // 8-bit copy
};
std::vector<Capture> captures;  // under runtime_mutex
std::string capture_result;     // Present thread (overlay)
// The game's console. Its cvar whitelist rejects all but a few cvars from config files ("Unknown command"); the
// others are registered and can be set in code. The console (gEnv->pConsole, null until the game creates it) and
// con_restricted come from con_restricted's registration (CXConsole::Init: mov rcx, [gEnv->pConsole] ... mov r9d,
// [con_restricted] (its default: the current value)). IConsole::GetCVar vtable +0xb0. ICVar: GetIVal +0x10, GetFVal
// +0x20, GetString +0x28, Set(int) +0x38, Set(float) +0x40, GetFlags +0x60.
constexpr std::string_view CONSOLE_REGISTRATION =
    "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 44 8B 0D ?? ?? ?? ?? C6 44 24 38 01 48 8D 15 ?? ?? ?? ?? 48 89 7C 24 30 "
    "4C 8D 05 ?? ?? ?? ?? 48 89 54 24 28 48 8D 15 ?? ?? ?? ?? C7 44 24 20 00 00 08 00";
constexpr size_t CONSOLE_GET_CVAR = 0xb0;
constexpr size_t CVAR_GET_FLOAT = 0x20;
constexpr size_t CVAR_GET_STRING = 0x28;
constexpr size_t CVAR_SET_FLOAT = 0x40;
constexpr size_t CVAR_GET_FLAGS = 0x60;
struct GameConsole {
  bool searched = false;
  void* const* console = nullptr;  // gEnv->pConsole
  const int* restricted = nullptr;
  std::string details;  // where, or why not; for the log
} game_console;  // Present thread (the overlay runs in it)
// The vegetation groups' sprite distance (StatInstGroup update +0x16EB00) is x the render width / 1024, or x
// e_ForceDetailLevelForScreenRes / 1024 when that is set (0: the current width). DLSS below the display width moved
// trees to their sprites (which cast no shadows) sooner: shadows ended closer at every smaller scale. While the addon
// renders below the display width it pins the cvar to the display width. Written in memory (3D engine cvars +0x3f0,
// read by the code below): its ICVar refuses sets (flags 0x800000, like e_DecalsRange).
constexpr std::string_view VEGETATION_SCREEN_RES =
    "48 8B 15 ?? ?? ?? ?? 48 69 DB 08 03 00 00 8B 82 F0 03 00 00 49 03 D8 85 C0 75 ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 00 02 00 00";
constexpr size_t SCREEN_RES_CVAR = 0x3f0;
struct ScreenResPin {
  bool searched = false;
  uint8_t* const* cvars = nullptr;  // the 3D engine's cvars object
  std::string details;              // where, or why not; for the log
  bool set = false;
  int game_value = 0;
} screen_res_pin;  // Present thread
// Debug (Advanced button): every console variable with its value and flags, in ryse_cvars.txt next to the exe. The
// console's name -> ICVar* map, an MSVC std::map: node _Left +0, _Parent +8, _Right +0x10, key +0x18, ICVar* +0x20,
// _Isnil +0x29; the head's _Left is the first node.
constexpr size_t CONSOLE_VARIABLES = 0xf8;
std::string saved_cvars;  // overlay only

// Game settings above its highest. Its menu offers Low / Medium / High; its 14 sys_spec groups at 4 (very high, also
// in autoexec.cfg) go one level further. The whitelist rejects the other cvars below in config files, so the addon
// sets them through the console. A choice multiplies the in-game value (as its spec set it, read again whenever the
// game sets one itself) or, absolute, replaces it; the first item (the in-game setting) restores it. Applied once the
// game draws its world (its startup spec is set by then). Buttons: Reset (the in-game settings),
// GRAPHICS_PRESETS (Very High, Ultra, Extreme, Insane), MAXED OUT (each option's last item).
struct QualityCvar {
  const char* name;
  bool is_float;
  bool inverse = false;   // a factor divides it (smaller is finer)
  float max_value = 0.f;  // 0: none (D3D11 textures: 16384)
  float min_value = 0.f;  // 0: none
  bool absolute = false;  // `values` per item instead of the option's (the first item: the in-game setting)
  std::array<float, 10> values = {};
  // Non-zero: written in the 3D engine's cvars object at this offset (EngineCvars), for VF_CHEAT cvars whose console
  // sets the game puts back. `cvar` then points at the value.
  size_t engine_offset = 0;
  int from_choice = 0;  // the option's items below this one keep the in-game value
  void* cvar = nullptr;  // ICVar*
  bool looked_up = false;
  float game_value = 0.f;  // while set
  float requested = 0.f;
  float set_value = 0.f;  // as read back
  bool set = false;
  int reverts = 0;  // the game put its value back right after a set (VF_CHEAT cvars, every frame)
};
// Reverts after which a cvar is left alone (refused).
constexpr int CVAR_REFUSED_REVERTS = 3;
// The buttons between Reset (the in-game settings) and MAXED OUT (each option's last item).
struct GraphicsPreset {
  const char* name;
  const char* tooltip;
};
constexpr std::array<GraphicsPreset, 4> GRAPHICS_PRESETS = {{
    {.name = "Very High", .tooltip = "The graphics preset at Very High, the other options at about 2x."},
    {.name = "Ultra", .tooltip = "Distances and details 8x, shadow resolution 2x, shadow pool 2x, 7 sun cascades, water 2x, sharper textures, model streaming 8x."},
    {.name = "Extreme", .tooltip = "Distances and details 16x, shadow resolution 2x, shadow pool 4x, 7 sun cascades, water 2x, sharper textures, model streaming 16x."},
    {.name = "Insane", .tooltip = "Distances and details 32x, shadow resolution 4x, shadow pool 4x, 8 sun cascades, water 2x, sharpest textures, model streaming 32x."},
}};
struct QualityOption {
  const char* label;
  const char* key;  // ReShade.ini, section "Ryse Game Settings"
  const char* tooltip;
  const char* choices;  // combo items; the first: the in-game setting; the last: MAXED OUT's
  int choice_count;
  std::array<int, GRAPHICS_PRESETS.size()> preset_choices;  // per GRAPHICS_PRESETS button
  std::array<float, 10> values;  // per item: a factor of the in-game value or, absolute, the value
  bool absolute = false;
  int floor_option = -1;  // index of an option whose factor this one's never goes below (-1: none)
  std::vector<QualityCvar> cvars;
  int choice = 0;  // Present thread (the overlay runs in it)
};
// Cvars that read back unchanged are refused (e.g. e_DecalsRange, flags 0x800000; left out). Past the engine's caps
// (the camera's far plane, LOD 0, the tessellation factor 64, every texture resident) higher factors change nothing.
std::array<QualityOption, 13> quality_options = {{
    {.label = "Graphics preset (in-game)",
     .key = "EngineQuality",
     .tooltip = "The game's own graphics preset (its 14 quality groups, sys_spec_*, as in autoexec.cfg).\n"
                "The game's menu goes up to High; Very High is one level above it, the highest the engine has.",
     .choices = "In-game setting\0" "High (the game's highest)\0" "Very High (above the game's High)\0",
     .choice_count = 3,
     .preset_choices = {2, 2, 2, 2},
     .values = {0.f, 3.f, 4.f},
     .absolute = true,
     .cvars = {{.name = "sys_spec_Shading", .is_float = false}, {.name = "sys_spec_Shadows", .is_float = false},
               {.name = "sys_spec_ObjectDetail", .is_float = false}, {.name = "sys_spec_Particles", .is_float = false},
               {.name = "sys_spec_Texture", .is_float = false}, {.name = "sys_spec_TextureResolution", .is_float = false},
               {.name = "sys_spec_Water", .is_float = false}, {.name = "sys_spec_Light", .is_float = false},
               {.name = "sys_spec_PostProcessing", .is_float = false}, {.name = "sys_spec_VolumetricEffects", .is_float = false},
               {.name = "sys_spec_GameEffects", .is_float = false}, {.name = "sys_spec_Physics", .is_float = false},
               {.name = "sys_spec_Animation", .is_float = false}, {.name = "sys_spec_Sound", .is_float = false}}},
    {.label = "Shadow resolution",
     .key = "ShadowResolution",
     .tooltip = "Shadow map resolution of the sun, of lights and of characters (per-object shadows)\n"
                "(e_ShadowsMaxTexRes 2048 at Very High, e_ShadowsResScale, e_ShadowsPerObjectTexRes 4096,\n"
                "up to 8192: above it, bands on characters). The light shadows' pool grows with it (Shadow\n"
                "pool). The heaviest option. 4x is the maximum: the lights' maps then fill about 3/4 of the\n"
                "largest pool; 8x would not fit.",
     .choices = "In-game setting\0" "2x (4096)\0" "4x (8192, the maximum)\0",
     .choice_count = 3,
     .preset_choices = {1, 1, 1, 2},
     .values = {1.f, 2.f, 4.f},
     // e_ShadowsMaxTexRes also sizes the lights' maps in the pool: at 4x the hall's 39 maps fill 74% of the 16384
     // pool (shadow trace); 8x would need four times that.
     .cvars = {{.name = "e_ShadowsMaxTexRes", .is_float = false, .max_value = 8192.f},
               {.name = "e_ShadowsResScale", .is_float = true},
               // Above 8192 characters showed shadow bands (tester, 4x / 8x): likely self-shadowing, the game's bias
               // tuned for its own sizes with 16-bit shadow depth (r_ShadowTexFormat 5).
               {.name = "e_ShadowsPerObjectTexRes", .is_float = false, .max_value = 8192.f}}},
    // The atlas the lights' shadow maps share. Fewer fit than cast shadows: the engine swaps which get one, frame to
    // frame (flicker). Never below Shadow resolution's factor, so its larger light maps still fit.
    {.label = "Shadow pool",
     .key = "ShadowPool",
     .tooltip = "Room for the shadows of lights such as torches and fires (e_ShadowsPoolSize, 4096 at Very\n"
                "High). When more lights cast shadows than fit, their shadows flicker. Grows with Shadow\n"
                "resolution at least. 16384 is the D3D11 maximum (a 16-bit 16384 x 16384 map, about 512 MB).",
     .choices = "Matches Shadow resolution\0" "2x (8192)\0" "4x (16384, the maximum)\0",
     .choice_count = 3,
     .preset_choices = {0, 1, 2, 2},
     .values = {1.f, 2.f, 4.f},
     .floor_option = 1,
     .cvars = {{.name = "e_ShadowsPoolSize", .is_float = false, .max_value = 16384.f}}},
    {.label = "Shadow cascades",
     .key = "ShadowCascades",
     .tooltip = "Number of sun shadow cascades (e_GsmLodsNum, 5 at Very High; each covers 3x the\n"
                "distance of the one before): more of them keep distant shadows sharp. 8 is CryEngine's most.\n"
                "Every cascade is also drawn again every frame (e_GsmCacheLodOffset, 3: the farther ones come\n"
                "from a cache updated less often), so far shadows follow moving things.",
     .choices = "In-game setting\0" "6\0" "7\0" "8 (the maximum)\0",
     .choice_count = 4,
     .preset_choices = {1, 2, 2, 3},
     .values = {0.f, 6.f, 7.f, 8.f},
     .absolute = true,
     // "Makes first X GSM lods not cached" (its help text): X = the cascades, none cached.
     .cvars = {{.name = "e_GsmLodsNum", .is_float = false}, {.name = "e_GsmCacheLodOffset", .is_float = false}}},
    {.label = "Shadow distance",
     .key = "ShadowDistance",
     .tooltip = "How far objects cast shadows (e_ShadowsCastViewDistRatio).",
     .choices = "In-game setting\0" "1.5x\0" "2x\0" "4x\0" "8x\0" "16x\0" "32x\0" "64x (the maximum)\0",
     .choice_count = 8,
     .preset_choices = {2, 4, 5, 6},
     .values = {1.f, 1.5f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f},
     .cvars = {{.name = "e_ShadowsCastViewDistRatio", .is_float = true}}},
    {.label = "Draw distance",
     .key = "DrawDistance",
     .tooltip = "How far objects, characters, vegetation, grass and details are drawn, how far grass is\n"
                "loaded, how far objects show in reflections (water), how far plants move in strong wind, and\n"
                "the pools that hold the grass and vegetation (e_ViewDistRatio*, e_ViewDistRatioCustom,\n"
                "e_ViewDistMin up to 20, e_MergedMeshes*, e_VegetationSpritesDistanceRatio, e_ProcVegetation*,\n"
                "e_RecursionViewDistRatio 0.1 up to 1, e_FoliageWindActivationDist 25 m; pools, grass loading\n"
                "and wind up to 16x). 64x is the maximum: measured, more draws nothing more.\n"
                "Lights stay at the game's distance: farther ones flickered.",
     .choices = "In-game setting\0" "1.5x\0" "2x\0" "3x\0" "4x\0" "8x\0" "16x\0" "32x\0" "64x (the maximum)\0",
     .choice_count = 9,
     .preset_choices = {2, 5, 6, 7},
     .values = {1.f, 1.5f, 2.f, 3.f, 4.f, 8.f, 16.f, 32.f, 64.f},
     .cvars = {{.name = "e_ViewDistRatio", .is_float = true}, {.name = "e_ViewDistRatioVegetation", .is_float = true},
               {.name = "e_ViewDistRatioDetail", .is_float = true},
               // "View distance ratio for special marked objects (Players,AI,Vehicles)" (its help text).
               {.name = "e_ViewDistRatioCustom", .is_float = true},
               // Also every light's minimum view distance (CLightEntity::GetMaxViewDist: max(e_ViewDistMin, size x
               // e_ViewDistRatioLights x lod ratio), dump +0x609D1): at 2560 every light in the level was active and
               // lights and their shadows flickered (ATN). Kept at Very High's 20; for objects it only matters for
               // the smallest ones, whose ratio distance is below it.
               {.name = "e_ViewDistMin", .is_float = true, .max_value = 20.f},
               {.name = "e_MergedMeshesViewDistRatio", .is_float = true}, {.name = "e_MergedMeshesInstanceDist", .is_float = true},
               {.name = "e_MergedMeshesPool", .is_float = false, .max_value = 32384.f * 16.f},
               // "Active distance up until merged mesh patches will be streamed in" (250); they fill the pool above.
               {.name = "e_MergedMeshesActiveDist", .is_float = true, .max_value = 250.f * 16.f},
               {.name = "e_VegetationSpritesDistanceRatio", .is_float = true},
               {.name = "e_ProcVegetationMaxViewDistance", .is_float = false},
               {.name = "e_ProcVegetationMaxObjectsInChunk", .is_float = false, .max_value = 512.f * 16.f},
               {.name = "e_ProcVegetationMaxSectorsInCache", .is_float = false, .max_value = 16.f * 16.f},
               // "Set all view distances shorter by factor of X" in reflections; 1 is the full distance.
               {.name = "e_RecursionViewDistRatio", .is_float = true, .max_value = 1.f},
               // Wind activates foliage physics within it (CPU): up to 16x like the pools.
               {.name = "e_FoliageWindActivationDist", .is_float = true, .max_value = 25.f * 16.f}}},
    {.label = "Detail distance (LOD)",
     .key = "DetailDistance",
     .tooltip = "How far the most detailed models, character attachments and detail textures are used,\n"
                "how far attachments (shields, weapons) are drawn, the resolution of far vegetation sprites and\n"
                "the most lights on one object (e_LodRatio, e_AttachmentLodDistScale, ca_AttachmentCullingRation,\n"
                "e_LodFaceAreaTargetSize: smaller is finer, r_DetailDistance, r_VegetationSpritesTexRes 64,\n"
                "up to 2048, e_DynamicLightsMaxEntityLights 16, up to 32). Objects pick their detail by triangle\n"
                "size on screen (e_LodFaceAreaTargetSize). Measured outdoors at the game's start, every object\n"
                "was at full detail by 64x. Armour and cloth: their blend shapes (the shapes that fit them\n"
                "to the body) are dropped when they move less than 1 pixel on screen, about 3 m away\n"
                "(ca_vaBlendCullingThreshold 1, divided). 64x also keeps attachments with fixed switch\n"
                "distances at full detail (e_LodMax 0: the most detailed model for everything).",
     .choices = "In-game setting\0" "2x\0" "4x\0" "8x\0" "16x\0" "32x\0" "64x (the maximum, armour too)\0",
     .choice_count = 7,
     .preset_choices = {1, 3, 4, 5},
     .values = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f},
     .cvars = {{.name = "e_LodRatio", .is_float = true}, {.name = "e_AttachmentLodDistScale", .is_float = true},
               // "ration between size of attachment and distance to camera" (300): attachments smaller on screen
               // are culled.
               {.name = "ca_AttachmentCullingRation", .is_float = true},
               {.name = "e_LodFaceAreaTargetSize", .is_float = true, .inverse = true},
               {.name = "r_DetailDistance", .is_float = true},
               // "Blend Shapes culling threshold" (1): each frame a blend shape whose largest move x weight projects
               // below this many pixels is culled (+0x8e0241: x 1 / (distance x tan) x screen height); the armour
               // then snaps to its smaller base shape (ATN: at 2-3 m, everything maxed).
               {.name = "ca_vaBlendCullingThreshold", .is_float = true, .inverse = true},
               {.name = "r_VegetationSpritesTexRes", .is_float = false, .max_value = 2048.f},
               // The lights sorted by strength, the first min(count, this) set in a 32-bit mask (+0xf9a3).
               {.name = "e_DynamicLightsMaxEntityLights", .is_float = false, .max_value = 32.f},
               // Attachments (armour, shields) with "LodDistances" in their CDF switch LOD at those metres, compared
               // with the camera distance unscaled (attachment LOD +0x881950: the distances passed, then clamped to
               // the LOD params' e_LodMin / e_LodMax, built at +0x17cf72 from 3D engine cvars +0x2fc / +0x41c).
               // e_LodMax is VF_CHEAT (the console's sets are put back): written in memory.
               {.name = "e_LodMax", .is_float = false, .absolute = true, .values = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
                .engine_offset = 0x41c, .from_choice = 6}}},
    {.label = "Tessellation",
     .key = "Tessellation",
     .tooltip = "Tessellation distance and density\n(e_TessellationMaxDistance, r_TessellationTriangleSize: smaller is denser).\n"
                "Measured outdoors at the game's start, its cost stopped growing at 4x.",
     .choices = "In-game setting\0" "2x\0" "4x\0" "8x\0" "16x\0" "32x\0" "64x (the maximum)\0",
     .choice_count = 7,
     .preset_choices = {1, 3, 4, 5},
     .values = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f},
     .cvars = {{.name = "e_TessellationMaxDistance", .is_float = true},
               {.name = "r_TessellationTriangleSize", .is_float = true, .inverse = true}}},
    {.label = "Particles",
     .key = "Particles",
     .tooltip = "How much of the screen particles may cover, the smallest particles drawn, their\n"
                "tessellation and the slices of volumetric light beams (e_ParticlesMaxScreenFill,\n"
                "e_ParticlesMinDrawPixels, r_ParticlesTessellationTriSize, r_BeamsMaxSlices 200, up to 300:\n"
                "the game's maximum).",
     .choices = "In-game setting\0" "2x\0" "4x\0" "8x\0" "16x\0" "32x\0" "64x (the maximum)\0",
     .choice_count = 7,
     .preset_choices = {1, 3, 4, 5},
     .values = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f},
     .cvars = {{.name = "e_ParticlesMaxScreenFill", .is_float = true},
               {.name = "e_ParticlesMinDrawPixels", .is_float = true, .inverse = true},
               {.name = "r_ParticlesTessellationTriSize", .is_float = false, .inverse = true, .min_value = 1.f},
               {.name = "r_BeamsMaxSlices", .is_float = false, .max_value = 300.f}}},
    {.label = "Water",
     .key = "Water",
     .tooltip = "Water surface tessellation (e_WaterTessellationAmount, e_WaterTessellationAmountX / Y),\n"
                "ripple resolution (r_WaterRipplesResolution 256), caustics resolution (r_WaterVolumeCausticsRes)\n"
                "and caustics distances (r_WaterCausticsDistance, r_WaterVolumeCausticsMaxDist); resolutions up\n"
                "to 4096, the game's maximum (they may need a level load). 2x is the maximum: 4x looked bad on\n"
                "the sea and 6x crashed (ATN); from about 7x the sea's grid no longer fits the game's 4 MB buffer.",
     .choices = "In-game setting\0" "2x (the maximum)\0",
     .choice_count = 2,
     // The render thread copies the sea's grid, (X - 1) x (Y - 1) vertices of 12 bytes, into a 4 MB buffer (ATN's
     // chapter 3 crash dumps: memcpy of 5532492 bytes at 8x, 22162572 at 16x, from e_WaterTessellationAmountX / Y
     // 85 at Very High). 4x looked bad on the sea, 6x (3.1 MB) crashed ATN's PC.
     .preset_choices = {1, 1, 1, 1},
     .values = {1.f, 2.f},
     .cvars = {{.name = "e_WaterTessellationAmount", .is_float = false},
               {.name = "e_WaterTessellationAmountX", .is_float = false},
               {.name = "e_WaterTessellationAmountY", .is_float = false},
               {.name = "r_WaterRipplesResolution", .is_float = false, .max_value = 4096.f},
               {.name = "r_WaterVolumeCausticsRes", .is_float = false, .max_value = 4096.f},
               {.name = "r_WaterCausticsDistance", .is_float = true},
               {.name = "r_WaterVolumeCausticsMaxDist", .is_float = true}}},
    {.label = "Texture streaming",
     .key = "TexturePool",
     .tooltip = "Video memory for streamed textures (r_TexturesStreamPoolSize, 1536 MB at Very High):\n"
                "fewer blurry textures. May need a level load or a restart. 2 GB is the game's maximum: one of\n"
                "its checks keeps the size in bytes in a signed 32-bit number, which turns negative from 2048 MB\n"
                "on (some or all textures then never load). Sharper / Sharpest also load the sharper texture\n"
                "levels from farther away (r_TexturesStreamingMipBias 0, -1, -2; they need more of the pool).",
     .choices = "In-game setting\0" "2 GB (the maximum)\0" "2 GB, sharper sooner\0" "2 GB, sharpest sooner\0",
     .choice_count = 4,
     .preset_choices = {1, 2, 2, 3},
     .values = {1.f, 16.f, 16.f, 16.f},
     .cvars = {{.name = "r_TexturesStreamPoolSize", .is_float = false, .max_value = 2047.f},
               {.name = "r_TexturesStreamingMipBias", .is_float = true, .absolute = true, .values = {0.f, 0.f, -1.f, -2.f}}}},
    {.label = "Model streaming",
     .key = "ModelStreaming",
     .tooltip = "Memory for detailed models and how fast textures load. The game keeps 80 MB of models\n"
                "(e_StreamCgfPoolSize, its render mesh cache, at Very High): with Detail distance above the\n"
                "game's, the detailed models may no longer fit. Also r_TexturesStreamingMaxRequestedMB up to 64\n"
                "and r_TexturesStreamingMaxRequestedJobs up to 256. 2 GB of models is the game's maximum: it\n"
                "keeps the size in bytes in a signed 32-bit number (as the texture pool).",
     .choices = "In-game setting\0" "2x\0" "4x\0" "8x\0" "16x\0" "32x (2 GB of models, the maximum)\0",
     .choice_count = 6,
     .preset_choices = {1, 3, 4, 5},
     .values = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f},
     // The cache: MB << 20 in a signed 32-bit compare (+0x17630a, +0x176457). The texture limits are compared with
     // counters, not array sizes. e_StreamCgfMaxTasksInProgress / MaxNewTasksPerUpdate are VF_CHEAT: the game puts
     // them back every frame ("is marked [VF_CHEAT]" in its console, ATN v59), left out.
     .cvars = {{.name = "e_StreamCgfPoolSize", .is_float = false, .max_value = 2047.f},
               {.name = "r_TexturesStreamingMaxRequestedMB", .is_float = true, .max_value = 64.f},
               {.name = "r_TexturesStreamingMaxRequestedJobs", .is_float = false, .max_value = 256.f}}},
    {.label = "Decals",
     .key = "Decals",
     .tooltip = "How long blood and impact marks stay (e_DecalsLifeTimeScale).",
     .choices = "In-game setting\0" "2x\0" "4x\0" "8x\0" "16x\0" "32x\0" "64x (the maximum)\0",
     .choice_count = 7,
     .preset_choices = {1, 3, 4, 5},
     .values = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f},
     .cvars = {{.name = "e_DecalsLifeTimeScale", .is_float = true}}},
}};

// Debug (Advanced, temporary): which system grows the soldiers' armour within 2-4 m (ATN: at every setting, also
// with v61's e_LodMax 0 and v62's blend-shape culling off). Each test overrides one cvar (the quality options leave it
// alone meanwhile); the test that makes the near armour look like the far one names the system.
struct ArmourTest {
  const char* label;
  const char* cvar;  // null: none
  float value;
};
constexpr std::array<ArmourTest, 6> ARMOUR_TESTS = {{
    {.label = "Off", .cvar = nullptr, .value = 0.f},
    {.label = "Blend shapes off (ca_vaBlendEnable 0)", .cvar = "ca_vaBlendEnable", .value = 0.f},
    {.label = "Vertex animation off (ca_vaEnable 0)", .cvar = "ca_vaEnable", .value = 0.f},
    {.label = "Tessellation only within 0.5 m (e_TessellationMaxDistance 0.5)", .cvar = "e_TessellationMaxDistance", .value = 0.5f},
    {.label = "Less detailed models (e_LodMin 2)", .cvar = "e_LodMin", .value = 2.f},
    {.label = "Less detailed characters (e_CharLodMin 2)", .cvar = "e_CharLodMin", .value = 2.f},
}};
struct ArmourTestState {
  int choice = 0;  // overlay
  int applied = 0;
  void* cvar = nullptr;  // ICVar* of the applied test
  float game_value = 0.f;
  std::string status;  // overlay
} armour_test;  // Present thread (the overlay runs in it)

// An option's `choice`th item.
const char* ChoiceName(const QualityOption& option, int choice) {
  const char* name = option.choices;
  for (int index = 0; index < choice; ++index) name += std::strlen(name) + 1;
  return name;
}

// Texture LOD bias below 100% (DLSS section): the game picks texture levels for its render resolution. Its anisotropic
// samplers (its material textures; post passes filter linear or point, shadow maps compare) are bound again right after
// it binds them, as clones with MipLODBias + log2(render / display width), minus 1 (Sharper) or 2 (Sharpest). ReShade
// raises push_descriptors after PSSetSamplers, so the clones replace them.
struct BiasedSampler {
  reshade::api::sampler_desc desc;  // the game's
  reshade::api::sampler clone = {0};
  float clone_bias = 0.f;
};
renodx::utils::data::ParallelFlatHashMap<uint64_t, BiasedSampler, std::shared_mutex> biased_samplers;
std::atomic<float> texture_lod_bias = 0.f;  // 0: none
int texture_lod_bias_choice = 2;  // Present thread (the overlay runs in it): 0 off, 1 matched, 2 sharper, 3 sharpest
thread_local bool creating_clone = false;  // the clone's own init_sampler
// While DLSS / DLAA runs (AA mode not Off), renderer statics written directly (FindIntCvar; the game's values come
// back with Off and at unload). r_TexNoAnisoAlphaTest 0: anisotropic filtering also on alpha-tested textures (foliage,
// fences, cloth; read while drawing, +0x18338EB; its ICVar refuses sets, flags 0x800100). r_DeferredShadingFilterGBuffer
// 0: no smoothing of normals and gloss against sparkling highlights (DLSS / DLAA calm them; fine surface detail stays;
// ATN: armour still shimmers a little with DLAA, the same without the HDR addon: the game's specular aliasing; the
// filter's smoothing was an option in v56, removed: off gives much better highlights).
struct RendererPin {
  const char* name;
  int value;  // while DLSS / DLAA runs
  bool searched = false;
  int* address = nullptr;
  std::string details;  // where, or why not; for the log
  bool set = false;
  int game_value = 0;
};
std::array<RendererPin, 2> renderer_pins = {{
    {.name = "r_TexNoAnisoAlphaTest", .value = 0},
    {.name = "r_DeferredShadingFilterGBuffer", .value = 0},
}};  // Present thread
bool attached = false;
bool registered = false;

// The game's projection jitter (r_PostAAMode): latest value in pixels, pattern length (frames until the
// first offset repeats; NVIDIA wants at least 8 * (output / input)^2), first frames logged.
struct Diagnostics {
  std::atomic<float> jitter_x = 0.f;
  std::atomic<float> jitter_y = 0.f;
  std::atomic_bool jitter_found = false;
  std::array<float, 2> first_jitter = {};
  uint32_t jitter_frames = 0;
  std::atomic<uint32_t> jitter_phases = 0;
  uint32_t logged = 0;
  // At the last TAA: camera uploads this frame, the main camera's jittered ones, and whether one of those
  // was used (the bound upload was unjittered).
  std::atomic<uint32_t> camera_uploads = 0;
  std::atomic<uint32_t> jittered_uploads = 0;
  std::atomic_bool from_upload = false;
  // Any TAA seen (also measured while DLSS is off); jitter on at the last one (logged when it changes);
  // TAA frames (a summary every 600).
  std::atomic_bool measured = false;
  bool jittering = false;
  uint64_t taa_frames = 0;
  std::atomic<uint64_t> restarts = 0;
  std::atomic<uint64_t> skipped_frames = 0;
} diagnostics;

// A camera in VS PER_FRAME (c0-c3 g_VS_ViewProjMatr, column vectors: clip = M * p; c6 g_VS_WorldViewPos):
// the projection adds jitter x row 3 to rows 0 and 1, and row 3 (view forward) is orthogonal to the
// right / up axes.
struct Camera {
  std::array<float, 2> jitter_ndc;
  std::array<float, 3> position;
  std::array<std::array<float, 4>, 4> view_projection;  // rows: clip x, y, z, w
};

std::optional<Camera> ReadCamera(std::span<const std::byte> bytes) {
  std::array<std::array<float, 4>, 7> rows;
  if (bytes.size() < sizeof(rows)) return std::nullopt;
  std::memcpy(rows.data(), bytes.data(), sizeof(rows));
  const auto dot3 = [](const std::array<float, 4>& a, const std::array<float, 4>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const float forward_length2 = dot3(rows[3], rows[3]);
  if (!(forward_length2 > 0.f) || !std::isfinite(forward_length2)) return std::nullopt;
  const Camera camera = {
      .jitter_ndc = {dot3(rows[0], rows[3]) / forward_length2, dot3(rows[1], rows[3]) / forward_length2},
      .position = {rows[6][0], rows[6][1], rows[6][2]},
      .view_projection = {rows[0], rows[1], rows[2], rows[3]},
  };
  // Jitter is a fraction of a pixel: anything larger is other data or an off-center projection.
  if (!(std::abs(camera.jitter_ndc[0]) < .01f && std::abs(camera.jitter_ndc[1]) < .01f)
      || !std::isfinite(camera.position[0]) || !std::isfinite(camera.position[1]) || !std::isfinite(camera.position[2])) {
    return std::nullopt;
  }
  return camera;
}

// Every camera the game uploads into PER_FRAME-sized buffers during a frame (shadow, reflection and post
// passes too): the upload bound at the TAA can be a later, unjittered one of the main camera.
struct CameraUploads {
  std::mutex mutex;
  uint64_t frame = UINT64_MAX;
  std::array<Camera, 16> latest;  // ring
  uint32_t count = 0;             // this frame
} camera_uploads;
std::atomic<uint64_t> present_frame = 0;
// r_PostAAMode as last seen or written on Present, for the render thread's log; -1: not found.
std::atomic<int> postaamode = -1;

void OnCameraUpload(std::span<const std::byte> bytes) {
  const auto camera = ReadCamera(bytes);
  if (!camera) return;
  const uint64_t frame = present_frame.load(std::memory_order_relaxed);
  const std::lock_guard lock(camera_uploads.mutex);
  if (camera_uploads.frame != frame) {
    camera_uploads.frame = frame;
    camera_uploads.count = 0;
  }
  camera_uploads.latest[camera_uploads.count % camera_uploads.latest.size()] = *camera;
  ++camera_uploads.count;
}

std::string DescribeTexture(ID3D11Resource* resource) {
  if (resource == nullptr) return "-";
  D3D11_TEXTURE2D_DESC desc = {};
  if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
    texture->GetDesc(&desc);
  }
  return std::format("{} {}x{} f{}", static_cast<const void*>(resource), desc.Width, desc.Height, static_cast<int>(desc.Format));
}

// A draw of the traced frame (see trace_requested).
void TraceDraw(reshade::api::command_list* cmd_list, std::string_view kind, uint32_t count) {
  const uint64_t frame = present_frame.load(std::memory_order_relaxed);
  if (frame != trace_frame.load(std::memory_order_relaxed)) return;
  const bool after_taa = taa_frame.load(std::memory_order_relaxed) == frame;
  if ((!after_taa && count > 6) || trace_lines.fetch_add(1) >= 2000) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  ComPtr<ID3D11Resource> target_resource;
  if (target) {
    target->GetResource(target_resource.GetAddressOf());
  }
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  std::array<ID3D11ShaderResourceView*, 8> views = {};
  context->PSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  std::string inputs;
  for (uint32_t slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    ComPtr<ID3D11Resource> resource;
    views[slot]->GetResource(resource.GetAddressOf());
    views[slot]->Release();
    inputs += std::format(" t{} {}", slot, DescribeTexture(resource.Get()));
  }
  renodx::utils::log::i(std::format(
      "[Ryse DLSS trace] {} {} {} ps {:08X} vs {:08X} rt {} vp {}x{} at {},{}{}", (after_taa ? "after TAA" : "before TAA"),
      kind, count, renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader),
      renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader),
      DescribeTexture(target_resource.Get()), viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY, inputs));
}

// Debug (Advanced button): the game's shadow maps for SHADOW_TRACE_FRAMES frames, in ryse_shadow_trace.txt next to the
// exe. Per frame, every depth target drawn without a color target (the sun's cascades, the lights' pool, per-object
// shadows) with each viewport drawn into it (a shadow map or a cube face) and its draws. Maps that come and go from
// frame to frame while the camera stands still are the flicker. The header lists each graphics cvar: the game's
// value, the addon's and what the game kept.
constexpr uint32_t SHADOW_TRACE_FRAMES = 300;
struct ShadowTarget {
  std::string description;
  uint32_t width = 0;
  uint32_t height = 0;
  std::map<std::array<int, 4>, uint32_t> draws_per_viewport;  // x, y, width, height
};
struct ShadowTrace {
  std::mutex mutex;
  std::ofstream file;
  uint64_t frame = 0;
  std::map<std::pair<uintptr_t, uint32_t>, ShadowTarget> targets;  // this frame; resource and array slice
} shadow_trace;
std::atomic<uint32_t> shadow_trace_frames = 0;  // frames left; 0: off
std::string shadow_trace_result;                // overlay only

// Debug (Advanced button): each option's choices in turn, the player standing still (not the graphics preset and the
// texture pool: those reload). Per choice, after MEASURE_SETTLE_FRAMES frames, the averages over MEASURE_FRAMES frames
// of the draws and indices into color targets (the scene) and into depth-only targets (shadow maps), and of the frame
// time, in ryse_options_measure.txt next to the exe. Where they stop changing, a higher choice draws nothing more
// (tessellation and shadow resolution show in the frame time only).
constexpr uint32_t MEASURE_SETTLE_FRAMES = 60;
constexpr uint32_t MEASURE_FRAMES = 60;
struct DrawCounts {
  std::atomic<uint64_t> scene_draws = 0;
  std::atomic<uint64_t> scene_indices = 0;
  std::atomic<uint64_t> shadow_draws = 0;
  std::atomic<uint64_t> shadow_indices = 0;
} draw_counts;  // added on the render thread, taken at Present
std::atomic_bool counting_draws = false;
struct OptionsMeasure {
  bool active = false;
  size_t option = 0;
  int choice = 0;
  uint32_t frame = 0;  // since the choice was applied
  std::array<int, std::tuple_size_v<decltype(quality_options)>> saved_choices = {};
  std::array<double, 5> sums = {};  // scene draws, scene indices, shadow draws, shadow indices, frame ms
  std::chrono::steady_clock::time_point last_present;
  std::ofstream file;
  std::string result;  // overlay
} options_measure;     // Present thread (the overlay runs in it)

void CountDraw(reshade::api::command_list* cmd_list, uint32_t count) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  ComPtr<ID3D11RenderTargetView> color;
  ComPtr<ID3D11DepthStencilView> depth;
  context->OMGetRenderTargets(1, color.GetAddressOf(), depth.GetAddressOf());
  if (!color && depth) {
    draw_counts.shadow_draws.fetch_add(1, std::memory_order_relaxed);
    draw_counts.shadow_indices.fetch_add(count, std::memory_order_relaxed);
  } else {
    draw_counts.scene_draws.fetch_add(1, std::memory_order_relaxed);
    draw_counts.scene_indices.fetch_add(count, std::memory_order_relaxed);
  }
}

void TraceShadowDraw(reshade::api::command_list* cmd_list) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  ComPtr<ID3D11RenderTargetView> color;
  ComPtr<ID3D11DepthStencilView> depth;
  context->OMGetRenderTargets(1, color.GetAddressOf(), depth.GetAddressOf());
  if (color || !depth) return;
  ComPtr<ID3D11Resource> resource;
  depth->GetResource(resource.GetAddressOf());
  D3D11_DEPTH_STENCIL_VIEW_DESC view = {};
  depth->GetDesc(&view);
  const uint32_t slice = (view.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY ? view.Texture2DArray.FirstArraySlice : 0u);
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  const std::lock_guard lock(shadow_trace.mutex);
  auto& target = shadow_trace.targets[{reinterpret_cast<uintptr_t>(resource.Get()), slice}];
  if (target.description.empty()) {
    D3D11_TEXTURE2D_DESC desc = {};
    if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource.As(&texture))) {
      texture->GetDesc(&desc);
    }
    target.description = std::format("{} slice {} of {}", DescribeTexture(resource.Get()), slice, desc.ArraySize);
    target.width = desc.Width;
    target.height = desc.Height;
  }
  ++target.draws_per_viewport[{static_cast<int>(viewport.TopLeftX), static_cast<int>(viewport.TopLeftY),
                               static_cast<int>(viewport.Width), static_cast<int>(viewport.Height)}];
}

bool OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t, uint32_t, uint32_t) {
  TraceDraw(cmd_list, "draw", vertex_count);
  if (shadow_trace_frames.load(std::memory_order_relaxed) != 0) TraceShadowDraw(cmd_list);
  if (counting_draws.load(std::memory_order_relaxed)) CountDraw(cmd_list, vertex_count);
  return false;
}

bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t, uint32_t, int32_t, uint32_t) {
  TraceDraw(cmd_list, "indexed", index_count);
  if (shadow_trace_frames.load(std::memory_order_relaxed) != 0) TraceShadowDraw(cmd_list);
  if (counting_draws.load(std::memory_order_relaxed)) CountDraw(cmd_list, index_count);
  return false;
}

// Texture LOD bias (biased_samplers): the game's anisotropic samplers.
void OnInitSampler(reshade::api::device* device, const reshade::api::sampler_desc& desc, reshade::api::sampler sampler) {
  if (creating_clone || device->get_api() != reshade::api::device_api::d3d11) return;
  if (desc.filter != reshade::api::filter_mode::anisotropic && desc.filter != reshade::api::filter_mode::min_mag_anisotropic_mip_point) return;
  biased_samplers.insert_or_assign(sampler.handle, BiasedSampler{.desc = desc});
}

void OnDestroySampler(reshade::api::device* device, reshade::api::sampler sampler) {
  reshade::api::sampler clone = {0};
  biased_samplers.erase_if(sampler.handle, [&](const auto& entry) {
    clone = entry.second.clone;
    return true;
  });
  if (clone.handle != 0u) {
    device->destroy_sampler(clone);
  }
}

// After the game's PSSetSamplers: its anisotropic samplers bound again as clones at the current bias.
void OnPushDescriptors(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages, reshade::api::pipeline_layout,
                       uint32_t, const reshade::api::descriptor_table_update& update) {
  const float bias = texture_lod_bias.load(std::memory_order_relaxed);
  if (bias == 0.f || stages != reshade::api::shader_stage::pixel || update.type != reshade::api::descriptor_type::sampler
      || update.count == 0 || update.binding + update.count > D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) {
    return;
  }
  auto* device = cmd_list->get_device();
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const auto* samplers = static_cast<const reshade::api::sampler*>(update.descriptors);
  std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> bound = {};
  bool replaced = false;
  for (uint32_t index = 0; index < update.count; ++index) {
    bound[index] = reinterpret_cast<ID3D11SamplerState*>(samplers[index].handle);
    reshade::api::sampler_desc desc = {};
    reshade::api::sampler clone = {0};
    const bool biased = biased_samplers.if_contains(samplers[index].handle, [&](const auto& entry) {
      desc = entry.second.desc;
      if (entry.second.clone_bias == bias) {
        clone = entry.second.clone;
      }
    });
    if (!biased) continue;
    if (clone.handle == 0u) {
      desc.mip_lod_bias += bias;
      creating_clone = true;
      const bool created = device->create_sampler(desc, &clone);
      creating_clone = false;
      if (!created) continue;
      reshade::api::sampler previous = {0};
      const bool kept = biased_samplers.modify_if(samplers[index].handle, [&](auto& entry) {
        previous = entry.second.clone;
        entry.second.clone = clone;
        entry.second.clone_bias = bias;
      });
      if (previous.handle != 0u) {
        device->destroy_sampler(previous);
      }
      if (!kept) {
        device->destroy_sampler(clone);
        continue;
      }
    }
    bound[index] = reinterpret_cast<ID3D11SamplerState*>(clone.handle);
    replaced = true;
  }
  if (replaced) {
    reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->PSSetSamplers(update.binding, update.count, bound.data());
  }
}

// A copy of the traced frame.
void TraceCopy(std::string_view kind, reshade::api::resource source, reshade::api::resource dest) {
  if (present_frame.load(std::memory_order_relaxed) != trace_frame.load(std::memory_order_relaxed)) return;
  renodx::utils::log::i("[Ryse DLSS trace] ", kind, " ", DescribeTexture(reinterpret_cast<ID3D11Resource*>(source.handle)), " -> ",
                        DescribeTexture(reinterpret_cast<ID3D11Resource*>(dest.handle)));
}

bool OnTraceCopy(reshade::api::command_list*, reshade::api::resource source, reshade::api::resource dest) {
  TraceCopy("copy", source, dest);
  return false;
}

bool OnTraceCopyRegion(reshade::api::command_list*, reshade::api::resource source, uint32_t, const reshade::api::subresource_box*,
                       reshade::api::resource dest, uint32_t, const reshade::api::subresource_box*, reshade::api::filter_mode) {
  TraceCopy("copy region", source, dest);
  return false;
}

// r_PostAAMode in the game's memory (engine_memory.hpp); Present thread only.
struct JitterControl {
  bool searched = false;
  int* value = nullptr;
  std::string details;
  bool controlling = false;
  int game_value = 0;
  int last_seen = -1;
  uint32_t logged_changes = 0;
  uint64_t rewrites = 0;  // the game set its own again while controlled
  bool setup_searched = false;
  rysesonofrome::engine_memory::CodePatch jitter_setup;  // the jump to the stub
  uint8_t* stub = nullptr;
  uint32_t phase = 0;
  rysesonofrome::engine_memory::CodePatch pause_flag_check;
} jitter_control;

// The Rendering Resolution while DLSS sets it (Present thread); the game's own comes back with DLSS off.
struct RenderResolution {
  bool searched = false;
  void* width = nullptr;  // the game's ICVar* (RENDER_SCENE)
  void* height = nullptr;
  void* native_res = nullptr;
  bool controlling = false;
  std::array<int, 2> game_size = {};  // the game's own (its menu)
  std::array<int, 2> target = {};     // set by the addon, as the game took it
  std::array<int, 2> pending = {};    // the mode's, set once unchanged for SETTLE_PRESENTS
  std::array<int, 2> last_seen = {};  // the cvars at the last present (a change not the addon's is the game's)
  uint32_t pending_presents = 0;
  uint32_t writes = 0;  // the game's own sets in the current window
  uint32_t window = 0;  // presents in the current window
  bool native_off = false;  // the mode's is below the display size, but Native Upscaling is off
  std::string problem;      // stopped (not found, or the game keeps setting its own)
} resolution;

// The game's ICVar (int).
int GetCvar(void* cvar) {
  return (*reinterpret_cast<int (***)(void*)>(cvar))[CVAR_GET_INT / sizeof(void*)](cvar);
}

void SetCvar(void* cvar, int value) {
  (*reinterpret_cast<void (***)(void*, int)>(cvar))[CVAR_SET_INT / sizeof(void*)](cvar, value);
}

// The game's IConsole*, or null (not found, or not created yet). Searched once: call from a present (the exe's code
// is readable once the game runs).
void* FindGameConsole() {
  auto& found = game_console;
  if (!found.searched) {
    found.searched = true;
    // rip-relative operand: the displacement at `at`, relative to the instruction's end 4 bytes later.
    const auto rip_target = [](const uint8_t* at) {
      int32_t displacement;
      std::memcpy(&displacement, at, sizeof(displacement));
      return at + 4 + displacement;
    };
    if (const uint8_t* site = rysesonofrome::engine_memory::FindPattern(CONSOLE_REGISTRATION, &found.details); site != nullptr) {
      found.console = reinterpret_cast<void* const*>(rip_target(site + 3));
      found.restricted = reinterpret_cast<const int*>(rip_target(site + 18));
    }
    renodx::utils::log::i("[Ryse DLSS] The game's console: ", found.details);
  }
  return (found.console != nullptr ? *found.console : nullptr);
}

// The 3D engine's cvars object (VEGETATION_SCREEN_RES's code loads it), or null (not found, or not created yet).
// Searched once: call from a present (the exe's code is only readable once the game runs).
uint8_t* EngineCvars() {
  auto& pin = screen_res_pin;
  if (!pin.searched) {
    pin.searched = true;
    if (const uint8_t* site = rysesonofrome::engine_memory::FindPattern(VEGETATION_SCREEN_RES, &pin.details); site != nullptr) {
      int32_t displacement;
      std::memcpy(&displacement, site + 3, sizeof(displacement));
      pin.cvars = reinterpret_cast<uint8_t* const*>(site + 7 + displacement);
    }
    renodx::utils::log::i("[Ryse DLSS] The 3D engine's cvars (e_ForceDetailLevelForScreenRes code): ", pin.details);
  }
  return (pin.cvars != nullptr ? *pin.cvars : nullptr);
}

// IConsole::GetCVar: the ICVar*, or null (no such cvar).
void* GetGameCvar(void* console, const char* name) {
  return (*reinterpret_cast<void* (***)(void*, const char*)>(console))[CONSOLE_GET_CVAR / sizeof(void*)](console, name);
}

// Render / display scale of the selected mode.
float ModeScale(AAMode mode) {
  return (mode == AAMode::DLSS_CUSTOM ? std::clamp(custom_scale.load(), MIN_CUSTOM_SCALE, 100.f) / 100.f
                                      : AA_MODES[static_cast<size_t>(mode)].scale);
}

// `texture` as a render target of the given size and formats (texture, target view), created again when they differ.
bool EnsureTexture(ID3D11Device* device, DXGI_FORMAT format, uint32_t width, uint32_t height, DXGI_FORMAT view_format,
                   Texture* texture) {
  if (texture->resource && texture->rtv) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture->resource->GetDesc(&desc);
    D3D11_RENDER_TARGET_VIEW_DESC view = {};
    texture->rtv->GetDesc(&view);
    if (desc.Format == format && desc.Width == width && desc.Height == height && view.Format == view_format) return true;
  }
  *texture = {};
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = width,
      .Height = height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = format,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
  };
  const D3D11_RENDER_TARGET_VIEW_DESC view = {.Format = view_format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
  return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->resource.GetAddressOf()))
         && SUCCEEDED(device->CreateRenderTargetView(texture->resource.Get(), &view, texture->rtv.GetAddressOf()));
}

// `texture`'s shader view in `format`, created again when it differs.
bool EnsureView(ID3D11Device* device, DXGI_FORMAT format, Texture* texture) {
  if (texture->srv) {
    D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
    texture->srv->GetDesc(&view);
    if (view.Format == format) return true;
  }
  texture->srv.Reset();
  const D3D11_SHADER_RESOURCE_VIEW_DESC view = {
      .Format = format,
      .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
      .Texture2D = {.MipLevels = 1},
  };
  return SUCCEEDED(device->CreateShaderResourceView(texture->resource.Get(), &view, texture->srv.GetAddressOf()));
}

// Debug: an 8-bit copy of `resource` seen as `format` (values x scale + bias, saturated, ^gamma), saved at Present.
// Render thread, under runtime_mutex, in the frame capture_frame.
void CaptureTexture(ID3D11DeviceContext1* context, ID3D11Resource* resource, DXGI_FORMAT format, std::string_view name,
                    float scale, float bias, float gamma) {
  ComPtr<ID3D11Texture2D> texture;
  if (resource == nullptr || !runtime.capture_shader || FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) return;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {
      .Format = format,
      .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
      .Texture2D = {.MipLevels = 1},
  };
  ComPtr<ID3D11ShaderResourceView> view;
  Capture capture = {.name = std::format("{}_{}", captures.size() + 1, name)};
  if (FAILED(runtime.device->CreateShaderResourceView(resource, &view_desc, view.GetAddressOf()))
      || !CreateTexture(runtime.device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, desc.Width, desc.Height,
                        &capture.texture)) {
    return;
  }
  // An sRGB view decodes: linear.
  const bool srgb = format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  const std::array<float, 4> values = {scale, bias, (srgb ? 1.f / 2.2f : gamma), 0.f};
  context->UpdateSubresource(runtime.capture_constants.Get(), 0, nullptr, values.data(), 0, 0);
  const rysesonofrome::dlss::ContextStateScope scope(context, runtime.private_state.Get());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(desc.Width), .Height = static_cast<float>(desc.Height), .MaxDepth = 1.f};
  ID3D11ShaderResourceView* sources[] = {view.Get()};
  ID3D11Buffer* buffers[] = {runtime.capture_constants.Get()};
  ID3D11RenderTargetView* outputs[] = {capture.texture.rtv.Get()};
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(runtime.fullscreen_shader.Get(), nullptr, 0);
  context->PSSetShader(runtime.capture_shader.Get(), nullptr, 0);
  context->PSSetShaderResources(0, 1, sources);
  context->PSSetConstantBuffers(12, 1, buffers);
  context->OMSetRenderTargets(1, outputs, nullptr);
  context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
  context->RSSetState(nullptr);
  context->RSSetViewports(1, &viewport);
  context->Draw(3, 0);
  captures.push_back(std::move(capture));
}

// Draws again what the game's command drew, with what is bound now.
template <typename Arguments>
void DrawAgain(ID3D11DeviceContext* context, const Arguments& arguments) {
  if constexpr (std::is_same_v<Arguments, command_action::DrawArguments>) {
    context->DrawInstanced(arguments.vertex_count, arguments.instance_count, arguments.first_vertex, arguments.first_instance);
  } else {
    context->DrawIndexedInstanced(arguments.index_count, arguments.instance_count, arguments.first_index, arguments.vertex_offset,
                                  arguments.first_instance);
  }
}

void SetStatus(std::string_view message) {
  bool changed;
  {
    const std::unique_lock lock(state.mutex);
    changed = state.status != message;
    if (changed) state.status.assign(message);
  }
  if (changed) {
    renodx::utils::log::i("[Ryse DLSS] ", message);
  }
}

// The native immediate context of a command, or null.
ComPtr<ID3D11DeviceContext1> ImmediateContext(reshade::api::command_list* cmd_list) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  ComPtr<ID3D11DeviceContext1> context;
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE
      || FAILED(native->QueryInterface(IID_PPV_ARGS(context.GetAddressOf())))) {
    return nullptr;
  }
  return context;
}

// `keep_nvidia`: mode changes keep NVIDIA's runtime (initializing takes over a second).
void Reset(bool keep_nvidia = false) {
  const std::lock_guard lock(runtime_mutex);
  auto backend = std::move(runtime.backend);
  runtime = {};
  if (keep_nvidia && !backend.Failed()) runtime.backend = std::move(backend);
  diagnostics.jitter_frames = 0;
  diagnostics.jitter_phases.store(0);
  diagnostics.logged = 0;
  diagnostics.restarts.store(0);
  diagnostics.skipped_frames.store(0);
  last_dlss_frame.store(UINT64_MAX);
  const std::unique_lock state_lock(state.mutex);
  state.claimed_frame = UINT64_MAX;
  state.completed = 0;
  state.performance = {};
}

constexpr auto OnTaaDraw = []<typename Context>(Context& command) -> command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, command_action::DrawArguments>
                && !std::is_same_v<Arguments, command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    // Off: the game's jitter is still measured (diagnostics), the stock TAA runs.
    const AAMode mode = requested_mode.load();
    const bool active = mode != AAMode::OFF;
    uint64_t frame;
    {
      const std::shared_lock lock(state.mutex);
      if (state.device != command.cmd_list->get_device() || (active && state.claimed_frame == state.frame)) return {};
      frame = state.frame;
    }
    taa_frame.store(frame, std::memory_order_relaxed);
    const auto fail = [active](std::string_view message) -> command_action::CallbackResult<Context> {
      if (active) SetStatus(message);
      return {};
    };
    const auto context = ImmediateContext(command.cmd_list);
    if (!context) return fail("Game TAA: drawn on a deferred context");
    {
      ComPtr<ID3D11Predicate> predicate;
      context->GetPredication(predicate.GetAddressOf(), nullptr);
      if (predicate) return fail("Game TAA: predicated draw unsupported");
    }

    // The game's bindings: inputs t2-t4, its target, PER_BATCH and the per-frame camera (VS b2).
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> inputs;
    {
      std::array<ID3D11ShaderResourceView*, 3> pointers = {};
      context->PSGetShaderResources(2, 3, pointers.data());
      for (uint32_t index = 0; index < inputs.size(); ++index) inputs[index].Attach(pointers[index]);
    }
    std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
    ComPtr<ID3D11DepthStencilView> depth_stencil;
    {
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> pointers = {};
      context->OMGetRenderTargets(static_cast<UINT>(pointers.size()), pointers.data(), depth_stencil.GetAddressOf());
      for (uint32_t index = 0; index < targets.size(); ++index) targets[index].Attach(pointers[index]);
    }
    if (!inputs[0] || !inputs[1] || !inputs[2] || !targets[0]) return fail("Game TAA: unexpected bindings");
    D3D11_SHADER_RESOURCE_VIEW_DESC color_view = {};
    inputs[2]->GetDesc(&color_view);
    ComPtr<ID3D11Resource> color_resource;
    inputs[2]->GetResource(color_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> color_texture;
    ComPtr<ID3D11Resource> target_resource;
    targets[0]->GetResource(target_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> target_texture;
    if (color_view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || FAILED(color_resource.As(&color_texture))
        || FAILED(target_resource.As(&target_texture))) {
      return fail("Game TAA: unexpected texture layout");
    }
    D3D11_TEXTURE2D_DESC color_desc = {};
    color_texture->GetDesc(&color_desc);
    D3D11_TEXTURE2D_DESC target_desc = {};
    target_texture->GetDesc(&target_desc);
    D3D11_VIEWPORT viewport = {};
    UINT viewport_count = 1;
    context->RSGetViewports(&viewport_count, &viewport);
    // The render size: the textures' (Rendering Resolution). A viewport inside them is the game's dynamic resolution
    // (osm_*), which its own passes do not handle on PC.
    const uint32_t width = color_desc.Width;
    const uint32_t height = color_desc.Height;
    if (target_desc.Width != width || target_desc.Height != height || viewport_count != 1 || viewport.TopLeftX != 0
        || viewport.TopLeftY != 0 || viewport.Width != static_cast<float>(width) || viewport.Height != static_cast<float>(height)) {
      return fail(std::format("Game TAA: unexpected size (input {}x{}, output {}x{}, viewport {}x{}; the game's dynamic "
                              "resolution is not supported)",
                              width, height, target_desc.Width, target_desc.Height, viewport.Width, viewport.Height));
    }

    const std::lock_guard runtime_lock(runtime_mutex);
    // This frame's jitter: the main camera bound at the TAA (VS PER_FRAME), or its latest jittered upload of
    // this frame when the bound one is not jittered.
    float jitter_ndc_x = 0.f;
    float jitter_ndc_y = 0.f;
    bool jitter_found = false;
    std::optional<Camera> current_camera;  // for the motion vectors
    {
      ComPtr<ID3D11Buffer> per_frame;
      UINT first_constant = 0;
      UINT constant_count = 0;
      context->VSGetConstantBuffers1(2, 1, per_frame.GetAddressOf(), &first_constant, &constant_count);
      std::optional<Camera> bound;
      if (per_frame && constant_count >= 7) {
        const uint64_t offset = uint64_t{first_constant} * 16;
        rysesonofrome::dlss::constants::Watch(per_frame.Get(), static_cast<uint32_t>(offset + 7 * 16));
        std::array<std::byte, 7 * 16> bytes;
        if (rysesonofrome::dlss::constants::Read(per_frame.Get(), offset, bytes)) bound = ReadCamera(bytes);
      }
      // Below 1/200 pixel: float noise of an unjittered matrix.
      const auto is_jittered = [&](const Camera& camera) {
        return std::abs(camera.jitter_ndc[0]) * width > .01f || std::abs(camera.jitter_ndc[1]) * height > .01f;
      };
      uint32_t uploads = 0;
      uint32_t jittered = 0;
      std::optional<Camera> uploaded;
      if (bound) {
        const auto same_position = [&](const Camera& camera) {
          float distance2 = 0.f;
          float length2 = 1.f;
          for (uint32_t axis = 0; axis < 3; ++axis) {
            const float difference = camera.position[axis] - bound->position[axis];
            distance2 += difference * difference;
            length2 += bound->position[axis] * bound->position[axis];
          }
          return distance2 <= 1e-8f * length2;
        };
        const std::lock_guard lock(camera_uploads.mutex);
        if (camera_uploads.frame == frame) {
          uploads = camera_uploads.count;
          const auto kept = std::min<uint32_t>(uploads, static_cast<uint32_t>(camera_uploads.latest.size()));
          for (uint32_t back = 1; back <= kept; ++back) {
            const auto& camera = camera_uploads.latest[(uploads - back) % camera_uploads.latest.size()];
            if (!same_position(camera) || !is_jittered(camera)) continue;
            ++jittered;
            if (!uploaded) uploaded = camera;
          }
        }
      }
      const bool use_upload = bound && !is_jittered(*bound) && uploaded;
      current_camera = (use_upload ? uploaded : bound);
      if (current_camera) {
        jitter_ndc_x = current_camera->jitter_ndc[0];
        jitter_ndc_y = current_camera->jitter_ndc[1];
        // At most 2 pixels.
        jitter_found = std::abs(jitter_ndc_x) * width <= 4.f && std::abs(jitter_ndc_y) * height <= 4.f;
      }
      if (!jitter_found) {
        jitter_ndc_x = 0.f;
        jitter_ndc_y = 0.f;
      }
      diagnostics.camera_uploads.store(uploads);
      diagnostics.jittered_uploads.store(jittered);
      diagnostics.from_upload.store(use_upload);
    }
    // NDC up, pixels down. DLSS: the sample offset in render pixels.
    const float jitter_x = jitter_ndc_x * 0.5f * width;
    const float jitter_y = -jitter_ndc_y * 0.5f * height;
    diagnostics.jitter_x.store(jitter_x);
    diagnostics.jitter_y.store(jitter_y);
    diagnostics.jitter_found.store(jitter_found);
    // Positions until the first one repeats; more than 1024: no repeat (random). Below 1/200 pixel (float
    // noise of an unjittered matrix): not jittering.
    diagnostics.measured.store(true);
    const bool jittering = std::abs(jitter_x) > .005f || std::abs(jitter_y) > .005f;
    if (!jittering) {
      diagnostics.jitter_phases.store(1);
      diagnostics.jitter_frames = 0;
    } else if (diagnostics.jitter_frames != 0 && std::abs(jitter_x - diagnostics.first_jitter[0]) < .01f
               && std::abs(jitter_y - diagnostics.first_jitter[1]) < .01f) {
      diagnostics.jitter_phases.store(diagnostics.jitter_frames);
      diagnostics.jitter_frames = 1;
    } else if (diagnostics.jitter_frames == 0 || diagnostics.jitter_frames > 1024) {
      if (diagnostics.jitter_frames > 1024) diagnostics.jitter_phases.store(diagnostics.jitter_frames);
      diagnostics.first_jitter = {jitter_x, jitter_y};
      diagnostics.jitter_frames = 1;
    } else {
      ++diagnostics.jitter_frames;
    }
    if (jittering != diagnostics.jittering) {
      diagnostics.jittering = jittering;
      renodx::utils::log::i("[Ryse DLSS] frame ", frame, ": the game's jitter ", (jittering ? "started" : "stopped"), " (r_PostAAMode ",
                            postaamode.load(), ", DLAA ", (active ? "on" : "off"), ", camera uploads ", diagnostics.camera_uploads.load(),
                            ", jittered main camera ", diagnostics.jittered_uploads.load(), ")");
    }
    if (++diagnostics.taa_frames % 600 == 0) {
      renodx::utils::log::i("[Ryse DLSS] frame ", frame, ": jitter (", jitter_x, ", ", jitter_y, ") pixels, ",
                            diagnostics.jitter_phases.load(), " positions, r_PostAAMode ", postaamode.load(), ", DLAA ",
                            (active ? "on" : "off"));
    }
    if (diagnostics.logged < 32) {
      ++diagnostics.logged;
      renodx::utils::log::i("[Ryse DLSS] frame ", frame, " jitter ", (jitter_found ? "" : "(not found) "), "ndc (", jitter_ndc_x,
                            ", ", jitter_ndc_y, ") pixels (", jitter_x, ", ", jitter_y, "); camera uploads ",
                            diagnostics.camera_uploads.load(), ", jittered main camera ", diagnostics.jittered_uploads.load(),
                            (diagnostics.from_upload.load() ? ", from an earlier upload" : ""));
    }
    if (!active) return {};

    uint32_t display_width;
    uint32_t display_height;
    {
      const std::shared_lock lock(state.mutex);
      display_width = (state.display_width != 0 ? state.display_width : width);
      display_height = (state.display_height != 0 ? state.display_height : height);
    }
    if (width > display_width || height > display_height) {
      SetStatus("Game TAA: the render size is above the display size (supersampling): DLSS needs it at or below");
      return {};
    }
    // Below the display size (the native upscale follows) DLSS upscales to it: the mode's backend at its scale, Custom
    // (any scale) for another size, as in the frames before the mode's resolution is set. Otherwise DLAA.
    const bool upscaling = width < display_width || height < display_height;
    const uint32_t output_width = (upscaling ? display_width : width);
    const uint32_t output_height = (upscaling ? display_height : height);
    auto backend_mode = rysesonofrome::dlss::Mode::DLAA;
    if (upscaling) {
      const bool mode_scale = mode != AAMode::DLAA
                              && std::abs(static_cast<float>(width) / static_cast<float>(display_width) - ModeScale(mode)) < .02f;
      backend_mode = (mode_scale ? AA_MODES[static_cast<size_t>(mode)].backend : rysesonofrome::dlss::Mode::DLSS_CUSTOM);
    }

    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (runtime.device != device || runtime.width != width || runtime.height != height || runtime.output_width != output_width
        || runtime.output_height != output_height) {
      renodx::utils::log::i("[Ryse DLSS] TAA input ", width, "x", height, ", output ", output_width, "x", output_height, ", color view format ",
                            static_cast<uint32_t>(color_view.Format), ", target format ", static_cast<uint32_t>(target_desc.Format));
      auto backend = std::move(runtime.backend);
      auto performance = std::move(runtime.performance);
      runtime = {};
      if (!backend.Failed()) runtime.backend = std::move(backend);
      runtime.performance = std::move(performance);
      ComPtr<ID3D11Device1> device1;
      if (FAILED(device.As(&device1))) {
        SetStatus("Game TAA: D3D11.1 context-state support unavailable");
        return {};
      }
      const auto feature_level = device->GetFeatureLevel();
      const uint32_t flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0
                                  ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                                  : 0);
      const D3D11_BUFFER_DESC buffer_desc = {
          .ByteWidth = sizeof(PassConstants),
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
      };
      constexpr float EXPOSURE = 1.f;
      const D3D11_TEXTURE2D_DESC exposure_desc = {
          .Width = 1,
          .Height = 1,
          .MipLevels = 1,
          .ArraySize = 1,
          .Format = DXGI_FORMAT_R32_FLOAT,
          .SampleDesc = {.Count = 1},
          .Usage = D3D11_USAGE_IMMUTABLE,
          .BindFlags = D3D11_BIND_SHADER_RESOURCE,
      };
      const D3D11_SUBRESOURCE_DATA exposure_data = {.pSysMem = &EXPOSURE, .SysMemPitch = sizeof(EXPOSURE)};
      if (FAILED(device1->CreateDeviceContextState(flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                   nullptr, runtime.private_state.GetAddressOf()))
          || FAILED(device->CreatePixelShader(__0x52594501.data(), __0x52594501.size(), nullptr, runtime.prepare_shader.GetAddressOf()))
          || FAILED(device->CreatePixelShader(__0x52594502.data(), __0x52594502.size(), nullptr, runtime.copy_shader.GetAddressOf()))
          || FAILED(device->CreatePixelShader(__0x52594503.data(), __0x52594503.size(), nullptr, runtime.smaa_shader.GetAddressOf()))
          || FAILED(device->CreateVertexShader(__0x52594504.data(), __0x52594504.size(), nullptr, runtime.fullscreen_shader.GetAddressOf()))
          || FAILED(device->CreatePixelShader(__0x52594505.data(), __0x52594505.size(), nullptr, runtime.capture_shader.GetAddressOf()))
          || FAILED(device->CreateBuffer(&buffer_desc, nullptr, runtime.constants.GetAddressOf()))
          || FAILED(device->CreateBuffer(&buffer_desc, nullptr, runtime.capture_constants.GetAddressOf()))
          || FAILED(device->CreateTexture2D(&exposure_desc, &exposure_data, runtime.exposure.GetAddressOf()))
          || !CreateTexture(device.Get(), DXGI_FORMAT_R32_FLOAT, D3D11_BIND_RENDER_TARGET, width, height, &runtime.depth)
          || !CreateTexture(device.Get(), DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET, width, height, &runtime.motion)
          || !CreateTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET, width, height, &runtime.color)
          || !CreateTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_UNORDERED_ACCESS, output_width, output_height,
                            &runtime.output)) {
        runtime = {};
        SetStatus("Game TAA: private resource creation failed");
        return {};
      }
      runtime.device = device;
      runtime.width = width;
      runtime.height = height;
      runtime.output_width = output_width;
      runtime.output_height = output_height;
    }
    if (runtime.backend.Failed()) {
      SetStatus(std::string("Game TAA: ") + runtime.backend.Status());
      return {};
    }

    // The sRGB view decodes; other views (8-bit UNORM, or FP16 from the HDR addon) hold encoded values.
    const bool srgb_view = color_view.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || color_view.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    PassConstants constants = {.color_encoding = (srgb_view ? 0u : 1u), .has_camera = (current_camera ? 1u : 0u)};
    if (current_camera) {
      // uv = (0.5 x + 0.5 w, -0.5 y + 0.5 w) / w, as the game's mViewProjPrev.
      const auto& rows = current_camera->view_projection;
      for (uint32_t index = 0; index < 4; ++index) {
        constants.current_uv[0][index] = .5f * rows[0][index] + .5f * rows[3][index];
        constants.current_uv[1][index] = -.5f * rows[1][index] + .5f * rows[3][index];
        constants.current_uv[2][index] = rows[3][index];
      }
    }
    context->UpdateSubresource(runtime.constants.Get(), 0, nullptr, &constants, 0, 0);

    // The game's pixel-stage and output state this pass changes, restored on every exit (CryEngine caches
    // its bindings).
    struct SavedState {
      ID3D11DeviceContext1* context;
      ComPtr<ID3D11PixelShader> shader;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11Buffer> constants;
      ComPtr<ID3D11BlendState> blend;
      std::array<float, 4> blend_factor = {};
      UINT sample_mask = 0;
      const std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>* targets;
      ID3D11DepthStencilView* depth_stencil;

      ~SavedState() {
        std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> pointers = {};
        for (uint32_t index = 0; index < pointers.size(); ++index) pointers[index] = (*targets)[index].Get();
        context->OMSetRenderTargets(static_cast<UINT>(pointers.size()), pointers.data(), depth_stencil);
        context->OMSetBlendState(blend.Get(), blend_factor.data(), sample_mask);
        context->PSSetShader(shader.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[] = {srv.Get()};
        context->PSSetShaderResources(0, 1, srvs);
        ID3D11Buffer* buffers[] = {constants.Get()};
        context->PSSetConstantBuffers(12, 1, buffers);
      }
    } saved = {.context = context.Get(), .targets = &targets, .depth_stencil = depth_stencil.Get()};
    context->PSGetShader(saved.shader.GetAddressOf(), nullptr, nullptr);
    context->PSGetShaderResources(0, 1, saved.srv.GetAddressOf());
    context->PSGetConstantBuffers(12, 1, saved.constants.GetAddressOf());
    context->OMGetBlendState(saved.blend.GetAddressOf(), saved.blend_factor.data(), &saved.sample_mask);

    const uint64_t overlay = overlay_frame.load();
    const bool measuring = overlay != 0 && frame + 1 - overlay < 120 && runtime.last_frame != UINT64_MAX;
    if (measuring) {
      runtime.performance.Poll(context.Get());
      runtime.performance.Begin(context.Get());
      runtime.performance.Mark(context.Get());  // no stock TAA
    }

    // Preparation: depth, motion and linear color, drawn with the game's TAA vertex stage and inputs.
    runtime.performance.Mark(context.Get());
    {
      ID3D11Buffer* buffers[] = {runtime.constants.Get()};
      ID3D11RenderTargetView* outputs[] = {runtime.depth.rtv.Get(), runtime.motion.rtv.Get(), runtime.color.rtv.Get()};
      context->PSSetShader(runtime.prepare_shader.Get(), nullptr, 0);
      context->PSSetConstantBuffers(12, 1, buffers);
      context->OMSetRenderTargets(3, outputs, nullptr);
      context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      DrawAgain(context.Get(), command.arguments);
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets = {};
      for (uint32_t index = 0; index < game_targets.size(); ++index) game_targets[index] = targets[index].Get();
      context->OMSetRenderTargets(static_cast<UINT>(game_targets.size()), game_targets.data(), depth_stencil.Get());
    }
    runtime.performance.Mark(context.Get());

    const bool restart = runtime.last_frame == UINT64_MAX;
    const bool skipped = !restart && frame != runtime.last_frame + 1;
    if (restart) ++diagnostics.restarts;
    if (skipped) ++diagnostics.skipped_frames;
    bool evaluated;
    runtime.performance.Mark(context.Get());
    {
      const rysesonofrome::dlss::ContextStateScope restore(context.Get(), runtime.private_state.Get());
      evaluated = runtime.backend.Evaluate(context.Get(), {
                                                              .color = runtime.color.resource.Get(),
                                                              .depth = runtime.depth.resource.Get(),
                                                              .motion_vectors = runtime.motion.resource.Get(),
                                                              .output = runtime.output.resource.Get(),
                                                              .exposure = runtime.exposure.Get(),
                                                              .width = width,
                                                              .height = height,
                                                              .jitter_x = jitter_x,
                                                              .jitter_y = jitter_y,
                                                              .reset = restart || skipped,
                                                              .output_width = (upscaling ? output_width : 0),
                                                              .output_height = (upscaling ? output_height : 0),
                                                              .mode = backend_mode,
                                                              .preset = preset.load(),
                                                          });
    }
    runtime.performance.Mark(context.Get());
    if (!evaluated) {
      runtime.last_frame = UINT64_MAX;
      if (measuring) runtime.performance.End(nullptr);
      SetStatus(std::string("Game TAA: ") + runtime.backend.Status());
      return {};
    }

    // NVIDIA's result encoded like the game's TAA output with its blend state. DLAA: into its TAA target, the stock
    // draw skipped. Upscaling: into the addon's copy of that target at the display size (taa_full) for the post pass
    // drawn again there; the stock TAA still fills the game's target (the frame the native upscale shows otherwise).
    runtime.performance.Mark(context.Get());
    {
      ID3D11ShaderResourceView* srvs[] = {runtime.output.srv.Get()};
      context->PSSetShader(runtime.copy_shader.Get(), nullptr, 0);
      context->PSSetShaderResources(0, 1, srvs);
      context->OMSetBlendState(saved.blend.Get(), saved.blend_factor.data(), saved.sample_mask);
      if (upscaling) {
        D3D11_RENDER_TARGET_VIEW_DESC target_view = {};
        targets[0]->GetDesc(&target_view);
        if (!EnsureTexture(device.Get(), target_desc.Format, output_width, output_height, target_view.Format, &runtime.taa_full)) {
          runtime.last_frame = UINT64_MAX;
          if (measuring) runtime.performance.End(nullptr);
          SetStatus("Game TAA: private resource creation failed");
          return {};
        }
        const D3D11_VIEWPORT whole_viewport = {
            .Width = static_cast<float>(output_width),
            .Height = static_cast<float>(output_height),
            .MaxDepth = 1.f,
        };
        const D3D11_RECT whole_scissor = {.right = static_cast<LONG>(output_width), .bottom = static_cast<LONG>(output_height)};
        std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_scissors = {};
        UINT game_scissor_count = static_cast<UINT>(game_scissors.size());
        context->RSGetScissorRects(&game_scissor_count, game_scissors.data());
        ID3D11RenderTargetView* outputs[] = {runtime.taa_full.rtv.Get()};
        context->OMSetRenderTargets(1, outputs, nullptr);
        context->RSSetViewports(1, &whole_viewport);
        context->RSSetScissorRects(1, &whole_scissor);
        DrawAgain(context.Get(), command.arguments);
        context->RSSetViewports(1, &viewport);
        context->RSSetScissorRects(game_scissor_count, game_scissors.data());
        upscaled_frame.store(frame);
        if (!upscale_traced.exchange(true)) {
          trace_requested.store(true);
        }
        if (capture_requested.exchange(false)) capture_frame.store(frame);
        if (const uint64_t captured = capture_frame.load(); captured == frame || captured + 1 == frame) {
          const std::string_view next = (captured == frame ? "" : "_next");
          CaptureTexture(context.Get(), runtime.color.resource.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, std::format("dlss_input{}", next),
                         1.f, 0.f, 1.f / 2.2f);
          if (captured == frame) {
            // Pixels / 32 around gray, and fine: -0.5 to 0.5 pixels (still camera: gray).
            CaptureTexture(context.Get(), runtime.motion.resource.Get(), DXGI_FORMAT_R16G16_FLOAT, "dlss_motion", 1.f / 32.f, .5f, 1.f);
            CaptureTexture(context.Get(), runtime.motion.resource.Get(), DXGI_FORMAT_R16G16_FLOAT, "dlss_motion_fine", 1.f, .5f, 1.f);
          }
          CaptureTexture(context.Get(), runtime.output.resource.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, std::format("dlss_output{}", next),
                         1.f, 0.f, 1.f / 2.2f);
          renodx::utils::log::i("[Ryse DLSS] Captured frame ", frame, ": jitter (", jitter_x, ", ", jitter_y, ") pixels, camera ",
                                (current_camera ? "found" : "not found"));
        }
      } else {
        DrawAgain(context.Get(), command.arguments);
      }
    }
    if (measuring) runtime.performance.End(context.Get());
    runtime.last_frame = frame;
    last_dlss_frame.store(frame);
    {
      const std::unique_lock lock(state.mutex);
      state.claimed_frame = frame;
      ++state.completed;
      state.render_width = width;
      state.render_height = height;
      state.output_width = output_width;
      state.output_height = output_height;
      state.performance = runtime.performance.Snapshot();
    }
    std::string status = std::string(upscaling ? AA_MODES[static_cast<size_t>(mode)].name : "DLAA") + " active";
    if (!jitter_found) status += ", but the game's jitter was not found (see below)";
    SetStatus(status);
    return {.bypass = !upscaling};
  }
};

constexpr auto OnSmaaDraw = []<typename Context>(Context& command) -> command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, command_action::DrawArguments>
                && !std::is_same_v<Arguments, command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (requested_mode.load() == AAMode::OFF
        || command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    uint64_t frame;
    {
      const std::shared_lock lock(state.mutex);
      if (state.device != command.cmd_list->get_device()) return {};
      frame = state.frame;
    }
    // DLSS replaced this frame's or the last frame's TAA.
    if (const uint64_t last = last_dlss_frame.load(); last == UINT64_MAX || frame > last + 1) return {};
    if (command.matched_shader_hash != SMAA_BLEND_SHADER) return {.bypass = true};

    const auto context = ImmediateContext(command.cmd_list);
    if (!context) return {};
    ComPtr<ID3D11ShaderResourceView> color;
    context->PSGetShaderResources(1, 1, color.GetAddressOf());
    if (!color) return {};
    const std::lock_guard runtime_lock(runtime_mutex);
    if (!runtime.smaa_shader) return {};
    ComPtr<ID3D11PixelShader> game_shader;
    context->PSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
    context->PSSetShader(runtime.smaa_shader.Get(), nullptr, 0);
    DrawAgain(context.Get(), command.arguments);
    context->PSSetShader(game_shader.Get(), nullptr, 0);
    return {.bypass = true};
  }
};

// The post pass of a frame DLSS upscaled (POST_SHADERS): drawn first at the display size into the addon's post_full,
// with NVIDIA's result (taa_full, in the view the game's t0 has), the addon's full-screen triangle (texture coordinates
// 0-1) and the game's pixel shader and other bindings (its lens flares t5, at the render size, are filtered up). Then
// the game's own draw at the render size.
constexpr auto OnPostDraw = []<typename Context>(Context& command) -> command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, command_action::DrawArguments>
                && !std::is_same_v<Arguments, command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    uint64_t frame;
    {
      const std::shared_lock lock(state.mutex);
      if (state.device != command.cmd_list->get_device() || upscaled_frame.load() != state.frame) return {};
      frame = state.frame;
    }
    const auto context = ImmediateContext(command.cmd_list);
    if (!context) return {};
    std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets;
    ComPtr<ID3D11DepthStencilView> game_depth;
    {
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> pointers = {};
      context->OMGetRenderTargets(static_cast<UINT>(pointers.size()), pointers.data(), game_depth.GetAddressOf());
      for (uint32_t index = 0; index < game_targets.size(); ++index) game_targets[index].Attach(pointers[index]);
    }
    ComPtr<ID3D11ShaderResourceView> game_input;
    context->PSGetShaderResources(0, 1, game_input.GetAddressOf());
    if (!game_targets[0] || !game_input) return {};
    ComPtr<ID3D11Resource> target_resource;
    game_targets[0]->GetResource(target_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> target_texture;
    if (FAILED(target_resource.As(&target_texture))) return {};
    D3D11_TEXTURE2D_DESC target_desc = {};
    target_texture->GetDesc(&target_desc);
    D3D11_RENDER_TARGET_VIEW_DESC target_view = {};
    game_targets[0]->GetDesc(&target_view);
    D3D11_SHADER_RESOURCE_VIEW_DESC input_view = {};
    game_input->GetDesc(&input_view);

    const std::lock_guard runtime_lock(runtime_mutex);
    if (!runtime.taa_full.resource || !runtime.fullscreen_shader) return {};
    D3D11_TEXTURE2D_DESC full_desc = {};
    runtime.taa_full.resource->GetDesc(&full_desc);
    if (!EnsureView(runtime.device.Get(), input_view.Format, &runtime.taa_full)
        || !EnsureTexture(runtime.device.Get(), target_desc.Format, full_desc.Width, full_desc.Height, target_view.Format,
                          &runtime.post_full)) {
      return {};
    }

    // The game's vertex stage, topology, viewports, scissors, targets and t0, restored after the draw.
    ComPtr<ID3D11VertexShader> game_vertex_shader;
    context->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
    D3D11_PRIMITIVE_TOPOLOGY game_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->IAGetPrimitiveTopology(&game_topology);
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_viewports = {};
    UINT game_viewport_count = static_cast<UINT>(game_viewports.size());
    context->RSGetViewports(&game_viewport_count, game_viewports.data());
    std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_scissors = {};
    UINT game_scissor_count = static_cast<UINT>(game_scissors.size());
    context->RSGetScissorRects(&game_scissor_count, game_scissors.data());

    const D3D11_VIEWPORT whole_viewport = {
        .Width = static_cast<float>(full_desc.Width),
        .Height = static_cast<float>(full_desc.Height),
        .MaxDepth = 1.f,
    };
    const D3D11_RECT whole_scissor = {.right = static_cast<LONG>(full_desc.Width), .bottom = static_cast<LONG>(full_desc.Height)};
    ID3D11RenderTargetView* outputs[] = {runtime.post_full.rtv.Get()};
    ID3D11ShaderResourceView* inputs[] = {runtime.taa_full.srv.Get()};
    context->VSSetShader(runtime.fullscreen_shader.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetViewports(1, &whole_viewport);
    context->RSSetScissorRects(1, &whole_scissor);
    context->OMSetRenderTargets(1, outputs, nullptr);
    context->PSSetShaderResources(0, 1, inputs);
    context->Draw(3, 0);
    {
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> pointers = {};
      for (uint32_t index = 0; index < pointers.size(); ++index) pointers[index] = game_targets[index].Get();
      ID3D11ShaderResourceView* game_inputs[] = {game_input.Get()};
      context->OMSetRenderTargets(static_cast<UINT>(pointers.size()), pointers.data(), game_depth.Get());
      context->PSSetShaderResources(0, 1, game_inputs);
    }
    context->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(game_topology);
    context->RSSetViewports(game_viewport_count, game_viewports.data());
    context->RSSetScissorRects(game_scissor_count, game_scissors.data());
    if (const uint64_t captured = capture_frame.load(); captured == frame || captured + 1 == frame) {
      const std::string_view next = (captured == frame ? "" : "_next");
      if (captured == frame) {
        CaptureTexture(context.Get(), runtime.taa_full.resource.Get(), input_view.Format, "post_input", 1.f, 0.f, 1.f);
      }
      ComPtr<ID3D11ShaderResourceView> game_flares;
      context->PSGetShaderResources(5, 1, game_flares.GetAddressOf());
      if (game_flares) {
        D3D11_SHADER_RESOURCE_VIEW_DESC flare_view = {};
        game_flares->GetDesc(&flare_view);
        ComPtr<ID3D11Resource> flare_resource;
        game_flares->GetResource(flare_resource.GetAddressOf());
        CaptureTexture(context.Get(), flare_resource.Get(), flare_view.Format, std::format("lens_flares{}", next), 1.f, 0.f, 1.f / 2.2f);
      }
      CaptureTexture(context.Get(), runtime.post_full.resource.Get(), target_view.Format,
                     std::format("post_output_{:08X}{}", command.matched_shader_hash, next), 1.f, 0.f, 1.f);
    }
    post_drawn_frame.store(frame);
    return {};
  }
};

// The native upscale of a frame whose post pass was drawn at the display size: that result copied 1:1 instead
// (0x52594503: Load of t1), into the back buffer, where the game's UI follows. Otherwise the game's own.
constexpr auto OnUpscaleDraw = []<typename Context>(Context& command) -> command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, command_action::DrawArguments>
                && !std::is_same_v<Arguments, command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    uint64_t frame;
    {
      const std::shared_lock lock(state.mutex);
      if (state.device != command.cmd_list->get_device() || post_drawn_frame.load() != state.frame) return {};
      frame = state.frame;
    }
    const auto context = ImmediateContext(command.cmd_list);
    if (!context) return {};
    ComPtr<ID3D11RenderTargetView> target;
    context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
    if (!target) return {};
    ComPtr<ID3D11Resource> target_resource;
    target->GetResource(target_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> target_texture;
    if (FAILED(target_resource.As(&target_texture))) return {};
    D3D11_TEXTURE2D_DESC target_desc = {};
    target_texture->GetDesc(&target_desc);
    D3D11_VIEWPORT viewport = {};
    UINT viewport_count = 1;
    context->RSGetViewports(&viewport_count, &viewport);

    const std::lock_guard runtime_lock(runtime_mutex);
    if (!runtime.post_full.resource || !runtime.smaa_shader) return {};
    D3D11_TEXTURE2D_DESC full_desc = {};
    runtime.post_full.resource->GetDesc(&full_desc);
    D3D11_RENDER_TARGET_VIEW_DESC full_view = {};
    runtime.post_full.rtv->GetDesc(&full_view);
    // The stored values: the game's upscale decodes sRGB, filters and encodes again.
    DXGI_FORMAT stored_format = full_view.Format;
    if (stored_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && full_desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) {
      stored_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    } else if (stored_format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB && full_desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS) {
      stored_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    if (target_desc.Width != full_desc.Width || target_desc.Height != full_desc.Height || viewport_count != 1
        || viewport.TopLeftX != 0 || viewport.TopLeftY != 0 || viewport.Width != static_cast<float>(full_desc.Width)
        || viewport.Height != static_cast<float>(full_desc.Height)
        || !EnsureView(runtime.device.Get(), stored_format, &runtime.post_full)) {
      return {};
    }
    ComPtr<ID3D11PixelShader> game_shader;
    context->PSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
    ComPtr<ID3D11ShaderResourceView> game_input;
    context->PSGetShaderResources(1, 1, game_input.GetAddressOf());
    ID3D11ShaderResourceView* inputs[] = {runtime.post_full.srv.Get()};
    context->PSSetShader(runtime.smaa_shader.Get(), nullptr, 0);
    context->PSSetShaderResources(1, 1, inputs);
    DrawAgain(context.Get(), command.arguments);
    ID3D11ShaderResourceView* game_inputs[] = {game_input.Get()};
    context->PSSetShaderResources(1, 1, game_inputs);
    context->PSSetShader(game_shader.Get(), nullptr, 0);
    shown_frame.store(frame);
    return {.bypass = true};
  }
};


// Debug: writes the captured stages (CaptureTexture) as PNGs next to the exe. Present thread.
void SaveCaptures() {
  const std::lock_guard lock(runtime_mutex);
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  const auto folder = std::filesystem::path(exe).parent_path();
  ComPtr<ID3D11DeviceContext> context;
  if (runtime.device) {
    runtime.device->GetImmediateContext(context.GetAddressOf());
  }
  uint32_t saved = 0;
  for (const auto& capture : captures) {
    D3D11_TEXTURE2D_DESC desc = {};
    capture.texture.resource->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (!context || FAILED(runtime.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) continue;
    context->CopyResource(staging.Get(), capture.texture.resource.Get());
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;
    const size_t row_bytes = size_t{desc.Width} * 4;
    std::vector<uint8_t> pixels(row_bytes * desc.Height);
    for (uint32_t row = 0; row < desc.Height; ++row) {
      std::memcpy(pixels.data() + row * row_bytes, static_cast<const uint8_t*>(mapped.pData) + size_t{row} * mapped.RowPitch, row_bytes);
    }
    context->Unmap(staging.Get(), 0);
    if (renodx::utils::png::WriteRgba8(folder / std::format("ryse_capture_{}.png", capture.name), desc.Width, desc.Height, pixels)) {
      ++saved;
    }
  }
  capture_result = std::format("Saved {} of {} stages: {}", saved, captures.size(),
                               reinterpret_cast<const char*>((folder / "ryse_capture_*.png").u8string().c_str()));
  renodx::utils::log::i("[Ryse DLSS] ", capture_result);
  captures.clear();
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  if (swapchain->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  Reset(true);
  const auto desc = swapchain->get_device()->get_resource_desc(swapchain->get_back_buffer(0));
  const std::unique_lock lock(state.mutex);
  state.device = swapchain->get_device();
  state.display_width = desc.texture.width;
  state.display_height = desc.texture.height;
}

void OnDestroyDevice(reshade::api::device* device) {
  bool matching;
  {
    const std::unique_lock lock(state.mutex);
    matching = state.device == device;
    if (matching) state.device = nullptr;
  }
  if (matching) Reset();
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  const auto mode = requested_mode.load();
  bool changed;
  bool missed;
  uint64_t closing_frame;
  {
    const std::unique_lock lock(state.mutex);
    if (state.device != swapchain->get_device()) return;
    changed = state.last_mode != mode;
    state.last_mode = mode;
    missed = state.claimed_frame != state.frame;
    if (upscaled_frame.load() == state.frame && shown_frame.load() != state.frame) {
      ++unshown_frames;
    }
    closing_frame = state.frame;
    ++state.frame;
    present_frame.store(state.frame, std::memory_order_relaxed);
  }
  if (trace_requested.exchange(false)) {
    trace_lines.store(0);
    trace_frame.store(present_frame.load());
    renodx::utils::log::i("[Ryse DLSS trace] frame ", present_frame.load(), ", mode ", AA_MODES[static_cast<size_t>(mode)].name);
  }
  if (shadow_trace_frames.load() != 0) {
    const std::lock_guard lock(shadow_trace.mutex);
    for (const auto& [key, target] : shadow_trace.targets) {
      uint64_t texels = 0;
      std::string maps;
      for (const auto& [viewport, draws] : target.draws_per_viewport) {
        texels += static_cast<uint64_t>(viewport[2]) * static_cast<uint64_t>(viewport[3]);
        maps += std::format(" [{},{} {}x{}: {}]", viewport[0], viewport[1], viewport[2], viewport[3], draws);
      }
      const double used = 100.0 * static_cast<double>(texels) / std::max(1.0, static_cast<double>(target.width) * target.height);
      shadow_trace.file << std::format("frame {} | {} | {} maps, {:.1f}% |{}\n", shadow_trace.frame, target.description,
                                       target.draws_per_viewport.size(), used, maps);
    }
    shadow_trace.targets.clear();
    ++shadow_trace.frame;
    if (shadow_trace_frames.fetch_sub(1) == 1) {
      shadow_trace.file.close();
      renodx::utils::log::i("[Ryse DLSS] Shadow trace written (ryse_shadow_trace.txt).");
    }
  }
  if (options_measure.active) {
    const auto now = std::chrono::steady_clock::now();
    const std::array<double, 5> frame_values = {
        static_cast<double>(draw_counts.scene_draws.exchange(0)), static_cast<double>(draw_counts.scene_indices.exchange(0)),
        static_cast<double>(draw_counts.shadow_draws.exchange(0)), static_cast<double>(draw_counts.shadow_indices.exchange(0)),
        std::chrono::duration<double, std::milli>(now - options_measure.last_present).count()};
    options_measure.last_present = now;
    if (++options_measure.frame > MEASURE_SETTLE_FRAMES) {
      for (size_t index = 0; index < frame_values.size(); ++index) options_measure.sums[index] += frame_values[index];
    }
    if (options_measure.frame == MEASURE_SETTLE_FRAMES + MEASURE_FRAMES) {
      auto& option = quality_options[options_measure.option];
      const char* choice_name = ChoiceName(option, options_measure.choice);
      const auto& sums = options_measure.sums;
      options_measure.file << std::format("{} | {} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.2f}\n", option.label, choice_name,
                                          sums[0] / MEASURE_FRAMES, sums[1] / MEASURE_FRAMES, sums[2] / MEASURE_FRAMES,
                                          sums[3] / MEASURE_FRAMES, sums[4] / MEASURE_FRAMES);
      options_measure.sums = {};
      options_measure.frame = 0;
      // The next choice, else the next option (its own choice back first).
      if (++options_measure.choice < option.choice_count) {
        option.choice = options_measure.choice;
      } else {
        option.choice = options_measure.saved_choices[options_measure.option];
        options_measure.choice = 0;
        do {
          ++options_measure.option;
        } while (options_measure.option < quality_options.size()
                 && (std::string_view(quality_options[options_measure.option].key) == "EngineQuality"
                     || std::string_view(quality_options[options_measure.option].key) == "TexturePool"));
        if (options_measure.option < quality_options.size()) {
          quality_options[options_measure.option].choice = 0;
        } else {
          options_measure.active = false;
          counting_draws.store(false);
          options_measure.file.close();
          options_measure.result = "Done: ryse_options_measure.txt next to the exe. Your choices are back.";
        }
      }
    }
  }
  // The frame after the captured one ends here.
  if (const uint64_t captured = capture_frame.load(); captured != UINT64_MAX && closing_frame > captured) {
    capture_frame.store(UINT64_MAX);
    SaveCaptures();
  }
  if (changed) {
    // NVIDIA's runtime stays initialized (initializing it freezes the game for seconds); Off frees its feature.
    Reset(true);
    if (mode == AAMode::OFF) {
      const std::lock_guard lock(runtime_mutex);
      runtime.backend.ReleaseFeature();
    }
    unshown_frames.store(0);
  }
  if (mode == AAMode::OFF) {
    SetStatus("Off — original game TAA");
  } else if (missed || changed) {
    std::shared_lock lock(state.mutex);
    if (state.status.find(" active") != std::string::npos || changed) {
      lock.unlock();
      SetStatus("Waiting for the game's TAA: turn Temporal Anti-Aliasing on in the game's settings");
    }
  }

  // Renderer pins (renderer_pins) while DLSS / DLAA runs, once the game draws its world.
  if (taa_frame.load(std::memory_order_relaxed) != UINT64_MAX) {
    for (auto& pin : renderer_pins) {
      if (!pin.searched) {
        pin.searched = true;
        const auto found = rysesonofrome::engine_memory::FindIntCvar(pin.name);
        pin.address = found.value;
        pin.details = found.details;
        renodx::utils::log::i("[Ryse DLSS] ", found.details);
      }
      if (pin.address == nullptr) continue;
      const std::atomic_ref<int> value(*pin.address);
      const int current = value.load(std::memory_order_relaxed);
      if (mode != AAMode::OFF) {
        if (!pin.set) {
          pin.game_value = current;
          pin.set = true;
        }
        if (current != pin.value) {
          value.store(pin.value, std::memory_order_relaxed);
        }
      } else if (pin.set) {
        value.store(pin.game_value, std::memory_order_relaxed);
        pin.set = false;
      }
    }
  }

  // Game settings above its highest (quality_options), once the game draws its world (the game's TAA was seen).
  if (void* console = (taa_frame.load(std::memory_order_relaxed) != UINT64_MAX ? FindGameConsole() : nullptr); console != nullptr) {
    const auto get = [](const QualityCvar& entry) {
      if (entry.engine_offset != 0) {
        if (entry.is_float) {
          return std::atomic_ref<float>(*static_cast<float*>(entry.cvar)).load(std::memory_order_relaxed);
        }
        return static_cast<float>(std::atomic_ref<int>(*static_cast<int*>(entry.cvar)).load(std::memory_order_relaxed));
      }
      if (entry.is_float) {
        return (*reinterpret_cast<float (***)(void*)>(entry.cvar))[CVAR_GET_FLOAT / sizeof(void*)](entry.cvar);
      }
      return static_cast<float>(GetCvar(entry.cvar));
    };
    const auto set = [](const QualityCvar& entry, float value) {
      if (entry.engine_offset != 0) {
        if (entry.is_float) {
          std::atomic_ref<float>(*static_cast<float*>(entry.cvar)).store(value, std::memory_order_relaxed);
        } else {
          std::atomic_ref<int>(*static_cast<int*>(entry.cvar)).store(static_cast<int>(value), std::memory_order_relaxed);
        }
      } else if (entry.is_float) {
        (*reinterpret_cast<void (***)(void*, float)>(entry.cvar))[CVAR_SET_FLOAT / sizeof(void*)](entry.cvar, value);
      } else {
        SetCvar(entry.cvar, static_cast<int>(value));
      }
    };
    for (auto& option : quality_options) {
      const QualityOption* floor_source =
          (option.floor_option >= 0 ? &quality_options[option.floor_option] : nullptr);
      const bool game_own = option.choice == 0 && (floor_source == nullptr || floor_source->choice == 0);
      float value = option.values[option.choice];
      if (floor_source != nullptr) {
        value = std::max(value, floor_source->values[floor_source->choice]);
      }
      for (auto& entry : option.cvars) {
        if (armour_test.applied != 0 && std::strcmp(entry.name, ARMOUR_TESTS[armour_test.applied].cvar) == 0) continue;
        if (!entry.looked_up) {
          if (entry.engine_offset == 0) {
            entry.looked_up = true;
            entry.cvar = GetGameCvar(console, entry.name);
          } else if (uint8_t* cvars = EngineCvars(); cvars != nullptr || screen_res_pin.cvars == nullptr) {
            // Found (else the object is not created yet: again next present), or its code was not found.
            entry.looked_up = true;
            entry.cvar = (cvars != nullptr ? cvars + entry.engine_offset : nullptr);
          }
          if (entry.looked_up) {
            renodx::utils::log::i("[Ryse DLSS] ", entry.name, (entry.cvar != nullptr ? std::format(" = {}", get(entry)) : " not found"));
          }
        }
        if (entry.cvar == nullptr || entry.reverts >= CVAR_REFUSED_REVERTS) continue;
        const float current = get(entry);
        // The game set its own (its spec, e.g. its menu): the new game's value. Its own value back right after a
        // set, again and again: refused (VF_CHEAT cvars are put back every frame, each time with a console error).
        if (entry.set && current != entry.set_value) {
          if (entry.engine_offset == 0 && current == entry.game_value && ++entry.reverts >= CVAR_REFUSED_REVERTS) {
            renodx::utils::log::w("[Ryse DLSS] ", entry.name, " refused: the game puts back ", current);
          }
          entry.set = false;
        }
        if (game_own || option.choice < entry.from_choice) {
          if (entry.set) {
            set(entry, entry.game_value);
            entry.set = false;
          }
          continue;
        }
        if (!entry.set) {
          entry.game_value = current;
        }
        float desired = value;
        if (entry.absolute) {
          desired = entry.values[option.choice];
        } else if (!option.absolute) {
          desired = (entry.inverse ? entry.game_value / value : entry.game_value * value);
        }
        if (entry.max_value > 0.f) {
          desired = std::min(desired, entry.max_value);
        }
        if (entry.min_value > 0.f) {
          desired = std::max(desired, entry.min_value);
        }
        if (!entry.is_float) {
          desired = std::round(desired);
        }
        // Set once per value: a cvar may clamp it (read back as set_value) or react to each set (e.g. reallocate).
        if (!entry.set || desired != entry.requested) {
          set(entry, desired);
          entry.requested = desired;
          entry.set_value = get(entry);
          entry.set = true;
        }
      }
    }
    if (auto& test = armour_test; test.choice != test.applied) {
      const auto get_float = [](void* cvar) {
        return (*reinterpret_cast<float (***)(void*)>(cvar))[CVAR_GET_FLOAT / sizeof(void*)](cvar);
      };
      const auto set_float = [](void* cvar, float value) {
        (*reinterpret_cast<void (***)(void*, float)>(cvar))[CVAR_SET_FLOAT / sizeof(void*)](cvar, value);
      };
      if (test.cvar != nullptr) {
        set_float(test.cvar, test.game_value);
        test.cvar = nullptr;
      }
      test.applied = test.choice;
      test.status.clear();
      if (const char* name = ARMOUR_TESTS[test.applied].cvar; name != nullptr) {
        test.cvar = GetGameCvar(console, name);
        if (test.cvar != nullptr) {
          test.game_value = get_float(test.cvar);
          set_float(test.cvar, ARMOUR_TESTS[test.applied].value);
          test.status = std::format("{}: {:g} -> {:g}", name, test.game_value, get_float(test.cvar));
        } else {
          test.status = std::format("{} not found", name);
        }
        renodx::utils::log::i("[Ryse DLSS] Armour test: ", test.status);
      }
    }
  }

  // The jitter pattern: written every frame it differs (the game sets its own again from its settings).
  // The exe's code is only readable once the game runs, so the search waits for the first frames.
  auto& control = jitter_control;
  const int desired = (mode != AAMode::OFF ? GAME_JITTER_MODE : -1);
  if (desired >= 0 && !control.searched) {
    control.searched = true;
    const auto found = rysesonofrome::engine_memory::FindIntCvar("r_PostAAMode");
    control.value = found.value;
    control.details = found.details;
    if (found.value != nullptr) {
      renodx::utils::log::i("[Ryse DLSS] Found ", found.details);
    } else {
      renodx::utils::log::w("[Ryse DLSS] ", found.details);
    }
  }
  const bool own = mode != AAMode::OFF && own_jitter.load();
  if (own && !control.setup_searched) {
    control.setup_searched = true;
    std::string details;
    uint8_t* site = rysesonofrome::engine_memory::FindPattern(JITTER_SETUP, &details);
    uint8_t* stub = (site != nullptr ? rysesonofrome::engine_memory::AllocateNearExe(4096) : nullptr);
    // rel32 from the end of an instruction to `target`; false if out of reach.
    const auto rel32 = [](const uint8_t* end, const uint8_t* target, uint8_t* out) {
      const int64_t delta = target - end;
      if (delta < std::numeric_limits<int32_t>::min() || delta > std::numeric_limits<int32_t>::max()) return false;
      const auto value = static_cast<int32_t>(delta);
      std::memcpy(out, &value, sizeof(value));
      return true;
    };
    if (site == nullptr) {
      control.jitter_setup.details = details;
    } else if (stub == nullptr) {
      control.jitter_setup.details = details + ", no memory near the game";
    } else {
      int32_t no_jitter_offset;
      std::memcpy(&no_jitter_offset, site + 21, sizeof(no_jitter_offset));
      const uint8_t* no_jitter = site + 25 + no_jitter_offset;
      // movzx ecx, byte ptr [rbp + 0xb0] (the view's flag, used after the jump); mov rax, [rip + offset];
      // mov [r12 + 0xcd0], rax; jmp no_jitter.
      constexpr std::array<uint8_t, 3> LOAD = {0x48, 0x8B, 0x05};
      constexpr std::array<uint8_t, 8> STORE = {0x49, 0x89, 0x84, 0x24, 0xD0, 0x0C, 0x00, 0x00};
      std::memcpy(stub, site, 7);
      std::memcpy(stub + 7, LOAD.data(), LOAD.size());
      std::memcpy(stub + 14, STORE.data(), STORE.size());
      stub[22] = 0xE9;
      std::vector<uint8_t> jump = {0xE9, 0, 0, 0, 0};
      if (rel32(stub + 14, stub + STUB_OFFSET, stub + 10) && rel32(stub + 27, no_jitter, stub + 23)
          && rel32(site + 5, stub, jump.data() + 1)) {
        FlushInstructionCache(GetCurrentProcess(), stub, 27);
        control.stub = stub;
        control.jitter_setup = {
            .address = site,
            .original = std::vector<uint8_t>(site, site + jump.size()),
            .replacement = std::move(jump),
            .details = details,
        };
      } else {
        control.jitter_setup.details = details + ", stub out of reach";
      }
    }
    std::string apply_details;
    if (uint8_t* apply = rysesonofrome::engine_memory::FindPattern(JITTER_APPLY, &apply_details); apply != nullptr) {
      uint8_t* check = apply + PAUSE_FLAG_CHECK;
      control.pause_flag_check = {
          .address = check,
          .original = std::vector<uint8_t>(check, check + 2),
          .replacement = {0x90, 0x90},
          .details = apply_details,
      };
    } else {
      control.pause_flag_check.details = apply_details;
    }
    renodx::utils::log::i("[Ryse DLSS] Jitter setup: ", control.jitter_setup.details, "; pause flag check: ",
                          control.pause_flag_check.details);
  }
  if (control.jitter_setup.address != nullptr && control.jitter_setup.applied != own) {
    const bool applied = rysesonofrome::engine_memory::ApplyCodePatch(&control.jitter_setup, own)
                         && (control.pause_flag_check.address == nullptr
                             || rysesonofrome::engine_memory::ApplyCodePatch(&control.pause_flag_check, own));
    renodx::utils::log::i("[Ryse DLSS] Jitter: ", (own ? "the addon's own" : "the game's"),
                          (applied ? "" : std::string(" (failed: ") + control.jitter_setup.details + "; "
                                              + control.pause_flag_check.details + ")"));
  }
  if (control.jitter_setup.applied) {
    // The next position in pixels (-0.5 to 0.5) as NDC of the render size; DLSS gets the jitter measured in the frame.
    uint32_t width;
    uint32_t height;
    uint32_t display_width;
    {
      const std::shared_lock lock(state.mutex);
      width = state.render_width;
      height = state.render_height;
      display_width = state.display_width;
    }
    // 8 x (display / render)^2 positions (NVIDIA): 8 for DLAA, 32 at 50%, 72 at 33%.
    const float ratio = (width != 0 && display_width > width ? static_cast<float>(display_width) / static_cast<float>(width) : 1.f);
    const uint32_t phases = std::clamp(static_cast<uint32_t>(std::ceil(JITTER_PHASES * ratio * ratio - 0.01f)), JITTER_PHASES, MAX_JITTER_PHASES);
    const auto halton = [](uint32_t index, uint32_t base) {
      float fraction = 1.f;
      float value = 0.f;
      for (; index > 0; index /= base) {
        fraction /= static_cast<float>(base);
        value += fraction * static_cast<float>(index % base);
      }
      return value;
    };
    control.phase = (control.phase + 1) % phases;
    const std::array<float, 2> offset = {
        (width != 0 ? (halton(control.phase + 1, 2) - .5f) * 2.f / static_cast<float>(width) : 0.f),
        (height != 0 ? (halton(control.phase + 1, 3) - .5f) * 2.f / static_cast<float>(height) : 0.f),
    };
    std::atomic_ref<uint64_t>(*reinterpret_cast<uint64_t*>(control.stub + STUB_OFFSET))
        .store(std::bit_cast<uint64_t>(offset), std::memory_order_relaxed);
  }
  // The Rendering Resolution: the mode's while DLSS is on (DLAA: the display's), set once unchanged for SETTLE_PRESENTS
  // and again whenever the game sets its own (its menu); the game's own back with DLSS off.
  {
    auto& rendering = resolution;
    if (changed) {
      rendering.problem.clear();
      rendering.writes = 0;
    }
    const bool saved = saved_game_size[0] > 0 && saved_game_size[1] > 0;
    if (!rendering.searched && (mode != AAMode::OFF || saved)) {
      rendering.searched = true;
      std::string details;
      if (const uint8_t* scene = rysesonofrome::engine_memory::FindPattern(RENDER_SCENE, &details); scene != nullptr) {
        int32_t displacement;
        std::memcpy(&displacement, scene + 23, sizeof(displacement));
        const uint8_t* renderer = scene + 27 + displacement;
        rendering.width = *reinterpret_cast<void* const*>(renderer + WIDTH_CVAR);
        rendering.height = *reinterpret_cast<void* const*>(renderer + HEIGHT_CVAR);
        rendering.native_res = *reinterpret_cast<void* const*>(renderer + NATIVE_RES_CVAR);
      }
      if (rendering.width == nullptr || rendering.height == nullptr || rendering.native_res == nullptr) {
        rendering.width = nullptr;
        rendering.problem = std::format("the game's Rendering Resolution was not found (RT_RenderScene {})", details);
        renodx::utils::log::w("[Ryse DLSS] ", rendering.problem);
      } else {
        renodx::utils::log::i("[Ryse DLSS] Rendering Resolution ", GetCvar(rendering.width), "x", GetCvar(rendering.height),
                              ", Native Upscaling ", GetCvar(rendering.native_res), " (RT_RenderScene ", details, ")");
        // The last session ended with the addon's: the game's own comes from ReShade.ini.
        if (saved) {
          rendering.controlling = true;
          rendering.game_size = saved_game_size;
          rendering.target = {GetCvar(rendering.width), GetCvar(rendering.height)};
        }
      }
    }
    uint32_t display_width;
    uint32_t display_height;
    {
      const std::shared_lock lock(state.mutex);
      display_width = state.display_width;
      display_height = state.display_height;
    }
    if (rendering.width != nullptr && rendering.problem.empty() && display_width != 0 && display_height != 0) {
      const std::array<int, 2> now = {GetCvar(rendering.width), GetCvar(rendering.height)};
      const auto set = [&](const std::array<int, 2>& size) {
        SetCvar(rendering.width, size[0]);
        SetCvar(rendering.height, size[1]);
        rendering.target = {GetCvar(rendering.width), GetCvar(rendering.height)};
        rendering.last_seen = rendering.target;
        renodx::utils::log::i("[Ryse DLSS] Rendering Resolution ", now[0], "x", now[1], " -> ", rendering.target[0], "x",
                              rendering.target[1]);
      };
      // The game's own, kept in ReShade.ini while the addon's is set (0: none).
      const auto save = [](const std::array<int, 2>& size) {
        saved_game_size = size;
        reshade::set_config_value(nullptr, "Ryse AA", "GameRenderWidth", size[0]);
        reshade::set_config_value(nullptr, "Ryse AA", "GameRenderHeight", size[1]);
      };
      // The game set a size of its own this present (its menu, its settings at startup); not the addon's last one.
      const bool game_set = rendering.controlling && now != rendering.last_seen && now != rendering.target;
      rendering.last_seen = now;
      if (mode != AAMode::OFF) {
        if (!rendering.controlling || game_set) {
          rendering.game_size = now;
        }
        if (game_set) {
          ++rendering.writes;
        }
        // The display's exact aspect ratio: steps of its reduced ratio (16x9 at 4K; the HDR addon upgrades only textures
        // of that ratio, v20: 2227x1253 stayed 8-bit), each axis rounded where the steps are too coarse. At least the
        // game's minimum (512x300: it renders smaller sizes at that). Below the display size only with Native Upscaling
        // (else the game changes the display mode).
        const float scale = (mode == AAMode::DLAA ? 1.f : ModeScale(mode));
        const uint32_t divisor = std::gcd(display_width, display_height);
        std::array<int, 2> mode_size;
        if (divisor >= 16) {
          const uint32_t unit_width = display_width / divisor;
          const uint32_t unit_height = display_height / divisor;
          const uint32_t min_steps = std::max((MIN_RENDER_WIDTH + unit_width - 1) / unit_width, (MIN_RENDER_HEIGHT + unit_height - 1) / unit_height);
          const uint32_t steps = std::clamp(static_cast<uint32_t>(std::lround(divisor * scale)), std::min(min_steps, divisor), divisor);
          mode_size = {static_cast<int>(unit_width * steps), static_cast<int>(unit_height * steps)};
        } else {
          mode_size = {
              std::clamp(static_cast<int>(std::lround(display_width * scale)), static_cast<int>(std::min(MIN_RENDER_WIDTH, display_width)),
                         static_cast<int>(display_width)),
              std::clamp(static_cast<int>(std::lround(display_height * scale)), static_cast<int>(std::min(MIN_RENDER_HEIGHT, display_height)),
                         static_cast<int>(display_height)),
          };
        }
        const bool full = mode_size[0] == static_cast<int>(display_width) && mode_size[1] == static_cast<int>(display_height);
        rendering.native_off = !full && GetCvar(rendering.native_res) == 0;
        const std::array<int, 2> desired = (rendering.native_off ? rendering.game_size : mode_size);
        if (desired != rendering.pending) {
          rendering.pending = desired;
          rendering.pending_presents = 0;
        } else {
          ++rendering.pending_presents;
        }
        if (rendering.writes > 20) {
          rendering.problem = "the game keeps setting its own Rendering Resolution";
          renodx::utils::log::w("[Ryse DLSS] ", rendering.problem);
          set(rendering.game_size);
          rendering.controlling = false;
          save({});
        } else if (rendering.pending_presents >= SETTLE_PRESENTS && (!rendering.controlling || desired != now)) {
          if (!rendering.controlling) save(rendering.game_size);
          if (desired != now) {
            set(desired);
          } else {
            rendering.target = now;
          }
          rendering.controlling = true;
        }
        if (++rendering.window >= 600) {
          rendering.window = 0;
          rendering.writes = 0;
        }
      } else if (rendering.controlling) {
        renodx::utils::log::i("[Ryse DLSS] Rendering Resolution back to the game's own");
        if (rendering.game_size != now) set(rendering.game_size);
        rendering.controlling = false;
        save({});
      }
    }

    // Vegetation detail at the display width while rendering below it (VEGETATION_SCREEN_RES).
    auto& pin = screen_res_pin;
    const bool below = rendering.controlling && rendering.target[0] > 0 && rendering.target[0] < static_cast<int>(display_width);
    // Texture LOD bias (biased_samplers): below the display width, log2(render / display width), minus 1 (Sharper) or
    // 2 (Sharpest).
    float bias = 0.f;
    if (below && texture_lod_bias_choice != 0) {
      bias = std::log2(static_cast<float>(rendering.target[0]) / static_cast<float>(display_width))
             - static_cast<float>(texture_lod_bias_choice - 1);
    }
    texture_lod_bias.store(bias, std::memory_order_relaxed);
    if (uint8_t* cvars = (rendering.width != nullptr ? EngineCvars() : nullptr); cvars != nullptr) {
      const std::atomic_ref<int> value(*reinterpret_cast<int*>(cvars + SCREEN_RES_CVAR));
      const int current = value.load(std::memory_order_relaxed);
      if (below) {
        if (!pin.set) {
          pin.game_value = current;
          pin.set = true;
        }
        if (current != static_cast<int>(display_width)) {
          value.store(static_cast<int>(display_width), std::memory_order_relaxed);
        }
      } else if (pin.set) {
        value.store(pin.game_value, std::memory_order_relaxed);
        pin.set = false;
      }
    }
  }
  if (control.value == nullptr) return;
  const std::atomic_ref<int> cvar(*control.value);
  const int current = cvar.load(std::memory_order_relaxed);
  if (current != control.last_seen) {
    if (control.logged_changes < 20) {
      ++control.logged_changes;
      renodx::utils::log::i("[Ryse DLSS] r_PostAAMode is ", current, (control.last_seen >= 0 ? " (the game set it)" : ""));
    }
    control.last_seen = current;
  }
  if (desired >= 0 && current != desired) {
    if (control.controlling) ++control.rewrites;
    control.game_value = current;
    control.controlling = true;
    cvar.store(desired, std::memory_order_relaxed);
    control.last_seen = desired;
  } else if (desired < 0 && control.controlling) {
    cvar.store(control.game_value, std::memory_order_relaxed);
    control.last_seen = control.game_value;
    control.controlling = false;
  }
  postaamode.store(control.last_seen);
}

void OnOverlay(reshade::api::effect_runtime*) {
  if (advanced) {
    const std::shared_lock lock(state.mutex);
    overlay_frame.store(state.frame + 1);
  }
  if (int settings_mode = (advanced ? 1 : 0); ImGui::Combo("Settings mode", &settings_mode, "Simple\0Advanced (For Debugging)\0")) {
    advanced = settings_mode == 1;
    reshade::set_config_value(nullptr, "Ryse AA", "SettingsMode", settings_mode);
  }
  auto mode = requested_mode.load();
  if (ImGui::BeginCombo("AA mode", AA_MODES[static_cast<size_t>(mode)].label)) {
    for (const AAMode option : MENU_ORDER) {
      if (ImGui::Selectable(AA_MODES[static_cast<size_t>(option)].label, option == mode)) {
        mode = option;
        requested_mode.store(mode);
        reshade::set_config_value(nullptr, "Ryse AA", "Mode", static_cast<int>(mode));
      }
    }
    ImGui::EndCombo();
  }
  if (float value = custom_scale.load(); mode == AAMode::DLSS_CUSTOM && ImGui::SliderFloat("Render scale", &value, MIN_CUSTOM_SCALE, 100.f, "%.0f%%")) {
    value = std::clamp(std::round(value), MIN_CUSTOM_SCALE, 100.f);
    custom_scale.store(value);
    reshade::set_config_value(nullptr, "Ryse AA", "CustomScale", value);
  }
  const auto selected_preset = std::ranges::find(PRESETS, preset.load(), &PresetSettings::value);
  if (ImGui::BeginCombo("DLSS preset", (selected_preset != PRESETS.end() ? selected_preset->label : PRESETS.back().label))) {
    for (const auto& option : PRESETS) {
      if (ImGui::Selectable(option.label, option.value == preset.load())) {
        preset.store(option.value);
        reshade::set_config_value(nullptr, "Ryse AA", "Preset", option.value);
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::Combo("Texture LOD bias", &texture_lod_bias_choice,
                   "Off (the game's own)\0"
                   "Matched (as sharp as at your output resolution)\0"
                   "Sharper (one step sharper; NVIDIA's recommendation)\0"
                   "Sharpest (two steps sharper; can shimmer)\0")) {
    reshade::set_config_value(nullptr, "Ryse AA", "TextureLodBias", texture_lod_bias_choice);
  }
  if (ImGui::IsItemHovered()) {
    // The bias each choice adds at the current mode's scale (the render width is rounded, so in game it can differ
    // slightly).
    std::string example;
    if (const float scale = ModeScale(mode); mode != AAMode::OFF && scale < 1.f) {
      const float matched = std::log2(scale);
      example = std::format("\n\nAt {:g}% (bias added to the game's, lower = sharper): Off 0.0, Matched {:.1f}, Sharper {:.1f}, "
                            "Sharpest {:.1f}.",
                            std::round(scale * 100.f), matched, matched - 1.f, matched - 2.f);
    }
    ImGui::SetTooltip("How sharp textures are while DLSS renders below 100%%. The game renders fewer pixels and picks\n"
                      "blurrier texture levels to match them.\n"
                      "Matched: textures as sharp as at your output resolution.\n"
                      "Sharper / Sharpest: one / two steps sharper than that (more detail, can shimmer).\n"
                      "DLAA and Off are not affected.%s",
                      example.c_str());
  }
  ImGui::TextWrapped("Changing the AA mode or render scale resizes the game's render targets: its auto exposure restarts "
                     "(too bright or too dark for a moment, then it settles) and textures reload.");

  std::string status;
  uint64_t completed;
  std::array<uint32_t, 2> extent;
  std::array<uint32_t, 2> output;
  std::array<uint32_t, 2> display;
  rysesonofrome::dlss::PerformanceSummary performance;
  {
    const std::shared_lock lock(state.mutex);
    status = state.status;
    completed = state.completed;
    extent = {state.render_width, state.render_height};
    output = {state.output_width, state.output_height};
    display = {state.display_width, state.display_height};
    performance = state.performance;
  }
  ImGui::Separator();
  ImGui::TextWrapped("%s", status.c_str());
  if (completed != 0 && status.find(" active") != std::string::npos) {
    if (output != extent) {
      ImGui::Text("Input %ux%u -> output %ux%u", extent[0], extent[1], output[0], output[1]);
    } else {
      ImGui::Text("Render resolution %ux%u%s", extent[0], extent[1],
                  (extent[0] < display[0] ? " (the game's own upscaling follows)" : ""));
    }
  }
  const auto& rendering = resolution;
  if (mode != AAMode::OFF) {
    if (!rendering.problem.empty()) {
      ImGui::TextWrapped("The addon does not set the Rendering Resolution: %s.", rendering.problem.c_str());
    } else if (rendering.native_off) {
      ImGui::TextWrapped("Turn on Native Upscaling in the game's options: DLSS renders below the display resolution only with it.");
    }
    if (const uint64_t unshown = unshown_frames.load(); unshown != 0) {
      ImGui::TextWrapped("Frames DLSS upscaled but the game showed its own (no native upscale or post pass seen): %llu",
                         static_cast<unsigned long long>(unshown));
    }
  }
  const uint32_t phases = diagnostics.jitter_phases.load();
  std::string jitter;
  if (!diagnostics.jitter_found.load()) {
    jitter = "not found";
  } else if (phases == 0) {
    jitter = "measuring";
  } else if (phases == 1) {
    jitter = "off (always the same position)";
  } else if (phases > 1024) {
    jitter = "random (no repeat in 1024 frames)";
  } else {
    jitter = std::format("{} positions", phases);
  }
  const auto& control = jitter_control;
  const bool active = requested_mode.load() != AAMode::OFF && completed != 0;
  if (active && (!diagnostics.jitter_found.load() || (phases != 0 && phases < 8))) {
    if (control.searched && control.value == nullptr) {
      ImGui::TextWrapped("The game's camera jitter is %s, and the addon could not find r_PostAAMode in the game (details in "
                         "ReShade.log). DLSS needs 8 or more positions.", jitter.c_str());
    } else if (!own_jitter.load()) {
      ImGui::TextWrapped("The game's camera jitter is %s: the game stops it in slow motion, hit stops and pauses. Choose "
                         "Jitter: the addon's own.", jitter.c_str());
    } else if (control.setup_searched && !control.jitter_setup.applied) {
      ImGui::TextWrapped("The game's camera jitter is %s, and the addon could not take it over: %s.", jitter.c_str(),
                         control.jitter_setup.details.c_str());
    } else if (diagnostics.jittered_uploads.load() == 0) {
      ImGui::TextWrapped("The game is not jittering its camera right now (jitter %s). DLSS then only smooths edges.", jitter.c_str());
    } else {
      ImGui::TextWrapped("The game's camera jitter is %s. DLSS needs 8 or more positions.", jitter.c_str());
    }
  }
  const auto& version = dlss_library.version;
  if (dlss_library.folder.empty()) {
    ImGui::TextWrapped("nvngx_dlss.dll is missing: download the latest nvngx_dlss.dll and put it next to this addon (the game's Bin64 folder), then restart the game.");
  } else if (version[0] != 0 && std::array<uint32_t, 2>{version[0], version[1]} < rysesonofrome::dlss::MINIMUM_RUNTIME_VERSION) {
    ImGui::TextWrapped("nvngx_dlss.dll %u.%u.%u is older than %u.%u: presets M and L are not available, NVIDIA uses its default. Download the latest nvngx_dlss.dll.",
                       version[0], version[1], version[2], rysesonofrome::dlss::MINIMUM_RUNTIME_VERSION[0],
                       rysesonofrome::dlss::MINIMUM_RUNTIME_VERSION[1]);
  }

  ImGui::Separator();
  ImGui::TextWrapped("Game settings above its High, applied live. In-game setting: what you set in the game. Nx: N "
                     "times what the in-game graphics preset sets (numbers in brackets: at Very High).");
  // A button that sets every option to the choice `pick` gives it, saved.
  // preset: -1 Reset, a GRAPHICS_PRESETS index, or GRAPHICS_PRESETS.size() for MAXED OUT.
  const auto preset_button = [](const char* label, const char* tooltip, int preset) {
    if (ImGui::Button(label)) {
      for (auto& option : quality_options) {
        int choice = option.choice_count - 1;
        if (preset < 0) {
          choice = 0;
        } else if (preset < static_cast<int>(GRAPHICS_PRESETS.size())) {
          choice = option.preset_choices[preset];
        }
        option.choice = choice;
        reshade::set_config_value(nullptr, "Ryse Game Settings", option.key, option.choice);
      }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", tooltip);
    }
  };
  preset_button("Reset", "Every option back to the in-game setting.", -1);
  for (int preset = 0; preset < static_cast<int>(GRAPHICS_PRESETS.size()); ++preset) {
    ImGui::SameLine();
    preset_button(GRAPHICS_PRESETS[preset].name, GRAPHICS_PRESETS[preset].tooltip, preset);
  }
  ImGui::SameLine();
  preset_button("MAXED OUT", "Every option at its highest (distances and details 64x).",
                static_cast<int>(GRAPHICS_PRESETS.size()));
  const bool world_seen = taa_frame.load(std::memory_order_relaxed) != UINT64_MAX;
  for (auto& option : quality_options) {
    // A slider over the items, showing the current one (as RenoDX's labelled settings).
    if (ImGui::SliderInt(option.label, &option.choice, 0, option.choice_count - 1, ChoiceName(option, option.choice),
                         ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
      reshade::set_config_value(nullptr, "Ryse Game Settings", option.key, option.choice);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", option.tooltip);
    }
    if (!advanced) continue;
    std::string values;
    for (const auto& entry : option.cvars) {
      std::string value = "in-game";
      if (entry.looked_up && entry.cvar == nullptr) {
        value = "not found";
      } else if (entry.reverts >= CVAR_REFUSED_REVERTS) {
        value = "refused (the game puts its value back)";
      } else if (entry.set) {
        value = std::format("{:g} -> {:g}", entry.game_value, entry.set_value);
      } else if (!world_seen) {
        value = "waiting for the game's world";
      }
      values += std::format("{}{}: {}", (values.empty() ? "" : ", "), entry.name, value);
    }
    ImGui::TextWrapped("%s", values.c_str());
  }
  if (!world_seen) {
    ImGui::TextWrapped("They apply once the game draws its world with its Temporal Anti-Aliasing on.");
  }
  if (!advanced) return;

  ImGui::Separator();
  ImGui::TextWrapped("Presets: E is the last CNN model, K the first transformer, M and L the second; L is the heaviest and in Ryse looks like M (the default). A preset forced in the NVIDIA app or NVIDIA Profile Inspector overrides this one.");
  ImGui::TextWrapped("DLAA and DLSS replace the game's TAA pass (and its SMAA). DLSS sets the game's Rendering Resolution for the mode (any size; "
                     "needs Native Upscaling on; each change resizes the game's textures) and its result goes through the game's post effects "
                     "(sharpening, film grain, lens flares) at the display resolution, in place of the game's upscale. The game's own Rendering "
                     "Resolution comes back with AA mode Off.");
  if (rendering.controlling) {
    ImGui::Text("Rendering Resolution %dx%d (the game's own: %dx%d)", rendering.target[0], rendering.target[1], rendering.game_size[0],
                rendering.game_size[1]);
  }
  if (!dlss_library.folder.empty()) {
    ImGui::TextWrapped("nvngx_dlss.dll %u.%u.%u %s (%s).", version[0], version[1], version[2], dlss_library.place,
                       reinterpret_cast<const char*>(std::filesystem::path(dlss_library.folder).u8string().c_str()));
  }
  ImGui::Text("Successful AA frames: %llu", static_cast<unsigned long long>(completed));
  if (diagnostics.measured.load()) {
    // Also measured while DLSS is off.
    // The values change every frame (one position per frame): only the count is readable. Both are in the log.
    ImGui::Text("Game jitter: %s", jitter.c_str());
    ImGui::Text("Jitter source: %s; camera uploads this frame %u, jittered main camera %u",
                (diagnostics.from_upload.load() ? "an earlier upload (the bound camera is not jittered)" : "the bound camera"),
                diagnostics.camera_uploads.load(), diagnostics.jittered_uploads.load());
  }
  ImGui::Text("Texture LOD bias now: %.2f (game samplers seen: %zu)", texture_lod_bias.load(std::memory_order_relaxed),
              biased_samplers.size());
  for (const auto& pin : renderer_pins) {
    if (pin.address != nullptr) {
      ImGui::Text("%s: %d%s", pin.name, *pin.address, (pin.set ? std::format(" (the game's: {})", pin.game_value).c_str() : ""));
    } else if (pin.searched) {
      ImGui::TextWrapped("%s not found: %s", pin.name, pin.details.c_str());
    }
  }
  if (completed != 0) {
    ImGui::Text("NVIDIA history resets: skipped frames %llu, restarts %llu",
                static_cast<unsigned long long>(diagnostics.skipped_frames.load()),
                static_cast<unsigned long long>(diagnostics.restarts.load()));
  }
  if (control.value != nullptr) {
    ImGui::Text("r_PostAAMode: %d%s", control.last_seen,
                (control.controlling ? std::format(" (the game's own: {}; set again by the game {} times)", control.game_value, control.rewrites).c_str()
                                     : " (the game's own)"));
  } else if (control.searched) {
    ImGui::TextWrapped("r_PostAAMode not found: %s", control.details.c_str());
  }
  if (control.setup_searched) {
    ImGui::TextWrapped("Jitter setup: %s%s; pause flag check: %s%s", control.jitter_setup.details.c_str(),
                       (control.jitter_setup.applied ? ", replaced by the addon's own jitter" : ""),
                       control.pause_flag_check.details.c_str(), (control.pause_flag_check.applied ? ", skipped" : ""));
  }
  if (performance.gpu_samples != 0) {
    ImGui::Separator();
    ImGui::Text("GPU input preparation: %.3f ms", performance.gpu_ms[1]);
    ImGui::Text("GPU NVIDIA evaluation: %.3f ms", performance.gpu_ms[2]);
    ImGui::Text("GPU copy into the game's target: %.3f ms", performance.gpu_ms[3]);
    ImGui::TextWrapped("Averages since selecting this AA mode, measured only while Advanced is open.");
  }
  ImGui::Separator();
  if (ImGui::Button("Log one frame's passes after the TAA (debug, ReShade.log)")) {
    trace_requested.store(true);
  }
  if (ImGui::Button("Save two upscaled frames' stages (debug, PNGs next to the exe)")) {
    capture_requested.store(true);
    capture_result = "Waiting for an upscaled frame (a DLSS mode below 100%)";
  }
  if (!capture_result.empty()) ImGui::TextWrapped("%s", capture_result.c_str());
  ImGui::BeginDisabled(shadow_trace_frames.load() != 0);
  if (ImGui::Button("Trace the shadow maps for 300 frames (debug, ryse_shadow_trace.txt next to the exe)")) {
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    const std::lock_guard lock(shadow_trace.mutex);
    shadow_trace.file.open(std::filesystem::path(exe).parent_path() / L"ryse_shadow_trace.txt", std::ios::trunc);
    if (shadow_trace.file) {
      shadow_trace.file << "Graphics cvars: game's value -> the addon's request -> what the game kept\n";
      for (const auto& option : quality_options) {
        shadow_trace.file << std::format("{} (choice {}):\n", option.label, option.choice);
        for (const auto& entry : option.cvars) {
          if (entry.cvar == nullptr) {
            shadow_trace.file << std::format("  {}: not found\n", entry.name);
          } else if (entry.set) {
            shadow_trace.file << std::format("  {}: {} -> {} -> {}\n", entry.name, entry.game_value, entry.requested, entry.set_value);
          } else {
            shadow_trace.file << std::format("  {}: the game's\n", entry.name);
          }
        }
      }
      shadow_trace.file << "\nPer frame: depth target | maps, share of the target | [x,y width x height: draws]\n";
      shadow_trace.frame = 0;
      shadow_trace.targets.clear();
      shadow_trace_frames.store(SHADOW_TRACE_FRAMES);
      shadow_trace_result = "Tracing: stand still where the shadows flicker for a few seconds.";
    } else {
      shadow_trace_result = "Could not create ryse_shadow_trace.txt next to the exe.";
    }
  }
  ImGui::EndDisabled();
  if (!shadow_trace_result.empty()) ImGui::TextWrapped("%s", shadow_trace_result.c_str());
  ImGui::BeginDisabled(options_measure.active);
  if (ImGui::Button("Measure every option's choices (debug, a few minutes, ryse_options_measure.txt)")) {
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    options_measure.file.open(std::filesystem::path(exe).parent_path() / L"ryse_options_measure.txt", std::ios::trunc);
    if (options_measure.file) {
      options_measure.file << std::format("Averages over {} frames, {} frames after each choice. The other options as you set them.\n"
                                          "Option | choice | scene draws | scene indices | shadow draws | shadow indices | frame ms\n",
                                          MEASURE_FRAMES, MEASURE_SETTLE_FRAMES);
      for (size_t index = 0; index < quality_options.size(); ++index) {
        options_measure.saved_choices[index] = quality_options[index].choice;
      }
      // The first option measured: the graphics preset is skipped.
      options_measure.option = 1;
      options_measure.choice = 0;
      options_measure.frame = 0;
      options_measure.sums = {};
      quality_options[options_measure.option].choice = 0;
      draw_counts.scene_draws.store(0);
      draw_counts.scene_indices.store(0);
      draw_counts.shadow_draws.store(0);
      draw_counts.shadow_indices.store(0);
      options_measure.last_present = std::chrono::steady_clock::now();
      options_measure.active = true;
      counting_draws.store(true);
      options_measure.result = "Measuring: stand still, do not touch anything until it says done.";
    } else {
      options_measure.result = "Could not create ryse_options_measure.txt next to the exe.";
    }
  }
  ImGui::EndDisabled();
  if (!options_measure.result.empty()) ImGui::TextWrapped("%s", options_measure.result.c_str());
  if (ImGui::BeginCombo("Armour test (temporary)", ARMOUR_TESTS[armour_test.choice].label)) {
    for (int index = 0; index < static_cast<int>(ARMOUR_TESTS.size()); ++index) {
      if (ImGui::Selectable(ARMOUR_TESTS[index].label, armour_test.choice == index)) armour_test.choice = index;
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Stand about 1 m from a soldier whose armour grows up close and try each test: the one that makes\n"
                      "it look like it does from far away names the system. Off puts the game's value back.");
  }
  if (!armour_test.status.empty()) ImGui::TextWrapped("%s", armour_test.status.c_str());
  if (ImGui::Button("Save the game's code for analysis (debug, about 55 MB)")) {
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    saved_image = rysesonofrome::engine_memory::SaveImage(std::filesystem::path(exe).parent_path() / L"ryse_image.bin");
    renodx::utils::log::i("[Ryse DLSS] Saved the game's image: ", saved_image);
  }
  if (!saved_image.empty()) ImGui::TextWrapped("%s", saved_image.c_str());
  if (ImGui::Button("Save the game's settings (debug, ryse_cvars.txt next to the exe)")) {
    void* console = FindGameConsole();
    // The memory a game pointer leads to is committed and accessible for `size` bytes.
    const auto readable = [](const void* address, size_t size) {
      MEMORY_BASIC_INFORMATION info = {};
      return address != nullptr && VirtualQuery(address, &info, sizeof(info)) != 0 && info.State == MEM_COMMIT
             && (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0
             && static_cast<const uint8_t*>(address) + size <= static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize;
    };
    const uint8_t* variables = nullptr;
    if (readable(console, CONSOLE_VARIABLES + sizeof(void*))) {
      variables = *reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(console) + CONSOLE_VARIABLES);
    }
    if (console == nullptr) {
      saved_cvars = std::format("The game's console was not found ({})", game_console.details);
    } else if (!readable(variables, 0x30) || variables[0x29] == 0) {
      saved_cvars = std::format("The game's console variables were not found (console {}, {})", console, game_console.details);
    } else {
      const auto module = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
      const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
      const uint8_t* module_end = module + nt->OptionalHeader.SizeOfImage;
      const auto child = [](const uint8_t* node, size_t offset) { return *reinterpret_cast<const uint8_t* const*>(node + offset); };
      const int restricted = *game_console.restricted;
      std::wstring exe(32768, L'\0');
      exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
      const auto path = std::filesystem::path(exe).parent_path() / L"ryse_cvars.txt";
      std::ofstream file(path, std::ios::trunc);
      uint32_t count = 0;
      std::string problem;
      for (const uint8_t* node = child(variables, 0); node != variables && count < 100000; ++count) {
        if (!readable(node, 0x30)) {
          problem = std::format(", stopped at an unreadable node {}", static_cast<const void*>(node));
          break;
        }
        const char* name = *reinterpret_cast<const char* const*>(node + 0x18);
        void* cvar = *reinterpret_cast<void* const*>(node + 0x20);
        void* const* table = (readable(cvar, sizeof(void*)) ? *static_cast<void* const* const*>(cvar) : nullptr);
        const bool valid_cvar = readable(table, CVAR_GET_FLAGS + sizeof(void*))
                                && reinterpret_cast<const uint8_t*>(table) >= module
                                && reinterpret_cast<const uint8_t*>(table) < module_end;
        if (readable(name, 1)) {
          file << name;
        }
        if (valid_cvar) {
          const char* value = reinterpret_cast<const char* (*)(void*)>(table[CVAR_GET_STRING / sizeof(void*)])(cvar);
          const int flags = reinterpret_cast<int (*)(void*)>(table[CVAR_GET_FLAGS / sizeof(void*)])(cvar);
          file << " = " << (readable(value, 1) ? value : "?") << std::format("  [flags {:#x}]", flags);
        } else {
          file << " (not a variable the addon can read)";
        }
        file << '\n';
        // In-order successor: the right subtree's first node, else the first ancestor reached from a left child.
        if (const uint8_t* right = child(node, 0x10); right[0x29] == 0) {
          node = right;
          while (child(node, 0)[0x29] == 0) {
            node = child(node, 0);
          }
        } else {
          const uint8_t* parent = child(node, 8);
          while (parent[0x29] == 0 && node == child(parent, 0x10)) {
            node = parent;
            parent = child(node, 8);
          }
          node = parent;
        }
      }
      const void* width = GetGameCvar(console, "r_Width");
      saved_cvars = std::format("Saved {} console variables to {}{}; con_restricted {}; GetCVar(r_Width) {} ({}){}", count,
                                reinterpret_cast<const char*>(path.u8string().c_str()), problem, restricted, width,
                                (width == resolution.width ? "the renderer's" : "not the renderer's, or DLSS was never on"),
                                (file ? "" : "; writing failed"));
    }
    renodx::utils::log::i("[Ryse DLSS] ", saved_cvars);
  }
  if (!saved_cvars.empty()) ImGui::TextWrapped("%s", saved_cvars.c_str());
  ImGui::Separator();
  if (int source = (own_jitter.load() ? 0 : 1);
      ImGui::Combo("Jitter", &source, "The addon's own (always on)\0The game's (stops in slow motion and pauses)\0")) {
    own_jitter.store(source == 0);
    reshade::set_config_value(nullptr, "Ryse AA", "OwnJitter", source == 0);
    // Measures the new jitter from the start; NVIDIA's history restarts.
    Reset(true);
  }
}

}  // namespace

extern "C" __declspec(dllexport) const char* NAME = "RenoDX Ryse: Son of Rome DLAA / DLSS";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "DLAA / DLSS in place of the game's TAA, and graphics settings above its High. Independent of HDR, with stock TAA fallback.";

extern "C" __declspec(dllexport) bool AddonInit(HMODULE module, HMODULE) {
  if (!registered) {
    if (!reshade::register_addon(module)) return false;
    registered = true;
  }
  if (attached) return true;
  // Settings apply at launch, without opening the overlay.
  if (int saved_mode = 0; reshade::get_config_value(nullptr, "Ryse AA", "Mode", saved_mode)
                          && saved_mode >= 0 && static_cast<size_t>(saved_mode) < AA_MODES.size()) {
    requested_mode.store(static_cast<AAMode>(saved_mode));
  }
  if (uint32_t saved_preset = 0; reshade::get_config_value(nullptr, "Ryse AA", "Preset", saved_preset)
                                 && std::ranges::find(PRESETS, saved_preset, &PresetSettings::value) != PRESETS.end()) {
    preset.store(saved_preset);
  }
  if (int settings_mode = 0; reshade::get_config_value(nullptr, "Ryse AA", "SettingsMode", settings_mode)) advanced = settings_mode == 1;
  if (bool value = true; reshade::get_config_value(nullptr, "Ryse AA", "OwnJitter", value)) own_jitter.store(value);
  if (float value = 50.f; reshade::get_config_value(nullptr, "Ryse AA", "CustomScale", value) && std::isfinite(value)) {
    custom_scale.store(std::clamp(std::round(value), MIN_CUSTOM_SCALE, 100.f));
  }
  for (auto& option : quality_options) {
    if (int choice = 0; reshade::get_config_value(nullptr, "Ryse Game Settings", option.key, choice)) {
      option.choice = std::clamp(choice, 0, option.choice_count - 1);
    }
  }
  if (int choice = 2; reshade::get_config_value(nullptr, "Ryse AA", "TextureLodBias", choice)) {
    texture_lod_bias_choice = std::clamp(choice, 0, 3);
  }
  reshade::get_config_value(nullptr, "Ryse AA", "GameRenderWidth", saved_game_size[0]);
  reshade::get_config_value(nullptr, "Ryse AA", "GameRenderHeight", saved_game_size[1]);
  dlss_library = rysesonofrome::dlss::FindDlssLibrary();
  if (dlss_library.folder.empty()) {
    renodx::utils::log::w("[Ryse DLSS] nvngx_dlss.dll not found next to the addon or the game.");
  } else {
    renodx::utils::log::i("[Ryse DLSS] nvngx_dlss.dll ", dlss_library.version[0], ".", dlss_library.version[1], ".",
                          dlss_library.version[2], " ", dlss_library.place, ".");
  }
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::draw>(OnDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
  reshade::register_event<reshade::addon_event::copy_resource>(OnTraceCopy);
  reshade::register_event<reshade::addon_event::copy_texture_region>(OnTraceCopyRegion);
  reshade::register_event<reshade::addon_event::init_sampler>(OnInitSampler);
  reshade::register_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);
  reshade::register_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
  reshade::register_overlay("Ryse DLSS", OnOverlay);
  command_action::Register(OnTaaDraw, {.shader_hash = TAA_SHADER, .command_types = command_action::COMMAND_TYPE_DIRECT_DRAW});
  for (const uint32_t shader_hash : {SMAA_EDGES_SHADER, SMAA_WEIGHTS_SHADER, SMAA_BLEND_SHADER}) {
    command_action::Register(OnSmaaDraw, {.shader_hash = shader_hash, .command_types = command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  for (const uint32_t shader_hash : POST_SHADERS) {
    command_action::Register(OnPostDraw, {.shader_hash = shader_hash, .command_types = command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  command_action::Register(OnUpscaleDraw, {.shader_hash = UPSCALE_SHADER, .command_types = command_action::COMMAND_TYPE_DIRECT_DRAW});
  rysesonofrome::dlss::constants::Use(DLL_PROCESS_ATTACH);
  rysesonofrome::dlss::constants::SetUploadCallback(OnCameraUpload);
  renodx::utils::state::Use(DLL_PROCESS_ATTACH);
  command_action::Use(DLL_PROCESS_ATTACH);
  attached = true;
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE) {
  if (attached) {
    command_action::Unregister(OnTaaDraw);
    command_action::Unregister(OnSmaaDraw);
    command_action::Unregister(OnPostDraw);
    command_action::Unregister(OnUpscaleDraw);
    command_action::Use(DLL_PROCESS_DETACH);
    renodx::utils::state::Use(DLL_PROCESS_DETACH);
    rysesonofrome::dlss::constants::Use(DLL_PROCESS_DETACH);
    reshade::unregister_overlay("Ryse DLSS", OnOverlay);
    reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
    reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
    reshade::unregister_event<reshade::addon_event::present>(OnPresent);
    reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnTraceCopy);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnTraceCopyRegion);
    reshade::unregister_event<reshade::addon_event::init_sampler>(OnInitSampler);
    reshade::unregister_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);
    reshade::unregister_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
    for (auto& pin : renderer_pins) {
      if (!pin.set) continue;
      std::atomic_ref<int>(*pin.address).store(pin.game_value, std::memory_order_relaxed);
      pin.set = false;
    }
    if (jitter_control.controlling) {
      std::atomic_ref<int>(*jitter_control.value).store(jitter_control.game_value, std::memory_order_relaxed);
      jitter_control.controlling = false;
    }
    if (uint8_t* cvars = (screen_res_pin.cvars != nullptr ? *screen_res_pin.cvars : nullptr); cvars != nullptr && screen_res_pin.set) {
      std::atomic_ref<int>(*reinterpret_cast<int*>(cvars + SCREEN_RES_CVAR)).store(screen_res_pin.game_value, std::memory_order_relaxed);
      screen_res_pin.set = false;
    }
    rysesonofrome::engine_memory::ApplyCodePatch(&jitter_control.jitter_setup, false);
    rysesonofrome::engine_memory::ApplyCodePatch(&jitter_control.pause_flag_check, false);
    Reset();
    attached = false;
  }
  if (registered) {
    reshade::unregister_addon(module);
    registered = false;
  }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    registered = reshade::register_addon(module);
    return registered ? TRUE : FALSE;
  }
  if (reason == DLL_PROCESS_DETACH && reserved == nullptr && registered) {
    reshade::unregister_addon(module);
    registered = false;
  }
  return TRUE;
}
