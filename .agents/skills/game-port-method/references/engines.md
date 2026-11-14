# Engine notes outside Total War

Short facts per engine; Total War's (Warscape) are in `tw-port-checklist`. Each game's README and `HANDOFF.md` hold the details. The fact-sheet questions for a new engine: who owns jitter and render scale, depth direction, deferred or tiled lights, where the tone mapper and LUT sit, which settings are read live.

## Shader sources

- Offline dumps come from the game's unencrypted install packs, or from the maintainer's runtime dumps (`bin/cmd_Decompiler-1.3.16/dump_shader_<game>/`, not in the repo). Never decrypt protected assets or work around a protection (Denuvo, Steam DRM) to read them; code a protection hides is level B only through a runtime dump or an image the add-on saves of its own process (Ryse's and Tales' debug buttons).
- The decompiler traps in `tw-port-checklist`'s `shaders-ui.md` section 1 hold in every engine. One more from Tales: 3Dmigoto misprints integer literals in compute shaders; check them in the bytecode (`fxc /dumpbin`).

## Unreal Engine 4 (Expeditions: Rome 4.23, Tales of Arise 4.21)

Expeditions' exe says `++UE4+Release-4.23-CL-0` (no 4.24+ cvars such as `r.SkyAtmosphere`); older notes and comments call it 4.26. The "4.26" slots below are the slots read from that 4.23 exe: take a game's version from its exe's build string, never from a note.

- **Live graphics settings** are cvars written in memory (`talesofarise_dlss/engine_cvars.hpp`, `expeditionsrome_dlss/engine_cvars.hpp`). Find `IConsoleManager::Get().FindConsoleVariable` by its call-site pattern (every site must agree on the singleton), call it by name from the present thread, and read each value's storage from its getter's code, never by calling it. Validate on `r.ScreenPercentage` (float 1..400).
- **4.21 against 4.23 slots**: `FindConsoleVariable` is vtable slot 0x90 in 4.21, 0xA0 with a third bool argument in Expeditions' 4.23; `AsVariableInt` / `AsVariableFloat` 7 / 8 against 9 / 10; `FConsoleVariableRef` `GetInt` / `GetFloat` 13 / 14 against 15 / 16. Read them from each exe, never copy them between versions.
- Writes skip UE's sinks: per-frame cvars apply live, others at the next area load. Values read only from UE's cached scalability copy (MaxResolution, MaxCSMResolution, ViewDistanceScale, StaticMeshLODDistanceScale, DetailMode, MaxAnisotropy) need `bCallAllConsoleVariableSinks` raised after the write. Raise-only (higher max, lower min), rewrite each frame if the game changes it, the game's value back on "Game's own" and unload.
- Crash traps: every sun cascade shares one CSM atlas (8192 at most; cascades that don't fit are silently not drawn); shadow resolution above 8192 or `r.Shadow.TexelsPerPixel` 4 written live crashed Tales. Bandai's "ScreenScale" cvars are integer divisors (lower is better): read the default from the registration code before choosing a direction. Matrices passed into UE functions must be 16-byte aligned.
- **Expand Gamut off in HDR modes**: zero the `ExpandGamut` term in the LUT builder (CombineLUTs) for every HDR mode, keep it only for SDR; it oversaturated both HDR modes in Expeditions.
- **Extended mode**: Expeditions' HDR Extended is a port of UE Extended's UE Filmic Extended (`src/games/expeditionsrome/ue_extended.hlsli`; source `src/games/ue-extended/`); start a UE game's Extended from it. Its shoulder above paper white rolls off per channel (`min(c, 1) + Neutwo(max(c - 1, 0), peak - 1)`), not by the brightest channel, or fire stays flat salmon (Ryse fix32).

## CryEngine (Ryse: Son of Rome)

- Shaders come packed in `ShaderCache.pak`: there is no offline dump to read before a run, so level B means "seen in a runtime dump" (`dump_shader_ryse`), and level A needs a trace of that run.
- The exe on disk is Steam-DRM encrypted: code offsets come from the image the DLSS add-on saves of its own process (`ryse_image.bin`), never from a decrypted exe.
- Cvars go through the game's console, past its whitelist (`rysesonofrome_dlss`).
- In-place format upgrades lost clamps here: every new add-on uses view cloning (`use_resource_cloning`, `src/mods/swapchain_v1.hpp`); don't retrofit an old one.
