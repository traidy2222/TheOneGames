$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'bridge.py') login
exit $LASTEXITCODE
