param(
    [string]$Destination = '',
    [string]$Runtime = '',
    [switch]$SkipPublish,
    [switch]$Force,
    [string]$SettingsFrom = '',
    [string]$ProfileFile = '',
    [string]$DotnetRoot = '',
    [switch]$NoRuntime
)
$ErrorActionPreference = 'Stop'

$repo    = Split-Path $PSScriptRoot -Parent
$version = (Get-Content (Join-Path $repo 'VERSION') -Raw).Trim()
if (-not $Destination) { $Destination = Join-Path $repo "dist\u9gl-$version" }
$stage = Join-Path $Destination $(if ($NoRuntime) { 'noruntime\u9gl' } else { 'u9gl' })
$zip   = Join-Path $Destination $(if ($NoRuntime) { 'u9gl-noruntime.zip' } else { 'u9gl.zip' })

if ((Test-Path $zip) -and -not $Force) {
    Write-Error "$zip exists. Bump VERSION, or pass -Force to replace this release's archive."
}

$latin1 = [System.Text.Encoding]::GetEncoding(28591)
New-Item -ItemType Directory -Force $Destination | Out-Null
$releaseProfile = Join-Path $Destination 'u9gl.profile.ini'
$committedProfile = Join-Path $repo 'u9gl\u9gl.profile.ini'
if ($SettingsFrom) {
    Write-Error ("-SettingsFrom no longer packages the author's live settings: the release ships the committed " +
                 "u9gl\u9gl.profile.ini. To take settings into it: tools\make-profile.ps1 -SettingsFrom $SettingsFrom -Compare, " +
                 "then -Apply, review and commit. Nothing staged.")
}
if (-not $ProfileFile) {
    if (-not (Test-Path $committedProfile)) { Write-Error "no release profile: $committedProfile is missing. Nothing staged." }
    $ProfileFile = $committedProfile
}
& (Join-Path $PSScriptRoot 'make-profile.ps1') -Check $ProfileFile
if ($env:CI -ne 'true' -and $ProfileFile -eq $committedProfile) {
    $tests = Join-Path $repo 'launcher\U9GL.Core.Tests'
    & dotnet test $tests --filter 'FullyQualifiedName~NewPlayerProfileTests' --nologo -v q
    if ($LASTEXITCODE -ne 0) { Write-Error "u9gl\u9gl.profile.ini fails NewPlayerProfileTests (above) - fix the template, commit, package again. Nothing staged." }
}
Copy-Item $ProfileFile $releaseProfile -Force

$stageArgs = @{ Destination = $stage }
if ($Runtime)     { $stageArgs.Runtime = $Runtime }
if ($SkipPublish) { $stageArgs.SkipPublish = $true }
if ($DotnetRoot)  { $stageArgs.DotnetRoot = $DotnetRoot }
if ($NoRuntime)   { $stageArgs.NoRuntime = $true }
& (Join-Path $PSScriptRoot 'stage.ps1') @stageArgs

$sampler = Join-Path $stage 'u9sample.exe'
if (Test-Path $sampler) { Remove-Item $sampler -Force }

$symbols = Join-Path $Destination 'symbols'
New-Item -ItemType Directory -Force $symbols | Out-Null
foreach ($name in 'u9log.dll', 'dsound.dll') {
    $from = Join-Path $repo "u9gl\out\sym\$name"
    if (-not (Test-Path $from)) { Write-Error "$from is missing - run u9gl/build.sh. Not packaging." }
    $want = (Get-Item (Join-Path $stage $name)).VersionInfo.ProductVersion
    if ((Get-Item $from).VersionInfo.ProductVersion -ne $want) {
        Write-Error "$from is not the build that was staged ($want) - run u9gl/build.sh. Not packaging."
    }
    Copy-Item $from (Join-Path $symbols $name) -Force
}
Write-Host "symbols\: unstripped u9log.dll and dsound.dll, kept beside the zip (not in it)"

Copy-Item $releaseProfile (Join-Path $stage 'u9gl.profile.ini') -Force

$staged = @(Get-ChildItem $stage -Recurse -File)
Write-Host "zipping $($staged.Count) files..."
if (Test-Path $zip) { Remove-Item $zip -Force }

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)

$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $inZip = @{}
    foreach ($entry in $archive.Entries) {
        if ($entry.Name) { $inZip[$entry.FullName.Replace('/', '\')] = $entry.Length }
    }
    $missing = @()
    foreach ($file in $staged) {
        $name = 'u9gl\' + $file.FullName.Substring($stage.Length).TrimStart('\')
        if (-not $inZip.ContainsKey($name) -or $inZip[$name] -ne $file.Length) { $missing += $name }
    }
    if ($missing.Count) {
        Write-Error ("the zip does not match what was staged: {0} of {1} files are missing or a different size, first {2}" -f `
                     $missing.Count, $staged.Count, ($missing | Select-Object -First 3 | Join-String -Separator ', '))
    }
} finally {
    $archive.Dispose()
}

$size = (Get-Item $zip).Length
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Host ""
Write-Host "U9GL $version packaged:"
Write-Host ("   {0}" -f $zip)
Write-Host ("   {0:N0} bytes, {1} files, sha256 {2}" -f $size, $staged.Count, $hash)
Write-Host ""
Write-Host "It unzips as one u9gl\ folder. A player drops it beside u9.exe and runs"
Write-Host "u9gl\U9GL.exe, which installs the loader after its own checks."
