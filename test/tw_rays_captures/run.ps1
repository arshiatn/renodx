# The rays' captures that needed a new path in rt_core (tests.cpp): the sprite soldiers' imposter vertex shader of Warhammer
# (sm 4.0 and 4.1), Troy, Warhammer II and Pharaoh streamed out through its game's gs_main and through
# twattila/rt_capture_imposter_gs.h (the bytes the add-ons embed), and Warhammer's, Warhammer II's, Warhammer III's and Rome
# II's tessellated rigid objects and Rome II's tessellated terrain through their add-on's capture and rows shaders, on WARP.
# amd64 (the games are 64-bit; WARP runs Rome II's 32-bit add-on's shaders the same).
# -GameShaders: the folder holding dump_shader_<game>_pack\cso (default the decompiler's, only read). -Built: the folder holding
# <game>.include\embed (default this tree's build, after renodx-twwarhammer.addon64, renodx-twwarhammer2.addon64 and
# renodx-twwarhammer3.addon64); -Built32 the same for the 32-bit games (default this tree's build32, after
# renodx-twrome2.addon32). -OutDir:
# where the test exe is built and run (ATN's antivirus-excluded test folder, tools\tw\testbin). Holds tools/tw's GPU job lock
# while it runs when -Lock names it.
param([Parameter(Mandatory = $true)][string]$OutDir, [string]$GameShaders = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16', [string]$Built = '',
      [string]$Built32 = '', [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($Built -eq '') { $Built = Join-Path $repairRoot 'build' }
if ($Built32 -eq '') { $Built32 = Join-Path $repairRoot 'build32' }
$testRoot = Join-Path $repairRoot 'test\tw_rays_captures'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Set-Location -LiteralPath $OutDir
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:rays_captures.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Rays captures test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only while
# it is this run's.
$mine = "tw_rays_captures (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $OutDir 'rays_captures.exe') $GameShaders $Built $Built32 | Tee-Object -FilePath (Join-Path $OutDir 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Rays captures test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
