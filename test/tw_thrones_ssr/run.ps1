# The environment_specular_light replacements (twattila/shaders/ssr00-07: Shader Model 5, 4 / 4.1 and the game's MSAA;
# Thrones of Britannia's include them) against the game's shaders on WARP (tests.cpp): 100% the game's, 50% bit for bit 100%
# in the view's part, CONTROLS the game's at 50% and SSR Quality 2; ssr00 / ssr01 with Ray-Traced Specular Occlusion: one
# occlusion a reflection. -Game: twthronesofbritannia (default) or twattila (the same game bytes, each add-on's build and
# settings). -GameShaders: the folder of the game's dumped csos, only read; default the game's pack. -Built: the add-on's
# compiled shaders (default build32\<game>.include\embed after a renodx-<game>.addon32 build). -Older: an older build's
# compiled shaders, part F's CONTROL (else the game's). -OutDir: where the test exe is built and run (ATN's
# antivirus-excluded test folder, tools\tw\testbin); else build\. Holds tools/tw's GPU job lock while it runs when -Lock
# names it. x86 (the games are 32-bit; the shaders are the same either way).
param([string]$Game = 'twthronesofbritannia', [string]$GameShaders = '', [string]$Built = '', [string]$Older = '', [string]$OutDir = '',
      [string]$Lock = '')
# SSR Quality's and Effects at Output Resolution's floats in b13 (the game's shared.h).
$settings = if ($Game -eq 'twattila') { @(56, 84) } else { @(41, 67) }
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($GameShaders -eq '') { $GameShaders = "D:\Dev\renodx\bin\cmd_Decompiler-1.3.16\dump_shader_${Game}_pack\cso" }
if ($Built -eq '') { $Built = Join-Path $repairRoot "build32\$Game.include\embed" }
$testRoot = Join-Path $repairRoot 'test\tw_thrones_ssr'
$testBuild = Join-Path $(if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build' }) 'tw_thrones_ssr_test_x86'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch x86 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=i686-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:thrones_ssr.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Thrones SSR test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later (else waited for
# again, left alone), removed only while it is this run's.
$mine = "tw_thrones_ssr (WARP) $PID $(Get-Date -Format s)"
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
  $arguments = @($GameShaders, $Built, $settings[0], $settings[1])
  if ($Older -ne '') { $arguments += $Older }
  & (Join-Path $testBuild 'thrones_ssr.exe') @arguments | Tee-Object -FilePath (Join-Path $testBuild "last_run_$Game.txt")
  if ($LASTEXITCODE -ne 0) { throw 'Thrones SSR test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
