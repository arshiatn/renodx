// Warhammer III's lighting constants with the game's Global Illumination flag off (twwarhammer3/game_gi.hpp) on WARP:
// which buffers it touches, and that it never writes through a pointer whose map has ended. Real buffers, maps, deferred
// contexts and updates; where a test needs to see a write that must not happen, the "mapped" memory is a page of its
// own that is freed after the unmap, so a late write faults.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "src/games/twwarhammer3/game_gi.hpp"

using Microsoft::WRL::ComPtr;
namespace gi = twwarhammer3::game_gi;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

static LONG CALLBACK OnFault(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  std::printf("FAIL: a write through a pointer whose map had ended (address %p)\n", reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]));
  std::fflush(stdout);
  TerminateProcess(GetCurrentProcess(), 1);
  return EXCEPTION_CONTINUE_SEARCH;
}

constexpr uint8_t FILL = 0xAB;
constexpr uint32_t GAME_FLAG = 1u;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> immediate;

static ComPtr<ID3D11Buffer> MakeBuffer(UINT bytes, UINT bind, D3D11_USAGE usage) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes,
                                  .Usage = usage,
                                  .BindFlags = bind,
                                  .CPUAccessFlags = (usage == D3D11_USAGE_DYNAMIC) ? static_cast<UINT>(D3D11_CPU_ACCESS_WRITE) : 0u};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, nullptr, &buffer);
  return buffer;
}

static reshade::api::resource Handle(ID3D11Buffer* buffer) {
  return {reinterpret_cast<uint64_t>(buffer)};
}

// As ReShade announces a new buffer.
static void Announce(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC native = {};
  buffer->GetDesc(&native);
  reshade::api::resource_desc desc(native.ByteWidth, reshade::api::memory_heap::cpu_to_gpu,
                                   ((native.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0) ? reshade::api::resource_usage::constant_buffer
                                                                                         : reshade::api::resource_usage::vertex_buffer);
  gi::OnInitResource(nullptr, desc, nullptr, reshade::api::resource_usage::undefined, Handle(buffer));
}

static void Fill(uint8_t* data, UINT bytes, uint8_t fill) {
  std::memset(data, fill, bytes);
  if (bytes >= gi::FLAG_OFFSET + 4) std::memcpy(data + gi::FLAG_OFFSET, &GAME_FLAG, sizeof(GAME_FLAG));
}

// The game's upload through a map, with ReShade's two events where ReShade raises them.
static bool Upload(ID3D11DeviceContext* context, ID3D11Buffer* buffer, uint8_t fill) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
  gi::OnMapBufferRegion(nullptr, Handle(buffer), 0, UINT64_MAX, reshade::api::map_access::write_discard, &mapped.pData);
  Fill(static_cast<uint8_t*>(mapped.pData), desc.ByteWidth, fill);
  gi::OnUnmapBufferRegion(nullptr, Handle(buffer));
  context->Unmap(buffer, 0);
  return true;
}

static std::vector<uint8_t> ReadBack(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, &staging);
  immediate->CopyResource(staging.Get(), buffer);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  std::vector<uint8_t> bytes(desc.ByteWidth);
  if (SUCCEEDED(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    std::memcpy(bytes.data(), mapped.pData, bytes.size());
    immediate->Unmap(staging.Get(), 0);
  }
  return bytes;
}

static uint32_t FlagOf(const uint8_t* bytes) {
  uint32_t flag = 0;
  std::memcpy(&flag, bytes + gi::FLAG_OFFSET, sizeof(flag));
  return flag;
}

// Every byte but the flag is the game's.
static bool RestIs(const std::vector<uint8_t>& bytes, uint8_t fill) {
  for (size_t at = 0; at < bytes.size(); ++at) {
    if ((at < gi::FLAG_OFFSET || at >= gi::FLAG_OFFSET + 4) && bytes[at] != fill) return false;
  }
  return true;
}

// Memory standing in for a map's: a page of its own, gone after its unmap.
static uint8_t* Page() {
  auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  Fill(page, static_cast<UINT>(gi::LIGHTING_BYTES), FILL);
  return page;
}

static void MapEvent(ID3D11Buffer* buffer, uint8_t* page) {
  void* data = page;
  gi::OnMapBufferRegion(nullptr, Handle(buffer), 0, UINT64_MAX, reshade::api::map_access::write_discard, &data);
}

static void OnThread(const auto& work) {
  std::thread(work).join();
}

static size_t OpenMaps(ID3D11Buffer* buffer) {
  size_t count = 0;
  gi::lighting_buffers.if_contains(reinterpret_cast<uint64_t>(buffer), [&count](const auto& entry) { count = entry.second.size(); });
  return count;
}

int main() {
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  AddVectoredExceptionHandler(1, OnFault);
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &immediate)));

  const auto lighting = MakeBuffer(480, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC);
  Announce(lighting.Get());

  // 1. The option on: the flag is 0 in what the GPU gets, every other byte the game's; off: the game's bytes.
  gi::off = true;
  CHECK(Upload(immediate.Get(), lighting.Get(), FILL));
  auto bytes = ReadBack(lighting.Get());
  CHECK(FlagOf(bytes.data()) == 0 && RestIs(bytes, FILL));
  CHECK(gi::open_map_count == 0 && OpenMaps(lighting.Get()) == 0);
  gi::off = false;
  CHECK(Upload(immediate.Get(), lighting.Get(), 0x11));
  bytes = ReadBack(lighting.Get());
  CHECK(FlagOf(bytes.data()) == GAME_FLAG && RestIs(bytes, 0x11));
  gi::off = true;

  // 2. Which buffers: the game's 480-byte constant buffers ReShade announced. Not another size, not a 480-byte vertex
  // buffer, not a buffer nobody announced.
  {
    const auto other_size = MakeBuffer(496, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC);
    const auto vertices = MakeBuffer(480, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC);
    const auto unannounced = MakeBuffer(480, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC);
    Announce(other_size.Get());
    Announce(vertices.Get());
    for (ID3D11Buffer* buffer : {other_size.Get(), vertices.Get(), unannounced.Get()}) {
      CHECK(Upload(immediate.Get(), buffer, FILL));
      bytes = ReadBack(buffer);
      CHECK(FlagOf(bytes.data()) == GAME_FLAG && RestIs(bytes, FILL));
      CHECK(gi::open_map_count == 0);
    }
    CHECK(gi::lighting_buffers.size() == 1);
  }

  // 3. The option switched between a map and its unmap. Off at the unmap: the game's flag, nothing kept. On at the
  // unmap only: no pointer was kept, nothing written.
  {
    uint8_t* page = Page();
    MapEvent(lighting.Get(), page);
    gi::off = false;
    gi::OnUnmapBufferRegion(nullptr, Handle(lighting.Get()));
    CHECK(FlagOf(page) == GAME_FLAG && gi::open_map_count == 0);
    MapEvent(lighting.Get(), page);
    CHECK(gi::open_map_count == 0);
    gi::off = true;
    gi::OnUnmapBufferRegion(nullptr, Handle(lighting.Get()));
    CHECK(FlagOf(page) == GAME_FLAG);
    VirtualFree(page, 0, MEM_RELEASE);
  }

  // 4. One buffer mapped on two deferred contexts at once, each on its own thread: both uploads get the flag 0.
  {
    ComPtr<ID3D11DeviceContext> deferred[2];
    ComPtr<ID3D11CommandList> lists[2];
    HANDLE both_mapped[2] = {CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic_uint32_t open_at_once = 0;
    std::atomic_bool failed = false;
    std::vector<std::thread> threads;
    for (int index = 0; index < 2; ++index) {
      CHECK(SUCCEEDED(device->CreateDeferredContext(0, &deferred[index])));
      threads.emplace_back([&, index] {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(deferred[index]->Map(lighting.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
          failed = true;
          SetEvent(both_mapped[index]);
          return;
        }
        gi::OnMapBufferRegion(nullptr, Handle(lighting.Get()), 0, UINT64_MAX, reshade::api::map_access::write_discard, &mapped.pData);
        Fill(static_cast<uint8_t*>(mapped.pData), 480, static_cast<uint8_t>(0x20 + index));
        SetEvent(both_mapped[index]);
        WaitForSingleObject(both_mapped[1 - index], 10000);
        open_at_once = std::max(open_at_once.load(), gi::open_map_count.load());
        gi::OnUnmapBufferRegion(nullptr, Handle(lighting.Get()));
        deferred[index]->Unmap(lighting.Get(), 0);
        deferred[index]->FinishCommandList(FALSE, &lists[index]);
      });
    }
    for (auto& thread : threads) thread.join();
    CHECK(!failed && open_at_once == 2 && gi::open_map_count == 0);
    for (int index = 0; index < 2; ++index) {
      immediate->ExecuteCommandList(lists[index].Get(), FALSE);
      bytes = ReadBack(lighting.Get());
      CHECK(FlagOf(bytes.data()) == 0 && RestIs(bytes, static_cast<uint8_t>(0x20 + index)));
      CloseHandle(both_mapped[index]);
    }
  }

  // 5. Mapped on one thread, unmapped on another: nothing is written, and the pointer is gone for the mapping thread
  // too (its page is freed: a write would fault).
  {
    uint8_t* page = Page();
    MapEvent(lighting.Get(), page);
    CHECK(gi::open_map_count == 1);
    OnThread([&] { gi::OnUnmapBufferRegion(nullptr, Handle(lighting.Get())); });
    CHECK(FlagOf(page) == GAME_FLAG && gi::open_map_count == 0 && OpenMaps(lighting.Get()) == 0);
    VirtualFree(page, 0, MEM_RELEASE);
    gi::OnUnmapBufferRegion(nullptr, Handle(lighting.Get()));
    // The other thread's own map beside this thread's: a third thread's unmap ends both.
    uint8_t* mine = Page();
    uint8_t* theirs = Page();
    MapEvent(lighting.Get(), mine);
    OnThread([&] { MapEvent(lighting.Get(), theirs); });
    CHECK(gi::open_map_count == 2);
    OnThread([&] { gi::OnUnmapBufferRegion(nullptr, Handle(lighting.Get())); });
    CHECK(gi::open_map_count == 0 && FlagOf(mine) == GAME_FLAG && FlagOf(theirs) == GAME_FLAG);
    VirtualFree(mine, 0, MEM_RELEASE);
    VirtualFree(theirs, 0, MEM_RELEASE);
    gi::OnUnmapBufferRegion(nullptr, Handle(lighting.Get()));
  }

  // 6. A map whose unmap never came (a lost device), then the next map on that thread: only the new pointer is written.
  {
    uint8_t* lost = Page();
    MapEvent(lighting.Get(), lost);
    VirtualFree(lost, 0, MEM_RELEASE);
    uint8_t* next = Page();
    MapEvent(lighting.Get(), next);
    CHECK(gi::open_map_count == 1);
    gi::OnUnmapBufferRegion(nullptr, Handle(lighting.Get()));
    CHECK(FlagOf(next) == 0 && gi::open_map_count == 0);
    VirtualFree(next, 0, MEM_RELEASE);
  }

  // 7. A buffer destroyed while mapped, and a new one at its address (with and without the destroy seen): the old
  // pointer is never written.
  {
    auto doomed = MakeBuffer(480, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC);
    Announce(doomed.Get());
    CHECK(gi::lighting_buffers.size() == 2);
    uint8_t* page = Page();
    MapEvent(doomed.Get(), page);
    gi::OnDestroyResource(nullptr, Handle(doomed.Get()));
    CHECK(gi::open_map_count == 0 && gi::lighting_buffers.size() == 1);
    VirtualFree(page, 0, MEM_RELEASE);
    gi::OnUnmapBufferRegion(nullptr, Handle(doomed.Get()));
    Announce(doomed.Get());
    gi::OnUnmapBufferRegion(nullptr, Handle(doomed.Get()));
    page = Page();
    MapEvent(doomed.Get(), page);
    Announce(doomed.Get());
    CHECK(gi::open_map_count == 0 && OpenMaps(doomed.Get()) == 0);
    VirtualFree(page, 0, MEM_RELEASE);
    gi::OnUnmapBufferRegion(nullptr, Handle(doomed.Get()));
    gi::OnDestroyResource(nullptr, Handle(doomed.Get()));
  }

  // 8. The game's UpdateSubresource, immediate and deferred: replaced by the same bytes with the flag 0.
  {
    const auto updated = MakeBuffer(480, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT);
    const auto other = MakeBuffer(496, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT);
    Announce(updated.Get());
    Announce(other.Get());
    uint8_t game[496];
    Fill(game, 496, 0x31);
    CHECK(gi::UpdateWithFlagOff(immediate.Get(), game, Handle(updated.Get()), 0, UINT64_MAX));
    bytes = ReadBack(updated.Get());
    CHECK(FlagOf(bytes.data()) == 0 && RestIs(bytes, 0x31));
    ComPtr<ID3D11DeviceContext> deferred;
    ComPtr<ID3D11CommandList> list;
    CHECK(SUCCEEDED(device->CreateDeferredContext(0, &deferred)));
    Fill(game, 496, 0x32);
    CHECK(gi::UpdateWithFlagOff(deferred.Get(), game, Handle(updated.Get()), 0, 480));
    Fill(game, 496, 0x33);  // the update took its copy
    CHECK(SUCCEEDED(deferred->FinishCommandList(FALSE, &list)));
    immediate->ExecuteCommandList(list.Get(), FALSE);
    bytes = ReadBack(updated.Get());
    CHECK(FlagOf(bytes.data()) == 0 && RestIs(bytes, 0x32));
    // Left to the game: another buffer, a part before the flag, another offset, no data, no context, the option off.
    CHECK(!gi::UpdateWithFlagOff(immediate.Get(), game, Handle(other.Get()), 0, UINT64_MAX));
    CHECK(!gi::UpdateWithFlagOff(immediate.Get(), game, Handle(updated.Get()), 0, 180));
    CHECK(!gi::UpdateWithFlagOff(immediate.Get(), game, Handle(updated.Get()), 16, UINT64_MAX));
    CHECK(!gi::UpdateWithFlagOff(immediate.Get(), nullptr, Handle(updated.Get()), 0, UINT64_MAX));
    CHECK(!gi::UpdateWithFlagOff(nullptr, game, Handle(updated.Get()), 0, UINT64_MAX));
    gi::off = false;
    CHECK(!gi::UpdateWithFlagOff(immediate.Get(), game, Handle(updated.Get()), 0, UINT64_MAX));
    gi::off = true;
    bytes = ReadBack(updated.Get());
    CHECK(FlagOf(bytes.data()) == 0 && RestIs(bytes, 0x32));
    gi::OnDestroyResource(nullptr, Handle(updated.Get()));
    gi::OnDestroyResource(nullptr, Handle(other.Get()));
  }

  // 9. Many threads, the option switched all the while (the player's On / Off in a battle), unmaps from the wrong
  // thread, buffers destroyed and made again: every page is freed right after its unmap, so one late write ends the test.
  {
    constexpr int THREADS = 6;
    constexpr int ROUNDS = 20000;
    ComPtr<ID3D11Buffer> shared[3];
    for (auto& buffer : shared) {
      buffer = MakeBuffer(480, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC);
      Announce(buffer.Get());
    }
    std::atomic_bool stop = false;
    std::atomic_uint32_t flags_off = 0;
    std::atomic_uint32_t wrong = 0;
    std::thread switcher([&] {
      while (!stop) {
        gi::off = !gi::off.load();
        std::this_thread::yield();
      }
      gi::off = true;
    });
    std::vector<std::thread> threads;
    for (int index = 0; index < THREADS; ++index) {
      threads.emplace_back([&, index] {
        ComPtr<ID3D11DeviceContext> deferred;
        ComPtr<ID3D11CommandList> list;
        if (FAILED(device->CreateDeferredContext(0, &deferred))) {
          ++wrong;
          return;
        }
        for (int round = 0; round < ROUNDS; ++round) {
          ID3D11Buffer* buffer = shared[(round + index) % 3].Get();
          if (round % 4 == 0) {
            // A real map on this thread's own context.
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(deferred->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
              ++wrong;
              continue;
            }
            gi::OnMapBufferRegion(nullptr, Handle(buffer), 0, UINT64_MAX, reshade::api::map_access::write_discard, &mapped.pData);
            Fill(static_cast<uint8_t*>(mapped.pData), 480, FILL);
            gi::OnUnmapBufferRegion(nullptr, Handle(buffer));
            deferred->Unmap(buffer, 0);
            if (round % 256 == 0) deferred->FinishCommandList(FALSE, &list);
            continue;
          }
          uint8_t* page = Page();
          MapEvent(buffer, page);
          if (round % 97 == 0) {
            // Another thread's buffer unmapped here, without a map of this thread's.
            gi::OnUnmapBufferRegion(nullptr, Handle(shared[(round + index + 1) % 3].Get()));
          }
          if (round % 1013 == 0) {
            gi::OnDestroyResource(nullptr, Handle(buffer));
            Announce(buffer);
          }
          gi::OnUnmapBufferRegion(nullptr, Handle(buffer));
          const uint32_t flag = FlagOf(page);
          if (flag == 0) {
            ++flags_off;
          } else if (flag != GAME_FLAG) {
            ++wrong;
          }
          for (size_t at = 0; at < 480; ++at) {
            if ((at < gi::FLAG_OFFSET || at >= gi::FLAG_OFFSET + 4) && page[at] != FILL) {
              ++wrong;
              break;
            }
          }
          VirtualFree(page, 0, MEM_RELEASE);
        }
      });
    }
    for (auto& thread : threads) thread.join();
    stop = true;
    switcher.join();
    std::printf("switching: %u of %d page uploads got the flag 0, %u wrong\n", flags_off.load(), THREADS * (ROUNDS - ROUNDS / 4), wrong.load());
    CHECK(wrong == 0 && flags_off > 0);
    CHECK(gi::open_map_count == 0);
    for (auto& buffer : shared) {
      CHECK(OpenMaps(buffer.Get()) == 0);
      // Steady again: every upload gets the flag 0.
      CHECK(Upload(immediate.Get(), buffer.Get(), FILL));
      bytes = ReadBack(buffer.Get());
      CHECK(FlagOf(bytes.data()) == 0 && RestIs(bytes, FILL));
      gi::OnDestroyResource(nullptr, Handle(buffer.Get()));
    }
  }

  // 10. One line in the log, and nothing kept after the detach's step.
  {
    const std::unique_lock lock(log_mutex);
    CHECK(log_lines.size() == 1 && log_lines[0].find("the game's Global Illumination flag off in its lighting constants (map)") != std::string::npos);
  }

  // 11. Sky Shade From the Game's Probes: at the probes' rays the compute stage's global_illumination constants (slot 2,
  // 224 bytes, dynamic as the game's) are a copy with g_irradiance_power 0 and every other byte the game's; after the
  // dispatch the game's own buffer is bound again, untouched. Twice (the copy is kept), then another size, a buffer too
  // small and no buffer at all.
  {
    const auto filled = [](UINT bytes, uint8_t fill) {
      auto buffer = MakeBuffer(bytes, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC);
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      immediate->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      std::memset(mapped.pData, fill, bytes);
      immediate->Unmap(buffer.Get(), 0);
      return buffer;
    };
    const auto bound = [] {
      ComPtr<ID3D11Buffer> buffer;
      immediate->CSGetConstantBuffers(gi::PROBE_CONSTANTS_SLOT, 1, &buffer);
      return buffer;
    };
    const auto power_only = [](const std::vector<uint8_t>& copy, uint8_t fill) {
      for (size_t at = 0; at < copy.size(); ++at) {
        const bool power = at >= gi::IRRADIANCE_POWER_OFFSET && at < gi::IRRADIANCE_POWER_OFFSET + 4;
        if (copy[at] != (power ? 0 : fill)) return false;
      }
      return true;
    };
    for (const UINT size : {224u, 224u, 256u}) {
      const uint8_t fill = static_cast<uint8_t>(0x40 + size / 16);
      const auto game = filled(size, fill);
      immediate->CSSetConstantBuffers(gi::PROBE_CONSTANTS_SLOT, 1, game.GetAddressOf());
      gi::SkyOnlyProbeConstants(immediate.Get());
      const auto copy = bound();
      CHECK(copy && copy != game);
      bytes = ReadBack(copy.Get());
      CHECK(bytes.size() == size && power_only(bytes, fill));
      gi::GameProbeConstants(immediate.Get());
      CHECK(bound() == game);
      bytes = ReadBack(game.Get());
      CHECK(std::ranges::all_of(bytes, [fill](uint8_t byte) { return byte == fill; }));
    }
    const auto too_small = filled(208, 0x55);
    immediate->CSSetConstantBuffers(gi::PROBE_CONSTANTS_SLOT, 1, too_small.GetAddressOf());
    gi::SkyOnlyProbeConstants(immediate.Get());
    CHECK(bound() == too_small);
    gi::GameProbeConstants(immediate.Get());
    CHECK(bound() == too_small);
    ID3D11Buffer* none = nullptr;
    immediate->CSSetConstantBuffers(gi::PROBE_CONSTANTS_SLOT, 1, &none);
    gi::SkyOnlyProbeConstants(immediate.Get());
    gi::GameProbeConstants(immediate.Get());
    CHECK(!bound());
    gi::OnDestroyDevice(nullptr);
    CHECK(!gi::probe_constants && !gi::no_power && !gi::game_probe_constants);
    const std::unique_lock lock(log_mutex);
    CHECK(log_lines.size() == 2 && log_lines[1].find("the sky's light alone") != std::string::npos);
  }
  CHECK(gi::lighting_buffers.size() == 1);
  gi::lighting_buffers.clear();
  std::printf("PASS\n");
  return 0;
}
