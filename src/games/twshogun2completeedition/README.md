# RenoDX — Total War: SHOGUN 2 Complete Edition (DX11)

### AO depth-level recovery (F15, 2026-11-09, GPT-6 Astra; not run in game)

Failed depth-level texture/view creation now releases all partial views and retries at the same resolution after a one-second cooldown. Build: `cmake --build build --config Release --target renodx-twshogun2completeedition.addon64`. Manual check: use an AO mode that reads depth levels, change resolution/render scale, and verify stable AO; after a transient allocation failure, depth levels should recover without another resolution change. Failure injection and real D3D11 WARP readback results are in `docs/Attila-crash-audit-2026-11-08.md` at the repository root. No shader behavior was changed.

The 2026 Complete Edition ships its shaders' HLSL source and every compiled permutation in `data\shaders.pack`
(extracted without playing: `bin\cmd_Decompiler-1.3.16\dump_shader_shogun2_pack`, README there). The replacements are
written from that source: the 3Dmigoto decompile of the tonemap collapses swizzles (it loses the white clip and
the shoulder, like Troy's / Pharaoh's).

## The game's pipeline (hdr_to_screen.hlsl, MTB rewrite WAS-459 / WAS-1719)

Scene (FP16) + bloom -> XYZ x brightness^2 (auto exposure, camera fade) -> white balance (cg_color_temp, then to
6000 K) -> AP1 -> UE4's filmic curve (FilmToneMap: slope, toe, shoulder, black / white clip; per channel; its toe
point multiplies by slope through a comma operator, kept) -> 3-way color grading by luma -> D65 -> BT.709, saturate,
x g_brightness (1.2), gamma 1 / 2.2. Not Reinhard: `reinhard()` (Rome 2 / Attila / Warhammer's curve) is still in
the file, unused. Troy / Pharaoh use an ACES fit, Rome Remastered Hable.

The game's HDR (`g_hdr_display`, R10G10B10A2 swap chain): the SDR result, its luminance stretched linearly from mid
gray (Y 1 -> peak / scene nits), x scene nits, BT.2020, PQ. The UI (`ui_resolve`) is converted to PQ at the UI nits
and blended in PQ (semi-transparent UI much darker than in SDR). Peak, scene and UI nits are menu settings.

## The mod

The game turns its HDR on by itself on an HDR display (gfx_hdr in preferences.script.txt is set back to true;
the menu has only its two nits sliders). Not needed off: only hdr_to_screen and ui_resolve read g_hdr_display, both
replaced; the swap chain is RenoDX's. RenoDX swap chain proxy (FP16, HDR10 or scRGB), 8-bit / 10-bit targets at back-buffer aspect
upgraded to FP16. The tonemap writes gamma 2.2 x Paper White / UI, the UI stays the SDR copy (gamma 2.2, 1 = UI
white), the proxy decodes and scales by UI.

| Mode | Tonemap |
|---|---|
| SDR | Stock, at Paper White. |
| HDR (Extended) | Default (v3). marat569's UE Filmic Extended as in Expeditions: Rome / Tales of Arise, on the game's curve (no RRT or blue correction: the game has none): the curve below mid gray, its tangent at mid gray above, 37.5% back to the curve, the game's grading, then a per-channel C-infinity shoulder from the curve's mid gray to Peak (AP1). Same as SDR up to about scene 0.5; scene 4 -> 504 nits (SDR 203) at Paper White 203, Peak 2500. v1-v2: the curve's shoulder asymptote raised to Peak (fire about 300 nits: too little). |
| HDR (PsychoV31) | PsychoV31 on the white-balanced scene, anchored on the curve's mid gray (0.087), its contrast x the curve's log-log slope there (1.40: the game's darker shadows; v1-v2 used 1, gray), then the game's grading. Recommended (bloom 0.5). |
| HDR (PsychoV35) | Recommended (2026-11-12). Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`), anchored as V31 on the curve's mid gray with each map's contrast; Hue Clip Emulation (default 70) toward the game's curve on each AP1 channel, clipped; the game's grading after it. Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain (UE's film curve). Built only, not run in game. |

Both HDR modes: the game's gamma (menu Gamma / 10 = g_gamma_output, 2.0 by default) shown as SDR shows it, x^(2.2 /
gamma) below 1, its tangent above (v1-v2 left it out: HDR mid tones 25-35% brighter than SDR); the menu's Brightness
/ 10 (g_brightness) at 10 (x 1) in every mode since v4 (ATN: the menu's 5-20, 12 by default, is an exposure;
Exposure and Paper White replace it); limit to Peak in BT.2020.

Extra (HDR, v4): Bloom (the tonemap's bloom target), Lens Flares (hdr_lenseflare's ghosts and lenseflare's sun flare
sprites), Vignette (lenseflare ps30_vignette, game option Vignette), Heat Haze (the tonemap's distortion offsets, game
option Distortion Effects), Radial Blur (its strength and brightening). The game has no film grain, sharpening, motion
blur, god rays or chromatic aberration. Not yet: depth of field (depthoffield.hlsl cs_coc_bokeh_high / low 0xB25F26C7
/ 0xB333A1B3: scale the circle of confusion), battle fog (battlefog.hlsl extinction 0xA61D2853 / inscattering
0x4355E897, needs fog.hlsl), the sun (sun.hlsl 0x3E33E81A).

The game's constants (data.pack `*.lighting`, LZ4: `<TONE_MAPPING ... film_slope='0.88' film_toe='0.5'
film_shoulder='0.13'/'0.14' film_black_clip='0' film_white_clip='0.04'>`): with the toe's comma operator mid gray 0.18 ->
0.087 (UE4's correct curve: 0.18), so below white the image is about a stop darker than a UE game at the same Paper
White: mid gray about 17 nits at 203. The game's own HDR put its scene nits (300 by default) on the curve's output
without gamma or Brightness: mid gray about 26 nits. Paper White about 300 matches it.

| File | Shader |
|---|---|
| `tonemap00_0xEF060C7C` | hdr_to_screen ps30_main_high / _sm40 (with or without depth of field). |
| `tonemap01_0xBEDBEFD8` | The same with ENABLE_DISTORTION (distortion offsets, radial blur: its 10 taps read the same pixel, a game bug, kept). |
| `uiresolve00_0x3D56DB58` | ui_resolve ps40_copy: always the SDR copy. |
| `hdrflare00_0x3208750E` | hdr_lenseflare ps30_main_lense_flare: bloom x g_bloom_amount + 7 flare ghosts (its mip = texcoord.y, a game bug, kept). |
| `flare00`-`flare03` (`0xD6708726`, `0x5C6F73D3`, `0x7645BCB0`, `0xE23F06FF`) | lenseflare ps30_flare_0-3: the sun's flare sprites. `flare.hlsli`. |
| `vignette00_0x658E9237` | lenseflare ps30_vignette. |
| `doffocus00_0x77F0657A` | depthoffield.hlsl cs_compute_focal_depth (render scale: focus points in the viewport). |
| `dofblur00_0x6449D186`, `dofblur01_0x32876DB2`, `dofblur02_0x6531CCC3` | summedareatable.hlsl cs_blur_hashed / cs_masked_blur_hashed / cs_blur_hashed_unorm (render scale: radius x scale, taps inside the rendered part). `dofblur.hlsli`. |
| `ssrcopy00_0x962CEB71` | environment_specular_light ps40_copy (render scale: read at the pixel). |
| `highlight00_0x5EAD0FF6` | gbuffer_highlight ps_main (render scale: read at the pixel). |
| `sss00_0x34ECD06E`, `sss01_0x1B529672` | screen_space_shadows cs with / without the cascades (sun shadow mask: Bend screen-space shadows; render scale). `sss.hlsli`, `bend_sss_gpu.hlsli`. |
| `sss02_0x78E88E1D` | screen_space_shadows cs_passthrough (screen-space shadows off: cascades only), the game's half-pixel double offset fixed. `sss.hlsli`. |
| `ssrhiz00_0x97624E56`, `ssrray00_0x5C0AF498`, `ssrfilter00_0x13C66077`, `ssrfilter01_0x073A6CF3`, `ssrresolve00_0xF87ECE85` | the reflections' HiZ copy, raycast, hit blur x / y, resolve: SSR Resolution. `ssr.hlsli`. |
| `ssao00_0xA00FE3A3` | ssao_3k cs50_new_sweep (render scale: view positions over the viewport, far depth outside). |
| `pointlight00_0x5CBF4FB6`, `pointlight01_0x57139F8D` | pointlight ps30_main_highdef / lowdef (render scale: viewport from the camera). |
| `particlelight00_0x70CAA144` | particle_light ps30_main (render scale: viewport from the camera). |
| `ribbon00_0xA583B6F4` | particle_ribbon volumetric_ribbon_pixel_main (render scale: viewport from the camera). |
| `autoexposure00_0x8233E11F` | autoexposurecs cs_compute_tone_mapper_brightness (render scale: points and crosses in the rendered part). |
| `fog00_0xA61D2853`, `fog01_0x4355E897` | battlefog ps40_main_extinction / inscattering: HDR x Battle Fog (extinction ^ value, haze x value). |
| `sun00_0x3E33E81A` | sun ps30_main: HDR x Sun. |
| `video00_0x8BCB52BB` | sprite pixel_yuv (the videos, movies*.pack, drawn as UI): at Paper White instead of UI (as Pharaoh); HDR: Video AutoHDR + Video Dithering (as Ryse). |
| `tonemap.hlsli` | The shared tonemap. |

## Verification

Build `renodx-twshogun2.addon64` (clang-x64-release). In game: ReShade.log shows the
three replacements; SDR mode should match the game without the mod; check menus, semi-transparent UI panels and
the campaign map. Untested: everything (first build).

## Render scale (with the DLSS addon, v5)

The DLSS addon draws the 3D passes into the top-left render scale of the full-size targets (twshogun2completeedition_dlss). Like
Warhammer 3's (its fixes: twwarhammer3 dof00 / dofbg00 / doffocus00), the depth of field counted in full-size texels
(Ultra Performance: 3x the blur, ATN) and its auto focus read depth outside the picture; the tonemap's distortion
texture is drawn with the 3D passes but was read at full-size coordinates (debug3: a black stroke ending at x 1280 =
the render width at 33%). The rendered fraction (SI.render_scale_x / y) is captured on the game's deferred light copy
(0x962CEB71) and battle fog (0xA61D2853, 0x4355E897), as a viewport over a back-buffer-size target, and reset to 1
after the tonemaps. The auto focus uses the camera's g_viewport_dimensions (the DLSS addon patches it).

## v6: render scale fixes (debug4), sliders

ATN at 33% (debug4): a small copy of the river in the sky (top left), extra shadows over the cascades' shadows, fire
glow smaller. Audit of a frame (tools\shogun2\scale_audit.py on debug3's trace) and the game's sources: the engine
mostly respects a viewport smaller than its targets (screen_size = the target, g_viewport_dimensions = the viewport,
patched by the DLSS addon), except:
- 0x962CEB71 (the reflections' copy onto the scene) and 0x5EAD0FF6 (highlights) read with the full-screen quad's 0-1
  texcoord: the whole full-size texture squeezed into the picture (the river copy = its reflections, none on the river).
- 0x34ECD06E (sun shadow mask): the game builds Bend's light coordinate and dispatches for the full-size screen; its
  rays marched full-size pixels over the rendered depth (wrong direction / length = the extra shadows). Rays stay in
  full-size pixels, read the depth in the rendered part, each rendered pixel written once (integer middle pixel).
- 0xA00FE3A3 (AO sweep): view positions from the screen UV (squeezed view). Now over the viewport; far depth outside.
- Point lights, particle lights (fire light), ribbons: the CPU's g_viewport_offset_scale stays full screen; the
  camera's viewport instead.
- 0x8233E11F (auto exposure): the focus crosses over the whole (stale) texture, like the depth of field's focus.
Checked and left: directional light, battle fog, water, motion vectors, HiZ, SSR resolve (viewport-aware), soft
particles (only depth), SSR raycast (rays past the rendered part meet the cleared far depth: a miss).
Sliders (HDR, Extra): Depth of Field (blur radius), Battle Fog (density and haze, its distance blur follows), Sun (disc);
Heat Haze renamed Distortion.

## v7: every permutation

Each fix covers every permutation the pack ships of that entry point (shader_names.txt: file, defines, entry point per
hash); v7 adds the screen-space shadows without cascades (0x1B529672) and the 8-bit hashed blur (0x6531CCC3). Static
scans of all 1350 decompiles: the only users of g_viewport_offset_scale are the fixed point / particle lights and
ribbons (volumetric particles and screen splats declare it unused); screen UV -> clip without the viewport only in
those and in soft particles / indicators (eye depth only: fine). Not fixed, engine references them but debug3 (max
settings) runs the newer ones: the old AO path (cs50_sweep / cs50_collect) and the summed-area-table DOF blurs
(cs_blur, cs_blur_unorm, cs_masked_blur, MTB's hashed blurs replace them). A trace at the lowest AO / DOF settings and
one on the campaign map show whether they run.
Videos (v7): sprite.hlsl pixel_yuv draws them into the UI target, so they followed UI brightness; now Paper White
(twpharaohdynasties cinematics00) and Video AutoHDR / Dithering (rysesonofrome video00; Recommended 0.25). The pack has
no sharpening, chromatic aberration or film grain (searched names and sources); TAA's Catmull-Rom history is its only
"sharpness".

## v8: Grass Fade (removed in v9)

ATN debug6 (also vanilla): where the grass ends, the band flickers when the camera moves. The grass's alpha cut-out and
its distance dissolve (rigidgrasscommon.hlsl: dissolve x dither >= 0.25, gone from 0.75 / inv_lod_distance, 75 m with
the shader's default 0.01) clip against a 64x64 dither fixed on screen (load_jitter), which DLSS / TAA cannot average.
v8 tried Grass Fade (the dither shifted by the camera jitter each frame): DLSS does not settle grass whose coverage changes
every frame (debug7, DLAA, paused: the grass smeared and flickered; ATN). Removed in v9.
The grass distance itself is the CPU's (scrub node grass_lod_distance / grass_feather_distance, grass_PS inv_lod_distance
+0x1a17133): not changed.

## v10: shadows with the screen-space shadows off, SSR resolution

ATN debug8 at 33%: screen-space shadows on fixed the rock; off, the rock's shadows broke while moving (AO on or off: the
same). Off, the game runs cs_passthrough (0x78E88E1D): ReadShadowMap(coords + 0.5), which adds 0.5 again: each pixel
got the cascades of its diagonal neighbour, from that neighbour's depth at its corner. Half a display pixel at full
size, 1.5 at 33% (acne on grazing rock). sss02 uses the pixel's own centre (also at full size: a game bug).
SSR (ATN: flickering at 33%): the game traces at half its render size (k_buffer_size = screen / 2, HiZ base level =
2x2 min); with the render scale that was 640x360 at 33%. Its buffers are half the display size, so at 50% or less the
render size fits: SSR Resolution Full (default) traces 1:1 (SSRFactor() 1 instead of 2 in the HiZ copy, raycast: 2x2
G-buffer gather, clip from pixel, k_buffer sizes, bound at the rendered part; filters' clamp; resolve's hit texture
coordinate x 2 / F). Above 50%: the game's.

## v11: Grass Distance Fade

The flicker where the grass ends (vanilla, debug6): the distance dissolve (1 - distance x inv_lod_distance) x a 64x64
dither fixed on screen < 0.25 is clipped, so moving the camera slides the grass under the pattern. v8's per-frame shift
let DLSS smear the grass (debug7). Stable (default): the dissolve's threshold is a hash per 0.5 m world cell (v2 =
world_position), so patches drop out at their own distance and stay put; the same uniform spread keeps the vanilla
fade. The blades' alpha test keeps the screen dither. Input declarations fixed to the game's widths (also ribbon00).

## v12: Extended user grading

HDR (Extended) sliders (Advanced, as rysesonofrome): Exposure, Highlights, Shadows, Contrast, Saturation / Purity,
Highlight Saturation: PsychoV31's grade (custom_psychograde_test31, mid gray 0.18) on the untonemapped scene before the
game's curve; all at 1: skipped (the game's look). Hue Correction (0-1, default 0 = the game's per-channel hue): after
the curve, toward the graded scene's hue (renodx Hue, OKLab), before the game's grading and the shoulder.

## v12 result (debug9), v13

ATN: rock shadows with SSS off fixed at every scale; SSR Full at 50% or less "phenomenal", above it (and at DLAA,
vanilla) still flickering; Grass Stable left square holes; a flowering bush unstable with jitter (even DLAA); shadows
look fatter at 33% (SSS off).
- Grass: v11 put the whole 25% that vanilla's dither drops up close into world cells (holes). v13 keeps the screen
  dither's constant 25% (dither >= 0.25) and gives only the distance part the cell value (0.25 m, 0.25-1): the game's
  density at every distance, stable band.
- SSR Full above 50%: SSRFactor() = render / min(render, display / 2) (1 to 2, non-integer): the half-display buffers
  filled (67%: 1920 instead of 1267 wide). HiZ copy: min of the 2x2 render texels from floor(texel x F); raycast gather
  from floor(texel x F), ceil(F) per axis. At 100% the game's (F 2); more there needs bigger buffers.
- Shadows at 33% (SSS off): one cascade lookup per render pixel (9x fewer than at 4K), rebuilt by DLSS over frames.
- Flowers: open (trace needed: which shader draws the bush).

## v13 result (debug10), v14

- Grass Distance Fade removed: ATN saw no real difference moving the camera (debug10); the grass popping in and its
  closer LODs are the CPU's (grass placement, scrub node grass_lod_distance), not the dither.
- Shadows thinner / with more holes below 100% (SSS on and off, debug9 / debug10): the DLSS addon's texture LOD bias
  also reached the shadow maps (the trees' leaves cut sharper alpha holes). DLSS v10: only in the main view.
- Flowers / tree leaves unstable with jitter (paused): motion vectors checked (rigidleaf_v3 0x09117812: the same
  jitter terms as the static path, prev wind time), leaves dissolve in texture space; open (alpha-tested petals and
  needles thinner than a pixel; preset L forced by the NVIDIA app; LOD bias Sharper).

## v15

SSR Resolution moved to the DLSS addon's overlay (graphics options there, ATN); this addon reads ReShade.ini
[Shogun 2 DLSS] SSRResolution every frame (OnPresent, default Full).

## v16: plant fade pattern stable with jitter

ATN (debug11, DLSS v12): flowers and leaves still flicker with jitter at any preset, LOD bias or particle mask. The trace
now lists every target: the leaves' motion vectors are written (G-buffer rt3, cleared each frame) and the engine adds
the jitter to the projection as the shaders expect (+0x19a42b4: x + jx w, y + jy w; g_camera_jitter from the same
camera fields at +0x1a92a85). What is jittered on screen: get_surface_colour's dissolve (a fading mesh, g_dissolve_enable,
clips by s_dissolve_map at the jittered clip position x screen_size / 64), used by rigidstandard ps30_gbuffer_alpha_test
(0xAECABA3F) and rigidvertexwind ps30_gbuffer (0x557A56D5), both in the flower and tree traces. dissolve00 / 01 look the
pattern up at SV_Position / 64 (the same texels as the game without jitter at full size). The forward variants
(0xED622011, 0xA406D4BE, 0xE16A1D78) never draw in the battle traces. Leaves (rigidleaf_v3), branches, tree billboards
and imposters dissolve in texture space, grass on the pixels: stable already.

## v17: Foliage Backlight

ATN (debug13, every setting maxed): the meadow is covered in white specks that flicker, only facing the sun (gone with
the sun behind the camera), SSR and SSS on or off alike. lighting.hlsl standard_lighting_model_directional_light gives
IS_FOLIAGE pixels the sun's backscattering: extinction plus Schlick phase inscattering (g 0.8, peaked toward the sun),
the inscattering with distance = 0.001 / max(0.1, dot(N, L)) x (dot(N, L) < 0): a step from 0 to full where a blade
turns away from the sun, so neighbouring blades flip between dark and bright. sunlight00-03 (directionallight.hlsl
ps30_main_highdef 0xED1BED93, used every battle frame; lowdef 0x5496DE73; overlay 0xC3E0E939 / 0xD5FDDE8F) scale the
sun's backscattering by Foliage Backlight (Extra, 0-2, 1 = the game's); the ambient backscattering is kept. Decompiles
recompiled against the originals: same signatures and constant reads (the decompiler compares the bool
g_use_spherical_harmonics instead of testing it; int / float casts around the spherical harmonics loop).

## v18: Foliage Sun Glints, Foliage Reflections

ATN: Foliage Backlight changes the trees' leaves (and a few leaves of the traced plant), not the meadow's specks: the
grass's normals face up, toward the sun, where the backscattering's inscattering is off. Next suspects, both on
IS_FOLIAGE pixels (flags & 128, decoded before them) in sunlight00-03: the sun's specular (get_reflectivity_dir_light x
sun_colour, added once at the end) x Foliage Sun Glints, and the environment reflectivity (get_reflectivity_env_light,
the probe's multiplier; SSR is a later pass) x Foliage Reflections. Extra, 0-2, 1 = the game's.

## v19: Foliage Sun Glints only, Highlight Hue Shift

ATN: Foliage Sun Glints at 0 removes the meadow's specks; Foliage Backlight and Foliage Reflections are right at 1:
both removed (sunlight00-03 keep only the sun's specular x Foliage Sun Glints on IS_FOLIAGE pixels).
Highlight Hue Shift (as Tales of Arise) in both HDR modes, Extended 0.30 and PsychoV31 0.24 by default: above mid gray,
fully from scene white, the hue moves toward the game's SDR image's (its curve and grading on the same input; OkLab,
brightness and chroma kept). Extended's Hue Correction is removed.
The DLSS addon's SSRResolution is read when the addon attaches and while ReShade's overlay is open (each read checks
ReShade.ini's file date) instead of at every present.

## v20: tree motion vectors, VFX brightness, SSR ray steps, shadow samples; Radial Blur removed

- Tree motion vectors (the flowering bushes, ATN debug14 / debug15): the tree format's vertex shaders
  (rigidleafcommon_v3 / rigidbranchcommon_v3) blend two instance transforms with half-precision bone weights:
  s = position.w x (weight0 + weight1) is not exactly 1. They draw the homogeneous (s p, s) (= p) and pass the previous
  position as (s p_prev, s); the pixel shaders read it with w = 1, s p_prev, off by (s - 1) p with p in world space
  (hundreds of meters). The DLSS addon's capture (paused): 10% of the pixels, all on trees, moved by up to 1000 px,
  signs per branch, the same every frame, so DLSS dropped the trees' history every frame (they flickered with jitter,
  and were still without it). treevelocity00-03 (rigidleaf_v3 0x09117812, rigidleafwet_v3 0xB670BCA1, rigidbranch_v3
  0x29EB778A, rigidbranchwet_v3 0x3A9E203D, ps30_gbuffer) read the previous position with its w. Decompile fixes: input
  widths, the flags compare (g_untextured, snow), -pi. Recompiled against the originals: same opcodes and constant
  reads, TEXCOORD7.w now read. Billboards and units compute theirs otherwise.
- Fire & Glow / Smoke & Dust Brightness (Extra, HDR, 0-10, 1 = the game's; as twpharaohdynasties' VFX boost):
  effects_overlay (0x3C9BC2F9, vfxoverlay00) composites the particles' buffers, the additive one (fire, sparks, glows)
  and the alpha-blended one (smoke, dust); the addon picks the slider from the draw's blend state (dest ONE: additive)
  into SI.vfx_brightness before the draw.
- SSR ray steps (the DLSS addon's SSRSteps): ssrray00's HiZ traversal gives up after 100 steps (then the probe is
  reflected): x 1 / 2 / 4.
- Shadow samples (SSSSamples): Bend's screen-space shadows reach SAMPLE_COUNT pixels, the game's 120 (at 4K half
  the 1080p reach) or 240. Both are compiled into sss00 / 01 (WriteScreenSpaceShadow's sample count is a literal at
  each call: unrolled, no indexed temporaries; a branch on the constant).
- Radial Blur slider removed (the game's effect as it is). Recommended: Foliage Sun Glints 0, Bloom 1.

## v21: finer VFX sliders (Extra (Experimental)), SSR / SSS further, hue defaults

ATN: the plants are fixed. VFX sliders move to "Extra (Experimental)", all named "VFX Brightness: ...", finer:
- Fire & Glow: the additive particles' own shaders (vfxglow00 particle.hlsl ps30_volumetric_particle 0x40744308,
  vfxglow01 particle_screen_splat 0xF70E8B73; recompiled against the originals: same signatures and constant reads,
  the spherical harmonics loop's int / float casts).
- Trails: ribbon00 (0xA583B6F4, drawn into the same additive buffer).
- Firelight: particlelight00 (0x70CAA144), the light particles cast on the scene.
- Smoke & Dust (brightness) and VFX Opacity: Smoke & Dust (ATN: black smoke did not change; it is dark, its look is
  its opacity): the effects overlay's smoke draw (blend one / source alpha: premultiplied colour and transmittance T):
  T^k (Beer-Lambert), colour x (1 - T^k) / (1 - T). The additive overlay draw is left alone.
Shadow samples to 960 (120 / 240 / 480 / 960, all compiled: 8263 instruction slots, unrolled, 4.3 KB groupshared),
SSR ray steps to 16x. Highlight Hue Shift defaults (ATN): Extended 0.45, PsychoV31 0.38.

## v22: banner motion vectors, projectile trails, glowing meshes, PsychoV31 highlights

- Banners and flags flickered while paused (ATN debug16, Ultra Performance: a near banner's motion vectors pointed
  300-590 px away, the same every frame; a far flag 1 px). cloth.hlsl is simulated on the CPU and passes the previous
  step's positions (prev_position); paused, the simulation stops but keeps the last step's, here a jump. clothvelocity00
  (vs30_main 0xBDFF60D4, written from the source; recompiled: same signatures) uses the current position while the
  scene's time stands still (time_in_sec == prev_time_in_sec, as paused).
- VFX Brightness: Trails now also scales projectiletrail.hlsl (0x03F58817: arrows, bullets, artillery; drawn into the
  scene, not the particle buffers; written from the source, the decompile merges its packed inputs; its distortion pass
  is left alone). Fire & Glow also scales rigidglowcolour / rigidglowtexture (0xBA0315BE / 0x546A3049: burning
  buildings' fire panels). Not reachable without a decompiled G-buffer shader: rigidburning's ember emissive.
- PsychoV31: its contrast x the curve's (1.40 at mid gray) also lifted its highlights by up to about a stop over the
  game's curve (+0.7 at 8x mid gray, +0.85 at 32x; ATN: overblown). V31's highlight contrast 0.86 brings them back to
  Extended's within 0.1 stop from mid gray to 128x (tools\shogun2\psycho_fit.py: V31's tonal grading against
  Extended); the shadows keep the game's contrast.

## v23: PsychoV31 with its own contrast again

ATN: no contrast match. PsychoV31's contrast is its own again (Contrast 1 = V31's), its highlight contrast 1: v22's
match of the game's curve (x 1.40 at mid gray) lifted the highlights and needed highlight contrast 0.86 against it.

## v24: Bloom without the flares, Cone Response Exponent, Firelight on point lights, Recommended without flares

- Bloom scaled the tonemap's whole bloom input, which hdr_lenseflare (hdrflare00) fills with the bloom and its lens
  flare ghosts: Bloom changed the flares too (ATN). Bloom now scales only the bloom there.
- Recommended: Lens Flares 0 (ATN: ghosts on every light).
- PsychoV31 Cone Response Exponent (as ruse's psychotm_test31): the tonal power is Contrast x the exponent and the
  purity is divided by Contrast, so Contrast keeps the saturation and the exponent changes it.
- VFX Brightness: Firelight also scales the deferred point lights (pointlight00 / 01: fires, explosions, torches,
  lanterns). The particles' own lights (particlelight00) are rare: 1 draw in debug16's burning scene, 23 point lights.
- VFX sliders in 0.1 steps.

## v25: shadow samples to 3840, SSR ray steps to 64x

ATN: Shadow samples up to 3840 (120 x 2^0-5), SSR ray steps up to 6400 (100 x 2^0-6). Bend's screen-space shadows
unrolled every sample count (960 and below: 8263 instruction slots); 3840 that way would be tens of thousands. Its loops
now run at the sample count given at run time, each depth read written to groupshared in the read loop (Bend kept 4
arrays of reads per thread for a second loop), the main samples 4 per iteration into the same 4 shadow values: Bend's
results, 463 instruction slots, 10 temps, groupshared 3968 floats (15.5 KB).

## v26: heat haze smooth below 100%, ATN's presets, SSR ray steps removed

- ATN debug17: a mosaic over a campfire with every VFX slider at 0: the heat haze (Extra: Distortion; the game's
  Distortion Effects). Its distortion texture is drawn at the render size; below 100% each texel covers a block of
  display pixels and the game's point sampler gave each block one offset. tonemap01 now reads it bilinearly there
  (at 100% the game's sample).
- Presets (ATN): Recommended = HDR (PsychoV31), Lens Flares 0, Foliage Sun Glints 0.15, Video AutoHDR 0.25, Highlight
  Hue Shift 0.25, VFX 2.5 / 2.0 / 1.8 / 2.0 / 1.0. Recommended (SDR-like Saturation) beside it: the same with Cone
  Response Exponent 1.20, Highlight Hue Shift 0.35, VFX 1.2 / 3.0 / 2.0 / 2.0 / 1.0. Reset on its own line below.
- Highlight Hue Shift defaults: Extended 0.20, PsychoV31 0.25.
- SSR ray steps removed (ATN: no visible or performance change): the game's 100 steps.

## v27: shadow stability, shorter tooltips

- ATN: some screen-space shadows flicker up close, also without the mods. The game uses Bend's defaults (surface
  thickness 0.005, bilinear threshold 0.02, shadow contrast 4: tweaks sss_* at +0xd4080..+0xd4320). Contrast 4 turns a
  depth match into full shadow within a quarter of the window, so shadows switch fully on / off between frames. Shadow
  stability (DLSS addon, ReShade.ini SSSStability): Steadier halves the contrast (Bend's other recommended value, 2),
  Steadiest also sets Bend's IgnoreEdgePixels (depth edges cast no shadow; thins some foliage shadows).
- "Recommended (SDR-like Saturation)" renamed "Recommended (Native-like Saturation/Contrast)".
- Tooltips shortened, for players.

## v28: heat haze steady below 100%, shadow softness, heat haze slider with the VFX

- ATN: below 100% the heat haze moves left and right and looks low quality. Its distortion texture is drawn in the 3D
  passes with DLSS's jitter, but tonemap01 applies it to DLSS's steady output: the haze moved by up to 1.5 display
  pixels every frame at 33%. The DLSS addon (v24) shares each evaluation's jitter (dlss_jitter.hpp, cross_addon);
  tonemap01 reads the texture where the pixel's scene point was drawn, with a cubic B-spline over the render-size
  texels (smooth at any scale). At 100% the game's read.
- Shadow softness (DLSS addon, ReShade.ini ShadowSoftness): the cascades' 3x3 filter replaced by a separable tent of
  radius 2.5 / 3.5 shadow-map texels (4 x 4 comparison gathers) for shimmering sun shadows with SSS off (and the
  cascades under SSS).
- The heat haze slider (heat_haze) moved to the VFX sliders: "VFX Distortion: Heat Haze".

## v29: VFX Brightness: Dim Fire & Glow

ATN: house fires reach Peak at Fire & Glow 1 while fire arrows, torches and other glows stay dark. All are the same
additive particle shader (their emissive strength is per effect; no effect data ships with the game, and house fires
are bright where many particles overlap), so the split is by brightness: the effects overlay's additive draw scales the
summed glow by 1 + (k - 1) p^2 / (y^2 + p^2), p = 2 scene-linear (x k when dim, x 1.04 at 10). Every VFX pixel shader
permutation in shaders.pack is covered (overlay, volumetric / splat / ribbon / light particles, projectile trails,
glow meshes, point lights, distortion via Heat Haze) except rigidburning / rigidburningpanel's ember emissive (G-buffer).

## v30: heat haze in the depth of field, VFX effect test, hue defaults

- ATN: haze stayed over burning houses at Heat Haze 0. With depth of field on, depthoffield.hlsl cs_coc_bokeh
  (0xB25F26C7 high / 0xB333A1B3 low) reads the scene through the distortion texture too (distort_pixel_index, whole
  pixels: only strong haze shows) and the blur passes write it over the scene before DLSS; the tonemap distorts it
  again. dofcoc00 / 01 (dofcoc.hlsli, written from the source) scale the offsets by Heat Haze and by the render scale
  (the offsets are screen UV, counted in full-size pixels: 1 / scale too strong below 100%).
- VFX Effect Test (Advanced, vfx_effects.hpp): ATN wants house fires apart from the other fires (Dim Fire & Glow brightens
  house fires too much). The volumetric / splat particles' effect constants (scripted_effect_info_constants_VS, b3 / b2:
  colors, emissive, animation; wind left out) hashed into an ID (CRC32) that stays the same between sessions, read at
  the game's Unmap of the buffer seen at a particle draw (read back once before that). Glow meshes: IDs 1 / 2. Hide one
  effect or show only it, previous / next effect on screen; the overlay shows its ID and emissive range. Texture IDs
  would not work: most fire draws share one 1024x1024 texture (debug16 trace).
- Highlight Hue Shift defaults (ATN): Extended 0.33, PsychoV31 0.42.

## v31: fire by effect, Distortion and Haze in Extra, one Recommended, shadow map stability

- ATN (v30 test): the haze over burning houses is gone. The haze slider is back in Extra: "Distortion and Haze".
- Fire & Glow per effect (ATN's VFX effect test IDs): burning houses 9FC96955, campfires FE957C04
  (vfx_effects.hpp). vfxglow00 / 01's draws take SI.vfx_glow_draw, set per draw from the effect: VFX Brightness:
  Burning Houses (default 1), Campfires (default 1, Recommended 5), Fire & Glow (the rest). The effect is looked up only
  in HDR when those sliders differ (or in the test). Glow mesh rigidglowcolour (burning buildings' fire panels) follows
  Burning Houses. Dim Fire & Glow removed.
- "Recommended (Native-like Saturation/Contrast)" removed (ATN); Recommended adds Campfires 5.
- Shadow map stability (DLSS addon v25, ReShade.ini ShadowMapStability): the cascades (SSS off, and under SSS) shadowed
  surfaces the sun grazes in patches (the filter reaches across the slope: acne that changes with the camera). The
  receiver moves along its normal (from the neighbouring pixels' positions) by half / all the filter radius in texels
  (1.5, or Shadow softness's 2.5 / 3.5) x sin(normal, sun), in each split's texel size.

## v32: steady effect IDs, effects set in the test, shadow softness removed

- ATN: the effect IDs changed when the camera brought more effects on screen. The constant buffer's mid colours and
  their offsets (the remaster's colour curve) are only set by effects that have one: the others keep the previous
  effect's values. The ID now hashes what every effect sets: start / end colours (c0-c3) and fade / thickness /
  emissive, animation, rotation (c16-c20). UpdateSubresource of a tracked buffer is read too (update_buffer_region,
  _command). v31's IDs (9FC96955, FE957C04) no longer apply.
- VFX Effect Test: "Set as Fire & Glow / Burning Houses / Campfires" for the selected effect, saved in ReShade.ini
  [Shogun 2 VFX effects] BurningHouses / Campfires (hex IDs; a key once written replaces that kind's defaults). Glow
  meshes too (default: rigidglowcolour as Burning Houses); glowmesh00 / 01 take SI.vfx_glow_draw.
- Recommended: Distortion and Haze 0.1, PsychoV31's default Highlight Hue Shift (0.42).
- Shadow softness removed (ATN); Shadow map stability's offset is half / all of the game's 3x3 radius (0.75 / 1.5
  texels).

## v33: ATN's effects as defaults, Big Fires, test hidden, banners while paused

- ATN's effect choices (VFX effect test, saved lists) are the defaults (vfx_effects.hpp DEFAULT_KINDS): Big Fires
  (burning buildings, castles, walls) = glow mesh 1, FC75D250, DCB6C2CB, FF972FE7; Campfires = 73B8E4C8.
  ReShade.ini [Shogun 2 VFX effects] BigFires / Campfires still replace them once written.
- "Burning Houses" renamed "Big Fires" (vfx_big_fire). Campfires 0-50 in whole steps. Recommended: Fire & Glow 5,
  Big Fires 1, Campfires 15.
- The VFX effect test's settings are commented out (addon.cpp, "VFX effect test (hidden)"): uncomment to use it again.
- Banners while paused (debug18): the cloth VS's "time stands still" check is now |time - prev time| < 1 ms. The paused
  banner still had 700-1000 render px of motion with v22's exact equality although the trace printed both times as
  98.3980 (its prev positions collapse toward a line: not the last simulation step). The DLSS addon's trace now prints
  the difference.

## v34: banners' motion vectors, the sea's overlay below 100%, SDR-like preset

- Banners while paused (debug19 capture: time - prev time exactly 0, so v33's check held, yet with the previous position
  = the current one the banner moved up to 33000 px): the cloth positions are homogeneous; the units' banners (UI,
  hidden with K) carry a w != 1, and the pixel shader's motion vector projects xyz with w = 1. clothvelocity00 divides
  the previous position (and the current one while paused) by w.
- Sea battles below 100% (ATN debug19 UP / DLAA): selection rings, firing arcs, wakes and bow waves showed at a third of
  their size in the top-left at 33%. They are drawn into the sea's colour overlay (indicator ps30_main_to_overlay
  0xC57C222D, seainteraction 0xC8375A74, wake 0x06701006), which ocean.hlsl reads at the sea's full-screen position
  (its decal projection), while the DLSS addon's render scale drew them into the overlay's top-left part. Their draws
  now use the whole target (callbacks, the game's shaders; the viewport restored after each).
- Preset "Recommended (SDR-like Contrast/Saturation)": Recommended with Cone Response Exponent 1.25 (ATN).

## v35: SDR output

- Output "SDR" for displays without HDR (or Windows HDR off): a 10-bit gamma 2.2 swap chain, white = the display's
  (Peak, Paper White and UI hidden). Rendering Mode SDR: the game's image; HDR (Extended): the game's curve with the
  Extended sliders (no room above white to extend into); HDR (PsychoV31): mapped to SDR within BT.709. Video AutoHDR
  off. Clipped per channel as the game.
- Output "Auto" (new default): scRGB when Windows HDR is on for the game's display, else SDR (checked at the first
  swap chain). A saved HDR10 / scRGB choice is kept.

## v36: less CPU

- ReShade's overlay was built every frame, closed too: the addon read the DLSS addon's options on the reshade_overlay
  event, and ReShade draws its overlay whenever an add-on has that event. Read now each frame while the overlay is open
  (reshade_open_overlay) and as it closes.
- VFX effect test bookkeeping (effects on screen, per draw and per frame) only once the test is used (it is hidden).
- Effect IDs: a buffer map / unmap takes a shared lock or none (the exclusive lock only for the effects' own buffers),
  a particle draw a shared lock once its buffer is known.
- Sea overlay: the render target is checked only when the viewport is smaller than the screen.
- Debug logging (DEBUG_LEVEL_0) off.

## v37: brighter sun, SDR-like preset

- Sun up to 20 (was 2). ATN debug20 (Sun 2, Peak 1000, Paper White 203): the disc flat at about 375 nits. Not a clip:
  the game's sun textures (models.pack rigidmodels\skybox\sundisk1.dds / sundisk_foggy.dds, DXT5) are an even white
  disc with a soft alpha edge, drawn into the 16-bit scene; its brightness only follows the slider (and the tone curve).
- Recommended (SDR-like Contrast/Saturation): Highlights 0.95, Cone Response Exponent 1.34, Highlight Roll-off 2 (ATN).

## v38: presets

- Recommended: Sun 2 (ATN). Recommended (SDR-like Contrast/Saturation): Cone Response Exponent 1.30 (ATN, was 1.34).

## v39: SSR ray steps back, wind-blown flags while paused

- SSR ray steps (the DLSS addon's SSRSteps, 100 x 2^0-6) back in ssrray00 (ATN: the reflections are on the sea in sea
  battles, where v26 had not tested them).
- ATN debug21 (paused, time - prev time 0): two big banners (camp / castle flags) flickered with motion vectors of
  80-1500 px. They are rigidtextureblendwind meshes (VS 0x40E181C2), not cloth: their previous position comes from the
  instance's previous matrix, which differs from the current one while paused (the game's shader also uses the current
  time for the previous wind). flagvelocity00: while paused the previous position is the current one.

## v40: menu figures, campaign overlay below 100%, SSR ray steps removed again

- Menu figures (clan leaders, generals: variantweightedvtf / rigidstandard ps30_main_ui, 0xF45347E8 / 0xA406D4BE)
  turned see-through (ATN debug22): the UI target is 8-bit in the game, upgraded to 16-bit here, and the first writes
  alpha 2 for lit pixels (and both colours above 1). Their output is clamped as the 8-bit target did. (Their other two
  permutations, variantrigid / variantweighted, already write 0-1.)
- Campaign map below 100% (ATN debug22: roads moving with the camera, in the wrong place): the map overlay (roads,
  borders, rivers) is drawn into a 4800x2700 target at its full size for the whole view, but sunlight02 / 03 and the
  campaign water (waterplane campaign high / medium / ultra 0xF7103E54, low 0x630BA7A4) read it at the projected
  position x viewport / screen: the render size's part of it. That factor is divided out (at full size the game's).
- SSR ray steps removed again (ATN: no visible or performance change on the sea either).

## v41: the sea's reflections at the right edge below 100%, Fall of the Samurai's campaign map in PsychoV31

- ATN debug22 / 23 (99% and 50%): the distant sea's reflections turned bright along the right edge, ~32 render px at
  any scale (34 / 70 output px), only in the far-water band. ssrresolve00 reads the reflected colour from the scene's
  mip chain at a level by the ray's length (up to mip 5: 32 px texels); those mips also average the unrendered rest of
  the full-size texture. Below 100% the lookup stays half a texel of its coarsest mip inside the rendered part.
- Fall of the Samurai's campaign map (bos_japan) has a much flatter tone curve than every other map (film slope 0.52
  in winter, 0.63 / 0.68 spring / summer, shoulder 0.23-0.40; elsewhere 0.88 / 0.13-0.14): PsychoV31 (own contrast) showed
  it far brighter than SDR (debug22: SDR 88-nit snow at 235 nits, Extended 140). PsychoV31's response power now x the
  map's slope / 0.88 (like the cone exponent): the same on every 0.88 map.

## v42: PsychoV31 grading as in Tales of Arise, credits, Ko-fi

- PsychoV31: Highlight Contrast, Shadow Contrast, Flare (slider^5), Dechroma, Gamut Compression and Gamut Compression
  Mode (BT.709 / BT.2020 / Display P3; SDR output BT.709), Highlight Saturation Shift (the SDR image's saturation in
  the highlights, Highlight Hue Shift's weighting). After the hue shift and the game's gamma, colors outside the
  target are brought to its edge (FitToTargetGamut). Defaults: V31's own (1 / 1 / 0 / 0 / 1 / BT.2020 / 0).
  ShiftSaturationToward and FitToTargetGamut are at the end of psycho_test31.hlsli.
- About: credits (RenoDX and PsychoV31 by ShortFuse, Musa's custom PsychoV31; Bend Studio's screen-space shadows,
  Apache 2.0, license shipped as APACHE_LICENSE-2.0.txt), ATN's Ko-fi (Simple and Advanced).

## v43: a note above the fire sliders

- PsychoV31 and Extended: "Fire too pink, too orange or too pale? Change the settings below." above Highlight
  Saturation / Hue Shift (/ Saturation Shift), Highlight Saturation moved next to them (as Tales of Arise). ATN's
  defaults kept (hue shift 0.42 / 0.33).

## v44: Complete Edition names, in-game settings note, Nexus

- The folder and addon are now twshogun2completeedition (renodx-twshogun2completeedition.addon64): delete the old
  renodx-twshogun2.addon64. Settings are kept (ReShade.ini [renodx]).
- General: yellow note "Use these in-game settings: Gamma 22 and Brightness 10."
- About: NexusMods button next to ATN's Ko-fi; every section title green (settings.hpp default_section_tint).
- Not yet: the Visibility Bitmask AO (cs50_new_sweep / new_collect with the DLSS addon's render scale).

## v45: Highlight Hue Shift 0.46, Nexus label, AO trace

- PsychoV31 Highlight Hue Shift default 0.46 (ATN; Reset / Recommended too).
- "NexusMods (For RenoDX + DLSS)" (as Ryse).
- Debug (Advanced): AO pass counter and "Trace ambient occlusion" (the next frame's ssao_3k passes to ReShade.log), for
  the Visibility Bitmask AO with the render scale. Its settings will go into the DLSS addon's Graphics (ATN: every
  graphics option there).

## v46: Visibility Bitmask AO with the render scale

- The new SSAO's collect (ssao01_0x98BB1159, replaced): the game's collect, or the Visibility Bitmask AO over the
  render-scale viewport (camera_VS_PS inv_projection c26, viewport c38) with its own 5x5 depth-weighted denoise (the
  new path has no blur). Settings in the DLSS overlay (Ambient Occlusion).
- Debug section (AO counter and trace) removed.

## v47: AO radius to 10

- ReShade.ini AmbientOcclusionRadius clamped to 0.25 - 10 (the DLSS overlay's slider).

## v48: faster ambient occlusion, Quality

- Ambient occlusion faster and Quality: twwarhammer2/bitmask_ao.hlsli as the other Total War mods; the Quality from
  the DLSS overlay (ReShade.ini AmbientOcclusionQuality).

## v49: Light Bounce, AO Directions

- Ambient Occlusion Directions (2 / 4 / 8 slices around the view vector, default 4 as before; SI.ssao_directions):
  fewer is faster. From the DLSS overlay (ReShade.ini AmbientOcclusionDirections).
- Light Bounce (Experimental): twattila/light_bounce.hpp on sunlight00-03 (camera b0, G-buffer t7 / t9) with
  light_bounce.hlsli: spherical normals, albedo x (1 - metalness) x 0.96, unlit pixels (flag 1 clear) none; position
  from g_viewport_dimensions (the render size at any render scale). Settings from the DLSS overlay (ReShade.ini
  LightBounceStrength / Radius / Resolution / Quality / Directions).

## v50: Light Bounce Temporal Smoothing, Eighth, 16 directions

- Light Bounce Temporal Smoothing (ReShade.ini LightBounceTemporal), Resolution up to Eighth, Directions up to 16,
  from the DLSS overlay.

## v51: Game's ambient occlusion at its own cost

- Game's ambient occlusion costs what it did again: the replaced AO pass is the game's code only and the Visibility
  Bitmask is the addon's own 8 x 8 compute shader dispatched in its place (twwarhammer2/line_sweep_ao.hpp
  DispatchInstead, the addon's settings bound at b13). Before, one 1024-thread shader held both (fxc X4714: fewer
  threads in flight even for the game's path). Here in place of the collect (ao_bitmask_collect.cs_5_0.hlsl, tiles
  with a 2-pixel border).

## v52: AO Depth Levels, Light Bounce levels

- AO Depth Levels (ATN: Rome II's frame time spikes gone with them): twattila/depth_levels.hpp builds the AO's depth at
  1/2 to 1/32 (5 mips of R32G32_UINT, one compute dispatch a level, depth_levels.cs_5_0.hlsl: each texel one depth texel
  of its 2 x 2 in a rotated grid, its bits and coordinate) before the Visibility Bitmask pass and binds them at t41
  (twwarhammer2/line_sweep_ao.hpp OnCollect: the collect's t0, kept for DispatchInstead). A sample 8 x 2^n to 16 x 2^n
  depth texels away reads level n at that depth texel's own position (depth_levels.hlsli, BitmaskSampleLevel in
  bitmask.hlsli; as Scalable Ambient Obscurance). Same look; the far samples stay in the texture cache, so more
  Directions / Quality cost much less.
- Light Bounce levels: the prepared light and geometry with 5 more mips (twattila/light_bounce_levels.ps_5_0.hlsl after
  the prepare: light the mean of 2 x 2, geometry one of them in a rotated grid); the trace's far samples read them
  (BitmaskSampleLevel). Same look, cheaper at Full / Half resolution and large radii.

## v53: Light Bounce G-buffer reads

- Light Bounce: the G-buffer read through twattila/light_bounce_gbuffer.hlsli (MSAA builds for the games with MSAA;
  this one has none: unchanged).

## Zip v59 (2026-10-26): licence files

- APACHE_LICENSE-2.0.txt in the zip: the screen-space shadows are Bend Studio's (Apache 2.0, as shipped in the game's
  shaders.pack), changed by RenoDX (bend_sss_gpu.hlsli: the render scale); RENODX_LICENSE.txt as before.

## v60 (2026-10-26): contact-hardening sun shadows (PCSS), untested in game

- ATN: a licence-free PCSS. Percentage-closer soft shadows are R. Fernando's technique (NVIDIA, 2005, a public paper);
  `shaders/pcss.hlsli` is written from the paper (RenoDX, MIT), no code from elsewhere. MIT / Apache code that would also
  have been usable: AMD GPUOpen ShadowFX (MIT, D3D11 HLSL, contact-hardening kernels), Bevy (MIT / Apache 2.0, WGSL),
  Filament (Apache 2.0).
- In the sun's shadow mask pass (sss.hlsli SampleShadowSplit, every cascade): in place of the game's 3x3 filter, the
  blockers' average depth within 24 texels (texels the game's comparison sampler finds in shadow: either depth
  direction), the penumbra the sun's size makes across the gap to the receiver (world units from the cascade's
  orthographic matrix), then a 24-sample comparison filter that wide (1-24 texels). Vogel disks turned per pixel and
  frame (shadow_noise_frame), settled by DLSS / TAA.
- DLSS addon: Shadows > "Sun shadow softness (PCSS, experimental)", 0-5 degrees (the real sun 0.5), 0 the game's filter
  (default; in no preset). ReShade.ini [Shogun 2 DLSS] SunShadowSoftness.
- GPU test (tools\shogun2\pcss_test.cpp): fully shadowed and fully lit away from a caster's edge; penumbra 3 texels
  near the caster, 15 far from it; the same with reversed depth.

## v61: Remastered CE

- Remastered CE (JakeTheRipper's patch; installer looked at, not run: it needs the Steam CE executable) rebuilds two
  of the sun shadow mask compute shaders (its edge rejection and floating-banner exclusion): sss00 0x34ECD06E ->
  0x9147EB4E, sss01 0x1B529672 -> 0x7FA1D600. Both get this mod's (PCSS, stability, render scale) as on CE. Its
  other shader changes (particle and ribbon vertex shaders, blood decals, the grass source) are not ones this mod
  replaces. ReShade and the addons go into Remastered CE's own folder (CE\RemasteredCE, where its executable runs).


## v72 (2026-11-14): variantrigid out of view, dissolved leaves, reflection rows, coloured shadows (stamped and installed 07:50 with renodx-tw-dlaa64.exe helper112, not run in game; first install with the helper: NVIDIA DLLs 310.9.16, the game's 310.5.2 renamed aside)

- **Shadow casters out of view** (the same Experimental row, off) also takes variantrigid's shadow draws (B0E006EE: 10 draws,
  4 to 256 instances in the battle trace), placed by their instances' rows (v8-v10) as the rigid ones
  (rt_capture / rt_rows_cascade_variant), their rows every frame (they sway in the wind like banners: what carries them
  isn't known). The sway itself (under 2 cm) is left out.
- test/tw_shogun2_shaders part 10 for both: the capture and rows versions against the game's AC25DAB8 and B0E006EE.
- Out of view, a dissolved tree's leaves no longer cast (rt_capture_cascade_tree_leaves): left out where the leaves' pixel
  shader (A97EF2E0) drops every texel at every dissolve value (saturate(fade) x fade > 1 - 0.196, fade = 1 - v10.y). Its
  branches keep casting, as in the game (their shadow pixel shader reads no fade). Part 12: against the game's leaf shadow
  pass (its vertex and pixel shaders, an occlusion query) at 12 fades; control: the branches' version keeps them.
- **Reflection reuse** (Experimental, off: from ReSTIR GI with the ray-traced Global Illumination, or Own ReSTIR) and
  **Steadier water reflections** (Experimental, off): Attila's RT_REFLECTIONS_REUSE / _OWN / _STEADY (DLSS addon v64).
- Not done: far tree cards out of view (rigidtreebillboard 30C453EE): no rows path (each card turns to the shadow camera at
  its corners and takes its own atlas view from its facing: no shared mesh and mask), 38.8M indices x instances a frame;
  Softer Water Reflections (needs rt_water_plane's roughness and the water's positions).
- **Coloured shadows** (Experimental, off; with Light through leaves): sunlight through sails, banners and cloth takes their
  colour (Attila's RT_COLOURED_SHADOWS): the helper's filter at t50 for the sun draws with this frame's trace, every use of
  the sun's colour in sunlight00-03 multiplied by 1 - it (shaders/rt_sun.hlsli RtSunColour); the G-buffer guides written
  for it (the colour where the screen shows the cloth). Part 13 (WARP): in each of the four, the filter 1 gives exactly the
  picture of the sun colour 0, unbound the sun lights it; control: half the filter lies between.

## v71 (2026-11-14): cloth, sprite soldiers and trees out of view, Path-traced AO rows (built only, not run in game)

- **Shadow casters out of view** (the same Experimental row, off; DLSS addon v63) also takes the shadow pass's cloth (sails,
  banners: 8A00A1D4, 44 draws in the battle trace), sprite soldiers (E3DA20D4, 272 draws; only with Sprite Soldier
  Shadows on, cards turned to the sun as the game's shadow pass turns them) and trees' leaves and branches (5851CA62,
  6F20485E: 22 and 17 draws, 327K and 32K corners; swayed by the game's wind, leaves cut out by their diffuse alpha).
  Streamed out every frame in world space by their capture versions (rt_capture_cascade_cloth / imposter / tree: the
  game's corners and the UV of its mask test) through
  Pharaoh's outside-view geometry shader (twpharaohdynasties RT_LEAF): the triangles wholly outside the last frame's main
  view kept, cut out by the game's masks; the rest collapse (the main pass has them).
- Shared rt_core (opt-in, the other games unchanged): SceneShader::cascade_world, Host::cascade_world for one shader, its
  capture version in place of the game's vertex shader and its world positions into the captured buffer's world part in a
  clip-space game.
- test/tw_shogun2_shaders part 11: the capture versions against the game's four csos (world, UV), through the geometry shader
  with a main view (kept, collapsed) and a control (the view turned away keeps another set). The test's GPU lock is taken
  atomically now (made only where none exists, removed only while it is this run's).
- **Path-traced AO: Own Sky Rays** and **Steady Bounce Light** (Experimental, off; Attila's RT_PATH_SKY_APART /
  RT_PATH_KEPT_CUBE): the sky averaged from each pixel's own rays; the bounces' stand-in light from rt_sun's cube (texel 3
  w 1: the sun shaders' sky, texel 4 w 1: in the game's units).
- Still not done: units (140M indices x instances a frame, no rows), far tree cards (rigidtreebillboard 30C453EE: 39M) and
  variantrigid (B0E006EE: 13M), too many to stream out every frame; a fully dissolved tree (the leaves' fade) still casts.

## v70 (2026-11-14): rigid shadow casters out of view (built only, not run in game)

- **Shadow casters out of view** (Experimental, off; with ray-traced sun shadows; restart the game): buildings and rocks
  the main view culled but the shadow maps draw (rigidterrainblend vs30_shadow, AC25DAB8) join the ray-traced scene, as
  Warhammer III's: each mesh captured once in world space (rt_capture_cascade_rigid), its instances placed by their rows
  (rt_rows_cascade_rigid), those whose origin is in the main view left out (the main pass has them). Shogun 2 draws its
  shadow maps before its main view: they test against the last frame's (rt_core Host::cascade_last_view, opt-in).
- Not done: cloth and sprite soldiers' shadow draws (small, but not placed: streamed in the last frame's clip space they
  would shift with the camera), units (140M indices x instances a frame, no rows).
- test/tw_shogun2_shaders part 10: the capture and rows versions against the game's AC25DAB8.

## v69 (2026-11-14): rivers, lakes and the Ultra sea in the water reflections (built only, not run in game)

- **Ray-traced water reflections** (the row's new name; Experimental, off) also on the battles' rivers and lakes
  (waterplane high / medium / ultra, waterplane00) and the sea at water quality Ultra (ocean01). The water planes' draws
  are kept (their mesh, vertex shader and a copy of their g_world) and drawn again depth-only with the next frame's
  camera before the reflections; rt_water_plane takes the nearer of the sea's plane and them.
- test/tw_shogun2_shaders: rt_water_plane with a water mesh band (nearer: water; under the land: not), ocean01 and
  waterplane00 against the game's csos.
- Casters out of view (W7d) not made: the shadow pass comes before the main view, and its instanced draws are far too
  many to stream out every frame (rigid: 38M, 53M, 326M indices x instances in cascades 1-3; units 140M).

## v68 (2026-11-14): sea reflections, sun-facing sprite soldiers and far trees (built only, not run in game)

- **Ray-traced sea reflections** (Experimental, off; with ray-traced reflections): the sea (ocean, water quality High)
  is drawn after the reflections with no G-buffer of its own, so before them rt_water_plane puts its plane (its vertex
  shader's decal projection plane, from the last frame's draw) into a copy of the depth with the water's guides and a
  mask; the rays leave from it, the resolve keeps the environment under the sea, and the sea's pixel shader (ocean00)
  takes their reflection in place of the probes' / the sky's where they met something. Rivers and lakes (waterplane)
  keep the game's.
- **Sprite soldier shadows** (Experimental, off): the far soldiers' cards turn to the sun in the ray-traced scene
  (rt_capture_imposter; off: they face the camera, as before). **Far tree shadows** (Experimental, off): the far tree
  cards join the scene turned to the sun with the atlas view the sun sees (rt_capture_tree_billboard; the scene had
  left them out).
- Shared (twattila/rt_core.hpp): a scene_draw host's shader with a capture version streams out through it in clip
  space (Ryse and Expeditions give none: their path is as before).
- test/tw_shogun2_shaders parts 5-8: the captures against the game's csos (card, view, UV), rt_water_plane against
  the CPU, ocean00 against the game's (its five outputs the game's unless the rays are bound and flagged), controls.

## v67 (2026-11-14): Path-traced AO, Light Through Foliage, specular occlusion (built only, not run in game)

- **Path-traced AO** (Experimental, off; with path tracing and ray-traced GI): the paths bring the sky's light, the
  sun light draws get their lighting without the game's ambient (the cube and the harmonics' flag zero). Battles only:
  the campaign map keeps the game's ambient. rt_sun is Shogun 2's own: the paths' sky is the sun shaders' ambient
  (influence x cube + the rest x fudge x the world's harmonics at the camera's probe), texel 6 unstretched.
- **Light Through Foliage** (Experimental, 0): Bend's screen-space shadows let that share of the sun through what grass
  and leaves shadow (foliage_light.hpp: the depth copied with foliage negative, the passes' light-caster versions).
- **Ray-traced specular occlusion** (Experimental, off): the sky's reflection off where something stands over a surface
  (ssrresolve00).
- The ray-traced scene: cloth thin (sails, banners), the grass as grass. Fog shadows alone install the ray tracing.
- test/tw_shogun2_shaders: rt_sun against the three data run buffers with harmonics, sss_foliage_depth (WARP).

## v66 (2026-11-14): the helper's upscaler and frame generation, ray tracing rows (built only, not run in game)

- **Upscaler** (the DLSS addon's row, Experimental): the game's DLSS evaluation through renodx-tw-dlaa64.exe
  (upscaling.hpp): newer DLSS, **Ray Reconstruction** with the ray tracing, FSR or XeSS. The game's colour, depth and
  motion into the helper (upscale_inputs: L, M and RR get the colour's square root), its result into the game's DLSS
  output (upscale_output). Any frame the helper can't, the game's DLSS runs.
- **Frame generation** and **Reflex** (twattila/frame_generation.hpp, reflex.hpp; the helper presents, its HUD-less
  image after tonemap00 / 01), with either upscaler. Tales' in-process frame generation is gone from the DLSS addon.
- Ray tracing: rt_sun from lighting_VS_PS (the sun, its colour, the ambient cube: path tracing's sky), the Debug View
  after the tone mapper, a profile button; rows for AO rays, 32 / 64 / 128 torch lights, importance, steady, range,
  softness (RR), GI resolution (no Checkerboard), path bounces up to 2, leaf micromaps, GPU baking, fast leaves, NRD
  (with the helper's upscaler), SER, SHaRC, Rays a Pixel, log details, scene test.
- Bend's thickness, contrast, edge threshold and precision offset as rows (sss.hlsli; 0 the game's).
- **Ray-traced fog shadows** (Experimental, off): the battle fog's sun light (fog01) shaded along each ray by the
  helper's fog trace (twattila/rt_fog.hlsli, as Warhammer III); the sky's part of the fog as it was.
- **Upscaling Debug View**: the motion the helper's upscaler gets, over the picture (upscale_output).
- rt_sun: the paths' sun light in the game's units (sunlight00-03's x 1.90384671e-005; read with Path-Traced AO only).
  test/tw_shogun2_shaders checks rt_sun with the data run's lighting buffers, upscale_inputs with a DLSS capture and
  the Debug View on WARP.
- The helper starts only with a ray tracing row on at launch, Upscaler not the game's DLSS, or frame generation on.

## v65: wrong constants from the decompiler (built only, not run in game)

Checked against the game's own shaders (data\shaders.pack: bytecode and source):

- **Battle fog** (fog00, fog01): the bottom plane read the top one, so the ramp was off: full density up to a hard ceiling. Now
  it fades as in the game: thinner, no ceiling, on maps with height fog. Recheck Battle Fog in Recommended, it was set on the
  thicker fog.
- **Flags** (flagvelocity00): the previous position's wave used this frame's time, so flag waves had no motion vectors.
- **UI figures** (uifigure00, uifigure01): the same fog plane; uifigure01's dissolve and untextured flags read the unlit flag.
- **Campaign water** (campaignwater00, 01): the north edge's black border compared with the map's x limit.

## v64: batch 5 (untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper22** (in the zip; bridge v11, needs this addon): **ReSTIR GI v2** (compared with
  Embark Studios' kajiya, MIT / Apache 2.0, not copied): fireflies clamped, history only on a surface facing the same
  way, and every third frame the reused light is checked again: a light switched on or off shows within 3 frames
  (was about 20). **Partitioned TLAS** fixed (its update operations stalled the GPU after BLAS compaction) and on
  again.
- **Light Through Leaves** (off by default, with Ray-Traced Sun Shadows): leaves let a quarter of the sun through
  each, only where the rays are the shadow (past the game's own shadow maps).
- With Ray-traced point light shadows on, the point lights' light also reaches what the Light Bounce / Path
  Tracing meets off screen.

## v63: ray-traced reflections and point light shadows (untested in game; the helper GPU-tested)

- **Ray-traced reflections** (DLSS addon v56; needs the game's screen-space reflections on): in the reflections'
  resolve (ssrresolve00) the rays' light replaces the march's hit and the share of rays that met something its
  confidence; while they run, what only the march reads is left out too: the march (ssrray00), its blurs (ssrfilter00 /
  01), its HiZ (ssrhiz00 and its levels) and the scene's and hit buffer's mip chains. The helper's G-buffer
  guides come from the sun draw's G-buffer (rt_guides.cs: diffuse = colour x (1 - metallic) x 0.96, specular = 0.04
  toward the colour with metallic, the normal's spherical angles, roughness 1 - gloss); the resolve's lit scene is kept
  for the next frame's reflections and paths.
- **Ray-traced point light shadows**: the point lights the last frame drew (pointlight00 / 01), the 32 nearest and
  brightest, go to the helper with the frame's trace: a ray from each pixel within a light's reach toward it (stopping
  half a metre short of the light: its own torch or fire); pointlight00 / 01 darken where it is blocked
  (twattila/rt_local_lights.hlsli, bridge v9).
- Path tracing also bounces the reflections' rays. Fix: path-traced rays that met what last frame saw took black light
  (no scene colour was kept).
- **Ray-traced AO at a quarter rate** (one pixel of each 2 x 2 traced a frame, in turn). Less CPU work a frame (static
  meshes the scene holds only marked drawn). helper21 (in the zip): the last zip's helper19 stalled each request (its
  partitioned TLAS, now off); ReSTIR GI for Path Tracing (about a quarter of the noise).

## v62: ray tracing (Experimental, untested in game; the helper GPU-tested)

- Settings in the DLSS addon's overlay ("Ray Tracing"; ReShade.ini [Shogun 2 DLSS] RayTraced* / PathTracing*), read
  here; restart after turning on the first effect. Needs an RTX GPU and the Total War helper (renodx-tw-dlaa64.exe,
  D3D12\D3D12Core.dll) next to the addons (ray_tracing.hpp, twattila/rt_core.hpp).
- The scene: every G-buffer draw of the main view (rt0-2 RGBA16F and the depth) streamed out through the game's own
  vertex shaders (leaves alpha-tested against their diffuse alpha, units every frame), the camera from camera_VS_PS's
  uploads.
- At the frame's first sun light draw: the sun and the trace. Ray-traced sun shadows and ambient occlusion reach
  sunlight00-03 at t40 / t41 (shaders/rt_sun.hlsli): the darker of each and the game's shadow mask / AO.
- Ray-traced Global Illumination: the Light Bounce's (GT-VBGI's) trace through rays, path traced when on.


### F1 helper lifetime repair (2026-11-09, GPT-6 Astra)

AddonUninit now shuts down the shared helper bridge after unregistering its users. The watcher exits before module unload; child process and IPC ownership are released. Shutdown drains outstanding GPU fence generations, with ordered CPU fence recovery only after confirmed helper exit. Failed launches clean up partial ownership and existing IPC sessions are not overwritten.

Offline checks: x86/x64 production-header tests cover process/thread cleanup, failure injection, 20 reloads with stable handle counts, and three interleaved D3D11/D3D12 WARP fence generations under normal completion and forced shutdown. No game was launched. Manually check helper-enabled unload/reload and device recreation, then repeat the reported battle. The original battle freeze is not proven fixed; unexpected mid-run helper failures remain audit F7.
