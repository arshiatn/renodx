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
#include "../../utils/settings.hpp"
#include "../../utils/state.hpp"
#include "./pass_dump.hpp"

// Time Each Pass (a button): one frame's GPU time of every draw and dispatch, from a timestamp at each, into
// <prefix>_pass_timing.txt next to the exe, summed by pass (the same targets in a row) and by shader. Its events are the
// addon's first (Install before any other draw hook): a draw another hook replays and skips (command_action's post callbacks)
// still gets its stamp, and the addon's work at a command counts to it. Otherwise each event only tests a flag.
// A request first counts one frame's commands, then makes the timestamps it needs (a quarter more and 1024 spare) a chunk
// a present (creating 40,001 in one present took ~100 ms), then times the next frame; the stamps stay for later runs. A
// timed frame with more commands than stamps makes more and times another frame, until the stamps can't grow (the cap,
// or the device makes no more): then its first commands are saved.
// In an add-on: Install("<prefix>") first in its AttachAddon, Button(<section>) among its settings (graphics_page.hpp
// places it by its label), Uninstall() in its DetachAddon. Its own present event ends and reads the timed frame.
// The add-on's own passes (native calls, which no event sees) are marked with OwnPass around them: the file's "Add-on
// passes" sections give each pass and each host command without them.
namespace twattila::pass_timing {

namespace internal {

using Microsoft::WRL::ComPtr;

// Stamps are kept until the device goes away (>= 13 MB at the cap in a 32-bit process); the biggest frame timed so far
// had 7,153 commands.
constexpr uint32_t MAX_COMMANDS = 40000;
constexpr uint32_t STAMPS_A_PRESENT = 1000;
constexpr uint32_t WAIT_PRESENTS = 120;

// GAME: a draw or dispatch event; OWN: an add-on pass's native calls inside one (OwnPass); REST: that event's time
// after the pass (its kind, shaders and target).
enum class Role : uint8_t { GAME, OWN, REST };

struct Command {
  const char* kind = "";
  uint32_t count = 0;
  uint32_t vertex_shader = 0;
  uint32_t pixel_shader = 0;
  uint32_t compute_shader = 0;
  const void* target = nullptr;  // render target 0's resource, else the depth's, else UAV 0's (a dispatch)
  Role role = Role::GAME;
  size_t host = SIZE_MAX;  // OWN and REST: the game command's index
};

enum class Phase : uint8_t { IDLE, REQUESTED, COUNTING, PREPARING, TIMING, READING };

struct State {
  std::mutex mutex;
  std::atomic_bool in_frame = false;  // phase COUNTING or TIMING, read without the lock by every event
  Phase phase = Phase::IDLE;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Query> disjoint;
  std::vector<ComPtr<ID3D11Query>> stamps;  // one before each command, one at the present
  uint32_t counted = 0;        // the counting frame's commands
  uint32_t unstamped = 0;      // the timed frame's commands after the stamps ran out
  uint32_t most_commands = 0;  // the most commands a counted or timed frame had on this device
  bool stamps_capped = false;  // the timed frame's stamps can't grow (MAX_COMMANDS, or CreateQuery failed)
  bool own_open = false;       // an OwnPass is open (marks don't nest)
  std::vector<Command> commands;
  std::unordered_map<const void*, std::string> targets;  // described once, while they live
  uint32_t presents_waited = 0;
  std::string status = "Not run yet.";
  std::string prefix;  // the file's name before _pass_timing.txt (Install)
};
static State state;

inline ID3D11DeviceContext* ImmediateContext(reshade::api::command_list* cmd_list) {
  // The timed frame's draws on another API's device (the port's other routes) are not D3D11 contexts.
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
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
  if (!state.in_frame.load(std::memory_order_relaxed)) return;
  ID3D11DeviceContext* context = ImmediateContext(cmd_list);
  if (context == nullptr) return;
  const std::lock_guard lock(state.mutex);
  if (state.phase == Phase::COUNTING) {
    ++state.counted;
    return;
  }
  if (state.phase != Phase::TIMING) return;
  if (state.commands.size() + 1 >= state.stamps.size()) {
    ++state.unstamped;
    return;
  }
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
  // Each own pass (OwnPass: its native calls) by name and host shaders, then each game command that hosted some: its
  // time without them (its own interval and its REST ones) and theirs.
  std::map<std::string, std::pair<double, uint32_t>> own_passes;
  std::map<size_t, std::pair<double, double>> hosts;  // by the game command's index: its time without the passes, theirs
  for (size_t index = 0; index < count; ++index) {
    const auto& command = state.commands[index];
    const double interval = ms(ticks[index], ticks[index + 1]);
    if (command.role == Role::OWN) {
      auto& [pass_ms, pass_count] = own_passes[std::format("{}\t{}", command.kind, Shaders(command))];
      pass_ms += interval;
      ++pass_count;
      hosts[command.host].second += interval;
    } else if (command.role == Role::REST) {
      hosts[command.host].first += interval;
    }
  }
  text += "\n== Add-on passes: each own pass (its native calls inside a game command) ==\nms\tcount\tpass\thost shaders\n";
  for (const auto& [pass, sums] : own_passes) text += std::format("{:.3f}\t{}\t{}\n", sums.first, sums.second, pass);
  text += "\n== Add-on passes: each host command without them ==\nindex\tms without\tms of the passes\tkind\tshaders\ttarget\n";
  for (const auto& [host, sums] : hosts) {
    const auto& command = state.commands[host];
    const auto target = state.targets.find(command.target);
    text += std::format("{}\t{:.4f}\t{:.4f}\t{}\t{}\t{} {}\n", host, sums.first + ms(ticks[host], ticks[host + 1]), sums.second, command.kind,
                        Shaders(command), command.target, (target == state.targets.end() ? std::string("-") : target->second));
  }
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
  const std::string commands_saved = (state.unstamped == 0 ? std::format("{} commands", count)
                                                           : std::format("first {} of {} commands", count, count + state.unstamped));
  state.status = std::format("Saved: {:.2f} ms, {}, {}.", total, commands_saved, path.filename().string());
  renodx::utils::log::i(std::format("[Total War] Time Each Pass: {:.3f} ms, {}, saved to {}.", total, commands_saved, path.string()));
}

}  // namespace internal

// An add-on pass's native calls inside a game command (ReShade never sees them, so no event stamps them), around them:
// in a timed frame two commands, the pass (kind `name`, the host's shaders and target: the Passes and Shaders sections
// stay as without it) and the rest of the host command (REST, count 0), so the game's draw after the pass isn't booked
// on it; in a counting frame 2. `name` must outlive the run (a literal): the pass's shader by its embed name (the pass
// dump's uses.tsv stem), `stage` the one whose bindings that row lists. Marks don't nest (an inner one does nothing).
enum class Stage : uint8_t { COMPUTE, PIXEL };

class OwnPass {
 public:
  OwnPass(ID3D11DeviceContext* native, const char* name, Stage stage) : native_(native), name_(name), stage_(stage) {
    auto& state = internal::state;
    if (!state.in_frame.load(std::memory_order_relaxed)) return;
    // The device's API: a native D3D11 context, the immediate one (as Stamp's ImmediateContext).
    if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    const std::lock_guard lock(state.mutex);
    if (state.own_open) return;
    if (state.phase == internal::Phase::COUNTING) {
      state.counted += 2;
      state.own_open = true;
      timed_ = true;
      return;
    }
    if (state.phase != internal::Phase::TIMING || state.commands.empty()) return;
    if (state.commands.size() + 2 >= state.stamps.size()) {
      state.unstamped += 2;
      return;
    }
    const internal::Command& last = state.commands.back();
    const size_t host = (last.role == internal::Role::GAME ? state.commands.size() - 1 : last.host);
    internal::Command own = state.commands[host];
    own.kind = name;
    own.count = 0;
    own.role = internal::Role::OWN;
    own.host = host;
    native->End(state.stamps[state.commands.size()].Get());
    state.commands.push_back(own);
    state.own_open = true;
    timed_ = true;
    own_index_ = state.commands.size() - 1;
  }
  ~OwnPass() {
    pass_dump::OwnPassDone(native_, name_, stage_ == Stage::COMPUTE);
    if (!timed_) return;
    auto& state = internal::state;
    const std::lock_guard lock(state.mutex);
    state.own_open = false;
    // A counting mark, or the frame ended meanwhile (it can't: one command's calls): no rest.
    if (own_index_ == SIZE_MAX || state.phase != internal::Phase::TIMING || own_index_ + 1 != state.commands.size()) return;
    internal::Command rest = state.commands[state.commands[own_index_].host];
    rest.count = 0;
    rest.role = internal::Role::REST;
    rest.host = state.commands[own_index_].host;
    native_->End(state.stamps[state.commands.size()].Get());
    state.commands.push_back(rest);
  }
  OwnPass(const OwnPass&) = delete;
  OwnPass& operator=(const OwnPass&) = delete;

 private:
  ID3D11DeviceContext* native_;
  const char* name_;
  Stage stage_;
  bool timed_ = false;           // this mark is open in Time Each Pass
  size_t own_index_ = SIZE_MAX;  // its OWN command (none in a counting frame)
};

// The button: counts the next frame's commands, makes their stamps, then times a whole frame.
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

// Each present (before it; the add-on's first present event, Install's): starts a requested frame's count, makes the
// stamps the counted frame needs (STAMPS_A_PRESENT a present), starts the timed frame once they exist, ends it, reads its
// stamps once the GPU passed them.
inline void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
                      uint32_t, const reshade::api::rect*) {
  using internal::Phase;
  auto& state = internal::state;
  const std::lock_guard lock(state.mutex);
  if (state.phase == Phase::IDLE) return;
  // The button and RENODX_TW_SET ask on any device: only a D3D11 queue is a context.
  if (queue->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  if (context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  // A run started stays on its device: the stamps are its queries.
  if (state.phase != Phase::REQUESTED && device != state.device) return;
  switch (state.phase) {
    case Phase::REQUESTED: {
      if (state.device != device) {
        state.stamps.clear();
        state.disjoint.Reset();
        state.most_commands = 0;
        state.device = device;
      }
      const D3D11_QUERY_DESC disjoint_desc = {.Query = D3D11_QUERY_TIMESTAMP_DISJOINT};
      if (!state.disjoint && FAILED(device->CreateQuery(&disjoint_desc, state.disjoint.GetAddressOf()))) {
        state.phase = Phase::IDLE;
        state.status = "No timestamps on this GPU.";
        return;
      }
      state.counted = 0;
      state.phase = Phase::COUNTING;
      state.in_frame.store(true);
      break;
    }
    case Phase::COUNTING:
      state.in_frame.store(false);
      state.most_commands = std::max(state.most_commands, state.counted);
      state.phase = Phase::PREPARING;
      [[fallthrough]];
    case Phase::PREPARING: {
      const size_t wanted_commands = size_t{state.most_commands} + (state.most_commands / 4) + 1024;
      const size_t wanted = std::min<size_t>(internal::MAX_COMMANDS, wanted_commands) + 1;
      const size_t chunk_end = std::min<size_t>(wanted, state.stamps.size() + internal::STAMPS_A_PRESENT);
      const D3D11_QUERY_DESC stamp_desc = {.Query = D3D11_QUERY_TIMESTAMP};
      while (state.stamps.size() < chunk_end) {
        Microsoft::WRL::ComPtr<ID3D11Query> stamp;
        if (FAILED(state.device->CreateQuery(&stamp_desc, stamp.GetAddressOf()))) break;
        state.stamps.push_back(stamp);
      }
      const bool out_of_queries = (state.stamps.size() < chunk_end);
      if (out_of_queries && state.stamps.empty()) {
        state.phase = Phase::IDLE;
        state.status = "No timestamps on this GPU.";
        return;
      }
      // More next present, unless the device makes no more: then the frame is timed with what exists.
      if (!out_of_queries && state.stamps.size() < wanted) break;
      state.stamps_capped = out_of_queries || state.stamps.size() >= size_t{internal::MAX_COMMANDS} + 1;
      state.commands.clear();
      state.targets.clear();
      state.unstamped = 0;
      context->Begin(state.disjoint.Get());
      state.phase = Phase::TIMING;
      state.in_frame.store(true);
      break;
    }
    case Phase::TIMING:
      context->End(state.stamps[state.commands.size()].Get());
      context->End(state.disjoint.Get());
      state.in_frame.store(false);
      state.most_commands = std::max(state.most_commands, static_cast<uint32_t>(state.commands.size()) + state.unstamped);
      if (state.unstamped != 0 && !state.stamps_capped) {
        // Stamps for at least this frame's commands, then another frame timed: one press, one file.
        state.phase = Phase::PREPARING;
        state.targets.clear();
        break;
      }
      // All stamped, or the stamps can't grow: the first commands saved (Write names the rest).
      state.presents_waited = 0;
      state.phase = Phase::READING;
      break;
    case Phase::READING: {
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock = {};
      if (context->GetData(state.disjoint.Get(), &clock, sizeof(clock), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
        if (clock.Disjoint != FALSE) {
          state.status = "The GPU's clock changed during the frame: run it again.";
        } else {
          internal::Write(context, clock, state.prefix);
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

// The run's device going away (null: Uninstall): nothing timing, queries released. Other devices (an NGX D3D12 device
// made again, another D3D11 device) leave the run and the kept stamps alone.
inline void OnDestroyDevice(reshade::api::device* destroyed) {
  auto& state = internal::state;
  const std::lock_guard lock(state.mutex);
  if (destroyed != nullptr) {
    if (destroyed->get_api() != reshade::api::device_api::d3d11) return;
    auto* native = reinterpret_cast<ID3D11Device*>(destroyed->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native != state.device.Get()) return;
  }
  if (state.phase != internal::Phase::IDLE) {
    state.status = "The game's device went away during the run: run it again.";
  }
  state.in_frame.store(false);
  state.phase = internal::Phase::IDLE;
  state.stamps.clear();
  state.disjoint.Reset();
  state.device.Reset();
  state.most_commands = 0;
  state.commands.clear();
  state.targets.clear();
}

// Before any other draw, dispatch or present hook of the addon (their run order is registration order). `prefix`: the
// file's name before _pass_timing.txt.
inline void Install(const std::string& prefix) {
  pass_dump::Install();
  {
    const std::lock_guard lock(internal::state.mutex);
    internal::state.prefix = prefix;
  }
  reshade::register_event<reshade::addon_event::draw>(internal::OnDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(internal::OnDrawIndexed);
  reshade::register_event<reshade::addon_event::dispatch>(internal::OnDispatch);
  reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(internal::OnIndirect);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::draw>(internal::OnDraw);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(internal::OnDrawIndexed);
  reshade::unregister_event<reshade::addon_event::dispatch>(internal::OnDispatch);
  reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(internal::OnIndirect);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  OnDestroyDevice(nullptr);
  pass_dump::Uninstall();
}

// The add-on's button in `section` (Graphics 1 of 2 moves it under Ray Tracing Extra Options by its label).
inline renodx::utils::settings::Setting* Button(const std::string& section) {
  return new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::BUTTON,
      .label = "Time Each Pass (One Frame)",
      .section = section,
      .tooltip = "The GPU's time of every pass of the next frame, saved as ..._pass_timing.txt next to the game's exe.",
      .on_change = [] { Request(); },
  };
}

}  // namespace twattila::pass_timing
