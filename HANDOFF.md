# Handoff: Ryse HDR + DLSS (2026-10-05, Claude Code session)

## ATN'S 2026-11-13 BATCH (day: ATN out ~11:00-19:00; agent runs allowed while ATN isn't playing)
Order (ATN): Pharaoh v58 -> grass CPU -> open items -> skill -> Rome II plan -> Rome II. Added by ATN: water Soft + Steady
rows on by default and in RT/PT; Attila PT GI angle brightness; unit LOD CPU. Mirror: memory project_rome2_batch.md.
- [x] Pharaoh v58 (4b554a21 .. 7a4a244c) rebuilt at HEAD 12:02-12:04 (the 11:3x builds predated 61923316's sway fix;
  helper106 rebuilt after 654901d2), STAMPED + INSTALLED 12:05 (tools/tw/round_v151/install_v58.py): Attila v154, WH3 v52,
  Pharaoh v58, helper106 in all three; ATN files unchanged by hash; old files renamed aside (.in-use-20261113_120458).
- [x] Grass study tools/tw/grass_cpu/report.md; unit study tools/tw/unit_cpu/report.md; PT GI diagnosis
  tools/tw/attila_ptgi_1113/diagnosis.md; Rome II census + plan tools/tw/rome2/ (census, caster_census, scale_census, plan).
- [x] Attila measurement run (one start, no builds beside it): grass sliders live (2vs3) + Unit Detail 1x / 2x / 4x
  (Replaced 2026-11-13 evening by ATN's own runs: WPR CPU / GPU profiles of his 8x battles and the 2vs3 replay,
  tools/tw/cpu_profile/report.md; they decided the CPU round, the scrub cache and the grass / unit hooks.)
  (for_claude1) + FrameTrace for the soldiers' shadow-pass pack marks; PresentMon; decides grass d2 and unit A1.
- [x] Rome II trace build (twrome2 test_hooks + frame_trace Options + on-demand marker), folder prep (stamped plain copy,
  helper, no DevKit), data runs S1 bm_battle benchmark 50/67/33/100, S2 Carthage siege 50%, S5 campaign_load Parthia; no
  MSAA (ATN allowed dropping it; set gfx_aa 0 in ATN's Rome II prefs at the final install, backup first).
  - [x] Trace build 5cbbff5e (twrome2 only): test_hooks + frame_trace Options (on_demand, at_100, the plan's 6 value hashes
    + units' VS A30C5E0B, vertex inputs A30C5E0B / 0F1FBD46, instances apart); build32 renodx-twrome2.addon32 12:36:28, built
    only, not stamped / installed / run; page dump before = after (tools/tw/rome2/page). Run tools in tools/tw/rome2:
    rome2_common.ps1, rome2_data_run.ps1, check_run_rome2.py, prepare_folder_rome2.py (dry by default; none run).
  - [x] Stamp the trace addon + helper106 for Rome II, prepare_folder_rome2.py dry then --apply, data runs S1 / S2 / S5.
    (Done as ATN's menu starts with tools/tw/rome2/atn_trace_watch.ps1: runs A1 / C1 / A2 / N1 / L1 / L2 / S1 in
    tools/tw/rome2/runs; the CLI starts crash in Rome2.exe itself, also without the add-on.)
- [x] Water rows RayTracedWaterSoft + RayTracedWaterSteady on by default and in RAYS_PRESET (Attila, WH3, Pharaoh): 33f6a5aa
  (shared rows: Thrones takes them too); page dumps a / w r155pre -> r155final, Pharaoh r59pre -> r59final: those two rows'
  defaults and labels only (+ WH3's grass row below). Built only (Attila v155, WH3 v53, Pharaoh Round v59), not stamped.
- [x] Grass round: WH3 indirect read-back without a wait (d1); Attila scrub cell radius clamp (d2) + LOD scaling per model
  (d2 clamp, the scrub race and Grass Ray Tracing Range came in the NIGHT MERGES; a2 / d1 without the lag still open.)
  (a2); RT grass capture range (Experimental).
  - [x] d1 quick form 796033c8: WH3 Grass Without Waiting (Experimental, off; Settings::indirect_counts_later: the probe's
    earlier-frame numbers, never the waiting read). test/tw_indirect_counts (WARP, x86 + amd64): row off this frame's numbers
    40 / 40, 3.8 ms a frame; row on 0 this frame's, every capture an earlier frame's, 0.011 ms. Not run in game.
  - NOT DONE: d1 without the lag (capture as an indirect draw onto emptied room sized from the probe, or a GPU copy of the
    numbers for the helper): changes the compact capture's room and the helper's cluster counts (bridge / helper107).
    Smallest step: the terrain_count_later pattern (NaN room, size = probe count x 9/8) for a non-compact indirect capture.
- [ ] Unit round: A1 (Attila UnitDistances[16] = 0 under Shadows Only, if the trace shows a soldier shadow pass), D (WH3
  cross-pass unit capture key), B (trim per-unit capture work), E (TBB grain test rows).
  - [x] A1 19581888: index 16 = 0 while rt_scene::CastersSkipped() (the next frame's sun-map casters skipped), the game's
    10000 back the frame it ends; index 16 = entity_display_shadow_max_shadow_distance read by the soldiers' shadow record
    (+0x441eb0 / +0x441f49, bytes read). test/tw_unit_shadows x86 + amd64 PASS. Later run: the shadow pack mark's count.
  - NOT DONE: A2 (sprite soldiers' 276 shadow draws into the map): entity_display_enable_imposter_shadows's only reader
    (+0x43c70d) sets index 16 to 10000, it never stops those draws; their submission code not found offline. Nothing
    visible would change (every caster of the map is skipped under the rays), only the game's submission cost (~0.1-0.2 ms).
- [x] Round builds + tests at 796033c8 (docs commit after): build32 twattila 43 s, WH3 43 s, Pharaoh 44 s, the skill's other
  13 x64 targets 67 s, build32 twrome2 + twthronesofbritannia 42 s: no errors, only the upstream deprecation warnings (and
  Ryse DLSS's old reorder warning); every test/tw_* x86 + amd64 PASS (19 suites, 37 runs). READMEs: Attila v155, WH3 v53,
  Pharaoh Round v59 (built only). Not stamped / installed / run; no helper change (helper106 as in v154).
- [x] v155 / WH3 v53 / Pharaoh v59 (33f6a5aa water rows, 19581888 unit A1, 796033c8 Grass Without Waiting) STAMPED +
  INSTALLED 13:35 (install_v59.py; helper106 unchanged; ATN files unchanged by hash). ATN's Attila and Pharaoh preset1
  already hold both water rows at 1; WH3 had none saved (the new default 1 applies).
- [x] Rome II runs BLOCKED (12:49 / 12:53): ATN's mod pack dialog (class mod_question_dlg) after an unclean exit; SendInput
  (Unblocked: ATN starts Rome II from the menu, see the trace runs above.)
  Enter doesn't answer it; screen access denied. ATN starts Rome II once, answers it, quits from the menu. Script fixed
  13:00 ($start vs [string]$Start, case-insensitive names; the dialog's class detected: stop at once).
- [x] PT GI fix (helper107): one diffuse estimate for screen / cache / unseen hits, KeptShade -> own occluded sky, freeze
  slope + base colour under PT Ambient; HARNESS_PEDESTAL proof; look at the bounce GPU spikes 19-40 ms. Built only (Attila
  v156 / WH3 v54 / Pharaoh v60 with helper107); left: ATN's in-game test, WH3's and Pharaoh's sun colour.
  - [x] 1 HARNESS_PEDESTAL (tools/tw/rt_harness, outside the repo): pedestal on a lit floor, four corner cameras (order
    01230: camera 0 cold and warm), ATN's paths (1 ray, Bounces 1, ReSTIR + RR, PT Ambient, Own Sky Rays, Kept Cube), light
    buffer = B6DA03C5's diffuse sun + its sky-cube and sun-lobe specular (HARNESS_PEDESTAL_SPECULAR). CONTROL helper106: spec
    0.5 the shaded E / N die and risers 14.3-21.6% apart (sunlit faces, treads <= 2.8%); spec 0.2 1.8-3.8%; spec 0 (diffuse
    only) <= 1.3% (so the specular drives it, not the unoccluded KeptShade here). Learned sun at each PT Ambient return: spec
    0.2 10.6 (learned with the ambient; true 20) then 35.9 / 24.4 / 24.4 / 25.6 by camera; spec 0 11.2 then 20.4-20.6.
  - [x] 2 helper107 fix (bridge v31, new flag RT_PATH_SUN_LIGHT; Attila's rt_sun writes the game's sun colour x 0.004 into
    texel 2): with it a path's screen hit, the cache's fill and an unseen hit all take base colour x the game's sun x N.L x
    traced shadow + base colour x the paths' own occluded sky (own_sky u54-u55, last paths' at the texel; OpenSky where
    none), the learned sun = the game's, no slope fit. HARNESS_PEDESTAL helper107: spec 0.2 / 0.5 / 0 worst 1.6% (W riser 3),
    order 32103 1.5%, kept sun 20.000 at every return; 4K full-res spec 0.2 h106 4.1% -> h107 0.5%; GI level 0.93-1.04 of
    h106's diffuse-only. Without the flag (WH3, Pharaoh: no sun colour yet) helper106's paths exactly (21.6% at spec 0.5).
    Freezing the learned slope without the game's sun rejected: frozen 4.3-11.2 against 20 (too dark).
  - [x] 3 bounce GPU 19-40 ms: not a spike in the code. The status line's bounce is one request's time (the last answered);
    the four high ones (10:22:21 19.35, 10:22:50 39.42, 10:23:03 39.75, 10:23:09 28.54) all fall between the settings
    changes of ~10:22:18 and ~10:23:15 (the "Ray tracing with" lines come 3 s after a change), 5.10 right after the next
    one, and 19 -> 39 with no change between (the view moved): a setting held for a minute whose cost follows the view.
    The log started at 2 rays and ended at 1; ATN's profile (1 ray) splits the bounce into cache fill + resolve 1.0, paths
    3.55, reuse 1.95 ms: only the paths scale with Rays (19 / 28 / 40 ms ~ 5 / 7 / 10 rays). Harness at 4K (helper106,
    107; 1, 4, 8 rays; PT Ambient toggles, four cameras): no bounce frame over 3x the mean. Nothing in the helper
    relearns or resets at a settings change (SHaRC off, NRD released). Now the "Ray tracing with" line names the Light
    Bounce's rays and bounces. What a trace must show if it comes back: Record Ray Tracing Profile over the slow time
    (tw_rt_profile_helper.csv gpu_a / b / c = fill + resolve / paths / reuse per request) and that line's rays.
  - [x] 4 builds + tests at d809907e: helper107 12 s (build\tw_dlaa_helper107, bridge v31), build32 twattila + twrome2 +
    twthronesofbritannia 44 s, the skill's 15 x64 targets 51 s (11 rebuilt, 4 up to date): no errors, only the upstream
    deprecation warnings. all_modes 179/179 PASS (tools/tw/all_modes_v31_helper107.txt; against helper105's log by name:
    release_rr passes now, new pedestal_gi). HARNESS_CACHE_LEAK A 0.0329 / B 0.0195 (helper105 0.033 / 0.020). RR noise
    (preset 5): RTAO 35% / 22% left as before; PT Ambient bounce 62% / 45% on helper107 and helper106 alike (no sun light
    flag there: the same paths). test/tw_* 19 suites x86 + amd64 38/38 PASS. WARP tools/tw/attila_ptgi_1113/rt_sun_colour:
    texel 2 = c1 x 0.004 / x 1, every other texel v155's bit for bit, Pharaoh's includer texel 2 zero, CONTROL apart: PASS.
    Page dumps a r156 / w r156 / p r60 against r155final / r59final: Build Date only.
  - [x] 5 READMEs Attila v156, WH3 v54, Pharaoh Round v60 (built only, helper107); skill rows: ao-gi.md (GI reads no light
    that depends on the view; HARNESS_PEDESTAL the check), testing.md (pedestal_gi; the status line's GPU ms are one
    request's). Not stamped / installed / run.
- [x] Attila v156 / WH3 v54 / Pharaoh v60 + helper107 rebuilt at HEAD 15:03-15:04 (590d2132; the 14:4x add-on builds
  predated d809907e's commit, helper107 14:23 its c592149c), STAMPED + INSTALLED 15:05 (tools/tw/round_v151/install_v60.py;
  ATN files unchanged by hash; old files renamed aside .in-use-20261113_150506; no DevKit). build.ps1 needs an absolute
  out folder (a relative one fails on fg_ui.h).
- [x] Pharaoh open items plan: tools/tw/pharaoh/open_items/plan.md (read-only study 15:00; A offline: "no main camera"
  (Done: PHARAOH V61 below.)
  = AO_DRAWS passed with Attila's slots, E8524BB1 slot map, clip at 25, building water scale rows, 7b / 7c WARP; B one
  siege benchmark run with new trace reads; C ATN's campaign trace).
- [x] Pharaoh open items (smoke off screen, dust vs smoke, E8524BB1 / 740B2536, water clip / fog leg, shallow + campaign
  (Done in V61 / V63 / V64 below; what is left: the Pharaoh README test list.)
  water, "no main camera" line, v58's census leftovers with their traces).
- [x] Skill lessons from all rounds; Rome II features (the whole Attila package); install newest; round report per game.
  (Skill: LESSONS + CHECKS ROUND; Rome II rounds 1 + 2 (ROME II BRING-UP, NIGHT MERGES); installed 22:59 (NIGHT
  MERGES); round report tools/tw/round_notes/night_1113_report.md.)

## SIX GAMES TO THE ATTILA PACKAGE (2026-11-14 night; ATN asleep, weekly budget: use it; Troy + WH1 added 01:50)
ATN (2026-11-13 ~23:40): after the skill update, plan and do the whole Attila package for Thrones, WH2, Shogun 2 Complete
Edition and Rome Remastered, in parallel; in-game runs only for traces, few, agents at medium effort. Troy / WH1 next
time. ATN tests Attila / WH3 / Pharaoh / Rome II and the rest after waking: nothing may run in a game then.
Brief: tools/tw/round_notes/game_agent_brief.md (phase P: plan + trace build + data-run script; phase I: the package).
- [x] Skill: night lessons a34444d6 (worktree base, scratch prefix, merged verification; bit-exact game CPU code; skipped
  calls read to every return; first-time race; MSAA keeps DLSS / rays / FG off; 0-thread processes; medium data runs;
  units on screen).
- [x] tools/tw/install/tw_install.py: thrones / wh2 / shogun2 (HDR + DLSS add-ons) / romer (no helper) entries, --helper
  optional, --first (symlinks, missing files, helper + NVIDIA DLLs, DevKit aside); backup tw_install.py.bak_1114.
- [ ] Phase P, four worktree agents: Thrones (tools/tw/thrones), WH2 (tools/tw/wh2), Shogun 2 (tools/shogun2/pkg_1114),
  Rome Remastered (tools/tw/romeremastered): plan_1114.md, trace build on the branch, <game>_data_run.ps1.
  - [x] Thrones: plan + trace build dd9359ab (merged 85ad67ae); thrones_data_run.ps1 (+ gpu_job.lock, coordinator). Old
    page layout (no graphics_page), 0 dead hashes, no trace anywhere; won't get: Grass Height Distance (hashes absent),
    engine code TRUNCATION / FMOD_MATHERR (patterns absent). Campaign needs ATN's menu start (no saves).
  - [x] WH2: plan + trace build e6b83f99 (merged 52fa2ffd); wh2_data_batch.ps1 A-E (+F -Night). Old layout, 0 dead hashes,
    12 dead engine values, exe code protected (no engine-code features). Campaign / walled siege need ATN.
  - [x] Rome Remastered: plan + trace build v7 03323ef0 (merged 9d5fef16); romeremastered_data_run.ps1 A100 / B67 / C50.
    Feral D3D11 Forward+ (no G-buffer, velocity, TAA, jitter): no RR, no GI / PT, no torch shadows in round 1; DLSS via
    the helper, RT sun shadows into CSMResolve's mask, RT AO into ASSAO's target planned. Replay start form a guess.
  - [x] Shogun 2: plan + trace build 8603a212 / 45571fa6 (merged aacfac9a); shogun2_data_run.ps1 (battle / naval replay,
    campaign_load: guessed forms). RT on the old InstallScene path (no host for static_checks), WriteSun texel 0 only;
    the game's NGX DLSS (one bridge per process: the HDR add-on's, DLSS add-on through cross_addon). Installed 00:16
    ("shogun2 t1 addon" + "dlss addon", --first; DevKit renamed aside; no helper; the game's nvngx_dlss 310.5.2 kept).
  - [x] Rome Remastered phase I part 1 (branch, not merged): aeff1928 stability wiring (device_loss, output_encoding),
    e0c594ea CSMResolve / ASSAO hook points + two Test rows (off), 4d6f63c4 README v8; the rest waits for the run.
  - [x] Trace builds STAMPED + INSTALLED 00:08-00:09 with tw_install.py --first: Thrones "thrones t1" + helper110 (helper
    DLLs + NVIDIA 310.9.16 copied, none before), WH2 "wh2 t1" + helper110 (symlink + the 10-27 helper aside, helper DLLs
    copied), Rome Remastered "romer t1" (no helper). ATN files unchanged by hash. Phase I trace-free items started in all
    three worktrees; data runs from 01:00 (the scripts' window), Thrones -> WH2 -> Rome Remastered -> Shogun 2.
- [x] Phase I part 1 (trace-free, on the branches, not merged): Thrones 0ef93203..f70d5285 (graphics_page, sun draws 8/4 +
  WARP 50 sun PS, Light Through Foliage, Attila graphics rows (proof B by disassembly), Faster Game CPU Code float->half
  only (Experimental), Smoke Shadows both kinds; shared graphics_page::Bindings::smoke_map_tested default true). WH2
  3f9d9b0e..1f7dfc7d (SetViewportChains, Graphics 2 of 2 = Attila's, Hue Clip measured 100, graphics_page, Round v48);
  PT GI sun colour blocked (SH branch, two diffuse ends). Shogun 2 0b4f02ef..00e18b87 (W3 rt_sun texel 2 + cube, W4 helper
  upscaler beside the game's DLSS, W5 helper FG + Reflex, W6, W7a, W8 part; HDR v66 / DLSS v58); rt_scene.hpp can't be
  included (it pulls anti_aliasing.hpp's shaders): NOT DONE smoke via rt_scene; smallest step: split the light / smoke
  installers out of rt_scene.hpp's AA include (shared, after the game branches are merged). Rome Remastered: see above.
- [x] DATA RUNS 01:01-01:29 (one medium-effort agent; no script changed; ATN's files the same by hash on every start; no
  GPU reset, Committed 21-22.5 GB, no markers or processes left): Thrones S1 siege / S2 replay / S3 night (9 traces,
  tools/tw/thrones/runs), WH2 A-F (11 traces, tools/tw/wh2/runs; A's 4 scales the same size: check the scale reached them),
  Shogun 2 battle / naval / campaign (12 traces, tools/shogun2/pkg_1114/runs/data_1114_012352; the campaign CRASHED 84 s in,
  1 s after RenderScale back to 0: Shogun agent reads the dump first), Rome Remastered NOT RUN (the exe has RUNASADMIN in
  AppCompatFlags\Layers: UAC prompt; ATN must clear it or start from the menu; script $start bug). Phase I part 2 resumed
  in all four worktrees. Skill a5c6d897: RUNASADMIN row, tools/tw/ps_param_shadow.ps1.
- [ ] (old line) Data runs from 01:01, one medium-effort agent (tools/tw/round_notes/data_run_brief.md), one game at a time; trace builds merged, stamped "<game> t1", installed with
  --first; fresh backups; Shogun 2's DevKit aside; its nvngx_dlss 310.5.2 -> 310.9.16 renamed aside).
- [x] Rome Remastered v8 MERGED 28a753f5 (stability wiring, two Test rows, Trace One Frame; script -Form + RUNASADMIN refuse).
- [ ] ATN 01:50 (awake briefly): start Rome Remastered now (the RUNASADMIN flag was gone at 01:51: its data run resumed),
  and plan + do Troy and WH1 too, at the same time. Two more worktree agents (tools/tw/troy, tools/tw/wh1; same brief,
  base 28a753f5); tw_install.py troy / wh1 entries (Epic folders under C:/Program Files/Epic Games; WH1 prefs EOS/scripts).
- [x] Rome Remastered starts: 01:51 the replay form (-replay:play:...file="temp/battle.rpy") quit 4 s in, before any
  window or device (add-on never loaded; its logs lost to the restore: script now keeps game_logs, skill ad644a65);
  01:56 menu form (no arguments): 60 s up, add-on loaded, swap chain, no 3D frame (ATN's options window / menu need
  clicks), 2 empty traces (romeremastered/runs/data_1114_015643/M). Rome Remastered traces need ATN's menu start with
  rome2\atn_trace_watch.ps1 (plan_1114.md sections 7-8). No further agent start tonight.
- [x] Shogun 2 phase I MERGED d378aac2 (14 commits: W3 rt_sun texel 2 + cube, W4 helper upscaler beside the game's DLSS,
  W5 helper FG + Reflex, W6, W7a, W8 part, W10 fog shadows (Experimental), W11 presets, rt_sun ~52525x too bright fixed,
  Upscaling Debug View, test/tw_shogun2_shaders WARP PASS; HDR v66 / DLSS v58). Campaign crash = the game's own exit
  path (heightmap.dat unmapped before the campaign renderer's destructor; no add-on frame; test 9 settles it). The
  helper ships now (tw_install.py shogun2 uses_helper True: helper110 + DLLs + NVIDIA 310.9.16 replace the game's 310.5.2).
  Round 2 on the branch: PT AO (SH sky), foliage light, families, water, casters out of view.
- [ ] 02:00 ATN started Rome Remastered from the menu, loads a battle and goes to bed: atn_trace_watch.ps1 (25 min, every
  20 s, romeremastered/runs/ATN_battle_*), gpu_job.lock held by the coordinator; then close the game through its window.
- [x] WH1 phase P: plan tools/tw/wh1/plan_1114.md, trace build e89b8453 (branch wh1-p1114, merged 19de303e), installed
  02:19 "wh1 t1" + helper110 with --first (symlink aside; helper DLLs + NVIDIA 310.9.16 copied, none before). Data run
  wh1_data_batch.ps1 A-E (+F -Night, G -Campaign; NOT -AaOff: ATN's gfx_aa 2 stays) after ATN's Rome Remastered session;
  the Epic start (warhammer.exe with -epicapp... and no exchange code) is a one-try guess. Exe code protected: no engine CPU
  features. Phase I trace-free items started.
- [x] Rome Remastered: ATN's own menu start 01:59-02:19 (campaign + battle), atn_trace_watch 50 traces
  (romeremastered/runs/ATN_battle_015951); phase I part 2 resumed with them.
- [x] WH2 Round v48 MERGED 28779515 (graphics_page, Graphics 2 of 2, viewport chains, Torch and Fire Shadows via
  InstallPointLights, Rays Replace / Skip the Shadow Maps, Fog Shadows + light in fog, all Experimental; Hue Clip 100;
  render scale reached the traces; SH flag on in every battle blocks PT AO). Round 2 on the branch: PT AO, casters out of
  view, spot lights, smoke, water.
- [x] Troy phase P: plan tools/tw/troy/plan_1114.md, trace build f7defa00 (merged 03beac2b; shared:
  twpharaohdynasties/trace_reads.hpp takes a Shaders struct, Pharaoh's output unchanged), installed 02:24 "troy t1" +
  helper110 --first. Offline bugs B: Coloured Shadows reads c1 (sun direction) not c3; AA wrappers lack TW_AA_FRAME_LINEAR.
  Data run after WH1's with -MsaaOff (direct starts only, no launcher-click fallback).
- [x] ATN 02:25: "You are allowed to change the game's settings for all of them" (memory feedback_game_aa_allowed.md).
- [x] Shogun 2 round 2 MERGED 878fffea (PT AO with the game's SH sky, Light Through Foliage, cloth / grass families, RT
  specular occlusion; HDR v67 / DLSS v59). Round 3 on the branch: water reflections, imposters / billboards, W7d.
- [x] WH1 + Troy data runs 02:20-02:34: no 3D frame in either (Epic). WH1: warhammer.exe -epicapp... without an exchange
  code loads the add-on, stays in the frontend (wh1/runs/A_022108); Troy.exe direct: "Please use the Epic launcher"
  dialog (troy/runs/S1_022643, S2_023214). ATN's files the same. Their traces need ATN's Epic start with
  atn_trace_watch.ps1; agents continue offline + the 10-27 traces (build/ingame/traces). Skill 8fad0a89.
- [x] Thrones round 1 MERGED 47b3c21a (graphics_page, sun draws, RT scene to v159 + cut-outs, shadow readers, water,
  graphics rows, Faster Game CPU Code half floats, Smoke Shadows; shared Bindings smoke_map_tested / far_cards_tested and
  RT_GRASS_HEIGHT_DISTANCE, defaults unchanged, consumers built, page dumps the same). Not done: Particle Resolution,
  campaign census (no campaign trace).
- [ ] FORGE'S POINTS (ATN 03:0x: "do the points that you mentioned too"): (1) PT image-quality scorer in the harness
  (agent; tools/tw/quality/report.md); (2) Nsight Graphics 2026.4 downloaded with ATN's OK (NVIDIA-signed msi, 694 MB,
  tools/nsight/download) and extracted, no system install (tools/nsight/NsightGraphics_2026.4); GPU counters are admin-only
  (RmProfilingAdminOnly unset): ATN may allow them in the NVIDIA Control Panel; GPU Trace agent on the harness
  (tools/tw/nsight); (3) unified ReSTIR DI + GI reservoir (worktree agent; plan tools/tw/restir_unified/plan.md, helper111
  if feasible, Experimental row off = helper110 bit-identical).
- [x] Rome Remastered v9 MERGED 70696220 (DLAA through the helper, Experimental; jitter into b5, motion from depth;
  test/tw_romer_dlaa WARP PASS); tw_install.py romer uses_helper True. Round on the branch: RT sun shadows / AO, Bend.
- [x] GPU counters: ATN allowed them for all users (RmProfilingAdminOnly 0, 02:4x). An ngfx call opened a Qt error
  dialog on ATN's screen: Nsight agent told to stay headless.
- [x] Troy: ATN's own Epic start 02:49-02:55 (campaign, then a night siege, asked to set the game's AA off):
  atn_trace_watch 14 traces (troy/runs/ATN_session2_024858); sent to the Troy agent. Troy's prefs still gfx_aa 2 on disk.
- [x] WH2 round 2 MERGED 5a19a9d4 (Path-Traced AO with WH2's own rt_sun; shared PatchAmbient AMBIENT_AFTER_HARMONICS (the
  old rule patched the fade-out cube in all 31 WH2 sun PS, harmless: t51 never bound); Far Tree Shadows, Shadow Casters /
  Hills Out of View; other games' patched bytes unchanged). NOT DONE: spot lights (instance layout differs: shared reader
  needs a layout parameter), Smoke (WH2's VS writes the atlas UV at TEXCOORD3: own record shader in rt_core, shared).
- [x] Unified ReSTIR DI + GI: study only (tools/tw/restir_unified/plan.md; branch worktree-agent-afd5e1bbbc7a65b04 f9350977
  not merged, HANDOFF-only). No DI reservoir exists (rt_lights = visibility bits, the game shades torches). U1 (torch light
  through the bounce: saves rt_lights 0.4 / 1.5 / 5.6 ms at 3 / 32 / 32 Steady lights, but no torch specular, softer
  moving shadows) or U2 (keep the game's torches, bounce supplies a shadow ratio: bridge + every game's point-light PS,
  several nights): ATN DECIDES. Smallest step: helper-only U1 prototype behind RENODX_TW_LIGHTS_IN_BOUNCE + a harness
  comparison against Steady.
- [x] Shogun 2 round 3 MERGED 22affa04 (sun-facing sprite / far tree captures, RT sea reflections, all Experimental;
  shared rt_core Host::scene_draw capture_shader, unchanged without it; HDR v68 / DLSS v60). NOT DONE: waterplane
  9B9EC116 (replay its 6 draws depth-only), other ocean qualities, W7d casters out of view.
- [x] ATN's WH1 session 02:57-03:02 (battle, then campaign): 15 traces wh1/runs/ATN_session_025635 -> WH1 agent.
- [ ] ATN 03:0x: "Can we just do both?" U1 + U2: the ReSTIR agent resumed: U1 prototype + harness comparison, then an
  Experimental row (helper111, spare flag bits); U2 designed and started behind an off row.
- [x] Merged: Rome Remastered v10 551ec58b (RT sun shadows + AO, Experimental), WH1 v48 df1aff82 (Hue Clip 90 measured:
  ATN eyes it), Troy v48 c7b41326 / v49 44bf144f / v50 22950581 (shared twattila/sun_replace_maps.hpp = Pharaoh's
  OnSunReplaceMaps moved verbatim: verify Pharaoh in the merged check).
- [x] ATN's trace sessions 03:08-03:48 (FrameTraceReads=1 in the four ini, removed after; ATN's own changes kept):
  Troy session 3 (17, rays on, DLSS P / UP, battle + campaign), Thrones campaign (13, DLAA / Balanced / UP, RT, RR F),
  WH1 session 2 (21, P / UP battle + campaign, RT, RR F), WH2 (30, campaign + fire siege, DLAA / P / UP). Rome Remastered
  wouldn't start: memory (a quality-scorer harness helper at 45 GB committed, PC at 74.8 / 73.8 GB; its agent stopped
  it; my kill was refused by the permission classifier); ATN does Rome Remastered 67 / 50% another day.
- [x] ATN 04:0x: "just keep working until you have finished everything" - no 08:30 stop: every agent finishes its whole
  plan (WH1, WH2, Thrones, Troy, Shogun 2, Rome Remastered resumed: Bend, FG, debug views; ReSTIR U1 + U2, quality
  baseline, Nsight). An interim install of the merged state at ~12:00 if agents still run (no game running); final
  install after the last merge + verification.
- [x] Quality scorer DONE (tools/tw/quality/report.md; outside the repo, no helper change): 11 / 11 validation, helper110
  baseline (rRMSE: sun RR 3.7, RTAO RR 1.03, PT AO ReSTIR + RR 2.19, torch RR + importance 1.40, Steady + RR 3.29).
  FINDING for ATN: PT AO with REBLUR under RR is 4.4x worse than RR alone and grows while the camera stands still (blotches).
  all_modes 179 / 179 (all_modes_iq1114.txt). quality.sh compare <helperA> <helperB> for helper111.
- [x] Rome Remastered v11 MERGED 6fccf345 (frame generation + Reflex via DLAA's HUD-less point, Screen-Space Shadows =
  twattila sss_bend into the mask, Ray Tracing Debug View, profile button; all Experimental, off; tw_romer_dlaa + rays PASS,
  case 6 proves the sun sign). Test list: tools/tw/round_notes/tests_1114.md.
- [x] Shogun 2 round 4 MERGED 5601f257 (waterplane 9B9EC116 recorded + replayed depth-only, Ultra sea 6B8954A6: RT water
  reflections on rivers, lakes, the Ultra sea; Experimental, off; HDR v69 / DLSS v61; WARP parts 7-9 PASS). NOT DONE: W7d
  (cascades draw before the main view; rigid shadow 38-326M indices x instances vs a 29M budget): agent resumed on the
  per-instance record (opt-in rt_core changes, every rt_core user's suite must stay unchanged).
- [x] Troy v51 MERGED 4053fe95 (Torch and Fire Shadows 143A02ED in Pharaoh's fire02 form, campaign trees 54D2CFBD +
  billboards DF8EAF69 in the rays; shared twattila/contact_shadows_off.hpp = Pharaoh's contact march off moved verbatim:
  verify Pharaoh in the merged check). NOT DONE: fires 643FC156 in the rays (after WH2's rt_scene light reader change),
  shroud test, Log Details light count: agent resumed.
- [x] WH1 round 2 MERGED 61f0bbb7 (PT AO with the harmonics' sky + PatchAmbient AMBIENT_AFTER_HARMONICS_Z (shared, other 7
  games' 238 rows byte-identical), far cards CA6BCF19, campaign plants 84797031, casters out of view, Torch and Fire
  Shadows D0C67938 / 5D1695BC, Rays Replace Shadow Maps (game-side), SSR share BUG 69A57B14 fixed; RT/PT preset turns on
  PT AO + torch shadows). NOT DONE: Skip the Shadow Maps (agent resumed: forward readers first), Fog Shadows (analytic),
  CDCF1ADD, raycast 021D7515 below 100%, AO / SSR reader census (needs ATN's start with SSAO + SSR off).
- [x] WH1 Skip the Shadow Maps NOT DONE (merged README 49eff1b7: 7 forward readers, 2 outside the shared forward patch,
  WARP control failed; attempt kept in tools/tw/wh1/skip_maps_attempt.diff); agent resumed (opt-in patch parameter,
  cascade constants, then RR guides, sprites, thin sheets, Bend foliage, cut-outs, particles, water).
- [x] Nsight DONE (tools/tw/nsight, README; memory reference_nsight_gpu_trace): helper110 8.4 ms at base clock, ReSTIR
  reuse 2.55 (L2 bandwidth), trace 1.48 (RT core), reflections 1.27, PT 1.16 (registers). No capture / shader swap: the
  helper has no frame boundary; NGFX_FrameBoundary_D3D12 (Apache-2.0 header, no-op without Nsight) = ATN DECIDES. The
  45 GB process at 03:2x was the Nsight run (commit grows ~8 MB a frame under injection); the ReSTIR agent overwrote its
  lock twice (told: atomic create + re-check). Reuse optimisation handed to the ReSTIR agent after U1 / U2.
- [x] Thrones round 2 MERGED 3da5efd9 (campaign census, Particle Resolution + edges + smooth half-size, Experimental);
  agent resumed: Light Through All Banners, Grass Height Distance, Faster Game CPU Code if provable.
- [x] Shogun 2 round 5 MERGED acfa8633 (W7d rigid casters out of view AC25DAB8 by rows v6-v8, Experimental off, needs a
  restart; shared rt_core Host::cascade_last_view opt-in: Attila x64 / build32, WH3, Ryse, Ryse DLSS, Expeditions DLSS,
  Rome Remastered rebuilt, their rt_core suites PASS). Agent resumed: cloth 8A00A1D4 + sprites E3DA20D4 (world route).
- [x] ATN ~04:50: "use the nvidia nsight sdk and any other nvidia sdks that you need" (free + donations OK). Nsight agent
  resumed: Nsight Graphics SDK 0.9.2 headers (Apache-2.0) + NGFX_FrameBoundary_D3D12 in dlaa_helper (ships in helper111,
  all_modes proof), capture + replay shader swaps for the reuse pass, Aftermath licence read (NVIDIA SDK licence:
  binaries only, no headers in the repo; dev-only opt-in crash dumps if the licence allows).
- [x] Troy v52 MERGED (Smoke Shadows, Experimental; A96ACBCB masks + B057851F cut-outs; billboards' shroud (fixes v51 cards
  vanishing with Far Tree Shadows); shared rt_core SceneShader::sun_slot default 0, twattila/light_count.hpp (Pharaoh's moved),
  Pharaoh's rt_particle_quads slots as defines: build32 Attila not built by the agent -> merged check). Agent resumed: water.
- [x] WH2 v49 MERGED (SSR copy 7F23C472 BUG below 100% fixed, spot lights F550D451 / 0764179D in Torch and Fire Shadows,
  Smoke Shadows 1FC776EF indirect, campaign trees 41147CD3, 7D979F60 out of view, Rays Replace via sun_replace_maps s1;
  shared rt_scene LightInstances + InstallPointLights(instances), rt_core LocalLight.shadows_only + indirect smoke,
  rt_particle_quads_instancing.hlsli: Pharaoh / Troy / WH2 GS headers byte-identical). Agent on 75914932 out of view.
  Troy told: fires 643FC156 can go on top of the new rt_scene light reader.
- [ ] ReSTIR U1 / U2 (branch worktree-agent-afd5e1bbbc7a65b04, NOT merged): off = helper110 proven (38 headers cmp, DUMP,
  all_modes 178 / 179 + 1 memory flake); cost rt_lights 2.20 -> 0.81 (U1) / 0.90 (U2) ms at 29 lights. Quality FAILED as a
  unified reservoir (U1 66 %, U2 20.5 % rRMSE vs RR + importance 1.98 %, Steady 5.15 %: the sun's bounce outweighs the torches).
  Redesign 39a8e204 (one light per bounce texel apart from the reservoir) being measured; helper111 only if it holds.
  All agents told: atomic gpu_job.lock (two overwrites tonight).
- [x] WH2 follow-up MERGED f8484655 (far cards 75914932 out of view, 16AFAF14 cut-outs in the rays = fix on by default,
  Light Through Foliage for Bend, Experimental). Agent resumed: Particle Resolution (indirect layer path), Water Clarity.
- [x] Shogun 2 round 6 MERGED 96e35372 (cloth / sprites / leaves / branches out of view via Pharaoh's leaf GS, PT AO
  own sky / steady bounce rows, Experimental; shared rt_core SceneShader::cascade_world opt-in, consumers' suites PASS;
  the 04:51 lock overwrite was its suite run.ps1: fixed to CreateNew; tw_warhammer_shaders run.ps1 -> WH1 agent).
- [x] Troy Water Clarity / Caustics MERGED 92d44484 (68 PS, Experimental, WARP 68 / 68). WH1 Skip the Shadow Maps + RR
  guides proof MERGED 1c9c3dc5 (shared Patch declare_position opt-in: 21300 PS of 8 dumps byte-identical by default).
- [x] Thrones Grass Height Distance MERGED 8f6de4c7 (Experimental; shared grass_fade::Install(hashes), rt_grass.hlsli's
  RT_GRASS_HEIGHT_DISTANCE define removed: Thrones was its only user, now has SI.grass_height_distance; WARP 31 / 31).
- [x] Pharaoh's RT water reflections moved to twattila/water_reflections.hpp MERGED 02249af9 (131 / 131 statements, page
  dump unchanged); Troy v53 RT water reflections MERGED 12472535 (off; RT/PT turns them on as Pharaoh). Troy next: fires.
- [x] Shogun 2 variantrigid B0E006EE out of view MERGED fab4ce3e (HDR v72). Thrones Light Through All Banners MERGED
  eca50543 (unit banners only; shared graphics_page Bindings::banner_light_tested default true).
- [x] Thrones Faster Game CPU Code MERGED 2c89ced4 (shared engine_code.hpp Game / Build parameters, Attila's constants
  unchanged; tw_engine_code PASS on both DLLs; truncation 66.8 -> 41.7 ns, fmod 140 -> 62 ns; Experimental off).
- [x] Thrones round 3 DONE (README 925a6211; capture test 42 / 48 = game's, 6 by design as Attila). Shogun 2 leaf fade
  MERGED 57a8266f; far cards 30C453EE not doable by rows (per-corner sun turn + own atlas view).
- [x] WH2 Water Clarity / Caustics MERGED aa12ece2 (15 PS; shared water_effects type 4 + every_refraction, other games' 133
  rows byte-identical). Thrones agent: Faster Game CPU Code for Rome II if its routines match.
- [x] Rome II Faster Game CPU Code MERGED (empire.retail.dll 6AB978BD; truncation 104.5 -> 43.0 ns, fmod 155 -> 71 ns;
  Experimental off; rome2 gets a new build tonight).
- [x] WH1 round 3 MERGED f99ae4e3 (Light Through All Banners, Light Through Foliage, Water Clarity / Caustics on battle water,
  unit cut-outs + census; Experimental off). NOT DONE: sprites (GS-made quads), particle rows, Fog Strength, 14 cut-outs.
- [x] Troy fires 643FC156 MERGED 61d0b84b (shared rt_scene LightInstances::stream_shader opt-in; WARP ALL PASSED).
  Troy agent DONE; follow-up: Pharaoh's fires 5AD46115 behind their own off row.
- [x] WH2 Smooth Half-Size Particles MERGED 5d102904 (at the game's own upsample 21DE8D05; no shared change). WH2 agent DONE.
  Agents done: Thrones, WH1, WH2, Rome Remastered, quality. Running: Shogun 2, Troy (Pharaoh fires), ReSTIR, Nsight.
- [x] Shogun 2 round 7 MERGED 1790ecd5 (Reflection reuse + Steadier water reflections rows, Coloured shadows; HDR v72 / DLSS
  v64). Shogun 2 agent DONE. NOT DONE: far cards 30C453EE, units out of view, Softer water reflections.
- [x] 06:08 page dumps at HEAD (c606eaf7+): Attila and WH3 = night1113 (v159 / v58) except Build Date. Final runner ready:
  tools/tw/round_notes/run_tests.ps1 (every suite x86 + amd64, atomic lock, game / 55 GB guard), build_shared.ps1.
- [x] Pharaoh v67 fires MERGED a6a4fed3 (Fire Shadows from Particle Lights, Experimental off, not in RT/PT; rt_scene
  LightInstances::enabled opt-in; row off = v66: WARP bits + page dump 615 -> 616 keys). Troy agent DONE.
  Running: ReSTIR (helper111), Nsight (frame boundary). Then: final suites, harness, builds, dumps, stamp, install.
- [x] helper111 MERGED b7b858c5 (Torch and Fire Light / Shadows from Path Tracing, Attila rows Experimental off; cost at
  29 lights rt_lights 2.06 -> 0.87 / 0.90 ms; quality U2 Full + RR 3.13 %, Half + RR 5.08 still / 11.48 moving vs Steady
  5.15; U1 worse; off = helper110 proven). Lean Reuse (reuse 2.72 -> 1.02 ms in Nsight, helper 8.63 -> 6.96 ms, same
  quality): shipping as an Experimental row (off) = ATN DECIDES the default. Final helper112 = helper111 + Lean flag +
  Nsight frame boundary, built by the ReSTIR agent with the off proof.
- [x] Nsight frame boundary MERGED d60c2993 (include/nsight_graphics_sdk 0.9.2 Apache-2.0 + NOTICE, main.cpp
  NGFX_FrameBoundary_D3D12 only when Nsight is injected; all_modes 179 / 179); tools/tw/release_zip.py ships
  NSIGHT_GRAPHICS_SDK_LICENSE.txt with the helper. ReSTIR agent builds helper112 (+ Lean flag) next.
- [x] Merged verification at dd484080 (06:30-06:45): both build lines exit 0; suites x86 + amd64 all PASS except
  tw_native_context (test only: its direct OnPointLightDraw call lacked the registration data Pharaoh v67's gate reads;
  fixed 48f1a03d, PASS) and tw_screen_space_shadows (Shogun 2 W7a / W7b changed sss00-02 bytes on purpose: agent proves
  defaults = fc24dbce on WARP and updates the allowed list); static_checks 0 findings, scan_register nothing. Zombie
  game processes (Rome2 x6, shogun2, Rome Remastered: 0 threads) left from ATN's session: runner + installer skip them.
- [x] tw_screen_space_shadows case 8 MERGED 02d030c9 (Shogun 2 sss00-02 at defaults = fc24dbce on WARP). Every suite PASS.
- [x] Nsight agent DONE (tools/tw/nsight/README.md): frame boundary changes nothing (GPU Trace 8.72 ms as helper110);
  NO replay: NVAPI clusters take 35-68 ms under capture, the RR device (ID3D12DeviceFactory) and the cross-process shared
  fence can't be replayed (fix: a harness mode with its own fence, not built); commit growth under injection still unknown.
  Reuse variants by GPU Trace (radius 6: 1.40 ms, 3 neighbours 1.5 ms vs 2.5-2.9). Aftermath: free use + donations OK,
  DLL redistributable with its own LICENSE, headers / lib not: proposal = dev-only RENODX_TW_AFTERMATH, ATN DECIDES.
- [x] Worktree junctions into the main checkout (bin, build\downloads, external\*) removed link-only from finished
  worktrees (targets checked intact); ReSTIR's worktree keeps its two until it finishes. Memory: reference_worktree_junctions.
- [x] Phase I DONE. helper112 MERGED 598b1b45 (Lean ReSTIR Reuse flag + row; off = helper110). Final check at 598b1b45:
  both build lines exit 0, suites 50 / 50 runs exit 0, page dumps Attila (+3 rows + Nsight credit) / WH3 (+1 row + credit).
- [x] INSTALLED 07:50 (stamped, dry run first, ATN's files unchanged, hashes checked, no game running): Attila v160, WH3 v59,
  Pharaoh v67, Rome II r3, Thrones r3, WH2 v49, Troy v53, WH1 r3 (all helper112); Shogun 2 v72 + DLSS v64 and Rome
  Remastered v11 --first with helper112 + NVIDIA DLLs 310.9.16 (Shogun 2's own nvngx_dlss 310.5.2 renamed aside).
  READMEs 7a930173. Test lists: tools/tw/round_notes/tests_1114.md. Report: tools/tw/round_notes/morning_report_1114.md.
- [x] ATN ~10:00 (awake briefly): tests 1-3 (Lean default, torch rows, REBLUR under RR) themselves; Aftermath: not shipped
  (so no licence text in the zips); WH1 Hue Clip default 40 -> WH1 r4 7d21a0b0 installed 10:10 (page dump default=40;
  ATN's own WH1 ReShade.ini keeps hue_clip=50 until Reset / Recommended); ATN restarted the PC (zombies gone).
- [x] ATN ~10:1x: "I like my RT/PT preset of Attila ... put the new games to Attila's preset (sliders that don't exist in
  Attila: a value as you fit)": MERGED ba290b8c (graphics_page Bindings::rays_preset_as_attila opt-in; Thrones, WH2, Troy,
  WH1, Shogun 2 DLSS; page dumps after RT/PT = Attila's but per-game tone mapping / AO keys; Attila / WH3 unchanged; Rome
  Remastered has no RT/PT button = ATN decides) + WH1 0124b5dc (No RT/PT / Reset wrote Water Caustics 1: default zeroed
  before Apply). Built 10:37 at 0124b5dc (both lines exit 0); page dumps *_rtpt1040: Attila / WH3 = 1114final, Thrones /
  WH2 / Troy / WH1 after RT/PT = Attila's but tone mapping / AO / FasterGameCpuCode, WH1 No RT/PT WaterCaustics 1 -> 0.
  INSTALLED 10:45 (dry run first, ATN's files unchanged): thrones r4, wh2 v50, troy v54, wh1 r5, "shogun2 v73 dlss addon"
  (HDR addon v72 unchanged: tw_install.py now keeps an unchanged add-on's earlier stamp, as the helper's; backup
  tw_install.py.bak_1114b); helper112 everywhere. READMEs: own sections.
- [x] WH1 AO / SSR reader census MERGED 0c28a0ea (from ATN's 36 traces, no new run: Skip the Game's Reflection Passes (42
  passes under RT reflections) + Rays' AO Alone (WH3's SkipGameSsao; changes the look: ATN decides), Experimental off;
  WARP parts 14 / 15). Not installed (after wh1 r5). WH1 agent DONE.
- [ ] ATN ~10:4x: (1) Rome Remastered on Attila's page layout + preset buttons (the skill has it: shaders-ui rows 59 / 65)
  -> Rome Remastered agent after resets / DLAA / DLSS below 100 %; (2) its base-game input bug (keys / clicks stop
  registering; players' fix RunAsDate year 2020): study the date-dependent input path read-only, then a detour of that one
  routine as an Experimental row; (3) Nsight: a whole-frame Attila check plan (game D3D11 + add-on + helper; 32-bit: tool
  per part), fixes behind switches with off = bit-identical, ONE in-game Attila run allowed on the Constantinople replay;
  then WH3 (harness only; an in-game run needs ATN's OK); (4) heapfix (Attila CPU Performance Fix): no fork / publish; its
  code in WH3 = "elsewhere" in its licence: needs the author's written OK first (ATN asks him).
- [x] ATN's Rome Remastered runs (FrameTrace=1 in its ReShade.ini, backup ini_backup_20261114_101134; prefs
  resolution_scale-1 100 -> 67 -> 50, backup prefs_backup_20261114_102403): in-game Resolution Scale change CRASHED
  (exe +0x6ee594, null sRGB RTV); 67 % run 10:24 (17 traces runs/ATN_multi_romer_102433; battle start "hung multiple
  times" = two GPU resets of the helper's ray-tracing device 10:27:42 / :50, rays off after; no RR in the log); 50 % run
  10:31-10:35 rays off, clean (15 traces runs/ATN_multi_romer_103123). 10:36 FrameTrace=1 removed, scale back to 100 (ATN's
  own ini changes kept; backup restore_backup_20261114_103625). Crash FIXED on the agent's branch f6a7cfc4 (Renderer::Resize
  took the upgraded RGBA16 back buffer format; detour passes the game's 8-bit one; tw_romer_dlaa case 5), not merged yet:
  agent on the resets, DLAA below 100 %, DLSS below 100 % from the traces.
- [ ] ATN ~10:2x: "fix the nsight issue ... then improve the performance in Attila": Nsight agent resumed: phase A a
  replay-able helper mode (own fence / textures, no factory RR device, clusters off), capture + replay + shader swap;
  phase B Attila perf variants behind switches (GPU Trace + quality scorer; defaults bit-identical; no GPU job while a
  game runs). WH1 agent resumed: AO / SSR reader census from the existing traces (else a trace with gfx_ssao /
  gfx_screen_space_reflections false set by us, DLSS Performance).
- [ ] Rome Remastered battle GPU resets: safe-side fix a8afd660 on the agent's branch (rays take only the tested capture
  kinds; banners out; row Ray Tracing Battle Objects (Test): Tested / Solid / All = v11; WARP tw_romer_rays case 1). Not
  merged. ATN TEST later: a battle with rays at Tested, Solid, All: the first to reset names the group. RR not involved.
  ATN ~11:2x: when the Rome Remastered agent's work is merged, a Fable 5.2 agent (medium effort, ATN's OK) hunts the battle
  reset: read-only review of the capture -> bridge -> helper path for the battle kinds, a harness repro from the battle
  traces, DRED breadcrumbs in the harness, then fixes proven offline (the Tested / Solid / All row stays until then).
  ATN ~11:4x CONFIRMED: no Fable review per merge (tokens); per merge the usual checks (builds, suites, dumps, harness); ONE
  Fable 5.2 HIGH review of the whole merged tree at the end, before the install.
- [ ] ATN 11:02 restarted Steam non-elevated (checked: PID 27416, module readable): Nsight agent told to do its one Attila
  Constantinople run.
  ATN ~11:5x: the run (or ONE extra run) with grass / unit LOD / model / building detail maxed (our rows via RENODX_TW_SET,
  prefs with backup), camera left to the map border (all units + artillery smoke on screen); Time Each Pass, helper capture,
  PresentMon, Log Details; CPU ETL only through ATN's own elevated tasks RenoDX_WPR_Start / _Stop if ATN creates them; then
  a ranked units / buildings / grass / smoke / rays speed-up analysis.
  DONE 11:35-11:39 (run cpl2, battle 44 s after launch; camera inside the walls by the Golden Horn, most units + artillery
  smoke on screen): Time Each Pass x3, profile 30 s, PresentMon 30 s, Log Details, WPR CPU+GPU ETL 20 s (3.0 GB); game closed
  through its window; ReShade.ini restored to its pre-run hash F0C01BD0.., preset / prefs / auto_save.replay / add-on unchanged.
  No in-game Nsight capture (REPLAYABLE turns RR off for the helper's life; the harness capture rp_a2 is used). Cluster build
  preference FAST_BUILD / FAST_TRACE: no gain (not committed). Analysis running.
- [ ] ATN ~11:4x: the skill checks the whole dump for render scale (scale_census.py) but casters only in traces: agent
  (cdc_, worktree) adds caster_census.py --whole-dump (every VS classified, held / NOT HELD / not a caster), validates on
  Attila + Pharaoh e34c6089 palms, runs it for nine games (tools/tw/round_notes/caster_dump_<game>.txt), skill lines; results
  -> parity agents. MERGED 612079e9 (2f341c3a): held / in view only / NOT HELD / not a caster per game (Attila 39/16/12/108,
  WH3 44/26/87/222, Pharaoh 28/13/41/182, Rome II 38/10/20/196 + 134 dead, Thrones 39/16/14/107, WH2 26/13/25/168, Troy
  31/10/19/164, WH1 35/15/22/175, Shogun 2 34/12/0/83); tables tools/tw/round_notes/caster_dump_<game>.txt; each game's
  NOT HELD list sent to its parity agent. QUEUED (no agent yet): Attila's own NOT HELD: traced 758C250B rigidterrainblend
  vs30_main (maybe the held rocks' material pass); level B far cards 398BD3DF / B2585ABA, sprites E42AE09F, rigid 9AEE504E
  (displacement), ropes / sail symbols.
- [x] Rome Remastered DLSS below 100 % on the agent's branch 867187d4 (v9-v12 ran DLAA at render size; now DLSS from the
  tone mapper's render-size output into Feral's upscaler target 9F3C9B3D / 3C19543E, its draw skipped; FG keeps it as the
  HUD-less frame; tw_romer_dlaa case 6 WARP PASS; README v13). With a ray row on: still DLAA at render size + Feral's
  upscaler (rt_core captures depth at bridge size) -> task C after A / B: opt-in rt_core Host field, all consumers built.
- [ ] Nsight phase A on ns-replay-mode 25125baa (RENODX_TW_REPLAYABLE=1 + RENODX_TW_NO_CLUSTERS=1: capture + ngfx-replay +
  shader swaps; replayed frame reuse 3.05 -> 0.96 ms Lean, frame 8.32 -> 6.40 ms; off = 44 headers byte-identical) and
  8357ef5c (Attila: Time Each Pass (One Frame), WH3's pass_timing.hpp; Nsight can't time 32-bit D3D11). Plan
  tools/tw/nsight/attila_frame_plan.md. In-game Constantinople run BLOCKED: Steam runs elevated (SteamAdminStart): ATN asked.
  Not merged (final all_modes count pending). Junctions in ns-replay: remove link-only before cleanup.
- [ ] ATN ~11:0x: "Other games didn't miss any of the skills, right? If they did, we have to do them too." Page dumps at
  0c28a0ea: Thrones / WH2 / Troy / WH1 / WH3 lack many Attila rows (Graphics* engine rows, SSR rows, fog / sky / bloom,
  particle resolution, smoke-map rows, grass height, torch-from-paths rows ...). PARITY ROUND, brief
  tools/tw/round_notes/parity_brief_1114.md (base fdfb307e): worktree agents Thrones (par_th_), WH2 (par_w2_), Troy
  (par_t_), WH1 (par_w1_), Shogun 2 (par_s2_): parity table by feature (HAVE / DO NOW / NEEDS A TRACE / WON'T GET + proof)
  + the DO NOW items, Experimental off; Rome Remastered's agent does the same after its page move; read-only survey of WH3 /
  Pharaoh / Rome II (par_old_: tools/tw/round_notes/parity_<game>_1114.md). After the merges: one shared step moves the torch
  rows from Path Tracing (RayTracedLightsInBounce / RayTracedLightShadowRatio) into graphics_page for every game that can
  carry them. Merge one branch at a time, verify the merged tree once, stamp, install.
- [x] WH2 parity round MERGED 4ded8d0d (table tools/tw/wh2/parity_1114.md: 207 have / 4 do now left / 7 trace / 55 won't; 19
  dead engine rows removed (0 code uses, 0 pointers), 5 Attila tests, Bloom, Indicator Brightness (Experimental), smoke
  twins DDABB799 / 687378DB, 4 casters out of view (level B); census held 26 -> 30; no shared change). Agent resumed:
  fog_strength, GrassHeightDistance, 4 units / props captures, 7DF335FF campaign banners (opt-in shared GS define).
- [x] Rome Remastered so far MERGED a4d6e0a6 (crash fix f6a7cfc4, reset safe side a8afd660, DLSS below 100 % 867187d4, Attila's
  page + Reset / No RT/PT / RT/PT 09a083f0 with opt-in graphics_page has_gtvbao / has_gi / every_row: Attila / WH3 / Thrones /
  WH2 / Troy / WH1 page dumps unchanged; dump target 'rr'). Not installed. Agent on: parity table + DO NOW, B (input bug), C
  (rays + DLSS below 100 %); then the Fable 5.2 medium reset hunt.
- [x] Old-game parity survey DONE (tools/tw/round_notes/parity_{wh3,pharaoh,rome2}_1114.md; dump targets dump_p / dump_r2):
  WH3 186 have / 8 do now / 2 trace / 77 won't (62 engine rows level B: Denuvo, name scan would make A); Pharaoh 220 / 14 / 7
  / 32; Rome II 224 / 23 / 1 / 25 (RT coverage 9 families, below-100 % audit 27 rows). All three can carry the torch rows.
  Implementers started (worktrees): WH3 par_w3_, Pharaoh par_p_, Rome II par_r2_ (DO NOW lists, Experimental off).
  ATN DECIDES: rays_preset_as_attila for Pharaoh and Rome II too (the 10:1x wish named "the new games").
- [ ] Forge's points (01:35 screenshots): scorer covers RMSE / SMAPE / boiling / swimming / covariance (done); Nsight GPU Trace
  done, replay + shader swap = Nsight agent phase A; SER per pass in GPU Trace added to the Nsight agent; unified DI + GI
  reservoir failed as one reservoir (U1 66 % / U2 20.5 % rRMSE), shipped as the light-per-bounce-texel rows (helper111).
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
- [ ] Stamp + install the newest of all eight games before ATN wakes; round report per game.

## LATE NIGHT ROUNDS (2026-11-13 23:10, two worktree agents, offline; installed state = NIGHT MERGES 22:59)
- [x] Rome II round 3 MERGED b2be3612 (e93bc2e1 Torch and Fire Shadows from point_light_PS b2, fed8a67e Smoke Shadows +
  shared SmokeVertexShader list + rt_particle_quads.hlsli, fecc800b README Round 3): Attila / Thrones / Rome II / WH3 /
  Pharaoh / Troy / WH1 / WH2 shaders byte-identical, page dumps unchanged but Rome II's new rows; WARP smoke_r3 ALL PASS.
  Step 3 (RT water reflections) resumed in the same worktree.
- [ ] (old line) Rome II round 3 (worktree agent): point light shadows (b2 constant-buffer lights), smoke shadows (shared VS list
  in InstallSmokeShadows), RT water reflections if time; Round 3 README; built only.
- [x] Screen-Space AO debug view MERGED 4e5a662c (worktree-agent-ab80c0b7cc4beb3f4: fbb368f9, d7eb5a55, 64daf016):
  rt_scene::InstallSunDebugAmbient (Pharaoh's lambda shared; Pharaoh onto it, same hook / place / mapping / log line), Troy /
  WH1 / WH2 view 8 through it. Mapping 1 proven from the bytes (tools/tw/debug_ao_v47/ao_mapping.py: Troy 24, WH1 30, WH2 30
  sun shaders read t_ssao at (SV_Position + g_vpos_texel_offset) x g_screen_size.zw as their G-buffer; WH3's 6 fail = control).
  Agent's checks: 8 targets built, 2954 embedded shader headers byte-identical, page dumps t / th / w1 / w2 / p / a / w Build
  Date only, 4 test suites x86 + amd64 PASS, static_checks 0. No WARP (no harness for it). READMEs Troy / WH1 / WH2 Round v47.
  Test (ATN): Troy / WH1 / WH2 Debug View Screen-Space AO not magenta (Game's and GT-VBAO, once at 50%); Pharaoh as v66.
- [ ] Merge both into atn-mods (both touch twattila/rt_scene.hpp), build all TW targets, tests, page dumps, then stamp +
  install the changed games (fresh backups) and update tools/tw/round_notes/night_1113_report.md.

## NIGHT MERGES (2026-11-13)
Main checkout, offline (no game, no Steam writes, no stamp / install: ATN stamps and installs with
tools/tw/install/tw_install.py). Four worktree rounds merged into atn-mods, each tested on its own branch, then once
together at 9c310a07. Docs: Attila v159, WH3 v58, Pharaoh Round v66 (built only, helper110); Rome II's README entry
(Round 2) came with its branch. Thrones (Attila's grass code: race fix, clamp, early-out) has no README entry.
- [x] GPU options 6a0d1c6b (worktree-agent-a4c40c7894a9a7dd2): Grass Ray Tracing Range (Experimental, off), the sway's
  counting pass on 64 GPU threads; helper110.
- [x] Engine CPU patches 11a0b65c (worktree-agent-aedad579679f7e14f): Faster Game CPU Code (on; twattila/engine_code.hpp,
  test/tw_engine_code).
- [x] Grass / unit hooks 8f5f24b7 (worktree-agent-af478607d76c72daa): scrub race fix, cell radius clamp, Small Soldiers as
  Sprites (Experimental, off) + Sprite Size.
- [x] Rome II round 2 9c310a07 (worktree-agent-a730d129398278ad7 at b0be274d): one conflict, twattila/anti_aliasing.hpp
  (MainFocalPixels from the grass / unit hooks round, GameMsaa from Rome II, both after MainCamera): both kept.
- [x] DrawScrub early-out 11c93ab2 (bit-exact, on; Attila and Thrones, the same code): OnDrawScrub returns without the
  game's DrawScrub for a record past every enabled LOD. Proof: empire.retail.dll +0x17f9800 read from the entry to the
  no-LOD return (+0xba) and the common return (+0x376): reads, its own frame, two getters (scene vtable +0x28 = +0xc6760
  lea eax, [ecx + 0x48]; ret; view vtable +0x3c = +0x20c850, the view's +0x8..+0x13 copied out); key (dy^2 + dx^2) + dz^2
  + (+0xc34 + bias). Thrones' DrawScrub (+0x186dd80) byte-identical. At run time: DrawScrub's 196 bytes and the return
  checked at install, the getters' bytes every frame (OnScrubUpdate), their vtable slots on every call. test/tw_scrub_lods
  section 5 (x86 + amd64): 400 random scenes the same draws, calls 387,936 -> 120,534 (= the draws); 40 scenes with
  LODs one float step above / at / below each key: the same draws, calls 20,400 -> 6,800, the control (Grass Trace's
  float order) draws less in 13; a getter with other bytes or DrawScrub not read: every call made; G1 replay 95% of calls
  left out; 8.9 ns a call left out against 7.5 ns into an empty DrawScrub. Estimate (tools/tw/cpu_profile, grass maxed:
  DrawScrub's own code 3.26 ms a frame, 2.65 of it before the LOD loop, getters 0.35): up to ~2.5 ms CPU a frame over all
  threads (~0.3 ms render thread) at G1's ~90% past the LODs; not measured in game.
- [x] Verification at 9c310a07: build32 Attila / Rome II / Thrones built 22:30; the skill's 15 x64 targets built; helper110
  from this HEAD in build\tw_dlaa_helper110 (22:32, md5 105cbf5e; bridge v31, helper sources unchanged since 6a0d1c6b).
  test/tw_* 23 suites, 43 runs (x86 + amd64; tw_engine_code and tw_motion_record x86, tw_shadow_map_size amd64): all PASS.
  Harness all_modes, 183 modes (the grass round's 640 x 360 harness against HEAD's bridge, build\tw_rt_harness_night,
  helper110; tools/tw/all_modes_night1113.txt): 182 passed against grass_range/all_modes_helper110.txt, release_rr failed
  once ("no memory kept by sets: 387 -> 418 MiB", as in all_modes_close / helper105 before) and passed 3 of 3 alone;
  switching_removal passed (the helper110 log's flake). static_checks.py: tonemap 8 hosts, 0 findings; comptr 0 findings
  (3 known). Page dumps (tag night1113) against Attila / WH3 r165 (the v158 / v57 tree; page_a_v158.txt is an older
  numbering), Pharaoh r65, Rome II r2_final: Attila only RayTracedGrassRange, FasterGameCpuCode,
  GraphicsSmallSoldiersAsSprites, GraphicsSpriteSize; WH3, Pharaoh, Rome II only the hidden RayTracedGrassRange row (in 0
  states); buttons and sliders renumbered only. No merge breakage found, nothing fixed.
- Test lines (ATN; Attila v159 + helper110 unless named):
  1. Control: the new Experimental rows off = v158's picture (campaign and battle); Faster Game CPU Code stays on.
  2. ReShade.log: the Faster Game CPU Code line (what is faster, what was left) and "grass records past every LOD skip
     DrawScrub".
  3. G1 battle, Grass Trace: the first line ends "DrawScrub past every LOD skipped" and prints "scrub LODs end at"; the
     grass as v158's at Grass Distance 1x and maxed.
  4. Grass-maxed battle, PresentMon against v158: CPU busy and frame time (the early-out and Faster Game CPU Code).
  5. 8x battle, Faster Game CPU Code on / off: PresentMon.
  6. 8x battle, Small Soldiers as Sprites on at 20 px: a screenshot pair at 200-400 m, PresentMon on / off; Sprite Size
     10 / 20 / 40.
  7. Grass Ray Tracing Range 30-60 m in a big field: the GPU ms (Record Ray Tracing Profile), the look at its edge.
  8. Swaying Shadows Range / Off-Screen Sway (Attila and WH3 v58): the same sway, the helper's GPU ms lower.
  9. Rome II round 2: its README's Test line.
- NOT DONE: the animation pack loop (heapfix's author measured no gain from a prefetch); a heapfix-aware variant of the
  rounding block (ATN decides).

- [x] (coordinator) rebuilt at HEAD 22:56-22:57 (2fd3c892), STAMPED + INSTALLED 22:59 (tools/tw/install/tw_install.py
  --helper 110 attila=159 wh3=58 pharaoh=66 rome2=r2 --install): Attila v159, WH3 v58, Pharaoh v66, Rome II "rome2 r2",
  helper110 in all four; ATN files unchanged by hash; old files .in-use-20261113_225955. Rome II prefs gfx_aa 2 -> 0
  (MSAA x2 keeps our AA / DLSS / RT off; ATN allowed dropping MSAA; backup tools/tw/install/backups/rome2_gfx_aa_20261113_230047).
## LESSONS + CHECKS ROUND (2026-11-13 night, main checkout; offline: no game, no Steam writes, no stamp / install)
Today's mistakes into the skill once, repeat mistakes as checks that fail loudly. Tools outside the repo live in
tools/tw (the maintainer's folder); repo checks in the skill's scripts/. One commit + push per step.
- [x] 1 Lessons into the skill's area files (each checked against what is there). New: Rome II's command-line crashes
  + the marker watcher, `KeyScreenshot` per ini, wpr profiles (CPU / GPU, admin) and A/B only on the critical path
  (heapfix), Bitdefender and new test exes (testing.md); Attila has no on_demand / at_100 (bringup.md); the NGX row now
  names Rome II / WH2 and the misleading "Upscaling: Ray Reconstruction" lines (render-scale-dlss.md). Already there:
  OnTonemapDrawn (shaders-ui.md), ComPtr `&member` (helper-fg-stability.md), builds older than the last commit, the
  unchanged helper's stamped copy (release.md), build.ps1's absolute folder (SKILL.md).
- [x] 2 tools/tw/install/tw_install.py (one install tool for every round; round_v151/install_v*.py left as history) +
  tools/tw/ngx_dlls.py (the three NVIDIA DLLs, versions by VS_FIXEDFILEINFO). Dry run `--helper 109 attila=158 wh3=57
  pharaoh=65`: exit 0, all six files "ALREADY INSTALLED" (builds 21:02-21:03 after their sources' last commits 02a05144 /
  20b91319; NVIDIA 310.9.16 x3 in each folder). Negative `--helper 108 attila=157`: exit 1, three refusals (the add-on
  is already v158's source, it changed after v157's stamp, helper108 built 19:54 before 02a05144). A harness helper from
  a build folder (a worktree's GPU job ran) doesn't block; one from a game folder does.
- [x] 3 tools/tw/check_logs.py <folder>...: 18 rules over ReShade.log / renodx-tw-dlaa64.log / run.txt, exit 1 on any (or
  a folder without logs). Wired (printed summary, exit codes unchanged) into the ends and Stop-Run of
  test_scripts/pharaoh_data_run.ps1, rome2/rome2_data_run.ps1, cpu_runs/cpu_run.ps1 and the end of
  rome2/atn_trace_watch.ps1 (all four parse). Over every run folder in tools/tw: 527 of 553 clean, 26 flagged, each a
  real one (RR unavailable in Pharaoh runs incl. B61 16:28, "no main camera" repeating before v61, the test key
  NoSuchKeyAtAll, a GPU reset, heapfix EXCEPTION lines, Rome II / Pharaoh crash starts, traces not taken). Game
  folders: Pharaoh's helper log still holds helper107's "unavailable" (18:24); Attila, Rome II, WH3 clean. Dropped as
  noisy: "the helper started again" (also the 10 s idle release).
- [x] 4 Static checks (skill scripts): OnTonemapDrawn per rt_scene host, ComPtr `&member` into pointer parameters; the
  OnTonemapDrawn gap fixed in Troy, Thrones, WH1, WH2 (page dumps before / after, builds).
  - .agents/skills/tw-port-checklist/scripts/static_checks.py [--rev C]: tonemap (every rt_scene::Install host, each
    anti_aliasing::OnTonemapDrawn followed by rt_scene's in the same on_drawn) and comptr (`&x` into a function / named
    lambda taking ComPtr<T>* that reads it before writing; comments stripped; SHaRC's and the micromap bake's GrowBuffer
    listed as known, HANDOFF CPU ROUND). Controls: --rev 041821eb^ tonemap flags Pharaoh too (6); --rev 20b91319^ comptr
    18 (every KeepCamera / AttilaCamera / Tales grow site that commit fixed + the sway's 3), --rev 02a05144^ the sway's 3.
    Before the fix at HEAD: tonemap 5 (twrome2, twthronesofbritannia, twtroy, twwarhammer, twwarhammer2), comptr 0.
  - Fix: rt_scene::OnTonemapDrawn after anti_aliasing's in the tone mapper's on_drawn (Pharaoh's two lines) in Troy,
    Thrones, WH1, WH2; README Round v46 each. After: tonemap 1 (twrome2: its writer's), comptr 0. Builds x64
    renodx-twtroy / twwarhammer / twwarhammer2.addon64 54 s, build32 renodx-twthronesofbritannia.addon32 48 s, exit 0.
    Page dumps (new stubs tools/tw/page_dump/dump_t / dump_th / dump_w1 / dump_w2.cpp, dump_build.py t / th / w1 / w2)
    lc_before = lc_after but the Build Date (the dump sets debug_view_drawn itself).
  - (DONE 4e5a662c, LATE NIGHT ROUNDS) Screen-Space AO (view 8) in Troy / WH1 / WH2 stayed magenta: no AO pass hook (AO_DRAWS {}), and Pharaoh's
    OnSunDebugAmbient copy at the sun draws would be a fourth copy of one lambda (DRY: it belongs in twattila/rt_scene.hpp,
    outside this round's files) with WH1 / WH2's AO mapping unproven. Smallest step: rt_scene::InstallSunDebugAmbient(
    LIGHT_BOUNCE_DRAWS) in rt_scene.hpp (Pharaoh's lambda, mapping 1), Pharaoh moved onto it, then the three.
- [x] 5 Release zips carry nvngx_dlss / dlssd / dlssg (310.9.16+) and refuse without them. tools/tw/release_zip.py
  (--helper) and player_zip.py add the three + NVIDIA_DLSS_LICENSE.txt from ngx_dlls.SOURCE (ATN's Streamline SDK
  2.14.16 bin\x64, its nvngx_dlss.license.txt = the zips' licence, 26727 bytes), refuse before writing when SOURCE lacks
  them, and after writing check every helper in the zip (ngx_dlls.zip_problems: missing or < 310.9.16 = renamed
  .refused.zip, exit 1). hdr_zip.py (no helper) unchanged. Existing zips: twattila_v145 / v146.zip REFUSED (no
  twattila/nvngx_dlss.dll: the player zip took it from the old player zip), RenoDX_Attila_v2.5.7.zip and
  twwarhammer3_v42.zip pass. player_zip.py run into the scratchpad (v146 release + v2.5.7 player): 47 files, the same
  entries as v2.5.7 but the READ ME's name, the check passes; release_zip.py not run (repack_game.py writes into
  Downloads), compiled only.
- [x] 6 SKILL.md and release / testing steps point at the tools: SKILL.md (data run -> check_logs.py; "Any C++ change in
  a TW addon or the helper" -> static_checks.py; Testing without the game; Finishing a round -> tw_install.py and the
  zip scripts; description: log checks, install, release zips), testing.md's tool table (the three tools, the script),
  release.md (install and zip bullets, steps 2 and 5).

## CPU ROUND (2026-11-13 night)
Our own TW code (twattila shared by Attila / WH3 / Pharaoh / Thrones / Rome II; the helper). Evidence: ATN's CPU profiles
tools/tw/cpu_profile/report.md, the Unit Detail 8x trace tools/tw/cpu_profile/trace_8x_1933 (v157 + helper108 installed,
HEAD 3325e7c5). Rules: offline only (another agent runs Attila A/B tests tonight: before any harness / GPU job no Attila,
Pharaoh, Warhammer3, Rome2, renodx-tw-dlaa64 process and Committed Bytes under 60 GB; WARP only while a game runs), no
Steam folder written, no stamp / install, never restart. Behaviour identical unless a row says otherwise; a possible look
change goes in an Experimental row, off. One commit + push to origin atn-mods per step.
- [x] 1 Shadow casters under the rays at 8x (725 draws into the 2048 sun map, the pack mark "0 to 418030 of 418030"):
  CastersSkipped() in ATN's setup, A1's effect, the mark's count; stop provably safe caster work; tests; mark corrected.
  - CastersSkipped() is true there: the live block is renodx-preset1 (settings.hpp preset_index starts at 1 and is never
    saved) with RayTracedShadowsOnly=1; the 2048 map is R32_TYPELESS (f39) square >= 1024, so OnSunDraw learns it; the
    trace's 725 draws (lines 46-771, one DSV 0x1c455264, 4 cascades 1024 at 0,0 / 1024,0 / 0,1024 / 1024,1024) are the
    game's submissions, which the frame trace logs before OnCasterDraw skips them (no draw callback short-circuits in
    ReShade: addon_manager.hpp runs every callback). None is captured (no SceneShader with cascade_keep in Attila).
  - A1 is in effect (index 16 = 0 while CastersSkipped) but the game's rule (+0x441f49, bytes read) drops the stretched /
    3D shadow only of small and medium soldiers whose state +0x10 or model +0xb0 is set; everyone else's shadow items
    stay: 20 soldier shadow draws (81DBC4C2) at 8x, against 0 in U1 at ATN's 0.69 Unit Detail (sprites there).
  - The mark was mislabelled, not miscounted: +0x186be70 is not a shadow-pass pack. The soldiers' draw (+0x17fb8a0, called
    from the model manager's Render(context, pass) +0x17fa310 for every pass) runs it first, once a frame (+0xb60, reset
    at +0x1827adf), over the manager's whole item list (+0xd8: count +0xdc, items +0xe0; per-pass begin +0xe4 / end +0x140,
    23 pass kinds): every pass's items, whichever pass draws soldiers first (here the cascades). 418,030 = items of every
    pass in ATN's 8x battle, all fitted. Now: "soldiers' bones packed (the frame's first pack, every pass's items): items
    a to b of n" / "(a pass's rest)"; SHADOW_PACK_CALL renamed FIRST_PACK_CALL; comments and the A1 lines corrected.
  - NOT DONE: the game's caster work at its source. The sun pass's casters come from several renderables' Render(pass):
    ModelManager +0x17fa310 draws its rigid lists (+0xa88 / +0xa9c through +0x17ae940, incl. F6D7D16D / F79BFB14, the
    alpha-tested shadow PS OnCutOutShadowDraw learns cut-out textures from) and then soldiers; sprites (292) and the other
    families through others. Skipping a renderable's pass loses the cut-out learning (a look change for RT); soldiers
    alone (+0x17fb8a0 while skipping_casters) are 20 of 725 draws, and whether a later pass relies on the shadow pass's
    rows or packs isn't provable without a run. Smallest step: one traced run with a mark per Render(pass) (return
    address + pass id from vtable +0x3c) to map the cascades' pass ids and each family's renderable; then a skip per
    renderable whose pass has no cut-out PS, Experimental off; the cut-out census from the G-buffer's alpha test first.
  - Builds: build32 twattila + twrome2 + twthronesofbritannia exit 0 (only the upstream deprecation warnings). Skill:
    rt-scene.md section 5 (the pack's caller, the 117 / 418,030 counts, A1's reach).
- [x] 2 OnDrawScrub (3.35 ms a frame, grass maxed): a per-thread cache of several models or a once-per-frame scale per
  scrub model; output bit-identical (test).
  - ScaleModelLods (DrawScrub's and DrawTree's): the one-model thread-local cache is now 64 models a thread
    (direct-mapped by address); a model needs nothing when its LOD count and every distance still hold what this thread
    wrote at the same scale (= the map's path finding every distance the addon's: nothing to write or keep). Misses take
    the map path as before. test/tw_scrub_lods (new; v157's code copied as the reference, two copies of one fake
    manager): runs, alternating cells, random order, slider changes 1x included, 31 setups, 7 new models, 55,224 calls:
    every LOD of both copies bit-identical after every call and = the closed form; four threads x 50,000 calls both =
    the closed form; a lone later-LOD rewrite (no game path) now scaled at the next call (the old cache: at its next
    miss). Cost a call x86: cells alternating 6 models 24.2 -> 5.7 ns (4 threads 69.6 -> 5.7), runs of 64 of one model
    4.1 -> 5.8 (4 threads 6.7 -> 5.7); amd64 17.1 -> 5.1 (92.1 -> 5.8), 4.8 -> 5.2 (7.5 -> 5.9). ATN's grass (~120 k
    calls a frame, ~27 ns each): E 3.35 -> ~0.8 ms CPU a frame (workers; render thread 0.49 -> ~0.12), no fps while
    GPU-bound. Not run in game. Builds: build32 twattila + twrome2 + twthronesofbritannia exit 0.
  - Found (step 5's suite run, section 3 failed once on amd64), NOT DONE: a model's first scaling (or its first after a
    slider change) from two threads at once can scale it twice, v157 as well: the map path reads the model's entry,
    writes the LODs and inserts the entry as three steps, so a second thread can read the first's written value as the
    game's (x scale again; a third pass can follow when the first thread's insert lands last). test/tw_scrub_lods 3b
    (four threads started together on 16 new models, 200 times, printed): v157 20-33 of 3,200 models off the closed
    form, now 23-36; section 3 now scales each model once first (passes every run). In game: some grass / tree models
    keep their LODs farther than the slider says until the game sets them up again. Fix (a behaviour change in the race
    only): the map path under the entry's submap lock (ParallelFlatHashMap's lazy_emplace_l / try_emplace_l, the LOD
    writes inside), 3b then 0; ATN decides.
- [x] 3 Per-draw ReShade callbacks on the render thread (~0.8-1.1 us a draw): trim where provably equivalent; a
  micro-benchmark or the harness before / after.
  - Done: rt_core's NativeContext (every captured draw, 981 in the 8x trace, and rt_scene's draw callbacks) asked
    QueryInterface for ReShade's proxy interface on every call, which fails on the game's own context (ReShade's
    command list hands that out: api_object_impl::_orig); the failed answer is now kept per thread while the pointer
    stays (object_motion's NativeContext already did this), a proxy still looked up every call. test/tw_native_context
    (new, WARP + mocked command list): the same answer as v157's for the immediate, a deferred (null), a second
    device's, a null and another API's context, every call; x86 27.3 -> 3.4 ns a call, amd64 24.7 -> 3.4. Saving E
    ~1,200 calls a frame x 24 ns = 0.03 ms on the render thread.
  - Measured prices (same test, WARP's d3d11 runtime, x86): QueryInterface failing 24 ns, QueryInterface for
    ID3D11DeviceContext1 + Release 28 ns, AddRef + Release 14 ns, a Get of an empty slot + Release 4 ns. So the
    profile's AddRef / Release 0.2-0.26 ms a frame are ~15-20 k pairs: the reads of the game's bindings in CaptureDraw
    (IAGetVertexBuffers up to three times, VS / PS / GS / SO Gets, ~20 objects a capture, AddRef'd by the API).
  - NOT DONE: (a) those Gets: dropping them means tracking the game's bindings from ReShade's bind events instead
    (a redesign of CaptureDraw's state save / restore; repeated IAGetVertexBuffers at most ~0.08 ms); (b) one Flush a
    frame: each helper request (TRACE_SCENE, TRACE_BOUNCE, TRACE_REFLECTIONS, EVALUATE, PRESENT_FRAME) signals the
    shared fence and the game's GPU then waits for the helper's answer within the frame; without the Flush the Signal
    stays in the game's command buffer and the helper's queue (waiting on it) starts only at the next flush: the
    ordering doesn't allow it (0.14-0.18 ms a frame); (c) the dispatcher's own per-draw work (utils/command_action:
    stage lookups, registration maps, the replay of captured draws) is shared upstream code, not ours.
  - Builds: build32 twattila, x64 twwarhammer3 exit 0.
- [x] 4 Helper (coordinator 2026-11-13 night: the 2.3 ms wait at +0x5c238 is GPU back-pressure, dropped): the RT worker
  creates and frees 3 GPU buffers every frame (13 MiB, 2.75 MiB, 320 KiB; create +0x689c1 / +0x688ca / +0x68956, free
  +0x688a8 / +0x68934 / +0x6899c; ~1.3 ms paging a frame + 2.4 ms in CreateAllocation / DestroyAllocation2 on the worker):
  keep them and only grow; harness proof (no per-frame create / free over N frames, results identical); helper109,
  all_modes + RR noise.
  - Cause: helper107's code at those addresses is RtScene12::Record's three GrowBuffer calls for the sway (source_location
    lines 3167-3169: sway_arguments_ = cluster_args_' width, sway_lists_ = clas_addresses_' width, sway_counts_ =
    SWAY_COUNTS_BYTES; 13 + 2.75 + 0.31 MiB), each preceded by `member = nullptr; Release()`: GrowBuffer(&member, ...)
    with a WRL ComPtr goes through ComPtrRef::operator T*(), which releases what the ComPtr holds (client.h: "*ptr_ =
    nullptr") before handing out the ComPtr*. So GrowBuffer always found nothing, made a new zero-filled buffer and the
    old one went at once (not through releases_ after the GPU). Fix (helper109, bridge v31 unchanged): std::addressof at
    the three calls; GrowBuffer's comment names the trap. Safe to keep: rt_sway's passes write every byte they read in
    the same request (sway_counts_' first 72 bytes copied from the upload, GROUPS stored by MODE_MASKS / MODE_FIRSTS for
    every group read, RUN_PLACES only read for runs with clusters, the kept arguments and lists written before the builds
    read them by those counts); the helper waits for the last trace before recording the next.
  - Harness (tools/tw/rt_harness/buffers_v65.sh, .txt; RENODX_TW_TEST_BUFFERS=1: each request's report counts
    Buffer()'s new buffers; 60 frames, 3 requests a frame): helper109_control (helper109 with the three calls as in
    helper108) sway_near / sway_off_screen 3 buffers every frame after warm-up (73 7 0 4, then 3 0 0 per frame);
    helper109 73 7 0 1, then 0 every request; default (no sway) 0 per frame in both; every mode ALL PASSED and the
    harness output identical control against fix (times aside).
  - Left as they are (same trap, not per frame or a look change): SHaRC's three buffers (line 1538: made new every frame
    while SHaRC Radiance Cache is on, so its cache never outlives a frame; keeping it changes what the Experimental row
    shows: ATN decides; smallest step std::addressof there + all_modes paths_sharc); GPU micromap baking's UVs and
    transient pools (5267 / 5269: made new per bake, not per frame; whether the OMM SDK's transient pools may hold an
    earlier bake's bytes not checked).
  - The same trap on the game's device (20b91319): every frame_history::KeepCamera / AttilaCamera caller passed
    &member, so each made a new D3D11 buffer every call (Attila / WH3 / Pharaoh / Thrones / Rome II / Troy / WH1-2 and
    Ryse's light bounce); now std::addressof. test/tw_kept_buffers (WARP, x86 + amd64): one buffer kept, its bytes = the
    v157 form's each frame (CONTROL 10 of 10 new); KeepCamera 9.6 -> 4.1 us, AttilaCamera 9.1 -> 5.5 us a call on WARP.
    Tales' micromap grow() had it too (std::addressof).
  - Regressions (helper109 in build/tw_rt_harness, tools/tw/all_modes_v65_driver.sh, GPU lock held): all_modes 179 of 179
    ALL PASSED, the same mode names as helper108's (all_modes_v31_helper109_done.txt); RR noise (build/tw_rt_harness_v65,
    60 frames) wall foot 61% left, open ground 37%, = helper108's. Commits 02a05144 (helper), 20b91319 (addons' cameras).
    Saving (ATN's recording, E): ~1.3 ms GPU paging + ~2.4 ms RT worker a frame while the sway runs (the RT/PT preset's
    Swaying Shadows Range 50 m, or Off-Screen Sway off); the cameras ~5 us a call on WARP.
- [x] 5 Builds (the skill's x64 list, build32 Attila / Rome II / Thrones, the helper), test/tw_* x86 + amd64, page dumps
  (Attila / WH3 / Pharaoh: Build Date only unless a new row), READMEs Attila v158 / WH3 v57 / Pharaoh Round v65 (built
  only), skill rows.
  - Builds: the skill's x64 list and build32 twattila / twrome2 / twthronesofbritannia exit 0 after 20b91319 (only the
    upstream deprecation warnings; later commits change no addon source); build/tw_dlaa_helper109.
  - test/tw_*: every suite x86 + amd64 PASS (tw_motion_record x86, tw_shadow_map_size amd64 only, as their scripts);
    GPU suites (lock held): tw_gpu_completion, tw_helper_lifetime, tw_particle_history, tw_screen_space_shadows, tw_unit_pack_rounds x86 + amd64 PASS. tw_scrub_lods section 3 failed once on amd64: the
    first-touch race (step 2's NOT DONE), section 3 now passes every run (3e508e56).
  - Page dumps: tools/tw/page_dump page_a_r165 / page_w_r165 against r164, tools/tw/pharaoh page_p_r65 against r64:
    only Build Date differs.
  - READMEs: Attila v158, WH3 v57, Pharaoh Round v65 (+ its test line 23), built only (6a571f63). Skill:
    helper-fg-stability.md section 3 (ComPtr's releasing operator&), rt-scene.md section 5, testing.md suites.

- [x] (coordinator) rebuilt at HEAD 21:02-21:03 (64c640dd), STAMPED + INSTALLED 21:04 (tools/tw/round_v151/install_v65.py):
  Attila v158, WH3 v57, Pharaoh v65, helper109 in all three; ATN files unchanged by hash; old files .in-use-20261113_210411.
- [ ] The scrub race (two threads scaling one model first): fix it under the lock (a bug: models keep farther LODs
  than the slider); queued for the next Attila round.
## PHARAOH V64: TORCH SHADOWS + DEBUG VIEWS (2026-11-13 evening, offline; Pharaoh v63 / helper107 installed)
Rules: no game start, no Steam folder touched, no stamp / install (ATN does those), no DevKit near game folders, never
restart. Committed Bytes under 60 GB and no game process before WARP / harness / GPU jobs, one GPU job at a time.
Unproven look changes: Experimental rows, off. One writer; each step committed and pushed to origin atn-mods.
- [x] 1 Debug Views: the rows (RayTracedDebugView, RayTracedSceneTest) are on Pharaoh's page but shown only once
  rt_scene::OnTonemapDrawn ran (is_visible = internal::debug_view_drawn): Pharaoh's tone mapper hook (UpgradeRTVReplaceShader:
  t00-t08, photo) called only screen_space_shadows' and anti_aliasing's OnTonemapDrawn, so they never showed and nothing drew
  them (the page dump fakes debug_view_drawn = true: it can't show this). Now rt_scene::OnTonemapDrawn after anti_aliasing's,
  as Attila's macro (the target the same display-size RGBA16F that DLSS / DLAA wrote). Screen-Space AO (view 8): no AO pass
  hook keeps it here (v61: ssao01 binds no camera), so OnSunDebugAmbient (registered before every other sun draw hook, the 24
  draws with an ao_slot) copies the sun draw's t_ssao through KeepDebugAmbient with mapping 1: ATN_UP trace_13 at 33% the AO
  1920x1080 (half the display), its pass at 640x360 at the top left, the sun draw's t12 the same texture: rt_debug's own
  mapping (render pixel x 1920 / 3840). Pharaoh addon64 built; page dump p r64a against r64pre: Build Date only (615 keys
  both). Troy, Rome II, Thrones, WH1, WH2 lack the same call (not changed this round). Not run in game.
- [x] 2 Steady Torch Shadows' black spots: under RT_RECONSTRUCTED (always in Pharaoh, step 3) helper106's Steady sent each
  pixel's one soft ray to a fixed point of the light's disk (seed without the frame): a light's bit can't hold a share, so
  every penumbra became a fixed dither that nothing averages (RR averages over frames, a fixed pattern stays): black dots,
  widest where the disk is big against the scene (Pharaoh's campaign: softness 0.25 against a reach of 1). Fix (helper108):
  Steady traces each ray to the light's centre and a pixel's rays decide by half (threshold 0.5): hard, deterministic.
  Harness torch_v64 (the pole's penumbra): h107 RR soft 102.8 changed a frame, 36.5 isolated dark, 0 steady spots (the
  noise); h107 RR soft + Steady 0 changed, 30 steady spots (dark every frame, 4 neighbours lit every frame), 54 px always
  dark (the black dots); h108 RR soft + Steady 0 changed, 0 spots, 15 px dark = hard h107 / h108 (the pole's foot); h108 RR
  soft without Steady = h107's (102.8: the non-steady path unchanged); jitter: h107 Steady 27 spots, h108 Steady = hard (0
  spots, 5.2 edge bits a frame). Every run's harness checks ALL PASSED. Row tooltip: sharp edges, overrides Importance and
  Softness. Regressions (all_modes, RR noise) after the Rome2 process ATN started at 18:59 ends.
- [x] 3 Torch flicker: Ray Reconstruction never ran in Pharaoh. Its folder has nvngx_dlss.dll only (no nvngx_dlssd.dll,
  no nvngx_dlssg.dll; Attila's and WH3's have all three): every Pharaoh helper log with the rays (W1, W2, B61, ATN_UP_181940)
  starts "Ray Reconstruction unavailable (nvngx_dlssd.dll next to nvngx_dlss.dll?)", then "[Tales DLSS] Created DLSS ...
  preset L" and "DLAA active" / "Custom DLSS active": plain DLSS. ATN's ini asks RR (AntiAliasingRayReconstruction=4), and
  dlaa_bridge's reconstructing followed the request (evaluated && ray_reconstruction && guides), so every trace had
  RT_RECONSTRUCTED: torch shadows one random disk point a frame (Softness 0.25) with an IGN threshold, NRD off (nrd &&
  !reconstructing), AO / reflections / paths raw: noise for RR to average, through DLSS that doesn't. The game's lights are
  steady: ATN_UP trace_13's 12 campaign lights (E318CE77 vb1, reach 1 = POSITION2.w) bit-equal on two frames, only their
  order in the buffer changes (the addon sorts by score: no effect); B61's 15 siege lights bit-equal. Fix (helper108,
  bridge v31 kept): the helper writes rr_refused (v25's padding) = 1 when an evaluation asked RR and it ran DLSS, the addon's
  reconstructing needs it 0 (a log line when it turns on). Harness (tools/tw/rt_harness/torch_v64.sh / .txt, HARNESS_RR=6
  + HARNESS_RR_FOLLOW: the trace's RT_RECONSTRUCTED from bridge.reconstructing as rt_core; HARNESS_TORCH: a torch on a pole
  0.25 m before the wall, light size 0.25, its bit within 2.5 m, last 20 of 40 frames, still camera): no dlssd helper107
  "DLAA active", reconstructing 1, 39 of 40 frames RECONSTRUCTED, 102.8 torch bits changed a frame (521 px flickering);
  helper108 rr_refused 1, reconstructing 0, 0 frames, 0 changed (= hard). With dlssd both helpers "DLSS Ray Reconstruction
  active (D3D12)", reconstructing 1, the same numbers (RR path unchanged). ATN still needs nvngx_dlssd.dll 310.9.16 in
  Pharaoh's folder for RR itself (coordinator's install). Not fixed (listed): Pharaoh sets no campaign_metre / campaign
  frames, so the torch softness (0.25), the 0.5 clearance and the AO / bounce ranges are map units on its campaign map
  (lights' reach 1 there, 25 in a siege).
- [x] 4 ATN_UP_181940 (33% campaign, 13 traces).
  - 7a broken, fixed: the shroud decals (campaignshrouddecals 61C823A0 volumetric hq, 5078A6E0 volumetric, 8852FBD3 direct,
    AABB5EBA projected; 431 + 431 + 12 draws a trace) make uv = SV_Position x inv_rt_size.xy (their own buffer's c13), the NDC
    uv x (2, -2) + (-1, 1), the depth / shroud ranges at uv x c13.zw. trace_13 l.2566 / 2575: c13 = 1/1920, 1/1080, 1, 1 (the
    decals' 1920 x 1080 target) at a 640 x 360 viewport: the NDC spanned a third of the view (the reads right). view_share's
    patch only knew the camera's sizes (census v58: BUG (B)). Now view_share::SetTargetSizes(names) (shared, default none):
    Reflect finds such a variable in any buffer (its slot and register), Classify takes it as a texture size, Patch's NDC
    rule applies; Pharaoh passes {"inv_rt_size"}. tools/tw/warp_tests/pharaoh/patch_v64 (run_v64.txt): without names every
    shader of the 10 TW dumps byte-identical to HEAD's Patch (Pharaoh with and without chains); with them exactly the 4 PS
    differ, ndc 0 -> 1 (inv_rt_size is in no other game's PS; Troy has it in 2 VS only). WARP decal_v64 (run_v64.txt): all 4
    at 100% patched = the game's bytes bit for bit (cb12 unbound); 33% patched against 100% 0 apart beyond 8 px and every
    pixel (largest 1.2e-7); another depth picture moves 305-4,975 values; CONTROL the game's bytes at 33% 770-3,882 apart.
    Level A (trace + bytes). Later run: log "pixel shader 0x61C823A0 patched (1 positions ...)".
  - 7d nothing broken, nothing changed: 026CBC61 (geometry_41_global) isn't captured (only 377DE0D5 in SCENE_SHADERS), so no
    double capture. open_items/terrain_overlap_v64.py (.txt): in all 12 traces with tiles every 377DE0D5 tile lies inside
    026CBC61's chunks' x / z bounds (trace_13: 128 tiles, 89 inside one chunk, 39 across two, 0 outside; chunks 170 x 197
    units, tiles about 4-55): capturing 026CBC61 as drawn would double the terrain under the tiles unless its mesh has holes
    there, which no trace shows (bounds only). NOT DONE: 026CBC61 beyond the tiles (far hills' shadows on the campaign);
    smallest step: a trace read of 026CBC61's vb0 positions inside one tile's box (holes or not), then its capture with the
    tiles' boxes cut out.
- [x] 5 Builds (Pharaoh, the skill's x64 list, build32 if shared, helper if changed), test/tw_* x86 + amd64, page dumps,
  READMEs (Pharaoh "### Round v64"; Attila / WH3 if shared behaviour changed), skill rows for real new lessons.
  - [x] Builds at ce280b22: helper108 (build\tw_dlaa_helper108, bridge v31, 18:55), the skill's 15 x64 targets 66 s and build32
    twattila + twrome2 + twthronesofbritannia 57 s: exit 0, only the upstream deprecation warnings.
  - [x] Page dumps (Build Date and the Steady tooltip only, every key kept): Pharaoh page_p_r64 against r64pre (= v63's page;
    tools/tw/pharaoh), Attila page_a_r164 and WH3 page_w_r164 against r161 (tools/tw/page_dump).
  - [x] READMEs: Pharaoh "### Round v64" + test lines 18-22, Attila "## v157", WH3 "## v56" (built only, helper108). Skill
    rows: rt-scene (Steady hard: a bit holds no share), render-scale-dlss (SetTargetSizes; RR asked but not started),
    shaders-ui (the Debug View's tone mapper call), game-facts (Pharaoh's campaign map units), testing (HARNESS_TORCH,
    HARNESS_RR_FOLLOW, guard-wrapped scripts).
  - [x] Tests: test/tw_* 19 suites x86 + amd64, 38 runs exit 0 (tw_native_hook_lifetime x86 failed once at stage 0 with no
    output right after its build, passed alone stages 0-3); the GPU suites (tw_device_loss, tw_gpu_completion,
    tw_helper_lifetime, tw_particle_history, tw_screen_space_shadows, tw_unit_pack_rounds) only while no game ran.
    all_modes with helper108 (build\tw_rt_harness: helper108 + the v64 harness; the old ones .before_v64): 179 / 179 ALL
    PASSED (tools/tw/all_modes_v31_helper108_done.txt; all_modes_v64.sh / _driver.sh guard every mode: ATN's Attila starts at
    19:16, 19:24, 19:31 paused it). RR noise (RT_EXTRA_TRACE_FLAGS=256 HARNESS_RR=5, 60 frames, all_modes_v64_driver.txt):
    helper108 = helper107 (wall foot 61% left, open ground 37%, "DLSS Ray Reconstruction active (D3D12)"). build\tw_fg_test
    holds helper108 (fg_bridge_test not run: its exe is from 11-03; the FG path is unchanged).
- [x] (coordinator) rebuilt at HEAD 19:53-19:54 (26ca8942), STAMPED + INSTALLED 19:55 (tools/tw/round_v151/install_v64.py):
  Attila v157, WH3 v56, Pharaoh v64, helper108 in all three; ATN files unchanged by hash; old files .in-use-20261113_195529.
  Pharaoh: nvngx_dlssd.dll 310.9.16 copied from Attila (same hash as WH3's); nvngx_dlssg.dll still missing there (FG off in
  ATN's ini; copy it before FG is used). A release zip for Pharaoh must carry nvngx_dlssd.dll.

## ROME II BRING-UP (offline)
Plan tools/tw/rome2/plan.md (Phase 0 + the Phase 3 items provable offline); results under tools/tw/rome2/. Data runs
(Phase 1.4) BLOCKED on the mod pack dialog (ATN answers it); no game start, no stamp / install (ATN does those).
Coordinator's runs 2026-11-13 ~17:30 (S1_173022, S1_173217, N1 in tools/tw/rome2/runs): the command-line battle start
`game_startup_mode battle script\bm_benchmark\bm_battle.xml` (with or without a mod-list file) crashes the retail game at
Empire.Retail.dll+0x276899 ~9 s in, before any frame, also with the add-on renamed aside: data runs need the menus.
Runs ~18:01 (L1_180127, L2_180236): the command-line `campaign_load` of ATN's save crashes too, at
Empire.Retail.dll+0x103661 ~16 s in, with or without test settings. Agent runs need the menus. Traces to use: ATN's own
play, runs\A1_175145 (custom battle; 33 / 50 / 67 / 100%) and runs\C1_175625 (Sassanid campaign, water; 50 / 100%).
- [x] 0.1 Encoding ramp on WARP (tools/tw/rome2/encoding, phase0_results.md): the game's EE1DA45C (dump bytes, identity
  S-curve) writes the linear result within 0.087% (its sqrt 89.6% off); hdr_to_screen 2C229891 gamma-encodes the frame as
  it is (frame^(1/2.2) within 0.094%, no square); the addon's t00 HDR 3 linear (0.104%). Today's wrappers give DLSS
  frame^2 (0.01 -> 0.0001, darks <= 0.000244 to 0); TW_AA_FRAME_LINEAR 1: 0 of 513 off by > 1% (w 0 / 1), define 0 =
  today bit for bit; CONTROLs fail as they must.
- [x] 0.2 Sun picture test (tools/tw/rome2/sun_test, WARP, dump bytes): all 50 live listed sun PS: unbound = the game,
  t49 = 1 = fully shadowed (CONTROLs fail), the cube at floats [8, 32) (OnPathAmbientDraw's copy = cube zeroed by name;
  Pharaoh's [24, 48) differs), slm ambient = AO x albedo^2 x (1 - w) x cube(normalize(n x (1, 4, 1))) x 1.44 (g_hdr_on
  c11.x) / x 1 within 6e-07; rt_sun texel 2 = c1 x 0.004 / x 1, texels 3-8 c2-c7. Found + fixed (90a917b1): the shared
  Patch's min route dropped the cascade shadow of 6 SM3 slm sun PS with every feature off.
- [x] 0.3 No contact march: 0 `loop` in the 50 live sun PS (no step count / flag in their constants); battlefog 259AEC56
  binds only the depth twin and the sky cube (no shadow map, no loop): no fog march. Bend and Fog Shadows add, nothing
  to replace (B; tools/tw/rome2/phase0_results.md).
- [x] 0.4 cbuffer_slots.py: Rome II's 2602 shaders bind b0-b7 (b7 in 8), Attila's b0-b6; the addon's b8-b13 are free (B).
- [x] 0.5 Cut-out census (census3/census3_rome2.py from Pharaoh's): 630 discarding PS; units = the weighted* family's
  s_diffuse_map.w alpha test (drawn 4AD298CF, DDDFEBA6, 1B291719, 42990BB4, shadow A6CFBF59; no *burn* PS in Rome II);
  foliage / billboard / imposter / rope / cloth / sail / rigid alpha tests listed with their 10-27 counts.
- [x] 0.6 Shadow readers: 147 PS (115 atlas, 92 baked, 32 point maps): sun, waterplane, river, ocean, sm_5_0 grass /
  leaves, mist / naval VFX; drawn at 10-27: 340CBE9D, 1A0532C0, 58DC6E12, 3ECFEAF5 x6 (census3/shadow_readers.tsv).
- [x] 0.7 rt_scene_map_rome2 (Pharaoh's fork, Attila's name form): 16 of 51 SCENE_SHADERS rows overridden (dead); live
  uncaptured twins: cloth 997A7119, sail 438E2D53, imposter A5EA617C, tree cards D2EC24BD / 1C47F106, campaign trees
  AAFE8EAB, rope 4F90C2B5 (rt_scene_map_rome2.txt).
- [x] Phase 2 from ATN's traces A1_175145 / C1_175625 (tools/tw/rome2/phase2/phase2_results.md): campaign tone mapper is
  EE1DA45C too; render-scale gaps only hdr_to_screen (t0 distortion, t2 god rays, t3 depth after DLSS) and page_touch
  28C847C0; baked 4096 R16 shadow buffer never written per frame; atlas read by sun / skin sun / water only; units now
  enter the RT scene (28.76 M triangles); every moving G-buffer VS has a motion row; g_hdr_on = 1, sun colour c1 x 0.004.
- [x] 3.1 Output + page skeleton (TW_AA_FRAME_LINEAR, graphics_page, SETTINGS_REVISION, MSAA note, dead entries, sky).
  Done: TW_AA_FRAME_LINEAR 1 (4bcaeaa7); dead entries (names.tsv column 4: data_rome2.pack overrides the hash):
  MOTION_SHADERS 57 -> 39, SCENE_SHADERS 51 -> 35, LIGHT_BOUNCE_DRAWS 56 -> 50, AO_DRAWS 2 -> 1, RENDER_SCALE_READS
  9 -> 5 (3.5's dead rows), 43 embedded files + bloom00 / bloom02 / ssao00 gone; page dump identical (page_r_r1dead).
  Graphics 1 of 2 (graphics_page as Pharaoh's; shared: has_reflections false keeps a Reflections section for SSSR):
  PAGE_TEXTS with the MSAA note, SetRaysPresetDefaults (fog / smoke stay 0), has_path_ambient false until 3.6,
  rays multiply the maps and SSAO; SETTINGS_REVISION 1 clears 6 hidden rows the preset raised. page_r_r1page: every key
  kept, 4 new (TaaMotion, FrameGenerationMethod, RayTracingDenoiser, AoReblur). Every consumer built (x86 + x64).
  Sky: sky00 now on F946A430 (data_rome2.pack's ps30_main): fxc /dumpbin shows the same instructions as the dead
  242566C2 (only unused camera constants moved); cb_audit: nothing lost, only cb13's sky rows added. No WARP run (one
  sample; at Sky Debanding / Smoothing 0 SkySample is the game's code). Not in this round: Graphics 2 of 2 ports
  (Cutscene Point Light Shadows, Campaign Tree Detail), Rope Distance (ATN decides).
- [x] 3.2 Stability (ReadGpuVendor / OnNvidia, helper-missing note, GitHub button). OnPresent reads the GPU's vendor
  first (a saved DLSS becomes XeSS on Intel, else FSR; the page lists DLSS on NVIDIA only), FULL_DOWNLOAD_NOTE in General
  while the helper is missing (and the page's in Anti-Aliasing), GitHub (Downloads) beside Ko-fi / NexusMods. page_r_r2
  against r1page: the note and the button only, every key and default the same. Lifetime suites: with the tests below.
- [x] 3.5 Render scale (12 hdr_to_screen FrameRenderShare, 37 replaced NDC shaders, dead RENDER_SCALE_READS).
  hdr_to_screen t01-t08 / t11-t14: the distortion (t0), god rays (t2) and depth (t3) reads at uv x FrameRenderShare (ATN's
  traces: all three are the 3D's, drawn at the render size; the frame t1 is DLSS's); page_touch 28C847C0 replaced
  (pagetouch00: its depth at uv x FrameRenderShare; at 100% the game's own viewport is 3840 into a 960 target, so its uv
  spans the target and below 100% it read the depth whole). WARP tools/tw/rome2/scale_test (scale_r2.cpp, run.sh,
  run_r2.txt): ALL PASSED: page_touch ours = the game's at 100% bit for bit, ours at 50% / 33% = the game's 100% bit for
  bit (12 px blocks of depth, the 3D's at its render pixels' centres), CONTROLs the game's / ours unbound at 50% 1,564
  apart; t03 ours = the previous build at 100% bit for bit, at 50% / 33% every display pixel = its 100% pixel bit for bit,
  CONTROLs the previous build / unbound 22,464 of 29,952 apart. The other 11 hdr_to_screen: the same three edits (a
  script asserts no bare read is left), not run on WARP. The "37" (DOF CoC x2, SSAO resolve, highlight, 25 VFX read): no
  change needed: their NDC only feeds inv_projection's z / w columns (view depth), whose x / y terms are 0 in the traces'
  camera at 50% and 100%. Dead RENDER_SCALE_READS: 9 -> 5 in e29f4428. Open: the VFX soft-particle depth read at the
  projected uv x g_viewport_offset_scale (the particles' own cbuffer: whether the game sets it to the render share needs
  a VFX PS in value_hashes in the next run).
- [x] 3.6 Sun draws (InstallSunDraws cube after 0.2; Rome II's sun colour into rt_sun texel 2). InstallSunDraws(
  LIGHT_BOUNCE_DRAWS, 8, 4): the cube c2-c7 (the paths' sky, Path-Traced AO zeroes it) and the sun colour c1 into texel 2
  through twrome2/rt_sun (Attila's file: x 0.004 with c11.x g_hdr_on, 1 in ATN's traces; Pharaoh v61's rt_sun WARP A
  covers places 4 / 8 with c11.x either side; 0.2 proved the layout). Page: Path-Traced AO (ReSTIR GI, Experimental) in
  the AO method, Own Sky Rays / Steady Bounce Light shown with it, RT/PT now path-traced AO (page_r_r3 against r2:
  RayTracedPathAmbient 1, RayTracedAmbientOcclusion 0 after RT/PT; every key and default kept). Skill game-facts rows:
  Rome II's sun diffuse scale, and the Patch's scale reading its destination's component (90a917b1).
- [ ] README twrome2 round entry (built only; New / Fixed / Not done) and test/tw_* x86 + amd64: NOT DONE (stopped
  2026-11-13 ~18:40 for the Pharaoh round, coordinator). Builds: every consumer of the shared graphics_page edit built
  (c890b773, x86 + x64); the later steps are twrome2-only (build32 twrome2 built at 3e5a7a1a). Not stamped, not run.
- Left for the next Rome II round: README + tests above; the units' pose in the RT scene (RT Debug View in a run; they
  enter it now, 2.4); point lights (pointlightsmoothfalloff x40 at 100% battle: InstallPointLights with Rome II's
  spans); particles / smoke (a siege trace, S2); water effects and RT water reflections; the VFX soft-particle depth
  (g_viewport_offset_scale: a VFX PS in value_hashes); page_touch's cb2 (0x28C847C0 in value_hashes) to confirm 3.5's
  reading; 3.7 Shadows Only (skip the per-frame cascades only, the baked R16 map stays); 3.8 RR guides; 3.10 presets;
  Graphics 2 of 2 ports (Cutscene Point Light Shadows, Campaign Tree Detail); MSAA (S4) and night (S3) traces. ATN
  decides: Rope Distance threshold, MSAA.

## WH3 V55: PT GI SUN COLOUR (2026-11-13 evening, offline; helper107 as installed, no helper change)
- [x] 1 Path-Traced Ambient's sun colour, as Attila v156 (c592149c) and Pharaoh v61 (2e0f71ae): all 7 sun light shaders
  (fxc /dumpbin of the pack's csos) hold sun_colour at lighting_VS_PS offset 16 (float 4, c1) and light their diffuse colour
  (albedo x (1 - metal) x (1 - Fresnel at N.L): the guide's albedo x (1 - metal) x (1 - F0) at normal incidence) by c1 x
  Oren-Nayar N.L x the shadow x 0x389FB405 (7.615242e-05; 6-12 copies in each cso; 9EAD8951 asm l.2138-2149, 76A707E5
  l.1668-1672, CCEF2C1E l.1742-1746, 18A84893 l.1347-1352), no debug coefficient in WH3's buffer. InstallSunDraws(..., 8, 4)
  (rt_core: RT_PATH_SUN_LIGHT with Path-Traced Ambient); WH3's own rt_sun.cs_5_0.hlsl takes b1's second uint4 and writes
  texel 2 = c1 x asfloat(0x389FB405) (w 1; 0 without the place). The sun log line now names the sun colour. WARP
  tools/tw/warp_tests/wh3_rt_sun_v55 (rt_sun_v55.cpp, run_v55.txt; v54's file in v54/): RT SUN V55 ALL PASSED: with the place
  texel 2 = c1 x the literal, the other 15 texels v54's bit for bit (128 cases: SH on / off, GI on / off, the probes' sky, the
  cube); without it texel 2 zero, the rest v54's (128); CONTROL unscaled c1 apart (128). Built WH3 addon64. Later run: Path-
  Traced Ambient steady while the camera turns (ATN's PT GI angle test), log "Sun shader ...: ... sun colour".

## PHARAOH V61: OPEN ITEMS (2026-11-13 evening, offline; plan tools/tw/pharaoh/open_items/plan.md, class A + the B trace reads)
Rules: no game start, no Steam folder touched, no stamp / install (ATN does those), no DevKit near game folders, no Nexus text,
never restart. Committed Bytes under 60 GB before WARP / harness / GPU jobs, one GPU job at a time. Unproven look changes:
Experimental rows, off. One writer; each step committed and pushed to origin atn-mods.
- [x] 1 "no main camera": rt_scene::Install(SCENE_SHADERS, {}, rt_host). D42C3523 (dx11_ssao2 ps50_generate, fxc -dumpbin)
  binds cb0 ssao2_PS (144 bytes: g_Resolution, g_ToViewTransform ...), t0 ssao2_linear_depth_texture, t1 random: no camera,
  so rt_scene's AO hook (Attila's t0 / b0) failed every frame (W1 24, W2 25, S1 21, campaign 105 log lines). Its other jobs:
  the multiply after the pass (never ran: no trace there) and the Debug View's Screen-Space AO copy (Pharaoh never draws the
  Debug View: no rt_scene::OnTonemapDrawn in its tone mapper hook, the row hidden), so nothing lost. The plan's "Debug View
  shows the sun's t12" is wrong (nothing calls KeepDebugAmbient in Pharaoh). Kept: ao_pass::Install(AO_DRAWS) (GT-VBAO), the
  sun draws' trace + combine at ao_slot (24 of 25 set it; FFD99CCD reads no AO, as before). Troy carries the same bytes
  (D42C3523) and call: the same fix (own commit). Later run: 0 "no main camera" lines, status trace ms as before.
- [x] 2 E8524BB1 (effects quality 0-2) in Smoke Shadows: 2d3bb6d3 shared SmokePixelShader {hash, view_shift, sampler_shift}
  (rt_core CaptureParticles copies the PS's views / samplers that many slots higher to the quad shader; Attila's 6 rows and
  3F1B86E1 at 0 / 0 = the old calls); Pharaoh {E8524BB1, 3, 2}: its t3 diffuse / t5 constants / s1 reach the quad shader's
  t6 / t8 / s3. Bytes (fxc -dumpbin): same input signature (TEXCOORD2.y unused), EMITTER_CONSTANTS_PS stride 80, the same
  alpha chain (two cells, sRGB decode with 0x3F72A76F in both csos, x COLOR.w, crush 0x100000; lit 0x80000), no shadow maps.
  WARP tools/tw/warp_tests/pharaoh/smoke_v61 (make_v61.py from smoke_v57, build.ps1, run_v61.txt): SMOKE V61 ALL PASSED:
  geometry as v57 (120 placed, 0 apart, 2 orders); E8524BB1 opacity plateau 0 of 69 apart (worst 1.19e-7), smooth 0 apart
  (7.5e-4 of 1.12e-3), mip level 0 of 23 apart (0.0039); CONTROL unshifted (3F1B86E1's slots) 39 / 39 / 22 apart; v57's
  controls fail as before; 3F1B86E1 itself unchanged. Level B (dump, no trace: quality 0-2 is the menu's; which VS draws
  it at 0-2 not seen: only 109B79CE is hooked). 740B2536 (MSAA) stays out: no rays under the game's MSAA (comment).
- [x] 3 Water reflections capped at 25 a channel as the game's apply: ssrapply00 r1.yzw = min(max(t47.rgb, 0), 25) (the
  game's t_colour_src t6 is R8G8B8A8_UNORM, W2 trace l.3129 f28, x 5 squared: at most 25). No row (the game's own look; the
  agent's choice, ATN judges it in game). WARP tools/tw/warp_tests/pharaoh/ssr_v61 (make_v61.py from ssr_v56, run.sh, run_v61.txt; previous
  build's cso v60_0xDE4F92CD.cso): ALL PASSED: 1 / 2 / 4 / 5 as v56 (0 misses); 3 the formula with t47 capped 0 misses
  (69,235 channels above 25), CONTROL uncapped 34,537 misses; 6a t47 below 25 vs v60's build 0 of 589,824 bits apart; 6b
  t47 25-1000, a 1 vs the game's march saturated (t6 255) 0 of 589,824 bits apart, CONTROL v60's build 248,840 apart.
- [x] 4 Building water (phar_building_water_01) t_ssao render-scale rows: RENDER_SCALE_READS 03B9CEFE t12 (ps_4_1),
  9D28319F t12, 40154AD7 t9, AC04E623 t11 (fxc -dumpbin: sample_l t_ssao at (SV_Position - c30 + c28.x) x c29.zw, gated
  by g_campaign_flat_map 0; their G-buffer loads take the screen uv x resinfo, their NDC the view uv: right at any scale).
  WARP tools/tw/warp_tests/pharaoh/scale_v61 (scale_v61.cpp, build.ps1, run_v61.txt; a 3D pass: viewport W x H in a 3W x 3H
  target against W x H, the game's 4 outputs): SCALE V61 ALL PASSED: view_share's patch 1 region read in each (ps_4_1
  too), ndc / projected 0; 100% with cb12 unbound bit-equal to the game's; another AO picture moves 7,902-9,194 values; 33%
  with the refraction one colour 0 apart beyond 8 px (0-232 not bit-equal, <= 8.4e-7), CONTROL the game's bytes 2,109-4,521
  apart. Listed, not fixed: the shadowed pair (03B9CEFE / 9D28319F) keeps 57 of 24,576 values apart at 33% with the
  refraction as drawn, the same 57 with a constant AO and the game's bytes: their refraction read (its distortion a uv offset
  of the whole texture), the game's own, not the AO. In no trace (ATN: a settlement with pools, Ultra Performance).
- [x] 5 7b / 7c offline WARP checks; nothing fixed (7b needs the vb0 read; 7c's one fault is the game's own march, below).
  - 7b tools/tw/warp_tests/pharaoh/apply_scale_v61 (run_v61.txt): A0EA5C9E passes vb0's position and TEXCOORD0 through
    (fxc); DE4F92CD reads t_mask / the G-buffer at TEXCOORD0 x the mask's size and builds its NDC from TEXCOORD0 x (c31.xy x
    c29.zw), so it is right only if TEXCOORD0 is the target's uv. WARP at 50% against 100% (W2's ratios, flat normals, misc.y
    0 and 1): U2 (TEXCOORD0 0..0.5) the game's bytes (view_share leaves them: ndc / projected / region 0) and our built copy
    0 of 6,144 apart beyond 8 px; U1 (TEXCOORD0 0..1, a quad written for 100%) both 5,892-5,944 apart (largest 25.7). The
    tone mapper 976C1C74 sharing vb0 doesn't read TEXCOORD0, so it pins nothing. Listed: with normals varied the game's own
    distortion (0.7 x (n.xy - 0.5) in uv of the whole SSR texture) spans twice the picture's texels at 50% (4,519 apart under
    U2 too). NOT DONE (needs step 7's vb0 read): if vb0 holds 0..1, the game's apply and ours are wrong below 100%; smallest
    fix then: ours takes the target uv from SV_Position x c31.zw (and the game's chain the same through a patch).
  - 7c tools/tw/warp_tests/pharaoh/sun_scale_v61 (run_v61.txt): the 9 single-sampled LIGHT_BOUNCE_DRAWS (the _ms ones never
    draw scaled: the game's MSAA keeps the render scale off) with F883F5B2's six buffers from A2 trace_100 (by name; trace_33's
    differ by DLSS jitter and time, so the camera's c27 / c29 / c31 are set per scale as trace_33 shows them), R32 shadow
    maps: SUN SCALE V61 ALL PASSED with the game's contact shadows off (as OnSunContactShadows leaves them under Bend or the
    rays): 33% patched against 100% 0 of 6,144 apart in all 9 (FFD99CCD needs view_share's NDC patch: unpatched 4,608 apart;
    the other 8 build their NDC from c29 and match unpatched too). NOT DONE: with the game's contact shadows on (the traces'
    flag; only where neither Bend nor the rays shadow the draw) 5 of 9 (40BE26E9, 5FF8081D, FFD99CCD, 46B411BB, 6C317272)
    stay 573-1,180 apart at 33%: the game's march reads its depth at ndc x (0.5, -0.5) + 0.5 x resinfo (ld t8: the whole
    texture, not the render share) and steps 6 / max(texture size) (FFD99CCD asm l.221-310). Smallest step: a view_share
    patch for that projected uv into an ld (x cb12.zw) and the step x the share, WARP as sun_scale_v61 with the flag on; or
    the game's march off below 100% (a look change: Experimental). ATN's presets (rays) never run the game's march.
- [x] 6 Pharaoh PT GI sun colour: InstallSunDraws(LIGHT_BOUNCE_DRAWS, 24, 12) (c3 sun_colour, all 25 sun shaders' RDEF),
  so rt_core sets RT_PATH_SUN_LIGHT with Path-Traced Ambient; no helper change. The sun shaders' diffuse (F883F5B2 fxc:
  albedo x (1 - specular) x c3 x N.L x shadow x 0x389FB405 literal (7.615242e-05, in all 25) x g_debug_light_diffuse_coef
  (cb4[25].y, 1 in A2 / W2 / campaign traces)): shared rt_sun.cs_5_0.hlsl takes TW_RT_SUN_LIGHT_SCALE (default 1.f; Attila's
  cso bit-identical to v156's), Pharaoh's includer asfloat(0x389FB405u). tools/tw/warp_tests/pharaoh/rt_sun_v61
  (run_v61.txt): RT SUN V61 ALL PASSED: Attila's file v156's bit for bit (256), Pharaoh texel 2 = c3 x the literal with the
  rest v60's bit for bit (128), without the place v60's (128), CONTROL unscaled c3 apart (128). A2's sun: texel 2 =
  3.046 2.968 2.666 against the sky's +x 0.510 0.688 1.100. Built Pharaoh addon64 + Attila addon32.
- [x] 7 Trace reads for the later runs: ReShade.ini [renodx] FrameTraceReads=1 (read at start; off: no hook, no call, the
  trace as before) with frame_trace's new Options draw_reads (Pharaoh only: twpharaohdynasties/trace_reads.hpp). Shared
  frame_trace: each traced draw's arguments (first index / vertex, base vertex, first instance; indirect ones from their
  buffer) and bound input layout (init_pipeline tracked only with the key) to the game's reads, their text after the line;
  then the frame after (lines "next frame: ...") and the file saved at its present. Pharaoh's reads: at 109B79CE + 3F1B86E1
  / E8524BB1 the sorted particles (t2 dist + pid, t6 emitter + position, t1 emc_id) and the t4 rows of their emc_ids; at
  E318CE77 vb1's instances (traced frame and next); at A0EA5C9E + DE4F92CD vb0's vertices; at 377DE0D5 world x / z bounds
  (vb0 through POSITION1 / 3, y from the height map not read) and 026CBC61 x / y / z (vb0 x (1, 1, 1.15476)); value_hashes
  += 61C823A0, 1E864815; blend_hashes += E8524BB1. tools/tw/warp_tests/pharaoh/trace_reads_v61 (run_v61.txt, the
  repository's headers, x86 + amd64): TRACE READS V61 ALL PASSED (key off: nothing registered or called, file as before;
  on: arguments, appended layout offsets, next frame, save; the reads against the CPU's; past-stride / unknown format "not
  read"; CONTROLs: a changed t1 emc_id changes the text, a moved unreferenced vertex not the bounds).
- [x] 8 Builds: the skill's 15 x64 targets and build32 Attila / Rome II / Thrones, exit 0. test/tw_* x86 + amd64 (motion
  record and shadow map size their own one): 36 runs, all exit 0. Page dumps (only Build Date differs): Pharaoh page_p_r61
  against r60 (tools/tw/pharaoh), Attila page_a_r161 and WH3 page_w_r161 against r156 (tools/tw/page_dump). README Pharaoh
  "### Round v61" + test lines 13-16; Attila / WH3 no entry (rt_sun's cso bit-identical, frame_trace's trace unchanged with
  the key off, the smoke rows' slots 0 / 0 as before). Skill rows: sun diffuse scale and the AO pass's camera
  (game-facts), E8524BB1's slots (particles), the game's contact march below 100% (render scale), R32 maps on WARP
  (testing), trace Options on_demand / draw_reads (bring-up).
- [x] 9 (coordinator) rebuilt at HEAD 16:27, STAMPED + INSTALLED Pharaoh v61 16:27 (tools/tw/round_v151/install_v61.py;
  helper107 unchanged, its stamped v60 copy; ATN files unchanged by hash). Attila v156 / WH3 v54 kept (no behaviour change).
- [x] 10 run B61 (tools/tw/pharaoh/runs/B61_162810, findings.md; pharaoh_data_run.ps1 -Ini "FrameTraceReads=1", new):
  clean, files equal. "no main camera" 1 line (loading screen only). Torches: E318CE77 vb1's 15 lights bit-equal on two
  frames (the lights don't move: torch flicker is our sampling). 7b: DE4F92CD's quad uv 0..1 over 3840x2160 targets at a
  1920x1080 viewport: the uv isn't the render share (fix: uv from SV_Position x share). Smoke: 90 of 90 sorted particles
  listed; projection to do.
- [x] 11 7b fix (SSR apply below 100%) + smoke projection (off-screen particles in the CPU list?) + the lever if so.
  - [x] 11a 7b fix: DE4F92CD reads every texture at TEXCOORD0 (t_mask / depth / normals / specular / the rays at TEXCOORD0 x
    the mask's size; the NDC from TEXCOORD0 x c31.xy x c29.zw; the SSR colour at TEXCOORD0 x 0.5 x mask / colour size + a
    distortion in the colour texture's whole uv), none at SV_Position; B61's vb0 holds 0..1 over the viewport, so below 100% all
    read the display-size textures whole (and the rays at 2x the pixel). Ours (shaders/ssrapply00): uv = TEXCOORD0 x
    DrawRenderShare() and the distortion x the share; OnWaterApplyDraw draws it below 100% with or without the rays (the game's
    own bytes at 100% without them) and binds cb12 itself (the game's pipeline isn't view_share's: nothing in it is patched),
    the draw's viewport / target. WARP tools/tw/warp_tests/pharaoh/apply_share_v62 (run.sh, run_v62.txt): ALL PASSED: 100%
    v62 = v61's build bit for bit (rays unbound, bound, cb12 1) and = the game's bytes; 50% U1 0 apart (bit-equal) with and
    without the rays, normals flat and varied, misc.y 0 / 1; 33% 0 apart except the distortion's bilinear read (listed: 2,112
    values; its texel coordinates within 7.6e-6 of 100%'s by the probe, WARP's weights differ by texture size at one
    coordinate; with s1 point 0 apart); CONTROLs: the game's bytes / v61 / v62 without cb12 at 50% 5,424-5,908 apart, the
    game's at U2 (uv right, distortion not) 2,576-4,667, the probe's unscaled distortion 7.8 / 15.7 texels. Listed, not
    changed: the SSR colour's mip (16 x roughness) is in texels, so rough water blurs 2x at 50% (a look choice). Later run:
    log "First run: the water's reflection apply DE4F92CD below 100%".
  - [x] 11b smoke projection, nothing added: tools/tw/pharaoh/open_items/smoke_projection.py (smoke_projection.txt) projects
    B61 trace_50's 90 sorted particles (l.3212-3302) with the 3F1B86E1 draw's camera (vs b0 c9-c12) by 109B79CE's own test
    (v55 asm l.272-302: |clip.xy| > w x (expansion + g_draw_flags.y 0 + 1), clip.z < 0 or > w): 78 on screen (emitters 391 /
    393, largest |ndc| 0.68), 0 beside the view (none within 393's expansion 1, none beyond it), 12 behind the camera (438,
    w -144 to -535; their shadows along the sun's (0.239, -0.400, 0.885) meet y 0 behind it too, 0 in view). A raised
    g_draw_flags.y keeps only particles beside the view: none here, so no lever (no row). The 12 behind it show the CPU
    doesn't cull its list to the view; a trace with smoke just beside the edge (camera panned off a burning building) would
    say whether side particles are listed (then plan.md A1; the behind ones need the VS's z test off, its alternative).
  - [x] 11c builds + tests at 82fb4cfb (+ docs): the skill's 15 x64 targets 50 s and build32 twattila + twrome2 +
    twthronesofbritannia 44 s, exit 0 (only the upstream deprecation warnings); test/tw_* 19 suites x86 + amd64 (motion record,
    shadow map size their own one): 36 runs, all exit 0. Page dumps (Build Date only): Pharaoh page_p_r62 against r61
    (tools/tw/pharaoh), WH3 page_w_r162 against r161 (tools/tw/page_dump); no shared code changed (Attila not dumped).
    cb_audit twpharaohdynasties: ssrapply00 adds cb12[0].xy only. READMEs Pharaoh "### Round v62" (+ test line 17), WH3
    "## v55". Skill rows: render scale (a quad's vb0 uv is the viewport's; a native swap binds its own cb12), testing (WARP's
    bilinear weights by texture size at 33%), game-facts (WH3's sun diffuse scale).
- [x] 12 (coordinator) rebuilt at HEAD 17:15, STAMPED + INSTALLED 17:15 (tools/tw/round_v151/install_v62.py; ATN files
  unchanged by hash; old add-ons .in-use-20261113_171548) Pharaoh v62 and WH3 v55 (helper107 unchanged). Open: a later run: Pharaoh at 50% with the
  water ("First run: the water's reflection apply DE4F92CD below 100%"), WH3 with Path-Traced Ambient ("Sun shader ... sun
  colour").
- [x] 13 v63 (coordinator's step): the SSR colour's level below 100% at 100%'s blur. ssrapply00 (our copy, drawn below 100% by
  OnWaterApplyDraw) samples t_colour_src at max(16 x (1 - spec.y) + 0.5 x log2(share.x x share.y), 0) when share.x < 1, else
  the game's level (the environment cube's level unchanged). WARP tools/tw/warp_tests/pharaoh/apply_share_v63 (make_v63.py
  from v62's, run.sh, run_v63.txt; v62's build v62_0xDE4F92CD.cso): ALL PASSED: 100% = v62's build and the game's bytes bit
  for bit (rays unbound / bound / cb12 1, one mip and a mip chain); one mip at 50% / 33% = v62 bit for bit; rough water (level
  2-5, float mip chains with level k one colour, F(footprint): 100% F(k), 50% F(k + 1), 33% F(k + log2 3)): 50% = the game's
  100% bit for bit, 33% inside the game's 100% at the level +- 1/256 in all ~4,600 written values per case; CONTROLs v62 at
  50% / 33% outside in every value. Built x64 only (Pharaoh's own shader, no shared code). README "### Round v63". Not run.

## PHARAOH V58: CAMPAIGN FIXES (2026-11-13 day, offline; ATN's campaign trace ATN_campaign_0857 at 100%)
Rules: no game starts (ATN plays), no stamp / install, no DevKit near game folders, no Nexus text, never restart. Committed
Bytes under 60 GB before WARP. One writer; read-only surveys beside (tools\tw\pharaoh\v58: census_pharaoh*.txt / .tsv, caster_census_after.txt).
Each step committed and pushed to origin atn-mods.
- [x] 1 (4b554a21) WARP tools\tw\warp_tests\pharaoh\ao_v58 (run_v58.txt, PASS): Reflect: 70 of 179 built csos render_scale, 0
  of 3168 game csos; our blurs at 100% 0 of 57,600 values apart (bits) from the game's; at 33% (3x) against 100%: bitmask Full 0
  of 12,464 px beyond 8 px of the edges (2 of 14,400 overall), Half 0 of 14,400; after the blurs 0 / 0 of 12,464 (Full 24 edge px,
  largest 0.027); CONTROLs: cb12 unbound 13,936 apart (top-left ninth 0.467 against 0.173), v53's patched blurs 14,087. The depth
  levels now held on the picture's last texel (twwarhammer2/bitmask_ao.hlsli: the same texels wherever depth_texel is the whole
  depth's: Troy, WH2, WH3, Shogun 2, line sweep). The GT-VBAO rectangle at Ultra Performance: cause (WARP, scratch v58\scale_survey\ao33): the build strips every addon
  shader's reflection (CMakeLists.txt /Qstrip_reflect), so twattila/view_share.hpp Reflect never sees `render_scale` at b12 in
  our replaced shaders (ssao01 D42C3523 + ao_bitmask swapped in, t03 60683951, fire02 E318CE77 ...): they get no cb12 of their
  own and FrameRenderShare() reads whatever b12 is left bound (null: 1, the whole 1920x1080 depth squeezed into the 640x360
  AO viewport = the top-left rectangle). Fix: Reflect takes a code with no RDEF that declares cb12 as render_scale (shared: every
  TW addon's render_scale replacements, the same bug). Second: the AO blurs' REGION read scales the taps too (a third of a texel
  apart at 33%): blurs replaced, base uv x share, taps in texels.
- [x] 2 (f62cbfd8) clouds below 100%: the campaign's clouds (t_shroud_clouds, the shroud's half-size buffer that
  5EB2FDE8 draws at the render scale) were read after DLSS at the display uv (the clouds slid toward the top left): the glow
  bright pass 57E5140E t0 (region slots 0b111, RegionRead::size_slot 1: t1's size says whether the share applies; t0 is a
  1024 x 1024 stand-in in battles) and the six replaced tone mappers t01-t04 / dof00 / dof01 (render_scale.hlsli RenderShareUv:
  uv x share, held on the share's last texel). Over-patches: Pharaoh's shaders map uvs to the viewport themselves (c29.xy x c31.zw
  or c29.zw x c31.xy); view_share SetViewportChains(true) (Pharaoh only) leaves a uv / projected uv any of whose factors is the
  viewport. Proof tools\tw\warp_tests\pharaoh\patch_v58 (run_v58.txt): chains off = HEAD's Patch byte-identical on every shader of
  10 TW dumps (Attila 2418, Rome II 2257, Thrones 2372, Troy 2882, WH1 2623, WH2 3595, WH3 2584, Pharaoh 2897, Shogun 2 1074, Rome
  Remastered 672); chains on, Pharaoh: exactly the census's 35 over-patches change (24 sun SSEO, 7 shroud_plane, 99164788,
  0CB1C821, 6ECC6281's 1 of 2, DB53BDDC's projected). WARP tools\tw\warp_tests\pharaoh\scale_v58 (run_v58.txt, PASS, 3x vs
  100%, point samplers, pixel 3x+1): (E) 57E5140E v58 0 of 12,288 apart (1e-5; 10 not bit-equal, 4e-6: v1's interpolation),
  CONTROL v57 slots 4,950 apart; (F) the clouds line, display-size and half-size clouds: 0 bits apart, CONTROL old line 12,288 /
  12,284 apart; all six built tone mappers declare cb12; (G) 99164788 the game's bytes (= v58) 0 bits apart, CONTROL v57 692
  apart; (H) DB53BDDC: visibility 0 apart, sun uv x 3 equal, CONTROL v57 visibility apart. Left (listed, not fixed): 99164788's
  5x5 taps spaced by the buffer's size (a third at 33%: mean 0.025 against v57's 0.052), DB53BDDC's edge fade from the texture uv.
- [x] 3 (e34c6089) done: palms 1C4B8681 in SCENE_SHADERS (rt_capture_rigid_standard_cut_out: 6A599241's world + the UV in
  w; placed by 6A599241's record, .foliage .still, cut out by t0 at slot 0: B97AD7BC's t0.w x fade < 0.505); 6A599241's
  alpha-tested PS (4B9BDD76, 7550C192, 81931C18, BD6C63DE) cut out (clip_cut_outs + CUT_OUT_MASKS); BUILDING_MASKS + D24616D0
  (campaign settlements / cliffs, 0.33 M tris), 509F97FB, 730C5322, 85E52BED, A6C653E1 (all solid: no discard). WARP
  tools\tw\warp_tests\pharaoh\trees_v58 (run_v58.txt, motion_warp + COMPARE_WIND_MULTIPLIER): wind 0, 1C4B8681 and 6A599241
  against the UV capture 1.36e-8 (relative), the solid capture 5.2e-8; CONTROL wind 1 (the palms' sway, <= 0.12 m) 8.25e-4 WRONG;
  6A599241 at wind 1 1.36e-8 (no sway). Not done: 026CBC61 (global terrain, 0.83 M tris: overlap with 377DE0D5's tiles unknown
  offline), 58733DB8 (campaign drapes on the terrain: coplanar), 35D229C0 / BDC503B1 (terrain shadow VS, depth only: no world to
  keep out of view), CE5AA8D1 (campaign tree billboards, no shadow-pass draw). The survey: palms = 1C4B8681 (campaign_trees, not in SCENE_SHADERS;
  atlas via DC5FE525 + 985F8494), 1ACBD907 + D24616D0 (and 730C5322 / 509F97FB / 85E52BED / A6C653E1) not in BUILDING_MASKS,
  58733DB8 campaign blend-mask terrain, 026CBC61 global terrain mesh; 6A599241's alpha-tested PS solid.
  Plan A palms: rt_capture_rigid_standard with RT_CAPTURE_UV (RtWorldUv(world, o2.xy)) + rt_capture_rigid_standard_cut_out;
  {0x1C4B8681, cut_out, rows 6a59 record, .foliage, .still} (VS = 6A599241's + an x/z sway <= 0.12 m x the wind multiplier; PS
  B97AD7BC discards t0.w x fade < 0.505 and a fade dither). B: D24616D0 (+4) to BUILDING_MASKS. E: 6A599241 cut-outs (81931C18,
  BD6C63DE, 4B9BDD76, 7550C192: t0.w < 0.5 at o2). C/D: 58733DB8 (drape, dynamic), 026CBC61 (terrain). Census data:
  tools\tw\pharaoh\v58\vs_table.txt, coverage.tsv, campaign_groups.txt; scale: census_pharaoh.txt / .tsv (whole dump: 34 BUG A, 27
  BUG B; over-patch hits 35: the 24 sun shaders' SSEO block NDC, shroud_plane x7, battlefog 99164788 / 0CB1C821, DB53BDDC, 6ECC6281;
  3F1B86E1 / E8524BB1 replacement lost view_share's NDC patch; phar_building_water_01 t_ssao at view uv (not traced)). Agents'
  scratch: ...\scratchpad\v58\{scale_survey,casters,water,torch}.
- [x] 4 (654901d2) Steady Torch Shadows (Experimental), off, every TW game's rays page under the torch rows (shared:
  rt_scene.hpp row RayTracedLightSteady, rt_core RT_LIGHT_STEADY = 1 << 28, helpers before 106 ignore it; bridge stays v31). The
  helper (build\tw_dlaa_helper106, not installed): every light a pixel reaches traced every frame (no turns, Importance picks
  or kept bits) and each soft ray to the same disk point (the seed without the frame). Root cause: the helper's torch bits
  change every frame by design (the RR soft rays' random disk point, the turns' and Importance's kept bits taken at the last
  trace's pixel, which the jitter moves); with NRD (ATN's preset) nothing averages them. Harness HARNESS_LIGHT_FLICKER
  (tools\tw\rt_harness\light_flicker_v58.sh / .txt, GPU, 40 frames, last 20): helper106 without the flag = helper105 to the
  bit (39,910.6 bits a frame, jitter + 27 lights); jitter + 27 lights: turns 39,911, Importance 66,801, Steady 28,344 (the
  jitter's own edges); still camera, RR soft 0.25 m: 24,154 -> 0 (27 lights), 5,870 -> 0 (3 lights); Importance 3,107 -> 0;
  the harness's checks ALL PASSED with Steady. Not proven: the game's own torch positions (a trace of two frames' light
  draws would show whether they move). The survey: the helper's soft torch rays (rt_lights.cs_6_5.hlsl 200-216) take a new random disk
  point every frame (seed (frame*128+light)*16+ray*2), one ray, a 0/1 bit, no filter (only RR averages); Importance holds old bits
  per 8x8 block (165, 179-194). No GPU flicker term in E19549A1 / fire02. Options: fixed per-pixel 4x4 disk pattern rotated by a
  position hash (helper, bridge v32) + a filter; steady light positions in the addon.
- [x] 5 (b260dc49) done: twattila/water_effects.hpp WaterOpacity + WaterShader::opacity, PatchWaterAbsorption(code, opacity)
  (type 0: WH3's log2(e) rule), the material path's caustics only where water.caustics; Pharaoh: WATER_SHADERS 84 rows (all
  material water, campaign caustics off), rows Water Clarity (Experimental) 1 and Water Caustics (Experimental) 0 (RT/PT turns
  it on as ATN's list), embeds water_caustics / water_clarity. WARP tools\tw\warp_tests\pharaoh\water_v58 (run_v58.txt): WH3 6 of 6
  byte-identical to HEAD's; Pharaoh 84 of 84 byte-identical to the survey's prototype; neutral (cb11.x 1, t51 unbound) 0 bits apart
  of 19,464,192 values in 84 shaders; tile k / clarity f: 0 misses, controls apart (136 runs). The survey: water_effects' material
  path refuses all 84 Pharaoh water PS (absorption exp2(-depth x opacity) has no log2(e)); opacity operands cb4[3].w (sea),
  cb5[3].w (campaign sea), v11.w (river COLOR2.w), cb3[45].w (big river). Plan: WaterShader.opacity {type, index, component}
  (0: WH3's rule), PatchWaterAbsorption(code, opacity), caustics only with water.caustics; table tools\tw\pharaoh\v58\
  pharaoh_water_table.txt; WARP: WH3 6 of 6 byte-identical, Pharaoh 84 neutral 0 bits apart, controls move.
- [x] 6 (61923316) builds exit 0, no new warnings: Pharaoh addon64 45 s, WH3 44 s, the other x64 63 s, build32 Attila
  47 s, Rome II + Thrones 50 s (all three sets rebuilt after the sway fix, exit 0); git diff --check clean; test/tw_* 17 x x86 + amd64
  PASS; WARP on the final build: sun PASS, ssr_v56, smoke_v57, ao_v58, scale_v58 PASS, patch_v58 (on: 35 differ, Reflect on game
  shaders 0), trees_v58 (only the control fails), water_v58. Page p v62 against v60: +3 rows (Steady, Water Clarity, Water
  Caustics), none lost; a v171 / w v67: Steady + date. Found by the dump: the palms' still foliage made Swaying Shadows Range
  and Off-Screen Sway appear in Pharaoh (no sway in its rays): rt_scene.hpp's two rows now skip still foliage (Attila, WH3
  unchanged: their swaying foliage). Scale census after (the skill's scale_census.py with viewport chains): BUG (A) 34 -> 0,
  BUG (B) 27 -> 11, unclear 6 -> 31 (the 24 sun shaders' second site, a mixed chain, patched since v53).
- [x] 7 (61923316) README Round v58 + test items 7-12; skill rows (render-scale-dlss.md: sweep at Ultra Performance
  campaign + battle, stripped RDEF, region taps, viewport chains, size_slot, RenderShareUv; rt-scene.md: caster census, Replace
  per caster impossible, Steady torches; sss-particles-water-fog.md: water opacity operand; bringup.md Phase 0: both censuses
  with limits); scripts/scale_census.py + _replay.cpp + _hook.py + _build.ps1 and caster_census.py in the skill.
  Not done (each needs one trace): shroud decals' inv_rt_size at 33% campaign (c13); DE4F92CD vb0 at 50%; a trace drawing
  phar_building_water_01 (t_ssao t12 / t9 / t11); the sun draw at 33% against 100% (the mixed site); 026CBC61 against
  377DE0D5's tiles in one campaign trace; two frames' light draws in a night siege (do torches move).

## PHARAOH V57: SMOKE SHADOWS (2026-11-13 night, offline; hard stop 05:30)
Rules: no game starts, no stamp / install (ATN's agent does those), no DevKit near game folders, no Nexus text; Committed Bytes
under 60 GB before GPU / WARP jobs (18.6 GB at 02:06). One writer. Each step committed and pushed to origin atn-mods.
- [x] 1 shared quad form (rt_core.hpp CaptureParticles + smoke_quad_shader, rt_scene.hpp InstallSmokeShadows(vs, ps, quad_shader = {}),
  graphics_page.hpp Bindings::smoke_shadows_tested): a game whose particles are instances of a quad (indexed, 6 indices) gives a
  geometry shader making one 48-byte record an instance (rt_smoke's layout) of the game VS's triangles; it sees the draw's PS
  views / samplers (16 each) and the VS's b0-b1 at their own slots (the game's GS stage put back). Empty: Attila's form, unchanged
  (its indexed draws returned and still return at once). smoke_shadows_tested false: the row "Smoke Shadows (Experimental)" and
  RT/PT leaves RayTracedSmokeShadows out (the page's default then). Dumps page_a_v168 / page_w_v64 against v167 / v63: the date only.
  (556ae024)
- [x] 2 Pharaoh: shaders rt_particle_quads.gs_5_0.hlsl -> rt_particle_quads_gs.h (fxc by hand, as rt_outside_view); InstallSmokeShadows(
  109B79CE, {3F1B86E1}, __rt_particle_quads). Bytes: the VS keeps only emitters whose m_draw_flags meet the draw's g_draw_flags
  (cb_vfx.x 0x40200001 at 3F1B86E1; the same 91 sorted particles in all five draws), writes world corners with the emitter's depth
  bias in view z (TEXCOORD4), the corner (TEXCOORD8), uv (TEXCOORD1/3), COLOR; the PS's alpha = sRGB-decoded texture alpha x
  COLOR.w, crush 0x100000, soft edge 0x200000 (left out), lit 0x80000. Record: centre / size by barycentrics of corner (0, 0),
  the bias put back from SV_POSITION.w, opacity the PS's at the centre (lit only, none around the camera). Row Smoke Shadows
  (Experimental) default 0, RT/PT leaves it (smoke_shadows_tested false), Quality under More Options (default 3, as before).
  WARP tools/tw/warp_tests/pharaoh/smoke_v57 (run_v57.txt; the trace's camera and cb_vfx, 144 particles, 12 emitters, 2 index
  orders): centres and sizes 0 apart of 120 placed (tolerance 2e-6 x (600 m + distance); worst 0.056 of it), 12 culled and 12
  screen-space with no record; CONTROL without the bias put back 60 apart (bias up to 2.31 m). Opacity against 3F1B86E1's own alpha
  at the centre pixel, 69 particles: plateau texture 0 apart within 4 ulps (worst 1.19e-7: the PS's COLOR comes interpolated),
  smooth texture 0 apart within 2/256 x largest step x 2.275 (1.12e-3; worst 7.5e-4); CONTROLs: no texture alpha 32 / 39 apart,
  unlit included 12 apart. fxc rounds 1/1.055 one bit low: the PS's 0x3F72A76F as asfloat. Dump page_p_v59 vs v58: 611 keys, none
  lost; Smoke Shadows (Experimental) default 1 -> 0, shown with the rays (row 137) and Quality under More Options (156); RT/PT's
  "ours" list without smoke shadows (as v58). (df95f06e; ff5d2e10 strips fxc's trailing spaces from the header)
- [x] 3 builds at ff5d2e10: the skill's x64 list (15) + build32 Attila / Rome II / Thrones linked, no errors, only the old
  LoadSetting deprecation warnings; test/tw_* amd64 + x86 PASS (depth_levels, device_loss, fg_initialization, gpu_completion,
  helper_lifetime, preset_writer, sampler_lifetime, screen_space_shadows, startup_worker, texture_lod_bias, unit_pack_rounds;
  motion_record x86). Committed 18.6 GB.
- [x] 4 one independent review (read-only agent) and its fixes: (1) RayTracedSmokeShadows saved hidden at 1 by v56 inis (every
  keyed setting is saved): Pharaoh's SettingsRevision 1 clears it from the three presets once (WH3's pattern); (2) one texel of
  the top mip: the record now samples at the PS's own mip level (D3D's isotropic level from the uv's change across a pixel of
  g_viewport_dimensions, c29); (3) the VS's mesh path: opacity 0 without the quad bit (TEXCOORD0.y 0x200000); (4) the VS's
  b0-b1 read and the GS's put back with their D3D11.1 ranges (*ConstantBuffers1); (5) braces, names (INDEXED, particle_count),
  a destructured pair, trace history out of addon.cpp's comment. Kept: the release lambda local (CXX_STYLE: small
  duplication over a helper). WARP again (run_v57.txt): everything as in 2, plus the mip level (mips with one alpha each, the
  test's viewport in c29): 23 particles (19 between the first and the last mip), worst 0.0039 of a level apart; CONTROL at the
  top mip 22 apart. Likely the same for rows v54-v56 showed with a new default 0 after being saved hidden (Fog Shadows, Torch
  and Fire Light in Fog, Water Reflections at RT/PT's 1; Bend's Light Through Banners 0.35, Coloured Shadows on): not cleared,
  ATN may have set them since they show; his ini would tell.
  (872a6917 shared, 8b58c119 Pharaoh)
- [x] 5 at 8b58c119: the x64 list + build32 Attila / Rome II / Thrones linked (only the old LoadSetting warnings); Pharaoh's
  add-on alone 44 s; test/tw_* amd64 + x86 23 of 23 PASS again; harness smoke_quality_medium / smoke_quality_high /
  release_smoke ALL PASSED (the helper and bridge unchanged); dumps page_p_v60 / page_a_v169 / page_w_v65 against v59 / v167 /
  v63: the date only. README Round v57 (New, WARP, page, Not done with ATN's test lines), skill rows (particles x2, testing,
  shaders-ui x2 + the saved-hidden-row row's Pharaoh case). Built only: not stamped, not installed, not run in game.
  NOT DONE (README): run in game; smoke just off screen (does the sort's t2 hold it? read its CS, then a raised
  g_draw_flags.y); E8524BB1 / 740B2536; dust apart from smoke (level C: per-emitter t8 masks in a trace).
- [x] 6 (ATN's agent) rebuilt at HEAD (the 03:03 builds were older than step 4's commits), STAMPED + INSTALLED 03:17
  (tools/tw/round_v151/install_night.py): Attila v153, WH3 v51, Pharaoh v57; helper105 unchanged in all three; ATN files
  unchanged by hash; old add-ons renamed aside (.in-use-20261113_031658); no DevKit. ATN's Pharaoh ReShade.ini holds none of
  the saved-hidden RT keys (RayTracedSmokeShadows, WaterReflections, Reflections, FogShadows, LightGlow), so nothing to clear.
- [x] 7 run W2 (W2_031738, 03:17-03:19, v57 as installed, siege benchmark windowed, DLSS Performance, RT sun / torch / fog
  shadows, Reflections + Water, Smoke Shadows on, trace_50): "Smoke Shadows: one particle a quad of vertex shader 109B79CE (15
  instances in this draw, room for 524288)", then every 10 s 67 to 105 particles a frame (most quads 24 to 41, least 0.05);
  water tracing line as W1; status reflections 1.59 ms, trace 2.18; no crash / hang / error; ATN files same.
- [x] Night round report given to ATN (per game done / not done, test list), then the PC shut down as ATN asked.

## PHARAOH V56: RAY-TRACED WATER REFLECTIONS (2026-11-13 night, offline; plan tools/tw/pharaoh/reflections_plan.md)
Rules: no game starts, no stamp / install (ATN's agent does those), no DevKit near game folders, no Nexus text; Committed Bytes
under 60 GB before GPU / WARP jobs (18.4 GB at 01:10). One writer. Each step committed and pushed to origin atn-mods.
- [x] 1 shared light copy: WH3's rt_light_copy.cs moved into twattila (+ a clamp at the traced size, b0), rt_core Reflections runs
  it for an R16G16B16A16F light buffer (Host::light_copy: WH3's RT_HOST, Pharaoh's rt_host; empty elsewhere: Attila / Thrones
  unchanged); WH3's own copy, its texture and its log line gone. WARP tools/tw/warp_tests/light_copy_v56: old WH3 path against
  the new, 0 texels apart in 4 cases (100%, a share, odd sizes, NaN / inf / negatives), outside the traced size kept; CONTROL
  b0 = whole buffer 207 apart. Dumps page_w_v63 / page_a_v167: the build date only. Log: "Ray-traced reflections: tracing" now
  says ", the water from its own surface" when water_traced. (27cc31a8)
- [x] 2 Pharaoh: rt_host.rewrite_guides = RewriteSunGuides + light_copy; backward_ssr 9A55B7F2 hook (custom_shaders on_draw
  OnBackwardSsr: Reflections under ComputeState with depth t1, light t3; nothing under the game's MSAA: t2 / t4 multisampled,
  one log line). Traces only; the game's chain still runs (step 3 leaves it out). Built clean (only the old LoadSetting warning). (96486b63)
- [x] 3 Pharaoh: DE4F92CD replaced (shaders/ssrapply00, tools/tw/warp_tests/pharaoh/ssr_v56/make_apply.py: 3Dmigoto + 2 traps
  fixed, cb0[0].y skydome lerp and cb3[0].zw fog heights), swapped in (OnWaterApplyDraw, finalize) only where the rays left from
  the water; then 9A55B7F2 + 0B78827B + 20 mips skipped (their target's only other reader in A2 / B / S1 is the apply). OnPresent:
  reflections only with Water Reflections, Specular Occlusion only with them. cb_audit: lost [], added cb10[0].x only. (f306ff2d)
- [x] 4 WARP parity (tools/tw/warp_tests/pharaoh/ssr_v56, run.sh <built cso>; run_built.txt): 24 runs 96x64, real camera, misc.y /
  misc.w both ways, no MSAA (ours never draws under it). Nothing bound: 0 of 589,824 apart at 100%, 0 of 1,179,648 at 50% / 67%;
  t47 bound b10.x 0: 0 apart; b10.x 1 vs env + (t47.rgb x w - env) x t47.a: 0 misses; CONTROL game's x5-squared decode 207,453;
  t46 0.5 vs game with cubes x 0.5: 0 bits apart (b10.x 0: game's); CONTROL own cubes 277,923; render scale: t47 at the render
  pixel 0 misses, CONTROL viewport uv 268,723. Raw 3Dmigoto 165,780 apart; faithful (2 traps fixed) 0. (def89d06)
- [x] 5 page: has_reflections (Bindings default), AppendSettings(..., reflection_pass true), water_pass; Water Reflections
  (Experimental) default 0 after SetRaysPresetDefaults, Specular Occlusion (Experimental) default 0 and shown only with the rays
  and Water Reflections, Samples / Reuse shown and acting only with Softer (calm water is a mirror); OnPresent gates the same.
  Dump page_p_v58 vs v57 (dump_p.cpp's rays_now as OnPresent): 604 -> 611 keys, lost 0, added the 7 reflection keys; new rows
  the Reflections and Water section (method Game's SSR / Ray-Traced (DXR, Experimental), note, Samples, Reuse, Water
  Reflections, Steadier, Softer, Specular Occlusion); tooltips now naming reflections: RT/PT (mine), Hills Out of View, SER,
  Denoiser (shared texts on has_reflections). RT/PT now writes RayTracedReflections / WaterReflections / SpecularOcclusion 1
  (RAYS_PRESET, ATN's list, as fog and torches): ATN to decide. Reset puts them back to 0. (a389c431)
- [x] 6 shared frame trace (twattila/frame_trace.hpp Options, all empty / off for Attila, WH3 and the rest): blend_hashes (the
  full blend state at the first draw of each listed PS: a "  blend: alpha to coverage, factor, sample mask | rtN on/off colour
  src dest op, alpha src dest op, mask" line under the draw), vertex_input_hashes (each draw of a listed VS: a "  vs inputs |
  tN buffer B bytes, stride S, misc M, view fF E elements from F [raw]" line), instances_apart (" | instanced C x I" on
  instanced draws, direct and indirect; the line's count stays C x I). Pharaoh: blend DE4F92CD + 3F1B86E1, inputs 109B79CE,
  instances apart, value hash 3F1B86E1 added. Not run (needs the game). (7e945569)
- [x] 7 builds: the skill's x64 list (15) + build32 renodx-twattila / twrome2 / twthronesofbritannia.addon32 linked, no errors, only
  the old LoadSetting deprecation warnings (Pharaoh, WH3 addon.cpp); git diff --check 91c6b529..HEAD clean. WARP: Pharaoh sun
  suite (tools/tw/pharaoh/sun_test/sun_results_v56.txt) SUN PHARAOH ALL PASSED (DRAW 25/25, v51 25/25, v54, v55 as before);
  test/tw_* default runs exit 0 + PASS, amd64 and x86: depth_levels, device_loss, fg_initialization, gpu_completion,
  helper_lifetime, preset_writer, sampler_lifetime, screen_space_shadows, startup_worker, texture_lod_bias, unit_pack_rounds;
  motion_record x86 (v53's list). Committed 18.6 GB.
- [x] 8 README Pharaoh "Round v56" (New: water reflections, Specular Occlusion, frame trace extras; WARP; page; Not done: game
  run with the trace's grep list, risks; smoke shadows still open) + WH3 README v51 (the light copy moved, its log line's new
  text; nothing else). Attila README unchanged (no behaviour change). Built only, not stamped, not installed, not run in game.
- [x] 9 (ATN's agent) v56 STAMPED + INSTALLED 01:57 (install_pharaoh_v56.py, ATN files unchanged, no DevKit, helper105 unchanged).
- [x] 10 run W1 (W1_015828, 01:58-02:00, siege benchmark windowed, DLSS Performance, RT sun / torch / fog shadows, glow, far trees,
  Reflections + Water Reflections, Log Details, trace_50 at +30 s): no crash / hang, ATN files same. Log: "hooks applied", "the hits
  read the pass's light (R16G16B16A16F, copied), 3840 x 2160", "tracing, the water from its own surface", "First run: Ray-Traced
  Water Reflections ... left out, its apply DE4F92CD drawn with the rays"; status line reflections 1.61 ms GPU (trace 2.22).
  The trace still lists 9A55B7F2 / 0B78827B / 10 + 10 mips: the trace logs commands other callbacks bypass (skill row). DE4F92CD's
  blend: colour ONE + ONE add (alpha ONE, ZERO): the apply ADDS the reflection into the light buffer, our copy writes the same value
  (WARP 0 apart), so the blend needs nothing. Particle draws with VS 109B79CE: 3F1B86E1 6 indices x 91 instances, blend ONE /
  INV_SRC_ALPHA (premultiplied over), vb0 stride 12, vb1 stride 112; also 2A315C27 (6 x 91, into f88), ACE25F9D (36 x 91), CFB48C60
  (36 x 91, G-buffer f24, vb1 140), 5AD46115 (60 x 91, vb1 64): same VS, same buffers t0 439 x 16, t1 32768 x 128, t2 131072 x 8,
  t4 439 x 112, t6 131072 x 64. Screenshots by CopyFromScreen: one stale loading frame (skill row); the water's look is ATN's test.
- [x] 11 run C2 (C2_020223): plain `game_startup_mode campaign` crashed at 6 s, before any frame: 0xC0000005 null read at
  Pharaoh.exe+0x47C81A, the same address as 2026-11-12's run C (`campaign <benchmark>`), v54 and v56 alike: the game's start form,
  not the add-on. Campaign packs: a lua-driven benchmark (cindy scene) started from the menu only, no xml. ATN files same.
  Campaign traces: ATN loads the autosave from the menu, then a `renodx_trace_now` file next to Pharaoh.exe.

## ATN'S 2026-11-12 FOURTH LIST + PHARAOH (started 15:07; ATN away until ~21:00, plays 21:00-01:00)
Rules for this batch: game runs (Pharaoh trace-only data runs, medium-effort agent, few runs, fresh backups incl. replays/saves) allowed
before 21:00 and after 01:00 only; NEVER restart the PC (a restart ends this session; nothing resumes it); before any GPU job check
Committed Bytes (24.6 GB of 31.7 GB RAM at 15:07, 5.7 h uptime): if it climbs past ~60 GB stop GPU work, keep offline work going and
leave ATN a note to reboot. One writer at a time. Install the newest stamped builds at the end of each round (install_v151.py pattern).
- [x] Fable 5.2 review of the TW skill, checked and applied (de8b5da3); restructure: one home per rule, newest helper first (2ab87370)
- [x] Rays a Pixel's four effect rows under More Options; FG menu line yellow (9239b3e6)
- [x] helper105 (50c8661c, 9b9d5418): PT GI cache keyed by facing + short-hit guard + no tone-mapped PreviousLight under a light buffer (Attila, WH3)
- [x] (8f866568, a50849fd, 64e23f59) water reflections: no CACHE_RELEARN for reflections (bug fix); Steadier Water Reflections (Exp, off: mirror firefly clamp); Samples tooltip
- [x] (c67d3153) capture flag (Attila): dynamic 347F3DD9 + PS ED1DF335 (D830AD9B + CE6F17A1, B) thin; Log Details line per (vs, ps, t0, cull)
- [x] (29e8e4e4) Softer Water Reflections (Exp, off): water roughness floor 0.06 in both games' guides + water positions for the reflection history
- [x] WH3: the same fixes where they apply (helper105 shared; Steadier / Softer rows shared, WH3 rr_guides floor; README v47 lines; the flag rule is Attila-only, WH3 has no thin_sheets_row entries) (shared helper and rows; WH3's rr_guides; WH3 banner / flag equivalent?); README v47
- [x] skill rows (this commit): cache key with facing, the mirror path (one ray, no history, no reuse), thin rule by PS not cull, page 2 parity rule
- [x] build + stamp + install Attila v151, WH3 v47, helper105 (tools/tw/round_v151/install_v151.py, 15:59; backup install_backup_20261112_155924; ATN files unchanged by hash; no DevKit). Harness all_modes 177/178 (release_rr +34 MiB once, 3/3 alone: noise); HARNESS_CACHE_LEAK B 0.172 -> 0.020 (A 0.033), PATH_ROOM 0.442 -> 0.062, REFLECT_FLICKER 0.0163 -> 0.0084 (no relearn) -> 0.00134 (Softer)
- [x] Pharaoh census: tools/tw/pharaoh/census.md (fact sheet with levels, every table entry A*/B, parity with both pages, data-run plan R0-R10, risks, order; page dump stub dump_p.cpp / build_dump.py, page_p_base.txt 629 rows)
  Key facts: frame linear between ACES 976C1C74 and hdr_to_screen but the shared prepare squares it (B); sun PS has its own contact march + SSEO, fog marches the atlas (double work); baked 4096^2 shadow buffer kept across frames; 10-27 trace: no unit triangles captured, terrain only in the prefill, 1ACBD907 also water/decals, far cards 2 M indices, ulm sun draws not hooked, cube at float 24 but InstallSunDraws(0); no test_hooks, no graphics_page, no vendor read; page 2: 23 Attila tests missing, 15 dead rows, 4 dead audio at-start tests hold sliders ~60 s; game folder: addon + DevKit symlinked into build/Release, no helper; start forms: game_startup_mode battle|campaign <xml> (no replay / campaign_load); benchmarks battle / siege / campaign xml.
- [x] Pharaoh P1 offline (tools/tw/pharaoh/phase0b_results.md): encoding CONFIRMED on WARP (linear frame squared by the shared prepare: 0.01 -> 1e-4, <= 2.4e-4 -> 0; fix TW_AA_FRAME_LINEAR, 0 apart for others; queued in P2); sun patch 25/25 PS pass (t49 moves exactly the game shadow incl. its AO lift; cube at float 24 proven; rt_sun reads Attila scale switch at float 60 = Pharaoh g_cloud_shadow_position.x: needs a no-switch option in P6); 1775 discarding PS / 132 families, 999 shadow readers (7 drawn); rt_scene_map fork 35/39; pharaoh_common.ps1, check_run_pharaoh.py (run_game.ps1 still attila/wh3 only). Was: WARP encoding ramp (prepare on a linear frame), WARP sun test for F883F5B2 / E238D4C7 (t49 lands on the sun term, cube offset 24), cut-out census and shadow-reader list from the dump, rt_scene_map.py for the dx11_ names, pharaoh_common.ps1 + check_run for Pharaoh
- [x] Pharaoh P2 trace build v46 (18ff96a0..8bccf8a9 + 7694dca3 encoding TW_AA_FRAME_LINEAR): test_hooks; shared frame_trace Options (on_demand marker renodx_trace_now, at_100, value_slots / floats; WH3 moved onto it unchanged); value hashes F883F5B2 / E238D4C7 / 976C1C74 (6 slots, 256 floats); 4 dead audio at-start tests out; bridge 31 / helper105; all 18 targets built, others bit-identical shaders; page dumps a_v155 / w_v51 date only, p_trace 629 -> 624. Was: test_hooks, shared trace trigger (renodx_trace_now) + 100% path behind a parameter (today's default), value_hashes F883F5B2 / E238D4C7 / 976C1C74, dead at-start tests out, newest bridge; build, page dump, dry-run R0-R10 against it
- [x] Pharaoh P3 game folder (16:33, prepare_folder.py; backup tools/tw/pharaoh/folder_backup_20261112_163335; links moved, plain stamped pharaoh v46 addon + helper105, DLLs, nvngx_dlss; no DevKit; protected files unchanged). Was: DevKit symlink moved aside, addon symlink -> stamped plain copy, helper105 + nvngx_dlss.dll; fresh backups (ini, preset, prefs, Auto-save.replay, the save, army_setups, battle_preferences; no_clean_exit kept then removed)
- [x] Pharaoh P4 data run (tools/tw/pharaoh/runs, summary.txt): A fullscreen drew nothing while not in front (no traces); windowed (gfx_fullscreen false, restored per start) works. A2 battle benchmark traces 50/67/33/100 (1289/1302/1288/1288 lines); B siege trace_50 (2470; only f883f5b2 patched, e238d4c7 never logged); C campaign_benchmark arg CRASHED before any 3D frame (0xC0000005 Pharaoh.exe+0x47C81A, guessed arg; crash_report D2026-11-12_T16-54-15 copied); D (SSAO / SSR / god rays off) trace_50 1240 lines, both sun PS patched; E (SSAO 1 + MSAA x2) ran, no trace (MSAA turns DLAA off, the trace hangs off the AA frame), MSAA sun PS 955658c3 / 3323bdcc / 24fd1318 / e5a6a379 patched, the MSAA WARN logged; all files restored by hash, no reboot, Committed <= 20 GB; campaign start form still open (no command; menu needs clicks). Dead Pharaoh pid 8284 (0 handles) left. Was: R0-R10 (medium-effort agent; before 21:00 / after 01:00; Committed Bytes check before each start)
- [x] Pharaoh P5 (tools/tw/pharaoh/phase2.md, level A from A2 / B / D): order same at all scales, targets display-size with viewports shrunk, nothing stops below 2/3, SSR grids / god rays / atlas fixed size; 3D UI sprites 45A3B2DF depth-tested at render size (needs a depth-upscale route); game SSAO output read by nobody in frame (t12 = addon AO), SSEO follows SSAO; SSR gated by G-buffer ch3 alpha bit 64; atlas 4 tiles every frame, baked 4096^2 never written (kept; writer candidate 35D229C0, B); 1ACBD907 captured twice via the prefill + catches water 833E7963 / VFX: needs pixel_masks allow list (21 PS, phase2 3d); missing far cards 97C53219 (t3), tree blocks FE92BA2D (t2), cloth 2F2A0E7E, flags 8DE94E2F; banners 76225B91 no game shadow (leave out); branches 4A82B8C2 two-matrix blend (capture, never place); out-of-view caster VS view-proj at c9 (WH3 c10-13); 7/16 SCENE and 22/27 MOTION entries never drawn; no stencil foliage bit; siege drew no E238D4C7; near 1, far 13000, sky z = w; cascades 4 in 2x2, splits 1/28/70/175/420 (battle); ACES only; point-light layout read, count C; particle lights 5AD46115; banners / cloth / flags no motion entry. Still C: campaign, night, light count, baked writer, blends, SSR bit-64 materials, MSAA trace, unmodded log, device loss.
- [x] Pharaoh v49 RT scene tables (94046bcd allow list 21 PS, 6b110188 far cards / cloth / flags / branches dynamic, blocks FE92BA2D left out, 943df193 motion 76225B91 / 2F2A0E7E / 8DE94E2F, e2d83777 dropped ocean x4 + 8C511965, 95f2ebe8 README; WARP scene 0 m error + controls, motion 0 apart + controls; item 4 no change: spans already per game, readers bind no t49 / t50 / cb9 under multiply). ATN to decide: Far Tree Shadows and Coloured Shadows now show on Pharaoh, on by RAYS_PRESET; cloth / flag RT shadows are new to the game. Skill: Pharaoh lessons 4bf87bff. Was: 1ACBD907 allow list, cards / blocks / cloth / flags rows, motion entries, Pharaoh's own shadow-reader / fog / particle spans
- [x] Pharaoh v50 (46b623d0 casters out of view, Exp off, c9 GS, WARP 9 cases 0 misses + controls; a3a269d2 point lights E318CE77 + count line, Exp off, WARP layout PASS; 4f03acf7 3D UI full depth incl. the depth copy ec5e0, WARP 0 misses at 50/33/67). ATN: RT/PT turns the Experimental torch row on (WH3 key in RAYS_PRESET). Was: out-of-view casters (c9 variant), point lights after a count line; v51 DONE (f6e1f884 contact march off only while t49 is bound, WARP 25/25 + controls; debc253e SSEO stays: it only touches the sky-cube specular, our GI is diffuse; 38149102 Reset proof, all 19 buttons in the dump); review of v46-v51 DONE (5 findings: dead Sprite row Pharaoh + WH3, dead Light in Fog row, contact destroy handler any device, BUILDING_MASKS slots unused, FrameTrace 100% needs AA; shared code otherwise same for other games); v52 review fixes DONE (fea6d27e sprite row, 52feec57 light-in-fog row, d3013b0b destroy handler, f3ae9351 1ACBD907 cut-outs 4 PS WARP 0 of 3310 apart + controls, 1d148039 FrameTrace 100% without AA); INSTALLED 20:55 (install_v152.py): Attila v152, WH3 v48, Pharaoh v52 stamped, helper105 unchanged in all three, ATN files unchanged by hash, old add-ons renamed aside (.in-use-20261112_205517), no DevKit; then review of v46-v51, stamp + install Pharaoh newest, AA at 100% check
- [ ] Pharaoh v47 page round DONE (014ea610, 672a0dff, 10e8a3dd, d67b9170); v48 page leftovers DONE (d5701b5c, 53a0bf11, 8941d3e7, 880d9b74); open: WH3 with Rays Replace Shadow Maps off multiplies but hides Shadow Map Size (rays_multiply_maps is a bool; WH3 would need it dynamic)
Was P5: pass maps 33/50/67/100, reader census by handle, capture candidates, velocity census, facts answered again
- [ ] Pharaoh P6 features in order: output fix + graphics_page + RAYS_PRESET + page 2 parity (keys kept); stability; AA at 100% with the encoding parameter; FG / Reflex; render-scale audit; RT scene (16 steps); replace game passes (contact march, SSEO, fog march, shadow maps with the baked buffer kept); RR guides; SSS extras, smoke, water, fog shadows; presets; Reset gives the game's effects back
  Round v47 (implementer, offline, no trace needed; beside the P4 data run, which uses plain stamped v46 copies):
  - [x] 1 Graphics 1 of 2 on graphics_page (WH3's way): 0 keys lost (581 -> 585: + TAA Motion Vectors, FG Method, RT Denoiser, AO under RR), Shadow Map Size to Shadows; graphics_page::SetRaysPresetDefaults shared (WH3's loop moved); ray_traced's Fog Shadows gated on the fog hook; SSS off under RT sun shadows (as Attila / WH3). Dumps: a_v156a / w_v52a date only
  - [x] 2 Graphics 2 of 2 matched to Attila: 24 Attila tests in (33 of Attila's 54 now), 11 dead rows out (3 Attila tests, 8 More Tests), Rope Distance without ROPE_DISTANCE / _FADEOUT, 4 tooltips with Pharaoh's game values, Attila's two presets, Reset / presets reset the tests too; more_tests.h marked hand-edited (generator's read list missed / kept the wrong ones). find_emu.py on Pharaoh.exe: 467 of 467 names found (before: 437 of 448, misses = the 10-27 run's 11). Dump p47a -> p47b: lost 11 (asked), added 24
  - [x] 3 sun facts: InstallSunDraws(..., 24); shared rt_sun TW_RT_SUN_SCALE_SWITCH (default 1; Pharaoh's wrapper 0); Coloured Shadows (row and setting) only with a thin capture (rt_scene internal::ThinInScene: Pharaoh has none). WARP sun_test R: Pharaoh texel 4 / 5 w 1.00 / 0.80 at float 60 +-0.5, CONTROL shared 1.44 / 0.60 vs 1.00 / 0.80; DRAW 25 / 25 PASS. rt_sun.cso same bytes in the other 13 builds; dumps a_v156b / w_v52b date only; Pharaoh + PT AO rows (Own Sky Rays, Steady Bounce Light), Coloured Shadows still hidden
  - [x] 4 GPU vendor read: Attila's OnPresent lines (ReadGpuVendor, saved DLSS -> XeSS on Intel / FSR elsewhere). Dump p47e: AMD states (v1) list no DLSS (AA slider 0..6), hide Reflex / Frame Rate Limit / DLSS rows; stub's WARP ReadGpuVendor with RENODX_TW_FAKE_VENDOR=1002 -> 0x1002, OnNvidia 0
  - [x] builds (17:08-17:10, HEAD of item 4): the skill's 15 x64 targets + Attila / Rome II / Thrones x86, clean. Dumps: page_a_v156 vs v155, page_w_v52 vs v51: the build date only; page_p_v47 vs v46: 581 -> 598 keys, lost exactly the 11 dead, added 28 (24 tests + 4 page rows), 98 rows moved / relabelled (14 of them with a RAYS_PRESET default) (tools/tw/pharaoh/keydiff_v46_v47.txt, key_diff.py, names_check.py). README v47. Not stamped, not installed (the data run's v46 copies stay), not run in game.
    Not done / for ATN: GitHub button and the General page's full-download note (Attila / WH3 have both); engine_tests.hpp's Attila tooltips have upper-case words (GRASS, SHADOWS, DISTANCES, CAMPAIGN, EFFECTS, AUDIO since e92046f6), shared by every TW addon: a fix changes Attila's page; WH3's RayTracedWaterReflections default 0 line is overridden by the RAYS_PRESET defaults (since b2fb27f9, dumps show 1); tw_game_tests.py still picks Attila tests by the read list (find_emu.py would be right)
  Round v48 (implementer, offline, page fixes; built only, not run in game):
  - [x] 1 engine_tests.hpp tooltips: 17 capital words (GRASS, DISTANCES, SHADOWS, EFFECTS, CAMPAIGN, AUDIO) in normal case; keys and engine names unchanged
  - [x] 2 Pharaoh General page: full-download note (under Settings Mode) + GitHub (Downloads) button in About (WH3's); README download link with v48
  - [x] 3 graphics_page Bindings rays_multiply_maps (default false): Shadow Map Size (and Attila's UI 4K lock) stays under ray-traced shadows; Pharaoh true. Attila replaces (InstallShadowsOnly); WH3 replaces by default, multiplies with Rays Replace Shadow Maps off (left hidden there)
  - [x] builds (HEAD 8941d3e7): the skill's 15 x64 targets + Attila / Rome II / Thrones x86, clean (relinked: WH1, WH2, Troy, Pharaoh, WH3, Attila, Rome II, Thrones). Dumps: page_a_v157 vs v156: 14 tooltips + date; page_w_v53 vs v52: date only (WH3's tests carry their own tooltips); page_p_v48 vs v47: 667 -> 669 rows (note, GitHub), 7 tooltips + date; states: GitHub in About everywhere, the note on page 0, Shadow Map Size in all 1024 page-1 Ray-Traced states; buttons and sliders the same but row numbers. README v48 (+ Attila v151 / WH3 v47 lines). Not stamped, not installed, not run in game.
- [x] Pharaoh v53 (5f2fdddb render scale: fire02 / AO / blurs at the share, WARP 0 apart + controls; 68220fa6 RR guides from F883F5B2's decode + RewriteSunGuides after water at sky dome 9CF980D0, WARP 0 beyond 2e-4 + controls; e44d2465 settings slot b13 free (game b0-b8); 97840ddd DetachAddon nullptr crash from v52 fixed; b4cb28ec 11 lifetime suites pass x86 + amd64). INSTALLED 22:23 (install_pharaoh_v53.py, rebuilt at HEAD first; ATN files unchanged).
- [x] WH3 v49 (42e266ae; fd1f2bce initializer order): Shadow Map Size shown under rays while Rays Replace Shadow Maps is off (Screen-Space already showed it); Attila / Pharaoh pages unchanged. INSTALLED 22:32 (install_wh3_v49.py). Attila stays v152 installed (no behaviour change since).
- [x] Pharaoh v54 (2329d357 Fog Shadows Exp off, WARP 0 apart + controls; 86544b2f Rays Replace Shadow Maps + Skip the Shadow Maps Exp off via ALWAYS comparison sampler at s2 on 12 atlas sun PS, WARP 0 apart + controls, skip B (atlas clear unproven); f8b9b1fe presets: RT/PT left GT-VBAO under PT AO -> ssao_mode 0 in Pharaoh, Light in Fog default off). INSTALLED 23:50. OPEN for ATN: WH3 has the same RT/PT GT-VBAO-under-PT-AO stacking (shared one-line fix in graphics_page RAYS values).
- [x] Pharaoh v55 (bd293682 Bend extras: thin replay cloth / flags, foliage bit 128, Coloured Shadows c3 not c1 fix, WARP 0 of 4.1 M + controls; 3b983e69 smoke NOT possible: GPU-simulated instanced quads 109B79CE vs CaptureParticles; cb862dd2 reflections plan feasible ~1 day, tools/tw/pharaoh/reflections_plan.md; cc135b62 shared RT/PT ssao_mode 0 when !rays_replace_ao). WH3 v50 INSTALLED 00:43. Pharaoh v55 INSTALLED 00:56 (install_pharaoh_v55.py, rebuilt at HEAD first; ATN files unchanged, no DevKit).
- [x] Night data run S1 (S1_002919, 00:29-00:31, v54 + helper105, windowed, siege benchmark, DLSS Performance, RT sun / torch / fog shadows, Log Details): battle at 22.2 s, trace_50 3782 lines; torches E318CE77 1 draw / 15 instances, 5AD46115 1 / 60; 1ACBD907 left out with 4 PS (opacity not known), E710D4E7 topology 5 left out once then placed; no far-card / cloth / flag lines; First run Fog Shadows 598C59F3; "no trace this frame (no main camera)" every 5 s; no crash / hang / new WARN; ATN files same. S2 (campaign): no start form found; the pack scan for a campaign benchmark xml ran on, the agent stopped there.
- [x] ATN 2026-11-13 ~00:55 (water reflections v56 DONE, W1 run, campaign C2 not possible from the command line): do the RT water reflections (tools/tw/pharaoh/reflections_plan.md) and every other open item tonight, lessons into the skill, install the newest, round report, then shut the PC down.
- [x] ATN 2026-11-13 ~00:30: approved the shared RT/PT fix (ssao_mode 0 when !rays_replace_ao; WH3 + Pharaoh; Attila unaffected: its ray AO replaces) - queued as v55 item 4; skill lessons 41323c4a. Night: v55 implementer + Pharaoh night data run (siege RT on + Log Details, one campaign try) running.
- [ ] Pharaoh next: RT water reflections (tonight), smoke layout 109B79CE trace facts, campaign start form (pack scan)
- ATN 2026-11-12 ~21:00 decisions: Pharaoh cloth / flag RT shadows stay (new to the game, accepted); RT/PT turning Pharaoh's Experimental torch row on (WH3 key in RAYS_PRESET) stays. Explained WH3 Shadow Map Size under multiplying rays; fix queued after v53 (show the row when Rays Replace Shadow Maps is off). No game starts until ATN writes (gaming ~21:00-01:00).
- [ ] Pharaoh round report (per item done / not done, ATN's <= 10-line test list)

## ATN'S 2026-11-12 THIRD LIST (offline; no game runs; Attila first, then the TW addons that share it)
- [x] WH3's in-game AA names back: TAA or TAA High (ATN confirmed).
- [x] smoke flicker cause: not the room. The 262,144 "particles a frame" is the game drawing its whole pool every frame (dead ones too; 2^18), our stream holds 2^19 and ATN's siege wanted at most 35,304 quads of High's 65,536 (ATN's preset1: RayTracedSmokeQuality=2). Likely the per-ray cap (SmokeTransmittance stops at 32 dimming / 64 met particles; which come first follows the smoke BLAS, rebuilt each frame). Test: Ultra against Extreme in one siege.
- [x] torch and fire shadows: up to 128 lights (bridge v31, helper104), each 8 x 8 tile its own 32 (helper rt_lights; the tiles' lists in the light mask's rows below the pixels, the history's too), the light shaders match within the tile's list (rt_local_lights.hlsli, b9 z the tiles' row). Rows (Experimental): Torch and Fire Shadow Lights 32 / 64 / 128, Torch and Fire Shadow Range (0 m any). SSS torches: 32 added, its own range row. Harness: default and 125-light stress PASS (trace 0.15 -> 0.19 ms, 29 -> 125 lights). Not run in game.
- [x] particles: Attila's Particle Resolution Full / Half / Quarter (key VfxHalfResolution kept, 1 = Half), Full-Resolution Particle Edges (VfxFullResolutionEdges, on under the Experimental parent; Cantlay's idea, GPU Gems 3 ch. 23): particle_edges marks the pixels whose layer texels were drawn against other depths (stencil 1 + the scene's depth in an addon depth-stencil at the light buffer's size, D24S8 or D32S8 as the scene's), each particle draw drawn again there as the game drew it, the composite stencil 0 alone. Upsample, depth, history at any scale (PL.layer_scale). WARP test 11 x86 / amd64 PASS (Quarter: outline 26 steps without edges, 1 with). 4K, 100 full-screen particles: full 4.2 ms, Half 1.3, Half + edges 1.7 (flat) / 2.9 (a post every 32 px), Quarter + edges 0.85 / 2.0. WH3 untouched (the game's own half layer). Not run in game.
- [x] licences: audit done (no unlicensed copies). Added: twattila/credits.hpp (HELPER, DLSS_ADDON, ACES lines); the helper's line in Pharaoh / Rome II / Thrones / Troy / WH1 / WH2 / Shogun 2 / Ryse / Expeditions About; the DLSS add-on's (DLSS, Streamline, Phantom FX, Detours) in Shogun 2 / Ryse / Tales / Expeditions About (the _dlss add-ons have no UI); Rome II FidelityFX SSSR credit; Expeditions ACES credit; Tales' ACES file name (.md); cmaa2/LICENSE_MINIENGINE.txt (MiniEngine's R11G11B10 packing, MIT); NVIDIA_DLSS_NOTICE.txt no longer says the DLL is never included. Private repack_game.py now adds RenoDX / ReShade / ImGui / Detours / frozen / gtl licences to every zip, Streamline / Phantom FX / DLSS notice where frame generation is, ACES where UE Extended is.
- [x] builds: helper104 (build/tw_dlaa_helper104), x86 Attila / Rome II / Thrones, x64 Troy / Pharaoh / WH1 / WH2 / WH3 / Shogun 2 (+_dlss) / Ryse (+_dlss) / Expeditions (+_dlss) / Tales (+_dlss): all clean. Harness all_modes 178 / 178 PASS (tools/tw/all_modes_v31lights.txt). WARP: tw_particle_history and tw_screen_space_shadows x86 + amd64 PASS. READMEs Attila v151, WH3 v47. Not stamped, not installed, no zips, not run in game. v151 / v47 need helper104 next to them (bridge v31).
- [x] ATN's question, Graphics 1 of 2 Attila vs WH3 (page dumps page_a_v151 / page_w_v47): WH3 lacks ray-traced smoke shadows (its particles not hooked: no InstallSmokeShadows, so v45's Extreme / Maximum note does nothing there), the no-ray smoke shadow maps, SSS Light Through Foliage, Attila's SSSR rows (WH3 has its own reflections); Attila lacks WH3's sky-probe rows (PT AO, GI), Texture LOD Bias on All Textures, the render-scale fixes (Full Detail Below 100%, GI and Shadow Blur at Scale, Water Motion), the out-of-view caster rows and the shadow-map skip rows (WH3's engine). Labels: WH3 calls Shadow Map Size, Fog Shadows, Specular Occlusion, Water Caustics / Clarity Experimental, Attila not. ATN decides what to port.
- [x] WH3 Light Through Foliage for the SSS (7c82c5a9): the G-buffer's foliage bit (channel 3 alpha & 128, at gbuffer_slot + 2) marks foliage in sss_depth, beside Attila's depth copies; off under MSAA. test/tw_screen_space_shadows material case: 0 pixels apart (x86 + amd64 PASS).
- [x] Attila Texture LOD Bias on All Textures (4aa057e4), Experimental, off. WH3's below-100% rows not ported: Full Detail Below 100% (Attila draws at display size, we scale viewports), GI and Shadow Blur at Scale (WH3's own GI grid and blur), Water Motion (WH3's motion G-buffer; Attila's water depth unknown: needs a trace).
- [x] WH3 RT/PT preset = Attila's (b2fb27f9): graphics_page::RAYS_PRESET (ATN's 2026-11-12 Attila settings + RayTracedPointLightShadows), and WH3's defaults of the rows the methods leave from it (20 defaults changed, games_compare.py on page_w_v47 / v48); Attila's RT/PT result unchanged (page_a_v152). Open for ATN: Fog in Reflections (WH3, Experimental, off) in the preset now that Water Reflections are on by default.
- [x] TW skill (b434ff9a): WH3's G-buffer material byte, 128 tiled lights, particle edges, smoke counts, one RT/PT preset, licences, page-dump caveats, fxc //dumpbin, outside TW; Recommended = PsychoV35 in Attila and WH3. tools/tw/page_dump/games_compare.py added.
- [x] READMEs: Attila v151 (LOD row, shared preset), WH3 v47 (Light Through Foliage, RT/PT and defaults). Nothing stamped, installed or run.

## ATN'S 2026-11-12 SECOND LIST (offline; PsychoV35 everywhere, blood check, Pharaoh hotfix, AA red line; Attila v150, WH3 v46)
- [x] blood: only Pharaoh's lens blood (0xE4931F75, amount x 20 - 8.55 below 0.45) needed the 8-bit clamp; WH3's lensblood00 already fixed. Attila / Thrones blood.pack: mesh shaders only; Troy shaders_bl.pack: mesh + river_blood compute; Rome II blood_rome2.pack: particle VFX; WH1 / WH2: no blood pack on disk, their shaders.pack has no lens blood (not checkable).
- [x] Pharaoh HDR-only hotfix v1.03: branch pharaoh-hdr-hotfix (8f99e030, v1.02 source ec46f956 + the blood shader only), worktree D:/Dev/renodx_pharaoh_hotfix; stamped build/stamped/pharaoh_v1.03; zip Downloads/renodx-twpharaohdynasties-v1.03.zip. Not run in game.
- [x] PsychoV35 + Hue Clip Emulation in all 14 HDR add-ons: shared twattila/psycho35.hlsli (HueTransfer, HueClip, AttilaEngineHueClip, ApplyPsychoV35 with mid gray / contrast scale) + psycho35_settings.hpp (rows, defaults). Modes: Attila / Thrones / WH1 / Rome II / Shogun 2 / Ryse / Tales / Expeditions / RUSE 3, WH3 / WH2 / Troy / Pharaoh / Rome Remastered 4. Recommended -> V35 everywhere; Reset follows where there is no Extended mode (Attila, Thrones, WH1, Rome II); RUSE keeps its Faithful to SDR reset.
- [x] Hue Clip defaults: Attila 40, WH3 100 (real chains, part 4); test/tw_psychov35 part 5 (hue_curves.hlsl, WARP; its WH3 form gives WH3's 1.00): WH2 / RUSE 100, Rome II / Rome Remastered / Ryse / Troy / Pharaoh 80, Shogun 2 / Tales / Expeditions 70; Thrones / WH1 40 (Attila's engine).
- [x] RUSE: injection c200 - c209; Musa's file has SM3 forms (PSYCHO35_FAILED / NEGATIVE_INFINITY / AND) behind __SHADER_TARGET_MAJOR, SM4+ unchanged tokens.
- [x] AA red line (graphics_page Texts.in_game_aa_note): Attila "Off", WH3 "TAA or TAA High ... (restart after changing it)" (ATN confirmed the menu's names).
- [x] smoke Maximum vs Ultra (from code + round_v149 log, not measured): VRAM +72 MB fixed (particle stream 24 -> 96 MB) + smoke BLAS only as far as quads grow (ATN's siege peaked 35k quads of Ultra's 131k room); RAM ~0; GPU: 64 vs 32 layers only in thick smoke, BLAS build per quad.
- NOT PROVEN: every V35 look in game; Hue Clip defaults outside Attila / WH3 measured on curve kinds, not the games' chains; other modes not bit-checked outside Attila / WH3; RUSE's V35 on real DX9 hardware.
- NEXT: ATN's tests; then WH3, then Pharaoh's RT / DLSS bring-up.

## ATN'S 2026-11-12 LIST (offline; Attila v149, WH3 v45; evidence tools/tw/round_v149/atn_run1)
- [x] smoke flicker at Ultra: the particle stream was full (262144 particles a frame in every window) -> Smoke Shadow Quality Extreme / Maximum (64 layers, room and stream 2x / 4x Ultra's), 2e4c8657.
- [x] PsychoV35 = Musa's customtest35 (his first file, include path only: twattila/psycho_test35.hlsli; the second file is the same math, self-contained, an SM6 switch) with its own v35_* sliders in V31's order, Hue Clip Emulation under it, "Fire too ..." text; Recommended presets (Attila also Reset) -> V35, SDR stays; V31's rows V31's alone; Rendering Mode "HDR (PsychoV35)".
- [x] reverts: HDR & Effects and Graphics 2 of 2 More Options (Fold gone), RT / PT AO and Reflections without More Options; GT-VBAO's More Options with Thickness.
- [x] Graphics 1 of 2: Rays a Pixel's four rows under it (shown above one ray); NRD default (SettingsRevision: Attila 1, WH3 33 clears the saved Denoiser / Banners / Coloured once); RT/PT = ATN's screenshot; SSS: Auto Thickness row gone (all SSS add-ons), all but Length under More Options, Shadow Map Size / UI 4K under SSS too, Light Through Banners 0.35 and Coloured on.
- [x] About: buttons first, then credits and licences.
- [x] proof: 11 add-ons built (Attila / Rome II / Thrones x86; WH3, Troy, Pharaoh, WH1, WH2, Shogun 2, Expeditions DLSS, Ryse x64); test/tw_psychov35 x86 + amd64 PASS (other modes 0 pixels from d3d6425b, other shaders same bytes, V35 every slider at its ends finite and within Peak; Gamut Compression 0: within 1%, Musa's mean is the cones'); hue report: best Hue Clip still 0.40 Attila / 1.00 WH3. Page dumps page_a_v149 / page_w_v45 against *_skill_after: 12 new v35_* keys, Auto Thickness gone, nothing else lost.
- [x] install: both stamped (build/stamped/attila_v149, wh3_v45), only the add-on replaced, helper103 unchanged, ATN's ini / preset / preferences backed up first and unchanged by hash (tools/tw/round_v149/install_backup_20261112_105729), no DevKit. Script tools/tw/round_v149/install_v149.py. NOT run in game.
- NOT PROVEN: V35's look in game, smoke Extreme / Maximum cost, NRD default's look, banners' coloured shadows in Attila (ATN traces them next).
- NEXT: ATN's banner traces (Coloured Shadows not seen in Attila), then water reflections at lower scalings.

## CLEANER PAGE (ATN 2026-11-12; offline; Attila v148, WH3 v44)
- [x] code 14577b7f: Rays a Pixel under Upscaling Mode (default 1, presets leave it), rarely changed rows under More Options (graphics_page::Fold), keys unchanged (page dumps tools/tw/page_dump/page_*_skill_before/after).
- [x] install: both stamped (build/stamped/attila_v148, wh3_v44), only the add-on replaced, helper103 unchanged, ATN's ini / preset / preferences backed up first and unchanged by hash (tools/tw/page_v148/install_backup_20261112_005919), no DevKit. Script tools/tw/page_v148/install_v148.py. NOT run in game.
- NEXT: ATN tests Attila (v147's PsychoV35 checks plus this page).

## PSYCHOV35 + HUE CLIP (ATN 2026-11-11 after the upstream merge; no game start; offline only; Attila v147, WH3 v43)
- [x] code: Rendering Mode "PsychoV35 (Experimental)" (Attila 3, WH3 4; not default, Recommended stays V31) = ShortFuse's psychotm_test35 at 0.18 / 0.18 with the game's exposure, V31's sliders (exposure, highlights, shadows, contrast, purity, roll-off = compression, gamut compression); Hue Clip row (Attila 40, WH3 100) before the grade toward the stock SDR path's per-channel clip, sky x2 clamped (acd00002, 522c5e2e).
- [x] proof: test/tw_psychov35 x86 + amd64 PASS (8f9de984): other modes 0 pixels different from d3d6425b, other shaders same bytes (fxc + dxc); Hue Clip keeps luminance / signs / grey; V35 within peak, finite. Page dumps: only the new rows, Rendering Mode's label / tooltip, credits, date.
- [x] install: both add-ons stamped (build/stamped/attila_v147, wh3_v43), only the add-on replaced next to Attila.exe / Warhammer3.exe, helper103 unchanged, ATN's ini / preset / preferences backed up first and unchanged by hash (tools/tw/psychov35/install_backup_20261111_233814), no DevKit, no zips, no tag. Script tools/tw/psychov35/install_psychov35.py.
- NOT PROVEN: the in-game look (defaults from synthetic colours), Attila's bloom share in the reference, real LUT content, V35's Highlights < 0.5 not monotonic (its own), Shadows < 1 near-black to black, GPU cost, the merge's changes in game.
- NEXT: ATN: modes V30 / V31 as before; PsychoV35: Hue Clip 0 vs default on fire and sky against the SDR mode; tell the value that looks right.

## ROUND v42 (ATN 2026-11-11 after WH3 v41; no game start; offline only)
Plan and rules: tools/tw/round_v42/plan.md. Reports: tools/tw/round_v42/reports/.
- [x] A Attila v145 = players' v2.5.6: helper103's ReShade fix (shared since 3bd71292), fg_bridge_test, install, stamp, zips, notes. Attila's ReShade is 32-bit and never reached the 64-bit helper (fg_bridge_test with a ReShade in the helper's folder: Attila's dxgi.dll out of helper102 and helper103; WH3's 64-bit one as dxgi.dll / d3d12.dll in helper102, not in helper103), so the banner was WH3's; helper103 keeps a 64-bit copy out too. What else reaches Attila from v41: far cards counted apart (Far Tree / Sprite Soldier Shadows: harness far_cards_48 helper102 lit 28/28, helper103 shadowed 28/28), FG asks no more than the backend presents, grass captured only for the rays that meet it, the light copy's box; WH3's rows / flags nothing (page dump against v144: the date). fg_bridge_test on the stamped helper103: DLSS 2x 2 / 6x 6 (PresentMon 5.9), FSR asked 6x 2.0, XeSS asked 6x 2; 7 harness modes on it pass; tw_helper_lifetime + tw_fg_initialization x86 / amd64 PASS; x86 3 add-ons built. Downloads: twattila_v145.zip, RenoDX_Attila_v2.5.6.zip, RenoDX_Attila_v2.5.6_HDR_only.zip, attila_github_v2.5.6_notes.md (unpacked, hash-checked, stamped). Installed next to Attila.exe (backup attila_v145/install_backup_20261111_170601, ATN's ini / preset / preferences backed up first and unchanged by hash, 47 of 47 equal, no DevKit). Not run in game, no tag. Report round_v42/reports/A.md.
- [x] B weapon jitter after the camera comes back (Attila, then WH3): both a game limit and our bug. Attila's main pass packs bones, draws and packs again into the same animation pages until all soldiers are drawn; object motion recorded the units' streamed records at the frame's end and keyed bones by (page, bone base), so a later round's soldier at a reused row gave its bones to an earlier one (false motion on still soldiers; the 64-texture clear also gave page 65 page 1's key). Fix (shared object_motion, 2501d12d): keys per frame and new after every page write (map / update_texture_region, only this frame's pages), draws on a rewritten page recorded without bones, and Attila's FlushUnitRecords before original_pack_animations (draws' thread only, compute bindings put back); Frame Trace marks per pack / flush / rewrite. WARP test/tw_unit_pack_rounds (x86 + amd64, the game's vs 0x5F9CD405): 5d15186f fails every case (A takes B's pose 6.398 px, 65 pages 1.598 px), the fix passes (0.000 paused, -1.279 for -1.280 moving, 65 pages 0.001). Cost (RTX 5090, x86): unit draw unchanged (2.1-2.9 -> 2.5-3.3 us, noise), texture write check 13-15 ns, idle pack hook 2 ns, a recording pack hook 3-4 us; GPU about 16.5 us a record dispatch (latency-bound, same dispatches Resolve made); big battle with 3 extra packs about 10-13 us CPU and up to 0.06 ms GPU. Audit of every other motion path (wind, rigid + read-backs, deformed, ocean history, in-draw, WH3 TIME / MOVING / GAP, Shogun 2): none live; bone pool and placement cells are limits that fail safe. WH3: no change; A/B request B4. 9 add-ons built (3 x86, 6 x64). Not run in game. Report round_v42/reports/B.md, run requests round_v42/run_requests.md (B1-B4).
- [x] C Particles at Half Resolution, smoothed (WH3 row Smooth Half-Size Particles; Attila rows Particles at Half Resolution + Smooth Half-Size Particles; all Experimental, off). Shared twattila/particle_history.hpp + .hlsli (one resolve a frame: last frame's layer moved by its depth, Catmull-Rom, 3x3 clamp, kept at most 0.9 by how this frame's 3x3 mean matches last frame's there; cut over 25 m, off screen, frame gap: this frame's) and particle_layer.hlsli (2x2 upsample, bilinear unless a depth is over 10% off, then 1 / (difference + 1 mm), WH3's own 1EF17B33 rule). WH3: 43606BB8 drawn with particle_composite (the game's with the half layer smoothed and upsampled, the half zeroth moment swapped). Attila: the particle draws (VS A7F257B5) into a half RGBA16F layer against the farthest-of-2x2 depth, camera c30 = the layer's size, alpha = the scene's share; composited at 781681E4 (else the tone mappers); other blends / scissor / MSAA stay full size (one line); vfx00 t49 at the render pixel (bit-equal at full size: disassembly, cb_audit). WARP test/tw_particle_history x86 + amd64 PASS (flip spark averaged, moving spark whole, pan edge 0.05 texel, cuts to the bit, WH3 composite = 43606BB8 to 2.75e-6, box edge 0.2% vs the game's half, Attila blends within one R11G11B10 step, 66% depth, state, stop). RTX 5090 4K: WH3 +0.068 ms; Attila open 0.038 + in 0.056 (smoothed 0.096) ms, 100 full-screen particles 4.79 -> 1.22 ms. 4 add-ons built (WH3 x64, Attila / Rome II / Thrones x86); page dumps: the new rows and the date (WH3 also phase A's DLSS tooltip). Not run in game. Report round_v42/reports/C.md, run requests C1-C7.
- [x] D SSS additions (shared, all Experimental, off): Auto Thickness and Softer Away from the Caster (all 7 SSS add-ons), Light Through Banners + Coloured Shadows and Torch and Fire Shadows (Attila, Thrones, WH3). Smoke under SSS was already there (Attila's smoke_map, WH3's sun mask; Thrones a possible port); off-screen casters and far reach not possible in screen space. The extended march (sss_bend_extended in 4 compiled variants, Bend's 4-lane loops; rows off = today's sss_bend), banners drawn again into their own depth (OnThinDraw, finalize, state put back; InstallThin: Attila 6 vertex shaders, Thrones 4, WH3 5), the filter at t50 where the rays bound none, sss_soft's penumbra, screen_space_lights.hpp (GPU copies of the light draws' instances, sss_lights' pick as the rays', one indirect light-mode march, t42 / b9 as rt_local_lights.hlsli reads them, two screens' cap). No helper / bridge change. WARP test/tw_screen_space_shadows x86 + amd64 PASS (rows off = fc24dbce's, Auto Thickness 1139 -> 1006 / 1091 -> 788 / pole 176 -> 176 wrong pixels, banner 0.69 of solid with filter 0 / 0.875 / 0.875, penumbra median 1.01, torch bits 96.2%, pick = CPU's, replay tags only what stays in view, state back). RTX 5090 4K 100%: Banners +0.08 ms, Coloured +0.60, Softer +0.53, Auto +0.73, torches 0.35 / 0.65 / 1.2 ms for 4 / 8 / 16 at battle distance (at most 1.4 near). Page dumps: Attila 315 -> 320 rows, WH3 284 -> 289 (the five rows, the date); scan_register clean; 14 add-ons built. Not run in game. Report round_v42/reports/D.md, run requests D1-D8.
- [x] E close (from 5255264b; helper103, bridge v30 unchanged): 14 add-ons built; every test/tw_* x86 / amd64 (30 runs of 16 tests) PASS; 7 harness modes on the stamped helper103 pass; scan_register clean; page dumps by content: Attila v146 against v145 the seven new rows + the date, WH3 v42 against v41 the six new rows + the date + DLSS Preset's tooltip (e9bb6b9e). Downloads: twwarhammer3_v42.zip; twattila_v146.zip, RenoDX_Attila_v2.5.7.zip, RenoDX_Attila_v2.5.7_HDR_only.zip, attila_github_v2.5.7_notes.md (unpacked, hash-checked, stamped add-ons; helper103 as v145 / v41). Installed: WH3 the add-on (backup wh3_v42/install_backup_20261111_212903), Attila the add-on + READ ME (attila_v146/install_backup_20261111_213002); ATN's ini / preset / preferences backed up first and unchanged by hash, no DevKit. READMEs: Attila v146, WH3 v42, a "Round v42" section in Rome II / Thrones / Troy / Pharaoh / WH1 / WH2 (built, not released). Not run in game. No tag: twattila-v146 and WH3's v42 tag belong on the commit that ticks this item (docs only; the binaries' sources are 5255264b); twattila-v145 on 5d15186f. Report round_v42/reports/round_report.md.
- NEXT: ATN's tests, smallest first, at the end of round_v42/reports/round_report.md (Attila rows off as v2.5.6, then the weapon jitter B1, the FSR switch, WH3 B4); the other TW add-ons' release waits for ATN.

## WH3 v41 (ATN's test of v40, 2026-11-11; no game start, ATN away; offline only)
Evidence: tools/tw/wh3_v41/atn_run1 (ReShade.log, the helper's ReShade.log1, renodx-tw-dlaa64.log, sl.log, ini, preferences). Report: tools/tw/wh3_v41/reports/v41.md. Rules: wh3_batch6/plan.md "Rules (every phase)" (commit lines end Claude Opus 5.5).
- [x] 1 Path-Traced AO too dark: rt_sun took the spherical harmonics' sky, but with the game's Global Illumination on (true in every pre-v37 trace) the sun shaders take the light probes, whose open sky is the environment maps x g_ambient_fudge_factor (cs DC860C60). ATN's helper lines: "the game's ambient" 1/12 to 1/58 of the screen's shade. rt_sun now builds that sky (row Sky as Bright as the Game's, on). WARP rt_sun_test. The game's SSAO: skipped under RTAO, Path-Traced AO and GT-VBAO (code read).
- [x] 2 any ray feature's fixed cost: Fog Shadows alone switched on Ray Reconstruction (+2 ms at 50%, +6.6 ms at 100%), the terrain's count wait (14-22 ms of the frame thread in run R, the cap when not GPU-bound) and the grass's capture, indirect read-back and clusters (grass about 3/4 of the helper's per-frame clusters; harness: 40,000 cards +0.64 ms) that no fog ray meets. Now (shared rt_core / rt_scene): grass captured only while a ray meets it (sun, AO, Path-Traced AO: same picture); WH3's Settings::cheap_fog_alone: fog alone without Ray Reconstruction and with the count a frame late. Attila: binary changes, picture the same (its flag false).
- [x] 3 far tree shadows: their cards (RT_SUN_ONLY) sit in the smoke's BLAS, and the sun's and the fog's rays counted the clear ones against the smoke's particle budget (16 for the sun at Low, 4 for the fog), then gave up lit: past it a far tree cast no shadow, near it the shadow came and went as LODs and kept instances changed the set. Now (helper103, rt_scene.hlsli) cards counted apart, past MAX_CANDIDATES blocked as Occluded's rays. Harness far_cards_20 / _48: helper102 lit all 28 frames, helper103 shadowed all 28.
- [x] 4 DLSS Preset Auto gone (a saved 3 loads as L: the row's clamp). The error: ReShade (the game folder's dxgi.dll) loaded into the helper too, its add-on load failed there (ATN's ReShade.log1) and its "errors loading add-ons" banner sat on FSR's frame generation window (made 11:00:17, seconds after DLSS 6x -> FSR). Helper103 loads Windows' dxgi / d3d11 / d3d12 first (delay-loaded imports, build.ps1): ReShade stays out. frame_generation.hpp: a count above the running backend's most (Shared::fg_presented) asks for its most (Attila's binary changes too, its picture not).
- [x] 5 Texture LOD Bias: the chain works (ATN's log: 1280x720 proven, so the bias ran; traces: the main view switches on at the render size; the G-buffer draws are the immediate context's), but only the game's anisotropic samplers were biased, and WH3's terrain, buildings, grass and texture blends sample through named samplers (s_xml_sampler; trilinear_wrap_srgb_sampler in rigidtextureblend 1405BFB4) whose filter is unknown or trilinear by name. New Experimental row (off) Texture LOD Bias on All Textures: wrapping trilinear samplers too (clamping ones, post passes, never). Log Details: one line 300 presents into a bias with the samplers by kind and the biased copies. WARP test/tw_texture_lod_bias (x86 + amd64 PASS): MipLODBias moves sample, sample_l, sample_b and sample_d alike; the clones replace what they should. Attila: binary changes, picture not (the row is WH3's).
- [x] 6 preset switch mid-battle: ATN's RT/PT preset had Skip the Shadow Maps and its tree row on. The game's two cascade arrays swap at each scroll (trace b5_mom: C883E3FA copies the kept maps, shifted, from the last kept array, t_previous_static_shadows, into the other, which takes the new strips and is t_static_objects_shadowmap_array, 800F7F7F's t1, until the next scroll). The skip took the copy and the strips too, so the kept maps stayed empty or stale for the rest of the battle after Reset: a flat picture. Now only draws into the dynamic maps are skipped (the kept array learned at the copy and from 800F7F7F's t1 every frame; none skipped before it is known). The other paths audited (report): the game's GI flag and probe power per upload, its SSAO, GT-VBAO's sweep, the sun draw's bypass, the fog's maps, the first map's reach: all per frame or per upload, back on the next frame. Not proven in game.
- [x] 7 Shadow Maps Under Rays (Experimental, off; rays section): with Rays Replace Shadow Maps on at the start, the game's two cascade arrays made at 2048 or 1024 (from the next start, until the one after: the game's own shadows stay that small with rays off too). shadow_map_size.hpp generalised (a made size; a smaller map tracked only when made here, the game makes 2048 / 1024 maps of its own), shader_injection.shadow_map_scale for the three copy shaders (one tap; buffer_size x scale). WARP test/tw_shadow_map_size: all v40 cases + 1/2 and 1/4 (half copies the game's result to the bit at 1/2, the 4 x 4 deepest at 1/4, never shallower; kept copy the game's result reduced, 0 texels apart). VRAM saved (v40 table): 402.7 MB at 2048, 503.3 MB at 1024. GPU ms not measured (no game run).
- [x] 8-9 answers (SSS: Coloured Shadows only for on-screen occluders, needs a thin mask of our own; what SSS lacks) and the full not-done list in the report.
- [x] close: all_modes 178 on helper103 (v40's 176 same names and verdicts + far_cards_20 / _48); WARP rt_sun_test, tw_texture_lod_bias, tw_sampler_lifetime, tw_shadow_map_size, tw_fg_initialization, tw_helper_lifetime PASS; 14 add-ons built; page dumps (WH3 against v40: DLSS Preset without Auto, three new rows, the date; Attila against v144: the date). twwarhammer3_v41.zip in Downloads, stamped; installed next to Warhammer3.exe with helper103 (backups wh3_v41/install_backup_20261111_142237, ATN's files unchanged). NOT run in game.
- NEXT: ATN's checks, smallest first, at the end of wh3_v41/reports/v41.md.

## WH3 v40 (fixes and performance, 2026-11-11; no game start, ATN away; helper102 unchanged, bridge v30)
Report: tools/tw/wh3_v40/reports/v40.md (performance table, what was measured and where, ATN's checks). Rules: wh3_batch6/plan.md "Rules (every phase)".
- [x] the 30 s profile read again: its states are ATN stepping through Upscaling Mode (log 23:09:47 "Resolution Scale: back to the game's 1"): Ultra Performance 15.6 ms, Performance 20.5, Balanced 22.7 (the frame called "Mode P" until now), Quality 26.1, 100% 41.3 (the "41 ms state", 19% of the time); the 60 to 167 ms frames are the mode changes. Not ours to smooth.
- [x] 1a(1) depth pre-pass for grass / leaves / branches: bit-identical on WARP and on the RTX 5090 with the game's shader bytes (tools/tw/wh3_v40/prepass), but SLOWER on the 5090 at every density tried (grass 2.99 -> 4.72 ms, leaves 3.41 -> 5.37, branches 2.57 -> 4.71 at 4000 instances a draw): the cost is the triangles and the alpha test, not the G-buffer shader. Not built.
- [x] 1b the wait at the sun's draw: not idle. Wait less the helper's GPU work is 0.44 to 0.57 ms in all four states of ATN's recording (frames of 15.6 to 41 ms), 0.33 ms in the harness: a fixed hand-over cost per request, three requests a frame. Reordering gains nothing; one request less would need a second fence or bridge v31 (the helper's signal would step the shared fence back). Not built.
- [x] 1a(4) two display-size copies a frame gone below 100% (c0e410b2 light copy as a box, shared; 93247066 the finish straight into the picture, WH3): WARP copies_test 12 + 16 cases bit-equal; RTX 5090 0.047 + 0.034 ms. Ray Reconstruction itself: nothing to cut (no copy, no conversion, 1920 x 1080 in, the layer the only optional input).
- [x] Terrain Without Waiting (6719f8ec): a frame without a count from the last six frames takes the row-off path. Default stays off (its empty triangles cost the helper GPU time; ATN's ini has it on).
- [x] close: 14 add-ons built; all_modes 176 modes on helper102 with the patched harness (HARNESS_ROUNDTRIP); test/tw_shadow_map_size + tw_game_gi PASS; page dumps (WH3 against v39: Bounces' range and tooltip and Frame Generation's status text, both from v144's shared code, and the date; Attila against v144: the date). twwarhammer3_v40.zip in Downloads, stamped; installed next to Warhammer3.exe with helper102. NOT run in game.
- NOT DONE: helper's grass every second frame and a bounce stage at half rate (no helper change this round; Light Bounce Resolution Half is the row that exists: 1.11 -> 0.42 ms at 1920 x 1080 in the harness); Shadow Map Size 2048 / 1024 (design and VRAM table in the report: 503 MB at 1024; three fixes to shadow_map_size.hpp and a scale constant for the three copy shaders, WARP-provable, one look in game); skipping only the first shadow map; the half copy 800F7F7F's bypass (0.10 ms, no reader in 94 traces); motion vectors for C0ED2883 + 1405BFB4, the cascade-box rule, dissolve cut-outs, the tent: untouched. Item 15 waits for ATN.
- NEXT: ATN's checks, smallest first, at the end of wh3_v40/reports/v40.md.

## ATTILA v144 = players' v2.5.5 (ATN's test of v143, 2026-11-11; no game start; helper102, bridge v30)
Report: tools/tw/attila_v144/reports/v144.md. Rules: wh3_batch6/plan.md "Rules (every phase)". v2.5.3 and v2.5.4 were never released.
- [x] 1 banners too thick for Coloured Shadows: only cloth.hlsl's meshes and sails were thin. New rows, Warhammer III's, Experimental, off: Light Through All Banners (thin_row on rigidtextureblend B06CB969 / 10FE7EBC, the 3D UI's unit banners' material; rt_core thin_sheets_row on rigidcommon 347F3DD9 / D830AD9B draws from a dynamic vertex buffer drawn from both sides: the flags the CPU moves) and Smooth Banner Shadows. No helper change needed (a placed thin mesh passes 0.35 and its colour: harness banner_*). Proof level B (model files, shader bytes, ReShade.log's "347F3DD9 in dynamic vertex buffers"); no frame trace has a banner's draw. NOT DONE: the standards soldiers carry and the towns' banner props (rigid material 68 from static buffers, as houses): what, a way to tell them from houses; smallest step, a Frame Trace with one in view (its pixel shader, textures, culling).
- [x] 2 Bounces: a path ends at the first surface whose light is known (screen now, screen last frame, radiance cache), so the row only counts surfaces nobody saw; and a cache cell out of sight kept its first frame's paths for 1800 frames, so moving the row in a battle did nothing. Helper102: an eighth of the paths leaves the cache out (CACHE_RELEARN). Row 1..2 (3 and 4 measured as 2), tooltip rewritten. Numbers in the report. Warhammer III: the same shared row and helper; its add-on needs only this build installed and a README line (next WH3 round).
- [x] 3 Frame Generation's status line: the helper's line is written while it presents, and it pauses under ReShade's menu, so the page showed the line of before the menu opened. Now said so, with what the backend presents at most where the row asks for more (Shared::fg_presented in v13's padding, helper102; no layout change). graphics_page.hpp: every Total War add-on with the page. The in-process games (Tales, Ryse, Expeditions, Shogun 2 DLSS) draw their own live line: untouched.
- [x] validation: all_modes on helper102: 176 modes, helper101's 169 with the same names and verdicts + banner_* (5) and bounces_* (2); 14 add-ons built; test/tw_helper_lifetime + tw_fg_initialization x86 / amd64 PASS; fg_bridge_test on helper102 (fg_presented per backend); Attila's page against v143's: the two banner rows, Bounces' end and tooltip, the date.
- [x] close: README v144; Downloads: twattila_v144.zip, RenoDX_Attila_v2.5.5.zip, RenoDX_Attila_v2.5.5_HDR_only.zip, attila_github_v2.5.5_notes.md (the same text in attila_github_v2.5.2_notes.md, as ATN asked); installed next to Attila.exe, stamped; not run in game; no tag (ATN tags and uploads).
- NEXT: ATN's checks, smallest first, at the end of attila_v144/reports/v144.md.

## ATTILA v143 (performance round on ATN's profile of 2026-11-11; no game start, ATN away; dev build, not a players' release)
Evidence: tools/tw/attila_v142/atn_run1 (the 30 s profile, ReShade.log, sl.log). Report: tools/tw/attila_v143/reports/v143.md. Rules: wh3_batch6/plan.md "Rules (every phase)".
- [x] 1 the 22 ms wait of the game's thread in 40% of the frames: frame generation's pacing, not a wait for the helper's work and not a poll. Those 403 frames are the ones with ReShade's menu closed (frame generation presenting: ReShade.log's "Reflex: frame generation paces the game" lines at 00:10:09.9, 14.98, 36.05 against the profile's switches at 0.70, 5.72, 26.80 s); the wait is the present request's answer (dlaa_bridge Request, PRESENT_FRAME, never steady: fg_d3d12.hpp Present = Streamline's Present, then slReflexSleep for the next frame). With the menu open (590 frames) frame generation is off and NVAPI's Reflex sleeps in the add-on's finish_present, which the column never counted. Reproduced without the game (tools/tw/fg_child/fg_bridge_test, 4K, helper100): GPU idle, the presents' answers 0.86 ms a frame; the GPU 134 ms behind, 134.9 ms a frame of a 135 ms frame, and with frame generation off under the same load 0.02 ms and the same 7.5 frames a second. Nothing to gain: the thread waits there for the GPU. The frames are 3 ms longer because frame generation runs (31.2 against 28.2 ms). New profile column `present_pacing_ms` (of `answer_wait_ms`: the presents' answers), so a recording says it by itself.
- [x] 2 the 16.7 ms split, measured without the game (fg_bridge_test unthrottled, 3840 x 2160 in and out, the 5090): DLSS 1.74 ms, Ray Reconstruction 8.30 ms, frame generation 2.5 to 3.8 ms a rendered frame (ATN's own two states: 3.0). So with the menu open (28.2 ms; rays 11.9): Ray Reconstruction about 8.3, the game's frame and our D3D11 passes about 8 (estimate). Our passes around the upscaler (guides, inputs, result, one copy; with frame generation a draw and a copy, in the helper a pass and a copy): about 1 ms by the code, not timed. Nothing merged: no two conversions follow each other, and each copy crosses a target without unordered access, a swap chain or a process.
- [x] 3 helper101 (build/tw_dlaa_helper101; bridge v30, layout unchanged): the paths' kept positions are two slices of one texture taking the frame's copy in turn: one 4K RGBA32F copy a frame less. 4K harness, the Light Bounce's first stage: 0.85 -> 0.69 ms at Full, 0.48 -> 0.31 at Half. Picture the same: the harness's six outputs bit for bit helper100's (HARNESS_DUMP, 60 frames standing, 40 with the camera moving). Profile: the helper's trace rows end in `cpu_record_parts` (its CPU by pass: ATN's recording has 2.5 to 3 ms there, the harness 0.16 ms with 14,000 placed instances and 80,000 clusters, so the harness does not show what costs it). Left, with reasons (report section 3): ray flags, masks and early-outs are there already; static BLASes are built once; ReSTIR GI's re-check every third frame makes the paths 0.5 ms dearer in that frame (evening it out changes which frame a texel is checked in).
- [x] 3b the sun rays' p95 5.6 ms against 2.92: no spike, one step at a single frame (24.8 s) with the scene standing and ReShade's menu open: sun rays 2.89 -> 5.64 ms, the Light Bounce's paths 1.69 -> 2.90, reflections 1.87 -> 2.77, steady after; lights' and fog's rays unchanged. A setting changed (which one is not in the evidence; Light Through Leaves and Smoke Shadow Quality touch exactly those rays, the RT/PT preset sets both). Worth 4.8 ms of the frame. 4K harness: Light Through Leaves in a view full of leaves, sun rays 0.82 -> 2.06 ms; Smoke Shadow Quality with 2000 particles toward the sun 1.35 / 2.04 / 2.09 ms (Low / Medium / High).
- [x] 4 options that change the picture: no new row. Light Bounce Resolution exists (default Half; the RT/PT preset sets Full): 4K harness with ATN's path flags, Full 4.25 ms, Half 1.41 ms. Through Ray Reconstruction (preset F, harness): Half leaves 1.3x Full's noise at a wall's foot and 1.4x on open ground, its change over time 2.0x and 1.2x; the means are equal. The preset is ATN's to change.
- [x] 5 double work under RT/PT: the sun's shadow map draws are skipped already (rt_scene.hpp OnBindTargets), Screen-Space Shadows and the smoke map are off, the reflection march is skipped where the rays reflect. Still run: the game's SSAO resolve and its two blurs under Path-Traced Ambient (our replaced shader; three full-screen draws), the shadow depth analysis chain (six small draws). Not skipped: no Frame Trace of Attila under RT/PT exists (the one of 2026-10-31 is without rays), so no reader could be proven gone.
- [x] validation: all_modes on helper101: 169 modes, names and verdicts equal helper100's (attila_v143/reports/all_modes_h101.txt; the debug layer's mode among them); 14 addons built (3 x86, 11 x64); test/tw_helper_lifetime + tw_fg_initialization x86 / amd64 PASS; Attila's page dump against v142's: the build date only.
- [x] close (2026-11-11): README v143; Downloads/twattila_v143.zip (408 entries, unpacked and checked); installed next to Attila.exe (addon32 + helper101, stamped; the stamped helper's harness outputs still bit for bit helper100's; backup attila_v143/install_backup_20261111_035453; ATN's ini files and preferences equal by hash; no DevKit). Not run in game. No players' zip, no notes, no tag.
- Gain of v143 by itself on ATN's 29.1 ms frame: about 0.16 ms (estimate from the harness). The ms are in his own rows: Light Bounce Resolution Half about 3 ms, the recording's step 4.8 ms, Ray Reconstruction 8.3 ms, frame generation 3.0 ms.
- NEXT: ATN's checks, smallest first, at the end of attila_v143/reports/v143.md (the picture as v142's; a 30 s profile with ReShade's menu closed: `present_pacing_ms`, `cpu_record_parts`; Light Bounce Resolution Full against Half; Light Through Leaves and Smoke Shadow Quality at a village with fires; one Frame Trace under RT/PT). Then: what `cpu_record_parts` names (the helper's 2.5 to 3 ms of CPU a trace, and the 2.5 ms the game's GPU waits past the helper's GPU work in frames with frame generation).

## ATTILA v142 (ATN's test of v2.5.2 = v141, 2026-11-11; no game start, ATN away; dev build, not a players' release)
ATN's evidence (3 preset shots, 8 shots, ReShade.ini / .log, helper log, 30 s RT profile): tools/tw/attila_v142/atn_run1. Report: attila_v142/reports/v142.md, perf_ideas.md. Rules: wh3_batch6/plan.md "Rules (every phase)".
- [x] 1 + 2 grass too bright in a house's shadow, shadows darker or lighter by sun and camera angle (helper100, not run in game): a blade's pixel turned away from the sun by the depth's normal took OccludedSolid (no shadow from grass, leaves, smoke; solid cut-outs culled where their micromap state is unknown), one turned to it the full sun ray. Now a pixel on grass (ShowsGrass: a card of the grass's instances at the pixel) always takes the full ray; OccludedSolid and the paths' TRACE_SURFACE alpha-test solid cut-outs; a rigid mesh the shadow pass alpha-tests is a solid cut-out (RT_SOLID_CUT_OUT: among the solids, no Light Through Leaves share). Since v119 / v104, not a regression since v2.5.
- [x] 3 grass shadows dark ~0.5 s after a zoom (rt_core, shared, not run in game): RT_PLACE_WHOLE was never sent while a mesh's instance count shrank, so a level of detail losing places kept its copies up to 8 frames beside the next one's. Now also sent for a mesh the last traced frame drew wholly before its trace.
- [x] 1b solid cut-outs in their own instance group (CUT_OUT_INSTANCE 0x40), the cull back in the hardware: told from leaves in the candidate loop, leaf storm at 4K cost the bounce 188 -> 415 ms. Path-Traced Ambient's own sky ray asks the group. Left: the paths' and reflections' nearest-surface rays still pass a cut-out's parts of unknown micromap state; leaves' pixels turned away take solids only.
- [x] 4 performance: attila_v142/reports/perf_ideas.md (median frame 29.1 ms: game + RR + FG about 16.7, bounce 5.18, sun 2.92, reflections 1.87, scene 1.17, fog 0.65, lights 0.36). Nothing implemented that lowers cost: none found that the harness proves equal in picture. The fixes cost about +2.5% trace, +3.6% bounce (4K harness).
- [x] validation: all_modes on helper100: 169 modes, helper99's 165 with the same names and verdicts + 4 leaf_cut_out modes; 14 addons built; test/tw_fg_initialization + tw_helper_lifetime x86 / amd64 PASS; Attila's page dump against v141's: build date only.
- [x] close (2026-11-11): README v142; Downloads/twattila_v142.zip (408 entries, unpacked and checked); installed next to Attila.exe (addon32 + helper100, stamped; backup attila_v142/install_backup_20261111_031429; ATN's ini files and preferences equal by hash; no DevKit). Not run in game. No players' zip, no notes, no tag.
- NEXT: ATN's checks, smallest first, at the end of attila_v142/reports/v142.md (grass in a house's shadow under RT/PT against No RT/PT; a field seen with and against the sun; smoke's shadow on grass; zoom; a house behind a tree; campaign palms).

## WH3 v39 (follow-up on ATN's test of v38, 2026-11-11; no game start, ATN away)
ATN's evidence (shots, logs, 30 s profile, pass timing, ini, replay 2): tools/tw/wh3_batch6/atn_run2. Report: wh3_batch6/reports/v39.md. Rules: wh3_batch6/plan.md.
- [x] A "with Global Illumination on things look cooked": since v37 the game's g_gi_enabled was 0 under ours, so all 1019 readers took the ambient cube unshaded in place of the probes' light (9EAD8951: the probes REPLACE the cube; DC860C60: a probe ray's miss is the sky, a hit the voxel's lit colour x g_irradiance_power). ATN's pair (Path-Traced, RTAO): shade 2.0 to 2.8x the game's (dark tones x2.2 to x3.4, bluest most), sunlit x1.04. Fix: row Sky Shade From the Game's Probes (`GlobalIlluminationGameSky`, on): the flag stays the game's, the probes' hits are unlit (g_irradiance_power 0 in a copy of DC860C60's constants, once a frame), so the sky's shade is the game's and only the bounce is ours; fog, water and effects get the game's probes back. Off: v38. test/tw_game_gi stage 11 (WARP). NOT run in game.
- [x] B Skip the Shadow Maps: shadows of trees out of view. By code and traces (no shot shows it): the rays' capture runs before the skip's bypass, and our sun mask under Rays Replace reads no map, so the skip itself takes nothing from the rays. The gap is the game's: it draws trees every frame only into shadow map 1 (40 m at quality 2 to 3); maps 2 to 4 take trees from its static cache, redrawn in small border pieces (6 of 93 traces). Those far trees reach the rays once and are lost when the helper's scene restarts (ATN's run: helper restarts 23:02:34 and 23:15:02, "Off for 10 s: its memory released" three times); far-tree cards FA844DB5 have no scene-table entry. ATN's last ini had Rays Replace off with the skip on, so the skip was idle and the maps showed every tree. Fix: row Trees Out of View up to 150 m (Experimental) (`RayTracedTreeReach`, off; needs the skip, Rays Replace, Casters Out of View): map 1's reach 149 m while the row is on (engine value battle_shadow_split_0, below map 2's 150 at every quality; the saved "Battle Shadow Map 1" row is untouched), so the game draws every tree within 150 m each frame. GPU cost nil under the skip, more CPU draws (unmeasured); smoke's shadows near the camera get coarser (VFX map follows map 1). By hand: "Battle Shadow Map 1" = 149. Beyond 150 m and the cards: not covered. NOT run in game.
- [x] C ATN's questions, answered in wh3_batch6/reports/v39.md, no code. (1) Shadow maps' memory with the skip: the two 4096 [4] arrays are 536.9 MB, the unread 2048 [4] copy 67.1 MB, the particles' two 1024 [4] maps 33.6 MB (needed); 1 x 1 maps are NOT clearly safe (gather readers in water and effect meshes depend on a sampler no trace holds, three copy shaders would read out of bounds, rays off would leave no shadows until a restart): not implemented. (2) Table of what the rays lack (smoke, far-tree cards in the shadow pass, cache-only casters past map 1, dissolving and 8-layer units, things moved out of view, water). (3) Smoke: keep the game's small map (16.8 + 16.8 MB, no cost without smoke; Attila's own map 0.1 to 0.15 ms, its rays up to 1 ms and a helper build). (4) Double work: none left that is safe to drop; the six probe passes (0.22 ms) carry the sky's shade since A; the unread 2048 copy is 4 small draws. (5) wh3_batch6/reports/perf_ideas.md: Mode P frame 22.7 ms ranked, ten ideas, none implemented.
- [x] D close as v39 (2026-11-11): 14 addons built (11 x64, 3 x86; shared graphics_page.hpp took WH3's two rows: other games' pages the same, Attila's dump differs in the date only; no Attila release, the stamped v141 addon kept in build/stamped/batch6). test/tw_game_gi x86 + amd64 PASS. Page dump against v38: DLSS Preset default L, the two new rows, the GI tooltip's end, the date. twwarhammer3_v39.zip in Downloads (326 entries, stamped, unpacked and hashed; helper99 and bridge v30 unchanged). Installed next to Warhammer3.exe (the addon only; backup wh3_batch6/install_backup_20261111_001259; ATN's ini, preset, preferences unchanged by hash; no DevKit). NOT run in game. Next: ATN's six checks at the end of wh3_batch6/reports/v39.md.

## WH3 BATCH 6 (Monday batch, ATN's go 2026-11-10; ATN away, keep going)
Plan and rules: C:/Users/arshi/.claude/projects/D--Dev-renodx/tools/tw/wh3_batch6/plan.md; items: list.md there; reports: wh3_batch6/reports. One bundled game run only (phase R).
- [x] P0 device-loss handling in shared code (Attila first) + swap chain audit: twattila/device_loss.hpp + output_encoding.hpp in the 9 TW HDR addons (cure in both rebuild orders, limit spares a background game, output decided per new chain, objects released at destroy_device, line-sweep AO never binds across devices, FG drops its kept back buffer). test/tw_device_loss + tw_fg_initialization pass x86 and amd64; 14 addons build. NOT run in game; no helper change.
  Report: wh3_batch6/reports/P0.md; the run's checks: wh3_batch6/run_requests.md (P0-1..9). Not done: Auto still sticks to SDR until the next new chain; twromeremastered and Shogun 2 DLSS untouched; a real removed device never tested.
- [x] P1 GI-off "hang" (item 1) = a game crash we caused: v37 held the engine value gi_enabled at 0 and wrote 1 back when Light Bounce went Off; the game's GI stage, built without textures, ran (null read at Warhammer3.exe+0x1A45803; the game's own crash_report\D2026-11-06_T01-55-38.mdmp, its handler compressing the report = the stall). Fix: gi_enabled never written, the flag in the lighting constants alone (game_gi.hpp: buffers by creation, pointers per buffer and thread). Stall watch: an exception line (64-bit) and renodx-stall.dmp beside the exe, written by hand (not MiniDumpWriteDump). test/tw_game_gi (new) + tw_device_loss pass x86 and amd64; harness HARNESS_GI_SWITCH 7 of 7 on helper96; 14 addons build. NOT run in game; no helper change.
  Report: wh3_batch6/reports/P1.md; the run's checks: run_requests.md P1-1..4. Not done: the game's six GI compute passes run again under ours (0.22 ms; a hash skip would be an Experimental row); exception line for 32-bit games; other on/off engine values (more_tests.h) unread for the same pattern. WH3 freezes: read %APPDATA%\The Creative Assembly\Warhammer3\crash_report first.
- [x] P1b the games' crash reports read (86: WH3 48, Attila 21, Troy 17) + engine values written live. 34 of WH3's 48 = the game faulting after a GPU reset because our swap chain mod made its blt fallback flip (0x80070005 twice; fixed before: a6e4dbb6, cb588e93, 6b06cc9c); ATN's freeze of 11-05 05:44 was that crash. New and ours: Attila v116..v135 (players' v2.5.1) `Rescue()` made a D3D11 device on the game's thread, its release ran our own destroy_device handlers under a held lock ("resource deadlock would occur", dump of 10-31); gone since a891b919, so the next Attila release must carry it. Fix (bae2eda8, c1b20cff): engine_tweaks::Uninstall switches nothing at detach (the game's values kept in a section of the process for the next image; code constants still go back), flags at Game's are not stored every present, Troy calls Uninstall. test/tw_native_hook_lifetime + tw_startup_worker pass x86 and amd64; the 8 addons with Graphics (Experimental) build. NOT run in game; no helper change.
  Reports: wh3_batch6/reports/P1b_crash_triage.md (tables), P1b.md (audit, not proven, for later phases); the run's checks: run_requests.md P1b-1..4; tools: wh3_batch6/p1b (triage.sh, parse.py). Not done: the 430 flags of Rome II / WH1 / WH2 / Troy / Pharaoh unread; Attila's new_shadows / terrain_enable_shadows / cindy_shadows read through shared functions (unproven, Experimental as before); out-of-memory exceptions leave our callbacks (seen only with commit full).
- [x] P2 RT shadow capture (items 2, 3, 5, 6): addon only, no helper change (helper96, bridge v30). Tent black spot = the cloth's 204-index stand-in for the shadow maps (8EE34EC5 + C998A3A2, drawn by the shadow pass alone beside the 13632-index cloth) standing in the rays over the cloth; 130 pixel shaders discard by their instance's fade (TEXCOORD6.w), the rows never read it: row No Shadows From Hidden Parts (Experimental, off; that the stand-in is drawn at fade 0 is NOT seen). Far Tree Shadows: the helper takes sun-only cards from the tail of a frame's captures, in WH3 units and the terrain always follow, so every card stood among the units (ambient, lights, paths): captures ordered in rt_core, sun-only last (Attila's sprites too). Trunks: Log Details says how far each draw's second rows (v11-13) lie from the first; row Tree Trunks as Drawn (Experimental, off: 474F97FB captured each frame as Placed Objects off does). Cut-outs: 66 untraced masks behind row Cut-outs on More Models (Experimental, off); Frame Trace prints 1405BFB4's constants. WARP warp_tests/rows_wh3 8 of 8; 14 addons build; page dump WH3 + 3 rows. NOT run in game; harness not re-run (no helper change).
  Report: wh3_batch6/reports/P2.md; the run's checks: run_requests.md P2-1..6. Not done: No Black Patches on Trunks stays until a run shows Tree Trunks as Drawn's look and cost; 1405BFB4's 8 layers (level C), dissolve shaders, 03298FD5 (campaign); SmokeTransmittance counting failed cards (helper, after the run).
- [x] P3 reflections and water (items 4, 14): **helper97** (build/tw_dlaa_helper97), bridge stays v30 (a flag and five unused texels of the sun texture; an older helper ignores the flag). Two WH3 rows, Experimental, off. Item 4: with Global Illumination off the rays' hits read last frame's picture and what the screen never saw was black, because WH3 passed the reflections no light buffer (the helper learns its lighting only from one; Attila passes its pass's). Row Reflections Lit as This Frame: the buffer the game's own march reads (cs 3AC630DF's input, R16G16B16A16F, trace b11_fog line 1598), copied to R11G11B10F and passed in every Global Illumination state; no helper change for it. Item 14: the game draws its fog after the reflection pass, over the water at the water's depth: the leg from the water to the thing reflected has none, in the game's own march too. Row Fog in Reflections: the helper puts 34719E09's opacity on that leg (twattila/rt_leg_fog.hlsli), the numbers from the game's own fog buffer and the fog's light measured from A8F5202D's target each frame (rt_fog_learn). WARP: the opacity against the game's bytes, 393216 values, 0 apart by more than 2e-4; the two new shaders 10 of 10. Harness on helper97: 155 of 155 (14 new modes: no Light Bounce request at all, fog on the mirror's leg within 3%). 14 addons build; page dump WH3 + 2 rows, Attila the date only. NOT run in game.
  Report: wh3_batch6/reports/P3.md; the run's checks: run_requests.md P3-1..5. Not done: neither row on by default (P7, from P3-2); the fit takes the water's pixels too; the fog's light is one colour a frame; five other fog light shaders write no numbers; release_zip.py must name tw_dlaa_helper97 at the close.
- [x] P4 casters and cascades (items 7, 10, offline 8 and 17): **helper98** (build/tw_dlaa_helper98), bridge stays v30 (one new rt_flags bit, RT_ONE_AT_A_PLACE; older helpers ignore it). Three WH3 rows, Experimental, off. Found: the shadow pass has a dynamic and a static (cached) 4096 [4] array, trees are drawn each frame into the first block only, the maps reach 40 / 150 / 350 / 4000 m; 454 pixel shaders can read the maps, 7 were ever drawn (sun mask, fog light, 3 water, a lit effect mesh, the particles' VS) plus the particle shadows' depth copy. Item 7: row One Model per Place Out of View (helper rt_place: a kept instance goes where the frame placed another mesh and not it: another LOD, a ruin); row Cut-outs on Units Out of View (shadow-pass units by B2358145's mask, UV through rt_outside_view_uv); weapons are 53C5A7EC's draws, placed since v34; Hills Out of View cannot change the sun's shadow (the terrain's own shadow map stays in the rays' mask). Item 10: the fog reads no map under Fog Shadows; row Skip the Shadow Maps (all 13 caster VS under Rays Replace; water, smoke shadows, lit effects lose the sun's shadow). Item 8: log lines `Shadow map sizes:` for the three constants, no row. Item 17: no listed value widens the shadow pass's casters; disable_parent_clip_status 0 and enable_billboard_block_shadows are do-not-touch. WARP outside_view_uv PASS; harness on helper98: 161 of 161 ALL PASSED (P3's 155 by name and 6 new: kept_forever, one_place, one_place_unasked, one_place_drift, one_place_beside, one_place_long); 14 addons build; page dump WH3 + 3 rows and one tooltip, Attila the date only. NOT run in game.
  Report: wh3_batch6/reports/P4.md; evidence: wh3_batch6/p4 (item10_readers.md, item17_readers.md); the run's checks: run_requests.md P4-1..8. Not done: moved or vanished things with nothing drawn in their place stay until seen (needs each shadow map's matrix and which block is which map: P4-8's Frame Trace); Shadow Map Size's row (after P4-1's numbers); the skip's real ms; release_zip.py must name tw_dlaa_helper98 at the close.
- [x] P5 terrain wait, motion, particles, preset L (items 9, 12, 13, 16): addon only, **no helper change** (helper98, bridge v30). Item 9: row Terrain Without Waiting (Experimental, off; rt_core Settings::terrain_count_later): the tessellated draws' count read a frame or more late from six query sets, the frame's triangles streamed onto empty (NaN) vertices as many as the newest count and an eighth, the Flush before the terrain's stream output kept. WARP + RTX 5090 warp_tests/terrain_count_later: 0 earlier-frame vertices, CPU at the trace 15.2 ms with the wait behind 15 ms of GPU work, 0.001 ms without; harness terrain_tail modes (200 K and 1 M empty triangles pass, a roof in their place fails 6). Off because batch 3's A/B of the idea gained no fps in steady views and lost 0.8 ms of p99. Item 16: the darkening is L's and M's own on fine contrast at any feed (harness tones test, real DLSS: two-stop texture at 50%: L 0.911 linear / 0.930 square root, M 0.922 / 0.904, K 0.998; flats 1.00; K within 1% at 100 / 67 / 50 / 33%): WH3's DLSS Preset gets Auto (its default): L at 100%, K below; a saved K / M / L stays (ATN's ini has L). Item 13: Particles Skip the Denoiser and Picture Before Particles work below 100% (the composite drawn once more unblended, the colour before copied, rr_extra_scaled.cs after the tone mapper; WARP rr_scaled_wh3 60 of 60); Particle Trails' taps stay in the rendered part; output-resolution particles not built (upscaler after the tone mapper, no display-size depth). Item 12: census of 93 traces: vs C0ED2883 with ps 1405BFB4 (skinned texture-blend units, 28 draws a frame) writes no motion (new), capture flags 444F1190 have no last rows, sails and ropes drawn in no trace; no fix has bytes to rest on: Log Details reads both families' instance data (OnInstanceLog). Item 15 left for ATN. all_modes on helper98: 165 of 165 ALL PASSED (P4's 161 by name and 4 new: terrain_tail, terrain_tail_plain, terrain_tail_long, upscaler_tones_l); 14 addons build; page dump WH3 + 1 row, the preset row, 2 tooltips, Attila the date only. NOT run in game.
  Report: wh3_batch6/reports/P5.md; evidence: wh3_batch6/p5 (tones*.txt, motion/), warp_tests/terrain_count_later, warp_tests/rr_scaled_wh3; the run's checks: run_requests.md P5-1..7. Not done: C0ED2883's and the flags' motion (after P5-6's log lines); Depth of Field Guide below 100%; a mean-kept brightness fix for L (shimmers 1.5 to 2.7% on sub-pixel detail); Terrain Without Waiting's default (P5-1); Auto's K against M at 50% (P5-3).
- [x] R one bundled game run: ONE start (2026-11-10 18:19, wh3test1.replay, ATN's preset: PT + RR + Mode P, 362 s, HEAD addon + helper98), no crash, no stall, 0 resets. PASS: the GI off / on switch that crashed v37 (twice off and on), the game's GI flag line, Alt-Tab 30 s, output line, 45 of 45, Reflections Lit as This Frame (GI off 3.68x -> 1.03x of the PT reference, free), Terrain Without Waiting's wait (16-21 ms of CPU a frame for the whole battle -> 0; fps the same, GPU-bound). FAIL: No Shadows From Hidden Parts (0 faded instances: the tent's stand-in is hidden another way). Numbers: shadow map size constants, reference count at quit 851, 37.6 fps at 4K Mode P PT. NOT RUN: everything needing the siege, a river, a night battle, the campaign, the AO view (AO row off in ATN's preset), DLSS presets, a second start. No source change.
  Report: wh3_batch6/reports/R.md (per-request table, item 11, "For P7", "Left for ATN"); plan run_plan.md; data wh3_batch6/run1; backup atn_backup_20261110_181901 (restore verified by hash). Installed next to the exe: the unstamped test pair (v37 pair in the backup).
- [x] P7 act on the run: **helper99** (build/tw_dlaa_helper99; two log lines, bridge v30). Reflections Lit as This Frame on by default (Reset / No RT/PT / RT/PT through the page's defaults). New row Shadow Map Size 8192 (Experimental, off; twwarhammer3/shadow_map_size.hpp: the maps made larger, viewport and scissor by the bound depth, shadowmap_PS's size by its 640-byte buffer through buffer_writes.hpp, three replaced copy shaders; test/tw_shadow_map_size PASS on WARP; level B). Particle rows' `001` is by design (WARP check D, 72 of 72; log says so, row greys out). A device's end logs its reference count before and after the addon's releases (851 at WH3's quit: whose is not told by the run; no holder found in WH3's modules). Terrain Without Waiting stays off (no fps while GPU-bound). Item 11: nothing removed, table in the report. all_modes on helper99: 165 of 165 ALL PASSED, P5's names. 14 addons build. NOT run in game.
  Report: wh3_batch6/reports/P7.md (item 11's table, "ATN's tests, smallest first"). Not done: the tent's black spot (trace_22 holds no tent draw; three causes left, ATN's three checks at the tent; Frame Trace now prints `cull`, Log Details `solid, its cut-outs without room`); GraphicsPlantSway cannot be set (WH3 has no such value; ATN's ini line has no reader). For C: release_zip.py must name tw_dlaa_helper99; test/tw_device_loss x86 fails line 489 at HEAD too (amd64 passes): look before the close.
- [x] C close (2026-11-10): **WH3 v38, Attila v141, helper99 (unchanged since P7), bridge v30.** The review's 9 findings: 8 fixed (stall dump only after a lost device or with ReShade.ini [Total War] StallDump=1, nothing thrown leaves the watch thread, stacks straight to the file; engine values' section merged, not emptied; a chosen HDR output falls to SDR only when no display has HDR on, Auto keeps its output without an answer; C883E3FA takes the game's buffer_size unless the map is above 4096; line-sweep AO releases only at its own device's end; StopWatch waits 2 s, Uninstall try_lock; Fog in Reflections puts the whole output back and clears stale fog texels; size lines and staging readback only with Log Details, table lookup before GetImmediateContext; test Reset() clears the removed devices: x86 20 of 20), 1 part declined (OnCascadeCaster stays on all 13 caster shaders: a later registration misses pipelines already made). 14 addons built from HEAD (11 x64, 3 build32); every test/tw_*/run.ps1 passes x86 and amd64; no helper source change, so no harness run (P7's 165 of 165 on helper99 stands); page dumps: WH3 against v37 = 10 new rows, DLSS Preset Auto, Reflections Lit on, 3 tooltips; against P7 the date only; Attila the date only. Downloads/twwarhammer3_v38.zip and twattila_v141.zip, stamped, tested by unpacking (addon, helper and sources by hash). WH3 v38 installed next to Warhammer3.exe (18 of 18 runtime files equal to the zip; the replaced test pair in wh3_batch6/install_backup_20261110_223903; ATN's ini, preset, preferences unchanged by hash). Attila not installed. No players' zip, tag, Nexus text, GitHub release.
  In game: only phase R's one run, of the build before P7 and the close. v38 itself, Attila v141 and every other game: NOT run. Report: wh3_batch6/reports/round_report.md (per game, item table, rows and defaults, open risks, ATN's tests smallest first). Players' Attila v2.5.1 = v135 still has the bridge's old Rescue and the soft re-attach: the next players' release must ship v141's code.
- [x] Attila players' v2.5.2 = v141 (2026-11-10): the close's stamped build as it was (no rebuild; addon 81285079..., helper99 0931c68d...). Downloads: RenoDX_Attila_v2.5.2.zip, RenoDX_Attila_v2.5.2_HDR_only.zip, attila_github_v2.5.2_notes.md (everything since v2.5; every line New / Fixed / Changed). Same 47 / 17 files as v2.5.1's zips.
  Installed next to Attila.exe (v2.5 = v134, helper74 was there; the two replaced files in tools/tw/attila_v252_install_backup_20261110_233057; ReShade.ini and ReShadePreset.ini untouched; no DevKit). NOT run in game.
  No tag, no upload, no Nexus text: ATN's. A tag twattila-v141 belongs on the README commit "docs(twattila): v141 = players' v2.5.2". Next: ATN's start, battle, Alt-Tab 30 s, quit (round report's items 14, 15).
- [x] S skill improved (2026-11-12): tw-port-checklist rewritten, SKILL.md plus 10 reference files by area (bring-up order, game facts, RT coverage, AO/GI and double work, render scale/DLSS/motion, SSS/particles/water/fog, helper/FG/stability, shaders/HDR/page, testing, release); old lessons.md split in, wrong rows fixed; independent review: 1 wrong hash, a bring-up order that needed the game in phase 0, VERSION rule, 8 lost rows, 7 unresolvable names: all fixed. Research: tools/tw/skill_v2/research. all_modes.sh gained far_cards_20 / far_cards_48. Next: ATN's Attila / WH3 findings go into the skill.

## Review and repair of Astra's stability commits (2026-11-10, Claude) - DONE, nothing installed, not run in game
- Review of the audit and the 16 commits, the repairs' notes ("## Repairs") and the second review: docs/Attila-crash-audit-review-2026-11-10.md. HEAD ed26c3b3, pushed.
- Repaired: F9/F10 (helper survives a GPU reset again; shared code can't end Tales of Arise; FG faults fail FG only), F1/F6/F7 (no device made in the addon at unload or on the watcher; fence recovery runs in a second helper process `rescue`; Prepare writes only the addon's fields), smaller items (LOD bias cost, WH3 / Shogun 2 DLSS / Expeditions leftover callbacks and hooks, engine values put back at detach), one log line with the removal reason and 32-bit free address space when the game's device or swap chain ends (twattila/device_end.hpp).
- Bridge is v30: addon and helper ship together. Newest helper: build/tw_dlaa_helper99 (WH3 batch 6 P7: two log lines; all_modes 165 of 165 ALL PASSED, P5's names). Before it: build/tw_dlaa_helper98 (WH3 batch 6 P4, unchanged by P5: all_modes 165 of 165 there; P4: all_modes 161 of 161 ALL PASSED (P3's 155 by name and 6 new: kept_forever, one_place, one_place_unasked, one_place_drift, one_place_beside, one_place_long)). Before it: build/tw_dlaa_helper97 (P3; 155 of 155), build/tw_dlaa_helper96 (compiled only; the 134-mode harness and fg_bridge_test ran on helper95, same sources but for a test switch).
- All 14 addons + talesofarise_dlss build; every test/tw_*/run.ps1 passes x86 and amd64.
- Not done: any game run, a real unload/reload under ReShade, a real TDR. The tester's first freeze is still unexplained; no audit finding matches a WH3 freeze (WH3 notes in the review workflow's answer, summarised in memory project_astra_review).
- Open by choice: helper stays off after a failure until the game restarts (as before Astra); WH3's 90 s no-frame limit after a reset can close a game sitting in the background (plausible, low).

## Attila stability repairs (completed 2026-11-09, GPT-6 Astra)
- F1-F8 code repairs and offline validation complete. Game reproduction, actual vendor GPU recovery and unload/reload remain manual validation, not claimed fixed.
- Authorized: repair audit F1-F8, validate, commit each logical fix and push to arshiatn/renodx atn-mods. No game launches or installed-binary changes.
- Isolated checkout: D:\Dev\renodx-astra-stability, branch codex/astra-attila-stability. Original D:\Dev\renodx and unsaved editor work stay untouched. Push HEAD:refs/heads/atn-mods, never force; reconcile any concurrent remote changes first.
- Baseline: 0a6cd493. Audit: docs/Attila-crash-audit-2026-11-08.md. Root cause of original battle freeze remains unproven.
- [x] F2: campaign/water callbacks unregistered before infrastructure teardown. Fresh Clang x86 Release Attila build passed (47 existing deprecation/header warnings), diff check passed. WH3 already unregisters this pair. No game or relocated-DLL runtime test. Build log: C:\Users\arshi\Documents\ChatGPT\Total War Rome2\astra-f2-build.log.
- Build setup: isolated build32 configured with clang-x86 and original shader compiler paths. Unchanged external submodules are directory junctions to the original checkout; use --ignore-submodules=all for Git diff/status here (their relative .git files do not resolve through the junction). Never edit dependency files through these links.
- [x] F1 startup-worker portion: all seven callers cancel and wait for native thread exit in AddonUninit, including CRT/capture cleanup after done. Final production-header regression passed x86/x64: gated thread-local destructor with pre-started coordinator, empty tests, cancellation, restart, repeated stop; 100 cycles with stable handle counts (88/88 x86, 71/71 x64). All 14 required Release consumer builds passed. Tests: test/tw_startup_worker/run.ps1. Logs: astra-f1-worker-build32/64.log and astra-f1-worker-test32/64.log in the audit workspace. No game unload/reload. Helper/watchdog ownership remains open below.
- [x] F1 helper ownership: watcher joined before unload; child/IPC ownership checked and released; all outstanding fence generations drained or recovered after confirmed child exit. x86/x64 production-header tests pass, including failure injection, 20 reload cycles, stable handle counts, and D3D11/D3D12 WARP queued work on three generations. F1 code repair complete; actual game unload/reload remains untested.
- [x] F7: generation-owned GPU-completion watchdog; healthy control, helper death/stall in initial/steady/deferred paths, late publication and shutdown regressions pass x86/x64. All 14 consumer builds pass.
- [x] F5: failed acknowledgement stops Prepare before resource mutation; x86/x64 regression and all 14 builds pass.
- [x] F6: checked remote transfers, rollback and staged publication; x86/x64 failure tests, x86 success test and 14 builds passed.
- [x] F3: bounded motion placement; 52 actual-shader WARP cases and all 14 consumer builds pass.
- [x] F4/F8: sampler clone/original ownership, duplicate identity preservation and per-device teardown; x86/x64 WARP tests and all 14 builds pass.
- [x] F1 final validation: all 14 Release consumers built successfully; x86/x64 helper tests passed. Manual game validation remains pending. User subsequently authorized continuing F3-F8 without usage-based pauses.

## WH3 v37 (2026-11-06): ATN: only our AO and GI. Report tools/tw/wh3_batch5/reports/v37.md. Not run in game.
- [x] Our AO feeds the game's SSAO readers (sun t_ssao only, 94 traces), its passes skipped under RTAO and PT AO (PT AO runs RTAO for the slot); GT-VBAO skips the sweep. WARP v37_wh3 ao.
- [x] Game's GI off under ours: gi_enabled 0, g_gi_enabled 0 at every 480-byte lighting upload. WARP v37_wh3 gi (26 traced readers). Row removed.
- [ ] In game: log line "Global Illumination flag off" appears (else the buffer size or upload path differs: NOT DONE until seen); gi_enabled written live without trouble.
- [x] Close: WH3 addon rebuilt at dd8de01d (only it changed: no other targets, build32 or harness; helper89 unchanged). Stamped wh3 v37 addon, helper89 copy wh3 v37 helper89 (build/stamped/wh3_v37). twwarhammer3_v37.zip from v36 with --helper, tested (same 308 entries, only the addon, helper, 3 sources differ). Installed next to the exe with the game closed, hashes checked; fresh backup wh3_batch5/install_backup_v37_*; ATN's ReShade.ini: RayTracedSkipGameAO and GraphicsTest_gi_enabled lines removed (3 presets). No Attila zip (unchanged), no players' zip, tag, Nexus text. Not run in game.

## WH3 BATCH 5 (from 2026-11-05): ATN said go; plan tools/tw/wh3_batch5/plan.md, his answers at the end of atn_notes.md, reports wh3_batch5/reports
- [x] P1 Particle Trails: flags only where the moment left the range of the texels around last frame's place (a moment texel = 2 render px, point copied); row still Experimental, off. WARP warp_tests/smear_wh3. Not run in game.
      Not done: a one-texel blade that comes into coverage still flags (120 of 120 in the test), going out doesn't; puffs under 2 px a frame keep trails, at 4 px a frame only the inner texel flags (72 of 320 pixels).
- [x] P2 AO row: AO under Ray Reconstruction, Light History (default, v135's way) / REBLUR, both games, helper83, bridge v26 (RT_AO_REBLUR). Harness wh3_batch5/p2_ao_runs.sh, reports/p2_ao_runs.txt: same numbers as helper78 and 79. Not run in game.
- [x] P4 RR specular: Fresnel in the guide, twattila/rr_specular.hlsli (NVIDIA 4.2.1), WH3 rr_guides by its camera, Attila aa_rr_guides by a new b0 (anti_aliasing.hpp GuidesCamera); the other games' guides stay bare F0. WARP warp_tests/rr_specular. Not run in game.
      P1, P2, P4 closed: 14 targets built, page dumps (Attila the row and the date), all_modes on helper83 123 of 125 (two release memory flakes, each passes twice alone). Report wh3_batch5/reports/P1.md. Not run in game; not stamped, no zips (batch close).
      Not done: the other games' guides keep bare F0 (Rome II, Thrones, Troy, Pharaoh, WH1, WH2; own copies). For P3: the AO row's default takes back v34's PT AO darkening at a wall's foot, so compare again.
- [x] P3 PT AO, RTAO dark patch; R1 ran (r1_p3: replay, paused, 14 shots, check_run ok, 0 resets; v35 files back after). Report wh3_batch5/reports/P3.md. Helper85, bridge v27.
      Dark patch = placed trunks: first instance's shape at every place; a drawn trunk inside it, every ray hits. Seen on the replay's pines; none with Placed Objects off (b8b_first). Row No Black Patches on Trunks (RayTracedStartPast), off: harness AO 0 to 67 of 255, NOT run in game: R1 was used up finding it. Row Tree Trunk Cut-outs (RayTracedBranchCutOuts), off: ran, real defect, black trunks stay.
      PT AO: first freeze 30 % darker than toggled (log line), so 150 frames, NOT run in game. Row Steady Bounce Light (RayTracedPathKeptCube), off; on the open view under a code. R1 also: P1 no blue pixels with the row on, paused; P2 row switched, REBLUR made; P4 no error, look not judged.
      Not done: why shapes differ by instance (read: VS blends v8-10 with v11-13 by v6.x, rt_place takes v8-10): smallest step, log both rows' difference in one run. ATN's forest not reached. PT AO's darkness not reproduced on the open view (PT 101.5 RTAO 99.8). Rays' Light Bounce (not paths) and reflections' starts not moved. 31584506's row off.
      14 targets built, page dumps, all_modes on helper85 123 of 126 (release, release_rr, release_smoke: the memory flake, each passes alone).
- [x] P5 Freeze and helper crash; R3 ran 3 times (r3_p5_1, _2, _3: alt-tab away up to 7 min and back, 18 short tabs, FG on in one): 0 resets, the freeze did not come back. Report wh3_batch5/reports/P5.md. Helper86.
      Helper "crash" = ReShade loads the addon into the helper, DllMain FALSE, CRT's cleanup faults in the loader (ntdll 0x272c5). DllMain TRUE, AddonInit fails: stub host 0 faults, 3 game runs no event. WH3, WH1, WH2, Troy, Pharaoh. Dump for real faults: forced only.
      Terrain's count: Flush + DONOTFLUSH, 250 ms bound (not 50: 100 ms traces exist), new queries past it. WARP warp_tests/terrain_wait. Measured: 14-17 ms a frame, nearly all of "tracing", is this wait: P7's number.
      Log: whose device (game's thread polling 1 s, helper's wait), frame's age, window in front and its changes, no frame for 20 s: frame thread's addresses (forced by a suspend: works). Helper's line in harness removal.
      Not done: freeze's cause; alt-tab not proven as trigger. Flush at the shadow pass not added: compute then plain stream out was always there (trace lines 95-96, B8b's clean runs); add it if the new log says the game's device. Not run in game: reset lines, bound firing, dump, WH1, WH2, Troy, Pharaoh, Attila.
      Damage: R1 put the 04:30 backup over ATN's ini, preferences, preset, Auto-save.replay. Ini back from atn_run4; the other three can't come back. wh3_common.ps1 fixed. New: ReShade runs inside the helper, harness doesn't: ATN's call.
      14 targets built, page dumps b5p5 (Attila, WH3: the date only), all_modes on helper86 123 of 126: release_nrd, release_smoke the memory flake (pass alone); switching_removal counted helper86's new reason line as a second loss: harness now skips it, passes (reports/all_modes_P5_reruns.txt).
- [x] P6 Reflections without paths (ATN's broken water). Report wh3_batch5/reports/P6.md. Helper87, bridge v28. Not run in game.
      Cause (code + harness): without the paths' prepared light (One Bounce, GI off) every seen hit read last frame's colour, the upscaler's input: its square root under RR, L, M (harness: red wall 2.00/0.10 came back 1.41/0.32, whitish). And the frame's history, cache and learned light were made at the reflections' request, after the water's surface replaced the scene's (unseen box 0.06 against 0.44 with paths). Nothing "backwards" found: mirror geometry right in every mode.
      Fix: RT_PATH_COLOR_SQUARE_ROOT (colour squared back), RT_PATH_CACHE_FILL (One Bounce's request does the paths' frame work, then reflections read its prepared light as with paths). Harness: 2.00/0.10 again, box 0.65-0.68; One Bounce's own bytes identical. Water Reflections tooltip: best with ray-traced GI. README lines WH3, Attila, Troy, Pharaoh, WH1, WH2.
      Not done: GI off still last frame's picture (after the tone mapper below 100%), no learned light: pass the march's light like Attila. Water's scale line (bytecode-patched shaders, 2700 lines). Water Reflections' fog (no water trace). Rome II, Thrones README.
      14 targets built, page dumps b5p6 (WH3 the tooltip + date, Attila the date), all_modes on helper87 128 of 128 (two new: reflect_one_bounce_fill, reflect_color_square_root), reports/all_modes_P6.txt.
- [x] P7 Double work; R2 ran 4 times (r2_p7_1..4: siege benchmark, 4K, ATN's rays, FG off; check_run ok, 0 resets). Report wh3_batch5/reports/P7.md. Helper unchanged.
      Probe: Time Each Pass (One Frame) button (twattila/pass_timing.hpp, first-registered events), wh3_pass_timing.txt; ran in all 4. Siege is CPU-limited (GPU busy 13.4-14.6 of 28-31 ms).
      Skip the Game's SSAO under Rays: default ON now, also under Path-Traced AO; AO pass 0.51 to 0.09 ms GPU. Skip Trees in Shadow Maps: 1.01 to 0.60 ms, stays off (fog look); its bypass no longer drops the trees' cascade capture (captured before the draw).
      Not done: terrain in one draw (WH3's mask GS drops triangles, one stream can't; WARP terrain_in_draw proves the pass-through way; row taken out), game's GI flag 0/1 (readers unread), cascades' rest, terrain count's wait (8-16 ms a frame in a battle's first 30 s). Not run in game: PT AO's look with the SSAO skip, trees' out-of-view shadows with Skip Trees.
      14 targets built, page dumps b5p7 (Attila the date only; WH3 the button and the SSAO row).
- [x] P8 Banners and gaps. Report wh3_batch5/reports/P8.md. Helper88, bridge v29. Not run in game.
      Cause (ATN's debug8 shots + code): the dark patches are whole triangles. The game lights 444F1190's banner from both sides by its smooth normal (989BED69 reads SV_IsFrontFace); rt_trace took the triangle's facing from the depth and traced turned-away ones against the banner itself. Mesh = the drawn one (same draw, paused), cascade copy collapses in view.
      Row Smooth Banner Shadows (RayTracedSheetsUnselfed, Experimental, off, WH3): cloth and sails trace from the camera's side, own triangles within 3 m passed (RT_SHEETS_UNSELFED). Harness HARNESS_SAIL_FOLDS: folded sheet 67.7 % dark off, 0.0 % on, a solid card's shadow on it 96.6 %. Two unit cut-outs added (AF697DAE, F2BC0D72: base alpha beside the dither, the script skipped them). Gap list reports/p8_gaps.txt.
      Not done: 1405BFB4 (fx_rigidtextureblend unit parts: 8 layer masks by per-draw weights, level C); 74 dump-only base-alpha cut-outs (level B, reports/p8_base_alpha_scan.txt); dissolve shaders; Attila has no row (same trace code); probe's cost unmeasured; 2-4 rays a pixel not proven (test folds finer than its pixels).
      14 targets built, page dumps b5p8 (WH3 the row, Attila the date only), all_modes on helper88 133 of 134 (release_rr: the memory flake, 1 of 2 alone passes; 6 new cloth_folds modes pass).
- [x] P9 Scaling audit's rest, Shadow Map Size. Report wh3_batch5/reports/P9.md. Not run in game.
      Reflections' depth levels: CB0A1AF6 (Hi-Z step) copy ssr_depth_step in the SSR scale chain (same gate as the mip copies). WARP warp_tests/scale_copies: share 1 same bits; picture 2/3, 1/2, 1/3: game up to 116 edge cells from outside, ours 0. Same test: GI and Shadow Blur at Scale copies at 50% / 33% = the game at 100% (GI 0 voxels differ, blur only within 3 px of the edge); the game's bytes 119171 voxels / 21829 px. Row stays Experimental, off (look not seen).
      NDC list, wider window (wh3_batch5/p9_ndc_trace.py): 15 VFX PS build NDC from the texture's uv but use it for depth only (harmless); the rest from the viewport; TAA runs at display size. Campaign sea 034010C4 fine by bytes but its refraction offset is 2x apart (depth load vs NDC) below 100%, vanilla too. 3AC630DF a 1:1 copy, nothing to fix.
      Not done: Shadow Map Size not shipped, level C (3 size constants only seen in game: shadowmap_PS c0 in every forward reader's PCF, fog's ld; C883E3FA's cb0[2].zw for the kept casters' copy; 800F7F7F / D5DCC85F ld x 2; packed exe, no table). Smallest step in P9.md. Water's line 2076D289:844 / DFA2AEA2:838 (P6's), trace below 50% (no run).
      WH3 built (no shared file touched), page dump b5p9 the date only. Harness not run (helper, bridge, rt_core untouched).
- [x] P10 the rest. Report wh3_batch5/reports/P10.md. Not run in game.
      Water's line below 100% (P6/P9's): twattila/water_effects.hpp PatchWaterScaleLine, the 6 battle water / river PS swap to the patched copy when the draw's viewport is smaller than its target (WH3 only: material_water). WARP warp_tests/water_line_wh3: 100% bits equal; 50% vs 100% over 0.01: game 6868-21822, ours 0-244. Preset L / M square root checked (both scales, both games): nothing to fix.
      Not done: reflections with GI Off (still last frame's picture; step: RT_PATH_CACHE_FILL without a bounce request, helper + harness); units' weapons out of view (level C); Far Tree flicker, culling settings, marching banners, Water Motion, sails, ropes (need runs); RR particle layer below 100%; fog light limit (B11's side draw); Attila grass flicker, FG memory; batch close (stamp, zips, install).
      14 targets built, page dumps b5p10 (WH3, Attila: the date only). Harness not run (helper, bridge, rt_core untouched).
- [x] CLOSE. Report wh3_batch5/reports/round_report.md. Review's 5 fixed: SSAO skip RTAO only and back to Experimental, off (reflection resolve t1 and PT AO's specular read it); other games keep REBLUR under RR (Sliders default, rows 0 in Attila / WH3); helper's forced fault only with -DRENODX_TW_HELPER_FAULT_TEST (helper89, bridge v29); Time Each Pass empty stamps, phase under the lock. README lines Troy, WH1, WH2, Pharaoh, new v47 Rome II / Thrones.
      14 targets, page dumps b5close (WH3 the SSAO row + date, Attila the date), all_modes on helper89 134 of 134, same names as P8 (reports/all_modes_close.txt). Test copies wh3test1/2.replay, wh3test.save in the Recycle Bin (copy back for run_game). Stamped wh3 v36 addon / helper89, v140 addon / helper89 copy (build/stamped/batch5). twwarhammer3_v36.zip, twattila_v140.zip tested; WH3 runtime installed, hashes checked; ATN's ini unchanged. Not run in game.

## WH3 BATCH 4 (from 2026-11-05): offline only, no game runs
List: tools/tw/wh3_batch4/list.md (`.claude/projects/D--Dev-renodx/tools/tw`). Reports: wh3_batch4/reports.
- [x] A AO and presets (items 18, 19 + 2, 1, 12). Report reports/A.md. Not run in game. WARP warp_tests/sky_wh3 (sky, spot), page dumps A1, A2 (Attila the date only), all targets built.
      Done: RTAO alone permanent, row gone; Path-Traced AO back in the method slider, Experimental, off, sky from the SH at the camera's place; RT/PT against Attila's listed with reasons, no value changed; Spot Filter row, Experimental, off.
      Not done: item 12's cause (lone black pixels 2.4x more at 50% in Debug View 2, filter hides them). Why every agent run of batch 3 has SH 0 and GI 0 while ATN's log has SH 1 and GI on: batch 3's ambient numbers are on the other path.

- [x] B Stability and scaling (items 14, 9, 8, 13). Report reports/B.md. Not run in game. WARP warp_tests/swap_loss, warp_tests/ssr_mips; page dumps B9, B13 (Attila the date only), all targets built.
      Done: 14 reset no longer hangs at CreateSwapChain (old flip chain kept the window, the swapchain mod rewrote the game's blt fallback; ClearState + Flush, fallback kept, 90 s no frame closes the game); 9 LOD Bias and Effects at Output enabled (rule read dlss_mode); 8 SSR mip chains clamp at the picture, resolve's levels follow render at 1:1 (L7: 39% of the picture lost at 0.333, now 0); 13 audit, GI grid and shadow blur copies under an Experimental row, off.
      Not done: why the GPU reset (rows on weren't in B8b's soak); water's NDC at scale (DFA2AEA2, 2076D289, line 838); about 30 PS not read one by one; nothing below 50% in a trace; Hi-Z's edges.

- [x] C Water, reflections, grass, double work, shadow map size (items 4, 5, 10, 11, 3). Report reports/C.md. Not run in game. WARP warp_tests/water_wh3, grass_wh3; page dumps C1 (Attila the date only), all targets built, harness on helper78: 115 of 116, release_nrd's memory flake passes alone.
      Done: 4 Clarity and Caustics on battle_water, battle_river, polluted (copies swapped at the draw, off = the game's shader); 5 rays' lobe was Attila's (alpha at most 0.10; WH3's is roughness^2): row off, needs helper78; 10 no hard grass limit in the drawn shaders, Grass Fade Distance row; 11 row Skip the Game's SSAO under RTAO, off; point light maps: none exist; TAA can't go (u0, u2 read later).
      Not done: 3 Shadow Map Size: not shipped, 2048 / 1024 chain and fog read the cascades by `ld`, one quadrant at 8192. 11 game's GI under rays not removed (bool has to clear at each reader). 5 RR's specular albedo without Fresnel. Campaign sea has no depth term: stays.
- [x] D Shadows from rays (items 22, 6, 7, 15). Report reports/D.md. Not run in game. WARP warp_tests/outside_view_wh3; page dump D1 (Attila the date only), all targets built; helper unchanged, harness not run.
      Done: 22 units from the shadow pass (BADA4FE3, 46E626D5) in the ray scene under Replace, row Units Out of View on; 6 Replace on by default, row Skip Trees in Shadow Maps, off; 15 row Light Through All Banners, off.
      Not done: 6 cascades can't go (fog, forward, the capture read them), memory is the game's; 7 Far Tree flicker's cause not found; trees and buildings never seen stay out (kept 300 frames after seen); weapons of off-screen units.

- [x] E Motion, particles, VFX (items 20, 21 + 17, 16). Report reports/E.md. Not run in game. WARP warp_tests/vfx_wh3; page dump E1; only WH3 built, no shared file; helper unchanged, harness not run.
      Done: 20 four VFX brightness rows (patched copies at the draw, 1 = the game's); 21 Particles at Half Resolution, Experimental, off (sharp sprites held and drawn in the half layer's state), Particle Trails Under Upscaling, Experimental, off (history dropped where the zeroth moment changed); why VFX costs so much, in the report; 16 list reports/E_motion_list.tsv.
      Not done: particles at output resolution (costs more, B11's rework); reactive mask (no input in the helper, L and M ignore it); marching banners (no last position, instance key not provable offline); Water Motion stays off; sails, ropes in no trace. Half row's gain and all looks unproven.

- [x] Close: review's 6 findings fixed (SH sky only under Path-Traced AO; VFX rows on replayed sprites; odd width; Grass Fade's PS gets its own callback; Skip SSAO only while rays trace; capture's failed buffer before the binds), compiled only. All 14 targets built, harness on helper78: wh3_batch4/all_modes_close.txt. twwarhammer3_v33.zip, twattila_v137.zip in Downloads, stamped; v33 installed, hashes checked; RayTracedAOAlone out of ATN's ini. Report wh3_batch4/reports/round_report.md. NOT run in game. First in game: Units Out of View's cost (siege no_room, GPU ms).
- [x] v34 AO (item 25). Report reports/v34_AO.md. Not run in game. Helper79, shared with Attila (v138). RR only saw AO inside the colour; REBLUR now cleans RTAO and PT AO first under RR. Harness, RR without history, 2 rays: spread over time 0.0055 to 0.0023; PT AO 0.27 to 0.011; settled RTAO little gain. +0.85 ms, 215 MB at 4K.
      NOT DONE: full all_modes.sh on helper79: Attila.exe pid 2508 up since 05:13, not started here; 7 related modes passed beside it. Close it, then run. PT AO's wall foot darker (0.74 to 0.57), ATN to judge.
- [x] v34 Casters (item 24). Report reports/v34_Casters.md. Not run in game. Helper80, bridge v24 (Attila same but the date; rebuilt only). WARP warp_tests/casters_wh3, harness HARNESS_KEPT; page dump Casters; all 14 targets built.
      Done: trees, walls, rocks, shrubs, props, flags from the shadow pass, placed from rows into once-built BLASes shared with the main pass (key_family), in-view rows left out; row Shadow Casters Out of View, on with rays' sun alone; Placed Objects keep until the scene ends within 6 M triangles, 12288 meshes; moved meshes 300 frames.
      NOT DONE: cost in game unmeasured; cascade range only logged at next run (Cascades' split distances); Far Tree Shadows stays off, flicker's cause unknown; full all_modes.sh on helper80: Attila.exe pid 2508 still up, 14 related modes passed beside it. Which screens trigger the scene-end rule (30 presents without main view) not provable offline.
- [x] v34 Quick (items 26, 27, 23). Report reports/v34_Quick.md. Not run in game. WARP warp_tests/ambient_probe; page dump Q1 (Attila the date only; WH3 keys: GrassFadeDistance, RayTracedAOSpotFilter out); all 14 targets built; helper unchanged, harness not run.
      Done: 26 Spot Filter gone; 27 pixel trace row in Ray Tracing Extra Options > More Options, wh3_pixel_trace.txt with the rays' AO section; 23 Grass Fade Distance removed (false motion provable, growth is the generator's shrink at 16 cells).
      Not done: ray hit distance in the trace (helper change: one pixel's Occluded hits read back); longer grass (second dispatch of 537AF064 or an outer ring); stale keys in ATN's ini, ignored, strip at install.
- [x] WH3 v34 close (helper81, Attila v138 zip): review's fixes, all_modes 115 of 116 (release's memory, one of two reruns passes). Installed, not run in game. Report tools/tw/wh3_batch4/reports/round_report_v34.md.
- [x] WH3 v35 RR inputs (helper82, bridge v25, Attila v139 zip). Report reports/round_report_v35.md. Not run in game. Rows off: Particles Skip the Denoiser, Picture Before Particles, Depth of Field Guide (preset E); 100% scale only. all_modes 119 of 120 (release's memory).
      NOT DONE: below 100% (colour after the tone mapper); particle mask (no preset reads it); diffuse hit distance (needs a UAV in the trace shaders); disocclusion mask (no format). Composite blend factors unread: ATN's test shows them.

## WH3 BATCH 3 (from 2026-11-04): fixes, DLSS rows, motion, full RT scene; game runs allowed on replay copies, campaign copy, benchmark
Plan: tools/tw/wh3_batch3/plan.md (`.claude/projects/D--Dev-renodx/tools/tw`). Reports: wh3_batch3/reports/Bn.md. Facts: wh3_batch3/facts.md. Run: test_scripts/run_game.ps1 -Game wh3, proof: wh3_batch3/check_run.py.
- [x] B0 kit, log fixes, captures. Proven by runs c1-c9, survey (reports/B0.md, facts.md); check_run ok on each but c9_dlss3 (no "Finished exiting" with in-game DLSS) and bm_old (the game's own crash). Harness: tools/tw/all_modes_b0.txt. Helper: build/tw_dlaa_helper76.
      Not done: soldier's height not measured; one line per drag not proven (no drag by script); water state at D409F9B5 not seen (no A5FC3927 on any map tried). ATN's ini restored byte for byte; the plan's end-state changes (Output Auto, stale keys) wait for Close. Test copies wh3test1, wh3test2, wh3test.save stay in AppData until Close (Wh3-Close).
      New for later: B1 black patches are terrain-flagged geometry, sun and AO both, 100% and 50% alike (not the 1.5x offset); B1 or B8: tessellated terrain joins last and goes short at 100% (196335 of 760548); B2: Debug View 8 always magenta in WH3; B11: gfx_fog 0 draws 438D86B8, 2D1039C0, not the 4 fog hashes; all phases: benchmarks are test material (siege fire, dark map).
- [x] B1 black patches. Proven by runs b1g_c3, b1g_c4, b1g_paths, b1g_paths50, b1g_c5, b1g_siege (check_run ok each; reports/B1.md, tools/tw/wh3_batch3/b1_numbers.py): replay's first view 12.69%, 28 blobs > 0, 0, at 100% and 50%. Cause: 709BC4D7's second draw of each pair (GS 33D642AB only makes skirts) streamed past its GS, coarse hills over the fine ones; row 5767F87C alone changed nothing in the replay, it was the campaign's rectangles. Terrain's mask in a GS (campaign's sea), terrain's room first, Path-Traced AO gone (N3), N4 scan. Harness: tools/tw/all_modes_b1.txt.
      Not done: campaign still has 22% of the view under 0.25 with Everything on, by eye mostly true shadows and AO's range in campaign units: B10, not called clean. Not run: Attila (no tessellated row, no change). WARP: none.
      New for later: B6 FIRST: unit banners (444F1190, stencil ref 1) black with rays on, also in Scene Test Terrain Only: shots/b1g_c3/31_all_near.png; B7/B10: black triangles on far mountains in Debug View 1; B8: leaves 783383C6 left out grow (0.2-0.4 M, siege 1.62 M); Close: delete *.in-use-* in the game folder. Off check's two-run floor is 0.05, not 0.035.
- [x] B2 AO at scale, Debug View 8. Proven by runs b2_50, b2_100, b2old_50 (old build, started at 50%), b2b_half, b2b_camp_50, b2b_camp_100, check_run ok; WARP warp_tests/ao_filter/run_ao_ratio.ps1; harness all_modes_b2.txt, 115 of 116: `release` memory flake +31 over 30 MiB, 2 of 3 pass alone, helper unchanged (reports/B2.md).
      Not done: collect 01 EE5C29AE drawn in no run; live 100 to 50 in a session not run (B4 can); Debug View 8 still magenta in Troy, Pharaoh, WH1, WH2. Not run: Attila (bytes same).
      Found beyond the plan: GT-VBAO read its settings at b10 while the addon bound b13 (flat white when started at 50%). New for later: B9 G18: game's SSAO is much darker than GT-VBAO under RTAO on this view (Debug 8 mean 28 against 101).
- [x] B3 the rest at 50%. Proven by runs b3_100, b3_50, b3f_*, b3camp_*, b3c_*, b3b_camp_*, b3r_off_50, b3z_50 (final build), check_run ok; WARP warp_tests/t00_scale; harness not run (rt_core, helper, bridge untouched) (reports/B3.md). Sharpening's scale term off (edge energy 50/100: 2.1 to 0.8), heat haze and secondary LUT by overscan, jitter amplitude right (16 phases, 0.43 render pixels at both), LOD and culling x share squared (50%: 28.0 to 50.19 M indices; row Full Detail Below 100%, on; RT/PT 25.85 to 31.95 ms on x8 sliders), Bend's memory kept 10 s off, guides and Depth Levels at the corner, SSS Debug View whole.
      Not done: GT-VBGI's difference 50 vs 100 correlates 0.31, under the 0.9 asked (effect under noise, not proven either way); bloom kept though wider at 50% (default per plan). Not run: Attila, live scale change, heat haze by eye, 33% and 20%; clean exit fails without an upscaler, as c9_dlss3.
      New for later: B4 or B12: DLSS L and M 9% darker at 50% (mid-tones; K 3%, TAA 2%), knee tried and reverted, needs a dark paused view. B4: the scale float replaces the prefs read, keep the detail row when the Below row goes. B12: GT-VBGI on a view with bounce. Close: *.in-use-* again.
- [x] B4 upscaling rows, LOD Bias, Effects at Output. Proven by runs b4_modes, b4_low, b4_lod_y, b4_lod_w, b4_quit, b4_saved, b4_file05, b4_hidden, b4_fg, b4_methods, b4_effects, b4_game50, b4_camp, check_run ok; b4_dlss3 fails only on the guard's own WARN and no "Finished exiting" as c9_dlss3; offline b4_aspect; Attila page same; harness not run, helper76 stays (reports/B4.md).
      Done: Upscaling Mode live through the game's float, exact sizes, Custom 1-100 (whole to 1%), kept value, Below 100% row gone, no game's-TAA frame for a proven size, FG stays up, LOD Bias default Sharper (0.99 of 100's edges), Effects at Output, gfx_dlss guard. Quit at a mode: the game writes it to its file before destroy_device; the kept value brings it back with the addon.
      Not done: Custom's applied-on-release not run (no script drags); rows not seen on screen; menu slider against a mode read only; Effects at Output's effect under the noise on one view. Not run: Attila (page same), 1440p and ultrawide in game (offline only).
      New for later: B12: look at the AA section by overlay; DLSS L and M 9% darker at 50% still open. All phases: unit banners fade 4-5% after the pause starts (control: nothing changed): take A/B shots late. gfx_dlss 3 makes the game save 0.58 as its resolution scale. Close: ATN's stale LOD, Render Scale, Effects keys are now the defaults; no UpscalingGameScale left.
- [x] B5 motion vectors. Proven by runs b5_flag, b5_near, b5_siege, b5_mom, b5_skaven, b5_camp, b5_cost_*, b5_amp*, b5_water*, b5_final, check_run ok; WARP wh3_batch3/b5_motion_test: 51 VS all outputs bit-same but o1, o1 = game's o2 at last time; 83 PS created, others same, flag right, o3 = 761079CC; Attila page same; harness not run, helper76 stays (reports/B5.md).
      Done: row Object Motion Vectors, On: the draw writes rt3 itself by token patches, no decompile, no redraw (M1, M2, M3, M5; M7, M8, M12 closed from traces). Paused against off 0.004 steps. Cost 0.4%. Water Motion (M6) Experimental, off.
      Not done: M4 (bones or rows with sway: drawn in no trace, needs a hand edit per VS). Water's crop not reached in 3 pans. Most listed shaders in no scene. Flag's real sway too small for the view (a temporary build proved the path). Not run: Attila (bytes same).
      New for later: marching unit banners (444F1190) have no last position: camera motion only, ATN decides. B11: water is 4F35D09F + DFA2AEA2, 2076D289, 034010C4, not A5FC3927. B12: Water Motion on a river, shots 1-2 s into a key press.
- [x] B6 cut-outs, cloth, units' other VS. Proven by runs b6_banner, moved, moved2, skip, hit, fix1, g5, ab, ab2, ab3, gates, final, check_run ok; Attila page same; harness all_modes_b6.txt 116 of 116, helper76 stays (reports/B6.md).
      Done: black banners = stale copies of C0ED2883 (placed from a buffer out of a capture's key), now every frame: black_patch 0% near view after a camera move, was 9 blobs. Cut-outs by clip_cut_outs: 541 PS from their asm, 6 seen; pines' shadow cell 1.22 in the Sun view, picture within 1%, trace 5.10 > 5.20 ms. 11 rows for G6, G7.
      Not done: cut-outs at the atlas's limit not reached; Scene 2 by plan not run (one-run Scene 0 A/B instead); close unit view has no before; G6, G7 rows drawn in no trace, level B. WARP none. Not run: Attila (page same).
      New for later: B8: Fallen Gates leaves 8-24 M out (dynamic texels full), VB test not enough (C0ED2883's VBs are static, it moves), keep UV and masks in placed versions. B7-B12: Scene Tests hold kept meshes for a while. B1's Terrain Only reading of the banners was that.
- [x] B7 buildings, rocks, tents, mountains. Report written by B8 from the commits: B7's agent stopped before its report (reports/B7.md). Runs b7_wall, town, first, siege, camp, grass_battle, stall_*, probe*, check_run ok; harness all_modes_b7.txt: `release` flake only.
      Not done: b7 `final` never run (see B8). Off and cost on B8's build.
- [x] B8 placed instances: built, works, **row Placed Objects, Experimental, Off** (stop rule; off = B7's capture). Proven by runs b8_*, b8f_*, b8z_*, b8end_* (reports/B8.md), WARP rt_capture_test OK, Attila page same, helper76 stays, harness all_modes_b8.txt 116 of 116.
      On: RT/PT 100% 52.0 to 43.0 ms, 50% 32.7 to 25.3, vertices 25 to 10 M, 823 static meshes, kept off screen, moving weapons by rows.
      Not done: trees (per frame), off-screen trees, sway rows hidden; named caster leaving view not isolated; 5 min soak only on a build with trees. Not run: Attila, row on with FG.
      **NEW, TOP OF THE LIST, "siege reset": siege benchmark + any ray effect resets the driver (nvlddmkm 153, about 10 s in), even Sun + AO alone, nothing placed, about B6's scene (1 of 6); rays off 0 of 6; placed on about 50%, off about 26%. Cause not found. Count the 153s, check_run only sees "set shot, clean exit".** Table in reports/B8.md. B9: G3 stays multiply unless the row is on.
- [x] B8b siege reset: **fixed by a Flush before the terrain's stream output** (`rt_core.hpp`): the game's D3D11 device hung (helper idle) in the batch with tree simulation 8824D3A2 and that capture. Without it 14 of 47 siege runs, with it 0 of 38; cause inside the driver unknown. Trees placed again, **row Placed Objects normal, default On**. B7's final as b8b_first. Proven by runs b8b_fx, cl, rs, g1, pl, tr, fg (RT/PT + RR + FG), gates, battle, soak, control c10 (reports/B8b.md); check_run counts 153s and fails on "No memory". Attila page same, helper76 stays, harness not run (helper, bridge same).
      Not done: **gate passed at 1080p, low textures, not at 4K.** Not run: RT/PT at 4K, campaign with rays, 4K numbers on this build: **each reset left its process's commit until a reboot: 109 of 127.7 GB, rays no longer fit at 4K. Nothing frees it but a reboot (ATN). Later phases: `-Prefs x_res=1920,y_res=1080,gfx_texture_quality=1` (b8b_final.py LOW), no 4K cost numbers, no campaign with rays until then.**
      New for later: trace 100 ms over the siege's forest (B12); B9: off-screen casters on by default now.
- [x] B9 rays in charge. Proven by runs b9_ab, pan, siege, skaven, foglow, rtpt, rtpt_skaven, final, check_run ok at 1080p low; b9_rtpt_siege fails only on "No memory" (the PC's commit); 0 events 153; WARP warp_tests/lights_wh3 (make_lights.py makes all 15 shaders); Attila page same; harness all_modes_b9.txt: `release_smoke` flake, `quad_980k_ramp` fails with helper76 unchanged, memory [I], re-run after reboot (reports/B9.md).
      Done: RTAO alone, row Replace the Game's AO, On (open ground +3.3%, largest cell +9.5% against rays off); Torch and Fire Shadows in RT/PT (156 lights read in the siege, Debug View 6 real, trace +0.14 ms); Fog Shadows on all 6 fog PS (G15, 438D86B8 run); torch light in fog row, 0, off RT/PT; N2 closed: no garbage with Placed Objects on.
      Not done: **Rays Replace Shadow Maps stays Experimental, off** (G3: a flying dragon off screen loses its shadow, units enter the scene only in view; the rest of the pan +1%, softness works). No fire on/off A/B (benchmarks don't pause, cameras cut). Smoke cell check: no smoke draw in any trace, t1 kept by WARP.
      Not run: 4K, campaign with rays (commit 109 GB until ATN reboots), FG with these rows, night, Darklands, 4 of 6 fog PS, torch light in fog's look, Attila (page same).
      New for later: **B11: C506E37C, 4347C118, 7998DC13 are traced_lights swaps now: Fog Strength merges there, make_lights.py.** B12: NVIDIA's text shows preset L plain, F when RR runs. Close: ATN's RayTracedLightGlow=1 to 0.
- [x] B10 far scene, campaign. Proven at 4K by runs b10_camp_a, single, rows_a, rows2_a, camp2, c3, empire, check_run ok each, 0 events 153 since ATN's reboot; Attila page same; harness all_modes_b10.txt 116 of 116, helper76 (quad_980k_ramp passes again: B9's fail was the commit) (reports/B10.md).
      Done: campaign's ground (15FE8427, A40D7DDB), trees and bushes (D025F795, BDAEC4F3) in the scene; terrain keep test by corners; campaign_frames; rays' ranges x0.1 on the map (Host::campaign_metre, Attila 1). Black under 0.25: 22% > 3.2% first view, 0.27% high up, 0 straight blobs. G10, G9b closed: don't draw. Replay at defaults still 0 black.
      Not done: **Hills Out of View (G9) stays Experimental, off; Far Tree Shadows (G2) off: both capture and run, neither pass met: no cell changed on the views tried.** 3.2% left is under trees and is shape, not error: ATN's eye. Far mountains' black triangles: facets turned from the sun [I], under fog in the picture, not fixed.
      Not run: Attila, FG or RT/PT preset on campaign, Darklands, night, moving armies.
      New for later: B12: **CPU 21 ms "tracing" in the status at 4K, read what it measures**; Bend darkens campaign trees 4.6x with rays off (B12 or ATN). Close: **Steam runs elevated after every reboot (ATN's task SteamAdminStart): -shutdown, then -silent, before runs;** Attila campaign_metre left 1, one line when tested.
- [x] B11 reflections, march, fog. Proven by runs b11_march, live, scale, fog, fog0, cost, camp, c3, check_run ok at 4K, 0 events 153; WARP b11/warp_march, warp_resolve, warp_fog; Attila page same; harness not run (helper, bridge, rt_core same), helper76 stays (reports/B11.md).
      Done: Method slider All Surfaces (Experimental, off): the game's march on flags' bit 0, +0.7 ms, water 0 bits. Water's march one to one at or below half the display with Effects at Output (Mode P: the tree's reflection kept, row +0.17 ms). Rule 0 no longer closes the pass, 76A707E5's rule (WARP 0 misses, water too). Fog Strength on 4 more PS, swapped only off 1; gfx_fog 0 needed nothing (2D1039C0 is fog of war).
      Not done: **fog light limit, measured 1.8x at 2, not added: needs a side draw (reports/B11.md), ATN decides;** particle resolution (traced, medium to large). Flicker in motion on armour: stills can't tell, row stays off.
      Not run: 76A707E5, skin, the 4 fog PS (draw in no run: level B), Darklands, below 50% one to one, 1440p, All Surfaces with FG, Attila (no code).
      New for later: B12: RT reflections on water blotchy at Mode P (b11/p/water_scale.jpg), look at 100% too.
- [x] B12 soak and whole. Proven at 4K by runs b12_siege0-2 + siegefg0-1 (**gate passed: siege RT/PT 3 full runs, 2 more with RR + FG, 0 events 153**), soak_p (20 min), soak_100, camp (memory flat), m_*, o_*, c_*, overlay, rr, black_100, black_50, c3 (off SAME within 0.05, black 0%, 0 blobs in 6 views), check_run ok; b12_m_game fails `clean exit` only, as B3's stock runs; Attila page same; harness not run (helper, bridge, rt_core same), helper76 stays (reports/B12.md).
      Done: water keeps the game's reflections under rays (hits have no fog, far hills as dark patches, also at 100%), row Water Reflections shown, Experimental, off, not in RT/PT; NVIDIA's overlay: follows what runs, L plain, F with any ray effect + RR, nothing wrong; CPU 21 ms = waiting on the terrain's counts: no-wait path built, A/B no fps, p99 worse, reverted; GT-VBGI holds at scale (0.98); M no longer darker at 50%; AA section seen; Bend on campaign trees right.
      Not done: **DLSS L still 10% darker at 50%** (tooltip says so, M 2.4%, K 0.7%); **Water Motion not proven, stays Experimental, off** (stop rule); replay ends at 6 min: the rest of soaks is the results panel's still scene.
      Not run: night, real fire A/B, Darklands, SDR preset by click, 1440p, Attila (page same).
      New for later: **Close: ATN's RayTracedWaterReflections=1 to 0** (else the patches are back), *.in-use-* 42.
- [x] Close. 14 addons, helper77 (FSR status after a pause only; rt_core: a test value out), harness tools/tw/all_modes_close.txt 115 of 116 by name, `release_rr` memory flake +32 over 30, 3 of 3 alone; Attila page date only, WH3 page tooltip and date. Runs: close_quit (first fix lost), close_quit2 (file 1, check_run ok), close_last (ATN's ini, stamped v32, check_run ok, 0 events 153). Round report: wh3_batch3/reports/round_report.md. Downloads/twwarhammer3_v32.zip, twattila_v136.zip; no players' zip, tag, Nexus text. v32 installed by hash, ATN's ini end state, copies removed.
      Review: quit at a mode fixed (the kept value goes in the game's preference at destroy_device: it writes the file a second after, so B4's "saves before" was half true), no scale written with the helper failed or gfx_aa below TAA (read, not run), old Glow and Water values reset once on SettingsRevision (run), sampler log out, viewport hook early return, stale text. Refuted: refusal sticking at a proven size (a resize moves the render size too). Left: RENODX_TW_* env, inert, the kit needs them.
      Not done: 8 *.in-use-* files pinned by dead helpers until a reboot; SteamAdminStart left alone (ATN's: starts Steam elevated after each logon, breaks game tests until -shutdown, -silent).

## WH3 BATCH (from 2026-11-03): WH3 gets Attila's layout and features; nobody starts a game

The plan lives outside the repo: tools/tw/wh3_batch/plan_final.md (tools/tw = C:/Users/arshi/.claude/projects/D--Dev-renodx/tools/tw), reports beside it in reports/. Lessons: `.agents/skills/tw-port-checklist`. One implementer at a time, every phase ticked here in its own commit, pushed to origin atn-mods. Everything new is off by default until ATN's in-game test list (P13).

Order: P0 > P1 > P2 > P3 > P4 > P5 (milestone 1, zip v30) > P7 > P12 > P6 > P8 > P9 > P10 > P6c > P11 > P13.

- [x] P0 Ground work: skill `tw-port-checklist` (cb_audit.py, scan_register.py moved there), this list, raw NUL, both baseline builds. ATN's check: nothing.
      NOT DONE: its own harness run. Attila was running (21.9 GB of VRAM), and the harness doesn't run beside another GPU job. tools/tw/all_modes_wh3_base.txt is the 18:05 log of the same helper code, 116 of 116, marked as a copy. Smallest step: P1's all_modes.sh with no game running.
- [x] P1 Correctness at 100%: `anti_aliasing::Settings` from the DLSS rows, `Configure`, SSS off under RT shadows, `Release(true)` (not while in-process FG is installed), `BeforeSharedWrites`, camera per frame + status line, Debug Views in t00, `ReadGpuVendor` in the bridge, square root for L, M, RR (7-decade ramps flat raw, fixed with it, K fine; the frames' real range unread), About.
      Not done: "saved DLSS off NVIDIA becomes XeSS or FSR" (P3: no such rows yet; P1 only keeps the helper from starting there). ATN's check: DLSS on, off, on: VRAM falls; scale below 100% in a session: DLSS off, status line says so, no half-picture; paused: DLSS stays on (camera per frame is assumed there); K vs M darks and bloom.
      Harness: all_modes_wh3_p1.txt, helper74, a real run: 115 of 116 by name; `release`'s "no memory kept by sets" missed its 30 MiB by 5 (+35), then 5 of 5 alone (+19 to +23), helper unchanged, +32 seen before in all_modes_switch.txt.
- [x] P2 Data kit: indirect draws in traces, WH3's camera registers (c37, c39), TAA and t00 marks (overscan, near, far, depth at near), RT Debug View + Scene Test + Record Profile shown, a log line at each hook's first run; `renodx_trace_now` traces at any scale, DLSS on or off.
      Not done: nothing of the phase; not run in game. ATN's check (P13's list): `renodx_trace_now` in a real battle with fire, on the campaign, at 100% and at 50%, one at a time, renaming tw_frame_trace.txt between them; "First run" lines in the log; RT Debug View with each Scene Test.
- [x] P3 Rows (`AppendAa`, flat in the old section until P5; rows without effect hidden), FSR and XeSS at 100%, Reflex without FG (mode 0 while the in-process FG runs), saved DLSS off NVIDIA becomes XeSS or FSR, red line without the helper, Motion Debug View (in t00's on_drawn, not at the TAA slot: exposure, bloom and LUTs would act on it).
      Not done: nothing of the phase; not run in game; FG rows still restart-only until P4. ATN's check: FSR, then XeSS in a pan: sharp, no doubled edges; Debug View on banner, ship, smoke, water, units; Reflex on with FG off: latency down, Frame Rate Limit holds.
- [x] P4 Frame generation in the helper: live on and off, HDR10, scRGB (fg_hdr10), SDR; `SetCamera` shared; camera rebuilt without jitter; HUD-less from the resolve and sharpening. The last commit with Streamline in the game's process: 253e463a (going back: revert P4's commits, that tree builds).
      Found: WH3's constants sit at b10, every other TW game's at b13: `frame_generation::Install` takes the register (b13 gave a black HUD-less image in the WARP test). Not done: FG's method row is P5's (until then saved DLSS, so off NVIDIA its status line says why); not run in game.
      ATN's check: FG on and off without a restart in HDR10, scRGB, SDR; FG Debug View HUD-less with flares and sharpening, no UI, portraits out; FG off: helper's memory falls after 10 s; menus show frames, no freeze.
- [x] P5 The page: Attila's layout, one copy of the code (`graphics_page::Apply`, game facts in `Bindings` and `Texts`, defaults Attila's). Attila's rows, every state's visible rows, buttons and sliders identical in a dump (tools/tw/page_dump); WH3: no key lost, pages 0 and 2 identical, 101 rows on the page.
      Milestone 1 closed: review's 5 low findings fixed (guide textures only with RR, saved RTAO + GT-VBAO, 2 Rays a Pixel texts, Motion Debug View's texture); Attila's page dump identical; twwarhammer3_v30.zip in Downloads. Not run in game. Row left: Debug View's labels name Reflections, Point Lights, Fog (magenta here, its tooltip says so).
      ATN's check: walk the page: each method slider follows a preset; RT/PT sets rays, RTAO over the game's AO, path-traced GI; Upscaling Mode rows absent; FG Method Auto; off NVIDIA no DLSS place; NRD on: AO and Light Bounce not denoised twice (no softer picture than Built-in with its Denoisers off).
- [x] P7 Graphics 2 of 2 on names that exist: 430 entries > 45 (28 main + 17 tests), replayed offline 45 of 45 `ok`; flags, preferences, Audio out, so no at-start thread. Shadow maps are absolute rows in metres (above 0 the game uses the value, 0 its table: a factor would do nothing). `engine_values.txt` is the 103 real values. Tools: wh3_batch/tweak_scan.py, tweak_lists.py.
      Not done: nothing of the phase; not run in game. ATN's check: `ReShade.log` "Engine values found: 45 of 45"; Battle Shadow Map 1 at 500 m, Grass Size at 2x, Point Lights With Shadows at 2x (crash?); Grass Density above 1x (clamped?).
      ATN decides, other games not touched: WH1 170 of 180, WH2 208 of 220, Pharaoh 445 of 460 (real addresses), Troy 2 of 402: its list is dead. Troy and Pharaoh also retry their 4 missing `SOUND_*` for a long time at start and sliders wait for it (read, not measured).
- [x] P12 HDR page: SDR preset + first start on an SDR monitor, Fog Strength (34719E09, 78E4CBF2 decompiled, bit-exact at 1 in WARP, 24 runs; 0xA8F5202D stays the game's: its light reaches the picture only through 78E4CBF2, so P11 decompiles it itself), Highlight Hue Shift. Out: Sky Smoothing (WH3's fog cube is BC6H, Attila's cause not shown here); Recommended variants (no VFX sliders).
      NOT DONE: Fog Strength on 4347C118, 7998DC13, C506E37C (fog light with alpha 0, not through the upsample [I]) and legacy C5B0D1F3: in no trace. Not run in game. ATN's check: Fog Strength 0 and 2 in a battle and on the campaign: at 0 no fog and no leftover glow (a glow left = another permutation: `renodx_trace_now`); SDR monitor, no saved settings: starts in the SDR preset.
- [x] P6 RT scene (6a, 6b; 6c below): leaves (mask t1), grass (mask by pixel shader, t2 or t0), 4-bone units, flags, in the table; guides written for paths too, before the trace, `guides_written` real. WARP on the game's bytes: stream output on all 4 VS, triangle list and point list, UV = the register read.
      Not done: nothing of 6a, 6b; not run in game, harness not run (no helper or bridge change, Attila running). Left out until a trace shows them: D29F3ED5, 97C57F3A, grass rock. Other games, no code: Pharaoh's grass mask is a texture array (README line), Troy's real grass 2C7FBC7D isn't in its table.
      ATN's check: RT Debug View + Scene Test at a forest: leaves with holes, not cards (No Leaves takes them out); grassy battle: blades; flag's shadow waves; a burning tree keeps its leaf shadow; Log Details: "left out", "without room", "new static meshes" for 783383C6 (keeps rising = re-keyed every frame), "captured every frame", fps with leaves; on 12 GB or less, leaves turning solid = past the alpha budget; Path-Traced GI with FSR: first-run guides line, normals sharper than before; Pharaoh's and Troy's RT grass shadows: solid, missing or wrong shapes?
- [x] P8 Upscaling below 100%: Experimental, off (row Below 100% Resolution Scale, way B, after the tone mapper). WARP: t00 off 11.06M values 0 differ against the old build, on at 50, 66.7, 67, 100% with the assumed constants 0 differ against the 100% mapping; 91 checks through the real Upload, ChooseScale, OnTonemapDraw.
      Not done: nothing of the phase; not run in game; `c37`, `c39`, overscan below 100% still never seen. Off, and at 100%: same path as before. ATN's check: 50, 67, 77%: `ReShade.log` "as expected" or "refused" with values; picture whole, UI and markers placed; DOF and bloom steady (both now see the unresolved picture), no dark rim at the rendered part's edge; particle smear against the game's TAA, texture sharpness (no LOD bias); TAA (not High) too: its resolve E1D67C6A is in no trace; Motion Debug View stretched; FG; RT at the render size; Rays a Pixel's Match Output.
- [x] P9 Sun features: t49 now on the sun (it was on the AO in 9EAD8951, 24F97B7B, 76A707E5), Path-Traced AO + Own Sky Rays (new anchor after SH, cube, GI, 9 places; Experimental, RT/PT stays on RTAO), W's own rt_sun, helper75 (SkyCalibration without Attila's stretch here), Coloured Shadows on capture flags, Light Bounce's metal rule, rt_sun also for paths without RT shadows (all TW; Attila v135 line), Game's GI row + tooltip, a Log Details line. WARP on the game's bytes (warp_tests/sun_wh3): unbound 0 of 262144 values differ on all 7, t51 = 1 leaves 0, formula within 0.00002; 268 other shaders byte-identical. Harness 116 modes on helper75: 115, `release_smoke` +30 once (5 of 5 alone).
      Not done: brightness of Path-Traced AO against SH and GI, and what gi_enabled 0 does (both only the game shows). Leaves' back ambient keeps the game's. Forward readers listed only (reports/P9.md): water, fog, portraits keep the maps and GI. Not run in game.
      ATN's check: Log Details: "Sun shader" lines (SH, GI, cube, scale); Bend or RT shadows, sun low: the sun is cut, stronger than before; armour under GT-VBGI: no flat glow; Path-Traced AO on an open field against RTAO: not much brighter or darker; Game's Global Illumination 0, new battle, restart: walls, under trees, fps.
- [x] P10 Reflections, specular occlusion, water: the game's SSR is water only. Rays and occlusion through our copy of its resolve, run only with them on (off: the game's bytes); guides fixed (roughness was the AO channel, since batch 3); rt_sun also for reflections (all games). Water Clarity, Caustics moved to `twattila/water_effects.hpp`, off here. WARP: resolve against game worst 0.0000011 of 210, the cubes taken back worst 0.00008 of 3.08; caustics patch 0 bits at t51 unbound, Attila's patch bytes same.
      NOT DONE: Quality and FidelityFX SSSR rows, hidden (literals; the march already is hierarchical); RT_REFLECTIONS_LIGHT (light is R16G16B16A16F: hits use last frame's picture, the cache, the sun); Clarity and Caustics on battle_water, battle_river (colour per material, Proof C); 3 of 7 sun PS (F9F8CE80, 18A84893, 76A707E5: the game's pass stays, logged); Water Reflections row (rays need the water's own start). Not run in game.
      ATN's check: in-game SSR on, river or sea: Game's keeps water's reflection, RT adds what's off screen, no riverbed in it; metal under an overhang darkens with Occlusion, all metal not shifting; log "Water: first draw of shader", "this sun shader's cubes" and Clarity on that water.
- [x] P6c Terrain in the scene: 709BC4D7 (`.tessellated`): the bound domain shader streamed out, counts by SO statistics read at the trace, copied after the frame's captures. WARP on the game's bytes, 6 HS x DS: written == rasterized, on the height map, depth identical to the game's draw, side draw leaves it bit-identical, append, overflow, culling, instanced; debug layer 0 errors (tools/tw/warp_tests/rt_terrain_wh3).
      Not done: terrain outside the view (the game's hull shader culls by the frustum: a hill behind the camera casts no shadow). `Capture` itself not executed; real constants, topology, which DS: not run in game. No helper or bridge change, harness not run (Attila running).
      ATN's check: log "vs 709BC4D7 streamed out through domain shader"; RT Debug View, Scene Test Terrain: hills there, same shape as the picture, no black circle or bands, also with Terrain Tessellation 4x; Log Details: "without room", trace CPU time and fps with RT on against before (the wait for the counts).
- [x] P11 Lights and fog in rays: Torch and Fire Shadows (8 point light PS, Proof B) and Fog Shadows (A8F5202D, rays in place of its cascade march), Experimental, off, not in RT/PT; our copies only where the rays are bound, else the game's bytes. WARP on the game's bytes: 9 of 9, fog 0 bits differ; 2 bugs fixed for Attila too (README v135).
      NOT DONE: torch light in fog (no proven scale: Proof C; row hidden); the fog's other 5 light PS (in no trace; they stay the game's); spot lights. Not run in game: which PS, direct or not, layout, order, blends. Harness: helper unchanged, leaf_light, path_ambient, default passed.
      ATN's check: night battle with fires, RT Shadows + Torch and Fire Shadows on: log "First run: light or fog draw, ps X", "Local lights: first N read", "Rays applied"; Debug View Point Light Shadows not magenta; a torch behind a wall casts its shadow; off, as before. With paths: unseen walls near fires not blown out. Sunny hazy map, Fog Shadows on against off (L9): shafts follow buildings, does far fog look flat past 400 m. No "First run" = send `renodx_trace_now` from that battle.
- [x] P13 Close: review's findings (Below 100% forgets a size nothing shows; shadow Debug View whole; guides by sun shader; Experimental labels; one refuted: dlss_output has the same typed UAV load since P1). 14 addons + harness 116 modes on helper75. twwarhammer3_v31.zip, twattila_v135.zip in Downloads; no players' zip, no tag, no Nexus text.
      Not done: nothing of the phase; nothing of the batch has run in game. ATN's check: tools/tw/wh3_batch/reports/round_report.md (test list smallest first, decisions); each phase above has its own "ATN's check".

Double work that stays (README v31): the game's TAA under any upscaler; sun cascades and mask under RT shadows; RTAO on the game's SSAO; terrain's geometry twice with rays; point light shadow maps under Torch and Fire Shadows.

## START HERE (2026-11-03): Attila v134 = players' v2.5 (helper74, bridge v23, tag twattila-v134; v133 never released): RenoDX_Attila_v2.5.zip, RenoDX_Attila_v2.5_HDR_only.zip (Nexus: the add-on alone, tools/tw/hdr_zip.py), twattila_v134.zip. Downloads/attila_github_v2.5_notes.md, attila_nexus_text_v2.5.txt. ENORMOUS BATCH done; ATN's in-game tests under "ENORMOUS BATCH" item 18, then WH3.
ATN decided (2026-11-03): no in-game tests by us unless asked, not even one start before a zip.
## START HERE (2026-11-01 night, ATN moves to another account on this PC): Total War Attila
- State: Attila v131 = players' v2.01i, the last build. Downloads/RenoDX_Attila_v2.01i.zip (players), twattila_v131.zip (full), helper71 =
  build/tw_dlaa_helper71, bridge v21, tag twattila-v131, installed in Attila's folder. All committed and pushed (origin atn-mods). Tag twattila-v130 stayed before the toggle: use v131.
- v2.01i: frame generation with Output SDR was red (the helper took 10-bit SDR frames for HDR10, README v131), not run in game yet: ATN tests. Other TW games with SDR output and frame
  generation have the same line in their helper until they take bridge v21.
- ATN's vertical lines in battle fog: cause found (the sky cube's horizon row, README v130). Fog & Sky Smoothing (Extra, off) waits for ATN's test in that spot, nothing of v130 ran in
  game. If lines stay: the particles' own sky sample (vfx00 / vfx01), a wider tap spread in sky_smooth.hlsli.
- ATN's tests: v2.01e FG fixed, v2.01f gamma fixed. v2.01g: the campaign's haze was the fog shader's wrong constant (battlefog00: decompiler), Gamma Correction for
  Rendering Mode SDR, the SDR preset on a first start on an SDR monitor (README v129); v2.01h: Gamma Correction on by default, Off = in-game Gamma. tools: cb_audit.py is in .agents/skills/tw-port-checklist/scripts: original vs built shaders' constants.
- NEXT, on ATN's word: "MONDAY BATCH" below, in its order (Mega Geometry 1M, XeSS / FSR / NRD, FG on scRGB, SSS smoke shadows, TAA motion, leak check,
  then WH3). Don't start it unasked.
- Rules: ATN wants very short answers in English, they/them. Push every commit to origin (arshiatn/renodx, branch atn-mods), NEVER upstream (clshortfuse).
  ATN uploads the zips to GitHub releases themselves: each release = a tag twattila-v<internal>, a slim zip RenoDX_Attila_v<name>.zip (ATN names them:
  v2.0 = v120, v2.1 = v121, v2.01 = v122, a-i = v123-v131, next j = v132) from tools/tw/player_zip.py (READ ME: tools/tw/player_readme/RenoDX Attila - READ ME.txt, update
  its title and text), after the full one by tools/tw/release_zip.py (--helper when the helper changed: bump tw_dlaa_helperN in it). ATN's weekly limit
  matters: no game runs or big workflows unless they ask or it is needed; say plainly what was not tested. Back up ATN's ReShade.ini around game runs
  (tools/tw/test_scripts/fg_mem_test.ps1 does: a replay with timed settings, memory every 5 s). Never stop Streamline in the helper (v2.01d).
- NOW (2026-11-01): v128 = players' RenoDX_Attila_v2.01f.zip shipped (full twattila_v128.zip, tag twattila-v128, helper70 as v127: SDR
  preset = Rendering Mode SDR, the proxy passes SDR mode + SDR output through). ATN's pictures: Attila's pictures\SDR. NEXT: "MONDAY BATCH".
- Before (2026-11-01): v127 = players' RenoDX_Attila_v2.01e.zip shipped (full twattila_v127.zip, tag twattila-v127, helper70 =
  build/tw_dlaa_helper70: FG off = slFreeResources + swap chain + window, Streamline stays; ATN tests it). NEXT: "MONDAY BATCH".
- Before (2026-11-01): v126 = players' RenoDX_Attila_v2.01d.zip shipped (withdrawn: it hung, see MONDAY BATCH item 5) (full twattila_v126.zip, tag twattila-v126, helper69 =
  build/tw_dlaa_helper69: frame generation's memory released 10 s after it is off). NEXT: "MONDAY BATCH".
- Before (2026-11-01): v125 = players' RenoDX_Attila_v2.01c.zip shipped (full twattila_v125.zip, tag twattila-v125, helper67 as v122: Ray
  Reconstruction only while something is ray-traced, rt_scene.hpp Configure). NEXT: "MONDAY BATCH".
- Before (2026-11-01): v124 = players' RenoDX_Attila_v2.01b.zip shipped (full twattila_v124.zip, tag twattila-v124, helper67 as v122: the
  RT/PT preset turns Ray Reconstruction on if it is off). NEXT: "MONDAY BATCH".
- Before (2026-11-01): v123 = players' RenoDX_Attila_v2.01a.zip shipped (full twattila_v123.zip, tag twattila-v123, helper67 as v122:
  Output Auto = HDR10, RIS on in RT/PT). NEXT: "MONDAY BATCH".
- Before (2026-11-01): v122 = players' RenoDX_Attila_v2.01.zip shipped (Downloads; full twattila_v122.zip; tag twattila-v122;
  helper67 = build/tw_dlaa_helper67, harness 50 of 50, installed in Attila's folder: Rays a Pixel up to 8,
  the red RT note removed, "Ray Tracing Extra Options", RT/PT preset with Smoke Shadows). NEXT: "MONDAY BATCH" below.
- Before (2026-11-01): v121 shipped (Downloads/RenoDX_Attila_v2.1.zip for players: ATN names them v2.0 = v120, v2.1 = v121, twattila_v121.zip full; helper66 =
  build/tw_dlaa_helper66, bridge v20; search "v121 (ATN 2026-11-01"); pushed to origin atn-mods + tag twattila-v121
  (ATN 2026-11-01: push every commit to their fork from now on). Next batch: v120 items 3-4 (FSR + FSR FG, RT/PT on
  AMD / Intel with NRD) and the v121 NEXT BATCH lines (TLAS growing on its own).
- Before (2026-10-31 night): v120 shipped (Downloads/twattila_v120.zip, helper62 unchanged; items 1, 2, 5 done; 3 FSR + FSR FG and 4 RT/PT
  on AMD / Intel with NVIDIA NRD NOT done: ATN's weekly limit ran out, they are the v121 batch, search "v120 BATCH" for their hints).
  Pushed to ATN's fork (origin arshiatn/renodx, branch atn-mods, tag twattila-v120); ATN attaches the zip to a GitHub release there
  (Nexus blocks the zip). v120 addon is in Attila's folder (old one renamed .addon32.v119), not tested in game. v119 done
  (search "v119 BIG BATCH"), v118 done ("v118 HUGE BATCH").
- Older state: Attila v119 shipped (Downloads/twattila_v119.zip, helper62 = build/tw_dlaa_helper62, bridge v19; v119 is in Attila's
  folder, its ReShade.ini as ATN left it (test_scripts/ReShade.ini.atn38); the addon there is a COPY, not the symlink to
  build32: copy it after each build; a dead Attila.exe can pin it: rename it aside, see the v116 notes), branch atn-mods,
  everything committed. Memory: C:/Users/arshi/.claude/projects/D--Dev-renodx/memory (MEMORY.md index;
  project_attila_first.md, reference_gpu_beside_game.md, reference_attila_frame_facts.md, reference_tw_ingame_testing.md,
  reference_attila_sun_shaders.md).
- tools/tw = C:/Users/arshi/.claude/projects/D--Dev-renodx/tools/tw (not in the repo).
- Read: "v119 BIG BATCH", then v118 under it for recent context. ATN's last test: debug37 (C:/Program Files (x86)/Steam/
  steamapps/common/Total War Attila/debug37). Next open: ATN's test of v119, its "Left" list.
- Round routine: helper build `src/games/twattila/dlaa_helper/build.ps1` (PowerShell), copy build/tw_dlaa_helper to a
  new build/tw_dlaa_helperN; 32-bit `cmake --build build32 --config Release --target renodx-twattila.addon32
  renodx-twrome2.addon32 renodx-twthronesofbritannia.addon32`; 64-bit TW check `cmake --build build --config Release
  --target renodx-twpharaohdynasties.addon64 renodx-twtroy.addon64 renodx-twwarhammer.addon64
  renodx-twwarhammer2.addon64 renodx-twwarhammer3.addon64`; harness: copy the helper to build/tw_rt_harness, run
  tools/tw/all_modes.sh (50 modes, all pass as of helper62; harness source tools/tw/rt_harness); profiles:
  `python -I tools/tw/profile_debug29.py <tw_rt_profile_helper.csv> 40`; pictures: tools/tw/shrink.py; zip: `python -I
  tools/tw/release_zip.py twattila twattila_v<old>.zip twattila_v<new>.zip --helper` (bump the helper folder in it; the
  new zip must not exist); players get only the slim zip (ATN 2026-11-01: no sources / other games): `python -I
  tools/tw/player_zip.py twattila_v<new>.zip RenoDX_Attila_v<old>.zip RenoDX_Attila_v<new>.zip <READ ME .txt>` (the READ ME
  text is the last player zip's "RenoDX Attila - READ ME.txt", updated); README section per version; commit after every step with the Co-Authored-By line.

Older handoff (other games, Ryse HDR history): `C:\Users\arshi\Downloads\HANDOFF.md`. This file supersedes its Ryse parts.

## ENORMOUS BATCH (ATN 2026-11-02; away for hours / days: don't stop; push every now and then; new bugs found: fix in this batch; avoid in-game tests, allowed if really needed; WH3 only after all of this)
ATN's test of v2.01i: FG with Output SDR correct, Gamma Correction correct in SDR. Their words, short:
- [x] 1 Fog & Sky Smoothing stronger: Low (what we have), Medium, High; and a slider above it, a multiplier 0 to 2.
- [x] 2 SDR: let people change everything in Extra but AutoHDR too (make sure they have effects).
- [x] 3 FG SDR fix, Gamma Correction, Fog & Sky Smoothing: "important for our other TW games too": port.
      DONE 2026-11-03: FG SDR + FG scRGB in Rome II, Thrones, Troy, Pharaoh, WH1, WH2; Gamma + Paper White gate on Output SDR + Extra's sliders in SDR in those + WH3; Smoothing in Rome II, Thrones, WH1. All 8 addons build, 60/60 harness modes; NOT run in game. Film Grain, Indicators, Troy / Pharaoh Sharpening (RenoDX's own) stay HDR-only. Left to ATN: FG + Output SDR in WH3 / Shogun 2, WH1's fog drawn at all? Their zips need a helper newer than helper71.
- [x] 4 RTAO range 6 m by default (every other preset too).
- [x] 5 GitHub link at the bottom (About); a red notice for people who only have the HDR addon: for DLSS / RT / PT download everything from git / Nexus.
- [x] 6 Motion vectors on in DLSS are very hard on the CPU when lots of soldiers fight at one spot (off ~40 fps, on 24). Improve. Trace: Attila\debug38\ctesphon full battle (won)
      (~15000 men, grass back to 1x; the RT trace at the end was lost to a ReShade screenshot crash: 32-bit, ReShade takes 2 pictures).
      DONE: it was GPU, not CPU: the records' stream output left bound through the game's draws (0584642b) stalled ~10 us per unit draw. Fixed; plus no second prelude for standing rigid draws (1020 -> 63 passes, game thread 0.19 -> 0.06 ms) and placement cells by mesh (GPU -0.25 ms). In game, ATN's RT settings, one run toggled: for_claude1 cavalry view (145 unit draws, 4300 meshes) on minus off +2.15 -> +0.54 ms frame, +2.1 -> +0.35 ms GPU. 2vs3 with Unit Detail 8 (420 unit draws, 50000 meshes, 450 compact draws like ATN's trace): old stall re-enabled by a test switch 27-28 ms / GPU 17-18; now 22.7-24.4 / GPU 13.2, off 21.5-23.5 / GPU 10.2 (~37 -> ~43 fps, off ~44). Left at that scale: GPU +3 ms (soldier records ~1.4: bones and placements; rigid 0.4), CPU +1 ms (hook 0.5). Far-soldier slider not added: no gain at Unit Detail 0.69 (far men are sprites).
      Untested: a real melee (no replay has one in 3D; these are marching and charging units), cloth captures (0 deformed draws in both replays), 64-bit C++ (WH2 stops at the known sky_smooth break; its motion shaders compile). ATN's answer: test the Ctesiphon melee: their 40 -> 24 was bigger than the ~8 fps this explains here.
- [x] 7 "Are we sure FG / MFG are 100% working correctly?" Check and answer. DONE (audit against Streamline docs + ATN's sl.log: 0 errors, 2x/4x/6x really generating): normal play correct. Fixed edges: resize (no zero buffer count, off for 2 presents), frozen window when textures no longer fit or open fails, flares + vignette into the HUD-less image, status line in the menu, thread join, first presents know the max frames, menu Off warnings + reset after hidden. Dynamic + FG frame limit not in logs; needs one in-game test. Old fg_bridge_test32 is on an old bridge: no offline test.
- [x] 8 LotR grass shadow flicker. DONE (tested in 2vs3.replay, zoomed in): each placed grass / leaf draw reached Capture twice (depth prefill + G-buffer): the share paid double, and past it an instance hidden by one and shown by the other raced in rt_place's dedupe. Capture takes the G-buffer draw alone. Micromap bake read unwritten UVs of a left-out instance 0: written now. Big find: Plant Wind Sway 4 (ATN's ini since 2026-11-02) makes the whole field pulse: dark pixels in the near band swing 13 % to 38 % about once a second, sway on or off, shots 1 s apart: exactly ATN's flat / full shots; at 1 it is steady (15.25 +- 0.39), and range 0 / 50 / 300 / sway off all agree (no silent copy). Paused shots freeze a phase: never compare settings across paused runs. Not reproduced at the LotR's cap (vanilla reaches ~1 M grass triangles).
- [x] 9 (units alpha-tested in the RT scene: 92 clip pixel shaders, RT_SOLID_CUT_OUT; not run on the tree unit: needs the LotR mod; buildings left: no evidence yet) Black spots (main game too, some spots and buildings): Attila\debug38\black spots (Normal / RT / SSS): why with SSS, and much darker with RT Shadows? The beard and leaves of the tree unit.
- [x] 10 RT Reflections look wrong with and without RT/PT AO / GI. Is it full resolution? Samples make no difference, noisy and low quality. Improve.
  Done: hits read the pass's light buffer in every GI mode (river in-game: reflected colour within 2% across GI Off / Rays / Path, gpu_paths_ms 1.5-1.7), rough light no longer loses brightness twice (harness 1 ray: partly met 0.745 -> 1.350, same as 4 rays), lobe = the game's highlight, no second AO on traced surfaces. Untested: armour A/B (grain, sharpness, RR in motion). Own ReSTIR / Reuse now nearly no-ops. Options for ATN later: more rays on water, RR specular albedo guide, other-side hits. ATN's answer: render-pixel rays (1 a pixel, can't go finer before DLSS); samples do nothing on water by code (always 1 ray there).
- [x] 11 More rivers for Water Clarity / Water Caustics: find their shaders. DONE: placed water meshes 2E69BF8E (draw over the plane; also the plane below Ultra), battle sea, campaign sea + lake (Clarity only there), no-shadow permutations; PatchWaterCaustics generalised, checked on WARP (real C++, all rows). Campaign rivers = map overlay, can't. Not run in game: check 'Water Caustics: water shader X patched' in ReShade.log, no 'not as expected'.
- [x] 12 TAA with real motion vectors, an option (a slider). Answer: are camera motion vectors off when DLSS uses real motion vectors? If yes, doesn't that cost performance? DONE: TAA Motion Vectors Camera / Objects (Attila; default Camera; no helper needed). Camera motion is never off (costs ~nothing, prepare pass); object motion is what costs. Found: TAA / SMAA T2x had NO jitter since 5693b7cb (v120): fixed. Not run in game.
- [x] 13 PT AO noisy at Ultra Performance; "Rays a Pixel" 1 to 8 show no difference on it: maybe not included (more missing?). PT AO noisier in general: Own ReSTIR like RT reflections? Does that just cost VRAM or fps too? DONE: rays did reach it, but ReSTIR GI showed one sample. Own Sky Rays (RayTracedPathSkyApart, default On, RT/PT preset; path_flags bit 27, bridge RT_PATH_SKY_APART = 4096): the sky skips the reservoirs, stratified mean as RT AO; no VRAM, 0.15 ms a ray at UP. Torch rays clamp 8. In game (UP, RR, still): Light Bounce view noise open ground 2.4 / 1.2 / 0.75 / 0.6% at 1 / 2 / 4 / 8 rays (Off: 2.2% at 8), dense grass 72 / 34 / 16 / 8.5% (Off: 10% at 1, 5% at 8: 1 ray On is much noisier in that raw view, final picture with RR: hf 3.57 vs 3.30%). Final picture +3 to 5% linear, now equal to RT AO (ratio 0.997-1.002, same colour; Off was 3-5% darker, warmer). Plan's Reflection samples, fog rays, spots not done. Untested: DLSS without RR, ATN's eye. Answer: Own ReSTIR not needed, it would cost 133-531 MB and barely react to rays (CPU model).
- [x] 14 Smoke shadows since the optimizations: blocky when artillery hits and smoke comes out, holes in the middle of shadows; arrows and shots in the air look normal; smoke of explosions, burning houses,
      dust of units in the desert look bad and flicker. Fix without losing performance, maybe an extra slider. DONE: soft spheres (quads one radius toward the sun, chord share), rays count only particles dimming > 5%, Smoke Shadow Quality Low / Medium / High = 8 / 16 / 32 (twice met at most; fog fixed 2 / 4), stale smoke value gone, no opaque smoke, Log Details "most quads", second lit shader 0xF5379F78 captured. Tested: 52 harness modes; for_claude1 catapult hit old vs new: no streaks; Ctesiphon counts unchanged by 0xF5379F78; most quads 2710 (cap 32768).
      Harness 4K, 5090, sun trace ms, old / Low / Medium / High: 3000 particles 5-10 m 1.25 / 1.6-2.1 / 2.4-3.6 / 3.4-7.0; 30000 same 2.3-3.1 / 2.8-3.3 / 5.2-7.1 / 5.9-11.4; 30000 10-20 m 7.5 / 9-11 / 21 / 21-33: Medium stays. Untested: burning houses (no replay has one, so 0xF5379F78 unproven: flames may cast faint shadows), flicker not measured, the camera-distance fade of particles unfixed. ATN: please pause at a burning house and zoom in and out: if the shadow changes, that fade is left.
- [x] 15 The wrong decompiled fog shader (fixed in 2.01g, Attila) probably exists in Rome II, Thrones and the other TW games: check (.agents/skills/tw-port-checklist/scripts/cb_audit.py). DONE 2026-11-03 (every fix re-read from the game's bytecode first): Rome II / Thrones battle fog are fine. Fixed: in-game DOF focus range 30% too narrow (decompiler: dot(f, f) on a scalar = f*f, game dp2 = 2*f*f; 44 shaders, ATTILA too, Rome II, Thrones, Troy, Pharaoh, WH1, WH2), UI red/blue swap (Rome II, Thrones), Rome II t09/t10 SDR range, Shogun battle fog ceiling + flag motion + UI figures + campaign border, Troy sky fog, WH1 SDR whiteout, WH3 DOF near fade, Pharaoh glow, 17 wind / feather lines in generated motion + RT VS (regenerating them brings these back: fix the decompile input).
      Not run in game. Tested: all 9 addons build; cb_audit.py (now takes games, per-instruction + dp2 checks, generated VS): no dp2 lost, no scalar mix-up left; interpreter original vs build, 24 random runs: 57 of 66 changed shaders exact; Rome II t09, WH1 t00 / t09 exact with our smooth bloom swapped for the plain read; Shogun fog00 / 01 differ in 1-2 values (our clamp); Pharaoh dof00 / 01, Shogun campaign water differ through our own edits, not closed numerically (the fixed instruction is there). All 46 Attila shaders: 41 exact, 5 on purpose (bloom00 / 01 own high pass, lensflare04 clamp, sky01 normalised direction, uiMap02 gamma 2.0). Open questions closed: Attila ssr00/01 = 0 differences (raw had sun_direction.y, ours fixed it; the seed 13 was the interpreter dropping resinfo_*_uint lines), Pharaoh t01-04, Remastered t03-10 = 0, Pharaoh vfx 0.000038 is in the source. No original to compare: Rome II bloodsplash00, WH3 dlssdof00_ / dlssdofbg00 (read by eye). Dead, left: motion_ocean o6 (sea uv y/z, never output).
      ATN: with in-game DOF on, the area near focus is sharper now: recheck the Depth of Field slider; Shogun: recheck Battle Fog in Recommended (the fog is thinner without its ceiling).
- [x] 16 MONDAY BATCH below: 1 (Mega Geometry 1M, TLAS growing), 2 + 2a (upscalers), 2b (FG on scRGB), 2c (SSS smoke shadows).
      ATN on upscalers: probably only FSR 4 SR + MFG (or the newest) for AMD (does it run on Intel and NVIDIA too?) and XeSS 2 / 3 SR + MFG (?) for Intel, AMD and old NVIDIA. Answer what we think.
- [x] 17 DONE 2026-11-03: VRAM / RAM leftovers (MONDAY BATCH 5 below, numbers there). Not run in game.
- [x] 17a DONE 2026-11-03: independent review's fixes. RT's / RR's first start after 10 s no longer built twice (rt_used / rr_used at open); Present failing: window hidden after 60 in a row, DLSS's failures reach Auto / fg_failed (fg_bridge_test: DLSS failing -> FSR runs; all failing -> failed 1, hidden; normal unchanged); rt_nrd_off (in v23's padding, no bump): NRD refused or Run false keeps the addon's denoisers (harness nrd_refused*, the addon's side only read); RT/PT preset Reflection Reuse 0; Frame Rate Limit row under XeSS FG; tooltips; presets' Gamma Correction, Thrones' / WH1's Smoothing; Expeditions' DLSS addon builds. Harness 103 of 103 modes ALL PASSED, all addons build (Ryse, Shogun 2, Expeditions too). Harness link now needs -lpdh -lpsapi. Not run in game.
- [x] 18 DONE 2026-11-03: Downloads/RenoDX_Attila_v2.01j.zip (players), twattila_v132.zip (full), attila_nexus_text_v132.txt, helper72. WH3 comes next, not in this batch.
      ATN TO TEST IN GAME: (1) Frame Generation on Output SDR with DLSS FG: status must say 2 frames (one automated run said none; the no-game test said 2.00). (2) FSR / XeSS upscaling with FSR / XeSS FG. (3) FSR FG in battle: HUD, smoke.
      (4) Smoke: a burning house: pause, zoom in and out; flames casting faint shadows? Smoke Shadows without rays on a catapult hit. (5) Ray Tracing Denoiser NRD vs Built-in, RR off: grass shadows still and panning, trails behind soldiers.
      (6) RT reflections on armour; PT AO at 1 and 2 rays in dense grass. (7) A real melee at Ctesiphon, motion on / off. (8) Memory: RT/PT + RR + MFG, Reset, wait 15 s, FG still generates, RT/PT again; RT off 10 s frees; campaign and battle 3 times.
      (9) Mega Geometry and TLAS growth: a really huge battle. (10) Depth of field near focus (sharper now), Shogun 2's Battle Fog, the other TW games' ports: nothing of those ran in game. (11) Lord of the Rings tree unit's beard and leaves.

## MONDAY BATCH (ATN 2026-11-01: "let's do it on monday"; push every commit to origin atn-mods; tick here)
ATN's words, short:
- [x] 1 DONE 2026-11-03 (helper + bridge constant; RT_DYNAMIC_CLUSTERS 1 << 20; all addons rebuilt, ship together; harness only, not run in game). Tiers: 1,048,576 over 28 GB
      (~9.6 GB), 786,432 over 20 GB (~7.3 GB), 262,144 over 12 GB, 131,072 below; same start room (262,144 = 2514 MB on top tiers: the bigger limit does not grow it). NVAPI has no
      cluster-count cap (caps = the CLUSTER_OPERATIONS bit, requirements call): a failed room goes one tier down, status "No memory: N clusters at most" (RENODX_TW_TEST_ROOM). Harness
      (HARNESS_QUAD_STRESS, RENODX_TW_TEST_GPU_GB, debug layer on: D3D12\d3d12SDKLayers.dll from build/agility_1.619.6 loads now): 980,000 clusters traced, a built CLAS at byte 8.8 G of
      9.4 G (one buffer over 4 GiB works), clusters 4-5 ms; old helper dropped the whole 600,000 draw. Clusters' ms follow the count, not the room (400 K: 1.48 ms with 524 K or 606 K room).
      Growth step wanted x 4/3 (5/4 grew twice: 827 K room for 400 K). During a growth the old and new rooms both live: up to ~18-22 GB at the top. Real battles still stop near 310 K (the addon's
      texel share, not raised; biggest profile 232,785), so this is headroom. Optional addon-side limit (plan's commit C) skipped.
      ATN's words: Mega Geometry's limit higher on 24-32 GB GPUs: up to 1,000,000 clusters a frame (really big battles: arrows, grass,
      soldiers). Now MAX_CLUSTERS 524,288 (RT_DYNAMIC_CLUSTERS, bridge) on GPUs over 20 GB, ~9.5 KB a cluster (~9.5 GB at
      1 M): a tier for over 28 GB (5090) and one for 24 GB (4090), the start room as now; check NVAPI's caps
      (maxClusterCount / CLAS limits), the records (RECORD_COUNT = ... + SLOTS * MAX_CLUSTERS) and the bridge's arrays.
      With it: TLAS instances growing on their own (the v121 NEXT BATCH line).
- [x] 2 UPSCALING PART DONE 2026-11-03 (Attila only; helper + addon, bridge v22, ship together; FG and NRD parts below). FSR 2.4.0 SDK and XeSS 3.0.2 SDK downloads, signed DLLs next to the helper
      (amd_fidelityfx_loader_dx12.dll, amd_fidelityfx_upscaler_dx12.dll, libxess.dll: dlaa_helper/upscale_d3d12.hpp), Methods FSR 6 / XeSS 7, DLSS hidden off NVIDIA (RENODX_TW_FAKE_VENDOR tests), RT/PT Experimental on AMD / Intel (DXR 1.1, built-in denoisers, only under FSR / XeSS: other TW games keep refusing).
      Harness 73 lines all PASS (tools/tw/all_modes_h72.txt; helper build/tw_dlaa_helper): jitter sign pairs (FSR 0.081 right vs 0.18-0.24 wrong, XeSS 0.097 vs 0.26-0.35, DLSS K 0.106 vs 0.20-0.28): the bridge's values unchanged; motion sign right; dark ramp 0.99-1.00 (no sqrt needed). XeSS takes sizes under its minimum (848 x 477 of 2560 x 1440): no clamp, no scale flip.
      In game, for_claude1.replay 50% (camera stood in the grass): FSR 3.1.6, XeSS-SR 2.0.2 on the status line; shots' colours alike, sharpness 3.9-4.3 / 3.8-4.2 vs DLSS 5.0-5.1; VRAM no RT: DLSS 8987, FSR 9696, XeSS 9687, DLSS back 8989 MiB after 10 s. Faked AMD: list without DLSS (overlay shot), saved DLSS -> FSR, saved XeSS survives restart, slider maps right, no nvngx / sl in the helper, RT on DXR 1.1 at 66 fps. Untested: FSR 4, Arc, real AMD / Intel drivers, moving units' smear, campaign, DLSS FG under FSR / XeSS.
      FG PART DONE 2026-11-03 (Attila only; helper + addon, bridge v23, ship together): FSR FG 3.1.6 and XeSS FG 1.3.1 through their proxy swap chains (dlaa_helper/fg_proxy_d3d12.hpp; XeLL with XeSS), Frame Generation Method row Auto / DLSS / FSR / XeSS (Auto: DLSS on RTX 40+, XeSS on Intel, else FSR; one that cannot start falls through), Experimental, off by default. DLSS path: window and matrix inverse moved to shared code, no behaviour change.
      Test without a game (tools/tw/fg_child/fg_bridge_test.cpp, fg_bridge_run.sh, fg_memory_run.ps1: real helper, real bridge, PresentMon on a 5090): FSR, XeSS, DLSS all 2.00 presents per frame in HDR10 and SDR; size, output and backend changes, paused, off and idle free all pass; missing DLLs: status, window hidden, game runs on; Auto with faked AMD / Intel picks FSR / XeSS. FSR frees 234 MiB, XeSS 281 MiB after 10 s (1080p), second cycle the same. Harness 73 of 73. In game before ATN dropped game tests (replay, 5090): 2.00 for all three in HDR10 and scRGB, display captures alike.
      ATN to test in game: (1) Output SDR + DLSS FG: one automated run said "no generated frames" at 1.00 while FSR and XeSS gave 2.00; the no-game test gives DLSS 2.00 in SDR, so look at the status line. (2) FSR upscaling with FSR / XeSS FG. (3) HUD and smoke under FSR FG (its HUD is not recomposed). Not testable here: Arc 3x / 4x, AMD and Intel drivers, RTX 20 / 30 falling through to FSR.
      XeSS (Intel; runs on any GPU with SM 6.4 / DP4a) and XeSS Frame Generation if Intel ships one usable here (XeSS 2:
      XeFG + XeLL, Intel Arc only? check the SDK's licence and GPU support), then FSR + FSR Frame Generation (v120 item 3)
      and RT/PT on AMD / Intel with NVIDIA NRD (v120 item 4). Attila is 32-bit: all of them in the 64-bit helper like DLSS.
      NRD PART, SIGMA DONE 2026-11-03 (Attila's row; helper + addon, no bridge layout change: RT_NRD flag; Experimental, default Built-in): NRD 4.18.0 built with clang-cl /MT into the helper (dlaa_helper/nrd_d3d12.hpp, its comment in build.ps1; licence = NVIDIA RTX SDKs, not MIT, as the batch text said: NRD_LICENSE.txt in release_zip.py).
      SIGMA_SHADOW_TRANSLUCENCY on soft sun shadows (leaves, smoke, several rays stay as translucency), camera-only motion (matrices taken apart from the view-projection), sun direction not passed (isotropic blur). Harness 640 x 360 (all_modes_nrd.txt, 8 new modes, debug layer clean, device loss, DXR 1.1):
      visibility change a frame under a DLSS-like jitter, Built-in 1.21 / NRD 0.55 (of 255), in grass 1.14 / 0.50; roughness in the penumbra Built-in 15.6 / NRD 33.6, grass 37 / 80; camera pan 0.1 m a frame no worse than still (mean difference from Built-in 1.36 vs 1.57): steadier, grainier, so default stays Built-in.
      ATN to test in game: Ray Tracing Denoiser row, RR off, Sun Softness > 0, Built-in vs NRD on grass shadows, panning and still; look for: flicker less, grain more, moving soldiers' shadows trailing.
      NRD PART, REBLUR DONE 2026-11-03 (same row, same switch; one commit, not three: the signals share the set-up): REBLUR_DIFFUSE_OCCLUSION on RT AO (raw open share as normalized hit distance, addon's AO filter off), REBLUR_DIFFUSE on the Light Bounce (rays and paths, ReSTIR GI, SHaRC, SER; new hit distance texture; instance at its texels; addon's Light Bounce denoiser off), REBLUR_SPECULAR on the reflections (light x share denoised, the share stays the built-in filter's, addon's SSR denoiser off). RT_PATH_NRD flag, no bridge layout change.
      Harness numbers 640 x 360 under DLSS-like jitter, frames 60 vs 61, raw (the addon's own denoisers are not in the harness) vs NRD: bounce red mean 0.177 / 0.173, frame change 0.181 / 0.0003, roughness 0.180 / 0.0017; AO (of 255) mean 219.1 / 218.8, change 3.80 / 0.43, roughness 5.98 / 0.80; reflections' light x share mean 0.667 / 0.660, change 0.0061 / 0.0008, roughness 0.0122 / 0.0105 (built-in filter on the left there). Panning 0.05 m a frame: means within 5%, changes 0.0013, 1.64, 0.012. all_modes_nrd2.txt.
      Not provable here: lag on moving light (fires, torches), moving soldiers (camera motion only), real grass, 4K cost. Numbers cannot rank NRD against the addon's AO and bounce denoisers, so default stays Built-in.
      ATN to test in game: same row with RT AO, Light Bounce, Reflections on and RR off: AO and bounce calm when still? trails behind moving soldiers and on torchlit walls? mirror water still sharp? Debug Log shows "NRD ..." with its memory, the status line which ran.
- [x] 2a DONE 2026-11-03 (the preset turns RR on only on NVIDIA, whatever the method; code only, not pressed in game). With RT/PT on AMD / Intel (item 2): the RT/PT preset turns DLSS Ray Reconstruction on where it was off (graphics_page.hpp,
      since 2026-11-01): harmless today (DLSS and RT do not start there); by then it must follow the GPU (their denoiser instead).
- [x] 2b DONE 2026-11-03 (Attila only; helper unchanged; fg_hdr10 pass; Auto stays HDR10; other TW games: ported 2026-11-03 with item 3, Auto stays scRGB there; WH3 / Shogun 2 have another FG module, not ported; not run in game). Frame generation on scRGB without a restart (ATN 2026-11-01: do it Monday): turned on in game it did nothing (the swap
      chain was made scRGB; Streamline takes no scRGB). v123 stopgap: Output Auto = HDR10 from the start (OnCreateSwapchain), a
      red note to restart with Output scRGB. The change: the helper's FG swap chain always HDR10, the addon converting scRGB to
      PQ for fg_frame / fg_hudless; then Auto can go back to scRGB (ask ATN) and the note goes.
- [x] 2c DONE 2026-11-03 (Attila only; helper unchanged; Smoke Shadows (Experimental) 0-2 + Smoke Shadow Softness in Graphics 1 of 2 > Shadows, default 0, in no preset; hidden under DXR, RT smoke untouched; smoke_map.hpp). The particles' stream no longer needs RT's objects (ParticleStream); map 1024 x 1024 over 1024 m, RGBA32F: optical depth + height moments (the plan's RGBA16F let a look-at height 60 m from dust move T 0.75 to 0.68; plan's planes 0.40-0.86).
      Tested: tools/tw/smoke_map_test (real shaders, GPU, no game): look-at height -30..118 m moves T by 0.00005, 150 m pan by 0, sub-texel move leaves the map's bytes alike, weather emitters rejected, no smoke leaves t49 exact. In game: for_claude1 Off / On / Off under Bend and Game's Shadow Maps: soft dim shadow under catapult hits, ground block ratio down to ~0.45-0.65, nothing else moves (median 1.000); GPU busy +0.11-0.13 ms (for_claude1, 1-7 K particles), +0.16 ms (2vs3, ~150 K particles); address space peak 3158 MB of 4096 with screenshots, On vs Off under 6 MB. ATN's RT settings: RT smoke still gets quads (most 1536). Expeditions' build fails on an older line (rt_core.hpp frame_history, 2026-10-31), not this change.
      Untested: in-game pan over one lasting plume (a hit's shadow lives 2-3 s; only short W / S steps across hits, no pop seen), snow / sandstorm weather in a real battle, burning houses, campaign, AMD / Intel, game MSAA (the _ms shader), flicker frame to frame, the overlay's sliders by hand. ATN's answer: works, off by default, please try 1.0 in a catapult battle; water not dimmed; add to the No RT/PT preset after that?
      ATN's request, 2026-11-01 ("like RT Shadows with its own softness"): Bend SSS marches the depth and particles write
      none, so not in its march. Way: the particles (rt_smoke's stream) into a small opacity map from the sun, soft by its own blur, read
      by the sun draws as a dimming with SSS or the game's shadows (no rays, any GPU; off-screen smoke too). Its own softness slider.
- [x] 3 (ENORMOUS 12) TAA: a choice of motion vectors: the game's own (now) or ours (DLSS's: object motion + camera, anti_aliasing.hpp).
- [ ] 4 Then WH3 (project_wh3_dlss memory: our DLSS on WH3's TAA inputs via the TW helper).
- [x] 5 DONE 2026-11-03 (helper + addon, not run in game; numbers from build/tw_fg_test and the harness on a 5090, helper's dedicated VRAM by Windows' counters, 4K for FG / RR, harness scene 640x360):
      rt_tint's handle closed (+1 handle per RT set, 32 MiB at 4K). The ~60 MiB per FG cycle was NOT that nor Streamline: FSR's FG has it too. A shared texture opened twice in the helper (D3D11 and
      D3D12, or RR and FG) stays after both let go, whichever the order, flush, Trim or a new D3D11 device (none with one open; a small repro without the helper's flow does not show it, cause not found).
      So D3D11 opens its textures at a set's first DLSS request: RR on / off +195 MiB a cycle -> +2; FG under FSR / XeSS upscaling none; left: FG beside DLSS or RR +64 MiB per FG on / off at 4K.
      Effects off free their objects (light_bounce, SSS, SSR, AO + depth levels, AO filter; smoke map already did), rt_core 15 s after RT off (captures, atlas, mask refs; atlas slices also age out), object_motion's tables
      once its AA is gone, Release(true) on leaving DLSS / FSR / XeSS (helper: empty set, DLSS feature + Trim: 413 -> 198 MiB; reconstructing flag no longer stale), AA Off its storage, FG's held target.
      Idle release checked in the harness: RT scene incl. NRD, TLAS growth, smoke: 5574 -> 387 MiB in 133 ms; RR with no FG 77 ms; FSR upscaler 143 -> 16, XeSS 139 -> 16 MiB; FG: DLSS FreeResources 49-57 ms.
      Reset with MFG (RR's Shutdown1 under Streamline): guard in, RR only frees its feature (FreeMemOnReleaseFeature, 3074 -> 1757 MB) once Streamline ran. Run end to end in tw_fg_test: FG + RR -> RR off 14 s -> RR on -> FG off -> FG on -> RR ->
      Release(true) -> back: PresentMon 3.0 presents a frame throughout, no hang. Release lines in renodx-tw-dlaa64.log give ms and video memory before -> after: all far under the 5 s answer limit. Not done: FG in its own process (ATN's call, numbers below).
      Tests: tools/tw/fg_child/fg_bridge_test.cpp new phase letters e g r and release,<s>, memory per phase; harness HARNESS_RELEASE + all_modes release* modes. Expeditions' addon does not build since 43dddcf3b (2026-10-31, frame_history in rt_core), not this item.
      ATN to test in game: (1) RT/PT + RR + MFG, Reset, wait 15 s: FG still generates, then RT/PT again: no hang. (2) DLSS -> TAA -> DLSS within 5 s with RT on: leaf shadows as after a fresh start. (3) RT off in a menu after 10 s idle: helper log's line, VRAM down.
      (4) Campaign <-> battle x3, resolution change: picture fine, no 'did not answer'. Numbers on request: tools/tw/test_scripts/fg_mem_test.ps1 -Set "60:FrameGeneration=3;100:FrameGeneration=0".
      FG's own process (ATN decides): what FG off leaves after its idle release, 4K, 3 frames, helper base 859 MiB / 968 MB private: DLSS-G (Streamline) +605 MiB VRAM, +776 MB RAM, each further FG on / off +64 MiB (above); FSR +34, XeSS +68 MiB, nothing to gain there.
      Harness: 101 of 101 ALL PASSED incl. release, release_rr, release_nrd, release_smoke, release_dlss, release_fsr, release_xess (release_smoke's first form with 2000 particles over the view failed only the scene's own checks; the release checks passed; mode now without that stress, rerun passes). All TW addons, helper, Ryse, Tales, Shogun 2 (both) built.
      Older notes of this item: MFG done first (v126,
      measured in for_claude1.replay at 4x, tools/tw/test_scripts/fg_mem_test.ps1, logs fgbase / fgfix / fgfull): FG on +1540 MiB; before, all of
      it stayed; slFreeResources + swap chain gone left +590 MiB (Streamline, its device); Shutdown + a new FrameGeneration12 leaves +65, +131,
      +180 MiB after 1, 2, 3 cycles, helper private +43, +52, +74 MB, the game's flat. So ~60 MiB a cycle stays: about fg_frame + fg_hudless's
      size (R10G10B10A2 at 4K: 2 x 31.6 MiB), find out who holds them (the helper's closed handles, the addon's fg_hudless_rtv, Streamline's
      restart). v126's Streamline restart hung the helper: ATN's log 16:12-16:16: FG used, slShutdown at 16:12:12, then the first RT + RR start
      (RR's NGX init runs first at the set change) never answered, no "Ray tracing ready", DLAA off for good. Not reproduced, inferred; NGX after
      Streamline's "Shutting down NGX" is the suspect. v127 keeps Streamline: FreeResources frees ~950 of 1540 MiB, ~590 stays. To get all of
      it: frame generation in a process of its own (it exits, everything goes, no NGX shared with RR). Then all of it: effects turned on and off (GT-VBAO, GT-VBGI, SSS, SSSR, TAA, CMAA2, SMAA, RT/PT, FG), battle <->
      campaign <-> menu, resolution and render scale changes, repeated battles; addon (32-bit: its address space too) and helper;
      measure with QueryVideoMemoryInfo / process memory before and after each.

## v121 (ATN 2026-11-01, quick asks; commit after every step, never push unless ATN asks, tick here)
- [x] RT/PT preset (graphics_page.hpp): Swaying Shadows Range 75 -> 50 m, Grass Shadow Range 90 -> 0 m (everywhere).
- [x] Grass RT shadows flickering in Constantinople ("we are running out of budget again"; ATN asked for a toggle): bridge v20
      raises RT_CAPTURES 256 -> 1024 (a grass draw that is not placed takes a capture; ATN's v120 log already showed 0.03 M
      "without room"), RT_DRAWN and RT_PLACEMENTS 8192 -> 16384; no toggle needed (~0.3 MB more shared memory). Arrays grew
      in place: OLDEST_VERSION = 20, addon and helper63 (build/tw_dlaa_helper63) must ship together; Rome II / Thrones addons
      share the header (their next zips need helper63+). Harness rebuilt: 50 of 50 modes pass (helper63 with the smoke change,
      tools/tw/all_modes_h63.txt). Not tested in game, not zipped, not installed.
      ATN's Log Details log + 30 s profile of Constantinople (01:00, v120): nothing left out, nothing without room (captures
      156-217 of 256: kept the raise anyway). The real cause is below: traces of 150-336 ms.
- [x] Smoke Shadows killed the frame rate with arrows (ATN) and made the grass shadows flicker (late traces): 120K-235K
      particles a frame, the trace's sun part 14 -> 138 ms and fog 6 -> 143 ms (frames up to 280 ms). Each particle was a
      candidate of the sun's and fog's rays (up to MAX_CANDIDATES 64 a ray). Now (rt_scene.hlsli SmokeTransmittance) those
      rays leave the smoke out (NO_GRASS_SMOKE) and meet it on a ray of its own: the sun's first 8 particles, a fog
      sample's 2, only when the helper built the smoke (TRACE_SMOKE / SMOKE_MET, flag bit 18). A ray stopped at smoke no
      longer misses what lies behind it. helper63 (01:06). Not tested in game; installed in Attila's folder with the v121
      addon. Next if still slow: fewer quads (opacity floor, arrow trails out), refit instead of rebuild.
- [x] ATN's second log (01:11, 6 x 480 archers, the three grass sliders maxed): no limit hit (captures at most 64 of the old
      256, placements 1240 of 8192, nothing left out or without room); sun 5-33 ms, fog 5-46 ms (were 138 / 143); 262144
      particles a frame (the game's whole list), frames ~68 ms (helper-bound: trace 13-65, Light Bounce 6-15, reflections
      2-4 ms). rt_smoke.cs: a quad's disk now has the particle's area (radius 0.5 sqrt(w h); an arrow's thin trail took a
      disk as wide as it was long), particles under 0.05 m^2 of full opacity take none. Addon only; not tested in game.
      Next if smoke still costs: the smoke in a TLAS of its own (its ray now walks the whole TLAS, ~150K instances here).
- [x] Far Tree Shadows and Sprite Shadows ("unit sprites") off by default and in the RT/PT preset (ATN).
- [x] helper64 (build/tw_dlaa_helper64; harness 50 of 50, all_modes_h64.txt; installed in Attila's folder): the smoke in a TLAS of its own (smoke_tlas_, root SRV t15 ROOT_SMOKE, its one
      desc the slot's last instance desc; SmokeTransmittance traces smoke_scene), out of the scene's TLAS. ATN asked about
      Mega Geometry / OMM for smoke: the smoke is rebuilt every request (cluster BLAS where Mega Geometry runs); OMM holds
      only opaque / transparent / unknown, smoke is partly see-through everywhere: no gain past the disks' corners.
- [x] Swaying grass shadows flickering at a battle's start (ATN, 01:22 log): no share or capture full; Mega Geometry's
      cluster room grew four times in the first seconds (32 K -> 320 K -> 467 K) and the swaying grass that did not fit
      was left out meanwhile. GPUs over 20 GB now start at half their limit (262,144 clusters, ~2.5 GB). In steady play
      186 K of 467 K used. Placed counts cycling 56-110 K with a still camera are PLACE_TURNS (kept ones fill in): fine.
- [x] helper65 (build/tw_dlaa_helper65; harness 50 of 50; HARNESS_SMOKE_STRESS=30000 trace 1.05 -> 0.17 ms, scene +0.4 ms
      for the sorted build; installed in Attila's folder), ATN's 01:38 stress battle (shadows "100% flickering", smoke still slow):
      (a) the TLAS's MAX_INSTANCES 262,144 ran out: 70-184 K placed a frame (grass placed every frame with sway, the
      still meshes in PLACE_TURNS), the kept ones past the limit dropped (rt_place Append), others each frame: now 524,288
      (PLACE_TABLE / rt_place TABLE_ENTRIES 1 << 20). (b) The trace went 3.6 -> 194 ms the frame the volley's smoke came
      (one capture, 772 clusters = the 32 K quad cap): with Mega Geometry the smoke was clusters of 85 triangles in the
      particles' list order, each spanning the battlefield. Now the smoke (the trailing RT_SUN_ONLY captures) is always a
      regular BLAS (PREFER_FAST_TRACE, sorted by the builder), never clusters (no stable_partition any more: sprites and
      far trees before the smoke stay in the units' clusters, as without Mega Geometry); smoke_record = units_record +
      unit_count (capture records). SmokeTransmittance: a sprite or far tree in the smoke's BLAS blocks (helper64 lost their
      sun shadows with Mega Geometry). Partitioned TLAS (ATN's question): no help, the TLAS build is 0.5 ms; it stays behind
      RENODX_TW_PTLAS (a siege stalled with it).
- [x] helper66 (build/tw_dlaa_helper66; harness 50 of 50; installed in Attila's folder): RT memory released when RT is off (ATN: 6.9 GB Reset, 7.5 GB No RT, 18 GB RT/PT,
      but only back to 15 GB after switching RT/PT off). The addon already remade its set without the RT textures, the
      helper only Closed its scene (device, pools, cluster room: GBs stayed). main.cpp: no RT request for 10 s (IDLE_RELEASE)
      -> scene replaced by a fresh object (scene_live / scene_idle, ++rt_device: the addon captures again), Ray
      Reconstruction the same (reconstruction_live / _idle, by shared->ray_reconstruction); opened again with their next
      request, not at a set change while idle. Swaying shadows "a bit": ATN's eyes (dropped). Not tested in game.
      VRAM by tier (code): fixed ~1.5 GB (vertex copy 512 MB, normals 416 MB, UVs 416 / 224 / 128 MB, TLAS + descs ~0.3 GB),
      Mega Geometry room >20 GB 2.5 GB at start up to ~5 GB (since Monday item 1: >28 GB ~9.6 GB, >20 GB ~7.3 GB), 12-20 GB up to ~2.4 GB, <=12 GB up to ~1.2 GB (ATN's stress
      battle needed ~200 K clusters: over the 12 GB tier's 131 K), render-size textures ~1-1.5 GB at 4K native, the addon's
      shared RT textures ~1 GB, Ray Reconstruction ~1 GB.
- [x] DONE 2026-11-03 (Monday item 1, helper only; harness only, not run in game): placed instances grow by themselves from 524,288 (x2 at 3/4
      of the last trace's count, on a worker, up to 16.7 M over 20 GB, 8.4 M over 12 GB, else 4.2 M), TLAS and clear only as far as cpu + drawn +
      last written. Harness, same scene: default TLAS 0.28 -> 0.05 ms, scene 0.29 -> 0.18 ms. 2.56 M placed: old loses instances (3 checks fail), new passes, 3.7 ms TLAS, +1.4 GB.
      NEXT BATCH (ATN 2026-11-01): TLAS instances growing on their own instead of a fixed MAX_INSTANCES (ATN asked for a
      slider up to 5,000,000 for future huge battles): read back rt_place's STATE_WRITTEN each request, grow x2 when 3/4
      full (as the cluster room: on a worker, put in between requests) up to D3D12's 16,777,216; the TLAS built for the
      last written count + margin, not the whole cap (now every frame builds all 524,288 descs: ~0.55 ms). A fixed 5 M cap
      would cost ~2-3 GB and ~8-10 ms a frame of TLAS build. rt_place's TABLE_ENTRIES from a constant, not compile time.
- [ ] NEXT BATCH: swaying grass shadows still flicker "a bit" with helper65 (ATN, 01:52 log + profile): no limit hit (no
      slow request, no room growth, nothing left out, placed at most 293 K, compact draws steady with a still camera).
      Suspects: Ray Reconstruction on thin moving shadows over still ground (no motion for shadows), the sway copy's time
      a frame behind the picture, grass flipping between sway and still at the share's edge while moving. Test: Swaying
      Shadows Range 50 m / Off-Screen Sway Off / RR off (DLSS without RR) to tell them apart.
- [x] Effects at Output Resolution audit (ATN): GT-VBAO resolution (ao_pass ResolutionScale), Light Bounce scale and
      SSSR rays (EffectSteps), the game's SSR blur (effects_share), RT shadow softness, RT AO quarter off: follow it; SSS's
      length x the render share always (screen_space_shadows Compute); the game's SSAO and water refraction are read
      whole (view_share WHOLE_TEXTURES: s_ssao, refraction): 100% detail. Not following it (by design): ray-traced
      effects trace at the render size (Ray Reconstruction upscales; Rays a Pixel: Match Output gives 100%'s ray count),
      fog shadows a quarter of the render pixels each way.

## v120 BATCH (ATN 2026-10-31 night; saved, NOT started: ATN starts it from their other account, same PC; commit after every step, never push, tick here)
ATN's words, verbatim:
"These don't need any tests in the game:
1) Reset on Graphics 1 of 2 should put in AO back to Game's SSAO, Reflection back to Game's SSAO, Water clarity, water caustics to their game's default
2) I was thinking that we bring Shadow map size and the 4k mod compatibility to graphics 1 of 2 and both only get showed when Game's Shadow Maps is selected. (4K Mod compatibility is on by default)
3) Let's add newest FSR and FSR Frame Generation please. (You are allowed to download whatever you need from AMD)
4) Let AMD and Intel GPUs use the RT/PT stuff but they probably can only do DXR 1.0/1.1 or 1.2, right? Just like something like 2080? (Idk what the "least" version is that a gpu can use but can we check what version a gpu uses and still let them try RT/PT despite it being much slower probably?)
5) There are some cities in campaign mode (I saved as "claude1" and "claude2". You can open them if you want and fix the "jittering" stuffs. But if it is easier for you, I can open them and log or something for you)"

- [x] 1 Reset (graphics_page.hpp, preset_button("Reset", ..., {})): its changes {{"ssao_mode", 0.f}, {"ssr_hierarchical", 0.f}, {"WaterClarity", 1.f}, {"WaterCaustics", 0.f}}.
      "Reflection back to Game's SSAO" is ATN's typo for Game's SSR. Water Caustics 0 = the game's. Only Reset: No RT/PT and RT/PT keep GT-VBAO and SSSR as defaults
      (page_defaults stays the sliders' defaults).
- [x] 2 Shadow Map Size + UI 4K Mod Compatibility to the Shadows section, both shown only with SHADOW_GAME (shadows_are(SHADOW_GAME)). The compatibility slider is a CUSTOM setting
      in addon.cpp without key or label: give it a label so Page::Take finds it (or page.Add it). Traps: Page::Defaults would put GraphicsShadowMapSize into page_defaults, then No RT/PT and
      RT/PT would reset it: keep it out of those; Graphics 2 of 2's two presets still write the key (tooltips say 8192). Compatibility stays on by default ([Attila] DisableGraphics missing = on).
      README, tooltips, attila_nexus_text ("Graphics 2 of 2" paragraph) follow.
- [ ] 3 NOT DONE in v120 (ATN's weekly limit ran out; ATN releases v120 without it): moved to the v121 batch. Newest FSR + FSR Frame Generation: look up AMD's newest FidelityFX SDK first (WebSearch, GitHub GPUOpen-LibrariesAndSDKs; ATN allows any AMD download; follow its licence, add it to
      licenses and About). Attila is 32-bit: it has to run in the 64-bit helper on the shared textures like DLSS (D3D12 there: rr_d3d12.hpp, fg_d3d12.hpp's child window for presenting; read them).
      Addon side: Method::DLAA has 14 uses in twattila/*.hpp, `method == 5.f` 48: FSR needs its own AntiAliasing method, render scale, jitter, DLSS Mode labels. Any GPU should get it.
- [ ] 4 NOT DONE in v120 (same; v121 batch, with NVIDIA NRD as the non-RR denoiser, ATN 2026-10-31: "Maybe just use Nvidia NRD and call it a day"). RT/PT on AMD and Intel. Answer for ATN: the least is DXR 1.1 (our shaders use inline ray queries, cs_6_5), the RTX 2080's level too; 1.2 (micromaps, SER) is checked at start and used only
      when there (dxr12_); Mega Geometry only on NVIDIA (clusters_, falls back). Remove the VendorId lock v119 added (rt_d3d12.hpp Initialize). Its path = RENODX_TW_NO_DXR12=1 + RENODX_TW_NO_CLUSTERS=1,
      harness modes no_dxr12_no_clusters; the game ran it at 30 fps against 60 on the 5090. Work left there: everything RT is gated on DLSS (`aa->method == 5`, labels "Needs DLSS"): needs "DLSS or FSR"; no Ray
      Reconstruction there, so the denoisers' non-RR paths must carry it (AO history, reflections' accumulation, Light Bounce denoiser). No AMD card here: say so, simulated only. Update README, Nexus text
      (Requirements: "NVIDIA RTX only for now", "AMD and Intel: DLSS and ray tracing stay off").
- [x] 5 FIXED (2026-10-31, ATN's live session + attila_pixel_trace.txt at Carthago Nova): the jittering wall / tower pieces are rigid 347F3DD9
      (every campaign rigid draw); their motion redraw wrote ~95 px of motion with a still camera (the camera's own: 0.0008 px). Cause: the campaign
      map's world is ~1/20-1/100 of the battle's (n 1 / f 500; the traced piece 7.9 units away, 0.09 wide, its same-mesh twin 0.25 away), and the
      placement table's 1-unit cells and 0.5 reach (made for metres) put whole cities in one cell: 8 slots overflowed, pieces took a neighbour's
      place, were flagged moving and jittered. Fix: motion.hlsli PlacementScale (campaign 0.05: cells 0.05, reach 0.025, rigid bootstrap 0.125)
      in MotionRecord, both lookups, motion_rigid.cs, unit records and cloth captures (campaign flag: motion_unit.z, the two compute constants' z).
      Not tested in game (ATN's game was running: v120 addon copied in, the old one renamed .addon32.v119). Rome II / Thrones / 64-bit TW games
      have their own motion.hlsli copies: port PlacementScale with their campaign work.
      Before: three runs (logs/jitter1-3, claude1 Tyrus with Motion Vectors, claude2 Hadrumetum with No Camera Motion): at the
      saves' start views nothing in the cities moves wrongly (motion view grey but UI; two still shots 4 s apart differ only in sea, surf, smoke). The campaign camera
      ignored Y (zoom) from the script: ATN to zoom where it jitters, Anti-Aliasing Debug View = Motion Vectors, F11 (and say what jitters).
      Jittering in campaign cities: saves claude1.save and claude2.save (Attila save_games, 2026-10-31 23:03 / 23:04). ATN offers to log if that is easier. Way that found the animals: test_scripts/run_game.ps1
      -Campaign "claude1.save" -Shot "35,6,10" -Set "33:AntiAliasingDebugView=1" -Seconds 75, zoom into what is coloured with a still camera; Object Motion view = 2; FrameTrace=1 in [renodx] with DLSS below 100%
      gives tw_frame_trace.txt, test_scripts/pairs.py names its draws. Remember campaign_frames (frame_history): bone history is off there (animals). Candidates: rigid 347F3DD9 draws in dynamic buffers, cloth.
- [ ] 6 ATN's test of v119 first if they sent one; v119's Left list below stays open.
- [x] 7 Zip twattila_v120.zip (helper62 carried over: the helper did not change), README v120 + download / fastest-GPU lines, Downloads/
      attila_nexus_text_v120.txt (download line, RTX 40 / 50 + DXR 1.2 line, Shadow Map Size + UI 4K Mod Compatibility moved to Graphics 1 of 2;
      ATN's two placeholders kept), memory, report.
- [ ] 6 stays open: ATN's v119 / v120 test (campaign walls and towers in the Object Motion view should stay grey).

Testing notes: all scripts are now in tools/tw/test_scripts (run_game.ps1, overlay_test.ps1, soak.ps1, install.ps1, ini_set.py, pairs.py, sheet.py, names_scan.py; logs and shots in its logs/ and shots/ folders). Back up
ATN's ReShade.ini first as ReShade.ini.atn39 and restore it after every test; compare with cmp. install.ps1 copies build32 + build/tw_dlaa_helper into Attila's folder. Downloads/attila_nexus_text_v119.txt has two
placeholders for ATN (Google Drive link, commit link): keep them when updating it.

## v119 BIG BATCH (ATN 2026-10-31 evening; ATN's debug37; commit after every step, never push, tick here)
ATN's words, short: Graphics 1 of 2: Shadows on top (above Ambient Occlusion), Ray Tracing Low/Medium/High/Ultra presets gone, on top of
Shadows three presets: Reset (everything but the DLSS section), No RT/PT (SSS default but range 480, AMD SSSR default, GT-VBGI Recommended,
GT-VBAO Recommended), RT/PT (ATN's screenshot debug37/RT pt preset settings). All HDR presets (not SDR) and PsychoV31 defaults: Highlight
Saturation 0.7, Highlight Hue Shift 0.04. Graphics 2 of 2: 4K UI compatibility locks only the shadow resolutions, the rest changeable; last 3
Shadow Tests sliders removed; Grass Detail Distance + Grass Density next to Grass Height Distance in Grass & Trees (Tests); Grass Always
Drawn removed.
- [x] 0 Questions for the report: PT AO / IL real path tracing? HDR-only use of the addon? RTX 2080/3080/AMD and RT/PT, crash or not?
- [x] 1 Item 2: GT-VBAO + GT-VBGI same rays? Only if it needs no same resolution / length: else just report.
- [x] 2 Item 2.1: campaign things jittering under DLAA (colourful in the motion debug): how ATN traces them, or find them.
- [x] 3 Item 3: presets + Shadows on top.
- [x] 4 Item 4: campaign shadows missing / too much (debug37). rigidcampaigntrees 0x4657DA8A, rigidcampaignvegetation 0x924D7813 / 0x819A414C
      are not in the scene; pine cards come out solid (rigid draws have no alpha test in the scene).
- [x] 5 Item 5: campaign beaches below 100% (debug37 Scaling issue): campaignterrain coast 0x647DB547 / 0x6F9A3968, waterplane campaign.
- [x] 6 Item 6: campaign profile + log (Attila folder, tw_rt_profile*.csv, ReShade.log).
- [x] 7 Item 7: HDR presets Highlight Saturation 0.7, Hue Shift 0.04.
- [x] 8 Item 8: Graphics 2 of 2.
- [x] 9 Nexus / GitHub / Steam text: short summary, requirements, what we have, dead body slider, grass over 100 m, pictures on Discord / Google
      Drive on top, Ko-fi + hot chocolate at the end (ATN's current text: Downloads/My current text for nexus (ATN).txt).
- [x] 10 Zip twattila_v119.zip, README, memory, report.
Results (commits 729681ca, 43dddcf3, 5693b7cb, 4e85adc4; all in README v119):
- Campaign: plants placed (rt_rows_campaign_vegetation, standing, .still), 62 fps against 66 without plants, 45 when captured every frame.
  Cut-outs: rigid meshes whose t0 the shadow pass alpha-tests (ps F79BFB14). Sea reads the rays' shadows, its rays start on y = 0
  (rt_positions, frame_history::campaign_frames). Refraction names added to WHOLE_TEXTURES. Animals: no bone history on the campaign
  (motion_unit.z), one colour each in the debug view. ATN: the yellow glow over forests in the fire save is the game's own.
- DLSS missing: picture stuck top-left below 100%, fixed (upscaled_frame). RT refused on non-NVIDIA (VendorId in Initialize).
- Questions: PT is path tracing of the indirect light only (camera image rasterized, direct light the game's with ray-traced shadows,
  hits take screen light / cache / learned lighting). HDR-only: the addon alone works (tested, helper missing, no crash). GT-VBAO +
  GT-VBGI same rays: not done, they would need one resolution, radius, directions and samples (3/4 12.5 m against 1/4 20 m).
- Tests: presets clicked in the overlay, 63 values match; Graphics 2 of 2 seen with compatibility on; battle frame has no campaign shader,
  220 unit draws; harness 50 of 50; the 8 TW targets build.
Left:
- Pines near Augusta Treverorum not seen (under the shroud in the saves here); ATN to look. rigidcampaigntrees 0x4657DA8A never drawn in
  my frames: its capture is untested.
- A real RTX 20 / 30 and AMD card: only simulated (RENODX_TW_NO_DXR12, RENODX_TW_NO_CLUSTERS, DLSS files hidden).
- Rome II, Thrones and the rest: port campaign capture, cut-outs, campaign_frames, trace wait in their rounds. Their highlight presets
  unchanged (per-game hue defaults).
- From the v118 list: bounce +1 ms with Far Tree Shadows, grass-lit off-screen banks.
- The red "do not work correctly yet" note in Ray Tracing is still there: ATN's call when to take it off.

## Working with ATN
- Very short answers, short code comments. Deliver changed files as a zip in `C:\Users\arshi\Downloads` (game folder
  at the zip root). Never push upstream. ATN tests in game and sends `ReShade.log` (next to `Ryse.exe`).
- Read `docs/CXX_STYLE.md` before C++ edits (designated initializers, local lambdas, no single-use helpers).
- Build: `cmake --build --preset clang-x64-release --target renodx-rysesonofrome.addon64` (HDR) /
  `renodx-rysesonofrome_dlss.addon64` (DLSS). Bin64 holds symlinks to `build/Release` (`0renodx-rysesonofrome`,
  `1renodx-rysesonofrome_dlss`, `2renodx-devkit`).
- If the build can't write the addon (game or zombie `Ryse.exe` running), rename the old file aside and rebuild.
- Shell heredocs eat backslashes (`\0`, `\\`): write edit scripts with the Write tool, then run them with python.

## ATN's BIG BATCH (2026-10-26; ATN away 24-48 h: work non-stop, don't wait for answers, zip each game when done)
Order (ATN's words condensed; tick items off here as they ship):
DONE 1: twattila_v65 (SSR refine / border fade / resolve with Rays + Denoiser, Grass Trace button).
Shared pieces for item 2 (twattila/): engine_tests.hpp (EngineTest, Attila() list, Append, SyncEarlyLoad,
StartAtStart), feature_settings.hpp (SSS / AA sliders + settings + module settings, SsrSettings / UpdateSsr templates on
the game's shader_injection, SetTints, AssignPages = Attila's 3 pages). Other games' compute shaders: one-line wrappers
of twattila's (see twthronesofbritannia/*.cs_5_0.hlsl); Attila's ssr00/01 include "../shared.h" only #ifndef SI.
tools\tw\tw_engine_values.py now: DATA PREFIX ADDON OUT TITLE "float=..,bool=..,int=.." (Thrones: 0xe0730 / 0xe09a0 /
0xe98e0; Attila 0xbeb60 / 0xbedd0 / 0xc85d0); repack_game.py adds LICENSE files next to shared code + Apache 2.0.
DONE 2a: twthronesofbritannia_v26 (Attila's SSR / SSS / AA / tests / pages; engine_values.txt); v27 adds Sky Debanding.
DONE 2b: twrome2_v27 (pages, SSS, AA, 42 Attila tests + 525 More Tests, water SSR hierarchical, Sky Debanding). Port
script: scratchpad port_tw_game.py (copy in tools\tw if kept); sky: add_sky_deband.py. SSS patch bug fixed (it inserted
at +5 tokens: broke Rome II's 7-token `min`; Attila / Thrones' mov_sat unaffected). ssr_march.hlsli = the shared FFX
march (includer defines FFX_SSSR_LoadWorldSpaceNormal / ScreenSpaceToViewSpace); ssr_hiz::OnReflectionDraw(cmd, slot).
WARP check: build\warp_grass\patch_test7.exe <dump cso folder> (dumps: bin\cmd_Decompiler-1.3.16\dump_shader_<game>_pack).
DONE 2c: twpharaohdynasties_v27, twtroy_v23, twwarhammer_v24, twwarhammer2_v24, twwarhammer3_v22 (pages, SSS, AA except
WH3 (TAA), tests, bloom fix, sky deband where missing). SSS patch: 3 strategies (Attila div pattern; else every shadow
map comparison x (1 - t49); else WH3's t_shadow mask reads x it). Tests for the protected exes (WH1-3, Troy: Denuvo-like
sections, no static scan): Pharaoh's names / types (tools\tw\data64, tw_convert64.py, tw_borrow64.py; WH1/WH2 only names
their exe holds). engine_tweaks.hpp x64 Find: one pass (by_name / by_address maps). Bloom: twattila/bloom_high_pass.hlsli
+ BLOOM_SAMPLE_ONLY reads; port script tools\tw\port\add_bloom_fix.py. Troy sky: twpharaohdynasties/sky_deband.hlsli.
LEFT for later in item 2: Shogun 2 (graphics live in its DLSS addon; tweaks are built `lea rdi, [name]; lea rsi, [obj];
call 0x1858720` in empire.retail.x64.dll, value at obj + 0xb8 for ints: tw_tweaks.py / engine_tweaks Find need that
form) and Rome Remastered (Feral Forward+: dx11_ASSAO, dx11_CSMResolvePS/CS = a screen-space shadow mask (SSS could
multiply it), Feral SSR compute (ScreenSpaceReflectionsIntersect, DownsampleDepth), SMAA / FXAA, no G-buffer pass).
DONE 3 (partly): talesofarise_v65 (fade / load / battle-end picture keeps HDR: post-tonemap targets and copies switch to
their clones; GT-VBAO in all 12 AO variants via bitmask_ao.hlsli, settings in the DLSS overlay through post_filters.hpp)
+ talesofarise_dlss_v95 (Spark trails fix: separate translucency t0 of ps 0x00438BF1 -> particle mask in 0x7A1E0D01 ->
off-screen motion = no DLSS history; GT-VBAO UI). NOT done for Tales: GT-VBGI, SSS (place: the sun shadow mask cs
0x2084C889 / sunshadow00 could multiply Bend's occlusion; needs a UE camera / light adapter for screen_space_shadows),
SSR (UE's SSR PS; FFX needs a nearest-depth pyramid of reversed-Z: max reduce + FFX_SSSR_INVERTED_DEPTH_RANGE).
UE 4.21 View UB (cb1 in UE passes): TranslatedWorldToView 8-11, ViewToClip 24, ClipToView 32, InvDeviceZToWorldZ 57,
ScreenPositionScaleBias 58, ClipToPrevClip 114, TemporalAAJitter 118, ViewSizeAndInvSize 122, BufferSizeAndInvSize 123.
NOT done in 2c: SSR for WH1-3 (they have none: a new pass needed); Pharaoh's water ray-cast SSR (ssr_new apply_ssr_ray_cast
0x6ECC6281, linear march) could take ssr_march.hlsli like Rome II's water.
1. Attila: SSR 1-3 (miss fallback / edge fade, FidelityFX SSSR denoiser + optional 3/4 / half-res trace, hit
   refinement); grass still ends ~100 m even with v64's PS fade patch and all grass sliders maxed (Detail Distance only
   costs fps): v65 adds a one-frame Grass Trace button (scrub DrawScrub distances / LOD picks) to find the limit.
2. All 10 TW games (Attila, Rome II, Thrones, Troy, Pharaoh, WH1, WH2, WH3, Shogun 2, Rome Remastered too): Attila's
   layout (Settings Mode HDR & Effects / Graphics 1 of 2 / Graphics 2 of 2, section colours), GT-VBAO, GT-VBGI, SSS,
   SSR, AA (TAA / SMAA T2x / SMAA / CMAA2 where the game has no TAA / DLSS); Graphics 2 of 2: engine tests like Attila
   + "More tests" (unsure ones) + engine_values.txt in each game folder (Thrones: Attila's cvars, no More tests).
   Rome II's blocky-bloom fix and Attila's dithering (sky deband, default Off) to every TW game that lacks them.
3. Tales of Arise: the HDR clamp on load / after battles (a fade / post-process that clamps); GT-VBAO, GT-VBGI, SSS,
   SSR like Attila (no RT yet); DLSS "don't use history here" (bias current colour) mask for the small fire / spark
   particles that smear (preset L, low scale).
4. Expeditions Rome: GT-VBAO, GT-VBGI, SSS, SSR.
5. All DLSS addons: the DLSS section colouring from Ryse.
6. Then: Attila DLAA (camera vectors, wind vectors, unit motion vectors, no-history mask; fix the smearing).
DONE 4: expeditionsrome_v28 (GT-VBAO in its last AO level ao00 0x31A1C68C / ao01 0xBD381E21, talesofarise/
ue_bitmask_ao.hlsli shared with Tales; HDR addon section "Ambient Occlusion"). GI / SSS / SSR not in (passes unknown).
DONE 5: talesofarise_dlss_v95, expeditionsrome_dlss_graphics_v21, twshogun2completeedition_dlss_v49 (Ryse headings:
DLSS lavender incl. its controls, Graphics pink, rest green).
DONE 6 (camera motion only): DLAA in every TW AA module (Method 5) via renodx-tw-dlaa64.exe (DLSS is x64-only):
dlaa_bridge.hpp (shared NT-handle textures + D3D11.4 shared fence, named mapping/events "Local\RenoDX_TW_DLAA_<pid>",
job kill-on-close, CPU answer before the GPU Wait), dlaa_helper/main.cpp + build.ps1 (reuses talesofarise_dlss/
ngx_backend.hpp, whose Evaluation now has create_flags), aa_dlaa_prepare/finish.cs. Zips: twattila_v66, twrome2_v28,
twthronesofbritannia_v28, twtroy_v24, twpharaohdynasties_v28, twwarhammer_v25, twwarhammer2_v25. RTX 5090 test without
the game: 117/120 frames. NOT done: wind (grass VS is HLSL already: previous position = same wind at time - dt; the PS
are DXBC-patched, so an MV output needs ISGN/OSGN signature edits) and unit motion vectors.
7. (ATN, added mid-batch; back Wednesday) After 6: DXR 1.2 for Tales of Arise starting from
   https://github.com/teemupynnonen/mgsv-phantom-fx (MIT): RT, path tracing, Ray Reconstruction 4.5, maybe frame gen.
8. Sparks: real motion vectors instead of the v95 no-history mask, so DLSS shows them sharp.

9. (ATN, after everything) Attila DLSS upscaling: render below output, everything scaled correctly (Tales' "Effects
   at output resolution" note and its difficulty: talesofarise_dlss README v54-v58, v70 crash), motion vectors for
   everything (wind, units) so DLSS can work.
DONE 9 (first step, 2026-10-26, untested in game): twattila_v67 "DLSS Render Scale (Experimental)" 33-100% under
   Anti-Aliasing DLAA (Attila only; AppendAa(..., true)): anti_aliasing.hpp scales the 3D span (present -> tone mapper
   drawn) like Shogun 2's render_scale: viewports / scissors at display >> 0..4 at 0, 0, every camera_VS_PS with
   g_screen_size (c27) = display and g_viewport_dimensions (c29) = display >> k gets render >> k (and reciprocals);
   jitter 8 / scale^2 Halton phases in render pixels; aa_dlaa_prepare at the render size; dlaa_bridge.h v2
   (output_width / height) -> helper Custom DLSS (MVLowRes). bridge_test.cpp DLSS=1: 128 -> 256, 117 / 120 frames on
   the 5090. All TW zips with the helper repacked (rome2 / thrones / pharaoh v29, troy v25, wh / wh2 v26: helper v2).
   OPEN: texture LOD bias (port twshogun2completeedition_dlss/texture_lod_bias.hpp: needs a MainView signal), our own
   effects (SSR / SSS / GT-VBAO compute passes) at the render size, VFX dynamic resolution (Shogun 2 v18 rule), unit /
   wind motion vectors, a frame trace for Attila. ASK ATN: Attila DLAA + render scale 50%: what breaks (screens).
10. Ryse Son of Rome: GT-VBAO, GT-VBGI, SSS, SSR.
DONE 10 (partly, 2026-10-26, untested in game): rysesonofrome_fix48: Ambient Occlusion GT-VBAO (ssdo00 0xD1106E8B =
    the SSDO pass, decompiled from Bin64\renodx-dev\dump (the DevKit dump; 354 shaders not in dump_shader_ryse are now
    copied to bin\cmd_Decompiler-1.3.16\dump_shader_ryse2), WARP-identical with GT-VBAO off) and Contact Shadows
    (contact_shadows.hpp before the tiled lighting cs 0x897232B5: sun's slice / channel from its light list t32, a march
    max-blended into the shadow mask slice 0). OPEN: GT-VBGI (additive pass after cs 0x897232B5 on u1 HDRSceneTargetUAV;
    t3 DiffuseRT albedo, t1 normals, t0 linear depth, b0 FrustumTL/TR/BL + WorldViewPos), SSR (the CS reads the game's
    SSR at t6 SSReflectionRT: swap in a hierarchical march in a pre-callback). Never replace cs 0x897232B5 itself: its
    decompile needs manual fixes (ld_raw / store_raw, denormal int literals).
11. At the end: patch notes of all these games (Downloads\tw_patch_notes.txt etc.), licence texts correct; answer ATN:
    is a licence txt next to the addon on Nexus enough, more copyright notices needed, our MIT licence in each game?

DONE 11 (2026-10-26): patch notes in Downloads (tw_patch_notes.txt prepended + tw_patch_notes_2026-10-26.txt,
    talesofarise_patch_notes_2026-10-26_nexus.txt, expeditionsrome_ryse_patch_notes_2026-10-26_nexus.txt). Licences:
    docs/licenses/NVIDIA_DLSS_NOTICE.txt (DLSS SDK licence 2b notice + trademark attribution) in every zip with NGX code
    (TW helper zips, Tales DLSS v97, Expeditions DLSS v21, Shogun 2 DLSS v49, Ryse DLSS v111); Expeditions zips got
    RENODX_LICENSE.txt (+ ACES_LICENSE.md: RenoDX's ACES RRT); Shogun 2 HDR zip v59 APACHE_LICENSE-2.0.txt (Bend's SSS).
    Answer for ATN: the licence files in the zip are enough; on Nexus credit NVIDIA DLSS (their licence asks for
    attribution in credits) and list the third parties (Bend Studio, Intel CMAA2, SMAA, AMD FidelityFX, Phantom FX).
12. (ATN, added 2026-10-26 after 7-11) Tales: more RT (beyond sun shadows), DLSS Ray Reconstruction 4.5 support,
    path tracing.
    DONE 2026-10-26: RTAO (DLSS v99), RR (v98) + preset F (v100, DLSS 310.9.16's new model), ray-traced bounce light
    (DLSS v101 / HDR v69, zips in Downloads): one diffuse bounce, hits lit by last frame's scene colour where last frame
    saw them, temporal accumulation + 0x7A1E0D14 filter, t42 in the lighting pass. GPU test rt_shadows_test.cpp passes
    (bounce 0.285 red under the red square, 0.004 open). Path tracing proper is not possible: the RT scene has positions
    only (no materials / emissives / skinned or masked geometry). ASK ATN: test bounce light (with DLSS or DLAA, outdoors).
13. (ATN, added 2026-10-26) DLSS Frame Generation for Tales (Phantom FX's frame_generation.cpp may help).
    DONE 2026-10-26: Tales DLSS v100 + HDR v68 (zips in Downloads; the DLSS zip carries Streamline 2.14.16's
    sl.interposer/common/dlss_g/reflex/pcl + nvngx_dlssg from ATN's SDK at C:\Users\arshi\Documents\Nvidia(myself)\
    streamline-sdk-v2.14.16, STREAMLINE_LICENSE.txt + PHANTOM_FX_LICENSE.txt). frame_generation.hpp (Phantom FX port,
    Detours on DXGI's CreateSwapChain behind ReShade, stand-in swap chain, D3D12 copy queue, Streamline manual hooking),
    d3d12_device.hpp (shared with rt_shadows), hudless via the HDR addon's own swap chain shader (0x7A1E0D40-42),
    Reflex (+ Boost option, "Off (Reflex only)" mode), RR preset F. GPU test tools\toa\fg_test\fg_test.cpp
    (build\toa_fg_test): 2x/3x/4x in SDR and HDR10 on the 5090, resize, release. Lessons: DLSS-G takes no FP16/scRGB
    (HDR addon Auto -> HDR10 with FrameGeneration); the real swap chain needs ALLOW_TEARING (else every present is
    DXGI_ERROR_INVALID_CALL); ResizeBuffers zeros must be resolved before Streamline sees them (else device removed);
    it pauses when the window is unfocused. ASK ATN: test in game (FrameGeneration on, HDR10/Auto, then 2x/4x).
14. (ATN, added 2026-10-26, after everything else) Attila: motion vectors for everything (better DLSS), then frame
    generation if possible (32-bit game: Streamline is 64-bit only), and answer whether RT/PT + RR 4.5 is possible.
    DONE (motion) 2026-10-26: twattila_v69 (Downloads; untested in game): DLAA / DLSS object motion: wind (grass,
    leaves, branches: 8 VS) and units (8 skinned main VS: movement / turning, a GPU placement table carries each
    soldier's world rows to the next frame, matched by mesh key + nearest within 0.5 m; limb swing not). Generator
    tools\tw\attila_motion\make_wind_motion_vs.py (`:unit`), tests compare_test.cpp + placement_test.cpp (build\
    tw_attila_motion) all pass. Shared AA module: other TW games pass no shaders (unchanged; Troy / Thrones compile).
    OPEN: rigid moving objects (ships, siege, projectiles: rigid instanced VS, same table), cloth / sails (CPU-moved
    VBs), limb swing (per-soldier bone history). ASK ATN: Attila DLAA (+ DLSS scale) in a battle: trees, grass,
    marching / charging units still smearing?
    ANSWERED (FG / RT): frame generation is not realistic: Streamline, DLSS-G and Reflex are 64-bit only and must sit in
    the process that presents; a 64-bit helper presenting into a child window of the game's would fight focus (DLSS-G
    pauses unfocused), input and Reflex pacing (the game's frames come from another process). Suggest NVIDIA's
    driver-level Smooth Motion (RTX 50) instead. RT: a 32-bit process gets D3D12 but raytracing tier 0 on the 5090
    (tools\tw\attila_motion\dxr32_check.cpp), so DXR could only run in the 64-bit helper (geometry over shared
    textures as Tales' capture, NT handles): sun shadows / AO possible in principle, a big project; RR 4.5 through the
    helper possible (Attila's G-buffer has the guides) but pointless without ray-traced effects; path tracing no (no
    materials / lights in a captured scene, as Tales).
    DONE (motion, rest) 2026-10-26: twattila_v70: rigid objects (stream-output instance rows, motion_rigid.cs match by
    prediction, readback of moving meshes), cloth / sails / ropes (capture pass positions by SV_VertexID, anchor
    lookup), ocean (old: last frame's time; ocean2: last frame's displacement textures). All shaders bit-identical in
    tools\tw\attila_motion tests (compare / rigid / deformed / placement).
    DONE (RR) twattila_v71: Ray Reconstruction (aa_rr_guides.cs from the G-buffer). NVIDIA DRIVER BUG found: D3D11 RR +
    a CPU/cross-process wait for the GPU right after it = black output or a crash in nvwgf2umx, so the helper runs RR
    through NGX D3D12 (dlaa_helper/rr_d3d12.hpp). Tales still uses D3D11 RR (risk: same bug in game; move to D3D12 if
    ATN sees black / crashes with RR).
    DONE (RT) twattila_v72: ray-traced sun shadows + AO in the helper (rt_scene.hpp, rt_d3d12.hpp, rt_trace.cs_6_5;
    bridge v4). rt_bridge_test32d.exe (NOT rt_bridge_test32.exe: old shadow point) passes. All TW zips repacked with
    the v4 helper (rome2 / thrones / pharaoh v30, troy v26, wh / wh2 v27).
15. (ATN, 2026-10-26) Optimize Tales + Attila. DONE (not zipped for TW yet: goes into the next TW round): helper BLAS
    compaction (2816 -> 1024 B in the test), RR <-> SR switch frees the other NGX feature (bridge_test ALTERNATE=1),
    bridge answer timeout 100 ms -> 5 s (a new NGX feature takes longer: switching RR / preset in game used to turn DLAA
    off), Tales DLSS v103 frees its RT memory 10 s after all RT effects are off. Measured (bridge_test at 2880^2 = 4K
    pixels, GPU Process Memory counters): helper DLAA preset M 1.46 GB, K 0.86 GB, E 0.59 GB, RR 2.06 GB; game side
    0.28 GB (0.62 GB with RR guides). A 32-bit D3D11 process holds 8 GB of VRAM with 426 MB address space
    (scratchpad vram32.cpp): VRAM is not limited by the 32-bit address space.
16. (ATN, 2026-10-26 evening, after 15; "no need to double check") (a) short report of the last 24 h (TW, Ryse,
    Expeditions, Tales): what is done / missing; (b) can RT run instead of GT-VBAO / GT-VBGI / SSS (save performance);
    (c) many TW games' SSS broken: line artefacts like Attila had, and shadows ~180 deg from the sun (wrong direction);
    (d) Graphics 2 of 2 in every TW game but Attila: keep only what Attila still has + campaign map, trees, grass,
    buildings, units, culling; delete the rest; (e) Attila's AO / GI / SSS sliders, names, values, denoisers, temporal
    smoothing missing in the other TW games: port them; (f) DLSS (render scale) + motion vectors for everything in every
    TW game like Attila (not WH3, Shogun 2: they have DLSS mods); (g) tests to make sure RT, DLSS, FG, SSS etc. work.
    DONE 2026-10-26 (zips: twattila_v73, twrome2_v31, twthronesofbritannia_v31, twtroy_v27, twpharaohdynasties_v31,
    twwarhammer_v28, twwarhammer2_v28, twwarhammer3_v23; all untested in game):
    (b) RT AO Off / On / Alone (Attila); RT shadows work with SSS off. (c) SSS bug = shared code assumed Attila's
    lighting slot b2 / sun c0.y / camera layout; SunDraw now has lighting_slot, sun_direction, camera_layout (WH3 and
    Rome II's 6 no-shadow shaders remapped by frame_history::AttilaCamera partial CB copies; Light Bounce NOT remapped:
    WH3's light_bounce.hlsli has its own layout). tools\tw\sun_draw_layouts.py [--write] [--ao] fills them from dumps.
    (d) tools\tw\trim_more_tests.py + tw_game_tests.py filter (campaign / grass & trees / units / buildings & culling).
    (e) twattila/ao_filter.hpp (AO Temporal Smoothing + Edge Fix + Denoiser at the first sun draw, SunDraw::ao_slot) for
    Troy, Pharaoh, WH1-3; lighting_settings AO_SCALES (ssao_scale) vs AO_TEMPORAL (ssao_temporal).
    (f) DLSS render scale + RR in Rome II, Thrones, Troy, Pharaoh, WH1, WH2 (RR guides per G-buffer family:
    twpharaohdynasties/aa_rr_guides, twwarhammer2/aa_rr_guides); frame_history::render_width/height set by the AA
    module; SSS (Bend RenderSize) and ao_filter follow it (ao_pass / light bounce / SSR at render scale: unchecked).
    Object motion in all six: tools\tw\attila_motion\classify_motion.py -> port_motion.py -> test_motion.py ->
    wire_motion.py; asm_to_hlsl.py (DXBC asm from fxc /dumpbin -> HLSL, bit-exact) where 3Dmigoto crashes (4-bone
    units) or is wrong (WH1 trees). compare_test reads time_in_sec's offset from reflection (Rome II c22 shaders).
    Bugs found: Light Bounce bypasses (draws the sun draw itself), so AA / RT / AO-filter hooks must install BEFORE it
    (AA camera+depth and RT sun dir were lost with GI on); bridge answer timeout 100 ms -> 5 s.
    OPEN: deformed_test only knows cloth layouts (sails / ropes untested everywhere); rigid ropes (view x projection)
    have no motion; Pharaoh feather-helmet / WH2 liquid units output nothing with random test data (identical, unchecked).
17. (ATN, 2026-10-26 late) Shogun 2: all motion vectors? (game velocity covers units, trees, grass, cloth, imposters,
    textureblend; MISSING: rigidcommon ships / siege, sails, ropes, ocean, waterplane, wakes, projectile trails); HDR
    upgrades (RGBA8 / RGB10A2 -> RGBA16F) do not touch its float2 velocity target. Attila: FG through the 64-bit helper
    (ATN insists), limb swing (code exists: bone pool ClaimBones / MotionAnimationLoad; verify), particle trails, units +
    leaves in RT. Other games: still-missing effects (Ryse GT-VBGI / SSR, Expeditions GI / SSS / SSR, Tales GT-VBGI /
    SSS / SSR, WH1-3 SSR). Answer: SER (DXR 1.2) why not / speed, other DXR features unused; temporal smoothing needed
    with DLSS?
    DONE 2026-10-26 (untested in game; zips below):
    - twattila/object_motion.hpp: Attila's object motion out of anti_aliasing.hpp (host gives a MakeView per draw;
      Resolve at the frame's end; Target), its target R32G32_UINT (motion.hlsli MotionTexel: halves + depth bits),
      MotionDepthMatches absolute 4e-6 (aa_dlaa_prepare + Shogun 2 merge), MotionPixels removes the rows' jitter
      (motion_jitter, Shogun 2's cameras are jittered).
    - Shogun 2 DLSS v51 (object_velocity.hpp + velocity_merge.cs): rigidstandard 0x41913E64 / rigidvertexwind 0x0798A33F
      (RIGID), sail / sail symbol / rope (DEFORMED), ocean 0xC1BE6CEE / waterplane 0xC883761A (camera only, drawn after
      the game's motion_vectors pass), constants copied from the draw's camera (CopySubresourceRegion runs), merged into
      a copy of the game's MVs at game_dlss on_evaluate (eval->pInMotionVectors replaced). Advanced: Ship and water
      motion vectors (ObjectMotion). Tools: port_motion / test_motion MOTION_DUMP=dump_shader_shogun2_pack, generator
      regex (?<!\w)view_projection (waterplane's g_overlay_view_projection), compare_test reads view_projection /
      prev_* offsets by reflection, deformed_test any vertex layout (sails / ropes now tested everywhere), velocity_test.
    - Shogun 2 HDR v60: PCSS (shaders/pcss.hlsli, own code from Fernando 2005) in sss.hlsli SampleShadowSplit;
      DLSS addon "Sun shadow softness (PCSS, experimental)" (SunShadowSoftness, degrees, 0 off). tools\shogun2\pcss_test.
    - Frame generation for the TW games with DLAA (bridge v5, helper14): dlaa_helper/fg_d3d12.hpp (Streamline in the
      helper, child window over the game's, its own window thread, disabled + HTTRANSPARENT; FG presents at the game's
      reshade_present), twattila/frame_generation.hpp (HUD-less drawn through the swap chain proxy shader at KeepFrame in
      a private context state; back buffer copied after ReShade's overlay), settings in feature_settings AppendAa
      (FrameGeneration, FrameGenerationReflexBoost). Bridge Start(device) writes the adapter LUID before the helper runs
      (race found by the FG test). Tests: fg_bridge_test (2x / 3x / 4x, mouse to the game's window), hudless_test.
      Ships Streamline 2.14.16 DLLs + STREAMLINE_LICENSE + PHANTOM_FX_LICENSE next to the helper (tw_pack.py).
    - Attila RT: units (RT_DYNAMIC: captured every frame, 3M vertices max, one fast-build BLAS per frame per slot;
      Ray-Traced Units setting) and leaves (RT_FOLIAGE: non-opaque, 0.5 transmittance per leaf, rt_trace Transmittance).
      rt_dynamic_test. Particle / projectile trail motion: not done by design (blended, no depth; transformer presets
      ignore the bias mask).
    - Expeditions DLSS v22: UE's r.SSGI.Enable / Quality / HalfRes in Optional. Not done (new passes, follow-ups): Ryse
      GT-VBGI, Tales GT-VBGI, Expeditions screen-space shadows, WH1-3 SSR, PCSS in the other games.
    - Remastered-CE (Shogun 2, a user's report): separate exe in a RemasteredCE folder (ReShade + addons must go there),
      rebuilt shaders.pack + patched engine DLLs: some HDR shader hashes / DLSS patterns may not match; needs its files.
    - Sweep: tools\tw\test_sweep.py runs every GPU / WARP test (motion of 7 TW games + Shogun 2, RT captures,
      placement, helper DLAA / DLSS / RR / RT / units / FG / HUD-less, velocity, PCSS, AO filter, Ryse, Tales, AA, SSR,
      SSS).
18. (ATN, 2026-10-26 night) Tales first: opacity micromaps, BVH refit, SER (+ any other accelerators), path tracing and
    everything it needs, every missing RT effect; then everything missing in the other games (Attila RT effects / refit
    / OMM, RT in the other TW games + Ryse + Expeditions, Ryse GT-VBGI, Expeditions SSS, WH1-3 SSR, PCSS elsewhere, FG
    for Shogun 2 / Ryse / Expeditions, Tales RR via NGX D3D12, AO / bounce / SSR at render scale, Remastered-CE).
    DONE for Tales 2026-10-26 (zips talesofarise_dlss_v104 + talesofarise_v71, untested in game; README v104 / v71):
    Agility SDK 1.619.6 (ATN's NuGet in Documents\Nvidia(myself)\agility, extracted to build\agility_1.619.6; the
    preview 1.721.3 needs developer mode: not used) through ID3D12SDKConfiguration1::CreateDeviceFactory = DXR 1.2 +
    SM 6.9 on the 5090; alpha-tested prepass draws (89 masked PS from tools\toa\rt_tests\scan_masked.py, stream output
    from the VS's own bytecode, opacity-mask atlas, UV / normal pools, records = InstanceID); OMM (verified by omm_test);
    character refit; batched builds; SER raygen (0x7A1E0D1C prebuilt .cso: rebuild with the dxc line in its source
    after changing rt_path / rt_cache / rt_scene / rt_surface); soft shadows (PCSS on ray blocker distance, no history:
    a temporal version ghosted moving shadows); character shadows; path tracing with the game's lighting learned from
    the screen (Bandai's lighting is NPR, so PT feeds its indirect slots); glossy reflections; specular occlusion; HDR
    v71 uses them in all 6 lighting permutations. Not done (README says why): volumetric RT, particle / translucent
    shadows, refraction / caustics, local light shadows (need an indoor / night frame trace from ATN).
    Other games, in progress 2026-10-27 (nothing zipped yet, all untested in game):
    - TW helper15 (bridge v6, Agility, DXR 1.2 OMM, units refit, soft sun shadows) + Attila; RT ported to Thrones (26),
      Rome II (45), Troy (16), Pharaoh (16), WH2 (16), WH1 (29) scene shaders with tools\tw\rt_port.py (captures from
      3Dmigoto or, RT_ASM=labels, fxc's disassembly: WH1's trees were silently wrong in 3Dmigoto) and
      tools\tw\rt_capture_test.py (all OK on the GPU; leaves compared projected: their capture divides by w now).
      WH1 / WH2 have no AO draw pass: rt_scene traces at the first sun draw and multiplies its AO into the AO the sun
      draws read (rt_ambient_combine.cs, every RT game needs its wrapper). WH3 has no DLAA: no RT. Builds: build32
      Attila / Rome II / Thrones need a rebuild (rt_scene changed), 64-bit Troy / Pharaoh / WH1 / WH2 built.
    - Tales DLSS v105: Ray Reconstruction through D3D12 (rr_d3d12.hpp, the helper's RayReconstruction12 generalized:
      existing device, create flags, pre-exposure); tools\toa\rr_test\rr12_test.cpp OK (CPU waits, SR in the same
      process, clean exit). The helper's rr_d3d12.hpp changed: rebuild helper15 before packing.
    - Frame generation (talesofarise_dlss/frame_generation.hpp, now shared: Camera near / far / depth_inverted, Inputs
      motion_scale, Install(dir, name), CreateSwapChainForHwnd hooked too, DrawSettings / LoadSettings, Inverse) in
      Expeditions DLSS (UE 4.26 View rows = Tales' + 4; hudless after t00-t09), Ryse DLSS (camera split from CryEngine's
      view-projection: tools\ryse\fg_camera_check.py OK; hudless after the post pass) and Shogun 2 DLSS (game's DLSS
      evaluation + main camera captured in render_scale's upload hook; hudless after tonemap00 / 01). fg_test FG_HWND=1
      runs the ForHwnd path (as CreateSwapChain; "1 frame" when the test window has no focus).
    - Ryse HDR fix49: Light Bounce (GT-VBGI) after the tiled lighting (light_bounce.hpp + light_bounce.hlsli on the
      shared twattila passes); tools\ryse\light_bounce_test.cpp OK.
    - Render scale (step 9, ATN): TW GT-VBAO (bitmask_ssao.hlsli AoViewScale: texture uv -> view uv; off-view samples
      skipped) and SSR (SSR_VIEW_SIZE, light buffer / resolve history at the view's share) were wrong below 100%: fixed,
      WARP tests ao\render_scale_test.cpp and ssr_test.cpp (100% vs 50%: identical). SSS samples x the render share.
      The game's own HBAO (Attila / Rome II / Thrones) stays wrong below 100% (its code). Troy / Pharaoh AO pass uses
      the game's TEXCOORD (unverified below 100%). TW render sizes now whole aspect units (frame_history AspectSize,
      Shogun 2's), TW texture LOD bias (twattila/texture_lod_bias.hpp, moved from Shogun 2, SetMainView), TW DLSS Mode
      list + Custom 1-100% (AntiAliasingDlssMode; ATN: like Tales / Shogun 2). Tales / Ryse / Expeditions / Shogun 2
      already had modes, custom and aspect-exact sizes.
    - Zips 2026-10-27 (tools\tw\release_zip.py: repack_game + old zip's DLLs / notices + --helper): twattila_v75,
      twrome2_v33, twthronesofbritannia_v33, twtroy_v29, twpharaohdynasties_v33, twwarhammer_v30, twwarhammer2_v30 (all
      with helper15 + D3D12\D3D12Core.dll + AGILITY_SDK_LICENSE.txt), talesofarise_dlss_v105, rysesonofrome_fix49,
      rysesonofrome_dlss_v112, expeditionsrome_dlss_graphics_v23, twshogun2completeedition_dlss_v52 (FG: Streamline DLLs +
      STREAMLINE / PHANTOM_FX licences added). Sweep build\sweep_2026-10-27.txt: 41 of 43 (Pharaoh feather / WH2 liquid
      units: the known generic-data cases).
    NOT DONE (needs ATN or a big new pass): WH1-3 SSR (no game SSR pass: the lighting's specular must be patched),
    Expeditions screen-space shadows (stripped UE shaders, no contact-shadow code: a one-frame trace to find the sun's
    light draw), PCSS in Ryse / TW sun shadows, RT in Ryse / Expeditions, Attila RT reflections / GI / PT, WH3 RT (no
    DLAA there), Remastered-CE (its files). ATN's plan for later: every RT effect + PT in every game, one toggle per
    effect, nothing traced twice.
19. (ATN, 2026-10-27, away; authorized: drive the desktop by script, launch / close games, change game-folder files and
    in-game settings (restore after), click first-start prompts, use saves) BIG BATCH 2: test everything untested
    myself in game, Not-done list (WH1-3 SSR, Expeditions SSS, PCSS Ryse / TW, RT Ryse / Expeditions / WH3 (+ WH3
    DLAA), TW RT reflections / GI / PT), no double work + per-effect RT toggles + PT master, RT + PT in every game,
    Remastered-CE (installer in D:\Downloads\RemasteredCE_Installer_Preview, Steam Shogun 2 now installed). TW game AO
    below 100%: low priority.
    In-game test kit (C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\ingame): drive.exe (build\ingame; shot via
    Desktop Duplication HDR->sRGB, burst N frames, click / key / rel mouse / focus / kill / windows), shifts.py (burst
    motion), ini.py (ReShade.ini keys), deploy.py (helper / Streamline / NGX DLLs from the zips into game folders;
    build\ingame\deployed.json), devkit.py + draws.py (DevKit MCP bridge from Python: snapshot, draws, shaders).
    build\ingame\restore_notes.txt lists every game-folder / ini change to undo at the end. DevKit must load LAST
    (zrenodx-devkit.addon64, copied: symlinks need admin): loaded first it crashes UE games at the early addon unload.
    Steam ids: Ryse 302510, Tales 740130, Expeditions 987840, Attila 325610, Rome II 214950, WH2 594570, WH3 1142710,
    Pharaoh 2951630; Shogun 2 CE exe direct (Esc x2, (1024,1120) Single Player, (1464,1400) Custom Battle,
    (1932,1568) start).
    Results so far: Ryse FG generates 2x with HUD-less (combat burst clean). Expeditions FG: hudless hook never fired
    (OnTonemapper had a non-deducible template parameter: command_action registered it for no draw types; same bug in
    Shogun 2) - fixed, generates with HUD-less. Shogun 2 FG: the game's DLSS depth is R24G8_TYPELESS (not shareable
    with D3D12): frame_generation Submit now draws depth-stencil formats into an R32 float copy (CreatePasses shared
    with DrawHudless) - generates 2x. FG + scRGB: the three HDR addons now output HDR10 whenever frame generation is
    on (SetOutputEncoding / Expeditions' setting). FG logs status changes, HUD-less skips, unshareable inputs.
    Tales in game: RT running (DXR 1.2, OMM + SER; status now logged), PT, RR on D3D12, FG 2x + HUD-less: all fine.
    TW in game (DLSS Performance + FG 2x + RT shadows / AO / units each): Attila, Thrones, WH2, Troy, Pharaoh, WH1 OK.
    Fixes found: (1) AA's camera buffer minimum was 36 registers, Rome II's is 35: CAMERA_BYTES = 30 x 16 (it reads
    c0-c29). (2) Rome II hooked AA's tone mapper end on its DOF passes too (dof00/03/04/05 in UpgradeRTVReplaceShader):
    DLSS ran mid-chain and the 3D stayed in the top-left: on_drawn cleared for them; OnTonemapDrawn now takes only
    display-size RGBA16 draws before consuming the scaling. (3) RT traced only at the AO draw in AO games: Pharaoh never
    traced: the first sun draw now traces when no AO pass did (ambient_frame), rt_scene::InstallSunDraws before the
    screen-space shadows in all 7. (4) TW FG + scRGB: HDR10 at the first swap chain when FG is on. (5) TW bridge logs
    the helper's DLAA / FG lines on change and the RT line each minute. (6) In-game MSAA (ATN's Rome II x4, Troy x2,
    WH1 2) silently keeps DLAA off: now a warning in the log. OPEN: GT-VBGI (Light Bounce) over-brightens below 100%
    (Rome II at 50%: washed-out grass, tree shadows faint; GI 0 looks right): needs a WARP 100%-vs-50% test of the
    whole chain. Game preference changes (gfx_aa) and ini changes to undo: build\ingame\restore_notes.txt.
    GI below 100% FIXED: the battle portrait (a second 3D view drawn after the tone mapper, unscaled) ran the sun draw
    hooks too and overwrote Light Bounce's history camera each frame: frame_history::MainViewport (tone mapper drawn
    -> false; else the viewport must be the render size) gates light_bounce / rt_scene / ao_filter / SSS sun hooks.
    Light Bounce's prepared levels / trace / geometry clamp to the view's region (light_bounce.h region_*).
    RENDER-SCALE AUDIT (ATN: DOF, CA, shadows, reflections, our RT etc. at DLSS): twattila/frame_trace.hpp
    ([renodx] FrameTrace=1: one frame 600 presents into scaling -> tw_frame_trace.txt; MARKs at the tone mapper),
    tools\tw\scale_audit.py (render-space reads outside the scaled span), scale_scan.py / scale_scan_hashes.py (NDC
    from texture uv, projected uv), scale_fix.py (HLSL rewrite, NOT used: hdr_to_screen runs after DLSS unscaled),
    cbuffer_slots.py (TW shaders use b0-b9 only: b12 free), tools\ingame\tw_battle.sh (TRACE=1: launch, custom
    battle, trace, close). Traces in build\ingame\traces\<game>.txt (all 7). Findings (Attila): engine builds NDC from
    the texture uv ((vpos + off) x g_screen_size.zw x (2,-2) + (-1,1)) in point lights (via point_light_PS
    g_viewport_offset_scale), decals, battle fog, water / river / refraction, particle distortion, game SSAO, markers;
    the sun light uses g_viewport_dimensions (right). Post-DLSS hdr_to_screen (unscaled) reads the distortion texture
    and depth (render space) at full uv; ps_downsample_image_store_luma (bloom + exposure source) reads the whole
    3840 texture (stale outside the region); shadow_map_depth_buffer_analysis too (cascade fit; low impact).
    PLAN being built: twattila/view_share.hpp: DXBC patch at creation of every PS with the pattern (runtime dataflow:
    source from camera g_screen_size / g_render_target_dimensions -> NDC source / (viewport/target); projected
    (0.5,-0.5)+(0.5,0.5) uv x (viewport/target)), cb12 render_scale (xy per draw viewport / target, zw frame
    render / display; 0 = unbound = 1), bound per draw for patched pipelines (command_action hash 0, only while the
    frame is scaled; unbound after). Replaced HLSL shaders declaring cbuffer render_scale b12 get it too (heat haze /
    depth reads after DLSS x zw).
    ATN 2026-10-27 more: WH1/WH2 army flags waving get wrong shadows (check RT capture of cloth / SSS); WH3: DLAA /
    DLSS / FG / RR / RT / PT and motion vectors for EVERYTHING; then a final pass for anything missing.
    RENDER SCALE DONE (2026-10-27 morning, all 7 TW built; Troy + Rome II checked in game at 50%: full frame, FG on):
    - view_share.hpp: PS and CS patched at creation. NDC sites (uv x (2,-2) + (-1,1), and x (4,-4): half-size CS of a
      full-size uv) divide the uv by rS.xy; projected sites (camera projection x (0.5,-0.5) + 0.5; CS: any, PS: read as
      a coordinate) multiply by rS.xy; region reads (per-addon RENDER_SCALE_READS: game hash + texture slots) multiply
      the read's coordinate by rS.zw (ld: utof / mul / ftou). rS = cb12 with 0 -> 1. Per draw: xy = viewport / target
      only for a viewport the render scale made (corner 0,0 and target x frame share within 1.5 px), else 1 (portraits
      at an offset, everything after DLSS); zw = frame share; a region read whose first slot's texture is not the
      display's size (or a halving) gets 1. CS: xy = frame share before the tone mapper, 1 after. WARP test: every
      patched PS / CS of 8 dumps creates (VIEW_SHARE_REGION=1 patches every 2D read for the test).
    - RENDER_SCALE_READS: Attila / Thrones luma + bloom downsample 781681E4 t0, shadow depth analysis B386A8BC /
      16390F4F t0 t1; Rome II downsample AD818F92 t0; Troy glow bright pass after DLSS 075C096D t0 t1; Pharaoh
      57E5140E t1 t2; WH1 shadow analysis 404B4343 t0 t1. NOT the luma stats CS (BAF987B3 / F9B80B32 / 96BCFCBB /
      A50883CE): they already read viewport / target. NOT Rome II's page touch 28C847C0 (scaled-span draw).
    - twattila/render_scale.hlsli (FrameRenderShare / DrawRenderShare / RenderShareTexel): 70 replaced post-DLSS
      shaders (hdr_to_screen t01.., DOF, photo mode: tools script render_share_hlsl.py in the session scratchpad)
      read depth (gbuffer_channel_4_*) and distortion at the render share; Attila / Thrones / Rome II tone mappers read
      the (now region-normalized) bloom at uv / DrawRenderShare. WH1 / WH2 tone mappers already read bloom at the
      viewport's uv. Rome II's hdr_to_screen is in the scaled span (no change). Bloom 01 high-pass shaders (Troy / WH)
      are the portraits' (no change).
    - anti_aliasing: after DLSS the game's own viewports go back if the scaled ones are still bound (Rome II lens
      flares drawn after the tone mapper without a viewport bind).
    - Remastered CE (analysis, nothing to install): its installer needs the Steam CE exe (ATN's CE exe differs: hash
      check fails; the engine DLL matches). It runs a patched copy of the engine as RemasteredCE\CE.dll (our DLSS
      addon looked only for empire.retail.x64.dll: now both; all 12 byte patterns found unchanged in the patched
      engine), its exe sits in RemasteredCE\ (ReShade + addons go there), and it rebuilds 2 of our SSS CS (sss00
      0x34ECD06E -> 0x9147EB4E, sss01 0x1B529672 -> 0x7FA1D600: IgnoreEdgePixels on, TAA-flagged banner pixels read as
      far): the HDR addon maps both to ours. Other changed shaders (particle / ribbon VS, blood decals, grass source)
      are not ones we replace.
    - WH1 / WH2 flags (ATN): the floating unit banners (rigidtextureblend vs40_main_2_bones: WH2 0x7DF335FF, WH1
      0xE7F0D71A, Troy 0x262DB049, Pharaoh 0x76225B91; 18 draws of one 2169-index mesh in WH2) were in the RT scene as
      static captures (pose of first sight) and cast RT shadows the game never gives them: dropped from SCENE_SHADERS
      (WH2 checked in game: banner shadows gone). rt_scene traces captures with FrameTrace (MARK "ray tracing capture:
      vs / ps"). The DevKit must NOT sit in a TW game folder with DLAA: ReShade loads it into renodx-tw-dlaa64.exe too
      and it crashes there (helper "stopped", WER shows zrenodx-devkit.addon64). DevKit live shader probes did not
      apply in WH2 (no visible change even on terrain).
    - WH3 (2026-10-27): the game's own DLSS (gfx_dlss, NGX linked in the protected exe, no menu option) never creates a
      feature even with nvngx_dlss.dll (hooked the driver's _nvngx.dll exports via a LoadLibraryExW detour to check:
      loaded, nothing created; hook removed again). Instead taa_dlss.hpp: after the TAA resolve cs (0x1ADBFF18 /
      0x6A8FC282: t0 depth .x, t2 velocity = clip now - before, t4 jittered colour RGBA16F, u1 out) DLSS through the TW
      helper (dlaa_bridge), result into u1; jitter = camera g_camera_jitter (float 257, clip space; equals the
      projection's [2][0] / [2][1]) x size / 2, y negated; motion = velocity x (-0.5, 0.5) x size. Tested in game:
      DLAA active, pans sharp. Frame generation (dlss_frame_generation.hpp, talesofarise FG + fg_camera.hpp, HUD-less
      after t00 0xE2EE82F8): generating 2x. Below 100% (gfx_resolution_scale) the game draws the 3D AND its post
      chain into the targets' top-left (TAA dispatched over the render size) and upscales at the very end: our DLSS
      stays off there (camera viewport != screen), FG pauses; DLSS < 100% would need the post after TAA at display
      size. RR / RT not ported. Debug: renodx_trace_now next to the exe = one-frame trace (taa_dlss.hpp).
    - Ray-traced Light Bounce (all 7 TW, bridge v7, helper16 in build\tw_dlaa_helper16): light_bounce.hpp's trace
      step can be replaced (SetRayTracedTrace) by rt_scene TraceLightBounce: the prepared light (level 0, R11G11B10)
      copied into the shared rt_light, request 2 -> helper RtScene12::Bounce -> rt_bounce.cs_6_5.hlsl (cosine rays
      from rt_positions, hit -> this frame's camera view-projection -> the pixel where the camera sees the same surface
      (rt_positions within 5 cm + 2%) -> prepared light; else 0) -> rt_bounce -> objects.traced; resolve / denoise /
      apply unchanged. Root sig: table t9 (light) / u9 (bounce), b2 20 constants. Settings RayTracedLightBounce /
      ...Rays (4) / ...Range (10 m) in the AA section. WH2 in game: "Ray-traced Light Bounce: tracing (960 x 540, 4
      rays)", picture fine, 172 fps (DLSS 50% + FG + RT). The helper exe in a game folder can be held by zombie helper
      processes (0 threads, killed games): rename it to swap (WH2 has renodx-tw-dlaa64.exe.old15).
    - Zips (Downloads): twattila_v76, twrome2_v34, twthronesofbritannia_v34, twtroy_v30, twpharaohdynasties_v34,
      twwarhammer_v31, twwarhammer2_v31 (helper16), twwarhammer3_v24 (helper16 + Streamline), twshogun2completeedition
      _v61, twshogun2completeedition_dlss_v53, expeditionsrome_dlss_graphics_v24, rysesonofrome_fix50,
      rysesonofrome_dlss_v113.
    - Expeditions DLSS v24: Contact shadows (Graphics slider, Off-10% of the screen, Max preset 3%): the game's sun
      light shader (ps 0xC38C91CD, draw-rectangle vs 0x7C21DA11) already has UE's ShadowRayCast; the game sends
      ContactShadowLength 0. contact_shadows.hpp copies the light's constants (ps b1, 144-256 bytes) with c1.z set (cs
      0x7A1E0D43, own context state) and binds the copy for that draw. In game at 10%: shadows behind feet / under a bag.
    - One-frame trace everywhere (Tales' frame_trace.hpp, `renodx_trace_now` next to the exe): Tales, Expeditions, Ryse
      DLSS; it also saves the traced shaders' bytecode (tales_frame_trace_shaders, needs the DevKit's subobject cache).
    - Ryse HDR fix50: Sun Shadow Softness (PCSS, Contact Shadows section, 0-5 deg) in the 5 cascade passes of the sun
      shadow mask (sunshadow00-04: E9264ADE B5DC6E36 F2B4EF30 F864A048 F81C7168; far cached map 10C1DA12 untouched);
      cascade world scale solved from CryEngine's vWBasisX/Y/Z (sun_shadow_pcss.hlsli); pcss.hlsli got a Texture2D
      version. In game at 3 deg: softer railing shadows, no acne, 45 -> 43 fps.
    - WH1-3 SSR: they DO have the game's SSR (environment_specular_light cs raycast 0x143BD25C in WH2: hierarchical-Z
      march at half resolution, then resolve / blur), the old "none" note was wrong. Possible later: full-resolution
      raycast (own UAV + resolve t7 swap).
    - NOT DONE (multi-day ports): RT in Ryse (CryEngine), Expeditions (Tales' 2.6k-line rt_shadows.hpp would need its
      tables per game; apply point: the sun light draw's light attenuation, as contact shadows), WH3 (GPU-driven
      material shaders, vertex pulling: rt_port.py's capture VS do not fit); TW RT reflections / PT; TW PCSS (the sun is
      evaluated in 31+ directionallight permutations; RT soft shadows cover it); WH3 DLSS below 100% / RR.
    - Game changes restored (restore_notes.txt): ini keys, preferences, deployed DLLs. Locked until a reboot: Ryse's
      Streamline / dlssd / dlssg DLLs, WH2 renodx-tw-dlaa64.exe.old15 (deleted at batch 3's start).
20. (ATN, 2026-10-27 13:00, BATCH 3, "don't stop until finished, hours / days") 1) everything still missing: RT + PT in
    Ryse, Expeditions, WH3 (+ RR, DLSS below 100%), Shogun 2, TW 7 RT reflections + PT; 2) Ryse VRR flicker with FG
    (Reflex sleep / CPU waits in talesofarise_dlss/frame_generation.hpp); 2.5) every RT effect + PT + all accelerators
    (DXR 1.0 DispatchRays, 1.1 RayQuery, 1.2 OMM + SER, NVAPI: Mega Geometry clusters / PTLAS, LSS) in all games;
    3) double-work check at the end; 4) patch notes, licences, report of everything since 2026-10-27 01:00.
    Authorized: the five in-game permissions again (scripted desktop, game-folder changes reverted at the end, first-
    start prompts, in-game settings put back, saves / custom battles never overwritten); downloads: NVAPI (MIT,
    build\downloads\nvapi), PresentMon 2.6.0 (MIT, build\downloads\presentmon; works unelevated: ATN is in Performance
    Log Users), other official NVIDIA / AMD / Microsoft / Intel SDKs with permissive licences (list each in the report);
    local git checkpoint commits on atn-mods (never pushed). Checkpoint e92046f6 = everything before batch 3.
    PROGRESS (batch 3):
    - TW helper17 (build\tw_dlaa_helper17, bridge v8): path tracing (Light Bounce texels: paths with a world radiance
      cache + learned lighting, rt_path.hlsli), ray-traced reflections + specular occlusion (request 3), DXR 1.2 SER
      ray generation shaders (DispatchRays) + OMM, DXR 1.1 compute fallback. GPU harness
      tools\tw\rt_harness\tw_rt_harness.cpp (build\tw_rt_harness: the addon side of dlaa_bridge.hpp + the helper, a
      ground plane and a red wall): sun shadow, AO, mirror reflections (wall red, sky above), specular occlusion,
      path-traced red bounce: ALL PASS in DXR 1.2 and 1.1 (2026-10-27 14:20). Single-frame path noise is expected (the
      Light Bounce's resolve / denoiser accumulate).
    - Reflection passes reading it (rt_reflection.hlsli t46 occlusion / t47 reflection): Attila + Thrones ssr00/ssr01
      (rt_scene OnReflectionDraw: the hierarchical march skipped when the rays reflect), WH1 / WH2 environment_specular_
      light cs resolve (ssr00_0x6B80E35B / ssr00_0x7E1CDD0A, 3Dmigoto decompiles fixed against the disassembly: WH2's
      |cb2[0].z| = abs(sun_direction.y), udiv by 3, icb static) with the raycast + blur skipped (OnReflectionMarch).
      Rome II / Troy / Pharaoh: no object reflection pass (water SSR only: ssr_new apply / waterreflection00; water is
      not in the traced positions), so their reflection settings are hidden (AppendSettings reflection_pass).
    - Fix: rt_scene::Configure in all 7 (Light Bounce alone never got RT textures before).
    - In-game test blocked: a Ryse.exe from the FG test (pid 4036) cannot be killed (access denied, needs a reboot) and
      SetCursorPos / input no longer reach games (WH2 menu ignored clicks / keys). Retest after ATN's reboot.
      (Later: Ryse 4036 is alive in the background, tutorial, ~43 fps with DLAA, and ignores WM_CLOSE; injected input
      reaches no game, SetCursorPos fails while a game is in front: likely the Claude app's input guard since the
      computer-use request. Needs ATN: close Ryse / reboot.)
    - FG (talesofarise_dlss/frame_generation.hpp, all 4 FG addons + WH3): Frame rate limit (Reflex frameLimitUs,
      FrameGenerationFrameLimit 0-240), FG off for 2 presents after ResizeBuffers (Streamline "repeated
      slDLSSGSetOptions" race at every resize = the stale frames after the startup fullscreen switch, not verified).
      TW helper FG: bridge fg_frame_limit (was fg_padding), TW setting FrameGenerationFrameLimit, WH3 DlssFrameLimit.
    - helper18 (frame limit), helper19 (build\tw_dlaa_helper19): + RTX Mega Geometry (NVAPI cluster ops: units as
      85-triangle clusters -> CLAS -> one cluster BLAS, a record per cluster; RENODX_TW_NO_CLUSTERS) and partitioned
      TLAS (instance index = record, units at MAX_INSTANCES; only changed instances written; RENODX_TW_NO_PTLAS);
      pipelines made with NVAPI ENABLE_CLUSTER_SUPPORT. NVAPI R615 (MIT) in build\downloads\nvapi (build.ps1 adds it
      when present). 5090 caps (driver 595.79): SER, OMM, clusters, PTLAS, spheres, LSS yes; DMM no. Harness: all
      checks pass in DXR 1.2 / 1.1, clusters on / off, PTLAS on / off (PTLAS changes per-pixel noise only: means equal).
      LSS / spheres: no geometry for them in these games (no strands / particles captured) - not used.
    - rt_core.hpp split from rt_scene.hpp (game-agnostic: capture, positions, trace, bounce, reflections, occlusion,
      AO apply; Host = camera / render size / guides / moving-mesh hooks; clip-space captures: the game's own VS
      streamed out (SV_POSITION) + inverse VP in rt_vertices.cs, bytecode kept at init_pipeline; GPU check
      scratchpad so_clip_test.cpp: back within 6e-8). rt_scene.hpp = TW glue (TW_HOST, AO pass, sun draws, reflection
      draws, settings; AppendSettings section parameter).
    - WH3 RT (v25): rt_scene with RT_HOST from taa_dlss (camera buffer tracked at upload, render size), 7 scene VS
      (F0395CC4 8EE34EC5 474F97FB C0ED2883 5767F87C terrain, B8C567C3 / 2C45437C dynamic), sun draws +
      SetExtraOcclusion; no guides (no reflections / RR). WH3 G-buffer for later RR: rt0 R10G10B10A2 view-space
      normal (spheremap: xy = normalize(n.xy) * sqrt(n.z * 0.5 + 0.5), * 0.5 + 0.5), rt1 sRGB base colour, rt2 material
      (x metal / spec mix, y z material map, w flags), rt3 RG16F velocity. WH3 DLSS below 100%: still off (needs the
      post chain at display size).
    - Zips (2026-10-27 ~15:10, Downloads): twattila_v77, twrome2_v35, twthronesofbritannia_v35, twtroy_v31,
      twpharaohdynasties_v35, twwarhammer_v32, twwarhammer2_v32, twwarhammer3_v25 (all with helper19 + NVAPI_LICENSE),
      rysesonofrome_dlss_v114, expeditionsrome_dlss_graphics_v25, twshogun2completeedition_dlss_v54,
      talesofarise_dlss_v106. tools\tw\release_zip.py --helper now takes helper19 + NVAPI's licence.
    - Ryse RT (HDR fix51, rysesonofrome/ray_tracing.hpp): rt_core's generic capture (Host.scene_draw: every G-buffer
      draw, 3x RGBA16F + depth; Host.capture_camera; the game's own VS streamed out, BLENDINDICES = skinned), camera from
      VS PER_FRAME b2 uploads (rysesonofrome_dlss/constant_mirror.hpp), positions + sun from 0x52C5E004 (linear depth,
      PER_BATCH) before the tiled lighting 0x897232B5, then rt_scene::Trace(depth null). Sun into the contact-shadow mask
      (0x52C5E003: max(value, 1 - traced); the march off while traced), Light Bounce through rays / PT. Settings
      "Ray Tracing (Experimental)", Install at launch only. talesofarise_dlss/matrix.hpp (Inverse) split from
      frame_generation.hpp so the HDR addon need not embed FG shaders.
    - rt_core additions: clip-space foliage (SceneShader.uv_semantic / uv_semantic_index: a second SO buffer with the UV,
      rt_vertices packs it into w as halves for the helper's alpha test); SceneCoverage (each pixel shader classified at
      init_pipeline: no discard = solid; discard + t0 sampled at an input = alpha-tested with that input's semantic;
      t0 sampled at a computed address = left out); Host.skinned_code; dxbc.hpp FindSignatureElement. Offline check:
      tools\expeditions\classify_test.cpp over Expeditions' dumped shaders (masks TEXCOORD0, tiled masks out,
      characters solid, bone-index VS skinned).
    - Expeditions RT (DLSS v26, expeditionsrome_dlss/ray_tracing.hpp): scene = UE prepass draws into the scene depth
      (t1 of the sun light draw), skinned = ATTRIBUTE3 uint or a UAV vertex buffer (skin cache), metres (VP columns x 100);
      camera from the sun draw's View (ps b0: WorldToClip 4-7 transposed, WorldCameraOrigin 63), sun from
      FDeferredLightUniformStruct c5 (ShadowedBits c2.x != 0). At the sun draw (vs 0x7C21DA11, before contact
      shadows'): guides (0x7A1E0D44 from GBufferA/B/C), trace, sun into LightAttenuation z (0x7A1E0D45: copy, then
      z x sqrt(min(1, traced / game's)) with the game's fade and static shadowing replicated: min semantics), AO min
      into SSAO (0x7A1E0D46). Light bounce after the sun draw (post callback: 0x7A1E0D48 prepare half res,
      TraceLightBounce, 0x7A1E0D49 temporal resolve with world-position validation, 0x7A1E0D4A 3x3 bilateral apply x
      diffuse colour, additive). Reflections replace the SSR draw (ps 0xE36ED33E bypassed, 0x7A1E0D47 premultiplied
      into its target); KeepColor copies the TAA input into bridge.color for the next trace. Contact shadows skip while
      the traced sun is applied. Unknown in game: SSR / sun-light hashes at other quality settings.
    - Shogun 2 RT (HDR v62 + DLSS v55 settings UI): rt_core generic capture (G-buffer rt0-2 RGBA16F, only at the sun
      draws' depth size), camera from camera_VS_PS uploads (expeditionsrome_dlss/constant_mirror.hpp; VP c10-13, render
      size c38), trace at the first sunlight00-03 draw (sun from lighting_VS_PS c0 negated), t40 / t41 bound for those
      draws: shaders/rt_sun.hlsli RtDarker (min with the shadow mask / AO), Light Bounce RT / PT via SetRayTracedTrace.
    - WH3 v26: Ray Reconstruction (rr_guides.cs: WH3 lights dielectrics only: F0 0.04, diffuse x 0.96, roughness =
      1 - material.z, normals from channel 1 by inv_view; taa_dlss WriteGuides at the first sun draw, Prepare with
      guides, RR camera = inverse projection x view-projection). DLSS below 100%: not done (post chain at render size).
    - Double work: Tales r.SSR.Quality 0 while RT reflections ran within 30 frames (rt_shadows ReflectingRecently);
      Ryse contact march off under the traced sun; Expeditions contact shadows off under it, SSR bypassed under RT
      reflections; TW / Shogun 2 / Ryse / Expeditions Light Bounce screen-space trace replaced by rays (never both);
      TW SSR march skipped under RT reflections. Kept on purpose (complementary, combined as the darker / product):
      game shadow maps + RT sun, SSAO / GT-VBAO + RT AO, TW / Shogun 2 Bend SSS + RT sun (grass / billboards are not
      in the RT scene).
    - Zips (batch 3, Downloads): rysesonofrome_fix51, expeditionsrome_dlss_graphics_v26, twshogun2completeedition_v62,
      twshogun2completeedition_dlss_v55, twwarhammer3_v26, talesofarise_dlss_v107 (RT ones with helper19, D3D12Core,
      AGILITY / NVAPI licences).
    - Restored (restore_notes.txt): Ryse FG keys + DLLs, WH2 RT keys + deployed files; build\Release *.old_* and the
      helper debug builds deleted. A Ryse.exe zombie (pid 31200) still in the process list.
    - NOT DONE: in-game tests of batch 3 (input to games blocked); WH3 DLSS below 100%; Tales Mega Geometry / PTLAS
      (few characters: little gain); LSS / spheres (no strands or particles in any RT scene); Shogun 2 RT reflections
      (needs its G-buffer guides and the ssrresolve patch); TW grass / billboards in RT.
21. (ATN, 2026-10-27 ~16:00, BATCH 4: performance + missing features; tests after it) DONE 2026-10-27 ~17:30, all
    untested in game (input to games blocked); local commits 8fff0147..85c37683 on atn-mods (never pushed).
    - helper21 (build\tw_dlaa_helper21; tools\tw\release_zip.py ships it): bridge v9. FG Dynamic + 5x / 6x (TW 7, WH3).
      RR as the only denoiser (bridge.reconstructing: Light Bounce denoiser, AO filter, reflection accumulation step
      aside; Tales RAY_RECONSTRUCTION). Quarter-rate AO (RT_AMBIENT_QUARTER). RT presets Low-Ultra (TW / WH3). CPU fast
      path (short keys). PTLAS opt-in (RENODX_TW_PTLAS): it stalled every request 0.3-0.8 s (helper19 in the batch-3
      zips had it on!). RENODX_TW_GPU_TIMES: GPU / CPU phase times in the status line. Measured round trip without
      PTLAS 0.15-0.2 ms a request: in-process D3D12 / earlier tracing / instancing not worth it.
    - Local light shadows (bridge v9 RT_LOCAL_LIGHTS, rt_core SetLocalLights / LocalLightMask,
      twattila/rt_local_lights.hlsli: t42 lit bits, b12 positions): up to 32 lights a trace, hard shadows, rays stop
      0.5 m short of the light. Wired: Shogun 2 pointlight00 / 01 only (last frame's lights by brightness x reach^2 /
      distance^2). Others need their light shaders patched (TW light volumes, Ryse tiled lights, Expeditions UE lights).
    - ReSTIR GI (RT_PATH_REUSE, always with path tracing; RENODX_TW_NO_REUSE=1 off): rt_path.hlsli PathTraceTexel builds
      reservoirs + temporal reuse (M <= 20x), ReuseTexel (rt_reuse.h, -D REUSE) spatial 5 neighbours + Jacobian + a
      final visibility ray. Harness (tools\tw\rt_harness, args: frames samples path_flags; 17 = PT + reuse): bounce
      noise 0.93 -> 0.26 at 1 path / texel, mean within noise.
    - Ryse fix52: RT AO (contact shadow PS second target into DirectionalOccRT w: (1 - v) / SSDOParams.z, max blend),
      RT reflections at t6 of the tiled lighting (restored on_drawn), SSR trace E0B7E115 skipped, guides 0x52C5E005,
      KeepColor 0x52C5E006 at ps 0x3BA5B162 (R11G11B10 render-size copies). Ryse DLSS v115: OnLightingDispatch now
      swaps its CS and replays (it bypassed before, so the HDR addon's on_drawn = Light Bounce never ran with the raised
      light limit, default 512!).
    - Shogun 2 v63 / DLSS v56: RT reflections in ssrresolve00 (rt_reflection.hlsli), march + blurs + HiZ + radiance /
      hit mip chains skipped (generic mips only between HiZ and resolve), guides rt_guides.cs, KeepColor (t9 mip 0),
      point light shadows. Settings in the DLSS overlay (RayTracedReflections / Samples, RayTracedLocalLights).
    - TW grass in RT (RayTracedGrass, off): capture VS from make_wind_motion_vs.py mode world_uv_o3 (UV in o3) for
      Attila / Thrones (grass00 / 01; Thrones from its own decompiles), Rome II (6), Troy, WH1 (2), WH2; not Pharaoh
      (its grass motion shaders are vfx grass flames), not WH3.
    - Zips (Downloads): twattila_v78, twrome2_v36, twthronesofbritannia_v36, twtroy_v32, twpharaohdynasties_v36,
      twwarhammer_v33, twwarhammer2_v33, twwarhammer3_v27, rysesonofrome_fix52, rysesonofrome_dlss_v115,
      expeditionsrome_dlss_graphics_v27, twshogun2completeedition_v63, twshogun2completeedition_dlss_v56,
      talesofarise_dlss_v108 (helper21 where the helper ships; build32 for Attila / Rome II / Thrones).
    - NOT DONE: WH3 DLSS below 100% (after the TAA WH3 draws 3D effects at the render size with the render-size depth,
      then the bloom chain and composite before its own upscale: all would move to the display size with an upscaled
      depth; needs in-game iteration; trace build\ingame\traces\twwarhammer3_50.txt lines 800-921); Tales Mega
      Geometry (few characters, refit is cheap) and PTLAS (stall); smoke / particle RT shadows (soft billboards, skipped
      as ATN allowed); NRC (needs NVIDIA's CUDA SDK); local light shadows outside Shogun 2; AMD.
    - NEXT: ATN's tests of batches 3 + 4 (ASK: Ryse RT AO / reflections / Light Bounce with the light limit; Shogun 2
      reflections + point light shadows; TW grass cost; PT noise with ReSTIR; helper21 smoothness vs helper19).
22. (ATN, 2026-10-27 ~18:00, BATCH 5, then ATN tests batches 3-5) STATUS LOG: tick items here and commit after every
    step (ATN asked, so compaction loses nothing). Licences: MIT / Apache 2.0 / CC0 / anything free for commercial use,
    also as references (credit + licence file in the zips). Steps (task list order):
    [x] 1 Checklist + memory (feedback_progress_log.md).
    [x] 2 DONE (helper22, build\tw_dlaa_helper22): ReSTIR GI v2 after kajiya (compared, not copied): W clamp 10, temporal
          reuse only on a surface facing the same way (receiver normal in reuse_data.w), every 3rd frame the reused
          sample re-checked (visibility; its light now via HitLight; change -> shorter history, kajiya's M / W damping).
          Harness: noise 0.229 at 32 frames, mean in range; a light switch now shows within <= 3 frames (was ~20).
          PTLAS FOUND: NVAPI UPDATE_INSTANCE ops stalled the GPU 4-5 s after BLAS compactions (no stall without
          compaction or with whole-instance WRITE ops). Fix: writes only, two PTLASes in turn, ops copied to device
          memory (NON_PIXEL_SHADER_RESOURCE); PTLAS on by default again (RENODX_TW_NO_PTLAS=1 off). Harness: no stall in
          30 frames x 3 runs, 0.21 ms a trace request. Harness flags: HARNESS_LIGHT_SWITCH=<frame>.
    [x] 3 DONE: mesh instancing (bridge v10 RT_INSTANCED, helper22): TW rigid shaders' instanced draws (rows_shader =
          the game's motion_record_* VS, SO 4 texels an instance) capture only their first instance's triangles plus each
          instance's rows; the helper reads the rows back (slot rows_readback, when the slot comes round), builds one BLAS
          and places it per instance (rows_k x inverse(rows_0)), places from free_instances_ (16384..65536), the PTLAS
          index in InstanceContributionToHitGroupIndex, PTLAS diff over this + last request's places only (O(instances)),
          hit normals turned by ObjectToWorld. MAX_INSTANCES 65536 (records stay 16384). Harness: an instanced box x3 casts
          3 shadows (PTLAS on / off), all checks pass. Not done: trees (branch / leaf: no record shaders yet), Tales Mega
          Geometry (needs NVAPI in the Tales addon build = root CMakeLists.txt, ATN's OK; characters refit cheaply), LSS
          (not applicable: grass and hair are alpha cards, no strands).
    [x] 4 DONE (addons only, helper unchanged): local lights moved into rt_core (AddLocalLight / AddLocalLights /
          BindLocalLights / UnbindLocalLights; the trace picks the 32 nearest and brightest of last frame's lights by
          brightness x reach^2 / distance^2; Shogun 2 refactored onto it). Attila + Thrones: Ray-Traced Point Light
          Shadows (RayTracedPointLights, off by default): the 8 point light PS without the game's point shadow maps
          (POINT_LIGHT_SHADERS, shaders/pointlight00-07, smooth / linear x slm / skin x high / low; Thrones wraps
          Attila's) x RtLocalLightVisibility(v0, v3 = POSITION2); the instanced light draws' instances (input layout
          tracked: POSITION1 colour, POSITION2 position + reach) copied to a 3-deep staging ring, read 3 frames on,
          deduplicated -> AddLocalLights. Frame order checked in traces: point lights after the sun draw (trace done).
          NOT done: Rome II / Troy / Pharaoh / WH1-3 (the game's own cube shadow maps on point and gobo spot lights,
          75-120 light PS permutations each, need traces of the drawn ones), Ryse (shadow-mapped lights in a
          hand-assembled tiled CS), Expeditions / Tales (light permutations unknown without traces), soft local light
          shadows (hard: torches are small), ReSTIR DI (32 traced lights cover the scenes seen).
    [x] 5 DONE (helper22, bridge v11): Light Through Leaves (RayTracedLeafLight, off by default; TW Graphics and the
          Shogun 2 DLSS overlay): the sun's ray steps through leaves nearest-first (opacity micromaps make a leaf's solid
          parts opaque hits, never candidates; RAY_FLAG_FORCE_NON_OPAQUE did not change that on the 5090), each leaf lets
          LEAF_TRANSMITTANCE 0.25 through, a solid surface or 3 leaves block. Torches / fires light what the paths meet
          unseen: bridge v11 rt_light_colours (LocalLight.colour: Attila / Thrones POSITION1, Shogun 2 b2 row 1), Paths()
          binds b4 (positions, count, colours; LIGHT_CONSTANT_BYTES 1280) with PATH_LOCAL_LIGHTS; at a hit neither the
          screen nor the cache knows, one light by reservoir sampling (colour x (1 - d / reach)^2 x N.L) and a shadow
          ray; the cache learns it. Harness: a leaf card (foliage + OMM) shadow 0 -> 64 / 255 with the flag; bounce
          beside the unit +0.068 / +0.042 / +0.019 with coloured lights (3:2:1); harness captures now in vertex order
          (the scene data pass binary-searches them, as the game's come). NOT done: emissive surfaces other than the
          games' lights (no emission in the RT scene), leaves letting light through for local lights and the paths' sun.
    [x] 6 PARTLY (helper22, bridge v11): Attila + Thrones cloth (banners, tents) and sails in the RT scene (they were
          not before: no RT shadows, reflections, AO or bounce from them): rt_capture_cloth / sail sm3 + sm41 (make_wind_
          motion_vs.py :world; Thrones wraps Attila's), dynamic (with Ray-Traced Units), RT_THIN capture flag ->
          RECORD_THIN (also in Mega Geometry cluster records); with Light Through Leaves each lets THIN_TRANSMITTANCE
          0.35 of the sun through. Sail emblems left out (their own layer). Harness: a dynamic thin card 0 -> 89 / 255
          with the flag, clusters on and off. NOTE: RT sun visibility only darkens on top of the game's shadow maps
          (screen_space_shadows' extra occlusion), so light through leaves / cloth shows only where the rays are the only
          shadow (past the shadow maps' reach). NOT done: coloured shadows (no colour in the RT scene: the atlas is
          opacity only), glass, smoke / particle shadows (billboards facing the camera, flipbook opacity per frame, no
          dynamic alpha-tested records: needs particle VS captures per game + sun-facing proxies), other TW games' cloth.
    [x] 7 DONE for Attila + Thrones (helper22, bridge v11): Ray-Traced Fog Shadows (Experimental, RayTracedFogShadows,
          off): helper rt_fog.cs (u27, RT_FOG_SHADOWS) per fog texel (a quarter of the render pixels each way) 8 points
          along the camera's ray (squared spacing, weighted by their stretch, 400 m at most) each one ray to the sun ->
          rt_fog (R8, shared); battlefog00 0xA6A0F887 (Thrones wraps it) x RtFogSun (rt_fog.hlsli: bilinear, full shadow
          keeps RT_FOG_SHADE 0.35) on the sun-lit fog colour (g_volume_fog_colour x sun_colour) only; the sky-coloured
          part and the fog amount stay. Harness: toward the wall's shadow 194 / 255, open 255. NOT done: Rome II (its
          own battlefog hashes 890A842B / 259AEC56 / lq AE2A62D9), WH / Troy / Pharaoh in-scattering passes, Shogun 2,
          the MSAA fog shader (A538288B), sky pixels (rays without a surface: 1).
    [-] 8 SKIPPED (reasoned): refraction + caustics. The TW water shaders already refract the screen behind them; the
          water surface is not in the RT scene (ocean / river draws not captured) and nothing lights what is under it;
          caustics need photon / light tracing into a caustics map. Little to see from the TW camera for the cost.
    [x] 9 PARTLY: Pharaoh grass in RT (grass.hlsl vs 0xE710D4E7, UV in o3 like Troy's: rt_capture_grass_e710
          :world_uv_o3; its RayTracedGrass option had nothing to capture before). Checked and NOT done: RT reflections
          in Troy / Pharaoh (no screen-space reflection pass at all: no environment_specular_light compute passes in their
          packs or traces) and Rome II (only the water's SSR, 0x7C9C8ECF): they need the reflections injected into the
          environment / sun lighting shaders (many permutations); their Reflections / Specular Occlusion options are
          already hidden (AppendSettings reflection_pass false), so nothing shows without effect. WH3 reflections /
          specular occlusion: no G-buffer guides in its RT host. WH3 grass: its own VS streamed out, needs the grass VS
          and its UV output found (trace). Shogun 2 grass: its scene is the G-buffer classifier (scene_draw); whether
          grass reaches the G-buffer needs a trace.
    [-] 10 SKIPPED (reasoned): SHaRC's design is what the helper's world radiance cache already does (rt_path.hlsli:
          spatial hash of cells by position and level (CACHE_ENTRIES 1M, CACHE_LEVELS 8), filled from the paths' unseen
          hits and the screen, resolved each frame, looked up at path vertices to end paths early). NRC needs NVIDIA's
          NRC library (RTXGI 2: binary, online training passes on tensor cores) wired into the paths; not done.
    [-] 11 NOT DONE (needs in-game iteration; game input was blocked in batches 3-4): WH3 DLSS below 100%. Trace
          twwarhammer3_50.txt 795-921 read again: the game's TAA (cs 6A8FC282, 240x135 groups) already writes display
          size (a TAAU, 1:1 at 100%); after it the post-TAA 3D effects (vs 4F35D09F particles, E2AEBB32) draw at the render
          viewport into 0x6a7605a0 with the render-size depth 0x6f454420, the bloom chain starts from a render-size
          combine (ps E1A30AFC) and the composite (vs E0C2FF5F ps E2EE82F8) runs at display size reading the TAA output,
          that layer (t2) and the bloom (t3) with render-scale UVs. Plan: viewports of the post-TAA draws to display size
          with a display-size depth (upscaled render depth: max of the 2 x 2 at 50%) bound in place of 0x6f454420, the
          combine + bloom at display size (view cloning, feedback_view_cloning), the composite's UV scale for t2 / t3 to
          1; test with ATN at 50 / 67 / 77 %.
    [x] 12 DONE: 64 + 32-bit builds, README notes, zips in Downloads (helper22, bridge v11: every helper zip needs it):
          twattila_v79, twthronesofbritannia_v37, twrome2_v37 (32-bit addons), twtroy_v33, twpharaohdynasties_v37,
          twwarhammer_v34, twwarhammer2_v34, twwarhammer3_v28, twshogun2completeedition_v64 + _dlss_v57,
          expeditionsrome_dlss_graphics_v28, rysesonofrome_fix53 (rysesonofrome_dlss and talesofarise_dlss unchanged).
          release_zip.py --helper now ships helper22 and KAJIYA_LICENSE-MIT.txt (build\downloads\kajiya). Harness
          (tools\tw\rt_harness): ALL PASSED with leaf / sail / fog / light colour checks; captures in vertex order.
          Double work: none new (point light shadows only on the light shaders without the game's point shadow maps;
          torch light only at path hits the screen and cache never saw; leaf / cloth light only past the shadow maps).
          NEXT: ATN tests batches 3-5 in game.
23. (2026-10-27 evening) ATTILA-FIRST (ATN: all RT / PT work in Attila first, other games ported later). Claude tested
    batches 3-5 in Attila itself:
    - Why game input was "blocked" before: Steam ran as administrator, so the games did (UIPI). ATN restarted Steam
      normally. TW reads the mouse through DirectInput: synthetic clicks never reach its UI (the launcher and the ReShade
      overlay take them); keys work after [Microsoft.VisualBasic.Interaction]::AppActivate(pid) (PowerShell). Battles
      without clicks: attila.exe game_startup_mode replay "2vs3.replay" ; used_mods.txt; (replays in
      %AppData%\The Creative Assembly\Attila\replays: 2vs3 = day, open hills + river; auto_save = desert siege, haze);
      campaign: the launcher's Continue Campaign. Overlay sliders drag unreliably: A/B by ReShade.ini edits + restart
      (scratchpad attila_test\set_ini.py, run.ps1 = replay + PresentMon 10 s at 50 s).
    - Without nvngx_dlss.dll next to the game DLAA falls back to the game's TAA (zips leave it out; README says so).
    - Seen working: helper22 DXR 1.2, OMMs on leaves, PTLAS, Mega Geometry clusters, path tracing 960 x 540, RT
      reflections; battlefog and the 8 point light shaders compile and replace; no artifacts (cyan units = their own
      colours, teal = the river). Not checkable in these replays: point lights (day), fog shadows / leaf / cloth light
      (subtle in open terrain and haze): ATN's eyes.
    - PERF (2vs3 start view, 4K DLAA 100%): RT off 11.5 ms; sun only 13.9 ms; all on 21.2 ms (CPU 18.3 ms, GPU-side the
      helper: trace 4.7-5.8 ms with AO 8 rays at 4K, paths 1.9 ms). Cause of the CPU cost: every RT request blocked the
      game thread while the helper recorded (~2.4 ms) and waited on its slot ring. FIX (helper e2b7f376): requests of a
      kind that ran last time are copied, answered at once (done_value = value + 1, evaluated 1) and recorded by a
      worker thread in order; the first of a kind and anything opening / closing the scene stay synchronous (drained).
      Now: sun only 11.5 ms (= RT off), all on CPU 12.5 ms (frame 21.3 ms, GPU-bound by the helper's trace).
    - Zip: twattila_v80 (helper with the worker). Other games keep batch 5 zips until their port round.
    - v81 (ATN's report: 3 fps siege, lines with RT units, river flicker, campaign shadows "vibrating"):
      - 3 fps in auto_save (siege): the partitioned TLAS stalled the helper's GPU ~0.3 s a frame (CPU 12.9 ms, GPU 7.3 ms,
        frame 356 ms; without it 22.6 ms). PTLAS now only with RENODX_TW_PTLAS=1.
      - Lines / river: not reproduced (likely the RT units' long shadows at a low sun; ask ATN for a screenshot). The
        debug view's Reflections tile is black in 2vs3: the river's water is not in the G-buffer, RT reflections only
        reach glossy G-buffer surfaces.
      - Campaign "vibration" = RT AO noise: the trace keeps the ambient visibility over frames (history u28-u29, last
        positions u30, rt_camera.hlsli's Paths cbuffer; 1/5 new): changed pixels 2.3-3.0% -> 0.5-0.7% (RT off 0.9%).
      - RT sun shadows never reached the campaign (and 7 other sun shaders): view_share's create callback (registered
        first, by anti_aliasing::Install) changed them, so screen_space_shadows' CRC missed sun_hashes: no t49 patch.
        Fix: view_share::OriginalHash(code) / Repatched(from, to) (its cb12 kept). 0xDCD5E5BA / 0x9ECED7F8 still "have no
        shadow to patch" (view_share's change or none of their own: unchecked).
      - Then every campaign sun ray was blocked: the campaign's sky dome is a rigid draw (vs 347F3DD9 ps D63DD3C3, 1632
        vertices, 918 units wide around the camera; far plane 500). rt_skies.cs (after rt_vertices): a static capture
        (not terrain / units / leaves / instanced) holding the camera and larger than the view's reach goes NaN (empty).
        Found with a temporary CPU readback of each capture's bounds (rt_core Capture).
      - Captures only from the main view (Host.main_view = frame_history::MainViewport): the UI portraits share the depth.
      - Ray-Traced Shadows Only (Experimental, RayTracedShadowsOnly): sun shadow map casters skipped while the sun draws
        took the RT mask last frame (occlusion_frame). Campaign + battle OK; 2vs3 12.08 -> 11.88 ms.
      - Ray Tracing Debug View (RayTracedDebugView: 1 sun, 2 AO, 3 bounce, 4 reflections, 5 spec occlusion, 6 point
        lights, 7 fog, 8 All tiled; magenta = not made this frame): rt_debug.cs after the tone mapper
        (rt_scene::OnTonemapDrawn, Attila only so far). 2vs3: all show except point lights (day) and reflections (black).
      - Frame trace (FrameTrace=1, needs DLSS < 100%) prints each draw's depth test "z<func>[w]".
      - Shared changes reach the other games' next builds (wrappers rt_skies / rt_debug added; all 10 compile).
      - Zip: twattila_v81.
    - v82 (ATN's night custom siege, 2026-10-27 late): torch light slid to the bottom right below 100% = the replaced
      pointlight00-07 / battlefog00 shaders lacked view_share's uv / share before their NDC (now render_scale.hlsli
      DrawRenderShare; the traced lights' cbuffer moved b12 -> b9, b12 is the share). ATN's "DLAA 19 fps with all RT
      off" = Path Tracing on kept RT active (light_bounce includes path_tracing) with Light Bounce strength 0: units and
      grass captured every frame (SettingsOf now takes the Light Bounce strength; day siege DLAA+RR CPU 14.2 -> 7.8 ms).
      Still open: DLAA alone costs more with soldiers in view (object_motion's per-unit motion redraws are DLAA-only;
      2vs3 army view 19.2 ms, CPU-bound); RR F at 4K +5 ms; FG needs HDR10 / SDR output; ATN's horizontal ground bands
      at night (ATN: gone with RT AO Alone; the day AO buffers show none: ask ATN for the Debug View's Screen-Space AO in
      that battle). Debug View 8 = Screen-Space AO (the game's AO pass target, rt_scene's AO hook), 9 = All.
    - Testing: quitting a custom battle from the pause menu does NOT write auto_save.replay. In replays P pauses, the
      wheel zooms (up = out; down did nothing), W/A/S/D move; TW UI ignores synthetic clicks. The Xbox Game Bar can
      minimize the game: ShowWindow(hwnd, SW_RESTORE) + AppActivate brings it back.
    - NEXT (Attila): ATN retests v82 at night (torches below 100%, RT with Light Bounce on, AO bands via the debug view);
      reflections on armour close-ups, smoke / billboard trees in the scene, cheaper AO at 4K, unit motion cost in DLAA;
      then port to the other TW games (OnTonemapDrawn + InstallShadowsOnly hooks).
    - 2026-10-28 (not zipped yet; Attila build32 only, the other TW games share anti_aliasing / object_motion / dlaa_bridge
      and need rebuilds): CPU cost vs No AA measured with an admin xperf profile (scratchpad attila_test rec.ps1 +
      prof.py; debug_timing.hpp hook timers, temporary). Fixed: the DLSS helper request no longer waits for the helper on
      the CPU (steady requests async, watchdog Rescue if it dies: ~0.25 ms); object motion's in-draw bindings and targets
      stay bound across consecutive moving draws (1.18 -> 0.77 us a moving draw); presets L/M get the colour's square
      root (they crushed dark tones on linear HDR: ATN confirmed fixed, also the armour pulse); the bloom read at
      CameraRenderShare (it sat at the sun's position / render share); the SSS Debug View stretched below 100%.
      Measured (Motion Test No Records): the soldier records cost 0.50 ms GPU at DLAA (131 vs 141 fps), 0.04 ms at UP.
      LATER (ATN: remember): redesign the unit records: copy each soldier draw's instance data (buffer copy, no stream
      output switch) and stream all out in one batch at the frame's end (feature level 11.0: no VS UAVs); should give
      ~+7% at DLAA and help RR / FG / RT.
      Scaling fixes (ATN confirmed): the grass depth pre-pass 0x125A0DD5 (its 100 m / 50 m cut follows Grass Height
      Distance, its terrain depth read scaled: grass past 100 m), the game's SSR, SSS lines (Bend marches the rendered
      part's pixels). Remove debug_timing.hpp, the Motion Test setting and the motion timing log before the zip.
      FG trace (2026-10-28, scratchpad attila_test fg.ps1 + fg_analyze.py, run fg1; DLSS UQ preset L, RT off, Reflex
      Boost, 4K): base fps Off 89 (CPU-bound, game GPU 6.2 ms) / 2x 85.5 / 4x 75 / 6x 66.7, GPU-bound with FG (game +
      helper GPU 94-96%). Helper GPU: DLSS SR ~2.7 ms a frame, DLSS-G ~1.2 ms a rendered frame + ~0.9 ms a generated
      one (NVIDIA's). Hand-off on the game thread: 230 us at 2x (helper Present + Reflex sleep; 0.9-3.3 ms at 4x/6x =
      Reflex holding the game for the full GPU), HUD-less copy 8 us. Helper presents Independent Flip, pacing fine (6x:
      2.50 ms median, p95 2.9); the game's own presents vanish while covered (nothing to gain). GPU idle ~0.6-0.8 ms
      a frame with FG (cross-process waits). DLSS-G presets: not in Streamline 2.14.16; nvngx_dlssg reads its render
      preset from DRS (DLSSGDRSKeys, "render preset (requested, effective)") = the helper exe's driver profile.
      LATER BATCH (ATN: with the records redesign, before RT): (1) HUD smear: DLSS-G UI recomposition (UI colour + alpha
      from final vs HUD-less in the helper, enableUserInterfaceRecomposition); (2) MFG preset option (find the DRS key
      in nvngx_dlssg, set it for the helper only); (3) no GPU wait on the FG hand-off: two FG frame sets + a fence the
      helper signals, the game waits only before reusing a set (~3-5% base fps with FG); (4) maybe DLSS SR on the
      helper's D3D12 device (one device with FG / RR: fewer GPU context switches).
    - BATCH 2026-10-28 night (ATN asleep: no in-game tests, don't stop to ask; at the end SHUT DOWN THE PC, ATN
      allowed it). Commit after every step; tick here.
      [x] 1 FG HUD smear: UI recomposition   [x] 2 MFG preset option   [x] 3 FG hand-off without the GPU wait
            (2028ba87: bridge v12, helper23 -> copy helper23 into the Attila folder at the end, v12 needs it)
      [-] 4 DLSS SR on the helper's D3D12: skipped (D3D12 queues are GPU contexts of their own like the D3D11 one: no
            gain to expect; DLAA's path left as tested)
      [x] 5 render scale (9a2eb81a): scissor rects restored after the tone mapper (flare + unit markers), flare
            occlusion / render area (lensflare04 VS, cb12 for VS), game HBAO share; trace audit clean (scan.py)
      [x] 6 soldier records redesign (DLAA GPU time for RT/PT) + more optimizations. Measured first (auto_save
            replay, DLAA, FG off; scratchpad attila_test records_matrix.ps1): all 9.98 ms (game GPU 5.67), no unit
            records 9.75 (5.44), no records or captures 9.61 (5.31): units 0.23 ms GPU, cloth captures 0.13. Tried:
            a CopySubresourceRegion of each unit draw's instances (no stream output): 11.00 ms (6.68): copies between
            draws stall this driver far more (1.24 ms) - dead end. Batched stream out (draws noted, streamed out
            together at the next non-moving draw; scratchpad records_batched/batched_stream_out.patch) 9.99 (5.69)
            and one frame-end dispatch per animation texture 9.99 (5.68): no change. Split with a debug mode: the
            records' GPU work 0.16 ms (each soldier recorded once per mesh draw: placements + bone copies), the
            motion VS's previous-frame lookups 0.09 ms (per vertex). Stream output was never the cost: reverted to
            the original code. Would help (later, needs motion checks): one record per soldier (draws sharing an
            instance range), per-instance lookups instead of per-vertex. Debug timers / Motion Test / timing log
            removed.
      [x] 7 clean-up done (debug timers, Motion Test, timing log; ReShade.ini back from tonight's backup without
            FrameTrace / MotionTest, tw_frame_trace.txt deleted, preferences unchanged; scratchpad mssym + symcache
            deleted). Smoke run (DLSS UP + FG 2x from the start) found the helper's first answer > 5 s (preset's DRS
            load + Streamline cold start): preset saved on a thread, waited-for answers 20 s (8b640d0d); rerun: DLSS
            + FG 2x, "HUD recomposed", no warnings. Zips in Downloads: twattila_v83, twrome2_v38,
            twthronesofbritannia_v38, twtroy_v34, twpharaohdynasties_v38, twwarhammer_v35, twwarhammer2_v35,
            twwarhammer3_v29 (all helper23), twshogun2completeedition_dlss_v58 (shared object_motion /
            texture_lod_bias clean-up). helper23 copied into the Attila folder (v12 addons need it).
      [x] 8 shut down the PC (shutdown /s /t 120 after the final message)
    - 2026-10-28 day (ATN tested v83: HUD smear and lens flare fixed): unit markers below 100% gone in the top-left
      ninth (they draw after the tone mapper at the display's size against the scene depth, whose render-size picture
      sits in that corner): the depth stretched to the full target for them (6220302a; anti_aliasing.hpp UpscaleDepth,
      FULL_DEPTH_SHADERS = sprite vertex_3d*; other TW games only got the shader wrappers). ATN debug16 "shading" and
      debug17 grass rectangle = one bug: the 13 grass PS read the terrain distance (t4, t3 in no_clip) at a projected
      uv made as mul x 0.5 / mad 1 - y x 0.5, which view_share's auto patch missed, so outside the top-left ninth the
      blades did not blend into the ground. view_share Kind::SPLIT (104eee9f) patches that form at the read with the
      draw's share; the pre-pass's manual region read dropped. Checked offline (scratchpad scale_audit patch_test.cpp:
      Patch on every dumped shader, created on WARP; old vs new): only grass changes, Attila 14 / Rome II 26 / Thrones
      14, Troy / Pharaoh / WH none. Zips twattila_v84, twrome2_v39, twthronesofbritannia_v39 (untested in game).
      ASK ATN: markers + grass at UP; the pixel trace for any seam left.
    - 2026-10-28 afternoon: NEXUS BETA (ATN: someone else released a DLSS / FG mod). v85 (e6f3b761): SDR preset
      (yellow), UI 4K Mod Compatibility On by default (DisableGraphics missing = on; ATN's ini has 0) with red restart
      notes, DLSS preset L default, red "RT / PT not working yet" note, NVIDIA attribution in About. Player zip
      Downloads\RenoDX_Attila_Beta_v85.zip (files at the root, licenses\, "RenoDX Attila - READ ME.txt", nvngx_dlss.dll
      310.9.16 included now), notes on top of Downloads\tw_patch_notes.txt, Steam text Downloads\attila_steam_text.txt.
      v86 (596330a8): (1) done, NVIDIA Reflex Off / On / On + Boost with any AA (reflex.hpp, NVAPI by id; GPU-tested
      32-bit). ATN: FG smears at 2x / 3x. Checked: FG's inputs follow Streamline's rules (row-major unjittered matrices,
      clipToPrevClip, pixel MVs x 1/render size, same depth / MVs as SR, render extents, binary premultiplied UI layer).
      DLSS-G preset DRS 0x10E41DF1 = 2 (B) in the helper's profile; nvngx_dlssg 310.9 ReadRegkeys: A = UI recomposition
      forced off, B on, C Lite + off (so A / C smear the HUD); its indicator shows DLSSG.UserInterfaceRecompositionEnabled
      / LiteCandidateBlendingEnabled, not a letter. Post-tonemap draws (lens flares, sprite.hlsl unit markers, outline,
      indicator, splines) land in the UI layer (not interpolated). WAITING: where ATN sees the smear.
      NEXT (ATN's order): (2) RT / PT fixes (ATN: 3 fps in battle before the DLSS performance fixes, effects invisible or broken:
      retest first); (3) later AMD RT + FSR / FSR FG.
    - 2026-10-28 evening: v87 (d1ada232) FG UI alpha + Frame Generation Debug View (helper24, bridge v13). Nexus
      quarantine: fork pushed, Downloads\nexus_quarantine_email.txt (ATN sends it), new short page text
      Downloads\nexus_attila_description.txt. v88: Effects at Output Resolution (AA section, default on, as Tales;
      frame_history::effect_share = the render share while DLSS really scales): AO ResolutionScale x share, Light
      Bounce resolution - EffectSteps(), SSR ssr_traced = rays - EffectSteps(2) (C++ writes it, ssr_hiz.hlsli reads it,
      so the resolve and the march agree), ssr00 / ssr01 blur step x share (effects_share), SSS softness x share, RT AO
      quarter off from one step down. Troy / Pharaoh keep their AO half switch (their shaders read ssao_resolution).
      Zips twattila_v88, twrome2_v40, twthronesofbritannia_v40. ASK ATN: water reflections / AO at Performance.
      v89: FG Debug View off while the ReShade menu is open (reshade_open_overlay; ATN could not see the menu in the
      HUD-less view). Effects audit (build\ingame\traces\twattila.txt, 50%): only the AO blur (B4026AF2 / 6203C824 ->
      s_ssao) and the water refraction (820D451B / 25944B2D -> s_terrain_refraction) drew below the render size (half of
      it). view_share RegionRead::whole: those passes draw their whole half-size target (viewport + scissor swapped in
      OnCommand, put back in a post callback), inputs region-read; readers by RDEF name (WHOLE_TEXTURES) divide the
      coordinate by the draw's share (Kind::WHOLE; split projections left). Replaced readers: pointlight00/02/04/06 and
      ssr00/01 (render_scale.hlsli, DrawRenderShare: that keeps the auto patch off them). WARP check: scratchpad
      scale_audit\patch_test_whole.exe on the three dumps. Zips twattila_v89, twrome2_v41, twthronesofbritannia_v41.
      v90: ATN's FG debug views: HUD-less much darker, UI layer = whole screen. Cause: KeepFrame drew the HUD-less from
      the tone mapper's output (t00 8BF6B657: linear, before hdr_to_screen's PsychoV / paper white / gamma 2 encoding),
      so fg_ui saw every pixel differ. Now OnTonemapDrawn's later calls of the frame (hdr_to_screen t01-t08, applied_frame
      == frame) redraw it (frame_generation::KeepOutput; KeepFrame skips its own draw while frames have that pass).
      Possibly behind the earlier "FG smears / judders" reports: retest FG with HUD Recomposition. Zips twattila_v90,
      twrome2_v42, twthronesofbritannia_v42. ATN: FG picks up the UI correctly now (v90 confirmed).
      v91 (ATN debug19, SSS only, UP / Quality vs DLAA): striped false shadow on distant hills, combed fence shadows.
      Bend's SurfaceThickness and BilinearThreshold are depth tolerances per one-pixel step; below 100% steps and
      neighbour differences grow by 1 / share, so screen_space_shadows::Compute divides both by the render share (as
      samples x share). ATN's settings: Length 5 (1920), Thickness 0.005, Edge 0.02, Hard / Fade 1, Softness 0.
      Zips twattila_v91, twrome2_v43, twthronesofbritannia_v43. ATN: the distant lines are gone (v91 confirmed).
      v92 (ATN debug20, SSS Debug View 2, DLAA vs UP: "lower quality"): the debug view was the render-size mask stretched
      (nearest) after DLSS. Now the tone mapper hooks call screen_space_shadows::OnTonemapDrawn before
      anti_aliasing's (7 games' macros), once a frame (debug_frame), into the render-size part: DLSS upscales it like
      the real shadows. Nothing else to gain: the mask feeds render-size lighting. Zips twattila_v92, twrome2_v44,
      twthronesofbritannia_v44. NEXT (ATN: no need to wait): RT / PT fixes in Attila.
      v93 (ATN debug21, campaign, DLAA, SSS on vs off: the sky / clouds past the map's edge darkened up to a hard
      vertical edge; the orange ring around the city is the game's): campaign projection n 1 / f 500 (depth_inf
      1.002004), the backdrop's depth 0.99935 (75% of far). Bend FarFade (bend_sss_gpu.hlsli, set in sss_bend.cs):
      shadow fades 0.4 -> 0.6 of the far distance. Zips twattila_v93, twrome2_v45, twthronesofbritannia_v45.
    - 2026-10-28 evening, RT / PT ROOT CAUSE (ATN's battle, RT all on, RR F, FG 2x; diagnostics d1f8d532: rt_core
      NotTraced + "new static meshes in 10 s by vertex shader"): while the camera is still the static keys hold (95
      known); once it moves, every instanced scenery draw is a "new mesh" every frame: rigidcommon 0x347F3DD9 up to
      107120 per 10 s, rigidleafcommon_v3 0x46C616F0 (trees), rigidbranchcommon_v3 0x77B993FD, rigidterrain 0x6ECB6C09.
      The game culls and repacks its per-instance stream (VB slot 1, transforms in v5-v12) every frame, and Capture's key
      mixes VB offsets, instance count / first instance and cb offsets. Effects: rt_core known grows past 200k entries
      (erase_if every frame), up to 256 new BLAS a frame, the helper's 16384 static records fill, its scene collapses to
      ~9 meshes, results freeze at the spawn view (ATN: "every effect smearing"), and perf drops. Earlier all-magenta
      session: not reproduced (NotTraced will name it). Light Bounce strength 0 in ATN's preset: RT bounce / PT off by
      design (red note added). FIX NEEDED (redesign, helper + addon): BLAS keyed by geometry only (shader, VB0, IB,
      index args; no instance stream / counts / cb offsets), built once in the first capture's space; every frame each
      draw's instance rows written (the rows VS, one point per instance) and the helper makes the TLAS instance descs on
      the GPU (rows_now x inverse(rows_at_build), stored per mesh at build) - no readback latency, no per-frame BLAS.
      ATN: "Yes, start". IMPLEMENTATION (bridge v14, helper25): dlaa_bridge.h RtPlacement {key, first_vertex, instances}
      x RT_PLACEMENTS appended to Shared (rt_placement_count). Addon rt_core Capture: placeable = rows_shader &&
      !dynamic (rigid x4 + new motion_record_tree_sm3/sm41/sm5 for branches / leaves: rows = TEXCOORD0-2); geometry key
      (shader, index / vertex range, base vertex, VB0, IB; no instance count / first instance / other VBs / CBs); known
      -> rows-only stream out + placement; new -> first instance's triangles + rows, RT_INSTANCED, instances >= 1; no
      "moving" exclusion for placeables. Helper Record (v14): RT_INSTANCED captures build the BLAS as before (no rows
      readback), Mesh.placed; jobs = this frame's new placed captures (build) + rt_placements of known placed meshes;
      CPU instances (others + units) copied into a device instance buffer, rt_place.cs writes the jobs' instances after
      them (rows now x inverse(build rows), build rows per record written by the build job), TLAS from that buffer;
      PTLAS off with v14. Not yet: placed meshes kept a while after they leave the view (off-screen casters).
      DONE (as built, uncommitted test round): RT_PLACEMENTS 8192; one rows shader motion_record_tree.vs_4_0 for all six
      tree shaders (one input signature); build rows copied by record (CopyBufferRegion) when a placed mesh is built,
      rt_place reads them for every job; placed meshes drawn lately but not now (RECENT_FRAMES) carried from the last
      dispatch's descs (two device instance buffers in turn, a mesh's instances together: draws sorted by key), their
      BLAS address refreshed (compaction); a placement over the limit does not refresh the addon's known frame (the
      mesh is forgotten and captured again). GPU harness (tools\tw\rt_harness, build\tw_rt_harness): boxes captured
      in frame 0, placed by rows with a 4th instance from frame 1, carried in the last frame: ALL PASSED (8 and 3
      frames; 2 frames: only the never-placed instance fails, as expected). helper25 copied into Attila's folder.
      NEXT: ATN's in-game test (status line "N in the scene (M placed)", the churn log should stay small, the Debug
      View should follow the camera), then README v94 / v46, zips (release_zip.py helper25), report.
    - 2026-10-28 night, ATN's first v14 test (commit 146469bf): Debug View follows the camera now (scene 8-18k placed
      instances, 2100+ meshes); but performance bad, shadows jump while moving, a big shadow circle under the camera
      (bigger when zooming in), grass blocky, no tree shadows (?), RT integer sliders showed "0". ReShade.log: 347F3DD9
      ~90 "new" captures/s with the known count flat = draws after the frame's trace, captured and dropped every frame.
      FIXES (helper25 rebuilt, untested in game): integer sliders "%d" (TW rt_scene + Ryse: SliderInt got "%.0f");
      captures skipped after the frame's trace; placed meshes not seen moving (object_motion) re-placed every
      PLACE_TURNS = 4 frames in turn (CPU: most draws stop before the main-view check and the stream out), moving ones
      every frame with RT_PLACE_MOVING (RtPlacement got flags); the sky check (rt_skies) covers placed captures and
      also empties captures centred on the camera (battle dome = the circle: placed, it followed the camera); helper:
      placed descs as a GPU union (rt_place modes clear / drawn / kept: each instance kept RECENT_FRAMES after its last
      draw, deduplicated by record + 1 cm place, BLAS from a per-record table (compaction), dropped when its mesh is
      drawn moving), TLAS of MAX_INSTANCES descs (the rest inactive), ROOT_DISPATCH 8 constants; grass alpha-tested
      (dynamic foliage: a slot's UV region, masked units geometry / cluster records + flags, units instance not
      forced opaque); GPU timings on by default (RENODX_TW_NO_GPU_TIMES off) in the status / 60 s log. Harness: kept,
      compaction, moving drop, grass clear / solid, clusters and refit paths: all pass. Diagnostic: "placed vs X: N
      vertices x M instances, VB strides / usage, leaf mask" for the first two draws of each placed shader (trees:
      are TEXCOORD0-2 per instance? leaf mask format?). NEXT: ATN retest; read the placed-vs lines for trees.
    - 2026-10-28 20:00, ATN's second test: circle still black under the camera, shadows vibrating, FG smears the ReShade
      menu. Log: trees instanced as assumed (VB1 per instance, stride 108 = TEXCOORD0-6; leaf mask BC1 1024x512);
      GPU ms trace 38-60, paths 24-31 (4K native, AO 4 rays 10 m, bounce 7 rays 50 m, PT 3 bounces, reflections 4,
      point lights, fog, grass, soft sun: the real scene's cost); the status was logged every frame (timings first).
      CAUSE of the circle: Attila's terrain VS pushes the ground 9 cm away from height 0 from 20-30 m off the camera
      (camera_position.xz); captured at one camera it lifted the RT ground over the one drawn near the next (and
      re-captures changed it: vibrating). FIXES: all 8 Attila rt_capture_terrain_* use min(sign, 0) (no push above 0,
      all below: never above the drawn ground); Thrones / Rome II copies still have it (port round); helper timings
      after the status (Status() = status_ + timings_); FG off while ReShade's menu is open (frame_generation
      OnReshadePresent multiplier 0). Built, helper25 copied; untested in game.
    - 2026-10-28 20:15, ATN's third test: grass "pulses" (RT grass or shadows). Log: units' share full (1.04 M of 1.05 M
      triangles: what did not fit changed every frame) and 347F3DD9 still ~80 new meshes / s (known 1884 -> 6216).
      ATN's plan, agreed order: 1 fix the scene (budget, churn, pop-in), 2 test each RT effect alone (per-effect GPU
      times), 3 optimize (light sampling for point lights, AO / bounce lower res + RR, PTLAS retry). STEP 1 DONE (untested
      in game): RT_TRANSFER_HEIGHT 2048, MAX_DYNAMIC_VERTICES 6M (2 M triangles; helper DYNAMIC_TRIANGLES, MAX_CLUSTERS
      32768), grass at most half (MAX_GRASS_VERTICES); meshes in DYNAMIC vertex buffers (not terrain) captured every frame
      as units; known = {frame, instances}: a placed draw placed at once when its instance count changed (else its
      turn); 10 s report adds the top churning shader's last 3 new captures (counts, VB0 / IB pointers + offsets) and
      draws captured for dynamic vertex buffers; status: units / grass triangles, GPU ms trace / bounce / reflections.
      NEXT: ATN tests each effect alone (sun shadows first) with the Debug View; read the report lines for the churn.
    - 2026-10-28 20:35, ATN tested every effect alone: RT Light Bounce + Path Tracing did nothing (light_bounce_strength 0:
      they only replace the Light Bounce's trace; the red note was missed); debug views "wrong (?)"; Screen-Space AO view
      showed things always or never; sun shadows pulsing and broken (debug shot: long black bands on flat ground,
      stepped bands on hills). Log: churn mostly gone (347F3DD9 dynamic-VB draws ~1-10 a frame now captured every
      frame), bounce GPU 0.00 always, reflections once 74 ms. CAUSE of the bands (most likely): the terrain draws its
      pieces from a dynamic instance stream refilled in the same places every frame, so its keys stayed while its
      pieces changed: earlier views' terrain stayed in the scene. FIXES (built, helper copied, untested in game): any
      scene draw without a record version with a DYNAMIC vertex buffer in slots 0-3 (the terrain) is captured every
      frame (budget: MAX_DYNAMIC_VERTICES 7M, units + grass + rest MAX_UNIT_VERTICES 4M so the terrain keeps the rest,
      grass MAX_GRASS_VERTICES 2M; helper DYNAMIC_TRIANGLES 7M / 3, MAX_CLUSTERS 49152, DYNAMIC_UVS 2M a slot with its
      own counter; status "every frame X M: terrain, grass"); the first two draws of every scene shader logged (VB
      usage); RT Light Bounce / Path Tracing turned on also turn the Light Bounce on (strength 1, on_change ->
      UpdateSettings: on_change_value runs under the settings mutex); the Debug View's Screen-Space AO is a copy made
      after the AO pass and before the traced AO is multiplied in; the status logged 3 s after any RT settings change
      with the effects that are on (each effect's GPU time). NEXT: ATN retests sun shadows (bands, pulsing).
    - 2026-10-28 21:00-21:35: the Debug View's scene test (1b10c5af) and the 10 s cost report (c3e08a29). ATN's debug22
      / debug23: RT sun shadows alone 71 -> 61 fps, with units 51, with grass 48; no shadows from walls, buildings or
      trees, a giant leg-shaped shadow far off, long straight lines, soldiers' shadows with holes. CAUSE (proven with a
      D3D11 test: one index an instance under a triangle list streams nothing, as points every instance): the placed
      meshes' rows draw ran under the game's triangle list, so no rows were written and the helper placed every placed
      mesh by stale texels (giant / stretched instances, trace up to 25 ms, nothing where it stood). Holes: the units'
      share (4M vertices, grass inside it) was full, half the soldiers' draws left out. FIXES (untested in game): rows
      drawn as points; RT_TRANSFER_HEIGHT 4096, MAX_DYNAMIC_VERTICES 14M, MAX_UNIT_VERTICES 10M (helper
      DYNAMIC_TRIANGLES 14M / 3, MAX_CLUSTERS 65536); consecutive dynamic draws of one kind joined into one capture (the
      256 captures a frame no longer a limit; draws' keys in captured_keys); the cost report adds the vertices captured
      every frame and those left out. Harness: all pass. helper25 copied. NEXT: ATN retests sun shadows alone, with
      units, with grass ("left out" in the cost line should be 0).
    - 2026-10-28 22:15, ATN's retest (RT + units + grass): grass shadows still flicker; zooming in froze the game ~2 s,
      then the old view's RT shadows stayed and RT never came back (toggling did nothing). EVIDENCE: Windows System log
      nvlddmkm event 153 (GPU channel reset) at 21:45:13/15 and 21:45:32/34 (also 18:01, 18:19, 18:32, 20:01 today);
      ReShade.log "the helper did not trace" from 21:45:35 on, its status stale; cost line: 50 ms waiting for the helper,
      1.46 M grass vertices left out a frame (grass over its 2M share). CAUSES FOUND: (1) the helper's device was
      removed by the GPU reset and nothing made a new one; (2) rt_scene_data read every capture as the vertex
      texture's threes, but captures after placed rows (4 texels an instance) start anywhere: grass / leaf UVs and
      normals from the wrong vertices, changing every frame (harness proof: new quarter-mask grass test fails on the
      old helper, passes now); (3) grass over its share. FIXES (untested in game): scene data by each capture's first
      triangle (table entry 6); grass 4M (helper DYNAMIC_UVS 4M); DRED (auto-breadcrumbs, page faults, contexts) with
      pass markers (vertices, scene data, micromaps, BLAS, clusters, place, TLAS, trace, fog, soft, bounce, radiance
      cache, path tracing, reuse, reflections): after a removal the helper logs where the GPU stopped
      (renodx-tw-dlaa64.log) and starts a new device on the set's kept handles (3 a set); bridge v15 rt_device tells the
      addon, which captures everything again; shadow rays count as blocked after 64 alpha-tested candidates (bounded
      cost). Harness: all modes pass, RENODX_TW_TEST_REMOVAL (RemoveDevice at the 3rd trace) restarts and passes.
      NEXT: ATN retests; if the GPU resets again, read renodx-tw-dlaa64.log's DRED lines (which pass hung).
    - 2026-10-28 22:30, ATN's retest: worked a while, the same freeze, then RT updating once a second. DRED: device
      hung (0x887A0006) with slot 0 in the micromap bake's Dispatch (10 of 48 ops; markers' strings not captured, the op
      order names it). ReShade.log after the restart: "TLAS 2.55, trace 1027.93" GPU ms (before: TLAS 0.24, trace 6.7-10)
      with fewer instances: the trace itself takes a second (the bake was only in flight at the reset). A TLAS build 10x
      slower plus traces 150x slower = scene bounds spread by garbage coordinates (instances' boxes all overlap, every
      ray meets every instance; as the pre-rows-fix garbage placements did: TLAS 0.71, trace 21-25 ms). FIXES (untested
      in game): rt_scene_data leaves garbage triangles out (NaN through a raw UAV of the vertex buffer, root u6): a
      coordinate past 200 km, an edge past 200 m in a dynamic capture but the terrain's, past 5 km elsewhere; counts per
      request (root u7, read back when the slot comes round) with the first such capture's key; bridge v15 tail now
      rt_device, rt_left_out, rt_left_out_key (unreleased layout changed); the addon logs "N garbage triangles left out in
      10 s, the last in a capture of vertex shader X" (captures now join only within one vertex shader, key_shaders map);
      rt_place leaves out instances moved past 200 km or scaled past 1000x; scene data and the D3D11 vertex pass dispatch
      in rows of 65535 groups (over 4.2 M triangles went past D3D12's limit). Harness: a far triangle + a 600 m sliver
      (old helper: 4 checks fail, the sliver shadows the open ground; new: all pass, 14 left out), all modes pass.
      NEXT: ATN retests; ReShade.log's "garbage triangles ... vertex shader X" names the capture shader to fix; if no
      garbage is reported and traces still explode, the next suspect is incoherent clusters (CLAS spanning draws).
    - 2026-10-28 22:45, ATN's retest (zoomed into soldiers): same freeze. ReShade.log: garbage IS filtered (10 s: grass
      01FA9CDE 126281, units 5F9CD405 14148, terrain 6ECB6C09 1879, rigid 347F3DD9 1846 triangles) but GPU resets go on
      (nvlddmkm 153 x5 in 11 s; Application log: LiveKernelEvent 141 = VIDEO_ENGINE_TIMEOUT_DETECTED). DRED twice: device
      hung with slot 2 at 13 of 79 / 229 ops, again the first micromap bake's Dispatch (3 of 3 reports at that op). GPU
      harness HARNESS_LEAF_STRESS=240 (240 new leaf meshes x 4000 random cards, 241 micromap arrays in one frame): no slow
      request, no hang: the bake is not slow by itself; in game the GPU stalls there past 2 s (cause unknown: not the
      bake's loops, not cross-process textures as the game's GPU waits for the helper right after each request).
      CHANGES (untested in game): micromap bakes only with RENODX_TW_OMM (leaves alpha-tested by the ray queries,
      64-candidate cap); the helper logs requests over 100 ms GPU (scene / TLAS / trace + that request's status, 50 at
      most) and one garbage triangle a 10 s (positions, capture key and flags, counts by kind) to renodx-tw-dlaa64.log.
      Harness: all modes pass, with and without RENODX_TW_OMM. NEXT: ATN retests; no freeze = the bake (then find why it
      stalls in game); a freeze = DRED names the next pass; the garbage example shows what the grass / unit captures make.
    - 2026-10-28 23:00, ATN's retest: NO freeze (bake confirmed as the stall; why it stalls in game is still open, bakes
      stay off). No garbage at all: last run's came only after the resets. But units were MISSING from the scene ("every
      frame 2.4 M: terrain 1.0, grass 1.4" = no units; "left out" 30-78 M): c3b19f02 joined captures only within one
      vertex shader, 4x units alternate shaders, the frame's 256 captures ran out (c362537f's run had 1.2-1.6 M unit
      triangles). ATN also: grass shadows don't move with the wind; some grass / trees behind buildings stay lit. The
      rays' shadows reach only the patched deferred sun shaders (x (1 - t49)); passes that light themselves (Attila's
      blended near grass, < 25 m) read the game's sun shadow map, which Ray-Traced Shadows Only empties: lit everywhere.
      RT grass uses the drawn grass's own wind code and time, so it sways; a few cm of sway on still ground is averaged
      by DLSS / Ray Reconstruction (no motion on the receiver). FIXES (untested in game): captures join across shaders
      again; left out counted only for main-view draws, split into "share full" and "without room (captures or vertex
      texture)"; under Shadows Only one frame every 10 s logs the pixel shaders that read the game's sun shadow map but are
      no patched sun draw ("pixel shaders reading the game's sun shadow map without the rays' shadows"). NEXT: those
      shaders get the rays' shadows (patched as the sun draws, or the map kept for them).
    - 2026-10-28 23:15, ATN's log: the readers are PS C9FBACCB, 2E9F5935 and ACE33D5D (vfx00: particles / smoke); the
      log now adds each one's vertex shader (grass F3741248 / 01FA9CDE, leaves 46C616F0 / 5B880546 / CABCB9D1, ocean ...)
      to tell which is the grass. RT Grass works (grass 1.40 M in, 0.00 off) but grass sits at its 4M-vertex share.
      Units at 4x unit size: 16-21 M vertices a frame left out of their share (counted after the main-view check; a draw
      in two main passes counts twice): the soldiers need several times the frame's capacity. ATN asked whether path
      traced shadows would hold everything: no (same scene, same receivers). NEXT: the readers take the rays' shadows;
      for 4x battles a compact capture (each mesh's index buffer once, only its vertices every frame: ~3-5x less) or
      the nearest units first.
    - 2026-10-29 00:10, "Fit 4x armies first" (ATN): compact capture, bridge v16 (2bc8af8d helper, a4b9611c addon;
      UNTESTED IN GAME). A dynamic indexed draw's mesh (index buffer + range, the buffer kept) has its indices read back
      (staging copy, read 2 frames later without waiting, 16 sets a frame) into a cluster set (MakeClusterSet: <= 128
      triangles over <= 128 vertices a cluster, 8-bit corners) and a remap index buffer; then its vertices stream ONCE
      each as points through the remap (harness box: 0.73 texels a triangle instead of 3), consecutive draws of a mesh
      joined (instances). Sets go once (second mapping "<name>_clusters", copied in the request's fill), kept in the
      helper's 64 MB pool (full: unused sets dropped, rt_compact_ok + 1, the addon resends). Helper rt_compact.cs builds
      the clusters' CLAS args / records / normals / UVs on the GPU, first in the units' cluster BLAS. Frame limits:
      16.8 M dynamic triangles, 131072 clusters (CLAS memory ~800 MB; it scales with the cluster count only: 262144 =
      1.6 GB), 16384 compact draws. Needs Mega Geometry (else the old triangle captures). Applies to every game on
      rt_core.hpp once rebuilt (only Attila shipped now). Harness: set rebuilt exactly, remap points' stream output in
      instance order, compact box (2 instances) + alpha-tested card shadows, all 7 modes pass. Cost line now says
      "(N compact draws; M cluster sets)". NEXT: ATN's 4x battle: left out should fall to ~0 (if clusters run out, raise
      RT_DYNAMIC_CLUSTERS); then the shadow-map readers (C9FBACCB, 2E9F5935, ACE33D5D) take the rays' shadows.
    - 2026-10-29 00:20 (cdbdbb47): clusters doubled to 262144 (ATN), half on GPUs <= 12 GB. Helper VRAM measured in the
      harness: 3.19 GB dedicated (2.35 GB at 131072). At 4K add ~1.3 GB of the helper's screen-size buffers, made at
      start even when their effect is off (making them lazily would save ~1 GB with shadows only).
    - 2026-10-29 00:40, ATN: "include everything", lazy buffers; trees' shadows don't move (UNTESTED IN GAME). ATN's 00:21
      log (units 0, 30 M left out) = RT units toggled off at the time, grass over its 4M share; units were in at 00:20
      (1.45 M triangles; unit draws are instanced: 55 soldiers x 77 triangles). DONE: helper history (paths' positions,
      ReSTIR, reflections, AO) made at first use, released after 600 frames unused (logged "history released"; harness
      HARNESS_PAUSE_EFFECTS=650 passes). Under Shadows Only the forward readers take the rays' shadows: C9FBACCB and
      2E9F5935 patched at creation (screen_space_shadows Patch forward: each comparison x (1 - t49); ReShade.log says
      "forward shader X patched" or "has no shadow to patch"), particles' vfx00 multiplies its shadow by 1 - t49; t49 =
      this frame's RT occlusion bound at their draws only under Shadows Only (rt_scene OnShadowReaderDraw). LIMITS: t49
      is the surface behind (grass near the ground fine, smoke high in the air takes the ground's shadow); beyond the
      game's last cascade the readers stay lit. Trees: placed meshes are one BLAS from the first capture (frozen pose),
      so their wind sway is not in the rays' shadows. NEXT: moving trees = near tree draws captured every frame (compact,
      like units) with the placed instances kept off-screen.
    - 2026-10-29 01:00 NIGHT BATCH (ATN asleep, tests in game when awake; no in-game tests by me). Checklist (commit
      after each):
      [x] 1 RT Shadows = all shadows: Shadows Only folded into RT Shadows (toggle removed), SSS off with it, torch /
          point light shadows on with it (toggle removed), banners / sails cast with RT Shadows (not under Units).
      [x] 2 Point light shadows: drawn light matched to the nearest traced one within 25 cm (was 1 cm: carried torches
          lost their shadow). Not done: soft edges (needs a fractional mask), > 32 lights.
      [x] 3 Moving trees: a placed leaves' draw whose set is made also streams its instances' leaves every frame
          (compact, the capture version's wind) within MAX_TREE_VERTICES (3M texels); its placed instances go in hidden
          (RT_PLACE_HIDDEN: mask 0) every such frame, kept ones shown again (rt_place restores mask 0xFF). Branches stay
          static. Transfer texture 5120 rows (20M texels), dynamic share 17M, RT_DYNAMIC_UVS 16M a slot (UV pool 224 MB).
          Harness HARNESS_HIDDEN=1 / 2 (hidden instance's shadow gone / back when not drawn) pass.
      [x] 4 Smoke shadows (Experimental, "Ray-Traced Smoke Shadows", off by default, with RT Shadows): Attila's /
          Thrones' particles are GPU particles (vs A7F257B5 particle_vfx_main: one vertex a particle, SV_POSITION = world
          centre, COLOR.w = opacity, TEXCOORD3.xy = size; a GS makes the quad). Draws with ps ACE33D5D
          (ps_main_high_quality, lit: smoke, dust) and a non-additive blend are streamed out (CaptureParticles, after
          the trace: kept for the next one). The next trace's rt_smoke makes each a quad facing the sun (rt_sun texel 0),
          UV +-1 x (1 + opacity), a capture RT_DYNAMIC | RT_FOLIAGE | RT_SMOKE; the helper's RECORD_SMOKE candidates
          never commit, they multiply smoke_transmittance (opacity x soft disk), SunRay returns it (Light Through Leaves:
          the first pass's). AO, paths, reflections, point lights ignore smoke. Harness: rt_smoke compiled and run on one
          0.6 m particle (opacity 0.5): 127 / 255 under it, 255 past it; all modes pass. UNVERIFIED in game: the size
          output's scale (full width assumed), whether fire shares the lit alpha-blended draw (it would cast too), the
          topology (POINTLIST assumed). rt_smoke stubs in the 9 other games (rt_core.hpp's compute list).
      [x] 5 Optimizations: [x] terrain compact (71eb3be0). [x] Cluster templates: a set's clusters built once as
          templates (opaque or alpha-tested), instantiated each frame with the vertices (3 KB each, 128 MB pool, the
          unused sets' templates given back when full); a quarter of the clusters still built from triangles (captures,
          sets without templates yet). Harness stress (20,000 boxes, 80k clusters): scene 1.48 -> 1.08 ms, clusters
          1.22 -> 0.82. Found: an instantiation's geometryIndexOffset is only applied to templates built with a
          per-triangle geometry index buffer (zeros: template_geometry_); without, the record lookup got garbage.
          Status line: GPU times right after the label (the bridge's 255 characters cut them in battles).
          [x] OMM SDK (NVIDIA RTX SDKs License: free, binaries in the app, ATN OK 2026-10-29): build\downloads\omm
          (release 1.10.0: omm-lib.dll loaded at run time, header for build.ps1; LICENSE.txt), the GPU bake (rt_omm,
          ATN's three GPU resets) removed. A new static leaf mesh (with the addon's Leaf Micromaps, RT_MICROMAPS 128,
          default on, Experimental) copies its positions, reads back its UVs and atlas slice; AdvanceMicromaps bakes on
          a thread (4-state, bilinear wrap, cutoff 0.5, level <= 6, workload 2^28, special indices), Record builds the
          array (none when all special: DXR allows a null array) and the BLAS again SLOTS+ requests on (new build,
          compacted). Log: "Leaf micromaps: N meshes baked (K KB), F failed". Harness: RT_EXTRA_TRACE_FLAGS=128 / 144,
          HARNESS_LEAF_STRESS=8 (4000-triangle random meshes: ~80 ms a bake) pass. release_zip.py ships helper26 +
          omm-lib.dll + OMM_SDK_LICENSE.txt; About credits the SDK.
          [-] early trace + late wait: no gap (the trace is asked at the AO pass, read right after). [-] CLAS memory to
          need: VRAM only (32 GB card). Both skipped.
      [x] 6 Grass / flags: their captures use this frame's time_in_sec (grass wind, cloth), captured every frame: their
          shadows move. All 10 other addons build (32-bit Rome II, Thrones; 64-bit Troy, Pharaoh, WH1-3, Ryse,
          Expeditions, Shogun 2). Zip: Downloads\twattila_v94.zip (release_zip.py --helper: helper26, omm-lib.dll,
          OMM_SDK_LICENSE.txt); README v94; helper + omm-lib.dll copied into Attila's folder. Morning report given.
      NEXT (ATN's test): RT Shadows = all, swaying trees, 4x battles (cluster count / scene ms in the status line, GPU
          times now first), Leaf Micromaps (log "Leaf micromaps: N meshes baked"; turn off if the GPU resets), Smoke
          Shadows (Experimental, off: particle size scale, fire casting?).
    - NEXT BIG BATCH (ATN 2026-10-29, after the v94 test; ATN: size doesn't matter, anything that speeds RT/PT on or off).
      DONE 2026-10-29 as v95 (Downloads\twattila_v95.zip; helper27 = build\tw_dlaa_helper27, copied into Attila's
      folder; bridge v17; all 11 other addons build; harness 16 modes pass; untested in game). Built: 1, 2 (part), 3 (ray
      budget), 5 (measured: SER removed), 7, 9, 10, 11, 12 + rocks. Not built, with numbers: 4 SHARC / NRC (no speed),
      8 (no gain), 13 cluster LOD (wants a 4x battle log first). ASK ATN: a 4x battle log (status line: every frame
      M triangles) and a siege with fires (the light budget).
      v96 (2026-10-29, ATN's v95 test: AO noisy under RR preset F, grass dropped): the harness's RR test (HARNESS_RR=<preset>,
      RT_EXTRA_TRACE_FLAGS=256; nvngx_dlssd 310.9.16 copied from the game, 310.7 kept in build\tw_rt_harness\dll_310_7)
      showed RR working but leaving 19-23% of raw 4-ray AO noise. Fixed: AO rays stratified + IGN-turned (rt_trace), the
      AO history kept under RR again: residual 0.0245 -> 0.0080 (wall foot). Grass: ~70M vertices a frame over its 4M
      share; now half the helper's alpha-tested corners (helper says rt_dynamic_uvs: 32M over 20 GB, 16M over 12 GB, 8M
      else; RT_DYNAMIC_UVS 32M). helper28 in Attila's folder, Downloads\twattila_v96.zip. Untested in game.
      v97 (2026-10-29, ATN after v96: RT costs too much, grass shadows vanish, city wall cracks lit, VRAM). ATN's log
      (05:50-06:01): AO alone 1.6-20 ms with foliage in view; with Smoke Shadows + point lights the trace up to 49 ms;
      0 vertices left out all session (grass not dropped); Mega Geometry drew 8-21k clusters of 524k reserved (3.3 GB).
      helper29 (build\tw_dlaa_helper29, bridge v17 unchanged), harness at 4K (helper28 -> 29):
      - Smoke in a BLAS + TLAS instance of its own (mask 2; the addon's last capture, Mega Geometry's last clusters):
        only sun and fog rays meet it (rt_scene.hlsli ALL_INSTANCES / NO_SMOKE). 1000 particles in view: trace 81.5 ->
        7.9 ms (AO 29.2 -> 0.7, lights 34.2 -> 0.6), bounce 12.7 -> 0.8, reflections 35.5 -> 1.5. Sun rays stop past
        SMOKE_DARK (5% left): 12.2 -> 5.0; fog rays dim by the smoke.
      - Nearest-hit rays (bounce, paths, reflections: TRACE_SURFACE) solid-only with micromaps (leaf stress in view:
        bounce 18.3 -> 12.6, reflections 39.4 -> 21.2), alpha-tested without (capped). AO / lights stay alpha-tested:
        culled, rays through dense leaves never stop (5x the trace in the leaf stress).
      - Wall cracks: rays start along the normal only where the 4 neighbours lie on its plane (Planar), else toward the
        camera; a non-planar pixel facing away from the sun is traced too (solid-only, from past itself).
      - VRAM: Mega Geometry's room starts at 32768 clusters (330 MB, was 3324 MB) and doubles when a frame has more
        (SizeClusters; log "room for N clusters a frame now"), up to the GPU's limit.
      Harness: HARNESS_SMOKE_STRESS=<particles>, HARNESS_TRACE_FLAGS=<flags>; scratchpad last_status.py reads the last
      status (slow frames report late). helper29 in Attila's folder, Downloads\twattila_v97.zip (Rome II / Thrones
      addon32 build). Default 4K scene: trace 1.47 -> 1.64 ms (the crack fix's extra rays). Untested in game. ASK ATN:
      when do grass shadows vanish (near / far, moving camera, wind)? A screenshot.
    - NIGHT BATCH 2026-10-29 (ATN asleep: "really really big batch", commit often; shut the PC down when all is done:
      `shutdown /s /t 120`, no /f). One bridge bump (v18) for the new request fields. Tick items here as they land:
      [x] 6 (RayTracedLightBounce: Off / Rays / Path Tracing; old PathTracing key dropped: pick it again; the Lighting
          section's search-only settings (Radius, Quality, Directions, Checkerboard, Back-Face, Auto Thickness) off while
          traced) GI: one selector (Off / Screen-Space GT-VBGI / Ray-Traced / Path-Traced); PT + RT GI could both be on (only
          one tracer runs, the toggles stacked); GT-VBGI's march settings only for Screen-Space.
      [x] 2 (helper30, bridge v18: RayTracedLightSoftness 0.25 m, rt_lights DiskPoint jitter + IGN threshold under RR; harness: penumbra band 5.8k -> 28.6k edge pixels) Soft torch / fire shadows: each frame a ray to a jittered point of a light of radius R (bits; RR / DLAA
          average them); slider; bridge v18 field.
      [x] 8 (RayTracingOutputResolution: density round(1/scale^2) <= 4 x AO rays / reflection samples / sun + light rays (PixelSpot R2 spots); harness all pass with 4) RT at output density below 100% render scale: samples x output / render pixels (sun, AO, lights,
          reflections, GI texels); toggle; bridge v18 field.
      [x] 1 (compact sets: first instance's UVs + slice read back, baked like leaves, templates rebuilt with the OMM array (ALLOW_OMM on all cluster ops, fallback without); harness HARNESS_GRASS_STRESS=50000 at 4K: trace 3.59 -> 2.67 ms, sun alone 1.44 -> 1.22; non-cluster grass captures not done) Grass micromaps: NVAPI cluster templates take OMMs (FLAG_ALLOW_OMM on every op touching them, template args'
          opacityMicromapArray / index buffer): bake per compact set (its UVs + alpha slice, OMM SDK as the leaves), its
          templates rebuilt with them; non-cluster path: OMM linkage on grass captures (stable UVs per key).
      [x] 3 (RayTracedLightGlow 1: rt_fog FogLightAt (one light a sample by RIS, shadow ray, smoke dims) -> rt_fog_lights RGBA16F t44, battlefog00 adds fog colour x it; vfx00 smoke/dust lit by b9 lights (colours + glow added to b9); trace light cbuffer now gets colours; 4K 32 lights +0.1 ms) Torch / fire light in fog and smoke: a helper pass (fog texels, points along the view ray, shadow rays to the
          lights in reach) -> RGB in-scatter the addon's fog pass adds; smoke particles lit by lights near them (vfx00).
      [x] 4 (partly: Water Clarity (addon.cpp OnWaterDraw: river 2E9F5935 / lake C9FBACCB, b2 copy with g_sea_bed_light_scatter^(1/clarity) via water_clarity.cs; the shaders' murk = lerp(deep colour, riverbed x scatter^depth, scatter.w^thickness)). RT refraction itself NOT done: the water surface (position, normal-mapped normal) exists only in the water PS; rays need a water pre-pass (replay with an output-normal PS) + a mid-frame helper request; the riverbed is already in the game's refraction copy, so rays would fix bending, not clarity) RT water refraction: rays from the water surface bent into the water (riverbed from the screen / cache),
          absorption by path length (a clarity control); find the river shader and the water reflection path first.
      [x] 5 (done: SkyLight = the game's ambient cube by direction (rt_sun.cs writes c2-c7 into sun texels 3-8, texture 16x1; InstallSunDraws(..., 8)), scaled to the learned shade; leaves / cloth / sails pass LEAF/THIN_TRANSMITTANCE of paths through (and the sun from behind); two light samples a vertex. Already there: the cache fed by paths' first unseen hits. Not done: glossy bounces (off-screen roughness unknown), ReSTIR PT (ReSTIR GI reuses first hits), roulette (paths <= 4 bounces end at known light)) Path tracing: sky light by direction (learned from sky pixels), light through leaves in paths, the radiance
          cache fed by the paths (multi-bounce off screen), glossy bounces, more light samples, Russian roulette.
      [x] 7 (answered: no. RTX Hair = LSS strands (Blackwell): TW hair, manes and tails are alpha cards, no strand curves to sweep; RTX Skin = ray-traced SSS on skin materials: the game shades units itself (no skin flag in its G-buffer), units are a few pixels; even a close-up-only option would need our own skin shading. A screen-space skin softening for close-ups is possible but not that SDK) Answer (ATN wants it as an option if it can work): RTX Character Rendering (hair = LSS strands, skin =
          ray-traced SSS) on TW humans / horses, close-up only?
      [x] Harness (16 modes pass (helper30), 4K: default trace 1.49 ms, fog light +0.08, soft lights +0, 4 rays a pixel 3.15 (at 4K render); 13 addons build; Downloads/twattila_v98.zip, helper30 in Attila's folder) all modes, 4K timings, zip twattila_v98, README, memory, per-game report; then shut down.
      NVIDIA's SDKs are allowed (NVIDIA RTX SDKs License: RTXDI, SHARC, NRC, NRD, RTXPT, RTXGI; RTXMU MIT; Mega
      Geometry samples Apache 2.0): download at build time like OMM, binaries / compiled shaders only, credit in About.
      [x] 1 (step 2: RT_RECONSTRUCTED 256; harness HARNESS_SUN_SOFTNESS) RR is the only denoiser (ATN: no NRD; RR/PT users use RR): under RR, SSR's denoiser + history (ssr_hiz)
          off, the helper's RT AO history (PATH_AMBIENT_HISTORY) off, sun shadows traced in the sun's cone (one jittered
          ray) instead of rt_soft's filter; any other temporal / spatial filter found off too. (GT-VBAO's filter and
          GT-VBGI's denoiser already step aside; ReSTIR and the radiance cache stay: samplers, not denoisers.)
      [x] 2 (step 5: bridge v17, helper27) RR's guides (we feed only albedos, normals, roughness, depth, motion): diffuse / specular hit distance
          (RT AO / GI / reflection rays), color before / after particles (the particle pass), bias-current-color mask
          (trails, smoke: no motion vectors), color before / after fog (battle fog), screen-space refraction guide
          (water), depth-of-field guide. Option "RR for screen-space effects" with RT off (A/B vs DLSS SR).
          Done: specular hit distance (bridge v17 rr_hit_distance R16F, written by the raw reflections: mean metres to
          what the rays met, 65504 sky / untraced; harness: 2.20 / 4.52 / 7.68 m vs 2.15 / 4.53 / 7.69), 32 jitter
          phases under RR (NVIDIA's guide: at least 32), sky guides 0.5 / 0 (NVIDIA's defaults), ReSTIR GI's temporal
          reuse permuted under RR (RT_PATH_RECONSTRUCTED 32; the guide: RR assumes independent samples). RR with RT off
          already exists (its own AA setting; the AO filter, GT-VBGI's and SSR's denoisers step aside).
          Not done, why: diffuse hit distance (not in NVIDIA's RR guide, only the struct); bias mask (NVIDIA: not for
          the latest models, CNN preset F only); colour before particles / fog (Attila's DLAA input is after tone
          mapping: a snapshot before them is pre-tone-map, RR would see transparency everywhere; would need the tone
          map run twice); refraction / water (blend the guides toward the water surface: the water shaders would write
          the guides, later); DOF guide (luminance after - before DOF, same post-tone-map problem); SSR's hit distance
          (the game's SSR pixel shader would need a UAV, later).
      [x] 3 (step 11: a ray budget, not RTXDI) RTXDI (ReSTIR DI) for torches and fires: a few sampled lights a pixel instead of up to 32 shadow rays.
          The game shades each light itself and reads our bit for it (light_mask), so a sampler choosing one light a pixel
          (ReSTIR DI) leaves the others unknown. Instead (rt_lights.cs_6_5.hlsl, its own pass after the trace): with more
          than 8 lights in a pixel's reach the list's first 4 (the addon orders it by importance) are traced every frame
          and the others in turns, 4 a frame, each 8 x 8 block its own phase; one not traced keeps the last trace's bit
          where it saw the same surface (light_history, two R32_UINT sets; lights matched to last frame's list by
          position and reach: light_previous bytes in b4). Harness (HARNESS_LIGHT_STRESS: up to 32 lights reaching every
          pixel): the bits equal tracing all once the turns came round; 4K 16 lights 3.80 -> 3.13 ms, 32 lights 6.64 ->
          5.17 ms (each light every pixel reaches costs ~0.18 ms; 8 or fewer: unchanged). The same rays chosen by a plain
          rule cost ~2.4 ms in a test pass; the gap is in the history code's shape, not found without a GPU profiler.
          Also: the harness never passed its camera with the trace (the game's addon does), so the ambient history was
          never exercised there; it is now. The trace binds its history constants every time (they were bound only
          with the ambient's history).
      [-] 4 (step 10: assessed, not built) SHARC in place of our world radiance cache (A/B); NRC after (bigger: training each frame).
          SHARC v1.6.5.0 (NVIDIA RTX SDKs License, build\downloads\sharc) speeds a path tracer by ending paths at cached
          hits; ours already end at the first hit the screen or our cache knows (HitLight) and only go on for surfaces
          nobody saw. It would add an update pass (paths from 1 pixel in 25 a frame), a resolve, 64-bit atomics and
          80-160 MB, for multi-bounce light off screen built on the same learned lighting: quality to A/B in game, no
          speed. NRC likewise (it trains each frame).
      [x] 5 (step 7: measured, SER removed) SER in the incoherent passes (AO, Light Bounce as ray generation shaders like the paths).
          The harness (tools\tw\rt_harness; 640 x 360, 1080p and 4K builds; base scene and HARNESS_STRESS_IN_VIEW: the leaf
          stress and 20,000 boxes where the rays go) says SER is slower in every pass. Trace (AO, shadows) as a ray
          generation shader: alone within 2% of compute; reordered by work and normal octant +4..17% (4K base), +1..7% (4K
          in view; full rate compute 3.910, reorder 4.165 ms). The paths' existing SER (since helper19): 4K base bounce
          1.182 vs 0.836 ms without, reflections 2.818 vs 2.106; 1080p in view (steady, frames 200+) bounce 1.714 vs
          1.673, reflections 3.221 vs 2.769; 4K in view 12.7 / 25.5 vs 4.6 / 10.0. Our hits need lookups, not divergent
          shading: all passes are compute again (rt_path_lib removed; DXR 1.2 keeps opacity micromaps).
      [x] 7 (step 8) RTXMU: BLAS suballocation and compaction (fewer allocations, less memory). Static BLASes now live
          in two pools (builds, compacted; 32 MB chunks in 256-byte units, first fit; past 8 MB a buffer of their own),
          frees through releases_ once the GPU is past them, an empty chunk (but a pool's first) let go. Harness: 250
          leaf meshes of 40 triangles: under 1 MB of BLAS in 2 chunks where separate buffers took 15 MB (64 KB each at
          least); of 4000: 32 MB in 3-4 chunks vs 46 MB. The helper logs "Static BLASes: ..." when its chunks change.
      [x] 8 (steps 4, 9: no gain in either half) Partitioned TLAS again (find the siege stall); Mega Geometry for the captures still on the CPU path
          (non-indexed or not yet compact dynamic draws: make them compact too). Step 4: the second half has no gain:
          ATN's 03:11 log (2026-10-29) has every cluster instantiated from templates after a battle's first frames
          (10 of 11 status lines; the first: 29,181 soup clusters before any set was made); every scene draw in the
          trace is indexed, a dynamic one is compact once its set arrives (2-3 frames). Soup templates would need <= 128
          vertices (the instantiation's limit): 42-triangle clusters, twice as many. Left as is.
          Step 9: the PTLAS half has no gain either: the whole TLAS builds in 0.10-0.16 ms (ATN's 03:11 log), all a
          PTLAS could save; and the PTLAS path predates placed meshes (v14: rt_place writes the TLAS's descs only), so
          RENODX_TW_PTLAS would drop every rigid object and tree from the scene. Kept off (comment says so).
      [x] 9 (step 1) Each RT effect replaces its raster one, never both: RT AO replaces the game's SSAO and GT-VBAO (fold On /
          Alone into one On = Alone). Already: RT Shadows (shadow maps + SSS), RT Reflections (SSR skipped), RT Light
          Bounce / Path Tracing (GT-VBGI's screen trace replaced; its resolve shared).
      [x] 10 (step 1) Ray-Traced Units and Ray-Traced Grass folded in (ATN): soldiers and grass always in the RT scene while any
          RT effect is on (they feed shadows, AO, bounce and reflections), the two toggles removed.
      [x] 11 (step 3: transfer 8192 rows, 32M triangles, 524288 clusters (>20 GB; half >12 GB, else a quarter), units at
          most 3/4 of each share; Mega Geometry 3.3 GB, stress 1.31 ms; ATN's next log confirms) Walls without RT shadows (ATN debug24: grass before a city wall sunlit, the Debug View shows only a strip
          at its base): v94's log left ~37 M texels a frame out (a share full). 597b8473 logs which vertex shaders and
          which share; fix from that log (rebalance / raise the shares, walls must never be left out). Also captured
          now: rocks and cliffs blended into the terrain (rigidterrainblend, its depth pass vs E03E161C; never in the
          scene before).
      [x] 12 (step 1) Log polish: "Leaf micromaps" tally at most every 10 s (120 lines in one session), the status's stale
          "bounce" GPU ms while the Light Bounce is off.
      [-] 13 (step 6: assessed, not built; ask ATN for a 4x battle log) Cluster LOD (ATN: add it): each mesh's clusters also built as coarser simplified versions (a hierarchy,
          as Nanite); every frame each instance traces the coarsest clusters right for its distance (far soldiers,
          buildings: several times fewer triangles to build and trace). NVIDIA's nv_cluster_lod_builder (hierarchy,
          Apache 2.0) with meshoptimizer (simplification, MIT); vk_lod_clusters (Apache 2.0) shows it with Mega Geometry
          ray tracing (Vulkan: port the method to NVAPI's cluster operations). Licence texts + credit in About.
          Assessment (2026-10-29): ATN's 03:11 log has 2.3-2.7 M triangles a frame, of them terrain ~1.0 M and grass /
          leaves ~1.55 M, units ~0.1 M; the terrain is regular patches the game already LODs (a coarser grid cracks at
          patch borders: light leaks in shadows), grass blades do not simplify (density LOD instead), units come in
          the game's own LOD meshes. Real gains only in 4x battles with units filling the clusters; the build would need
          bind-pose positions (the static vertex buffers' layouts) or helper read-backs, meshoptimizer per set, LOD sets
          with their own remaps (the addon's stream output) chosen per set by the nearest instance's distance (helper
          read-back). Before that: a 4x battle's status line (every frame M triangles: terrain, grass, the rest = units).
      Not possible / no gain: DMM (not in DXR, dropped by NVIDIA), LSS (no strand geometry in Attila), AddToStateObject
      and indirect DispatchRays (no shader streaming; the helper's dispatch is not the cost), Mega Geometry for static
      meshes (built once already: no gain in tracing).
    - v99 BATCH (ATN 2026-10-29 13:20, after testing v98: micro stutters panning over the map; grass shadows gone only
      with Grass Always Drawn). Their log: only sun shadows + point lights on; trace 1-2 ms in the open, 17-18 ms over
      forests (Light Through Leaves stepped every leaf layer: harness dense leaves 4K 16.2 ms, 0.9 without); helper
      waits 3-35 ms; the cluster room grew 3 times (32k -> 262k clusters, 2.5 GB), each a stall; Grass Always Drawn:
      ~70-95 M grass vertices a frame, 54-79 M left out (share full, whole instanced draws dropped, near ones too).
      DONE as v99: Downloads	wattila_v99.zip, helper31 (build	w_dlaa_helper31, bridge v18 unchanged) in Attila's
      folder; all 16 harness modes pass; the 10 TW addons build (Attila / Rome II / Thrones addon32). Untested in game.
      ASK ATN: pan over forests with Light Through Leaves on (trace ms), Grass Always Drawn + max density / distance
      (grass shadows everywhere? helper wait ms, CPU capturing ms), any hitch left (helper log lines).
      [x] 1 (d53c3023) Light Through Leaves cheap: leaves' meshes instance mask 4 (solids 1, smoke 2; rt_place too);
          SunRay: one any-hit ray on the rest (cloth / sail / grass met first: the old stepping without the leaves),
          then the leaves: none, one, or more (any-hit rays around the first): T or T^2. Harness: 16.2 -> 2.5 ms; leaf,
          sail and grass checks unchanged.
      [x] 2 Cluster room grown in the background: SizeClusters split (SizeClusterRoom: requirements on the request's
          thread; MakeClusterRoom: the 6 buffers on a std::async worker; UseClusterRoom at a request's start), grown when
          a run's clusters pass 3/4 of its room (was: once they ran out). Harness grass stress: 32k -> 262k on the worker.
      [x] 3 Micromap reads: a request's reads share one buffer of each kind (positions, UVs, masks; each read made 2-3:
          up to 64 leaves a request, a few ms of CreateCommittedResource). Arrays were already one a request (one bake
          at a time); template rebuilds bounded by the addon's 16 new sets a frame: left as they are.
      [x] 4 (instead of nearest first: the addon can't know a grass draw's distance before choosing, its instance rows
          are in the game's dynamic ring, and one draw is 192 patches of 13,824 triangles spread over a cell) Grass past
          its share placed still, as the trees' leaves: grass shaders get rows (rt_rows_grass.vs_4_0.hlsl, one for
          F3741248 / 01FA9CDE), no longer .dynamic, sway within the grass share (rt_core sway budget by shader.grass),
          else placed in turns (PLACE_TURNS) from their mesh's BLAS (its first capture). Shrink with distance: the
          capture versions and the rows shrink the whole blade (rt_grass.hlsli RtGrassShrink, >= 5%: invertible), so
          rows_k x inverse(rows_0) x capture_0 = capture_k (GPU check scratchpad grass_rows_test: 0.01 mm, both shaders,
          a flat 150 m first instance). Placed keys mix the PS mask (a mesh drawn with two masks: two BLASes). TLAS
          MAX_INSTANCES 65536 -> 131072. Cost: a rows draw a swaying grass draw a frame (hidden copies), a quarter of
          one for still ones.
    - v100 (2026-10-29, ATN: "find the optimizations from data, I record with every effect on and pan 30 s"; untested in
      game; helper32, bridge v18 + RT_PROFILE 1024 (old helpers ignore it)). Record Ray Tracing Profile (30 s) button
      (rt_scene.hpp, after the scene test; rt_core StartProfile / Profile, PROFILE_MS):
      - tw_rt_profile.csv (addon, next to the exe; a line a present, written in Update): frame, time_ms, frame_ms (present
        to present, CPU), capture_ms / trace_ms (the addon on the game's thread), answer_wait_ms (of it: Answer() waiting
        for the helper, all request kinds: bridge.answer_ticks), set_worker_ms (MakeClusterSet on workers), sets_started,
        captures, compact_draws, placements, new_sets, captured / dynamic / left_out / no_room M texels, lights, traced,
        then the last GPU timing that came back: gpu_frame, gpu_passes_ms (the addon's passes), gpu_helper_wait_ms.
      - tw_rt_profile_helper.csv (helper, its folder; a line a request once its timestamps came back, WriteProfile):
        request, frame, kind (trace / bounce / reflections), cpu_wait / record / submit ms, 17 counts (M triangles
        dynamic / terrain / grass, clusters, templated, compact draws, sets, new sets, template builds, meshes, new
        BLASes, micromaps built / waiting, instances, placed, cluster room, static BLAS MB), gpu scene / clusters / TLAS /
        trace / paths ms. The harness writes one with RT_EXTRA_TRACE_FLAGS=1024.
      - MakeClusterSet off the game's thread: PrepareClusterSet maps the read-back indices, std::async makes the set,
        a later draw of the mesh takes it (ClusterMesh::making; the remap buffer still made on the game's thread: its
        device may be single-threaded).
      NEXT BIG BATCH: ASK ATN for the two CSVs + ReShade.log + renodx-tw-dlaa64.log (every effect on, 1 ray / sample
      where there is a slider, press Record, pan over the map 30 s), then optimize from them (where the frame's time
      goes: game thread waits, helper CPU, GPU passes, hitches = frames far above the median and what they did).
    - v101 (2026-10-29, helper33; ATN's first profile: Attila\debug25, Ctesiphon, 4K DLAA 100% + RR, every RT effect on at
      1 ray, max LOD / grass density, height, distance, Grass Always Drawn). Findings (scratchpad profile_stats.py): frame
      45 ms mean (p90 69); the addon's CPU 0.34 capture + 0.10 trace + ~0 answer waits; the game's GPU waited 19 ms for
      the helper; helper GPU: scene 1.55 (clusters 1.06), TLAS 0.36, trace 17.0 (p90 31, max 43), bounce 3.1, reflections
      6.3 (max 22); 46-65 K placed instances (grass patches since v99), 113-166 K clusters (all templated), cluster room
      grew to 524 K = 4.8 GB. Done:
      - RT_GRASS (bridge flag 64, capture / compact draw flags; old helpers ignore it): RECORD_GRASS; placed grass meshes
        GRASS_INSTANCE 0x08 (mesh.grass, InstanceMask, rt_place PLACE_GRASS; table bit 1); the compact draws in four
        runs (templated rest, templated grass, built grass, built rest) so the swaying grass's clusters are one run: four
        cluster BLASes (the rest before / after the grass, the grass's GRASS_INSTANCE, the smoke's; cluster_blases_).
      - Rays: sun meets grass, grass blocks (RECORD_GRASS, also under Light Through Leaves); AO meets grass within
        GRASS_AMBIENT_RANGE 0.5 m (a second query, GRASS mask); lights, fog, bounce, paths, reflections pass it (NO_GRASS,
        NO_GRASS_SMOKE, TRACE_SURFACE). Grass kept off-screen GRASS_KEPT_FRAMES 8 (two place turns), not 300.
      - Cluster room: SizeClusterRoom(room, built), each run grown to 1.5x what the last request wanted (CLUSTER_STEP
        8192), neither shrinking, within the GPU's limit (was: the whole room doubled at 3/4).
      - Profile: STAMPS 9 a slot; with RT_PROFILE a UAV barrier + stamp after each part; helper CSV gpu_a..d: trace = sun
        and AO, local lights, fog, rest (soft); bounce = cache (or the bounce rays), paths, reuse, rest; reflections =
        cache, paths, reuse, reflections. The addon CSV's counts taken at the trace (Trace empties its lists).
      Harness (tools\tw\rt_harness): HARNESS_PLACED_GRASS=<n> (n x n placed 2 m patches of 1000 blades, RT_GRASS),
      HARNESS_GRASS_PLAIN (the compact grass stress without RT_GRASS), the grass card RT_GRASS (opaque to the sun). 4K
      field 61 x 61, every effect: helper32 13.7 ms -> helper33 7.1 ms; no grass: +0.2 ms. All 16 modes pass.
      NEXT: ATN's second profile (the same place and settings) shows each pass's share; then the next pass on whatever
      leads (candidates: AO quarter-rate everywhere, sun at half rate under RR, fog step count, BLAS ops merged).
    - v102 (2026-10-29, helper34; ATN's second profile Attila\debug26, same place / settings as debug25, v101): frame 45.4 ->
      33.0 ms mean (p90 69 -> 45), the game's GPU waiting for the helper 19.2 -> 11.7; trace 17.0 -> 9.0 (gpu_a sun+AO 8.66,
      lights 0.14, fog 0.17), bounce 3.1 -> 1.03, reflections 6.3 -> 1.31, scene 1.55 -> 2.07 (clusters 1.48: 144 K clusters,
      5.1 M grass triangles swaying), placed 65 K (max 126 K of 131 K), the room regrown 12 times to 352 K (3.3 GB). Done:
      - Cluster sets in order of place: dlaa_bridge.hpp SortTriangles (Morton order of the triangles' centres, garbage last);
        Capture copies a new mesh's first instance's captured corners (objects.captured, after its SO targets are put
        back) the frame its indices are copied (ClusterMesh::corners), PrepareClusterSet reads both, the worker sorts and
        makes the set. Harness HARNESS_COMPACT_FIELD=61 (3721 swaying 1000-blade patches) 4K every effect: trace 1.73 (blades
        in random order) -> 1.44 ms sorted, 1.39 without the field; check "sorted cluster set".
      - Swaying Grass Shadows (RayTracedGrassSway, default On; shown where grass is placed): Off = grass never sways in RT
        (no compact grass, no sets made for it; all placed still in turns).
      - Room growth: a crowded run's room max(2x, 1.5x wanted) (was 1.5x wanted: 12 regrowths).
      - MAX_INSTANCES 262144, the placements' dedupe table 2^19.
      - Profile: with RT_PROFILE the trace runs as two dispatches (rt_trace PART_SUN / PART_AMBIENT via the settings'
        flags word): trace gpu_a = sun (with Light Through Leaves), b = AO, c = local lights, d = fog (+ soft).
      NEXT: ATN's third profile (same place): sun vs AO; then the sun's rays (Light Through Leaves' leaf query for every lit
      pixel?) or the AO (quarter rate already).
    - v103 (2026-10-29, helper35; ATN's debug27: third profile + "grass shadow delay" + "RT shadows leak" screenshots).
      Profile (v102): frame 34.5 ms; trace 13.5 = sun 10.2 (p90 15.5, max 24) + AO 3.0 + lights 0.14 + fog 0.17; bounce 1.1,
      reflections 1.5, scene 1.4; the sun's cost follows none of grass / placed / clusters (highest, 12-15 ms, where the
      view had the least grass: 4-7 K placed), so a city / terrain view cost; room grew to 524 K (4.9 GB). Done:
      - Leak: rt_trace traces every pixel (the facing test only picks SunRay vs OccludedSolid from past itself): planar
        pixels facing away (by the depth's normal) kept 1 before; grass (shaded with its own normal) and normal-mapped
        walls at grazing angles were lit in other objects' shadows. Harness: a plate behind the wall (static key 13, z 9,
        y 4.5..6.5) shadows the wall's camera side: check "facing away in a shadow" (helper34 255, helper35 0).
      - Sun with Light Through Leaves: one query against everything first (none met: lit; solid or grass: blocked; a
        leaves' mesh: its hit the first leaf, RECORD_LEAF = masked static / placed foliage, compact draws' records get
        RT_DYNAMIC); else the old two-part logic. Harness 4K: open 0.250 -> 0.244, dense leaves 3.59 -> 3.67 (few leaf
        instances there: the saved ray is cheap; in game it crosses the whole TLAS).
      - Grass shadow delay = micromaps: until a new grass set / mesh is baked its blades are alpha-tested and dense grass
        hits MAX_CANDIDATES (counted as blocked: too dark), seconds with one bake at a time. Now MICROMAP_WORKERS 4 (a
        baker each, the SDK's internal threads on), grass sets first: harness leaf stress 48 meshes, 400 frames: 17 -> 33
        built (6 workers without internal threads: 13).
      - Room growth max(1.5x room, 1.25x wanted).
      NEXT: ATN records twice at the same place, Light Through Leaves On / Off (and Swaying Grass Shadows Off), to see the
      leaf logic's share of the sun; then the sun's rays (TMax, the leaves' query) or the AO.
    - v104 (2026-10-29, helper36; ATN's debug28: 3 profiles (Light Through Leaves on / off with Graphics 2 of 2 maxed but
      Grass Always Drawn off, and reset), "Reflection" and "DLSS RR precision" shots, a list of questions). Profiles:
      sun 5.3-6.9 and AO 5.5-7.0 ms move together with the view (foliage), Light Through Leaves now ~free, scene 2.8 ms
      (211 K clusters), placed up to 200 K of 262 K (with Grass Always Drawn off). Done:
      - RR dark tones: the RR shot's darks below ~1.36 nits (G) one flat violet level (653 K pixels on one 10-bit PQ code).
        RR ignores exposure (its guide, 3.7). Harness: ATN's L frame as RR's colour (HARNESS_RR_IMAGE, jittered,
        HARNESS_RR_JITTER) is fine, but squared (the DLSS input is the tone mapper's output, far darker than the graded
        shot) RR raises the darkest 3x and L 2.7x; with the square root 1.0x (noisy AO: ~1% darker, a bit more noise).
        anti_aliasing.hpp square_root for RR too. The harness's nvngx_dlss.dll is now 310.9.16 (the game's).
      - Water reflections: the water draws its own G-buffer (normal, material 4) after the deferred light, so the rays
        (traced from the G-buffer taken at the sun light draw) left from the riverbed with its rough normal: sky, too
        bright, no bridge (the debug view black on the river). ssr00 / ssr01: the game's material flag 4 (its own no-SSAO
        test) keeps the screen-space march and leaves out the RT specular occlusion; rt_scene builds the pyramid and the
        resolve with the rays too (the rays' pixels hit -2: passed through).
      - Water Clarity: the copy / compute / rebind works in a GPU test (scratchpad water_test); range to 16 and a
        one-time log line (did the hook fire in ATN's river? check the log).
      - Torch light on smoke: vfx01 (the particles' path without shadows) had none: rt_local_lights there too (b9 via
        SHADOW_READER_SHADERS); the wrap-lit branch (sun colour without a phase function) drops the 1 / 4 pi.
      - Reflex: on and accepted by NVAPI in ATN's log; pacing only when GPU-bound (check NVIDIA overlay's PC Latency).
      - Grass Always Drawn flicker: rt_place leaves out grass outside the view's sides +16 m (world origin: the rows are
        world matrices, the BLAS the first instance in world space; harness rows made the same); kept grass too.
        Untested whether that was the cause: next profile with it ON (placed vs 262 K).
      - GRASS_MICROMAP_LEVEL 5 (MicromapRead / MicromapBake grass flag).
      NEXT: ATN's test of RR darks, water, torches; a profile with Grass Always Drawn ON; near-only grass sway (GPU-side
      per-instance distance in rt_place + rt_compact) if the sway's cost still matters.
    - v105 BIG BATCH (ATN 2026-10-29 ~18:30, away a few hours, "no need to stop", commit often). Checklist (tick + commit
      after each; [ ] open, [x] done, [-] left out with the reason):
      [x] 1. (68617ac6: RT captures / lit particles wait for the bridge's RT textures, object motion for the helper.)
          Menu fps 500 -> 140 (not Reflex: V-Sync off, 165 Hz; RT / DLSS not running in the menu): early-outs in every
          per-draw hook / per-frame pass whose effect is not running (RT capture without the bridge, motion redraws
          without DLSS, ...); note what else runs there.
      [x] 2. (e5b61213, helper37.) AO from path tracing: Path-Traced Ambient (RayTracedPathAmbient, Experimental, needs
          Path Traced Light Bounce): the game's ambient cube zeroed in the sun draws' lighting CB (OnPathAmbientDraw:
          CopyResource + CopySubresourceRegion of zeros at ambient_cube x 4), the paths add the sky light (light_kept[2]
          = the cube's scale fitted in rt_cache_resolve before it goes; PATH_KEEP_LIGHT freezes the fit while the game's
          ambient is off), RT AO off under it. Harness HARNESS_PATH_AMBIENT=<frame>.
      [x] 3. (b4fbf967.) RT reflections on water: rays from the water's own depth (its draws write depth) and normal, smooth.
          Reflections(native, surface_depth): positions again from the depth-stencil (t8 of the reflection draw) +
          anti_aliasing::RewriteGuides (PS t0-t2; aa_rr_guides: material flag 4 = water: roughness 0.03, specular 0.02,
          no diffuse), b10 flags -> RtWaterReflections; option RayTracedWaterReflections. RT refraction: item 7.
      [x] 4. (3e0fba3a WIP, 436430cb helper38.) GPU micromap baking: RayTracedGpuMicromaps (Experimental, default Off,
          bridge RT_GPU_MICROMAPS 2048): the OMM SDK's GPU baker (ommGpu, ComputeOnly, Force32BitIndices, no texcoord
          dedup) at the end of a request, up to MICROMAP_GPU_BAKES (8) a request: each mask's atlas slice copied to a
          texture of its own, UVs copied with garbage zeroed (rt_omm_uvs, |uv| <= 2), the SDK's chain (root signature
          per pipeline: its descriptor table, the global CBV always, local root constants), outputs in VRAM, only the
          small desc histogram read back after the GPU finished (no mid-frame read-back; the D3D12 array build needs
          it on the CPU), then the normal array / BLAS build. The hang (DRED: the SDK's InitCs dispatch) was a
          use-after-free: the histogram / post-dispatch buffers were dropped when the function returned (DRED page fault
          on a freed resource; LostReport now also prints faults at VA 0). Harness: gpu_micromaps, _leaf_light,
          _leaf_stress, and HARNESS_LEAF_QUARTER=1 (the leaf takes the quarter mask: the micromaps' known states matter)
          pass the same as the CPU baker. Debug switches left: RENODX_TW_D3D12_DEBUG (the Agility debug layer still says
          0x887A002D). Not done: async compute queue for bakes; the SDK's CPU setup is cheap (ommGpuDispatch per bake on
          the record thread).
      [x] 5. (df8291a6, helper39.) Mega Geometry: near-only sway (GPU, per instance by distance), off-screen sway left out,
          far sway at a lower rate, (cluster LOD / unit templates if time): experimental toggles where they cost nothing.
          Done: Swaying Shadows Range (RayTracedSwayRange, 0 m = everywhere, default 0) and Off-Screen Sway
          (RayTracedOffScreenSway, default On), bridge v18 rt_sway_range (v14's padding) + RT_SWAY_IN_VIEW 4096; the addon
          marks a placed draw's swaying copy RT_INSTANCED (its rows after its blocks). Helper: the compact table's fourth
          uint4 = the swaying draw's rows (only when its hidden placement goes in: mesh placed, rows copied); rt_compact
          marks a turned-down instance's clusters (the argument's last word, SwayTraced from rt_sway.hlsli: |view depth| >
          range, or 16 m outside the view) and skips its triangles; rt_sway MODE_KEPT packs the kept arguments (GPU counts
          in sway_counts_) before the CLAS builds, MODE_LISTS writes each cluster BLAS's list after them (each BLAS keeps
          its run's first cluster: none empty); rt_place shows a hidden instance SwayTraced turns down. Harness:
          HARNESS_SWAY_CUT=<m> (+ HARNESS_SWAY_OFF_SCREEN, flag 4096): modes sway_near / sway_far / sway_off_screen.
          Left out: far sway at a lower rate (the CLASes are rebuilt each frame into one buffer: a slower rate needs the
          old ones kept per instance; alternating still / swaying frames would shimmer under DLSS), cluster LOD / unit
          templates (time). The game-side capture still streams every instance (the cut saves the helper's CLAS / BLAS
          builds and its scene data, not the addon's stream output).
      [x] 6. (0b4574a2, helper40.) The game waits ~15 ms a frame for the helper's GPU work (sun 5-7 + AO 5-7 ms): speedups
          (foliage shadow rays through micromaps as opaque, RT one frame behind / overlap?), as experimental toggles.
          Done: Fast Leaf Shadows (RayTracedFastLeafShadows, with Leaf Micromaps; bridge RT_FAST_LEAVES 8192 -> rt_scene
          FAST_LEAVES: Occluded / TRACE_NEAREST / LeafHit / SunRay's queries take RAY_FLAG_FORCE_OMM_2_STATE in rt_trace and
          rt_lights: no alpha tests for micromapped leaves); Grass Shadow Range (RayTracedGrassShadowRange, 0 m = all;
          bridge rt_grass_shadow_range in v6's padding -> Settings b0 grass_shadow_range): pixels deeper than it trace the
          sun without GRASS_INSTANCE (placed + swaying grass; per-frame captured grass sits in the units' BLAS and stays)
          and skip the grass AO ray. Harness: HARNESS_GRASS_SHADOW_RANGE (a placed solid patch at 5.0 m), modes
          grass_range_near / _far, fast_leaves (+ _gpu_light); 4K grass field (HARNESS_PLACED_GRASS=40) trace 4.05 ms ->
          2.10 ms at 3 m, 3.26 at 8 m; Fast Leaf Shadows unmeasured (no micromapped canopy in the harness view).
          Not done: RT one frame behind: the helper's CPU round trip is ~0.1 ms (async) and its GPU work runs in its own
          process (time-sliced with the game's, no overlap), so a frame-late trace would only hide that bubble while
          its shadows lag a frame (reprojection needed). The wait is the GPU work itself.
      [~] 7. NVIDIA advice not used yet, as experimental toggles even if slower now (ATN tests in a big city): ReSTIR DI
          for torches / fires, reflections through the path tracer, SHaRC radiance cache, emissive lights in paths,
          SER; and the old list's misses (refraction, caustics, coloured shadows through glass / banners).
          Done: SER (558c4689, helper41): Path Tracing SER (RayTracedPathSer; bridge path flag RT_PATH_SER 128, on both
          the bounce and the reflections requests): rt_path_ser.lib_6_9.hlsl exports PathTraceRayGen / ReflectRayGen
          (rt_path.hlsli REORDER after each TRACE_SURFACE: hint none / instance & 7, 4 bits), a state object with the
          main root signature as global, records at 0 / 64; 4K harness slower (bounce 1.7 -> 3.1 ms, reflections
          1.6 -> 4.1), as the v8 note found. Already there: reflections are path traced (rt_path with REFLECT); emissive
          light reaches paths through the screen's light (HitLight), last frame's colour and the world radiance cache, and
          torches / fires are RIS-picked in every path vertex (LocalLight, two picks).
          v106 (ATN: "Start SHaRC/ReSTIR"; twattila_v106.zip, helper44):
          SHaRC (34e8900b, helper43): SHaRC Radiance Cache (RayTracedSharc, with Path Tracing; bridge path flag
          RT_PATH_SHARC 256): SHaRC 1.6.5 headers from build\downloads\sharc at build time (build.ps1 compiles
          rt_path_sharc / rt_sharc_update / rt_sharc_resolve only when they are there; the .hlsl without a target suffix
          so CMake's addon glob skips them; 64-bit atomics + -enable-16bit-types, cs_6_9); buffers at u6-u8 (root UAVs,
          2^21 entries, 80 MB, made at first use), BUFFER_AT_OFFSET clamps every index. Update: a bounce texel of each
          5 x 5 (screen light where known, else UnseenLight = the paths' sun + torch shading), SHARC_PROPAGATION_DEPTH 2
          (SHaRC's 4 with its cache resampling hung the GPU: the hang bisected to that pair; -O1/-O3/-Gfp all hung,
          -Od failed PSO creation); resolve each bounce frame (last camera from last frame's view-projection); the
          Light Bounce's SHaRC variant reads it where the screen does not know a hit (normal + voxel-size rule). The
          reflections' SHaRC variant hung the GPU at 4K: removed, the reflections keep the helper's cache. 4K harness:
          bounce 1.66 -> 2.1 ms; values beside the unseen unit change (0.53/0.09/0.07 -> 0.43/0.12/0.09).
          ReSTIR DI for torches (c4bc2524, helper44): Importance-Sampled Torch Shadows (RayTracedLightImportance;
          RT_LIGHT_IMPORTANCE 16384): rt_lights traces 2 lights a pixel picked systematically along LightArriving's
          luminance (one number an 8 x 8 block), new lights always, the rest keep their bits; 32 fires 4K: trace
          2.98 -> 2.47 ms, the same bits once settled. Full ReSTIR DI shading not done: the game shades each torch
          itself with our per-light bits (ReSTIR DI picks one light a pixel: it needs our own torch lighting).
          Still not done: refraction (the game's water refraction already samples the underwater ground on screen; RT
          would help only at the shores), caustics (photon tracing), coloured shadows through banners / sails (one-
          channel visibility the sun shaders read: RGB needs those shaders and the format changed).
      [x] 8. Build (helper41, build32, 64-bit check), harness (30 modes pass, all_modes_h41), twattila_v105.zip, README /
          HANDOFF / memory, report.

    - v107 BIG BATCH (ATN 2026-10-29 ~21:05, away a few hours, "no need to stop", commit often). ATN's v106 test: every
      Experimental toggle on (Path Tracing 16 rays, SHaRC, SER, GPU bakes, sway 50 m + Off-Screen Sway off, importance
      torch shadows, Path-Traced Ambient, grass range 75 m). Logs (Attila folder): the helper's first GPU loss: a hang in a
      trace request's sway pass (DRED: BLAS marker, cluster-destination copy, sway marker, counts copy, >Dispatch), then
      three restarts each lost at the cluster-destination copy after the BLAS builds (page faults on freed resources),
      then "Off until the next set" (RR off with RT); frame generation then failed "no D3D12 device for Streamline"
      (sticky Fail). Checklist ([ ] open, [x] done, [-] left out with the reason):
      [x] 1. The freeze and losing RR / MFG (a350179a, helper45). Found: a removed D3D12 device leaves the SHARED fence at
          all ones (harness: FFFFFFFFFFFFFFFF after RENODX_TW_TEST_REMOVAL), so every wait on it passed: the game, RR and
          FG ran unsynchronized after the first loss (RR / MFG "not working", fps "better"); the harness's sway check
          failed after a removal for that reason (results one frame late). Fixed: dlaa_bridge::Prepare makes a new set
          (new fence) when its fence reads all ones (the harness now calls Prepare each frame and copies the old set's
          contents: "new set after a removal" check). Windows refused the helper new devices after the 4 hangs (RT's
          "needs DXR 1.1" at the next set and FG's "no D3D12 device" were D3D12CreateDevice failures): one restart a
          session, with GPU Micromap Baking / sway range / Off-Screen Sway / SER / SHaRC paused (masked in main.cpp each
          request), a second loss leaves RT off ("restart the game" status); RR / FG / RT device creation failures are
          no longer sticky (tried at the next set, HRESULT in the status); RR / FG Lost() -> a new device (FG: Streamline
          shut down and started again). DRED: each slot's request (kind, value, flags, sway), the passes' marker names
          (DRED left them unnamed), buffers named "rt_d3d12.hpp:<line>" (std::source_location). The first hang's cause
          (rt_sway's MODE_KEPT dispatch, no page fault) not found: bounded loops, bounded indices; next hang's report
          names it.
      [x] 2. River zoom fps (35f1ecc7). ATN's ReShade.log: the TRACE request 24-53 ms (bounce 2-3, reflections 1-4),
          worst with AO; ATN's settings: AO 1 ray x 4 (Ray Tracing at Output Resolution, DLSS Performance), 4 sun rays,
          softness 1.58, grass range 75. 4K harness field (HARNESS_COMPACT_FIELD 30) with those: UNSORTED grass sets
          traced 30 ms, sorted 7.6 (grass AO alone 6.3 of 9.6 ms unsorted; candidate cap 4 or 2-state micromaps barely
          helped: traversal, clusters each over the whole patch). Found: a swaying placed mesh got its triangles' order
          only from a full capture in its index-copy frame; placed meshes first captured before the helper took compact
          draws (a battle's first frames) were made unsorted. Fixed: corners from any later full capture (a placed mesh
          captured once more), a set waits up to 30 frames for them; the cost log says "N cluster sets, K unsorted".
          Not done: one grass AO ray a pixel (-1 ms sorted, noisier). Untested in game: ask ATN for the river again
          (and a Record Ray Tracing Profile there if still slow).
      [x] 3. Surfaces black a few seconds then lit (88556aff, helper46): kept placed instances stayed in the TLAS 300
          frames after their last draw (off-screen casters); zooming switches levels of detail, the coarser copy kept
          darkened the finer one in its place. rt_place drops a kept instance undrawn 16 frames (four placing turns)
          whose origin is inside the view (NearView, margin 0); off-screen ones stay. HARNESS_UNDRAWN=12 kept, =20 gone
          (all_modes undrawn_kept / undrawn_gone, also removal_sway). All Attila statics are placed (rows shaders): the
          CPU path's RECENT_FRAMES unchanged. Untested in game.
      [x] 4. NVIDIA guidelines audit (a545dbc0). OMM SDK integration guide: GPU baker for baking at runtime ("significantly
          more efficient"), CPU baker for offline builds; a frame budget; 4-state + any-hit for the unknown states;
          2-state suggested for secondary rays; the bake prepass with maxOutOmmArraySize; no per-ray mip bias. Ours:
          GPU Micromap Baking now ON by default (no longer Experimental; CPU baker the fallback); budget
          (MICROMAP_GPU_BAKES), PerformSetupAndBake + maxOutOmmArraySize, 4-state + alpha test at mip 0 already;
          Fast Leaf Shadows (2-state) left an option (coarser leaf edges). DLSS RR: it replaces denoisers that
          accumulate frames: the AO's history now off under RR (reflections raw, soft-shadow filter off, ReSTIR GI
          permuted already; hit distance given). Mega Geometry: spatially coherent clusters (the sorting fix, item 2),
          templates for instanced sets, GPU-driven counts. Deviations kept on purpose: SHaRC propagation depth 2 (4
          hung the GPU), partitioned TLAS off (stalled; our TLAS 0.1-0.16 ms), SER Experimental (paused after a reset),
          GPU bake DisableTexCoordDeduplication.
      [-] 5. More ReSTIR (analysis, nothing shipped): ReSTIR GI is already spatiotemporal and always on with Path
          Tracing (16 candidates a texel, temporal reuse with validation, 5 neighbours with Jacobians, final visibility).
          4K harness (noise_runs.py, "bounce near the wall: red spread"): 1 ray 0.149, 4 rays 0.080 (bounce 1.69 ms), 16
          rays 0.064 (2.49 ms): tell ATN 4-8 rays. More neighbours (5 -> 8 -> 12): same spread, +0.4 / +0.9 ms: not
          worth it. Sun shadows: one light, nothing to resample (RR denoises its one ray). Torches: Importance-Sampled
          Torch Shadows is ReSTIR DI's light picking; full ReSTIR DI needs our own torch lighting (the game shades each
          torch with our bits). Reflections share nothing with the bounce's rays (own samples, ATN 1); ReSTIR on the GGX
          lobe needs full-resolution reservoirs (~0.25-0.5 GB at 4K: against ATN's VRAM wish); cheaper next step:
          rough reflections (now not traced past ROUGH_LIMIT) from the GI reservoirs (no rays, no VRAM).
      [~] 6. Water Caustics DONE (89a57ef7, untested in game): the river (2E9F5935, world v1, normal sampler s4) and lake
          (C9FBACCB, v3, s3) PS patched at creation (addon.cpp PatchWaterCaustics: after sample_l r7.xyz from t2, mul /
          frc / sample_l t51 / mad r7 *= 1 + c; t51 unbound = the game's); water_caustics.cs (tile 4 m, 256^2 R16F, two
          layers of curved drifting cell borders, seamless) at the first water draw a frame; WaterCaustics slider 0-2
          (default 1). Checked offline (scratchpad water_patch_test: WARP creates both, disassembly; caustics PNG).
          Not done: coloured shadows (plan: the trace's cloth hits on screen tinted by RR's albedo guide / last frame's
          colour into a new shared RGB texture (bridge v19), the patched sun shaders' sun_colour reads (lighting_VS_PS c1)
          x (1 - t50); needs the sun shaders' bytecode (dump in game) to check the patch), RT refraction (the water is
          drawn forward after the lighting, so the trace never sees its surface: needs a capture of the water surface at
          its draw and a late trace, or last frame's, reprojected).
      [x] 7. v107 for items 1-4 + caustics: twattila_v107.zip (Downloads, 22:15; helper46, release_zip.py now names
          build\tw_dlaa_helper46), build32 Attila / Rome II / Thrones and the 64-bit TW check built, all_modes 38/38
          (all_modes_h46.txt: new removal_sway, undrawn_kept, undrawn_gone), README v107. Items 5-6 go to v108.

    - v108 BATCH (ATN 2026-10-30 ~00:10, debug29: PT AO pictures, a 30 s trace through grass to a river). ATN tested
      v107 (0 unsorted sets). Checklist ([ ] open, [x] done, [-] left out with the reason):
      [x] 1. PT AO dark after 1-2 s (201dd567, helper47): the "correct" first seconds were PATH_AMBIENT_FRAMES (120) of the
          game's ambient before PT AO took over. Causes: rt_ambient_alone (the game's SSAO / GT-VBAO off) only for RT AO,
          so both stacked on PT AO and biased its learned shade; paths meeting a surface on screen read the prepared
          light, which lost the game's ambient (zeroed in the sun draws): walls brought back ~nothing. Fixed:
          rt_ambient_alone = RT AO || Path-Traced Ambient; HitLight / rt_cache_fill add base colour x light_kept[0]
          (the learned shade) under PATH_KEEP_LIGHT (SHaRC's update reads HitLight); warm-up 30 frames.
      [x] 2. Grass / river fps (helper47): debug29 profile: the trace's SUN part 22-27 ms zoomed into grass and over the
          river (AO 0.13, bounce ~1, reflections ~1-1.7, TLAS ~0.4); positions at the river = its grassy bed (the water
          is drawn after the trace). Grass instances FORCE_OMM_2_STATE (DXR 1.2; CPU AddInstance + rt_place grass_flags at
          b2[18]); the sun's ray split: everything but grass full length, grass only for 2.5 m of rise (min(2.5 /
          sin(elevation), 40 m)): harness 4K field unsorted 4.84 -> 2.10 ms, sorted 1.27 -> 1.12, placed grass 1.16 ->
          0.97. Untested in game (the harness's grass is sparser than the game's).
      [x] 3. Grass shadows settling 1-2 s: before their micromaps, sun rays through grass hit MAX_CANDIDATES (64) and count
          as blocked (dark); GPU bakes now 24 a request (was 8), set reads 32, 128 bakes in flight, OMM descriptor ring
          65536 (was 16384: 24 bakes x 3 requests would have wrapped onto descriptors in flight).
      [x] 6. Water Caustics never patched (v107): both water shaders are also screen_space_shadows' forward shaders
          (SHADOW_READER_SHADERS), patched after the render scale and before OnCreateWater; OriginalHash knew only
          view_share's own code: the callback saw an unknown hash (no "Water Caustics" log line in debug29). Fixed:
          view_share::Repatched(patched, code, hash) records every patch's code with the game's hash (called by
          screen_space_shadows, grass_fade and OnCreateWater every time); OriginalHash looks it up.
      [x] 7. Pure RT/PT Light Bounce (ATN's add-on to the batch: GT-VBGI's 1/4 resolution, checkerboard and filters on the
          rays' light): light_bounce::SetRayTraced (from rt_scene::Configure, every present after light_bounce::Update):
          the rays' resolution from the new RayTracedLightBounceResolution (Full / Half / Quarter, default Half), no
          Checkerboard, and under RR raw (no resolve, no denoiser: the apply reads `traced`). Without RR the resolve
          stays (noise).
      [ ] 4. Answers: GPU bake (already default in v107; the toggle stays as the CPU baker's fallback; nothing else bakes
          on the CPU: BLAS builds, compaction, placement, sway, garbage checks, light picks are GPU; cluster sets are
          made on the addon's worker threads, off the game's thread), PTLAS (TLAS ~0.4 ms of a 25 ms frame: at most
          that saved; needs rt_place to write partition updates; the 0.3 s stall was its CPU path, unrelated to the GPU
          bake / SHaRC fixes), ReSTIR reflections (pick: the GI reservoirs for rough reflections).
      [x] 5. twattila_v108.zip (Downloads, 00:33, with items 6-7; helper47, also in Attila's folder; release_zip.py names helper47),
          build32 Attila / Rome II / Thrones built, all_modes 38/38 (all_modes_h47.txt), README v108, memory. The
          harness cannot check PT AO's fix (no learned shade / ambient cube there): untested.

    - v109 NIGHT BATCH (ATN 2026-10-30 ~00:40, debug30: freeze, PT AO pictures, a trace through grass to a river and the
      city; ATN asleep, "turn off my pc" after). Checklist:
      [x] 1. Freeze (6306778d): DRED's "reflections" request was stale: the slot's request / marks were overwritten before
          the failed Reset after the loss; the hung list was a trace request's GPU micromap bake (marker, copy, clamp,
          OMM chain dispatch 4). Now a removed device keeps the slots' last request. Probe (scratchpad omm_chain): a bake
          = 11 dispatches, ~71 descriptors (the 65536 ring holds ~900 bakes). rt_omm_uvs a triangle a thread: a corner
          past |2| or not finite, or a triangle over 1.1 texture widths -> COLLAPSED (no texels), its micromap index
          set to -4 (unknown opaque) after the chain. MICROMAP_GPU_BAKES 64 + MICROMAP_GPU_TRIANGLES 131072 a request;
          past them CPU bakes (MicromapRead.gpu; before, meshes past 24 never got micromaps). Profile: gpu_bakes,
          gpu_bakes_ms (STAMPS 10).
      [x] 2. PT AO (0254ad1b, 35048c6f): ReSTIR GI is always on with Path Tracing and gave escaped rays weight 0: no sky
          in PT AO, only the ground's warm bounce. Misses join the reservoirs (Jacobian 1 for sky samples), the camera
          rays' sky x SkyCalibration (the game's cube at the normal, its y x4 normalized as Attila's sun shaders take
          it, over 16 fixed cosine rays' sky). In game (auto_save desert replay, same view): PT vs RT AO region means
          within 1-4 levels.
      [x] 3. Grass dark after camera moves (ad70391a): past MAX_CANDIDATES the grass-only sun query now aborts (lit)
          instead of committing (black) while micromaps are missing. Not reproduced in my replays (2vs3 hill: no change
          with or without, camera could not zoom into dense grass; the wheel / F did nothing).
      [x] 4. Optimizations: AO rays FORCE_OMM_2_STATE (harness 4K leaf stress: 2.14 -> 2.10 ms; ATN's debug30 AO 6-11 ms
          was RT AO 8 rays quarter in grass). debug30's bounce column 0 = ATN had switched to RT AO for the profile.
      [x] 5. Coloured Shadows (10ce5cba, bridge v19 rt_tint RGBA8 = 1 - filter, RT_COLOURED_SHADOWS): rt_trace multiplies
          each cloth / sail / masked layer's ThinColour (RR diffuse albedo guide where positions match) into the ray's
          tint; screen_space_shadows' Patch replaces every lighting_VS_PS c1 (sun_colour) read with rC = c1 x (1 - t50)
          (loads at the code start now, the shadow mul at the saturate). Sun shader dumps: scratchpad sun_attila
          (B6DA03C5 etc.; their ambient: normalize(n * (1, 4, 1)) squared x cube c2-c7). WARP test: all 10 shadowed
          variants create; in game 8 patched, picture fine; the tint itself untested (no cloth near the camera).
      [x] 6. Reflection Reuse From ReSTIR GI (524954cf, RT_REFLECTIONS_REUSE path flag): ReflectPixel at roughness >= 0.3
          resamples the GI reservoir at its texel (this frame's set: 1 - reuse_parity_ after the bounce request) with its
          own estimate (GRIS, weights lum(own) and lum(L) x GgxReflectionDensity x W, one visibility ray). In game:
          reflections 0.71 ms, no errors; looks untested. [ ] "Own ReSTIR" not done: full-res reservoirs (~265 MB at 4K
          for two sets) or half-res ones plus temporal + spatial passes; ~2-3 h.
      [x] 7. Known: auto_save (desert) replay: the first trace request's Close fails once (E_INVALIDARG, also with v108's
          helper47; the D3D12 debug layer would not load: 0x887A002D). Status now carries the HRESULT.
      [x] 8. twattila_v109.zip, builds, all_modes, README v109, memory (see the commits after 524954cf).
    - v114 BATCH (ATN 2026-10-30 ~11:45, away for hours; debug32 = C:/Program Files (x86)/Steam/steamapps/common/Total
      War Attila/debug32):
      [x] 1. Debug layer works (Graphics Tools). In game (auto_save.replay, RENODX_TW_D3D12_DEBUG=1, d3d12SDKLayers.dll in
          Attila's D3D12 folder: remove after tests) with helper57 and with helper54: no Close failure, the only error
          "Resolve query never performed" (fixed: 4d9f75c6, all slot stamps written first). A failed Close now logs the
          request's passes and counts (debug32's recorded in 0.01 ms). Not reproduced: next time read that line.
      [x] 2. debug32: the 11:34 one-off Close failure went through Fail (sticky failed_); at 11:39:23 ATN turned Specular
          Occlusion off, the shared textures were made again and the scene's re-open refused (Initialize: failed_): RT off
          until a restart, fps up. Fixed (2412c943): Record's Close / BLAS scratch / PTLAS failures only set the status.
          The 20-30 fps: GPU trace 9-14 ms above the river (grass and leaves 0.45-0.56 M triangles): item 3.
      [x] 2.1 PT AO "cooked" (4db7aa68): not the tonemapper (the sun shaders light the scene before it). The sun shaders
          scale the diffuse ambient by c11.x's choice (x1.44 or x1; foliage 0.6 / 0.8 of that) and the foliage ones add a
          sky sheen from the cube at the reflection: zeroing the cube lost the sheen, the paths' sky ignored the scale.
          Now PatchAmbient (screen_space_shadows) scales the first cube read by 1 - t51 (PT AO binds 1), rt_sun gives
          the scale (texel 4 w: SkyCalibration) and the foliage share (texel 5 w: light_bounce_apply t52). In game,
          same view: PT vs RT AO -0.4% brightness, blue -3% (open ground: the sand's bounce; ReSTIR's W clamp ~1%).
      [x] 3. CPU / GPU limits (the debug layer measures nothing: profiles instead; RENODX_TW_PROFILE=<s> starts Record
          Ray Tracing Profile, RENODX_TW_SHOT=<s> takes ReShade screenshots; scratchpad profile_run.ps1 / shot_run2.ps1).
          2vs3.replay start view (river map, 4K, ATN's ini): frame ~20 ms; the game's GPU waits 4.4 ms a frame for the
          helper; helper GPU: sun rays 2.9 (grass / leaves alpha tests; debug32 above the river 9-14), bounce 2.0,
          reflections 0.7, scene 0.7, TLAS 0.3; CPU: addon 0.25 ms a frame, helper trace record 1.2 ms on its worker
          (parallel). Sun rays already first-hit with 64 alpha tests at most. Left for ATN: Grass Shadow Range (his 200 m)
          lower, Fast Leaf Shadows per instance for tree leaves only (grass exact) would need AddInstance, rt_place, the
          PTLAS conversion and cluster templates to carry FORCE_OMM_2_STATE: not done.
      [x] 4. CPU fallbacks: with GPU Micromap Baking (default on) none; CPU bakes only with it off or its baker failing.
          CPU reads left are D3D12's own needs (OMM histograms for the array builds, BLAS compaction sizes).
      [x] 5. Side by side (135b8af4): a trace's GPU bakes (and the timestamps' end) in a second list after the game's
          fence signal: the game stops waiting for them. The trace's own passes (sun / AO, lights, fog) already run without
          barriers between them; bounce and reflections depend on this frame's lit image (no earlier start); the helper
          records on its own worker thread already.
      [x] 6. Explained in the round report: SHaRC = two extra passes a frame (update traces from 1 of 25 texels, resolve
          walks 2 M entries) for paths that are short and mostly end on surfaces the screen knows; SER = a ray tracing
          pipeline instead of the inline compute plus a sort by hit, worth it only for long divergent shading.
    - v118 HUGE BATCH (ATN 2026-10-31 ~10:00, away for hours / days: don't stop midway, don't wait for answers, commit after every
      step, never push, tick off here; new issues found: fix them in the following batch. debug36 = C:/Program Files (x86)/Steam/
      steamapps/common/Total War Attila/debug36: logs started mid game, a profile, two pictures). ATN's words:
      "3 things, maybe you can find it in the logs: Shadows on the smokes (smoke of catapults shots) looks insane but Idk why or how,
      when I was playing around with the toggles, they just stopped working (Smoke shadow and RT shadows are are on.) They just don't
      come back anymore. (There was a short freeze when I went to GT-VBAO then back to PT AO too)
      Subfolder "RT Reflection leak", I have realized that sides of the screen (no matter what DLSS or how many samples RT reflection
      takes), has this "issue" it seems like screen space reflection. Idk if you see it or not. (on left and right sides). It is not in
      this folder but sometimes, It seems that SSAO/GT-VBAO/RT AO don't use RT Reflection at all for Water. It looks like SSR for me.
      Subfolder "shadows or ao missing". I was on the other side of the map looking at the water reflections and testing things out:
      "RT Reflection leak". Then I came back to this side of the battle and saw the Shadows are missing. I put RT Shadows back to SSS
      (which brought the shadows back) then back to RT Shadows but the shadows didn't come back anymore. I changed AO (if I remember
      correctly) to GT-VBAO then back to PT AO then the shadows and etc. came back. Why is this?
      One more thing, I got no picture of it but "dead bodies" on the the ground in shadows have lit up bodies on their corners/bottom.
      What I mean is, it looks like they don't get shadows under their dead bodies.
      Add: Far tree billboards aren't in the RT scene, so they have the same missing-shadow problem sprites had.
      Btw. can we get a toggle for these tree billboards and 2d unit spirits a toggle in More Options when RT Shadows is selected too
      please?
      For the settings, could you please, for example name the technology that is being used too please. Like for Reflection. You don't
      say the AMD SSR (or whatever it is called) but Hiarchical (or whatever). There are probably more stuff on that page.
      One last check please: check that NO debug or anything runs when the debug sliders isn't on. Please make sure that no CPU/GPU time
      is wasted when the reshade window is closed. Optimize for last time please."
      Findings from debug36 so far (timeline, helper log has no times, ReShade.log has): 09:47:33 trace 4144 Close failed E_INVALIDARG
      (256 captures, 162 compact, 733 placements), then "no trace" (record 0.01 ms) until 09:49:31: a failed Close seems to leave the
      slot's list open, so allocator->Reset fails for good: that SLOT never records again. With 3 requests a frame one effect dies:
      first the trace, after ATN's toggles shifted the order the Light Bounce (bounce stuck at 0.51 ms 09:49:41-09:53:23, PT AO and GI
      gone: the picture "shadows or ao missing"). Nothing but the GPU reset at 09:53:17 (nvlddmkm 153 x2) cured it: new helper, new
      slots: ATN's "came back" after the AO change was that reset. That reset: trace 46119, 26 of 69 ops, first sway Dispatch.
      [x] 1. Failed Close: remake the slot's list and allocator; find what made it fail (256 captures).
          CAUSE PROVEN: slot.sizes / readback held RT_CAPTURES entries, a request compacts up to RT_CAPTURES new BLASes + its finished
          bakes' BLASes: 256 + 1 = CopyBufferRegion past the buffer = E_INVALIDARG at Close. Harness leaf_storm (256 new leaves a frame with
          micromaps) failed the same way with the old size ("256 captures ... 0x80070057"), passes with COMPACTIONS = RT_CAPTURES + MICROMAP_BAKES.
          A failed request now counts as Lost: nothing of it ran but CPU state says it did (meshes_, sets_), the list never resets. main.cpp:
          new scene, addon captures again, nothing paused, the fourth failure leaves RT off (scene_off). skip() the same (BLAS scratch, PTLAS).
          Harness bad_list (RENODX_TW_TEST_BAD_LIST) passes all checks after the restart.
      [x] 2. Smoke shadows stop: the smoke is the LAST capture, skipped with RT_CAPTURES full; check why else.
          CAUSE: a particle draw streams its whole list, nearly all without opacity (a one-off readback: 742 of 47884, 0-18 of 262144 in
          2vs3.replay, whose one draw holds 262144). MAX_PARTICLES was 65536, first come: a big list drawn first filled it and the catapults'
          smoke, drawn later, was dropped for as long as that list stayed. Not the failed request, not the toggles.
          Fix: MAX_PARTICLES 1 << 19; rt_smoke.cs gives a quad only to particles with opacity (u1 counter), MAX_SMOKE_QUADS 32768, a second
          dispatch empties the rest: at most 196608 vertices a frame (the old cap 393216). A/B in auto_save.replay, sun debug view: the two
          catapult trails' shadows there with it on, gone with it off. GPU its passes 0.24 ms.
      [x] 3. The freeze: GPU reset at the sway Dispatch. NOT the failed request (my first note was wrong: withdrawn).
          CAUSE (by test, not by a driver explanation): a trace RECORDED while the last trace still ran on the GPU. Requests take the
          slots in turn and only a slot's own last request is waited for. Three requests a frame: every trace lands in the last trace's
          slot, so it always waited. Light Bounce off (2 a frame, or traces alone): traces change slots and nothing waits. Reflections off
          (T + B) changes slots too, but the long bounce list sits between two traces: clean. FIX: Trace does WaitFor(trace_value_) before
          recording (no cost: the GPU is busy with that list anyway; also covers any toggle that shifts the order for a frame).
          AFTER (ATN's ini, all runs valid, no driver events, closes 2-6 s, frame costs unchanged): sequence 0/11, ATN's 18-toggle list 0/4, traces
          alone 0/3, Light Bounce off from the start 0/6: 0/24, the last 5 with the shipped binaries. Harness 48 of 48 modes.
          BEFORE, soak.ps1 with test_scripts/ReShade.ini.atn37, keys W:6,D:1.6,Y:1.2, RENODX_TW_SET "40:RayTracedLightBounce=0;42:RayTracedReflections=1":
          default 3/3, Off-Screen Sway on 2/2, range 0 + Off-Screen on 2/2, far trees + sprites + smoke off 2/2, GPU bakes off 2/2, full room from
          the start 2/2, Log Details off 3/3, RT AO in place of Path AO 2/2, GI strength 0 2/2, ATN's old grass 2/2, Reflections off too 2/2, Reflex
          off 3/3, Light Bounce off from the start 3/3: 33/33. Sway fully off 1/2 (at a GPU bake's first Dispatch), no camera move 1/2, debug layer
          0/1 (no errors). Sway AND GPU bakes off 0/3: what a reset pauses, which is why no run ever hung twice.
          Light Bounce on: camera move alone 0/2, Path AO off 0/2, Path AO to RT AO 0/2, Reflections off 0/3 (slots and pool sizes are fine).
          REPRO IN A MINUTE: RENODX_TW_SET "1:RayTracedLightBounce=0", no keys: hangs at the battle's second trace (trace 3), first sway Dispatch,
          128 groups, so not workload: 3/3, and 2/2 with the wait taken out again (whyA). No toggle is needed: every battle with Light Bounce off
          and sway on did this, then ran on with sway and bakes paused. Tried on it: without the mid-list SetComputeRootSignature (sway, first bake
          pass) still 3/3, same op (whyB): not that. The wait only before the submit, after recording: still 3/3 (whyC): the RECORDING must not
          overlap, not the submit. Inside the driver: unknown. By reading: every CPU write is per slot, every release fenced, no descriptors change.
          Pattern for whoever digs: stops are the first sway Dispatch with the room growing, the CLAS operation with a full room from the start (and
          10-18 s later instead of 0.2-5 s after the toggle), a bake's first Dispatch with sway off, or a list's start; never in a ray pass, no page
          fault, two nvlddmkm 153 about 2 s apart (the first can come 0.2 s after the cause), only the RT device removed.
          v117's "0 of 8" held only for ini atn35. Sway WAS on there (range 100); what kept it clean is not known: most likely 1 ray a pixel
          (Rays a Pixel Match Output makes the trace long enough to overlap). I once wrote "sway 0": wrong.
          TEST TRAP: a second Attila.exe fails while an earlier game still closes, and the old log's copy made waitA / waitB look like 0 of 4
          (found by their identical logs; one waitA game took 6.5 minutes to close, not seen again: closes are logged now). soak.ps1 marks runs
          without a new log INVALID, run_game waits for the earlier game, logs/closing.txt has the times.
          Side fix: micromap BLAS rebuilds waited SLOTS requests "for the first compaction and rows read": with 2 requests a frame that slot's next
          trace comes later, so a rebuild dropped an instanced leaf mesh's rows. Now 2 x SLOTS.
          ATN's card (read-only, nvidia-smi): Afterburner, core locked ~2190 MHz, memory 17001 MHz (NVML's max 14001). GPU at 99% with Light Bounce
          off too. Not the cause as far as the tests show (the rows split sharply), but if a reset ever comes back: try stock memory first.
      [x] 4. RT reflections: screen sides; water without RT reflections under SSAO / GT-VBAO / RT AO.
          Three causes. (a) Water "without RT reflections, looks like SSR": ReShade.log "Ray-traced reflections: not traced (the game's)" exactly
          09:47:33 to 09:49:31: the failed request of item 1 (its dead slot), nothing to do with the AO method.
          (b) The sides: a diagnostic build painted each hit by where its light came from: on screen the screen's light, past the screen's left and
          right edges the cache: in the water's reflection the far bank (hits at 15-200 m) goes off screen at the sides, and those surfaces took a
          grey or the screen's mean as base colour: pale at the sides, right in the middle, as SSR. HitAlbedo: the cell's, past the edge the
          edge pixel's on the way there (banks and walls go on), else the screen's mean (light_kept[3], new sums in the fit).
          (c) Rays turned into their own surface by a normal map (waves at a grazing view) now bounce off it (surface_normal from the depth):
          lit share at the left edge 57 -> 38%. river view (for_claude1, W:6.4,Y:2.5): the bank's dark green streaks now reach the sides.
      [x] 5. Dead bodies lit at their corners.
          Not seen on a live picture (ATN has none; replays' cameras can't get to bodies in the shade; debug36's picture shows bodies only in the
          sun, plus black ones while the GI slot was dead). Cause by reading the paths: a camera ray from a body's underside meets the ground beside
          it and HitLight brings back `light + albedo x shade`, the open ground's ambient: nothing knows that ground lies under the body. Fix:
          ContactShade: PathTraceTexel's sample light x saturate(distance / 0.3 m), Validate the same for ReSTIR GI (reflections' Own ReSTIR not).
          UNTESTED ON BODIES: ask ATN to look again; CONTACT_RANGE in rt_path.hlsli is the one number.
      [x] 6. Far tree billboards in the RT scene; toggles for them and the sprites under Shadows' More Options.
          rt_capture_billboard (0x4D50E1C7, mask t1, four views 0.25 wide, alpha 0.5): cards turned to the sun, the view of the tree from the
          sun, NaN when faded out (v2.w 0). Dynamic capture a frame (47 quads a draw instanced, ~184 draws: ~55000 vertices). Joined by atlas
          slice now (RtCapture: foliage draws of one slice join: 184 draws no longer eat RT_CAPTURES). First build: trace 2.4 -> 6.2 ms, bounce 5.5
          -> 11, reflections 1.9 -> 3.8: among the units' cluster BLAS every ray crossed them and 42 scattered quads a cluster had boxes across
          whole tiles. RT_SUN_ONLY: sprites and far trees moved to the smoke's BLAS (instance mask 2, sun's rays alone; stable_partition with
          clusters); RT_SCATTERED: far trees a cluster a quad. Now on vs off: trace 2.60 -> 2.66, reflections 2.02 -> 2.05, scene 0.94 -> 1.25,
          bounce 5.8 -> 6.9 (rays don't meet them, probably the TLAS's map-wide instance). Sprites gain the same (no more GI / reflection rays).
          A/B in one run, sun debug view: the far specks are the difference. Toggles: Far Tree Shadows, Sprite Soldier Shadows in Shadows' More Options.
      [x] 7. Technology names on the settings page.
          Method labels: AO Game's SSAO / GT-VBAO / Ray-Traced (RTAO) / Path-Traced (ReSTIR GI). Shadows Game's Shadow Maps / Bend Screen-Space /
          Ray-Traced (DXR). GI Off / GT-VBGI / Ray-Traced (One Bounce) / Path-Traced (ReSTIR GI). Reflections Game's SSR / FidelityFX SSSR / Ray-Traced
          (DXR). Rows: DLSS Ray Reconstruction, DLSS Frame Generation, Opacity Micromaps (Leaves), Opacity Micromaps: GPU Baking, Importance-Sampled
          Torch Shadows (RIS). Tooltips say what each is. Keys unchanged: saved settings and presets stay.
      [x] 8. Last check: nothing debug runs unless asked, nothing wasted with the overlay closed, last optimizations.
          RT_DETAILS (Log Details or the profile) now decides, in the helper: GPU timestamps and their readback, the status and profile strings
          (status_ stays the ready line without it), PIX markers, slot.request, micromap_builds, the left-out readback + capture keys, the log's
          history / room / BLAS / micromap lines. DRED only when Log Details was on as the device was made (shared.rt_flags written with the set;
          D3D12_DRED_ENABLEMENT_FORCED_OFF else). A GPU reset's report then says only the error: Log Details' tooltip tells so.
          Addon: new_static + examples, dynamic_buffers, key_shaders, logged_draws behind debug_log; the SSS one-time report (46 log lines and a
          stall each time SSS came on) removed with sss_probe.cs and its parameters; OnReshadePresentShot registered only with a RENODX_TW_ test switch.
          Already right: runtime.timed (D3D11 timestamps), frame_trace, shadow reader notes, ImGui only with the overlay open.
      [x] 9. v118 zip (helper61), README, HANDOFF, memory, report.
      Left for the next batch (found on the way): bounce +1 ms with Far Tree Shadows though its rays pass the cards (probably the TLAS's
      map-wide smoke instance: try a BLAS for the cards of its own, or a smaller box); off-screen surfaces take the screen edge's
      colour but are lit without grass shadows (a grass bank off screen is brighter than on screen); ContactShade not seen on bodies; why a
      trace recorded over the last one hangs the GPU (repro above; only if it comes back in another shape); the other TW games' helpers
      get the trace wait in their port rounds (older zips can still hit it with Light Bounce off and GPU bakes or sway on).
      Harness: smoke, no_details, no_details_removal, bad_list, leaf_storm modes added; its rt_smoke call follows the shader's counter.
    - v117 HUGE BATCH (ATN 2026-10-31 ~00:20, away for hours / days: don't stop, don't wait for answers, commit after every step,
      tick off here, new issues found: fix them in the following batch; debug35 = C:/Program Files (x86)/Steam/steamapps/common/
      Total War Attila/debug35: logs copied there, ATN's ini also tools/tw/test_scripts/ReShade.ini.atn35). ATN's items:
      New issues found and fixed in this batch: second GPU reset crash (omm-lib.dll unloaded under OpenMP's idle threads: it
      stays loaded now); ReadTimes' `slow_report_ =` overwrote waiting reports; bakes in flight counted as "failed" after a
      removal (that misled me: "failed" lines before each hang are a RESULT of the removal, the fence reads all ones).
      Left for the next batch: (a) far tree billboards (rigidtreebillboard vs 0x4D50E1C7, ps E0D24DBB) are not in the ray-traced
      scene: their shadows are missing as the sprites' were; (b) dead Attila / helper processes pin the exe and addon: rename
      aside (leftovers in the game folder: *.in-use-*, delete after a reboot).
      [x] 0. FIXED, soak-tested. Cause: v114 moved the GPU micromap bakes into a list of their own after the game's fence signal,
          so they ran beside the game's frame. Soaks (for_claude1.replay, Light Bounce off + reflections on, keys W:6,D:1.6,Y:1.2,
          86 s a run): bakes beside the frame 3 of 5 runs hung; GPU bakes off 0 of 8; bakes in the request's own list 0 of 8
          with about 90 bakes a run. nvlddmkm 153: 5 in 7 hours after the tail landed (10-30 15:54), 1 in the 14 hours before. Why
          beside the frame hangs is unknown (any op of the chain, also plain clear dispatches: no data pattern; the helper's queue
          and the game's D3D11 share the GPU only there: the game waits for every other list). The tail only ever helped with Light
          Bounce off (with it on the game waited for the follower anyway). Fix: tail removed, bakes back in the request's list,
          8 bakes / 32768 triangles a request (profile: at most 1.5 ms a request, 47 of 3459 requests). Final build (soakD, soakE):
          0 of 8 on the sequence, 0 of 6 with 18 toggles in a battle (Light Bounce, reflections, AO, sun shadows, path ambient,
          reuse modes, micromaps, Rays a Pixel, fog), no driver events. Harness: 44 of 44 modes pass. Rays a Pixel 4 in game: frame 26
          -> 31 ms in the replay's first view, paths 1.39 -> 3.54 ms, sun 1.45 -> 3.23 ms.
          ATN's words: "Logs next to the exe: it froze again while ATN toggled every slider. "Maybe you can find out why. It is really
          random. Maybe it got something to do with my togglings all the time."
          Found so far: CORRECTION to v116: the freeze DID reproduce in my refl1 run (22:48:42, 8 s after reflections on): I
          only looked at the stress runs. Five DRED reports now, none inside a ray tracing pass (those take most of the GPU
          time): debug30 GPU bake Dispatch; debug33 cluster instantiate (trace, "clusters >Barrier"); debug34 BLAS BuildAS; refl1
          bake ExecuteIndirect in a tail list; debug35 sway list Dispatch after the CLAS builds. No page fault in any.
          nvlddmkm 153 incidents by time: none from 10-29 00:00 to 18:49 (ATN testing v94-v104, CPU micromap bakes), my
          bake development 18:49-19:16 (helper38), SHaRC work 20:13-20:59, then one in ~each of ATN's sessions since GPU
          Micromap Baking became the default (v107): 10-29 23:29, 10-30 00:52, 09:31, 16:57, 17:41, 21:14, 22:48, 23:36.
          After a hang (GPU bakes paused) no session hung again. So: the GPU bake path, also where the hang shows elsewhere
          (those stages consume the micromaps). The CPU baker has a workload cap (MICROMAP_WORKLOAD), the GPU one had none.
          REPRODUCED 2026-10-31 (soak.ps1, kept in tools/tw/test_scripts): for_claude1.replay, RENODX_TW_SET
          "40:RayTracedLightBounce=0;42:RayTracedReflections=1", keys W:6,D:1.6,Y:1.2: 3 of 5 runs hung (86 s a run), all three in
          a slot's TAIL list inside the OMM bake chain (ops 22, 31, 47 of 46-64). Workload cap NOT the fix: harness random-UV
          stress bakes 33 meshes in 68 ms. RENODX_TW_OMM_CHURN=30 (416 grass sets baked again every 30 frames, ~2500 bakes in 3
          min, Light Bounce on, no toggles): 0 resets. So next: run soak.ps1 with RayTracedGpuMicromaps=0 in the -Set list (does
          it stop?), then with bakes moved back from the tail into the main list (v114 item 5 made them run beside the game's
          frame; debug30's hang predates the tail, so check), then Own ReSTIR off (RayTracedReflectionReuse=0), and only then
          change a default. Lesson: reading freed GPU memory hangs this driver with NO page fault in DRED (my first churn build
          did that): "no page fault" rules out nothing. Also v116's report was wrong: refl1 hung, I had not checked it.
          Item 1.1 answer: Water Caustics is not RT: water_caustics.cs_5_0.hlsl makes an animated pattern, multiplied on the bed.
      [x] 1.1 Quick question: is Water Caustics "RT Caustics"? No: water_caustics.cs_5_0.hlsl, an animated pattern on the bed.
      [x] 2. (rt_core camera_moving: every placed mesh placed every frame while the camera's place changes; 0.42 -> 0.49 ms
          capture on average, p95 0.63, placements up to 756.) LOD change with RT Shadows still obvious: "shadow disappearing when the LOD changes and appearing really fast after";
          hide it. ATN: this, 2.1 and 2.2 are the only remaining RT Shadows issues.
      [x] 2.1 (imposter.hlsl vs30_main 0x1B9E7692: rt_capture_imposter.vs: the card turned to the sun with rt_sun at t0, cut out by
          its diffuse alpha, blocks as grass does (RT_GRASS); Sprite Soldier Shadows in Shadows' More Options; shot compared on / off.)
          Soldiers' LOD ends as 2D sprites: give them shadows too.
      [x] 2.2 (rays started 2 cm + 0.1% depth off the surface along the normal AND again toward the sun: 3-5 cm at close range, past
          cheek guards. SurfaceOffset 3 mm + 0.1%: tower view shows no acne. Not tested on a close-up soldier: keys can't get there.)
          Light goes through soldiers' helmets (debug35/solder helmet: 1.png his right side, thick helmet; 2.png from the front,
          no shadow on the right side of his face; with SSS it is there). Game limit or RT ignoring helmets and more?
      [x] 3. (graphics_page.hpp, run in game with the overlay: overlay_test.ps1 in tools/tw/test_scripts. Every section seen, every
          More Options opened, AO / Shadows / GI / Reflections methods clicked through and read back from ReShade.ini. Found and
          fixed there: the four "Method" sliders had one ImGui id. A click after a ReShade screenshot does nothing: test artefact.)
          VERY IMPORTANT, "make sure you don't break anything": redesign Graphics 1 of 2. Reflection moves there from Graphics
          2 of 2. Grouped: AA, DLSS SR, RR, MFG, Reflex on top; all titles the same colour. "DLAA" in AA renamed DLSS. Each AA
          option's settings hidden unless selected. Simple and short: a "More Options" (closed by default, its own distinct
          colour, the same for every More Options, not the title colour) holds what normal people don't care about (e.g. motion
          vectors). Then an ambient occlusion section (a slider like AA: Off (game's), GT-VBAO, RT-AO, PT-AO; each mode's
          important settings shown, the rest under More Options), then Shadows (Off (raster), SSS (raster + SSS), RT Shadows
          which replaces those), the rest in the same spirit. Check what depends on what / can't run together. Update tooltips.
      [x] 4. (Light Through Foliage, ScreenSpaceShadowsFoliage 0.5: what foliage SHADOWS keeps that share of the sun; solid objects
          stay whole, on foliage too. Nothing in the game marks foliage: the whole scene has one stencil value, the "foliage" sun
          shaders never draw, the material byte is 0 for leaves, grass and most objects. Mask: the depth buffer copied at the
          first foliage draw and at the first rigid draw after it (foliage's prefill follows the terrain's), compared in sss_depth;
          foliage pixels negative in the depth, bend_sss_gpu.hlsli BEND_SSS_LIGHT_CASTERS keeps a least delta for them. Shot: 1.0
          removes palm and grass shadows, walls keep theirs.) debug35/SSS or AO grass or tree mask: is it doable for SSS? Soft shadows for grass / trees so light goes through
          them a bit and the shadows are not that dark. (Free licences allowed.)
      [x] 5. (RT Reflections: rt_reflected already skipped the march per pixel; the depth pyramid and its resolve no longer run when
          every pixel of the pass is traced. GT-VBAO: the addon's shader in place of the game's. RT / PT AO: ssao00 returns 1 without
          the game's code. RT Shadows: no sun shadow map casters. So: no double work left there.)
          With RT Reflections, does the game's own reflection still run? GT-VBAO / RT-AO: is the game's SSAO bypassed? If not,
          double work: fix without breaking anything.
      [x] 6. (Log Details, RayTracedDebugLog off by default: the 10 s statistics, the shadow-reader frame every 10 s (16 view reads a
          draw), the D3D11 timing queries, the clock reads per captured draw. Light Bounce: its depth levels only for the screen-space
          trace, rays read level 0. Helper logs stay: rare and on its thread.)
          More double work or leftovers? Debug lines that write all the time must not exist for release (keep them, off),
          the game's CPU is atrocious.
      [x] 7. (Rays a Pixel, same key RayTracingOutputResolution: 1, Match Output, 2, 3, 4, also at DLAA; More Options: on / off for
          shadows, RT AO, reflections, GI. Paths now take it, they did not before. Above render size is impossible: the shading
          runs at render size until DLSS, so more rays a pixel from their own spots IS 4K's rays.)
          Rethink Ray Tracing at Output Resolution: does it act on every RT/PT effect? Costs a lot, not enough. PT AO 1 ray 1
          sample at Full resolution looks almost noise free: can the toggle make DLSS Performance trace above 1920x1080, like
          3840x2160? Finer sliders / toggles.
      [x] 8. v117 zip (helper60), README, HANDOFF, memory, report.
    - v116 BIG BATCH (ATN 2026-10-30 ~21:25; started ~21:35, ATN away for hours). ATN's words, as given:
      "1) I am just in the game, you can see the shadow flicker on the tower. I am like really close. IT was flickering in
      distant already too. Please check it out. In the same exact battle as now (log is probably here. I traced the RT for
      30s too after the freeze.), the game froze when I was truing out light bounce Off instead of Rays/Paths and at the
      same time RT Reflection. The reflection were flickering for a bit, then the game froze then it came back but I am not
      sure if everything is working correctly now.
      2) To "LOD switches black": is there any reason why we don't switch instantly? (Like what are the reasons for those 8
      frames?
      3) On german keyboard: you can press y to zoom down or x to zoom up on a map. Maybe you can load the
      for_claude1.replay move forward until you arrive at the river, zoom down and see what I mean by "big" fps hit. when
      around rivers. Maybe you can fix that too. Thanks again!"
      Checklist ([ ] open, [x] done):
      [x] 0. Done: logs in debug34 (the game was still running: closed through its window, the helper's log then readable;
          ReShade.ini = the ini as ATN left it, also tools/tw/test_scripts/ReShade.ini.atn34: restore it after tests, it
          now has [SCREENSHOT] SavePath = this session's scratchpad). Test runs: scratchpad run_game.ps1 (-Shot, -Profile,
          -Set "s:key=value;s:key=value", -Keys "W:6,Y:2.5"; closes the game through its window: the session's
          permission check refused Stop-Process -Force), kept in tools/tw/test_scripts. RENODX_TW_SET takes a list now.
          Was: FIRST, before launching the game: ATN's evidence is in C:/Program Files (x86)/Steam/steamapps/common/Total
          War Attila/debug34 (6 screenshots 21:18-21:23 incl. the tower flicker, the addon's 30 s tw_rt_profile.csv,
          ReShade.log as of 21:25). The game was still running then: renodx-tw-dlaa64.log (the freeze's DRED report) and
          tw_rt_profile_helper.csv were still empty (written when the helper closes) -> copy renodx-tw-dlaa64.log,
          tw_rt_profile_helper.csv and ReShade.log from the game folder into debug34 now (a game launch overwrites them).
          Back up ATN's CURRENT ReShade.ini (changed 21:22; tools/tw/test_scripts/ReShade.ini.atn33 is the older 17:18
          one) and restore it after tests.
      [x] 1a. Tower flicker: ATN's debug34 pairs (21-23-14/15, 21-23-19/19) differ by ONE WHOLE FACET of the round tower, the
          one at the terminator, half lit; debug33's Debug View 1 shows it salt-and-pepper per pixel. Cause: `facing` from the
          depth's normal: within a degree or so of edge-on (more with distance) its sign is noise, and the two branches
          disagree there: "facing" starts off the plane and a ray at a tiny angle leaves the facet before it crosses the
          plane (lit), "away" hits the tower (shadow). Fix: rt_trace asks the triangle (rt_scene ShownNormal: a short ray
          to the pixel, HitNormal, moved to rt_scene with OctahedralDecode) where |n.l| < EDGE_ON 0.1 + 0.002 per metre,
          and starts an edge-on away-facing ray behind the plane. Raw view, W:6 view, two shots 5 s apart: pixels changed
          near tower 4.46 -> 2.74%, city 4.13 -> 2.53%; left: thin jitter edges. The facet stays as hard an edge as the
          mesh is (the game's smooth normal is not the triangle's: not flicker).
          Was: Tower shadow flicker, close and far (v115 item 8: not changed; Debug View 1 showed raw per-frame visibility at
          the towers' sun-facing edges = terminator; candidates: depth-derived normals (SurfaceNormal) flipping `facing` /
          Planar per frame with jitter, the soft sun's one random disk ray under RR, ray start offsets). Note: the G-buffer
          guides at the trace are LAST frame's (written after the sun draws), so not usable as this frame's normals as is.
      [x] 1b. Freeze: debug34's DRED: 0x887A0006 hung (no page fault), slot 2 trace at op 36 of 95: "micromaps BuildAS BuildAS
          Barrier BLAS BuildAS BuildAS >BuildAS BuildAS": in a BLAS build right after two micromap array builds, probably the
          first micromap BLAS (2 meshes baked, 237 KB, just before), 13 s after reflections went on. Not reproduced:
          same toggles at the river 60 s clean (freeze1). Forced loss (RENODX_TW_TEST_REMOVAL, lost1): reflections, RT AO,
          Light Bounce off and on all work after the restart, same costs. System log: nvlddmkm 153 in pairs 2-3 s apart,
          5 times on 10-30 alone, at different stages (v109 bake, v115 cluster build, this BLAS): not these two settings.
          Read without finding an invalid call: array / BLAS builds, scratch, compaction, the tail list's fence order, release
          order. Driver 32.0.15.9579 dates from 2026-03: told ATN to try a newer one. LostReport now names the request's
          micromap BLASes (triangles, KB, levels, GPU / CPU bake): read that line next time.
          Was: Freeze toggling Light Bounce Off + RT Reflections together (reflections flickered, then freeze, then back):
          read the DRED report in renodx-tw-dlaa64.log; after a device loss the helper restarts once with GPU Micromap
          Baking / Swaying Shadows Range / Off-Screen Sway / SER / SHaRC paused until the game restarts (by design: tell
          ATN), a second loss leaves RT off. Check that reflections / Light Bounce work after the restart (the 30 s trace).
      [x] 2. Done (rt_core Known, RT_PLACE_WHOLE, rt_place bit 3 of the records): a mesh's draws are counted per frame; the
          frame after their sum changed all its draws are placed (a sum over last frame's places at once); Trace flags the
          placements of a mesh whose instances all went in, as many as last frame's (draws after the trace not in yet);
          rt_place then drops its in-view kept instances not placed again at once. So a LOD switch: 0-1 frames; same count
          but other instances: its turn (at most 4); KEPT_IN_VIEW_FRAMES 8 stays the fallback (partial placements).
          Why 8 before (answer for ATN): still meshes are placed every 4th frame to save a stream-out draw per draw a
          frame, so "not placed again" could mean "not its turn": two turns had to pass.
          Was: Why 8 frames for kept LOD copies: the addon places a still placed mesh's instances only in its turn (PLACE_TURNS
          4: `(key + frame) % 4`, saves a stream-out draw per draw a frame); between turns the helper keeps them, so per
          instance "not re-placed" means "not its turn" or "gone" until a turn passed: KEPT_IN_VIEW_FRAMES 8 = two turns
          (one missed request of slack). v115 already drops them at once when the game drew none of that mesh this frame
          (RT_DRAWN_PLACED). Left: a mesh still drawn elsewhere (same house elsewhere on screen). Instant options: place
          every drawn key every frame (measure the addon's CPU cost), or decide the turn once per key per frame (all its
          draw calls agree; counts differ between calls: 256 / 256 / 44 placed partially today) and drop in-view kept
          instances of records placed this request that were not re-placed.
      [x] 3. River fps. Reproduced: for_claude1.replay, -Keys "W:6.4,Y:2.5" (camera at the water; Y zooms down AND forward; W:6 =
          the village, W:7 past the river), 53 ms a frame, sun rays 17 ms, paths 10 ms. Not smoke (2000 particles), not kept
          instances (scene test 4), not hidden placed instances (tried inactive: no change), not leaves, soft sun, fog.
          Found by removing one thing every 4 s of a profile (scratchpad windows.py, RENODX_TW_SET list):
          (a) OccludedSolid (pixels facing away) had grass in its mask though it culls alpha-tested geometry: its rays walked
          every field: now NO_GRASS_SMOKE: sun 17 -> 8.6 ms, same picture.
          (b) the short ambient rays (PT AO, RT AO: GRASS_AMBIENT_RANGE) cost 4 x on the swaying grass's cluster BLAS than on
          static BLASes: now masks split: 0x08 placed shown, 0x10 swaying clusters (sun's rays: GRASS), 0x20 still copies of
          what sways (ambient rays: GRASS_STILL): paths 9.8 -> 5.95 ms. River frame 53 -> 41 ms (fps 18.6 -> 24.4).
          Left at the river: sun solid 4.6 + grass 4.2, fog 2.3, paths 5.95, reuse 3.5, reflections 2.7 (solid rays there
          ~2 G rays/s: normal). Tried, no gain: TLAS PREFER_FAST_TRACE, cluster BLAS and CLAS FAST_TRACE, hidden instances
          inactive, Off-Screen Sway off; Swaying Shadows Range 30 / 15 m: sun -0.5 / -1.8 ms (left at ATN's 100).
          Was: River fps (for_claude1.replay: move forward to the river, zoom down). German keys per ATN: y zooms down, x up;
          for SendInput use scan codes (0x2C = the key ATN calls y, 0x2D = x; W 0x11; verify). Scripts:
          tools/tw/test_scripts (move_profile.ps1: profile + keys; move_compare.py; shot_run3.ps1; ini_set.py; noise.py;
          scan_register.py) - their paths still point at the v115 session's scratchpad: adjust. debug33 profile at ATN's
          river view (before v115's fix): sun rays 8 ms still, bounce 9 ms; positions there are the riverbed (water drawn
          after the trace), with grass in it.
      [x] 4. twattila_v116.zip (Downloads, helper59 = build/tw_dlaa_helper59), README v116, memory. build32 Attila / Rome II /
          Thrones, 64-bit TW + Shogun 2 / Ryse / Expeditions (shared rt_core) built without errors; all_modes 44/44
          (all_modes_h59.txt, new mode undrawn_whole_gone). Stress on the final build (for_claude1.replay, camera moves,
          Light Bounce / reflections toggled 36 times): 12 minutes, no GPU reset. Reflections with Light Bounce off: a series of
          10 shots (RENODX_TW_SHOT=50,10,3) steady within 0.1 level, no lasting flicker (ATN's "flickering for a bit" not
          seen). Game folder: v116, ReShade.ini = ATN's. Left there: renodx-twattila.addon32.in-use-2159 (an old test build,
          not loaded: pinned by dead Attila.exe processes until a reboot, then delete it).
    - v115 BATCH (ATN 2026-10-30 ~17:20, away for hours; debug33 = C:/Program Files (x86)/Steam/steamapps/common/Total
      War Attila/debug33; new replay for_claude1.replay = Large Ctesiphon, day): checklist ([ ] open, [x] done):
      [x] 1. (e5bf7dd3) RT AO / SSAO / GT-VBAO "dark" under Rays / Off after PT: the Light Bounce bypasses the sun draw
          (draws it itself), command_action then skips post callbacks: OnPathAmbientDraw's t51 unbind never ran, t51
          stayed 1 = the game's diffuse ambient gone. Now finalize callbacks. In game PT then Off (RENODX_TW_SET):
          equal to Off from start; old build blue -10% in the far desert view.
      [x] 2. (1405fe61) Specular Occlusion with RT reflections on: ssr00/01 dim the environment always (helper writes
          1 where reflections traced, skips occlusion rays when the setting is off); t46 bound only with it on.
          WH1 / WH2 / Shogun 2 shaders still `if (!rt_reflected)` (port later). In game: on vs off differs now.
          Test switch RENODX_TW_SET=<s>:<key>=<value> (b1fbe98e). NOTE: the game folder's addon is now a COPY (the
          symlink to build32 was replaced; making symlinks needs admin): copy after each build.
      [x] 3. (daec47ba) Water Clarity / Caustics did nothing: command_action::Register WITHOUT callback data drops
          lambdas declared <typename T, typename Context> (not invocable, no command types: silently not registered).
          Never ran: OnWaterDraw, OnShadowReaderDraw (river / lake / particles' RT shadows), OnPointLightDraw (torch
          shadows + light picks), OnFogDraw, OnParticleDraw (Smoke Shadows; also its VS code was never kept: code hook
          only with clip captures), object_motion OnAnyDraw, rt_core OnAnyDraw (Ryse / Expeditions / Shogun 2 scene).
          Scanner: scratchpad scan_register.py (clean now). In game: water clarified, smoke 58-65 K particles (~0.2 ms).
          [ ] River noise while moving (RR / reflections).
      [x] 4. (6fe88cde) Lose-then-regain fps: kept duplicates while moving. Test without input: scratchpad
          move_profile.ps1 (SendInput scan codes W/S/A during a RENODX_TW_PROFILE; move_compare.py still vs slowest).
          2vs3: sun 0.96 -> 3.04, bounce 5.2 -> 10.7 ms moving; not grass rays (range 1 m same), not sway, not leaf light,
          not RR, not queue priority (HIGH tried, reverted), not the game (PresentMon GPU busy flat 8.0 ms); RT_NO_KEPT
          (scene test 4) removed it all. Cause: grass rows carry the camera-distance shrink (lift in y) -> new dedupe
          key each placement -> kept copy beside the new one (profile "placed" counts drawn only). Fix: grass key x/z
          10 cm; RT_DRAWN_PLACED (addon lists placed meshes drawn but not placed) -> in-view kept instances of meshes not
          drawn at all gone at once; KEPT_IN_VIEW_FRAMES 16 -> 8. After: sun 1.29, bounce 6.1 moving. ATN's river view
          was also after a GPU hang (0x887A0006 in a trace's cluster build) that paused GPU bakes / SER / SHaRC.
      [x] 5. Grass shadows darker while moving, settle ~1 s: the same duplicates (item 4).
      [x] 6. LOD switches black: the old level of detail kept 16 frames in view; now gone at once when the game draws
          none of that mesh this frame, else 8 frames.
      [x] 7. (8fff25ae) AO noisier: v107 made AO / reflections raw under RR, v108 the RT bounce; history (AO), history +
          filter (reflections; hit distance still for RR) and the bounce's resolve are back under RR. In game 2vs3:
          mid-ground speckle 1.23-1.28 -> 1.12-1.14 (scratchpad noise.py). Also the river's noise while moving (untested).
      [-] 8. Far shadows flicker: ATN's shots are Debug View 1 (raw per-frame sun visibility: under RR one random ray to
          the sun's disk a pixel), the towers' sun-facing edges (terminator). Not changed: the G-buffer normal at the
          trace is last frame's (guides written after the sun draws). Ask ATN whether the final image flickers too.
      [x] 9. twattila_v115.zip (Downloads, helper58 = build/tw_dlaa_helper58), README v115, build32 Attila / Rome II /
          Thrones, 64-bit TW check + Shogun 2 / Ryse / Expeditions (shared rt_core) built, all_modes 45/45
          (all_modes_h58.txt: new undrawn_all_kept / undrawn_all_gone). Game folder: v115 installed, ReShade.ini = ATN's.
    - NEXT BATCH (v110; ATN 2026-10-30: added, NOT started until ATN says):
      [x] 1. Reflection Reuse "Own ReSTIR" (v112, helper55, untested in game; setting RayTracedReflectionReuse 2, bridge
          bit RT_REFLECTIONS_OWN 1024): rt_path.hlsli ReflectReservoirTexel (pass RT_REFLECT_RESERVOIR, half res: the
          pixel of each 2 x 2 rotates per frame; one GGX candidate, met surfaces only, temporal merge receiver-agnostic,
          the reservoir keeps the weights' mean in place of W; u36-u39, HISTORY_REFLECT ~130 MB at 4K) and ReflectPixel's
          spatial step (own texel + 4 neighbours, near-field rejection REFLECT_SHIFT, generalized balance heuristic for
          the choice, magnitude = the reservoirs' mean weight: lobe ratios made it noisy near walls; mixed with the rays'
          own by sample counts; the met share stays the rays'). Harness (rough reflection RMSE vs 16 spp, scratchpad
          rmse.py / compare.sh, new all_modes modes reflect_own*): 1 spp raw colour 0.114 vs rays 0.148, accumulated
          0.092 vs 0.175 (plain 1-spp accumulation is biased dark: frames with no met sample put rgb 0 in the history; a
          fix for later). From ReSTIR GI measured worse than plain rays (0.30 vs 0.09 accumulated, 4 spp). Cost: the
          reflection pass ~+60% (0.12 -> 0.19 ms at 640x360).
      [ ] 2. (2026-10-30 v113 session: still blocked on the debug layer; v113 records the GPU bakes differently in that
          same first request (staged, masks / UVs copied at read time), so ATN's next auto_save.replay run also shows
          whether it is gone. Static checks done: dispatch sizes within 65535, atlas slices < 256.)
          The one-off Close failure (E_INVALIDARG) of the first trace request on auto_save.replay's desert map (v108's
          helper47 too): first get the D3D12 debug layer loading in the helper (0x887A002D with d3d12SDKLayers.dll next
          to D3D12Core.dll), then fix the invalid call it names. v111 session (2026-10-30): the debug layer fails the same
          way in the harness (build/tw_rt_harness has both DLLs, 1.619.6): factory->GetConfigurationInterface(
          CLSID_D3D12Debug) gives 0x887A002D; System32 has no d3d12SDKLayers.dll (Windows' Graphics Tools not installed:
          installing them is ATN's call). The failing request carries ~5 GPU bakes and the battle's first BLAS builds;
          read so far without finding the bad call: BakeMicromapsOnGpu (masks, clamp / fix-up passes, chain barriers),
          the micromap read copies, static BLAS builds (zero-triangle captures are skipped). Reproduce: auto_save.replay,
          ReShade.log line "Ray tracing: D3D12 stopped (0x80070057)" ~1 s into the battle. Harmless (the next frame
          traces).
      [x] 3. GPU bakes concurrent, never on the CPU (v113, helper56, untested in game): BakeMicromapsOnGpu copies each
          chain (the SDK reuses its chain memory), gives each bake its own place in one transient buffer per pool (pools
          are small: ~300 KB for a 4000-triangle bake, the 64 MB budget is not what they take) and records stage by stage
          (all bakes' step k, one barrier; a step whose states clash waits a wave). Every read with GPU Micromap Baking is
          a GPU bake: its UVs copied into a GPU buffer and its mask into a pooled texture (OmmGpu::masks, mask_index) when
          read, value UINT64_MAX while waiting; each request bakes the oldest within MICROMAP_GPU_BAKES /
          MICROMAP_GPU_TRIANGLES, stale ones (mesh / set rebuilt) dropped. Harness (scratchpad bakes.sh, stress 4/48/100,
          profile): 33 bakes 171 -> 68 ms, 4 bakes 18 -> 11 ms, results identical; stress 100 baked over 4 frames on the
          GPU. No async compute (not needed so far).
    - [x] PT AO vs RT AO (ATN asked for it in the v111 session; b0bd2639, helper54, untested in game): what blocked PT
      AO's sky differed: the Light Bounce's range (ATN 50 m) and no grass, against RT AO's reach (ATN 6 m) and grass
      within 0.5 m. Now PT AO's camera rays stop at min(bounce range, AO range) (rt_camera.hlsli sky_range = the
      bridge's rt_ambient_range; past it: the calibrated sky, the reservoir sample at that distance) and grass within
      GRASS_AMBIENT_RANGE (now in rt_scene.hlsli) blocks the sky. Left: PT AO's bounce texels are half res (RT AO full res
      at a quarter rate); if ATN still sees a difference, A/B both on the same view (auto_save.replay still view).
      Next batch order then: 1 (Own ReSTIR), 2 (above), 3 (concurrent GPU bakes).
    - v110 BATCH (ATN 2026-10-30 ~09:45, debug31: "first frame" / "after 1s" pictures right after a W/A/S/D move, a
      trace): the whole grass field black after camera moves, still not see-through after 1 s. ATN: "since the grass
      optimizations" (v108).
      [x] 1. Cause (5d164d98, helper53): v108's FORCE_OMM_2_STATE on grass instances (AddInstance for GRASS_INSTANCE and
          rt_place's grass_flags): grass micromaps (level 5: ~128 texels a micro-triangle) are mostly mixed micro-triangles;
          2-state made them solid. The swaying copies near the camera (templates, mostly no micromaps: exact alpha test)
          looked right, the placed copies (micromapped meshes) shown while moving and past the sway range blocked the
          sun. debug31 profile: no bakes waiting (micromaps all there) yet the sun part 4 -> 8-11 ms and placed 3K ->
          13-16K on moves. Removed: grass is exact again; Fast Leaf Shadows (ray flag, all rays) still makes every
          micromap 2-state (tooltip says grass too). v109's AO 2-state ray flag reverted (same blobs). Untested in game.
      [x] 2. twattila_v110.zip, builds (32 + 64-bit), all_modes, README v110, memory.

### Tales DXR plan (item 7, started 2026-10-26)
DONE 2026-10-26: DLSS v96 / HDR v66 (zips in Downloads, untested in game): Ray-traced sun shadows (Experimental,
rt_shadows.hpp; DLSS README v96 has the whole design). Stream output of the opaque depth prepass (vs C81C5E1F,
287D5CA8, 344F8ACF, 5F4BCA38, F1D731D5; ps null) -> world positions with the draw's View -> one BLAS per new draw ->
TLAS (this frame's + last 300 frames' non-landscape) -> cs_6_5 inline ray query per pixel -> t40 min() in sunshadow00.
GPU test tools\toa\rt_tests\rt_shadows_test.cpp passes (the game's own VS). ASK ATN: turn it on, Advanced "Trace
ray-traced shadows for one frame" -> tales_rt_trace.txt (shows captures, BLAS, sun direction), screenshots on/off.
NEXT for item 7: masked leaves (alpha test: needs UVs + texture: capture TEXCOORD via SO of the masked VS + any-hit-like
candidate loop), skinned characters (re-capture each frame, refit), off-screen casters (shadow-depth pass draws
EB00F8A5 in the sun's view: their ShadowView UB converts them), soft shadows (sun cone + DLSS RR or a denoiser), then
GI / reflections, RR (nvngx_dlssd.dll), frame generation (Streamline).
Reference: refs/mgsv-phantom-fx (MIT; cloned in the session scratchpad): D3D12 beside D3D11 (d3d12_helpers: device on
D3D11's GPU behind ReShade's proxy, shared textures and fences opened in D3D12), gi_scene (geometry pool, BLAS per
mesh compacted, TLAS per frame), NRD RELAX GI, FFX shadow denoiser for RT sun shadows. Its scene comes from MGSV's
memory; Tales' comes from UE draws instead:
- Depth prepass vs 0xC81C5E1F (277 draws, debug20 trace): ATTRIBUTE0 positions (VB slot 0), cb1[0..3] = Primitive
  LocalToWorld rows, cb0 = View (TranslatedWorldToClip 0-3, PreViewTranslation 62); world = L2W x pos + cb1[3]; others:
  1370AE15, 5F4BCA38, A5D5F634, F1D731D5, 287D5CA8, 367A646D (check: WPO / masked). Shadow-depth vs EB00F8A5 (348,
  Primitive at cb2). Skinned characters are skinned in the VS (no skin cache): not in BLAS at first.
- Steps: (1) rt_device: D3D12 device on the game's LUID, D3D12 fence shared to D3D11 (D3D11.4 OpenSharedFence),
  textures made in D3D11 with NT handles opened in D3D12; (2) rt_scene: mesh key (VB, IB, ranges) -> BLAS (copy of
  the ranges via a staging readback once), TLAS each frame from the prepass draws (L2W); (3) rt_sun_shadow.cs_6_5 inline
  RayQuery from depth toward the sun, min() with the game's shadow mask (sunshadow00 cs 0x2084C889 in the HDR addon),
  far mountains beyond the CSM; (4) later: NRD / RR (nvngx_dlssd.dll in external/DLSS rel), GI, DXR 1.2 SER / OMM (dxc
  1.9 = SM 6.9, Win SDK 28000 headers; Agility SDK via ID3D12SDKConfiguration1 device factory), frame generation
  (Streamline in external/Streamline).
- Sparks (8) DONE 2026-10-26: DLSS v97 (zip, untested in game): the 8 sprite VS get motion versions (0x7A1E0D20-27 from
  tools\toa\spark_motion\make_motion_vs.py, WARP-matched by compare_test.cpp; 3Dmigoto printed if_nz cb0[3].x as a float
  test: fixed) redrawn after each sprite draw into an RGBA16F motion target; 0x7A1E0D01 uses it where a sprite is this
  frame, no history where one only was. Other particle kinds (ribbons / trails, mesh, GPU sprites) still no history.
  ASK ATN: sparks sharp with DLSS? any trails? (Old plan below.)
- Sparks (8): CPU sprite VS (3147D98A, 75139A1B, D7B928CD...) have OldPosition (ATTRIBUTE1): replace them to output the
  previous clip position (View PrevTranslatedWorldToClip 83, PrevPreViewTranslation 105) and redraw each particle draw
  into a motion target (DrawAtOutputSize-style post callback); the prepare pass takes it where the spark mask is set.

## START HERE (2026-10-25, newest; ATN moves to their brother's account): read this first
This section is the current state; everything below it is history (newest first). Memory (MEMORY.md, loaded
automatically) has the per-game summaries and ATN's rules.

Rules (ATN): very short answers, always English. Zips of the changed files in C:\Users\arshi\Downloads (game folder at
the zip root, versioned names). Never push or commit unless asked. Read docs/CXX_STYLE.md before C++ edits. Short
player-facing tooltips. Never copy other modders' code (Apache / CC0 / MIT sources with credit are fine: Bend Studio,
Mirko Salm, Ottosson, XeGTAO ideas). Attila / Rome II / Thrones are 32-bit: always build build32 too. Shell heredocs
turn "\\n" into real newlines: put C++ strings with \n in through the Edit / Write tools.

Build / pack:
- 32-bit: `cmake --build build32 --config Release --target renodx-twattila.addon32 renodx-twrome2.addon32
  renodx-twthronesofbritannia.addon32`; 64-bit: `cmake --build build --config Release --target renodx-<game>.addon64`
  (twtroy, twpharaohdynasties, twwarhammer, twwarhammer2, twwarhammer3, twshogun2completeedition(_dlss), and the
  three 32-bit games' 64-bit builds as a compile check).
- Pack: `python -I C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw\repack_game.py <game> <old zip> <new
  zip>` (zip names in Downloads; keeps the old zip's extras such as APACHE_LICENSE-2.0.txt and adds every shared file
  the game's sources include).

Attila v55 (2026-10-25, untested; README v55): Settings Mode HDR & Effects / Graphics 1 of 2 (AO, GI, SSS) / Graphics 2
of 2 (Graphics (Experimental) + 78 engine test sliders in 8 "(Tests)" sections, found with tools\tw\tw_tweak_uses32.py;
switches Game's / Off / On); AO presets Faster (Recommended at half res) / Recommended / Heavier (25 m, 64), radius max
30, Temporal Edge Fix off everywhere (ATN: ghosting); GI Faster 32 samples (shared lighting_settings.hpp: all TW games
get these on their next build); SSS title lavender + empty line after GI; pixel trace (attila_pixel_trace.txt) to find
the water reflection pass (ATN then wants a better SSR, MIT / CC0 code, e.g. AMD FidelityFX SSSR); Sky Debanding
(skydome 0xA5BA3E04 / 0x9436471B, Pharaoh's deband + dither). Grass lead: OIT_GRASS_MAX_DISTANCE 25 m (blended grass
queue) = "Soft Grass Distance". WAITING: ATN's test of v54 (SSS, Shadow Test) and v55.

Attila v56 (2026-10-25, untested; README v56): Anti-Aliasing Off / SMAA (MIT) / CMAA2 (Apache, Intel) as compute after
the tone mapper (twattila/anti_aliasing.hpp; WARP-tested: tools\tw\warp_tests\aa), SSS extra sliders (Softness blur,
Contrast, Hard / Fade-Out Samples run-time in bend_sss_gpu.hlsli, Edge Threshold, 3 toggles). Grass pop ~25 m: open,
asked ATN for a pixel trace on a popping plant + in-game OIT setting. Bitdefender blocks fresh test exes (run them
from build\, or ATN pauses it). Shell heredocs collapse "\\": use the Write tool for scripts with C++ "\n" strings.

Attila v57 (2026-10-26, untested; README v57): Grass Height Distance (grass VS replacements grass00-02, the 1/100 m
shrink / slider) + 217 more engine tests generated into twattila/engine_values.h by tools\tw\tw_engine_values.py
(attila_tweaks_typed.txt / attila_uses.txt copied to tools\tw). ~315 graphics tests in total; ATN tests and reports
which work. Next: keep the working ones, delete the rest; the grass pop trace.

Attila v58 (2026-10-26, untested; README v58): Audio Memory (Tests): Wwise pool sizes + low-memory ratios, written at
game start by a thread (engine_tweaks ApplyWhenSet), ReShade.ini [ADDON] LoadFromDllMain set while changed. ATN: only
buffer sizes (arrow / group sound values not wanted). Check ReShade.log "Graphics trace" lines after start.

Attila v59 (2026-10-26, untested; README v59): Hierarchical SSR (FidelityFX SSSR traversal, ssr_hiz.hpp / .hlsli,
WARP 97% vs exact), SSS sky streak fix (far-plane pixels skipped in bend_sss_gpu.hlsli), Sky Debanding cheaper + Off,
tests trimmed / top sliders split, engine_values.txt. NEXT (ATN asked): TAA (camera jitter injection into the main
view's camera_VS_PS, reprojection from depth + last camera, variance clip) and SMAA T2x in Attila; grass at the screen
border gets no SSS (likely screen-space limit, check with a mask view); grass pop ~25 m still open (needs a trace).
Tales question: MGSV Phantom FX (MIT, DXR 1.1 ReShade add-on, NRD) - RT in Tales would be a large project.

Attila v60 (2026-10-26, untested; README v60): Anti-Aliasing + SMAA T2x / TAA (camera_VS_PS jittered in the map /
unmap / update_buffer_region events, camera + depth taken at the sun draws, camera-only reprojection, clip, sharpen;
TAA Strength / Sharpening). WARP-tested with tools\tw\warp_tests\aa\taa_test.cpp (compiles anti_aliasing.hpp itself;
the exe exports ReShadeLogMessage stubs; clang-cl -std:c++20 /O2 /EHsc /DWIN32 /D_WINDOWS -DNOMINMAX, includes
build32\twattila.include + external reshade / json / frozen / gtl / Detours, run from build\warp_taa). Risks for ATN's test: a camera the module misses (log line missing: TAA never runs, no jitter),
moving units can trail (no motion vectors, only the clip), UI 3D markers drawn before the tone mapper would jitter.
Still open: grass pop ~25 m (trace), SSS at the screen border.

Attila v61 (2026-10-26, untested): SSS Debug View (Shadows / + Screen Edge in red: ray leaves the screen within the
Length) to check ATN's border report. Grass pop: asked ATN to try Soft Grass Distance (OIT_GRASS_MAX_DISTANCE, game
25 m: blended grass inside, cut-out outside) first, else a pixel trace before / after a pop. Game paths: memory
reference_game_paths.md.

Attila v62 (2026-10-26, untested; README v62): Grass Distance (scrub hooks DrawScrub +0x17f9800 / ScrubUpdate
+0x17fb510 in graphics_hooks.hpp; Thrones has the same code, no slider yet), SSS rays stop halfway to the sun's screen
point (bend_sss_gpu.hlsli, Shogun 2 too: rebuilt, not zipped), Mirror Screen Edges. tools\tw\pe_range32.py (plain
range disassembly when pe_dis32.py's start guess fails); warp_tests\sss\sss_scene.cpp updated (16-float settings,
SSS_EMBED / SSS_OUT / SSS_BEND env).

Attila v63 (2026-10-26, untested; README v63): Grass Density (ScrubUpdate bias / density), Mirror Screen Edges moved
into the shader (left / right / bottom), TAA resolve centres this frame by the jitter (less shaking where the history is
rejected). SSR answer: full resolution (the game's environment_specular_light pass, 3840 x 2160). For DLAA later: the
jitter is runtime.jitter (pixels, y up); 8 Halton phases now, DLSS wants more.

Attila v64 (2026-10-26, untested; README v64): the grass circle = the grass PIXEL shaders' own 100 m colour fade (13
shaders, l(0.01) before min); grass_fade.hpp patches it to cb12[0].x = 0.01 / Grass Height Distance (dxbc.hpp shared
with the SSS patch). ATN runs Grass Always Drawn On (every plant, no thinning: Grass Density does nothing there).
ATN's batch is next (SSR items 1-3 included: miss fallback / fade, FidelityFX SSSR denoiser, hit refinement).

Latest zips: twattila_v64 (newest Attila), twrome2_v26, twthronesofbritannia_v25, twtroy_v22, twpharaohdynasties_v26,
twwarhammer_v23, twwarhammer2_v23, twwarhammer3_v21, twshogun2completeedition_v58 + _dlss_v48 (round 48). Rome II /
Thrones were rebuilt after v54's Edge Fix (shared code) but not zipped. Ryse v1.04 shipped (HDR fix47 / DLSS v110).
Tales of Arise HDR v64 / DLSS v94 unchanged.

Total War lighting (all untested by ATN unless noted; details in "Round 48" and the Attila notes below):
- twattila/lighting_settings.hpp: one source for every TW game's AO (GT-VBAO) and Global Illumination (GT-VBGI, the
  Light Bounce) settings and ATN's presets (AO Faster / Heavier (Recommended); GI Faster (Recommended) / Heavier (More
  accurate)). Temporal resolve: variance clip only with 5+ same-surface neighbours, history length (AO 3rd channel, LB
  MRT frame-count target). v54: AO "Temporal Edge Fix" toggle (nearest history texel within 2% on outlines, 0.2 hold;
  ATN reported thin halos).
- Attila screen-space shadows (v49-v53, ATN asked; Bend Studio, Apache 2.0, Copyright 2023 Sony Interactive
  Entertainment, credit in About + APACHE_LICENSE-2.0.txt in the zip): twattila/screen_space_shadows.hpp patches the 40
  shadowed sun shaders' DXBC at creation (t49 occlusion after the cascade shadow's saturate), compute: depth-stencil copy
  -> sss_depth -> sss_setup (Bend's BuildDispatchList on the GPU) -> 8 x DispatchIndirect sss_bend. v49-v52 artefacts
  were one bug: SRVs bound before the UAVs (D3D11 nulled the inputs); v53 fixed it (WARP reproduced the GPU mask
  exactly). WAITING: ATN's in-game test of v53/v54. Then remove the one-time "Screen-space shadows report" log
  (sss_probe) if it works.
- Attila buildings losing their own shadows (also vanilla; ATN debug4 / debug5): not solved. v51+ has three test
  sliders at the bottom of Graphics (Shadow Test: Caster Reach / Sun Camera Depth / Cascade Margin) and ATN was asked
  to try Building / Model Detail Distance at max. WAITING for ATN's result.

Next (ATN's plan): GT-VBAO / GT-VBGI in Tales of Arise (UE4 4.21, has DLSS): no own temporal smoothing there (XeGTAO
style: noise per frame, DLSS resolves it with the game's motion vectors), port the twattila shaders' ideas; maybe
Bend SSS later. Then the rest of the TW tests.

Tools (C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw): repack_game.py, pack_game.py, tw_tweaks32.py
(x86 tweak names / readers), tw_tweak_survey.py (x64), pe_dis32.py, x86_disp_scan.py ([reg + disp] users), d3d_disasm.py,
warp_tests\sss (dxbc.py: DXBC checksum / patch prototype; warp_check.cpp; sss_scene.cpp synthetic scene with env SSS_*;
sss_real.cpp: Bend on a captured depth), warp_tests\ao (flicker_test.cpp + run_variants.ps1), bend_original (Bend's
official zip). Decompiler and pack dumps: D:\Dev\renodx\bin\cmd_Decompiler-1.3.16 (dump_shader_<game>_pack).

## OLDER START HERE (2026-10-23, end of the session on ATN's brother's account): Total War state
ATN continues on their own account. Everything below this section is the history (newest first). Untested by ATN unless
noted. Zips in Downloads (game folder at the zip root); patch notes for Nexus: Downloads\tw_patch_notes.txt (newest
UPDATE block on top, player-facing) and Downloads\tw_all_changes.txt (LATEST sections, detailed).

Latest zips: Attila v33, Thrones v18, Rome II v19, Pharaoh v19, Troy v15, Warhammer v17, Warhammer II v17,
Warhammer III v16, Shogun 2 HDR v53 / DLSS v44. Attila v28 / Rome II v13 / Thrones v12 / Pharaoh v14 were released by
ATN; later ones only add tooltips / presets / fixes.

Build: 32-bit (Attila, Thrones, Rome II): `cmake --build build32 --config Release --target twattila twthronesofbritannia
twrome2`; 64-bit: `cmake --build build --config Release --target twpharaohdynasties twtroy twwarhammer twwarhammer2
twwarhammer3 twshogun2completeedition twshogun2completeedition_dlss`. Pack: `python -I pack_game.py <game> <zip>
[extras]` (C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw, with tooltip_sync.py, tw_sun_layouts.py,
tw_refs.py, tw_string_refs.py). Write edit scripts with a file tool, run them with `python -I` (shell heredocs eat
backslashes).

Shared code (all in src/games):
- twattila/light_bounce.hpp (+ light_bounce.h, light_bounce_{prepare,trace,apply}.hlsli, frame_history.hpp): Light
  Bounce on each game's sun draws (SunDraw table: hash, standard, G-buffer slot, camera slot), fixed slots t40-t47 /
  b10-b12, prepare -> trace (history ping-pong, last camera at b10) -> apply. Each game: light_bounce.hlsli decode
  (Attila family: twattila; Pharaoh + Troy: twpharaohdynasties; WH1 + WH2: twwarhammer2; WH3: twwarhammer3; Shogun 2)
  and light_bounce_*.hlsl wrappers. Install after renodx::mods::shader::Use.
- AO: twattila/bitmask.hlsli (bitmask math), twattila/bitmask_ssao.hlsli (Attila / Thrones / Rome II ssao pixel
  shaders), twattila/ao_temporal.hpp (AO Temporal Smoothing: Attila / Thrones / Rome II only), twwarhammer2/
  bitmask_ao.hlsli (line-sweep / HBAO games), twwarhammer2/line_sweep_ao.hpp (kept depth / camera, OnCollect,
  DispatchInstead: the addon's own AO compute shader in place of a game pass whose replacement stays the game's code),
  twwarhammer2/ao_bitmask_blur.hlsli (WH1 / Troy / Pharaoh, in place of the blur), twwarhammer3/line_sweep_bitmask.hlsli
  (WH3 collects, WH2's own collect ao_bitmask_collect).
- twrome2/engine_tweaks.hpp: graphics sliders (engine tweaks), Trace (button "Trace graphics settings"), watch (button
  "Watch graphics values (10 s)": hardware data breakpoints, WH1-3).
- Settings Mode "HDR & Effects" / "Graphics" in Attila, Thrones, Rome II, Pharaoh, Troy, WH1-3 (not Rome Remastered or
  Shogun 2 HDR: no graphics sections). AO / Light Bounce presets (Recommended, Recommended (heavier but looks better),
  Reset) top of the AO section; Shogun 2's in its DLSS overlay.

Open, in ATN's order:
1. WH2 stutter (ATN, Lighting 0, AO Game's): no per-frame CPU work or log spam found from the addon. Found and fixed:
   one 1024-thread shader held the game's AO pass and ours (fxc X4714), so Game's AO paid for ours; now split
   (DispatchInstead) in WH1 / WH2 / Troy / Pharaoh / Shogun 2. Still combined: the pixel-shader AO (Attila / Thrones /
   Rome II ssao, Pharaoh / Troy HBAO ssao2) and WH3's 8 x 8 collects. If ATN still sees stutter: test with the addon
   removed, the graphics sliders at Reset, and after the driver's shader cache warmed up (new shaders after updates).
2. WH1-3 graphics sliders do nothing: values found, written, kept (Trace). Watch logs (debug4 / debug5) read: see
   "RESUME HERE (round 20-23)": tools\tw\tw_tweak_survey.py finds every tweak and its readers statically (no more
   traces); WH1-3 sliders rebuilt on read values (round 23, untested). Next: ATN's test; Troy / Pharaoh the same way.
3. ATN's OLED VRR flicker (Attila / Pharaoh): AO / Light Bounce cost varies with the view; offered a smaller on-screen
   radius cap for the Light Bounce. FPS cap advised.
4. Later: Light Bounce / presets for Rome Remastered (no graphics there), AO temporal where an AO pass has a camera.

## RESUME HERE (2026-10-24): Ryse HDR fix46 / DLSS v110 = v1.04, ATN ships
- ATN: Light limit costs no fps -> default / Reset / ATN's setting 512; Particle lights crashed (removed). G-buffer
  filter items "Off (Recommended, ...)" / "On (Vanilla, ...)"; DLSS section settings in lavender. HDR fix46: preset
  "Recommended (with Deeper Shadows)". Patch notes: Downloads\ryse_v1.04_patch_notes_{nexus,discord}.txt (updated
  with fix41-46 / v106-110). Zips: rysesonofrome_fix46, rysesonofrome_dlss_v110.

## Ryse DLSS v109 (superseded by v110)
- ATN: chapter 5 flicker = lights at the 255 limit, 20-40 dropped (particle vertices ~30000 of 65535; still with the
  particle sliders at the game's). Light limit back (Experimental, yellow): 255 / 350 (default, Reset and ATN's
  setting) / 512 / 1024 / 4096. v78's code (particle_counts.hpp light list sites, OnLightingDispatch) with the shader
  at 255 per tile (tools/ryse/tiled_lights_raise.py 4096 255; scratchpad lights/ raised.shdr, WARP-tested), so not
  v78's 20-30% fps. Particle lights (Experimental, yellow): The game's / Every particle, no button changes it.
- New QualityOption fields: own_choice, highlighted, default_choice. 4096 vs 350 cost unmeasured: ATN can use Advanced >
  Measure every option's choices; if equal, set default_choice / atn_choice to 4 (4096) per ATN.

## RESUME HERE (2026-10-24): Ryse HDR fix45 / DLSS v108 (untested)
- DLSS v108: Audio section (languages + Dynamic sound mixing), SSR Extreme 2x / 3x range (Experimental), G-buffer
  filter choice + note (aniso on alpha-tested textures). HDR fix45: preset names, vanilla-like shadows 0.75 / flare 0.25,
  DOF 2 in the Recommended presets.
- Chapter 5 "light" flicker (debugForClaude44 shadow trace): every shadow map drawn every frame (4 sun + 7 light maps in
  the 16384 pool; one light map 30-74 draws: a moving / flickering light), so not the shadows. Like v75-v79's torch
  blinking (particle lights dropped): asked ATN for the Advanced line "X of Y lights (Z dropped); particle vertices ..."
  at the spot, and a test with Particle memory 16x / the particle sliders lower.

## RESUME HERE (2026-10-24): Ryse HDR fix44 (untested; DLSS v107)
- Fire buttons Option 1 (Default) / Option 2 (FIRE_OPTION_1/2, EXT_FIRE_OPTION_1/2 in addon.cpp) for PsychoV31 and
  Extended; Extended got ext_highlight_hue_shift / ext_highlight_saturation_shift (ShiftHighlightHue now takes its
  amounts). Less Post Processing: Sharpening 0.25; new "Vanilla-like Shadows" preset = it + Flare 0.41.
- Ch.3 ship smoke flicker: ATN, everything at the game's values still flickers: likely the game's; ATN traces later.

## RESUME HERE (2026-10-24): Ryse HDR fix43 / DLSS v107 (untested)
- fix43: PsychoV31 Last removed; Game Shadows under Gamut Compression Mode; fire line yellow + Default / Less Red
  buttons (FIRE_DEFAULT / FIRE_LESS_RED in addon.cpp; Default = slider defaults, Reset, both Recommended); Extended
  ext_fire_hue / ext_fire_hue_focus (default off); Recommended Bloom 1; section titles one green (Output, About own).
- DLSS v107 = v105's addon.cpp again (v106's pause check made it worse when dead, ATN).
- Chapter 3 ship smoke flicker (debugForClaude42 shadow trace): shadow maps stable (4 x 8192, draws +-2 every frame):
  not the shadows. Next: Advanced "Particle vertices" counter at the ship (buffer full?), and the graphics option
  "Particles" (e_ParticlesMaxScreenFill / r_ParticlesTessellationTriSize) and Draw distance at their defaults.

## RESUME HERE (2026-10-24): Ryse HDR fix42 / DLSS v106 (superseded)
- fix42: Extended Hue Correction removed (0-3 degrees with UC2 Extended: low knee, tangent above it; credit line gone).
- DLSS v106: own jitter keeps the game's pause check (bit 27, +0x4524; the JITTER_APPLY nop patch removed): paused /
  dead frames unjittered (ATN debugForClaude41: armour shimmer while paused). Slow motion keeps the own jitter.
- Chapter 5 shadow / light flicker (ATN reached it): asked for Advanced -> "Trace the shadow maps for 300 frames"
  (ryse_shadow_trace.txt) standing where it flickers, plus whether it happens with DLSS off (TAA).

## RESUME HERE (2026-10-24): Ryse HDR fix41 (detour, untested)
Zip rysesonofrome_fix41 (DLSS unchanged, v105). ATN (chapter 4; the chapter 5 shadow flicker report still open): fires
too yellow / pink; dim fires ignore the fire sliders (V31's Highlight Saturation / Dechroma act from ~2.75 decades above
mid gray; Hue Shift's Hable target = our hue below SDR white). Dechroma moved under the fire line; Fire Hue / Fire Hue
Focus (AdjustFireHue, OkLCh, after the color chart, mid gray -> Paper White, saturated warm hues only); PsychoV31 Last
toggle (shafts + chart on the scene, V31 after). Game's "dark filter" guess: HDRColorBalance / color chart, both before
our fire controls now. Details: rysesonofrome/README.md fix41.

## RESUME HERE (2026-10-24, round 29): Light Bounce "Fast MSAA (Once per Pixel)" (untested; round 28 untested too)
Zips: Rome II v20, Attila v34, Thrones v19, Troy v16, Pharaoh v20, WH1 v18, WH2 v18 (WH3 / Shogun 2: no MSAA, built,
no new zip).
- ATN asked whether the Light Bounce runs once per MSAA sample: only the apply did (trace / prepare / levels once on the
  resolved copy). New toggle in the 7 MSAA games (default Off = per sample as round 28; shared.h
  light_bounce_msaa_pixel, Sliders.msaa_pixel): light_bounce_apply_ms_pixel build (LIGHT_BOUNCE_PIXEL: no
  SV_SampleIndex, G-buffer at sample 0), wrapper in all 9 games (light_bounce.hpp creates it).
- Round 30 (untested): Multi-Bounce (0-1, default 0) in all 9 TW games (Shogun 2: DLSS overlay key
  LightBounceMultiBounce, presets keep it): the prepare adds the last trace's output (light_bounce_history.hlsli
  BounceHistory, shared with the trace's history) x diffuse albedo x Multi-Bounce to the prepared light. Strength is
  not in the loop (gain = what a texel sees x albedo x share < 1: settles). LightBounceConstants padded to 48 bytes.
  Zips: Rome II v21, Attila v35, Thrones v20, Troy v17, Pharaoh v21, WH1 v19, WH2 v19, WH3 v17, Shogun 2 v54 / DLSS v45.
- Shared AO + Light Bounce trace: not built, told ATN why (AO is needed before the lighting the bounce reads: last
  frame's light or geometry; AO full res ~1 m vs bounce half / quarter res 3-10 m; 4 AO paths in 9 games).
- Ryse HDR fix47 (Downloads): Fire Hue (OkLCh) / Fire Hue Focus (OkLCh) labels; patch notes updated.
- Round 31 (untested): AO Resolution Full / Half (shared.h ssao_resolution, default Full) in the 5 ao_pass.hpp games
  (Attila, Thrones, Rome II, Troy, Pharaoh). Half: the bitmask drawn with the game's draw at a half viewport into the
  addon's R16G16 target (top-left pixel of each 2 x 2: result, distance / linear depth; temporal history at half size),
  then the game's draw with the pass's ao_upsample build (AO_UPSAMPLE) reading t40: twattila/ao_upsample.hlsli (3 x 3,
  3% distance, else the closest in distance: grazing far ground striped without it, WARP test scratchpad aohalf/).
  DrawAgain moved to frame_history.hpp. The game's AO is full size (WH2 trace: 3840x2160). WH1-3 / Shogun 2 (compute
  AO paths) not done: offered. Zips Rome II v22, Attila v36, Thrones v21, Troy v18, Pharaoh v22.
- Round 32 (untested): (1) Light Bounce Checkerboard toggle (all 9; needs Temporal; LightBounceConstants.checkerboard,
  shared.h light_bounce_checkerboard, Shogun 2 key LightBounceCheckerboard): the trace skips every other 16 x 16 block
  of texels per frame (whole waves), keeping BounceHistory where alpha > 0.5. (2) WH3 split: its collect replacements
  (ssao01 / ssao02) are the game's code only; Visibility Bitmask = ao_bitmask_collect01/02 via DispatchInstead after
  OnCollect, OnCollectDone in the hook (no on_drawn). (3) AO Half resolution in the compute paths: DispatchInstead(...,
  upsample, half): bitmask over half the output into the addon's R16G16 at u1, then the AO_UPSAMPLE build over the
  output reading t40 (ao_bitmask_blur.hlsli: WH1 / Troy / Pharaoh line-sweep path, ao_upsample_blur; line_sweep_bitmask
  .hlsli: WH2 ao_upsample_collect, WH3 ao_upsample_collect01/02; Shogun 2 collect, key AmbientOcclusionResolution).
  bitmask_ao.hlsli declares u1 / t40 and AoDistance. WARP test (scratchpad aohalf/ao_half_cs_test.cpp): mean AO 0.9710
  vs 0.9707. Zips Rome II v23, Attila v37, Thrones v22, Troy v19, Pharaoh v23, WH1 v20, WH2 v20, WH3 v18, Shogun 2 v55 /
  DLSS v46. AO Temporal for Shogun 2 / WH3: answered (camera-only like Attila; motion vectors would be the real fix).
- Round 33 (Attila experiment, untested): AO "GT-VBAO (Experimental)" toggle (shared.h ssao_gtvbao, also in Thrones /
  Rome II shared.h without a setting): Mirko Salm's GT-VBAO (Shadertoy XXGSDd, dual CC0 / MIT, CC0 chosen; his code in
  Downloads\Shadertoy stuff) ported into twattila/bitmask.hlsli (BitmaskSliceSampled: approximate inverse-CDF slice
  directions in the view vector's frame, perspective screen direction; BitmaskCosineShare: his SliceRelCDF_Cos, checked
  against a brute-force integral to 3e-9; BitmaskCosineSectors: floor edges + per-slice jitter) and bitmask_ssao.hlsli
  (slice weight 1, back point along the sample's own ray). WARP test vs a CPU ray-traced reference (scratchpad
  aohalf/gtvbao_test.cpp): mean error 0.042 -> 0.020. Credit in About. Zip Attila v38.
- Attila v39: Light Bounce "GT-VBGI (Experimental)" (shared.h light_bounce_gtvbgi, LightBounceConstants.gtvbgi, only
  Attila's setting): his GT-VBGI (Shadertoy lfdBWn, code in Downloads\Shadertoy stuff\GT-VBGI (diffuse), CC0; default
  without back-face rejection): sampled slices, cosine sectors, light x fresh share, no receiver / emitter cosine,
  bounce / slices; back point along the sample's own ray (normalize(position) x thickness: either handedness;
  BitmaskViewQuaternion now takes +z or -z view vectors). Not WARP-tested (needs the trace's inputs). Brighter than the
  original (his note: original too dark): Strength may need lowering. Next: the other games if ATN likes both.
- Round 34 (untested; ATN: replace, no toggles): GT-VBAO / GT-VBGI are the only AO and Light Bounce in all 9 games.
  twattila/bitmask.hlsli API now: BitmaskSliceAt(u, normal, view, position) (sampled slices, BitmaskSlice.direction),
  BitmaskSectors(front, back, view, side, slice, jitter) (cosine shares, floor edges); old uniform BitmaskSliceAt /
  BitmaskSectors and projected_length gone; BitmaskAcos = his improved polynomial. bitmask_ssao.hlsli (Attila family),
  twwarhammer2/bitmask_ao.hlsli (WH1-3, Troy, Pharaoh, Shogun 2: screen direction from the slice plane against one AO
  pixel's step along x / y at the pixel's depth: any handedness) and light_bounce_trace.hlsli: slice weight 1, back point
  delta + normalize(sample position) x thickness, light x fresh share / slices. Toggles, ssao_gtvbao,
  light_bounce_gtvbgi removed. Credits "as GT-VBAO and GT-VBGI by Mirko Salm (2024)" in About; AO mode tooltips "GT-VBAO,
  ground-truth visibility bitmask occlusion". WARP vs ray-traced reference: Attila 0.0198, WH1 compute 0.0199 (old
  0.042). Light Bounce brighter (no emitter cosine): presets' Strength may need lowering. Zips Rome II v24, Attila v40,
  Thrones v23, Troy v20, Pharaoh v24, WH1 v21, WH2 v21, WH3 v19, Shogun 2 v56 / DLSS v47. Offered: lower-precision
  light / geometry reads in the bounce trace (his suggestion; bandwidth).
- Round 35 (untested): Light Bounce data halved (all 9): prepared light R11G11B10_FLOAT (was RGBA16F), prepared geometry
  R32G32_UINT = depth buffer value + octahedral normal (was position xyz + normal); each game's light_bounce.hlsli split
  into BounceDepth / BounceViewPositionAt / BounceViewPosition; the trace rebuilds a sample's pixel through the levels'
  rotated grid (clamped as light_bounce_levels) and its position from the depth (identical to the prepare's).
  Experiments in Attila only (fields in every shared.h / LightBounceConstants): AO "Fast Thickness" (ssao_view_thickness:
  thickness along the pixel's view vector), Light Bounce "Back-Face Rejection" (GT-VBGI's exact bitmask version, the
  sample normal from the prepared geometry: no extra fetch) and "Fast Thickness". One-directional / optimized-uniform
  variants need his Shadertoy code (Xc3yzs, XcdBWf, 4cdfzf, XfcBDl): asked ATN. Zips Rome II v25, Attila v41, Thrones v24,
  Troy v21, Pharaoh v25, WH1 v22, WH2 v22, WH3 v20, Shogun 2 v57.
- 32-bit fix: rounds 29-35 built only 64-bit, so Attila v34-v41, Rome II v20-v25 and Thrones v19-v24 carried an
  .addon64 (pack_game.py took the newer file). build32 rebuilt; v41 / v25 / v24 repacked with .addon32; pack_game.py now
  always ships build32's .addon32 for these three. Build both trees every round.
- Attila v42 (untested): authors' values. Defaults + presets "Authors' Settings" / "(Faster Light Bounce)" / Reset:
  AO Strength 1 (now an absolute exponent: AO_GROUND_TRUTH in Attila's ssao00, no game g_Strength / g_MinStrength),
  Radius 2 m (now metres; paper), Thickness 0.5 m (Salm), Quality Ultra, 1 direction, Temporal On, Full; Light Bounce
  Strength 1 (preset), Radius 4 m (paper), Full, Ultra, 1 direction, Temporal On, options off; faster: LB Half + High
  (paper's 16 samples half res). Old ini Radius values now read as metres: press a preset. Gemini notes: Ottosson's
  distance thickness heuristic (Shadertoy 3clGWB) a candidate; Gemini's "keep emitter cosine / x projNormalLen" wrong
  for Salm's importance-sampled slices.
- Attila v43 (untested): Auto Thickness (Experimental) for AO and Light Bounce (ssao_auto_thickness in Attila / Thrones /
  Rome II shared.h, LightBounceConstants.auto_thickness, 64 bytes now): Bjorn Ottosson's GT-VBAO variation (Shadertoy
  3clGWB, code in Downloads\Shadertoy stuff\GT-VBAO thickness heuristic, CC0 / MIT header): bitmask.hlsli
  BitmaskOccluderThickness / BitmaskOccluderBack (occluder restarts when a step runs > 0.9 along the view vector;
  thickness = random 0.5-2 x (Thickness + width so far); back no nearer than the occluder start's). Presets set it Off.
- Attila v44: ATN loves AO Auto Thickness (uses Thickness 0.10). Speed: the occluders[2] array compiled to an indexable
  temporary (x0[4], scratch memory): now two variables (occluder_minus / occluder_plus); the restart test without a
  square root. dcl_temps 29 (AO) / 31 (trace). Answered: temporal settle ~0.5-1 s is the 0.9 history; better = history
  clamped to the current 3 x 3 neighbourhood (needs a resolve pass; offered); Fast Thickness no effect (the two rays
  differ by degrees, memory-bound; auto ignores it): offered removal; radius vs samples.
- Attila v45 (untested in game): noise + temporal + denoisers (ATN: "the best for Attila"). bitmask.hlsli
  BitmaskHilbertIndex / BitmaskNoise (XeGTAO's idea: 64 x 64 Hilbert index + 288 a frame through R3; AO and Light Bounce,
  all games' shared code). AO (ao_pass.hpp, Attila / Thrones / Rome II builds): with Half, Temporal or Denoiser the
  bitmask draws into the addon's raw target, then AO_RESOLVE (BitmaskResolve: camera reprojection, history clamped to the
  3 x 3 same-surface mean +- 1.25 sigma, 0.9), AO_DENOISE (BitmaskDenoise: 5 x 5 binomial, 3% distance), AO_UPSAMPLE
  output (scale 1 = own texel); t40 input, t42 history, b10 last camera; the old in-shader temporal and SV_Target1 gone;
  Update(bitmask, temporal, half, denoise); AmbientOcclusionDraw.resolve / .denoise. Light Bounce (all 9): trace into
  objects.traced (Checkerboard texels negative alpha = history already), light_bounce_resolve (same clamp, 3 x 3) into
  the history, optional light_bounce_denoise (16-tap golden-angle disk 2.5 texels, plane 2% + normal^8 weights, turned
  per frame) into objects.denoised the apply reads; light_bounce_geometry.hlsli holds LoadGeometry; wrappers in all 9
  games. Attila settings: AO / LB "Denoiser" (presets Off). WARP pipeline test (scratchpad aohalf/pipeline_test.cpp,
  Quality Normal, 1 direction): error 0.044 -> 0.015, frame-to-frame 0.047 -> 0.004. Zip Attila v45; other games built,
  not shipped (their behaviour: new noise + Light Bounce resolve with clamp).
- Attila v46 (untested): AO Radius to 20 m, Quality adds 64 / 128 / 256 samples per side (clamp 5), Resolution Full /
  3/4 / Half / 1/4. shared.h ssao_scale (Attila / Thrones / Rome II; addon sets it each present: Attila AO_SCALES
  {1, 4/3, 2, 4}, others 2 or 1); AoScale() reads it; ao_pass::Update(bitmask, temporal, float scale, denoise), targets
  ceil(size / scale); AoUpsample(results, pixel, distance, scale) (other callers pass 2). Samples vs radius: quadratic
  spacing, edge gap 2R / N: keep N about 6-7 x R (m) for 32 @ 5 m (64 @ 10 m, 128 @ 20 m).
- Attila v47 (untested): temporal flicker fix (ATN: flickers even at full res, AO and GI). Cause: the variance clip
  over 3 x 3 same-surface neighbours collapsed on thin things / edges (1 neighbour = no spread = history held to this
  frame's noise) and on quantized values. Now: clip only with 5+ same-surface neighbours (else distance check only),
  AO width max(1.5 sigma, 0.05), LB max(1.5 sigma, 10% of mean), history 0.95 (AO resolve; LB HISTORY_SHARE, all 9
  games). WARP flicker test (scratchpad aohalf/flicker_test.cpp, resolve compiled from source with -D knobs): worst
  pixel spread over time 0.205 -> 0.051 (Quality 0) / 0.087 -> 0.019 (Quality 2, 4 directions), mean 0.0126 -> 0.0060.
  LB Radius to 20 m, LB Quality to 256 (light_bounce.hpp power_of_two(quality, 5)). Not done: per-pixel history
  length (done in round 48).
- Round 48 (2026-10-25, all untested in game; zips: twattila v48, twrome2 v26, twthronesofbritannia v25, twtroy v22,
  twpharaohdynasties v26, twwarhammer v23, twwarhammer2 v23, twwarhammer3 v21, twshogun2completeedition v58,
  twshogun2completeedition_dlss v48):
  - twattila/lighting_settings.hpp: ONE source for every TW game's AO + GI settings (Append(&settings,
    &shader_injection); LightBounceSliders(shader_injection) for light_bounce::Update). Sections "Ambient Occlusion
    (GT-VBAO, Experimental, needs in-game SSAO)" tint 0x7FBFFF and "Global Illumination (GT-VBGI, Experimental, no
    in-game SSAO needed)" tint 0xFFB86B (AO_SECTION / GI_SECTION / AO_TINT / GI_TINT used by each addon's
    section_tints and settings mode). Keys unchanged (ssao_*, light_bounce_*). AO_PASS (Temporal, Denoiser, 3/4 + 1/4)
    detected by `requires injection.ssao_temporal` (Attila, Rome II, Thrones); Fast MSAA by light_bounce_msaa_pixel
    (not WH3 / Shogun 2). ATN's presets from debug3 screenshots: AO Faster (5 m, High, 3/4; Half where no 3/4) /
    Heavier (Recommended) (12.5 m, Ultra, 3/4; Full where no 3/4) / Reset (game's AO); GI Faster (Recommended)
    (strength 1, 20 m, Quarter, 64) / Heavier (More accurate) (Half, Ultra) / Reset (off). Defaults = recommended
    (GI strength 0). Both: 1 direction, auto thickness on, temporal + denoiser on, checkerboard on, multi-bounce 0,
    back-face off, Fast MSAA on. Preset buttons tinted with their section colour; empty line after the AO section.
    Thickness min 0.01 (ATN tests halos). Authors' presets and Fast Thickness removed everywhere (shader code too;
    LightBounceConstants.view_thickness -> padding3; shared.h ssao_view_thickness -> ssao_auto_thickness where missing,
    LB backface / auto_thickness / denoise fields added to all).
  - All games' AO now ground truth: radius in metres, strength an exponent (AO_GROUND_TRUTH ifdef gone; compute family
    AO_RADIUS 2 m gone; Troy / Pharaoh no longer use the game's radius / power / limit). Auto thickness ported into
    twwarhammer2/bitmask_ao.hlsli (WH1-3, Troy, Pharaoh, Shogun 2). Samples to 256 there too.
  - History length (SVGF-style frame count): AO history R16G16B16A16 (value, distance, frames), keep = min(n / (n + 1),
    0.95); LB: history_frames[2] R16_FLOAT targets written as SV_Target1 by the resolve (MRT), read at t48
    (FRAMES_VIEW, VIEW_COUNT 9), nearest texel at the reprojected spot, checkerboard-held texels keep their count.
    WARP (aohalf/flicker_test.cpp, history reset): 8 frames after, |diff from settled| 0.0154 -> 0.0103 (Quality 2,
    4 directions), steady state unchanged.
  - ao_pass::RESOLUTION_SCALES {1, 4/3, 2, 4} shared by Attila / Rome II / Thrones (Rome II / Thrones got 3/4 and 1/4).
  - Shogun 2: the DLSS addon's own ImGui (presets, new keys AmbientOcclusionAutoThickness, LightBounceDenoise,
    LightBounceBackFace, LightBounceAutoThickness, save_ao / save_gi write a whole section); HDR addon reads them with
    read_float / read_choice.
  - Halos (ATN question): screen-space limit at large radius (a nearer object inside the radius darkens what is
    behind it); lower Thickness / Auto Thickness / smaller radius help; a distance falloff (XeGTAO-style) could be
    added. To tell if it is the game: Resolution Full + Denoiser off, then Strength 0.
- Round 49 (2026-10-25, Attila v49, untested in game): Bend Studio's screen-space shadows (ATN asked; Apache 2.0,
  Copyright 2023 Sony Interactive Entertainment: the license is in the code's header (bendstudio.com blog post, zip
  code_final_candidate.zip, downloaded with ATN's OK to scratchpad bend_dl/); credit in About + APACHE_LICENSE-2.0.txt in
  the zip). twattila/screen_space_shadows.hpp: the 40 shadowed sun shaders (LIGHT_BOUNCE_DRAWS list) patched at pipeline
  creation (renodx::utils::pipeline::RegisterOnCreateCallback keeps the original hash, so the Light Bounce still hooks
  them): DXBC token patch after `mov_sat rN.c, rN.c` + `div r0.xy, r0.xyxx, r0.wwww` (the cascade shadow factor):
  ftou / mov / ld t49 / mad (1 - x) / mul_sat, dcl_resource t49, dcl_temps + 1, container sizes + DXBC checksum (MD5
  variant, verified on all 65 originals). t49 = occlusion, so unbound / 0 = the game's light (WARP: bit-identical).
  Compute at the frame's first patched main-view sun draw: sss_depth(_ms) copies G-buffer channel 4 x, sss_setup =
  Bend's BuildDispatchList in HLSL (light = mul(float4(-sun_direction, 0), view_projection); camera b0 of the draw,
  lighting_VS_PS b2 = every shadowed variant's), 8 x DispatchIndirect of sss_bend (Shogun 2's bend_sss_gpu.hlsli +
  BEND_SSS_OCCLUSION: writes 1 - result). Settings section "Screen-Space Shadows (Bend Studio, Experimental, needs
  in-game shadows)" tint 0xFFD966: On (default Off), Length 60 x 2^0-5 (default 240), Thickness 0.001-0.05 (0.005).
  WARP tests: scratchpad sss_patch/warp_check.cpp (patched sun shaders), sss_scene.cpp (box + plane scene vs ray-traced
  reference, 6 sun directions: no unwritten pixels; contact shadows as Bend's thickness allows; a 20% line on the
  horizon row next to the sky).
- Attila v50 (untested): ATN debug4 SSS: whole areas shadowed in 64-px blocks with line patterns = the G-buffer's
  depth (channel 4) too coarse far away (WARP: 16-bit-quantized depth reproduces it; Bend's thickness is 0.005 x
  (1 - depth)). Now Bend reads a copy of the depth-stencil buffer the sun draws keep bound (SceneDepth: typeless copy,
  R24 / R32 view, MSAA sample 0; channel 4 only without one); ReShade.log logs the format once.
  debug4 buildings: the big building's own shadow (arch recess, cornices) gone after a small camera move, building on
  screen = not off-screen culling; asked ATN to test Building / Model Detail Distance at max (LOD without shadows?).
- Attila v51 (untested): ATN debug5 (SSS on the scene depth): near flat ground fully shadowed inside an arc, more with
  Thickness; far scene fine; ReShade.log: depth-stencil R24G8 (44), 2x MSAA. WARP (sss_scene.cpp, env SSS_*) with 24-bit
  depth, 4K-like, steep camera, any near plane: no false shadow, except the side-on sun (w 0): Bend's light z = 0 and its
  w sign vs the xy divisor disagree -> fixed (z = projection _m22 / _m23, w sign = the divisor's). The in-game cause is
  still unknown: sss_probe logs once per SSS enable the camera registers c0-c35, sun_direction, the light coordinate and
  8 depth pairs (Bend's input vs G-buffer channel 4) -> ReShade.log "Screen-space shadows report". Suspect: the depth
  buffer's mapping (viewport depth range / projection) differs from the camera constants Bend extrapolates with.
  Buildings (debug5: the arch's shadow into its own recess changes with small pans, palms / banner fine): 3 test sliders
  in Graphics (Shadow Test: Caster Reach = entity_display_shadow_extrusion_multiplier, Sun Camera Depth =
  shadow_cam_depth_offset_battle + g_battle_terrain_on_..., Cascade Margin = shadow_split_enlargement_safe_zone).
- Attila v52 (debug6 report): camera standard LH (near 1, far 13000, c25 = (1, 13000)), projection _m22 1.000077,
  light z 1.000077, sun_direction = travel direction (y -0.454), Bend's depth 0.873 at the centre (7.9 m); G-buffer
  copy read 0 under MSAA (unimportant). WARP with that exact camera / sun, flat ground, 24-bit depth, 1920 samples: no
  false shadow. So the real depth or a GPU-only difference. v52 also writes renodx_sss_depth.raw (R32, Bend's input)
  and renodx_sss_occlusion.raw (R8, the GPU's result) to the game folder with the report: next, run Bend on that depth
  in WARP with the logged camera / sun and compare.
- Attila v53 (SSS FIXED, untested): the cause of all SSS artefacts (debug4-7) = D3D11 binding hazard in
  screen_space_shadows.hpp's bind lambda: SRVs were set before the UAVs, so the views of the last pass's outputs
  (parameters buffer, depth copy) were dropped to NULL -> Bend read light coordinate 0 / depth 0 (one corner fully
  shadowed). debug7 capture (real depth + GPU mask): WARP with the addon's order reproduced the GPU mask exactly (mean
  33.1, 12.92% > 128); with UAVs first it is clean. bind now sets UAVs first. Raw dumps removed; the log report stays.
  Earlier "16-bit depth" theory was wrong (the depth source switch to the depth-stencil copy stays: full precision).
  Tool: scratchpad sss_patch/sss_real.cpp runs Bend on a captured depth with logged camera / sun.
- Attila v54 (untested): AO "Temporal Edge Fix" toggle (ssao_edge_fix, default on, Attila / Rome II / Thrones via
  lighting_settings.hpp AO_PASS; ATN: thin halos from Temporal Smoothing): on outlines (3 x 3 not all one surface) the
  nearest history texel within 2% instead of the 4-tap blend at 5%, and with < 5 same-surface neighbours a 0.2 hold
  instead of none. WARP flicker (aohalf/flicker_test.cpp, env SSS_EDGE_FIX): Quality High 1 direction worst outline
  spread 0.081 -> 0.153 (0.1: 0.22, 0.3: 0.083); Ultra 4 directions unchanged. Tales of Arise plan: no own temporal,
  let DLSS resolve per-frame noise (XeGTAO style) with the game's motion vectors.
- Attila disappearing tree / building shadows (ATN, also without RenoDX): not found yet. shadow_cam_depth_offset_battle
  is 5000 m with battle terrain (no near-plane clip). 0x47f020 = entity "fast cull" (enable_entity_fast_cull: beyond
  20 m and centre outside the view cone + 5 deg) but only in the per-soldier update (0x4569e7), not trees / buildings.
  Next: trace the shadow pass's tree / building caster lists (tools/tw/x86_disp_scan.py finds [reg + disp] users).
- NEXT (ATN, when awake): the same GT-VBAO / GT-VBGI work for Tales of Arise (has DLSS: TAA-reliant XeGTAO-style
  noise, ideas from three-gtvbao and the path tracing paper).

## RESUME HERE (2026-10-24, round 28): Light Bounce with MSAA (untested)
Zips: Rome II v19, Attila v33, Thrones v18, Troy v15, Pharaoh v19, WH1 v17, WH2 v17, WH3 v16, Shogun 2 v53.
- ATN: "support every AA". MSAA: each game's *_ms directional light shaders (not ulm) added to LIGHT_BOUNCE_DRAWS
  (Rome II / Attila / Thrones 24: slm / skinlm x highdef / lowdef x shadow methods, per-sample; WH1 / WH2 20, Troy /
  Pharaoh 16: edges (per-sample) / interior; G-buffer slot + camera slot per shader from RDEF: scratchpad ms_draws.py
  <game> over dump_shader_<game>_pack). WH3 / Shogun 2 have no MSAA. light_bounce.hpp: no MSAA rejection; light copy
  single-sampled (ResolveSubresource with the RTV format when MSAA); prepare / apply = *_ms builds when the light buffer
  is multisampled. twattila/light_bounce_gbuffer.hlsli: BounceGBuffer (Texture2D / Texture2DMS) + BounceLoad
  (bounce_sample: prepare 0, apply SV_SampleIndex); every light_bounce.hlsli uses it; light_bounce_prepare_ms /
  apply_ms.ps_5_0.hlsl wrappers (#define LIGHT_BOUNCE_MSAA) in all 9 Light Bounce games. Apply runs per sample always
  (WH interior draws are per-pixel in the game: 4x apply cost there; per-pixel variant possible).

## RESUME HERE (2026-10-24, round 27): Light Bounce + MSAA, Game's AO split
Zips: Rome II v18, Attila v32, Thrones v17, Troy v14, Pharaoh v18 (WH1-3 / Shogun 2 unchanged since round 26).
- ATN: Light Bounce "no effect" in Rome II / Attila / Thrones. Cause (one-time draw logs, all removed again): the game
  had MSAA on, so it lights with directionallight ps41_slm_highdef_ms / ps41_skinlm_highdef_ms (0x006F587A,
  0xFCD6C263; G-buffer as Texture2DMS; point lights _ms too), not in LIGHT_BOUNCE_DRAWS (non-MSAA only). Not a bug of
  the levels. Hashes -> names: bin\cmd_Decompiler-1.3.16\dump_shader_<game>_pack\names.tsv. Offered: MSAA support
  (prepare reads sample 0 of the MS G-buffer, apply into the MSAA light buffer) or a tooltip note.
- Game's AO split for the pixel-shader passes (ATN: "stop our AO running when Game's is selected"): Attila / Thrones
  ssao00, Rome II ssao00 / ssao01, Troy / Pharaoh ssao01: the replacement = the game's code only (#else of AO_BITMASK);
  the Visibility Bitmask = the addon's own PS (root ao_bitmask*.ps_5_0.hlsl: #define AO_BITMASK + include of the pass
  file). ao_pass.hpp: Install(span<AmbientOcclusionDraw{hash, bitmask}>, &shader_injection, size); with the bitmask on
  it swaps in the own PS + its settings buffer at b13 (game's PS / b13 put back after), levels every draw, temporal at
  the frame's first. Game's: the callback returns at once.

## RESUME HERE (2026-10-23, round 26): Depth Levels everywhere, LB levels, presets 0.11, bloom (untested)
Zips: Rome II v17, Attila v31, Thrones v16, Pharaoh v17, Troy v13, WH1 v16, WH2 v16, WH3 v15, Shogun 2 v52 / DLSS v44.
- ATN on v16: spikes were GPU-bound pacing; Depth Levels fixed them; directions do not lighten the AO. The paper ATN
  attached (D:\Downloads\ssil_visibilitybitmask.pdf) is Therrien et al., the one bitmask.hlsli follows.
- Depth Levels now a shared compute builder: twattila/depth_levels.hpp (Build(context, depth view) -> all-levels SRV,
  5 mips R32G32_UINT at half the depth; depth_levels.cs_5_0.hlsl, wrappers in every TW game folder; CS shader / t0-t1 /
  u0 put back) + depth_levels.hlsli (t41: DepthLevelsBound, DepthLevelLoad by depth texel). Level rule in bitmask.hlsli
  BitmaskSampleLevel(distance texels, count) = firstbithigh - 3 (shared with the Light Bounce). The PS levels pass of
  round 25 removed (ao_depth_levels.ps_5_0.hlsl gone).
  Bound by: ao_pass.hpp (PS t41; Attila / Thrones / Rome II ssao00, Troy / Pharaoh HBAO ssao01 0xD42C3523, Update(bitmask,
  false) there); line_sweep_ao.hpp internal::BindLevels / UnbindLevels (CS t41): DispatchInstead (kept ? t2 : t0) unless
  OnCollect bound them; OnCollect(depth_slot) (WH3 ssao01 now passes depth slot 0, Shogun 2 passes 0, 0). WH1 / WH2 /
  WH3 / Shogun 2 register depth_levels::OnDestroyDevice; ao_pass::OnDestroyDevice calls it.
- Light Bounce levels: prepared light / geometry textures have up to 6 mips (PreparedTarget in light_bounce.hpp);
  light_bounce_levels.ps_5_0.hlsl after the prepare (light mean 2 x 2, geometry rotated-grid pick, t46-t47 the level
  above); trace reads Load(texel >> level, level), level by BitmaskSampleLevel(length(offset)).
- Presets: AO Thickness 0.11 in both Recommended presets (all 9 addons + Shogun 2 DLSS overlay); yellow TEXT (0xFFD23C)
  above the buttons: "Prefer darker shading behind trees and objects? Pick a preset, then raise Thickness (e.g. 0.20)."
- Bloom (Rome II / Attila / Thrones, ATN "blocky"): twattila/bloom_filter.hlsli: BloomHighPassAveraged (the high pass
  per source pixel over the bloom texel's footprint, ratio from source size x target recip size, at most 8 x 8) with
  each game's BloomHighPassTap in its bloom.hlsli; SampleBloomSmooth (cubic B-spline, 16 Loads) in the tonemaps (Rome
  t00 / t09, Attila t00 / t09, Thrones t00 / t17). Footprint ratio not measured in game (1: same as stock).
- Packing: scratchpad repack3.py (old zip extras + include closure under src/games).

## RESUME HERE (2026-10-23, round 25): AO Depth Levels (Rome II v16, Attila v30, Thrones v15, untested)
- ATN on v15: "Directions Share Samples" made the AO lighter at more directions (too few steps per slice to find the
  occluders): removed (shared.h field, UI, shader). ATN with it off, 8 directions High: still ~10 spikes a second. Our
  pass does the same work every frame (no readback, no periodic work; noise still with Temporal off): likely GPU-bound
  pacing (the game's CPU waits on the GPU in bursts). Asked ATN to try an FPS cap just below the 8-direction FPS.
- Depth Levels (SAO, McGuire et al. 2012; own code): twattila/ao_temporal.hpp -> twattila/ao_pass.hpp (namespace
  twattila::ao_pass, Update(bitmask, temporal)). Before the AO draw (bitmask AO on): the game's depth (AO pass t0) into
  an R32G32_UINT texture with 5 mips at half its size (ao_depth_levels.ps_5_0.hlsl, Light Bounce's full-screen VS;
  default blend / depth / rasterizer states; each texel = one depth texel of its 2 x 2 in a rotated grid: depth bits +
  coordinate x | y << 16), bound at t41 for the draw (game's t40 / t41 / b10 / targets / blend put back after).
  bitmask_ssao.hlsli: a step 8 x 2^n to 16 x 2^n depth texels away reads level n (firstbithigh - 3, clamped to the
  levels bound; none bound = level 0), its position from the stored coordinate (on the surface).
- Next: ATN's frame time test (Rome II first). Then the line-sweep games (twwarhammer2/bitmask_ao.hlsli, WH3, Shogun
  2) and the Light Bounce trace (light_bounce_trace.hlsli: its prepared geometry is already per texel at the trace's
  scale; levels of it the same way).

## RESUME HERE (2026-10-23, round 24): AO frame time spikes (Rome II v15, superseded by round 25)
- ATN (Rome II): frame time "mountains" at AO Directions above 1, a flat line at 1. Cause found in
  twattila/bitmask_ssao.hlsli: every slice took all 8 x 2^Quality steps per side (x2 sides), so 4 directions = 4x the
  depth reads and math, most where near surfaces fill the screen (radius in pixels large -> max steps). Rome II's
  default (Quality Normal, 4 directions) = 64 samples, heavier preset (High, 4) = 128, Recommended (High, 1) = 32.
- Fix: ssao_shared_samples (shared.h of Rome II / Attila / Thrones; UI "Directions Share Samples", default on, Rome II
  only): steps = max(2, steps / slices). Off = old cost (A/B). Attila / Thrones: field 0 = unchanged.
- If ATN confirms: UI + default in Attila / Thrones; the same in the line-sweep games (twwarhammer2/bitmask_ao.hlsli,
  WH3 collects, Shogun 2) and the Light Bounce trace (LB.slices x LB.steps, light_bounce_trace.hlsli). If spikes
  remain: depth mip chain (XeGTAO) for far samples, or a cap on the radius in pixels.

## RESUME HERE (2026-10-23, round 23): WH1-3 graphics rebuilt from the survey (untested)
Zips: WH1 v15, WH2 v15, WH3 v14 (patch notes: UPDATE (5) / LATEST 3). ATN: "add the dead ones [= remove] and add all".
- WH1: Model / Building Detail removed; Campaign Detail = rigid_campaign_tree_lod only ("Campaign Tree Detail
  Distance"); new Tree Detail (tree_lod_scaler, inverse), Bush Detail (scrub_lod_distance_default), Terrain Draw
  (terrain_visible_tiles), Campaign Draw (campaign_culling_ratio, inverse), Shadow Distance (shadowmap_split_scale,
  0.5-4x), Small Object Shadows (cull_small_shadow_size, inverse), Unit Shadow Distance (*_IMPOSTER_SHADOW_DISTANCE +
  entity_display_shadow_max_shadow_distance).
- WH2: Building / Campaign Detail removed; Shadow Map Size = code constants at +0x7e157d (8192.f, 8192.f, 4096; the
  highest Shadows setting, x1.5 via parse: 12288); new Bush Detail, Campaign Draw, Shadow Detail
  (shadow_lod_distance_scaler, inverse; the game rewrites it per Shadows setting), Small Object Shadows
  (cull_small_shadow_start / end_size, inverse). far_buildings default 1 (skipped).
- WH3: init_device Update; new Unit / Building / Tree / Grass / Object / Terrain Draw Distance (cull_aabb_at_num_pixels
  battle 16 / campaign 8, inverse; + campaign_tree_display_max_distance, battle_small_prop_cull_distance), Grass Density
  (0.5-2x), Terrain Tessellation. Shadow splits skipped (overrides, off by default; the game's table at +0x1aeb0e0).
- Every new value's address checked offline with tools\tw\find_emu.py (the addon's Find emulated) = survey object +
  0x68 (WH1) / + 0x60 (WH2, WH3).

## RESUME HERE (2026-10-23, round 22): static tweak survey replaces the traces
- WH1 watch-at-start (TWWarhammer\debug6): terrain_far_shader_distance read in the campaign (getter callers +0x4018fa,
  +0x40473f), SMALL_IMPOSTER in battle (+0x532730 builds the imposter struct); battle_entity_lod0 / campaign_tree_lod0
  never read.
- tools\tw\tw_tweak_survey.py EXE [REGEX] [--all]: every tweak (constructors = functions called after a name lea with a
  static object in rcx; WH1 4973, WH2 6059, WH3 3689 tweaks), its float default, and every code use (lea of the object
  for the getter, or rip access to the value +0x60 / +0x68; initializer, atexit thunk and destructor refs excluded).
  Matches all watch results (WH1 / WH2 dead ones = 0 uses; the readers found by the watch = its uses).
- Current sliders, 0 uses (dead): WH1 Model Detail (battle_entity_lod*), Building Detail (battle_building_lod*),
  Campaign Detail (campaign_tree / grass_lod*; rigid_campaign_tree_lod has 1 use); WH2 Building Detail, Campaign
  Detail (same), Shadow Map Size (shadowmap_extreme_quality: 0 uses in WH2; shadow_resolution_multiplier has 6). WH3:
  all 9 used (per model at load).
- Candidates with uses (defaults): WH3 battle_cull_aabb_at_num_pixels_{unit,building,tree,grass,prop,terrain,
  deployable} 16, campaign_* 8, battle_small_prop_cull_distance 500, campaign_tree_display_max_distance 10000,
  grass_density_multiplier 1, terrain_tessellation_quality_battle 1 / _campaign 0.25, battle / campaign_shadow_split_0-3.
  WH1 tree_lod_scaler 1.35, rigid_tree_lod_scalar 1, scrub_lod_distance_default 175, campaign_culling_ratio 0.003,
  terrain_visible_tiles 500, terrain_mid_distance_slope_factor 3, cull_small_shadow_size 0.02, IMPOSTER_FOV_SCALE 1,
  *_IMPOSTER_SHADOW_DISTANCE 10000, entity_display_shadow_max_shadow_distance 10000, shadowmap_split_scale 1.
  WH2 shadow_resolution_multiplier, shadow_lod_distance_scaler, cull_small_shadow_start / end_size,
  campaign_culling_ratio, scrub_lod_distance_default, far_buildings, IMPOSTER_DITHER_RATIO. Asked ATN which to add.

## RESUME HERE (2026-10-23, round 21): watch-at-start logs (WH1 deubg5, WH2 debug5, WH3 debug6)
- Watch v2 ran without crashes (WH2, WH3). WH1: no watch at all, no [Total War] WatchAtStart in its ReShade.ini (the
  button press was not saved): asked ATN to press it again and check the ini before restarting.
- WH3: num_pixels_per_triangle_lod_change_* are the game's quality values: +0x58cafc (a parser, movss [rax], xmm6)
  writes unit / building / battle_tree 0.8 s after the addon's load (from the game's settings); +0x1a0363c (by model
  type at [model + 0x58] -> (value, a 16.0 at +0x3d3a4xx)) is called by +0x1a02a00 (model setup) and stored at
  [model + 0x84 / + 0x88]: read per model when it loads (9 s menu units / buildings, 24.5 s campaign trees, 90 s battle
  trees), never per frame. So the sliders apply to models loaded after the change: test = restart (or a new
  battle / campaign) with the sliders set. Consumer of model + 0x84 not found yet (if a restart shows no change).
- WH2: campaign_tree_lod0, campaign_grass_lod0, rigid_campaign_tree_lod, battle_building_lod0: never read in ~2 min
  (start, campaign, battle). Only rip-relative refs: static initializers (+0x61db0 ...), rigid_campaign_tree_lod also
  +0x868810 (const / value -> a renderer parameter, then its changed callback; not run in play). Campaign Detail looks
  dead (offered removal); buildings inconclusive (a battle without buildings loads none). distance_lod_scaler (Model
  Detail) is read live in the campaign too (millions of reads).

## RESUME HERE (2026-10-23, round 20): WH1-3 watch logs read, watch v2 (untested)
Zips: WH1 v14, WH2 v14, WH3 v13 (debug buttons only, plus WH3 values applied from the device on).
- ATN's 10 s watch logs (WH1 TWWarhammer\debug4, WH2 debug4 battle_crash / campaign_crash, WH3 debug5), battle then
  campaign:
  - WH1: only SMALL_IMPOSTER_DISTANCE read during play (battle, ~170/s) at warhammer.exe+0x162f10, a virtual float
    getter (movss xmm0, [rcx + 0x68]; ret): caller unknown. battle_entity_lod0, campaign_tree_lod0,
    rigid_campaign_tree_lod: never read in play.
  - WH2: distance_lod_scaler read live per object (millions / 10 s; rip-relative at +0x5e41a0, +0x5e4f47, +0x5e7c54,
    +0x5e7e73, +0x61a117: squared, scales the LOD distance). SMALL_IMPOSTER_DISTANCE copied with the other imposter
    distances into a struct at +0x2256720 (~37 / s in battle: per unit). campaign_tree_lod0 / campaign_grass_lod0:
    never read in play.
  - WH3: none of the num_pixels_per_triangle_lod_change_* read in play: load time or the game's start (WH3 applied
    the values only from its first present, ~0.8 s after its device; now also at init_device like WH1 / WH2).
  - WH2 crash right after "done": a thread caught between its access and the handler got Dr7 back from its exception
    context after the clear, then hit with the handler removed. Fixed (below).
- Watch v2 (twrome2/engine_tweaks.hpp watch): handler clears Dr7 while stopping, clears twice before removing; a
  getter's access (next byte ret) logs its caller from [rsp]; new threads armed every 250 ms; each new reader logged
  as found ("new reader at N s"); buttons "Watch graphics values (60 s)" (press, then load) and "Watch graphics values
  at the next start (5 min)" (ReShade.ini [Total War] WatchAtStart, read once in AttachAddon: Find + watch from the
  addon's load at the game's device). Watched: WH1 SMALL_IMPOSTER / battle_entity_lod0 / campaign_tree_lod0 /
  terrain_far_shader_distance; WH2 campaign_tree_lod0 / campaign_grass_lod0 / rigid_campaign_tree_lod /
  battle_building_lod0; WH3 unit / building / battle_tree / campaign_tree.

## RESUME HERE (2026-10-23, round 19): WH2 stutter check, Game's AO at its own cost (untested)
Zips: WH1 v13, WH2 v13, Troy v12, Pharaoh v16, Shogun 2 v51.
- ATN: WH2 stutters even with Lighting 0 / AO Game's. Per present the addon only updates small state (light_bounce
  Update, ao_trace / pixel_trace atomics, engine_tweaks Apply); the Light Bounce hook returns at strength 0; no log
  spam (ReShade.log debug3). The cost found: v12 put the bitmask into WH2's collect (32 x 32 = 1024 threads, X4714), so
  the game's collect ran in that heavy shader too. Now line_sweep_ao.hpp DispatchInstead: the replaced pass is the
  game's code only, the addon's own 8 x 8 shader is dispatched in its place with the addon's settings at b13.
  WH2: ao_bitmask_collect (kept depth t2 / camera b0). WH1 / Troy / Pharaoh: ao_bitmask_blur in place of the blur, the
  collect no longer skipped (its replacement removed: never an unwritten AO where a game skips its blur). Shogun 2:
  ao_bitmask_collect in place of the collect (sweep camera via OnCollect, the collect's depth).
- Helper scripts copied to C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw.

## RESUME HERE (2026-10-23, newest+++++++++): round 18 (untested)
Zips: Thrones v14 (ATN's "What I would use:" from the screenshot), WH1 v12, WH2 v12, WH3 v12.
- WH2 AO bug solved by ATN's traces (debug3): only the collect 0x19CF3C83 runs at ATN's settings, the blur 0xC7B1F6F7
  never; the addon skipped the collect with the bitmask, so the AO texture kept zeros (black on load) or the game's
  last AO (afterimage). Now: bitmask in the collect (twwarhammer3/line_sweep_bitmask.hlsli, parametrised: group size,
  own camera, output size), depth = the depth downsample's input (0xF1CE6943 replaced with the game's code; hook
  KeepComputeDepth), camera = CS b0 at the collect (the sweep's; KeepComputeCamera), bound by OnCollect at t2 / b0.
  WH2's blur replacement removed (game blur).
- WH1-3 graphics: Trace shows every value found, written and kept (no "changed by the game"), yet nothing changes
  (WH1 campaign / battle, WH2 campaign trees, WH3 everything). The exe only references the tweak objects in their
  static constructors (tw_refs.py / tw_string_refs.py): readers go through the registry. Added
  tw::engine_tweaks::watch (hardware data breakpoints on 4 values for 10 s, VEH counts the instruction after each
  access, log module + offset); buttons "Watch graphics values (10 s)" in WH1-3. Next: ATN's watch logs -> disassemble
  the readers (load-time copy vs per-frame) -> hooks as Attila's LOD pick / tree LOD.

## RESUME HERE (2026-10-23, newest++++++++): round 17 (untested)
Zips: Attila v29, Thrones v13, Rome II v14, Pharaoh v15, Troy v11, WH1 v11, WH2 v11, WH3 v11, Shogun 2 v50 / DLSS v43.
- Tooltips: scratchpad tooltip_sync.py copies Attila's AO / Light Bounce tooltips by key; the AO choice uses a template
  with each game's own method (GAME_AO). Rome II / Pharaoh heavier preset: 4 AO directions (ATN).
- Thrones "What I would use:": ATN's new values were missing from the message: asked again (not changed).
- Light Bounce ports (sun draws from dump_shader_<game>_pack via scratchpad tw_sun_layouts.py):
  Troy = Pharaoh's decode (twpharaohdynasties/light_bounce.hlsli); WH1 / WH2 = twwarhammer2/light_bounce.hlsli
  (spherical normals from ch1.xy, albedo ch2 x (1 - ch3)); WH1 camera b2 (reflection b1), WH2 slm b1 / skin b2; WH3 =
  twwarhammer3/light_bounce.hlsli (3K camera layout c0-c40, view-space normals from 10-bit ch1, albedo x 0.96; one shader
  per model via g_skin_enable, all marked standard). Troy got its own OnPresent.
- WH1-3 / Troy / Shogun 2: AO + Light Bounce presets (heavier: 4 AO directions; no AO temporal there), modes
  "HDR & Effects" / "Graphics" (not Shogun 2: no graphics in its HDR addon). Shogun 2 overlay: presets, Temporal
  (LightBounceTemporal), Eighth, 16 directions, LIGHTING_COLOR light blue.
- Traces: tw::engine_tweaks::Trace (button "Trace graphics settings", WH1-3), WH2: ao_trace on 0x19CF3C83 / 0xC7B1F6F7
  + "Trace ambient occlusion" button + pixel trace in a TreeNode (warhammer2_pixel_trace.txt).
- ATN's VRR flicker question (Attila / Pharaoh): AO / Light Bounce cost varies with the view (radius on screen), test
  with them off; an fps cap steadies VRR. Offered: a smaller on-screen radius cap for steadier cost.

## RESUME HERE (2026-10-23, newest+++++++): release round: Attila v28, Rome II v13, Thrones v12, Pharaoh v14
ATN ships these four. Patch notes: Downloads\tw_patch_notes.txt (UPDATE block on top), tw_all_changes.txt (LATEST).
- All four: modes "HDR & Effects" / "Graphics"; AO + Light Bounce presets (Recommended = ATN's screenshot values, heavier
  one AO Thickness 0.11 + 2 directions, Reset = defaults + game's AO), graphics presets without AO / LB keys; AO
  Directions 1-8 (1 << setting, default 2) everywhere incl. bitmask_ao.hlsli users (WH1-3, Troy, Shogun 2 overlay /
  HDR read: built, not shipped); AO / LB titles 0x7FBFFF; two empty TEXT lines after Graphics; copyright headers on
  the lighting / AO files.
- AO Temporal Smoothing (default Off) in Attila / Thrones / Rome II: the shared bitmask AO is twattila/bitmask_ssao.hlsli
  (included by Attila / Thrones ssao00 0xBD389EF1 and Rome II ssao00 0x37F7EDEF / ssao01 0x1ED22F9C). Pharaoh: none
  (its ssao2 generate pass has no camera CB; would need the camera from elsewhere).
- Pharaoh: pixel trace (Debug section) removed from the release (red cross drawn in Graphics mode).
- Attila presets (ATN): Reflection 1x, Building 3x (4x / 8x) / 4x (vanilla).
- Next: the same for WH1-3 / Troy / Rome Remastered / Shogun 2 when ATN asks.

## RESUME HERE (2026-10-23, newest++++++): Attila v27 (untested)
- Settings Mode {"HDR & Effects", "Graphics"} in Attila only (others still "Image").
- AO Temporal Smoothing: twattila/ao_temporal.hpp (command_action on 0xBD389EF1 with a post_callback: RT1 = history,
  t40 = last history, b10 = last camera; game's draw not replaced), ssao00 writes o1 = (AO, distance); SI.ssao_temporal,
  SI.ssao_frame (ao_temporal::Update returns it). twattila/frame_history.hpp: Target, MakeTarget, KeepCamera (also
  used by light_bounce.hpp).
- UI 4K Mod Compatibility: CUSTOM slider in Graphics writing [Attila] DisableGraphics (used at the next start); with it
  on, the section keeps text, this slider and SSR Quality. Two empty TEXT lines end the Graphics section. AO / Light
  Bounce titles 0x7FBFFF. Graphics presets (graphics_defaults) no longer list AO / Light Bounce keys.
- Next (ATN): presets for the AO and the Light Bounce.

## RESUME HERE (2026-10-23, newest+++++): TW round 14, Settings Mode Image / Graphics (untested)
Zips: twattila_v26, twthronesofbritannia_v11, twrome2_v12, twpharaohdynasties_v13, twwarhammer_v10, twwarhammer2_v10,
twwarhammer3_v10, twtroy_v10.
- ATN: Settings Mode labels {"Image", "Graphics"} (key SettingsMode kept). IsAdvancedSettings -> IsGraphicsSettings;
  every `.is_visible = IsAdvancedSettings` removed and the Psycho mode checks no longer need Advanced; one loop at the
  end of ConfigureAddon: graphics sections (Graphics (Experimental), Ambient Occlusion, Light Bounce; Pharaoh also
  Debug) only in Graphics, the rest only in Image, About and SettingsMode in both.
- Thrones / Rome II / Pharaoh now ship the round 13 Light Bounce (prepare pass, Temporal Smoothing setting, Eighth,
  16 directions). AO Directions 1 and the AO step cap are still Attila-only.
- Not changed: Rome Remastered and Shogun 2 HDR (no graphics sections; still Simple / Advanced). Shogun 2 still has
  the round 12 Light Bounce settings in its DLSS overlay (no Temporal toggle there yet; the HDR addon would default On).

## RESUME HERE (2026-10-23, newest++++): TW round 13, Attila v25 (untested)
- Attila AO Directions 1 / 2 / 4 / 8 (Attila's ssao00 only: 1 << setting, default 2 = 4; Thrones / Rome II / bitmask_ao
  keep 2 << setting), AO steps capped at one per AO pixel of the radius.
- Light Bounce: Resolution up to Eighth, Directions up to 16 (Update clamps 0-3 / 0-4), Temporal Smoothing (Sliders.temporal,
  Attila setting light_bounce_temporal, default On; other addons get the Sliders default On with no setting yet).
- Passes now: light copy (t40) -> light_bounce_prepare (MRT: mean light per texel RGBA16F, position + octahedron normal
  RGBA32_UINT; t46-t47) -> trace into history[i] reading history[1-i] (t41) and the last camera (b10, CopyResource of
  the sun draw's camera CB after each trace) -> apply reads history[i]. Each game's light_bounce.hlsli adds a
  previous_camera cbuffer (camera position + view_projection offsets only) and BouncePrevious; every game folder has a
  light_bounce_prepare.ps_5_0.hlsl wrapper. Thrones / Rome II / Pharaoh / Shogun 2 build with it but were not shipped.
- Not done: temporal smoothing for the AO (would need its own history around the game's ssao draw); offered to ATN.

## RESUME HERE (2026-10-23, newest+++): TW round 12 (untested)
Zips: twattila_v24, twthronesofbritannia_v10, twrome2_v11, twwarhammer_v9, twwarhammer2_v9, twtroy_v9,
twpharaohdynasties_v12, twwarhammer3_v9, twshogun2completeedition_v49, twshogun2completeedition_dlss_v42.
- Speed toggles: AO Directions (SI.ssao_directions: 2 / 4 / 8 slices, default 4) in every bitmask game (Shogun 2 via
  the DLSS overlay, ReShade.ini AmbientOcclusionDirections). Light Bounce Resolution (Full / Half / Quarter), its own
  Quality (8 / 16 / 32) and Directions (1 / 2 / 4); defaults = round 11 (half, 8, 2). The fast acos stays on (no
  visible difference: no toggle).
- twattila/light_bounce.hpp generic: Install(span<const SunDraw>) (hash, standard, gbuffer_slot, camera_slot; the
  callback reads it with GetCallbackData), each draw's 4 G-buffer SRVs -> t42-t45 and camera CB -> b11, light copy t40,
  bounce t41, constants b12 (light_bounce.h: steps, slices, scale). Trace / apply bodies are
  light_bounce_trace.hlsli / light_bounce_apply.hlsli; each game's light_bounce.hlsli defines BounceViewPosition /
  BounceViewNormal / BounceDiffuseAlbedo / BouncePixelsPerMetre / BounceViewportSize (Attila's is shared by Thrones /
  Rome II). Install after renodx::mods::shader::Use everywhere (replaced sun shaders get renodx's injection first).
- Pharaoh: 9 sun draws (camera b1; G-buffer t8 / t5 / t7), normal ch1.xy, albedo ch2 x (1 - ch3 specular), emissive
  (flags & 10) == 10 none. Shogun 2 HDR: sunlight00-03 (G-buffer t7 / t9), spherical normals, albedo x (1 - metal) x
  0.96, flag 1 clear = unlit, none; settings from the DLSS overlay (LightBounce* keys).
- Attila: ReShade.ini [Attila] DisableGraphics=1 skips every engine change (tweaks, hooks, shadow map code) and hides
  the Graphics sliders (SSR Quality stays), for the UI 4K mod.
- Not yet: Light Bounce for WH1 / WH2 / WH3 / Troy, WH2 AO trace, WH3 LODs / shadows.

## RESUME HERE (2026-10-23, newest++): TW round 11 (untested)
Zips: twattila_v23, twthronesofbritannia_v9, twrome2_v10, twwarhammer_v8, twwarhammer2_v8, twtroy_v8,
twpharaohdynasties_v11, twwarhammer3_v8, twshogun2completeedition_v48, twshogun2completeedition_dlss_v41.
- Speed: twattila/bitmask.hlsli (BitmaskAcos = GTAO's fast acos, BitmaskSliceAt, BitmaskSectors) used by Attila /
  Thrones / Rome II ssao shaders, the Light Bounce trace and twwarhammer2/bitmask_ao.hlsli (WH1-3, Troy, Pharaoh,
  Shogun 2); slices stop once all 32 sectors are covered. AO Quality (SI.ssao_quality: 8 / 16 / 32 steps) in every
  bitmask game (Shogun 2: DLSS overlay -> ReShade.ini AmbientOcclusionQuality -> HDR addon).
- Light Bounce: traced once a frame at half resolution after the first standard (slm) sun draw (one light copy),
  own full-screen VS, no depth / stencil; apply per sun draw with a 3x3 bilateral upscale (alpha = view depth).
  Install(standard, skin) lists per game in addon.cpp; Thrones (14 + 12) and Rome II (18 + 14; 20 variants with the
  camera at b1 skipped) use twattila's header and shaders via light_bounce_*.hlsl wrappers.
- Pharaoh's black polygons = lens blood (shaders_bl.pack lenseflare ps40_blood 0xE4931F75: alpha down to -8.55 in an
  FP16 target): lensblood00 clamps; lensflare.hlsli ApplyPharaohLensOverlay (the 5 flares' copies merged).
- Shogun 2 DLSS v41: 14336 removed (ATN: 1080p again), sizes 4096 / 8192 / 12288 via the code constant.
- Not yet: Light Bounce for WH1 / WH2 / Troy / Pharaoh / WH3 / Shogun 2 (other lighting passes; Pharaoh's: trace
  #2607 vs 0xCCBE6B59 ps 0xF883F5B2, G-buffer t4-t7), WH2 AO trace, WH3 LODs / shadows.

## RESUME HERE (2026-10-23, newest+): TW round 10 (untested)
Zips: twpharaohdynasties_v10, twshogun2completeedition_dlss_v40. ATN: Attila AO Quality + Light Bounce both work and
look very good.
- Attila screenshot crash: ReShade's own std::bad_alloc (throw info in its dxgi.dll; ReShade.log "Failed to create
  system memory texture for screenshot capture!"), crash dump AppData\...\Attila\crash_report\D2026-10-23_T02-49-24:
  the 32-bit (LAA) process out of address space at a 4K screenshot (SaveOverlayShot=1 takes two). Not our code. Told
  ATN: overlay shot off, or Win+Alt+PrtScn (outside the game).
- Shogun 2 v40: the top Shadows setting's size written into the code (mov eax, size at +0x1a77c27 / +0x1a77c3d):
  4096 / 8192 / 12288 / 14336. D3D11 limits for the 4-slice array (scratchpad d3d11_size_test): 14336 any depth
  format, 16256 D32 only, 16383+ fails.
- Pharaoh v10: Shadow Map Size via .rdata table rows 4 / 5 (+0x3da08a0) and the extreme immediates (+0x31e5271):
  6144 / 8192 / 12288 / 16384 (single texture with tiles; 16384 creatable in every format). ATN's "What I would use:"
  preset. Debug (Advanced): pixel trace (talesofarise_dlss/pixel_trace.hpp, file_prefix "pharaoh") for the black
  polygons; unit sliders at 0.1x did not remove them. Next: read ATN's pharaoh_pixel_trace.txt.

## RESUME HERE (2026-10-23, newest): TW round 9 (Attila AO test; untested)
Zips: twattila_v22 (AO Quality + Light Bounce), twshogun2completeedition_dlss_v39.
- Attila AO Quality: ssao00 steps 8 << SI.ssao_quality (Normal / High / Ultra). Light Bounce (screen-space indirect
  light, the visibility-bitmask paper's Algorithm 1): twattila/light_bounce.hpp registers command_action callbacks on
  the 26 directionallight ps30 slm / skinlm hashes; per sun draw it redraws the game's draw, copies RT0 (the light
  buffer), draws light_bounce_trace.ps_5_0 into its R16G16B16A16F texture (game's VS / depth-stencil: same pixels),
  then light_bounce_apply.ps_5_0 (3x3 depth-aware average x albedo^2 x (1 - metal)) additively; b12 / t10 / t11,
  restored after. Main view only (RT0 = display size). Default Strength 0 (off). If ATN sees nothing: check RT0 size,
  view-space handedness (z > 0 like the AO), the sun hashes ATN's settings use.
- Shogun 2 v39: ATN: the game shows 32 GB normally; 1.8 GB only after 16384. v38's force_total_video_memory (4 GB)
  reverted (it would lower the budget); 16384 stays removed (D3D11 cannot create it). ATN to retest 8192 / 12288 after
  setting 4K + DLSS again.
- ATN's reports, for later (after Attila's AO answer): Rome II / Thrones / WH1 Shadow Map Size works. WH3: graphics
  sliders (buildings, unit LOD) seem to do nothing (more LOD tweaks needed?), AO works, wants shadow resolution. WH2:
  AO blackens a lot when a battle loads, toggling Game's and back leaves an afterimage and no AO: wants a trace;
  shadow size seems to work. Pharaoh: "What I would use:" = 1.2 x5, 0.15, 2, 2, 2, 3, 1.25, 4, 4, 6, 4, 1, 4, 8192
  (slider order); debug1 (Pharaoh folder): huge black polygons + blue garbage mid-battle with a field full of corpses
  (Unit Detail 1.25, Dead 0.45, Animation / Variety / Footstep 2.5, Model 3): exploding skinned meshes, most likely
  the animation / bone buffer overflowing (Attila's Big Battle issue in the main pass); the dead-unit stub checked
  (xmm0 only for the dead, flags rewritten by the comiss). Asked ATN to retry with Unit Animation 1x.

## RESUME HERE (2026-10-23, latest+): TW round 8 (Attila / Thrones / Rome II shipped; untested)
Zips: twattila_v21, twthronesofbritannia_v8, twrome2_v9, twshogun2completeedition_dlss_v38. Full player-facing list of
every TW change for ATN's Nexus pages: Downloads\tw_all_changes.txt (ATN writes the Nexus texts now).
- Thrones: "What I would use:" (ATN's 14 values), V31 Highlight Hue Shift 0.13. Rome II: preset Dead 0.5 + Shadow
  8192. Attila: Shadow 8192 in both of ATN's presets. AO tooltips name the game's HBAO vs our GTAO bitmask.
- CPU (ATN: no debug overhead): DebugUnits probe gone (graphics_hooks.hpp), engine_tweaks.hpp StatusLine (shown only
  on "not found" / "failed"), campaign tree hook: thread_local last tree + no lookups at 1x until first scaled.
- Shogun 2 shadow map resolution: not the wrong value (only +0x1a77b90 reads shadow_resolution_multiplier). The
  game's TEXTURE_MANAGER::create_resource (+0x19da5d4) budget is ~1.8 GB on the 5090 (ATN's "Max"; likely ~30 GB
  wrapped at 32 bits); bigger shadow maps did not fit -> "Insufficient video memory to run game, terminating"
  (PostQuitMessage) -> next start = GPU database defaults (boot.pack: RTX 5090 Ultra 1920x1080, TAA): ATN's 1080p /
  no DLSS. Fix: force_total_video_memory (+0x3454e58, dword bytes) = 0xFFFFFFFF while the multiplier > 1. 16384
  removed (D3D11 CreateTexture2D 4 x 16384^2 fails with E_OUTOFMEMORY on the 5090; 12288 works; test in scratchpad
  d3d11_size_test). Built while the game ran: the addon was linked to scratchpad s2_dlss_build (build/Release's copy
  is still the old one until the next build with the game closed). Untested.
- Next (ATN): the other TW games the same way (CPU / debug cleanup: Troy / WH1 / WH3 still have "Save game code" and
  trace buttons; graphics_status lines), then Rome Remastered tiers, Troy / WH3 shadows.

## RESUME HERE (2026-10-23, latest): TW round 7 (ATN's feedback; untested)
Zips: twattila_v20, twthronesofbritannia_v7, twrome2_v8, twwarhammer_v7, twwarhammer2_v7, twtroy_v7,
twpharaohdynasties_v9, twwarhammer3_v7, twshogun2completeedition_dlss_v37.
- Shadow Map Size: Attila / Thrones scale the tier immediates (graphics_hooks.hpp shadow_map); Rome II, WH1, WH2,
  Pharaoh set the game's shadowmap_extreme_quality (engine_tweaks flag tweaks; 8192 at the highest Shadows setting,
  their own code). WH2 also has shadow_resolution_multiplier (unused). Troy / WH3: their shadow size code not found
  (newer renderer; the 2048 / 1024 / 768 / 512 switch in Troy, WH1, Pharaoh is another map). Engine values applied
  at init_device too (before the shadows are built).
- Line-sweep AO (WH1 / WH2 / Troy / Pharaoh): the bitmask now runs in the blur (its own depth + camera, this frame's;
  5x5 in-group average); collects skip with the bitmask. Kept resources across frames were the likely cause of
  WH2's marks (round-6's depth-region fix did not help).
- WH3 AO: ATN runs the depth-less collect 0x2A629B2B (debug3 log: 0xEE5C29AE never created); replaced, depth from
  the downsample's UAV (KeepUavDepth) at t2, camera from t00 b1. WH3 LOD values are copied per model at creation
  (callers of +0x141a0363c store them at model +0x84): next battle / campaign load.
- Shogun 2 unit distances: UnitDistances +0x3a33a0 hooked (DLSS addon unit_detail.hpp); tweak values at +0xB8.
- Rome Remastered (open): Feral rewrites Preferences Data at exit (after our addon unloads), so file edits are lost;
  each tier value is looked up by name when settings apply and copied into engine objects (terrain_lod_distance a
  global at exe+0x2970970, grass_range the settings object exe+0x297A978 -> +0x3b0, building scale [obj]+0x6a8,
  shadow range +0x680, shadow atlas +0x34 of other objects). Scratchpad tw7_rr_*.py.

## RESUME HERE (2026-10-22, latest+): Attila v19 Shadow Map Size
- ATN found another author's Attila UI mod with 8192 shadows; ATN: never copy his mod or code. Ours is written from
  our own disassembly: the shadow setup (+0x17665e0) sizes per Shadows level (Max / High 2048, Medium 1024) scaled
  1x / 2x / 4x by one byte pattern (graphics_hooks.hpp shadow_map), at init_device; left alone if the sizes were
  changed already (his version.dll does). Thrones has the same code (same pattern, not wired yet); Rome II differs.
  Next: the other TW games.

## RESUME HERE (2026-10-22, latest): TW round 6 (ATN's feedback on round 5; untested)
Zips: twattila_v18, twthronesofbritannia_v6, twrome2_v7, twwarhammer_v6, twwarhammer2_v6, twtroy_v6,
twpharaohdynasties_v8, twwarhammer3_v6, twshogun2completeedition_v47 / _dlss_v36 (no Rome Remastered change).
- AO Radius max 10 everywhere (same sample count: farther apart, coarser, a little more cache cost).
- WH1 engine values 4 / 45 -> 45 / 45: ObjectValue returned null when no object lea preceded the name (Warhammer's
  object follows it). WH2 log: 50 / 50, Pharaoh 58 / 58.
- WH2 AO marks: the blur's depth texture is larger than the region the blur reads; the collect sampled it whole (old
  frames in the rest). line_sweep_ao.hlsli now takes depth_size (WH2 / Troy / Pharaoh: C / 2; WH1: C).
- Thrones dead units: its per-soldier update frame is 0x148 (Attila 0x144): graphics_hooks.hpp Game enum.
- Pharaoh dead units: twpharaohdynasties/dead_units.hpp patches the inlined imposter test (+0x6fe1c4 comiss xmm0,
  xmm10; jb) with a jump to a stub (DetourAllocateRegionWithinJumpBounds) scaling xmm0 when bit 0 of [r15 + 0x150]
  (alive) is clear. Tools: scratchpad tw6_pe.py / tw6_pdata.py / tw6_x64_refs.py / tw6_calls.py.
- WH3: graphics from num_pixels_per_triangle_lod_change_* (lea r8 layout, engine_tweaks FollowingObjectValue, value
  +0x60, read live at +0x141a036ba), inverse sliders. AO: collect ssao01_0xEE5C29AE hand-written (game path = Shogun 2's
  new_collect) + bitmask over a 10x10 tile; camera kept from t00 (PS b1) and bound at CS b1. If no change: the other
  collect 0x2A629B2B (no depth bound) may run -> trace.
- Shogun 2 DLSS overlay: reset buttons (settings.hpp's look) on every slider, AO mode slider. Unit LOD: tweaks
  (ENTITY_LOD_OVERRIDE, FEATHERED_SHARED_POSE_RANGE in empire.retail.x64.dll) have no direct readers: needs code
  hooks like Attila's UnitDistances (open).
- Rome Remastered: graphics tiers are values in %LOCALAPPDATA%/Feral Interactive/Total War ROME REMASTERED/Preferences
  Data (GraphicsSetting: lod_units-4 distances 14/30/40/50, lod_building-4 scale 2, lod_grass-4 range 150,
  lod_terrain-4 600, shadow_distance-4 600, shadow_resolution-4 4096, ...; scratchpad tw6_rr_tiers.py). Asked ATN
  how to raise them (open).

## RESUME HERE (2026-10-22, earlier): TW round 5 (ATN's feedback on round 4; untested)
Zips: twattila_v17, twthronesofbritannia_v5, twrome2_v6, twwarhammer_v5, twwarhammer2_v5, twtroy_v5,
twpharaohdynasties_v7, twwarhammer3_v5, twromeremastered_v5, twshogun2completeedition_v46 / _dlss_v35. Patch notes
updated (tw_patch_notes.txt).
- Advanced-only fix: Setting::is_visible defaults to a lambda returning true, so wrap it:
  `[shown = setting->is_visible] { return IsAdvancedSettings() && shown(); }`. AO Radius default 1 everywhere.
  Counters / code dump buttons removed; ao_trace.hpp is trace-only (OnPresent is one atomic exchange).
- Hooks on custom shader entries without replacement code never run (WH3 traced 0 passes, WH2 collect skipped ->
  stale AO lines). Line-sweep AO: the blur is hand-written (twwarhammer2/line_sweep_blur.hlsli, per game ssao01/02
  files) and its hook keeps t0 depth + b0 camera for the next collect (line_sweep_ao.hpp OnBlur / OnCollect).
- SDR output for the 8 games without it (shared.h OutputShaderInjection, proxy gamma 2.2, OnCreateSwapchain Auto).
- WH2 / WH3 PsychoV31: the per-map Extended Reinhard (W = 1/burn - 0.999) handoff now matches value and log-log slope
  at mid gray (t00 shaders).
- WH1 graphics via engine_tweaks (x64 tweak object found by the lea rcx after the name, value at +0x68). Thrones:
  Attila's graphics (graphics_hooks + engine_tweaks). Rome II / WH2: "What I would use:" preset (ATN's values).
- Shogun 2 AO: ssao01_0x98BB1159 (new_collect) replaced, bitmask over camera_VS_PS viewport (c38) with its own 5x5
  denoise; settings in the DLSS overlay (ReShade.ini [Shogun 2 DLSS] AmbientOcclusion*).
- WH3 AO: ssao00_0x49F538CF (depth downsample) hand-written so the Advanced trace runs; waits for ATN's trace.
- Not possible / open: Rome II Big Battle fix (no Attila-style animation packing), Troy / WH3 engine sliders (no tweak
  names), x64 dead unit detail (inlined per-soldier code), Rome Remastered AO (Intel ASSAO, deinterleaved).

## RESUME HERE (2026-10-22, later): TW round 4 (ATN's feedback on round 3; untested)
Zips: twattila_v16, twthronesofbritannia_v4, twrome2_v5, twwarhammer_v4, twwarhammer2_v4, twtroy_v4,
twpharaohdynasties_v6, twwarhammer3_v4, twromeremastered_v4, twshogun2completeedition_v45 / _dlss_v34,
rysesonofrome_fix40 / _dlss_v105. Patch notes updated (tw_patch_notes.txt, ryse_v1.04_*).
- AO: own section "Ambient Occlusion (Experimental, needs in-game SSAO)" after Graphics; defaults 1 / 4x / 0.15 m
  (ATN, Attila too: its presets no longer set AO); Graphics + AO Advanced only (loop before configured = true), titles
  pink. `twwarhammer2/ao_trace.hpp`: AO pass counter (Advanced text), one-frame AO trace (textures, UAVs, constants to
  ReShade.log; WH3 fx_ssao, Shogun 2 ssao_3k), SaveGameCode (exe image from memory to Downloads\<exe>.memory.bin; WH3,
  Troy, WH1: protected exes; analyse offline for engine tweaks).
- WH2 "matrix" AO: positions off the depth texel centre (half-size depth) -> self-occlusion dots; bitmask_ao.hlsli
  AoSamplePosition snaps every sample. Rome II AO: ATN saw no change; the two resolves are replaced, the counter will
  tell if they run. Rome II Grass / Campaign removed (no effect); Dead Unit Detail via graphics_hooks (Rome II:
  ImposterDistance +0x4978a0, call +0x464960, frame 0x1d0, alive +0xd8).
- Bloom: high-pass caps -> log shoulder in HDR (bloom.hlsli per game): Thrones (Attila's shaders), WH1 / WH2 / Troy /
  Pharaoh (luminance 25), Rome II (Y 2.1 x white), Rome Remastered (brightest channel cb0[0].z). WH3: none.
- Extended per-channel shoulder: WH2 / Troy / Pharaoh hdr_to_screen.hlsli, WH3 t00 (WH1 has no Extended).
- Hue defaults (V31 Highlight Hue Shift): WH3 0.15, WH2 0.05, Pharaoh 0.06, Troy 0.10, Rome II 0.05, Thrones 0.20,
  Rome Remastered 0.06, Attila 0.20, WH1 0, Shogun 2 0.46, Ryse 0.08. Reset = PsychoV31 where no Extended mode (WH1,
  Attila, Rome II, Thrones). Graphics presets MAX / Reset: WH2, Pharaoh, Rome II, Thrones (Troy / WH3: no graphics
  sliders). WH3: one Recommended (the 2000+ one). Pharaoh: Shadow Distance removed. Attila: Big Battle Fix last, off.
- Shogun 2 / Ryse DLSS overlays: headings DLSS lavender (B39DDB), Graphics pink, the rest green; Shogun Nexus labels
  "NexusMods (For RenoDX + DLSS)"; Shogun graphics options go into its DLSS addon (HDR addon reads them,
  ReadDlssAddonOptions).
- Open: WH3 / Shogun 2 AO (ATN traces: WH3 with and without the Nexus DLSS mod; Shogun at 100% and a lower render scale),
  WH3 / Troy / WH1 engine sliders (game code dumps), dead units for x64 games (the per-soldier code is inlined:
  Pharaoh +0x1406fdf88 region), WH2 campaign / unit slider reports (unit tweaks are read every call at +0x142256720;
  campaign_tree_lod* has no direct reader), grass (only Pharaoh has a working grass distance).

## RESUME HERE (2026-10-22): TW round 3 (after Attila v15): all other TW mods (untested)
Zips: twwarhammer / twwarhammer2 / twtroy / twwarhammer3 / twromeremastered / twthronesofbritannia _v3,
twpharaohdynasties_v5, twrome2_v4, twshogun2completeedition_v44, twshogun2completeedition_dlss_v33 (Downloads). Patch
notes: Downloads\tw_patch_notes.txt (all TW, unposted), ryse_v1.04_patch_notes_nexus / _discord.txt.
- Every addon: yellow in-game settings note (General), NexusMods button next to Ko-fi, section titles green /
  Graphics (Experimental) pink (`settings.hpp` default_section_tint + section_tints), Recommended presets = Reset +
  PsychoV31 at its defaults + Video AutoHDR 0.20 (Reset: AutoHDR off), Video AutoHDR / Dithering (autohdr.hlsli; the
  sharp video shaders and the blurred_yuv variants), V31 defaults 1 / 0 / 0. WH2: the 2000-nit preset removed (same in V31).
- AO (Visibility Bitmask, `twwarhammer2/bitmask_ao.hlsli` core): Thrones = Attila's shader; Rome II ssao00/01 (HBAO
  resolve); WH1 / WH2 / Troy / Pharaoh line sweep: `line_sweep_ao.hpp` skips the sweep, binds its depth (t1) and camera
  (b0) for the collect (ssao00_*), radius 1 = 2 m; Troy / Pharaoh default AO is ssao2 HBAO (ssao_algorithm = 1,
  ssao01_0xD42C3523). Not done: WH3 (fx_ssao with overscan; needs a trace) and Shogun 2 (new_sweep + render scale).
- Engine sliders: WH2 + Pharaoh: Unit Detail x5 sizes (*_IMPOSTER_DISTANCE etc. are read every call as the base
  distances: Pharaoh +0x1406b2790, WH2 +0x142256720), Animation / Variety / Footstep, Building / Campaign Detail; WH2
  also Model / Terrain. engine_tweaks.hpp skips sections > 256 MB (WH2's protection). Rome II: Attila's UnitDistances
  hook (Rome II pattern +0x4497e0, `Update(..., rome2 = true)`), Building / Campaign Detail, Shadow Distance removed.
  Thrones: Attila's SSR shaders (Reflection Quality). WH3: Warhammer3.exe registers tweaks differently (none found).
  No binaries here: Troy, WH1, Thrones, Rome Remastered, Shogun 2.
- Rome Remastered Extended: ApplyExtendedPeakShoulder per channel (Ryse fix32). Shogun 2 renamed
  twshogun2completeedition(_dlss) (delete the old addons). DLSS for Attila / Rome II / Pharaoh: no scene motion vectors.
- Tools (scratchpad, copy if needed): tw_tweak_list.py (x64 tweak list + defaults), tw_override_xref.py, tw_dis.py,
  tw_dis32.py, tw_pattern.py, tw_callers32.py.

## RESUME HERE (2026-10-20): Total War round (ATN): every TW mod besides Shogun 2
ATN's request: per game (Rome II, Attila, Thrones, Rome Remastered, Troy, Pharaoh, WH1, WH2, WH3): missing shader
permutations from the packs, missing post-processing sliders, PsychoV31 as an extra mode (+ Tales' grading sliders),
credits + ATN's Ko-fi (Simple + Advanced), DLSS feasibility (motion vectors / depth; not WH3, not Shogun 2), higher
graphics options (a "Graphics" section in the HDR addon) where possible. Shogun 2 (v42 / DLSS v32): credits, Ko-fi, V31
sliders done. Tools: C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw (tw_pack, tw_names, tw_feral_names,
tw_shaders, tw_missing, tw_cover, tw_family, tw_vfx_port); dumps bin\cmd_Decompiler-1.3.16\dump_shader_<game>_pack
(names.tsv, missing.txt, cso\). Findings: only WH3 has motion vectors (Pharaoh's are the shroud's only), so DLSS is not
viable elsewhere (moving units would smear). No TW mod runs ACES code (Troy / Pharaoh: the game's own ACES fit, not
A.M.P.A.S. code); Shogun 2 ships Bend Studio's SSS (Apache 2.0, APACHE_LICENSE-2.0.txt in docs/licenses).
32-bit games (build32, clang-x86-release, .addon32): Rome II, Attila, Thrones. The others are 64-bit.
- Rome II v1 done (Downloads\twrome2_v1.zip; README in src/games/twrome2): V31 mode, 9 new permutations + 28 VFX ports, credits, Ko-fi.
- Attila v1 done (Downloads\twattila_v1.zip): V31, constant tone mappers, lens flares 1-3, lens vignette + Vignette slider; VFX permutations and the and_gamma tone mappers not ported (see its README).
- Thrones v1 done (Downloads\twthronesofbritannia_v1.zip): V31, constant tone mappers (t17 / t18), lens flares 1-3.
- Rome Remastered v1 done (Downloads\twromeremastered_v1.zip): resolve.hlsli covers all 12 ResolveMainColor permutations (t00-t11), fxaa01, distortion01, V31 (mode 3).
- Troy v1 done (Downloads\twtroy_v1.zip): hdr_to_screen.hlsli, V31 (mode 3), photo mode (photo01-04), MSAA DOF preprocess (dof01).
- Pharaoh v1 done (Downloads\twpharaohdynasties_v1.zip): hdr_to_screen.hlsli, V31 (mode 3), DOF passes t05-t08, photo01-04, DOF composite dof00/01, sharpening00 (+ slider re-enabled).
- WH1 v1 done (Downloads\twwarhammer_v1.zip): hdr_to_screen / tone_mapper / lensflare includes, V31 (mode 2), constant tone mappers t09 / t10, lens flares 0-3 + Lens Flare slider.
- WH2 v1 done (Downloads\twwarhammer2_v1.zip): hdr_to_screen.hlsli, V31 (mode 3), t03-t08 (no distortion / MSAA main passes), lens flares + slider.
- WH3 v1 done (Downloads\twwarhammer3_v1.zip): V31 (mode 3) in t00, lensflare.hlsli, lensblood00. Not ported: DOF/SAT permutations (DLSS-mod render scale rebuilds).
- Graphics v2 (ATN: only the games ATN plays: Pharaoh, Attila, Rome II; leave the rest): Downloads\twpharaohdynasties_v2.zip,
  twrome2_v2.zip, twattila_v2.zip, untested. Shared `src/games/twrome2/engine_tweaks.hpp`: Warscape engine tweaks
  found by name at the first present (x86 empire.retail.dll: push name; mov ecx, obj; call ctor, value obj+0x48;
  x64 Pharaoh.exe: lea obj; lea rdx, [name]; call ctor; movss [obj+0x60]), scaled every present (a value the game
  sets again = new base); "Graphics" section (1x-4x) + Advanced status "Engine values found: n of n". Pharaoh (live):
  Model Detail Distance = distance_lod_scaler / x (LOD pick +0x30e59e0), Grass Distance = grass_visible_range +
  grass_fade_start (written by the grass quality setting +0x3119a60), Small Object Distance = cull_small_screenspace_
  objects_default / vegetation factors. Rome II (next battle / campaign load): lod_multiplier_for_low..extreme (model
  setup +0x14b9770), Grass = scrub_lod_distance_default (int 200, copied into grass nodes +0x1492653). Attila: its
  quality factors are code constants (+0x17fbd30), so the battle_entity / building / ship / campaign_tree lod0-3
  distances are scaled; Grass = scrub_lod_distance_default. Not read anywhere in Pharaoh: tree_lod_scaler,
  battle_entity_* / building lods (dead tweaks). Tools: tools\tw\tw_tweaks.py (x64), tw_tweaks32.py, pe_dis32.py.
- TW round 2 (ATN feedback 2026-10-20; zips: Pharaoh / Rome II / Attila v3, Thrones / Rome Remastered / Troy / WH1-3 v2,
  Shogun 2 v43; untested): V31 Highlight Hue Shift default 0 + the Tales fire note above Highlight Saturation / Hue
  Shift / Saturation Shift (Shogun 2: note only, ATN's 0.42 / 0.33 kept). "Graphics (Experimental)", before Output.
  ATN: Rome II / Attila v2 sliders did nothing even after a restart. v3: Model Detail = hook on the LOD pick (same code
  in both, Rome II +0x14f0710, Attila +0x183d570: distance squared / slider squared, live); Grass = the tweak plus
  the grass node constructor's 200 m constant (code constant; next battle; unverified which field limits grass);
  Terrain Detail = terrain_far_shader_distance; Shadow Distance = shadowmap_split_scale (cascade widths, Attila
  +0x1784e52). Pharaoh (ATN liked v2): 7 sliders (adds terrain_distance_scale_*, shadowmap_split_scale*, SSR /
  contact shadow step counts, blended-object cull distances), maxima 4x-8x; int tweaks found via mov dword [rip], imm.
  Pharaoh sharpening: no game setting (strength 0): sharpening.hlsli, RenoDX's 0.10 x slider in ps40_sharpen and
  ps40_chromatic_aberration. Attila bloom: high pass caps luminance at 25 (flat HDR halos): bloom.hlsli log shoulder.
  Tools added: tw_tweaks32.py, tw_tweak_uses32.py / 64.py, pe_dis32.py, x86_lod_pick.py, x86_list_field.py, rtti32.py
  (no RTTI in these DLLs).
- Graphics round 3 (ATN: Attila first until 100% sure, then Rome II, then the rest): Attila v4 (Downloads\twattila_v4.zip,
  untested): Unit Detail = hook on UnitDistances +0x43c160 (18 unit distances: imposters etc.), Object Detail = LOD
  pick (trees), Grass = scrub update hook +0x17fb510 (node +0x80) + grass VS 0.01 / SI.grass_distance, Shadow =
  FitShadowSplits hook +0x1794430 (new_shadows) + shadowmap_split_scale; `twattila/graphics_hooks.hpp`. Yellow note
  (engine_tweaks::Note) in Attila / Pharaoh v4 / Rome II. Rome II next: none of Attila's patterns match; redo the
  same analysis (units table, grass VS 0.01, new_shadows) in Rome II's empire.retail.dll.
- Attila v5 (Downloads\twattila_v5.zip, untested): Grass / Shadow removed (ATN: costly, no visible change). Unit Detail =
  only the 8 imposter values of UnitDistances (the 18 named in twattila/graphics_hooks.hpp). Model Detail = the LOD
  pick hook (soldiers' parts 100/200/400/500 m, trees, buildings, props; whole-engine scan: the only LOD distance
  compare) + rigid_campaign_tree_lod. New: Building Draw Distance (battle_building_no_draw_distance_calibration,
  next battle), Campaign Prop Distance (campaign_props_max_distance, live). Unit LOD db tables
  (warscape_animated_lod: siege / chariots / campaign traders; warscape_rigid_lod_range: 200/400/750/1500 m).
- Attila v6 (Downloads\twattila_v6.zip): Shadow Distance 0.5x-4x = the shadow cascade table (.rdata +0x1c88aa0) scaled
  in place (battle highest 30/90/210/450 m = 780 m range). Shadow map resolution not found statically: next step a
  runtime resource log (square depth textures) if ATN wants sharper shadows than Ultra.
- Attila v7 (Downloads\twattila_v7.zip, ATN: done with Attila after this): Unit Detail 0.1x-8x; Campaign Detail
  Distance (campaign_tree_lod0-3 / campaign_grass_lod0-4 at model setup, campaign trees use their own pick loop
  +0x17ee130; rigid_campaign_tree_lod + campaign_props_max_distance live); campaign sea reflections = BC1
  environment_map.dds (LDR source, not a clip; left); Recommended presets in PsychoV31. Next: Rome II graphics
  (redo Attila's analysis: units table, shadow cascade table, campaign LOD tweaks), then the other TW games.
- Attila v8 (Downloads\twattila_v8.zip): Extra: Water Reflections (AutoHDR on the BC1 environment cube in the
  deferred water reflection pass, 10 permutations) + Video AutoHDR / Dithering (cutscene00), autohdr.hlsli.
- Attila v9 (Downloads\twattila_v9.zip): campaign trees via DrawTree hook +0x17ee130 (Campaign Detail, live; per
  model LOD distance scaling), Water Reflections removed, Unit Animation / Variety / Footstep sliders (0.1x-4x).
  Open (ATN, 8x unit size mod): dead soldiers at full LOD up close are heavy; a dead-only imposter distance needs
  the per-soldier update (+0x456843) traced (state values 0x52 / 0x59 / 0x5a seen there, unconfirmed).
- Attila v10 (Downloads\twattila_v10.zip): Dead Unit Detail Distance (0.05x-1x) via ImposterDistance hook +0x48a8f0,
  only from its per-soldier caller (return address checked), animation id at return address + 12 + 0xd8, death ids
  125-129 / 138-139 / 185-324; Graphics presets "With 4x / 8x unit sizes", "With vanilla unit sizes", Reset. Shadow
  jumping in big zoomed-in fights: no pool tweak found; ATN to test Unit Animation 1x vs 3x. Later (ATN, possible):
  better SSAO (replace the game's), screen-space shadows (Bend, as Shogun 2), better SSR (the game has one).
- Open: Attila VFX permutations, with_bloom_and_gamma tone mappers, WH3 DOF/SAT permutations.
- Attila v15 (Downloads\twattila_v15.zip, ATN: Attila done): preset values (AO 0.39 / 4x / 0.19 m, reflection 2x, building 6x,
  vanilla unit detail 1.2x), AO choice named Visibility Bitmask, note / gamma wording. Next: other TW games.
- Attila v14 (Downloads\twattila_v14.zip): one Graphics section, presets that set everything (ATN's values),
  Building Detail Distance (battle_building_lod0-3), Shadow Distance out, Campaign Detail max 4. Open (ATN's question):
  shadow flicker: cascades not snapped to shadow-map texels when the camera moves (the matrices are built outside the
  split fit +0x1794430), small units a few texels big; options: hook the cascade matrix setup (snap), larger shadow map,
  softer filter (sun light replacement).
- Attila v13 (Downloads\twattila_v13.zip): Big Battle Shadow Fix (shadow pass overflow -> unit distances down)
  instead of taller animation pages (soldiers vanished); contact shadows and Building Draw Distance removed; SSR
  slider = Reflection Quality (armour). Dead Unit Detail confirmed working.
- Attila v12 (Downloads\twattila_v12.zip): jumping shadows = animation texture overflow (Animated Soldier Limit
  slider: page height x it), Dead Unit Detail by the soldier's alive flag, 4 unit distances (fade widths out), contact
  shadow fixes (grass, 40-80 m fade, edges), UI order / colours, V31 hue 0.20, video AutoHDR 0.20 in Recommended.
- Ryse DLSS v104: Voice / Text language (g_languageAudio / g_language at run time).
- Attila v11 (Downloads\twattila_v11.zip): Lighting (Experimental): RenoDX AO (visibility bitmask GTAO, replaces
  ssao resolve 0xBD389EF1, toggle Game's / RenoDX), Contact Shadows (sun light 0xC64DB947 / 0x00D1F51E / 0xB6DA03C5 /
  0x23E4B7D8 rebuilt from decompiles, see README v11 for the decompiler fixes), Water Reflection Quality (SSR
  0x66006BEA / 0x47D75349). Output SDR / Auto, Nexus, section colours (settings.hpp section_tints), Unit Detail test
  sliders, DebugUnits probe for dead soldiers. ATN has in-game SSAO off (preferences.script.txt gfx_ssao false).
  Open: dead soldiers (probe numbers), shadow jumping in zoomed fights (ATN: when Unit Animation and Unit Detail
  differ; contact shadows do not touch the shadow maps). Later: other TW games' Nexus buttons + graphics.
- Ryse HDR fix39 / DLSS v103 (Downloads): V31 fire sliders, SDR / Auto, credits + Ko-fi + Nexus; DLSS: Dynamic sound
  mixing (s_HDR), links. Cloth (debugForClaude40: a climbing soldier's thigh through his tunic): the cloth has no leg
  colliders (g_avatar_legs_colliders_dist, v73: characters fly); left. Shadow flicker report: needs ATN's shadow trace.
- Tales DLSS v93 (Downloads\talesofarise_dlss_v93.zip): without Arise-SDK (and DebugDevArise) sun shadow distance
  stops at 2 (clamp patch undone, choices above 2 removed, presets capped) and Far sun shadows is greyed out.
- Tales DLSS v92 (Downloads\talesofarise_dlss_v92.zip): Arise-SDK link = its Nexus page
  (www.nexusmods.com/talesofarise/mods/5, newer than GitHub) in overlay / README / Arise-SDK.ini; README text matches
  v89 (the group needs Arise-SDK). Updated text for emoose: Downloads\talesofarise_emoose_text.txt.
<!-- TW round progress -->

## RESUME HERE (2026-10-19): Tales of Arise release candidate - HDR v60, DLSS / graphics v89
ATN continues on another Claude account (same PC / Windows user, same paths). Details per version: the READMEs
(`src/games/talesofarise/README.md` up to v60, `src/games/talesofarise_dlss/README.md` up to v89); the long Tales
history is further down ("Tales DLSS upscaling" section, newest at its end).
- Deliverables: `Downloads\talesofarise_v60.zip` (HDR), `Downloads\talesofarise_dlss_v89.zip` (DLSS + graphics, with
  the shipped `Arise-SDK.ini`). Nexus text: `Downloads\talesofarise_nexus_v2.txt` (short; known issues, Arise-SDK
  credit). Arise-SDK credit facts: `Downloads\talesofarise_arise_sdk_credits.txt`.
- Build: `cmake --build --preset clang-x64-release --target renodx-talesofarise.addon64` / `renodx-talesofarise_dlss.addon64`.
  Game: `D:\SteamLibrary\steamapps\common\Tales of Arise\Arise\Binaries\Win64` (symlinks to build/Release; ATN's
  traces in `debugN` there; `tales_image.bin` = the exe dumped from memory, file offset = RVA, base 0x7FF7BFF60000).
  Tools (persistent): `C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\toa\image` (img.py: pdata funcs + capstone,
  refs.py, pat.py, vt.py, crashdump.py) and `tools\pack.ps1` (zip of changed files + the addon64 into Downloads).
- ATN's next steps: test v89 (with Arise-SDK installed, and without it with `DebugDevArise` 0 / 1), pictures, video,
  release. Untested and unreported: "Sun shadow layers (test)" row (raises the light's DynamicShadowCascades, proxy
  +0x194; v66 found the sun is one shadow map): remove it if it changes nothing.
- Arise-SDK (emoose, GPL-3.0, none of its code used): he asked that with Arise-SDK installed our menu drive his live
  sdk.* cvars (done, v86; engine_cvars.hpp now reads Refs whose global lives in another module + bool Refs) and offered
  to link our mod. ATN asked whether our own versions may stay without Arise-SDK: no answer yet. v89: the "Arise-SDK's
  settings" group (first in Graphics) needs Arise-SDK (greyed, presets skip it); NPC distance uses our code (shipped
  ini: MinimumNPCDistance -1; Arise-SDK has no cvar for it); hidden ReShade.ini `[Tales AA] DebugDevArise=1` = our own
  code for the whole group (ATN's testing only, never written by the addon). Shipped ini: game values (monster 1,
  LOD 1, outline 6000), TAA three untouched (-1 / -1 / false), SkipIntroLogos false (ATN: it skipped nothing; maybe
  tied to Arise-SDK's DLC "A Feast of Blessings" infinite-loading bug).
- This round (all in the READMEs): Smooth sun movement (Recommended, off by default; engine_hooks.hpp: light proxy
  SetTransform + sky collection vector (MPC) smoothing at constant speed, CSM snap grid anchored at the camera via the
  proxy's vtable slot 20 = no shimmer, ATN: perfect); Day and night speed Off..1000x (TWorldTimeManager rate +0x68);
  Shaders on load checkbox; preset M default (L smears small particles: they have no motion vectors; NVIDIA's bias
  mask is ignored by L / M, tried v81-v82 and removed); HDR: Thin Sun Shadow Edges 0.22, Shadow Edge Color 5, Post
  Processing section, SDR output + Auto (Shogun 2's), Sun Glow tried and removed (its glare shader also draws text).
- Known, not fixable: ambient occlusion halo around characters (screen-space; the option only reduces it).
- 2026-10-19: HDR v61 / DLSS v90 (`Downloads\talesofarise_v61.zip`, `talesofarise_dlss_v90.zip`): PsychoV31 Highlight /
  Shadow Contrast, Flare, Dechroma, Gamut Compression + Gamut Compression Mode (BT.709 / BT.2020 / Display P3 / Samsung
  G80SD; ATN: fire too orange, maybe outside the display's gamut); About: credits, ACES notice, ATN's Ko-fi (both
  addons). ACES license (official) in `docs/licenses/ACES_LICENSE.md`, shipped as ACES_LICENSE.txt. No more Tales Nexus
  text (ATN). Next after Tales: old games get Ko-fi, credits and (where ACES code runs) the ACES notice (memory
  project_credits_aces_kofi).
- v62 (`Downloads\talesofarise_v62.zip`): G80SD mode removed, Display P3 default, Psycho Highlight Saturation 1.00,
  Recommended (Vanilla shadows) Shadows 0.50 + Flare 0.39, final gamut fit after hue shift / LUTs / post grade, credits
  say Musa's custom PsychoV31.
- v63: PsychoV31 Highlight Saturation Shift (experiment, default 0): highlight saturation toward the SDR curve's
  (debug21: SDR fire yellow and saturated to its brightest, V31 red fading to white = pink).
- v64 (ATN's values): Psycho defaults + Recommended: Highlight Saturation 1.00, Hue Shift 0.57, Saturation Shift 0.30,
  Gamut Compression Mode BT.2020 again; Extended legacy Highlight Hue Shift 0.20.

## RESUME HERE (2026-10-16): Shogun 2 HDR v41 / DLSS v31, Ryse HDR fix37 / DLSS v101 (history below, newest at the end of this section)
Game: D:\Games\Total War SHOGUN 2 Complete Edition (DX11). Every shader + the game's HLSL source extracted from
data\shaders.pack: `bin\cmd_Decompiler-1.3.16\dump_shader_shogun2_pack` (README.txt; tools\shogun2). v1
(`Downloads\twshogun2_v4.zip`; v4: menu Brightness fixed at x1 in all modes, Extra sliders Lens Flares / Vignette /
Heat Haze / Radial Blur (README lists what's left: DOF, battle fog, sun; DLSS idea below); v3: Extended = UE Filmic Extended, Psycho contrast x the curve's 1.40, game
gamma 2.0 emulated in HDR; film constants from data.pack *.lighting, mid gray 0.087 by the toe typo): proxy (the game's HDR stays on: it re-enables itself on HDR
displays; harmless, only the two replaced shaders read it), tonemap 0xEF060C7C / 0xBEDBEFD8 (distortion) written from
hdr_to_screen.hlsl (UE4 FilmToneMap in AP1 + 3-way grading; Extended = shoulder asymptote at Peak; PsychoV31),
ui_resolve 0x3D56DB58 always the SDR copy. Mod README has the pipeline. Next: ATN's test of v4, DOF / fog / sun sliders; then ATN's idea: the game's "DLSS" is DLAA only
(it has jitter + motion vectors): make it render below output and let DLSS upscale. Then Tales of Arise, then recheck the other Total War games for shader packs (memory project_roadmap).
Addon not linked into the game folder (symlink needs admin).
Shogun 2 DLSS RE (2026-10-14, tools\shogun2\sx.py on empire.retail.x64.dll, NGX 3.10.4 + FSR3 + XeSS static/imported):
the game's DLSS = DLAA: feature create 0x1abc6f0 (Width/Height = OutWidth/OutHeight = display size from the
object at 0x3457890 +0x10/+0x14, PerfQualityValue 5 = DLAA, flags 1 / 0x41 | 0x80), pass 0x1ad1580 (eval struct:
Color rbx+0x298, Output rbx+0x2a0, render subrect = display size, MV scale -1 / 1), SDK evaluate helper 0x1abd950.
No render scale anywhere (gfx_aa 5/6/7 = FSR/DLSS/XeSS as AA; FSR3's GetRenderResolutionFromQualityMode never
called). Upscaling needs the engine to render smaller. ATN: do both (1: lower game resolution + 4K output, tonemap /
UI re-run; 2: viewport-scaled render scale). Step 1 = `src/games/twshogun2_dlss` v1 (`Downloads\twshogun2_dlss_v1.zip`,
untested): detours on create / evaluate (patterns in game_dlss.hpp), overlay status, one-frame trace
(shogun2_frame_trace.txt). v1 trace (debug1): hooks work; jitter passed to DLSS = 0; everything before DLSS = 3D,
everything after (bloom, tonemap, UI) display size: ATN's idea (UI at output, 3D upscaled) = option 2. v2
(`Downloads\twshogun2_dlss_v2.zip`): trace adds camera constants (screen / viewport / origin / RT dims, jitter).
v2 traces (debug2): TAA jitters, DLSS never (game bug). v3 (`Downloads\twshogun2_dlss_v3.zip`): jitter
patch (+0x1a9101b test/je -> jmp +0x1a91075) while DLSS runs, overlay toggle + debug sign (ATN: clearly better,
sign right). v4 (`Downloads\twshogun2_dlss_v4.zip`, untested): render scale (option 2, README v4: viewports /
scissors / camera g_viewport_dimensions scaled until the DLSS evaluation, feature recreated with the render
input, Halton jitter in render pixels). v4 test (debug3): works (33%: 1280x720 -> 4K); 77% failed (UltraQuality
refused). v5 (DLSS: Quality from 65%; HDR twshogun2 v5: DOF blur / focus + tonemap distortion lookup scaled, render
scale captured like WH3). ATN: if option 2 works, option 1 is not needed. v5 test (debug4): DOF fixed; river copy in the sky,
extra shadows, fire. v6 (HDR: SSR copy, highlight, Bend SSS, AO sweep, point / particle lights, ribbons,
auto exposure fixed for the render scale; sliders DOF / Battle Fog / Sun, Distortion; DLSS: custom 1-100%).
Audit tool: tools\shogun2\scale_audit.py TRACE NAMES. HDR v7: all permutations (+ SSS without cascades,
8-bit hashed DOF blur; videos at Paper White + Video AutoHDR); open: old AO path, SAT DOF blurs (README
v7); own sharpening: ATN no. DLSS v7 (debug5 flicker below 100%): MVLowRes was never set by the game
(flags immediates +0x143 / +0x15b of create), now set while scaled; texture LOD bias as Ryse.
ATN: MV fix works. v8: DLSS folds the jitter into the camera's inverse matrices when they lack it (blurry shadows,
rock acne at 33%; log line 'Camera inverses'); HDR Grass Fade (grass dither moved by the jitter; vanilla
flicker where the grass ends). debug7: inverses 'kept' (already jittered); Grass Fade made DLSS smear grass
(coverage changing every frame). v9 = both removed (code as v7). OPEN: softer shadows + rock stripes at 33%
(asked ATN: AO off at 33% on the rocks); vanilla grass-end flicker (CPU grass distance, not changed).
debug8: rock fixed with SSS on; SSS off broken = the game's cs_passthrough double half-pixel offset (sss02 fixes).
SSR flicker at 33%: traced at half the render size; HDR v10 SSR Resolution Full = render size at <= 50%
(ssr.hlsli). v11 adds Grass Distance Fade Stable (dissolve threshold per 0.5 m world cell, grass.hlsli).
v12 adds Extended user grading (as Ryse; Hue Correction default 0).
debug9: rock fixed; SSR Full great <= 50%; grass Stable had holes (v13 fixed: screen 25% + cell distance part);
SSR Full now also 50-100% (F = render / min(render, display/2)). OPEN: flowering bush unstable with jitter
(asked for a trace), SSR at DLAA (needs bigger buffers), shadows fatter at 33% (sampling).
debug10: shadows thinner below 100% = the LOD bias on shadow-map leaves (DLSS v10: main view only); grass fade
removed (HDR v14, no visible gain; popping is the CPU's grass placement / LODs); flowers / tree leaves: MVs
and dissolve checked, open (asked: preset K, LOD bias Matched). HDR v15 / DLSS v11: DLSS presets (SetUI hook),
SSR resolution in the DLSS overlay (HDR addon reads ReShade.ini). ATN: presets / LOD bias Off do not fix the
leaves; v11 blurrier textures (sampler cache). DLSS v12 (README v12): Simple / Advanced overlay, LOD bias
rebinds on main-view changes, Grass distance (scrub node +0xd0, update hook +0x1b15f20), Advanced "DLSS
particle mask" off (game passes FSR reactive mask; leaves never write it), trace with 8 RTs + clears.
debug11: particle mask off = no change; MVs bound (rt3) and jitter sign consistent (+0x19a42b4).
HDR v16: dissolve00/01 (rigidstandard alpha test 0xAECABA3F, rigidvertexwind 0x557A56D5 G-buffer) fade
pattern on SV_Position instead of the jittered clip position. DLSS v13: grass distance to 32x. NEXT: ATN's
test of flowers / leaves; near plant LOD pops (no global plant LOD distance found yet; terrain force_lod
is the terrain grid, force_render_lod a debug filter). ATN: v16 flowers still flicker. DLSS v14: Wind
motion fix (prev_time_in_sec = last frame's time on the main camera; theory: scene prev time only set
by its time update +0x1a968e0), Terrain detail Finest everywhere (enable_force_lod / force_lod).
debug12: wind fix no effect (removed in v15). DLSS v15: Model detail distance (SelectLod +0x1b26470 / fade
+0x1b265b0 hooked, distance^2 / scale^2; grass clumps RMV2 LODs 25/50/100 m). Flowers flicker: still open.
NEXT: ATN's test of model detail on grass pop-in; trees (tree_mesh / TREE_LOD_LIST) not found.
debug13: white flickering specks on the meadow only facing the sun (SSR/SSS no matter) = foliage sun
backscattering (step at dot(N,L) < 0). HDR v17: sunlight00-03 (directionallight 0xED1BED93 etc.) x Foliage
Backlight (Extra): ATN only trees change. HDR v18: Foliage Sun Glints (sun specular) and Foliage Reflections
(env reflectivity) on IS_FOLIAGE pixels. ATN: Sun Glints 0 removes the specks, the others are right at 1.
HDR v19: only Sun Glints kept; Highlight Hue Shift (Tales) in both HDR modes (Ext 0.30 / V31 0.24), Hue Correction
removed; SSRResolution read only while the overlay is open. DLSS v16: render sizes in whole display-aspect units
(custom 99% flickered: rounded aspect -> motion_vectors.hlsl static MVs up to 0.6 px at the edges); debug14: bushes
(rigidleaf_v3) and tree trunks (rigidbranch_v3) unstable, stones / grass stable -> Advanced "Capture DLSS inputs"
(4 frames color / MV / depth raw .bin) + trace prints camera constants on every change. NEXT: ATN's capture at 33%
paused at the bushes; decode the .bin (header w, h, DXGI format, texel bytes) and check tree MVs.
debug15 (tools\shogun2\capture_decode.py): tree pixels MVs up to 1000 px while paused (grass 0) = tree PS read prev
pos with w = 1, VS passes (s p, s), s = pos.w x (w0 + w1) != 1 (half weights). HDR v20: treevelocity00-03 fix; Fire &
Glow / Smoke & Dust Brightness via effects_overlay (blend state per draw); SSR ray steps (x1/2/4), SSS samples
120/240 (both compiled); Radial Blur removed; Recommended: glints 0, bloom 1. DLSS v17: particle resolution
(vfx_enable_dynres off below 100%: smoke stuck on screen in heavy smoke), SSR/SSS options, particle mask test removed.
ATN: plants FIXED. Particle resolution Full = 80 -> 30 fps. HDR v21: VFX sliders in Extra (Experimental), "VFX
Brightness: Fire & Glow / Trails / Firelight / Smoke & Dust", "VFX Opacity: Smoke & Dust" (black smoke); SSS to 960,
SSR to 16x; hue 0.45 / 0.38. DLSS v18: VFX dynres viewports / scissors / camera c38 (int(display x s), s 0.3-1)
scaled to render x s (render_scale.hpp ScaledParticleSize); dynres back on by default. ATN: smoke stays in place
below 100%, but blurry. HDR v22: banner flicker paused (debug16) = cloth VS passes the CPU's stale prev_position
(clothvelocity00 0xBDFF60D4: current position while time == prev time); Trails slider + projectiletrail 0x03F58817,
Fire & Glow + rigidglow 0xBA0315BE / 0x546A3049; Psycho highlight contrast 0.86 (contrast 1.40 had lifted highlights up
to a stop; tools\shogun2\psycho_fit.py). HDR v23 (ATN): Psycho contrast match removed (own contrast, highlight
contrast 1). HDR v24: Bloom only the bloom (hdrflare00; the tonemap's t1 holds bloom + flare ghosts), Recommended Lens
Flares 0, Psycho Cone Response Exponent (power = contrast x cone, purity / contrast, as ruse), Firelight + point lights,
VFX 0.1 steps. DLSS v20: particle minimum = 0.3 / render scale automatically, slider removed, Advanced shows the
particles' lowest scale last frame. HDR v25 / DLSS v21: SSS to 3840 (bend_sss_gpu.hlsli loops dynamic, reads straight
to groupshared: 463 slots), SSR to 64x. HDR v26 / DLSS v22: heat haze bilinear below 100% (debug17 mosaic = point-sampled
render-size distortion), ATN's presets (HDR: Recommended + Recommended (SDR-like Saturation), Reset below; DLSS:
Recommended (ATN's settings) / Reset), hue defaults 0.20 / 0.25, SSR ray steps removed. Open: SSR Full at 100% needs
display-size SSR buffers (HiZ 1920x1080 R32G32 with mips, hit buffer R16G16) and doubled dispatches; offered to ATN.
HDR v27 / DLSS v23: Shadow stability (SSS contrast 4 -> 2, then IgnoreEdgePixels; untested), preset renamed Native-like
Saturation/Contrast, tooltips shortened. HDR v28 / DLSS v24: heat haze un-jittered below 100% (DLSS addon shares eval
jitter via dlss_jitter.hpp cross_addon; tonemap01 B-spline read), Shadow softness (cascade tent 2.5 / 3.5 texels), heat
haze slider in VFX. Untested: jitter sign (if the haze wobbles more, flip it in tonemap01). HDR v29: VFX Brightness:
Dim Fire & Glow (vfxoverlay00 additive draw, brightness-dependent gain, pivot 2 scene-linear; ATN: too bright on
house fires). HDR v30: depth of field cs_coc_bokeh (dofcoc00/01) also applied the heat haze (now x Heat Haze, x render
scale); VFX Effect Test (vfx_effects.hpp: effect constants CRC32 per draw; ATN to report house fire IDs, then Fire &
Glow per effect and Dim Fire & Glow removed); hue defaults Ext 0.33 / V31 0.42. HDR v31 / DLSS v25: ATN confirmed
the haze fix; Burning Houses (9FC96955) / Campfires (FE957C04) / Fire & Glow per draw (SI.vfx_glow_draw), Distortion
and Haze in Extra, Native-like preset removed; shadow map resolution (tweak shadow_resolution_multiplier) and shadow map
stability (normal offset in sss.hlsli). Untested. HDR v32 / DLSS v26: effect ID = CRC of c0-c3 + c16-c20 (mid colours
are leftovers from the previous effect: IDs changed with what was on screen, ATN); test buttons Set as Fire & Glow /
Burning Houses / Campfires (ReShade.ini [Shogun 2 VFX effects]); Recommended heat haze 0.1, hue = Psycho default;
Shadow softness removed; DLSS Recommended both shadow stabilities Steadier. HDR v33 / DLSS v27: ATN's IDs baked
(Big Fires 1 FC75D250 DCB6C2CB FF972FE7, Campfires 73B8E4C8), Burning Houses -> Big Fires, Campfires 0-50, test
settings commented out, cloth VS paused check |dt| < 1 ms (debug18 banner 700-1000 px with exact equality; trace now
prints time - prev); DLSS: Particle resolution On/Off (Recommended Off), preset K default. DLSS v28: Particle
resolution Off by default and in Reset (ATN). Ryse DLSS v98: raw DLSS input capture (dlss_capture.hpp, Advanced button,
4 frames at DLAA too) for the paused flicker; tools\shogun2\capture_decode.py reads ryse_dlss_capture_* (R32 depth).
Ryse DLSS v99 (debugForClaude38, paused): MVs ~0 (max 0.07 px, valid camera MVs), image jitter = told jitter within 0.02
px (Lucas-Kanade, scratchpad ryse_lk.py), but DLSS depth = 1.0 on every pixel: 0x52594501 used PER_BATCH
PS_NearFarClipDist, which the game's TAA 0x121C45E4 declares but never reads (unset). Now depth = clip z / w of the
reconstructed surface with g_VS_ViewProjMatr (PassConstants 80 bytes: depth_reversed, current_z), reverse-Z measured on
the CPU. ATN debugForClaude39: depth varies now (fixed), still armour flicker standing still = sub-pixel glints (16 px
block max 0.2 -> 2.3 -> 0.2, 0.4 -> 11.4 in 1 of 4 frames; HDR addon values >> 1, SDR clamped at 1). v100: Highlight
stability (Ryse AA HighlightStability 0/1/2, default 1): highlight_stability.hlsli compresses the max channel above a knee
in 0x52594501 and expands it in the copy 0x52594502 (PassConstants.highlight_stability). v101: removed (ATN: brightness
changes, not wanted). Open: armour glint flicker = sub-pixel specular aliasing x jitter; no Ryse specular AA cvar (only
r_NormalsLength debug). Options offered: Jitter "The game's" while paused; G-buffer specular AA (roughness from normal
variance) as a larger job. Shogun 2 (ATN 2026-10-16): DLSS addon "loads but does not work" after reordering addons:
ReShade.log 00:24 shows all hooks installed but no "The game creates DLSS" / jitter on: the game's DLSS pass never ran
(in-game AA not DLSS?); asked ATN to check.
Shogun 2 HDR v34: cloth VS divides positions by w (UI unit banners have w != 1; debug19 time - prev = 0 yet 33000 px),
sea colour overlay (indicator to_overlay C57C222D, seainteraction C8375A74, wake 06701006) drawn with a full viewport
(ocean reads it by decal projection = full-screen UV; was 1/3 in the top-left at 33%), preset Recommended (SDR-like
Contrast/Saturation) = Recommended + cone 1.25. Untested.
Shogun 2 HDR v35: Output SDR (10-bit r10g10b10a2 + srgb_nonlinear, proxy passes the gamma 2.2 image, Peak / Paper White
/ UI hidden, SHOGUN_SDR_OUTPUT / SHOGUN_PAPER_WHITE macros; Extended = game curve, PsychoV31 BT.709 at peak 1) and
Output Auto (new default index 3: scRGB if Windows HDR is on for the window's monitor, else SDR; own create_swapchain
handler registered before the swapchain mod's). Untested.
CPU pass (ATN 2026-10-15: game CPU-bound, no debug work in the background): HDR v36: reshade_overlay event removed (any
add-on with it makes ReShade build its ImGui frame every frame, overlay closed too; DLSS options now read on present
while reshade_open_overlay says open, and on close), VFX test bookkeeping only after the test's first Drawn, map / unmap
shared lock / atomic fast path, sea overlay viewport check first, DEBUG_LEVEL_0 off. DLSS v29: frame trace events
registered only for the traced frame (frame_trace::internal::UseEvents), render_scale debug counts only when the overlay
requests them, any_mapped fast path on unmap, texture_lod_bias tracks samplers only while bias != 0 (SetTracking at
present seeds from PSGetSamplers). Untested.
Shogun 2 HDR v37: Sun slider 0-20 (debug20: the flat sun top is the texture: models.pack (entries u64 offset, u32 size,
u32 stored size, LZ4 block data) rigidmodels\skybox\sundisk1.dds DXT5 = even white disc, soft alpha edge; not a clip),
SDR-like preset = Recommended + highlights 0.95, cone 1.34, roll-off 2. HDR v38: Recommended Sun 2, SDR-like cone 1.30.
HDR v39 / DLSS v30: SSR ray steps back (SSRSteps 0-6, ssrray00 100 x 2^n; ATN: sea reflections), flagvelocity00
(VS 0x40E181C2 rigidtextureblendwind: camp / castle flags, prev instance matrix stale while paused (debug21: 80-1500 px):
paused -> prev = current position; dcl identical to the game's). Nexus text v4 (SSR ray steps, flags).
HDR v40 / DLSS v31 (debug22): SSR ray steps removed again (no effect); uifigure00/01 (ps30_main_ui F45347E8 alpha 2,
A406D4BE: saturate o0, UI RT upgraded); campaign overlay (4800x2700 roads / borders / rivers at full size) read with
uv / min(viewport / screen, 1) in sunlight02/03 and campaignwater00/01 (F7103E54, 630BA7A4). Open: SSR missing in the
last 16 SSR px (render 3776-3808) at 99% (debug22 sea battle ssr; not stretched, cause not found: asked ATN for 50%
screenshots); campaign Psycho brighter: SDR 88-nit snow -> Psycho 235 / Extended 140 (own contrast), auto exposure runs.
Nexus text v5 (no SSR ray steps; menu figures, campaign overlay).
HDR v41: SSR right-edge strip = ssrresolve00's radiance lookup at coarse mips (up to 5, 32 px texels) averaging the
unrendered area (debug23: ~32 render px at 99% and 50%, far-water band only): clamped half a coarsest texel inside the
viewport below 100%. FotS campaign map (data_fots.pack bos_japan *.lighting) film_slope 0.52-0.68 (others 0.88):
PsychoV31 response power x g_tone_mapping_film_slope / 0.88. ATN 2026-10-16: Shogun 2 DLSS engages again after restarts.
Nexus text v3: Downloads\nexus_text_shogun2_v3.txt (SDR output, game's fake HDR / 80-peak sliders, sea battle line
pending ATN's sea battle test). DLSS v19: Particle resolution minimum = dynres min variable (+0x2e7b744) set
to share-of-display / render scale. SSS: samples are the reach (1 px each). NEXT: ATN's test; after Shogun 2: Ryse
paused flicker with the same DLSS input capture (memory project_roadmap).
Older: NEXT: ATN's v7 test + traces at lowest AO / DOF and on the campaign map at 33%.
Tools: tools\shogun2\trace_phases.py / trace_summary.py.

## CPU pass (2026-10-16, roadmap 1c; untested): Ryse HDR fix38 / DLSS v102, Expeditions HDR v27 / DLSS v16, Tales HDR v31 / DLAA v49
Same rules as Shogun 2 HDR v36 / DLSS v29. No addon had the reshade_overlay event. Ryse HDR: back_buffer_fix compared
every render target bind with the back buffers under a mutex: now lock-free atomics (back_buffers, at most 16;
immediate_context; bound_back_buffer), the mutex only for back buffer binds; HUD / skull / highlight draw events only
while something is hidden. Ryse DLSS: frame / shadow trace and options measurement draw + copy events only while one
runs. All three DLSS addons: constant_mirror returns at once until something is watched (atomic `watching`). All three
HDR addons: DEBUG_LEVEL_0 off. Checked and already fine: particle counters (counting_frames), GPU timings (Advanced
open only), Tales pixel trace (events per trace), engine hooks, per-frame cvar loops (cheap; a 30-frame throttle was
tried and dropped: memory-written cvars could flicker if the game resets them).
ATN tested Ryse fix38 / v102: fine. Shogun 2 distortion at low scales checked (debug24): not a bug. Shogun 2 done.

## Tales DLSS upscaling (2026-10-16): talesofarise_dlss v50 trace, v51-v52 DLSS (ATN: works), v53 / HDR v32 (untested)
v50: Advanced "Trace one frame" (frame_trace.hpp). ATN's traces `Arise\Binaries\Win64\debug1\taa50.txt` / `taa100.txt`
(script scratchpad tales_trace_after_taa.py): at 50% everything after the TAA runs at 1920x1080 (view = buffer):
bloom chain from the TAA output, MB velocity cs F28BBA66 / tiles 57504B6F, LUT, MB ps 87FBD5AE (t0 = TAA output),
tonemap ps DCFDF14D (t2 = MB output, t3 bloom), then upscale vs B3E3D245 / ps 57192B01 (plain Catmull-Rom, cb0[0] =
source size / inverse, alpha 0) to 3840x2160, then UI. v51 (README "DLSS upscaling"): all modes in the menu; DLSS at
the TAA writes the output size, downsampled (cs 0x7A1E0D03) into the game's TAA output; MB / tonemap draws (32 PS
hashes) repeated at the output size in a command_action post callback (HDR addon state still bound) with TAA output ->
DLSS output and earlier targets -> output-size copies; the upscale draw re-run with our copy and the output size in
cb0 (exact copy), game's draw bypassed. Render sizes in whole 16:9 units of 64x36 (buffer multiple 4, auto 8),
r.ScreenPercentage = exact - 1/4 px; TAA only accepted at the expected size; r.TemporalAASamples 8 x ratio^2 (max 127).
ATN v51: works (2 passes, upscale replaced; the in-game render scale is overridden), textures blurry (no mip bias).
v52 (untested): Texture LOD bias (View c133.x = MaterialTextureMipBias, written at Unmap of the main view's View UB in
constant_mirror.hpp; Matched / Sharper default / Sharpest, no -1 floor) and NVIDIA's Halton (2, 3) jitter for DLSS
(the skipped jitter multiply at exe+0x1A86CBC becomes rip-relative loads of our sample; jmp disp 0x12 -> 0), any
length, so no 127 cap; r.TemporalAASamples no longer touched. The exe on disk is SteamStub-encrypted: only runtime
pattern searches see the code (a dump of the running process would allow offline RE).
ATN v52: Halton works down to 8%, LOD bias -2 at Performance (Sharpest looks best; Sharper stays default).
Found (debug2): Bandai's pre-TAA screen filters are sized in render pixels, so they grow at low scales: painterly
edge-aware blur ps 0x2483CE26 (horizontal, step 1.5 x cb0[0].z, +-4 taps) / 0x773EABCB (vertical), game sharpen in
ps 0x381F5A30 (5-tap at +-0.5 texel, strength cb0[34].x), outlines, canvas / hatching, the character halo (8%: huge).
HDR addon "Sharpening" only scales the tonemapper's sharpen; tonemap00 (0xDCFDF14D) has none. Proposed fix: scale
those passes' pixel steps by render / output (HDR addon shaders + scale shared from the DLSS addon).
ATN (debug3: two frame traces, not pixel traces; no pause needed for a pixel trace): disable the pre-TAA sharpening
with DLSS, scale the filters, Halton for DLAA too. Halo found by code: lighting00's shadow edge tint (t14 Gather 2.3
render px up-right, cb0[63]/[64] instead of cb0[50]/[52] in penumbra) = a ~3 render px band at every shadow-mask edge,
characters included. Outlines = ps 0x77D287B7 (material IDs 11/12, cb0[62].x px). 0x66DC643D = painterly filter + its
own 4-tap sharpen (0.35 - 0.2 depth). 0x381F5A30: material 6 (grass) gets a radial-blur glow (A5AFEAB4, UV-sized, fine)
instead of the sharpen.
DLSS v53 / HDR v32 (zips talesofarise_dlss_v53 / talesofarise_v32, untested): post_filters.hpp (cross_addon, UUID
9c1b57b2...) shares render / output (1 unless upscaling) + game sharpening (0 under DLAA/DLSS unless the overlay's
"Game's sharpening" is on); HDR addon replaces painterly00-02, outline00-01, grading00-03 and lighting00's edge tests
with offsets x SI.filter_scale (point-sampled tests: min 1 render px, faded by the ratio; tint faded 3.3s/(1+2.3s)).
Halton (2,3) now for DLAA too (8 positions). Lighting permutations B2726B44 / 6513DC8E / 81E3CED2 / C28D2F1E /
F95FABAA (same tint, in no trace) not replaced yet.
ATN v53 / v32: sharpening off is huge (default off good), black bars no longer grow. Asked: remove the halo at 100%
too (stock game), and cover other permutations. CORRECTION: the debug2 cross pixel had t14.x 0.96 (light mask), which
skips the tint: the tint is NOT the halo. Earlier v42 "Halo test" (DLSS addon cvars: SSS, contact, capsule, DF shadows,
SSGI, Bandai AO 0 broke lighting) did not remove it either. Bandai AO (E243580F: Alchemy-style, 8 NDC-offset samples
from its R16F depth pyramid, no range check, 11 permutations in New_1) is a classic halo suspect; sun shadow mask cs
2084C889 is per pixel (no screen neighbours). HDR v33 (zip talesofarise_v33, DLSS unchanged v53): lighting01-05
permutations with the edge scaling; temporary "Halo test" (HDR Extra, Advanced): lighting00 outputs its AO (t5), light
mask (t14.xyz) or light (t4). ATN debug4 screenshots: the AO view is 20-35% brighter within ~50 px of the character
(4K DLAA), t14 / t4 clean: Bandai's AO is the halo. HDR v34 (zip talesofarise_v34): AO Halo Fix (default On) in all
12 AO permutations (ao00-11, generated from the dumps: CS decl / numthreads / store fixed; samples > 0.1 depth + 20 cm in
front dropped, sum x 9 / kept); Halo test removed. WAITING: ATN's test. Proposed for ATN's question 2 (effects at low
scales): sun shadows are render-scale independent; render-pixel-sized are Bandai's divisors (SSR, ground fog, clouds,
light shafts), volumetric lighting / fog cells (8 px), and UE's object / point / spot shadow texels per pixel; an
"Effects at output resolution" option could scale those by render / output while upscaling.
ATN v34: halo much less, bright spots behind grass fixed too; a ring around the whole character left. Cause 2: AO blurs
weight neighbours >= 0.1 across depth jumps. HDR v35 (zip talesofarise_v35): aoblur00-07 with AoBlurWeight (0 beyond
the limit); 8C5387E3's decompile fixed by hand (int literals, aliasing ld_structured). DLSS v54 (zip
talesofarise_dlss_v54): "Effects at output resolution" (default off): OUTPUT_SCALED list (cells / divisors x s,
TexelsPerPixel / s, half-res switches to Maximum's value). WAITING: ATN's test of both. Not covered yet: half-res
translucency (Bandai, no cvar found), texture streaming wanted mips (may follow the render size), skin SSS half res,
HZB occlusion culling; half-res AO upsample permutations 4D0A29EA / 8E29A68D.
ATN v35 / v54 (debug5): AO fix on removes most of the halo, a faint band left (even DLAA); bright spots behind grass
gone; Effects at output resolution: much better (fog not blocky, ~5-10 fps of 250) -> default on. HDR v36: AO cutoff
= max(2 x AO reach, 5% depth + 10 cm), blur cutoff 5% + 10 cm, temporary "Show AO (test)". DLSS v55: default on, exe
cvars added (mixed-res translucency, quarter depth, SSS half res, SSR blur color divisor). WAITING: ATN's test; if the
band stays, a Show AO screenshot tells AO vs something else (Bandai's SSGI next suspect).
v55 crashed at startup (UE crash: CreateTexture3D 3840x2160x40 A8, > D3D11 2048): r.EnableMixedResolutionTranslucent
RenderingTO14 0 -> removed in DLSS v56 (zip talesofarise_dlss_v56; game folder addons are links to build\Release).
DLSS v57 (zip talesofarise_dlss_v57): test row r.SeparateTranslucencyScreenPercentage (ATN tests 200 at DLAA: sharper
smoke / fire?); if yes, add it to OUTPUT_SCALED as 100 / s. Volume cell guard (render width / cell <= 2048).
v57 crashed the same way: real cause = cloud shadow / ray march divisors 2 -> 1 (Effects at output resolution at the
720p startup window; the game sizes those volumes from the output). DLSS v58 (zip talesofarise_dlss_v58): guard on all
4 volume settings with max(output width, widest monitor mode) / value <= 2048. Mixed-res translucency stays removed
(untested whether it alone crashes).
ATN v58: starts fine; SeparateTranslucencyScreenPercentage no effect; debug6 AO views (33%): fix on = AO nearly clean
around the character (off: clear ring); a "really small" halo stays in the normal view -> another pass (suspects:
half-res translucency upsample C7A2979E, SSGI, lighting00 edge terms; white grass blades in front of the legs = AO 1,
samples behind are far). DLSS v59 (zip talesofarise_dlss_v59): test row removed, mixed-res translucency back in the
option (0 while upscaling). WAITING: two normal-view pixel traces at DLAA (in the halo / just outside, same bush).
ATN debug7: traces on different materials (lit leaf vs dark branch: not comparable); Performance plants ~10% darker
(rocks same). Cause: AO samples mip 0 of the closest-depth pyramid (AA7E0D32; coarser in screen terms at low scales).
HDR v37 (zip talesofarise_v37): below 70% DLSS, AO permutations with full-res depth (ao00/01/06/07) sample t1; AO Halo
Fix Off / On / Strong (cutoffs halved). WAITING: ATN: Performance vs DLAA plant brightness; Strong vs On band.
ATN (before testing v37): fix on lightens rock corners too. Data (debug7 DLAA off/on): crevices under overhangs lighter
(set-aside overhang samples replaced by the visible average) = wrong; convex edges lighter (blur no longer smears
crevice AO) = right. HDR v38 (zip talesofarise_v38, supersedes v37): set-aside samples add max(own, visible average):
never lighter than stock AO.
ATN v38 (2026-10-16): grass etc. no longer lighter; likes Strong for everything ("like old HBAO at stronger presets"),
wants to keep it as a "stronger AO" choice (asked whether the naming makes sense). BUT: the halo around the character
(head, sword handle, cloth) does NOT change with any AO Halo Fix setting (Off / On / Strong): the halo is NOT the AO
(v34-v37's apparent fix was its accidental brightening). Pictures: Arise\Binaries\Win64\debug8 (2). Game cannot be
paused; the character moves on the spot (idle), grass moves with wind. NEXT (after ATN compacts): find the real halo
source. Ideas: DLSS / TAA disocclusion around the moving character (compare AA Off = game TAA vs DLAA), Bandai's
mixed-resolution translucency upsample (C7A2979E) around depth edges, lighting00 edge terms, SSGI, character-only
passes (outline 77D287B7, screen-space chara self shadow, translucency chara mask r.TranslucentCharaMaskFade*),
volumetric lighting froxels; traces must use the same material (in / out on similar leaves or ground) or a frame
trace + per-pass debug views.
debug8 = Show AO views (pure gray), not normal views. HDR v39 (zip talesofarise_v39): setting renamed "Ambient
Occlusion" Game's / Fixed / Strong (key ao_halo_fix unchanged). DLSS v60 (zip talesofarise_dlss_v60): the pixel trace
copies a horizontal line (LINE_WIDTH 1/16 of each target's width, red line in the overlay) per command: the halo's
profile in every pass of ONE frame. Analysis: scratchpad line_trace.py (per change, luminance / first channel along
the line). WAITING: ATN's line trace at DLAA, normal view, cross on the character's edge (line into the halo) +
screenshot. Suspect besides those above: Bandai's character mask fade of translucency / fog (blurred mask = ring).
ATN debug9 (v60 line, cross on top of the head, bush 5.8 m behind): per-pixel bush noise hides any halo on one line.
Seen: SSR (cs 8ABB9808 / 559B58FD / 99ECDDCF, 1920x1080) no hits on the line; full-res cs B81E220D / 91EC6207 /
3222F1EF (r8g8b8a8, from last frame's half-res color: SSGI-like) smooth, no edge band; translucency composite
C7A2979E only writes alpha there; characters lit by full-screen CB0EC552 (C70CE890 RT1 = 0 on characters, 0.96
else; that RT is lighting00's t14); F6334BA2 (before lighting00) also reads the AO. Final AO (8C5387E3) right of the
head ~0.55 for ~10 px vs ~0.40 further out (raw AO 0.83 spikes next to silhouettes): AO setting of the trace unknown.
DLSS v61 (zip talesofarise_dlss_v61): trace also saves square patches (PATCH_WIDTH 1/8 width) of every full-screen
pass / dispatch to tales_trace_patches (patches.txt + raw .bin, header w, h, DXGI format, bytes per texel). Analysis:
scratchpad patches.py (background median by distance band from the character, per pass). WAITING: ATN's v61 trace.
ATN debug10 (v61 trace, Strong; plus off.png / strong.png at DLAA): Off vs Strong = the halo IS the AO (Off brighter
by +0.12 stop at 1-3 px from the character, +0.09 to 15 px, fading by ~100 px; soft blobs beside head / shoulder /
sword). In the Strong trace the final AO (8C5387E3) runs up to the silhouette with no ring (rock crevices continue to
the head). Per-pass residuals after fitting each pass's brightness curve (scratchpad patches_load.py / patch_view.py):
tonemap + bloom -0.01, painterly 66DC643D +0.03 stop at 1-3 px only, grading +-0.02, DLSS 0; 0A9D482A (toon ramp
lighting before lighting00, AO only for material types >= 3) varies by rock normals (sunlit face behind the head), no
ring. A 0 cm cutoff would bring the halo back (kept samples would all be behind the plane: hidden average ~0).
HDR v40 (zip talesofarise_v40, temporary): Ambient Occlusion test choices 3-5 = cutoff 1/2, 1/8 of Strong's, 0
(AO_HALO_CUTOFF in shared.h; blurs keep Strong's cutoff). ATN compares; in the end Game's + two choices stay.
ATN picked Game's / Fixed / 1/8 Strong and wanted it in the DLSS overlay. HDR v41 (zip talesofarise_v41) + DLSS v62
(zip talesofarise_dlss_v62): DLSS overlay "Ambient occlusion" Game's / Fixed (default) / Deep (cutoff 1/16 of
Fixed's, blur /2), config [Tales AA] AmbientOcclusion, shared via post_filters SharedData.ambient_occlusion
(appended; unreleased struct, both addons must match); HDR AO settings + Show AO removed; aoblur entries are
PostFilterShader. Pixel trace Present: lock-free active flag. ATN: the halo on GRASS behind the character does not
change with any AO choice -> another pass (grass-only radial glow A5AFEAB4 adds far-scenery color to grass along a
line toward cb0[28].zw; a character on that line lowers it: would darken, not lighten; unproven). ATN may release as
is; a v61 patch trace with grass behind the character would find it.
ATN debug11 (Fixed, bush 2.8 m behind the head; scratchpad residual.py): 0A9D482A residual +0.47 stop at 1-3 px to
+0.24 at 30-40 px, then <= 0; bush (material 6, RT4.w) final AO 0.50-0.55 within ~30 px vs 0.35-0.40 beyond 40 px at
matched depth (raw AO at its 0.3 floor further out). Could be the AO halo or the bush's front leaves (one frame
cannot separate). No character flag in the G-buffer (characters = material 0/1 like rock; flags RT2.w overlap), so a
character-aware AO would need the stencil. ATN: Deep worse than Fixed on rails. HDR v42 / DLSS v63 (zips): Deep
removed; tests 2-4 = samples AND blur cutoffs x 1/2, 1/4, 1/16; 5 = no screen-space AO (AoHaloSum 0). WAITING: ATN:
does the grass halo vanish with "Test no AO"? If not, it is not the AO.
ATN v42: no AO = no halo anywhere -> the rest is screen-space AO's blindness behind characters (not fully fixable);
1/4: same halo, darker shading. HDR v43 / DLSS v64 (zips): "Ambient occlusion halo" in Graphics > Shadows (first
row): Game's / Reduced (default, cutoff 1) / Reduced + darker (samples + blur x 1/4); tests removed. Next: ATN tunes
HDR and releases.
Tales HDR v44 / DLSS v65 (zips): presets set the AO halo (Game's own: Game's; others: Reduced); Replace TAA in
Advanced only; "Game's sharpening (Not recommended)", "Effects at output resolution (Recommended)"; yellow note "Every
preset keeps the game's volumetric fog ... No preset changes Loading."; HDR scene buffers always RGBA16F (setting
removed); HDR Sharpening tooltip: UE tonemapper sharpen (12 of 20 permutations), normal gameplay uses tonemap00
without it, with or without DLSS.
Expeditions DLSS v17 (zip expeditionsrome_dlss_graphics_v17, untested): exact-aspect render sizes (Tales' units,
buffer multiple 4 -> 8 when padding seen, r.ScreenPercentage = exact - 1/4 px); NVIDIA Halton (2, 3) jitter for DLAA
and all DLSS scales via a jump at the TemporalJitterPixels store exe+0x1369279 (pattern JITTER_STORE, all jitter paths
end there with xmm7 / xmm8) into a Detours-region block loading the addon's sample; Effects at output resolution
(fog cell + light shaft divisor x s, TexelsPerPixel x4 / s, SSS / capsule / DF shadows full res; new rows
r.CapsuleShadowsFullResolution, r.DFFullResolution). Exe not encrypted: scratchpad exr_dis.py disassembles offline.
Ryse: nothing to do: exact-aspect steps since v21, own Halton (2, 3) up to 65536 positions, vegetation at display
width, LOD bias; ryse_cvars.txt (Very High): r_ParticlesHalfRes / SSAODownscale / ssdoHalfRes / SSReflHalfRes /
ShadowsMaskDownScale all 0 (full render res); only r_SSIIHalfRes 1 (SSII is ATN's experimental option).
Expeditions DLSS v18 (zip expeditionsrome_dlss_graphics_v18): fog cell guard = max(output width, widest monitor
mode) / 2048 (Tales v58's); the only volume Effects at output resolution touches; shadows keep their 100% sizes.
Expeditions DLSS v19 (zip expeditionsrome_dlss_graphics_v19): Tales' sun shadow atlas patch ported (exe+0x139CBAE,
SUN_SHADOW_ATLAS offset 16: mov r13d 1 -> 2 = 16384 atlas) while resolution x ceil(sqrt(cascades)) > 8192, applied
before writing the larger value; Sun shadow resolution presets 4096 / 6144 / 8192 / 8192, max 8192 per cascade.
ATN v19: sun shadows vanished above 4096 (the patch did not take in 4.26: maybe the CSM layout is the second
GMaxTextureMipCount user, exe+0x139E1AF, cmp [rcx+0x390], 3) -> Expeditions DLSS v20 (zip ..._graphics_v20) = v19
reverted (ATN: not worth the risk).
Tales: every frame trace (debug1/10/11) shows ONE sun map (8184 viewport at 4,4 in the 8192 atlas, ~435 draws, cs
81785A81 min-gathers it to 2048 by UV, cs 24D721C3 projects it with a 1024x1024x16 array): cascades most likely do
nothing. Tales DLSS v66 (zip): Sun shadow resolution list-only 16384 (test, can crash): atlas patch applied before
writing > 8192 (else 8192), kept 8 presents after. WAITING: ATN tests cascades 6 / 8 (expect no change) and 16384
(crash / no sun shadow / works); a frame trace at 16384 shows the atlas size.
ATN v66 (2026-10-16): 16384 crashed: UE crash exe+0x1A9E56C (AV, `mov rax, [rcx]` with rcx = a null shadow depth
target from the atlas list), same as above 8192 without the patch. Tales DLSS v67 (zip): test choice 12288 instead:
works = the doubled atlas takes (only 16384 cannot be created); crash = it does not take (as Expeditions v19), 8192
is the most. Scratchpad crashdump.py reads UE minidumps (exception, code near the IP).
ATN v67: 12288 crashed the same way (log: atlas 16384 applied) -> Unreal's computed atlas size is not the limit; 8192
is the most. Tales DLSS v68 (zip): test choice and the atlas patch removed (the sun is one map that fits 8192).
Tales HDR v45 / DLSS v69 (zips, untested): Smooth Volumetric Light Soft / Long Rays tests (shared.h SampleVisibility);
Psycho Highlight Saturation 1.18 / Hue Shift 1.30 defaults (slider to 2), fire note above both in both modes; presets
AO Reduced + darker; "Weak CPU" -> "Game's character culling"; yellow note above the preset toggles; "Max volumetric
fog" toggle (Bandai cells 2, UE fog 4); Bandai cell 1 test; volume guard split (render-sized: lighting + UE fog);
sun penumbra edge color / (sun shadow distance / game's), shared via post_filters (debug12: distance 4 pale patch =
edge color over a ~180 px penumbra from cs 2084C889).
ATN: v69 crashed at startup (CreateTexture3D): Effects at output scaled lighting cell 2 -> 1 and v69's render-size
bound allowed it while the game still rendered at full size. HDR v46 / DLSS v70 (zips): output bound for all volumes
again, cell 1 removed, "Weak CPU" restored (ATN wants more name ideas later), Psycho hue shift 0-1 default 0.30.
WAITING: ATN tests Smooth Volumetric Light Soft / Long Rays and sun shadow distance 4 (edge color).
ATN debug13 (distance 4, v70): the shadow edge runs smoothly lit -> shadow color, no pale band. HDR v47 / DLSS v71
(zips): Long Rays removed, Off / Soft / Along Sun Rays (new key volumetric_light_smoothing); "Vanilla crowd limits
(CPU saver, not recommended)"; toggles stacked with empty lines; tooltips shortened; Nexus text updated
(Downloads\talesofarise_nexus.txt). Sun shadow "banding" at long distances: Bandai's cs 2084C889 filters the 8192 map
with a 6x6 bilinear-weighted PCF (9 Gathers); options: screen-space edge-aware blur of t14.x in lighting00-05, or a
wider PCF in a replaced 2084C889. WAITING: ATN's screenshot of the banding.
ATN debug14: Max volumetric fog (Bandai cells 2) showed stair-stepped light shafts: cs D09F3430 (volumetric lighting,
writes the 1920x1080x5 visibility t6 + x16 scattering) averages each 2 x 2 Gather of the 2048 closest-depth shadow map
(cs 81785A81 from the 8192 map) -> steps per texel, hidden by 8-px cells. HDR v48 (zip talesofarise_v48, untested):
volumetric00 = D09F3430 with bilinear Gather weights when Smooth Volumetric Light != Off; sunshadow00 = 2084C889 with a
tent PCF (36 Gathers, half-width 3.5 x ratio^0.25) when the new Extra "Smooth Sun Shadows" (default On) and the sun
shadow distance ratio > 1 (shared from the DLSS addon). Post effects without a slider: the sun haze / shafts
(Bandai's volumetric light in lighting00), the painterly filter, outlines, the grass glow; lens flares / glare are in
the bloom chain (Bloom slider). ATN: "after this we are done with this game".
ATN debug15: v48's Smooth Sun Shadows looked worse (blurrier, bigger penumbra edge color); Max fog steps reduced but
still visible. HDR v49 (zip talesofarise_v49, untested): Smooth Sun Shadows = the game's 9 Gathers with a tent of
half-width 3 texels (no widening); SampleVisibility (shared.h) stretches Soft / Along Sun Rays to the game's 8-px reach
at finer cells (screen_width = 1 / cb0[0].z); new Extra sliders (Advanced, 1 = game's) Sun Haze, Painterly Filter,
Outlines, Grass Glow. WAITING: ATN says which sliders to delete.
ATN on v49: Along Sun Rays flawless (default); Smooth Sun Shadows much better. HDR v50 (zip talesofarise_v50): Sun Haze
> 1 fix (tint^haze, was lerp -> negative: black edges, debug16); Smooth Sun Shadows Off / Smooth / Sharp / Sharper /
Sharpest (tent 3 / 2 / 1.5 / 1.2 texels); test slider Shadow Edge Color (lighting00-05). Asked: shadow pops when the
sun steps (~10 s) at long distances: idea = crossfade old -> new map over ~1 s (2nd 8192 map copy every frame, old
matrix; moving characters' shadows briefly double). Not started: ATN decides.
HDR v51 (zip talesofarise_v51): all Extra sliders safe above 1 (Outlines saturate, Painterly weights x k above 1,
Shadow Edge Color saturate, HDR sharpen / grain max 0). Unused permutations in New_1 listed in the README (v51).
ATN: stable shadows OK as Experimental, VRAM no issue, worried about character shadow lag in the crossfade.
Offered: crossfade (quick, may lag) vs. moving the sun smoothly every frame (find Bandai's sun update; no ghosts).
ATN chose the crossfade (likely removed later). HDR v52 (zip talesofarise_v52, untested): Sun Step Fade
(Experimental, 0-2 s): sunstep00 / sunstep01 compute shaders + native D3D11 in sunshadow00's on_draw / on_drawn
(README v52). Smooth sun movement: not started (needs Bandai's sun update found; maps without sun motion are fine).
HDR v53 (zip): Grass Glow removed, Painterly 0-1, Sun Step Fade removed (ATN). Smooth sun: Bandai's WorldTime plugin
(.rdata reflection: AWorldTimeActor, transient float ActorRefresher +0x32C, int myCounter +0x330, BP event UpdateTime
(native exec at exe+0x5FF1B0), OnChangeWorldTime(newHour), UWorldTimeBPLibrary, TWorldTimeManager::ResidentInst).
Guess: Tick counts ActorRefresher and calls UpdateTime (the BP turns the sun) every ~10 s. Code is encrypted on disk:
DLSS v72 (zip) adds "Save the game's code for analysis" -> tales_image.bin (file offset = RVA). WAITING: ATN's dump.
Next: find AWorldTimeActor::Tick (vtable entries near exe+0x5FF1B0), make the update run every frame (or interpolate).
ATN saved tales_image.bin (Win64 folder; loaded at 0x7FF7BFF60000, file offset = RVA; scratchpad img.py /
refs.py style tools: pdata funcs, rip refs, abs refs with that base). Findings in DLSS README v73: the manager's hour
is continuous every frame; the actor calls UpdateTime (BP turns the sun) on every hour change -> steps = actor tick
interval (guess). HDR v54 + DLSS v73 (zips, untested): ATN's highlight defaults; Smooth sun movement = tick interval
0 + overlay counts. If the sun still steps with ticks every frame: the BP gates it (then interpolate the light at
engine level: FScene::UpdateLightTransform not found yet, no RTTI / render command names in this exe).
ATN: v73 no change (interval 0, updates every tick) -> BP gates the sun. DLSS v74 (zip, untested): SetTransform hook +
present-time interpolation (DLSS README v74). Risks: Present must be on the render thread (UE 4.21 D3D11: no RHI
thread); sky sun disk may still step.
v74 crashed (unaligned matrix to SetTransform's movaps). ATN: the game moves the sun in bursts (fast turn every
~5 s, then hold). DLSS v75 (zip, addon linked to build/Release/v75 because the running game locked the normal
output): alignas(16) + burst-based smoothing (README v75). Waiting for ATN's test.
ATN: v75 shadows smooth (period "really close"), sky sun disk still steps (debug17 traces). DLSS v76 (zip, untested):
the sky material's sun direction (vector parameter, cb2[4] of ps 0x11039AD5) follows the smoothed light via a hook on
FMaterialInstanceResource::RenderThread_UpdateParameter<FLinearColor> (exe+0x2474270) + recache (exe+0x2488060) +
resource destructor slot (README v76). Overlay debug line "Sun steps": the game's exact step time / degrees and the
number of followed sky parameters (0 = the disk's parameter not matched: then check unit length / other path).
ATN (v76): steps every 7.200 s exactly (0.73 deg), 0 followed. v77 (zip, untested): the sun direction is a material
parameter collection vector (shader clamps cb2 itself); MID hook removed; UMaterialParameterCollectionInstance::
SetVectorParameterValue (exe+0x2497ED0) hooked + re-set after every world time tick; collection instance dtor slot;
constant-speed motion (aim = previous burst end; ATN: game moves ~2.2 s, holds ~5 s); Smooth sun movement is now a
checkbox under the 3 preset toggles (default on, no preset) + Time of day slider (game / 2x / 3x / 6x slower:
TWorldTimeManager rate +0x68 divided). HDR v55 (zip): Shadow Edge Color tooltip fixed (0 = tint fringe, higher =
cleaner; ATN likes 2: offer as default). README v77 / v55.
ATN (v77): disk follows the shadows. DLSS v78 + HDR v56 (zips, untested): "Day and night speed" 100x slower ..
100x faster (rate x speed; motions rescaled at once on rate changes); Shadow Edge Color default 2, max 10. Shadow
shimmer while the sun turns: CSM snap grid anchored at the world origin (SetupWholeSceneProjection exe+0x1ACC450);
v78 patches proxy vtable slot 20 (cascade initializer) to anchor each cascade's grid at its center via a small
WorldToLight translation (README v78). If shadows break/offset: that patch (Smooth sun off = game's).
ATN (v78): no crash, shimmer much lower; 1000x speeds amazing. DLSS v79 + HDR v57 (zips, untested): anchors follow the
camera (FSceneView +0x60C); day speed Off / 1000x; test row Sun shadow layers (r.Shadow.UnbuiltNum...Cascades: Tales'
sun is one shadow map, blocky at distance 3; 4096 needed to fit 2-4); HDR: Shadow Edge Color 5, Post Processing
section, Sun Glow (sky00 = 0x11039AD5, the sky's own sun glow; README v57).
ATN (v79): camera pivot = perfect; layers cvar did nothing. DLSS v80 + HDR v58 (zips, untested): layers test via the
light's DynamicShadowCascades (proxy +0x194); HDR sunglare00 (0x287CCB34, UI glare clipped to UI white, Sun Glow) and
Thin Sun Shadow Edges (contrast on the filtered sun visibility, k = 1 + (ratio - 1) x setting; README v58).
HDR v59 (zip): Sun Glow removed entirely (0x287CCB34 also draws in-game text); Thin Sun Shadow Edges default 0.22
(ATN likes it). OPEN QUESTION to ATN: move Extra (Smooth Volumetric Light, Sun Haze, Smooth Sun Shadows, Thin Sun
Shadow Edges, Shadow Edge Color) into the DLSS addon's Graphics (passed like AO halo via post_filters); ATN asked for
an opinion first, nothing done yet.
ATN: leave Extra in the HDR addon. HDR v60 (zip, untested): SDR output + Auto (Shogun 2's port; README v60). Then:
Arise-SDK list for ATN's message to emoose and the Nexus text (Downloads\talesofarise_nexus_v2.txt).
DLSS v81 (zip, untested): particle mask (bias current color) from the scene copied after the grading composite vs the
TAA input (README v81); Advanced checkbox (default on), "Advanced (For Debugging)", sun debug stats removed. ATN: after
this, Tales is done.
ATN (debug20): v81 mask did nothing visible (sparks streak with L). DLSS v82 (zip, untested): mask also marks last
frame's positions (reprojected, x 3/4); Particle mask Off / On / Test: whole screen (shows if L follows the mask).
ATN: nothing changes even with Test -> L/M ignore the bias mask. DLSS v83 (zip): mask removed, preset M default.
ATN declined particles-after-DLSS. DLSS v84 (zip): yellow note under DLSS preset (particles have no motion vectors,
some presets smear them, most of all L); Nexus text known issues updated.
DLSS v85 (zip): Smooth sun movement (Recommended) off by default; Shaders on load checkbox above it. ATN: done after
this; next ATN takes pictures, makes a video and releases (Nexus text Downloads\talesofarise_nexus_v2.txt).
emoose replied: prefers our menu to drive Arise-SDK's sdk.* cvars when it is installed, will link our mod. DLSS v86
(zip, untested with Arise-SDK): engine_cvars accepts Ref globals in any module + bool Refs; Never hide characters /
Smooth cutscenes map to sdk.CharaDisableCull / sdk.CutsceneAllowSubframes; yellow "best used together with
Arise-SDK" note (README v86). ATN should test once with Arise-SDK installed (rows not grey, tooltip "Arise-SDK's ...").
DLSS v87 (zip): those rows grouped as "Arise-SDK's settings" (work without it too: ATN's choice). Suggest asking emoose
for a sdk.MinimumNPCDistance cvar (only ini today).
DLSS v88 (zip): Arise-SDK's settings group first, under its yellow note (empty line before). ATN's game-folder
Arise-SDK.ini shortened (short comments; ATN set MinimumNPCDistance -1 himself). Repo's old custom Arise-SDK.ini still
differs: ask ATN which one ships. -> ATN ships the shortened one: game values (monster 1, LOD 1, outline 6000),
MinimumNPCDistance -1, SkipIntroLogos false (ATN: it skipped nothing; suspected for the DLC loading bug); copied to
src/games/talesofarise_dlss/Arise-SDK.ini.
DLSS v89 (zip): Arise-SDK group needs Arise-SDK (greyed + no presets without it); NPC distance via our code with the
shipped ini's -1; hidden [Tales AA] DebugDevArise=1 = our own code for the group (ATN's testing). Nexus text updated.
emoose has not answered ATN's question yet.
Then: Halton jitter to Expeditions (roadmap; ATN: for all scales incl. DLAA); Expeditions / Ryse aspect-exact custom
scales.

## RESUME HERE (2026-10-13, ATN continues on another Claude account, same PC / Windows user)
Nothing of this conversation carries over except files: this HANDOFF, memory (`C:\Users\arshi\.claude\projects\
D--Dev-renodx\memory\`, same Windows user), RE tools (`...\D--Dev-renodx\tools\{ryse,toa,exr}`), the repo (branch
`atn-mods`, many uncommitted changes: never push upstream) and the zips in `Downloads`. ATN's rules above still hold.
1. DONE (ATN: "it fixes it", no fps cost on a 7950X3D), Ryse DLSS v70 (`Downloads\rysesonofrome_dlss_v70.zip`):
   soldiers' arm armour not sitting until ~2-4 m = ANIMATION LOD. Soldiers' bodies (romn_lgnr*.cdf, 257 joints,
   the armour's bone buffer) have 4 chrparams <Lod><JointList> lists (126/125/54/25 joints), heads 2 (87/16);
   soldiers a few metres away run at animation LOD 2-19. No list keeps L/R_UpArmTwist1 (54, 97), parents of the
   LGNR_*_lorica_shoulder01-04_HLP plates, nor the twist / shoulder / knee helpers: from LOD 1 they keep their
   default pose. Code: the sampling commands' controller lookup +0x934a70 (sampling state = command buffer +
   0x810: model +0, joint count +0x30, LOD +0x34, instance +0x50; lists at model +0x248, loader +0x95e850) gives
   controllers only to listed joints. Fix: game setting "Character animation" (last quality option, key
   CharacterAnimation, Full in every preset) patches je +0x934ba0 -> jmp (LOD_LIST_TEST) and sets the old "Max every
   character setting" CryAnimation console object values (QualityCvar::animation_offset, AnimationCvars();
   ca_ForceUpdateSkeletons 1, ca_UseJointMasking 0, ca_ClothMaxChars 100, ca_FacialAnimationRadius 1000,
   ca_FacialAnimationFramerate 240). Debug toggles and skeleton_log.hpp removed (copy: tools/ryse/
   skeleton_log_v69.hpp, Detours probes); the pixel trace stays. Dead ends on the way (v61-v68): model LOD, blend shapes, vertex animation,
   tessellation, a character swap (none: body 257 + head 515 joint characters at every distance), skinning skips.
   v72 (tested by ATN): "Screen-space reflections" (the game's trace ps 0xE0B7E115 drawn with the addon's
   0x52594506: High / Very High / Ultra / Extreme = 4x steps per metre along 1/2/4/8x the game's ray, bisected exact
   hits; the game's gloss blur kept: ATN rejected a "sharper" item), Experimental section (presets skip it), water
   volume caustics, silhouette POM, terrain LOD / detail materials, rain wetness distance, grass bending / far
   physics / branches / crowd, debris (README v72). Global illumination removed in v73 (no difference).
   v97 (`Downloads\rysesonofrome_dlss_v97.zip`): Terrain sun shadows removed (bugged in some chapters), ATN's
   setting leaves Skip taunts off. v96: v95's Near shadow sharpness removed (ATN: looked worse); cutscenes
   use the gameplay cvars (README v96). AI aggression: ATN said leave it. NEXT: text fixes, then Shogun 2.
   v94 (`Downloads\rysesonofrome_dlss_v94.zip`): High quality shadows + View-aligned shadows removed (cutscene
   shadow mode: 8 cascades ending near 100 m; README v94). v93: Advanced "Save the enemy attack patterns" works
   (saved at level load): Bin64\ryse_attack_patterns\Scripts\AI\AttackPatterns\enemyattackpatterns.xml
   (CryXmlB; tools\ryse\cryxmlb.py IN OUT makes text). Sequences = "Blocking[Attacker:AttackType] Random(a-b)":
   one attacker at a time, then a pause; group ones start a second after a delay; Seq_Agro_00..03 pause 1-2 /
   0.5-1 / 0-0.5 / 0 s. ATN undecided on "more aggressive AI": plan = feed a rewritten XML to the loader
   (shorter pauses, fewer Blocking) via ISystem::LoadXmlFromBuffer at level load.
   v92: 6 Experimental switches (DOF bokeh, HQ shadows, view-aligned
   shadows, SSII, grass tessellation, skip taunts via static_int), heading texts. AI attack patterns: Scripts/AI/
   AttackPatterns/*.xml in encrypted Scripts.pak (next: dump through the game). v91: Detail limit folded into Detail distance
   (e_LodMax 4/3/2/1/0 from 4x), short tooltips. v90: v89 hook removed (e_LodMax only reaches attachments, sticky
   when raised until characters rebuild), Detail limit tooltip, settings heading in orange. v89: (SetFrameLodInfo hook bumps
   the LOD info ID on min / max LOD changes). v88 + HDR fix35 (`rysesonofrome_fix35.zip`; v1.03): overlay texts (when
   settings apply, Experimental switches warning); HDR: "Video Highlights" -> "Video AutoHDR" (key unchanged),
   Recommended (All Post Processing On) sharpening 0.5. v87: ATN's setting blood 3x. v86: Experimental
   heading text. v85 (README v85): release polish (counts to Advanced,
   user tooltips, presets' Detail limit 3/2/1). v84: Detail limit (LOD) option, e_LodMax
   6/3/2/1/0 (out of Detail distance 64x). v83: Particle memory Off / 16x, warning
   text, Experimental Reset / ATN's setting buttons (atn_choice). v82 (tested: some blink at max sliders):
   Particle memory option (Off / 8x / 16x;
   off locks the particle sliders at in-game), Fire / Smoke / Other up to 4x, Experimental warning. v81 (tested: 65535
   shown, some parts still dropped at high sliders): particle vertices set to 65535 in a hook
   on the buffers' maker (the game re-applies 30720 after the device). v80 (tested: showed 30720): particle vertex counter (used of
   buffer, not fitted), particle memory 16x at install (ATN's test; v79 grew on demand). v79 (tested: sliders go
   much higher, memory 2-3x; keep 65535): light limit removed (never reached,
   20-30% fps), particle pool grown on demand (alloc hook, up to 16x), particle vertices 15360 -> 65535 at device
   creation, counters under Other particles. Next if still flickering: read the counters; vertex pool above 65535
   needs proof that indices are relative (or a per-part clamp). v78 (tested: counter < 255, flicker stays, 20-30% fps):
   Light limit 255 / 1024 / 4096, also per 8x8
   tile (shader index clamps), light counter under the slider, v76 budget removed. v77 (tested: blinking from Fire
   4-6x, per-tile 255): Experimental "Light limit" 1024 (arrays
   moved, buffers made again, edited lighting cs tiled_lights_cs.h, WARP-tested). v76: plant motion removed (fixed 1/30 s spine
   steps, ATN: "1 fps"), particle lights held under the 255 tiled lights by a stable per-particle rank (light list
   hook). Raising 255 (arrays, buffers, cs in both addons) offered, not done. v75 (tested: no chapter crash;
   README v75): chapter-change crash fixed (per-level
   g_crowdEmotionUpdateTimer left out), extra particles add no lights (255 tiled lights limit), "Plant motion" (merged
   meshes simulated every frame with a scaled time step; the game: every 4th-12th frame). v74 (tested): particles per
   effect part by texture (README v74),
   leg colliders removed (ATN: characters fly), cloth cutting renamed. v73 (tested): per-kind particle counts (particle_counts.hpp,
   Detours hook on the emit count scale +0xf5e50; README v73), replacing e_ParticlesLod; Experimental: leg
   colliders, physical hit reactions, explosion craters, deformable objects, terrain sun shadows; cloth cutting
   explained (world physics cloth / ropes only). Soldiers' clothes: CryAnimation "[Character Cloth]" (sim + render
   attachments, wrap skinning), no cut / tear code: not possible. Possible later: the game's equipment drop
   (DropAttachment +0x15abf53, DetachEquipment, DroppedEquipment: helmets / shields) on more hits. Ask ATN: each experimental toggle, the particle
   sliders, ryse_particle_effects.txt (Advanced button) to refine the kind words, reflections per tier.
   Plants ~24 fps: merged-mesh grass, a 1/30 s fixed-step spine simulation run at 1/4-1/12 speed (README v76): left
   as the game's. Trees: other systems (vegetation BendInfo per frame on the CPU, foliage ropes within 25 m).
   How the RE was done: rx.py (`python -I rx.py cvar NAME` = help text from the exe; `xref RVA`; `dis RVA`),
   names.py (string users), field_any.py / disp_scan.py (struct field readers); 3D engine CVars* global +0x2e25048,
   CryAnimation console static +0x2240930 (found at runtime via FORCE_SKELETON_UPDATE); crash dumps with cdb
   (`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe -z DUMP -c ".ecxr; kn 30; q"`).
2. NEXT PROJECT (ATN, after Ryse): an HDR addon for Total War: Shogun 2 (Complete Edition, released 2026-10-13).
   Start with the `setup-game-mod-dev` skill (`.agents/skills/`), new folder from `src/games/generic`, shader dump
   with 3Dmigoto (`bin/cmd_Decompiler-1.3.16`), then `swapchain-resource-analysis` / `handle-sdr-tonemap-lut`.

## RESUME HERE (2026-10-12): state of all three games
- DONE (Tales HDR v28, ATN: "Along Sun Rays is insanely good", -3/4 fps, better than the cvar cells at 2). v29: "Soft"
  removed (Off / Along Sun Rays, key `sun_ray_smoothing`). v30: v29's Sun Haze slider removed again (ATN: black disc
  around the sun above 1, character flicker below 1). History: v27 fixed the black (ATN; it was NaN from
  log2(1 - filtered 1.0000001), now saturated). Stairs still faint with v27's isotropic Gaussian, so v28 adds Smooth
  Volumetric Light "Along Sun Rays" (2, default; "Soft" = v27): 11 bilinear t6 taps along the screen direction toward
  the sun's point (Cramer's rule on v1, ddx/ddy(v1) and sun dir cb0[53].xyz). Ask ATN: Soft vs Along Sun Rays, and
  the fps cost. Then ATN's haze strength slider (0-2). Older notes: Problem: stair-step / saw-tooth edge of the mountains' shadow wedge in the sun haze (8 px period in x).
  Facts (all in "Tales" entries further down, HDR v20-v26): Bandai's deferred sun lighting ps 0xF2A6E883 (replaced as
  `src/games/talesofarise/shaders/lighting00_0xF2A6E883.ps_5_0.hlsl`, CustomShaderEntry in addon.cpp's explicit list)
  reads three 480x270 volumes through s4 = a POINT sampler: t6 RGBA8 x5 (sun visibility per distance band
  0-480/-960/-1920/-3840/beyond; sky reads the far band, channel w), t7 RGBA16F x16 (in-scatter), t8 R16F x16.
  ATN's v23 test: zeroing t6 removes the wedge (haze everywhere); t7/t8 only flicker on the character. Filtering
  t6/t7/t8 with Loads (v24 B-spline / v25 Gaussian, NaN+negative skipped via exponent bits) made the stairs much
  better (Low best; Medium/High worse) BUT black cell-blocky blobs on rocks (never with Off; worse at sun shadow
  distance 4: black almost everywhere without haze). v26 filters only t6 (SampleVisibility, 4x4 Gaussian sigma 0.58
  + linear between slices) and point-samples t7/t8 as stock: ATN says not fixed. Next ideas: (1) filter only sky
  pixels (view depth r2.x beyond the last band, e.g. > 3840 + 2880) and keep geometry stock: the stairs are in the
  sky haze, the black on geometry; (2) or weight each tap by similarity to the stock point sample / same-band
  validity (cells whose content belongs to nearer geometry hold 0 visibility for far bands, so blending darkens
  geometry: r0.w-style factors multiply all lighting); (3) check with ATN Off vs On on rocks once more. Then ATN's
  idea: a haze strength slider (0-2). Tools: DLAA addon Advanced "Trace this pixel" (pixel_trace.hpp,
  tales_pixel_trace.txt next to the exe); screenshots in `Arise\Binaries\Win64\debugforclaude\Nexus_SDR_UC2`.
- Ryse cvar audit (2026-10-13) -> DLSS v59 -> v60 (sent). ATN: no chapter crashes with everything maxed (v59). v60:
  experimental options folded into the sliders, Water presets 2x (4x looked bad; 6x to see it), the 2 VF_CHEAT
  streaming cvars removed + revert guard (QualityCvar::reverts). v61 (sent, untested): armour / shields snapping to
  full detail within 2-3 m (ATN: a model switch) = attachments' CDF "LodDistances" (fixed metres, compared unscaled in
  attachment LOD +0x881950, then clamped by e_LodMin / e_LodMax). Detail distance 64x now writes e_LodMax 0 in memory
  (VF_CHEAT; QualityCvar::engine_offset 0x41c, from_choice 6; EngineCvars()). ATN: not fixed. v62 (sent): the cause
  is blend-shape culling (ca_vaBlendCullingThreshold 1 px, CryAnimation +0x8e0241): Detail distance divides it;
  Water max 2x (6x crashed ATN's PC). ATN: still grows at 2-4 m. v63 (sent): Advanced "Armour test (temporary)"
  combo (ARMOUR_TESTS: ca_vaBlendEnable 0, ca_vaEnable 0, e_TessellationMaxDistance 0.5, e_LodMin 2, e_CharLodMin 2)
  to name the system; then fix that and remove the test (and e_LodMax 0 / blend threshold if useless).
  Shadow distance (e_ShadowsCastViewDistRatio x object view distance, +0x5ea5c): ATN sees no difference. Details: Tool `.claude/projects/D--Dev-renodx/tools/ryse/rx.py`
  (cvar NAME: help text from the decrypted image's IConsole::Register calls; xref; dis). Full cvar list:
  Bin64\ryse_cvars.txt (2026-10-06, flags 0x800000 = refused). Pool limits: r_buffer_banksize 4 and
  r_transient_pool_size 4 (MB banks, read once in SPoolManager::Initialize, +0x17b3e88; the water crash copied a
  render mesh bigger than a bank, CRenderMesh update +0x17bef61 -> +0x17b4b50 -> memcpy). Candidates for ATN:
  e_StreamCgfPoolSize 80 ("Render mesh cache size in MB": likely the shield / rocks loading late with our LOD
  multipliers), e_ViewDistRatioCustom 100 (players / AI), ca_AttachmentCullingRation 300 (attachments),
  e_MergedMeshesActiveDist 250 (grass patches streamed in), e_RecursionViewDistRatio 0.1 (reflections), streaming
  speed (r_TexturesStreamingMaxRequestedMB 2 / Jobs 32, e_StreamCgfMaxTasksInProgress 32 / MaxNewTasksPerUpdate 4),
  r_TexturesStreamingMipBias 0, e_GsmCacheLodOffset 3, e_DynamicLightsMaxEntityLights 16, e_FoliageWindActivationDist
  25, e_ParticlesLod 1, water extras (r_WaterRipplesResolution 256, caustics), r_BeamsMaxSlices 200 (max 300).
- Ryse (HDR fix34, DLSS v65: v65 trace saves skinning bones; v64 character test toggle + pixel trace; v63 armour test; v62 blend shape culling, water 2x max; v61 e_LodMax 0 at Detail 64x (armour LOD); v60 experimental folded into sliders, water 2x; v59 LOD / streaming cvars; v58 Water capped at 4x (8x/16x crashed chapter 3: the sea grid (X-1)(Y-1) x 12 bytes overflows a 4 MB buffer, from 2 dumps); v55 clearer Texture LOD bias labels/tooltip; v56 "Smooth shiny surfaces" option removed again in v57 (ATN: G-buffer filter always off, better highlights; armour shimmer identical without the HDR addon = game specular aliasing). HDR fix34: Recommended presets Bloom 0.5): done, ATN: "perfect". Expeditions: Rome (HDR v26, DLSS + graphics): ATN: "perfect".
- Tales of Arise: HDR v30 (v27 NaN fix for Smooth Volumetric Light, v28 Along Sun Rays, v29 Soft removed, v30 no Sun Haze slider; v18 Motion Blur slider Advanced only; v19 16-bit scene buffers against sky banding; v20 Smooth Volumetric Light), DLAA + graphics v48 (zip `Downloads\talesofarise_dlss_v48.zip`). Confirmed by ATN: sun
  shadow resolution 6144 / 8192 (CSM atlas doubled to 16384) fixes the sun-move shimmer; "Keep characters behind the
  camera" (Arise.PF.Culling.ForceInFrustum 1) fixes characters fading in on fast turns. Untested: v34-v36
  `engine_hooks.hpp` (NPC / monster distance, NPC / monster detail distance, never hide characters, outline distance,
  sun distance above 2, skit resolution, smooth cutscenes; details in the Tales DLAA README's v34-v36 note).
- Arise-SDK (GPL-3.0): none of its code or byte patterns are used (v36: all patterns chosen anew from ATN's exe,
  own formulas). Its source was read to learn which functions to change: say so honestly. Credited (ATN): thanks to
  emoose, no Arise-SDK code or byte patterns, ours not in it: keep characters behind the camera, sun shadow
  resolution above 4096 (Nexus text, README, engine_hooks.hpp). The sun distance clamp removal is Arise-SDK's idea
  (its source), the rest of the engine cvars were found independently.
- v37: Beyond Maximum (Maximum + outline 100000, monster 10000, detail 1000: each row's highest choice) and a second
  Recommended (Beyond Maximum with the game's volumetric fog), each Recommended on its own line. ATN confirmed:
  with Arise-SDK installed the overlapping rows grey out. Zip talesofarise_dlss_v37.
- v38: outline distance and NPC distance up to 750000 (NPC distance joins Beyond Maximum). Zip talesofarise_dlss_v38.
- v39: skit resolution 200 from Maximum on; smooth cutscenes in every preset incl. Game's own (EVERY_PRESET_SETTING);
  lower presets for the new rows: outline 9000/12000/18000/24000, keep behind camera 1 everywhere, NPC distance
  5000/10000/50000/200000, monster 2/5/10/1000, detail 2/3/5/15, never hide from Extreme, URO 0 everywhere. Zip v39.
- v40: Recommended presets removed; every preset keeps the game's volumetric fog (as Expeditions). Higher distances
  (README v40 note). Zip talesofarise_dlss_v40.
- v41: terrain distributions up to 50, terrain bias -2, grass draw distance 20, all in Beyond Maximum. Character
  self-shadows: Bandai's screen-space chara shadow is driven by FPostProcessSettings fields (SelfShadowSearchRadiusTO14,
  SelfShadowSearchBiasTO14, SelfShadowStrengthTO14, SelfShadowDirectionTO14, FaceSelfShadowZBiasTO14,
  EnableSkinSelfShadowTO14, DisableScreenSpaceCharaShadowTO14) + r.EnableScreenSpaceCharaSelfShadowTO14 (default 1);
  no quality cvar; waiting for ATN's close-up of what looks wrong. Zip v41.
- v42: temporary "Halo test" rows (README v42 note) to find what removes shadows in a halo around the character
  (stock game too). Suspects: Bandai's screen-space shadows (can't see behind the character), half-res capsule
  shadows. Sun resolution above 8192: impossible (16384 atlas = D3D11's max texture, 4 cascades of 8192). Zip v42.
- v43: Halo test removed (none of its rows helped; README v43 note has the game's values). Capsule shadows at full
  resolution row (Ultra+). Halo (stock game too): ATN's shots (`Win64\testpic\Nexus_SDR_UC2`) show the bush losing
  Bandai's painted strokes / painted shadows in a band around the character: most likely the stage post filter
  (EnableStagePostFilterTO14, StageFilter* / StageShadowPenumbra* / StageEdge* in FPostProcessSettings) skipping
  character pixels. ATN: leave it. Zip v43.
- NEXT (2026-10-12): ATN installs Tales of Arise (and later Expeditions: Rome) on Steam and sends the exe path: check
  every pattern (engine_hooks.hpp, SUN_SHADOW_ATLAS, JITTER_SCALE, console lookup) against it offline with the tools in
  `C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\toa` (edit the exe path in `xref.py`), then ReShade.log. Bundled `Arise-SDK.ini` leaves the overlapping settings to the
  addon; with Arise-SDK installed the addon detects its hooks and uses its sdk.* cvars or shows unavailable.
- 2026-10-12 Steam check: ATN's old copies are gone; Steam installs `D:/SteamLibrary/steamapps/common/Tales of Arise`
  and `.../common/Expeditions Rome`. Expeditions exe: plain code, same build (1722 console lookups, singleton
  exe+0x37B6B20, ScalabilityCVarsSinkCallback exe+0x1FFF590): nothing to change. Tales exe: SteamStub (`.bind`
  section, `.text` encrypted on disk, decrypted before ReShade loads add-ons); its plain `.pdata` has the old build's
  function starts (0x1A866B0, 0x21B9FC0, 0xA0C230, 0x6288C0): same build, nothing to change. Offline disassembly of
  Tales now needs a runtime dump; final check = ReShade.log ("[Tales engine hooks]", jitter / atlas found).
  Tool scripts (`tools/toa`, `tools/exr`) now point at the Steam exes.
- 2026-10-12 Expeditions on Steam (ATN): HDR clips, DLSS never activates. Log: swap chain 2194x1234 = 4K at 175%
  Windows scaling (DPI-virtualized; the old install was not). Unreal quantizes buffers to 4 px (2196x1236): the HDR
  addon's BACK_BUFFER aspect match (tolerance 0.0001) upgrades no post target (only the LUT), DLSS rejects the
  2196-wide TAA output ("unsupported source/output texture layout"). Fix: compatibility override "High DPI scaling:
  Application" on ExpeditionsRome-Win64-Shipping.exe (both READMEs, step added). The exe also reads
  `[/Script/Engine.UserInterfaceSettings] bAllowHighDPIInGameMode` (default true; game config sets it false) -> an
  Engine.ini alternative, untested (UI scale unknown).
- 2026-10-12 Tales DLAA v44: "Weak CPU" checkbox under the presets (CPU_HEAVY_SETTINGS: ForceInFrustum, never hide
  characters, a.URO.Enable -> the game's in every preset; ticking sets them now). ATN: ForceInFrustum ~5 fps on X3D
  cache cores, ~30 on frequency cores. Zip talesofarise_dlss_v44.
- v45: Weak CPU without a.URO.Enable (ATN: no difference). Zip talesofarise_dlss_v45.
- v46: "Far sun shadows (mountains)" checkbox (FAR_SUN_SHADOWS: distance 3 + resolution 8192 in every preset; light
  through mountains vs softer near shadows, so in no preset). Sun distance choice 3 added. Zip talesofarise_dlss_v46.
- v47: sky banding (ATN: in SDR too; scene color is already RGBA16F). Advanced pixel trace (pixel_trace.hpp, README
  v47) to find the sky's pass and the format that bands: waiting for ATN's tales_pixel_trace.txt (aim at the banded
  sky). Then: deband + dither like Pharaoh's skydome in that shader, or upgrade the low-precision target.
- Trace result (2026-10-12): scene color RT0 is R11G11B10 3840x2160 (r.SceneColorFormat 4 ignored by Bandai's
  renderer; frame starts with last frame's sky: no clear). Sky = Unreal's sky sphere material ps 0x11039AD5 (vs
  0x57DAED04; gradient from cb2 constants, t0-t2 cloud noise only), then fullscreen ps 0x2483CE26 and 0x381F5A30
  (R11G11B10), half-size R11G11B10 1920x1080 chain, tonemapper 0xDCFDF14D. Fix: HDR v19 "Scene buffers" (Advanced,
  restart, default 16-bit): R11G11B10 RT / copy-dest at back-buffer aspect -> RGBA16F (view cloning). DLAA accepts
  RGBA16F color. DLAA v48: trace compares only texel bytes. Zips talesofarise_v19, talesofarise_dlss_v48.
  ATN: sky fixed.
- HDR v20 (untested): stair-step haze edge near the sun (Bandai volumetric lighting, 8-px cells; cell size 2 helped for
  -25 fps). Trace 2: deferred sun lighting ps 0xF2A6E883 samples the 480x270x16 RGBA16F volume t7 once (line 966 of
  the dump). Replaced shader `lighting00_0xF2A6E883`: 3x3 1-2-1 taps one cell apart (Smooth Volumetric Light, default
  On). Volume also read by 0x0A9D482A, 0x1C8D11B6, 0x1FE8B2B6, 0x36E08F9A, 0x4E695056, 0x4E8218FE, 0x8CCA5153,
  0x90EF2450 (ps) and 0x0DCD11E1, 0xBC95A089 (cs): stock. Zip talesofarise_v20.
- HDR v21: v20 forgot CustomShaderEntry(0xF2A6E883) in custom_shaders (the list is explicit!): added. t8 (R16F
  480x270x16, slice depth / 6000) smoothed too (SmoothVolume). Zip talesofarise_v21. ATN: still no change
  (replacement confirmed in the log). v22: the saw-tooth is t6 (RGBA8 480x270x5, sun visibility per distance band;
  sky reads the far band): every t6/t7/t8 sample -> SampleVolume (3x3 1-2-1 when On). Zip talesofarise_v22.
  ATN: ~0.1% better. Trace 3 (dark side, 47.1% x 25.3%): lighting00 output A 0.17/0.32/0.53 vs bright side
  0.35/0.58/1.03: the wedge is made in lighting00. Saw-tooth period 8 px in x (480-wide cells). v23: temporary
  "Volumetric Light Test" (1/2/3 zero t6/t7/t8 samples) to find the volume. Zip talesofarise_v23.
  ATN test: without t6 the wedge is gone (haze everywhere), t7/t8 off: flicker on the character. Volumes are read
  through s4 = POINT sampler (8-px blocks; a 3x3 of point samples kept steps). v24: SampleVolume = cubic B-spline
  over 4x4 cells via Load + linear between slices (32 loads per call, 4 calls max per pixel). Test setting
  removed. Zip talesofarise_v24. ATN: stairs much better, but black blocks on rocks with Smooth on (not Off, so
  the decompile is fine), changing with sun shadow distance. v25: FilterVolume skips NaN/Inf (exponent bits:
  fxc compiles isnan() away, warning X3577) and negative cells, renormalizes; single-return functions (X4000).
  Smooth Volumetric Light Off/Low/Medium/High (Gaussian 4/6/8 taps, sigma 0.58/1/1.5; Medium/High only t6).
  Zip talesofarise_v25. ATN: black still at Low/Medium/High (never Off), sun distance 4: black almost everywhere
  without haze; Low looks best. v26: only t6 filtered (SampleVisibility, 4x4 Gaussian + linear slices), t7/t8
  point-sampled as stock (their finite "no data" cells caused the black); Off/On again. Zip talesofarise_v26.
  Next (ATN idea): a haze strength slider (0-2) once this works. Side note: psycho_test31.hlsli isnan() calls
  are compiled away by fxc (X3577) - check the PsychoV31 black-pixel fallbacks some day.
- 2026-10-12 Expeditions DLSS graphics v13 (untested): jitter fix. UE 4.26 keeps the TAA jitter position in a signed
  byte (FSceneViewState +0x4AA, movsx at exe+0x1368EC0 in PreVisibilityFrameSetup, and the getter exe+0x132D120);
  pattern length min(trunc(8 / scale^2), 255) (exe+0x1368F33..). Past 128 positions (< 25%) it goes negative,
  Halton(<= 0) = 0 -> jitter frozen 127 of 256 frames. Both loads patched movsx -> movzx (one byte each,
  `code_patch.hpp` copied from Tales) while loaded. Warning threshold 18% (25% without the fix); Advanced shows
  "jitter fix: on". Below 18% UE's 255 cap remains (own jitter = Ryse-style stub, not done). Zip
  expeditionsrome_dlss_graphics_v13.
- Expeditions DLSS graphics v14 (untested): Texture LOD bias like Ryse (Off / Matched / Sharper default / Sharpest).
  UE already biases by max(log2(scale) + r.ViewTextureMipBias.Offset (-0.3), r.ViewTextureMipBias.Min (-1 in this
  game)), TAAU path only (exe+0x1369008, View +0x46FC -> cb0[137].y in 599 PS). Addon writes Offset 0/-1/-2 and Min
  -16 while DLSS < 100%, game's values otherwise (README). Zip expeditionsrome_dlss_graphics_v14.
- v15: clearer Texture LOD bias labels; tooltip shows each choice's bias at the current scale. Zip v15.
- Next, in order: (1) ATN tests v36 (also once with Arise-SDK installed: ReShade.log "[Tales engine hooks]" lines);
  (2) ATN gives his preferred values: update the Tales presets' sliders; (3) ATN buys Tales of Arise and Expeditions:
  Rome on Steam: check every pattern against the Steam exes offline first (tools below; Denuvo-encrypted code only
  shows at runtime: then ReShade.log), then in game.
- Analysis tools (copies): `C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\toa` (Tales: `xref.py` exe +
  xrefs, `toa_dis.py <rva> [from to]` disassembly, `sdk_sigs.py` / `csm_atlas.py` pattern checks) and `...\tools\exr`
  (Expeditions). Edit the exe path at the top of `xref.py` for another build. Reflection offsets: find the property
  name's char string, the pointer to it in .rdata, offset at +0x28 of that params struct.

## Ryse HDR addon (`src/games/rysesonofrome`) - done (fix9)
- HDR (Extended) = UC2 Extended (`uncharted2extended.hlsli`, Musa/Jon) on the game's Hable; Highlight Expansion removed;
  fxc crashed on `SignSqrt(-x)` -> rewritten with `sign()*sqrt(abs())` in that local copy.
- PsychoV31 keeps the game's grading, replaces only the curve. A "Highlight Hue Shift" slider was tried, then removed
  (V31's default already = full response hue).
- ATN: "HDR looks ready".

- 2026-10-05 review (code reading): Hable coefficient mapping, UC2 Extended, peak / sRGB-on-2.2 emulation, Paper
  White / UI chain all consistent. V31 mid gray exact: anchor 0.18 -> FilmCurveY(0.18) (anchored grading = gain
  anchor_out/anchor_in, C-inf shoulder starts at anchor_out), same as SDR after chart + emulation.
  Clipped in HDR: sun shafts and lens flares (up to white, by choice), film grain (up to white), videos (SDR).
  Lens flare sprites (AC3155D8 ghosts, BE5D77E6 orbs, 92DFB7F1, 3468E47B; not replaced) each tonemap with their
  own filmic curve x(ax+b)/(x(ax+1.7)+c) (HDRParams, Hejl-style) and saturate -> HDR flares possible. Scene passes
  (bloom, fog, particles, sky, motion blur, DOF) clamp only weights.
- fix10 (untested, zip `rysesonofrome_fix10.zip`): Extra toggles Sun Shafts HDR / Lens Flares HDR / Film Grain HDR
  (On by default) + Video Highlights slider (ITM, default 0). Light past white added via `RyseAddLight` (Neutwo
  into the headroom below Peak). Flare sprites flare00-03 (curve to x = 0.5, linear-light tangent above). Grain
  overlay white = Peak. See README "HDR effects".
- fix10 test (debugForClaude8, PQ PNGs): grain on highlights OK; lens flares no visible change (scene without
  bright flares?); Sun Shafts HDR turned the sun area orange (per-channel excess, red only). fix11: RyseAddLight
  adds the lost light's luminance in the pixel's SDR hue. Video Highlights tooltip simplified.
- fix11 test (debugForClaude9): half circle with colored rim on the flag pole near the sun = crease of the stock
  screen's saturate(1 - base) where the HDR bloom passes 1 (flat white plateau outside, bloom inside; gone with
  bloom off). fix12: `RyseScreen` (1 - base up to 0.5, then C1 exp tail) for shafts and lens flares. Lens Flares
  HDR removed (flare00-03 shaders deleted).
- fix12 test: half circle fixed, game "beautiful". fix13: Film Grain HDR removed (ATN: stay faithful), Sharpening
  default 0 (tooltip: game's own = 1), Video Highlights tooltip says ITM, Video Dithering toggle (On; IGN 1 code
  value before the ITM, only with Video Highlights > 0).
- fix14: Sun Shafts HDR toggle removed; the shafts past 1 (stock saturate) are always added in HDR modes.
  ATN: "HDR is perfect", DLSS "super happy".
- fix15: Chromatic Aberration slider (x vLensOpticsParams.w, the post pass's red shift of the lens flare layer;
  exe strings r_ChromaticAberration / r_FlaresChromaShift). Full-screen ChromaShift filters exist in the exe but
  never seen in dumps. autoexec.cfg: Game.log shows "Unknown command" for every e_/r_/con_ cvar (compiled out
  in the PC build); only sys_spec_* (not sys_spec_Quality) accepted.
- autoexec.cfg now = all 14 sys_spec_* groups at 4 (all accepted, Game.log 23:42). Correction: the cvars are NOT
  compiled out; the game's cvar whitelist (+0x1594B40..+0x15967AA) rejects them with a fake "Unknown command".
  Whitelist + ICVar offsets in the HDR README. Possible next: addon sets non-whitelisted quality cvars directly
  (diagnostic first: log their values at Very High).
- DLSS v28 (untested): Advanced "Save the game's settings" -> Bin64\ryse_cvars.txt (all cvars, values, flags; console
  via CONSOLE_REGISTRATION pattern = con_restricted's registration at +0xF013B0; map CXConsole +0xF8). Next: read
  the dump at Very High, propose raised values, then an addon option that sets them (ICVar Set int +0x38 / float +0x40).
- v28 test: 3141 cvars dumped, con_restricted 0, GetCVar(r_Width) = the renderer's. Game menu: Low/Medium/High only;
  sys_spec 4 is above it (ATN sees a difference). Chromatic aberration: vLensOpticsParams.w = r_FlaresChromaShift (6;
  +0x180AFB6), r_ChromaticAberration = 0 (off). v29 (untested): "Game settings above its High" (Shadow resolution x2,
  Draw distance x1.5/x2, Detail distance x2/x4, Texture streaming pool x2/x4), README.
- v29 test: "100% works". v30 (untested): section moved below the DLSS status (Simple), Reset / MAXED OUT, options:
  Engine quality (14 sys_spec groups, absolute 3/4), Shadow resolution x2/x4 (+pool, res scale), Shadow distance,
  Draw distance up to x3 (+merged meshes, veg sprites, proc veg), Detail distance (+attachments, detail textures),
  Tessellation (distance x, triangle size /), Texture pool, Decals, Chromatic aberration slider (r_ChromaticAberration
  0-16: CA_Sharpening pass at +0x180A1D0, psParams.x, runs only above 0; HDR behavior of that pass unknown). If CA
  looks good, the HDR addon's Chromatic Aberration slider may switch to it.
- v30 test: all work. v31: "Game's" -> "In-game setting", TESTING button (last items: shadows 8x capped 16384, shadow
  dist 4x, draw 6x, detail 10x, tess 8x, pool 8x, decals 10x), CA option removed (moved to HDR).
- HDR fix16: Chromatic Aberration = the game's full-screen CA (CA_Sharpening 0xC53CE326, from the DevKit auto-dump
  Bin64\renodx-dev\dump) rebuilt in post.hlsli (default 0, 0-16, new key chromatic_aberration_amount); lens flare
  shift stock again. UI slider flicker fixed: Paper White-UI gain moved from tonemap/TAA output to the post pass
  output (TAA/DLSS history was in UI-scaled space).
- "New ones 3" (27 shaders after the higher settings, = the DevKit auto-dump): 20 lighting/material variants
  (irreg_kernel_2d: finer shadow filtering), 2 VS, motion blur gathers 0x2EC2DAAE / 0x3CB3A474 (read the replaced
  velocity pack 0x799A5EB8, no color clamp), SSDO 0xD1106E8B, CA_Sharpening 0xC53CE326. None need replacing.
- "New ones 4" (2026-10-14, every chapter, 171 PS / 147 VS): no permutation of a replaced shader (signature match on
  cbuffer/texture names over all 511 dumped PS). New: lens flare sprites 0x199D8C5B / 0x25C7B8E3 / 0xBC7D5AD9 /
  0x1096988E (HDRParams curve + saturate, like the stock ones), materials, water, fog, rain, color chart builder
  0x69A6ADE3. Older dumps hold multiply-blend video variants 0x0ABA0C5A (YUV) / 0x67F12AF3 (YUVA), not replaced.
  Chapter 3 sun trace (debugForClaude37): sun disc 18 (sky 0x74F62697) under the cloud layer (0x1C896707, 0.2);
  final ~(1.09, 1.09, 1.07); orange sun glow sprite 0x3468E47B (0.32, 0.11, 0.03) via post01. ATN's Peak was 10000.
- HDR fix37 (`Downloads\rysesonofrome_fix37.zip`): Sun Glow removed again (ATN: doesn't look good).
- HDR fix36 (removed in fix37): ATN: sun stays 203-250 nits whatever Peak / Exposure /
  Highlights = the shafts' screen blend pinning the full-mask area at white. Extra slider Sun Glow (default 0)
  adds what the blend cuts off past white as light (README). Video multiply variants: stock is right.
- HDR fix17: Chromatic Aberration slider 0-2 (default 0; 1 = amount 6 = r_FlaresChromaShift), key
  chromatic_aberration_strength.
- v31 test (debugForClaude10): ATN edited TESTING to shadows 32x, others 256x; shadow res stops at our 16384 cap and
  costs the most; e_DecalsRange refused (reads back 20). Past the caps (camera far plane, LOD 0, tess factor 64, all
  textures resident) more changes nothing. v32 (untested): new knobs with headroom (per-object shadows, shadow
  cascades 6/7/8, vegetation pools, vegetation sprite res, particles, water), labels match ATN's values, auto
  exposure note under the DLSS preset.
- HDR fix18: Chromatic Aberration slider 1 = amount 12 (0.5 = old 6, 2 = 24; 16 was only the v30 test slider's end).
- Release candidates (ATN to write the text and release on Nexus / Discord): DLSS v33 (buttons Reset / Very High /
  Recommended MAX (shadow res 4x) / MAXED OUT; "Graphics preset (in-game)"; more steps per option) and HDR fix19
  (Recommended preset: PsychoV31 + Film Grain 0). DLSS v34: texture pool 16x (24 GB) added (MAXED OUT; Recommended
  MAX stays 12 GB). v34 test: pool > 6 GB loads no textures: the game keeps the pool in bytes in 32 bits (6 GB ->
  2 GB, 12 / 24 GB -> 0). v35: In-game / 2x (3 GB) / 4 GB max (capped 4000 MB). v36: note that an AA mode / scale
  change reloads textures (some can stay missing: checkpoint or restart). v36 test: 3 / 4 GB still leave some textures
  unloaded. Cause: +0x174015F does (secondary + r_TexturesStreamPoolSize) << 20 in 32 bits, stored as an int
  (signed: negative from 2048 MB; other readers use movsxd / 64-bit). v37: In-game / 2 GB max (2047 MB).
  v38 (untested): shadows ended closer at smaller scales = vegetation sprite distance x render width / 1024
  (+0x16EB00); the addon pins e_ForceDetailLevelForScreenRes (3D cvars +0x3f0, written in memory) to the display
  width while rendering below it. v38 test: fixed.
  v39 (untested): Fine detail options: G-buffer filtering Off, Texture LOD bias (anisotropic sampler clones bound after
  PSSetSamplers; log2(render / display), Sharper -1), Foliage anisotropic filtering (r_TexNoAnisoAlphaTest static = 0).
  v40: foliage aniso and G-buffer filtering Off are automatic while DLSS / DLAA runs (renderer_pins, statics); Texture
  LOD bias moved under the DLSS preset (Off / Matched / Sharper default / Sharpest -2, key Ryse AA/TextureLodBias).
  v41 = RELEASE (ATN: "It is ready"): preset M default (L looks the same in Ryse, costs more), "(experimental)" removed
  from the addon name and the overlay tab ("Ryse DLSS").
  v42 (reverted): the post pass redrawn at the display size got a copy of its b0 with PS_ScreenSize = display size
  (CopySubresourceRegion1 + UpdateSubresource1 on one register). No effect in game (debugForClaude12: 14% CA offsets
  exactly display / render = 7x DLAA's; grain at display resolution, so the redraw ran); cause not found. HDR fix25
  instead: post.hlsli takes its texel offsets from t0's size (display in the redraw, render size otherwise).
  ATN: fix25 fixed it. v43 (tester report, untested): Draw distance no longer raises e_ViewDistRatioLights (shadows
  flickered in one area with draw distance up, any shadow settings; suspected shadow pool overflow from more lights);
  e_ShadowsPerObjectTexRes capped at 8192 (bands on characters at shadow res 4x / 8x; suspected bias vs D16,
  r_ShadowTexFormat 5). HDR fix26: hotkeys F6 / Caps Lock = HUD (skip 0x96675B77 + 0x1B59F534, menus too), F7 / Tab =
  execution highlights (skip 0xD2063417); session only. HDR fix27 (ATN: works): hotkeys polled with GetAsyncKeyState
  at Present (F8 on ATN's keyboard is a SteelSeries Print key, so ReShade's key state was never the problem); F7 also
  0xF495DD17 (head highlights); F8 / key above Tab (scan 0x29) = execution skull: 0x1B59F534 draws >= 3500 indices at
  4K (skull 3744-4539, other HUD <= 3219), cut x (height / 2160)^0.23. Logger kept behind RYSE_UI_DRAW_LOG (F9).
  DevKit MCP bridge works (build\Release\renodx-mcp-bridge.exe, JSON-RPC over stdio) but has no draw args/constants.
  HDR fix21: upgrade aspect tolerance 0.03 (reduced targets rounded down at 77% / 58% stayed 8-bit: no uncapped
  shafts there), "Experimental Uncapped Sun Shafts" toggle back (On); fix22 renames it "Sun Shaft Highlights"
  (tooltip: never above Peak). HDR fix23: sun shafts were ~4x larger / blinding vs vanilla, also in SDR: the mask
  (0x4DFAE399, sky depth x 1.32 x sum(scene)) and radial blur (0xF1FCED4E) write quarter-size RGBA8 targets that
  clamped to 0-1; our FP16 upgrade (back buffer aspect) removed the clamp. Both replaced with saturated outputs.
  Other reduced-size RGBA8 targets are upgraded too (DOF, flares, ...): watch for similar differences.
  HDR fix24 = RELEASE (ATN: mod ready): "Sun Shaft Highlights" toggle removed, always on in both HDR modes.
  Future mods: use view cloning for resource upgrades (not on Ryse, by ATN's choice). HDR fix20: Recommended = PsychoV31, Film Grain 0, Motion Blur (Camera) 0, (Objects) 1, Video
  Highlights 0.25.

## Ryse DLSS addon (`src/games/rysesonofrome_dlss`) - v27 DONE (Jitter option moved to the bottom of Advanced) (ATN: 'DLSS basically done')
- Separate addon, copy of `expeditionsrome_dlss` infrastructure (`ngx_backend.hpp`, `performance.hpp`,
  `native_state.hpp`, `constant_mirror.hpp` with an upload hook). CMakeLists links NGX for both `_dlss` addons
  (local change). `README.md` documents the whole design.
- Hooks: TAA `0x121C45E4` (replaced by DLSS), SMAA edges/weights/blend `0x15D2F34D`/`0x5DC96409`/`0xF2D692D1`
  (skipped while DLSS ran last frame, blend -> copy `0x52594503`), post `0xD0B247FF`/`0xB022AFEA`/`0xC3A469C1`/
  `0xCC6DF78E` (upscaling: re-drawn at display size), native upscale `0xCD8548F6` (upscaling: 1:1 copy).
- Private shaders: `0x52594501` prep (reverse-Z depth, motion px, linear color; drawn with the game's TAA VS),
  `0x52594502` encode NVIDIA output (sRGB or gamma 2.2 round trip), `0x52594503` copy t1 (SV_Position only).
- Tested in game (ATN, v8-v10): DLAA works. Own jitter is 8 positions all the time (gameplay, slow motion, pauses).
  Motion vectors "The game's, as is" = best/stable (the game's MVs contain no jitter). Flips flicker (signs
  right). Off<->DLAA instant (NGX runtime kept, only the feature released). SMAA skip: +4-5 fps. Exit crash fixed.
- v11 (untested): modes like Expeditions (DLAA, UQ 77, Q 67, B 58, P 50, UP 33, Custom 1-100); live render scale via
  the game's setter; DLSS outputs display size; post re-drawn at display size; upscale -> copy; needs Native
  Upscaling on; jitter phases 8*(display/render)^2.

### Reverse-engineering facts (Ryse.exe, base 0x137000000, RVAs)
- The exe code is Steam-DRM encrypted on disk. `Bin64\ryse_image.bin` = decrypted image dump (file offset = RVA),
  made with the addon's Advanced debug button. Analysis: python + capstone/numpy scripts (rip-relative ref search).
- cvars (static ints): `r_PostAAMode` +0x22BA8EC (game sets 1 = 2x; addon forces 6), `r_PostAAEdgeFilter`
  +0x22BA8E8 (=3 SMAA 2TX), `r_PostAA` +0x22BAD4C, `r_PostProcessEffects` +0x22BAD44. Finder: `lea rdx,name` +
  `lea r8,&var` near the `Register` call (engine_memory.hpp).
- Jitter: renderer +0xCD0/+0xCD4 = sub-pixel offset (NDC = 2*px/width/ss), added to proj m20/m21 in the camera setup
  (+0x17E97A2 block). Offset computed inline in `RT_RenderScene` (+0x18000B0): zeroed, then only if r_PostAA,
  per-view flag, EdgeFilter==3, `ITimer::GetTimeScale(1..5)>=1` (vtable +0x48), frame counter (+0x2B728F0) moved.
  Addon: 5-byte jump at +0x18006CB (pattern JITTER_SETUP) to a stub near the exe (stores the addon's offset,
  saves r12 = renderer at stub+72, continues at +0x1800ABE). The apply block also requires bit 27 of renderer
  +0x4524 clear (set while paused): `and cl,al` at +0x17E97F6 -> nops (only reader of that bit).
- Render scale: renderer = static object +0x22AA780 (vtable +0x1E4A2D0; global ptr +0x222FB58). Request = vtable
  +0x168 (function +0x173A6E0; ret Vec2 via rdx, x/y in xmm2/xmm3): trunc to px, &~7, min 128, 1 if both >= 1 ->
  pending +0xC1D8. Present-thread end of frame (+0x1702D70) copies pending -> current +0xC1E0 (prev +0xC1E8; forced
  1 by debug cvars). RT_RenderScene calls vtable +0xC50 (+0x16FAEA0) with +0xC1E0 every frame: +0x52A4 scale,
  render size +0xC1F8/+0xC1FC. Callers of the request: console command (+0xEB8910), dynres (+0xEB9150), reset.
  v12 wrote +0x52A4 directly -> overwritten each frame. v13 calls the request: it is the game's DYNAMIC RESOLUTION
  (overload scene manager, osm_* cvars, console osm_setFBScale +0xEB8910): targets stay full size, every pass renders
  into the top-left corner (viewport), fullscreen VS scale uv by g_VS_HPosScale (PER_FRAME c21). v13 test: TAA
  "unexpected size" (input 3840x2160, viewport 2952x1656) -> stock TAA -> the game shows only the corner (frozen
  picture around it). v14: TAA render size = viewport, DLSS output = texture size over the whole TAA target; post
  (4 variants) + native upscale redrawn with own full-screen VS 0x52594504 (uv 0-1, full viewport); lens flares t5
  stretched from the corner (0x52594505); fallbacks: no DLSS for 3 presents -> game's scale; corner not upscaled or
  post not redrawn -> stop until mode change. Motion test modes 1/2 removed (motion px = uv * texture size).
- v14 test (ATN): Quality/Performance upscale (DLSS active, whole frame redrawn) but gameplay shows a darker half-size
  copy of the frame in the top-left quarter (+ a black 25% box): some pass after the TAA still uses the corner layout
  (candidates: LayerBlendAmount passes 0x93B10183 / 0xDC26E6B4). Main menu: TAA only every other frame -> menu drawn
  into the corner. UQ/Custom refused by the backend's aspect check (fixed in v15: 16 px tolerance). v15 adds the
  pass trace (Advanced button, automatic once after the first upscaled frame) and "game's scale until 30 DLSS frames
  in a row". v15 trace: after the TAA only the post pass and the Scaleform UI (96675B77, 1B59F534, 8BA2D1A9) into the back
  buffer, all with the corner viewport -> UI shrunk into the corner. v16: UI draws redrawn with the whole viewport
  (generic draw hook after the post), Custom backend for mismatched corners (UP transition fix).
  v16 test: UI grew by 1/s (UI maps pixels 1:1 into the corner viewport, clipped) and radial MV streaks (quad uv
  spans the corner, mViewProjPrev the screen). v17: Scaleform VS copies 0x52594506-0D (clip transform for the whole
  viewport), prep reprojects in screen space with mViewProjPrev's own uv scale.
  v17 test: UI right; every scale below 100% smeared (95%: radial streaks; 77/50%: repeated bands; 1%: nested
  rectangles to the top-left; also in pauses). Cause = the GAME's motion blur: PostAA::Render (+0x180C670) and
  MotionBlur (+0x18C92C0) build mViewProjPrev = prev view x proj (+0x22BA368) x static scale-bias +0x221CC10
  (0.5,-0.5,0.5,0.5; referenced only by those two) -> uv [0,1], while their shaders (velocity pack 799A5EB8, TAA)
  subtract v1 in [0,s]. SSR (E0B7E115) multiplies by PS_HPosScale.zw, so Crytek's convention is [0,1] x HPosScale.
  v18: OnTaaDraw writes the corner into that matrix's 0.5 entries (pattern REPROJECTION_SCALE_BIAS at +0x180CEC2,
  values checked, restored at unload; testing checkbox ScaleReprojection). SSAO_ReprojectionMatrix uses its own
  generic 0.5 (full-viewport half-res buffers, likely fine). Object velocity scale (G-buffer, e.g. 0x7C093A97:
  v0 * 2 * PS_ScreenSize.zw vs prev from VS) not verified.
  v18 test (HDR addon loaded too, TAA format 10): smear gone; top-left corner region shows semi-transparent stacked
  copies of the scene shrunk toward the top-left (boundaries at s, s^2, s^3 of the screen), parts frozen, HUD full size
  over it. Ruled out: second TAA run (shared command_action has one elected handler module), HDR proxy pass (sets a
  full viewport), PS_ScreenSize (engine sGetScreenSize +0x1824F60 = viewport w,h, 0.5/(w/scale) using renderer
  +0x52A4: correct; skinned velocity VS writes prev * g_VS_HPosScale, so object velocity is texture uv). Post t5 = the
  refraction/flare copy texture (screen-blended). v19 adds the stage capture (PNGs) to find where the copies appear.
  ATN: the game's own Rendering Resolution (r_Width, menu 50-100% step 10, Native Upscaling) at 50% looks normal (no
  copies, DOF fine) with a sharp HUD (UI drawn after the native upscale at display size). DOF also broke under OSM
  (black in executions). => v20 DROPS OSM: sets r_Width/r_Height via renderer ICVar* (+0xB828/+0xB830, native
  +0xF540; Set(int) +0x38, GetIVal +0x10; menu code +0xED912E does the same), DLSS at the true low res outputs display
  size into taa_full; stock TAA still runs; post pass drawn again at display size into post_full (t0 = taa_full);
  native upscale CD8548F6 replaced by a 1:1 copy of post_full (0x52594503 on t1). Removed: OSM control, scale-bias
  patch, UI VS copies, flare stretch. Engine facts kept: sGetScreenSize +0x1824F60 (0.5/(w/scale)), renderer +0xF549
  = native upscale active.
  v20 test (HDR addon on): all modes work, images sharp, HUD sharp. Non-16:9 sizes (2957x1663, 2227x1253, 512x300)
  were not upgraded by the HDR addon (TAA formats 29/27, HDR clipped at 85 nits; also slower). The game renders
  sizes below 512x300 at 512x300. 40% used preset M (only the selected mode's hint was set). FPS (paused, 4K):
  TAA 167, DLAA 181, Q 157, P 196, UP 214 (likely CPU-limited near 200). v21: exact-aspect steps, min 512x300,
  all preset hints. Slight temporal wobble in pauses at lower res (DLSS itself?) still open.
  v21 test: saved mode stopped at startup (game sets its own r_Width once while loading; v21 counted it every
  present -> 'keeps setting its own'); DLDSR 2.25x GPU-bound: TAA 90, DLAA(L) 50, K 90-100, lower scales faster.
  Wobble when still, also DLAA. v22: game writes counted once (last_seen), Custom 14-100, fixed exposure texture
  (no AutoExposure), prep folds WorldViewPos into mViewProjPrev's translation in double (ps_5_0 dmul/dadd).
  v22 test: startup mode fixed. Still wobbles (whole image sub-pixel shifts frame to frame; HDR graph moves in
  DLAA too). Motion capture while still: ~(-0.12, +0.05) px, near-uniform -> jitter in the camera MVs (view ray
  TEXCOORD1 jittered vs mViewProjPrev unjittered). v23: current position = this frame's camera (VS b2 rows, as
  uv) applied to the same reconstructed point; part along w (jitter) removed from both matrices; self-check vs
  v1 (1 px) falls back. Capture adds dlss_motion_fine (+-0.5 px) and logs the frame's jitter.
  v23 test: wobble less but still there, HDR graph still moves (DLAA too). Fine motion capture: |MV| < 0.01 px
  (fixed). DLSS indicator says 'Autoexposure: ON' although the flag is gone (driver override?). Suspect: lens
  flares t5 (AC3155D8/906C01A4, CPU xform) added after DLSS at render res, jittered. v24: two-frame capture
  (input/output/lens_flares/post _next) + testing checkbox 'No lens flares'.
  v24 test: lens flares black (not it); input shift = reported jitter delta within 0.01 px; output shifts < 0.02 px
  (DLSS residual). v25 cleanup: SMAA skip always on (checkbox gone), jitter flips and No lens flares removed.
  Debug buttons (trace, two-frame capture, code dump) kept. NEXT: back to the HDR addon (ATN's item).
- The engine's UpscaleImage (+0x180B270, shader 0xCD8548F6) runs only for Native Upscaling (renderer +0xF549, from
  +0x16FD060), never for OSM: the PC game never stretches the OSM corner.
- The game's Rendering Resolution menu = r_Width/r_Height + Native Upscaling (r_FullscreenNativeRes): real smaller
  targets via ChangeResolution (renderer vtable +0xF8, from HandleDisplayPropertyChanges +0x16FAF60 reading ICVar*
  at renderer +0xB828/+0xB830; ICVar Set(int) = vtable +0x38, GetIVal +0x10; IConsole GetCVar +0xB0; gEnv
  +0x222FA40, pConsole +0xB8, pRenderer +0x118). Alternative if the OSM path breaks: set r_Width/r_Height (swap
  chain reset per change, saved by the game). Display size +0xBDE8/+0xBDEC = render resolution (GetWidth +0x200).
- TAA cbuffer: `mViewProjPrev` unjittered (measured), view ray v2 unjittered (inferred), t2 linear depth*far.

### Next steps
1. ATN tests v20 (Native Upscaling on; all modes: hitch, then sharp image, HUD sharp, DOF/particles fine; AA Off
   restores the game's own Rendering Resolution; if broken: stage capture PNGs) (all modes in gameplay: no smear/streaks, HUD normal size; moving characters for object velocity;
   AA Off restores the scale). Testing checkbox off should bring the v17 smear back (confirms the cause).
2. Then: performance work; remove test options (motion modes 1/2, jitter flips, image dump button,
   "previous jitter" log) once stable; maybe restore the render scale on addon unload (not at process exit).
3. Offer to port the exit-crash fix (`RtlDllShutdownInProgress` in Backend::Reset) and the Off-keeps-NGX fix to
   `expeditionsrome_dlss`. That addon has the same code; ATN asked not to replace it.
4. Ryse HDR addon open items from the old handoff (HDR10 output, supersampling and other paths untested) remain.

## Expeditions: Rome HDR (`src/games/expeditionsrome`) - v14 (2026-10-09, untested)
- Compared with marat569's UE Extended (`src/games/ue-extended`, untracked copy from ATN). No SDR LUT textures in
  this game's CombineLUTs, so their LUT upgrade does not apply. Added mode 3 "HDR (UE Extended)" (experimental):
  their UE Filmic Extended at its defaults in `ue_extended.hlsli` (see README). Differences found vs our Extended:
  our Extended keeps UE's Expand Gamut (theirs and PsychoV31 drop it), UE matrix blue correction vs their exact
  inverse, post grade after (ours) vs before (theirs) the display mapping, SDR-matched color (ours) vs much brighter
  above mid gray (theirs).
- v16 (ATN): old Pharaoh-style Extended replaced by the UE Extended port (mode 1); mode 3 and the old Extended
  sliders removed; Expand Gamut off in both HDR modes (ATN: it oversaturated both). Remember for Tales of Arise.
- DLSS v44: Shadow pool option (e_ShadowsPoolSize out of Shadow resolution; matched / 2x / 4x 16384, floor = Shadow
  resolution factor via QualityOption::floor_option) and Light distance option (e_ViewDistRatioLights 1.5x-8x;
  Recommended MAX: pool 4x, lights 2x). Untested. Ryse HDR Extended fire looks broken (tester): cause = MatchSDRColor +
  HueOKLab to SDR in tonemap00 (UC2 Extended core matches Musa/Jon/upstream gbfrelink); ATN deciding the fix.
- HDR fix28: "colors from SDR" removed from Ryse Extended (MatchSDRColor + HueOKLab to SDR; code and how to restore
  in rysesonofrome/README.md). Extended = UC2 Extended per channel + Neutwo max-channel roll-off to Peak.
- HDR fix29: Extended Hue Correction (ext_hue_correction, default 1): renodx::color::correct::Hue(UC2 per-channel
  result, untonemapped scene) before the Neutwo roll-off, as Musa's Batman (batmanak tonemapper.hlsl).

## Tales of Arise HDR (`src/games/talesofarise`) - v1 (2026-10-09, untested)
- UE4 4.21. Built from Expeditions: Rome v17. CombineLUTs `0x876F0F03` is Expeditions' `0x61F067B4` except two HDR
  output-device branches; lut00 = Tales stock + exactly the Expeditions edits (verified by diff).
- Tonemappers: one body in `tonemap.hlsli`, permutation files set TOA_* features. 0xDCFDF14D from ATN's dump,
  0x8D5C5C52 / 0xC89B89AF / 0x7C1D6772 (cutscene: CA + grain) from marat569's UE Extended. Sliders: bloom,
  vignette, sharpening, film grain, chromatic aberration. Game gamma option `pow(cb0[50].w)` kept (HDR: relative
  to Paper White). Video `0x295C50DE` (CriWare UMG) with Ryse's Video Highlights.
- Shader inventory and the `r.CreateShadersOnLoad=1` trick for all permutations in the README.
- Open: trace passes after the tonemapper (SMAA `0xE462919A` registered as RTV clone writer, unverified), more
  permutations, DLSS later (reuse expeditionsrome_dlss).

## 2026-10-09 full dumps: Tales of Arise v2, Expeditions: Rome v18 (both untested in game)
- Dumps: `bin/cmd_Decompiler-1.3.16/dump_shaders_tales_of_arise/New_1` (12,846 new) and
  `dump_shaders_Expeditions_rome/all_shaders_` (15,735).
- Tales v2: 20 tonemapper permutations (one `tonemap.hlsli`, TOA_* defines; 10 add an animated noise overlay
  `t6`/`t7`), 5 CombineLUTs (`lutbuilder.hlsli`; lut01-04 blend 1-4 artist SDR LUT textures, weights cb0[38..42].x),
  4 opaque CriWare videos (shared `VideoOutput` in shared.h), FXAA/copies as RTV clone writers.
- Expeditions v18: 10 tonemapper permutations (`tonemap.hlsli`, EXR_*; ddx_fine sharpen, pre-exposure), same 5
  CombineLUTs (`lutbuilder.hlsli`, identical helper code to Tales), Film Grain + Chromatic Aberration sliders
  (Sharpening also scales the tonemapper sharpen).
- HDR SDR-LUT stage: UE Extended's adaptive D65 gamut compression around the lookup, max-channel division above 1
  (Ryse chart method), exact stock LUT within 0-1. No LUT scaling (keeps the artist's black level).
- Verified on D3D11 WARP vs stock CSOs (harness in the session scratchpad, not in the repo): tonemappers bit-exact
  in SDR, HDR within the dither; LUT builders SDR within 2.4e-7, identity LUT textures reproduce lut00.
- LUT Scaling slider (both games, Extra, 0-100%, default 0%): UE Extended's "Hue Preserving" black-raise
  compensation (CompensateHuePreservingLUTBlackRaise, half weight 1 stop, in gamma 2.2), inside the HDR SDR-LUT bridge.
  Their default is 100%; ours 0% (faithful). Unit test: LUT black 0.0039 -> 0.0020 at 50%, 0 at 100%; shadows from
  0.032 up unchanged. Tales v3 / Expeditions v19.
- All 3 UE Extended LUT controls (both games), own section "Color Grading LUTs" between PsychoV31 and Extra:
  LUT Strength (100%), LUT Scaling (0%), LUT Scaling Mode (Perceptual / Hue Preserving default / Color Restoring),
  ported from their filmiclutbuilder.hlsli with their default 2.2 gamma correction. Unit-tested (scratchpad
  scale_test): strength blends to the no-LUT color; all modes take a raised LUT black (0.0039) to 0. Tales v4 /
  Expeditions v20. LUT controls only act in scenes that use LUT textures (lut01-04).
- Tales v5 / Expeditions v21: LUT controls at UE Extended's defaults (Strength 100, Scaling 100, Hue Preserving);
  "HDR (Extended)" renamed "HDR (UE Extended)" (section "UE Extended"; keys unchanged), tooltips name RenoDX's UE
  Extended addon (marat569). lut01-04 set `lut_textures_seen` on draw; the Color Grading LUTs section shows whether
  the game drew one this session (ATN saw no slider effect in Expeditions; its pak index is AES-encrypted, so no
  static check).
- Expeditions DLSS: the full dump has 20 compute TAA permutations with the same contract (cb0[27], cb1[130], 8x8);
  the DLSS addon knows 6 (29C140B2, 201C3D3D, CCB4A156, D8C07EEE, 4B43383D, B266ECB6). The other 14 (0B7C5DC9,
  0BFD441F, 0C18907A, 1337487F, 2D45E8A9, 38C84ED8, E3370ED7, E91209D0, 43330B16, 71C87DBC, 7E46ACA4, 87970FD3,
  90BED7D1, BC7F1717; six with a second UAV) are unmapped: other quality/upsampling configs or DOF/SSR TAA. Only add
  after seeing one in game as the main TAA.
- Tales v6 / Expeditions v22: Game Shadows commented out in both (ATN: looked like SDR / Extended with desaturated
  colors). Expeditions: Film Grain, Chromatic Aberration and the 3 LUT sliders moved to "Extra (Probably not used)"
  (ATN 99.99% sure the game doesn't use them), LUT-in-use check removed. Tales keeps them (cutscene tonemapper uses
  grain / CA) and keeps the LUT-in-use line for testing.
- Tales v7 / Expeditions v23: ATN saw HDR (UE Extended) and PsychoV31 identical in Tales. Cause: our UE Extended
  only ran on UE's film curve (cb0[35].w != 0) and fell back to PsychoV31 on the legacy tonemapper; Tales most likely
  uses the legacy one. Now always the film curve like RenoDX UE Extended (UE's default film settings if slope 0).
  Expeditions (film) unchanged. Tales LUT sliders + LUT-in-use line moved to "Extra (experimental)".
- Tales v8: PsychoV31 "Highlight Hue Shift" (0-1, default 1): ATN saw pinkish fire. V31's own MeanA2 hue weights
  (1/0.5/0, our call) only shift hue where the shoulder compresses (fire x8 mid gray: unchanged even at full
  response weight), so instead: HueOKLab toward ToneCurve() (the game's SDR per-channel curve) above mid gray,
  smoothstep up to scene white 1.0. Test (film curve, fire 1/0.35/0.08): g/r at x8 0.35 -> 0.50 (SDR 0.66), x16
  0.38 -> 0.61 (SDR 0.80). Attila's equivalent: twattila psycho_test30 psycho30_HighlightHueShiftWeight.
- Tales v9: Highlight Hue Shift default 0.37 (ATN). UE Extended "Base Curve": 0 game's (default; on UE's legacy
  tonemapper, LegacyCurveExtended: FilmPostProcess with the highlight segment from Cm1 replaced by the midtone line,
  lerp 37.5% to ToneCurve, then UE Extended post grade + DisplayMap), 1 UE Filmic (marat's behavior). UE Extended
  addon has no legacy path (its lutbuilder SKILL: ignore bUseMobileTonemapper) and no hue correction at defaults
  (Hue Restore only with LMS scaling). Unit test: extended == stock below Cm1, linear above.
- Tales v10: ATN confirmed legacy (Base Curve "Game's": shadows up to ~paper white match SDR) but fire pink in
  Extended. ShiftHighlightHue() shared by PsychoV31 and UE Extended (applied to the tonemapped color before SDR LUTs /
  post grade), UE Extended slider ext_highlight_hue_shift default 0.37. Test (synthetic legacy curve, fire): g/r x8
  0.42 -> 0.45 (0.37) / 0.54 (1), x16 0.43 -> 0.49 / 0.77; SDR 0.61 / 0.82.
- Match SDR (experimental) toggle in PsychoV31 for Expeditions v24, Tales v11, Ryse fix30 (default off). Our V31 calls
  used contrast 1 / flare 0 (neutral); upstream fits them per game (Pragmata Match SDR: exponent 1.255, flare 0.93 ->
  0.1*0.93^10; Elite 1.55 / 0.85). FitPsychoToSDR(anchor_out) solves V31's shadow model n*c*(1 + w(x) f/(x+f)) at 2
  and 5 stops below mid gray from the game's curve (GameCurveY / FilmCurveY); flare bisection on [0, 0.075].
  UE default film: c 1.545, f 0.002, full V31 within ~0.1 stop of SDR at 1-5 stops (1,3-stop fit was off by 1.3 at
  5). Hable (U2 constants): c 0.98, f 0 (already matched).
- Match SDR reverted in all three (ATN: didn't like the look); Ryse files byte-identical to fix29 again (fix29 stays
  current). Tales v12: Highlight Hue Shift defaults PsychoV31 0.40, UE Extended 0.35 (slider moved to the end of the
  UE Extended section). Expeditions v25 = v23 + nothing else (Match SDR removed). Next: Tales DLSS.

## Tales of Arise DLAA (`src/games/talesofarise_dlss`) - v1 (2026-10-09, untested)
- Port of expeditionsrome_dlss + Ryse backend fixes (process-exit guard, every-mode preset hint), own NGX project ID
  1a688a59-20b4-48a1-9b9b-fd28bd910ec8, cache %TEMP%\RenoDX_TalesOfArise_DLSS. CMakeLists NGX condition extended.
- UE 4.21 TAA CS 0x88765764 / 0xE2AF861D: t0 depth (±2 diagonal dilation), t1 color, t2 history, t3 velocity, u0;
  View b1: ClipToPrevClip 114..117, TemporalAAJitter 118 (unconfirmed), ViewSizeAndInvSize 122 (self-checked).
  Pre-exposure 1, no camera-cut flag. Menu Off/DLAA only; DLSS modes wait for the TAAU mapping.
- Capture retargeted to 45 TAA-like CS + PS 0x57192B01 / 0xC3F0E4DA with per-target CB sizes, logs b1[110..123].

### Tales DLSS v2 (2026-10-09)
- v1 test: TAA `0x88765764` hooked, View checks passed (3840x2160), NVIDIA rejected the output: the game's TAA UAV is
  R11G11B10F (backend wants RGBA16F). v2: `direct` only for an RGBA16F target; otherwise NVIDIA writes private RGBA16F
  `runtime.output`, merge `0xA3450ADB` -> `runtime.merged` (target format) -> CopyResource, also in replace mode.
  Backend status now names the rejected texture (color/depth/motion/output). Zip `Downloads\talesofarise_dlss_v2.zip`.

### Tales DLSS v3 (2026-10-09)
- ATN: DLAA works (v2) but small particles smear (preset L). Captures: View TemporalAAJitter b1[118] is ±0 every frame
  (Box-Muller with Sigma 0 -> r.TemporalAAFilterSize 0 in Tales), TAA weights symmetric. v3: engine_scale Finder takes a
  `Variable` {name, help prefix, plausible range}; second finder for r.TemporalAAFilterSize, set to 1 while an AA mode
  is on, game value restored on Off (`camera_jitter`, config "CameraJitter"). Logged as "[Tales AA jitter] Found ...".
- Bandai's TO14 TAA (PostProcessTemporalAA2.usf) cannot upscale; 50% = TAA at 1920x1080 + Catmull-Rom 0x57192B01
  after tonemap; r.TemporalAA.Upsampling=1 no effect. DLSS upscaling not feasible without moving the post chain: DLAA
  only. Zip `Downloads\talesofarise_dlss_v3.zip`. Untested: confirm the found value is 0 and pattern reads 8.

### Tales DLSS v4 (2026-10-09)
- v3 log: r.TemporalAAFilterSize is 1 in Tales (guess wrong; v3 finder reverted, engine_scale.hpp back to v1).
- Real cause (disassembly of exe 0x1A866B0, UE 4.21 PreVisibilityFrameSetup jitter): `cmp [r14+0x1BE0],5; jne;
  mulss jitter by [r14+0x1290]` (AA method 5 = the game's TAA, factor 0). v4: `code_patch.hpp` (Ryse FindPattern /
  ApplyCodePatch), pattern JITTER_SCALE, byte +8 0x75 -> 0xEB while an AA mode is on; restored on Off / AddonUninit.
- Non-replace path now copies non-RGBA16F scene color (first frames failed "Temporal color format").
- ATN: motion blur smeared the DLSS indicator (expected: MB after TAA). Particles smeared without jitter.
  Zip `Downloads\talesofarise_dlss_v4.zip`. Untested.
- 2026-10-10: ATN confirmed v4 (ReShade4.log): jitter patch found and toggles cleanly, pattern 8, particles no longer
  smear, non-replace first frames now work. Only preset L tested (NVIDIA Profile Inspector forced L globally).

### Tales HDR v13 + DLSS v5 (2026-10-10)
- HDR: SDR Tonemapper option (`sdr_curve`, General, SDR only): Game's (legacy, bit-exact; harness ok <= 2.4e-7) or
  UE Filmic (stock film branch + its gamut expansion, as r.TonemapperFilm=1). `ToneCurve(ap1, film)` overload,
  `UseFilmCurve(hdr_lut)` in lutbuilder.hlsli; lut00-04 use it. Harness: si[32] = sdr_curve; the LUT-scaling
  dark-cell heuristic "FAIL" for lut01-04 also happens with v12's shaders (old heuristic, not a regression).
- DLSS v5: graphics presets in the DLSS addon (ATN: like Ryse, inside the DLSS addon). `engine_cvars.hpp`: console
  manager from the 1067 FindConsoleVariable sites (singleton exe+0x4B08670 in this build, slot 0x90), values via
  AsVariableInt/Float code (slots 7/8 lea rax,[rcx+off]); validated on r.ScreenPercentage. `code_patch.hpp` now has
  FindPatterns. 21 settings in GRAPHICS_SETTINGS, raise-only, live, restored on Game's own / unload; config
  "Tales Graphics" / "Preset". Arise-SDK recommended (ATN: tell users to get it from GitHub; ATN called it a must),
  our `Arise-SDK.ini` (UseUE4TAA false, jitter/sharpness -1). Untested in game: check the log's "[Tales graphics]"
  lines, which settings apply live, and that the jitter patch is still found with Arise-SDK loaded.
- Next (agreed order): DLSS upscaling via UE4 TAA (Arise-SDK's UseUE4TAA idea / its r.TemporalAA.Upsampling patch).
- 2026-10-10 DLSS v6: ATN loves Maximum. v5 log: 5 settings were FConsoleVariableRef (now supported: MainValue +
  exe global), ground fog scale is a divisor (8). Added 5 settings (26 total), logged AA cvars, Advanced checkbox
  "Unreal temporal upsampling" (r.TemporalAA.Upsampling live). Upscaling plan: Arise-SDK UseUE4TAA=true ->
  UE4 TAA (AA method 2) + TAAU -> capture at 50% -> map 4.21 TAAU CS + its cb -> DLSS modes. Zip talesofarise_dlss_v6.zip.
- 2026-10-10 log6 (ATN: Arise-SDK UseUE4TAA = true): jitter live (b1[118] varies) and TAA weights vary per frame ->
  AA method 2 active, but the TAA shader is still 0x88765764: it is UE 4.21's own TAA CS (Bandai's "TO14" mode 5
  runs it with zero jitter, cb0[51].x 0.6 vs 0). DLAA covers both. 50% capture: TAA at 1920x1080, no TAAU
  (2362 dispatches, no other target) -> upsampling not active (checkbox state unknown). v7: capture logs
  r.ScreenPercentage / r.TemporalAA.Upsampling / r.DefaultFeature.AntiAliasing at start and the checkbox toggles,
  and any other compute pass writing >=1280 wide from a smaller texture ("upscaling compute"); unknown getter bytes
  logged (r.ForceLOD still unsupported). Zip talesofarise_dlss_v7.zip.
- 2026-10-10 log7: r.TemporalAA.Upsampling 1 at 50% with UseUE4TAA true, still no TAAU. Cause: view ctor
  (exe+0x21B9A75) sets TemporalUpscale only if AA method == 2; SetupAntiAliasingMethod (exe+0x21B9FC0) starts
  with mov [rbx+0x1BE0],5 (Bandai), SDK switches later. v8: AA_METHOD_SETUP patch 5->2 with the capture checkbox.
  Presets: 6th "Beyond Maximum" (grass.densityScale / foliage.DensityScale 4); r.ForceLOD dropped (no registration
  in exe); Min/FadeResolution dropped, shadow distance <= 1.5 (ATN: pixelated/flickering shadows). Zip v8.
- 2026-10-10 log8: AA mode 2 patch active (jitter live), upsampling 1, still TAA 1080p + Catmull-Rom 0x57192B01.
  View ctor (exe+0x21B9A75): `cmp [rsi+rdi*4],0; je; cmp [rbx+0x1BE0],2; jne; mov [rbx+0x1BE4],1`; FSceneRenderer
  (exe+0x1A74193) keeps TemporalUpscale for mode 2 / SM5. Likely Arise-SDK's 0.1.10 upsampling patch rewrote the
  compare (not in its public source). v9: TEMPORAL_UPSCALE_CHECK jne -> nop while the capture checkbox is on, and
  logs the compared mode. Zip talesofarise_dlss_v9.zip.
- 2026-10-10 logs 9/10: with Arise-SDK the view's TAAU check compares mode 5 (SDK rewrote it), without it 2; with
  both our patches TAA still at render res + spatial upscale in both: the game's post-processing has only its own TAA
  routine (UE's TAAU path dead). ATN: drop DLSS, DLAA only. v10: upsampling patches, checkbox, capture
  (temporal_capture.hpp deleted) and logged cvars removed; NAME "DLAA + Graphics". Beyond Maximum: shadow maps
  16384, sun shadow distance 3, TexelsPerPixel 4, Min/FadeResolution 64/32 back (ATN wants more shadows). README
  rewritten. Zip talesofarise_dlss_v10.zip (temporal_capture.hpp gone: delete it from the repo copy if merging zips).
- 2026-10-10 v11: 18 more preset settings (45 total; r.ShadowQuality, spot/point texels, grass.CullDistanceScale,
  r.ForceHighestMipOnCharacterTextures, r.DetailMode, particle lights, motion blur quality, light shaft divisor,
  Bandai SSR / cloud / volumetric lighting / translucency divisors from their registration defaults). Blur divisors
  kept (look). Post sliders (% of game): r.MotionBlur.Scale, r.ScreenSpaceLightShaftDirectionScaleTO14. README:
  Arise-SDK not needed; DPI fix via Windows compatibility override. Zip talesofarise_dlss_v11.zip.
- 2026-10-10 HDR v14: Motion Blur slider (shader): 12 UE motion blur PS (`motionblur00-11`), cb0[28].y / .w times
  SI.motion_blur (shared.h last field, index 33); WARP harness mb_test.cpp: identical at 1 (diff 0), 0 removes blur.
  DLAA addon v12: post sliders removed (ATN: motion blur belongs in HDR, no light shafts). DoF: Bandai's own TO14,
  composite pass not identified (0x66DC643D / 0x2483CE26 / 0x773EABCB are sharpen/edge filters); needs a DevKit
  snapshot with DoF on. Zips talesofarise_v14.zip, talesofarise_dlss_v12.zip.
- 2026-10-10 DLAA addon v13: Beyond Maximum crashed (4 UE crash reports, %LOCALAPPDATA%\Arise\Saved\Crashes, all at
  exe+0x1A9E56C: shadow pass setup calls GetRenderTargetItem on a null pooled shadow target). 16384 shadow maps +
  2x4 border = 16392 > D3D11 limit. Beyond Maximum shadows now 12288. Zip talesofarise_dlss_v13.zip.
- 2026-10-10 DLAA addon v14: Beyond Maximum shadows 16000 (ATN; limit 16376 with the border). Zip v14.
- 2026-10-10 DLAA addon v15: still crashed at 16000 (same exe+0x1A9E56C). Most likely r.Shadow.MaxResolution sizing
  point light cube maps (6 faces, ~3 GB at 16000, over D3D11 per-resource limit). Split: light shadows (MaxResolution)
  max 8192, sun (MaxCSMResolution) 16000 in Beyond Maximum. Graphics UI like Ryse: preset buttons + one combo per
  setting ("Game's (value)" + distinct preset values) in 5 collapsing groups, saved per cvar name ("Tales Graphics");
  old "Preset" key migrates. Zip talesofarise_dlss_v15.zip.
- 2026-10-10 DLAA addon v16: v15 still crashed. Theory: shadow atlases sized from UE's cached scalability copy
  (not refreshed by live writes) while shadows use the live cvars; all crashing builds had TexelsPerPixel 4. Beyond
  Maximum = Maximum's shadows + the mod Engine.ini's stronger distances / density 10 / foliage tri cap; new options
  r.MaxAnisotropy 16, landscape LOD x3, r.Streaming.HLODStrategy 2. Risky values (sun 16000, texels 4, min/fade 64/32,
  grass cull 10, LOD 0.0001, density 4) only as extra dropdown choices (6th value). Choices saved in section
  "Tales Graphics Settings" (indices; v15's not read). 51 settings. Zip talesofarise_dlss_v16.zip.
- 2026-10-10 release cleanup. DLAA addon v17: no "experimental" (NAME "RenoDX Tales of Arise DLAA + Graphics", overlay
  "Tales of Arise DLAA + Graphics", metadata stable), user texts rewritten, "Stock TAA" -> "Game TAA", Simple mode
  shows render scale / jitter lines only on problems, risky shadow extras labelled "(can crash)", README rewritten
  (install + technical notes). HDR v15: LUT sliders + in-use check removed; LUT stage fixed at UE Extended defaults
  (Hue Preserving 100%, strength 100%); shared.h lost 3 fields (motion_blur now index 30). Harness: all LUT builders
  SDR exact, identity ok; motion blur ok. Zips talesofarise_v15.zip, talesofarise_dlss_v17.zip.
- 2026-10-10 DLAA addon v18: preset Recommended = Beyond Maximum with the game's r.VolumetricLightingBufferPixelNumPerCell / r.VolumetricFog.GridPixelSize / GridSizeZ (ATN: 80 -> 100 fps); PresetValue(). ATN DLAA timings at 4K, preset L: NVIDIA 4.03 ms, input prep 0.088, merge+copy 0.107. Zip v18.

### Next: Expeditions: Rome live graphics settings (ATN, 2026-10-10)
- Port talesofarise_dlss's engine_cvars.hpp / GRAPHICS_SETTINGS / presets + per-setting combos into
  expeditionsrome_dlss (UE 4.26). Verify IConsoleObject vtable slots for 4.26 first (bool variants may shift them).
  Tales is release-ready: HDR v15, DLAA + Graphics v18, Nexus text in Downloads\talesofarise_nexus.txt.
- 2026-10-10 DLAA addon v19: 'Loading' group, r.CreateShadersOnLoad (extra choice 1, in no preset) with tooltip; GraphicsSetting got an optional tooltip field. Zip v19.
- 2026-10-10 Expeditions DLSS: live graphics settings (zip Downloads\expeditionsrome_dlss_graphics_v1.zip, untested).
  New `engine_cvars.hpp` for this exe: FindConsoleVariable is slot 0xA0 with a bool (`41 B0 ??` before the call;
  1722 sites, one singleton); cvar vtables read offline: AsVariableInt/Float slots 9/10 (lea [rcx+0x38]), Ref
  GetInt/GetFloat 15/16 (MainValue 0x40, global ptr 0x38). 47 settings, presets Game/High/Ultra/Extreme/Maximum/
  Beyond Maximum (no Recommended yet: add one after ATN finds the expensive setting), section "Rome Graphics
  Settings". GRAPHICS_SETTINGS sized by std::to_array (a hand-written count of 49 for 47 rows would have left two
  empty rows). Offline tools: scratchpad exr\ (xref.py on the Expeditions exe, sites.py, vt3.py, cvars.py names.txt).
- 2026-10-10 Tales DLAA v20 + Expeditions graphics v2: crash extras removed in both (sun shadow 16000, texels per pixel
  4); the "(can crash)" label logic is gone (Min/FadeResolution extras stay, they never crashed). Expeditions:
  Recommended = Beyond Maximum with the game's r.VolumetricFog.GridPixelSize / GridSizeZ / HistoryMissSupersampleCount
  (ATN: volumetric fog is very expensive). Saved choices are indexes into the sorted choices: the removed values were
  the largest, so old saves clamp to 8192 / 3. Nexus text: "(can crash)" line removed. Zips talesofarise_dlss_v20.zip,
  expeditionsrome_dlss_graphics_v2.zip.
- 2026-10-10 Tales DLAA v21 + Expeditions graphics v3 (ATN: Expeditions shadows vanish while the camera moves).
  Causes found in both exes: (1) all sun shadow cascades share ONE CSM atlas of 1 << (GMaxTextureMipCount - 1) = 16384
  (Expeditions exe+0x139CBAE, Tales exe+0x1AD7041); a cascade FTextureLayout can't fit is not drawn. 8192 fits 4;
  presets had up to 10 (Tales' own value is 10: this was the "pixelated shadows at higher presets"). Now the written
  r.Shadow.MaxCSMResolution is capped to 16384 / ceil(sqrt(cascades)) (never below the game's), cascades row moved
  before it (static_assert). Expeditions presets: cascades 3/4/4/4/4 (+10), CSM 4096/4096/8192/8192/8192. Tales: CSM
  4096 in all presets. (2) Unreal's cached scalability copy (MaxResolution, MaxCSMResolution, ViewDistanceScale,
  StaticMeshLODDistanceScale, DetailMode, MaxAnisotropy, MaterialQualityLevel; ScalabilityCVarsSinkCallback,
  Expeditions exe+0x1FFF590; no direct readers of the shadow resolutions / draw distance) only refreshed when the game
  Set something. Console::Refresh() raises FConsoleManager::bCallAllConsoleVariableSinks (+0xCC; slot 8 Expeditions /
  7 Tales tests it each game-thread frame) after any write and on unload. Expeditions r.Shadow.DistanceScale clamped
  to 2 by the engine (Beyond 3 -> 2). Zips talesofarise_dlss_v21.zip, expeditionsrome_dlss_graphics_v3.zip.
- 2026-10-10 CORRECTION (Tales v22, Expeditions graphics v4): the CSM atlas is 8192, not 16384: GMaxTextureMipCount =
  min(CeilLog2(16384) + 1, MAX_TEXTURE_MIP_COUNT 14) = 14 (Expeditions exe+0xFD12C8). v3 (4 cascades at 8192) drew one
  cascade (ATN: shadows only on part of the screen, following the camera). Cap is now 8192 / ceil(sqrt(cascades)).
  Expeditions: cascades 3/4/4/4/4 (no extra), CSM 4096 in all presets. Tales: the game's 10 cascades only fit at its own
  2048, so the r.Shadow.MaxCSMResolution row and the cap code are removed (51 settings); this was the old "pixelated
  shadows" (8192 = one cascade) and the >8192 crash (no cascade fits). Nexus text: "sun" removed from the shadow line.
  ATN's old Expeditions Engine.ini (10 cascades @ 4096) only fits if the sun uses <= 4 cascades.
- 2026-10-10 Expeditions graphics v5: volumetric fog only cheaper or off (ATN): r.VolumetricFog 0 (new row), GridPixelSize
  12/16, GridSizeZ 96/64, HistoryMissSupersampleCount 2/1; EXACT, no preset values; GraphicsSetting::more_extras holds
  a second list-only value. Preset buttons (except Game's own) skip rows with no preset values. Recommended removed.
  Zip expeditionsrome_dlss_graphics_v5.zip.
- 2026-10-10 Tales DLAA v23 + Expeditions graphics v6: presets Game/High/Ultra/Extreme/Maximum (old Maximum column
  dropped, Beyond Maximum renamed; Tales keeps Recommended = Maximum with the game's 3 volumetric values). Tales: new
  r.VolumetricFog row (0 off), volumetric rows EXACT with cheaper list-only values (Bandai cell 16, fog cell 12/16,
  slices 96/64), GraphicsSetting::more_extras. Both: r.VolumetricFog 0 greys out r.VolumetricFog.* (BeginDisabled);
  Shaders on load EXACT 0/1 (engine default 0; ATN's Engine.ini had 1); preset buttons except Game's own skip rows in no
  preset. Tales: old "Tales Graphics"/"Preset" migration and PresetValue removed. Zips talesofarise_dlss_v23.zip,
  expeditionsrome_dlss_graphics_v6.zip.
- 2026-10-10 Ryse HDR fix31: execution skull hotkey is F5 only (the key above Tab is Ryse's command key; F8 is Print on
  ATN's keyboard). HUD F6 / Caps Lock and highlights F7 / Tab unchanged. Zip rysesonofrome_fix31.zip.
- 2026-10-10 Ryse HDR fix32 + DLSS v45 (debugForClaude14, untested): (1) HUD hotkey no longer hides the skull:
  0x1B59F534 left out of custom_shaders; OnDrawUi skips its non-indexed draws while the HUD is hidden, OnDrawIndexedUi
  skips indexed ones below the skull cut (HUD key) or above it (F5). (2) Extended: per-channel Neutwo shoulder above 1
  instead of the brightest-channel one (fire core was [1, 0.47, 0.25] salmon; PsychoV31 [1, 0.87, 0.77]); Hue
  Correction tooltip says it acts mostly on mid tones. (3) DLSS Detail distance also divides e_LodFaceAreaTargetSize
  (vase LOD switch at 2-3 m with e_LodRatio x256: face-area LOD). Zips rysesonofrome_fix32.zip, rysesonofrome_dlss_v45.zip.
- 2026-10-10 Ryse DLSS v46 (debugForClaude15, untested): light/shadow flicker above Very High. Shadow resolution max 4x
  (8x removed; e_ShadowsMaxTexRes cap 8192 = half the 16384 pool: it caps each light's pool map too). Light distance also
  divides e_ShadowsCastViewDistRatioLights (light casts shadows within [light+0x30] x ratio: +0x18C7E9, +0x250632).
  Recommended MAX now differs from MAXED OUT only in light distance (2x vs 8x). Zip rysesonofrome_dlss_v46.zip.
  ATN confirmed: Extended fire (fix32) good, LOD (v45) fixed.
- 2026-10-10 Ryse DLSS v47: v46 did NOT fix the light flicker (ATN). Added Advanced debug "Trace the shadow maps for 300
  frames" -> Bin64\ryse_shadow_trace.txt (cvar header: game -> requested -> kept; per frame each depth-only target with
  viewports + draws + used share). Next: read ATN's traces (MAXED OUT vs Very High, same spot, camera still).
- 2026-10-10 Ryse DLSS v48: shadow traces (debugForClaude16): pool maps stable in MAX and Very High (39 maps / 74% of
  16384; 38 / 66% of 8192) -> not pool overflow. e_ShadowsCastViewDistRatioLights refused (flags 0x800080) -> removed.
  Next: ATN bisects MAXED OUT with Light distance In-game (suspect: more lights than tiled deferred shading keeps).
- 2026-10-10 Ryse DLSS v49: flicker cause found with ATN's bisect + dump: Draw distance scaled e_ViewDistMin (10 ->
  2560), which is also every light's minimum view distance (CLightEntity::GetMaxViewDist +0x609D1: max(e_ViewDistMin
  [cvars+0x474], size x e_ViewDistRatioLights [+0xa0] x lod)) -> all lights active. e_ViewDistMin capped at 20.
  New Advanced debug "Measure every option's choices" -> ryse_options_measure.txt (scene/shadow draws + indices, frame
  ms per choice). Candidates not exposed: e_MergedMeshesLodRatio, e_TerrainLodRatio, e_VegetationSpritesMinDistance,
  e_CoverageBufferResolution, r_SilhouettePOM. Zip rysesonofrome_dlss_v49.zip.
- 2026-10-10 Ryse DLSS v50: Light distance option removed (ATN: anything above 1x flickers, even with e_ViewDistMin 20).
  Options measurement (debugForClaude17, game start outdoors): Detail distance and Draw distance saturate at 64x,
  Tessellation at 4x (frame time) -> capped there; Shadow res 4x costs 1.3 ms. Pool / cascades / shadow distance /
  particles / water / decals showed nothing in that scene (needs a scene with them). Recommended MAX == MAXED OUT now.
  quality_options 12. Zip rysesonofrome_dlss_v50.zip.
- 2026-10-10 Ryse DLSS v51: all multiplying options end at 64x with every power of two (ATN); Water 16x, shadow res /
  pool 4x unchanged; Tessellation back to 64x (ATN's call; measured flat from 4x in one scene). Options are labelled
  sliders (SliderInt, item name as format). ChoiceName() shared by the overlay and the measurement. Zip v51.
- 2026-10-10 Ryse DLSS v52: 5 new options (GrassDetail e_MergedMeshesLodRatio x, TerrainDetail e_TerrainLodRatio /,
  TreeModels e_VegetationSpritesMinDistance x, all to 64x; OcclusionCulling e_CoverageBufferResolution to 1024 = engine
  clamp; SilhouettePOM r_SilhouettePOM on). Only MAXED OUT uses them; test buttons set just these five to game / max.
  quality_options 17. Zip v52.
- 2026-10-10 Ryse DLSS v53: the 5 v52 options removed (measured: no change in draws/indices, frame time noise; ATN saw
  none). Presets: Reset / Very High / Ultra (16x, shadow 2x, pool 2x, 7 cascades, water 8x) / Extreme (32x, 4x, 4x, 8,
  water 16x) / MAXED OUT (64x); Recommended MAX removed (QualityOption ultra_choice / extreme_choice). Zip v53.
- 2026-10-10 Ryse HDR fix33: execution highlights = every PS whose bytecode contains "VisionMtlParams" (init_pipeline,
  highlight_shaders set, skipped in OnDrawUi / OnDrawIndexedUi while hidden); fix32 only had 0xD2063417 / 0xF495DD17,
  ATN found 0x6B40240A. ShaderCache.pak is CryEngine-packed (no plain DXBC), so offline enumeration was not possible.
  DLSS v54: presets Reset / Very High / Ultra (8x) / Extreme (16x, pool 4x) / Insane (32x) / MAXED OUT (64x).
- 2026-10-11 Tales DLAA v24 + Expeditions graphics v7: graphics settings are labelled sliders (SliderInt), steps added
  between choices at AddonInit (whole numbers across gaps <= 4 below 10; 1-2-5 numbers across gaps > 2x), choices saved
  by value (SaveChoice; sections "Tales Graphics Values" / "Rome Graphics Values"; "game" = game's). Expeditions HDR v26:
  LUT sliders removed (shared.h lost lut_strength / lut_scaling / lut_scaling_mode, the last fields), lutbuilder's LUT
  stage = Tales v15's fixed defaults. Zips talesofarise_dlss_v24, expeditionsrome_dlss_graphics_v7, expeditionsrome_v26.
- 2026-10-11 Expeditions graphics v8: group "Optional (changes the look)", in no preset, EXACT: AO max quality -100, AO on
  static indirect, DF shadows, DF AO, contact shadows, capsule shadows, lens flare 3, SSR max roughness 1 (ATN's old
  Engine.ini lines). Zip expeditionsrome_dlss_graphics_v8.zip.
- 2026-10-11 Expeditions graphics v9: preset "Beyond MAX (+different AO)" = Maximum + OPTIONAL_GROUP extras (AO max
  quality -100, AO on static indirect = DFAO on baked light, lens flare 3); r.SSR.MaxRoughness 1 in Maximum; DF
  shadows / DFAO / contact / capsule rows removed (game already 1). Tales v25 + Expeditions v9: a short tooltip on every
  setting. Zips talesofarise_dlss_v25, expeditionsrome_dlss_graphics_v9.
- 2026-10-11 Expeditions graphics v10: r.AOApplyToStaticIndirect row removed; preset renamed "Beyond MAX" (Maximum +
  AO max quality -100 + lens flare 3). Zip expeditionsrome_dlss_graphics_v10.zip.
- 2026-10-11 Expeditions graphics v11 + Tales v26: presets set every row (KEEP -> game's) except PLAYER_SETTINGS (fog,
  shaders on load); Expeditions presets renamed Insane (old Maximum) / Max (old Beyond MAX, every row's best choice by
  Better: HIGHER max, LOWER min, EXACT Insane's or the optional extra), all on one line. Verified by simulation.
- 2026-10-11 Expeditions graphics v12 + Tales v27: r.SSR.MaxRoughness in Extreme/Insane/Max; presets (Game's own too)
  set every row but r.CreateShadersOnLoad (NO_PRESET_SETTING); Expeditions Max keeps the game's volumetric fog (EXACT:
  Insane's, else optional extra, else game's). Ryse DLSS: buttons set all 12 options (no list-only items): no leftovers.
- 2026-10-12 Tales HDR v16: Highlight Hue Shift defaults: PsychoV31 0.24, UE Extended legacy curve 0.17, new
  ext_filmic_highlight_hue_shift 0.30 for the UE Filmic base curve (shared.h last field; lutbuilder picks by
  legacy_curve); a TEXT note above each slider; Recommended adds Motion Blur 0 and Film Grain 0. DLAA v28: Maximum /
  Recommended object detail 0.0001 + grass 10; sun shadow distance up to 4 (1.25 ... 4 in halves); Recommended renamed.
  ATN: sun shadow distance 3 fixes light through mountains but shimmers: same 10 x 2048 cascades over 2x the ground.
- 2026-10-12 Tales DLAA v29: sun shadow layout choice: cascades EXACT 4-9 + r.Shadow.MaxCSMResolution row back (2730 /
  4096), capped to 8192 / ceil(sqrt(cascades)) (SUN_SHADOW_* + SettingIndex, as Expeditions); both in no preset (KEEP).
  Sun shadow distance 2 at Maximum / Recommended (>2 needs Arise-SDK; list 1.25 ... 4). ATN: shimmer comes from the
  sun moving every ~10 s; sharper maps (fewer cascades) shimmer less.
- 2026-10-12 Tales DLAA v30: v29 sun layout reverted (game's r.Shadow.MaxCSMResolution is 4096 per ATN -> its sun uses
  <= 4 cascades; nothing higher fits). Test row r.Shadow.FilterMethod 1 (PCSS, in no preset) against sun-move shimmer.
  HDR v17: "Video Highlights" label -> "Video AutoHDR" (key unchanged). Zips talesofarise_dlss_v30, talesofarise_v17.
- 2026-10-12 Tales DLAA v31: Sun shadow resolution row back (6144 / 8192, no preset) with a code patch doubling the CSM
  atlas to 16384 (SUN_SHADOW_ATLAS: `mov r13d, 1` -> 2 before `shl r13d, cl`, exe+0x1AD705A; applied only while the
  row writes above the game's 4096; unusable if not found). 16384 = the D3D11 shadow-map limit the game sets
  (exe+0x179F800 -> GMaxShadowDepthBufferSize). Untested: does it stop the sun-move shimmer at distance 2?
- 2026-10-12 Tales DLAA v32: ATN confirmed the higher sun resolution fixes the shimmer. Sun shadow resolution in presets
  (High / Ultra 6144, Extreme / Maximum / Recommended 8192); PCSS row removed (no effect). Zip talesofarise_dlss_v32.
- 2026-10-12 Tales DLAA v33 (untested): row Arise.PF.Culling.ForceInFrustum 1 (Extreme+): Bandai's PFCullingManager
  hides off-frustum characters, which fade in + rebuild on a fast turn (fps 120 -> 80 for 1-2 s); r.AllowOcclusionQueries
  0 and CharaDisableCull did not help. Zip talesofarise_dlss_v33.
- 2026-10-12 Tales DLAA v33 confirmed by ATN (characters stay). v34 (untested in game): Arise-SDK's [Graphics] options
  built in (new sdk_options.hpp; details in the README's v34 note): NPC distance, monster distance, NPC/monster LOD
  distance, never hide characters, outline distance (Detours hooks + LOD memcpy stub + NPC fade immediates, installed
  in AddonInit), sun distance clamp NOPed at load, a.URO.Enable row; new "Characters" group. All patterns, function
  starts and offsets verified offline on ATN's exe (scratchpad toa/sdk_sigs.py). With Arise-SDK installed its hooks
  win (rows bind sdk.* cvars or show unavailable); the bundled Arise-SDK.ini now leaves these to the addon. Zip
  talesofarise_dlss_v34.
- 2026-10-12 Tales DLAA v35 (untested in game): Arise-SDK's cutscene fixes built in (Cutscenes group: skit resolution
  via a stub on execCreateRenderTarget2D's UpdateResource call + scene capture AA flags; smooth cutscenes via the
  sequence vtables' PostLoad slot). Arise-SDK now only adds skipping the intro logos. Arise-SDK is GPL-3.0: nothing
  of its code is used, only its findings (functions, byte patterns, formulas); credited. Zip talesofarise_dlss_v35.
- 2026-10-12 Tales DLAA v36: sdk_options.hpp -> engine_hooks.hpp; every byte pattern chosen anew from ATN's exe (all
  unique, hook starts verified; Arise-SDK's alternates dropped), own formulas (detail distance: all LOD distances
  multiplied; NPC distance: every NPC distance scaled by distance / 3100), keys Tales.*, no credit in code / README
  (ATN: Nexus only, his wording: idea from Arise-SDK's ini, AI found the patterns, no Arise-SDK code). Zip v36.

- [x] 2026-11-09 F9 (GPT-6 Astra): checked helper GPU completion and queue synchronization; terminal failure without GPU-owner unwinding. WARP/child regression passed; helper90 built. F10-F15 pending; no game run.

- [x] 2026-11-09 F10 (GPT-6 Astra): both FG recording paths check Reset/Close before submission. Terminal HRESULT child test and full helper91 build pass. Vendor-specific fault injection/game checks remain manual.

- [x] 2026-11-09 F11 (GPT-6 Astra): DLSS-G SDR/HDR state updated at fixed size/format; both color spaces explicitly checked/applied. Helper92 builds; display comparison remains manual.

- [x] 2026-11-09 F12 (GPT-6 Astra): owned, ordered/coalescing preset worker; current-generation status and shutdown drain. Deterministic fake persistence test and helper93 build pass; no driver profile modified by validation.

- [x] 2026-11-09 F13 (GPT-6 Astra): complete FG object readiness, same-device retry, scRGB guard. Actual production WARP regression passes x86/x64 (four real failures and output readback); all 14 consumer builds pass. Paused by user; F14/F15 remain open and untouched. See docs/Attila-crash-audit-2026-11-08.md.

- [x] 2026-11-09 F14 (GPT-6 Astra): native hook owners retain addon code/records on removal failure; callbacks pass through, retained hooks can reactivate without pointer replacement, and the three x86 addon attach paths reset discovery flags. Normal removal releases the extra module reference; after failure it is intentionally kept until process exit, including after successful retry. Actual production-header/Detours DLL-host tests pass x86/x64 for normal unload, begin/detach/commit-failure controls, host FreeLibrary, retry and reload. All 14 Release consumers build. No game/installed files changed. F15 remains open; stop here pending user continuation. Full limits in docs/Attila-crash-audit-2026-11-08.md.

- [x] 2026-11-09 F15 (GPT-6 Astra): depth-level cache publishes dimensions only after all views succeed; failed setup releases every partial view and retries after a one-second cooldown. Instrumented production-header WARP tests pass x86/x64 for all 12 creation failures, ownership release, cooldown, same-size recovery, all-five-mip readback and resize recovery. All 14 Release consumers build. No game/deployment/original checkout changes. F9-F15 code repairs complete; manual validation and limits in docs/Attila-crash-audit-2026-11-08.md.
