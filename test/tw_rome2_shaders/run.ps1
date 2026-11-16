# Rome II's replaced shaders on WARP (tests.cpp): the water's screen-space reflections (waterreflection00) below 100% and
# Reflection Quality, the HBAO resolve (ssao01) under the rays' AO, the build before the change (-Before: a git revision) against the working tree's, both compiled
# here with the build's flags. -BuildDir: where the test exe and the csos go (default build\tw_rome2_shaders_test_amd64).
# Holds tools/tw's GPU job lock while it runs when -Lock names it. amd64 (the shaders are the same bytes as the add-on's).
param([string]$Before = 'HEAD', [string]$BuildDir = '', [string]$Lock = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($BuildDir -eq '') { $BuildDir = Join-Path $repairRoot 'build\tw_rome2_shaders_test_amd64' }
$testRoot = Join-Path $repairRoot 'test\tw_rome2_shaders'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $BuildDir
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:rome2_shaders.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Rome II shader test compile failed' }
# The shader before (src from -Before) and after (the working tree), with the build's fxc flags.
$shaders = @(@{ Source = 'src\games\twrome2\shaders\waterreflection00_0x7C9C8ECF.ps_5_0.hlsl'; Name = '' },
             @{ Source = 'src\games\twrome2\shaders\ssao01_0x1ED22F9C.ps_5_0.hlsl'; Name = 'ambient_' })
$beforeRoot = Join-Path $BuildDir 'before'
if (Test-Path -LiteralPath $beforeRoot) { Remove-Item -LiteralPath $beforeRoot -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $beforeRoot | Out-Null
& git -C $repairRoot archive --format=tar -o (Join-Path $BuildDir 'before.tar') $Before src/games/twrome2 src/games/twattila src/shaders
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& tar.exe -xf (Join-Path $BuildDir 'before.tar') -C $beforeRoot
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }
$fxc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe'
foreach ($shader in $shaders) {
  foreach ($side in @(@{ Root = $beforeRoot; Out = "$($shader.Name)before.cso" }, @{ Root = $repairRoot; Out = "$($shader.Name)after.cso" })) {
    # Its warnings go to stderr: not errors here (the exit code is).
    $ErrorActionPreference = 'Continue'
    & $fxc -nologo -T ps_5_0 -E main -O3 -Gec -DNDEBUG -Fo (Join-Path $BuildDir $side.Out) (Join-Path $side.Root $shader.Source) 2>$null
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "fxc failed: $($side.Out)" }
  }
}
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later (else waited for
# again, left alone), removed only while it is this run's.
$mine = "tw_rome2_shaders (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $BuildDir 'rome2_shaders.exe') (Join-Path $BuildDir 'before.cso') (Join-Path $BuildDir 'after.cso') `
      (Join-Path $BuildDir 'ambient_before.cso') (Join-Path $BuildDir 'ambient_after.cso') | Tee-Object -FilePath (Join-Path $BuildDir 'last_run.txt')
  if ($LASTEXITCODE -ne 0) { throw 'Rome II shader test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
