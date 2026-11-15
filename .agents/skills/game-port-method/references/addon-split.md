# One add-on or two

Since 2026-11-15 Shogun 2, Ryse, Tales of Arise and Expeditions: Rome are one add-on each, like the other TW games: the maintainer chose one add-on (DLSS on Graphics 1 of 2, the higher graphics settings on Graphics 2 of 2, the HDR-only snapshot still working). Before that each shipped the HDR add-on (`<game>`) and `<game>_dlss` (DLSS, frame generation, ray tracing, graphics rows in its own overlay). The facts below (as of 2026-11-15, before the merges) stay for a game that would split again; a new game starts as one add-on.

## What two cost

- **Not two command_action registries.** `utils/command_action.hpp` keeps one process-wide registry (`cross_addon::Shared<SharedData>`), and `cross_addon` elects one module to register ReShade's draw and dispatch events for all. A draw walks one list holding both add-ons' callbacks, in registration order (the HDR add-on first: add-ons load by name). The consequence: a `.bypass` in one add-on skips the other's post callbacks (Ryse DLSS's light-limit dispatch once stopped the HDR add-on's Light Bounce); swap the shader and `.replay`, or put state back in `finalize_callback`.
- **Each add-on's own event handlers, once per event each.** Events an add-on registers directly with ReShade run once per add-on: Ryse's HDR add-on registers `draw` and `draw_indexed` all the time (each returns after one atomic load), its DLSS add-on only while a debug trace or the options measurement runs (otherwise `push_descriptors`, the constant mirror's buffer / resource events, and `present`; with RT on the constant mirror runs twice, the HDR add-on has its own copy); Tales DLSS `draw`, `draw_indexed` and `dispatch` only while a frame or pixel trace runs (`talesofarise_dlss/frame_trace.hpp` UseEvents), otherwise neither Tales add-on registers a per-draw event (its 224 shader-hash registrations sit in the shared registry); Shogun 2's DLSS part (one add-on now) `draw`, `draw_indexed`, `bind_viewports` and `push_descriptors`; `mods::shader` adds `push_constants` and `push_descriptors` in each add-on that uses it with pipeline-layout cloning. Every add-on has its own `present` work.
- **Not two swapchain mods today.** Only the HDR add-on uses `mods::swapchain`; each `_dlss` add-on has its own `init_swapchain` / `destroy_swapchain` / `present` handlers, not a second swapchain mod. A merge must keep it that way.
- **The fast draw dispatch** runs only while every module attached to `command_action` opted in (`game-port-method`'s section). Shogun 2's add-on opts in; Ryse's, Tales' and Expeditions' four add-ons don't, so their draws take `RunCallbacks` either way. Opting both in is the first, cheaper step, before any merge.
- **Settings across add-ons**: through `ReShade.ini` (Shogun 2's `[Shogun 2 DLSS]` keys until its merge, read by the HDR add-on at attach and each frame while ReShade's overlay was open) or through `cross_addon` shared blocks of atomics (`talesofarise_dlss/post_filters.hpp`, append-only). Two pages, two key sets, two places a player looks; an older partner build turns the fast dispatch off.
- **Latency**: the code holds no wait between the two add-ons (the links are atomics and ini reads, nothing blocks on the partner's GPU or CPU work), so the split itself adds no frame of latency. A value one add-on writes after the other read it that frame arrives a frame later: a setting lag, not a picture delay. Re-read this if a link ever gets a wait or a fence.

## What one costs

- The HDR-only snapshot still has to work ([hdr-only-snapshot.md](hdr-only-snapshot.md)): the merged add-on's DLSS, frame generation and ray tracing parts must stay dead and quiet without the helper, the NVIDIA DLLs and the `_dlss` files, and upstream must still build it alone (the DLSS and Streamline SDK headers from `external/`, nothing from `tools/`).
- Saved values: every key keeps its name, and the old `[<Game> DLSS]` values are copied once into the renodx presets (Shogun 2: each preset that lacks the key takes the DLSS section's value, then `[renodx] SettingsRevision = 1`), or players lose their settings; the overlay's rows move under the renodx page.
- One bigger add-on in the upstream snapshot, carrying code upstream doesn't build today.

## Measure before deciding

In the game's fixed scene ([../SKILL.md](../SKILL.md)'s Measurement section), three builds: both add-ons, the HDR add-on alone, the `_dlss` add-on alone; then both with the fast dispatch opted in.

- **Draw replay**: a frame trace of the scene replayed through the dispatch (the TW replay in `tools/tw/draw_redesign`, `drw_cost.py`: ns a command and ms a frame by class of command). A non-TW game needs its trace in the replay's format first (Tales: `talesofarise_dlss/frame_trace.hpp`).
- **WPR**: a sampled CPU profile of the render thread (`game-port-method`'s WPR rule), read by module offsets: the time in each add-on's handlers and in ReShade's event dispatch per frame.
- **PresentMon** CPU busy and GPU wait for the same runs. A per-draw saving shows in fps only where the render thread is the bound.

The answer for the maintainer: ms a frame the split costs in that scene, what opting in saves, and what a merge would change for players (pages, keys, the snapshot).
