param(
    [string]$ZigPath = $env:TORNE_ZIG,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'),
    [switch]$Diagnostics
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$zigCompiler = Resolve-TorneZig $ZigPath
if ($Diagnostics -and -not $PSBoundParameters.ContainsKey('OutputDirectory')) { $OutputDirectory = Join-Path $PSScriptRoot 'dist\diagnostics' }
[string[]]$diagnosticFlags = @()
if ($Diagnostics) { $diagnosticFlags += '-DTORNE_DIAGNOSTICS' }
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$destination = Join-Path $OutputDirectory 'BonDriver_Torne.dll'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.tmp\zig-cache'
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot '.tmp') | Out-Null
$adapterObject = Join-Path $projectRoot '.tmp\BonDriver_Torne.o'
& $zigCompiler cc -target x86_64-windows-gnu -std=c11 -O2 @diagnosticFlags -c (Join-Path $PSScriptRoot 'BonDriver_Torne.c') -o $adapterObject -Wall -Wextra -Werror
if ($LASTEXITCODE -ne 0) { throw 'BonDriver adapter build failed' }
& $zigCompiler c++ -target x86_64-windows-gnu -std=c++17 -O2 -shared $adapterObject (Join-Path $PSScriptRoot 'TorneNative.cpp') -o $destination -lwinusb -lsetupapi -lbcrypt -ladvapi32 -lole32 -Wall -Wextra -Werror
if ($LASTEXITCODE -ne 0) { throw 'BonDriver build failed' }
$installedIni = Join-Path (Split-Path $destination -Parent) 'BonDriver_Torne.ini'
if (-not (Test-Path -LiteralPath $installedIni)) {
    # Keep the default dist directory relocatable; other destinations use an
    # absolute asset root, independent of the TVTest installation directory.
    $assetRoot = $projectRoot
    if ($OutputDirectory -eq (Join-Path $PSScriptRoot 'dist')) { $assetRoot = '..\..' }
    $ini = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'BonDriver_Torne.ini')
    $ini | ForEach-Object {
        if ($_ -like 'Root=*') { 'Root=' + $assetRoot } else { $_ }
    } | Set-Content -LiteralPath $installedIni -Encoding Unicode
}
Get-FileHash -LiteralPath $destination -Algorithm SHA256 | Format-List
