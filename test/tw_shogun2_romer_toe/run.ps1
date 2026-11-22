# PsychoV35's toe against Vanilla below white in Shogun 2's and Rome Remastered's tone mappers on WARP (tests.cpp):
# numbers for ATN, checks that the ramps reach the curves, and the V35 fit of Shogun 2's defaults (~45k WARP draws:
# three Shogun 2 fits and Rome Remastered's, 1.5-3 min each). Build renodx-twshogun2completeedition.addon64 and
# renodx-twromeremastered.addon64 first (their csos: build\<game>.include\embed). -OutDir: where the test exe is built
# and run (ATN's antivirus-excluded test folder, tools\tw\testbin); else build\. Both games are 64-bit: amd64 only.
param([string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_shogun2_romer_toe'
$testBuild = if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build\tw_shogun2_romer_toe_test' }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations -Wno-macro-redefined /I $repairRoot "$testRoot\tests.cpp" /Fe:shogun2_romer_toe.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Shogun 2 / Rome Remastered toe test compile failed' }
& (Join-Path $testBuild 'shogun2_romer_toe.exe') (Join-Path $repairRoot 'build')
if ($LASTEXITCODE -ne 0) { throw 'Shogun 2 / Rome Remastered toe test failed' }
