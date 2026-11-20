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
// path inside the same replacements (now only the measure path, map trace v3), never a second detour on a body.
// Bodies, not table slots: d3d11.dll gives each context its own table inside the context object and rewrites it at
// every SwapDeviceContextState (to stubs that call the same bodies, settled back at the next state call), so a slot
// hook dies at the add-on's first private-state pass. ReShade's proxy calls the bodies too, so every call on the
// context comes through: the game's, ReShade's and the add-ons'. Installed (Install) only on the render thread, while
// device_loss knows exactly one D3D11 device and it is single-threaded (the game's: no other thread runs these bodies),
// with CALLER_GUARANTEES_QUIESCENCE (enlisting threads can deadlock on the heap lock), and only when the bodies check
// out. Removed by the installing feature: at its end, at the device's end (OnDestroyDevice, from its destroy_device
// handler, under its lock) and at the add-on's detach.
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

// One call on the hooked context as the measure path's observer gets it: after the native body returned (the arrays are
// the caller's, valid only inside the observer).
struct Call {
  Kind kind = Kind::MAP;
  Stage stage = Stage::VERTEX_SHADER;
  int64_t qpc = 0;              // before the native body
  int64_t done = 0;             // after it
  uint32_t nested = 0;          // hook entries inside the native body (d3d11's own calls between bodies): not recorded
  uintptr_t direct_caller = 0;  // the replacement's return address (ReShade's proxy for the game's calls)
  ID3D11Resource* resource = nullptr;
  UINT subresource = 0;
  D3D11_MAP map_type = D3D11_MAP_READ;
  UINT map_flags = 0;
  HRESULT result = S_OK;
  UINT depth_pitch = 0;
  // An Unmap of a constant buffer written through its Map, hashing on: its bytes' hash (FNV-1a, 8-byte words) and the
  // hash's time (before the native body, outside qpc / done).
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

// The bodies (State's indices) and their ID3D11DeviceContext1 table slots. Set and Set1 are indexed by Stage.
constexpr size_t MAP = 0;
constexpr size_t UNMAP = 1;
constexpr size_t SET_CONSTANT_BUFFERS = 2;
constexpr size_t SET_CONSTANT_BUFFERS1 = 8;
constexpr size_t SET_VERTEX_BUFFERS = 14;
constexpr size_t SWAP_STATE = 15;
constexpr size_t CLEAR_STATE = 16;
constexpr size_t BODIES = 17;
constexpr std::array<size_t, BODIES> BODY_SLOTS = {14,  15,  7,   62,  66,  22, 16,  71, 119,
                                                   120, 121, 122, 123, 124, 18, 131, 110};

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
  std::atomic_bool foreign_thread = false;  // a call on it from another thread came: every later call goes straight on
  std::atomic<int64_t> foreign_qpc = 0;     // when (0: none)
  // Set at Install before `context`; the rest only on the render thread.
  ID3D11Device* device = nullptr;  // not owned: the context's
  DWORD thread = 0;                // the render thread
  Observer observer = nullptr;
  bool hash_contents = false;  // the Unmaps' content hashes (they read write-combined memory: off for timing runs)
  uint32_t depth = 0;
  std::array<uint32_t, BODIES> entries = {};  // hook entries on the context and thread since Install, nested ones too
  uint32_t nested = 0;                        // of them, inside another hook's native body
  std::vector<OpenMap> open_maps;  // reserved at Install, never grown inside a body
  // Detours' trampolines (a body's own address until installed). A test seam: test/tw_map_trace puts a slow driver
  // between a replacement and its trampoline (only while no call runs).
  std::array<void*, BODIES> trampolines = {};
  std::vector<renodx::utils::detour::Function> hooks;
  tw::native_hooks::Lifetime lifetime;
};
static State state;

inline int64_t Qpc() {
  LARGE_INTEGER now = {};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}

// The replacements' guards, in order: another context (or none hooked) straight on; a call from another thread straight
// on, logged once, and every later call too (the device is single-threaded: never expected); a call inside another
// hook's native body straight on, counted. The feature path's observer (kept by the caller for after the native body),
// or null: straight on.
inline Observer Tracked(const void* context, size_t body) {
  if (context != state.context.load(std::memory_order_acquire)
      || state.foreign_thread.load(std::memory_order_relaxed)) {
    return nullptr;
  }
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
    return nullptr;
  }
  ++state.entries[body];
  if (state.depth != 0) {
    ++state.nested;
    return nullptr;
  }
  return state.observer;
}

// The native body (the trampoline), timed, with the depth raised: entries inside it are nested ones.
template <typename Native>
auto TimeNative(Call* call, Native native) {
  const uint32_t nested_before = state.nested;
  const auto finish = [call, nested_before] {
    call->done = Qpc();
    --state.depth;
    call->nested = state.nested - nested_before;
  };
  ++state.depth;
  call->qpc = Qpc();
  if constexpr (std::is_void_v<std::invoke_result_t<Native>>) {
    native();
    finish();
  } else {
    const auto result = native();
    finish();
    return result;
  }
}

inline HRESULT STDMETHODCALLTYPE Map(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
                                     D3D11_MAP map_type, UINT map_flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
  const auto native = reinterpret_cast<MapFunction>(state.trampolines[MAP]);
  const Observer observer = Tracked(context, MAP);
  if (observer == nullptr) return native(context, resource, subresource, map_type, map_flags, mapped);
  Call call = {.kind = Kind::MAP, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()), .resource = resource,
               .subresource = subresource, .map_type = map_type, .map_flags = map_flags};
  call.result = TimeNative(&call, [&] { return native(context, resource, subresource, map_type, map_flags, mapped); });
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
    // A constant buffer's map: one of the same subresource replaces the older (its unmap not seen here); a full list
    // takes no more.
    if ((desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0) {
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
  observer(call);
  return call.result;
}

inline void STDMETHODCALLTYPE Unmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) {
  const auto native = reinterpret_cast<UnmapFunction>(state.trampolines[UNMAP]);
  const Observer observer = Tracked(context, UNMAP);
  if (observer == nullptr) {
    native(context, resource, subresource);
    return;
  }
  Call call = {.kind = Kind::UNMAP, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()),
               .resource = resource, .subresource = subresource};
  // The bytes as the caller wrote them, before the native Unmap (its pointer is valid until then). Write-combined
  // memory: slow to read, a trace's cost outside the native time (timed: hash_ticks). A DISCARD's bytes the caller did
  // not write are undefined: the same upload can hash differently.
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
  TimeNative(&call, [&] { native(context, resource, subresource); });
  observer(call);
}

template <size_t STAGE>
void STDMETHODCALLTYPE SetConstantBuffers(ID3D11DeviceContext* context, UINT start_slot, UINT count,
                                          ID3D11Buffer* const* buffers) {
  const auto native = reinterpret_cast<SetConstantBuffersFunction>(state.trampolines[SET_CONSTANT_BUFFERS + STAGE]);
  const Observer observer = Tracked(context, SET_CONSTANT_BUFFERS + STAGE);
  if (observer == nullptr) {
    native(context, start_slot, count, buffers);
    return;
  }
  Call call = {.kind = Kind::SET_CONSTANT_BUFFERS, .stage = static_cast<Stage>(STAGE),
               .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()), .start_slot = start_slot, .count = count,
               .buffers = buffers};
  TimeNative(&call, [&] { native(context, start_slot, count, buffers); });
  observer(call);
}

template <size_t STAGE>
void STDMETHODCALLTYPE SetConstantBuffers1(ID3D11DeviceContext1* context, UINT start_slot, UINT count,
                                           ID3D11Buffer* const* buffers, const UINT* first_constants,
                                           const UINT* constant_counts) {
  const auto native = reinterpret_cast<SetConstantBuffers1Function>(state.trampolines[SET_CONSTANT_BUFFERS1 + STAGE]);
  const Observer observer = Tracked(context, SET_CONSTANT_BUFFERS1 + STAGE);
  if (observer == nullptr) {
    native(context, start_slot, count, buffers, first_constants, constant_counts);
    return;
  }
  Call call = {.kind = Kind::SET_CONSTANT_BUFFERS1, .stage = static_cast<Stage>(STAGE),
               .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()), .start_slot = start_slot, .count = count,
               .buffers = buffers, .first_constants = first_constants, .constant_counts = constant_counts};
  TimeNative(&call, [&] { native(context, start_slot, count, buffers, first_constants, constant_counts); });
  observer(call);
}

inline void STDMETHODCALLTYPE SetVertexBuffers(ID3D11DeviceContext* context, UINT start_slot, UINT count,
                                               ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets) {
  const auto native = reinterpret_cast<SetVertexBuffersFunction>(state.trampolines[SET_VERTEX_BUFFERS]);
  const Observer observer = Tracked(context, SET_VERTEX_BUFFERS);
  if (observer == nullptr) {
    native(context, start_slot, count, buffers, strides, offsets);
    return;
  }
  Call call = {.kind = Kind::SET_VERTEX_BUFFERS, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()),
               .start_slot = start_slot, .count = count, .buffers = buffers, .strides = strides, .offsets = offsets};
  TimeNative(&call, [&] { native(context, start_slot, count, buffers, strides, offsets); });
  observer(call);
}

// The active state tracked here: a swap back passes no out pointer (the add-on's passes), so the hook asks for the
// replaced state itself and lets it go. d3d11 applies the new state lazily: its swap stubs, at the next call on the
// context, call IASetVertexBuffers and each stage's SetConstantBuffers1 bodies from inside d3d11.dll
// (test/tw_context_hooks).
// One settling call here makes that application nested in this hook (passed on, not taken for the caller's binds).
inline void STDMETHODCALLTYPE SwapState(ID3D11DeviceContext1* context, ID3DDeviceContextState* new_state,
                                        ID3DDeviceContextState** previous_state) {
  const auto native = reinterpret_cast<SwapStateFunction>(state.trampolines[SWAP_STATE]);
  const Observer observer = Tracked(context, SWAP_STATE);
  if (observer == nullptr) {
    native(context, new_state, previous_state);
    return;
  }
  Call call = {.kind = Kind::SWAP_STATE, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress()),
               .state = new_state};
  ID3DDeviceContextState* replaced = nullptr;
  TimeNative(&call, [&] {
    native(context, new_state, (previous_state != nullptr ? previous_state : &replaced));
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->IAGetPrimitiveTopology(&topology);
    call.previous_state = (previous_state != nullptr ? *previous_state : replaced);
    if (replaced != nullptr) {
      replaced->Release();
    }
  });
  observer(call);
}

inline void STDMETHODCALLTYPE ClearState(ID3D11DeviceContext* context) {
  const auto native = reinterpret_cast<ClearStateFunction>(state.trampolines[CLEAR_STATE]);
  const Observer observer = Tracked(context, CLEAR_STATE);
  if (observer == nullptr) {
    native(context);
    return;
  }
  Call call = {.kind = Kind::CLEAR_STATE, .direct_caller = reinterpret_cast<uintptr_t>(_ReturnAddress())};
  TimeNative(&call, [&] { native(context); });
  observer(call);
}

}  // namespace internal

// The hooks out: the context let go first (every call goes straight on), then the detours removed. A removal that fails
// leaves them passing every call on, and this image loaded for the process (Lifetime). On the render thread (no call
// in flight), or at the add-on's detach.
inline void Uninstall() {
  auto& state = internal::state;
  state.context.store(nullptr);
  state.observer = nullptr;
  state.open_maps.clear();
  if (state.hooks.empty()) return;
  try {
    state.lifetime.Uninstall(&state.hooks);
  } catch (const std::exception& error) {
    renodx::utils::log::e(
        "[Total War] Context hooks could not be removed; they pass every call on and the add-on stays loaded: ",
        error.what());
  }
}

// On the render thread, `context` the game's immediate context below ReShade's proxy, and `observer` the measure path:
// the bodies in its table checked and detoured, the bindings it already holds given to `observer` as seeds, then its
// calls taken (`hash_contents`: with the constant buffers' content hashes). False (logged): nothing installed.
inline bool Install(ID3D11DeviceContext* context, Observer observer, bool hash_contents) {
  auto& state = internal::state;
  const auto refuse = [](std::string_view reason) {
    renodx::utils::log::w("[Total War] Context hooks: not installed (", reason, ").");
    return false;
  };
  if (!state.hooks.empty()) return refuse("an earlier removal failed");
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
    std::array<void*, internal::BODIES> bodies = {};
    for (size_t body = 0; body < bodies.size(); ++body) {
      bodies[body] = table[internal::BODY_SLOTS[body]];
    }
    return bodies;
  };
  const auto bodies = read_table();
  if (read_table() != bodies) return refuse("the context's table changed between two reads");
  // Each body inside the system's d3d11.dll (not a proxy named so, nor another module's hook), each once, none starting
  // with a jump (E9 rel32, EB rel8, FF 25 indirect, push imm32 / ret: another hook's).
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
  for (void* body : bodies) {
    HMODULE module = nullptr;
    const auto* code = static_cast<const uint8_t*>(body);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(body), &module)
            == FALSE
        || module != d3d11) {
      return refuse("a body outside d3d11.dll");
    }
    if (code[0] == 0xE9 || code[0] == 0xEB || (code[0] == 0xFF && code[1] == 0x25)
        || (code[0] == 0x68 && code[5] == 0xC3)) {
      return refuse(std::format("the body at d3d11.dll+0x{:x} starts with a jump",
                                reinterpret_cast<uintptr_t>(body) - reinterpret_cast<uintptr_t>(d3d11)));
    }
  }
  auto sorted = bodies;
  std::ranges::sort(sorted);
  if (std::ranges::adjacent_find(sorted) != sorted.end()) return refuse("two of the bodies are one function");

  const std::array<void*, internal::BODIES> replacements = {
      reinterpret_cast<void*>(&internal::Map),
      reinterpret_cast<void*>(&internal::Unmap),
      reinterpret_cast<void*>(&internal::SetConstantBuffers<0>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers<1>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers<2>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers<3>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers<4>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers<5>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers1<0>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers1<1>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers1<2>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers1<3>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers1<4>),
      reinterpret_cast<void*>(&internal::SetConstantBuffers1<5>),
      reinterpret_cast<void*>(&internal::SetVertexBuffers),
      reinterpret_cast<void*>(&internal::SwapState),
      reinterpret_cast<void*>(&internal::ClearState),
  };
  state.device = device.Get();
  state.thread = GetCurrentThreadId();
  state.observer = observer;
  state.hash_contents = hash_contents;
  state.depth = 0;
  state.entries = {};
  state.nested = 0;
  state.open_maps.clear();
  state.open_maps.reserve(64);
  state.foreign_thread.store(false);
  state.foreign_qpc.store(0);
  state.trampolines = bodies;
  for (size_t body = 0; body < internal::BODIES; ++body) {
    state.hooks.emplace_back(&state.trampolines[body], replacements[body],
                             renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE);
  }
  try {
    state.lifetime.Install(state.hooks);
  } catch (const std::exception& error) {
    state.hooks.clear();
    state.trampolines = {};
    return refuse(error.what());
  }
  // The seeds: the bindings from before (the game keeps buffers bound across frames), read straight from d3d11 (the Get
  // bodies are not hooked).
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
  state.context.store(context, std::memory_order_release);
  renodx::utils::log::i("[Total War] Context hooks: ", internal::BODIES,
                        " d3d11.dll bodies hooked on the game's context",
                        (hash_contents ? ", content hashes on." : "."));
  return true;
}

// The game's device's end (ReShade's destroy_device): the hooks out with it. The installing feature calls it from its
// own handler, under the lock it holds around its other removals. True: they were in and came out.
inline bool OnDestroyDevice(reshade::api::device* device) {
  if (internal::state.context.load() == nullptr || device->get_api() != reshade::api::device_api::d3d11) return false;
  if (reinterpret_cast<ID3D11Device*>(device->get_native()) != internal::state.device) return false;
  Uninstall();
  return true;
}

}  // namespace twattila::context_hooks
