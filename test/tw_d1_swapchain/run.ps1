# Divinity: Original Sin EE's swap-chain recreation on WARP (tests.cpp): a flip chain left bound keeps its window and the
# next flip chain gets E_ACCESSDENIED (the start crash); which releases free the window. amd64 (the game is 64-bit).
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_d1_swapchain'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot 'build\tw_d1_swapchain_test_amd64'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:d1_swapchain.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 swap chain test compile failed' }
& (Join-Path $testBuild 'd1_swapchain.exe')
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 swap chain test failed' }
