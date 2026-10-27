# Total War: ROME REMASTERED (RenoDX HDR, ATN)

Build: `cmake --build --preset clang-x64-release --target renodx-twromeremastered.addon64`.

## v5 (2026-10-22)

- Graphics (Experimental), Ambient Occlusion and their preset buttons: Advanced only (the round-4 check never hid
  them).
- Ambient occlusion Radius default 1 (ATN; Thickness stays 0.15).
- Video AutoHDR and Video Dithering at the end of Extra (next to the post-processing settings).
- No live counters (no CPU work per frame for them).
- Output: SDR (gamma 2.2, 10-bit) and Auto (scRGB with Windows HDR on, else SDR); default Auto.

## v4 (2026-10-22)

- PsychoV31 Highlight Hue Shift default 0.06 (ATN; Reset and the presets too).
- Bloom (as Attila): the high pass's hard cap of the bloom source (the brightest channel at cb0[0].z) is a logarithmic shoulder in HDR
  (bloom.hlsli; Bloom.hlsl PS_ApplyThresholdAndBloomDownsample, bloom01_0x8199AACD): bright sources no longer all get the same flat halo. SDR unchanged.
- (Extended's per-channel shoulder: v3.)

## v3 (2026-10-22)

- General: yellow note "Use these in-game settings: Gamma 50% and Brightness 100%."
- Presets: "Recommended (PsychoV30)" is now Recommended: Reset, then PsychoV31 at its defaults and Video AutoHDR 0.20;
  Reset turns Video AutoHDR off.
- Extra: Video AutoHDR / Video Dithering (cutscene00).
- HDR (Extended): the shoulder above 1 per channel (as Ryse fix32): bright fire turns yellow / white at its core as in
  SDR (the brightest-channel roll-off kept its full orange: flat, salmon).
- About: NexusMods button next to ATN's Ko-fi; every section title green, Graphics (Experimental) pink
  (settings.hpp default_section_tint / section_tints).

## v2 (2026-10-20)

- PsychoV31: Highlight Hue Shift 0 by default (ATN); a note above Highlight Saturation / Hue Shift / Saturation
  Shift ("Fire too pink, too orange or too pale? Change the settings below.", as Tales of Arise), Highlight
  Saturation moved next to them.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (Extended) / HDR (PsychoV30) / HDR (PsychoV31, new): the same PsychoV31 sliders as the
  other Total War mods (Musa's custom test31; Highlight Hue / Saturation Shift toward the SDR image's per-channel clip),
  on the same scene-linear handoff as PsychoV30.
- `resolve.hlsli`: ResolveMainColor (Feral's resolve: auto exposure + Hable, LUT, brightness, vignette, tint, dither),
  every permutation through one function with RMC_TONEMAP / RMC_LUT / RMC_VIGNETTE / RMC_TINT. t00 (the mod's original)
  plus t01-t11: FeralShaders holds 12 distinct ResolveMainColor shaders (names in
  `bin\cmd_Decompiler-1.3.16\dump_shader_twromeremastered_pack\names.tsv`, tools\tw\tw_feral_names.py). Tone mapping
  off (6 of them): the stored scene is SDR, shown at Paper White in every mode (the Vignette slider still works).
- fxaa01 (fxaa00's twin with luma computed in the shader): stock, registered for the 16-bit target.
- distortion01 (the heat haze's offsets-only permutation): Distortion slider.
- About: credits, ATN's Ko-fi.

Verify: SDR / Extended / V30 / V31 in a battle and on the campaign map; with vignette / LUT options changed; the game's
HDR / tone mapping option off (SDR at Paper White); FXAA on.
