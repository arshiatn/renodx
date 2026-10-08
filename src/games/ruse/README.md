# RenoDX — R.U.S.E.

Requires Windows HDR and the game's HDR option. Native scene targets keep
their packed 10-bit format. RenoDX uses an FP16 backbuffer clone and a DX11
HDR presentation proxy.

## Rendering modes

| Mode | Behaviour |
|---|---|
| SDR | Original scene response and clipped highlights, scaled by Paper White. |
| PsychoV31 - Native Tone | Applies PsychoV31 after the game's gamma/brightness response. The Faithful to SDR preset selects this mode. |
| PsychoV31 - Full Replacement | Replaces that gamma/brightness response with PsychoV31. Same response and grey calibration as the preceding build's last V31 mode. Default. |

Both Psycho modes retain the game's colour grading and each shader variant's
bloom, vignette and directional blur. Native no-bloom variants remain without
bloom. Existing processing and shared Psycho controls are unchanged. Extended
and its separate grading controls have been removed.
Psycho controls, Bloom and Vignette appear only in Advanced settings with a
Psycho mode selected. Peak appears only in the Psycho modes.

The `rendering_mode` config key is retained: 0 SDR, 1 Native Tone, 2 Full
Replacement. Settings loading clamps the former last V31 mode's index 3 to 2.
The former V30 selections now use V31. Existing slider keys and defaults are
retained. Both Recommended presets reset Psycho controls, Bloom and Vignette
while preserving Peak, Paper White, UI brightness, output settings, Motion Blur
and Depth of Field.

The separate V30 library and calls are removed. The supplied V31 header and
its SM3 workaround remain unchanged: compression 1.5, no flare, BT.709 source
cage and BT.2020 target. Its framework helper dependencies remain necessary.

## Postprocess variants

All seven final postprocess shaders use `shaders/postprocess.hlsli`. The wrappers
retain the native register bindings, blur kernels, bloom and packed scene-range
restoration found in the dumped CSO/ASM:

| Native sampling | Bloom | No bloom |
|---|---|---|
| 17 taps, weighted | `0x232EB5DF` | `0x44550114` |
| 13 taps, uniform | `0xC3EBA3CA` | `0x2D5EFDE2` |
| 9 taps, uniform | `0xFB692F37` | `0x7E93D12F` |
| Single tap | — | `0x0106B253` |

The multi-tap families appear to correspond to high, medium and low quality;
the exact setting-to-hash mapping still needs an in-game check. The single-tap
shader has no native scene-range multiplier, directional blur or vignette.
Psycho can only use the highlight headroom that reaches that shader. Keep the
game's HDR option enabled. All three rendering modes are available in every
variant.

## Effect controls

Advanced → Extra includes two independent controls in all three rendering modes:

| Control | 0 | 1 (default) | Intermediate values |
|---|---|---|---|
| Motion Blur | Keep only the centre scene sample in the final blur pass. | Previous native blur path. | Reduce its sampling radius. |
| Depth of Field | Bypass the two identified blur filters and zero the two DOF mask alphas. | Native filters and masks. | Reduce filter radius and mask strength. |

Motion blur is confirmed by the previous-frame projection/motion-vector
producers `0x88292FC6`, `0x8588D239` and `0xF2B4369C`, whose encoded Y/Z vectors
are consumed by the final postprocess. The control covers all six multi-tap
postprocess variants, including SDR. The single-tap variant has no motion blur
to disable. Motion-vector/shadow producers themselves are left unchanged.

The four DOF replacements retain native bindings, UVs, colour and alpha rules:

| Shader | Role | Native bindings |
|---|---|---|
| `0x6F08A739` | Horizontal five-tap DOF filter | Input dimensions c0, native DOF intensity c1, input s0. |
| `0xDC8B14DB` | Vertical five-tap DOF filter | Same. |
| `0x971790DF` | Procedural DOF mask | Native DOF intensity c0; RGB stays zero. |
| `0x7E5A7D9B` | Coloured DOF mask | HDR parameters c0, DOF intensity c1, subpass c2; RGB stays native. |

Filter bypass outputs the input RGB and alpha 1, matching the native filter's
alpha convention. The masks retain their RGB and only scale alpha. These passes
can run offscreen, so DOF control applies there too; it does not scale nits or
apply Psycho. Missing addon injection preserves native output. Both controls
default to 1, and tone presets preserve the user's choices.

Bloom and Vignette retain their existing controls in the Psycho modes. The dump
has no separately confirmed lens-flare, god-ray or film-grain pass. Sun/sky glare,
water simulation, general sprite draws and PreviousSceneMap filters are not
given speculative effect-disable controls.

## Gamma assumption

Gamma 2.2 remains the mod's working decode/encode assumption; these shader dumps
do not establish it as the game's display transfer function. The native final
response uses `Gamma` at c5.x and `Brightness` at c6.x, operating on luminance:

```text
output = Brightness * gradedRGB * pow(luma(gradedRGB), Gamma - 1)
```

On a neutral ramp this is `Brightness * x^Gamma`. It is a configurable tone
response, not evidence of a fixed 2.2 display curve. DX9 sRGB sampler/write
states and presentation/gamma-ramp conversions can also operate outside the
shader and are absent from the dump.

To settle this, capture the unmodified game's c5.x/c6.x, sampler sRGB states,
render-target format, sRGB write state and any presentation/gamma-ramp
conversion, then compare a known grey ramp. If another transfer function is
confirmed, the scene decode, Full Replacement grey anchor, UI/video scaling
and proxy decode must be changed together. No gamma change was guessed here.

## Brightness and cutscenes

Paper White scales the scene and cutscene video. UI brightness scales the
HUD/text separately. The two video replacements retain their native
three-texture YUV-to-RGB conversion and chroma UV scaling:

| Shader | Native composition |
|---|---|
| `0x1D8E1FA8` | Y dimensions c0, chroma c1; vertex RGBA tint/fade. |
| `0x66D7BE52` | Background/fade c0, Y dimensions c1, chroma c2; background blend including native alpha. |

Both keep Y/U/V at s0/s1/s2 and share `shaders/video.hlsli`. Finished video
keeps its original SDR contrast and colour; it does not receive a Psycho tone
curve. Paper White is applied after the native tint/background composition.

Video is clipped to the native range before Paper White scaling. The final
proxy does not clip it back to the 203-nit reference. A white video pixel at
Paper White 400 therefore targets 400 nits, provided Peak is at least 400.
At Paper White 203 the gain is 1. UI brightness does not control video.

Only draws targeting the backbuffer receive UI/video scaling. Offscreen draws
and shaders without valid addon injection preserve their native output. UI
and video gains are captured once per host frame. If another video pass is
found, or this pass renders offscreen in a different cutscene, its final
composition pass still needs to be identified.

## Automatic loading-screen fix

The confirmed native DX9 ReShade GUI block is now automatic; there is no
compatibility slider. It prevents the stale startup banner and cursor trails
after successful HDR proxy presentation. The DX11 proxy overlay remains
available. Failed/occluded proxy frames and device recovery retain native
presentation and GUI. This retains the conditions of ATN's successful test.
The install log message is `R.U.S.E.: native DX9 GUI draw filter installed.`

## Highlight Hue Shift

Advanced → Psycho V31 → Highlight Hue Shift controls V31's direction blend in
both Psycho modes:

| Value | Fully weighted highlight direction |
|---|---|
| 1 | Bisector hue. |
| 2 | Tonemapper's own per-cone response hue. Default. |

The range is now 1–2: V31 already returned the same bisector result for every
value below 1. Saved lower values load as 1 with the same V31 result.

Intermediate values blend these directions using V31's shadow, mid-grey and
highlight weights. The strongest highlight change is also gated by shoulder
compression. It preserves opponent radius and cone sum before gamut/Yf-ceiling
projection. It affects all hues; gamut constraints can also change brightness.
The Full Replacement grey-anchor calibration is retained.

## Build and verification

Replace the whole `src/games/ruse` folder and rebuild with the game closed.
Do not place dumped CSOs in that folder.

```powershell
cmake --preset clang-x64
cmake --build --preset clang-x64-debug --target ruse
```

Install `renodx-ruse.addon64`, keeping one R.U.S.E. addon in the game directory,
and restart. Check that `embed/0x1D8E1FA8.h`, `embed/0x66D7BE52.h`, all seven
postprocess headers and all four DOF headers are generated. There are now 25
game shader replacements.

- Check loading before and after opening/closing the proxy overlay.
- During video, compare Paper White 100, 203 and 400 with Peak at least 400.
  Hold Paper White fixed and move UI brightness; only HUD/subtitles should change.
- Check both video passes, their colour, black/background fades and transitions
  into gameplay. Offscreen use of a video shader retains native output.
- Switch low/medium/high settings and bloom on/off. Confirm the corresponding
  postprocess replacement is active and retains its native effects.
- Compare all three rendering modes and hue values 1, 1.5 and 2. Check SDR,
  preset selection and slider visibility. A saved mode 3 must select Full
  Replacement on upgrade.
- In Advanced → Extra, compare Motion Blur 1/0 while panning the camera and
  watching moving units. Compare 0.5 as well. Check high/medium/low quality and
  bloom on/off; the single-tap path should remain unchanged.
- Compare Depth of Field 1/0/0.5 where the native game blurs the scene, especially
  during zoom/focus transitions. Check mask edges and colour/brightness at the
  same camera position. Confirm the active passes match the four hashes above;
  additional unseen DOF permutations would need coverage.
- Test both controls in SDR and both V31 modes. Switch Recommended presets and
  confirm the chosen Motion Blur/Depth of Field values are retained.

The injection remains 24 floats (96 bytes), DX9 c200–c205. Former Extended
grading fields are padding; existing active offsets are retained. Offset 84
holds video gain. Previously unused offsets 28 and 32 now hold Motion Blur and
Depth of Field. Rebuild shaders and addon together.

This update checked all 250 dumped CSO hashes and matched the seven final
postprocess opcode streams, constants and sampler bindings against ASM. All
seven replacements preprocess with the c200–c205 injection. In 10080 CPU cases,
the actual postprocess main bodies match an independent native-ASM evaluation
of sampling, grading and response, including Psycho input/grey-anchor routing
(maximum relative difference 1.16e-6). Non-linear scene samples exercise Motion
Blur 0/0.25/0.5/0.75/1; sample counts confirm the zero setting bypasses the blur.
These include raw legacy mode 3 mapping to Full Replacement. Psycho calls were
input/anchor spies in these checks; the tone library itself was not executed.

The four actual DOF bodies were checked against the original CSO tokens in
81920 float32 pixel cases, covering native defaults, missing injection, zero
blur/mask strength and intermediate values. The filter kernels and native
mask RGB/alpha conventions match; all four preprocess with their native
bindings and c200–c205 injection.

Both video shaders' actual conversion/composition assignments match their original
CSO tokens in 10,000 float32 samples per shader, including vertex tint or
background/fade alpha. The shared video output block passed the existing
10,000-case Paper White/UI independence, alpha and offscreen/injection checks.
GPU partial-precision rounding remains a runtime check.

The actual addon body, compiled with an API shim, passed mode/default/preset
and slider visibility checks, 1,000 frame sequences across all 25 callbacks,
and the existing loading/presentation guard cases. The framework's actual
settings read/clamp methods also verified saved mode 3 → 2 and hue below 1 → 1.

The V31 header, shared injection offsets, postprocess sampling/grading,
UI/proxy files and loading/presentation guards are retained. The existing
loading fix remains automatic. Windows addon/FXC compilation and in-game
quality/cutscene checks were not available here. ATN verified the loading fix
in the preceding build.

## PsychoV31 on SM3

The supplied `psycho_test31.hlsli` is retained byte-for-byte, including its SM3
`isnan`/`isinf` macros. The supplied base's notes report an FXC `isinf()` lowering
issue, a comparison against ps_5_0 with maximum relative difference 1.5e-5, and
about 2700 instruction slots for the earlier shader containing two tonemappers.
These FXC results and the new shader's instruction count were not independently
reproduced in this update.
