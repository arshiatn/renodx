# The game's other builds of Attila's replaced shaders against the game's dumped bytes on WARP (tests.cpp): the lit particles
# (particle_vfx_main: vfx00 / vfx01 and every other build twattila/shaders replaces: rows off bit for bit with fog off and on,
# VFX Brightness affine, the rays' sun on smoke (t49) as every shadow test failing, Particle Resolution's half-size layer) and
# the battle fog (battlefog00 and its MSAA build battlefog01: rows at the game's bit for bit, Fog Strength, Fog & Sky
# Smoothing), the rays' captures of draws no trace has shown, and the particles' Shader Model 4 vertex shader against its 5's,
# with CONTROLS.
# -Game: twattila (default) or twthronesofbritannia (its wrappers of every vfx build, of both fog builds and of the captures;
# the same game bytes): every build and the captures must be there.
# -GameShaders: the game's dumped csos (default the game's pack), only read. -Built: the add-on's compiled shaders (default
# build32\<game>.include\embed after a renodx-<game>.addon32 build). -VfxBrightness / -FogStrength: their floats in b13
# (default the game's shared.h: Attila 14 / 88, Thrones 89 / 71). -OutDir: where the test exe is built and run (ATN's
# antivirus-excluded test folder, tools\tw\testbin); else build\. Holds tools/tw's GPU job lock while it runs when -Lock
# names it. x86.
param([string]$Game = 'twattila', [string]$GameShaders = '', [string]$Built = '', [int]$VfxBrightness = -1, [int]$FogStrength = -2,
      [string]$OutDir = '', [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$attila = ($Game -eq 'twattila')
if ($GameShaders -eq '') { $GameShaders = "D:\Dev\renodx\bin\cmd_Decompiler-1.3.16\dump_shader_${Game}_pack\cso" }
if ($Built -eq '') { $Built = Join-Path $repairRoot "build32\$Game.include\embed" }
if ($VfxBrightness -lt 0) { $VfxBrightness = if ($attila) { 14 } else { 89 } }
if ($FogStrength -eq -2) { $FogStrength = if ($attila) { 88 } else { 71 } }
$testRoot = Join-Path $repairRoot 'test\tw_attila_builds'
$testBuild = Join-Path $(if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build' }) 'tw_attila_builds_test_x86'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch x86 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=i686-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:attila_builds.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Attila builds test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later (else waited for
# again, left alone), removed only while it is this run's.
$mine = "tw_attila_builds (WARP) $PID $(Get-Date -Format s)"
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
  # Also kept in last_run_<game>.txt (a failing run's lines survive a caller's pipeline).
  & (Join-Path $testBuild 'attila_builds.exe') $GameShaders $Built $VfxBrightness $FogStrength all | Tee-Object -FilePath (Join-Path $testBuild "last_run_$Game.txt")
  if ($LASTEXITCODE -ne 0) { throw 'Attila builds test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
