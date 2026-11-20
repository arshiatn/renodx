/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"
#include "./context_hooks.hpp"

// Fewer Driver Waits (the constant buffer ring): the game's dynamic constant buffers go to the GPU through one ring of
// 256 KB pieces mapped NO_OVERWRITE and bound with D3D11.1 offsets, instead of a driver rename at each DISCARD map (the
// render thread's waits on the driver). Map hands the caller a CPU copy; Unmap (after ReShade's unmap event, so its
// handlers' edits are in) writes the copy into the current piece and rebinds every slot of the active state that holds
// the buffer. Set, Get and Copy translate between the game's buffer (all the caller ever sees) and its segment, per
// context state. The paths run inside context_hooks' replacements on the render thread; taken out (row off, another
// thread's call, a lost device, the add-on's detach), every buffer is written back into itself and every slot of every
// live state rebound to it first. Our own buffers are made below ReShade (no init_resource): never tracked.
namespace twattila::buffer_pool {

namespace internal {

namespace hooks = context_hooks::internal;
using context_hooks::Stage;
using context_hooks::STAGES;

constexpr UINT SLOTS = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
constexpr UINT UNIT_BYTES = 256;  // a segment's granularity: Set1's offset unit, 16 constants
constexpr UINT UNIT_CONSTANTS = UNIT_BYTES / 16;
constexpr UINT PIECE_BYTES = 256 * 1024;
constexpr UINT PIECE_UNITS = PIECE_BYTES / UNIT_BYTES;
constexpr size_t FIRST_PIECES = 4;
constexpr size_t MAX_PIECES = 32;
constexpr UINT MAX_BUFFER_BYTES = 65536;
constexpr UINT WHOLE = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;  // a whole bind's window as Get1 gives it: 0 + 4096
// Test seams: the most pieces (MAX_PIECES), and a piece's event as GetData(DONOTFLUSH) answers it.
static size_t piece_cap = MAX_PIECES;
static HRESULT (*piece_done)(ID3D11DeviceContext* context, ID3D11Query* query) = nullptr;

struct alignas(UNIT_BYTES) Unit {
  std::array<uint8_t, UNIT_BYTES> bytes = {};
};

struct Piece;

// A game buffer (selected at its creation). The render thread's, but `destroyed`.
struct Buffer {
  ID3D11Buffer* buffer = nullptr;  // not owned
  ID3D11Device* device = nullptr;  // not owned: its device (forgotten with it)
  UINT bytes = 0;
  UINT units = 0;
  std::vector<Unit> copy;   // while the ring is in: its bytes as the caller wrote them, padding zero
  bool copy_valid = false;  // `copy` holds its newest bytes (a NO_OVERWRITE map keeps the rest)
  bool mapped = false;      // mapped through the ring, not yet unmapped
  Piece* piece = nullptr;   // where its newest bytes are (null: in itself)
  UINT first = 0;           // there, in constants
  uint32_t live_index = 0;  // in that piece's live list
  std::array<uint16_t, STAGES> active = {};  // the active state's slots holding it, per stage
  std::atomic_bool destroyed = false;
};

// The buffers by address: open addressing, grown copy-on-write. The render thread looks up without a lock; writers (any
// thread's create and destroy, the present's reclaim) hold Pool::mutex. A replaced table and a destroyed record are
// freed at a present on the render thread (no lookup runs then).
struct Table {
  explicit Table(size_t capacity) : slots(capacity) {}
  std::vector<std::atomic<Buffer*>> slots;
  size_t used = 0;  // slots ever taken (tombstones too)
};
static Buffer tombstone;

inline size_t Home(const void* buffer, size_t capacity) {
  uint64_t key = reinterpret_cast<uintptr_t>(buffer) >> 4;
  key = (key ^ (key >> 17)) * 0x9E3779B97F4A7C15;
  return static_cast<size_t>(key >> 32) & (capacity - 1);
}

enum class Phase : uint8_t {
  FREE,     // the GPU is done with it (or never had it)
  CURRENT,  // being written
  CLOSED,   // full; some buffers' newest bytes still here
  ENDED,    // none left: its event ended, reused once the GPU passed it
};

struct Piece {
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  Microsoft::WRL::ComPtr<ID3D11Query> done;
  Phase phase = Phase::FREE;
  bool mapped_before = false;  // its first map is a DISCARD, later ones NO_OVERWRITE
  std::vector<Buffer*> live;   // the buffers whose newest bytes are here (reserved: never grown inside a body)
};

// A slot of a state as the caller sees it: a game buffer (one reference held) with the caller's window, and what is
// bound for it.
struct Slot {
  Buffer* buffer = nullptr;
  UINT first = 0;  // in constants (a whole bind: 0 + WHOLE)
  UINT count = 0;
  ID3D11Buffer* bound = nullptr;  // the buffer itself, a piece or the zero segment
  UINT bound_first = 0;
};

// A context state's slots (the add-on's private states have their own). Key null: the state active at install, until
// the first swap names it.
struct StateTable {
  ID3DDeviceContextState* key = nullptr;  // not owned
  std::shared_ptr<std::atomic_bool> dead = std::make_shared<std::atomic_bool>(false);
  bool noted = false;  // its state holds a DeathNote
  std::array<std::array<Slot, SLOTS>, STAGES> slots = {};
  std::array<uint16_t, STAGES> used = {};  // the slots holding a game buffer
};

// {4E1C5B7A-2D93-4F06-A8E1-37C0B5D9216F}: a state's private data, set when the state goes.
constexpr GUID DEATH_NOTE = {0x4e1c5b7a, 0x2d93, 0x4f06, {0xa8, 0xe1, 0x37, 0xc0, 0xb5, 0xd9, 0x21, 0x6f}};

// Held by a context state (its private data): the state's end sets `dead` (its table let go at a present).
class DeathNote final : public IUnknown {
 public:
  explicit DeathNote(std::shared_ptr<std::atomic_bool> dead) : dead_(std::move(dead)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (id != __uuidof(IUnknown)) return E_NOINTERFACE;
    *object = static_cast<IUnknown*>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = --references_;
    if (left == 0) {
      dead_->store(true);
      delete this;
    }
    return left;
  }

 private:
  std::atomic<ULONG> references_ = 1;
  std::shared_ptr<std::atomic_bool> dead_;
};

struct Pool {
  std::mutex mutex;  // the writers of `table`, `tables`, `records` and `copies`
  std::atomic<Table*> table = nullptr;
  std::vector<std::unique_ptr<Table>> tables;  // the current one last; replaced ones until a present
  std::vector<std::unique_ptr<Buffer>> records;
  bool copies = false;  // records get their copy (the ring is in)
  std::atomic_bool reclaim_wanted = false;
  // The render thread's.
  ID3D11Device* device = nullptr;           // not owned: the game's
  ID3D11DeviceContext1* context = nullptr;  // not owned: its immediate context, below ReShade
  bool installed = false;
  bool stopped = false;        // unwound: every call goes straight on (open ring maps finish in their buffers)
  bool unwind_wanted = false;  // the add-on's detach came off the render thread
  bool lost = false;           // a piece's map or event failed: the device is gone
  bool quiet = false;          // a debug frame: the full ring's line waits
  bool full_logged = false;
  uint32_t open_maps = 0;  // ring maps not yet unmapped
  std::vector<std::unique_ptr<Piece>> pieces;  // in ring order
  size_t current = 0;
  UINT next_unit = 0;  // in the current piece
  Microsoft::WRL::ComPtr<ID3D11Buffer> zero;  // the zero segment: a window past a buffer's end reads zeros
  std::vector<std::unique_ptr<StateTable>> states;
  StateTable* active = nullptr;
  ID3D11Device* refused = nullptr;  // the device the ring stays off on until the row is turned off (logged)
  bool registered = false;
};
static Pool pool;

template <typename Function>
Function Native(size_t body) {
  return reinterpret_cast<Function>(hooks::state.trampolines[body]);
}

// A game buffer's record (null: not one). Lock-free on the render thread; any thread under `pool.mutex`.
inline Buffer* Find(const ID3D11Resource* resource) {
  const Table* table = pool.table.load(std::memory_order_acquire);
  if (table == nullptr || resource == nullptr) return nullptr;
  const size_t mask = table->slots.size() - 1;
  for (size_t index = Home(resource, table->slots.size());; index = (index + 1) & mask) {
    Buffer* record = table->slots[index].load(std::memory_order_acquire);
    if (record == nullptr) return nullptr;
    if (record != &tombstone && static_cast<const ID3D11Resource*>(record->buffer) == resource) {
      return (record->destroyed.load(std::memory_order_relaxed) ? nullptr : record);
    }
  }
}

// Under `pool.mutex`: `record` into `table`, over the slot of the same address (a destroyed buffer's, its address
// reused before the reclaim) or in the first free one.
inline void Place(Table* table, Buffer* record) {
  const size_t mask = table->slots.size() - 1;
  size_t free = SIZE_MAX;
  for (size_t index = Home(record->buffer, table->slots.size());; index = (index + 1) & mask) {
    Buffer* held = table->slots[index].load(std::memory_order_relaxed);
    if (held == nullptr) {
      if (free == SIZE_MAX) {
        free = index;
        ++table->used;
      }
      break;
    }
    if (held == &tombstone) {
      free = std::min(free, index);
    } else if (held->buffer == record->buffer) {
      free = index;
      break;
    }
  }
  table->slots[free].store(record, std::memory_order_release);
}

// Under `pool.mutex`: `record` in, the table grown (copied) first at half full. May throw (no memory).
inline void Insert(Buffer* record) {
  Table* table = pool.table.load(std::memory_order_relaxed);
  if (table == nullptr || (table->used + 1) * 2 > table->slots.size()) {
    size_t live = 0;
    if (table != nullptr) {
      live = static_cast<size_t>(std::ranges::count_if(table->slots, [](const std::atomic<Buffer*>& slot) {
        Buffer* held = slot.load(std::memory_order_relaxed);
        return held != nullptr && held != &tombstone;
      }));
    }
    size_t capacity = 1024;
    while (capacity < (live + 1) * 4) capacity *= 2;
    auto grown = std::make_unique<Table>(capacity);
    pool.tables.reserve(pool.tables.size() + 1);
    if (table != nullptr) {
      for (const auto& slot : table->slots) {
        if (Buffer* held = slot.load(std::memory_order_relaxed); held != nullptr && held != &tombstone) {
          Place(grown.get(), held);
        }
      }
    }
    table = grown.get();
    pool.tables.push_back(std::move(grown));
    pool.table.store(table, std::memory_order_release);
  }
  Place(table, record);
}

// Where a bind of `record` with the caller's window goes now: the buffer itself, its segment (the window clamped to the
// segment, padding zero) or, a window from past its end, the zero segment (D3D11 reads zeros past a buffer's end).
struct Bound {
  ID3D11Buffer* buffer = nullptr;
  UINT first = 0;
  UINT count = 0;
};

inline Bound Translate(const Buffer& record, UINT first, UINT count) {
  if (record.piece == nullptr) return {record.buffer, first, count};
  const UINT constants = record.units * UNIT_CONSTANTS;
  if (first >= constants) return {pool.zero.Get(), 0, std::min(count, UNIT_CONSTANTS)};
  return {record.piece->buffer.Get(), record.first + first, std::min(count, constants - first)};
}

inline void BindSlot(size_t stage, UINT slot, const Bound& bound) {
  Native<hooks::SetConstantBuffers1Function>(hooks::SET_CONSTANT_BUFFERS1 + stage)(pool.context, slot, 1, &bound.buffer,
                                                                                    &bound.first, &bound.count);
}

// Every slot of the active state that holds `record`, bound to where its bytes are now.
inline void Rebind(const Buffer& record) {
  for (size_t stage = 0; stage < STAGES; ++stage) {
    for (uint32_t slots = record.active[stage]; slots != 0; slots &= slots - 1) {
      const auto slot = static_cast<UINT>(std::countr_zero(slots));
      Slot& entry = pool.active->slots[stage][slot];
      const Bound bound = Translate(record, entry.first, entry.count);
      if (bound.buffer != entry.bound || bound.first != entry.bound_first) {
        BindSlot(stage, slot, bound);
        entry.bound = bound.buffer;
        entry.bound_first = bound.first;
      }
    }
  }
}

// The active state's slot as now bound: `record` (null: none of the game's) with the caller's window. The new buffer's
// reference is taken before the old one's goes (the same buffer may be both).
inline void Assign(size_t stage, UINT slot, Buffer* record, UINT first, UINT count, const Bound& bound) {
  Slot& entry = pool.active->slots[stage][slot];
  const auto bit = static_cast<uint16_t>(1u << slot);
  Buffer* old = entry.buffer;
  if (record != nullptr) {
    record->buffer->AddRef();
  }
  if (old != nullptr) {
    old->active[stage] &= static_cast<uint16_t>(~bit);
  }
  entry = {.buffer = record, .first = first, .count = count, .bound = bound.buffer, .bound_first = bound.first};
  if (record != nullptr) {
    record->active[stage] |= bit;
    pool.active->used[stage] |= bit;
  } else {
    pool.active->used[stage] &= static_cast<uint16_t>(~bit);
  }
  if (old != nullptr) {
    old->buffer->Release();
  }
}

// A stage's slots [start, start + count) of the active state taken from d3d11 (Get1, native): an entry stays where its
// bind is still there (d3d11's own application binds what was bound); else the bind is taken as the caller's (a game
// buffer's: rebound to its segment when its bytes are in the ring).
inline void Seed(size_t stage, UINT start, UINT count) {
  std::array<ID3D11Buffer*, SLOTS> buffers = {};
  std::array<UINT, SLOTS> firsts = {};
  std::array<UINT, SLOTS> counts = {};
  Native<hooks::GetConstantBuffers1Function>(hooks::GET_CONSTANT_BUFFERS1 + stage)(
      pool.context, start, count, buffers.data(), firsts.data(), counts.data());
  for (UINT index = 0; index < count; ++index) {
    const UINT slot = start + index;
    const Slot& entry = pool.active->slots[stage][slot];
    if (entry.buffer == nullptr || entry.bound != buffers[index] || entry.bound_first != firsts[index]) {
      Buffer* record = Find(buffers[index]);
      Assign(stage, slot, record, firsts[index], counts[index], {buffers[index], firsts[index], counts[index]});
      if (record != nullptr && record->piece != nullptr) {
        Rebind(*record);
      }
    }
    if (buffers[index] != nullptr) {
      buffers[index]->Release();
    }
  }
}

// `record`'s bytes leave its piece: a closed piece that holds no buffer's newest bytes now gets its event.
inline void Detach(Buffer* record) {
  Piece& piece = *record->piece;
  Buffer* last = piece.live.back();
  piece.live[record->live_index] = last;
  last->live_index = record->live_index;
  piece.live.pop_back();
  record->piece = nullptr;
  if (piece.phase == Phase::CLOSED && piece.live.empty()) {
    pool.context->End(piece.done.Get());
    piece.phase = Phase::ENDED;
  }
}

// `record`'s copy into the buffer itself: a native DISCARD map (today's wait). A failed map: the device is gone.
inline void WriteNative(const Buffer& record) {
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(Native<hooks::MapFunction>(hooks::MAP)(pool.context, record.buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                                                     &mapped))) {
    pool.lost = true;
    return;
  }
  std::memcpy(mapped.pData, record.copy.data(), record.bytes);
  Native<hooks::UnmapFunction>(hooks::UNMAP)(pool.context, record.buffer, 0);
}

// `record` back in itself (its copy written there) and every active slot holding it rebound to it.
inline void ToNative(Buffer* record) {
  WriteNative(*record);
  if (record->piece != nullptr) {
    Detach(record);
  }
  Rebind(*record);
}

inline bool MakePiece(std::unique_ptr<Piece>* made) {
  try {
    auto piece = std::make_unique<Piece>();
    piece->live.reserve(PIECE_UNITS);
    const D3D11_BUFFER_DESC desc = {.ByteWidth = PIECE_BYTES, .Usage = D3D11_USAGE_DYNAMIC,
                                    .BindFlags = D3D11_BIND_CONSTANT_BUFFER, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    const D3D11_QUERY_DESC query = {.Query = D3D11_QUERY_EVENT};
    if (FAILED(pool.device->CreateBuffer(&desc, nullptr, piece->buffer.GetAddressOf()))
        || FAILED(pool.device->CreateQuery(&query, piece->done.GetAddressOf()))) {
      return false;
    }
    *made = std::move(piece);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

inline bool Upload(Buffer* record);

// The next piece to write: the one after the current once the GPU passed it, else a new one between them (the ring
// grows while the oldest is busy, up to piece_cap). Then the oldest closed piece's buffers move into it while it keeps
// `reserve` units free, so that piece's event ends a lap before its reuse. False: full (or the device lost).
inline bool Advance(UINT reserve) {
  // Closed once (a full ring leaves it current in name, closed or ended, until a piece frees).
  if (Piece& closing = *pool.pieces[pool.current]; closing.phase == Phase::CURRENT) {
    if (closing.live.empty()) {
      pool.context->End(closing.done.Get());
      closing.phase = Phase::ENDED;
    } else {
      closing.phase = Phase::CLOSED;
    }
  }
  size_t next = (pool.current + 1) % pool.pieces.size();
  Piece& candidate = *pool.pieces[next];
  if (candidate.phase == Phase::ENDED) {
    const HRESULT done = (piece_done != nullptr ? piece_done(pool.context, candidate.done.Get())
                                                : pool.context->GetData(candidate.done.Get(), nullptr, 0,
                                                                        D3D11_ASYNC_GETDATA_DONOTFLUSH));
    if (done == S_OK) {
      candidate.phase = Phase::FREE;
    } else if (done != S_FALSE) {
      pool.lost = true;
    }
  }
  if (candidate.phase != Phase::FREE) {
    std::unique_ptr<Piece> grown;
    if (pool.lost || pool.pieces.size() >= piece_cap || !MakePiece(&grown)) return false;
    next = pool.current + 1;
    pool.pieces.insert(pool.pieces.begin() + static_cast<std::ptrdiff_t>(next), std::move(grown));
  }
  pool.current = next;
  pool.next_unit = 0;
  pool.pieces[next]->phase = Phase::CURRENT;
  Piece& oldest = *pool.pieces[(next + 1) % pool.pieces.size()];
  if (oldest.phase == Phase::CLOSED) {
    // From the back: Detach moves the last one into the slot it frees.
    for (size_t index = oldest.live.size(); index-- > 0 && index < oldest.live.size();) {
      Buffer* moved = oldest.live[index];
      if (moved->mapped) continue;
      if (pool.next_unit + moved->units + reserve > PIECE_UNITS || !Upload(moved)) break;
    }
  }
  return true;
}

// `record`'s copy into the current piece (the next one when this has no room), every active slot holding it rebound.
// False: the ring is full or the device lost (the caller falls back to the buffer itself).
inline bool Upload(Buffer* record) {
  if (pool.next_unit + record->units > PIECE_UNITS && !Advance(record->units)) return false;
  Piece& piece = *pool.pieces[pool.current];
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(Native<hooks::MapFunction>(hooks::MAP)(
          pool.context, piece.buffer.Get(), 0,
          (piece.mapped_before ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD), 0, &mapped))) {
    pool.lost = true;
    return false;
  }
  piece.mapped_before = true;
  auto* target = static_cast<uint8_t*>(mapped.pData) + (size_t{pool.next_unit} * UNIT_BYTES);
  std::memcpy(target, record->copy.data(), record->bytes);
  std::memset(target + record->bytes, 0, (size_t{record->units} * UNIT_BYTES) - record->bytes);
  Native<hooks::UnmapFunction>(hooks::UNMAP)(pool.context, piece.buffer.Get(), 0);
  if (record->piece != nullptr) {
    Detach(record);
  }
  record->piece = &piece;
  record->first = pool.next_unit * UNIT_CONSTANTS;
  record->live_index = static_cast<uint32_t>(piece.live.size());
  piece.live.push_back(record);
  pool.next_unit += record->units;
  Rebind(*record);
  return true;
}

// The active state's slots back on the game's buffers (each bound as the caller bound it, where d3d11 still holds what
// was bound for it; else left as it is), their references let go.
inline void Restore(StateTable* table) {
  for (size_t stage = 0; stage < STAGES; ++stage) {
    if (table->used[stage] == 0) continue;
    std::array<ID3D11Buffer*, SLOTS> buffers = {};
    std::array<UINT, SLOTS> firsts = {};
    std::array<UINT, SLOTS> counts = {};
    Native<hooks::GetConstantBuffers1Function>(hooks::GET_CONSTANT_BUFFERS1 + stage)(
        pool.context, 0, SLOTS, buffers.data(), firsts.data(), counts.data());
    for (UINT slot = 0; slot < SLOTS; ++slot) {
      Slot& entry = table->slots[stage][slot];
      if (entry.buffer != nullptr) {
        if (buffers[slot] == entry.bound && firsts[slot] == entry.bound_first) {
          BindSlot(stage, slot, {entry.buffer->buffer, entry.first, entry.count});
        }
        entry.buffer->active[stage] = 0;
        entry.buffer->buffer->Release();
        entry = {};
      }
      if (buffers[slot] != nullptr) {
        buffers[slot]->Release();
      }
    }
    table->used[stage] = 0;
  }
}

// A state's table let go: its references, and its state's note (the state still alive).
inline void Drop(StateTable* table) {
  for (size_t stage = 0; stage < STAGES; ++stage) {
    for (Slot& entry : table->slots[stage]) {
      if (entry.buffer != nullptr) {
        if (table == pool.active) {
          entry.buffer->active[stage] = 0;
        }
        entry.buffer->buffer->Release();
        entry = {};
      }
    }
    table->used[stage] = 0;
  }
  if (table->noted && !table->dead->load()) {
    table->key->SetPrivateDataInterface(DEATH_NOTE, nullptr);
  }
  table->noted = false;
}

// The ring out of every state: each buffer's bytes back in itself (open ring maps finish there at their Unmaps), every
// live state's slots rebound to the game's buffers (the others swapped in natively and back), the tables, pieces and
// copies let go. Calls go straight on from now. The depth raised (d3d11's application of a swapped state is nested).
inline void Unwind() {
  if (pool.stopped) return;
  pool.stopped = true;
  for (const auto& piece : pool.pieces) {
    for (Buffer* record : piece->live) {
      if (!record->mapped && !pool.lost) {
        WriteNative(*record);
      }
      record->piece = nullptr;
    }
    piece->live.clear();
  }
  if (pool.active != nullptr) {
    Restore(pool.active);
    for (const auto& table : pool.states) {
      if (table.get() == pool.active || table->key == nullptr || table->dead->load()) continue;
      ID3DDeviceContextState* back = nullptr;
      ID3DDeviceContextState* swapped = nullptr;
      const auto swap = Native<hooks::SwapStateFunction>(hooks::SWAP_STATE);
      D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      swap(pool.context, table->key, &back);
      pool.context->IAGetPrimitiveTopology(&topology);
      Restore(table.get());
      swap(pool.context, back, &swapped);
      pool.context->IAGetPrimitiveTopology(&topology);
      if (swapped != nullptr) swapped->Release();
      if (back != nullptr) back->Release();
    }
  }
  for (const auto& table : pool.states) {
    Drop(table.get());
  }
  pool.states.clear();
  pool.active = nullptr;
  pool.pieces.clear();
  pool.zero.Reset();
}

// A call from another thread, a lost device or a detach off the render thread: the ring unwinds at once. True: calls
// go straight on.
inline bool Stopped() {
  if (!pool.stopped
      && (pool.lost || pool.unwind_wanted || hooks::state.foreign_thread.load(std::memory_order_relaxed))) {
    Unwind();
  }
  return pool.stopped;
}

// Map of a game buffer for writing (DISCARD, or NO_OVERWRITE with its copy valid): its CPU copy, pitches its size.
// Flags (DO_NOT_WAIT): the buffer itself, its bytes there first.
inline bool RingMap(ID3D11Resource* resource, UINT subresource, D3D11_MAP map_type, UINT map_flags,
                    D3D11_MAPPED_SUBRESOURCE* mapped, HRESULT* result) {
  if (Stopped()) return false;
  Buffer* record = Find(resource);
  if (record == nullptr || subresource != 0 || mapped == nullptr || record->copy.empty()
      || (map_type != D3D11_MAP_WRITE_DISCARD && map_type != D3D11_MAP_WRITE_NO_OVERWRITE)) {
    return false;
  }
  if (map_flags != 0) {
    if (record->piece != nullptr) {
      ToNative(record);
    }
    record->copy_valid = false;
    return false;
  }
  // Its newest bytes are in itself (none in the copy): the native pair.
  if (map_type == D3D11_MAP_WRITE_NO_OVERWRITE && !record->copy_valid) return false;
  *mapped = {.pData = record->copy.data(), .RowPitch = record->bytes, .DepthPitch = record->bytes};
  if (!record->mapped) {
    record->mapped = true;
    ++pool.open_maps;
  }
  if (map_type == D3D11_MAP_WRITE_DISCARD) {
    record->copy_valid = true;
  }
  *result = S_OK;
  return true;
}

inline bool RingUnmap(ID3D11Resource* resource, UINT subresource) {
  const bool stopped = Stopped();
  Buffer* record = Find(resource);
  if (record == nullptr || subresource != 0 || !record->mapped) return false;
  record->mapped = false;
  --pool.open_maps;
  if (stopped) {
    WriteNative(*record);
  } else if (!Upload(record)) {
    // The ring full: today's DISCARD of the buffer itself, bound as the caller bound it.
    ToNative(record);
    if (!pool.lost && !pool.quiet && !pool.full_logged) {
      pool.full_logged = true;
      try {
        renodx::utils::log::w("[Total War] Fewer Driver Waits: all ", pool.pieces.size(),
                              " pieces still in use by the GPU: constants written the old way meanwhile.");
      } catch (const std::exception&) {
        // No-op: the fallback stands without its log line.
      }
    }
    Stopped();
  }
  return true;
}

// Set and Set1: a game buffer bound to its segment (the caller's window translated), the stage's entries updated. An
// invalid window or slot range (the runtime drops such a call), half a window or no array: the native call, its slots
// read back.
inline bool RingSet(Stage stage_value, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* firsts,
                    const UINT* counts) {
  if (Stopped() || start >= SLOTS || count == 0 || buffers == nullptr) return false;
  const auto stage = static_cast<size_t>(stage_value);
  const UINT bound_count = std::min(count, SLOTS - start);
  bool odd = count > SLOTS - start || (firsts == nullptr) != (counts == nullptr);
  for (UINT index = 0; !odd && firsts != nullptr && index < count; ++index) {
    odd = (firsts[index] % UNIT_CONSTANTS) != 0 || (counts[index] % UNIT_CONSTANTS) != 0 || counts[index] > WHOLE;
  }
  if (odd) {
    Native<hooks::SetConstantBuffers1Function>(hooks::SET_CONSTANT_BUFFERS1 + stage)(pool.context, start, count,
                                                                                      buffers, firsts, counts);
    Seed(stage, start, bound_count);
    return true;
  }
  std::array<Buffer*, SLOTS> records = {};
  bool any = (pool.active->used[stage] & (((1u << bound_count) - 1) << start)) != 0;
  for (UINT index = 0; index < bound_count; ++index) {
    records[index] = Find(buffers[index]);
    any = any || records[index] != nullptr;
  }
  if (!any) return false;
  std::array<ID3D11Buffer*, SLOTS> bound_buffers = {};
  std::array<UINT, SLOTS> bound_firsts = {};
  std::array<UINT, SLOTS> bound_counts = {};
  for (UINT index = 0; index < bound_count; ++index) {
    const UINT first = (firsts != nullptr ? firsts[index] : 0);
    const UINT constants = (firsts != nullptr ? counts[index] : WHOLE);
    const Bound bound =
        (records[index] != nullptr ? Translate(*records[index], first, constants) : Bound{buffers[index], first, constants});
    bound_buffers[index] = bound.buffer;
    bound_firsts[index] = bound.first;
    bound_counts[index] = bound.count;
  }
  Native<hooks::SetConstantBuffers1Function>(hooks::SET_CONSTANT_BUFFERS1 + stage)(
      pool.context, start, bound_count, bound_buffers.data(), bound_firsts.data(), bound_counts.data());
  for (UINT index = 0; index < bound_count; ++index) {
    Assign(stage, start + index, records[index], (firsts != nullptr ? firsts[index] : 0),
           (firsts != nullptr ? counts[index] : WHOLE),
           {bound_buffers[index], bound_firsts[index], bound_counts[index]});
  }
  return true;
}

// After the native Get and Get1: the game's buffer for its segment (a reference the caller's), the caller's window.
inline void RingGet(Stage stage_value, UINT start, UINT count, ID3D11Buffer** buffers, UINT* firsts, UINT* counts) {
  if (!pool.stopped && pool.active != nullptr && start < SLOTS) {
    const auto stage = static_cast<size_t>(stage_value);
    for (UINT index = 0; index < std::min(count, SLOTS - start); ++index) {
      const Slot& entry = pool.active->slots[stage][start + index];
      if (entry.buffer == nullptr) continue;
      if (buffers != nullptr) {
        if (buffers[index] != nullptr) {
          buffers[index]->Release();
        }
        buffers[index] = entry.buffer->buffer;
        buffers[index]->AddRef();
      }
      if (firsts != nullptr) firsts[index] = entry.first;
      if (counts != nullptr) counts[index] = entry.count;
    }
  }
  Stopped();
}

// A copy into a game buffer: its bytes back in itself first (the copy lands there; its CPU copy no longer the newest).
inline void CopyInto(ID3D11Resource* destination) {
  if (Buffer* record = Find(destination); record != nullptr) {
    if (record->piece != nullptr) {
      ToNative(record);
    }
    record->copy_valid = false;
  }
}

// CopySubresourceRegion(1) from a game buffer whose bytes are in the ring: from its segment (the box moved by the
// segment's offset), through the context's table (nested: the version of the body d3d11 has in it now). An invalid
// box: the native call as made.
inline bool RingCopy(ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
                     ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, const UINT* copy_flags) {
  if (Stopped()) return false;
  CopyInto(destination);
  const Buffer* record = Find(source);
  if (record == nullptr || record->piece == nullptr || source_subresource != 0) return false;
  const UINT offset = record->first * 16;
  D3D11_BOX moved = {.left = offset, .top = 0, .front = 0, .right = offset + record->bytes, .bottom = 1, .back = 1};
  if (box != nullptr) {
    if (box->left >= box->right || box->right > record->bytes || box->top != 0 || box->bottom != 1 || box->front != 0
        || box->back != 1) {
      return false;
    }
    moved.left = offset + box->left;
    moved.right = offset + box->right;
  }
  if (copy_flags != nullptr) {
    pool.context->CopySubresourceRegion1(destination, destination_subresource, x, y, z, record->piece->buffer.Get(),
                                         0, &moved, *copy_flags);
  } else {
    pool.context->CopySubresourceRegion(destination, destination_subresource, x, y, z, record->piece->buffer.Get(), 0,
                                        &moved);
  }
  return true;
}

// CopyResource from a game buffer whose bytes are in the ring, into another buffer of its size: its segment's range
// (through the table, as above).
inline bool RingCopyResource(ID3D11Resource* destination, ID3D11Resource* source) {
  if (Stopped()) return false;
  CopyInto(destination);
  const Buffer* record = Find(source);
  if (record == nullptr || record->piece == nullptr || destination == source || destination == nullptr) return false;
  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  destination->GetType(&dimension);
  if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) return false;
  D3D11_BUFFER_DESC desc = {};
  static_cast<ID3D11Buffer*>(destination)->GetDesc(&desc);
  if (desc.ByteWidth != record->bytes) return false;
  const UINT offset = record->first * 16;
  const D3D11_BOX moved = {.left = offset, .top = 0, .front = 0, .right = offset + record->bytes, .bottom = 1,
                           .back = 1};
  pool.context->CopySubresourceRegion(destination, 0, 0, 0, 0, record->piece->buffer.Get(), 0, &moved);
  return true;
}

// The state's end sets the table's dead flag (the note in its private data). No memory: never noticed (its table kept
// until the ring goes).
inline void Note(StateTable* table) {
  if (table->key == nullptr || table->noted) return;
  try {
    auto* note = new DeathNote(table->dead);
    table->noted = SUCCEEDED(table->key->SetPrivateDataInterface(DEATH_NOTE, note));
    note->Release();
  } catch (const std::bad_alloc&) {
    // No-op: the table stays until the ring goes.
  }
}

// After the native swap and its settling call: the replaced state's table named, the new state's found (its slots on
// the segments as they are now) or made from d3d11's binds.
inline void RingSwap(ID3DDeviceContextState* state, ID3DDeviceContextState* previous_state) {
  if (pool.stopped) return;
  StateTable* old = pool.active;
  if (old->key == nullptr && previous_state != nullptr) {
    old->key = previous_state;
    Note(old);
  }
  for (size_t stage = 0; stage < STAGES; ++stage) {
    for (uint32_t slots = old->used[stage]; slots != 0; slots &= slots - 1) {
      old->slots[stage][std::countr_zero(slots)].buffer->active[stage] = 0;
    }
  }
  const auto found = std::ranges::find_if(pool.states, [state](const std::unique_ptr<StateTable>& table) {
    return table->key == state && state != nullptr && !table->dead->load();
  });
  if (found == pool.states.end()) {
    try {
      pool.states.push_back(std::make_unique<StateTable>());
    } catch (const std::bad_alloc&) {
      pool.lost = true;
      pool.active = old;
      Stopped();
      return;
    }
    pool.active = pool.states.back().get();
    pool.active->key = state;
    Note(pool.active);
    for (size_t stage = 0; stage < STAGES; ++stage) {
      Seed(stage, 0, SLOTS);
    }
  } else {
    pool.active = found->get();
    for (size_t stage = 0; stage < STAGES; ++stage) {
      for (uint32_t slots = pool.active->used[stage]; slots != 0; slots &= slots - 1) {
        const auto slot = static_cast<UINT>(std::countr_zero(slots));
        Slot& entry = pool.active->slots[stage][slot];
        entry.buffer->active[stage] |= static_cast<uint16_t>(1u << slot);
        const Bound bound = Translate(*entry.buffer, entry.first, entry.count);
        if (bound.buffer != entry.bound || bound.first != entry.bound_first) {
          BindSlot(stage, slot, bound);
          entry.bound = bound.buffer;
          entry.bound_first = bound.first;
        }
      }
    }
  }
  Stopped();
}

// After the native ClearState: the active state's slots are empty.
inline void RingClear() {
  if (pool.stopped) return;
  for (size_t stage = 0; stage < STAGES; ++stage) {
    for (uint32_t slots = pool.active->used[stage]; slots != 0; slots &= slots - 1) {
      Assign(stage, static_cast<UINT>(std::countr_zero(slots)), nullptr, 0, 0, {});
    }
  }
  Stopped();
}

// The paths given to context_hooks (a test seam: a control replaces one).
static context_hooks::Ring ring_paths = {.map = RingMap, .unmap = RingUnmap, .set = RingSet, .get = RingGet,
                                         .copy = RingCopy, .copy_resource = RingCopyResource, .swap = RingSwap,
                                         .clear = RingClear};

// The ring's own objects and the copies let go after an unwind or at the device's end (no call on the context).
inline void Release() {
  for (const auto& piece : pool.pieces) {
    for (Buffer* record : piece->live) {
      record->piece = nullptr;
    }
  }
  for (const auto& table : pool.states) {
    Drop(table.get());
  }
  pool.states.clear();
  pool.active = nullptr;
  pool.pieces.clear();
  pool.zero.Reset();
  pool.installed = false;
  pool.stopped = false;
  pool.unwind_wanted = false;
  pool.lost = false;
  pool.open_maps = 0;
  pool.full_logged = false;
  pool.context = nullptr;
  pool.device = nullptr;
  const std::lock_guard lock(pool.mutex);
  pool.copies = false;
  for (const auto& record : pool.records) {
    record->copy = {};
    record->copy_valid = false;
    record->mapped = false;
    record->active = {};
  }
}

// The ring in on the game's immediate context (below ReShade), on the render thread: its pieces made, the game's
// buffers' copies, the hooks, the active state's slots seeded. False (logged): off.
inline bool Install(ID3D11DeviceContext* context) {
  const auto refuse = [](std::string_view reason) {
    renodx::utils::log::w("[Total War] Fewer Driver Waits: off (", reason, ").");
    return false;
  };
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
  if (FAILED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)))
      || options.ConstantBufferOffsetting == FALSE || options.MapNoOverwriteOnDynamicConstantBuffer == FALSE) {
    return refuse("the driver has no constant buffer offsets");
  }
  Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return refuse("no D3D11.1 context");
  pool.device = device.Get();
  pool.context = context1.Get();
  const auto fail = [&](std::string_view reason) {
    Release();
    return refuse(reason);
  };
  try {
    pool.pieces.reserve(MAX_PIECES);
    pool.states.reserve(8);
    for (size_t index = 0; index < FIRST_PIECES; ++index) {
      std::unique_ptr<Piece> piece;
      if (!MakePiece(&piece)) return fail("no room for its pieces");
      pool.pieces.push_back(std::move(piece));
    }
    const std::array<uint8_t, UNIT_BYTES> zeros = {};
    const D3D11_BUFFER_DESC zero_desc = {.ByteWidth = UNIT_BYTES, .Usage = D3D11_USAGE_IMMUTABLE,
                                         .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA zero_data = {.pSysMem = zeros.data()};
    if (FAILED(device->CreateBuffer(&zero_desc, &zero_data, pool.zero.GetAddressOf()))) {
      return fail("no room for its pieces");
    }
    pool.states.push_back(std::make_unique<StateTable>());
    const std::lock_guard lock(pool.mutex);
    for (const auto& record : pool.records) {
      record->copy.assign(record->units, {});
      record->copy_valid = false;
    }
    pool.copies = true;
  } catch (const std::bad_alloc&) {
    return fail("no memory");
  }
  if (!context_hooks::InstallRing(context, &ring_paths)) return fail("its hooks");
  pool.active = pool.states.front().get();
  pool.current = 0;
  pool.next_unit = 0;
  pool.pieces.front()->phase = Phase::CURRENT;
  pool.installed = true;
  ++hooks::state.depth;
  for (size_t stage = 0; stage < STAGES; ++stage) {
    Seed(stage, 0, SLOTS);
  }
  --hooks::state.depth;
  renodx::utils::log::i("[Total War] Fewer Driver Waits: on (", FIRST_PIECES, " pieces of 256 KB, up to ", MAX_PIECES,
                        ").");
  return true;
}

// The ring out on the render thread: unwound (the depth raised), its hooks and objects let go.
inline void Remove() {
  ++hooks::state.depth;
  Unwind();
  --hooks::state.depth;
  context_hooks::UninstallRing();
  Release();
}

}  // namespace internal

// A buffer made on `device` (any thread; ReShade's init_resource, below its proxy for the game's buffers only): a game
// buffer when dynamic, a constant buffer alone, CPU write, no misc flags, at most 64 KB; its copy filled from the
// initial data while the ring is in.
inline void Track(ID3D11Device* device, ID3D11Buffer* buffer, const void* initial_data) {
  using internal::pool;
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (desc.Usage != D3D11_USAGE_DYNAMIC || desc.BindFlags != D3D11_BIND_CONSTANT_BUFFER
      || desc.CPUAccessFlags != D3D11_CPU_ACCESS_WRITE || desc.MiscFlags != 0 || desc.ByteWidth == 0
      || desc.ByteWidth > internal::MAX_BUFFER_BYTES) {
    return;
  }
  const std::lock_guard lock(pool.mutex);
  try {
    pool.records.reserve(pool.records.size() + 1);
    auto record = std::make_unique<internal::Buffer>();
    record->buffer = buffer;
    record->device = device;
    record->bytes = desc.ByteWidth;
    record->units = (desc.ByteWidth + internal::UNIT_BYTES - 1) / internal::UNIT_BYTES;
    if (pool.copies) {
      record->copy.resize(record->units);
      if (initial_data != nullptr) {
        std::memcpy(record->copy.data(), initial_data, record->bytes);
        record->copy_valid = true;
      }
    }
    pool.records.push_back(std::move(record));
    try {
      internal::Insert(pool.records.back().get());
    } catch (const std::bad_alloc&) {
      pool.records.pop_back();
    }
  } catch (const std::bad_alloc&) {
    // No-op: the buffer stays the game's own (its maps native).
  }
}

// A buffer's end (any thread, also inside a D3D11 call): its record marked; the render thread frees it at a present.
inline void Forget(const ID3D11Resource* resource) {
  using internal::pool;
  const std::lock_guard lock(pool.mutex);
  if (internal::Buffer* record = internal::Find(resource); record != nullptr) {
    record->destroyed.store(true);
    pool.reclaim_wanted.store(true);
  }
}

// A device's end: the ring on it out (its references and objects let go, its hooks out; no call on the context), its
// buffers' records forgotten (freed at a present).
inline void ForgetDevice(ID3D11Device* device) {
  using internal::pool;
  if (pool.refused == device) {
    pool.refused = nullptr;
  }
  if (pool.installed && pool.device == device) {
    context_hooks::UninstallRing();
    internal::Release();
  }
  const std::lock_guard lock(pool.mutex);
  for (const auto& record : pool.records) {
    if (record->device == device) {
      record->destroyed.store(true);
      pool.reclaim_wanted.store(true);
    }
  }
}

inline void OnInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc,
                           const reshade::api::subresource_data* initial_data, reshade::api::resource_usage,
                           reshade::api::resource resource) {
  if (device->get_api() == reshade::api::device_api::d3d11 && desc.type == reshade::api::resource_type::buffer
      && desc.buffer.size <= internal::MAX_BUFFER_BYTES) {
    Track(reinterpret_cast<ID3D11Device*>(device->get_native()), reinterpret_cast<ID3D11Buffer*>(resource.handle),
          (initial_data != nullptr ? initial_data->data : nullptr));
  }
}

inline void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource) {
  if (device->get_api() == reshade::api::device_api::d3d11) {
    Forget(reinterpret_cast<const ID3D11Resource*>(resource.handle));
  }
}

inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() == reshade::api::device_api::d3d11) {
    ForgetDevice(reinterpret_cast<ID3D11Device*>(device->get_native()));
  }
}

// At each present, on the present thread (the render thread in Attila), `context` the game's immediate context below
// ReShade: destroyed buffers and dead states' tables freed; the ring in when `on` (its first present on, again after the
// row was off when it stopped), out when off or stopped (with no ring map open). `quiet`: a debug frame (its line waits).
inline void Update(ID3D11DeviceContext* context, bool on, bool quiet) {
  using internal::pool;
  pool.quiet = quiet;
  if (pool.installed && GetCurrentThreadId() != internal::hooks::state.thread) return;
  if (pool.reclaim_wanted.exchange(false)) {
    const std::lock_guard lock(pool.mutex);
    for (const auto& record : pool.records) {
      if (!record->destroyed.load()) continue;
      // Not one in a slot (each holds a reference); in a piece only as its newest bytes.
      if (record->piece != nullptr) {
        internal::Detach(record.get());
      }
      // Its slot (replaced already where its address was reused).
      internal::Table* table = pool.table.load(std::memory_order_relaxed);
      const size_t mask = table->slots.size() - 1;
      for (size_t index = internal::Home(record->buffer, table->slots.size());; index = (index + 1) & mask) {
        const internal::Buffer* held = table->slots[index].load(std::memory_order_relaxed);
        if (held == nullptr) break;
        if (held == record.get()) {
          table->slots[index].store(&internal::tombstone, std::memory_order_release);
          break;
        }
      }
    }
    std::erase_if(pool.records, [](const std::unique_ptr<internal::Buffer>& record) { return record->destroyed.load(); });
    if (pool.tables.size() > 1) {
      pool.tables.erase(pool.tables.begin(), pool.tables.end() - 1);
    }
  }
  if (pool.installed) {
    std::erase_if(pool.states, [](const std::unique_ptr<internal::StateTable>& table) {
      if (table.get() == pool.active || !table->dead->load()) return false;
      internal::Drop(table.get());
      return true;
    });
    const bool stop = pool.stopped || pool.lost || pool.unwind_wanted
                      || internal::hooks::state.foreign_thread.load(std::memory_order_relaxed);
    if ((on && !stop) || pool.open_maps != 0) return;
    internal::Remove();
    if (stop && on) {
      Microsoft::WRL::ComPtr<ID3D11Device> device;
      context->GetDevice(device.GetAddressOf());
      pool.refused = device.Get();
      renodx::utils::log::w("[Total War] Fewer Driver Waits: off until the row is turned off and on again.");
    }
    return;
  }
  if (!on) {
    pool.refused = nullptr;
    return;
  }
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (pool.refused == device.Get()) return;
  if (!internal::Install(context)) {
    pool.refused = device.Get();
  }
}

// At the add-on's load: the buffers' events (the destroy event only reaches buffers made after it is registered).
inline void Use() {
  if (internal::pool.registered) return;
  internal::pool.registered = true;
  reshade::register_event<reshade::addon_event::init_resource>(OnInitResource);
  reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
}

// At the add-on's detach: the ring out where the render thread runs this with no ring map open; else it unwinds at the
// render thread's next call and its hooks stay, passing every call on (the image kept loaded).
inline void Uninstall() {
  using internal::pool;
  if (pool.installed) {
    if (GetCurrentThreadId() == internal::hooks::state.thread && pool.open_maps == 0) {
      internal::Remove();
    } else {
      pool.unwind_wanted = true;
      internal::hooks::state.lifetime.retain_until_exit = true;
      internal::hooks::state.ring_lifetime.retain_until_exit = true;
    }
  }
  if (pool.registered) {
    pool.registered = false;
    reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResource);
    reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
    reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  }
}

}  // namespace twattila::buffer_pool
