// The seam the shipping constant-buffer ring (S5: src/games/twattila/buffer_pool.hpp) plugs into the map replay.
//
// The ring works below ReShade, as detours on d3d11.dll's context bodies (context_hooks.hpp). The replay makes every
// stream call through the real immediate context's table, so once the ring's detours are in, every Map, Unmap, Set,
// Set1, swap and ClearState of the stream runs through them, timed as the game's would be. The replay therefore only
// hands the ring what the add-on hands it in the game: the device at init_device, ReShade's init_resource and
// destroy_resource for the buffers, the present event, and the add-on's unload. An adapter header (run.ps1 -RingAdapter,
// force-included) defines TW_MAP_REPLAY_RING_PLUGIN and `inline const tw_map_replay::RingPlugin RING_PLUGIN`; the harness
// then offers `--plugin ring`. Every member may be null.
#pragma once

#include <d3d11.h>

#include <string>

namespace tw_map_replay {

struct RingPlugin {
  const char* name = "native";
  // init_device: the device and its immediate context exist, nothing is bound and no stream call has run (the row on at
  // start). False refuses: the run stops.
  bool (*attach)(ID3D11Device* device, ID3D11DeviceContext* immediate_context) = nullptr;
  // init_resource / destroy_resource for each game buffer the stream makes (Resource::game; every buffer with
  // Options::addon_buffers), as ReShade's proxy fires them in the game (initial data: its bytes, or null).
  void (*buffer_created)(ID3D11Buffer* buffer, const D3D11_BUFFER_DESC& desc, const void* initial_data) = nullptr;
  void (*buffer_destroyed)(ID3D11Buffer* buffer) = nullptr;
  // ReShade's present event: on the render thread, before every Present (timed with the Present). `on`: the row
  // (false outside Options' plugin window).
  void (*present)(ID3D11DeviceContext* immediate_context, bool on) = nullptr;
  // The add-on's unload, the device still alive: every buffer the ring holds goes back to its native form.
  void (*detach)() = nullptr;
  // One line for the run's report (pieces used, fallbacks); null: none.
  std::string (*stats)() = nullptr;
};

// The pass-through: native D3D11, the baseline and the "OFF" side of every comparison.
inline const RingPlugin NATIVE_PLUGIN = {};

}  // namespace tw_map_replay
