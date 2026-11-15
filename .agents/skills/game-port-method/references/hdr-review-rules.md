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
  histogram.
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
- The output goes unclamped into the swap chain pass.
- Our PsychoV35 (Musa's, `twattila/psycho_test35.hlsli`) keeps this contract where its arguments match.

## Hue

- No Hue Correction and no Hue Shift after PsychoV: elsewhere it turned fire green.
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
  wrong copy that must fail. Then any difference under the HDR modes is the mod's, not a porting error.
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
