param([string]$ZigPath = $env:TORNE_ZIG)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$zigCompiler = Resolve-TorneZig $ZigPath
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.tmp\zig-cache'
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot '.tmp') | Out-Null
$testExe = Join-Path $projectRoot '.tmp\test-recovery.exe'
& $zigCompiler cc -target x86_64-windows-gnu -std=c11 -O2 -D_WIN64 (Join-Path $PSScriptRoot 'test-recovery.c') -o $testExe -Wall -Wextra -Werror
if ($LASTEXITCODE -ne 0) { throw 'Recovery test build failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Recovery tests failed' }
