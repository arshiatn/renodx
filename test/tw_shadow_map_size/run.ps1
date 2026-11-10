# Warhammer III's Shadow Map Size on WARP (tests.cpp). The addon is 64-bit only. Needs the addon built (its three shaders'
# .cso under build\twwarhammer3.include\embed) and the game's shader dump (bin\cmd_Decompiler-1.3.16\dump_shader_twwarhammer3_pack\cso,
# not in the repository): without the dump the test cannot compare against the game's bytes and fails.
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_shadow_map_size'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot 'build\tw_shadow_map_size_test_amd64'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' '--target=x86_64-pc-windows-msvc' /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot /I $testRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:shadow_map_size.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Shadow map size test compile failed' }
& (Join-Path $testBuild 'shadow_map_size.exe') (Join-Path $repairRoot 'build\twwarhammer3.include\embed') (Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_twwarhammer3_pack\cso')
if ($LASTEXITCODE -ne 0) { throw 'Shadow map size test failed' }
