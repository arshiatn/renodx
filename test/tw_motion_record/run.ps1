$ErrorActionPreference='Stop'
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch x86 -HostArch amd64 -SkipAutomaticLocation
$build = Join-Path $PSScriptRoot '../../build/astra_motion_test'
New-Item -ItemType Directory -Force -Path $build | Out-Null
Set-Location -LiteralPath $PSScriptRoot
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=i686-pc-windows-msvc /nologo /std:c++20 /EHsc /MT f3-motion-test.cpp "/Fe:$build/test.exe" "/Fo:$build/test.obj" /link d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Compile failed' }
& "$build/test.exe"
if ($LASTEXITCODE -ne 0) { throw 'WARP regression failed' }
