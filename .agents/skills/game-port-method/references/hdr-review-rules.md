# HDR review rules

**Credit:** the RenoDX maintainers ShortFuse and Musa gave these rules in reviews of other modders' work.
boostedwarr wrote them down, together with lessons from his Batman: Arkham City port, and shared the notes with the
maintainer (2026-11-15). They are adopted here with that credit and rephrased; the maintainer's own additions say so.
Code the rules point to keeps its authors' credit (ShortFuse: PsychoV, Neutwo, the clip bend; Musa: PsychoV35, the
C-infinity roll-off).

Read before any tone mapper, LUT, bloom, hue, gamma or PsychoV change. `handle-sdr-tonemap-lut` owns the workflow
(proving the signal, choosing a strategy); where these rules are newer, they win.

## The tone map chain

- **One curve, once.** Each HDR tone mapper (PsychoV, Extended) runs once, on the untonemapped scene at the game's
  tone-map position. Never a curve on another curve's output. Divinity 1 ran UC2 Extended, then UpgradeToneMap, then
  PsychoV30 on top of each other (the maintainer, 2026-11-15).
- **SDR LUT bridge:** tone-map the max channel into the LUT's range, sample, then apply the inverse: scale by
  `max / roll_off(max)`, with a smooth shoulder that is the identity below its start. No `UpgradeToneMap` (and so no 2- or
  3-argument `ToneMapPass`) on clipped input. Never rebuild HDR by a per-channel division or an HDR / SDR ratio: both make
  bad gradients. Check an anchor change on many areas' LUTs, not on one scene.
- **Grading** blends in linear; the bridge's inverse applies there.
- **Gamma:** decode the game's output as sRGB or 2.2 (Gamma Correction). Don't carry a game's cooked write curve (a toe
  such as `min(8.4 x, sRGB)`) into the picture; keep it only where a game pass reads it back, such as an exposure
  histogram. A gamma 2.0 game was seen on 2.2 displays: decode its frame with 2.2 in every mode that shows it, and
  give every add mixed into the frame (lens flares, sprites) the frame's own gamma, so Vanilla keeps the game's add
  (Attila's and Thrones' flares were 0.035 off; the maintainer, 2026-11-16).
- **No clip at white in a redone add.** Where the port redoes an add the game made on its 8-bit image (bloom, a sun
  sprite, stacked additive sprites), don't clip the sum or the destination at white: the plateau it leaves can't be
  lifted. The Vanilla mode clips once, at the end.
- **Bloom:** the game's own add, unclipped in HDR. An energy-conserving lerp is only an option to measure. Over a
  thresholded bright pass it darkens the picture: Bloom 50 must come out brighter than Bloom 0 around lights.

## PsychoV calls

- Input: linear BT.709 at D65, or the call's source boundary for a wider gamut (AP1 in an Unreal ACES chain).
- Peak: `max(1, peak / paper white)`, pre-corrected for the active Gamma Correction, as `ToneMapPass` does.
- Gamut compression: BT.709 when the swap chain output is SDR, BT.2020 for HDR.
- Anchors: mid grey (0.18) unless anchor matching picks another point. The cone exponent is the game's own response
  around the anchor (1 where the game is the identity there).
- Compression's toe darkens shadows under the anchor: measure against Vanilla below white before keeping the default.
  Where the game's own toe and saturation are the look (an ACES game's darker shadows), PsychoV reproduces them,
  measured against the game's LUT on the traced constants, and keeps only its own highlights: Divinity 2's V35 came out
  with shadows 5x brighter and mid tones 23% paler than Off (the maintainer, 2026-11-16). No second curve for it: fit
  V35's own sliders (Contrast, Shadows, Saturation, Highlight Contrast) to the game's chain by minimax on WARP and
  ship them as the rows' defaults (Divinity 2: shadows x0.97-1.04 and chroma x1.03 of the game's).
- The output goes unclamped into the swap chain pass.
- Our PsychoV35 (Musa's, `twattila/psycho_test35.hlsli`) keeps this contract where its arguments match.

## Hue

- No Hue Correction and no Hue Shift after PsychoV: elsewhere it turned fire green. One exception, the legacy
  PsychoV31 mode (the maintainer, 2026-11-16): the TW add-ons' V31 Highlight Hue Shift moves the hue toward the clipped
  SDR colour after PsychoV31, above mid grey only (`twattila/hdr_to_screen.hlsli`), and every HDR preset writes
  `v31_highlight_hue_shift` 0.04. PsychoV35, the Recommended mode, follows the rule.
- Clip emulation (vanilla's clipped hue) is ShortFuse's clip bend, applied **before** the LUT. Reference = a per-channel
  Neutwo of the colour; move only the middle channel toward the reference's place between min and max; lerp by the
  strength; restore the luminance. Our Hue Clip Emulation is this bend.
- Because only the middle channel moves, a separate strength per channel order is seamless (sky blue bent twice as far
  in Arkham City). Whitening only far above white (4x-16x): from 1x it greys bright blues; cyan is the better failure.
- A tone mapper that scales each pixel by one factor (a max-channel roll-off) keeps hue by itself: no hue settings.
- Settings `renodx::draw` already has keep its names and meanings (Saturation = OKLab chroma). Shaders read settings
  through `shared.h` defines: `RENODX_*` for framework concepts, `CUSTOM_*` for the mod's own. A D3D9 mod names its free
  constant registers there.

## Targets and bad pixels

- **Vanilla exact first:** every replaced shader has an offline test against the game's own bytes, plus a deliberately
  wrong copy that must fail. Then any difference under the HDR modes is the mod's, not a porting error. These tests
  find real bugs: R.U.S.E.'s vignette had lost a `dp2add_sat`'s saturate, 0.13 to 0.30 off in 6 of 7 permutations
  (the maintainer, 2026-11-16); the suite (`test/tw_vanilla_exact`: 212 shaders, x86 and amd64) also caught three
  decompiler traps ([engines.md](engines.md)'s Shader sources).
- **Every 8-bit target between the HDR scene and the tone map clips HDR:** DOF and blur targets, a LUT strip, an AA
  pass's back-buffer-sized input. Upgrade narrowly (exact size and usage, not an aspect tolerance).
- **MSAA:** upgrade the MSAA surface and its resolve target together. Upgrading only one crashed Arkham City (RGBA8
  can't resolve into RGBA16F).
- **After an upgrade, list every draw that samples the target.** A bright pass with its own `min` read an upgraded DOF
  target and bloomed sparks red.
- **Float targets carry NaN and garbage** that UNORM used to clamp. Clean them where they break, and after the lerp,
  not before: some negative channels are valid (Arkham City's teal rocks turned brown).
- **Hot pixels:** trace them to their material and fix them there, not with a cap in a blur.

## Variants, content and tools

- **Settings and launcher switches swap in other uber or tone map passes** (motion blur on); some areas run their own
  LUT builder. Find them by diffing shader dumps after long play against a baseline, and by searching the disassembly
  for the LUT's indexing constant. Keep one shared `.hlsli` plus a thin file per hash.
- **Content or bug:** read the effect's constants first (a live cbuffer probe), and do the maths before trying variants
  in game. HDR makes content the game's clip used to hide stronger: compare with the unmodded game before calling it a
  bug. No caps picked by eye.
- **Same-frame A/B:** pause, hide the pause menu's draws with `discard` live shaders, then shoot every setting against
  Vanilla. Measure from `.exr` dumps or HDR shots: PNG previews add an sRGB encode.
- **Ask players about ReShade HDR effects** on top: they hide the tone mappers' differences.
- **A skipped swap chain:** UE3 creates its window with `WS_EX_TOOLWINDOW` and clears it only after `CreateSwapChain`,
  so the swapchain mod skipped the game's only chain. Clearing the style first fixed it. Check this in any Unreal game
  that gets no HDR chain.

## Page 1 and presets

The maintainer's rules (2026-11-16, from Attila's page); every HDR add-on follows them.

- **The game's Gamma and Brightness at their neutral values.** The game's own sliders change the picture before the
  port's decode, so page 1's first line names the values the port's maths assumes: Attila's "Use these in-game
  settings: Gamma 20 and Brightness 10." (its colour matrix assumes gamma 2.0 at in-game 20). Read them from what the
  game's slider writes (a step, an exponent, a percentage), never from another game; the TW values are in
  [game-facts.md](../../tw-port-checklist/references/game-facts.md).
- **Video AutoHDR, the one expansion allowed.** Only an 8-bit SDR source with nothing behind it (the game's
  pre-rendered videos), in that source's own shader, never the scene or the final frame (the rejected inverse tone map
  of `swapchain-resource-analysis`). Only luminance above mid grey, smooth from 0.18, one factor a pixel (hue kept),
  SDR white to lerp(1, Peak / Paper White, amount); HDR output only (`twattila/autohdr.hlsli`). Default 0 (Reset),
  Recommended 0.2 (Tales 0.25). Video Dithering (on, only while AutoHDR is above 0) adds one code value peak to peak of
  static interleaved gradient noise before the decode: the stretch shows the 8-bit steps; never temporal. In every TW
  add-on, Ryse, Tales of Arise and R.U.S.E.
- **Standard rows** where the game has the effect, Attila's as the model: VFX Brightness by particle family (Attila's
  three: flame, dust, clouds and smoke; blood, storms and fog; snow, rain and sparks; 1 = the game's), Unit / Building
  Indicator Brightness (the game's highlight markers, HDR modes only), Sky Debanding (taps weighted by colour plus
  4 code values of screen-space dither; off in every preset: too heavy), Modded Auto Exposure (off = the game's bits;
  on caps only the meter's maximum at the frame's average x 2^Sensitivity, the scene's colours untouched).
- **Presets keep the calibration.** Reset, Recommended and every condition preset (Attila's All Conditions,
  Day/Dry/Rain, Night/Snow/Fog) write the look rows only, never Peak, Paper White, UI, Output or Settings Mode; their
  tooltips say so. Only the SDR button writes Output (SDR). Check: every button in a page dump.
- **Output Auto's preset switch** (`twattila/output_encoding.hpp`; every HDR add-on, Ryse, Tales and Expeditions too,
  since [89d250f1]): with Output Auto, a first start without Windows HDR gets the SDR preset; Windows HDR coming on
  then applies Recommended, going off the SDR preset again, once per change, until the player changes any row (a hash
  of every non-global row after Auto's preset, `OutputAutoPreset` in the ini; "off" for good after a change). A first
  start with Windows HDR on never gets a preset. Check: the page dump's `== first start without hdr`, the log's
  "Output Auto:" lines.
