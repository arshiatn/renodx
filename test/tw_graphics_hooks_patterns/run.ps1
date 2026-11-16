# twattila/graphics_hooks.hpp's code patterns (units, dead units, trees, grass, big battles) exactly once in each game's
# empire.retail.dll, read from its game folder (never loaded), and a CONTROL with a changed byte that finds none
# (tests.cpp). Attila and Rome II from Steam, Thrones of Britannia from C:\Games; a game not installed is left out (said).
# Needs build32\twattila.include (a renodx-twattila.addon32 build: rt_core.hpp's embedded shaders). x86 (the games').
param([string]$Attila = 'C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll',
      [string]$Thrones = 'C:\Games\Total War Saga Thrones of Britannia\empire.retail.dll',
      [string]$Rome2 = 'C:\Program Files (x86)\Steam\steamapps\common\Total War Rome II\empire.retail.dll')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_graphics_hooks_patterns'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch x86 -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot 'build\tw_graphics_hooks_patterns_test_x86'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=i686-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build32\twattila.include" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:graphics_hooks_patterns.exe /link /OPT:REF /OPT:ICF "$repairRoot\external\Detours\lib.X86\detours.lib" user32.lib advapi32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Graphics hooks patterns test compile failed' }
$games = @()
foreach ($game in @(@('attila', $Attila), @('thrones', $Thrones), @('rome2', $Rome2))) {
  if (Test-Path -LiteralPath $game[1]) { $games += $game } else { Write-Output "$($game[0]): not installed ($($game[1])), left out" }
}
& (Join-Path $testBuild 'graphics_hooks_patterns.exe') @games
if ($LASTEXITCODE -ne 0) { throw 'Graphics hooks patterns test failed' }
