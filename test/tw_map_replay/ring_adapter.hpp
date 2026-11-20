// The shipping constant buffer ring (src/games/twattila/buffer_pool.hpp) as the map replay's `--plugin ring`
// (run.ps1 -RingAdapter test\tw_map_replay\ring_adapter.hpp, force-included into map_replay.cpp): handed what the
// add-on hands it in the game. device_loss's device list (init_device), ReShade's init_resource / destroy_resource (the
// game's buffers alone: ReShade's own, as its push buffer, are made below its proxy; --addon-buffers ring offers them
// too), the present event with the row on (the ring goes in at the first one), the row off at the unload, then the
// device's end. The ring's log lines go to stdout.
#pragma once

#include <cstdio>
#include <format>
#include <string>

#include "src/games/twattila/buffer_pool.hpp"
#include "test/tw_map_replay/ring_seam.hpp"

#define TW_MAP_REPLAY_RING_PLUGIN

extern "C" __declspec(dllexport) const char* const NAME = "Map replay ring";
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("ring: %s\n", message); }
}

namespace tw_map_replay {

namespace ring_adapter {
inline ID3D11Device* device = nullptr;
inline ID3D11DeviceContext* context = nullptr;
}  // namespace ring_adapter

inline const RingPlugin RING_PLUGIN = {
    .name = "ring",
    .attach =
        [](ID3D11Device* device, ID3D11DeviceContext* context) {
          twattila::device_loss::devices = {
              {.device = device, .single_threaded = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0}};
          ring_adapter::device = device;
          ring_adapter::context = context;
          return true;
        },
    .buffer_created =
        [](ID3D11Buffer* buffer, const D3D11_BUFFER_DESC&, const void* initial_data) {
          twattila::buffer_pool::Track(ring_adapter::device, buffer, initial_data);
        },
    .buffer_destroyed = [](ID3D11Buffer* buffer) { twattila::buffer_pool::Forget(buffer); },
    .present = [](ID3D11DeviceContext* context) { twattila::buffer_pool::Update(context, true, false); },
    .detach =
        [] {
          twattila::buffer_pool::Update(ring_adapter::context, false, false);
          twattila::buffer_pool::ForgetDevice(ring_adapter::device);
          twattila::device_loss::devices.clear();
        },
    .stats =
        [] {
          const auto& pool = twattila::buffer_pool::internal::pool;
          return std::format("ring {}, {} pieces, full ring {}, stopped {}", (pool.installed ? "in" : "out"),
                             pool.pieces.size(), (pool.full_logged ? "seen" : "never"), (pool.stopped ? "yes" : "no"));
        },
};

}  // namespace tw_map_replay
