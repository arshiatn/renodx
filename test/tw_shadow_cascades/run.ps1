# Two Shadow Cascades Under Rays in Warhammer and Warhammer II (tests.cpp): src/games/twwarhammer2/shadow_cascades.hpp
# through engine_tweaks over the shipped executables (only read; the defaults are ATN's install folders). x64 (the games').
param([string]$Warhammer = 'C:\Program Files\Epic Games\TWWarhammer\warhammer.exe',
      [string]$Warhammer2 = 'C:\Program Files (x86)\Steam\steamapps\common\Total War WARHAMMER II\Warhammer2.exe')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_shadow_cascades'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot 'build\tw_shadow_cascades_test_amd64'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:shadow_cascades.exe /link /OPT:REF /OPT:ICF "$repairRoot\external\Detours\lib.X64\detours.lib" user32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Shadow cascades test compile failed' }
& (Join-Path $testBuild 'shadow_cascades.exe') $Warhammer $Warhammer2
if ($LASTEXITCODE -ne 0) { throw 'Shadow cascades test failed' }
