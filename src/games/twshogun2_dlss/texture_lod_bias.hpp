/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include <include/reshade.hpp>

#include "../../utils/data.hpp"
#include "./render_scale.hpp"

// Texture LOD bias below the display size (as rysesonofrome_dlss): the game picks texture levels for the render size,
// so its textures got blurrier with the render scale (ATN: grass and plants use lower textures). Its anisotropic
// samplers (material textures; post passes filter linear or point, shadow maps compare) are bound as clones with
// MipLODBias + the addon's bias (SetBias: log2(render / display width), minus 1 or 2 for sharper; 0 outside the 3D
// span). ReShade raises push_descriptors after PSSetSamplers, so the clones replace them.
// Only while the main view is bound (render_scale::MainView): the shadow maps keep the game's levels (v7-v9 biased
// them too: the trees' leaves cut sharper holes into their shadows below 100%, ATN debug9 / debug10).
// The game skips PSSetSamplers when a slot already holds the sampler (v10: many textures kept the shadow pass's
// unbiased sampler, ATN): the game's samplers are tracked per slot and bound again, as clones or as the game's, when
// the main view starts or ends (OnBindViewports, after render_scale's).
// v29: recorded only while the next frames can be biased (SetTracking at present: a render scale with the bias on),
// starting from the immediate context's current samplers; otherwise its events return before any lock.
namespace twshogun2::texture_lod_bias {

namespace internal {

constexpr uint32_t SLOTS = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;

struct BiasedSampler {
  reshade::api::sampler_desc desc;  // the game's
  reshade::api::sampler clone = {0};
  float clone_bias = 0.f;
};
static renodx::utils::data::ParallelFlatHashMap<uint64_t, BiasedSampler, std::shared_mutex> samplers;
static std::atomic<float> bias = 0.f;  // 0: none
static std::atomic<bool> tracking = false;  // the game's samplers recorded per slot
// The addon creating or destroying a clone, or binding samplers (its own events skipped; BindSlots holds
// slots.mutex).
static thread_local bool addon_sampler = false;

// The immediate context's pixel shader sampler slots: what the game bound, what is bound now (its or a clone).
struct Slots {
  std::shared_mutex mutex;
  std::array<uint64_t, SLOTS> game = {};
  std::array<uint64_t, SLOTS> bound = {};
  std::atomic<bool> biased = false;  // the last binding's state: bias and main view (read without the lock first)
};
static Slots slots;

inline bool Immediate(reshade::api::command_list* cmd_list) {
  return cmd_list->get_device()->get_api() == reshade::api::device_api::d3d11
         && reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
}

// The clone of one of the game's anisotropic samplers at a bias (created on first use), or 0 for other samplers.
inline uint64_t BiasedClone(reshade::api::device* device, uint64_t game_sampler, float bias) {
  reshade::api::sampler_desc desc = {};
  reshade::api::sampler clone = {0};
  const bool anisotropic = samplers.if_contains(game_sampler, [&](const auto& entry) {
    desc = entry.second.desc;
    if (entry.second.clone_bias == bias) {
      clone = entry.second.clone;
    }
  });
  if (!anisotropic) return 0;
  if (clone.handle != 0u) return clone.handle;
  desc.mip_lod_bias += bias;
  addon_sampler = true;
  bool created = device->create_sampler(desc, &clone);
  if (created) {
    reshade::api::sampler previous = {0};
    created = samplers.modify_if(game_sampler, [&](auto& entry) {
      previous = entry.second.clone;
      entry.second.clone = clone;
      entry.second.clone_bias = bias;
    });
    if (previous.handle != 0u) {
      device->destroy_sampler(previous);
    }
    if (!created) {
      device->destroy_sampler(clone);
    }
  }
  addon_sampler = false;
  return (created ? clone.handle : 0u);
}

// Binds slots [first, first + count) as the game's samplers or, biased, their clones (only those that changed).
// slots.mutex held.
inline void BindSlots(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, bool biased) {
  const float current_bias = bias.load(std::memory_order_relaxed);
  std::array<ID3D11SamplerState*, SLOTS> binding = {};
  bool changed = false;
  for (uint32_t slot = first; slot < first + count; ++slot) {
    uint64_t sampler = slots.game[slot];
    if (biased && sampler != 0u) {
      if (const uint64_t clone = BiasedClone(cmd_list->get_device(), sampler, current_bias); clone != 0u) {
        sampler = clone;
      }
    }
    binding[slot - first] = reinterpret_cast<ID3D11SamplerState*>(sampler);
    changed = changed || (sampler != slots.bound[slot]);
    slots.bound[slot] = sampler;
  }
  slots.biased.store(biased, std::memory_order_relaxed);
  if (changed) {
    addon_sampler = true;
    reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->PSSetSamplers(first, count, binding.data());
    addon_sampler = false;
  }
}

inline bool Biased() {
  return bias.load(std::memory_order_relaxed) != 0.f && render_scale::MainView();
}

}  // namespace internal

inline void SetBias(float bias) {
  internal::bias.store(bias, std::memory_order_relaxed);
}

inline float Bias() {
  return internal::bias.load(std::memory_order_relaxed);
}

inline size_t SamplerCount() {
  return internal::samplers.size();
}

// Present: whether the next frames can be biased. Starting, the game's samplers are the immediate context's current
// ones (it skips binding what a slot already holds); stopping, the game's are bound again if clones are.
inline void SetTracking(reshade::api::command_queue* queue, bool tracking) {
  if (tracking == internal::tracking.load(std::memory_order_relaxed)) return;
  if (queue->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  reshade::api::command_list* cmd_list = queue->get_immediate_command_list();
  const std::unique_lock lock(internal::slots.mutex);
  if (tracking) {
    std::array<ID3D11SamplerState*, internal::SLOTS> current = {};
    reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->PSGetSamplers(0, internal::SLOTS, current.data());
    for (uint32_t slot = 0; slot < internal::SLOTS; ++slot) {
      internal::slots.game[slot] = reinterpret_cast<uint64_t>(current[slot]);
      internal::slots.bound[slot] = internal::slots.game[slot];
      if (current[slot] != nullptr) {
        current[slot]->Release();
      }
    }
  } else if (internal::slots.biased.load(std::memory_order_relaxed)) {
    internal::BindSlots(cmd_list, 0, internal::SLOTS, false);
  }
  internal::tracking.store(tracking, std::memory_order_relaxed);
}

inline void OnInitSampler(reshade::api::device* device, const reshade::api::sampler_desc& desc, reshade::api::sampler sampler) {
  if (internal::addon_sampler || device->get_api() != reshade::api::device_api::d3d11) return;
  if (desc.filter != reshade::api::filter_mode::anisotropic
      && desc.filter != reshade::api::filter_mode::min_mag_anisotropic_mip_point) {
    return;
  }
  internal::samplers.insert_or_assign(sampler.handle, internal::BiasedSampler{.desc = desc});
}

inline void OnDestroySampler(reshade::api::device* device, reshade::api::sampler sampler) {
  if (internal::addon_sampler) return;
  reshade::api::sampler clone = {0};
  internal::samplers.erase_if(sampler.handle, [&](const auto& entry) {
    clone = entry.second.clone;
    return true;
  });
  if (clone.handle != 0u) {
    internal::addon_sampler = true;
    device->destroy_sampler(clone);
    internal::addon_sampler = false;
  }
  // Never bound again from the slots (a slot the game left on it holds a clone instead).
  const std::unique_lock lock(internal::slots.mutex);
  for (auto& game_sampler : internal::slots.game) {
    if (game_sampler == sampler.handle) {
      game_sampler = 0u;
    }
  }
}

// After the game's PSSetSamplers: its samplers recorded, and bound again as clones in the main view.
inline void OnPushDescriptors(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages,
                              reshade::api::pipeline_layout, uint32_t, const reshade::api::descriptor_table_update& update) {
  if (!internal::tracking.load(std::memory_order_relaxed) || internal::addon_sampler
      || stages != reshade::api::shader_stage::pixel
      || update.type != reshade::api::descriptor_type::sampler || update.count == 0
      || update.binding + update.count > internal::SLOTS || !internal::Immediate(cmd_list)) {
    return;
  }
  const auto* game_samplers = static_cast<const reshade::api::sampler*>(update.descriptors);
  const std::unique_lock lock(internal::slots.mutex);
  for (uint32_t index = 0; index < update.count; ++index) {
    internal::slots.game[update.binding + index] = game_samplers[index].handle;
    internal::slots.bound[update.binding + index] = game_samplers[index].handle;
  }
  if (const bool biased = internal::Biased(); biased) {
    internal::BindSlots(cmd_list, update.binding, update.count, biased);
  }
}

// After render_scale's: the main view started or ended, every slot bound again (clones or the game's).
inline void OnBindViewports(reshade::api::command_list* cmd_list, uint32_t, uint32_t, const reshade::api::viewport*) {
  const bool biased = internal::Biased();
  if (biased == internal::slots.biased.load(std::memory_order_relaxed) || !internal::Immediate(cmd_list)) return;
  const std::unique_lock lock(internal::slots.mutex);
  internal::BindSlots(cmd_list, 0, internal::SLOTS, biased);
}

}  // namespace twshogun2::texture_lod_bias
