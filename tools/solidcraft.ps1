# Project-local SolidCraft CLI/MCP entry point. No robot/device access.
$ErrorActionPreference = 'Stop'
$cli = $env:SOLIDCRAFT_CLI
if (-not $cli) {
    $found = Get-Command solidcraft-cli -ErrorAction SilentlyContinue
    if ($found) { $cli = $found.Source }
    elseif ($env:ProgramFiles) {
        $cli = Join-Path $env:ProgramFiles 'SolidCraft\solidcraft-cli.exe'
    }
}
if (-not $cli -or -not (Test-Path -LiteralPath $cli -PathType Leaf)) {
    throw 'Install SolidCraft or set SOLIDCRAFT_CLI to the full path of solidcraft-cli.'
}
$cad = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../hardware/mechanical'))
if ($args.Count -eq 0) {
    & $cli --version
} elseif ($args[0] -eq 'mcp') {
    # Explicit headless mode avoids modifying an unrelated open desktop design.
    & $cli @args --headless --allow-path $cad
} else {
    & $cli @args
}
exit $LASTEXITCODE
