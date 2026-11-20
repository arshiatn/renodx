# Threaded Draw Recording (twattila/draw_recording.hpp, tests.cpp): the pool, the types and the depth path on a synthetic loop.
# x86 only (the header is empty stubs on x64); `-Architecture amd64` only proves that the stubs compile. Runs in its own process;
# nothing outside it changes. Test programs only in the maintainer's excluded folder.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_draw_recording'
$external = Join-Path $repairRoot 'external'
if (-not (Get-ChildItem -LiteralPath (Join-Path $external 'reshade') -Force -ErrorAction SilentlyContinue)) { $external = 'D:\Dev\renodx\external' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_draw_recording_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$source = if ($Architecture -eq 'x86') { "$testRoot\tests.cpp" } else { Join-Path $testBuild 'stubs.cpp' }
if ($Architecture -eq 'amd64') {
  # The x64 stubs: the header must compile and its calls be empty.
  Set-Content -LiteralPath $source -Value ('#include "src/games/twattila/draw_recording.hpp"' + [Environment]::NewLine + 'int main() { twattila::draw_recording::Update({}, false); twattila::draw_recording::Uninstall(false); return twattila::draw_recording::SharedPage() == nullptr ? 0 : 1; }') -Encoding ascii
}
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /DTWATTILA_DRAW_RECORDING_CONTROLS -Wno-deprecated-declarations /I $repairRoot /I "$external\reshade" /I "$external\reshade\include" $source /Fe:draw_recording.exe /link /OPT:REF /OPT:ICF user32.lib
$exit = $LASTEXITCODE
if ($Architecture -eq 'amd64') { Remove-Item -LiteralPath $source -Force }
if ($exit -ne 0) { throw 'Threaded Draw Recording test compile failed' }
if ($Architecture -eq 'amd64') { & (Join-Path $testBuild 'draw_recording.exe'); if ($LASTEXITCODE -ne 0) { throw 'x64 stubs failed' }; return }
& (Join-Path $testBuild 'draw_recording.exe')
if ($LASTEXITCODE -ne 0) { throw 'Threaded Draw Recording test failed' }
