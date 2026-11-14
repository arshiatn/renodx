# Total War: WARHAMMER's sun light shader 230F2241 (the game's bytes from a shader dump: its cso folder, only read), WH1's
# rt_sun and the SSR resolve replacement on WARP (tests.cpp). Holds tools/tw's GPU job lock while it runs when -Lock names it.
param([Parameter(Mandatory = $true)][string]$Dump, [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_warhammer_shaders'
$testBuild = Join-Path $repairRoot "build\tw_warhammer_shaders_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:warhammer_shaders.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'WH1 shader test compile failed' }
if ($Lock -ne '') {
  while (Test-Path -LiteralPath $Lock) { Start-Sleep -Seconds 30 }
  Set-Content -LiteralPath $Lock -Value "tw_warhammer_shaders (WARP) $(Get-Date -Format s)"
}
try {
  & (Join-Path $testBuild 'warhammer_shaders.exe') $repairRoot $Dump
  if ($LASTEXITCODE -ne 0) { throw 'WH1 shader test failed' }
} finally {
  if ($Lock -ne '') { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
