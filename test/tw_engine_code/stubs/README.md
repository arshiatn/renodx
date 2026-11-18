# Stub sources

The byte arrays in `src/games/twattila/engine_code.hpp` (`JOB_STUB`, `HALF_ROWS_STUB`, `ChildSumStub(K)`, `Float4ReloadStub(at)` / `FLOAT4_RELOAD_STUB`, `Float4ReloadFarStub(at)`, `ParticlePrefetchStub(K)`, `EmitterPrefetchStub(K)`) are the `.text` of these
files, with the placeholders the header's `Prepare` replaces (`0x5A5A5Axx` sentinels, see `STUB_*_SENTINEL`). Rebuild one with

```
clang --target=i686-pc-windows-msvc -c half_rows.s -o half_rows.obj
llvm-objcopy --dump-section .text=half_rows.bin half_rows.obj
```

and compare the bytes with the array (the arrays carry the disassembly as comments; `half_rows.s` ends its code at the
`fallback:` jump, the 16-byte constants after `.balign 16` are `HALF_ROWS_CONSTANTS`).

- `job_wakeups.s` - Faster Job Wake-Ups (WakeAll, Sleep and the destructor's call of Stop).
- `half_rows.s` - the sprite row builder's 20 half floats four at a time (Faster Game CPU Code).
- `child_sum.s` - the effect tree's child sum prefetch of the children K - 1 and K / 2 - 1 after the next one (Faster Effect Prefetch); written for the K in
  `CHILD_SUM_AHEAD` (the add's immediate 4K and the near load's displacement 2K - 4 follow it).
- `float4_reload.s` - the particle writer's two float4 reloads, lane by lane (Faster Game CPU Code); written for its `[esp + 0x40]` (the four disp8
  bytes follow `at`).
- `float4_reload_far.s` - the same with 4-byte displacements: the soldier draw's matrix row reloads (Faster Game CPU Code); written for its
  `[esp + 0x90]` (the four disp32 follow `at`).
- `particle_prefetch.s` - the particle update loop's prefetch of the particle K places ahead (Faster Effect Prefetch); the file is written for the
  K in `PARTICLE_PREFETCH_AHEAD` (the two disp8 bytes in `lea esi, [ebx + K]` and `[esi + ebx*4 + 4K]` follow it).
- `emitter_prefetch.s` - the emitter update loop's prefetch of the emitter K places after the next one (Faster Effect Prefetch); written for the K in
  `EMITTER_PREFETCH_AHEAD` (the two disp8 bytes in `lea ecx, [esi + K]` and `[ecx + esi*8 + 8K]` follow it).
