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
  v42: the post pass redrawn at the display size gets PS_ScreenSize = display size (copy of its b0, CopySubresourceRegion1
  + UpdateSubresource1 on one register): CA / sharpening / lens flare shift were display / render too strong (ATN, 14%).
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
