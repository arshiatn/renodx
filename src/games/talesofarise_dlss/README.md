# Tales of Arise — DLSS / DLAA + graphics settings

ReShade addon (DX11, x64), separate from the HDR addon (`talesofarise`):

- **NVIDIA DLAA and DLSS** (Ultra Quality 77%, Quality 67%, Balanced 58%, Performance 50%, Ultra Performance 33%,
  Custom 1-100%) on the game's own TAA pass, with the camera jitter they need (the game turns it off). With DLSS, motion
  blur and the tonemapper run at the output size, so nothing after DLSS is upscaled again.
- **Graphics settings** beyond the game's menu, applied live: preset buttons and one choice per setting.

## Install

1. Put `renodx-talesofarise_dlss.addon64` and NVIDIA's latest `nvngx_dlss.dll` (310.5 or newer) in
   `Arise\Binaries\Win64`, next to ReShade.
2. In the game: Anti-Aliasing on **TAA** (the addon sets the render scale itself).
3. ReShade overlay -> Add-ons -> **Tales of Arise DLSS + Graphics**: AA mode **DLAA** or a **DLSS** mode. The status
   reads "DLAA active — native resolution, replacing the game's TAA" or e.g. "DLSS Balanced active — 2240x1260 to
   3840x2160, replacing the game's TAA".
4. **Graphics**: Game's own / High / Ultra / Extreme / Maximum / Beyond Maximum (also outline distance 750000,
   NPC distance 750000, monster distance 10000, NPC / monster detail distance 1000, terrain detail distances 50,
   terrain detail bias -2, grass draw distance 20), then any setting on its own.
   Every preset keeps the game's volumetric lighting and fog (finer and cheaper values in their lists). Skit
   resolution 100%, 200% from Maximum on; smooth cutscenes on in every preset, Game's own included. A preset never lowers what the game
   already sets. Some settings apply after loading a new area. Maximum is very heavy (lots of VRAM). Volumetric fog
   can be turned off (Volumetric fog 0, in no preset: its cell size / depth slices grey out) and the volumetric
   settings also offer cheaper values than the game's. Loading -> Shaders on load (r.CreateShadersOnLoad 1 or 0, in
   no preset): an area's shaders created while it loads, fewer first-use hitches; the engine's default is 0.
5. Optional, **DLSS Frame Generation + Reflex** (Experimental): copy `sl.interposer.dll`, `sl.common.dll`,
   `sl.dlss_g.dll`, `sl.reflex.dll`, `sl.pcl.dll` and `nvngx_dlssg.dll` (NVIDIA Streamline SDK 2.14.16, `bin\x64`)
   next to the addon, tick **Frame generation** in the DLSS section and restart the game. HDR: the HDR addon's Output
   on HDR10 or Auto (frame generation takes no scRGB). RTX 40 or newer, hardware-accelerated GPU scheduling on.
Thanks to emoose: the "Arise-SDK's settings" group (NPC / monster distance, NPC / monster detail distance, never hide
characters, full-rate animation, outline distance, skit resolution, smooth cutscenes) is his Arise-SDK's ideas and
needs Arise-SDK (www.nexusmods.com/talesofarise/mods/5): greyed out without it; with it the rows set Arise-SDK's own settings
(NPC distance: the addon's code; full-rate animation: Unreal's a.URO.Enable). Sun shadow distance above 2 (and Far sun
shadows) is also his idea (Arise-SDK removes the game's limit) and needs Arise-SDK: without it the game's limit of 2 stays. No Arise-SDK
code or byte patterns are in the addon. Everything else is not from Arise-SDK.

5. Arise-SDK (https://www.nexusmods.com/talesofarise/mods/5, newer than its GitHub): needed for the "Arise-SDK's settings" group; it also
   skips the intro logos. Use the included `Arise-SDK.ini` (the game's values; NPC distance -1, so the addon's NPC
   distance works). Windows display scaling above 100% without Arise-SDK:
   "Tales of Arise.exe" -> Properties -> Compatibility -> Change high DPI settings -> Override high DPI scaling
   behavior, scaled by Application.

DLSS (v51): see "DLSS upscaling" below.

## Technical notes (UE 4.21)

- TAA: compute `0x88765764` (twin `0xE2AF861D`), UE 4.21's own TAA shader, in the game's AA mode 5 (Bandai's settings)
  or mode 2 (Arise-SDK's UseUE4TAA): `t0` depth, `t1` scene color, `t2` history, `t3` packed velocity, `u0` output
  R11G11B10F, 8x8 groups. View (`b1`): `ClipToPrevClip` 114..117, `TemporalAAJitter` 118, `ViewSizeAndInvSize` 122
  (checked), `BufferSizeAndInvSize` 123. No pre-exposure (DLSS gets 1), no camera-cut flag.
- Inputs: color copied to RGBA16F and depth / motion prepared by `0x7A1E0D01` (nearest depth of the diagonals 2 texels
  away, as the game's TAA); NVIDIA writes RGBA16F, `0xA3450ADB` converts it into the game's R11G11B10F target.
- Camera jitter: the game multiplies UE's jitter by a per-view factor that is 0 in its mode 5 (`cmp [view+0x1BE0], 5;
  jne; mulss` at exe+0x1A86CB2). While DLAA is on that `jne` becomes a `jmp` (`code_patch.hpp`; restored on Off and
  unload).
- Render scale: `r.ScreenPercentage` found by its help text (`engine_scale.hpp`), kept at 100% for DLAA.
- No upscaling: UE 4.21 flags temporal upscaling only for AA mode 2 with r.TemporalAA.Upsampling 1 (exe+0x21B9A75; the
  game's AA setup starts from mode 5, exe+0x21B9FEA). With both patched the view was flagged and the TAA still ran at
  the render resolution, then the spatial upscale (Catmull-Rom `0x57192B01`): the game's post-processing only has its
  own TAA routine.
- Graphics settings (`engine_cvars.hpp`): cvars by name through the engine's console manager (the singleton every
  `FindConsoleVariable` site loads, vtable slot 0x90); values at the offset `AsVariableInt` / `AsVariableFloat`
  (slots 7 / 8) return, or a FConsoleVariableRef's MainValue (`GetInt` / `GetFloat`, slots 13 / 14) and the global
  before it. Checked on `r.ScreenPercentage`. Written directly (no change callbacks): per-frame settings apply live,
  others on area load. Raise-only (higher-is-better: max of the game's and the choice; LOD scales, thresholds,
  divisors: min). Table: `GRAPHICS_SETTINGS` in `addon.cpp` (52 settings in 6 groups; each with 4 preset values and
  at most one extra list-only value). Choices saved in ReShade.ini, "Tales Graphics Settings", per cvar name.
- Shadows above 8192 or texels per pixel 4, written live, crash (exe+0x1A9E56C, a null shadow atlas; most likely sized
  from UE's cached scalability copy, which live writes do not refresh): no choice goes above 8192 / 3 (the extra choices 16000 and 4 were removed in v20).
- v22: all sun shadow cascades share one 8192 x 8192 atlas (`1 << (GMaxTextureMipCount - 1)`, at most 14 mips;
  exe+0x1AD7041) and a cascade that does not fit is not drawn. The game allows 10 cascades, which only fit at its own
  2048: 8192 drew one cascade (the "pixelated shadows at higher presets"; above 8192 none, the crash). Sun shadow
  resolution (r.Shadow.MaxCSMResolution) is no longer changed. Unreal's cached scalability copy (shadow resolutions, draw and object detail
  distance, detail mode, anisotropy) is refreshed after each write by raising FConsoleManager's
  `bCallAllConsoleVariableSinks` (+0xCC, tested by `CallAllConsoleVariableSinks`, vtable slot 7, every game-thread
  frame); before, these applied only when the game set a variable itself (its menu).
- Bandai's resolution divisors (registration defaults: SSR 2, SSR color 2, translucent SSR depth 4, clouds 3, cloud
  shadows / ray march 8, volumetric lighting 8 px per cell, ground fog 8) go to 1-2; its blur divisors stay (their
  radius is in pixels of the smaller buffer). Not settable live: r.ForceLOD (only looked up in the exe).

Build: `cmake --build --preset clang-x64-release --target renodx-talesofarise_dlss.addon64` (the NGX link is set for
this target in `CMakeLists.txt`).
- v23: presets High / Ultra / Extreme / Maximum (the old Maximum column dropped, Beyond Maximum renamed). New row
  r.VolumetricFog (0: off), volumetric settings EXACT with cheaper list-only values (Bandai lighting cell 16, fog cell
  12 / 16, depth slices 96 / 64; GraphicsSetting::more_extras), r.VolumetricFog 0 greys out r.VolumetricFog.*.
  Shaders on load EXACT 0 / 1. Preset buttons (except Game's own) skip settings in no preset. The pre-v16 single
  "Tales Graphics" / "Preset" key is no longer read.

## Sliders (2026-10-11)

Every graphics setting is a slider over its choices, showing the current one (as Ryse's options), with steps between
neighbouring choices: every whole number across small gaps (cascades 6 -> 10: 7, 8, 9), the 1-2-5 numbers across gaps
of more than 2x (draw distance 3 -> 20: 5, 10). Choices are saved by value in a new ReShade.ini section
("... Graphics Values"; "game" = the game's), so added steps never change a saved choice; the old index section is
not read (press a preset once).
Every setting has a short tooltip (2026-10-11).
v26: every preset sets every setting (KEEP: the game's) except volumetric fog off and shaders on load (PLAYER_SETTINGS;
Game's own resets them too), so no older choice stays.
v27: shaders on load is in no preset (Game's own included); every other setting is set by every preset.
v28: Maximum (and Recommended) use object detail 0.0001 and grass draw distance 10 (their old extras; the extras are
now .01 and 4). Sun shadow distance 1.25 / 1.5 / 2 / 2.5 / 3 / 3.5 / 4 (extra 4, 3.5 via more_extras, 2 / 2.5 by a new
step rule: halves across small gaps of halves). Recommended renamed "Recommended (Maximum with stock volumetric fog:
faster, no visual difference)".
v29: Sun shadow cascades now EXACT 4-9 (the game: 10; in no preset) and Sun shadow resolution is back (2730 / 4096,
in no preset): all cascades share the 8192 CSM atlas, so the written resolution is capped to 8192 / ceil(sqrt(cascades))
(10: the game's 2048, 5-9: 2730, 4: 4096), never dropping a cascade or exceeding the atlas (the old 16000 crash: none
fit). Sharper sun shadows shimmer less when the sun moves (ATN: every ~10 s). Sun shadow distance 2 at Maximum and
Recommended (no Arise-SDK needed; 2.5-4 in its list need Arise-SDK).
v30: the v29 sun layout reverted: the game sets r.Shadow.MaxCSMResolution 4096 itself (ATN), the most 2-4 cascades
fit in the 8192 atlas (so its sun uses at most 4), and 8192 fits one: no higher sun resolution is possible. Instead a
test, in no preset: Soft sun shadows (r.Shadow.FilterMethod 1, UE 4.21's experimental PCSS for directional / spot
lights) against the shimmer when the sun moves.
v31: Sun shadow resolution back as a test (6144 / 8192, in no preset): the atlas size `1 << (GMaxTextureMipCount - 1)`
is one instruction (`mov r13d, 1` ... `shl r13d, cl`, exe+0x1AD705A); while the resolution is written above the game's,
its 1 becomes 2 (SUN_SHADOW_ATLAS): a 16384 atlas (the game's D3D11 shadow map limit, exe+0x179F800) holds 4 cascades of
6144 or 8192, as many as 8192 holds of 4096. Finer texels against the sun-move shimmer at distances above 1. Rows
already at the game's value (ShadowQuality 5, 10 cascades, MotionBlurQuality 4) are Unreal's limits; pre-shadows stay 1
(above it a close-up's pre-shadow can outgrow the shadow atlas); they stay for lower in-game settings.
v32: ATN: the higher sun resolution fixes the shimmer. Sun shadow resolution in the presets: High / Ultra 6144,
Extreme / Maximum / Recommended 8192. The PCSS row is removed (no visible effect).
v33: Keep characters behind the camera (Arise.PF.Culling.ForceInFrustum 1; Extreme, Maximum, Recommended). Bandai's
PFCullingManager (exe+0xA0C230, cvars read every frame) hides characters outside the view frustum; turning back fades
them in (dithered) and rebuilds them, an fps drop for 1-2 s (ATN; screenshots 2026-10-12). 1 skips the frustum test
(every object "in frustum"). Arise-SDK's CharaDisableCull hooks UAchCharacterBuildComponent::SetCulling, another
system. Arise.PF.Culling.Enable 0 would stop the manager, leaving whatever it had hidden hidden: not used.
v34-v36: Characters, outline distance and Cutscenes, settings without a console variable (engine_hooks.hpp): the
addon's own values, read by hooks in the game's code, installed in AddonInit (ReShade loads the addon on the game
thread during D3D11CreateDevice, before any level). Found by byte patterns of this exe (v36: all chosen anew); a hooked
function starts at its unwind entry (RtlLookupFunctionEntry + chained entries). Offsets from the exe's reflection
params (UE4CodeGen offset at +0x28): APFNpcManager SpawnSettings 0x338 / CameraSettings 0x344 / High 0x374 / 0x380,
AEncountGroup.SpawnAreaInfo 0x410, FPFNpcWalkSpawnParams.DespawnMaxDistance 8,
FPostProcessSettings.StageEdgeBaseDistance_TO14 0x700 (FSceneView's FinalPostProcessSettings 0xD60: BloomIntensity
0x248 zeroed at view+0xFA8), UTextureRenderTarget2D SizeX / SizeY 0xC0 / 0xC4, ULevelSequence / UActorSequence
MovieScene 0x348, UMovieScene.EvaluationType 0xC8.
- NPC distance: Detours on APFNpcManager::InitsDistances (once per level) and APFNpcWalkSystem::CreateSpawnData; every
  NPC distance (spawn / despawn, camera fade in / out, the walk despawn, the fade defaults in UPFNpcCameraFadeComponent's
  and FPFNpcCameraSettingsData's constructors, immediates written live: a torn read only gives an odd distance) times
  the NPC distance / 3100 (the game's despawn distance), so their gaps keep the game's proportions.
- Monster distance: AEncountGroup::IsSpawnRangePlayer, SpawnAreaInfo's Range and HalfHeight times it (BattleAreaInfo
  unchanged). Never hide characters: UAchCharacterBuildComponent::SetCulling gets false. Outline distance:
  FSceneView::EndFinalPostprocessSettings, StageEdgeBaseDistance raised to it.
- NPC and monster detail distance: the LOD code's memcpy of FAchCharacterLODData's distances goes through a stub
  (DetourAllocateRegionWithinJumpBounds) to CopyLodDistances, which multiplies them all.
- Sun shadow distance above 2: the r.Shadow.DistanceScale clamp (minss 2.0) is NOPed at load (no effect at 2 or less).
- Skit resolution: in UKismetRenderingLibrary::execCreateRenderTarget2D the UpdateResource call (call [reg + 0x248])
  becomes nop; call stub -> OnRenderTargetCreated: a target smaller than the screen gets the screen's pixel count x
  percent^2 in its own shape (multiples of 4); the screen size comes from the swap chain. The scene capture
  constructor's and [rbx+0x2B8], ~0x2008 (TemporalAA, MotionBlur) becomes or 0x2008 while it is on (the one followed
  by unpcklps; 2 other constructors match without it). DLAA ignores TAA passes whose size is not the screen's.
- Smooth cutscenes: ULevelSequence and UActorSequence vtable slot 0x10 (the same PostLoad here) ->
  OnSequencePostLoad<N>: MovieScene.EvaluationType = WithSubFrames (1); vtables from the constructors' lea.
- Each setting is the game's at its game value. a.URO.Enable 0 (full-rate distant animation) is a plain cvar row.
  Maximum: ATN's values (200000, 1000, 15, on, URO off, 24000). Keys Tales.* (v36; v34-v35's saved values reset).
- Arise-SDK installed (it changes some of the same code): a function already hooked (jmp in its first instruction),
  a redirected call or a changed pattern is left alone; the row then sets Arise-SDK's live sdk.* float where it has one
  (monster distance, detail distance, outline distance, skit resolution) or shows unavailable.
v40: no Recommended presets: every preset keeps the game's volumetric lighting and fog (as Expeditions: Rome; their
finer values moved to the lists, more_extras now 3 wide). Higher distances in every preset: outline 12000 / 24000 /
100000 / 500000, NPC distance 10000 / 50000 / 200000 / 500000, monster distance 10 / 100 / 1000 / 5000, NPC / monster
detail distance 3 / 10 / 50 / 200 (High / Ultra / Extreme / Maximum; Beyond Maximum: 750000 / 750000 / 10000 / 1000).
v41: terrain detail / full detail distance up to 50, terrain detail bias down to -2, grass draw distance up to 20 (list
only; in Beyond Maximum, which takes a LOWER row's first choice).
v42: temporary "Halo test" group (in no preset): ATN sees shadows missing in a halo around his character on what it
hides (stock game too). One row each turns off Bandai's screen-space shadows, contact shadows, capsule shadows (or full
resolution), Bandai's / Unreal's AO, distance field shadows, Bandai's SSGI, character self-shadows, or lets grass cast
real shadows. Remove once the cause is known.
v43: Halo test removed: none of its rows removed the halo (ATN; Bandai's AO 0 breaks the lighting). Game's values:
screen-space shadows 1, contact shadows 1, capsule shadows 1 (half resolution), Bandai AO 1, r.AmbientOcclusionLevels
-1, distance field shadows 1, SSGI 1, character self-shadows 1, grass.DisableDynamicShadows 0 (grass shadows on). New
row: Character soft shadows at full resolution (r.CapsuleShadowsFullResolution 1, Ultra and up).
v44: "Weak CPU" checkbox under the presets (ATN: keeping characters behind the camera costs ~5 fps on 3D V-Cache
cores, ~30 on the others): every preset then leaves Keep characters behind the camera, Never hide characters and
Half-rate distant animation (the CPU-only rows) at the game's values; ticking it sets them now. Saved as
`[Tales AA] WeakCPU`.
v45: Weak CPU covers only Keep characters behind the camera and Never hide characters (ATN: full-rate animation makes
no difference; presets keep it).
v46: "Far sun shadows (mountains)" checkbox next to Weak CPU (in no preset, ATN: a trade-off): every preset then sets
Sun shadow distance 3 (new choice; stops sunlight shining through far mountains, e.g. the first area after the
tutorial) and Sun shadow resolution 8192 (makes up for part of the softer, shimmering near shadows); ticking it sets
them now. Saved as `[Tales AA] FarSunShadows`.
v47: Advanced "Trace this pixel for one frame" (pixel_trace.hpp; ATN: sky banding in SDR too, the sky draw not found by
turning shaders off: Unreal does not clear those pixels, so the last frame's sky stays). A red cross marks the pixel
(% sliders). During the next frame, before every draw / dispatch / clear / copy, the texel at that position (scaled to
each target's size) is copied from the previous command's targets (1x1 staging, up to 20000); saved next to the exe
as `tales_pixel_trace.txt`: each command that changed it (shader hashes, target format and size, before -> after,
the shader's input textures).
v48: pixel trace compares only the texel's bytes (v47 compared 16, the rest of the staging row: false changes).
First trace (ATN, sky): scene color is R11G11B10, the sky is Unreal's sky sphere (ps 0x11039AD5): fixed in the HDR
addon v19 (Scene buffers 16-bit).
v49: less CPU. Constant mirror: until the TAA pass's constant buffers are watched, every map, update, copy, new and destroyed resource returns at once (no GetDesc, no lock).
v50: Advanced "Trace one frame" (frame_trace.hpp, from the Shogun 2 DLSS addon): every draw, dispatch, clear and copy
of the next frame with its shaders, targets (8 + depth), viewport, scissor, inputs and Unreal's view / buffer size (View
uniform buffer c122 / c123, found in the bound constant buffers), into tales_frame_trace.txt next to the exe. For DLSS
upscaling: at a render scale below 100%, which passes after the TAA run at the render size, where the spatial upscale
(0x57192B01) is and in which formats. Its events are registered only for the traced frame.

## DLSS upscaling (v51)

The game has no temporal upscaling: below 100% its TAA, motion blur and tonemapper run at the render size and a
spatial Catmull-Rom upscale (ps 0x57192B01) brings the image to the screen (frame trace, 50%: everything after the TAA
at 1920x1080, then the upscale to 3840x2160, then the UI). DLSS replaces the TAA (as DLAA does) and outputs the screen
size; then:

- The game's render-size TAA output gets DLSS's result downsampled (private cs 0x7A1E0D03: 4 bilinear taps a quarter of
  an output pixel apart, an exact 2x2 box at 50%): bloom, eye adaptation, the motion blur tiles and the next frame's
  reflections read it.
- Each motion blur (12 permutations) and tonemapper (20) draw runs as the game issues it, then once more at the output
  size (command_action post callback, so the HDR addon's shader and constants are still bound): its inputs that are
  the TAA output become DLSS's output, inputs that are earlier passes' targets their output-size copies, and it draws
  into an output-size copy of its own target (same format; viewport and scissor to the output size; every binding put
  back for UE's state cache). Both shaders sample by UV (motion blur's tiles from UV x buffer size; its SV_Position
  only seeds noise; the tonemapper's optional sharpen uses texel offsets, now output pixels).
- The upscale draw, when its input is the tonemapper's target with a copy this frame: the same shader with the copy as
  t0 and the output size as its source size (cb0[0]), so every pixel is sampled at its center: an exact copy (alpha 0
  as the game's). The game's draw is skipped. Any other upscale input (an unknown pass in between) keeps the game's
  upscale: correct, softer.
- Render sizes: whole units of the screen's aspect ratio with both sides multiples of 4 (UE 4.21 pads its buffers to
  4; padding seen switches to 8): 16:9 at 4K in steps of 64x36. Ultra Quality 2944x1656, Quality 2560x1440, Balanced
  2240x1260, Performance 1920x1080, Ultra Performance 1280x720; Custom: the nearest step. The view fills its buffer
  (the output-size draws read buffer UVs) and both axes have exactly the same scale (the custom-scale shimmer seen in
  Shogun 2). r.ScreenPercentage gets the exact size minus a quarter pixel (UE renders ceil(size x percentage / 100)).
  Unusual aspect ratios without a unit of 5% or finer round each side on its own.
- TAA dispatches count only at the mode's input size (the screen size for DLAA): scene captures and the frames before
  a new render scale applies stay the game's.
- Jitter (v51 only: r.TemporalAASamples 8 x (output / input)^2, at most 127; replaced in v52, below).
- Advanced: "Control the game's render scale" (was "Keep ... at 100% for DLAA"; DLSS needs it), the render size, the
  jitter count, and "Post-processing at the output size: N passes, upscale replaced".
- v52: texture LOD bias and NVIDIA's jitter pattern:
  - **Texture LOD bias** (overlay, saved as `[Tales AA] TextureLodBias`, default Sharper): UE 4.21 sets
    View.MaterialTextureMipBias only for its own temporal upscaling, so the game leaves it 0 and textures get the
    render size's blurrier mips. Every material sample uses it (SampleBias with cb1[133].x: 774 samples in the dumped
    pixel shaders, cb0[133].x: 53). The View uniform buffer is a 4096-byte dynamic buffer (Map / Unmap), already copied
    by constant_mirror.hpp for the TAA: at Unmap, an upload whose ViewSizeAndInvSize (c122) is the main view's input
    size gets c133.x = log2(input / output) + 0 / -1 / -2 (Matched / Sharper / Sharpest; no -1 floor as UE's
    r.ViewTextureMipBias.Min had in Expeditions: Rome) before the GPU reads it. Off, DLAA: untouched. Advanced shows
    the bias and how many view uploads per frame got it.
  - **Jitter**: UE's pattern is a Gaussian inside a circle over r.TemporalAASamples positions, at most 127 (signed
    byte). While DLSS upscales, the game's jitter multiply (skipped by the jmp patch anyway) becomes two rip-relative
    loads of the addon's sample (memory from Detours' region near the code), and the jmp's displacement 0x12 becomes 0:
    NVIDIA's Halton (2, 3) over the whole pixel, 8 x (output / input)^2 positions, any length (128 at 25%, 800 at 10%).
    The multiply bytes change only while the jmp skips them; the jmp changes one byte (atomic). UE computes
    TemporalJitterPixels and the projection jitter from these registers, so DLSS still reads the jitter from the View.
    DLAA keeps the game's 8-position pattern. r.TemporalAASamples stays the game's.
- v53: Halton for DLAA too, Bandai's screen filters at the output size, game sharpening off:
  - **Jitter**: DLAA also uses NVIDIA's Halton (2, 3) pattern (8 positions; UE's Gaussian is denser in the middle).
  - **Screen filters** (debug2 / debug3 traces): Bandai's passes between the deferred lighting and the TAA are sized
    in render pixels, so below 100% they grew on screen: the painterly blurs (ps 0x2483CE26 / 0x773EABCB, +-4 taps 1.5
    pixels apart), the painterly filter 0x66DC643D, the character outlines (0x77D287B7 / 0x1A51F09D, cb0[62].x pixels:
    the thick black lines on characters), the grading pass's sharpening (0x381F5A30 and 3 permutations, 0.5 pixels)
    and, in lighting00, the shadow edge tint: the shadow mask 2.3 pixels up-right decides the tint, so a band about
    3 render pixels wide around every shadow edge, characters included, got a brighter tint (the halo: about 7 output
    pixels at 33%, 28 at 8%). post_filters.hpp (cross_addon) shares render / output while upscaling (1 otherwise) with
    the RenoDX HDR addon, whose replacements of those passes multiply their pixel offsets by it. Point-sampled edge
    tests (outlines, lighting00's normal edges) keep at least one render pixel and fade by the ratio instead; the
    shadow edge tint fades by its band width (3.3 s / (1 + 2.3 s)). 100% and DLAA: unchanged.
  - **Game's sharpening** (overlay checkbox, `[Tales AA] GameSharpening`, default off): Bandai sharpens before the TAA
    (0x381F5A30's 5 taps with strength cb0[34].x, 0x66DC643D's 4 diagonal taps); under DLAA / DLSS it is dropped
    (shared 0) unless kept. The HDR addon's Sharpening slider is the tonemapper's, not this one.
  - Lighting permutations 0xB2726B44 / 0x6513DC8E / 0x81E3CED2 / 0xC28D2F1E / 0xF95FABAA (same tint, not in any of
    ATN's traces) stay the game's.
- v54: **Effects at output resolution** (overlay checkbox, `[Tales AA] EffectsAtOutputResolution`, default off; ATN
  compares visuals and fps): while DLSS renders below 100% (s = render / output), settings sized in render pixels are
  written so their effects keep their 100% detail, on top of the preset (or the game's value): volumetric lighting and
  fog cell sizes and Bandai's divisors (reflections, reflection color, translucent reflection depth, ground fog, clouds,
  cloud shadows, cloud ray march, light shafts) x s (nearest, at least 1); r.Shadow.TexelsPerPixel / Spotlight /
  Pointlight / s (Unreal sizes object and local light shadows by their size in render pixels; capped by
  r.Shadow.MaxResolution); capsule shadows, translucency depth and Bandai's GI at full resolution (the Maximum preset's
  values). Sun shadows (fixed cascade resolution) and LODs (screen fraction) do not depend on the render scale.
- v55: Effects at output resolution on by default (ATN: much better, fog no longer blocky, a few fps). From the exe's
  UTF-16 strings (only .text is SteamStub-encrypted), new rows in no preset (their lists only) and in the option:
  r.EnableMixedResolutionTranslucentRenderingTO14 (Bandai's half-resolution effects) 0, r.TransulcentUseQuarterSizeDepthTO14
  0, r.SSS.HalfRes 0 (full resolution while upscaling), r.ScreenSpaceReflectionColorDownSampledScreenScaleTO14 x s.
  Half-resolution switches now go to 1 where higher is better, else 0.
- v56: r.EnableMixedResolutionTranslucentRenderingTO14 removed (row and option): 0 crashed the game at startup at 4K
  (UE crash report: CreateTexture3D 3840x2160x40 A8 E_INVALIDARG, above D3D11's 2048 per side). Half-resolution
  effects stay the game's.
- v57: test row "Effect resolution (%, test)" = r.SeparateTranslucencyScreenPercentage (Unreal: percent of the render
  size, above 100 supersampled; a 2D buffer), choices 50 / 100 / 150 / 200, in no preset: if Bandai's half-resolution
  effects follow it, Effects at output resolution can keep them at half the output size. Volume cell guard: the
  volumetric lighting and fog cell sizes keep render width / cell <= 2048 (D3D11's 3D texture limit).
- v58: the v55-v57 startup crash (CreateTexture3D 3840x2160x40 A8) was cloud shadows / cloud ray march: ATN's 2 (a
  1920-wide volume at 4K) scaled to 1 by Effects at output resolution at the 1280x720 startup window, then the game
  recreated the volume from the full 4K output. v56's guess (mixed-resolution translucency) was wrong. Every volume
  setting (volumetric lighting and fog cells, cloud shadow and ray march divisors) now keeps
  max(output width, widest monitor mode) / value <= 2048.
- v59: the r.SeparateTranslucencyScreenPercentage test row removed (ATN: no visual or fps change: Bandai's half-resolution
  effects do not follow it). r.EnableMixedResolutionTranslucentRenderingTO14 back (row, and 0 in Effects at output
  resolution): v58 starting fine confirmed the cloud volumes as the crash. While upscaling, effects then render at the
  full render size: at 50% the same 1920x1080 as the game's half resolution at 4K 100%, at 33% 1280x720 (was 640x360).
- v60: "Trace these pixels" traces a horizontal line (1/16 of each target's width, centred on the red cross; drawn in
  red) instead of one pixel: every pass's texels across a silhouette in the same frame (the game cannot be paused, the
  character idles). tales_pixel_trace.txt: per change the centre texel before -> after, then the line's texels.
- v61: the pixel trace also saves a square patch (1/8 of each target's width) around the cross from every full-screen
  pass and dispatch, raw, to tales_trace_patches next to the exe (patches.txt lists them). ATN's v60 line through the
  bush behind the head was too noisy to show the halo; patches let it be measured by distance from the character.
- v62: "Ambient occlusion" (Game's / Fixed default / Deep; config AmbientOcclusion) next to Game's sharpening, shared
  with the HDR addon (post_filters.hpp SharedData + ambient_occlusion: both addons must be this version or newer).
  The pixel trace's Present check is a lock-free flag while no trace is requested.
- v63 (temporary): Ambient occlusion Game's / Fixed / Test 1/2 / Test 1/4 / Test 1/16 / Test no AO (Deep removed).
- v64: "Ambient occlusion halo" moved to Graphics > Shadows (first row; not a cvar): Game's / Reduced (default) /
  Reduced + darker. Tests removed.
- v65: graphics presets set Ambient occlusion halo too (Game's own: Game's; every other preset: Reduced). "Replace
  the game's TAA pass" in Advanced only; "Game's sharpening (Not recommended)", "Effects at output resolution
  (Recommended)". The volumetric fog note is yellow and says no preset changes Loading.
- v66 (test): every frame trace shows the game's sun as ONE shadow map (one 8184 x 8184 viewport in the 8192 atlas,
  drawn once, projected by cs 0x24D721C3), whatever Sun shadow cascades says: the cascade count most likely has no
  effect in Tales. Sun shadow resolution offers 16384 (list only, "test, can crash", 512 MB D16): the doubled atlas
  (SUN_SHADOW_ATLAS) is applied before a value above 8192 is written (if it cannot be, 8192 is written) and kept 8
  presents after (a frame in flight may still use the larger map). Without the doubled atlas the game drew no sun
  shadow above 8192 and crashed. Unverified: Expeditions v19's same patch did not take (its shadows vanished above
  4096), so 16384 may not work here either; a frame trace at 16384 shows the atlas size.
- v67 (test): 16384 crashed (ATN; UE crash at exe+0x1A9E56C, an access violation on a null shadow depth target: the
  same place as above 8192 without the doubled atlas). The list-only test choice is now 12288: if it works, the doubled
  atlas takes and only a 16384 texture cannot be made; if it crashes, the atlas patch does not take in Tales either
  (as Expeditions v19) and 8192 is the most.
- v68: 12288 crashed too (ATN; same exe+0x1A9E56C, with the doubled atlas applied per the log): Unreal's atlas size
  (1 << (GMaxTextureMipCount - 1)) is not what limits the sun's map, so 8192 is the most in Tales. The test choice and
  the atlas patch (SUN_SHADOW_ATLAS, exe+0x1AD7041, never needed: the sun is one map that fits 8192) are removed.
- v69: presets set Ambient occlusion halo Reduced + darker (Game's own: Game's). "Weak CPU" is now "Game's character
  culling" (same key WeakCPU). Above the three preset toggles a yellow note: they change what presets set, unticking
  does not undo. New toggle "Max volumetric fog" (MaxVolumetricFog): presets set Bandai's lighting cells 2 and Unreal's
  fog cells 4 (its Cinematic). Bandai's lighting cell size offers 1 (test). Volume guard split: Bandai's lighting and
  Unreal's fog follow the render size (debug12: 491 x 276 = 2944 / 6 at DLSS Ultra Quality): render width / cell <=
  2048; the cloud volumes keep output width / divisor <= 2048. Sun shadow distance / the game's is shared with the HDR
  addon (post_filters sun_shadow_distance) for its penumbra edge color.
- v70: v69 crashed at startup (UE LowLevelFatalError: CreateTexture3D failed): Effects at output resolution scales the
  lighting cell 2 to 1 below ~75% and v69's render-size bound allowed it while the game still rendered at the full size
  (the scale not applied yet). Back to v68's bound for every volume setting: max(output width, widest monitor) / 2048
  (cells >= 2 at 4K). Lighting cell 1 removed. "Weak CPU" name and tooltip restored.
- v71: "Weak CPU" is "Vanilla crowd limits (CPU saver, not recommended)" (key WeakCPU); the three preset toggles one per
  line with an empty line above the yellow note and after the last one. Shorter tooltips. (ATN debug13, sun shadow
  distance 4: the shadow edge now runs smoothly from the lit color to the shadow color, no pale band.)
- v72: debug button "Save the game's code for analysis": the exe's image as loaded (code decrypted; on disk SteamStub
  encrypts it) to `tales_image.bin` next to the exe, file offset = RVA (`code_patch::SaveImage`). For the smooth sun:
  Bandai's WorldTime plugin (`AWorldTimeActor`: transient float `ActorRefresher` +0x32C, int `myCounter` +0x330, BP event
  `UpdateTime`) moves the sun in steps.
- v73: **Smooth sun movement** (Shadows, `Tales.SmoothSun`, High to Maximum 1). From the saved image: Bandai's
  TWorldTimeManager (singleton at exe+0x4B07C90 / +0x4B07C98) advances its time every world tick (delegate
  exe+0x5FE290: time += dt x rate, rate 50 = a day in 28.8 min; hour +0x5C = time / 3600, exe+0x5FE6C0).
  AWorldTimeActor::Tick (exe+0x5FD930) and the game's subclass's (exe+0xDC33C0) call the Blueprint event UpdateTime
  (which turns the sun) whenever that hour differs from the one sent (+0x328); so the steps come from how often the
  actor ticks. The hooks set its PrimaryActorTick.TickInterval (+0x68, as AActor::SetActorTickInterval writes it,
  exe+0x25DB510) to 0 while on and put the game's back when off. Overlay (debug): the game's tick interval, ticks and
  sun updates (to check the cause if the sun still steps).
- v74: v73's tick interval changed nothing (ATN: the actor already ticks every frame, interval 0, every tick sends the
  hour: 25880 / 25880): Bandai's sky Blueprint gates the sun itself (about every 5 s). Smooth sun movement now works
  at the light: FLightSceneProxy::SetTransform (exe+0x23CF710; LightToWorld +0x70, inverse +0x30, Position +0xB0,
  LightType +0x140; called by the render command exe+0x1A47E50 and exe+0x1A3D672) is hooked; for directional lights
  the new transform becomes a target and the shown one passes through; every present (render thread) moves it toward
  the target over the game's step length (axes interpolated and orthonormalized, position = -X x WORLD_MAX), one step
  behind; turns above ~10 degrees snap. Found via UDirectionalLightComponent::GetLightPosition (exe+0x23C8F10, the
  component's ComponentToWorld rotation +0x190) -> its vtable -> CreateSceneProxy (exe+0x23C8EC0) -> the proxy
  constructor (exe+0x23C4440). The directional proxy vtable's destructor slot is patched to drop freed proxies.
  The tick hooks and the overlay counts are gone. The sky's sun disk (Blueprint material parameters) may still step.
- v75: v74 crashed: SetTransform loads both arguments with movaps; the addon's matrix and position (std::array on the
  stack) were not 16-byte aligned (minidump: access violation at -1 in the hook's trampoline). Matrix / Vector4 are
  now alignas(16). ATN: the game itself moves the sun in bursts (every ~5 s it turns quickly to the next position,
  then holds), so v74's per-update targets would only have followed it. Now a burst (the first update after 0.5 s
  without one) restarts the shown motion from where it is toward the (moving) target, spread over the time since
  the previous burst (1-15 s): an even turn, one burst behind.
- v76: ATN: the shadows now move smoothly, the sun disk in the sky still steps. Pixel trace (debug17): the disk is the
  sky sphere's base pass draw (vs 0x57DAED04, ps 0x11039AD5): pow(dot(-view, normalize(cb2[4].xyz)), 3500 / 500), cb2
  = its material constants, so cb2[4] is a vector parameter the Blueprint sets with the light. Path: Blueprint
  SetVectorParameterValue (MID exec exe+0x2755F40) -> UMaterialInstance::SetVectorParameterValueInternal (exe+0x2470DF0,
  VectorParameterValues +0xB0, 0x30 each) -> GameThread_UpdateMIParameter (exe+0x24729F0, Resources +0x108 / +0x110 /
  +0x118) -> FMaterialInstanceResource::RenderThread_UpdateParameter<FLinearColor> (exe+0x2474270: invalidates the
  uniform expressions (exe+0x24883B0), vector parameters +0x138, 0x20 each), then CacheUniformExpressions_GameThread
  (exe+0x2488170) -> FMaterialRenderProxy::CacheUniformExpressions (exe+0x2488060: InitResource, deferred requests).
  The update is hooked: a unit-length vector within 3 degrees of a smoothed light's direction follows the light
  (turned from its target to its shown transform), sent again with each light update and present, then recached as
  the game does. FMaterialInstanceResource (constructor exe+0x24690B0, vtable RVA 0x3F456A0, size 0x168) gets its
  destructor slot patched like the light proxies. Overlay (debug): the game's step time (averaged), its last turn in
  degrees and how many sky parameters follow the sun.
- v77: ATN (v76): steps every 7.200 s (0.730 degrees), 0 sky parameters followed: the disk did not move. The sky
  shader clamps its constants itself (min / max on cb2), so cb2 is a material parameter collection, not a material
  instance's folded uniform expressions; the v76 material instance hook is removed. Path: Blueprint
  UKismetMaterialLibrary::SetVectorParameterValue (exec exe+0x26ADDA0, exe+0x20E96B0) ->
  UMaterialParameterCollectionInstance::SetVectorParameterValue (exe+0x2497ED0: Collection +0x30, its vector
  parameters +0x48 (0x28 each), VectorParameterValues +0x90) -> UpdateRenderState (exe+0x2498220: World +0x38,
  +0xE8 needs update) -> DeferredUpdateRenderState (exe+0x24982D0) -> GameThread_UpdateContents (exe+0x2498670,
  Resource +0xE0; the render command exe+0x2498800 makes a new uniform buffer each time). The setter is hooked (game
  thread): a unit-length vector within 3 degrees of a smoothed light's direction is smoothed like the light and set
  again after every world time tick (AWorldTimeActor::Tick exe+0x5FD930 and the subclass's exe+0xDC33C0, v73's
  patterns). UMaterialParameterCollectionInstance's vtable (constructor exe+0x27598C0, RVA 0x3F485C0; slot 0 =
  its destructor exe+0x2497C40) gets its destructor slot patched. ATN: the game turns the sun for ~2.2 s, then holds
  ~5 s; aiming at the moving target sped the shown motion up for 2.2 s, so a motion now aims at where the previous
  burst ended (constant speed; changes without gaps start the next motion once the shown one is done). Smooth sun
  movement left the graphics settings: a checkbox under the three preset toggles (config SmoothSun, default on, in
  no preset), with **Time of day** below it (config TimeOfDay: game's speed, 2x / 3x / 6x slower): TWorldTimeManager
  (globals exe+0x4B07C90, else exe+0x4B07C98) adds DeltaSeconds x rate (+0x68) to its time every world tick
  (exe+0x5FE2C1); the world time ticks divide the rate (a rate the game sets meanwhile becomes the game's). Smoothing
  lasts up to 60 s (43.2 s steps at 6x slower). Overlay (debug): collection vector changes and the closest one to the
  sun (degrees, length), for when nothing follows.
- v78: ATN: the disk follows the shadows now. "Time of day" renamed **Day and night speed** (config DaySpeed): 100x /
  10x / 6x / 3x / 2x slower, game's speed (default), 2x / 3x / 6x / 10x / 100x faster (engine option Tales.DaySpeed =
  the rate's multiplier). A rate change (the setting or the game's) stretches every running motion and the time since
  the last burst by old rate / new rate at once (Burst::Rescale), so the sun's speed follows immediately (ATN: 6x ->
  default took long: the running 43 s motion went on). Motions last 1-15 s times 1 / speed (12 min steps at 100x
  slower; 0.15 s at 100x faster). Shadow flicker while the sun moves (ATN): Unreal's CSM snapping
  (FProjectedShadowInfo::SetupWholeSceneProjection, exe+0x1ACC450, two copies: Fmod of WorldToFace(-PreShadowTranslation)
  .xy by 8 / resolution, then back through WorldToFace's inverse exe+0x6CFF20) anchors each cascade's grid at the world
  origin; a turning light swings it around that origin, so the scene slides by texels every frame. Fix (with Smooth sun
  movement on): FDirectionalLightSceneProxy::GetViewDependentWholeSceneProjectedShadowInitializer (vtable slot 20,
  exe+0x23C4CD0; out: PreShadowTranslation +0, WorldToLight +0x10, Scales +0x50, CascadeSettings +0xB0; the LPV/RSM
  one is slot 21) is patched: per cascade index (4 anchors each, the closest within 4 radii: skit views), an anchor
  follows the center in whole steps of radius / 64 across the light (a multiple of the snap step for resolutions from
  512: the grid stays), and WorldToLight's translation gets -fmod(anchor's light y / z, step) / 2 (the snap and the
  shadow matrices both add it: shadow coord = L P - K + 2t). Camera moves keep the grid; a turn swings it around the
  cascade's center. The shadow window moves under 64 texels.
- v79: ATN (v78): no crash; the shimmer with Smooth sun movement on much lower, not gone. Tales' sun is one shadow map,
  so the one cascade's center sits hundreds of metres ahead: the grid swung around a far point. Anchors now follow the
  camera (FSceneView +0x60C, the view origin GetShadowSplitBounds exe+0x23C8AF0 passes to the split bounds
  exe+0x23C8320, whose ShadowViewMatrices are at +0x670). Day and night speed: Off (rate 0: the time of day stands
  still), 1000x slower and 1000x faster added (14 steps); saved as the multiplier (config DaySpeedMultiplier; v78's
  index key DaySpeed is no longer read). Test row "Sun shadow layers (test)" (in no preset): r.Shadow.
  UnbuiltNumWholeSceneDynamicShadowCascades 2 / 3 / 4. GetNumShadowMappedCascades (exe+0x23C61F0) takes the light's
  DynamicShadowCascades (proxy +0x194) with built lighting, that cvar without (Unreal's default 1); if the sun stays
  one map, Tales' lighting counts as built (then the proxy field). Bandai's projection (cs 0x24D721C3 / 0x2084C889)
  may not handle more than one; needs Sun shadow resolution 4096 to fit the 8192 atlas.
- v80: ATN (v79): the camera pivot looks perfect (with Smooth Sun Shadows Off / Smooth: really smooth). Layers 4 at
  4096 changed nothing (debug19: still one 4088 x 4088 viewport in a 4096 map): the unbuilt cvar is not the one. The
  test row now raises the light's DynamicShadowCascades (proxy +0x194, copied from the component's +0x3A4 in the
  proxy constructor exe+0x23C4440; +0x198 CascadeDistributionExponent, +0x19C CascadeTransitionFraction, +0x1A0
  ShadowDistanceFadeoutFraction) in the cascade initializer hook for the next frame (engine option
  Tales.SunShadowLayers; the light's own back at Game's). Needs Smooth sun movement's hooks.
- v81: particle mask (ATN: preset L leaves trails behind small particles, M does not). DLSS's input is fine here
  (debug19: the TAA's t0 is the scene depth 269 draws write, unlike Ryse's). Particles have no motion vectors. The
  stage grading composite (grading00-03: 0x381F5A30 / 0x4105D9D8 / 0x376E3F34 / 0xA1B7FCDA) writes the scene color
  the TAA reads; after it come only translucent passes (the half-resolution effects composite ps 0xC7A2979E, then the
  particles at full resolution). Its post callback copies its render target (CaptureGradedScene, a typed copy of the
  view's format); the input pass 0x7A1E0D01 compares the TAA's scene color with it (both on a Reinhard curve,
  max-channel change - 0.02, x 2.5) into an R8 mask, NVIDIA's pInBiasCurrentColorMask (1: trust the current frame).
  Only this frame's copy is used. Advanced checkbox "Particle mask" (config ParticleMask, default on). Settings mode
  "Advanced (For Debugging)". CPU: the collection vector stats (every sky value change) and their overlay text are
  removed; per frame the sun code takes one uncontended lock per present, world time tick, cascade and sun value.
- v82: ATN (debug20, DLSS at 1280 x 720, preset L): the mask changed nothing; fast sparks still streak. The particles
  are in the TAA's input (separate translucency at the render size, composited by ps 0x00438BF1 right before the TAA),
  but v81 marked them only where they are; the streaks are history where they were. The mask now also takes last
  frame's mask at each pixel's previous position (pixel + its motion, as DLSS's history moves) x 3/4 (two R8 masks,
  alternating; the older one cleared when a frame has no mask). Particle mask is now Off / On / Test: whole screen
  (mask 1 everywhere: if the image gets rough, the preset follows the mask at all).
- v83: ATN: the mask changed nothing, not even Test: whole screen: presets L / M ignore NVIDIA's bias current color
  mask. The mask is removed again (0x7A1E0D01 and ngx_backend.hpp as in v80). Preset M is the default (L's label and
  the overlay text say small fast particles can smear). Next idea, not done: particles after DLSS (skip Bandai's
  separate translucency composite ps 0x00438BF1 before the TAA, blend that buffer into DLSS's output, shifted by the
  jitter); debug19's DLAA trace also drew particles straight into the scene color (another path to check first).
- v84: ATN: no particles-after-DLSS. A yellow note under DLSS preset: the game's small particles have no motion
  vectors, so some presets smear them (most of all L). Same line in the Nexus text's known issues.
- v85: Smooth sun movement "(Recommended)", off by default (ATN). Shaders on load (r.CreateShadersOnLoad, still in
  no preset) is a checkbox above it (ticked 1, unticked 0; the same graphics entry and saved choice); the Loading list
  is gone from the settings. Nexus text updated.
- v86: emoose (Arise-SDK) asked that, with Arise-SDK installed, the addon's menu change its settings instead of greying
  them out. Its live cvars (src/dllmain.cpp, names only): sdk.MonsterDistanceMultiplier, sdk.CharaLODMultiplier,
  sdk.MinStageEdgeBaseDistance, sdk.CutsceneScreenPercentage (floats), sdk.CharaDisableCull and
  sdk.CutsceneAllowSubframes (bools; now the arise_sdk_cvar of Never hide characters and Smooth cutscenes). They are
  FConsoleVariableRefs whose global is in Arise-SDK's DLL: engine_cvars.hpp accepted only globals in the exe, so every
  sdk.* row greyed out (ATN). It now takes a global in any loaded module (GetModuleHandleEx from the address) and bool
  Refs (GetInt: movzx eax, byte ptr [rcx + offset]; ret; one byte, 0 / 1). Arise-SDK's MinNPCDistance and
  DisableUpdateRateOptimization have no cvar (NPC distance stays grey with its hooks in place; full-rate animation is
  Unreal's a.URO.Enable, the same in both). Yellow note under the presets: best used together with Arise-SDK. Nexus
  text updated.
- v87: the rows from Arise-SDK's ideas are one group, "Arise-SDK's settings" (ATN: they keep working without it): NPC
  distance, monster distance, NPC / monster detail distance, never hide characters, half-rate distant animation,
  outline distance, skit resolution, smooth cutscenes (from Characters, Distance and detail, Cutscenes; the Cutscenes
  group is gone). A line under its header says so. Arise-SDK's own ini (v30) is safe for both addons at its defaults
  (UseUE4TAA false, TAA jitter scale / sharpness -1, cutscene CA kept); its MinimumNPCDistance has no cvar (-1 lets
  this addon's NPC distance work beside it).
- v88: the Arise-SDK's settings group is first (its rows lead GRAPHICS_SETTINGS), right under the yellow note about
  Arise-SDK, with an empty line before that note. ATN's Arise-SDK.ini (game folder) shortened: same keys and values,
  one short comment each (TAA three: keep for RenoDX; DisableCutsceneCA: keep false, the HDR addon's slider does it).
- v89: the Arise-SDK's settings group needs Arise-SDK (ATN, out of respect for its author): without it every row there
  is greyed out ("needs Arise-SDK"; presets skip it). Installed (its sdk.MonsterDistanceMultiplier exists): rows with an
  sdk.* setting set it; NPC distance uses the addon's code (the shipped Arise-SDK.ini has MinimumNPCDistance -1, so
  Arise-SDK's NPC hook is off); half-rate distant animation is Unreal's a.URO.Enable. Hidden key (never written, not in
  the menu): ReShade.ini [Tales AA] DebugDevArise=1 = the addon's own code for the whole group (v88's behavior), for
  ATN's testing without Arise-SDK. Shipped Arise-SDK.ini: the game's values, NPC -1, SkipIntroLogos false.
- v90: ATN's Ko-fi button after the graphics settings (the end of the Simple view).
- v91: Never hide characters' tooltip: it does not stop characters behind the camera fading in. Two systems: Arise-SDK's
  CharaDisableCull (and the addon's own hook without it) passes false to UAchCharacterBuildComponent::SetCulling when a
  character is built (the character's frustum / occlusion culling flags; needs an area load); Bandai's PFCullingManager
  (exe+0xA0C230) tests every registered character against the view on its own timer and hides it outside, unless
  Arise.PF.Culling.ForceInFrustum (Keep characters behind the camera, live). Neither mod blocks the other (ATN 2026-10-20).
- v92: Arise-SDK's link is its Nexus page (www.nexusmods.com/talesofarise/mods/5, newer than GitHub) in the overlay, README and Arise-SDK.ini;
  the overlay note says the "Arise-SDK's settings" group needs it (greyed out without it).
- v93: Sun shadow distance above 2 (Arise-SDK's idea) needs Arise-SDK or DebugDevArise=1: without either the
  clamp patch is undone at the console search, choices above 2 are removed (a saved 3 shows and writes as 2, the ini
  keeps it), presets cap at 2 and "Far sun shadows" is greyed out (its overrides skipped).
- v94: "NexusMods (For RenoDX + DLSS)" button (nexusmods.com/talesofarise/mods/444) next to ATN's Ko-fi; both tinted as the
  HDR addon's (RenoDX's tint: the theme's button colours at the tint's hue).
- v95 (ATN's batch, untested): Spark trails fix (checkbox under the DLSS preset, config SparkTrailsFix, default on):
  UE's separate translucency (t0 of its composite ps 0x00438BF1 right before the TAA) is kept for the input
  preparation (0x7A1E0D01), which marks pixels where a particle adds more light than its background has (sparks,
  embers, flames; not smoke or haze) and where one was last frame (two R8 masks, last frame's moved with the motion,
  x 3/4 a frame); there the motion vector points 4 screens away, so DLSS finds no history and shows the current frame
  (NVIDIA's bias current color mask did nothing for presets L / M, v81-v83). Ambient occlusion GT-VBAO (Shadows group:
  Ambient occlusion Game's / GT-VBAO, AO radius / strength / thickness / quality / directions; the halo option only
  for the game's): stored for the HDR addon (post_filters.hpp SharedData appended; update both addons together).
  Section headings as Ryse's DLSS overlay: DLSS lavender (its settings' text too), Graphics pink, About green.
- v96 (DXR start, untested in game): **Ray-traced sun shadows (Experimental)** (Graphics > Shadows, config
  RayTracedSunShadows, default off; needs a DXR 1.1 GPU and the HDR addon v66+). rt_shadows.hpp: a D3D12 device beside
  the game's D3D11 one (d3d12.dll loaded only when turned on; ReShade's wrapper unwrapped; D3D11 textures with NT
  handles and a D3D12 fence shared both ways, as Phantom FX by Bustanity, MIT). The scene comes from the depth
  prepass: after each opaque draw of vs C81C5E1F (static meshes), 287D5CA8 / 344F8ACF (instanced), 5F4BCA38
  (landscape), F1D731D5 (spline meshes) without a pixel shader (masked leaves left out) on the scene depth, a draw not
  seen before (key: buffers, ranges, textures, uniform buffers but View) is drawn once more into a stream-output buffer
  (0x7A1E0D10's SV_Position signature, no rasterization). Before the sun shadow mask (cs 0x2084C889): its clip
  positions to world positions with the draw's View (0x7A1E0D11: ClipToTranslatedWorld c36, PreViewTranslation c62),
  pixel world positions and the sun direction (0x7A1E0D12: the direction that keeps a point's place on the game's
  shadow map, from ScreenToShadow c28-c31 and ScreenToWorld c44-c47), D3D11 signals; D3D12 copies the vertices,
  builds one BLAS per new draw (kept, released after 3600 frames unseen), a TLAS of this frame's draws plus the last
  300 frames' (not the landscape: its older LODs would shadow the current one), traces one ray per pixel toward the sun
  (0x7A1E0D13, cs_6_5 inline ray query, offset 1 cm + 0.1% of the depth) into an R8 visibility; D3D11 waits and binds
  it at t40 for the HDR addon's mask, which keeps the darker of shadow map and ray. Skinned characters (vs 2C264050)
  and leaves stay shadow-map only; casters the camera never saw (behind it or occluded for 5 s) are missing. Advanced:
  "Trace ray-traced shadows for one frame" -> tales_rt_trace.txt (each prepass draw: in the scene / captured / budget;
  BLAS counts and memory; the sun direction read back). GPU test (tools\toa\rt_tests\rt_shadows_test.cpp: the game's
  own 0xC81C5E1F, a ground and a raised square, two View buffers): shadow 0 under the square, ground and its top 1,
  sun direction exact.
- v97 (untested in game): **Spark motion** (part of the Spark trails fix): the game's sprite particles get their own
  motion vectors, so DLSS keeps sparks sharp instead of only dropping their history. After each draw of the 8 sprite
  vertex shaders (3147D98A, 691BCF53, 75139A1B, 75BBCA50, AE463873, D7B928CD, E1234AB3, F885F191: Primitive at b2,
  View at b1, Position / OldPosition inputs) the same draw runs with its motion version (0x7A1E0D20-27, made from the
  3Dmigoto decompiles by tools\toa\spark_motion\make_motion_vs.py; 3Dmigoto's float test of the cutout vertex count
  fixed to an integer test; WARP compare_test.cpp: SV_Position equal to the game's with and without cutouts) and
  0x7A1E0D28 into an RGBA16F target of the translucency's size, with the game's depth test: last frame's corner =
  this frame's minus TranslatedWorldToClip x LocalToWorld x (Position - OldPosition), then ClipToPrevClip (c114,
  unjittered with c118) -> render pixels, alpha 1. The input preparation (0x7A1E0D01, t5) uses it where a particle
  is this frame (current mask > 0.25) and has its own motion; where one only was (last frame's mask) and for other
  particle kinds (ribbons, meshes, GPU sprites) the motion still points off the screen (no history).
- v98 (untested in game): **Ray Reconstruction (Experimental)** (DLSS section, config RayReconstruction /
  RayReconstructionPreset: NVIDIA's default, D, E): NVIDIA's DLSS-D (nvngx_dlssd.dll next to nvngx_dlss.dll; without
  it, or "SuperSamplingDenoising.Available" 0, Super Resolution as before) in place of DLSS / DLAA at every mode, with
  its guides: UE's G-buffer A / B / C copied at the first draw of Bandai's lighting pass each frame (its 6 permutations
  F2A6E883 ... read them at t1-t3), and 0x7A1E0D31 at the TAA point: diffuse albedo (base colour x (1 - metallic)),
  specular albedo (lerp(0.08 x specular, base colour, metallic) through Karis' environment BRDF at N.V), world
  normals with View.TranslatedWorldToView (c8-c11) and ViewToClip (c24-c27) as its matrices, roughness; hardware
  depth (reversed), the same colour, motion, jitter and flags. ngx_backend.hpp: Evaluation.ray_reconstruction
  (guides; null = Super Resolution), NGX_D3D11_CREATE_DLSSD_EXT / EVALUATE_DLSSD_EXT, Ray Reconstruction presets per
  mode, switching either way recreates the feature. GPU test (tools\toa\rr_test, the RTX 5090, SDK 310.7 DLLs): 960x540
  -> 1920x1080 through DLSS-D ("DLSS Ray Reconstruction active", output finite, mean 0.546 for 0.55 in), then back to
  Super Resolution.
- v99 (untested in game): **Ray-traced ambient occlusion (Experimental)** (Graphics > Shadows, config
  RayTracedAmbientOcclusion / RayTracedAORange 0.5-10 m (2) / RayTracedAORays 1-8 (2)): the same D3D12 pass as the
  sun shadows (0x7A1E0D13, now root constants b0: frame, flags, range, rays) also sends cosine-weighted rays around each
  pixel's normal against the captured scene (the share that escapes, noise moving every frame for DLSS / Ray
  Reconstruction) into a second shared R8 texture; it is bound at t41 around the first draws of Bandai's lighting pass
  (its 6 permutations), which the HDR addon (v67) multiplies into its own ambient occlusion. Either effect on captures
  and traces the scene (rt_shadows::Active); the sun shadow mask still gets t40 (1 everywhere with shadows off). Like
  the shadows it needs the sun shadow mask's pass (outdoors). GPU test (rt_shadows_test.cpp, 16 rays, 5 m): ground
  under the raised 2 m square 0.89, open ground 1.00.
- v100: **DLSS Frame Generation + Reflex (Experimental, restart)** (DLSS section, config FrameGeneration /
  FrameGenerationMultiplier 2-6, 0 Dynamic, 1 Off (Reflex only) / ReflexBoost), frame_generation.hpp, ported from
  Phantom FX by Bustanity (MIT, PHANTOM_FX_LICENSE.txt). On at launch, the addon detours DXGI's own
  IDXGIFactory::CreateSwapChain (slot 10, read from dxgi.dll's file) behind ReShade; the game's D3D11 swap chain becomes
  a stand-in whose back buffer is a D3D11 texture shared with D3D12, and each present copies it into a D3D12 swap chain
  made through Streamline's factory proxy (manual hooking, NGX project = the DLSS one, ngx_backend.hpp PROJECT_ID), on a
  queue of its own (shared fence both ways). ReShade and the HDR addon wrap the stand-in as the game's swap chain. Per
  frame: the DLSS evaluation's depth and motion (copied into shared textures at present), the camera (View
  ViewToClipNoAA 28, ClipToPrevClip 114, forward / up / right 52-54, WorldCameraOrigin 59; inverses in doubles), the
  jitter, and a HUD-less image: after the tonemapper's output-size draw (DLSS: its output-size copy; DLAA: its target)
  the HDR addon's own swap chain shader (0x7A1E0D40/41, its constants still bound at b13) encodes it as the back buffer
  will be (no HDR addon: a copy, 0x7A1E0D42). Reflex Low Latency (or + Boost) with PC Latency markers; without a game
  loop hook the next frame's sleep and markers come after the present (render thread). Generation is off on frames
  without DLSS / DLAA and around resizes (NVIDIA's guide). The real swap chain allows tearing where the display does
  (else every present failed). NVIDIA's frame generation takes no FP16 / scRGB: HDR needs the HDR addon's Output on
  HDR10 (Auto picks it when FrameGeneration is on, HDR addon v68); with scRGB the status says so. Needs an RTX 40+ GPU,
  hardware-accelerated GPU scheduling, and next to the addon: sl.interposer.dll, sl.common.dll, sl.dlss_g.dll,
  sl.reflex.dll, sl.pcl.dll, nvngx_dlssg.dll (Streamline SDK 2.14.16 bin\x64; STREAMLINE_LICENSE.txt). It pauses
  while the game window is not focused (NVIDIA's). Test (tools\toa\fg_test\fg_test.cpp: a D3D11 window through the
  hook, moving bar with depth / motion / camera, SL 2.14.16 on an RTX 5090): 2x, 3x and 4x presented per rendered frame
  in SDR and HDR10, through ResizeBuffers(0, 0) and back; the stand-in releases cleanly. Not run in the game yet.
  Ray Reconstruction preset F (DLSS 310.9's new transformer model, nvngx_dlssd.dll 310.9.16 from the same SDK).
- v101 (untested in game): **Ray-traced bounce light (Experimental)** (Graphics > Shadows, config RayTracedBounceLight /
  RayTracedBounceStrength 0-4 (1) / RayTracedBounceRange 1-50 m (10) / RayTracedBounceRays 1-8 (1)): one bounce of
  diffuse light. The trace pass (0x7A1E0D13) also sends cosine-weighted rays to the nearest surface of the captured
  scene; the light leaving it is last frame's scene colour (the TAA's input, copied at the DLSS evaluation:
  rt_shadows::CaptureSceneColor) at the pixel last frame's camera saw it (the positions pass now keeps
  TranslatedWorldToClip / PreViewTranslation / view size in sun[2-7]; last frame's sun texture and positions are copied
  before it), only where last frame's position there is the hit (no light through walls). Accumulated over up to 12
  frames where the surface was seen last frame, then 0x7A1E0D14 filters it (7x7 tent at every other pixel, plane and
  normal weights; shared helpers rt_surface.hlsli) into a shared RGBA16F texture, bound at t42 for Bandai's lighting
  pass; the HDR addon (v69) adds base colour x bounce light before the fog. Needs DLSS / DLAA (the scene colour comes
  from its evaluation) and the sun shadow mask's pass (outdoors). Descriptor table now SRV t1-t5 / UAV u0-u4, 8 root
  constants; the bounce textures are made when it is first on (null descriptors until then). GPU test
  (rt_shadows_test.cpp, 12 frames, 8 rays, the square's top red in the scene colour): ground under the square 0.285 red,
  open ground 0.004; shadows and ambient occlusion as before. Path tracing proper (materials and emissive light in the
  ray-traced scene) is not possible with what the game's draws give: the scene has positions only.
- v102 (untested in game): more ray tracing (all Experimental, Graphics > Shadows):
  - **Ray-traced reflections** (config RayTracedReflections): at the lighting pass (its pre callback) the G-buffer's
    normals (t1) are copied for D3D12 and one mirror ray per pixel is traced (0x7A1E0D17) against the scene the sun
    shadow mask built; the light where it hits is last frame's scene colour where last frame saw it, else the radiance
    cache's. RGBA16F (rgb, known) at t43; the HDR addon (v70) puts it into Bandai's screen reflection (lighting00-02:
    the t16 / t17 sample) as much as the game uses its sharpest one there (smooth surfaces); sky, misses and unknown
    surfaces keep the game's. No noise: one deterministic ray.
  - **Characters in the ray-traced scene**: the skinned depth prepass (vs 0x2C264050, View at b0) is captured every
    frame and built into fast-build BLASes from a pool per frame (FRAMES_IN_FLIGHT in turn, grown by a quarter when
    needed; at most 3/4 of the vertex budget). They cast ambient occlusion, bounce light and appear in reflections; not
    in the sun shadows' rays (instance mask 2; the game's shadow maps keep shadowing them), which avoids the streaks on
    folds and blotches on faces Phantom FX 1.5.1 describes for ray-traced character self-shadows.
  - **World radiance cache** (rt_cache.hlsli): last frame's scene colour of one pixel in four added by world cell (25 cm
    near the camera, doubling every 10 m; 1M-entry hash table, keys / radiance / sums, 36 MB) (0x7A1E0D15), each cell
    moved a fifth toward the frame's mean (0x7A1E0D16), dropped after 30 s unseen. Bounce light and reflection rays
    that hit what is off screen or hidden now get its light (before: nothing).
  - **BLAS compaction** (DXR's): static BLASes are built with ALLOW_COMPACTION, their compacted size written by the build
    and read back FRAMES_IN_FLIGHT frames later, then copied into a buffer that size (typically half the memory).
  - Shared settings / flags in rt_surface.hlsli (HISTORY_VALID: last frame's positions and colour; BOUNCE_HISTORY;
    CACHE_VALID; instance masks); descriptor table SRV t1-t6 / UAV u0-u8; last frame's positions and scene colour now
    kept when the bounce light or the reflections are on.
  - GPU test (rt_shadows_test.cpp, 12 frames): as before plus a moving character (60 cm a frame: ambient occlusion
    0.70 beside it now, 1.00 where it started; no sun shadow from it), reflections of a red wall 18 m away hidden from
    the camera behind an occluder from frame 8 (not in the scene): red 4.0 from the radiance cache; open ground
    unknown (0); 3 of 3 static BLASes compacted.
  - Not possible here: OMM (opacity micromaps) needs the alpha-tested geometry and its textures in D3D12 (the capture
    has positions only, masked materials are left out); SER (shader execution reordering) needs DXR pipelines and
    divergent hit shaders, while these passes are inline ray queries whose hits read one texel. AMD: the ray tracing is
    plain DXR 1.1 (RDNA 2 and newer could run it), but this addon needs NVIDIA's DLSS / DLAA to run its frame, so it is
    NVIDIA RTX only in practice.
- v103 (untested in game): ray tracing's memory (its D3D12 device, BLASes, radiance cache, histories and shared
  textures) is freed after every ray-traced effect has been off for 10 s (600 frames), and made again when one is turned
  on (the scene is captured again). GPU test (rt_shadows_test.cpp): unchanged, all OK.
- v104 (untested in game): DXR 1.2 and much more ray tracing (all Experimental, Graphics > Shadows; needs the HDR
  addon v71):
  - **Agility SDK 1.619.6** (Microsoft's redistributable D3D12Core.dll in a D3D12 folder next to the addon;
    AGILITY_SDK_LICENSE.txt): the ray tracing device comes from its own device factory
    (ID3D12SDKConfiguration1::CreateDeviceFactory with the folder's path: no developer mode, no exports in the game's
    exe, not seen by ReShade; frame generation's D3D12 device keeps Windows' runtime). With a driver offering shader
    model 6.9 (RTX 5090: ray tracing tier 1.2, SM 6.9) DXR 1.2 is used (opacity micromaps, shader execution
    reordering); otherwise DXR 1.1 runs the same effects. The status line says which.
  - **Alpha-tested geometry** (leaves, grass, fences, hair) in the scene: the masked prepass's pixel shaders
    (tools\toa\rt_tests\scan_masked.py over the dumps, 89: 55 alpha masks = t0's alpha at a TEXCOORD x the material's
    UV scale register, x its opacity factor register, solid from 0.3333; 33 dithered LOD fades only = opaque for rays; one
    never draws) are registered too. Their stream output is made from the draw's own vertex shader bytecode (pipeline
    subobject caching on; SV_Position and the UV, 24 bytes a vertex into the same raw stream-output buffer), the View
    being the vertex shader's largest bound uniform buffer (UE's View, over 2 KB). Per capture the two material
    registers are copied (0x7A1E0D11's b3) and the opacity mask (t0) once per texture into a 512 x 512 R8 slice of a
    shared 256-slice atlas (0x7A1E0D18, least recently used slices reused). 0x7A1E0D11 packs each vertex's scaled UV as
    two halves into w and writes each capture's threshold (0.3333 / factor). D3D12 (0x7A1E0D19): every captured
    triangle's normal (octahedral) into a normal pool, alpha-tested vertices' UVs into a UV pool, each mesh's record
    (its instances' InstanceID: pool offsets, atlas slice, flags, threshold). Ray queries test non-opaque candidates
    against the atlas (rt_scene.hlsli: Occluded, TRACE_NEAREST). Leaves and grass now cast ray-traced shadows (mask 4).
  - **Opacity micromaps** (DXR 1.2): each new alpha-tested static mesh gets 4-state micromaps (0x7A1E0D1A: level 4,
    lowered to fit 8 MB a mesh, 64 MB a frame; 7 samples per micro-triangle: all solid opaque, none transparent, else
    unknown and tested by the ray query; wholly opaque / transparent triangles get the special indices -2 / -1), an
    OMM array and an OMM_TRIANGLES BLAS. The ray queries are compiled for shader model 6.9 with
    RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS (0x7A1E0D33 = 0x7A1E0D13 for 6.9). The micro-triangle order (DXR's bird
    curve), the 4-state bit layout and the special indices were checked on the RTX 5090 (tools\toa\rt_tests\omm_test.cpp:
    1024 micro-triangles, 0 mismatches).
  - **BVH refit** for characters: each character draw (its geometry and its turn among this frame's draws of it) keeps
    an ALLOW_UPDATE BLAS refitted in place to the frame's pose (PERFORM_UPDATE), rebuilt every 30 frames or when its
    vertices change, released after 120 frames unseen.
  - **Batched builds**: a frame's micromap arrays, then its BLAS builds share one scratch arena (256-byte aligned
    offsets) with one barrier each, instead of a barrier per build.
  - **Shader execution reordering** (DXR 1.2): the lighting pass's paths run as a ray generation shader (0x7A1E0D1C,
    lib_6_9; CMake does not build ray tracing libraries, so its prebuilt .cso sits next to the source with the dxc line)
    that regroups threads after each path's first hit (dx::HitObject::FromRayQuery, dx::MaybeReorderThread; hint: hit,
    instance); 0x7A1E0D1B is the same as a compute shader for older GPUs.
  - **Soft sun shadows** (Ray-traced shadow softness, RayTracedShadowSoftness 0-3 degrees, 0.5; 0 hard as before): the
    ray toward the sun's centre also returns the blocker's distance; 0x7A1E0D1F turns the rays into penumbras as
    percentage-closer soft shadows (mean blocker distance within 8 pixels x tan of the sun's radius = the penumbra in
    pixels from the surface's pixel size, up to 16; tent on the pixel's plane): no noise and no history (moving shadows
    stay with their casters), hard where shadows touch their caster.
  - **Characters cast ray-traced shadows** (RayTracedCharacterShadows, default on) on the world, not on themselves: a
    ray from the camera finds whether a pixel is a character's (their own shading keeps the game's shadow maps).
  - **Path tracing** (PathTracing / PathTracingBounces 1-4 (2) / PathTracingSamples 1-4 (1); replaces the bounce light,
    same t42): at the lighting pass, cosine-weighted paths from each pixel. A surface last frame saw, or one the radiance
    cache knows, gives its light and ends the path; one nobody saw is lit with the game's own lighting learned from the
    screen and the path goes on from it. Bandai's lighting is hand-tuned, so the paths feed its indirect light instead of
    replacing its shading: per frame 0x7A1E0D1D fits scene colour / base colour against the sun's visibility x N.L over
    one pixel in sixteen (0x7A1E0D16 solves and smooths the line: intercept = light in shade, slope = what the sun adds)
    and writes the G-buffer's base colour into each pixel's radiance cache cell; an unseen surface gets that base colour
    (grey where never seen), the sun through a shadow ray and the shade light at the last bounce; the sky from an unseen
    surface is the shade light. What such surfaces send on goes into the radiance cache, so off-screen light keeps
    itself current. Accumulated and filtered as the bounce light.
  - **Reflections** now sample each pixel's own GGX lobe (visible normals; roughness from G-buffer B; a mirror ray below
    0.05, nothing traced above 0.75) with the same path radiance (surfaces nobody saw are lit by the learned lighting
    instead of left unknown), accumulated up to 12 frames by roughness (mirrors none) and filtered as wide as the lobe
    (0x7A1E0D1E); t43 is rgb and the share of rays that met a surface.
  - **Specular occlusion** (RayTracedSpecularOcclusion): the share of the lobe open for 10 m (t44); the HDR addon dims
    the game's cubemap sky tint by it.
  - The lighting pass copies the G-buffer A / B / C (shared with D3D12) for the reflections, specular occlusion and
    path tracing once a frame (TraceLighting replaces TraceReflections; 0x7A1E0D17 removed).
  - Not done, why: volumetric light (Bandai's volumetric pass already marches its shadow map of everything, leaves and
    characters included; a ray-traced map in the same projection would look the same); particle and translucent
    shadows (sprites face the camera, not the sun, so their ray-traced shadows would have the wrong shapes; translucent
    materials have no opacity in the prepass); refraction / caustics (the water's shaders are not identified); point /
    spot light shadows (no local light draw in the captured frames: needs a frame trace indoors or at night).
  - GPU tests (RTX 5090; tools\toa\rt_tests): rt_shadows_test.cpp now also draws an alpha-tested quad through the game's
    own masked pixel shader 0x63075590, path-traces frames 12-23 and checks specular occlusion; run without and with
    the Agility SDK, every check OK in both (DXR 1.1 compute / DXR 1.2 micromaps + SER): soft shadow 0.00 under the
    square, 1.00 open; alpha-tested quad 0.00 under its solid half, 1.00 under its clear half; the character's shadow
    0.00 (22 refits); ambient occlusion 0.861; path tracing red 0.269 under the square; reflections red 4, known 1;
    specular occlusion 0 / 1; 4 of 4 BLASes compacted; the quad with micromaps on DXR 1.2. omm_test.cpp: micromap
    order, 4-state layout, special indices.
- v105 (untested in game): **Ray Reconstruction through D3D12** (rr_d3d12.hpp). NVIDIA's D3D11 Ray Reconstruction can
  turn black or crash when the GPU is waited for right after it (the ray tracing's fences do), so it now runs on a D3D12
  queue of the addon's own: the inputs (colour, depth, motion, the four guides) are copied into shared textures, NVIDIA's
  D3D12 Ray Reconstruction (the TW DLAA helper's RayReconstruction12, now with Tales' reversed depth) runs between two
  values of a shared fence and the result is copied back. Super Resolution's NGX feature is freed while it runs (and
  Ray Reconstruction's when it is off). If it cannot run, Super Resolution takes over. The status line ends in
  "Ray Reconstruction (D3D12)". GPU test (tools\toa\rr_test\rr12_test.cpp, 960x540 to 1920x1080, the CPU waiting
  for the GPU after every frame): Ray Reconstruction output mean 0.558, Super Resolution 0.531 in the same process
  (input 0.55), all finite, clean exit.
  Frame generation's module is now shared with Expeditions', Ryse's and Shogun 2's DLSS addons: the camera's near / far
  and depth direction per game (Tales: as before), the motion's scale, the game's name in the log, CreateSwapChainForHwnd
  hooked too, the overlay settings in the module (DrawSettings / LoadSettings: the same keys). fg_test: unchanged.
