# Troy's engine tweaks (tests.cpp): src/games/twtroy/tweak_objects.hpp's Find on the game's decrypted image (a memory-layout
# copy of Troy.exe saved from the running game: -Image, only read) against the list its constructors give (-Expected:
# tools\tw\troy\par_t_troy_tweaks.txt), and every engine value behind Troy's Graphics 2 of 2 rows (more_tests.h, Attila's
# tests it names) found. amd64 only (Troy is 64-bit).
param([string]$Image = 'C:\Program Files\Epic Games\TotalWarSagaTROY\debug1\troy.exe.memory.bin',
      [Parameter(Mandatory = $true)][string]$Expected)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_troy_tweaks'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot 'build\tw_troy_tweaks_test_amd64'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:troy_tweaks.exe /link /OPT:REF /OPT:ICF "$repairRoot\external\Detours\lib.X64\detours.lib" user32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Troy tweak test compile failed' }
& (Join-Path $testBuild 'troy_tweaks.exe') $Image $Expected
if ($LASTEXITCODE -ne 0) { throw 'Troy tweak test failed' }
