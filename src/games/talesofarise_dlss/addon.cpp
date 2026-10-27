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
#include <numeric>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/state.hpp"
#include "./constant_mirror.hpp"
#include "./constant_readback.hpp"
#include "./code_patch.hpp"
#include "./engine_cvars.hpp"
#include "./engine_scale.hpp"
#include "./native_state.hpp"
#include "./ngx_backend.hpp"
#include "./rr_d3d12.hpp"
#include "./performance.hpp"
#include "./frame_trace.hpp"
#include "./pixel_trace.hpp"
#include "./engine_hooks.hpp"
#include "./frame_generation.hpp"
#include "./post_filters.hpp"
#include "./rt_shadows.hpp"

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
  float live_percentage = 0;  // render scale before it is snapped to the output's aspect ratio; Custom uses its slider
};

constexpr std::array<ModeSettings, 8> AA_MODES = {{
    {.label = "Off (game TAA)", .name = "Off", .minimum_scale = 0, .maximum_scale = 0, .backend = talesofarise::dlss::Mode::DLAA},
    {.label = "DLAA (100%)", .name = "DLAA", .minimum_scale = 1, .maximum_scale = 1, .backend = talesofarise::dlss::Mode::DLAA, .live_percentage = 100},
    {.label = "DLSS Quality (67%)", .name = "DLSS Quality", .minimum_scale = .64f, .maximum_scale = .70f, .backend = talesofarise::dlss::Mode::DLSS_QUALITY, .live_percentage = 200.f / 3.f},
    {.label = "DLSS Balanced (58%)", .name = "DLSS Balanced", .minimum_scale = .56f, .maximum_scale = .60f, .backend = talesofarise::dlss::Mode::DLSS_BALANCED, .live_percentage = 58},
    {.label = "DLSS Performance (50%)", .name = "DLSS Performance", .minimum_scale = .50f, .maximum_scale = .52f, .backend = talesofarise::dlss::Mode::DLSS_PERFORMANCE, .live_percentage = 50},
    {.label = "DLSS Custom (1-100%)", .name = "DLSS Custom", .minimum_scale = .01f, .maximum_scale = 1, .backend = talesofarise::dlss::Mode::DLSS_CUSTOM},
    {.label = "DLSS Ultra Performance (33%)", .name = "DLSS Ultra Performance", .minimum_scale = .32f, .maximum_scale = .35f, .backend = talesofarise::dlss::Mode::DLSS_ULTRA_PERFORMANCE, .live_percentage = 100.f / 3.f},
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
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_M, .label = "M (Transformer 2, recommended, default)"},
    // L: small fast particles (sparks) smear (ATN debug20); NVIDIA's bias mask changes nothing with L or M (DLSS v81-v82).
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_L, .label = "L (Transformer 2, Slowest, sharpest, small particles can smear)"},
}};

// Menu order; AAMode values stay as saved in ReShade.ini.
constexpr std::array<AAMode, 8> MENU_ORDER = {
    AAMode::OFF, AAMode::DLAA, AAMode::DLSS_ULTRA_QUALITY, AAMode::DLSS_QUALITY, AAMode::DLSS_BALANCED,
    AAMode::DLSS_PERFORMANCE, AAMode::DLSS_ULTRA_PERFORMANCE, AAMode::DLSS_CUSTOM};

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
constexpr uint32_t VIEW_WORLD_CAMERA_ORIGIN = 59;
constexpr uint32_t VIEW_CLIP_TO_PREV_CLIP = 114;
constexpr uint32_t VIEW_TEMPORAL_AA_JITTER = 118;
constexpr uint32_t VIEW_SIZE_AND_INV_SIZE = 122;
// View.MaterialTextureMipBias (.x): the bias of every material texture sample (SampleBias with cb1[133].x in 774 of the
// dumped pixel shaders' samples, cb0[133].x in 53). The game leaves it 0 (UE sets it only for its temporal upscaling).
constexpr uint32_t VIEW_MATERIAL_TEXTURE_MIP_BIAS = 133;

// Texture LOD bias while DLSS renders below 100%: 0 the game's (none), 1 matched (log2(input / output)), 2 one step
// sharper (NVIDIA's), 3 two steps sharper.
int texture_lod_bias = 2;
// Bandai's sharpening before the TAA (HDR addon, post_filters.hpp): off under DLAA / DLSS unless kept.
bool keep_game_sharpening = false;
// Spark trails fix (the input preparation's particle mask, 0x7A1E0D01): pixels with small bright particles, now or last
// frame, get no DLSS history.
std::atomic<bool> spark_fix = true;
// DLSS Ray Reconstruction (Experimental): NVIDIA's denoising upscaler with the G-buffer's guides, and its preset
// (NVSDK_NGX_RayReconstruction_Hint_Render_Preset: 0 NVIDIA's, 4 D, 5 E, 6 F: DLSS 310.9's new transformer model).
std::atomic<bool> ray_reconstruction = false;
std::atomic<uint32_t> ray_reconstruction_preset = 0;
// DLSS Frame Generation (Experimental, frame_generation.hpp): set at launch (config FrameGeneration).
bool frame_generation = false;
// Bandai's ambient occlusion (HDR addon, post_filters.hpp): 0 the game's, 1 reduced light ring around characters and
// objects, 2 the same + darker (ATN's pick of the tests: samples and blur cutoffs x 1/4).
int ambient_occlusion = 1;
constexpr std::array<const char*, 3> AMBIENT_OCCLUSION_LABELS = {"Game's", "Reduced", "Reduced + darker"};
// Ambient occlusion GT-VBAO in place of the game's (HDR addon's bitmask_ao.hlsli, post_filters.hpp): 0 the game's,
// 1 GT-VBAO; its radius and thickness (metres), strength (an exponent), 2^directions and 8 x 2^quality samples.
int ao_method = 0;
float ao_radius = 1.5f;
float ao_strength = 1.f;
float ao_thickness = 0.1f;
int ao_directions = 1;
int ao_quality = 1;
uint32_t ao_frame = 0;
constexpr std::array<const char*, 2> AO_METHOD_LABELS = {"Game's", "GT-VBAO"};
constexpr std::array<const char*, 4> AO_QUALITY_LABELS = {"8", "16", "32", "64"};
constexpr std::array<const char*, 4> AO_DIRECTION_LABELS = {"1", "2", "4", "8"};
talesofarise::dlss::constants::ViewMipBias applied_mip_bias;  // the one the constant mirror writes

struct Texture {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

struct Runtime {
  talesofarise::dlss::Backend backend;
  // Ray Reconstruction (D3D12; NVIDIA stays initialized as the backend does); after the backend: released before it.
  talesofarise::rr_d3d12::Reconstruction reconstruction;
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
  Texture depth;
  Texture motion;
  Texture color;   // replacing: cleaned input; DLAA writes the game's output
  Texture output;  // otherwise: NVIDIA's output (DLAA: merged with stock TAA's alpha; DLSS: output size)
  Texture merged;
  // Spark trails fix: this frame's particle mask and the last one's (they alternate).
  std::array<Texture, 2> particle_masks;
  // Ray Reconstruction's guides (0x7A1E0D31) at the render size.
  ComPtr<ID3D11ComputeShader> guide_shader;
  ComPtr<ID3D11Buffer> guide_constants;
  Texture diffuse_albedo;
  Texture specular_albedo;
  Texture normals;
  Texture roughness;
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
struct GBufferCopy {
  std::shared_mutex mutex;
  std::array<ComPtr<ID3D11Texture2D>, 3> textures;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> srvs;
  uint64_t frame = UINT64_MAX;
} gbuffer;

// Frame generation's inputs from this frame's DLSS evaluation, submitted at present.
struct FrameGenerationFrame {
  std::shared_mutex mutex;
  talesofarise::frame_generation::Inputs inputs;
  uint64_t frame = UINT64_MAX;
} frame_generation_frame;

std::atomic<AAMode> requested_mode = AAMode::OFF;
std::atomic<uint32_t> preset = NVSDK_NGX_DLSS_Hint_Render_Preset_M;
// nvngx_dlss.dll found at load (overlay warnings); the backend looks again when it starts.
talesofarise::dlss::DlssLibrary dlss_library;
bool advanced = false;  // overlay settings mode
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
std::array<float, 2> traced_pixel = {50.f, 10.f};  // overlay: % of the screen's width and height (pixel_trace.hpp)
std::string saved_image;                           // overlay: the last code save's result
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
  // Output-size draws and replaced upscales: this frame's, the last frame's (Advanced).
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
    {"Shadows", L"r.Shadow.MaxResolution", "Light shadow resolution (point / spot)", Better::HIGHER, {4096.f, 4096.f, 8192.f, 8192.f, KEEP},
     "Shadow sharpness of point and spot lights."},
    {"Shadows", L"r.ShadowQuality", "Shadow filtering quality", Better::HIGHER, {KEEP, 4.f, 5.f, 5.f, KEEP},
     "Smoothing of shadow edges."},
    {"Shadows", L"r.CapsuleShadowsFullResolution", "Character soft shadows at full resolution", Better::HIGHER, {KEEP, 1.f, 1.f, 1.f, KEEP},
     "1: the soft shadows characters cast from their capsules (around their feet and body) at full resolution\n"
     "instead of half: sharper."},
    {"Shadows", L"r.Shadow.CSM.MaxCascades", "Sun shadow cascades", Better::HIGHER, {4.f, 5.f, 6.f, 10.f, KEEP},
     "Layers of the sun's shadow: more of them keep shadows sharp at every distance."},
    // Test (in no preset): the sun is one shadow map whatever the row above says (DLSS v66): the light's own cascade
    // count (engine_hooks.hpp) raised; the cascades share the 8192 atlas (two to four fit at 4096).
    {"Shadows", L"Tales.SunShadowLayers", "Sun shadow layers (test)", Better::HIGHER,
     {KEEP, KEEP, KEEP, KEEP, 2.f},
     "Test: splits the sun's single shadow map into layers, sharp near you and coarser far away.\n"
     "Needs Sun shadow resolution 4096 (2-4 layers fit). May change nothing or break sun shadows.",
     {3.f, 4.f, KEEP}},
    // Sun shadows shimmer when the sun moves (every ~10 s) at distances above 1; finer texels fix it (ATN: 6144 / 8192).
    // The game's sun is one shadow map (every frame trace: one 8184 x 8184 viewport in the 8192 atlas, whatever the
    // cascade count), drawn once and projected by cs 0x24D721C3. 8192 is the most: v65-v67 doubled the atlas size Unreal
    // computes (1 << (GMaxTextureMipCount - 1), exe+0x1AD7041) and tried 16384 and 12288: both crashed at exe+0x1A9E56C
    // (no depth target), as above 8192 without it (ATN): that size is not the limit; the patch was removed.
    {"Shadows", L"r.Shadow.MaxCSMResolution", "Sun shadow resolution", Better::HIGHER, {6144.f, 6144.f, 8192.f, 8192.f, KEEP},
     "Sharpness of the sun's shadow (the game: 4096; 8192 is the most it can use). Sharper edges shimmer less\n"
     "when the sun moves."},
    // Above 2 once engine_hooks.hpp removed the game's clamp. Farther spreads the same shadow maps over more ground:
    // shadow edges shimmer more (ATN: above 1).
    {"Shadows", L"r.Shadow.DistanceScale", "Sun shadow distance", Better::HIGHER, {KEEP, 1.25f, 1.5f, 2.f, 4.f},
     "How far the sun's shadows reach. Farther: softer edges that shimmer more (higher resolution helps).\n"
     "3 stops sunlight shining through far mountains (above 2: needs Arise-SDK).",
     {2.5f, 3.f, 3.5f}},
    // The sun's light, turned a bit every frame toward the game's steps (engine_hooks.hpp).
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
    state.translucency.Reset();
    state.translucency_frame = UINT64_MAX;
    diagnostics.camera_cuts.store(0);
    diagnostics.skipped_frames.store(0);
    diagnostics.restarts.store(0);
    retired = std::move(state.runtime);
    state.runtime = std::make_unique<Runtime>();
    if (keep_nvidia && retired && !retired->backend.Failed()) state.runtime->backend = std::move(retired->backend);
  }
  {
    const std::unique_lock lock(post.mutex);
    post.frame = UINT64_MAX;
    post.taa_output.Reset();
    post.output.Reset();
    post.targets = {};
    post.upscale_constants.Reset();
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
    auto reconstruction = std::move(runtime.reconstruction);
    auto constant_readback = std::move(runtime.constant_readback);
    auto performance = std::move(runtime.performance);
    runtime = Runtime{};
    if (!backend.Failed()) runtime.backend = std::move(backend);
    runtime.reconstruction = std::move(reconstruction);
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
    if (!create_texture(&runtime.depth, DXGI_FORMAT_R32_FLOAT, frame->render_width, frame->render_height)
        || !create_texture(&runtime.motion, DXGI_FORMAT_R16G16_FLOAT, frame->render_width, frame->render_height)
        || !create_texture(&runtime.particle_masks[0], DXGI_FORMAT_R8_UNORM, frame->render_width, frame->render_height)
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

  if (runtime.backend_failed) {
    SetStatus(std::string("Game TAA: ") + runtime.backend.Status(), frame->generation);
    return false;
  }
  // NVIDIA reads and writes RGBA16F: the game's R11G11B10F scene color is copied by the input preparation, its
  // R11G11B10F output written by the merge. When replacing, the color is copied anyway (the stock pass is skipped).
  // DLAA replacing an RGBA16F output writes it directly; otherwise NVIDIA writes `output`, which DLAA merges into the
  // game's output and DLSS (output size) downsamples into it when replacing.
  const bool copy_color = replace || frame->color_format != DXGI_FORMAT_R16G16B16A16_FLOAT;
  const bool direct = !frame->temporal_upscaling && replace && frame->target_format == DXGI_FORMAT_R16G16B16A16_FLOAT;
  const bool merge = !frame->temporal_upscaling && !direct;
  if (!((!copy_color || runtime.color.resource
         || create_texture(&runtime.color, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->render_width, frame->render_height))
        && (direct || runtime.output.resource
            || create_texture(&runtime.output, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->width, frame->height))
        && (!merge || runtime.merged.resource
            || create_texture(&runtime.merged, frame->target_format, frame->width, frame->height)))) {
    runtime.color = {};
    runtime.output = {};
    runtime.merged = {};
    SetStatus("Game TAA: private texture allocation failed", frame->generation);
    return false;
  }
  {
    // Commit only for the current frame and mode. When replacing, NVIDIA writes the
    // game's output; a failure lets the stock dispatch run (and overwrite it) instead.
    const std::shared_lock lock(state.mutex);
    if (frame->generation != state.generation || frame->frame != state.frame || requested_mode.load() != frame->mode) return false;
  }
  bool evaluated = false;
  bool reconstructed = false;
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
        .copy_color = (copy_color ? 1u : 0u),
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
    ID3D11UnorderedAccessView* prep_outputs[] = {runtime.depth.uav.Get(), runtime.motion.uav.Get(),
                                                 copy_color ? runtime.color.uav.Get() : nullptr,
                                                 (translucency ? current_mask.uav.Get() : nullptr)};
    context->CSSetShader(runtime.prepare_shader.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, buffers);
    context->CSSetShaderResources(0, 6, prep_inputs);
    context->CSSetUnorderedAccessViews(0, 4, prep_outputs, nullptr);
    context->Dispatch((frame->render_width + 7) / 8, (frame->render_height + 7) / 8, 1);
    runtime.performance.Mark(context);
    // NVIDIA reads these outputs next (cheaper than ClearState).
    constexpr std::array<ID3D11ShaderResourceView*, 6> NO_INPUTS = {};
    constexpr std::array<ID3D11UnorderedAccessView*, 4> NO_OUTPUTS = {};
    context->CSSetShaderResources(0, 6, NO_INPUTS.data());
    context->CSSetUnorderedAccessViews(0, 4, NO_OUTPUTS.data(), nullptr);
    // The mask written this frame is the next frame's last one.
    runtime.particle_mask_valid = static_cast<bool>(translucency);
    runtime.particle_mask_index ^= 1u;

    // UE 4.21's TAA buffer has no camera-cut flag (UE 4.26: b0[0].z); cuts are not detected yet.
    constexpr bool camera_cut = false;
    const bool restart = runtime.last_frame == UINT64_MAX;
    const bool skipped = !restart && frame->frame != runtime.last_frame + 1;
    if (camera_cut) ++diagnostics.camera_cuts;
    if (restart) ++diagnostics.restarts;
    if (skipped) ++diagnostics.skipped_frames;
    runtime.performance.Mark(context);
    // Ray Reconstruction: its guides from this frame's G-buffer copy (none: Super Resolution).
    talesofarise::dlss::RayReconstruction guides;
    const talesofarise::dlss::RayReconstruction* ray_guides = nullptr;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer_views;
    if (ray_reconstruction.load()) {
      const std::shared_lock lock(gbuffer.mutex);
      if (gbuffer.frame == frame->frame) gbuffer_views = gbuffer.srvs;
    }
    if (gbuffer_views[0] && gbuffer_views[1] && gbuffer_views[2]
        && (runtime.normals.resource
            || (create_texture(&runtime.diffuse_albedo, DXGI_FORMAT_R8G8B8A8_UNORM, frame->render_width, frame->render_height)
                && create_texture(&runtime.specular_albedo, DXGI_FORMAT_R8G8B8A8_UNORM, frame->render_width, frame->render_height)
                && create_texture(&runtime.normals, DXGI_FORMAT_R16G16B16A16_FLOAT, frame->render_width, frame->render_height)
                && create_texture(&runtime.roughness, DXGI_FORMAT_R16_FLOAT, frame->render_width, frame->render_height)))) {
      struct GuideConstants {
        std::array<std::array<float, 4>, 3> world_to_view;
        std::array<float, 2> projection;
        std::array<uint32_t, 2> extent;
      } guide_constants = {
          .world_to_view = {frame->view[VIEW_TRANSLATED_WORLD_TO_VIEW], frame->view[VIEW_TRANSLATED_WORLD_TO_VIEW + 1],
                            frame->view[VIEW_TRANSLATED_WORLD_TO_VIEW + 2]},
          .projection = {frame->view[VIEW_VIEW_TO_CLIP][0], frame->view[VIEW_VIEW_TO_CLIP + 1][1]},
          .extent = {frame->render_width, frame->render_height},
      };
      static_assert(sizeof(guide_constants) == 64);
      context->UpdateSubresource(runtime.guide_constants.Get(), 0, nullptr, &guide_constants, 0, 0);
      ID3D11Buffer* guide_buffers[] = {runtime.guide_constants.Get()};
      ID3D11ShaderResourceView* guide_inputs[] = {gbuffer_views[0].Get(), gbuffer_views[1].Get(), gbuffer_views[2].Get()};
      ID3D11UnorderedAccessView* guide_outputs[] = {runtime.diffuse_albedo.uav.Get(), runtime.specular_albedo.uav.Get(),
                                                    runtime.normals.uav.Get(), runtime.roughness.uav.Get()};
      context->CSSetShader(runtime.guide_shader.Get(), nullptr, 0);
      context->CSSetConstantBuffers(0, 1, guide_buffers);
      context->CSSetShaderResources(0, 3, guide_inputs);
      context->CSSetUnorderedAccessViews(0, 4, guide_outputs, nullptr);
      context->Dispatch((frame->render_width + 7) / 8, (frame->render_height + 7) / 8, 1);
      constexpr std::array<ID3D11ShaderResourceView*, 3> NO_GUIDE_INPUTS = {};
      constexpr std::array<ID3D11UnorderedAccessView*, 4> NO_GUIDE_OUTPUTS = {};
      context->CSSetShaderResources(0, 3, NO_GUIDE_INPUTS.data());
      context->CSSetUnorderedAccessViews(0, 4, NO_GUIDE_OUTPUTS.data(), nullptr);
      guides = {
          .diffuse_albedo = runtime.diffuse_albedo.resource.Get(),
          .specular_albedo = runtime.specular_albedo.resource.Get(),
          .normals = runtime.normals.resource.Get(),
          .roughness = runtime.roughness.resource.Get(),
          .preset = ray_reconstruction_preset.load(),
      };
      for (size_t row = 0; row < 4; ++row) {
        std::memcpy(&guides.world_to_view[row * 4], frame->view[VIEW_TRANSLATED_WORLD_TO_VIEW + row].data(), 16);
        std::memcpy(&guides.view_to_clip[row * 4], frame->view[VIEW_VIEW_TO_CLIP + row].data(), 16);
      }
      ray_guides = &guides;
    }
    const talesofarise::dlss::Evaluation evaluation = {
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
    };
    // Ray Reconstruction through D3D12 (rr_d3d12.hpp; Super Resolution's feature freed while it runs), else, or when it
    // cannot run, Super Resolution.
    if (ray_guides != nullptr) {
      reconstructed = runtime.reconstruction.Evaluate(context, evaluation, *ray_guides, dlss_library.folder);
      if (reconstructed) runtime.backend.ReleaseFeature();
    } else {
      runtime.reconstruction.ReleaseFeature();
    }
    evaluated = reconstructed || runtime.backend.Evaluate(context, evaluation);
    runtime.performance.Mark(context);
    if (evaluated) {
      talesofarise::rt_shadows::CaptureSceneColor(context, frame->resources[0].Get());
    }
    if (evaluated && talesofarise::frame_generation::Presenting()) {
      const auto& view = frame->view;
      const auto vector = [&](uint32_t row) { return std::array<float, 3>{view[row][0], view[row][1], view[row][2]}; };
      talesofarise::frame_generation::Inputs inputs = {
          .depth = runtime.depth.resource,
          .motion = runtime.motion.resource,
          .render_width = frame->render_width,
          .render_height = frame->render_height,
          .jitter = frame->pixel_jitter,
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
    if (evaluated && merge) {
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
    } else if (evaluated && frame->temporal_upscaling && replace) {
      // The stock pass is skipped: its render-size output gets DLSS's result, downsampled.
      context->ClearState();
      runtime.performance.Mark(context);
      const std::array<uint32_t, 4> extent = {frame->render_width, frame->render_height, 0, 0};
      context->UpdateSubresource(runtime.downsample_constants.Get(), 0, nullptr, extent.data(), 0, 0);
      ID3D11Buffer* downsample_buffers[] = {runtime.downsample_constants.Get()};
      ID3D11ShaderResourceView* downsample_inputs[] = {runtime.output.srv.Get()};
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
    post.output = runtime.output.srv;
  }
  SetStatus(std::string(mode.name) + " active — "
                + (frame->temporal_upscaling
                       ? std::format("{}x{} to {}x{}{}", frame->render_width, frame->render_height, frame->width, frame->height,
                                     (output_size_post ? "" : ", post-processing at the render size"))
                       : "native resolution")
                + (replace ? ", replacing the game's TAA" : "") + (reconstructed ? ", Ray Reconstruction (D3D12)" : ""),
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

// After the game's own motion blur / tonemapper draw (post callback: the HDR addon's shader and constants are still
// bound; it restores them in finalize callbacks), the same draw at the output size.
template <DrawCommand Command>
void DrawAtOutputSize(Command& command, const void*) {
  const uint64_t frame = CurrentFrame();
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
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
  ++post.passes;
  if (std::ranges::find(TONEMAPPER_SHADERS, command.matched_shader_hash) != TONEMAPPER_SHADERS.end()) {
    talesofarise::frame_generation::DrawHudless(context, copy->rtv.Get(), frame);
  }
}

// DLAA: after the tonemapper's draw (at the output size), frame generation's hudless colour from its target.
template <DrawCommand Command>
void DrawHudlessAfterTonemapper(Command& command, const void*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  talesofarise::frame_generation::DrawHudless(context, target.Get(), CurrentFrame());
}

constexpr auto OnPostPass = []<DrawCommand Command>(Command& command) -> renodx::utils::command_action::CallbackResult<Command> {
  const auto mode = requested_mode.load();
  if (mode == AAMode::OFF) return {};
  if (mode == AAMode::DLAA) {
    const bool tonemapper = std::ranges::find(TONEMAPPER_SHADERS, command.matched_shader_hash) != TONEMAPPER_SHADERS.end();
    if (tonemapper && talesofarise::frame_generation::Presenting()) return {.post_callback = DrawHudlessAfterTonemapper<Command>};
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
  renodx::utils::directx::NativeFromReShadeProxy(&context);
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
  renodx::utils::directx::NativeFromReShadeProxy(&context);
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

constexpr auto OnSunShadowMask = [](Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr || !talesofarise::rt_shadows::Trace(context)) return {};
  return {.post_callback = [](Context& command, const void*) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&context);
    if (context != nullptr) {
      talesofarise::rt_shadows::Unbind(context);
    }
  }};
};

// Spark motion: after a sprite particle draw (its translucency target and the scene depth bound), the same draw with its
// motion shaders into the spark motion target, with the game's depth test, the game's state restored after.
template <DrawCommand Command>
void DrawSparkMotion(Command& command, const void*) {
  const auto sprite = std::ranges::find_if(SPRITE_SHADERS, [&](const auto& entry) { return entry.first == command.matched_shader_hash; });
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
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
// lighting pass, unbound after it.
constexpr UINT AMBIENT_SLOT = 41;
template <DrawCommand Command>
void UnbindAmbient(Command& command, const void*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr) return;
  constexpr std::array<ID3D11ShaderResourceView*, 4> NONE = {};
  context->PSSetShaderResources(AMBIENT_SLOT, static_cast<UINT>(NONE.size()), NONE.data());
}

constexpr auto OnLightingDraw = []<DrawCommand Command>(Command& command) -> renodx::utils::command_action::CallbackResult<Command> {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr) return {};
  renodx::utils::command_action::CallbackResult<Command> result = {};
  talesofarise::rt_shadows::LightingResult lighting;
  if (talesofarise::rt_shadows::UsesLightingPass()) {
    BoundViews<ID3D11ShaderResourceView, 3> gbuffer_views;
    context->PSGetShaderResources(1, 3, gbuffer_views.views.data());
    lighting = talesofarise::rt_shadows::TraceLighting(context, gbuffer_views.views);
  }
  const auto ambient = talesofarise::rt_shadows::AmbientOcclusion();
  const auto bounce = lighting.bounce ? lighting.bounce : talesofarise::rt_shadows::BounceLight();
  if (ambient || bounce || lighting.reflection || lighting.specular_occlusion) {
    const std::array<ID3D11ShaderResourceView*, 4> views = {ambient.Get(), bounce.Get(), lighting.reflection.Get(),
                                                            lighting.specular_occlusion.Get()};
    context->PSSetShaderResources(AMBIENT_SLOT, static_cast<UINT>(views.size()), views.data());
    result.post_callback = UnbindAmbient<Command>;
  }
  if (!ray_reconstruction.load() || requested_mode.load() == AAMode::OFF) return result;
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
  renodx::utils::directx::NativeFromReShadeProxy(&context);
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
  ++post.upscales;
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

void OnDestroyDevice(reshade::api::device* device) {
  talesofarise::rt_shadows::Release();
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
  talesofarise::frame_trace::OnPresent();
  {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_immediate_command_list()->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&context);
    talesofarise::rt_shadows::OnPresent(context);
    if (talesofarise::frame_generation::Presenting()) {
      const uint64_t frame = CurrentFrame();
      talesofarise::frame_generation::Inputs inputs;
      {
        const std::shared_lock lock(frame_generation_frame.mutex);
        if (frame_generation_frame.frame == frame) inputs = frame_generation_frame.inputs;
      }
      talesofarise::frame_generation::Submit(context, inputs, frame);
    }
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
  {
    const std::unique_lock lock(post.mutex);
    post.last = {post.passes, post.upscales};
    post.passes = 0;
    post.upscales = 0;
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
    graphics.available = graphics.console.Start(&details);
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
  const float render_scale = (upscaling ? static_cast<float>(expected[0]) / static_cast<float>(width) : 1.f);
  for (size_t index = 0; graphics.available && index < GRAPHICS_SETTINGS.size(); ++index) {
    auto& entry = graphics.entries[index];
    if (entry.cvar.values[0] == nullptr) continue;
    const auto& setting = GRAPHICS_SETTINGS[index];
    const float preset_value = (entry.choice == 0 ? KEEP : entry.choices[entry.choice - 1]);
    const OutputScaling scaling = ((effects_at_output && upscaling) ? OUTPUT_SCALING[index] : OutputScaling::NONE);
    const float current = entry.cvar.Get();
    const bool replaced = index == SSR_QUALITY && talesofarise::rt_shadows::ReflectingRecently();
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
    if (replaced) desired = 0.f;
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
  // Texture LOD bias while upscaling, from the next View upload on.
  talesofarise::dlss::constants::ViewMipBias mip_bias = {.size_vector = VIEW_SIZE_AND_INV_SIZE, .bias_vector = VIEW_MATERIAL_TEXTURE_MIP_BIAS};
  if (upscaling && texture_lod_bias != 0) {
    mip_bias.width = expected[0];
    mip_bias.height = expected[1];
    mip_bias.bias = std::log2(std::min(static_cast<float>(expected[0]) / static_cast<float>(width), static_cast<float>(expected[1]) / static_cast<float>(height)))
                    - static_cast<float>(texture_lod_bias - 1);
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
  talesofarise::post_filters::Store({
      .scale = render_scale,
      .sharpening = ((mode != AAMode::OFF && !keep_game_sharpening) ? 0.f : 1.f),
      .ambient_occlusion = static_cast<float>(ambient_occlusion),
      .sun_shadow_distance = sun_distance_ratio,
      .ao_method = static_cast<float>(ao_method),
      .ao_radius = ao_radius,
      .ao_strength = ao_strength,
      .ao_thickness = ao_thickness,
      .ao_directions = static_cast<float>(ao_directions),
      .ao_quality = static_cast<float>(ao_quality),
      .ao_frame = static_cast<float>(ao_frame),
  });
  talesofarise::engine_hooks::Update();
}

void OnOverlay(reshade::api::effect_runtime*) {
  // Timings are only shown (and measured) in Advanced.
  if (advanced) {
    const std::shared_lock lock(state.mutex);
    overlay_frame.store(state.frame + 1);
  }
  using Phase = talesofarise::engine_scale::Finder::Phase;
  if (int settings_mode = (advanced ? 1 : 0); ImGui::Combo("Settings mode", &settings_mode, "Simple\0Advanced (For Debugging)\0")) {
    advanced = settings_mode == 1;
    reshade::set_config_value(nullptr, "Tales AA", "SettingsMode", settings_mode);
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
        reshade::set_config_value(nullptr, "Tales AA", "Mode", static_cast<int>(mode));
      }
    }
    ImGui::EndCombo();
  }
  std::array<uint32_t, 4> expected_extents;
  {
    const std::shared_lock lock(state.mutex);
    expected_extents = {state.expected_width, state.expected_height, state.width, state.height};
  }
  if (live.enabled && mode == AAMode::DLSS_CUSTOM
      && ImGui::SliderFloat("Render scale", &live.custom, 1.f, 100.f, "%.0f%%")) {
    live.custom = std::clamp(std::round(live.custom), 1.f, 100.f);
    reshade::set_config_value(nullptr, "Tales AA", "CustomScale", live.custom);
  }
  if (mode != AAMode::OFF && mode != AAMode::DLAA && expected_extents[0] != 0 && expected_extents[2] != 0) {
    ImGui::Text("Render size %ux%u (%.1f%%), same aspect ratio as the screen", expected_extents[0], expected_extents[1],
                100. * expected_extents[1] / expected_extents[3]);
  }
  if (mode != AAMode::OFF && mode != AAMode::DLAA && !live.enabled) {
    ImGui::TextWrapped("DLSS needs \"Control the game's render scale\" (Advanced).");
  }
  const auto selected_preset = std::ranges::find(PRESETS, preset.load(), &PresetSettings::value);
  if (ImGui::BeginCombo("DLSS preset", selected_preset != PRESETS.end() ? selected_preset->label : PRESETS[2].label)) {
    for (const auto& option : PRESETS) {
      if (ImGui::Selectable(option.label, option.value == preset.load())) {
        preset.store(option.value);
        reshade::set_config_value(nullptr, "Tales AA", "Preset", option.value);
      }
    }
    ImGui::EndCombo();
  }
  if (bool fix = spark_fix.load(); ImGui::Checkbox("Spark trails fix", &fix)) {
    spark_fix.store(fix);
    reshade::set_config_value(nullptr, "Tales AA", "SparkTrailsFix", fix);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("The game's sparks and embers have no motion, so DLSS can leave trails behind them (most\n"
                      "of all preset L). On: sparks get their own motion (DLSS keeps them sharp); where one was\n"
                      "and for other effects, DLSS shows the current frame.");
  }
  if (bool reconstruct = ray_reconstruction.load(); ImGui::Checkbox("Ray Reconstruction (Experimental)", &reconstruct)) {
    ray_reconstruction.store(reconstruct);
    reshade::set_config_value(nullptr, "Tales AA", "RayReconstruction", reconstruct);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("NVIDIA's DLSS Ray Reconstruction in place of DLSS / DLAA: denoises and upscales with the\n"
                      "scene's materials (best with the ray-traced effects). Needs nvngx_dlssd.dll next to\n"
                      "nvngx_dlss.dll; without it DLSS runs as before.");
  }
  if (ray_reconstruction.load()) {
    constexpr std::array<std::pair<uint32_t, const char*>, 4> RR_PRESETS = {
        {{0u, "NVIDIA's default"}, {4u, "D"}, {5u, "E"}, {6u, "F (newest, needs nvngx_dlssd.dll 310.9 or newer)"}}};
    const uint32_t current = ray_reconstruction_preset.load();
    const auto selected = std::ranges::find(RR_PRESETS, current, &std::pair<uint32_t, const char*>::first);
    if (ImGui::BeginCombo("Ray Reconstruction preset", selected != RR_PRESETS.end() ? selected->second : RR_PRESETS[0].second)) {
      for (const auto& [value, label] : RR_PRESETS) {
        if (ImGui::Selectable(label, value == current)) {
          ray_reconstruction_preset.store(value);
          reshade::set_config_value(nullptr, "Tales AA", "RayReconstructionPreset", value);
        }
      }
      ImGui::EndCombo();
    }
  }
  talesofarise::frame_generation::DrawSettings("Tales AA", &frame_generation);
  if (ImGui::Combo("Texture LOD bias", &texture_lod_bias,
                   "Off (the game's own)\0"
                   "Matched (as sharp as at your output resolution)\0"
                   "Sharper (one step sharper; NVIDIA's recommendation)\0"
                   "Sharpest (two steps sharper; can shimmer)\0")) {
    reshade::set_config_value(nullptr, "Tales AA", "TextureLodBias", texture_lod_bias);
  }
  if (ImGui::IsItemHovered()) {
    std::string example;
    if (expected_extents[0] != 0 && expected_extents[1] < expected_extents[3]) {
      const float matched = std::log2(static_cast<float>(expected_extents[1]) / static_cast<float>(expected_extents[3]));
      example = std::format("\n\nAt {}x{} (bias, lower = sharper): Off 0, Matched {:.1f}, Sharper {:.1f}, Sharpest {:.1f}.",
                            expected_extents[0], expected_extents[1], matched, matched - 1.f, matched - 2.f);
    }
    ImGui::SetTooltip("How sharp textures are while DLSS renders below 100%%.\n"
                      "Matched: as sharp as at your output resolution. Sharper / Sharpest: one / two steps more\n"
                      "(more detail, can shimmer).%s",
                      example.c_str());
  }
  if (ImGui::Checkbox("Game's sharpening (Not recommended)", &keep_game_sharpening)) {
    reshade::set_config_value(nullptr, "Tales AA", "GameSharpening", keep_game_sharpening);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Bandai's sharpening before DLAA / DLSS (needs the RenoDX HDR addon).\n"
                      "Off (default): DLSS alone, without dark and bright edges around characters.");
  }
  if (ImGui::Checkbox("Effects at output resolution (Recommended)", &effects_at_output)) {
    reshade::set_config_value(nullptr, "Tales AA", "EffectsAtOutputResolution", effects_at_output);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("While DLSS renders below 100%%, fog, light shafts, reflections, clouds, effects, skin and shadows\n"
                      "keep their 100%% detail. Looks better, can cost some performance.");
  }
  if (bool replace = replace_taa.load(); advanced && ImGui::Checkbox("Replace the game's TAA pass (faster)", &replace)) {
    replace_taa.store(replace);
    reshade::set_config_value(nullptr, "Tales AA", "ReplaceTAA", replace);
  }

  std::string status;
  std::string constants;
  uint64_t copied_per_frame;
  uint64_t biased_per_frame;
  uint64_t completed;
  std::array<uint32_t, 4> extents;
  uint32_t temporal_shader_hash;
  talesofarise::dlss::PerformanceSummary performance;
  {
    const std::shared_lock lock(state.mutex);
    status = state.status;
    constants = state.constants;
    copied_per_frame = state.copied_per_frame;
    biased_per_frame = state.biased_per_frame;
    completed = state.completed;
    extents = {state.render_width, state.render_height, state.width, state.height};
    temporal_shader_hash = state.temporal_shader_hash;
    performance = state.performance;
  }
  std::string live_status;
  if (!live.enabled) {
    live_status = "Render scale: the game's (DLAA needs it at 100% in the game's menu).";
  } else if (!live.problem.empty()) {
    live_status = (live.retries < MAX_LIVE_RETRIES
                       ? "Live render scale paused: " + live.problem + ". The game's own value is back for now; retrying in a few seconds."
                       : "Live render scale stopped: " + live.problem + ". The game's own value was put back; untick and tick \"Control the game's render scale\" (Advanced) to retry.");
  } else if (live.finder.phase() == Phase::FAILED) {
    live_status = "Render scale: not found in this version of the game (details in ReShade.log): DLAA only, with the game's render scale at 100%.";
  } else if (live.finder.phase() != Phase::FOUND) {
    live_status = live.finder.status() + "...";
  } else {
    live_status = std::format("Render scale: {:g}%{}", live.finder.Get().value_or(0),
                              live.note.empty() ? std::string() : "; " + live.note);
  }
  std::string jitter_status;
  const auto& jitter_patch = camera_jitter.patch;
  if (camera_jitter.halton_branch.applied) {
    jitter_status = std::format("Camera jitter: on, NVIDIA's Halton (2, 3) pattern, {} positions", camera_jitter.halton_positions);
  } else if (jitter_patch.applied) {
    jitter_status = "Camera jitter: on, the game's pattern (8 positions)";
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

  ImGui::PopStyleColor();
  heading("Graphics", GRAPHICS_COLOR);
  ImGui::TextUnformatted("Applied live; a preset never lowers the game's own settings.");
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
        if (const auto sun_override = far_sun_shadow(setting);
            far_sun_shadows && !sun_distance_limited && sun_override != FAR_SUN_SHADOWS.end()) {
          value = sun_override->value;
        }
        if (sun_distance_limited && index == SUN_SHADOW_DISTANCE) {
          value = std::min(value, SUN_DISTANCE_GAME_LIMIT);  // KEEP (NaN) stays
        }
        if (const auto fog_override = max_fog(setting); max_volumetric_fog && fog_override != MAX_VOLUMETRIC_FOG.end()) {
          value = fog_override->value;
        }
        set_choice(&entry, value);
      }
      ambient_occlusion = (preset_index == 0 ? 0 : 2);
      reshade::set_config_value(nullptr, "Tales AA", "AmbientOcclusion", ambient_occlusion);
    }
  }
  // The three preset toggles, one per line, set apart by an empty line above and below (ATN).
  ImGui::NewLine();
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 210, 60, 255));
  ImGui::TextWrapped("These change what every preset sets. Ticking one applies it now; unticking keeps it until you pick a preset.");
  ImGui::PopStyleColor();
  if (ImGui::Checkbox("Vanilla crowd limits (CPU saver, BUT not recommended)", &weak_cpu)) {
    reshade::set_config_value(nullptr, "Tales AA", "WeakCPU", weak_cpu);
    if (weak_cpu) {
      for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
        if (!is_cpu_heavy(GRAPHICS_SETTINGS[index])) continue;
        set_choice(&graphics.entries[index], KEEP);
      }
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("On: presets keep the game's limits for characters behind the camera and hidden characters.\n"
                      "Saves CPU time in crowds; characters can fade in again when you turn around.");
  }
  ImGui::BeginDisabled(sun_distance_limited);
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
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("On: presets set Sun shadow distance 3 and resolution 8192, so sunlight no longer shines\n"
                      "through far mountains. Shadows near you get a bit softer.%s",
                      (sun_distance_limited ? "\n\nNeeds Arise-SDK." : ""));
  }
  if (ImGui::Checkbox("Max volumetric fog", &max_volumetric_fog)) {
    reshade::set_config_value(nullptr, "Tales AA", "MaxVolumetricFog", max_volumetric_fog);
    if (max_volumetric_fog) {
      for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
        if (const auto fog_override = max_fog(GRAPHICS_SETTINGS[index]); fog_override != MAX_VOLUMETRIC_FOG.end()) {
          set_choice(&graphics.entries[index], fog_override->value);
        }
      }
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("On: presets set the finest volumetric lighting and fog: smoother fog and light shafts.\n"
                      "Very heavy without DLSS.");
  }
  {
    auto& shaders = graphics.entries[SHADERS_ON_LOAD];
    const auto one = std::ranges::find(shaders.choices, 1.f);
    const auto zero = std::ranges::find(shaders.choices, 0.f);
    const int on_choice = static_cast<int>(one - shaders.choices.begin()) + 1;
    const int off_choice = static_cast<int>(zero - shaders.choices.begin()) + 1;
    ImGui::BeginDisabled(shaders.cvar.values[0] == nullptr || one == shaders.choices.end() || zero == shaders.choices.end());
    if (bool on = shaders.choice == on_choice; ImGui::Checkbox("Shaders on load", &on)) {
      shaders.choice = (on ? on_choice : off_choice);
      SaveChoice(shaders);
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
  }
  ImGui::NewLine();
  if (!graphics.status.empty()) ImGui::TextWrapped("%s", graphics.status.c_str());
  ImGui::TextWrapped("Some settings apply after loading a new area, anisotropic filtering to textures loaded afterwards. Maximum is very heavy and uses a lot of VRAM; Beyond Maximum also pushes outline distance, NPC distance, monster distance, NPC / monster detail distance, terrain detail and grass draw distance to their highest values.");
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 210, 60, 255));
  ImGui::TextWrapped("Every preset keeps the game's volumetric fog (the most expensive effect) unless Max volumetric fog is on: finer, cheaper or off in its own settings.");
  ImGui::NewLine();
  ImGui::TextWrapped("Best used together with Arise-SDK (www.nexusmods.com/talesofarise/mods/5). The \"Arise-SDK's settings\" group, Far sun shadows and Sun shadow distance above 2 need it (greyed out without it); the group changes Arise-SDK's own settings.");
  ImGui::PopStyleColor();
  const auto& volumetric_fog = graphics.entries[VOLUMETRIC_FOG].cvar;
  const bool fog_off = volumetric_fog.values[0] != nullptr && volumetric_fog.Get() == 0.f;
  const char* open_group = nullptr;
  bool group_open = false;
  for (size_t index = 0; index < GRAPHICS_SETTINGS.size(); ++index) {
    const auto& setting = GRAPHICS_SETTINGS[index];
    auto& entry = graphics.entries[index];
    if (index == SHADERS_ON_LOAD) continue;  // its checkbox is above
    if (open_group == nullptr || std::string_view(open_group) != setting.group) {
      open_group = setting.group;
      group_open = ImGui::CollapsingHeader(setting.group);
      if (group_open && std::string_view(open_group) == ARISE_SDK_GROUP) {
        ImGui::TextWrapped("Ideas from Arise-SDK (thanks to emoose). They need Arise-SDK installed and change its own settings.");
      }
      // Not a cvar: Bandai's ambient occlusion is replaced in the HDR addon (post_filters.hpp).
      if (group_open && std::string_view(open_group) == "Shadows") {
        constexpr ImGuiSliderFlags CHOICE_FLAGS = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput;
        if (ImGui::SliderInt("Ambient occlusion", &ao_method, 0, 1, AO_METHOD_LABELS[ao_method], CHOICE_FLAGS)) {
          reshade::set_config_value(nullptr, "Tales AA", "AOMethod", ao_method);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("GT-VBAO: contact shading that lets light pass behind thin things (grass, poles,\n"
                            "characters) instead of dark halos. Needs the HDR addon.");
        }
        ImGui::BeginDisabled(ao_method == 0);
        if (ImGui::SliderFloat("AO radius", &ao_radius, 0.25f, 5.f, "%.2f m", ImGuiSliderFlags_AlwaysClamp)) {
          reshade::set_config_value(nullptr, "Tales AA", "AORadius", ao_radius);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("How far around a point other surfaces shade it.");
        }
        if (ImGui::SliderFloat("AO strength", &ao_strength, 0.f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
          reshade::set_config_value(nullptr, "Tales AA", "AOStrength", ao_strength);
        }
        if (ImGui::SliderFloat("AO thickness", &ao_thickness, 0.01f, 1.f, "%.2f m", ImGuiSliderFlags_AlwaysClamp)) {
          reshade::set_config_value(nullptr, "Tales AA", "AOThickness", ao_thickness);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("How thick things are taken to be: thicker shades more behind them.");
        }
        if (ImGui::SliderInt("AO quality", &ao_quality, 0, 3, AO_QUALITY_LABELS[ao_quality], CHOICE_FLAGS)) {
          reshade::set_config_value(nullptr, "Tales AA", "AOQuality", ao_quality);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Samples per side: more catch thin things. Heavier.");
        }
        if (ImGui::SliderInt("AO directions", &ao_directions, 0, 3, AO_DIRECTION_LABELS[ao_directions], CHOICE_FLAGS)) {
          reshade::set_config_value(nullptr, "Tales AA", "AODirections", ao_directions);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Directions per pixel: less noise. Heavier.");
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(ao_method != 0);
        if (ImGui::SliderInt("Ambient occlusion halo", &ambient_occlusion, 0, 2, AMBIENT_OCCLUSION_LABELS[ambient_occlusion],
                             CHOICE_FLAGS)) {
          reshade::set_config_value(nullptr, "Tales AA", "AmbientOcclusion", ambient_occlusion);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("The game's ambient occlusion leaves a light ring around characters (most on plants).\n"
                            "Reduced: a smaller ring. Reduced + darker: also darker shading. Needs the HDR addon.");
        }
        ImGui::EndDisabled();
        if (bool traced = talesofarise::rt_shadows::enabled.load(); ImGui::Checkbox("Ray-traced sun shadows (Experimental)", &traced)) {
          talesofarise::rt_shadows::enabled.store(traced);
          if (traced) {
            talesofarise::rt_shadows::Retry();
          }
          reshade::set_config_value(nullptr, "Tales AA", "RayTracedSunShadows", traced);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Buildings, terrain, rocks, leaves and grass also cast ray-traced sun shadows, at any\n"
                            "distance. Heavy. Needs an RTX GPU and the HDR addon.");
        }
        if (talesofarise::rt_shadows::enabled.load()) {
          if (float softness = talesofarise::rt_shadows::sun_softness.load();
              ImGui::SliderFloat("Ray-traced shadow softness", &softness, 0.f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
            talesofarise::rt_shadows::sun_softness.store(softness);
            reshade::set_config_value(nullptr, "Tales AA", "RayTracedShadowSoftness", softness);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How soft shadows get away from what casts them. 0: sharp.");
          }
          if (bool cast = talesofarise::rt_shadows::character_shadows.load(); ImGui::Checkbox("Characters cast ray-traced shadows", &cast)) {
            talesofarise::rt_shadows::character_shadows.store(cast);
            reshade::set_config_value(nullptr, "Tales AA", "RayTracedCharacterShadows", cast);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Characters' ray-traced shadows on the ground and walls.");
          }
        }
        if (bool ambient = talesofarise::rt_shadows::ambient_occlusion.load(); ImGui::Checkbox("Ray-traced ambient occlusion (Experimental)", &ambient)) {
          talesofarise::rt_shadows::ambient_occlusion.store(ambient);
          if (ambient) {
            talesofarise::rt_shadows::Retry();
          }
          reshade::set_config_value(nullptr, "Tales AA", "RayTracedAmbientOcclusion", ambient);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Large-scale shading from buildings, terrain and rocks (rays around each point), on top of\n"
                            "the game's ambient occlusion. Heavy. Needs an RTX (DXR 1.1) GPU and the HDR addon.");
        }
        if (talesofarise::rt_shadows::ambient_occlusion.load()) {
          if (float range = talesofarise::rt_shadows::ambient_range.load() / 100.f;
              ImGui::SliderFloat("Ray-traced AO range", &range, 0.5f, 10.f, "%.1f m", ImGuiSliderFlags_AlwaysClamp)) {
            talesofarise::rt_shadows::ambient_range.store(range * 100.f);
            reshade::set_config_value(nullptr, "Tales AA", "RayTracedAORange", range);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far the rays look for something in the way.");
          }
          if (int rays = static_cast<int>(talesofarise::rt_shadows::ambient_rays.load());
              ImGui::SliderInt("Ray-traced AO rays", &rays, 1, 8, "%d per pixel", ImGuiSliderFlags_AlwaysClamp)) {
            talesofarise::rt_shadows::ambient_rays.store(static_cast<uint32_t>(rays));
            reshade::set_config_value(nullptr, "Tales AA", "RayTracedAORays", rays);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("More rays: less noise. Heavier.");
          }
        }
        if (bool bounce = talesofarise::rt_shadows::bounce_light.load(); ImGui::Checkbox("Ray-traced bounce light (Experimental)", &bounce)) {
          talesofarise::rt_shadows::bounce_light.store(bounce);
          if (bounce) {
            talesofarise::rt_shadows::Retry();
          }
          reshade::set_config_value(nullptr, "Tales AA", "RayTracedBounceLight", bounce);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Light bouncing off lit walls, rocks and the ground onto nearby surfaces (one bounce). Heavy.\n"
                            "Needs DLSS or DLAA, an RTX (DXR 1.1) GPU and the HDR addon. Best with Ray Reconstruction.");
        }
        if (talesofarise::rt_shadows::bounce_light.load()) {
          if (float strength = talesofarise::rt_shadows::bounce_strength.load();
              ImGui::SliderFloat("Bounce light strength", &strength, 0.f, 4.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
            talesofarise::rt_shadows::bounce_strength.store(strength);
            reshade::set_config_value(nullptr, "Tales AA", "RayTracedBounceStrength", strength);
          }
          if (float range = talesofarise::rt_shadows::bounce_range.load() / 100.f;
              ImGui::SliderFloat("Bounce light range", &range, 1.f, 50.f, "%.0f m", ImGuiSliderFlags_AlwaysClamp)) {
            talesofarise::rt_shadows::bounce_range.store(range * 100.f);
            reshade::set_config_value(nullptr, "Tales AA", "RayTracedBounceRange", range);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far light travels between surfaces.");
          }
          if (int rays = static_cast<int>(talesofarise::rt_shadows::bounce_rays.load());
              ImGui::SliderInt("Bounce light rays", &rays, 1, 8, "%d per pixel", ImGuiSliderFlags_AlwaysClamp)) {
            talesofarise::rt_shadows::bounce_rays.store(static_cast<uint32_t>(rays));
            reshade::set_config_value(nullptr, "Tales AA", "RayTracedBounceRays", rays);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("More rays: less noise. Heavier.");
          }
        }
        if (bool reflect = talesofarise::rt_shadows::reflections.load(); ImGui::Checkbox("Ray-traced reflections (Experimental)", &reflect)) {
          talesofarise::rt_shadows::reflections.store(reflect);
          if (reflect) {
            talesofarise::rt_shadows::Retry();
          }
          reshade::set_config_value(nullptr, "Tales AA", "RayTracedReflections", reflect);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Real reflections on wet, polished and smooth surfaces where the game fakes them from the screen.\n"
                            "Needs DLSS or DLAA, an RTX GPU and the HDR addon.");
        }
        if (bool occlusion = talesofarise::rt_shadows::specular_occlusion.load();
            ImGui::Checkbox("Ray-traced specular occlusion (Experimental)", &occlusion)) {
          talesofarise::rt_shadows::specular_occlusion.store(occlusion);
          if (occlusion) {
            talesofarise::rt_shadows::Retry();
          }
          reshade::set_config_value(nullptr, "Tales AA", "RayTracedSpecularOcclusion", occlusion);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("No sky reflections in covered places (under roofs, in caves). Needs an RTX GPU and the HDR addon.");
        }
        if (bool traced = talesofarise::rt_shadows::path_tracing.load(); ImGui::Checkbox("Path tracing (Experimental)", &traced)) {
          talesofarise::rt_shadows::path_tracing.store(traced);
          if (traced) {
            talesofarise::rt_shadows::Retry();
          }
          reshade::set_config_value(nullptr, "Tales AA", "PathTracing", traced);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Light bouncing several times between surfaces, also off screen. Replaces the bounce light.\n"
                            "Very heavy. Needs DLSS or DLAA, an RTX GPU and the HDR addon. Best with Ray Reconstruction.");
        }
        if (talesofarise::rt_shadows::path_tracing.load()) {
          if (int bounces = static_cast<int>(talesofarise::rt_shadows::path_bounces.load());
              ImGui::SliderInt("Path bounces", &bounces, 1, 4, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            talesofarise::rt_shadows::path_bounces.store(static_cast<uint32_t>(bounces));
            reshade::set_config_value(nullptr, "Tales AA", "PathTracingBounces", bounces);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How many times light may bounce off unseen surfaces. Heavier.");
          }
          if (int samples = static_cast<int>(talesofarise::rt_shadows::path_samples.load());
              ImGui::SliderInt("Path samples", &samples, 1, 4, "%d per pixel", ImGuiSliderFlags_AlwaysClamp)) {
            talesofarise::rt_shadows::path_samples.store(static_cast<uint32_t>(samples));
            reshade::set_config_value(nullptr, "Tales AA", "PathTracingSamples", samples);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("More paths: less noise. Heavier.");
          }
        }
        if (talesofarise::rt_shadows::Active()) {
          ImGui::TextWrapped("%s", talesofarise::rt_shadows::Status().c_str());
        }
      }
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
  heading("About", OTHER_COLOR);
  // Tinted as RenoDX tints a setting (the theme's button colours at the tint's hue): the same as the HDR addon's.
  const auto link_button = [](const char* label, const ImColor& tint, const char* url) {
    float hue = 0.f;
    float saturation = 0.f;
    float value = 0.f;
    ImGui::ColorConvertRGBtoHSV(tint.Value.x, tint.Value.y, tint.Value.z, hue, saturation, value);
    for (const auto style : {ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive}) {
      ImVec4 color = ImGui::GetStyleColorVec4(style);
      float style_hue = 0.f;
      ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, style_hue, saturation, value);
      ImGui::ColorConvertHSVtoRGB(hue, saturation, value, color.x, color.y, color.z);
      ImGui::PushStyleColor(style, color);
    }
    if (ImGui::Button(label)) renodx::utils::platform::LaunchURL(url);
    ImGui::PopStyleColor(3);
  };
  link_button("ATN's Ko-fi", ImColor(0xFF, 0x5A, 0x16), "https://ko-fi.com/atnthegreat");
  ImGui::SameLine();
  link_button("NexusMods (For RenoDX + DLSS)", ImColor(0x21, 0x96, 0xF3), "https://nexusmods.com/talesofarise/mods/444");
  if (!advanced) return;

  ImGui::Separator();
  if (ImGui::Checkbox("Control the game's render scale", &live.enabled)) {
    reshade::set_config_value(nullptr, "Tales AA", "LiveScale", live.enabled);
    if (live.enabled) {
      live.problem.clear();
      live.retries = 0;
      if (live.finder.phase() == Phase::FAILED) live.finder = {};
    }
  }
  ImGui::TextWrapped("On: the AA mode sets the game's render scale (DLAA 100%%, DLSS its own); Off puts the game's own value back. DLSS needs it.");
  if (ImGui::Checkbox("Camera jitter", &camera_jitter.enabled)) {
    reshade::set_config_value(nullptr, "Tales AA", "CameraJitter", camera_jitter.enabled);
  }
  ImGui::TextWrapped("The game turns off the camera's sub-pixel jitter, which DLAA / DLSS need for fine detail and to keep small particles from smearing. On: turned on while DLAA / DLSS is on (NVIDIA's Halton pattern, 8 x (output / input)^2 positions: 8 for DLAA), the game's own when AA mode is Off.");
  ImGui::TextWrapped("DLSS presets: E is the older CNN model, K the first transformer model, M (default) and L the second; L is the sharpest and heaviest, but small fast particles can smear. A preset forced in the NVIDIA app or NVIDIA Profile Inspector overrides this one.");
  ImGui::TextWrapped("Replacing the game's TAA pass skips it entirely (faster). Off: the game's TAA runs first and DLAA / DLSS is written over it.");
  ImGui::Separator();
  // Pixel trace (pixel_trace.hpp): a red cross marks the pixel, the red line the traced line through it.
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
  if (ImGui::Button("Trace one frame")) {
    talesofarise::frame_trace::Request();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Saves every draw, dispatch, clear and copy of the next frame to tales_frame_trace.txt\n"
                      "next to the game's exe (one short hitch).");
  }
  ImGui::TextWrapped("%s", talesofarise::frame_trace::Status().c_str());
  if (ImGui::Button("Trace ray-traced shadows for one frame")) {
    talesofarise::rt_shadows::RequestTrace();
  }
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
  if (!saved_image.empty()) {
    ImGui::TextWrapped("%s", saved_image.c_str());
  }
  ImGui::TextWrapped("%s", talesofarise::engine_hooks::SunSteps().c_str());
  ImGui::Separator();
  if (!dlss_library.folder.empty()) {
    ImGui::TextWrapped("nvngx_dlss.dll %u.%u.%u %s (%s). An override in the NVIDIA app or Profile Inspector can load another version; the DLSS indicator shows the one in use.",
                       version[0], version[1], version[2], dlss_library.place,
                       reinterpret_cast<const char*>(std::filesystem::path(dlss_library.folder).u8string().c_str()));
  }
  ImGui::Text("Successful AA frames: %llu", static_cast<unsigned long long>(completed));
  if (completed != 0) {
    ImGui::Text("TAA shader: 0x%08X", temporal_shader_hash);
    const double area_ratio = (extents[0] == 0 || extents[1] == 0
                                   ? 1.
                                   : static_cast<double>(extents[2]) * extents[3] / (static_cast<double>(extents[0]) * extents[1]));
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

extern "C" __declspec(dllexport) const char* NAME = "RenoDX Tales of Arise DLSS + Graphics";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "NVIDIA DLAA and DLSS on the game's TAA (post-processing at the output size), with the camera jitter they need, and graphics settings beyond the game's menu. Independent of the HDR addon.";

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
  reshade::get_config_value(nullptr, "Tales AA", "MaxVolumetricFog", max_volumetric_fog);
  reshade::get_config_value(nullptr, "Tales AA", "FarSunShadows", far_sun_shadows);
  reshade::get_config_value(nullptr, "Tales AA", "SmoothSun", smooth_sun);
  if (float speed = 1.f; reshade::get_config_value(nullptr, "Tales AA", "DaySpeedMultiplier", speed)) {
    day_speed = static_cast<int>(std::ranges::min_element(DAY_SPEEDS, {}, [&](float candidate) { return std::abs(candidate - speed); })
                                 - DAY_SPEEDS.begin());
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
  if (int choice = 2; reshade::get_config_value(nullptr, "Tales AA", "TextureLodBias", choice)) texture_lod_bias = std::clamp(choice, 0, 3);
  if (bool replace = true; reshade::get_config_value(nullptr, "Tales AA", "ReplaceTAA", replace)) replace_taa.store(replace);
  reshade::get_config_value(nullptr, "Tales AA", "DebugDevArise", debug_dev_arise);
  reshade::get_config_value(nullptr, "Tales AA", "GameSharpening", keep_game_sharpening);
  if (bool fix = true; reshade::get_config_value(nullptr, "Tales AA", "SparkTrailsFix", fix)) spark_fix.store(fix);
  if (bool traced = false; reshade::get_config_value(nullptr, "Tales AA", "RayTracedSunShadows", traced)) {
    talesofarise::rt_shadows::enabled.store(traced);
  }
  if (bool ambient = false; reshade::get_config_value(nullptr, "Tales AA", "RayTracedAmbientOcclusion", ambient)) {
    talesofarise::rt_shadows::ambient_occlusion.store(ambient);
  }
  if (float range = 2.f; reshade::get_config_value(nullptr, "Tales AA", "RayTracedAORange", range)) {
    talesofarise::rt_shadows::ambient_range.store((std::isfinite(range) ? std::clamp(range, 0.5f, 10.f) : 2.f) * 100.f);
  }
  if (int rays = 2; reshade::get_config_value(nullptr, "Tales AA", "RayTracedAORays", rays)) {
    talesofarise::rt_shadows::ambient_rays.store(static_cast<uint32_t>(std::clamp(rays, 1, 8)));
  }
  if (bool bounce = false; reshade::get_config_value(nullptr, "Tales AA", "RayTracedBounceLight", bounce)) {
    talesofarise::rt_shadows::bounce_light.store(bounce);
  }
  if (bool reflect = false; reshade::get_config_value(nullptr, "Tales AA", "RayTracedReflections", reflect)) {
    talesofarise::rt_shadows::reflections.store(reflect);
  }
  if (float softness = 0.5f; reshade::get_config_value(nullptr, "Tales AA", "RayTracedShadowSoftness", softness)) {
    talesofarise::rt_shadows::sun_softness.store(std::isfinite(softness) ? std::clamp(softness, 0.f, 3.f) : 0.5f);
  }
  if (bool cast = true; reshade::get_config_value(nullptr, "Tales AA", "RayTracedCharacterShadows", cast)) {
    talesofarise::rt_shadows::character_shadows.store(cast);
  }
  if (bool occlusion = false; reshade::get_config_value(nullptr, "Tales AA", "RayTracedSpecularOcclusion", occlusion)) {
    talesofarise::rt_shadows::specular_occlusion.store(occlusion);
  }
  if (bool traced = false; reshade::get_config_value(nullptr, "Tales AA", "PathTracing", traced)) {
    talesofarise::rt_shadows::path_tracing.store(traced);
  }
  if (int bounces = 2; reshade::get_config_value(nullptr, "Tales AA", "PathTracingBounces", bounces)) {
    talesofarise::rt_shadows::path_bounces.store(static_cast<uint32_t>(std::clamp(bounces, 1, 4)));
  }
  if (int samples = 1; reshade::get_config_value(nullptr, "Tales AA", "PathTracingSamples", samples)) {
    talesofarise::rt_shadows::path_samples.store(static_cast<uint32_t>(std::clamp(samples, 1, 4)));
  }
  if (float strength = 1.f; reshade::get_config_value(nullptr, "Tales AA", "RayTracedBounceStrength", strength)) {
    talesofarise::rt_shadows::bounce_strength.store(std::isfinite(strength) ? std::clamp(strength, 0.f, 4.f) : 1.f);
  }
  if (float range = 10.f; reshade::get_config_value(nullptr, "Tales AA", "RayTracedBounceRange", range)) {
    talesofarise::rt_shadows::bounce_range.store((std::isfinite(range) ? std::clamp(range, 1.f, 50.f) : 10.f) * 100.f);
  }
  if (int rays = 1; reshade::get_config_value(nullptr, "Tales AA", "RayTracedBounceRays", rays)) {
    talesofarise::rt_shadows::bounce_rays.store(static_cast<uint32_t>(std::clamp(rays, 1, 8)));
  }
  if (bool reconstruct = false; reshade::get_config_value(nullptr, "Tales AA", "RayReconstruction", reconstruct)) {
    ray_reconstruction.store(reconstruct);
  }
  if (uint32_t rr_preset = 0; reshade::get_config_value(nullptr, "Tales AA", "RayReconstructionPreset", rr_preset)) {
    ray_reconstruction_preset.store((rr_preset >= 4u && rr_preset <= 6u) ? rr_preset : 0u);
  }
  talesofarise::frame_generation::LoadSettings("Tales AA", &frame_generation);
  reshade::get_config_value(nullptr, "Tales AA", "AmbientOcclusion", ambient_occlusion);
  ambient_occlusion = std::clamp(ambient_occlusion, 0, 2);
  reshade::get_config_value(nullptr, "Tales AA", "AOMethod", ao_method);
  ao_method = std::clamp(ao_method, 0, 1);
  reshade::get_config_value(nullptr, "Tales AA", "AORadius", ao_radius);
  ao_radius = std::isfinite(ao_radius) ? std::clamp(ao_radius, 0.25f, 5.f) : 1.5f;
  reshade::get_config_value(nullptr, "Tales AA", "AOStrength", ao_strength);
  ao_strength = std::isfinite(ao_strength) ? std::clamp(ao_strength, 0.f, 3.f) : 1.f;
  reshade::get_config_value(nullptr, "Tales AA", "AOThickness", ao_thickness);
  ao_thickness = std::isfinite(ao_thickness) ? std::clamp(ao_thickness, 0.01f, 1.f) : 0.1f;
  reshade::get_config_value(nullptr, "Tales AA", "AOQuality", ao_quality);
  ao_quality = std::clamp(ao_quality, 0, 3);
  reshade::get_config_value(nullptr, "Tales AA", "AODirections", ao_directions);
  ao_directions = std::clamp(ao_directions, 0, 3);
  reshade::get_config_value(nullptr, "Tales AA", "EffectsAtOutputResolution", effects_at_output);
  if (reshade::get_config_value(nullptr, "Tales AA", "CustomScale", live.custom)) {
    live.custom = std::isfinite(live.custom) ? std::clamp(std::round(live.custom), 1.f, 100.f) : 50.f;
  }
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_overlay("Tales of Arise DLSS + Graphics", OnOverlay);
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
  for (const uint32_t lighting_shader : LIGHTING_SHADERS) {
    renodx::utils::command_action::Register(OnLightingDraw, {
                                                                .shader_hash = lighting_shader,
                                                                .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                            });
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
  renodx::utils::command_action::Register(OnSunShadowMask, {
                                                               .shader_hash = talesofarise::rt_shadows::SUN_SHADOW_SHADER,
                                                               .command_types = renodx::utils::command_action::COMMAND_TYPE_DISPATCH,
                                                           });
  talesofarise::dlss::constants::Use(DLL_PROCESS_ATTACH);
  talesofarise::post_filters::Use(DLL_PROCESS_ATTACH);
  renodx::utils::state::Use(DLL_PROCESS_ATTACH);
  renodx::utils::command_action::Use(DLL_PROCESS_ATTACH);
  std::wstring module_path(32768, L'\0');
  module_path.resize(GetModuleFileNameW(module, module_path.data(), static_cast<DWORD>(module_path.size())));
  // Ray tracing's Agility SDK (DXR 1.2) in the D3D12 folder next to the add-on.
  talesofarise::rt_shadows::agility_folder = (std::filesystem::path(module_path).parent_path() / L"D3D12" / L"").string();
  // Frame generation takes over the game's swap chain, which it makes after its device.
  if (frame_generation) {
    talesofarise::frame_generation::Install(std::filesystem::path(module_path).parent_path(), "Tales");
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
  renodx::utils::log::i("[Tales DLSS] DLAA and DLSS Ultra Quality/Quality/Balanced/Performance/Ultra Performance/Custom 1-100% on UE 4.21 TAA 0x88765764 / 0xE2AF861D; motion blur and tonemapper at the output size; aspect-exact render sizes; preset choice; camera jitter; graphics presets.");
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE) {
  if (attached) {
    renodx::utils::command_action::Unregister(OnCommand);
    renodx::utils::command_action::Unregister(OnPostPass);
    renodx::utils::command_action::Unregister(OnUpscale);
    renodx::utils::command_action::Unregister(OnTranslucencyComposite);
    renodx::utils::command_action::Unregister(OnSpriteDraw);
    renodx::utils::command_action::Unregister(OnLightingDraw);
    renodx::utils::command_action::Unregister(OnDepthPrepass);
    renodx::utils::command_action::Unregister(OnSunShadowMask);
    renodx::utils::command_action::Use(DLL_PROCESS_DETACH);
    renodx::utils::state::Use(DLL_PROCESS_DETACH);
    talesofarise::dlss::constants::Use(DLL_PROCESS_DETACH);
    talesofarise::post_filters::Store({});
    talesofarise::post_filters::Use(DLL_PROCESS_DETACH);
    reshade::unregister_overlay("Tales of Arise DLSS + Graphics", OnOverlay);
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
