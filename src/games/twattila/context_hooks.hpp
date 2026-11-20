/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11_1.h>
#include <intrin.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "./device_loss.hpp"
#include "./native_hook_lifetime.hpp"

// The game's immediate context's D3D11 runtime bodies, detoured: the single owner of these detours. Each feature is a
// path inside the same replacements, never a second detour on a body: the measure path (map trace v3, 17 bodies) and
// the ring (buffer_pool.hpp: the same 17 and 15 more, the Get, Get1 and copy bodies, and the 3 copies' versions d3d11
// puts in the table at a Flush).
// Bodies, not table slots: d3d11.dll gives each context its own table inside the context object and rewrites it at
// every SwapDeviceContextState (to stubs that call the same bodies, settled back at the next state call), so a slot
// hook dies at the add-on's first private-state pass. ReShade's proxy calls the bodies too, so every call on the
// context comes through: the game's, ReShade's and the add-ons'. Installed (Install, InstallRing) only on the render
// thread, while device_loss knows exactly one D3D11 device and it is single-threaded (the game's: no other thread runs
// these bodies), with CALLER_GUARANTEES_QUIESCENCE (enlisting threads can deadlock on the heap lock), and only when the
// bodies check out. Each feature takes itself out (Uninstall, UninstallRing, OnDestroyDevice); a detour comes out with
// the last feature that needs it.
namespace twattila::context_hooks {

enum class Kind : uint8_t {
  MAP,
  UNMAP,
  SET_CONSTANT_BUFFERS,
  SET_CONSTANT_BUFFERS1,
  SET_VERTEX_BUFFERS,
  SWAP_STATE,
  CLEAR_STATE,
  SEED_CONSTANT_BUFFERS,  // at Install: a stage's 14 slots as bound before it (Get1: windows in constants)
  SEED_VERTEX_BUFFERS,    // at Install: the 32 input slots as bound before it
};

// In the order of D3D11's stage-indexed bodies below.
enum class Stage : uint8_t { VERTEX_SHADER, HULL_SHADER, DOMAIN_SHADER, GEOMETRY_SHADER, PIXEL_SHADER, COMPUTE_SHADER };
constexpr size_t STAGES = 6;

// One call on the hooked context as the measure path's observer gets it: the caller's arguments, after the call's body
// (the native one, or the ring's) returned (the arrays are the caller's, valid only inside the observer).
struct Call {
  Kind kind = Kind::MAP;
  Stage stage = Stage::VERTEX_SHADER;
  int64_t qpc = 0;              // before the body
  int64_t done = 0;             // after it
  uint32_t nested = 0;          // hook entries inside the body (d3d11's own calls between bodies): not recorded
  uintptr_t direct_caller = 0;  // the replacement's return address (ReShade's proxy for the game's calls)
  ID3D11Resource* resource = nullptr;
  UINT subresource = 0;
  D3D11_MAP map_type = D3D11_MAP_READ;
  UINT map_flags = 0;
  HRESULT result = S_OK;
  UINT depth_pitch = 0;
  // An Unmap of a constant buffer written through its Map, hashing on: its bytes' hash (FNV-1a, 8-byte words) and the
  // hash's time (before the body, outside qpc / done).
  bool content_known = false;
  uint64_t content = 0;
  int64_t hash_ticks = 0;
  UINT start_slot = 0;
  UINT count = 0;
  ID3D11Buffer* const* buffers = nullptr;
  const UINT* first_constants = nullptr;  // Set1 (null: whole buffers) and the constant buffer seeds
  const UINT* constant_counts = nullptr;
  const UINT* strides = nullptr;  // IASetVertexBuffers and its seed
  const UINT* offsets = nullptr;
  ID3DDeviceContextState* state = nullptr;           // SwapDeviceContextState's new state
  ID3DDeviceContextState* previous_state = nullptr;  // and the one it replaced (asked for where the caller passed null)
};

using Observer = void (*)(const Call&);

// The ring's paths (buffer_pool.hpp): on the render thread, outside every other hook's body, with the depth raised (their
// own calls on the context go straight on). map, unmap, set, copy and copy_resource replace the native body (false: it
// runs as called); get runs after the native body filled the caller's arrays, swap after the native swap and its
// settling call, clear after the native ClearState. After another thread's call they still come (the ring unwinds);
// the measure path's do not. A copy path's own copy goes through the context's table (nested: straight on), so that
// d3d11's version of the body in it now runs (see FLUSHED_COPIES).
struct Ring {
  bool (*map)(ID3D11Resource* resource, UINT subresource, D3D11_MAP map_type, UINT map_flags,
              D3D11_MAPPED_SUBRESOURCE* mapped, HRESULT* result);
  bool (*unmap)(ID3D11Resource* resource, UINT subresource);
  // Set and Set1 (first_constants and constant_counts null: whole buffers).
  bool (*set)(Stage stage, UINT start_slot, UINT count, ID3D11Buffer* const* buffers, const UINT* first_constants,
              const UINT* constant_counts);
  // After Get and Get1 (`count` within the stage's slots).
  void (*get)(Stage stage, UINT start_slot, UINT count, ID3D11Buffer** buffers, UINT* first_constants,
              UINT* constant_counts);
  // CopySubresourceRegion (copy_flags null) and CopySubresourceRegion1.
  bool (*copy)(ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
               ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, const UINT* copy_flags);
  bool (*copy_resource)(ID3D11Resource* destination, ID3D11Resource* source);
  void (*swap)(ID3DDeviceContextState* state, ID3DDeviceContextState* previous_state);
  void (*clear)();
};

namespace internal {

using MapFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
                                                D3D11_MAPPED_SUBRESOURCE*);
using UnmapFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using SetConstantBuffersFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*);
using SetConstantBuffers1Function = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer* const*,
                                                             const UINT*, const UINT*);
using SetVertexBuffersFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*,
                                                          const UINT*, const UINT*);
using SwapStateFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*, ID3DDeviceContextState*,
                                                   ID3DDeviceContextState**);
using ClearStateFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using GetConstantBuffersFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer**);
using GetConstantBuffers1Function = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer**, UINT*,
                                                             UINT*);
using CopySubresourceRegionFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT,
                                                               UINT, ID3D11Resource*, UINT, const D3D11_BOX*);
using CopyResourceFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
using CopySubresourceRegion1Function = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*, ID3D11Resource*, UINT, UINT,
                                                                UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*,
                                                                UINT);

// The bodies (State's indices) and their ID3D11DeviceContext1 table slots. Set, Set1, Get and Get1 are indexed by
// Stage. The measure path's are the first MEASURE_BODIES; the ring needs all but IASetVertexBuffers.
constexpr size_t MAP = 0;
constexpr size_t UNMAP = 1;
constexpr size_t SET_CONSTANT_BUFFERS = 2;
constexpr size_t SET_CONSTANT_BUFFERS1 = 8;
constexpr size_t SET_VERTEX_BUFFERS = 14;
constexpr size_t SWAP_STATE = 15;
constexpr size_t CLEAR_STATE = 16;
constexpr size_t MEASURE_BODIES = 17;
constexpr size_t GET_CONSTANT_BUFFERS = 17;
constexpr size_t GET_CONSTANT_BUFFERS1 = 23;
constexpr size_t COPY_SUBRESOURCE_REGION = 29;
constexpr size_t COPY_RESOURCE = 30;
constexpr size_t COPY_SUBRESOURCE_REGION1 = 31;
// d3d11 rewrites the three copy slots in place after a Flush, and back at the next call that needs it: those versions
// (the same three slots), hooked too where they differ.
constexpr size_t FLUSHED_COPIES = 32;
constexpr size_t BODIES = 35;
constexpr std::array<size_t, BODIES> BODY_SLOTS = {14,  15,  7,   62,  66,  22,  16,  71,  119, 120, 121, 122,
                                                   123, 124, 18,  131, 110, 72,  100, 104, 81,  77,  109, 125,
                                                   126, 127, 128, 129, 130, 46,  47,  115, 46,  47,  115};

// A constant buffer mapped for writing, not yet unmapped: its pointer for the unmap's hash.
struct OpenMap {
  ID3D11Resource* resource = nullptr;
  UINT subresource = 0;
  const void* data = nullptr;
  UINT bytes = 0;
};

struct State {
  // The hooked context (null: every call goes straight on), read by every thread that runs a hooked body.
  std::atomic<ID3D11DeviceContext*> context = nullptr;
  // A call on it from another thread came: the measure path's calls go straight on from now (the ring unwinds).
  std::atomic_bool foreign_thread = false;
  std::atomic<int64_t> foreign_qpc = 0;  // when (0: none)
  // Set when the detours go in, before `context`; the rest only on the render thread.
  ID3D11Device* device = nullptr;  // not owned: the context's
  DWORD thread = 0;                // the render thread
  Observer observer = nullptr;     // the measure path (null: off)
  const Ring* ring = nullptr;      // the ring (null: off)
  bool hash_contents = false;  // the Unmaps' content hashes (they read write-combined memory: off for timing runs)
  uint32_t depth = 0;
  // While the measure path is on (no counters on the ring's own path): hook entries on the context and thread since its
  // Install, nested ones too, and of them, those inside another hook's body.
  std::array<uint32_t, BODIES> entries = {};
  uint32_t nested = 0;
  std::vector<OpenMap> open_maps;  // reserved at Install, never grown inside a body
  std::array<void*, BODIES> bodies = {};  // as read from the context's table when the detours went in
  // Detours' trampolines (a body's own address until installed). A test seam: test/tw_map_trace puts a slow driver
  // between a replacement and its trampoline (only while no call runs).
  std::array<void*, BODIES> trampolines = {};
  // The measure bodies' detours (in while either feature is on) and the ring's others (while the ring is on): 15, 18
  // with the flushed copies.
  std::vector<renodx::utils::detour::Function> hooks;
  std::vector<renodx::utils::detour::Function> ring_hooks;
  tw::native_hooks::Lifetime lifetime;
  tw::native_hooks::Lifetime ring_lifetime;
};
static State state;

inline int64_t Qpc() {
  LARGE_INTEGER now = {};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}

// What a replacement does with a call: the native body alone (both null), or the features' paths.
struct Route {
  Observer observer = nullptr;
  const Ring* ring = nullptr;
  explicit operator bool() const { return observer != nullptr || ring != nullptr; }
};

// The replacements' guards, in order: another context (or none hooked): straight on; a call from another thread:
// straight on, logged once, and the measure path off from then on (the device is single-threaded: never expected; the
// ring still gets the render thread's calls, to unwind); a call inside another hook's body: straight on (counted while
// measuring).
inline Route Tracked(const void* context, size_t body) {
  if (context != state.context.load(std::memory_order_acquire)) return {};
  if (GetCurrentThreadId() != state.thread) {
    // The time before the switch: a render-thread call passed on after it is never taken for one before it.
    const int64_t now = Qpc();
    if (!state.foreign_thread.exchange(true)) {
      state.foreign_qpc.store(now, std::memory_order_relaxed);
      // The log line allocates: nothing may unwind through d3d11.
      try {
        renodx::utils::log::w("[Total War] Context hooks: thread ", GetCurrentThreadId(),
                              " called the game's context (body ", BODY_SLOTS[body],
                              "): every call goes straight on from now.");
      } catch (const std::exception&) {
        // No-op: the switch stands without its log line.
      }
    }
    return {};
  }
  const bool foreign = state.foreign_thread.load(std::memory_order_relaxed);
  if (state.observer != nullptr && !foreign) {
    ++state.entries[body];
    if (state.depth != 0) {
      ++state.nested;
    }
  }
  if (state.depth != 0) return {};
  return {.observer = (foreign ? nullptr : state.observer), .ring = state.ring};
}

// A call's body (the ring's path or the native one) with the depth raised: hook entries inside it are nested ones.
// Timed into `call` for the measure path (null: the ring's alone, untimed).
template <typename Body>
auto RunBody(Call* call, Body body) {
  const uint32_t nested_before = state.nested;
  const auto finish = [call, nested_before] {
    --state.depth;
    if (call != nullptr) {
      call->done = Qpc();
      call->nested = state.nested - nested_before;
    }
  };
  ++state.depth;
  if (call != nullptr) {
    call->qpc = Qpc();
  }
  if constexpr (std::is_void_v<std::invoke_result_t<Body>>) {
    body();
    finish();
  } else {
    const auto result = body();
    finish();
    return result;
  }
}

inline HRESULT STDMETHODCALLTYPE Map(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
                                     D3D11_MAP map_type, UINT map_flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
  const auto native = reinterpret_cast<MapFunction>(state.trampolines[MAP]);
  const Route route = Tracked(context, MAP);
  if (!route) return native(context, resource, subresource, map_type, map_flags, mapped);
  const auto body = [&] {
    HRESULT result = S_OK;
    if (route.ring == nullptr || !route.ring->map(resource, subresource, map_type, map_flags, mapped, &result)) {
      result = native(context, resource, subresource, map_type, map_flags, mapped);
    }
    return result;
  };
  if (route.observer == nullptr) return RunBody(nullptr, body);
  Call call = {.kind = Kind::MAP, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()), .resource = resource,
               .subresource = subresource, .map_type = map_type, .map_flags = map_flags};
  call.result = RunBody(&call, body);
  if (SUCCEEDED(call.result) && mapped != nullptr) {
    call.depth_pitch = mapped->DepthPitch;
  }
  if (SUCCEEDED(call.result) && mapped != nullptr && state.hash_contents && map_type != D3D11_MAP_READ) {
    D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    resource->GetType(&dimension);
    D3D11_BUFFER_DESC desc = {};
    if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
      static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
    }
    // A constant buffer's map, at most 64 KB (all a shader sees of one; larger ones are the add-on's own rings, too
    // costly to hash): one of the same subresource replaces the older (its unmap not seen here); a full list takes no
    // more.
    if ((desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0
        && desc.ByteWidth <= D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16) {
      const OpenMap open = {.resource = resource, .subresource = subresource, .data = mapped->pData,
                            .bytes = desc.ByteWidth};
      const auto same = std::ranges::find_if(state.open_maps, [&](const OpenMap& map) {
        return map.resource == resource && map.subresource == subresource;
      });
      if (same != state.open_maps.end()) {
        *same = open;
      } else if (state.open_maps.size() < state.open_maps.capacity()) {
        state.open_maps.push_back(open);
      }
    }
  }
  route.observer(call);
  return call.result;
}

inline void STDMETHODCALLTYPE Unmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) {
  const auto native = reinterpret_cast<UnmapFunction>(state.trampolines[UNMAP]);
  const Route route = Tracked(context, UNMAP);
  if (!route) {
    native(context, resource, subresource);
    return;
  }
  const auto body = [&] {
    if (route.ring == nullptr || !route.ring->unmap(resource, subresource)) {
      native(context, resource, subresource);
    }
  };
  if (route.observer == nullptr) {
    RunBody(nullptr, body);
    return;
  }
  Call call = {.kind = Kind::UNMAP, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()),
               .resource = resource, .subresource = subresource};
  // The bytes as the caller wrote them, before the body (its pointer is valid until then). Write-combined memory
  // (natively; the ring's CPU copy is cached): slow to read, a trace's cost outside the body's time (timed:
  // hash_ticks). A DISCARD's bytes the caller did not write are undefined: the same upload can hash differently.
  const auto open = std::ranges::find_if(state.open_maps, [&](const OpenMap& map) {
    return map.resource == resource && map.subresource == subresource;
  });
  if (open != state.open_maps.end()) {
    const int64_t hash_start = Qpc();
    const auto* bytes = static_cast<const uint8_t*>(open->data);
    uint64_t hash = 0xCBF29CE484222325;
    size_t offset = 0;
    for (; offset + sizeof(uint64_t) <= open->bytes; offset += sizeof(uint64_t)) {
      uint64_t word = 0;
      std::memcpy(&word, bytes + offset, sizeof(word));
      hash = (hash ^ word) * 0x100000001B3;
    }
    for (; offset < open->bytes; ++offset) {
      hash = (hash ^ bytes[offset]) * 0x100000001B3;
    }
    call.content_known = true;
    call.content = hash;
    state.open_maps.erase(open);
    call.hash_ticks = Qpc() - hash_start;
  }
  RunBody(&call, body);
  route.observer(call);
}

template <size_t STAGE>
void STDMETHODCALLTYPE SetConstantBuffers(ID3D11DeviceContext* context, UINT start_slot, UINT count,
                                          ID3D11Buffer* const* buffers) {
  const auto native = reinterpret_cast<SetConstantBuffersFunction>(state.trampolines[SET_CONSTANT_BUFFERS + STAGE]);
  const Route route = Tracked(context, SET_CONSTANT_BUFFERS + STAGE);
  if (!route) {
    native(context, start_slot, count, buffers);
    return;
  }
  const auto body = [&] {
    if (route.ring == nullptr
        || !route.ring->set(static_cast<Stage>(STAGE), start_slot, count, buffers, nullptr, nullptr)) {
      native(context, start_slot, count, buffers);
    }
  };
  if (route.observer == nullptr) {
    RunBody(nullptr, body);
    return;
  }
  Call call = {.kind = Kind::SET_CONSTANT_BUFFERS, .stage = static_cast<Stage>(STAGE),
               .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()), .start_slot = start_slot, .count = count,
               .buffers = buffers};
  RunBody(&call, body);
  route.observer(call);
}

template <size_t STAGE>
void STDMETHODCALLTYPE SetConstantBuffers1(ID3D11DeviceContext1* context, UINT start_slot, UINT count,
                                           ID3D11Buffer* const* buffers, const UINT* first_constants,
                                           const UINT* constant_counts) {
  const auto native = reinterpret_cast<SetConstantBuffers1Function>(state.trampolines[SET_CONSTANT_BUFFERS1 + STAGE]);
  const Route route = Tracked(context, SET_CONSTANT_BUFFERS1 + STAGE);
  if (!route) {
    native(context, start_slot, count, buffers, first_constants, constant_counts);
    return;
  }
  const auto body = [&] {
    if (route.ring == nullptr
        || !route.ring->set(static_cast<Stage>(STAGE), start_slot, count, buffers, first_constants, constant_counts)) {
      native(context, start_slot, count, buffers, first_constants, constant_counts);
    }
  };
  if (route.observer == nullptr) {
    RunBody(nullptr, body);
    return;
  }
  Call call = {.kind = Kind::SET_CONSTANT_BUFFERS1, .stage = static_cast<Stage>(STAGE),
               .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()), .start_slot = start_slot, .count = count,
               .buffers = buffers, .first_constants = first_constants, .constant_counts = constant_counts};
  RunBody(&call, body);
  route.observer(call);
}

inline void STDMETHODCALLTYPE SetVertexBuffers(ID3D11DeviceContext* context, UINT start_slot, UINT count,
                                               ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets) {
  const auto native = reinterpret_cast<SetVertexBuffersFunction>(state.trampolines[SET_VERTEX_BUFFERS]);
  const Observer observer = Tracked(context, SET_VERTEX_BUFFERS).observer;
  if (observer == nullptr) {
    native(context, start_slot, count, buffers, strides, offsets);
    return;
  }
  Call call = {.kind = Kind::SET_VERTEX_BUFFERS, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()),
               .start_slot = start_slot, .count = count, .buffers = buffers, .strides = strides, .offsets = offsets};
  RunBody(&call, [&] { native(context, start_slot, count, buffers, strides, offsets); });
  observer(call);
}

// The active state tracked here: a swap back passes no out pointer (the add-on's passes), so the hook asks for the
// replaced state itself and lets it go. d3d11 applies the new state lazily: its swap stubs, at the next call on the
// context, call IASetVertexBuffers and each stage's SetConstantBuffers1 bodies from inside d3d11.dll
// (test/tw_context_hooks).
// One settling call here makes that application nested in this hook (passed on, not taken for the caller's binds),
// before the ring's path sees the new state.
inline void STDMETHODCALLTYPE SwapState(ID3D11DeviceContext1* context, ID3DDeviceContextState* new_state,
                                        ID3DDeviceContextState** previous_state) {
  const auto native = reinterpret_cast<SwapStateFunction>(state.trampolines[SWAP_STATE]);
  const Route route = Tracked(context, SWAP_STATE);
  if (!route) {
    native(context, new_state, previous_state);
    return;
  }
  ID3DDeviceContextState* replaced = nullptr;
  ID3DDeviceContextState* previous = nullptr;
  const auto body = [&] {
    native(context, new_state, (previous_state != nullptr ? previous_state : &replaced));
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->IAGetPrimitiveTopology(&topology);
    previous = (previous_state != nullptr ? *previous_state : replaced);
    if (route.ring != nullptr) {
      route.ring->swap(new_state, previous);
    }
    if (replaced != nullptr) {
      replaced->Release();
    }
  };
  if (route.observer == nullptr) {
    RunBody(nullptr, body);
    return;
  }
  Call call = {.kind = Kind::SWAP_STATE, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()),
               .state = new_state};
  RunBody(&call, body);
  call.previous_state = previous;
  route.observer(call);
}

inline void STDMETHODCALLTYPE ClearState(ID3D11DeviceContext* context) {
  const auto native = reinterpret_cast<ClearStateFunction>(state.trampolines[CLEAR_STATE]);
  const Route route = Tracked(context, CLEAR_STATE);
  if (!route) {
    native(context);
    return;
  }
  const auto body = [&] {
    native(context);
    if (route.ring != nullptr) {
      route.ring->clear();
    }
  };
  if (route.observer == nullptr) {
    RunBody(nullptr, body);
    return;
  }
  Call call = {.kind = Kind::CLEAR_STATE, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress())};
  RunBody(&call, body);
  route.observer(call);
}

// The ring's bodies: straight on unless the ring is on. There a Get's count is bounded to the stage's slots (past them
// the call is invalid: d3d11 may fill none of the entries the ring reads).
template <size_t STAGE>
void STDMETHODCALLTYPE GetConstantBuffers(ID3D11DeviceContext* context, UINT start_slot, UINT count,
                                          ID3D11Buffer** buffers) {
  const auto native = reinterpret_cast<GetConstantBuffersFunction>(state.trampolines[GET_CONSTANT_BUFFERS + STAGE]);
  const Ring* ring = Tracked(context, GET_CONSTANT_BUFFERS + STAGE).ring;
  if (ring == nullptr) {
    native(context, start_slot, count, buffers);
    return;
  }
  constexpr UINT SLOTS = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
  const UINT bounded = (start_slot < SLOTS ? std::min(count, SLOTS - start_slot) : 0);
  RunBody(nullptr, [&] {
    native(context, start_slot, bounded, buffers);
    ring->get(static_cast<Stage>(STAGE), start_slot, bounded, buffers, nullptr, nullptr);
  });
}

template <size_t STAGE>
void STDMETHODCALLTYPE GetConstantBuffers1(ID3D11DeviceContext1* context, UINT start_slot, UINT count,
                                           ID3D11Buffer** buffers, UINT* first_constants, UINT* constant_counts) {
  const auto native = reinterpret_cast<GetConstantBuffers1Function>(state.trampolines[GET_CONSTANT_BUFFERS1 + STAGE]);
  const Ring* ring = Tracked(context, GET_CONSTANT_BUFFERS1 + STAGE).ring;
  if (ring == nullptr) {
    native(context, start_slot, count, buffers, first_constants, constant_counts);
    return;
  }
  constexpr UINT SLOTS = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
  const UINT bounded = (start_slot < SLOTS ? std::min(count, SLOTS - start_slot) : 0);
  RunBody(nullptr, [&] {
    native(context, start_slot, bounded, buffers, first_constants, constant_counts);
    ring->get(static_cast<Stage>(STAGE), start_slot, bounded, buffers, first_constants, constant_counts);
  });
}

// The copies: BODY the version d3d11 has in the table after a copy, or the one after a Flush (FLUSHED_COPIES).
template <size_t BODY>
void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination,
                                             UINT destination_subresource, UINT x, UINT y, UINT z,
                                             ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box) {
  const auto native = reinterpret_cast<CopySubresourceRegionFunction>(state.trampolines[BODY]);
  const Ring* ring = Tracked(context, BODY).ring;
  if (ring == nullptr) {
    native(context, destination, destination_subresource, x, y, z, source, source_subresource, box);
    return;
  }
  RunBody(nullptr, [&] {
    if (!ring->copy(destination, destination_subresource, x, y, z, source, source_subresource, box, nullptr)) {
      native(context, destination, destination_subresource, x, y, z, source, source_subresource, box);
    }
  });
}

template <size_t BODY>
void STDMETHODCALLTYPE CopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination,
                                    ID3D11Resource* source) {
  const auto native = reinterpret_cast<CopyResourceFunction>(state.trampolines[BODY]);
  const Ring* ring = Tracked(context, BODY).ring;
  if (ring == nullptr) {
    native(context, destination, source);
    return;
  }
  RunBody(nullptr, [&] {
    if (!ring->copy_resource(destination, source)) {
      native(context, destination, source);
    }
  });
}

template <size_t BODY>
void STDMETHODCALLTYPE CopySubresourceRegion1(ID3D11DeviceContext1* context, ID3D11Resource* destination,
                                              UINT destination_subresource, UINT x, UINT y, UINT z,
                                              ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box,
                                              UINT copy_flags) {
  const auto native = reinterpret_cast<CopySubresourceRegion1Function>(state.trampolines[BODY]);
  const Ring* ring = Tracked(context, BODY).ring;
  if (ring == nullptr) {
    native(context, destination, destination_subresource, x, y, z, source, source_subresource, box, copy_flags);
    return;
  }
  RunBody(nullptr, [&] {
    if (!ring->copy(destination, destination_subresource, x, y, z, source, source_subresource, box, &copy_flags)) {
      native(context, destination, destination_subresource, x, y, z, source, source_subresource, box, copy_flags);
    }
  });
}

// The detours no feature needs any more out (the ring's own without the ring; all with neither feature on): the context
// let go first (every call goes straight on). A removal that fails leaves them passing every call on, and this image
// loaded for the process (Lifetime). On the render thread (no call in flight), or at the add-on's detach.
inline void Unhook() {
  const auto remove = [](tw::native_hooks::Lifetime* lifetime, std::vector<renodx::utils::detour::Function>* hooks) {
    if (hooks->empty()) return;
    try {
      lifetime->Uninstall(hooks);
    } catch (const std::exception& error) {
      renodx::utils::log::e(
          "[Total War] Context hooks could not be removed; they pass every call on and the add-on stays loaded: ",
          error.what());
    }
  };
  if (state.ring == nullptr) {
    remove(&state.ring_lifetime, &state.ring_hooks);
  }
  if (state.ring != nullptr || state.observer != nullptr) return;
  state.context.store(nullptr);
  state.open_maps.clear();
  remove(&state.lifetime, &state.hooks);
}

// On the render thread, `context` the game's immediate context below ReShade's proxy: the detours a feature needs in
// (`ring`: the ring's own too), each body checked first. Those already in (the other feature's) stay. False (logged):
// nothing changed.
inline bool Hook(ID3D11DeviceContext* context, bool ring) {
  const auto refuse = [](std::string_view reason) {
    renodx::utils::log::w("[Total War] Context hooks: not installed (", reason, ").");
    return false;
  };
  const bool hooked = !state.hooks.empty();
  if (hooked && state.context.load() != context) return refuse("an earlier removal failed, or another context hooked");
  if (ring && !state.ring_hooks.empty()) return refuse("an earlier removal failed");
  if (hooked && GetCurrentThreadId() != state.thread) return refuse("another thread than the hooks' own");
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  {
    const std::shared_lock lock(device_loss::devices_mutex);
    if (device_loss::devices.size() != 1 || device_loss::devices.front().device != device.Get()) {
      return refuse(std::format("{} D3D11 devices known, not only the game's", device_loss::devices.size()));
    }
    // A multithreaded device's other threads may run these bodies while Detours patches them.
    if (!device_loss::devices.front().single_threaded) return refuse("the game's D3D11 device is not single-threaded");
  }
  Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()))) || context1.Get() != context) {
    return refuse("no ID3D11DeviceContext1 at the context's address");
  }
  // The table settles at the first state call after a swap: read around one, twice.
  const auto read_table = [context] {
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->IAGetPrimitiveTopology(&topology);
    void* const* table = *reinterpret_cast<void* const* const*>(context);
    std::array<void*, BODIES> bodies = {};
    for (size_t body = 0; body < bodies.size(); ++body) {
      bodies[body] = table[BODY_SLOTS[body]];
    }
    return bodies;
  };
  auto bodies = read_table();
  if (read_table() != bodies) return refuse("the context's table changed between two reads");
  size_t used = MEASURE_BODIES;
  if (ring) {
    // Both versions of the copy bodies: the table after a copy between two buffers of ours, then after a Flush (the
    // context left so). The others stay.
    const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT,
                                    .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    Microsoft::WRL::ComPtr<ID3D11Buffer> from;
    Microsoft::WRL::ComPtr<ID3D11Buffer> to;
    if (FAILED(device->CreateBuffer(&desc, nullptr, from.GetAddressOf()))
        || FAILED(device->CreateBuffer(&desc, nullptr, to.GetAddressOf()))) {
      return refuse("no buffers to read the copy bodies with");
    }
    context->CopyResource(to.Get(), from.Get());
    const auto copied = read_table();
    context->Flush();
    const auto flushed = read_table();
    size_t changed = 0;
    for (size_t body = 0; body < FLUSHED_COPIES; ++body) {
      if (body >= COPY_SUBRESOURCE_REGION) {
        bodies[body] = copied[body];
        bodies[body + FLUSHED_COPIES - COPY_SUBRESOURCE_REGION] = flushed[body];
        changed += (copied[body] != flushed[body] ? 1 : 0);
      } else if (copied[body] != bodies[body] || flushed[body] != bodies[body]) {
        return refuse("the context's table changed at a copy or a Flush");
      }
    }
    if (changed != 0 && changed != BODIES - FLUSHED_COPIES) return refuse("only some copy bodies change at a Flush");
    used = (changed != 0 ? BODIES : FLUSHED_COPIES);
  }
  const size_t first_new = (hooked ? MEASURE_BODIES : 0);
  if (hooked && !std::equal(bodies.begin(), bodies.begin() + MEASURE_BODIES, state.bodies.begin())) {
    return refuse("the context's table changed since the hooks went in");
  }
  // Each body inside the system's d3d11.dll (not a proxy named so, nor another module's hook), each once, none to be
  // detoured starting with a jump (E9 rel32, EB rel8, FF 25 indirect, push imm32 / ret: another hook's).
  // A 32-bit process sees its SysWOW64 modules under System32's name.
  std::wstring system_d3d11(MAX_PATH, L'\0');
  system_d3d11.resize(std::min<size_t>(GetSystemDirectoryW(system_d3d11.data(), MAX_PATH), MAX_PATH));
  system_d3d11 += L"\\d3d11.dll";
  HMODULE d3d11 = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCWSTR>(bodies.front()), &d3d11)
      == FALSE) {
    return refuse("a body outside every module");
  }
  std::wstring d3d11_path(MAX_PATH, L'\0');
  d3d11_path.resize(GetModuleFileNameW(d3d11, d3d11_path.data(), MAX_PATH));
  if (CompareStringOrdinal(d3d11_path.c_str(), -1, system_d3d11.c_str(), -1, TRUE) != CSTR_EQUAL) {
    return refuse("the bodies are not in the system's d3d11.dll");
  }
  for (size_t body = 0; body < used; ++body) {
    HMODULE module = nullptr;
    const auto* code = static_cast<const uint8_t*>(bodies[body]);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(bodies[body]), &module)
            == FALSE
        || module != d3d11) {
      return refuse("a body outside d3d11.dll");
    }
    if (body >= first_new
        && (code[0] == 0xE9 || code[0] == 0xEB || (code[0] == 0xFF && code[1] == 0x25)
            || (code[0] == 0x68 && code[5] == 0xC3))) {
      return refuse(std::format("the body at d3d11.dll+0x{:x} starts with a jump",
                                reinterpret_cast<uintptr_t>(bodies[body]) - reinterpret_cast<uintptr_t>(d3d11)));
    }
  }
  std::vector<void*> sorted(bodies.begin(), bodies.begin() + used);
  std::ranges::sort(sorted);
  if (std::ranges::adjacent_find(sorted) != sorted.end()) return refuse("two of the bodies are one function");

  const std::array<void*, BODIES> replacements = {
      reinterpret_cast<void*>(&Map),
      reinterpret_cast<void*>(&Unmap),
      reinterpret_cast<void*>(&SetConstantBuffers<0>),
      reinterpret_cast<void*>(&SetConstantBuffers<1>),
      reinterpret_cast<void*>(&SetConstantBuffers<2>),
      reinterpret_cast<void*>(&SetConstantBuffers<3>),
      reinterpret_cast<void*>(&SetConstantBuffers<4>),
      reinterpret_cast<void*>(&SetConstantBuffers<5>),
      reinterpret_cast<void*>(&SetConstantBuffers1<0>),
      reinterpret_cast<void*>(&SetConstantBuffers1<1>),
      reinterpret_cast<void*>(&SetConstantBuffers1<2>),
      reinterpret_cast<void*>(&SetConstantBuffers1<3>),
      reinterpret_cast<void*>(&SetConstantBuffers1<4>),
      reinterpret_cast<void*>(&SetConstantBuffers1<5>),
      reinterpret_cast<void*>(&SetVertexBuffers),
      reinterpret_cast<void*>(&SwapState),
      reinterpret_cast<void*>(&ClearState),
      reinterpret_cast<void*>(&GetConstantBuffers<0>),
      reinterpret_cast<void*>(&GetConstantBuffers<1>),
      reinterpret_cast<void*>(&GetConstantBuffers<2>),
      reinterpret_cast<void*>(&GetConstantBuffers<3>),
      reinterpret_cast<void*>(&GetConstantBuffers<4>),
      reinterpret_cast<void*>(&GetConstantBuffers<5>),
      reinterpret_cast<void*>(&GetConstantBuffers1<0>),
      reinterpret_cast<void*>(&GetConstantBuffers1<1>),
      reinterpret_cast<void*>(&GetConstantBuffers1<2>),
      reinterpret_cast<void*>(&GetConstantBuffers1<3>),
      reinterpret_cast<void*>(&GetConstantBuffers1<4>),
      reinterpret_cast<void*>(&GetConstantBuffers1<5>),
      reinterpret_cast<void*>(&CopySubresourceRegion<COPY_SUBRESOURCE_REGION>),
      reinterpret_cast<void*>(&CopyResource<COPY_RESOURCE>),
      reinterpret_cast<void*>(&CopySubresourceRegion1<COPY_SUBRESOURCE_REGION1>),
      reinterpret_cast<void*>(&CopySubresourceRegion<FLUSHED_COPIES>),
      reinterpret_cast<void*>(&CopyResource<FLUSHED_COPIES + 1>),
      reinterpret_cast<void*>(&CopySubresourceRegion1<FLUSHED_COPIES + 2>),
  };
  if (!hooked) {
    state.device = device.Get();
    state.thread = GetCurrentThreadId();
    state.depth = 0;
    state.open_maps.clear();
    state.foreign_thread.store(false);
    state.foreign_qpc.store(0);
  }
  // The ring's: both copy versions, also where they are one (its check at each present compares the table with them).
  std::copy(bodies.begin() + first_new, bodies.begin() + (ring ? BODIES : used), state.bodies.begin() + first_new);
  std::copy(bodies.begin() + first_new, bodies.begin() + used, state.trampolines.begin() + first_new);
  // The measure bodies into `hooks`, the ring's others into `ring_hooks`, each set with its own module reference.
  for (const size_t set_start : {size_t{0}, MEASURE_BODIES}) {
    const size_t begin = std::max(set_start, first_new);
    const size_t end = (set_start == 0 ? MEASURE_BODIES : used);
    if (begin >= end) continue;
    auto& hooks = (set_start == 0 ? state.hooks : state.ring_hooks);
    auto& lifetime = (set_start == 0 ? state.lifetime : state.ring_lifetime);
    for (size_t body = begin; body < end; ++body) {
      hooks.emplace_back(&state.trampolines[body], replacements[body],
                         renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE);
    }
    try {
      lifetime.Install(hooks);
    } catch (const std::exception& error) {
      hooks.clear();
      std::copy(bodies.begin() + begin, bodies.begin() + end, state.trampolines.begin() + begin);
      // The measure bodies in for this call alone come out again.
      if (set_start != 0 && !hooked) {
        Unhook();
      }
      return refuse(error.what());
    }
  }
  state.context.store(context, std::memory_order_release);
  return true;
}

}  // namespace internal

// The measure path out (the detours with it when the ring is off). On the render thread (no call in flight), or at the
// add-on's detach.
inline void Uninstall() {
  internal::state.observer = nullptr;
  internal::state.open_maps.clear();
  internal::Unhook();
}

// The ring out (its own detours, and the others when the measure path is off). On the render thread, or at the add-on's
// detach, or at the device's end.
inline void UninstallRing() {
  internal::state.ring = nullptr;
  internal::Unhook();
}

// On the render thread, `context` the game's immediate context below ReShade's proxy, and `observer` the measure path:
// the bodies in its table checked and detoured, the bindings it already holds given to `observer` as seeds, then its
// calls taken (`hash_contents`: with the constant buffers' content hashes). False (logged): nothing installed.
inline bool Install(ID3D11DeviceContext* context, Observer observer, bool hash_contents) {
  auto& state = internal::state;
  if (!internal::Hook(context, false)) return false;
  state.hash_contents = hash_contents;
  state.entries = {};
  state.nested = 0;
  state.open_maps.clear();
  state.open_maps.reserve(64);
  Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
  context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()));
  // The seeds: the bindings from before (the game keeps buffers bound across frames), as the caller sees them (the Get1
  // bodies: the ring's, if it is on, give the game's buffers, not its pieces).
  constexpr std::array GETS = {
      &ID3D11DeviceContext1::VSGetConstantBuffers1, &ID3D11DeviceContext1::HSGetConstantBuffers1,
      &ID3D11DeviceContext1::DSGetConstantBuffers1, &ID3D11DeviceContext1::GSGetConstantBuffers1,
      &ID3D11DeviceContext1::PSGetConstantBuffers1, &ID3D11DeviceContext1::CSGetConstantBuffers1};
  for (size_t stage = 0; stage < STAGES; ++stage) {
    std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers = {};
    std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> first_constants = {};
    std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> constant_counts = {};
    const int64_t qpc = internal::Qpc();
    (context1.Get()->*GETS[stage])(0, static_cast<UINT>(buffers.size()), buffers.data(), first_constants.data(),
                                   constant_counts.data());
    observer({.kind = Kind::SEED_CONSTANT_BUFFERS, .stage = static_cast<Stage>(stage), .qpc = qpc,
              .done = internal::Qpc(), .count = static_cast<UINT>(buffers.size()), .buffers = buffers.data(),
              .first_constants = first_constants.data(), .constant_counts = constant_counts.data()});
    for (ID3D11Buffer* buffer : buffers) {
      if (buffer != nullptr) {
        buffer->Release();
      }
    }
  }
  std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertex_buffers = {};
  std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides = {};
  std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> offsets = {};
  const int64_t qpc = internal::Qpc();
  context->IAGetVertexBuffers(0, static_cast<UINT>(vertex_buffers.size()), vertex_buffers.data(), strides.data(),
                              offsets.data());
  observer({.kind = Kind::SEED_VERTEX_BUFFERS, .qpc = qpc, .done = internal::Qpc(),
            .count = static_cast<UINT>(vertex_buffers.size()), .buffers = vertex_buffers.data(),
            .strides = strides.data(), .offsets = offsets.data()});
  for (ID3D11Buffer* buffer : vertex_buffers) {
    if (buffer != nullptr) {
      buffer->Release();
    }
  }
  state.observer = observer;
  renodx::utils::log::i("[Total War] Context hooks: ", internal::MEASURE_BODIES,
                        " d3d11.dll bodies hooked on the game's context", (hash_contents ? ", content hashes on." : "."));
  return true;
}

// On the render thread, `context` the game's immediate context below ReShade's proxy: the ring's paths on (its bodies
// checked and detoured; the caller seeds its tables after). False (logged): nothing installed.
inline bool InstallRing(ID3D11DeviceContext* context, const Ring* ring) {
  if (!internal::Hook(context, true)) return false;
  internal::state.ring = ring;
  return true;
}

// The game's device's end (ReShade's destroy_device): the measure path out with it, every call straight on from now
// (the ring's own handler takes the ring out). The installing feature calls it from its own handler, under the lock it
// holds around its other removals. True: the hooks were in.
inline bool OnDestroyDevice(reshade::api::device* device) {
  if (internal::state.context.load() == nullptr || device->get_api() != reshade::api::device_api::d3d11) return false;
  if (reinterpret_cast<ID3D11Device*>(device->get_native()) != internal::state.device) return false;
  internal::state.context.store(nullptr);
  Uninstall();
  return true;
}

}  // namespace twattila::context_hooks
