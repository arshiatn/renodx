[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Apply', 'Restore')]
    [string]$Action,
    [string]$ConfigDirectory = (Join-Path $env:LOCALAPPDATA 'ExpeditionsRome/Saved/Config/WindowsNoEditor'),
    [string]$BackupDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Get-Process -Name 'ExpeditionsRome-Win64-Shipping' -ErrorAction SilentlyContinue) {
    throw 'Close Expeditions: Rome completely before applying or restoring the probe.'
}

if ($Action -eq 'Restore') {
    if (-not $BackupDirectory) { throw 'Restore requires the exact -BackupDirectory returned by Apply.' }
    $backup_root = (Resolve-Path -LiteralPath $BackupDirectory).ProviderPath
    $manifest = Get-Content -LiteralPath (Join-Path $backup_root 'manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.Version -ne 1 -or $manifest.Probe -ne 'Rome engine scale 33.333333') {
        throw 'This is not a supported Rome engine-scale probe backup.'
    }
    $source_path = (Resolve-Path -LiteralPath $manifest.SourcePath).ProviderPath
    if ($source_path -ne $manifest.SourcePath -or [IO.Path]::GetFileName($source_path) -ne 'Engine.ini') {
        throw 'The manifest source path is ambiguous or is not Engine.ini.'
    }
    $source_info = Get-Item -LiteralPath $source_path
    if ($source_info.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::ReadOnly)) {
        throw 'Refusing to restore a linked or read-only configuration file.'
    }
    if ((Get-FileHash -LiteralPath $source_path -Algorithm SHA256).Hash -ne $manifest.AppliedSha256) {
        throw 'Engine.ini changed since Apply. Nothing was restored; inspect the diff before removing the probe key.'
    }
    $original_path = Join-Path $backup_root 'Engine.ini.original'
    if ((Get-FileHash -LiteralPath $original_path -Algorithm SHA256).Hash -ne $manifest.OriginalSha256) {
        throw 'The backup checksum does not match its manifest. Nothing was restored.'
    }
    $restore_bytes = [IO.File]::ReadAllBytes($original_path)
    if (Get-Process -Name 'ExpeditionsRome-Win64-Shipping' -ErrorAction SilentlyContinue) {
        throw 'The game started during verification. Close it before restoring the probe.'
    }
    if ((Get-FileHash -LiteralPath $source_path -Algorithm SHA256).Hash -ne $manifest.AppliedSha256) {
        throw 'Engine.ini changed during restore verification. Nothing was restored; inspect the diff before removing the probe key.'
    }
    [IO.File]::WriteAllBytes($source_path, $restore_bytes)
    [IO.File]::SetAttributes($source_path, [IO.FileAttributes]$manifest.OriginalAttributes)
    [pscustomobject]@{ State = 'Restored'; EngineIni = $source_path; BackupDirectory = $backup_root }
    return
}

if ($BackupDirectory) { throw 'Apply creates a unique backup; do not supply -BackupDirectory.' }
$config_root = (Resolve-Path -LiteralPath $ConfigDirectory).ProviderPath
$source_path = Join-Path $config_root 'Engine.ini'
$source_info = Get-Item -LiteralPath $source_path
if ($source_info.PSIsContainer -or ($source_info.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::ReadOnly))) {
    throw 'Expected an existing, writable, non-linked Engine.ini file.'
}
$original_bytes = [IO.File]::ReadAllBytes($source_path)
$encoding = [Text.UTF8Encoding]::new($false, $true)
[byte[]]$preamble = @()
if ($original_bytes.Length -ge 4 -and $original_bytes[0] -eq 255 -and $original_bytes[1] -eq 254 -and $original_bytes[2] -eq 0 -and $original_bytes[3] -eq 0) {
    throw 'UTF-32 configuration encoding is not supported by this probe.'
} elseif ($original_bytes.Length -ge 3 -and $original_bytes[0] -eq 239 -and $original_bytes[1] -eq 187 -and $original_bytes[2] -eq 191) {
    $preamble = @(239, 187, 191)
} elseif ($original_bytes.Length -ge 2 -and $original_bytes[0] -eq 255 -and $original_bytes[1] -eq 254) {
    $encoding = [Text.UnicodeEncoding]::new($false, $false, $true)
    $preamble = @(255, 254)
} elseif ($original_bytes.Length -ge 2 -and $original_bytes[0] -eq 254 -and $original_bytes[1] -eq 255) {
    $encoding = [Text.UnicodeEncoding]::new($true, $false, $true)
    $preamble = @(254, 255)
}
$original_text = $encoding.GetString($original_bytes, $preamble.Length, $original_bytes.Length - $preamble.Length)
if ([regex]::IsMatch($original_text, '(?im)^[ \t]*r\.ScreenPercentage[ \t]*=')) {
    throw 'An r.ScreenPercentage override already exists. Refusing reapply or overwrite; restore the previous probe first.'
}
if ($original_text.Contains("`r") -and -not $original_text.Contains("`n")) {
    throw 'CR-only configuration newlines are not supported by this probe.'
}
$newline = $(if ($original_text.Contains("`r`n")) { "`r`n" } else { "`n" })
$sections = [regex]::Matches($original_text, '(?im)^[ \t]*\[SystemSettings\][ \t]*(?:;[^\r\n]*)?(?:\r?\n|$)')
if ($sections.Count -gt 1) { throw 'Multiple SystemSettings sections are ambiguous. Nothing was changed.' }
if ($sections.Count -eq 1) {
    $insert_at = $sections[0].Index + $sections[0].Length
    $before = $original_text.Substring(0, $insert_at)
    if (-not $before.EndsWith("`n")) { $before += $newline }
    $updated_text = $before + 'r.ScreenPercentage=33.333333' + $newline + $original_text.Substring($insert_at)
} else {
    $updated_text = $original_text
    if ($updated_text.Length -gt 0 -and -not $updated_text.EndsWith("`n")) { $updated_text += $newline }
    $updated_text += $newline + '[SystemSettings]' + $newline + 'r.ScreenPercentage=33.333333' + $newline
}
[byte[]]$updated_bytes = $preamble + $encoding.GetBytes($updated_text)
$backup_root = Join-Path ([IO.Path]::GetTempPath()) ('RenoDX_Rome_EngineScaleProbe_' + [DateTime]::Now.ToString('yyyyMMdd_HHmmss') + '_' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $backup_root
$original_path = Join-Path $backup_root 'Engine.ini.original'
[IO.File]::WriteAllBytes($original_path, $original_bytes)
$sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $applied_hash = [BitConverter]::ToString($sha256.ComputeHash($updated_bytes)).Replace('-', '')
} finally {
    $sha256.Dispose()
}
$manifest = [ordered]@{
    Version = 1
    Probe = 'Rome engine scale 33.333333'
    SourcePath = $source_path
    OriginalAttributes = [int]$source_info.Attributes
    OriginalSha256 = (Get-FileHash -LiteralPath $original_path -Algorithm SHA256).Hash
    AppliedSha256 = $applied_hash
}
[IO.File]::WriteAllText((Join-Path $backup_root 'manifest.json'), ($manifest | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
if (Get-Process -Name 'ExpeditionsRome-Win64-Shipping' -ErrorAction SilentlyContinue) {
    throw "The game started during verification. No probe was applied; backup retained at $backup_root."
}
if ((Get-FileHash -LiteralPath $source_path -Algorithm SHA256).Hash -ne $manifest.OriginalSha256) {
    throw "Engine.ini changed during verification. No probe was applied; backup retained at $backup_root."
}
try {
    [IO.File]::WriteAllBytes($source_path, $updated_bytes)
} catch {
    throw "Could not apply the probe. Original bytes are preserved at $original_path. $($_.Exception.Message)"
}
[pscustomobject]@{ State = 'Applied'; EngineIni = $source_path; BackupDirectory = $backup_root; RequiresRestart = $true }
