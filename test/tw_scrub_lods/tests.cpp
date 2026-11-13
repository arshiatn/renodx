// Attila's grass and tree LOD scaling (twattila/graphics_hooks.hpp ScaleModelLods, OnDrawScrub's and OnDrawTree's), the
// per-thread cache of several models against v157's one-model cache (copied below as OldScaleModelLods), on two copies of
// the same fake model managers (the game's layout: manager +0x1c count / +0x20 models; model +0x28 LOD count / +0x2c LOD
// list; LOD +0x38 distance squared).
// 1. Same distances: every sequence the game makes (a model's instances in a run, cells alternating models, random order,
//    the slider changing, a model's setup again writing all its LODs, a new model at a model's index) leaves every LOD
//    distance of both copies bit-identical after every call, and equal to the closed form (the game's x the scale,
//    FLT_MAX kept).
// 2. A lone rewrite of a later LOD (no game path does this): the new cache scales it at the next call (the closed form);
//    the old one only at its next miss (printed, not compared).
// 3. Four threads on shared models (the game's TBB workers), each model scaled once first: both end at the closed form.
//    3b: a model's first scaling from four threads at once: v157 scaled some models twice (its map path's read, write and
//    insert were three steps; printed), this one never (the map path under the entry's lock: every model = closed form).
// 4. Cost: ns a call, cells alternating 6 models, runs of one model, and 128 models in turn (more than the cache holds:
//    every call takes the map path), one thread and four (printed).
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <shared_mutex>
#include <thread>
#include <vector>
#include "src/games/twattila/graphics_hooks.hpp"

namespace hooks = twattila::graphics_hooks;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Scrub LOD test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

namespace old {
// v157's ScaleModelLods (graphics_hooks.hpp at 3325e7c5), word for word but its own map and cache.
struct LastModel {
  const uint8_t* model = nullptr;
  float scale = 1.f;
  uint32_t first_written = 0;
};
static renodx::utils::data::ParallelFlatHashMap<uintptr_t, hooks::internal::ModelLods, std::shared_mutex> scaled_models;
static std::atomic<bool> models_scaled = false;
static thread_local LastModel last_model;
constexpr uint32_t MAX_MODEL_LODS = hooks::internal::MAX_MODEL_LODS;

static void ScaleModelLods(uint8_t* manager, uint32_t index, float scale) {
  uint32_t model_count;
  std::memcpy(&model_count, manager + 0x1c, sizeof(model_count));
  uint8_t* model = nullptr;
  if (index < model_count) {
    uint8_t** models;
    std::memcpy(&models, manager + 0x20, sizeof(models));
    model = models[index];
  }
  if (model != nullptr && (scale != 1.f || models_scaled.load(std::memory_order_relaxed))) {
    uint32_t count;
    std::memcpy(&count, model + 0x28, sizeof(count));
    count = std::min<uint32_t>(count, MAX_MODEL_LODS);
    uint8_t** entries;
    std::memcpy(&entries, model + 0x2c, sizeof(entries));
    uint32_t first_bits = 0;
    if (count > 0) std::memcpy(&first_bits, entries[0] + 0x38, sizeof(first_bits));
    if (model == last_model.model && scale == last_model.scale && first_bits == last_model.first_written) return;
    models_scaled.store(true, std::memory_order_relaxed);
    const auto key = reinterpret_cast<uintptr_t>(model);
    hooks::internal::ModelLods known;
    scaled_models.if_contains(key, [&](const auto& entry) {
      const auto& [model_key, model_lods] = entry;
      known = model_lods;
    });
    hooks::internal::ModelLods next = known;
    next.count = count;
    bool changed = (known.count != count);
    for (uint32_t index = 0; index < count; ++index) {
      uint32_t bits;
      std::memcpy(&bits, entries[index] + 0x38, sizeof(bits));
      if (known.count != count || bits != known.written[index]) {
        next.game[index] = std::bit_cast<float>(bits);
      }
      const float game = next.game[index];
      next.written[index] = std::bit_cast<uint32_t>(game < FLT_MAX ? std::min(game * scale, FLT_MAX) : game);
      if (next.written[index] != bits) {
        std::memcpy(entries[index] + 0x38, &next.written[index], sizeof(bits));
      }
      changed |= (next.written[index] != known.written[index]) || (next.game[index] != known.game[index]);
    }
    if (changed) scaled_models.insert_or_assign(key, next);
    last_model = {.model = model, .scale = scale, .first_written = (count > 0 ? next.written[0] : 0u)};
  }
}
}  // namespace old

// A manager with its models, in the game's layout. `game` is what the game set (the test's record of it).
struct World {
  struct Model {
    std::unique_ptr<uint8_t[]> bytes = std::make_unique<uint8_t[]>(0x40);
    std::vector<std::unique_ptr<uint8_t[]>> lods;
    std::vector<uint8_t*> lod_pointers;
    std::vector<float> game;
  };
  std::unique_ptr<uint8_t[]> manager = std::make_unique<uint8_t[]>(0x40);
  std::vector<std::unique_ptr<Model>> models;
  std::vector<uint8_t*> model_pointers;

  void Publish() {
    const auto count = static_cast<uint32_t>(model_pointers.size());
    std::memcpy(manager.get() + 0x1c, &count, sizeof(count));
    uint8_t** list = model_pointers.data();
    std::memcpy(manager.get() + 0x20, &list, sizeof(list));
  }
  static std::unique_ptr<Model> MakeModel(const std::vector<float>& distances) {
    auto model = std::make_unique<Model>();
    model->game = distances;
    for (const float distance : distances) {
      auto lod = std::make_unique<uint8_t[]>(0x40);
      std::memcpy(lod.get() + 0x38, &distance, sizeof(distance));
      model->lod_pointers.push_back(lod.get());
      model->lods.push_back(std::move(lod));
    }
    const auto count = static_cast<uint32_t>(distances.size());
    std::memcpy(model->bytes.get() + 0x28, &count, sizeof(count));
    uint8_t** list = model->lod_pointers.data();
    std::memcpy(model->bytes.get() + 0x2c, &list, sizeof(list));
    return model;
  }
  void Add(const std::vector<float>& distances) {
    models.push_back(MakeModel(distances));
    model_pointers.push_back(models.back()->bytes.get());
    Publish();
  }
  // The game's setup again: every LOD of the model `index` written with `distances`.
  void Setup(size_t index, const std::vector<float>& distances) {
    auto& model = *models[index];
    model.game = distances;
    for (size_t lod = 0; lod < distances.size(); ++lod) {
      std::memcpy(model.lod_pointers[lod] + 0x38, &distances[lod], sizeof(float));
    }
  }
  // A new model at index `index` (another address, another LOD count).
  void Replace(size_t index, const std::vector<float>& distances) {
    models.push_back(MakeModel(distances));
    model_pointers[index] = models.back()->bytes.get();
    std::swap(models[index], models.back());
  }
  uint32_t Bits(size_t model, size_t lod) const {
    uint32_t bits;
    std::memcpy(&bits, models[model]->lod_pointers[lod] + 0x38, sizeof(bits));
    return bits;
  }
};

static uint32_t Expected(float game, float scale) {
  return std::bit_cast<uint32_t>(game < FLT_MAX ? std::min(game * scale, FLT_MAX) : game);
}

static bool Same(const World& first, const World& second) {
  for (size_t model = 0; model < first.models.size(); ++model) {
    if (first.models[model]->game.size() != second.models[model]->game.size()) return false;
    for (size_t lod = 0; lod < first.models[model]->game.size(); ++lod) {
      if (first.Bits(model, lod) != second.Bits(model, lod)) return false;
    }
  }
  return true;
}

static bool ClosedForm(const World& world, size_t model, float scale) {
  for (size_t lod = 0; lod < world.models[model]->game.size(); ++lod) {
    if (world.Bits(model, lod) != Expected(world.models[model]->game[lod], scale)) return false;
  }
  return true;
}

static std::vector<float> Distances(std::mt19937* random, size_t count) {
  std::vector<float> distances;
  float distance = 0.f;
  for (size_t lod = 0; lod < count; ++lod) {
    distance += std::uniform_real_distribution<float>(50.f, 4000.f)(*random);
    distances.push_back(distance);
  }
  if (count > 2 && (*random)() % 4 == 0) distances.back() = FLT_MAX;  // a last LOD that never ends
  return distances;
}

int main() {
  std::mt19937 random(1113);
  // 1. Two copies of one scene: 8 grass models of 1 to 5 LODs (and FLT_MAX ends).
  World before;
  World after;
  for (size_t model = 0; model < 8; ++model) {
    const auto distances = Distances(&random, 1 + (model % 5));
    before.Add(distances);
    after.Add(distances);
  }
  float scale = 4.f;  // Grass Distance 2x (OnDrawScrub passes its square)
  uint32_t calls = 0;
  const auto call = [&](size_t model) {
    old::ScaleModelLods(before.manager.get(), static_cast<uint32_t>(model), scale);
    hooks::internal::ScaleModelLods(after.manager.get(), static_cast<uint32_t>(model), scale);
    ++calls;
    return Same(before, after) && ClosedForm(after, model, scale);
  };
  // Runs of one model (a cell's instances), cells alternating models, random order.
  for (int run = 0; run < 2000; ++run) {
    const size_t model = random() % 8;
    for (int instance = 0, length = 1 + static_cast<int>(random() % 12); instance < length; ++instance) CHECK(call(model));
  }
  for (int round = 0; round < 4000; ++round) CHECK(call(round % 6));
  for (int round = 0; round < 8000; ++round) CHECK(call(random() % 8));
  std::printf("1a runs / alternating / random: %u calls, every LOD of both copies equal after each, = closed form\n", calls);
  // The slider moving (1x included: the path still runs once a model was scaled), the game's setup again (all of a model's
  // LODs), a new model at an index.
  const std::array<float, 6> SCALES = {9.f, 1.f, 2.25f, 16.f, 0.25f, 4.f};
  uint32_t setups = 0;
  uint32_t replacements = 0;
  for (int round = 0; round < 30000; ++round) {
    if (round % 2500 == 0) scale = SCALES[(round / 2500) % SCALES.size()];
    if (round % 997 == 0) {
      const size_t model = random() % 8;
      const auto distances = Distances(&random, before.models[model]->game.size());
      before.Setup(model, distances);
      after.Setup(model, distances);
      ++setups;
    }
    if (round % 4999 == 0) {
      const size_t model = random() % 8;
      const auto distances = Distances(&random, 1 + (random() % 5));
      before.Replace(model, distances);
      after.Replace(model, distances);
      ++replacements;
    }
    const size_t model = ((round / 7) % 3 == 0) ? (round % 5) : (random() % 8);
    CHECK(call(model));
  }
  std::printf("1b slider changes, %u setups, %u new models: %u calls in all, identical and = closed form\n", setups,
              replacements, calls);

  // 2. A lone rewrite of a later LOD while the first still holds the addon's value: the new cache at the next call.
  {
    const size_t model = 4;  // 5 LODs
    CHECK(after.models[model]->game.size() >= 2);
    CHECK(call(model));  // the model last scaled on this thread
    for (World* world : {&before, &after}) {
      world->models[model]->game[1] += 1.f;
      std::memcpy(world->models[model]->lod_pointers[1] + 0x38, &world->models[model]->game[1], sizeof(float));
    }
    old::ScaleModelLods(before.manager.get(), model, scale);
    hooks::internal::ScaleModelLods(after.manager.get(), model, scale);
    CHECK(ClosedForm(after, model, scale));
    std::printf("2 lone later-LOD rewrite: new = closed form at the next call; old %s\n",
                ClosedForm(before, model, scale) ? "too" : "still the game's (until its next miss)");
  }

  // 3. Four threads on 16 shared models, cells alternating, each model scaled once on this thread first; then the closed
  //    form everywhere for both.
  {
    World old_world;
    World new_world;
    for (size_t model = 0; model < 16; ++model) {
      const auto distances = Distances(&random, 1 + (model % 5));
      old_world.Add(distances);
      new_world.Add(distances);
    }
    constexpr float SCALE = 6.25f;
    for (uint32_t model = 0; model < 16; ++model) {
      old::ScaleModelLods(old_world.manager.get(), model, SCALE);
      hooks::internal::ScaleModelLods(new_world.manager.get(), model, SCALE);
    }
    std::vector<std::thread> threads;
    for (uint32_t thread = 0; thread < 4; ++thread) {
      threads.emplace_back([&, thread] {
        for (uint32_t round = 0; round < 50000; ++round) {
          const uint32_t model = (round * 7 + thread * 3) % 16;
          old::ScaleModelLods(old_world.manager.get(), model, SCALE);
          hooks::internal::ScaleModelLods(new_world.manager.get(), model, SCALE);
        }
      });
    }
    for (auto& thread : threads) thread.join();
    for (size_t model = 0; model < 16; ++model) {
      CHECK(ClosedForm(old_world, model, SCALE) && ClosedForm(new_world, model, SCALE));
    }
    CHECK(Same(old_world, new_world));
    std::printf("3 four threads x 50000 calls on 16 models: both = closed form\n");
  }

  // 3b. Cold starts: 200 times four threads started together on 16 new models (each version on its own copy).
  {
    constexpr float SCALE = 6.25f;
    uint32_t old_twice = 0;
    uint32_t new_twice = 0;
    for (uint32_t trial = 0; trial < 200; ++trial) {
      World old_world;
      World new_world;
      for (size_t model = 0; model < 16; ++model) {
        const auto distances = Distances(&random, 1 + (model % 5));
        old_world.Add(distances);
        new_world.Add(distances);
      }
      std::atomic<uint32_t> ready = 0;
      std::vector<std::thread> threads;
      for (uint32_t thread = 0; thread < 4; ++thread) {
        threads.emplace_back([&, thread] {
          ready.fetch_add(1);
          while (ready.load() < 4) {
          }
          for (uint32_t round = 0; round < 64; ++round) {
            const uint32_t model = (round * 7 + thread * 3) % 16;
            old::ScaleModelLods(old_world.manager.get(), model, SCALE);
            hooks::internal::ScaleModelLods(new_world.manager.get(), model, SCALE);
          }
        });
      }
      for (auto& thread : threads) thread.join();
      for (size_t model = 0; model < 16; ++model) {
        old_twice += ClosedForm(old_world, model, SCALE) ? 0u : 1u;
        new_twice += ClosedForm(new_world, model, SCALE) ? 0u : 1u;
      }
    }
    std::printf("3b cold starts, 200 x 4 threads x 16 models: off the closed form v157 %u, now %u models\n", old_twice,
                new_twice);
    CHECK(new_twice == 0);
  }

  // 4. Cost a call (the game's DrawScrub in between not counted).
  {
    World old_world;
    World new_world;
    for (size_t model = 0; model < 128; ++model) {
      const auto distances = Distances(&random, 3);
      old_world.Add(distances);
      new_world.Add(distances);
    }
    constexpr float SCALE = 4.f;
    constexpr uint32_t CALLS = 2000000;
    enum class Order : uint8_t { ALTERNATING, RUNS, PAST_CACHE };
    const auto time = [&](auto scale_lods, uint8_t* manager, Order order, uint32_t thread_count) {
      std::vector<std::thread> threads;
      const auto start = std::chrono::steady_clock::now();
      for (uint32_t thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&, thread] {
          for (uint32_t round = 0; round < CALLS; ++round) {
            uint32_t model = 0;
            switch (order) {
              case Order::ALTERNATING: model = (round + thread) % 6; break;
              case Order::RUNS:        model = ((round / 64) + thread) % 6; break;
              case Order::PAST_CACHE:  model = (round + thread * 32) % 128; break;
            }
            scale_lods(manager, model, SCALE);
          }
        });
      }
      for (auto& thread : threads) thread.join();
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / CALLS;
    };
    for (const uint32_t threads : {1u, 4u}) {
      for (const Order order : {Order::ALTERNATING, Order::RUNS, Order::PAST_CACHE}) {
        const double old_ns = time(&old::ScaleModelLods, old_world.manager.get(), order, threads);
        const double new_ns = time(&hooks::internal::ScaleModelLods, new_world.manager.get(), order, threads);
        const char* name = (order == Order::ALTERNATING ? "cells alternating 6 models"
                            : order == Order::RUNS      ? "runs of 64 of one model"
                                                        : "128 models in turn (the map path every call)");
        std::printf("4 %s, %u thread%s: old %.1f ns a call, new %.1f ns a call (wall time / calls a thread)\n", name,
                    threads, threads == 1 ? "" : "s", old_ns, new_ns);
      }
    }
    for (size_t model = 0; model < 128; ++model) CHECK(ClosedForm(new_world, model, SCALE) && ClosedForm(old_world, model, SCALE));
  }
  std::printf("PASS\n");
  return 0;
}
