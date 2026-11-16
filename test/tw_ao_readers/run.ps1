# twattila/ao_readers.hpp (tests.cpp): Pharaoh's building pools read the rays' AO at their t_ssao slot under Ray-Traced AO,
# the game's view back after each draw, the controls the game's; the pools' own bytes (the game's dump) sample t_ssao at
# the listed slots. WARP, amd64 (Pharaoh's). Needs build32\twattila.include (a renodx-twattila.addon32 build: rt_scene.hpp's
# embedded shaders). Holds tools/tw's GPU job lock while it runs when -Lock names it.
param([string]$Lock = '', [string]$Dumps = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_ao_readers'
$external = Join-Path $repairRoot 'external'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_ao_readers'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build32\twattila.include" /I "$external\reshade" /I "$external\frozen\include" /I "$external\Detours\include" /I "$external\gtl\include" "$testRoot\tests.cpp" /Fe:ao_readers.exe /link /OPT:REF /OPT:ICF "$external\Detours\lib.X64\detours.lib" user32.lib advapi32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'AO readers test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only
# while it is this run's.
$mine = "tw_ao_readers (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $testBuild 'ao_readers.exe') (Join-Path $Dumps 'dump_shader_twpharaohdynasties_pack\cso')
  if ($LASTEXITCODE -ne 0) { throw 'AO readers test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
