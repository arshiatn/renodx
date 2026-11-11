/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/data.hpp"

// Texture LOD bias below the display size (as rysesonofrome_dlss; Shogun 2's game DLSS, the Total War anti-aliasing's
// DLSS): the game picks texture levels for the render size, so its textures got blurrier with the render scale (ATN,
// Shogun 2: grass and plants use lower textures). Its anisotropic
// samplers (material textures; post passes filter linear or point, shadow maps compare) are bound as clones with
// MipLODBias + the addon's bias (SetBias: log2(render / display width), minus 1 or 2 for sharper; 0 outside the 3D
// span). ReShade raises push_descriptors after PSSetSamplers, so the clones replace them.
// Only while the main view is bound (SetMainView: the viewport scaling's own answer; Shogun 2 render_scale::MainView,
// anti_aliasing.hpp MainView): the shadow maps keep the game's levels (Shogun 2 v7-v9 biased them too: the trees'
// leaves cut sharper holes into their shadows below 100%, ATN debug9 / debug10).
// The game skips PSSetSamplers when a slot already holds the sampler (v10: many textures kept the shadow pass's
// unbiased sampler, ATN): the game's samplers are tracked per slot and bound again, as clones or as the game's, when
// the main view starts or ends (OnBindViewports, after the viewport scaling's).
// v29: recorded only while the next frames can be biased (SetTracking at present: a render scale with the bias on),
// starting from the immediate context's current samplers; otherwise its events return on one atomic
// (tracking_devices), before any lookup or lock.
// SetWrappingTrilinear (Warhammer III's row): the game's trilinear samplers that wrap too. Its terrain, buildings, grass
// and texture blends take their textures through named samplers (s_xml_sampler, trilinear_wrap_srgb_sampler in the
// shaders' bytes) whose anisotropic filter is not proven; post passes clamp.
namespace twattila::texture_lod_bias {

namespace internal {

constexpr uint32_t SLOTS = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;

struct BiasedSampler {
  reshade::api::device* owner = nullptr;
  reshade::api::sampler_desc desc;  // the game's
  reshade::api::sampler clone = {0};
  float clone_bias = 0.f;
  bool trilinear = false;  // a wrapping trilinear sampler (biased with SetWrappingTrilinear only)
};
static renodx::utils::data::ParallelFlatHashMap<uint64_t, BiasedSampler, std::shared_mutex> samplers;
static std::atomic<float> bias = 0.f;  // 0: none
static std::atomic<bool> wrapping_trilinear = false;
static std::atomic<uint32_t> other_samplers = 0;  // the game's samplers of other filters or addresses (Report)
// Whether the main view is bound now (SetMainView; none: never).
static std::atomic<bool (*)()> main_view = nullptr;
// The addon creating or destroying a clone, or binding samplers (its own events skipped; BindSlots holds
// slots.mutex).
static thread_local bool addon_sampler = false;

// The immediate context's pixel shader sampler slots: what the game bound, what is bound now (its or a clone).
struct Slots {
  std::shared_mutex mutex;
  std::array<Microsoft::WRL::ComPtr<ID3D11SamplerState>, SLOTS> game = {};
  bool tracking = false;
  std::array<uint64_t, SLOTS> bound = {};
  std::atomic<bool> biased = false;  // the last binding's state: bias and main view (read without the lock first)
};
// Never destroyed: a game that exits with its device alive would otherwise release D3D11 samplers from this
// module's static destructors, under the loader lock. CleanupDevice empties it on every other path.
static auto& device_slots = *new renodx::utils::data::ParallelFlatHashMap<reshade::api::device*, std::shared_ptr<Slots>, std::shared_mutex>();
// Devices whose Slots are tracking: 0 is the events' and the present's early return (no lookup, no lock).
static std::atomic<uint32_t> tracking_devices = 0;

inline std::shared_ptr<Slots> SlotsFor(reshade::api::device* device, bool create = true) {
  std::shared_ptr<Slots> result;
  if (!create) {
    device_slots.if_contains(device, [&](const auto& entry) {
      const auto& [slots_device, slots] = entry;
      result = slots;
    });
    return result;
  }
  device_slots.lazy_emplace_l(
      device,
      [&](const auto& entry) {
        const auto& [slots_device, slots] = entry;
        result = slots;
      },
      [&](const auto& construct) {
        result = std::make_shared<Slots>();
        construct(device, result);
      });
  return result;
}

inline bool Immediate(reshade::api::command_list* cmd_list) {
  return cmd_list->get_device()->get_api() == reshade::api::device_api::d3d11
         && reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
}

// The clone of one of the game's anisotropic samplers at a bias (created on first use), or 0 for other samplers.
inline uint64_t BiasedClone(reshade::api::device* device, uint64_t game_sampler, float bias) {
  reshade::api::sampler_desc desc = {};
  reshade::api::sampler clone = {0};
  bool biasable = false;
  samplers.if_contains(game_sampler, [&](const auto& entry) {
    const auto& [sampler_handle, biased_sampler] = entry;
    desc = biased_sampler.desc;
    biasable = !biased_sampler.trilinear || wrapping_trilinear.load(std::memory_order_relaxed);
    if (biased_sampler.clone_bias == bias) {
      clone = biased_sampler.clone;
    }
  });
  if (!biasable) return 0;
  if (clone.handle != 0u) return clone.handle;
  desc.mip_lod_bias += bias;
  const bool was_addon_sampler = std::exchange(addon_sampler, true);
  bool created = device->create_sampler(desc, &clone);
  if (created) {
    reshade::api::sampler previous = {0};
    created = samplers.modify_if(game_sampler, [&](auto& entry) {
      auto& [sampler_handle, biased_sampler] = entry;
      previous = biased_sampler.clone;
      biased_sampler.clone = clone;
      biased_sampler.clone_bias = bias;
    });
    if (previous.handle != 0u) {
      device->destroy_sampler(previous);
    }
    if (!created) {
      device->destroy_sampler(clone);
    }
  }
  addon_sampler = was_addon_sampler;
  return (created ? clone.handle : 0u);
}

// Binds slots [first, first + count) as the game's samplers or, biased, their clones (only those that changed).
// slots.mutex held.
inline void BindSlots(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, bool biased, Slots* state) {
  auto& slots = *state;
  const float current_bias = bias.load(std::memory_order_relaxed);
  std::array<ID3D11SamplerState*, SLOTS> binding = {};
  bool changed = false;
  for (uint32_t slot = first; slot < first + count; ++slot) {
    uint64_t sampler = reinterpret_cast<uintptr_t>(slots.game[slot].Get());
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
    const bool was_addon_sampler = std::exchange(addon_sampler, true);
    reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->PSSetSamplers(first, count, binding.data());
    addon_sampler = was_addon_sampler;
  }
}

inline bool Biased() {
  bool (*is_main_view)() = main_view.load(std::memory_order_relaxed);
  return bias.load(std::memory_order_relaxed) != 0.f && is_main_view != nullptr && is_main_view();
}

}  // namespace internal

// Where the main view's bound: its viewport scaling's answer (at install, before the events).
inline void SetMainView(bool (*is_main_view)()) {
  internal::main_view.store(is_main_view, std::memory_order_relaxed);
}

inline void SetBias(float bias) {
  internal::bias.store(bias, std::memory_order_relaxed);
}

inline float Bias() {
  return internal::bias.load(std::memory_order_relaxed);
}

inline size_t SamplerCount() {
  return internal::samplers.size();
}

inline void SetWrappingTrilinear(bool biased) {
  internal::wrapping_trilinear.store(biased, std::memory_order_relaxed);
}

// Log Details' line: the game's samplers the bias can take, the others, and those with a copy at the bias now.
inline std::string Report() {
  const float now = internal::bias.load(std::memory_order_relaxed);
  uint32_t anisotropic = 0;
  uint32_t trilinear = 0;
  uint32_t biased = 0;
  internal::samplers.for_each([&](const auto& entry) {
    const auto& [game_sampler, biased_sampler] = entry;
    ++(biased_sampler.trilinear ? trilinear : anisotropic);
    if (biased_sampler.clone.handle != 0u && biased_sampler.clone_bias == now) ++biased;
  });
  return std::format("Texture LOD Bias {:.2f}: the game's samplers {} anisotropic, {} trilinear wrapping ({}), {} others; {} of them with a biased copy now.",
                     now, anisotropic, trilinear, internal::wrapping_trilinear.load(std::memory_order_relaxed) ? "biased" : "as the game's",
                     internal::other_samplers.load(std::memory_order_relaxed), biased);
}

// Present: whether the next frames can be biased. Starting, the game's samplers are the immediate context's current
// ones (it skips binding what a slot already holds); stopping, the game's are bound again if clones are.
inline void SetTracking(reshade::api::command_queue* queue, bool tracking) {
  if (queue->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  if (!tracking && internal::tracking_devices.load(std::memory_order_relaxed) == 0u) return;
  reshade::api::command_list* cmd_list = queue->get_immediate_command_list();
  auto state = internal::SlotsFor(queue->get_device(), tracking);
  if (!state) return;
  std::array<Microsoft::WRL::ComPtr<ID3D11SamplerState>, internal::SLOTS> retired;
  const std::unique_lock lock(state->mutex);
  if (tracking == state->tracking) return;
  if (tracking) {
    std::array<ID3D11SamplerState*, internal::SLOTS> current = {};
    reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->PSGetSamplers(0, internal::SLOTS, current.data());
    for (uint32_t slot = 0; slot < internal::SLOTS; ++slot) {
      retired[slot] = std::move(state->game[slot]);
      state->game[slot].Attach(current[slot]);
      state->bound[slot] = reinterpret_cast<uintptr_t>(current[slot]);
    }
    internal::tracking_devices.fetch_add(1u, std::memory_order_relaxed);
  } else {
    if (state->biased.load(std::memory_order_relaxed)) {
      internal::BindSlots(cmd_list, 0, internal::SLOTS, false, state.get());
    }
    retired = std::move(state->game);
    state->bound = {};
    internal::tracking_devices.fetch_sub(1u, std::memory_order_relaxed);
  }
  state->tracking = tracking;
}

inline void OnInitSampler(reshade::api::device* device, const reshade::api::sampler_desc& desc, reshade::api::sampler sampler) {
  if (internal::addon_sampler || device->get_api() != reshade::api::device_api::d3d11) return;
  const bool trilinear = desc.filter == reshade::api::filter_mode::min_mag_mip_linear && desc.address_u == reshade::api::texture_address_mode::wrap
                         && desc.address_v == reshade::api::texture_address_mode::wrap;
  if (!trilinear && desc.filter != reshade::api::filter_mode::anisotropic
      && desc.filter != reshade::api::filter_mode::min_mag_anisotropic_mip_point) {
    internal::other_samplers.fetch_add(1u, std::memory_order_relaxed);
    return;
  }
  // D3D11 can return an existing identity for an identical descriptor. Keep its owned clone.
  internal::samplers.lazy_emplace_l(sampler.handle, [](auto&) {}, [&](const auto& construct) {
    construct(sampler.handle, internal::BiasedSampler{.owner = device, .desc = desc, .trilinear = trilinear});
  });
}

inline void OnDestroySampler(reshade::api::device* device, reshade::api::sampler sampler) {
  reshade::api::sampler clone = {0};
  internal::samplers.erase_if(sampler.handle, [&](const auto& entry) {
    const auto& [sampler_handle, biased_sampler] = entry;
    if (biased_sampler.owner != device) return false;
    clone = biased_sampler.clone;
    return true;
  });
  if (clone.handle != 0u) {
    const bool was_addon_sampler = std::exchange(internal::addon_sampler, true);
    device->destroy_sampler(clone);
    internal::addon_sampler = was_addon_sampler;
  }
  // Slot originals own references, so a final release cannot leave a live slot pointing here.
  // Do not acquire a slot lock: clone release can reenter this callback while binding slots.
}

// After the game's PSSetSamplers: its samplers recorded, and bound again as clones in the main view.
inline void OnPushDescriptors(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages,
                              reshade::api::pipeline_layout, uint32_t, const reshade::api::descriptor_table_update& update) {
  if (internal::tracking_devices.load(std::memory_order_relaxed) == 0u
      || internal::addon_sampler
      || stages != reshade::api::shader_stage::pixel
      || update.type != reshade::api::descriptor_type::sampler || update.count == 0
      || update.binding >= internal::SLOTS || update.count > internal::SLOTS - update.binding || !internal::Immediate(cmd_list)) {
    return;
  }
  const auto* game_samplers = static_cast<const reshade::api::sampler*>(update.descriptors);
  auto state = internal::SlotsFor(cmd_list->get_device(), false);
  if (!state) return;
  std::array<Microsoft::WRL::ComPtr<ID3D11SamplerState>, internal::SLOTS> retired;
  const std::unique_lock lock(state->mutex);
  if (!state->tracking) return;
  for (uint32_t index = 0; index < update.count; ++index) {
    state->bound[update.binding + index] = game_samplers[index].handle;
    // The usual bind names the sampler the slot holds: no reference taken or dropped for it.
    auto& game_sampler = state->game[update.binding + index];
    auto* bound_sampler = reinterpret_cast<ID3D11SamplerState*>(game_samplers[index].handle);
    if (game_sampler.Get() == bound_sampler) continue;
    retired[index] = std::move(game_sampler);
    game_sampler = bound_sampler;
  }
  if (const bool biased = internal::Biased(); biased) {
    internal::BindSlots(cmd_list, update.binding, update.count, biased, state.get());
  }
}

// After the viewport scaling's: the main view started or ended, every slot bound again (clones or the game's).
inline void OnBindViewports(reshade::api::command_list* cmd_list, uint32_t, uint32_t, const reshade::api::viewport*) {
  if (internal::tracking_devices.load(std::memory_order_relaxed) == 0u || !internal::Immediate(cmd_list)) return;
  auto state = internal::SlotsFor(cmd_list->get_device(), false);
  if (!state) return;
  const bool biased = internal::Biased();
  if (biased == state->biased.load(std::memory_order_relaxed)) return;
  const std::unique_lock lock(state->mutex);
  if (!state->tracking || biased == state->biased.load(std::memory_order_relaxed)) return;
  internal::BindSlots(cmd_list, 0, internal::SLOTS, biased, state.get());
}

// Called with render callbacks quiescent. Release owned COM references outside map/slot locks.
inline void CleanupDevice(reshade::api::device* device, bool restore) {
  std::shared_ptr<internal::Slots> state;
  internal::device_slots.erase_if(device, [&](const auto& entry) {
    const auto& [slots_device, slots] = entry;
    state = slots;
    return true;
  });
  if (state && state->tracking) {
    internal::tracking_devices.fetch_sub(1u, std::memory_order_relaxed);
  }
  if (restore && state && state->tracking) {
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    reinterpret_cast<ID3D11Device*>(device->get_native())->GetImmediateContext(context.GetAddressOf());
    std::array<ID3D11SamplerState*, internal::SLOTS> original = {};
    for (size_t index = 0; index < original.size(); ++index) {
      original[index] = state->game[index].Get();
    }
    context->PSSetSamplers(0, internal::SLOTS, original.data());
  }
  std::vector<uint64_t> keys;
  internal::samplers.for_each([&](const auto& entry) {
    const auto& [game_sampler, biased_sampler] = entry;
    if (biased_sampler.owner == device) {
      keys.push_back(game_sampler);
    }
  });
  std::vector<Microsoft::WRL::ComPtr<ID3D11SamplerState>> clones;
  for (uint64_t key : keys) {
    internal::samplers.erase_if(key, [&](const auto& entry) {
      const auto& [game_sampler, biased_sampler] = entry;
      if (biased_sampler.owner != device) return false;
      Microsoft::WRL::ComPtr<ID3D11SamplerState> clone;
      clone.Attach(reinterpret_cast<ID3D11SamplerState*>(biased_sampler.clone.handle));
      clones.push_back(std::move(clone));
      return true;
    });
  }
}

inline void OnDestroyDevice(reshade::api::device* device) {
  CleanupDevice(device, false);
}

inline void Shutdown() {
  std::vector<reshade::api::device*> devices;
  internal::device_slots.for_each([&](const auto& entry) {
    const auto& [slots_device, slots] = entry;
    devices.push_back(slots_device);
  });
  internal::samplers.for_each([&](const auto& entry) {
    const auto& [game_sampler, biased_sampler] = entry;
    devices.push_back(biased_sampler.owner);
  });
  for (auto* device : devices) {
    CleanupDevice(device, true);
  }
  internal::bias.store(0.f);
  internal::main_view.store(nullptr);
  internal::other_samplers.store(0u);
}

}  // namespace twattila::texture_lod_bias
