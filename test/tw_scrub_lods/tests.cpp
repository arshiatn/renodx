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
//    3 cold: the same on new models without that first scaling: this version at the closed form (v157 printed).
//    3b: a model's first scaling from four threads at once: v157 scaled some models twice (its map path's read, write and
//    insert were three steps; printed), this one never (the map path under the entry's lock: every model = closed form).
// 4. Cost: ns a call, cells alternating 6 models, runs of one model, and 128 models in turn (more than the cache holds:
//    every call takes the map path), one thread and four (printed).
// 5. The scrub's cell radius clamp (OnScrubUpdate): a replay of the game's ScrubUpdate, cell loop and DrawScrub (namespace
//    scrub) through the hooks and at v158's radius: the same draws in 400 random scenes; visits saved printed, also for
//    G1's grass (the models and radius of ATN's Grass Trace).
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
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

// The game's scrub replayed for section 5 (empire.retail.dll as graphics_hooks.hpp SCRUB_UPDATE reads it): ScrubUpdate's
// quadtree query of the box camera +- float(+0x80) (+0x1898800, the frustum test left out: it doesn't depend on +0x80),
// the cell loop (+0x1897a30: the distance squared <= float(+0x80 squared as an int), the byte grid, the bias, the
// transform) and DrawScrub (+0x17f9800: the key and the first enabled LOD past it), with the float operations in the
// game's order. The scrub's, the manager's, the models' and the LODs' fields the hooks read are in the game's layout;
// cells and records in the test's own.
namespace scrub {

struct Record {
  std::array<float, 3> position = {};
  uint8_t slot = 0;
  uint8_t thinning = 0;  // the record's +0x11
  bool grid = true;      // the byte grid holds 1 at its place
  uint32_t id = 0;
};

struct Node {
  std::array<float, 3> low = {};
  std::array<float, 3> high = {};
  std::vector<Record> records;
  std::vector<std::unique_ptr<Node>> children;  // none or 4
};

struct Draw {
  uint32_t id = 0;
  uint32_t model = 0;
  uint32_t lod = 0;
  uint32_t key = 0;
  uint32_t bias = 0;
  std::array<uint32_t, 12> transform = {};
  auto operator<=>(const Draw&) const = default;
};

struct Visits {
  uint64_t cells = 0;
  uint64_t records = 0;  // records past the distance test (each a DrawScrub call when the grid allows it)
  uint64_t draws = 0;
};

struct ModelSpec {
  std::vector<float> distances;  // the game's, squared
  std::vector<uint8_t> enabled;
};

struct Spec {
  std::vector<ModelSpec> models;  // the manager's (scrub models and others)
  std::vector<uint32_t> slots;    // the scrub's: the manager's model index per slot (0xffffffff: none)
  float lod_offset = 0.f;         // manager +0xc34
  int32_t game_cells = 200;
  float game_bias = 200.f;
  std::array<float, 3> camera = {};
};

// One copy of a scene's game objects (each run scales its own LODs).
struct Objects {
  std::unique_ptr<uint8_t[]> manager = std::make_unique<uint8_t[]>(0xc40);
  std::unique_ptr<uint8_t[]> scrub = std::make_unique<uint8_t[]>(0x120);
  std::vector<std::unique_ptr<uint8_t[]>> storage;
  std::vector<uint8_t*> models;
  std::vector<std::vector<uint8_t*>> lods;
  std::vector<uint32_t> slots;

  explicit Objects(const Spec& spec) {
    for (const auto& model_spec : spec.models) {
      uint8_t* model = storage.emplace_back(std::make_unique<uint8_t[]>(0x40)).get();
      auto& entries = lods.emplace_back();
      for (size_t lod = 0; lod < model_spec.distances.size(); ++lod) {
        uint8_t* entry = storage.emplace_back(std::make_unique<uint8_t[]>(0x48)).get();
        std::memcpy(entry + 0x38, &model_spec.distances[lod], sizeof(float));
        entry[0x44] = model_spec.enabled[lod];
        entries.push_back(entry);
      }
      const auto count = static_cast<uint32_t>(entries.size());
      std::memcpy(model + 0x28, &count, sizeof(count));
      uint8_t** list = entries.data();
      std::memcpy(model + 0x2c, &list, sizeof(list));
      models.push_back(model);
    }
    const auto model_count = static_cast<uint32_t>(models.size());
    std::memcpy(manager.get() + 0x1c, &model_count, sizeof(model_count));
    uint8_t** model_list = models.data();
    std::memcpy(manager.get() + 0x20, &model_list, sizeof(model_list));
    std::memcpy(manager.get() + 0xc34, &spec.lod_offset, sizeof(float));
    slots = spec.slots;
    std::memcpy(scrub.get() + 0x80, &spec.game_cells, sizeof(int32_t));
    std::memcpy(scrub.get() + 0x84, &spec.game_bias, sizeof(float));
    uint8_t* manager_pointer = manager.get();
    std::memcpy(scrub.get() + 0x90, &manager_pointer, sizeof(manager_pointer));
    const auto slot_count = static_cast<uint32_t>(slots.size());
    std::memcpy(scrub.get() + 0x98, &slot_count, sizeof(slot_count));
    const uint32_t* slot_list = slots.data();
    std::memcpy(scrub.get() + 0x9c, &slot_list, sizeof(slot_list));
  }
};

// The replay's inputs and outputs (one run at a time, one thread).
static const Node* root = nullptr;
static std::array<float, 3> camera = {};
static const std::array<float, 14>* params = nullptr;  // the cell loop's per-record block: +0x10 the bias
static std::vector<Draw>* draws = nullptr;
static Visits* visits = nullptr;
static hooks::internal::DrawScrub draw_scrub = nullptr;  // what the cell loop calls (the manager's vtable +0x80)
static float reference_scale = 1.f;                      // the reference run's LOD scale (squared)

static void __fastcall GameDrawScrub(uint8_t* manager, void*, uint32_t index, const float* transform, uint32_t, uint32_t,
                                     uint32_t id) {
  if (index == 0xffffffffu) return;
  uint32_t model_count;
  std::memcpy(&model_count, manager + 0x1c, sizeof(model_count));
  if (index >= model_count) return;
  uint8_t** models;
  std::memcpy(&models, manager + 0x20, sizeof(models));
  uint32_t count;
  uint8_t** entries;
  std::memcpy(&count, models[index] + 0x28, sizeof(count));
  std::memcpy(&entries, models[index] + 0x2c, sizeof(entries));
  const float dx = transform[3] - camera[0];
  const float dy = transform[7] - camera[1];
  const float dz = transform[11] - camera[2];
  float distance_squared = dy * dy + dx * dx;
  distance_squared = distance_squared + dz * dz;
  float lod_offset;
  std::memcpy(&lod_offset, manager + 0xc34, sizeof(lod_offset));
  const float bias = (*params)[4];
  const float offset = lod_offset + bias;
  const float key = distance_squared + offset;
  for (uint32_t lod = 0; lod < count; ++lod) {
    float distance;
    std::memcpy(&distance, entries[lod] + 0x38, sizeof(distance));
    if (distance > key && entries[lod][0x44] != 0) {
      Draw draw = {.id = id, .model = index, .lod = lod, .key = std::bit_cast<uint32_t>(key), .bias = std::bit_cast<uint32_t>(bias)};
      for (size_t element = 0; element < 12; ++element) draw.transform[element] = std::bit_cast<uint32_t>(transform[element]);
      draws->push_back(draw);
      ++visits->draws;
      return;
    }
  }
}

// v158's draw: the LODs scaled by v157's code (section 1: the same distances), then the game's.
static void __fastcall ReferenceDrawScrub(uint8_t* manager, void* edx, uint32_t index, const float* transform,
                                          uint32_t third, uint32_t fourth, uint32_t id) {
  old::ScaleModelLods(manager, index, reference_scale);
  GameDrawScrub(manager, edx, index, transform, third, fourth, id);
}

static void __fastcall GameScrubUpdate(uint8_t* scrub, void*) {
  int32_t radius;
  float bias_scale;
  uint8_t* manager;
  const uint32_t* slots;
  std::memcpy(&radius, scrub + 0x80, sizeof(radius));
  std::memcpy(&bias_scale, scrub + 0x84, sizeof(bias_scale));
  std::memcpy(&manager, scrub + 0x90, sizeof(manager));
  std::memcpy(&slots, scrub + 0x9c, sizeof(slots));
  const float box_radius = static_cast<float>(radius);
  std::array<float, 3> low;
  std::array<float, 3> high;
  for (size_t axis = 0; axis < 3; ++axis) {
    low[axis] = camera[axis] - box_radius;
    high[axis] = camera[axis] + box_radius;
  }
  std::vector<const Node*> cells;
  const auto query = [&](const auto& self, const Node& node) -> void {
    for (size_t axis = 0; axis < 3; ++axis) {
      if (node.low[axis] > high[axis] || low[axis] > node.high[axis]) return;
    }
    if (!node.records.empty()) cells.push_back(&node);
    for (const auto& child : node.children) self(self, *child);
  };
  query(query, *root);
  const float radius_squared = static_cast<float>(radius * radius);
  const float thinning_scale = std::bit_cast<float>(0x37810101u);  // 1 / 65025 as the game has it
  for (const Node* cell : cells) {
    ++visits->cells;
    for (const Record& record : cell->records) {
      const float dx = record.position[0] - camera[0];
      const float dy = record.position[1] - camera[1];
      const float dz = record.position[2] - camera[2];
      float distance_squared = dx * dx + dy * dy;
      distance_squared = distance_squared + dz * dz;
      if (!(distance_squared <= radius_squared)) continue;
      ++visits->records;
      if (!record.grid) continue;
      const float thinning = static_cast<float>(record.thinning);
      std::array<float, 14> block = {};
      block[4] = (((thinning * thinning_scale) * bias_scale) * thinning) * bias_scale;
      const float angle = static_cast<float>(record.id % 64) * 0.09817477f;
      const float cosine = std::cos(angle);
      const float sine = std::sin(angle);
      const std::array<float, 12> transform = {cosine, 0.f, -sine, record.position[0],
                                               0.f, 1.f, 0.f, record.position[1],
                                               sine, 0.f, cosine, record.position[2]};
      params = &block;
      draw_scrub(manager, nullptr, slots[record.slot], transform.data(), 0, 0, record.id);
    }
  }
}

// A quadtree over [-extent, extent]^2 (x, z), `depth` levels below the root, `records` in its leaves by position; every
// node's bounds hold its records and its children's.
static std::unique_ptr<Node> Build(std::vector<Record> records, float extent, int depth) {
  const auto build = [](const auto& self, std::vector<Record> held, float x0, float z0, float size, int level)
      -> std::unique_ptr<Node> {
    auto node = std::make_unique<Node>();
    if (level == 0) {
      node->records = std::move(held);
    } else {
      std::array<std::vector<Record>, 4> parts;
      const float half = size * 0.5f;
      for (const Record& record : held) {
        const size_t column = (record.position[0] >= x0 + half) ? 1 : 0;
        const size_t row = (record.position[2] >= z0 + half) ? 1 : 0;
        parts[row * 2 + column].push_back(record);
      }
      for (size_t part = 0; part < 4; ++part) {
        node->children.push_back(self(self, std::move(parts[part]), x0 + half * static_cast<float>(part % 2),
                                      z0 + half * static_cast<float>(part / 2), half, level - 1));
      }
    }
    node->low = {x0, 0.f, z0};
    node->high = {x0 + size, 0.f, z0 + size};
    for (const Record& record : node->records) {
      for (size_t axis = 0; axis < 3; ++axis) {
        node->low[axis] = std::min(node->low[axis], record.position[axis]);
        node->high[axis] = std::max(node->high[axis], record.position[axis]);
      }
    }
    for (const auto& child : node->children) {
      for (size_t axis = 0; axis < 3; ++axis) {
        node->low[axis] = std::min(node->low[axis], child->low[axis]);
        node->high[axis] = std::max(node->high[axis], child->high[axis]);
      }
    }
    return node;
  };
  return build(build, std::move(records), -extent, -extent, 2.f * extent, depth);
}

}  // namespace scrub

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
  // 3 cold: the same without the first scaling on this thread (658223e3's section 3, its check at line 277 failing about
  // 1 run in 20 on amd64 through the first-touch race): this version = closed form; v157's models off it printed.
  {
    World old_world;
    World new_world;
    for (size_t model = 0; model < 16; ++model) {
      const auto distances = Distances(&random, 1 + (model % 5));
      old_world.Add(distances);
      new_world.Add(distances);
    }
    constexpr float SCALE = 6.25f;
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
    uint32_t old_off = 0;
    for (size_t model = 0; model < 16; ++model) {
      CHECK(ClosedForm(new_world, model, SCALE));
      old_off += ClosedForm(old_world, model, SCALE) ? 0u : 1u;
    }
    std::printf("3 cold four threads x 50000 calls on 16 new models: this = closed form; v157 %u models off\n", old_off);
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

  // 5. The cell radius clamp (OnScrubUpdate): every scene's frames through the hooks (the clamp) and at the radius v158
  //    wrote (the game's x Grass Distance, LODs scaled by v157's code): the same records drawn, each with the same model,
  //    LOD, key, bias and transform bits. Scenes: 1 to 10 scrub models among others (far LODs the scrub never draws), 1 to 10
  //    LODs, disabled, FLT_MAX and NaN ones, far plants (3 km), a slot without a model, manager +0xc34 below, at and above
  //    0, game radius 200 / 400 / 1600, the slider 0.5x-4x, Grass Density, records across the map and 1-3 m around the
  //    clamp and around every LOD's distance.
  {
    hooks::internal::hooks_active.store(true);
    hooks::internal::original_scrub_update = &scrub::GameScrubUpdate;
    hooks::internal::original_draw_scrub = &scrub::GameDrawScrub;
    bool installed = false;
    renodx::utils::settings::Setting status;
    std::vector<std::unique_ptr<uint8_t[]>> retired;  // every scene's scrub at its own address
    scrub::Visits hooked_visits;
    scrub::Visits reference_visits;
    uint32_t clamped_scenes = 0;
    const auto run = [&](const scrub::Spec& spec, const std::vector<scrub::Record>& records, float distance,
                         float density) {
      const auto tree = scrub::Build(records, 2000.f, 3);
      scrub::root = tree.get();
      scrub::camera = spec.camera;
      hooks::Update({.grass_distance = distance, .grass_density = density}, &installed, &status);
      scrub::Objects hooked(spec);
      scrub::Objects reference(spec);
      const int32_t reference_cells = static_cast<int32_t>(std::lround(static_cast<float>(spec.game_cells) * distance));
      const float reference_bias = spec.game_bias * distance / std::max(density, 0.010001118f);
      scrub::reference_scale = distance * distance;
      bool same = true;
      for (int frame = 0; frame < 2; ++frame) {
        std::vector<scrub::Draw> hooked_draws;
        scrub::draws = &hooked_draws;
        scrub::visits = &hooked_visits;
        scrub::draw_scrub = &hooks::internal::OnDrawScrub;
        hooks::internal::OnScrubUpdate(hooked.scrub.get(), nullptr);
        std::vector<scrub::Draw> reference_draws;
        std::memcpy(reference.scrub.get() + 0x80, &reference_cells, sizeof(reference_cells));
        std::memcpy(reference.scrub.get() + 0x84, &reference_bias, sizeof(reference_bias));
        scrub::draws = &reference_draws;
        scrub::visits = &reference_visits;
        scrub::draw_scrub = &scrub::ReferenceDrawScrub;
        scrub::GameScrubUpdate(reference.scrub.get(), nullptr);
        std::ranges::sort(hooked_draws);
        std::ranges::sort(reference_draws);
        same = same && (hooked_draws == reference_draws);
        int32_t written;
        std::memcpy(&written, hooked.scrub.get() + 0x80, sizeof(written));
        if (frame == 0 && written < reference_cells) ++clamped_scenes;
      }
      retired.push_back(std::move(hooked.scrub));
      return same;
    };
    std::mt19937 scene_random(5113);
    const auto uniform = [&](float low, float high) { return std::uniform_real_distribution<float>(low, high)(scene_random); };
    const auto chance = [&](float probability) { return uniform(0.f, 1.f) < probability; };
    const auto pick = [&](std::initializer_list<float> values) { return *(values.begin() + scene_random() % values.size()); };
    constexpr int SCENES = 400;
    for (int scene = 0; scene < SCENES; ++scene) {
      scrub::Spec spec;
      for (size_t other = 0, others = 1 + scene_random() % 4; other < others; ++other) {
        spec.models.push_back({.distances = {5000.f * 5000.f}, .enabled = {1}});
      }
      for (size_t model = 0, models = 1 + scene_random() % 10; model < models; ++model) {
        scrub::ModelSpec lods;
        float metres = uniform(5.f, 120.f);
        for (size_t lod = 0, count = (chance(0.05f) ? 10 : 1 + scene_random() % 5); lod < count; ++lod) {
          lods.distances.push_back(metres * metres);
          lods.enabled.push_back(chance(0.85f) ? 1 : 0);
          metres += uniform(10.f, (scene % 2 == 0) ? 120.f : 400.f);
        }
        if (chance(0.06f)) lods.distances.back() = 3000.f * 3000.f;
        if (chance(0.05f)) lods.distances.back() = FLT_MAX;
        if (chance(0.03f)) lods.distances[scene_random() % lods.distances.size()] = std::numeric_limits<float>::quiet_NaN();
        spec.slots.push_back(static_cast<uint32_t>(spec.models.size()));
        spec.models.push_back(std::move(lods));
      }
      if (chance(0.2f)) spec.slots.push_back(0xffffffffu);
      spec.lod_offset = pick({0.f, 100.f, -2500.f, 1e4f, -1e4f, 0.f});
      spec.game_cells = static_cast<int32_t>(pick({200.f, 400.f, 1600.f, 200.f}));
      spec.game_bias = pick({200.f, 0.f, 50.f});
      spec.camera = {uniform(-1000.f, 1000.f), uniform(5.f, 300.f), uniform(-1000.f, 1000.f)};
      const float distance = pick({1.f, 0.5f, 1.37f, 2.f, 4.f, 1.f});
      const float density = pick({1.f, 2.f, 8.f, 0.5f});
      std::vector<scrub::Record> records;
      const auto add = [&](std::array<float, 3> position, uint8_t slot, uint8_t thinning) {
        records.push_back({.position = position, .slot = slot, .thinning = thinning, .grid = chance(0.9f),
                           .id = static_cast<uint32_t>(records.size())});
      };
      const auto random_slot = [&] { return static_cast<uint8_t>(scene_random() % spec.slots.size()); };
      for (int record = 0; record < 2500; ++record) {
        add({uniform(-2000.f, 2000.f), uniform(0.f, 40.f), uniform(-2000.f, 2000.f)}, random_slot(),
            static_cast<uint8_t>(scene_random() % 256));
      }
      // Around a distance from the camera, in a random direction.
      const auto around = [&](double metres) {
        const float heading = uniform(0.f, 6.2831853f);
        const float height = uniform(-0.3f, 0.3f);
        const auto radius = static_cast<float>(metres);
        return std::array<float, 3>{spec.camera[0] + radius * std::cos(heading) * std::sqrt(1.f - height * height),
                                    spec.camera[1] + radius * height,
                                    spec.camera[2] + radius * std::sin(heading) * std::sqrt(1.f - height * height)};
      };
      // The clamp's reach as the hook finds it (the slots' models, scaled), and every LOD's distance.
      float farthest = 0.f;
      for (size_t slot = 0; slot < spec.slots.size(); ++slot) {
        if (spec.slots[slot] >= spec.models.size()) continue;
        const auto& model = spec.models[spec.slots[slot]];
        for (size_t lod = 0; lod < model.distances.size(); ++lod) {
          const float game = model.distances[lod];
          const float scaled = (lod < hooks::internal::MAX_MODEL_LODS ? std::bit_cast<float>(Expected(game, distance * distance)) : game);
          if (model.enabled[lod] != 0 && scaled > farthest) farthest = scaled;
          if (std::isfinite(scaled) && scaled < 1e8f && scaled - spec.lod_offset > 0.f) {
            for (int copy = 0; copy < 4; ++copy) {
              add(around(std::sqrt(static_cast<double>(scaled) - spec.lod_offset) + uniform(-0.05f, 0.05f)),
                  static_cast<uint8_t>(slot), static_cast<uint8_t>(copy == 0 ? 0 : scene_random() % 256));
            }
          }
        }
      }
      if (const double reach = std::sqrt(static_cast<double>(farthest) - std::min(spec.lod_offset, 0.f)); reach < 4000.0) {
        for (int record = 0; record < 300; ++record) {
          add(around(reach + uniform(-3.f, 3.f)), random_slot(), static_cast<uint8_t>(scene_random() % 4));
        }
      }
      CHECK(run(spec, records, distance, density));
    }
    std::printf("5 cell radius clamp, %d scenes x 2 frames: the same draws (model, LOD, key, bias, transform bits) as v158's "
                "radius; clamped in %u scenes; cells visited %llu -> %llu, records past the distance test %llu -> %llu, "
                "draws %llu\n",
                SCENES, clamped_scenes, reference_visits.cells, hooked_visits.cells, reference_visits.records,
                hooked_visits.records, hooked_visits.draws);
    CHECK(hooked_visits.draws == reference_visits.draws);

    // G1's grass (cpu_runs/G1_121119 Grass Trace at 1x: the game's radius 1600 m, bias 200; 8 grass models with LODs to
    // 75 / 150 / 300 m, 3 plants to 300 / 600 / 900 / 2543-3276 m), records evenly over the map: visits saved with the
    // plants (none: they reach past 1600 m) and with the grass models alone.
    for (const bool plants : {true, false}) {
      scrub::Spec spec;
      for (int model = 0; model < 8; ++model) {
        spec.slots.push_back(static_cast<uint32_t>(spec.models.size()));
        spec.models.push_back({.distances = {75.f * 75.f, 150.f * 150.f, 300.f * 300.f}, .enabled = {1, 1, 1}});
      }
      for (const float last : {3031.f, 2543.f, 3276.f}) {
        if (!plants) break;
        spec.slots.push_back(static_cast<uint32_t>(spec.models.size()));
        spec.models.push_back({.distances = {300.f * 300.f, 600.f * 600.f, 900.f * 900.f, last * last}, .enabled = {1, 1, 1, 1}});
      }
      spec.game_cells = 1600;
      spec.game_bias = 200.f;
      spec.camera = {0.f, 60.f, 0.f};
      std::vector<scrub::Record> records;
      for (uint32_t record = 0; record < 60000; ++record) {
        const auto slot = static_cast<uint8_t>((plants && record % 50 == 0) ? 8 + record % 3 : record % 8);
        records.push_back({.position = {uniform(-2000.f, 2000.f), uniform(0.f, 20.f), uniform(-2000.f, 2000.f)},
                           .slot = slot, .thinning = static_cast<uint8_t>(scene_random() % 256), .id = record});
      }
      const scrub::Visits hooked_before = hooked_visits;
      const scrub::Visits reference_before = reference_visits;
      CHECK(run(spec, records, 1.f, 1.f));
      const uint64_t kept = (hooked_visits.records - hooked_before.records) / 2;
      const uint64_t all = (reference_visits.records - reference_before.records) / 2;
      std::printf("5 G1's grass %s: a frame's records past the distance test %llu -> %llu (%.0f%% saved), cells %llu -> %llu\n",
                  plants ? "with its 3 far plants" : "without the far plants", all, kept,
                  100.0 * static_cast<double>(all - kept) / static_cast<double>(std::max<uint64_t>(all, 1)),
                  (reference_visits.cells - reference_before.cells) / 2, (hooked_visits.cells - hooked_before.cells) / 2);
    }
  }
  std::printf("PASS\n");
  return 0;
}
