// Grass Dump (src/games/twattila/graphics_hooks.hpp: DumpGrass, AddGrassDumpRows) against the game's own grass code: a
// synthetic battle's scrub in the game's layouts (quadtree nodes, 18-byte records, the byte grid, the (cos, sin) table,
// scrub models with LODs and parts, the manager and its scene's camera) runs through the hooks (OnScrubUpdate, OnScrubCell,
// OnDrawScrub) into empire.retail.dll's cell loop (+0x1897a30) and DrawScrub (+0x17f9800), read from disk and mapped
// (relocated, never loaded as a DLL), on 4 threads as the game's workers. The parts' draws (vtable +0x10c) are the
// test's: they keep what the game passes (the part's 3x4, the parameters) as the instance rows, put into a dynamic vertex
// buffer on a WARP device and read back by AddGrassDumpRows at two draws a mesh (prefill, G-buffer). Three dumps:
// a: the game's defaults (x87 53-bit, no burn override); b: x87 24-bit on the workers, a burn override, Grass Detail
//    Distance 1.5x and Density 2x (the hook's own written values);
// c: DrawScrub's early-out off (every call reaches the game's DrawScrub: its own LOD pick decides the rows).
// Each: the dump's chunks parse, its counts are the test's (nodes, records, cell calls, DrawScrub calls, the parts' draws,
// rows equal to the game's, the second draw of a mesh not read again; each dump its own tw_grass_dump_<n>.bin), then it is kept as grass_synthetic_<x>.bin next
// to the exe for tools/tw/grass_replay (bit-exact replay of the game's placement). Also the game's code alone: ns a record.
// tests.exe [empire.retail.dll]   x86 only (Attila is 32-bit).
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "src/games/twattila/graphics_hooks.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Grass dump test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }

#if defined(_WIN64)

int main() {
  std::printf("SKIP: Attila is 32-bit; the dump reads the x86 game's layouts.\n");
  return 0;
}

#else

namespace {

namespace hooks = twattila::graphics_hooks;
namespace internal = twattila::graphics_hooks::internal;

constexpr uint32_t PREFERRED_BASE = 0x10000000;
// empire.retail.dll (Attila): the camera getters DrawScrub calls (the scene's vtable +0x28, the view's +0x3c).
constexpr uint32_t GET_VIEW_RVA = 0xc6760;
constexpr uint32_t GET_POSITION_RVA = 0x20c850;
constexpr uint32_t ROW_BYTES = 56;  // the grass VS's TEXCOORD1-4 (VB1 stride)

uint32_t Read32(const uint8_t* at) {
  uint32_t value;
  std::memcpy(&value, at, sizeof(value));
  return value;
}

void Write32(uint8_t* at, uint32_t value) { std::memcpy(at, &value, sizeof(value)); }

template <typename T>
void Put(uint8_t* at, const T& value) { std::memcpy(at, &value, sizeof(value)); }

// The DLL's sections at a new address, relocated (as test/tw_engine_code). Executable and writable (a test).
uint8_t* Map(const std::vector<uint8_t>& file) {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
  auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (base == nullptr) return nullptr;
  std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    const size_t size = std::min<size_t>(section->SizeOfRawData, section->Misc.VirtualSize);
    if (size != 0) std::memcpy(base + section->VirtualAddress, file.data() + section->PointerToRawData, size);
  }
  const uint32_t delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base)) - nt->OptionalHeader.ImageBase;
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  for (uint32_t at = 0; at < directory.Size;) {
    const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(base + directory.VirtualAddress + at);
    if (block->SizeOfBlock == 0) break;
    const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
    const size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
    for (size_t entry = 0; entry < count; ++entry) {
      if ((entries[entry] >> 12) != IMAGE_REL_BASED_HIGHLOW) continue;
      uint8_t* fixed = base + block->VirtualAddress + (entries[entry] & 0xFFF);
      Write32(fixed, Read32(fixed) + delta);
    }
    at += block->SizeOfBlock;
  }
  return base;
}

// A part's draw as the game's DrawScrub calls it (the part's vtable +0x10c, thiscall, 5 arguments): what it was given.
struct PartDraw {
  const uint8_t* part = nullptr;
  uint32_t model = 0;
  std::array<uint8_t, 48> matrix = {};
  std::array<uint8_t, internal::DUMP_PARAMS_BYTES> params = {};
  uint32_t entry_state = 0;
};
std::mutex part_draws_mutex;
std::vector<PartDraw> part_draws;
std::atomic<bool> record_parts = true;  // false: counted only (the game's code timed)
std::atomic<uint32_t> parts_counted = 0;
std::atomic<uint32_t> draw_scrub_calls = 0;  // the cell loop's
std::atomic<uint32_t> game_draw_scrub_calls = 0;  // the hook's calls into the game's DrawScrub
uint8_t* game_draw_scrub = nullptr;

void __fastcall FakePartDraw(const uint8_t* part, void*, uint32_t model, const uint8_t* matrix, const uint8_t* entry,
                             const uint8_t* params, uint32_t) {
  parts_counted.fetch_add(1, std::memory_order_relaxed);
  if (!record_parts.load(std::memory_order_relaxed)) return;
  PartDraw draw = {.part = part, .model = model, .entry_state = Read32(entry)};
  std::memcpy(draw.matrix.data(), matrix, draw.matrix.size());
  std::memcpy(draw.params.data(), params, draw.params.size());
  const std::unique_lock lock(part_draws_mutex);
  part_draws.push_back(draw);
}

// The manager's vtable +0x80 (what the cell loop calls): counted, then the hook.
void __fastcall CountedDrawScrub(uint8_t* manager, void* edx, uint32_t model, const float* transform, uint32_t third,
                                 const uint8_t* params, uint32_t fifth) {
  draw_scrub_calls.fetch_add(1, std::memory_order_relaxed);
  internal::OnDrawScrub(manager, edx, model, transform, third, params, fifth);
}

// The hook's original: counted, then the game's DrawScrub.
void __fastcall CountedGameDrawScrub(uint8_t* manager, void* edx, uint32_t model, const float* transform, uint32_t third,
                                     const uint8_t* params, uint32_t fifth) {
  game_draw_scrub_calls.fetch_add(1, std::memory_order_relaxed);
  reinterpret_cast<internal::DrawScrub>(game_draw_scrub)(manager, edx, model, transform, third, params, fifth);
}

// The synthetic battle, every object in the game's layout (the vtables too: the objects point at them).
struct Battle {
  std::vector<std::unique_ptr<uint8_t[]>> storage;
  uint8_t* scrub = nullptr;
  uint8_t* manager = nullptr;
  uint8_t* scene = nullptr;
  const void** manager_vtable = nullptr;
  std::vector<uint8_t*> nodes;  // with records
  uint64_t records = 0;
  uint32_t node_count = 0;
  uint32_t lods = 0;           // the scrub models' LODs (each model once)
  uint32_t parts_in_lods = 0;  // their parts

  uint8_t* Allocate(size_t size) { return storage.emplace_back(std::make_unique<uint8_t[]>(size)).get(); }
  const void** Vtable(size_t slots) { return reinterpret_cast<const void**>(Allocate(slots * sizeof(void*))); }
};

// The quadtree query (+0x1898800) as ScrubUpdate calls it: the box camera +- float(+0x80); a node out of it is left out
// with its children (comiss: a NaN bound never leaves one out). The frustum part's result (the entry's first dword: 0
// across, 2 inside) made up by the test: the game's camera test isn't the dump's subject.
struct ListedCell {
  const uint8_t* node = nullptr;
  internal::CellEntry entry = {};
};

std::vector<ListedCell> Query(const uint8_t* root, const std::array<float, 3>& low, const std::array<float, 3>& high) {
  std::vector<ListedCell> cells;
  std::vector<const uint8_t*> pending = {root};
  while (!pending.empty()) {
    const uint8_t* node = pending.back();
    pending.pop_back();
    if (node == nullptr) continue;
    bool outside = false;
    for (size_t axis = 0; axis < 3; ++axis) {
      float node_low;
      float node_high;
      std::memcpy(&node_low, node + 4 + (axis * 4), sizeof(float));
      std::memcpy(&node_high, node + 0x10 + (axis * 4), sizeof(float));
      outside |= (node_low > high[axis] || low[axis] > node_high);
    }
    if (outside) continue;
    if (Read32(node + internal::CELL_COUNT) != 0) {
      ListedCell cell = {.node = node};
      cell.entry.values[0] = (reinterpret_cast<uintptr_t>(node) >> 6) % 3 == 0 ? 2u : 0u;
      for (size_t value = 1; value < cell.entry.values.size(); ++value) cell.entry.values[value] = static_cast<uint32_t>(value * 0x01010101u);
      cells.push_back(cell);
    }
    for (size_t child = 0; child < 4; ++child) {
      const uint8_t* next;
      std::memcpy(&next, node + internal::NODE_CHILDREN + (child * 4), sizeof(next));
      pending.push_back(next);
    }
  }
  return cells;
}

// ScrubUpdate's (+0x17fb510) camera, box, radius squared and the cells on `threads` threads, each cell through
// `cell_loop` (OnScrubCell, or the game's code alone for its timing).
const Battle* running = nullptr;
int threads = 4;
internal::ScrubCell cell_loop = nullptr;

void __fastcall FakeScrubUpdate(uint8_t* scrub, void*) {
  std::array<float, 3> camera;
  std::memcpy(camera.data(), running->scene + 0x48 + 0x8, sizeof(camera));
  int32_t cells;
  std::memcpy(&cells, scrub + internal::SCRUB_CELLS, sizeof(cells));
  const float radius = static_cast<float>(cells);
  std::array<float, 3> low;
  std::array<float, 3> high;
  for (size_t axis = 0; axis < 3; ++axis) {
    low[axis] = camera[axis] - radius;
    high[axis] = camera[axis] + radius;
  }
  const uint8_t* quadtree;
  std::memcpy(&quadtree, scrub + internal::SCRUB_QUADTREE, sizeof(quadtree));
  const uint8_t* root;
  std::memcpy(&root, quadtree + internal::QUADTREE_ROOT, sizeof(root));
  const std::vector<ListedCell> list = Query(root, low, high);
  const float radius_squared = static_cast<float>(cells * cells);
  std::atomic<size_t> next = 0;
  const auto work = [&] {
    for (size_t index = next++; index < list.size(); index = next++) {
      cell_loop(scrub, nullptr, list[index].node, camera.data(), list[index].entry, radius_squared, 1);
    }
  };
  std::vector<std::thread> workers;
  for (int thread = 1; thread < threads; ++thread) workers.emplace_back(work);
  work();
  for (auto& worker : workers) worker.join();
}

// x87 24-bit on the calling thread (variant b's workers: the cell loop's grid index rounds there).
std::atomic<bool> single_precision = false;
void __fastcall PrecisionScrubCell(uint8_t* scrub, void* edx, const uint8_t* cell, const float* camera, internal::CellEntry entry,
                                   float radius_squared, uint32_t last) {
  unsigned int control = 0;
  if (single_precision.load()) __control87_2(_PC_24, _MCW_PC, &control, nullptr);
  internal::OnScrubCell(scrub, edx, cell, camera, entry, radius_squared, last);
  if (single_precision.load()) __control87_2(_PC_53, _MCW_PC, &control, nullptr);
}

Battle Build(uint8_t* game, uint32_t seed) {
  Battle battle;
  std::mt19937 random(seed);
  const auto uniform = [&](float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); };
  battle.manager = battle.Allocate(internal::DUMP_MANAGER_BYTES);
  battle.scene = battle.Allocate(0x60);
  battle.scrub = battle.Allocate(internal::DUMP_SCRUB_BYTES);
  battle.manager_vtable = battle.Vtable(64);
  const void** scene_vtable = battle.Vtable(16);
  const void** view_vtable = battle.Vtable(16);
  const void** part_vtable = battle.Vtable(0x50);
  battle.manager_vtable[0x80 / 4] = reinterpret_cast<const void*>(&CountedDrawScrub);
  scene_vtable[internal::GET_VIEW_SLOT] = game + GET_VIEW_RVA;
  view_vtable[internal::GET_POSITION_SLOT] = game + GET_POSITION_RVA;
  part_vtable[internal::PART_DRAW_SLOT] = reinterpret_cast<const void*>(&FakePartDraw);
  Put(battle.manager, battle.manager_vtable);
  Put(battle.scene, scene_vtable);
  Put(battle.scene + 0x48, view_vtable);
  Put(battle.manager + internal::MANAGER_SCENE, battle.scene);
  Put(battle.manager + internal::MANAGER_LOD_OFFSET, -4.f);
  // Models: grass clumps (LODs to 30, 60, 100 m), a plant to 320 m, one with a disabled middle LOD; parts with bound centres.
  const std::vector<std::vector<std::pair<float, uint8_t>>> model_lods = {
      {{900.f, 1}, {3600.f, 1}, {10000.f, 1}},
      {{1600.f, 1}, {6400.f, 1}},
      {{102400.f, 1}},
      {{2500.f, 1}, {8100.f, 0}, {14400.f, 1}},
      {{400.f, 1}, {4900.f, 1}},
  };
  std::vector<uint8_t*> models;
  for (size_t index = 0; index < model_lods.size(); ++index) {
    uint8_t* model = battle.Allocate(internal::DUMP_MODEL_BYTES);
    auto* lods = reinterpret_cast<uint8_t**>(battle.Allocate(model_lods[index].size() * 4));
    const uint32_t parts = 1 + static_cast<uint32_t>(index % 3);
    battle.lods += static_cast<uint32_t>(model_lods[index].size());
    for (size_t lod_index = 0; lod_index < model_lods[index].size(); ++lod_index) {
      uint8_t* lod = battle.Allocate(internal::DUMP_LOD_BYTES);
      Put(lod + 0x38, model_lods[index][lod_index].first);
      lod[0x44] = model_lods[index][lod_index].second;
      auto* part_list = reinterpret_cast<uint8_t**>(battle.Allocate(parts * 4));
      for (uint32_t part_index = 0; part_index < parts; ++part_index) {
        uint8_t* part = battle.Allocate(internal::DUMP_PART_BYTES);
        const std::array<float, 3> centre = {uniform(-0.3f, 0.3f), uniform(0.f, 0.8f), uniform(-0.3f, 0.3f)};
        Put(part, part_vtable);
        Put(part + 0xe8, centre);
        part_list[part_index] = part;
      }
      Put(lod + internal::LOD_PART_COUNT, parts);
      Put(lod + internal::LOD_PARTS, part_list);
      lods[lod_index] = lod;
      battle.parts_in_lods += parts;
    }
    Put(model + 0x28, static_cast<uint32_t>(model_lods[index].size()));
    Put(model + 0x2c, lods);
    models.push_back(model);
  }
  // The manager's list: the scrub models among others (index 0 and 2 not scrub models).
  auto* manager_models = reinterpret_cast<uint8_t**>(battle.Allocate(8 * 4));
  const std::array<uint32_t, 8> placement = {UINT32_MAX, 0, UINT32_MAX, 1, 2, 3, 4, 0};
  for (size_t index = 0; index < placement.size(); ++index) {
    manager_models[index] = (placement[index] == UINT32_MAX ? battle.Allocate(internal::DUMP_MODEL_BYTES) : models[placement[index]]);
  }
  Put(battle.manager + 0x1c, uint32_t{8});
  Put(battle.manager + 0x20, manager_models);
  const std::array<uint32_t, 6> slots = {1, 3, 4, 5, 6, 7};  // slot 5: model 0 again
  auto* slot_list = reinterpret_cast<uint32_t*>(battle.Allocate(sizeof(slots)));
  std::memcpy(slot_list, slots.data(), sizeof(slots));
  Put(battle.scrub + internal::SCRUB_CELLS, int32_t{200});
  Put(battle.scrub + internal::SCRUB_BIAS, 200.f);
  Put(battle.scrub + internal::SCRUB_MANAGER, battle.manager);
  Put(battle.scrub + internal::SCRUB_MODEL_COUNT, static_cast<uint32_t>(slots.size()));
  Put(battle.scrub + internal::SCRUB_MODELS, slot_list);
  // The (cos, sin) table and the byte grid (10 m a cell, 256 x 256 around the map's centre, roads of 0; row and column
  // 127 too, just below 0 m: a record at a tiny negative coordinate is there only by floor, not by the game's x87 index).
  auto* table = reinterpret_cast<float*>(battle.Allocate(internal::DUMP_TABLE_BYTES));
  for (size_t entry = 0; entry < 256; ++entry) {
    const double angle = static_cast<double>(entry) * 6.283185307179586 / 256.0;
    table[entry * 2] = static_cast<float>(std::cos(angle));
    table[entry * 2 + 1] = static_cast<float>(std::sin(angle));
  }
  Put(battle.scrub + internal::SCRUB_TABLE, table);
  uint8_t* grid = battle.Allocate(65536);
  for (uint32_t row = 0; row < 256; ++row) {
    for (uint32_t column = 0; column < 256; ++column) {
      grid[(row << 8) + column] = (row % 37 == 5 || column % 53 == 7 || row == 127 || column == 127) ? 0 : 1;
    }
  }
  Put(battle.scrub + internal::SCRUB_GRID, grid);
  Put(battle.scrub + internal::SCRUB_GRID_SHIFT, uint32_t{8});
  Put(battle.scrub + 0xb0, uint32_t{128});
  Put(battle.scrub + internal::SCRUB_GRID_CLAMP, uint32_t{255});
  Put(battle.scrub + 0xc4, 0.1f);
  // The quadtree: 1200 m square, 5 levels below the root (37.5 m leaves), ~0.9 records a square metre, some cells empty;
  // records near the grid's and the radius's edges and at tiny negative coordinates.
  uint8_t* quadtree = battle.Allocate(0x20);
  Put(battle.scrub + internal::SCRUB_QUADTREE, quadtree);
  const auto build = [&](const auto& self, float x0, float z0, float size, int level) -> uint8_t* {
    uint8_t* node = battle.Allocate(internal::NODE_BYTES);
    ++battle.node_count;
    std::array<float, 3> low = {x0, 0.f, z0};
    std::array<float, 3> high = {x0 + size, 0.f, z0 + size};
    if (level == 0) {
      const auto count = static_cast<uint32_t>((uniform(0.f, 1.f) < 0.08f) ? 0 : uniform(600.f, 1900.f));
      uint8_t* records = battle.Allocate(size_t{count} * internal::RECORD_SIZE + 1);
      for (uint32_t index = 0; index < count; ++index) {
        uint8_t* record = records + (index * internal::RECORD_SIZE);
        std::array<float, 3> position = {uniform(x0, x0 + size), uniform(-2.f, 30.f), uniform(z0, z0 + size)};
        const uint32_t kind = index % 97;
        if (kind == 3) position[0] = std::floor(position[0] / 10.f) * 10.f;  // on a grid line
        if (kind == 5) position[2] = std::floor(position[2] / 10.f) * 10.f;
        if (kind == 7 && x0 <= 0.f && x0 + size >= 0.f) position[0] = -1e-9f * uniform(0.1f, 2.f);  // tiny negative
        if (kind == 11 && z0 <= 0.f && z0 + size >= 0.f) position[2] = -1e-30f;
        Put(record, position);
        record[0xc] = static_cast<uint8_t>(random());
        record[0xd] = static_cast<uint8_t>(random());
        record[0xe] = static_cast<uint8_t>(random());
        record[0xf] = static_cast<uint8_t>(random());
        record[0x10] = static_cast<uint8_t>(random() % slots.size());
        record[0x11] = static_cast<uint8_t>(random());
        for (size_t axis = 0; axis < 3; ++axis) {
          low[axis] = std::min(low[axis], position[axis]);
          high[axis] = std::max(high[axis], position[axis]);
        }
      }
      Put(node + internal::CELL_COUNT, count);
      Put(node + internal::CELL_RECORDS, records);
      battle.records += count;
      if (count != 0) battle.nodes.push_back(node);
    } else {
      const float half = size * 0.5f;
      for (size_t child = 0; child < 4; ++child) {
        uint8_t* next = self(self, x0 + half * static_cast<float>(child % 2), z0 + half * static_cast<float>(child / 2), half, level - 1);
        Put(node + internal::NODE_CHILDREN + (child * 4), next);
        for (size_t axis = 0; axis < 3; ++axis) {
          float child_low;
          float child_high;
          std::memcpy(&child_low, next + 4 + (axis * 4), sizeof(float));
          std::memcpy(&child_high, next + 0x10 + (axis * 4), sizeof(float));
          low[axis] = std::min(low[axis], child_low);
          high[axis] = std::max(high[axis], child_high);
        }
      }
    }
    Put(node + 4, low);
    Put(node + 0x10, high);
    return node;
  };
  Put(quadtree + internal::QUADTREE_ROOT, build(build, -600.f, -600.f, 1200.f, 5));
  return battle;
}

// The dump's chunks: their count by tag, and the fields the test checks.
struct Parsed {
  bool valid = false;
  std::map<std::string, uint32_t> tags;
  uint64_t node_records = 0;
  uint32_t picked = 0;     // DRAW chunks whose LOD the hook found
  uint32_t row_bytes = 0;  // ROWS chunks' rows
  uint32_t rows_read_before = 0;
  std::vector<uint8_t> rows;
  std::string last;
};

Parsed Parse(const std::filesystem::path& path) {
  Parsed parsed;
  std::ifstream stream(path, std::ios::binary);
  const std::vector<uint8_t> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (file.size() < 8 || std::memcmp(file.data(), "TWGD", 4) != 0 || Read32(file.data() + 4) != internal::DUMP_VERSION) return parsed;
  size_t at = 8;
  while (at + 8 <= file.size()) {
    const std::string tag(reinterpret_cast<const char*>(file.data() + at), 4);
    const uint32_t size = Read32(file.data() + at + 4);
    if (at + 8 + size > file.size()) return parsed;
    const uint8_t* body = file.data() + at + 8;
    ++parsed.tags[tag];
    if (tag == "NODE") parsed.node_records += Read32(body + 8 + 4 + internal::NODE_BYTES + 8);
    if (tag == "DRAW") {
      int32_t picked;
      std::memcpy(&picked, body + 8 + 4 + 48 + 4 + internal::DUMP_PARAMS_BYTES + 4 + 12, sizeof(picked));
      parsed.picked += (picked >= 0 ? 1 : 0);
    }
    if (tag == "ROWS") {
      // 9 draw fields, the mesh's buffer, offset, stride, the instances' buffer, offset, stride, size, the index
      // buffer, format, offset, the VS b0's first 4 floats, then "read before" and the rows' size.
      constexpr size_t READ_BEFORE = (9 * 4) + 8 + 4 + 4 + 8 + 4 + 4 + 4 + 8 + 4 + 4 + 16;
      const uint32_t bytes = Read32(body + READ_BEFORE + 4);
      parsed.rows_read_before += Read32(body + READ_BEFORE);
      parsed.row_bytes += bytes;
      parsed.rows.insert(parsed.rows.end(), body + READ_BEFORE + 8, body + READ_BEFORE + 8 + bytes);
    }
    parsed.last = tag;
    at += 8 + size;
  }
  parsed.valid = (at == file.size());
  return parsed;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const char* path = (argc > 1 ? argv[1] : R"(C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll)");
  std::ifstream stream(path, std::ios::binary);
  CHECK(stream.good());
  const std::vector<uint8_t> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data())->e_lfanew);
  CHECK(nt->OptionalHeader.ImageBase == PREFERRED_BASE);
  const auto* text = IMAGE_FIRST_SECTION(nt);
  uint8_t* game = Map(file);
  CHECK(game != nullptr);
  const std::string_view code(reinterpret_cast<const char*>(game + text->VirtualAddress), text->Misc.VirtualSize);
  auto* cell_code = tw::engine_tweaks::FindPattern({code}, internal::SCRUB_CELL);
  auto* draw_code = tw::engine_tweaks::FindPattern({code}, internal::DRAW_SCRUB);
  CHECK(cell_code != nullptr && draw_code != nullptr);
  CHECK(cell_code - game == 0x1897a30 && draw_code - game == 0x17f9800);
  CHECK(std::memcmp(game + GET_VIEW_RVA, internal::GET_VIEW_CODE.data(), internal::GET_VIEW_CODE.size()) == 0);
  CHECK(std::memcmp(game + GET_POSITION_RVA, internal::GET_POSITION_CODE.data(), internal::GET_POSITION_CODE.size()) == 0);
  std::printf("empire.retail.dll build %08lX: the cell loop +0x%X, DrawScrub +0x%X\n", nt->FileHeader.TimeDateStamp,
              static_cast<unsigned>(cell_code - game), static_cast<unsigned>(draw_code - game));
  game_draw_scrub = draw_code;
  internal::original_scrub_cell = reinterpret_cast<internal::ScrubCell>(cell_code);
  internal::original_draw_scrub = &CountedGameDrawScrub;
  internal::original_scrub_update = &FakeScrubUpdate;
  internal::cell_loop_code = cell_code;
  internal::draw_scrub_code = draw_code;
  internal::hooks_active = true;
  // The tweaks the cell loop reads (their objects' +0x48, the addresses in its code; set by the game's start-up, not run).
  uint8_t* always_render = nullptr;
  uint8_t* burn_override = nullptr;
  std::memcpy(&always_render, cell_code + 0x44, sizeof(always_render));
  std::memcpy(&burn_override, cell_code + 0xe6, sizeof(burn_override));
  CHECK(cell_code[0x43] == 0xB9 && cell_code[0xe5] == 0xB9);
  always_render[0x48] = 0;

  struct Variant {
    char name;
    uint32_t seed;
    std::array<float, 3> camera;
    bool single_precision;
    float burn_override;
    float grass_distance;
    float grass_density;
    bool early_out;
  };
  uint32_t dumps = 0;
  for (const Variant& variant : {Variant{'a', 1113, {37.3f, 12.f, -55.7f}, false, -1.f, 1.f, 1.f, true},
                                 Variant{'b', 2026, {-140.25f, 4.5f, 81.f}, true, 0.5f, 1.5f, 2.f, true},
                                 Variant{'c', 77, {0.5f, 20.f, 0.25f}, false, -1.f, 1.f, 1.f, false}}) {
    Battle battle = Build(game, variant.seed);
    running = &battle;
    Put(battle.scene + 0x48 + 0x8, variant.camera);
    Put(burn_override + 0x48, variant.burn_override);
    internal::draw_scrub_read = variant.early_out;
    internal::grass_scale = variant.grass_distance;
    internal::grass_density = variant.grass_density;
    single_precision = variant.single_precision;
    internal::scrub_values = {};
    // The game's code alone (no hooks, the parts' draws only counted): the cell loop and DrawScrub over the frame's cells
    // on one thread, the game's 200 m and bias.
    if (variant.name == 'a') {
      threads = 1;
      cell_loop = internal::original_scrub_cell;
      battle.manager_vtable[0x80 / 4] = draw_code;
      record_parts = false;
      parts_counted = 0;
      FakeScrubUpdate(battle.scrub, nullptr);
      constexpr int ROUNDS = 10;
      const auto start = std::chrono::steady_clock::now();
      for (int round = 0; round < ROUNDS; ++round) FakeScrubUpdate(battle.scrub, nullptr);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / ROUNDS;
      std::printf("a: the game's cell loop and DrawScrub alone, one thread: %.3f ms a frame, %u parts drawn\n", ms,
                  parts_counted.load() / (ROUNDS + 1));
      battle.manager_vtable[0x80 / 4] = reinterpret_cast<const void*>(&CountedDrawScrub);
      record_parts = true;
    }
    threads = 4;
    cell_loop = &PrecisionScrubCell;
    // Frame 0 without the dump; then the dump: timed, the scene and its calls, two frames of rows.
    internal::OnScrubUpdate(battle.scrub, nullptr);
    part_draws.clear();
    draw_scrub_calls = 0;
    hooks::DumpGrass();
    CHECK(internal::grass_dump.state == internal::DUMP_TIMING);
    internal::OnScrubUpdate(battle.scrub, nullptr);
    CHECK(internal::grass_dump.state == internal::DUMP_SCENE);
    part_draws.clear();
    draw_scrub_calls = 0;
    game_draw_scrub_calls = 0;
    internal::OnScrubUpdate(battle.scrub, nullptr);
    CHECK(internal::grass_dump.state == internal::DUMP_ROWS);
    const uint32_t calls = draw_scrub_calls.load();
    const uint32_t game_calls = game_draw_scrub_calls.load();
    const std::vector<PartDraw> drawn = part_draws;
    // The rows: one mesh a (model, LOD's part), its rows in one dynamic buffer (the game's VB1) in the game's order.
    std::map<const uint8_t*, std::vector<const PartDraw*>> meshes;
    for (const PartDraw& draw : drawn) meshes[draw.part].push_back(&draw);
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                      device.GetAddressOf(), nullptr, context.GetAddressOf())));
    // A row: the part's 3x4 as DrawScrub passes it, then (the test's choice for TEXCOORD4) the burn (+0x8) and the bias (+0x10).
    std::vector<uint8_t> rows;
    for (const auto& [part, draws] : meshes) {
      for (const PartDraw* draw : draws) {
        rows.insert(rows.end(), draw->matrix.begin(), draw->matrix.end());
        rows.insert(rows.end(), draw->params.begin() + 0x8, draw->params.begin() + 0xc);
        rows.insert(rows.end(), draw->params.begin() + 0x10, draw->params.begin() + 0x14);
      }
    }
    const D3D11_BUFFER_DESC rows_desc = {.ByteWidth = static_cast<UINT>(std::max<size_t>(rows.size(), ROW_BYTES)), .Usage = D3D11_USAGE_DYNAMIC,
                                         .BindFlags = D3D11_BIND_VERTEX_BUFFER, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    Microsoft::WRL::ComPtr<ID3D11Buffer> instance_buffer;
    CHECK(SUCCEEDED(device->CreateBuffer(&rows_desc, nullptr, instance_buffer.GetAddressOf())));
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHECK(SUCCEEDED(context->Map(instance_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
    std::memcpy(mapped.pData, rows.data(), rows.size());
    context->Unmap(instance_buffer.Get(), 0);
    const D3D11_BUFFER_DESC mesh_desc = {.ByteWidth = 28 * 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
    const D3D11_BUFFER_DESC index_desc = {.ByteWidth = 12, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_INDEX_BUFFER};
    Microsoft::WRL::ComPtr<ID3D11Buffer> mesh_buffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
    CHECK(SUCCEEDED(device->CreateBuffer(&mesh_desc, nullptr, mesh_buffer.GetAddressOf())));
    CHECK(SUCCEEDED(device->CreateBuffer(&index_desc, nullptr, index_buffer.GetAddressOf())));
    for (const uint32_t pass : {0u, 1u}) {  // prefill, G-buffer: the same rows
      uint32_t first_instance = 0;
      for (const auto& [part, draws] : meshes) {
        const std::array<ID3D11Buffer*, 2> buffers = {mesh_buffer.Get(), instance_buffer.Get()};
        const std::array<UINT, 2> strides = {28, ROW_BYTES};
        const std::array<UINT, 2> offsets = {0, 0};
        context->IASetVertexBuffers(0, 2, buffers.data(), strides.data(), offsets.data());
        context->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        hooks::AddGrassDumpRows(context.Get(), {.vertex_shader = (pass == 0 ? 0xF3741248u : 0x01FA9CDEu), .indexed = true, .count = 6,
                                                .instances = static_cast<uint32_t>(draws.size()), .first_instance = first_instance});
        first_instance += static_cast<uint32_t>(draws.size());
      }
    }
    internal::OnScrubUpdate(battle.scrub, nullptr);
    CHECK(internal::grass_dump.state == internal::DUMP_ROWS_NEXT);
    internal::OnScrubUpdate(battle.scrub, nullptr);
    CHECK(internal::grass_dump.state == internal::DUMP_IDLE);

    const auto dump_path = std::filesystem::path(argv[0]).parent_path() / std::format("tw_grass_dump_{}.bin", ++dumps);
    const Parsed parsed = Parse(dump_path);
    CHECK(parsed.valid);
    CHECK(parsed.last == "END_");
    CHECK(parsed.tags.at("HEAD") == 1 && parsed.tags.at("SCRB") == 1 && parsed.tags.at("MNGR") == 1 && parsed.tags.at("CODE") == 2);
    CHECK(parsed.tags.at("CVAL") == internal::CODE_VALUES.size() && parsed.tags.at("TABL") == 1 && parsed.tags.at("GRID") == 1);
    CHECK(parsed.tags.at("MODL") == 6 && parsed.tags.at("LOD_") == battle.lods && parsed.tags.at("PART") == battle.parts_in_lods);
    CHECK(parsed.tags.at("NODE") == battle.node_count && parsed.node_records == battle.records);
    CHECK(parsed.tags.at("CALL") == internal::grass_dump.cells && parsed.tags.at("DRAW") == calls);
    CHECK(internal::grass_dump.draws == calls && calls > 1000);
    CHECK(parsed.tags.at("ROWS") == 2 * meshes.size());
    CHECK(parsed.rows_read_before == meshes.size());
    CHECK(parsed.row_bytes == rows.size() && parsed.rows == rows);
    CHECK(!internal::grass_dump.truncated);
    // With the early-out the game's DrawScrub ran for the calls the hook found a LOD for and no other (each drew parts:
    // its pick is the game's); without it (c) every call reached the game and the hook judged none.
    if (variant.early_out) {
      CHECK(parsed.picked > 0 && parsed.picked < calls && game_calls == parsed.picked);
    } else {
      CHECK(parsed.picked == 0 && game_calls == calls);
    }
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const auto kept = dump_path.parent_path() / std::format("grass_synthetic_{}.bin", variant.name);
    std::filesystem::remove(kept);
    std::filesystem::rename(dump_path, kept);
    std::printf("%c: %u nodes, %llu records, %u cell calls, %u DrawScrub calls (%u to the game's), %zu parts drawn in %zu meshes, "
                "update %.2f ms through the hooks (4 threads) -> %s\n",
                variant.name, battle.node_count, static_cast<unsigned long long>(battle.records), internal::grass_dump.cells, calls,
                game_calls, drawn.size(), meshes.size(),
                static_cast<double>(internal::grass_dump.clean_ticks) * 1000.0 / static_cast<double>(frequency.QuadPart),
                kept.string().c_str());
  }
  // Asked out of a battle: given up after DUMP_PRESENTS presents without a grass update.
  hooks::DumpGrass();
  bool installed = true;
  renodx::utils::settings::Setting status;
  for (uint32_t present = 0; present <= internal::DUMP_PRESENTS; ++present) hooks::Update({}, &installed, &status);
  CHECK(internal::grass_dump.state == internal::DUMP_IDLE);
  std::printf("PASS\n");
  return 0;
}

#endif
