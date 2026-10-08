# Expeditions: Rome audit — 2026-10-01

## Outcome and scope

Source corrections are confined to this game folder. The addon builds with
Clang x64 Debug as `build/Debug/renodx-expeditionsrome.addon64`. It was not
installed in the game folder, and no Git commit or push was made.

The captured device-0/sRGB LUT path is internally coherent, but this is not a
claim that every game scene or shader permutation has been verified. The game
was closed during the audit; screenshots and the prior ReShade log are not
live resource traces.

## Corrections

1. **SDR LUT headroom:** the original LUT stores sRGB divided by 1.05, then its
   UNORM target clips. Clipping before division loses valid LUT headroom and
   changes interpolation. The builder now clips after division. Tonemap
   consumers emulate their original UNORM clip after multiplication/dither,
   then apply Paper White/UI scaling. Dither scales with the scene; FXAA alpha
   remains unscaled reference luma.
2. **Psycho contrast:** the gray anchor was already correct. Contrast previously
   came from a two-stop shadow secant; it now uses the local log-log derivative
   at input 0.18. The purity denominator is guarded when Contrast is zero.
3. **Film Extended fades:** post grade now follows the complete display
   shoulder, as it already did in Psycho. Previously the shoulder counteracted
   fades. An analytical neutral example at Peak 1000/Paper White 203 changed a
   50% black-faded bright endpoint from approximately 862 to 519 nits. A 50%
   linear fade is not necessarily a 50% final-nits fade because the game also
   applies its display-gamma emulation.
4. **Outline gamut preservation:** only the original final negative-RGB clip
   changes in HDR. Outline/depth/stencil/palette math stays stock; SDR keeps
   the original clip.
5. **Unique registration:** the generated all-shaders macro duplicated several
   explicitly registered replacement entries. Explicit LUT/godray entries now
   avoid depending on which duplicate entry the map retains. Ten hashes are
   registered once: seven replacements and three activation-only passes.

## What is coherent

- The 32³ LUT builder and both consumers agree on the shaper, 31/32 scale and
  half-texel centering. Alpha is 1 for the custom HDR LUT and 0 for SDR;
  consumers use that tag, including for cached LUTs.
- LUT resources are upgraded to FP16 at creation. Post-tonemap FP16 hot-swap
  clones are activated by known passes; the activation retains the depth view.
- The shared injection buffer matches its HLSL packing (the LUT range field was
  removed again on 2026-10-03; it was the last field, so nothing else moved).
- Proxy state restoration is enabled for UE4 state caching. Output encoding is
  restart-only, so changing the setting does not immediately feed gamma color
  to a mismatched HDR10/scRGB output.
- Paper White/UI transport scaling cancels correctly through gamma decoding.
  Bloom/vignette are applied before LUT mapping. CAS retains signed HDR color.
  Cutscene images remain SDR at Paper White, not inverse-tonemapped.
- The captured FXAA/copy math has no RGB range clamp. FXAA still uses saturated
  SDR-style alpha luma, so edges solely between bright HDR highlights are a
  remaining quality limitation.

## Midgray: actual compiled HLSL measurements

Native D3D11 hardware tests, Paper White/UI 203 and Peak 1000. This is a
synthetic standard-film fixture, **not the screenshots' captured live settings**:
film parameters (slope, toe, shoulder, black clip, white clip) =
(0.88, 0.55, 0.26, 0, 0.04), blue correction 1, identity post grade.

| Neutral scene input | SDR reference nits | Previous Psycho nits | Corrected Psycho nits |
|---|---:|---:|---:|
| 0.09 | 12.47 | 11.82 | 13.71 |
| 0.18 | 37.01 | 37.01 | 37.01 |
| 0.25 | 56.64 | 61.91 | 58.22 |
| 0.36 | 83.13 | 103.55 | 94.26 |
| 0.50 | 106.97 | 155.47 | 141.30 |
| 1.00 | 148.23 | 318.66 | 297.61 |

Thus the screenshots' brighter Psycho appearance is plausible, but is not proof
of an incorrect gray anchor. Local contrast is improved; the mode is still not
globally vanilla-neutral. Film Extended is the closer vanilla-curve option.
Psycho approximates film purity and borrows hue in OKLab; that is not exact
preservation of the film's complete color processing or CIE luminance.

## Verification performed

- Focused Clang x64 Debug addon build succeeded, including all seven pixel
  replacements and the proxy shaders. Existing shared-code deprecation and
  Psycho31 FXC warnings remain; shared infrastructure was not changed.
- An FXC `/O3` full-LUT test wrapper executes the complete current LUT main on
  native D3D11 hardware, flattening its 32³ nodes into a 1024×32 float target.
  Thirty-six fixtures covered all rendering modes at Peak 400/1000/2500/10000,
  and both HDR modes at Peak 400/10000 with grading sliders all zero/all two,
  individual contrast/highlight/saturation zero, and full colored fades.
  Across 1,179,648 cells (4,718,592 RGBA values), no NaN/Inf values or bad alpha
  tags appeared. Default Psycho had only its expected black-origin cell.
  Full fades produced constant LUT output in both HDR modes.
- The **exact baseline production LUT CSO** was then tested independently through
  a UV vertex shader and geometry shader writing all 32 slices of a 32×32×32
  RGBA32F target. No pixel-shader wrapper or recompilation was used in this
  second test. The same 36 fixtures passed: no NaN/Inf, correct alpha tags, and
  matching extrema/black-cell counts. This directly checks the bytecode to be
  installed, including the handoff's compiler-dependent black-pixel concern.
  Its SHA256 is
  `5B16E112BFB64AFDFC8E9F310542B7803795D2EFD8324036BEC916FA7C087C6C`.
- Outline tests ran the original captured CSO, rebuilt baseline and replacement
  over nine stencil patterns and sixteen RGB colors on D3D11 hardware. SDR and
  disabled-outline signed HDR passthrough differences were zero. Active outline
  effects were exercised; 90 negative and 344 above-one channels were preserved.
  All 11,520 readback values were finite. The test target was RGBA32F to isolate
  shader math before UNORM/FP16 quantization.

These tests do not establish physical pipeline parity: an FP16 LUT differs from
the original 10-bit LUT's quantization. Nor do synthetic fixture tests prove
every live grade, GPU, compiler or uncaptured permutation works.

## Optional LUT range extension (removed 2026-10-03: no visible effect in game; kept for the record)

Advanced -> Extra -> LUT Highlight Range defaults to Original, and Reset also
restores Original. +2/+4/+6 stops widen only the HDR device-0 input domain.
`lut_range.hlsli` keeps per-axis nodes 0..20 at their original inputs (through
approximately 1.394 scene units), stretching the upper 11 intervals to reach
174.829/699.324/2797.303 instead of 43.705. Wider ranges reduce highlight
interpolation precision. Film Extended's display shoulder is recalibrated to
the selected endpoint rather than letting its newly admitted inputs overshoot
Peak; that changes its upper-highlight roll-off.

Both captured consumers invert this shaper BEFORE saturation and retain the
original half-texel centering. They derive the actual domain from alpha tags
1/3/5/7, not the current range setting, preventing a setting/cache mismatch
from reinterpreting an existing LUT. SDR and Original retain their original
expressions. This is a generated-HDR-LUT domain extension, not a generic
SDR-LUT scaling/recolor bridge.

The focused Clang x64 Debug addon build passed. Native D3D11 tests ran the exact
new production LUT bytecode over all four ranges and the previous 36 fixtures
per range: 144 fixtures / 4,718,592 cells. All values were finite and every
alpha tag was correct. All 36 Original fixtures were byte-for-byte identical
to the preserved baseline output, including extreme grading and fades.
Psycho RGB in the first 21³ nodes differed by at most 6e-6 encoded units with
extension enabled; input spacing is preserved but floating-point evaluation
is not necessarily bit-identical. Film output nodes also change because of
the intentional display-shoulder recalibration.

Both exact production tonemap consumers also passed 136 synthetic/generated-LUT
fixtures using FP32 and actual FP16 3D textures. Deliberately mismatched current
settings (Original versus +6) produced byte-identical outputs for the same
cached LUT tag. Original/SDR tags matched the preserved old consumers exactly.
An independently derived sampling-reference shader using the same hardware
sampler matched 58 consumer fixtures within 5.96e-8 encoded units, including
inputs beyond the original range. These remain synthetic tests, not in-game
resource/state verification.

New production LUT CSO SHA256:
`115F04572787E5A01455FAB906D31E5CC28749E49F4E10F0C2A9B203FB0DE8B9`.
The existing game-folder symlink still targets `build/Release`; only Debug was
rebuilt for this change. Build the `expeditionsrome` target with
`clang-x64-release` while the game is closed before testing the new option.

## Reference comparisons

The official [Ace Combat 7 UE4 addon](https://github.com/clshortfuse/renodx/blob/main/src/games/acecombat7/addon.cpp)
also upgrades the 32³ LUT to FP16. Its
[HDR consumer](https://github.com/clshortfuse/renodx/blob/main/src/games/acecombat7/Output_InGame0_0x7A572E8F.ps_5_0.hlsl)
uses paired PQ LUT addressing/decoding, unlike Rome's preserved log domain.
That illustrates why a wider domain must be changed on both sides; its older
implementation is not a drop-in template. The local generic Unreal shader
reference was also inspected for encoding/grade separation.

The local 007 First Light tangent/shoulderless film extension was checked as
the Film Extended mathematical reference. It is not a UE4 resource template.

## Remaining in-game checks

- The LUT clips each input channel at approximately 43.705. Compare bright
  colored lights, bloom and transitions in both captured tonemappers.
- Confirm the new build in a fixed scene, including fades, blue lights, outline
  highlights, both tone consumers, menu/UI-only frames and cutscenes.
- Inspect actual cloned SRV/RTV formats. FP16 replaces sRGB views without an
  automatic sRGB conversion, and the current shared `FlushDescriptors` is a
  no-op; source inspection alone cannot prove cached-view behavior is correct.
- Check resizing, rounded/dynamic resolutions and pooled target reuse. The
  backbuffer-aspect clone filter may exclude some rounded-size targets.
- Godrays are user-confirmed in the menu only. Videos, depth of field, motion
  blur, lens flares and uncaptured tone/LUT permutations need runtime evidence
  and any additional shader dumps. Non-device-0 output is not validated.
