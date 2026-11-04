param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$Runtime = '',
    [switch]$SkipPublish,
    [string]$DotnetRoot = '',
    [switch]$NoRuntime
)
$ErrorActionPreference = 'Stop'

$repo    = Split-Path $PSScriptRoot -Parent
$version = (Get-Content (Join-Path $repo 'VERSION') -Raw).Trim()
$native  = Join-Path $repo 'u9gl\out'
$project = Join-Path $repo 'launcher\U9GL.Launcher\U9GL.Launcher.csproj'
$publish = Join-Path $repo $(if ($NoRuntime) { 'launcher\U9GL.Launcher\bin\publish-noruntime' } else { 'launcher\U9GL.Launcher\bin\publish' })
$dotnet  = if ($DotnetRoot) { $DotnetRoot }
           elseif ($env:DOTNET_ROOT -and (Test-Path (Join-Path $env:DOTNET_ROOT 'shared'))) { $env:DOTNET_ROOT }
           else { Join-Path $env:ProgramFiles 'dotnet' }
$major   = 10
$latin1  = [System.Text.Encoding]::GetEncoding(28591)

foreach ($name in 'u9log.dll', 'dsound.dll') {
    $path = Join-Path $native $name
    if (-not (Test-Path $path)) { Write-Error "$name is not in $native - run u9gl/build.sh first." }
    $info = (Get-Item $path).VersionInfo
    if ($info.ProductName -ne 'U9GL' -or $info.FileVersion -ne $version) {
        Write-Error "$name is version '$($info.FileVersion)', not $version - run u9gl/build.sh again."
    }
}
if (Get-Process -Name u9,u9-laa -ErrorAction SilentlyContinue) {
    Write-Error "u9.exe or u9-laa.exe is running - quit the game first. Nothing staged."
}

function Get-Versions([string]$dir) {
    if (Test-Path $dir) {
        Get-ChildItem $dir -Directory | Where-Object Name -match '^\d+\.\d+\.\d+$' | ForEach-Object { [version]$_.Name }
    }
}
if (-not $NoRuntime) {
    $core = @(Get-Versions (Join-Path $dotnet 'shared\Microsoft.NETCore.App'))
    $desk = @(Get-Versions (Join-Path $dotnet 'shared\Microsoft.WindowsDesktop.App'))
    $asp = @(Get-Versions (Join-Path $dotnet 'shared\Microsoft.AspNetCore.App'))
    if (-not $Runtime) {
        $Runtime = "$($core | Where-Object { $_.Major -eq $major -and $desk -contains $_ -and $asp -contains $_ } | Sort-Object | Select-Object -Last 1)"
    }
    if (-not $Runtime) { Write-Error "No .NET $major version with base, desktop and ASP.NET Core runtimes is installed in $dotnet." }
    $fxr = "$(Get-Versions (Join-Path $dotnet 'host\fxr') | Where-Object Major -eq $major | Sort-Object | Select-Object -Last 1)"
    $parts = "host\fxr\$fxr", "shared\Microsoft.NETCore.App\$Runtime", "shared\Microsoft.WindowsDesktop.App\$Runtime", "shared\Microsoft.AspNetCore.App\$Runtime"
    foreach ($part in $parts) {
        if (-not $fxr -or -not (Test-Path (Join-Path $dotnet $part))) { Write-Error "$part is not in $dotnet." }
    }
}

if (-not $SkipPublish) {
    if ($NoRuntime) {
        Write-Host "publishing the launcher (framework-dependent, the installed .NET)..."
        & dotnet publish $project -c Release -r win-x64 --self-contained false `
            -p:PublishSingleFile=true -o $publish --nologo -v q
    } else {
        Write-Host "publishing the launcher (framework-dependent, .NET from u9gl\runtime)..."
        & dotnet publish $project -c Release -r win-x64 --self-contained false `
            -p:PublishSingleFile=true -p:AppHostDotNetSearch=AppRelative -p:AppHostRelativeDotNet=runtime `
            -o $publish --nologo -v q
    }
    if ($LASTEXITCODE -ne 0) { Write-Error "dotnet publish failed." }
}
$exe = Join-Path $publish 'U9GL.exe'
if (-not (Test-Path $exe)) { Write-Error "U9GL.exe is not in $publish - run without -SkipPublish." }

$staged = @{}
function Copy-Checked([string]$from, [string]$name) {
    $to = Join-Path $Destination $name
    Copy-Item $from $to -Force
    $a = Get-FileHash $from -Algorithm SHA256
    $b = Get-FileHash $to   -Algorithm SHA256
    if ($a.Hash -ne $b.Hash) {
        Write-Error ("$name did not stage: $to is not the file that was copied to it. " +
                     "Source {0:N0} bytes {1}, destination {2:N0} bytes {3}. Quit the game " +
                     "and anything else holding it, then stage again." -f `
                     (Get-Item $from).Length, $a.Hash.Substring(0, 12),
                     (Get-Item $to).Length,   $b.Hash.Substring(0, 12))
    }
    $staged[$name] = $b.Hash
}

New-Item -ItemType Directory -Force $Destination, (Join-Path $Destination 'media') | Out-Null
Copy-Checked $exe                          'U9GL.exe'
$video = Join-Path $publish 'media\about.mp4'
if (-not (Test-Path $video)) { Write-Error "media\about.mp4 is not in $publish - run without -SkipPublish." }
Copy-Checked $video                        'media\about.mp4'
New-Item -ItemType Directory -Force (Join-Path $Destination 'licenses') | Out-Null
$qrNotice = Join-Path $publish 'licenses\QRCoder-LICENSE.txt'
if (-not (Test-Path $qrNotice)) { Write-Error "licenses\QRCoder-LICENSE.txt is not in $publish - run without -SkipPublish." }
Copy-Checked $qrNotice                     'licenses\QRCoder-LICENSE.txt'
Copy-Checked (Join-Path $native 'u9log.dll')  'u9log.dll'
Copy-Checked (Join-Path $native 'dsound.dll') 'dsound.dll'
if (Test-Path (Join-Path $native 'u9sample.exe')) { Copy-Checked (Join-Path $native 'u9sample.exe') 'u9sample.exe' }
Copy-Checked (Join-Path $repo 'CHANGELOG.md') 'CHANGELOG.md'

$rt = Join-Path $Destination 'runtime'
if (-not $NoRuntime) {
    foreach ($part in $parts) {
        & robocopy (Join-Path $dotnet $part) (Join-Path $rt $part) /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { Write-Error "copying $part failed (robocopy $LASTEXITCODE)." }
    }
    foreach ($notice in 'LICENSE.txt', 'ThirdPartyNotices.txt') {
        $path = Join-Path $dotnet $notice
        if (Test-Path $path) { Copy-Item $path (Join-Path $rt $notice) -Force }
    }
} elseif (Test-Path $rt) {
    Write-Warning "$rt is left from an earlier stage with the runtime; this U9GL.exe does not use it."
}
$global:LASTEXITCODE = 0

$defaults = "# u9gl.ini defaults $version`n" + [System.IO.File]::ReadAllText((Join-Path $repo 'u9gl\u9gl.ini'), $latin1)
[System.IO.File]::WriteAllText((Join-Path $Destination 'u9gl.defaults.ini'), $defaults, $latin1)

$dotnetNote = if ($NoRuntime) { @"
U9GL.exe needs Microsoft .NET $major on this PC: the .NET Desktop Runtime $major and the
ASP.NET Core Runtime $major, both from https://dotnet.microsoft.com/download/dotnet/$major.0.
If they are missing, U9GL.exe says so and offers the download page. The other U9GL zip
brings its own copy instead.
"@ } else { @"
The runtime folder is a private copy of Microsoft .NET $Runtime, used only by U9GL.exe.
Nothing is installed on your system for it.
"@ }
$readme = @"
U9GL $version for Ultima IX: Ascension (the GOG release)
=========================================================

This u9gl folder belongs inside the game folder, beside u9.exe:

    Ultima9\u9.exe
    Ultima9\u9gl\U9GL.exe   <- this folder

1. Run U9GL.exe. Its dashboard checks the game and offers Install.
2. Install puts one file, dsound.dll, beside u9.exe. Anything it replaces or edits is
   backed up in u9gl\backup\ first.
3. Start the game with Play, or the way you always do. Settings are in U9GL.exe.

To remove U9GL: Uninstall in the dashboard, then delete this folder.

$dotnetNote

Logs: %TEMP%\u9log.txt. Crash reports: %TEMP%\u9gl_diag\.
No game data is included; U9GL needs your own copy of the game.
"@ -replace "`r`n", "`r`n"
[System.IO.File]::WriteAllText((Join-Path $Destination 'README.txt'), $readme, [System.Text.Encoding]::UTF8)

foreach ($name in $staged.Keys) {
    $now = (Get-FileHash (Join-Path $Destination $name) -Algorithm SHA256).Hash
    if ($now -ne $staged[$name]) {
        Write-Error "$name changed after it was staged - something else is writing $Destination. Nothing here can be trusted; stage again."
    }
}

Write-Host "Staged U9GL $version in ${Destination}:"
Get-ChildItem $Destination -File | Where-Object { $_.Name -in 'U9GL.exe', 'u9log.dll', 'dsound.dll', 'u9gl.defaults.ini', 'CHANGELOG.md', 'README.txt' } | ForEach-Object {
    $v = $_.VersionInfo.ProductVersion
    $h = if ($staged.ContainsKey($_.Name)) { "sha " + $staged[$_.Name].Substring(0, 8) } else { '' }
    Write-Host ("   {0,-18} {1,12:N0} bytes  {2,-46} {3}" -f $_.Name, $_.Length, $(if ($v) { $v } else { '' }), $h)
}
$media = Get-Item (Join-Path $Destination 'media\about.mp4')
Write-Host ("   {0,-18} {1,12:N0} bytes  {2,-46} sha {3}" -f 'media\about.mp4', $media.Length, '', $staged['media\about.mp4'].Substring(0, 8))
if ($NoRuntime) {
    Write-Host "   (no runtime folder: U9GL.exe uses the installed .NET $major)"
} else {
    $files = @(Get-ChildItem $rt -Recurse -File)
    Write-Host ("   {0,-18} {1,12:N0} bytes  .NET {2}, hostfxr {3}, {4} files" -f 'runtime\', ($files | Measure-Object Length -Sum).Sum, $Runtime, $fxr, $files.Count)
    $aspFiles = @(Get-ChildItem (Join-Path $rt "shared\Microsoft.AspNetCore.App\$Runtime") -Recurse -File)
    Write-Host ("   ASP.NET Core adds {0:N0} bytes in {1} files (uncompressed)" -f ($aspFiles | Measure-Object Length -Sum).Sum, $aspFiles.Count)
}
