# Troy's replaced shaders against the game's on WARP (tests.cpp): the battle fog's Fog Strength (battlefog00 / 01).
# -GameShaders: the folder of the game's dumped csos (0x018FED0C, 0xD9A3B04F .ps_5_0.cso), only read. -Built: the add-on's
# compiled shaders (build\twtroy.include\embed after a renodx-twtroy.addon64 build). Holds tools/tw's GPU job lock while it
# runs when -Lock names it. amd64 only (Troy is 64-bit).
param([string]$GameShaders = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16\dump_shader_twtroy_pack\cso', [string]$Built = '', [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($Built -eq '') { $Built = Join-Path $repairRoot 'build\twtroy.include\embed' }
$testRoot = Join-Path $repairRoot 'test\tw_troy_shaders'
$testBuild = Join-Path $repairRoot 'build\tw_troy_shaders_test_amd64'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:troy_shaders.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Troy shader test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later (else waited for
# again, left alone), removed only while it is this run's.
$mine = "tw_troy_shaders (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $testBuild 'troy_shaders.exe') $GameShaders $Built | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Troy shader test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
