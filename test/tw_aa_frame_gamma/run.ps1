# twattila aa.hlsli AaFrameGamma22 (tests.cpp): DLAA's and CMAA2's passes at the frame's gamma, their built csos against
# the builds before the change. WARP, amd64 (the csos are the same for x86). -Before / -After: embed folders, comma-
# separated, in pairs, e.g. -Before <copy of build32\twattila.include\embed before> -After build32\twattila.include\embed.
# Holds tools/tw's GPU job lock while it runs when -Lock names it.
param([string]$Before, [string]$After, [string]$Lock = '')
$befores = @($Before -split ',')
$afters = @($After -split ',')
$ErrorActionPreference = 'Stop'
if ($befores.Count -eq 0 -or $befores.Count -ne $afters.Count) { throw 'Give -Before and -After folders in pairs' }
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_aa_frame_gamma'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_aa_frame_gamma'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\tests.cpp" /Fe:aa_frame_gamma.exe /link d3d11.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'AA frame gamma test compile failed' }
$arguments = for ($i = 0; $i -lt $befores.Count; ++$i) { $befores[$i]; $afters[$i] }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only
# while it is this run's.
$mine = "tw_aa_frame_gamma (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $testBuild 'aa_frame_gamma.exe') @arguments
  if ($LASTEXITCODE -ne 0) { throw 'AA frame gamma test failed' }
} finally {
  if ($Lock -ne '' -and (Get-Content -LiteralPath $Lock -Raw -ErrorAction SilentlyContinue) -eq $mine) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
