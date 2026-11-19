# twattila/context_hooks.hpp's d3d11.dll body detours on WARP (tests.cpp; S1's gate: the 17 bodies, the table states, the
# seeds, the records, re-entry, other contexts and threads, removal). No game build needed. Run as x86 (Attila, Rome II,
# Thrones) and amd64. -OutDir: where the test exe goes (default testbin\tw_context_hooks_<arch>, the maintainer's
# excluded folder).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repoRoot 'test\tw_context_hooks'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_context_hooks_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$detoursArch = if ($Architecture -eq 'x86') { 'X86' } else { 'X64' }
$exe = "context_hooks_$Architecture.exe"
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repoRoot /I "$repoRoot\external\reshade" /I "$repoRoot\external\frozen\include" /I "$repoRoot\external\gtl\include" /I "$repoRoot\external\Detours\include" "$testRoot\tests.cpp" /Fe:$exe /link /OPT:REF /OPT:ICF "$repoRoot\external\Detours\lib.$detoursArch\detours.lib" user32.lib advapi32.lib d3d11.lib dxgi.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Context hooks test compile failed' }
& (Join-Path $testBuild $exe)
if ($LASTEXITCODE -ne 0) { throw 'Context hooks test failed' }
