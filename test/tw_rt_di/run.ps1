# Path-Traced Torch Light's RTXDI passes on WARP (tests.cpp, PT DI gate W4): the initial picks from the tiles' lists,
# their weights and uniformity, the spatial reuse's membership (CONTROL: the members off must fail), the shading. amd64.
# -Bin: the folder of dxcompiler.dll and dxil.dll (the repository's bin; a worktree without one: the main checkout's).
# -Rtxdi: RTXDI-Library's root (build\downloads' extracted b67f61cd). -Out: the test program's folder (testbin\tw_rt_di).
# Holds tools/tw's GPU job lock while it runs when -Lock names it.
param([string]$Bin = '', [string]$Rtxdi = '', [string]$Out = '', [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_rt_di'
if ($Bin -eq '') { $Bin = Join-Path $repairRoot 'bin' }
if ($Rtxdi -eq '') { $Rtxdi = Join-Path $repairRoot 'build\downloads\rtxdi\extracted\RTXDI-Library-b67f61cd' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
if ($Out -eq '') { $Out = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_rt_di' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Set-Location -LiteralPath $Out
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "/I$Rtxdi\Include" "$testRoot\tests.cpp" "$Rtxdi\Source\RtxdiUtils.cpp" /Fe:rt_di.exe /link d3d12.lib dxgi.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'DI test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only
# while it is this run's.
$mine = "tw_rt_di (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $Out 'rt_di.exe') $repairRoot $Bin "$Rtxdi\Include" | Tee-Object -FilePath (Join-Path $Out 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'DI test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
