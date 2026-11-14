/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtl/phmap.hpp>
#include <include/reshade.hpp>

#include "./command_action.hpp"
#include "./cross_addon.hpp"
#include "./pipeline.hpp"
#include "./platform.hpp"
#include "./shader.hpp"
#include "./state.hpp"

// Opt-in fast dispatch of command_action's draw, indexed draw and dispatch commands (the Total War add-ons turn it on;
// every other module keeps RunCallbacks). RunCallbacks recomputes, on every command, which callbacks the bound
// pipelines have: five bound-slot reads, a registrations_by_pipeline_handle lookup under its shared lock per bound
// pipeline, every command-wide callback called. This keeps the answer per command list: what each pipeline brings to
// its slot, resolved at its first bind, and each command type's command-wide callbacks with their gates, both until the
// registrations, the modules or the gates change (the epoch); a command reads its slots and combines only what changed.
// A command-wide callback with a gate (SetGate) is called only when its gate says it can act. Everything else is
// RunCallbacks' order and semantics, which the replay oracle (tools/tw/draw_redesign) and test/tw_draw_dispatch
// compare decision for decision. command_action's OfferOrRunCallbacks offers each command (Use sets its hook).
//
// Invariants:
// - Only D3D11 command lists, and only pipelines whose prepared callbacks all sit on the stage the pipeline is bound
//   at, in registration order (one shader a pipeline). Anything else is left to RunCallbacks for that command.
// - A command list's cache borrows command_action's registrations across commands: per slot and pipeline handle its
//   prepared callbacks (registrations_by_pipeline_handle), per command type the command-wide callbacks
//   (global_callbacks_by_command_type). Every change to them bumps the epoch, which drops what the caches hold:
//   command_action's OnInitPipeline / OnDestroyPipeline / RegisterCallback (command-wide) / UnregisterCallback in every
//   module that opted in, also after its detach (command_action::internal::on_registrations_changed stays set). A
//   module without the opt-in doesn't bump it:
//   while one is attached to command_action, every command takes RunCallbacks; when the attached modules change, the
//   caches resolve everything again (module signature). Registrations, like today, must not change while a command
//   dispatches.
namespace renodx::utils::command_action::fast {

// When a command-wide callback (Register without a shader hash) can act. A gate may only leave out calls that would
// return {} and change nothing; on the fast dispatch the callback is then not called at all. Gates are read before any
// of the command's callbacks run: no callback may make another one's gate true within the same command.
struct Gate {
  // Per command; null: always.
  bool (*active)(reshade::api::command_list* cmd_list) = nullptr;
  // Per pipeline and stage, at its first bind after the epoch moved: whether the callback can act with `pipeline` bound
  // at `stage`. Its answer must hold until the pipeline is destroyed (or SetGate / a registration moves the epoch). The
  // command calls the callback only when one of its bound pipelines said so. Null: with any pipeline.
  bool (*pipeline_active)(reshade::api::pipeline pipeline, reshade::api::pipeline_stage stage) = nullptr;
};

namespace internal {

constexpr size_t MAX_MODULES = 8u;
constexpr size_t MAX_GATES = 8u;
// The callbacks one command can run with two pipelines contributing (RunCallbacks' identity set); a command with
// more takes RunCallbacks.
constexpr size_t MAX_CALLED = 64u;
// A command type's command-wide callbacks (a bit each); a command type with more takes RunCallbacks.
constexpr size_t MAX_GLOBALS = 64u;

// The command types this dispatch takes; the others never reach it.
constexpr std::array FAST_COMMAND_TYPES = {DrawArguments::COMMAND_TYPE, DrawIndexedArguments::COMMAND_TYPE, DispatchArguments::COMMAND_TYPE};

// The slots a command reads, in RunCallbacks' stage order: the graphics stages, then compute.
constexpr std::array SLOT_STAGES = {
    reshade::api::pipeline_stage::vertex_shader,
    reshade::api::pipeline_stage::hull_shader,
    reshade::api::pipeline_stage::domain_shader,
    reshade::api::pipeline_stage::geometry_shader,
    reshade::api::pipeline_stage::pixel_shader,
    reshade::api::pipeline_stage::compute_shader,
};
constexpr size_t COMPUTE_SLOT = 5u;

struct GateEntry {
  const void* callback = nullptr;  // command_action::internal::CALLBACK_IDENTITY of the gated callback
  Gate gate = {};
};

// Process-shared: the opted-in modules, their gates, the epoch. Own uuid: command_action's SharedData keeps its layout
// for modules without the opt-in. A layout change here needs a new uuid (modules of two layouts then don't see each
// other's opt-in, and both take RunCallbacks).
struct __declspec(uuid("5f3b0c62-9d4e-4f7a-b1c8-2e6a7d904c13")) SharedData {
  std::shared_mutex mutex;  // modules, gates
  std::array<cross_addon::internal::ModuleToken, MAX_MODULES> modules = {};
  size_t module_count = 0u;
  std::array<GateEntry, MAX_GATES> gates = {};
  size_t gate_count = 0u;
  std::atomic<uint64_t> epoch = 0u;
};

static cross_addon::Shared<SharedData> shared;

// command_action's control block: its attached modules. cross_addon keeps a control block and its module records for
// the life of the process.
static const cross_addon::internal::ControlBlock<command_action::internal::SharedData>* action_control = nullptr;

// What a pipeline brings to the slot it is bound at. Fixed until the epoch moves, so each list keeps it per slot and
// handle (CommandListCache::resolved): a rebind costs a lookup, not registrations_by_pipeline_handle's lock.
struct Resolution {
  // Borrowed from registrations_by_pipeline_handle (valid until the epoch moves), by command index.
  std::array<const cross_addon::vector<command_action::internal::PipelineRegistration>*, COMMAND_INDEX_COUNT> matches = {};
  uint32_t command_types = 0u;  // of DrawArguments, DrawIndexedArguments, DispatchArguments
  uint32_t gate_bits = 0u;      // the gates (CommandListCache::gates) this pipeline arms
  bool in_order = true;         // every match on this slot's stage alone, by registration position
};

// One bound slot.
struct Slot {
  reshade::api::pipeline pipeline = {0u};
  reshade::api::pipeline_stage applied_stages = {};
  Resolution resolution = {};
};

// The slots a command reads, combined.
struct Bound {
  uint32_t command_types = 0u;
  uint32_t gate_bits = 0u;
  bool in_order = true;
};

// One command type's command-wide callbacks with their gates, as of the epoch.
struct Globals {
  // Borrowed from global_callbacks_by_command_type (valid until the epoch moves); null: none.
  const cross_addon::vector<command_action::internal::CallbackRegistration>* registrations = nullptr;
  bool too_many = false;  // more than MAX_GLOBALS: the command takes RunCallbacks
  uint64_t ungated = 0u;  // by registration index
  uint64_t gated = 0u;    // by registration index; its gate: gate_of[index] (CommandListCache::gates)
  std::array<uint8_t, MAX_GLOBALS> gate_of = {};
};

// Per command list and module (ReShade private data: cache_guid); used by the thread recording the list.
struct CommandListCache {
  bool d3d11 = false;
  // state.hpp's handle: set at the list's init_command_list, freed at its destroy_command_list (as this cache is).
  const state::internal::CommandListStateHandle* state_handle = nullptr;
  uintptr_t module_signature = 0u;
  uint64_t epoch = UINT64_MAX;
  bool all_opted_in = false;
  std::array<GateEntry, MAX_GATES> gates = {};
  size_t gate_count = 0u;
  std::array<Globals, COMMAND_INDEX_COUNT> globals = {};  // by command index (FAST_COMMAND_TYPES')
  std::array<Slot, SLOT_STAGES.size()> slots = {};
  // Per slot, the pipelines resolved since the epoch moved, by handle.
  std::array<gtl::flat_hash_map<uint64_t, Resolution>, SLOT_STAGES.size()> resolved;
  Bound graphics = {};
  Bound compute = {};
};

// This module's private-data key: a fixed prefix and the module's token, so a module never reads another's cache.
static std::array<uint8_t, 16> cache_guid = {};
static std::shared_mutex caches_mutex;
static std::unordered_map<reshade::api::command_list*, std::unique_ptr<CommandListCache>> caches;

inline void BumpEpoch() {
  if (shared.data != nullptr) {
    shared.data->epoch.fetch_add(1u, std::memory_order_relaxed);
  }
}

// Registered by every opted-in module for its own caches (module-local memory, so not the cross-addon handler).
inline void OnDestroyCommandList(reshade::api::command_list* cmd_list) {
  uint64_t value = 0u;
  cmd_list->get_private_data(cache_guid.data(), &value);
  if (value == 0u) return;
  cmd_list->set_private_data(cache_guid.data(), 0u);
  const std::unique_lock lock(caches_mutex);
  caches.erase(cmd_list);
}

// RunCallbacks for one command, from the cache; false: this command takes RunCallbacks. Mirrors RunCallbacks' order and
// results: the command-wide callbacks (those that can act) in registration order, then per stage slot its pipeline's
// callbacks by registration position, a callback identity once per command when two pipelines contribute; bypass runs
// the pending finalize callbacks in reverse and skips the command; post / finalize / replay redraw the command, then
// post in order, finalize in reverse.
template <typename Arguments>
bool Run(CommandContext<Arguments>* context, bool* skip) {
  constexpr bool DISPATCH = std::is_same_v<Arguments, DispatchArguments>;
  constexpr uint32_t COMMAND_TYPE = Arguments::COMMAND_TYPE;
  constexpr size_t COMMAND_INDEX = Arguments::COMMAND_INDEX;
  constexpr size_t FIRST_SLOT = DISPATCH ? COMPUTE_SLOT : 0u;
  constexpr size_t LAST_SLOT = DISPATCH ? SLOT_STAGES.size() : COMPUTE_SLOT;
  using Context = CommandContext<Arguments>;

  auto* const cmd_list = context->cmd_list;
  if (cmd_list == nullptr || action_control == nullptr || shared.data == nullptr) return false;

  // The command list's cache, made on its first command.
  uint64_t cache_value = 0u;
  cmd_list->get_private_data(cache_guid.data(), &cache_value);
  auto* cache = reinterpret_cast<CommandListCache*>(static_cast<uintptr_t>(cache_value));
  if (cache == nullptr) {
    auto made = std::make_unique<CommandListCache>();
    made->d3d11 = (cmd_list->get_device()->get_api() == reshade::api::device_api::d3d11);
    cache = made.get();
    {
      const std::unique_lock lock(caches_mutex);
      caches.insert_or_assign(cmd_list, std::move(made));
    }
    cmd_list->set_private_data(cache_guid.data(), reinterpret_cast<uintptr_t>(cache));
  }
  if (!cache->d3d11) return false;
  if (cache->state_handle == nullptr) {
    cache->state_handle = state::internal::GetCommandListStateHandle(cmd_list);
    if (cache->state_handle == nullptr) return false;
  }
  const auto* state_handle = cache->state_handle;

  // Everything again when the epoch moved or the modules attached to command_action changed (the sum of the active
  // module records' addresses): whether every one of them opted in, the gates, each command type's command-wide
  // callbacks with their gates; the pipelines' resolutions dropped, every slot read.
  uintptr_t module_signature = 0u;
  for (const auto* module = action_control->modules; module != nullptr; module = module->next) {
    if (module->active) {
      module_signature += reinterpret_cast<uintptr_t>(module);
    }
  }
  const uint64_t epoch = shared.data->epoch.load(std::memory_order_relaxed);
  const bool every_slot = (module_signature != cache->module_signature || epoch != cache->epoch);
  if (every_slot) {
    cache->module_signature = module_signature;
    cache->epoch = epoch;
    const std::shared_lock lock(shared.data->mutex);
    const auto opted_in = std::span(shared.data->modules.data(), shared.data->module_count);
    cache->all_opted_in = true;
    for (const auto* module = action_control->modules; module != nullptr; module = module->next) {
      if (module->active && std::ranges::find(opted_in, module->token) == opted_in.end()) {
        cache->all_opted_in = false;
      }
    }
    cache->gates = shared.data->gates;
    cache->gate_count = shared.data->gate_count;
    for (auto& resolved : cache->resolved) {
      resolved.clear();
    }
    const auto gates = std::span(cache->gates.data(), cache->gate_count);
    for (const uint32_t command_type : FAST_COMMAND_TYPES) {
      Globals& globals = cache->globals[std::countr_zero(command_type)];
      globals = {};
      command_action::internal::shared.data->global_callbacks_by_command_type.if_contains(command_type, [&](const auto& entry) {
        globals.registrations = &entry.second;
      });
      if (globals.registrations == nullptr) continue;
      globals.too_many = globals.registrations->size() > MAX_GLOBALS;
      for (size_t index = 0u; index < globals.registrations->size() && index < MAX_GLOBALS; ++index) {
        const auto gate = std::ranges::find(gates, (*globals.registrations)[index].callback, &GateEntry::callback);
        if (gate == gates.end()) {
          globals.ungated |= uint64_t{1u} << index;
        } else {
          globals.gated |= uint64_t{1u} << index;
          globals.gate_of[index] = static_cast<uint8_t>(gate - gates.begin());
        }
      }
    }
  }
  if (!cache->all_opted_in) return false;

  // The command's slots from state.hpp's tracked binds (as RunCallbacks' collect_stage reads them). A slot whose
  // pipeline changed takes that pipeline's resolution: its prepared callbacks by command index (borrowed), whether they
  // run in vector order, the gates it arms; made on the pipeline's first bind at that slot since the epoch moved. Every
  // slot when every_slot (the other kind's slots then hold this epoch too).
  const size_t first_read = (every_slot ? 0u : FIRST_SLOT);
  const size_t last_read = (every_slot ? SLOT_STAGES.size() : LAST_SLOT);
  bool graphics_changed = every_slot;
  bool compute_changed = every_slot;
  state::internal::WithCommandListState(state_handle, [&](const auto& tracked) {
    for (size_t slot_index = first_read; slot_index < last_read; ++slot_index) {
      const auto stage = SLOT_STAGES[slot_index];
      state::PipelineBind bind;
      const auto pipeline = tracked.GetBoundShaderPipeline(stage, &bind);
      Slot& slot = cache->slots[slot_index];
      if (!every_slot && pipeline == slot.pipeline && (pipeline.handle == 0u || bind.stages == slot.applied_stages)) continue;
      graphics_changed = graphics_changed || slot_index < COMPUTE_SLOT;
      compute_changed = compute_changed || slot_index == COMPUTE_SLOT;
      slot.pipeline = pipeline;
      slot.applied_stages = bind.stages;
      if (pipeline.handle == 0u) {
        slot.resolution = {};
        continue;
      }
      const auto [found, made] = cache->resolved[slot_index].try_emplace(pipeline.handle);
      Resolution& resolution = found->second;
      if (made) {
        const auto stage_index = renodx::utils::pipeline::GetShaderStageIndex(stage);
        command_action::internal::shared.data->registrations_by_pipeline_handle.if_contains(pipeline.handle, [&](const auto& entry) {
          const auto& [pipeline_handle, prepared] = entry;
          // Only the command types this dispatch takes (a lookup each, cheaper than walking the nested map).
          for (const uint32_t command_type : FAST_COMMAND_TYPES) {
            prepared.registrations_by_command_type.if_contains(command_type, [&](const auto& group) {
              const auto& matches = group.second;
              if (matches.empty()) return;
              resolution.matches[std::countr_zero(command_type)] = &matches;
              resolution.command_types |= command_type;
              // RunCallbacks runs a stage's matches by registration position; this runs them in vector order.
              size_t next_position = 0u;
              for (const auto& match : matches) {
                const size_t position = match.registration_position_by_stage[stage_index];
                if (match.eligible_stages != static_cast<uint32_t>(stage) || position < next_position) {
                  resolution.in_order = false;
                }
                next_position = position + 1u;
              }
            });
          }
        });
        for (size_t gate = 0u; gate < cache->gate_count; ++gate) {
          const auto pipeline_active = cache->gates[gate].gate.pipeline_active;
          if (pipeline_active != nullptr && pipeline_active(pipeline, stage)) {
            resolution.gate_bits |= 1u << gate;
          }
        }
      }
      slot.resolution = resolution;
    }
  });
  // A pipeline bound in two slots (at several stages) is RunCallbacks' merge, not this.
  const auto combine = [&](size_t first, size_t last) {
    Bound combined = {};
    for (size_t slot_index = first; slot_index < last; ++slot_index) {
      const Slot& slot = cache->slots[slot_index];
      combined.command_types |= slot.resolution.command_types;
      combined.gate_bits |= slot.resolution.gate_bits;
      combined.in_order = combined.in_order && slot.resolution.in_order;
      for (size_t other = first; other < slot_index; ++other) {
        if (slot.pipeline.handle != 0u && cache->slots[other].pipeline == slot.pipeline) {
          combined.in_order = false;
        }
      }
    }
    return combined;
  };
  if (graphics_changed) {
    cache->graphics = combine(0u, COMPUTE_SLOT);
  }
  if (compute_changed) {
    cache->compute = combine(COMPUTE_SLOT, SLOT_STAGES.size());
  }
  const Bound& bound = DISPATCH ? cache->compute : cache->graphics;
  if (!bound.in_order) return false;

  // The command-wide callbacks that can act now: ungated, or their gate says so.
  const Globals& globals = cache->globals[COMMAND_INDEX];
  if (globals.too_many) return false;
  uint64_t acting_globals = globals.ungated;
  for (uint64_t gated = globals.gated; gated != 0u; gated &= gated - 1u) {
    const size_t index = std::countr_zero(gated);
    const size_t gate = globals.gate_of[index];
    const Gate& gate_functions = cache->gates[gate].gate;
    if ((gate_functions.pipeline_active == nullptr || (bound.gate_bits & (1u << gate)) != 0u)
        && (gate_functions.active == nullptr || gate_functions.active(cmd_list))) {
      acting_globals |= uint64_t{1u} << index;
    }
  }
  if (acting_globals == 0u && (bound.command_types & COMMAND_TYPE) == 0u) {
    *skip = false;
    return true;
  }
  size_t contributing = 0u;
  size_t match_count = 0u;
  for (size_t slot_index = FIRST_SLOT; slot_index < LAST_SLOT; ++slot_index) {
    const auto* matches = cache->slots[slot_index].resolution.matches[COMMAND_INDEX];
    if (matches == nullptr || matches->empty()) continue;
    ++contributing;
    match_count += matches->size();
  }
  if (contributing > 1u && match_count > MAX_CALLED) return false;

  // The bound pipelines were read through this handle: the callbacks' GetBoundShaderPipeline reuses it.
  context->bound_state_cache = state_handle;
  struct PendingPostResult {
    CallbackResult<Context> result = {};
    const void* callback_data = nullptr;
    uint32_t matched_shader_hash = 0u;
    std::optional<reshade::api::pipeline_stage> matched_shader_stage = std::nullopt;
    std::optional<renodx::utils::shader::StageState> matched_stage_state = std::nullopt;
  };
  static thread_local std::vector<PendingPostResult> pending_post_results;
  size_t pending_post_result_count = 0u;
  bool ran_callback = false;
  bool should_replay = false;
  const auto run_deferred_result = [&](const PendingPostResult& pending, bool finalize) {
    const auto callback = (finalize ? pending.result.finalize_callback : pending.result.post_callback);
    if (callback == nullptr) return;
    context->callback_data = pending.callback_data;
    context->matched_shader_hash = pending.matched_shader_hash;
    context->matched_shader_stage = pending.matched_shader_stage;
    context->matched_stage_state = pending.matched_stage_state;
    callback(*context, pending.result.post_data);
  };
  const auto finalize_post_results = [&] {
    for (size_t i = pending_post_result_count; i != 0u; --i) {
      run_deferred_result(pending_post_results[i - 1u], true);
    }
  };
  // One registration as RunCallbacks' run_registration_list runs it; true: bypass (finalized, the command skipped).
  const auto run_registration = [&](const command_action::internal::CallbackRegistration& registration) {
    ran_callback = true;
    CallbackResult<Context> result = {};
    registration.invoke(context, registration.callback_data, &result);
    should_replay |= result.replay || result.post_callback != nullptr || result.finalize_callback != nullptr;
    if (result.post_callback != nullptr || result.finalize_callback != nullptr) {
      PendingPostResult pending = {
          .result = result,
          .callback_data = registration.callback_data,
          .matched_shader_hash = context->matched_shader_hash,
          .matched_shader_stage = context->matched_shader_stage,
          .matched_stage_state = context->matched_stage_state,
      };
      if (pending_post_result_count == pending_post_results.size()) {
        pending_post_results.push_back(std::move(pending));
      } else {
        pending_post_results[pending_post_result_count] = std::move(pending);
      }
      ++pending_post_result_count;
    }
    if (result.bypass) {
      finalize_post_results();
      return true;
    }
    return false;
  };

  // In registration order: the bits from the lowest index up.
  for (uint64_t acting = acting_globals; acting != 0u; acting &= acting - 1u) {
    if (run_registration((*globals.registrations)[std::countr_zero(acting)])) {
      *skip = true;
      return true;
    }
  }
  // The callback identities called so far when two pipelines contribute; only the first called_count entries are read
  // (left uninitialized: zeroing it would cost every command with callbacks).
  struct Identity {
    const void* callback;
    const void* callback_data;
    bool operator==(const Identity&) const = default;
  };
  std::array<Identity, MAX_CALLED> called;
  size_t called_count = 0u;
  for (size_t slot_index = FIRST_SLOT; slot_index < LAST_SLOT; ++slot_index) {
    const Slot& slot = cache->slots[slot_index];
    const auto* matches = slot.resolution.matches[COMMAND_INDEX];
    if (matches == nullptr) continue;
    const auto stage = SLOT_STAGES[slot_index];
    const auto stage_index = renodx::utils::pipeline::GetShaderStageIndex(stage);
    for (const auto& match : *matches) {
      if (contributing > 1u) {
        const Identity identity = {.callback = match.callback.callback, .callback_data = match.callback.callback_data};
        const auto called_span = std::span(called.data(), called_count);
        if (std::ranges::find(called_span, identity) != called_span.end()) continue;
        called[called_count++] = identity;
      }
      context->matched_shader_hash = match.shader_hash_by_stage[stage_index];
      context->matched_shader_stage = stage;
      context->matched_stage_state = renodx::utils::shader::StageState{
          .stage = stage,
          .applied_stage = slot.applied_stages,
          .pipeline = slot.pipeline,
          .pipeline_details = match.pipeline_details,
      };
      const bool bypass = run_registration(match.callback);
      context->matched_shader_hash = 0u;
      context->matched_shader_stage = std::nullopt;
      context->matched_stage_state = std::nullopt;
      if (bypass) {
        *skip = true;
        return true;
      }
    }
  }

  if (!ran_callback || !should_replay) {
    *skip = false;
    return true;
  }
  if constexpr (std::is_same_v<Arguments, DrawArguments>) {
    cmd_list->draw(context->arguments.vertex_count, context->arguments.instance_count, context->arguments.first_vertex,
                   context->arguments.first_instance);
  } else if constexpr (std::is_same_v<Arguments, DrawIndexedArguments>) {
    cmd_list->draw_indexed(context->arguments.index_count, context->arguments.instance_count, context->arguments.first_index,
                           context->arguments.vertex_offset, context->arguments.first_instance);
  } else {
    cmd_list->dispatch(context->arguments.group_count_x, context->arguments.group_count_y, context->arguments.group_count_z);
  }
  for (size_t i = 0u; i < pending_post_result_count; ++i) {
    run_deferred_result(pending_post_results[i], false);
  }
  finalize_post_results();
  *skip = true;
  return true;
}

}  // namespace internal

// Gates a command-wide callback of this module on the fast dispatch (RunCallbacks ignores gates). Pair with ClearGate
// before the module unloads.
template <typename Callback>
inline void SetGate(const Callback& callback, const Gate& gate) {
  (void)callback;
  internal::shared.RegisterModule();
  auto* data = internal::shared.data;
  if (data == nullptr) return;
  const void* const callback_identity = &command_action::internal::CALLBACK_IDENTITY<std::remove_cvref_t<Callback>>;
  {
    const std::unique_lock lock(data->mutex);
    const auto gates = std::span(data->gates.data(), data->gate_count);
    size_t index = std::ranges::find(gates, callback_identity, &internal::GateEntry::callback) - gates.begin();
    if (index == data->gate_count) {
      // Out of room the callback stays ungated: called on every command, as RunCallbacks does.
      assert(data->gate_count < internal::MAX_GATES);
      if (data->gate_count == internal::MAX_GATES) return;
      ++data->gate_count;
    }
    data->gates[index] = {.callback = callback_identity, .gate = gate};
  }
  internal::BumpEpoch();
}

template <typename Callback>
inline void ClearGate(const Callback& callback) {
  (void)callback;
  auto* data = internal::shared.data;
  if (data == nullptr) return;
  const void* const callback_identity = &command_action::internal::CALLBACK_IDENTITY<std::remove_cvref_t<Callback>>;
  {
    const std::unique_lock lock(data->mutex);
    const auto gates = std::span(data->gates.data(), data->gate_count);
    const auto existing = std::ranges::find(gates, callback_identity, &internal::GateEntry::callback);
    if (existing == gates.end()) return;
    *existing = gates.back();
    --data->gate_count;
  }
  internal::BumpEpoch();
}

// The module's opt-in: after command_action is attached (mods::shader::Use or command_action::Use), and its detach
// before command_action's.
inline void Use(DWORD fdw_reason) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH: {
      if (command_action::internal::fast_run_callbacks<DrawArguments> != nullptr) return;
      // command_action's control block (attached before this), read once; the slot's view closes again, the block stays.
      auto slot = platform::OpenProcessSharedSlot<cross_addon::internal::ControlBlock<command_action::internal::SharedData>>(
          __uuidof(command_action::internal::SharedData), [](auto*&) {});
      if (slot.value != nullptr) {
        internal::action_control = *slot.value;
      }
      if (slot.view != nullptr) {
        ::UnmapViewOfFile(slot.view);
      }
      if (slot.handle != nullptr) {
        ::CloseHandle(slot.handle);
      }
      if (internal::action_control == nullptr) return;
      internal::shared.RegisterModule();
      auto* data = internal::shared.data;
      if (data == nullptr) return;
      {
        const std::unique_lock lock(data->mutex);
        assert(data->module_count < internal::MAX_MODULES);
        if (data->module_count == internal::MAX_MODULES) return;
        data->modules[data->module_count++] = cross_addon::internal::GetModuleToken();
      }
      const auto token = reinterpret_cast<uintptr_t>(cross_addon::internal::GetModuleToken());
      internal::cache_guid = {0x9b, 0x41, 0x0e, 0x7c, 0x2d, 0x55, 0x4a, 0x63};
      for (size_t byte = 0u; byte < sizeof(token); ++byte) {
        internal::cache_guid[8u + byte] = static_cast<uint8_t>(token >> (8u * byte));
      }
      reshade::register_event<reshade::addon_event::destroy_command_list>(internal::OnDestroyCommandList);
      command_action::internal::fast_run_callbacks<DrawArguments> = &internal::Run<DrawArguments>;
      command_action::internal::fast_run_callbacks<DrawIndexedArguments> = &internal::Run<DrawIndexedArguments>;
      command_action::internal::fast_run_callbacks<DispatchArguments> = &internal::Run<DispatchArguments>;
      command_action::internal::on_registrations_changed = &internal::BumpEpoch;
      internal::BumpEpoch();
      reshade::log::message(reshade::log::level::info, "utils::command_action fast dispatch on.");
      break;
    }
    case DLL_PROCESS_DETACH: {
      auto* data = internal::shared.data;
      if (data == nullptr || command_action::internal::fast_run_callbacks<DrawArguments> == nullptr) return;
      command_action::internal::fast_run_callbacks<DrawArguments> = nullptr;
      command_action::internal::fast_run_callbacks<DrawIndexedArguments> = nullptr;
      command_action::internal::fast_run_callbacks<DispatchArguments> = nullptr;
      // on_registrations_changed stays: this module still changes the shared registrations after its detach (an
      // Unregister after command_action's detach, as resource_upgrade's in swapchain::Use(DETACH)), and the other
      // opted-in modules' caches must see that. BumpEpoch touches only the process-shared block, which outlives the module.
      reshade::unregister_event<reshade::addon_event::destroy_command_list>(internal::OnDestroyCommandList);
      {
        const std::unique_lock lock(internal::caches_mutex);
        for (const auto& [cmd_list, cache] : internal::caches) {
          cmd_list->set_private_data(internal::cache_guid.data(), 0u);
        }
        internal::caches.clear();
      }
      {
        const std::unique_lock lock(data->mutex);
        const auto modules = std::span(data->modules.data(), data->module_count);
        if (const auto mine = std::ranges::find(modules, cross_addon::internal::GetModuleToken()); mine != modules.end()) {
          *mine = modules.back();
          --data->module_count;
        }
      }
      internal::BumpEpoch();
      internal::shared.UnregisterModule();
      break;
    }
    default:
      break;
  }
}

}  // namespace renodx::utils::command_action::fast
