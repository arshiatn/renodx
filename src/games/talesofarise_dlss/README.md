# Tales of Arise — DLAA + graphics settings

ReShade addon (DX11, x64), separate from the HDR addon (`talesofarise`):

- **NVIDIA DLAA** on the game's own TAA pass, with the camera jitter DLAA needs (the game turns it off).
- **Graphics settings** beyond the game's menu, applied live: preset buttons and one choice per setting.

## Install

1. Put `renodx-talesofarise_dlss.addon64` and NVIDIA's latest `nvngx_dlss.dll` (310.5 or newer) in
   `Arise\Binaries\Win64`, next to ReShade.
2. In the game: Anti-Aliasing on **TAA**, render scale **100%**.
3. ReShade overlay -> Add-ons -> **Tales of Arise DLAA + Graphics**: AA mode **DLAA**. The status reads
   "DLAA active — native resolution, replacing the game's TAA".
4. **Graphics**: Game's own / High / Ultra / Extreme / Maximum / Beyond Maximum (also outline distance 750000,
   NPC distance 750000, monster distance 10000, NPC / monster detail distance 1000, terrain detail distances 50,
   terrain detail bias -2, grass draw distance 20), then any setting on its own.
   Every preset keeps the game's volumetric lighting and fog (finer and cheaper values in their lists). Skit
   resolution 100%, 200% from Maximum on; smooth cutscenes on in every preset, Game's own included. A preset never lowers what the game
   already sets. Some settings apply after loading a new area. Maximum is very heavy (lots of VRAM). Volumetric fog
   can be turned off (Volumetric fog 0, in no preset: its cell size / depth slices grey out) and the volumetric
   settings also offer cheaper values than the game's. Loading -> Shaders on load (r.CreateShadersOnLoad 1 or 0, in
   no preset): an area's shaders created while it loads, fewer first-use hitches; the engine's default is 0.
Thanks to emoose: the settings his Arise-SDK.ini offers are here too (NPC / monster distance, NPC / monster detail
distance, never hide characters, full-rate animation, outline distance, skit resolution, smooth cutscenes), plus sun
shadow distance above 2 (Arise-SDK removes the game's limit). Found again in the exe from scratch: no Arise-SDK code or
byte patterns are used. Everything else is not from Arise-SDK.

5. Optional: Arise-SDK (https://github.com/emoose/Arise-SDK) for skipping the intro logos, with the included
   `Arise-SDK.ini` (it leaves the settings this addon also has to the addon). With Arise-SDK installed, its own code
   changes stay: the addon's monster distance, detail distance, outline distance and skit resolution then set its
   live values; Never hide characters and Smooth cutscenes are its own settings. Windows display scaling above 100%
   without Arise-SDK:
   "Tales of Arise.exe" -> Properties -> Compatibility -> Change high DPI settings -> Override high DPI scaling
   behavior, scaled by Application.

DLAA only: the game's TAA never upscales, so there is no DLSS upscaling.

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
