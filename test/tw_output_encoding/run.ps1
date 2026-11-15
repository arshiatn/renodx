param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_output_encoding'
$external = Join-Path $repairRoot 'external'
if (-not (Get-ChildItem -LiteralPath (Join-Path $external 'reshade') -Force -ErrorAction SilentlyContinue)) { $external = 'D:\Dev\renodx\external' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_output_encoding_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot /I $testRoot /I "$external\reshade" /I "$external\reshade\include" /I "$external\frozen\include" /I "$external\Detours\include" /I "$external\gtl\include" "$testRoot\tests.cpp" /Fe:output_encoding.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Output encoding test compile failed' }
& (Join-Path $testBuild 'output_encoding.exe')
if ($LASTEXITCODE -ne 0) { throw 'Output encoding test failed' }
& (Join-Path $testBuild 'output_encoding.exe') control
if ($LASTEXITCODE -eq 0) { throw 'Control passed: the test does not see a missing "off" mark' }
'Control failed as it must.'
