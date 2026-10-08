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
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"
#include "./constant_mirror.hpp"
#include "./constant_readback.hpp"
#include "./engine_scale.hpp"
#include "./native_state.hpp"
#include "./ngx_backend.hpp"
#include "./performance.hpp"
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
  const std::unique_lock lock(state.mutex);
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

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
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
              renodx::utils::log::w("[Rome AA live scale] At ", desired, "% ", live.note, ".");
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
  // UE caps its jitter pattern at 255 positions; NVIDIA wants 8 * (output / input)^2.
  if ((mode == AAMode::DLSS_CUSTOM && live.enabled && live.custom < 33.f)
      || (completed != 0 && extents[2] != 0 && static_cast<float>(extents[0]) / static_cast<float>(extents[2]) < .32f)) {
    ImGui::TextWrapped("Below Ultra Performance (33%%), the game's (UE's) jitter pattern gets too short for DLSS: the image can pulse blurry/sharp.");
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
  if (!advanced) return;

  ImGui::Separator();
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
    ImGui::Text("Game jitter pattern: %s (NVIDIA recommends %u or more)",
                phases != 0 ? std::to_string(phases).c_str() : "measuring", recommended);
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

}  // namespace

extern "C" __declspec(dllexport) const char* NAME = "RenoDX Expeditions: Rome DLAA / DLSS (experimental)";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "Native DLAA, DLSS Ultra Quality/Quality/Balanced/Performance/Ultra Performance and custom 1-100% scaling with live render scale and preset choice. Independent of HDR, with stock TAA fallback.";

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
  expeditionsrome::dlss::constants::Use(DLL_PROCESS_ATTACH);
  renodx::utils::state::Use(DLL_PROCESS_ATTACH);
  renodx::utils::command_action::Use(DLL_PROCESS_ATTACH);
  attached = true;
  renodx::utils::log::i("[Rome DLAA] Native DLAA and DLSS Ultra Quality/Quality/Balanced/Performance/Ultra Performance/Custom 1-100% (all six Ultra/Fast compute hashes; exact/4/8-pixel allocations). Live render scale; preset choice. Runtime NVIDIA scale limits; exact CB readbacks and asynchronous phase timings; Release-compatible shared ABI.");
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE) {
  if (attached) {
    renodx::utils::command_action::Unregister(OnCommand);
    renodx::utils::command_action::Unregister(expeditionsrome::dlss::temporal_capture::OnCommand);
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
