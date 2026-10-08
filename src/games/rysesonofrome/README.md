# RenoDX — Ryse: Son of Rome (CryEngine 3, DX11)

The game has no HDR option; RenoDX outputs HDR10/scRGB through a swap chain proxy.

## Rendering modes

| Mode | What it does |
|---|---|
| SDR | The game's native tonemapper (Hable / Uncharted 2 filmic) and color chart, scaled by Paper White. |
| HDR (Extended) | Default. UC2 Extended (Musa/Jon) on the game's Hable: Hable up to its knee (third derivative root), its tangent above it; the brightest channel rolls off to Peak (Neutwo). SDR's colors and hue for SDR's share of the light. |
| HDR (PsychoV31) | Recommended preset (with Film Grain 0, Motion Blur (Camera) 0, Motion Blur (Objects) 1, Video Highlights 0.25). PsychoV31 replaces Hable, anchored on Hable's mid gray. Exposure, bloom, saturation / color balance, sun shafts and the color chart stay. Falls back to target-only, then Extended, where V31 returns black. |

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
| `post00_0xD0B247FF`, `post01_0xB022AFEA`, `post02_0xC3A469C1`, `post03_0xCC6DF78E` | Film grain, with or without sharpening and lens flares (red-channel shift). `post.hlsli`. |
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
