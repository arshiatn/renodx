# Time Each Pass's phases (tests.cpp: twattila/pass_timing.hpp on WARP through tw_sampler_lifetime's ReShade mocks). Run as
# x86 (the TW games') and amd64. -OutDir: where the exe is built and run (it writes and deletes its pass timing file there).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_pass_timing'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir) { $OutDir } else { Join-Path $repairRoot "build\tw_pass_timing_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:pass_timing.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Pass timing test compile failed' }
& (Join-Path $testBuild 'pass_timing.exe')
if ($LASTEXITCODE -ne 0) { throw 'Pass timing test failed' }
