# The helper's micromap bake cutoff against its ray queries' alpha test (X14; tests.cpp), on the CPU with NVIDIA's OMM SDK
# 1.12.0 (build\downloads\omm\extracted: omm.h, omm-lib.lib, omm-lib.dll; the helper's). amd64, no GPU.
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$omm = Join-Path $repairRoot 'build\downloads\omm\extracted'
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_omm_bake_cutoff'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
Copy-Item (Join-Path $omm 'bin\omm-lib.dll') $testBuild -Force
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT "-imsvc$omm\include" `
  (Join-Path $PSScriptRoot 'tests.cpp') /Fe:omm_bake_cutoff.exe /link (Join-Path $omm 'lib\omm-lib.lib')
if ($LASTEXITCODE -ne 0) { throw 'OMM bake cutoff test compile failed' }
& (Join-Path $testBuild 'omm_bake_cutoff.exe') $repairRoot | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'OMM bake cutoff test failed' }
