# Faster Game CPU Code (twattila/engine_code.hpp) against the game's own code in empire.retail.dll (tests.cpp), read
# from the game folder, never loaded as a DLL. -Quick: 2^28 floats instead of all 2^32 in the exhaustive parts.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86',
      [string]$GameDll = 'C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll',
      [switch]$Quick)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_engine_code'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\tw_engine_code_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -fms-extensions -Wno-deprecated-declarations /I $repairRoot /I $testRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\Detours\src" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:engine_code.exe /link /OPT:REF /OPT:ICF user32.lib
if ($LASTEXITCODE -ne 0) { throw 'Engine code test compile failed' }
$arguments = @($GameDll)
if ($Quick) { $arguments += 'quick' }
& (Join-Path $testBuild 'engine_code.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'Engine code test failed' }
