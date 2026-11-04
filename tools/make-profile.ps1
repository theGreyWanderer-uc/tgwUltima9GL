param(
    [string]$Out = '',
    [string]$Check = '',
    [string]$SettingsFrom = ''
)
$ErrorActionPreference = 'Stop'

$repo    = Split-Path $PSScriptRoot -Parent
$version = (Get-Content (Join-Path $repo 'VERSION') -Raw).Trim()
$latin1  = [System.Text.Encoding]::GetEncoding(28591)
if (-not $Out -and -not $Check) { Write-Error "pass -Out <file> to write a profile, or -Check <file> to check one." }

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

if ($Check) {
    if (-not (Test-Path $Check)) { Write-Error "$Check not found." }
    $pairs = @(Read-IniPairs ([System.IO.File]::ReadAllText($Check, $latin1) -split "`r?`n"))
    $keys = @($pairs | ForEach-Object Key)
    $repeated = @($pairs | Group-Object Key | Where-Object Count -gt 1 | ForEach-Object Name)
    $absent = @($catalogKeys | Where-Object { -not $diagKeys.Contains($_) -and $keys -notcontains $_ } | Sort-Object)
    $diag = @($keys | Where-Object { $diagKeys.Contains($_) })
    $problems = @()
    if ($repeated.Count) { $problems += "repeats " + ($repeated -join ', ') }
    if ($absent.Count) { $problems += "lacks " + ($absent -join ', ') + " (made before the launcher had them: make it again)" }
    if ($diag.Count) { $problems += "carries diagnostics " + ($diag -join ', ') + " (a fresh install has them off)" }
    if ($problems.Count) { Write-Error ("$Check " + ($problems -join '; ') + ".") }
    Write-Host ("u9gl.profile.ini checked: {0} settings, every key the launcher writes, no diagnostic" -f $pairs.Count)
    return
}

if (-not $SettingsFrom) { Write-Error "pass -SettingsFrom <game folder>: the folder whose launcher settings become the profile." }
$liveIni  = Join-Path $SettingsFrom 'u9gl\u9gl.ini'
$liveDiag = Join-Path $SettingsFrom 'u9gl\u9gl.diag.ini'
$marker   = '# ==== U9GL launcher settings'
if (-not (Test-Path $liveIni)) { Write-Error "$liveIni not found - pass -SettingsFrom <game folder>." }

$liveLines = [System.IO.File]::ReadAllText($liveIni, $latin1) -split "`r?`n"
$at = [array]::FindIndex($liveLines, [Predicate[string]]{ param($l) $l.TrimStart().StartsWith($marker) })
if ($at -lt 0) { Write-Error "$liveIni has no launcher block - open its U9GL.exe and Save." }
$blockFrom = if ($liveLines[$at] -match 'written by (U9GL \S+)') { "saved by " + $Matches[1] } else { 'its launcher block' }
$pairs = @(Read-IniPairs ($liveLines | Select-Object -Skip ($at + 1)))
$repeated = @($pairs | Group-Object Key | Where-Object Count -gt 1 | ForEach-Object Name)
if ($repeated.Count) { Write-Error ("the launcher block in $liveIni repeats {0} - remove the extra lines." -f ($repeated -join ', ')) }
$blockKeys = @($pairs | ForEach-Object Key)
$absent = @($catalogKeys | Where-Object { $blockKeys -notcontains $_ } | Sort-Object)
if ($absent.Count) {
    Write-Error ("the launcher block in $liveIni lacks {0} - it was written by an older launcher, and Save writes only changed settings. Add them to the block at the values its U9GL.exe shows (back the file up first), then try again." -f ($absent -join ', '))
}

$profileValues = [ordered]@{}
foreach ($p in $pairs) { $profileValues[$p.Key] = $p.Value }
if (Test-Path $liveDiag) {
    $left = @()
    foreach ($p in Read-IniPairs ([System.IO.File]::ReadAllText($liveDiag, $latin1) -split "`r?`n")) {
        if ($profileValues.Contains($p.Key) -and -not $diagKeys.Contains($p.Key)) { $profileValues[$p.Key] = $p.Value } else { $left += $p.Key }
    }
    if ($left.Count) { Write-Warning ("u9gl.diag.ini keys not carried (not in the launcher block): {0}" -f (($left | Sort-Object -Unique) -join ', ')) }
}
$heldBack = @($profileValues.Keys | Where-Object { $diagKeys.Contains($_) })
foreach ($k in $heldBack) { $profileValues.Remove($k) }
$authorOn = @($pairs | Where-Object { $diagKeys.Contains($_.Key) -and $_.Value -ne '0' } | ForEach-Object { "$($_.Key) = $($_.Value)" })
Write-Host ("diagnostics: {0} left out of the profile, so a fresh install has them off{1}" -f $heldBack.Count,
    $(if ($authorOn.Count) { "; on in the author's settings, not shipped: " + ($authorOn -join ', ') } else { '' }))
$extra = @($profileValues.Keys | Where-Object { -not $catalogKeys.Contains($_) })

$profileLines = @(
    "# u9gl.profile.ini $version - the settings this release installs with.",
    "# Install writes them once, over U9GL's defaults; after that the launcher's own settings",
    "# in u9gl.ini win, and an update keeps them. Taken from the author's launcher settings",
    "# ($blockFrom) on $(Get-Date -Format 'yyyy-MM-dd')."
)
foreach ($k in $profileValues.Keys) { $profileLines += "$k = $($profileValues[$k])" }
$dir = Split-Path -Parent $Out
if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
[System.IO.File]::WriteAllText($Out, (($profileLines -join "`r`n") + "`r`n"), $latin1)
$back = [ordered]@{}
foreach ($p in Read-IniPairs ([System.IO.File]::ReadAllText($Out, $latin1) -split "`r?`n")) { $back[$p.Key] = $p.Value }
if ($back.Count -ne $profileValues.Count -or @($profileValues.Keys | Where-Object { $back[$_] -ne $profileValues[$_] }).Count) {
    Write-Error "u9gl.profile.ini did not read back as written."
}
Write-Host ("u9gl.profile.ini: {0} settings from {1}{2}" -f $profileValues.Count, $liveIni,
            $(if ($extra.Count) { "; beyond the launcher's pages: " + (($extra | ForEach-Object { "$_ = $($profileValues[$_])" }) -join ', ') } else { '' }))
