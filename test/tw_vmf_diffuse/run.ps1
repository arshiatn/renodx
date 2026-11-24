# vMF diffuse S1 (tests.cpp): src/games/twattila/vmf_diffuse.hlsli against its CPU twin, S0's walk and model, on WARP
# with fxc (cs_5_0, D3D11) and dxc (cs_6_5, D3D12). amd64. No GPU job: WARP only.
# -Bin: the folder of dxcompiler.dll and dxil.dll (the repository's bin; a worktree without one: the main checkout's).
param([string]$Bin = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_vmf_diffuse'
if ($Bin -eq '') { $Bin = Join-Path $repairRoot 'bin' }
if (-not (Test-Path -LiteralPath (Join-Path $Bin 'dxcompiler.dll'))) { throw "no dxcompiler.dll in $Bin (pass -Bin)" }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_vmf_diffuse'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\tests.cpp" /Fe:vmf_diffuse.exe /link d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'vMF diffuse test compile failed' }
& (Join-Path $testBuild 'vmf_diffuse.exe') $repairRoot $Bin | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'vMF diffuse test failed' }
