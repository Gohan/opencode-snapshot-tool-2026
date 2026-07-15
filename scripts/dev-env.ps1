[CmdletBinding()]
param(
    [string]$QtRoot = $env:QT_ROOT
)

$ErrorActionPreference = 'Stop'

function Import-VsEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'MSVC was not found. Install Visual Studio 2022 Build Tools with Desktop development with C++.'
    }

    $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'A Visual Studio installation containing MSVC was not found.' }
    $devCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    $lines = & $env:ComSpec /s /c "`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

function Find-QtRoot {
    param([string]$Requested)
    if ($Requested) {
        $resolved = Resolve-Path -LiteralPath $Requested -ErrorAction SilentlyContinue
        if ($resolved -and (Test-Path -LiteralPath (Join-Path $resolved 'bin\qmake.exe'))) { return $resolved.Path }
        throw "QT_ROOT does not point to a Qt MSVC kit: $Requested"
    }

    $roots = @('C:\Qt', 'C:\Tools\Qt') | Where-Object { Test-Path -LiteralPath $_ }
    $kits = foreach ($root in $roots) {
        Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Directory -Filter 'msvc*_64' -ErrorAction SilentlyContinue } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\qmake.exe') }
    }
    $selected = $kits | Sort-Object { [version]$_.Parent.Name } -Descending | Select-Object -First 1
    if (-not $selected) { throw 'Qt 6 MSVC kit not found. Set QT_ROOT to its directory.' }
    return $selected.FullName
}

Import-VsEnvironment
$QtRoot = Find-QtRoot $QtRoot
$env:QT_ROOT = $QtRoot
$env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$QtRoot;$($env:CMAKE_PREFIX_PATH)" } else { $QtRoot }
$env:Path = "$(Join-Path $QtRoot 'bin');$env:Path"

foreach ($command in 'cmake', 'ninja', 'cl') {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "Required command not found: $command" }
}

Write-Host "Development environment ready (Qt: $QtRoot)"
