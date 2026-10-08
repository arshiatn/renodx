/*
 * Copyright (C) 2026 RenoDX contributors
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <d3d11_1.h>
#include <wrl/client.h>

// Compile the actual callback implementation rather than reproduce its claim
// rules. This host never attaches the addon or initializes NVIDIA/ReShade.
#if defined(__clang__)
#pragma clang diagnostic push
// Preserve the production compilation surface without promoting unrelated
// existing shared-header warnings to errors in this focused /W4 fixture.
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "../addon.cpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace {
std::atomic_uint64_t host_log_messages = 0;
std::atomic_uint64_t host_registration_calls = 0;
}  // namespace

// Header-only ReShade logging discovers these exports in the test executable.
// They service logging only; registration would be an unexpected test side effect.
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {
  ++host_log_messages;
}
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) {
  ++host_registration_calls;
  return false;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*) {
  ++host_registration_calls;
}

int main() {
  try {
    const auto check = [](HRESULT result) {
      if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
    };
    const auto expect = [](bool condition, const std::string& label) {
      if (!condition) throw std::runtime_error(label);
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                            device.GetAddressOf(), nullptr, context.GetAddressOf()));
    ComPtr<ID3D11DeviceContext1> context1;
    check(context.As(&context1));
    requested_mode.store(AAMode::DLAA);
    state.generation = 10;
    state.frame = 20;
    state.claimed_frame = UINT64_MAX;
    state.completed = 0;
    Context command = {};
    for (const uint32_t hash : {0x29C140B2u, 0x201C3D3Du, 0xCCB4A156u, 0xD8C07EEEu, 0x4B43383Du, 0xB266ECB6u}) {
      expect(std::ranges::find(TEMPORAL_SHADERS, hash) != TEMPORAL_SHADERS.end(),
             "production registration recognizes all six temporal compute contracts");
    }
    expect(TEMPORAL_SHADERS.size() == 6
               && std::ranges::find(TEMPORAL_SHADERS, 0xC0DEC0DEu) == TEMPORAL_SHADERS.end(),
           "production temporal mapping keeps unknown shader hashes excluded");

    struct CandidateLayout {
      std::array<uint32_t, 2> output = {16, 16};
      std::array<uint32_t, 2> source = {16, 16};
      std::array<uint32_t, 2> render = {16, 16};
      AAMode mode = AAMode::DLAA;
      uint32_t temporal_shader_hash = 0;
      std::optional<bool> temporal_upscaling;
      std::optional<std::array<float, 2>> render_override;
    };
    const auto make_frame = [&](bool valid_layout, bool valid_matrix = true, const CandidateLayout& layout = {}) {
      expect(state.runtime != nullptr, "runtime available for candidate");
      auto frame = std::make_unique<Frame>();
      frame->runtime = std::move(state.runtime);
      frame->context = context1;
      frame->generation = state.generation;
      frame->frame = state.frame;
      frame->width = layout.output[0];
      frame->height = layout.output[1];
      frame->source_width = layout.source[0];
      frame->source_height = layout.source[1];
      frame->mode = layout.mode;
      frame->temporal_shader_hash = layout.temporal_shader_hash;
      frame->temporal_upscaling = layout.temporal_upscaling.value_or(layout.render != layout.output);
      requested_mode.store(layout.mode);
      frame->runtime->device = device;
      frame->runtime->width = frame->width;
      frame->runtime->height = frame->height;
      frame->runtime->render_width = layout.render[0];
      frame->runtime->render_height = layout.render[1];
      frame->runtime->mode = layout.mode;
      frame->runtime->temporal_shader_hash = layout.temporal_shader_hash;
      frame->runtime->temporal_upscaling = layout.render != layout.output;
      // A valid candidate reaches the real post-validation branch but stops
      // before NVIDIA submission or private pipeline/texture initialization.
      frame->runtime->backend_failed = true;
      state.claimed_frame = frame->frame;

      std::array<std::array<float, 4>, 27> taa = {};
      std::array<std::array<float, 4>, 130> view = {};
      taa[19] = {static_cast<float>(valid_layout ? frame->source_width : 8), static_cast<float>(frame->source_height), 0, 0};
      taa[20] = {static_cast<float>(frame->width), static_cast<float>(frame->height), 0, 0};
      taa[22] = {static_cast<float>(frame->width), static_cast<float>(frame->height), 0, 0};
      taa[23] = {0, 0, static_cast<float>(frame->width), static_cast<float>(frame->height)};
      taa[25] = {static_cast<float>(layout.render[0]), static_cast<float>(layout.render[1]), 0, 0};
      if (layout.render_override) {
        taa[25][0] = (*layout.render_override)[0];
        taa[25][1] = (*layout.render_override)[1];
      }
      taa[26] = {.25f, -.125f, taa[25][0] / frame->width, static_cast<float>(frame->width) / taa[25][0]};
      view[122] = {2 * taa[26][0] / taa[25][0], -2 * taa[26][1] / taa[25][1], 0, 0};
      view[129] = {0, 1, 1, 0};
      for (uint32_t index = 0; index < 4; ++index) view[118 + index][index] = 1;
      if (!valid_matrix) view[118][0] = std::numeric_limits<float>::quiet_NaN();
      const std::array<std::span<const std::byte>, 2> contents = {
          std::as_bytes(std::span(taa)),
          std::as_bytes(std::span(view)),
      };
      std::array<ComPtr<ID3D11Buffer>, 2> buffers;
      std::array<uint32_t, 2> counts = {};
      for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
        const D3D11_BUFFER_DESC desc = {
            .ByteWidth = static_cast<uint32_t>(contents[slot].size()),
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = 0,
            .MiscFlags = 0,
            .StructureByteStride = 0,
        };
        const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = contents[slot].data(), .SysMemPitch = 0, .SysMemSlicePitch = 0};
        check(device->CreateBuffer(&desc, &initial, buffers[slot].GetAddressOf()));
        counts[slot] = desc.ByteWidth / 16;
      }
      expect(frame->runtime->constant_readback.Queue(context1.Get(), buffers, {}, counts,
                                                     {contents[0].size(), contents[1].size()}),
             "actual GPU snapshot queue");
      frame->taa[0][0] = -321;
      frame->view[0][0] = -654;
      return frame;
    };

    auto invalid = make_frame(false);
    auto* invalid_runtime = invalid->runtime.get();
    EvaluateFrame(command, invalid.get());
    expect(!invalid->values_validated && !invalid->runtime->constant_readback.pending_context,
           "invalid first candidate completes snapshots then rejects layout");
    expect(invalid->taa[0][0] == 0 && state.claimed_frame == state.frame,
           "post validation used actual queued bytes; claim held until finalization");
    FinishFrame(command, invalid.release());
    expect(state.runtime.get() == invalid_runtime && state.claimed_frame == UINT64_MAX,
           "invalid first candidate returns runtime and releases same-frame claim");

    auto valid = make_frame(true);
    auto* valid_runtime = valid->runtime.get();
    EvaluateFrame(command, valid.get());
    expect(valid->values_validated && !valid->committed && valid->taa[0][0] == 0 && valid->view[0][0] == 0,
           "valid same-frame candidate passes real CB gate without SDK submission");
    expect(!valid->runtime->private_state && !valid->runtime->prepare_shader && !valid->runtime->output.resource,
           "prefilled matching runtime prevents pipeline/texture initialization");
    FinishFrame(command, valid.release());
    expect(state.runtime.get() == valid_runtime && state.claimed_frame == state.frame && state.completed == 0,
           "valid candidate retains claim and returns matching-generation runtime");

    state.claimed_frame = UINT64_MAX;
    auto invalid_matrix = make_frame(true, false);
    EvaluateFrame(command, invalid_matrix.get());
    expect(!invalid_matrix->values_validated, "nonfinite matrix rejected before SDK");
    FinishFrame(command, invalid_matrix.release());
    expect(state.claimed_frame == UINT64_MAX, "matrix rejection also releases same-frame claim");

    const CandidateLayout quality = {
        .output = {3840, 2160},
        .source = {2576, 1448},
        .render = {2573, 1448},
        .mode = AAMode::DLSS_QUALITY,
    };
    auto upscaled = make_frame(true, true, quality);
    auto* upscaled_runtime = upscaled->runtime.get();
    EvaluateFrame(command, upscaled.get());
    expect(upscaled->values_validated && upscaled->render_width == 2573 && upscaled->render_height == 1448
               && upscaled->taa[19][0] == 2576 && upscaled->taa[20][0] == 3840 && !upscaled->committed,
           "Quality accepts actual lower-resolution rectangle, padded source, and full output");
    expect(!upscaled->runtime->private_state && !upscaled->runtime->prepare_shader && !upscaled->runtime->output.resource,
           "Quality callback fixture does not initialize private pipelines or NVIDIA");
    FinishFrame(command, upscaled.release());
    expect(state.runtime.get() == upscaled_runtime && state.claimed_frame == state.frame && state.completed == 0,
           "valid Quality candidate retains claim without SDK submission");

    state.claimed_frame = UINT64_MAX;
    auto half_scale = make_frame(true, true, {
                                                 .output = {3840, 2160},
                                                 .source = {1920, 1080},
                                                 .render = {1920, 1080},
                                                 .mode = AAMode::DLSS_QUALITY,
                                             });
    EvaluateFrame(command, half_scale.get());
    expect(!half_scale->values_validated && !half_scale->runtime->constant_readback.pending_context,
           "Quality rejects 50 percent render scale after exact CB snapshot");
    FinishFrame(command, half_scale.release());
    expect(state.claimed_frame == UINT64_MAX, "wrong Quality scale releases its claim");

    auto padded_wrong = quality;
    padded_wrong.source[0] = 2584;
    auto unsupported_padding = make_frame(true, true, padded_wrong);
    EvaluateFrame(command, unsupported_padding.get());
    expect(!unsupported_padding->values_validated,
           "Quality rejects pooled allocation beyond the exact four/eight-pixel padding");
    FinishFrame(command, unsupported_padding.release());
    expect(state.claimed_frame == UINT64_MAX, "unsupported padding releases its claim");

    struct ScalingCase {
      const char* label;
      CandidateLayout layout;
      bool accepted;
    };
    const auto scaling_cases = std::to_array<ScalingCase>({
        {"Balanced lower boundary", {.output = {1000, 1000}, .source = {560, 560}, .render = {560, 560}, .mode = AAMode::DLSS_BALANCED}, true},
        {"Balanced center", {.output = {3840, 2160}, .source = {2232, 1256}, .render = {2227, 1253}, .mode = AAMode::DLSS_BALANCED}, true},
        {"Balanced upper boundary", {.output = {1000, 1000}, .source = {600, 600}, .render = {600, 600}, .mode = AAMode::DLSS_BALANCED}, true},
        {"Balanced below range", {.output = {1000, 1000}, .source = {552, 552}, .render = {550, 550}, .mode = AAMode::DLSS_BALANCED}, false},
        {"Balanced above range", {.output = {1000, 1000}, .source = {616, 616}, .render = {610, 610}, .mode = AAMode::DLSS_BALANCED}, false},
        {"Performance lower boundary", {.output = {3840, 2160}, .source = {1920, 1080}, .render = {1920, 1080}, .mode = AAMode::DLSS_PERFORMANCE}, true},
        {"Performance upper boundary", {.output = {1000, 1000}, .source = {520, 520}, .render = {520, 520}, .mode = AAMode::DLSS_PERFORMANCE}, true},
        {"Performance below range", {.output = {1000, 1000}, .source = {496, 496}, .render = {490, 490}, .mode = AAMode::DLSS_PERFORMANCE}, false},
        {"Performance above range", {.output = {1000, 1000}, .source = {536, 536}, .render = {530, 530}, .mode = AAMode::DLSS_PERFORMANCE}, false},
        {"Quality lower boundary", {.output = {1000, 1000}, .source = {640, 640}, .render = {640, 640}, .mode = AAMode::DLSS_QUALITY}, true},
        {"Quality upper boundary", {.output = {1000, 1000}, .source = {704, 704}, .render = {700, 700}, .mode = AAMode::DLSS_QUALITY}, true},
        {"Ultra Performance captured Fast 33 percent", {.output = {3840, 2160}, .source = {1268, 716}, .render = {1268, 713}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .temporal_shader_hash = 0x4B43383Du}, true},
        {"Ultra Performance 33 percent Ultra contract", {.output = {3840, 2160}, .source = {1268, 716}, .render = {1268, 713}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE, .temporal_shader_hash = 0xCCB4A156u}, true},
        {"Ultra Performance 34 percent", {.output = {1000, 1000}, .source = {340, 340}, .render = {340, 340}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE}, true},
        {"Ultra Performance below range", {.output = {1000, 1000}, .source = {312, 312}, .render = {310, 310}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE}, false},
        {"Ultra Performance above range", {.output = {1000, 1000}, .source = {360, 360}, .render = {360, 360}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE}, false},
        {"Ultra Performance cannot use 50 percent", {.output = {3840, 2160}, .source = {1920, 1080}, .render = {1920, 1080}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE}, false},
        {"Ultra Performance cannot use native 100 percent", {.output = {3840, 2160}, .source = {3840, 2160}, .render = {3840, 2160}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE}, false},
        {"Custom captured Fast 33 percent", {.output = {3840, 2160}, .source = {1268, 716}, .render = {1268, 713}, .mode = AAMode::DLSS_CUSTOM, .temporal_shader_hash = 0x4B43383Du}, true},
        {"Custom 33 percent Ultra contract", {.output = {3840, 2160}, .source = {1268, 716}, .render = {1268, 713}, .mode = AAMode::DLSS_CUSTOM, .temporal_shader_hash = 0xCCB4A156u}, true},
        {"Custom 40 percent", {.output = {3840, 2160}, .source = {1536, 864}, .render = {1536, 864}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 49 percent padded", {.output = {3840, 2160}, .source = {1884, 1060}, .render = {1882, 1059}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 50 percent", {.output = {3840, 2160}, .source = {1920, 1080}, .render = {1920, 1080}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 53 percent four-pixel height padding", {.output = {3840, 2160}, .source = {2036, 1148}, .render = {2036, 1145}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 54 percent four-pixel width padding", {.output = {3840, 2160}, .source = {2076, 1168}, .render = {2074, 1167}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 56 percent mixed four/eight-pixel padding", {.output = {3840, 2160}, .source = {2152, 1212}, .render = {2151, 1210}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 58 percent rounded", {.output = {3840, 2160}, .source = {2232, 1256}, .render = {2227, 1253}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 67 percent rounded", {.output = {3840, 2160}, .source = {2576, 1448}, .render = {2573, 1448}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 80 percent", {.output = {3840, 2160}, .source = {3072, 1728}, .render = {3072, 1728}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 85 percent captured Ultra contract", {.output = {3840, 2160}, .source = {3264, 1836}, .render = {3264, 1836}, .mode = AAMode::DLSS_CUSTOM, .temporal_shader_hash = 0x29C140B2u, .temporal_upscaling = false}, true},
        {"Custom 85 percent captured Fast contract", {.output = {3840, 2160}, .source = {3264, 1836}, .render = {3264, 1836}, .mode = AAMode::DLSS_CUSTOM, .temporal_shader_hash = 0x201C3D3Du, .temporal_upscaling = false}, true},
        {"Custom 99 percent rounded and padded", {.output = {3840, 2160}, .source = {3808, 2144}, .render = {3802, 2139}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 100 percent native pass", {.output = {3840, 2160}, .source = {3840, 2160}, .render = {3840, 2160}, .mode = AAMode::DLSS_CUSTOM}, true},
        {"Custom 100 percent stale upscale flag", {.output = {3840, 2160}, .source = {3840, 2160}, .render = {3840, 2160}, .mode = AAMode::DLSS_CUSTOM, .temporal_upscaling = true}, true},
        {"Custom below 33 percent", {.output = {1000, 1000}, .source = {320, 320}, .render = {320, 320}, .mode = AAMode::DLSS_CUSTOM}, false},
        {"Custom above 100 percent", {.output = {1000, 1000}, .source = {1016, 1016}, .render = {1010, 1010}, .mode = AAMode::DLSS_CUSTOM}, false},
        {"Custom mismatched aspect", {.output = {3840, 2160}, .source = {3072, 1512}, .render = {3072, 1512}, .mode = AAMode::DLSS_CUSTOM}, false},
        {"Custom reduced input overrides stale native flag", {.output = {3840, 2160}, .source = {3072, 1728}, .render = {3072, 1728}, .mode = AAMode::DLSS_CUSTOM, .temporal_upscaling = false}, true},
        {"Custom excessive allocation padding", {.output = {3840, 2160}, .source = {3816, 2144}, .render = {3802, 2139}, .mode = AAMode::DLSS_CUSTOM}, false},
        {"Custom unrelated pooled source height", {.output = {3840, 2160}, .source = {2036, 1156}, .render = {2036, 1145}, .mode = AAMode::DLSS_CUSTOM}, false},
        {"Custom arbitrary non-aligned source extent", {.output = {3840, 2160}, .source = {2037, 1148}, .render = {2036, 1145}, .mode = AAMode::DLSS_CUSTOM}, false},
        {"Ultra Performance excessive source padding", {.output = {3840, 2160}, .source = {1276, 716}, .render = {1268, 713}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE}, false},
        {"Custom 33 percent excessive source padding", {.output = {3840, 2160}, .source = {1268, 724}, .render = {1268, 713}, .mode = AAMode::DLSS_CUSTOM}, false},
        {"Ultra Performance mismatched aspect", {.output = {3840, 2160}, .source = {1268, 760}, .render = {1268, 760}, .mode = AAMode::DLSS_ULTRA_PERFORMANCE}, false},
        {"Custom nonfinite width", {.output = {3840, 2160}, .source = {3072, 1728}, .render = {3072, 1728}, .mode = AAMode::DLSS_CUSTOM, .render_override = std::array<float, 2>{std::numeric_limits<float>::quiet_NaN(), 1728}}, false},
        {"Custom nonfinite height", {.output = {3840, 2160}, .source = {3072, 1728}, .render = {3072, 1728}, .mode = AAMode::DLSS_CUSTOM, .render_override = std::array<float, 2>{3072, std::numeric_limits<float>::infinity()}}, false},
        {"Custom fractional active size", {.output = {3840, 2160}, .source = {3072, 1728}, .render = {3072, 1728}, .mode = AAMode::DLSS_CUSTOM, .render_override = std::array<float, 2>{3071.5f, 1728}}, false},
        {"Custom zero active size", {.output = {3840, 2160}, .source = {3072, 1728}, .render = {3072, 1728}, .mode = AAMode::DLSS_CUSTOM, .render_override = std::array<float, 2>{0, 1728}}, false},
        {"Balanced reduced input overrides stale native flag", {.output = {1000, 1000}, .source = {584, 584}, .render = {580, 580}, .mode = AAMode::DLSS_BALANCED, .temporal_upscaling = false}, true},
        {"Performance reduced input overrides stale native flag", {.output = {3840, 2160}, .source = {1920, 1080}, .render = {1920, 1080}, .mode = AAMode::DLSS_PERFORMANCE, .temporal_upscaling = false}, true},
        {"DLAA cannot use reduced input", {.output = {3840, 2160}, .source = {3072, 1728}, .render = {3072, 1728}, .mode = AAMode::DLAA}, false},
    });
    for (const auto& [label, layout, accepted] : scaling_cases) {
      state.claimed_frame = UINT64_MAX;
      auto candidate = make_frame(true, true, layout);
      auto* candidate_runtime = candidate->runtime.get();
      EvaluateFrame(command, candidate.get());
      expect(candidate->values_validated == accepted && !candidate->committed
                 && !candidate->runtime->constant_readback.pending_context,
             std::string(label) + ": exact GPU constants validate or reject before NVIDIA submission");
      expect(!candidate->runtime->private_state && !candidate->runtime->prepare_shader
                 && !candidate->runtime->output.resource,
             std::string(label) + ": no private pipeline or SDK initialization");
      if (accepted) {
        expect(candidate->temporal_upscaling == (layout.render != layout.output),
               std::string(label) + ": validated active extents determine preparation convention");
      }
      FinishFrame(command, candidate.release());
      expect(state.runtime.get() == candidate_runtime && state.completed == 0
                 && state.claimed_frame == (accepted ? state.frame : UINT64_MAX),
             std::string(label) + ": fallback preserves runtime and matching claim ownership");
    }

    constexpr std::array<uint32_t, 2> OUTPUT = {3840, 2160};
    for (const bool fast : {false, true}) {
      for (uint32_t percentage = 33; percentage <= 100; ++percentage) {
        CandidateLayout layout = {.output = OUTPUT, .mode = AAMode::DLSS_CUSTOM};
        if (percentage >= 71) {
          layout.temporal_shader_hash = (fast ? 0x201C3D3Du : 0x29C140B2u);
        } else {
          layout.temporal_shader_hash = (fast ? 0x4B43383Du : 0xCCB4A156u);
        }
        layout.temporal_upscaling = percentage == 100;
        for (uint32_t axis = 0; axis < OUTPUT.size(); ++axis) {
          layout.render[axis] = (OUTPUT[axis] * percentage + 99) / 100;
          layout.source[axis] = (layout.render[axis] + 3) / 4 * 4;
        }
        const std::string label = "Custom " + std::to_string(percentage)
                                  + (fast ? " percent Fast TAA" : " percent Ultra TAA")
                                  + " with UE four-pixel allocation";
        state.claimed_frame = UINT64_MAX;
        auto candidate = make_frame(true, true, layout);
        auto* candidate_runtime = candidate->runtime.get();
        EvaluateFrame(command, candidate.get());
        expect(candidate->values_validated && !candidate->committed
                   && !candidate->runtime->constant_readback.pending_context
                   && candidate->render_width == layout.render[0] && candidate->render_height == layout.render[1]
                   && candidate->temporal_upscaling == (percentage < 100),
               label + ": integer active subrectangle passes the actual post-callback guard");
        expect(!candidate->runtime->private_state && !candidate->runtime->prepare_shader
                   && !candidate->runtime->output.resource,
               label + ": no private pipeline or SDK initialization");
        FinishFrame(command, candidate.release());
        expect(state.runtime.get() == candidate_runtime && state.claimed_frame == state.frame && state.completed == 0,
               label + ": valid fallback retains its claim and runtime ownership");
      }
    }

    for (const uint32_t hash : TEMPORAL_SHADERS) {
      for (const uint32_t percentage : {100u, 85u, 100u, 85u}) {
        CandidateLayout layout = {
            .output = OUTPUT,
            .mode = AAMode::DLSS_CUSTOM,
            .temporal_shader_hash = hash,
            .temporal_upscaling = percentage == 100,
        };
        for (uint32_t axis = 0; axis < OUTPUT.size(); ++axis) {
          layout.render[axis] = OUTPUT[axis] * percentage / 100;
          layout.source[axis] = layout.render[axis];
        }
        state.claimed_frame = UINT64_MAX;
        auto candidate = make_frame(true, true, layout);
        auto* candidate_runtime = candidate->runtime.get();
        EvaluateFrame(command, candidate.get());
        expect(candidate->values_validated && candidate->temporal_upscaling == (percentage < 100)
                   && candidate->temporal_shader_hash == hash && !candidate->committed
                   && !candidate->runtime->constant_readback.pending_context,
               "same-hash 100/85 percent transition replaces stale flag with validated active extents");
        expect(!candidate->runtime->private_state && !candidate->runtime->prepare_shader
                   && !candidate->runtime->output.resource,
               "same-hash scale regression does not initialize private pipelines or NVIDIA");
        FinishFrame(command, candidate.release());
        expect(state.runtime.get() == candidate_runtime && state.claimed_frame == state.frame && state.completed == 0,
               "same-hash scale regression preserves runtime and claim ownership");
      }
    }

    for (const auto next_mode : {AAMode::DLSS_BALANCED, AAMode::DLSS_PERFORMANCE, AAMode::DLSS_CUSTOM, AAMode::DLSS_ULTRA_PERFORMANCE}) {
      state.claimed_frame = UINT64_MAX;
      auto interrupted = make_frame(true, true, quality);
      auto* interrupted_runtime = interrupted->runtime.get();
      requested_mode.store(next_mode);
      EvaluateFrame(command, interrupted.get());
      expect(!interrupted->values_validated && interrupted->taa[0][0] == -321
                 && interrupted->runtime->constant_readback.pending_context != nullptr,
             "new AA mode transition cancels a previous Quality transaction before readback");
      FinishFrame(command, interrupted.release());
      expect(state.runtime.get() == interrupted_runtime && !state.runtime->constant_readback.pending_context
                 && state.claimed_frame == UINT64_MAX,
             "new AA mode transition cancels only the matching claim and snapshot");
    }

    auto changed_mode = make_frame(true);
    auto* changed_runtime = changed_mode->runtime.get();
    requested_mode.store(AAMode::DLSS_QUALITY);
    EvaluateFrame(command, changed_mode.get());
    expect(!changed_mode->values_validated && changed_mode->taa[0][0] == -321
               && changed_mode->runtime->constant_readback.pending_context != nullptr,
           "mode change between pre and post returns before completing snapshots");
    FinishFrame(command, changed_mode.release());
    expect(state.runtime.get() == changed_runtime && !state.runtime->constant_readback.pending_context
               && state.claimed_frame == UINT64_MAX,
           "unreset mode change cancels old snapshot and releases only its matching claim");

    auto reset_mode = make_frame(true);
    requested_mode.store(AAMode::DLSS_QUALITY);
    Reset();
    auto* mode_replacement_runtime = state.runtime.get();
    state.claimed_frame = state.frame;
    EvaluateFrame(command, reset_mode.get());
    expect(!reset_mode->values_validated && reset_mode->taa[0][0] == -321
               && reset_mode->runtime->constant_readback.pending_context != nullptr,
           "mode-reset generation returns before completing old snapshots");
    FinishFrame(command, reset_mode.release());
    expect(state.runtime.get() == mode_replacement_runtime && state.claimed_frame == state.frame,
           "mode reset cancels retired snapshot without replacing runtime or clearing newer claim");

    state.claimed_frame = UINT64_MAX;

    auto advanced = make_frame(true);
    auto* advanced_runtime = advanced->runtime.get();
    ++state.frame;
    state.claimed_frame = state.frame;
    EvaluateFrame(command, advanced.get());
    expect(!advanced->values_validated && advanced->taa[0][0] == -321 && advanced->view[0][0] == -654
               && advanced->runtime->constant_readback.pending_context != nullptr,
           "advanced Present returns before readback completion");
    FinishFrame(command, advanced.release());
    expect(state.runtime.get() == advanced_runtime && !state.runtime->constant_readback.pending_context
               && state.claimed_frame == state.frame,
           "advanced Present cancels queue, returns runtime, and preserves newer claim");

    state.claimed_frame = UINT64_MAX;
    auto expired = make_frame(true);
    Reset();
    auto* replacement_runtime = state.runtime.get();
    state.claimed_frame = state.frame;
    EvaluateFrame(command, expired.get());
    expect(!expired->values_validated && expired->taa[0][0] == -321
               && expired->runtime->constant_readback.pending_context != nullptr,
           "expired generation returns before readback completion");
    FinishFrame(command, expired.release());
    expect(state.runtime.get() == replacement_runtime && state.claimed_frame == state.frame,
           "expired-generation runtime is retired without replacing or clearing newer state");

    state.claimed_frame = UINT64_MAX;
    auto disabled = make_frame(true);
    requested_mode.store(AAMode::OFF);
    EvaluateFrame(command, disabled.get());
    expect(disabled->taa[0][0] == -321 && disabled->runtime->constant_readback.pending_context != nullptr,
           "disabled post callback does not complete snapshots");
    FinishFrame(command, disabled.release());
    expect(state.runtime != nullptr && !state.runtime->constant_readback.pending_context
               && state.claimed_frame == UINT64_MAX,
           "disabled finalizer cancels snapshot and releases matching claim");
    expect(host_registration_calls.load() == 0 && host_log_messages.load() != 0,
           "standalone host only services logs and never attaches ReShade");
    state.runtime.reset();
    std::cout << "PASS: actual addon EvaluateFrame/FinishFrame callbacks; invalid-first/valid-second candidate; "
                 "nonfinite matrix; Quality/Balanced/Performance/Ultra Performance bounds; all integer Custom 33..100 with UE four-pixel "
                 "allocations in Ultra/Fast TAA; captured Fast 33 percent input and padded source; below33 rejection; "
                 "captured 85 percent 29/201 contracts; same-hash 100/85 transitions and stale "
                 "flag correction for every compute contract; exact/eight-pixel padding; six-entry temporal registration mapping; "
                 "mode scale and aspect/finite/padding guards; new mode transitions; changed mode; advanced Present; "
                 "expired generation; disabled post; queued GPU constants; "
                 "claim cancellation and runtime ownership. No NVIDIA initialization or game attachment.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
