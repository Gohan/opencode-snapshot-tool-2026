[CmdletBinding()]
param(
    [ValidateSet('dev', 'release')]
    [string]$Preset = 'dev',
    [switch]$Test,
    [switch]$WithoutTests,
    [switch]$Deploy,
    [string]$QtRoot = $env:QT_ROOT
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'dev-env.ps1') -QtRoot $QtRoot
$root = Split-Path $PSScriptRoot -Parent

Push-Location $root
try {
    $configureArguments = @('--preset', $Preset)
    if ($WithoutTests) { $configureArguments += '-DOST_BUILD_TESTS=OFF' }
    cmake @configureArguments
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    cmake --build --preset $Preset
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    if ($Deploy) {
        $deployTool = Join-Path $env:QT_ROOT 'bin\windeployqt.exe'
        $executable = Join-Path $root "build\$Preset\opencode-snapshot-tool.exe"
        $mode = if ($Preset -eq 'release') { '--release' } else { '--debug' }
        & $deployTool $mode --qmldir (Join-Path $root 'apps\snapshot-tool\qml') $executable
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
    if ($Test -and $Preset -eq 'dev') {
        ctest --test-dir (Join-Path $root 'build\dev') --output-on-failure
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
} finally {
    Pop-Location
}
