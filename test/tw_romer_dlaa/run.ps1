# Rome Remastered's DLAA host on WARP (tests.cpp) with the addon's own shaders (build\twromeremastered.include: build
# renodx-twromeremastered.addon64 first). The game is 64-bit: amd64 only.
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_romer_dlaa'
$testBuild = Join-Path $repairRoot 'build\tw_romer_dlaa_test'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build\twromeremastered.include" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:romer_dlaa.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Rome Remastered DLAA test compile failed' }
& (Join-Path $testBuild 'romer_dlaa.exe')
if ($LASTEXITCODE -ne 0) { throw 'Rome Remastered DLAA test failed' }
