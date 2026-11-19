// twattila/context_hooks.hpp on WARP (WARP runs on the same d3d11.dll runtime bodies as the game's device), a
// single-threaded device as the game's, x86 and amd64:
// 1. The 17 bodies read from the immediate context's own table: inside d3d11.dll, distinct, none starting with a jump
//    (each printed: slot, RVA, first bytes). CONTROLS refused: no device / two devices / one multithreaded device in
//    device_loss's list; a body the
//    test detoured first (its E9).
// 2. Seeds: a constant buffer bound with a Set1 window, one with Set, a vertex buffer, all before Install: the seeds.
// 3. Every body in every table state: settled, after a Flush, after a Draw, after SwapDeviceContextState and after the
//    swap back with a null out pointer, each through the hook and through the native body (the table left in d3d11's
//    stubs): the call's own record once, its entry count +1. After a native swap the stubs apply the new state through
//    the bind bodies, from inside d3d11.dll, before the call: 7 records (IASetVertexBuffers, each stage's
//    SetConstantBuffers1); after the hook's swap none (its settling call takes them, nested). The nested entries d3d11
//    makes between bodies are printed (passed on, never recorded).
// 4. CONTROL: a slot hook in the table's Map slot (map trace v2's) sees a Map before a swap and misses the Maps after it;
//    the body hook sees them all.
// 5. Re-entry: a hooked body called inside a native body (the trampoline seam): counted nested, not recorded.
// 6. The records equal the calls made, field by field (a written constant buffer's content hash, a failed Map, Set1
//    windows and null windows, vertex buffers, both swaps' states, ClearState).
// 7. Other contexts (a second device's immediate one, its deferred one; this device's: none, single-threaded) and
//    another thread on this context: not recorded; the thread logged once, every later call passed on.
// 8. Uninstall: every body's first bytes back, the trampolines the bodies, the module reference let go, no records;
//    installed and removed again.
#include <windows.h>
#include <d3d11_1.h>
#include <detours.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "src/games/twattila/context_hooks.hpp"
#include "src/utils/vtable.hpp"

using Microsoft::WRL::ComPtr;
namespace hooks = twattila::context_hooks;
namespace loss = twattila::device_loss;
using hooks::Kind;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Context hooks test";
static std::vector<std::string> log_lines;
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  std::printf("  log: %s\n", message);
  log_lines.emplace_back(message);
}
}

// A Call with its arrays copied.
struct Record {
  hooks::Call call;
  std::vector<ID3D11Buffer*> buffers;
  std::vector<UINT> first_constants;
  std::vector<UINT> constant_counts;
  std::vector<UINT> strides;
  std::vector<UINT> offsets;
};
static std::vector<Record> records;

static void Observe(const hooks::Call& call) {
  Record record = {.call = call};
  const auto copy = [&call](const auto* values, auto* out) {
    if (values != nullptr) out->assign(values, values + std::min<UINT>(call.count, 32));
  };
  copy(call.buffers, &record.buffers);
  copy(call.first_constants, &record.first_constants);
  copy(call.constant_counts, &record.constant_counts);
  copy(call.strides, &record.strides);
  copy(call.offsets, &record.offsets);
  records.push_back(std::move(record));
}

static bool Logged(const char* text) {
  return std::ranges::any_of(log_lines, [text](const std::string& line) { return line.find(text) != std::string::npos; });
}

// The test's own detour on Map's body (the jump-prologue control) and its slot hook in Map's table slot (v2's form).
static hooks::internal::MapFunction test_detour_next = nullptr;
static HRESULT STDMETHODCALLTYPE TestDetourMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT sub, D3D11_MAP type, UINT flags,
                                               D3D11_MAPPED_SUBRESOURCE* mapped) {
  return test_detour_next(context, resource, sub, type, flags, mapped);
}
static hooks::internal::MapFunction slot_next = nullptr;
static int slot_maps = 0;
static HRESULT STDMETHODCALLTYPE SlotMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT sub, D3D11_MAP type, UINT flags,
                                         D3D11_MAPPED_SUBRESOURCE* mapped) {
  ++slot_maps;
  return slot_next(context, resource, sub, type, flags, mapped);
}
// The re-entry seam: Map's native body calls a hooked body.
static ID3D11DeviceContext* reentry_context = nullptr;
static ID3D11Buffer* reentry_buffer = nullptr;
static hooks::internal::MapFunction reentry_next = nullptr;
static HRESULT STDMETHODCALLTYPE ReentrantMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT sub, D3D11_MAP type, UINT flags,
                                              D3D11_MAPPED_SUBRESOURCE* mapped) {
  const UINT stride = 16;
  const UINT offset = 0;
  reentry_context->IASetVertexBuffers(7, 1, &reentry_buffer, &stride, &offset);
  return reentry_next(context, resource, sub, type, flags, mapped);
}

static uint64_t Fnv(const void* data, size_t bytes) {
  const auto* in = static_cast<const uint8_t*>(data);
  uint64_t hash = 0xCBF29CE484222325;
  size_t offset = 0;
  for (; offset + 8 <= bytes; offset += 8) {
    uint64_t word = 0;
    std::memcpy(&word, in + offset, 8);
    hash = (hash ^ word) * 0x100000001B3;
  }
  for (; offset < bytes; ++offset) hash = (hash ^ in[offset]) * 0x100000001B3;
  return hash;
}

int main() {
  constexpr std::array<const char*, hooks::internal::BODIES> NAMES = {
      "Map", "Unmap", "VSSetConstantBuffers", "HSSetConstantBuffers", "DSSetConstantBuffers", "GSSetConstantBuffers",
      "PSSetConstantBuffers", "CSSetConstantBuffers", "VSSetConstantBuffers1", "HSSetConstantBuffers1", "DSSetConstantBuffers1",
      "GSSetConstantBuffers1", "PSSetConstantBuffers1", "CSSetConstantBuffers1", "IASetVertexBuffers", "SwapDeviceContextState",
      "ClearState"};
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, &level, 1, D3D11_SDK_VERSION,
                                    device.GetAddressOf(), nullptr, context.GetAddressOf())));
  ComPtr<ID3D11Device1> device1;
  ComPtr<ID3D11DeviceContext1> context1;
  CHECK(SUCCEEDED(device.As(&device1)) && SUCCEEDED(context.As(&context1)));
  CHECK(static_cast<void*>(context1.Get()) == static_cast<void*>(context.Get()));
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3DDeviceContextState> other_state;
  for (ID3DDeviceContextState** made : {private_state.GetAddressOf(), other_state.GetAddressOf()}) {
    CHECK(SUCCEEDED(device1->CreateDeviceContextState(D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED, &level, 1, D3D11_SDK_VERSION,
                                                      __uuidof(ID3D11Device1), nullptr, made)));
  }
  const auto buffer = [&](UINT bytes, D3D11_USAGE usage, UINT bind, UINT cpu) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = usage, .BindFlags = bind, .CPUAccessFlags = cpu};
    ComPtr<ID3D11Buffer> made;
    device->CreateBuffer(&desc, nullptr, made.GetAddressOf());
    return made;
  };
  const auto cb = buffer(256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE);
  const auto cb_big = buffer(1024, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE);
  const auto vb = buffer(1024, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE);
  const auto staging = buffer(256, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
  CHECK(cb && cb_big && vb && staging);

  // 1. The bodies.
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  context->IAGetPrimitiveTopology(&topology);
  void* const* table = *reinterpret_cast<void* const* const*>(context.Get());
  const auto d3d11 = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"d3d11.dll"));
  std::array<void*, hooks::internal::BODIES> bodies = {};
  std::array<std::array<uint8_t, 16>, hooks::internal::BODIES> prologues = {};
  for (size_t body = 0; body < bodies.size(); ++body) {
    bodies[body] = table[hooks::internal::BODY_SLOTS[body]];
    std::memcpy(prologues[body].data(), bodies[body], 16);
    HMODULE module = nullptr;
    CHECK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(bodies[body]), &module));
    CHECK(reinterpret_cast<uintptr_t>(module) == d3d11);
    const auto& code = prologues[body];
    std::printf("  slot %3zu %-24s d3d11.dll+0x%06zx  %02x %02x %02x %02x %02x %02x %02x %02x\n", hooks::internal::BODY_SLOTS[body], NAMES[body],
                static_cast<size_t>(reinterpret_cast<uintptr_t>(bodies[body]) - d3d11), code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7]);
    CHECK(code[0] != 0xE9 && code[0] != 0xEB && !(code[0] == 0xFF && code[1] == 0x25) && !(code[0] == 0x68 && code[5] == 0xC3));
  }
  auto sorted = bodies;
  std::ranges::sort(sorted);
  CHECK(std::ranges::adjacent_find(sorted) == sorted.end());
  CHECK(!hooks::Install(context.Get(), Observe, true) && Logged("0 D3D11 devices"));
  loss::devices = {{.device = device.Get(), .single_threaded = true}, {.device = device.Get(), .single_threaded = true}};
  CHECK(!hooks::Install(context.Get(), Observe, true) && Logged("2 D3D11 devices"));
  loss::devices = {{.device = device.Get(), .single_threaded = false}};
  CHECK(!hooks::Install(context.Get(), Observe, true) && Logged("not single-threaded") && hooks::internal::state.hooks.empty());
  loss::devices = {{.device = device.Get(), .single_threaded = true}};
  test_detour_next = reinterpret_cast<hooks::internal::MapFunction>(bodies[hooks::internal::MAP]);
  CHECK(DetourTransactionBegin() == NO_ERROR && DetourAttach(reinterpret_cast<void**>(&test_detour_next), reinterpret_cast<void*>(&TestDetourMap)) == NO_ERROR
        && DetourTransactionCommit() == NO_ERROR);
  CHECK(static_cast<const uint8_t*>(bodies[hooks::internal::MAP])[0] == 0xE9);
  CHECK(!hooks::Install(context.Get(), Observe, true) && Logged("starts with a jump") && hooks::internal::state.hooks.empty());
  CHECK(DetourTransactionBegin() == NO_ERROR && DetourDetach(reinterpret_cast<void**>(&test_detour_next), reinterpret_cast<void*>(&TestDetourMap)) == NO_ERROR
        && DetourTransactionCommit() == NO_ERROR);
  CHECK(std::memcmp(bodies[hooks::internal::MAP], prologues[hooks::internal::MAP].data(), 16) == 0);
  std::printf("1. 17 distinct bodies in d3d11.dll, no jump prologue; refused: 0 and 2 devices, a multithreaded one, a body detoured"
              " first (E9)\n");

  // 2. Seeds.
  const UINT window_first = 16;
  const UINT window_count = 16;
  context1->PSSetConstantBuffers1(3, 1, cb_big.GetAddressOf(), &window_first, &window_count);
  context->CSSetConstantBuffers(5, 1, cb.GetAddressOf());
  const UINT seed_stride = 12;
  const UINT seed_offset = 24;
  context->IASetVertexBuffers(1, 1, vb.GetAddressOf(), &seed_stride, &seed_offset);
  CHECK(hooks::Install(context.Get(), Observe, true));
  CHECK(records.size() == 7);
  for (size_t stage = 0; stage < hooks::STAGES; ++stage) {
    const auto& seed = records[stage];
    CHECK(seed.call.kind == Kind::SEED_CONSTANT_BUFFERS && static_cast<size_t>(seed.call.stage) == stage && seed.buffers.size() == 14);
    for (UINT slot = 0; slot < 14; ++slot) {
      const bool ps3 = (stage == 4 && slot == 3);
      const bool cs5 = (stage == 5 && slot == 5);
      CHECK(seed.buffers[slot] == (ps3 ? cb_big.Get() : (cs5 ? cb.Get() : nullptr)));
      if (ps3) CHECK(seed.first_constants[slot] == window_first && seed.constant_counts[slot] == window_count);
      // Get1's answer for a Set bind: the whole buffer as 0 + 4096 constants (map trace v3 reads it so).
      if (cs5) CHECK(seed.first_constants[slot] == 0 && seed.constant_counts[slot] == D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT);
    }
  }
  CHECK(records[6].call.kind == Kind::SEED_VERTEX_BUFFERS && records[6].buffers.size() == 32 && records[6].buffers[1] == vb.Get()
        && records[6].strides[1] == seed_stride && records[6].offsets[1] == seed_offset && records[6].buffers[0] == nullptr);
  std::printf("2. seeds: ps b3 (Set1 window %u+%u), cs b5 (Set: 0+4096), vb 1 (stride %u offset %u)\n", window_first, window_count, seed_stride,
              seed_offset);

  // 3. Every body in every table state.
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  // The device's own state (the swap backs').
  ComPtr<ID3DDeviceContextState> game_state;
  context1->SwapDeviceContextState(private_state.Get(), game_state.GetAddressOf());
  context1->SwapDeviceContextState(game_state.Get(), nullptr);
  const auto call_body = [&](size_t body) {
    const UINT first = 0;
    const UINT count = 16;
    const UINT stride = 16;
    const UINT offset = 0;
    ID3D11Buffer* const buffers[1] = {cb.Get()};
    if (body == hooks::internal::MAP) {
      context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    } else if (body == hooks::internal::UNMAP) {
      context->Unmap(cb.Get(), 0);
    } else if (body < hooks::internal::SET_CONSTANT_BUFFERS1) {
      const std::array setters = {&ID3D11DeviceContext::VSSetConstantBuffers, &ID3D11DeviceContext::HSSetConstantBuffers,
                                  &ID3D11DeviceContext::DSSetConstantBuffers, &ID3D11DeviceContext::GSSetConstantBuffers,
                                  &ID3D11DeviceContext::PSSetConstantBuffers, &ID3D11DeviceContext::CSSetConstantBuffers};
      (context.Get()->*setters[body - hooks::internal::SET_CONSTANT_BUFFERS])(1, 1, buffers);
    } else if (body < hooks::internal::SET_VERTEX_BUFFERS) {
      const std::array setters = {&ID3D11DeviceContext1::VSSetConstantBuffers1, &ID3D11DeviceContext1::HSSetConstantBuffers1,
                                  &ID3D11DeviceContext1::DSSetConstantBuffers1, &ID3D11DeviceContext1::GSSetConstantBuffers1,
                                  &ID3D11DeviceContext1::PSSetConstantBuffers1, &ID3D11DeviceContext1::CSSetConstantBuffers1};
      (context1.Get()->*setters[body - hooks::internal::SET_CONSTANT_BUFFERS1])(1, 1, buffers, &first, &count);
    } else if (body == hooks::internal::SET_VERTEX_BUFFERS) {
      context->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
    } else if (body == hooks::internal::SWAP_STATE) {
      ComPtr<ID3DDeviceContextState> active;
      context1->SwapDeviceContextState(other_state.Get(), active.GetAddressOf());
      context1->SwapDeviceContextState(active.Get(), nullptr);
    } else {
      context->ClearState();
    }
  };
  // The test's own call of each body (its records matched by these arguments), and the table states it is made in: 3
  // and 4 swapped through the hook (which settles the table), 5 and 6 through the native body (the trampoline: the
  // table left in d3d11's swap stubs).
  const auto own = [&](size_t body, const Record& record) {
    const auto& call = record.call;
    switch (body) {
      case hooks::internal::MAP:
      case hooks::internal::UNMAP:
        return call.kind == (body == hooks::internal::MAP ? Kind::MAP : Kind::UNMAP) && call.resource == cb.Get();
      case hooks::internal::SET_VERTEX_BUFFERS:
        return call.kind == Kind::SET_VERTEX_BUFFERS && call.start_slot == 0 && call.count == 1 && record.buffers[0] == vb.Get() && record.strides[0] == 16;
      case hooks::internal::SWAP_STATE:
        return call.kind == Kind::SWAP_STATE;
      case hooks::internal::CLEAR_STATE:
        return call.kind == Kind::CLEAR_STATE;
      default: {
        const bool set1 = body >= hooks::internal::SET_CONSTANT_BUFFERS1;
        return call.kind == (set1 ? Kind::SET_CONSTANT_BUFFERS1 : Kind::SET_CONSTANT_BUFFERS)
               && static_cast<size_t>(call.stage) == (body - hooks::internal::SET_CONSTANT_BUFFERS) % hooks::STAGES && call.start_slot == 1
               && call.count == 1 && record.buffers[0] == cb.Get() && (!set1 || (record.first_constants[0] == 0 && record.constant_counts[0] == 16));
      }
    }
  };
  const auto native_swap = reinterpret_cast<hooks::internal::SwapStateFunction>(hooks::internal::state.trampolines[hooks::internal::SWAP_STATE]);
  constexpr std::array<const char*, 7> TABLE_STATES = {"settled", "after Flush", "after Draw", "swapped in", "swapped back with null",
                                                       "swapped in natively (stubs)", "applied, swapped back with null natively (stubs)"};
  std::array<uint32_t, hooks::internal::BODIES> nested_seen = {};
  size_t applied = 0;
  for (size_t body = 0; body < hooks::internal::BODIES; ++body) {
    for (size_t table_state = 0; table_state < TABLE_STATES.size(); ++table_state) {
      if (body == hooks::internal::UNMAP) context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      switch (table_state) {
        case 0: context->IAGetPrimitiveTopology(&topology); break;
        case 1: context->Flush(); break;
        case 2: context->Draw(3, 0); break;
        case 3: context1->SwapDeviceContextState(private_state.Get(), nullptr); break;
        case 4:
          context1->SwapDeviceContextState(private_state.Get(), nullptr);
          context1->SwapDeviceContextState(game_state.Get(), nullptr);
          break;
        case 5: native_swap(context1.Get(), private_state.Get(), nullptr); break;
        default:
          // The private state applied before the swap back (else d3d11 has nothing to apply: it is lazy).
          native_swap(context1.Get(), private_state.Get(), nullptr);
          context->IAGetPrimitiveTopology(&topology);
          native_swap(context1.Get(), game_state.Get(), nullptr);
          break;
      }
      void* const slot_entry = (*reinterpret_cast<void* const* const*>(context.Get()))[hooks::internal::BODY_SLOTS[body]];
      records.clear();
      const auto entries = hooks::internal::state.entries;
      const uint32_t nested = hooks::internal::state.nested;
      call_body(body);
      // A swap's measured call is two (in and back).
      const size_t calls = (body == hooks::internal::SWAP_STATE ? 2 : 1);
      const auto own_records = static_cast<size_t>(std::ranges::count_if(records, [&](const Record& record) { return own(body, record); }));
      // The rest: d3d11's application of the swapped-in state (from inside d3d11.dll); none for a swap (its own slot holds
      // the body, and the hook's settling call takes it, nested).
      size_t state_application = 0;
      for (const auto& record : records) {
        if (own(body, record)) continue;
        const bool bind = record.call.kind == Kind::SET_VERTEX_BUFFERS || record.call.kind == Kind::SET_CONSTANT_BUFFERS1;
        const auto direct = record.call.direct_caller;
        CHECK(bind && direct > d3d11 && direct - d3d11 < 0x1000000);
        ++state_application;
      }
      if (own_records != calls || hooks::internal::state.entries[body] < entries[body] + calls
          || state_application != ((table_state >= 5 && body != hooks::internal::SWAP_STATE) ? 7u : 0u)) {
        std::printf("FAIL: %s %s (slot holds d3d11.dll+0x%zx): %zu own records, %zu state application ones, %u entries\n", NAMES[body],
                    TABLE_STATES[table_state], static_cast<size_t>(reinterpret_cast<uintptr_t>(slot_entry) - d3d11), own_records, state_application,
                    hooks::internal::state.entries[body] - entries[body]);
        return 1;
      }
      applied += state_application;
      nested_seen[body] += hooks::internal::state.nested - nested;
      if (body == hooks::internal::MAP) context->Unmap(cb.Get(), 0);
      if (table_state == 3 || table_state == 5) context1->SwapDeviceContextState(game_state.Get(), nullptr);
    }
  }
  for (size_t body = 0; body < hooks::internal::BODIES; ++body) {
    if (nested_seen[body] != 0) std::printf("  %s: %u nested hook entries inside it (d3d11's own calls)\n", NAMES[body], nested_seen[body]);
  }
  std::printf("3. every body's own call seen once in all 7 table states; d3d11's state application after a native swap (stubs): 7 bind calls"
              " (IASetVertexBuffers, 6 SetConstantBuffers1) from inside d3d11.dll each time (%zu); after the hook's swap: none (nested)\n",
              applied);

  // 4. CONTROL: v2's slot hook dies at the swap, the body hook does not.
  context->IAGetPrimitiveTopology(&topology);
  renodx::utils::vtable::Slot<hooks::internal::MapFunction> slot = {.object = context.Get(), .index = 14, .original = &slot_next, .replacement = &SlotMap};
  renodx::utils::vtable::Install(&slot);
  records.clear();
  context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  context->Unmap(cb.Get(), 0);
  CHECK(slot_maps == 1 && records.size() == 2);
  context1->SwapDeviceContextState(private_state.Get(), nullptr);
  context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  context->Unmap(cb.Get(), 0);
  context1->SwapDeviceContextState(game_state.Get(), nullptr);
  context->IAGetPrimitiveTopology(&topology);
  context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  context->Unmap(cb.Get(), 0);
  void* const map_slot_after = (*reinterpret_cast<void* const* const*>(context.Get()))[14];
  std::printf("  the Map slot after the swaps: d3d11.dll+0x%zx (the body: +0x%zx)\n", static_cast<size_t>(reinterpret_cast<uintptr_t>(map_slot_after) - d3d11),
              static_cast<size_t>(reinterpret_cast<uintptr_t>(bodies[hooks::internal::MAP]) - d3d11));
  CHECK(slot_maps == 1);
  CHECK(std::ranges::count_if(records, [](const Record& record) { return record.call.kind == Kind::MAP; }) == 3);
  CHECK(map_slot_after != reinterpret_cast<void*>(&SlotMap));
  slot = {};
  slot_next = nullptr;
  std::printf("4. CONTROL: the slot hook saw 1 of 3 Maps (dead after the swap), the body hook all 3\n");

  // 5. Re-entry.
  reentry_context = context.Get();
  reentry_buffer = vb.Get();
  reentry_next = reinterpret_cast<hooks::internal::MapFunction>(hooks::internal::state.trampolines[hooks::internal::MAP]);
  hooks::internal::state.trampolines[hooks::internal::MAP] = reinterpret_cast<void*>(&ReentrantMap);
  records.clear();
  const auto entries_before = hooks::internal::state.entries;
  const uint32_t nested_before = hooks::internal::state.nested;
  CHECK(SUCCEEDED(context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  hooks::internal::state.trampolines[hooks::internal::MAP] = reinterpret_cast<void*>(reentry_next);
  context->Unmap(cb.Get(), 0);
  CHECK(records.size() == 2 && records[0].call.kind == Kind::MAP && records[0].call.nested == 1 && records[1].call.kind == Kind::UNMAP);
  CHECK(hooks::internal::state.entries[hooks::internal::SET_VERTEX_BUFFERS] == entries_before[hooks::internal::SET_VERTEX_BUFFERS] + 1
        && hooks::internal::state.nested == nested_before + 1);
  std::printf("5. re-entry: IASetVertexBuffers inside Map's native body counted nested (1), not recorded\n");

  // 6. Records equal the calls.
  records.clear();
  CHECK(SUCCEEDED(context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  std::array<uint8_t, 256> pattern = {};
  for (size_t index = 0; index < pattern.size(); ++index) pattern[index] = static_cast<uint8_t>(index * 7 + 3);
  std::memcpy(mapped.pData, pattern.data(), pattern.size());
  context->Unmap(cb.Get(), 0);
  const HRESULT invalid = context->Map(staging.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  CHECK(FAILED(invalid));
  ID3D11Buffer* const pair[2] = {cb.Get(), cb_big.Get()};
  context->VSSetConstantBuffers(2, 2, pair);
  const UINT gs_first = 32;
  const UINT gs_count = 16;
  context1->GSSetConstantBuffers1(4, 1, cb_big.GetAddressOf(), &gs_first, &gs_count);
  context1->HSSetConstantBuffers1(0, 1, cb.GetAddressOf(), nullptr, nullptr);
  ID3D11Buffer* const vbs[2] = {vb.Get(), vb.Get()};
  const UINT strides[2] = {12, 16};
  const UINT offsets[2] = {0, 48};
  context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
  ComPtr<ID3DDeviceContextState> swapped_out;
  context1->SwapDeviceContextState(private_state.Get(), swapped_out.GetAddressOf());
  context1->SwapDeviceContextState(game_state.Get(), nullptr);
  context->ClearState();
  CHECK(records.size() == 10);
  const auto& map = records[0].call;
  CHECK(map.kind == Kind::MAP && map.resource == cb.Get() && map.subresource == 0 && map.map_type == D3D11_MAP_WRITE_DISCARD && map.result == S_OK
        && map.qpc <= map.done && map.direct_caller != 0);
  CHECK(records[1].call.kind == Kind::UNMAP && records[1].call.content_known && records[1].call.content == Fnv(pattern.data(), pattern.size()));
  CHECK(records[2].call.kind == Kind::MAP && records[2].call.resource == staging.Get() && records[2].call.result == invalid);
  CHECK(records[3].call.kind == Kind::SET_CONSTANT_BUFFERS && records[3].call.stage == hooks::Stage::VERTEX_SHADER && records[3].call.start_slot == 2
        && records[3].buffers == std::vector<ID3D11Buffer*>(pair, pair + 2));
  CHECK(records[4].call.kind == Kind::SET_CONSTANT_BUFFERS1 && records[4].call.stage == hooks::Stage::GEOMETRY_SHADER && records[4].call.start_slot == 4
        && records[4].buffers[0] == cb_big.Get() && records[4].first_constants == std::vector<UINT>{gs_first} && records[4].constant_counts == std::vector<UINT>{gs_count});
  CHECK(records[5].call.stage == hooks::Stage::HULL_SHADER && records[5].call.first_constants == nullptr && records[5].call.constant_counts == nullptr);
  CHECK(records[6].call.kind == Kind::SET_VERTEX_BUFFERS && records[6].call.count == 2 && records[6].strides == std::vector<UINT>(strides, strides + 2)
        && records[6].offsets == std::vector<UINT>(offsets, offsets + 2));
  CHECK(records[7].call.kind == Kind::SWAP_STATE && records[7].call.state == private_state.Get() && records[7].call.previous_state == game_state.Get() && swapped_out == game_state);
  CHECK(records[8].call.kind == Kind::SWAP_STATE && records[8].call.state == game_state.Get() && records[8].call.previous_state == private_state.Get());
  CHECK(records[9].call.kind == Kind::CLEAR_STATE);
  // d3d11's own calls between bodies: inside the swaps (the state application) and ClearState; none elsewhere.
  for (const auto& record : records) {
    CHECK(record.call.nested == 0 || record.call.kind == Kind::SWAP_STATE || record.call.kind == Kind::CLEAR_STATE);
  }
  std::printf("  nested entries: swap in %u, swap back %u, ClearState %u\n", records[7].call.nested, records[8].call.nested, records[9].call.nested);
  std::printf("6. 10 records equal the calls (content hash, failed Map 0x%08x, Set1 windows and nulls, vertex buffers, both swaps' states, ClearState)\n",
              static_cast<uint32_t>(invalid));

  // 7. Other contexts and another thread. A second single-threaded device's table holds the hooked bodies (its calls enter
  // them: the control); the multithreaded one's may hold others (counted, said so).
  ComPtr<ID3D11Device> twin_device;
  ComPtr<ID3D11DeviceContext> twin_context;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, &level, 1, D3D11_SDK_VERSION,
                                    twin_device.GetAddressOf(), nullptr, twin_context.GetAddressOf())));
  const auto hooked_slots = [&](ID3D11DeviceContext* other) {
    other->IAGetPrimitiveTopology(&topology);
    void* const* other_table = *reinterpret_cast<void* const* const*>(other);
    size_t count = 0;
    for (size_t body = 0; body < bodies.size(); ++body) {
      count += (other_table[hooks::internal::BODY_SLOTS[body]] == bodies[body] ? 1 : 0);
    }
    return count;
  };
  CHECK(hooked_slots(twin_context.Get()) == hooks::internal::BODIES);
  ComPtr<ID3D11Device> other_device;
  ComPtr<ID3D11DeviceContext> other_context;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, other_device.GetAddressOf(), nullptr,
                                    other_context.GetAddressOf())));
  ComPtr<ID3D11DeviceContext> deferred;
  CHECK(SUCCEEDED(other_device->CreateDeferredContext(0, deferred.GetAddressOf())));
  ComPtr<ID3D11DeviceContext> own_deferred;
  const bool own_deferred_made = SUCCEEDED(device->CreateDeferredContext(0, own_deferred.GetAddressOf()));
  const D3D11_BUFFER_DESC other_desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  ComPtr<ID3D11Buffer> other_cb;
  CHECK(SUCCEEDED(other_device->CreateBuffer(&other_desc, nullptr, other_cb.GetAddressOf())));
  ComPtr<ID3D11Buffer> twin_cb;
  CHECK(SUCCEEDED(twin_device->CreateBuffer(&other_desc, nullptr, twin_cb.GetAddressOf())));
  const size_t other_hooked = hooked_slots(other_context.Get());
  const size_t deferred_hooked = hooked_slots(deferred.Get());
  records.clear();
  const auto entries_other = hooks::internal::state.entries;
  CHECK(SUCCEEDED(twin_context->Map(twin_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  twin_context->Unmap(twin_cb.Get(), 0);
  twin_context->PSSetConstantBuffers(0, 1, twin_cb.GetAddressOf());
  twin_context->IASetVertexBuffers(0, 1, twin_cb.GetAddressOf(), &seed_stride, &seed_offset);
  twin_context->ClearState();
  CHECK(SUCCEEDED(other_context->Map(other_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  other_context->Unmap(other_cb.Get(), 0);
  other_context->PSSetConstantBuffers(0, 1, other_cb.GetAddressOf());
  other_context->ClearState();
  CHECK(SUCCEEDED(deferred->Map(other_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  deferred->Unmap(other_cb.Get(), 0);
  deferred->VSSetConstantBuffers(0, 1, other_cb.GetAddressOf());
  CHECK(records.empty() && hooks::internal::state.entries == entries_other);
  log_lines.clear();
  std::thread([&] {
    context->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
    context->PSSetConstantBuffers(1, 1, cb.GetAddressOf());
  }).join();
  context->PSSetConstantBuffers(2, 1, cb.GetAddressOf());
  CHECK(records.empty() && std::ranges::count_if(log_lines, [](const std::string& line) { return line.find("called the game's context") != std::string::npos; }) == 1);
  std::printf("7. not recorded: a second single-threaded device's context (all 17 slots the hooked bodies), a multithreaded device's immediate"
              " (%zu of 17 slots the hooked bodies) and deferred (%zu of 17) contexts (this device's deferred: %s), another thread (logged once,"
              " later calls passed on)\n",
              other_hooked, deferred_hooked, (own_deferred_made ? "made" : "refused, single-threaded"));

  // 8. Uninstall, then installed and removed again.
  const auto removed = [&] {
    if (!hooks::internal::state.hooks.empty() || hooks::internal::state.lifetime.module != nullptr || hooks::internal::state.trampolines != bodies) return false;
    for (size_t body = 0; body < bodies.size(); ++body) {
      if (std::memcmp(bodies[body], prologues[body].data(), 16) != 0) return false;
    }
    records.clear();
    context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    context->Unmap(cb.Get(), 0);
    context->ClearState();
    return records.empty();
  };
  hooks::Uninstall();
  CHECK(removed());
  CHECK(hooks::Install(context.Get(), Observe, true) && records.size() == 7);
  records.clear();
  context->PSSetConstantBuffers(2, 1, cb.GetAddressOf());
  CHECK(records.size() == 1);
  hooks::Uninstall();
  CHECK(removed());
  for (size_t body = 0; body < bodies.size(); ++body) CHECK(table[hooks::internal::BODY_SLOTS[body]] == bodies[body]);
  std::printf("8. Uninstall: the bodies' first 16 bytes back, trampolines = bodies, the module let go, nothing recorded; again after a reinstall\n");
  std::printf("PASS\n");
  return 0;
}
