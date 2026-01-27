Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Update HLAC LDA model C sources in src/ (PowerShell version)
#
# What it does:
# - Searches the current folder and all subfolders for a directory named:
#     - lda_model/
#   (and also lda_nodel/ as a common typo)
# - If found, copies *.c and *.h files from that directory into ./src/
#   overwriting existing files.
#
# Usage:
#   .\update_c_hlac_model.ps1
#
# Notes:
# - If your environment blocks scripts, run:
#     powershell -ExecutionPolicy Bypass -File .\update_c_hlac_model.ps1

$RootDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DestDir = Join-Path $RootDir 'src'

if (-not (Test-Path -LiteralPath $DestDir -PathType Container)) {
    throw "ERROR: destination folder not found: $DestDir"
}

function Get-ModelDir {
    param([string]$RootDir)

    $topLdaModel = Join-Path $RootDir 'lda_model'
    if (Test-Path -LiteralPath $topLdaModel -PathType Container) { return $topLdaModel }

    $topLdaNodel = Join-Path $RootDir 'lda_nodel'
    if (Test-Path -LiteralPath $topLdaNodel -PathType Container) { return $topLdaNodel }

    # Otherwise, pick the shortest path (closest to repo root).
    $candidates = Get-ChildItem -LiteralPath $RootDir -Recurse -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq 'lda_model' -or $_.Name -eq 'lda_nodel' }

    if (-not $candidates) { return $null }

    return $candidates |
        Sort-Object { $_.FullName.Length } |
        Select-Object -First 1 -ExpandProperty FullName
}

$modelDir = Get-ModelDir -RootDir $RootDir
if (-not $modelDir) {
    Write-Host "No lda_model/ (or lda_nodel/) directory found under: $RootDir"
    exit 0
}

$relModelDir = Resolve-Path -LiteralPath $modelDir | ForEach-Object {
    $_.Path.Substring($RootDir.Length).TrimStart('\','/')
}

Write-Host "Using model directory: $relModelDir"

$files = @()
$files += Get-ChildItem -LiteralPath $modelDir -File -Filter '*.c' -ErrorAction SilentlyContinue
$files += Get-ChildItem -LiteralPath $modelDir -File -Filter '*.h' -ErrorAction SilentlyContinue

if (-not $files -or $files.Count -eq 0) {
    Write-Host "No *.c/*.h files found in: $modelDir"
    exit 0
}

$copied = 0
foreach ($f in $files) {
    $dest = Join-Path $DestDir $f.Name
    Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
    Write-Host "  - updated: src/$($f.Name)"
    $copied++
}

Write-Host "Copied $copied file(s) into: $DestDir"
Write-Host "Done."
