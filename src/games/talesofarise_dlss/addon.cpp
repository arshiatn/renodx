/*
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
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"
#include "./constant_mirror.hpp"
#include "./constant_readback.hpp"
#include "./code_patch.hpp"
#include "./engine_cvars.hpp"
#include "./engine_scale.hpp"
#include "./native_state.hpp"
#include "./ngx_backend.hpp"
#include "./performance.hpp"
#include "./pixel_trace.hpp"
#include "./engine_hooks.hpp"

static_assert(_ITERATOR_DEBUG_LEVEL == 0, "DLAA must match the shared STL layout of Release RenoDX addons");

namespace {

using Microsoft::WRL::ComPtr;
using Context = renodx::utils::command_action::CommandContext<renodx::utils::command_action::DispatchArguments>;

enum class AAMode { OFF,
                    DLAA,
                    DLSS_QUALITY,
                    DLSS_BALANCED,
                    DLSS_PERFORMANCE,
                    DLSS_CUSTOM,
                    DLSS_ULTRA_PERFORMANCE,
                    DLSS_ULTRA_QUALITY };

struct ModeSettings {
  const char* label;
  const char* name;
  float minimum_scale;
  float maximum_scale;
  talesofarise::dlss::Mode backend;
  float live_percentage = 0;  // live render scale; Custom uses its slider
};

constexpr std::array<ModeSettings, 8> AA_MODES = {{
    {.label = "Off (game TAA)", .name = "Off", .minimum_scale = 0, .maximum_scale = 0, .backend = talesofarise::dlss::Mode::DLAA},
    {.label = "DLAA (100%)", .name = "DLAA", .minimum_scale = 1, .maximum_scale = 1, .backend = talesofarise::dlss::Mode::DLAA, .live_percentage = 100},
    // Live: UE renders ceil(size * percentage / 100); 66.66 and 33.33 give exactly 2560x1440 and 1280x720 at 4K.
    {.label = "DLSS Quality (~67%)", .name = "DLSS Quality", .minimum_scale = .64f, .maximum_scale = .70f, .backend = talesofarise::dlss::Mode::DLSS_QUALITY, .live_percentage = 66.66f},
    {.label = "DLSS Balanced (~58%)", .name = "DLSS Balanced", .minimum_scale = .56f, .maximum_scale = .60f, .backend = talesofarise::dlss::Mode::DLSS_BALANCED, .live_percentage = 58},
    {.label = "DLSS Performance (50%)", .name = "DLSS Performance", .minimum_scale = .50f, .maximum_scale = .52f, .backend = talesofarise::dlss::Mode::DLSS_PERFORMANCE, .live_percentage = 50},
    {.label = "DLSS Custom (1-100%)", .name = "DLSS Custom", .minimum_scale = .01f, .maximum_scale = 1, .backend = talesofarise::dlss::Mode::DLSS_CUSTOM},
    {.label = "DLSS Ultra Performance (~33%)", .name = "DLSS Ultra Performance", .minimum_scale = .32f, .maximum_scale = .35f, .backend = talesofarise::dlss::Mode::DLSS_ULTRA_PERFORMANCE, .live_percentage = 33.33f},
    // Not an NVIDIA SDK mode: the common 77%, created on NVIDIA's Quality model.
    {.label = "DLSS Ultra Quality (77%)", .name = "DLSS Ultra Quality", .minimum_scale = .74f, .maximum_scale = .80f, .backend = talesofarise::dlss::Mode::DLSS_ULTRA_QUALITY, .live_percentage = 77},
}};

struct PresetSettings {
  uint32_t value;
  const char* label;
};

constexpr std::array<PresetSettings, 4> PRESETS = {{
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_E, .label = "E (CNN, Fastest, Worst looking, not recommended)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_K, .label = "K (Transformer 1, Slower but better than E)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_M, .label = "M (Transformer 2, Slower than E, recommended for HDR)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_L, .label = "L (Transformer 2, Slowest, best looking, recommended for HDR)"},
}};

// Menu order; AAMode values stay as saved in ReShade.ini. Native DLAA only: the game's TAA never upscales (its
// post-processing has no temporal upscaling path), so the DLSS modes stay hidden.
constexpr std::array<AAMode, 2> MENU_ORDER = {AAMode::OFF, AAMode::DLAA};

// UE 4.21 main-scene TAA compute passes at native resolution: t0 depth, t1 scene color, t2 history, t3 velocity,
// u0 output; 8x8 groups over the output. 0xE2AF861D is 0x88765764's twin (same contract).
constexpr std::array<uint32_t, 2> TEMPORAL_SHADERS = {0x88765764u, 0xE2AF861Du};

// Constants read at the TAA dispatch: its own buffer (b0, 52 vectors) and UE 4.21's View (b1): ClipToPrevClip
// 114..117, TemporalAAJitter 118 (NDC), ViewSizeAndInvSize 122.
constexpr size_t TAA_VECTORS = 52;
constexpr size_t VIEW_VECTORS = 124;
constexpr uint32_t VIEW_CLIP_TO_PREV_CLIP = 114;
constexpr uint32_t VIEW_TEMPORAL_AA_JITTER = 118;
constexpr uint32_t VIEW_SIZE_AND_INV_SIZE = 122;

struct Texture {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

struct Runtime {
  talesofarise::dlss::Backend backend;
  talesofarise::dlss::ConstantReadback constant_readback;
  talesofarise::dlss::Performance performance;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> prepare_shader;
  ComPtr<ID3D11ComputeShader> merge_shader;
  ComPtr<ID3D11Buffer> prepare_constants;
  Texture depth;
  Texture motion;
  Texture color;   // replacing: cleaned input; NVIDIA writes the game's output
  Texture output;  // otherwise: NVIDIA's output, merged with stock TAA's alpha
  Texture merged;
  std::array<Texture, 2> target_views;
  uint32_t next_target_view = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  AAMode mode = AAMode::OFF;
  bool temporal_upscaling = false;
  uint32_t temporal_shader_hash = 0;
  uint64_t last_frame = UINT64_MAX;
  bool backend_failed = false;
};

struct State {
  std::shared_mutex mutex;
  std::unique_ptr<Runtime> runtime = std::make_unique<Runtime>();
  reshade::api::device* device = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t generation = 0;
  uint64_t frame = 0;
  uint64_t claimed_frame = UINT64_MAX;
  uint64_t completed = 0;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  uint32_t temporal_shader_hash = 0;
  AAMode last_mode = AAMode::OFF;
  talesofarise::dlss::PerformanceSummary performance;
  std::string status = "Off — original game TAA";
  std::string constants;
  uint64_t copied_bytes = 0;
  uint64_t copied_per_frame = 0;
} state;

std::atomic<AAMode> requested_mode = AAMode::OFF;
std::atomic<uint32_t> preset = NVSDK_NGX_DLSS_Hint_Render_Preset_L;
// nvngx_dlss.dll found at load (overlay warnings); the backend looks again when it starts.
talesofarise::dlss::DlssLibrary dlss_library;
bool advanced = false;  // overlay settings mode
bool weak_cpu = false;  // presets leave CPU_HEAVY_SETTINGS at the game's
bool far_sun_shadows = false;  // presets set FAR_SUN_SHADOWS
std::array<float, 2> traced_pixel = {50.f, 10.f};  // overlay: % of the screen's width and height (pixel_trace.hpp)
// Skip the stock TAA dispatch once constants are known before it (CPU copy trusted).
std::atomic_bool replace_taa = true;
// Active input size the temporal pass last received (width | height << 32), even when
// rejected, and how many temporal passes were seen.
std::atomic<uint64_t> seen_input = 0;
std::atomic<uint64_t> seen_frames = 0;
// Present (+1) at which the Advanced overlay was last drawn; timings are measured only then.
std::atomic<uint64_t> overlay_frame = 0;

// Why NVIDIA's history restarted, and UE's jitter pattern length (frames until its first
// offset repeats); NVIDIA wants at least 8 * (output / input)^2.
struct Diagnostics {
  std::atomic<uint64_t> camera_cuts = 0;
  std::atomic<uint64_t> skipped_frames = 0;
  std::atomic<uint64_t> restarts = 0;
  std::array<float, 2> first_jitter = {};
  uint32_t jitter_frames = 0;
  std::atomic<uint32_t> jitter_phases = 0;
} diagnostics;
bool attached = false;

// Live render scale (engine_scale.hpp): the AA mode's percentage is written into
// r.ScreenPercentage; Off or disabling puts the game's own value back.
struct LiveScale {
  talesofarise::engine_scale::Finder finder;
  bool enabled = true;
  float custom = 50.f;
  bool controlling = false;
  float game_value = 100.f;
  float target = 0.f;
  uint64_t seen_at_write = 0;
  uint32_t width_at_write = 0;
  bool verified = false;
  uint32_t writes = 0;  // rewrites in the current window
  uint32_t window = 0;  // presents in the current window
  std::string problem;  // paused; retried a few times (game menu changes, a moved variable)
  std::chrono::steady_clock::time_point retry_at;
  uint32_t retries = 0;  // since the last verified write
  std::string note;
} live;
constexpr uint32_t MAX_LIVE_RETRIES = 5;

// Tales renders without camera jitter, so NVIDIA gets no sub-pixel samples: after UE 4.21's jitter
// (Box-Muller, r.TemporalAASamples / r.TemporalAAFilterSize), the game multiplies it by a per-view
// factor that is 0 when the view's AA method is 5 (its TAA):
//   cmp dword ptr [r14 + AntiAliasingMethod], 5; jne skip; movss xmm0, [r14 + factor];
//   mulss xmm7, xmm0; mulss xmm8, xmm0; skip: movss [r14 + TemporalJitterPixels.x], xmm7
// While an AA mode is on, the jne becomes a jmp (UE's own jitter, projection and velocity included);
// Off or disabling puts the game's code back.
constexpr std::string_view JITTER_SCALE =
    "41 83 BE ?? ?? 00 00 05 75 ?? F3 41 0F 10 86 ?? ?? 00 00 F3 0F 59 F8 F3 44 0F 59 C0 F3 41 0F 11 BE ?? ?? 00 00";
constexpr size_t JITTER_SCALE_BRANCH = 8;
struct CameraJitter {
  talesofarise::code_patch::CodePatch patch;
  bool searched = false;
  bool enabled = true;
} camera_jitter;

// Unreal's sun shadow atlas, 1 << (GMaxTextureMipCount - 1) = 8192 (Unreal caps the mip count at 14; the game's
// D3D11 limit for shadow maps, exe+0x179F800, is 16384):
//   mov ecx, [GMaxTextureMipCount]; xor esi, esi; dec ecx; ...; mov r13d, 1; mov [rsp + 0x58], rsi; shl r13d, cl
// While the sun shadow resolution is written above the game's, the 1 becomes 2: a 16384 atlas holds 4 cascades of
// 6144 or 8192, as many as 8192 holds of the game's 4096.
constexpr std::string_view SUN_SHADOW_ATLAS =
    "8B 0D ?? ?? ?? ?? 33 F6 FF C9 40 88 74 24 38 BA 01 00 00 00 40 88 7C 24 40 41 BD 01 00 00 00 48 89 74 24 58 41 D3 E5";
constexpr size_t SUN_SHADOW_ATLAS_SHIFTED = 27;
talesofarise::code_patch::CodePatch sun_shadow_atlas;


// Graphics presets: engine settings written live (engine_cvars.hpp) while a preset is chosen; "Game's own" puts the
// game's values back. A preset only raises quality: higher-is-better settings take the larger of the game's value
// and the preset's, lower-is-better ones (LOD scales, thresholds, cell sizes) the smaller. KEEP: the game's value.
enum class Better : uint8_t { HIGHER,
                              LOWER,
                              EXACT };
constexpr float KEEP = std::numeric_limits<float>::quiet_NaN();
struct GraphicsSetting {
  const char* group;
  const wchar_t* name;
  const char* label;
  Better better;
  // High, Ultra, Extreme, Maximum; then one extra choice offered only in the setting's own list.
  std::array<float, 5> values;
  const char* tooltip = nullptr;  // shown above the cvar name
  std::array<float, 3> more_extras = {KEEP, KEEP, KEEP};  // further choices offered only in the setting's own list
};
constexpr std::array<const char*, 6> GRAPHICS_PRESETS = {"Game's own", "High", "Ultra", "Extreme", "Maximum", "Beyond Maximum"};
constexpr size_t MAXIMUM_PRESET = 4;
// Beyond Maximum: Maximum with BEYOND_MAXIMUM_SETTINGS at their best choice (the last; the first for LOWER).
constexpr size_t BEYOND_MAXIMUM_PRESET = 5;
constexpr std::array<std::wstring_view, 8> BEYOND_MAXIMUM_SETTINGS = {
    L"Tales.OutlineDistance", L"Tales.NPCDistance", L"Tales.MonsterDistance", L"Tales.CharacterDetailDistance",
    L"r.LandscapeLODDistributionScale", L"r.LandscapeLOD0DistributionScale", L"r.LandscapeLODBias", L"grass.CullDistanceScale"};
// In no preset at all, Game's own included (ATN: never reset by accident): shaders on load.
constexpr std::wstring_view NO_PRESET_SETTING = L"r.CreateShadersOnLoad";
// On in every preset, Game's own included (ATN): smooth cutscenes.
constexpr std::wstring_view EVERY_PRESET_SETTING = L"Tales.SmoothCutscenes";
// Only cost CPU time; with "Weak CPU" every preset leaves them at the game's (ATN: keeping characters behind the camera
// costs ~5 fps on 3D V-Cache cores, ~30 on the others; full-rate animation makes no difference, it stays).
constexpr std::array<std::wstring_view, 2> CPU_HEAVY_SETTINGS = {L"Arise.PF.Culling.ForceInFrustum", L"Tales.NeverHideCharacters"};
// In no preset (ATN: a trade-off); with "Far sun shadows" every preset sets them. Sun shadow distance 3 stops sunlight
// shining through far mountains (ATN: the first area after the tutorial; above 2 only with engine_hooks.hpp's patch) by
// spreading the same shadow maps over more ground; 8192 makes up for part of the softer, shimmering shadows near by.
struct PresetOverride {
  std::wstring_view name;
  float value;
};
constexpr std::array<PresetOverride, 2> FAR_SUN_SHADOWS = {{
    {.name = L"r.Shadow.DistanceScale", .value = 3.f},
    {.name = L"r.Shadow.MaxCSMResolution", .value = 8192.f},
}};
constexpr std::array<GraphicsSetting, 63> GRAPHICS_SETTINGS = {{
    {"Distance and detail", L"r.ViewDistanceScale", "Draw distance", Better::HIGHER, {1.5f, 2.f, 3.f, 20.f, KEEP},
     "How far objects are drawn."},
    {"Distance and detail", L"r.StaticMeshLODDistanceScale", "Object detail distance", Better::LOWER, {.75f, .5f, .25f, .0001f, .01f},
     "How far objects keep their full detail (smaller: farther)."},
    {"Distance and detail", L"foliage.LODDistanceScale", "Foliage detail distance", Better::HIGHER, {1.5f, 2.f, 4.f, 50.f, KEEP},
     "How far trees and plants keep their full detail."},
    {"Distance and detail", L"grass.CullDistanceScale", "Grass draw distance", Better::HIGHER, {KEEP, 1.5f, 2.f, 10.f, 4.f},
     "How far grass is drawn.",
     {20.f, KEEP, KEEP}},
    {"Distance and detail", L"r.SkeletalMeshLODRadiusScale", "Character detail distance", Better::LOWER, {.75f, .5f, .25f, .01f, KEEP},
     "How far characters keep their full detail (smaller: farther)."},
    {"Distance and detail", L"r.SkeletalMeshLODBias", "Highest character detail everywhere", Better::LOWER, {KEEP, KEEP, KEEP, -10.f, KEEP},
     "-10: characters always at full detail."},
    {"Distance and detail", L"r.ForceHighestMipOnCharacterTextures", "Full-resolution character textures", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "1: characters' textures always at full resolution."},
    {"Distance and detail", L"r.DetailMode", "Detail mode (high)", Better::HIGHER, {2.f, 2.f, 2.f, 2.f, KEEP},
     "2: every small detail object and effect is drawn."},
    {"Distance and detail", L"r.ParticleLODBias", "Highest effect detail everywhere", Better::LOWER, {KEEP, KEEP, KEEP, -10.f, KEEP},
     "-10: effects always at full detail."},
    {"Distance and detail", L"r.ParticleLightQuality", "Particle lights", Better::HIGHER, {KEEP, 2.f, 2.f, 2.f, KEEP},
     "2: lights from effects (sparks, fire) at full quality."},
    {"Distance and detail", L"r.LightMaxDrawDistanceScale", "Light draw distance", Better::HIGHER, {1.5f, 2.f, 3.f, 5.f, KEEP},
     "How far lights are drawn."},
    {"Distance and detail", L"r.Streaming.UseAllMips", "All texture mips (VRAM)", Better::HIGHER, {KEEP, KEEP, KEEP, 1.f, KEEP},
     "1: every texture at full resolution (uses more video memory)."},
    {"Distance and detail", L"r.MaxAnisotropy", "Anisotropic filtering (textures loaded later)", Better::HIGHER, {KEEP, 16.f, 16.f, 16.f, KEEP},
     "Texture sharpness at steep angles (textures loaded afterwards)."},
    {"Distance and detail", L"r.LandscapeLODDistributionScale", "Terrain detail distance", Better::HIGHER, {KEEP, KEEP, 2.f, 15.f, 50.f},
     "How far the terrain keeps its detail."},
    {"Distance and detail", L"r.LandscapeLOD0DistributionScale", "Terrain full detail distance", Better::HIGHER, {KEEP, KEEP, 2.f, 15.f, 50.f},
     "How far the terrain keeps its full detail."},
    {"Distance and detail", L"r.LandscapeLODBias", "Terrain detail bias", Better::LOWER, {KEEP, KEEP, KEEP, -1.f, -2.f},
     "-1: terrain one detail level finer everywhere."},
    {"Distance and detail", L"r.Streaming.HLODStrategy", "Distant proxy textures (2: all mips)", Better::HIGHER, {KEEP, KEEP, KEEP, 2.f, KEEP},
     "2: distant merged buildings with full-resolution textures."},
    {"Distance and detail", L"Tales.OutlineDistance", "Cel-shading outline distance", Better::HIGHER, {12000.f, 24000.f, 100000.f, 500000.f, 750000.f},
     "How far the game's outlines and cel-shading reach (the game: 6000)."},
    // Bandai's PFCullingManager (exe+0xA0C230, read every frame) hides characters outside the view; turning back fades
    // them in and rebuilds them (ATN: an fps drop for 1-2 s). 1 counts every one as inside the view (Never hide
    // characters covers another system).
    {"Characters", L"Arise.PF.Culling.ForceInFrustum", "Keep characters behind the camera", Better::HIGHER, {1.f, 1.f, 1.f, 1.f, KEEP},
     "1: characters behind the camera stay instead of being hidden. Turning around no longer fades them in\n"
     "(with a short fps drop). CPU heavy: on CPUs with less cache it can cost a lot of fps (Weak CPU under\n"
     "the presets)."},
    // Settings without a console variable (engine_hooks.hpp: the addon's own values, read by its hooks in the game's
    // code). Maximum: ATN's values.
    {"Characters", L"Tales.NPCDistance", "NPC distance", Better::HIGHER, {10000.f, 50000.f, 200000.f, 500000.f, 750000.f},
     "How far NPCs (townspeople, soldiers) stay spawned and visible (the game: about 3000; all its NPC\n"
     "distances grow with it). Applies after loading an area."},
    {"Characters", L"Tales.MonsterDistance", "Monster distance", Better::HIGHER, {10.f, 100.f, 1000.f, 5000.f, 10000.f},
     "How far monsters spawn and stay (times the game's)."},
    {"Characters", L"Tales.CharacterDetailDistance", "NPC and monster detail distance", Better::HIGHER, {3.f, 10.f, 50.f, 200.f, 1000.f},
     "How far NPCs and monsters keep their full detail (times the game's)."},
    {"Characters", L"Tales.NeverHideCharacters", "Never hide characters", Better::HIGHER, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "1: characters are never culled off screen or behind things (their sounds keep playing too). Costs\n"
     "performance. Applies after loading an area."},
    {"Characters", L"a.URO.Enable", "Half-rate distant animation (0: off)", Better::LOWER, {0.f, 0.f, 0.f, 0.f, KEEP},
     "0: distant NPCs and monsters animate at the full rate."},
    // Cutscene fixes (engine_hooks.hpp), in every preset; smooth cutscenes in Game's own too (EVERY_PRESET_SETTING).
    {"Cutscenes", L"Tales.SkitResolution", "Skit resolution (% of the screen's pixels)", Better::HIGHER, {100.f, 100.f, 100.f, 200.f, KEEP},
     "Draws the skits' panels with as many pixels as the screen (times this): sharp instead of blurry, and\n"
     "anti-aliased.",
     {50.f, KEEP, KEEP}},
    {"Cutscenes", L"Tales.SmoothCutscenes", "Smooth cutscenes", Better::HIGHER, {1.f, 1.f, 1.f, 1.f, KEEP},
     "1: cutscenes play at your frame rate instead of in steps. Applies to cutscenes loaded afterwards."},
    // Unreal puts all sun shadow cascades into one 8192 x 8192 atlas (SUN_SHADOW_ATLAS) and does not draw a cascade that
    // does not fit (8192 drew one cascade; above 8192 none, and the game crashed). The game sets its own 4096 (ATN), the
    // most 2 to 4 cascades fit: above it only with the doubled atlas. Texels per pixel 4, written live, crashed too
    // (exe+0x1A9E56C, a null shadow atlas): no choice goes above 3. Pre-shadows stay at 1: above it a close-up's
    // pre-shadow can outgrow the shadow atlas. ShadowQuality 5, 10 cascades and MotionBlurQuality 4 are Unreal's limits.
    {"Shadows", L"r.Shadow.MaxResolution", "Light shadow resolution (point / spot)", Better::HIGHER, {4096.f, 4096.f, 8192.f, 8192.f, KEEP},
     "Shadow sharpness of point and spot lights."},
    {"Shadows", L"r.ShadowQuality", "Shadow filtering quality", Better::HIGHER, {KEEP, 4.f, 5.f, 5.f, KEEP},
     "Smoothing of shadow edges."},
    {"Shadows", L"r.CapsuleShadowsFullResolution", "Character soft shadows at full resolution", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "1: the soft shadows characters cast from their capsules (around their feet and body) at full resolution\n"
     "instead of half: sharper."},
    {"Shadows", L"r.Shadow.CSM.MaxCascades", "Sun shadow cascades", Better::HIGHER, {4.f, 5.f, 6.f, 10.f, KEEP},
     "Layers of the sun's shadow: more of them keep shadows sharp at every distance."},
    // Sun shadows shimmer when the sun moves (every ~10 s) at distances above 1; finer texels fix it (ATN: 6144 / 8192).
    // Above the game's 4096 only with SUN_SHADOW_ATLAS (unusable without it).
    {"Shadows", L"r.Shadow.MaxCSMResolution", "Sun shadow resolution", Better::HIGHER, {6144.f, 6144.f, 8192.f, 8192.f, KEEP},
     "Sharpness of the sun's shadow (the game: 4096). Sharper edges shimmer less when the sun moves.\n"
     "Doubles the sun's shadow texture to 16384 (up to 1 GB of video memory)."},
    // Above 2 once engine_hooks.hpp removed the game's clamp. Farther spreads the same shadow maps over more ground:
    // shadow edges shimmer more (ATN: above 1).
    {"Shadows", L"r.Shadow.DistanceScale", "Sun shadow distance", Better::HIGHER, {KEEP, 1.25f, 1.5f, 2.f, 4.f},
     "How far the sun's shadows reach. Farther: the same shadow detail over more\n"
     "ground, so shadow edges shimmer more (a higher Sun shadow resolution makes up for it).\n"
     "3 stops sunlight shining through far mountains (Far sun shadows under the presets).",
     {2.5f, 3.f, 3.5f}},
    {"Shadows", L"r.Shadow.RadiusThreshold", "Shadows of small objects", Better::LOWER, {.02f, .01f, .005f, .001f, KEEP},
     "How small an object can be and still cast a shadow (smaller: more of them)."},
    {"Shadows", L"r.Shadow.CSM.TransitionScale", "Sun shadow cascade blending", Better::HIGHER, {KEEP, 1.5f, 2.f, 2.f, KEEP},
     "Smoother change between the sun shadow's layers."},
    {"Shadows", L"r.Shadow.PreShadowResolutionFactor", "Pre-shadow resolution", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "Sharpness of moving objects' shadows on the scenery."},
    {"Shadows", L"r.Shadow.TexelsPerPixel", "Object shadow resolution", Better::HIGHER, {KEEP, KEEP, 2.f, 3.f, KEEP},
     "Shadow sharpness of single objects such as characters."},
    {"Shadows", L"r.Shadow.TexelsPerPixelSpotlight", "Spot light shadow resolution", Better::HIGHER, {KEEP, KEEP, 2.f, 3.f, KEEP},
     "Shadow sharpness from spot lights."},
    {"Shadows", L"r.Shadow.TexelsPerPixelPointlight", "Point light shadow resolution", Better::HIGHER, {KEEP, KEEP, 2.f, 3.f, KEEP},
     "Shadow sharpness from point lights such as torches."},
    {"Shadows", L"r.Shadow.MinResolution", "Distant object shadow resolution", Better::HIGHER, {KEEP, KEEP, KEEP, KEEP, 64.f},
     "Least resolution of distant objects' shadows."},
    {"Shadows", L"r.Shadow.FadeResolution", "Distant object shadow fade", Better::LOWER, {KEEP, KEEP, KEEP, KEEP, 32.f},
     "Resolution at which distant objects' shadows fade out (smaller: later)."},
    {"Reflections and GI", L"r.SSR.Quality", "Screen-space reflections", Better::HIGHER, {KEEP, 3.f, 4.f, 4.f, KEEP},
     "Quality of reflections on shiny surfaces."},
    // Bandai's resolution divisors (game defaults: SSR 2, SSR color 2, translucent SSR depth 4, clouds 3, cloud
    // shadows and ray march 8, volumetric lighting 8 pixels per cell, ground fog 8). Blur passes keep theirs (their
    // radius is in pixels of the downsampled buffer: full resolution would change the look).
    {"Reflections and GI", L"r.ScreenSpaceReflectionScreenScaleTO14", "Reflection resolution divisor (Bandai)", Better::LOWER, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "Reflections' resolution divisor (1: full resolution)."},
    {"Reflections and GI", L"r.ScreenSpaceReflectionColorScreenScaleTO14", "Reflection color resolution divisor (Bandai)", Better::LOWER, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "Resolution divisor of the reflected image (1: full resolution)."},
    {"Reflections and GI", L"r.TranslucnetSSRDepthBufferScreenScaleTO14", "Translucent reflection depth divisor (Bandai)", Better::LOWER, {KEEP, KEEP, 2.f, 1.f, KEEP},
     "Depth resolution divisor for reflections on glass and water (1: full)."},
    {"Reflections and GI", L"r.SpearateTransulcentUseDownSampledDepthTO14", "Full-resolution translucency depth (Bandai)", Better::LOWER, {KEEP, KEEP, 0.f, 0.f, KEEP},
     "0: effects and glass use full-resolution depth (cleaner edges)."},
    {"Reflections and GI", L"r.SceneColorFormat", "Scene color precision (4: RGBA16F)", Better::HIGHER, {KEEP, 4.f, 4.f, 4.f, KEEP},
     "4: 16-bit color for the scene (less banding)."},
    {"Reflections and GI", L"r.UseHighResSSGITO14", "High-resolution GI (Bandai)", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "1: bounced (indirect) light at high resolution."},
    {"Reflections and GI", L"r.UseDownSampleColorBufferForSSGITO14", "Full-resolution GI color (Bandai)", Better::LOWER, {KEEP, KEEP, 0.f, 0.f, KEEP},
     "0: bounced (indirect) light reads the full-resolution image."},
    {"Fog, clouds and effects", L"r.DenseGroundFogScreenScaleTO14", "Ground fog resolution divisor (Bandai)", Better::LOWER, {KEEP, 4.f, 2.f, 1.f, KEEP},
     "Ground fog's resolution divisor (1: full resolution)."},
    {"Fog, clouds and effects", L"r.VolumetricCloudScreenScaleTO14", "Cloud resolution divisor (Bandai)", Better::LOWER, {KEEP, 2.f, 2.f, 1.f, KEEP},
     "Clouds' resolution divisor (1: full resolution)."},
    {"Fog, clouds and effects", L"r.VolumetricCloudShadowScreenScaleTO14", "Cloud shadow resolution divisor (Bandai)", Better::LOWER, {KEEP, KEEP, 4.f, 2.f, KEEP},
     "Cloud shadows' resolution divisor (1: full resolution)."},
    {"Fog, clouds and effects", L"r.VolumetricCloudSimpleRayMarchScreenScaleTO14", "Cloud ray march resolution divisor (Bandai)", Better::LOWER, {KEEP, KEEP, 4.f, 2.f, KEEP},
     "Cloud detail's resolution divisor (1: full resolution)."},
    {"Fog, clouds and effects", L"r.VolumetricCloudDensityTextureSizeScale", "Cloud density texture size", Better::HIGHER, {KEEP, KEEP, KEEP, 2.f, KEEP},
     "Size of the clouds' shape texture."},
    // Volumetric lighting and fog are the most expensive effects, with no visible gain above the game's: every preset
    // keeps the game's (ATN, as Expeditions: Rome); finer and cheaper values are in their lists (EXACT: those may go
    // below the game's). Off is r.VolumetricFog 0; the grid settings clamp 0 to 1.
    {"Fog, clouds and effects", L"r.VolumetricLightingBufferPixelNumPerCell", "Volumetric lighting cell size (Bandai)", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 16.f},
     "Pixels per lighting cell (the game's: 8). Smaller: finer, slower. 16: faster, blockier.",
     {4.f, 2.f, KEEP}},
    {"Fog, clouds and effects", L"r.VolumetricFog", "Volumetric fog", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 0.f},
     "0: off, faster. The game's normal height fog stays; Unreal's volumetric fog goes."},
    {"Fog, clouds and effects", L"r.VolumetricFog.GridPixelSize", "Volumetric fog cell size", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 12.f},
     "Pixels per fog cell (the game's: 8). Smaller: finer, slower. Larger: faster, blockier.", {16.f, 4.f, 2.f}},
    {"Fog, clouds and effects", L"r.VolumetricFog.GridSizeZ", "Volumetric fog depth slices", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 96.f},
     "Fog slices into the distance (the game's: 128). More: finer, slower. Fewer: faster.", {64.f, 256.f, KEEP}},
    {"Fog, clouds and effects", L"r.LightShaftDownSampleFactor", "Light shaft resolution divisor", Better::LOWER, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "Light shafts' resolution divisor (1: full resolution)."},
    {"Fog, clouds and effects", L"r.MotionBlurQuality", "Motion blur quality", Better::HIGHER, {KEEP, 4.f, 4.f, 4.f, KEEP},
     "Motion blur quality (its strength stays)."},
    {"Density (more than the game places)", L"grass.densityScale", "Grass density", Better::HIGHER, {KEEP, KEEP, KEEP, 10.f, 4.f},
     "More grass than the game places."},
    {"Density (more than the game places)", L"foliage.DensityScale", "Foliage density", Better::HIGHER, {KEEP, KEEP, KEEP, 10.f, 4.f},
     "More plants than the game places (where a plant allows it)."},
    {"Density (more than the game places)", L"foliage.MaxTrianglesToRender", "Foliage triangle cap", Better::HIGHER, {KEEP, KEEP, KEEP, 1000000000.f, KEEP},
     "Limit on drawn plant triangles (higher: none dropped)."},
    // In no preset (a load-time trade-off, not quality): read when an area's shaders load.
    {"Loading", L"r.CreateShadersOnLoad", "Shaders on load", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 1.f},
     "1: creates all of an area's shaders while it loads, instead of the first time an effect or material appears on\n"
     "screen. Fewer stutters when something new shows up for the first time (the typical DirectX 11 hitching).\n"
     "Loading takes a bit longer and uses a bit more memory. Applies to areas loaded afterwards: load a save after\n"
     "changing it. 0: the engine's default (also overrides an Engine.ini line).",
     {0.f, KEEP, KEEP}},
}};
constexpr size_t SettingIndex(std::wstring_view name) {
  return std::ranges::find(GRAPHICS_SETTINGS, name, [](const GraphicsSetting& setting) { return std::wstring_view(setting.name); })
         - GRAPHICS_SETTINGS.begin();
}
// r.VolumetricFog 0 greys out the r.VolumetricFog.* settings.
constexpr size_t VOLUMETRIC_FOG = SettingIndex(L"r.VolumetricFog");
constexpr size_t SUN_SHADOW_RESOLUTION = SettingIndex(L"r.Shadow.MaxCSMResolution");
static_assert(VOLUMETRIC_FOG < GRAPHICS_SETTINGS.size() && SUN_SHADOW_RESOLUTION < GRAPHICS_SETTINGS.size());
// Each setting's own choice (a slider, as Ryse's options): 0 the game's value, else one of its choices (its distinct
// preset values and extras, with steps between them); the preset buttons set every choice. Saved per setting in
// ReShade.ini, section GRAPHICS_SECTION, key the cvar name, by value (SaveChoice): steps move the indexes. (The old
// "Tales Graphics Settings" section held indexes.)
constexpr const char* GRAPHICS_SECTION = "Tales Graphics Values";

struct GraphicsEntry {
  talesofarise::engine_cvars::Cvar cvar;
  std::string details;  // int / float and the game's value at the search, or why it is unusable
  std::string key;      // the cvar name
  std::vector<float> choices;
  int choice = 0;
  bool controlling = false;
  float game_value = 0.f;
  float written = 0.f;
};
struct Graphics {
  talesofarise::engine_cvars::Console console;
  bool searched = false;
  bool available = false;
  std::string status = "Searching the game's settings...";
  std::array<GraphicsEntry, GRAPHICS_SETTINGS.size()> entries;
} graphics;
// "game" for the game's own value (not a number: it reads back as unset).
void SaveChoice(const GraphicsEntry& entry) {
  const std::string value = (entry.choice == 0 ? std::string("game") : std::format("{:g}", entry.choices[entry.choice - 1]));
  reshade::set_config_value(nullptr, GRAPHICS_SECTION, entry.key.c_str(), value.c_str());
}
bool registered = false;

struct Frame {
  std::unique_ptr<Runtime> runtime;
  ComPtr<ID3D11DeviceContext1> context;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> inputs;
  std::array<ComPtr<ID3D11Texture2D>, 3> resources;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11ShaderResourceView> target_srv;
  std::array<std::array<float, 4>, TAA_VECTORS> taa;
  std::array<std::array<float, 4>, VIEW_VECTORS> view;
  // CPU copy of the same constants (constant_mirror.hpp).
  std::array<std::array<float, 4>, TAA_VECTORS> mirror_taa;
  std::array<std::array<float, 4>, VIEW_VECTORS> mirror_view;
  bool from_mirror = false;
  bool compare_mirror = false;
  uint64_t generation = 0;
  uint64_t frame = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t source_width = 0;
  uint32_t source_height = 0;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  std::array<float, 2> pixel_jitter = {};
  DXGI_FORMAT color_format = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT target_format = DXGI_FORMAT_UNKNOWN;
  AAMode mode = AAMode::DLAA;
  bool temporal_upscaling = false;
  uint32_t temporal_shader_hash = 0;
  double cpu_readback_ms = 0;
  bool measuring = false;
  bool sample_cpu = false;
  bool committed = false;
  bool values_validated = false;
  std::chrono::steady_clock::time_point evaluation_started = {};

  ~Frame() {
    // Return the claimed runtime on failed input checks as well as after the
    // dispatch. A concurrent reset retires it outside the lock instead.
    if (runtime) {
      runtime->constant_readback.Cancel();
      const std::unique_lock lock(state.mutex);
      if (generation == state.generation) {
        state.runtime = std::move(runtime);
      }
    }
  }
};

void SetStatus(std::string_view message, uint64_t generation = UINT64_MAX) {
  bool changed = false;
  {
    const std::unique_lock lock(state.mutex);
    if (generation != UINT64_MAX && generation != state.generation) return;
    changed = state.status != message;
    if (changed) state.status.assign(message);
  }
  if (changed) {
    renodx::utils::log::i("[Tales DLSS] ", message);
  }
}

// `keep_nvidia`: AA mode changes keep NVIDIA's runtime initialized (initializing takes over a
// second); it re-creates its feature for a new size or mode by itself.
void Reset(bool keep_nvidia = false) {
  std::unique_ptr<Runtime> retired;
  {
    const std::unique_lock lock(state.mutex);
    ++state.generation;
    state.claimed_frame = UINT64_MAX;
    state.completed = 0;
    state.render_width = 0;
    state.render_height = 0;
    state.temporal_shader_hash = 0;
    state.performance = {};
    state.constants.clear();
    diagnostics.camera_cuts.store(0);
    diagnostics.skipped_frames.store(0);
    diagnostics.restarts.store(0);
    retired = std::move(state.runtime);
    state.runtime = std::make_unique<Runtime>();
    if (keep_nvidia && retired && !retired->backend.Failed()) state.runtime->backend = std::move(retired->backend);
  }
  // NGX teardown is outside the module lock, just like evaluation.
}

void FinishFrame(Context&, const void* data) {
  auto* frame = const_cast<Frame*>(static_cast<const Frame*>(data));
  auto& runtime = *frame->runtime;
  runtime.constant_readback.Cancel();
  runtime.performance.End(frame->context.Get());
  if (frame->committed && frame->evaluation_started != std::chrono::steady_clock::time_point{}) {
    runtime.performance.CpuSample(
        frame->cpu_readback_ms,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame->evaluation_started).count());
  }
  {
    const std::unique_lock lock(state.mutex);
    if (frame->generation == state.generation) {
      state.performance = runtime.performance.Snapshot();
      if (frame->measuring) state.constants = runtime.constant_readback.Describe();
      // A candidate rejected after its asynchronous stock dispatch must not
      // prevent a later, valid main temporal view in the same presentation.
      if (!frame->values_validated && state.frame == frame->frame && state.claimed_frame == frame->frame) {
        state.claimed_frame = UINT64_MAX;
      }
    }
  }
  delete static_cast<const Frame*>(data);
}

// Validation, NVIDIA evaluation and commit. `replace`: before the stock dispatch, which is
// skipped on success, NVIDIA writing the game's output; otherwise after it, merged with its alpha.
bool Evaluate(Frame* frame, bool replace) {
  auto& runtime = *frame->runtime;
  auto* context = frame->context.Get();
  if (frame->sample_cpu) frame->evaluation_started = std::chrono::steady_clock::now();
  const auto width = static_cast<float>(frame->width);
  const auto height = static_cast<float>(frame->height);
  const auto& view = frame->view;
  // ViewSizeAndInvSize must look like one (size, 1 / size): checks the View layout too.
  const auto& view_size = view[VIEW_SIZE_AND_INV_SIZE];
  const float render_width = view_size[0];
  const float render_height = view_size[1];
  if (std::isfinite(render_width) && std::isfinite(render_height) && render_width >= 1 && render_height >= 1
      && render_width <= 65536 && render_height <= 65536) {
    seen_input.store(static_cast<uint64_t>(render_width) | (static_cast<uint64_t>(render_height) << 32));
    seen_frames.fetch_add(1);
  }
  // TemporalAAJitter is in NDC; UE's jitter stays within half a pixel.
  const std::array<float, 2> pixel_jitter = {view[VIEW_TEMPORAL_AA_JITTER][0] * render_width / 2,
                                             -view[VIEW_TEMPORAL_AA_JITTER][1] * render_height / 2};
  if (!std::isfinite(render_width) || !std::isfinite(render_height)
      || render_width < 1 || render_height < 1
      || render_width > frame->source_width || render_height > frame->source_height
      || std::floor(render_width) != render_width || std::floor(render_height) != render_height
      || !std::isfinite(view_size[2]) || !std::isfinite(view_size[3])
      || std::abs(view_size[2] * render_width - 1.f) > .001f || std::abs(view_size[3] * render_height - 1.f) > .001f
      || !std::isfinite(pixel_jitter[0]) || !std::isfinite(pixel_jitter[1])
      || std::abs(pixel_jitter[0]) > 1 || std::abs(pixel_jitter[1]) > 1) {
    SetStatus("Game TAA: unsupported temporal view layout", frame->generation);
    return false;
  }
  frame->pixel_jitter = pixel_jitter;
  frame->render_width = static_cast<uint32_t>(render_width);
  frame->render_height = static_cast<uint32_t>(render_height);
  frame->temporal_upscaling = frame->render_width != frame->width || frame->render_height != frame->height;
  const auto& mode = AA_MODES[static_cast<size_t>(frame->mode)];
  if ((frame->source_width != frame->render_width
       && frame->source_width != (frame->render_width + 3) / 4 * 4
       && frame->source_width != (frame->render_width + 7) / 8 * 8)
      || (frame->source_height != frame->render_height
          && frame->source_height != (frame->render_height + 3) / 4 * 4
          && frame->source_height != (frame->render_height + 7) / 8 * 8)
      || render_width / width < mode.minimum_scale || render_width / width > mode.maximum_scale
      || render_height / height < mode.minimum_scale || render_height / height > mode.maximum_scale
      || std::abs(render_width / width - render_height / height) > std::max(2.f / width, 2.f / height)) {
    SetStatus(std::string("Game TAA: unsupported scale/layout for ") + mode.label
                  + "; keep the game's Anti-Aliasing on TAA and its render scale at 100%",
              frame->generation);
    return false;
  }
  for (uint32_t row = VIEW_CLIP_TO_PREV_CLIP; row < VIEW_CLIP_TO_PREV_CLIP + 4; ++row) {
    for (const float value : view[row]) {
      if (!std::isfinite(value)) {
        SetStatus("Game TAA: invalid camera reprojection matrix", frame->generation);
        return false;
      }
    }
  }
  frame->values_validated = true;
  if (const std::array<float, 2> jitter = pixel_jitter; diagnostics.jitter_frames == 0 || diagnostics.jitter_frames > 4096) {
    if (diagnostics.jitter_frames > 4096) diagnostics.jitter_phases.store(0);
    diagnostics.first_jitter = jitter;
    diagnostics.jitter_frames = 1;
  } else if (jitter == diagnostics.first_jitter) {
    diagnostics.jitter_phases.store(diagnostics.jitter_frames);
    diagnostics.jitter_frames = 1;
  } else {
    ++diagnostics.jitter_frames;
  }
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const auto create_texture = [&](Texture* texture, DXGI_FORMAT format, uint32_t texture_width, uint32_t texture_height) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = texture_width,
        .Height = texture_height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
    };
    return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->resource.ReleaseAndGetAddressOf()))
           && SUCCEEDED(device->CreateShaderResourceView(texture->resource.Get(), nullptr, texture->srv.ReleaseAndGetAddressOf()))
           && SUCCEEDED(device->CreateUnorderedAccessView(texture->resource.Get(), nullptr, texture->uav.ReleaseAndGetAddressOf()));
  };

  if (runtime.device != device || runtime.width != frame->width || runtime.height != frame->height
      || runtime.render_width != frame->render_width || runtime.render_height != frame->render_height
      || runtime.mode != frame->mode || runtime.temporal_upscaling != frame->temporal_upscaling
      || runtime.temporal_shader_hash != frame->temporal_shader_hash) {
    renodx::utils::log::i("[Tales AA input] hash ", renodx::utils::log::AsHex(frame->temporal_shader_hash),
                          "; active ", frame->render_width, "x", frame->render_height,
                          "; allocated ", frame->source_width, "x", frame->source_height,
                          "; output ", frame->width, "x", frame->height);
    // A render-scale change can recreate the feature without resizing the
    // swapchain. Exclude that one-time allocation from steady-state timings.
    runtime.performance.End(context);
    frame->sample_cpu = false;
    frame->evaluation_started = {};
    diagnostics.jitter_frames = 0;
    diagnostics.jitter_phases.store(0);
    // NVIDIA stays initialized (a failed one is retried); its feature follows size and mode.
    auto backend = std::move(runtime.backend);
    auto constant_readback = std::move(runtime.constant_readback);
    auto performance = std::move(runtime.performance);
    runtime = Runtime{};
    if (!backend.Failed()) runtime.backend = std::move(backend);
    runtime.constant_readback = std::move(constant_readback);
    runtime.performance = std::move(performance);
    runtime.device = device;
    ComPtr<ID3D11Device1> device1;
    if (FAILED(device.As(&device1))) {
      SetStatus("Game TAA: D3D11.1 context-state support unavailable", frame->generation);
      return false;
    }
    const auto feature_level = device->GetFeatureLevel();
    const uint32_t flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
    if (FAILED(device1->CreateDeviceContextState(flags, &feature_level, 1, D3D11_SDK_VERSION,
                                                 __uuidof(ID3D11Device1), nullptr, runtime.private_state.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__0x7A1E0D01.data(), __0x7A1E0D01.size(), nullptr, runtime.prepare_shader.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__0xA3450ADB.data(), __0xA3450ADB.size(), nullptr, runtime.merge_shader.GetAddressOf()))) {
      SetStatus("Game TAA: private pipeline creation failed", frame->generation);
      return false;
    }
    const D3D11_BUFFER_DESC buffer_desc = {
        .ByteWidth = 96,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, runtime.prepare_constants.GetAddressOf()))) {
      SetStatus("Game TAA: input constant-buffer allocation failed", frame->generation);
      return false;
    }
    if (!create_texture(&runtime.depth, DXGI_FORMAT_R32_FLOAT, frame->render_width, frame->render_height)
        || !create_texture(&runtime.motion, DXGI_FORMAT_R16G16_FLOAT, frame->render_width, frame->render_height)) {
      SetStatus("Game TAA: private texture allocation failed", frame->generation);
      return false;
    }
    runtime.width = frame->width;
    runtime.height = frame->height;
    runtime.render_width = frame->render_width;
    runtime.render_height = frame->render_height;
    runtime.mode = frame->mode;
    runtime.temporal_upscaling = frame->temporal_upscaling;
    runtime.temporal_shader_hash = frame->temporal_shader_hash;
  }

  if (runtime.backend_failed) {
    SetStatus(std::string("Game TAA: ") + runtime.backend.Status(), frame->generation);
    return false;
  }
  // NVIDIA reads and writes RGBA16F: the game's R11G11B10F scene color is copied by the input preparation, its
  // R11G11B10F output written by the merge. When replacing, the color is copied anyway (the stock pass is skipped).
  const bool copy_color = replace || frame->color_format != DXGI_FORMAT_R16G16B16A16_FLOAT;
  const bool direct = replace && frame->target_format == DXGI_FORMAT_R16G16B16A16_FLOAT;
  if ((copy_color && !runtime.color.resource) || (!direct && !runtime.merged.resource)) {
    if (!((!copy_color || create_texture(&runtime.color, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->render_width, frame->render_height))
          && (direct
              || (create_texture(&runtime.output, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->width, frame->height)
                  && create_texture(&runtime.merged, frame->target_format, frame->width, frame->height))))) {
      runtime.color = {};
      runtime.merged = {};
      SetStatus("Game TAA: private texture allocation failed", frame->generation);
      return false;
    }
  }
  {
    // Commit only for the current frame and mode. When replacing, NVIDIA writes the
    // game's output; a failure lets the stock dispatch run (and overwrite it) instead.
    const std::shared_lock lock(state.mutex);
    if (frame->generation != state.generation || frame->frame != state.frame || requested_mode.load() != frame->mode) return false;
  }
  bool evaluated = false;
  {
    talesofarise::dlss::ContextStateScope restore(context, runtime.private_state.Get());
    runtime.performance.Mark(context);
    struct PrepareConstants {
      std::array<std::array<float, 4>, 4> clip_to_previous;
      uint32_t width;
      uint32_t height;
      std::array<float, 2> pixel_jitter;
      uint32_t copy_color;
      std::array<uint32_t, 3> padding;
    } constants = {
        .width = frame->render_width,
        .height = frame->render_height,
        .pixel_jitter = (frame->temporal_upscaling ? frame->pixel_jitter : std::array<float, 2>{}),
        .copy_color = (copy_color ? 1u : 0u),
    };
    static_assert(sizeof(constants) == 96);
    std::memcpy(constants.clip_to_previous.data(), frame->view[VIEW_CLIP_TO_PREV_CLIP].data(), 64);
    context->UpdateSubresource(runtime.prepare_constants.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11Buffer* buffers[] = {runtime.prepare_constants.Get()};
    ID3D11ShaderResourceView* prep_inputs[] = {frame->inputs[1].Get(), frame->inputs[2].Get(), copy_color ? frame->inputs[0].Get() : nullptr};
    ID3D11UnorderedAccessView* prep_outputs[] = {runtime.depth.uav.Get(), runtime.motion.uav.Get(), copy_color ? runtime.color.uav.Get() : nullptr};
    context->CSSetShader(runtime.prepare_shader.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, buffers);
    context->CSSetShaderResources(0, 3, prep_inputs);
    context->CSSetUnorderedAccessViews(0, 3, prep_outputs, nullptr);
    context->Dispatch((frame->render_width + 7) / 8, (frame->render_height + 7) / 8, 1);
    runtime.performance.Mark(context);
    // NVIDIA reads these outputs next (cheaper than ClearState).
    constexpr std::array<ID3D11ShaderResourceView*, 3> NO_INPUTS = {};
    constexpr std::array<ID3D11UnorderedAccessView*, 3> NO_OUTPUTS = {};
    context->CSSetShaderResources(0, 3, NO_INPUTS.data());
    context->CSSetUnorderedAccessViews(0, 3, NO_OUTPUTS.data(), nullptr);

    // UE 4.21's TAA buffer has no camera-cut flag (UE 4.26: b0[0].z); cuts are not detected yet.
    constexpr bool camera_cut = false;
    const bool restart = runtime.last_frame == UINT64_MAX;
    const bool skipped = !restart && frame->frame != runtime.last_frame + 1;
    if (camera_cut) ++diagnostics.camera_cuts;
    if (restart) ++diagnostics.restarts;
    if (skipped) ++diagnostics.skipped_frames;
    runtime.performance.Mark(context);
    evaluated = runtime.backend.Evaluate(context, {
                                                      .color = (copy_color ? runtime.color.resource.Get() : frame->resources[0].Get()),
                                                      .depth = runtime.depth.resource.Get(),
                                                      .motion_vectors = runtime.motion.resource.Get(),
                                                      .output = (direct ? frame->target.Get() : runtime.output.resource.Get()),
                                                      .width = frame->render_width,
                                                      .height = frame->render_height,
                                                      .jitter_x = frame->pixel_jitter[0],
                                                      .jitter_y = frame->pixel_jitter[1],
                                                      // The scene color is not pre-exposed (the tonemapper does not scale it).
                                                      .pre_exposure = 1.f,
                                                      .reset = camera_cut || restart || skipped,
                                                      .output_width = frame->width,
                                                      .output_height = frame->height,
                                                      .mode = mode.backend,
                                                      .preset = preset.load(),
                                                  });
    runtime.performance.Mark(context);
    if (evaluated && !direct) {
      // The stock output's alpha, and its RGB where NVIDIA's is not finite (when replacing: the stale output).
      context->ClearState();
      runtime.performance.Mark(context);
      ID3D11ShaderResourceView* merge_inputs[] = {runtime.output.srv.Get(), frame->target_srv.Get()};
      ID3D11UnorderedAccessView* merge_outputs[] = {runtime.merged.uav.Get()};
      context->CSSetShader(runtime.merge_shader.Get(), nullptr, 0);
      context->CSSetShaderResources(0, 2, merge_inputs);
      context->CSSetUnorderedAccessViews(0, 1, merge_outputs, nullptr);
      context->Dispatch((frame->width + 7) / 8, (frame->height + 7) / 8, 1);
      context->CopyResource(frame->target.Get(), runtime.merged.resource.Get());
    } else if (evaluated) {
      runtime.performance.Mark(context);
    }
  }
  runtime.performance.End(context);
  if (!evaluated) {
    runtime.backend_failed = runtime.backend.Failed();
    runtime.last_frame = UINT64_MAX;
    SetStatus(std::string("Game TAA: ") + runtime.backend.Status(), frame->generation);
    return false;
  }
  frame->committed = true;
  runtime.last_frame = frame->frame;
  {
    const std::unique_lock lock(state.mutex);
    if (frame->generation == state.generation) {
      ++state.completed;
      state.render_width = frame->render_width;
      state.render_height = frame->render_height;
      state.temporal_shader_hash = frame->temporal_shader_hash;
    }
  }
  SetStatus(std::string(mode.name) + " active — "
                + (frame->render_width == frame->width && frame->render_height == frame->height
                       ? "native resolution"
                       : "reduced-resolution input")
                + (replace ? ", replacing the game's TAA" : ""),
            frame->generation);
  return true;
}

void EvaluateFrame(Context&, const void* data) {
  if (requested_mode.load() == AAMode::OFF) return;
  auto* frame = const_cast<Frame*>(static_cast<const Frame*>(data));
  {
    const std::shared_lock lock(state.mutex);
    if (frame->generation != state.generation || frame->frame != state.frame
        || frame->mode != requested_mode.load()) return;
  }
  auto& runtime = *frame->runtime;
  auto* context = frame->context.Get();
  runtime.performance.Mark(context);
  if (!frame->from_mirror) {
    const auto readback_started = std::chrono::steady_clock::now();
    const bool readable = runtime.constant_readback.Complete(
        context, {std::as_writable_bytes(std::span(frame->taa)), std::as_writable_bytes(std::span(frame->view))});
    frame->cpu_readback_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readback_started).count();
    if (!readable) {
      SetStatus(std::string("Game TAA: ") + runtime.constant_readback.error, frame->generation);
      return;
    }
    if (frame->compare_mirror) {
      runtime.constant_readback.Compare(
          {std::as_bytes(std::span(frame->mirror_taa)), std::as_bytes(std::span(frame->mirror_view))},
          {std::as_bytes(std::span(frame->taa)), std::as_bytes(std::span(frame->view))});
    }
  }
  Evaluate(frame, false);
}

constexpr auto OnCommand = [](Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto mode = requested_mode.load();
  if (mode == AAMode::OFF || command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  if (std::ranges::find(TEMPORAL_SHADERS, command.matched_shader_hash) == TEMPORAL_SHADERS.end()) return {};
  bool mode_changed;
  {
    const std::unique_lock lock(state.mutex);
    if (state.device != command.cmd_list->get_device() || state.width == 0 || state.height == 0
        || state.claimed_frame == state.frame || !state.runtime) return {};
    mode_changed = state.last_mode != mode;
    state.last_mode = mode;
  }
  if (mode_changed) Reset(true);
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  if (native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
    SetStatus("Game TAA: temporal dispatch recorded on a deferred context");
    return {};
  }
  ComPtr<ID3D11Predicate> predicate;
  BOOL predicate_value = FALSE;
  native->GetPredication(predicate.GetAddressOf(), &predicate_value);
  if (predicate != nullptr) {
    SetStatus("Game TAA: predicated temporal commands are unsupported");
    return {};
  }

  auto frame = std::make_unique<Frame>();
  if (FAILED(native->QueryInterface(IID_PPV_ARGS(frame->context.GetAddressOf())))) return {};
  {
    const std::unique_lock lock(state.mutex);
    if (state.device != command.cmd_list->get_device() || state.width == 0 || state.height == 0
        || state.claimed_frame == state.frame || !state.runtime) return {};
    frame->width = state.width;
    frame->height = state.height;
    frame->mode = mode;
    frame->temporal_shader_hash = command.matched_shader_hash;
    frame->frame = state.frame;
    frame->generation = state.generation;
    frame->runtime = std::move(state.runtime);
  }
  if (frame->runtime->backend_failed) {
    SetStatus(std::string("Game TAA: ") + frame->runtime->backend.Status(), frame->generation);
    return {};
  }
  if (command.arguments.group_count_x != (frame->width + 7) / 8
      || command.arguments.group_count_y != (frame->height + 7) / 8 || command.arguments.group_count_z != 1) {
    SetStatus("Game TAA: unsupported temporal dispatch extent", frame->generation);
    return {};
  }
  frame->runtime->performance.Poll(frame->context.Get());
  std::array<ComPtr<ID3D11Buffer>, 2> buffers;
  std::array<uint32_t, 2> first_constants = {};
  std::array<uint32_t, 2> constant_counts = {};
  for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
    frame->context->CSGetConstantBuffers1(slot, 1, buffers[slot].GetAddressOf(),
                                          &first_constants[slot], &constant_counts[slot]);
  }
  const auto unsupported_texture = [generation = frame->generation]() -> renodx::utils::command_action::CallbackResult<Context> {
    SetStatus("Game TAA: unsupported source/output texture layout", generation);
    return {};
  };
  // inputs: scene color (t1), depth (t0), packed velocity (t3), each in the formats UE may use.
  constexpr std::array<uint32_t, 3> slots = {1, 0, 3};
  constexpr std::array<std::array<DXGI_FORMAT, 3>, 3> formats = {{
      {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT},
      {DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_R32_FLOAT},
      {DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UNORM},
  }};
  for (uint32_t index = 0; index < frame->inputs.size(); ++index) {
    native->CSGetShaderResources(slots[index], 1, frame->inputs[index].GetAddressOf());
    if (!frame->inputs[index]) return unsupported_texture();
    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    frame->inputs[index]->GetDesc(&srv);
    ComPtr<ID3D11Resource> resource;
    frame->inputs[index]->GetResource(resource.GetAddressOf());
    if (srv.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || srv.Texture2D.MostDetailedMip != 0
        || std::ranges::find(formats[index], srv.Format) == formats[index].end()
        || FAILED(resource.As(&frame->resources[index]))) return unsupported_texture();
    D3D11_TEXTURE2D_DESC desc = {};
    frame->resources[index]->GetDesc(&desc);
    if (index == 0) {
      frame->color_format = srv.Format;
      frame->source_width = desc.Width;
      frame->source_height = desc.Height;
    }
    if (desc.Width != frame->source_width || desc.Height != frame->source_height || desc.ArraySize != 1
        || desc.MipLevels != 1 || desc.SampleDesc.Count != 1) return unsupported_texture();
  }
  if (frame->source_width > (frame->width + 7) / 8 * 8 || frame->source_height > (frame->height + 7) / 8 * 8) {
    return unsupported_texture();
  }
  ComPtr<ID3D11UnorderedAccessView> target;
  native->CSGetUnorderedAccessViews(0, 1, target.GetAddressOf());
  if (!target) return unsupported_texture();
  D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
  target->GetDesc(&uav);
  ComPtr<ID3D11Resource> target_resource;
  target->GetResource(target_resource.GetAddressOf());
  if (uav.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D || uav.Texture2D.MipSlice != 0
      || (uav.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && uav.Format != DXGI_FORMAT_R11G11B10_FLOAT)
      || FAILED(target_resource.As(&frame->target))) return unsupported_texture();
  D3D11_TEXTURE2D_DESC target_desc = {};
  frame->target->GetDesc(&target_desc);
  if (target_desc.Width != frame->width || target_desc.Height != frame->height || target_desc.MipLevels != 1
      || target_desc.ArraySize != 1 || target_desc.SampleDesc.Count != 1
      || target_desc.Format != uav.Format
      || frame->target == frame->resources[0]) return unsupported_texture();
  frame->target_format = target_desc.Format;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& runtime = *frame->runtime;
  for (const auto& cached : runtime.target_views) {
    if (cached.resource == frame->target) {
      frame->target_srv = cached.srv;
      break;
    }
  }
  if (!frame->target_srv) {
    Texture cached = {.resource = frame->target};
    if (FAILED(device->CreateShaderResourceView(frame->target.Get(), nullptr, cached.srv.GetAddressOf()))) return unsupported_texture();
    frame->target_srv = cached.srv;
    runtime.target_views[runtime.next_target_view] = std::move(cached);
    runtime.next_target_view = (runtime.next_target_view + 1) % runtime.target_views.size();
  }
  const auto readback_started = std::chrono::steady_clock::now();
  auto& readback = runtime.constant_readback;
  readback.PollChecks(frame->context.Get());
  const std::array<size_t, 2> sizes = {sizeof(frame->taa), sizeof(frame->view)};
  const std::array<std::span<std::byte>, 2> cpu = {
      std::as_writable_bytes(std::span(frame->mirror_taa)), std::as_writable_bytes(std::span(frame->mirror_view))};
  bool mirrored = true;
  for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
    if (!buffers[slot]) {
      mirrored = false;
      continue;
    }
    const uint64_t offset = uint64_t{first_constants[slot]} * 16;
    talesofarise::dlss::constants::Watch(buffers[slot].Get(), static_cast<uint32_t>(std::min<uint64_t>(offset + sizes[slot], UINT32_MAX)));
    mirrored = mirrored && uint64_t{constant_counts[slot]} * 16 >= sizes[slot]
               && talesofarise::dlss::constants::Read(buffers[slot].Get(), offset, cpu[slot]);
  }
  if (mirrored && readback.Trusted()) {
    // No GPU wait: values from the game's own uploads, checked later against GPU copies.
    frame->taa = frame->mirror_taa;
    frame->view = frame->mirror_view;
    frame->from_mirror = true;
    readback.QueueCheck(frame->context.Get(), buffers, first_constants, {cpu[0], cpu[1]});
  } else {
    if (!readback.Queue(frame->context.Get(), buffers, first_constants, constant_counts, sizes)) {
      SetStatus(std::string("Game TAA: ") + readback.error, frame->generation);
      return {};
    }
    frame->compare_mirror = mirrored;
    if (!mirrored) ++readback.misses;
  }
  frame->cpu_readback_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readback_started).count();
  {
    const std::unique_lock lock(state.mutex);
    if (state.generation != frame->generation || state.frame != frame->frame || state.claimed_frame == state.frame) return {};
    state.claimed_frame = state.frame;
  }
  // Timings only while the overlay is open, and not for one-time shader/NGX initialization.
  const uint64_t overlay = overlay_frame.load();
  frame->measuring = overlay != 0 && frame->frame + 1 - overlay < 120;
  frame->sample_cpu = frame->measuring && runtime.last_frame != UINT64_MAX;
  if (frame->sample_cpu) runtime.performance.Begin(frame->context.Get());
  if (frame->from_mirror && replace_taa.load()) {
    // Constants are known before the dispatch: DLSS replaces the stock pass (~0 ms "stock TAA").
    runtime.performance.Mark(frame->context.Get());
    const bool replaced = Evaluate(frame.get(), true);
    auto* data = frame.release();
    if (replaced) return {.post_data = data, .bypass = true, .finalize_callback = FinishFrame};
    // Not evaluated: the stock dispatch runs (replayed), then cleanup.
    return {.post_data = data, .finalize_callback = FinishFrame};
  }
  auto* data = frame.release();
  return {.post_callback = EvaluateFrame, .post_data = data, .finalize_callback = FinishFrame};
};

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  if (swapchain->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  const auto desc = swapchain->get_device()->get_resource_desc(swapchain->get_back_buffer(0));
  Reset();
  const std::unique_lock lock(state.mutex);
  state.device = swapchain->get_device();
  state.width = desc.texture.width;
  state.height = desc.texture.height;
  talesofarise::engine_hooks::screen_size.store((uint64_t{desc.texture.width} << 32) | desc.texture.height);
}

void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool) {
  bool matching;
  {
    const std::unique_lock lock(state.mutex);
    matching = state.device == swapchain->get_device();
    if (matching) {
      state.width = 0;
      state.height = 0;
      state.device = nullptr;
      talesofarise::engine_hooks::screen_size.store(0);
    }
  }
  if (matching) Reset();
}

void OnDestroyDevice(reshade::api::device* device) {
  bool matching;
  {
    const std::shared_lock lock(state.mutex);
    matching = state.device == device;
  }
  if (matching) Reset();
}

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  talesofarise::pixel_trace::OnPresent(queue);
  const auto mode = requested_mode.load();
  bool changed;
  bool missed;
  {
    const std::unique_lock lock(state.mutex);
    if (state.device != swapchain->get_device()) return;
    changed = state.last_mode != mode;
    state.last_mode = mode;
    missed = state.claimed_frame != state.frame;
    ++state.frame;
    const uint64_t copied = talesofarise::dlss::constants::CopiedBytes();
    state.copied_per_frame = copied - state.copied_bytes;
    state.copied_bytes = copied;
  }
  if (changed) {
    // AA Off frees NVIDIA's memory.
    Reset(mode != AAMode::OFF);
  }
  if (mode == AAMode::OFF) {
    SetStatus("Off — original game TAA");
  } else if (missed || changed) {
    // Do not leave an old "active" label up when this view no longer matches.
    std::shared_lock lock(state.mutex);
    if (state.status.starts_with("DLAA active") || state.status.starts_with("DLSS ") || changed) {
      lock.unlock();
      SetStatus(std::string("Game TAA: waiting for ") + AA_MODES[static_cast<size_t>(mode)].label
                + "; keep the game's Anti-Aliasing on TAA");
    }
  }

  // Live render scale: find r.ScreenPercentage once, then keep it at the mode's value.
  auto& finder = live.finder;
  using Phase = talesofarise::engine_scale::Finder::Phase;
  if (live.enabled && !live.problem.empty() && live.retries < MAX_LIVE_RETRIES
      && std::chrono::steady_clock::now() >= live.retry_at) {
    ++live.retries;
    live.problem.clear();
    live.writes = 0;
    live.window = 0;
    // A moved or unreadable variable: search again.
    if (finder.phase() == Phase::FAILED || !finder.Get()) finder = {};
    renodx::utils::log::i("[Tales AA live scale] Retrying (", live.retries, "/", MAX_LIVE_RETRIES, ").");
  }
  if (live.enabled && live.problem.empty()) {
    if (finder.phase() == Phase::IDLE) {
      finder.Start();
      renodx::utils::log::i("[Tales AA live scale] Searching the game's exe for r.ScreenPercentage.");
    }
    if (finder.Searching()) {
      // About 1 ms per frame, for well under a second.
      if (!finder.Step(std::chrono::milliseconds(1))) {
        if (finder.phase() == Phase::FOUND) {
          renodx::utils::log::i("[Tales AA live scale] Found r.ScreenPercentage (", finder.Get().value_or(0), "%): ", finder.details());
        } else {
          renodx::utils::log::w("[Tales AA live scale] ", finder.status(), ": ", finder.details());
        }
      }
    } else if (finder.phase() == Phase::FOUND) {
      const float desired = (mode == AAMode::DLSS_CUSTOM ? live.custom : AA_MODES[static_cast<size_t>(mode)].live_percentage);
      const auto current = finder.Get();
      if (!current) {
        live.problem = "r.ScreenPercentage is no longer readable";
      } else if (desired <= 0) {
        if (live.controlling) finder.Set(live.game_value);
        live.controlling = false;
      } else {
        if (!live.controlling) {
          live.game_value = *current;
          live.controlling = true;
          live.target = 0;
        }
        if (*current != desired) {
          // The game's menu sets it too: that becomes the value to restore, then ours is
          // written back, unless the game never lets it stay.
          if (*current != live.target) live.game_value = *current;
          if (live.target == desired && ++live.writes > 20) {
            live.problem = "the game keeps resetting the render scale";
          } else if (!finder.Set(desired)) {
            live.problem = finder.status();
          }
          live.target = desired;
          live.seen_at_write = seen_frames.load();
          live.width_at_write = static_cast<uint32_t>(seen_input.load());
          live.verified = false;
          live.note.clear();
        } else if (live.target == desired && !live.verified && seen_frames.load() - live.seen_at_write >= 60) {
          // 60 temporal passes later (loading screens have none) the input must have changed.
          live.verified = true;
          live.retries = 0;
          const auto width = static_cast<uint32_t>(seen_input.load());
          uint32_t output_width;
          {
            const std::shared_lock lock(state.mutex);
            output_width = state.width;
          }
          // UE renders ceil(width * percentage / 100).
          const auto expected = static_cast<uint32_t>(std::ceil(static_cast<float>(output_width) * (desired / 100.f)));
          if (output_width != 0 && width != 0 && std::abs(static_cast<int64_t>(width) - static_cast<int64_t>(expected)) > 1) {
            if (width == live.width_at_write) {
              live.problem = std::format("no effect: the input stayed {} pixels wide instead of {}", width, expected);
            } else {
              live.note = std::format("the game renders {} pixels wide, not {}", width, expected);
              renodx::utils::log::w("[Tales AA live scale] At ", desired, "% ", live.note, ".");
            }
          }
        }
        if (++live.window >= 600) {
          live.window = 0;
          live.writes = 0;
        }
      }
      if (!live.problem.empty()) {
        live.retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        renodx::utils::log::w("[Tales AA live scale] Paused: ", live.problem);
      }
    }
  }
  if ((!live.enabled || !live.problem.empty()) && live.controlling) {
    if (finder.phase() == Phase::FOUND) finder.Set(live.game_value);
    live.controlling = false;
  }

  // Camera jitter: the game's code is searched once, then patched while an AA mode is on.
  auto& jitter_patch = camera_jitter.patch;
  if (!camera_jitter.searched) {
    camera_jitter.searched = true;
    if (uint8_t* site = talesofarise::code_patch::FindPattern(JITTER_SCALE, &jitter_patch.details); site != nullptr) {
      jitter_patch.address = site + JITTER_SCALE_BRANCH;
      jitter_patch.original = {0x75};
      jitter_patch.replacement = {0xEB};
      renodx::utils::log::i("[Tales AA jitter] Found the game's jitter scaling ", jitter_patch.details, ".");
    } else {
      renodx::utils::log::w("[Tales AA jitter] The game's jitter scaling was not found: ", jitter_patch.details, ".");
    }
  }
  if (const bool jitter = camera_jitter.enabled && mode != AAMode::OFF;
      jitter_patch.address != nullptr && jitter_patch.applied != jitter) {
    if (talesofarise::code_patch::ApplyCodePatch(&jitter_patch, jitter)) {
      renodx::utils::log::i("[Tales AA jitter] ", (jitter ? "On" : "Off (the game's)"), ".");
    } else {
      renodx::utils::log::w("[Tales AA jitter] Patch failed: ", jitter_patch.details, ".");
    }
  }

  // Graphics preset: the console is searched once (after r.ScreenPercentage's search), then each setting is kept at
  // the preset's value; a value the game sets again (its menu) becomes the one to restore.
  if (!graphics.searched && !finder.Searching()) {
    graphics.searched = true;
    std::string details;
    graphics.available = graphics.console.Start(&details);
    graphics.status = (graphics.available ? "" : "Graphics presets unavailable: " + details + " (details in ReShade.log).");
    if (graphics.available) {
      renodx::utils::log::i("[Tales graphics] ", details, ".");
    } else {
      renodx::utils::log::w("[Tales graphics] ", details, ".");
    }
    for (size_t index = 0; graphics.available && index < GRAPHICS_SETTINGS.size(); ++index) {
      auto& entry = graphics.entries[index];
      const auto own = std::ranges::find(talesofarise::engine_hooks::options, std::wstring_view(GRAPHICS_SETTINGS[index].name),
                                         [](const auto& option) { return std::wstring_view(option.name); });
      if (own == talesofarise::engine_hooks::options.end()) {
        entry.cvar = graphics.console.Find(GRAPHICS_SETTINGS[index].name, &entry.details);
      } else {
        entry.details = own->details;
        if (own->available) {
          entry.cvar = {.values = {&own->value, &own->value}};
        } else if (own->arise_sdk_cvar != nullptr) {
          // Arise-SDK's hooks in place: its live setting for the same thing instead, where it has one.
          std::string sdk_details;
          entry.cvar = graphics.console.Find(own->arise_sdk_cvar, &sdk_details);
          if (entry.cvar.values[0] != nullptr) {
            entry.details = std::format("Arise-SDK's {}, {}", std::filesystem::path(own->arise_sdk_cvar).string(), sdk_details);
          }
        }
      }
      renodx::utils::log::i("[Tales graphics] ", std::filesystem::path(GRAPHICS_SETTINGS[index].name).string(), ": ", entry.details, ".");
    }
    if (uint8_t* site = talesofarise::code_patch::FindPattern(SUN_SHADOW_ATLAS, &sun_shadow_atlas.details); site != nullptr) {
      sun_shadow_atlas.address = site + SUN_SHADOW_ATLAS_SHIFTED;
      sun_shadow_atlas.original = {0x01};
      sun_shadow_atlas.replacement = {0x02};
      renodx::utils::log::i("[Tales graphics] Found the sun shadow atlas size ", sun_shadow_atlas.details, ".");
    } else {
      graphics.entries[SUN_SHADOW_RESOLUTION].cvar = {};
      renodx::utils::log::w("[Tales graphics] The sun shadow atlas size was not found (", sun_shadow_atlas.details,
                            "): Sun shadow resolution unavailable.");
    }
  }
  bool wrote = false;
  for (size_t index = 0; graphics.available && index < GRAPHICS_SETTINGS.size(); ++index) {
    auto& entry = graphics.entries[index];
    if (entry.cvar.values[0] == nullptr) continue;
    const auto& setting = GRAPHICS_SETTINGS[index];
    const float preset_value = (entry.choice == 0 ? KEEP : entry.choices[entry.choice - 1]);
    const float current = entry.cvar.Get();
    if (std::isnan(preset_value)) {
      if (entry.controlling) {
        entry.cvar.Set(entry.game_value);
        wrote = true;
      }
      entry.controlling = false;
      continue;
    }
    if (!entry.controlling || current != entry.written) entry.game_value = current;
    entry.controlling = true;
    float desired = preset_value;
    if (setting.better == Better::HIGHER) desired = std::max(entry.game_value, preset_value);
    if (setting.better == Better::LOWER) desired = std::min(entry.game_value, preset_value);
    if (entry.cvar.is_int) desired = std::round(desired);
    if (current != desired) {
      entry.cvar.Set(desired);
      wrote = true;
    }
    entry.written = desired;
  }
  if (wrote) graphics.console.Refresh();
  talesofarise::engine_hooks::Update();
  const auto& sun_resolution = graphics.entries[SUN_SHADOW_RESOLUTION];
  if (const bool large_atlas = sun_resolution.controlling && sun_resolution.written > sun_resolution.game_value;
      sun_shadow_atlas.address != nullptr && sun_shadow_atlas.applied != large_atlas) {
    if (talesofarise::code_patch::ApplyCodePatch(&sun_shadow_atlas, large_atlas)) {
      renodx::utils::log::i("[Tales graphics] Sun shadow atlas ", (large_atlas ? "16384" : "8192 (the game's)"), ".");
    } else {
      renodx::utils::log::w("[Tales graphics] Sun shadow atlas patch failed: ", sun_shadow_atlas.details, ".");
    }
  }
}

void OnOverlay(reshade::api::effect_runtime*) {
  // Timings are only shown (and measured) in Advanced.
  if (advanced) {
    const std::shared_lock lock(state.mutex);
    overlay_frame.store(state.frame + 1);
  }
  using Phase = talesofarise::engine_scale::Finder::Phase;
  if (int settings_mode = (advanced ? 1 : 0); ImGui::Combo("Settings mode", &settings_mode, "Simple\0Advanced\0")) {
    advanced = settings_mode == 1;
    reshade::set_config_value(nullptr, "Tales AA", "SettingsMode", settings_mode);
  }
  auto mode = requested_mode.load();
  if (ImGui::BeginCombo("AA mode", AA_MODES[static_cast<size_t>(mode)].label)) {
    for (const AAMode option : MENU_ORDER) {
      if (ImGui::Selectable(AA_MODES[static_cast<size_t>(option)].label, option == mode)) {
        mode = option;
        requested_mode.store(mode);
        reshade::set_config_value(nullptr, "Tales AA", "Mode", static_cast<int>(mode));
      }
    }
    ImGui::EndCombo();
  }
  if (live.enabled && mode == AAMode::DLSS_CUSTOM
      && ImGui::SliderFloat("Render scale", &live.custom, 1.f, 100.f, "%.0f%%")) {
    live.custom = std::clamp(std::round(live.custom), 1.f, 100.f);
    reshade::set_config_value(nullptr, "Tales AA", "CustomScale", live.custom);
  }
  const auto selected_preset = std::ranges::find(PRESETS, preset.load(), &PresetSettings::value);
  if (ImGui::BeginCombo("DLSS preset", selected_preset != PRESETS.end() ? selected_preset->label : PRESETS.back().label)) {
    for (const auto& option : PRESETS) {
      if (ImGui::Selectable(option.label, option.value == preset.load())) {
        preset.store(option.value);
        reshade::set_config_value(nullptr, "Tales AA", "Preset", option.value);
      }
    }
    ImGui::EndCombo();
  }
  if (bool replace = replace_taa.load(); ImGui::Checkbox("Replace the game's TAA pass (faster)", &replace)) {
    replace_taa.store(replace);
    reshade::set_config_value(nullptr, "Tales AA", "ReplaceTAA", replace);
  }

  std::string status;
  std::string constants;
  uint64_t copied_per_frame;
  uint64_t completed;
  std::array<uint32_t, 4> extents;
  uint32_t temporal_shader_hash;
  talesofarise::dlss::PerformanceSummary performance;
  {
    const std::shared_lock lock(state.mutex);
    status = state.status;
    constants = state.constants;
    copied_per_frame = state.copied_per_frame;
    completed = state.completed;
    extents = {state.render_width, state.render_height, state.width, state.height};
    temporal_shader_hash = state.temporal_shader_hash;
    performance = state.performance;
  }
  std::string live_status;
  if (!live.enabled) {
    live_status = "Render scale: set it to 100% in the game's menu.";
  } else if (!live.problem.empty()) {
    live_status = (live.retries < MAX_LIVE_RETRIES
                       ? "Live render scale paused: " + live.problem + ". The game's own value is back for now; retrying in a few seconds."
                       : "Live render scale stopped: " + live.problem + ". The game's own value was put back; untick and tick live render scale (Advanced) to retry.");
  } else if (live.finder.phase() == Phase::FAILED) {
    live_status = "Render scale: set it to 100% in the game's menu (not found in this version of the game; details in ReShade.log).";
  } else if (live.finder.phase() != Phase::FOUND) {
    live_status = live.finder.status() + "...";
  } else {
    live_status = std::format("Render scale: {:g}%{}", live.finder.Get().value_or(0),
                              live.note.empty() ? std::string() : "; " + live.note);
  }
  std::string jitter_status;
  const auto& jitter_patch = camera_jitter.patch;
  if (jitter_patch.applied) {
    jitter_status = "Camera jitter: on";
  } else if (jitter_patch.address != nullptr || !camera_jitter.enabled) {
    jitter_status = "Camera jitter: off (the game's)";
  } else if (camera_jitter.searched) {
    jitter_status = "Camera jitter unavailable: this version of the game is not recognized (details in ReShade.log). DLAA still works, with more smearing on small details.";
  }
  ImGui::Separator();
  ImGui::TextWrapped("%s", status.c_str());
  if (advanced || live.finder.phase() == Phase::FAILED) ImGui::TextWrapped("%s", live_status.c_str());
  if (advanced || (!jitter_patch.applied && jitter_patch.address == nullptr && camera_jitter.searched)) {
    ImGui::TextWrapped("%s", jitter_status.c_str());
  }
  if (completed != 0 && status.find("active") != std::string::npos) {
    ImGui::Text("Input %ux%u -> output %ux%u", extents[0], extents[1], extents[2], extents[3]);
  }
  const uint32_t phases = diagnostics.jitter_phases.load();
  if (mode != AAMode::OFF && status.starts_with("Game TAA")) {
    ImGui::TextWrapped("In the game's graphics settings, keep Anti-Aliasing on TAA.");
  }
  const auto& version = dlss_library.version;
  const bool old_library = version[0] != 0
                           && std::array<uint32_t, 2>{version[0], version[1]} < talesofarise::dlss::MINIMUM_RUNTIME_VERSION;
  if (dlss_library.folder.empty()) {
    ImGui::TextWrapped("nvngx_dlss.dll is missing: download the latest nvngx_dlss.dll and put it next to this addon (the game's Binaries\\Win64 folder), then restart the game.");
  } else if (old_library) {
    ImGui::TextWrapped("nvngx_dlss.dll %u.%u.%u is older than %u.%u: presets M and L are not available, NVIDIA uses its default. Download the latest nvngx_dlss.dll.",
                       version[0], version[1], version[2], talesofarise::dlss::MINIMUM_RUNTIME_VERSION[0],
                       talesofarise::dlss::MINIMUM_RUNTIME_VERSION[1]);
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Graphics (applied live; a preset never lowers the game's own settings):");
  const auto is_cpu_heavy = [](const GraphicsSetting& setting) {
    return std::ranges::find(CPU_HEAVY_SETTINGS, std::wstring_view(setting.name)) != CPU_HEAVY_SETTINGS.end();
  };
  const auto far_sun_shadow = [](const GraphicsSetting& setting) {
    return std::ranges::find(FAR_SUN_SHADOWS, std::wstring_view(setting.name), &PresetOverride::name);
  };
  // The choice holding `value` (KEEP or none: the game's), saved.
  const auto set_choice = [](GraphicsEntry* entry, float value) {
    const auto found = std::ranges::find(entry->choices, value);
    entry->choice = (found == entry->choices.end() ? 0 : static_cast<int>(found - entry->choices.begin()) + 1);
    SaveChoice(*entry);
  };
  for (size_t preset_index = 0; preset_index < GRAPHICS_PRESETS.size(); ++preset_index) {
    if (preset_index != 0) ImGui::SameLine();
    if (ImGui::Button(GRAPHICS_PRESETS[preset_index])) {
      for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
        const auto& setting = GRAPHICS_SETTINGS[index];
        const auto preset_values = std::span(setting.values).first(MAXIMUM_PRESET);
        // Every setting but shaders on load gets the preset's value (KEEP: the game's), so none keeps an older choice.
        if (std::wstring_view(setting.name) == NO_PRESET_SETTING) continue;
        auto& entry = graphics.entries[index];
        float value = KEEP;
        if (preset_index == 0 && std::wstring_view(setting.name) == EVERY_PRESET_SETTING) {
          value = preset_values[0];
        } else if (preset_index != 0) {
          value = preset_values[std::min(preset_index, MAXIMUM_PRESET) - 1];
          if (preset_index == BEYOND_MAXIMUM_PRESET && !entry.choices.empty()
              && std::ranges::find(BEYOND_MAXIMUM_SETTINGS, std::wstring_view(setting.name)) != BEYOND_MAXIMUM_SETTINGS.end()) {
            value = (setting.better == Better::LOWER ? entry.choices.front() : entry.choices.back());
          }
        }
        if (weak_cpu && is_cpu_heavy(setting)) {
          value = KEEP;
        }
        if (const auto sun_override = far_sun_shadow(setting); far_sun_shadows && sun_override != FAR_SUN_SHADOWS.end()) {
          value = sun_override->value;
        }
        set_choice(&entry, value);
      }
    }
  }
  if (ImGui::Checkbox("Weak CPU", &weak_cpu)) {
    reshade::set_config_value(nullptr, "Tales AA", "WeakCPU", weak_cpu);
    if (weak_cpu) {
      for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
        if (!is_cpu_heavy(GRAPHICS_SETTINGS[index])) continue;
        set_choice(&graphics.entries[index], KEEP);
      }
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("On: every preset leaves Keep characters behind the camera and Never hide characters at the game's\n"
                      "values (ticking it sets them now). They only cost CPU time: on CPUs with less cache they can cost\n"
                      "a lot of fps. Off: pick a preset again to turn them back on.");
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Far sun shadows (mountains)", &far_sun_shadows)) {
    reshade::set_config_value(nullptr, "Tales AA", "FarSunShadows", far_sun_shadows);
    if (far_sun_shadows) {
      for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
        if (const auto sun_override = far_sun_shadow(GRAPHICS_SETTINGS[index]); sun_override != FAR_SUN_SHADOWS.end()) {
          set_choice(&graphics.entries[index], sun_override->value);
        }
      }
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Sunlight can shine through far mountains and big rocks where the sun's shadows end too early\n"
                      "(e.g. the first area after the tutorial).\n"
                      "On: every preset sets Sun shadow distance 3 and Sun shadow resolution 8192 (ticking it sets them\n"
                      "now): the shadows reach those mountains.\n"
                      "Trade-off: the same shadow detail is spread over more ground, so shadows near you get softer and\n"
                      "can shimmer a little (8192 makes up for part of it). That is why no preset does this on its own.\n"
                      "Off: pick a preset again for its own sun shadows.");
  }
  if (!graphics.status.empty()) ImGui::TextWrapped("%s", graphics.status.c_str());
  ImGui::TextWrapped("Some settings apply after loading a new area, anisotropic filtering to textures loaded afterwards. Maximum is very heavy and uses a lot of VRAM; Beyond Maximum also pushes outline distance, NPC distance, monster distance, NPC / monster detail distance, terrain detail and grass draw distance to their highest values. Every preset keeps the game's volumetric fog (the most expensive effect, no visible gain above it): finer, cheaper or off in its own settings.");
  const auto& volumetric_fog = graphics.entries[VOLUMETRIC_FOG].cvar;
  const bool fog_off = volumetric_fog.values[0] != nullptr && volumetric_fog.Get() == 0.f;
  const char* open_group = nullptr;
  bool group_open = false;
  for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
    const auto& setting = GRAPHICS_SETTINGS[index];
    auto& entry = graphics.entries[index];
    if (open_group == nullptr || std::string_view(open_group) != setting.group) {
      open_group = setting.group;
      group_open = ImGui::CollapsingHeader(setting.group);
    }
    if (!group_open) continue;
    const bool usable = entry.cvar.values[0] != nullptr;
    const float game_value = (entry.controlling ? entry.game_value : (usable ? entry.cvar.Get() : 0.f));
    const std::string game_label = (usable ? std::format("Game's ({:g})", game_value) : std::string("Game's"));
    ImGui::BeginDisabled(!usable || (fog_off && std::wstring_view(setting.name).starts_with(L"r.VolumetricFog.")));
    const auto choice_label = [&](int choice) {
      if (choice == 0) return game_label;
      return std::format("{:g}", entry.choices[choice - 1]);
    };
    // A slider over the choices, showing the current one (as RenoDX's labelled settings).
    if (ImGui::SliderInt(setting.label, &entry.choice, 0, static_cast<int>(entry.choices.size()), choice_label(entry.choice).c_str(),
                         ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
      SaveChoice(entry);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("%s%s%s%s", (setting.tooltip != nullptr ? setting.tooltip : ""), (setting.tooltip != nullptr ? "\n\n" : ""),
                        entry.key.c_str(),
                        (usable ? (entry.controlling ? std::format(": now {:g}", entry.cvar.Get()) : std::string(": the game's"))
                                : ": unavailable (" + entry.details + ")")
                            .c_str());
    }
  }
  if (!advanced) return;

  ImGui::Separator();
  if (ImGui::Checkbox("Keep the game's render scale at 100% for DLAA", &live.enabled)) {
    reshade::set_config_value(nullptr, "Tales AA", "LiveScale", live.enabled);
    if (live.enabled) {
      live.problem.clear();
      live.retries = 0;
      if (live.finder.phase() == Phase::FAILED) live.finder = {};
    }
  }
  ImGui::TextWrapped("DLAA needs the game's render scale at 100%%. On: set while DLAA is on, the game's own value back when AA mode is Off.");
  if (ImGui::Checkbox("Camera jitter for DLAA", &camera_jitter.enabled)) {
    reshade::set_config_value(nullptr, "Tales AA", "CameraJitter", camera_jitter.enabled);
  }
  ImGui::TextWrapped("The game turns off the camera's sub-pixel jitter, which DLAA needs for fine detail and to keep small particles from smearing. On: turned on while DLAA is on (8 positions, shown below), the game's own when AA mode is Off.");
  ImGui::TextWrapped("DLSS presets: E is the older CNN model, K the first transformer model, M and L the second; L is the sharpest and heaviest. A preset forced in the NVIDIA app or NVIDIA Profile Inspector overrides this one.");
  ImGui::TextWrapped("DLAA only: the game's TAA never upscales, so there is no DLSS upscaling (render scale stays at 100%%).");
  ImGui::TextWrapped("Replacing the game's TAA pass skips it entirely (faster). Off: the game's TAA runs first and DLAA is written over it.");
  ImGui::Separator();
  // Pixel trace (pixel_trace.hpp): a red cross marks the pixel.
  ImGui::SliderFloat2("Traced pixel (% of width, height)", traced_pixel.data(), 0.f, 100.f, "%.1f%%");
  const ImVec2 screen = ImGui::GetIO().DisplaySize;
  const ImVec2 cross = {screen.x * traced_pixel[0] / 100.f, screen.y * traced_pixel[1] / 100.f};
  ImDrawList* foreground = ImGui::GetForegroundDrawList(nullptr);
  foreground->AddLine({cross.x - 12.f, cross.y}, {cross.x + 12.f, cross.y}, IM_COL32(255, 0, 0, 255), 2.f);
  foreground->AddLine({cross.x, cross.y - 12.f}, {cross.x, cross.y + 12.f}, IM_COL32(255, 0, 0, 255), 2.f);
  if (ImGui::Button("Trace this pixel for one frame")) {
    talesofarise::pixel_trace::Request(traced_pixel[0] / 100.f, traced_pixel[1] / 100.f);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Finds which of the game's passes draws the pixel under the red cross, and in which format.\n"
                      "During the next frame, every draw, dispatch, clear and copy that changes it is saved to\n"
                      "tales_pixel_trace.txt next to the game's exe (one short hitch).");
  }
  ImGui::TextWrapped("%s", talesofarise::pixel_trace::Status().c_str());
  ImGui::Separator();
  if (!dlss_library.folder.empty()) {
    ImGui::TextWrapped("nvngx_dlss.dll %u.%u.%u %s (%s). An override in the NVIDIA app or Profile Inspector can load another version; the DLSS indicator shows the one in use.",
                       version[0], version[1], version[2], dlss_library.place,
                       reinterpret_cast<const char*>(std::filesystem::path(dlss_library.folder).u8string().c_str()));
  }
  ImGui::Text("Successful AA frames: %llu", static_cast<unsigned long long>(completed));
  if (completed != 0) {
    ImGui::Text("TAA shader: 0x%08X", temporal_shader_hash);
    ImGui::Text("Camera jitter pattern: %s positions (NVIDIA recommends %u or more)",
                phases != 0 ? std::to_string(phases).c_str() : "measuring", 8u);
    ImGui::Text("NVIDIA history resets: camera cuts %llu, skipped frames %llu, restarts %llu",
                static_cast<unsigned long long>(diagnostics.camera_cuts.load()),
                static_cast<unsigned long long>(diagnostics.skipped_frames.load()),
                static_cast<unsigned long long>(diagnostics.restarts.load()));
  }
  if (!constants.empty()) {
    ImGui::TextWrapped("Constants: %s", constants.c_str());
    ImGui::Text("CPU copy uploads: %.1f KB/frame", static_cast<double>(copied_per_frame) / 1024);
  }
  if (performance.cpu_samples != 0) {
    ImGui::Separator();
    ImGui::Text("CPU constants (queue + wait): %.3f ms", performance.cpu_readback_ms);
    ImGui::Text("CPU addon submission: %.3f ms", performance.cpu_evaluate_ms);
    if (performance.gpu_samples != 0) {
      ImGui::Text("GPU game TAA: %.3f ms", performance.gpu_ms[0]);
      ImGui::Text("GPU input preparation: %.3f ms", performance.gpu_ms[1]);
      ImGui::Text("GPU NVIDIA evaluation: %.3f ms", performance.gpu_ms[2]);
      ImGui::Text("GPU merge + copy (none when replacing): %.3f ms", performance.gpu_ms[3]);
    } else {
      ImGui::TextUnformatted("GPU timing samples pending or unavailable; AA is unaffected.");
    }
    ImGui::Text("Timing samples: CPU %llu / GPU %llu; skipped %llu / rejected %llu",
                static_cast<unsigned long long>(performance.cpu_samples), static_cast<unsigned long long>(performance.gpu_samples),
                static_cast<unsigned long long>(performance.dropped), static_cast<unsigned long long>(performance.rejected));
    ImGui::TextWrapped("Averages since selecting this AA mode, measured only while Advanced is open. CPU and GPU times overlap: do not add them together. Compare FPS with this overlay closed too.");
  }
}

}  // namespace

extern "C" __declspec(dllexport) const char* NAME = "RenoDX Tales of Arise DLAA + Graphics";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "NVIDIA DLAA on the game's TAA, with the camera jitter it needs, and graphics settings beyond the game's menu. Independent of the HDR addon.";

extern "C" __declspec(dllexport) bool AddonInit(HMODULE module, HMODULE) {
  if (!registered) {
    if (!reshade::register_addon(module)) return false;
    registered = true;
  }
  if (attached) return true;
  // Settings apply at launch, without opening the overlay.
  int saved_mode = 0;
  if (reshade::get_config_value(nullptr, "Tales AA", "Mode", saved_mode)
      && saved_mode >= 0 && static_cast<size_t>(saved_mode) < AA_MODES.size()
      && std::ranges::find(MENU_ORDER, static_cast<AAMode>(saved_mode)) != MENU_ORDER.end()) {
    requested_mode.store(static_cast<AAMode>(saved_mode));
  }
  reshade::get_config_value(nullptr, "Tales AA", "LiveScale", live.enabled);
  reshade::get_config_value(nullptr, "Tales AA", "CameraJitter", camera_jitter.enabled);
  reshade::get_config_value(nullptr, "Tales AA", "WeakCPU", weak_cpu);
  reshade::get_config_value(nullptr, "Tales AA", "FarSunShadows", far_sun_shadows);
  for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
    const auto& setting = GRAPHICS_SETTINGS[index];
    auto& entry = graphics.entries[index];
    entry.key = std::filesystem::path(setting.name).string();
    for (const auto& values : {std::span<const float>(setting.values), std::span<const float>(setting.more_extras)}) {
      for (const float value : values) {
        if (!std::isnan(value) && std::ranges::find(entry.choices, value) == entry.choices.end()) entry.choices.push_back(value);
      }
    }
    std::ranges::sort(entry.choices);
    // The slider's steps between neighbouring choices: every whole number across small gaps (cascades 6 -> 10), the
    // halves across small gaps of halves, the 1-2-5 numbers across gaps of more than 2x (draw distance 3 -> 10: 5).
    std::vector<float> steps;
    for (size_t choice = 0; choice + 1 < entry.choices.size(); ++choice) {
      const float low = entry.choices[choice];
      const float high = entry.choices[choice + 1];
      if (low == std::round(low) && high == std::round(high) && high - low <= 4.f && low <= 10.f) {
        for (float value = low + 1.f; value < high; ++value) steps.push_back(value);
      } else if (low * 2.f == std::round(low * 2.f) && high * 2.f == std::round(high * 2.f) && high - low <= 2.f) {
        // Halves across small gaps of halves (sun shadow distance 1.5 -> 3: 2, 2.5).
        for (float value = low + .5f; value < high; value += .5f) steps.push_back(value);
      } else if (low > 0.f && high > low * 2.f) {
        for (float decade = std::pow(10.f, std::floor(std::log10(low))); decade < high; decade *= 10.f) {
          for (const float digit : {1.f, 2.f, 5.f}) {
            if (const float value = decade * digit; value > low * 1.001f && value < high * .999f) steps.push_back(value);
          }
        }
      }
    }
    entry.choices.insert(entry.choices.end(), steps.begin(), steps.end());
    std::ranges::sort(entry.choices);
    // The saved value's nearest choice ("game" does not read as a number: the game's value).
    if (float saved = 0.f; !entry.choices.empty() && reshade::get_config_value(nullptr, GRAPHICS_SECTION, entry.key.c_str(), saved)) {
      const auto nearest = std::ranges::min_element(entry.choices, {}, [saved](float choice) { return std::abs(choice - saved); });
      entry.choice = static_cast<int>(nearest - entry.choices.begin()) + 1;
    }
  }
  dlss_library = talesofarise::dlss::FindDlssLibrary();
  if (dlss_library.folder.empty()) {
    renodx::utils::log::w("[Tales DLSS] nvngx_dlss.dll not found next to the addon or the game.");
  } else {
    renodx::utils::log::i("[Tales DLSS] nvngx_dlss.dll ", dlss_library.version[0], ".", dlss_library.version[1], ".",
                          dlss_library.version[2], " ", dlss_library.place, ".");
  }
  if (uint32_t saved_preset = 0; reshade::get_config_value(nullptr, "Tales AA", "Preset", saved_preset)
                                 && std::ranges::find(PRESETS, saved_preset, &PresetSettings::value) != PRESETS.end()) {
    preset.store(saved_preset);
  }
  if (int settings_mode = 0; reshade::get_config_value(nullptr, "Tales AA", "SettingsMode", settings_mode)) advanced = settings_mode == 1;
  if (bool replace = true; reshade::get_config_value(nullptr, "Tales AA", "ReplaceTAA", replace)) replace_taa.store(replace);
  if (reshade::get_config_value(nullptr, "Tales AA", "CustomScale", live.custom)) {
    live.custom = std::isfinite(live.custom) ? std::clamp(std::round(live.custom), 1.f, 100.f) : 50.f;
  }
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_overlay("Tales of Arise DLAA + Graphics", OnOverlay);
  for (const uint32_t shader_hash : TEMPORAL_SHADERS) {
    renodx::utils::command_action::Register(OnCommand, {
                                                           .shader_hash = shader_hash,
                                                           .command_types = renodx::utils::command_action::COMMAND_TYPE_DISPATCH,
                                                       });
  }
  talesofarise::dlss::constants::Use(DLL_PROCESS_ATTACH);
  renodx::utils::state::Use(DLL_PROCESS_ATTACH);
  renodx::utils::command_action::Use(DLL_PROCESS_ATTACH);
  // Engine hooks: installed now, while the game is still creating its device (no level runs yet).
  talesofarise::engine_hooks::Install();
  for (const auto& option : talesofarise::engine_hooks::options) {
    renodx::utils::log::i("[Tales engine hooks] ", std::filesystem::path(option.name).string(), ": ", option.details, ".");
  }
  renodx::utils::log::i("[Tales engine hooks] Sun shadow distance limit: ",
                        (talesofarise::engine_hooks::sun_distance_limit.applied ? "removed " : "kept "),
                        talesofarise::engine_hooks::sun_distance_limit.details, ".");
  attached = true;
  renodx::utils::log::i("[Tales DLSS] Native DLAA on UE 4.21 TAA 0x88765764 / 0xE2AF861D; preset choice; exact CB readbacks; camera jitter; graphics presets.");
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE) {
  if (attached) {
    renodx::utils::command_action::Unregister(OnCommand);
    renodx::utils::command_action::Use(DLL_PROCESS_DETACH);
    renodx::utils::state::Use(DLL_PROCESS_DETACH);
    talesofarise::dlss::constants::Use(DLL_PROCESS_DETACH);
    reshade::unregister_overlay("Tales of Arise DLAA + Graphics", OnOverlay);
    reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
    reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
    reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
    reshade::unregister_event<reshade::addon_event::present>(OnPresent);
    talesofarise::pixel_trace::Stop();
    talesofarise::code_patch::ApplyCodePatch(&camera_jitter.patch, false);
    for (auto& entry : graphics.entries) {
      if (entry.controlling) entry.cvar.Set(entry.game_value);
      entry.controlling = false;
    }
    graphics.console.Refresh();
    talesofarise::engine_hooks::Uninstall();
    talesofarise::code_patch::ApplyCodePatch(&sun_shadow_atlas, false);
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
