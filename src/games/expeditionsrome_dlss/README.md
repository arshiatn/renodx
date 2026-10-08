# Expeditions: Rome — experimental DLAA / DLSS

Separate DX11/x64 AA addon. The `expeditionsrome` HDR folder and the user's
other HDR copy are not changed. This supports **native-resolution DLAA** and
experimental **DLSS Ultra Quality, Quality, Balanced, Performance, Ultra
Performance and Custom upscaling**, not frame generation or a finished release. Custom follows the
game's actual 1–100% Screen Percentage; native 100% uses DLAA. Temporal
Upscaling must be on below 100%. The Temporal Upscaling-off path is not enabled.

## Install (release)

1. Put `renodx-expeditionsrome_dlss.addon64` and NVIDIA's latest
   `nvngx_dlss.dll` (310.5 or newer; TechPowerUp or `lib/Windows_x86_64/rel` of
   NVIDIA's DLSS GitHub) in `ExpeditionsRome\Binaries\Win64`, next to ReShade.
2. In the game: Anti-Aliasing Fast or Ultra, Temporal Upscaling on.
3. ReShade overlay -> Add-ons -> Rome DLAA: pick the AA mode. The render scale
   follows it live; the preset defaults to L.

The overlay warns when `nvngx_dlss.dll` is missing or older than 310.5 (its file
version; an NVIDIA app or Profile Inspector override may load another one). The
log names the version and folder. Build: `cmake --build --preset
clang-x64-release --target expeditionsrome_dlss`.

## This build: Ultra Quality, preset E, live scale retry (not yet tested in game)

- **DLSS Ultra Quality (77%)** after DLAA in the menu. Not an NVIDIA SDK mode
  (NVIDIA removed it): NVIDIA's Quality model at 77% (2957x1664 at 4K). Saved
  values of the other modes are unchanged.
- **Presets**, in order: E (CNN, Fastest, Worst looking, not recommended), K
  (Transformer 1, Slower but better than E), M (Transformer 2, Slower than E,
  recommended for HDR), L (Transformer 2, Slowest, best looking, recommended for
  HDR). L stays the default. E is deprecated in NVIDIA's SDK but still there.
- **Live render scale recovers by itself**: a problem (the game's menu writing
  the scale many times, e.g. dragging its slider; a moved or unreadable
  variable; no effect) now pauses it, puts the game's value back and retries
  after 3 s (the variable is searched again if needed). It stops for good only
  after 5 retries without a verified write. Before, it needed untick/tick. No
  per-frame cost beyond the existing one read.
- Status texts: "keep Fast or Ultra TAA" (Fast works too).

## Previous build: Simple/Advanced, presets, DLL lookup, pulse diagnostics

- **Settings mode** (top of the overlay, saved): **Simple** shows the AA mode,
  the Custom render scale, the DLSS preset, Replace TAA and a short status.
  **Advanced** adds the live render scale checkbox, explanations, statistics,
  diagnostics and the input capture.
- **Live render scale is on by default** (confirmed working in game). The
  restart-based "Render scale (restart required)" Apply/Restore and
  `saved_scale.hpp` are removed; delete that file. A value saved by it stays in
  the game's own setting until the game's menu changes it; the
  `GameUserSettings.ini.renodx-aa.bak`/`.renodx-aa.txt` files can be deleted.
- **DLSS preset**: K, M or L for every mode, **L by default** (NVIDIA's own
  defaults are K for DLAA/Quality/Balanced, M for Performance, L for Ultra
  Performance). Changing it re-creates the feature. A preset forced in the NVIDIA
  app or NVIDIA Profile Inspector overrides it (the DLSS indicator then shows
  `nvapp_override`). Before, the addon always asked for K.
- AA mode menu order: Off, DLAA, Ultra Quality, Quality, Balanced,
  Performance, Ultra Performance, Custom (saved values unchanged).
- **nvngx_dlss.dll lookup**: next to the addon, then next to the game's exe,
  then (development builds only) `RENODX_NGX_RUNTIME_PATH`. The log names the
  folder used. For a release: put NVIDIA's production `nvngx_dlss.dll`
  (`lib/Windows_x86_64/rel` of the DLSS SDK) next to the addon. Wine test of the
  lookup order passed.
- **Blur/sharp pulsing at low scales**: UE 4.26 picks its TAAU jitter pattern
  length as 8 × (output/input)², NVIDIA's own formula, but caps it at 255
  positions. Below about 18% the pattern is shorter than NVIDIA wants (800 at
  10%), so it repeats too soon and the image can pulse with its period (255
  frames, about 0.7 s at 350 FPS). Simple mode shows a fixed warning below
  Ultra Performance (33%). Advanced shows the measured
  **Game jitter pattern** and **NVIDIA history resets** (camera cuts, skipped
  frames, restarts) to confirm that no addon reset causes it.

## Replacing the game's TAA pass (in game: faster, especially when CPU-bound)

**Replace the game's TAA pass (faster)** (overlay, on by default): once the CPU
copy of the constants is trusted, all values are known before the TAA/TAAU
dispatch. The addon then runs input preparation and NVIDIA evaluation inside
that dispatch call and skips the stock dispatch (`command_action` bypass); the
overlay's "GPU stock TAA" reads about 0. If anything fails before NVIDIA runs,
the stock dispatch runs as usual. While learning, after a CPU-copy mismatch, or
with the checkbox off, the previous order applies: stock TAA, then DLSS merged
with its alpha.

New in this build (not yet tested in game):

- NVIDIA writes straight into the game's TAA output; the 4K resolve pass
  (about 0.08 ms) is gone. Non-finite input pixels are now cleaned by the
  preparation pass at render resolution (written to a private copy that NVIDIA
  reads), so they also never enter NVIDIA's history. Output alpha is NVIDIA's;
  the stock pass only uses it as its own history flag, and its history is not
  used while replacing.
- Fewer state resets per frame: no ClearState when entering the private
  context state (it is always left cleared) and none between preparation and
  NVIDIA (only the three views are unbound); the merge path keeps one.
- GPU/CPU timings (timestamp queries) and the constants text are only
  collected while the overlay is drawn.
- The private 4K output/merge textures are only created for the merge path.
- Switching AA modes (or the render scale changing) no longer shuts NVIDIA's
  runtime down: only its feature is re-created (about 20 ms). Before, every
  switch re-initialized it (about 1.3 s freeze in the log). AA Off still frees it.
- The preparation shader passed a Wine/D3D11 test: depth and motion are
  bit-identical to the previous version with and without the color copy;
  non-finite colors (NaN, ±Inf, half overflow) become 0.

## Live render scale (in game: works; mode switches no longer freeze)

The first version searched the game's heap (over 6 GB at 20–30 FPS for
minutes, never finished). The new search (`engine_scale.hpp`) reads only the
game's exe image (tens of MB), about 1 ms per frame, well under a second:

1. Every `TAutoConsoleVariable`-like static in the exe's data:
   {vtable in the exe's read-only data, heap object, value pointer just past it}.
2. The variable whose object's help text starts with "To render in lower
   resolution and upscale for better performance" (r.ScreenPercentage's help;
   same in UE5's docs).
3. If that is not exactly one: the UTF-16 name `r.ScreenPercentage` in the
   exe's read-only data, the code that loads it (RIP-relative references), and
   in that function (bounds from the exe's unwind table) the static the code
   stores the registered object into, after loading the name.

It needs a value of 1–400% (game and render thread copies). Every write
re-checks the static's link. The log line `Found r.ScreenPercentage` or the
warning says how it was found (or what was seen) with exe offsets.

- DLAA 100%, Ultra Quality 77%, Quality 66.66% and Ultra Performance 33.33% (UE renders
  ceil(size × percentage / 100): exactly 2560x1440 and 1280x720 at 4K;
  66.666667 would give 2561 wide), Balanced 58%, Performance 50%, Custom its
  own 1–100% slider. AA Off, or the checkbox off, puts the game's own value
  back.
- A change from the game's menu becomes the value to restore; ours is written
  back. More than 20 such rewrites in about 600 frames pauses it (retried after 3 s).
- 60 temporal passes after a write: no change in the input width pauses it
  ("no effect", retried); a different width than expected is only noted (engine limits).
- Wine test with UE 4.26-like statics in the test exe (GCC puts all
  initializers in one function, with decoys at the same value on both sides):
  found by help text; by name when the help differs; by name when two help
  texts match; moved link refused; implausible value; nothing found when the
  name is gone. 32 MB of random data, read-only data and code: under 0.3 s
  in this slow VM, longest step a few ms (VM scheduling; about 1 ms normally).

## Settings at launch

The AA mode, live render scale, Custom value, preset, settings mode and Replace
setting are read from ReShade.ini when the addon loads, so DLSS runs without
opening the overlay.

## Custom 1–100%

Custom (live slider) goes down to 1%. Below 33% is beyond
NVIDIA's modes; the closest mode is created at the actual input size. NVIDIA
may refuse very small inputs: the status then shows the creation error and
stock TAA runs until the scale changes.

## No GPU wait for constants (in game: constants about 0.01 ms)

The jitter, camera-cut, size and camera constants used to come from a GPU
readback that made the CPU wait every frame (about 1.0 ms at 33%, 1.8–2.2 ms
at Quality, 5.2–6.0 ms native, plus lost CPU/GPU overlap). `constant_mirror.hpp`
now keeps a CPU copy of the game's own uploads of the two buffers the TAA pass
binds: UpdateSubresource (ReShade reports it for the immediate context only),
Map/Unmap and initial data. Only buffers with the same ByteWidth and D3D11
usage as the bound ones are copied, and only the bytes the pass reads (432 and
2080 at offset 0; larger ring-buffer offsets are not copied).

`ConstantReadback` decides when it may replace the wait:

1. Learning: the exact waiting readback stays in use and is compared with the
   CPU copy of the same frame. After 30 identical frames in a row the wait is
   removed (logged: `CPU copy matched 30 exact readbacks; GPU wait removed`).
2. In use: every frame also queues whole-buffer GPU copies that are compared
   later with `Map(DO_NOT_WAIT)`, so nothing waits. Map events do not say which
   context wrote, so a write the CPU copy did not see is caught by the next
   comparison (usually one to three frames later).
3. Any difference turns the CPU copy off for the session and logs the first
   differing constant (three differences while learning). A frame without
   CPU-copy data uses the waiting readback; GPU copies into a watched buffer
   invalidate it.

Overlay: **Constants** shows the source and counts; **CPU copy uploads** shows
the KB copied per frame. The Wine/D3D11 test (UE-style 512-byte DEFAULT and
pooled 8192-byte DYNAMIC buffers) checked learning, 49 frames without waiting
with all GPU checks equal, usage separation, initial/partial/mapped data, copy
invalidation and detection of an unseen write within one check.

Test: same scene and setting, compare FPS and **CPU constants** with the
previous build, Off vs DLSS (33%) and Off vs DLAA (100%). Send `ReShade.log`
if **Constants** stays on the GPU readback or turns the CPU copy off.

## Ultra Performance and saved Screen Percentage (saved percentage removed in a later build)

- **Ultra Performance (~33%)**: NVIDIA reports no dynamic range below 50%
  (Ultra Performance min = max = 1280x720 at 4K), and the game's whole-number
  setting cannot hit 1/3: 33% renders 1268x713. Inputs outside NVIDIA's
  reported range no longer fall back to stock TAA; the closest mode is created
  at exactly the active input size. The same applies to Custom below 50%.
  Inputs inside the range (all tested modes) are unchanged.
- **Render scale (restart required)** in the overlay: Apply writes only the
  `ScreenPercentage` digits under `[/Script/ExpeditionsRome.RomeUserSettings]`
  in `%LOCALAPPDATA%\ExpeditionsRome\Saved\Config\WindowsNoEditor\GameUserSettings.ini`
  (1–100%). The first Apply saves `GameUserSettings.ini.renodx-aa.bak` and a
  `.renodx-aa.txt` record next to it; Restore writes the previous value back
  (only if the addon's value is still there) and removes both. Encoding, line
  endings and all other bytes are kept; unknown formats (UTF-16, decimals,
  duplicate or missing key) are refused without changes.
- The game writes the value it loaded at startup (or last set in its menu)
  back when it quits. The addon then writes its last Apply/Restore value again
  when it unloads, only if exactly that game value is in the file; a value set
  in the game's menu is kept. (Fixed 2026-10-02: the first version compared
  against the original value, so a second Apply such as 33 -> 100, or Restore
  in a later session, was undone by the game.)
- `saved_scale.hpp` was missing from the handed-over files and was rewritten.
  Its file logic passed a Windows (Wine) test simulating the game's startup
  load, menu changes and write-back on quit across sessions, plus BOM/LF files,
  refused formats and the recovery record.

Confirmed in game (2026-10-02): Custom at a saved 33% created DLSS Ultra
Performance at **1268x713 -> 3840x2160** (driver indicator `UltraPerf(0.3302)`)
with both Fast `0x4B43383D` and Ultra `0xCCB4A156` TAAU. Arena: 148 FPS with
DLSS versus 157 FPS native Off at 100% (157 matches a driver frame cap). NVIDIA
evaluation was about 0.99 ms; the per-frame CPU cost (constants queue + wait
about 1.0 ms, submission about 0.16 ms) is what keeps the 33% path below native.

## Current in-game test: Custom high-scale input detection

This revision fixes the Custom holes reported at 53/54/56/59/63/64/66/69%:
the allocation guard now accepts exact, four-pixel-rounded or eight-pixel-rounded
textures. It also recognizes both low/high-scale compute TAAU permutations for
Ultra and Fast TAA. All finite-value, actual-input bounds, format, zero-origin,
full-output and NVIDIA runtime-range checks remain enabled. Unknown or different
pixel TAA paths still fall back to stock; below 100%, keep Temporal Upscaling on.
The earlier 50–70% holes matched four-pixel-rounded UE allocations. The new
`ReShadeDLSS85test.log` identifies a separate reason for the 71–99% failures:
at 85%, Fast uses `0x201C3D3D` and Ultra uses `0x29C140B2`, previously treated
as native-only hashes. All three captures completed four snapshots with zero
readback failures and show actual 3264x1836 scene/depth/velocity inputs and
3840x2160 history/output. CB input extents, scale, jitter and exposure agree.
This revision determines the preparation convention from the validated active
input/output dimensions, not the hash. Exact 100% retains zero-offset native
preparation; reduced inputs use the captured input-pixel jitter. All six hashes
retain the same format, rectangle and scale safeguards. The user has now
confirmed correct Custom operation with both Fast and Ultra TAA using the
rebuilt addon. Further scene/motion and live-transition testing remains useful;
this confirmation is not a performance measurement.

1. Close the game completely. Build target `expeditionsrome_dlss` with configure
   preset `clang-x64` and build preset `clang-x64-debug` in `D:\Dev\renodx`.
2. Keep the existing game-folder AA symlink to that artifact; it automatically
   uses the rebuilt file after a fresh game launch. The current link is named
   `1renodx-expeditionsrome_dlss.addon64`. If using a copied DLL instead, replace
   only that previous AA DLL. Do not add a second active copy, overwrite the HDR
   addon, or replace the ReShade loader.
3. Leave `src\streamline` alone. No Streamline DLL installation is needed. This
   development build locates `nvngx_dlss.dll` in
   `external\DLSS\lib\Windows_x86_64\rel` under the repository automatically.
   Keep that folder available; this is not yet a portable distribution package.
4. Start the game afresh. Keep Ultra TAA enabled and dynamic resolution disabled
   if exposed. Set Screen Percentage to about 67% for Quality, about 58% for
   Balanced, 50% for Performance, about 33% for Ultra Performance, or 1–100%
   for Custom (below 50%: use the overlay's Render scale Apply). Enable Temporal
   Upscaling below 100%, and save/apply settings. For DLAA, use 100% instead. Keep the user's
   finished `expeditionsromeBRANCHclaude` Release HDR addon enabled, but leave
   DevKit disabled for a clean performance comparison. Start a new game process
   rather than hot-loading the addon.
5. In ReShade **Add-ons**, open **Rome DLAA (experimental)**. The **AA mode
   (experimental)** selector remembers the last mode at launch. Choose the corresponding
   mode. Check its **active** status, an increasing **Successful AA frames**
   counter, and the active-input/output dimensions. The addon changes Screen
   Percentage only when you press Apply. Custom picks the closest NVIDIA
   quality mode; at exactly 100% it uses DLAA.
6. After at least 20 seconds enabled in one scene, capture the CPU/GPU timing
   readouts. Compare off/on FPS with the overlay closed too. GPU queries are
   polled without waiting; CPU/GPU times overlap and must not be summed.
   One-time pipeline/SDK initialization is excluded from the averages. Compare
   Off versus the selected mode at the same scale, then native Off/100% and
   DLAA/100%. First test Balanced/58% and Performance/50% separately. For Custom,
   retest 85% first with both Ultra and High (TAA Fast), then 71%, 75%, 99% and
   100%, including changing between them while enabled. Repeat representative
   low/high scales with High (TAA Fast) after Ultra. A scale or temporal-shader
   change recreates private textures/features and resets NVIDIA
   history; a brief first-frame hitch is possible. Quality itself still only
   accepts near-67% inputs, not 50%.
   Toggle the addon Off and back on before collecting fresh timing averages at
   a new Custom percentage; its averages otherwise span the earlier scales too.
   Compare while stationary, panning, zooming, talking, and entering or
   leaving the ship cutscene. Check character edges, ropes, thin geometry,
   moving characters, highlights, menus and UI. Test a resolution change too.
   Give the image a moment to settle after switching: the engine's temporal
   history is not forcibly cleared when returning to stock TAA.
7. If the status stays on stock TAA, looks wrong, or crashes, turn it off and
   send `ReShade.log` plus the exact displayed status and a screenshot. Do not
   recapture the old HDR-folder temporal probe. Returning to stock requires
   no rebuilt HDR mod; selecting Off or removing only this AA addon suffices.

Never install another DLSS injector alongside this prototype. The user has
confirmed DLAA activation and improved motion quality alongside the finished
**Release** HDR addon; broader scene/cutscene testing remains necessary. The
user's `ReShadeDLSSQualityTest.log` confirms successful Quality activation at
2573x1448 to 3840x2160 with no logged evaluation failures. The new Balanced,
Performance and Custom modes still need in-game motion/scene testing.
The new addon does not modify HDR shaders/settings, including
`expeditionsromeBRANCHclaude`. Save before testing; runtime scene quality remains
unverified for the additional modes.

The next three paragraphs describe the readback-only builds; the checked CPU
copy at the top supersedes their wait.

The first in-game build never reached NGX: its CPU-mirror safety check rejected
the entire device when UE4 created deferred work, even though the main TAA pass
was immediate. This build reads the exact bound constants at that immediate
dispatch instead. The current revision queues both exact CB snapshots before
stock TAA and maps them after the stock dispatch has been submitted, allowing
that work to overlap the wait. It still synchronizes with those GPU copies;
**the wait has not been removed**, and any FPS gain must be measured in game.
An eight-slot asynchronous timestamp ring separates stock TAA, input preparation,
NVIDIA evaluation, and merge/copy GPU intervals; it drops measurements rather
than waiting when full. CPU timings separately cover CB queue/completion and
addon submission. A frame-late readback is not a safe substitute for current
jitter, exposure and camera cuts. Two cached target SRVs accommodate temporal
ping-pong without recreating a view each frame.

The latest uncapped, DevKit-disabled native screenshots show 177 to 89 FPS in
the menu and 277 to 118 FPS on the ship. Native NVIDIA evaluation averages about
3.74 ms; stock TAA about 0.16–0.21 ms, preparation about 0.08 ms, and merge/copy
about 0.13 ms. CPU constants queue/wait averages 5.21–6.00 ms; addon submission
about 0.18–0.20 ms. CPU and GPU intervals overlap, so these are not additive
costs. The safe exact readback still waits: no claim is made that Quality removes
that synchronization or guarantees a net FPS gain. The shared diagnostic CPU
constant cache is not an execution-version-safe substitute for this readback.

The Quality screenshots show 190 to 133 FPS in the menu and 166 to 118 FPS in
the arena, assuming identical near-67% settings for each off/on pair. NVIDIA
evaluation averages about 2.09 ms, CPU constants queue/wait 1.80–2.21 ms,
and submission about 0.18 ms. This is not a comparison against native 100%
rendering. The modes update deliberately retains exact GPU readback: removing
its wait safely requires context-aware CPU upload/execution tracking, not stale
constants or an unchecked diagnostic CPU mirror. No wait-removal gain is claimed.

## Capture reduced-resolution inputs without changing HDR

The new **Capture upscaling inputs (15 seconds)** button belongs to this AA
addon; the old HDR-folder probe is not required. It is available with AA Off.
Selecting any active AA mode cancels a diagnostic capture. Capturing does not enable DLSS,
replace a shader, or change game rendering settings.

1. Keep output resolution fixed (currently 3840x2160), choose Ultra TAA, enable
   Temporal Upscaling, and set Screen Percentage near 67% (or 66% if the slider
   cannot select 67). Save/apply the game settings.
2. With AA Off, click the capture button. Move the camera during gameplay;
   wait 20 seconds before saving the log. The capture itself lasts 15 seconds,
   with bounded, nonblocking GPU readbacks and a two-second pending timeout.
3. Repeat at 50% with Temporal Upscaling on, then at 50% with it off. Keep these
   as separate captures; do not switch settings during one capture.
4. Close the game and send `ReShade.log`, naming each capture's settings. Restore
   100% before turning the current native-only DLAA mode back on.

The 220 files in the `new ones` dump are 110 shader pairs (78 PS, 30 VS, 2 CS),
all byte-identical to files already in the parent dump. The newly exercised
CS `0xCCB4A156` and `0xD8C07EEE` have the same external 27/130-vector CB and
t0-t4/u0 contract as the native variants. PS `0x1CC13002` and `0xAFD833AA` use the
different 24-vector pixel contract. PS `0x57192B01` is a potential later spatial
resampler. The capture now watches twelve compute/pixel temporal and spatial
targets, including the four AA permutations in the latest dump; none of the 110
original CSOs declares b13.

The `new ones 2` dump has 18 complete, new CSO/HLSL pairs: 13 PS, 2 CS and 3 VS.
All eighteen bytecodes are distinct from the earlier parent/`new ones` captures,
and their filename CRC32s match. Original bytecode disassembly declares no CB13.
The dump files are unchanged.

| New hash | Static classification | Addon handling |
|---|---|---|
| `0x4B43383D` CS | TAA Fast upsampling, lower-scale cached variant | Enabled with the existing compute input guards |
| `0xB266ECB6` CS | TAA Fast upsampling, higher-scale uncached variant | Enabled with the existing compute input guards |
| `0x3351BD21` PS | Fast pixel TAA | Diagnostic capture only; stock rendering |
| `0x6827D9DD` PS | Full-quality pixel TAA | Diagnostic capture only; stock rendering |
| Nine weighted-filter PS and three matching UV-offset VS | 6/9/18/27/28-tap filters, four with a second texture | Unchanged; bloom use needs runtime tracing |
| `0x84DA2764` PS | Directional spatial upscale/sharpen | Unchanged |
| `0xA4C19553` PS | RGB copy with zero alpha | Unchanged |

The two new compute variants share the original TAAU b0[27]/b1[130], t0-t4/u0,
active extent, jitter, pre-exposure and camera-reprojection contract. Their stock
filter/history weights differ. The two pixel variants instead use b0[24] and a
different UV/depth-neighborhood contract; their layouts are not interchangeable.
No new tonemapper, LUT builder, DOF or godray pass was identified in this batch.

The capture logs allocated versus active extents, resource/view formats, bound
CB ranges, dispatch or RTV/viewport metadata, exact CB values, and the exposure
texel when available. Shader dumps alone do not prove that a temporal output is
full-resolution. The four captures in `ReShadeDLSSTest.log` now prove:

| Game settings | Active input | Allocated input | Temporal output |
|---|---|---|---|
| 67%, Temporal Upscaling on (menu/load and gameplay) | 2573x1448 | 2576x1448 | CS `0xCCB4A156`, 3840x2160 |
| 50%, Temporal Upscaling on | 1920x1080 | 1920x1080 | CS `0xCCB4A156`, 3840x2160 |
| 50%, Temporal Upscaling off | 1920x1080 | 1920x1080 | Pixel TAA then spatial `0x57192B01` to 3840x2160 |

All active shaders produced four successful samples with no failed readbacks or
deferred temporal dispatches. Jitter matches active-input pixel dimensions;
pre-exposure is `b1[129].y = 1`, not the exposure texture's approximately 1.25.
`0xCCB4A156` was directly observed near 67% and at 50%. The current revision also
registers Ultra high-scale `0xD8C07EEE` and Fast low/high-scale `0x4B43383D` /
`0xB266ECB6`, whose matching external compute contracts were checked against
original bytecode. Actual activation at each quality/scale still needs the new
in-game tests; the overlay and configuration log now identify the active hash.
All pixel variants remain diagnostic-only.

The simultaneous-addon startup crash was independently reproduced without the
game: ordinary Debug STL iterator level 2 misread Release level-0 shared objects.
This target now uses `_ITERATOR_DEBUG_LEVEL=0` in every configuration and links
the matching NGX `nvsdk_ngx_s_dbg_iterator0.lib` in Debug. It keeps `/MTd` and
debug symbols; no global build presets or shared utility structures were changed.
Mixing it with ordinary iterator-2 Debug RenoDX addons is still unsupported.
Renaming addons to change load order cannot fix a shared-object ABI mismatch.

## Proven insertion point and safety limits

- Direct, immediate-context scene TAA `CS 0x29C140B2` and `CS 0x201C3D3D` are
  intercepted. Both are confirmed in the 85% runtime capture with reduced
  inputs and full-sized outputs; their hashes do not imply native resolution.
  Pixel TAA `0x8763A5F8` and the half-resolution DOF temporal
  pass remain stock; their layouts are different.
- Reduced-resolution modes intercept immediate scene TAAU compute variants
  `0xCCB4A156`, `0xD8C07EEE`, `0x4B43383D` and `0xB266ECB6`, only with a
  full-sized output. Adapter scale bands are 64–70% for Quality, 56–60%
  for Balanced, 50–52% for Performance, 32–35% for Ultra Performance and
  1–100% for Custom. Outside NVIDIA's reported range the feature is created
  at the active input size. Custom also accepts reduced inputs
  through `0x29C140B2`/`0x201C3D3D`; exact 100% selects native preparation and
  DLAA independently of shader identity. Zero-origin
  input/output rectangles and exact, four- or eight-pixel-rounded input allocations are
  required; the three padded columns in the captured 67% input are excluded.
  A temporal-shader identity or native/TAAU preparation-convention change at the same resolution also
  recreates the feature, rather than reusing history with different jitter math.
- Current scene `t1` is pre-tonemap RGBA16F; depth `t2` is reversed-Z
  `R32_FLOAT_X8X24`; packed object velocity `t3` is RG16_UNORM. Camera motion
  comes from live `b1[118..121]`; pixel jitter from live `b0[26].xy`.
- Both bound constant buffers are copied in full to reusable private staging
  buffers before stock TAA, then read at the bound D3D11.1 offsets after the
  dispatch has been submitted and the copies have completed (now the learning
  and fallback path; see the top). Buffer
  dimensions, device and bound ranges are checked. Deferred uploads that have
  already executed on the immediate context are supported; a TAA dispatch itself
  recorded on a deferred context remains unsupported. Native copies/maps do not
  change pipeline bindings, and active predication is refused.
  ReShade hooks a buffer's `GetDevice` to return its proxy, so device ownership
  checks compare the unwrapped native identity, not raw proxy pointer equality.
  The owning returned COM reference remains intact. A failed read now reports
  the exact buffer/range/allocation/map reason rather than a generic message.
- Only one full-screen native or supported upscaling view per presentation is supported. Source,
  output, depth, velocity, dispatch dimensions and constant-buffer layout are
  checked. Multi-swapchain, stereo, subviews and other temporal permutations are
  outside this prototype. The latest initialized DX11 swapchain supplies the
  expected dimensions.
- Without replacing (see the top), the original TAA dispatch runs once first. DLAA reads **pre-TAA** scene color
  and writes private RGBA16F. In DLSS, preparation and motion use active input
  dimensions, while NVIDIA output and merge use full output dimensions. Its RGB is merged with the original TAA alpha
  metadata and copied into the original result only after successful evaluation.
  This is deliberately slower than a finished replacement: original TAA is
  retained for the engine's history/alpha contract, not fed into DLAA.
- All native graphics state is restored through D3D11.1 context-state objects.
  Missing SDK/input/support or failed evaluation leaves the original output.
  Nonfinite SDK RGB falls back per pixel to stock RGB; finite HDR values are not
  clipped by the merge.
  Feature history resets on enable, camera-cut flag, skipped views and resize.
  Unsupported SDK scale/settings are recoverable: the next supported input
  can retry without changing the AA mode. Genuine initialization/evaluation
  failures remain latched until a reset, avoiding repeated expensive failures.
- No tone curve, LUT, gamma, gamut, UI brightness or HDR output changes occur.

Private embedded compute shaders follow CRC-style repository naming:
`0xB69D8215` prepares dense current-to-previous **pixel** motion and center depth;
`0xA3450ADB` merges RGB with stock alpha. Neither is registered as a game-shader
replacement. No dumped CSOs are stored in this source folder.
Reduced-input preparation reconstructs unjittered camera coordinates from input pixel centers by
subtracting the captured input-pixel jitter before the camera transform. Native
DLAA retains its original zero-offset preparation. `PixelJitter` reuses the former
padding at byte 72; the preparation CB remains 80 bytes. All modes separately
pass captured jitter to NGX evaluation.

## SDK and verification

Uses the existing public NVIDIA NGX SDK in `external/DLSS`, with this mod's own
project UUID and static-runtime-compatible libraries. No borrowed game/app ID,
Streamline interposer, Luma dependency, vendor edits or OTA update calls.
NGX creates its development cache in `%TEMP%\RenoDX_ExpeditionsRome_DLAA`.
The runtime is looked up next to the addon or the game (see the top section);
release packaging of NVIDIA's DLL is still to do.
The source license does not relicense NVIDIA's SDK/runtime. `Backend` is movable,
not copyable; the adapter serializes evaluation/reset, restores native state,
and releases features outside the Windows loader lock before addon unload.
The preset is the overlay's choice (NVIDIA default per mode, J, K, L or M);
auto-exposure is enabled with explicit pre-exposure. Driver-forced presets
remain outside the addon's control.

The focused Clang x64 Debug addon build passes. `tests/utility_shaders.cpp`
executes on D3D11 WARP and checks camera/object motion direction, nearest reverse-Z
depth selection, unchanged center depth, finite values, non-8-multiple bounds,
RGB/alpha merge, and restoration of compute/graphics/ranged-CB/predication state.
It also passes two signed fractional-jitter/projective reprojection cases,
excludes poisoned padding from 13-wide inputs in 16-wide allocations, and checks
every pixel of a separate 31x17 output's finite HDR RGB/stock-alpha merge.
The D3D11 debug layer is not installed here, so hazard diagnostics are unverified.
`tests/constant_readback.cpp` exercises exact GPU ordering, deferred uploads,
bound offsets/ranges, buffer reuse/resizing and unchanged state. The old
device-wide CPU-upload mirror was replaced by the checked CPU copy at the top.

`tests/performance.cpp` passes on WARP: independent GPU intervals exclude
interphase/CB-wait gaps, invalid samples are rejected, ring pressure drops
measurements, and partial captures/moves/device changes preserve safety and
pipeline state. `tests/addon_scheduling.cpp` invokes the actual addon post/finalize
callbacks on WARP: a rejected candidate releases its same-frame claim, a valid
later candidate retains it, and advanced Present, expired generation, or disabled
callbacks cancel pending snapshots without replacing newer state. It does not
attach ReShade or initialize NVIDIA. Quality coverage includes the captured
2573x1448 active / 2576x1448 allocation, wrong-scale and excessive-padding
rejection, and a mode change between pre/post callbacks.
`tests/temporal_capture_smoke.cpp` passes on WARP for all twelve targets, each
individually queued with exact CB/exposure snapshots checked after source-buffer
reuse. It also checks duplicate suppression, the eight-pending-slot bound and reset/requeue;
the real ReShade presentation-driven asynchronous drain still needs the
in-game capture above when new permutations/settings are exercised. The user's
four current captures have now validated that drain with zero failed readbacks.

The split `Queue`/`Complete` path passed the existing actual-ReShade/NVIDIA
fixture: CB snapshots remained byte-exact after intervening GPU dispatch and
updates to those same buffers; a later read saw the new data. Cancellation and
duplicate-operation guards passed, as did NGX resize/reset and unsupported
device fallback. Device flags `0x21` are single-threaded and reject deferred
context creation, so that fixture does not claim deferred-replay coverage.
`tests/constant_readback_split.cpp` compiles, but its temporary executables
disappeared before execution; their cause is unconfirmed. No security settings
were changed. The successful actual-ReShade fixture is the runtime verification
for this scheduling change.

`tests/ngx_backend_smoke.cpp` passed on NVIDIA hardware: public SDK initialization,
native DLAA creation/evaluation, ownership moves, resize, reset/reinitialization,
invalid-input rejection and output readback. All RGB was finite; outside the
driver's DLSS indicator, the constant-color test had no differences greater than
32 half-float ULPs (maximum observed: 15). This is not a substitute for in-game
motion/scene-cut tests.
The extended backend smoke test also passed with the actual ReShade loader on
NVIDIA hardware: active 2573x1448 on a 2576x1448 allocation to 3840x2160,
1920x1080 to 3840x2160, and 960x540 to 1920x1080. Poisoned input padding did not
leak, all output RGB was finite, and native callers omitting output dimensions
remained compatible. Quality/native transitions, output resize, reset/move and
invalid dimensions/modes/aliases/device ownership were checked. These are
synthetic constant-color, zero-motion tests, not proof of in-game SR quality.
The unsupported WARP-device test also returns false and latches fallback without
per-frame initialization retries. A reproducible hardware smoke-test build
command is included at the top of its source file.

The current backend smoke test passed 26 synthetic scenarios on the NVIDIA GPU
under the real ReShade loader: Balanced around 58%, Performance at 50%, Custom
50/53/54/56/58/67/69/75/85/99/100%, ascending/descending scale and mode transitions, padding,
resize/reset/move, invalid-input recovery and unsupported-device fallback. SDK
Quality/Balanced/Performance each report a 1920x1080–3840x2160 supported range
at 4K on this installed runtime. Exact-native Custom creates DLAA. Feature
creation uses the SDK's returned optimal dimensions; evaluation passes the
actual active rectangle. Above-optimal Custom inputs were checked repeatedly
after reconfiguration. RGB remained finite, padded pixels did not leak, and
maximum constant-color error was 15 half-float ULPs outside the existing NVIDIA
driver indicator. This does not establish in-game motion quality or FPS.

The current scheduling fixture passes 37 named scale/layout cases plus existing
cancellation/ownership tests against the actual adapter callbacks. It covers
fixed-mode boundaries, Custom 50/58/67/80/85/99/100%, CB-derived native/reduced preparation,
aspect and padding checks, and nonfinite/fractional/zero-size rejection. It
also checks every integer Custom percentage from 50 through 100 with UE-style
four-pixel-rounded allocations for both Ultra and Fast shader identities (102
frames), using the captured `0x29C140B2`/`0x201C3D3D` high-scale identities,
plus the six-entry registration mapping. Twenty-four additional same-hash
100%/85% transitions verify that stale initial flags cannot override the CB-derived
preparation convention. It constructs frames directly.
`tests/addon_on_command.cpp` additionally passes 21 cases through the actual
pre/post/finalize callbacks with typed ReShade API mocks and real WARP texture
bindings and CB snapshots. It reproduces 29/201 at 71/85/99/100%, verifies
mode-scale rejection and malformed-resource/CB/dispatch fallback, and checks
claim/runtime ownership. It does not initialize NVIDIA or intercept a game;
high-scale activation, motion quality and live transitions still need in-game
testing. The Clang x64 Debug addon target and
standalone actual-DLL attach/detach checks also pass; Release-compatible shared
registration was checked in both loading orders. No HDR source files changed.

The previous working Quality-only addon was preserved at
`%TEMP%\exprome_before_scaling_20261002\renodx-expeditionsrome_dlss.addon64`.
Close the game before restoring that file over the Debug build output if needed.

The working all-mode addon from before this Custom fix was preserved at
`%TEMP%\exprome_before_custom_fix_20261002\renodx-expeditionsrome_dlss.addon64`.
Close the game before restoring it over the Debug build output. Its SHA256 is
`64D07E7CFA5EAE8FB70324C75EB82FB5935EDB6B71750C6CEA1AE32DA0B78DF4`.

The working addon immediately before the high-scale detection fix was preserved
at `%TEMP%\exprome_before_high_scale_fix_20261002_268aa45b\renodx-expeditionsrome_dlss.addon64`.
Its SHA256 is `FFDD1397C18AE900F41476E46046B461C78817C9EAF56E7D802F5E3F2E7D0EAD`.
Close the game before restoring a backup over the Debug output.

Off-game startup tests also passed for the actual rebuilt Debug addon, alone
and with a Release shared-registration fixture in both loading orders. Attach
and detach balanced events, overlays, shader callbacks and addon registrations.
The current six AA plus twelve capture registrations total eighteen standalone,
or nineteen with the Release probe; all three fresh-process checks passed.
The host used minimal ReShade stubs, not the game or real HDR/DevKit addons;
fresh in-game validation is still required.

The proxy-aware readback fix also passed an isolated test with the actual
ReShade 6.8.0.2155 loader and game device flags `0x21`: two proxy-created
4096-byte CBs bound through the proxy context were read byte-exactly through
the native context, including the real hooked `GetDevice` behavior. Native NGX
evaluation with proxy-created textures also passed, including resize/reset and
unsupported-device fallback. No game or HDR addon was loaded for this test.

The NVIDIA DLSS information indicator may be enabled through the NVIDIA driver's
`ShowDlssIndicator=1024` setting on this machine; this produces SDK overlay pixels
in synthetic tests. The prototype does not change that registry preference.

References: [NVIDIA NGX/DLSS guide](https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf),
[D3D11 context-state restoration](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate).

## Below-50% engine-scale probe (restored; override not honored)

The addon still follows the engine's actual input scale. Its saved-percentage
control (see the top) needs a restart; there is no live engine control.
`tests/engine_scale_probe.ps1` prepares a fixed 33.333333% restart-required probe.
With explicit user approval, it was applied on 2026-10-02. Only the 50-byte
SystemSettings/ScreenPercentage addition was made; the original 979-byte file,
its attributes and a checksum manifest were preserved. GameUserSettings.ini
was unchanged. No addon rebuild or HDR modification was needed.

The retained recovery directory is
`C:\Users\arshi\AppData\Local\Temp\RenoDX_Rome_EngineScaleProbe_20261002_160649_80e36679cac7440ca2dcfab269bb4aec`.
Original Engine.ini SHA256: `065E61D348C59D0C3C327C87F817A3AB33937691EE021E06B7BFD5EFC1913A6E`.
Applied Engine.ini SHA256: `CDC0A5ACA9FB29FA9184ECDBE6F0CA58BDD82E993B18AB3EDBFC8457471E6C34`.

`ReShadeDLSS33test.log` generation 2 captured Fast TAA `0x201C3D3D`:
all four samples used active input **2727x1534** (allocation 2728x1536),
output **3840x2160**, and CB0[26].z **0.710156261920929**. The summary reports
2481 commands, four completed samples, zero failures and zero deferred readbacks.
Thus rendering stayed at about 71%, despite the Engine.ini 33.333333% override.
The log does not establish whether the game ignored or overrode that CVar;
below-50% rendering and Ultra Performance remain unproven. The menu label alone
is not the evidence for this conclusion.

The game reserialized blank lines around the newly appended SystemSettings
section. Its rewritten Engine.ini was preserved as `Engine.ini.game-rewritten`
in the recovery directory (SHA256
`BECAE37CF0D3450F8FAF95C28C543283D16B203C91B7144450669B0E2124291E`).
After verifying that no settings other than the probe differed from the original,
the probe section and its extra blank line were removed with the game closed.
The restored file matches the original SHA256 above exactly: 979 bytes,
attributes 32, no ScreenPercentage override. GameUserSettings.ini is also
unchanged (SHA256 `AB259986923AF7D9F0ED01517F664A2742CF44A118048820EB678065E4641DE1`).
No addon rebuild or HDR modification was made for this capture or rollback.

The probe script's Restore operation deliberately requires its exact applied
hash and refuses intervening edits. Review any differences and preserve a recovery
copy before manually removing only an owned probe; never force an unchecked
backup overwrite. Do not repeat this Engine.ini probe or modify another game
configuration file without approval for the new test.
The script's ten fixture scenarios cover exact UTF-8/UTF-16, BOM/newline/attribute round
trips, preserved settings, reapply/conflict rejection and game-running guards;
an edit injected during restore verification is also preserved and rejected.
They do not prove the shipping engine accepts the override.

## Saved Screen Percentage probe (33%; confirmed and restored)

With separate user approval on 2026-10-02, the game was closed and only
`ScreenPercentage=71` was changed to `ScreenPercentage=33` in
`C:\Users\arshi\AppData\Local\ExpeditionsRome\Saved\Config\WindowsNoEditor\GameUserSettings.ini`,
under `[/Script/ExpeditionsRome.RomeUserSettings]`. The integer spelling follows
the existing saved value; fractional parsing and limits at other percentages
have not been validated.
Byte comparison against the original confirms this is the only change: the
2664-byte file's UTF-8/CRLF format and attributes 32 are preserved. Fast TAA
(`sg.AntiAliasingQuality=2`), Temporal Upscaling on, dynamic resolution off,
3840x2160 output and `sg.ResolutionQuality=100.000000` are unchanged. Engine.ini
still matches the original hash in the restored probe section above. No HDR or
AA code/binary changes and no rebuild were needed.

The retained recovery directory is
`C:\Users\arshi\AppData\Local\Temp\RenoDX_Rome_SavedScaleProbe_20261002_163146_bb129ddf00e44305a302a68d5311796c`.
It contains the exact `GameUserSettings.ini.original` and `manifest.json`.
Original SHA256: `AB259986923AF7D9F0ED01517F664A2742CF44A118048820EB678065E4641DE1`.
Applied SHA256: `DD8ECA850CC7FB52DF5D279C2C847F778C72D9768CF2C93BEBBD4C2D2A578F32`.
Do not use `tests/engine_scale_probe.ps1` to restore this different probe.

`ReShade33Test.log` generation 2 confirms that the saved setting was honored:
Fast TAAU `0x4B43383D` used active input **1268x713**, input allocations
**1268x716** (four-pixel padding), and temporal output **3840x2160** in all
four samples. CB0[26].z is about **0.33020833**; output rect CB0[23] spans the
full output, and input origin CB0[24] is zero. The summary reports 3467 commands,
four completed samples, zero failures, eight CB readbacks and zero deferred
readbacks. Thus the game supports actual rendering below its menu's 50% minimum
through this saved setting. The menu label and blur were not the proof: the
captured active input was. Integer 33% does not produce exact 1280x720 at 4K.

The AA addon was Off for this capture. This was stock TAAU, not DLSS Ultra
Performance. This test proves a restart-based configuration path, not a live
engine CVar API. Below-50% DLSS creation is described at the top.

With the game closed, the unchanged applied file and original backup were
verified, and only `ScreenPercentage=33` was restored to `ScreenPercentage=71`.
The resulting file exactly matches the original SHA256 above: 2664 bytes,
attributes 32. Engine.ini also still matches its original SHA256. The manifest
now records Restored; the recovery copy remains available. No HDR files or AA
code/binaries changed, and no rebuild was needed for this probe or rollback.

## Next stage

Test replacing the TAA pass, the live render scale and Custom down to 10% in
game. What remains is mostly NVIDIA's evaluation, set by the preset.
Changing the SDK enum alone cannot reduce the game's rendering workload.
Simply changing the SDK quality enum is not a performance-upscaling implementation;
this adapter explicitly uses independent active render and full output extents.
Stock TAA/TAAU is retained until bypass can preserve permutation-specific
alpha/history and safe fallback.
No changes to the user's completed HDR shaders are planned.
