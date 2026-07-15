[CmdletBinding()]
param(
    [string]$Version = '0.1.2',
    [string]$QtRoot = $env:QT_ROOT
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'dev-env.ps1') -QtRoot $QtRoot

$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$distRoot = Join-Path $root 'dist'
$packageName = "OpenCode-Snapshot-Tool-v$Version-windows-x64"
$staging = [IO.Path]::GetFullPath((Join-Path $distRoot $packageName))
$archive = Join-Path $distRoot "$packageName.zip"
$executable = Join-Path $root 'build\release\opencode-snapshot-tool.exe'
$deployTool = Join-Path $env:QT_ROOT 'bin\windeployqt.exe'

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Release executable not found: $executable. Run scripts/build.ps1 -Preset release first."
}

$distPrefix = [IO.Path]::GetFullPath($distRoot).TrimEnd('\') + '\'
if (-not $staging.StartsWith($distPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace a staging directory outside dist: $staging"
}

if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Copy-Item -LiteralPath $executable -Destination $staging
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $staging
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $staging
Copy-Item -LiteralPath (Join-Path $root 'design.md') -Destination $staging

& $deployTool --release --compiler-runtime --qmldir (Join-Path $root 'apps\snapshot-tool\qml') (Join-Path $staging 'opencode-snapshot-tool.exe')
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
}
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $archive -CompressionLevel Optimal

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $archive
Write-Output "Package: $archive"
Write-Output "SHA256: $($hash.Hash.ToLowerInvariant())"
