# Thrones of Britannia's environment_specular_light replacements for Shader Model 4 / 4.1 (ssr02-05) against the game's
# shaders on WARP (tests.cpp): 100% bit for bit, 50% bit for bit 100% in the view's part, CONTROL the game's at 50%.
# -GameShaders: the folder of the game's dumped csos (0x485F33D7 / 0x0AC579F6 .ps_4_1.cso, 0x8E734BAA / 0xC8BD2B2F .ps_4_0.cso),
# only read. -Built: the add-on's compiled shaders (build32\twthronesofbritannia.include\embed after a
# renodx-twthronesofbritannia.addon32 build). Holds tools/tw's GPU job lock while it runs when -Lock names it. x86 (the game is
# 32-bit; the shaders are the same either way).
param([string]$GameShaders = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16\dump_shader_twthronesofbritannia_pack\cso', [string]$Built = '',
      [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($Built -eq '') { $Built = Join-Path $repairRoot 'build32\twthronesofbritannia.include\embed' }
$testRoot = Join-Path $repairRoot 'test\tw_thrones_ssr'
$testBuild = Join-Path $repairRoot 'build\tw_thrones_ssr_test_x86'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch x86 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=i686-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:thrones_ssr.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Thrones SSR test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later (else waited for
# again, left alone), removed only while it is this run's.
$mine = "tw_thrones_ssr (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $testBuild 'thrones_ssr.exe') $GameShaders $Built | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Thrones SSR test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
