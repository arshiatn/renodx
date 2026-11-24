# Directional Light Bounce's V1 (tests.cpp: shg_ design 8, items 1-6). WARP, amd64 only: the shaders are the same on
# x86, and item 1 reads the x86 builds' (build32) shaders too.
# -Before: a folder of copies of the add-ons' embed folders before the change (<Before>\<game>\*.cso, the x86 ones from
# build32). Reads build\ and build32\ of this repository. Holds tools/tw's GPU job lock while it runs when -Lock names it.
# -Items: which items (e.g. 24; all: 123456; t alone: the passes' time on the hardware GPU, under the GPU job lock).
param([string]$Before, [string]$Lock = '', [string]$Items = '123456')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_bounce_sh'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_bounce_sh'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\tests.cpp" /Fe:bounce_sh.exe /link d3d11.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Directional Light Bounce test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only
# while it is this run's.
$mine = "tw_bounce_sh (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $testBuild 'bounce_sh.exe') $repairRoot $Before $Items | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Directional Light Bounce test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
