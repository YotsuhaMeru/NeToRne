param([string]$ToolchainRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $projectRoot '.tmp/fr30/usr/local/fr30' }
$ToolchainRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$toolsBin = Join-Path $ToolchainRoot 'bin'
$compiler = Join-Path $ToolchainRoot 'lib/gcc-lib/fr30-elf/3.3.6/cc1.exe'
$output = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$savedPath = $env:PATH
try {
    $env:PATH = $toolsBin + ';' + $savedPath
    # Direct stages avoid the old Cygwin GCC driver's fork failure on Windows.
    & $compiler -quiet -std=c99 -Os -ffreestanding -fno-builtin -Wall -W -Werror (Join-Path $PSScriptRoot 'c/torne_core.c') -o (Join-Path $output 'torne-core-fr30.s')
    if ($LASTEXITCODE -ne 0) { throw 'FR30 compilation failed' }
    & (Join-Path $toolsBin 'fr30-elf-as.exe') -o (Join-Path $output 'torne-core-fr30.o') (Join-Path $output 'torne-core-fr30.s')
    if ($LASTEXITCODE -ne 0) { throw 'FR30 assembly failed' }
    & (Join-Path $toolsBin 'fr30-elf-ld.exe') -T (Join-Path $PSScriptRoot 'c/core.ld') -L (Join-Path $ToolchainRoot 'lib/gcc-lib/fr30-elf/3.3.6') -o (Join-Path $output 'torne-core-fr30.elf') (Join-Path $output 'torne-core-fr30.o') -lgcc
    if ($LASTEXITCODE -ne 0) { throw 'FR30 link failed' }
    $undefined = & (Join-Path $toolsBin 'fr30-elf-nm.exe') -u (Join-Path $output 'torne-core-fr30.elf')
    if ($LASTEXITCODE -ne 0 -or $undefined) { throw "Unresolved symbols: $undefined" }
    & (Join-Path $toolsBin 'fr30-elf-objcopy.exe') -O binary (Join-Path $output 'torne-core-fr30.elf') (Join-Path $output 'torne-core-fr30.bin')
    if ($LASTEXITCODE -ne 0) { throw 'FR30 binary extraction failed' }
    $disassembly = & (Join-Path $toolsBin 'fr30-elf-objdump.exe') -d (Join-Path $output 'torne-core-fr30.elf')
    if ($LASTEXITCODE -ne 0) { throw 'FR30 disassembly failed' }
    $disassembly | Set-Content -Encoding utf8 (Join-Path $output 'torne-core-fr30.disasm.txt')
    $elfBytes = [IO.File]::ReadAllBytes((Join-Path $output 'torne-core-fr30.elf'))
    if ($elfBytes[0] -ne 127 -or $elfBytes[1] -ne 69 -or $elfBytes[2] -ne 76 -or $elfBytes[3] -ne 70 -or $elfBytes[4] -ne 1 -or $elfBytes[5] -ne 2 -or $elfBytes[18] -ne 0 -or $elfBytes[19] -ne 84) { throw 'Expected big-endian ELF32 FR30 output' }
    $report = [ordered]@{
        language = 'C99'; architecture = 'FR30'; endian = 'big'; pointerBits = 32
        bytes = (Get-Item -LiteralPath (Join-Path $output 'torne-core-fr30.bin')).Length
        sha256 = (Get-FileHash -LiteralPath (Join-Path $output 'torne-core-fr30.bin')).Hash.ToLowerInvariant()
        sourceSha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'c/torne_core.c')).Hash.ToLowerInvariant()
        unresolvedSymbols = @(); loadAddress = $null
        deployableFirmware = $false; hardwareTested = $false; verifiedRawTS = $false
        usbAccessed = $false; spiWritten = $false
    }
    $report | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $output 'build-report.json')
    $report | ConvertTo-Json
} finally { $env:PATH = $savedPath }
