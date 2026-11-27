/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The model LOD table (model_lods.hpp) without a game, on fake nodes and models: a model's first scaling and the
// values back at 1x, a value the game set again, a changed LOD count, entries past the eighth, distances that must not
// be scaled, two threads on the same models (one scale, then two scales at once: never a model scaled twice), and a
// full stripe with its notice.
// model_lods_test.exe; exit code = failures.

#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include "../model_lods.hpp"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::test;

// A node of `models` fake models with `lods` LOD entries each, in the game's layout.
struct FakeNode {
  FakeNode(size_t models, uint32_t lods) : lods(lods), entries(models * lods), entry_lists(models * lods), models(models), list(models) {
    for (size_t model = 0; model < models; ++model) {
      for (uint32_t lod = 0; lod < lods; ++lod) {
        entry_lists[(model * lods) + lod] = entries[(model * lods) + lod].data();
      }
      SetCount(model, lods);
      const uint8_t* const* model_lods = entry_lists.data() + (model * lods);
      std::memcpy(this->models[model].data() + model_lods::MODEL_LODS, &model_lods, sizeof(model_lods));
      list[model] = this->models[model].data();
    }
    const auto count = static_cast<uint32_t>(models);
    std::memcpy(node.data() + model_lods::NODE_MODEL_COUNT, &count, sizeof(count));
    const uint8_t* const* first = list.data();
    std::memcpy(node.data() + model_lods::NODE_MODELS, &first, sizeof(first));
  }
  void SetCount(size_t model, uint32_t count) {
    std::memcpy(models[model].data() + model_lods::MODEL_LOD_COUNT, &count, sizeof(count));
  }
  void Set(size_t model, uint32_t lod, float distance_squared) {
    std::memcpy(entries[(model * lods) + lod].data() + model_lods::LOD_DISTANCE_SQUARED, &distance_squared, sizeof(float));
  }
  [[nodiscard]] uint32_t Bits(size_t model, uint32_t lod) const {
    uint32_t bits = 0;
    std::memcpy(&bits, entries[(model * lods) + lod].data() + model_lods::LOD_DISTANCE_SQUARED, sizeof(bits));
    return bits;
  }
  [[nodiscard]] bool Holds(size_t model, uint32_t lod, float distance_squared) const {
    return Bits(model, lod) == std::bit_cast<uint32_t>(distance_squared);
  }

  uint32_t lods;
  std::vector<std::array<uint8_t, 0x60>> entries;
  std::vector<uint8_t*> entry_lists;
  std::vector<std::array<uint8_t, 0x50>> models;
  std::vector<const uint8_t*> list;
  std::array<uint8_t, 0x40> node = {};
};

}  // namespace

int main() {
  std::atomic<uint32_t> notices = 0;

  // One model: its first scaling, the same scale again, a value the game set again, back at 1x.
  {
    FakeNode fake(2, 3);
    fake.list[1] = nullptr;
    fake.Set(0, 0, 100.f);
    fake.Set(0, 1, 400.f);
    fake.Set(0, 2, 900.f);
    Expect(model_lods::Scale(fake.node.data(), 0, 4.f, &notices) == fake.list[0] && fake.Holds(0, 0, 400.f)
               && fake.Holds(0, 1, 1600.f) && fake.Holds(0, 2, 3600.f),
           "a model's first scaling: each distance x the scale, the model back");
    Expect(model_lods::Scale(fake.node.data(), 2, 4.f, &notices) == nullptr
               && model_lods::Scale(fake.node.data(), 1, 4.f, &notices) == nullptr,
           "no model at the index: nullptr");
    Expect(model_lods::Scale(fake.node.data(), 0, 4.f, &notices) == fake.list[0] && fake.Holds(0, 0, 400.f)
               && fake.Holds(0, 1, 1600.f) && fake.Holds(0, 2, 3600.f),
           "the same scale again writes nothing new");
    Expect(model_lods::Scale(fake.node.data(), 0, 2.f, &notices) == fake.list[0] && fake.Holds(0, 0, 200.f)
               && fake.Holds(0, 1, 800.f) && fake.Holds(0, 2, 1800.f),
           "another scale: from the game's values, not from the written ones");
    fake.Set(0, 1, 250.f);
    Expect(model_lods::Scale(fake.node.data(), 0, 2.f, &notices) == fake.list[0] && fake.Holds(0, 0, 200.f)
               && fake.Holds(0, 1, 500.f) && fake.Holds(0, 2, 1800.f),
           "a value the game set again is the new base, the others keep theirs");
    Expect(model_lods::Scale(fake.node.data(), 0, 1.f, &notices) == fake.list[0] && fake.Holds(0, 0, 100.f)
               && fake.Holds(0, 1, 250.f) && fake.Holds(0, 2, 900.f),
           "back at 1x: the game's bits");
    // The game builds the model again with another count: every value found then is the game's.
    model_lods::Scale(fake.node.data(), 0, 2.f, &notices);
    fake.SetCount(0, 2);
    fake.Set(0, 0, 200.f);
    fake.Set(0, 1, 50.f);
    Expect(model_lods::Scale(fake.node.data(), 0, 2.f, &notices) == fake.list[0] && fake.Holds(0, 0, 400.f)
               && fake.Holds(0, 1, 100.f) && fake.Holds(0, 2, 1800.f),
           "another LOD count: every value is the game's again, even one equal to the last written");
    Expect(model_lods::Scale(fake.node.data(), 0, 1.f, &notices) == fake.list[0] && fake.Holds(0, 0, 200.f)
               && fake.Holds(0, 1, 50.f),
           "another LOD count: back at 1x");
  }

  // Entries past the eighth keep the game's distances; distances that are no finite range stay as they are.
  {
    FakeNode fake(2, 10);
    for (uint32_t lod = 0; lod < 10; ++lod) {
      fake.Set(0, lod, 10.f + static_cast<float>(lod));
    }
    model_lods::Scale(fake.node.data(), 0, 4.f, &notices);
    bool scaled = true;
    for (uint32_t lod = 0; lod < 10; ++lod) {
      scaled = scaled && fake.Holds(0, lod, (10.f + static_cast<float>(lod)) * ((lod < model_lods::MAX_MODEL_LODS) ? 4.f : 1.f));
    }
    Expect(scaled, "a model with ten LODs: the first eight scaled, the rest the game's");

    fake.SetCount(1, 5);
    const uint32_t nan_bits = 0x7fc12345;
    fake.Set(1, 0, std::numeric_limits<float>::infinity());
    fake.Set(1, 1, std::bit_cast<float>(nan_bits));
    fake.Set(1, 2, FLT_MAX);
    fake.Set(1, 3, 1.0e38f);
    fake.Set(1, 4, 0.f);
    model_lods::Scale(fake.node.data(), 1, 4.f, &notices);
    Expect(fake.Holds(1, 0, std::numeric_limits<float>::infinity()) && fake.Bits(1, 1) == nan_bits && fake.Holds(1, 2, FLT_MAX)
               && fake.Holds(1, 3, FLT_MAX) && fake.Holds(1, 4, 0.f),
           "infinity, a NaN and the largest float stay; a product past the largest float stops there");
    model_lods::Scale(fake.node.data(), 1, 1.f, &notices);
    Expect(fake.Holds(1, 0, std::numeric_limits<float>::infinity()) && fake.Bits(1, 1) == nan_bits && fake.Holds(1, 2, FLT_MAX)
               && fake.Holds(1, 3, 1.0e38f) && fake.Holds(1, 4, 0.f),
           "back at 1x: the clamped distance is the game's again");
  }

  // Two threads on the same models, as the grass cells' draw threads are.
  {
    constexpr size_t MODELS = 256;
    constexpr uint32_t LODS = 4;
    constexpr int PASSES = 200;
    FakeNode fake(MODELS, LODS);
    const auto game = [](size_t model, uint32_t lod) { return static_cast<float>(100 + (model * 4) + lod); };
    for (size_t model = 0; model < MODELS; ++model) {
      for (uint32_t lod = 0; lod < LODS; ++lod) {
        fake.Set(model, lod, game(model, lod));
      }
    }
    // Both threads start each pass together; the second runs the models from the far end.
    const auto run = [&](float first_scale, float second_scale) {
      std::atomic<int> ready = 0;
      const auto pass = [&](float scale, bool reversed) {
        ++ready;
        while (ready.load() < 2) {
        }
        for (int repeat = 0; repeat < PASSES; ++repeat) {
          for (size_t model = 0; model < MODELS; ++model) {
            model_lods::Scale(fake.node.data(), static_cast<uint32_t>(reversed ? (MODELS - 1 - model) : model), scale, &notices);
          }
        }
      };
      std::thread first(pass, first_scale, false);
      std::thread second(pass, second_scale, true);
      first.join();
      second.join();
    };
    const auto every_entry = [&](float first_scale, float second_scale) {
      for (size_t model = 0; model < MODELS; ++model) {
        // One model's entries all hold the same scale: they are written under one lock.
        const float scale = (fake.Holds(model, 0, game(model, 0) * first_scale) ? first_scale : second_scale);
        for (uint32_t lod = 0; lod < LODS; ++lod) {
          if (!fake.Holds(model, lod, game(model, lod) * scale)) return false;
        }
      }
      return true;
    };
    bool same_scale_ok = true;
    for (const float scale : {2.f, 3.f, 1.f, 4.f, 2.f}) {
      run(scale, scale);
      same_scale_ok = same_scale_ok && every_entry(scale, scale);
    }
    Expect(same_scale_ok, "two threads, one scale: every distance is the game's x the scale, pass after pass");
    bool two_scales_ok = true;
    for (int round = 0; round < 8; ++round) {
      run(2.f, 3.f);
      two_scales_ok = two_scales_ok && every_entry(2.f, 3.f);
    }
    Expect(two_scales_ok, "two threads, two scales: every model holds the game's x one of them, never a product of both");
    run(1.f, 1.f);
    Expect(every_entry(1.f, 1.f), "two threads: back at 1x, the game's bits (no written value was ever taken for the game's)");
  }
  Expect(notices.load() == 0, "no notice so far");

  // More models than the table has slots: the models that find their stripe full keep the game's distances and leave
  // the notice; every other one is scaled and comes back at 1x.
  {
    constexpr size_t MODELS = 80000;
    constexpr size_t TABLE_MODELS = model_lods::STRIPES * model_lods::STRIPE_MODELS;
    FakeNode fake(MODELS, 1);
    for (size_t model = 0; model < MODELS; ++model) {
      fake.Set(model, 0, 100.f);
    }
    size_t scaled = 0;
    size_t kept = 0;
    for (size_t model = 0; model < MODELS; ++model) {
      model_lods::Scale(fake.node.data(), static_cast<uint32_t>(model), 4.f, &notices);
      scaled += (fake.Holds(model, 0, 400.f) ? 1 : 0);
      kept += (fake.Holds(model, 0, 100.f) ? 1 : 0);
    }
    Expect(scaled + kept == MODELS && scaled <= TABLE_MODELS && kept >= MODELS - TABLE_MODELS
               && notices.load() == NOTICE_MODEL_TABLE_FULL,
           "a full stripe: its later models keep the game's distances, the notice is left");
    std::printf("  %zu of %zu models scaled, %zu kept the game's distances\n", scaled, MODELS, kept);
    size_t back = 0;
    for (size_t model = 0; model < MODELS; ++model) {
      model_lods::Scale(fake.node.data(), static_cast<uint32_t>(model), 1.f, &notices);
      back += (fake.Holds(model, 0, 100.f) ? 1 : 0);
    }
    Expect(back == MODELS, "a full stripe: back at 1x, every model holds the game's bits");
  }

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
