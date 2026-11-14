# Attila's Grass Dump (tests.cpp): graphics_hooks.hpp's dump through the hooks into the game's own cell loop and DrawScrub
# (empire.retail.dll read from the game folder, never loaded as a DLL), three synthetic battles; the dumps are kept next to
# the exe as grass_synthetic_<a|b|c>.bin for tools/tw/grass_replay. Needs the Attila addon built in build32
# (graphics_hooks.hpp includes object_motion.hpp's embedded shaders: build32\twattila.include). x86 only (amd64: SKIP).
# -OutDir: where the exe and the dumps go (default build\tw_grass_dump_test_<arch>).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86',
      [string]$GameDll = 'C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll',
      [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_grass_dump'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_grass_dump_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$detoursArch = if ($Architecture -eq 'x86') { 'X86' } else { 'X64' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build32\twattila.include" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:grass_dump.exe /link /OPT:REF /OPT:ICF "$repairRoot\external\Detours\lib.$detoursArch\detours.lib" user32.lib advapi32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Grass dump test compile failed' }
& (Join-Path $testBuild 'grass_dump.exe') $GameDll
if ($LASTEXITCODE -ne 0) { throw 'Grass dump test failed' }
