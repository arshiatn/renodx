# RenoDX — Expeditions: Rome (UE4, DX11)

The game has no HDR option; RenoDX outputs HDR10/scRGB through a swap chain proxy.

Windows display scaling above 100%: "ExpeditionsRome-Win64-Shipping.exe" (`ExpeditionsRome\Binaries\Win64`) ->
Properties -> Compatibility -> Change high DPI settings -> Override high DPI scaling behavior, scaled by Application.
Without it the game renders at the scaled size (4K at 175%: 2194x1234), Unreal's buffers are rounded up to 4 pixels
(2196x1236), and neither HDR (the tonemap output's aspect no longer matches the swap chain: it stays 8/10-bit and
clips) nor DLSS ("Stock TAA: unsupported source/output texture layout") applies.

## Rendering modes

| Mode | What it does |
|---|---|
| SDR | The game's native SDR tonemapper (UE4 Filmic) and LUT headroom. The original UNORM clipping stages are emulated, then the scene is scaled by Paper White. FP16 and original 10-bit LUT quantization are not identical. |
| HDR (UE Extended) | Default (v16). marat569's UE Filmic Extended from RenoDX UE Extended (`src/games/ue-extended`), ported at its defaults in `ue_extended.hlsli`: Pumbo's blue correction (0.6, exact inverse), film black clip 0, the film curve below mid gray and its mid-gray tangent above (no shoulder), blended 37.5% back to the curve (Blend Factor 50), the game's post grade (incl. fades), then a per-channel C-infinity shoulder in blue-corrected AP1 from the tone mapped mid gray to Peak. Brighter than SDR above mid gray (UE defaults: scene 1 -> 1.08 vs SDR 0.72). Kept: the game's gamma (cb0[27].y), which UE Extended leaves out. Optional grading sliders (v17, Advanced: Exposure, Highlights, Shadows, Contrast, Saturation, Highlight Saturation) run PsychoV31 grading before the curve, on the mid gray anchor too; all at 1 they are skipped. Like RenoDX's UE Extended addon, always UE's film curve (also if a scene used UE's legacy tonemapper; v23, before: fell back to PsychoV31). Replaced the Pharaoh-style Extended (SDR-matched colors, v15 and older). |
| HDR (PsychoV31) | Recommended (Presets → Recommended). Replaces the game's tonemapper with PsychoV31, like Pharaoh: Dynasties' Psycho mode: only the artist's grading (white balance, color grading, fades) is kept, with PsychoV31's own contrast (1), shadows and colors, anchored on the game's mid gray. No film curve or ACES parts. **Game Shadows** (the game's toe below mid gray) is commented out (ATN: shadows looked like SDR / Extended, colors desaturated). |
| HDR (PsychoV35) | Recommended (2026-11-12). Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`), anchored on the game curve's mid gray; Hue Clip Emulation (default 70) toward the game's curve on each channel, clipped. Recommended (Deeper shadows): V35 Shadows 0. Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain (UE's film curve). Built only, not run in game. |

**No Expand Gamut in HDR.** UE's Expand Gamut pushes saturated colors outside sRGB, made for the
film's SDR clip. In HDR nothing clips it, so it oversaturated both HDR modes (ATN, v15 test toggle);
UE Extended forces it to 0 as well. SDR keeps it.

PsychoV31 fallbacks: if V31 returns black (saturated blues; its source cage depending on how FXC compiles the shader), the pixel is redone target-only; if still black (its target solve at high contrast), the Extended result is used (its post grade then after the display mapping).

Like SDR on a 2.2 display, the sRGB output is decoded with gamma 2.2.

## Shaders

| File | Pass |
|---|---|
| `lut00_0x61F067B4` + `lutbuilder.hlsli` | CombineLUTs (32^3, upgraded to FP16). Builds the HDR LUT and tags it with alpha 1 for the tonemappers. Helpers and HDR in `lutbuilder.hlsli`. |
| `lut01`-`lut04` (`0x3938E429`, `0xDF7A41F2`, `0xB50332F4`, `0x50964B52`) | CombineLUTs that also blend 1-4 artist SDR LUT textures (post process volume Color Grading LUTs, weights `cb0[38..42].x`). HDR: the LUTs on the tone mapped color with UE Extended's adaptive D65 gamut compression around them; above 1 the color is divided by its max channel for the lookup (Ryse's chart method): hue kept, colors within 0-1 get the exact stock LUT. The LUTs' raised black removed as UE Extended (by marat569) does at its defaults (LUT Strength and Scaling 100%, Hue Preserving; `filmiclutbuilder.hlsli`). v26: the three LUT sliders removed (ATN: the game doesn't seem to use LUT textures), the stage fixed at those defaults (same code as Tales of Arise). Film grain and chromatic aberration stay in **Extra (Probably not used)**. |
| `t00`-`t09` + `tonemap.hlsli` | All 10 tonemapper permutations, one shared body: bloom (always), sharpen, vignette, chromatic aberration, film grain + jitter. Sliders (HDR): Bloom, Vignette, Sharpening (with CAS), Film Grain, Chromatic Aberration; no 8-bit dither in HDR. |
| `sharpen00_0x57737062` | FidelityFX CAS. Its float bit tricks are restored. In HDR there is no clip; sharpening fades out above paper white. |
| `godrays01_0x1E796BD3` | Light shaft bloom setup. Godrays slider. |
| `fog07_0xCDCA640F`, `fog08_0xBCD37CAF` | Height fog + volumetric fog composite (`fog08`: directional in-scattering variant). **Volumetric Fog (experimental)** slider, HDR only: scales the volumetric fog's density, transmittance^x and its in-scatter by the matching Beer-Lambert ratio; 0 removes it, height fog stays. Fog on translucent materials (particles, glass) is in their own shaders and stays; the fog's cost stays too (Engine.ini `r.VolumetricFog=0` is the cheaper off). |
| `cutscene00_0xEDD3DA15` | Slate material for cutscene images. Stays SDR (no inverse tonemap), shown at Paper White instead of UI brightness. |
| `outline00_0x4DF6E0CF`, `outline01_0x84B3F0E5` | Stock outline/depth/stencil math; signed HDR color is preserved instead of clipped at zero. `outline01` is the same material with scene-texture UVs clamped to the view rect (render size differing from the buffer, e.g. below 100% render scale); found in the 2026-10-02 dump. |
| FXAA `0x8533AF0C`, copies `0x75751BBD` / `0xE6551133` | Stock; they only switch their render target to the FP16 clone. |

Post-tonemap targets (B8G8R8A8 / R8G8B8A8 / R10G10B10A2 at back-buffer aspect)
use hot-swap FP16 clones. Only targets written by the passes above switch to
their clone.

UE4 caches D3D state, so the swap chain proxy restores it after its draw
(`swapchain_proxy_revert_state`). Without that, UI-only frames such as the
options menu lose alpha blending.

## Verification and remaining limits

2026-10-09 full dump (`all_shaders_`, 15,735 shaders): 10 tonemapper permutations (were 2) and 5 CombineLUTs
(0-4 SDR LUTs; were 1), all added. `0x74466640` (a 3D-LUT builder with its own `cb0[11]` and output-device switches) is
probably UE's HDR display-mapping LUT for UE's own HDR output, which the game doesn't use. The only video is `Rome_MainMenuLoading.mp4` (UE Media Framework: YUV matrix in a
constant buffer, then a UI texture at UI brightness). 4 motion blur filter permutations exist (none was drawn in the 2026-10-03 capture below).
Tested on D3D11 WARP against the stock CSOs (synthetic inputs, 6 constant sets): all tonemappers bit-exact in SDR,
HDR within the removed 8-bit dither; all 5 LUT builders equal in SDR (within 2.4e-7), HDR finite with the alpha tag,
and with identity LUT textures the LUT builders reproduce lut00 (within 2.6e-4).

2026-10-03 dump review (1,248 shaders, 403 new since the analysis): the only
tonemappers are still `t00`/`t01` and the only LUT builder `lut00` (searched by
their LUT constants in all disassembly). New: `outline01` (added), four
pixel-shader TAA permutations (Temporal Upscaling off path; the DLSS addon
leaves them to stock TAA) and more material/DOF shaders. No UE motion blur pass
(velocity flatten, tile max, gather or filter) exists in the dump; velocity is
read only by TAA, DOF temporal, SSR and distance-field AO. `outline01` was run
against its original CSO on D3D11 (Wine, 10,492 values): SDR identical; HDR
differs only in 1,068 negative channels the original clips to zero.
`fog07`/`fog08` vs their original CSOs on D3D11 (Wine, 3,072 pixels, point-sampled
volume): SDR (any strength) and HDR at 1 identical; HDR at 0 equals the original
with an empty volume; at 0.5 and 2 equals the original with each volume texel
scaled (within 2e-5). Not yet seen in game.

The 2026-10-01 audit fixed SDR clipping/dither scaling, the then Psycho contrast,
HDR fade ordering, signed HDR outline output, and duplicate shader
registrations. A focused Clang x64 Debug build passed. Native D3D11 synthetic
GPU tests checked the LUT and outline; see [AUDIT.md](AUDIT.md) for the fixture,
results and distinction between shader-math tests and in-game validation.

Each scene channel above approximately 43.705 addresses the last LUT cell. The
"LUT Highlight Range" option (wider LUT input domain) was removed on 2026-10-03:
it had no visible effect in game. Uncaptured permutations,
actual render-resource views, dynamic-resolution changes, videos, and full-game
UI/post-process coverage still need in-game testing. Godrays were reported
working in the menu by the user; other lighting paths are not yet confirmed.

v27: less CPU: debug logging (DEBUG_LEVEL_0) off.

v28 (ATN's batch, untested): Ambient Occlusion section: GT-VBAO (Game's / GT-VBAO, Radius, Strength, Thickness,
Quality, Directions; default Game's). The game's last AO level (UE 4.21 AmbientOcclusionPS with the lower levels
upsampled: ao00 0x31A1C68C, ao01 0xBD381E21; t0 scene depth, t1 G-buffer normal, View: TranslatedWorldToView cb1[12..14],
InvDeviceZToWorldZTransform cb1[61]) returns the visibility bitmask's result (talesofarise/ue_bitmask_ao.hlsli, shared with
Tales of Arise; Attila's twattila/bitmask.hlsli primitives) with the game's intensity, not its power. The depth texture's
place for a screen position comes from the interpolants' derivatives (works below 100% render scale). Noise moves every
frame (present), settled by the game's AO blur and TAA / DLSS. GT-VBGI, screen-space shadows and reflections are not in
(the passes for them are not identified in the dump yet).
