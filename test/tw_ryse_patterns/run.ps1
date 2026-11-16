# Ryse's byte-pattern hooks against the game's decrypted image (patterns.py, offline, CPU). -Image: the DLSS add-on's
# debug dump Bin64\ryse_image.bin (only read; default: the Steam library's Ryse).
param([string]$Image = 'D:\SteamLibrary\steamapps\common\Ryse Son of Rome\Bin64\ryse_image.bin')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if (-not (Test-Path -LiteralPath $Image)) { throw "No decrypted image at $Image (the DLSS add-on's Advanced debug button saves it)" }
& python -I (Join-Path $PSScriptRoot 'patterns.py') $repairRoot $Image
if ($LASTEXITCODE -ne 0) { throw 'Ryse pattern test failed' }
