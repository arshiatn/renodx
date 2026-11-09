param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_depth_levels'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\astra_depth_levels_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
# Compile the production header with only allocation call sites routed through
# failure-injection forwarding functions. No runtime patching or COM interception.
$source = Get-Content -Raw -LiteralPath (Join-Path $repairRoot 'src\games\twattila\depth_levels.hpp')
$source = $source.Replace('#include <embed/shaders.h>', '// Test supplies the real shader compiled at runtime.')
foreach ($method in @('CreateTexture2D', 'CreateShaderResourceView', 'CreateUnorderedAccessView')) {
  if (-not $source.Contains("device->$method(")) { throw "Missing production injection site: $method" }
  $source = $source.Replace("device->$method(", "Test$method(device.Get(), ")
}
Set-Content -LiteralPath (Join-Path $testBuild 'depth_levels_under_test.hpp') -Value $source
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $testBuild /I "$repairRoot\src\games\twattila" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:depth_recovery.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Depth-level regression compile failed' }
& (Join-Path $testBuild 'depth_recovery.exe') (Join-Path $repairRoot 'src\games\twattila\depth_levels.cs_5_0.hlsl')
if ($LASTEXITCODE -ne 0) { throw 'Depth-level regression failed' }
