param([string]$Compiler = $env:TORNE_ZIG)
$ErrorActionPreference = 'Stop'
if (-not $Compiler) {
    $Compiler = 'zig.exe'
}
$output = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$arguments = @('-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', (Join-Path $PSScriptRoot 'c/torne_core.c'), (Join-Path $PSScriptRoot 'c/test_core.c'), '-o', (Join-Path $output 'test-core.exe'))
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { & $Compiler cc @arguments }
else { & $Compiler @arguments }
if ($LASTEXITCODE -ne 0) { throw 'C core test build failed' }
& (Join-Path $output 'test-core.exe')
if ($LASTEXITCODE -ne 0) { throw 'C core tests failed' }
$rawArguments = @('-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', (Join-Path $PSScriptRoot 'c/raw_packet.c'), (Join-Path $PSScriptRoot 'c/test_raw_packet.c'), '-o', (Join-Path $output 'test-raw-packet.exe'))
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { & $Compiler cc @rawArguments }
else { & $Compiler @rawArguments }
if ($LASTEXITCODE -ne 0) { throw 'Raw packet test build failed' }
& (Join-Path $output 'test-raw-packet.exe')
if ($LASTEXITCODE -ne 0) { throw 'Raw packet tests failed' }
$outputArguments = @('-std=c99', '-O2', '-DTR_OUTPUT_TEST', '-Wall', '-Wextra', '-Werror', (Join-Path $PSScriptRoot 'c/raw_output.c'), (Join-Path $PSScriptRoot 'c/test_raw_output.c'), '-o', (Join-Path $output 'test-raw-output.exe'))
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { & $Compiler cc @outputArguments }
else { & $Compiler @outputArguments }
if ($LASTEXITCODE -ne 0) { throw 'C raw output test build failed' }
& (Join-Path $output 'test-raw-output.exe')
if ($LASTEXITCODE -ne 0) { throw 'C raw output tests failed' }
$framingArguments = @('-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', (Join-Path $PSScriptRoot 'c/raw_framing.c'), (Join-Path $PSScriptRoot 'c/raw_packet.c'), (Join-Path $PSScriptRoot 'c/test_raw_framing.c'), '-o', (Join-Path $output 'test-raw-framing.exe'))
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { & $Compiler cc @framingArguments }
else { & $Compiler @framingArguments }
if ($LASTEXITCODE -ne 0) { throw 'USB framing test build failed' }
& (Join-Path $output 'test-raw-framing.exe')
if ($LASTEXITCODE -ne 0) { throw 'USB framing tests failed' }
$inputArguments = @('-std=c99', '-O2', '-DTR_INPUT_TEST', '-Wall', '-Wextra', '-Werror', (Join-Path $PSScriptRoot 'c/raw_input.c'), (Join-Path $PSScriptRoot 'c/test_raw_input.c'), '-o', (Join-Path $output 'test-raw-input.exe'))
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { & $Compiler cc @inputArguments }
else { & $Compiler @inputArguments }
if ($LASTEXITCODE -ne 0) { throw 'C input test build failed' }
& (Join-Path $output 'test-raw-input.exe')
if ($LASTEXITCODE -ne 0) { throw 'C input tests failed' }
$preservedArguments = @('-std=c99', '-O2', '-DTR_INPUT_TEST', '-DTR_INPUT_PRESERVE_HEADER', '-Wall', '-Wextra', '-Werror', (Join-Path $PSScriptRoot 'c/raw_input.c'), (Join-Path $PSScriptRoot 'c/test_raw_input.c'), '-o', (Join-Path $output 'test-preserved-input.exe'))
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { & $Compiler cc @preservedArguments }
else { & $Compiler @preservedArguments }
if ($LASTEXITCODE -ne 0) { throw 'Preserved input test build failed' }
& (Join-Path $output 'test-preserved-input.exe')
if ($LASTEXITCODE -ne 0) { throw 'Preserved input tests failed' }
$verifiedArguments = @('-std=c99', '-O2', '-DTR_OUTPUT_TEST', '-DTR_OUTPUT_PREFIX_CRC', '-Wall', '-Wextra', '-Werror', (Join-Path $PSScriptRoot 'c/raw_verified_output.c'), (Join-Path $PSScriptRoot 'c/test_raw_output.c'), '-o', (Join-Path $output 'test-verified-output.exe'))
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { & $Compiler cc @verifiedArguments }
else { & $Compiler @verifiedArguments }
if ($LASTEXITCODE -ne 0) { throw 'Verified output test build failed' }
& (Join-Path $output 'test-verified-output.exe')
if ($LASTEXITCODE -ne 0) { throw 'Verified output tests failed' }
