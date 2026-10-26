param([switch]$Execute, [switch]$LongCapture, [string]$ZigPath = $env:TORNE_ZIG)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$zigCompiler = Resolve-TorneZig $ZigPath
if ($LongCapture -and -not $Execute) { throw 'LongCapture requires Execute' }
$testExe = Join-Path $projectRoot '.tmp\native-torne-test.exe'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.tmp\zig-cache'
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot '.tmp') | Out-Null
if ($Execute -and (Get-Process -Name TVTest -ErrorAction SilentlyContinue)) { throw 'Close TVTest before live tests' }
if ($Execute) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'dist\BonDriver_Torne.dll'))) {
        throw 'Build bondriver/dist/BonDriver_Torne.dll before live tests.'
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot 'diagnostics') | Out-Null
}
& $zigCompiler c++ -target x86_64-windows-gnu -std=c++17 -O2 -municode (Join-Path $projectRoot 'tests\native_torne.cpp') -o $testExe -lwinusb -lsetupapi -lbcrypt -ladvapi32 -lole32 -Wall -Wextra -Werror
if ($LASTEXITCODE -ne 0) { throw 'Native test build failed' }
if ($Execute) {
    if ($LongCapture) { & $testExe $projectRoot --execute-long } else { & $testExe $projectRoot --execute }
} else { & $testExe $projectRoot }
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed' }
