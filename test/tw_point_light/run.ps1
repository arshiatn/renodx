# PT DI step 4a W1 (tests.cpp): src/games/twattila/tw_point_light.hlsli against Attila's 40 point-light shaders' own
# DXBC on WARP (fxc, D3D11) and on D3D12 WARP (dxc). amd64. No GPU job: WARP only.
# -Bin: the folder of dxcompiler.dll and dxil.dll and of cmd_Decompiler-1.3.16 (the repository's bin; a worktree
# without one: the main checkout's). -Cso: the dump's cso folder (default: Attila's under -Bin).
param([string]$Bin = '', [string]$Cso = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_point_light'
if ($Bin -eq '') { $Bin = Join-Path $repairRoot 'bin' }
if ($Cso -eq '') { $Cso = Join-Path $Bin 'cmd_Decompiler-1.3.16\dump_shader_twattila_pack\cso' }
if (-not (Test-Path -LiteralPath (Join-Path $Bin 'dxcompiler.dll'))) { throw "no dxcompiler.dll in $Bin (pass -Bin)" }
if (-not (Test-Path -LiteralPath (Join-Path $Cso '0x6426B042.ps_4_0.cso'))) { throw "no point-light DXBC in $Cso (pass -Cso)" }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_point_light'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\tests.cpp" /Fe:point_light.exe /link d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'point-light test compile failed' }
& (Join-Path $testBuild 'point_light.exe') $repairRoot $Cso $Bin | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'point-light test failed' }
