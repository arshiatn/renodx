# twattila/rt_core.hpp's scene_draw hosts' masked draws (tests.cpp): SceneCoverage with Host::leaf_cutoff and
# Host::mask_uv_scales, Tales' rt_vertices, on WARP with the real rt_core. Needs build\talesofarise.include (a
# renodx-talesofarise.addon64 build: rt_core.hpp's embedded shaders). -DumpFolder / -Table: Tales' dumped masked prepass
# shaders and the scanner's table (part 6). Holds tools/tw's GPU job lock while it runs when -Lock names it.
param([string]$Lock = '', [string]$DumpFolder = '', [string]$Table = '')
$Dumps = @()
if ($DumpFolder -ne '') { $Dumps = @($DumpFolder, $Table) }
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_scene_coverage'
$external = Join-Path $repairRoot 'external'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_scene_coverage'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build\talesofarise.include" /I "$external\reshade" /I "$external\frozen\include" /I "$external\Detours\include" /I "$external\gtl\include" "$testRoot\tests.cpp" /Fe:scene_coverage.exe /link /OPT:REF /OPT:ICF "$external\Detours\lib.X64\detours.lib" user32.lib advapi32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Scene coverage test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only
# while it is this run's.
$mine = "tw_scene_coverage (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $testBuild 'scene_coverage.exe') $repairRoot @Dumps | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Scene coverage test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
