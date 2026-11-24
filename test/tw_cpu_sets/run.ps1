# Keep Render Core Free (twattila/cpu_sets.hpp, tests.cpp): the plan on synthetic CPUs, then live in the test's own process
# (its own process default CPU sets and threads; nothing outside it changes). Run as x86 (Attila's) and amd64.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86',
      [string]$BuildFolder = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_cpu_sets'
$external = Join-Path $repairRoot 'external'
if (-not (Get-ChildItem -LiteralPath (Join-Path $external 'reshade') -Force -ErrorAction SilentlyContinue)) { $external = 'D:\Dev\renodx\external' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = if ($BuildFolder -ne '') { $BuildFolder } else { Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_cpu_sets_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$external\reshade" /I "$external\reshade\include" "$testRoot\tests.cpp" /Fe:cpu_sets.exe /link /OPT:REF /OPT:ICF user32.lib
if ($LASTEXITCODE -ne 0) { throw 'Keep Render Core Free test compile failed' }
& (Join-Path $testBuild 'cpu_sets.exe')
if ($LASTEXITCODE -ne 0) { throw 'Keep Render Core Free test failed' }
