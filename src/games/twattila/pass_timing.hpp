/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"

// Time Each Pass (a button): one frame's GPU time of every draw and dispatch, from a timestamp at each, into
// <prefix>_pass_timing.txt next to the exe, summed by pass (the same targets in a row) and by shader. Its events are the
// addon's first (Install before any other draw hook): a draw another hook replays and skips (command_action's post callbacks)
// still gets its stamp, and the addon's work at a command counts to it. Otherwise each event only tests a flag.
namespace twattila::pass_timing {

namespace internal {

using Microsoft::WRL::ComPtr;

constexpr uint32_t MAX_COMMANDS = 40000;
constexpr uint32_t WAIT_PRESENTS = 120;

struct Command {
  const char* kind = "";
  uint32_t count = 0;
  uint32_t vertex_shader = 0;
  uint32_t pixel_shader = 0;
  uint32_t compute_shader = 0;
  const void* target = nullptr;  // render target 0's resource, else the depth's, else UAV 0's (a dispatch)
};

enum class Phase : uint8_t { IDLE, REQUESTED, TIMING, READING };

struct State {
  std::mutex mutex;
  std::atomic_bool timing = false;  // phase TIMING, read without the lock by every event
  Phase phase = Phase::IDLE;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Query> disjoint;
  std::vector<ComPtr<ID3D11Query>> stamps;  // one before each command, one at the present
  std::vector<Command> commands;
  std::unordered_map<const void*, std::string> targets;  // described once, while they live
  uint32_t presents_waited = 0;
  std::string status = "Not run yet.";
};
static State state;

inline ID3D11DeviceContext* ImmediateContext(reshade::api::command_list* cmd_list) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return nullptr;
  return context;
}

inline std::string Describe(ID3D11Resource* resource) {
  if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    return std::format("{}x{}{} f{}", desc.Width, desc.Height, (desc.ArraySize > 1 ? std::format(" [{}]", desc.ArraySize) : std::string()),
                       static_cast<int>(desc.Format));
  }
  return "buffer";
}

template <typename View>
const void* Target(View* view) {
  if (view == nullptr) return nullptr;
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  view->Release();
  if (!state.targets.contains(resource.Get())) state.targets[resource.Get()] = Describe(resource.Get());
  return resource.Get();
}

inline void Stamp(reshade::api::command_list* cmd_list, const char* kind, uint32_t count, bool dispatch) {
  if (!state.timing.load(std::memory_order_relaxed)) return;
  ID3D11DeviceContext* context = ImmediateContext(cmd_list);
  if (context == nullptr) return;
  const std::lock_guard lock(state.mutex);
  if (state.phase != Phase::TIMING || state.commands.size() + 1 >= state.stamps.size()) return;
  Command command = {.kind = kind, .count = count};
  if (dispatch) {
    command.compute_shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::compute_shader);
    ID3D11UnorderedAccessView* uav = nullptr;
    context->CSGetUnorderedAccessViews(0, 1, &uav);
    command.target = Target(uav);
  } else {
    command.vertex_shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
    command.pixel_shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
    ID3D11RenderTargetView* colour = nullptr;
    ID3D11DepthStencilView* depth = nullptr;
    context->OMGetRenderTargets(1, &colour, &depth);
    command.target = (colour != nullptr ? Target(colour) : nullptr);
    if (command.target == nullptr) {
      command.target = Target(depth);
    } else if (depth != nullptr) {
      depth->Release();
    }
  }
  context->End(state.stamps[state.commands.size()].Get());
  state.commands.push_back(command);
}

inline bool OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t, uint32_t) {
  Stamp(cmd_list, "draw", vertex_count * instance_count, false);
  return false;
}

inline bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t, int32_t, uint32_t) {
  Stamp(cmd_list, "indexed", index_count * instance_count, false);
  return false;
}

inline bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t x, uint32_t y, uint32_t z) {
  Stamp(cmd_list, "dispatch", x * y * z, true);
  return false;
}

inline bool OnIndirect(reshade::api::command_list* cmd_list, reshade::api::indirect_command type, reshade::api::resource, uint64_t, uint32_t, uint32_t) {
  const bool dispatch = type == reshade::api::indirect_command::dispatch;
  Stamp(cmd_list, (dispatch ? "dispatch indirect" : "draw indirect"), 0, dispatch);
  return false;
}

inline std::string Shaders(const Command& command) {
  if (command.compute_shader != 0 || command.vertex_shader == 0) return std::format("cs {:08X}", command.compute_shader);
  return std::format("vs {:08X} ps {:08X}", command.vertex_shader, command.pixel_shader);
}

// The stamps read (the GPU has passed the present's), the file written.
inline void Write(ID3D11DeviceContext* context, const D3D11_QUERY_DATA_TIMESTAMP_DISJOINT& clock, const std::string& prefix) {
  const size_t count = state.commands.size();
  std::vector<uint64_t> ticks(count + 1);
  for (size_t index = 0; index <= count; ++index) {
    if (context->GetData(state.stamps[index].Get(), &ticks[index], sizeof(uint64_t), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
      state.status = "A timestamp was missing: run it again.";
      return;
    }
  }
  const auto ms = [&](uint64_t from, uint64_t to) { return (to > from) ? static_cast<double>(to - from) * 1000.0 / static_cast<double>(clock.Frequency) : 0.0; };
  const double total = ms(ticks.front(), ticks.back());
  std::string text = std::format(
      "One frame's GPU time: {:.3f} ms from the first draw or dispatch to the present, {} commands.\n"
      "A command's time runs from its timestamp to the next one: the addon's work at it, the command, then copies and clears before the next.\n"
      "With a CPU-limited frame the GPU waits between commands, and that wait counts too.\n",
      total, count);
  text += "\n== Passes: commands in a row with the same target (render target 0, else depth, else a dispatch's UAV 0) ==\nms\tcommands\ttarget\tfirst shaders\n";
  for (size_t first = 0; first < count;) {
    size_t end = first + 1;
    while (end < count && state.commands[end].target == state.commands[first].target) ++end;
    const auto target = state.targets.find(state.commands[first].target);
    text += std::format("{:.3f}\t{}\t{}\t{}\n", ms(ticks[first], ticks[end]), end - first,
                        (target == state.targets.end() ? std::string("-") : target->second), Shaders(state.commands[first]));
    first = end;
  }
  std::map<std::string, std::pair<double, uint32_t>> shaders;
  for (size_t index = 0; index < count; ++index) {
    auto& [shader_ms, shader_count] = shaders[Shaders(state.commands[index])];
    shader_ms += ms(ticks[index], ticks[index + 1]);
    ++shader_count;
  }
  std::vector<std::pair<std::string, std::pair<double, uint32_t>>> by_time(shaders.begin(), shaders.end());
  std::ranges::sort(by_time, [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
  text += "\n== Shaders, most time first ==\nms\tcommands\tshaders\n";
  for (const auto& [shader, sums] : by_time) text += std::format("{:.3f}\t{}\t{}\n", sums.first, sums.second, shader);
  text += "\n== Commands ==\nindex\tms\tkind\tcount\tshaders\ttarget\n";
  for (size_t index = 0; index < count; ++index) {
    const auto& command = state.commands[index];
    const auto target = state.targets.find(command.target);
    text += std::format("{}\t{:.4f}\t{}\t{}\t{}\t{} {}\n", index, ms(ticks[index], ticks[index + 1]), command.kind, command.count, Shaders(command),
                        command.target, (target == state.targets.end() ? std::string("-") : target->second));
  }
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  const auto path = std::filesystem::path(exe).parent_path() / (prefix + "_pass_timing.txt");
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
  state.status = std::format("Saved: {:.2f} ms, {} commands, {}.", total, count, path.filename().string());
  renodx::utils::log::i(std::format("[Total War] Time Each Pass: {:.3f} ms, {} commands, saved to {}.", total, count, path.string()));
}

}  // namespace internal

// The button: times the next whole frame.
inline void Request() {
  const std::lock_guard lock(internal::state.mutex);
  if (internal::state.phase != internal::Phase::IDLE) return;
  internal::state.phase = internal::Phase::REQUESTED;
  internal::state.status = "Waiting for the next frame.";
}

inline std::string Status() {
  const std::lock_guard lock(internal::state.mutex);
  return internal::state.status;
}

// Each present (before it): starts a requested frame, ends a timed one, reads its stamps once the GPU passed them.
// `prefix`: the file's name before _pass_timing.txt.
inline void OnPresent(reshade::api::command_queue* queue, const std::string& prefix) {
  using internal::Phase;
  auto& state = internal::state;
  const std::lock_guard lock(state.mutex);
  if (state.phase == Phase::IDLE) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  if (context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  switch (state.phase) {
    case Phase::REQUESTED: {
      Microsoft::WRL::ComPtr<ID3D11Device> device;
      context->GetDevice(device.GetAddressOf());
      if (state.device != device) {
        state.stamps.clear();
        state.disjoint.Reset();
        state.device = device;
      }
      const D3D11_QUERY_DESC stamp_desc = {.Query = D3D11_QUERY_TIMESTAMP};
      const D3D11_QUERY_DESC disjoint_desc = {.Query = D3D11_QUERY_TIMESTAMP_DISJOINT};
      if (!state.disjoint && FAILED(device->CreateQuery(&disjoint_desc, state.disjoint.GetAddressOf()))) {
        state.phase = Phase::IDLE;
        state.status = "No timestamps on this GPU.";
        return;
      }
      while (state.stamps.size() < internal::MAX_COMMANDS + 1) {
        if (Microsoft::WRL::ComPtr<ID3D11Query> stamp; SUCCEEDED(device->CreateQuery(&stamp_desc, stamp.GetAddressOf()))) {
          state.stamps.push_back(stamp);
        } else {
          break;
        }
      }
      if (state.stamps.empty()) {
        state.phase = Phase::IDLE;
        state.status = "No timestamps on this GPU.";
        return;
      }
      state.commands.clear();
      state.targets.clear();
      context->Begin(state.disjoint.Get());
      state.phase = Phase::TIMING;
      state.timing.store(true);
      break;
    }
    case Phase::TIMING:
      context->End(state.stamps[state.commands.size()].Get());
      context->End(state.disjoint.Get());
      state.presents_waited = 0;
      state.phase = Phase::READING;
      state.timing.store(false);
      break;
    case Phase::READING: {
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock = {};
      if (context->GetData(state.disjoint.Get(), &clock, sizeof(clock), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
        if (clock.Disjoint != FALSE) {
          state.status = "The GPU's clock changed during the frame: run it again.";
        } else {
          internal::Write(context, clock, prefix);
        }
        state.phase = Phase::IDLE;
      } else if (++state.presents_waited > internal::WAIT_PRESENTS) {
        state.status = "The GPU gave no times: run it again.";
        state.phase = Phase::IDLE;
      }
      if (state.phase == Phase::IDLE) state.targets.clear();
      break;
    }
    case Phase::IDLE:
      break;
  }
}

// Device loss: nothing timing, queries released.
inline void OnDestroyDevice(reshade::api::device*) {
  auto& state = internal::state;
  const std::lock_guard lock(state.mutex);
  state.timing.store(false);
  state.phase = internal::Phase::IDLE;
  state.stamps.clear();
  state.disjoint.Reset();
  state.device.Reset();
  state.commands.clear();
  state.targets.clear();
}

// Before any other draw or dispatch hook of the addon (their run order is registration order).
inline void Install() {
  reshade::register_event<reshade::addon_event::draw>(internal::OnDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(internal::OnDrawIndexed);
  reshade::register_event<reshade::addon_event::dispatch>(internal::OnDispatch);
  reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(internal::OnIndirect);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::draw>(internal::OnDraw);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(internal::OnDrawIndexed);
  reshade::unregister_event<reshade::addon_event::dispatch>(internal::OnDispatch);
  reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(internal::OnIndirect);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  OnDestroyDevice(nullptr);
}

}  // namespace twattila::pass_timing
