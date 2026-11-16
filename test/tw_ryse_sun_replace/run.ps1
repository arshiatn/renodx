# Ryse's Rays Replace Shadow Maps on WARP (tests.cpp): the sun's shadow mask channels hold the rays' shadow alone, the rest
# the game's bytes. Needs the add-on built (the repository's build\rysesonofrome.include\embed\0x52C5E001-3.cso and
# rt_sun_keep.cso). -OutDir: where the test exe goes (default build\tw_ryse_sun_replace_test_<arch>).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_ryse_sun_replace'
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_ryse_sun_replace_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:ryse_sun_replace.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Ryse sun replace test compile failed' }
& (Join-Path $testBuild 'ryse_sun_replace.exe') $repairRoot | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'Ryse sun replace test failed' }
