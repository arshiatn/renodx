# P0.d's map replay (render_mt track A, S3): builds map_replay_<arch>.exe (the replay, map_replay.cpp) and
# map_replay_tests_<arch>.exe (tests.cpp: the readers, the replay against a CPU model, parity with its controls, the
# wait locator; WARP only), runs the tests, then the replay when -Replay has its arguments. No game build needed.
# Run as x86 (Attila) and amd64.
#   -OutDir: where the exes go (default testbin\tw_map_replay_<arch>, the maintainer's excluded folder).
#   -RingAdapter: a header that defines TW_MAP_REPLAY_RING_PLUGIN and tw_map_replay::RING_PLUGIN (S5's ring through
#     ring_seam.hpp), force-included into the replay: --plugin ring.
#   -Replay: the replay's arguments, e.g. -Replay '<W3m>\tw_map_trace.txt','--waits','<join_W3m.txt>','--nvidia',
#     '--frames','1-11','--runs','2','--gpu-ms','37.6'. -SkipTests: the replay only.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [string]$OutDir = '', [string]$RingAdapter = '',
  [string[]]$Replay = @(), [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repoRoot 'test\tw_map_replay'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_map_replay_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$flags = @("--target=$triple", '/nologo', '/std:c++20', '/EHsc', '/O2', '/MT', '/DNOMINMAX', '/DWIN32=1', '-Wno-deprecated-declarations', '/I', $repoRoot)
$libraries = @('/link', '/OPT:REF', '/OPT:ICF', 'user32.lib', 'd3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib')
$adapter = @()
$adapterLibraries = @()
if ($RingAdapter -ne '') {
  # The ring's own headers (ReShade, Detours and the utils' dependencies) and libraries.
  $detoursArch = if ($Architecture -eq 'x86') { 'X86' } else { 'X64' }
  $adapter = @('/bigobj', '/I', "$repoRoot\external\reshade", '/I', "$repoRoot\external\frozen\include", '/I', "$repoRoot\external\gtl\include",
    '/I', "$repoRoot\external\Detours\include", '/FI', (Resolve-Path -LiteralPath $RingAdapter).Path)
  $adapterLibraries = @("$repoRoot\external\Detours\lib.$detoursArch\detours.lib", 'advapi32.lib')
}
& 'C:\Program Files\LLVM\bin\clang-cl.exe' @flags @adapter "$testRoot\map_replay.cpp" "/Fe:map_replay_$Architecture.exe" @libraries @adapterLibraries
if ($LASTEXITCODE -ne 0) { throw 'Map replay compile failed' }
if (-not $SkipTests) {
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' @flags "$testRoot\tests.cpp" "/Fe:map_replay_tests_$Architecture.exe" @libraries
  if ($LASTEXITCODE -ne 0) { throw 'Map replay test compile failed' }
  & (Join-Path $testBuild "map_replay_tests_$Architecture.exe")
  if ($LASTEXITCODE -ne 0) { throw 'Map replay tests failed' }
}
if ($Replay.Count -gt 0) {
  & (Join-Path $testBuild "map_replay_$Architecture.exe") @Replay
  if ($LASTEXITCODE -eq 2) { throw 'Map replay stopped (STOP line above)' }
}
