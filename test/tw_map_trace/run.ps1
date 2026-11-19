# The map trace (twattila/frame_trace.hpp, RENODX_TW_MAP_TRACE; tests.cpp on WARP with ReShade's events mocked), then
# tools\tw\map_trace.py on the traces it wrote: the summary, and the join with waits.txt (the presents on another clock,
# one wait ending just before a map, one between events), the native calls' summary (v2: the native hook's maps by
# class and type, the longest native waits; v3's context hooks write the same lines) on tw_map_trace_native.txt, then
# tools\tw\map_trace_test.py (the join's synthetic cases, the native lines' parse). No game build needed. Run as x86
# (Attila, Rome II, Thrones) and amd64; links Detours (the context hooks).
# -OutDir: where the test exe goes (default testbin\tw_map_trace_<arch>, the maintainer's excluded folder).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repoRoot 'test\tw_map_trace'
$tools = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $tools "testbin\tw_map_trace_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$detoursArch = if ($Architecture -eq 'x86') { 'X86' } else { 'X64' }
$exe = "map_trace_$Architecture.exe"
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repoRoot /I "$repoRoot\external\reshade" /I "$repoRoot\external\frozen\include" /I "$repoRoot\external\gtl\include" /I "$repoRoot\external\Detours\include" "$testRoot\tests.cpp" /Fe:$exe /link /OPT:REF /OPT:ICF "$repoRoot\external\Detours\lib.$detoursArch\detours.lib" user32.lib advapi32.lib d3d11.lib dxgi.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Map trace test compile failed' }
& (Join-Path $testBuild $exe)
if ($LASTEXITCODE -ne 0) { throw 'Map trace test failed' }
$trace = Join-Path $testBuild 'tw_map_trace.txt'
$summary = & python -I (Join-Path $tools 'map_trace.py') summary $trace
if ($LASTEXITCODE -ne 0) { throw 'map_trace.py summary failed' }
$summary
$join = & python -I (Join-Path $tools 'map_trace.py') join $trace --waits (Join-Path $testBuild 'waits.txt')
if ($LASTEXITCODE -ne 0) { throw 'map_trace.py join failed' }
$join
# The parser's own checks: the resources and kinds, the alignment of the two clocks, each wait where the test put it;
# part 2's native maps (each followed by its v1 map: game) and frame 1's WARNING (3 v1 maps, two of them events only).
foreach ($expected in @('access discard 2, no_overwrite 1', 'readback', 'aligned: 5 of 5 presents', '1 of 1 waits, ',
    'IN map no_overwrite', 'between traced events', 'native hook: on, 6 native calls',
    'WARNING: frame 1: 1 native maps < 3 v1 maps', 'game: 3 of 3 native maps')) {
  if (-not (($summary + $join) -match [regex]::Escape($expected))) { throw "map_trace.py output lacks '$expected'" }
}
$native = & python -I (Join-Path $tools 'map_trace.py') summary (Join-Path $testBuild 'tw_map_trace_native.txt')
if ($LASTEXITCODE -ne 0) { throw 'map_trace.py summary (native) failed' }
$native
# The native calls: the hook on, the totals per class (the constant buffer's DISCARD with the slow driver's wait), the
# failed map, the longest native wait first.
foreach ($expected in @('native hook: on', 'constant: 1.00 maps/f', 'texture: 1.00 maps/f', 'other: 3.00 maps/f', 'hr 0x80070057',
    'discard 4, no_overwrite 1', 'longest native maps', 'buffer 256 B dynamic bind 0x4')) {
  if (-not (($native -join "`n") -match [regex]::Escape($expected))) { throw "map_trace.py native summary lacks '$expected'" }
}
'PASS: map_trace.py'
# The join's own cases (a wait before a map is not in it, an even cadence with a present bias, no presents) and the
# native lines' parse.
& python -I (Join-Path $tools 'map_trace_test.py')
if ($LASTEXITCODE -ne 0) { throw 'map_trace_test.py failed' }
