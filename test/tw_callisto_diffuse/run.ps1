# Callisto diffuse gates (tests.cpp): src/games/twattila/callisto_diffuse.hlsli against its CPU twin and a double slide
# model, on WARP with fxc (cs_5_0, D3D11) and dxc (cs_6_5, D3D12; cs_6_9 static). amd64. No GPU job: WARP only.
# -Bin: the folder of dxcompiler.dll and dxil.dll (the repository's bin; a worktree without one: the main checkout's).
# -Out: the build folder (default tools\tw\testbin\tw_callisto_diffuse, the maintainer's excluded folder).
param([string]$Bin = '', [string]$Out = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_callisto_diffuse'
if ($Bin -eq '') { $Bin = Join-Path $repairRoot 'bin' }
if (-not (Test-Path -LiteralPath (Join-Path $Bin 'dxcompiler.dll'))) { throw "no dxcompiler.dll in $Bin (pass -Bin)" }
if ($Out -eq '') { $Out = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_callisto_diffuse' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Set-Location -LiteralPath $Out
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\tests.cpp" /Fe:callisto_diffuse.exe /link d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Callisto diffuse test compile failed' }
# last_run.txt in UTF-8 without a BOM (Windows PowerShell 5.1's Tee-Object writes UTF-16).
$lines = @(& (Join-Path $Out 'callisto_diffuse.exe') $repairRoot $Bin)
$exitCode = $LASTEXITCODE
[IO.File]::WriteAllLines((Join-Path $Out 'last_run.txt'), [string[]]$lines)
$lines
if ($exitCode -ne 0) { throw 'Callisto diffuse test failed' }
