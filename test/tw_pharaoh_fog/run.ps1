# Pharaoh's battle fog below 100 % on WARP (tests.cpp: the game's bytes against the add-on's 99164788 / 0CB1C821 /
# DB53BDDC). Build renodx-twpharaohdynasties.addon64 first (its csos: build\twpharaohdynasties.include\embed). -OutDir:
# where the test exe is built and run (ATN's antivirus-excluded test folder, tools\tw\testbin); else build\. amd64 only.
param([string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_pharaoh_fog'
$testBuild = if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build\tw_pharaoh_fog_test' }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX -Wno-deprecated-declarations "$testRoot\tests.cpp" /Fe:pharaoh_fog.exe /link d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Pharaoh fog test compile failed' }
& (Join-Path $testBuild 'pharaoh_fog.exe') (Join-Path $repairRoot 'build\twpharaohdynasties.include\embed')
if ($LASTEXITCODE -ne 0) { throw 'Pharaoh fog test failed' }
