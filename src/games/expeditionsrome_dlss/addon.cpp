/*
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <d3d11_1.h>
#include <detours.h>
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
#include <numeric>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"
#include "./code_patch.hpp"
#include "./constant_mirror.hpp"
#include "./constant_readback.hpp"
#include "./contact_shadows.hpp"
#include "./engine_cvars.hpp"
#include "./engine_scale.hpp"
#include "./native_state.hpp"
#include "./ngx_backend.hpp"
#include "../talesofarise_dlss/frame_generation.hpp"
#include "../talesofarise_dlss/frame_trace.hpp"
#include "./performance.hpp"
#include "./ray_tracing.hpp"
#include "./temporal_capture.hpp"

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
  expeditionsrome::dlss::Mode backend;
  float live_percentage = 0;  // live render scale; Custom uses its slider
};

constexpr std::array<ModeSettings, 8> AA_MODES = {{
    {.label = "Off (game TAA)", .name = "Off", .minimum_scale = 0, .maximum_scale = 0, .backend = expeditionsrome::dlss::Mode::DLAA},
    {.label = "DLAA (100%)", .name = "DLAA", .minimum_scale = 1, .maximum_scale = 1, .backend = expeditionsrome::dlss::Mode::DLAA, .live_percentage = 100},
    // Live: UE renders ceil(size * percentage / 100); 66.66 and 33.33 give exactly 2560x1440 and 1280x720 at 4K.
    {.label = "DLSS Quality (~67%)", .name = "DLSS Quality", .minimum_scale = .64f, .maximum_scale = .70f, .backend = expeditionsrome::dlss::Mode::DLSS_QUALITY, .live_percentage = 66.66f},
    {.label = "DLSS Balanced (~58%)", .name = "DLSS Balanced", .minimum_scale = .56f, .maximum_scale = .60f, .backend = expeditionsrome::dlss::Mode::DLSS_BALANCED, .live_percentage = 58},
    {.label = "DLSS Performance (50%)", .name = "DLSS Performance", .minimum_scale = .50f, .maximum_scale = .52f, .backend = expeditionsrome::dlss::Mode::DLSS_PERFORMANCE, .live_percentage = 50},
    {.label = "DLSS Custom (1-100%)", .name = "DLSS Custom", .minimum_scale = .01f, .maximum_scale = 1, .backend = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
    {.label = "DLSS Ultra Performance (~33%)", .name = "DLSS Ultra Performance", .minimum_scale = .32f, .maximum_scale = .35f, .backend = expeditionsrome::dlss::Mode::DLSS_ULTRA_PERFORMANCE, .live_percentage = 33.33f},
    // Not an NVIDIA SDK mode: the common 77%, created on NVIDIA's Quality model.
    {.label = "DLSS Ultra Quality (77%)", .name = "DLSS Ultra Quality", .minimum_scale = .74f, .maximum_scale = .80f, .backend = expeditionsrome::dlss::Mode::DLSS_ULTRA_QUALITY, .live_percentage = 77},
}};

struct PresetSettings {
  uint32_t value;
  const char* label;
};

// UE pads its render-size buffers to multiples of 4 (8 in later versions: switched to once seen). DLSS render sizes
// are whole units of the output's aspect ratio in those multiples: the view fills its buffer and both axes get exactly
// the same scale (no custom-scale shimmer; ATN, Shogun 2).
std::atomic<uint32_t> buffer_multiple = 4;

constexpr std::array<PresetSettings, 4> PRESETS = {{
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_E, .label = "E (CNN, Fastest, Worst looking, not recommended)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_K, .label = "K (Transformer 1, Slower but better than E)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_M, .label = "M (Transformer 2, Slower than E, recommended for HDR)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_L, .label = "L (Transformer 2, Slowest, best looking, recommended for HDR)"},
}};

// Menu order; AAMode values stay as saved in ReShade.ini.
constexpr std::array<AAMode, 8> MENU_ORDER = {
    AAMode::OFF, AAMode::DLAA, AAMode::DLSS_ULTRA_QUALITY, AAMode::DLSS_QUALITY, AAMode::DLSS_BALANCED,
    AAMode::DLSS_PERFORMANCE, AAMode::DLSS_ULTRA_PERFORMANCE, AAMode::DLSS_CUSTOM};

// UE reuses 29C140B2/201C3D3D with reduced inputs at high screen percentages.
// A shader hash identifies the compute contract, not the active render scale.
constexpr std::array<uint32_t, 6> TEMPORAL_SHADERS = {
    0x29C140B2u, 0x201C3D3Du, 0xCCB4A156u, 0xD8C07EEEu, 0x4B43383Du, 0xB266ECB6u};

struct Texture {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

struct Runtime {
  expeditionsrome::dlss::Backend backend;
  expeditionsrome::dlss::ConstantReadback constant_readback;
  expeditionsrome::dlss::Performance performance;
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
  uint32_t widest_display = 0;  // the widest monitor mode (volume texture guard)
  uint64_t generation = 0;
  uint64_t frame = 0;
  uint64_t claimed_frame = UINT64_MAX;
  uint64_t completed = 0;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  uint32_t temporal_shader_hash = 0;
  AAMode last_mode = AAMode::OFF;
  expeditionsrome::dlss::PerformanceSummary performance;
  std::string status = "Off — original game TAA";
  std::string constants;
  uint64_t copied_bytes = 0;
  uint64_t copied_per_frame = 0;
} state;

std::atomic<AAMode> requested_mode = AAMode::OFF;
std::atomic<uint32_t> preset = NVSDK_NGX_DLSS_Hint_Render_Preset_L;
// nvngx_dlss.dll found at load (overlay warnings); the backend looks again when it starts.
expeditionsrome::dlss::DlssLibrary dlss_library;
bool advanced = false;  // overlay settings mode
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

// UE 4.26 keeps the jitter position in a signed byte (FSceneViewState +0x4AA, loaded with movsx). Past 128 positions
// (8 * (output / input)^2: below 25% scale) it turns negative and Halton(position + 1 <= 0) is 0, so the jitter stops
// for 127 of every 256 frames: DLSS pulses blurry/sharp. Loaded unsigned (movsx -> movzx), the pattern is right up to
// UE's cap of 255 positions (below 18% it repeats early). Applied while the addon is loaded.
constexpr std::array<std::string_view, 2> JITTER_POSITION_LOADS = {
    "44 0F BE B6 ?? ?? 00 00 41 FF C6 45 3B F4",  // the jitter setup: load, +1, wrap at the pattern length
    "CC 0F BE 81 ?? ?? 00 00 C3",                  // FSceneViewState::GetCurrentTemporalAASampleIndex
};
constexpr size_t SIGNED_LOAD_OPCODE = 2;  // 0F BE (movsx) -> 0F B6 (movzx)
std::array<expeditionsrome::code_patch::CodePatch, JITTER_POSITION_LOADS.size()> jitter_position_patches;

// DLAA / DLSS use NVIDIA's jitter (as Tales): Halton (2, 3) over the whole pixel, 8 * (output / input)^2 positions (8
// for DLAA), any length. UE's own: Halton only while it upscales and at most 255 positions (below 18% it repeats
// early); at 100% a Gaussian of r.TemporalAASamples positions, denser in the middle. Every path of the jitter setup
// ends at
//   movss [rdi + TemporalJitterPixels.x], xmm7; lea rbx, [rdi + ...]; movss [rdi + TemporalJitterPixels.y], xmm8
// and the projection jitter follows from xmm7 / xmm8. While an AA mode is on and the render size is known, the first
// 5 bytes jump to a block next to the code (rip-relative reach) that loads the addon's sample (written every present)
// into xmm7 / xmm8, stores x as the game did and jumps back; the jump is one atomic store (one aligned 8-byte block).
constexpr std::string_view JITTER_STORE =
    "F3 0F 11 BF ?? ?? 00 00 48 8D 9F ?? ?? 00 00 F3 44 0F 11 87 ?? ?? 00 00 F3 0F 58 FF";
constexpr size_t JITTER_STORE_SIZE = 8;  // movss [rdi + disp32], xmm7
constexpr size_t JUMP_SIZE = 5;          // jmp rel32
struct HaltonJitter {
  expeditionsrome::code_patch::CodePatch patch;  // the jump into the block
  float* sample = nullptr;                       // x, y in input pixels
  uint32_t positions = 0;
  uint32_t index = 0;
} halton_jitter;

// Live render scale (engine_scale.hpp): the AA mode's percentage is written into
// r.ScreenPercentage; Off or disabling puts the game's own value back.
struct LiveScale {
  expeditionsrome::engine_scale::Finder finder;
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
  // High, Ultra, Extreme, Insane; then one extra choice offered only in the setting's own list.
  std::array<float, 5> values;
  const char* tooltip = nullptr;  // shown above the cvar name
  std::array<float, 2> more_extras = {KEEP, KEEP};  // further choices offered only in the setting's own list
};
constexpr std::array<const char*, 6> GRAPHICS_PRESETS = {"Game's own", "High", "Ultra", "Extreme", "Insane", "Max"};
constexpr size_t INSANE_PRESET = 4;
// Every setting at its best choice: the highest (the lowest where lower is better), the optional ones' values.
constexpr size_t MAX_PRESET = 5;
// In no preset at all, Game's own included (ATN: never reset by accident): shaders on load.
constexpr std::wstring_view NO_PRESET_SETTING = L"r.CreateShadersOnLoad";
constexpr const char* OPTIONAL_GROUP = "Optional (changes the look)";
// Contact shadows' length in the Max preset (a share of the screen).
constexpr float CONTACT_SHADOWS_MAX = .03f;
constexpr auto GRAPHICS_SETTINGS = std::to_array<GraphicsSetting>({
    {"Distance and detail", L"r.ViewDistanceScale", "Draw distance", Better::HIGHER, {1.5f, 2.f, 3.f, 20.f, KEEP},
     "How far objects are drawn."},
    {"Distance and detail", L"r.StaticMeshLODDistanceScale", "Object detail distance", Better::LOWER, {.75f, .5f, .25f, .01f, .0001f},
     "How far objects keep their full detail (smaller: farther)."},
    {"Distance and detail", L"foliage.LODDistanceScale", "Foliage detail distance", Better::HIGHER, {1.5f, 2.f, 4.f, 50.f, KEEP},
     "How far trees and plants keep their full detail."},
    {"Distance and detail", L"grass.CullDistanceScale", "Grass draw distance", Better::HIGHER, {KEEP, 1.5f, 2.f, 4.f, 10.f},
     "How far grass is drawn."},
    {"Distance and detail", L"r.SkeletalMeshLODRadiusScale", "Character detail distance", Better::LOWER, {.75f, .5f, .25f, .01f, KEEP},
     "How far characters keep their full detail (smaller: farther)."},
    {"Distance and detail", L"r.SkeletalMeshLODBias", "Highest character detail everywhere", Better::LOWER, {KEEP, KEEP, KEEP, -10.f, KEEP},
     "-10: characters always at full detail."},
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
    {"Distance and detail", L"r.LandscapeLODDistributionScale", "Terrain detail distance", Better::HIGHER, {KEEP, KEEP, 2.f, 15.f, KEEP},
     "How far the terrain keeps its detail."},
    {"Distance and detail", L"r.LandscapeLOD0DistributionScale", "Terrain full detail distance", Better::HIGHER, {KEEP, KEEP, 2.f, 15.f, KEEP},
     "How far the terrain keeps its full detail."},
    {"Distance and detail", L"r.LandscapeLODBias", "Terrain detail bias", Better::LOWER, {KEEP, KEEP, KEEP, -1.f, KEEP},
     "-1: terrain one detail level finer everywhere."},
    {"Distance and detail", L"r.Streaming.HLODStrategy", "Distant proxy textures (2: all mips)", Better::HIGHER, {KEEP, KEEP, KEEP, 2.f, KEEP},
     "2: distant merged buildings with full-resolution textures."},
    // Unreal adds a 4-texel border per side (limit 16376). In Tales of Arise (UE 4.21), shadow resolutions above 8192
    // or texels per pixel 4, written live, crashed the game (a null shadow atlas), so no choice goes above 8192 / 3
    // here either.
    {"Shadows", L"r.Shadow.MaxResolution", "Light shadow resolution (point / spot)", Better::HIGHER, {4096.f, 4096.f, 8192.f, 8192.f, KEEP},
     "Shadow sharpness of point and spot lights."},
    {"Shadows", L"r.ShadowQuality", "Shadow filtering quality", Better::HIGHER, {KEEP, 4.f, 5.f, 5.f, KEEP},
     "Smoothing of shadow edges."},
    {"Shadows", L"r.Shadow.CSM.MaxCascades", "Sun shadow cascades", Better::HIGHER, {3.f, 4.f, 4.f, 4.f, KEEP},
     "Layers of the sun's shadow: more of them keep shadows sharp at every distance."},
    {"Shadows", L"r.Shadow.MaxCSMResolution", "Sun shadow resolution", Better::HIGHER, {4096.f, 4096.f, 4096.f, 4096.f, KEEP},
     "Unreal puts all sun shadow cascades into one 8192 x 8192 texture and does not draw a cascade that does not fit\n"
     "(its shadows vanish). So the resolution is limited by the cascade count: up to 4 cascades 4096, up to 9\n"
     "cascades 2730, up to 16 cascades 2048."},
    // The engine clamps it to 2.
    {"Shadows", L"r.Shadow.DistanceScale", "Sun shadow distance", Better::HIGHER, {KEEP, 1.25f, 1.5f, 2.f, KEEP},
     "How far the sun's shadows reach."},
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
    {"Shadows", L"r.Shadow.TexelsPerPixelRectlight", "Rect light shadow resolution", Better::HIGHER, {KEEP, KEEP, 2.f, 3.f, KEEP},
     "Shadow sharpness from area lights."},
    {"Shadows", L"r.CapsuleShadowsFullResolution", "Character soft shadows at full resolution", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "1: the soft shadows under characters at full resolution (the game: half)."},
    {"Shadows", L"r.DFFullResolution", "Distance field shadows at full resolution", Better::HIGHER, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "1: distance field shadows (far objects) at full resolution (the game: half)."},
    {"Shadows", L"r.Shadow.MinResolution", "Distant object shadow resolution", Better::HIGHER, {KEEP, KEEP, KEEP, KEEP, 64.f},
     "Least resolution of distant objects' shadows."},
    {"Shadows", L"r.Shadow.FadeResolution", "Distant object shadow fade", Better::LOWER, {KEEP, KEEP, KEEP, KEEP, 32.f},
     "Resolution at which distant objects' shadows fade out (smaller: later)."},
    {"Lighting and reflections", L"r.AmbientOcclusionLevels", "Ambient occlusion levels", Better::HIGHER, {KEEP, KEEP, 3.f, 3.f, KEEP},
     "Detail levels of the ambient occlusion (shading in corners); 3 is the most."},
    {"Lighting and reflections", L"r.AmbientOcclusionMipLevelFactor", "Ambient occlusion detail (0: full)", Better::LOWER, {KEEP, .4f, .2f, 0.f, KEEP},
     "Detail of the ambient occlusion (0: full)."},
    {"Lighting and reflections", L"r.AOQuality", "Distance field ambient occlusion quality", Better::HIGHER, {KEEP, KEEP, 2.f, 2.f, KEEP},
     "Quality of the large-scale (distance field) ambient occlusion."},
    {"Lighting and reflections", L"r.SSR.Quality", "Screen-space reflections", Better::HIGHER, {KEEP, 3.f, 4.f, 4.f, KEEP},
     "Quality of reflections on shiny surfaces."},
    // -1 (the game's): the post process volume's limit; 1: every roughness. Changes the look (ATN: wanted from Extreme).
    {"Lighting and reflections", L"r.SSR.MaxRoughness", "Reflections on rough surfaces", Better::EXACT, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "1: screen-space reflections on rough surfaces too (the game's: the level's own limit). Shinier, slower."},
    {"Lighting and reflections", L"r.SceneColorFormat", "Scene color precision (4: RGBA16F)", Better::HIGHER, {KEEP, 4.f, 4.f, 4.f, KEEP},
     "4: 16-bit color for the scene (less banding)."},
    {"Lighting and reflections", L"r.SSS.Quality", "Skin (subsurface scattering) quality", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "Quality of light scattering in skin."},
    {"Lighting and reflections", L"r.SSS.HalfRes", "Full-resolution skin (subsurface scattering)", Better::LOWER, {KEEP, 0.f, 0.f, 0.f, KEEP},
     "0: light scattering in skin at full resolution."},
    // Volumetric fog is the most expensive effect: in no preset, only the game's value or less (EXACT: these may go
    // below the game's). Off is r.VolumetricFog 0; the grid settings clamp 0 to 1 (the most expensive).
    {"Fog and effects", L"r.VolumetricFog", "Volumetric fog", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 0.f},
     "0: off, much faster. The game's normal height fog stays; volumetric fog and its light shafts go."},
    {"Fog and effects", L"r.VolumetricFog.GridPixelSize", "Volumetric fog cell size (larger: faster)", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 12.f},
     "Pixels per fog cell. Larger: faster, blockier fog.", {16.f, KEEP}},
    {"Fog and effects", L"r.VolumetricFog.GridSizeZ", "Volumetric fog depth slices (fewer: faster)", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 96.f},
     "Fog slices into the distance. Fewer: faster, less fog detail in depth.", {64.f, KEEP}},
    {"Fog and effects", L"r.VolumetricFog.HistoryMissSupersampleCount", "Volumetric fog samples on camera moves (fewer: faster)", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 2.f},
     "Samples for fog the camera just revealed. Fewer: faster, noisier fog while turning.", {1.f, KEEP}},
    {"Fog and effects", L"r.LightShaftDownSampleFactor", "Light shaft resolution divisor", Better::LOWER, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "Light shafts' resolution divisor (1: full resolution)."},
    {"Fog and effects", L"r.MotionBlurQuality", "Motion blur quality", Better::HIGHER, {KEEP, 4.f, 4.f, 4.f, KEEP},
     "Motion blur quality (its strength stays)."},
    {"Fog and effects", L"r.DepthOfFieldQuality", "Depth of field quality", Better::HIGHER, {KEEP, KEEP, KEEP, 4.f, KEEP},
     "Quality of the depth of field blur (its strength stays)."},
    {"Fog and effects", L"r.FastBlurThreshold", "Bloom and blur quality", Better::HIGHER, {KEEP, KEEP, 100.f, 100.f, KEEP},
     "Quality of bloom and blurs (100: highest)."},
    {"Density (more than the game places)", L"grass.densityScale", "Grass density", Better::HIGHER, {KEEP, KEEP, KEEP, 10.f, 4.f},
     "More grass than the game places."},
    {"Density (more than the game places)", L"foliage.MaxTrianglesToRender", "Foliage triangle cap", Better::HIGHER, {KEEP, KEEP, KEEP, 1000000000.f, KEEP},
     "Limit on drawn plant triangles (higher: none dropped)."},
    // In no preset but Max: they change the game's look (ATN's old Engine.ini had them). EXACT: the game's value
    // or this one. (Its distance field shadows / AO, contact and capsule shadows: the game has them at 1.)
    {OPTIONAL_GROUP, L"r.AmbientOcclusionMaxQuality", "Ambient occlusion at its highest quality", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, -100.f},
     "-100: ambient occlusion at its highest quality even where a level asks for less (100: the level's own)."},
    {OPTIONAL_GROUP, L"r.LensFlareQuality", "Lens flare quality", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 3.f},
     "3: higher quality (bokeh-shaped) lens flares; the game uses 2."},
    // UE 4.26's screen-space global illumination (off in the game): light bouncing off what is on screen.
    {OPTIONAL_GROUP, L"r.SSGI.Enable", "Screen-space global illumination", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 1.f},
     "1: light bounces off nearby surfaces onto others (Unreal's own, screen-space). Brighter shade, slower."},
    {OPTIONAL_GROUP, L"r.SSGI.Quality", "Screen-space global illumination quality", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 4.f},
     "Rays per pixel of the global illumination (1-4; 4: least noise)."},
    {OPTIONAL_GROUP, L"r.SSGI.HalfRes", "Screen-space global illumination at half resolution", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 0.f},
     "0: the global illumination at full resolution (sharper, slower)."},
    // In no preset (a load-time trade-off, not quality): read when an area's shaders load.
    {"Loading", L"r.CreateShadersOnLoad", "Shaders on load", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 1.f},
     "1: creates all of an area's shaders while it loads, instead of the first time an effect or material appears on\n"
     "screen. Fewer stutters when something new shows up for the first time (the typical DirectX 11 hitching).\n"
     "Loading takes a bit longer and uses a bit more memory. Applies to areas loaded afterwards: load a save after\n"
     "changing it. 0: the engine's default (also overrides an Engine.ini line).",
     {0.f, KEEP}},
});
// Unreal puts all sun shadow cascades into one 8192 x 8192 atlas (1 << (GMaxTextureMipCount - 1), at most 14 mips;
// read from the exe) and does not draw a cascade that does not fit: the sun's resolution is limited to what its
// cascades leave. The cascades are applied first.
constexpr size_t SettingIndex(std::wstring_view name) {
  for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
    if (GRAPHICS_SETTINGS[index].name == name) return index;
  }
  return GRAPHICS_SETTINGS.size();
}
constexpr size_t SUN_SHADOW_CASCADES = SettingIndex(L"r.Shadow.CSM.MaxCascades");
constexpr size_t SUN_SHADOW_RESOLUTION = SettingIndex(L"r.Shadow.MaxCSMResolution");
// r.VolumetricFog 0 greys out the r.VolumetricFog.* settings.
constexpr size_t VOLUMETRIC_FOG = SettingIndex(L"r.VolumetricFog");
static_assert(SUN_SHADOW_CASCADES < SUN_SHADOW_RESOLUTION && SUN_SHADOW_RESOLUTION < GRAPHICS_SETTINGS.size());
static_assert(VOLUMETRIC_FOG < GRAPHICS_SETTINGS.size());
// "Effects at output resolution" (on by default, as Tales): while DLSS renders below 100%, settings sized in render
// pixels follow the render scale s (render / output), so these effects keep the detail they have at 100%: cell sizes
// and resolution divisors x s (nearest, at least 1), shadow texels per pixel / s (Unreal sizes object, spot, point and
// rect light shadows by their size in render pixels), half-resolution switches at full resolution (1 where higher is
// better, else 0). Sun shadows do not depend on the render scale; post-processing after the TAA runs at the output.
enum class OutputScaling : uint8_t { NONE,
                                     CELL_OR_DIVISOR,
                                     PER_PIXEL,
                                     FULL_RESOLUTION };
struct OutputScaled {
  std::wstring_view name;
  OutputScaling scaling;
};
constexpr std::array<OutputScaled, 9> OUTPUT_SCALED = {{
    {L"r.VolumetricFog.GridPixelSize", OutputScaling::CELL_OR_DIVISOR},
    {L"r.LightShaftDownSampleFactor", OutputScaling::CELL_OR_DIVISOR},
    {L"r.Shadow.TexelsPerPixel", OutputScaling::PER_PIXEL},
    {L"r.Shadow.TexelsPerPixelSpotlight", OutputScaling::PER_PIXEL},
    {L"r.Shadow.TexelsPerPixelPointlight", OutputScaling::PER_PIXEL},
    {L"r.Shadow.TexelsPerPixelRectlight", OutputScaling::PER_PIXEL},
    {L"r.CapsuleShadowsFullResolution", OutputScaling::FULL_RESOLUTION},
    {L"r.DFFullResolution", OutputScaling::FULL_RESOLUTION},
    {L"r.SSS.HalfRes", OutputScaling::FULL_RESOLUTION},
}};
constexpr auto OUTPUT_SCALING = [] {
  std::array<OutputScaling, GRAPHICS_SETTINGS.size()> scaling = {};
  for (const auto& [name, kind] : OUTPUT_SCALED) {
    scaling[SettingIndex(name)] = kind;
  }
  return scaling;
}();
bool effects_at_output = true;
// DLSS Frame Generation (Experimental, Tales' frame_generation.hpp): set at launch (config FrameGeneration).
bool frame_generation = false;
// Ray tracing (Experimental, ray_tracing.hpp): its settings (config RayTraced*, PathTracing*), installed when an effect
// is on at launch.
twattila::rt_scene::Settings ray_tracing_settings;
float ray_traced_bounce_strength = 1.f;
bool ray_tracing_installed = false;

// UE 4.26's View rows frame generation reads (Tales' 4.21 rows + 4: ClipToWorld came before them).
constexpr uint32_t VIEW_VIEW_TO_CLIP_NO_AA = 32;
constexpr uint32_t VIEW_FORWARD = 56;
constexpr uint32_t VIEW_UP = 57;
constexpr uint32_t VIEW_RIGHT = 58;
constexpr uint32_t VIEW_WORLD_CAMERA_ORIGIN = 63;
constexpr uint32_t VIEW_CLIP_TO_PREV_CLIP = 118;
// The tonemapper's permutations (the HDR addon's t00-t09): frame generation's hudless colour after them.
constexpr std::array<uint32_t, 10> TONEMAPPER_SHADERS = {0x316D5B96u, 0x70CBF75Eu, 0x3BAE1754u, 0xC4ABB3CCu, 0x3D18E77Bu,
                                                         0xA77C9003u, 0xB176941Cu, 0xAAAEB4F0u, 0x2501C25Fu, 0x37FDFDF2u};

// Frame generation's inputs from this frame's DLSS evaluation, submitted at present.
struct FrameGenerationFrame {
  std::shared_mutex mutex;
  talesofarise::frame_generation::Inputs inputs;
  uint64_t frame = UINT64_MAX;
} frame_generation_frame;
// Volume textures: D3D11 allows at most 2048 per side (Tales crashed creating a 3840-wide one: its divisors were scaled
// at the 720p startup window and the game sized the volume from the full output). The fog grid is render width / cell
// (UE clamps it too): cells stay at least max(output width, widest monitor mode) / 2048, so no window or monitor change
// can reach the limit. The only volume Effects at output resolution touches; the depth slices are not scaled. The other
// settings stay as large as at 100% (shadow maps: projected render pixels x texels per pixel, capped by
// r.Shadow.MaxResolution <= 8192; full-resolution switches: the render size).
constexpr size_t VOLUMETRIC_FOG_CELL = SettingIndex(L"r.VolumetricFog.GridPixelSize");
// Each setting's own choice (a slider, as Ryse's options): 0 the game's value, else one of its choices (its distinct
// preset values and extras, with steps between them); the preset buttons set every choice. Saved per setting in
// ReShade.ini, section GRAPHICS_SECTION, key the cvar name, by value (SaveChoice): steps move the indexes. (The old
// "Rome Graphics Settings" section held indexes.)
constexpr const char* GRAPHICS_SECTION = "Rome Graphics Values";

struct GraphicsEntry {
  expeditionsrome::engine_cvars::Cvar cvar;
  std::string details;  // int / float and the game's value at the search, or why it is unusable
  std::string key;      // the cvar name
  std::vector<float> choices;
  int choice = 0;
  bool controlling = false;
  float game_value = 0.f;
  float written = 0.f;
};
struct Graphics {
  expeditionsrome::engine_cvars::Console console;
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

// Texture LOD bias below 100% (DLSS section). With temporal upscaling UE 4.26 biases material textures (599 of the
// game's pixel shaders sample with View.MaterialTextureMipBias) by max(log2(scale) + r.ViewTextureMipBias.Offset,
// r.ViewTextureMipBias.Min), read every frame (PreVisibilityFrameSetup); the game's -0.3 and -1 leave textures blurrier
// than NVIDIA's log2(scale) - 1 below ~57%. While DLSS renders below 100%: offset 0 / -1 / -2 (Matched / Sharper /
// Sharpest) and no minimum; otherwise the game's values.
constexpr std::array<const wchar_t*, 2> TEXTURE_LOD_BIAS_CVARS = {L"r.ViewTextureMipBias.Offset", L"r.ViewTextureMipBias.Min"};
constexpr float LOWEST_MIP_BIAS = -16.f;  // D3D11's lowest; no minimum in practice
struct TextureLodBias {
  std::array<expeditionsrome::engine_cvars::Cvar, TEXTURE_LOD_BIAS_CVARS.size()> cvars;
  std::array<float, TEXTURE_LOD_BIAS_CVARS.size()> game_values = {};
  bool controlling = false;
  int choice = 2;  // 0 off (the game's), 1 matched, 2 sharper, 3 sharpest; Present thread (the overlay runs in it)
} texture_lod_bias;
void RestoreTextureLodBias() {
  if (!texture_lod_bias.controlling) return;
  for (size_t index = 0; index < TEXTURE_LOD_BIAS_CVARS.size(); ++index) {
    texture_lod_bias.cvars[index].Set(texture_lod_bias.game_values[index]);
  }
  texture_lod_bias.controlling = false;
}
bool registered = false;

struct Frame {
  std::unique_ptr<Runtime> runtime;
  ComPtr<ID3D11DeviceContext1> context;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> inputs;
  std::array<ComPtr<ID3D11Texture2D>, 3> resources;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11ShaderResourceView> target_srv;
  std::array<std::array<float, 4>, 27> taa;
  std::array<std::array<float, 4>, 130> view;
  // CPU copy of the same constants (constant_mirror.hpp).
  std::array<std::array<float, 4>, 27> mirror_taa;
  std::array<std::array<float, 4>, 130> mirror_view;
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
    renodx::utils::log::i("[Rome DLAA] ", message);
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
  const auto& taa = frame->taa;
  const auto& view = frame->view;
  const float render_width = taa[25][0];
  const float render_height = taa[25][1];
  if (std::isfinite(render_width) && std::isfinite(render_height) && render_width >= 1 && render_height >= 1
      && render_width <= 65536 && render_height <= 65536) {
    seen_input.store(static_cast<uint64_t>(render_width) | (static_cast<uint64_t>(render_height) << 32));
    seen_frames.fetch_add(1);
  }
  if (!std::isfinite(render_width) || !std::isfinite(render_height)
      || render_width < 1 || render_height < 1
      || render_width > frame->source_width || render_height > frame->source_height
      || std::floor(render_width) != render_width || std::floor(render_height) != render_height
      || taa[19][0] != frame->source_width || taa[19][1] != frame->source_height
      || taa[20][0] != width || taa[20][1] != height || taa[22][0] != width || taa[22][1] != height
      || taa[23] != std::array<float, 4>{0, 0, width, height}
      || taa[24][0] != 0 || taa[24][1] != 0
      || !std::isfinite(taa[26][2]) || !std::isfinite(taa[26][3])
      || std::abs(taa[26][2] - render_width / width) > .001f
      || std::abs(taa[26][3] - width / render_width) > .001f
      || !std::isfinite(taa[26][0]) || !std::isfinite(taa[26][1])
      || std::abs(taa[26][0]) > 1 || std::abs(taa[26][1]) > 1
      || !std::isfinite(view[129][1]) || view[129][1] <= 0
      || !std::isfinite(view[129][2]) || !std::isfinite(view[122][0]) || !std::isfinite(view[122][1])
      || std::abs(view[129][1] * view[129][2] - 1.f) > .001f
      || std::abs(view[122][0] * render_width / 2 - taa[26][0]) > .001f
      || std::abs(-view[122][1] * render_height / 2 - taa[26][1]) > .001f) {
    SetStatus("Stock TAA: unsupported temporal view layout", frame->generation);
    return false;
  }
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
    SetStatus(std::string("Stock TAA: unsupported scale/layout for ") + mode.label
                  + "; keep Fast or Ultra TAA and Temporal Upscaling ON below 100%",
              frame->generation);
    return false;
  }
  if (frame->temporal_upscaling && (frame->source_width != frame->render_width || frame->source_height != frame->render_height)
      && buffer_multiple.exchange(8) != 8) {
    renodx::utils::log::i("[Rome AA] The game pads ", frame->render_width, "x", frame->render_height, " to ",
                          frame->source_width, "x", frame->source_height, ": render sizes in multiples of 8 from now on.");
  }
  for (uint32_t row = 118; row < 122; ++row) {
    for (const float value : view[row]) {
      if (!std::isfinite(value)) {
        SetStatus("Stock TAA: invalid camera reprojection matrix", frame->generation);
        return false;
      }
    }
  }
  frame->values_validated = true;
  if (const std::array<float, 2> jitter = {taa[26][0], taa[26][1]}; diagnostics.jitter_frames == 0 || diagnostics.jitter_frames > 4096) {
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
    renodx::utils::log::i("[Rome AA input] hash ", renodx::utils::log::AsHex(frame->temporal_shader_hash),
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
      SetStatus("Stock TAA: D3D11.1 context-state support unavailable", frame->generation);
      return false;
    }
    const auto feature_level = device->GetFeatureLevel();
    const uint32_t flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
    if (FAILED(device1->CreateDeviceContextState(flags, &feature_level, 1, D3D11_SDK_VERSION,
                                                 __uuidof(ID3D11Device1), nullptr, runtime.private_state.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__0xB69D8215.data(), __0xB69D8215.size(), nullptr, runtime.prepare_shader.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__0xA3450ADB.data(), __0xA3450ADB.size(), nullptr, runtime.merge_shader.GetAddressOf()))) {
      SetStatus("Stock TAA: private pipeline creation failed", frame->generation);
      return false;
    }
    const D3D11_BUFFER_DESC buffer_desc = {
        .ByteWidth = 96,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, runtime.prepare_constants.GetAddressOf()))) {
      SetStatus("Stock TAA: input constant-buffer allocation failed", frame->generation);
      return false;
    }
    if (!create_texture(&runtime.depth, DXGI_FORMAT_R32_FLOAT, frame->render_width, frame->render_height)
        || !create_texture(&runtime.motion, DXGI_FORMAT_R16G16_FLOAT, frame->render_width, frame->render_height)) {
      SetStatus("Stock TAA: private texture allocation failed", frame->generation);
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
    SetStatus(std::string("Stock TAA: ") + runtime.backend.Status(), frame->generation);
    return false;
  }
  if (replace ? !runtime.color.resource : !runtime.merged.resource) {
    if (!(replace ? create_texture(&runtime.color, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->render_width, frame->render_height)
                  : create_texture(&runtime.output, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->width, frame->height)
                        && create_texture(&runtime.merged, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->width, frame->height))) {
      runtime.color = {};
      runtime.merged = {};
      SetStatus("Stock TAA: private texture allocation failed", frame->generation);
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
    expeditionsrome::dlss::ContextStateScope restore(context, runtime.private_state.Get());
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
        .pixel_jitter = (frame->temporal_upscaling
                             ? std::array<float, 2>{frame->taa[26][0], frame->taa[26][1]}
                             : std::array<float, 2>{}),
        .copy_color = (replace ? 1u : 0u),
    };
    static_assert(sizeof(constants) == 96);
    std::memcpy(constants.clip_to_previous.data(), frame->view[118].data(), 64);
    context->UpdateSubresource(runtime.prepare_constants.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11Buffer* buffers[] = {runtime.prepare_constants.Get()};
    ID3D11ShaderResourceView* prep_inputs[] = {frame->inputs[1].Get(), frame->inputs[2].Get(), replace ? frame->inputs[0].Get() : nullptr};
    ID3D11UnorderedAccessView* prep_outputs[] = {runtime.depth.uav.Get(), runtime.motion.uav.Get(), replace ? runtime.color.uav.Get() : nullptr};
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

    const bool camera_cut = std::bit_cast<uint32_t>(frame->taa[0][2]) != 0;
    const bool restart = runtime.last_frame == UINT64_MAX;
    const bool skipped = !restart && frame->frame != runtime.last_frame + 1;
    if (camera_cut) ++diagnostics.camera_cuts;
    if (restart) ++diagnostics.restarts;
    if (skipped) ++diagnostics.skipped_frames;
    runtime.performance.Mark(context);
    evaluated = runtime.backend.Evaluate(context, {
                                                      .color = (replace ? runtime.color.resource.Get() : frame->resources[0].Get()),
                                                      .depth = runtime.depth.resource.Get(),
                                                      .motion_vectors = runtime.motion.resource.Get(),
                                                      .output = (replace ? frame->target.Get() : runtime.output.resource.Get()),
                                                      .width = frame->render_width,
                                                      .height = frame->render_height,
                                                      .jitter_x = frame->taa[26][0],
                                                      .jitter_y = frame->taa[26][1],
                                                      .pre_exposure = frame->view[129][1],
                                                      .reset = camera_cut || restart || skipped,
                                                      .output_width = frame->width,
                                                      .output_height = frame->height,
                                                      .mode = mode.backend,
                                                      .preset = preset.load(),
                                                  });
    runtime.performance.Mark(context);
    if (evaluated && talesofarise::frame_generation::Presenting()) {
      const auto& view = frame->view;
      const auto vector = [&](uint32_t row) { return std::array<float, 3>{view[row][0], view[row][1], view[row][2]}; };
      talesofarise::frame_generation::Inputs inputs = {
          .depth = runtime.depth.resource,
          .motion = runtime.motion.resource,
          .render_width = frame->render_width,
          .render_height = frame->render_height,
          .jitter = {frame->taa[26][0], frame->taa[26][1]},
          .reset = camera_cut || restart || skipped,
          .camera = {
              .position = vector(VIEW_WORLD_CAMERA_ORIGIN),
              .right = vector(VIEW_RIGHT),
              .up = vector(VIEW_UP),
              .forward = vector(VIEW_FORWARD),
              // UE's reversed infinite projection: its near plane at [14]; as far, UE's world size (centimetres).
              .near_plane = view[VIEW_VIEW_TO_CLIP_NO_AA + 3][2],
              .far_plane = 2097152.f,
          },
      };
      for (size_t row = 0; row < 4; ++row) {
        std::memcpy(&inputs.camera.view_to_clip[row * 4], view[VIEW_VIEW_TO_CLIP_NO_AA + row].data(), 16);
        std::memcpy(&inputs.camera.clip_to_previous_clip[row * 4], view[VIEW_CLIP_TO_PREV_CLIP + row].data(), 16);
      }
      const std::unique_lock lock(frame_generation_frame.mutex);
      frame_generation_frame.inputs = std::move(inputs);
      frame_generation_frame.frame = frame->frame;
    }
    if (evaluated && !replace) {
      // After the stock dispatch: its alpha, and its RGB where NVIDIA's is not finite.
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
    SetStatus(std::string("Stock TAA: ") + runtime.backend.Status(), frame->generation);
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
                + (replace ? ", replacing game TAA (experimental)" : " (experimental)"),
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
      SetStatus(std::string("Stock TAA: ") + runtime.constant_readback.error, frame->generation);
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
    SetStatus("Stock TAA: temporal dispatch recorded on a deferred context");
    return {};
  }
  ComPtr<ID3D11Predicate> predicate;
  BOOL predicate_value = FALSE;
  native->GetPredication(predicate.GetAddressOf(), &predicate_value);
  if (predicate != nullptr) {
    SetStatus("Stock TAA: predicated temporal commands are unsupported");
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
    SetStatus(std::string("Stock TAA: ") + frame->runtime->backend.Status(), frame->generation);
    return {};
  }
  if (command.arguments.group_count_x != (frame->width + 7) / 8
      || command.arguments.group_count_y != (frame->height + 7) / 8 || command.arguments.group_count_z != 1) {
    SetStatus("Stock TAA: unsupported temporal dispatch extent", frame->generation);
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
    SetStatus("Stock TAA: unsupported source/output texture layout", generation);
    return {};
  };
  constexpr std::array<DXGI_FORMAT, 3> formats = {
      DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_R16G16_UNORM};
  for (uint32_t index = 0; index < frame->inputs.size(); ++index) {
    native->CSGetShaderResources(index + 1, 1, frame->inputs[index].GetAddressOf());
    if (!frame->inputs[index]) return unsupported_texture();
    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    frame->inputs[index]->GetDesc(&srv);
    ComPtr<ID3D11Resource> resource;
    frame->inputs[index]->GetResource(resource.GetAddressOf());
    if (srv.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || srv.Texture2D.MostDetailedMip != 0
        || srv.Format != formats[index] || FAILED(resource.As(&frame->resources[index]))) return unsupported_texture();
    D3D11_TEXTURE2D_DESC desc = {};
    frame->resources[index]->GetDesc(&desc);
    if (index == 0) {
      frame->source_width = desc.Width;
      frame->source_height = desc.Height;
    }
    if (desc.Width != frame->source_width || desc.Height != frame->source_height || desc.ArraySize != 1
        || desc.MipLevels != 1 || desc.SampleDesc.Count != 1) return unsupported_texture();
  }
  if (ray_tracing_installed) expeditionsrome::dlss::ray_tracing::KeepColor(native, frame->resources[0].Get());
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
      || uav.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || FAILED(target_resource.As(&frame->target))) return unsupported_texture();
  D3D11_TEXTURE2D_DESC target_desc = {};
  frame->target->GetDesc(&target_desc);
  if (target_desc.Width != frame->width || target_desc.Height != frame->height || target_desc.MipLevels != 1
      || target_desc.ArraySize != 1 || target_desc.SampleDesc.Count != 1
      || target_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT
      || frame->target == frame->resources[0]) return unsupported_texture();
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
    expeditionsrome::dlss::constants::Watch(buffers[slot].Get(), static_cast<uint32_t>(std::min<uint64_t>(offset + sizes[slot], UINT32_MAX)));
    mirrored = mirrored && uint64_t{constant_counts[slot]} * 16 >= sizes[slot]
               && expeditionsrome::dlss::constants::Read(buffers[slot].Get(), offset, cpu[slot]);
  }
  if (mirrored && readback.Trusted()) {
    // No GPU wait: values from the game's own uploads, checked later against GPU copies.
    frame->taa = frame->mirror_taa;
    frame->view = frame->mirror_view;
    frame->from_mirror = true;
    readback.QueueCheck(frame->context.Get(), buffers, first_constants, {cpu[0], cpu[1]});
  } else {
    if (!readback.Queue(frame->context.Get(), buffers, first_constants, constant_counts, sizes)) {
      SetStatus(std::string("Stock TAA: ") + readback.error, frame->generation);
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
  uint32_t widest_display = 0;
  EnumDisplayMonitors(
      nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        if (GetMonitorInfoW(monitor, &info) != FALSE && EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) != FALSE) {
          auto* widest = reinterpret_cast<uint32_t*>(data);
          *widest = std::max(*widest, static_cast<uint32_t>(mode.dmPelsWidth));
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&widest_display));
  const std::unique_lock lock(state.mutex);
  state.widest_display = widest_display;
  state.device = swapchain->get_device();
  state.width = desc.texture.width;
  state.height = desc.texture.height;
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
    }
  }
  if (matching) {
    expeditionsrome::dlss::temporal_capture::Reset();
    Reset();
  }
}

void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() == reshade::api::device_api::d3d11) {
    expeditionsrome::dlss::contact_shadows::Release(reinterpret_cast<ID3D11Device*>(device->get_native()));
    if (ray_tracing_installed) expeditionsrome::dlss::ray_tracing::Release(device);
  }
  bool matching;
  {
    const std::shared_lock lock(state.mutex);
    matching = state.device == device;
  }
  if (matching) {
    expeditionsrome::dlss::temporal_capture::Reset();
    Reset();
  }
}

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // Debug: renodx_trace_now next to the exe traces the next frame (Tales' frame_trace.hpp: tales_frame_trace.txt;
  // checked each second, the file removed).
  if (static uint32_t presents = 0; ++presents % 60 == 0) {
    if (DeleteFileW((talesofarise::frame_trace::internal::ExecutableFolder() / L"renodx_trace_now").c_str()) != 0) {
      talesofarise::frame_trace::Request();
    }
  }
  talesofarise::frame_trace::OnPresent();
  if (ray_tracing_installed) expeditionsrome::dlss::ray_tracing::Update(ray_tracing_settings, ray_traced_bounce_strength);
  if (talesofarise::frame_generation::Presenting()) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_immediate_command_list()->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&context);
    uint64_t frame;
    {
      const std::shared_lock lock(state.mutex);
      frame = state.frame;
    }
    talesofarise::frame_generation::Inputs inputs;
    {
      const std::shared_lock lock(frame_generation_frame.mutex);
      if (frame_generation_frame.frame == frame) inputs = frame_generation_frame.inputs;
    }
    talesofarise::frame_generation::Submit(context, inputs, frame);
  }
  const auto mode = requested_mode.load();
  bool changed;
  bool missed;
  uint32_t width;
  uint32_t height;
  uint32_t widest_display;
  {
    const std::unique_lock lock(state.mutex);
    if (state.device != swapchain->get_device()) return;
    width = state.width;
    height = state.height;
    widest_display = state.widest_display;
    changed = state.last_mode != mode;
    state.last_mode = mode;
    missed = state.claimed_frame != state.frame;
    ++state.frame;
    const uint64_t copied = expeditionsrome::dlss::constants::CopiedBytes();
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
      SetStatus(std::string("Stock TAA: waiting for ") + AA_MODES[static_cast<size_t>(mode)].label
                + "; keep Fast or Ultra TAA and Temporal Upscaling ON below 100%");
    }
  }

  // The mode's render size: whole units of the output's aspect ratio, sides in buffer multiples (16:9 at 4K: 64x36,
  // 60 steps), nearest to its scale. UE renders ceil(size * percentage / 100): the percentage written is a quarter
  // pixel below the exact size. 0: the game's own.
  float render_percentage = 0.f;
  if (mode != AAMode::OFF && width != 0 && height != 0) {
    const float scale = (mode == AAMode::DLSS_CUSTOM ? live.custom : AA_MODES[static_cast<size_t>(mode)].live_percentage) / 100.f;
    const uint32_t multiple = buffer_multiple.load();
    const uint32_t divisor = std::gcd(width, height);
    const uint32_t units = std::lcm(multiple / std::gcd(multiple, width / divisor), multiple / std::gcd(multiple, height / divisor));
    const uint32_t unit_width = width / divisor * units;
    const uint32_t unit_height = height / divisor * units;
    std::array<uint32_t, 2> render_size = {width, height};
    if (scale < 1.f && unit_height * 20 <= height) {
      const auto count = std::clamp(static_cast<uint32_t>(std::lround(scale * static_cast<float>(height / unit_height))), 1u, height / unit_height);
      render_size = {count * unit_width, count * unit_height};
    } else if (scale < 1.f) {
      // No unit of 5% or finer (unusual aspect ratios): each side on its own.
      const auto side = [&](uint32_t size) {
        return std::clamp(static_cast<uint32_t>(std::lround(scale * static_cast<float>(size) / static_cast<float>(multiple))) * multiple, multiple, size);
      };
      render_size = {side(width), side(height)};
    }
    render_percentage = (render_size == std::array<uint32_t, 2>{width, height}
                   ? 100.f
                   : 100.f * std::min(static_cast<float>(render_size[0]) / static_cast<float>(width), static_cast<float>(render_size[1]) / static_cast<float>(height))
                         - 25.f / static_cast<float>(std::max(width, height)));
  }

  // Live render scale: find r.ScreenPercentage once, then keep it at the mode's value.
  auto& finder = live.finder;
  using Phase = expeditionsrome::engine_scale::Finder::Phase;
  if (live.enabled && !live.problem.empty() && live.retries < MAX_LIVE_RETRIES
      && std::chrono::steady_clock::now() >= live.retry_at) {
    ++live.retries;
    live.problem.clear();
    live.writes = 0;
    live.window = 0;
    // A moved or unreadable variable: search again.
    if (finder.phase() == Phase::FAILED || !finder.Get()) finder = {};
    renodx::utils::log::i("[Rome AA live scale] Retrying (", live.retries, "/", MAX_LIVE_RETRIES, ").");
  }
  if (live.enabled && live.problem.empty()) {
    if (finder.phase() == Phase::IDLE) {
      finder.Start();
      renodx::utils::log::i("[Rome AA live scale] Searching the game's exe for r.ScreenPercentage.");
    }
    if (finder.Searching()) {
      // About 1 ms per frame, for well under a second.
      if (!finder.Step(std::chrono::milliseconds(1))) {
        if (finder.phase() == Phase::FOUND) {
          renodx::utils::log::i("[Rome AA live scale] Found r.ScreenPercentage (", finder.Get().value_or(0), "%): ", finder.details());
        } else {
          renodx::utils::log::w("[Rome AA live scale] ", finder.status(), ": ", finder.details());
        }
      }
    } else if (finder.phase() == Phase::FOUND) {
      const auto current = finder.Get();
      if (!current) {
        live.problem = "r.ScreenPercentage is no longer readable";
      } else if (render_percentage <= 0) {
        if (live.controlling) finder.Set(live.game_value);
        live.controlling = false;
      } else {
        if (!live.controlling) {
          live.game_value = *current;
          live.controlling = true;
          live.target = 0;
        }
        if (*current != render_percentage) {
          // The game's menu sets it too: that becomes the value to restore, then ours is
          // written back, unless the game never lets it stay.
          if (*current != live.target) live.game_value = *current;
          if (live.target == render_percentage && ++live.writes > 20) {
            live.problem = "the game keeps resetting the render scale";
          } else if (!finder.Set(render_percentage)) {
            live.problem = finder.status();
          }
          live.target = render_percentage;
          live.seen_at_write = seen_frames.load();
          live.width_at_write = static_cast<uint32_t>(seen_input.load());
          live.verified = false;
          live.note.clear();
        } else if (live.target == render_percentage && !live.verified && seen_frames.load() - live.seen_at_write >= 60) {
          // 60 temporal passes later (loading screens have none) the input must have changed.
          live.verified = true;
          live.retries = 0;
          const auto input_width = static_cast<uint32_t>(seen_input.load());
          // UE renders ceil(width * percentage / 100).
          const auto expected = static_cast<uint32_t>(std::ceil(static_cast<float>(width) * (render_percentage / 100.f)));
          if (width != 0 && input_width != 0 && std::abs(static_cast<int64_t>(input_width) - static_cast<int64_t>(expected)) > 1) {
            if (input_width == live.width_at_write) {
              live.problem = std::format("no effect: the input stayed {} pixels wide instead of {}", input_width, expected);
            } else {
              live.note = std::format("the game renders {} pixels wide, not {}", input_width, expected);
              renodx::utils::log::w("[Rome AA live scale] At ", render_percentage, "% ", live.note, ".");
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
        renodx::utils::log::w("[Rome AA live scale] Paused: ", live.problem);
      }
    }
  }
  if ((!live.enabled || !live.problem.empty()) && live.controlling) {
    if (finder.phase() == Phase::FOUND) finder.Set(live.game_value);
    live.controlling = false;
  }

  // Halton jitter: the render size is the live scale's (DLAA: the output), else UE's own pattern.
  std::array<uint32_t, 2> jitter_render_size = {};
  if (mode == AAMode::DLAA || (mode != AAMode::OFF && live.controlling && render_percentage > 0.f)) {
    const auto rendered = [&](uint32_t size) {
      return static_cast<uint32_t>(std::ceil(static_cast<float>(size) * (render_percentage / 100.f)));
    };
    jitter_render_size = (live.controlling && render_percentage > 0.f ? std::array<uint32_t, 2>{rendered(width), rendered(height)}
                                                                      : std::array<uint32_t, 2>{width, height});
  }
  const bool halton = halton_jitter.sample != nullptr && jitter_render_size[0] != 0 && jitter_render_size[1] != 0;
  if (halton) {
    const auto positions = static_cast<uint32_t>(std::ceil(8. * width * height / (static_cast<double>(jitter_render_size[0]) * jitter_render_size[1])));
    if (positions != halton_jitter.positions) {
      halton_jitter.positions = positions;
      halton_jitter.index = 0;
    }
    halton_jitter.index = (halton_jitter.index + 1) % positions;
    const auto radical_inverse = [](uint32_t index, uint32_t base) {
      float result = 0.f;
      float fraction = 1.f;
      for (; index != 0; index /= base) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
      }
      return result;
    };
    // Read by the render thread at any time: each value on its own (both stay inside the pixel).
    std::atomic_ref(halton_jitter.sample[0]).store(radical_inverse(halton_jitter.index + 1, 2) - .5f, std::memory_order_relaxed);
    std::atomic_ref(halton_jitter.sample[1]).store(radical_inverse(halton_jitter.index + 1, 3) - .5f, std::memory_order_relaxed);
  }
  if (halton_jitter.patch.address != nullptr && halton_jitter.patch.applied != halton) {
    if (expeditionsrome::code_patch::ApplyCodePatch(&halton_jitter.patch, halton)) {
      renodx::utils::log::i("[Rome DLAA jitter] ", (halton ? std::format("NVIDIA's Halton (2, 3), {} positions", halton_jitter.positions)
                                                           : std::string("the game's")), ".");
    } else {
      renodx::utils::log::w("[Rome DLAA jitter] Halton patch failed: ", halton_jitter.patch.details, ".");
    }
  }

  // Graphics settings: the console is searched once (after r.ScreenPercentage's search), then each setting is kept at
  // its choice; a value the game sets again (its menu) becomes the one to restore.
  if (!graphics.searched && !finder.Searching()) {
    graphics.searched = true;
    std::string details;
    graphics.available = graphics.console.Start(&details);
    graphics.status = (graphics.available ? "" : "Graphics settings unavailable: " + details + " (details in ReShade.log).");
    if (graphics.available) {
      renodx::utils::log::i("[Rome graphics] ", details, ".");
    } else {
      renodx::utils::log::w("[Rome graphics] ", details, ".");
    }
    for (size_t index = 0; graphics.available && index < GRAPHICS_SETTINGS.size(); ++index) {
      auto& entry = graphics.entries[index];
      entry.cvar = graphics.console.Find(GRAPHICS_SETTINGS[index].name, &entry.details);
      renodx::utils::log::i("[Rome graphics] ", entry.key, ": ", entry.details, ".");
    }
    for (size_t index = 0; graphics.available && index < TEXTURE_LOD_BIAS_CVARS.size(); ++index) {
      std::string details;
      texture_lod_bias.cvars[index] = graphics.console.Find(TEXTURE_LOD_BIAS_CVARS[index], &details);
      renodx::utils::log::i("[Rome DLSS texture LOD bias] ", std::filesystem::path(TEXTURE_LOD_BIAS_CVARS[index]).string(), ": ",
                            details, ".");
    }
  }
  bool wrote = false;
  const bool upscaling = live.controlling && render_percentage > 0.f && render_percentage < 100.f;
  const float render_scale = (upscaling ? render_percentage / 100.f : 1.f);
  for (size_t index = 0; graphics.available && index < GRAPHICS_SETTINGS.size(); ++index) {
    auto& entry = graphics.entries[index];
    if (entry.cvar.values[0] == nullptr) continue;
    const auto& setting = GRAPHICS_SETTINGS[index];
    const float preset_value = (entry.choice == 0 ? KEEP : entry.choices[entry.choice - 1]);
    const OutputScaling scaling = ((effects_at_output && upscaling) ? OUTPUT_SCALING[index] : OutputScaling::NONE);
    const float current = entry.cvar.Get();
    if (std::isnan(preset_value) && scaling == OutputScaling::NONE) {
      if (entry.controlling) {
        entry.cvar.Set(entry.game_value);
        wrote = true;
      }
      entry.controlling = false;
      continue;
    }
    if (!entry.controlling || current != entry.written) entry.game_value = current;
    entry.controlling = true;
    float desired = (std::isnan(preset_value) ? entry.game_value : preset_value);
    if (setting.better == Better::HIGHER) desired = std::max(entry.game_value, desired);
    if (setting.better == Better::LOWER) desired = std::min(entry.game_value, desired);
    switch (scaling) {
      case OutputScaling::CELL_OR_DIVISOR:
        desired = std::max(1.f, std::round(desired * render_scale));
        break;
      case OutputScaling::PER_PIXEL:
        desired /= render_scale;
        break;
      case OutputScaling::FULL_RESOLUTION:
        desired = (setting.better == Better::HIGHER ? 1.f : 0.f);
        break;
      case OutputScaling::NONE:
        break;
    }
    if (index == VOLUMETRIC_FOG_CELL && scaling != OutputScaling::NONE) {
      desired = std::max(desired, std::ceil(static_cast<float>(std::max(width, widest_display)) / 2048.f));
    }
    if (index == SUN_SHADOW_RESOLUTION) {
      const auto& cascades = graphics.entries[SUN_SHADOW_CASCADES];
      float cascade_count = 10.f;  // not found: Unreal's limit
      if (cascades.cvar.values[0] != nullptr) cascade_count = (cascades.controlling ? cascades.written : cascades.cvar.Get());
      const float fitting = std::floor(8192.f / std::ceil(std::sqrt(std::max(cascade_count, 1.f))));
      desired = std::min(desired, std::max(entry.game_value, fitting));
    }
    if (entry.cvar.is_int) desired = std::round(desired);
    if (current != desired) {
      entry.cvar.Set(desired);
      wrote = true;
    }
    entry.written = desired;
  }
  if (wrote) graphics.console.Refresh();

  const bool below_native = (mode == AAMode::DLSS_CUSTOM ? (live.custom < 100.f)
                                                          : (mode != AAMode::OFF && AA_MODES[static_cast<size_t>(mode)].maximum_scale < 1.f));
  const bool found = std::ranges::all_of(texture_lod_bias.cvars, [](const auto& cvar) { return cvar.values[0] != nullptr; });
  if (found && below_native && texture_lod_bias.choice != 0) {
    if (!texture_lod_bias.controlling) {
      for (size_t index = 0; index < TEXTURE_LOD_BIAS_CVARS.size(); ++index) {
        texture_lod_bias.game_values[index] = texture_lod_bias.cvars[index].Get();
      }
      texture_lod_bias.controlling = true;
    }
    texture_lod_bias.cvars[0].Set(-static_cast<float>(texture_lod_bias.choice - 1));
    texture_lod_bias.cvars[1].Set(LOWEST_MIP_BIAS);
  } else {
    RestoreTextureLodBias();
  }
}

void OnOverlay(reshade::api::effect_runtime*) {
  // Timings are only shown (and measured) in Advanced.
  if (advanced) {
    const std::shared_lock lock(state.mutex);
    overlay_frame.store(state.frame + 1);
  }
  using Phase = expeditionsrome::engine_scale::Finder::Phase;
  if (int settings_mode = (advanced ? 1 : 0); ImGui::Combo("Settings mode", &settings_mode, "Simple\0Advanced\0")) {
    advanced = settings_mode == 1;
    reshade::set_config_value(nullptr, "Rome AA", "SettingsMode", settings_mode);
  }
  // Section headings (ATN, as Ryse's): DLSS lavender (its settings too), Graphics pink, the rest green.
  const auto heading = [](const char* label, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
  };
  constexpr ImU32 DLSS_COLOR = IM_COL32(0xB3, 0x9D, 0xDB, 0xFF);
  constexpr ImU32 GRAPHICS_COLOR = IM_COL32(0xFF, 0x7F, 0xAF, 0xFF);
  constexpr ImU32 OTHER_COLOR = IM_COL32(0x7E, 0xE0, 0x8A, 0xFF);
  heading("DLSS", DLSS_COLOR);
  ImGui::PushStyleColor(ImGuiCol_Text, DLSS_COLOR);
  auto mode = requested_mode.load();
  if (ImGui::BeginCombo("AA mode", AA_MODES[static_cast<size_t>(mode)].label)) {
    for (const AAMode option : MENU_ORDER) {
      if (ImGui::Selectable(AA_MODES[static_cast<size_t>(option)].label, option == mode)) {
        mode = option;
        requested_mode.store(mode);
        reshade::set_config_value(nullptr, "Rome AA", "Mode", static_cast<int>(mode));
        if (mode != AAMode::OFF) expeditionsrome::dlss::temporal_capture::Reset();
      }
    }
    ImGui::EndCombo();
  }
  if (live.enabled && mode == AAMode::DLSS_CUSTOM
      && ImGui::SliderFloat("Render scale", &live.custom, 1.f, 100.f, "%.0f%%")) {
    live.custom = std::clamp(std::round(live.custom), 1.f, 100.f);
    reshade::set_config_value(nullptr, "Rome AA", "CustomScale", live.custom);
  }
  const auto selected_preset = std::ranges::find(PRESETS, preset.load(), &PresetSettings::value);
  if (ImGui::BeginCombo("DLSS preset", selected_preset != PRESETS.end() ? selected_preset->label : PRESETS.back().label)) {
    for (const auto& option : PRESETS) {
      if (ImGui::Selectable(option.label, option.value == preset.load())) {
        preset.store(option.value);
        reshade::set_config_value(nullptr, "Rome AA", "Preset", option.value);
      }
    }
    ImGui::EndCombo();
  }
  const bool lod_bias_found = texture_lod_bias.cvars[0].values[0] != nullptr && texture_lod_bias.cvars[1].values[0] != nullptr;
  ImGui::BeginDisabled(graphics.searched && !lod_bias_found);
  if (ImGui::Combo("Texture LOD bias", &texture_lod_bias.choice,
                   "Off (the game's own)\0"
                   "Matched (as sharp as at your output resolution)\0"
                   "Sharper (one step sharper; NVIDIA's recommendation)\0"
                   "Sharpest (two steps sharper; can shimmer)\0")) {
    reshade::set_config_value(nullptr, "Rome AA", "TextureLodBias", texture_lod_bias.choice);
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    // The bias each choice gives at the current mode's scale (the game's: its own offset and minimum).
    std::string example;
    const float percentage = (mode == AAMode::DLSS_CUSTOM ? live.custom : AA_MODES[static_cast<size_t>(mode)].live_percentage);
    if (lod_bias_found && percentage > 0.f && percentage < 100.f) {
      const float matched = std::log2(percentage / 100.f);
      const auto& game = (texture_lod_bias.controlling
                              ? texture_lod_bias.game_values
                              : std::array<float, 2>{texture_lod_bias.cvars[0].Get(), texture_lod_bias.cvars[1].Get()});
      example = std::format("\n\nAt {:g}% (bias, lower = sharper): Off {:.1f}, Matched {:.1f}, Sharper {:.1f}, Sharpest {:.1f}.",
                            percentage, std::max(matched + game[0], game[1]), matched, matched - 1.f, matched - 2.f);
    }
    ImGui::SetTooltip("How sharp textures are while DLSS renders below 100%%. The game renders fewer pixels and picks\n"
                      "blurrier texture levels to match them (at most one step blurrier than your output resolution).\n"
                      "Matched: textures as sharp as at your output resolution.\n"
                      "Sharper / Sharpest: one / two steps sharper than that (more detail, can shimmer).\n"
                      "DLAA and Off are not affected.%s%s",
                      example.c_str(), (graphics.searched && !lod_bias_found) ? "\n\nUnavailable: r.ViewTextureMipBias.* not found." : "");
  }
  if (ImGui::Checkbox("Effects at output resolution (Recommended)", &effects_at_output)) {
    reshade::set_config_value(nullptr, "Rome AA", "EffectsAtOutputResolution", effects_at_output);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Recommended (default): looks better, can cost some performance.\n"
                      "While DLSS renders below 100%%, volumetric fog, light shafts, skin and the shadows of characters,\n"
                      "objects and lamps keep the detail they have at 100%%.");
  }
  talesofarise::frame_generation::DrawSettings("Rome AA", &frame_generation);
  if (bool replace = replace_taa.load(); ImGui::Checkbox("Replace the game's TAA pass (faster)", &replace)) {
    replace_taa.store(replace);
    reshade::set_config_value(nullptr, "Rome AA", "ReplaceTAA", replace);
  }

  std::string status;
  std::string constants;
  uint64_t copied_per_frame;
  uint64_t completed;
  std::array<uint32_t, 4> extents;
  uint32_t temporal_shader_hash;
  expeditionsrome::dlss::PerformanceSummary performance;
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
    live_status = "Live render scale off: set Screen Percentage in the game's menu.";
  } else if (!live.problem.empty()) {
    live_status = (live.retries < MAX_LIVE_RETRIES
                       ? "Live render scale paused: " + live.problem + ". The game's own value is back for now; retrying in a few seconds."
                       : "Live render scale stopped: " + live.problem + ". The game's own value was put back; untick and tick live render scale (Advanced) to retry.");
  } else if (live.finder.phase() == Phase::FAILED) {
    live_status = "Live render scale unavailable: " + live.finder.status() + " (details in ReShade.log).";
  } else if (live.finder.phase() != Phase::FOUND) {
    live_status = live.finder.status() + "...";
  } else {
    live_status = std::format("Render scale {:g}%{}{}", live.finder.Get().value_or(0),
                              live.controlling ? std::format(" (the game's own: {:g}%)", live.game_value) : std::string(" (the game's own)"),
                              live.note.empty() ? std::string() : "; " + live.note);
  }
  ImGui::Separator();
  ImGui::TextWrapped("%s", status.c_str());
  ImGui::TextWrapped("%s", live_status.c_str());
  if (completed != 0 && status.find("active") != std::string::npos) {
    ImGui::Text("Input %ux%u -> output %ux%u", extents[0], extents[1], extents[2], extents[3]);
  }
  const uint32_t phases = diagnostics.jitter_phases.load();
  const auto recommended = static_cast<uint32_t>(std::ceil(
      8.0 * std::pow(static_cast<double>(extents[2]) / std::max(extents[0], 1u), 2)));
  // NVIDIA wants 8 * (output / input)^2 positions: UE's pattern has them down to 25% (signed position) or, with the
  // jitter fix, 18% (its cap of 255).
  const bool jitter_fixed = std::ranges::all_of(jitter_position_patches, &expeditionsrome::code_patch::CodePatch::applied);
  const float lowest_scale = (jitter_fixed ? 18.f : 25.f);
  if (!halton_jitter.patch.applied && (mode == AAMode::DLSS_CUSTOM && live.enabled && live.custom < lowest_scale)
      || (completed != 0 && extents[2] != 0
          && static_cast<float>(extents[0]) / static_cast<float>(extents[2]) < lowest_scale / 100.f - .005f)) {
    ImGui::TextWrapped("Below %.0f%%, the game's (UE's) jitter pattern gets too short for DLSS: the image can pulse blurry/sharp.",
                       lowest_scale);
  }
  if (mode != AAMode::OFF && status.starts_with("Stock TAA")) {
    ImGui::TextWrapped("In the game's graphics settings, keep Anti-Aliasing on Fast or Ultra and Temporal Upscaling on.");
  }
  const auto& version = dlss_library.version;
  const bool old_library = version[0] != 0
                           && std::array<uint32_t, 2>{version[0], version[1]} < expeditionsrome::dlss::MINIMUM_RUNTIME_VERSION;
  if (dlss_library.folder.empty()) {
    ImGui::TextWrapped("nvngx_dlss.dll is missing: download the latest nvngx_dlss.dll and put it next to this addon (the game's Binaries\\Win64 folder), then restart the game.");
  } else if (old_library) {
    ImGui::TextWrapped("nvngx_dlss.dll %u.%u.%u is older than %u.%u: presets M and L are not available, NVIDIA uses its default. Download the latest nvngx_dlss.dll.",
                       version[0], version[1], version[2], expeditionsrome::dlss::MINIMUM_RUNTIME_VERSION[0],
                       expeditionsrome::dlss::MINIMUM_RUNTIME_VERSION[1]);
  }

  ImGui::PopStyleColor();
  heading("Graphics", GRAPHICS_COLOR);
  ImGui::TextUnformatted("Applied live; a preset never lowers the game's own settings.");
  // Not a cvar (contact_shadows.hpp): only Game's own (off) and Max set it.
  const auto set_contact_shadows = [](float contact_length) {
    expeditionsrome::dlss::contact_shadows::length.store(contact_length);
    reshade::set_config_value(nullptr, "Rome AA", "ContactShadows", contact_length);
  };
  for (size_t preset_index = 0; preset_index < GRAPHICS_PRESETS.size(); ++preset_index) {
    if (preset_index != 0) ImGui::SameLine();
    if (ImGui::Button(GRAPHICS_PRESETS[preset_index])) {
      // Every setting but shaders on load gets the preset's value (KEEP: the game's), so none keeps an older choice.
      for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
        const auto& setting = GRAPHICS_SETTINGS[index];
        if (std::wstring_view(setting.name) == NO_PRESET_SETTING) continue;
        auto& entry = graphics.entries[index];
        float value = KEEP;
        if (preset_index == MAX_PRESET && !entry.choices.empty()) {
          switch (setting.better) {
            case Better::HIGHER:
              value = entry.choices.back();
              break;
            case Better::LOWER:
              value = entry.choices.front();
              break;
            case Better::EXACT:
              // Insane's value, else the optional ones' extra; the others (cheaper volumetric fog) the game's.
              value = setting.values[INSANE_PRESET - 1];
              if (std::isnan(value) && std::string_view(setting.group) == OPTIONAL_GROUP) {
                value = setting.values[INSANE_PRESET];
              }
              break;
          }
        } else if (preset_index != 0) {
          value = setting.values[preset_index - 1];
        }
        const auto found = std::ranges::find(entry.choices, value);
        entry.choice = (found == entry.choices.end() ? 0 : static_cast<int>(found - entry.choices.begin()) + 1);
        SaveChoice(entry);
      }
      if (preset_index == 0) {
        set_contact_shadows(0.f);
      } else if (preset_index == MAX_PRESET) {
        set_contact_shadows(CONTACT_SHADOWS_MAX);
      }
    }
  }
  if (!graphics.status.empty()) ImGui::TextWrapped("%s", graphics.status.c_str());
  ImGui::TextWrapped("Some settings apply after loading a new area, anisotropic filtering to textures loaded afterwards. Insane and Max are very heavy and use a lot of VRAM. Max puts every setting at its highest, the optional ones too. Every preset keeps the game's volumetric fog: turning it off or lowering it is the biggest saving. Shaders on load is in no preset.");
  if (float percent = expeditionsrome::dlss::contact_shadows::length.load() * 100.f;
      ImGui::SliderFloat("Contact shadows (Experimental)", &percent, 0.f, 10.f, (percent > 0.f ? "%.1f%% of the screen" : "Off"),
                         ImGuiSliderFlags_AlwaysClamp)) {
    set_contact_shadows(percent / 100.f);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Unreal's own short sun shadows where things meet the ground and in small gaps (screen-space).\n"
                      "Longer: shadows reach farther from contact points; can show halos at the screen's edges.\n"
                      "Off while ray-traced sun shadows are on. Max preset: %.0f%%.", CONTACT_SHADOWS_MAX * 100.f);
  }
  if (ImGui::TreeNode("Ray tracing (Experimental)")) {
    auto& ray_tracing = ray_tracing_settings;
    const auto checkbox = [](const char* label, const char* key, bool* value, const char* tooltip) {
      if (ImGui::Checkbox(label, value)) reshade::set_config_value(nullptr, "Rome AA", key, *value);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    };
    const auto slider = [](const char* label, const char* key, float* value, float min, float max, const char* format, const char* tooltip) {
      if (ImGui::SliderFloat(label, value, min, max, format, ImGuiSliderFlags_AlwaysClamp)) {
        reshade::set_config_value(nullptr, "Rome AA", key, *value);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    };
    const auto count = [](const char* label, const char* key, uint32_t* value, int max, const char* tooltip) {
      if (int shown = static_cast<int>(*value); ImGui::SliderInt(label, &shown, 1, max, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        *value = static_cast<uint32_t>(shown);
        reshade::set_config_value(nullptr, "Rome AA", key, *value);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    };
    ImGui::TextWrapped("Needs an RTX GPU and renodx-tw-dlaa64.exe next to this addon. Restart the game after turning on the first effect.");
    checkbox("Ray-traced sun shadows", "RayTracedSunShadows", &ray_tracing.sun_shadows,
             "Sun shadows from rays, with the game's: also where its shadow maps miss things or end.");
    ImGui::BeginDisabled(!ray_tracing.sun_shadows);
    slider("Shadow softness", "RayTracedShadowSoftness", &ray_tracing.sun_softness, 0.f, 3.f, "%.2f",
           "The sun's size in degrees: shadows softer away from what casts them. 0: sharp.");
    ImGui::EndDisabled();
    checkbox("Ray-traced ambient occlusion", "RayTracedAmbientOcclusion", &ray_tracing.ambient_occlusion,
             "Shade in corners and under things from rays, with the game's.");
    ImGui::BeginDisabled(!ray_tracing.ambient_occlusion);
    slider("Ambient occlusion range", "RayTracedAmbientRange", &ray_tracing.ambient_range, .25f, 5.f, "%.2f m",
           "How far its rays look for what shades a point.");
    checkbox("Ambient occlusion at a quarter rate", "RayTracedAOQuarter", &ray_tracing.ambient_quarter,
             "One pixel of each 2 x 2 traced a frame, in turn: a quarter of the rays (DLSS smooths it). Faster.");
    ImGui::EndDisabled();
    checkbox("Ray-traced light bounce", "RayTracedLightBounce", &ray_tracing.light_bounce,
             "Sunlight bouncing off what it hits onto what is near: lit ground brightens walls and shade.");
    ImGui::BeginDisabled(!ray_tracing.light_bounce);
    slider("Light bounce strength", "RayTracedLightBounceStrength", &ray_traced_bounce_strength, 0.f, 2.f, "%.2f",
           "How much bounced light is added.");
    count("Light bounce rays", "RayTracedLightBounceRays", &ray_tracing.light_bounce_rays, 16, "Rays per texel: less noise, heavier.");
    slider("Light bounce range", "RayTracedLightBounceRange", &ray_tracing.light_bounce_range, 1.f, 50.f, "%.0f m",
           "How far its rays look for light.");
    ImGui::EndDisabled();
    checkbox("Ray-traced reflections", "RayTracedReflections", &ray_tracing.reflections,
             "Reflections from rays in place of the screen-space ones: also what is off screen.\n"
             "Needs the game's screen-space reflections on.");
    ImGui::BeginDisabled(!ray_tracing.reflections);
    count("Reflection samples", "RayTracedReflectionSamples", &ray_tracing.reflection_samples, 8,
          "Rays per pixel on rough surfaces: less noise, heavier.");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!ray_tracing.light_bounce && !ray_tracing.reflections);
    checkbox("Path tracing", "PathTracing", &ray_tracing.path_tracing,
             "The light bounce's and the reflections' rays go on bouncing: lit by what the screen never saw. Heavy.");
    ImGui::BeginDisabled(!ray_tracing.path_tracing);
    count("Path tracing bounces", "PathTracingBounces", &ray_tracing.path_bounces, 4, "Bounces past the first. More: brighter shade, heavier.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (ray_tracing_installed) ImGui::TextWrapped("%s", twattila::rt_scene::Status().c_str());
    ImGui::TreePop();
  }
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
                                : std::string(": not found in this version of the game"))
                            .c_str());
    }
  }
  if (!advanced) return;

  heading("Advanced", OTHER_COLOR);
  if (ImGui::Checkbox("Set the game's render scale from the AA mode (live)", &live.enabled)) {
    reshade::set_config_value(nullptr, "Rome AA", "LiveScale", live.enabled);
    if (live.enabled) {
      live.problem.clear();
      live.retries = 0;
      if (live.finder.phase() == Phase::FAILED) live.finder = {};
    }
  }
  ImGui::TextWrapped("Live: DLAA 100%%, Ultra Quality 77%%, Quality 66.66%%, Balanced 58%%, Performance 50%%, Ultra Performance 33.33%%, Custom its slider; AA Off puts the game's own value back. Off: set Screen Percentage in the game's menu (it stops at 50%%).");
  ImGui::TextWrapped("Custom follows the game's actual 1-100%% input scale; 100%% uses DLAA. Below 33%% is beyond NVIDIA's modes. Changing scale resets NVIDIA history.");
  ImGui::TextWrapped("Presets: E is the last CNN model, K the first transformer, M and L the second; L is the sharpest and heaviest. NVIDIA's defaults: K for DLAA/Quality/Balanced, M for Performance, L for Ultra Performance. A preset forced in the NVIDIA app or NVIDIA Profile Inspector overrides this one.");
  ImGui::TextWrapped("Ultra Quality (77%%) is not an NVIDIA mode: it runs NVIDIA's Quality model at 77%%.");
  ImGui::TextWrapped("Both low/high-scale Ultra and Fast TAAU compute variants are recognized. Below 100%%, Temporal Upscaling OFF still uses stock AA; that pixel-shader path is not supported yet.");
  ImGui::TextWrapped("Replacing skips the game's TAA/TAAU dispatch and NVIDIA writes straight into its output, once the CPU copy of the constants is trusted. Off, or while the GPU readback is still in use, stock TAA runs first and DLSS is merged with its alpha.");
  ImGui::TextWrapped("Constants come from a CPU copy of the game's uploads once it matched %u exact GPU readbacks in a row; it stays checked against GPU copies without waiting, and any mismatch returns to the waiting readback.", expeditionsrome::dlss::ConstantReadback::TRUST_AFTER);
  ImGui::Separator();
  if (!dlss_library.folder.empty()) {
    ImGui::TextWrapped("nvngx_dlss.dll %u.%u.%u %s (%s). An override in the NVIDIA app or Profile Inspector can load another version; the DLSS indicator shows the one in use.",
                       version[0], version[1], version[2], dlss_library.place,
                       reinterpret_cast<const char*>(std::filesystem::path(dlss_library.folder).u8string().c_str()));
  }
  ImGui::Text("Successful AA frames: %llu", static_cast<unsigned long long>(completed));
  if (completed != 0) {
    ImGui::Text("Temporal shader: 0x%08X", temporal_shader_hash);
    if (texture_lod_bias.controlling) {
      ImGui::Text("Texture LOD bias: log2(render scale) %+g (the game's: %+g, at least %g)", texture_lod_bias.cvars[0].Get(),
                  texture_lod_bias.game_values[0], texture_lod_bias.game_values[1]);
    }
    ImGui::Text("Jitter pattern: %s (NVIDIA recommends %u or more); %s",
                phases != 0 ? std::to_string(phases).c_str() : "measuring", recommended,
                (halton_jitter.patch.applied ? "NVIDIA's Halton (2, 3)"
                                             : (jitter_fixed ? "the game's, position fix on" : "the game's (correct down to 25%)")));
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
      ImGui::Text("GPU stock TAA: %.3f ms", performance.gpu_ms[0]);
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
  ImGui::Separator();
  ImGui::BeginDisabled(mode != AAMode::OFF);
  if (ImGui::Button("Capture upscaling inputs (15 seconds)")) {
    expeditionsrome::dlss::temporal_capture::QueueCapture();
  }
  ImGui::EndDisabled();
  ImGui::TextWrapped("Diagnostic capture only: choose AA Off, keep TAA on, then capture at fixed Screen Percentage / Temporal Upscaling settings. Capturing does not enable DLSS.");
  ImGui::TextWrapped("%s", expeditionsrome::dlss::temporal_capture::Snapshot().c_str());
}

// After the tonemapper's draw: frame generation's hudless colour from its target (at the output size only).
constexpr auto OnTonemapper = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!talesofarise::frame_generation::Presenting()) return {};
  return {.post_callback = [](Context& command, const void*) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&context);
    if (context == nullptr) return;
    ComPtr<ID3D11RenderTargetView> target;
    context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
    uint64_t frame;
    {
      const std::shared_lock lock(state.mutex);
      frame = state.frame;
    }
    talesofarise::frame_generation::DrawHudless(context, target.Get(), frame);
  }};
};

}  // namespace

extern "C" __declspec(dllexport) const char* NAME = "RenoDX Expeditions: Rome DLAA / DLSS (experimental)";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "Native DLAA, DLSS Ultra Quality/Quality/Balanced/Performance/Ultra Performance and custom 1-100% scaling with live render scale and preset choice, and graphics settings beyond the game's menu. Independent of HDR, with stock TAA fallback.";

extern "C" __declspec(dllexport) bool AddonInit(HMODULE module, HMODULE) {
  if (!registered) {
    if (!reshade::register_addon(module)) return false;
    registered = true;
  }
  if (attached) return true;
  // Settings apply at launch, without opening the overlay.
  int saved_mode = 0;
  if (reshade::get_config_value(nullptr, "Rome AA", "Mode", saved_mode)
      && saved_mode >= 0 && static_cast<size_t>(saved_mode) < AA_MODES.size()) {
    requested_mode.store(static_cast<AAMode>(saved_mode));
  }
  reshade::get_config_value(nullptr, "Rome AA", "LiveScale", live.enabled);
  if (int choice = 2; reshade::get_config_value(nullptr, "Rome AA", "TextureLodBias", choice)) {
    texture_lod_bias.choice = std::clamp(choice, 0, 3);
  }
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
    // 1-2-5 numbers across gaps of more than 2x (draw distance 3 -> 10: 5).
    std::vector<float> steps;
    for (size_t choice = 0; choice + 1 < entry.choices.size(); ++choice) {
      const float low = entry.choices[choice];
      const float high = entry.choices[choice + 1];
      if (low == std::round(low) && high == std::round(high) && high - low <= 4.f && low <= 10.f) {
        for (float value = low + 1.f; value < high; ++value) steps.push_back(value);
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
  dlss_library = expeditionsrome::dlss::FindDlssLibrary();
  if (dlss_library.folder.empty()) {
    renodx::utils::log::w("[Rome DLAA] nvngx_dlss.dll not found next to the addon or the game.");
  } else {
    renodx::utils::log::i("[Rome DLAA] nvngx_dlss.dll ", dlss_library.version[0], ".", dlss_library.version[1], ".",
                          dlss_library.version[2], " ", dlss_library.place, ".");
  }
  if (uint32_t saved_preset = 0; reshade::get_config_value(nullptr, "Rome AA", "Preset", saved_preset)
                                 && std::ranges::find(PRESETS, saved_preset, &PresetSettings::value) != PRESETS.end()) {
    preset.store(saved_preset);
  }
  if (int settings_mode = 0; reshade::get_config_value(nullptr, "Rome AA", "SettingsMode", settings_mode)) advanced = settings_mode == 1;
  if (bool replace = true; reshade::get_config_value(nullptr, "Rome AA", "ReplaceTAA", replace)) replace_taa.store(replace);
  reshade::get_config_value(nullptr, "Rome AA", "EffectsAtOutputResolution", effects_at_output);
  if (float contact_length = 0.f; reshade::get_config_value(nullptr, "Rome AA", "ContactShadows", contact_length)) {
    expeditionsrome::dlss::contact_shadows::length.store(std::isfinite(contact_length) ? std::clamp(contact_length, 0.f, .1f) : 0.f);
  }
  if (reshade::get_config_value(nullptr, "Rome AA", "CustomScale", live.custom)) {
    live.custom = std::isfinite(live.custom) ? std::clamp(std::round(live.custom), 1.f, 100.f) : 50.f;
  }
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::present>(expeditionsrome::dlss::temporal_capture::OnPresent);
  reshade::register_overlay("Rome DLAA (experimental)", OnOverlay);
  for (const uint32_t shader_hash : TEMPORAL_SHADERS) {
    renodx::utils::command_action::Register(OnCommand, {
                                                           .shader_hash = shader_hash,
                                                           .command_types = renodx::utils::command_action::COMMAND_TYPE_DISPATCH,
                                                       });
  }
  for (const auto& target : expeditionsrome::dlss::temporal_capture::TARGETS) {
    renodx::utils::command_action::Register(expeditionsrome::dlss::temporal_capture::OnCommand, {
                                                                                                    .shader_hash = target.hash,
                                                                                                    .command_types = (target.is_compute ? renodx::utils::command_action::COMMAND_TYPE_DISPATCH
                                                                                                                                        : renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW),
                                                                                                });
  }
  {
    auto& ray_tracing = ray_tracing_settings;
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedSunShadows", ray_tracing.sun_shadows);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedShadowSoftness", ray_tracing.sun_softness);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedAmbientOcclusion", ray_tracing.ambient_occlusion);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedAmbientRange", ray_tracing.ambient_range);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedAOQuarter", ray_tracing.ambient_quarter);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedLightBounce", ray_tracing.light_bounce);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedLightBounceStrength", ray_traced_bounce_strength);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedLightBounceRays", ray_tracing.light_bounce_rays);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedLightBounceRange", ray_tracing.light_bounce_range);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedReflections", ray_tracing.reflections);
    reshade::get_config_value(nullptr, "Rome AA", "RayTracedReflectionSamples", ray_tracing.reflection_samples);
    reshade::get_config_value(nullptr, "Rome AA", "PathTracing", ray_tracing.path_tracing);
    reshade::get_config_value(nullptr, "Rome AA", "PathTracingBounces", ray_tracing.path_bounces);
    ray_tracing.sun_softness = std::isfinite(ray_tracing.sun_softness) ? std::clamp(ray_tracing.sun_softness, 0.f, 3.f) : .5f;
    ray_tracing.ambient_range = std::isfinite(ray_tracing.ambient_range) ? std::clamp(ray_tracing.ambient_range, .25f, 5.f) : 2.f;
    ray_tracing.reflection_samples = std::clamp(ray_tracing.reflection_samples, 1u, 8u);
    ray_tracing.path_bounces = std::clamp(ray_tracing.path_bounces, 1u, 4u);
    ray_tracing.light_bounce_rays = std::clamp(ray_tracing.light_bounce_rays, 1u, 16u);
    ray_tracing.light_bounce_range = std::isfinite(ray_tracing.light_bounce_range) ? std::clamp(ray_tracing.light_bounce_range, 1.f, 50.f) : 10.f;
    ray_traced_bounce_strength = std::isfinite(ray_traced_bounce_strength) ? std::clamp(ray_traced_bounce_strength, 0.f, 2.f) : 1.f;
    ray_tracing_installed = ray_tracing.sun_shadows || ray_tracing.ambient_occlusion || ray_tracing.light_bounce || ray_tracing.reflections;
    if (ray_tracing_installed) expeditionsrome::dlss::ray_tracing::Install();
  }
  renodx::utils::command_action::Register(expeditionsrome::dlss::contact_shadows::OnLightRectangle, {
                                                                                                        .shader_hash = expeditionsrome::dlss::contact_shadows::LIGHT_RECTANGLE_SHADER,
                                                                                                        .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                                                                    });
  for (size_t index = 0; index < JITTER_POSITION_LOADS.size(); ++index) {
    auto& patch = jitter_position_patches[index];
    patch = {};
    if (uint8_t* site = expeditionsrome::code_patch::FindPattern(JITTER_POSITION_LOADS[index], &patch.details); site != nullptr) {
      patch.address = site + SIGNED_LOAD_OPCODE;
      patch.original = {0xBE};
      patch.replacement = {0xB6};
    }
    if (expeditionsrome::code_patch::ApplyCodePatch(&patch, true)) {
      renodx::utils::log::i("[Rome DLAA jitter] Jitter position read unsigned ", patch.details, ".");
    } else {
      renodx::utils::log::w("[Rome DLAA jitter] Jitter position load ", index, " not fixed: ", patch.details, ".");
    }
  }
  halton_jitter = {};
  if (uint8_t* site = expeditionsrome::code_patch::FindPattern(JITTER_STORE, &halton_jitter.patch.details); site != nullptr) {
    DWORD region_size = 0;
    auto* region = static_cast<uint8_t*>(DetourAllocateRegionWithinJumpBounds(site, &region_size));
    DWORD protection = 0;
    if (region != nullptr && region_size >= 64 && VirtualProtect(region, 64, PAGE_EXECUTE_READWRITE, &protection)) {
      // The sample at +0, the block at +16: movss xmm7, [x]; movss xmm8, [y]; the game's store; jmp back.
      auto* sample = region;
      uint8_t* block = region + 16;
      std::vector<uint8_t> code = {0xF3, 0x0F, 0x10, 0x3D, 0, 0, 0, 0, 0xF3, 0x44, 0x0F, 0x10, 0x05, 0, 0, 0, 0};
      code.insert(code.end(), site, site + JITTER_STORE_SIZE);
      code.insert(code.end(), {0xE9, 0, 0, 0, 0});
      const auto x = static_cast<int32_t>(sample - (block + 8));
      const auto y = static_cast<int32_t>(sample + 4 - (block + 17));
      const auto back = static_cast<int32_t>(site + JITTER_STORE_SIZE - (block + code.size()));
      std::memcpy(code.data() + 4, &x, sizeof(x));
      std::memcpy(code.data() + 13, &y, sizeof(y));
      std::memcpy(code.data() + code.size() - 4, &back, sizeof(back));
      std::memset(sample, 0, 8);
      std::memcpy(block, code.data(), code.size());
      FlushInstructionCache(GetCurrentProcess(), block, code.size());
      std::vector<uint8_t> jump = {0xE9, 0, 0, 0, 0};
      const auto to_block = static_cast<int32_t>(block - (site + JUMP_SIZE));
      std::memcpy(jump.data() + 1, &to_block, sizeof(to_block));
      halton_jitter.sample = reinterpret_cast<float*>(sample);
      halton_jitter.patch.address = site;
      halton_jitter.patch.original = std::vector<uint8_t>(site, site + JUMP_SIZE);
      halton_jitter.patch.replacement = std::move(jump);
      renodx::utils::log::i("[Rome DLAA jitter] Found the jitter store ", halton_jitter.patch.details, ": Halton (2, 3) while DLAA / DLSS is on.");
    } else {
      renodx::utils::log::w("[Rome DLAA jitter] Halton jitter unavailable: no memory near the code.");
    }
  } else {
    renodx::utils::log::w("[Rome DLAA jitter] The jitter store was not found (", halton_jitter.patch.details, "): the game's jitter.");
  }
  // Frame generation takes over the game's swap chain, which it makes after its device.
  talesofarise::frame_generation::LoadSettings("Rome AA", &frame_generation);
  if (frame_generation) {
    std::wstring module_path(32768, L'\0');
    module_path.resize(GetModuleFileNameW(module, module_path.data(), static_cast<DWORD>(module_path.size())));
    talesofarise::frame_generation::Install(std::filesystem::path(module_path).parent_path(), "Expeditions");
    for (const uint32_t shader_hash : TONEMAPPER_SHADERS) {
      renodx::utils::command_action::Register(OnTonemapper, {
                                                                .shader_hash = shader_hash,
                                                                .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                            });
    }
  }
  expeditionsrome::dlss::constants::Use(DLL_PROCESS_ATTACH);
  renodx::utils::state::Use(DLL_PROCESS_ATTACH);
  renodx::utils::command_action::Use(DLL_PROCESS_ATTACH);
  attached = true;
  renodx::utils::log::i("[Rome DLAA] Native DLAA and DLSS Ultra Quality/Quality/Balanced/Performance/Ultra Performance/Custom 1-100% (all six Ultra/Fast compute hashes; exact/4/8-pixel allocations). Live render scale; preset choice; graphics settings. Runtime NVIDIA scale limits; exact CB readbacks and asynchronous phase timings; Release-compatible shared ABI.");
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE) {
  if (attached) {
    renodx::utils::command_action::Unregister(OnCommand);
    renodx::utils::command_action::Unregister(expeditionsrome::dlss::temporal_capture::OnCommand);
    renodx::utils::command_action::Unregister(expeditionsrome::dlss::contact_shadows::OnLightRectangle);
    if (ray_tracing_installed) expeditionsrome::dlss::ray_tracing::Uninstall();
    talesofarise::frame_trace::Stop();
    renodx::utils::command_action::Use(DLL_PROCESS_DETACH);
    renodx::utils::state::Use(DLL_PROCESS_DETACH);
    expeditionsrome::dlss::constants::Use(DLL_PROCESS_DETACH);
    reshade::unregister_overlay("Rome DLAA (experimental)", OnOverlay);
    reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
    reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
    reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
    reshade::unregister_event<reshade::addon_event::present>(OnPresent);
    reshade::unregister_event<reshade::addon_event::present>(expeditionsrome::dlss::temporal_capture::OnPresent);
    expeditionsrome::dlss::temporal_capture::Reset();
    for (auto& entry : graphics.entries) {
      if (entry.controlling) entry.cvar.Set(entry.game_value);
      entry.controlling = false;
    }
    graphics.console.Refresh();
    RestoreTextureLodBias();
    for (auto& patch : jitter_position_patches) expeditionsrome::code_patch::ApplyCodePatch(&patch, false);
    expeditionsrome::code_patch::ApplyCodePatch(&halton_jitter.patch, false);
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
