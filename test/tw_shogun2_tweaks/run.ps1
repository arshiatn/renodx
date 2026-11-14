# Shogun 2's engine tweaks (tests.cpp): src/games/twshogun2completeedition_dlss/tweak_objects.hpp's Find on the game's
# empire.retail.x64.dll (-Dll, mapped as an image resource: only read) against the list its initializers give (-Expected:
# tools\shogun2\pkg_1114\s2_tweaks_1114.txt), and every engine value behind Shogun 2's engine value rows found. amd64 only.
# -OutDir: where the test exe is built and run (ATN's antivirus-excluded test folder, tools\tw\testbin).
param([Parameter(Mandatory = $true)][string]$Dll, [Parameter(Mandatory = $true)][string]$Expected,
      [Parameter(Mandatory = $true)][string]$OutDir)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_shogun2_tweaks'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Set-Location -LiteralPath $OutDir
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:shogun2_tweaks.exe /link /OPT:REF /OPT:ICF "$repairRoot\external\Detours\lib.X64\detours.lib" user32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Shogun 2 tweak test compile failed' }
& (Join-Path $OutDir 'shogun2_tweaks.exe') $Dll $Expected
if ($LASTEXITCODE -ne 0) { throw 'Shogun 2 tweak test failed' }
