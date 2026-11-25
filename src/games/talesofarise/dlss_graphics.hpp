/*
 * SPDX-License-Identifier: MIT
 */

// The former DLSS add-on (renodx-talesofarise_dlss.addon64, UE4 merge round 2026-11-15): DLAA / DLSS (with Ray
// Reconstruction), FSR or XeSS at Unreal's TAA and frame generation, all through the Total War helper
// (talesofarise_dlss/upscaling.hpp: renodx-tw-dlaa64.exe next to the add-on; without it the game's TAA), the live render
// scale, the camera jitter, ray tracing (in-process D3D12: rt_shadows.hpp) and the graphics settings, part of the HDR
// add-on (addon.cpp: Install after its shader mod, Uninstall first). Its history: README_DLSS.md.
#pragma once

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
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../utils/command_action.hpp"
#include "../../utils/command_action_fast.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../../utils/state.hpp"
#include "../talesofarise_dlss/graphics_table.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/pass_timing.hpp"
#include "../twattila/lighting_settings.hpp"
#include "./hdr_only.hpp"
#include "../talesofarise_dlss/constant_mirror.hpp"
#include "../talesofarise_dlss/constant_readback.hpp"
#include "../talesofarise_dlss/code_patch.hpp"
#include "../talesofarise_dlss/engine_cvars.hpp"
#include "../talesofarise_dlss/engine_scale.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/performance.hpp"
#include "../talesofarise_dlss/frame_trace.hpp"
#include "../talesofarise_dlss/pixel_trace.hpp"
#include "../talesofarise_dlss/engine_hooks.hpp"
#include "../talesofarise_dlss/post_filters.hpp"
#include "../talesofarise_dlss/rt_shadows.hpp"
#include "../talesofarise_dlss/ue_light_bounce.hpp"
#include "../talesofarise_dlss/ue_screen_space_shadows.hpp"
#include "../talesofarise_dlss/upscaling.hpp"
#include "./ray_tracing.hpp"
#include "../twattila/frame_generation.hpp"

namespace talesofarise::dlss_graphics {

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
  float live_percentage = 0;  // render scale before it is snapped to the output's aspect ratio; Custom uses its slider
};

constexpr std::array<ModeSettings, 8> AA_MODES = {{
    {.label = "Off (game TAA)", .name = "Off", .minimum_scale = 0, .maximum_scale = 0},
    {.label = "Native AA (100%)", .name = "Native AA", .minimum_scale = 1, .maximum_scale = 1, .live_percentage = 100},
    {.label = "Quality (67%)", .name = "Quality", .minimum_scale = .64f, .maximum_scale = .70f, .live_percentage = 200.f / 3.f},
    {.label = "Balanced (58%)", .name = "Balanced", .minimum_scale = .56f, .maximum_scale = .60f, .live_percentage = 58},
    {.label = "Performance (50%)", .name = "Performance", .minimum_scale = .50f, .maximum_scale = .52f, .live_percentage = 50},
    {.label = "Custom (1-100%)", .name = "Custom", .minimum_scale = .01f, .maximum_scale = 1},
    {.label = "Ultra Performance (33%)", .name = "Ultra Performance", .minimum_scale = .32f, .maximum_scale = .35f, .live_percentage = 100.f / 3.f},
    // Not an NVIDIA SDK mode: the common 77%.
    {.label = "Ultra Quality (77%)", .name = "Ultra Quality", .minimum_scale = .74f, .maximum_scale = .80f, .live_percentage = 77},
}};

// UE 4.21 main-scene TAA compute passes at the render size: t0 depth, t1 scene color, t2 history, t3 velocity,
// u0 output (render size too); 8x8 groups over the view. 0xE2AF861D is 0x88765764's twin (same contract).
constexpr std::array<uint32_t, 2> TEMPORAL_SHADERS = {0x88765764u, 0xE2AF861Du};

// DLSS upscaling: the game has no temporal upscaling. After its TAA, motion blur and the tonemapper run at the render
// size, then a spatial upscale (Catmull-Rom) brings the image to the output size. With DLSS, each motion blur and
// tonemapper draw is repeated at the output size, reading DLSS's output instead of the TAA's (and the output-size
// copies of the earlier passes), into an output-size copy of its target. The game's own draw still runs, so every
// other pass keeps valid render-size inputs. The upscale then reads the tonemapper's output-size copy at 1:1: a copy.
// Motion blur: the HDR addon's 12 permutations; tonemapper: its 20 (both sample their inputs by UV).
constexpr std::array<uint32_t, 32> POST_SHADERS = {
    0x87FBD5AEu, 0x0C04E57Au, 0x14168103u, 0x15EED702u, 0x361FB737u, 0x3A6B3A7Bu, 0x521A2C2Au, 0x5622A37Fu,
    0x579514E7u, 0x5D770744u, 0x68A6E6C5u, 0x78A0D210u,
    0xDCFDF14Du, 0x8D5C5C52u, 0xC89B89AFu, 0x7C1D6772u, 0x283D10A8u, 0x94099C09u, 0xADA0697Au, 0x222F0E81u,
    0x5113B408u, 0x1CB60A72u, 0x82A4BA46u, 0xBC89DFD2u, 0xC0DF59F9u, 0x0DF83FCEu, 0x167CE09Du, 0x44994D1Cu,
    0x757DCFEFu, 0xD3DC1A67u, 0xD936FC2Bu, 0x0867B746u};
constexpr auto TONEMAPPER_SHADERS = std::span(POST_SHADERS).subspan(12);
// UE's Catmull-Rom upscale (r.Upscale.Quality 3): t0 the tonemapper's output, cb0[0] its size and inverse size.
constexpr uint32_t UPSCALE_SHADER = 0x57192B01u;

// Constants read at the TAA dispatch: its own buffer (b0, 52 vectors) and UE 4.21's View (b1): ClipToPrevClip
// 114..117, TemporalAAJitter 118 (NDC), ViewSizeAndInvSize 122.
constexpr size_t TAA_VECTORS = 52;
constexpr size_t VIEW_VECTORS = 124;
constexpr uint32_t VIEW_TRANSLATED_WORLD_TO_VIEW = 8;
constexpr uint32_t VIEW_VIEW_TO_CLIP = 24;
constexpr uint32_t VIEW_VIEW_TO_CLIP_NO_AA = 28;
constexpr uint32_t VIEW_FORWARD = 52;
constexpr uint32_t VIEW_UP = 53;
constexpr uint32_t VIEW_RIGHT = 54;
constexpr uint32_t VIEW_TRANSLATED_CAMERA_ORIGIN = 60;
constexpr uint32_t VIEW_PRE_VIEW_TRANSLATION = 62;
constexpr uint32_t VIEW_CLIP_TO_PREV_CLIP = 114;
constexpr uint32_t VIEW_TEMPORAL_AA_JITTER = 118;
// UE's world size (centimetres): frame generation's far plane (the projection's is at infinity).
constexpr float FAR_PLANE = 2097152.f;
constexpr uint32_t VIEW_SIZE_AND_INV_SIZE = 122;
// View.MaterialTextureMipBias (.x): the bias of every material texture sample (SampleBias with cb1[133].x in 774 of the
// dumped pixel shaders' samples, cb0[133].x in 53). The game leaves it 0 (UE sets it only for its temporal upscaling).
constexpr uint32_t VIEW_MATERIAL_TEXTURE_MIP_BIAS = 133;

// Texture LOD bias while DLSS renders below 100%: 0 the game's (none), 1 matched (log2(input / output)), 2 one step
// sharper (NVIDIA's), 3 two steps sharper.
int texture_lod_bias = 2;
// Bandai's sharpening before the TAA (HDR part, post_filters.hpp): off under DLAA / DLSS unless kept.
bool keep_game_sharpening = false;
// Spark trails fix (the input preparation's particle mask, 0x7A1E0D01): pixels with small bright particles, now or last
// frame, get no DLSS history.
std::atomic<bool> spark_fix = true;
// Bandai's ambient occlusion (HDR part, post_filters.hpp; the page's row AmbientOcclusionHalo): 0 the game's, 1 reduced
// light ring around characters and objects, 2 the same + darker (ATN's pick of the tests: samples and blur cutoffs x 1/4).
float ambient_occlusion = 1.f;
// Ambient occlusion GT-VBAO in place of the game's (HDR part's bitmask_ao.hlsli, post_filters.hpp; the page's ssao_*
// rows, hdr_only.hpp): 0 the game's, 1 GT-VBAO; its radius and thickness (metres), strength (an exponent), 2^directions
// and 8 x 2^quality samples.
float ao_method = 0.f;
float ao_radius = 1.5f;
float ao_strength = 1.f;
float ao_thickness = 0.1f;
float ao_directions = 1.f;
float ao_quality = 1.f;
float ao_auto_thickness = 1.f;
float ao_exact_depth = 0.f;
uint32_t ao_frame = 0;
talesofarise::dlss::constants::ViewMipBias applied_mip_bias;  // the one the constant mirror writes

// The page's rows (Graphics 1 of 2: graphics_page.hpp, Upscaling and Tools), read into the state above at each present.
twattila::feature_settings::AaSliders aa_sliders;
twattila::rt_scene::Sliders rt_sliders;
// Bend's screen-space shadows and GT-VBGI in place of Bandai's contact shadows' march toward the sun and its screen-space
// GI (talesofarise_dlss/ue_screen_space_shadows.hpp, ue_light_bounce.hpp); both off by default. GI's Strength is also
// the rays' bounce light's.
twattila::feature_settings::SssSliders screen_space_shadows_rows;
talesofarise::ue_light_bounce::Rows gi_rows;
// Under our GI, Bandai's GI add keeps its share through the open sky (shared.h TOA_GAME_GI_SHARE, SI.gtvbgi 2).
float game_gi_open_sky_row = 1.f;
float live_scale_row = 1.f;
float camera_jitter_row = 1.f;
float replace_taa_row = 1.f;
float spark_fix_row = 1.f;
float game_sharpening_row = 0.f;
float character_shadows_row = 1.f;
float path_samples_row = 1.f;
float path_reorder_row = 0.f;
// Ray Tracing in the Helper (ray_tracing.hpp; a restart: its scene's shaders are read as the game makes them), and whether
// it was installed at launch (on, with the helper and a sun shadow or AO row on).
float helper_rays_row = 0.f;
bool helper_rays_installed = false;

struct Texture {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

// The helper's inputs, output and Ray Reconstruction's guides are the bridge's textures (the input preparation and the guide
// shader write them); its output is merged with the stock TAA's alpha (`merged`), or downsampled into the game's output.
struct Runtime {
  talesofarise::dlss::ConstantReadback constant_readback;
  talesofarise::dlss::Performance performance;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> prepare_shader;
  ComPtr<ID3D11ComputeShader> merge_shader;
  ComPtr<ID3D11ComputeShader> downsample_shader;
  ComPtr<ID3D11Buffer> prepare_constants;
  ComPtr<ID3D11Buffer> downsample_constants;
  ComPtr<ID3D11SamplerState> linear_sampler;
  Texture merged;
  // Spark trails fix: this frame's particle mask and the last one's (they alternate).
  std::array<Texture, 2> particle_masks;
  // Ray Reconstruction's guides (0x7A1E0D31) into the bridge's, at the render size.
  ComPtr<ID3D11ComputeShader> guide_shader;
  ComPtr<ID3D11Buffer> guide_constants;
  uint32_t particle_mask_index = 0;
  bool particle_mask_valid = false;
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
  // The main view's input size for the AA mode (the output size for DLAA; 0: none yet). TAA passes of other sizes
  // (scene captures) and frames before a new render scale applies are left to the game.
  uint32_t expected_width = 0;
  uint32_t expected_height = 0;
  uint32_t temporal_shader_hash = 0;
  AAMode last_mode = AAMode::OFF;
  talesofarise::dlss::PerformanceSummary performance;
  std::string status = "Off — original game TAA";
  std::string constants;
  uint64_t copied_bytes = 0;
  uint64_t copied_per_frame = 0;
  uint64_t biased_uploads = 0;
  uint64_t biased_per_frame = 0;
  // The widest monitor mode (physical pixels): the game may grow its window to it after settings were written for a
  // smaller one (startup: 1280x720, then the screen size).
  uint32_t widest_display = 0;
  // UE's separate translucency composited before the TAA this frame (Spark trails fix).
  ComPtr<ID3D11ShaderResourceView> translucency;
  uint64_t translucency_frame = UINT64_MAX;
} state;

// Spark motion: the game's sprite particle vertex shaders and their motion versions made from the decompiles
// (0x7A1E0D20-27, tools\toa\spark_motion\make_motion_vs.py; the same placement as the game's on WARP, compare_test.cpp).
constexpr std::array<std::pair<uint32_t, std::span<const uint8_t>>, 8> SPRITE_SHADERS = {{
    {0x3147D98Au, __0x7A1E0D20},
    {0x691BCF53u, __0x7A1E0D21},
    {0x75139A1Bu, __0x7A1E0D22},
    {0x75BBCA50u, __0x7A1E0D23},
    {0xAE463873u, __0x7A1E0D24},
    {0xD7B928CDu, __0x7A1E0D25},
    {0xE1234AB3u, __0x7A1E0D26},
    {0xF885F191u, __0x7A1E0D27},
}};

// The sprites' own motion this frame (RGBA16F at the separate translucency's size: render pixels to last frame, alpha
// 1 where a sprite was drawn), cleared by the frame's first sprite.
struct SparkMotion {
  std::shared_mutex mutex;
  std::array<ComPtr<ID3D11VertexShader>, SPRITE_SHADERS.size()> vertex_shaders;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11ShaderResourceView> srv;
  uint64_t frame = UINT64_MAX;
} spark_motion;

// Ray Reconstruction: UE's G-buffer A / B / C (normal; metallic, specular, roughness; base colour), which Bandai's
// lighting pass (its 6 permutations) reads at t1-t3, copied at its first draw each frame for the guides.
constexpr std::array<uint32_t, 6> LIGHTING_SHADERS = {0xF2A6E883u, 0x6513DC8Eu, 0xC28D2F1Eu, 0xB2726B44u, 0x81E3CED2u, 0xF95FABAAu};
// Bandai's toon lighting (characters, foliage: the HDR part's toon00-09), drawn before lighting00-05 with the same
// G-buffer at t1-t3, the base pass's light at t7 (lighting00-05: t4): our AO and GI bound for them too.
constexpr std::array<uint32_t, 10> TOON_SHADERS = {0x0A9D482Au, 0xDF1B494Du, 0xF6334BA2u, 0x1C8D11B6u, 0x36E08F9Au,
                                                   0x1FE8B2B6u, 0x4E695056u, 0x4E8218FEu, 0x8CCA5153u, 0x90EF2450u};
struct GBufferCopy {
  std::shared_mutex mutex;
  std::array<ComPtr<ID3D11Texture2D>, 3> textures;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> srvs;
  uint64_t frame = UINT64_MAX;
} gbuffer;

// Bend's and GT-VBGI's settings from the page's rows (each present) for the sun shadow mask and the lighting pass, and
// this frame's GT-VBGI (it runs at the frame's first lighting draw, the others reuse it); the last frame the rays' bounce
// light or path tracing reached the lighting pass (Bandai's GI add stays out while it did recently).
struct UeEffects {
  std::shared_mutex mutex;
  twattila::screen_space_shadows::Settings shadows;
  LightBounceConstants bounce = {};
  bool denoise = false;
  float render_share = 1.f;
  uint64_t bounce_frame = UINT64_MAX;
  ID3D11ShaderResourceView* bounce_view = nullptr;
  uint64_t rays_bounce_frame = UINT64_MAX;
} ue_effects;

// Frame generation's inputs from this frame's upscaling (KeepFrame after the tone mapper), around the camera (UE's
// translated world); the last frame's view-projection and translation (its camera's step moves it).
struct FrameGenerationFrame {
  std::shared_mutex mutex;
  twattila::frame_generation::Inputs inputs;
  uint64_t frame = UINT64_MAX;
  std::array<float, 16> previous_view_projection = {};
  std::array<float, 3> previous_translation = {};
  bool previous_kept = false;
} frame_generation_frame;

std::atomic<AAMode> requested_mode = AAMode::OFF;
std::atomic<uint32_t> preset = 13u;  // NVSDK_NGX_DLSS_Hint_Render_Preset M
std::atomic<float> fsr_sharpness = 0.f;  // FSR's RCAS, 0-1 (0: off)
// The page's upscaler (dlaa_bridge::UPSCALER_*) and its name.
std::atomic<uint32_t> requested_upscaler = twattila::dlaa_bridge::UPSCALER_DLSS;
constexpr std::array<const char*, 3> UPSCALER_NAMES = {"DLSS", "FSR", "XeSS"};
bool weak_cpu = false;  // presets leave CPU_HEAVY_SETTINGS at the game's
bool far_sun_shadows = false;  // presets set FAR_SUN_SHADOWS
// Taste settings under the preset toggles, in no preset (ATN): engine_hooks SMOOTH_SUN and DAY_SPEED.
bool smooth_sun = false;  // config SmoothSun (recommended, off by default: ATN)
constexpr std::array<float, 14> DAY_SPEEDS = {0.f, .001f, .01f, .1f, 1.f / 6.f, 1.f / 3.f, .5f, 1.f, 2.f, 3.f, 6.f, 10.f, 100.f, 1000.f};
constexpr std::array<const char*, DAY_SPEEDS.size()> DAY_SPEED_LABELS = {
    "Off (time stands still)", "1000x slower", "100x slower", "10x slower", "6x slower", "3x slower", "2x slower",
    "Game's speed", "2x faster", "3x faster", "6x faster", "10x faster", "100x faster", "1000x faster"};
constexpr int GAME_DAY_SPEED = 7;
int day_speed = GAME_DAY_SPEED;  // config DaySpeedMultiplier: DAY_SPEEDS[day_speed]

// The taste settings into the engine hooks (an unavailable one stays at the game's).
void ApplyTasteSettings() {
  using talesofarise::engine_hooks::options;
  if (auto& sun = options[talesofarise::engine_hooks::SMOOTH_SUN]; sun.available) {
    std::atomic_ref(sun.value).store(smooth_sun ? 1.f : sun.game, std::memory_order_relaxed);
  }
  if (auto& speed = options[talesofarise::engine_hooks::DAY_SPEED]; speed.available) {
    std::atomic_ref(speed.value).store(DAY_SPEEDS[day_speed], std::memory_order_relaxed);
  }
}
bool max_volumetric_fog = false;  // presets set MAX_VOLUMETRIC_FOG
std::array<float, 2> traced_pixel = {50.f, 10.f};  // the page's: % of the screen's width and height (pixel_trace.hpp)
std::string saved_image;                           // the page's: the last code save's result
// Skip the stock TAA dispatch once constants are known before it (CPU copy trusted).
std::atomic_bool replace_taa = true;
// Active input size the temporal pass last received (width | height << 32), even when
// rejected, and how many temporal passes were seen.
std::atomic<uint64_t> seen_input = 0;
std::atomic<uint64_t> seen_frames = 0;
// Present (+1) at which the page's Upscaling Details were last drawn; timings are measured only then.
std::atomic<uint64_t> overlay_frame = 0;

// Upscaling Details drawn within the last 120 presents of `frame`: its timings and counters run only then (no per-frame
// counters otherwise).
inline bool DetailsShown(uint64_t frame) {
  const uint64_t overlay = overlay_frame.load(std::memory_order_relaxed);
  return overlay != 0 && frame + 1 - overlay < 120;
}

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

// Output-size post-processing for DLSS upscaling (POST_SHADERS, UPSCALE_SHADER).
struct PostTarget {
  ComPtr<ID3D11Resource> game;       // the game's render-size target
  ComPtr<ID3D11Texture2D> resource;  // its output-size copy
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11RenderTargetView> rtv;
  uint64_t frame = UINT64_MAX;  // drawn in this frame
};
struct Post {
  std::shared_mutex mutex;
  uint64_t frame = UINT64_MAX;  // the frame DLSS's output belongs to
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  ComPtr<ID3D11Resource> taa_output;        // the game's
  ComPtr<ID3D11ShaderResourceView> output;  // DLSS's
  std::array<PostTarget, 4> targets;
  size_t next_target = 0;
  ComPtr<ID3D11Buffer> upscale_constants;  // the upscale's source size and inverse: the output size
  // Output-size draws and replaced upscales: this frame's, the last frame's (Upscaling Details).
  uint32_t passes = 0;
  uint32_t upscales = 0;
  std::array<uint32_t, 2> last = {};
} post;

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
// UE pads its render-size buffers to multiples of 4 (8 in later versions: switched to once seen). DLSS render sizes
// are whole units of the output's aspect ratio in those multiples: the view fills its buffer (the output-size
// post-processing samples the buffer's UVs) and both axes get exactly the same scale (no custom-scale shimmer).
std::atomic<uint32_t> buffer_multiple = 4;


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
// DLAA / DLSS use NVIDIA's pattern instead: Halton (2, 3) over the whole pixel, 8 * (output / input)^2 positions (8 for
// DLAA; UE's is a Gaussian inside a circle, denser in the middle, r.TemporalAASamples positions, and it keeps the
// position in a signed byte: at most 127, too few below 25%). The skipped multiply becomes two loads of the addon's sample, written every present
// next to the code (rip-relative reach), and the jmp's displacement 0x12 becomes 0 (it then falls through):
//   movss xmm7, [rip + x]; movss xmm8, [rip + y]; nop
// The multiply is rewritten only while the jmp skips it; the jmp itself changes one byte. UE's projection jitter and
// TemporalJitterPixels are computed from xmm7 / xmm8 after it, so DLSS reads the same jitter from the View as always.
constexpr size_t JITTER_SCALE_MULTIPLY = 10;
constexpr size_t JITTER_SCALE_MULTIPLY_SIZE = 18;
struct CameraJitter {
  talesofarise::code_patch::CodePatch patch;
  talesofarise::code_patch::CodePatch halton_multiply;
  talesofarise::code_patch::CodePatch halton_branch;
  float* halton_sample = nullptr;  // x, y in input pixels
  uint32_t halton_positions = 0;
  uint32_t halton_index = 0;
  bool searched = false;
  bool enabled = true;
} camera_jitter;



// Graphics presets: engine settings written live (engine_cvars.hpp) while a preset is chosen; "Game's own" puts the
// game's values back. A preset only raises quality: higher-is-better settings take the larger of the game's value
// and the preset's, lower-is-better ones (LOD scales, thresholds, cell sizes) the smaller. KEEP: the game's value.
// The table's types and rows: talesofarise_dlss/graphics_table.hpp (values: High, Ultra, Extreme, Maximum, then one
// extra choice offered only in the setting's own list).
using talesofarise::graphics_table::Better;
using talesofarise::graphics_table::KEEP;
using GraphicsSetting = talesofarise::graphics_table::Setting;
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
// Only cost CPU time; with "Vanilla crowd limits" (config WeakCPU) every preset leaves them at the game's (ATN: keeping characters behind the camera
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
// In no preset (the most expensive effect); with "Max volumetric fog" every preset sets them (ATN: with DLSS the
// finest volumetric fog is affordable and visible): Bandai's lighting cells 2 (its volume has 16 slices: 1920 x 1080
// x 16 at 4K), Unreal's fog grid at its own Cinematic 4 (with 128 slices; 2 and 256 would be GBs per volume at 4K).
// The cell sizes stay within D3D11's volume limit (VOLUME_SETTINGS).
constexpr std::array<PresetOverride, 2> MAX_VOLUMETRIC_FOG = {{
    {.name = L"r.VolumetricLightingBufferPixelNumPerCell", .value = 2.f},
    {.name = L"r.VolumetricFog.GridPixelSize", .value = 4.f},
}};
// Arise-SDK's ideas (ATN: they need Arise-SDK, as its author prefers): without it the group is greyed out (presets skip
// it); with it, a row sets Arise-SDK's live sdk.* setting where it has one, else this addon's code (NPC distance: its
// ini's MinimumNPCDistance -1 leaves it to the addon) or Unreal's cvar (a.URO.Enable). The hidden ReShade.ini key
// [Tales AA] DebugDevArise=1 (ATN's own testing) uses this addon's code for the whole group, as before v89.
constexpr const char* ARISE_SDK_GROUP = "Arise-SDK's settings";
bool debug_dev_arise = false;  // config DebugDevArise, never written by the addon
// Sun shadow distance above 2 is Arise-SDK's idea too (no limit): without Arise-SDK (and DebugDevArise) the game's
// limit stays (engine_hooks.hpp's patch undone, choices above it gone) and "Far sun shadows" is greyed out.
constexpr float SUN_DISTANCE_GAME_LIMIT = 2.f;
bool sun_distance_limited = false;  // set at the console search
constexpr std::array<GraphicsSetting, 68> GRAPHICS_SETTINGS = {{
    // Arise-SDK's ideas, in one group, first (ATN: right under the note about Arise-SDK). They need it (greyed out
    // without it); the rows set its live sdk.* settings (engine_hooks.hpp: arise_sdk_cvar).
    // Settings without a console variable: the addon's own values, read by its hooks in the game's code. Maximum: ATN's
    // values.
    {ARISE_SDK_GROUP, L"Tales.NPCDistance", "NPC distance", Better::HIGHER, {10000.f, 50000.f, 200000.f, 500000.f, 750000.f},
     "How far NPCs (townspeople, soldiers) stay spawned and visible (the game: about 3000; all its NPC\n"
     "distances grow with it). Applies after loading an area."},
    {ARISE_SDK_GROUP, L"Tales.MonsterDistance", "Monster distance", Better::HIGHER, {10.f, 100.f, 1000.f, 5000.f, 10000.f},
     "How far monsters spawn and stay (times the game's)."},
    {ARISE_SDK_GROUP, L"Tales.CharacterDetailDistance", "NPC and monster detail distance", Better::HIGHER, {3.f, 10.f, 50.f, 200.f, 1000.f},
     "How far NPCs and monsters keep their full detail (times the game's)."},
    {ARISE_SDK_GROUP, L"Tales.NeverHideCharacters", "Never hide characters", Better::HIGHER, {KEEP, KEEP, 1.f, 1.f, KEEP},
     "1: characters out of view or behind things stay drawn (their sounds keep playing too). Costs\n"
     "performance. Applies after loading an area. Characters behind the camera still fade in unless\n"
     "Keep characters behind the camera is on."},
    {ARISE_SDK_GROUP, L"a.URO.Enable", "Half-rate distant animation (0: off)", Better::LOWER, {0.f, 0.f, 0.f, 0.f, KEEP},
     "0: distant NPCs and monsters animate at the full rate."},
    {ARISE_SDK_GROUP, L"Tales.OutlineDistance", "Cel-shading outline distance", Better::HIGHER, {12000.f, 24000.f, 100000.f, 500000.f, 750000.f},
     "How far the game's outlines and cel-shading reach (the game: 6000)."},
    // Cutscene fixes (engine_hooks.hpp), in every preset; smooth cutscenes in Game's own too (EVERY_PRESET_SETTING).
    {ARISE_SDK_GROUP, L"Tales.SkitResolution", "Skit resolution (% of the screen's pixels)", Better::HIGHER, {100.f, 100.f, 100.f, 200.f, KEEP},
     "Draws the skits' panels with as many pixels as the screen (times this): sharp instead of blurry, and\n"
     "anti-aliased.",
     {50.f, KEEP, KEEP}},
    {ARISE_SDK_GROUP, L"Tales.SmoothCutscenes", "Smooth cutscenes", Better::HIGHER, {1.f, 1.f, 1.f, 1.f, KEEP},
     "1: cutscenes play at your frame rate instead of in steps. Applies to cutscenes loaded afterwards."},
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
    // Bandai's PFCullingManager (exe+0xA0C230, read every frame) hides characters outside the view; turning back fades
    // them in and rebuilds them (ATN: an fps drop for 1-2 s). 1 counts every one as inside the view (Never hide
    // characters covers another system).
    {"Characters", L"Arise.PF.Culling.ForceInFrustum", "Keep characters behind the camera", Better::HIGHER, {1.f, 1.f, 1.f, 1.f, KEEP},
     "1: characters behind the camera stay, so turning around does not fade them in (with an fps drop).\n"
     "Costs CPU time (Vanilla crowd limits under the presets)."},
    // The sun's shadow map lives in an 8192 x 8192 atlas: above 8192 the game finds no shadow depth target and crashes
    // (exe+0x1A9E56C). Texels per pixel 4, written live, crashed there too (a null shadow atlas): no choice goes above 3. Pre-shadows stay at 1: above it a close-up's
    // pre-shadow can outgrow the shadow atlas. ShadowQuality 5, 10 cascades and MotionBlurQuality 4 are Unreal's limits.
    {"Shadow quality", L"r.Shadow.MaxResolution", "Light shadow resolution (point / spot)", Better::HIGHER, {4096.f, 4096.f, 8192.f, 8192.f, KEEP},
     "Shadow sharpness of point and spot lights."},
    {"Shadow quality", L"r.ShadowQuality", "Shadow filtering quality", Better::HIGHER, {KEEP, 4.f, 5.f, 5.f, KEEP},
     "Smoothing of shadow edges."},
    {"Shadow quality", L"r.CapsuleShadowsFullResolution", "Character soft shadows at full resolution", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "1: the soft shadows characters cast from their capsules (around their feet and body) at full resolution\n"
     "instead of half: sharper."},
    {"Shadow quality", L"r.Shadow.CSM.MaxCascades", "Sun shadow cascades", Better::HIGHER, {4.f, 5.f, 6.f, 10.f, KEEP},
     "Layers of the sun's shadow: more of them keep shadows sharp at every distance."},
    // Test (in no preset): the sun is one shadow map whatever the row above says (DLSS v66): the light's own cascade
    // count (engine_hooks.hpp) raised; the cascades share the 8192 atlas (two to four fit at 4096).
    {"Shadow quality", L"Tales.SunShadowLayers", "Sun shadow layers (test)", Better::HIGHER,
     {KEEP, KEEP, KEEP, KEEP, 2.f},
     "Test: splits the sun's single shadow map into layers, sharp near you and coarser far away.\n"
     "Needs Sun shadow resolution 4096 (2-4 layers fit). May change nothing or break sun shadows.",
     {3.f, 4.f, KEEP}},
    // Sun shadows shimmer when the sun moves (every ~10 s) at distances above 1; finer texels fix it (ATN: 6144 / 8192).
    // The game's sun is one shadow map (every frame trace: one 8184 x 8184 viewport in the 8192 atlas, whatever the
    // cascade count), drawn once and projected by cs 0x24D721C3. 8192 is the most: v65-v67 doubled the atlas size Unreal
    // computes (1 << (GMaxTextureMipCount - 1), exe+0x1AD7041) and tried 16384 and 12288: both crashed at exe+0x1A9E56C
    // (no depth target), as above 8192 without it (ATN): that size is not the limit; the patch was removed.
    {"Shadow quality", L"r.Shadow.MaxCSMResolution", "Sun shadow resolution", Better::HIGHER, {6144.f, 6144.f, 8192.f, 8192.f, KEEP},
     "Sharpness of the sun's shadow (the game: 4096; 8192 is the most it can use). Sharper edges shimmer less\n"
     "when the sun moves."},
    // Above 2 once engine_hooks.hpp removed the game's clamp. Farther spreads the same shadow maps over more ground:
    // shadow edges shimmer more (ATN: above 1).
    {"Shadow quality", L"r.Shadow.DistanceScale", "Sun shadow distance", Better::HIGHER, {KEEP, 1.25f, 1.5f, 2.f, 4.f},
     "How far the sun's shadows reach. Farther: softer edges that shimmer more (higher resolution helps).\n"
     "3 stops sunlight shining through far mountains (above 2: needs Arise-SDK).",
     {2.5f, 3.f, 3.5f}},
    // The sun's light, turned a bit every frame toward the game's steps (engine_hooks.hpp).
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
    {"Shadow quality", L"r.Shadow.MinResolution", "Distant object shadow resolution", Better::HIGHER, {KEEP, KEEP, KEEP, KEEP, 64.f},
     "Least resolution of distant objects' shadows."},
    {"Shadow quality", L"r.Shadow.FadeResolution", "Distant object shadow fade", Better::LOWER, {KEEP, KEEP, KEEP, KEEP, 32.f},
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
    // Found in the exe (not in the game's menu): in no preset; only their lists and Effects at output resolution.
    {"Reflections and GI", L"r.ScreenSpaceReflectionColorDownSampledScreenScaleTO14", "Reflection blur color divisor (Bandai)", Better::LOWER, {KEEP, KEEP, KEEP, KEEP, 1.f},
     "Resolution divisor of the reflected image's blurred copy (1: full resolution)."},
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
    // Bandai's mixed-resolution translucency (from the exe's strings; in no preset): some effects at half the render
    // resolution. Unreal's r.SeparateTranslucencyScreenPercentage does not change them (ATN, v57). v55's startup crash
    // blamed on this row was the cloud volumes (v58).
    {"Fog, clouds and effects", L"r.EnableMixedResolutionTranslucentRenderingTO14", "Half-resolution effects (Bandai)", Better::LOWER, {KEEP, KEEP, KEEP, KEEP, 0.f},
     "1: some effects (smoke, fire, glow) at half resolution, faster. 0: all at full resolution."},
    {"Fog, clouds and effects", L"r.TransulcentUseQuarterSizeDepthTO14", "Quarter-size depth for effects (Bandai)", Better::LOWER, {KEEP, KEEP, KEEP, KEEP, 0.f},
     "1: half-resolution effects are tested against a quarter-size depth, faster. 0: full size."},
    {"Fog, clouds and effects", L"r.SSS.HalfRes", "Skin light scattering at half resolution", Better::LOWER, {KEEP, KEEP, KEEP, KEEP, 0.f},
     "1: light scattering in skin at half resolution, faster. 0: full resolution."},
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
     "1: builds an area's shaders while it loads: fewer stutters when new effects first appear.\n"
     "Loading takes a bit longer. Applies after loading a save. 0: the engine's default.",
     {0.f, KEEP, KEEP}},
}};
constexpr size_t SettingIndex(std::wstring_view name) {
  return std::ranges::find(GRAPHICS_SETTINGS, name, [](const GraphicsSetting& setting) { return std::wstring_view(setting.name); })
         - GRAPHICS_SETTINGS.begin();
}
// "Effects at output resolution" (off by default): while DLSS renders below 100%, settings sized in render pixels
// follow the render scale s (render / output), so these effects keep the detail they have at 100% and cost about as
// much: cell sizes and resolution divisors x s (nearest, at least 1), shadow texels per pixel / s (Unreal sizes object,
// spot and point light shadows by their size in render pixels), half-resolution switches at full resolution (1 where
// higher is better, else 0). Sun shadows do not depend on the render scale. On by default (ATN: fog no longer blocky,
// a few fps).
enum class OutputScaling : uint8_t { NONE,
                                     CELL_OR_DIVISOR,
                                     PER_PIXEL,
                                     FULL_RESOLUTION };
struct OutputScaled {
  std::wstring_view name;
  OutputScaling scaling;
};
constexpr std::array<OutputScaled, 21> OUTPUT_SCALED = {{
    {L"r.VolumetricLightingBufferPixelNumPerCell", OutputScaling::CELL_OR_DIVISOR},
    {L"r.VolumetricFog.GridPixelSize", OutputScaling::CELL_OR_DIVISOR},
    {L"r.ScreenSpaceReflectionScreenScaleTO14", OutputScaling::CELL_OR_DIVISOR},
    {L"r.ScreenSpaceReflectionColorScreenScaleTO14", OutputScaling::CELL_OR_DIVISOR},
    {L"r.ScreenSpaceReflectionColorDownSampledScreenScaleTO14", OutputScaling::CELL_OR_DIVISOR},
    {L"r.TranslucnetSSRDepthBufferScreenScaleTO14", OutputScaling::CELL_OR_DIVISOR},
    {L"r.DenseGroundFogScreenScaleTO14", OutputScaling::CELL_OR_DIVISOR},
    {L"r.VolumetricCloudScreenScaleTO14", OutputScaling::CELL_OR_DIVISOR},
    {L"r.VolumetricCloudShadowScreenScaleTO14", OutputScaling::CELL_OR_DIVISOR},
    {L"r.VolumetricCloudSimpleRayMarchScreenScaleTO14", OutputScaling::CELL_OR_DIVISOR},
    {L"r.LightShaftDownSampleFactor", OutputScaling::CELL_OR_DIVISOR},
    {L"r.Shadow.TexelsPerPixel", OutputScaling::PER_PIXEL},
    {L"r.Shadow.TexelsPerPixelSpotlight", OutputScaling::PER_PIXEL},
    {L"r.Shadow.TexelsPerPixelPointlight", OutputScaling::PER_PIXEL},
    {L"r.CapsuleShadowsFullResolution", OutputScaling::FULL_RESOLUTION},
    {L"r.SpearateTransulcentUseDownSampledDepthTO14", OutputScaling::FULL_RESOLUTION},
    {L"r.UseHighResSSGITO14", OutputScaling::FULL_RESOLUTION},
    {L"r.UseDownSampleColorBufferForSSGITO14", OutputScaling::FULL_RESOLUTION},
    {L"r.EnableMixedResolutionTranslucentRenderingTO14", OutputScaling::FULL_RESOLUTION},
    {L"r.TransulcentUseQuarterSizeDepthTO14", OutputScaling::FULL_RESOLUTION},
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
// r.VolumetricFog 0 greys out the r.VolumetricFog.* settings.
constexpr size_t VOLUMETRIC_FOG = SettingIndex(L"r.VolumetricFog");
// Off while the ray-traced reflections run (rt_shadows.hpp ReflectingRecently): they replace it.
constexpr size_t SSR_QUALITY = SettingIndex(L"r.SSR.Quality");
// The game's menu Motion Blur sets r.MotionBlurQuality 0 (off) or 4 (exe 0xE6C370): a preset raises it only while on.
constexpr size_t MOTION_BLUR_QUALITY = SettingIndex(L"r.MotionBlurQuality");
// Shaders on load: a checkbox above Smooth sun movement (ATN), not in the Loading list: ticked 1, unticked 0.
constexpr size_t SHADERS_ON_LOAD = SettingIndex(NO_PRESET_SETTING);
// Volume textures: D3D11 allows at most 2048 per side. Cloud shadows / cloud ray march at divisor 1 made a 3840x2160x40
// volume at startup (ATN, 4K: Effects at output resolution scaled their 2 to 1; the game sizes them from the full
// output, not the render size), so output width / value stays at most 2048 for every volume setting. Bandai's
// lighting cells and Unreal's fog grid follow the render size (491 x 276 = 2944 / 6 at DLSS Ultra Quality, debug12),
// but the render size can still be the full output while a scale is being applied (v69: a lighting cell of 1 allowed
// by the render-size bound crashed at startup in CreateTexture3D): the output bound stays for them too.
constexpr std::array<size_t, 4> VOLUME_SETTINGS = {SettingIndex(L"r.VolumetricLightingBufferPixelNumPerCell"),
                                                   SettingIndex(L"r.VolumetricFog.GridPixelSize"),
                                                   SettingIndex(L"r.VolumetricCloudShadowScreenScaleTO14"),
                                                   SettingIndex(L"r.VolumetricCloudSimpleRayMarchScreenScaleTO14")};
// Shared with the HDR addon (post_filters.hpp): its edge color in the sun's penumbras follows their width.
constexpr size_t SUN_SHADOW_DISTANCE = SettingIndex(L"r.Shadow.DistanceScale");
static_assert(VOLUMETRIC_FOG < GRAPHICS_SETTINGS.size() && SUN_SHADOW_DISTANCE < GRAPHICS_SETTINGS.size());
// Each setting's own choice (a slider, as Ryse's options): 0 the game's value, else one of its choices (its distinct
// preset values and extras, with steps between them); the preset buttons set every choice. Saved per setting in
// ReShade.ini, section GRAPHICS_SECTION, key the cvar name, by value (SaveChoice): steps move the indexes. (The old
// "Tales Graphics Settings" section held indexes.)
constexpr const char* GRAPHICS_SECTION = "Tales Graphics Values";

using GraphicsEntry = talesofarise::graphics_table::Entry;
struct Graphics {
  talesofarise::engine_cvars::Console console;
  bool searched = false;
  bool available = false;
  std::string status = "Searching the game's settings...";
  std::array<GraphicsEntry, GRAPHICS_SETTINGS.size()> entries;
} graphics;

struct Frame {
  std::unique_ptr<Runtime> runtime;
  ComPtr<ID3D11DeviceContext1> context;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> inputs;
  std::array<ComPtr<ID3D11Texture2D>, 3> resources;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11ShaderResourceView> target_srv;
  ComPtr<ID3D11UnorderedAccessView> target_uav;  // the game's
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

// The mode as the upscaler calls it (DLSS at 100%: DLAA).
inline std::string ModeName(AAMode mode) {
  const uint32_t upscaler = std::min<uint32_t>(requested_upscaler.load(), UPSCALER_NAMES.size() - 1);
  if (mode == AAMode::DLAA && upscaler == twattila::dlaa_bridge::UPSCALER_DLSS) return "DLAA";
  return std::string(UPSCALER_NAMES[upscaler]) + " " + AA_MODES[static_cast<size_t>(mode)].name;
}

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

// The retired runtime goes outside the module lock.
void Reset() {
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
    state.translucency.Reset();
    state.translucency_frame = UINT64_MAX;
    diagnostics.camera_cuts.store(0);
    diagnostics.skipped_frames.store(0);
    diagnostics.restarts.store(0);
    retired = std::move(state.runtime);
    state.runtime = std::make_unique<Runtime>();
  }
  {
    const std::unique_lock lock(post.mutex);
    post.frame = UINT64_MAX;
    post.taa_output.Reset();
    post.output.Reset();
    post.targets = {};
    post.upscale_constants.Reset();
  }
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

// Validation, the helper's evaluation and commit. `replace`: before the stock dispatch, which is skipped on success, the
// upscaled frame going into the game's output; otherwise after it, merged with its alpha.
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
      || std::abs(view_size[2] * render_width - 1.f) > 0.0010000911f || std::abs(view_size[3] * render_height - 1.f) > .001f
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
    SetStatus("Game TAA: unsupported scale/layout for " + ModeName(frame->mode) + "; keep the game's Anti-Aliasing on TAA and its render scale at 100%",
              frame->generation);
    return false;
  }
  if (frame->temporal_upscaling && (frame->source_width != frame->render_width || frame->source_height != frame->render_height)
      && buffer_multiple.exchange(8) != 8) {
    renodx::utils::log::i("[Tales DLSS] The game pads ", frame->render_width, "x", frame->render_height, " to ",
                          frame->source_width, "x", frame->source_height, ": render sizes in multiples of 8 from now on.");
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
  // The jitter's cycle only while Upscaling Details is drawn.
  if (const std::array<float, 2> jitter = pixel_jitter; frame->measuring) {
    if (diagnostics.jitter_frames == 0 || diagnostics.jitter_frames > 4096) {
      if (diagnostics.jitter_frames > 4096) {
        diagnostics.jitter_phases.store(0);
      }
      diagnostics.first_jitter = jitter;
      diagnostics.jitter_frames = 1;
    } else if (jitter == diagnostics.first_jitter) {
      diagnostics.jitter_phases.store(diagnostics.jitter_frames);
      diagnostics.jitter_frames = 1;
    } else {
      ++diagnostics.jitter_frames;
    }
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
    auto constant_readback = std::move(runtime.constant_readback);
    auto performance = std::move(runtime.performance);
    runtime = Runtime{};
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
        || FAILED(device->CreateComputeShader(__0xA3450ADB.data(), __0xA3450ADB.size(), nullptr, runtime.merge_shader.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__0x7A1E0D03.data(), __0x7A1E0D03.size(), nullptr, runtime.downsample_shader.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__0x7A1E0D31.data(), __0x7A1E0D31.size(), nullptr, runtime.guide_shader.GetAddressOf()))) {
      SetStatus("Game TAA: private pipeline creation failed", frame->generation);
      return false;
    }
    const D3D11_BUFFER_DESC buffer_desc = {
        .ByteWidth = 96,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    const D3D11_BUFFER_DESC downsample_buffer_desc = {
        .ByteWidth = 16,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    const D3D11_SAMPLER_DESC sampler_desc = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    const D3D11_BUFFER_DESC guide_buffer_desc = {
        .ByteWidth = 64,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, runtime.prepare_constants.GetAddressOf()))
        || FAILED(device->CreateBuffer(&downsample_buffer_desc, nullptr, runtime.downsample_constants.GetAddressOf()))
        || FAILED(device->CreateBuffer(&guide_buffer_desc, nullptr, runtime.guide_constants.GetAddressOf()))
        || FAILED(device->CreateSamplerState(&sampler_desc, runtime.linear_sampler.GetAddressOf()))) {
      SetStatus("Game TAA: input constant-buffer allocation failed", frame->generation);
      return false;
    }
    if (!create_texture(&runtime.particle_masks[0], DXGI_FORMAT_R8_UNORM, frame->render_width, frame->render_height)
        || !create_texture(&runtime.particle_masks[1], DXGI_FORMAT_R8_UNORM, frame->render_width, frame->render_height)) {
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

  // The helper's bridge (upscaling.hpp: the swap chain's size, the output's); none: the game's TAA.
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (!talesofarise::upscaling::PrepareBridge(device.Get())) {
    SetStatus(twattila::dlaa_bridge::HelperInstalled() ? "Game TAA: the helper (renodx-tw-dlaa64.exe) is not ready"
                                                       : "Game TAA: renodx-tw-dlaa64.exe is not next to the add-on (the full download)",
              frame->generation);
    return false;
  }
  if (bridge.width != frame->width || bridge.height != frame->height) {
    SetStatus("Game TAA: the helper's size is not the output's", frame->generation);
    return false;
  }
  // The helper reads and writes RGBA16F: the input preparation copies the game's scene colour (R11G11B10F or not) into the
  // bridge's. DLAA replacing an RGBA16F output gets the helper's output copied; otherwise DLAA merges it into the game's
  // output (with the stock alpha), and DLSS (output size) downsamples it into the game's output when replacing.
  const bool direct = !frame->temporal_upscaling && replace && frame->target_format == DXGI_FORMAT_R16G16B16A16_FLOAT;
  const bool merge = !frame->temporal_upscaling && !direct;
  if (merge && !runtime.merged.resource && !create_texture(&runtime.merged, frame->target_format, frame->width, frame->height)) {
    runtime.merged = {};
    SetStatus("Game TAA: private texture allocation failed", frame->generation);
    return false;
  }
  {
    // Commit only for the current frame and mode. When replacing, the upscaled frame goes into the game's output; a failure
    // lets the stock dispatch run instead.
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
  // UE 4.21's TAA buffer has no camera-cut flag (UE 4.26: b0[0].z); cuts are not detected yet.
  constexpr bool camera_cut = false;
  const bool restart = runtime.last_frame == UINT64_MAX;
  const bool skipped = !restart && frame->frame != runtime.last_frame + 1;
  const bool reset = camera_cut || restart || skipped;
  // The scene's light as it is (linear, before the tone mapper): no square root for any model.
  bridge.color_square_root = false;
  bool evaluated = false;
  bool reconstructed = false;
  twattila::dlaa_bridge::BeforeSharedWrites(context);
  {
    talesofarise::dlss::ContextStateScope restore(context, runtime.private_state.Get());
    runtime.performance.Mark(context);
    ComPtr<ID3D11ShaderResourceView> translucency;
    {
      const std::shared_lock lock(state.mutex);
      if (state.translucency_frame == frame->frame) translucency = state.translucency;
    }
    ComPtr<ID3D11ShaderResourceView> particle_motion;
    if (translucency) {
      const std::shared_lock lock(spark_motion.mutex);
      if (spark_motion.frame == frame->frame) particle_motion = spark_motion.srv;
    }
    struct PrepareConstants {
      std::array<std::array<float, 4>, 4> clip_to_previous;
      uint32_t width;
      uint32_t height;
      std::array<float, 2> pixel_jitter;
      uint32_t copy_color;
      uint32_t particle_mask;
      uint32_t previous_particle_mask_valid;
      uint32_t particle_motion_valid;
    } constants = {
        .width = frame->render_width,
        .height = frame->render_height,
        .pixel_jitter = (frame->temporal_upscaling ? frame->pixel_jitter : std::array<float, 2>{}),
        .copy_color = 1u,
        .particle_mask = (translucency ? 1u : 0u),
        .previous_particle_mask_valid = ((translucency && runtime.particle_mask_valid) ? 1u : 0u),
        .particle_motion_valid = (particle_motion ? 1u : 0u),
    };
    static_assert(sizeof(constants) == 96);
    std::memcpy(constants.clip_to_previous.data(), frame->view[VIEW_CLIP_TO_PREV_CLIP].data(), 64);
    context->UpdateSubresource(runtime.prepare_constants.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11Buffer* buffers[] = {runtime.prepare_constants.Get()};
    const auto& previous_mask = runtime.particle_masks[runtime.particle_mask_index ^ 1u];
    const auto& current_mask = runtime.particle_masks[runtime.particle_mask_index];
    ID3D11ShaderResourceView* prep_inputs[] = {frame->inputs[1].Get(), frame->inputs[2].Get(), frame->inputs[0].Get(),
                                               translucency.Get(), (translucency ? previous_mask.srv.Get() : nullptr),
                                               particle_motion.Get()};
    ID3D11UnorderedAccessView* prep_outputs[] = {bridge.depth.uav.Get(), bridge.motion.uav.Get(), bridge.color.uav.Get(),
                                                 (translucency ? current_mask.uav.Get() : nullptr)};
    context->CSSetShader(runtime.prepare_shader.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, buffers);
    context->CSSetShaderResources(0, 6, prep_inputs);
    context->CSSetUnorderedAccessViews(0, 4, prep_outputs, nullptr);
    context->Dispatch((frame->render_width + 7) / 8, (frame->render_height + 7) / 8, 1);
    runtime.performance.Mark(context);
    // The helper reads these outputs next (cheaper than ClearState).
    constexpr std::array<ID3D11ShaderResourceView*, 6> NO_INPUTS = {};
    constexpr std::array<ID3D11UnorderedAccessView*, 4> NO_OUTPUTS = {};
    context->CSSetShaderResources(0, 6, NO_INPUTS.data());
    context->CSSetUnorderedAccessViews(0, 4, NO_OUTPUTS.data(), nullptr);
    // The mask written this frame is the next frame's last one.
    runtime.particle_mask_valid = static_cast<bool>(translucency);
    runtime.particle_mask_index ^= 1u;

    if (camera_cut) ++diagnostics.camera_cuts;
    if (restart) ++diagnostics.restarts;
    if (skipped) ++diagnostics.skipped_frames;
    runtime.performance.Mark(context);
    // Ray Reconstruction: its guides from this frame's G-buffer copy into the bridge's (none: Super Resolution).
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer_views;
    if (nvidia_model && use.ray_reconstruction != 0 && bridge.guides) {
      const std::shared_lock lock(gbuffer.mutex);
      if (gbuffer.frame == frame->frame) gbuffer_views = gbuffer.srvs;
    }
    const bool reconstruct = gbuffer_views[0] && gbuffer_views[1] && gbuffer_views[2];
    if (reconstruct) {
      talesofarise::dlss::ray_tracing::WriteGuides(context, runtime.guide_shader.Get(), runtime.guide_constants.Get(),
                                                   {gbuffer_views[0].Get(), gbuffer_views[1].Get(), gbuffer_views[2].Get()}, frame->view,
                                                   {frame->render_width, frame->render_height});
    }
    // The scene colour is not pre-exposed (the tonemapper does not scale it); FSR and XeSS get the forward projection.
    const twattila::dlaa_bridge::RayReconstruction reconstruction = {.preset = (use.ray_reconstruction != 0) ? use.ray_reconstruction - 1 : 0u,
                                                                     .world_to_view = world_to_view,
                                                                     .view_to_clip = view_to_clip};
    evaluated = twattila::dlaa_bridge::Evaluate(context, frame->render_width, frame->render_height, frame->pixel_jitter[0], frame->pixel_jitter[1],
                                                reset, preset.load(), reconstruct ? &reconstruction : nullptr,
                                                nvidia_model ? nullptr : &view_to_clip, {.fsr_sharpness = fsr_sharpness.load()});
    reconstructed = evaluated && reconstruct && bridge.reconstructing;
    talesofarise::rt_shadows::reconstructing.store(reconstructed, std::memory_order_relaxed);
    runtime.performance.Mark(context);
    if (evaluated) {
      talesofarise::rt_shadows::CaptureSceneColor(context, frame->resources[0].Get());
    }
    if (evaluated && merge) {
      // The stock output's alpha, and its RGB where the helper's is not finite (when replacing: the stale output).
      context->ClearState();
      runtime.performance.Mark(context);
      ID3D11ShaderResourceView* merge_inputs[] = {bridge.output.srv.Get(), frame->target_srv.Get()};
      ID3D11UnorderedAccessView* merge_outputs[] = {runtime.merged.uav.Get()};
      context->CSSetShader(runtime.merge_shader.Get(), nullptr, 0);
      context->CSSetShaderResources(0, 2, merge_inputs);
      context->CSSetUnorderedAccessViews(0, 1, merge_outputs, nullptr);
      context->Dispatch((frame->width + 7) / 8, (frame->height + 7) / 8, 1);
      context->CopyResource(frame->target.Get(), runtime.merged.resource.Get());
    } else if (evaluated && direct) {
      // DLAA replacing an RGBA16F output: the upscaled frame is the TAA output (its alpha the helper's, as NVIDIA's was).
      runtime.performance.Mark(context);
      const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = frame->width, .bottom = frame->height, .back = 1};
      context->CopySubresourceRegion(frame->target.Get(), 0, 0, 0, 0, bridge.output.texture.Get(), 0, &region);
    } else if (evaluated && frame->temporal_upscaling && replace) {
      // The stock pass is skipped: its render-size output gets the upscaler's result, downsampled.
      context->ClearState();
      runtime.performance.Mark(context);
      const std::array<uint32_t, 4> extent = {frame->render_width, frame->render_height, 0, 0};
      context->UpdateSubresource(runtime.downsample_constants.Get(), 0, nullptr, extent.data(), 0, 0);
      ID3D11Buffer* downsample_buffers[] = {runtime.downsample_constants.Get()};
      ID3D11ShaderResourceView* downsample_inputs[] = {bridge.output.srv.Get()};
      ID3D11SamplerState* downsample_samplers[] = {runtime.linear_sampler.Get()};
      ID3D11UnorderedAccessView* downsample_outputs[] = {frame->target_uav.Get()};
      context->CSSetShader(runtime.downsample_shader.Get(), nullptr, 0);
      context->CSSetConstantBuffers(0, 1, downsample_buffers);
      context->CSSetShaderResources(0, 1, downsample_inputs);
      context->CSSetSamplers(0, 1, downsample_samplers);
      context->CSSetUnorderedAccessViews(0, 1, downsample_outputs, nullptr);
      context->Dispatch((frame->render_width + 7) / 8, (frame->render_height + 7) / 8, 1);
    } else if (evaluated) {
      runtime.performance.Mark(context);
    }
  }
  runtime.performance.End(context);
  if (!evaluated) {
    runtime.last_frame = UINT64_MAX;
    SetStatus(std::string("Game TAA: ") + (bridge.shared != nullptr ? bridge.shared->status : "no answer from the helper"), frame->generation);
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
        .jitter_x = frame->pixel_jitter[0],
        .jitter_y = frame->pixel_jitter[1],
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
  // Post-processing at the output size reads the buffer's UVs: only while the view fills its buffer.
  const bool output_size_post = frame->temporal_upscaling && frame->source_width == frame->render_width
                                && frame->source_height == frame->render_height;
  if (frame->temporal_upscaling) {
    const std::unique_lock lock(post.mutex);
    if (post.width != frame->width || post.height != frame->height || post.render_width != frame->render_width
        || post.render_height != frame->render_height) {
      // New sizes: the game's targets are new too.
      post.targets = {};
      post.upscale_constants.Reset();
      post.width = frame->width;
      post.height = frame->height;
      post.render_width = frame->render_width;
      post.render_height = frame->render_height;
    }
    post.frame = (output_size_post ? frame->frame : UINT64_MAX);
    post.taa_output = frame->target;
    post.output = bridge.output.srv;
  }
  SetStatus(ModeName(frame->mode) + " active — "
                + (frame->temporal_upscaling
                       ? std::format("{}x{} to {}x{}{}", frame->render_width, frame->render_height, frame->width, frame->height,
                                     (output_size_post ? "" : ", post-processing at the render size"))
                       : "native resolution")
                + (replace ? ", replacing the game's TAA" : "") + (reconstructed ? ", Ray Reconstruction" : ""),
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
    if (state.expected_width == 0 || command.arguments.group_count_x != (state.expected_width + 7) / 8
        || command.arguments.group_count_y != (state.expected_height + 7) / 8 || command.arguments.group_count_z != 1) {
      return {};
    }
    mode_changed = state.last_mode != mode;
    state.last_mode = mode;
  }
  if (mode_changed) Reset();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
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
  if (target_desc.Width != frame->source_width || target_desc.Height != frame->source_height || target_desc.MipLevels != 1
      || target_desc.ArraySize != 1 || target_desc.SampleDesc.Count != 1
      || target_desc.Format != uav.Format
      || frame->target == frame->resources[0]) return unsupported_texture();
  frame->target_format = target_desc.Format;
  frame->target_uav = target;
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
  frame->measuring = DetailsShown(frame->frame);
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

template <typename Command>
concept DrawCommand = std::is_same_v<Command, renodx::utils::command_action::CommandContext<renodx::utils::command_action::DrawArguments>>
                      || std::is_same_v<Command, renodx::utils::command_action::CommandContext<renodx::utils::command_action::DrawIndexedArguments>>;

// The command's draw on the native context (no addon events).
template <DrawCommand Command>
void DrawNative(ID3D11DeviceContext* context, const Command& command) {
  const auto& arguments = command.arguments;
  if constexpr (std::is_same_v<Command, renodx::utils::command_action::CommandContext<renodx::utils::command_action::DrawIndexedArguments>>) {
    context->DrawIndexedInstanced(arguments.index_count, arguments.instance_count, arguments.first_index, arguments.vertex_offset,
                                  arguments.first_instance);
  } else {
    context->DrawInstanced(arguments.vertex_count, arguments.instance_count, arguments.first_vertex, arguments.first_instance);
  }
}

// Views from a D3D11 Get call (each holds a reference), released at scope exit.
template <typename View, size_t COUNT>
struct BoundViews {
  std::array<View*, COUNT> views = {};
  BoundViews() = default;
  BoundViews(const BoundViews&) = delete;
  BoundViews& operator=(const BoundViews&) = delete;
  ~BoundViews() {
    for (View* view : views) {
      if (view != nullptr) {
        view->Release();
      }
    }
  }
};

uint64_t CurrentFrame() {
  const std::shared_lock lock(state.mutex);
  return state.frame;
}

// Frame generation's HUD-less image (twattila/frame_generation.hpp KeepFrame: the helper presents) from the tone mapper's
// output-size target, with this frame's inputs.
inline void KeepFrame(ID3D11DeviceContext* context, ID3D11Texture2D* target) {
  if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  const uint64_t frame = CurrentFrame();
  twattila::frame_generation::Inputs inputs;
  {
    const std::shared_lock lock(frame_generation_frame.mutex);
    if (frame_generation_frame.frame != frame) return;
    inputs = frame_generation_frame.inputs;
  }
  twattila::frame_generation::KeepFrame(context, target, inputs);
}

// After the game's own motion blur / tonemapper draw (post callback: the HDR addon's shader and constants are still
// bound; it restores them in finalize callbacks), the same draw at the output size.
template <DrawCommand Command>
void DrawAtOutputSize(Command& command, const void*) {
  const uint64_t frame = CurrentFrame();
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  const std::unique_lock lock(post.mutex);
  if (post.frame != frame) return;
  // The game's fullscreen passes: one render-size target, no depth, one viewport over the view.
  BoundViews<ID3D11RenderTargetView, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
  ComPtr<ID3D11DepthStencilView> depth;
  context->OMGetRenderTargets(static_cast<UINT>(targets.views.size()), targets.views.data(), depth.GetAddressOf());
  if (targets.views[0] == nullptr || depth != nullptr
      || std::ranges::any_of(std::span(targets.views).subspan(1), [](const auto* view) { return view != nullptr; })) return;
  std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
  UINT viewport_count = static_cast<UINT>(viewports.size());
  context->RSGetViewports(&viewport_count, viewports.data());
  if (viewport_count != 1 || viewports[0].TopLeftX != 0.f || viewports[0].TopLeftY != 0.f
      || viewports[0].Width != static_cast<float>(post.render_width) || viewports[0].Height != static_cast<float>(post.render_height)) return;

  // Inputs: the TAA output becomes DLSS's output, earlier passes' targets their output-size copies.
  BoundViews<ID3D11ShaderResourceView, 16> inputs;
  context->PSGetShaderResources(0, static_cast<UINT>(inputs.views.size()), inputs.views.data());
  auto replaced = inputs.views;
  bool reads_upscaled = false;
  for (auto*& input : replaced) {
    if (input == nullptr) continue;
    ComPtr<ID3D11Resource> resource;
    input->GetResource(resource.GetAddressOf());
    if (resource == post.taa_output) {
      input = post.output.Get();
      reads_upscaled = true;
    } else if (const auto copy = std::ranges::find_if(post.targets, [&](const PostTarget& target) { return target.frame == frame && target.game == resource; });
               copy != post.targets.end()) {
      input = copy->srv.Get();
      reads_upscaled = true;
    }
  }
  if (!reads_upscaled) return;

  // The target's output-size copy (a new target takes the oldest slot).
  ComPtr<ID3D11Resource> game_target;
  targets.views[0]->GetResource(game_target.GetAddressOf());
  D3D11_RENDER_TARGET_VIEW_DESC target_view = {};
  targets.views[0]->GetDesc(&target_view);
  ComPtr<ID3D11Texture2D> game_texture;
  if (target_view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || target_view.Texture2D.MipSlice != 0
      || FAILED(game_target.As(&game_texture))) return;
  auto copy = std::ranges::find(post.targets, game_target, &PostTarget::game);
  if (copy == post.targets.end()) {
    copy = post.targets.begin() + post.next_target;
    post.next_target = (post.next_target + 1) % post.targets.size();
    *copy = {.game = game_target};
  }
  if (!copy->resource) {
    D3D11_TEXTURE2D_DESC desc = {};
    game_texture->GetDesc(&desc);
    desc.Width = post.width;
    desc.Height = post.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc = {.Count = 1};
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    const D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
        .Format = target_view.Format,
        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
    };
    const D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {
        .Format = target_view.Format,
        .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MipSlice = 0},
    };
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (FAILED(device->CreateTexture2D(&desc, nullptr, copy->resource.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(copy->resource.Get(), &srv_desc, copy->srv.GetAddressOf()))
        || FAILED(device->CreateRenderTargetView(copy->resource.Get(), &rtv_desc, copy->rtv.GetAddressOf()))) {
      *copy = {};
      return;
    }
  }

  std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors = {};
  UINT scissor_count = static_cast<UINT>(scissors.size());
  context->RSGetScissorRects(&scissor_count, scissors.data());
  context->PSSetShaderResources(0, static_cast<UINT>(replaced.size()), replaced.data());
  ID3D11RenderTargetView* copy_view = copy->rtv.Get();
  context->OMSetRenderTargets(1, &copy_view, nullptr);
  D3D11_VIEWPORT output_viewport = viewports[0];
  output_viewport.Width = static_cast<float>(post.width);
  output_viewport.Height = static_cast<float>(post.height);
  context->RSSetViewports(1, &output_viewport);
  const D3D11_RECT output_scissor = {.left = 0, .top = 0, .right = static_cast<LONG>(post.width), .bottom = static_cast<LONG>(post.height)};
  context->RSSetScissorRects(1, &output_scissor);
  DrawNative(context, command);
  // The game's bindings back (UE caches them).
  context->PSSetShaderResources(0, static_cast<UINT>(inputs.views.size()), inputs.views.data());
  context->OMSetRenderTargets(static_cast<UINT>(targets.views.size()), targets.views.data(), nullptr);
  context->RSSetViewports(viewport_count, viewports.data());
  context->RSSetScissorRects(scissor_count, scissors.data());
  copy->frame = frame;
  if (DetailsShown(frame)) {
    ++post.passes;
  }
  if (twattila::frame_generation::Wanted() && std::ranges::find(TONEMAPPER_SHADERS, command.matched_shader_hash) != TONEMAPPER_SHADERS.end()) {
    KeepFrame(context, copy->resource.Get());
  }
}

// DLAA: after the tonemapper's draw (at the output size), frame generation's HUD-less image from its target.
template <DrawCommand Command>
void DrawHudlessAfterTonemapper(Command& command, const void*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  context->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  if (ComPtr<ID3D11Texture2D> target; SUCCEEDED(resource.As(&target))) KeepFrame(context, target.Get());
}

constexpr auto OnPostPass = []<DrawCommand Command>(Command& command) -> renodx::utils::command_action::CallbackResult<Command> {
  const auto mode = requested_mode.load();
  if (mode == AAMode::OFF) return {};
  if (mode == AAMode::DLAA) {
    const bool tonemapper = std::ranges::find(TONEMAPPER_SHADERS, command.matched_shader_hash) != TONEMAPPER_SHADERS.end();
    if (tonemapper && twattila::frame_generation::Wanted()) return {.post_callback = DrawHudlessAfterTonemapper<Command>};
    return {};
  }
  const uint64_t frame = CurrentFrame();
  {
    const std::shared_lock lock(post.mutex);
    if (post.frame != frame) return {};
  }
  return {.post_callback = DrawAtOutputSize<Command>};
};

// UE's separate translucency composite before the TAA (ps 0x00438BF1; its t0 the particles' own buffer): kept for this
// frame's input preparation (Spark trails fix).
constexpr uint32_t TRANSLUCENCY_COMPOSITE_SHADER = 0x00438BF1u;
constexpr auto OnTranslucencyComposite = []<DrawCommand Command>(Command& command) -> renodx::utils::command_action::CallbackResult<Command> {
  if (!spark_fix.load() || requested_mode.load() == AAMode::OFF) return {};
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  if (context == nullptr) return {};
  ComPtr<ID3D11ShaderResourceView> translucency;
  context->PSGetShaderResources(0, 1, translucency.GetAddressOf());
  const std::unique_lock lock(state.mutex);
  state.translucency = std::move(translucency);
  state.translucency_frame = state.frame;
  return {};
};

// Ray tracing (rt_shadows.hpp): after each depth prepass draw (opaque ones by their vertex shader, alpha-tested ones by
// their masked pixel shader), a draw not yet in the scene is drawn once more into it; before Bandai's sun shadow mask
// the scene is traced and its visibility bound at t40.
template <DrawCommand Command>
void CaptureDepthDraw(Command& command, const void*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  const bool opaque = std::ranges::find(talesofarise::rt_shadows::DEPTH_SHADERS, command.matched_shader_hash,
                                        &talesofarise::rt_shadows::DepthShader::hash)
                      != talesofarise::rt_shadows::DEPTH_SHADERS.end();
  const uint32_t vertex_hash = opaque ? command.matched_shader_hash
                                      : renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::vertex_shader,
                                                                                   &command.bound_state_cache);
  const uint32_t pixel_hash = opaque ? 0u : command.matched_shader_hash;
  const auto vertex_code = [&] {
    const auto pipeline = renodx::utils::state::GetBoundShaderPipeline(command.cmd_list, reshade::api::pipeline_stage::vertex_shader,
                                                                        &command.bound_state_cache);
    return renodx::utils::shader::GetShaderData(pipeline, vertex_hash);
  };
  const auto& arguments = command.arguments;
  const uint32_t instances = std::max(arguments.instance_count, 1u);
  if constexpr (std::is_same_v<Command, renodx::utils::command_action::CommandContext<renodx::utils::command_action::DrawIndexedArguments>>) {
    const std::array<uint32_t, 5> draw = {arguments.index_count, arguments.instance_count, arguments.first_index,
                                          static_cast<uint32_t>(arguments.vertex_offset), arguments.first_instance};
    talesofarise::rt_shadows::Capture(context, vertex_hash, pixel_hash, draw, arguments.index_count * instances, vertex_code,
                                      [&] { DrawNative(context, command); });
  } else {
    const std::array<uint32_t, 4> draw = {arguments.vertex_count, arguments.instance_count, arguments.first_vertex,
                                          arguments.first_instance};
    talesofarise::rt_shadows::Capture(context, vertex_hash, pixel_hash, draw, arguments.vertex_count * instances, vertex_code,
                                      [&] { DrawNative(context, command); });
  }
}

constexpr auto OnDepthPrepass = []<DrawCommand Command>(Command&) -> renodx::utils::command_action::CallbackResult<Command> {
  if (!talesofarise::rt_shadows::Active()) return {};
  return {.post_callback = CaptureDepthDraw<Command>};
};

// Bend's screen-space shadows (ue_screen_space_shadows.hpp) from the sun shadow mask's scene depth (t0), sun (its b0,
// host_sss_setup.cs_5_0.hlsl) and View (b1), at t41 for the HDR part's sunshadow00, which multiplies 1 - them into the sun's
// shadowing; unbound after it with the rays' (t40).
constexpr UINT SCREEN_SPACE_SHADOWS_SLOT = 41;

// Bandai's SSAO (ao00 E243580F and its two blurs, 0.11 ms at DLSS Performance, 0.34 ms at DLAA in run_124955): skipped
// while the rays' AO or Path-Traced Ambient replaces it at every reader (lighting00-05's t5, toon00-09's t6: rt_ambient.hlsli
// reads t41 instead). This frame's trace comes after it (the sun shadow mask): last frame's decides.
constexpr std::array<uint32_t, 3> GAME_AO_SHADERS = {0xE243580Fu, 0x8D9AC9A4u, 0x8C5387E3u};

constexpr auto OnGameAo = [](Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!(helper_rays_installed ? talesofarise::dlss::ray_tracing::AmbientTracedLastFrame()
                              : talesofarise::rt_shadows::AmbientOcclusionTracedLastFrame())) {
    return {};
  }
  return {.bypass = true};
};

constexpr auto OnSunShadowMask = [](Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  const bool traced = helper_rays_installed ? talesofarise::dlss::ray_tracing::Trace(context) : talesofarise::rt_shadows::Trace(context);
  twattila::screen_space_shadows::Settings shadows;
  BoundViews<ID3D11ShaderResourceView, 1> depth;
  talesofarise::ue_screen_space_shadows::Inputs inputs;
  {
    const std::shared_lock lock(ue_effects.mutex);
    shadows = ue_effects.shadows;
    inputs.render_share = ue_effects.render_share;
  }
  ID3D11ShaderResourceView* occlusion = nullptr;
  if (shadows.enabled) {
    context->CSGetShaderResources(0, 1, depth.views.data());
    inputs.depth = depth.views[0];
    context1->CSGetConstantBuffers1(0, 1, inputs.sun.buffer.GetAddressOf(), &inputs.sun.first, &inputs.sun.count);
    context1->CSGetConstantBuffers1(1, 1, inputs.view.buffer.GetAddressOf(), &inputs.view.first, &inputs.view.count);
    occlusion = talesofarise::ue_screen_space_shadows::Compute(context, inputs, shadows);
  }
  if (occlusion == nullptr && !traced) return {};
  context->CSSetShaderResources(SCREEN_SPACE_SHADOWS_SLOT, 1, &occlusion);
  return {.post_callback = [](Context& command, const void*) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    if (context != nullptr) {
      talesofarise::rt_shadows::Unbind(context);
      ID3D11ShaderResourceView* none = nullptr;
      context->CSSetShaderResources(SCREEN_SPACE_SHADOWS_SLOT, 1, &none);
    }
  }};
};

// Spark motion: after a sprite particle draw (its translucency target and the scene depth bound), the same draw with its
// motion shaders into the spark motion target, with the game's depth test, the game's state restored after.
template <DrawCommand Command>
void DrawSparkMotion(Command& command, const void*) {
  const auto sprite = std::ranges::find_if(SPRITE_SHADERS, [&](const auto& entry) { return entry.first == command.matched_shader_hash; });
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (sprite == SPRITE_SHADERS.end() || context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  BoundViews<ID3D11RenderTargetView, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
  ComPtr<ID3D11DepthStencilView> depth;
  context->OMGetRenderTargets(static_cast<UINT>(targets.views.size()), targets.views.data(), depth.GetAddressOf());
  if (targets.views[0] == nullptr) return;
  ComPtr<ID3D11Resource> target_resource;
  targets.views[0]->GetResource(target_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target_texture;
  if (FAILED(target_resource.As(&target_texture))) return;
  D3D11_TEXTURE2D_DESC target_desc = {};
  target_texture->GetDesc(&target_desc);
  const uint64_t frame = CurrentFrame();

  const std::unique_lock lock(spark_motion.mutex);
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (!spark_motion.pixel_shader) {
    for (size_t index = 0; index < SPRITE_SHADERS.size(); ++index) {
      const auto& code = SPRITE_SHADERS[index].second;
      if (FAILED(device->CreateVertexShader(code.data(), code.size(), nullptr, spark_motion.vertex_shaders[index].GetAddressOf()))) return;
    }
    if (FAILED(device->CreatePixelShader(__0x7A1E0D28.data(), __0x7A1E0D28.size(), nullptr, spark_motion.pixel_shader.GetAddressOf()))) return;
  }
  D3D11_TEXTURE2D_DESC motion_desc = {};
  if (spark_motion.texture) spark_motion.texture->GetDesc(&motion_desc);
  if (!spark_motion.texture || motion_desc.Width != target_desc.Width || motion_desc.Height != target_desc.Height) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = target_desc.Width,
        .Height = target_desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };
    spark_motion.target.Reset();
    spark_motion.srv.Reset();
    spark_motion.frame = UINT64_MAX;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, spark_motion.texture.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateRenderTargetView(spark_motion.texture.Get(), nullptr, spark_motion.target.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(spark_motion.texture.Get(), nullptr, spark_motion.srv.GetAddressOf()))) {
      spark_motion.texture.Reset();
      return;
    }
  }
  if (spark_motion.frame != frame) {
    constexpr std::array<float, 4> NONE = {};
    context->ClearRenderTargetView(spark_motion.target.Get(), NONE.data());
    spark_motion.frame = frame;
  }

  ComPtr<ID3D11VertexShader> game_vertex_shader;
  ComPtr<ID3D11PixelShader> game_pixel_shader;
  ComPtr<ID3D11Buffer> view;
  ComPtr<ID3D11Buffer> game_pixel_constants;
  UINT first_constant = 0;
  UINT constant_count = 0;
  ComPtr<ID3D11BlendState> game_blend;
  std::array<float, 4> blend_factor = {};
  UINT sample_mask = 0;
  context->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
  context->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
  context->VSGetConstantBuffers(1, 1, view.GetAddressOf());
  context1->PSGetConstantBuffers1(1, 1, game_pixel_constants.GetAddressOf(), &first_constant, &constant_count);
  context->OMGetBlendState(game_blend.GetAddressOf(), blend_factor.data(), &sample_mask);
  ID3D11Buffer* view_buffer = view.Get();
  ID3D11RenderTargetView* motion_target = spark_motion.target.Get();
  context->VSSetShader(spark_motion.vertex_shaders[sprite - SPRITE_SHADERS.begin()].Get(), nullptr, 0);
  context->PSSetShader(spark_motion.pixel_shader.Get(), nullptr, 0);
  context->PSSetConstantBuffers(1, 1, &view_buffer);
  context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
  context->OMSetRenderTargetsAndUnorderedAccessViews(1, &motion_target, depth.Get(), D3D11_KEEP_UNORDERED_ACCESS_VIEWS, 0, nullptr, nullptr);
  DrawNative(context, command);
  context->OMSetRenderTargetsAndUnorderedAccessViews(static_cast<UINT>(targets.views.size()), targets.views.data(), depth.Get(),
                                                     D3D11_KEEP_UNORDERED_ACCESS_VIEWS, 0, nullptr, nullptr);
  context->OMSetBlendState(game_blend.Get(), blend_factor.data(), sample_mask);
  ID3D11Buffer* game_buffer = game_pixel_constants.Get();
  // A whole-buffer binding reads back as offset 0 and 4096 constants, more than a small buffer holds.
  if (first_constant == 0) {
    context->PSSetConstantBuffers(1, 1, &game_buffer);
  } else {
    context1->PSSetConstantBuffers1(1, 1, &game_buffer, &first_constant, &constant_count);
  }
  context->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
  context->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
}

constexpr auto OnSpriteDraw = []<DrawCommand Command>(Command&) -> renodx::utils::command_action::CallbackResult<Command> {
  if (!spark_fix.load() || requested_mode.load() == AAMode::OFF) return {};
  return {.post_callback = DrawSparkMotion<Command>};
};

// Ray-traced ambient occlusion, bounce light (or path tracing), reflections and specular occlusion (rt_shadows.hpp; the
// lighting pass's own traced here, from the G-buffer at t1-t3): bound at t41 / t42 / t43 / t44 for the HDR addon's
// lighting and toon lighting passes (whichever draws first traces; TOON_SHADERS), unbound after each.
constexpr UINT AMBIENT_SLOT = 41;
template <DrawCommand Command>
void UnbindAmbient(Command& command, const void*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  constexpr std::array<ID3D11ShaderResourceView*, 4> NONE = {};
  context->PSSetShaderResources(AMBIENT_SLOT, static_cast<UINT>(NONE.size()), NONE.data());
}

constexpr auto OnLightingDraw = []<DrawCommand Command>(Command& command) -> renodx::utils::command_action::CallbackResult<Command> {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  if (context == nullptr) return {};
  renodx::utils::command_action::CallbackResult<Command> result = {};
  talesofarise::rt_shadows::LightingResult lighting;
  if (talesofarise::rt_shadows::UsesLightingPass()) {
    BoundViews<ID3D11ShaderResourceView, 3> gbuffer_views;
    context->PSGetShaderResources(1, 3, gbuffer_views.views.data());
    lighting = talesofarise::rt_shadows::TraceLighting(context, gbuffer_views.views);
  }
  if (helper_rays_installed) {
    const auto reflected = talesofarise::dlss::ray_tracing::Reflections(context);
    lighting.reflection = reflected[0];
    lighting.specular_occlusion = reflected[1];
  }
  const auto ambient = helper_rays_installed ? talesofarise::dlss::ray_tracing::Ambient() : talesofarise::rt_shadows::AmbientOcclusion();
  ComPtr<ID3D11ShaderResourceView> bounce = lighting.bounce ? lighting.bounce : talesofarise::rt_shadows::BounceLight();
  // Without the rays' bounce light, GT-VBGI's (ue_light_bounce.hpp: off at Strength 0) at the frame's first lighting draw:
  // from the sun's light (t4), the scene depth (t0), the G-buffer (t1-t3) and the View (b1) at the draw's viewport.
  if (bounce) {
    const uint64_t frame = CurrentFrame();
    const std::unique_lock lock(ue_effects.mutex);
    ue_effects.rays_bounce_frame = frame;
  } else {
    const uint64_t frame = CurrentFrame();
    const std::unique_lock lock(ue_effects.mutex);
    ComPtr<ID3D11DeviceContext1> context1;
    if (ue_effects.bounce_frame != frame && ue_effects.bounce.strength > 0.f
        && SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
      BoundViews<ID3D11ShaderResourceView, 8> views;
      context->PSGetShaderResources(0, static_cast<UINT>(views.views.size()), views.views.data());
      const bool toon = std::ranges::find(TOON_SHADERS, command.matched_shader_hash) != TOON_SHADERS.end();
      D3D11_VIEWPORT viewport = {};
      UINT viewport_count = 1;
      context->RSGetViewports(&viewport_count, &viewport);
      talesofarise::ue_light_bounce::Inputs inputs = {
          .light = views.views[toon ? 7 : 4],
          .depth = views.views[0],
          .gbuffer_a = views.views[1],
          .gbuffer_b = views.views[2],
          .gbuffer_c = views.views[3],
          .render_size = {static_cast<UINT>(viewport.Width), static_cast<UINT>(viewport.Height)},
      };
      context1->PSGetConstantBuffers1(1, 1, inputs.view.buffer.GetAddressOf(), &inputs.view.first, &inputs.view.count);
      ue_effects.bounce_view = talesofarise::ue_light_bounce::Apply(context, inputs, ue_effects.bounce, ue_effects.denoise, nullptr);
      ue_effects.bounce_frame = frame;
    }
    if (ue_effects.bounce_frame == frame) {
      bounce = ue_effects.bounce_view;
    }
  }
  if (ambient || bounce || lighting.reflection || lighting.specular_occlusion) {
    const std::array<ID3D11ShaderResourceView*, 4> views = {ambient.Get(), bounce.Get(), lighting.reflection.Get(),
                                                            lighting.specular_occlusion.Get()};
    context->PSSetShaderResources(AMBIENT_SLOT, static_cast<UINT>(views.size()), views.data());
    result.post_callback = UnbindAmbient<Command>;
  }
  if (!talesofarise::upscaling::Guides() || requested_mode.load() == AAMode::OFF) return result;
  const uint64_t frame = CurrentFrame();
  {
    const std::shared_lock lock(gbuffer.mutex);
    if (gbuffer.frame == frame) return result;
  }
  BoundViews<ID3D11ShaderResourceView, 3> views;
  context->PSGetShaderResources(1, 3, views.views.data());
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const std::unique_lock lock(gbuffer.mutex);
  for (size_t index = 0; index < views.views.size(); ++index) {
    if (views.views[index] == nullptr) return result;
    ComPtr<ID3D11Resource> resource;
    views.views[index]->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) return result;
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC copy_desc = {};
    if (gbuffer.textures[index]) gbuffer.textures[index]->GetDesc(&copy_desc);
    if (!gbuffer.textures[index] || copy_desc.Width != desc.Width || copy_desc.Height != desc.Height || copy_desc.Format != desc.Format) {
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.CPUAccessFlags = 0;
      desc.MiscFlags = 0;
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
      views.views[index]->GetDesc(&view_desc);
      gbuffer.srvs[index].Reset();
      if (FAILED(device->CreateTexture2D(&desc, nullptr, gbuffer.textures[index].ReleaseAndGetAddressOf()))
          || FAILED(device->CreateShaderResourceView(gbuffer.textures[index].Get(), &view_desc, gbuffer.srvs[index].GetAddressOf()))) {
        gbuffer.textures[index].Reset();
        gbuffer.srvs[index].Reset();
        return result;
      }
    }
    context->CopyResource(gbuffer.textures[index].Get(), texture.Get());
  }
  gbuffer.frame = frame;
  return result;
};

// The game's upscale of the tonemapper's output, from its output-size copy instead: the same shader with the output
// size as its source size samples every pixel at its center (an exact copy, alpha 0 as the game's).
constexpr auto OnUpscale = []<DrawCommand Command>(Command& command) -> renodx::utils::command_action::CallbackResult<Command> {
  const auto mode = requested_mode.load();
  if (mode == AAMode::OFF || mode == AAMode::DLAA) return {};
  const uint64_t frame = CurrentFrame();
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  const std::unique_lock lock(post.mutex);
  if (post.frame != frame) return {};
  ComPtr<ID3D11ShaderResourceView> input;
  context->PSGetShaderResources(0, 1, input.GetAddressOf());
  if (!input) return {};
  ComPtr<ID3D11Resource> resource;
  input->GetResource(resource.GetAddressOf());
  const auto copy = std::ranges::find_if(post.targets, [&](const PostTarget& target) { return target.frame == frame && target.game == resource; });
  if (copy == post.targets.end()) return {};
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  if (viewport_count != 1 || viewport.TopLeftX != 0.f || viewport.TopLeftY != 0.f
      || viewport.Width != static_cast<float>(post.width) || viewport.Height != static_cast<float>(post.height)) return {};
  if (!post.upscale_constants) {
    const std::array<float, 4> source_size = {static_cast<float>(post.width), static_cast<float>(post.height),
                                              1.f / static_cast<float>(post.width), 1.f / static_cast<float>(post.height)};
    const D3D11_BUFFER_DESC desc = {
        .ByteWidth = sizeof(source_size),
        .Usage = D3D11_USAGE_IMMUTABLE,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = source_size.data()};
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (FAILED(device->CreateBuffer(&desc, &data, post.upscale_constants.GetAddressOf()))) return {};
  }
  ComPtr<ID3D11Buffer> game_constants;
  UINT first_constant = 0;
  UINT constant_count = 0;
  context1->PSGetConstantBuffers1(0, 1, game_constants.GetAddressOf(), &first_constant, &constant_count);
  ID3D11ShaderResourceView* copy_input = copy->srv.Get();
  ID3D11Buffer* output_size = post.upscale_constants.Get();
  context->PSSetShaderResources(0, 1, &copy_input);
  context->PSSetConstantBuffers(0, 1, &output_size);
  DrawNative(context, command);
  ID3D11ShaderResourceView* game_input = input.Get();
  ID3D11Buffer* game_buffer = game_constants.Get();
  context->PSSetShaderResources(0, 1, &game_input);
  // A whole-buffer binding reads back as offset 0 and 4096 constants, more than a small buffer holds.
  if (first_constant == 0) {
    context->PSSetConstantBuffers(0, 1, &game_buffer);
  } else {
    context1->PSSetConstantBuffers1(0, 1, &game_buffer, &first_constant, &constant_count);
  }
  if (DetailsShown(frame)) {
    ++post.upscales;
  }
  return {.bypass = true};
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

// The game's D3D11 device only: ray tracing's own D3D12 device, wrapped by ReShade without the Agility SDK, raises this
// too when rt_shadows lets it go under its own lock, and rt_shadows::Release would lock again on that thread.
void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  talesofarise::rt_shadows::Release();
  if (helper_rays_installed) talesofarise::dlss::ray_tracing::Release(device);
  {
    auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    const std::unique_lock lock(ue_effects.mutex);
    talesofarise::ue_screen_space_shadows::Release(native);
    talesofarise::ue_light_bounce::Release(native);
    ue_effects.bounce_frame = UINT64_MAX;
    ue_effects.bounce_view = nullptr;
    ue_effects.rays_bounce_frame = UINT64_MAX;
  }
  {
    const std::unique_lock lock(spark_motion.mutex);
    spark_motion.vertex_shaders = {};
    spark_motion.pixel_shader.Reset();
    spark_motion.texture.Reset();
    spark_motion.target.Reset();
    spark_motion.srv.Reset();
    spark_motion.frame = UINT64_MAX;
  }
  {
    const std::unique_lock lock(gbuffer.mutex);
    gbuffer.textures = {};
    gbuffer.srvs = {};
    gbuffer.frame = UINT64_MAX;
  }
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
  talesofarise::engine_hooks::UpdateSun();
  talesofarise::frame_trace::OnPresent("tales");
  // Before any row is read this frame: DLSS never starts on another vendor's GPU, a saved DLSS or a preset's is XeSS on
  // Intel's, else FSR (as Attila's).
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  // The page's rows (Anti-Aliasing and Upscaling Mode: DLAA, Ultra Quality, Quality, Balanced, Performance, Ultra
  // Performance, Custom; the rays; Upscaling and Tools) into the state the passes read.
  {
    constexpr std::array<AAMode, 7> UPSCALING_MODES = {AAMode::DLAA, AAMode::DLSS_ULTRA_QUALITY, AAMode::DLSS_QUALITY, AAMode::DLSS_BALANCED,
                                                       AAMode::DLSS_PERFORMANCE, AAMode::DLSS_ULTRA_PERFORMANCE, AAMode::DLSS_CUSTOM};
    // Without the helper (the HDR-only download) the game's TAA, its own render scale.
    const bool upscaling = twattila::feature_settings::Upscaling(aa_sliders) && twattila::dlaa_bridge::HelperInstalled();
    requested_mode.store(upscaling ? UPSCALING_MODES[static_cast<size_t>(std::clamp(aa_sliders.dlss_mode, 0.f, 6.f))] : AAMode::OFF);
    const auto aa = twattila::feature_settings::AaSettings(aa_sliders);
    requested_upscaler.store(twattila::anti_aliasing::UpscalerOf(aa.method));
    preset.store(aa.dlaa_preset);
    fsr_sharpness.store(aa.fsr_sharpness);
    // What the bridge serves this frame (upscaling.hpp): the upscaler, Ray Reconstruction's guides, frame generation's frames.
    std::array<uint32_t, 2> size;
    {
      const std::shared_lock lock(state.mutex);
      size = {state.width, state.height};
    }
    talesofarise::upscaling::SetUse({
        .upscaler = upscaling ? requested_upscaler.load() + 1 : 0u,
        .ray_reconstruction = aa.ray_reconstruction ? aa.ray_reconstruction_preset + 1 : 0u,
        .frame_format = twattila::frame_generation::Format(),
        .frame_backend = aa.frame_backend,
        .width = size[0],
        .height = size[1],
    });
    live.custom = std::clamp(std::round(aa_sliders.render_scale), 1.f, 100.f);
    texture_lod_bias = aa.texture_lod_bias;
    effects_at_output = aa.effects_at_output;
    replace_taa.store(replace_taa_row != 0.f);
    spark_fix.store(spark_fix_row != 0.f);
    keep_game_sharpening = game_sharpening_row != 0.f;
    camera_jitter.enabled = camera_jitter_row != 0.f;
    // On again: a stopped or failed live scale is retried.
    if (const bool enabled = live_scale_row != 0.f; enabled != live.enabled) {
      live.enabled = enabled;
      if (enabled) {
        live.problem.clear();
        live.retries = 0;
        if (live.finder.phase() == talesofarise::engine_scale::Finder::Phase::FAILED) live.finder = {};
      }
    }
    // The rays (rt_shadows.hpp) with an upscaler on (rt_scene::SettingsOf; none without the helper); path tracing replaces the
    // bounce light. An effect turned on tries again after a failed start. Path-Traced Ambient (SettingsOf's needs a Total
    // War ambient cube): with the paths, the ambient rays weighted by the game's sky light in place of Bandai's AO under its
    // ambient (Bandai's sky light comes from its base pass inside the scene colour the lighting passes read: no input to
    // zero there, so the paths' share of it enters where its AO does), the paths' bounce at t42 as before.
    // Ray Tracing in the Helper: all its effects (ray_tracing.hpp; Path-Traced Ambient its AO under the paths), nothing in
    // process.
    const auto all_rays = upscaling ? twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, gi_rows.light_bounce_strength) : twattila::rt_scene::Settings{};
    if (helper_rays_installed) {
      auto helper_rays = all_rays;
      helper_rays.ambient_occlusion = all_rays.ambient_occlusion || (all_rays.path_tracing && rt_sliders.path_ambient != 0.f);
      helper_rays.path_ambient = false;
      talesofarise::dlss::ray_tracing::Update(helper_rays);
    }
    const auto rays = helper_rays_installed ? twattila::rt_scene::Settings{} : all_rays;
    const bool path_ambient = rays.path_tracing && rt_sliders.path_ambient != 0.f;
    bool turned_on = false;
    for (const auto& [effect, on] : std::to_array<std::pair<std::atomic_bool*, bool>>({
             {&talesofarise::rt_shadows::enabled, rays.sun_shadows},
             {&talesofarise::rt_shadows::ambient_occlusion, rays.ambient_occlusion || path_ambient},
             {&talesofarise::rt_shadows::bounce_light, rays.light_bounce && !rays.path_tracing},
             {&talesofarise::rt_shadows::reflections, rays.reflections},
             {&talesofarise::rt_shadows::specular_occlusion, rays.specular_occlusion},
             {&talesofarise::rt_shadows::path_tracing, rays.path_tracing},
         })) {
      turned_on |= !effect->exchange(on) && on;
    }
    if (turned_on) talesofarise::rt_shadows::Retry();
    talesofarise::rt_shadows::sun_softness.store(std::clamp(rays.sun_softness, 0.f, 3.0004132f));
    talesofarise::rt_shadows::character_shadows.store(character_shadows_row != 0.f);
    talesofarise::rt_shadows::ambient_range.store(std::clamp(rays.ambient_range, 0.5f, 10.001733f) * 100.f);
    talesofarise::rt_shadows::ambient_rays.store(std::clamp(rays.ambient_rays, 1u, 8u));
    talesofarise::rt_shadows::sky_weighted_ambient.store(path_ambient);
    talesofarise::rt_shadows::bounce_strength.store(std::clamp(gi_rows.light_bounce_strength, 0.f, 4.f));
    talesofarise::rt_shadows::bounce_range.store(std::clamp(rays.light_bounce_range, 1.f, 50.012573f) * 100.f);
    talesofarise::rt_shadows::bounce_rays.store(std::clamp(rays.light_bounce_rays, 1u, 8u));
    talesofarise::rt_shadows::path_bounces.store(rays.path_bounces);
    talesofarise::rt_shadows::path_samples.store(static_cast<uint32_t>(std::clamp(path_samples_row, 1.f, 4.f)));
    talesofarise::rt_shadows::path_reorder.store(path_reorder_row != 0.f);
  }
  {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_immediate_command_list()->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    talesofarise::rt_shadows::OnPresent(context);
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
    changed = state.last_mode != mode;
    state.last_mode = mode;
    missed = state.claimed_frame != state.frame;
    ++state.frame;
    width = state.width;
    widest_display = state.widest_display;
    height = state.height;
    const uint64_t copied = talesofarise::dlss::constants::CopiedBytes();
    state.copied_per_frame = copied - state.copied_bytes;
    state.copied_bytes = copied;
    const uint64_t biased = talesofarise::dlss::constants::BiasedUploads();
    state.biased_per_frame = biased - state.biased_uploads;
    state.biased_uploads = biased;
  }
  talesofarise::dlss::constants::SetCounting(false);
  {
    const std::unique_lock lock(post.mutex);
    post.last = {post.passes, post.upscales};
    post.passes = 0;
    post.upscales = 0;
  }
  if (changed) Reset();
  if (mode == AAMode::OFF) {
    SetStatus("Off — original game TAA");
  } else if (missed || changed) {
    // Do not leave an old "active" label up when this view no longer matches.
    std::shared_lock lock(state.mutex);
    if (state.status.find(" active") != std::string::npos || changed) {
      lock.unlock();
      SetStatus("Game TAA: waiting for " + ModeName(mode) + "; keep the game's Anti-Aliasing on TAA");
    }
  }

  // The mode's render size: whole units of the output's aspect ratio, sides in buffer multiples (16:9 at 4K: 64x36,
  // 60 steps), nearest to its scale. UE renders ceil(size * percentage / 100): the percentage written is a quarter
  // pixel below the exact size. 0: the game's own.
  float desired = 0.f;
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
    desired = (render_size == std::array<uint32_t, 2>{width, height}
                   ? 100.f
                   : 100.f * std::min(static_cast<float>(render_size[0]) / static_cast<float>(width), static_cast<float>(render_size[1]) / static_cast<float>(height))
                         - 25.f / static_cast<float>(std::max(width, height)));
  }
  const auto rendered = [&](uint32_t size) { return static_cast<uint32_t>(std::ceil(static_cast<float>(size) * (desired / 100.f))); };

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
          const auto seen_width = static_cast<uint32_t>(seen_input.load());
          const uint32_t expected = rendered(width);
          if (width != 0 && seen_width != 0 && std::abs(static_cast<int64_t>(seen_width) - static_cast<int64_t>(expected)) > 1) {
            if (seen_width == live.width_at_write) {
              live.problem = std::format("no effect: the input stayed {} pixels wide instead of {}", seen_width, expected);
            } else {
              live.note = std::format("the game renders {} pixels wide, not {}", seen_width, expected);
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
  // DLSS needs the render scale written; DLAA without it needs the game's own at 100%.
  std::array<uint32_t, 2> expected = {};
  if (live.controlling && desired > 0.f) {
    expected = {rendered(width), rendered(height)};
  } else if (mode == AAMode::DLAA) {
    expected = {width, height};
  }
  {
    const std::unique_lock lock(state.mutex);
    state.expected_width = expected[0];
    state.expected_height = expected[1];
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
      DWORD region_size = 0;
      auto* sample = static_cast<uint8_t*>(DetourAllocateRegionWithinJumpBounds(site, &region_size));
      uint8_t* multiply = site + JITTER_SCALE_MULTIPLY;
      // The jne skips exactly the multiply.
      if (sample != nullptr && region_size >= 8 && site[JITTER_SCALE_BRANCH + 1] == JITTER_SCALE_MULTIPLY_SIZE) {
        std::vector<uint8_t> loads = {0xF3, 0x0F, 0x10, 0x3D, 0, 0, 0, 0, 0xF3, 0x44, 0x0F, 0x10, 0x05, 0, 0, 0, 0, 0x90};
        const auto x = static_cast<int32_t>(sample - (multiply + 8));
        const auto y = static_cast<int32_t>(sample + 4 - (multiply + 17));
        std::memcpy(loads.data() + 4, &x, sizeof(x));
        std::memcpy(loads.data() + 13, &y, sizeof(y));
        camera_jitter.halton_sample = reinterpret_cast<float*>(sample);
        camera_jitter.halton_multiply = {
            .address = multiply,
            .original = std::vector<uint8_t>(multiply, multiply + JITTER_SCALE_MULTIPLY_SIZE),
            .replacement = std::move(loads),
            .details = jitter_patch.details,
        };
        camera_jitter.halton_branch = {
            .address = site + JITTER_SCALE_BRANCH + 1,
            .original = {static_cast<uint8_t>(JITTER_SCALE_MULTIPLY_SIZE)},
            .replacement = {0x00},
            .details = jitter_patch.details,
        };
      } else {
        renodx::utils::log::w("[Tales AA jitter] Halton jitter for DLSS unavailable (no memory near the code or an unexpected branch).");
      }
    } else {
      renodx::utils::log::w("[Tales AA jitter] The game's jitter scaling was not found: ", jitter_patch.details, ".");
    }
  }
  const bool jitter = camera_jitter.enabled && mode != AAMode::OFF;
  const bool upscaling = expected[0] != 0 && (expected[0] < width || expected[1] < height);
  const bool halton = jitter && expected[0] != 0 && camera_jitter.halton_sample != nullptr;
  if (halton) {
    const auto positions = static_cast<uint32_t>(std::ceil(8. * width * height / (static_cast<double>(expected[0]) * expected[1])));
    if (positions != camera_jitter.halton_positions) {
      camera_jitter.halton_positions = positions;
      camera_jitter.halton_index = 0;
    }
    camera_jitter.halton_index = (camera_jitter.halton_index + 1) % positions;
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
    std::atomic_ref(camera_jitter.halton_sample[0]).store(radical_inverse(camera_jitter.halton_index + 1, 2) - .5f, std::memory_order_relaxed);
    std::atomic_ref(camera_jitter.halton_sample[1]).store(radical_inverse(camera_jitter.halton_index + 1, 3) - .5f, std::memory_order_relaxed);
  }
  // The multiply is rewritten only while the jmp skips it: Halton off before the jmp goes, on after it.
  auto& halton_branch = camera_jitter.halton_branch;
  auto& halton_multiply = camera_jitter.halton_multiply;
  if (!halton && halton_branch.applied && talesofarise::code_patch::ApplyCodePatch(&halton_branch, false)) {
    renodx::utils::log::i("[Tales AA jitter] Halton off.");
  }
  if (!halton && halton_multiply.applied && !halton_branch.applied) {
    talesofarise::code_patch::ApplyCodePatch(&halton_multiply, false);
  }
  if (jitter_patch.address != nullptr && jitter_patch.applied != jitter && !halton_multiply.applied) {
    if (talesofarise::code_patch::ApplyCodePatch(&jitter_patch, jitter)) {
      renodx::utils::log::i("[Tales AA jitter] ", (jitter ? "On" : "Off (the game's)"), ".");
    } else {
      renodx::utils::log::w("[Tales AA jitter] Patch failed: ", jitter_patch.details, ".");
    }
  }
  if (halton && jitter_patch.applied && !halton_branch.applied
      && (halton_multiply.applied || talesofarise::code_patch::ApplyCodePatch(&halton_multiply, true))) {
    if (talesofarise::code_patch::ApplyCodePatch(&halton_branch, true)) {
      renodx::utils::log::i("[Tales AA jitter] Halton (2, 3), ", camera_jitter.halton_positions, " positions.");
    } else {
      renodx::utils::log::w("[Tales AA jitter] Halton patch failed: ", halton_branch.details, ".");
    }
  }

  // Graphics preset: the console is searched once (after r.ScreenPercentage's search), then each setting is kept at
  // the preset's value; a value the game sets again (its menu) becomes the one to restore.
  if (!graphics.searched && !finder.Searching()) {
    graphics.searched = true;
    std::string details;
    graphics.available = graphics.console.Start(talesofarise::engine_cvars::UE_4_21, &details);
    graphics.status = (graphics.available ? "" : "Graphics presets unavailable: " + details + " (details in ReShade.log).");
    if (graphics.available) {
      renodx::utils::log::i("[Tales graphics] ", details, ".");
    } else {
      renodx::utils::log::w("[Tales graphics] ", details, ".");
    }
    // Arise-SDK installed: its live settings exist.
    std::string arise_sdk_details;
    const bool arise_sdk = graphics.available && graphics.console.Find(L"sdk.MonsterDistanceMultiplier", &arise_sdk_details).values[0] != nullptr;
    renodx::utils::log::i("[Tales graphics] Arise-SDK ", (arise_sdk ? "installed" : "not found"),
                          (debug_dev_arise ? "; DebugDevArise: the addon's own code for its settings" : ""), ".");
    sun_distance_limited = !arise_sdk && !debug_dev_arise;
    if (sun_distance_limited) {
      talesofarise::code_patch::ApplyCodePatch(&talesofarise::engine_hooks::sun_distance_limit, false);
      // A saved choice above the limit shows (and is written) as the limit; the ini keeps it for Arise-SDK.
      auto& sun_distance = graphics.entries[SUN_SHADOW_DISTANCE];
      std::erase_if(sun_distance.choices, [](float value) { return value > SUN_DISTANCE_GAME_LIMIT; });
      sun_distance.choice = std::min(sun_distance.choice, static_cast<int>(sun_distance.choices.size()));
    }
    for (size_t index = 0; graphics.available && index < GRAPHICS_SETTINGS.size(); ++index) {
      auto& entry = graphics.entries[index];
      const auto own = std::ranges::find(talesofarise::engine_hooks::options, std::wstring_view(GRAPHICS_SETTINGS[index].name),
                                         [](const auto& option) { return std::wstring_view(option.name); });
      const bool arise_row = !debug_dev_arise && std::string_view(GRAPHICS_SETTINGS[index].group) == ARISE_SDK_GROUP;
      if (arise_row && !arise_sdk) {
        entry.details = "needs Arise-SDK";
      } else if (arise_row && own != talesofarise::engine_hooks::options.end() && own->arise_sdk_cvar != nullptr) {
        std::string sdk_details;
        entry.cvar = graphics.console.Find(own->arise_sdk_cvar, &sdk_details);
        entry.details = std::format("Arise-SDK's {}, {}", std::filesystem::path(own->arise_sdk_cvar).string(), sdk_details);
      } else if (own == talesofarise::engine_hooks::options.end()) {
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
  }
  bool wrote = false;
  // The size the game renders at: the upscaler's, else the game's own below 100% (its menu's DrawScale, r.ScreenPercentage
  // as found; UE renders ceil(size x percentage)), for Bandai's screen filters, Bend and the texture LOD bias.
  std::array<uint32_t, 2> render_size = expected;
  if (const auto current = (finder.phase() == Phase::FOUND ? finder.Get() : std::nullopt); !upscaling && current && *current > 0.f && *current < 100.f) {
    render_size = {static_cast<uint32_t>(std::ceil(static_cast<float>(width) * *current / 100.f)),
                   static_cast<uint32_t>(std::ceil(static_cast<float>(height) * *current / 100.f))};
  }
  const bool below_native = render_size[0] != 0 && (render_size[0] < width || render_size[1] < height);
  const float render_scale = (below_native ? static_cast<float>(render_size[0]) / static_cast<float>(width) : 1.f);
  for (size_t index = 0; graphics.available && index < GRAPHICS_SETTINGS.size(); ++index) {
    auto& entry = graphics.entries[index];
    if (entry.cvar.values[0] == nullptr) continue;
    const auto& setting = GRAPHICS_SETTINGS[index];
    const float preset_value = (entry.choice == 0 ? KEEP : entry.choices[entry.choice - 1]);
    const OutputScaling scaling = ((effects_at_output && upscaling) ? OUTPUT_SCALING[index] : OutputScaling::NONE);
    const float current = entry.cvar.Get();
    const bool replaced = index == SSR_QUALITY
                          && (talesofarise::rt_shadows::ReflectingRecently()
                              || (helper_rays_installed && talesofarise::dlss::ray_tracing::ReflectingRecently()));
    if (std::isnan(preset_value) && scaling == OutputScaling::NONE && !replaced) {
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
    if (replaced || (index == MOTION_BLUR_QUALITY && entry.game_value == 0.f)) desired = 0.f;
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
    if (std::ranges::find(VOLUME_SETTINGS, index) != VOLUME_SETTINGS.end()) {
      desired = std::max(desired, std::ceil(static_cast<float>(std::max(width, widest_display)) / 2048.f));
    }
    if (entry.cvar.is_int) desired = std::round(desired);
    if (current != desired) {
      entry.cvar.Set(desired);
      wrote = true;
    }
    entry.written = desired;
  }
  if (wrote) graphics.console.Refresh();
  // Texture LOD bias at every render scale below 100%, from the next View upload on: the row's choice while upscaling,
  // log2(scale) (Matched) at the game's own scale.
  talesofarise::dlss::constants::ViewMipBias mip_bias = {.size_vector = VIEW_SIZE_AND_INV_SIZE, .bias_vector = VIEW_MATERIAL_TEXTURE_MIP_BIAS};
  if (below_native && texture_lod_bias != 0) {
    mip_bias.width = render_size[0];
    mip_bias.height = render_size[1];
    mip_bias.bias = std::log2(std::min(static_cast<float>(render_size[0]) / static_cast<float>(width), static_cast<float>(render_size[1]) / static_cast<float>(height)))
                    - (upscaling ? static_cast<float>(texture_lod_bias - 1) : 0.f);
  }
  if (mip_bias.width != applied_mip_bias.width || mip_bias.height != applied_mip_bias.height || mip_bias.bias != applied_mip_bias.bias) {
    talesofarise::dlss::constants::SetViewMipBias(mip_bias);
    applied_mip_bias = mip_bias;
    renodx::utils::log::i("[Tales DLSS] Texture LOD bias ", (mip_bias.width != 0 ? std::format("{:.2f}", mip_bias.bias) : std::string("the game's")), ".");
  }
  // Bandai's screen filters before the TAA are sized in render pixels: the HDR addon spaces them in output pixels.
  const auto& sun_distance = graphics.entries[SUN_SHADOW_DISTANCE];
  const float sun_distance_ratio = ((sun_distance.controlling && sun_distance.game_value > 0.f)
                                        ? sun_distance.written / sun_distance.game_value
                                        : 1.f);
  ao_frame = (ao_frame + 1u) % 64u;
  // Bend's screen-space shadows (not under the rays' sun shadows) and GT-VBGI (not with the rays' bounce light: Global
  // Illumination's method), the passes' thickness the AO's; the HDR part leaves Bandai's own out while they run.
  auto shadows = twattila::feature_settings::SssSettings(screen_space_shadows_rows);
  const auto helper_rays = helper_rays_installed ? talesofarise::dlss::ray_tracing::CurrentSettings() : twattila::rt_scene::Settings{};
  shadows.enabled = shadows.enabled && !talesofarise::rt_shadows::enabled.load() && !helper_rays.sun_shadows;
  gi_rows.ssao_thickness = ao_thickness;
  const auto gi = twattila::lighting_settings::LightBounceSliders(gi_rows);
  auto bounce = talesofarise::ue_light_bounce::ConstantsOf(gi, ao_frame);
  // The rays' bounce light: in process its own (GT-VBGI off); in the helper GT-VBGI's passes with the rays' trace in
  // place of its own (ray_tracing.hpp), at the rays' resolution and every texel each frame (as the Total War games'
  // light_bounce::SetRayTraced).
  if (helper_rays.light_bounce) {
    bounce.scale = twattila::light_bounce::internal::PowerOfTwo(helper_rays.light_bounce_resolution, 2.f);
    bounce.checkerboard = 0.f;
  } else if (rt_sliders.light_bounce != 0.f) {
    bounce.strength = 0.f;
  }
  // One GI method (ATN 2026-11-16): lighting00-05 leave out Bandai's GI add while GT-VBGI runs, or while the rays' bounce
  // light or path tracing reached the lighting pass within the last half second (their light in place of Bandai's, never
  // added to it; a frame the rays miss stays so); Bandai's GI still colours their fill light (shared.h TOA_GAME_GI_SHARE:
  // the menu's GI-off path also halved that fill). The ten toon lighting passes (toon00-09) the same: their GI add goes,
  // ours in its place. Bandai's SSGI chain (17CE7BCD, 13BDD599, 86CBF00A, 746654CA) still runs under ours: lighting00-05's
  // fill colour and the toon passes' tint of the lit colour and their rims read its 10 x 6 texture (t25-t30 there),
  // terms ours does not replace; a skipped chain left them a stale pooled texture.
  constexpr uint64_t RECENT_FRAMES = 30;
  const uint64_t frame = CurrentFrame();
  bool rays_gi = false;
  {
    const std::unique_lock lock(ue_effects.mutex);
    ue_effects.shadows = shadows;
    ue_effects.bounce = bounce;
    ue_effects.denoise = gi.denoise != 0.f;
    ue_effects.render_share = render_scale;
    rays_gi = rt_sliders.light_bounce != 0.f && ue_effects.rays_bounce_frame != UINT64_MAX && ue_effects.rays_bounce_frame + RECENT_FRAMES >= frame;
  }
  talesofarise::post_filters::Store({
      .scale = render_scale,
      .sharpening = ((mode != AAMode::OFF && !keep_game_sharpening) ? 0.f : 1.f),
      .ambient_occlusion = static_cast<float>(ambient_occlusion),
      .sun_shadow_distance = sun_distance_ratio,
      // One AO method (ao-gi.md rule 6): GT-VBAO off while the rays' AO or Path-Traced Ambient takes the AO's place.
      .ao_method = ((talesofarise::rt_shadows::ambient_occlusion.load(std::memory_order_relaxed) || helper_rays.ambient_occlusion)
                        ? 0.f
                        : static_cast<float>(ao_method)),
      .ao_radius = ao_radius,
      .ao_strength = ao_strength,
      .ao_thickness = ao_thickness,
      .ao_directions = static_cast<float>(ao_directions),
      .ao_quality = static_cast<float>(ao_quality),
      .ao_frame = static_cast<float>(ao_frame),
      .ao_auto_thickness = ao_auto_thickness,
      .screen_space_shadows = (shadows.enabled ? 1.f : 0.f),
      .gtvbgi = ((bounce.strength > 0.f || rays_gi) ? ((game_gi_open_sky_row != 0.f) ? 2.f : 1.f) : 0.f),
      .ao_exact_depth = ao_exact_depth,
  });
  talesofarise::engine_hooks::Update();
}

// The status line under the anti-aliasing (graphics_page.hpp's upscaler_status): what DLSS does, the live render scale's
// and the camera jitter's problems, the helper's, the ray tracing's state.
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
    text += "\nLive render scale off: Native AA needs the game's render scale at 100% (its menu), the other modes need it on.";
  } else if (!live.problem.empty()) {
    text += (live.retries < MAX_LIVE_RETRIES
                 ? "\nLive render scale paused: " + live.problem + ". The game's own value is back for now; retrying in a few seconds."
                 : "\nLive render scale stopped: " + live.problem + ". The game's own value was put back; turn Live Render Scale off and on to retry.");
  } else if (live.finder.phase() == Phase::FAILED) {
    text += "\nRender scale: not found in this version of the game (details in ReShade.log): DLAA only, with the game's render scale at 100%.";
  }
  if (completed != 0 && status.find("active") != std::string::npos) {
    text += std::format("\nInput {}x{} -> output {}x{}", extents[0], extents[1], extents[2], extents[3]);
  }
  if (requested_mode.load() != AAMode::OFF && status.starts_with("Game TAA")) {
    text += "\nIn the game's graphics settings, keep Anti-Aliasing on TAA.";
  }
  const auto& jitter_patch = camera_jitter.patch;
  if (camera_jitter.enabled && !jitter_patch.applied && jitter_patch.address == nullptr && camera_jitter.searched) {
    text += "\nCamera jitter unavailable: this version of the game is not recognized (details in ReShade.log). DLAA still works, with more smearing on small details.";
  }
  if (const auto& shared = twattila::dlaa_bridge::bridge.shared; shared != nullptr && shared->status[0] != '\0') {
    text += std::string("\nHelper: ") + shared->status;
  }
  if (talesofarise::rt_shadows::Active()) text += "\n" + talesofarise::rt_shadows::Status();
  return text;
}

// The former overlay's keys ([Tales AA]: the DLSS add-on's until the UE4 merge round) in the page's, once (SettingsRevision 1),
// each into every preset without one of its own; before the settings are loaded. Settings Mode's Advanced (1) is HDR &
// Effects (0) again: its 1 is Graphics 1 of 2 now. [Tales AA]'s preset toggles (WeakCPU, FarSunShadows,
// MaxVolumetricFog), SmoothSun, DaySpeedMultiplier and DebugDevArise stay where they are (their own rows).
inline void Migrate() {
  const std::string& global = renodx::utils::settings::global_name;
  if (int revision = 0; reshade::get_config_value(nullptr, global.c_str(), "SettingsRevision", revision) && revision >= 1) return;
  std::vector<std::pair<std::string, float>> values;
  const auto read = [](const char* key, float* value) { return reshade::get_config_value(nullptr, "Tales AA", key, *value); };
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
  // Ray Reconstruction on with NVIDIA's preset (0), D (4), E (5), F (6): the page's 1-4.
  if (float reconstruct = 0.f; read("RayReconstruction", &reconstruct)) {
    float rr_preset = 0.f;
    read("RayReconstructionPreset", &rr_preset);
    values.emplace_back("AntiAliasingRayReconstruction", (reconstruct == 0.f ? 0.f : ((rr_preset >= 4.f && rr_preset <= 6.f) ? rr_preset - 2.f : 1.f)));
  }
  for (const auto& [old_key, new_key] : std::to_array<std::pair<const char*, const char*>>({
           {"CustomScale", "AntiAliasingRenderScale"},
           {"TextureLodBias", "AntiAliasingTextureLodBias"},
           {"EffectsAtOutputResolution", "AntiAliasingEffectsAtOutput"},
           {"LiveScale", "LiveScale"},
           {"CameraJitter", "CameraJitter"},
           {"ReplaceTAA", "ReplaceTAA"},
           {"SparkTrailsFix", "SparkTrailsFix"},
           {"GameSharpening", "GameSharpening"},
           {"AmbientOcclusion", "AmbientOcclusionHalo"},
           {"AOMethod", "ssao_mode"},
           {"AORadius", "ssao_radius"},
           {"AOStrength", "ssao_strength"},
           {"AOThickness", "ssao_thickness"},
           {"AOQuality", "ssao_quality"},
           {"AODirections", "ssao_directions"},
           {"RayTracedSunShadows", "RayTracedSunShadows"},
           {"RayTracedShadowSoftness", "RayTracedShadowSoftness"},
           {"RayTracedCharacterShadows", "RayTracedCharacterShadows"},
           {"RayTracedAmbientOcclusion", "RayTracedAmbientOcclusion"},
           {"RayTracedAORange", "RayTracedAORange"},
           {"RayTracedAORays", "RayTracedAORays"},
           {"RayTracedBounceStrength", "light_bounce_strength"},
           {"RayTracedBounceRange", "RayTracedLightBounceRange"},
           {"RayTracedBounceRays", "RayTracedLightBounceRays"},
           {"RayTracedReflections", "RayTracedReflections"},
           {"RayTracedSpecularOcclusion", "RayTracedSpecularOcclusion"},
           {"PathTracingBounces", "PathTracingBounces"},
           {"PathTracingSamples", "PathTracingSamples"},
       })) {
    if (float value = 0.f; read(old_key, &value) && std::isfinite(value)) values.emplace_back(new_key, value);
  }
  // The bounce light through rays (1), path traced (2: Path tracing, which replaced it).
  float bounce = 0.f;
  float paths = 0.f;
  const bool bounce_saved = read("RayTracedBounceLight", &bounce);
  if (read("PathTracing", &paths) || bounce_saved) values.emplace_back("RayTracedLightBounce", (paths != 0.f ? 2.f : (bounce != 0.f ? 1.f : 0.f)));
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
  renodx::utils::log::i("[Tales] Settings: ", values.size(), " values of [Tales AA] taken over by the page (SettingsRevision 1).");
}

// This game's page texts (graphics_page.hpp Texts): the game's TAA stays on for DLSS; the rays' sun shadows and AO with the
// game's (the darker of both, multiplied).
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = {"Game's TAA", "", "", "", "", "DLSS", "FSR", "XeSS"},
    .aa_tooltip = "Game's TAA: the game's own temporal anti-aliasing.\n"
                  "In its place, also setting the game's render scale: DLSS: NVIDIA's (RTX), the best. FSR: AMD's, FSR 4 on\n"
                  "Radeon RX 6000 and newer, else 3.1. XeSS: Intel's, any GPU.\n"
                  "Every ray-traced effect needs an upscaler. Needs renodx-tw-dlaa64.exe and its DLLs in the game folder.",
    .in_game_aa_note = "In-game Anti-Aliasing must be TAA for DLSS, FSR, XeSS and Frame Generation.",
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling at its default: Bend Screen-Space Shadows and\n"
                     "GT-VBGI, the game's ambient occlusion and reflections.",
    .no_rays_tooltip = "No ray tracing: Bend Screen-Space Shadows and GT-VBGI in place of the game's contact shadows and Global\n"
                       "Illumination, the game's ambient occlusion and reflections. The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows, ambient occlusion and reflections, path-traced Global Illumination at my settings. The\n"
                    "rest of this page at its default. Needs an upscaler (Anti-Aliasing above) and a DXR GPU; on NVIDIA also\n"
                    "turns DLSS Ray Reconstruction on.",
    .shadow_tooltip = "Game's Shadow Maps: the original. Bend Screen-Space Shadows: those plus fine sun shadows from what is on\n"
                      "screen (Bend Studio's; characters, grass, small details), in place of the game's contact\n"
                      "shadows toward the sun. DXR: buildings, terrain, rocks, leaves and grass also cast ray-traced sun\n"
                      "shadows, at any distance (the darker of the game's and the rays'). Heavy. Needs an upscaler and a DXR GPU.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep in-game ambient occlusion on.\n"
                  "Game's SSAO: the original. GT-VBAO: contact shading that lets light pass behind thin things (grass,\n"
                  "poles, characters) instead of dark halos.\n"
                  "RTAO: large-scale shading from buildings, terrain and rocks, in place of the game's. Heavy.\n"
                  "Path-Traced: with Path Tracing Global Illumination, the share of the sky's light that reaches each spot.\n"
                  "Needs an upscaler and a DXR GPU.",
    .ao_paths_label = "Path-Traced",
    .gi_tooltip_start = "Sunlit ground, walls and characters bounce their light and colour into nearby shadows.\n",
    .gi_tooltip_end = "\nGT-VBGI takes the place of the game's own Global Illumination.",
    .effects_at_output_tooltip = "While the upscaler renders below 100%, fog, light shafts, reflections, clouds, effects, skin and shadows\n"
                                 "keep their 100% detail. Looks better, can cost some performance.",
    .dlss_preset_tooltip = "NVIDIA's models: K the first transformer, M the second (default), L the sharpest and heaviest (small fast\n"
                           "particles can smear). A preset forced in the NVIDIA app or NVIDIA Profile Inspector overrides this one.",
    .reflections_tooltip = "Real reflections on wet, polished and smooth surfaces. Need in-game Screen Space Reflections on.\n"
                           "Game's SSR: the original, faked from the screen. DXR: from rays, also what is off screen. Needs an\n"
                           "upscaler and a DXR GPU.",
};
// The page's anti-aliasing: the game's TAA, DLSS, FSR, XeSS.
constexpr std::array<int, 4> AA_METHODS = {0, 5, 6, 7};
// Graphics 1 of 2's own section after graphics_page's: the live render scale, the camera jitter, the TAA pass, the
// details and the tools.
inline const std::string TOOLS_SECTION = "Upscaling and Tools";
inline const std::string GRAPHICS_PRESETS_SECTION = "Graphics Presets";

// Graphics 2 of 2's sections: the presets', then each group's (GRAPHICS_SETTINGS' order; shaders on load is a checkbox
// under the presets, its Loading group no section).
inline std::vector<std::string> EngineSections() {
  std::vector<std::string> sections = {GRAPHICS_PRESETS_SECTION};
  for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
    const char* group = GRAPHICS_SETTINGS[index].group;
    if (index != SHADERS_ON_LOAD && std::ranges::find(sections, group) == sections.end()) sections.emplace_back(group);
  }
  return sections;
}

// The page's rows (HDR part's ConfigureAddon, after its own rows): Graphics 1 of 2 (graphics_page.hpp with
// feature_settings::AppendAa's, rt_scene::AppendSettings' and GT-VBAO's rows, this game's own in their sections, then
// TOOLS_SECTION), Graphics 2 of 2 (GRAPHICS_SETTINGS, EngineSections). After feature_settings::SetTints.
inline void AppendPage(std::vector<renodx::utils::settings::Setting*>* settings) {
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
  // the helper's frame generation and Reflex; not the shader anti-aliasing (TW's), object motion (Unreal's own motion
  // vectors) or the Upscaling Debug View (no view of it here yet).
  twattila::feature_settings::AppendAa(settings, &aa_sliders, true, true);
  erase({"AntiAliasingQuality", "AntiAliasingExtraSharpness", "AntiAliasingTaaStrength", "AntiAliasingSharpening", "AntiAliasingObjectMotion",
         "AntiAliasingDebugView"});
  // The rays (talesofarise_dlss/rt_shadows.hpp: sun shadows, AO, the bounce light and its paths, reflections, specular
  // occlusion); not the light bounce's resolution (its own).
  twattila::rt_scene::AppendSettings(settings, &rt_sliders, &aa_sliders, false, true, twattila::feature_settings::AA_SECTION,
                                     &gi_rows.light_bounce_strength);
  erase({"RayTracedLightBounceResolution"});
  for (Setting* setting : *settings) {
    // The former overlay's defaults and ranges: preset M, the AO's 2 m, 1-8 bounce rays a pixel (1).
    if (setting->key == "AntiAliasingDlaaPreset") setting->default_value = 1.f;
    if (setting->key == "RayTracedAORange") setting->default_value = 2.f;
    if (setting->key == "RayTracedLightBounceRays") {
      setting->default_value = 1.f;
      setting->max = 8.f;
    }
  }
  // GT-VBAO's rows, for the HDR part's shaders through post_filters.hpp.
  const auto ambient_rows = talesofarise::hdr_only::AmbientOcclusionRows({
      .mode = &ao_method,
      .radius = &ao_radius,
      .strength = &ao_strength,
      .thickness = &ao_thickness,
      .quality = &ao_quality,
      .directions = &ao_directions,
      .auto_thickness = &ao_auto_thickness,
      .exact_depth = &ao_exact_depth,
  });
  settings->insert(settings->end(), ambient_rows.begin(), ambient_rows.end());
  // One frame's GPU time of every pass (twattila/pass_timing.hpp, the add-on's first draw hooks; the page places it).
  settings->push_back(new Setting{
      .value_type = SettingValueType::BUTTON,
      .label = "Time Each Pass (One Frame)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "The GPU's time of every pass of the next frame into talesofarise_pass_timing.txt next to the game's exe.",
      .on_change = [] { twattila::pass_timing::Request(); },
  });
  // GT-VBGI's rows (Global Illumination; its Strength also the rays' bounce light's), the screen-space search's off
  // under the rays; Bend's rows without those of the Total War games' own sun shaders.
  talesofarise::ue_light_bounce::AppendRows(settings, &gi_rows, [] { return rt_sliders.light_bounce != 0.f; });
  twattila::feature_settings::AppendSss(settings, &screen_space_shadows_rows);
  erase({"ScreenSpaceShadowsDistanceSoftness", "ScreenSpaceShadowsDebugView"});
  // Bend (Length 480, as No RT/PT) and GT-VBGI on by default (ATN 2026-11-16), in place of Bandai's contact shadows toward
  // the sun and its Global Illumination; before Apply (the page's Reset reads the defaults as it is built).
  for (Setting* setting : *settings) {
    if (setting->key == "ScreenSpaceShadows" || setting->key == "light_bounce_strength") {
      setting->default_value = 1.f;
    }
    if (setting->key == "ScreenSpaceShadowsLength") {
      setting->default_value = 3.f;
    }
  }
  twattila::graphics_page::Apply(settings, {.aa = &aa_sliders,
                                            .sss = &screen_space_shadows_rows,
                                            .rt = &rt_sliders,
                                            .ssao_mode = &ao_method,
                                            .light_bounce_strength = &gi_rows.light_bounce_strength,
                                            .texts = &PAGE_TEXTS,
                                            .aa_methods = AA_METHODS,
                                            .upscaler_status = &UpscalerStatus,
                                            .has_foliage_light = false,
                                            .path_ambient_tested = false,
                                            .rays_replace_ao = false,
                                            .rays_multiply_maps = true,
                                            .rays_tested = false,
                                            .has_fog_shadows = false,
                                            .every_row = false});
  // What the in-process ray tracing has no part of (the helper's: Rays a Pixel, its filters and caches, micromaps, its
  // debug tools): hidden, kept (graphics_page's presets name them).
  for (Setting* setting : *settings) {
    for (const std::string_view key : {"RayTracingOutputResolution", "RayTracingRaysShadows", "RayTracingRaysAmbient", "RayTracingRaysReflections",
                                       "RayTracingRaysPaths", "RayTracedColouredShadows", "RayTracedLeafLight", "RayTracedAOQuarter",
                                       "RayTracedPathSer", "RayTracedSharc", "RayTracedReuseLean", "RayTracedReuseNear", "RayTracedReuseBalanced", "RayTracedReflectionSamples",
                                       "RayTracedReflectionReuse", "RayTracingDenoiser", "RayTracedAoReblur", "RayTracedLeafMicromaps",
                                       "RayTracedGpuMicromaps", "RayTracedFastLeafShadows", "RayTracedDebugView", "RayTracedSceneTest",
                                       "RayTracedDebugLog"}) {
      if (setting->key == key) setting->is_visible = [] { return false; };
    }
    if (setting->key.empty() && setting->label == "Record Ray Tracing Profile (30 s)") setting->is_visible = [] { return false; };
  }
  // This game's own rows after the page's row `anchor` (its key, else its label), in its section, shown with it.
  const auto insert_after = [settings](std::string_view anchor, Setting* row) {
    const auto at = std::ranges::find_if(*settings, [anchor](const Setting* setting) {
      return setting->key == anchor || (setting->key.empty() && setting->label == anchor);
    });
    if (at == settings->end()) {
      renodx::utils::log::w("[Tales] Graphics page: no row ", std::string(anchor));
      settings->push_back(row);
      return;
    }
    row->section = (*at)->section;
    row->is_visible = [anchor_visible = (*at)->is_visible, own = row->is_visible] { return anchor_visible() && own(); };
    settings->insert(at + 1, row);
  };
  insert_after("Method##Ambient Occlusion", new Setting{
                                                .key = "AmbientOcclusionHalo",
                                                .binding = &ambient_occlusion,
                                                .value_type = SettingValueType::INTEGER,
                                                .default_value = 1.f,
                                                .label = "Ambient Occlusion Halo",
                                                .tooltip = "The game's ambient occlusion leaves a light ring around characters (most on plants).\n"
                                                           "Reduced: a smaller ring. Reduced + darker: also darker shading.",
                                                .labels = {"Game's", "Reduced", "Reduced + darker"},
                                                .is_visible = [] { return ao_method == 0.f; },
                                            });
  insert_after("RayTracedShadowSoftness", new Setting{
                                              .key = "RayTracedCharacterShadows",
                                              .binding = &character_shadows_row,
                                              .value_type = SettingValueType::BOOLEAN,
                                              .default_value = 1.f,
                                              .label = "Characters Cast Ray-Traced Shadows",
                                              .tooltip = "Characters' ray-traced shadows on the ground and walls.",
                                              .is_enabled = [] { return twattila::feature_settings::Upscaling(aa_sliders) && rt_sliders.sun_shadows != 0.f; },
                                          });
  insert_after("PathTracingBounces", new Setting{
                                         .key = "PathTracingSamples",
                                         .binding = &path_samples_row,
                                         .value_type = SettingValueType::INTEGER,
                                         .default_value = 1.f,
                                         .label = "Path Tracing Samples",
                                         .tooltip = "Paths a pixel (also the reflections' samples): less noise. Heavier.",
                                         .min = 1.f,
                                         .max = 4.f,
                                         .format = "%d",
                                         .is_enabled = [] { return twattila::feature_settings::Upscaling(aa_sliders) && rt_sliders.light_bounce == 2.f; },
                                     });
  insert_after("PathTracingSamples", new Setting{
                                          .key = "PathTracingReorder",
                                          .binding = &path_reorder_row,
                                          .value_type = SettingValueType::BOOLEAN,
                                          .default_value = 0.f,
                                          .label = "Path Tracing Reordering (Experimental)",
                                          .tooltip = "Regroups the paths by what they hit first (shader execution reordering, newest GPUs and drivers).\n"
                                                     "Same picture; faster or slower depending on the GPU.",
                                          .is_enabled = [] { return twattila::feature_settings::Upscaling(aa_sliders) && rt_sliders.light_bounce == 2.f; },
                                      });
  insert_after("PathTracingReorder", new Setting{
                                           .key = "RayTracingInHelper",
                                           .binding = &helper_rays_row,
                                           .value_type = SettingValueType::BOOLEAN,
                                           .default_value = 0.f,
                                           .label = "Ray Tracing in the Helper (Experimental)",
                                           .tooltip = "The ray-traced effects traced in the helper, as in the Total War games.\n"
                                                      "Restart the game after changing it.",
                                           .is_enabled = [] { return twattila::feature_settings::Upscaling(aa_sliders); },
                                       });
  insert_after("light_bounce_multi_bounce", new Setting{
                                                .key = "GlobalIlluminationGameOpenSky",
                                                .binding = &game_gi_open_sky_row,
                                                .value_type = SettingValueType::BOOLEAN,
                                                .default_value = 1.f,
                                                .label = "Game's GI Under the Open Sky",
                                                .tooltip = "Keeps the game's own Global Illumination where the sky is open (open ground, cliffs);\n"
                                                           "ours lights what nearby surfaces cover. Off: ours alone, darker in open areas.",
                                            });
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
          .tooltip = "The upscaler sets the game's render scale from the Upscaling Mode (Native AA 100%, the others their own).\n"
                     "Off puts the game's own value back. Every mode below 100% needs it.",
          .is_visible = upscaling,
      },
      new Setting{
          .key = "CameraJitter",
          .binding = &camera_jitter_row,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Camera Jitter",
          .section = TOOLS_SECTION,
          .tooltip = "The game turns off the camera's sub-pixel jitter, which DLAA / DLSS need for fine detail and to keep small\n"
                     "particles from smearing. On: turned on while DLAA / DLSS is on (NVIDIA's Halton pattern, 8 x (output /\n"
                     "input)^2 positions: 8 for DLAA), the game's own while Anti-Aliasing is the game's TAA.",
          .is_visible = upscaling,
      },
      new Setting{
          .key = "ReplaceTAA",
          .binding = &replace_taa_row,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Replace the Game's TAA Pass (Faster)",
          .section = TOOLS_SECTION,
          .tooltip = "DLSS skips the game's TAA pass entirely. Off: the game's TAA runs first and DLAA / DLSS is written over it.",
          .is_visible = upscaling,
      },
      new Setting{
          .key = "SparkTrailsFix",
          .binding = &spark_fix_row,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Spark Trails Fix",
          .section = TOOLS_SECTION,
          .tooltip = "The game's sparks and embers have no motion, so DLSS can leave trails behind them (most of all preset L).\n"
                     "On: sparks get their own motion (DLSS keeps them sharp); where one was and for other effects, DLSS shows\n"
                     "the current frame.",
          .is_visible = upscaling,
      },
      new Setting{
          .key = "GameSharpening",
          .binding = &game_sharpening_row,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Game's Sharpening (Not Recommended)",
          .section = TOOLS_SECTION,
          .tooltip = "Bandai's sharpening before DLAA / DLSS. Off (default): DLSS alone, without dark and bright edges around\n"
                     "characters.",
          .is_visible = upscaling,
      },
      // What the upscaler did (the timings measured only while this is drawn).
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Upscaling Details",
          .section = TOOLS_SECTION,
          .on_draw = [] {
            using Phase = talesofarise::engine_scale::Finder::Phase;
            {
              const std::shared_lock lock(state.mutex);
              overlay_frame.store(state.frame + 1);
            }
            talesofarise::dlss::constants::SetCounting(true);
            uint64_t completed;
            uint32_t temporal_shader_hash;
            uint64_t copied_per_frame;
            uint64_t biased_per_frame;
            std::string constants;
            talesofarise::dlss::PerformanceSummary performance;
            std::array<uint32_t, 4> extents;
            std::array<uint32_t, 4> expected_extents;
            {
              const std::shared_lock lock(state.mutex);
              completed = state.completed;
              temporal_shader_hash = state.temporal_shader_hash;
              copied_per_frame = state.copied_per_frame;
              biased_per_frame = state.biased_per_frame;
              constants = state.constants;
              performance = state.performance;
              extents = {state.render_width, state.render_height, state.width, state.height};
              expected_extents = {state.expected_width, state.expected_height, state.width, state.height};
            }
            const auto mode = requested_mode.load();
            if (mode != AAMode::OFF && mode != AAMode::DLAA && expected_extents[0] != 0 && expected_extents[2] != 0) {
              ImGui::Text("Render size %ux%u (%.1f%%), same aspect ratio as the screen", expected_extents[0], expected_extents[1],
                          100. * expected_extents[1] / expected_extents[3]);
            }
            if (live.enabled && live.problem.empty() && live.finder.phase() == Phase::FOUND) {
              ImGui::TextWrapped("Render scale: %g%%%s", live.finder.Get().value_or(0), live.note.empty() ? "" : ("; " + live.note).c_str());
            } else if (live.enabled && live.problem.empty() && live.finder.phase() != Phase::FAILED) {
              ImGui::TextWrapped("%s...", live.finder.status().c_str());
            }
            if (camera_jitter.halton_branch.applied) {
              ImGui::Text("Camera jitter: on, NVIDIA's Halton (2, 3) pattern, %u positions", camera_jitter.halton_positions);
            } else if (camera_jitter.patch.applied) {
              ImGui::TextUnformatted("Camera jitter: on, the game's pattern (8 positions)");
            } else if (camera_jitter.patch.address != nullptr || !camera_jitter.enabled) {
              ImGui::TextUnformatted("Camera jitter: off (the game's)");
            }
            ImGui::Text("Successful upscaled frames: %llu", static_cast<unsigned long long>(completed));
            if (completed != 0) {
              ImGui::Text("TAA shader: 0x%08X", temporal_shader_hash);
              const double area_ratio = (extents[0] == 0 || extents[1] == 0
                                             ? 1.
                                             : static_cast<double>(extents[2]) * extents[3] / (static_cast<double>(extents[0]) * extents[1]));
              const uint32_t phases = diagnostics.jitter_phases.load();
              ImGui::Text("Camera jitter pattern: %s positions (NVIDIA recommends %.0f or more)",
                          phases != 0 ? std::to_string(phases).c_str() : "measuring", std::ceil(8. * area_ratio));
              std::array<uint32_t, 2> post_counts;
              {
                const std::shared_lock lock(post.mutex);
                post_counts = post.last;
              }
              if (extents[0] != extents[2] || extents[1] != extents[3]) {
                ImGui::Text("Post-processing at the output size: %u passes, upscale %s", post_counts[0],
                            post_counts[1] != 0 ? "replaced" : "not replaced (render size, softer)");
              }
              if (applied_mip_bias.width != 0) {
                ImGui::Text("Texture LOD bias: %.2f in %llu view uploads per frame", applied_mip_bias.bias,
                            static_cast<unsigned long long>(biased_per_frame));
              }
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
                ImGui::Text("GPU game TAA: %.3f ms", performance.gpu_ms[0]);
                ImGui::Text("GPU input preparation: %.3f ms", performance.gpu_ms[1]);
                ImGui::Text("GPU helper evaluation: %.3f ms", performance.gpu_ms[2]);
                ImGui::Text("GPU merge + copy (none when replacing): %.3f ms", performance.gpu_ms[3]);
              } else {
                ImGui::TextUnformatted("GPU timing samples pending or unavailable; AA is unaffected.");
              }
              ImGui::Text("Timing samples: CPU %llu / GPU %llu; skipped %llu / rejected %llu",
                          static_cast<unsigned long long>(performance.cpu_samples), static_cast<unsigned long long>(performance.gpu_samples),
                          static_cast<unsigned long long>(performance.dropped), static_cast<unsigned long long>(performance.rejected));
              ImGui::TextWrapped("Averages since choosing this mode, measured only while this is shown. CPU and GPU times overlap: do not add them together.");
            }
            ImGui::TextWrapped("%s", talesofarise::engine_hooks::SunSteps().c_str());
            return false;
          },
          .is_visible = upscaling,
      },
      // Pixel trace (pixel_trace.hpp): a red cross marks the pixel, the red line the traced line through it.
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Trace These Pixels",
          .section = TOOLS_SECTION,
          .on_draw = [] {
            ImGui::SliderFloat2("Traced pixel (% of width, height)", traced_pixel.data(), 0.f, 100.f, "%.1f%%");
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            const ImVec2 cross = {screen.x * traced_pixel[0] / 100.f, screen.y * traced_pixel[1] / 100.f};
            const float half_line = screen.x * talesofarise::pixel_trace::LINE_WIDTH * 0.5f;
            ImDrawList* foreground = ImGui::GetForegroundDrawList(nullptr);
            foreground->AddLine({cross.x - half_line, cross.y}, {cross.x + half_line, cross.y}, IM_COL32(255, 0, 0, 255), 1.f);
            foreground->AddLine({cross.x, cross.y - 12.f}, {cross.x, cross.y + 12.f}, IM_COL32(255, 0, 0, 255), 2.f);
            if (ImGui::Button("Trace these pixels for one frame")) {
              talesofarise::pixel_trace::Request(traced_pixel[0] / 100.f, traced_pixel[1] / 100.f);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Saves every pass that changes the pixels under the red line during the next frame\n"
                                "to tales_pixel_trace.txt next to the game's exe (one short hitch).");
            }
            ImGui::TextWrapped("%s", talesofarise::pixel_trace::Status().c_str());
            return false;
          },
      },
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Trace One Frame",
          .section = TOOLS_SECTION,
          .on_draw = [] {
            if (ImGui::Button("Trace one frame")) talesofarise::frame_trace::Request();
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Saves every draw, dispatch, clear and copy of the next frame to tales_frame_trace.txt\n"
                                "next to the game's exe (one short hitch).");
            }
            ImGui::TextWrapped("%s", talesofarise::frame_trace::Status().c_str());
            if (ImGui::Button("Trace ray-traced shadows for one frame")) talesofarise::rt_shadows::RequestTrace();
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Saves what the ray-traced sun shadows did in the next frame (meshes, sun direction)\n"
                                "to tales_rt_trace.txt next to the game's exe.");
            }
            if (ImGui::Button("Save the game's code for analysis (debug, about 80 MB)")) {
              std::wstring exe(32768, L'\0');
              exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
              saved_image = talesofarise::code_patch::SaveImage(std::filesystem::path(exe).parent_path() / L"tales_image.bin");
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Saves the game's program as loaded in memory to tales_image.bin next to the game's exe.");
            }
            if (!saved_image.empty()) ImGui::TextWrapped("%s", saved_image.c_str());
            return false;
          },
      },
  });

  // Graphics 2 of 2: the presets (every setting but shaders on load gets the preset's value, KEEP: the game's, so none keeps
  // an older choice), the three toggles that change what every preset sets, shaders on load and the taste settings, then each
  // group's settings in its own section with their saved choices.
  talesofarise::graphics_table::Prepare(GRAPHICS_SECTION, GRAPHICS_SETTINGS, graphics.entries);
  const auto is_cpu_heavy = [](const GraphicsSetting& setting) {
    return std::ranges::find(CPU_HEAVY_SETTINGS, std::wstring_view(setting.name)) != CPU_HEAVY_SETTINGS.end();
  };
  const auto far_sun_shadow = [](const GraphicsSetting& setting) {
    return std::ranges::find(FAR_SUN_SHADOWS, std::wstring_view(setting.name), &PresetOverride::name);
  };
  const auto max_fog = [](const GraphicsSetting& setting) {
    return std::ranges::find(MAX_VOLUMETRIC_FOG, std::wstring_view(setting.name), &PresetOverride::name);
  };
  // The choice holding `value` (KEEP or none: the game's), saved.
  const auto set_choice = [](GraphicsEntry* entry, float value) {
    const auto found = std::ranges::find(entry->choices, value);
    entry->choice = (found == entry->choices.end() ? 0 : static_cast<int>(found - entry->choices.begin()) + 1);
    talesofarise::graphics_table::SaveChoice(GRAPHICS_SECTION, *entry);
  };
  const auto apply_preset = [is_cpu_heavy, far_sun_shadow, max_fog, set_choice](size_t preset_index) {
    for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
      const auto& setting = GRAPHICS_SETTINGS[index];
      const auto preset_values = std::span(setting.values).first(MAXIMUM_PRESET);
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
      if (weak_cpu && is_cpu_heavy(setting)) value = KEEP;
      if (const auto sun_override = far_sun_shadow(setting); far_sun_shadows && !sun_distance_limited && sun_override != FAR_SUN_SHADOWS.end()) {
        value = sun_override->value;
      }
      if (sun_distance_limited && index == SUN_SHADOW_DISTANCE) value = std::min(value, SUN_DISTANCE_GAME_LIMIT);  // KEEP (NaN) stays
      if (const auto fog_override = max_fog(setting); max_volumetric_fog && fog_override != MAX_VOLUMETRIC_FOG.end()) value = fog_override->value;
      set_choice(&entry, value);
    }
    // Bandai's ambient occlusion: the game's in Game's own, reduced + darker in the others.
    renodx::utils::settings::UpdateSettings({{"AmbientOcclusionHalo", (preset_index == 0 ? 0.f : 2.f)}});
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
                  .label = "These change what every preset sets. Ticking one applies it now; unticking keeps it until you pick a preset.",
                  .section = GRAPHICS_PRESETS_SECTION,
                  .tint = 0xFFD23C},
      // The three toggles, Shaders on load and the taste settings: [Tales AA]'s keys as before (their rows save them).
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Preset Toggles",
          .section = GRAPHICS_PRESETS_SECTION,
          .on_draw = [is_cpu_heavy, far_sun_shadow, max_fog, set_choice] {
            if (ImGui::Checkbox("Vanilla crowd limits (CPU saver, BUT not recommended)", &weak_cpu)) {
              reshade::set_config_value(nullptr, "Tales AA", "WeakCPU", weak_cpu);
              for (size_t index = 0; weak_cpu && index < GRAPHICS_SETTINGS.size(); ++index) {
                if (is_cpu_heavy(GRAPHICS_SETTINGS[index])) set_choice(&graphics.entries[index], KEEP);
              }
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("On: presets keep the game's limits for characters behind the camera and hidden characters.\n"
                                "Saves CPU time in crowds; characters can fade in again when you turn around.");
            }
            ImGui::BeginDisabled(sun_distance_limited);
            if (ImGui::Checkbox("Far sun shadows (mountains)", &far_sun_shadows)) {
              reshade::set_config_value(nullptr, "Tales AA", "FarSunShadows", far_sun_shadows);
              for (size_t index = 0; far_sun_shadows && index < GRAPHICS_SETTINGS.size(); ++index) {
                if (const auto sun_override = far_sun_shadow(GRAPHICS_SETTINGS[index]); sun_override != FAR_SUN_SHADOWS.end()) {
                  set_choice(&graphics.entries[index], sun_override->value);
                }
              }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
              ImGui::SetTooltip("On: presets set Sun shadow distance 3 and resolution 8192, so sunlight no longer shines\n"
                                "through far mountains. Shadows near you get a bit softer.%s",
                                (sun_distance_limited ? "\n\nNeeds Arise-SDK." : ""));
            }
            if (ImGui::Checkbox("Max volumetric fog", &max_volumetric_fog)) {
              reshade::set_config_value(nullptr, "Tales AA", "MaxVolumetricFog", max_volumetric_fog);
              for (size_t index = 0; max_volumetric_fog && index < GRAPHICS_SETTINGS.size(); ++index) {
                if (const auto fog_override = max_fog(GRAPHICS_SETTINGS[index]); fog_override != MAX_VOLUMETRIC_FOG.end()) {
                  set_choice(&graphics.entries[index], fog_override->value);
                }
              }
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("On: presets set the finest volumetric lighting and fog: smoother fog and light shafts.\n"
                                "Very heavy without DLSS.");
            }
            ImGui::NewLine();
            auto& shaders = graphics.entries[SHADERS_ON_LOAD];
            const auto one = std::ranges::find(shaders.choices, 1.f);
            const auto zero = std::ranges::find(shaders.choices, 0.f);
            const int on_choice = static_cast<int>(one - shaders.choices.begin()) + 1;
            const int off_choice = static_cast<int>(zero - shaders.choices.begin()) + 1;
            ImGui::BeginDisabled(shaders.cvar.values[0] == nullptr || one == shaders.choices.end() || zero == shaders.choices.end());
            if (bool on = shaders.choice == on_choice; ImGui::Checkbox("Shaders on load", &on)) {
              shaders.choice = (on ? on_choice : off_choice);
              talesofarise::graphics_table::SaveChoice(GRAPHICS_SECTION, shaders);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
              ImGui::SetTooltip("On: builds an area's shaders while it loads: fewer stutters when new effects first appear.\n"
                                "Loading takes a bit longer. Applies after loading a save.");
            }
            const auto& sun = talesofarise::engine_hooks::options[talesofarise::engine_hooks::SMOOTH_SUN];
            const auto& speed = talesofarise::engine_hooks::options[talesofarise::engine_hooks::DAY_SPEED];
            ImGui::BeginDisabled(!sun.available);
            if (ImGui::Checkbox("Smooth sun movement (Recommended)", &smooth_sun)) {
              reshade::set_config_value(nullptr, "Tales AA", "SmoothSun", smooth_sun);
              ApplyTasteSettings();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
              ImGui::SetTooltip("The game turns the sun in steps every few seconds. On: it turns a bit every frame,\n"
                                "so its shadows and its disk in the sky no longer jump.%s",
                                (sun.available ? "" : ("\n\nUnavailable: " + sun.details).c_str()));
            }
            ImGui::BeginDisabled(!speed.available);
            if (ImGui::SliderInt("Day and night speed", &day_speed, 0, static_cast<int>(DAY_SPEEDS.size()) - 1, DAY_SPEED_LABELS[day_speed],
                                 ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
              reshade::set_config_value(nullptr, "Tales AA", "DaySpeedMultiplier", DAY_SPEEDS[day_speed]);
              ApplyTasteSettings();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
              ImGui::SetTooltip("How fast day turns into night where the game has a day cycle (the sun moves with it).\n"
                                "Slower: longer days and nights. Faster: a time-lapse. Off: the time of day stands still.%s",
                                (speed.available ? "" : ("\n\nUnavailable: " + speed.details).c_str()));
            }
            ImGui::NewLine();
            if (!graphics.status.empty()) ImGui::TextWrapped("%s", graphics.status.c_str());
            return false;
          },
      },
      new Setting{.value_type = SettingValueType::TEXT,
                  .label = "Some settings apply after loading a new area, anisotropic filtering to textures loaded afterwards. Maximum is "
                           "very heavy and uses a lot of VRAM; Beyond Maximum also pushes outline distance, NPC distance, monster "
                           "distance, NPC / monster detail distance, terrain detail and grass draw distance to their highest values.",
                  .section = GRAPHICS_PRESETS_SECTION},
      new Setting{.value_type = SettingValueType::TEXT,
                  .label = "Every preset keeps the game's volumetric fog (the most expensive effect) unless Max volumetric fog is on: "
                           "finer, cheaper or off in its own settings.",
                  .section = GRAPHICS_PRESETS_SECTION,
                  .tint = 0xFFD23C},
      new Setting{.value_type = SettingValueType::TEXT,
                  .label = "Best used together with Arise-SDK (www.nexusmods.com/talesofarise/mods/5). The \"Arise-SDK's settings\" "
                           "group, Far sun shadows and Sun shadow distance above 2 need it (greyed out without it); the group changes "
                           "Arise-SDK's own settings.",
                  .section = GRAPHICS_PRESETS_SECTION,
                  .tint = 0xFFD23C},
  });
  const auto fog_off = [] {
    const auto& volumetric_fog = graphics.entries[VOLUMETRIC_FOG].cvar;
    return volumetric_fog.values[0] != nullptr && volumetric_fog.Get() == 0.f;
  };
  for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
    const auto& setting = GRAPHICS_SETTINGS[index];
    if (index == SHADERS_ON_LOAD) continue;  // its checkbox is under the presets
    if (index == 0 || std::string_view(GRAPHICS_SETTINGS[index - 1].group) != setting.group) {
      if (std::string_view(setting.group) == ARISE_SDK_GROUP) {
        settings->push_back(new Setting{.value_type = SettingValueType::TEXT,
                                        .label = "Ideas from Arise-SDK (thanks to emoose). They need Arise-SDK installed and change its own settings.",
                                        .section = setting.group});
      }
    }
    const bool fog_cell = std::wstring_view(setting.name).starts_with(L"r.VolumetricFog.");
    settings->push_back(talesofarise::graphics_table::Row(setting, &graphics.entries[index], GRAPHICS_SECTION, setting.group,
                                                          fog_cell ? std::function<bool()>(fog_off) : std::function<bool()>()));
  }
}

// The former add-on's AddonInit (the HDR part registered the add-on; its shader mod attached state, command_action and,
// with twattila::draw_dispatch, the fast dispatch, post_filters' shared block too). `module`: this add-on's.
inline bool Install(HMODULE module) {
  if (attached) return true;
  // [Tales AA]'s own keys (the page's rows hold the rest, read at each present): the preset toggles, the taste settings,
  // Arise-SDK's switch.
  reshade::get_config_value(nullptr, "Tales AA", "WeakCPU", weak_cpu);
  reshade::get_config_value(nullptr, "Tales AA", "MaxVolumetricFog", max_volumetric_fog);
  reshade::get_config_value(nullptr, "Tales AA", "FarSunShadows", far_sun_shadows);
  reshade::get_config_value(nullptr, "Tales AA", "SmoothSun", smooth_sun);
  if (float speed = 1.f; reshade::get_config_value(nullptr, "Tales AA", "DaySpeedMultiplier", speed)) {
    day_speed = static_cast<int>(std::ranges::min_element(DAY_SPEEDS, {}, [&](float candidate) { return std::abs(candidate - speed); })
                                 - DAY_SPEEDS.begin());
  }
  reshade::get_config_value(nullptr, "Tales AA", "DebugDevArise", debug_dev_arise);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  for (const uint32_t shader_hash : TEMPORAL_SHADERS) {
    renodx::utils::command_action::Register(OnCommand, {
                                                           .shader_hash = shader_hash,
                                                           .command_types = renodx::utils::command_action::COMMAND_TYPE_DISPATCH,
                                                       });
  }
  for (const uint32_t shader_hash : POST_SHADERS) {
    renodx::utils::command_action::Register(OnPostPass, {
                                                            .shader_hash = shader_hash,
                                                            .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                        });
  }
  renodx::utils::command_action::Register(OnUpscale, {
                                                         .shader_hash = UPSCALE_SHADER,
                                                         .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                     });
  renodx::utils::command_action::Register(OnTranslucencyComposite, {
                                                                       .shader_hash = TRANSLUCENCY_COMPOSITE_SHADER,
                                                                       .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                                   });
  for (const auto& shaders : {std::span<const uint32_t>(LIGHTING_SHADERS), std::span<const uint32_t>(TOON_SHADERS)}) {
    for (const uint32_t lighting_shader : shaders) {
      renodx::utils::command_action::Register(OnLightingDraw, {
                                                                  .shader_hash = lighting_shader,
                                                                  .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                              });
    }
  }
  for (const auto& [sprite_shader, motion_shader] : SPRITE_SHADERS) {
    renodx::utils::command_action::Register(OnSpriteDraw, {
                                                              .shader_hash = sprite_shader,
                                                              .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                          });
  }
  for (const auto& depth_shader : talesofarise::rt_shadows::DEPTH_SHADERS) {
    renodx::utils::command_action::Register(OnDepthPrepass, {
                                                                .shader_hash = depth_shader.hash,
                                                                .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                            });
  }
  for (const auto& masked_shader : talesofarise::rt_shadows::MASKED_SHADERS) {
    if (masked_shader.opacity == talesofarise::rt_shadows::Opacity::NEVER) continue;
    renodx::utils::command_action::Register(OnDepthPrepass, {
                                                                .shader_hash = masked_shader.hash,
                                                                .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                            });
  }
  // The alpha-tested draws' stream output is made from their own vertex shader's bytecode, kept from its creation.
  renodx::utils::pipeline::EnableSubobjectCaching();
  for (const uint32_t shader_hash : GAME_AO_SHADERS) {
    renodx::utils::command_action::Register(OnGameAo, {
                                                          .shader_hash = shader_hash,
                                                          .command_types = renodx::utils::command_action::COMMAND_TYPE_DISPATCH,
                                                      });
  }
  // Ray Tracing in the Helper: installed when on at launch with the helper there and an effect it traces on.
  // Specular Occlusion counts here though it traces nothing alone: else a row turned on later ran in process until a restart.
  helper_rays_installed = twattila::dlaa_bridge::HelperInstalled() && helper_rays_row != 0.f
                          && (rt_sliders.sun_shadows != 0.f || rt_sliders.ambient_occlusion != 0.f || rt_sliders.reflections != 0.f
                              || rt_sliders.specular_occlusion != 0.f || rt_sliders.light_bounce != 0.f);
  if (helper_rays_installed) talesofarise::dlss::ray_tracing::Install();
  renodx::utils::command_action::Register(OnSunShadowMask, {
                                                               .shader_hash = talesofarise::rt_shadows::SUN_SHADOW_SHADER,
                                                               .command_types = renodx::utils::command_action::COMMAND_TYPE_DISPATCH,
                                                           });
  talesofarise::dlss::constants::Use(DLL_PROCESS_ATTACH);
  std::wstring module_path(32768, L'\0');
  module_path.resize(GetModuleFileNameW(module, module_path.data(), static_cast<DWORD>(module_path.size())));
  // Ray tracing's Agility SDK (DXR 1.2) in the D3D12 folder next to the add-on. Its factory takes a char path: a folder
  // name outside the ANSI code page goes without it (the conversion throws std::system_error).
  try {
    talesofarise::rt_shadows::agility_folder = (std::filesystem::path(module_path).parent_path() / L"D3D12" / L"").string();
  } catch (const std::system_error&) {
    talesofarise::rt_shadows::agility_folder.clear();
    renodx::utils::log::w("[Tales DLSS] The add-on's folder name has characters outside the system's code page: ray tracing without the Agility SDK (DXR 1.1).");
  }
  // Engine hooks: installed now, while the game is still creating its device (no level runs yet).
  talesofarise::engine_hooks::Install();
  ApplyTasteSettings();
  for (const auto& option : talesofarise::engine_hooks::options) {
    renodx::utils::log::i("[Tales engine hooks] ", std::filesystem::path(option.name).string(), ": ", option.details, ".");
  }
  renodx::utils::log::i("[Tales engine hooks] Sun shadow distance limit: ",
                        (talesofarise::engine_hooks::sun_distance_limit.applied ? "removed " : "kept "),
                        talesofarise::engine_hooks::sun_distance_limit.details, ".");
  attached = true;
  renodx::utils::log::i("[Tales DLSS] The helper's DLAA / DLSS, FSR and XeSS (Native AA, Ultra Quality/Quality/Balanced/Performance/Ultra Performance/Custom 1-100%) on UE 4.21 TAA 0x88765764 / 0xE2AF861D, Ray Reconstruction, frame generation; motion blur and tonemapper at the output size; aspect-exact render sizes; camera jitter; graphics presets.");
  return true;
}

// The former add-on's AddonUninit, before the HDR part detaches its shader mod.
inline void Uninstall() {
  if (attached) {
    renodx::utils::command_action::Unregister(OnCommand);
    renodx::utils::command_action::Unregister(OnPostPass);
    renodx::utils::command_action::Unregister(OnUpscale);
    renodx::utils::command_action::Unregister(OnTranslucencyComposite);
    renodx::utils::command_action::Unregister(OnSpriteDraw);
    renodx::utils::command_action::Unregister(OnLightingDraw);
    renodx::utils::command_action::Unregister(OnDepthPrepass);
    renodx::utils::command_action::Unregister(OnSunShadowMask);
    renodx::utils::command_action::Unregister(OnGameAo);
    if (helper_rays_installed) talesofarise::dlss::ray_tracing::Uninstall();
    talesofarise::dlss::constants::Use(DLL_PROCESS_DETACH);
    talesofarise::post_filters::Store({});
    reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
    reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
    reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
    reshade::unregister_event<reshade::addon_event::present>(OnPresent);
    talesofarise::pixel_trace::Stop();
    talesofarise::frame_trace::Stop();
    // The multiply only while the jmp skips it.
    talesofarise::code_patch::ApplyCodePatch(&camera_jitter.halton_branch, false);
    if (!camera_jitter.halton_branch.applied) {
      talesofarise::code_patch::ApplyCodePatch(&camera_jitter.halton_multiply, false);
    }
    if (!camera_jitter.halton_multiply.applied) {
      talesofarise::code_patch::ApplyCodePatch(&camera_jitter.patch, false);
    }
    for (auto& entry : graphics.entries) {
      if (entry.controlling) entry.cvar.Set(entry.game_value);
      entry.controlling = false;
    }
    graphics.console.Refresh();
    talesofarise::engine_hooks::Uninstall();
    Reset();
    attached = false;
  }
  twattila::dlaa_bridge::Shutdown();
}

}  // namespace talesofarise::dlss_graphics
