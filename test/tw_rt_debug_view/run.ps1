# twattila/rt_debug.cs_5_0.hlsl's Point Light Shadows view on the light mask's ratio and bit words (tests.cpp), on WARP: the
# shader compiled from this tree (-Tree another one, e.g. an older commit's export, with -Old for its CONTROL). Holds tools/tw's
# GPU job lock while it runs when -Lock names it.
param([string]$Lock = '', [string]$Tree = '', [switch]$Old)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_rt_debug_view'
if ($Tree -eq '') { $Tree = $repairRoot }
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_rt_debug_view'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:rt_debug_view.exe /link /OPT:REF d3d11.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'rt debug view test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only
# while it is this run's.
$mine = "tw_rt_debug_view (WARP) $PID $(Get-Date -Format s)"
$held = { try { [IO.File]::ReadAllText($Lock) -eq $mine } catch { $false } }
while ($Lock -ne '') {
  try {
    $stream = [IO.File]::Open($Lock, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $bytes = [Text.Encoding]::ASCII.GetBytes($mine)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Close()
    Start-Sleep -Seconds 3
    if (& $held) { break }
  } catch {
  }
  Start-Sleep -Seconds 30
}
try {
  if ($Old) { & (Join-Path $testBuild 'rt_debug_view.exe') $Tree old } else { & (Join-Path $testBuild 'rt_debug_view.exe') $Tree }
  if ($LASTEXITCODE -ne 0) { throw 'rt debug view test failed' }
} finally {
  if ($Lock -ne '' -and (& $held)) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
