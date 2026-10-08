# RenoDX — Expeditions: Rome (UE4, DX11)

The game has no HDR option; RenoDX outputs HDR10/scRGB through a swap chain proxy.

## Rendering modes

| Mode | What it does |
|---|---|
| SDR | The game's native SDR tonemapper (UE4 Filmic) and LUT headroom. The original UNORM clipping stages are emulated, then the scene is scaled by Paper White. FP16 and original 10-bit LUT quantization are not identical. |
| HDR (Extended) | Default, as close as possible to SDR. Keeps the game's native Filmic tonemapper and extends its result into HDR, like Pharaoh: Dynasties: the color is scaled down until its brightest channel is below the curve's white point, run through the stock film path, scaled back up, and the brightest channel rolls off to Peak (Neutwo). **Highlight Expansion** On (default) puts the white point 3.5 stops lower; Off only expands what SDR clips. Colors from SDR (below). Matches SDR up to about 90 nits and is never darker than SDR. Optional V31 grading sliders. Needs the film curve; the legacy curve falls back to PsychoV31. |
| HDR (PsychoV31) | Recommended (Presets → Recommended). Replaces the game's tonemapper with PsychoV31, like Pharaoh: Dynasties' Psycho mode: only the artist's grading (white balance, color grading, fades) is kept, with PsychoV31's own contrast (1), shadows and colors, anchored on the game's mid gray. No film curve, ACES parts or gamut expansion. Not faithful: shadows are lifted (about 4x at 1.4 nits), skies more violet and reds pinker than SDR, but fire stays orange. **Game Shadows** (experimental) uses the game's darker toe below mid gray, faded in over the stop below it. |

**Colors from SDR** (Extended). UE's film curve works per channel: its shoulder
compresses the brightest channel most, so bright blues turn cyan, oranges yellow,
and both lose saturation. An HDR extension undoes that, which is why skies read
purple and sunlit wood red next to SDR. So the SDR color is scaled to the HDR
luminance; only the light HDR adds on top of SDR keeps the extension's
saturation (also where the scaled SDR color would pass Peak). The hue is matched
to SDR as displayed (after the 2.2 emulation and post grade) for SDR's share of
the light; the light HDR adds keeps its own hue, so bright fire stays orange
instead of SDR's lemon yellow. Up to about 140 nits hue and saturation equal SDR;
very bright saturated reds lean pinker. Saturated colors that would pass Peak or
BT.2020 are desaturated to fit, keeping luminance and hue.

PsychoV31 fallbacks: if V31 returns black (saturated blues; its source cage depending on how FXC compiles the shader), the pixel is redone target-only; if still black (its target solve at high contrast), the Extended result is used.

Fades and post grade run after the complete HDR mapping. Like SDR on a 2.2
display, the sRGB output is decoded with gamma 2.2.

## Shaders

| File | Pass |
|---|---|
| `lut00_0x61F067B4` | CombineLUTs (32^3, upgraded to FP16). Builds the HDR LUT and tags it with alpha 1 for t00/t01. |
| `t00_0x316D5B96`, `t01_0x70CBF75E` | Tonemap with and without vignette. Bloom and vignette sliders; no 8-bit dither in HDR. |
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
