/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <include/reshade.hpp>
#include <include/reshade_api_device.hpp>
#include <include/reshade_api_pipeline.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "./bitwise.hpp"
#include "./cross_addon.hpp"
#include "./cstring.hpp"
#include "./hash.hpp"
#include "./log.hpp"
#include "./pipeline_layout.hpp"
#include "./platform.hpp"

namespace renodx::utils::pipeline {

static bool track_shader_hashes = false;
static bool cache_subobjects = false;
static bool retain_destroyed_subobjects = false;
// The process's binds may reuse their locked lookup's outcome (BindCacheData below). Set before this module's first Use.
// RENODX_PIPELINE_MEMO=0 turns the cache off in the process, =1 turns it on in every module that attaches.
static bool use_bind_cache = false;

enum class PipelineBindPoint : uint8_t {
  UNKNOWN,
  GRAPHICS,
  COMPUTE,
  RAY_TRACING,
};

struct PipelineShaderHashEntry {
  uint32_t subobject_index = 0u;
  uint32_t shader_hash = 0u;
  reshade::api::pipeline_stage stage = static_cast<reshade::api::pipeline_stage>(0u);
};

static constexpr std::array SHADER_STAGES = {
    reshade::api::pipeline_stage::vertex_shader,
    reshade::api::pipeline_stage::hull_shader,
    reshade::api::pipeline_stage::domain_shader,
    reshade::api::pipeline_stage::geometry_shader,
    reshade::api::pipeline_stage::pixel_shader,
    reshade::api::pipeline_stage::compute_shader,
    reshade::api::pipeline_stage::amplification_shader,
    reshade::api::pipeline_stage::mesh_shader,
};

static constexpr size_t GetShaderStageIndex(reshade::api::pipeline_stage stage) {
  return std::ranges::find(SHADER_STAGES, stage) - SHADER_STAGES.begin();
}

struct PipelineShaderDetails {
  reshade::api::pipeline pipeline = {0u};
  reshade::api::device* device = nullptr;
  [[deprecated("Use utils::pipeline::PipelineInfo::layout")]]
  reshade::api::pipeline_layout layout = {0};
  cross_addon::vector<reshade::api::pipeline_subobject> subobjects;
  cross_addon::vector<PipelineShaderHashEntry> subobject_shaders;
  reshade::api::pipeline replacement_pipeline = {0};
  reshade::api::pipeline_stage replacement_stages = static_cast<reshade::api::pipeline_stage>(0u);
  bool initialized_replacement = false;
  std::optional<cross_addon::string> tag;
  bool destroyed = false;
  bool is_replacement = false;
  reshade::api::pipeline_layout replacement_layout = {0u};
  reshade::api::pipeline_layout injection_layout = {0u};
  int32_t injection_index = -1;
  int32_t injection_register_index = -1;
  int32_t injection_constant_buffer_offset = 0;
  reshade::api::shader_stage injection_visibility = reshade::api::shader_stage::all;
  cross_addon::vector<reshade::api::descriptor_table> descriptor_tables;
  cross_addon::unordered_map<
      pipeline_layout::DescriptorBindingKey,
      pipeline_layout::DescriptorPushLocation,
      pipeline_layout::DescriptorBindingKeyHash>
      descriptor_push_locations;
  uint64_t replacement_revision = 0u;

  PipelineShaderDetails() = default;
  PipelineShaderDetails(
      reshade::api::pipeline pipeline,
      reshade::api::device* device,
      const reshade::api::pipeline_layout& layout,
      const reshade::api::pipeline_subobject* subobjects,
      const uint32_t& subobject_count,
      std::span<const PipelineShaderHashEntry> shader_details,
      std::span<const uint32_t> replaced_shader_subobject_indexes);
};

struct PipelineInfo {
  reshade::api::pipeline pipeline = {0u};
  reshade::api::device* device = nullptr;
  reshade::api::pipeline_layout layout = {0u};
  PipelineBindPoint bind_point = PipelineBindPoint::UNKNOWN;
  cross_addon::vector<PipelineShaderHashEntry> shader_details;
  cross_addon::vector<uint32_t> replaced_shader_subobject_indexes;
  PipelineShaderDetails details;
  uint64_t generation = 0u;
  std::array<std::optional<size_t>, SHADER_STAGES.size()> shader_detail_by_stage = {};
  bool shader_detail_index_ready = false;
};

using InitCallback = void (*)(
    PipelineInfo*,
    uint32_t,
    const reshade::api::pipeline_subobject*);
using CreateCallback = bool (*)(
    reshade::api::device*,
    reshade::api::pipeline_layout,
    uint32_t,
    const reshade::api::pipeline_subobject*);
using BindCallback = void (*)(
    reshade::api::command_list*,
    reshade::api::pipeline_stage,
    reshade::api::pipeline,
    PipelineBindPoint);
// A disengaged result requests an unlocked retry with info == nullptr.
// A zero handle means the pipeline is resolved without a replacement.
using BindInfoCallback = std::optional<reshade::api::pipeline> (*)(
    reshade::api::command_list*,
    reshade::api::pipeline_stage,
    reshade::api::pipeline,
    PipelineBindPoint,
    const PipelineInfo*);
using DestroyCallback = void (*)(const PipelineInfo&);
using AfterDestroyCallback = void (*)();

template <typename Callback>
using CallbackList = cross_addon::vector<Callback>;

using PipelineInfoMap = cross_addon::parallel_node_hash_map<
    uint64_t,  // reshade::api::pipeline::handle
    PipelineInfo,
    std::shared_mutex>;
namespace internal {
struct __declspec(uuid("a92dff63-bb92-4543-80aa-3948b089f964")) SharedData {
  PipelineInfoMap pipeline_infos;
  CallbackList<InitCallback> on_init_callbacks;
  CallbackList<BindCallback> on_bind_callbacks;
  CallbackList<DestroyCallback> on_destroy_callbacks;
  CallbackList<CreateCallback> on_create_callbacks;
  CallbackList<AfterDestroyCallback> on_after_destroy_callbacks;
  bool track_shader_hashes = false;
  bool retain_destroyed_subobjects = false;
  std::atomic_uint64_t next_pipeline_generation = 0u;
  CallbackList<BindInfoCallback> on_bind_pipeline_info_callbacks;
  bool cache_subobjects = false;
};

static cross_addon::Shared<SharedData> shared;
}  // namespace internal

// The bind cache. OnBindPipeline's locked lookup (GetPipelineInfo, the bind-info callbacks inside the map lock) gives an
// outcome: found, bind point, replacement. A thread keeps the outcomes of its binds by handle and stages, tagged with
// `generation`, which every write to pipeline_infos bumps inside its exclusive submap lock (InvalidateBindCache), as does
// every change to what the bind-info callbacks read besides the info (a module's attach or detach, the callback list). A
// tag equal to the current generation proves no write since the outcome was taken: the bind uses it without the lock.
// Never the info itself: the callbacks read fields written after creation (the replacement). Every module attached to the
// pipeline map must write with this protocol (its token in BindCacheData::modules), else the cache turns off
// (CheckBindCacheWriters): an add-on built before it writes without bumping.
// Own uuid, frozen (SharedData is append-only ABI, made by whichever module comes first): any change to these fields or to
// the protocol takes a new uuid. Native-width words: one plain load on x86 and x64. x86's generation wraps at 2^32 writes:
// a false hit needs exactly 2^32 pipeline writes between an outcome and its next bind.
constexpr size_t BIND_CACHE_MAX_MODULES = 8u;
constexpr size_t BIND_CACHE_WORD_BITS = sizeof(size_t) * 8u;
constexpr size_t BIND_CACHE_OFF = size_t{1u} << (BIND_CACHE_WORD_BITS - 1u);  // sticky
constexpr size_t BIND_CACHE_USE = size_t{1u} << (BIND_CACHE_WORD_BITS - 2u);
constexpr size_t BIND_CACHE_CHECK = size_t{1u} << (BIND_CACHE_WORD_BITS - 3u);  // RENODX_PIPELINE_MEMO_CHECK=1
constexpr uint32_t BIND_CACHE_INDEX_BITS = 10u;                                // 1,024 entries a thread
// The self-check compares the hits of the table indices of this frame's phase (advanced at an opting-in module's present):
// every index once in BIND_CACHE_SELF_CHECK_FRAMES frames.
constexpr uint32_t BIND_CACHE_SELF_CHECK_FRAMES = 256u;
static_assert(std::atomic_size_t::is_always_lock_free);

// USE is set while at least one checking module (an opted-in one: it runs the writer check and advances the self-check
// phase at its present) is attached: the last one's detach clears it, so no module answers from its tables unchecked.
struct __declspec(uuid("42143cab-fd3f-43e7-9bc8-0f94b99f7db2")) BindCacheData {
  std::shared_mutex mutex;  // modules, module_count, checking_modules; USE changes under it
  std::array<cross_addon::internal::ModuleToken, BIND_CACHE_MAX_MODULES> modules = {};
  size_t module_count = 0u;
  std::atomic_size_t control = 0u;
  std::atomic_size_t generation = 0u;
  std::atomic_size_t self_check_phase = 0u;
  size_t checking_modules = 0u;
};

// A bind's locked outcome: pipeline and stages are the key, generation the tag.
struct BindCacheEntry {
  uint64_t pipeline = 0u;
  uint64_t replacement = 0u;
  size_t generation = 0u;
  reshade::api::pipeline_stage stages = static_cast<reshade::api::pipeline_stage>(0u);
  PipelineBindPoint bind_point = PipelineBindPoint::UNKNOWN;
  bool found = false;
};
using BindCacheTable = std::array<BindCacheEntry, size_t{1u} << BIND_CACHE_INDEX_BITS>;

namespace internal {
static cross_addon::Shared<BindCacheData> bind_cache;
// Per thread, not per command list: an outcome depends on the handle, the stages and process state only, so a thread's own
// table needs no synchronization and no lookup to find it (D3D11 deferred contexts and D3D12 lists record on their own
// threads: a table each). Made at the thread's first cached bind, freed at its exit.
static thread_local std::unique_ptr<BindCacheTable> bind_cache_table;
// pipeline::internal::shared's control block: the modules attached to the pipeline map (CheckBindCacheWriters).
static const cross_addon::internal::ControlBlock<SharedData>* bind_cache_writers = nullptr;
static bool bind_cache_token = false;     // this module's token is in BindCacheData::modules
static bool bind_cache_checking = false;  // this module opted in: it checks the writers at init_device and present
// RENODX_PIPELINE_MEMO_CHECK=1: the handler module's counts (plain loads and stores: a check mode for tests and runs).
static std::atomic_uint32_t bind_cache_hits = 0u;
static std::atomic_uint32_t bind_cache_misses = 0u;
static std::atomic_uint32_t bind_cache_races = 0u;
static std::atomic_uint32_t bind_cache_mismatches = 0u;
}  // namespace internal

// After every write to pipeline_infos (inside its exclusive lock) and every change to what the bind-info callbacks read
// besides the info: no outcome cached before it hits again.
inline void InvalidateBindCache() {
  if (auto* cache = internal::bind_cache.data; cache != nullptr) {
    cache->generation.fetch_add(1u);
  }
}

// Sticky for the process. Logged only once a module uses the cache (other mods stay quiet).
inline void TurnOffBindCache(std::string_view reason) {
  auto* cache = internal::bind_cache.data;
  if (cache == nullptr || (cache->control.load() & BIND_CACHE_OFF) != 0u) return;
  const size_t control = cache->control.fetch_or(BIND_CACHE_OFF);
  if ((control & BIND_CACHE_OFF) != 0u || (control & BIND_CACHE_USE) == 0u) return;
  log::w("utils::pipeline bind cache off: ", reason, " (every bind takes the pipeline map's lock again).");
}

// This module's token before its pipeline-map record turns active: every registration with internal::shared calls this
// first, so a module whose record is active always has its token. A full table leaves it out: off for all.
static void RegisterBindCacheToken() {
  if (internal::bind_cache_token) return;
  internal::bind_cache.RegisterModule();
  auto* cache = internal::bind_cache.data;
  if (cache == nullptr) return;
  internal::bind_cache_token = true;
  const auto token = cross_addon::internal::GetModuleToken();
  bool table_full = false;
  {
    const std::unique_lock lock(cache->mutex);
    const auto modules = std::span(cache->modules.data(), cache->module_count);
    if (std::ranges::find(modules, token) == modules.end()) {
      table_full = (cache->module_count == BIND_CACHE_MAX_MODULES);
      if (!table_full) {
        cache->modules[cache->module_count++] = token;
      }
    }
  }
  if (table_full) {
    TurnOffBindCache("all module slots taken");
  }
}

static bool attached = false;
// RegisterEvent needs this module's interest; the shared callback list also contains other modules' callbacks.
static std::vector<CreateCallback> local_create_callbacks;
static std::vector<BindCallback> local_bind_callbacks;
static std::vector<BindInfoCallback> local_bind_info_callbacks;

static void EnableShaderHashTracking() {
  track_shader_hashes = true;
  if (internal::shared.data != nullptr) {
    internal::shared.data->track_shader_hashes = true;
  }
}

static void EnableSubobjectCaching() {
  cache_subobjects = true;
  if (internal::shared.data != nullptr) {
    internal::shared.data->cache_subobjects = true;
  }
}

static void EnableDestroyedSubobjectRetention() {
  retain_destroyed_subobjects = true;
  if (internal::shared.data != nullptr) {
    internal::shared.data->retain_destroyed_subobjects = true;
  }
}

template <typename F>
static bool UpdatePipelineInfo(reshade::api::pipeline pipeline, F&& callback) {
  if (internal::shared.data == nullptr || pipeline.handle == 0u) return false;
  bool updated = false;
  internal::shared.data->pipeline_infos.modify_if(pipeline.handle, [&](auto& entry) {
    std::invoke(std::forward<F>(callback), entry.second);
    InvalidateBindCache();
    updated = true;
  });
  return updated;
}

struct PendingOriginalShaderHashes {
  struct Shader {
    uint32_t subobject_index;
    uint32_t original_hash;
    const void* code;
    size_t code_size;
  };
  reshade::api::device* device;
  const reshade::api::pipeline_subobject* subobjects;
  uint32_t subobject_count;
  std::vector<Shader> shaders;
};

static thread_local std::optional<PendingOriginalShaderHashes> pending_original_shader_hashes;

template <typename F>
static bool GetPipelineInfo(reshade::api::pipeline pipeline, F&& callback) {
  if (pipeline.handle == 0u) return false;
  bool found = false;
  internal::shared.data->pipeline_infos.if_contains(
      pipeline.handle,
      [&](const std::pair<const uint64_t, PipelineInfo>& pair) {
        if (!pair.second.details.destroyed) {
          std::invoke(std::forward<F>(callback), pair.second);
          found = true;
        }
      });
  return found;
}

template <typename F>
static bool GetPipelineShaderDetails(reshade::api::pipeline pipeline, F&& callback) {
  if (pipeline.handle == 0u) return false;
  bool found = false;
  internal::shared.data->pipeline_infos.if_contains(pipeline.handle, [&](const auto& entry) {
    std::invoke(std::forward<F>(callback), entry.second.details);
    found = true;
  });
  return found;
}

template <typename F>
static bool UpdatePipelineShaderDetails(reshade::api::pipeline pipeline, F&& callback) {
  if (pipeline.handle == 0u) return false;
  bool updated = false;
  internal::shared.data->pipeline_infos.modify_if(pipeline.handle, [&](auto& entry) {
    std::invoke(std::forward<F>(callback), entry.second.details);
    InvalidateBindCache();
    updated = true;
  });
  return updated;
}

static uint32_t GetPipelineShaderHash(
    reshade::api::pipeline pipeline,
    reshade::api::pipeline_stage stage) {
  const auto index = GetShaderStageIndex(stage);
  if (index >= SHADER_STAGES.size()) return 0u;
  uint32_t shader_hash = 0u;
  GetPipelineInfo(pipeline, [&](const PipelineInfo& info) {
    if (info.shader_detail_index_ready) {
      if (const auto& detail_index = info.shader_detail_by_stage[index]; detail_index.has_value()) {
        shader_hash = info.shader_details[*detail_index].shader_hash;
      }
    } else {
      for (const auto& detail : info.shader_details) {
        if (detail.stage == stage) {
          shader_hash = detail.shader_hash;
        }
      }
    }
  });
  return shader_hash;
}

template <typename T>
static void ReplacePipelineSubobjectData(
    reshade::api::pipeline_subobject* subobject,
    std::span<const T> values) {
  assert(subobject != nullptr);
  if (subobject == nullptr) return;

  auto* replacement = cross_addon::Allocate(sizeof(T) * values.size());
  std::memcpy(replacement, values.data(), sizeof(T) * values.size());
  cross_addon::Free(subobject->data);
  subobject->count = static_cast<uint32_t>(values.size());
  subobject->data = replacement;
}

static reshade::api::pipeline_subobject* ClonePipelineSubObjects(const reshade::api::pipeline_subobject* subobjects, uint32_t subobject_count) {
  auto* new_subobjects = new reshade::api::pipeline_subobject[subobject_count];
  std::memcpy(new_subobjects, subobjects, sizeof(reshade::api::pipeline_subobject) * subobject_count);
  for (uint32_t i = 0; i < subobject_count; ++i) {
    const auto& subobject = subobjects[i];
#ifdef DEBUG_LEVEL_2
    {
      std::stringstream s;
      s << "utils::pipeline::ClonePipelineSubObjects(cloning " << subobjects[i].type << "[" << i << "]";
      reshade::log::message(reshade::log::level::debug, s.str().c_str());
    }
#endif
    switch (subobject.type) {
      case reshade::api::pipeline_subobject_type::vertex_shader:
      case reshade::api::pipeline_subobject_type::hull_shader:
      case reshade::api::pipeline_subobject_type::domain_shader:
      case reshade::api::pipeline_subobject_type::geometry_shader:
      case reshade::api::pipeline_subobject_type::compute_shader:
      case reshade::api::pipeline_subobject_type::pixel_shader:
      case reshade::api::pipeline_subobject_type::amplification_shader:
      case reshade::api::pipeline_subobject_type::mesh_shader:
      case reshade::api::pipeline_subobject_type::raygen_shader:
      case reshade::api::pipeline_subobject_type::any_hit_shader:
      case reshade::api::pipeline_subobject_type::closest_hit_shader:
      case reshade::api::pipeline_subobject_type::miss_shader:
      case reshade::api::pipeline_subobject_type::intersection_shader:
      case reshade::api::pipeline_subobject_type::callable_shader:      {
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::shader_desc));
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::shader_desc));
        auto* old_desc = static_cast<reshade::api::shader_desc*>(subobject.data);
        auto* new_desc = static_cast<reshade::api::shader_desc*>(new_subobjects[i].data);

        if (new_desc->entry_point != nullptr && new_desc->entry_point[0] == '\0') {
          // Reshade guards against nullptr entry points but not empty strings, so we convert as a workaround
          new_desc->entry_point = nullptr;
        } else if (new_desc->entry_point != nullptr) {
          new_desc->entry_point = renodx::utils::CloneCString(new_desc->entry_point);
        }

#ifdef DEBUG_LEVEL_2
        {
          std::stringstream s;
          s << "utils::pipeline::ClonePipelineSubObjects(entry point";
          s << " from " << (old_desc->entry_point != nullptr ? old_desc->entry_point : "(null)");
          s << " to " << (new_desc->entry_point != nullptr ? new_desc->entry_point : "(null)");
          s << ")";
          reshade::log::message(reshade::log::level::debug, s.str().c_str());
        }
#endif

        if (old_desc->code_size != 0u) {
          void* code_copy = malloc(old_desc->code_size);
          memcpy(code_copy, old_desc->code, old_desc->code_size);
          new_desc->code = code_copy;
        }

#ifdef DEBUG_LEVEL_1
        std::stringstream s;
        s << "utils::pipeline::ClonePipelineSubObjects(cloning ";
        s << subobject.type;
        s << " with " << PRINT_CRC32(renodx::utils::hash::ComputeCRC32(static_cast<const uint8_t*>(old_desc->code), old_desc->code_size));
        s << " => " << PRINT_CRC32(renodx::utils::hash::ComputeCRC32(static_cast<const uint8_t*>(new_desc->code), new_desc->code_size));
        s << " (" << old_desc->code_size << " bytes)";
        s << " from " << old_desc->code;
        s << " to " << new_desc->code;
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
#endif
        break;
      }
      case reshade::api::pipeline_subobject_type::input_layout:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::input_element) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::input_element) * subobject.count);
        for (uint32_t j = 0; j < subobject.count; j++) {
          auto* old_input_elements = static_cast<reshade::api::input_element*>(subobject.data);
          auto* new_input_elements = static_cast<reshade::api::input_element*>(new_subobjects[i].data);
          new_input_elements[j].semantic = renodx::utils::CloneCString(old_input_elements[j].semantic);
        }
        break;
      case reshade::api::pipeline_subobject_type::stream_output_state:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::stream_output_desc) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::stream_output_desc) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::blend_state:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::blend_desc) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::blend_desc) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::rasterizer_state:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::rasterizer_desc) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::rasterizer_desc) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::depth_stencil_state:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::depth_stencil_desc) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::depth_stencil_desc) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::primitive_topology:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::primitive_topology) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::primitive_topology) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::depth_stencil_format:
      case reshade::api::pipeline_subobject_type::render_target_formats:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::format) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::format) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::sample_mask:
      case reshade::api::pipeline_subobject_type::sample_count:
      case reshade::api::pipeline_subobject_type::viewport_count:
      case reshade::api::pipeline_subobject_type::max_vertex_count:
      case reshade::api::pipeline_subobject_type::max_payload_size:
      case reshade::api::pipeline_subobject_type::max_attribute_size:
      case reshade::api::pipeline_subobject_type::max_recursion_depth:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(uint32_t) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(uint32_t) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::dynamic_pipeline_states:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::dynamic_state) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::dynamic_state) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::libraries:
      case reshade::api::pipeline_subobject_type::shader_groups:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::shader_group) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::shader_group) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::flags:
        new_subobjects[i].data = cross_addon::Allocate(sizeof(reshade::api::pipeline_flags) * subobject.count);
        memcpy(new_subobjects[i].data, subobject.data, sizeof(reshade::api::pipeline_flags) * subobject.count);
        break;
      case reshade::api::pipeline_subobject_type::unknown:
        break;
    }
  }

  return new_subobjects;
}

static void DestroyPipelineSubobjects(std::span<reshade::api::pipeline_subobject> subobjects) {
  for (auto& subobject : subobjects) {
    if (subobject.data == nullptr) continue;
    switch (subobject.type) {
      case reshade::api::pipeline_subobject_type::vertex_shader:
      case reshade::api::pipeline_subobject_type::hull_shader:
      case reshade::api::pipeline_subobject_type::domain_shader:
      case reshade::api::pipeline_subobject_type::geometry_shader:
      case reshade::api::pipeline_subobject_type::compute_shader:
      case reshade::api::pipeline_subobject_type::pixel_shader:
      case reshade::api::pipeline_subobject_type::amplification_shader:
      case reshade::api::pipeline_subobject_type::mesh_shader:
      case reshade::api::pipeline_subobject_type::raygen_shader:
      case reshade::api::pipeline_subobject_type::any_hit_shader:
      case reshade::api::pipeline_subobject_type::closest_hit_shader:
      case reshade::api::pipeline_subobject_type::miss_shader:
      case reshade::api::pipeline_subobject_type::intersection_shader:
      case reshade::api::pipeline_subobject_type::callable_shader:      {
        auto* desc = static_cast<reshade::api::shader_desc*>(subobject.data);
        if (desc->entry_point != nullptr) {
          std::free(const_cast<char*>(desc->entry_point));
          desc->entry_point = nullptr;
        }
        free(const_cast<void*>(desc->code));
        desc->code = nullptr;
        break;
      }
      case reshade::api::pipeline_subobject_type::input_layout: {
        auto* input_elements = static_cast<reshade::api::input_element*>(subobject.data);
        for (uint32_t i = 0; i < subobject.count; ++i) {
          std::free(const_cast<char*>(input_elements[i].semantic));
          input_elements[i].semantic = nullptr;
        }
        break;
      }
      default:
        break;
    }

    cross_addon::Free(subobject.data);
    subobject.data = nullptr;
  }
}

static void DestroyPipelineSubobjects(reshade::api::pipeline_subobject* subobjects, uint32_t subobject_count) {
  DestroyPipelineSubobjects({subobjects, subobjects + subobject_count});
  delete[] subobjects;
}

static bool HasSDRAlphaBlend(std::span<const reshade::api::pipeline_subobject> subobjects) {
  for (const auto& subobject : subobjects) {
    if (subobject.type != reshade::api::pipeline_subobject_type::blend_state) continue;
    for (uint32_t j = 0; j < subobject.count; ++j) {
      auto& desc = static_cast<reshade::api::blend_desc*>(subobject.data)[j];
      if (!desc.blend_enable[0]) continue;

      if (((desc.render_target_write_mask[0] & 0x1) != 0)
          || ((desc.render_target_write_mask[0] & 0x2) != 0)
          || ((desc.render_target_write_mask[0] & 0x4) != 0)) {
        if (desc.color_blend_op[0] != reshade::api::blend_op::min
            && desc.color_blend_op[0] != reshade::api::blend_op::max) {
          if (
              desc.source_color_blend_factor[0] == reshade::api::blend_factor::dest_alpha
              || desc.source_color_blend_factor[0] == reshade::api::blend_factor::one_minus_dest_alpha
              || desc.dest_color_blend_factor[0] == reshade::api::blend_factor::dest_alpha
              || desc.dest_color_blend_factor[0] == reshade::api::blend_factor::one_minus_dest_alpha) {
            return true;
          }
        }
      }
      if ((desc.render_target_write_mask[0] & 0x8) != 0) {
        if (desc.alpha_blend_op[0] == reshade::api::blend_op::min
            || desc.alpha_blend_op[0] == reshade::api::blend_op::max) {
          return true;
        }
        if (
            desc.source_alpha_blend_factor[0] == reshade::api::blend_factor::dest_alpha
            || desc.source_alpha_blend_factor[0] == reshade::api::blend_factor::one_minus_dest_alpha
            || desc.dest_alpha_blend_factor[0] == reshade::api::blend_factor::one
            || desc.dest_alpha_blend_factor[0] == reshade::api::blend_factor::dest_alpha
            || desc.dest_alpha_blend_factor[0] == reshade::api::blend_factor::one_minus_dest_alpha) {
          return true;
        }
      }
    }
  }
  return false;
}

struct PipelineSubobjects {
  std::span<const uint8_t> vertex_shader;
  std::span<const uint8_t> pixel_shader;
  std::span<const uint8_t> compute_shader;
  std::vector<reshade::api::blend_desc> blend_states = {reshade::api::blend_desc{}};
  std::vector<reshade::api::input_element> input_layout;
  std::vector<reshade::api::depth_stencil_desc> depth_stencil_states = {reshade::api::depth_stencil_desc{.depth_enable = false}};
  std::vector<uint32_t> max_vertex_counts = {3};
  std::vector<reshade::api::primitive_topology> primitive_topologies = {reshade::api::primitive_topology::triangle_list};
  std::vector<reshade::api::rasterizer_desc> rasterizer_states = {reshade::api::rasterizer_desc{.cull_mode = reshade::api::cull_mode::none}};
  std::vector<reshade::api::format> render_target_formats = {reshade::api::format::r16g16b16a16_float};
};

static reshade::api::pipeline CreateRenderPipeline(
    reshade::api::device* device,
    const reshade::api::pipeline_layout layout,
    PipelineSubobjects& options) {
  std::vector<reshade::api::pipeline_subobject> subobjects = {
      {
          .type = reshade::api::pipeline_subobject_type::blend_state,
          .count = static_cast<uint32_t>(options.blend_states.size()),
          .data = options.blend_states.data(),
      },
      {
          .type = reshade::api::pipeline_subobject_type::depth_stencil_state,
          .count = static_cast<uint32_t>(options.depth_stencil_states.size()),
          .data = options.depth_stencil_states.data(),
      },
      {
          .type = reshade::api::pipeline_subobject_type::input_layout,
          .count = static_cast<uint32_t>(options.input_layout.size()),
          .data = options.input_layout.data(),
      },
      {
          .type = reshade::api::pipeline_subobject_type::max_vertex_count,
          .count = static_cast<uint32_t>(options.max_vertex_counts.size()),
          .data = options.max_vertex_counts.data(),
      },
      {
          .type = reshade::api::pipeline_subobject_type::primitive_topology,
          .count = static_cast<uint32_t>(options.primitive_topologies.size()),
          .data = options.primitive_topologies.data(),
      },
      {
          .type = reshade::api::pipeline_subobject_type::rasterizer_state,
          .count = static_cast<uint32_t>(options.rasterizer_states.size()),
          .data = options.rasterizer_states.data(),
      },

      {
          .type = reshade::api::pipeline_subobject_type::render_target_formats,
          .count = static_cast<uint32_t>(options.render_target_formats.size()),
          .data = options.render_target_formats.data(),
      },
  };

  std::vector<reshade::api::shader_desc> shader_descriptions = {};
  shader_descriptions.reserve(
      (options.vertex_shader.empty() ? 0 : 1)
      + (options.pixel_shader.empty() ? 0 : 1)
      + (options.compute_shader.empty() ? 0 : 1));

  if (!options.vertex_shader.empty()) {
    shader_descriptions.push_back({
        .code = options.vertex_shader.data(),
        .code_size = options.vertex_shader.size(),
    });
    subobjects.push_back({
        .type = reshade::api::pipeline_subobject_type::vertex_shader,
        .count = 1,
        .data = &shader_descriptions.back(),
    });
  }
  if (!options.pixel_shader.empty()) {
    shader_descriptions.push_back({
        .code = options.pixel_shader.data(),
        .code_size = options.pixel_shader.size(),
    });
    subobjects.push_back({
        .type = reshade::api::pipeline_subobject_type::pixel_shader,
        .count = 1,
        .data = &shader_descriptions.back(),
    });
  }
  if (!options.compute_shader.empty()) {
    shader_descriptions.push_back({
        .code = options.compute_shader.data(),
        .code_size = options.compute_shader.size(),
    });
    subobjects.push_back({
        .type = reshade::api::pipeline_subobject_type::compute_shader,
        .count = 1,
        .data = &shader_descriptions.back(),
    });
  }

  reshade::api::pipeline pipeline;
  if (device->create_pipeline(layout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &pipeline)) {
    return pipeline;
  }
  return {0};
}

static reshade::api::pipeline CreateRenderPipeline(
    reshade::api::device* device,
    const reshade::api::pipeline_layout layout,
    const std::unordered_map<reshade::api::pipeline_subobject_type, std::span<const std::uint8_t>>& shaders,
    const reshade::api::format render_target_format = reshade::api::format::r16g16b16a16_float) {
  auto format = render_target_format;
  uint32_t num_vertices = 3;
  auto topology = reshade::api::primitive_topology::triangle_list;
  reshade::api::blend_desc blend_state = {};
  reshade::api::rasterizer_desc rasterizer_state = {.cull_mode = reshade::api::cull_mode::none};
  reshade::api::depth_stencil_desc depth_stencil_state = {.depth_enable = false};
  std::vector<reshade::api::input_element> input_layout;

  std::vector<reshade::api::pipeline_subobject> subobjects = {
      {reshade::api::pipeline_subobject_type::render_target_formats, 1, &format},
      {reshade::api::pipeline_subobject_type::max_vertex_count, 1, &num_vertices},
      {reshade::api::pipeline_subobject_type::primitive_topology, 1, &topology},
      {reshade::api::pipeline_subobject_type::blend_state, 1, &blend_state},
      {reshade::api::pipeline_subobject_type::rasterizer_state, 1, &rasterizer_state},
      {reshade::api::pipeline_subobject_type::depth_stencil_state, 1, &depth_stencil_state},
      {reshade::api::pipeline_subobject_type::input_layout, static_cast<uint32_t>(input_layout.size()), input_layout.data()},
  };

  subobjects.reserve(6 + shaders.size());

  std::vector<reshade::api::shader_desc> shader_descriptions;
  shader_descriptions.reserve(shaders.size());
  for (const auto& [type, shader] : shaders) {
    shader_descriptions.push_back({
        .code = shader.data(),
        .code_size = shader.size(),
    });
    subobjects.push_back({.type = type, .count = 1, .data = &shader_descriptions.back()});
  }

  reshade::api::pipeline pipeline;
  if (device->create_pipeline(layout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &pipeline)) {
    return pipeline;
  }
  return {0};
}

static bool OnCreatePipeline(
    reshade::api::device* device,
    reshade::api::pipeline_layout layout,
    uint32_t subobject_count,
    const reshade::api::pipeline_subobject* subobjects) {
  pending_original_shader_hashes.reset();  // A failed create does not produce an init callback.
  std::vector<std::optional<uint32_t>> original_hashes(subobject_count);
  if (internal::shared.data->track_shader_hashes) {
    for (uint32_t index = 0u; index < subobject_count; ++index) {
      switch (subobjects[index].type) {
        case reshade::api::pipeline_subobject_type::vertex_shader:
        case reshade::api::pipeline_subobject_type::hull_shader:
        case reshade::api::pipeline_subobject_type::domain_shader:
        case reshade::api::pipeline_subobject_type::geometry_shader:
        case reshade::api::pipeline_subobject_type::pixel_shader:
        case reshade::api::pipeline_subobject_type::compute_shader:
        case reshade::api::pipeline_subobject_type::amplification_shader:
        case reshade::api::pipeline_subobject_type::mesh_shader:
          break;
        default: continue;
      }
      const auto* shader = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
      if (shader == nullptr || shader->code == nullptr || shader->code_size == 0u) continue;
      original_hashes[index] = hash::ComputeCRC32(static_cast<const uint8_t*>(shader->code), shader->code_size);
    }
  }
  bool modified = false;
  for (const auto callback : internal::shared.data->on_create_callbacks) {
    if (callback != nullptr && callback(device, layout, subobject_count, subobjects)) {
      modified = true;
    }
  }
  if (modified && internal::shared.data->track_shader_hashes) {
    PendingOriginalShaderHashes pending{.device = device, .subobjects = subobjects, .subobject_count = subobject_count};
    for (uint32_t index = 0u; index < subobject_count; ++index) {
      if (!original_hashes[index].has_value()) continue;
      const auto* shader = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
      if (shader == nullptr || shader->code == nullptr || shader->code_size == 0u) continue;
      pending.shaders.push_back({index, *original_hashes[index], shader->code, shader->code_size});
    }
    pending_original_shader_hashes = std::move(pending);
  }
  return modified;
}

static void OnInitPipeline(
    reshade::api::device* device,
    reshade::api::pipeline_layout layout,
    uint32_t subobject_count,
    const reshade::api::pipeline_subobject* subobjects,
    reshade::api::pipeline pipeline) {
  std::optional<PendingOriginalShaderHashes> original_shader_hashes = std::move(pending_original_shader_hashes);
  pending_original_shader_hashes.reset();
  PipelineBindPoint bind_point = PipelineBindPoint::UNKNOWN;
  for (uint32_t index = 0u; index < subobject_count; ++index) {
    PipelineBindPoint subobject_bind_point = PipelineBindPoint::UNKNOWN;
    switch (subobjects[index].type) {
      case reshade::api::pipeline_subobject_type::compute_shader:
        subobject_bind_point = PipelineBindPoint::COMPUTE;
        break;
      case reshade::api::pipeline_subobject_type::raygen_shader:
      case reshade::api::pipeline_subobject_type::any_hit_shader:
      case reshade::api::pipeline_subobject_type::closest_hit_shader:
      case reshade::api::pipeline_subobject_type::miss_shader:
      case reshade::api::pipeline_subobject_type::intersection_shader:
      case reshade::api::pipeline_subobject_type::callable_shader:
      case reshade::api::pipeline_subobject_type::libraries:
      case reshade::api::pipeline_subobject_type::shader_groups:
        subobject_bind_point = PipelineBindPoint::RAY_TRACING;
        break;
      case reshade::api::pipeline_subobject_type::vertex_shader:
      case reshade::api::pipeline_subobject_type::hull_shader:
      case reshade::api::pipeline_subobject_type::domain_shader:
      case reshade::api::pipeline_subobject_type::geometry_shader:
      case reshade::api::pipeline_subobject_type::pixel_shader:
      case reshade::api::pipeline_subobject_type::amplification_shader:
      case reshade::api::pipeline_subobject_type::mesh_shader:
      case reshade::api::pipeline_subobject_type::input_layout:
      case reshade::api::pipeline_subobject_type::stream_output_state:
      case reshade::api::pipeline_subobject_type::blend_state:
      case reshade::api::pipeline_subobject_type::rasterizer_state:
      case reshade::api::pipeline_subobject_type::depth_stencil_state:
      case reshade::api::pipeline_subobject_type::primitive_topology:
      case reshade::api::pipeline_subobject_type::depth_stencil_format:
      case reshade::api::pipeline_subobject_type::render_target_formats:
      case reshade::api::pipeline_subobject_type::sample_mask:
      case reshade::api::pipeline_subobject_type::sample_count:
      case reshade::api::pipeline_subobject_type::viewport_count:
      case reshade::api::pipeline_subobject_type::max_vertex_count:
        subobject_bind_point = PipelineBindPoint::GRAPHICS;
        break;
      default:
        break;
    }
    if (subobject_bind_point == PipelineBindPoint::UNKNOWN) continue;
    if (bind_point == PipelineBindPoint::UNKNOWN) {
      bind_point = subobject_bind_point;
    } else if (bind_point != subobject_bind_point) {
      bind_point = PipelineBindPoint::UNKNOWN;
      break;
    }
  }
  PipelineInfo info{
      .pipeline = pipeline,
      .device = device,
      .layout = layout,
      .bind_point = bind_point,
  };
  info.generation = internal::shared.data->next_pipeline_generation.fetch_add(1u) + 1u;
  if (internal::shared.data->track_shader_hashes) {
    for (uint32_t index = 0u; index < subobject_count; ++index) {
      const auto& subobject = subobjects[index];
      reshade::api::pipeline_stage stage;
      switch (subobject.type) {
        case reshade::api::pipeline_subobject_type::vertex_shader:        stage = reshade::api::pipeline_stage::vertex_shader; break;
        case reshade::api::pipeline_subobject_type::hull_shader:          stage = reshade::api::pipeline_stage::hull_shader; break;
        case reshade::api::pipeline_subobject_type::domain_shader:        stage = reshade::api::pipeline_stage::domain_shader; break;
        case reshade::api::pipeline_subobject_type::geometry_shader:      stage = reshade::api::pipeline_stage::geometry_shader; break;
        case reshade::api::pipeline_subobject_type::pixel_shader:         stage = reshade::api::pipeline_stage::pixel_shader; break;
        case reshade::api::pipeline_subobject_type::compute_shader:       stage = reshade::api::pipeline_stage::compute_shader; break;
        case reshade::api::pipeline_subobject_type::amplification_shader: stage = reshade::api::pipeline_stage::amplification_shader; break;
        case reshade::api::pipeline_subobject_type::mesh_shader:          stage = reshade::api::pipeline_stage::mesh_shader; break;
        default:                                                          continue;
      }
      const auto* shader = static_cast<const reshade::api::shader_desc*>(subobject.data);
      if (shader == nullptr || shader->code == nullptr || shader->code_size == 0u) continue;
      uint32_t shader_hash = hash::ComputeCRC32(static_cast<const uint8_t*>(shader->code), shader->code_size);
      if (original_shader_hashes.has_value()
          && original_shader_hashes->device == device
          && original_shader_hashes->subobjects == subobjects
          && original_shader_hashes->subobject_count == subobject_count) {
        const auto original = std::ranges::find_if(original_shader_hashes->shaders, [&](const auto& entry) {
          return entry.subobject_index == index && entry.code == shader->code && entry.code_size == shader->code_size;
        });
        if (original != original_shader_hashes->shaders.end()) {
          if (original->original_hash != shader_hash) {
            info.replaced_shader_subobject_indexes.push_back(index);
          }
          shader_hash = original->original_hash;
        }
      }
      info.shader_details.push_back({
          .subobject_index = index,
          .shader_hash = shader_hash,
          .stage = stage,
      });
    }
  }
  bool should_run_after_destroy_callbacks = false;
  const auto initialize = [&](PipelineInfo* current) {
    for (const auto callback : internal::shared.data->on_init_callbacks) {
      if (callback != nullptr) {
        callback(current, subobject_count, subobjects);
      }
    }
    current->shader_detail_by_stage.fill(std::nullopt);
    for (size_t index = 0u; index < current->shader_details.size(); ++index) {
      const auto stage_index = GetShaderStageIndex(current->shader_details[index].stage);
      if (stage_index < SHADER_STAGES.size()) {
        current->shader_detail_by_stage[stage_index] = index;
      }
    }
    current->shader_detail_index_ready = true;
  };
  internal::shared.data->pipeline_infos.lazy_emplace_l(
      pipeline.handle,
      [&](std::pair<const uint64_t, PipelineInfo>& pair) {
        if (!pair.second.details.destroyed) {
          for (const auto callback : internal::shared.data->on_destroy_callbacks) {
            if (callback != nullptr) {
              callback(pair.second);
            }
          }
          should_run_after_destroy_callbacks = true;
        }
        DestroyPipelineSubobjects(pair.second.details.subobjects);
        pair.second = std::move(info);
        initialize(&pair.second);
        InvalidateBindCache();
      },
      [&](const PipelineInfoMap::constructor& ctor) {
        const auto& entry = ctor(pipeline.handle, std::move(info));
        initialize(&const_cast<PipelineInfo&>(entry.second));
        InvalidateBindCache();
      });
  if (should_run_after_destroy_callbacks) {
    for (const auto callback : internal::shared.data->on_after_destroy_callbacks) {
      if (callback != nullptr) {
        callback();
      }
    }
  }
}

static void OnBindPipeline(
    reshade::api::command_list* cmd_list,
    reshade::api::pipeline_stage stages,
    reshade::api::pipeline pipeline) {
  PipelineBindPoint bind_point = PipelineBindPoint::UNKNOWN;
  if (stages != reshade::api::pipeline_stage::all) {
    if (renodx::utils::bitwise::HasAnyFlag(stages, reshade::api::pipeline_stage::all_ray_tracing)) {
      bind_point = PipelineBindPoint::RAY_TRACING;
    } else if (renodx::utils::bitwise::HasAnyFlag(stages, reshade::api::pipeline_stage::all_compute)) {
      bind_point = PipelineBindPoint::COMPUTE;
    } else if (renodx::utils::bitwise::HasAnyFlag(
                   stages,
                   reshade::api::pipeline_stage::all_graphics
                       | reshade::api::pipeline_stage::amplification_shader
                       | reshade::api::pipeline_stage::mesh_shader)) {
      bind_point = PipelineBindPoint::GRAPHICS;
    }
  }
  BindCacheEntry outcome = {.pipeline = pipeline.handle, .stages = stages, .bind_point = bind_point};
  bool retry_unlocked = false;
  if (stages == reshade::api::pipeline_stage::all
      || !internal::shared.data->on_bind_pipeline_info_callbacks.empty()) {
    // The locked lookup: the info's bind point and the bind-info callbacks' answers, inside the map lock. Returns whether a
    // callback asked for the unlocked retry.
    const auto look_up = [&](BindCacheEntry* result) {
      bool retry = false;
      result->found = GetPipelineInfo(pipeline, [&](const PipelineInfo& info) {
        if (stages == reshade::api::pipeline_stage::all) {
          result->bind_point = info.bind_point;
        }
        for (const auto callback : internal::shared.data->on_bind_pipeline_info_callbacks) {
          if (callback != nullptr) {
            const auto callback_replacement = callback(cmd_list, stages, pipeline, result->bind_point, &info);
            retry = retry || !callback_replacement.has_value();
            // Non-owner callbacks return zero and must not erase an owner's replacement.
            if (callback_replacement.has_value() && callback_replacement->handle != 0u) {
              result->replacement = callback_replacement->handle;
            }
          }
        }
      });
      // assert(result->found && "Pipeline bind has no live PipelineInfo.");
      return retry;
    };
    auto* cache = internal::bind_cache.data;
    const size_t control = (cache != nullptr ? cache->control.load() : 0u);
    if ((control & (BIND_CACHE_USE | BIND_CACHE_OFF)) == BIND_CACHE_USE && pipeline.handle != 0u) {
      // Fibonacci hashing of the handle (D3D11 / D3D12 objects: 16-byte aligned pointers).
      const auto folded = static_cast<uint32_t>((pipeline.handle >> 4u) ^ (pipeline.handle >> 36u));
      const uint32_t index = (folded * 0x9E3779B1u) >> (32u - BIND_CACHE_INDEX_BITS);
      if (internal::bind_cache_table == nullptr) {
        internal::bind_cache_table = std::make_unique<BindCacheTable>();
      }
      auto& entry = (*internal::bind_cache_table)[index];
      // Loaded before the lookup: a write after the lookup bumps past this tag, so the next bind misses.
      const size_t generation = cache->generation.load();
      const bool checking = (control & BIND_CACHE_CHECK) != 0u;
      const bool hit = (entry.pipeline == pipeline.handle && entry.stages == stages && entry.generation == generation);
      if (hit) {
        outcome = entry;
        // The check mode, the self-check (this frame's indices: reads the phase only) and debug builds also take the
        // lookup. A changed generation means a write between the tag's load and the lookup (a race: the cached outcome
        // was right when taken); an unchanged one a write without the protocol.
#ifdef NDEBUG
        const bool compare = checking
                             || ((index ^ cache->self_check_phase.load(std::memory_order_relaxed)) % BIND_CACHE_SELF_CHECK_FRAMES) == 0u;
#else
        const bool compare = true;
#endif
        if (compare) {
          BindCacheEntry locked = {.pipeline = pipeline.handle, .stages = stages, .bind_point = bind_point};
          const bool locked_retry = look_up(&locked);
          if (locked_retry || locked.found != entry.found || locked.bind_point != entry.bind_point
              || locked.replacement != entry.replacement) {
            const bool race = (cache->generation.load() != generation);
            if (checking) {
              auto& count = (race ? internal::bind_cache_races : internal::bind_cache_mismatches);
              const uint32_t counted = count.load(std::memory_order_relaxed) + 1u;
              count.store(counted, std::memory_order_relaxed);
              if (!race && (counted <= 16u || counted % 1024u == 0u)) {
                log::w("utils::pipeline bind cache mismatch: pipeline ", log::AsPtr(pipeline.handle), " stages ",
                       static_cast<uint32_t>(stages), " cached replacement ", log::AsPtr(entry.replacement), " found ",
                       entry.found, ", locked replacement ", log::AsPtr(locked.replacement), " found ", locked.found,
                       " retry ", locked_retry, " (", counted, " so far).");
              }
            } else if (!race) {
              TurnOffBindCache("self-check mismatch (a pipeline write without the protocol)");
            }
            outcome = locked;
            retry_unlocked = locked_retry;
          }
        }
      } else {
        retry_unlocked = look_up(&outcome);
        // A retry outcome is pending work (the replacement is being built): not kept.
        if (!retry_unlocked) {
          outcome.generation = generation;
          entry = outcome;
        }
      }
      if (checking) {
        auto& count = (hit ? internal::bind_cache_hits : internal::bind_cache_misses);
        count.store(count.load(std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
        const uint32_t hits = internal::bind_cache_hits.load(std::memory_order_relaxed);
        const uint32_t misses = internal::bind_cache_misses.load(std::memory_order_relaxed);
        if ((hits + misses) % 100000u == 0u) {
          log::i("utils::pipeline bind cache check: ", hits + misses, " binds, ", hits, " hits, ", misses, " misses, ",
                 internal::bind_cache_mismatches.load(std::memory_order_relaxed), " mismatches, ",
                 internal::bind_cache_races.load(std::memory_order_relaxed), " races.");
        }
      }
    } else {
      retry_unlocked = look_up(&outcome);
    }
  }
  for (const auto callback : internal::shared.data->on_bind_callbacks) {
    if (callback != nullptr) {
      callback(cmd_list, stages, pipeline, outcome.bind_point);
    }
  }
  if (outcome.found && retry_unlocked) {
    for (const auto callback : internal::shared.data->on_bind_pipeline_info_callbacks) {
      if (callback != nullptr) {
        const auto callback_replacement = callback(cmd_list, stages, pipeline, outcome.bind_point, nullptr);
        if (callback_replacement.has_value() && callback_replacement->handle != 0u) {
          outcome.replacement = callback_replacement->handle;
        }
      }
    }
  }
  if (outcome.replacement != 0u) {
    cmd_list->bind_pipeline(stages, {outcome.replacement});
  }
}

static void OnDestroyPipeline(
    reshade::api::device* device,
    reshade::api::pipeline pipeline) {
  (void)device;
  std::optional<PipelineInfo> destroyed_info;
  internal::shared.data->pipeline_infos.modify_if(
      pipeline.handle,
      [&](auto& entry) {
        if (entry.second.details.destroyed) return;
        const auto& info = entry.second;
        destroyed_info.emplace(PipelineInfo{
            .pipeline = info.pipeline,
            .device = info.device,
            .layout = info.layout,
            .bind_point = info.bind_point,
            .shader_details = info.shader_details,
            .replaced_shader_subobject_indexes = info.replaced_shader_subobject_indexes,
            .generation = info.generation,
            .shader_detail_by_stage = info.shader_detail_by_stage,
            .shader_detail_index_ready = info.shader_detail_index_ready,
        });
        destroyed_info->details.replacement_pipeline = info.details.replacement_pipeline;
        entry.second.details.replacement_pipeline = {0u};
        entry.second.details.destroyed = true;
        ++entry.second.details.replacement_revision;
        InvalidateBindCache();
      });
  if (!destroyed_info.has_value()) return;
  for (const auto callback : internal::shared.data->on_destroy_callbacks) {
    if (callback != nullptr) {
      callback(*destroyed_info);
    }
  }
  bool retain_cache = false;
  internal::shared.data->pipeline_infos.modify_if(pipeline.handle, [&](auto& entry) {
    auto& details = entry.second.details;
    entry.second.shader_details.clear();
    entry.second.shader_detail_by_stage.fill(std::nullopt);
    entry.second.shader_detail_index_ready = false;
    entry.second.replaced_shader_subobject_indexes.clear();
    retain_cache = internal::shared.data->retain_destroyed_subobjects && !details.subobjects.empty();
    if (!retain_cache) {
      DestroyPipelineSubobjects(details.subobjects);
    }
    InvalidateBindCache();
  });
  if (!retain_cache) {
    internal::shared.data->pipeline_infos.erase_if(pipeline.handle, [](auto&) {
      InvalidateBindCache();
      return true;
    });
  }
  for (const auto callback : internal::shared.data->on_after_destroy_callbacks) {
    if (callback != nullptr) {
      callback();
    }
  }
}

static void OnDestroyDevice(reshade::api::device* device) {
  std::vector<uint64_t> pipeline_handles;
  internal::shared.data->pipeline_infos.for_each_m([&](auto& entry) {
    auto& [pipeline_handle, info] = entry;
    if (info.device != device) return;
    pipeline_handles.push_back(pipeline_handle);
    DestroyPipelineSubobjects(info.details.subobjects);
    InvalidateBindCache();
  });
  for (const auto pipeline_handle : pipeline_handles) {
    internal::shared.data->pipeline_infos.erase_if(pipeline_handle, [](auto&) {
      InvalidateBindCache();
      return true;
    });
  }
}

// Every module attached to the pipeline map (internal::shared's active module records) must write with the bind cache's
// protocol (its token in BindCacheData::modules); else the cache turns off, naming the module's file. Run at an
// opting-in module's attach, at init_device (add-ons that attached after it, before any pipeline exists) and at every
// present (a late add-on: unchecked for at most one frame).
static void CheckBindCacheWriters() {
  auto* cache = internal::bind_cache.data;
  if (cache == nullptr || internal::bind_cache_writers == nullptr || (cache->control.load() & BIND_CACHE_OFF) != 0u) return;
  cross_addon::internal::ModuleToken missing = nullptr;
  {
    const std::shared_lock lock(cache->mutex);
    const auto modules = std::span(cache->modules.data(), cache->module_count);
    // Records are never freed: the walk is memory-safe while modules attach.
    for (const auto* module = internal::bind_cache_writers->modules; module != nullptr && missing == nullptr; module = module->next) {
      if (module->active && std::ranges::find(modules, module->token) == modules.end()) {
        missing = module->token;
      }
    }
  }
  if (missing == nullptr) return;
  std::array<char, MAX_PATH> file = {'?'};
  if (HMODULE module = nullptr; ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                       static_cast<const char*>(missing), &module)) {
    ::GetModuleFileNameA(module, file.data(), static_cast<DWORD>(file.size()));
  }
  TurnOffBindCache(std::string("a module writes the pipeline map without its protocol: ") + file.data());
}

static void OnBindCacheInitDevice(reshade::api::device* device) {
  CheckBindCacheWriters();
}

static void OnBindCachePresent(
    reshade::api::command_queue* queue,
    reshade::api::swapchain* swapchain,
    const reshade::api::rect* source_rect,
    const reshade::api::rect* dest_rect,
    uint32_t dirty_rect_count,
    const reshade::api::rect* dirty_rects) {
  if (auto* cache = internal::bind_cache.data; cache != nullptr) {
    cache->self_check_phase.fetch_add(1u, std::memory_order_relaxed);
  }
  CheckBindCacheWriters();
}

static void RegisterOnInitCallback(InitCallback callback) {
  RegisterBindCacheToken();
  internal::shared.RegisterCallback(&internal::SharedData::on_init_callbacks, callback);
}

static void RegisterOnCreateCallback(CreateCallback callback, bool active = true) {
  RegisterBindCacheToken();
  internal::shared.RegisterCallback(&internal::SharedData::on_create_callbacks, callback, active);
  if (active) {
    if (std::find(local_create_callbacks.begin(), local_create_callbacks.end(), callback) == local_create_callbacks.end()) {
      local_create_callbacks.push_back(callback);
    }
  } else {
    std::erase(local_create_callbacks, callback);
  }
  internal::shared.RegisterEvent<reshade::addon_event::create_pipeline>(OnCreatePipeline, !local_create_callbacks.empty());
}

static void RegisterOnBindCallback(BindCallback callback, bool active = true) {
  RegisterBindCacheToken();
  internal::shared.RegisterCallback(&internal::SharedData::on_bind_callbacks, callback, active);
  if (active) {
    if (std::find(local_bind_callbacks.begin(), local_bind_callbacks.end(), callback) == local_bind_callbacks.end()) {
      local_bind_callbacks.push_back(callback);
    }
  } else {
    std::erase(local_bind_callbacks, callback);
  }
  internal::shared.RegisterEvent<reshade::addon_event::bind_pipeline>(
      OnBindPipeline, !local_bind_callbacks.empty() || !local_bind_info_callbacks.empty());
}

// A bind-info callback's answer for a live pipeline may depend only on its arguments other than the command list, on the
// info, and on state that changes only around InvalidateBindCache calls (shader::Use's module registration); when it
// returns a value it has no side effects. The bind cache reuses the answers until the next InvalidateBindCache.
static void RegisterOnBindInfoCallback(BindInfoCallback callback) {
  RegisterBindCacheToken();
  internal::shared.RegisterCallback(&internal::SharedData::on_bind_pipeline_info_callbacks, callback);
  InvalidateBindCache();
  if (std::find(local_bind_info_callbacks.begin(), local_bind_info_callbacks.end(), callback) == local_bind_info_callbacks.end()) {
    local_bind_info_callbacks.push_back(callback);
  }
  internal::shared.RegisterEvent<reshade::addon_event::bind_pipeline>(OnBindPipeline, true);
}

static void RegisterOnDestroyCallback(DestroyCallback callback) {
  RegisterBindCacheToken();
  internal::shared.RegisterCallback(&internal::SharedData::on_destroy_callbacks, callback);
}

static void RegisterOnAfterDestroyCallback(AfterDestroyCallback callback) {
  RegisterBindCacheToken();
  internal::shared.RegisterCallback(&internal::SharedData::on_after_destroy_callbacks, callback);
}

static void UnregisterOnInitCallback(InitCallback callback) {
  internal::shared.UnregisterCallback(&internal::SharedData::on_init_callbacks, callback);
}

static void UnregisterOnCreateCallback(CreateCallback callback) {
  internal::shared.UnregisterCallback(&internal::SharedData::on_create_callbacks, callback);
  std::erase(local_create_callbacks, callback);
}

static void UnregisterOnBindCallback(BindCallback callback) {
  internal::shared.UnregisterCallback(&internal::SharedData::on_bind_callbacks, callback);
  std::erase(local_bind_callbacks, callback);
}

static void UnregisterOnBindInfoCallback(BindInfoCallback callback) {
  internal::shared.UnregisterCallback(&internal::SharedData::on_bind_pipeline_info_callbacks, callback);
  InvalidateBindCache();
  std::erase(local_bind_info_callbacks, callback);
}

static void UnregisterOnDestroyCallback(DestroyCallback callback) {
  internal::shared.UnregisterCallback(&internal::SharedData::on_destroy_callbacks, callback);
}

static void UnregisterOnAfterDestroyCallback(AfterDestroyCallback callback) {
  internal::shared.UnregisterCallback(&internal::SharedData::on_after_destroy_callbacks, callback);
}

static void Use(DWORD fdw_reason) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH: {
      if (attached) {
        if (track_shader_hashes) {
          internal::shared.data->track_shader_hashes = true;
        }
        if (cache_subobjects) {
          internal::shared.data->cache_subobjects = true;
        }
        if (retain_destroyed_subobjects) {
          internal::shared.data->retain_destroyed_subobjects = true;
        }
        return;
      }
      attached = true;
      RegisterBindCacheToken();
      if (internal::shared.RegisterModule([](internal::SharedData& data) {
            data.track_shader_hashes = data.track_shader_hashes || track_shader_hashes;
            data.retain_destroyed_subobjects = data.retain_destroyed_subobjects || retain_destroyed_subobjects;
            data.cache_subobjects = data.cache_subobjects || cache_subobjects;
          })) {
        reshade::log::message(reshade::log::level::info, "PipelineUtil attached.");
      }
      internal::shared.RegisterEvent<reshade::addon_event::init_pipeline>(OnInitPipeline);
      internal::shared.RegisterEvent<reshade::addon_event::create_pipeline>(OnCreatePipeline, !local_create_callbacks.empty());
      internal::shared.RegisterEvent<reshade::addon_event::bind_pipeline>(
          OnBindPipeline, !local_bind_callbacks.empty() || !local_bind_info_callbacks.empty());
      internal::shared.RegisterEvent<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);
      internal::shared.RegisterEvent<reshade::addon_event::destroy_device>(OnDestroyDevice);
      // The module set changed: which module handles the events (the bind-info callbacks' answers depend on it).
      InvalidateBindCache();

      auto* cache = internal::bind_cache.data;
      if (cache == nullptr) break;
      // '0', '1' or 0 (unset).
      const auto environment_switch = [](const char* name) {
        std::array<char, 2> value = {};
        return (::GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size())) == 1u) ? value[0] : '\0';
      };
      const char memo = environment_switch("RENODX_PIPELINE_MEMO");
      if (memo == '0') {
        // The A/B base: said as information (check_logs.py fails a run on the cache's warnings).
        TurnOffBindCache("RENODX_PIPELINE_MEMO=0");
        if (use_bind_cache) {
          reshade::log::message(reshade::log::level::info, "utils::pipeline bind cache not used (RENODX_PIPELINE_MEMO=0).");
        }
        break;
      }
      if (!use_bind_cache && memo != '1') break;

      if (internal::bind_cache_writers == nullptr) {
        // The slot's view closes again; the control block stays for the process.
        auto slot = platform::OpenProcessSharedSlot<cross_addon::internal::ControlBlock<internal::SharedData>>(
            __uuidof(internal::SharedData), [](auto*&) {});
        if (slot.value != nullptr) {
          internal::bind_cache_writers = *slot.value;
        }
        if (slot.view != nullptr) {
          ::UnmapViewOfFile(slot.view);
        }
        if (slot.handle != nullptr) {
          ::CloseHandle(slot.handle);
        }
      }
      if (environment_switch("RENODX_PIPELINE_MEMO_CHECK") == '1') {
        cache->control.fetch_or(BIND_CACHE_CHECK);
      }
      {
        const std::unique_lock lock(cache->mutex);
        ++cache->checking_modules;
        cache->control.fetch_or(BIND_CACHE_USE);
      }
      reshade::register_event<reshade::addon_event::init_device>(OnBindCacheInitDevice);
      reshade::register_event<reshade::addon_event::present>(OnBindCachePresent);
      internal::bind_cache_checking = true;
      CheckBindCacheWriters();
      if (internal::bind_cache_writers == nullptr) {
        TurnOffBindCache("the pipeline map's module records not found");
      }
      if ((cache->control.load() & BIND_CACHE_OFF) != 0u) {
        reshade::log::message(reshade::log::level::warning,
                              "utils::pipeline bind cache off at attach (a pointer-form access, a module without its protocol "
                              "or all module slots taken, logged before): every bind takes the pipeline map's lock.");
        break;
      }
      reshade::log::message(reshade::log::level::info, ((cache->control.load() & BIND_CACHE_CHECK) != 0u)
                                                            ? "utils::pipeline bind cache on, every hit checked (RENODX_PIPELINE_MEMO_CHECK=1)."
                                                            : "utils::pipeline bind cache on.");
      break;
    }
    case DLL_PROCESS_DETACH:
      if (!attached) return;
      attached = false;
      if (internal::bind_cache_checking) {
        reshade::unregister_event<reshade::addon_event::init_device>(OnBindCacheInitDevice);
        reshade::unregister_event<reshade::addon_event::present>(OnBindCachePresent);
        internal::bind_cache_checking = false;
        // The last checking module: the binds take the lock again until a module opts in (its attach sets USE and checks
        // the writers at once; the writes meanwhile bumped as ever, so the threads' tables stay exact).
        auto* cache = internal::bind_cache.data;  // set before the checking flag
        const std::unique_lock lock(cache->mutex);
        if (--cache->checking_modules == 0u) {
          cache->control.fetch_and(~BIND_CACHE_USE);
        }
      }
      internal::shared.UnregisterEvent<reshade::addon_event::init_pipeline>(OnInitPipeline);
      internal::shared.UnregisterEvent<reshade::addon_event::create_pipeline>(OnCreatePipeline);
      internal::shared.UnregisterEvent<reshade::addon_event::bind_pipeline>(OnBindPipeline);
      internal::shared.UnregisterEvent<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);
      internal::shared.UnregisterEvent<reshade::addon_event::destroy_device>(OnDestroyDevice);
      if (internal::shared.UnregisterModule()) {
        InvalidateBindCache();  // the module set changed (see the attach)
      }
      // The record is inactive now (or never was: RegisterModule failed at attach); the token goes after it (the attach's
      // order reversed), whatever UnregisterModule returned, as the attach added it whatever RegisterModule returned. The
      // generation and control stay: the module's later writes still bump.
      if (internal::bind_cache_token) {
        auto* cache = internal::bind_cache.data;  // set before the token
        {
          const std::unique_lock lock(cache->mutex);
          const auto modules = std::span(cache->modules.data(), cache->module_count);
          if (const auto mine = std::ranges::find(modules, cross_addon::internal::GetModuleToken()); mine != modules.end()) {
            *mine = modules.back();
            --cache->module_count;
          }
        }
        internal::bind_cache.UnregisterModule();
        internal::bind_cache_token = false;
      }
      local_create_callbacks.clear();
      local_bind_callbacks.clear();
      local_bind_info_callbacks.clear();
      break;
  }
}
}  // namespace renodx::utils::pipeline
