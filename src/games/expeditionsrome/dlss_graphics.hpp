/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

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
#include <fstream>
#include <functional>
#include <initializer_list>
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
#include "../../utils/settings.hpp"
#include "../talesofarise_dlss/code_patch.hpp"
#include "../talesofarise_dlss/constant_mirror.hpp"
#include "../talesofarise_dlss/constant_readback.hpp"
#include "./contact_shadows.hpp"
#include "../talesofarise_dlss/engine_cvars.hpp"
#include "../talesofarise_dlss/engine_scale.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/frame_trace.hpp"
#include "../talesofarise_dlss/graphics_table.hpp"
#include "../talesofarise_dlss/performance.hpp"
#include "./ray_tracing.hpp"
#include "./temporal_capture.hpp"
#include "../talesofarise_dlss/upscaling.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/frame_generation.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/pass_timing.hpp"

// The former DLSS add-on (renodx-expeditionsrome_dlss.addon64, one add-on with the HDR part since the UE4 merge round):
// DLAA / DLSS (with Ray Reconstruction), FSR or XeSS at Unreal's TAA, frame generation and the ray tracing, all through
// the Total War helper (upscaling.hpp: renodx-tw-dlaa64.exe next to the add-on; without it the game's TAA), the live
// render scale and Halton jitter, the live graphics settings (engine cvars). The HDR part's AttachAddon calls Install
// after its own attach (its mods::shader attached state and command_action, draw_dispatch the fast dispatch) and
// Uninstall before its detach; the rows are the page's (AppendPage).
namespace expeditionsrome::dlss_graphics {

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
  float live_percentage = 0;  // live render scale; Custom uses its slider
};

constexpr std::array<ModeSettings, 8> AA_MODES = {{
    {.label = "Off (game TAA)", .name = "Off", .minimum_scale = 0, .maximum_scale = 0},
    {.label = "Native AA (100%)", .name = "Native AA", .minimum_scale = 1, .maximum_scale = 1, .live_percentage = 100},
    // Live: UE renders ceil(size * percentage / 100); 66.66 and 33.33 give exactly 2560x1440 and 1280x720 at 4K.
    {.label = "Quality (~67%)", .name = "Quality", .minimum_scale = .64f, .maximum_scale = .70f, .live_percentage = 66.66f},
    {.label = "Balanced (~58%)", .name = "Balanced", .minimum_scale = .56f, .maximum_scale = .60f, .live_percentage = 58},
    {.label = "Performance (50%)", .name = "Performance", .minimum_scale = .50f, .maximum_scale = .52f, .live_percentage = 50},
    {.label = "Custom (1-100%)", .name = "Custom", .minimum_scale = .01f, .maximum_scale = 1},
    {.label = "Ultra Performance (~33%)", .name = "Ultra Performance", .minimum_scale = .32f, .maximum_scale = .35f, .live_percentage = 33.33f},
    // Not an NVIDIA SDK mode: the common 77%.
    {.label = "Ultra Quality (77%)", .name = "Ultra Quality", .minimum_scale = .74f, .maximum_scale = .80f, .live_percentage = 77},
}};

// UE pads its render-size buffers to multiples of 4 (8 in later versions: switched to once seen). DLSS render sizes
// are whole units of the output's aspect ratio in those multiples: the view fills its buffer and both axes get exactly
// the same scale (no custom-scale shimmer; ATN, Shogun 2).
static std::atomic<uint32_t> buffer_multiple = 4;

// UE reuses 29C140B2/201C3D3D with reduced inputs at high screen percentages.
// A shader hash identifies the compute contract, not the active render scale. The last four (seen in the shader dump
// only, never in a frame) read and write what the first six do, without the upsample filter's kernel width (E91209D0
// with it). Not here: the six that also write a half-size copy of the result to u1 (skipping them would leave it old)
// and the depth of field's four temporal passes (census: tools/nontw/expeditions/census/census_taa.txt).
constexpr std::array<uint32_t, 10> TEMPORAL_SHADERS = {
    0x29C140B2u, 0x201C3D3Du, 0xCCB4A156u, 0xD8C07EEEu, 0x4B43383Du, 0xB266ECB6u,
    0xE91209D0u, 0x71C87DBCu, 0x90BED7D1u, 0xBC7F1717u};

struct Texture {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

// The helper's inputs are the bridge's textures (the prepare shader writes them); its output merged with stock TAA's alpha
// when not replacing (`merged`).
struct Runtime {
  talesofarise::dlss::ConstantReadback constant_readback;
  talesofarise::dlss::Performance performance;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> prepare_shader;
  ComPtr<ID3D11ComputeShader> merge_shader;
  ComPtr<ID3D11Buffer> prepare_constants;
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
};

static struct State {
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
  talesofarise::dlss::PerformanceSummary performance;
  std::string status = "Off — original game TAA";
  std::string constants;
  uint64_t copied_bytes = 0;
  uint64_t copied_per_frame = 0;
} state;

static std::atomic<AAMode> requested_mode = AAMode::OFF;
static std::atomic<uint32_t> preset = 12u;  // NVSDK_NGX_DLSS_Hint_Render_Preset L
// The page's upscaler (dlaa_bridge::UPSCALER_*) and its name.
static std::atomic<uint32_t> requested_upscaler = twattila::dlaa_bridge::UPSCALER_DLSS;
constexpr std::array<const char*, 3> UPSCALER_NAMES = {"DLSS", "FSR", "XeSS"};
// Skip the stock TAA dispatch once constants are known before it (CPU copy trusted); not with the Upscaling Debug View
// (the merge draws it).
static std::atomic_bool replace_taa = true;
static std::atomic_bool upscaling_debug_view = false;  // Motion Vectors
// Active input size the temporal pass last received (width | height << 32), even when
// rejected, and how many temporal passes were seen.
static std::atomic<uint64_t> seen_input = 0;
static std::atomic<uint64_t> seen_frames = 0;
// Present (+1) at which Upscaling Details (the page) was last drawn; timings are measured only then.
static std::atomic<uint64_t> overlay_frame = 0;

// Why NVIDIA's history restarted, and UE's jitter pattern length (frames until its first
// offset repeats); NVIDIA wants at least 8 * (output / input)^2.
static struct Diagnostics {
  std::atomic<uint64_t> camera_cuts = 0;
  std::atomic<uint64_t> skipped_frames = 0;
  std::atomic<uint64_t> restarts = 0;
  std::array<float, 2> first_jitter = {};
  uint32_t jitter_frames = 0;
  std::atomic<uint32_t> jitter_phases = 0;
} diagnostics;
static bool attached = false;

// UE 4.23 keeps the jitter position in a signed byte (FSceneViewState +0x4AA, loaded with movsx). Past 128 positions
// (8 * (output / input)^2: below 25% scale) it turns negative and Halton(position + 1 <= 0) is 0, so the jitter stops
// for 127 of every 256 frames: DLSS pulses blurry/sharp. Loaded unsigned (movsx -> movzx), the pattern is right up to
// UE's cap of 255 positions (below 18% it repeats early). Applied while the addon is loaded.
constexpr std::array<std::string_view, 2> JITTER_POSITION_LOADS = {
    "44 0F BE B6 ?? ?? 00 00 41 FF C6 45 3B F4",  // the jitter setup: load, +1, wrap at the pattern length
    "CC 0F BE 81 ?? ?? 00 00 C3",                  // FSceneViewState::GetCurrentTemporalAASampleIndex
};
constexpr size_t SIGNED_LOAD_OPCODE = 2;  // 0F BE (movsx) -> 0F B6 (movzx)
static std::array<talesofarise::code_patch::CodePatch, JITTER_POSITION_LOADS.size()> jitter_position_patches;

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
static struct HaltonJitter {
  talesofarise::code_patch::CodePatch patch;  // the jump into the block
  float* sample = nullptr;                       // x, y in input pixels
  uint32_t positions = 0;
  uint32_t index = 0;
} halton_jitter;

// Live render scale (engine_scale.hpp): the AA mode's percentage is written into
// r.ScreenPercentage; Off or disabling puts the game's own value back.
static struct LiveScale {
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

// Graphics presets: engine settings written live (engine_cvars.hpp) while a preset is chosen; "Game's own" puts the
// game's values back. A preset only raises quality: higher-is-better settings take the larger of the game's value
// and the preset's, lower-is-better ones (LOD scales, thresholds, cell sizes) the smaller. KEEP: the game's value.
// Each setting's values: High, Ultra, Extreme, Insane; then one extra choice offered only in the setting's own list.
using talesofarise::graphics_table::Better;
using talesofarise::graphics_table::KEEP;
using GraphicsSetting = talesofarise::graphics_table::Setting;
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
    {"Shadow quality", L"r.Shadow.MaxResolution", "Light shadow resolution (point / spot)", Better::HIGHER, {4096.f, 4096.f, 8192.f, 8192.f, KEEP},
     "Shadow sharpness of point and spot lights."},
    {"Shadow quality", L"r.ShadowQuality", "Shadow filtering quality", Better::HIGHER, {KEEP, 4.f, 5.f, 5.f, KEEP},
     "Smoothing of shadow edges."},
    {"Shadow quality", L"r.Shadow.CSM.MaxCascades", "Sun shadow cascades", Better::HIGHER, {3.f, 4.f, 4.f, 4.f, KEEP},
     "Layers of the sun's shadow: more of them keep shadows sharp at every distance."},
    {"Shadow quality", L"r.Shadow.MaxCSMResolution", "Sun shadow resolution", Better::HIGHER, {4096.f, 4096.f, 4096.f, 4096.f, KEEP},
     "Unreal puts all sun shadow cascades into one 8192 x 8192 texture and does not draw a cascade that does not fit\n"
     "(its shadows vanish). So the resolution is limited by the cascade count: up to 4 cascades 4096, up to 9\n"
     "cascades 2730, up to 16 cascades 2048."},
    // The engine clamps it to 2.
    {"Shadow quality", L"r.Shadow.DistanceScale", "Sun shadow distance", Better::HIGHER, {KEEP, 1.25f, 1.5f, 2.f, KEEP},
     "How far the sun's shadows reach."},
    {"Shadow quality", L"r.Shadow.RadiusThreshold", "Shadows of small objects", Better::LOWER, {.02f, .01f, .005f, .001f, KEEP},
     "How small an object can be and still cast a shadow (smaller: more of them)."},
    {"Shadow quality", L"r.Shadow.CSM.TransitionScale", "Sun shadow cascade blending", Better::HIGHER, {KEEP, 1.5f, 2.f, 2.f, KEEP},
     "Smoother change between the sun shadow's layers."},
    {"Shadow quality", L"r.Shadow.PreShadowResolutionFactor", "Pre-shadow resolution", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "Sharpness of moving objects' shadows on the scenery."},
    {"Shadow quality", L"r.Shadow.TexelsPerPixel", "Object shadow resolution", Better::HIGHER, {KEEP, KEEP, 2.f, 3.f, KEEP},
     "Shadow sharpness of single objects such as characters."},
    {"Shadow quality", L"r.Shadow.TexelsPerPixelSpotlight", "Spot light shadow resolution", Better::HIGHER, {KEEP, KEEP, 2.f, 3.f, KEEP},
     "Shadow sharpness from spot lights."},
    {"Shadow quality", L"r.Shadow.TexelsPerPixelPointlight", "Point light shadow resolution", Better::HIGHER, {KEEP, KEEP, 2.f, 3.f, KEEP},
     "Shadow sharpness from point lights such as torches."},
    {"Shadow quality", L"r.Shadow.TexelsPerPixelRectlight", "Rect light shadow resolution", Better::HIGHER, {KEEP, KEEP, 2.f, 3.f, KEEP},
     "Shadow sharpness from area lights."},
    {"Shadow quality", L"r.CapsuleShadowsFullResolution", "Character soft shadows at full resolution", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "1: the soft shadows under characters at full resolution (the game: half)."},
    {"Shadow quality", L"r.DFFullResolution", "Distance field shadows at full resolution", Better::HIGHER, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "1: distance field shadows (far objects) at full resolution (the game: half)."},
    {"Shadow quality", L"r.Shadow.MinResolution", "Distant object shadow resolution", Better::HIGHER, {KEEP, KEEP, KEEP, KEEP, 64.f},
     "Least resolution of distant objects' shadows."},
    {"Shadow quality", L"r.Shadow.FadeResolution", "Distant object shadow fade", Better::LOWER, {KEEP, KEEP, KEEP, KEEP, 32.f},
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
     "Pixels per fog cell. Larger: faster, blockier fog.", {16.f, KEEP, KEEP}},
    {"Fog and effects", L"r.VolumetricFog.GridSizeZ", "Volumetric fog depth slices (fewer: faster)", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 96.f},
     "Fog slices into the distance. Fewer: faster, less fog detail in depth.", {64.f, KEEP, KEEP}},
    {"Fog and effects", L"r.VolumetricFog.HistoryMissSupersampleCount", "Volumetric fog samples on camera moves (fewer: faster)", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 2.f},
     "Samples for fog the camera just revealed. Fewer: faster, noisier fog while turning.", {1.f, KEEP, KEEP}},
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
    // UE 4.23's screen-space global illumination: one cvar (no r.SSGI.Enable / HalfRes in this exe). Registered with 0
    // (off, the game's); the renderer draws it while the value is above 0 and clamps the quality to 1..4.
    {OPTIONAL_GROUP, L"r.SSGI.Quality", "Screen-space global illumination", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 4.f},
     "Light bounces off nearby surfaces onto others (Unreal's own, screen-space). Brighter shade, slower.\n"
     "1-4: rays per pixel (4: least noise).",
     {1.f, KEEP, KEEP}},
    // In no preset (a load-time trade-off, not quality): read when an area's shaders load.
    {"Loading", L"r.CreateShadersOnLoad", "Shaders on load", Better::EXACT, {KEEP, KEEP, KEEP, KEEP, 1.f},
     "1: creates all of an area's shaders while it loads, instead of the first time an effect or material appears on\n"
     "screen. Fewer stutters when something new shows up for the first time (the typical DirectX 11 hitching).\n"
     "Loading takes a bit longer and uses a bit more memory. Applies to areas loaded afterwards: load a save after\n"
     "changing it. 0: the engine's default (also overrides an Engine.ini line).",
     {0.f, KEEP, KEEP}},
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
static bool effects_at_output = true;
// Ray tracing (Experimental, ray_tracing.hpp): its settings (config RayTraced*, PathTracing*), installed when an effect
// is on at launch.
static twattila::rt_scene::Settings ray_tracing_settings;
static float ray_traced_bounce_strength = 1.f;
static bool ray_tracing_installed = false;
// The page's rows (AppendPage): DLSS's (feature_settings::AppendAa), the rays' (rt_scene::AppendSettings), the rays' light
// bounce strength and this game's own; OnPresent reads them into the state above.
static twattila::feature_settings::AaSliders aa_sliders;
static twattila::rt_scene::Sliders rt_sliders;
static float light_bounce_strength = 1.f;
static float live_scale_row = 1.f;
static float replace_taa_row = 1.f;
static float contact_shadows_row = 0.f;  // percent of the screen
// No Bend shadows in this game (graphics_page.hpp Bindings::has_screen_space_shadows false).
static twattila::feature_settings::SssSliders no_screen_space_shadows;

// UE 4.23's View rows the upscalers and frame generation read (Tales' 4.21 rows + 4: ClipToWorld came before them):
// TranslatedWorldToView, ViewToClipNoAA, the camera's axes, TranslatedWorldCameraOrigin, PreViewTranslation.
constexpr uint32_t VIEW_TRANSLATED_WORLD_TO_VIEW = 12;
constexpr uint32_t VIEW_VIEW_TO_CLIP_NO_AA = 32;
constexpr uint32_t VIEW_FORWARD = 56;
constexpr uint32_t VIEW_UP = 57;
constexpr uint32_t VIEW_RIGHT = 58;
constexpr uint32_t VIEW_TRANSLATED_CAMERA_ORIGIN = 64;
constexpr uint32_t VIEW_PRE_VIEW_TRANSLATION = 66;
// UE's world size (centimetres): frame generation's far plane (the projection's is at infinity).
constexpr float FAR_PLANE = 2097152.f;
// The tonemapper's permutations (the HDR addon's t00-t09): frame generation's hudless colour after them.
constexpr std::array<uint32_t, 10> TONEMAPPER_SHADERS = {0x316D5B96u, 0x70CBF75Eu, 0x3BAE1754u, 0xC4ABB3CCu, 0x3D18E77Bu,
                                                         0xA77C9003u, 0xB176941Cu, 0xAAAEB4F0u, 0x2501C25Fu, 0x37FDFDF2u};

// Frame generation's inputs from this frame's upscaling (KeepFrame after the tone mapper), around the camera (UE's
// translated world); the last frame's view-projection and translation (its camera's step moves it).
static struct FrameGenerationFrame {
  std::shared_mutex mutex;
  twattila::frame_generation::Inputs inputs;
  uint64_t frame = UINT64_MAX;
  std::array<float, 16> previous_view_projection = {};
  std::array<float, 3> previous_translation = {};
  bool previous_kept = false;
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
// ReShade.ini, section GRAPHICS_SECTION, key the cvar name, by value (graphics_table::SaveChoice): steps move the indexes. (The old
// "Rome Graphics Settings" section held indexes.)
constexpr const char* GRAPHICS_SECTION = "Rome Graphics Values";

static struct Graphics {
  talesofarise::engine_cvars::Console console;
  bool searched = false;
  bool available = false;
  std::string status = "Searching the game's settings...";
  std::array<talesofarise::graphics_table::Entry, GRAPHICS_SETTINGS.size()> entries;
} graphics;

// Texture LOD bias below 100% (DLSS section). With temporal upscaling UE 4.23 biases material textures (599 of the
// game's pixel shaders sample with View.MaterialTextureMipBias) by max(log2(scale) + r.ViewTextureMipBias.Offset,
// r.ViewTextureMipBias.Min), read every frame (PreVisibilityFrameSetup); the game's -0.3 and -1 leave textures blurrier
// than NVIDIA's log2(scale) - 1 below ~57%. While DLSS renders below 100%: offset 0 / -1 / -2 (Matched / Sharper /
// Sharpest) and no minimum; otherwise the game's values.
constexpr std::array<const wchar_t*, 2> TEXTURE_LOD_BIAS_CVARS = {L"r.ViewTextureMipBias.Offset", L"r.ViewTextureMipBias.Min"};
constexpr float LOWEST_MIP_BIAS = -16.f;  // D3D11's lowest; no minimum in practice
static struct TextureLodBias {
  std::array<talesofarise::engine_cvars::Cvar, TEXTURE_LOD_BIAS_CVARS.size()> cvars;
  std::array<float, TEXTURE_LOD_BIAS_CVARS.size()> game_values = {};
  bool controlling = false;
  int choice = 2;  // 0 off (the game's), 1 matched, 2 sharper, 3 sharpest; Present thread (the overlay runs in it)
} texture_lod_bias;
inline void RestoreTextureLodBias() {
  if (!texture_lod_bias.controlling) return;
  for (size_t index = 0; index < TEXTURE_LOD_BIAS_CVARS.size(); ++index) {
    texture_lod_bias.cvars[index].Set(texture_lod_bias.game_values[index]);
  }
  texture_lod_bias.controlling = false;
}

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

// The mode as the upscaler calls it (DLSS at 100%: DLAA).
inline std::string ModeName(AAMode mode) {
  const uint32_t upscaler = std::min<uint32_t>(requested_upscaler.load(), UPSCALER_NAMES.size() - 1);
  if (mode == AAMode::DLAA && upscaler == twattila::dlaa_bridge::UPSCALER_DLSS) return "DLAA";
  return std::string(UPSCALER_NAMES[upscaler]) + " " + AA_MODES[static_cast<size_t>(mode)].name;
}

inline void SetStatus(std::string_view message, uint64_t generation = UINT64_MAX) {
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

inline void Reset() {
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
  }
}

inline void FinishFrame(Context&, const void* data) {
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

// Validation, the helper's evaluation and commit. `replace`: before the stock dispatch, which is skipped on success, the
// upscaled frame copied into the game's output; otherwise after it, merged with its alpha.
inline bool Evaluate(Frame* frame, bool replace) {
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
      || std::abs(taa[26][2] - render_width / width) > 0.0010002436f
      || std::abs(taa[26][3] - width / render_width) > .001f
      || !std::isfinite(taa[26][0]) || !std::isfinite(taa[26][1])
      || std::abs(taa[26][0]) > 1 || std::abs(taa[26][1]) > 1
      || !std::isfinite(view[129][1]) || view[129][1] <= 0
      || !std::isfinite(view[129][2]) || !std::isfinite(view[122][0]) || !std::isfinite(view[122][1])
      || std::abs(view[129][1] * view[129][2] - 1.f) > 0.0010000403f
      || std::abs(view[122][0] * render_width / 2 - taa[26][0]) > .001f
      || std::abs(-view[122][1] * render_height / 2 - taa[26][1]) > 0.0010002838f) {
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
    SetStatus("Stock TAA: unsupported scale/layout for " + ModeName(frame->mode) + "; keep Fast or Ultra TAA and Temporal Upscaling ON below 100%",
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
    auto constant_readback = std::move(runtime.constant_readback);
    auto performance = std::move(runtime.performance);
    runtime = Runtime{};
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
    runtime.width = frame->width;
    runtime.height = frame->height;
    runtime.render_width = frame->render_width;
    runtime.render_height = frame->render_height;
    runtime.mode = frame->mode;
    runtime.temporal_upscaling = frame->temporal_upscaling;
    runtime.temporal_shader_hash = frame->temporal_shader_hash;
  }

  // The helper's bridge (upscaling.hpp: the swap chain's size, the TAA output's); none: the game's TAA.
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (!talesofarise::upscaling::PrepareBridge(device.Get())) {
    SetStatus(twattila::dlaa_bridge::HelperInstalled() ? "Stock TAA: the helper (renodx-tw-dlaa64.exe) is not ready"
                                                       : "Stock TAA: renodx-tw-dlaa64.exe is not next to the add-on (the full download)",
              frame->generation);
    return false;
  }
  if (bridge.width != frame->width || bridge.height != frame->height) {
    SetStatus("Stock TAA: the helper's size is not the TAA output's", frame->generation);
    return false;
  }
  if (!replace && !runtime.merged.resource && !create_texture(&runtime.merged, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->width, frame->height)) {
    runtime.merged = {};
    SetStatus("Stock TAA: private texture allocation failed", frame->generation);
    return false;
  }
  {
    // Commit only for the current frame and mode. When replacing, the upscaled frame goes into the game's output; a
    // failure lets the stock dispatch run instead.
    const std::shared_lock lock(state.mutex);
    if (frame->generation != state.generation || frame->frame != state.frame || requested_mode.load() != frame->mode) return false;
  }
  const auto use = talesofarise::upscaling::Use();
  const bool nvidia_model = use.upscaler == twattila::dlaa_bridge::UPSCALER_DLSS + 1;
  // UE's camera (row vectors, centimetres around the camera: the translated world): the view, and the projection without
  // its jitter with the depth turned forward as the bridge's is (1 - the device depth: z' = w - z; near 0, infinity 1).
  std::array<float, 16> world_to_view = {};
  std::array<float, 16> view_to_clip = {};
  for (size_t row = 0; row < 4; ++row) {
    std::memcpy(&world_to_view[row * 4], frame->view[VIEW_TRANSLATED_WORLD_TO_VIEW + row].data(), 16);
    std::memcpy(&view_to_clip[row * 4], frame->view[VIEW_VIEW_TO_CLIP_NO_AA + row].data(), 16);
    view_to_clip[(row * 4) + 2] = view_to_clip[(row * 4) + 3] - view_to_clip[(row * 4) + 2];
  }
  const bool camera_cut = std::bit_cast<uint32_t>(frame->taa[0][2]) != 0;
  const bool restart = runtime.last_frame == UINT64_MAX;
  const bool skipped = !restart && frame->frame != runtime.last_frame + 1;
  const bool reset = camera_cut || restart || skipped;
  // Ray Reconstruction with a ray-traced effect whose G-buffer guides the sun draw wrote this frame (ray_tracing.hpp).
  const bool reconstruct = nvidia_model && use.ray_reconstruction != 0 && bridge.guides && ray_tracing_installed
                           && expeditionsrome::dlss::ray_tracing::HOST.guides_written();
  // The scene's light as it is (linear, before the tone mapper): no square root for any model.
  bridge.color_square_root = false;
  bool evaluated = false;
  twattila::dlaa_bridge::BeforeSharedWrites(context);
  {
    talesofarise::dlss::ContextStateScope restore(context, runtime.private_state.Get());
    runtime.performance.Mark(context);
    struct PrepareConstants {
      std::array<std::array<float, 4>, 4> clip_to_previous;
      uint32_t width;
      uint32_t height;
      std::array<float, 2> pixel_jitter;
      uint32_t copy_color;
      uint32_t debug_view;  // the merge's (0xA3450ADB): 1 the Upscaling Debug View
      std::array<uint32_t, 2> padding;
    } constants = {
        .width = frame->render_width,
        .height = frame->render_height,
        .pixel_jitter = (frame->temporal_upscaling
                             ? std::array<float, 2>{frame->taa[26][0], frame->taa[26][1]}
                             : std::array<float, 2>{}),
        .copy_color = 1u,
        .debug_view = upscaling_debug_view.load() ? 1u : 0u,
    };
    static_assert(sizeof(constants) == 96);
    std::memcpy(constants.clip_to_previous.data(), frame->view[118].data(), 64);
    context->UpdateSubresource(runtime.prepare_constants.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11Buffer* buffers[] = {runtime.prepare_constants.Get()};
    ID3D11ShaderResourceView* prep_inputs[] = {frame->inputs[1].Get(), frame->inputs[2].Get(), frame->inputs[0].Get()};
    ID3D11UnorderedAccessView* prep_outputs[] = {bridge.depth.uav.Get(), bridge.motion.uav.Get(), bridge.color.uav.Get()};
    context->CSSetShader(runtime.prepare_shader.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, buffers);
    context->CSSetShaderResources(0, 3, prep_inputs);
    context->CSSetUnorderedAccessViews(0, 3, prep_outputs, nullptr);
    context->Dispatch((frame->render_width + 7) / 8, (frame->render_height + 7) / 8, 1);
    runtime.performance.Mark(context);
    context->ClearState();
    if (camera_cut) ++diagnostics.camera_cuts;
    if (restart) ++diagnostics.restarts;
    if (skipped) ++diagnostics.skipped_frames;
    runtime.performance.Mark(context);
    const twattila::dlaa_bridge::RayReconstruction reconstruction = {.preset = (use.ray_reconstruction != 0) ? use.ray_reconstruction - 1 : 0u,
                                                                     .world_to_view = world_to_view,
                                                                     .view_to_clip = view_to_clip};
    evaluated = twattila::dlaa_bridge::Evaluate(context, frame->render_width, frame->render_height, frame->taa[26][0], frame->taa[26][1],
                                                reset, preset.load(), reconstruct ? &reconstruction : nullptr,
                                                nvidia_model ? nullptr : &view_to_clip);
    runtime.performance.Mark(context);
    if (evaluated && replace) {
      // Replacing: the upscaled frame is the TAA output (its alpha the helper's, as NVIDIA's was).
      const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = frame->width, .bottom = frame->height, .back = 1};
      context->CopySubresourceRegion(frame->target.Get(), 0, 0, 0, 0, bridge.output.texture.Get(), 0, &region);
    } else if (evaluated) {
      // After the stock dispatch: its alpha, and its RGB where the upscaler's is not finite (the Upscaling Debug View:
      // the motion and colour the upscaler got instead).
      ID3D11ShaderResourceView* merge_inputs[] = {bridge.output.srv.Get(), frame->target_srv.Get(), bridge.motion.srv.Get(), bridge.color.srv.Get()};
      ID3D11UnorderedAccessView* merge_outputs[] = {runtime.merged.uav.Get()};
      context->CSSetShader(runtime.merge_shader.Get(), nullptr, 0);
      context->CSSetConstantBuffers(0, 1, buffers);
      context->CSSetShaderResources(0, 4, merge_inputs);
      context->CSSetUnorderedAccessViews(0, 1, merge_outputs, nullptr);
      context->Dispatch((frame->width + 7) / 8, (frame->height + 7) / 8, 1);
      context->CopyResource(frame->target.Get(), runtime.merged.resource.Get());
    }
  }
  runtime.performance.End(context);
  if (!evaluated) {
    runtime.last_frame = UINT64_MAX;
    SetStatus(std::string("Stock TAA: ") + (bridge.shared != nullptr ? bridge.shared->status : "no answer from the helper"), frame->generation);
    return false;
  }
  if (twattila::frame_generation::Wanted()) {
    // Frame generation's camera around this frame's camera; the last frame's moved by the camera's step (a point here is
    // that much further around the last camera: its row 3 += step x rows 0-2).
    std::array<float, 16> view_projection = {};
    for (size_t row = 0; row < 4; ++row) {
      for (size_t column = 0; column < 4; ++column) {
        for (size_t k = 0; k < 4; ++k) view_projection[(row * 4) + column] += world_to_view[(row * 4) + k] * view_to_clip[(k * 4) + column];
      }
    }
    const auto& translation = frame->view[VIEW_PRE_VIEW_TRANSLATION];
    const auto vector = [&](uint32_t row) { return std::array<float, 3>{frame->view[row][0], frame->view[row][1], frame->view[row][2]}; };
    const auto position = vector(VIEW_TRANSLATED_CAMERA_ORIGIN);
    const auto right = vector(VIEW_RIGHT);
    const auto up = vector(VIEW_UP);
    const auto forward = vector(VIEW_FORWARD);
    const std::unique_lock lock(frame_generation_frame.mutex);
    auto& kept = frame_generation_frame;
    std::array<float, 16> previous = view_projection;
    if (!reset && kept.previous_kept) {
      previous = kept.previous_view_projection;
      const std::array<float, 3> step = {kept.previous_translation[0] - translation[0], kept.previous_translation[1] - translation[1],
                                         kept.previous_translation[2] - translation[2]};
      for (size_t column = 0; column < 4; ++column) {
        previous[12 + column] += (step[0] * previous[column]) + (step[1] * previous[4 + column]) + (step[2] * previous[8 + column]);
      }
    }
    kept.inputs = {
        .width = frame->render_width,
        .height = frame->render_height,
        .jitter_x = frame->taa[26][0],
        .jitter_y = frame->taa[26][1],
        .reset = reset,
        .view_to_clip = view_to_clip,
        .view_projection = view_projection,
        .previous_view_projection = previous,
        .camera = {position[0], position[1], position[2], right[0], right[1], right[2], up[0], up[1], up[2], forward[0], forward[1], forward[2]},
        // UE's reversed infinite projection: its near plane at [14].
        .near_plane = frame->view[VIEW_VIEW_TO_CLIP_NO_AA + 3][2],
        .far_plane = FAR_PLANE,
    };
    kept.frame = frame->frame;
    kept.previous_view_projection = view_projection;
    kept.previous_translation = {translation[0], translation[1], translation[2]};
    kept.previous_kept = true;
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
  SetStatus(ModeName(frame->mode) + " active — "
                + (frame->render_width == frame->width && frame->render_height == frame->height
                       ? "native resolution"
                       : "reduced-resolution input")
                + (replace ? ", replacing game TAA (experimental)" : " (experimental)"),
            frame->generation);
  return true;
}

inline void EvaluateFrame(Context&, const void* data) {
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
  if (mode_changed) Reset();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
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
  // Timings only while Upscaling Details is drawn, and not for the first frame of a mode or size.
  const uint64_t overlay = overlay_frame.load();
  frame->measuring = overlay != 0 && frame->frame + 1 - overlay < 120;
  frame->sample_cpu = frame->measuring && runtime.last_frame != UINT64_MAX;
  if (frame->sample_cpu) runtime.performance.Begin(frame->context.Get());
  if (frame->from_mirror && replace_taa.load()) {
    // Constants are known before the dispatch: the upscaler replaces the stock pass (~0 ms "stock TAA").
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

inline void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
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

inline void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool) {
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

inline void OnDestroyDevice(reshade::api::device* device) {
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

inline void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // Debug: also renodx_trace_now next to the exe traces the next frame (Tales' frame_trace.hpp: tales_frame_trace.txt).
  talesofarise::frame_trace::OnPresent();
  // The page's rows (Upscaling Mode: DLAA, Ultra Quality, Quality, Balanced, Performance, Ultra Performance, Custom).
  {
    constexpr std::array<AAMode, 7> UPSCALING_MODES = {AAMode::DLAA, AAMode::DLSS_ULTRA_QUALITY, AAMode::DLSS_QUALITY, AAMode::DLSS_BALANCED,
                                                       AAMode::DLSS_PERFORMANCE, AAMode::DLSS_ULTRA_PERFORMANCE, AAMode::DLSS_CUSTOM};
    // Without the helper (the HDR-only download) the game's TAA, its own render scale.
    const bool upscaling = twattila::feature_settings::Upscaling(aa_sliders) && twattila::dlaa_bridge::HelperInstalled();
    const AAMode mode = (upscaling ? UPSCALING_MODES[static_cast<size_t>(std::clamp(aa_sliders.dlss_mode, 0.f, 6.f))] : AAMode::OFF);
    if (requested_mode.exchange(mode) != mode && mode != AAMode::OFF) expeditionsrome::dlss::temporal_capture::Reset();
    const auto aa = twattila::feature_settings::AaSettings(aa_sliders);
    requested_upscaler.store(aa_sliders.method == 7.f ? twattila::dlaa_bridge::UPSCALER_XESS
                                                      : (aa_sliders.method == 6.f ? twattila::dlaa_bridge::UPSCALER_FSR : twattila::dlaa_bridge::UPSCALER_DLSS));
    preset.store(aa.dlaa_preset);
    live.custom = std::clamp(std::round(aa_sliders.render_scale), 1.f, 100.f);
    texture_lod_bias.choice = std::clamp(static_cast<int>(aa_sliders.texture_lod_bias), 0, 3);
    effects_at_output = aa_sliders.effects_at_output != 0.f;
    upscaling_debug_view.store(aa_sliders.debug_view != 0.f);
    replace_taa.store(replace_taa_row != 0.f && aa_sliders.debug_view == 0.f);
    expeditionsrome::dlss::contact_shadows::length.store(std::clamp(contact_shadows_row, 0.f, 10.001993f) / 100.f);
    // On again: a stopped or failed live scale is retried.
    if (const bool enabled = live_scale_row != 0.f; enabled != live.enabled) {
      live.enabled = enabled;
      if (enabled) {
        live.problem.clear();
        live.retries = 0;
        if (live.finder.phase() == talesofarise::engine_scale::Finder::Phase::FAILED) live.finder = {};
      }
    }
    ray_tracing_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, light_bounce_strength);
    ray_traced_bounce_strength = light_bounce_strength;
    // What the bridge serves this frame (upscaling.hpp); Ray Reconstruction with a ray-traced effect only.
    const auto& rays = ray_tracing_settings;
    const bool any_rays = ray_tracing_installed && (rays.sun_shadows || rays.ambient_occlusion || rays.light_bounce || rays.reflections);
    std::array<uint32_t, 2> size;
    {
      const std::shared_lock lock(state.mutex);
      size = {state.width, state.height};
    }
    talesofarise::upscaling::SetUse({
        .ray_tracing = ray_tracing_installed,
        .guides = ray_tracing_installed && (rays.reflections || rays.path_tracing),
        .reflections = ray_tracing_installed && rays.reflections,
        .upscaler = upscaling ? requested_upscaler.load() + 1 : 0u,
        .ray_reconstruction = (any_rays && aa.ray_reconstruction) ? aa.ray_reconstruction_preset + 1 : 0u,
        .frame_format = twattila::frame_generation::Format(),
        .frame_backend = aa.frame_backend,
        .width = size[0],
        .height = size[1],
    });
  }
  if (ray_tracing_installed) expeditionsrome::dlss::ray_tracing::Update(ray_tracing_settings, ray_traced_bounce_strength);
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
    const uint64_t copied = talesofarise::dlss::constants::CopiedBytes();
    state.copied_per_frame = copied - state.copied_bytes;
    state.copied_bytes = copied;
  }
  if (changed) Reset();
  if (mode == AAMode::OFF) {
    SetStatus("Off — original game TAA");
  } else if (missed || changed) {
    // Do not leave an old "active" label up when this view no longer matches.
    std::shared_lock lock(state.mutex);
    if (state.status.find(" active") != std::string::npos || changed) {
      lock.unlock();
      SetStatus("Stock TAA: waiting for " + ModeName(mode) + "; keep Fast or Ultra TAA and Temporal Upscaling ON below 100%");
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
  using Phase = talesofarise::engine_scale::Finder::Phase;
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
    if (talesofarise::code_patch::ApplyCodePatch(&halton_jitter.patch, halton)) {
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
    graphics.available = graphics.console.Start(talesofarise::engine_cvars::UE_4_23, &details);
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

// The Anti-Aliasing section's status lines (graphics_page.hpp Bindings::upscaler_status): DLSS's, the live render scale's,
// and what the game's settings or NVIDIA's files need.
inline std::string UpscalerStatus() {
  using Phase = talesofarise::engine_scale::Finder::Phase;
  std::string status;
  uint64_t completed;
  std::array<uint32_t, 4> extents;
  {
    const std::shared_lock lock(state.mutex);
    status = state.status;
    completed = state.completed;
    extents = {state.render_width, state.render_height, state.width, state.height};
  }
  std::string text = status;
  if (!live.enabled) {
    text += "\nLive render scale off: set Screen Percentage in the game's menu.";
  } else if (!live.problem.empty()) {
    text += (live.retries < MAX_LIVE_RETRIES
                 ? "\nLive render scale paused: " + live.problem + ". The game's own value is back for now; retrying in a few seconds."
                 : "\nLive render scale stopped: " + live.problem + ". The game's own value was put back; turn Live Render Scale off and on to retry.");
  } else if (live.finder.phase() == Phase::FAILED) {
    text += "\nLive render scale unavailable: " + live.finder.status() + " (details in ReShade.log).";
  } else if (live.finder.phase() != Phase::FOUND) {
    text += "\n" + live.finder.status() + "...";
  } else {
    text += std::format("\nRender scale {:g}%{}{}", live.finder.Get().value_or(0),
                        live.controlling ? std::format(" (the game's own: {:g}%)", live.game_value) : std::string(" (the game's own)"),
                        live.note.empty() ? std::string() : "; " + live.note);
  }
  if (completed != 0 && status.find("active") != std::string::npos) {
    text += std::format("\nInput {}x{} -> output {}x{}", extents[0], extents[1], extents[2], extents[3]);
  }
  // NVIDIA wants 8 * (output / input)^2 positions: UE's pattern has them down to 25% (signed position) or, with the
  // jitter fix, 18% (its cap of 255); NVIDIA's Halton (the jitter store's patch) any length.
  const auto mode = requested_mode.load();
  const bool jitter_fixed = std::ranges::all_of(jitter_position_patches, &talesofarise::code_patch::CodePatch::applied);
  const float lowest_scale = (jitter_fixed ? 18.f : 25.f);
  if (!halton_jitter.patch.applied
      && ((mode == AAMode::DLSS_CUSTOM && live.enabled && live.custom < lowest_scale)
          || (completed != 0 && extents[2] != 0 && static_cast<float>(extents[0]) / static_cast<float>(extents[2]) < lowest_scale / 100.f - .005f))) {
    text += std::format("\nBelow {:.0f}%, the game's (UE's) jitter pattern gets too short for the upscaler: the image can pulse blurry/sharp.", lowest_scale);
  }
  if (mode != AAMode::OFF && status.starts_with("Stock TAA")) {
    text += "\nIn the game's graphics settings, keep Anti-Aliasing on Fast or Ultra and Temporal Upscaling on.";
  }
  if (const auto& shared = twattila::dlaa_bridge::bridge.shared; shared != nullptr && shared->status[0] != '\0') {
    text += std::string("\nHelper: ") + shared->status;
  }
  if (ray_tracing_installed) text += "\n" + twattila::rt_scene::Status();
  return text;
}

// The former overlay's keys ([Rome AA]: the DLSS add-on's until the UE4 merge round) in the page's, once (SettingsRevision 1),
// each into every preset without one of its own; before the settings are loaded. Settings Mode's Advanced (1) is HDR &
// Effects (0) again: its 1 is Graphics 1 of 2 now.
inline void Migrate() {
  const std::string& global = renodx::utils::settings::global_name;
  if (int revision = 0; reshade::get_config_value(nullptr, global.c_str(), "SettingsRevision", revision) && revision >= 1) return;
  std::vector<std::pair<std::string, float>> values;
  const auto read = [](const char* key, float* value) { return reshade::get_config_value(nullptr, "Rome AA", key, *value); };
  // AAMode (as saved): the page's Anti-Aliasing and Upscaling Mode (DLAA, Ultra Quality, Quality, Balanced, Performance,
  // Ultra Performance, Custom).
  if (float mode = 0.f; read("Mode", &mode) && mode >= 0.f && mode < static_cast<float>(AA_MODES.size())) {
    constexpr std::array<float, 8> UPSCALING_MODES = {0.f, 0.f, 2.f, 3.f, 4.f, 6.f, 5.f, 1.f};
    values.emplace_back("AntiAliasing", (mode != 0.f ? 5.f : 0.f));
    values.emplace_back("AntiAliasingDlssMode", UPSCALING_MODES[static_cast<size_t>(mode)]);
  }
  // NVSDK_NGX_DLSS_Hint_Render_Preset E (5, gone: K, the next model), K (11), L (12), M (13): the page's K, M, L.
  if (float ngx_preset = 0.f; read("Preset", &ngx_preset)) {
    values.emplace_back("AntiAliasingDlaaPreset", (ngx_preset == 12.f ? 2.f : (ngx_preset == 13.f ? 1.f : 0.f)));
  }
  for (const auto& [old_key, new_key] : std::to_array<std::pair<const char*, const char*>>({
           {"CustomScale", "AntiAliasingRenderScale"},
           {"TextureLodBias", "AntiAliasingTextureLodBias"},
           {"EffectsAtOutputResolution", "AntiAliasingEffectsAtOutput"},
           {"LiveScale", "LiveScale"},
           {"ReplaceTAA", "ReplaceTAA"},
           {"RayTracedSunShadows", "RayTracedSunShadows"},
           {"RayTracedShadowSoftness", "RayTracedShadowSoftness"},
           {"RayTracedAmbientOcclusion", "RayTracedAmbientOcclusion"},
           {"RayTracedAmbientRange", "RayTracedAORange"},
           {"RayTracedAOQuarter", "RayTracedAOQuarter"},
           {"RayTracedLightBounceStrength", "light_bounce_strength"},
           {"RayTracedLightBounceRays", "RayTracedLightBounceRays"},
           {"RayTracedLightBounceRange", "RayTracedLightBounceRange"},
           {"RayTracedReflections", "RayTracedReflections"},
           {"RayTracedReflectionSamples", "RayTracedReflectionSamples"},
           {"PathTracingBounces", "PathTracingBounces"},
       })) {
    if (float value = 0.f; read(old_key, &value) && std::isfinite(value)) values.emplace_back(new_key, value);
  }
  // A share of the screen: the row's percent.
  if (float length = 0.f; read("ContactShadows", &length) && std::isfinite(length)) values.emplace_back("ContactShadowsLength", length * 100.f);
  // The light bounce through rays (1), path traced (2: Path tracing with it on).
  if (float bounce = 0.f; read("RayTracedLightBounce", &bounce)) {
    float paths = 0.f;
    read("PathTracing", &paths);
    values.emplace_back("RayTracedLightBounce", (bounce == 0.f ? 0.f : (paths != 0.f ? 2.f : 1.f)));
  }
  // The in-process frame generation's (talesofarise_dlss/frame_generation.hpp, Reflex always on with it) into the helper's
  // rows: its multiplier 2-6 (0 Dynamic, 1 Reflex alone) as the page's 2x-6x (1-5, Dynamic 6), its boost as Reflex's.
  if (float generation = 0.f; read("FrameGeneration", &generation)) {
    float multiplier = 2.f;
    float boost = 0.f;
    read("FrameGenerationMultiplier", &multiplier);
    read("ReflexBoost", &boost);
    const bool on = generation != 0.f;
    values.emplace_back("FrameGeneration", (!on || multiplier == 1.f) ? 0.f : (multiplier == 0.f ? 6.f : std::clamp(multiplier, 2.f, 6.f) - 1.f));
    values.emplace_back("Reflex", on ? (boost != 0.f ? 2.f : 1.f) : 0.f);
  }
  if (float limit = 0.f; read("FrameGenerationFrameLimit", &limit) && std::isfinite(limit)) values.emplace_back("FrameGenerationFrameLimit", limit);
  for (const char* preset : {"-preset1", "-preset2", "-preset3"}) {
    const std::string section = global + preset;
    for (const auto& [key, value] : values) {
      if (float saved = 0.f; reshade::get_config_value(nullptr, section.c_str(), key.c_str(), saved)) continue;
      reshade::set_config_value(nullptr, section.c_str(), key.c_str(), value);
    }
  }
  if (float settings_mode = 0.f; reshade::get_config_value(nullptr, global.c_str(), "SettingsMode", settings_mode) && settings_mode == 1.f) {
    reshade::set_config_value(nullptr, global.c_str(), "SettingsMode", 0);
  }
  reshade::set_config_value(nullptr, global.c_str(), "SettingsRevision", 1);
  renodx::utils::log::i("[Rome] Settings: ", values.size(), " values of [Rome AA] taken over by the page (SettingsRevision 1).");
}

// This game's page texts (graphics_page.hpp Texts): the game's TAA stays on for DLSS; the rays' sun shadows and AO with the
// game's (the darker of both).
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = {"Game's TAA", "", "", "", "", "DLSS", "FSR", "XeSS"},
    .aa_tooltip = "Game's TAA: the game's own temporal anti-aliasing.\n"
                  "In its place, also setting the game's render scale: DLSS: NVIDIA's (RTX), the best. FSR: AMD's, FSR 4 on\n"
                  "Radeon RX 6000 and newer, else 3.1. XeSS: Intel's, any GPU.\n"
                  "Every ray-traced effect needs an upscaler. Needs renodx-tw-dlaa64.exe and its DLLs in the game folder.",
    .in_game_aa_note = "In-game Anti-Aliasing must be Fast or Ultra with Temporal Upscaling on for DLSS, FSR and XeSS.",
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its shadows, AO and reflections.\n"
                     "The rest at its default.",
    .no_rays_tooltip = "No ray tracing: the game's shadows, ambient occlusion and reflections. The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows, ambient occlusion and reflections, path-traced Global Illumination at my settings. The\n"
                    "rest of this page at its default. Needs an upscaler (Anti-Aliasing above); on NVIDIA also turns DLSS Ray\n"
                    "Reconstruction on. Restart the game after turning ray tracing on.",
    .shadow_tooltip = "Game's Shadow Maps: the original. DXR: ray-traced sun shadows with the game's (the darker of both): also\n"
                      "where its shadow maps miss things or end. Needs an upscaler. Restart the game after turning it on.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep in-game ambient occlusion on.\n"
                  "Game's SSAO: the original. GT-VBAO: contact shading that lets light pass behind thin things (grass,\n"
                  "poles, characters) instead of dark halos.\n"
                  "RTAO: from rays through the scene, with the game's. Needs an upscaler. Restart the game after turning it on.",
    .gi_tooltip_end = "\nRestart the game after turning it on.",
    .effects_at_output_tooltip = "While the upscaler renders below 100%, volumetric fog, light shafts, skin and the shadows of characters,\n"
                                 "objects and lamps keep the detail they have at 100%. Looks better, can cost some performance.",
    .dlss_preset_tooltip = "NVIDIA's models: K the first transformer, M the second, L the sharpest and heaviest (default).\n"
                           "A preset forced in the NVIDIA app or NVIDIA Profile Inspector overrides this one.",
    .reflections_tooltip = "Reflections on water, metal and wet ground. Need in-game Screen Space Reflections on.\n"
                           "Game's SSR: the original. DXR: from rays, also what is off screen. Needs an upscaler. Restart the game\n"
                           "after turning it on.",
};
// The page's anti-aliasing: the game's TAA, DLSS, FSR, XeSS.
constexpr std::array<int, 4> AA_METHODS = {0, 5, 6, 7};
// Graphics 1 of 2's own section after graphics_page's: the live render scale, the TAA pass, the details and the tools.
inline const std::string TOOLS_SECTION = "Upscaling and Tools";
inline const std::string GRAPHICS_PRESETS_SECTION = "Graphics Presets";

// Graphics 2 of 2's sections: the presets', then each group's (GRAPHICS_SETTINGS' order).
inline std::vector<std::string> EngineSections() {
  std::vector<std::string> sections = {GRAPHICS_PRESETS_SECTION};
  for (const auto& setting : GRAPHICS_SETTINGS) {
    if (std::ranges::find(sections, setting.group) == sections.end()) sections.emplace_back(setting.group);
  }
  return sections;
}

// The page's rows (HDR part's ConfigureAddon, after its own rows): Graphics 1 of 2 (graphics_page.hpp with
// feature_settings::AppendAa's and rt_scene::AppendSettings' rows, then TOOLS_SECTION), Graphics 2 of 2
// (GRAPHICS_SETTINGS, EngineSections); `ssao_mode` GT-VBAO's (the HDR part's). After feature_settings::SetTints.
inline void AppendPage(std::vector<renodx::utils::settings::Setting*>* settings, float* ssao_mode) {
  using renodx::utils::settings::Setting;
  using renodx::utils::settings::SettingValueType;
  const auto upscaling = [] { return twattila::feature_settings::Upscaling(aa_sliders); };
  const auto erase = [settings](std::initializer_list<std::string_view> keys) {
    std::erase_if(*settings, [keys](Setting* setting) {
      if (std::ranges::find(keys, setting->key) == keys.end()) return false;
      delete setting;
      return true;
    });
  };
  // The upscalers with their modes, DLSS's preset and Ray Reconstruction, Texture LOD Bias, Effects at Output Resolution,
  // the helper's frame generation and Reflex, the Upscaling Debug View (the motion alone: the merge's, 0xA3450ADB); not
  // the shader anti-aliasing (TW's) or object motion (Unreal's own motion vectors).
  twattila::feature_settings::AppendAa(settings, &aa_sliders, true, true);
  erase({"AntiAliasingQuality", "AntiAliasingExtraSharpness", "AntiAliasingTaaStrength", "AntiAliasingSharpening", "AntiAliasingObjectMotion"});
  // The rays (ray_tracing.hpp: sun shadows, AO, the light bounce and its paths, reflections); not what this host doesn't
  // apply (specular occlusion; the light bounce's resolution: its own half-size texels).
  twattila::rt_scene::AppendSettings(settings, &rt_sliders, &aa_sliders, false, true, twattila::feature_settings::AA_SECTION, &light_bounce_strength);
  erase({"RayTracedSpecularOcclusion", "RayTracedLightBounceResolution"});
  for (Setting* setting : *settings) {
    // The former overlay's AO range; leaf micromaps (the helper's opacity micromaps over the game's cut-out masks) as the
    // former add-on ran: off (no run of this game has shown them yet).
    if (setting->key == "RayTracedAORange") setting->default_value = 2.f;
    if (setting->key == "RayTracedLeafMicromaps" || setting->key == "RayTracedGpuMicromaps") setting->default_value = 0.f;
    if (setting->key == "AntiAliasingDebugView") setting->labels = {"Off", "Motion Vectors"};
  }
  // One frame's GPU time of every pass (twattila/pass_timing.hpp, the add-on's first draw hooks).
  settings->push_back(new Setting{
      .value_type = SettingValueType::BUTTON,
      .label = "Time Each Pass (One Frame)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "The GPU's time of every pass of the next frame into expeditionsrome_pass_timing.txt next to the game's exe.",
      .on_change = [] { twattila::pass_timing::Request(); },
  });
  settings->push_back(new Setting{
      .key = "light_bounce_strength",
      .binding = &light_bounce_strength,
      .default_value = 1.f,
      .label = "Strength",
      .section = twattila::lighting_settings::GI_SECTION,
      .tooltip = "How much bounced light is added.",
      .max = 2.f,
      .format = "%.2f",
  });
  twattila::graphics_page::Apply(settings, {.aa = &aa_sliders,
                                            .sss = &no_screen_space_shadows,
                                            .rt = &rt_sliders,
                                            .ssao_mode = ssao_mode,
                                            .light_bounce_strength = &light_bounce_strength,
                                            .texts = &PAGE_TEXTS,
                                            .aa_methods = AA_METHODS,
                                            .upscaler_status = &UpscalerStatus,
                                            .has_foliage_light = false,
                                            .has_path_ambient = false,
                                            .rays_replace_ao = false,
                                            .rays_multiply_maps = true,
                                            .has_screen_space_shadows = false,
                                            .has_gtvbgi = false,
                                            .rays_tested = false,
                                            .every_row = false});
  // The Upscaling Debug View's one view (the page's tooltip names Total War's object motion too).
  for (Setting* setting : *settings) {
    if (setting->key == "AntiAliasingDebugView") setting->tooltip = "Motion Vectors: the motion the upscaler gets (colour: direction, brighter: faster).";
  }
  renodx::utils::settings::section_tints.emplace_back(TOOLS_SECTION, twattila::graphics_page::TITLE_TINT);

  // Graphics 1 of 2's own section.
  settings->insert(settings->end(), {
      new Setting{
          .key = "LiveScale",
          .binding = &live_scale_row,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Live Render Scale",
          .section = TOOLS_SECTION,
          .tooltip = "DLSS sets the game's render scale (Screen Percentage) from the Upscaling Mode, at once: DLAA 100%, Ultra\n"
                     "Quality 77%, Quality 66.66%, Balanced 58%, Performance 50%, Ultra Performance 33.33%, Custom its row.\n"
                     "Off: set Screen Percentage in the game's menu (it stops at 50%). The game's Anti-Aliasing Off puts its\n"
                     "own value back.",
          .is_visible = upscaling,
      },
      new Setting{
          .key = "ReplaceTAA",
          .binding = &replace_taa_row,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Replace the Game's TAA Pass (Faster)",
          .section = TOOLS_SECTION,
          .tooltip = "DLSS writes straight into the game's TAA output and the game's TAA pass is skipped. Off: the game's TAA\n"
                     "runs first and DLSS is merged with its alpha.",
          .is_visible = upscaling,
      },
      // What the upscaler did (the timings measured only while this is drawn).
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Upscaling Details",
          .section = TOOLS_SECTION,
          .on_draw = [] {
            {
              const std::shared_lock lock(state.mutex);
              overlay_frame.store(state.frame + 1);
            }
            uint64_t completed;
            uint32_t temporal_shader_hash;
            uint64_t copied_per_frame;
            std::string constants;
            talesofarise::dlss::PerformanceSummary performance;
            std::array<uint32_t, 4> extents;
            {
              const std::shared_lock lock(state.mutex);
              completed = state.completed;
              temporal_shader_hash = state.temporal_shader_hash;
              copied_per_frame = state.copied_per_frame;
              constants = state.constants;
              performance = state.performance;
              extents = {state.render_width, state.render_height, state.width, state.height};
            }
            ImGui::Text("Successful upscaled frames: %llu", static_cast<unsigned long long>(completed));
            if (completed != 0) {
              const uint32_t phases = diagnostics.jitter_phases.load();
              const auto recommended = static_cast<uint32_t>(std::ceil(8.0 * std::pow(static_cast<double>(extents[2]) / std::max(extents[0], 1u), 2)));
              const bool jitter_fixed = std::ranges::all_of(jitter_position_patches, &talesofarise::code_patch::CodePatch::applied);
              ImGui::Text("Temporal shader: 0x%08X", temporal_shader_hash);
              if (texture_lod_bias.controlling) {
                ImGui::Text("Texture LOD bias: log2(render scale) %+g (the game's: %+g, at least %g)", texture_lod_bias.cvars[0].Get(),
                            texture_lod_bias.game_values[0], texture_lod_bias.game_values[1]);
              }
              ImGui::Text("Jitter pattern: %s (NVIDIA recommends %u or more); %s", phases != 0 ? std::to_string(phases).c_str() : "measuring",
                          recommended,
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
              ImGui::Text("CPU constants (queue + wait): %.3f ms", performance.cpu_readback_ms);
              ImGui::Text("CPU addon submission: %.3f ms", performance.cpu_evaluate_ms);
              if (performance.gpu_samples != 0) {
                ImGui::Text("GPU stock TAA: %.3f ms", performance.gpu_ms[0]);
                ImGui::Text("GPU input preparation: %.3f ms", performance.gpu_ms[1]);
                ImGui::Text("GPU helper evaluation: %.3f ms", performance.gpu_ms[2]);
                ImGui::Text("GPU merge + copy (none when replacing): %.3f ms", performance.gpu_ms[3]);
              }
              ImGui::TextWrapped("Averages since choosing this mode, measured only while this is shown. CPU and GPU times overlap.");
            }
            return false;
          },
          .is_visible = upscaling,
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Trace One Frame",
          .section = TOOLS_SECTION,
          .tooltip = "Writes tales_frame_trace.txt next to the game's exe about a second later, for bug reports.",
          .on_change = [] {
            // talesofarise_dlss/frame_trace.hpp looks for this marker at its presents.
            std::wstring exe(32768, L'\0');
            exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
            std::ofstream(std::filesystem::path(exe).parent_path() / L"renodx_trace_now", std::ios::binary | std::ios::trunc);
            renodx::utils::log::i("[Rome] Trace One Frame: renodx_trace_now written.");
          },
      },
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Capture Upscaling Inputs",
          .section = TOOLS_SECTION,
          .on_draw = [] {
            if (ImGui::Button("Capture Upscaling Inputs (15 Seconds)")) expeditionsrome::dlss::temporal_capture::QueueCapture();
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Diagnostics: keep the game's TAA on, then capture at fixed Screen Percentage / Temporal Upscaling settings.");
            }
            ImGui::TextWrapped("%s", expeditionsrome::dlss::temporal_capture::Snapshot().c_str());
            return false;
          },
          .is_visible = [] { return aa_sliders.method == 0.f; },
      },
  });

  // Graphics 2 of 2: the presets (every setting but shaders on load gets the preset's value, KEEP: the game's, so none keeps
  // an older choice; contact shadows only in Game's own and Max), then each group's settings in its own section with their
  // saved choices.
  talesofarise::graphics_table::Prepare(GRAPHICS_SECTION, GRAPHICS_SETTINGS, graphics.entries);
  const auto apply_preset = [](size_t preset_index) {
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
            if (std::isnan(value) && std::string_view(setting.group) == OPTIONAL_GROUP) value = setting.values[INSANE_PRESET];
            break;
        }
      } else if (preset_index != 0) {
        value = setting.values[preset_index - 1];
      }
      const auto found = std::ranges::find(entry.choices, value);
      entry.choice = (found == entry.choices.end() ? 0 : static_cast<int>(found - entry.choices.begin()) + 1);
      talesofarise::graphics_table::SaveChoice(GRAPHICS_SECTION, entry);
    }
    if (preset_index == 0) {
      renodx::utils::settings::UpdateSettings({{"ContactShadowsLength", 0.f}});
    } else if (preset_index == MAX_PRESET) {
      renodx::utils::settings::UpdateSettings({{"ContactShadowsLength", CONTACT_SHADOWS_MAX * 100.f}});
    }
  };
  settings->push_back(new Setting{.value_type = SettingValueType::TEXT,
                                  .label = "Applied live; a preset never lowers the game's own settings.",
                                  .section = GRAPHICS_PRESETS_SECTION});
  for (size_t preset_index = 0; preset_index < GRAPHICS_PRESETS.size(); ++preset_index) {
    settings->push_back(new Setting{
        .value_type = SettingValueType::BUTTON,
        .label = GRAPHICS_PRESETS[preset_index],
        .section = GRAPHICS_PRESETS_SECTION,
        .group = "graphics-presets",
        .on_change = [apply_preset, preset_index] { apply_preset(preset_index); },
    });
  }
  settings->insert(settings->end(), {
      new Setting{.value_type = SettingValueType::TEXT,
                  .label = "Some settings apply after loading a new area, anisotropic filtering to textures loaded afterwards. Insane and "
                           "Max are very heavy and use a lot of VRAM. Max puts every setting at its highest, the optional ones too. "
                           "Every preset keeps the game's volumetric fog: turning it off or lowering it is the biggest saving. Shaders "
                           "on load is in no preset.",
                  .section = GRAPHICS_PRESETS_SECTION},
      new Setting{.value_type = SettingValueType::CUSTOM,
                  .section = GRAPHICS_PRESETS_SECTION,
                  .on_draw = [] {
                    if (!graphics.status.empty()) ImGui::TextWrapped("%s", graphics.status.c_str());
                    return false;
                  }},
  });
  const auto fog_off = [] {
    const auto& volumetric_fog = graphics.entries[VOLUMETRIC_FOG].cvar;
    return volumetric_fog.values[0] != nullptr && volumetric_fog.Get() == 0.f;
  };
  for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
    const auto& setting = GRAPHICS_SETTINGS[index];
    const bool fog_cell = std::wstring_view(setting.name).starts_with(L"r.VolumetricFog.");
    settings->push_back(talesofarise::graphics_table::Row(setting, &graphics.entries[index], GRAPHICS_SECTION, setting.group,
                                                          fog_cell ? std::function<bool()>(fog_off) : std::function<bool()>()));
    // Not a cvar (contact_shadows.hpp): after the shadows' settings.
    if (index + 1 < GRAPHICS_SETTINGS.size() && std::string_view(setting.group) == "Shadow quality"
        && std::string_view(GRAPHICS_SETTINGS[index + 1].group) != "Shadow quality") {
      settings->push_back(new Setting{
          .key = "ContactShadowsLength",
          .binding = &contact_shadows_row,
          .default_value = 0.f,
          .label = "Contact Shadows (Experimental)",
          .section = "Shadow quality",
          .tooltip = "Unreal's own short sun shadows where things meet the ground and in small gaps (screen-space), a share\n"
                     "of the screen. Longer: shadows reach farther from contact points; can show halos at the screen's\n"
                     "edges. 0: off. Off while ray-traced sun shadows are on. Max preset: 3%.",
          .max = 10.f,
          .format = "%.1f%%",
      });
    }
  }
}

// After the tonemapper's draw (the output size, before the UI): the ray tracing's Debug View over its target
// (rt_scene::OnTonemapDrawn, as the Total War games' tone mapper hooks), then frame generation's HUD-less image from it
// with the frame's inputs (twattila/frame_generation.hpp KeepFrame: the helper presents).
constexpr auto OnTonemapper = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!ray_tracing_installed && !twattila::frame_generation::Wanted()) return {};
  return {.post_callback = [](Context& command, const void*) {
    if (ray_tracing_installed) twattila::rt_scene::OnTonemapDrawn(command.cmd_list);
    if (!twattila::frame_generation::Wanted()) return;
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    if (context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    ComPtr<ID3D11RenderTargetView> target_view;
    context->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    if (!target_view) return;
    ComPtr<ID3D11Resource> resource;
    target_view->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> target;
    if (FAILED(resource.As(&target))) return;
    uint64_t frame;
    {
      const std::shared_lock lock(state.mutex);
      frame = state.frame;
    }
    twattila::frame_generation::Inputs inputs;
    {
      const std::shared_lock lock(frame_generation_frame.mutex);
      if (frame_generation_frame.frame != frame) return;
      inputs = frame_generation_frame.inputs;
    }
    twattila::frame_generation::KeepFrame(context, target.Get(), inputs);
  }};
};

inline bool Install() {
  if (attached) return true;
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::present>(expeditionsrome::dlss::temporal_capture::OnPresent);
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
  // Installed when an effect is on at launch (restart to turn the first one on) and the helper is there to trace it.
  ray_tracing_installed = twattila::dlaa_bridge::HelperInstalled()
                          && (rt_sliders.sun_shadows != 0.f || rt_sliders.ambient_occlusion != 0.f || rt_sliders.light_bounce != 0.f
                              || rt_sliders.reflections != 0.f);
  if (ray_tracing_installed) expeditionsrome::dlss::ray_tracing::Install();
  renodx::utils::command_action::Register(expeditionsrome::dlss::contact_shadows::OnLightRectangle, {
                                                                                                        .shader_hash = expeditionsrome::dlss::contact_shadows::LIGHT_RECTANGLE_SHADER,
                                                                                                        .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                                                                    });
  for (size_t index = 0; index < JITTER_POSITION_LOADS.size(); ++index) {
    auto& patch = jitter_position_patches[index];
    patch = {};
    if (uint8_t* site = talesofarise::code_patch::FindPattern(JITTER_POSITION_LOADS[index], &patch.details); site != nullptr) {
      patch.address = site + SIGNED_LOAD_OPCODE;
      patch.original = {0xBE};
      patch.replacement = {0xB6};
    }
    if (talesofarise::code_patch::ApplyCodePatch(&patch, true)) {
      renodx::utils::log::i("[Rome DLAA jitter] Jitter position read unsigned ", patch.details, ".");
    } else {
      renodx::utils::log::w("[Rome DLAA jitter] Jitter position load ", index, " not fixed: ", patch.details, ".");
    }
  }
  halton_jitter = {};
  if (uint8_t* site = talesofarise::code_patch::FindPattern(JITTER_STORE, &halton_jitter.patch.details); site != nullptr) {
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
  // The ray tracing's Debug View and frame generation's HUD-less image (the HDR part installs twattila/frame_generation.hpp
  // with its swap chain shader).
  for (const uint32_t shader_hash : TONEMAPPER_SHADERS) {
    renodx::utils::command_action::Register(OnTonemapper, {
                                                              .shader_hash = shader_hash,
                                                              .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                          });
  }
  talesofarise::dlss::constants::log_tag = "[Rome AA constants]";
  talesofarise::dlss::constants::Use(DLL_PROCESS_ATTACH);
  attached = true;
  renodx::utils::log::i("[Rome DLAA] The helper's DLAA / DLSS, FSR and XeSS at the game's TAA (ten Ultra/Fast compute hashes; exact/4/8-pixel allocations), frame generation, Ray Reconstruction; live render scale; graphics settings.");
  return true;
}

inline void Uninstall() {
  if (attached) {
    renodx::utils::command_action::Unregister(OnCommand);
    renodx::utils::command_action::Unregister(OnTonemapper);
    renodx::utils::command_action::Unregister(expeditionsrome::dlss::temporal_capture::OnCommand);
    renodx::utils::command_action::Unregister(expeditionsrome::dlss::contact_shadows::OnLightRectangle);
    if (ray_tracing_installed) expeditionsrome::dlss::ray_tracing::Uninstall();
    talesofarise::frame_trace::Stop();
    talesofarise::dlss::constants::Use(DLL_PROCESS_DETACH);
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
    for (auto& patch : jitter_position_patches) talesofarise::code_patch::ApplyCodePatch(&patch, false);
    talesofarise::code_patch::ApplyCodePatch(&halton_jitter.patch, false);
    Reset();
    attached = false;
  }
  twattila::dlaa_bridge::Shutdown();
}

}  // namespace expeditionsrome::dlss_graphics
