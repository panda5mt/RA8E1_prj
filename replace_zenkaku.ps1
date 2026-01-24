# powershell -ExecutionPolicy Bypass -File .\replace_zenkaku.ps1 -Root . -DryRun
# powershell -ExecutionPolicy Bypass -File .\replace_zenkaku.ps1 -Root . -Backup

param(
  [string]$Root = ".",
  [switch]$DryRun,
  [switch]$Backup
)

function Decode-Auto([byte[]]$bytes) {
  # BOM判定
  if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
    $enc = New-Object System.Text.UTF8Encoding($true, $true)
    return @{ Enc=$enc; BomLen=3; Text=$enc.GetString($bytes,3,$bytes.Length-3) }
  }
  if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
    $enc = New-Object System.Text.UnicodeEncoding($false, $true) # UTF-16LE
    return @{ Enc=$enc; BomLen=2; Text=$enc.GetString($bytes,2,$bytes.Length-2) }
  }
  if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
    $enc = New-Object System.Text.UnicodeEncoding($true, $true)  # UTF-16BE
    return @{ Enc=$enc; BomLen=2; Text=$enc.GetString($bytes,2,$bytes.Length-2) }
  }

  # NUL多めならバイナリとしてスキップ（安全）
  $sampleLen = [Math]::Min(4096, $bytes.Length)
  if ($sampleLen -gt 0) {
    for ($i=0; $i -lt $sampleLen; $i++) { if ($bytes[$i] -eq 0) { throw "Binary-like (NUL found)" } }
  }

  # BOMなし：まず厳密UTF-8（不正なら例外）→ ダメならCP932
  $utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
  try {
    $t = $utf8Strict.GetString($bytes)
    return @{ Enc=$utf8Strict; BomLen=0; Text=$t }
  } catch {
    $sjis = [System.Text.Encoding]::GetEncoding(932) # CP932
    return @{ Enc=$sjis; BomLen=0; Text=$sjis.GetString($bytes) }
  }
}

$rootPath = (Resolve-Path -LiteralPath $Root).Path
Write-Host "Root = $rootPath"

# 対象拡張子（必要に応じて増減OK）
$includeExt = @(
  '.m','.c','.h','.cpp','.hpp','.cs',
  '.txt','.md','.cmake',
  '.xml','.json','.yml','.yaml','.ini'
)

# 除外したいフォルダ（データ・画像が多いところ）
$excludeRegex = '\\\.git\\|\\matlab\\hlac_training_data\\'

$files = Get-ChildItem -LiteralPath $rootPath -Recurse -File -Force -ErrorAction SilentlyContinue |
  Where-Object {
    ($includeExt -contains $_.Extension.ToLower()) -and
    ($_.FullName -notmatch $excludeRegex)
  }


# 置換対象文字（Unicodeで固定）
$FW_LP = [char]0xFF08  # （
$FW_RP = [char]0xFF09  # ）
$IDE_COMMA = [char]0x3001 # 、
$IDE_DOT   = [char]0x3002 # 。
$HW_COMMA = [char]0xFF64  # ､
$HW_DOT   = [char]0xFF61  # ｡
$FW_COMMA = [char]0xFF0C  # ，
$FW_DOT   = [char]0xFF0E  # ．

$targets = @($FW_LP,$FW_RP,$IDE_COMMA,$IDE_DOT,$HW_COMMA,$HW_DOT)

$scanned=0; $changed=0; $skipped=0; $matlabSeen=0

foreach ($f in $files) {
  $scanned++
  if ($f.FullName -like "*\matlab\*") { $matlabSeen++ }

  try {
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
    $d = Decode-Auto $bytes
    $text = $d.Text

    # まず対象文字が無ければスキップ（高速化）
    if ($text.IndexOfAny($targets) -lt 0) { continue }

    $newText = $text.
      Replace($FW_LP, '(').
      Replace($FW_RP, ')').
      Replace($IDE_COMMA, $FW_COMMA).
      Replace($IDE_DOT,   $FW_DOT).
      Replace($HW_COMMA,  $FW_COMMA).
      Replace($HW_DOT,    $FW_DOT)

    if ($newText -ne $text) {
      $changed++
      if ($DryRun) {
        Write-Host "[CHANGE] $($f.FullName)"
        continue
      }

      if ($Backup) {
        Copy-Item -LiteralPath $f.FullName -Destination ($f.FullName + ".bak") -Force
      }

      $outBytes = @()
      $preamble = $d.Enc.GetPreamble()
      if ($preamble -and $preamble.Length -gt 0) { $outBytes += $preamble }
      $outBytes += $d.Enc.GetBytes($newText)

      [System.IO.File]::WriteAllBytes($f.FullName, [byte[]]$outBytes)
      Write-Host "[UPDATED] $($f.FullName)"
    }
  } catch {
    $skipped++
    Write-Warning "[SKIP] $($f.FullName) : $($_.Exception.Message)"
  }
}

Write-Host "Scanned=$scanned  Changed=$changed  Skipped=$skipped  MatlabFilesSeen=$matlabSeen"
