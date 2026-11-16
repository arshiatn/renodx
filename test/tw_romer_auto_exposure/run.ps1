# Rome Remastered's auto exposure (Feral's 6A1D2178) against the add-on's replacement on WARP (tests.cpp): off the game's
# bytes' output to the bit, the walk as read from the assembly, Modded Auto Exposure's capped mean, each with a CONTROL.
# Build renodx-twromeremastered.addon64 first (its cso: build\twromeremastered.include\embed). -GameCso: the game's shader
# from the decompiler's pack (only read). -OutDir: where the test exe is built and run (ATN's antivirus-excluded test
# folder, tools\tw\testbin); else build\. The game is 64-bit: amd64 only.
param([string]$GameCso = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16\dump_shader_twromeremastered_pack\cso\0x6A1D2178.cs_5_0.cso',
      [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_romer_auto_exposure'
$testBuild = if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build\tw_romer_auto_exposure_test' }
$addonCso = Join-Path $repairRoot 'build\twromeremastered.include\embed\0x6A1D2178.cso'
if (-not (Test-Path -LiteralPath $addonCso)) { throw "Build renodx-twromeremastered.addon64 first: $addonCso missing" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot "$testRoot\tests.cpp" /Fe:romer_auto_exposure.exe /link /OPT:REF /OPT:ICF d3d11.lib
if ($LASTEXITCODE -ne 0) { throw 'Rome Remastered auto exposure test compile failed' }
& (Join-Path $testBuild 'romer_auto_exposure.exe') $GameCso $addonCso
if ($LASTEXITCODE -ne 0) { throw 'Rome Remastered auto exposure test failed' }
