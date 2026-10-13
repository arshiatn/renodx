# Handoff: Ryse HDR + DLSS (2026-10-05, Claude Code session)

Older handoff (other games, Ryse HDR history): `C:\Users\arshi\Downloads\HANDOFF.md`. This file supersedes its Ryse parts.

## Working with ATN
- Very short answers, short code comments. Deliver changed files as a zip in `C:\Users\arshi\Downloads` (game folder
  at the zip root). Never push upstream. ATN tests in game and sends `ReShade.log` (next to `Ryse.exe`).
- Read `docs/CXX_STYLE.md` before C++ edits (designated initializers, local lambdas, no single-use helpers).
- Build: `cmake --build --preset clang-x64-release --target renodx-rysesonofrome.addon64` (HDR) /
  `renodx-rysesonofrome_dlss.addon64` (DLSS). Bin64 holds symlinks to `build/Release` (`0renodx-rysesonofrome`,
  `1renodx-rysesonofrome_dlss`, `2renodx-devkit`).
- If the build can't write the addon (game or zombie `Ryse.exe` running), rename the old file aside and rebuild.
- Shell heredocs eat backslashes (`\0`, `\\`): write edit scripts with the Write tool, then run them with python.

## RESUME HERE (2026-10-13, ATN continues on another Claude account, same PC / Windows user)
Nothing of this conversation carries over except files: this HANDOFF, memory (`C:\Users\arshi\.claude\projects\
D--Dev-renodx\memory\`, same Windows user), RE tools (`...\D--Dev-renodx\tools\{ryse,toa,exr}`), the repo (branch
`atn-mods`, many uncommitted changes: never push upstream) and the zips in `Downloads`. ATN's rules above still hold.
1. OPEN, Ryse DLSS v63 (`Downloads\rysesonofrome_dlss_v63.zip`): soldiers' armour / cloth snaps to a bigger, more
   detailed shape within ~2-4 m at every setting (screenshots `Bin64\debugForClaude20`). Ruled out by ATN's tests:
   model LOD clamp (v61, e_LodMax 0 written in memory at Detail 64x, log showed it found) and blend-shape culling
   (v62, ca_vaBlendCullingThreshold divided). v63 adds Advanced "Armour test (temporary)" (ARMOUR_TESTS in
   addon.cpp): ATN stands ~1 m from a soldier and tries each (blend shapes off, vertex animation off, tessellation
   within 0.5 m, e_LodMin 2, e_CharLodMin 2); the one that makes the near armour look like the far one names the
   system. RESULT (debugForClaude21, everything maxed): no test changes the soldier at all. But tests 4 / 5 were
   void: Detail distance 64x pins e_LodMax 0, and the attachment LOD clamp does max(lod, e_LodMin) then
   min(lod, e_LodMax) = 0. Next: ATN repeats e_LodMin 2 / e_CharLodMin 2 with Detail distance at 32x, standing
   close. If still nothing, likely game logic swapping a nearby NPC's model (not a cvar). Then: fix that system,
   remove the test, and drop e_LodMax 0 (costs fps, did not
   help) and maybe the blend threshold if they change nothing. Also ask: Shadow distance shows no difference (ATN),
   remove it? Water is capped at 2x (4x ugly, 6x crashed ATN's PC, 8x+ crashed chapter 3).
   How the RE was done: rx.py (`python -I rx.py cvar NAME` = help text from the exe; `xref RVA`; `dis RVA`),
   names.py (string users), field_any.py / disp_scan.py (struct field readers); 3D engine CVars* global +0x2e25048,
   CryAnimation console static +0x2240930; crash dumps with cdb (`C:\Program Files (x86)\Windows Kits\10\
   Debuggers\x64\cdb.exe -z DUMP -c ".ecxr; kn 30; q"`).
2. NEXT PROJECT (ATN, after Ryse): an HDR addon for Total War: Shogun 2 (Complete Edition, released 2026-10-13).
   Start with the `setup-game-mod-dev` skill (`.agents/skills/`), new folder from `src/games/generic`, shader dump
   with 3Dmigoto (`bin/cmd_Decompiler-1.3.16`), then `swapchain-resource-analysis` / `handle-sdr-tonemap-lut`.

## RESUME HERE (2026-10-12): state of all three games
- DONE (Tales HDR v28, ATN: "Along Sun Rays is insanely good", -3/4 fps, better than the cvar cells at 2). v29: "Soft"
  removed (Off / Along Sun Rays, key `sun_ray_smoothing`). v30: v29's Sun Haze slider removed again (ATN: black disc
  around the sun above 1, character flicker below 1). History: v27 fixed the black (ATN; it was NaN from
  log2(1 - filtered 1.0000001), now saturated). Stairs still faint with v27's isotropic Gaussian, so v28 adds Smooth
  Volumetric Light "Along Sun Rays" (2, default; "Soft" = v27): 11 bilinear t6 taps along the screen direction toward
  the sun's point (Cramer's rule on v1, ddx/ddy(v1) and sun dir cb0[53].xyz). Ask ATN: Soft vs Along Sun Rays, and
  the fps cost. Then ATN's haze strength slider (0-2). Older notes: Problem: stair-step / saw-tooth edge of the mountains' shadow wedge in the sun haze (8 px period in x).
  Facts (all in "Tales" entries further down, HDR v20-v26): Bandai's deferred sun lighting ps 0xF2A6E883 (replaced as
  `src/games/talesofarise/shaders/lighting00_0xF2A6E883.ps_5_0.hlsl`, CustomShaderEntry in addon.cpp's explicit list)
  reads three 480x270 volumes through s4 = a POINT sampler: t6 RGBA8 x5 (sun visibility per distance band
  0-480/-960/-1920/-3840/beyond; sky reads the far band, channel w), t7 RGBA16F x16 (in-scatter), t8 R16F x16.
  ATN's v23 test: zeroing t6 removes the wedge (haze everywhere); t7/t8 only flicker on the character. Filtering
  t6/t7/t8 with Loads (v24 B-spline / v25 Gaussian, NaN+negative skipped via exponent bits) made the stairs much
  better (Low best; Medium/High worse) BUT black cell-blocky blobs on rocks (never with Off; worse at sun shadow
  distance 4: black almost everywhere without haze). v26 filters only t6 (SampleVisibility, 4x4 Gaussian sigma 0.58
  + linear between slices) and point-samples t7/t8 as stock: ATN says not fixed. Next ideas: (1) filter only sky
  pixels (view depth r2.x beyond the last band, e.g. > 3840 + 2880) and keep geometry stock: the stairs are in the
  sky haze, the black on geometry; (2) or weight each tap by similarity to the stock point sample / same-band
  validity (cells whose content belongs to nearer geometry hold 0 visibility for far bands, so blending darkens
  geometry: r0.w-style factors multiply all lighting); (3) check with ATN Off vs On on rocks once more. Then ATN's
  idea: a haze strength slider (0-2). Tools: DLAA addon Advanced "Trace this pixel" (pixel_trace.hpp,
  tales_pixel_trace.txt next to the exe); screenshots in `Arise\Binaries\Win64\debugforclaude\Nexus_SDR_UC2`.
- Ryse cvar audit (2026-10-13) -> DLSS v59 -> v60 (sent). ATN: no chapter crashes with everything maxed (v59). v60:
  experimental options folded into the sliders, Water presets 2x (4x looked bad; 6x to see it), the 2 VF_CHEAT
  streaming cvars removed + revert guard (QualityCvar::reverts). v61 (sent, untested): armour / shields snapping to
  full detail within 2-3 m (ATN: a model switch) = attachments' CDF "LodDistances" (fixed metres, compared unscaled in
  attachment LOD +0x881950, then clamped by e_LodMin / e_LodMax). Detail distance 64x now writes e_LodMax 0 in memory
  (VF_CHEAT; QualityCvar::engine_offset 0x41c, from_choice 6; EngineCvars()). ATN: not fixed. v62 (sent): the cause
  is blend-shape culling (ca_vaBlendCullingThreshold 1 px, CryAnimation +0x8e0241): Detail distance divides it;
  Water max 2x (6x crashed ATN's PC). ATN: still grows at 2-4 m. v63 (sent): Advanced "Armour test (temporary)"
  combo (ARMOUR_TESTS: ca_vaBlendEnable 0, ca_vaEnable 0, e_TessellationMaxDistance 0.5, e_LodMin 2, e_CharLodMin 2)
  to name the system; then fix that and remove the test (and e_LodMax 0 / blend threshold if useless).
  Shadow distance (e_ShadowsCastViewDistRatio x object view distance, +0x5ea5c): ATN sees no difference. Details: Tool `.claude/projects/D--Dev-renodx/tools/ryse/rx.py`
  (cvar NAME: help text from the decrypted image's IConsole::Register calls; xref; dis). Full cvar list:
  Bin64\ryse_cvars.txt (2026-10-06, flags 0x800000 = refused). Pool limits: r_buffer_banksize 4 and
  r_transient_pool_size 4 (MB banks, read once in SPoolManager::Initialize, +0x17b3e88; the water crash copied a
  render mesh bigger than a bank, CRenderMesh update +0x17bef61 -> +0x17b4b50 -> memcpy). Candidates for ATN:
  e_StreamCgfPoolSize 80 ("Render mesh cache size in MB": likely the shield / rocks loading late with our LOD
  multipliers), e_ViewDistRatioCustom 100 (players / AI), ca_AttachmentCullingRation 300 (attachments),
  e_MergedMeshesActiveDist 250 (grass patches streamed in), e_RecursionViewDistRatio 0.1 (reflections), streaming
  speed (r_TexturesStreamingMaxRequestedMB 2 / Jobs 32, e_StreamCgfMaxTasksInProgress 32 / MaxNewTasksPerUpdate 4),
  r_TexturesStreamingMipBias 0, e_GsmCacheLodOffset 3, e_DynamicLightsMaxEntityLights 16, e_FoliageWindActivationDist
  25, e_ParticlesLod 1, water extras (r_WaterRipplesResolution 256, caustics), r_BeamsMaxSlices 200 (max 300).
- Ryse (HDR fix34, DLSS v63: v63 armour test; v62 blend shape culling, water 2x max; v61 e_LodMax 0 at Detail 64x (armour LOD); v60 experimental folded into sliders, water 2x; v59 LOD / streaming cvars; v58 Water capped at 4x (8x/16x crashed chapter 3: the sea grid (X-1)(Y-1) x 12 bytes overflows a 4 MB buffer, from 2 dumps); v55 clearer Texture LOD bias labels/tooltip; v56 "Smooth shiny surfaces" option removed again in v57 (ATN: G-buffer filter always off, better highlights; armour shimmer identical without the HDR addon = game specular aliasing). HDR fix34: Recommended presets Bloom 0.5): done, ATN: "perfect". Expeditions: Rome (HDR v26, DLSS + graphics): ATN: "perfect".
- Tales of Arise: HDR v30 (v27 NaN fix for Smooth Volumetric Light, v28 Along Sun Rays, v29 Soft removed, v30 no Sun Haze slider; v18 Motion Blur slider Advanced only; v19 16-bit scene buffers against sky banding; v20 Smooth Volumetric Light), DLAA + graphics v48 (zip `Downloads\talesofarise_dlss_v48.zip`). Confirmed by ATN: sun
  shadow resolution 6144 / 8192 (CSM atlas doubled to 16384) fixes the sun-move shimmer; "Keep characters behind the
  camera" (Arise.PF.Culling.ForceInFrustum 1) fixes characters fading in on fast turns. Untested: v34-v36
  `engine_hooks.hpp` (NPC / monster distance, NPC / monster detail distance, never hide characters, outline distance,
  sun distance above 2, skit resolution, smooth cutscenes; details in the Tales DLAA README's v34-v36 note).
- Arise-SDK (GPL-3.0): none of its code or byte patterns are used (v36: all patterns chosen anew from ATN's exe,
  own formulas). Its source was read to learn which functions to change: say so honestly. Credited (ATN): thanks to
  emoose, no Arise-SDK code or byte patterns, ours not in it: keep characters behind the camera, sun shadow
  resolution above 4096 (Nexus text, README, engine_hooks.hpp). The sun distance clamp removal is Arise-SDK's idea
  (its source), the rest of the engine cvars were found independently.
- v37: Beyond Maximum (Maximum + outline 100000, monster 10000, detail 1000: each row's highest choice) and a second
  Recommended (Beyond Maximum with the game's volumetric fog), each Recommended on its own line. ATN confirmed:
  with Arise-SDK installed the overlapping rows grey out. Zip talesofarise_dlss_v37.
- v38: outline distance and NPC distance up to 750000 (NPC distance joins Beyond Maximum). Zip talesofarise_dlss_v38.
- v39: skit resolution 200 from Maximum on; smooth cutscenes in every preset incl. Game's own (EVERY_PRESET_SETTING);
  lower presets for the new rows: outline 9000/12000/18000/24000, keep behind camera 1 everywhere, NPC distance
  5000/10000/50000/200000, monster 2/5/10/1000, detail 2/3/5/15, never hide from Extreme, URO 0 everywhere. Zip v39.
- v40: Recommended presets removed; every preset keeps the game's volumetric fog (as Expeditions). Higher distances
  (README v40 note). Zip talesofarise_dlss_v40.
- v41: terrain distributions up to 50, terrain bias -2, grass draw distance 20, all in Beyond Maximum. Character
  self-shadows: Bandai's screen-space chara shadow is driven by FPostProcessSettings fields (SelfShadowSearchRadiusTO14,
  SelfShadowSearchBiasTO14, SelfShadowStrengthTO14, SelfShadowDirectionTO14, FaceSelfShadowZBiasTO14,
  EnableSkinSelfShadowTO14, DisableScreenSpaceCharaShadowTO14) + r.EnableScreenSpaceCharaSelfShadowTO14 (default 1);
  no quality cvar; waiting for ATN's close-up of what looks wrong. Zip v41.
- v42: temporary "Halo test" rows (README v42 note) to find what removes shadows in a halo around the character
  (stock game too). Suspects: Bandai's screen-space shadows (can't see behind the character), half-res capsule
  shadows. Sun resolution above 8192: impossible (16384 atlas = D3D11's max texture, 4 cascades of 8192). Zip v42.
- v43: Halo test removed (none of its rows helped; README v43 note has the game's values). Capsule shadows at full
  resolution row (Ultra+). Halo (stock game too): ATN's shots (`Win64\testpic\Nexus_SDR_UC2`) show the bush losing
  Bandai's painted strokes / painted shadows in a band around the character: most likely the stage post filter
  (EnableStagePostFilterTO14, StageFilter* / StageShadowPenumbra* / StageEdge* in FPostProcessSettings) skipping
  character pixels. ATN: leave it. Zip v43.
- NEXT (2026-10-12): ATN installs Tales of Arise (and later Expeditions: Rome) on Steam and sends the exe path: check
  every pattern (engine_hooks.hpp, SUN_SHADOW_ATLAS, JITTER_SCALE, console lookup) against it offline with the tools in
  `C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\toa` (edit the exe path in `xref.py`), then ReShade.log. Bundled `Arise-SDK.ini` leaves the overlapping settings to the
  addon; with Arise-SDK installed the addon detects its hooks and uses its sdk.* cvars or shows unavailable.
- 2026-10-12 Steam check: ATN's old copies are gone; Steam installs `D:/SteamLibrary/steamapps/common/Tales of Arise`
  and `.../common/Expeditions Rome`. Expeditions exe: plain code, same build (1722 console lookups, singleton
  exe+0x37B6B20, ScalabilityCVarsSinkCallback exe+0x1FFF590): nothing to change. Tales exe: SteamStub (`.bind`
  section, `.text` encrypted on disk, decrypted before ReShade loads add-ons); its plain `.pdata` has the old build's
  function starts (0x1A866B0, 0x21B9FC0, 0xA0C230, 0x6288C0): same build, nothing to change. Offline disassembly of
  Tales now needs a runtime dump; final check = ReShade.log ("[Tales engine hooks]", jitter / atlas found).
  Tool scripts (`tools/toa`, `tools/exr`) now point at the Steam exes.
- 2026-10-12 Expeditions on Steam (ATN): HDR clips, DLSS never activates. Log: swap chain 2194x1234 = 4K at 175%
  Windows scaling (DPI-virtualized; the old install was not). Unreal quantizes buffers to 4 px (2196x1236): the HDR
  addon's BACK_BUFFER aspect match (tolerance 0.0001) upgrades no post target (only the LUT), DLSS rejects the
  2196-wide TAA output ("unsupported source/output texture layout"). Fix: compatibility override "High DPI scaling:
  Application" on ExpeditionsRome-Win64-Shipping.exe (both READMEs, step added). The exe also reads
  `[/Script/Engine.UserInterfaceSettings] bAllowHighDPIInGameMode` (default true; game config sets it false) -> an
  Engine.ini alternative, untested (UI scale unknown).
- 2026-10-12 Tales DLAA v44: "Weak CPU" checkbox under the presets (CPU_HEAVY_SETTINGS: ForceInFrustum, never hide
  characters, a.URO.Enable -> the game's in every preset; ticking sets them now). ATN: ForceInFrustum ~5 fps on X3D
  cache cores, ~30 on frequency cores. Zip talesofarise_dlss_v44.
- v45: Weak CPU without a.URO.Enable (ATN: no difference). Zip talesofarise_dlss_v45.
- v46: "Far sun shadows (mountains)" checkbox (FAR_SUN_SHADOWS: distance 3 + resolution 8192 in every preset; light
  through mountains vs softer near shadows, so in no preset). Sun distance choice 3 added. Zip talesofarise_dlss_v46.
- v47: sky banding (ATN: in SDR too; scene color is already RGBA16F). Advanced pixel trace (pixel_trace.hpp, README
  v47) to find the sky's pass and the format that bands: waiting for ATN's tales_pixel_trace.txt (aim at the banded
  sky). Then: deband + dither like Pharaoh's skydome in that shader, or upgrade the low-precision target.
- Trace result (2026-10-12): scene color RT0 is R11G11B10 3840x2160 (r.SceneColorFormat 4 ignored by Bandai's
  renderer; frame starts with last frame's sky: no clear). Sky = Unreal's sky sphere material ps 0x11039AD5 (vs
  0x57DAED04; gradient from cb2 constants, t0-t2 cloud noise only), then fullscreen ps 0x2483CE26 and 0x381F5A30
  (R11G11B10), half-size R11G11B10 1920x1080 chain, tonemapper 0xDCFDF14D. Fix: HDR v19 "Scene buffers" (Advanced,
  restart, default 16-bit): R11G11B10 RT / copy-dest at back-buffer aspect -> RGBA16F (view cloning). DLAA accepts
  RGBA16F color. DLAA v48: trace compares only texel bytes. Zips talesofarise_v19, talesofarise_dlss_v48.
  ATN: sky fixed.
- HDR v20 (untested): stair-step haze edge near the sun (Bandai volumetric lighting, 8-px cells; cell size 2 helped for
  -25 fps). Trace 2: deferred sun lighting ps 0xF2A6E883 samples the 480x270x16 RGBA16F volume t7 once (line 966 of
  the dump). Replaced shader `lighting00_0xF2A6E883`: 3x3 1-2-1 taps one cell apart (Smooth Volumetric Light, default
  On). Volume also read by 0x0A9D482A, 0x1C8D11B6, 0x1FE8B2B6, 0x36E08F9A, 0x4E695056, 0x4E8218FE, 0x8CCA5153,
  0x90EF2450 (ps) and 0x0DCD11E1, 0xBC95A089 (cs): stock. Zip talesofarise_v20.
- HDR v21: v20 forgot CustomShaderEntry(0xF2A6E883) in custom_shaders (the list is explicit!): added. t8 (R16F
  480x270x16, slice depth / 6000) smoothed too (SmoothVolume). Zip talesofarise_v21. ATN: still no change
  (replacement confirmed in the log). v22: the saw-tooth is t6 (RGBA8 480x270x5, sun visibility per distance band;
  sky reads the far band): every t6/t7/t8 sample -> SampleVolume (3x3 1-2-1 when On). Zip talesofarise_v22.
  ATN: ~0.1% better. Trace 3 (dark side, 47.1% x 25.3%): lighting00 output A 0.17/0.32/0.53 vs bright side
  0.35/0.58/1.03: the wedge is made in lighting00. Saw-tooth period 8 px in x (480-wide cells). v23: temporary
  "Volumetric Light Test" (1/2/3 zero t6/t7/t8 samples) to find the volume. Zip talesofarise_v23.
  ATN test: without t6 the wedge is gone (haze everywhere), t7/t8 off: flicker on the character. Volumes are read
  through s4 = POINT sampler (8-px blocks; a 3x3 of point samples kept steps). v24: SampleVolume = cubic B-spline
  over 4x4 cells via Load + linear between slices (32 loads per call, 4 calls max per pixel). Test setting
  removed. Zip talesofarise_v24. ATN: stairs much better, but black blocks on rocks with Smooth on (not Off, so
  the decompile is fine), changing with sun shadow distance. v25: FilterVolume skips NaN/Inf (exponent bits:
  fxc compiles isnan() away, warning X3577) and negative cells, renormalizes; single-return functions (X4000).
  Smooth Volumetric Light Off/Low/Medium/High (Gaussian 4/6/8 taps, sigma 0.58/1/1.5; Medium/High only t6).
  Zip talesofarise_v25. ATN: black still at Low/Medium/High (never Off), sun distance 4: black almost everywhere
  without haze; Low looks best. v26: only t6 filtered (SampleVisibility, 4x4 Gaussian + linear slices), t7/t8
  point-sampled as stock (their finite "no data" cells caused the black); Off/On again. Zip talesofarise_v26.
  Next (ATN idea): a haze strength slider (0-2) once this works. Side note: psycho_test31.hlsli isnan() calls
  are compiled away by fxc (X3577) - check the PsychoV31 black-pixel fallbacks some day.
- 2026-10-12 Expeditions DLSS graphics v13 (untested): jitter fix. UE 4.26 keeps the TAA jitter position in a signed
  byte (FSceneViewState +0x4AA, movsx at exe+0x1368EC0 in PreVisibilityFrameSetup, and the getter exe+0x132D120);
  pattern length min(trunc(8 / scale^2), 255) (exe+0x1368F33..). Past 128 positions (< 25%) it goes negative,
  Halton(<= 0) = 0 -> jitter frozen 127 of 256 frames. Both loads patched movsx -> movzx (one byte each,
  `code_patch.hpp` copied from Tales) while loaded. Warning threshold 18% (25% without the fix); Advanced shows
  "jitter fix: on". Below 18% UE's 255 cap remains (own jitter = Ryse-style stub, not done). Zip
  expeditionsrome_dlss_graphics_v13.
- Expeditions DLSS graphics v14 (untested): Texture LOD bias like Ryse (Off / Matched / Sharper default / Sharpest).
  UE already biases by max(log2(scale) + r.ViewTextureMipBias.Offset (-0.3), r.ViewTextureMipBias.Min (-1 in this
  game)), TAAU path only (exe+0x1369008, View +0x46FC -> cb0[137].y in 599 PS). Addon writes Offset 0/-1/-2 and Min
  -16 while DLSS < 100%, game's values otherwise (README). Zip expeditionsrome_dlss_graphics_v14.
- v15: clearer Texture LOD bias labels; tooltip shows each choice's bias at the current scale. Zip v15.
- Next, in order: (1) ATN tests v36 (also once with Arise-SDK installed: ReShade.log "[Tales engine hooks]" lines);
  (2) ATN gives his preferred values: update the Tales presets' sliders; (3) ATN buys Tales of Arise and Expeditions:
  Rome on Steam: check every pattern against the Steam exes offline first (tools below; Denuvo-encrypted code only
  shows at runtime: then ReShade.log), then in game.
- Analysis tools (copies): `C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\toa` (Tales: `xref.py` exe +
  xrefs, `toa_dis.py <rva> [from to]` disassembly, `sdk_sigs.py` / `csm_atlas.py` pattern checks) and `...\tools\exr`
  (Expeditions). Edit the exe path at the top of `xref.py` for another build. Reflection offsets: find the property
  name's char string, the pointer to it in .rdata, offset at +0x28 of that params struct.

## Ryse HDR addon (`src/games/rysesonofrome`) - done (fix9)
- HDR (Extended) = UC2 Extended (`uncharted2extended.hlsli`, Musa/Jon) on the game's Hable; Highlight Expansion removed;
  fxc crashed on `SignSqrt(-x)` -> rewritten with `sign()*sqrt(abs())` in that local copy.
- PsychoV31 keeps the game's grading, replaces only the curve. A "Highlight Hue Shift" slider was tried, then removed
  (V31's default already = full response hue).
- ATN: "HDR looks ready".

- 2026-10-05 review (code reading): Hable coefficient mapping, UC2 Extended, peak / sRGB-on-2.2 emulation, Paper
  White / UI chain all consistent. V31 mid gray exact: anchor 0.18 -> FilmCurveY(0.18) (anchored grading = gain
  anchor_out/anchor_in, C-inf shoulder starts at anchor_out), same as SDR after chart + emulation.
  Clipped in HDR: sun shafts and lens flares (up to white, by choice), film grain (up to white), videos (SDR).
  Lens flare sprites (AC3155D8 ghosts, BE5D77E6 orbs, 92DFB7F1, 3468E47B; not replaced) each tonemap with their
  own filmic curve x(ax+b)/(x(ax+1.7)+c) (HDRParams, Hejl-style) and saturate -> HDR flares possible. Scene passes
  (bloom, fog, particles, sky, motion blur, DOF) clamp only weights.
- fix10 (untested, zip `rysesonofrome_fix10.zip`): Extra toggles Sun Shafts HDR / Lens Flares HDR / Film Grain HDR
  (On by default) + Video Highlights slider (ITM, default 0). Light past white added via `RyseAddLight` (Neutwo
  into the headroom below Peak). Flare sprites flare00-03 (curve to x = 0.5, linear-light tangent above). Grain
  overlay white = Peak. See README "HDR effects".
- fix10 test (debugForClaude8, PQ PNGs): grain on highlights OK; lens flares no visible change (scene without
  bright flares?); Sun Shafts HDR turned the sun area orange (per-channel excess, red only). fix11: RyseAddLight
  adds the lost light's luminance in the pixel's SDR hue. Video Highlights tooltip simplified.
- fix11 test (debugForClaude9): half circle with colored rim on the flag pole near the sun = crease of the stock
  screen's saturate(1 - base) where the HDR bloom passes 1 (flat white plateau outside, bloom inside; gone with
  bloom off). fix12: `RyseScreen` (1 - base up to 0.5, then C1 exp tail) for shafts and lens flares. Lens Flares
  HDR removed (flare00-03 shaders deleted).
- fix12 test: half circle fixed, game "beautiful". fix13: Film Grain HDR removed (ATN: stay faithful), Sharpening
  default 0 (tooltip: game's own = 1), Video Highlights tooltip says ITM, Video Dithering toggle (On; IGN 1 code
  value before the ITM, only with Video Highlights > 0).
- fix14: Sun Shafts HDR toggle removed; the shafts past 1 (stock saturate) are always added in HDR modes.
  ATN: "HDR is perfect", DLSS "super happy".
- fix15: Chromatic Aberration slider (x vLensOpticsParams.w, the post pass's red shift of the lens flare layer;
  exe strings r_ChromaticAberration / r_FlaresChromaShift). Full-screen ChromaShift filters exist in the exe but
  never seen in dumps. autoexec.cfg: Game.log shows "Unknown command" for every e_/r_/con_ cvar (compiled out
  in the PC build); only sys_spec_* (not sys_spec_Quality) accepted.
- autoexec.cfg now = all 14 sys_spec_* groups at 4 (all accepted, Game.log 23:42). Correction: the cvars are NOT
  compiled out; the game's cvar whitelist (+0x1594B40..+0x15967AA) rejects them with a fake "Unknown command".
  Whitelist + ICVar offsets in the HDR README. Possible next: addon sets non-whitelisted quality cvars directly
  (diagnostic first: log their values at Very High).
- DLSS v28 (untested): Advanced "Save the game's settings" -> Bin64\ryse_cvars.txt (all cvars, values, flags; console
  via CONSOLE_REGISTRATION pattern = con_restricted's registration at +0xF013B0; map CXConsole +0xF8). Next: read
  the dump at Very High, propose raised values, then an addon option that sets them (ICVar Set int +0x38 / float +0x40).
- v28 test: 3141 cvars dumped, con_restricted 0, GetCVar(r_Width) = the renderer's. Game menu: Low/Medium/High only;
  sys_spec 4 is above it (ATN sees a difference). Chromatic aberration: vLensOpticsParams.w = r_FlaresChromaShift (6;
  +0x180AFB6), r_ChromaticAberration = 0 (off). v29 (untested): "Game settings above its High" (Shadow resolution x2,
  Draw distance x1.5/x2, Detail distance x2/x4, Texture streaming pool x2/x4), README.
- v29 test: "100% works". v30 (untested): section moved below the DLSS status (Simple), Reset / MAXED OUT, options:
  Engine quality (14 sys_spec groups, absolute 3/4), Shadow resolution x2/x4 (+pool, res scale), Shadow distance,
  Draw distance up to x3 (+merged meshes, veg sprites, proc veg), Detail distance (+attachments, detail textures),
  Tessellation (distance x, triangle size /), Texture pool, Decals, Chromatic aberration slider (r_ChromaticAberration
  0-16: CA_Sharpening pass at +0x180A1D0, psParams.x, runs only above 0; HDR behavior of that pass unknown). If CA
  looks good, the HDR addon's Chromatic Aberration slider may switch to it.
- v30 test: all work. v31: "Game's" -> "In-game setting", TESTING button (last items: shadows 8x capped 16384, shadow
  dist 4x, draw 6x, detail 10x, tess 8x, pool 8x, decals 10x), CA option removed (moved to HDR).
- HDR fix16: Chromatic Aberration = the game's full-screen CA (CA_Sharpening 0xC53CE326, from the DevKit auto-dump
  Bin64\renodx-dev\dump) rebuilt in post.hlsli (default 0, 0-16, new key chromatic_aberration_amount); lens flare
  shift stock again. UI slider flicker fixed: Paper White-UI gain moved from tonemap/TAA output to the post pass
  output (TAA/DLSS history was in UI-scaled space).
- "New ones 3" (27 shaders after the higher settings, = the DevKit auto-dump): 20 lighting/material variants
  (irreg_kernel_2d: finer shadow filtering), 2 VS, motion blur gathers 0x2EC2DAAE / 0x3CB3A474 (read the replaced
  velocity pack 0x799A5EB8, no color clamp), SSDO 0xD1106E8B, CA_Sharpening 0xC53CE326. None need replacing.
- HDR fix17: Chromatic Aberration slider 0-2 (default 0; 1 = amount 6 = r_FlaresChromaShift), key
  chromatic_aberration_strength.
- v31 test (debugForClaude10): ATN edited TESTING to shadows 32x, others 256x; shadow res stops at our 16384 cap and
  costs the most; e_DecalsRange refused (reads back 20). Past the caps (camera far plane, LOD 0, tess factor 64, all
  textures resident) more changes nothing. v32 (untested): new knobs with headroom (per-object shadows, shadow
  cascades 6/7/8, vegetation pools, vegetation sprite res, particles, water), labels match ATN's values, auto
  exposure note under the DLSS preset.
- HDR fix18: Chromatic Aberration slider 1 = amount 12 (0.5 = old 6, 2 = 24; 16 was only the v30 test slider's end).
- Release candidates (ATN to write the text and release on Nexus / Discord): DLSS v33 (buttons Reset / Very High /
  Recommended MAX (shadow res 4x) / MAXED OUT; "Graphics preset (in-game)"; more steps per option) and HDR fix19
  (Recommended preset: PsychoV31 + Film Grain 0). DLSS v34: texture pool 16x (24 GB) added (MAXED OUT; Recommended
  MAX stays 12 GB). v34 test: pool > 6 GB loads no textures: the game keeps the pool in bytes in 32 bits (6 GB ->
  2 GB, 12 / 24 GB -> 0). v35: In-game / 2x (3 GB) / 4 GB max (capped 4000 MB). v36: note that an AA mode / scale
  change reloads textures (some can stay missing: checkpoint or restart). v36 test: 3 / 4 GB still leave some textures
  unloaded. Cause: +0x174015F does (secondary + r_TexturesStreamPoolSize) << 20 in 32 bits, stored as an int
  (signed: negative from 2048 MB; other readers use movsxd / 64-bit). v37: In-game / 2 GB max (2047 MB).
  v38 (untested): shadows ended closer at smaller scales = vegetation sprite distance x render width / 1024
  (+0x16EB00); the addon pins e_ForceDetailLevelForScreenRes (3D cvars +0x3f0, written in memory) to the display
  width while rendering below it. v38 test: fixed.
  v39 (untested): Fine detail options: G-buffer filtering Off, Texture LOD bias (anisotropic sampler clones bound after
  PSSetSamplers; log2(render / display), Sharper -1), Foliage anisotropic filtering (r_TexNoAnisoAlphaTest static = 0).
  v40: foliage aniso and G-buffer filtering Off are automatic while DLSS / DLAA runs (renderer_pins, statics); Texture
  LOD bias moved under the DLSS preset (Off / Matched / Sharper default / Sharpest -2, key Ryse AA/TextureLodBias).
  v41 = RELEASE (ATN: "It is ready"): preset M default (L looks the same in Ryse, costs more), "(experimental)" removed
  from the addon name and the overlay tab ("Ryse DLSS").
  v42 (reverted): the post pass redrawn at the display size got a copy of its b0 with PS_ScreenSize = display size
  (CopySubresourceRegion1 + UpdateSubresource1 on one register). No effect in game (debugForClaude12: 14% CA offsets
  exactly display / render = 7x DLAA's; grain at display resolution, so the redraw ran); cause not found. HDR fix25
  instead: post.hlsli takes its texel offsets from t0's size (display in the redraw, render size otherwise).
  ATN: fix25 fixed it. v43 (tester report, untested): Draw distance no longer raises e_ViewDistRatioLights (shadows
  flickered in one area with draw distance up, any shadow settings; suspected shadow pool overflow from more lights);
  e_ShadowsPerObjectTexRes capped at 8192 (bands on characters at shadow res 4x / 8x; suspected bias vs D16,
  r_ShadowTexFormat 5). HDR fix26: hotkeys F6 / Caps Lock = HUD (skip 0x96675B77 + 0x1B59F534, menus too), F7 / Tab =
  execution highlights (skip 0xD2063417); session only. HDR fix27 (ATN: works): hotkeys polled with GetAsyncKeyState
  at Present (F8 on ATN's keyboard is a SteelSeries Print key, so ReShade's key state was never the problem); F7 also
  0xF495DD17 (head highlights); F8 / key above Tab (scan 0x29) = execution skull: 0x1B59F534 draws >= 3500 indices at
  4K (skull 3744-4539, other HUD <= 3219), cut x (height / 2160)^0.23. Logger kept behind RYSE_UI_DRAW_LOG (F9).
  DevKit MCP bridge works (build\Release\renodx-mcp-bridge.exe, JSON-RPC over stdio) but has no draw args/constants.
  HDR fix21: upgrade aspect tolerance 0.03 (reduced targets rounded down at 77% / 58% stayed 8-bit: no uncapped
  shafts there), "Experimental Uncapped Sun Shafts" toggle back (On); fix22 renames it "Sun Shaft Highlights"
  (tooltip: never above Peak). HDR fix23: sun shafts were ~4x larger / blinding vs vanilla, also in SDR: the mask
  (0x4DFAE399, sky depth x 1.32 x sum(scene)) and radial blur (0xF1FCED4E) write quarter-size RGBA8 targets that
  clamped to 0-1; our FP16 upgrade (back buffer aspect) removed the clamp. Both replaced with saturated outputs.
  Other reduced-size RGBA8 targets are upgraded too (DOF, flares, ...): watch for similar differences.
  HDR fix24 = RELEASE (ATN: mod ready): "Sun Shaft Highlights" toggle removed, always on in both HDR modes.
  Future mods: use view cloning for resource upgrades (not on Ryse, by ATN's choice). HDR fix20: Recommended = PsychoV31, Film Grain 0, Motion Blur (Camera) 0, (Objects) 1, Video
  Highlights 0.25.

## Ryse DLSS addon (`src/games/rysesonofrome_dlss`) - v27 DONE (Jitter option moved to the bottom of Advanced) (ATN: 'DLSS basically done')
- Separate addon, copy of `expeditionsrome_dlss` infrastructure (`ngx_backend.hpp`, `performance.hpp`,
  `native_state.hpp`, `constant_mirror.hpp` with an upload hook). CMakeLists links NGX for both `_dlss` addons
  (local change). `README.md` documents the whole design.
- Hooks: TAA `0x121C45E4` (replaced by DLSS), SMAA edges/weights/blend `0x15D2F34D`/`0x5DC96409`/`0xF2D692D1`
  (skipped while DLSS ran last frame, blend -> copy `0x52594503`), post `0xD0B247FF`/`0xB022AFEA`/`0xC3A469C1`/
  `0xCC6DF78E` (upscaling: re-drawn at display size), native upscale `0xCD8548F6` (upscaling: 1:1 copy).
- Private shaders: `0x52594501` prep (reverse-Z depth, motion px, linear color; drawn with the game's TAA VS),
  `0x52594502` encode NVIDIA output (sRGB or gamma 2.2 round trip), `0x52594503` copy t1 (SV_Position only).
- Tested in game (ATN, v8-v10): DLAA works. Own jitter is 8 positions all the time (gameplay, slow motion, pauses).
  Motion vectors "The game's, as is" = best/stable (the game's MVs contain no jitter). Flips flicker (signs
  right). Off<->DLAA instant (NGX runtime kept, only the feature released). SMAA skip: +4-5 fps. Exit crash fixed.
- v11 (untested): modes like Expeditions (DLAA, UQ 77, Q 67, B 58, P 50, UP 33, Custom 1-100); live render scale via
  the game's setter; DLSS outputs display size; post re-drawn at display size; upscale -> copy; needs Native
  Upscaling on; jitter phases 8*(display/render)^2.

### Reverse-engineering facts (Ryse.exe, base 0x137000000, RVAs)
- The exe code is Steam-DRM encrypted on disk. `Bin64\ryse_image.bin` = decrypted image dump (file offset = RVA),
  made with the addon's Advanced debug button. Analysis: python + capstone/numpy scripts (rip-relative ref search).
- cvars (static ints): `r_PostAAMode` +0x22BA8EC (game sets 1 = 2x; addon forces 6), `r_PostAAEdgeFilter`
  +0x22BA8E8 (=3 SMAA 2TX), `r_PostAA` +0x22BAD4C, `r_PostProcessEffects` +0x22BAD44. Finder: `lea rdx,name` +
  `lea r8,&var` near the `Register` call (engine_memory.hpp).
- Jitter: renderer +0xCD0/+0xCD4 = sub-pixel offset (NDC = 2*px/width/ss), added to proj m20/m21 in the camera setup
  (+0x17E97A2 block). Offset computed inline in `RT_RenderScene` (+0x18000B0): zeroed, then only if r_PostAA,
  per-view flag, EdgeFilter==3, `ITimer::GetTimeScale(1..5)>=1` (vtable +0x48), frame counter (+0x2B728F0) moved.
  Addon: 5-byte jump at +0x18006CB (pattern JITTER_SETUP) to a stub near the exe (stores the addon's offset,
  saves r12 = renderer at stub+72, continues at +0x1800ABE). The apply block also requires bit 27 of renderer
  +0x4524 clear (set while paused): `and cl,al` at +0x17E97F6 -> nops (only reader of that bit).
- Render scale: renderer = static object +0x22AA780 (vtable +0x1E4A2D0; global ptr +0x222FB58). Request = vtable
  +0x168 (function +0x173A6E0; ret Vec2 via rdx, x/y in xmm2/xmm3): trunc to px, &~7, min 128, 1 if both >= 1 ->
  pending +0xC1D8. Present-thread end of frame (+0x1702D70) copies pending -> current +0xC1E0 (prev +0xC1E8; forced
  1 by debug cvars). RT_RenderScene calls vtable +0xC50 (+0x16FAEA0) with +0xC1E0 every frame: +0x52A4 scale,
  render size +0xC1F8/+0xC1FC. Callers of the request: console command (+0xEB8910), dynres (+0xEB9150), reset.
  v12 wrote +0x52A4 directly -> overwritten each frame. v13 calls the request: it is the game's DYNAMIC RESOLUTION
  (overload scene manager, osm_* cvars, console osm_setFBScale +0xEB8910): targets stay full size, every pass renders
  into the top-left corner (viewport), fullscreen VS scale uv by g_VS_HPosScale (PER_FRAME c21). v13 test: TAA
  "unexpected size" (input 3840x2160, viewport 2952x1656) -> stock TAA -> the game shows only the corner (frozen
  picture around it). v14: TAA render size = viewport, DLSS output = texture size over the whole TAA target; post
  (4 variants) + native upscale redrawn with own full-screen VS 0x52594504 (uv 0-1, full viewport); lens flares t5
  stretched from the corner (0x52594505); fallbacks: no DLSS for 3 presents -> game's scale; corner not upscaled or
  post not redrawn -> stop until mode change. Motion test modes 1/2 removed (motion px = uv * texture size).
- v14 test (ATN): Quality/Performance upscale (DLSS active, whole frame redrawn) but gameplay shows a darker half-size
  copy of the frame in the top-left quarter (+ a black 25% box): some pass after the TAA still uses the corner layout
  (candidates: LayerBlendAmount passes 0x93B10183 / 0xDC26E6B4). Main menu: TAA only every other frame -> menu drawn
  into the corner. UQ/Custom refused by the backend's aspect check (fixed in v15: 16 px tolerance). v15 adds the
  pass trace (Advanced button, automatic once after the first upscaled frame) and "game's scale until 30 DLSS frames
  in a row". v15 trace: after the TAA only the post pass and the Scaleform UI (96675B77, 1B59F534, 8BA2D1A9) into the back
  buffer, all with the corner viewport -> UI shrunk into the corner. v16: UI draws redrawn with the whole viewport
  (generic draw hook after the post), Custom backend for mismatched corners (UP transition fix).
  v16 test: UI grew by 1/s (UI maps pixels 1:1 into the corner viewport, clipped) and radial MV streaks (quad uv
  spans the corner, mViewProjPrev the screen). v17: Scaleform VS copies 0x52594506-0D (clip transform for the whole
  viewport), prep reprojects in screen space with mViewProjPrev's own uv scale.
  v17 test: UI right; every scale below 100% smeared (95%: radial streaks; 77/50%: repeated bands; 1%: nested
  rectangles to the top-left; also in pauses). Cause = the GAME's motion blur: PostAA::Render (+0x180C670) and
  MotionBlur (+0x18C92C0) build mViewProjPrev = prev view x proj (+0x22BA368) x static scale-bias +0x221CC10
  (0.5,-0.5,0.5,0.5; referenced only by those two) -> uv [0,1], while their shaders (velocity pack 799A5EB8, TAA)
  subtract v1 in [0,s]. SSR (E0B7E115) multiplies by PS_HPosScale.zw, so Crytek's convention is [0,1] x HPosScale.
  v18: OnTaaDraw writes the corner into that matrix's 0.5 entries (pattern REPROJECTION_SCALE_BIAS at +0x180CEC2,
  values checked, restored at unload; testing checkbox ScaleReprojection). SSAO_ReprojectionMatrix uses its own
  generic 0.5 (full-viewport half-res buffers, likely fine). Object velocity scale (G-buffer, e.g. 0x7C093A97:
  v0 * 2 * PS_ScreenSize.zw vs prev from VS) not verified.
  v18 test (HDR addon loaded too, TAA format 10): smear gone; top-left corner region shows semi-transparent stacked
  copies of the scene shrunk toward the top-left (boundaries at s, s^2, s^3 of the screen), parts frozen, HUD full size
  over it. Ruled out: second TAA run (shared command_action has one elected handler module), HDR proxy pass (sets a
  full viewport), PS_ScreenSize (engine sGetScreenSize +0x1824F60 = viewport w,h, 0.5/(w/scale) using renderer
  +0x52A4: correct; skinned velocity VS writes prev * g_VS_HPosScale, so object velocity is texture uv). Post t5 = the
  refraction/flare copy texture (screen-blended). v19 adds the stage capture (PNGs) to find where the copies appear.
  ATN: the game's own Rendering Resolution (r_Width, menu 50-100% step 10, Native Upscaling) at 50% looks normal (no
  copies, DOF fine) with a sharp HUD (UI drawn after the native upscale at display size). DOF also broke under OSM
  (black in executions). => v20 DROPS OSM: sets r_Width/r_Height via renderer ICVar* (+0xB828/+0xB830, native
  +0xF540; Set(int) +0x38, GetIVal +0x10; menu code +0xED912E does the same), DLSS at the true low res outputs display
  size into taa_full; stock TAA still runs; post pass drawn again at display size into post_full (t0 = taa_full);
  native upscale CD8548F6 replaced by a 1:1 copy of post_full (0x52594503 on t1). Removed: OSM control, scale-bias
  patch, UI VS copies, flare stretch. Engine facts kept: sGetScreenSize +0x1824F60 (0.5/(w/scale)), renderer +0xF549
  = native upscale active.
  v20 test (HDR addon on): all modes work, images sharp, HUD sharp. Non-16:9 sizes (2957x1663, 2227x1253, 512x300)
  were not upgraded by the HDR addon (TAA formats 29/27, HDR clipped at 85 nits; also slower). The game renders
  sizes below 512x300 at 512x300. 40% used preset M (only the selected mode's hint was set). FPS (paused, 4K):
  TAA 167, DLAA 181, Q 157, P 196, UP 214 (likely CPU-limited near 200). v21: exact-aspect steps, min 512x300,
  all preset hints. Slight temporal wobble in pauses at lower res (DLSS itself?) still open.
  v21 test: saved mode stopped at startup (game sets its own r_Width once while loading; v21 counted it every
  present -> 'keeps setting its own'); DLDSR 2.25x GPU-bound: TAA 90, DLAA(L) 50, K 90-100, lower scales faster.
  Wobble when still, also DLAA. v22: game writes counted once (last_seen), Custom 14-100, fixed exposure texture
  (no AutoExposure), prep folds WorldViewPos into mViewProjPrev's translation in double (ps_5_0 dmul/dadd).
  v22 test: startup mode fixed. Still wobbles (whole image sub-pixel shifts frame to frame; HDR graph moves in
  DLAA too). Motion capture while still: ~(-0.12, +0.05) px, near-uniform -> jitter in the camera MVs (view ray
  TEXCOORD1 jittered vs mViewProjPrev unjittered). v23: current position = this frame's camera (VS b2 rows, as
  uv) applied to the same reconstructed point; part along w (jitter) removed from both matrices; self-check vs
  v1 (1 px) falls back. Capture adds dlss_motion_fine (+-0.5 px) and logs the frame's jitter.
  v23 test: wobble less but still there, HDR graph still moves (DLAA too). Fine motion capture: |MV| < 0.01 px
  (fixed). DLSS indicator says 'Autoexposure: ON' although the flag is gone (driver override?). Suspect: lens
  flares t5 (AC3155D8/906C01A4, CPU xform) added after DLSS at render res, jittered. v24: two-frame capture
  (input/output/lens_flares/post _next) + testing checkbox 'No lens flares'.
  v24 test: lens flares black (not it); input shift = reported jitter delta within 0.01 px; output shifts < 0.02 px
  (DLSS residual). v25 cleanup: SMAA skip always on (checkbox gone), jitter flips and No lens flares removed.
  Debug buttons (trace, two-frame capture, code dump) kept. NEXT: back to the HDR addon (ATN's item).
- The engine's UpscaleImage (+0x180B270, shader 0xCD8548F6) runs only for Native Upscaling (renderer +0xF549, from
  +0x16FD060), never for OSM: the PC game never stretches the OSM corner.
- The game's Rendering Resolution menu = r_Width/r_Height + Native Upscaling (r_FullscreenNativeRes): real smaller
  targets via ChangeResolution (renderer vtable +0xF8, from HandleDisplayPropertyChanges +0x16FAF60 reading ICVar*
  at renderer +0xB828/+0xB830; ICVar Set(int) = vtable +0x38, GetIVal +0x10; IConsole GetCVar +0xB0; gEnv
  +0x222FA40, pConsole +0xB8, pRenderer +0x118). Alternative if the OSM path breaks: set r_Width/r_Height (swap
  chain reset per change, saved by the game). Display size +0xBDE8/+0xBDEC = render resolution (GetWidth +0x200).
- TAA cbuffer: `mViewProjPrev` unjittered (measured), view ray v2 unjittered (inferred), t2 linear depth*far.

### Next steps
1. ATN tests v20 (Native Upscaling on; all modes: hitch, then sharp image, HUD sharp, DOF/particles fine; AA Off
   restores the game's own Rendering Resolution; if broken: stage capture PNGs) (all modes in gameplay: no smear/streaks, HUD normal size; moving characters for object velocity;
   AA Off restores the scale). Testing checkbox off should bring the v17 smear back (confirms the cause).
2. Then: performance work; remove test options (motion modes 1/2, jitter flips, image dump button,
   "previous jitter" log) once stable; maybe restore the render scale on addon unload (not at process exit).
3. Offer to port the exit-crash fix (`RtlDllShutdownInProgress` in Backend::Reset) and the Off-keeps-NGX fix to
   `expeditionsrome_dlss`. That addon has the same code; ATN asked not to replace it.
4. Ryse HDR addon open items from the old handoff (HDR10 output, supersampling and other paths untested) remain.

## Expeditions: Rome HDR (`src/games/expeditionsrome`) - v14 (2026-10-09, untested)
- Compared with marat569's UE Extended (`src/games/ue-extended`, untracked copy from ATN). No SDR LUT textures in
  this game's CombineLUTs, so their LUT upgrade does not apply. Added mode 3 "HDR (UE Extended)" (experimental):
  their UE Filmic Extended at its defaults in `ue_extended.hlsli` (see README). Differences found vs our Extended:
  our Extended keeps UE's Expand Gamut (theirs and PsychoV31 drop it), UE matrix blue correction vs their exact
  inverse, post grade after (ours) vs before (theirs) the display mapping, SDR-matched color (ours) vs much brighter
  above mid gray (theirs).
- v16 (ATN): old Pharaoh-style Extended replaced by the UE Extended port (mode 1); mode 3 and the old Extended
  sliders removed; Expand Gamut off in both HDR modes (ATN: it oversaturated both). Remember for Tales of Arise.
- DLSS v44: Shadow pool option (e_ShadowsPoolSize out of Shadow resolution; matched / 2x / 4x 16384, floor = Shadow
  resolution factor via QualityOption::floor_option) and Light distance option (e_ViewDistRatioLights 1.5x-8x;
  Recommended MAX: pool 4x, lights 2x). Untested. Ryse HDR Extended fire looks broken (tester): cause = MatchSDRColor +
  HueOKLab to SDR in tonemap00 (UC2 Extended core matches Musa/Jon/upstream gbfrelink); ATN deciding the fix.
- HDR fix28: "colors from SDR" removed from Ryse Extended (MatchSDRColor + HueOKLab to SDR; code and how to restore
  in rysesonofrome/README.md). Extended = UC2 Extended per channel + Neutwo max-channel roll-off to Peak.
- HDR fix29: Extended Hue Correction (ext_hue_correction, default 1): renodx::color::correct::Hue(UC2 per-channel
  result, untonemapped scene) before the Neutwo roll-off, as Musa's Batman (batmanak tonemapper.hlsl).

## Tales of Arise HDR (`src/games/talesofarise`) - v1 (2026-10-09, untested)
- UE4 4.21. Built from Expeditions: Rome v17. CombineLUTs `0x876F0F03` is Expeditions' `0x61F067B4` except two HDR
  output-device branches; lut00 = Tales stock + exactly the Expeditions edits (verified by diff).
- Tonemappers: one body in `tonemap.hlsli`, permutation files set TOA_* features. 0xDCFDF14D from ATN's dump,
  0x8D5C5C52 / 0xC89B89AF / 0x7C1D6772 (cutscene: CA + grain) from marat569's UE Extended. Sliders: bloom,
  vignette, sharpening, film grain, chromatic aberration. Game gamma option `pow(cb0[50].w)` kept (HDR: relative
  to Paper White). Video `0x295C50DE` (CriWare UMG) with Ryse's Video Highlights.
- Shader inventory and the `r.CreateShadersOnLoad=1` trick for all permutations in the README.
- Open: trace passes after the tonemapper (SMAA `0xE462919A` registered as RTV clone writer, unverified), more
  permutations, DLSS later (reuse expeditionsrome_dlss).

## 2026-10-09 full dumps: Tales of Arise v2, Expeditions: Rome v18 (both untested in game)
- Dumps: `bin/cmd_Decompiler-1.3.16/dump_shaders_tales_of_arise/New_1` (12,846 new) and
  `dump_shaders_Expeditions_rome/all_shaders_` (15,735).
- Tales v2: 20 tonemapper permutations (one `tonemap.hlsli`, TOA_* defines; 10 add an animated noise overlay
  `t6`/`t7`), 5 CombineLUTs (`lutbuilder.hlsli`; lut01-04 blend 1-4 artist SDR LUT textures, weights cb0[38..42].x),
  4 opaque CriWare videos (shared `VideoOutput` in shared.h), FXAA/copies as RTV clone writers.
- Expeditions v18: 10 tonemapper permutations (`tonemap.hlsli`, EXR_*; ddx_fine sharpen, pre-exposure), same 5
  CombineLUTs (`lutbuilder.hlsli`, identical helper code to Tales), Film Grain + Chromatic Aberration sliders
  (Sharpening also scales the tonemapper sharpen).
- HDR SDR-LUT stage: UE Extended's adaptive D65 gamut compression around the lookup, max-channel division above 1
  (Ryse chart method), exact stock LUT within 0-1. No LUT scaling (keeps the artist's black level).
- Verified on D3D11 WARP vs stock CSOs (harness in the session scratchpad, not in the repo): tonemappers bit-exact
  in SDR, HDR within the dither; LUT builders SDR within 2.4e-7, identity LUT textures reproduce lut00.
- LUT Scaling slider (both games, Extra, 0-100%, default 0%): UE Extended's "Hue Preserving" black-raise
  compensation (CompensateHuePreservingLUTBlackRaise, half weight 1 stop, in gamma 2.2), inside the HDR SDR-LUT bridge.
  Their default is 100%; ours 0% (faithful). Unit test: LUT black 0.0039 -> 0.0020 at 50%, 0 at 100%; shadows from
  0.032 up unchanged. Tales v3 / Expeditions v19.
- All 3 UE Extended LUT controls (both games), own section "Color Grading LUTs" between PsychoV31 and Extra:
  LUT Strength (100%), LUT Scaling (0%), LUT Scaling Mode (Perceptual / Hue Preserving default / Color Restoring),
  ported from their filmiclutbuilder.hlsli with their default 2.2 gamma correction. Unit-tested (scratchpad
  scale_test): strength blends to the no-LUT color; all modes take a raised LUT black (0.0039) to 0. Tales v4 /
  Expeditions v20. LUT controls only act in scenes that use LUT textures (lut01-04).
- Tales v5 / Expeditions v21: LUT controls at UE Extended's defaults (Strength 100, Scaling 100, Hue Preserving);
  "HDR (Extended)" renamed "HDR (UE Extended)" (section "UE Extended"; keys unchanged), tooltips name RenoDX's UE
  Extended addon (marat569). lut01-04 set `lut_textures_seen` on draw; the Color Grading LUTs section shows whether
  the game drew one this session (ATN saw no slider effect in Expeditions; its pak index is AES-encrypted, so no
  static check).
- Expeditions DLSS: the full dump has 20 compute TAA permutations with the same contract (cb0[27], cb1[130], 8x8);
  the DLSS addon knows 6 (29C140B2, 201C3D3D, CCB4A156, D8C07EEE, 4B43383D, B266ECB6). The other 14 (0B7C5DC9,
  0BFD441F, 0C18907A, 1337487F, 2D45E8A9, 38C84ED8, E3370ED7, E91209D0, 43330B16, 71C87DBC, 7E46ACA4, 87970FD3,
  90BED7D1, BC7F1717; six with a second UAV) are unmapped: other quality/upsampling configs or DOF/SSR TAA. Only add
  after seeing one in game as the main TAA.
- Tales v6 / Expeditions v22: Game Shadows commented out in both (ATN: looked like SDR / Extended with desaturated
  colors). Expeditions: Film Grain, Chromatic Aberration and the 3 LUT sliders moved to "Extra (Probably not used)"
  (ATN 99.99% sure the game doesn't use them), LUT-in-use check removed. Tales keeps them (cutscene tonemapper uses
  grain / CA) and keeps the LUT-in-use line for testing.
- Tales v7 / Expeditions v23: ATN saw HDR (UE Extended) and PsychoV31 identical in Tales. Cause: our UE Extended
  only ran on UE's film curve (cb0[35].w != 0) and fell back to PsychoV31 on the legacy tonemapper; Tales most likely
  uses the legacy one. Now always the film curve like RenoDX UE Extended (UE's default film settings if slope 0).
  Expeditions (film) unchanged. Tales LUT sliders + LUT-in-use line moved to "Extra (experimental)".
- Tales v8: PsychoV31 "Highlight Hue Shift" (0-1, default 1): ATN saw pinkish fire. V31's own MeanA2 hue weights
  (1/0.5/0, our call) only shift hue where the shoulder compresses (fire x8 mid gray: unchanged even at full
  response weight), so instead: HueOKLab toward ToneCurve() (the game's SDR per-channel curve) above mid gray,
  smoothstep up to scene white 1.0. Test (film curve, fire 1/0.35/0.08): g/r at x8 0.35 -> 0.50 (SDR 0.66), x16
  0.38 -> 0.61 (SDR 0.80). Attila's equivalent: twattila psycho_test30 psycho30_HighlightHueShiftWeight.
- Tales v9: Highlight Hue Shift default 0.37 (ATN). UE Extended "Base Curve": 0 game's (default; on UE's legacy
  tonemapper, LegacyCurveExtended: FilmPostProcess with the highlight segment from Cm1 replaced by the midtone line,
  lerp 37.5% to ToneCurve, then UE Extended post grade + DisplayMap), 1 UE Filmic (marat's behavior). UE Extended
  addon has no legacy path (its lutbuilder SKILL: ignore bUseMobileTonemapper) and no hue correction at defaults
  (Hue Restore only with LMS scaling). Unit test: extended == stock below Cm1, linear above.
- Tales v10: ATN confirmed legacy (Base Curve "Game's": shadows up to ~paper white match SDR) but fire pink in
  Extended. ShiftHighlightHue() shared by PsychoV31 and UE Extended (applied to the tonemapped color before SDR LUTs /
  post grade), UE Extended slider ext_highlight_hue_shift default 0.37. Test (synthetic legacy curve, fire): g/r x8
  0.42 -> 0.45 (0.37) / 0.54 (1), x16 0.43 -> 0.49 / 0.77; SDR 0.61 / 0.82.
- Match SDR (experimental) toggle in PsychoV31 for Expeditions v24, Tales v11, Ryse fix30 (default off). Our V31 calls
  used contrast 1 / flare 0 (neutral); upstream fits them per game (Pragmata Match SDR: exponent 1.255, flare 0.93 ->
  0.1*0.93^10; Elite 1.55 / 0.85). FitPsychoToSDR(anchor_out) solves V31's shadow model n*c*(1 + w(x) f/(x+f)) at 2
  and 5 stops below mid gray from the game's curve (GameCurveY / FilmCurveY); flare bisection on [0, 0.075].
  UE default film: c 1.545, f 0.002, full V31 within ~0.1 stop of SDR at 1-5 stops (1,3-stop fit was off by 1.3 at
  5). Hable (U2 constants): c 0.98, f 0 (already matched).
- Match SDR reverted in all three (ATN: didn't like the look); Ryse files byte-identical to fix29 again (fix29 stays
  current). Tales v12: Highlight Hue Shift defaults PsychoV31 0.40, UE Extended 0.35 (slider moved to the end of the
  UE Extended section). Expeditions v25 = v23 + nothing else (Match SDR removed). Next: Tales DLSS.

## Tales of Arise DLAA (`src/games/talesofarise_dlss`) - v1 (2026-10-09, untested)
- Port of expeditionsrome_dlss + Ryse backend fixes (process-exit guard, every-mode preset hint), own NGX project ID
  1a688a59-20b4-48a1-9b9b-fd28bd910ec8, cache %TEMP%\RenoDX_TalesOfArise_DLSS. CMakeLists NGX condition extended.
- UE 4.21 TAA CS 0x88765764 / 0xE2AF861D: t0 depth (±2 diagonal dilation), t1 color, t2 history, t3 velocity, u0;
  View b1: ClipToPrevClip 114..117, TemporalAAJitter 118 (unconfirmed), ViewSizeAndInvSize 122 (self-checked).
  Pre-exposure 1, no camera-cut flag. Menu Off/DLAA only; DLSS modes wait for the TAAU mapping.
- Capture retargeted to 45 TAA-like CS + PS 0x57192B01 / 0xC3F0E4DA with per-target CB sizes, logs b1[110..123].

### Tales DLSS v2 (2026-10-09)
- v1 test: TAA `0x88765764` hooked, View checks passed (3840x2160), NVIDIA rejected the output: the game's TAA UAV is
  R11G11B10F (backend wants RGBA16F). v2: `direct` only for an RGBA16F target; otherwise NVIDIA writes private RGBA16F
  `runtime.output`, merge `0xA3450ADB` -> `runtime.merged` (target format) -> CopyResource, also in replace mode.
  Backend status now names the rejected texture (color/depth/motion/output). Zip `Downloads\talesofarise_dlss_v2.zip`.

### Tales DLSS v3 (2026-10-09)
- ATN: DLAA works (v2) but small particles smear (preset L). Captures: View TemporalAAJitter b1[118] is ±0 every frame
  (Box-Muller with Sigma 0 -> r.TemporalAAFilterSize 0 in Tales), TAA weights symmetric. v3: engine_scale Finder takes a
  `Variable` {name, help prefix, plausible range}; second finder for r.TemporalAAFilterSize, set to 1 while an AA mode
  is on, game value restored on Off (`camera_jitter`, config "CameraJitter"). Logged as "[Tales AA jitter] Found ...".
- Bandai's TO14 TAA (PostProcessTemporalAA2.usf) cannot upscale; 50% = TAA at 1920x1080 + Catmull-Rom 0x57192B01
  after tonemap; r.TemporalAA.Upsampling=1 no effect. DLSS upscaling not feasible without moving the post chain: DLAA
  only. Zip `Downloads\talesofarise_dlss_v3.zip`. Untested: confirm the found value is 0 and pattern reads 8.

### Tales DLSS v4 (2026-10-09)
- v3 log: r.TemporalAAFilterSize is 1 in Tales (guess wrong; v3 finder reverted, engine_scale.hpp back to v1).
- Real cause (disassembly of exe 0x1A866B0, UE 4.21 PreVisibilityFrameSetup jitter): `cmp [r14+0x1BE0],5; jne;
  mulss jitter by [r14+0x1290]` (AA method 5 = the game's TAA, factor 0). v4: `code_patch.hpp` (Ryse FindPattern /
  ApplyCodePatch), pattern JITTER_SCALE, byte +8 0x75 -> 0xEB while an AA mode is on; restored on Off / AddonUninit.
- Non-replace path now copies non-RGBA16F scene color (first frames failed "Temporal color format").
- ATN: motion blur smeared the DLSS indicator (expected: MB after TAA). Particles smeared without jitter.
  Zip `Downloads\talesofarise_dlss_v4.zip`. Untested.
- 2026-10-10: ATN confirmed v4 (ReShade4.log): jitter patch found and toggles cleanly, pattern 8, particles no longer
  smear, non-replace first frames now work. Only preset L tested (NVIDIA Profile Inspector forced L globally).

### Tales HDR v13 + DLSS v5 (2026-10-10)
- HDR: SDR Tonemapper option (`sdr_curve`, General, SDR only): Game's (legacy, bit-exact; harness ok <= 2.4e-7) or
  UE Filmic (stock film branch + its gamut expansion, as r.TonemapperFilm=1). `ToneCurve(ap1, film)` overload,
  `UseFilmCurve(hdr_lut)` in lutbuilder.hlsli; lut00-04 use it. Harness: si[32] = sdr_curve; the LUT-scaling
  dark-cell heuristic "FAIL" for lut01-04 also happens with v12's shaders (old heuristic, not a regression).
- DLSS v5: graphics presets in the DLSS addon (ATN: like Ryse, inside the DLSS addon). `engine_cvars.hpp`: console
  manager from the 1067 FindConsoleVariable sites (singleton exe+0x4B08670 in this build, slot 0x90), values via
  AsVariableInt/Float code (slots 7/8 lea rax,[rcx+off]); validated on r.ScreenPercentage. `code_patch.hpp` now has
  FindPatterns. 21 settings in GRAPHICS_SETTINGS, raise-only, live, restored on Game's own / unload; config
  "Tales Graphics" / "Preset". Arise-SDK recommended (ATN: tell users to get it from GitHub; ATN called it a must),
  our `Arise-SDK.ini` (UseUE4TAA false, jitter/sharpness -1). Untested in game: check the log's "[Tales graphics]"
  lines, which settings apply live, and that the jitter patch is still found with Arise-SDK loaded.
- Next (agreed order): DLSS upscaling via UE4 TAA (Arise-SDK's UseUE4TAA idea / its r.TemporalAA.Upsampling patch).
- 2026-10-10 DLSS v6: ATN loves Maximum. v5 log: 5 settings were FConsoleVariableRef (now supported: MainValue +
  exe global), ground fog scale is a divisor (8). Added 5 settings (26 total), logged AA cvars, Advanced checkbox
  "Unreal temporal upsampling" (r.TemporalAA.Upsampling live). Upscaling plan: Arise-SDK UseUE4TAA=true ->
  UE4 TAA (AA method 2) + TAAU -> capture at 50% -> map 4.21 TAAU CS + its cb -> DLSS modes. Zip talesofarise_dlss_v6.zip.
- 2026-10-10 log6 (ATN: Arise-SDK UseUE4TAA = true): jitter live (b1[118] varies) and TAA weights vary per frame ->
  AA method 2 active, but the TAA shader is still 0x88765764: it is UE 4.21's own TAA CS (Bandai's "TO14" mode 5
  runs it with zero jitter, cb0[51].x 0.6 vs 0). DLAA covers both. 50% capture: TAA at 1920x1080, no TAAU
  (2362 dispatches, no other target) -> upsampling not active (checkbox state unknown). v7: capture logs
  r.ScreenPercentage / r.TemporalAA.Upsampling / r.DefaultFeature.AntiAliasing at start and the checkbox toggles,
  and any other compute pass writing >=1280 wide from a smaller texture ("upscaling compute"); unknown getter bytes
  logged (r.ForceLOD still unsupported). Zip talesofarise_dlss_v7.zip.
- 2026-10-10 log7: r.TemporalAA.Upsampling 1 at 50% with UseUE4TAA true, still no TAAU. Cause: view ctor
  (exe+0x21B9A75) sets TemporalUpscale only if AA method == 2; SetupAntiAliasingMethod (exe+0x21B9FC0) starts
  with mov [rbx+0x1BE0],5 (Bandai), SDK switches later. v8: AA_METHOD_SETUP patch 5->2 with the capture checkbox.
  Presets: 6th "Beyond Maximum" (grass.densityScale / foliage.DensityScale 4); r.ForceLOD dropped (no registration
  in exe); Min/FadeResolution dropped, shadow distance <= 1.5 (ATN: pixelated/flickering shadows). Zip v8.
- 2026-10-10 log8: AA mode 2 patch active (jitter live), upsampling 1, still TAA 1080p + Catmull-Rom 0x57192B01.
  View ctor (exe+0x21B9A75): `cmp [rsi+rdi*4],0; je; cmp [rbx+0x1BE0],2; jne; mov [rbx+0x1BE4],1`; FSceneRenderer
  (exe+0x1A74193) keeps TemporalUpscale for mode 2 / SM5. Likely Arise-SDK's 0.1.10 upsampling patch rewrote the
  compare (not in its public source). v9: TEMPORAL_UPSCALE_CHECK jne -> nop while the capture checkbox is on, and
  logs the compared mode. Zip talesofarise_dlss_v9.zip.
- 2026-10-10 logs 9/10: with Arise-SDK the view's TAAU check compares mode 5 (SDK rewrote it), without it 2; with
  both our patches TAA still at render res + spatial upscale in both: the game's post-processing has only its own TAA
  routine (UE's TAAU path dead). ATN: drop DLSS, DLAA only. v10: upsampling patches, checkbox, capture
  (temporal_capture.hpp deleted) and logged cvars removed; NAME "DLAA + Graphics". Beyond Maximum: shadow maps
  16384, sun shadow distance 3, TexelsPerPixel 4, Min/FadeResolution 64/32 back (ATN wants more shadows). README
  rewritten. Zip talesofarise_dlss_v10.zip (temporal_capture.hpp gone: delete it from the repo copy if merging zips).
- 2026-10-10 v11: 18 more preset settings (45 total; r.ShadowQuality, spot/point texels, grass.CullDistanceScale,
  r.ForceHighestMipOnCharacterTextures, r.DetailMode, particle lights, motion blur quality, light shaft divisor,
  Bandai SSR / cloud / volumetric lighting / translucency divisors from their registration defaults). Blur divisors
  kept (look). Post sliders (% of game): r.MotionBlur.Scale, r.ScreenSpaceLightShaftDirectionScaleTO14. README:
  Arise-SDK not needed; DPI fix via Windows compatibility override. Zip talesofarise_dlss_v11.zip.
- 2026-10-10 HDR v14: Motion Blur slider (shader): 12 UE motion blur PS (`motionblur00-11`), cb0[28].y / .w times
  SI.motion_blur (shared.h last field, index 33); WARP harness mb_test.cpp: identical at 1 (diff 0), 0 removes blur.
  DLAA addon v12: post sliders removed (ATN: motion blur belongs in HDR, no light shafts). DoF: Bandai's own TO14,
  composite pass not identified (0x66DC643D / 0x2483CE26 / 0x773EABCB are sharpen/edge filters); needs a DevKit
  snapshot with DoF on. Zips talesofarise_v14.zip, talesofarise_dlss_v12.zip.
- 2026-10-10 DLAA addon v13: Beyond Maximum crashed (4 UE crash reports, %LOCALAPPDATA%\Arise\Saved\Crashes, all at
  exe+0x1A9E56C: shadow pass setup calls GetRenderTargetItem on a null pooled shadow target). 16384 shadow maps +
  2x4 border = 16392 > D3D11 limit. Beyond Maximum shadows now 12288. Zip talesofarise_dlss_v13.zip.
- 2026-10-10 DLAA addon v14: Beyond Maximum shadows 16000 (ATN; limit 16376 with the border). Zip v14.
- 2026-10-10 DLAA addon v15: still crashed at 16000 (same exe+0x1A9E56C). Most likely r.Shadow.MaxResolution sizing
  point light cube maps (6 faces, ~3 GB at 16000, over D3D11 per-resource limit). Split: light shadows (MaxResolution)
  max 8192, sun (MaxCSMResolution) 16000 in Beyond Maximum. Graphics UI like Ryse: preset buttons + one combo per
  setting ("Game's (value)" + distinct preset values) in 5 collapsing groups, saved per cvar name ("Tales Graphics");
  old "Preset" key migrates. Zip talesofarise_dlss_v15.zip.
- 2026-10-10 DLAA addon v16: v15 still crashed. Theory: shadow atlases sized from UE's cached scalability copy
  (not refreshed by live writes) while shadows use the live cvars; all crashing builds had TexelsPerPixel 4. Beyond
  Maximum = Maximum's shadows + the mod Engine.ini's stronger distances / density 10 / foliage tri cap; new options
  r.MaxAnisotropy 16, landscape LOD x3, r.Streaming.HLODStrategy 2. Risky values (sun 16000, texels 4, min/fade 64/32,
  grass cull 10, LOD 0.0001, density 4) only as extra dropdown choices (6th value). Choices saved in section
  "Tales Graphics Settings" (indices; v15's not read). 51 settings. Zip talesofarise_dlss_v16.zip.
- 2026-10-10 release cleanup. DLAA addon v17: no "experimental" (NAME "RenoDX Tales of Arise DLAA + Graphics", overlay
  "Tales of Arise DLAA + Graphics", metadata stable), user texts rewritten, "Stock TAA" -> "Game TAA", Simple mode
  shows render scale / jitter lines only on problems, risky shadow extras labelled "(can crash)", README rewritten
  (install + technical notes). HDR v15: LUT sliders + in-use check removed; LUT stage fixed at UE Extended defaults
  (Hue Preserving 100%, strength 100%); shared.h lost 3 fields (motion_blur now index 30). Harness: all LUT builders
  SDR exact, identity ok; motion blur ok. Zips talesofarise_v15.zip, talesofarise_dlss_v17.zip.
- 2026-10-10 DLAA addon v18: preset Recommended = Beyond Maximum with the game's r.VolumetricLightingBufferPixelNumPerCell / r.VolumetricFog.GridPixelSize / GridSizeZ (ATN: 80 -> 100 fps); PresetValue(). ATN DLAA timings at 4K, preset L: NVIDIA 4.03 ms, input prep 0.088, merge+copy 0.107. Zip v18.

### Next: Expeditions: Rome live graphics settings (ATN, 2026-10-10)
- Port talesofarise_dlss's engine_cvars.hpp / GRAPHICS_SETTINGS / presets + per-setting combos into
  expeditionsrome_dlss (UE 4.26). Verify IConsoleObject vtable slots for 4.26 first (bool variants may shift them).
  Tales is release-ready: HDR v15, DLAA + Graphics v18, Nexus text in Downloads\talesofarise_nexus.txt.
- 2026-10-10 DLAA addon v19: 'Loading' group, r.CreateShadersOnLoad (extra choice 1, in no preset) with tooltip; GraphicsSetting got an optional tooltip field. Zip v19.
- 2026-10-10 Expeditions DLSS: live graphics settings (zip Downloads\expeditionsrome_dlss_graphics_v1.zip, untested).
  New `engine_cvars.hpp` for this exe: FindConsoleVariable is slot 0xA0 with a bool (`41 B0 ??` before the call;
  1722 sites, one singleton); cvar vtables read offline: AsVariableInt/Float slots 9/10 (lea [rcx+0x38]), Ref
  GetInt/GetFloat 15/16 (MainValue 0x40, global ptr 0x38). 47 settings, presets Game/High/Ultra/Extreme/Maximum/
  Beyond Maximum (no Recommended yet: add one after ATN finds the expensive setting), section "Rome Graphics
  Settings". GRAPHICS_SETTINGS sized by std::to_array (a hand-written count of 49 for 47 rows would have left two
  empty rows). Offline tools: scratchpad exr\ (xref.py on the Expeditions exe, sites.py, vt3.py, cvars.py names.txt).
- 2026-10-10 Tales DLAA v20 + Expeditions graphics v2: crash extras removed in both (sun shadow 16000, texels per pixel
  4); the "(can crash)" label logic is gone (Min/FadeResolution extras stay, they never crashed). Expeditions:
  Recommended = Beyond Maximum with the game's r.VolumetricFog.GridPixelSize / GridSizeZ / HistoryMissSupersampleCount
  (ATN: volumetric fog is very expensive). Saved choices are indexes into the sorted choices: the removed values were
  the largest, so old saves clamp to 8192 / 3. Nexus text: "(can crash)" line removed. Zips talesofarise_dlss_v20.zip,
  expeditionsrome_dlss_graphics_v2.zip.
- 2026-10-10 Tales DLAA v21 + Expeditions graphics v3 (ATN: Expeditions shadows vanish while the camera moves).
  Causes found in both exes: (1) all sun shadow cascades share ONE CSM atlas of 1 << (GMaxTextureMipCount - 1) = 16384
  (Expeditions exe+0x139CBAE, Tales exe+0x1AD7041); a cascade FTextureLayout can't fit is not drawn. 8192 fits 4;
  presets had up to 10 (Tales' own value is 10: this was the "pixelated shadows at higher presets"). Now the written
  r.Shadow.MaxCSMResolution is capped to 16384 / ceil(sqrt(cascades)) (never below the game's), cascades row moved
  before it (static_assert). Expeditions presets: cascades 3/4/4/4/4 (+10), CSM 4096/4096/8192/8192/8192. Tales: CSM
  4096 in all presets. (2) Unreal's cached scalability copy (MaxResolution, MaxCSMResolution, ViewDistanceScale,
  StaticMeshLODDistanceScale, DetailMode, MaxAnisotropy, MaterialQualityLevel; ScalabilityCVarsSinkCallback,
  Expeditions exe+0x1FFF590; no direct readers of the shadow resolutions / draw distance) only refreshed when the game
  Set something. Console::Refresh() raises FConsoleManager::bCallAllConsoleVariableSinks (+0xCC; slot 8 Expeditions /
  7 Tales tests it each game-thread frame) after any write and on unload. Expeditions r.Shadow.DistanceScale clamped
  to 2 by the engine (Beyond 3 -> 2). Zips talesofarise_dlss_v21.zip, expeditionsrome_dlss_graphics_v3.zip.
- 2026-10-10 CORRECTION (Tales v22, Expeditions graphics v4): the CSM atlas is 8192, not 16384: GMaxTextureMipCount =
  min(CeilLog2(16384) + 1, MAX_TEXTURE_MIP_COUNT 14) = 14 (Expeditions exe+0xFD12C8). v3 (4 cascades at 8192) drew one
  cascade (ATN: shadows only on part of the screen, following the camera). Cap is now 8192 / ceil(sqrt(cascades)).
  Expeditions: cascades 3/4/4/4/4 (no extra), CSM 4096 in all presets. Tales: the game's 10 cascades only fit at its own
  2048, so the r.Shadow.MaxCSMResolution row and the cap code are removed (51 settings); this was the old "pixelated
  shadows" (8192 = one cascade) and the >8192 crash (no cascade fits). Nexus text: "sun" removed from the shadow line.
  ATN's old Expeditions Engine.ini (10 cascades @ 4096) only fits if the sun uses <= 4 cascades.
- 2026-10-10 Expeditions graphics v5: volumetric fog only cheaper or off (ATN): r.VolumetricFog 0 (new row), GridPixelSize
  12/16, GridSizeZ 96/64, HistoryMissSupersampleCount 2/1; EXACT, no preset values; GraphicsSetting::more_extras holds
  a second list-only value. Preset buttons (except Game's own) skip rows with no preset values. Recommended removed.
  Zip expeditionsrome_dlss_graphics_v5.zip.
- 2026-10-10 Tales DLAA v23 + Expeditions graphics v6: presets Game/High/Ultra/Extreme/Maximum (old Maximum column
  dropped, Beyond Maximum renamed; Tales keeps Recommended = Maximum with the game's 3 volumetric values). Tales: new
  r.VolumetricFog row (0 off), volumetric rows EXACT with cheaper list-only values (Bandai cell 16, fog cell 12/16,
  slices 96/64), GraphicsSetting::more_extras. Both: r.VolumetricFog 0 greys out r.VolumetricFog.* (BeginDisabled);
  Shaders on load EXACT 0/1 (engine default 0; ATN's Engine.ini had 1); preset buttons except Game's own skip rows in no
  preset. Tales: old "Tales Graphics"/"Preset" migration and PresetValue removed. Zips talesofarise_dlss_v23.zip,
  expeditionsrome_dlss_graphics_v6.zip.
- 2026-10-10 Ryse HDR fix31: execution skull hotkey is F5 only (the key above Tab is Ryse's command key; F8 is Print on
  ATN's keyboard). HUD F6 / Caps Lock and highlights F7 / Tab unchanged. Zip rysesonofrome_fix31.zip.
- 2026-10-10 Ryse HDR fix32 + DLSS v45 (debugForClaude14, untested): (1) HUD hotkey no longer hides the skull:
  0x1B59F534 left out of custom_shaders; OnDrawUi skips its non-indexed draws while the HUD is hidden, OnDrawIndexedUi
  skips indexed ones below the skull cut (HUD key) or above it (F5). (2) Extended: per-channel Neutwo shoulder above 1
  instead of the brightest-channel one (fire core was [1, 0.47, 0.25] salmon; PsychoV31 [1, 0.87, 0.77]); Hue
  Correction tooltip says it acts mostly on mid tones. (3) DLSS Detail distance also divides e_LodFaceAreaTargetSize
  (vase LOD switch at 2-3 m with e_LodRatio x256: face-area LOD). Zips rysesonofrome_fix32.zip, rysesonofrome_dlss_v45.zip.
- 2026-10-10 Ryse DLSS v46 (debugForClaude15, untested): light/shadow flicker above Very High. Shadow resolution max 4x
  (8x removed; e_ShadowsMaxTexRes cap 8192 = half the 16384 pool: it caps each light's pool map too). Light distance also
  divides e_ShadowsCastViewDistRatioLights (light casts shadows within [light+0x30] x ratio: +0x18C7E9, +0x250632).
  Recommended MAX now differs from MAXED OUT only in light distance (2x vs 8x). Zip rysesonofrome_dlss_v46.zip.
  ATN confirmed: Extended fire (fix32) good, LOD (v45) fixed.
- 2026-10-10 Ryse DLSS v47: v46 did NOT fix the light flicker (ATN). Added Advanced debug "Trace the shadow maps for 300
  frames" -> Bin64\ryse_shadow_trace.txt (cvar header: game -> requested -> kept; per frame each depth-only target with
  viewports + draws + used share). Next: read ATN's traces (MAXED OUT vs Very High, same spot, camera still).
- 2026-10-10 Ryse DLSS v48: shadow traces (debugForClaude16): pool maps stable in MAX and Very High (39 maps / 74% of
  16384; 38 / 66% of 8192) -> not pool overflow. e_ShadowsCastViewDistRatioLights refused (flags 0x800080) -> removed.
  Next: ATN bisects MAXED OUT with Light distance In-game (suspect: more lights than tiled deferred shading keeps).
- 2026-10-10 Ryse DLSS v49: flicker cause found with ATN's bisect + dump: Draw distance scaled e_ViewDistMin (10 ->
  2560), which is also every light's minimum view distance (CLightEntity::GetMaxViewDist +0x609D1: max(e_ViewDistMin
  [cvars+0x474], size x e_ViewDistRatioLights [+0xa0] x lod)) -> all lights active. e_ViewDistMin capped at 20.
  New Advanced debug "Measure every option's choices" -> ryse_options_measure.txt (scene/shadow draws + indices, frame
  ms per choice). Candidates not exposed: e_MergedMeshesLodRatio, e_TerrainLodRatio, e_VegetationSpritesMinDistance,
  e_CoverageBufferResolution, r_SilhouettePOM. Zip rysesonofrome_dlss_v49.zip.
- 2026-10-10 Ryse DLSS v50: Light distance option removed (ATN: anything above 1x flickers, even with e_ViewDistMin 20).
  Options measurement (debugForClaude17, game start outdoors): Detail distance and Draw distance saturate at 64x,
  Tessellation at 4x (frame time) -> capped there; Shadow res 4x costs 1.3 ms. Pool / cascades / shadow distance /
  particles / water / decals showed nothing in that scene (needs a scene with them). Recommended MAX == MAXED OUT now.
  quality_options 12. Zip rysesonofrome_dlss_v50.zip.
- 2026-10-10 Ryse DLSS v51: all multiplying options end at 64x with every power of two (ATN); Water 16x, shadow res /
  pool 4x unchanged; Tessellation back to 64x (ATN's call; measured flat from 4x in one scene). Options are labelled
  sliders (SliderInt, item name as format). ChoiceName() shared by the overlay and the measurement. Zip v51.
- 2026-10-10 Ryse DLSS v52: 5 new options (GrassDetail e_MergedMeshesLodRatio x, TerrainDetail e_TerrainLodRatio /,
  TreeModels e_VegetationSpritesMinDistance x, all to 64x; OcclusionCulling e_CoverageBufferResolution to 1024 = engine
  clamp; SilhouettePOM r_SilhouettePOM on). Only MAXED OUT uses them; test buttons set just these five to game / max.
  quality_options 17. Zip v52.
- 2026-10-10 Ryse DLSS v53: the 5 v52 options removed (measured: no change in draws/indices, frame time noise; ATN saw
  none). Presets: Reset / Very High / Ultra (16x, shadow 2x, pool 2x, 7 cascades, water 8x) / Extreme (32x, 4x, 4x, 8,
  water 16x) / MAXED OUT (64x); Recommended MAX removed (QualityOption ultra_choice / extreme_choice). Zip v53.
- 2026-10-10 Ryse HDR fix33: execution highlights = every PS whose bytecode contains "VisionMtlParams" (init_pipeline,
  highlight_shaders set, skipped in OnDrawUi / OnDrawIndexedUi while hidden); fix32 only had 0xD2063417 / 0xF495DD17,
  ATN found 0x6B40240A. ShaderCache.pak is CryEngine-packed (no plain DXBC), so offline enumeration was not possible.
  DLSS v54: presets Reset / Very High / Ultra (8x) / Extreme (16x, pool 4x) / Insane (32x) / MAXED OUT (64x).
- 2026-10-11 Tales DLAA v24 + Expeditions graphics v7: graphics settings are labelled sliders (SliderInt), steps added
  between choices at AddonInit (whole numbers across gaps <= 4 below 10; 1-2-5 numbers across gaps > 2x), choices saved
  by value (SaveChoice; sections "Tales Graphics Values" / "Rome Graphics Values"; "game" = game's). Expeditions HDR v26:
  LUT sliders removed (shared.h lost lut_strength / lut_scaling / lut_scaling_mode, the last fields), lutbuilder's LUT
  stage = Tales v15's fixed defaults. Zips talesofarise_dlss_v24, expeditionsrome_dlss_graphics_v7, expeditionsrome_v26.
- 2026-10-11 Expeditions graphics v8: group "Optional (changes the look)", in no preset, EXACT: AO max quality -100, AO on
  static indirect, DF shadows, DF AO, contact shadows, capsule shadows, lens flare 3, SSR max roughness 1 (ATN's old
  Engine.ini lines). Zip expeditionsrome_dlss_graphics_v8.zip.
- 2026-10-11 Expeditions graphics v9: preset "Beyond MAX (+different AO)" = Maximum + OPTIONAL_GROUP extras (AO max
  quality -100, AO on static indirect = DFAO on baked light, lens flare 3); r.SSR.MaxRoughness 1 in Maximum; DF
  shadows / DFAO / contact / capsule rows removed (game already 1). Tales v25 + Expeditions v9: a short tooltip on every
  setting. Zips talesofarise_dlss_v25, expeditionsrome_dlss_graphics_v9.
- 2026-10-11 Expeditions graphics v10: r.AOApplyToStaticIndirect row removed; preset renamed "Beyond MAX" (Maximum +
  AO max quality -100 + lens flare 3). Zip expeditionsrome_dlss_graphics_v10.zip.
- 2026-10-11 Expeditions graphics v11 + Tales v26: presets set every row (KEEP -> game's) except PLAYER_SETTINGS (fog,
  shaders on load); Expeditions presets renamed Insane (old Maximum) / Max (old Beyond MAX, every row's best choice by
  Better: HIGHER max, LOWER min, EXACT Insane's or the optional extra), all on one line. Verified by simulation.
- 2026-10-11 Expeditions graphics v12 + Tales v27: r.SSR.MaxRoughness in Extreme/Insane/Max; presets (Game's own too)
  set every row but r.CreateShadersOnLoad (NO_PRESET_SETTING); Expeditions Max keeps the game's volumetric fog (EXACT:
  Insane's, else optional extra, else game's). Ryse DLSS: buttons set all 12 options (no list-only items): no leftovers.
- 2026-10-12 Tales HDR v16: Highlight Hue Shift defaults: PsychoV31 0.24, UE Extended legacy curve 0.17, new
  ext_filmic_highlight_hue_shift 0.30 for the UE Filmic base curve (shared.h last field; lutbuilder picks by
  legacy_curve); a TEXT note above each slider; Recommended adds Motion Blur 0 and Film Grain 0. DLAA v28: Maximum /
  Recommended object detail 0.0001 + grass 10; sun shadow distance up to 4 (1.25 ... 4 in halves); Recommended renamed.
  ATN: sun shadow distance 3 fixes light through mountains but shimmers: same 10 x 2048 cascades over 2x the ground.
- 2026-10-12 Tales DLAA v29: sun shadow layout choice: cascades EXACT 4-9 + r.Shadow.MaxCSMResolution row back (2730 /
  4096), capped to 8192 / ceil(sqrt(cascades)) (SUN_SHADOW_* + SettingIndex, as Expeditions); both in no preset (KEEP).
  Sun shadow distance 2 at Maximum / Recommended (>2 needs Arise-SDK; list 1.25 ... 4). ATN: shimmer comes from the
  sun moving every ~10 s; sharper maps (fewer cascades) shimmer less.
- 2026-10-12 Tales DLAA v30: v29 sun layout reverted (game's r.Shadow.MaxCSMResolution is 4096 per ATN -> its sun uses
  <= 4 cascades; nothing higher fits). Test row r.Shadow.FilterMethod 1 (PCSS, in no preset) against sun-move shimmer.
  HDR v17: "Video Highlights" label -> "Video AutoHDR" (key unchanged). Zips talesofarise_dlss_v30, talesofarise_v17.
- 2026-10-12 Tales DLAA v31: Sun shadow resolution row back (6144 / 8192, no preset) with a code patch doubling the CSM
  atlas to 16384 (SUN_SHADOW_ATLAS: `mov r13d, 1` -> 2 before `shl r13d, cl`, exe+0x1AD705A; applied only while the
  row writes above the game's 4096; unusable if not found). 16384 = the D3D11 shadow-map limit the game sets
  (exe+0x179F800 -> GMaxShadowDepthBufferSize). Untested: does it stop the sun-move shimmer at distance 2?
- 2026-10-12 Tales DLAA v32: ATN confirmed the higher sun resolution fixes the shimmer. Sun shadow resolution in presets
  (High / Ultra 6144, Extreme / Maximum / Recommended 8192); PCSS row removed (no effect). Zip talesofarise_dlss_v32.
- 2026-10-12 Tales DLAA v33 (untested): row Arise.PF.Culling.ForceInFrustum 1 (Extreme+): Bandai's PFCullingManager
  hides off-frustum characters, which fade in + rebuild on a fast turn (fps 120 -> 80 for 1-2 s); r.AllowOcclusionQueries
  0 and CharaDisableCull did not help. Zip talesofarise_dlss_v33.
- 2026-10-12 Tales DLAA v33 confirmed by ATN (characters stay). v34 (untested in game): Arise-SDK's [Graphics] options
  built in (new sdk_options.hpp; details in the README's v34 note): NPC distance, monster distance, NPC/monster LOD
  distance, never hide characters, outline distance (Detours hooks + LOD memcpy stub + NPC fade immediates, installed
  in AddonInit), sun distance clamp NOPed at load, a.URO.Enable row; new "Characters" group. All patterns, function
  starts and offsets verified offline on ATN's exe (scratchpad toa/sdk_sigs.py). With Arise-SDK installed its hooks
  win (rows bind sdk.* cvars or show unavailable); the bundled Arise-SDK.ini now leaves these to the addon. Zip
  talesofarise_dlss_v34.
- 2026-10-12 Tales DLAA v35 (untested in game): Arise-SDK's cutscene fixes built in (Cutscenes group: skit resolution
  via a stub on execCreateRenderTarget2D's UpdateResource call + scene capture AA flags; smooth cutscenes via the
  sequence vtables' PostLoad slot). Arise-SDK now only adds skipping the intro logos. Arise-SDK is GPL-3.0: nothing
  of its code is used, only its findings (functions, byte patterns, formulas); credited. Zip talesofarise_dlss_v35.
- 2026-10-12 Tales DLAA v36: sdk_options.hpp -> engine_hooks.hpp; every byte pattern chosen anew from ATN's exe (all
  unique, hook starts verified; Arise-SDK's alternates dropped), own formulas (detail distance: all LOD distances
  multiplied; NPC distance: every NPC distance scaled by distance / 3100), keys Tales.*, no credit in code / README
  (ATN: Nexus only, his wording: idea from Arise-SDK's ini, AI found the patterns, no Arise-SDK code). Zip v36.
