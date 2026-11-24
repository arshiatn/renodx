# tw_vmf_diffuse: vMF diffuse S1 gates

`src/games/twattila/vmf_diffuse.hlsli` (`VmfRoughness`, `VmfLobe`, `VmfLobeFast`) checked against its CPU twin
(`tests.cpp`, a float port of S0's `vmf_s0.cpp`), and the twin checked against S0's walk, model and beta check points.
Both include the one `src/games/twattila/vmf_diffuse.h` (the constants and S0's beta / fast-form tables). The header
runs in `vmf_probe.hlsl` on WARP twice: fxc `cs_5_0 /O3` on D3D11 and dxc `cs_6_5 -O3` on D3D12; dxc `cs_6_9` is
compiled and checked statically. Design: `designs/vmf_diffuse.md` sections 2-4 and 9, `designs/vmf_s0/s0_report.md`
sections 7-8.

- `s0_points.h`: S0's reference points, independent of `vmf_diffuse.h`: 2040 T0 points (walk and model; r 0.005 /
  0.05 / 0.15 / 0.26 / 0.40, c 0.1 / 0.5 / 0.8, u >= 0.2) and 80 beta check points (10 r x 8 base off the nodes),
  written by `python -I make_points.py <designs/vmf_s0/t0_points.csv> <designs/vmf_s0/beta_check.csv> s0_points.h`.
- Run (WARP only, no GPU job): `powershell -File test\tw_vmf_diffuse\run.ps1 [-Bin <folder of dxcompiler.dll and
  dxil.dll>]`. Builds with clang-cl into `tools\tw\testbin\tw_vmf_diffuse`, logs to `last_run.txt` there, and exits 0
  only when every gate passes and every control fails.

## Why the bars catch slips

- WARP vs twin is the same code, so the bar is 1e-4 x max(|twin|, 1) on every output of every record (measured: fxc
  6e-7, dxc 3.4e-5). Records include the fade ramp (n.v 0.03 / 0.05 / 0.08).
- WARP's own "finite" check cannot see a NaN inside the lobes: GPU min / max return the non-NaN operand, so the clamp
  turns a NaN into 0 or 4. The proof is the twin's finite check (`std::clamp` keeps a NaN) plus the 1e-4 bar.
- A slip in `vmf_diffuse.h` moves the twin as well, so the header's numbers have their own references: the beta table
  against S0's check points, and the fade against the design's 0.1 (R = 1 + (n.v / 0.1) (unfaded twin - 1)).
- vmr1_'s 9 one-line mutations (`designs/vms1_review_work/mutations_s1fix.ps1`, each rebuilt from a copy) all fail:
  - c303 r^5 -> r^4, c204, the series without x2, fall cubed, the fm ramp of Rev 1 and the erf a5 typo fail WARP vs
    twin;
  - beta[7][3] + 0.001 / + 0.01 fail the beta check (6.7e-4 / 6.7e-3, bar 2e-6);
  - fade 0.08 fails the fade gate (0.20).

## Results (2026-11-24; twin / fxc WARP / dxc WARP)

| Gate | Bar | Result |
|---|---|---|
| erf (A-S 7.1.26) vs `std::erf`, [-6, 6] | <= 1e-6 (probe 2e-6); erf(0) = 0, erf(+-inf) = +-1 exactly | 4.1e-7 (twin); 2.7e-7 on the probe's inputs, all three; ends exact |
| T0p twin vs S0's double model | relative <= 1e-4 | 1.0e-6 |
| T0w twin vs the walk, per (r, c) | rms <= 3 % | 0.23-2.83 % (worst r 0.26, c 0.1, as S0) |
| T2 reciprocity, u >= 0.1 | <= 1e-5 x max(R, 1) | 4.8e-7 / 4.6e-7 / 3.5e-7 |
| T2 R in [0, 4] | | [0, 4] |
| T2 denominator u_l sigma(-u_v) + u_v sigma(u_l), u in [1e-4, 1]^2, r in (0, 0.40] | >= 0.5 u_l u_v | min 0.6209 u_l u_v (r 0.40, u 1) |
| T2 beta table vs S0's 80 check points | <= 2e-6 | 6.7e-7 |
| T2 bond albedo with the beta table, r 0.005-0.40, base 0.05-0.95, R as shipped | within 1 % | max 0.20 % (0.20 % without the fade) |
| T3 r 0, slider 0, rho 0 | exactly 1.0f | exact, all three (`VmfLobe`, `VmfLobeFast`, `VmfLobe(VmfRoughness)`) |
| T4 finite, R in [0, 4], 1 at n.v <= 0, 0 at n.l <= 0 | | all three, every record (incl. r 1e-12 / 1e-30, base 0 / 1.5, inconsistent l.v) |
| T4 fade below n.v 0.1 vs the design's 0.1 | <= 1e-4 x max(R, 1) | 7.8e-8 / 5.7e-7 / 3.2e-5 |
| T4 sigma across the Eq. 4 / 5 and 5 / 6 seams | absolute <= 0.02 | 0.0067 (alpha 0.25), 0.0168 (alpha 0.9) |
| T4 R across each seam (r x (1 -+ 1e-4)), u >= 0.2, no blend | jump <= 0.03 per seam | Eq. 4 / 5: 0.0190; Eq. 5 / 6: 0.0250 (both at u 0.2, l.v -0.92, base 0.05); u >= 0.1: 0.064 / 0.048 |
| T4 WARP vs twin, every output | <= 1e-4 x max(\|twin\|, 1) | fxc 6.0e-7, dxc 3.4e-5 |
| fast: `VmfLobeFast` vs `VmfLobe` at base 0.5 | weighted rms <= 3 %, max <= 10 % (u >= 0.2) | <= 1.18 % / 4.5 % |
| static | no warnings; no indexable temp / icb (fxc); no alloca / wave op / half (dxc 6.5 and 6.9) | clean; fxc 848 instruction slots for the probe (2 `VmfLobe` + `VmfLobeFast`); DXIL 16676 / 14704 bytes |

## Controls (each must fail; all did)

| Control | Gate it fails | Result |
|---|---|---|
| The preprint's series (no x2) | T0w, every cell r >= 0.05 | 4.2-38.6 % rms (passes only at r 0.005, 0.4 %: lobe too small) |
| | WARP vs that twin | 1.00 |
| The paper's Section 5 remap | T2 bond albedo | 28.6 % |
| | WARP vs that twin | 1.13 |
| beta 0 (c = base) | T2 bond albedo | 35.9 % |
| | WARP vs that twin | 0.56 |
| No fade | T4's 1 at n.v <= 0 | not 1 (R at sun 45 / n.v 0 behind, r 0.40, base 0.5: 1.51 unfaded, 1.00 faded) |
| | WARP vs that twin | 1.00 |
| Every beta + 0.001 (sensitivity) | WARP vs that twin, 1e-4 bar | 1.6e-3 |
| Slider 50 % (rho 1) | T3 | R != 1, all three |
| sigma(-u_v) - 0.001 (design R2.2 c) | T2 denominator >= 0.5 u_l u_v | min -8.97 u_l u_v (r 1e-8, u_l 4.7e-4, u_v 1e-4) |
