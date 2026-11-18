# The map trace (twattila/frame_trace.hpp, RENODX_TW_MAP_TRACE; tests.cpp on WARP with ReShade's events mocked), then
# tools\tw\map_trace.py on the trace it wrote: the summary, and the join with waits.txt (the presents on another clock,
# one wait ending just before a map, one between events), then tools\tw\map_trace_test.py (the join's synthetic cases).
# No game build needed. Run as x86 (Attila, Rome II, Thrones) and amd64.
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
$exe = "map_trace_$Architecture.exe"
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repoRoot /I "$repoRoot\external\reshade" /I "$repoRoot\external\frozen\include" /I "$repoRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:$exe /link /OPT:REF /OPT:ICF user32.lib advapi32.lib d3d11.lib dxgi.lib dxguid.lib
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
# The parser's own checks: the resources and kinds, the alignment of the two clocks, each wait where the test put it.
foreach ($expected in @('access discard 2, no_overwrite 1', 'readback', 'aligned: 5 of 5 presents', 'wait fit (chain 0x179b980): 1 of 1',
    'IN map no_overwrite', 'between traced events')) {
  if (-not (($summary + $join) -match [regex]::Escape($expected))) { throw "map_trace.py output lacks '$expected'" }
}
'PASS: map_trace.py'
# The join's own cases (a wait before a map is not in it, an even cadence with a present bias, no presents).
& python -I (Join-Path $tools 'map_trace_test.py')
if ($LASTEXITCODE -ne 0) { throw 'map_trace_test.py failed' }
