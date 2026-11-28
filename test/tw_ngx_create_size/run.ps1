param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [switch]$Control)
# -Control builds with CONTROL_DLSS_LM_CREATE_AT_OPTIMAL (the size choice before X38): the run must FAIL.
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_ngx_create_size'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\tw_ngx_create_size_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$defines = @()
if ($Control) { $defines += '/DCONTROL_DLSS_LM_CREATE_AT_OPTIMAL' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 @defines /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\DLSS\include" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:ngx_create_size.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib version.lib
if ($LASTEXITCODE -ne 0) { throw 'NGX create size test compile failed' }
& (Join-Path $testBuild 'ngx_create_size.exe')
if ($LASTEXITCODE -ne 0) { throw 'NGX create size test failed' }
