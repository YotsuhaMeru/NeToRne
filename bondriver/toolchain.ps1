function Resolve-TorneZig {
    param([string]$ZigPath)
    if ([string]::IsNullOrWhiteSpace($ZigPath)) { $ZigPath = 'zig' }
    $compiler = Get-Command -Name $ZigPath -CommandType Application -ErrorAction SilentlyContinue
    if (-not $compiler) {
        throw 'Zig was not found. Install Zig 0.15.2 and add it to PATH, or pass -ZigPath C:\path\to\zig.exe (TORNE_ZIG is also supported).'
    }
    return $compiler.Source
}
