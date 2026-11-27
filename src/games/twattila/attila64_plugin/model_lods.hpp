/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

#include "plugin_block.h"

// A model's LOD distances x a scale, for the grass and campaign tree rows: the game picks such an instance's LOD by
// comparing its distance squared with each LOD entry of its model, so the hooks write the entries themselves. Per model
// the game's values are kept beside the written ones; a value that is not the last one written is the game's (its setup
// again, or another model at the same address). The 32-bit add-on's ScaleModelLods (graphics_hooks.hpp) with its
// locking order: the thread's own cache, the shared lock, the exclusive lock. No allocation; nothing here throws.
namespace twattila::attila64_plugin::model_lods {

// A scene node's models, a model's LOD entries (pointers), and an entry's fields, as the game's code reads them.
inline constexpr size_t NODE_MODEL_COUNT = 0x34;
inline constexpr size_t NODE_MODELS = 0x38;
inline constexpr size_t MODEL_LOD_COUNT = 0x44;
inline constexpr size_t MODEL_LODS = 0x48;
inline constexpr size_t LOD_DISTANCE_SQUARED = 0x50;  // a float
inline constexpr size_t LOD_ENABLED = 0x5c;           // a byte
// A model's entries past these keep the game's distances, as on 32-bit.
inline constexpr uint32_t MAX_MODEL_LODS = 8;

struct Model {
  uintptr_t model = 0;                                // its address; 0: a free slot
  uint32_t count = 0;
  std::array<uint32_t, MAX_MODEL_LODS> game = {};     // the game's distances squared, as bits
  std::array<uint32_t, MAX_MODEL_LODS> written = {};  // the scaled ones written over them
};

// The table: 65536 models in 64 stripes by the model address's hash, open addressing inside a stripe. Slots are
// claimed and never freed. 5 MiB of zero pages (5243392 bytes), touched only where models are scaled.
inline constexpr size_t STRIPE_BITS = 6;
inline constexpr size_t STRIPE_MODEL_BITS = 10;
inline constexpr size_t STRIPES = size_t{1} << STRIPE_BITS;
inline constexpr size_t STRIPE_MODELS = size_t{1} << STRIPE_MODEL_BITS;
struct Stripe {
  std::shared_mutex mutex;
  std::array<Model, STRIPE_MODELS> models = {};
};
static std::array<Stripe, STRIPES> table;
static_assert(sizeof(Model) == 80 && sizeof(Stripe) == sizeof(std::shared_mutex) + (STRIPE_MODELS * sizeof(Model)));
static_assert(sizeof(table) == 5243392, "the table's bytes, as its comment says");
// The plugin is never unloaded and nothing may run at process exit.
static_assert(std::is_trivially_destructible_v<Stripe>);

// The models this thread scaled last, by address (direct-mapped): the grass cells draw their instances model after
// model in turn, on several threads. An entry holds what was written; a model whose distances all still hold it at
// the same scale needs nothing and no lock. 512 entries as on 32-bit (twice the grass models, the trees beside them).
struct CachedModel {
  const uint8_t* model = nullptr;
  float scale = 0.f;
  uint32_t count = 0;
  std::array<uint32_t, MAX_MODEL_LODS> written = {};
};
inline constexpr uint32_t CACHED_MODEL_BITS = 9;
static thread_local std::array<CachedModel, size_t{1} << CACHED_MODEL_BITS> cached_models;

// The node's model `index`: its LOD distances (squared) x `scale` (squared). Returns the model; nullptr: none at
// `index`. A model that finds its stripe full keeps the game's distances and leaves NOTICE_MODEL_TABLE_FULL.
static const uint8_t* Scale(const uint8_t* node, uint32_t index, float scale, std::atomic<uint32_t>* notices) noexcept {
  uint32_t model_count = 0;
  std::memcpy(&model_count, node + NODE_MODEL_COUNT, sizeof(model_count));
  if (index >= model_count) return nullptr;
  const uint8_t* const* models = nullptr;
  std::memcpy(&models, node + NODE_MODELS, sizeof(models));
  const uint8_t* model = models[index];
  if (model == nullptr) return nullptr;
  uint32_t count = 0;
  std::memcpy(&count, model + MODEL_LOD_COUNT, sizeof(count));
  count = std::min(count, MAX_MODEL_LODS);
  uint8_t* const* lods = nullptr;
  std::memcpy(&lods, model + MODEL_LODS, sizeof(lods));

  const auto key = reinterpret_cast<uintptr_t>(model);
  const uint32_t hash = static_cast<uint32_t>(key >> 4) * 0x9E3779B1u;
  const auto holds = [&](const std::array<uint32_t, MAX_MODEL_LODS>& written) {
    for (uint32_t lod = 0; lod < count; ++lod) {
      if (std::memcmp(lods[lod] + LOD_DISTANCE_SQUARED, &written[lod], sizeof(uint32_t)) != 0) return false;
    }
    return true;
  };
  CachedModel& cached = cached_models[hash >> (32 - CACHED_MODEL_BITS)];
  if (cached.model == model && cached.scale == scale && cached.count == count && holds(cached.written)) return model;

  // An infinite distance stays infinite and a NaN is kept; x 1 is the game's bits.
  const auto scaled = [&](uint32_t game_bits) {
    const auto game = std::bit_cast<float>(game_bits);
    return ((game < FLT_MAX) ? std::bit_cast<uint32_t>(std::min(game * scale, FLT_MAX)) : game_bits);
  };
  Stripe& stripe = table[hash >> (32 - STRIPE_BITS)];
  // The model's slot, else the first free one on its probe path; nullptr: the stripe is full of other models.
  const auto find_slot = [&]() -> Model* {
    for (size_t probe = 0; probe < STRIPE_MODELS; ++probe) {
      Model& slot = stripe.models[((hash >> 16) + probe) & (STRIPE_MODELS - 1)];
      if (slot.model == key || slot.model == 0) return &slot;
    }
    return nullptr;
  };
  const auto leave_notice = [&] {
    if ((notices->load(std::memory_order_relaxed) & NOTICE_MODEL_TABLE_FULL) == 0) {
      notices->fetch_or(NOTICE_MODEL_TABLE_FULL, std::memory_order_relaxed);
    }
  };
  // Most misses find the model as another thread (or this one before its cache entry was replaced) left it: nothing
  // to write, under the shared lock. A model that finds its stripe full here stays out for good (slots are never
  // freed), so each of its later calls ends here too, without the exclusive lock.
  {
    const std::shared_lock lock(stripe.mutex);
    const Model* found = find_slot();
    if (found == nullptr) {
      leave_notice();
      return model;
    }
    bool settled = (found->model == key && found->count == count && holds(found->written));
    for (uint32_t lod = 0; settled && lod < count; ++lod) {
      settled = (found->written[lod] == scaled(found->game[lod]));
    }
    if (settled) {
      cached = {.model = model, .scale = scale, .count = count, .written = found->written};
      return model;
    }
  }
  // The slot read, the distances written and the slot updated under one exclusive lock: as separate steps, two threads
  // scaling one model at once could take each other's written value for the game's and scale the model twice.
  const std::scoped_lock lock(stripe.mutex);
  Model* slot = find_slot();
  if (slot == nullptr) {
    leave_notice();
    return model;
  }
  const bool every_value_is_the_games = (slot->model != key || slot->count != count);
  slot->model = key;
  slot->count = count;
  for (uint32_t lod = 0; lod < count; ++lod) {
    uint32_t bits = 0;
    std::memcpy(&bits, lods[lod] + LOD_DISTANCE_SQUARED, sizeof(bits));
    if (every_value_is_the_games || bits != slot->written[lod]) {
      slot->game[lod] = bits;
    }
    slot->written[lod] = scaled(slot->game[lod]);
    if (slot->written[lod] != bits) {
      std::memcpy(lods[lod] + LOD_DISTANCE_SQUARED, &slot->written[lod], sizeof(bits));
    }
  }
  cached = {.model = model, .scale = scale, .count = count, .written = slot->written};
  return model;
}

}  // namespace twattila::attila64_plugin::model_lods
