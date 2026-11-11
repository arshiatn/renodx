/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/data.hpp"
#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "./engine_memory.hpp"

// Particle counts per kind of effect (game settings, Experimental). An effect is a tree of sub-effects (a fire: flames,
// smoke, embers), each with its own particle params (the game's type info: fCount +8, sTexture +0xa0, sMaterial
// +0xc0). Each frame every particle container spawns by its params' count x its main emitter's count scale (container
// spawn, CONTAINER_SPAWN_CODE: container = [first argument], its params +8, its main emitter +0x100; the emitter's top
// effect +0x60, its GetName at vtable +0x30, as the emitter's own GetName +0xf4b90). The hook sets each params' count to
// the game's x the factor of its kind, from its texture and material (else its top effect's name): smoke in a fire
// follows the smoke option. Running effects change at once.
// Lights: each particle of params with a light source adds one (PARTICLE_LIGHT_CODE, called per particle of a
// container with lights, particle +0x80 = its container). Only the game's share of a container's particles keeps its
// light, the same particles every frame: the game's light count and brightness (more fire particles made lights blink,
// ATN v74 above 2-4x; v109's option for every particle's light crashed the game, ATN: removed in v110).
// The light limit (v109, ATN: chapter 5 drops 20-40 lights; v76-v78 also raised each tile's, 20-30% fps): the tiled
// lighting takes at most 255 lights per frame and skips the rest, others each frame (they blink). The light list
// (LIGHT_LIST_CODE) fills two static arrays (cull info 255 x 0x50 B, its end a zeroed entry, CULL_INFO_CODE; shade info
// 255 x 0x100 B), clears and uploads them whole and stops at 255 (LIGHT_LIST_SITES, all in the light list function; the
// lights it skipped at [list + 0xe8]); the tiled shading object's GPU buffers (+8, +0x28; count at +0x18) hold 255.
// Raised: the sites point at MOST_LIGHTS-entry arrays near the exe and take the limit, the buffers are made again by the
// game's Create (BUFFER_CREATE_CODE) and the lighting cs is the addon's (tiled_lights_cs.h: up to MOST_LIGHTS lights,
// each 8x8-pixel tile the game's 255; swapped in by the addon while raised). Transparent objects' per-tile list stays
// 8-bit: the first 255 lights.
// The counting hooks count only while the overlay shows the counts (KeepCounting).
// The game's two particle limits, both raised (ATN v78: torches vanish and come back at 8x):
// - Live particles: one 256-byte block each from a pool (e_ParticlesPoolSize, 12288 KB: 49152 blocks), a lock-free
//   free list (SLIST_HEADER first; free pushes, the particle allocation PARTICLE_ALLOC_CODE pops, null when empty: no
//   particle). The Particle memory option (SetParticleMemory) adds the game's pool size until the pool holds that many
//   times it (v82; v79 grew it each time it was empty, v80 16x at once). Added blocks stay until the game exits.
// - Particle vertices per frame (r_ParticleVerticePoolSize, 15360, 30720 at the game's settings: the renderer's 3
//   vertex buffers of 44-byte vertices with 16-bit indices, made after the device): an effect part that does not fit
//   is not drawn that frame (VERTEX_ALLOC_CODE returns false). Raised to MOST_PARTICLE_VERTICES as the renderer makes
//   the buffers (PARTICLE_BUFFERS_CODE, its render thread init: the game applies its settings after the device). The
//   overlay shows the most used of the frame's buffer (the renderer's bytes used per buffer) and the vertices of the
//   parts that did not fit.
namespace rysesonofrome::particle_counts {

enum Kind : uint8_t { FIRE, SPARKS, SMOKE, BLOOD, OTHER, KIND_COUNT };

namespace internal {

// mov r11, rsp; push rbp, rbx, rdi, r12; ...; mov rax, [rcx]; ...; mov rbx, [rax + 8]; mov rax, [rax + 0x100]; mov rdi,
// rcx; movss xmm11, [rax + 0x80]; comiss xmm11, [rcx + 0x18]
constexpr std::string_view CONTAINER_SPAWN_CODE =
    "4C 8B DC 55 53 57 41 54 48 8D 6C 24 88 48 81 EC 78 01 00 00 48 8B 01 45 0F 29 9B 68 FF FF FF 4C 8B E2 48 8B 58 08 "
    "48 8B 80 00 01 00 00 48 8B F9 F3 44 0F 10 98 80 00 00 00 44 0F 2F 59 18";
// mov rax, rsp; ...; mov rbx, rcx; mov r12, rdx; ...; mov rax, [rcx + 0x80]; mov rcx, [gEnv renderer]; mov rdi, [rax + 8]
constexpr std::string_view PARTICLE_LIGHT_CODE =
    "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 48 8D A8 D8 FD FF FF 48 81 EC 10 03 00 00 0F 29 70 D8 "
    "0F 29 78 C8 44 0F 29 40 B8 48 8B D9 4C 8B E2 44 0F 29 48 A8 44 0F 29 50 98 44 0F 29 58 88 44 0F 29 A0 78 FF FF FF "
    "48 8B 81 80 00 00 00 48 8B 0D ?? ?? ?? ?? 48 8B 78 08";
// The tiled lighting's light list (this = the tiled shading object, renderer +0xd550): mov rax, rsp; ...; sub rsp, 0x540
constexpr std::string_view LIGHT_LIST_CODE =
    "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 48 89 48 08 55 41 54 41 55 41 56 41 57 48 81 EC 40 05 00 00 0F 29 70 C8 "
    "0F 29 78 B8";
// lea rdi, [rip + cull info array]; cmp r12d, 0xff (the lights taken)
constexpr std::string_view CULL_INFO_CODE = "48 8D 3D ?? ?? ?? ?? 41 81 FC FF 00 00 00";
constexpr size_t LIGHT_LIST_SKIPPED = 0xe8;
constexpr uint32_t MAX_LIGHTS = 255;
constexpr size_t CULL_INFO_SIZE = 0x50;  // volume type +0 (0: the end)
constexpr uint32_t MOST_LIGHTS = 4096;  // the raised arrays; tiled_lights_cs.h takes as many
constexpr size_t SHADE_INFO_SIZE = 0x100;
// The game's GPU buffer Create(buffer, count, stride, format, flags, data): mov [rsp + 8], rbx; ...; mov rbx, rcx; mov
// rcx, [rcx + 8]; xor esi, esi; mov r13d, r9d; mov r14d, r8d; mov edi, edx
constexpr std::string_view BUFFER_CREATE_CODE =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 8B D9 48 8B 49 08 33 F6 45 8B "
    "E9 45 8B F0 8B FA";
constexpr size_t BUFFER_COUNT = 0x18;
constexpr uint32_t LIGHT_BUFFER_FLAGS = 7;  // as the game makes them
struct LightBuffer {
  size_t offset;  // in the tiled shading object
  uint32_t stride;
};
constexpr std::array<LightBuffer, 2> LIGHT_BUFFERS = {{{.offset = 0x8, .stride = CULL_INFO_SIZE}, {.offset = 0x28, .stride = SHADE_INFO_SIZE}}};
// The light list function's code that sizes, clears, fills and uploads the arrays: a 32-bit operand at `operand`.
enum class SiteKind : uint8_t {
  CULL_ARRAY,   // lea reg, [rip + cull info (+ field)]
  SHADE_ARRAY,  // lea reg, [rip + shade info (+ field)]
  CULL_BYTES,   // mov r8d, 255 x 0x50 (memset, upload)
  SHADE_BYTES,  // mov r8d, 255 x 0x100
  LIMIT,        // cmp r12d / eax, 255
};
struct LightListSite {
  size_t offset;   // from the function's start
  size_t operand;  // from the instruction's start
  SiteKind kind;
};
constexpr std::array<LightListSite, 19> LIGHT_LIST_SITES = {{
    {.offset = 0xc7, .operand = 2, .kind = SiteKind::CULL_BYTES},
    {.offset = 0x1c5, .operand = 3, .kind = SiteKind::CULL_ARRAY},
    {.offset = 0x249, .operand = 3, .kind = SiteKind::SHADE_ARRAY},
    {.offset = 0x252, .operand = 2, .kind = SiteKind::SHADE_BYTES},
    {.offset = 0x323, .operand = 3, .kind = SiteKind::CULL_ARRAY},
    {.offset = 0x34a, .operand = 3, .kind = SiteKind::SHADE_ARRAY},
    {.offset = 0x372, .operand = 3, .kind = SiteKind::LIMIT},
    {.offset = 0x23c5, .operand = 1, .kind = SiteKind::LIMIT},  // a shadow-casting light's entries
    {.offset = 0x2506, .operand = 3, .kind = SiteKind::CULL_ARRAY},
    {.offset = 0x2514, .operand = 3, .kind = SiteKind::SHADE_ARRAY},
    {.offset = 0x2d70, .operand = 3, .kind = SiteKind::CULL_ARRAY},
    {.offset = 0x2d87, .operand = 3, .kind = SiteKind::SHADE_ARRAY},
    {.offset = 0x2fdd, .operand = 3, .kind = SiteKind::CULL_ARRAY},
    {.offset = 0x2fe4, .operand = 3, .kind = SiteKind::LIMIT},  // the end entry
    {.offset = 0x300a, .operand = 3, .kind = SiteKind::SHADE_ARRAY},
    {.offset = 0x3024, .operand = 3, .kind = SiteKind::SHADE_ARRAY},
    {.offset = 0x304c, .operand = 3, .kind = SiteKind::LIMIT},  // the sun, last
    {.offset = 0x31cd, .operand = 2, .kind = SiteKind::CULL_BYTES},
    {.offset = 0x31df, .operand = 2, .kind = SiteKind::SHADE_BYTES},
}};
// The particle allocation (container, spawner) -> particle or null: mov [rsp + 8], rbx; ...; inc dword [rcx + 0x98];
// mov rsi, rdx; mov rdi, rcx; call the pool's getter
constexpr std::string_view PARTICLE_ALLOC_CODE =
    "48 89 5C 24 08 48 89 74 24 10 57 48 81 EC B0 00 00 00 FF 81 98 00 00 00 48 8B F2 48 8B F9 E8";
// In the pool's getter: lea rbx, [rip + pool]; ...; mov r8d, 0x100 (the block size); ...; movsxd rdx, [cvars + 8]
// (e_ParticlesPoolSize)
constexpr std::string_view PARTICLE_POOL_CODE =
    "48 8D 1D ?? ?? ?? ?? 75 ?? 33 C0 48 8B CB 48 89 05 ?? ?? ?? ?? 89 05 ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 4C 8B 1D ?? ?? ?? "
    "?? 41 B8 00 01 00 00 48 8B CB 49 63 53 08";
constexpr size_t POOL_BLOCK_SIZE = 0x18;
constexpr size_t POOL_BLOCKS = 0x1c;
constexpr uint32_t MOST_POOL_GROWTHS = 15;  // up to 16x the game's live particles
// The particle vertex allocation (render vertices, frame, vertices, indices) -> fits: ...; mov rdi, [rip + renderer];
// ...; mov r13d, [rdi + 0x5d48] (the vertex buffer's bytes); mov r14d, [rdi + 0x5d4c] (the index buffer's)
constexpr std::string_view VERTEX_ALLOC_CODE =
    "48 89 5C 24 10 44 89 44 24 18 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 40 48 8B 3D ?? ?? ?? ?? 44 8B "
    "D2 B8 56 55 55 55 44 8B AF 48 5D 00 00 44 8B B7 4C 5D 00 00";
constexpr size_t VERTEX_ALLOC_RENDERER = 0x1e;  // mov rdi, [rip + the renderer]
constexpr size_t RENDERER_VERTEX_BYTES = 0x5d48;
constexpr size_t RENDERER_VERTEX_BYTES_USED = 0x5d30;  // per frame buffer, int[3]
constexpr int32_t FRAME_BUFFERS = 3;
constexpr uint32_t PARTICLE_VERTEX_SIZE = 0x2c;
// r_ParticleVerticePoolSize's registration: lea r8, [rip + its int]; lea rdx, [rip + its name]; mov r9d, 15360
constexpr std::string_view VERTEX_POOL_CVAR_CODE =
    "4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 41 B9 00 3C 00 00 C7 44 24 20 00 20 00 00";
constexpr size_t RIP_LEA_SIZE = 7;
// The renderer's particle vertex and index buffers made (this = the renderer, once its render init set the buffer
// sizes, +0xa634): push rbp; sub rsp, 0x40; cmp dword [rcx + 0xa634], 0; mov rbp, rcx; je ...
constexpr std::string_view PARTICLE_BUFFERS_CODE =
    "40 55 48 83 EC 40 83 B9 34 A6 00 00 00 48 8B E9 0F 84 ?? ?? ?? ?? 48 89 5C 24 50 48 89 74 24 58 48 89 7C 24 60 4C 89 "
    "64 24 38 4C 89 6C 24 30 E8";
// The 16-bit indices reach 65535 vertices (if they count from the buffer's start, not each part's).
constexpr int MOST_PARTICLE_VERTICES = 65535;
constexpr size_t PARTICLE_CONTAINER = 0x80;
constexpr size_t CONTAINER_PARAMS = 0x8;
constexpr size_t CONTAINER_EMITTER = 0x100;
constexpr size_t PARAMS_COUNT = 0x8;
constexpr size_t PARAMS_TEXTURE = 0xa0;
constexpr size_t PARAMS_MATERIAL = 0xc0;
constexpr size_t EMITTER_EFFECT = 0x60;
constexpr size_t EFFECT_GET_NAME = 0x30;
constexpr std::array<std::string_view, KIND_COUNT> KIND_NAMES = {"fire", "sparks", "smoke", "blood", "other"};

// Words per kind, checked in this order against a name's words (split at non-letters and lower -> upper case): a word
// matches the start of a name's word, or anywhere in it from 4 letters (Grounddust; not "ash" in slash or bash).
struct KindWords {
  Kind kind;
  std::array<std::string_view, 10> words;
};
constexpr std::array<KindWords, 4> KIND_WORDS = {{
    {.kind = BLOOD, .words = {"blood", "gore", "bleed", "wound", "decapit", "dismember", "gib", "spurt", "execution", "excecution"}},
    {.kind = SPARKS, .words = {"spark", "ember", "cinder"}},
    {.kind = FIRE, .words = {"fire", "flame", "burn", "torch", "braz", "candle", "lava", "napalm"}},
    {.kind = SMOKE, .words = {"smoke", "steam", "fog", "mist", "vapo", "dust", "ash", "cloud", "haze"}},
}};

using ContainerSpawn = void (*)(uint8_t** spawner, void* context);
using ParticleLight = void (*)(const uint8_t* particle, void* context);
using LightList = void (*)(uint8_t* tiled_shading, void* lists, void* view, void* frame);
using BufferCreate = void (*)(uint8_t* buffer, uint32_t count, uint32_t stride, uint32_t format, uint32_t flags, const void* data);
using ParticleAlloc = void* (*)(uint8_t* container, void* spawner);
using VertexAlloc = bool (*)(void* render_vertices, int32_t frame, uint32_t vertices, uint32_t indices);
using ParticleBuffers = void (*)(void* renderer);

struct Params {
  const char* texture = nullptr;  // as when classified: another one means other params at the same address
  Kind kind = OTHER;
  std::string description;  // top effect, texture, material: for SaveNames
  float game_count = 0.f;     // the game's fCount
  float applied_count = 0.f;  // written by the hook
  uint32_t generation = 0;    // of the factors it was written with
};

struct State {
  bool searched = false;  // Present thread
  std::vector<renodx::utils::detour::Function> hooks;
  std::string details;  // where, or why not
  bool buffers_searched = false;  // device creation
  std::vector<renodx::utils::detour::Function> buffer_hooks;
};
static State state;
static ContainerSpawn original_container_spawn = nullptr;
static ParticleLight original_particle_light = nullptr;
static LightList original_light_list = nullptr;
static ParticleAlloc original_particle_alloc = nullptr;
static VertexAlloc original_vertex_alloc = nullptr;
static ParticleBuffers original_particle_buffers = nullptr;
static int* particle_vertices = nullptr;  // r_ParticleVerticePoolSize's int
static const uint8_t* cull_info = nullptr;
static uint8_t* const* renderer = nullptr;
// A light list site's operand and, for the array sites, its displacement to the raised arrays.
struct RaisedSite {
  uint8_t* operand;
  SiteKind kind;
  uint32_t array_displacement;
};
// The raised light limit: the render thread (in the light list hook) applies what the Present thread requests.
struct RaisedLights {
  std::vector<RaisedSite> sites;                  // empty: not available
  std::vector<engine_memory::CodePatch> patches;  // applied for `lights`
  const uint8_t* cull_info = nullptr;             // the raised cull info array
  BufferCreate buffer_create = nullptr;
  uint32_t lights = MAX_LIGHTS;
};
static RaisedLights raised;
static std::atomic<uint32_t> requested_lights = MAX_LIGHTS;
static std::atomic<bool> raise_active = false;  // the lighting dispatch's (render thread)
// The game's particle pool and the blocks the hook added (particle threads).
struct ParticlePool {
  std::shared_mutex mutex;
  uint8_t* pool = nullptr;
  uint32_t growths = 0;
  uint32_t target_growths = 0;  // the option's
};
static ParticlePool particle_pool;
// Light lists left to count (KeepCounting: while the overlay shows the counts); 0: the hooks only pass through.
static std::atomic<uint32_t> counting_frames = 0;
constexpr uint32_t COUNTING_FRAMES = 120;
// Since the last light list (particle and render threads).
static std::atomic<uint32_t> unspawned_particles = 0;
static std::atomic<uint32_t> undrawn_parts = 0;
static std::atomic<uint32_t> most_vertices = 0;  // the most used of a frame buffer
static std::atomic<uint32_t> undrawn_vertices = 0;
// The last frame's limits in use, for the overlay (render thread, once per frame).
struct FrameUse {
  std::shared_mutex mutex;
  uint32_t lights = 0;
  uint32_t light_limit = MAX_LIGHTS;
  uint32_t skipped_lights = 0;
  uint32_t unspawned_particles = 0;
  uint32_t undrawn_parts = 0;
  uint32_t vertices = 0;
  uint32_t undrawn_vertices = 0;
  uint64_t all_unspawned_particles = 0;
  uint64_t all_undrawn_parts = 0;
};
static FrameUse frame_use;
static std::array<std::atomic<float>, KIND_COUNT> factors = {1.f, 1.f, 1.f, 1.f, 1.f};
static std::atomic<uint32_t> generation = 0;  // counts factor changes
static renodx::utils::data::ParallelFlatHashMap<uintptr_t, Params, std::shared_mutex> known_params;

// OTHER when no word of any kind is in `name`.
inline Kind KindOf(std::string_view name) {
  std::vector<std::string> name_words(1);
  for (size_t index = 0; index < name.size(); ++index) {
    const auto character = static_cast<unsigned char>(name[index]);
    const bool starts_word = std::isupper(character) != 0 && index > 0 && std::islower(static_cast<unsigned char>(name[index - 1])) != 0;
    if (std::isalnum(character) == 0 || starts_word) {
      if (!name_words.back().empty()) name_words.emplace_back();
      if (std::isalnum(character) == 0) continue;
    }
    name_words.back() += static_cast<char>(std::tolower(character));
  }
  for (const auto& [kind, words] : KIND_WORDS) {
    for (const std::string_view word : words) {
      if (word.empty()) continue;
      for (const std::string& name_word : name_words) {
        if (name_word.starts_with(word) || (word.size() >= 4 && name_word.find(word) != std::string::npos)) return kind;
      }
    }
  }
  return OTHER;
}

// Particle update threads (the game's and its jobs).
inline void OnContainerSpawn(uint8_t** spawner, void* context) {
  uint8_t* container = *spawner;
  uint8_t* params = (container != nullptr ? *reinterpret_cast<uint8_t**>(container + CONTAINER_PARAMS) : nullptr);
  if (params != nullptr) {
    auto* count = reinterpret_cast<float*>(params + PARAMS_COUNT);
    const char* texture = *reinterpret_cast<const char* const*>(params + PARAMS_TEXTURE);
    const uint32_t current_generation = generation.load(std::memory_order_relaxed);
    bool current = false;
    known_params.if_contains(reinterpret_cast<uintptr_t>(params), [&](const auto& entry) {
      const auto& [key, known] = entry;
      current = known.texture == texture && known.generation == current_generation && *count == known.applied_count;
    });
    if (!current) {
      // Its kind from its texture and material, else its top effect's name.
      const auto classify = [&](Params* known) {
        const char* material = *reinterpret_cast<const char* const*>(params + PARAMS_MATERIAL);
        const std::string own = std::format("{} {}", (texture != nullptr ? texture : ""), (material != nullptr ? material : ""));
        const char* effect_name = "";
        if (uint8_t* emitter = *reinterpret_cast<uint8_t**>(container + CONTAINER_EMITTER); emitter != nullptr) {
          if (const void* effect = *reinterpret_cast<void* const*>(emitter + EMITTER_EFFECT); effect != nullptr) {
            using GetName = const char* (*)(const void*);
            const char* name = (*reinterpret_cast<GetName const* const*>(effect))[EFFECT_GET_NAME / sizeof(void*)](effect);
            if (name != nullptr) effect_name = name;
          }
        }
        known->texture = texture;
        known->kind = KindOf(own);
        if (known->kind == OTHER) known->kind = KindOf(effect_name);
        known->description = std::format("{}\t{}", effect_name, own);
        known->game_count = *count;
      };
      // The game's count is kept while the params hold what the hook wrote (a reloaded effect writes its own).
      const auto apply = [&](Params* known) {
        known->applied_count = known->game_count * factors[known->kind].load(std::memory_order_relaxed);
        known->generation = current_generation;
        *count = known->applied_count;
      };
      known_params.lazy_emplace_l(
          reinterpret_cast<uintptr_t>(params),
          [&](auto& entry) {
            auto& [key, known] = entry;
            if (known.texture != texture) {
              classify(&known);
            } else if (*count != known.applied_count) {
              known.game_count = *count;
            }
            apply(&known);
          },
          [&](const auto& construct) {
            Params known;
            classify(&known);
            apply(&known);
            construct(reinterpret_cast<uintptr_t>(params), std::move(known));
          });
    }
  }
  original_container_spawn(spawner, context);
}

// Particle render jobs.
inline void OnParticleLight(const uint8_t* particle, void* context) {
  const uint8_t* container = *reinterpret_cast<const uint8_t* const*>(particle + PARTICLE_CONTAINER);
  float kept = 1.f;  // the share of its particles that keep their light: the game's count / the hook's
  if (container != nullptr) {
    known_params.if_contains(*reinterpret_cast<const uintptr_t*>(container + CONTAINER_PARAMS), [&](const auto& entry) {
      const auto& [key, known] = entry;
      if (known.applied_count > known.game_count) kept = known.game_count / known.applied_count;
    });
  }
  // Picked by its address (Fibonacci hashing): the same particles every frame.
  const auto pick = static_cast<uint32_t>((reinterpret_cast<uintptr_t>(particle) * 0x9E3779B97F4A7C15ull) >> 32);
  if (kept >= 1.f || pick < static_cast<uint32_t>(kept * 4294967296.0)) {
    original_particle_light(particle, context);
  }
}

// Present thread: the game's pool size added until the option's growths, each linked and pushed onto its free list in
// one operation (never freed: particles hold the blocks).
inline void GrowParticlePool() {
  const std::unique_lock lock(particle_pool.mutex);
  if (particle_pool.pool == nullptr) return;
  const uint32_t block_size = *reinterpret_cast<const uint32_t*>(particle_pool.pool + POOL_BLOCK_SIZE);
  const uint32_t blocks = *reinterpret_cast<const uint32_t*>(particle_pool.pool + POOL_BLOCKS);
  while (particle_pool.growths < particle_pool.target_growths) {
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, size_t{blocks} * block_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (memory == nullptr) return;
    for (uint32_t index = 0; index + 1 < blocks; ++index) {
      reinterpret_cast<SLIST_ENTRY*>(memory + (size_t{index} * block_size))->Next =
          reinterpret_cast<SLIST_ENTRY*>(memory + (size_t{index + 1} * block_size));
    }
    InterlockedPushListSListEx(reinterpret_cast<SLIST_HEADER*>(particle_pool.pool), reinterpret_cast<SLIST_ENTRY*>(memory),
                               reinterpret_cast<SLIST_ENTRY*>(memory + (size_t{blocks - 1} * block_size)), blocks);
    ++particle_pool.growths;
  }
}

// Particle update threads: the pool empty, no particle (counted for the overlay).
inline void* OnParticleAlloc(uint8_t* container, void* spawner) {
  void* particle = original_particle_alloc(container, spawner);
  if (particle == nullptr && counting_frames.load(std::memory_order_relaxed) != 0) {
    unspawned_particles.fetch_add(1, std::memory_order_relaxed);
  }
  return particle;
}

// Particle render jobs: false when the part does not fit this frame's vertex buffer (not drawn). After a fit: the
// buffer's use so far (the renderer's bytes used, frame % 3 as the allocation picks it), for the overlay's most.
inline bool OnVertexAlloc(void* render_vertices, int32_t frame, uint32_t vertices, uint32_t indices) {
  if (counting_frames.load(std::memory_order_relaxed) == 0) return original_vertex_alloc(render_vertices, frame, vertices, indices);
  if (original_vertex_alloc(render_vertices, frame, vertices, indices)) {
    uint8_t* used_bytes = *renderer + RENDERER_VERTEX_BYTES_USED + (sizeof(uint32_t) * (frame % FRAME_BUFFERS));
    const uint32_t used = std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(used_bytes)).load(std::memory_order_relaxed) / PARTICLE_VERTEX_SIZE;
    uint32_t most = most_vertices.load(std::memory_order_relaxed);
    while (used > most && !most_vertices.compare_exchange_weak(most, used, std::memory_order_relaxed)) {
    }
    return true;
  }
  undrawn_parts.fetch_add(1, std::memory_order_relaxed);
  undrawn_vertices.fetch_add(vertices, std::memory_order_relaxed);
  return false;
}

// The render thread, once per frame. The light limit follows its option between light lists: the game's code back
// before its buffers change, the buffers at the new limit before the code writes more. The game makes its buffers
// again on a resolution change: checked each time. Counting: the lights it took (the cull info up to its zeroed end)
// and skipped, and what the particle limits missed since the last one.
inline void OnLightList(uint8_t* tiled_shading, void* lists, void* view, void* frame) {
  if (!raised.sites.empty()) {
    const uint32_t requested = requested_lights.load(std::memory_order_relaxed);
    if (requested != raised.lights) {
      for (auto& patch : raised.patches) {
        engine_memory::ApplyCodePatch(&patch, false);
      }
      raised.patches.clear();
      raised.lights = MAX_LIGHTS;
    }
    for (const auto& [offset, stride] : LIGHT_BUFFERS) {
      if (*reinterpret_cast<const uint32_t*>(tiled_shading + offset + BUFFER_COUNT) != requested) {
        raised.buffer_create(tiled_shading + offset, requested, stride, 0, LIGHT_BUFFER_FLAGS, nullptr);
      }
    }
    if (requested != raised.lights) {
      bool applied = true;
      for (const auto& [operand, kind, array_displacement] : raised.sites) {
        uint32_t value = requested;  // LIMIT
        if (kind == SiteKind::CULL_ARRAY || kind == SiteKind::SHADE_ARRAY) {
          value = array_displacement;
        } else if (kind == SiteKind::CULL_BYTES) {
          value = requested * CULL_INFO_SIZE;
        } else if (kind == SiteKind::SHADE_BYTES) {
          value = requested * SHADE_INFO_SIZE;
        }
        std::vector<uint8_t> replacement(sizeof(value));
        std::memcpy(replacement.data(), &value, sizeof(value));
        auto& patch = raised.patches.emplace_back(engine_memory::CodePatch{
            .address = operand,
            .original = std::vector<uint8_t>(operand, operand + sizeof(value)),
            .replacement = std::move(replacement),
        });
        applied = engine_memory::ApplyCodePatch(&patch, true) && applied;
      }
      if (applied) {
        raised.lights = requested;
      } else {
        // Code changed by something else: the game's code back, not tried again.
        for (auto& patch : raised.patches) {
          engine_memory::ApplyCodePatch(&patch, false);
        }
        raised.patches.clear();
        raised.sites.clear();
      }
    }
    raise_active.store(raised.lights != MAX_LIGHTS, std::memory_order_relaxed);
  }
  original_light_list(tiled_shading, lists, view, frame);
  if (counting_frames.load(std::memory_order_relaxed) == 0) return;
  counting_frames.fetch_sub(1, std::memory_order_relaxed);
  const uint8_t* taken = (raised.lights != MAX_LIGHTS ? raised.cull_info : cull_info);
  uint32_t lights = raised.lights;
  for (uint32_t index = 0; index < raised.lights; ++index) {
    if (*reinterpret_cast<const uint32_t*>(taken + index * CULL_INFO_SIZE) == 0) {
      lights = index;
      break;
    }
  }
  const std::unique_lock lock(frame_use.mutex);
  frame_use.lights = lights;
  frame_use.light_limit = raised.lights;
  frame_use.skipped_lights = static_cast<uint32_t>(std::max(*reinterpret_cast<const int32_t*>(tiled_shading + LIGHT_LIST_SKIPPED), 0));
  frame_use.unspawned_particles = unspawned_particles.exchange(0, std::memory_order_relaxed);
  frame_use.undrawn_parts = undrawn_parts.exchange(0, std::memory_order_relaxed);
  frame_use.vertices = most_vertices.exchange(0, std::memory_order_relaxed);
  frame_use.undrawn_vertices = undrawn_vertices.exchange(0, std::memory_order_relaxed);
  frame_use.all_unspawned_particles += frame_use.unspawned_particles;
  frame_use.all_undrawn_parts += frame_use.undrawn_parts;
}

// The render thread's init (and a device change): the vertices per frame raised as the buffers are made.
inline void OnParticleBuffers(void* renderer) {
  const int game_vertices = *particle_vertices;
  if (game_vertices < MOST_PARTICLE_VERTICES) {
    *particle_vertices = MOST_PARTICLE_VERTICES;
  }
  renodx::utils::log::i("[Ryse DLSS] Particle vertex buffers made: ", game_vertices, " -> ", *particle_vertices, " vertices");
  original_particle_buffers(renderer);
}

}  // namespace internal

// Render device creation (the game's code runs: decrypted), before the renderer's render thread init.
inline void HookParticleBuffers() {
  auto& state = internal::state;
  if (state.buffers_searched) return;
  state.buffers_searched = true;
  std::string cvar_at;
  std::string buffers_at;
  const uint8_t* cvar_code = engine_memory::FindPattern(internal::VERTEX_POOL_CVAR_CODE, &cvar_at);
  uint8_t* buffers_code = engine_memory::FindPattern(internal::PARTICLE_BUFFERS_CODE, &buffers_at);
  std::string details = std::format("r_ParticleVerticePoolSize code {}, particle buffers {}", cvar_at, buffers_at);
  if (cvar_code != nullptr && buffers_code != nullptr) {
    int32_t displacement;
    std::memcpy(&displacement, cvar_code + 3, sizeof(displacement));
    internal::particle_vertices = reinterpret_cast<int*>(const_cast<uint8_t*>(cvar_code) + internal::RIP_LEA_SIZE + displacement);
    internal::original_particle_buffers = reinterpret_cast<internal::ParticleBuffers>(buffers_code);
    state.buffer_hooks = {renodx::utils::detour::Function(&internal::original_particle_buffers, &internal::OnParticleBuffers, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE)};
    try {
      renodx::utils::detour::Install(state.buffer_hooks);
    } catch (const std::exception& error) {
      state.buffer_hooks.clear();
      details += std::format("; hook failed: {}", error.what());
    }
  }
  renodx::utils::log::i("[Ryse DLSS] Particle vertices: ", details);
}

// Present thread, once the game runs (its code is decrypted then).
inline void Install() {
  auto& state = internal::state;
  if (state.searched) return;
  state.searched = true;
  std::string found_at;
  std::string light_found_at;
  std::string light_list_at;
  std::string cull_info_at;
  std::string alloc_at;
  std::string pool_at;
  std::string vertex_alloc_at;
  uint8_t* code = engine_memory::FindPattern(internal::CONTAINER_SPAWN_CODE, &found_at);
  uint8_t* light_code = engine_memory::FindPattern(internal::PARTICLE_LIGHT_CODE, &light_found_at);
  uint8_t* light_list_code = engine_memory::FindPattern(internal::LIGHT_LIST_CODE, &light_list_at);
  const uint8_t* cull_info_code = engine_memory::FindPattern(internal::CULL_INFO_CODE, &cull_info_at);
  uint8_t* alloc_code = engine_memory::FindPattern(internal::PARTICLE_ALLOC_CODE, &alloc_at);
  const uint8_t* pool_code = engine_memory::FindPattern(internal::PARTICLE_POOL_CODE, &pool_at);
  uint8_t* vertex_alloc_code = engine_memory::FindPattern(internal::VERTEX_ALLOC_CODE, &vertex_alloc_at);
  state.details = std::format("container spawn {}, particle light {}, light list {}, cull info {}, particle allocation {}, "
                              "particle pool {}, vertex allocation {}",
                              found_at, light_found_at, light_list_at, cull_info_at, alloc_at, pool_at, vertex_alloc_at);
  // Without the light hook more particles would blink lights: both or none.
  if (code != nullptr && light_code != nullptr) {
    internal::original_container_spawn = reinterpret_cast<internal::ContainerSpawn>(code);
    internal::original_particle_light = reinterpret_cast<internal::ParticleLight>(light_code);
    state.hooks = {renodx::utils::detour::Function(&internal::original_container_spawn, &internal::OnContainerSpawn, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE),
                   renodx::utils::detour::Function(&internal::original_particle_light, &internal::OnParticleLight, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE)};
  }
  // The light list: the overlay's counts and the light limit (every site as expected, the raised arrays near the exe:
  // rel32 reach).
  if (light_list_code != nullptr && cull_info_code != nullptr) {
    const auto target = [](const uint8_t* instruction) {
      int32_t displacement;
      std::memcpy(&displacement, instruction + 3, sizeof(displacement));
      return instruction + internal::RIP_LEA_SIZE + displacement;
    };
    internal::cull_info = target(cull_info_code);
    internal::original_light_list = reinterpret_cast<internal::LightList>(light_list_code);
    state.hooks.push_back(renodx::utils::detour::Function(&internal::original_light_list, &internal::OnLightList, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE));
    std::string create_at;
    uint8_t* create_code = engine_memory::FindPattern(internal::BUFFER_CREATE_CODE, &create_at);
    uint8_t* arrays = nullptr;
    if (create_code != nullptr) {
      arrays = engine_memory::AllocateNearExe(internal::MOST_LIGHTS * (internal::CULL_INFO_SIZE + internal::SHADE_INFO_SIZE));
    }
    const uint8_t* game_shade = nullptr;
    for (const auto& site : internal::LIGHT_LIST_SITES) {
      if (site.kind != internal::SiteKind::SHADE_ARRAY) continue;
      const uint8_t* site_target = target(light_list_code + site.offset);
      if (game_shade == nullptr || site_target < game_shade) game_shade = site_target;
    }
    bool expected = arrays != nullptr;
    std::vector<internal::RaisedSite> sites;
    for (const auto& site : internal::LIGHT_LIST_SITES) {
      if (!expected) break;
      uint8_t* instruction = light_list_code + site.offset;
      uint8_t* operand = instruction + site.operand;
      uint32_t value;
      std::memcpy(&value, operand, sizeof(value));
      uint32_t array_displacement = 0;
      switch (site.kind) {
        case internal::SiteKind::CULL_ARRAY:
        case internal::SiteKind::SHADE_ARRAY: {
          const bool cull = site.kind == internal::SiteKind::CULL_ARRAY;
          const uint8_t* game_array = (cull ? internal::cull_info : game_shade);
          const uint8_t* raised_array = (cull ? arrays : arrays + internal::MOST_LIGHTS * internal::CULL_INFO_SIZE);
          const int64_t game_bytes = internal::MAX_LIGHTS * (cull ? internal::CULL_INFO_SIZE : internal::SHADE_INFO_SIZE);
          const int64_t field = target(instruction) - game_array;
          const int64_t displacement = (raised_array + field) - (instruction + internal::RIP_LEA_SIZE);
          expected = field >= 0 && field < game_bytes && displacement >= std::numeric_limits<int32_t>::min()
                     && displacement <= std::numeric_limits<int32_t>::max();
          array_displacement = static_cast<uint32_t>(static_cast<int32_t>(displacement));
          break;
        }
        case internal::SiteKind::CULL_BYTES:
          expected = value == internal::MAX_LIGHTS * internal::CULL_INFO_SIZE;
          break;
        case internal::SiteKind::SHADE_BYTES:
          expected = value == internal::MAX_LIGHTS * internal::SHADE_INFO_SIZE;
          break;
        case internal::SiteKind::LIMIT:
          expected = value == internal::MAX_LIGHTS;
          break;
      }
      sites.push_back({.operand = operand, .kind = site.kind, .array_displacement = array_displacement});
    }
    if (expected) {
      internal::raised = {
          .sites = std::move(sites),
          .cull_info = arrays,
          .buffer_create = reinterpret_cast<internal::BufferCreate>(create_code),
      };
    }
    state.details += std::format(", buffer create {}, light limit {}", create_at,
                                 (expected ? "ready" : "not available (code or memory)"));
  }
  if (vertex_alloc_code != nullptr) {
    int32_t renderer_offset;
    std::memcpy(&renderer_offset, vertex_alloc_code + internal::VERTEX_ALLOC_RENDERER + 3, sizeof(renderer_offset));
    internal::renderer = reinterpret_cast<uint8_t* const*>(vertex_alloc_code + internal::VERTEX_ALLOC_RENDERER + internal::RIP_LEA_SIZE
                                                            + renderer_offset);
    internal::original_vertex_alloc = reinterpret_cast<internal::VertexAlloc>(vertex_alloc_code);
    state.hooks.push_back(renodx::utils::detour::Function(&internal::original_vertex_alloc, &internal::OnVertexAlloc, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE));
  }
  // The pool is made with the particle system, before any level: its block size and count as the getter makes them.
  if (alloc_code != nullptr && pool_code != nullptr) {
    int32_t pool_offset;
    std::memcpy(&pool_offset, pool_code + 3, sizeof(pool_offset));
    uint8_t* pool = const_cast<uint8_t*>(pool_code) + internal::RIP_LEA_SIZE + pool_offset;
    const uint32_t block_size = *reinterpret_cast<const uint32_t*>(pool + internal::POOL_BLOCK_SIZE);
    const uint32_t blocks = *reinterpret_cast<const uint32_t*>(pool + internal::POOL_BLOCKS);
    if (block_size == 0x100 && blocks > 0 && reinterpret_cast<uintptr_t>(pool) % MEMORY_ALLOCATION_ALIGNMENT == 0) {
      {
        const std::unique_lock lock(internal::particle_pool.mutex);
        internal::particle_pool.pool = pool;
      }
      internal::GrowParticlePool();
      internal::original_particle_alloc = reinterpret_cast<internal::ParticleAlloc>(alloc_code);
      state.hooks.push_back(renodx::utils::detour::Function(&internal::original_particle_alloc, &internal::OnParticleAlloc, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE));
    }
    state.details += std::format(" ({} blocks of {} bytes{})", blocks, block_size,
                                 (internal::particle_pool.pool != nullptr ? "" : ": not as expected, not grown"));
  }
  if (!state.hooks.empty()) {
    try {
      renodx::utils::detour::Install(state.hooks);
    } catch (const std::exception& error) {
      state.hooks.clear();
      state.details += std::format("; hook failed: {}", error.what());
    }
  }
  renodx::utils::log::i("[Ryse DLSS] Particle counts: ", state.details);
}

// Present thread: the particle pool's size in times the game's (1-16), grown once the pool is known (Install); never
// shrunk.
inline void SetParticleMemory(uint32_t times) {
  {
    const std::unique_lock lock(internal::particle_pool.mutex);
    internal::particle_pool.target_growths = std::clamp(times, 1u, internal::MOST_POOL_GROWTHS + 1) - 1;
    if (internal::particle_pool.growths >= internal::particle_pool.target_growths) return;
  }
  internal::GrowParticlePool();
}

// Present thread: the factor of each kind (1: the game's count).
inline void SetFactor(Kind kind, float factor) {
  if (internal::factors[kind].exchange(factor, std::memory_order_relaxed) != factor) {
    internal::generation.fetch_add(1, std::memory_order_relaxed);
  }
}

// Present thread: the light limit, 255 (the game's) up to MOST_LIGHTS (applied at the next light list).
inline void SetLightLimit(uint32_t lights) {
  internal::requested_lights.store(std::clamp(lights, internal::MAX_LIGHTS, internal::MOST_LIGHTS), std::memory_order_relaxed);
}

// Render thread: the lighting cs 0x897232B5 must be the addon's (tiled_lights_cs.h).
inline bool RaisedLightsActive() {
  return internal::raise_active.load(std::memory_order_relaxed);
}

// Overlay: the hooks count for the next COUNTING_FRAMES frames.
inline void KeepCounting() {
  internal::counting_frames.store(internal::COUNTING_FRAMES, std::memory_order_relaxed);
}

// The last frame's lights, particle vertices and particles missed, and the particle pool, for the overlay.
inline std::string LastFrameText() {
  uint32_t growths;
  {
    const std::shared_lock lock(internal::particle_pool.mutex);
    growths = internal::particle_pool.growths;
  }
  uint32_t buffer_vertices = 0;
  if (internal::renderer != nullptr && *internal::renderer != nullptr) {
    buffer_vertices = *reinterpret_cast<const uint32_t*>(*internal::renderer + internal::RENDERER_VERTEX_BYTES) / internal::PARTICLE_VERTEX_SIZE;
  }
  auto& use = internal::frame_use;
  const std::shared_lock lock(use.mutex);
  return std::format("{} of {} lights ({} dropped); particle vertices {} of {} ({} more did not fit: {} effect parts not "
                     "drawn, {} in all); {} particles not spawned ({} in all), particle memory {}x the game's",
                     use.lights, use.light_limit, use.skipped_lights, use.vertices, buffer_vertices,
                     use.undrawn_vertices, use.undrawn_parts, use.all_undrawn_parts, use.unspawned_particles,
                     use.all_unspawned_particles, growths + 1);
}

inline const std::string& Details() {
  return internal::state.details;
}

// The sub-effects seen since the game started, each with its kind, top effect, texture and material, into `path`.
// Returns what was written, for the overlay.
inline std::string SaveNames(const std::filesystem::path& path) {
  std::ofstream file(path, std::ios::trunc);
  if (!file) return std::format("could not create {}", path.string());
  std::multimap<std::string, std::string> lines;  // sorted by top effect
  internal::known_params.for_each([&](const auto& entry) {
    const auto& [key, known] = entry;
    lines.emplace(known.description, std::string(internal::KIND_NAMES[known.kind]));
  });
  for (const auto& [description, kind] : lines) {
    file << kind << '\t' << description << '\n';
  }
  return std::format("{} sub-effects in {}", lines.size(), path.string());
}

// Removes the hooks. The counts they wrote stay until the effects reload: the params may be freed by now (unloads come
// with the game's exit). The pool's added blocks stay with the game.
inline void Uninstall() {
  auto& state = internal::state;
  for (auto* hooks : {&state.hooks, &state.buffer_hooks}) {
    if (hooks->empty()) continue;
    try {
      renodx::utils::detour::Uninstall(*hooks);
    } catch (const std::exception&) {
      // No-op: the game is closing; Detours left its code as it was.
    }
    hooks->clear();
  }
}

}  // namespace rysesonofrome::particle_counts
