param(
    [string]$Check = '',
    [string]$SettingsFrom = '',
    [switch]$Compare,
    [switch]$Apply,
    [string[]]$Keys = @(),
    [string]$Out = ''
)
$ErrorActionPreference = 'Stop'

$repo    = Split-Path $PSScriptRoot -Parent
$version = (Get-Content (Join-Path $repo 'VERSION') -Raw).Trim()
$latin1  = [System.Text.Encoding]::GetEncoding(28591)
if ($Out) { Write-Error "-Out is gone: a release ships the committed u9gl\u9gl.profile.ini. Use -SettingsFrom <game folder> -Compare / -Apply to take settings into it." }
if (-not $Check -and -not $Compare -and -not $Apply) { Write-Error "pass -Check <file>, or -SettingsFrom <game folder> with -Compare or -Apply." }

function Read-IniPairs([string[]]$lines) {
    foreach ($line in $lines) {
        $t = $line.Trim()
        if (-not $t -or $t.StartsWith('#') -or $t.StartsWith(';')) { continue }
        $eq = $t.IndexOf('=')
        if ($eq -le 0) { continue }
        [pscustomobject]@{ Key = $t.Substring(0, $eq).Trim().ToLowerInvariant(); Value = $t.Substring($eq + 1).Trim() }
    }
}

$catalogText = [System.IO.File]::ReadAllText((Join-Path $repo 'launcher\U9GL.Core\Settings\SettingsCatalog.cs'))
$catalogKeys = [System.Collections.Generic.HashSet[string]]::new()
foreach ($pattern in 'U9gl\("([a-z0-9_]+)"\)', 'Diag\("([a-z0-9_]+)"', '\bFix\("[^"]+", "([a-z0-9_]+)"', 'Managed\("([a-z0-9_]+)"') {
    foreach ($m in [regex]::Matches($catalogText, $pattern)) { [void]$catalogKeys.Add($m.Groups[1].Value) }
}
foreach ($group in [regex]::Matches($catalogText, 'FixGroup\("[^"]+",\s*\[([^\]]+)\]')) {
    foreach ($m in [regex]::Matches($group.Groups[1].Value, '"([a-z0-9_]+)"')) { [void]$catalogKeys.Add($m.Groups[1].Value) }
}
$feedKey = if ($catalogText -match 'TelemetryFeedKey\s*=\s*"([a-z0-9_]+)"') { $Matches[1] } else { $null }
if (-not $feedKey -or $catalogText -notmatch 'Diag\(TelemetryFeedKey') { Write-Error "SettingsCatalog.cs no longer has Diag(TelemetryFeedKey) - this script's patterns are out of date." }
[void]$catalogKeys.Add($feedKey)
if ($catalogKeys.Count -lt 50) { Write-Error "found only $($catalogKeys.Count) keys in SettingsCatalog.cs - this script's patterns are out of date." }
$diagKeys = [System.Collections.Generic.HashSet[string]]::new()
foreach ($m in [regex]::Matches($catalogText, 'Diag\("([a-z0-9_]+)"')) { [void]$diagKeys.Add($m.Groups[1].Value) }
[void]$diagKeys.Add($feedKey)
function Test-DiagKey([string]$k) { return $diagKeys.Contains($k) -or $k -match '^(diag_|stream_)' }
$playerDefaultKeys = @('metrics_overlay')
function Test-HeldKey([string]$k) { return (Test-DiagKey $k) -or $playerDefaultKeys -contains $k }

if ($Check) {
    if (-not (Test-Path $Check)) { Write-Error "$Check not found." }
    $pairs = @(Read-IniPairs ([System.IO.File]::ReadAllText($Check, $latin1) -split "`r?`n"))
    $keys = @($pairs | ForEach-Object Key)
    $repeated = @($pairs | Group-Object Key | Where-Object Count -gt 1 | ForEach-Object Name)
    $diag = @($keys | Where-Object { (Test-DiagKey $_) })
    $player = @($keys | Where-Object { $playerDefaultKeys -contains $_ })
    $problems = @()
    if ($repeated.Count) { $problems += "repeats " + ($repeated -join ', ') }
    if ($diag.Count) { $problems += "carries diagnostics " + ($diag -join ', ') + " (a fresh install has them off)" }
    if ($player.Count) { $problems += "carries " + ($player -join ', ') + " (every player starts at the default: make it again)" }
    if ($problems.Count) { Write-Error ("$Check " + ($problems -join '; ') + ".") }
    Write-Host ("u9gl.profile.ini checked: {0} settings, no repeat, no diagnostic, no metrics overlay (completeness: NewPlayerProfileTests)" -f $pairs.Count)
    return
}

if (-not $SettingsFrom) { Write-Error "pass -SettingsFrom <game folder>: the folder whose launcher settings to compare with." }
$template = Join-Path $repo 'u9gl\u9gl.profile.ini'
$liveIni  = Join-Path $SettingsFrom 'u9gl\u9gl.ini'
$liveDiag = Join-Path $SettingsFrom 'u9gl\u9gl.diag.ini'
$marker   = '# ==== U9GL launcher settings'
if (-not (Test-Path $template)) { Write-Error "$template not found." }
if (-not (Test-Path $liveIni)) { Write-Error "$liveIni not found - pass -SettingsFrom <game folder>." }

$liveLines = [System.IO.File]::ReadAllText($liveIni, $latin1) -split "`r?`n"
$at = [array]::FindIndex($liveLines, [Predicate[string]]{ param($l) $l.TrimStart().StartsWith($marker) })
if ($at -lt 0) { Write-Error "$liveIni has no launcher block - open its U9GL.exe and Save." }
$live = @{}
foreach ($p in Read-IniPairs ($liveLines | Select-Object -Skip ($at + 1))) { $live[$p.Key] = $p.Value }
if (Test-Path $liveDiag) {
    foreach ($p in Read-IniPairs ([System.IO.File]::ReadAllText($liveDiag, $latin1) -split "`r?`n")) {
        if ($live.ContainsKey($p.Key) -and -not (Test-DiagKey $p.Key)) { $live[$p.Key] = $p.Value }
    }
}

$tLines = [System.IO.File]::ReadAllText($template, $latin1) -split "`r?`n"
$diffs = [ordered]@{}
$absentHere = @()
foreach ($p in Read-IniPairs $tLines) {
    if (-not $live.ContainsKey($p.Key)) { $absentHere += $p.Key; continue }
    if ($live[$p.Key] -ne $p.Value) { $diffs[$p.Key] = @($p.Value, $live[$p.Key]) }
}
if ($absentHere.Count) { Write-Host ("not in your launcher block (left as they are): " + ($absentHere -join ', ')) }
if (-not $diffs.Count) { Write-Host "the template and your settings agree on every key it holds."; return }
Write-Host ("{0} setting(s) where your settings differ from what a new player starts with:" -f $diffs.Count)
foreach ($k in $diffs.Keys) { Write-Host ("  {0}: new player {1}, yours {2}" -f $k, $diffs[$k][0], $diffs[$k][1]) }
if (-not $Apply) { Write-Host "nothing written (-Apply writes them, -Keys a,b only some)."; return }

$take = if ($Keys.Count) { @($Keys | ForEach-Object { $_.ToLowerInvariant() }) } else { @($diffs.Keys) }
$unknown = @($take | Where-Object { -not $diffs.Contains($_) })
if ($unknown.Count) { Write-Error ("-Keys names {0}, which the template does not hold at a different value." -f ($unknown -join ', ')) }
$outLines = foreach ($line in $tLines) {
    $t = $line.Trim()
    $eq = $t.IndexOf('=')
    if ($t -and -not $t.StartsWith('#') -and $eq -gt 0) {
        $k = $t.Substring(0, $eq).Trim().ToLowerInvariant()
        if ($take -contains $k) { "$k = $($diffs[$k][1])"; continue }
    }
    $line
}
$text = ($outLines -join "`r`n").TrimEnd("`r", "`n") + "`r`n"
[System.IO.File]::WriteAllText($template, $text, $latin1)
Write-Host ("written into {0}: {1}. Run the launcher tests, look at the diff, commit." -f $template, ($take -join ', '))
