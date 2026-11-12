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
| HDR (PsychoV35) | Recommended (2026-11-12). Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`), anchored on the game curve's mid gray; Hue Clip Emulation (default 70) toward the game's curve on each channel, clipped; its post grade and gamut fit (V35's Gamut Compression) after it. Recommended (Vanilla shadows): V35 Shadows 0.5. Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain (UE's film curve). Built only, not run in game. |

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
v31: less CPU: debug logging (DEBUG_LEVEL_0) off.
v32: Bandai's screen filters before the TAA follow the DLSS addon's render scale (talesofarise_dlss README v53,
`post_filters.hpp`): painterly00-02 (0x66DC643D, 0x2483CE26, 0x773EABCB), outline00-01 (0x77D287B7, 0x1A51F09D),
grading00-03 (0x381F5A30, 0x4105D9D8, 0x376E3F34, 0xA1B7FCDA) and lighting00's edge tests (shadow edge tint: the halo
around characters below 100%; normal and depth edges) space their taps in output pixels (SI.filter_scale), and the
game's sharpening before the TAA drops when the DLSS addon says so (SI.game_sharpening). Without the DLSS addon, or at
100%, both are 1: the game's.
v33: the five other deferred sun lighting permutations get the same edge scaling: lighting01 / 02 (0x6513DC8E,
0xC28D2F1E: lighting00 with Bandai's t14.y test folded, Smooth Volumetric Light included) and lighting03-05
(0xB2726B44, 0x81E3CED2, 0xF95FABAA: no volumetric lighting, light mask in t11). The halo around characters (ATN: stock
game at 100% too, grass behind the character brighter) is not the shadow edge tint: in the debug2 pixel trace the
marked pixel's light mask t14.x was 0.96, which skips the tint. Temporary **Halo test** (Extra, Advanced): lighting00
shows its AO (t5, Bandai's AO E243580F -> 8D9AC9A4 -> 8C5387E3), its light mask (t14.xyz, ps F2EFB728 then the light
passes 0xC70CE890 / 0x7EF5B0CC / 0xCB0EC552) or its light (t4) instead of the lit scene, to see which input has the
halo.
v34: **AO Halo Fix** (Extra, Advanced, `ao_halo_fix`, default On). ATN's Halo test (debug4, DLAA 4K): the AO view was
20-35% brighter within ~50 pixels of the character than further out, the lit scene the same ring; the light mask t14
(x 1 nearly everywhere, y marks foliage) and the light t4 showed none. Bandai's AO (ao00-11: 6 compute, 6 pixel shader
permutations, E243580F traced) sums 9 samples of its R16F depth pyramid at NDC offsets (radius 100 -> 2 / 150 -> 10
with depth) as max(0, dot(v, n)) / (|v|^2 + 0.01) with no range check, so a sample on a character in front of a bush
counts as nearly open. The fix (shared.h AoHaloKeep): a sample more than 0.1 x depth + 20 cm in front of the pixel is
left out and the sum scaled by 9 / kept. The temporary Halo test is removed.
v35: AO Halo Fix, part 2 (ATN on v34: the halo much smaller, bright spots behind grass gone too, a thin ring left around
the whole character): Bandai's AO blurs (aoblur00-07: horizontal 0x8D9AC9A4, vertical + temporal 0x8C5387E3 /
0x808E30C0, vertical 0xE3BCE975, 4 pixel shader permutations) weight each neighbour 0.1 + 1 / (1 + |depth difference|),
never below 0.1, so the character's own open AO bled into the bush (+-3 render pixels). AoBlurWeight: 0 beyond the
AO's limit (0.1 x depth + 20 cm). 3Dmigoto misprinted the temporal pass's integer constants (the loop count 2 and the
groupshared row offsets +-24 as float bit patterns, -24 as -1) and split one ld_structured into two reads that alias:
fixed by hand against the bytecode. The half-resolution AO upsample permutations (0x4D0A29EA, 0x8E29A68D: the same
weight blends the AO toward 1) are left as the game's (not seen in ATN's traces).
v36: AO Halo Fix tighter (ATN debug5, 33%: much less, a faint band left around the character): an AO sample is left
out when it is further in front than twice the AO's reach at the pixel's depth (its widest ring, 0.000608 x radius
factor in NDC, x depth / cb0[28].x), at least 5% of the depth + 10 cm (v34: 10% + 20 cm, ~65 cm at 4.5 m, kept a
character standing ~50 cm before the bush); the blurs' cutoff is 5% + 10 cm too. Temporary "Show AO (test)" (Extra,
Advanced): lighting00-02 output their AO (t5).
v37: plants darker below 100% (ATN debug7: DLSS Performance ~10% darker in the bush, rocks unchanged): Bandai's AO
samples mip 0 of its closest-depth pyramid (ps 0xAA7E0D32: the nearest of 2x2 pixels; 2048 wide at 4K, 1024 at
1080p), whose texels cover more of the screen the lower the render scale, so leaves look thicker to the AO. Below 70%
DLSS (SI.filter_scale), ao00 / 01 / 06 / 07 (the permutations with a full-resolution depth, t1) read t1 instead (one
render pixel, about the pyramid's footprint at 100%); AO shaders now load the DLSS scale (PostFilterShader). AO Halo
Fix gets a third level, Strong (cutoffs halved), to tell whether the last, tiny normal-view band is still AO (the
debug7 in / out traces landed on a lit leaf and a dark branch: not comparable).
v38: AO Halo Fix never lighter than the game's AO (ATN debug7, DLAA off / on: crevices under rock overhangs got
lighter: their samples on the overhang are "far in front" too, and the visible average replaced real occlusion). Set
aside samples now add back max(their own occlusion, the visible samples' average for each) (AoHaloTerm / AoHaloSum):
the halo is still filled in, overhangs keep their shade. The blurs' depth cutoff stays (it stops crevice darkness
smearing onto convex rock edges: those get lighter, as without the blur).

v39: AO Halo Fix renamed "Ambient Occlusion" (Game's / Fixed / Strong; same setting key, saved choices kept). ATN v38:
Strong looks like a high HBAO preset and stays as the deeper choice. The character halo ATN still sees does not change
with any of the three (debug8): it is not this AO; the DLSS addon's line trace (v60) finds its pass.

v40 (temporary): Ambient Occlusion test choices for ATN's comparison: "Test 1/2 Strong", "Test 1/8 Strong", "Test 0"
(sample cutoff x 0.25, x 0.0625, 0 of Fixed's; blurs keep Strong's cutoff). Two choices besides Game's stay in the end.

v41: the Ambient Occlusion choice moved to the DLSS addon's overlay (graphics options live there; post_filters.hpp
shares it, the AO and blur replacements load it like the filter scale): Game's / Fixed / Deep. Deep = ATN's pick of
the v40 tests (cutoff 1/16 of Fixed's, blurs at half Fixed's). Without the DLSS addon: Fixed. "Show AO (test)" and the
HDR addon's own AO setting removed.

v42 (temporary tests): Deep removed (ATN: worse than Fixed on rail tracks). Ambient occlusion tests 2-4 shrink the
sample cutoff AND the blurs' cutoff together (x 1/2 = v38-v40's Strong, x 1/4, x 1/16); 5 "no AO" drops the
screen-space AO (AoHaloSum returns 0) to tell whether the grass halo is the AO. ATN debug11 (bush 2.8 m behind the
character, Fixed): the bush's final AO is 0.50-0.55 within ~30 px of the character vs 0.35-0.40 (raw at the 0.3
floor) beyond 40 px at matched depths; 0A9D482A (toon lighting, uses the AO x 0.7 + 0.3 for material types >= 3:
foliage is 6) carries it into the image.

v43: final AO choices (DLSS overlay, Shadows: "Ambient occlusion halo"): Game's / Reduced (v34-v41's fix, default) /
Reduced + darker (samples and blur cutoffs x 1/4). ATN v42: "Test no AO" has no halo anywhere, so the rest is the
screen-space AO's blindness behind characters (what they hide is guessed from what is visible; worst on plants);
1/4 kept the same halo with darker shading. Tests removed.

v44: scene buffers always 16-bit (setting removed; R11G11B10 -> RGBA16F for HDR10 and scRGB output alike).
Sharpening tooltip: it scales UE's tonemapper sharpen (12 of 20 tonemapper permutations), which normal gameplay does
not use (tonemap00 DCFDF14D), with or without DLSS; Bandai's pre-TAA sharpen is the DLSS addon's toggle.

v45: Smooth Volumetric Light: SampleVisibility moved to shared.h (lighting00-02 call it with t6 / s4); test choices
Soft (3 x 3 tent 0.75 cells along and across the sun's rays, no streaks) and Long Rays (21 taps, +-8 cells).
PsychoV31: Highlight Saturation default 1.18, Highlight Hue Shift default 1.30 (slider to 2; above 1 OKLab a / b
extrapolate past the game's SDR hue, chroma kept); Reset / Recommended use them. Both HDR modes: the fire note, then
Highlight Saturation, then Highlight Hue Shift. Sun penumbra edge color (lighting00-05): the edge measure (largest
light-mask change over the 2.3-pixel gather, x100 -> the cb0[63] / [64] edge colors) is divided by the sun shadow
distance relative to the game's (DLSS addon, post_filters sun_shadow_distance): at distance 4 Bandai's soft-shadow pass
(cs 0x2084C889) spreads the head's penumbra over ~180 px and the edge color filled it (ATN debug12: a pale patch).

v46: PsychoV31 Highlight Hue Shift back to 0-1, default (and Reset / Recommended) 0.30 (ATN meant 0.30); Highlight
Saturation stays 1.18.

v47: Smooth Volumetric Light: Long Rays removed; choices Off / Soft / Along Sun Rays (new key volumetric_light_smoothing,
default Along Sun Rays: the order changed). Shorter tooltips.

v48: volumetric00 (cs 0xD09F3430, Bandai's volumetric lighting): with Smooth Volumetric Light on, its 160 shadow lookups
weight the 2 x 2 Gather texels bilinearly instead of averaging them (ATN debug14: Max volumetric fog's 2-pixel cells
showed the 2048 shadow map's texel steps as stair-stepped light shafts). sunshadow00 (cs 0x2084C889, the sun shadow
mask): new Extra setting Smooth Sun Shadows (default On): above the game's sun shadow distance (DLSS addon ratio), a
tent filter over 12 x 12 texels (36 Gathers, half-width 3.5 x ratio^0.25 texels, at most 5) replaces Unreal's 5 x 5
box PCF, so edges no longer follow the stretched texel grid; at the game's distance the box stays. Both generated
from the 3Dmigoto dumps (CS declarations / stores fixed; D09F3430's iadd with a negated source restored); fxc counts
match the originals (gathers / stores / loops / samples).

v49: Smooth Sun Shadows: v48's wider tent made shadows blurrier and the penumbra's edge color bigger (ATN debug15); now
the game's own 9 Gathers (6 x 6 texels) with a tent of half-width 3 texels (no widening with distance): about the
box's softness, no texel stairs (fxc: 18 gathers, 9 + 9). Smooth Volumetric Light: with finer cells (Max volumetric
fog) Soft / Along Sun Rays stretch their spacing and taps to keep the game's 8-pixel reach on screen (at most 5 x 5 /
25 taps). New Extra sliders (Advanced, 1 = the game's, ATN testing which stay): **Sun Haze** (lighting00-02: the
haze tint and in-scattered light), **Painterly Filter** (painterly00: lerp to the unfiltered pixel; painterly01/02:
the blur's add), **Outlines** (outline00/01), **Grass Glow** (grading00/01: material 6's x1.3 glow add).

v50: Sun Haze above 1 fixed (ATN debug16, 2.0: black rock edges against the sky, orange sky): Bandai's haze tint
(r8.xyw, below 1 toward the sun) went negative with lerp(1, tint, haze); now tint^haze (as denser haze would),
in-scatter still x haze. Smooth Sun Shadows: Off / Smooth (v49, tent half-width 3 texels) / Sharp / Sharper /
Sharpest (2 / 1.5 / 1.2; half-width 6 / (setting + 1)), ATN tests. New test slider **Shadow Edge Color** (lighting00-05:
Bandai's colored penumbra line, cb0[63] / [64], x the slider; 0 = plain shadow color). Smooth Volumetric Light
Along Sun Rays confirmed by ATN (already the default).

v51: every Extra slider checked above 1 for blends that extrapolate below 0 (v50's Sun Haze bug). Outlines: opacity
saturate(a x k) (at most opaque). Painterly Filter: below 1 a fade to the pixel as before; above 1 the neighbours'
weights x k in painterly00 / 01 / 02 (still averages; 02 keeps Bandai's 1.2 denominator) instead of extrapolating.
Shadow Edge Color: weight saturated. HDR only: Sharpening's result max 0, Film Grain's multiplier max 0. Safe already:
Bloom, Grass Glow (additive), Vignette (saturated), Chromatic Aberration / Motion Blur (offsets), Video AutoHDR (0-1).
Permutations: all slider passes the traces showed are covered (lighting00-05, painterly00-02, outline00/01,
grading00/01; grading02/03 have no grass glow). New_1 also holds outline variants 0xC07E4E61 / 0xDDF4509C and
lighting-like 0x71953DD2 / 0x0E8ACF10 / 0x198E7382 / 0x2022424E (cb0[64] edge color, no 2.3 px gather): never in a
trace, not replaced.

v52: **Sun Step Fade (Experimental)** (Extra, Advanced, 0-2 s, default 0 = off): where the sun moves it turns in
steps (~10 s) and the whole map is rendered anew; at long sun shadow distances its large texels jump. HDR addon
(native D3D11 in the sunshadow00 callbacks, every used slot saved / restored, b2 with its range): sunstep00
(`0x5A1E57E0`, 1 thread before the mask, with its b0 / b1) builds world-to-shadow = inverse(screen-to-world,
cb1[44-47], affine, absolute world: the cloud shadows use it) x screen-to-shadow (cb0[28-31]) and flags a step when
the shadow u or depth axis turns by more than ~0.01 degrees (camera moves only shift the frustum); state (9 float4,
t6): last / old matrices, timer. sunstep01 (`0x5A1E57E1`, after the mask) copies the 8192 D16 map to an R32_FLOAT
old map (t5) every frame except during a fade. sunshadow00 then lerps to the old map's visibility (tent, same width;
3 for the game's box) by smoothstep(timer), only where the old uv is inside the map. Costs ~256 MB VRAM and one
map copy per frame while on. Moving characters' shadows trail during the fade (the old map is frozen); a camera cut
that changes the sun also fades. fxc: 27 gathers (9 + 9 + 9).

v53: Grass Glow removed (ATN: no visible change); Painterly Filter 0-1 (v51's stronger-than-game weights removed:
at most the game's filter, so the plain blends cannot extrapolate). Sun Step Fade removed (ATN: unsure it worked;
smooth sun movement instead, DLSS addon).

v54 (ATN): UE Extended Highlight Saturation per base curve: legacy 0.92 (`ext_highlight_saturation`), UE Filmic
1.05 (new `ext_filmic_highlight_saturation`, the last shared.h field but one); UE Filmic Highlight Hue Shift 0.34.
PsychoV31 (default, Reset and Recommended): Highlight Hue Shift 0.38, Highlight Saturation 1.20.
v55: Shadow Edge Color tooltip corrected (ATN: 0 shows a colored fringe, 2 almost none). In the sun shadow's soft
edge (shadow 0.1-0.9) the slider weights the game's edge color (cb0[63] / [64]) against the plain shadow tint (cb0[50] /
[52]); at 0 the tint fills the soft edge, which is the fringe.
v56: Shadow Edge Color default 2 (ATN), slider up to 10 (the per-pixel weight still saturates at 1; higher fills more of
the soft edge with the edge color).
v57: Shadow Edge Color default 5 (Reset and Recommended; ATN). Post Processing section above Extra: Bloom, Motion
Blur, Chromatic Aberration, Vignette, Film Grain, Sharpening, Painterly Filter, Outlines (keys unchanged). Extra:
Smooth Volumetric Light, Sun Haze, Sun Glow (new), Smooth Sun Shadows, Shadow Edge Color, Video AutoHDR / Dithering.
Sun Glow (`sun_glow`, last shared.h field, 0-2): the white halo around the sun (ATN, debug18 Sun) is the sky sphere's
own (ps 0x11039AD5, now sky00; base pass, so not post processing): around the disk it adds 2 x pow(dot, 3500) and
0.1 x pow(dot, 500) of the sun's color (collection vector cb2[5] x cb2[0].x / 2), and within ~25 degrees a halo
(1 - 10 x (1 - dot)) x cb2[1].w x cb2[1].y on the cloud texture; values 27 -> 49 within two degrees in the trace,
40-60 after the painterly passes, all near Peak after tonemapping. Sun Glow scales the pow 500 glow and the halo; the
disk and its pow 3500 rim stay. Its compute twin 0x7C4ABF39 (most likely the sky for reflections) is left alone.
v58: ATN (debug19, Sun Glow 0): another glow. The biggest at the traced pixel near the sun is a UI material drawn
after tonemapping (vs 0x038D5E3D ps 0x287CCB34, now sunglare00: radial glow cb2[9] x pow(1 - r^2, cb2[11].y) pulsing
with sin(time), a 1024 x 1024 mask and a 512 x 8 ramp, premultiplied): 1.65 -> 7.66 there, unclipped in the HDR target
(SDR's UNORM held at most UI white), so it reached Peak. Its color now stops at UI white (SDR) and Sun Glow scales it
(alpha up to 1). The rest: Bandai's haze in lighting00 (Sun Haze), bloom, the sky's pow 3500 rim. Thin Sun Shadow Edges
(`sun_shadow_edge`, default 1, 0-2; last shared.h field): ATN likes Smooth Sun Shadows Off / Smooth with the smooth
sun (no flicker) but at Sun shadow distance 4 / 8192 the soft edge (either filter: ~3 texels) is a wide halo; sharper
filters show stairs and flicker. In sunshadow00 the filtered visibility gets saturate((v - 0.5) x k + 0.5), k = 1 +
(distance ratio - 1) x setting: 1 keeps the edge as wide on the ground as at the game's distance; its position stays
the smooth filter's (its half-shadow line), so no stairs.
v59: Sun Glow removed with both its shaders (sky00 0x11039AD5, sunglare00 0x287CCB34; ATN: 0x287CCB34 also draws
in-game text, which its clip changed); the `sun_glow` field is gone. Thin Sun Shadow Edges default 0.22 (ATN; Reset
and Recommended too). Sun Haze stays in Extra (Bandai's lighting pass, not post processing).
v60: SDR output (Shogun 2 v35's): Output HDR10 / scRGB / SDR / Auto (new default; a saved HDR10 / scRGB stays). SDR:
a 10-bit gamma 2.2 swap chain, white = the display's (TOA_SDR_OUTPUT: TOA_PEAK and the new TOA_PAPER_WHITE 1; the
proxy returns the encoded image clipped per channel as the game). Rendering Mode SDR: the game's image; UE Extended:
display-mapped to peak 1; PsychoV31: target gamut BT.709. Peak, Paper White and UI hidden, Video AutoHDR off. Auto:
scRGB when Windows HDR is on for the game's display (or unknown), else SDR, at the first swap chain (create_swapchain,
registered before the swapchain mod's). Every shader fix and slider works in both.
v61: PsychoV31 sliders (ATN), in order: Exposure, Highlights, Shadows, Contrast, Highlight Contrast, Shadow
Contrast, Flare (slider^5, as RenoDX's UE Extended), Saturation, Dechroma, Highlight Roll-off, Gamut Compression,
Gamut Compression Mode (PsychoV31's target: BT.709 / BT.2020 / Display P3 / Samsung G80SD; SDR output stays BT.709),
then the fire pair. Defaults = v60's look (1 / 1 / 0 / 0 / 1 / BT.2020). About: credits, the ACES copyright notice
(UE Extended uses RenoDX's ACES RRT; the full license ships as ACES_LICENSE.txt from docs/licenses), ATN's Ko-fi.
v62 (ATN): Gamut Compression Mode BT.709 / BT.2020 / Display P3 (Samsung G80SD removed), Display P3 default.
PsychoV31 Highlight Saturation default 1.00 (Reset, Recommended). Recommended (Vanilla shadows): Shadows 0.50, Flare
0.39. After the hue shift, the SDR LUTs and the post grade (all after PsychoV31's own gamut compression), colors
outside the target are brought to its edge at the same luminance and hue (RenoDX's RGB-bound compression in
MacLeod-Boynton; inside unchanged), with Gamut Compression above 0. Credits: RenoDX and PsychoV31 by ShortFuse, Musa's
custom PsychoV31.
v63 (ATN's experiment, debug21): PsychoV31 Highlight Saturation Shift (after Highlight Hue Shift, default 0): the
saturation (OkLab chroma / lightness) of highlights toward the game's SDR curve with Highlight Hue Shift's weighting.
debug21 (SDR vs PsychoV31 at Hue Shift 0, other frames): SDR fire 85-110 deg OkLab hue, blue 0, chroma / L 0.21 up to
its brightest; V31 36 deg, chroma / L 0.32 in the body down to 0.11 in the cores (pink: red faded to white). Hue Shift
above 0.5 turns too much yellow (ATN), so the saturation is separate.
v64 (ATN's values): PsychoV31 defaults (Reset, both Recommended): Highlight Saturation 1.00, Highlight Hue Shift
0.57, Highlight Saturation Shift 0.30, Gamut Compression Mode BT.2020 again. UE Extended (legacy curve) Highlight Hue
Shift 0.20.

v65 (ATN's batch, untested): Pictures of the finished frame keep HDR: on loading and after battles the game showed a
clamped copy of the frame. Once the tone mapper has drawn a frame, a render target of the frame's kind bound after it
(a capture or fade) switches to its 16-bit clone too, and so does the destination of a copy from an active clone
(before, the swap chain module skipped such a copy as mismatched). Ambient occlusion GT-VBAO (the DLSS addon's setting,
post_filters.hpp): all 12 AO variants (ao00-ao11) can run the visibility bitmask (bitmask_ao.hlsli, Attila's
twattila/bitmask.hlsli primitives) on their own inputs (G-buffer normal through View.TranslatedWorldToView cb1[8..10],
the pass's linear depth) in place of the 16-sample spiral, before the game's minimum and Bandai's height-field term; the
game's AO blur (with its temporal step) and DLSS settle its per-frame noise. WARP (tools\toa\warp_ao: floor + wall):
open floor 0.92, wall foot 0.50, wall middle 0.84, sky 1.

v66 (DXR start, untested in game): the sun shadow mask (sunshadow00, cs 0x2084C889) keeps the darker of its shadow
map and the DLSS addon's ray-traced visibility at t40 (DLSS addon v96, Ray-traced sun shadows, Experimental); unbound
(off, or an older DLSS addon) it is the game's mask as before.

v67 (untested in game): the lighting pass (lighting00-05) multiplies its ambient occlusion by the DLSS addon's
ray-traced ambient occlusion at t41 (rt_ambient.hlsli; DLSS addon v99, Experimental); unbound it is 1.

v68: Output Auto picks HDR10 instead of scRGB when the DLSS addon's frame generation is on (ReShade.ini [Tales AA]
FrameGeneration): NVIDIA's frame generation takes no FP16 / scRGB swap chain.

v69 (untested in game): the lighting pass (lighting00-05) adds the DLSS addon's ray-traced bounce light at t42 times
the base colour before the fog (rt_ambient.hlsli RayTracedBounceLight; DLSS addon v101, Experimental); unbound it is 0.

v70 (untested in game): ray-traced reflections (DLSS addon v102, Experimental): lighting00-02 replace Bandai's screen
reflection sample (t16 / t17) with the DLSS addon's at t43 where its ray found a known surface, as much as the game
uses its sharpest screen reflection there (rt_ambient.hlsli RayTracedReflection); unbound it is unchanged. lighting03-05
have no screen reflection and are unchanged.

v71 (untested in game; DLSS addon v104): ray-traced reflections (t43) in both screen reflection branches of all six
lighting permutations (the sharp t16 / t17 and the blurry t17 / t18 in lighting00-02; lighting03-05 do have screen
reflections too, at t13-t15: v70's note was wrong), where the rays met a surface (RayTracedReflection now takes their
share: the rays sample the pixel's own roughness); ray-traced specular occlusion (t44, rt_ambient.hlsli
RayTracedSpecularOcclusion) dims the cubemap sky tint (the cb0[75].w term) in covered places. Unbound: unchanged.
