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
| Vanilla+ (PsychoV35) | Musa's PsychoV35 (`twattila/psycho35.hlsli`) after the same game grading, Hue Clip Emulation before it | About 15 nits |

Presets: **Recommended** = PsychoV35 with Bloom 0.44; **Reset** = every image
control at its default (Vanilla); **SDR (monitors without HDR)** = Off (turn the
game's HDR off too). The PsychoV30 / Tweaked ACES presets (Recommended
(PsychoV30), Recommended (Tweaked ACES), Closer to Native (PsychoV30)) stay until
ATN decides. No preset writes Peak, Paper White, UI, Settings Mode or Output, and
each tooltip says so.

Page 1 starts with: the modded modes ignore the game's Brightness, Contrast and
HDR sliders; keep the game's Gamma Correction at 1 (PPGammaCorrection is not
drawn at 1; other values are still open).

**Sun Shadow Softness (Experimental, Extra, default 1):** the game's own PCSS sun
shadows (CascadedPCSS, every cascade / size variant and the orthographic camera's)
take the sun's angle from `LightProperties.x`; the row scales its tangent, the
light size that sets the blocker search and the penumbra, as Divinity 1's row
does. 1 is the game's bytes.

PsychoV35 replaces everything after the game's white balance and regional
grading (the untonemapped scene-linear point, `t00` before the former V30 call),
like V30: no curve runs on another curve's output. It has no fire rows: Hue Clip
Emulation (0-100, default 80) moves bright colours toward the hue the game's own
SDR picture clips them to. Its reference (`dos2_vanillaplus::SdrReference`) is
the game's SDR output transform with the inventory's traced SDR LUT constants
(ACES 1.1 at 0.02 / 48 nits, the highlight-fix LMT at 0.6, 10 % luminance tone
scale): within 0.32 8-bit codes of the game's own SDR LUT (RenoDX ACES alone,
the earlier stand-in, missed it by up to 137 codes). The SDR picture's mid gray
is 0.0996 of white (Mid Gray "Game's SDR": 20.2 nits at Paper White 203).

PsychoV35 keeps the game's darker shadows and stronger colours (ATN, 2026-11-16):
four of its sliders default to the game's own response, the rest keep Musa's
defaults (`shared.h` `DOS2_V35_*`, Recommended and Reset set them too):
Contrast 1.42 (the game's ACES contrast around mid gray, per cone as the game's
curve is per channel), Shadows 0.43 (its toe), Saturation 0.93 (its colour at
equal lightness after that contrast) and Highlight Contrast 0.87 (PsychoV35's
own highlights above it). Fitted on WARP against the game's own t00 through its
LUT and t01 (test/tw_psychov35 part 6, 10.), Peak 1000 / Paper White 203:

| Against the game (Off) | PsychoV35's own sliders | These defaults |
|---|---:|---:|
| Grey where the game is 0.05 - 1 nits | x3.1 .. x6.9 | x0.97 .. x1.04 |
| Grey where the game is 1 - 15 nits | x1.09 .. x2.40 | x0.98 .. x1.03 |
| Colour (OkLab chroma at equal lightness), the game's 1 - 30 nits | x0.81 | x1.03 |
| Grey at +5 .. +8 stops against PsychoV35's own (Peak 1000 / 203, 2500 / 275) | x1 | x0.95 .. x1.07 |

One curve, once: these are PsychoV35's own parameters on the untonemapped
scene, not a curve before it. Between mid gray and +4 stops the picture follows
the game (brighter than PsychoV35's own); its roll-off to Peak stays PsychoV35's.

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

**Per-Channel Highlights** (Vanilla, on by default): when Peak is below the
scaled native peak (1000 x Paper White / 203), each channel gets its own
shoulder and ceiling, so bright fire's red rolls off before its green and the
core turns orange / white as the SDR clip does. Expansions (Peak above it) keep
the channel ratios either way: no per-channel boost. Off = the earlier
brightest-channel shoulder, bit for bit.

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

**LUT addressing (2026-11-15, test/tw_psychov35 part 6):** t00 writes texel i
for the shaper's PQ i / 31, and the game's t01 samples the PQ code u straight,
so every pixel reads the colour at PQ (32 u - 0.5) / 31: mid gray about 12.2
nits where a mode places 15, in every mode including Off. **Exact Colour LUT
(Experimental, off)** samples a modded LUT at its texel centres (grey 14.98 nits
through the LUT); Off and the SDR LUT are read as the game reads them. On the
traced constants the game's own chain shows the scene's 0.18 at 11.2 nits in
HDR (9.9 with the city's grade, gain 0.92) and at 0.100 of white in SDR.

**Traced constants (R1b, 2026-11-16):** both LUT dispatches share c0
(ToneMapLuminanceApply 0.1, RedCorrectionScale 1, HighlightsFixAmount 0.6 / 1);
the HDR LUT is ACES 1.1 at 1e-4 / 1000 nits, the inventory's SDR LUT ACES 1.1
at 0.02 / 48 nits (sRGB). `test/tw_psychov35/part6_divinity2.ps1 -GameShaders
<the game's cso folder>` builds every LUT from them and fails when Off's t00 or
t01 leaves the game's own bytes.

**Orthographic sight (outlines02, 0x5C4137E2):** the orthographic camera's
EoCSight_HDR ships an older build of the sight shader (50-nit fill, bonus area
and outline, 100-nit sneak; the perspective pass has 240 / 300 / 300 / 240).
Modded modes give it the perspective levels relative to UI brightness; Off keeps
the game's. Not drawn in any traced frame yet (menu, city, inventory).

**Higher Precision Scene (Experimental, off, Output section, restart):** the
game's back-buffer-shaped R11G11B10_FLOAT render targets (the scene through
lighting, forward passes, bloom and PPApplyLUT's input) are created as RGBA16F
with cloned views. It turns resource cloning on, so the back buffer also goes
through the swap chain proxy. Not run in game yet: check alpha readers, copies
and the inventory.

## Graphics: DLSS, FSR, XeSS and ray tracing (Advanced)

The game has no TAA, jitter or motion vectors (FXAA and SMAA only). With the
Total War helper (`renodx-tw-dlaa64.exe` and its DLLs next to `EoCApp.exe`):

- **Anti-Aliasing DLSS / FSR / XeSS** (`dlaa.hpp`, off by default, 100% only):
  a Halton jitter written into the main camera's PerView upload (b12: a
  perspective projection, ViewInfo's size the back buffer's, view-projection =
  projection x view; the shadow cascades, the inventory portrait and the
  orthographic camera are left alone), the camera's motion from the depth, the
  helper on PPApplyLUT's input (the linear scene) with the result bound in its
  place. The game's SMAA weights are not drawn (its blend then copies) and
  FXAA is copied. Frame generation's HUD-less image is CombineUI_HDR's input.
- **Ray-traced sun shadows / AO** (`ray_tracing.hpp`, Experimental, off,
  restart after turning on; needs one of the upscalers): the G-buffer draws
  (decals, the ground's surfaces and water left out by their shaders) streamed
  out into the helper's scene; the trace right after LinearizeDepth. Sun
  shadows: the traced visibility in the full-screen mask's place (every filter
  and cascade count). AO: the SAO chain not drawn, its two blurs write the
  traced visibility into the texture PPApplyAO and ClusteredLightingWithSSAO
  read. Skip the Game's Shadow Maps (Experimental, off): under the rays' sun
  nothing is drawn into the cascades' atlas (learned at the mask's draw).
- **Global Illumination** (rays or paths, Experimental, Strength 0 = off):
  the lit scene right after ClusteredLightingWithSSAO prepared, traced by the
  helper, resolved over frames and added to it, times each pixel's diffuse
  colour as the game's IBL takes it (base colour x (1 - metal); not the sky,
  not unlit materials). **Path-Traced Ambient** (Experimental, not in RT/PT):
  DeferredSky's distant diffuse cube becomes the paths' sky (6 axis values);
  once the paths have learned the game's light, the sky's and the light
  probes' diffuse cubes are black and the SAO chain open: the paths bring the
  ambient light and its shade (the specular IBL stays the game's).
- **Ray Reconstruction** (DLSS, Experimental, off; with a ray tracing row on):
  the G-buffer guides written at ClusteredLightingWithSSAO's draw as
  DeferredSky lights the pixel (`rt_guides`: diffuse colour, specular colour
  through its LUT, world normal, roughness); the paths read them too.
- **Ray-traced reflections / specular occlusion** (Experimental, off): traced
  once a frame at ClusteredLightingWithSSAO's draw, after the guides, and
  blended into the IBL specular it reads (`rt_reflect_apply`, one / inverse
  source alpha): where the rays met a surface their light times the specular
  colour DeferredSky's IBL uses replaces the game's (sky and light probes
  alike); the game's IBL stays for the rest, times the open share under
  Specular Occlusion. The game's screen-space reflections (DeferredSSR and
  its combine) are left out under them: one method. Specular Occlusion works
  only with another ray-traced effect on (2026-11-18, Attila's: alone it
  started the whole ray tracing for itself); alone it is off, the game's look.

Without the helper (`screen_space.hpp`, Experimental, off until a game run):

- **GT-VBAO** (Ambient Occlusion's method; keep in-game SSAO on): PPSAO's four
  draws are not drawn and the reinterleave's draw runs `ao_vbao` (the
  visibility bitmask on LinearizeDepth's depth in PerView's camera, GBuffer1's
  normal) in its pixel shader's place: the game's two blurs, PPApplyAO and
  ClusteredLightingWithSSAO read it as the game's AO. Ray-traced AO and
  Path-Traced Ambient take the chain over: one method. Its Exact Depth Samples
  (Experimental, off; shs2_ 2026-11-17): each sample's depth at its own place
  between pixels where the four around it lie on one plane (twattila/bitmask.hlsli
  BitmaskPlaneDepth), so open ground seen at a low angle no longer shades itself.
- **GT-VBGI** (Global Illumination's method, Strength above 0 with the rays'
  bounce off): the Total War games' passes (`talesofarise_dlss/ue_light_bounce.hpp`;
  `bounce_gbuffer.hlsli` reads GBuffer1-3 and LinearizeDepth's depth through
  the main camera, the last frame's for its history) right after
  ClusteredLightingWithSSAO, from the scene it lit, added to it times each
  pixel's diffuse colour: where the rays' bounce goes.
- **Bend's screen-space shadows** (Shadows' method): Bend Studio's passes
  (`talesofarise_dlss/ue_screen_space_shadows.hpp`; `ue_sss_depth` on the
  G-buffer pass's D24S8 depth, `ue_sss_setup` on PerView and PerFrame's sun)
  after the game's sun mask is drawn, the occlusion multiplied into it: fine
  contact shadows over the game's maps. Under the rays' sun shadows the mask
  is theirs.

No RT/PT (Recommended) turns GT-VBAO, GT-VBGI and Bend on, as Attila's does.

Not yet: render scale below 100%, the game's settings above its maximum.

## Data runs and installing

- **Trace One Frame** (Advanced, Debug), `renodx_trace_now` next to the exe or
  `RENODX_TW_SET`'s `trace` key: `tales_frame_trace.txt` next to `EoCApp.exe`
  with every draw / dispatch, their formats, PPComputeLUT's cb0 (each LUT, HDR
  and the inventory's SDR) and PerView b12 at the sight pass.
- **Time Each Pass (One Frame)** (Advanced, Debug): every pass's GPU time of the
  next frame into `divinity2_pass_timing.txt` next to `EoCApp.exe`.
- A removed device (GPU reset): `twattila/device_loss.hpp` lets the old chain go,
  logs the reason and closes the game when no frame comes for 90 s with its window
  in front; the
  output stays HDR10 (no Output choice to decide).
- `RENODX_TW_SET` / `RENODX_TW_SHOT` (or `renodx_test_hooks.txt` next to the exe)
  set rows and take screenshots in scripted runs.
- ReShade: `dxgi.dll` in `DefEd\bin` today. Norbyte's Script Extender installs
  as `DXGI.dll` there too: where it is present, ReShade goes in as `d3d11.dll`
  (the game is D3D11), never over the Extender.

## Compare in game

1. Press **Reset** to put every image control at its default.
2. Use Paper White 203 and Peak 1000. For matching native UI, use its native
   brightness reference (300 in the supplied comparison).
3. Compare Off at game Brightness 1 / Contrast approximately 1 with Vanilla,
   including the inventory model. Modded HDR should remain independent of
   game brightness/contrast; SDR inventory intentionally retains stock controls.
4. Increase Vanilla Peak to 10000: highlights should expand again while the
   gray reference remains stable. Compare Vanilla+ using its Neutral presets.
5. Enable Fire Colour Correction and try its strength/hue on pink and orange
   fire, checking other bright red objects as well.

Closer to Native (PsychoV30) is an artistic variation of PsychoV30 (contrast and
cone response): press Reset before comparing defaults.

## Tests (offline, WARP)

- `test/tw_psychov35/part6_divinity2.ps1 -GameShaders <the game's cso folder>`:
  Off's t00 / t01 against the game's bytes on the traced constants, every
  mode's LUT, the SDR reference, PsychoV35's game-response fit.
- `test/tw_d2_passes/run.ps1`: the PCSS sun shadows (8 variants) Vanilla exact
  at Sun Shadow Softness 1 against the game's bytes, each with a wrong copy that
  must fail; at 2 the shadow must change.
- `test/tw_d2_helper/run.ps1` (after building the add-on): DLAA's camera hooks
  (R1b's PerView), the jitter, the motion and colour through aa_dlaa_prepare /
  finish, SMAA / FXAA giving way, the rays' writes read by the game's own
  PPApplyAO, the sun's direction from PerFrame and the overlays left out of
  the rays' scene, Skip the Game's Shadow Maps, and against the game's
  DeferredSky the paths' sky, its black diffuse under the paths and the GI's
  diffuse factor, the G-buffer guides against its own diffuse / specular,
  the reflections over its own IBL specular and the screen-space reflections
  left out (the game's DeferredSSRCombine bytes), GT-VBAO in the game's
  reinterleave's place (its depth channel the game's bit for bit, open ground
  and a corner) and the game's own blur over it, GT-VBGI by a lit wall and
  its view of the G-buffer (positions, normals, diffuse colour, the last
  frame's pixel), Bend's contact shadow by a wall in the sun's mask, each with
  a control that must fail.
- `test/tw_psychov30_nan`, `test/tw_output_encoding`, `test/tw_device_loss`,
  `test/tw_draw_dispatch`: the shared PsychoV30 guards, the not-configured
  Output, device loss and the fast draw dispatch.

- `test/tw_vanilla_exact/run.ps1 -Games Divinity2`: t01, bloom00, ui00 and
  the sight / outline passes outlines00-02 in Off against the game's bytes.

The game's shaders (`tools/nontw/divinity2/shader_dump`) stay outside the repo. Nothing here is
a substitute for a run in the game.
