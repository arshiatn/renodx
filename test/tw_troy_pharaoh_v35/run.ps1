# PsychoV35's V35 FIT for Troy and Pharaoh on WARP (tests.cpp; the recipe: G04's Shogun 2 pilot). Build
# renodx-twtroy.addon64 and renodx-twpharaohdynasties.addon64 first (their csos: build\<game>.include\embed). -OutDir:
# where the test exe is built and run (ATN's antivirus-excluded test folder, tools\tw\testbin); else build\. Both games
# are 64-bit: amd64 only.
param([string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_troy_pharaoh_v35'
$testBuild = if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build\tw_troy_pharaoh_v35_test' }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations -Wno-macro-redefined /I $repairRoot "$testRoot\tests.cpp" /Fe:troy_pharaoh_v35.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Troy / Pharaoh V35 fit test compile failed' }
& (Join-Path $testBuild 'troy_pharaoh_v35.exe') (Join-Path $repairRoot 'build')
if ($LASTEXITCODE -ne 0) { throw 'Troy / Pharaoh V35 fit test failed' }
