# Shogun 2's rt_sun and upscale_inputs compute shaders on WARP (tests.cpp): rt_sun with the 2026-11-14 data run's
# lighting buffers, upscale_inputs with a DLSS input capture (the DLSS addon's "Capture DLSS inputs": its folder holds
# shogun2_dlss_capture_0_*.bin; only read). Holds tools/tw's GPU job lock while it runs when -Lock names it.
param([Parameter(Mandatory = $true)][string]$Capture, [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_shogun2_shaders'
$testBuild = Join-Path $repairRoot "build\tw_shogun2_shaders_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:shogun2_shaders.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Shogun 2 shader test compile failed' }
if ($Lock -ne '') {
  while (Test-Path -LiteralPath $Lock) { Start-Sleep -Seconds 30 }
  Set-Content -LiteralPath $Lock -Value "tw_shogun2_shaders (WARP) $(Get-Date -Format s)"
}
try {
  # Also kept in last_run.txt (a failing run's lines survive a caller's pipeline).
  & (Join-Path $testBuild 'shogun2_shaders.exe') $repairRoot $Capture | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Shogun 2 shader test failed' }
} finally {
  if ($Lock -ne '') { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
