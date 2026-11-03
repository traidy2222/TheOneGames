param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('validate','play','stop','capture','build','status','refresh','save','planet','landscape','venus','venus-surface','venus-orbit','validate-planet','flight','flight-descend','flight-ascend','flight-status','flight-validate-ground','flight-verify','volume-verify','lighting-verify')]
    [string]$Action,
    [int]$TimeoutSeconds = 60
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$bridgeRoot = Join-Path $projectRoot 'Library\TerrainBridge'
if (!(Test-Path -LiteralPath $bridgeRoot)) { throw 'Unity has not initialized the bridge. Open this project and let scripts import.' }
$request = Join-Path $bridgeRoot 'request.json'
$response = Join-Path $bridgeRoot 'response.json'
$lockPath = Join-Path $bridgeRoot 'client.lock'
$clientLock = $null
try {
    $clientLock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None')
    if (Test-Path -LiteralPath $request) { throw 'A bridge command is already pending. Wait for it before sending another.' }
    # Remove stale acknowledgements before publishing a new request.
    if (Test-Path -LiteralPath $response) { Remove-Item -LiteralPath $response }
    $tempRequest = Join-Path $bridgeRoot 'request.tmp'
    @{action=$Action} | ConvertTo-Json -Compress | Set-Content -LiteralPath $tempRequest -Encoding utf8
    Move-Item -LiteralPath $tempRequest -Destination $request
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        if (Test-Path -LiteralPath $response) {
            try { $result = Get-Content -LiteralPath $response -Raw | ConvertFrom-Json } catch { Start-Sleep -Milliseconds 100; continue }
            if (!$result.ok) { throw $result.message }
            if ($result.action -ne $Action) { throw 'Unexpected acknowledgement; another client may be using the bridge.' }
            $result | ConvertTo-Json -Compress
            if ($Action -eq 'capture') { Join-Path $bridgeRoot 'preview.png' }
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'Unity did not acknowledge before timeout. The command may still run; inspect request.json and response.json before retrying.'
} finally {
    if ($clientLock) { $clientLock.Dispose() }
}

