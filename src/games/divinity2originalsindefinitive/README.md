# Divinity: Original Sin 2 DE — calibrated rendering modes

Rebuild the complete folder in your existing RenoDX checkout. This archive
contains source, not a compiled addon. The shared shader-data layout changed,
and t00/t01 now communicate through LUT metadata: rebuild and install them
together. Do not combine these shaders with the previous addon binary.

## Modes

| Mode | Rendering | Neutral gray at Paper White 203 |
|---|---|---:|
| Off | Original game curve and game brightness/contrast | About 15 nits at game Brightness 1 / Contrast 1 |
| Vanilla | Captured native curve and native colour processing, with fixed reference brightness/contrast | About 15 nits |
| Vanilla+ (Tweaked ACES) | Full RenoDX ACES after the game's white balance and regional colour grading | Calibrated to about 15 nits |
| Vanilla+ (PsychoV30) | PsychoV30 after the same game grading | About 15 nits |

These are neutral scene-linear 0.18 inputs at the tone-mapping stage, with
neutral user controls. Authored grading, exposure and other user adjustments
can change actual scene pixels. Matching gray does not make the curves or
colours identical. 203 is the scene-brightness reference chosen for the addon;
it is not a measurement of native diffuse white.

## Vanilla peak behaviour

Vanilla restores the earlier highlight expansion requested by the user. It
scales the captured native output by Paper White / 203, then expands or
compresses the upper range to the selected Peak. Values below the shoulder
keep their scaled native brightness, including the neutral gray reference.
At Paper White 203 / Peak 1000, in-range native values are unchanged. Higher
peaks now brighten highlights again. Raising paper white also scales scene
brightness intentionally.

The captured spline, luminance/per-channel blend, native colour processing,
black normalization and output matrix are retained. The baseline curve comes
from the supplied September 29 captures with native brightness/contrast 1.
The highlight remapping and final peak ceiling can change the original output.
UI brightness remains a separate control.

## Vanilla+ calibration

Both modes retain native white balance and the complete shadow/midtone/highlight
colour-grading block. They bypass the native glow/red transform, saturation
stage, highlight pre/post matrices, tone curve, final brightness and contrast.
Tweaked ACES supplies its own complete RenoDX rendering transform. PsychoV30
does not use an ACES rendering transform around its curve.

Psycho's input anchor is fixed at 0.18 and its output anchor at 15 / 203. Its
algorithm file is unchanged. The old exposed adaptation/background debug
controls are removed; use Exposure for brightness adjustments.

Tweaked ACES solves a neutral input-exposure multiplier against the actual
RGCAndRRTAndODT implementation in the build. A 20-step bisection in exposure
stops targets 15 / 203 output luminance while keeping the selected ACES peak.
The solve runs once per 1024-thread compute group and is shared by that group,
not repeated for every LUT voxel. It assumes the linked ACES neutral response
is monotonic and reaches the target within +/-16 exposure stops. The exact
RenoDX dependency was not included in the supplied archive, so its integration
and GPU cost still need checking in the user's build.

Both outputs receive final BT.2020 gamut/peak handling. Out-of-gamut colours are
moved toward neutral at the same luminance, as far as the selected peak permits.
There is no native post-matrix after this mapping.

## Optional fire colours

In Advanced settings, Vanilla and both Vanilla+ modes have their own independent
**Fire Colour Correction** and **Fire Hue** values. Only the selected mode's
controls are shown, and switching modes retains all three sets of values.

**Fire Colour Correction**:

- 0: off, with an exact bypass of the colour correction.
- 1: maximum correction allowed by the highlight selection and display gamut.
- Intermediate values blend the correction.

When enabled, **Fire Hue: Red to Yellow** selects the target hue. Its default
0.65 is a warm yellow-orange direction; 0 is red and 1 is yellow. The adjustment
preserves output luminance and keeps channels within the selected peak. It
does not colour neutral white highlights.

This selects bright reddish/pinkish colours, not fire objects. Other bright
red materials can be affected. The scene-linear highlight mask begins above
1 and reaches full weight at 4, after authored grading and user exposure but
before the ACES gray-calibration exposure. Dim colours are left alone. The
old Red Correction Scale and native matrix sliders have been removed.

Existing saved fire values are retained for PsychoV30 and Tweaked ACES. Vanilla's
new correction starts with strength 0 and target hue 0.65. Each mode's
Neutral/personal preset resets only its own fire controls; Default everything
resets all three sets.

Vanilla uses the same correction after its native tone curve, colour matrices
and peak calibration, directly in absolute BT.2020 nits. Its highlight mask
uses the graded scene colour before native ACES colour processing. Strength 0
bypasses the addition entirely, retaining the preceding Vanilla output. The
correction is HDR-only; Off and the inventory's SDR path do not use it.

The common grading sliders use the same order in all three modded modes:
Exposure, Highlights, Shadows, Contrast, Saturation. Highlight Saturation
follows these in Vanilla/Tweaked ACES; Cone Response Exponent follows them in
PsychoV30. This changes UI order only, not the order of grading calculations.

## Inventory and LUT routing

The original `if (!is_hdr || !DOS2_CORRECT_HDR)` block is unchanged. SDR and Off
also retain their original LUT shaper domains. Only modified HDR LUTs use the
fixed domain `(0.0000054931616, 1516.4874)`.

t00 writes alpha 0 to modified HDR LUTs. Its stock SDR/Off LUTs retain alpha 1.
t01 reads a texel's alpha before choosing the matching domain; it still samples
only RGB and produces its original output alpha/fade. This avoids guessing
whether a pass is HDR from UI state or domain values. The supplied t01 previously
ignored LUT alpha. Any additional, uncaptured LUT consumers still need to be
checked in game.

The user confirmed matching inventory appearance in the preceding version.
This update preserves that version's SDR handling and t01 routing. UI
composition and bloom shaders are unchanged. t01's FadeValue at c0.z remains
live; c0.w is still unused.

## Compare in game

1. Choose the relevant **Neutral** preset to reset its grading and bloom.
2. Use Paper White 203 and Peak 1000. For matching native UI, use its native
   brightness reference (300 in the supplied comparison).
3. Compare Off at game Brightness 1 / Contrast approximately 1 with Vanilla,
   including the inventory model. Modded HDR should remain independent of
   game brightness/contrast; SDR inventory intentionally retains stock controls.
4. Increase Vanilla Peak to 10000: highlights should expand again while the
   gray reference remains stable. Compare Vanilla+ using its Neutral presets.
5. Enable Fire Colour Correction and try its strength/hue on pink and orange
   fire, checking other bright red objects as well.

The earlier personal presets retain their grading and bloom choices; they now
use the calibrated gray references and start with optional fire correction off.
The former "Native like" Psycho preset is labelled "My settings for (PsychoV30)"
because its contrast settings are an artistic variation. Old persisted gray
and matrix keys are no longer used. Existing grading keys are retained, so use
Neutral when comparing the new defaults.

## Validation limits

CPU checks cover the native captured curve, upper-range calibration,
gamut bounds, fire-correction luminance, neutral/bypass behaviour, gray-solver
convergence on monotonic reference curves, control wiring and preservation of
stock SDR/Off math. They do not substitute for compiling the full RenoDX
addon/HLSL or testing the game. Neither the full framework nor an HLSL compiler
is present in this workspace.

This update additionally checks that Vanilla's calibration header exactly
matches the earlier expansion version, that the inventory routing and shared
fire-colour math are unchanged, and that per-mode controls/presets are independent.
The Vanilla fire-control addition also checks the zero-strength bypass, capture
and output stages, and all three modes' independent preset resets/visibility.
