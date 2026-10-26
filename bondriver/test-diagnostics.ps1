param([string]$ZigPath = $env:TORNE_ZIG)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$zigCompiler = Resolve-TorneZig $ZigPath
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.tmp\zig-cache'
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot '.tmp') | Out-Null
$testExe = Join-Path $projectRoot '.tmp\test-diagnostics.exe'
& $zigCompiler c++ -target x86_64-windows-gnu -std=c++17 -O2 -municode (Join-Path $PSScriptRoot 'test-diagnostics.cpp') -o $testExe -lwinusb -lsetupapi -lbcrypt -ladvapi32 -lole32 -Wall -Wextra -Werror
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic test build failed' }
& $testExe $projectRoot
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic tests failed' }
$logTest = Join-Path $projectRoot '.tmp\test-diagnostic-log.exe'
& $zigCompiler cc -target x86_64-windows-gnu -std=c11 -O2 -municode (Join-Path $PSScriptRoot 'test-diagnostic-log.c') -o $logTest -Wall -Wextra -Werror
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic log test build failed' }
& $logTest $projectRoot
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic log tests failed' }
