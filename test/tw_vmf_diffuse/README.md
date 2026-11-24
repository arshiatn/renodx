# tw_vmf_diffuse: vMF diffuse S1 gates

`src/games/twattila/vmf_diffuse.hlsli` (`VmfRoughness`, `VmfLobe`, `VmfLobeFast`) checked against its CPU twin
(`tests.cpp`, a float port of S0's `vmf_s0.cpp`), and the twin checked against S0's walk and double-precision model.
The header runs in `vmf_probe.hlsl` on WARP twice: fxc `cs_5_0 /O3` on D3D11 and dxc `cs_6_5 -O3` on D3D12. Design:
`designs/vmf_diffuse.md` (Rev 1) sections 2-4 and 9, `designs/vmf_s0/s0_report.md` sections 7-8.

- `vmf_beta_table.h`, `vmf_fast_form.h`: S0's generated tables, renamed. The numbers are byte-identical to S0's
  headers and to the copies in the `.hlsli` (checked when the files were made).
- `t0_points.h`: 2040 of S0's T0 points (walk and model; r 0.005 / 0.05 / 0.15 / 0.26 / 0.40, c 0.1 / 0.5 / 0.8,
  u >= 0.2), written by `python -I make_points.py <designs/vmf_s0/t0_points.csv> t0_points.h`.
- Run (WARP only, no GPU job): `powershell -File test\tw_vmf_diffuse\run.ps1 [-Bin <folder of dxcompiler.dll and
  dxil.dll>]`. Builds with clang-cl into `tools\tw\testbin\tw_vmf_diffuse` and exits 0 only when every gate passes
  and every control fails. Full log of the 2026-11-24 run: `results.txt`.

## Results (2026-11-24; twin / fxc WARP / dxc WARP)

| Gate | Bar | Result |
|---|---|---|
| erf (A-S 7.1.26) vs `std::erf`, [-6, 6] | <= 1e-6 (WARP 2e-6); erf(0) = 0, erf(+-inf) = +-1 exactly | 4.1e-7 (twin); 2.0e-7 on the probe's inputs, all three; ends exact |
| T0p twin vs S0's double model | relative <= 1e-4 | 1.0e-6 |
| T0w twin vs the walk, per (r, c) | rms <= 3 % | 0.23-2.83 % (worst r 0.26, c 0.1, as S0) |
| T2 reciprocity, u >= 0.1 | <= 1e-5 x max(R, 1) | 4.8e-7 / 4.6e-7 / 3.5e-7 |
| T2 R in [0, 4] | | [0, 4] |
| T2 bond albedo with the beta table, r 0.005-0.40, base 0.05-0.95, R as shipped | within 1 % | max 0.20 % (0.20 % without the fade) |
| T3 r 0, slider 0, rho 0 | exactly 1.0f | exact, all three (`VmfLobe`, `VmfLobeFast`, `VmfLobe(VmfRoughness)`) |
| T4 finite, R in [0, 4], 1 at n.v <= 0, 0 at n.l <= 0 | | all three, every record (incl. r 1e-12 / 1e-30, base 0 / 1.5, inconsistent l.v) |
| T4 sigma across the Eq. 4 / 5 and 5 / 6 seams | absolute <= 0.02 | 0.0067 (alpha 0.25), 0.0168 (alpha 0.9) |
| T4 WARP vs twin, every output | absolute <= 0.02 | fxc 9.5e-7, dxc 3.4e-5 |
| fast: `VmfLobeFast` vs `VmfLobe` at base 0.5 | weighted rms <= 3 %, max <= 10 % (u >= 0.2) | <= 1.18 % / 4.5 % |
| static | no warnings; no indexable temp / icb (fxc); no alloca / wave op / half (dxc) | clean; fxc 854 instruction slots for the probe (2 `VmfLobe` + `VmfLobeFast`) |

INFO: R's own jump across the seams (r x (1 -+ 1e-4)) is 0.064 at u_l = u_v = 0.1 forward (Eq. 4 / 5, base 0.05)
and 0.025 for u >= 0.2: the paper's branches are not continuous (Eq. 5 is its own loose fit, s0_report 1).

## Controls (each must fail; all did)

| Control | Gate it fails | Result |
|---|---|---|
| The preprint's series (no x2) | T0w, every cell r >= 0.05 | 4.2-38.6 % rms (passes only at r 0.005, 0.4 %: lobe too small) |
| | WARP vs that twin | 1.64 |
| The paper's Section 5 remap | T2 bond albedo | 28.6 % |
| | WARP vs that twin | 1.88 |
| beta 0 (c = base) | T2 bond albedo | 35.9 % |
| | WARP vs that twin | 1.35 |
| No fade | T4's 1 at n.v <= 0 | not 1 (R at sun 45 / view 85 behind, r 0.40: 1.47 unfaded, 1.41 faded) |
| | WARP vs that twin | 3.00 |
| Slider 50 % (rho 1) | T3 | R != 1, all three |
