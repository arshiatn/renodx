# The 64-bit CPU page's start files (tests.cpp): settings.txt and heapfix.ini rows read, written and compared as their
# owners do, over sample files in a temp folder only. Run as amd64 (the port's) and x86 (the header builds there too).
# -OutDir: where the test exe is built and run (ATN's antivirus-excluded test folder, tools\tw\testbin); else build\.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_start_files'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $(if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build' }) "tw_start_files_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /W4 /I $repairRoot "$testRoot\tests.cpp" /Fe:start_files.exe /link kernel32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Start files test compile failed' }
& (Join-Path $testBuild 'start_files.exe')
if ($LASTEXITCODE -ne 0) { throw 'Start files test failed' }
