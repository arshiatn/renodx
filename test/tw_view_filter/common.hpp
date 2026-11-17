// What tests.cpp and partner.cpp (a second module) share: a ReShade device whose resources and views are test handles,
// the oracle (GetResourceViewClone's locked decision before the view filter, copied from 25a0e7731 + e85de8faa) and the
// random workload over resource.hpp / resource_upgrade.hpp's writers and readers.
#pragma once
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include "src/utils/resource_upgrade.hpp"

namespace view_filter_test {
namespace res = renodx::utils::resource;
namespace upgrade = renodx::utils::resource::upgrade;
using namespace reshade::api;

// D3D11 (handles are pointers: 16-byte aligned, as the filter's hash expects). Resources and views made by
// create_resource / create_resource_view (the clones) get fresh handles; get_resource_from_view answers from `views`.
struct Device : device {
  uint64_t base = 0u;
  std::atomic_uint64_t next = 0u;
  mutable std::mutex mutex;
  std::unordered_map<uint64_t, uint64_t> views;  // view -> resource

  uint64_t Fresh() { return base + 0x100000u + ((next.fetch_add(1u) + 1u) << 4u); }
  void SetView(uint64_t view, uint64_t resource) {
    const std::unique_lock lock(mutex);
    views[view] = resource;
  }
  uint64_t get_native() const override { return base; }
  device_api get_api() const override { return device_api::d3d11; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { *data = 0u; }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override {}
  bool check_capability(device_caps capability) const override { throw std::runtime_error("unexpected device call: check_capability"); }
  bool check_format_support(format format, resource_usage usage) const override { throw std::runtime_error("unexpected device call: check_format_support"); }
  bool create_sampler(const sampler_desc& desc, sampler* out_sampler) override { throw std::runtime_error("unexpected device call: create_sampler"); }
  void destroy_sampler(sampler sampler) override { throw std::runtime_error("unexpected device call: destroy_sampler"); }
  bool create_resource(const resource_desc& desc, const subresource_data* initial_data, resource_usage initial_state, resource* out_resource, void** shared_handle = nullptr) override {
    out_resource->handle = Fresh();
    return true;
  }
  void destroy_resource(resource resource) override {}
  resource_desc get_resource_desc(resource resource) const override { return resource_desc(64u, 64u, 1u, 1u, format::r8g8b8a8_unorm, 1u, memory_heap::gpu_only, resource_usage::shader_resource | resource_usage::render_target); }
  bool create_resource_view(resource resource, resource_usage usage_type, const resource_view_desc& desc, resource_view* out_view) override {
    out_view->handle = Fresh();
    SetView(out_view->handle, resource.handle);
    return true;
  }
  void destroy_resource_view(resource_view view) override {}
  resource get_resource_from_view(resource_view view) const override {
    const std::unique_lock lock(mutex);
    const auto found = views.find(view.handle);
    return {(found != views.end()) ? found->second : 0u};
  }
  resource_view_desc get_resource_view_desc(resource_view view) const override { throw std::runtime_error("unexpected device call: get_resource_view_desc"); }
  bool map_buffer_region(resource resource, uint64_t offset, uint64_t size, map_access access, void** out_data) override { throw std::runtime_error("unexpected device call: map_buffer_region"); }
  void unmap_buffer_region(resource resource) override { throw std::runtime_error("unexpected device call: unmap_buffer_region"); }
  bool map_texture_region(resource resource, uint32_t subresource, const subresource_box* box, map_access access, subresource_data* out_data) override { throw std::runtime_error("unexpected device call: map_texture_region"); }
  void unmap_texture_region(resource resource, uint32_t subresource) override { throw std::runtime_error("unexpected device call: unmap_texture_region"); }
  void update_buffer_region(const void* data, resource dest, uint64_t dest_offset, uint64_t size) override { throw std::runtime_error("unexpected device call: update_buffer_region"); }
  void update_texture_region(const subresource_data& data, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override { throw std::runtime_error("unexpected device call: update_texture_region"); }
  bool create_pipeline(pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject* subobjects, pipeline* out_pipeline) override { throw std::runtime_error("unexpected device call: create_pipeline"); }
  void destroy_pipeline(pipeline pipeline) override { throw std::runtime_error("unexpected device call: destroy_pipeline"); }
  bool create_pipeline_layout(uint32_t param_count, const pipeline_layout_param* params, pipeline_layout* out_layout) override { throw std::runtime_error("unexpected device call: create_pipeline_layout"); }
  void destroy_pipeline_layout(pipeline_layout layout) override { throw std::runtime_error("unexpected device call: destroy_pipeline_layout"); }
  bool allocate_descriptor_tables(uint32_t count, pipeline_layout layout, uint32_t param, descriptor_table* out_tables) override { throw std::runtime_error("unexpected device call: allocate_descriptor_tables"); }
  void free_descriptor_tables(uint32_t count, const descriptor_table* tables) override { throw std::runtime_error("unexpected device call: free_descriptor_tables"); }
  void get_descriptor_heap_offset(descriptor_table table, uint32_t binding, uint32_t array_offset, descriptor_heap* out_heap, uint32_t* out_offset) const override { throw std::runtime_error("unexpected device call: get_descriptor_heap_offset"); }
  void copy_descriptor_tables(uint32_t count, const descriptor_table_copy* copies) override { throw std::runtime_error("unexpected device call: copy_descriptor_tables"); }
  void update_descriptor_tables(uint32_t count, const descriptor_table_update* updates) override { throw std::runtime_error("unexpected device call: update_descriptor_tables"); }
  bool create_query_heap(query_type type, uint32_t count, query_heap* out_heap) override { throw std::runtime_error("unexpected device call: create_query_heap"); }
  void destroy_query_heap(query_heap heap) override { throw std::runtime_error("unexpected device call: destroy_query_heap"); }
  bool get_query_heap_results(query_heap heap, uint32_t first, uint32_t count, void* results, uint32_t stride) override { throw std::runtime_error("unexpected device call: get_query_heap_results"); }
  void set_resource_name(resource resource, const char* name) override { throw std::runtime_error("unexpected device call: set_resource_name"); }
  void set_resource_view_name(resource_view view, const char* name) override { throw std::runtime_error("unexpected device call: set_resource_view_name"); }
  bool create_fence(uint64_t initial_value, fence_flags flags, fence* out_fence, void** shared_handle = nullptr) override { throw std::runtime_error("unexpected device call: create_fence"); }
  void destroy_fence(fence fence) override { throw std::runtime_error("unexpected device call: destroy_fence"); }
  uint64_t get_completed_fence_value(fence fence) const override { throw std::runtime_error("unexpected device call: get_completed_fence_value"); }
  bool wait(fence fence, uint64_t value, uint64_t timeout = UINT64_MAX) override { throw std::runtime_error("unexpected device call: wait"); }
  bool signal(fence fence, uint64_t value) override { throw std::runtime_error("unexpected device call: signal"); }
  bool get_property(device_properties property, void* data) const override { throw std::runtime_error("unexpected device call: get_property"); }
  uint64_t get_resource_view_gpu_address(resource_view view) const override { throw std::runtime_error("unexpected device call: get_resource_view_gpu_address"); }
  void get_acceleration_structure_size(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input* inputs, uint64_t* out_size, uint64_t* out_build_scratch_size, uint64_t* out_update_scratch_size) const override { throw std::runtime_error("unexpected device call: get_acceleration_structure_size"); }
  bool get_pipeline_shader_group_handles(pipeline pipeline, uint32_t first, uint32_t count, void* out_handles) override { throw std::runtime_error("unexpected device call: get_pipeline_shader_group_handles"); }
};

// GetResourceViewClone's decision from a locked read, as before the filter: the clone it returns, or "create".
struct Expected {
  uint64_t clone = 0u;
  bool create = false;
};

inline Expected Oracle(uint64_t handle, const upgrade::ResourceViewCloneOptions& options) {
  Expected expected;
  if (handle == 0u) return expected;
  res::GetResourceViewInfo({handle}, [&](const res::ResourceViewInfo& info) {
    if (info.destroyed || info.is_clone) return;
    if (info.original_resource.handle == 0u) return;
    if (!info.clone_enabled && options.require_enabled && !options.activate) return;
    if (info.clone.handle != 0u) {
      expected.clone = info.clone.handle;
      return;
    }
    if (!options.allow_create) return;
    expected.create = true;
  });
  return expected;
}

// Whether this module's GetResourceViewClone may answer `handle` from the filter (the bit is clear, the filter on here).
inline bool FilterWouldAnswer(uint64_t handle, const upgrade::ResourceViewCloneOptions& options) {
  if (!options.require_enabled || options.activate || !res::view_filter_reading.load()) return false;
  const auto* filter = res::view_filter.data;
  const uint32_t index = res::ViewFilterIndex(handle);
  const size_t bit = size_t{1u} << (index % res::VIEW_FILTER_WORD_BITS);
  return (filter->control.load() & res::VIEW_FILTER_OFF) == 0u
         && (filter->enabled_bits[index / res::VIEW_FILTER_WORD_BITS].load() & bit) == 0u;
}

inline bool BitSet(uint64_t handle) {
  const auto* filter = res::view_filter.data;
  const uint32_t index = res::ViewFilterIndex(handle);
  return (filter->enabled_bits[index / res::VIEW_FILTER_WORD_BITS].load() & (size_t{1u} << (index % res::VIEW_FILTER_WORD_BITS))) != 0u;
}

constexpr std::array<upgrade::ResourceViewCloneOptions, 8> ALL_OPTIONS = {{
    {.require_enabled = true, .allow_create = false, .activate = false},
    {.require_enabled = true, .allow_create = true, .activate = false},
    {.require_enabled = false, .allow_create = false, .activate = false},
    {.require_enabled = false, .allow_create = true, .activate = false},
    {.require_enabled = true, .allow_create = false, .activate = true},
    {.require_enabled = true, .allow_create = true, .activate = true},
    {.require_enabled = false, .allow_create = false, .activate = true},
    {.require_enabled = false, .allow_create = true, .activate = true},
}};

struct Counts {
  uint64_t lookups = 0u;
  uint64_t filter_answers = 0u;  // lookups the filter could answer
  uint64_t wrong = 0u;
  uint64_t writes = 0u;
};

// One GetResourceViewClone against the oracle taken just before (single writer: nothing changes in between).
inline void Lookup(uint64_t handle, const upgrade::ResourceViewCloneOptions& options, Counts* counts) {
  const Expected expected = Oracle(handle, options);
  counts->filter_answers += FilterWouldAnswer(handle, options) ? 1u : 0u;
  const uint64_t actual = upgrade::GetResourceViewClone({handle}, options).handle;
  ++counts->lookups;
  bool good = (actual == expected.clone);
  if (expected.create) {
    // Created now (or refused: no clone target): the map then holds what it returned.
    good = (actual == 0u || Oracle(handle, {.require_enabled = false, .allow_create = false, .activate = true}).clone == actual);
  }
  if (!good) {
    if (counts->wrong < 8u) {
      std::printf("  wrong: view %llx options %d%d%d: %llx, expected %llx%s\n", static_cast<unsigned long long>(handle), options.require_enabled,
                  options.allow_create, options.activate, static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected.clone),
                  expected.create ? " (create)" : "");
    }
    ++counts->wrong;
  }
}

// A writer's handles: resources and views of one device; the extra handles get UpsertResourceViewInfo's construct path.
// The second half of each is quiet, as most of a game's textures: never cloned, never enabled (the views on quiet
// resources only), so the filter has views to answer; the first half churns through every write path.
struct Workload {
  Device* device = nullptr;
  std::array<uint64_t, 32> resources = {};
  std::array<bool, 32> resource_live = {};
  std::array<uint64_t, 96> views = {};
  std::array<int, 96> view_resource = {};  // -1: not initialized
  std::array<uint64_t, 32> extra = {};
  std::array<res::ResourceUpgradeInfo, 2> targets = {{
      {.new_format = format::r16g16b16a16_float, .use_resource_view_cloning = true, .use_resource_view_hot_swap = true},
      {.new_format = format::r16g16b16a16_float, .use_resource_view_cloning = true, .use_resource_view_hot_swap = false},
  }};
  std::mt19937 random;

  void Init(Device* owner, uint64_t first, uint32_t seed) {
    device = owner;
    random.seed(seed);
    for (size_t index = 0u; index < resources.size(); ++index) resources[index] = first + 0x1000u + (index << 4u);
    for (size_t index = 0u; index < views.size(); ++index) views[index] = first + 0x4000u + (index << 4u);
    for (size_t index = 0u; index < extra.size(); ++index) extra[index] = first + 0x8000u + (index << 4u);
    view_resource.fill(-1);
  }

  uint64_t AnyHandle() {
    const uint32_t pick = random() % 3u;
    if (pick == 0u) return extra[random() % extra.size()];
    return views[random() % views.size()];
  }

  // One write; also through the events and upgrade paths that write the view map (W1, W2, W4, W9, W10, W11).
  void Write(Counts* counts) {
    ++counts->writes;
    const bool quiet = (random() % 2u) == 0u;
    const size_t r = (random() % (resources.size() / 2u)) + (quiet ? resources.size() / 2u : 0u);
    const size_t v = (random() % (views.size() / 2u)) + (quiet ? views.size() / 2u : 0u);
    const resource_view view = {views[v]};
    switch (random() % 11u) {
      case 0u:
        if (!resource_live[r]) {
          res::OnInitResource(device, device->get_resource_desc({0u}), nullptr, resource_usage::shader_resource, {resources[r]});
          resource_live[r] = true;
          // A quarter hot-swappable, a quarter cloned at activation, half never cloned (most game textures).
          if (const uint32_t pick = random() % 2u; !quiet) {
            auto* target = &targets[pick];
            res::UpdateResourceInfo({resources[r]}, [target](res::ResourceInfo* info) { info->clone_target = target; });
          }
        }
        break;
      case 1u:
      case 2u:
        if (resource_live[r]) {
          device->SetView(view.handle, resources[r]);
          res::OnInitResourceView(device, {resources[r]}, resource_usage::shader_resource,
                                       resource_view_desc(format::r8g8b8a8_unorm, 0u, 1u, 0u, 1u), view);
          view_resource[v] = static_cast<int>(r);
        }
        break;
      case 3u:
        if (view_resource[v] >= 0) {
          res::OnDestroyResourceView(device, view);
          view_resource[v] = -1;
        }
        break;
      case 4u:
        if (view_resource[v] >= 0) upgrade::ActivateCloneHotSwap(device, view);
        break;
      case 5u:
        if (view_resource[v] >= 0) upgrade::DeactivateCloneHotSwap(device, view);
        break;
      case 6u:
        if (resource_live[r] && random() % 4u == 0u) upgrade::GetResourceClone({resources[r]}, {.allow_create = (random() % 2u) == 0u, .activate = true});
        break;
      case 7u:
        if (resource_live[r] && random() % 4u == 0u) {
          res::OnDestroyResource(device, {resources[r]});
          resource_live[r] = false;
        }
        break;
      case 8u: {
        const uint32_t bits = random();
        res::UpdateResourceViewInfo(view, [bits, quiet](res::ResourceViewInfo* info) {
          info->clone_enabled = !quiet && (bits & 15u) == 0u;
          if ((bits & 0x70u) == 0u) info->is_clone = !info->is_clone;
          if ((bits & 0x700u) == 0u) info->original_resource = {0u};
        });
        break;
      }
      case 9u: {
        const uint64_t handle = extra[(random() % (extra.size() / 2u)) + (quiet ? extra.size() / 2u : 0u)];
        const uint64_t original = resources[r];
        const uint32_t bits = random();
        res::UpsertResourceViewInfo({handle}, [&](res::ResourceViewInfo* info, bool inserted) {
          info->original_resource = {original};
          info->clone_enabled = !quiet && (bits & 15u) == 0u;
          info->destroyed = (bits & 0x30u) == 0u;
        });
        break;
      }
      default:
        if (view_resource[v] >= 0) upgrade::CloneResourceView(view);
        break;
    }
  }
};

// The threaded part: roles 0 / 1 write (each its own Workload), 2..4 look up any handle of both, 5 starts and ends short
// lived lookup threads. `pause` holds the writers for the barrier checks (`paused` counts the held ones).
struct StressControl {
  std::atomic_bool stop = false;
  std::atomic_bool pause = false;
  std::atomic_int paused = 0;
  std::array<Workload*, 2> writers = {};
  std::atomic_uint64_t lookups = 0u;
  std::atomic_uint64_t writes = 0u;
  std::atomic_uint64_t short_threads = 0u;
};

inline void Stress(StressControl* control, int role) {
  std::mt19937 random(7919u * static_cast<uint32_t>(role + 1));
  Counts counts;
  const auto look_up = [&] {
    Workload* workload = control->writers[random() % 2u];
    const uint64_t handle = (random() % 3u == 0u) ? workload->extra[random() % workload->extra.size()] : workload->views[random() % workload->views.size()];
    upgrade::GetResourceViewClone({handle}, {.require_enabled = true, .allow_create = false, .activate = false});
    control->lookups.fetch_add(1u, std::memory_order_relaxed);
  };
  while (!control->stop.load()) {
    if (role < 2) {
      if (control->pause.load()) {
        control->paused.fetch_add(1);
        while (control->pause.load() && !control->stop.load()) ::Sleep(0);
        control->paused.fetch_sub(1);
        continue;
      }
      control->writers[role]->Write(&counts);
      control->writes.fetch_add(1u, std::memory_order_relaxed);
    } else if (role < 5) {
      look_up();
    } else {
      std::thread([&] {
        for (int index = 0; index < 2000; ++index) look_up();
      }).join();
      control->short_threads.fetch_add(1u);
    }
  }
}

}  // namespace view_filter_test
