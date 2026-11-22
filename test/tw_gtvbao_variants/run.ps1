# GT-VBAO's per-sample math variants (tests.cpp): Attila's AO pass at BITMASK_VARIANT 0 / 1 / 2 on WARP (-Hardware: the
# GPU, the 5090's same-picture gate), V0's bytes against -Base's bitmask.hlsli / bitmask_ssao.hlsli (git), PNGs into -Png.
param([string]$Base = 'atn-mods', [string]$Png = '', [switch]$Hardware)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_gtvbao_variants'
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_gtvbao_variants'
if ($Png -eq '') { $Png = Join-Path $testBuild 'png' }
$baseCopies = Join-Path $testBuild 'base'
New-Item -ItemType Directory -Force -Path $baseCopies | Out-Null
Set-Location -LiteralPath $repairRoot
foreach ($file in @('bitmask.hlsli', 'bitmask_ssao.hlsli')) {
  $text = (& git show "${Base}:src/games/twattila/$file") -join "`n"
  if ($LASTEXITCODE -ne 0) { throw "git show $Base $file failed" }
  [IO.File]::WriteAllText((Join-Path $baseCopies $file), $text + "`n")
}
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:PATH"
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "/I$repairRoot" "$testRoot\tests.cpp" /Fe:gtvbao_variants.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'GT-VBAO variants test compile failed' }
$arguments = @($repairRoot, $baseCopies, $Png)
if ($Hardware) { $arguments += 'hw' }
& (Join-Path $testBuild 'gtvbao_variants.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'GT-VBAO variants test failed' }
