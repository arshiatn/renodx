param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_helper_lifetime'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\astra_helper_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:helper_lifetime.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Helper lifetime regression compile failed' }
$helperPath = Join-Path $testBuild 'renodx-tw-dlaa64.exe'
if (Test-Path -LiteralPath $helperPath) { Remove-Item -LiteralPath $helperPath }
& (Join-Path $testBuild 'helper_lifetime.exe') --missing
if ($LASTEXITCODE -ne 0) { throw 'Missing-helper regression failed' }
Copy-Item -LiteralPath (Join-Path $testBuild 'helper_lifetime.exe') -Destination $helperPath
& (Join-Path $testBuild 'helper_lifetime.exe')
if ($LASTEXITCODE -ne 0) { throw 'Helper lifetime regression failed' }
