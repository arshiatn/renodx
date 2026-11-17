# The reference build's samplers (tests.cpp: rt_reference.hlsli's random numbers, disk, cosine directions, basis, visible
# normals with their G2 / G1 weight, strata) on WARP against their closed forms. amd64.
# -Bin: the folder of dxcompiler.dll and dxil.dll (the repository's bin; a worktree without one: the main checkout's).
# -Dump: a folder for each sampler's inputs and outputs (tools/tw/reference/xcheck reads them). Holds tools/tw's GPU job lock
# while it runs when -Lock names it.
param([string]$Bin = '', [string]$Dump = '', [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_rt_reference'
if ($Bin -eq '') { $Bin = Join-Path $repairRoot 'bin' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_rt_reference'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
if ($Dump -ne '') { New-Item -ItemType Directory -Force -Path $Dump | Out-Null }
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\tests.cpp" /Fe:rt_reference.exe /link d3d12.lib dxgi.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Reference sampler test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only
# while it is this run's.
$mine = "tw_rt_reference (WARP) $PID $(Get-Date -Format s)"
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
  $arguments = @($repairRoot, $Bin)
  if ($Dump -ne '') { $arguments += $Dump }
  & (Join-Path $testBuild 'rt_reference.exe') @arguments | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Reference sampler test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
