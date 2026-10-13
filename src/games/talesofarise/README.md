# RenoDX — Tales of Arise (UE4 4.21, DX11)

The game has no HDR option; RenoDX outputs HDR10/scRGB through a swap chain proxy. Built from the
Expeditions: Rome mod: its CombineLUTs is the same UE 4.21 shader (only two HDR output-device branches differ),
and the post-tonemap handling is the same.

## Rendering modes

| Mode | What it does |
|---|---|
| SDR | The game's native SDR tonemapper. The original UNORM clip and dither are emulated, then the scene is scaled by Paper White. **SDR Tonemapper** (v13): the game's (UE's legacy tonemapper, default, bit-exact) or UE Filmic: UE's film curve with its gamut expansion, as `r.TonemapperFilm=1` would (`UseFilmCurve` in `lutbuilder.hlsli`, SDR only; HDR keeps the game's curve as its reference). |
| HDR (UE Extended) | Default. marat569's UE Filmic Extended (RenoDX UE Extended) at its defaults (`ue_extended.hlsli`, as in Expeditions: Rome v17): blue correction 0.6 with its exact inverse, film black clip 0, the film curve below mid gray and its tangent above, 37.5% back to the curve, the game's post grade (incl. fades), then a per-channel C-infinity shoulder to Peak. Optional grading sliders (Advanced) run PsychoV31 grading before the curve; all at 1 they are skipped. **Base Curve** (v9): the game's curve (default) or UE Filmic. Tales most likely uses UE's legacy tonemapper (`bUseMobileTonemapper`, `cb0[35].w == 0`; ATN saw UE Extended and PsychoV31 identical before v7, when UE Extended fell back to PsychoV31 there). Game's: that legacy curve (UE4's FilmPostProcess) extended: identical up to its highlight segment (Cm1), then its midtone line continued, 37.5% back to the stock curve like UE Extended's Blend Factor 50, then UE Extended's display mapping; closest to SDR. Its linear continuation keeps fire's channel ratios (pinkish), so UE Extended has its own **Highlight Hue Shift** (same function as PsychoV31's: hue toward the game's SDR curve above mid gray), one per base curve since v16 (ATN): legacy 0.17 (0.35 was too orange), UE Filmic 0.30 (`ext_filmic_highlight_hue_shift`, the last shared.h field). UE Filmic: what RenoDX's UE Extended addon always uses (its lutbuilder notes: ignore the mobile/legacy path), with the game's film settings: deeper shadows than this game's SDR. **Highlight Hue Shift** default 0.24 (v16, ATN). A note above each Hue Shift slider: fire too pink or too orange? Change it. Recommended preset (v16): PsychoV31, Video AutoHDR 0.25, Motion Blur and Film Grain 0. |
| HDR (PsychoV31) | Recommended. The artist's grading (white balance, color grading, fades) and PsychoV31 only, anchored on the game's mid gray. **Highlight Hue Shift** (0-1, default 0.24 since v16; like ATN's Attila PsychoV30 slider): above mid gray (fully from scene white) the hue is turned toward the game's SDR curve's per-channel hue (OkLab, V31's brightness and chroma kept), so fire is orange/yellow like SDR instead of pinkish. V31's own hue weights only shift where its shoulder compresses. **Game Shadows** (the game's toe below mid gray) is commented out (ATN: shadows looked like SDR / Extended, colors desaturated). |

**No Expand Gamut in HDR** (UE's fake wide gamut for the SDR clip oversaturates in HDR; UE Extended forces it
to 0 too). SDR keeps it.

The game's gamma option (tonemapper `pow(cb0[50].w)`) is kept: in HDR on the value relative to Paper White.

## Shaders

| File | Pass |
|---|---|
| `lut00_0x876F0F03` + `lutbuilder.hlsli` | CombineLUTs (32^3, upgraded to FP16). Builds the HDR LUT and tags it with alpha 1 for the tonemappers. Same edits as Expeditions' `lut00`; helpers and HDR in `lutbuilder.hlsli`. |
| `lut01`-`lut04` (`0xFBB78F9F`, `0x357C5DF0`, `0xD81AC5B1`, `0xCF1F1C65`) | CombineLUTs that also blend 1-4 artist SDR LUT textures (post process volume Color Grading LUTs, weights `cb0[38..42].x`, applied after the tone curve on sRGB 0-1). HDR: the LUTs on the tone mapped color with UE Extended's adaptive D65 gamut compression around them; above 1 the color is divided by its max channel for the lookup (Ryse's chart method): hue kept, colors within 0-1 get the exact stock LUT. LUT Scaling at UE Extended's defaults, fixed (v15; its sliders and the in-use check removed, the textures seem unused): 100%, Hue Preserving (from its `filmiclutbuilder.hlsli`), removing the LUTs' raised black. |
| `tonemap00`-`tonemap19` + `tonemap.hlsli` | All 20 scene tonemapper permutations, one shared body; each file sets its features: bloom (always), sharpen, vignette, chromatic aberration, film grain + jitter; 10-19 add the game's animated noise overlay (`t6`/`t7`, `cb0[51]`, stock). Sliders (HDR): Bloom (incl. lens dirt), Vignette, Sharpening, Film Grain, Chromatic Aberration; no 8-bit dither in HDR. |
| `video00`-`video03` (`0x295C50DE`, `0x3474E3BC`, `0x5E300F3E`, `0xCB3E2B7E`) | Opaque CriWare Sofdec videos in UMG materials (Y/U/V planes, BT.601 limited; 01/02 with a UV region, 02/03 with a faded look). Shown at Paper White instead of UI; HDR: Video AutoHDR (ITM; "Video Highlights" until v17) + Video Dithering, from Ryse. Alpha videos (15 UI-effect materials, 4 on 3D meshes) stay stock at UI brightness. |
| `motionblur00`-`motionblur11` (`0x87FBD5AE`, `0x0C04E57A`, `0x14168103`, `0x15EED702`, `0x361FB737`, `0x3A6B3A7B`, `0x521A2C2A`, `0x5622A37F`, `0x579514E7`, `0x5D770744`, `0x68A6E6C5`, `0x78A0D210`) | UE 4.21's motion blur filters, pixel shader path (15-51 samples; the 12 compute twins only run with `r.MotionBlur.PreferCompute`). **Motion Blur** slider (v14, SDR and HDR; Advanced only since v18): the velocity scale `cb0[28].y` and maximum length `cb0[28].w` times its value. WARP vs stock: identical at 1 (max diff 0, 6 random input sets); 0 removes the blur. |
| FXAA `0x8533AF0C`, copies `0x75751BBD` / `0xE6551133`, SMAA blending `0xE462919A` | Stock; they only switch their render target to the FP16 clone. FXAA and the copies write the post-tonemap frame in Expeditions: Rome (same engine shaders); SMAA's position unverified. |

Post-tonemap targets (B8G8R8A8 / R8G8B8A8 / R10G10B10A2 at back-buffer aspect) use hot-swap FP16 clones
(view cloning); only targets written by the passes above switch. UE4 caches D3D state, so the proxy restores it
after its draw.

## Dumps (2026-10-09)

First dump 679 shaders; `New_1` (ATN) 12,846 more: 5,122 material, 1,707 UMG, 978 CS,
3,737 VS, ~1,300 global PS. Only `0x4D918BAF` (CS) and `0x87DF11B6` (PS) failed to decompile (the same two fail in
Expeditions: Rome; engine shaders, not ours).

UE creates shaders on first use; the exe also has `r.CreateShadersOnLoad` (Engine.ini `[SystemSettings]`), which
creates every shader of a loaded shader map at load.

- Tonemappers: 20 PS (all added). ~20 compute tonemapper variants exist too (UE's compute path, not used on PC).
- LUT builders: 5 CombineLUTs (all added). `0x74466640` is a different 3D-LUT builder (`cb0[11]`, output-device
  switches), probably UE's HDR display-mapping LUT for UE's own HDR output (off in this game).
- Videos: 25 CriWare shaders (4 opaque added, see above; 1 CS).
- Post process / game passes, stock: the motion blur filters above (added in v14), Gaussian blur chain (~150), Catmull-Rom resamples (44; TAA / upscale), SMAA (edges `0xCF6F0A2A`, weights
  `0xAD8DD538`, blending `0xE462919A`), radial blur `0xA5AFEAB4` (light shafts?), bloom/glare threshold with temporal
  accumulation `0x2EBD060B`, `0xDD57C93A` (samples a frame and decodes gamma 2.2: menu background capture?),
  stencil-tagged outlines `0x77D287B7`, contact shadows `0x95802ADA`, depth-aware blurs `0x66DC643D` /
  `0x2483CE26` / `0x773EABCB`.

Tested on D3D11 WARP against the stock CSOs (synthetic inputs, 6 constant sets): all 20 tonemappers bit-exact in
SDR, HDR within the removed 8-bit dither; the 4 videos identical with Video Highlights off; all 5 LUT builders equal
in SDR (within 2.4e-7), HDR finite with the alpha tag, and with identity LUT textures they reproduce lut00 (within
2.6e-4). Wrong-permutation controls fail as expected.

## Open

- Untested in game. Order of the passes after the tonemapper (SMAA, upscale, menu background) to be traced.
v19: sky banding (ATN: in SDR too). The DLAA addon's pixel trace showed Bandai's scene color is R11G11B10 (6 / 6 / 5
mantissa bits; r.SceneColorFormat 4 has no effect): the sky (Unreal's sky sphere material, ps 0x11039AD5, a gradient
computed from constants, no banded texture) is written to it, then two fullscreen passes (ps 0x2483CE26, 0x381F5A30)
and the half-size buffers stay R11G11B10 until the tonemapper. New Advanced option **Scene buffers** (restart
required, `SceneBuffers16`, default 16-bit): R11G11B10 render / copy targets at the screen's aspect are created as
RGBA16F (view cloning, as UE Extended's resource upgrades).
v20: stair-step haze edge around the sun at mountains (ATN; r.VolumetricLightingBufferPixelNumPerCell 2 helped, -25 fps).
Second pixel trace: Bandai's deferred sun lighting (ps 0xF2A6E883, `lighting00`) adds the volumetric lighting with one
trilinear sample of a 480x270x16 RGBA16F volume (t7; uv cb0[36].xy * v0.xy, slice (depth - 250) / 8000): lit and
shadowed 8-pixel cells meet in stairs. **Smooth Volumetric Light** (Extra, Advanced, default On,
`volumetric_light_smoothing`, last shared.h field): 3x3 taps one cell apart (1-2-1 weights) instead. Other shaders
sampling that volume (translucency, fog) stay stock.
v21: v20's lighting00 was missing from custom_shaders (never replaced: ATN saw no change). Added; t8 (R16F volume,
read at slice depth / 6000, the sun's share of the haze) smoothed like t7.
v22: v21 changed nothing visible (ATN): the saw-tooth comes from t6 (RGBA8 480x270x5: the sun's visibility in five
distance bands, sky pixels read the farthest), which v22 smooths too: all t6 / t7 / t8 samples go through
SampleVolume (one trilinear sample when Off).
v23: v22 only ~0.1% better (ATN). The saw-tooth repeats every 8 px in x (one 480-wide cell). Temporary **Volumetric
Light Test** (Advanced, Extra; `volumetric_light_test`, last shared.h field): 1 / 2 / 3 return 0 for every t6 / t7 /
t8 sample in lighting00, to find which volume draws the wedge.
v24: the test (ATN): without t6 the wedge is gone (haze everywhere); t7 / t8 off only flicker on the character. t6
draws the wedge, and the volumes are read through s4, a point sampler (8-px blocks; v22's 3x3 of point samples kept
the steps). Smooth Volumetric Light now filters every t6 / t7 / t8 sample itself: cubic B-spline over 4x4 cells
(Loads) and linear between slices. Test setting removed.
v25: v24's stairs much better (ATN) but black blocks on rocks with Smooth on, changing with sun shadow distance: some
cells hold NaN / negative values a point sample never reaches (behind surfaces, beyond the sun's shadows). FilterVolume
leaves them out and weights the rest up (all invalid: the point sample). Smooth Volumetric Light is now Off / Low /
Medium / High (ATN): Gaussian over 4 / 6 / 8 cells (sigma 0.58 / 1 / 1.5), linear between slices; Medium / High only
for t6 (the wedge), t7 / t8 at Low.
v26: v25 still black on rocks at Low / Medium / High, never Off (ATN; sun shadow distance 4: black almost everywhere
without haze); Medium / High (wider t6 only) no better than Low. The black comes from filtering t7 / t8 (finite
"no data" values a point sample never reaches). Now only t6 is filtered (SampleVisibility: 4x4 Gaussian, linear
between slices); t7 / t8 point-sampled as stock. Smooth Volumetric Light back to Off / On (On = v25's Low).
v27: v26 still black (ATN's trace: the scene color turns NaN in the passes after lighting00; black = every pixel the
sun's shadow fully covers, so more at sun shadow distance 4). Cause: the game computes log2(1 - visibility); a
filtered visibility of 1 came out as 1.0000001 (the division), 1 - it is negative, log2 gives NaN. SampleVisibility now
saturates its result (stock t6 values are UNORM, never above 1).
v28: v27 fixed the black (ATN); stairs still faintly visible. A wider Gaussian (v25 Medium / High) also blurs across
the shafts. New **Along Sun Rays** (Smooth Volumetric Light 2, new default; Soft = v27): 11 bilinear t6 taps 0.8 cells
apart along the screen direction toward the sun's point on screen (shaft edges run along the sun's rays), tent weights.
The direction comes from Cramer's rule on the view ray v1 and its screen derivatives against the sun direction
cb0[53].xyz (no matrices needed). Slices point-sampled in both modes (every call reads a slice centre).
ATN: "Along Sun Rays is insanely good", fixes the stairs for 3-4 fps (r.VolumetricLightingBufferPixelNumPerCell 2
cost ~25 fps and still had them).
v29: "Soft" removed (ATN): Smooth Volumetric Light is Off / Along Sun Rays (new key `sun_ray_smoothing`, default 1;
v28's key held 2 for Along Sun Rays). New **Sun Haze** slider (Extra, Advanced, `sun_haze`, 0-2, default 1 in every
preset): in lighting00 it scales the sun haze's light (r5.xzw, added after the t6 band lookup) and its shadows' tint
(r8.xyw, lerp from 1, max 0; the sky haze after it uses the same tint).
v30: Sun Haze removed (ATN): above 1 a black disc grew around the sun (the shadow tint r8.xyw scaled below 0, clamped
to black), below 1 the character flickered. lighting00 is v29's without it.
