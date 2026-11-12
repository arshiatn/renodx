# RenoDX — Ryse: Son of Rome (CryEngine 3, DX11)

The game has no HDR option; RenoDX outputs HDR10/scRGB through a swap chain proxy.

## Rendering modes

| Mode | What it does |
|---|---|
| SDR | The game's native tonemapper (Hable / Uncharted 2 filmic) and color chart, scaled by Paper White. |
| HDR (Extended) | Default. UC2 Extended (Musa/Jon) on the game's Hable: Hable up to its knee (third derivative root), its tangent above it, per channel (Hue Correction, fix29-41, removed in fix42: it moved hues by 0-3 degrees, the knee is low and the tangent above it keeps the channels' ratios); above 1 each channel rolls off to Peak on its own (Neutwo, fix32), as SDR clips each channel, so bright fire gets a yellow / white core. Until fix31 the brightest channel rolled off and the color kept its saturation: bright fire stayed a flat salmon orange ([1, 0.47, 0.25] at its core; PsychoV31 [1, 0.87, 0.77], SDR white). No "colors from SDR" since fix28 (below). |
| HDR (PsychoV31) | Recommended preset (with Film Grain 0, Motion Blur (Camera) 0, Motion Blur (Objects) 1, Video Highlights 0.25). PsychoV31 replaces Hable, anchored on Hable's mid gray. Exposure, bloom, saturation / color balance, sun shafts and the color chart stay. Falls back to target-only, then Extended, where V31 returns black. |
| HDR (PsychoV35) | Recommended (2026-11-12). Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`), anchored on Hable's mid gray; Hue Clip Emulation (default 80) toward Hable on each channel, clipped; V31's highlight and fire hue shifts not used. Recommended (with Deeper Shadows): V35 Shadows 0.75. Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain (Hable on each channel). Built only, not run in game. |

**Removed in fix28: "colors from SDR" (Extended).** Until fix27, Extended pulled its color toward SDR's:
`MatchSDRColor` scaled the SDR color (Hable, saturated) to the HDR luminance and kept the HDR color only for
the light HDR adds (`hdr_share = max(1 - y_sdr / y_hdr, the share that keeps the scaled SDR color below
Peak)`), then `renodx::color::correct::HueOKLab(color, sdr_display, y_sdr_display / y_color)` matched SDR's
displayed hue (after shafts, chart and the 2.2 emulation) for SDR's share. Why removed: SDR clips each channel
at white, so bright fire turns yellow / white there; pulling HDR fire toward that, with the mix changing fast
across a flame, looked broken (tester; PsychoV31's fire was fine). The same step was in Expeditions: Rome's old
Extended (also removed). To bring it back: restore those two steps in `tonemap00` (`ApplyExtended(untonemapped,
sdr, peak)` with `sdr = saturate(FilmCurve(untonemapped))`, both in the Extended branch and the PsychoV31
fallback), as in the fix27 zip.

```hlsl
float3 MatchSDRColor(float3 sdr, float3 hdr, float peak) {
  float y_sdr = renodx::color::y::from::BT709(sdr);
  float y_hdr = renodx::color::y::from::BT709(hdr);
  if (!(y_sdr > 0.f && y_hdr > 0.f)) return hdr;
  if (y_hdr <= y_sdr) return sdr;
  float3 scaled = sdr * (y_hdr / y_sdr);
  float3 need = max(scaled - max(peak, hdr), 0.f) / max(scaled - hdr, 1e-10f);
  float hdr_share = max(saturate(1.f - y_sdr / y_hdr), saturate(max(need.r, max(need.g, need.b))));
  return lerp(scaled, hdr, hdr_share);
}
// Extended branch, after GammaSafe(ApplyShaftsAndChart(ApplyExtended(untonemapped, sdr, peak), shafts)):
float3 sdr_display = renodx::color::correct::GammaSafe(ApplyShaftsAndChart(sdr, shafts));
float y_color = renodx::color::y::from::BT709(color);
color = renodx::color::correct::HueOKLab(
    color, sdr_display, (y_color > 0.f) ? saturate(renodx::color::y::from::BT709(sdr_display) / y_color) : 1.f);
```

HDR: sun shafts and lens flares use the stock screen blend (base + s x (1 - base), encoded) with
(1 - base) replaced past 0.5 by a C1 exponential tail that never reaches 0 (`RyseScreen`). The stock
saturate made a flat plateau at white and a crease where the HDR image passes 1: near the sun the
HDR bloom showed as a half circle with colored fringes (R, G, B crossing 1 at different radii; gone
with bloom off). Shaft values past 1 are saturated as stock (unclamped they pushed edges in front
of the sun to Peak). The 16³ color chart is applied as stock in the SDR range; colors past 1 are
normalized by their max channel, graded and scaled back. sRGB-on-2.2 display emulation, gamma 2.2
encoded.

HDR effects:
- Sun shaft highlights (always on in both HDR modes since fix24; a toggle in fix21-23): the stock saturates the shaft value; near
  the sun it passes 1. The luminance of the part past 1 is added in the pixel's own (SDR's) hue,
  rolled off (Neutwo) into the headroom left below Peak (`RyseAddLight`). fix10 added it per channel:
  the excess was red only -> orange sun area. Sun Shafts HDR toggle until fix13, always on fix14-20.
- Upgrade aspect tolerance 0.03: the game rounds its reduced targets down (2960x1665 -> 1480x832,
  740x416); with an exact match those stayed 8-bit at DLSS sizes like 77% (2960x1665) and 58%
  (2224x1251), so the sun shafts' quarter target clipped at white (fix20 test, debugForClaude11).
- Lens flares HDR was tried (fix10/11) and removed: each flare sprite (AC3155D8, BE5D77E6, 92DFB7F1,
  3468E47B) goes through its own filmic curve x(ax + b) / (x(ax + 1.7) + c) and is saturated; no
  visible change in game.
- Sun Glow (fix36, Extra slider) was tried and removed (fix37, ATN: doesn't look good). ATN saw the sun stay at
  203-250 nits whatever Peak / Exposure / Highlights (chapter 3, debugForClaude37): the sky dome's sun (0x74F62697, 18)
  is under an opaque sky texture (0x1C896707, about 0.2) and the shafts' mask is full around the sun, so the screen
  blend pins it near white. It added, as light, what the blend cuts off past white (decode(max(base + s, 1)) -
  decode(max(screen, 1))).
- Video variants 0x0ABA0C5A (YUV) / 0x67F12AF3 (YUVA) are Scaleform's multiply mode (lerp(1, video, alpha), then
  multiplied with the screen): a factor, not a color, so stock (no Paper White gain, no ITM).
- Film Grain HDR was tried (fix10-12) and removed (not faithful: it moved the overlay's white to Peak).
  Grain is the stock overlay up to white.
- Video Highlights (slider, default 0): inverse tone mapping (ITM) of the 8-bit SDR videos; luminance
  above mid gray stretched so video white reaches lerp(1, Peak, amount). Video Dithering (On, only with
  Video Highlights above 0): 1 code value of interleaved gradient noise before the stretch.
- Sharpening defaults to 0 (the game's own = 1).
- Chromatic Aberration (default 0, 0-2; 1 = amount 12, 2x the game's r_FlaresChromaShift 6, key
  chromatic_aberration_strength): the game's full-screen one, rebuilt in the post pass (after
  TAA / DLSS). The game's CA_Sharpening pass (0xC53CE326, PostAA, psParams.x = r_ChromaticAberration,
  run only above 0; the game ships 0): red sampled at (uv - 0.5) x (1 + 2 x amount x PS_ScreenSize.zw)
  + 0.5, blue at (1 - ...), green in place. The lens flare red shift (vLensOpticsParams.w =
  r_FlaresChromaShift, 6) is stock.
- UI gain: the Paper White-UI gain (RYSE_ENCODED_SCALE) is applied at the post pass's output, not at the
  tonemap: the TAA / DLSS history then does not change while the UI slider moves (objects flickered
  brighter/darker while dragging it). The tonemap, SMAA and TAA run at Paper White = 1; the upscale
  reads the scaled post output.

Config files: the game has a cvar whitelist (code at +0x1594B40..+0x15967AA, which also prints the
"[Warning] Unknown command: %s" for everything else). Allowed: the 14 sys_spec_* groups (ObjectDetail,
Shading, VolumetricEffects, Shadows, Texture, Physics, Animation, PostProcessing, Particles, Sound,
Water, GameEffects, Light, TextureResolution; 4 = very high, the PC maximum; all accepted in game),
r_MotionBlur, r_PostAASharpening, r_VSync, r_MultiGPU(_TemporalAAMode), r_FullscreenWindow,
r_FullscreenPreemption, r_NumBackbuffers, r_overrideDXGIOutput/Adapter, cl_fov, cl_sensitivity,
cl_bobHud, sys_MaxFps, sound volumes and a few commands. The other cvars (e_ShadowsMaxTexRes default
1024, r_TexMaxAnisotropy default 8, e_LodRatio, e_ViewDistRatio, r_SSReflections, ...) are registered
but only settable from code (IConsole +0xB0 GetCVar; ICVar +0x10 GetIVal, +0x20 GetFVal, +0x28
GetString, +0x38 Set(int), +0x40 Set(float)).

## Shaders

| File | Pass |
|---|---|
| `tonemap00_0xB9B672AA` | HDRFinalPass: auto exposure, vignette, bloom, color balance, Hable, sun shafts, color chart. |
| `smaa00_0xF2D692D1` | SMAA neighborhood blending, signed (wide-gamut negatives pass). |
| `taa00_0x121C45E4` | TAA (SMAA 1TX style). |
| `post00_0xD0B247FF`, `post01_0xB022AFEA`, `post02_0xC3A469C1`, `post03_0xCC6DF78E` | Film grain, with or without sharpening and lens flares (red-channel shift). `post.hlsli`. Texel offsets (sharpening taps, lens flare shift, chromatic aberration) from t0's size, not PS_ScreenSize: the DLSS addon draws this pass again at the display size with the render size there (7x too strong at 14%, fix25). |
| `upscale00_0xCD8548F6` | Native upscaling (Rendering Resolution below 100%). |
| `mb00_0x799A5EB8` | Motion blur velocity pack (its own target; the TAA's velocity is untouched). Camera part (static scenery at that depth) and object part (object velocity minus it) scaled apart, before the stock length limit. |
| `dof00_0x910E0E0C` | Depth of field composite (before the tonemap). Depth of Field slider. |
| `shafts00_0x4DFAE399`, `shafts01_0xF1FCED4E` | Sun shafts mask (sky depth x 1.32 x (r + g + b)) and radial blur (run twice). Stock, saturated: their quarter-size RGBA8 targets clamped to 0-1; upgraded to FP16 (back buffer aspect) the mask passed 1 far and the shafts were about 4x larger and blinding, also in SDR (ATN, fix22). |
| `video00_0x37ABF6CF`, `video01_0xC1F889A4` | Scaleform YUV / YUVA videos (8-bit planes, BT.601 limited), shown at Paper White; Video Highlights. |

RGBA8 / BGRA8 / RGB10A2 render targets at back-buffer aspect are upgraded to FP16 at creation
(no hot swap): CryEngine copies between them and the back buffer (CopyResource), which needs
the same format on both sides. The game reads the post-tonemap frame through UNORM views
(SMAA, sharpening: encoded) and sRGB views (TAA, upscaling: linear). FP16 views have no
hardware sRGB decode, so the TAA and the upscaler decode by hand, filtering after decoding
like an sRGB view. Other stock passes that used sRGB views now see encoded values.

The game renders into a persistent FP16 clone of the back buffer
(`swapchain_proxy_compatibility_mode = false`): menus draw over the last frame, and an in-place
scRGB conversion at Present was applied again every frame (black or posterized menu, overlay trails).
Main menu black (windowed), frozen after alt-tab (fullscreen): RenoDX makes the swap chain flip
model, where Present unbinds the back buffer from the pipeline (the game's blt model kept it bound).
The menu binds its back buffer once and keeps drawing, so after the first Present it drew into
nothing; an alt-tab rebinds it for one frame. Gameplay rebinds every frame. At the first draw after
each Present, if the game's last binding was the back buffer and nothing is bound, it is bound again;
back buffer binds (current or older, kept after a resize) go to RenoDX's FP16 clone of the current
one, which the proxy presents. Logged as `[Ryse] back buffer unbound by Present ...`.
Not yet seen in game.

## Hotkeys (fix27)

Polled at Present (GetAsyncKeyState) while the game window is in front; not saved, all shown again at
the next start.

- F6 or Caps Lock: HUD. The draws of Scaleform's text (`0x96675B77`) and flat shapes (`0x1B59F534`, in
  OnDrawUi / OnDrawIndexedUi) are skipped, so the menus are hidden too; since fix32 not the execution skull (the
  largest flat shape), which only F5 hides.
- F7 or Tab: execution highlights on enemies (yellow / blue). Since fix33 every pixel shader with `VisionMtlParams`
  in its constant buffers, found when the game creates it (OnInitPipelineHighlights, logged as "[Ryse UI] Execution
  highlight shader"): seen `0xD2063417` (body), `0xF495DD17` (head), `0x6B40240A` (fix32 missed it); the shader
  cache's permutations are not readable offline (CryEngine's packed format), so the name decides.
- F5: the execution skull above enemies (not the key above Tab: the game's command key; not F8: Print on some
  keyboards). It is the largest
  `0x1B59F534` draw: 3744 indices far to 4401-4539 near at 4K, every other HUD shape at most 3219.
  Draws from 3500 indices are skipped, the cut scaled by (back buffer height / 2160)^0.23 (the skull's
  growth with size; untested below 4K). Found with the F9 logger (`RYSE_UI_DRAW_LOG`, off).

## Verification

No shader compiler was available, so the HLSL was translated to C++ (HLSL-like vector
shim) and compared against the translated 3Dmigoto originals on random inputs:
tonemap SDR, sharpening/grain/lens SDR, SMAA blend, videos: bit-identical. TAA and upscale
vs the originals with emulated sRGB views: max difference 1.9e-6 / 0. Extended: monotonic,
never darker than SDR, no NaN/Inf (Neutwo and V31 were stand-ins there). The addon passed a
g++ syntax check against stub headers. In game (Oct 4 build): gameplay correct in HDR; the
main menu was black/posterized (fixed above, not yet seen in game).

## Known limits

- One tonemapper permutation in 540 dumped shaders; CryEngine may still compile others.
- Untested: UI gamma (Scaleform), supersampling and Native Upscaling off paths, other AA
  permutations, the color chart behavior past white in HDR, stock passes that read sRGB views.
- DevKit together with this add-on hangs at start unless this add-on loads first (file name
  sorted before the DevKit's, e.g. `0renodx-rysesonofrome.addon64`, `1renodx-devkit.addon64`).
- Rendering Resolution 50% + Supersampling 1.5x: black scene with an engine error
  ('$StereoL' render target vs depth surface size). Same without the add-on (game bug).
- `0x437BA5AF` (4-layer blend by a per-pixel amount, maybe DOF) is not replaced: not seen in a frame yet.
- Extra sliders: Bloom, Vignette, Sun Shafts, Lens Flares, Chromatic Aberration, Film Grain,
  Sharpening (default 0), Motion Blur (Camera), Motion Blur (Objects), Depth of Field, Video Highlights
  (+ Video Dithering) (HDR only).
fix34: both Recommended presets set Bloom to 0.5 (ATN).
fix38: less CPU. Back buffer binds: the game's every render target bind compared its target with the back buffers under
a mutex; now lock-free copies (back_buffer_fix::back_buffers, at most 16, the oldest earlier ones dropped; the
immediate context and its last binding atomic): the mutex only when the back buffer is bound. A new resource checks the
same copies before locking. The HUD / execution skull / highlight draw events are registered only while one of them is
hidden (F5 / F6 / F7, Caps Lock, Tab), else every draw went through them. Debug logging (DEBUG_LEVEL_0) off.
fix39: PsychoV31 sliders as Tales / Attila: Highlight / Shadow Contrast, Flare, Dechroma, Gamut Compression + Mode, and
under "Fire too pink, too orange or too pale?" Highlight Saturation (1), Highlight Hue Shift (0), Highlight Saturation
Shift (0) toward the game's SDR (Hable, clipped). Output SDR + Auto (default), Video AutoHDR tooltip as Attila's,
About: credits, Ko-fi and NexusMods (nexusmods.com/rysesonofrome/mods/59), section titles coloured. psycho_test31.hlsli:
ShiftSaturationToward / FitToTargetGamut (ATN's additions, as Attila's copy).

fix40: PsychoV31 Highlight Hue Shift default 0.08 (ATN; Reset and the presets too).

fix41: fire controls (ATN: some fires too yellow, some pink; dim fires did not react to the three fire sliders: V31's
Highlight Saturation / Dechroma reach full strength only ~2.75 decades above mid gray, and the Highlight Hue Shift's
target, Hable's hue, equals ours below SDR white). Dechroma moved under "Fire too pink, too yellow or too pale?". New
there: Fire Hue (-1 to 1, 0 off: warm colors turned up to 30 degrees, redder / more yellow) and Fire Hue Focus (0 to 1:
pink and yellow pulled toward orange, OkLCh 55 degrees); both on the screen's colors after the game's color chart
(AdjustFireHue in tonemap00), warm hues within 70 degrees of orange (none past 100), from mid gray (full from Paper
White), saturated colors only; PsychoV31 only. PsychoV31 Last (experimental, ATN's theory): the sun shafts and the color
chart on the untonemapped scene (colors past 1 normalized as for the chart), PsychoV31 after them; off = as before.

fix42: Extended's Hue Correction removed (ATN: no visible effect): the per-channel UC2 Extended moves hues by 0-3 degrees
(standard Hable constants: skin +1.7, dim fire +2.0, fire at 1.2 +2.7, mid orange -2.5), its knee is low (0.40) and
the tangent above it keeps the channels' ratios; bright fire's yellow core comes from the per-channel shoulder above 1,
which ran after it anyway. Setting, field and the About line's "Hue Correction as in Musa's Batman" gone; the result is
Hue Correction 0 (clamped at 0 as before).

fix43 (ATN): PsychoV31 Last removed. Game Shadows (experimental) under Gamut Compression Mode. The fire line yellow, with
two buttons: Default (Dechroma 0.41, Highlight Saturation 1, Highlight Hue / Saturation Shift 0, Fire Hue -0.05, Fire
Hue Focus 0.75: also the sliders' defaults, Reset and both Recommended presets) and Less Red (Fire Hue 0.05, Focus
0.70). Extended has its own Fire Hue / Fire Hue Focus (ext_fire_hue / ext_fire_hue_focus, default off) under a yellow
fire line. Both Recommended presets keep Bloom at 1. Section titles in one colour (Attila's green), Output and About
their own.

fix44 (ATN): fire buttons Option 1 (Default: Highlight Hue Shift 0.03, Highlight Saturation Shift 0.10, Fire Hue 0,
Fire Hue Focus 0.50, Dechroma 0, Highlight Saturation 1; the sliders' defaults, Reset and the Recommended presets) and
Option 2 (0.11, 0.07, -0.05, 0.75). Extended (its fire also yellow / green / pink against SDR's): its own Highlight Hue
Shift / Highlight Saturation Shift (ShiftHighlightHue toward Hable clipped, on Extended's result) with Fire Hue / Focus
and the same two buttons and defaults. Recommended (Less Post Processing): Sharpening 0.25; new Recommended (Less Post
Processing, Vanilla-like Shadows): the same with PsychoV31 Flare 0.41.

fix45 (ATN): presets renamed Recommended (was Less Post Processing) and Recommended (Vanilla-like Shadows): Shadows 0.75,
Flare 0.25; Depth of Field 2 in all three Recommended presets (Reset keeps 1).

fix46 (ATN): Recommended (Vanilla-like Shadows) renamed Recommended (with Deeper Shadows).

fix47 (ATN): Fire Hue / Fire Hue Focus labelled Fire Hue (OkLCh) / Fire Hue Focus (OkLCh) (AdjustFireHue turns the
OkLCh hue; Highlight Hue / Saturation Shift are OkLab too but keep their names, as in the other games).

fix48 (ATN's batch, untested in game): **Ambient Occlusion** (Game's SSDO / GT-VBAO, AO Radius / Strength / Thickness /
Quality / Directions; default Game's): the SSDO pass (ssdo00 0xD1106E8B, half resolution; decompiled from the DevKit
dump, bit-identical to the game's on WARP with GT-VBAO off) runs the visibility bitmask (ssdo_bitmask.hlsli on
twattila/bitmask.hlsli) on its own linear depth and normals in its camera space, and hands the game what its SSDO
does: the direction toward what covers the pixel (world space, through SSDO_CameraMatrixInv) and the amount, in the
units the tiled lighting (cs 0x897232B5, t5) turns into ambient (1 - 0.35 x amount x SSDOParams.z, at least 0.3), light
and reflection occlusion; the terrain's large-scale LSDO is added as the game's. Positions from depth texel centres,
off-screen samples skipped. WARP (tools\ryse\warp_ssdo): open floor 0.03, the floor in front of a wall 0.19 -> 0.05
over a metre, pointing at the wall; the wall's foot pointing at the floor. **Contact Shadows** (0-1, Length, Thickness;
default off; contact_shadows.hpp): before the tiled lighting, the sun's slice and channels from its light list (t32,
lightType 8: 0x52C5E001), then a full-screen march toward the sun through its linear depth (0x52C5E002 / 0x52C5E003,
its own camera constants: far-plane corners) max-blended into the shadow mask's slice 0 (1 = shadow), in the addon's
own context state. WARP (tools\ryse\warp_contact): the floor beside a 0.5 m pole in shadow, farther floor, the other
channels and slice 1 untouched. Not yet: SSR (the tiled lighting reads the game's SSR at t6: a hierarchical march could replace it).

fix49 (ATN's batch, untested in game): **Light Bounce** (GT-VBGI; Light Bounce 0-2, default 0 = off, Radius, Quality,
Directions, Resolution, Multi-Bounce, Denoiser; light_bounce.hpp): after the tiled lighting (on_drawn of cs
0x897232B5), the Total War games' Light Bounce passes (twattila/light_bounce_*.hlsli: prepare + levels, visibility
bitmask trace, temporal resolve, edge-aware denoiser, apply) on Ryse's G-buffer (light_bounce.hlsli: positions from
PER_BATCH's far-plane corners x the linear depth t0, world normals t1 into a view space of the frustum's axes, albedo t3
squared, the last frame's PER_BATCH for the history's reprojection), the lit HDR scene (u1, copied) as the light, the
bounce x albedo added to u1 through a render target view, in the addon's own context state. AO Thickness is the
occluders' thickness. GPU test (tools\ryse\light_bounce_test.cpp: floor + red-lit wall, 30 frames): the floor at the
wall's foot 0.10 -> 0.49 red (green 0.11), near the camera 0.101, the sky untouched, the game's compute bindings back.

fix50 (ATN's batch 2, tested in game: Ryse's first combat, DLSS + FG): **Sun Shadow Softness** (Contact Shadows
section, 0-5 degrees, default 0 = the game's filter): contact-hardening sun shadows (PCSS,
twshogun2completeedition/shaders/pcss.hlsli, now with a Texture2D version) in the sun shadow mask's cascade passes
(sunshadow00-04: 0xE9264ADE, 0xB5DC6E36, 0xF2B4EF30, 0xF864A048, 0xF81C7168; CryEngine draws each cascade into the
mask with a rotated 12-tap kernel, found with the one-frame trace: the five cascade draws before the tiled lighting,
the far cached 16384 map 0x10C1DA12 keeps the game's filter). The blockers' depth around the receiver, the penumbra
the sun's size makes across the gap, then a filter that wide (1-32 texels). The cascade's world scale comes from its
screen-to-shadow basis (sun_shadow_pcss.hlsli: CryEngine's vWBasisX / Y / Z are the screen's pixel steps and corner ray
in the cascade's space; the orthographic map's u and v rows are orthogonal with equal length in camera space, which
gives the field of view's tangent, then texels per unit of depth), so every cascade gets the same world-size penumbra.
In game at 3 degrees (an overcast scene): railing shadows on the floor softer, no acne or seams; 45 -> 43 fps.


fix51 (ATN's batch 3, untested in game; the helper GPU-tested): **Ray tracing (Experimental)**, section "Ray Tracing
(Experimental)": Ray-Traced Sun Shadows (softness 0-3 degrees), Ray-Traced Light Bounce (rays, range) and Path Tracing
(bounces) through the Total War helper (renodx-tw-dlaa64.exe, D3D12\D3D12Core.dll next to the addon; restart after
turning on; ray_tracing.hpp, twattila/rt_core.hpp). The scene: every G-buffer draw (three RGBA16F targets and the depth)
streamed out through the game's own vertex shaders (alpha-tested foliage with its UV, tested against its diffuse alpha;
characters every frame), the camera from VS PER_FRAME's uploads. Before the tiled lighting each pixel's world position
(from the linear depth) and the sun go to the helper; the traced sun goes into the sun's shadow mask with the contact
shadows (the darker; their screen-space march steps aside while the rays run), the Light Bounce's trace goes through
rays (path traced when on).

fix52 (ATN's batch 4, untested in game; the helper GPU-tested): **Ray-Traced Ambient Occlusion** (restart; range,
rate: every pixel or a quarter) into the tiled lighting's directional occlusion (t5, which it reads as 1 - w x
SSDOParams.z): the darker of SSDO / GT-VBAO and the rays', through the contact shadows' pass (its second target).
**Ray-Traced Reflections** (restart; samples 1-8) in place of the screen-space ones: the tiled lighting blends them over
the probes as it does the screen-space reflections (t6: reflected light, the share of rays that met something), and the
game's screen-space trace (ps 0xE0B7E115) is left out while they run. The helper's G-buffer guides come from Ryse's
G-buffer (0x52C5E005: world normals, roughness 1 - gloss, the specular colour from its luma and chroma, the diffuse
colour), and the game's HDR scene at the end of the frame is kept for the next frame's reflections and paths
(0x52C5E006). Path Tracing also bounces the reflections' rays. Fix: path-traced rays that met what last frame saw took
black light (no scene colour was kept). helper21 (in the zip): the last zip's helper19 stalled each request (its
partitioned TLAS, now off); ReSTIR GI for Path Tracing (about a quarter of the noise). Needs DLSS addon v115 for the
Light Bounce with the raised light limit (below).

fix53 (ATN's batch 5, untested in game; the helper GPU-tested): helper22 (in the zip; bridge v11): ReSTIR GI v2
(compared with Embark Studios' kajiya, MIT / Apache 2.0, not copied: a light switched on or off shows within 3
frames), the partitioned TLAS fixed and on again.


### F1 helper lifetime repair (2026-11-09, GPT-6 Astra)

AddonUninit now shuts down the shared helper bridge after unregistering its users. The watcher exits before module unload; child process and IPC ownership are released. Shutdown drains outstanding GPU fence generations, with ordered CPU fence recovery only after confirmed helper exit. Failed launches clean up partial ownership and existing IPC sessions are not overwritten.

Offline checks: x86/x64 production-header tests cover process/thread cleanup, failure injection, 20 reloads with stable handle counts, and three interleaved D3D11/D3D12 WARP fence generations under normal completion and forced shutdown. No game was launched. Manually check helper-enabled unload/reload and device recreation, then repeat the reported battle. The original battle freeze is not proven fixed; unexpected mid-run helper failures remain audit F7.
