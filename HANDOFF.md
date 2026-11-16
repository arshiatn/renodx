# Handoff: Ryse HDR + DLSS (2026-10-05, Claude Code session)

## OPEN FOR ATN (2026-11-15)
ATN's questions from the HANDOFF audit (tools/tw/round_notes/handoff_audit_1115.tsv); each names the line it came
from (search its text). Tick here and on that line when ATN answers.
- [ ] NEW 11-16 (Rome II round): shadows default Multiply vs Replace (a trace first); morefire00 is x0.2 in SDR too (since
  09-30): keep?; V35 toe at 1-15 nits x0.063-1, colour x1.22 (test black point 1e-4): OK?; Hue Clip 90 vs 80;
  rays_preset_as_attila for Rome II?; a bench save for Faster Game CPU Code. Owner: ATN
- [ ] NEW 11-16 (R.U.S.E. round): Recommended motion blur 0?; RT/PT scope; upscaler sub-rows; Ray Reconstruction for the
  rays (today DLAA preset K + our own denoise). Owner: ATN
- [ ] NEW 11-16 (WH1 / WH2 round), in game: a river or lava with RT/PT (WH2 RT Water Reflections); wet stone with
  Specular Occlusion (reflections darker only once); WH2 Modded Auto Exposure; WH1 Smoke Shadows without rays + Smooth
  Particles; Time Each Pass before / after the new skips; WH1 / WH2 into the Gamma Correction look checks. Decide (C):
  unit-size presets. Owner: ATN
- [ ] NEW 11-16 (Troy / Pharaoh round): TAA / T2x in Troy / Pharaoh run on the linear frame while Attila's run on 2.0
  (linear: up to 12 % more light at thin lines, flicker 4 % higher, bias 22 % lower); the ready patch
  tools/tw/troy/aa_domain/shared_step_taa.patch matches Attila within 0.004 - apply?; under Rays Replace the baked far
  shadow map still multiplies; Pharaoh's Shadow Casters Out of View is off by default (no rays_preset_as_attila); Troy Hue
  Clip 80 (fire 1.99 deg) vs the measured best 70 (0.82 deg); v31_highlight_hue_shift 0.1 / 0.06 vs the 0.04 rule;
  RenoDX's own sharpening on by default. Owner: ATN
- [ ] NEW 11-16 (shared + Ryse rounds): Auto Thickness is now OFF by default everywhere (it was labelled Experimental
  while on; your rule: Experimental off) - keep off, or on without the label?; players' saved Fog Shadows / Specular
  Occlusion = 1 and saved trilinear = 0 stay as saved (no ClearSavedRows) - clear them once?; Ryse: rays overwriting the
  sun mask (not proven: off-screen casters, mask channels only on the GPU; Skip All already gives rays alone except the
  kept far map). Owner: ATN
- [ ] NEW 11-16 (Shogun 2 / Rome Remastered / GPU rounds): Shogun 2 Rays Replace Shadow Maps stays Experimental off (far
  soldiers / trees need their Experimental shadow rows, grass loses contact shadows, the cascades are still drawn: no cost
  saved yet) - OK?; Auto Thickness "(Experimental)" but on by default (Shogun 2, Attila, Thrones, WH3): drop the label or
  default off?; Shogun 2 Video AutoHDR 0.25 (skill 0.2, see the line above); unseen-surface local lights in paths cost
  ~0.1 ms (dropping them is a look change): an Experimental row?; in game: the GPU round's two helper wins in Attila at
  your rows. Owner: ATN
- [ ] NEW 11-16 (WH3 round): in game (B): one trace of t00's g_gamma_output (GC level B until then), GC on / off in SDR +
  HDR, Bend + kept maps (contact shadows once), torch-lit scenes under rays (untraced lights are now unshadowed), metal with
  Spec Occlusion on. ATN decides (C): the Multiply row (Rays Replace off); terrain t7 kept under the rays;
  RayTracedWaterReflections' dead default lines; texture LOD bias under the game's own scale; Auto Thickness's
  "(Experimental)" label; reflection00 still multiplies the rays' reflection by AO (Attila exempts smooth surfaces); V35's
  toe lifts WH3 shadows (grey 0.05-1 nits x1.27-1.39, 1-15 nits x1.03-1.25; Attila x1.00). Owner: ATN
- [ ] NEW 11-16 (Tales + Expeditions round, ATN decides / checks): the look of Bend + GT-VBGI on by default; Tales camp
  still runs the SSGI chain (an unproven reader there); RT shadows replacing the maps is unproven in both games, so
  min(map, rays) stays for now: accept until a proof?; Expeditions Brightness 40 (neutral, the page-1 line) or 50 (the
  developers' default: lifts shadows, dims HDR highlights)?; Tales Brightness 50 neutral by eye (Bandai's curve unread);
  Expeditions Two Shadow Cascades Under Rays: CPU on a battle map + far off-screen shadows. Owner: ATN
- [ ] NEW 11-16 (fix round, ATN decides): Thrones' preset turns on Steady Bounce Light and 128 torch lights (both
  Experimental) and Auto Thickness is on while labelled Experimental: keep or off?; Ryse's Recommended Video AutoHDR 0.25
  (as Shogun 2's) against the skill's 0.2; Path-Traced Ambient alone leaves the reflections without occlusion: Specular
  Occlusion with the paths, or keep SSAO for that reader?; Specular Occlusion on makes armour creases a little brighter
  (look). Owner: ATN
- [x] ANSWERED 06:xx (ATN: "RT Shadows should replace them (and make them stop to save performance maybe)"): rays
  keep the far shadows (more correct), replace the maps' term, maps only for hair / fog; skip fix in the Ryse fix round. Still open from the run analysis: Ryse far-facade ray shadows (rays darken far facades the maps leave lit, up to 0.67
  stop): keep or limit? Owner: ATN
- [x] ANSWERED 06:xx "you are allowed to use mouse click on screens" (memory feedback_mouse_clicks_allowed): Tales redo with a click after the fix round. NEW 11-16: Tales' title screen ("Press Any Key") won't take our keyboard input from a script (Enter / Space end in the
  attract movie), so the Bend / GT-VBGI look shots couldn't be redone. Choose: you load SaveData_006 once during a run, or
  allow one mouse click on Tales' title screen for scripted runs. Owner: ATN
- [x] ANSWERED 06:xx "we could improve them later with nsight maybe (in next days/weeks)": box below. NEW 11-16: Ryse costs (4K DLSS Quality): RT sun shadows +1.0 ms, path tracing +2.7 ms (+1.7 over RT), Path-Traced
  Ambient about 0. Need work, or fine? (tools/tw/round_notes/redo_1116.md) Owner: ATN
- [x] ANSWERED 06:xx "On by default" (shs_ applying; Ryse / Shogun 2 tables at merge). NEW 11-16: Lean ReSTIR Reuse is faster with the same picture (2.72 -> 1.02 ms, same rRMSE) but still Experimental
  off: on by default under your 11-15 rule "faster + same picture = on"? Owner: ATN
- [x] ANSWERED 06:xx: Bend + GT-VBGI ON by default in Tales / Expeditions (Tales+Exp agent, with the SSGI replace); Higher Precision Scene stays Experimental off; trilinear LOD bias default ON (shs_); Ryse far-facade shadows + redo save still open below. NEW 11-16 06:xx: from the run analysis: Bend + GT-VBGI defaults (Tales +0.31 / +0.27 ms, Expeditions +0.90 / +0.49
  ms); Higher Precision Scene stays Experimental off?; Ryse far-facade ray shadows (rays darken far facades the maps leave
  lit, up to 0.67 stop): keep or limit?; pick a Ryse redo save (sunlit, near hair, fog volume, torch interior). Pictures:
  tools/tw/round_notes/an_1116/out. Owner: ATN
- [x] ANSWERED 05:3x (ATN "Please do everything and the things that are left": my picks applied, sk4_ adding all 17 + V31 exception + no tooltip numbers; revert any in the morning). NEW 11-16 05:2x: which of the 17 Attila features missing from the skills to add (my pick: all 17), and 2 stale-line
  choices (V31 hue shift after PsychoV: rule exception (my pick) or drop it; K / M / L numbers in the tooltip: drop from
  the skill (my pick)). List: tools/tw/round_notes/attila_vs_skills_1116.md. Owner: ATN, then coordinator
- [ ] Rome Remastered battle with rays at Battle Objects Tested, then Solid + restart, then All + restart: any GPU reset? (send renodx-tw-dlaa64.log + ReShade.log) (from: "Rome Remastered battle GPU resets: safe-side fix a8afd660 ...")
  Detail (from its ticked duplicate "Rome Remastered battle GPU resets: safe-side fix a8afd660"): fix a8afd660 (rays
  take only the tested capture kinds; banners out; row Ray Tracing Battle Objects (Test): Tested / Solid / All = v11;
  WARP tw_romer_rays case 1); the first group to reset names the group; RR not involved. ATN ~11:2x: once merged, a
  Fable 5.2 agent (medium effort) hunts the battle reset (capture -> bridge -> helper review, a harness repro from the
  battle traces, DRED breadcrumbs, fixes proven offline; the Tested / Solid / All row stays until then). ATN ~11:4x: no
  Fable review per merge; ONE Fable 5.2 High review of the whole merged tree at the end, before the install.
- [ ] test the shared-step rows (torch from PT, GS sprite soldiers, tessellated rigid, own cut-off, Faster Smoke Build) from the README test lines, or let the bench take the numbers first? (from: "ATN's in-game tests of the new Experimental rows (README ...")
  Detail (from its ticked duplicate "ATN's in-game tests of the new Experimental rows"): README lines, level B.
- [ ] grass and banners into Rome Remastered's rays as Experimental rows (off)? (from: "ATN DECIDES: grass and banners into Rome Remastered's rays ...")
  Detail (from its ticked duplicate "ATN DECIDES: grass and banners into Rome Remastered's rays"): the game gives them
  no shadow (0 cascade draws in 50 traces), so it would be a new look.
- [ ] set the path-traced method sliders' Reset defaults to RT/PT's (paths 6.50 -> 2.82 ms), yes / no? (from: "Path-traced method sliders turned on from Reset default ...")
  Detail (from its ticked duplicate "Path-traced method sliders turned on from Reset default"): Reset default = 4 bounce
  rays, 2 bounces, range 10, resolution 1 (look).
- [ ] keep or remove the Lean ReSTIR Reuse row? Bench: Lean saves 0.09-0.16 ms more in game. (from: "Lean Reuse row: now saves only ~0.06 ms (RT_PROFILE) / 0.39 ...")
  Detail (from its ticked duplicate "Lean Reuse row: now saves only ~0.06 ms"): ~0.06 ms (RT_PROFILE) / 0.39 ms reuse
  (Nsight base clocks), look scored as no change; END BENCH PART 2 (g3b_reuse.json): Lean -0.09 (max) / -0.16 ms (atn)
  of the bounce on top of the record. Merged (f): Lean = 3 neighbours + radius 6: 0.96 -> 0.85 ms, rRMSE 2.18 -> 1.95 %
  (better), after ATN's Lean verdict (A3); then the ReSTIR follow-up.
- [ ] in game, do SDR first start / SDR preset / SDR output look right in Troy, WH2, Rome Remastered, Shogun 2? (from: "In game: SDR first start, SDR preset, SDR output for Troy ...")
  Detail (from its ticked duplicate "In game (ATN's list): new player HDR off -> SDR preset"): new player HDR off -> SDR
  preset + log; HDR on + restart -> Recommended once; HDR off -> SDR again; change a row -> never again; first start
  with HDR -> never a preset (ini OutputAutoPreset=off); 24cdfb9c.
- [x] DONE 11-15 (ATN ended it 12:1x, the coordinator deleted both exes): end PID 35256 and delete page_dump_ry_rys1.exe / page_dump_ryd_rys1.exe in tools/tw/page_dump? (from: "A page_dump_ryd_rys1.exe (PID 35256) is stuck in ...")
- [ ] Skip the Game's Shadow Map Work: let the end bench take the numbers (Time Each Pass, PresentMon) and you only check cut-outs / unit shadows? Bench done: CPU busy -1.8 / -2.3 ms, no fps change; only your look check left. (from: "ATN tests (README lines): Skip the Game's Shadow Map Work ...")
  Detail (from its ticked duplicate "ATN tests (README lines): Skip the Game's Shadow Map Work"): off / on in cpl1's
  view, Time Each Pass x3 + PresentMon 30 s, cut-outs and unit shadows; G1 grass maxed against the previous build; END
  BENCH PART 2 g2a_shadow: CPU busy -1.8 / -2.3 ms, frame unchanged.
- [ ] allow data runs for unit mesh LOD part counts and one WH3 stack capture for the x64 shadow-pass skip (with the x64 Faster Render Lists WPR runs)? (from: "ATN DECIDES (data runs): mesh LOD part counts; the x64 ...")
  Detail (from its ticked duplicate "ATN DECIDES (data runs): mesh LOD part counts"): the x64 shadow-pass skip: WH3
  first, one stack capture a frame while the sun's map is bound; also the 8x pose hold's bone-matrix writer 0x17dab80
  (only with a data run).
- [x] DONE 11-15 (settled by the replay: c = keep heapfix's x87fix (no frame-time gain), b = the prefetch site's proposal): (when sending heapfix package) may we patch the x87 block over heapfix's bytes (+0x17f6515), and what about his disabled prefetch site? (from: "Author packages (tools/tw/testbin/cpu4_heapfix/ ... (the heapfix-package question merged there)")
- [x] DONE 11-15 (ATN 12:1x: same-result rows on by default (Faster Render Lists 356b7100); Rome II Skip the Shadow Maps changes pictures, see the open "Faster-with-a-look-risk list" box): Faster Render Lists + Rome II Skip the Shadow Maps: bench numbers enough, or do you want to look in game? (from: "ATN tests: Faster Render Lists off / on in cpl2's view ...")
- [ ] one cascade instead of two under Rays Replace (after your Shadows Medium A/B), and which grass settings note text? (from: "ATN DECIDES: one cascade instead of two (after ATN's ...")
  Detail (from its ticked duplicate "ATN DECIDES: one cascade instead of two"): ATN 15:xx: Shadows Medium A/B not tested
  yet.
- [ ] do swaying grass shadows still flicker with helper112 + RR? (if yes: Swaying Shadows Range 50 / Off-Screen Sway Off / RR off A/B) (from: "NEXT BATCH: swaying grass shadows still flicker "a bit" ...")
  Detail (from its ticked duplicate "NEXT BATCH: swaying grass shadows still flicker"): helper65, ATN's 01:52 log +
  profile: no limit hit (no slow request, no room growth, nothing left out, placed at most 293 K, compact draws steady
  with a still camera). Suspects: Ray Reconstruction on thin moving shadows over still ground (no motion for shadows),
  the sway copy's time a frame behind the picture, grass flipping between sway and still at the share's edge while
  moving.
- [x] DONE 11-15 (ATN 12:1x: everything faster with the same picture on by default: 356b7100): Batched Unit Capture on by default? (bit-exact; bench: -1 to -2 ms a frame, +1.2 fps in both views) (from: "ATN DECIDES: Batched Unit Capture (RayTracedBatchedUnits) on by default ...")
- [ ] g2b trunc8 (8-bit cluster truncation, -0.2 ms, a look change): try it? (from: "ATN DECIDES: g2b trunc8 ...")
  Detail (from its ticked duplicate "ATN DECIDES: g2b trunc8"): -0.2 ms more cluster build.
- [ ] GPU grass placement (phases 1-3): go ahead? (probe: the game's grass update 1.2 ms on the CPU at maxed grass vs 0.02 ms on the GPU, bit-exact) (from: "GRASS after the probe: ...")
  Detail (from its ticked duplicate "GRASS after the probe: part-draw cull"): after ATN's yes (next grass agent):
  part-draw cull (vtable +0x10c), burn writers (the probe: only burn changes, 256-269 records in 35 s, grid static: a
  GPU pass needs the records' burn each update or the writer hooked), frustum entry +0x8c if needed, AMD / Intel
  self-test (phase 1).
- [x] DONE 20:5x (ATN: "ray traced shadows for objects should join the RUSE round too"): R.U.S.E.: RT shadows for objects too this round (+2,000 replayed depth draws, CPU cost unknown until the T-run), or
  ground RT shadows / AO / GI / water reflections first? Path tracing can't work (forward renderer). (from: "R.U.S.E. not
  this round")
- [ ] HDR audit 20:3x look changes (R1 shoulders, R2 Shogun 2 light, R3 gamma 2.0, R5 legacy hue, R6 bloom, Extended
  Saturation, Div1 mode 1): which become Experimental rows? (from: "HDR AUDIT FINDINGS")
Added 11-16 from the HANDOFF audit (C = you decide, B = you check in game):
- [x] DONE 11-16: Ryse windowed fix (fix58, ryw_ d52aa32e merged 6e1c247b): you checked windowed, fine (04:2x) (from: "AFTER THE RESTART, in order")
- [ ] C: Ryse helper DLSS presets L/M: the sqrt feed (Attila's dark-tone fix) or linear; the helper's AutoExposure where Ryse gave exposure 1? (from: "ATN DECIDES: Ryse's helper DLSS presets L/M get the sqrt feed ...")
- [ ] C: Tales' rays need an upscaler on, Path Tracing Bounces capped at 2, RT/PT preset = Attila's values: OK by eye? (from: "ATN DECIDES: Tales' rays now need an upscaler on ...")
- [ ] C: real GT-VBAO savings need per-sample math changes (a look change): try a visual A/B? (from: "Real GT-VBAO savings need per-sample math changes ...")
- [ ] C: Expeditions save load by script: a foreground fix or a mouse click? (from: "Expeditions save load: the main menu takes no SendInput keys ...")
- [ ] C: Divinity 1 Hue Clip 70 in Extended / V35, by eye? (from: "ATN by eye (Divinity 1): Hue Clip 70 in Extended / V35 ...")
- [ ] C: Divinity 1 at the end: drop or keep the legacy V30 / Pragmap modes? (from: "ATN at the end (Divinity 1): drop or keep the legacy V30 / Pragmap modes ...")
- [ ] C: Divinity 2 "Exact Colour LUT (Experimental)" (off): turn it on? (the game reads its LUT half a texel off; on brightens mid tones) (from: "ATN DECIDES (Divinity 2): "Exact Colour LUT (Experimental)" ...")
- [ ] C: Divinity 2 at the end: the mid grey row's default; old presets / V30 keep or drop? (from: "ATN at the end (Divinity 2): mid grey row's default ...")
- [ ] C: R.U.S.E.: keep its own V31 rows or move to Attila's 15 v31_* rows (saved V31 looks change)? Hue Clip 100 by eye? (from: "ATN DECIDES (R.U.S.E.): keep R.U.S.E.'s own V31 rows ...")
- [ ] C: R.U.S.E. at the end: Extended replacing V31 Native Tone or beside it? (from: "ATN at the end (R.U.S.E.): Extended replacing V31 Native Tone ...")
- [ ] C: Hue Clip by eye: Ryse 80, Tales 70, Expeditions 70? (from: "ATN by eye: Hue Clip Ryse 80 ...")
- [ ] C: WH3: Fog in Reflections (Experimental, 0) into the RT/PT preset now that Water Reflections are on by default? (from: "ATN DECIDES (WH3, lost in the cleanup): Fog in Reflections ...")
- [ ] B: Ryse rays + RR with a moving camera: look right? (from: "Ryse rays + RR in motion ...")
- [ ] B: a cutscene with Cutscene Point Light Shadows + the new row; Half + RR past torches; a battle with 4-bone / dismember units under Batched Unit Capture: look right? (from: "In game (ATN's list, per batch): a cutscene with Cutscene Point Light Shadows ...")
- [ ] B: R.U.S.E. marker / map-interface colours in SDR / Extended / V35: look right? (from: "R.U.S.E. C2: 15 marker / map-interface draws ...")
- [ ] B: Attila / Thrones / Rome II / WH1 / WH2 / Troy / Pharaoh / WH3 2.2 Gamma Correction in HDR (5f63ed7c, WH3 10eb0417): scene, UI, flares, video, FG HUD-less look right? (from: "Attila / Thrones 2.2 GC in HDR: ATN's in-game look ...")
- [ ] B: HDR bug fixes: Output SDR colours (V30 / V31), Shogun 2 highlights up to Peak, WH2 with FXAA on: look right? (from: "In-game checks of the HDR bug fixes ...")
- [ ] B: Rome Remastered: do keys / clicks still register after a long session? (from: "Rome Remastered input bug: ATN watches input ...")
- [ ] B: DLSS migration list (old values kept, no red note, the game's DLSS as before, helper DLSS / FSR / XeSS / RR, FG + Reflex, RT / PT after restart, helper exe removed = no crash): all fine? (from: "In game (ATN's list): migration keeps the old DLSS values ...")
- [ ] B: after everything: switch your CPU set rule to an affinity rule of the same 16 CPUs for one A/B (we measure)? (from: "After everything: one A/B of ATN's CPU set rule ...")
- [ ] B: OK for the Rome Remastered IB-rewrite trace at its next data run (you load a battle)? (from: "Rome Remastered IB-rewrite trace ...")
- [ ] B: Divinity 2 Higher Precision Scene A/B + gamma 1.2 look: after an_'s analysis (from: "Divinity 2 next run: what triggers the orthographic camera ...")
Also waiting on ATN, newer than the audit: "In game (ATN's list): new player HDR off -> SDR preset" (11-16: merged into the SDR first start entry above) (SDR first start ->
HDR later, 24cdfb9c).

## OPEN (owners)
Pointer index of every open box below (short title | owner, file order); the detailed line stays where it is.
Rebuild it when boxes change; the boxes themselves are the record.
- Unit round: A1 (Attila UnitDistances[16] = 0 under Shadows Only, if the trace sh ... | coordinator
- A2 sprite soldiers' 276 shadow draws into the sun map (submission code not found ... | coordinator, next CPU round (find the submission code in a t
- ATN ~19:xx: "No more performance improvements on our side and the game's?" From ... | next CPU round
- tools: compare_test.cpp zeroes uint vertex inputs (can't catch bone-index bugs); ... | coordinator, tools pass after the batch
- Author packages: zip READY 12:1x in ATN's Downloads (AttilaCPUPerfFix_7.0_notes_ ... | ATN (merged from "ATN, when sending the heapfix package: ask
- Thrones Grass Dump button: only when its grass port starts. Owner: that round | that round
- U2 step 3 (own light reservoir); record at bounce size at Half (VRAM); f16 relat ... | next ReSTIR round
- Field-scene ray slowdown after merging unit clusters unexplained; fewer unit tri ... | coordinator
- A32: under GPU Trace without REPLAYABLE the helper commits ~838 MB/s for ~15 s ( ... | Nsight round (from the J3 / A32 line: one Nsight run without
- ns_trace_run.py must end its "rescue" helper (one blocked the next job 7+ minute ... | Nsight tooling
- Resource upgrade 0.196 ms = ALL gtl resource-info lookups (OnPushDescriptors 0.0 ... | CPU round (shared util, full build)
- WH3 grass read-back without the wait: 8 of 78 traces have indirect grass (14-25 ... | WH3 data run (coordinator), then GPU ROUND 4+ (Same work as 
- J3 in game (one grass pixel's G-buffer read): next data run. A32: one Nsight run ... | coordinator (Its A32 part merged into the A32 line above
- Scorer: reflections / fog / water features, a faint-smoke reference; the smoke f ... | next scorer round
- DLSS RR 310.9 made paths boil (paths 2.19 -> 5.01 % rRMSE, noise 0.54 -> 4.13): ... | next RR round / ATN
- WARP ramp for the four new SDR presets. Owner: next TW round | next TW round
- README New: lines for Troy, WH2, Rome Remastered, Shogun 2, WH3. Owner: release ... | release round
- ATN 02:3x (for later): per-game GitHub download links instead of the shared rele ... | release round
- WARP ramp of the UE SDR encodes; Ryse's redundant renodx_trace_now poll; the Exp ... | UE4 / Ryse merge rounds
- Ryse rays + RR in motion (s8b sat on the tutorial pause: still camera): a moving ... | next Ryse round (R2)
- Ryse: cvars from the ini (system.cfg is read before the cvars exist: every line ... | next Ryse round
- Ryse: helper round trip 2.5 (DLSS Quality) to 4.7 ms (DLAA) a frame against 0.25 ... | next Ryse round
- Ryse FG 2x pacing: frames presented in pairs 0.15 ms apart, shown 2.7-9.3 ms, p9 ... | next Ryse round
- Ryse: port 0x292F2865 (half-res shadow, every start). Owner: next Ryse round (wi ... | next Ryse round (with the families box)
- tools/ingame/deploy.py:30 (_dlss zip entry) and tools/ryse/fg_camera_check.py (r ... | coordinator, release round
- ATN DECIDES: Ryse's helper DLSS presets L/M get the sqrt feed (Attila's dark-ton ... | ATN after R0
- WARP check of rt_specular_merge; switch to twattila/settings_pages.hpp once it d ... | next Ryse round
- Port the new shader families (tools/nontw/ryse/rym_shaders: families.tsv): tonem ... | next Ryse round (after R0)
- Tales' rays still run in process (rt_shadows.hpp on its own D3D12 device): move ... | next UE round
- ATN DECIDES: Tales' rays now need an upscaler on (before: also with the game's T ... | ATN
- Dead files: talesofarise_dlss/rr_d3d12.hpp (no user), ngx_backend.hpp, 0x7A1E0D4 ... | coordinator, release round
- Expeditions left: the three max(0) carve materials (G1 data run), micromaps star ... | next UE round (after G1)
- Wire RayTracedLightRatioFollow into Shogun 2 (its own rows). Owner: next Shogun ... | next Shogun 2 round
- Skip the game's point-shadow casters under the row (shadow_pointlight VS 7DF3D76 ... | next D3D11 round
- In game (ATN's list, per batch): a cutscene with Cutscene Point Light Shadows + ... | ATN (in game)
- Real GT-VBAO savings need per-sample math changes (look change): visual A/B, ATN ... | later
- release_rr's +30 MiB threshold is too tight (the base helper reaches 419 MiB too ... | next harness round
- shadow_pass: (a) the row's next game run reads the new off-thread warning (if it ... | next Attila CPU / D3D11 round
- Two Shadow Cascades live rebuild (no restart). Owner: next D3D11 round | next D3D11 round
- +265 MB VRAM for the bounce-size ReSTIR record. Owner: next ReSTIR round | next ReSTIR round
- Shogun 2 terrain_detail not restored at detach (minor). Owner: next Shogun 2 rou ... | next Shogun 2 round
- Faster-with-a-look-risk list for ATN (numbers in the reply 12:1x): Lean ReSTIR R ... | ATN
- Release round: player / release zips carry no Streamline (release_zip.py, player ... | coordinator, release round
- Tales: G2's camp save has no torch / night / indoor view; the in-process RT has ... | next UE round
- Expeditions rays-on DLAA 22.9 fps with GPU busy 9.31 ms (frame 43.7 ms): a stall ... | next UE round
- Expeditions save load: the main menu takes no SendInput keys; the save scene (ca ... | ATN decides (foreground fix / a mouse click), then coordinat
- Expeditions unreplaced post into 8-bit f90 targets before the tonemapper: A45907 ... | next UE round
- ATN by eye (Divinity 1): Hue Clip 70 in Extended / V35. Owner: ATN | ATN
- D1 steps 9-10 + Graphics 2 of 2: helper route at 100 % (DLAA / FSR / XeSS, b12 j ... | d1_ after R1
- ATN at the end (Divinity 1): drop or keep the legacy V30 / Pragmap modes. Owner: ... | ATN
- ATN DECIDES (Divinity 2): "Exact Colour LUT (Experimental)", off: the game reads ... | ATN
- Divinity 2 SHARED STEP: output_encoding needs a game-owned-output flag (Apply mu ... | coordinator (shared-steps round after the fix merge)
- Divinity 2 later: D2-8b scene R11G11B10F -> RGBA16F (view cloning, Experimental ... | d2_ after R1b
- Divinity 2 next run: what triggers the orthographic camera (tactical toggle?); A ... | next Divinity 2 run / ht_
- ATN at the end (Divinity 2): mid grey row's default; old presets / V30 keep or d ... | ATN
- R.U.S.E. U1 run (after ReShade + the stamped add-on are installed on D:): Frame ... | coordinator (next game window, ATN's OK)
- R.U.S.E. open ledger rows: Time Each Pass (D3D9 timestamp queries, optional); th ... | ru_ / release round
- ATN DECIDES (R.U.S.E.): keep R.U.S.E.'s own V31 rows or move to Attila's 15 v31_ ... | ATN
- R.U.S.E. C2: 15 marker / map-interface draws pre-undo the post grade (authored c ... | ru_
- ATN at the end (R.U.S.E.): Extended replacing V31 Native Tone or beside it; Hue ... | ATN
- Divinity 1 offline from R1's traces: R7 RGBA8-upgrade readers, census clamp cand ... | next Divinity 1 agent
- tw_install.py's include scanner counts includes inside an #ifndef the add-on def ... | coordinator (tools)
- tw_install.py: stamp only through it. Stamping a build in place made it refuse; ... | coordinator (note)
- R.U.S.E. T-run list (ATN loads a battle, one row at a time with PresentMon): DLA ... | coordinator (T-run with ATN)
- R.U.S.E.: Ray Reconstruction for the rays (today DLAA preset K + our own denoise ... | ATN decides, then the game's next round
- R.U.S.E.: test hooks (RENODX_TW_SET) for scripted runs. twattila/test_hooks.hpp ... | next tools pass
- R.U.S.E. runs (notes): the game ignores window close in battle (two tries, then ... | coordinator (notes for runs)
- Attila / Thrones / Rome II / WH1 / WH2 / Troy / Pharaoh / WH3 2.2 GC in HDR: ATN ... | ATN
- AFTER THE RESTART, in order (nothing of this started before it): 1. DONE (non-TW ... | coordinator (steps 2, 4, 5), ATN (step 6)
- Attila has no frame trace `on_demand` / `at_100` (Pharaoh, Rome II, WH3 have bot ... | next Attila round
- grass_fade.hpp:36: unbound cb8 reads 0 = no fade, against the skill's "unbound i ... | next Attila round
- SKILL AUDIT 11-16 (ATN: "double check that every game of ours hasn't missed any ... | coordinator 06:xx rao_ merged ad541a4f (RT AO replaces the g
- dvf_ not done: VFX / indicator Experimental rows in both games (D1 633 FX_MAT an ... | next Divinity round / runs, ATN (VFX scope, C rows)
- tex_ not done: Expeditions Video AutoHDR (NV12 convert ps 0x524CDCDF; needs one ... | next UE round, coordinator (release), shs_
- w3f_ not done: vanilla_exact over every WH3 shader + a lensflare00 control (~20 ... | next WH3 round, shs_
- s2r_ not done: Rome Remastered Modded Auto Exposure (Feral's AE compute not iden ... | next Shogun 2 / Rome Remastered rounds and data runs
- gpu2_ next wins: (1) paths at 128 registers (split trace / shade, or SASS livene ... | next GPU round, next ReSTIR round, tools owner
- Shared leftovers (shs_ / ryf_): rt_core OnAnyDraw replays every draw while trace ... | next shared round, next WH3 round, release round
- ryf_ not done: row 9 e_GsmLodsNum under the rays' sun (trace: which lod is the c ... | next Ryse round / data run
- tpf_ not done: Torch / Fire Shadows on the 7 other point-light PS per game (14 W ... | next Troy / Pharaoh round, data run
- w12_ not done: Rays Replace Shadow Maps as default (no WARP proof of the replace ... | next WH1 / WH2 round
- rux_ not done: RT debug view, moving units under DLAA, combined O1 variant, Time ... | next R
- r2fix_ not done: cut-outs (cut_out_shader capture versions first); Batched Unit ... | next Rome II round, shared round, data run
- ath_ not done: unclear-sun WARP 2x test (B6DA03C5 / 1B10DDD8), traces of 0FB7344 ... | next Attila round / data runs, per-game agents (t46)
- Non-TW 17-row leftovers: Tales' path tracing always uses SER on DXR 1.2 (rt_shad ... | next GPU round (SER), next data runs (rest)
- From sk4_ (skills update 3, 1e75898a / ec000d46 / 47a73741, zip 48 files): Phara ... | per-game rounds (SMAA / CMAA2, fallback), coordinator (READM
- 17-ROW CHECK (ATN 06:xx: the first audits ran before the 17 Attila rows were in ... | coordinator
- Group 1 leftovers: Grass Ray Tracing Range for grass captured as drawn (Rome II, ... | next GPU round (grass range), next data runs (VFX)
- ATN DECIDES: UI 4K Mod Compatibility for Thrones and Rome II, only if a UI mod f ... | ATN
- Group 2 leftovers: WH3 Grass Ray Tracing Range for GPU-placed grass (new work, m ... | next per-game data runs / rounds
- ATN DECIDES: Shogun 2 Recommended Video AutoHDR is 0.25, the skill says 0.2 (Tal ... | ATN
- SHARED STEP (from the audits): SetRaysPresetDefaults makes Fog Shadows + Specula ... | SHARED STEP agent (after rao_)
- Ryse ray costs (RT shadows +1.0 ms, PT +2.7 ms at 4K DLSS Quality): Nsight work ... | a later GPU round
- RYSE RT / PT IDEAS (ATN 11-16 "don't add them yet"; ATN decides when, then plan ... | ATN (go-ahead), then a Ryse round
- Tales Bend / GT-VBGI look shots: rerun tales\g1g2\run.ps1 -Bend with a mouse cli ... | coordinator
- REDO 11-16 (rd_, tools/tw/round_notes/redo_1116.md): Ryse cost runs r0/run_r2cos ... | coordinator, Ryse fix agent
- RUN ANALYSIS 11-16 (tools/tw/round_notes/runs_1116_analysis.md, pictures an_1116 ... | coordinator + per-game rounds
- Replace audit (ATN 2026-11-16: "Everything that we add, GT-VBAO, RT AO, PT AO, G ... | rao_, then the next replace round
- Attila features vs skills: sk3_ is comparing; then ATN decides which to add. Own ... | sk3_, then ATN
- Tonight's run analysis: an_ is writing tools/tw/round_notes/runs_1116_analysis.m ... | coordinator
- Ryse redo: per-state GPU cost (PresentMon GPU busy per state + a copy of the pas ... | coordinator tonight
- In-game checks of the HDR bug fixes: Output SDR colours (BT.709 in V30 / V31), S ... | next TW data runs / release round
- Ryse next run: Time Each Pass + PresentMon with Skip the Game's Sun Shadow Maps ... | next Ryse data run / ATN
- Ryse: VFX rows need a trace (particle VS candidates in tools/nontw/ryse/r0/famil ... | next Ryse data run
- Tales / Expeditions Bend + GT-VBGI: in-game look + GPU cost, then defaults on (A ... | ATN
- Both D3D12 devices (Streamline FG + RR) removed at the same moment: every order ... | next helper round
- [B] R7 no reader inventory for Shogun 2's broad upgrade (addon.cpp:2576-2594), D ... | HDR bug round (inventory from traces)
- [B] R10 unproven 8-bit links (never traced): Pharaoh FXAA 0xA6756C1F, WH2 dof00/ ... | next data run per game
- [B] R14 variants not replaced (traces decide if they draw): Attila / Thrones D91 ... | next data run per game, then the HDR bug round
- [P] ATN DECIDES R1 Div1 mode 1 "UC2 Extended / PsychoV30" = UpgradeToneMap then ... | ATN
- [P] ATN DECIDES R1 shoulders not identity below their start: Neutwo from 0 (grad ... | ATN
- [P] ATN DECIDES R2 Shogun 2 pointlight01_0x57139F8D:258 saturate after HDR Firel ... | ATN
- [P] ATN DECIDES R5 hue settings after PsychoV in legacy V30 / V31 modes (all TW ... | ATN
- [P] ATN DECIDES R6 bloom: smoothing also in Rendering Mode SDR (Vanilla != game: ... | ATN
- [P] ATN DECIDES Extended Saturation = RGB blend to grey, not OKLab chroma (WH2 : ... | ATN
- PARTLY DONE a0057296 (ssao auto thickness Experimental off; censuses T9 / T10 / ... | non-TW parity round (Tales)
- PARTLY DONE a0057296 (4 mapped, 6 half-size-copy ones mapped but not bypassed, 4 ... | non-TW parity round (Expeditions)
- All three: parity_check letters + tools/tw/parity ledgers (ryse / tales / expedi ... | non-TW parity round (coordinator part)
- ATN by eye: Hue Clip Ryse 80 (addon.cpp:87), Tales 70 (:67), Expeditions 70 (:63 ... | ATN
- Release (all three): README entries (Tales last v71 at :369; Expeditions README_ ... | coordinator, release round
- Never verified in game (all three): SDR first start / SDR preset / SDR output; t ... | coordinator (next game window + harness)
- ATN DECIDES (WH3, lost in the cleanup): Fog in Reflections (RayTracedReflectionF ... | ATN
- Attila v118 / v119 "Left" list (archive 1878-1885, 3589): Light Bounce +1 ms wit ... | next Attila RT round
- ATN A/B tests named in the "No more performance improvements" box without their ... | ATN (in game)
- Attila 10-29 / 10-30 archive items: river noise while moving (RR / reflections) ... | next Attila RT round / harness
- Rome Remastered input bug: ATN watches input after a long session (ticked line H ... | ATN (in game)
- Astra review: the tester's first freeze still unexplained (archive 1344): check ... | coordinator
- WH3 v40 / batch 6 "Not done" (archive 1270, 1315-1331; wh3_v40 / wh3_batch6 repo ... | next WH3 round (+ ATN for the tent)
- Pharaoh below 100 %: p2ph_scale_census.txt still has 8 BUG (B) rows (shroud deca ... | next Pharaoh round
- Path tracing register limit (1.47-1.81 ms in game, 28.7 % occupancy, 85 % regist ... | next GPU round
- Attila reflections (1.07-1.16 ms, latency-bound, 88 % registers; hot TRACE_SURFA ... | next GPU round
- Radiance cache (under 0.23 ms; dependent loads, SM 3 %): at most -0.05 to -0.10 ... | coordinator (next GPU round)
- R21 skipped (Shogun 2 NGX feature release): needs the game's static NVSDK_NGX_D3 ... | next Shogun 2 round
- test/tw_native_hook_lifetime does not compile (x86 / amd64): graphics_hooks.hpp ... | next test round
- fx_ notes: R1 the kept lost RR object holds its removed D3D12 device (RR / RT / ... | coordinator (harness before the install)
- WH3's x64 render-list WPR run: the game said "Unable to initialize SteamAPI" twi ... | ATN, then coordinator
- In game (ATN's list): migration keeps the old DLSS values (log line once); no re ... | ATN (in game)
- Port RayTracedBatchedUnits + RayTracedUnitsRange to Shogun 2 (SHARED STEP, after ... | GPU round after the install
- Cosmetic: moved files keep "[Shogun 2 DLSS]" log tags; HLSL comments "(DLSS addo ... | coordinator, release round
- SHARED STEP owners (coordinator 01:2x): UE4 S1 cvars per-exe slot table, S2 the ... | UE4 MERGE (uem_)
- 128 torch lights' cost at night / burning city / WH3 / Pharaoh: a variant in the ... | coordinator, end-of-batch bench (torch-128 night / burning-c
- Rarer Objects needs a view with siege towers or ships (bench scene choice) Owner ... | coordinator, end-of-batch bench
- After everything: one A/B of ATN's CPU set rule against an affinity rule of the ... | the bench, ATN's OK at the time
- caster_census.py for Feral's engine (a depth-prepass scene_draw host, VSMain ent ... | coordinator, after parity round 2 (shared script)
- D9123DE1 + its MSAA / stencil builds (constant tone mapper "with_bloom_and_gamma ... | the next data run or ATN's trace
- render_pass_behavior / shader_injection_behavior suites: ATN 02:2x OK to downloa ... | coordinator, tools pass
- Rome Remastered IB-rewrite trace (whether Feral rewrites index buffers, b6ec6be6 ... | the next Rome Remastered data run (coordinator)
- Look-changing GPU savings (half-rate rays for grass / far soldiers, float16 anim ... | GPU round after the end bench

## CARRIED OVER (open lines of archived sections; HANDOFF_ARCHIVE.md, 2026-11-15)
From "ATN'S 2026-11-13 BATCH (day: ATN out ~11:00-19:00; agent runs allowed while ATN isn't playing)":
- [ ] Unit round: A1 (Attila UnitDistances[16] = 0 under Shadows Only, if the trace shows a soldier shadow pass), D (WH3
  cross-pass unit capture key), B (trim per-unit capture work), E (TBB grain test rows). Owner (audit 11-15): coordinator. A1 DONE 19581888; B partly by Batched Unit Capture dd44b703 (2 families); D: WH3 unit geometry in FrameTraceReads 5d570f20 (17efac2b) needs the next WH3 data-run trace; E (TBB grain rows) = SHARED STEP coordinator, rows after the end bench.
  (merged from "Unit round D (WH3 cross-pass unit capture key) and E (TBB gr...": Unit round D (WH3 cross-pass unit
  capture key) and E (TBB grain rows), line 37: owner PARITY ROUND 2 WH3)
  - [x] A1 19581888: index 16 = 0 while rt_scene::CastersSkipped() (the next frame's sun-map casters skipped), the game's
    10000 back the frame it ends; index 16 = entity_display_shadow_max_shadow_distance read by the soldiers' shadow record
    (+0x441eb0 / +0x441f49, bytes read). test/tw_unit_shadows x86 + amd64 PASS. Later run: the shadow pack mark's count.
  - NOT DONE: A2 (sprite soldiers' 276 shadow draws into the map): entity_display_enable_imposter_shadows's only reader
    (+0x43c70d) sets index 16 to 10000, it never stops those draws; their submission code not found offline. Nothing
    visible would change (every caster of the map is skipped under the rays), only the game's submission cost (~0.1-0.2 ms).
  - [ ] A2 sprite soldiers' 276 shadow draws into the sun map (submission code not found offline; ~0.1-0.2 ms of the game's submission cost, no look change). Owner: coordinator, next CPU round (find the submission code in a trace first)
From "v121 (ATN 2026-11-01, quick asks; commit after every step, never push unless ATN asks, tick here)":
- [x] DUPLICATE of "do swaying grass shadows still flicker" in OPEN FOR ATN (11-16): NEXT BATCH: swaying grass shadows still flicker "a bit" with helper65 (ATN, 01:52 log + profile): no limit hit (no
      slow request, no room growth, nothing left out, placed at most 293 K, compact draws steady with a still camera).
      Suspects: Ray Reconstruction on thin moving shadows over still ground (no motion for shadows), the sway copy's time
      a frame behind the picture, grass flipping between sway and still at the share's edge while moving. Test: Swaying
      Shadows Range 50 m / Off-Screen Sway Off / RR off (DLSS without RR) to tell them apart. Owner: ATN.

## SIX GAMES TO THE ATTILA PACKAGE, CONTINUED (2026-11-14 from ~10:4x; the night part 00:00-10:45 and its
heading text are in HANDOFF_ARCHIVE.md)
- [x] ATN ~10:4x: (1) Rome Remastered on Attila's page layout + preset buttons (the skill has it: shaders-ui rows 59 / 65)
  -> Rome Remastered agent after resets / DLAA / DLSS below 100 %; (2) its base-game input bug (keys / clicks stop
  registering; players' fix RunAsDate year 2020): study the date-dependent input path read-only, then a detour of that one
  routine as an Experimental row; (3) Nsight: a whole-frame Attila check plan (game D3D11 + add-on + helper; 32-bit: tool
  per part), fixes behind switches with off = bit-identical, ONE in-game Attila run allowed on the Constantinople replay;
  then WH3 (harness only; an in-game run needs ATN's OK); (4) heapfix (Attila CPU Performance Fix): no fork / publish; its
  code in WH3 = "elsewhere" in its licence: needs the author's written OK first (ATN asks him). DONE: (1) a4d6e0a6 / 09a083f0 Attila page + presets; (2) input bug studied c0a61b2d (protection-like code: not patched, ATN watches input); (3) tools/tw/nsight/attila_frame_results.md + WH3 harness 8.4-8.5 ms; (4) tools/tw/heapfix_wh3 study.
- [x] ATN's Rome Remastered runs (FrameTrace=1 in its ReShade.ini, backup ini_backup_20261114_101134; prefs
  resolution_scale-1 100 -> 67 -> 50, backup prefs_backup_20261114_102403): in-game Resolution Scale change CRASHED
  (exe +0x6ee594, null sRGB RTV); 67 % run 10:24 (17 traces runs/ATN_multi_romer_102433; battle start "hung multiple
  times" = two GPU resets of the helper's ray-tracing device 10:27:42 / :50, rays off after; no RR in the log); 50 % run
  10:31-10:35 rays off, clean (15 traces runs/ATN_multi_romer_103123). 10:36 FrameTrace=1 removed, scale back to 100 (ATN's
  own ini changes kept; backup restore_backup_20261114_103625). Crash FIXED on the agent's branch f6a7cfc4 (Renderer::Resize
  took the upgraded RGBA16 back buffer format; detour passes the game's 8-bit one; tw_romer_dlaa case 5), not merged yet:
  agent on the resets, DLAA below 100 %, DLSS below 100 % from the traces.
- [x] ATN ~10:2x: "fix the nsight issue ... then improve the performance in Attila": Nsight agent resumed: phase A a
  replay-able helper mode (own fence / textures, no factory RR device, clusters off), capture + replay + shader swap;
  phase B Attila perf variants behind switches (GPU Trace + quality scorer; defaults bit-identical; no GPU job while a
  game runs). WH1 agent resumed: AO / SSR reader census from the existing traces (else a trace with gfx_ssao /
  gfx_screen_space_reflections false set by us, DLSS Performance). DONE: phase A 29b54d18, phase B 77958ae7 + 10a98983 (Nsight agent DONE); WH1 census 0c28a0ea.
- [x] DUPLICATE of "Rome Remastered battle with rays at Battle Objects Tested" in OPEN FOR ATN (11-16): Rome Remastered battle GPU resets: safe-side fix a8afd660 on the agent's branch (rays take only the tested capture
  kinds; banners out; row Ray Tracing Battle Objects (Test): Tested / Solid / All = v11; WARP tw_romer_rays case 1). Not
  merged. ATN TEST later: a battle with rays at Tested, Solid, All: the first to reset names the group. RR not involved.
  ATN ~11:2x: when the Rome Remastered agent's work is merged, a Fable 5.2 agent (medium effort, ATN's OK) hunts the battle
  reset: read-only review of the capture -> bridge -> helper path for the battle kinds, a harness repro from the battle
  traces, DRED breadcrumbs in the harness, then fixes proven offline (the Tested / Solid / All row stays until then).
  ATN ~11:4x CONFIRMED: no Fable review per merge (tokens); per merge the usual checks (builds, suites, dumps, harness); ONE
  Fable 5.2 HIGH review of the whole merged tree at the end, before the install. Owner: ATN (in game)
- [x] ATN 11:02 restarted Steam non-elevated (checked: PID 27416, module readable): Nsight agent told to do its one Attila
  Constantinople run.
  ATN ~11:5x: the run (or ONE extra run) with grass / unit LOD / model / building detail maxed (our rows via RENODX_TW_SET,
  prefs with backup), camera left to the map border (all units + artillery smoke on screen); Time Each Pass, helper capture,
  PresentMon, Log Details; CPU ETL only through ATN's own elevated tasks RenoDX_WPR_Start / _Stop if ATN creates them; then
  a ranked units / buildings / grass / smoke / rays speed-up analysis.
  DONE 11:35-11:39 (run cpl2, battle 44 s after launch; camera inside the walls by the Golden Horn, most units + artillery
  smoke on screen): Time Each Pass x3, profile 30 s, PresentMon 30 s, Log Details, WPR CPU+GPU ETL 20 s (3.0 GB); game closed
  through its window; ReShade.ini restored to its pre-run hash F0C01BD0.., preset / prefs / auto_save.replay / add-on unchanged.
  No in-game Nsight capture (REPLAYABLE turns RR off for the helper's life; the harness capture rp_a2 is used). Cluster build
  preference FAST_BUILD / FAST_TRACE: no gain (not committed). ANALYSIS DONE (tools/tw/nsight/attila_frame_results.md):
  cpl1 outside the walls render-thread bound 37.4 ms; cpl2 maxed inside the city GPU bound 36.5 ms (game 20.8 + helper 12.0 +
  RR / copies 3.7; game and helper strictly serial). Soldiers 8x 14-20 M tris, 6.8-10 ms GPU, ~17 ms CPU; grass max 4.8 ms GPU
  + ~5 ms CPU; buildings 8x 3.0 ms; hand-offs 18.7 ms of the game's GPU timeline; forests 4.4-4.8 ms submitting casters we
  skip; add-on 5.4 ms on the render thread; Smoke Shadows hit the 262,144 cap. Ranked: Small Soldiers as Sprites A/B (ATN),
  fewer cascades under Rays Replace (A/B Shadows Medium, then a row), Lean Reuse (-0.7 ms), PAUSE WALLPAPER ENGINE (3.0 ms GPU a
  frame while playing: ATN's app setting), trim per-draw callbacks, grass numbers, Light Bounce Half (-1.2 ms, ATN), fewer
  hand-offs (design), smoke cap (needs a stress mode). No gain: SER, cluster build flags. WH3 harness 8.4-8.5 ms. Agent next:
  callbacks trim, cascade row, smoke stress mode, hand-off design. ns-replay-mode 25125baa + 8357ef5c MERGED 29b54d18
  (all_modes 179 / 179). Code items MERGED 77958ae7: light readback key sort + order reuse (identical; -0.5 ms amd64, up to
  -0.84 ms when the lights repeat), Attila Two Shadow Cascades Under Rays (Experimental; tw_unit_shadows part 6 over the
  shipped dll bytes). Hot callbacks (cpl2): OnDrawIndexed 2.22 ms (no identical cut: redesign), Capture 0.90, light sort 0.84,
  DrawObjectMotion 0.63; workers OnDrawScrub 1.79, ScaleModelLods 1.16. Smoke stress: cap 262,144 = build 1.80 ms;
  PREFER_FAST_BUILD -0.66 ms (not bit-identical). Hand-off design: merge requests 0.1-0.3 ms; 2.9 ms untimed helper work.
  BITDEFENDER: blocked a scratch x86 benchmark exe of the Nsight agent; path stopped, exe deleted; ATN TOLD. Agent resumed:
  smoke fast-build row (Experimental), the 2.9 ms helper work.
  Round 2 MERGED 10a98983: Faster Smoke Shadow Build (Experimental, off; Attila + WH3; NEEDS helper113 at the next install:
  smoke BLAS 1.69 -> 1.03 ms at the cap; off = helper112 by 44 headers / HARNESS_DUMP 6 modes / all_modes 179; on: 17.7 % of
  sun-visibility pixels change by 2.5 / 255 at ATN's rows, fog-shadow probe 251 -> 218); light sort 4 x 32-bit keys (x86
  2.02 -> 1.83, amd64 2.08 -> 1.51 ms at 16384 lights, identical). The "2.9 ms" is not helper work: ~1.0 ms hand-off idle
  (0.32 a request, no identical merge possible) + ~1.3-1.7 ms the game's waiting pass + inputs. Nsight agent DONE; ns-replay
  links removed. Follow-up (not started): a smoke / fog feature in the quality scorer's scene. DONE: run cpl2 11:35-11:39 + tools/tw/nsight/attila_frame_results.md; Nsight agent DONE; its scorer follow-up = the "Scorer: reflections / fog / water" line.
- [x] Draw redesign PHASE 1 DONE (tools/tw/draw_redesign/design.md; replay oracle over 9 games, 832 replays, 2.05 M commands):
  dispatch layer 0.72-0.85 ms a frame in cpl2 (~138 ns a command; 0.48 ms in shared_mutex locks), 42.8 % of commands need
  nothing. Design C (per-command-list cache of bound handles + prepared lists, registration epoch, gates for the 3
  command-wide callbacks): saves ~0.52-0.56 ms (cpl2) / 0.37-0.39 (cpl1) of the 2.2 ms; Capture / DrawObjectMotion out of
  scope. Touches src/utils/command_action.hpp (every mod in atn-mods, upstream-merge conflicts), resource_upgrade.hpp,
  view_share / object_motion gates. ATN ~19:xx: "Yes, TW-only opt-in" -> PHASE 2 started (run-time opt-in by the TW add-ons
  only; new code in its own header, minimal hook in command_action.hpp; non-TW mods keep today's path; full proof plan);
  then a FABLE 5.2 HIGH review of this change alone. DONE: DRAW REDESIGN MERGED 63d17575; Fable HIGH review tools/tw/draw_redesign/fable_review.md, fixes 3166f59f..ac649f09.
- [x] ATN ~11:4x: the skill checks the whole dump for render scale (scale_census.py) but casters only in traces: agent
  (cdc_, worktree) adds caster_census.py --whole-dump (every VS classified, held / NOT HELD / not a caster), validates on
  Attila + Pharaoh e34c6089 palms, runs it for nine games (tools/tw/round_notes/caster_dump_<game>.txt), skill lines; results
  -> parity agents. MERGED 612079e9 (2f341c3a): held / in view only / NOT HELD / not a caster per game (Attila 39/16/12/108,
  WH3 44/26/87/222, Pharaoh 28/13/41/182, Rome II 38/10/20/196 + 134 dead, Thrones 39/16/14/107, WH2 26/13/25/168, Troy
  31/10/19/164, WH1 35/15/22/175, Shogun 2 34/12/0/83); tables tools/tw/round_notes/caster_dump_<game>.txt; each game's
  NOT HELD list sent to its parity agent. QUEUED (no agent yet): Attila's own NOT HELD: traced 758C250B rigidterrainblend
  vs30_main (maybe the held rocks' material pass); level B far cards 398BD3DF / B2585ABA, sprites E42AE09F, rigid 9AEE504E
  (displacement), ropes / sail symbols. DONE: 612079e9 caster_census --whole-dump + round_notes/caster_dump_<game>.txt; Attila's queued NOT HELD settled in b43dcc14 (758C250B HAVE; E42AE09F / 398BD3DF / B2585ABA held; 9AEE504E + ropes 8D0D26B9 / 3926A823 Rarer Objects); attila.tsv waits only on D9123DE1 and the point lights (own lines).
- [x] Rome Remastered DLSS below 100 % on the agent's branch 867187d4 (v9-v12 ran DLAA at render size; now DLSS from the
  tone mapper's render-size output into Feral's upscaler target 9F3C9B3D / 3C19543E, its draw skipped; FG keeps it as the
  HUD-less frame; tw_romer_dlaa case 6 WARP PASS; README v13). With a ray row on: still DLAA at render size + Feral's
  upscaler (rt_core captures depth at bridge size) -> task C after A / B: opt-in rt_core Host field, all consumers built.
- [x] Nsight phase A on ns-replay-mode 25125baa (RENODX_TW_REPLAYABLE=1 + RENODX_TW_NO_CLUSTERS=1: capture + ngfx-replay +
  shader swaps; replayed frame reuse 3.05 -> 0.96 ms Lean, frame 8.32 -> 6.40 ms; off = 44 headers byte-identical) and
  8357ef5c (Attila: Time Each Pass (One Frame), WH3's pass_timing.hpp; Nsight can't time 32-bit D3D11). Plan
  tools/tw/nsight/attila_frame_plan.md. In-game Constantinople run BLOCKED: Steam runs elevated (SteamAdminStart): ATN asked.
  Not merged (final all_modes count pending). Junctions in ns-replay: remove link-only before cleanup. DONE: ns-replay-mode 25125baa + 8357ef5c MERGED 29b54d18.
- [x] ATN ~11:0x: "Other games didn't miss any of the skills, right? If they did, we have to do them too." Page dumps at
  0c28a0ea: Thrones / WH2 / Troy / WH1 / WH3 lack many Attila rows (Graphics* engine rows, SSR rows, fog / sky / bloom,
  particle resolution, smoke-map rows, grass height, torch-from-paths rows ...). PARITY ROUND, brief
  tools/tw/round_notes/parity_brief_1114.md (base fdfb307e): worktree agents Thrones (par_th_), WH2 (par_w2_), Troy
  (par_t_), WH1 (par_w1_), Shogun 2 (par_s2_): parity table by feature (HAVE / DO NOW / NEEDS A TRACE / WON'T GET + proof)
  + the DO NOW items, Experimental off; Rome Remastered's agent does the same after its page move; read-only survey of WH3 /
  Pharaoh / Rome II (par_old_: tools/tw/round_notes/parity_<game>_1114.md). After the merges: one shared step moves the torch
  rows from Path Tracing (RayTracedLightsInBounce / RayTracedLightShadowRatio) into graphics_page for every game that can
  carry them. Merge one branch at a time, verify the merged tree once, stamp, install. DONE: parity rounds 1-3 merged; parity_check over all 10 games 0 OPEN.
- [x] Rome II parity round MERGED 8b8ba21d (275 keyed rows: HAVE 224 -> 239; SDR preset + vanilla unit sizes, Far Tree /
  campaign tree / Sprite Soldier Shadows, Fog Strength, Texture LOD Bias, Smoke Shadows without rays, soldier shadow distance 0
  under the skip, cloth / sails / ropes in the rays + Bend thin rows + Smooth Banner Shadows, Fog Shadows + Torch Light in Fog,
  Small Soldiers as Sprites + Sprite Size, Light Through Foliage incl. MSAA; caster census NOT HELD 20 -> 4; shared shader
  ifndefs opt-in (Attila / Thrones bytes SAME); SettingsRevision 2). Hue Clip measured 90 for Rome II (default stays 80: ATN
  DECIDES). Agent resumed: SSR rows, 14 unclear sun rows, DrawScrub skip, banner material read, grass reach.
- [x] ATN ~15:30: "include everything, not everyone plays at highest settings": rule in the skill (d5b92cdb), memory
  feedback_every_setting, sent to every running agent; Thrones resumed for the 4 sm4 / 4.1 SSR shaders below 100 %.
- [x] Fable 5.2 reset hunt DONE, MERGED b53fbfa4 (8a075306): NOT reproduced (0 of 3 resets used; harness fed the battle's own
  draw lists of traces 12 / 15, NaN / origin / collinear meshes, 144 alpha layers, idle release: all clean). Ranked suspects:
  (1) Windows' own D3D12 / DXR 1.1: Rome Remastered had NO D3D12\D3D12Core.dll (only Attila and WH3 have it), (2) wrong
  geometry of a whole family (stretched triangles: 0.2 / 0.5 / 2 % wrong vertices -> 15 / 39 / 118 ms traces), (3) stale
  cluster set on a rewritten index buffer, (4) origin-collapsed markers. Hardening: Battle Objects Solid / All turn on DRED
  (Log Details); a new helper device within 5 s of a trace drops the untested kinds until restart. Side findings for later:
  7DD754DA not flagged terrain (triangles over 200 m dropped), float32 unprojection noise at km range, BasicLit draws of one
  mesh with other b7 matrices share a key. ATN TEST: Tested, then Solid + restart, then All + restart; after a reset send
  renodx-tw-dlaa64.log + ReShade.log.
- [x] Rome Remastered side fixes MERGED 8f050f90 (from the Fable hunt: BasicLit instances per b7 matrix (shared opt-in
  SceneShader::every_draw), battle terrain 7DD754DA flagged terrain, double-precision unprojection 1-5 km within 1 mm (shared
  opt-ins Host::clip_relative / Camera::relative_inverse, rt_vertices #if: the other 11 builds' cso byte-identical), Upscaling
  Debug View (opt-in motion_view in AppendAa), Bend's Softer Away + Debug View; parity DO NOW 0, HAVE 16). Agent DONE. ATN
  tests: README "Fable fixes" 1-4.
- [x] tw_install.py (tools, 13:0x): every helper game gets D3D12\D3D12Core.dll (Agility SDK 1.619.6, as release_zip.py ships
  it) where missing; a different one only with --first (backup tw_install.py.bak_agility). Next install copies it into the
  8 helper games that lack it (all but Attila / WH3).
- [x] ATN ~13:00: standalone test exes allowed; ATN excluded tools	w	estbin in Bitdefender; probes (x86 / x64 mingw, x64
  clang-cl) ran and stayed; all agents told.
- [x] WH1 continuation MERGED 9d945a18 (Grass Height Distance, prop cut-out masks, RT Water Reflections (Experimental; RT/PT on),
  Bloom). Agent resumed on the 6 DO NOW left.
- [x] Pharaoh parity round MERGED 74568884 (SDR preset + first start, Depth of Field, Texture LOD Bias (Experimental), Smoke
  Shadows without rays (Experimental; off while the rays' sun shadows run), Smooth Banner Shadows (RT/PT on), Hue Clip measured:
  fire best at 70 (burn 0.2) / 80 (burn 0): default stays 80; campaign billboards CE5AA8D1 (Troy's capture, WARP 4.3e-5 m) +
  vegetation 3368E9E7 out of view; shared opt-ins rt_core SmokeVertexShader::behaviours_at_cb3, Troy billboard
  RT_BILLBOARD_SCALE (Troy bytes unchanged), psychov35 curve 8; scale census BUG (B) rows settled). Round 2 MERGED 887ee24c:
  Fog Strength on all 24 last-stage fog shaders (patched as created; 1 = bit for bit), Fog & Sky Smoothing on the legacy fog
  (Experimental), sails ED0BC391 + ropes DD5D5694 in the rays (level B), vegetation WARP on Pharaoh's bytes, smoke_v57 / v61
  run again. Pharaoh agent DONE. Left: Sprite Soldier Shadows (GS imposter 27A16995: shared GS capture path, WH1 agent
  building it opt-in), particle rows, grass height (trace), feather / fur and level-B casters (traces).
- [x] Rome Remastered task C MERGED cb98cd5e (DLSS below 100 % with the ray rows on; shared rt_core Host::depth_at_render_size
  opt-in: other games' rt_core suites PASS x86 + amd64, page dumps unchanged; tw_romer_rays case 7). Rome Remastered agent DONE
  (3 DO NOW left: Upscaling Debug View (shared opt-in in AppendAa), Bend's Softer Away + Debug View). ATN runs: Battle Objects
  Tested / Solid / All, 67 / 50 % with RT/PT, README test lines, a FrameTraceReads trace at 67 % + a siege with fire.
- [x] FABLE 5.2 MEDIUM reset hunt (DONE b53fbfa4) started (ATN's OK; worktree, scratch fbl_): review capture -> bridge -> helper, DRED in the
  harness, battle kinds fed at real sizes (max 3 resets), fix + proof, safe-side row stays until proven.
- [x] WH3 parity round MERGED b6cb52a1 (table tools/tw/wh3_parity/parity_1114.md: 188 have / 2 do now left / 2 trace / 81 won't;
  Indicator Brightness (Experimental; MBOIT resolve 492ED8DA left out), four own-flag tests (Far Tree Block Shadows, Point Light
  Shadow Maps, Debris Shadows, Ragdolls), traced casters 03298FD5 + 1C3500F0 held (census 44 -> 46), 54 engine rows level A
  (name scan); Modded Auto Exposure WON'T (the game's AE is off: buffer 0 in 39 traces); Fog & Sky Smoothing WON'T (fog reads
  no cube); scale census at HEAD mostly N/A (WH3 owns its scale, no view_share)). Round 2 MERGED 10b3c753: scale_census.py
  checks view_share before patch verdicts + parses uint64_t slots (skill script; WH3 BUG 10 -> 2, Pharaoh only its 4
  building-water rows to OK), Reflection Quality (Experimental, march step cap), Modded Auto Exposure (Experimental; the exe
  can turn its AE on), Indicator Brightness on the MBOIT resolve, suite test/tw_warhammer3_shaders (casters, march, AE).
  Particle edges STOPPED (effect meshes in the half layer: every material replayed; gain half a texel). WH3 agent DONE.
- [x] Troy parity round MERGED 9095731c (table tools/tw/troy/parity_1114.md: 20 have / 4 done now / 6 trace / 12 won't / 7 open;
  BUG: all 400 Graphics 2 of 2 rows did nothing (the shared lea-name finder matched 0 of 167 heap-copied names + 4 unresolved
  audio at-start tests blocked every slider): twtroy/tweak_objects.hpp finds them by Troy's initializers (memory image),
  384 dead rows removed, 21 rows live; Depth of Field, Smoke Shadows without rays (Experimental), Fog Strength; suites
  tw_troy_tweaks / tw_troy_shaders). Agent resumed on 7 open items. SHARED STEP LATER: per-game leaf cut-out (Troy 0.01 vs
  the helper's LEAF_ALPHA_CUTOFF 0.5), with the torch rows step.
  Round 2 MERGED c28d1591 (campaign terrain 0D70D820 / grass 62E0F935 / vegetation 04C7C807 out of view, Shadow Map Size
  (Experimental; Troy's own table +0x2e81920, quality 4 only: quality 3 = ATN DECIDES), VFX Brightness (10 lit particle PS,
  Experimental), Fog Strength on sm4.1 twins, Particle Resolution / Edges / Smooth Half-Size (Experimental; shared
  particle_layer Install(shaders, readers, Options): Attila / Thrones same behaviour, not byte-identical), fur EE95696C +
  campaign trees 07321725 / 5192C0BA + sails 18CE6F5A / F4B04EF3 in the rays; held 35 -> 40). Conflict in the outside-view GS
  comment resolved; all 10 variants recompile to their headers. Troy agent DONE. Left: GS sprites 370A7440 + tessellated
  D6DEB200 (shared step), leaf cut-out (shared step), 3 traces.
- [x] WH1 parity round MERGED a5787d66 (table tools/tw/wh1/parity_1114.md: 27 have / 8 done now / 3 left / 7 trace / 14 won't;
  Fog Strength, Fog Shadows + torch light in fog (Experimental, RT/PT on), Depth of Field, Smooth Banner Shadows, SDR preset +
  first start + GitHub button, Smoke Shadows (Experimental, not in RT/PT), far card / plant twins held (NOT HELD 22 -> 19),
  Indicator Brightness (Reset wrote 0.4 to a missing row before); WARP 1-17). Agent resumed: Grass Height Distance, 14
  cut-out masks, RT water reflections on 44B4A2F0.
  Continuation MERGED 9d945a18; round 3 MERGED 96aefe57 (campaign trees A8BB2DB2 + props' liquid 766D159E in the rays; AE
  closed: compute atomics + CPU readback). Round 4 MERGED e7ed33e7 (3888C71F out of view via a new RT_COLOR_WORLD variant of
  Pharaoh's shared outside-view GS: the 7 existing variants byte-identical). That agent ran out of context; a FRESH WH1 agent
  (par_w1b_) does the cloth casters, raycast 021D7515 at 50 %, AE via 96BCFCBB.
  Round 5 MERGED ffe5c488 (cloth casters: sails E4B46D0E / 52C2D565 thin, ropes 7BD6E8CB / 1CEFE3BB solid, shadow pass out of
  view; shared rt_core: a cascade_world cascade draw streams through its cascade_keep in world-space games (Shogun 2 unchanged,
  all consumers built); raycast 021D7515 at 50 % proven (part 24); Modded Auto Exposure via 96BCFCBB (Experimental, part 25);
  held 49). WH1 agents DONE. Left: GS sprites + E168387B (shared step), 3 traces.
- [x] SHARED STEP (after the Attila gaps merge; one agent): (1) the torch rows from Path Tracing (RayTracedLightsInBounce /
  RayTracedLightShadowRatio) into graphics_page for every game with PT GI + Torch and Fire Shadows (Thrones, WH2, Troy, WH1,
  Shogun 2, WH3, Pharaoh incl. its particle lights, Rome II); (2) the GS capture path in rt_core (SceneShader::
  capture_geometry_shader + SO statistics count, WH1's design) for the GS imposters of WH1 381E079E / 883D12E7, Pharaoh
  27A16995, Troy / WH2 370A7440; (3) per-game leaf cut-out (Troy 0.01) through the bridge; (4) PREFER_FAST_BUILD smoke row
  to the other games. DONE: SHARED STEP MERGED b435a0e6; its leftovers have own lines (helper114, campaign-tree twin, Experimental row tests).
- [x] Thrones parity round MERGED 348a383b (table tools/tw/thrones/parity_1114.md: 31 have / 9 done now / 1 left / 6 trace / 4
  won't; flares + vignette into FG's HUD-less image, flare occlusion + markers below 100 %, Depth of Field, Indicator
  Brightness, VFX Brightness (Experimental), UI sprite fixes, Water Clarity on sea / lake (shared water_effects
  WaterOpacity.component_only opt-in, 133 / 133 other rows byte-identical, 18 consumers built), Modded Auto Exposure
  (Experimental; CPU-metered at empire.retail.dll +0x18041bf); 758C250B = HAVE). FasterGameCpuCode default: ATN DECIDES
  (off in Thrones, on in Attila). Round 2 MERGED 5395e41f: Object Motion in the Game's Draws (Experimental, restart; Attila's
  in-draw motion + unit records; 30 / 32 bit for bit, 2 grass differ only in an unwritten lane; tw_motion_record /
  tw_unit_pack_rounds PASS; shared graphics_page motion_in_draw opt-in, Attila's page identical); 29 unclear scale rows
  settled (24 sun OK; 4 SSR sm4 / 4.1 bugs only at gfx_shadermodel 1-2: not fixed, trace listed; 0FB73442 in no trace).
  Thrones agent DONE. Note: its WARP checks use small tool exes under tools/tw/thrones (as earlier rounds), not quarantined.
  Round 3 MERGED e73b72c5: the 4 sm4 / 4.1 SSR builds fixed below 100 % (suite test/tw_thrones_ssr), far card builds
  98AA2F91 / A86B4247 + sprite build E42AE09F held; 43 unreplaced builds listed (Attila's bytes too: particles in other shader
  models / shadow methods, point lights with point shadows, MSAA builds, bloom tone mappers). Thrones agent DONE.
- [x] ATN ~13:3x: the per-draw handling REDESIGN (OnDrawIndexed 2.2 ms a frame in cpl2), then a FABLE 5.2 HIGH review of that
  change alone (two perspectives; the end-of-batch whole-tree review stays). Agent drw_ phase 1 (design + draw-stream replay
  oracle + cost map, no src code) started; phase 2 (code) only after the Attila gaps + Nsight smoke work is merged; proof =
  identical decisions per draw over every recorded trace, x86 + amd64, + the suites. DONE: 63d17575 + tools/tw/draw_redesign/fable_review.md (same work as "Draw redesign PHASE 1").
- [x] ATTILA GAPS MERGED b43dcc14 (ssr02-05 shared in twattila, Thrones includes them; E42AE09F / 398BD3DF / B2585ABA held;
  758C250B = HAVE; the 11 other vfx builds edited (suite test/tw_attila_builds); MSAA battle fog A538288B with Fog Strength +
  Smoothing; Rarer Objects in the Rays (Experimental: tessellated rigid 9AEE504E via shared opt-in rt_core
  SceneShader::patch_triangles, ropes 8D0D26B9 / 3926A823); point-light MSAA builds WON'T (no rays / scale under MSAA)).
  Agent resumed: sm4 smoke / particle layer, SSR quality in the 8 sm4 + MSAA builds, Thrones' vfx wrappers.
- [ ] ATN ~19:xx: "No more performance improvements on our side and the game's?" From attila_frame_results.md, NOT yet done:
  (1) the game's shadow pass under Rays Replace + Skip (cpl1 4.4-4.8 ms render thread culling / submitting 1561 skipped draws),
  (2) G0 grass scrub radius clamp (ALREADY DONE f158b659: 1114 audit; bit-exact; E 3-5 ms workers + 0.5-0.9 render thread at maxed grass), (3) add-on worker
  detours 1.2 ms, (4) the render thread's 12 ms unnamed tail (research), (5) Capture 0.9 + DrawObjectMotion 0.63 (after the
  redesign). CPU perf agent (cpu_) started on 1-4, Attila first, then Rome II / Thrones. ATN's A/B list: Small Soldiers as
  Sprites, game Shadows Medium + Two Shadow Cascades, Lean Reuse, Light Bounce Half, pause Wallpaper Engine (3.0 ms GPU). Owner (audit 11-15): next CPU round. DONE: (1) 05522f9b Skip the Game's Shadow Map Work, (2) f158b659, (4) tools/tw/perf_plan/render_thread_tail.md (438c060f). Left: (3) add-on worker detours 1.2 ms, (5) Capture 0.9 + DrawObjectMotion 0.63 ms after 63d17575: re-time at cpl2 on the merged build (Time Each Pass) in the end bench, then decide.
  (ATN's A/B items have own lines: Lean Reuse row, Skip the Game's Shadow Map Work tests, Faster Render Lists tests, one
  cascade instead of two; Small Soldiers as Sprites, Light Bounce Half, Wallpaper Engine: the "ATN A/B tests named in the" box, merged 11-16.)
- [x] SHARED STEP MERGED b435a0e6 (fast-forward; built only): torch rows from PT in Thrones / Rome II / WH1 / WH2 / Troy /
  Pharaoh (its particle lights too) / WH3 / Shogun 2 (overlay); sprite soldiers through a GS capture (WH1, Troy, WH2,
  Pharaoh; rt_core SceneShader::capture_geometry_shader; tw_rays_captures WARP 5 pairs PASS); tessellated rigid WH1
  E168387B / WH2 4CC2D68F (Troy D6DEB200 WON'T: no hull / domain shaders in its dump); a scene shader's own alpha cut-off
  through the bridge (Shared::rt_leaf_cutoff in v8's padding, RT_OWN_CUTOFF 1024; Troy's campaign trees 0.01); Faster
  Smoke Shadow Build in every smoke game (graphics_page makes the row); WH2 Smoke Shadows without rays. Every new row
  Experimental, off; 18 consumers built; page dumps: only the intended rows. Leftovers:
  - [x] DONE 11-15 (built bc7ba324, all_modes 178 / 179, installed 08:00 in all ten games): helper114 = helper113 + the own cut-off (05b79c7b): build, harness, stamp and install with the next release
    (Troy's trees stay at 0.5 until then). Owner: final verification Owner (audit 11-15): coordinator, final verification (installed helper is still helper112; helper113 / g3b2 / 114 never stamped): build the next helper at HEAD (113 + 05b79c7b cut-off + 3B surface record), all_modes, stamp, install with the next release.
  - [x] Pharaoh's campaign-tree twin 1C4B8681 at 0.01? Owner: PARITY ROUND 2 Pharaoh DONE: 1C4B8681 cuts at 0.505, the helper's 0.5 stays (08396134).
  - [x] DUPLICATE of "test the shared-step rows" in OPEN FOR ATN (11-16): ATN's in-game tests of the new Experimental rows (README lines, level B) Owner: ATN.
- [x] PARITY ROUND 2 started ~18:20 (brief tools/tw/round_notes/parity_round2_brief_1114.md; goal parity_check exits 0
  per game): agents p2w3_ (WH3 + own ReSTIR reflections row + unit round D / E), p2ph_ (Pharaoh), p2r2_ (Rome II), p2rr_
  (Rome Remastered), p2s2_ (Shogun 2), p2sm_ (Thrones, WH1, Troy, WH2 in that order). Merge one at a time, then the final
  verification. DONE: all game sub-boxes ticked; PARITY ROUND 3 merged, parity_check 0 OPEN in 10 games (its open children stay).
  - [x] Rome Remastered DONE (1c4f8f36, fddd7ae6 cherry-picked; ledger + README only, no code): parity_check rr exits 0
    (open 0 / waiting 49 / settled 235). 85 casters: 29 HAVE, 33 NEEDS A TRACE (one battle trace with FrameTraceReads=1,
    already on ATN's list), 23 WON'T GET with proof; ShadowCascades / SkipShadowPass WON'T (game's own 1-4 cascades,
    Feral exe), UntracedCasters HAVE (RayTracedBattleScene).
    - [x] DUPLICATE of "grass and banners into Rome Remastered's rays" in OPEN FOR ATN (11-16): ATN DECIDES: grass and banners into Rome Remastered's rays as Experimental rows? (the game gives them no shadow:
      0 cascade draws in 50 traces, so it would be a new look) Owner: ATN
  - [x] Rome II DONE: 12bc7a08 Rarer Objects in the Rays (tessellated terrain B1C7E29F + rigid E51A2D2A, Experimental),
    d99b3a10 Two Shadow Cascades Under Rays (Experimental, restart; patches Rome II's own shadow-quality switch +0x142fcdd,
    level B), b3d7e1b6 README; parity_check r2 exits 0 (0 / 8 / 70). Its Skip the Game's Shadow Map Work (edc6fb65) and
    tw_unit_shadows section 8 (dac1a645) DUPLICATED the CPU round's d52fb7ae: not taken (the CPU version stays); conflict
    resolved by hand. After the cherry-picks: rome2 / attila / thrones x86 + wh1 / wh2 x64 built; tw_rays_captures ALL
    PASSED (Rome II T + G). Lesson 25 + SKILL.md: owned pieces go into every ledger first. Waiting: the 3 Vfx particle rows
    and DrawScrub / Grass Trace NEED A TRACE; rays_preset_as_attila and Hue Clip 90 vs 80 are ATN's.
  - [x] WH3 MERGED 17efac2b (after 9eefea13 combined tw_rays_captures with Rome II's cases: ALL PASSED; 2b5ce800
    tw_psychov35 part 1 skips Rome II's r_ shaders: PASS x86 + amd64). Not rerun after the merge: other consumers,
    page dumps, parity_check (final verification). The agent's WARP + test exe at 19:40-19:41 overlapped a gap in the
    bench's quiet lock: hfp_ checks that start and now holds the lock for the whole batch. Was: WH3 DONE on its branch (f8a6353d Rarer Objects in the Rays: tessellated D5D2905E / 958EC025; 6f4c5065 Particle
    Edges by Depth; da965fc9 Faster Reflections, ATN-approved Experimental: 1.35-1.42 -> 1.06 ms in the 4K harness, only
    with Attila's narrower lobe; 5d570f20 unit draws' geometry in FrameTraceReads for unit round D): parity_check w exits 0
    (0 / 79 / 123). ShadowCascades WON'T (no cascade count in WH3), SkipShadowPass SHARED STEP CPU round 4, the GS line =
    editor wireframe (WON'T), fx_imposter 4B4AC54B NEEDS A TRACE, unit round E (TBB rows) SHARED STEP coordinator. NOT
    MERGED yet: its test/tw_rays_captures conflicts with Rome II's; the agent merges atn-mods, combines both, reruns the
    suite, and fixes tw_psychov35 (fails on atn-mods since Rome II's 5206dcbc put r_ shaders in part 1 with Attila's
    bindings). Then merge.
  - [x] Pharaoh MERGED 08396134 (campaign trees 16D14106 / 81696642 + vegetation E7792B60 + fur units 621E86DD held;
    Three Shadow Cascades Under Rays, Rarer Objects (ropes 655AB38D), the 3 Vfx particle rows; test/tw_pharaoh 1-5 PASS;
    parity_check p 0 open / 35 waiting; built after the merge). 1C4B8681 cuts at 0.505: helper's 0.5 stays.
    - [x] F81881EB (8-bone units, in no trace) not held: its asm_to_hlsl translation misses TEXCOORD0 by 2.6 on WARP, so
      motion_unit_weightedcommon_f818 (already in MOTION_SHADERS, level B) is probably wrong too. Owner: PARITY ROUND 3 DONE: bf8be7e4: F81881EB held + motion_unit_weightedcommon_f818 fixed (asm_to_hlsl --bits).
  - [x] Thrones / WH1 / Troy / WH2 MERGED aa3468a9 (conflict in Thrones' rows: both kept; Thrones / Attila x86 + WH1 / WH2
    / Troy x64 built): Two Shadow Cascades Under Rays in all four (WH1 / WH2 via new twwarhammer2/shadow_cascades.hpp, test
    tw_shadow_cascades; Troy's shadow_map.hpp rewritten), Rarer Objects in Thrones / Troy, ropes in Thrones / Troy / WH2,
    Troy's Smooth Sail Shadows; parity_check th w1 t w2 exit 0. Suites built their exes in the worktree's build (not
    testbin; nothing quarantined).
    - [x] Waiting lines the shared step settled but the ledgers still show (Troy C7FDC209 / 370A7440, Thrones' torch
      lines): re-mark. Owner: PARITY ROUND 3 DONE: ledgers re-marked: tools/tw/parity/troy.tsv C7FDC209 DONE 05b79c7b, 370A7440 DONE 767334b9; thrones.tsv torch line DONE ec540722.
  - [x] Shogun 2 MERGED ec83dc9c (GT-VBAO Temporal Smoothing / Edge Fix / Denoiser; Rarer objects in the rays 5074655A /
    C24D8705; SSS softness, debug view, softer away from the caster; all Experimental; tw_shogun2_shaders 1-19 PASS;
    both add-ons built after the merge). ShadowCascades / Sharpening / Extra Sharpness WON'T with proof.
    - [x] OPEN: ScreenSpaceShadowsColoured and ScreenSpaceShadowsBannerLight need the banners' own depth (Attila's
      InstallThin, inside twattila/screen_space_shadows.hpp): an opt-in there or a Shogun copy. Owner: PARITY ROUND 3 DONE: PARITY ROUND 3 8a21362e: Shogun 2 SSS Light Through Banners + Coloured Shadows.
  - [x] PARITY ROUND 3 MERGED (cherry-picks 8a21362e Shogun 2 SSS Light Through Banners + Coloured Shadows, Experimental;
    bf8be7e4 Pharaoh F81881EB held + motion_unit_weightedcommon_f818 FIXED: asm_to_hlsl's float4 registers made the bone
    bytes denormals that fxc flushed to 0, every vertex skinned with bone 0; new --bits flag in tools/tw/attila_motion/
    asm_to_hlsl.py). Merge conflict in test/tw_screen_space_shadows: the torch fix's case 9 and Shogun's case 9 -> Shogun's
    is case 10; suite PASS x86 + amd64 after the merge; Attila / Shogun 2 / Pharaoh built. parity_check over all 10
    games: 0 OPEN (waiting: Attila 2, Thrones 4, Rome II 8, Troy 6, WH3 80, WH1 8, WH2 5, Pharaoh 35, Rome Remastered
    46, Shogun 2 13: traces). FasterRenderLists: Rome II / Rome Remastered WON'T (byte search), WH3 / Pharaoh / Shogun 2
    SHARED STEP -> CPU ROUND 5. Attila's "Point lights with point shadows" -> GPU ROUND 2A (research).
    - [ ] tools: compare_test.cpp zeroes uint vertex inputs (can't catch bone-index bugs); dump_any.py doesn't retry when
      a worktree's shaders.h lacks generated headers. Owner: coordinator, tools pass after the batch
- [x] EVERY-PRESET GPU AUDIT DONE (tools/tw/preset_audit/; bench runs pre_presets_atn_220233 + pre_rerun_atn_225427, all
  used starts VALID, ATN's files 0 mismatches). TAKEN (cherry-pick of 10e6c41e): Bend Torch and Fire Shadows fix in
  sss_lights.cs (shared, 9 consumers): HLSL's && loaded both lights of every pair; bit-exact (tw_screen_space_shadows
  case 9); RTX 5090 4K light draws 18.86 -> 3.18 ms (8 lights); in game the row's GPU cost +8.12 -> +2.29 ms.
  NOT TAKEN: e606e8ef (RT/PT preset RayTracedLightCount 128): not scored better (no reference, not visible in the view),
  cost in heavy scenes unmeasured, and through SetRaysPresetDefaults it changes the DEFAULT in 8 games (WH3 / Pharaoh
  0 -> 2, Reset leaves 2). ATN DECIDES; it stays on its branch.
  Per-preset (stopped view, all CPU-bound ~28-29 ms CPU busy): ATN 31.97 ms frame (game GPU 17.9 + helper 10.4), Reset
  29.50, no RT/PT 29.35, RT shadows only 28.32, RT/PT 31.85, path methods from Reset 33.82. Row A/Bs: Particle Resolution
  Half -0.14 ms game GPU; Faster Smoke Build -0.20 helper; Lean -0.55 helper; Match Output +2.18 helper (sun worse 3.70 ->
  4.43 % rRMSE, bounce better 2.19 -> 1.59 %): stays out of presets. Owner of the findings: NEXT GPU ROUND:
  - [x] Units G-buffer 9.6 ms under every ray preset vs 5.8 without (+3.8 ms of game GPU: the capture at unit draws).
    g2a_: it is rt_core's per-draw stream-output capture (~70 ps a game index vs 95 for the game's draw); a BATCHED form
    (mesh once + a row point per instance through SO, one compute after) is bit-equal (102000 / 102000 floats) and cuts
    ~2/3 (testbin/g2a_unit_capture: 2.92 -> 2.16 ms with the game draws). Owner: GPU ROUND 3 (Experimental row, one unit
    family first; tw_rays_captures case; level B needs a game run) DONE: GPU ROUND 3A dd44b703 Batched Unit Capture (14.66 M corners 3.203 -> 2.627 ms, bit-exact); the rest = the "Batched capture for 4_bones / dismember" line.
  - [x] The helper's scene build 3.96 ms a frame: g2b_ 883336aa (4 cluster BLASes in one NVAPI call, byte-identical:
    sorted field 2.64 -> 2.415 ms). Units 2.10 = instantiate 1.60 + BLAS 0.43 + CLAS 0.06; 256-triangle clusters DROPPED
    (rays through grass slower). Rest -> the GPU ROUND 3 lines below
  - [x] DUPLICATE of "set the path-traced method sliders' Reset defaults" in OPEN FOR ATN (11-16): Path-traced method sliders turned on from Reset default much heavier than RT/PT (4 bounce rays, 2 bounces, range
    10, resolution 1: paths 6.50 vs 2.82 ms): align their defaults with RT/PT's? ATN DECIDES (look) Owner: ATN
  - [x] Sprites spike 1.8-2.0 ms: EXPLAINED (g2a_): the GPU waiting for the CPU on cascade-refresh frames, landing on
    command 1633 (imposters' depth draw); nothing to cut on the GPU
  - [x] Shadow maps under rays: EXPLAINED (g2a_): the sun-atlas draws are bypassed (0.0000 ms); the spikes are CPU waits
    (~850 more static-caster + 150 soldier shadow commands, 3.9-4.6 ms waits on those frames): the CPU side = the Skip
    rows, measured by bench variant g2a_shadow_skip.json
  - [x] Bend torches' remaining +2.3 ms: g2a_ 7fd71432 (the pick in turns, bit-exact; 4K light draws 3.198 -> 1.050 ms at
    8 lights, 3.847 -> 1.154 at 32; NaN score now left out); expected in game ~+0.2 ms (variant g2a_torch.json)
  - [x] SSSR pyramid: 0.127 ms in all, a single-pass downsample saves <= 0.05 ms: NOT BUILT (the rest is march + resolve)
  - [x] GT-VBAO timed (g3a_ao_time, 4K 5090, Recommended 3/4 res): ao_pass 1.61 ms = bitmask 1.462 (90 %) + depth
    0.041 + resolve 0.038 + denoise 0.027 + upsample 0.046; nothing cuttable bit-exact outside the bitmask
    - [x] DONE 11-15 (GPU ROUND 4: ALU / issue-bound, exact prefetch slower, not taken): Nsight shader profile of the bitmask pass in that harness (fetch vs ALU) for an exact rewrite. Owner: GPU ROUND 4
  - [x] ATN 23:5x YES: 128 torch lights in RT/PT taken (603ca343 = e606e8ef; the default also becomes 128 in the 8 games
    calling SetRaysPresetDefaults; all TW add-ons built)
  - (moved under "ONE Constantinople bench": 128 torch lights' cost at night / burning city / WH3 / Phara...)
  - (moved under "ONE Constantinople bench": Rarer Objects needs a view with siege towers or ships (bench...)
  Was: EVERY-PRESET GPU AUDIT (pre_, started ~22:15, ATN "go for it"): Attila's inventory of presets + Experimental rows
  (RT shadows, SSS, AO, reflections, particles, fog, bounce); one measurement session at the bench view (Reset, non-RT,
  RT shadows, RT/PT, Path Tracing, RT/PT + chosen quality rows: Time Each Pass, PresentMon, screenshot each); per-preset
  costs; non-ray wins implemented; quality rows that cost little and score better into RT/PT (ATN delegated), the rest
  Experimental with numbers; ray findings handed to the GPU round
- [x] CPU ROUND 4 MERGED c2cd9e90 (clean; NOT yet built on the merged tree: the preset audit holds the quiet lock):
  Faster Game CPU Code sites 5 (radix sort for the render list sort's plain 4-byte values +0x17b9217, Thrones +0x182cfa7:
  545 -> 45 us at 16384 values; 8 MB address space) and 6 (instance gather prefetch 0x182c990: cold 0.661 -> 0.340 ms),
  both proven identical (tw_engine_code parts 9-10), on by default inside the existing row; not run in game. The agent's
  heapfix test exes ran inside the preset audit's quiet window: its starts 1_atn and 3_norays were INVALID (rerun asked).
  - [x] merged into "Final verification" (was: "Build both lines + tw_engine_code on the merged tree when th...")
  - (moved under "ONE Constantinople bench": Bench variants ready: tools/tw/bench/variants/cpu4_round4.js...)
  - [ ] Author packages: zip READY 12:1x in ATN's Downloads (AttilaCPUPerfFix_7.0_notes_2026-11-15.zip: README + b's
    NOTE / patch, a's NOTE / patch, c's NOTE; no copies of his code) + a short Nexus message; ATN sends. (was: tools/tw/testbin/cpu4_heapfix/: a_memfast_65_128, b_loopfetch_animation, c_x87_truncation
    question; plus for_author_pf): bench numbers into the NOTEs, then ATN sends Owner (audit 11-15): coordinator (bench numbers into the NOTEs after the end bench), then ATN sends.
    Bench numbers 06:2x (END BENCH PART 1 line): b gains 0.15-0.62 ms in every round; c no gain; a worse in max CPU.
    NOTEs DONE 06:4x ("## In the replay" in a / b / c NOTE.md, each start's heapfix.log line checked: x87 1276 of 1277,
    loop prefetch 7 of 7, memcpy 128 bytes): b = proposal with its gain; a = "we don't propose it any more"; c = "keep
    x87fix as it is". ATN SENDS b (a / c optional, as information) and the earlier for_author_pf. Owner: ATN
    (merged from "ATN, when sending the heapfix package: ask the author whethe...": ATN, when sending the heapfix
    package: ask the author whether we may patch the x87 block over heapfix's bytes (+0x17f6515) and about his disabled
    prefetch site in the animation pack loop)
  - [x] Render-list headers 64 B apart: WON'T (cpu5b_, report tools/tw/round_notes/cpu5b_report.md): the header vector
    is a generic std::vector<std::vector<Item>> whose grow / copy / destroy (0x175de30 / 0x18fc430 / 0x173c260) and x12
    indexing are shared with unrelated vectors: not separable by a byte patch. cpu5b_ also stopped once on a safety
    classifier cut-off while writing the x64 analysis (not retried). Was: CPU ROUND 5 STOPPED ~00:10: a
    safety classifier cut off one of its responses during the static analysis, so the agent stopped and nothing was
    committed (not retried: ATN decides). Facts so far: the old reader list was WRONG (0x176d430 / 0x176da30 / 0x176dbf0 /
    0x176ddb0 are push_backs of other vectors); list layout 32 B: +0 thread-index provider, +4 merged vector, +0x10 header
    vector {cap, count, data} of 12-byte entries, +0x1c "has items"; header readers so far: push 0x176ced0, merge 0x178fd90,
    release 0x1810d30, clear 0x1731160; allocation candidates 0x17bd700 / 0x16c8ec0 unconfirmed; the whole-DLL reader census
    unfinished. ATN 00:1x YES: a fresh agent (cpu5b_) continues from these facts, items 1-3
  - [x] DONE 11-15 (WPR 11-15 (tools/tw/x64_render_lists, hot.txt): Pharaoh push 0x3103eb0 0 of 435,018 module samples, on none of 215,075 stacks; Shogun 2 push 0x1a7adb0 (empire.retail.x64.dll) 0 of 34,880 and none of 76,104 stacks: WON'T GET for both (not hot); WH3 not measured: its own box): Faster Render Lists for WH3 / Pharaoh / Shogun 2 (x64): equivalents FOUND (cpu5b_): WH3 push 0x19042d8 flag
    0x1904320 [r14+0x28]; Pharaoh 0x3103eb0 flag 0x3103ef8; Shogun 2 0x1a7adb0 flag 0x1a7afe9 [r15+0x30]; covered bytes
    movable, flags dead at the store. Two gaps: (a) which push is hot (no CPU profile: one WPR data run per game, ATN's
    OK), (b) engine_code.hpp has no x64 path (reuse rysesonofrome_dlss/engine_memory.hpp AllocateNearExe; x64 stub,
    safe 5-byte write, 3 Experimental rows, 64-bit tw_engine_code part). Owner: ATN DECIDES (data runs) Owner (audit 11-15): coordinator, after the Attila end bench (ATN 02:2x OK for the x64 render-list WPR data runs): one WPR data run each WH3 / Pharaoh / Shogun 2, then the x64 path in engine_code.hpp; also owns the x64 engine_memory shared step.
  - (moved under "ONE Constantinople bench": Attila's Faster Render Lists row (Experimental, site 3) was...)
  - DROPPED with proof: the 8x pose hold (0x49aad0 is simulation, not pose); next only with a data run (the bone-matrix
    writer behind 0x17dab80), ATN DECIDES
- [x] ATN 21:2x: Attila's number_of_threads set to 16 (ATN's preferences.script.txt 559f4696 -> 432ba846; fresh backup
  tools/tw/attila_prefs_backup_20261114_211544); CPU Set rule 0-15 stays; the parallel sort is dropped
- [x] ROUNDS MERGED ~00:40 (7e668fe7 grass phase 0, 610d6654 GPU 2B, 9afd0c8d GPU 2A; CPU 5b research only). Attila
  build32 built on the merged tree; tw_screen_space_shadows rerun on it (coordinator). Open items with owners:
  - (moved under "ONE Constantinople bench": GRASS: one Grass Dump from a real battle = bench probe varia...)
  - [x] DUPLICATE of "GPU grass placement (phases 1-3): go ahead?" in OPEN FOR ATN (11-16): GRASS after the probe: part-draw cull (vtable +0x10c), burn writers (the probe: only burn changes, 256-269 records in 35 s, grid static: a GPU pass needs the records' burn each update or the writer hooked), frustum entry +0x8c if
    needed, AMD / Intel self-test (phase 1). Owner: next grass agent; phases 1-3: ATN DECIDES after the probe
  - [x] test/tw_scrub_lods Bias() uses 0x37810101, the game's is 0x37810183 (one line, no shipped effect). Owner: GPU
    ROUND 3 DONE: 15111de1 (in dd44b703): Bias() = 0x37810183.
  - [ ] Thrones Grass Dump button: only when its grass port starts. Owner: that round
  - (moved under "ONE Constantinople bench": g2b_ bench variants g2b_clusters.json (g2b-base / g2b-blas /...)
  - (moved under "ONE Constantinople bench": g2a_ bench variants g2a_torch.json + g2a_shadow_skip.json (c...)
  - [x] GPU ROUND 3B MERGED (g3b_, a833578d + 0c4335ff; final helper build\tw_dlaa_helper_g3b2 3C9DB0234740, not
    installed): (e) surface record: byte-identical in 32 modes (NRD modes nondeterministic in the base too), 4K bounce
    1.39 -> 1.02 ms, reuse 0.74 -> 0.34, Nsight reuse 2.52 -> 1.49 ms, frame 8.00 -> 7.16; +265 MB VRAM at 4K while reuse
    history exists. RT_PATH_RATIO_FOLLOW (bridge flag 1u<<25, off; bridge stays v31): U2 Half + RR moving 11.48 -> 6.99 %
    (settles in 10 frames, not 32), Half no RR 11.92 -> 8.44, Full no RR WORSE 5.12 -> 5.68. NOT TAKEN: merging opaque
    unit draws (units -0.23 ms but field rays +3 ms), f16 reservoirs (reuse already 0.34 ms). all_modes 179/179,
    fg_bridge_test 0 failed, helper suites PASS x86 + amd64.
    - [x] merged into "Final verification" (was: "Rebuild every add-on using dlaa_bridge.h (x86 + amd64) on th...")
    - [x] DUPLICATE of "keep or remove the Lean ReSTIR Reuse row?" in OPEN FOR ATN (11-16): Lean Reuse row: now saves only ~0.06 ms (RT_PROFILE) / 0.39 ms reuse (Nsight base clocks), its look scored as
      no change: keep or remove? Bench variant g3b_reuse.json gives the in-game number. In game (END BENCH PART 2): Lean -0.09 (max) / -0.16 ms (atn) of the bounce on top of the record. Owner: ATN
      (merged from "(f) Lean = 3 neighbours + radius 6: 0.96 -> 0.85 ms, rRMSE 2...": (f) Lean = 3 neighbours + radius
      6: 0.96 -> 0.85 ms, rRMSE 2.18 -> 1.95 % (better): after ATN's Lean verdict (A3). Owner: ATN, then the ReSTIR
      follow-up)
    - [x] DONE 11-15 (merged in GPU ROUND 4 (d22bc4a9 "Torch Shadows Follow the Camera", Experimental off); ATN places it): RT_PATH_RATIO_FOLLOW as an Experimental add-on row (Half + RR the clear win; maybe only with RR or Half and
      below, since Full without RR scores worse). Owner: next add-on round; ATN places the row Owner (audit 11-15): in progress, NOT merged: d22bc4a9 "Torch Shadows Follow the Camera row (Experimental, off)" on worktree-agent-a3aa080e775bbd8bf: that agent merges + builds, then ATN places the row.
    - [ ] U2 step 3 (own light reservoir); record at bounce size at Half (VRAM); f16 relative hits need a two-set record.
      Owner: next ReSTIR round
    - [ ] Field-scene ray slowdown after merging unit clusters unexplained; fewer unit triangles or the trunc8 bench next. trunc8 bench (END BENCH PART 2): cluster build 1.95 -> 1.58 ms, scene 3.68 -> 3.32 (look change, ATN DECIDES); the slowdown itself still unexplained.
      Owner: coordinator
    - [ ] A32: under GPU Trace without REPLAYABLE the helper commits ~838 MB/s for ~15 s (private 13 GB peak, system
      Committed 30 -> 48.5 GB): a test switch that serialises requests but keeps the shared fence, to split the two
      halves. Watch the 55 GB abort rule in any such run. Owner: Nsight round
      (from the J3 / A32 line: one Nsight run without REPLAYABLE (coordinator's OK, offline harness).)
    - [ ] ns_trace_run.py must end its "rescue" helper (one blocked the next job 7+ minutes). Owner: Nsight tooling
    - (moved under "ONE Constantinople bench": Bench variants g3b_reuse.json + g3b_round3b_plan.txt in the...)
  - [x] f16 reservoirs (look change, scored) only if (e) leaves the reuse pass above ~0.4 ms. Owner: after (e) OBSOLETE: (e) left the reuse pass at 0.34 ms, under the 0.4 ms bar (f16 reservoirs NOT TAKEN); see the GPU ROUND 3B MERGED line.
  - [x] GPU ROUND 3A MERGED dd44b703 (g3a_): Batched Unit Capture row (RayTracedBatchedUnits, Experimental off, Attila
    only: needs the pack hook; families 5F9CD405 / A691CFF4): bit-exact (WARP case B + 58,650,000 floats on the 5090),
    14.66 M corners 3.203 -> 2.627 ms (flush 0.44, copy ~0.18); tw_scrub_lods bias 0x37810183. Page dumps of every rt_core
    consumer: only the new (hidden) row. test_hooks include change NOT committed (ue4_'s macro took its place).
    - [x] DONE 11-15 (GPU ROUND 4 (Batched Unit Capture for every family, bit-exact)): Batched capture for 4_bones / dismember families (own mesh + rows shaders); bind `captured` as a UAV to drop the
      0.18 ms copy. Owner: GPU ROUND 4
    - (moved under "ONE Constantinople bench": Bench variant g3a_capture.json (+ g3a_round3a_plan.txt; add-...)
  - [ ] Resource upgrade 0.196 ms = ALL gtl resource-info lookups (OnPushDescriptors 0.031 exclusive): split by caller
    from the cpl2 pickles, then push_descriptors in the drw_ replay; a cache needs its own uuid'd shared block + epoch in
    every writer (swapchain_v1:900 writes directly). Owner: CPU round (shared util, full build)
  - [x] Skipped no-handler casters: under today's fast dispatch only 0.042 / 0.029 ms (cpl2 / cpl1): no exact early
    skip exists below that; NO CHANGE (the game issuing the casters is the real cost: Skip rows)
  - [x] DONE 11-15 (GPU ROUND 4 (f407205a "Rays Replace Point Light Shadows", Experimental off)): Attila point lights with point shadows: the bytecode differs from the brief: 16 "...falloffshadow" builds read
    the shadow map at t4 (2D array); gather_4_1 = plain gather4 at s4 + ALU compare, gather_5_0 = gather4_c at s4 +
    blocker gather4 at s5. Plan (g3a report): patch all 32 like the existing 8 (visibility multiply behind a shader
    flag), register in InstallPointLights, row on binds a 1x1 lit array at t4 + ALWAYS comparison sampler at s4, WARP
    proof (off = game bytes; on + visibility 1 = no shadow; control). Owner: GPU ROUND 4
  - [ ] WH3 grass read-back without the wait: 8 of 78 traces have indirect grass (14-25 draws, max 13.94 M index x
    instance a frame, sieges 6.5 M); plan (b): room from last frame's probe x 9/8 NaN-filled, GPU copy of the instance
    count into the capture draw's own arguments, DrawInstancedIndirect into SO, overflow drops (never stalls). Needs a
    trace with the 5 argument words + ByteWidth. Owner: WH3 data run (coordinator), then GPU ROUND 4+
    (Same work as the 11-13 Grass round's unboxed "NOT DONE: d1 without the lag" note and (d) of the NEXT GPU ROUND
    list.)
  - [ ] J3 in game (one grass pixel's G-buffer read): next data run. A32: one Nsight run without REPLAYABLE (coordinator's
    OK, offline harness). Owner: coordinator
    (Its A32 part merged into the A32 line above.)
  - [ ] Scorer: reflections / fog / water features, a faint-smoke reference; the smoke feature (tools/tw/quality/g2b)
    into the shared scorer. Owner: next scorer round
  - [ ] DLSS RR 310.9 made paths boil (paths 2.19 -> 5.01 % rRMSE, noise 0.54 -> 4.13): check its guide inputs before any
    RR DLL update; 310.9.16 ships now (tools/tw/ngx_dlls.py wants 310.9.16 or newer; "310.7 stays" was out of date, 11-16). Owner: next RR round / ATN
  - [x] J2 (Validate drops a stored light) NOT TAKEN: paths through RR worse 2.19 -> 2.56
  Was: ROUNDS STARTED ~23:40 (base bf8be7e4): GPU 2A g2a_ (the add-on's D3D11 side: units capture +3.8 ms, shadow-map
  redraw under rays, sprite spike, Bend torches' +2.3 ms, GT-VBAO / VBGI / SSSR, WH3 grass read-back, Attila point-light
  shadows research); GPU 2B g2b_ (helper scene build 3.96 ms, per-draw leftovers, J2 / J3 / A32, scorer scenes + scored
  look verdicts; Lean follow-ups as plans only); GPU GRASS phase 0 grass_ (Grass Dump button, grass_replay, design);
  CPU 5 cpu5_ (render-list headers 64 B apart, Faster Render Lists for WH3 / Pharaoh / Shogun 2). Each writes bench
  variants for the end-of-batch bench.
- [x] ATN 00:2x (2026-11-15): plan Expeditions: Rome, Tales of Arise, Ryse (medium effort, offline, concurrent): DONE: plans tools/nontw/{tales,ryse,expeditions}/plan.md + tsv, skill 2759a2ce; its open children stay.
  - [x] SKILL MERGED 2759a2ce: .agents/skills/game-port-method (SKILL.md + references engines.md, hdr-only-snapshot.md,
    addon-split.md); tw-port-checklist keeps TW facts and points to it; release.md step 9 = the snapshot check; page
    dump (tools/tw/page_dump) now writes "== helper absent" and "== first start without hdr". Facts: command_action is
    ONE process-wide registry (a second add-on adds its own directly registered events + present work, no frame of
    latency); fast dispatch runs only while every module opted in (Shogun 2's two do; Ryse / Tales / Expeditions don't:
    opting in = the cheap first step). Upstream has external/DLSS + Streamline (merged add-ons can build there).
  - [x] SNAPSHOT ROUND MERGED (snp_): SDR preset + first start for Troy / WH2 / Rome Remastered / Shogun 2 (shared
    output_encoding, no copy), Shogun 2 download note + GitHub (note text in twattila/credits.hpp), Troy / WH2 GitHub,
    Rome Remastered's note on General too, WH3 Upscaling Mode gated on HelperInstalled() from the first frame; Shogun 2's
    dump body has both sections; all 10 games PASS (tools/tw/round_notes/snapshot_check_1115.md); every TW add-on
    built; tw_device_loss PASS (x86 needed a second run: the focus-dependent "limit, front" case).
    - [x] DUPLICATE of "in game, do SDR first start / SDR preset / SDR output look right" in OPEN FOR ATN (11-16): In game: SDR first start, SDR preset, SDR output for Troy, WH2, Rome Remastered, Shogun 2. Owner: ATN (look)
    - [ ] WARP ramp for the four new SDR presets. Owner: next TW round
    - [ ] README New: lines for Troy, WH2, Rome Remastered, Shogun 2, WH3. Owner: release round
    Was: SNAPSHOT GAPS (TW): no SDR first-start path in Troy, WH2, Rome Remastered, Shogun 2; no full-download notice
    (red + GitHub / Nexus buttons) in Shogun 2's HDR add-on; Shogun 2's page-dump body lacks the two new sections; every
    TW game's helper-absent dump + preset dry runs not yet checked against the snapshot list. Owner: SNAPSHOT agent snp_
  - [x] Attila's "first start writes SwapChainEncoding Auto -> SDR": a PAGE-DUMP ARTIFACT, not code (the dump's
    ResetSettings(false) kept the SDR button's global value). Fixed in tools/tw/page_dump (resets between steps are
    global now); Attila's re-dump page_a_coord_reset.txt: first start writes only tonemapper / auto exposure / V31 hue
    rows, no Output row
  - [x] DONE 11-15 (rym_ / uem_ merges: no rays / DLSS / FG rows without the helper, first start without HDR in the page dumps (ry / ta / exr)): SNAPSHOT GAPS (non-TW): Ryse / Tales / Expeditions: no SDR first-start path, no full-download notice, no page-dump
    driver. Owner: their plans Owner (audit 11-15): RYSE MERGE (rym_) / UE4 MERGE (uem_). Partly DONE: Ryse d0b97911 (note, GitHub, SDR preset, first start), Tales / Expeditions f181c71d + d50bfc2c (note, Auto / SDR / first start); left: the page-dump drivers (S6).
  - [x] TALES PLAN DONE: tools/nontw/tales/plan.md + tales.tsv (both add-ons build at HEAD). Top gaps: the install is
    DevKit + symlinks into build\Release with only nvngx_dlss 310.9.1 (no dlssd / dlssg / sl.* / D3D12: RR, FG, DXR 1.2
    can't run there); no fast-dispatch opt-in; no SDR preset / first start / download notice; no page-dump driver; no
    scripted-run hooks (SET / SHOT / PROFILE); own in-process D3D12 device with no device-removed handling (not on the
    helper / rt_core like Expeditions and Ryse); the game's 25 menu settings never audited; untested since 2026-10-27.
    Shaders: ATN's dumps in bin\cmd_Decompiler-1.3.16\dump_shaders_tales_of_arise (13,525 CSOs) and
    dump_shaders_Expeditions_rome (16,983); paks untouched; code offsets only from tales_image.bin (SteamStub). Nsight:
    no GPU Trace (the game is D3D11); rt_tests exe yes. Game runs: G1 data run (outside-city save), G2 data run (camp
    save + 30 s WPR), G3 feature run, G4 ATN's look list. UE4 SHARED STEP candidates S1-S11 in the plan.
    - [x] ATN 01:5x: saves = Manual 03 (Sandinus Ravine, outside, 10/12/2026 15:30) and Manual 04 (Mosgul camp, 10/17/2026
      00:15); probably SaveData_004 / _005 by their times (the run confirms in the load menu); scripts may load them
    - [x] ATN: full package = plain stamped copies (DevKit + symlinks out, NVIDIA set + D3D12 / Streamline in)
    - [x] RT route: the shared helper + rt_core (coordinator's suggestion, ATN asked: the TW stability fixes come along);
      Tales' in-process D3D12 RT goes
    - [x] AO halo default question gone: ONE add-on (the row lives in it)
    - [x] ATN: ONE add-on (DLSS + presets on Graphics 1 of 2, higher graphics on 2 of 2, like TW)
    - [x] ATN: hue by eye, no measuring; G1 + G2 in one session fine; no Steam setting changes, saves backed up
    - [x] addon-split.md fixed: Tales DLSS registers draw / dispatch only while a trace runs
  - [x] RYSE PLAN DONE: tools/nontw/ryse/plan.md + ryse.tsv (~75 rows; both add-ons build at HEAD; the tree is HDR fix53 +
    DLSS v115, never run in game). Top gaps: shipped helper22 / bridge v11 vs HEAD bridge v31 / helper 113 (+ Agility);
    no page-dump driver; no fast-dispatch opt-in (and the DevKit symlink in Bin64 keeps it off); no download notice /
    SDR preset / first start; no RR (D3D11 RR bug: helper as upscaler or NGX D3D12); no FSR / XeSS (+ FG); no scripted
    runs / pass timer / bench scene; Attila RT rows missing (Debug View, RT/PT preset, NRD, rays a pixel, SHaRC, SER,
    Lean, specular occlusion, Log Details, profile); the game's other settings never traced; Hue Clip 80 from the
    generic curve; device_loss not wired. Install: symlinks + DevKit, nvngx_dlss 310.9.1 only. Shaders: dump_shader_ryse
    (540 + HLSL), ryse2 (354), DevKit dump (894), ryse_image.bin; paks' directories encrypted (not read). Nsight: no GPU
    Trace (D3D11); a D3D11 frame capture untried. Runs: R0 data run, R1 split bench (one add-on or two), R2 features +
    ATN's look list. Shared x64 step proposed: engine_memory.hpp near-exe memory + atomic code patch + FindPattern (third
    copy in the repo) for the TW x64 CPU detours.
    - [x] ATN 01:5x: the auto-continue save (chapter 1, tutorial pauses; chapter 3 has none); scripts may load it
    - [x] ATN: full package as Attila: RR / FSR / XeSS / FG through the helper
    - [x] ATN: read the shipped shader packs / caches where possible (unencrypted data + the game's own caches; no
      decryption): SHADER-PACK round
    - [x] ATN: Nsight on all three (D3D11 frame debugger / profiler on the game; GPU Trace on the helper)
    - [x] ATN: runs allowed (few, scripted, fresh backups); R1 split bench DROPPED (one add-on decided); menu settings as
      the TW rule (fresh backup)
    - [x] ATN: ONE add-on; hue by eye; stamps + watermarks for every game
    - [x] addon-split.md fixed: Ryse's HDR add-on registers draw / draw_indexed always, its DLSS add-on only while tracing
  - [x] EXPEDITIONS PLAN DONE: tools/nontw/expeditions/plan.md + expeditions.tsv (both add-ons build at HEAD). The engine
    is UE 4.23 (build string ++UE4+Release-4.23-CL-0), not 4.26: skill engines.md + memory fixed, add-on comments in E1.
    D3D11 (a -d3d12 path exists), no SteamStub / Denuvo. Gaps: r.SSGI.Enable / HalfRes not in the exe (4.23 has
    r.SSGI.Quality); no fast-dispatch opt-in (HDR add-on needs draw_dispatch::Use for resource_upgrade); no SDR output /
    Auto / SDR preset / first start / download notice / buttons; no page-dump driver; shipped helper19-22; 14 of 20 TAA
    permutations unmapped + other quality levels (every setting); the game's own HDR toggle and -d3d12 unhandled; no RR
    (helper NGX D3D12 route) and Attila's RT rows; Hue Clip 70 from the generic curve. Install: symlinks, no helper.
    BUG: two crash folders 2026-10-27 01:57 / 01:59: the DLSS add-on's unload (AddonUninit, a wstring destructor).
    Shaders: dump_shaders_Expeditions_rome\all_shaders_ (15,735 CSOs, r.CreateShadersOnLoad=1). Nsight GPU Trace looks
    possible (64-bit, no DRM wrapper, launch through Steam -applaunch 987840). Runs: G1 data run (menu + battle save,
    traces, PresentMon split numbers), G2 only if permutations stay unmapped, G3 ATN's look list. Steps E1-E11 offline.
    - [x] ATN 01:5x: "Save Silanus 3m" (start of the game, on the ship; no battle yet); scripts may load it
    - [x] ATN: hue by eye; ONE add-on
    - [x] ATN: full package as Attila (FSR / XeSS / FSR FG through the helper; Bend SSS rows as in Attila)
    - [x] ATN: plain stamped copies; Nsight through Steam OK
    - [x] ATN 02:2x: the game's own HDR toggle stays OFF (the add-on says so on the page). Owner of the row: UE4 merge
  - [x] DONE 11-15 (Shogun 2 d4c82679, Ryse 8374d2d0, Expeditions + Tales 727a6f2c): ATN 01:5x: ONE ADD-ON for Tales, Expeditions, Ryse AND Shogun 2 (DLSS + RT/PT / no-RT presets on Graphics 1 of 2,
    the game's higher graphics on Graphics 2 of 2, TW page structure; the HDR-only snapshot still works: graphics rows
    in the add-on, DLSS with NVIDIA's DLL, rays / FG / RR / FSR / XeSS with the helper). Owners: Shogun 2 = MERGE agent
    s2m_ (TW batch, before the Fable review); Tales + Expeditions = ue4_ after its offline steps, then a UE4 MERGE round;
    Ryse = rys_ after its offline steps, then a RYSE MERGE round Owner (audit 11-15): coordinator: check s2m_'s branch (no merge commit on atn-mods yet), merge, then the rym_ / uem_ merges.
  - [x] RYSE OFFLINE ROUND MERGED d0b97911 (rys_; both add-ons build on the merged tree): fast dispatch opt-in (HDR
    draw_dispatch::Use, DLSS fast::Use; tw_draw_dispatch amd64 PASS), constant mirror once per process (moot after the
    merge), HDR-only pieces (download note on General + Ray Tracing, GitHub, SDR preset, first start; RT installs only
    with the helper), device_loss + output_encoding as Attila (each new swap chain decides the output, HDR -> SDR only
    when Windows HDR is known off everywhere, stall log after 20 s: TW behaviour, accepted by the coordinator as the
    "same as TW" decision, ATN may object), reorder warning fixed, README fix54 DRAFT (release must carry helper from
    the same commit, Agility, NVIDIA set, Streamline 2.14.16). Not run in game.
    - [x] SHARED STEP: twattila/test_hooks.hpp includes rt_scene.hpp (Attila's embedded shaders) but needs only
      rt_core.hpp. Owner: g3a_ (sent ~02:0x), then Ryse / UE4 test hooks in the merge rounds DONE: twattila/test_hooks.hpp's #ifndef RENODX_TEST_HOOKS_PROFILE guards the rt_scene include (ue4_ 6e8f661a, d50bfc2c); Ryse's hooks go with the RYSE MERGE round.
    - [x] merged into "ATN 03:4x: Ryse / Tales / Expeditions are IN THIS BATCH" (was: "RYSE MERGE ROUND (one add-on): rys_'s plan in its report: mo...")
    - [x] DONE 11-15 (ATN ended it 12:1x, the coordinator deleted both exes): A page_dump_ryd_rys1.exe (PID 35256) is stuck in tools/tw/page_dump (built outside testbin; "Access denied",
      likely Bitdefender). Owner: ATN ends it and deletes page_dump_ry_rys1.exe / page_dump_ryd_rys1.exe there
    - [x] DONE 12:11 (install, .aside-20261115_121136): Remove the DevKit symlink from Ryse's Bin64 before runs (it keeps fast dispatch off). Owner: coordinator at
      the install (fresh backup)
  - [x] UE4 OFFLINE ROUND MERGED d50bfc2c (ue4_): Expeditions 4.23 labels + r.SSGI.Quality (exe default 0, clamp
    1..4); fast dispatch opt-in in all four (tw_draw_dispatch PASS); one engine_cvars (UE_4_21 / UE_4_23 tables),
    Expeditions' 7 duplicate headers removed; HDR-only pieces (talesofarise/hdr_only.hpp: note + Ko-fi / Nexus / GitHub)
    + output_encoding (Auto / SDR / first start; Expeditions gained SDR output EXR_SDR_OUTPUT, default still scRGB); test
    hooks via twattila/test_hooks.hpp's RENODX_TEST_HOOKS_PROFILE + "trace" key. Not run in game.
    - [x] DONE 11-15 (uem_ unload harness PASS 0 problems (Tales, Expeditions; control 160); in game not run): Expeditions' unload crash (2026-10-27): a ReShade unload / reload 1 s after start (UE's probe device), not exit;
      likely a stale HDR-module record in cross_addon after the HDR add-on was freed. The ONE-ADD-ON merge removes the
      cross-add-on link; verify with tools\nontw\ue4_unload's harness on the merged add-on (+ device events). Owner:
      UE4 merge round
    - [x] DONE 11-15 (f181c71d / d50bfc2c, kept in the UE4 merge): Expeditions' default Output scRGB -> Auto (as TW, so SDR first start works for everyone). Owner: UE4 merge
      round (the "same as TW" decision)
    - [x] Expeditions NexusMods URL from ATN 02:3x: https://www.nexusmods.com/expeditionsrome/mods/2 (About button)
  - [ ] ATN 02:3x (for later): per-game GitHub download links instead of the shared releases page. Checked: GitHub's
    filtered list works (releases?q=attila&expanded=true showed only Attila's 8 releases). Plan: each add-on's GitHub
    button + FULL_DOWNLOAD_NOTE text use github.com/arshiatn/renodx/releases?q=<its tag prefix>&expanded=true (one
    constant per game; the release titles / tags must keep the game's name). Owner: release round
    - [x] output_encoding's log lines say "[Total War]" in non-TW games: a log-name parameter. Owner: sdr_ (sent) DONE: MERGED 24cdfb9c (sdr_): output_encoding log_name, log names [Ryse] / [Tales] / [Rome].
    - [ ] WARP ramp of the UE SDR encodes; Ryse's redundant renodx_trace_now poll; the Expeditions tests' include paths
      (no build target). Owner: UE4 / Ryse merge rounds
    - [x] merged into "ATN 03:4x: Ryse / Tales / Expeditions are IN THIS BATCH" (was: "UE4 MERGE ROUND (one add-on each, Expeditions first): ue4_'s...")
  - [x] ATN 02:2x: the x64 render-list data runs (WH3, Pharaoh, Shogun 2: one short WPR run each, our own Faster Render
    Lists, not the author's code) are OK. Owner: coordinator, after the Attila end bench
  - [x] DONE 11-16 (8374d2d0 + 727a6f2c merged, runs done, installed): ATN 03:4x: Ryse / Tales / Expeditions are IN THIS BATCH (do them all, several agents). Started ~03:50:
    RYSE MERGE rym_ (one add-on, pages, helper route for DLSS / RR / FSR / XeSS / FG, RT rows, the 5,527 new shaders'
    tonemap / post families) and UE4 MERGE uem_ (Expeditions then Tales: one add-on, pages, Auto default, helper route,
    Tales' in-process D3D12 RT out, unload harness); the root CMake NGX block loses each game when its helper route lands
    (an upstream snapshot can't link NGX). Then data runs (Tales G1 + G2, Expeditions G1, Ryse R0 with the shader-name
    cvars), fixes, stamp, install. Owner: coordinator
    - [x] RYSE MERGED 8374d2d0 (rym_, 5 commits, built 06:17: 4,492,288 B, 0 errors): one add-on (graphics.hpp from the
      old _dlss addon.cpp), Settings Mode pages + SettingsAdvanced (migrated once), helper route (ngx_backend.hpp and
      Streamline FG gone; TAA hook -> shared bridge; twattila frame_generation / reflex), 7 new [Ryse AA] keys, 11 RT keys
      at Attila's parity (Experimental off), rt_specular_merge into t6; HDR-only state hides AA / FG / RT rows; key compare
      132 -> 151 keyed rows, every old key kept. Skills + build_shared.ps1 / drw_verify.py lose rysesonofrome_dlss.
      - [x] R0 data run DONE 16:51-17:29 (tools/nontw/ryse/r0, 10 starts through steam.exe -applaunch 302510; Ryse.exe
        started alone exits: DRM), chapter 1 checkpoint (Continue; held on the "Press Space to deflect" prompt, jitter
        frozen). 4K fps avg / 1 % low: ATN's ini (DLSS Custom 85 %, preset L) 65.2 / 60.5, DLAA 52.4 / 49.5, DLSS Quality
        88.6 / 80.3, FSR Quality 101.8 / 92.8 ("FSR 3.1.6 active"), XeSS Quality 95.3 / 86.7 ("XeSS-SR 2.0.2 active"),
        game TAA native 80.5 / 72.8, DLSS Quality + FG 2x 152.0 presented (by the helper; PresentMon filtered to Ryse.exe
        saw nothing). Replaced shaders hit 11-14 of 20; never hit 0x37ABF6CF, 0x910E0E0C, 0xC1F889A4, 0xC3A469C1,
        0xCC6DF78E; unreplaced variant 0x292F2865 (half-res shadow) in every start. NVIDIA overlay: Autoexposure ON,
        exposure scale 1.0. Screenshots s1-s7 normal. ATN's 4 files restored + hash OK, saves unchanged, installs unchanged,
        no crash dump. s8 (rays + RR): the menu keys landed on Campaign > Timeline, ATN loaded the checkpoint by hand; rays
        started 17:25:10 after the PresentMon window (no valid fps); no crash; ONE screenshot 10 s after RR began: BLACK
        scene, HUD only. s9 Nsight: ngfx-capture's default answer KILLED STEAM (ATN restarts it), Ryse exit 53 (DRM), ngfx
        won't intercept ReShade's dxgi.dll: WON'T GET.
      - [x] Ryse rays + RR black scene: NOT black (s8b 18:30-18:35, fix56 + helper114, run_s8b_dlssq_rt_rr_183030): ATN
        loaded the checkpoint, 6 shots 16-66 s after RR began all normal (Colosseum, lit); RR preset 6, 2560 x 1440 ->
        3840 x 2160; DLSS Quality + rays + RR 76.1 fps (GPU busy 6.95, CPU busy 9.50 ms) vs 88.6 without rays; no
        device-lost lines, files restored, no dump. s8's black shot was the first frames.
      - [ ] Ryse rays + RR in motion (s8b sat on the tutorial pause: still camera): a moving-camera look in R2.
        Owner: next Ryse round (R2)
      - [ ] Ryse: cvars from the ini (system.cfg is read before the cvars exist: every line "Unknown command"): a
        [Ryse Debug] Cvars= key or test hooks through the console hook, for Low / Medium, AA modes, supersampling and
        r_ShadersLogCacheMisses / r_LogShaders runs (spec 3 + 4 not run). Owner: next Ryse round
      - [ ] Ryse: helper round trip 2.5 (DLSS Quality) to 4.7 ms (DLAA) a frame against 0.25 game TAA, FSR 1.0, XeSS 1.6;
        DLAA 52 fps vs native TAA 80: profile the wait. Owner: next Ryse round
      - [ ] Ryse FG 2x pacing: frames presented in pairs 0.15 ms apart, shown 2.7-9.3 ms, p99 15.4 ms. Owner: next
        Ryse round
      - [ ] Ryse: port 0x292F2865 (half-res shadow, every start). Owner: next Ryse round (with the families box)
      - [x] DONE (installs 12:10 / 13:5x / 17:37 used it): tw_install.py's Ryse entry (helper + rename aside of the old _dlss add-on by exact name; rym_ patched it,
        .bak_rym): dry run. Owner: coordinator at the install
      - [ ] tools/ingame/deploy.py:30 (_dlss zip entry) and tools/ryse/fg_camera_check.py (removed camera math):
        obsolete, update or retire. Owner: coordinator, release round
      - [ ] ATN DECIDES: Ryse's helper DLSS presets L/M get the sqrt feed (Attila's dark-tone fix) or stay linear; the
        helper's AutoExposure where Ryse gave exposure 1. Owner: ATN after R0
      - [ ] WARP check of rt_specular_merge; switch to twattila/settings_pages.hpp once it drops the lighting_settings
        dependency; the DevKit symlink in Bin64. Owner: next Ryse round
      - [ ] Port the new shader families (tools/nontw/ryse/rym_shaders: families.tsv): tonemap 0xC3970136 wrapper, TAA
        0x6A057F66 (clamps HDR at 1), tiled lighting CS 0x488BF732 / 0xCDB59F36, SSDO 0x06EE6A8E / 0xA65B60AF /
        0xC17645D2 -> GT-VBAO, classic SSAO 0x4FB1027A + blur 0x3453A92B, 14 sun shadow masks -> PCSS, 0x44D3DE7F in
        game; half-res shadows trace first. Owner: next Ryse round (after R0)
    - [x] UE4 MERGED 727a6f2c (uem_, 15 commits; all add-ons + build32 built 06:2x, unload harness PASS 0 problems for
      Tales (no ini; RT + RR + FG ini) and Expeditions, control 160 problems): Expeditions and Tales one add-on each
      (Graphics 1 of 2 / 2 of 2, [<Game> AA] migration tests PASS, every old HDR key kept, DLSS / RR / FSR / XeSS / FG /
      Reflex through the helper, Attila's RT tools at parity, Time Each Pass, no rays without the helper); shared
      graphics_page flags (has_screen_space_shadows / has_gtvbgi / rays_tested), hdr_only::AmbientOcclusionRows /
      FullDownloadNote, talesofarise_dlss/upscaling.hpp shared; Tales Output default Auto (hdr_encoding hook gone, FG
      scRGB via fg_hdr10). CMake NGX block deleted in the merge (no add-on links NGX). Skills / tools lose the _dlss targets.
      - [ ] Tales' rays still run in process (rt_shadows.hpp on its own D3D12 device): move to the helper (rt_scene host
        like Expeditions' ray_tracing.hpp: scene from the depth prepass, sun from the lighting pass, outputs t40-t44,
        bounce resolve); until then rt_scene-only rows hidden, no Upscaling Debug View. Owner: next UE round
      - [ ] ATN DECIDES: Tales' rays now need an upscaler on (before: also with the game's TAA); Path Tracing Bounces
        capped at 2 in both games (was 1..4); RT/PT preset = Attila's values (RR F, range 50, softness 0.8, bounces 1;
        preset E -> K on migration), by eye. Owner: ATN
      - [ ] Dead files: talesofarise_dlss/rr_d3d12.hpp (no user), ngx_backend.hpp, 0x7A1E0D42.ps_5_0.hlsl (folder has no
        target): delete or keep. Owner: coordinator, release round
      - [ ] Expeditions left: the three max(0) carve materials (G1 data run), micromaps start off, no Bend SSS / GT-VBGI,
        NRD default, RT AO range minimum 0.5, no pre-exposure to the helper, expeditionsrome/tests not compiled, debug
        views. Owner: next UE round (after G1)
      - [x] merged into the older "Data runs (ATN allowed, scripted, fresh save backups)" line: Data runs (helper DLAA / DLSS Quality, RR, FG 2x, Tales' in-process RT together; ReShade.log status lines,
        talesofarise_pass_timing.txt, HDR-only start; Expeditions' five carve materials' first-draw lines): Tales G1 + G2,
        Expeditions G1. Owner: coordinator (non-TW data runs)
      - [x] DONE (installs used them): tw_install.py entries for Tales / Expeditions (one add-on, old _dlss renamed aside): dry run. Owner:
        coordinator at the non-TW install
    (merged from "RYSE MERGE ROUND (one add-on): rys_'s plan in its report: mo...": RYSE MERGE ROUND (one add-on):
    rys_'s plan in its report: move _dlss files + 0x52594501-06 / 0x7A1E0D40-42 shaders into rysesonofrome, the DLSS
    addon.cpp as a header, Graphics 1 of 2 (AA / DLSS / FG / RT presets + RT rows) and 2 of 2 (32 game settings +
    presets, Experimental, Audio) via graphics_page, ini sections [Ryse AA] / [Ryse Game Settings] kept, the _dlss
    target removed from the loader list (ATN's one-add-on decision), then helper upscaler / RR / FSR / XeSS / FG via
    rt_core (Shogun 2's HelperUpscaler model), Debug View after R0, page dump, tw_device_loss Ryse table, SDR WARP ramp.
    Owner: RYSE MERGE agent (after s2m_ shows the TW pattern))
    (merged from "UE4 MERGE ROUND (one add-on each, Expeditions first): ue4_'s...": UE4 MERGE ROUND (one add-on each,
    Expeditions first): ue4_'s plan in its report (move _dlss files, shared UE headers to talesofarise/ with Ryse's
    includes, Graphics 1 of 2 / 2 of 2 via graphics_page, ini sections kept, RT / RR / FG through the helper: Tales'
    d3d12_device / rt_shadows / rr_d3d12 go). Owner: UE4 MERGE agent (after s2m_))
    (merged from "Full Attila package for all three (memory project_nontw_full...": Full Attila package for all three
    (memory project_nontw_full_package.md); stamps + watermarks for every game. Owner: per-game rounds after the merges)
  - [x] DONE 11-15 (applied (hfx_ merge)): (04:2x) HANDOFF audit done (hfa_, tools/tw/round_notes/handoff_audit_1115.tsv: 66 DONE, 13 OBSOLETE, 61 OPEN, 14 ATN);
    hfx_ applying it + HANDOFF_ARCHIVE.md. BENCH PREP bpr_ started (copy-out fix for tw_rt_profile*.csv /
    attila_pass_timing.txt / grass dumps, end_of_batch_1115.md plan, dry runs). Owner: coordinator merges both
  - [x] BENCH PREP DONE (bpr_): constantinople_bench.ps1 copies every per-start output out (tw_rt_profile*.csv were LOST
    before: ATN's old copies were restored over them; attila_pass_timing.txt / grass dumps never backed up) + OUTPUT MISSING
    lines; test 15 / 15 in a fake folder; every variant dry-run exits 0; plan tools/tw/bench/end_of_batch_1115.md: 94 starts
    ~6 h (shorter 57 / 3.6 h, shortest 35 / 2.2 h). Coordinator: full plan (no time cap per ATN); cpu5 + grass_dump use
    ATN's installed helper; the older gpu_units_range / gpu_probe sets skipped (g3b2 answers gpu_reuse; units_range /
    probe lines stay open for a later bench); 128-light night / Rarer Objects need other scenes (open)
  - [x] GPU ROUND 4 MERGED (g4_; d22bc4a9 / 507f71cc / f407205a / 1bd9908a): "Torch Shadows Follow the Camera"
    (RayTracedLightRatioFollow, Experimental off, shared graphics_page: Attila, Rome II, Thrones, Troy, Pharaoh, WH1, WH2,
    WH3); Batched Unit Capture for dismember / 4-bone / 4-bone dismember units (58,650,000 floats equal per family,
    capture 1.56-1.84 -> 0.60-0.84 ms in the harness); "Rays Replace Point Light Shadows" (RayTracedGamePointShadows,
    Experimental off, Attila + Thrones, 32 builds, WARP P1-P4 0 apart); FIX: Attila's pointlight00-07 replacements were
    not the game's bits (skinlm 1,305 / 1,301 / 212 / 206 values apart, fxc reassociation): now bit-exact. `captured` as a
    UAV impossible (SO + UAV binding refused). GT-VBAO bitmask: ALU / issue-bound (98 registers); the exact prefetch
    rewrite is slower (1.483 -> 1.544 ms): not taken. tw_rays_captures ALL PASSED. Attila build32 rebuilt on the merge.
    - [ ] Wire RayTracedLightRatioFollow into Shogun 2 (its own rows). Owner: next Shogun 2 round
    - [ ] Skip the game's point-shadow casters under the row (shadow_pointlight VS 7DF3D76D / AD71694F, rigid 8B138A6A,
      billboard 6B379EC6); Rome II's point-shadow builds (64 unique non-MSAA, 96 with MSAA; f407205a's message says 48:
      wrong). Owner: next D3D11 round
    - [ ] In game (ATN's list, per batch): a cutscene with Cutscene Point Light Shadows + the new row (log "Rays Replace
      Point Light Shadows applied: ps X"); Half + RR moving past torches with the follow row; a battle with 4-bone /
      dismember units under Batched Unit Capture Owner: ATN (in game)
    - [ ] Real GT-VBAO savings need per-sample math changes (look change): visual A/B, ATN DECIDES. Owner: later
  - [x] END BENCH PART 1 (03:28-06:15, end_of_batch_1115_run.log): cpu4 (36 starts) + cpu5 (11 of 12) all VALID.
    summary.txt said "no frames in the window" for every start: bench_summary.py's fromisoformat failed on PresentMon's
    hour without a leading zero before 10:00 ("4:28:41"); FIXED, re-summarized into runs\<batch>\summary_fixed.txt.
    cpu5 atn's 4th start: CPU Set Setter's rule never applied ("CPU set first seen never") -> exit 2 stopped the session
    before set 3; constantinople_bench.ps1 now retries such a start once (code 3), part 2's script runs on past a failed set.
    VERDICTS (median vs P per round, ms; spread = P-to-P):
    - sort (f8153d31): cpu +0.15 / -0.34, maxcpu -0.56 / -0.37, atn -0.01 / +0.13; worse beyond the spread in 2 scenes
      -> REVERT (pre-set rule; also removes the Fable review's radix scan-fallback row). Owner: rfx_ (sent 06:2x)
    - gather (8f5e263f, on top of sort): against sort cpu +2.41 / +0.34, maxcpu +0.14 / +0.47 -> REVERT. Owner: rfx_
    - loopanim (author package b): better in all 6 rounds (cpu -0.22 / -0.37, maxcpu -0.62 / -0.29, atn -0.18 / -0.15;
      atn "better in every round") -> into the package's NOTE
    - x87keep (c): cpu +0.37 / -0.47, maxcpu -0.31 / -0.43, atn -0.14 / +0.15: noise -> the package says "no gain
      measured"; memfast128 (a): maxcpu +0.41 / +0.46 (worse) -> "worse in the max-CPU scene"
    - Faster Render Lists (cpu5, Experimental off): better median in all 5 rounds (cpu -0.10 / -0.30, maxcpu -0.10 /
      -0.73, atn -0.14), not a look change. ATN PLACES THE ROW (default on / presets / stays Experimental)
  - [x] FABLE 5.2 HIGH REVIEW DONE (fdfb307e..47aed531, read-only; tools/tw/round_notes/fable_review_1115.tsv, 13 rows):
    no crash-class defect on the default path. CONFIRMED: (1) Shogun 2's merged add-on attaches fully beside an old
    _dlss file (double DLSS / scale hooks if a player unzips over an old install); (2) WH1 grass_height never
    uninstalled (stale callbacks after an add-on reload). PLAUSIBLE / LOW: radix sort site's whole-image scan fallback;
    batched cluster BLASes with empty runs; ReSTIR surface record if a mode traces a subset; OnIndexWrite without an API
    check; OutputAutoPreset on a first start with no Windows answer; Thrones headroom 2 vs 4; 8 MB scratch never freed;
    Tales' notice enumerating modules every frame; shadow_pass hook quiescence; Two Shadow Cascades after RT-off.
    Checked clean: command_action_fast vs RunCallbacks, engine_code stubs by hand, OutputAutoPreset otherwise, Batched
    Unit Capture math, point-shadow rows, units range, sss pick, bridge fields, constant mirror sharing, engine_cvars.
    Not covered: HLSL bit-exactness (needs builds), parts of several per-game files (listed in the review).
  - [x] FIX ROUND MERGED bc7ba324 (rfx_; suites on its branch: tw_engine_code x86 PASS, tw_output_encoding x86 + amd64
    PASS + own control, tw_helper_lifetime / tw_gpu_completion PASS, tw_rays_captures ALL PASSED, fg_bridge_test as
    g3b2, HARNESS_DUMP 59 outputs equal over 10 scenes, forced per-BLAS fallback 0 differences, all_modes 178 / 179):
    Shogun 2 beside an old _dlss file keeps its DLSS / scale / engine half out + migration carries global rows; WH1
    grass_height and frame_trace Uninstall at detach; batched cluster BLASes take non-empty runs (GPU-read count,
    per-BLAS fallback, RENODX_TW_TEST_CLUSTER_BLASES_APART); ReSTIR record invariant comment; rt_core index write
    handlers D3D11 only; Output Auto: failed Windows HDR query = SDR out, no SDR preset, no decision; Thrones / Attila
    AUTO_EXPOSURE_HEADROOM_DEFAULT 4 (protection-off presets write it); Two Shadow Cascades tooltip (restart / the game's
    Shadows option); shadow_pass off-thread warning; sort + gather REVERTED (engine_code.hpp / tests byte-identical to
    f8153d31^; Fable rows 4 and 13 obsolete). Merge conflict hdr_only.hpp: kept uem_'s HelperInstalled() notice (no
    module listing left; rfx_'s 2 s limit dropped).
    - [ ] release_rr's +30 MiB threshold is too tight (the base helper reaches 419 MiB too; fails 2 of 16 on base).
      Owner: next harness round
    - [ ] shadow_pass: (a) the row's next game run reads the new off-thread warning (if it fires: suspension-based 6-byte
      patch); (b) relearning needs per-kind call counts over a moving battle. Owner: next Attila CPU / D3D11 round
    - [ ] Two Shadow Cascades live rebuild (no restart). Owner: next D3D11 round
    - [ ] +265 MB VRAM for the bounce-size ReSTIR record. Owner: next ReSTIR round
    - [ ] Shogun 2 terrain_detail not restored at detach (minor). Owner: next Shogun 2 round
    - [x] OBSOLETE (Fable review 11-15: no add-on includes it; goes with the Dead files box): talesofarise_dlss/frame_generation.hpp detours have no uninstall. Owner: next UE round
  - [x] FINAL VERIFICATION bc7ba324 (rfx_): builds exit 0; tw_engine_code x86 PASS (3 DLLs), tw_output_encoding 5 / 5
    x86 + amd64, tw_helper_lifetime / tw_gpu_completion / tw_screen_space_shadows x86 + amd64 PASS, tw_rays_captures ALL
    PASSED, fg_bridge_test PASS (7 phases), all_modes 178 / 179 (rr_ao_reblur_release: the same flaky +30 MiB sets check,
    4 / 4 alone), HARNESS_DUMP 59 / 60 equal to rfx1 (nrd_bounce_reuse nondeterministic in rfx1 too); page dumps: TW +
    Shogun 2 + Ryse unchanged, Tales / Expeditions +1 row RayTracedGamePointShadows (f407205a's shared row, Experimental
    off); parity_check exit 0 after 3 ledger lines (shogun2 RayTracedLightRatioFollow SHARED STEP / RayTracedGamePointShadows
    NEEDS A TRACE, romeremastered RayTracedLightRatioFollow SHARED STEP). Main checkout build + helper114 (sha256 cf5ba820...,
    44 headers as g3b2).
  - [x] INSTALLED 08:00 (tw_install.py --helper 114 --install; stamped, hashes checked, ATN's files unchanged, fresh
    backups): Attila v161, WH3 v60, Pharaoh v68, Rome II r4, Thrones r5, WH2 v51, Shogun 2 v74 (old _dlss add-on
    .retired-20261115_080042), Troy v55, WH1 r6, Rome Remastered v12, all with helper114; Agility SDK D3D12Core.dll
    copied to the 8 games that lacked it. Not run in game.
  - [x] END BENCH PART 2 (08:03-10:45, end_of_batch_1115_run2.log; installed v161 + helper114, each set's test add-ons /
    helpers): 6 sets, 46 starts, all VALID, no CPU-set retry. Per set (tools/tw/bench/runs/<set>_<scene>_<time>/summary.txt):
    - g2a_torch (atn, CPU-bound): Time Each Pass "lights" off 0.48 / base 2.79 / pick 1.12 ms, PresentMon GPU busy 17.3 /
      19.5 / 17.85 ms: the torch pick (7fd71432, in v161) cuts the Torch and Fire Shadows row from +2.3 to +0.64 ms GPU
      (harness said ~+0.2: the march stays). Frame: base = off; pick round 1 +2.8 ms (that start's CPU busy +2.7), round 2
      +0.03: one noisy start, not the shader.
    - g2a_shadow (atn, max): skipwork CPU busy -1.8 (atn) / -2.3 ms (max), skipmaps -1.3 / -1.7; GPU waits for the CPU
      (g2a_tep_gaps.py) 3.8 -> 1.1 / 1.8 ms (atn), 5.4 -> 1.5 / 1.0 (max); frame unchanged (atn -0.25 / +0.01 ms, spread
      0.30; max +0.0..+0.3, the control): the CPU time saved turns into CPU wait (another limit in these views). Row stays
      as it is (Experimental); left: ATN's look check. max base "units" kind 9.54 / 9.62 ms.
    - g2b_clusters (max): helper cluster build base 1.95 -> blas (g2b2, in helper114) 1.78 -> trunc8 1.58 ms, scene 3.68
      -> 3.52 -> 3.32 ms; fps spread 0.74 ms: no fps verdict. trunc8 (8-bit truncation) is a look change: ATN DECIDES.
    - g4_capture (max, atn): Batched Unit Capture on: median -2.06 / -1.93 ms (max), -1.14 / -1.01 (atn), fps +1.1 / +1.3
      and +1.3 / +1.2, GPU busy -1.7 / -2.2 and -2.0 / -1.8 ms, "units" kind 9.68 / 9.49 -> 7.05 / 6.87 ms; better beyond
      the spread in every round in both scenes; the proof line in the on starts only (3 / 1 unit draws' corners in one
      pass). Bit-exact in the harness: a default-on candidate, ATN DECIDES.
    - g3b_reuse (max, atn): the per-texel record (a833578d, in helper114): bounce 2.85 -> 2.63 ms (max), 2.76 -> 2.52
      (atn), reuse 0.87 -> 0.61 / 1.07 -> 0.79; Lean on top: bounce -0.09 / -0.16 ms more (reuse 0.55 / 0.66). fps:
      CPU-bound, base spread 0.01-0.05 ms, the per-round "worse" flags are noise. For ATN's Lean row decision.
    - grass_dump (max, atn): the GRASS line under "ONE Constantinople bench" (grass_replay_N.txt / .json in the runs).
    - [x] DONE 11-15 (ATN 12:1x rule: on by default, 356b7100): ATN DECIDES: Batched Unit Capture (RayTracedBatchedUnits) on by default (bit-exact; -1 to -2 ms a frame in both
      bench views). Owner: ATN
    - [x] DUPLICATE of "g2b trunc8 (8-bit cluster truncation" in OPEN FOR ATN (11-16): ATN DECIDES: g2b trunc8 (8-bit cluster truncation: -0.2 ms more cluster build; a look change). Owner: ATN
    - [x] ATN 12:1x: "Default everything that makes it faster to on and the rest that could have lower quality, I will
      decide": 356b7100 turns on Batched Unit Capture (shared rt_scene), Faster Render Lists (Attila, Thrones), WH1's
      Skip the Game's Reflection Passes (bit-identical in its test part 14); none shipped to players before (merge-base
      against v146 8dca9eef), so every player gets the new default; ATN's own ini keeps a saved Off. Faster Game CPU
      Code was on already; Path Tracing SER stays off (no gain measured). Skills 53ef3773 carry the rule.
    - [ ] Faster-with-a-look-risk list for ATN (numbers in the reply 12:1x): Lean ReSTIR Reuse, trunc8, Faster Smoke
      Shadow Build, Terrain / Grass Without Waiting, Skip the Game's Shadow Map Work, Skip Trees in Shadow Maps, Skip the
      Shadow Maps, Rome II's Skip the Shadow Maps, One Model per Place Out of View, Fast Leaf Shadows, SHaRC, WH3's
      Faster Reflections, GPU grass phases 1-3. Owner: ATN
    - [x] INSTALLED 12:10-12:11 (tw_install.py --helper 114 --install; fresh backups 20261115_121015 / _121136, hashes
      checked, ATN's files unchanged) on 356b7100 + ad641275, every add-on rebuilt 12:05-12:07: Attila v162, WH3 v61,
      Pharaoh v69, Rome II r5, Thrones r6, WH2 v52, Shogun 2 v75, Troy v56, WH1 r7, Rome Remastered v13 (helper114 keeps
      each game's earlier stamp); FIRST stamped installs (--first): Ryse fix55, Tales v72, Expeditions v29 (helper114,
      NVIDIA 310.9.16, Agility D3D12Core, FSR / XeSS / OMM DLLs; old 0- / 1-prefixed add-ons .retired-20261115_121136,
      DevKit .aside-20261115_121136, Expeditions' symlink .symlink-20261115_121136). Their 8 watermarks each survive in
      the binaries (scan 8 / 8). Not run in game. Author zip in ATN's Downloads (+ timing program, bench tables, the
      one-line x87 bench patch).
    - [x] DONE 11-15 (Tales G1 + G2, Expeditions G1 + v32 recheck, Ryse R0 + s8b): Non-TW data runs (ATN 12:2x "Sure, go ahead"): Tales G1 + G2 (agent started 12:2x, tools/nontw/tales/g1g2),
      then Expeditions G1, then Ryse R0, one at a time. Owner: coordinator
      - Tales try 1 (12:24 / 12:30): SteamStub relaunches the exe through steam.exe, the launching shell's RENODX_TW_SET
        never arrived (no "[Tales] Test:" line); no save loaded, nothing measured; ATN's files restored, hashes OK, no save
        changed. The first start's log: migration "8 values of [Tales AA] taken over (SettingsRevision 1)", scRGB, DLSS
        Quality preset L through the helper, AA constants matched 30 readbacks, no ERROR. Fix f4d95f6c (shared
        test_hooks.hpp: renodx_test_hooks.txt next to the exe, deleted at the first present); Tales v73 + Expeditions v30
        installed 12:41 (stamped, ATN's files unchanged); the Tales agent resumed with the file.
      - [x] Tales G1 + G2 DONE 12:50-13:28 (tools/nontw/tales/g1g2/run_124955): both saves loaded by keys; PresentMon fps
        G1 DLAA 94.9 / Quality 165.6 / Performance 232.4, with RT 40.8 / 77.3 / 114.8, Quality RT + FG 2x 133.8; G2 76.9 /
        128.5 / 173.8, RT 46.4 / 84.3 / 122.6, FG 140.9; RT +4.1-6.9 ms at Quality; "[Tales ray tracing] Running (DXR 1.2:
        opacity micromaps, SER; Agility SDK)", RR active (D3D12); Time Each Pass with RT: TAA/DLSS cs 88765764 3.57 ms,
        lighting00 3.24, sunshadow00 2.85; frame traces: all 14 replaced families hit their replaced permutation, none
        unreplaced; WPR G2: add-on 1.6 % of the process; no errors; ATN's 13 files restored + hash OK, no save changed.
      - [x] FG fell back to FSR FG: no Streamline next to the helper. 11 of 13 folders had none (only Attila, WH3):
        tw_install.py now writes ngx_dlls.STREAMLINE (sl.interposer / common / dlss_g / reflex / pcl 2.14.16) where
        missing; INSTALLED 13:5x with the rebuild for f4d95f6c: Attila v163, WH3 v62, Pharaoh v70, Rome II r6, Thrones r7,
        WH2 v53, Shogun 2 v76, Troy v57, WH1 r8, Rome Remastered v14, Ryse fix55 (unchanged), Tales v74, Expeditions v31
        (build_shared relinked Tales / Expeditions after their v73 / v30 stamps: new tags, same source); Streamline in all
        13 folders, ATN's files unchanged.
      - [ ] Release round: player / release zips carry no Streamline (release_zip.py, player_zip.py): DLSS FG there needs
        sl.* 2.14.16 + NVIDIA's licence in the zips, or a README line. Owner: coordinator, release round
      - [ ] Tales: G2's camp save has no torch / night / indoor view; the in-process RT has no torch rows yet (Tales' rays to
        the helper box). The NVIDIA DLSS on-screen indicator shows in every shot (ATN's driver setting). Owner: next UE round
      - [x] Expeditions G1 ran 16:14-16:35 (tools/nontw/expeditions/g1, two starts): start 1 (rays on at launch)
        CRASHED in the main menu ~9 s after "Ray tracing ready" (run_epic_161410, crash_29648.dmp); start 2 (rays off)
        ran the timeline in the MAIN MENU (the UE menu ignored SendInput Up / Down / Enter / Tab / Space: save not loaded).
        PresentMon 4K menu: DLAA 83.1 fps (GPU busy 8.14 ms), DLSS Performance 150.1, Performance + FG 2x 221.1 (DLSS-G
        "FG v2", HUD recomposed), DLAA + FG 2x 138.5, game TAA 135.9. DLSS dispatch 4.20 ms of a 12.28 ms DLAA frame.
        Migration "6 values of [Rome AA] taken over", preset L, "GPU wait removed", no errors; ATN's 20 files restored +
        hash OK, no save changed, installed files unchanged. Low (sg=0) start not run.
      - [x] DONE 11-16 (fixed acf3d270, confirmed in game 17:38-17:54): Expeditions RT crash: 0xC0000409 from std::_Throw_Cpp_error (std::system_error, a std::mutex locked twice on
        one thread): add-on RVAs 0x162D4B -> 0x125B37 -> 0xC1685 -> 0xC6698 -> a D3D11 create -> ReShade -> add-on
        callback near 0xC0864 throws, right after RT installs (stamped 13:48 add-on; the 13:44 PDB does not match).
        Owner: coordinator (now)
        - CAUSE FOUND + FIXED acf3d270 (not built): CaptureDraw held rt_core's runtime.mutex across its stream-output
          DrawIndexedInstanced (vtable 0xa0); D3D11 deleted a released shader at that draw, ReShade fired destroy_pipeline
          on the thread, OnDestroyPipeline locked runtime.mutex again. pixel_coverage now has its own shared_mutex. Every
          scene_draw host had it: Expeditions, Ryse, Rome Remastered, Shogun 2. Skills f99e871c.
        - [x] 17:37 Expeditions v32 + Ryse fix56 built (acf3d270) and INSTALLED (tw_install.py, helper114 kept, ATN's
          files unchanged); re-check runs started ~17:40 (Expeditions rays on at launch; Ryse s8 rays + RR, 4 shots).
        - [x] FIX CONFIRMED in game 17:38-17:54 (expeditions/g1/run_epic_173834): Expeditions v32 with rays on at launch ran
          ~9.5 min of rays (RR active, Ray tracing ready), all 7 segments, normal exit, no dump; ATN's 20 files OK, saves
          unchanged. Menu fps rays on / off: DLAA 22.9 / 83.1, DLSS Performance 79.5 / 150.1, Performance + FG 136.4 /
          221.1.
        - [ ] Expeditions rays-on DLAA 22.9 fps with GPU busy 9.31 ms (frame 43.7 ms): a stall, not GPU work (helper
          wait? RR at native?); read that run's pass timing + helper log. Owner: next UE round
        - [x] Ryse s8b re-check RAN 18:30-18:35 (ATN loaded the checkpoint): not black (see the Ryse R0 boxes).
        - [x] DONE 11-16 (TW install 05:0x: Attila v164, Rome II r7, Thrones r8, WH1 r9, WH2 v54, Troy v58, Pharaoh v71, WH3 v63, Shogun 2 v77, Rome Remastered v15, all on helper115): The rest (Rome Remastered, Shogun 2 and every add-on on the shared code) on the review-fix commit: build all,
          stamp, install. Owner: coordinator after the review fixes
      - [ ] Expeditions save load: the main menu takes no SendInput keys; the save scene (carve materials 93E43D26,
        07E055CF, 1F128822, 630E166D, A7FF9EDD first-draw lines, RT on vs off) still needs a way in. Owner: ATN decides
        (foreground fix / a mouse click), then coordinator
      - [ ] Expeditions unreplaced post into 8-bit f90 targets before the tonemapper: A45907DE (1920x1080), 2491478D
        (960x540), D9414A76 (3840x2160 + depth); B25C9978, FF9ECC89 (target unknown): clamp check. Owner: next UE round
    - [x] DONE 11-16 (skills f99e871c, plans written): ATN 17:0x: "Add new/fixes to the skill then Start the whole package planning for Divinity 1, Divinity2 and
      R.U.S.E." (PsychoV35 position: never on top of UpgradeToneMap / UC2 Extended; Divinity 2's at its Psycho's place).
      Skills f99e871c. Planners d1_ / d2_ / ru_ started ~17:05 (brief tools/tw/round_notes/nontw2_plan_brief_1115.md,
      plans to tools/nontw/divinity1 | divinity2 | ruse/plan.md). Owner: coordinator
      - [x] DIVINITY 1 PLAN DONE 18:2x (d1_): tools/nontw/divinity1/plan.md + divinity1.tsv (84 rows: 49 DO NOW, 10 HAVE,
        12 NEEDS A TRACE, 8 WON'T GET, 1 SHARED STEP), names.tsv (3192 hashes), atmospheres.tsv (243 scenes), tools/.
        R0 ran 17:54-18:04 (installed 11-11 build, DevKit snapshots of the Cyseal beach: 684 / 606 draws, pass order +
        formats; tone-map input R11G11B10F, mean 0.225, max 9.3; GPU busy 3.0 ms, 120 fps cap); retry 18:10 crashed 20 s in
        with no input; ATN's files restored 0 differ both times.
      - [x] DONE 11-16 (411a3dd5; switch fix fa089b0e confirmed in game): D1 step 1 (TOP, S): the start crash: the game recreates its swap chain 2-4 times a start (and at window-mode
        changes); swapchain_v2.hpp:633-639 makes each FLIP_DISCARD x2; the old chain lingers ("IDXGISwapChain0 reference
        count inconsistent (1)"), the game's BufferCount 1 path gets E_ACCESSDENIED, EoCApp+0xF7C532 reads null +0x18.
        WARP repro test/tw_d1_swapchain (control must fail), unbind + Flush at destroy, fall back to the game's swap effect
        on failure, shared opt-in for other D3D11 games; check Tales / Expeditions logs for the same warning. AFTER fx_
        (it edits swapchain_v2.hpp for R19). The installed 11-11 Divinity 1 build can crash any start (ATN told).
        Owner: Divinity 1 implementer (d1_ resumed)
      - [x] D1 STEPS 2-7 MERGED f850f93d (d1_, 8 commits): census 3192 (1497 PS, 1689 VS, 2 CS; 3188 decompiled,
        tools/nontw/divinity1/census); test/tw_divinity1 (SDR = the game's bytes, mid grey / monotonic / Peak 1000 + 400
        pass, mode 1 control fails, grade to 9.5e-7); HDR (Extended) mode 3 + HDR (PsychoV35) mode 4 once on Base.Load,
        Hue Clip 70, no per-channel boosts; V30 / Pragmap kept; Output HDR10 / scRGB / SDR / Auto (Auto default), presets
        (Recommended V35 bloom 0.25, Reset Extended, SDR, "PsychoV30 (earlier Recommended)"), first start SDR, About
        (mods/80), device_loss, fast dispatch; frame trace + Time Each Pass + test hooks + applied lines; page dump
        (tools/tw/page_dump dump_d1), parity_check 'd1' (open 169 = steps 10-12), tw_install 'divinity1' (no helper; prefix
        'd1 ', stamped folder 'divinity1'); test/tw_d1_swapchain repro (flip chain with a bound / read / viewed back buffer:
        E_ACCESSDENIED; unbind + Flush: S_OK). Built, not installed.
      - [x] DONE 11-16 (d1 v1 / v2 stamped + installed): D1 install: --first (add-on + DevKit are symlinks in Shipping today), DevKit aside, fresh backup. Owner:
        coordinator (next install)
      - [ ] ATN by eye (Divinity 1): Hue Clip 70 in Extended / V35. Owner: ATN
      - [ ] D1 steps 9-10 + Graphics 2 of 2: helper route at 100 % (DLAA / FSR / XeSS, b12 jitter, camera motion, FG,
        Reflex, RR guides); rt_core host (RT shadows into the PCSS mask, RT AO). Owner: d1_ after R1
      - [x] DONE 11-16 (ran 01:40 (run_r1_014005) and r2ssr 03:21 (run_r2ssr_032119)): D1 R1 run (after steps 1 + 7, ATN's OK, no DevKit): main-menu torches (fire values, point lights, run-time
        shader dump), beach Params / ColorMatrix vs atmospheres.tsv, b12, mode shots, timing, window-mode path with the
        fix; optional SSREnabled 1, FIREEXAMPLE save. Owner: coordinator (next game window)
      - [x] ATN ANSWERED 18:3x (Divinity 1): keep V30 / Pragmap for now (ATN decides at the end); bench / R1 save = ATN's
        own profile, newest save (Act 2/3); no start crashes known, two seen at fullscreen -> fake fullscreen; below 100 %
        YES (the full Attila package); fire through Hue Clip Emulation, ideally no per-channel boosts; rows may write
        graphicSettings.lsx; Pragmap credit not needed; the "mod invalid" dialog is expected (mods not installed). Sent to
        d1_.
      - [ ] ATN at the end (Divinity 1): drop or keep the legacy V30 / Pragmap modes. Owner: ATN
      - [x] DIVINITY 2 PLAN DONE 18:4x (d2_): tools/nontw/divinity2/plan.md + divinity2.tsv (~88 rows), scripts,
        shader_dump, engine_shaders, r1/run_r1_181317. Psycho: no stacking; every modded mode branches at scene-linear AP1
        after the white balance + ColorCorrectAll (PPComputeLUT.shd:65; t00:124-211); V35 goes where V30 is called
        (t00:247), Tales' point (lut00:259). Census: 7,187 distinct shaders (7,119 in the LZ4 packs, 6,376 live; 68 in
        EoCApp.exe's UI renderer); R1's city 155 + menu 110 all in it. R1 18:13-18:26 (installed build, DevKit snapshots,
        ATN loaded a 6Party Arx save and saved once: kept): R11G11B10F scene, PPComputeLUT once into a 32^3 R10G10B10A2
        UAV, PPApplyLUT writes PQ, EoCSight_HDR / EoCFog_HDR / SMAA on PQ / CombineUI_HDR; no FP16, no velocity target;
        fps NOT valid (always-on-top window over the game); files restored 0 differ; ended by Stop-Process (two closes
        ignored).
      - [x] DIVINITY 2 ROUND 1 MERGED 0e7ce1ba (d2_, 9 commits): census 6,444 live decompiled (census_settings /
        materials / scale.tsv: 829 shaders read the screen size); mode 4 "Vanilla+ (PsychoV35)" at t00:247, Hue Clip 80;
        Recommended (V35, bloom 0.44) / Reset (Vanilla) / SDR (Off), old presets + V30 kept; Mid Gray row (game HDR 15
        nits default / SDR picture 20.3); part 6 WARP PASS x86 + amd64 (18 old LUTs bit-equal, V35 grey 15.00, stacked
        control 6.16 rejected, Off = game bytes); Vanilla Per-Channel Highlights on (acts only below the game's peak);
        fast dispatch, About (mods/547, ACES notice); frame trace + PPComputeLUT cb0 / b12 + test hooks + applied lines;
        page dump dump_d2 (61 rows), tw_install 'divinity2'; parity_check 'd2' 6b095e34. Built, not installed.
      - [ ] ATN DECIDES (Divinity 2): "Exact Colour LUT (Experimental)", off: the game reads its colour LUT half a texel
        off in every mode (grey 12.2 nits where a mode sets 15; on: 14.98) and it brightens mid tones of every modded
        mode. Owner: ATN
      - [ ] Divinity 2 SHARED STEP: output_encoding needs a game-owned-output flag (Apply must not change the swap chain
        format / colour space: the game makes its own HDR10 chain) + the Rendering Mode key as a Config field (S1 done c04da639; corrected 11-16: the first_start_key
        "tonemap_type" is Divinity 1's, addon.cpp:999; Divinity 2 sets none yet); until then no first-start SDR switch. Zips ship ACES_LICENSE + RENODX_LICENSE. Owner: coordinator
        (shared-steps round after the fix merge)
      - [x] DONE 11-16 (a5d62b19, merged 1c1641ab): Divinity 2: the orthographic EoCSight_HDR 0x5C4137E2 (50-nit constants vs 300) needs a trace first. Owner:
        d2_ after R1b
      - [ ] Divinity 2 later: D2-8b scene R11G11B10F -> RGBA16F (view cloning, Experimental off); D2-9 helper DLAA / FG /
        Reflex on the linear scene before PPApplyLUT; D2-11 RT host (RT sun into the R8 mask, shadow pass skipped under
        RT). Owner: d2_ after R1b
      - [x] DONE 00:13 (R1b agent, ATN loaded the Arx save, stamp "d2 v1 addon" via tw_install divinity2=1 --first;
        tools/nontw/divinity2/r1b/run_r1b_235909): HDR LUT cb0 (c31 = 3, 1e-4, 1000; c32 1516.49); SDR LUT cb0 from the
        inventory = ACES 0.02 / 48 nits, output device 0; PerView b12 (vfov 45, city near 1 / far 100, menu far 270);
        per-mode shots Off / Vanilla / V30 / V35 / Tweaked (c1_grid.jpg; ATN's HDR Analysis overlay visible); PresentMon
        30 s focused 247 fps, 4.05 ms, p99 4.62, GPU 3.74, CPU 1.88, Independent Flip. ATN's 10 files restored + hashed.
        Divinity 2 R1b (trace build): both cb0s (HDR / SDR LUT), PerView b12, per-mode shots, valid frame times, the
        inventory / character sheet with ATN's key.
      - [x] DONE 11-16 (gamma 1.2 run run_r2gamma_042527 at 04:25; its analysis goes to the "Divinity 2 next run" box): Divinity 2 R1b leftovers: the gamma slider at 1.2 (needs ATN's clicks); no menu videos exist (the menu is a
        live 3D scene). Owner: next Divinity 2 run
      - [x] MERGED 1c1641ab 00:5x (d2b_): a5d62b19 orthographic EoCSight_HDR 0x5C4137E2 replaced (its bytes hold 50 / 100
        nits, an older compile than the source's 240 / 300; never drawn in R1 / R1b, a CameraOrtho camera picks it);
        5815198f Hue Clip reference = the game's SDR transform on the traced constants (within 0.32 8-bit codes; the old
        stand-in was off by 137), part 6 on the R1b cb0s, Off t00 + t01 bit-exact with failing controls (x86 + amd64
        PASS); 2435eb92 "Higher Precision Scene (Experimental)" off. ATN's UI Brightness is 203 (not 50; 50 is the game's
        own HDRPaperWhite, ignored in modded modes). Divinity 2 after R1b: D2-8, ortho EoCSight, D2-8b.
      - [x] MERGED a318ef72 02:1x (d2c_ e7ea6056 + 012424bc): V35's own sliders default to the game's response (Contrast
        1.42, Shadows 0.43, Saturation 0.93, Highlight Contrast 0.87; one curve, nothing before V35); grey 0.05-1 nits x3.1
        ..x6.9 -> x0.97..x1.04 of the game, mid-tone chroma x0.81 -> x1.03, highlights within 7 % of V35's own; part 6
        section 10 PASS with a failing control. Saved V35 sliders stay: press Recommended. ATN ANSWERED 01:0x "Yes, match the game": V35 keeps the game's darker shadows and stronger colours (V30 /
        Tweaked unchanged).
        (was) ATN DECIDES (Divinity 2, from the R1b shots): V30 / V35 shadows ~5x brighter than Off (0.05-1 nit pixels) and
        mid tones ~23 % less saturated (flatter than the game's ACES toe); Hue Clip default (80 kept; 60 closest on
        average, 80 best for fire / red). Owner: ATN
      - [ ] Divinity 2 next run: what triggers the orthographic camera (tactical toggle?); A/B of Higher Precision Scene
        (alpha readers, copies, inventory, back buffer through the proxy); gamma slider 1.2. R13 exact tests for bloom00,
        ui00, outlines00-02. Owner: next Divinity 2 run / ht_
        11-16: the gamma 1.2 run is done (run_r2gamma_042527, 04:25); its analysis (an_,
        tools/tw/round_notes/runs_1116_analysis.md) lands here. Owner: next Divinity 2 run / ht_
      - [x] ATN ANSWERED 18:5x (Divinity 2): mid grey BOTH as a row (ATN decides later); "i" is the inventory key; keep the
        old presets + V30; per-channel shoulder ON by default; Nexus divinityoriginalsin2definitiveedition/mods/547; Script
        Extender installs as DXGI.dll in DefEd\bin (ReShade can't be dxgi.dll there: d2_ writes the rule). Sent to d2_.
      - [ ] ATN at the end (Divinity 2): mid grey row's default; old presets / V30 keep or drop. Owner: ATN
      - [x] R.U.S.E. PLAN DONE 18:4x (ru_): tools/nontw/ruse/plan.md + ruse.tsv, census\ (all 1,803 shipped shaders:
        1,528 ps_3_0 + 275 vs_3_0 from ZZ_Win.dat's shadercachev02.shc, decompiled, families), scripts\, u0\. U0 runs
        18:04-18:10 + 18:26-18:29 (plain game, no ReShade on D:): D3D9, battle 158 fps (VSync cap), GPU busy 3.4 ms;
        DirectX11PlatformWanted=true still D3D9 and no DXBC ships: DX11 helper route WON'T GET; the window close crashes
        the unmodded game ("ChangeMode failure"); menu ignores keys, battle camera keys work. Psycho: V35 at the right
        place but keeps slope 1 while each map grades Brightness * rgb * luma^(Gamma-1) (Gamma 1.0-1.6 over 87 maps):
        25-60 % flatter.
      - [x] R.U.S.E. ROUND MERGED 8e80afae (ru_, 10 commits): U2 part 6 WARP on the real postprocess.hlsli PASS; U1 V35
        after each map's own response (slope = Gamma exactly, markers keep their colour; contrast_scale drifted saturated
        colours, not taken), Extended as mode 4, Reset = Extended; U3 Output HDR10 / scRGB / SDR / Auto, first start SDR,
        SDR preset, proxy SDR branch; U4 About (ruse/mods/6) + "Turn on the in-game HDR setting."; U5 12 more UI / video /
        loading replacements (SM3 interpreter: 0 difference), Video AutoHDR off + dithering; U6 Frame Trace + Trace One
        Frame, "Wait for Each Frame Copy (Experimental)" on; U7 parity 'ru' (open 2), page dump dump_ru, tw_install
        'ruse'; U9 Graphics 2 of 2: Shadow Map Size 4096 / 8192, Sprite Resolution 4096 / 8192, Sprite AA 8.5, Detail
        Distance 2x / 4x (Option.ini written each start, game's values kept, "Game's" restores byte for byte). Built only.
        D3D9 bridge (plan 4c): DLAA / DLSS / FSR / XeSS yes, FG + Reflex after DLAA, AO / GI Experimental, RT / PT WON'T
        GET (no stream output, pure hardware device: no ProcessVertices); the device is D3D9Ex already.
      - [x] R.U.S.E. B0 TRACE RAN 20:08-20:12 (ATN's OK + ATN loaded the battle): ReShade 6.8.0.2155 installed as
        d3d9.dll + ReShade.ini (FrameTrace=1) in D:\...\R.U.S.E, add-on stamped "ruse u1" (af1acced, tw_install --first);
        two one-frame traces (tools/nontw/ruse/u1_trace_200820); clean exit, no dump, Option.ini unchanged. ru_ reads them
        and writes ONE whole-package plan (ATN 20:0x: "plan and do everything ... from the beginning", after the reviews).
      - [x] R.U.S.E. B0 ANSWERED + WHOLE-PACKAGE PLAN DONE 20:4x (ru_, offline, no commits): tools/nontw/ruse/plan.md
        section 10 (scripts ru_trace_passes.py / ru_trace_vs.py). Battle frame (AA on): 3,855-3,948 indexed draws, 0
        instanced; shadow map 2048 1,512 draws; ground pre-pass 4K 33 (own depth 34450ee0, BGRA8 .x = ground sun shadow,
        .yz motion -> StretchRect 54712ec0, read by VPOS); main lit 4K MSAA 2,121 (objects 1,434, terrain 484, water 34);
        main depth 361219b8; post s0 = resolve 382ebdc0; map events fire (only 2 ring buffers a frame, scene meshes
        static); 393 skinned draws (64 bones); camera c0-c8, world ~3.8e5 (capture camera-relative).
      - [x] DONE 11-16 (df26e42c, c76cb18b, e6e897ad): R.U.S.E. whole package in ONE implementation round, plan.md 10b order, all Experimental until the T-run:
        W1-W3 (resource log, own D3D9 passes on the proxy's shared textures, D3D9Ex fence) -> D1-D6 (INTZ depth, DLAA row
        writes MainRTAA off, depth-aware camera motion, jitter via VS c0-c3, DLAA back into 382ebdc0, FSR / XeSS, DLSS
        <100% only if MainRTDivider is a render scale) -> A1-A5 draw replay (mirror buffers at unmap, 275 VS -> vs_5_0
        stream output proved by the SM3 interpreter, ~1,500 object draws + 32 terrain patches) -> G1 RT sun shadows into
        54712ec0.x (no PS edits) -> G2 Bend (terrain self-shadow) -> ground AO (110 terrain PS) -> RT water reflections
        (94 PS) + one-bounce ground GI -> FG (HUD-less = back buffer before the first UI draw) + Reflex -> P1 pages +
        tw_install with helper. Tests H1 (D3D9Ex + D3D11 GPU harness), H2 (helper on synthetic frames), H3 (replay on the
        trace's constants), interpreter per generated shader. Stop rules: D4 DLAA passes H1 + H2 before the replay; the
        T-run's replay CPU cost decides default on / Experimental. Size XL+. After the review fixes merge, then the Fable
        5.2 High bridge review (ATN). Owner: ru_ (implementation round)
      - [x] DUPLICATE of "R.U.S.E. T-run list (ATN loads a battle" (11-16): R.U.S.E. T-run (one agent data run, ATN's OK): W1 resource log (pools, formats, samples), an AA-off frame
        (INTZ), small vs big battle draw counts, PresentMon CPU / GPU, is uniSunView the sun's projection alone
        (off-screen casters), MainRTDivider, Option.ini 8192 / 4x kept? Owner: coordinator (next game window)
      - [x] ANSWERED 20:5x (ATN: objects join the round; sent to ru_; PT stays impossible): R.U.S.E. not this round (ru_'s plan): O1 RT shadows for objects (needs +2,000 replayed depth draws or last
        frame's depth; objects keep the game's maps, 8192 via Graphics 2 of 2); path tracing (forward renderer: no depth +
        materials per pixel before shading). ATN wanted "everything": ATN DECIDES whether O1 joins the round. Owner: ATN
      - [x] OBSOLETE (answered by the 20:08 trace + plan.md 10): (was) R.U.S.E. D3D9 bridge B0 trace (next game run: Frame Trace at the menu + in battle, in-game AA off and on:
        depth MSAA / INTZ, motion target, camera pass 0x88292FC6 constants c0-c8, final post samplers), then B1-B4, then
        the Fable 5.2 High review (ATN). Owner: coordinator (run) then ru_
      - [ ] R.U.S.E. U1 run (after ReShade + the stamped add-on are installed on D:): Frame Trace menu + battle; every
        Rendering Mode by eye; Graphics 2 of 2 at 8192 / 4x (does the game keep and use them?); Wait for Each Frame Copy
        off: PresentMon A/B + loading screens. Owner: coordinator (next game window)
        Merged 11-16 from "R.U.S.E. U8 / U1 run": ReShade + the stamped add-on into D:\...\R.U.S.E (coordinator, fresh
        backup; ruse u2 installed 03:2x with --first), trace build, starts: ATN's settings, proxy waits off,
        ShaderQuality 1 / 2, UseHDR off, Option.ini above the menu's max; ATN loads the save and quits by hand. Owner:
        coordinator (next game window, ATN's OK)
      - [ ] R.U.S.E. open ledger rows: Time Each Pass (D3D9 timestamp queries, optional); the HDR-only snapshot check
        (release round). Owner: ru_ / release round
      - [x] DONE c04da639 (sh_): SHARED STEPS from R.U.S.E.: S1 output_encoding's first-start key as a Config field ("rendering_mode"; Divinity
        1 needs the same with "tonemap_type", addon.cpp:999; corrected 11-16, was "Divinity 2"); S2 output_encoding Apply calls utils::device_proxy::SetTargetFormat /
        SetTargetColorSpace for proxy games (R.U.S.E. does it itself until then); S3 a runtime filter in
        twattila/test_hooks.hpp (only the D3D11 proxy's runtime). Owner: shared-steps round (after the fix merge)
      - [x] ATN 20:4x: V35 = Recommended AND the default Rendering Mode (Reset stays Extended): DONE 995f08fc (merged).
        RT / PT RE-CHECK DONE (plan.md 4c "RT / PT: the routes"): WON'T GET was wrong; one route, XL, the trace decides:
        a D3D9 draw replay on the proxy's D3D11 device (VB / IB via ReShade's Lock / Unlock map events in RESHADE_ADDON=2
        builds, the 275 vs_3_0 recompiled to vs_5_0 with stream output and proven by the SM3 interpreter, 52 skinned VS
        with a 64-bone palette, terrain plain VB geometry, imposters out) into rt_core's clip-space host; RTX Remix's
        runtime does not fit (a whole renderer). NEW: the ground has a screen-space sun mask (pre-pass 0x8588D239 writes
        ShadowMap.x; 110 terrain + 94 water PS read it by VPOS): RT sun shadows or Bend can replace .x with no PS edits.
        Order: DLAA (B0-B4) -> Bend into the ground mask -> route a for RT sun shadows (Experimental) if B0 shows map /
        unmap firing and under ~2,000 scene draws in a big battle -> PT / GI / reflections later. ATN also asks
        to DOUBLE-CHECK RT / PT for R.U.S.E. (WON'T GET was argued from stream output / ProcessVertices only): ru_ studies a
        D3D9 draw replay on the proxy's D3D11 device, the RTX Remix (dxvk-remix) route, forward-renderer shadow / AO
        placement, and what the B0 trace must answer. Owner: ru_ (resumed 20:4x)
      - [ ] ATN DECIDES (R.U.S.E.): keep R.U.S.E.'s own V31 rows or move to Attila's 15 v31_* rows
        (saved V31 looks would change)?; Hue Clip by eye (100). Experimental rows built, never run: Extended, Video AutoHDR,
        Wait for Each Frame Copy, the four Graphics 2 of 2 rows. Owner: ATN
      - [x] DONE in 8e80afae: R.U.S.E. U1-U7 (offline): U1 V35 contrast = the map's Gamma (postprocess.hlsli:161) + Extended (per-channel
        roll-off) as a mode; U2 test/tw_psychov35 part 6 (WARP: grey, slope, highlights, hue, C2 markers, ps_3_0 slots);
        U3 Output SDR / Auto + first start + SDR preset (coordinator owns the shared S1 / S2); U4 About / Ko-fi / fast::Use;
        U5 the 20 unreplaced UI / video / loading shaders (C1), Video AutoHDR Experimental; U6 D3D9 one-frame trace +
        test_hooks on the proxy runtime (S3); U7 dump_ru.cpp, parity_check 'ru', tw_install 'ruse'. Owner: ru_ (resumed)
      - [ ] R.U.S.E. C2: 15 marker / map-interface draws pre-undo the post grade (authored colour in SDR / Extended,
        shifted under V35): measured in U2, fix after. Owner: ru_
      - [x] DUPLICATE of "R.U.S.E. U1 run (after ReShade + the stamped add-on" (11-16): R.U.S.E. U8 / U1 run: ReShade + the stamped add-on into D:\...\R.U.S.E (coordinator, fresh backup), trace build,
        starts: ATN's settings, proxy waits off, ShaderQuality 1 / 2, UseHDR off, Option.ini above the menu's max; ATN
        loads the save and quits by hand. Owner: coordinator (next game window, ATN's OK)
      - [x] ATN ANSWERED 18:5x (R.U.S.E.): V35 with each map's contrast YES; Extended added beside the V31 modes (ATN asked
        what it is: the game's own response with a per-channel roll-off instead of its clip); D3D9 bridge GO, then a Fable
        5.2 High review of it, and the plan answers whether RT / PT and the whole package can work through it; Nexus
        ruse/mods/6; ReShade on D: by the coordinator (the C: copy is an empty stub: no old ini); values above the menu's
        maximum: everything the engine reads, coordinator / ru_ decide. Sent to ru_.
      - [x] DONE 11-16 (review done 02:4x, fixes e6e897ad): R.U.S.E. D3D9 bridge Fable 5.2 High review when ru_ finishes it (ATN asked). Owner: coordinator
      - [ ] ATN at the end (R.U.S.E.): Extended replacing V31 Native Tone or beside it; Hue Clip by eye (100 now). Owner: ATN
      - [x] Divinity 1 Nexus divinityoriginalsinenhancededition/mods/80 (sent to d1_).
    - [x] DONE 11-16 (runs done): ATN 17:4x: in-game data runs of R.U.S.E., Divinity 1 and Divinity 2 BEFORE 20:00 (the planners run them, one at
      a time through gpu_job.lock, no start after 19:30); from 20:00 ATN plays 4-6 h: OFFLINE ONLY until ATN says done.
      Owner: coordinator
    NIGHT ROUNDS 2026-11-15 from 21:0x (ATN 20:5x: "can we go and do the stuff from handoff now? (If you need seperate
    agents, go ahead)"). Common rules: tools/tw/round_notes/night_rounds_brief_1115.md; builds only through
    tools/tw/round_notes/gated_build.ps1 (one build at a time via tools\tw\build_job.lock, waits while Committed Bytes
    > 45 GB: MHR running, 8.7 GB RAM free at 20:5x). Agents commit on their branches; the coordinator merges one at a
    time, then the GPU harness, build all, stamp, install.
      - [x] d1_ MERGED 411a3dd5 02:1x: b2fb01c2 device_loss release_flip_chains_before_recreate (opt-in, off for other
        games; unbind + Flush when a flip chain goes) + 516388e9 Divinity opts in, first_start_key "tonemap_type";
        f92a1aca Peak clamp; 22f873f3 "Sun Shadow Softness (Experimental)"; 35a37ac4 test/tw_d1_post 17/17 vs the game's
        shaders + FXAA 0x35F39F1D FIXED (decompiler wrote Gather for GatherGreen: FXAA was broken for AntiAliasing 1
        players). tw_d1_swapchain PASS (control E_ACCESSDENIED -> S_OK), tw_device_loss 3c PASS, tw_divinity1 2c PASS; all
        12 device_loss consumers built. Divinity 1 step 1 swapchain crash fix + the rest of plan.md + Div1 R4 peak clamp + R13 tests.
      - CONTROL RUNS 01:49-01:5x (ATN, inside the Act 2/3 save, fullscreen -> fake fullscreen -> fullscreen): VANILLA no
        crash, RESHADE ONLY no crash, with our add-on CRASH (R1) -> the cause is ours (sent to d1f_).
      - [x] FIXED IN GAME 02:24-02:27 (d1 v2 stamped + installed, run_switchfix_022412): ATN switched fullscreen -> fake
        fullscreen -> fullscreen twice: no crash, picture visible, stays HDR. The safety net WAS needed: 0x80070005 twice,
        each time the game's own SDR chain for ~15 ms, then the next chain upgraded to HDR again; "let go before its next"
        at every recreation; no "cannot fall back". The game didn't quit on window close (60 s): ended by the script, no
        crash. ATN's files restored, 0 differ. MERGED fa089b0e 02:2x (d1f_ e1026a81): views unbound again at the window's next create + a safety net on DXGI's
        own IDXGIFactory::CreateSwapChain (slot 10, Divinity opt-in only): a refused flip upgrade retries as the game's own
        blt chain (SDR for that chain, logged). Root cause partly proven: the old flip chain still alive at the switch (most
        likely the game's own view of the old back buffer). tw_d1_swapchain + tw_device_loss 3d PASS x86 + amd64; 14 add-ons
        built. Re-test run started 02:2x (switch only; picture visible in fake fullscreen? HDR back after fullscreen?).
        Owner: coordinator. Divinity 1 window-switch crash: d1f_ (started 01:5x) finds what holds the old flip chain (view clones,
        ReShade refs), releases it at destroy_swapchain, adds an E_ACCESSDENIED safety net (retry as the game's blt chain),
        extends tw_d1_swapchain; then a vanilla control run (no ReShade) and a re-run of the switch with ATN. Owner: d1f_
      - [ ] Divinity 1 offline from R1's traces: R7 RGBA8-upgrade readers, census clamp candidates, torch / fire values,
        PCSS constants; SSREnabled 1 + a second mode cycle at the next run. Owner: next Divinity 1 agent
      - [ ] tw_install.py's include scanner counts includes inside an #ifndef the add-on defines (rt_scene.hpp via
        test_hooks.hpp:31 for Divinity 1) and refuses a current build. Owner: coordinator (tools)
      - [ ] tw_install.py: stamp only through it. Stamping a build in place made it refuse; the fix was to move the
        stamped bytes to build/stamped and truncate. Owner: coordinator (note)
      - [x] RAN 01:40-01:47 (d1 v1 stamped + installed, ATN loaded the Act 2/3 save; run_r1_014005): START CLEAN (9 swap
        chains S_OK, no E_ACCESSDENIED); FULLSCREEN -> FAKE FULLSCREEN STILL CRASHES at EoCApp+0xF7C532 (fresh factory,
        BufferCount 1 Windowed DISCARD upgraded to flip -> 0x80070005; ReShade "Reference count inconsistent (1)" at every
        release; our back-buffer view clone suspected). Captured: menu + save traces (torches), pass timing 3.185 ms GPU,
        7 mode shots, PresentMon 120 fps / 8.33 ms (CPU 8.30 = CPU-bound, paused). ATN's files restored, 0 differ.
        Divinity 1 R1 run (now possible): the start crash + fullscreen -> fake fullscreen switch with the fix; PCSS /
        0xCB4B3BA6 other branches' constants; R7 RGBA8-upgrade readers; 8 census clamp candidates; steps 9-13. Limit: a
        view of the old back buffer held by another add-on still fails (no blt fallback). ATN by eye: Hue Clip 70, Sun
        Shadow Softness, legacy V30 / Pragmap. Owner: coordinator (data run, ATN loads the save)
      - [x] DONE 11-16 (merged df26e42c): ru_ (resumed): R.U.S.E. whole package (plan.md 10b) WITH O1 object RT shadows + R4 SDR gamut + R13 final post
        test; H1 / H2 / T-run LEFT for after the game. Owner: ru_
        20:5x on branch ruse-whole-package (not built): 01029a12 R4 V31 gamut BT.709 on SDR; 618a347a R13 final post
        Vanilla exact on the SM3 interpreter (7/7, 7.8e-9; controls fail; found + fixed the vignette saturate); eaa334e1
        W1 resource log + D1 "Readable Scene Depth (Testing)" (INTZ). O1 = last frame's depth reprojected (plan.md 11).
        Sent back to write W2..P1 + H1-H3 now (it had stopped at the build gate). 21:1x ru_ out of context after
        3109a806 W2 / W3 / D2-D4 (bridge.hpp, DLAA (Experimental) restart row, Halton jitter in VS c0-c3; not built;
        tw_install 'ruse' ships the helper). ru2_ continues in the same worktree: H1 / H2 sources, D5 / D6 / gpu_fence,
        A1-A5 + H3; status in plan.md 11b. Then a part 3 agent: G1, G2, O1, AO, GI + water reflections, FG + Reflex, P1.
        ru2_ DONE (not built): 3e4ebcc4 D5 + W3 "Anti-Aliasing (Experimental)" Game's / DLAA / FSR / XeSS + GPU Sync
        row; 4bdf5df3 test/tw_ruse_bridge H1 + H2 sources; 9fa99a6b A2 275 VS -> vs_5_0 (275/275 SAME on a new SM3 VS
        interpreter); 5807314f A1 / A3 / A4 bridge_replay.hpp + "Scene Replay (Testing)"; H3 CPU 12/12. Open: A4 replays
        every draw (~10 ms CPU), D6 needs the T-run (MainRTDivider), moving units ghost (no object velocity).
        ru3_ started: static draws once, A5 rt_core host, G1 ground RT shadows, O1. Part 4 after it: G2, AO, GI + water
        reflections, FG + Reflex, P1.
        ru3_ DONE (not built; clang-cl /Zs syntax PASS): 7833bc4f static placement (B0: 1,783 of 2,223 draws placed
        once, 440 replayed); 1bce9c10 A5 rt_core additive API (InstallReplayedScene / CaptureReplayed / MarkDrawn,
        shared file); cfce94eb G1 "Ray-Traced Ground Shadows (Experimental)" (needs DLAA / FSR / XeSS on); bcd378c3
        utils/trace.hpp prints the first register (shared file). T-run checks for G1: sun sign, world units, acne, mask
        size, CPU waits. ru4_ started: O1, ground AO, G2 Bend, then water reflections + ground GI. Part 5: FG + Reflex + P1.
        ru4_ DONE (not built): 10868c65 O1 "Ray-Traced Object Shadows" (3 instructions into the game's bytecode, 642 / 644
        object PS SAME at visibility 1; s15; last frame's depth reprojected); 03158598 "Ray-Traced Ground Ambient
        Occlusion" (108 / 112 terrain PS; s14; 200 units = cm assumed); 3972cacf "Screen-Space Ground Shadows" (Bend via
        Attila's shaders, Apache line in About). 750 patched .cso (~1.8 MB) in object_shadows/ + ground_ambient/;
        ru_d3d9_validate accepts all 750. Not added: "Replace Shadow Maps" (proof open). ru5_ started: FG + Reflex, P1
        pages, GT-VBAO without rays, then water reflections + ground GI.
        ru5_ DONE (not built; clang-cl /Zs PASS): 35847418 FG + Reflex (frame_generation.hpp reused; HUD-less = the final
        post's FP16 clone; shared twattila/reflex.hpp skips non-D3D11 swap chains: rebuild D3D11 games before release);
        3032cc42 P1 pages (Simple / Advanced / Graphics 1 + 2 of 2); a934e61e "Screen-Space Ground Ambient Occlusion
        (Experimental)" GT-VBAO without the helper (WARP PASS). ru6_ started 00:1x: water reflections (259 PS read
        uniDefaultEnvMap once) + ground GI as Experimental rows. LEFT after: gated build fixes, H1 / H2 / H3 GPU, T-run,
        Fable 5.2 High review of bridge.hpp / bridge_replay.hpp / ray_tracing.hpp / rt_core replay API / reflex.hpp.
        ru6_ DONE 01:3x, BUILT (ruse amd64 0 errors at 01:07, tip 995b4183): 774ee417 + 7f0d1e82 "Water Reflections
        (Experimental)" Off / Screen-Space (120 water PS, 116 proved) + "Ground Light Bounce (Experimental)" (GT-VBGI, 108 /
        112 terrain PS); D3D9 runtime accepts 974 / 974 patched shaders; WARP mirror hits 16,186 / 16,186; S1 / S2 adopted
        (027e08d6 + 995b4183). Not possible yet: RT water reflections + RT GI (no G-buffer guides / albedo; route in 11b,
        M); lit objects' reflections need a combined O1 variant. ruh_ started 01:4x: H1 / H2 / H3 on the GPU.
      - [x] MERGED c76cb18b 02:3x (ru7_): both WON'T verdicts were wrong: RT GI needs no G-buffer (Light Bounce reads
        positions + last frame's light; the game's patched shader multiplies by its own albedo); only ReflectPixel needs
        guides: the game's own shaders write them as MRT (D3D9 caps: 4 targets, per-target formats / masks). Guide tables:
        terrain 100/112, objects 543/603, water 120/120; off = the game (interpreter worst 0; D3D9 runtime 1737/1737);
        WARP + GPU (H1 test 9: 36,864/36,864) PASS. Rows: Ground Light Bounce Off / Screen-Space / Ray-Traced /
        Path-Traced, Water Reflections Off / Screen-Space / Ray-Traced (traced states need Ray-Traced Ground Shadows), all
        Experimental off; RT/PT preset leaves the traced states off. Water writes no depth in 64 of 68 draws: its surface
        goes in through guide oC3. R.U.S.E. RT GI / PT GI + RT water reflections re-check (ATN 01:2x: "RT GI/PT GI would be a huge upgrade";
        doubts "needs a G-buffer"): guides via MRT from the patched game shaders, then rt_core light bounce / paths +
        ReflectPixel; Experimental rows; branch ruse-gi. Owner: ru7_ (started 01:2x)
      - [x] DONE a98b1871 02:0x (sk_ 6b854be3 + 67e7e62d, all 17 lessons placed; zip rebuilt: Downloads\RenoDX_skills_2026-11-15.zip,
        48 files). Skills update from tonight's 17 lessons (tools/tw/round_notes/skill_lessons_1116.md) + rebuilt skills zip.
      - [x] MERGED e6e897ad 03:3x (rrf_ c8d653b0..7f961437, 13 commits; H1 / H2 / H3 PASS after the fixes): 1 INTZ depths
        by the bound surface (init_resource_view); 2 WaitProxy waits only on S_FALSE; 3 old-size objects released at a D3D9
        Reset (destroy_swapchain); 4 no scene draws from the bloom to the present; 5 no jitter while the helper is lost;
        6 + 7 placements keyed by a serial in rt_core's keys; 8 deadlock REFUTED (filter added anyway: map / unmap /
        destroy for the D3D9 device's resources alone); 9 scene depth cleared at the present, INTZ depths erased with their
        views; 10 traced bounce takes GT-VBAO's occlusion when the rays' AO is off; 11 device filters; 12 skipped captures
        remembered. +1 the review missed: compute passes now bind their output first and unbind it after (Bend shaded 0
        pixels before; test at 20, 41-43 measured).
      - [x] DUPLICATE of "R.U.S.E. T-run list (ATN loads a battle" (11-16): R.U.S.E. rrf_ LEFT (T-run with ATN): #10 and the replay capture with the helper's rays in game; #3 on a real
        in-game Reset (display mode / resolution); the 16-frame static budget after a level load. Owner: coordinator
        (T-run, after the restart)
      - [x] FABLE 5.2 HIGH REVIEW OF THE R.U.S.E. BRIDGE DONE 02:4x (read-only, 12 findings; ATN asked): 1 BLOCKER
        IsSceneDraw never true in game (INTZ: container texture stored, level-0 surface bound; the harness hid it); 2 freeze
        after a GPU reset (WaitProxy spins on DEVICE_REMOVED); 3 no teardown on a D3D9 Reset (old-size textures / MRT);
        4 UI + final post jittered (counted as scene draws); 5 helper dies -> jitter stays; 6 rt_core short key ignores
        placements (instances never enter the BVH); 7 placement slots wrap; 8 PLAUSIBLE self-deadlock (mirrors lock + no API
        filter); 9 dangling scene_depth / intz_depths never pruned; 10 GT-VBAO unused under RT bounce; 11 device filters;
        12 perf retries. rrf_ fixed all (merged e6e897ad), then the T-run.
      - [ ] R.U.S.E. T-run list (ATN loads a battle, one row at a time with PresentMon): DLAA / FSR / XeSS in game, readable
        depth with in-game AA off, G1 sun sign / units / acne, O1 reprojection, AO range, Bend thickness, GT-VBAO darkening
        at the real pitch, water reflections + ground bounce (units: BOUNCE_RANGE 1000 = cm?, brightness of the gamma-space
        ambient sum, 3 SetRenderState a draw CPU cost, moving units a frame late, water draws in the replay blocking rays),
        FG 2x on PresentMon, Reflex, MainRTDivider, Option.ini 8192 / 4x, S2 SDR output + S3 test hooks; H2 with the helper
        on the GI inputs. 12 terrain + 60 object PS keep the game's ambient (no guide patch). Owner: coordinator (after the
        Fable review fixes)
        Merged 11-16 from "R.U.S.E. T-run (one agent data run": W1 resource log (pools, formats, samples), an AA-off
        frame (INTZ), small vs big battle draw counts, PresentMon CPU / GPU, is uniSunView the sun's projection alone
        (off-screen casters); from "R.U.S.E. rrf_ LEFT": #10 and the replay capture with the helper's rays in game, #3
        on a real in-game Reset (display mode / resolution), the 16-frame static budget after a level load. PROGRESS
        11-16: two runs done 04:48 / 04:52; the rest of the list open. Owner: coordinator (T-run with ATN)
      - [ ] R.U.S.E.: Ray Reconstruction for the rays (today DLAA preset K + our own denoisers; ATN noticed preset K in
        game, expected). Owner: ATN decides, then the game's next round
      - [ ] R.U.S.E.: test hooks (RENODX_TW_SET) for scripted runs. twattila/test_hooks.hpp includes rt_scene.hpp, which
        pulls Attila's light_bounce shaders and doesn't build in R.U.S.E. (reverted 03:2x; ini + restarts instead):
        split test_hooks from rt_scene. Owner: next tools pass
      - [ ] R.U.S.E. runs (notes): the game ignores window close in battle (two tries, then Stop-Process). Its
        screenshots: the HDR runtime (ReShade2.ini) takes them with F1, NOT Print Screen (that opens Windows' Snipping
        Tool). Owner: coordinator (notes for runs)
      - [x] ruh_ GPU TESTS PASS + MERGED df26e42c 02:2x: af61a7ae real bug (the bridge could never start: D3D9Ex opens
        D3D11-made shared textures only as RGBA16F / RGB10A2 / BGRA8; now D3D9Ex makes them, compute writes via D3D11 work
        textures + copy); H1 PASS (INTZ 1e-5, 10-bit 0 of 36,864 differ, jitter restored bit for bit), H2 PASS (DLSS / FSR
        3.1.6 / XeSS 2.0.2 24/24), GT-VBAO GPU = WARP (6.4e-7; flat ground 0.969 at 13.6 deg pitch: look note), FG hand-over
        300/300 (DLSS-G 2x in 7 of 11 runs: flaky, not R.U.S.E.'s), H3 39/39 WARP + GPU. Stop rule met. Fable 5.2 High
        review of the bridge started 02:2x (ATN asked). R.U.S.E. after ruh_: merge ruse-whole-package into atn-mods (stop rule: DLAA passes H1 + H2 first), Fable 5.2
        High review of the bridge (ATN asked), T-run with ATN loading the battle (water depth, world up, units, cube-map
        brightness, bounce double count, GPU cost, G1 / O1 / AO / Bend checks, FG / Reflex, MainRTDivider). Owner: coordinator
      - [x] sh_ MERGED c04da639 01:1x: S1 Config::first_start_key (default "tonemapper", every game unchanged); S2 Apply's
        SDR branch sets the proxy's target format / colour space; S3 test hooks answer only on the D3D11 proxy's runtime;
        tw_output_encoding x86 + amd64 ALL PASS (+ FirstStartKeys, ProxyTarget), new test/tw_test_hooks PASS. Divinity 2
        has nothing to adopt; the "tonemap_type" override is Divinity 1's (sent to d1_); R.U.S.E. adoption sent to ru6_.
        sh_: shared steps S1 (first-start key as a Config field, Div2 adopts) + S2 (device_proxy target format) + S3
        (test_hooks runtime filter); R.U.S.E. adopts later.
      - [x] DONE 11-16 (R.U.S.E. 995b4183; Divinity 1 first_start_key 516388e9; the R.U.S.E. test hooks can't be included there (rt_scene.hpp pulls Attila's shaders): own box "R.U.S.E.: test hooks (RENODX_TW_SET)"; the S2 / S3 in-game checks stay in the "R.U.S.E. T-run list" box): Adopt S1 / S2: R.U.S.E. (`.first_start_key = "rendering_mode"`, drop its own SetTargetFormat lines) and
        Divinity 1 (`.first_start_key = "tonemap_type"`, drop the override at addon.cpp:989-993); in-game check of S2 SDR
        output through the R.U.S.E. proxy and S3 SET / SHOT. Owner: ru6_ / d1_, then the R.U.S.E. T-run
      - [x] MERGED 02:4x (vf_ 047d1b06 Shogun 2 flare rotation, 3f51dfcc Rome R bloom01 dither, 61000221 run.ps1
        -Architecture): suite 172 EXACT / 32 R6 / 8 FAIL (only Attila / Thrones lensflare00-03, lg_ fixing) on amd64 AND x86
        (first x86 run, same verdicts). LEFT: Shogun 2 sun-flare sprites by eye in game. vf_ (started 02:0x): fix the Vanilla-exact FAILs: Shogun 2 flare00 / 02 / 03 sprite rotation (0.70-0.74), Rome
        Remastered bloom01 dither cast (0.0052); confirm Div1 FXAA EXACT after 35a37ac4; whole suite amd64 + x86. Owner: vf_
      - [x] ATN ANSWERED 02:1x: fix the Attila / Thrones lens flares (frame's own gamma, Vanilla exact); give Attila / Thrones
        the 2.2 Gamma Correction in HDR like the other TW games; keep the bloom smoothing (R6 accepted as intended); keep
        Gamma Correction on in the others. lg_ started 02:1x. (was) ATN DECIDES (Vanilla not exactly the game, by design today): Attila / Thrones lens flares decode 2.2 / encode 2.0
        in every mode (0.035); Gamma Correction on by default re-encodes 2.0 -> 2.2 in Rome II / WH1 / WH2 / Troy / Pharaoh
        Vanilla (0.027-0.035); the R6 bloom smoothing in SDR (up to 22 in WH2 t00). Make Vanilla = the game? Owner: ATN
      - [x] DONE 3d7c63ed 03:0x (sk2_ 6c22b57d, 11 lessons placed; zip rebuilt: Downloads\RenoDX_skills_2026-11-15.zip, 48
        files). Skills update 2 (tools/tw/round_notes/skill_lessons_1116b.md, 11 lessons) by sk2_, then rebuild the zip.
      - [x] SURVEY DONE 02:5x (all ReShade logs under tools, deduplicated): only Divinity 1 makes a second chain on the same
        live device and gets 0x80070005; no other game needs the opt-in on current evidence. Game flip already (not
        exposed): WH3, Troy, Pharaoh, Shogun 2, Rome Remastered, Expeditions, Divinity 2. blt -> flip but 1 chain per
        session seen: Attila, Thrones, WH1, WH2 (alt-tab = SetFullscreenState), Rome II (settings apply rebuilds the device
        cleanly), Tales (ResizeBuffers), Ryse (SetFullscreenState + ResizeBuffers). R.U.S.E.: the proxy's second flip chain
        hits "Flip swapchain already exists for this window, aborting modification" (no crash; SDR fallback possible).
      - [x] DONE 11-16 (no crash in 7 games, 04:4x; the PROGRESS note logs them at 03:4x, tools/tw/round_notes/winswitch_1116; no opt-in): Window-switch tests (after the restart, with ATN): Attila / Thrones / WH1 / WH2 options Windowed <-> Fullscreen +
        a resolution change; Tales Display Mode / resolution; Ryse r_FullscreenWindow + resolution; R.U.S.E. display mode +
        resolution (if the proxy falls back to SDR: release the old proxy chain in utils::device_proxy). Opt in only where
        one crashes. Owner: coordinator (data runs)
      - [x] DONE 11-16 (restarted 03:1x): PC RESTART (ATN 02:1x, after 39+ h): ping ATN once every agent has finished, everything is merged, pushed and
        HANDOFF is current (no running build / GPU job / game); installs after the restart. Owner: coordinator
        03:3x: lg_ / rrf_ / sk2_ / survey merged + pushed (e6e897ad), skills zip rebuilt, nothing running; ATN pinged.
        ATN 02:5x: only the RUNNING agents finish (lg_ lens flares + 2.2 gamma, rrf_ R.U.S.E. review fixes, sk2_ skills 2,
        the switch-fix survey); nothing new starts; merge them, rebuild the skills zip, push, then ping ATN.
      - [x] DONE 11-16 (5f63ed7c (g5_)): CORRECTION (lg_): Rome II / WH1 / WH2 / Troy / Pharaoh have NO Gamma Correction in HDR (row hidden in HDR, HDR
        shows the 2.0 frame linear), not as told to ATN 02:1x. ATN 03:1x "Yes, all five": the same 2.2 Gamma Correction in
        every mode for Rome II / WH1 / WH2 / Troy / Pharaoh, reusing twattila/frame_gamma.hlsli (TW_FRAME_GAMMA, the
        proxy's decode, flares / UI / video adds in the frame's gamma, TW_TONE_MAP_PEAK + EmulateFrameGamma), GC check in
        tw_vanilla_exact per game, psychov35 parts per game, build32 Rome II. Owner: an agent AFTER THE RESTART (step 1b)
      - [ ] Attila / Thrones / Rome II / WH1 / WH2 / Troy / Pharaoh / WH3 2.2 GC in HDR: ATN's in-game look (scene, UI, flares,
        video, FG HUD-less). Owner: ATN
      - [ ] AFTER THE RESTART, in order (nothing of this started before it):
        1. DONE (non-TW 03:2x: ruse u2, ryse fix57, tales v75, exr v33, d2 v2; TW install 05:0x: Attila v164, Rome II r7, Thrones r8, WH1 r9, WH2 v54, Troy v58, Pharaoh v71, WH3 v63, Shogun 2 v77, Rome Remastered v15, all on helper115): Build all (build32: Attila / Rome II / Thrones; build: the rest incl. Ryse / Tales / Expeditions / Div1 / Div2 /
           R.U.S.E.; the helper from atn-mods), stamp, install every game with tw_install (dry run, fresh backups, hashes):
           the newest merged state for ATN's Attila test.
        1b. DONE 5f63ed7c (g5_): The 2.2 Gamma Correction for Rome II / WH1 / WH2 / Troy / Pharaoh (ATN 03:1x yes), before step 1's build.
        2. PARTLY (two T-runs done 04:48 / 04:52; the rest of the T-run list open): R.U.S.E.: H1-H3 already PASS on e6e897ad (rrf_); the T-run with ATN loading a battle
           (list in the "R.U.S.E. T-run list" box).
        3. DONE (no crash in 7 games, no opt-in; Ryse windowed fix58, ATN: fine 04:2x): Window-switch fix for other games per the survey's table (opt-ins + tests), if any.
        4. OPEN (the "Radiance cache (under 0.23 ms" box): GPU round 2: the radiance cache item (f); after the install a fresh Nsight capture of Attila at ATN's rows.
        5. RUNS DONE (Divinity 1 r2ssr 03:21, Ryse r1rays 03:46, Tales 03:58, Expeditions 04:10, Divinity 2 r2gamma 04:25); LEFT: an_'s analysis + the Ryse / Tales redo: Data runs with ATN: Ryse (shadow-map skip Off / Near / All + PT ambient look, new rows), Tales / Expeditions
           (Bend + GT-VBGI look -> defaults on, E9 -d3d12, Low start), Divinity 2 (gamma 1.2, ortho camera, Higher
           Precision Scene A/B), Divinity 1 (SSREnabled, second mode cycle, R7 offline).
        6. OPEN (ATN): ATN's in-game checks list: Near ReSTIR, HDR bug fixes (SDR colours, Shogun 2 highlights + flares, WH2 FXAA, DOF),
           Divinity 2 V35 look (press Recommended), Attila / Thrones 2.2 gamma.
        Owner: coordinator
        PROGRESS 11-16 (after the restart 03:1x; ATN: "do every single thing that is left", ATN-present runs first, then
        bed): page file max 76800 MB active. Steam came up elevated (logon task): -shutdown / -silent, non-elevated.
        03:2x 1b started: g5_ agent (worktree) on the five games' 2.2 GC. Built + stamped + installed (tw_install, fresh
        backups, hashes): ruse u2 (--first: helper115 + DLLs), ryse fix57, tales v75, exr v33 (helper115 each), d2 v2;
        helper115 = atn-mods' helper (gr_ + Near ReSTIR). Divinity 1 run r2ssr 03:21-03:30 (d1 v2, SSREnabled 1 via
        d1_ssr.py, two mode cycles in ATN's save, 14 shots, PresentMon 105 fps / GPU busy 2.92 ms, Time Each Pass 2.947 ms,
        closed clean, restore 0 differ): tools/nontw/divinity1/r1/run_r2ssr_032119. Run scripts ready: ryse r0/r1_rays.ps1
        (skip Off / Near / All, PT ambient Off / On, no rays; hooks via run.ps1 -Hooks), tales g1g2 + expeditions g1 run.ps1
        -Bend (game / Bend / Bend+GI / GI), divinity2 r1b run.ps1 -Ini / -CycleStart. ATN doing the window-switch tests by
        hand (Attila, Thrones, WH1, WH2, Tales, Ryse, R.U.S.E.). R.U.S.E. test hooks: twattila/test_hooks.hpp can't be
        included there (rt_scene.hpp pulls Attila's light_bounce shaders): reverted, ini + restarts instead.
        03:4x WINDOW-SWITCH TESTS (ATN by hand, menu, fullscreen <-> windowed / borderless + a resolution change): NO CRASH
        in Attila, Thrones, WH1, WH2, Tales, Ryse, R.U.S.E. Logs: tools/tw/round_notes/winswitch_1116 (no 0x80070005; R.U.S.E.
        5x "Flip swapchain already exists" for the proxy's new chain at each Reset, SetColorSpace1(1) after: ATN to confirm
        HDR). No opt-in needed. Ryse windowed: the window shrank 0.85x every 2 s (render scale applied to the back buffer
        the game then sizes the window to): ryw_ fix d52aa32e merged 6e1c247b (windowed = the game's own size, scale only
        fullscreen / borderless; window model 145 PASS), ryse fix58 installed; ATN re-checks windowed. ATN reset Attila's
        Graphics 2 of 2 by mistake: ReShade.ini restored from backups/20261115_134847 (unchanged since 11-14 18:07; reset
        copy in winswitch_1116/attila_ReShade.ini.after_reset).
        Ryse run r0/run_r1rays_034623 (ATN's save, 3 cycles, 18 shots): every change applied; Ryse runs at its 60 fps cap so
        Time Each Pass totals stay ~16.8 ms (the file is overwritten: only the last state kept). REDO the per-state GPU cost
        with PresentMon GPU busy + per-state timing copies (overnight, keys load). Owner: coordinator
        Tales run g1g2/run_035823 (ATN's save outside the town; rays off): GPU busy game 4.80 / Bend 5.10 / Bend+GT-VBGI 5.40
        / GT-VBGI 5.09 ms; NO shots (schedule had none; fixed: each segment shoots the one before). ATN's new save
        SaveData_006 (made during the run, moved aside) put back. REDO the look shots overnight (keys). Owner: coordinator
        Expeditions run g1/run_epic_041035 (menu scene, ATN's wish: heavier): GPU busy 8.34 / Bend 9.23 / Bend+GI 9.72 /
        GI 8.84 / back 8.35 ms; 5 shots, traces + timings per state.
        04:25-04:57 Divinity 2 r2gamma / r2precision (gamma 1.2, Higher Precision Scene; 10 shots each, trace at 300 s in the
        second), R.U.S.E. t1rt (all RT rows: 137 fps, GPU busy 4.65 ms in RUSE.exe) / t2ss (screen-space: 138 fps, 4.63 ms;
        HDR shots view\hdr_*.png). ATN done 04:57, in bed. 05:0x TW install (10 games, helper115), skills c4ebedb8 / 367b9d4d /
        1ed75008 (ours replaces the game's, one method per effect, Bend keeps the maps), audits started: rao_ (RT AO replace),
        sk3_ (Attila vs skills), an_ (run analysis), rd_ (Ryse cost + Tales shots redo), 10 per-game skill audits
        (tools/tw/round_notes/skill_audit_1116), gpu2_ (GPU round 2 + Nsight). handoff_index.py no longer adds a blank line.
        LEFT (11-16 audit): step 2 (the rest of the "R.U.S.E. T-run list" box), step 4 (GPU round 2 + the Nsight
        capture), step 5's analysis (an_) and redo (own box "Ryse redo: per-state GPU cost"), step 6 (ATN's checks).
        Owner: coordinator (steps 2, 4, 5), ATN (step 6)
      - [ ] Attila has no frame trace `on_demand` / `at_100` (Pharaoh, Rome II, WH3 have both; Thrones, Troy, WH1, WH2, Rome
        Remastered, Divinity 1 have on_demand): port to the reference. Owner: next Attila round
      - [x] STALE (sk4_ + the Ryse / Tales audits 11-16: all three have it since 89d250f1; only test/tw_output_encoding has no
        Ryse case: in the SHARED STEP list). Ryse / Tales / Expeditions lack Output Auto's first-start path + the HDR-on / off preset switch
        (twattila/output_encoding.hpp, test/tw_output_encoding). Owner: next non-TW round
      - [ ] grass_fade.hpp:36: unbound cb8 reads 0 = no fade, against the skill's "unbound is the game's" (100 m fade).
        Owner: next Attila round
      - [ ] SKILL AUDIT 11-16 (ATN: "double check that every game of ours hasn't missed any of the skill sets"): reports per
        game in tools/tw/round_notes/skill_audit_1116/<game>.md (attila, thrones, rome2, wh1, wh2, wh3, troy, pharaoh,
        shogun2, romer, ryse, tales, expeditions, divinity1, divinity2, ruse); each table row is A (offline) / B (ATN in
        game) / C (ATN decides) / W. Fix rounds after rao_ merges: one SHARED STEP agent (twattila / talesofarise_dlss /
        utils) + one agent per game folder, every A row, each row a commit; B / C rows go to OPEN FOR ATN. Divinity started
        early (dvf_, no rao_ overlap). Owner: coordinator
        06:xx rao_ merged ad541a4f (RT AO replaces the game's AO in 10 games, AO gates; Ryse in its round). Fix round started
        (brief tools/tw/round_notes/fix_round_1116/brief.md): shs_ (shared), Attila + Thrones, Rome II, WH1 + WH2, WH3, Troy +
        Pharaoh, Shogun 2 + Rome Remastered, Ryse, Tales + Expeditions, R.U.S.E.; dvf_ (Divinity) earlier. Merge one at a time.
        - [x] Attila + Thrones MERGED 1f5f02e94 (ath_, 10 commits): Thrones Time Each Pass, alpha clamp, battlefog01 MSAA,
          sky_deband off in its presets, video rows HDR-only; Attila + Thrones ssr00 / ssr01 one occlusion under RT Specular
          Occlusion (tw_thrones_ssr part F 0 differ, control 27686 / 7168); tw_vanilla_exact 60 EXACT / 8 accepted / 0 FAIL
          (x86 + amd64); tw_psychov35 part 4 on Thrones (Hue Clip 40 = minimum, 6.31 deg), part 8 (V35 toe: darks
          x0.996-1.000, chroma 30-150 nits x1.055), default -Before now 4b5099035. Not built on atn-mods yet (rebuild all).
        - [x] Divinity 1 + 2 MERGED b27e5eb02 (dvf_, 13 commits; amd64 builds exit 0 on its branch): D1 credits + tooltips,
          GradeAfterCurve C-infinity shoulder from 0.8 (tw_divinity1 test 4), one shared twattila/psycho_test30.hlsli
          (tw_psychov30_nan: the NaN guards work), D1 spot lights 0x23B84721 / 0x7CBAA94E follow the light rows +
          LightVolume SMAA trio (tw_d1_post 22 exact), D2 device_loss + Time Each Pass (output_encoding.hpp: no-op when
          never configured; tw_output_encoding ALL PASS), D2 Sun Shadow Softness Experimental on 8 CascadedPCSS variants
          (tw_d2_passes: 0 at 1, control 0.006), page-1 Gamma Correction lines (D1 exe read: proof B), preset tooltips.
        - [ ] dvf_ not done: VFX / indicator Experimental rows in both games (D1 633 FX_MAT and D2 2,050 RS3_FX permutations,
          no few shaders to replace: scope is ATN's call); Modded Auto Exposure (plain 4x4-average chain to 1x1, needs the
          last frame's average bound into the first downsample, size M, one run); D1 PsychoV35 toe / saturation fit; exact
          tests for D2 bloom00 / ui00 / outlines; B: D1 SSR proof, D2 SDR path, SupportTool dumps (Larian's, not ours); C:
          hue shift after V30, Vanilla Calibrate curve-on-curve, UI sRGB decode. Next run: D2 Sun Shadow Softness at PCSS,
          page-1 lines, Time Each Pass file; D1 "applied" log lines for LightVolume / AA. Owner: next Divinity round / runs,
          ATN (VFX scope, C rows)
        - [x] Tales + Expeditions MERGED 4346ab2f4 (tex_, 16 commits; amd64 builds exit 0 on its branch): vendor fallback,
          Tales SSGI chain skipped under GT-VBGI / RT / PT (readers proven in 18 traces, 8 unproven readers turn it back on
          for 30 frames, ~0.07 ms), Expeditions r.SSGI.Quality 0 under GT-VBGI / bounce, Bend + GT-VBGI on by default (ATN),
          no per-frame counters, Tales LOD bias follows DrawScale, credits, page-1 lines (Tales Brightness 50, Expeditions
          Brightness 40 = DisplayGamma 1), Expeditions Two Shadow Cascades Under Rays (Experimental off; cascades 2-3 =
          34-55 % of the atlas indices, atlas 0.47-0.84 ms), tw_ue_patterns (23/23 + 4/4, controls fail). Tests:
          tw_vanilla_exact 40 EXACT, tw_ue_sss_gi PASS both, tw_psychov35 PASS.
        - [ ] tex_ not done: Expeditions Video AutoHDR (NV12 convert ps 0x524CDCDF; needs one trace while
          Rome_MainMenuLoading.mp4 plays); Expeditions Modded Auto Exposure (eye adaptation ps 323FECA6, port + WARP); VFX /
          indicator / sky lines (traces named in the ledgers); from the main round: V35 fit, Path-Traced Ambient,
          vanilla_exact widening, R11G11B10 / LOD-bias censuses, one occlusion (192686A0 t1), double-work table, level-B
          twins, 6 New_1 permutations, WARP of C38C91CD vs rt_sun texel 2, CSM caster proof; release_zip.py Detours licence
          + a shared credits template (coordinator, release round); shared rows (WriteSun sun light, constant_mirror LOD bias
          replaces c133.x instead of adding, frame_trace poll / name / slots, post_filters comment) sent to shs_. Owner: next
          UE round, coordinator (release), shs_
        - [x] WH3 MERGED f357bba3f (w3f_, 10 commits; amd64 build OK on the merged tree): Fog Shadows / Spec Occlusion /
          Caustics default 0 (revision 34); Bend and rays over kept maps turn the game's contact march off (b3 copy, byte 28
          = 0; part 4: 0 apart, CONTROL 4491 px); the 4 shadowed point-light PS run their unshadowed twins under the rays,
          one shadow per light (part 5: 0 apart, CONTROL 20850+; the 4 shadowed copies deleted; the game's bytes without
          rays); Recommended sky_deband 0; Reset writes the 4 VFX rows; reflection00 one occlusion (part 6); 2.2 gamma in
          every mode from t00's g_gamma_output; psychov35 part 8. Tests: tw_warhammer3_shaders 1-6 PASS, tw_vanilla_exact
          WH3 6 EXACT, psychov35 amd64 + x86 PASS, parity_check wh3 open 0.
        - [ ] w3f_ not done: vanilla_exact over every WH3 shader + a lensflare00 control (~20 shaders need Vanilla bindings,
          own round); the game's point shadow maps are still drawn (fog / portraits / forward passes read them: cost);
          Grass RT range (row 16, boxed in the 17-row leftovers). Shared rows (contact_shadows_off route, per-light traced
          flag, RAY_METHODS, frame_gamma comment, DLSS scaled decode 2.2 vs WH3's 2.0) sent to shs_. Owner: next WH3 round,
          shs_
        - [x] Shogun 2 + Rome Remastered MERGED 0bf6d320c (s2r_, 24 commits; amd64 builds OK; vanilla_exact conflict
          resolved keeping both tables): vendor fallback (both), Time Each Pass (both), credits; Shogun 2 one occlusion per
          term (resolve t41 in place, t46 x open environment, no AO on the resolve hit; GT-VBAO off under PT AO; WARP 0 apart,
          controls caught), RT/PT on the shared RAYS_PRESET + 128 torches, Reflex / limit gates, Torch Shadows Follow the
          Camera (Exp off), Lean ReSTIR default on, SER tooltip, Rays Replace Shadow Maps (Exp off; t51 0/1024 apart),
          tw_shogun2_tweaks part 4 (12 patterns once, CONTROL 0); Rome Remastered FSR / XeSS through the helper, test hooks,
          Bend off where the rays shadowed. Tests: tw_shogun2_shaders / tweaks, tw_romer_dlaa / rays PASS; tw_vanilla_exact
          45 EXACT (Shogun2 7, Shogun2PS 17, RomeR 15, RomeRPS 6). dump_rr.cpp now dumps AA {0,5,6,7} (coordinator).
        - [ ] s2r_ not done: Rome Remastered Modded Auto Exposure (Feral's AE compute not identified: trace 40, port, WARP);
          skip Shogun 2's own line-sweep SSAO under the rays' AO (reader census first); Rome Remastered rays replacing the
          cascades + texture LOD bias proof (NEEDS A TRACE, reader census); Shogun 2 old AO path + summed-area DOF blurs
          (NEEDS A TRACE); PsychoV35 toe / settings matrix / scale census (harness); vanilla_exact 8 open (highlight00 /
          ribbon00 inf, ssrcopy00 0.79: render-scale constants first; treevelocity00-03 by design; particlelight00,
          pointlight00 / 01, vfxglow00 control missed); smoke shadow map WON'T (shogun2.tsv:38 / 113), Rome Remastered WON'T
          rows and Gamma Correction row: second look. Owner: next Shogun 2 / Rome Remastered rounds and data runs
        - [x] GPU ROUND 2 MERGED aa295fd32 (gpu2_; 4K harness, ATN's rows, Nsight base clocks): radiance cache fill by wave
          (0.583 -> 0.462 ms), reflections without spills (0.701 -> 0.630, local memory 1.14 GB -> 0); whole helper 5.49 ->
          5.33 ms; HARNESS_DUMP 43 modes / 264 outputs 0 differ. Helper changed: the next install needs a new helper number.
        - [ ] gpu2_ next wins: (1) paths at 128 registers (split trace / shade, or SASS liveness; est. 0.2-0.3 ms); (2) reuse
          near L2-bound 95.7 % (pack reservoirs / group-shared tile, ~0.2); (3) fill latency-bound (strided loads; harness
          can't set RT_PATH_SUN_LIGHT: harness switch); (4) reflect_filter 0.36 ms L2 87 % (group-shared tile, ~0.1); (6) c3
          only at REFLECT_ONE_SAMPLE (~-0.03, unproven); (7) trace / lights / fog RT-core bound (fewer rays = look risk,
          numbers for ATN); (8) one more copy (0.16 ms); (10) ns_trace_run cleanup misses an exiting helper (empty Path),
          one lingered ~20 min. Files: tools\tw\nsight runs gpu2_ns_*, scratchpad gpu2_c3_files. Owner: next GPU round,
          next ReSTIR round, tools owner
        - [x] SHARED STEP MERGED ad7edf1b2 (shs_, 26 commits; build32 Attila / Rome II / Thrones + 13 amd64 exit 0 on its
          branch): Fog Shadows + RT Specular Occlusion in RAY_METHODS; helper idle release = info; Lean ReSTIR and trilinear
          LOD bias on by default (ATN 06:xx); Auto Thickness off by default (it was labelled Experimental); pass_timing one
          call Install(prefix) + Button(section); the game's AO blurs skipped only under PT ambient alone (tw_ao_blurs: white
          stays 1.0, control 8192 / 8192; cost blur_x 0.022 + blur_y 0.047 ms); credits::Methods + Detours line; OnNvidia =
          vendor 0x10DE and reflex reads the vendor everywhere; tw_graphics_hooks_patterns (Attila 9/9, Thrones 9/9, Rome II
          3/3 once, controls 0); tw_test_hooks builds again; Characters Ray Tracing Range for scene_draw hosts
          (Expeditions); Tales RENODX_TW_NO_DXR12 / RENODX_TW_TEST_REMOVAL; constant mirror counts only while Details shows;
          LOD bias added to c133.x (tw_view_mip_bias, control fails 4); WriteSun sun light + RT_PATH_SUN_LIGHT; frame trace
          <game>_frame_trace.txt + change notification + t0-t127 / declared slots / VBs; RtLocalLightTraced.
          Follow-up 620749d6c: Shogun 2 / Rome Remastered on the new pass_timing API, Shogun 2 trilinear on (coordinator).
        - [x] Ryse MERGED 6be38b3c3 (ryf_, 18 commits; amd64 OK): ComPtr fix (static_checks clean), shadow skip through the
          command_action bypass (11 x 837 draws bypassed; the old skip removed none), HUD / highlight / skull hotkeys same
          trap, RT AO in place of SSDO's amount, specular occlusion once, r_SSII off under Light Bounce, video AutoHDR off at
          SDR output (tw_ryse_video 0), vendor fallback, Reflex with any AA + limit gates, Lean on (RAYS_RESET), Characters
          RT Range (Exp off), tw_ryse_patterns 18/18, credits, AA Off without per-frame jitter diagnostics, README fix59.
        - [x] DONE daa56e6a7 (csp_, 8 commits; every add-on built, page dumps: only About rows differ; largest gate use 4 of 8
          (Ryse), a full table now logs a warning, tw_draw_dispatch case 10; DLSS_ADDON -> NGX_HEADER; run scripts read
          <game>_frame_trace.txt; page_dump drivers dump_ta / dump_exr / page_dump_ry_body.h fixed for the renamed rows,
          backups *.bak_csp). CALL-SITE PASS (csp_, running): credits::Methods in Thrones / WH3 / Rome Remastered / Shogun 2 / Ryse / Tales /
          Expeditions; credits::DLSS_ADDON stale; fast MAX_GATES 8 headroom (Ryse adds 2) with a check; run scripts read
          <game>_frame_trace.txt. Rome II / WH1 / WH2 / Troy / Pharaoh / R.U.S.E. told to do Methods + pass_timing in their
          rounds. Owner: csp_, per-game agents
        - [ ] Shared leftovers (shs_ / ryf_): rt_core OnAnyDraw replays every draw while traced, defeating raw draw-event
          skips in every rt_scene host (audit each host's raw skips + CPU cost); WH3's DLSS scaled prepare / finish and
          rr_extra_scaled decode at 2.2 while t00 writes TW_FRAME_GAMMA 2.0 with GC off (exact round trip, wrong domain: a
          frame-gamma float in their b0 + WARP); contact_shadows_off sharing WH3's b3 / byte-28 route; WH3 shadowed point
          lights back with RtLocalLightTraced (keep the map where a light isn't traced); WriteSun no effect in Expeditions
          (no PT ambient there) + the C38C91CD WARP; MSAA + DLSS + PT ambient edge (white AO without paths); Detours licence
          file missing from the release zips. Not run in game: Characters range at 50 / 100 m, Tales switch runs, a frame
          trace with the new code, Tales' own c133.x. Owner: next shared round, next WH3 round, release round
        - [ ] ryf_ not done: row 9 e_GsmLodsNum under the rays' sun (trace: which lod is the cached far map, what a live 3 / 2
          draws); LOD bias at AA Off below 100 % + main-view gate (without the gate shadow-map leaf holes sharpen);
          Advanced's live counters + back_buffer_fix's per-bind view lookup; vanilla-exact widening, reader census, V35 fit.
          B in game: R2 cost rerun for the skip, HUD hotkeys with RT on, r_SSII read-back 0 in the log, RENODX_TW_FAKE_VENDOR
          8086 / 1002, Reflex with AA Off, Characters range at 50 / 100 m. Owner: next Ryse round / data run
        - [x] Troy + Pharaoh MERGED ed7f9fb63 (tpf_, 22 commits; amd64 both, static_checks 0): Rays Replace Shadow Maps
          on by default (proven offline: P1-P3 0 of 2,752,512, controls off; ATN's replace rule); Troy RR guides after the
          water (WARP 0 apart, CONTROL 43,008 / 43,008); Sky Debanding 0 in every preset (Pharaoh SettingsRevision 4); the
          standard labels; tw_pharaoh part 7 dead_units pattern once, CONTROL none; test switches only via
          renodx_test_hooks.txt; vanilla_exact 42 EXACT / 12 accepted / 0 FAIL; pass_timing + credits::Methods (+ smaa).
          AA domain shared patch applied 7150cd9cd (CMAA2 29.7x -> 0.0011, SMAA edges 548 -> 0; Attila's path unchanged).
        - [ ] tpf_ not done: Torch / Fire Shadows on the 7 other point-light PS per game (14 WARPs; DO NOW in both ledgers);
          Pharaoh Filmgrain c9.z (next data run); Troy fxaa00 (0.062) and menuvideo01 (0.037) differ in Vanilla, causes not
          read; tw_troy_shaders not run (no -OutDir; nothing it tests changed). Owner: next Troy / Pharaoh round, data run
        - [x] WH1 + WH2 MERGED 2e2e74f63 (w12_, 25 commits; amd64 both exit 0): Reset sky_deband off, credits::Methods (+
          smaa fb6224f60), Time Each Pass (one-call API), GT-VBAO skips the game's sweep / collect (WH1 part 26 0 of 3072,
          WH2 sweep 4D2C2BF5), WH1 Smoke Shadows without rays + Smooth Half-Size Particles (both Experimental off), WH2
          cheap_fog_alone, WH2 Skip the Game's Reflection Passes under the rays (on: hi-Z + mip chains, part 27 0 of 4096,
          controls 4096), WH2 RT Water Reflections (Experimental off; RT/PT on), WH2 one occlusion per reflection (part 28),
          WH2 Modded Auto Exposure (Experimental off; off = the game's bits), gamma line first, video rows HDR-only, one
          psycho_test30 (0 of 87 shaders change bytes). Tests: tw_warhammer_shaders 1-28 PASS, vanilla_exact WH1 / WH2 34
          EXACT / 7 accepted / 0 FAIL, GC 24 / 0 failed.
        - [ ] w12_ not done: Rays Replace Shadow Maps as default (no WARP proof of the replace path yet, DO NOW both);
          WH1 ssr00 one occlusion (the resolve binds no AO and adds AO + t46 - 1: needs the sun's AO at the resolve, DO NOW);
          psychov35 part 6 for WH t00 (needs a WH driver); vanilla_exact can't check cinematics (WH1 0.035 by design, WH2
          zero output), FXAA (alpha 1), sharpening / pointlight00 (zero output); psycho_test31 copies differ (a merge changes
          the look); WH2 depth downsample F1CE6943 kept (an untraced blur may read it); GI + contact-shadow replace audit not
          done; Grass RT range for dynamic grass (shared, boxed). Owner: next WH1 / WH2 round
        - [x] R.U.S.E. MERGED 5c43d8f95 (rux_, 26 commits; amd64 PASS): the AA row writes Option.ini MainRTAA 1 (1x) so the
          bridge runs (a multisampled start keeps it off with one log line and a red row); status lines on change (<= 9 a
          start); RT ground / object shadows replace the map term (cmp; 1,185 patched shaders proven, D3D9 NULLREF 1185 /
          1185, refused control); Bend off under the rays; PT bounce without O1; AO rays + GT-VBAO skipped under PT ambient;
          Near + Lean ReSTIR with PT; credits::Methods; Recommended Video AutoHDR 0.2; ComPtr fix (1x1 textures were rebuilt
          every frame); vendor fallback (DLAA on NVIDIA only); Reflex NVIDIA-only; Units RT Range (max 600 m, 16-bit);
          replay re-uploads call OnIndexWrite (tw_cluster_rewrite case 8, control "marked 0"); mirror width fix (buffers not a
          multiple of 16 B were recreated every upload); test hooks. Tests: psychov35 part 6 (V35 = SDR exactly), ru_c1_verify,
          ru_post_vanilla 7 / 7, page dump, static_checks 0. dump_any.py now links MinHook + d3d9 (coordinator; backup
          dump_any.py.bak_minhook).
        - [ ] rux_ not done: RT debug view, moving units under DLAA, combined O1 variant, Time Each Pass (D3D9 queries); shared:
          utils/device_proxy has no removed-device path (+ WARP test; ledger row reopened). T-run checks (with ATN or a
          click-loaded Continue): MainRTAA 1x against an all-off start; is MainRTDivider a render scale (then D6 + LOD bias);
          Gamma Correction (U1 box); shadow-map pass skip (off-screen casters / imposters); Units range zoomed out (is a world
          unit a centimetre?). Owner: next R.U.S.E. round, shared round, coordinator (T-run)
        - [x] Rome II MERGED f5f1ba0b2 (r2fix_, 25 commits; build32 OK; page dump 489 rows, no key lost): Bend off under RT
          sun shadows, torch hook on one atomic, DllMain TRUE, tooltips, credits::Methods (+ smaa), Time Each Pass, campaign
          frames counted, smoke 7 -> 14 PS / water 3 -> 17 + shadow readers 3 -> 15 / point lights 2 -> 26 (WARP all pass;
          lights 14 bit-identical, rest <= 2.4e-7, controls 41k-57k), Modded Auto Exposure in stops (x0.30103, 1-8, default
          4, saved x log2(10) at revision 3), indicator HDR-only (SDR = game bytes), plain Recommended for Output Auto, preset
          tooltips, page-1 order + video rows HDR-only, twrome2/shadow_cascades.hpp + tw_unit_shadows part 8, AO blurs
          {41894C1F, 7DDD84A3}, vanilla_exact 21 -> 57 (53 EXACT, 4 accepted, 0 FAIL).
        - [ ] r2fix_ not done: cut-outs (cut_out_shader capture versions first); Batched Unit Capture (no Rome II bone pack
          hook; its row shows default 1 but does nothing: hide it); shared: a quad shader per SmokePixelShader for
          fire_smoke_loop, a shadow patch route for the 18 SM3 water readers, unit patterns in the shared once-count test;
          8 NEEDS A TRACE ledger rows (rome2.tsv) for a data run; ao-gi.md double-work rows (SSAO under RT AO gone, maps
          under rays Multiply, Bend under rays off; costs need a Time Each Pass battle). B in game: Motion Debug View on the
          campaign (scale 0.05), LOD bias log at 50 %, PT ambient blurs, auto exposure look at 1.33, level-B ports at SM3 /
          SM4.1. Owner: next Rome II round, shared round, data run
        - [ ] ath_ not done: unclear-sun WARP 2x test (B6DA03C5 / 1B10DDD8), traces of 0FB73442 / D9123DE1, vanilla coverage
          of pointlight00-39 / UI sprites / grass00-01 (the generic driver can't feed them); view_share Classify 24 unclear
          sun-PS rows a game (NEEDS A TRACE). The t46 x AO double-occlusion check was sent to WH3, WH1 / WH2, Troy / Pharaoh,
          Rome II and Shogun 2 agents. Owner: next Attila round / data runs, per-game agents (t46)
        - [x] 17-ROW CHECK non-TW DONE (ryse / tales / expeditions / divinity1 / divinity2 / ruse _17.md): A rows sent to the
          Ryse, Tales + Expeditions, R.U.S.E., Divinity agents and shs_ (Units Range for scene_draw hosts, Tales NO_DXR12 /
          TEST_DEVICE_REMOVAL switches, rt_core index-write event for R.U.S.E.'s replay if needed).
        - [ ] Non-TW 17-row leftovers: Tales' path tracing always uses SER on DXR 1.2 (rt_shadows.hpp:1113-1125,
          rt_path.hlsli:39-48; Attila measured SER slower at every size): a cs_6_9 compute path with micromaps, SER an
          Experimental row after an Nsight A/B; torch lights in Ryse / Tales / Expeditions (ryse.tsv:47, tales.tsv:44-45,
          expeditions.tsv:59, traces); R.U.S.E. gamma slider, Divinity 1 / 2 Bink videos, Ryse VFX / sky, Tales VFX / sky /
          auto exposure, Tales smaa00 input encoding (traces). Owner: next GPU round (SER), next data runs (rest)
      - [ ] From sk4_ (skills update 3, 1e75898a / ec000d46 / 47a73741, zip 48 files): Pharaoh's and Troy's SMAA / CMAA2
        wrappers decode Attila's gamma 2.0 while their frame is linear there (wrong domain? WARP check); the vendor fallback
        (saved DLSS -> XeSS on Intel, else FSR) is missing in Rome Remastered, Shogun 2, Ryse, Tales, Expeditions;
        tools/tw/page_dump/README.txt still says non-TW add-ons have no driver (dump_ry / dump_ta / dump_exr exist).
        Owner: per-game rounds (SMAA / CMAA2, fallback), coordinator (README)
      - [ ] 17-ROW CHECK (ATN 06:xx: the first audits ran before the 17 Attila rows were in the skills): 3 read-only agents,
        reports skill_audit_1116/<game>_17.md; their A rows go to a second fix pass. Owner: coordinator
        - [x] Group 1 DONE (thrones / rome2 / wh1 / wh2 _17.md): A rows sent to the running fix agents (Rome II Modded
          Auto Exposure in decades -> stops x0.30103, indicator HDR-only, a plain Recommended, preset tooltips, cascades
          pattern test; WH1 preset tooltips; Thrones sky_deband 0 in Reset; Rome II / WH1 / WH2 gamma line above the
          download note; all four IsHdrOutput on the video rows; shared: graphics_hooks pattern count test to shs_). Check
          each agent's report for them.
        - [ ] Group 1 leftovers: Grass Ray Tracing Range for grass captured as drawn (Rome II, WH1, WH2: drop far vertices
          in the capture, Nsight cost first; shared code); VFX Brightness families need traces (Thrones snow / storm
          thrones.tsv:10, :51; WH1 wh1.tsv:73; WH2 wh2.tsv:36). Owner: next GPU round (grass range), next data runs (VFX)
        - [ ] ATN DECIDES: UI 4K Mod Compatibility for Thrones and Rome II, only if a UI mod for those games patches the
          same DLL code (Thrones writes Shadow Map Size and the cascades with no lock, addon.cpp:1705). Owner: ATN
        - [x] Group 2 DONE (wh3 / troy / pharaoh / shogun2 / romer _17.md): A rows sent to the running fix agents (WH3
          sky_deband 0 + Reset VFX rows; Troy indicator label; Pharaoh sky_deband 0 in presets + dead_units pattern test;
          Shogun 2 Reflex / limit gates, Torch Follow row, own Lean ReSTIR on, 11-pattern test, SER tooltip; Rome
          Remastered Modded Auto Exposure (optional), vendor-aware Reflex; shared: unknown vendor read as NVIDIA
          dlaa_bridge.hpp:316-318 + SER tooltip to shs_). Check each agent's report for them.
        - [ ] Group 2 leftovers: WH3 Grass Ray Tracing Range for GPU-placed grass (new work, measure in the 4K harness
          first); Rome Remastered Unit/Building Indicator Brightness on C919A85F (needs a trace with a selected unit), VFX
          Brightness (particle census) and Sky Debanding (trace); Shogun 2 Sky Debanding (trace, shogun2.tsv:13); WH3's
          offline pattern test needs a memory image (Denuvo exe on disk). Owner: next per-game data runs / rounds
        - [ ] ATN DECIDES: Shogun 2 Recommended Video AutoHDR is 0.25, the skill says 0.2 (Tales 0.25). Owner: ATN
      - [ ] SHARED STEP (from the audits): SetRaysPresetDefaults makes Fog Shadows + Specular Occlusion default on, so rays
        run with every ray method off (Thrones, Rome II, Rome Remastered, WH1-3, Pharaoh, Troy; fix: both keys into
        RAY_METHODS, graphics_page.hpp:46-47); ssr00/01/06/07 reflections occluded twice under RT Specular Occlusion and not
        at all under Path-Traced Ambient alone (twattila/shaders/ssr00_0x66006BEA:174, 331-336); the game's AO blurs still
        draw on a white target under RT AO / PT ambient (cost); Time Each Pass missing in Rome II, Thrones, Troy, Pharaoh,
        Shogun 2, Rome Remastered, Divinity 2; credits template (GT-VBAO / GT-VBGI, auto thickness after Bjorn Ottosson,
        Detours, SMAA / CMAA2 / FidelityFX SSSR, UE Extended by Marat) in every About page; rt_core.hpp:3926-3931 "helper
        started again" on the helper's 10 s idle release = info, not WARN; talesofarise_dlss frame_trace.hpp:341 polls
        renodx_trace_now every 60 presents on the game thread; Tales / Expeditions per-frame counters for the details panel.
        Owner: SHARED STEP agent (after rao_)
      - [ ] Ryse ray costs (RT shadows +1.0 ms, PT +2.7 ms at 4K DLSS Quality): Nsight work in the next days / weeks (ATN
        06:xx). Owner: a later GPU round
      - [ ] RYSE RT / PT IDEAS (ATN 11-16 "don't add them yet"; ATN decides when, then plan per row, each replaces the
        game's method): (1) ray shadows for the hair's blended strands: a second small mask from the strands' own depth
        (the hair's solid first pass already reads the ray mask); RTX Character Rendering's LSS strands need strand curves
        the game likely doesn't have (its hair looks like textured cards: check in a trace), so trace the cards as alpha-tested triangles; (2) ray fog shadows
        replacing the fog volumes' map march (ps 0x91A72BC2), Attila's Fog Shadows port, then a path-traced fog step (sun
        shafts + torch light in the fog through the PT light list); no standalone NVIDIA volumetrics SDK found, RTX Remix /
        NvRTX have it built in; (3) ray shadows for torches / fires replacing the 16384 light atlas (Attila's TW point light
        shadows); (4) RT skin subsurface (RTX Character Rendering's Burley + single scattering) replacing the game's skin
        SSS; (5) smoke / particles lit and shadowed by the rays (Attila's smoke shadows, particle light). With (1) + (2) the
        shadow-map skip All loses nothing near you. Licence: NVIDIA RTX SDK terms. Owner: ATN (go-ahead), then a Ryse round
      - [ ] Tales Bend / GT-VBGI look shots: rerun tales\g1g2\run.ps1 -Bend with a mouse click on the title (ATN allows it)
        after the fix round's install. Owner: coordinator
      - [ ] REDO 11-16 (rd_, tools/tw/round_notes/redo_1116.md): Ryse cost runs r0/run_r2cost_050544 + run_r2pt_053054
        (r2_cost.ps1: keyboard Continue, PresentMon + nvidia-smi + pass copy per state, light_bounce=1). Skip 1 / 2 removed
        NO shadow-map draws (5 passes, 862 draws in every state): sent to the Ryse fix agent. PresentMon GPU busy counts
        only Ryse's D3D11 queue (not the helper's D3D12): use frame time. Tales redo failed at the title (keyboard). Tales
        keys.ps1 focus fix without clicks (AttachThreadInput / Alt before SetForegroundWindow) to try. Owner: coordinator,
        Ryse fix agent
      - [ ] RUN ANALYSIS 11-16 (tools/tw/round_notes/runs_1116_analysis.md, pictures an_1116/out): D1 SSR never drew (the
        exe dropped or ignored SSREnabled; keep the game's lsx after exit next time); D2 gamma slider does nothing in HDR
        (PPGammaCorrection never draws); Higher Precision Scene darks +0.000..+0.026 stops, nothing broken, banding not
        judged (no sky): needs a sky view, 2 starts each; Ryse PT ambient was never on (light_bounce=0; rd_ redoing), skip
        0/1/2 no visible change in that view; Tales SSGI 17CE7BCD / 13BDD599 / 86CBF00A / 746654CA still dispatch under
        GT-VBGI (0.07 ms, replace round); Expeditions r.SSGI.Quality not forced 0 under GT-VBGI (replace round); R.U.S.E.
        rays idle in both T-runs (in-game MSAA 8x: DLAA row must turn MainRTAA off, plus a log line; then the T-run again
        against an all-off start, keys: F1 for HDR shots). Its full Boxes list (coordinator / next rounds / ATN) is in the
        report. Owner: coordinator + per-game rounds
      - [ ] Replace audit (ATN 2026-11-16: "Everything that we add, GT-VBAO, RT AO, PT AO, GT-VBGI, RT GI, PT GI, RT
        Shadows should always replace the game's own version"; skills c4ebedb8). AO: rao_ is fixing `rays_replace_ao =
        false` in Expeditions, Tales, Pharaoh, Rome II, Rome Remastered, Troy, WH1, WH2, WH3 (+ Ryse, Shogun 2,
        Divinity, R.U.S.E. checks). After it: GI (GT-VBGI / RT / PT vs the game's GI / bounce / SSGI: Tales' Bandai SSGI
        17CE7BCD / 13BDD599 / 746654CA, UE's) and shadows (Bend / RT shadows vs contact shadows and shadow maps,
        `rays_multiply_maps`). Owner: rao_, then the next replace round
      - [ ] Attila features vs skills: sk3_ is comparing; then ATN decides which to add. Owner: sk3_, then ATN
      - [ ] Tonight's run analysis: an_ is writing tools/tw/round_notes/runs_1116_analysis.md; its "Boxes" list becomes
        boxes here. Owner: coordinator
      - [ ] Ryse redo: per-state GPU cost (PresentMon GPU busy per state + a copy of the pass timing after each press;
        Ryse is capped at 60 fps, Time Each Pass totals stay ~16.8 ms and the file keeps only the last state) and Tales:
        look screenshots of Bend / GT-VBGI (run_035823 had none). Both are scripted runs with keyboard save loads.
        Owner: coordinator tonight
      - [ ] In-game checks of the HDR bug fixes: Output SDR colours (BT.709 in V30 / V31), Shogun 2 highlights (V35 top
        now reaches Peak), WH2 with FXAA on; a trace that the DOF chain draws (Attila / Thrones / WH1) and its targets are
        16-bit; README lines for the fixes at release. Owner: next TW data runs / release round
      - [x] hb_ MERGED 8bb28f1f 01:3x (7 fixes, 1 false finding; built x86 + amd64). hb_: HDR audit [B] fixes 1-8 (peak clamps, SDR peak, SDR gamut, Shogun 2 peak, WH2 FXAA, alpha, Pharaoh max,
        DOF blurs Attila / Thrones / WH1). Owner: hb_
      - [x] DONE 11-16 (78622e74, 373477d0, fd002b1c, 61000221, 55a8dff3): ht_: HDR audit R13 Vanilla-exact WARP tests with failing controls (Shogun 2 first, TW table driver, Ryse, Div2,
        Rome R, Tales / Expeditions). Owner: ht_
      - [x] npr_ MERGED 707b4b68 00:2x: 8 commits (shared twattila/rays_preset.hpp; Ryse uses twattila's psycho_test31;
        RENODX_TW_CVARS; tw_device_loss 3b Ryse chain; test/tw_ryse_tonemap PASS: SDR 0 difference, HDR control 1.07;
        cb_audit DUMPS table; README fix57 draft). RT/PT: PathAmbient / SkyApart / KeptCube WON'T GET (tiled lighting cs
        keeps the game's ambient); skipping the shadow pass WON'T GET (cascades bound for forward draws); gamma_correction
        WON'T GET. Ryse rows not run in game.
      - [x] ATN ANSWERED 00:0x: no ssao temporal / edge / denoise port ("ignore Attila's own SSAO, we don't need it");
        exposure cap: explained (not needed: geometric-mean metering). (was) ATN DECIDES (Ryse): an ssao temporal / edge / denoise port (M) and an optional exposure cap in EDAD119F.
      - [x] MERGED c1aa521c 01:0x (rys_, built, WARP PASS): Ryse: skip the game's shadow maps under RT Shadows Only (ATN 00:0x rejected npr_'s WON'T GET: "There must be a
        way"): readers per pass from R0, design, Experimental row, Time Each Pass hooks. Real readers: mask passes
        sunshadow00-04, hair strands, fog volumes (the rest only have the maps bound); 16384 map = torch / fire pool,
        stays. "Skip the Game's Sun Shadow Maps (Experimental)": Off / Past the Near Ones (72-80 % of sun caster draws) /
        All; RT/PT + Reset = Off. test/tw_ryse_shadow_skip PASS.
      - [x] MERGED c1aa521c (rys_): npr_'s WON'T GET was wrong (the tiled lighting cs takes diffuse ambient only from the
        probe cube array t34): "Path-Traced Ambient (Experimental)" off + Own Sky Rays + Steady Bounce Light rows; black
        1x1 cube at t34 while the paths bring the sky, rt_probe_sky from the widest probe; RT AO off under it.
        test/tw_ryse_path_ambient PASS. Ryse: path-traced GI / AO (ATN 00:1x: "PT GI/AO is probably doable. You need to double check"; npr_ wrote WON'T
        GET for PathAmbient / SkyApart / KeptCube: the tiled lighting cs keeps the game's ambient): find where ambient /
        indirect enters, Experimental rows if possible. Owner: rys_ (after the shadow-map item)
      - [x] CHECKED 00:13 (read-only): our scripts never write profiles (lsx_set.py = graphicSettings.lsx only); the GAME
        created Debug_Client_Profile_2 at 00:03:42 when 6Party failed and rewrote 6Party's profile.lsb / modsettings.lsx /
        InventorySettings.lsb (not in the backup set, left as they are); restored playerprofiles.lsb has 6Party active
        again, so the next start may fail the same way. Debug profile kept. Divinity 2 profile oddity (ATN 00:1x): R1 could load 6Party not ATN THE GREAT; R1b the reverse,
        "Debug_Client_Profile_2" blocks; did our scripts (lsx_set.py, settings restores) touch playerprofiles? Restore from
        the fresh backup, never delete profiles. Owner: d2 R1b agent (running 00:0x, gpu_job.lock)
      - [ ] Ryse next run: Time Each Pass + PresentMon with Skip the Game's Sun Shadow Maps Off / Past the Near Ones / All;
        look: hair fringe in shade, fog-volume shafts, casters behind the camera; Path-Traced Ambient brightness / colour
        vs the game, interiors, skin. Then ATN decides both defaults. Follow-ups: hair reading the rays, cascade casters
        into the RT scene (All loses nothing off screen). Owner: next Ryse data run / ATN
      - [ ] Ryse: VFX rows need a trace (particle VS candidates in tools/nontw/ryse/r0/families.txt); in-game check of every
        new row (Debug View / Scene Test, RT rows, Time Each Pass, test hooks + RENODX_TW_CVARS, half-res particles, LOD
        bias, auto thickness). Owner: next Ryse data run
        Merged 11-16: what the ticked Ryse rows box ("PARTLY DONE 707b4b68") left: the VFX rows and r_ParticlesHalfRes.
        Owner: next Ryse data run
      - [x] npu_ MERGED a0057296 01:2x (11 commits; built amd64; cb_audit.py conflict with npr_ resolved: list DUMPS +
        Ryse, --embed): see the NON-TW SKILLS AUDIT boxes. npu_: Tales + Expeditions parity boxes + psycho_test31 / ue_extended dedupe.
      - [x] ATN ANSWERED 01:1x "Yes, ours. Ours should be much better": Bend SSS + GT-VBGI replace Bandai's contact shadows
        + SSGI in Tales (then Expeditions, shared UE port); rows off until an in-game look check, then on by default.
      - [x] MERGED 1fd4f778 02:3x (tl_ 7e85a4ea Tales, bfe83f4e Expeditions, 63a0bcec test/tw_ue_sss_gi WARP PASS): Bend
        + GT-VBGI from twattila's unchanged passes on UE's View; Tales: Bend at the sun mask (contactshadow00's sun march
        skipped), GT-VBGI replaces Bandai's SSGI term (its 3 dispatches still run); Expeditions: Bend into the attenuation,
        UE contact shadows off while it runs, GT-VBGI after the sun draw; default off; "No RT/PT (Recommended)" turns both on.
      - [ ] Tales / Expeditions Bend + GT-VBGI: in-game look + GPU cost, then defaults on (ATN wants them on); skip Bandai's
        SSGI dispatches 17CE7BCD / 13BDD599 / 746654CA after a GI-off trace; check UE 4.23's attenuation blue channel +
        scene colour target in an Expeditions frame trace. Owner: next Tales / Expeditions run
        (was) ATN DECIDES (Tales): Bend SSS instead of Bandai's contact shadows 0x95802ADA? GT-VBGI instead of Bandai's own
        SSGI? (double work: tools/nontw/tales/census/t13_double_work.txt; both need a UE port). AO temporal / edge /
        denoise for Tales / Expeditions: not wanted per ATN's 00:0x "ignore Attila's own SSAO" (UE GT-VBAO leaves noise to
        TAA / DLSS). Owner: ATN
      - [x] gr_ MERGED 16ef0b28 02:0x: a-c on (proven: 0 of 240 harness outputs changed, 40 modes): radiance-cache selects,
        batched texel loads in paths (0.339 -> 0.324 ms) + reflections (1.070 -> 1.034), one positions copy a frame (copy
        0.32 -> 0.17, bounce 0.97 -> 0.83); Nsight frame 6.48 -> 6.17 ms; (e) "Near ReSTIR Reuse (Experimental)" off (reuse
        -46 %, frame -0.16 ms, RR rRMSE 2.19 -> 1.96 % = better); (d) half-precision ReSTIR NOT TAKEN (not faster; branch
        gr-half-restir-wip ac78a3ed); (f) radiance cache not started. Helper build\gr_final BD968BB67FF4 (includes fx3's
        fixes? -> the install round rebuilds the helper from atn-mods). gr_ (still waiting for "GPU free"): Nsight's untried speedups measured.
      - [x] DONE 2f0d8152 (ATN 02:2x "Sure, worst case ... I tell you to turn it off"): Near ReSTIR Reuse on by default
        (Attila's page = every TW game, Ryse, Shogun 2; label without "Experimental"). In-game check with ATN's tests.
      - [x] DUPLICATE of "Radiance cache (under 0.23 ms" (11-16): GPU round 2 (after the Divinity 1 run + ruh_'s R.U.S.E. GPU tests free the GPU): radiance cache item (f) from
        the Nsight list; then, after the install, a fresh Nsight capture of Attila at ATN's preset for the next wins.
        Owner: coordinator (next GPU round)
      - 23:4x ATN DONE PLAYING ("I am done with the game"): committed 38.8 GB, the gated builds started (d1_ first); gr_
        told GPU free; fx_ resumed for its harness checks on atn-mods (gpu_job.lock per job).
      - 23:5x ATN closed the leftover game processes (some Attila ones won't end; committed 38.8 -> 28.5 GB) and allowed
        in-game data runs tonight ("Start game data runs if you want"). Runs after their builds + GPU checks, one at a time
        under gpu_job.lock, fresh backups first: R.U.S.E. T-run (after ru5_ + H1 / H2), Divinity 1 R1 (after d1_ step 1),
        Divinity 2 R1b, Expeditions E9 (-d3d12) + Low start. Saves that need a mouse click: ask ATN (keyboard only).
      - [x] fx_ GPU ROUND DONE + MERGED 4a7f6318 00:4x: RR removal with FG on CRASHED the helper (C0000005 nvngx_dlssd)
        -> 066d921e: a lost RR object goes before Streamline's stop, Streamline's own loss keeps its live RR (PASS 3/3,
        2.6 s stall; SL loss with RR PASS 2/2, 3.0 s, ~180 MiB kept); 62423a4c RENODX_TW_TEST_FG_DEVICE_REMOVAL; both
        losses in a row PASS (5.3 s); FSR / XeSS FG losses with RR PASS; harness 17/17 ALL PASSED + 5/5 on the final
        helper build\tw_dlaa_helper_fx3 (6DAAA4BF9CBF); fg_bridge_test 6 phases PASS; quiet tw_device_loss x86 + amd64
        PASS; tw_gpu_completion + tw_helper_lifetime PASS.
      - [ ] Both D3D12 devices (Streamline FG + RR) removed at the same moment: every order tried crashes or keeps a
        removed device; not covered by the switches. Owner: next helper round
      - 00:4x coordinator ended ru_'s gated build (ninja -j 3 target ruse, hung 45 min with no child, held slot 1;
        everything queued behind it). ru6_ builds the R.U.S.E. tree once its pieces are in.
      - [x] DONE 11-16 (fx_ GPU harness 4a7f6318, tw_device_loss quiet PASS, gr_ 16ef0b28, ruh_ H1 / H2 df26e42c; TW install 05:0x: Attila v164, Rome II r7, Thrones r8, WH1 r9, WH2 v54, Troy v58, Pharaoh v71, WH3 v63, Shogun 2 v77, Rome Remastered v15, all on helper115): Coordinator after ATN's game: merges (one at a time), fx_ GPU harness (device removal with FG on, FG device
        loss, fg_bridge_test), quiet tw_device_loss, gr_, ru_ H1 / H2, build all, stamp, install (dry run, fresh backups).
        Owner: coordinator
    - [x] DONE 11-16 (TW install 05:0x: Attila v164, Rome II r7, Thrones r8, WH1 r9, WH2 v54, Troy v58, Pharaoh v71, WH3 v63, Shogun 2 v77, Rome Remastered v15, all on helper115): REVIEWS + FIXES MERGED 8ee4ab2f 20:5x (left: GPU harness after ATN's game, build all, stamp, install): ATN 18:5x: after the fix agent's 21 fixes, a second Fable 5.2 High review of THE FIX DIFF ONLY (coordinator
      recommends it: helper / threading fixes), then build all, stamp, install for ATN's Attila test. Nsight audit agent
      started (were last night's Nsight captures used for more wins?). Owner: coordinator
    - [x] DONE 20:3x (audit agent, read-only at 3c750922; findings below, rule numbers = hdr-review-rules.md): ATN 20:1x: HDR audit of all our add-ons against four notes from another RenoDX modder (maintainers' review
      rules from ShortFuse / Musa: Downloads\3 skills from\ SKILL.md "tonemap-reviewer-rules", SKILL (1).md
      "shader-settings", lesson.md + lessons-hdr-path.md, Arkham City): LUT bridges without UpgradeToneMap, no clip at
      white in redone adds, gamma, PsychoV call contract (peak clamp, gamut mode per output), no hue settings after
      PsychoV, bloom unclipped, every reader of an upgraded target, 8-bit targets in the chain, MSAA halves, NaN order,
      Vanilla-exact tests with a failing mutant, switch / area variants, skipped swap chains. Audit agent running; findings
      become boxes (look changes: ATN). Owner: coordinator
      HDR AUDIT FINDINGS (all 15 add-ons break at least one rule; [B] = pure bug, [P] = changes the picture):
      - [x] DONE 12577cce (hb_, merged 8bb28f1f; Div1 = d1_): [B] R4 Peak / Paper White can reach 0.8 (Peak min 400, Paper White max 500): clamp max(peak, paper white) in
        Attila shared.h:149, Div1 shared.h:59, Div2 t00_0x7016F292:221 + shared.h:84. Owner: HDR bug round (after the fix merge)
      - [x] FALSE FINDING (hb_: WH2 / WH3 / Pharaoh / Troy already set peak = paper white = UI = 100 on Output SDR since e92046f6): [B] R4 Peak not 1 on Output SDR (Attila does it): WH2 shared.h:157, WH3 :156, Pharaoh :156, Troy :150 (SDR
        display + Recommended clips per channel in the proxy). Owner: HDR bug round
      - [x] DONE 4b509903 (hb_, merged 8bb28f1f; 9 V30 macros + 8 V31 sites; R.U.S.E. = ru_ 01029a12 on its branch): [B] R4 V30 / V31 gamut mode on SDR output: V30 macros always pass mode 1 (Attila / Rome II / Thrones / WH1 /
        WH2 / WH3 / Pharaoh / Troy shared.h, Rome R :126-130, Div1 :70-73); V31 takes the slider, never BT.709 on SDR
        (hdr_to_screen.hlsli of Rome II / Thrones / WH1 :17, WH2 :83, WH3 t00:170, Pharaoh :84, Troy :81, Rome R
        resolve.hlsli:83, RUSE shared.h:70). Legacy modes only. Owner: HDR bug round
      - [x] DONE 881dd9ed (hb_, merged 8bb28f1f; changes the V35 top too, the bug was in it): [B] R4 Shogun 2: Peak not pre-corrected for ApplyGameGamma after PsychoV (tonemap.hlsli:415 / :419 -> :443),
        LimitToPeak (:461) clips the top. Owner: HDR bug round
      - [x] DONE 130d2dc6 (hb_, merged 8bb28f1f; also the unfiltered centre texel at line 53): [B] R4 WH2 fxaa00_0xCE9EA4E8:221 max(0) strips PsychoV's BT.2020 channels when FXAA is on. Owner: HDR bug round
      - [x] DONE 9f68e7a7 (hb_, merged 8bb28f1f; also Thrones / WH1 compute blurs; test/tw_dof_blurs WARP 12/12 bit-exact in SDR, mutant + LOD controls fail; LEFT: a trace that the chain draws into 16-bit targets; Expeditions part = no change): [B] R7 game DOF blurs clip every tap (mov_sat), not replaced: Attila A27C3A75 / B35968DC / CS C9D036B1 /
        83A1A82D, Thrones A27C3A75 / B35968DC, WH1 0228529B / 4044871E (Rome II replaced its dof01-05); first a trace that
        the chain draws. Expeditions 0x93E43D26 / 0x07E055CF / 0x1F128822 max(0, x+c) stock (draw after the tone map?).
        Owner: HDR bug round (trace part: next data run per game)
      - [x] DONE 534c88d5 (hb_, merged 8bb28f1f): [B] R7 alpha unclamped: Thrones tone_mapper.hlsli:71, WH1 :52 (Attila saturates). Owner: HDR bug round
      - [ ] [B] R7 no reader inventory for Shogun 2's broad upgrade (addon.cpp:2576-2594), Div1's every-RGBA8 upgrade
        (addon.cpp:957-965), Tales' stock FXAA 0x8533AF0C / SMAA. Owner: HDR bug round (inventory from traces)
      - [ ] [B] R10 unproven 8-bit links (never traced): Pharaoh FXAA 0xA6756C1F, WH2 dof00/01 C337D8E3 / 91B4F89A,
        Shogun 2 dofblur02_0x6531CCC3 (unorm). Owner: next data run per game
      - [x] DONE 73c0d40c (hb_, merged 8bb28f1f): [B] R12 Pharaoh t00_0x976C1C74:132 max(0) before the bypass lerp (:224). Owner: HDR bug round
      - [x] DONE 78622e74 (ht_ test/tw_vanilla_exact: 212 shaders vs the game's bytes on WARP amd64; 167 EXACT, 32 known R6,
        13 FAIL, controls 212/212 caught; part 6 conflict kept atn-mods' (d2b_ had the same Div2 check)). [B] R13 Vanilla-exact tests with a failing control (only Div1 0x30A1D40D has one): TW tone mappers /
        hdr_to_screen / bloom / lens flare, Shogun 2 tonemap00/01 first (rewritten from source), Ryse (ledger DO NOW),
        Tales / Expeditions (no harness source to re-run their WARP claim), Div1 bloom / god rays / UI / AA, Div2 t01 /
        bloom00 / ui00 (t00 report-only), Rome R, RUSE. Owner: HDR test round (after the fix merge)
      - [ ] [B] R14 variants not replaced (traces decide if they draw): Attila / Thrones D9123DE1, 83192B6F + MSAA 7104ADE8
        / 94EE9A57 / 787D82F2; hdr_lenseflare Attila / Thrones 169B6370, Rome II 172DE8D6, WH1 D0FA2C68; WH1 50DEBCD2 /
        77B6E6A2 (UI portraits); WH2 ~10 (sprite 1C42B26B draws); WH3 10217D55 / stereo 427A5478; Troy 7 + MSAA; Pharaoh
        ~13; Ryse HDRFinalPass 0xC3970136 + TAA 0x6A057F66; Tales (rests on ATN's dump). Owner: next data run per game,
        then the HDR bug round
      - [ ] [P] ATN DECIDES R1 Div1 mode 1 "UC2 Extended / PsychoV30" = UpgradeToneMap then PsychoV30 (tonemappers.hlsli
        :141-143) + preset "PsychoV30 (earlier Recommended)": ATN said keep the old ones, decide at the end. Owner: ATN
      - [ ] [P] ATN DECIDES R1 shoulders not identity below their start: Neutwo from 0 (grade below white moves with Peak:
        1000/203 -> 0.18 = 0.177, 0.5 = 0.45, 1 = 0.71) in Attila / Rome II / Thrones / WH1 / Troy / Pharaoh / Rome R /
        Div1 Pragmap (Troy t00:147 + Pharaoh t00:171 comment "SDR exactly below white" is false); hard max(m,1) knee
        (kink at white) in WH2 / WH3 / Ryse / Tales / Expeditions / Div1 GradeAfterCurve. Fix = an Experimental row.
        Owner: ATN
      - [ ] [P] ATN DECIDES R2 Shogun 2 pointlight01_0x57139F8D:258 saturate after HDR Firelight (low-quality lights).
        Owner: ATN
      - [x] MERGED 03:1x (lg_ 94444af0 + 09a27adf + fcc98258): TW_FRAME_GAMMA = the proxy's decode gamma (2.2 with GC on,
        2.0 off), twattila/frame_gamma.hlsli EncodeFrame / DecodeFrame used by the proxy, hdr_to_screen, flares, video,
        uiMap02; GC row (same key, on) now in every mode; TW_TONE_MAP_PEAK = Peak^(1/1.1) + EmulateFrameGamma keep Peak.
        Flares EXACT (6e-8), GC check 32/32, psychov35 x86 + amd64 PASS. V35 at Paper White 203: 1 % grey 2.03 -> 1.28 nits,
        5 % 10.15 -> 7.52, 18 % 36.54 -> 30.78, white unchanged. Built Attila + Thrones x86. ANSWERED 02:1x (2.2 in HDR). [P] ATN DECIDES R3 Attila + Thrones HDR show the gamma-2.0 picture as linear (no 2.2 emulation; Shogun 2
        emulates it). Owner: ATN
      - [ ] [P] ATN DECIDES R5 hue settings after PsychoV in legacy V30 / V31 modes (all TW games, Rome R, Shogun 2, Ryse
        + Fire Hue, Tales, Div2 ApplyFireColor): V35 is fine everywhere. Owner: ATN
      - [ ] [P] ATN DECIDES R6 bloom: smoothing also in Rendering Mode SDR (Vanilla != game: Attila, Rome II, Thrones,
        WH1-3, Troy, Pharaoh); log shoulder on the bloom cap (Attila, Rome R); Troy / Pharaoh glow mask reads the linear
        handoff; Recommended lowers bloom (Attila 0.35, Rome R 0.05, Div1 0.25, Div2 0.44). Owner: ATN
      - [ ] [P] ATN DECIDES Extended Saturation = RGB blend to grey, not OKLab chroma (WH2 :33, WH3 t00:287, Pharaoh :61).
        Owner: ATN
    - [x] ATN 20:2x "Yes, with credit": DONE fb2cc981 game-port-method/references/hdr-review-rules.md (credited:
      ShortFuse, Musa; notes by boostedwarr), linked from SKILL.md + README; skills zip in Downloads rebuilt.
    - [x] NON-TW SKILLS AUDIT 19:5x (ATN: "have Ryse, Tales, Expeditions done everything from the skills?"): all three
      pass one add-on / pages / Reset + RT/PT + No RT/PT / V35 / SDR preset / Output + first start / per-channel Extended /
      About / fast dispatch / frame trace / helper114 / tw_install / registry / helper-absent page dumps. GAPS below; one
      NON-TW PARITY ROUND (an implementer per game, after the fix merge: R2 / R5 / R6 / R11 / R17 touch these games).
      - [x] DONE 11-16 (npr_, merged 707b4b68; not run in game): Ryse: Time Each Pass (no pass_timing; no button in page_ry_fin). Owner: non-TW parity round (Ryse)
      - [x] DONE 11-16 (npr_, merged 707b4b68; not run in game): Ryse: twattila/test_hooks (SET / SHOT / PROFILE) beside the renodx_trace_now poll (graphics.hpp:2650). Owner:
        non-TW parity round (Ryse), with the cvars-from-ini box
      - [x] DONE 11-16 (npr_, merged 707b4b68; not run in game): Ryse: ray-tracing Debug View + Scene Test rows + rt_scene::OnTonemapDrawn; README.md:339 "Not yet: the Debug
        View". Owner: non-TW parity round (Ryse)
      - [x] DONE 11-16 (npr_, merged 707b4b68; not run in game): Ryse: RT rows missing in page_ry_fin (ryse.tsv DO NOW): RayTracedAORays, PathSkyApart, PathKeptCube,
        PathAmbient, ReflectionReuse, LightBounceResolution, AoReblur; the RT/PT preset is a hand list (addon.cpp:239-260),
        not graphics_page::RAYS_PRESET (shaders-ui.md rule). Owner: non-TW parity round (Ryse)
      - [x] DONE 11-16 (rows 707b4b68; the SSAO temporal / edge / denoise port answered "no" (ATN 00:0x); the VFX rows moved to the "Ryse: VFX rows need a trace" box): PARTLY DONE 707b4b68 (Experimental off: ssao auto thickness, light bounce checkerboard / backface / auto thickness, LOD bias all textures, half-res particles; Modded Auto Exposure NOT NEEDED: geometric-mean metering; ssao_resolution WON'T GET; LEFT: ssao temporal / edge / denoise = an M-size ao_filter port (ATN decides), VFX rows need a trace): Ryse: rows missing (ledger DO NOW): ssao temporal / edge fix / denoise / auto thickness / resolution; light
        bounce temporal / checkerboard / backface / auto thickness; LOD bias on all textures; Modded Auto Exposure; the VFX
        rows; r_ParticlesHalfRes. Owner: non-TW parity round (Ryse)
      - [x] DONE 11-16 (npr_, merged 707b4b68; not run in game): Ryse: tooltips name Attila / ATN (addon.cpp:1028 "as ATN plays Attila", :1215 "in Attila's tests"); its own
        FULL_DOWNLOAD_NOTE (addon.cpp:188) lacks FSR / XeSS (twattila/credits.hpp:12 has them). Owner: non-TW parity round
      - [x] DONE 11-16 (npr_, merged 707b4b68; not run in game): Ryse: SDR-encode WARP ramp of tonemap00, the gamma_correction code read, a Ryse table in test/tw_device_loss;
        the offline read of R0's traces (plan step 8: light list, smoke / foliage / water / fog families, cascade readers).
        Owner: non-TW parity round (Ryse)
      - [x] DONE 11-16 (npu_, merged a0057296; not run in game): Tales + Expeditions: device_loss not wired (no Install / InstallLast / Uninstall; Decide straight from
        OnCreateSwapchain: Tales addon.cpp:735-738, Expeditions :564-567). Owner: non-TW parity round (UE)
      - [ ] PARTLY DONE a0057296 (ssao auto thickness Experimental off; censuses T9 / T10 / T11 / T12 / T15 written, T9 found + fixed a preset turning Motion Blur Off back on; LEFT: Bend SSS / GT-VBGI = ATN decides, landscape LOD bias needs sampler clones on 44 PS): Tales: no Bend SSS, no GT-VBGI (no ScreenSpaceShadows* / light_bounce_* keys; tales.tsv T13); no AO temporal /
        edge fix / denoise rows, no LOD bias trilinear (T12); the censuses T9 (25 menu settings), T10 (casters), T11 (scale)
        and T15 (shadow-map skip) not found. Owner: non-TW parity round (Tales)
      - [ ] PARTLY DONE a0057296 (4 mapped, 6 half-size-copy ones mapped but not bypassed, 4 DOF temporal never mapped, all level B; RayTracedSpecularOcclusion + RayTracedLightBounceResolution rows + a cbuffer over-read fix; ssao auto thickness; E7 WARP PASS; sg.* census; page text "Keep the game's own HDR setting off"; LEFT: E9 -d3d12 launch, the Low start, NEEDS A TRACE rows): Expeditions: 14 unmapped TAA permutations (TEMPORAL_SHADERS has 6, dlss_graphics.hpp:110); the sg.* level
        census (S10); the Low start never ran; rows missing: RayTracedSpecularOcclusion, RayTracedLightBounceResolution,
        ssao temporal / edge fix / denoise / auto thickness / resolution; E9 (-d3d12 launch), E7 (WARP of lut00's
        output-device branch); the page says "The game has no HDR option of its own" (addon.cpp:446) though
        bUseHDRDisplayOutput exists: say the game's HDR stays off (ATN); stale comment addon.cpp:552. Owner: non-TW parity
        round (Expeditions)
      - [ ] All three: parity_check letters + tools/tw/parity ledgers (ryse / tales / expeditions .tsv from 00:4x list
        done items as DO NOW: refresh); dump_any.py letters ry / ta / exr; box "SHARED STEP owners" S6 (page-dump drivers)
        is done: tick it there. Owner: non-TW parity round (coordinator part)
      - [x] DONE 11-16 (npr_ 707b4b68 + npu_ a0057296: 0 constants lost in Tales 78 / Expeditions 24 / Ryse 20; ReShade.log facts in the READMEs): All three: cb_audit never run on the replaced shaders (ryse plan.md:167, tales plan.md:101); stability item 1
        (unmodded ReShade.log facts in a commit) missing. Owner: non-TW parity round
      - [ ] ATN by eye: Hue Clip Ryse 80 (addon.cpp:87), Tales 70 (:67), Expeditions 70 (:63). Owner: ATN
      - [ ] Release (all three): README entries (Tales last v71 at :369; Expeditions README_DLSS last v29 at :960; Ryse
        check); one-add-on release zips not built. Owner: coordinator, release round
      - [x] DONE 11-16 (npu_ b200df25: Tales / Expeditions include twattila's psycho_test31 + one ue_extended, byte-identical bytecode; npr_ 13d6906a Ryse): Shared copies: psycho_test31.hlsli (Tales / Expeditions 32 lines off twattila's, Ryse 2); ue_extended.hlsli
        twice (S3's de-dup dropped when S3 was answered). Owner: shared-steps round
      - [ ] Never verified in game (all three): SDR first start / SDR preset / SDR output; the HDR-only state (helper
        absent) only by page dumps; the fast dispatch replay oracle (no UE / Ryse trace converter, S9). Tales: DLSS frame
        generation never ran (G1 / G2 had no Streamline). Expeditions: rays only in the menu, no save scene. Ryse: unload
        harness never ran; every RT / AO / Light Bounce row untested in game. Owner: coordinator (next game window +
        harness)
    - [x] CLEANUP AUDIT 19:4x (ATN: "anything else lost last night?"): of 154 open boxes before the cleanup 42 still open,
      9 merged (7 into open lines, 2 into Final verification, ticked today), 78 ticked in step 1 (all 88 cited hashes
      exist; ~30 spot-checked OK; 13 OBSOLETE reasons hold), 25 ticked since with evidence, 0 gone; the archive has no
      "- [ ]" boxes. Really lost: 1 ATN question; doubtful: the boxes below (from the archive / ticked lines).
      - [ ] ATN DECIDES (WH3, lost in the cleanup): Fog in Reflections (RayTracedReflectionFog, Experimental, default 0) in
        the RT/PT preset now that Water Reflections are on by default (archive :1202, "ATN'S 2026-11-12 THIRD LIST").
        Owner: ATN
      - [ ] Attila v118 / v119 "Left" list (archive 1878-1885, 3589): Light Bounce +1 ms with Far Tree Shadows (a BLAS of
        its own for the cards?); an off-screen grass bank brighter (lit without grass shadows); ContactShade not seen on
        bodies. Check against today's build first. Owner: next Attila RT round
      - [ ] ATN A/B tests named in the "No more performance improvements" box without their own box: Small Soldiers as
        Sprites, Light Bounce Half, pausing Wallpaper Engine. Owner: ATN (in game)
        Merged 11-16 with the A/B list of the "ATN ~19:xx: "No more performance improvements" box: pausing Wallpaper
        Engine = 3.0 ms GPU; game Shadows Medium + Two Shadow Cascades = the "one cascade instead of two" entry. Owner:
        ATN (in game)
      - [ ] Attila 10-29 / 10-30 archive items: river noise while moving (RR / reflections) (archive 3788); the first trace
        request's Close fails on the debug layer (GPU bakes) (archive 3822). Re-check on today's helper. Owner: next
        Attila RT round / harness
      - [ ] Rome Remastered input bug: ATN watches input after a long session (ticked line HANDOFF :1414 had no ATN box).
        Owner: ATN (in game)
      - [ ] Astra review: the tester's first freeze still unexplained (archive 1344): check against today's 21 review
        findings. Owner: coordinator
      - [ ] WH3 v40 / batch 6 "Not done" (archive 1270, 1315-1331; wh3_v40 / wh3_batch6 reports): Shadow Map Size 2048 /
        1024, the 800F7F7F half-copy bypass (0.10 ms), C0ED2883 / 1405BFB4 motion, the tent's black spot (item 15 waits
        for ATN). Owner: next WH3 round (+ ATN for the tent)
      - [ ] Pharaoh below 100 %: p2ph_scale_census.txt still has 8 BUG (B) rows (shroud decals 61C823A0 / 8852FBD3, SSR
        apply DE4F92CD, particles 3F1B86E1, ...) and 5 of 9 sun draws differ at 33 % with the game's contact shadows on
        (archive 852, v61). Owner: next Pharaoh round
    - [x] NSIGHT AUDIT 19:1x (ATN: "did we use last night's Nsight captures?"): harness captures only (11-14 / 11-15:
      g3b reuse base / record / lean / a32, g4_ao x3, h110, rp_*, ser, cl_*, atn_true, wh3_true, smoke_*); no in-game
      Nsight, no per-preset Nsight (per-preset numbers came from Time Each Pass / PresentMon / RT_PROFILE). ACTED: reuse
      (record + Lean), WH3 reflections, smoke build, SER / cluster flags / GT-VBAO prefetch / hand-off hops rejected with
      numbers. NEVER TURNED INTO A TASK: the four below + f16 reservoirs closed on the wrong number.
      - ATN 19:3x "let's do them": GPU round agent gr_ started (worktree; code + builds + WARP now; GPU harness / Nsight
        only after ATN's MHR session: ATN "do them after the game"). Owner of the six boxes below: gr_
      - [ ] Path tracing register limit (1.47-1.81 ms in game, 28.7 % occupancy, 85 % registers): fewer live registers
        or surface / shading as two dispatches with the same math; estimate -0.15 to -0.45 ms; exact (prove with
        HARNESS_DUMP as a833578d). Owner: next GPU round
      - [ ] Attila reflections (1.07-1.16 ms, latency-bound, 88 % registers; hot TRACE_SURFACE + screen look-ups): the
        register cut or earlier screen loads; estimate -0.10 to -0.25 ms; exact. Owner: next GPU round
      - [x] DONE 11-16 (c9885dec, merged 16ef0b28): Two render-size RGBA32F position copies in rt_d3d12.hpp (kept_positions_ ~1386, ambient_positions_ ~4910):
        one copy from a shader that reads the positions already, or one shared copy; estimate -0.03 to -0.08 ms; exact.
        Owner: next GPU round
      - [x] NOT TAKEN 11-16 (not faster, ac78a3ed): REOPENED f16 reservoirs (32 -> 24 B, the record's reuse still L2-bound 93-97 %): estimate -0.05 to -0.15 ms;
        look change, scored. The closed box judged it on harness RT_PROFILE 0.34 ms; its plan said Nsight (1.49 ms); in game
        reuse 0.61 / 0.79 ms. Owner: next ReSTIR round (look-risk list for ATN)
      - [x] DONE 11-16 (9c36a924 Near ReSTIR Reuse, on by default 2f0d8152): Record (full) reuse with radius 6 / 3 neighbours (radius 6 halved the pre-record pass 2.54 -> 1.40 ms; Lean +
        3 neighbours + radius 6 scored better 2.18 -> 1.95 % rRMSE): estimate -0.20 to -0.35 ms; look change, scored.
        Owner: next ReSTIR round (look-risk list for ATN)
      - [ ] Radiance cache (under 0.23 ms; dependent loads, SM 3 %): at most -0.05 to -0.10 ms, picture unknown; low.
        Owner: next GPU round (last)
        Merged 11-16 from "GPU round 2 (after the Divinity 1 run" (AFTER THE RESTART step 4): radiance cache item (f)
        from the Nsight list first; then, after the install, a fresh Nsight capture of Attila at ATN's preset for the
        next wins. Owner: coordinator (next GPU round)
    - [x] DONE 11-16 (fixes 8ee4ab2f + 4a7f6318; install done 05:0x): ATN 17:3x "Start the review": Fable 5.2 High crash / freeze review workflow wf_73f86de5-728 (6 areas: locks,
      bridge, hooks, lifetime, non-TW, TW; each verified by a refuting agent). Then fixes one at a time, build all,
      install. Owner: coordinator
      - DONE 17:45: 24 confirmed (21 unique: R7 = R2, R10 = R9, R20 = R17), 8 refuted; full text with scenarios and
        fixes in tools/tw/round_notes/review_1115.json. Fixes one at a time by one agent, then build all, stamp, install.
      - fx_ DONE 19:0x: branch worktree-agent-a415dd24277bcc54b (base 355eee64, 19 commits, tip ec99db49, NOT merged):
        R1 791a2123, R2 746f0548, R3 c346cf04, R4 2d941d68, R5 6dec7afa, R6 038a216a, R8 79a834a9, R9 4957c9af, R11
        4fffa624, R12 f35d6d5f, R13 d312c48c, R14 0883c7c6, R15 80802e0b, R16 a4a9371d, R17 70490b09, R18 75680deb, R19
        6f836836, R23 54e09a90, R24 ec99db49. Both build lines + helper built; x86 / amd64 PASS: tw_fg_initialization,
        tw_game_gi, tw_helper_lifetime, tw_gpu_completion, tw_output_encoding, tw_scrub_lods, tw_unit_pack_rounds.
        fx_ ROUND 2 DONE 20:1x (same branch, 7 commits 8fd15e5c..159f46e6): R13 Answer(20000) for a pending steady
        answer (also Release(true)); R15 park on a backend switch, never on a loss; R19 no pass without a proxy shader; R1
        RR loss stops Streamline, destroys RR normally, restarts both (no leak); R4 presenting cleared on failure; R22 retry
        (resize / device change / 30 s, 3 a session, PublishEmptySet frees the helper's set). Builds both lines + helper
        build\tw_dlaa_helper_fx2; x86 + amd64 PASS: tw_helper_lifetime (new retry case), tw_device_loss, tw_fg_initialization,
        tw_game_gi, tw_gpu_completion, tw_output_encoding, tw_scrub_lods, tw_unit_pack_rounds. LEFT: harness
        RENODX_TW_TEST_DEVICE_REMOVAL with FG on + an FG device loss + fg_bridge_test (GPU, after ATN's game); then merge,
        build all, stamp, install.
        Second Fable 5.2 High review of the fix diff DONE 20:0x (wf_b2c7ad87-41e, review2_1115.json): 7 confirmed, 1
        refuted; 15 of the 19 commits OK. fx_ round 2 started on the same branch (the 7 + R22). Merge after round 2 and
        the harness runs (after ATN's game). The R boxes below are ticked at the merge.
      - [x] DONE 11-15 (merged 8ee4ab2f): Review 2 #4 REGRESSION (791a2123 R1): the kept lost RR object holds its removed D3D12 device: no new device
        (Streamline, scene, RR, FSR / XeSS) until restart; fix: Shutdown Streamline first, then destroy RR normally, then
        restart both. Owner: fx_ round 2 (+ harness removal modes after ATN's game)
      - [x] DONE 11-15 (merged 8ee4ab2f): Review 2 #1 (d312c48c R13): a scene reopened inside a steady request still gets the 5 s budget: Answer(20000).
        Owner: fx_ round 2
      - [x] DONE 11-15 (merged 8ee4ab2f): Review 2 #2 (80802e0b R15): an FG backend switch during an FSR / XeSS chain cold start still joins the maker:
        park a still-making proxy. Owner: fx_ round 2
      - [x] DONE 11-15 (merged 8ee4ab2f): Review 2 #3 (6f836836 R19 half): WH1 / WH2 in DX12 still new a proxy pass + log a warning every present: early
        return when no proxy shader. Owner: fx_ round 2
      - [x] DONE 11-15 (merged 8ee4ab2f): Review 2 #5 / #6 (minor): don't park in the Lost branches; clear state.presenting when the bridge can't be
        prepared on the new device. Owner: fx_ round 2
      - [ ] R21 skipped (Shogun 2 NGX feature release): needs the game's static NVSDK_NGX_D3D11_ReleaseFeature by pattern
        (the DLL image, tools/shogun2/sx.py) + an in-game check. Owner: next Shogun 2 round
      - [x] DONE 11-15 (merged 8ee4ab2f): R22 skipped (a failed Prepare texture turns the bridge off for the session): a retry changes what players see
        (DLSS comes back after a VRAM failure); freeing the helper's old set from Prepare's fail path needs Release's request
        sequence. ATN 19:3x: YES, DLSS comes back after a failure (coordinator: with a limit, e.g. a retry at the next
        resize or after 30 s, at most 3 a session, one log line each; review 2 #7: also free the helper's previous set on
        the fail path). Owner: fx_ round 2 (started 20:0x)
      - [ ] test/tw_native_hook_lifetime does not compile (x86 / amd64): graphics_hooks.hpp includes rt_core.hpp ->
        embed/shaders.h, run.ps1 has no twattila include path (before these fixes). Owner: next test round
      - [x] DONE 11-16 (quiet rerun x86 + amd64 PASS): test/tw_device_loss "limit, front" case failed once per arch while builds ran (window focus): rerun quiet.
        Owner: coordinator before the install
      - [ ] fx_ notes: R1 the kept lost RR object holds its removed D3D12 device (RR / RT / FSR devices may be refused until
        restart instead of a helper crash); R2 Ryse patterns now searched at device creation: check the next Ryse log; R5 a
        Windows HDR toggle mid-game applies at a new swap chain only (as TW); R15 open_generation's backend switch still
        frees the old proxy's resources inside a waited request; harness modes (removal, switching_removal, release_*,
        leaf_storm) + fg_bridge_test not run for the helper commits. Owner: coordinator (harness before the install)
      - [x] DONE 11-15 (merged 8ee4ab2f): R1 [crash] A lost Ray Reconstruction device is destroyed with NGX D3D12 shutdown while Streamline is in use
        (src/games/twattila/dlaa_helper/main.cpp:657; NVIDIA with Ray Reconstruction plus DLSS Frame Generation through the ).
        Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R2 [crash] Ryse particle/light detours go in at a Present with no thread coordination while particle job threads
        run the hooked functions (src/games/rysesonofrome/particle_counts.hpp:664; rysesonofrome). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R3 [crash] device_loss watch thread can still be inside the add-on's code when ReShade unmaps the image (2 s
        bounded stop, then FreeLibrary) (src/games/twattila/device_loss.hpp:516; twattila, twrome2, twthronesofbritannia,
        twwarhammer, twwarhammer2, tw). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R4 [crash] Frame generation's reshade_present uses the bridge's and its own objects without checking they belong
        to the presenting device (src/games/twattila/frame_generation.hpp:198; twwarhammer3). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R5 [crash] Tales / Expeditions run output_encoding::Decide at every create_swapchain, including ResizeBuffers,
        while swapchain_v2 keeps the creation-time swap_chain_upgrade_info (src/games/talesofarise/addon.cpp:738;
        talesofarise, expeditionsrome). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R6 [crash] Tales: AddonInit throws on a game folder with characters outside the ANSI code page (path::string())
        (src/games/talesofarise/dlss_graphics.hpp:3364; talesofarise). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R8 [crash] WH3 reflections / traced_lights / sun_ao / game_gi caches are not tied to their device: after a GPU
        reset with the old device still alive, objects from the removed device are bound on the new context
        (src/games/twwarhammer3/addon.cpp:958; twwarhammer3). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R9 [freeze] object_motion: pipeline-destroy callback re-locks state.mutex (shared_mutex) held across the motion
        draw's D3D11 calls and releases (src/games/twattila/object_motion.hpp:1458; twattila). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R11 [freeze] Tales: destroy_device handler re-locks rt_shadows' mutex when rt_shadows drops its own ReShade-
        wrapped D3D12 device (self-deadlock) (src/games/talesofarise/dlss_graphics.hpp:2082; talesofarise). Owner: review fix
        agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R12 [gpu_reset] anti_aliasing::Update calls ID3D11Device::GetImmediateContext on a D3D12 device (vtable slot 40 =
        ID3D12Device::SetStablePowerState) (src/games/twattila/anti_aliasing.hpp:1547; twwarhammer2). Owner: review fix agent
        fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R13 [wrong_output] Steady requests keep the 5 s answer budget while the helper reopens its idled scene / RR device
        inside them (helper killed, all helper features off) (src/games/twattila/dlaa_bridge.hpp:605; Every game that ray-
        traces through the helper and takes Prepare's ray_). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R14 [wrong_output] The helper never replaces its own D3D11 device after a GPU reset, and every request branch is
        gated on the fence that device opens (src/games/twattila/dlaa_helper/main.cpp:729; Any game where the same helper
        outlives the removal of its D3D11 devic). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R15 [wrong_output] Replacing or idling an FSR/XeSS upscaler or proxy frame generation joins its cold-start thread
        on the helper's request thread (src/games/twattila/dlaa_helper/upscale_d3d12.hpp:352; Games using FSR or XeSS
        upscaling, or FSR/XeSS frame generation, throu). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R16 [wrong_output] Attila/Thrones grass and tree hook writes are neither restored nor kept at detach: every device
        rebuild scales the already scaled values again (src/games/twattila/graphics_hooks.hpp:1349; twattila,
        twthronesofbritannia). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R17 [leak_or_growth] Expeditions ray tracing: scene_views keeps an SRV (and so the texture) of every scene colour
        target ever seen (src/games/expeditionsrome/ray_tracing.hpp:282; expeditionsrome). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R18 [leak_or_growth] A set published before the helper opened it leaves its duplicated handles, and so its shared
        textures, open in the helper for the whole session (src/games/twattila/dlaa_bridge.hpp:941; Ryse with helper upscaling
        below 100% render scale). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R19 [leak_or_growth] Swap chain proxy passes are new'ed and erased without delete (per failed present, and at
        every DestroySwapchainProxyItems) (src/mods/swapchain_v2.hpp:1254; All swapchain_v2 add-ons: one SwapchainProxyPass
        object per back buffe). Owner: review fix agent fx_
      - [x] NOT DONE: see the R21 box ("R21 skipped (Shogun 2 NGX feature release)", still open; 8ee4ab2f has no R21 fix; corrected 11-16): R21 [leak_or_growth] Shogun 2: each render scale or DLSS preset change abandons the game's existing NGX DLSS
        feature without releasing it (src/games/twshogun2completeedition/game_dlss.hpp:354; twshogun2completeedition). Owner:
        review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R22 [minor] One failed texture creation in Prepare turns the bridge off for the session, and the helper keeps the
        previous set open (src/games/twattila/dlaa_bridge.hpp:921; Every game using the helper; most likely on 8-12 GB cards
        at 4K with R). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R23 [minor] Shogun 2 vfx_effects keeps destroyed buffers' handles and stores a write-map pointer without checking
        size, so Identify can read up to 368 bytes past a smaller buffer reusing the address
        (src/games/twshogun2completeedition/vfx_effects.hpp:189; twshogun2completeedition). Owner: review fix agent fx_
      - [x] DONE 11-15 (merged 8ee4ab2f): R24 [minor] Pharaoh Dead Unit Detail: dead_units_installed is never reset, so an add-on re-attached in a retained
        image never patches again; the 6-byte patch is also written without stopping threads
        (src/games/twpharaohdynasties/addon.cpp:1853; twpharaohdynasties). Owner: review fix agent fx_
    - [ ] WH3's x64 render-list WPR run: the game said "Unable to initialize SteamAPI" twice (Steam not elevated;
      HKCU Valve ActiveProcess pointed at a dead pid); ATN restarts Steam, then one run (x64_run.ps1). Owner: ATN, then
      coordinator
  - [x] SHOGUN 2 ONE ADD-ON MERGED d4c82679 (s2m_): _dlss folded in (pages HDR & Effects / Graphics 1 of 2 / 2 of 2;
    shared twattila/settings_pages.hpp for the page helpers; fast dispatch; hdr_link / dlss_jitter / in-process FG
    shaders 0x7A1E0D40-42 dropped; red note + log while the old _dlss file sits in the folder; README tells players to
    delete it); key compare 196 keys 0 unexplained; [Shogun 2 DLSS] values migrated once into the presets
    (SettingsRevision 1; s2m_migrate 4 / 4); parity_check s2 exit 0; suites PASS; tw_install.py retires the old file
    (renamed .retired-<time>). Not run in game.
    - [x] DONE 11-15 (install 08:00 printed and did the retire (.retired-20261115_080042)): Install dry run (python -I tw_install.py --helper <N> shogun2=<tag>) must print "Retired ...
      renodx-twshogun2completeedition_dlss.addon64". Owner: coordinator at the install
    - [ ] In game (ATN's list): migration keeps the old DLSS values (log line once); no red note after install; the
      game's DLSS (scale / preset / jitter / motion / LOD bias) as before without hdr_link; helper DLSS / FSR / XeSS / RR;
      FG + Reflex + limit (HDR10 while FG); RT / PT rows after restart; helper exe removed = no crash, note shows; old
      Advanced players open on Graphics 1 of 2 Owner: ATN (in game)
    - [x] SDR-to-HDR switch hashes the DLSS rows too (any changed row ends the automatic switching, as in every one-add-on
      TW game): kept, consistent with "any change ends it"
    - [ ] Port RayTracedBatchedUnits + RayTracedUnitsRange to Shogun 2 (SHARED STEP, after ATN's Attila test; Shogun's host
      has no rt_scene table). Owner: GPU round after the install
    - [ ] Cosmetic: moved files keep "[Shogun 2 DLSS]" log tags; HLSL comments "(DLSS addon)"; Custom render scale row has
      no reset icon. Owner: coordinator, release round
    - [x] DONE 11-15 (086d8d86): game-port-method SKILL.md ~line 108 says Ryse / Tales / Expeditions don't opt into fast dispatch (they do now);
      rysesonofrome_dlss/dlss_capture.hpp names the old Shogun folder. Owner: rym_ / uem_ (skill line: coordinator)
  - [x] ATN 03:4x: go through HANDOFF: leftovers vs open, tick / trace / archive. DONE 05:0x (merge of hfx_: 7115 ->
    1035 lines, 82 open, verify OK, HANDOFF_ARCHIVE.md). Was: HANDOFF AUDIT hfa_ (read-only,
    tools/tw/round_notes/handoff_audit_1115.tsv), then the coordinator applies it (archive file for history). Owner:
    hfa_, then coordinator Owner (audit 11-15): coordinator (hfa_ DONE: the TSV; applied by the hfx_ round with HANDOFF_ARCHIVE.md).
  - [x] ATN 03:3x: no time caps on rounds; night sound fine; no page-file change (system setting, ATN's). GPU ROUND 4 g4_
    (no cap) + UE SHADER CARVE uec_ (both games' plain pak data, parallel, stops on the quiet lock) running Owner (audit 11-15): coordinator: tick when g4_ and uec_ report (their work has own lines).
  - [x] merged into "ATN 03:4x: Ryse / Tales / Expeditions are IN THIS BATCH" (was: "Full Attila package for all three (memory project_nontw_full...")
  - [x] SHADER-PACK ROUND DONE (shp_, tools/nontw/shader_packs/report.md; read-only, nothing decrypted): RYSE COMPLETE
    OFFLINE: Engine\ShaderCache.pak + _FastLoad\ShaderCacheStartup.pak store their CPCK data plain (only zip headers
    encrypted), LZSS-compressed entries -> 6,421 unique DXBC (ps 5,015, vs 1,271, ds 98, hs 32, cs 4, gs 1), 5,527 NOT
    in the dumps, all 894 dumped ones included -> tools/nontw/shader_packs/ryse/cso. Names: exact for 92, families for
    the rest (entries.tsv keeps name_crc). UE: pak indexes AES-encrypted (not read); plain entry data walked
    (Expeditions 43.2 GB plain, Tales 76.4 GB plain); a partial carve of Expeditions' pakchunk0_s5 found 552 SM5 shaders
    NOT in the create-on-load dump.
    - [x] DONE 11-15 (rym_ step 5: 5,527 / 5,527 decompiled, families.tsv; the porting is its own box (Ryse MERGED)): Decompile Ryse's 6,421 (cmd_Decompiler) and map the new families (tonemap / post / HDR permutations first).
      Owner: RYSE MERGE round
    - [x] merged into "Data runs (Tales G1 + G2, Expeditions G1, Ryse R0)" (was: "Ryse runtime names + on-demand compiles in R0: r_ShadersLogC...")
    - [x] UE carve DONE (uec_, ~05:3x; tools/nontw/shader_packs/report.md "UE carve finished"): 38 s / 50 s with 8
      workers; Expeditions 54,722 unique (all 15,735 dumped included, 38,982 new + 5 DXIL libs), Tales 46,688 (its
      tonemap / LUT / post / global shaders only in encrypted entries; ATN's dump has them); NO new tonemap / LUT / PQ /
      BT.2020 permutation in either game; 74,078 new decompiled.
    - [x] DONE 11-15 (uem_ 89b01a72 logs their first draw; the check moved to "Expeditions left" (G1 data run)): Expeditions' unregistered fullscreen post candidates: the stencil / depth edge detect 0x30AD0356, 0x600BADCC,
      0xBB1E5908, 0xC3FD465B (+ MSAA) against outline00 / 01, and the composite / max / 4-texture / depth-copy ones
      (report.md): do any need HDR treatment (clamps, sRGB)? Owner: UE4 MERGE round (uem_) or its follow-up Owner (audit 11-15): UE SHADER CARVE uec_ (running): tick when uec_ reports.
  - [x] DONE 11-15 (all three ran; leftovers in the per-game boxes): Data runs (ATN allowed, scripted, fresh save backups): Tales G1 + G2 one session (Manual 03 / 04), Expeditions G1
    (Save Silanus 3m), Ryse R0 (auto-continue or chapter 3) + Nsight D3D11 capture try; after the merges + test hooks.
    Not while the TW end bench runs. Owner: coordinator
    (merged from "Ryse runtime names + on-demand compiles in R0: r_ShadersLogC...": Ryse runtime names + on-demand
    compiles in R0: r_ShadersLogCacheMisses 1, one r_LogShaders frame per level, e_PrecacheLevel +
    e_AutoPrecacheTexturesAndShaders, the 9 levels from the menu (set through the add-on). Owner: coordinator's R0)
  - [x] SDR first start -> HDR later MERGED 24cdfb9c (sdr_; ATN 02:1x YES): output_encoding keeps one global ini key
    OutputAutoPreset ("sdr <hash>" / "hdr <hash>" / "off"; FNV-1a of every non-global numeric row as the next start reads
    them); first start Windows HDR off -> SDR preset (now SAVED to the ini), on -> "off" forever, no answer -> next start
    decides; later chains: hash changed -> "off" forever, else sdr + HDR on -> Recommended once, hdr + HDR off -> SDR.
    test/tw_output_encoding PASS x86 + amd64 (5 sequences + a failing control); 12 add-ons wired (Attila "no VFX boost",
    WH3 PsychoRecommended2500, Pharaoh no-VFX-boost, Rome II Reset PsychoV35, Thrones / WH1 Recommended, WH2 / Troy / Rome
    Remastered PsychoRecommended, Ryse / Tales / Expeditions their Recommended button's body); log names [Ryse] / [Tales] /
    [Rome]; 9 TW page dumps = base except the date. Shogun 2's wiring: s2m_ (sent). Not run in game.
    - [x] DUPLICATE of "in game, do SDR first start / SDR preset / SDR output look right" in OPEN FOR ATN (11-16): In game (ATN's list): new player HDR off -> SDR preset + log; HDR on + restart -> Recommended once; HDR off ->
      SDR again; change a row -> never again; first start with HDR -> never a preset (ini OutputAutoPreset=off) Owner: ATN (in game)
  - [x] Shogun 2 build broken on atn-mods since the UE4 round (it included expeditionsrome_dlss/constant_mirror.hpp,
    deleted): fixed ea5204bb (Tales' copy, log tag "[Shogun 2 constants]"), both Shogun 2 add-ons built
  - [x] ATN 04:0x: "the whole package for all 3 games, so they get everything" (in rym_ / uem_ briefs)
  - [x] ATN 02:1x: Tales G1 + G2 in one session OK; one shared helper for all games (future DXR / feature fixes once)
  - [x] ATN 02:1x asked why not decrypt the encrypted packs: answered (no decryption = no circumvention of the games'
    protection; the same bytecode comes at run time when the game creates it, and every permutation by switching each
    setting during the data runs' dumps + UE's create-on-load cvars). Owner of the runtime census: the data runs
  - [ ] SHARED STEP owners (coordinator 01:2x): UE4 S1 cvars per-exe slot table, S2 the six duplicated headers, S4
    psycho_test31, S5 snapshot pieces, S6 page-dump drivers, S7 fast dispatch, S8 on-demand trace + test hooks, S12
    graphics-page table, S13 FG device-loss rules = UE4 IMPLEMENTATION agent ue4_ (owns src/games/talesofarise* +
    expeditionsrome*); S3 hue measurement + S11 RT route + S9 replay converter + S10 census game parts = ATN's answers /
    after G1. Ryse's offline steps = RYSE IMPLEMENTATION agent rys_ (owns src/games/rysesonofrome*; Ryse's HDR add-on
    includes talesofarise_dlss + twattila code: ue4_ rebuilds Ryse when it changes such a header). The x64 engine_memory
    shared step for TW = ATN DECIDES with the x64 render-list data runs Owner (audit 11-15): UE4 MERGE (uem_). DONE in d50bfc2c / f181c71d / 6e8f661a: S1 engine_cvars 4.21 / 4.23, S2 duplicate headers removed, S5, S7 fast dispatch, S8; S3 answered (hue by eye), S11 answered (helper + rt_core). Left: S4, S6 page-dump drivers, S12 graphics page, S13 FG device-loss; S9 / S10 after G1; the x64 engine_memory step goes with the x64 Faster Render Lists line.
  - [x] PLAN agent (done): tools/nontw/expeditions/plan.md (+
    ledgers): state, gaps vs Attila's package, shader sources (ATN's ~12000-file dumps), Nsight feasibility, game-run
    list (Tales: ATN's two newest saves), 2 add-ons -> 1, HDR-only snapshot, UE4 SHARED STEP candidates. Owner: plan agents,
    then the coordinator assigns shared owners and ATN picks the order DONE: plans tools/nontw/expeditions/plan.md (+ tales / ryse); ATN answered; order chosen (the IN THIS BATCH line).
  - [x] ATN 00:3x: Fable only when ATN asks, or with ATN's OK for an unsolvable problem / a really big change (memory
    feedback_fable_review_end.md); the TW batch's end review stays (ATN asked for it)
- [x] DONE 11-16 (END BENCH PART 1 + PART 2 done; its children have their own boxes): ATN 21:2x: ONE Constantinople bench after ALL CPU and GPU rounds of this batch (not after each): every round's
  candidates as variants against heapfix P, then the CPU set vs affinity A/B. Owner: coordinator, before the install
  - [ ] 128 torch lights' cost at night / burning city / WH3 / Pharaoh: a variant in the end-of-batch bench Owner: coordinator, end-of-batch bench (torch-128 night / burning-city variant; WH3 / Pharaoh numbers from their data runs).
  - [ ] Rarer Objects needs a view with siege towers or ships (bench scene choice) Owner: coordinator, end-of-batch bench.
  - [x] DONE 11-15 (END BENCH PART 1 (verdicts there)): Bench variants ready: tools/tw/bench/variants/cpu4_round4.json + cpu4_round4_plan.txt (P, sort, gather, x87keep,
    loopanim, memfast128; ~2.1 h for 3 scenes): in the single end-of-batch bench. If sort or gather is worse than P beyond
    the spread: take that site out of CPU_CODE_SITES Owner: coordinator, end-of-batch bench.
  - [x] DONE 11-15 (END BENCH PART 1 cpu5: better median in 5 / 5 rounds): Attila's Faster Render Lists row (Experimental, site 3) was never benched: variant cpu5_render_lists.json (P vs
    P + FasterRenderLists) in the end-of-batch bench; check the runner's multi-key "set" syntax first. Owner: coordinator
  - [x] DONE 11-15 (END BENCH PART 2 grass_dump on v161, replay + GPU on all 4 dumps: 1365 nodes, 363,404 records (6.2 MB, 6.9 padded), grid 1 MB; workers x87 0xa001f / MXCSR 0x1fa0; cell loop / DrawScrub bytes = the file past the hooks' jumps; DrawScrub bit-exact: atn 8,213 / 8,213 both dumps, max 57,288 and 57,344 of 57,366, the rest one burn step (1 / 255) newer in the game than the dumped record (burn written in battle); hook LOD = replay in all; rows = DrawScrub's part 3x4 (all 229,464 / 32,852), TEXCOORD4 = burn (+ bias), no part culled; draw camera = the scrub's; one part draw RVA 0x17f36a0 (45 parts); no LOD attachments; visits max 105 cells / 57,366 calls, atn 18 / 8,213; no-frustum mode 1 +94,648 / +7,624 parts; dump 1 vs 2: 256-269 records' burn changed (8 nodes), grid unchanged; the game's grass update 1.25 / 1.19 ms (max), 0.25 / 0.24 (atn) on the CPU vs the GPU's mode 0 0.020 / 0.011 ms (bit-exact to the replay), mode 1 0.042 / 0.012 ms): GRASS: one Grass Dump from a real battle = bench probe variants/grass_dump.json (scenes max, atn; it points at
    the grass worktree's build: repoint it at the merged build), then grass_replay.py on its files (12 points, design.md
    section 8). Owner: coordinator, end-of-batch bench
  - [x] DONE 11-15 (END BENCH PART 2: blas 1.95 -> 1.78 ms cluster build, trunc8 1.58 (ATN DECIDES)): g2b_ bench variants g2b_clusters.json (g2b-base / g2b-blas / g2b-trunc8, scene max): end-of-batch bench. g2b-trunc8
    is a look change (8-bit truncation): fps only, ATN DECIDES. Owner: coordinator
  - [x] DONE 11-15 (END BENCH PART 2: pick +2.3 -> +0.64 ms GPU; skipwork CPU busy -1.8 / -2.3 ms, fps unchanged): g2a_ bench variants g2a_torch.json + g2a_shadow_skip.json (copy attila_pass_timing.txt after each start, read with
    tools/tw/preset_audit/g2a_tep_gaps.py); their add-ons in testbin/g2a_bench. Owner: coordinator
  - [x] DONE 11-15 (END BENCH PART 2: record bounce -0.22 / -0.24 ms, Lean -0.09 / -0.16 more): Bench variants g3b_reuse.json + g3b_round3b_plan.txt in the end-of-batch bench. Owner: coordinator
  - [x] DONE 11-15 (superseded by g4_capture.json (part 2)): Bench variant g3a_capture.json (+ g3a_round3a_plan.txt; add-on testbin\g3a_bench\capture). Owner: coordinator
  - [ ] After everything: one A/B of ATN's CPU set rule against an affinity rule of the same 16 CPUs (ATN switches the
    rule; we measure). Owner: the bench, ATN's OK at the time
  - [x] DONE 11-15 (not run: superseded by the g2b2 / g3b2 helpers (end_of_batch_1115.md "Optional")): Bench variants: gpu_units_range.json, gpu_probe.json (answers c, k, h, j, b in game), gpu_reuse.json; plan
    tools/tw/bench/gpu_bench_plan.md. The bench must copy tw_rt_profile.csv, tw_rt_profile_helper.csv and
    attila_pass_timing.txt out after each start (it doesn't yet). Owner: coordinator before the end-of-batch bench
    - [ ] caster_census.py for Feral's engine (a depth-prepass scene_draw host, VSMain entry points, Feral's shader names)
      so parity_check settles Rome Remastered's casters itself. Owner: coordinator, after parity round 2 (shared script)
- Was: SHARED STEP agent started (shs_): torch rows from PT for 8 games via graphics_page (Pharaoh's particle lights too), GS
  capture path in rt_core (WH1 / Pharaoh / Troy / WH2 imposters), tessellated meshes via patch_triangles (WH1 / Troy / WH2),
  per-game leaf cut-off through the bridge (Troy 0.01), Faster Smoke Build row to the other smoke games, smoke without rays
  for 64-bit games if small.
- [x] ATTILA GAPS ROUND 2 MERGED 3a391fa4 (fast-forward): Smoke Shadows + Particle Resolution at Shader Model 4 / 4.1 (VS
  A3966664; rt_scene's particle lists shared by Attila and Thrones), Reflection Quality + FidelityFX SSSR in ssr02-07 (sm4 /
  4.1 and the game's MSAA builds), Thrones' 11 vfx builds as wrappers; tw_attila_builds 17 / 17 + 14 / 14, tw_thrones_ssr
  PASS, page dumps a / th / t differ by the date only. Default change: sm4 / 4.1 players get FidelityFX SSSR. Agent DONE.
  - [ ] D9123DE1 + its MSAA / stencil builds (constant tone mapper "with_bloom_and_gamma"): NEEDS A TRACE of that mode.
    Owner: the next data run or ATN's trace.
- [x] ATN 14:3x: "improve the author's code / any other fmod / anything else on the game's side?" heapfix's own code stays
  its author's (own use only); our own detours on the game's functions (tools/tw/cpu_profile/report.md sections 2 and 6)
  were in no brief (the Nothing dropped miss). Owner: cpu_ agent, round 2 (sent 14:3x), each bit-exact offline, Experimental: DONE: children settled: done, or dropped with numbers; Faster Render Lists 438c060f; radix sort c2cd9e90.
  - [x] ALREADY DONE 11a0b65c (Faster Game CPU Code; my list was wrong): G2a software float->half 0x169a220 (98 sites; 4.3 ms workers, ~0.7 render at grass): bit-exact over all 2^32 inputs
  - [x] ALREADY DONE 11a0b65c: G2b CRT fmod 0x1a05cc0 (x87 fprem, 81 sites; 1.6-1.9 ms workers, 0.3 render)
  - [x] U3 only the heapfix-aware variant (the plain patch is in 11a0b65c): x87 truncation block +0x17f6515 (4.1 ms workers at 8x, 0.5 render; heapfix's live bytes checked first) OBSOLETE: DROPPED with numbers: heapfix's form already drops both fldcw, <= 0.1 ms on heapfix's bytes; see the author question in the "Author packages" line.
  - [x] U5 animation pack loop 0x182e080 (2.29 ms render thread at 8x) OBSOLETE: DROPPED: heapfix's loopfetch has a prefetch site there, disabled by its author; see the author package b_loopfetch_animation (c2cd9e90) in the "Author packages" line.
  - [x] render-list append, lock xadd per item 0x17ae370 / 3d0 / 420 (6.2 ms workers, 0.9 render at 8x; research first) DONE: push 0x176ced0 (callers 0x17ae370 / 3d0) = Faster Render Lists 438c060f (tools/tw/perf_plan/render_thread_tail.md); header layout WON'T (cpu5b_report.md).
  - [x] the game's introsort 0xda6b0 / 0xda4b0 (3.5 ms workers at maxed grass; only with an equal-key order proof) DONE: radix sort site 5 c2cd9e90 (tw_engine_code part 9, plain 4-byte values).
  - [x] ALREADY DONE for float->half / fmod / x87: Rome II 0246a786, Thrones 2c89ced4 (the rest: with each item)
  - [x] DONE 17:57 (hfp_): P = prefetch-only sort WINS where the CPU sets the frame: ATN's settings -0.7 to -2.0 ms median
    (every P start faster than every A start; p99 -1.5 to -2.8), CPU-bound -2.6 / -2.8 ms (+7 % fps), maxed CPU-bound
    -3.2 / -3.7 ms (+8 %, p99 -4.2 / -5.2); maxed with ATN's rays (GPU / helper-limited) no change (fps and mean equal,
    median +0.5 / +0.9 in a two-cluster distribution, p95 / p99 slightly better). B (parallel) helps less than P everywhere.
    WPR: the render thread waits at the workers' join 1.96 ms (A) / 1.37 (P) / 1.70 (B), 75 % ended by a sorting worker.
    battle_draw_batching off: +17.6 to +21.3 ms: keep on. number_of_threads 8 best (6: -7 % fps, 4: -20 %). Whole heapfix
    test set + eq_test_pf (3,528 sorts, stress, page-end lists) pass for A / P / B. 48 starts valid, ATN's files back
    (hashes equal). Package for the author: tools/tw/heapfix/for_author_pf/ (NOTE.md, sortfix_prefetch.patch,
    eq_test_pf.c): ATN sends it, never us. Runs: tools/tw/heapfix/ab_runs_pf/ (summary.txt per scene; 3 WPR ETLs of
    ~1.1 GB each to delete when done).
    Original plan: APPROVED by ATN 14:5x, owner hfp_ agent (game runs, Constantinople auto_save.replay, camera moved left at the start
    so artillery, units, walls and shots are on screen): heapfix A = 7.0, P = prefetch-only sort, B = last night's parallel
    sort, interleaved A,P,B in three scenarios (ATN's settings GPU-bound; CPU-bound rays off + Ultra Performance; everything
    maxed in view: 8x units + the game's max unit LOD / buildings / grass / model LOD); then U4 number_of_threads 8 / 6 / 4
    (A only, CPU-bound). The winner's patch + numbers go to tools/tw/heapfix/for_author_pf/ for ATN to send (never by us).
    Quiet machine during measurement: tools/tw/quiet_run.lock (all running agents told: no builds / WARP / GPU jobs while it
    exists) + gpu_job.lock + no compiler processes before each start.
- [x] DRAW REDESIGN MERGED 63d17575 (one include conflict in Shogun 2: both kept; after the merge both SKILL.md build
  lines + tw_draw_dispatch x86 / amd64 rerun). Fixes of the Fable review: 3166f59f (1: late Unregisters move the epoch;
  suite case 8 with a partner DLL), ef1af8e5 (2: fast::Invalidate from view_share; case 9), f68e514b (3: only shader
  pipelines move the epoch; state objects in-frame +0.035 ms -> noise), 0122d5c7 (4: rt_scene::OnAnyDraw gated, a log
  line when an un-opted module turns the fast path off), ac649f09 (5: style + the SKILL.md row). Oracle 832 runs / 2.05 M
  commands 0 differences; dispatch 0.575 -> 0.146 ms (cpl1), 0.700 -> 0.211 ms (cpl2) in the replay; page dumps date
  only. Left: the rt_scene gate isn't in the oracle (no RT replay registers OnAnyDraw); no in-game run (the final bench).
  Was: DRAW REDESIGN phase 2 DONE on drw-draw-redesign (6f2d8dea utility + test/tw_draw_dispatch, 5a00df0a every TW add-on
  opts in with gates, 1a4ed7c7 cost pass; not merged): command_action_fast.hpp per-command-list cache, TW-only opt-in,
  other mods unchanged. Replay oracle 832 runs / 2.05 M commands 0 differences; tw_draw_dispatch x86 + amd64 0 differences
  (control fails); 14 TW suite runs PASS; both build lines + Ryse / Tales / Expeditions / R.U.S.E. build; page dumps date
  only. Saves 0.43 ms (cpl1) / 0.54 ms (cpl2) of dispatch a frame in the replay.
  - [x] FABLE 5.2 HIGH review DONE (tools/tw/draw_redesign/fable_review.md; read-only, nothing built: quiet lock): no
    reachable crash / wrong picture / leak; callback set and order match RunCallbacks everywhere walked. Findings:
    (1) latent crash: Unregister after a module's detach invisible to other modules' caches (not reachable with ReShade's
    unload order); (2) theoretical race: a gate's pipeline answer cached before view_share saw the pipeline; (3) perf: the
    epoch moves on every state-object init / destroy; (4) rt_scene::OnAnyDraw ungated, a log line when a module isn't
    opted in; (5) style + a skill row. Suite gaps: null hook, two modules, threads, epoch inside a frame.
  - [x] drw_ agent fixed 1-5 + the suite gaps; merged
  - [x] rt_scene::OnAnyDraw gated (0122d5c7)
  - [ ] render_pass_behavior / shader_injection_behavior suites: ATN 02:2x OK to download, but ATN plays on ReShade
    6.8.0 (the repo's SDK submodule is 6.7.3): run them against ATN's installed 6.8.0 DLLs (32-bit from Attila's folder,
    64-bit from a 64-bit game's folder; copies into testbin, no download). Owner: coordinator, tools pass
  - [x] The agent started 6 amd64 replay builds early while quiet_run.lock existed (the hfp_ A/B window): every hfp_ start
    was checked for compilers and the A-to-A spreads stayed small; the 16-thread run re-measures P anyway OBSOLETE: a note, not a task: the 16-thread batch re-measured P anyway; see 16-CPU BATCH DONE 21:03.
- [x] ATN ~19:3x RULE SET (CPU Set Setter: a CPU SET, not affinity, of the first 16 logical CPUs = V-cache CCD with SMT):
  hfp_ told GO. ATN's policy for the CPU fix: everything that makes the game faster is wanted; whatever changes the
  author's code or files goes to him as ready-to-review code + notes (never shipped by us); game code his fix doesn't touch
  stays in our add-on (and he gets a note so nothing collides). Owner of the author-side patches: CPU ROUND 4 (memfast
  65-127 B, his disabled loopfetch prefetch site in the animation pack loop, the x87 block over his bytes), each with tests
  and a bench A/B against P. DONE: author-side patches built in CPU ROUND 4 c2cd9e90 (testbin/cpu4_heapfix a / b / c); the send = the "Author packages" line.
  - [x] 16-CPU BATCH DONE 21:03 (bench tools/tw/bench/constantinople_bench.ps1, every start valid, files restored 0
    mismatches): CPU set 0-15 + number_of_threads 16 WINS: CPU-bound t16 33.66-34.23 ms vs t8 34.65-34.99 (spread 0.34);
    maxed CPU-bound 39.69-39.90 vs this morning's 8-core 42.0 (23.7 -> 24.8 fps); ATN's settings unchanged (GPU / helper
    limited, ~31 fps). Parallel sort B2 / B4 / B8 slower than P everywhere (1.3-2.3 ms CPU-bound): P stays the only
    author proposal (for_author_pf unchanged). The bench is finished and reusable (variant JSONs in tools/tw/bench/variants).
  - [x] ATN: keep the 16-CPU rule; set number_of_threads 16 in preferences.script.txt (ATN or coordinator with a fresh
    backup, ATN's choice) DONE: number_of_threads 16 (preferences 559f4696 -> 432ba846, backup attila_prefs_backup_20261114_211544); rule 0-15 stays.
  - Was: When the 16-thread batch ends: TELL ATN the verdict right away; if 16 threads is slower than today's 8-core
    numbers, ATN switches the rule back to the 8 V-cache cores (one thread a core)
  - (moved under "ONE Constantinople bench": After everything: one A/B of ATN's CPU set rule against an a...)
- [x] ATN 19:xx: (1) a 16-thread test: ATN sets CPU Set Setter's Attila rule to the V-cache CCD's 16 logical CPUs (now 8
  cores), then CPU-bound P with number_of_threads 8 / 12 / 16, and P vs B4 vs B8 (sort threads) at the best count in the
  three scenes. hfp_ agent preparing (variants, tests, runner); NOT started until ATN confirms the rule. (2) The same siege
  benchmark after every CPU / GPU optimization round, heapfix P as the baseline: tools/tw/bench/constantinople_bench.ps1
  (hfp_ preparing); first use = the end-of-round build (new Experimental CPU rows off / each on / all on). (3) Ideas that
  belong in the author's fix (ATN decides what to send): a radix sort for the game's plain 4-byte introsort 0xda6b0,
  memfast extended to 65-127 B copies, and a note that our Faster Render Lists patches +0x176ced0 (so his fix doesn't
  collide). Owner: coordinator collects with the package. DONE: (1) 16-CPU BATCH DONE 21:03; (2) constantinople_bench.ps1 reusable; (3) radix sort c2cd9e90 + the author packages ("Author packages" line).
- [x] Audit agent (aud_, read-only, ATN 14:5x "did the same happen to Nsight?"): every actionable line of the 2026-11-13 / 14
  analysis reports (Nsight, CPU / GPU recordings, heapfix, draw redesign cost map, Rome Remastered hunt, round notes) against
  HANDOFF, commits and the running agents' scopes; report tools/tw/round_notes/audit_1114.md. Each MISSING line becomes a
  checkbox here with an owner.
  DONE: 125 rows: done 29, owned 32, maintainer 17, dropped on purpose 13, missing ~33 distinct. My round-2 CPU list held
  finished work (fmod / float->half / x87 / G0: 11a0b65c, f158b659): the CPU brief is corrected. Owners of the missing: DONE: tools/tw/round_notes/audit_1114.md; the missing lines got their boxes below.
  - [x] cpu_ agent, round 3 (sent): the workers' unnamed 36.8 ms, memcpy / memset 4.44 + 1.08, frame allocator 2.40 + 0.39,
    soldier update 2.87 + 0.45, render-list execution 1.35 render, battle draw loop 0.67 (cpl2 pickles); off-screen soldiers
    keep their last pose / far soldiers half-rate animation (Experimental); unit-only mesh LOD (count parts per LOD first) DONE: CPU PERF ROUND 3 438c060f + render_thread_tail.md; pose hold DROPPED with proof; unit-only mesh LOD -> the "ATN DECIDES (data runs)" line.
  - [x] CPU PERF ROUND MERGED 05522f9b (agent stopped at 17:00, quiet lock held since 15:07): Skip the Game's Shadow Map
    Work (RayTracedSkipShadowPass, Experimental, off, Attila + Thrones: the model manager's sun-map pass kinds learned in
    game, one call a frame kept in turn; E 1.5-3.5 ms render thread when CPU-bound; tw_unit_shadows part 7) and the grass
    scrub per-cell radius (on, bit-exact; G1 visits -71 %; tw_scrub_lods section 5). Report:
    tools/tw/perf_plan/render_thread_tail.md. Its leftovers, each owned by the cpu_ agent resumed AFTER the hfp_ window:
    - [x] the 15 x64 consumer builds after the shared rt_scene accessor (scratchpad cpu_work\cpu_build64.ps1) + Thrones'
      page dump (coordinator, if the agent isn't resumed first) DONE: 5e1aa6ee: the 15 x64 consumers built, Thrones page +1 row.
    - [x] the tail script (cpu_tail.py cpl2 / units): memset callers (4.0 ms workers), the introsort's callers and
      comparator, frame allocator, the 8x pose evaluator, per-LOD part counts DONE: settled by CPU round 3 (render_thread_tail.md).
    - [x] 0x176ced0 per-thread vector push (6.2 ms workers + 1.0 render): false sharing of 8 threads' 12-byte headers;
      next: the readers of list +0x18, then a layout patch DONE: Faster Render Lists 438c060f (ac7858da); header layout WON'T (cpu5b_report.md).
    - [x] ScaleModelLods' cache check merged into the key loop (~2 ns a call) OBSOLETE: measured, no gain, not kept (CPU PERF ROUND 3); see the CPU round 3 result line ("Measured and not kept").
    - [x] Rome II: the shadow-pass skip (its Render pattern differs: find its pass-kind switch near the soldier draw) DONE: 438c060f Rome II Skip the Game's Shadow Map Work (+0x14b84a0).
    - [x] 64-bit games: the learned shadow-pass skip on their model manager's Render (pe_func.py), and a ScrubUpdate
      equivalent by its tweak string (WH3: GPU grass, nothing) OBSOLETE: no Render / scrub hook found statically (CPU round 3); see the "ATN DECIDES (data runs)" line (WH3 stack capture).
    - DROPPED with numbers: U3 heapfix-aware (heapfix's form already drops both fldcw; <= 0.1 ms on heapfix's bytes); U5
      (heapfix's loopfetch has a prefetch site there, disabled by its author: a note for the author package)
    - [x] DUPLICATE of "Skip the Game's Shadow Map Work: let the end bench take the numbers" in OPEN FOR ATN (11-16): ATN tests (README lines): Skip the Game's Shadow Map Work off / on in cpl1's view, Time Each Pass x3 + PresentMon
      30 s, cut-outs and unit shadows; G1 grass maxed against the previous build. Numbers (END BENCH PART 2 g2a_shadow): CPU busy -1.8 / -2.3 ms, frame unchanged. Owner: ATN (look).
  - [x] CPU PERF ROUND 3 MERGED 438c060f (one conflict: Attila's torch row floats, moved to graphics_page by the shared
    step, dropped; x86 three + 8 x64 TW add-ons rebuilt after the merge): the 15 x64 consumers built (5e1aa6ee), Thrones'
    page +1 row; Faster Render Lists (Experimental, off, Attila + Thrones: the push writes its shared "has items" byte only
    when not 1; tw_engine_code part 8 bit-exact over 1 M pushes + 720 M threaded; 94 / 84 -> 44 / 31 ns a push; E 2-3.5 ms
    workers, ~0.4 render in cpl2); Rome II Skip the Game's Shadow Map Work (Experimental, off; its one-argument Render
    +0x14b84a0); research doc tools/tw/perf_plan/render_thread_tail.md (the "memset" is memcpy 32-127 B; introsort sorts
    plain 4-byte values; the 8x tail is the battle simulation). Measured and not kept: ScaleModelLods' cache check (no
    gain). The x64 games: no Render / scrub hook found statically. The earlier leftovers above are settled by this round.
    Its leftovers, owner CPU ROUND 4 (a fresh agent after parity round 2):
    - [x] render-list headers 64 bytes apart (~1-2 ms workers): find where the header array is allocated (readers
      0x176ced0, 0x178fd90, 0x176d430, 0x176da30, 0x176dbf0, 0x176ddb0) OBSOLETE: WON'T: the header vector is shared generic vector code (tools/tw/round_notes/cpu5b_report.md); see "Render-list headers 64 B apart: WON'T".
    - [x] radix sort at +0x17b9217 for 0xda6b0's plain 4-byte values (0.5-1 ms workers, ~2 ms at maxed grass): a harness
      test against the game's own sort first DONE: c2cd9e90 site 5 (545 -> 45 us).
    - [x] prefetch in the 0x182c990 gather loop (<= 0.3 ms): harness timing first DONE: c2cd9e90 site 6 (0.661 -> 0.340 ms cold).
    - [x] 8x pose hold / half rate: read what 0x49aad0 writes (pose or simulation) before any row OBSOLETE: DROPPED with proof: 0x49aad0 is simulation, not pose; see the "ATN DECIDES (data runs)" line (bone-matrix writer 0x17dab80 only with a data run).
    - [x] the animation pack loop 0x182e080: read its loop DONE: read in CPU ROUND 4: author package testbin/cpu4_heapfix/b_loopfetch_animation (c2cd9e90).
    - [x] DUPLICATE of "allow data runs for unit mesh LOD part counts" in OPEN FOR ATN (11-16): ATN DECIDES (data runs): mesh LOD part counts; the x64 shadow-pass skip (WH3 first: one stack capture a frame
      while the sun's map is bound) Also the 8x pose hold's bone-matrix writer 0x17dab80 (only with a data run). Owner: ATN.
    - [x] merged into "Author packages" (was: "ATN, when sending the heapfix package: ask the author whethe...")
    - [x] DONE 11-15 (Faster Render Lists on by default, 356b7100; Rome II's Skip the Shadow Maps stays for ATN): ATN tests: Faster Render Lists off / on in cpl2's view (PresentMon 60 s, log "Faster Render Lists: on"); Rome II
      Skip the Shadow Maps in a forest battle (log "pass kind N draws only into the sun's map", shadows unchanged) Owner: ATN (look) / end bench (numbers).
  - [x] hfp_ agent: heapfix's whole test set + eqtest on every packaged variant; battle_draw_batching A/B (keep it on)
  - [x] GPU PERF ROUND MERGED 75773bfb (clean; every consumer rebuilt after the merge): b6ec6be6 (a) Rome Remastered
    reset suspect 3 was REAL: an index rewrite under the same capture key kept the old cluster set (control: set A used 60
    frames after B); index writes now drop it until rebuilt (tw_cluster_rewrite PASS x86 / amd64; whether Rome Remastered
    rewrites IBs needs a trace); ec1b443a (b) Units Ray Tracing Range (RayTracedUnitsRange, Experimental, 0-500 m; bridge
    flag RT_UNIT 2048 + Shared::rt_units_range in v7's padding, VERSION 31; 4K harness clusters / scene+trace ms: off
    2.10 / 4.55, 200 m 1.07 / 3.49, 100 m 0.66 / 3.04, 50 m 0.47 / 2.81; row 0 byte-identical; all_modes 179 / 179,
    fg_bridge_test OK); fb93c061 pass split + light_order in tw_rt_profile.csv. Helper built only (gpu1 1710ACC85B2B).
    - [x] merged into "Lean Reuse row" (was: "(f) Lean = 3 neighbours + radius 6: 0.96 -> 0.85 ms, rRMSE 2...")
    - (moved under "ONE Constantinople bench": Bench variants: gpu_units_range.json, gpu_probe.json (answer...)
    - [x] Not done, owner NEXT GPU ROUND: (d) WH3 grass read-back without the wait (terrain_count_later pattern);
      (e) per-texel surface record and (g) f16 reservoirs / torch U2 history (after the Lean verdict); (i) per-draw
      leftovers after 63d17575; (l) J2 / J3 / A32, the helper's commit under Nsight (J9 answered: no change); (m) the
      scorer's smoke / fog / reflections / water scenes, history off, RR 310.9; Rome Remastered IB-rewrite trace;
      the look-changing savings (ATN decides) OBSOLETE: split into later boxes: (d) -> WH3 grass read-back line, (e) DONE 0b50ca87, (g) f16 NOT TAKEN / torch U2 -> U2 step 3 line, (i) -> Resource upgrade line, (l) J2 NOT TAKEN, J3 / A32 -> the J3 and A32 lines, (m) + RR 310.9 -> the Scorer and DLSS RR 310.9 lines, look savings -> Look-changing GPU savings; see those lines + the Rome Remastered IB-rewrite trace box below.
    - [ ] Rome Remastered IB-rewrite trace (whether Feral rewrites index buffers, b6ec6be6; from the NEXT GPU ROUND list). Owner: the next Rome Remastered data run (coordinator)
    Was: GPU PERF ROUND STARTED ~22:00 (gpu_ agent, base cdfae24e; no game runs: its bench variants go into the single
    end-of-batch session). Plan: (one agent after the drw_ merge; helper / rt_core; ATN 21:xx: measure at the bench's stopped view,
    Constantinople after the camera move: Time Each Pass (One Frame) + a frame trace there in game, the helper's D3D12
    work from that view replayed in the 4K harness for Nsight (Attila's own D3D11 can't be captured: 32-bit)): (a) FIRST Rome Remastered reset suspect 3 (a
    cluster set kept after the game rewrites an index buffer under the same key: offline check); (b) units ray-tracing
    range row (Experimental; cluster pass 1.8-2.58 ms GPU over 18.7-22.6 M soldier triangles; harness GPU Trace with
    HARNESS_COMPACT_STRESS and a distance cut); (c) the game side of the three ray hand-offs (~1.3-1.7 ms; cpl2 pass_timing
    per command); (d) WH3 grass read-back without the wait (>= 1.2 ms in grassy sieges; smallest step in lines 34-36);
    (e) bit-identical ReSTIR reuse from a per-texel surface record (~0.7 ms); (f) score Lean + 3 neighbours (0.98 -> 0.69 ms)
    and radius 6; (g) f16 reservoir layout, torch U2 Half while moving (11.5 % vs 5.2 % rRMSE), a separate light reservoir;
    (h) do light draws repeat frame to frame (the -1.4 ms sort reuse); (i) per-draw leftovers (resource upgrade hash set
    0.196 ms, 2461 skipped caster commands); (j) game passes doubled under RT/PT (~0.18 ms); (k) the add-on's own GPU passes
    0.48-0.53 ms; (l) PT GI fallbacks, grass specular into the GI cache, OMM transient pools, helper commit under Nsight;
    (m) quality scorer: a smoke / fog scene (Faster Smoke Shadow Build unscored), reflections / water, history off, RR 310.9;
    ATN 21:xx: look verdicts by score wherever a reference exists (ray effects against a many-sample reference without the
    denoiser; "as vanilla" rows against the game's own picture), so ATN judges only taste rows (Hue Clip, look sliders)
  - [x] GPU GRASS (perf_plan G1; one agent after cpu_ ends: Attila's grass code): phase 0 = a Grass Dump button +
    grass_replay.py; estimate -5 to -10 ms render thread, -20 to -37 ms workers at maxed grass when CPU-bound DONE: GRASS phase 0 merged 7e668fe7; follow-ups = the two GRASS lines (probe, after the probe).
  - [ ] Look-changing GPU savings (half-rate rays for grass / far soldiers, float16 animation pages; 0.2-0.6 ms each):
    Experimental rows, owner the GPU PERF ROUND after (a)-(m) Owner (audit 11-15): GPU round after the end bench; ATN places the rows.
  - [x] DUPLICATE of "one cascade instead of two under Rays Replace" in OPEN FOR ATN (11-16): ATN DECIDES: one cascade instead of two (after ATN's Shadows Medium A/B; ATN 15:xx: not tested yet); the grass
    settings note text Owner: ATN
  - [x] ATN 15:xx YES: WH3 own ReSTIR reflections (~0.3 ms cheaper, picture differs) as an Experimental row, off. Owner:
    PARITY ROUND 2 WH3 DONE: WH3 17efac2b: da965fc9 Faster Reflections (Experimental, 1.35-1.42 -> 1.06 ms).
  - [x] DONE 18:07: 6 lines removed from [renodx-preset1] (ReShade.ini f0c01bd0 -> 397a2141; fresh backup
    tools/tw/attila_ini_backup_20261114_180736). Was: ATN 15:xx YES: remove the dead keys GraphicsModelDetail / GraphicsUnitDetail / GraphicsUnitFade* from ATN's Attila
    ReShade.ini (twattila reads none of them; other games' add-ons do, their inis stay). Owner: coordinator, AFTER the hfp_
    batch (its runs restore their own ini backups); fresh backup, edit, hash noted
  - [x] HAGS: stays on (ATN: FG and MFG need it); no measurement, never suggest it off
  - [x] merged into "Unit round" (was: "Unit round D (WH3 cross-pass unit capture key) and E (TBB gr...")
- [x] ATN 14:2x: the lessons of the last 16 h into the skill (tools/tw/round_notes/lessons_1114.md, 24 lines): SKILL.md
  "Nothing dropped" rule (every open line of a report = a checkbox with an owner; SHARED STEP; parity gate), Finishing a
  round + release.md 5 / 7 / 8 (Fable at the end, parity, reports read to the end), rows in testing / helper-fg-stability /
  rt-scene / shaders-ui / bringup.
- [x] parity_check agent DONE: script merged 8da262d3 (cherry-pick of ab82b04b); tools/tw/page_dump/dump_any.py; ledgers
  tools/tw/parity/<game>.tsv (seed_1114.py overwrites them: edit the seeder's EXTRAS or the ledgers after the last seed);
  report tools/tw/parity/report_1114.md, full output check_pc1114.txt. At 3a391fa4 open / waiting / settled: Attila 0/2/16,
  Thrones 2/10/46, Rome II 19/5/55, Troy 4/10/135, WH3 3/87/112, WH1 2/12/89, WH2 7/8/75, Pharaoh 45/13/54, Rome Remastered
  87/17/179, Shogun 2 16/12/216. Owner of every open line below: PARITY ROUND 2 (one agent per game, after the shs_ merge
  and the hfp_ quiet window), then parity_check again until it exits 0:
  - [x] RayTracedShadowCascades (Two Shadow Cascades Under Rays): missing in all 9 games (needs each game's cascade value) DONE: d99b3a10 Rome II, aa3468a9 Thrones / WH1 / Troy / WH2, 08396134 Pharaoh; WON'T with proof WH3 / Rome Remastered / Shogun 2; parity_check 0 OPEN in all 10 games.
  - [x] RayTracedUntracedCasters (Rarer Objects in the Rays): missing in all but WH2 (each game's tessellated / rare casters) DONE: 12bc7a08, f8a6353d, aa3468a9, 08396134, ec83dc9c; Rome Remastered HAVE (RayTracedBattleScene); parity_check 0 OPEN in all 10 games.
  - [x] Troy: RayTracedBannerLight, RayTracedSheetsUnselfed (no verdict in its table) DONE: aa3468a9: parity_check t exits 0.
  - [x] WH3: VfxFullResolutionEdges (DO NOW, stopped with numbers) + 2 more open lines DONE: 17efac2b (6f4c5065 Particle Edges by Depth): parity_check w exits 0.
  - [x] WH2: SmokeShadowMapStrength / Softness and ropes 0334E480 / 9ECDC023 / C2CA1469 / F75E8029 ("LATER (shared step)",
    no owner: now PARITY ROUND 2 WH2) DONE: aa3468a9 (ropes in WH2): parity_check w2 exits 0.
  - [x] Rome II: GrassHeightDistance, ssr_rays, SSRDenoiser, the 3 Vfx particle rows; casters 2C98F649 / 8C68069B / B1C7E29F /
    E51A2D2A; feature lines from the pre-round survey re-marked (PsychoV35 hue, SDR preset, RT/PT, DrawScrub, soldier
    shadow distance) DONE: b3d7e1b6 + f4f39372 (SSR rays / denoiser / grass height WON'T with proof): parity_check r2 exits 0.
  - [x] Pharaoh: GrassHeightDistance, RayTracedBannerLight, the 3 Vfx particle rows; 34 NOT HELD casters without a verdict;
    survey feature lines re-marked; scale-census BUG rows re-checked DONE: 08396134: parity_check p 0 open.
  - [x] Rome Remastered: 85 NOT HELD casters without a verdict (census over traces never done), the 2 new rows DONE: 1c4f8f36: parity_check rr 0 open (85 casters: 29 HAVE / 33 trace / 23 WON'T).
  - [x] Shogun 2: its table's DO NOW (Sharpening + Extra Sharpness; SSS softness / Coloured / banner light / distance
    softness / Debug View; GT-VBAO temporal / edge fix / denoise; Full-Resolution Edges, Smooth Half-Size particles) DONE: ec83dc9c + 8a21362e; Sharpening / Extra Sharpness WON'T with proof; parity_check 0 OPEN in all 10 games.
  - [x] Thrones 2, WH1 2: the open lines in report_1114.md DONE: aa3468a9: parity_check th w1 exit 0.
  - [x] DONE 11-15 (final_build_1115.ps1 07:20 + rfx_ suites): Final verification: rebuild the main checkout's build / build32 (stale headers: Attila, WH1, Rome Remastered), then
    parity_check at the merged HEAD exits 0 before stamping
    (merged from "Build both lines + tw_engine_code on the merged tree when th...": Build both lines + tw_engine_code on
    the merged tree when the quiet lock is free (coordinator))
    (merged from "Rebuild every add-on using dlaa_bridge.h (x86 + amd64) on th...": Rebuild every add-on using
    dlaa_bridge.h (x86 + amd64) on the merged tree. Owner: coordinator, before the bench)
- [x] Ryse / Tales of Arise / Expeditions: Rome: old builds; their own planning round with the whole current method. ATN
  picks the time (tonight / tomorrow, or Monday after the Ultra reset). Not started. OBSOLETE: superseded: plans done (TALES / RYSE / EXPEDITIONS PLAN DONE); see "ATN 03:4x: Ryse / Tales / Expeditions are IN THIS BATCH".
- [x] Rome Remastered parity round MERGED c0a61b2d (table tools/tw/romeremastered/parity_1114.md: 47 features, 13 have / 3 do
  now left / 6 trace / 24 won't; credits, Bend rows + Softness, GitHub button; no PT GI -> no torch rows). Input bug (task B):
  study only: the wall clock is a seconds-since-2001 double; obfuscated code at 0xbbaa60 reads GetMessageTime; the
  RunAsDate cure fits a date-gated check in protection-like obfuscated code, not the input code; call site unproven; not
  patched (no circumvention of protection code). Risk: our Resize detour could trip such a check: ATN watches input after a
  long session. Agent on task C (rays + DLSS below 100 %).
- [x] Shogun 2 parity round MERGED 5e4e2d80 (table tools/shogun2/pkg_1114/parity_1114.md: 43 have / 23 do now (8 done) / 5 trace
  / 13 won't; FG HDR read the wrong key (fixed), Softer water reflections, Smooth banner shadows, Grass shadow range, AO under
  RR, 5 Bend rows, casters out of view: far tree cards 30C453EE, units A22FA941 ("too big" was a counting error: 1.18 M
  corners), naval 7B067623; page dump only the 9 new rows; WARP PASS). Round 3 MERGED 48ddae02 (RT/PT = Attila's incl.
  LightGlow / Caustics / Clarity, smoke shadows via a quad GS for BEA3F2D4 (Experimental), water rows at low / medium water
  quality, 5 casters out of view + 3k far cards, engine-name reader (8 tweaks, 5 Experimental rows, suite tw_shogun2_tweaks
  built in testbin); finding: a global built at start-up from Attila's header globals came out empty under clang-cl (static
  init order); the other add-ons call engine_tests::Attila() inside functions (checked). Left: SSS Softness (second UAV),
  particle smoothing, CAS sharpening, AO temporal. Shogun 2 agent DONE. Round 2 MERGED 16126f41 (naval sails / symbols / ropes
  out of view, Water Clarity + Caustics, Modded Auto Exposure (GPU 8233E11F), Indicator Brightness, torch light in fog,
  campaign overlay D7E27B35 below 100 % fixed (scale census), Gamma = WON'T (in-game Gamma reaches every mode)). Round 3:
  RT/PT = Attila's (LightGlow, WaterCaustics), smoke GS for BEA3F2D4, engine-name reader, SSS Softness pass.
- [x] WH2 parity round MERGED 4ded8d0d (table tools/tw/wh2/parity_1114.md: 207 have / 4 do now left / 7 trace / 55 won't; 19
  dead engine rows removed (0 code uses, 0 pointers), 5 Attila tests, Bloom, Indicator Brightness (Experimental), smoke
  twins DDABB799 / 687378DB, 4 casters out of view (level B); census held 26 -> 30; no shared change). Agent resumed:
  fog_strength, GrassHeightDistance, 4 units / props captures, 7DF335FF campaign banners (opt-in shared GS define).
  Round 2 MERGED 316aa162 (Fog Strength + Grass Height Distance incl. untraced permutations, Depth of Field, Fog & Sky
  Smoothing (legacy fog), Indicator Brightness x3, Rarer Units and Props in the Rays (Experimental), Smooth Banner Shadows;
  shared opt-ins rt_core SceneShader::held_row + GS RT_KEEP_IN_VIEW; held 35 -> 44). GS comment conflict resolved, all 11
  variants recompile to their headers. WH2 agent DONE. Left: VFX rows (snow / rain trace), smoke without rays (shared 64-bit
  host), ropes (sun-facing flag), GS sprites 370A7440, tessellated 4CC2D68F (shared step).
- [x] Rome II round 2 MERGED f4f39372 (water reflections 7C9C8ECF fixed below 100 % + Reflection Quality, suite
  test/tw_rome2_shaders; Light Through All Banners (Experimental; material 29 = the 4 army_banner_flag models; the traces draw
  banners only as 3D UI); SSR rays / denoiser + Grass Height WON'T with proof; DrawScrub skip needs a Rome II hook + trace).
  Rome II agent DONE.
- [x] Merged-HEAD build check at ef212467: both lines exit 0 (every TW add-on, x64 + build32) after the many shared opt-ins
  (rt_core held_row / depth_at_render_size / behaviours_at_cb3, particle_layer options, GS variants). Suites + harness +
  page dumps follow at the final verification.
- [x] Rome Remastered so far MERGED a4d6e0a6 (crash fix f6a7cfc4, reset safe side a8afd660, DLSS below 100 % 867187d4, Attila's
  page + Reset / No RT/PT / RT/PT 09a083f0 with opt-in graphics_page has_gtvbao / has_gi / every_row: Attila / WH3 / Thrones /
  WH2 / Troy / WH1 page dumps unchanged; dump target 'rr'). Not installed. Agent on: parity table + DO NOW, B (input bug), C
  (rays + DLSS below 100 %); then the Fable 5.2 medium reset hunt.
- [x] Old-game parity survey DONE (tools/tw/round_notes/parity_{wh3,pharaoh,rome2}_1114.md; dump targets dump_p / dump_r2):
  WH3 186 have / 8 do now / 2 trace / 77 won't (62 engine rows level B: Denuvo, name scan would make A); Pharaoh 220 / 14 / 7
  / 32; Rome II 224 / 23 / 1 / 25 (RT coverage 9 families, below-100 % audit 27 rows). All three can carry the torch rows.
  Implementers started (worktrees): WH3 par_w3_, Pharaoh par_p_, Rome II par_r2_ (DO NOW lists, Experimental off).
  ATN DECIDES: rays_preset_as_attila for Pharaoh and Rome II too (the 10:1x wish named "the new games").
- [x] Forge's points (01:35 screenshots): scorer covers RMSE / SMAPE / boiling / swimming / covariance (done); Nsight GPU Trace
  done, replay + shader swap = Nsight agent phase A; SER per pass in GPU Trace added to the Nsight agent; unified DI + GI
  reservoir failed as one reservoir (U1 66 % / U2 20.5 % rRMSE), shipped as the light-per-bounce-texel rows (helper111). DONE: same as FORGE'S POINTS (ticked); SER per pass + replay done by the Nsight agent (29b54d18).
- [x] ATN ~11:0x: heapfix in WH3: the author is fine; first prove it doable + a gain in our RT/PT preset in a jungle battle,
  then ATN sends him the whole code. Study DONE (tools/tw/heapfix_wh3, nothing built): PARTLY. Only the allocator has a WH3
  twin (one global dlmalloc 2.8 + spin lock: malloc 0x4f3990, free 0x53eca0, realloc 0x17bcb28, lock 0x17bc510, in readable
  .xpdata; dinput8 imported directly, no dinput8 in the folder); the render-list sorts / pools / x87 / memcpy have none or
  aren't needed. WH3 RT/PT is GPU-bound in every measurement (CPU 7.3 ms of a 19.8 ms frame; frame follows resolution):
  ~0 fps expected. Nothing to send. Optional: ATN records a Lustria jungle replay -> ab_run_wh3.ps1 (not run) with WPR; build
  the x64 heap only if CPU-bound with the heap >= 5 % of the frame thread.
- [x] HAZARD FIXED (tools, 11:25): wh3_common.ps1's Wh3-Restore deleted every replay / save outside a name list (ATN's
  2.replay of 11-10 at the next WH3 run). Now: only files created inside a run's window (run_started.txt at the end of
  Wh3-Before-Run, run_ended.txt) move aside to atn_backup\moved_aside, never deleted; no start mark = nothing moves.
  Backups: wh3_atn_files_backup_20261114_112043 (replays + saves, hashes), wh3_common.ps1.bak_20261114_112043. Parses clean.
- [x] Stamp + install the newest of all eight games before ATN wakes; round report per game. DONE: INSTALLED 07:50 2026-11-14 + 10:45; tools/tw/round_notes/morning_report_1114.md.

## Working with ATN
- Very short answers, short code comments. Deliver changed files as a zip in `C:\Users\arshi\Downloads` (game folder
  at the zip root). Never push upstream. ATN tests in game and sends `ReShade.log` (next to `Ryse.exe`).
- Read `docs/CXX_STYLE.md` before C++ edits (designated initializers, local lambdas, no single-use helpers).
- Build: `cmake --build --preset clang-x64-release --target renodx-rysesonofrome.addon64` (HDR) /
  `renodx-rysesonofrome_dlss.addon64` (DLSS). Bin64 holds symlinks to `build/Release` (`0renodx-rysesonofrome`,
  `1renodx-rysesonofrome_dlss`, `2renodx-devkit`).
- If the build can't write the addon (game or zombie `Ryse.exe` running), rename the old file aside and rebuild.
- Shell heredocs eat backslashes (`\0`, `\\`): write edit scripts with the Write tool, then run them with python.

## Ryse reverse-engineering facts
Moved to HANDOFF_ARCHIVE.md, "### Reverse-engineering facts (Ryse.exe, base 0x137000000, RVAs)" (RVAs, cvars,
jitter; standing facts for the RYSE MERGE round), under "## Ryse DLSS addon" in the original order.
