# Shogun 2's rt_sun and upscale_inputs compute shaders on WARP (tests.cpp): rt_sun with the 2026-11-14 data run's
# lighting buffers, upscale_inputs with a DLSS input capture (the DLSS addon's "Capture DLSS inputs": its folder holds
# shogun2_dlss_capture_0_*.bin; only read). Holds tools/tw's GPU job lock while it runs when -Lock names it.
# -GameShaders: the folder of the game's dumped csos (0xFAAE535B, 0x03FB2979, 0xAC25DAB8, 0x8A00A1D4, 0xE3DA20D4 .vs_5_0.cso,
# 0x16ADFD60, 0x6B8954A6, 0x9B9EC116 .ps_5_0.cso: parts 5, 6, 8, 9, 10, 11), only read.
param([Parameter(Mandatory = $true)][string]$Capture, [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$Lock = '',
      [string]$GameShaders = '')
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
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later (else waited for
# again, left alone), removed only while it is this run's.
$mine = "tw_shogun2_shaders (WARP) $PID $(Get-Date -Format s)"
while ($Lock -ne '') {
  try {
    $file = [System.IO.File]::Open($Lock, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write)
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($mine)
    $file.Write($bytes, 0, $bytes.Length)
    $file.Close()
    Start-Sleep -Seconds 3
    if ((Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { break }
  } catch {}
  Start-Sleep -Seconds 30
}
try {
  # Also kept in last_run.txt (a failing run's lines survive a caller's pipeline).
  $arguments = @($repairRoot, $Capture)
  if ($GameShaders -ne '') { $arguments += $GameShaders }
  & (Join-Path $testBuild 'shogun2_shaders.exe') @arguments | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Shogun 2 shader test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
