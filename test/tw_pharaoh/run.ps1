# Pharaoh's shadow-map table and ray-traced captures against the game's own bytes (tests.cpp): Three Shadow Cascades Under Rays
# over Pharaoh.exe's shadow setup (read from the Steam folder, or -Exe), and on WARP the campaign trees'
# captures against the game's vertex shaders.
# -GameShaders: the folder of the game's dumped csos (0x16D14106, 0x81696642; not in the repository), only read.
# -Built: the add-on's compiled shaders (build\twpharaohdynasties.include\embed after a renodx-twpharaohdynasties.addon64
# build). -OutDir: where the test exe is built and run (default build\tw_pharaoh_test_<arch>; ATN's antivirus-excluded test
# folder, tools\tw\testbin, when given). Run as amd64 (the game's) and x86. Holds tools/tw's GPU job lock while it runs when
# -Lock names it.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64',
      [string]$GameShaders = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16\dump_shader_twpharaohdynasties_pack\cso', [string]$Built = '',
      [string]$Exe = 'C:\Program Files (x86)\Steam\steamapps\common\Total War PHARAOH DYNASTIES\Pharaoh.exe', [string]$OutDir = '',
      [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($Built -eq '') { $Built = Join-Path $repairRoot 'build\twpharaohdynasties.include\embed' }
$testRoot = Join-Path $repairRoot 'test\tw_pharaoh'
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_pharaoh_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$detoursArch = if ($Architecture -eq 'x86') { 'X86' } else { 'X64' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:pharaoh.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib "$repairRoot\external\Detours\lib.$detoursArch\detours.lib" user32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Pharaoh test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later (else waited for
# again, left alone), removed only while it is this run's.
$mine = "tw_pharaoh (WARP) $PID $(Get-Date -Format s)"
while ($Lock -ne '') {
  try {
    $file = [System.IO.File]::Open($Lock, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write)
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($mine)
    $file.Write($bytes, 0, $bytes.Length)
    $file.Close()
    Start-Sleep -Seconds 3
    if ((Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { break }
  } catch {}
  Start-Sleep -Seconds 30
}
try {
  # Also kept in last_run.txt (a failing run's lines survive a caller's pipeline).
  & (Join-Path $testBuild 'pharaoh.exe') $GameShaders $Built $Exe | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Pharaoh test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
