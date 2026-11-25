# U6 part 1 gates (tests.cpp): the fog local stream's reservoir math (dlaa_helper/rt_fog_reservoir.hlsli) on D3D12 WARP
# with dxc (cs_6_5; cs_6_9 statically), against a quadrature and a closed form. amd64. WARP under tools\tw\gpu_job.lock.
# -Bin: the folder of dxcompiler.dll and dxil.dll (the repository's bin; a worktree without one: the main checkout's).
# -Out: where the test program is built (outside the repository).
param([string]$Bin = '', [string]$Out = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_fog_restir'
if ($Bin -eq '') { $Bin = Join-Path $repairRoot 'bin' }
if (-not (Test-Path -LiteralPath (Join-Path $Bin 'dxcompiler.dll'))) { throw "no dxcompiler.dll in $Bin (pass -Bin)" }
$tw = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw'
if ($Out -eq '') { $Out = Join-Path $tw 'testbin\tw_fog_restir' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Set-Location -LiteralPath $Out
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\tests.cpp" /Fe:tw_fog_restir.exe /link d3d12.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'tw_fog_restir compile failed' }
$record = & python -I (Join-Path $tw 'lab\gpu_lock.py') acquire --pid $PID --script $PSCommandPath --note 'tw_fog_restir WARP'
if ($LASTEXITCODE -ne 0) { throw 'gpu_job.lock not taken' }
try {
  # Tee-Object -FilePath writes UTF-16 in Windows PowerShell 5.1: the log is kept, then written as UTF-8.
  & (Join-Path $Out 'tw_fog_restir.exe') $repairRoot $Bin | Tee-Object -Variable log
  $result = $LASTEXITCODE
  $log | Out-File -Encoding utf8 -FilePath (Join-Path $Out 'last_run.txt')
} finally {
  & python -I (Join-Path $tw 'lab\gpu_lock.py') release --record "$record" | Out-Null
}
if ($result -ne 0) { throw 'tw_fog_restir failed' }
