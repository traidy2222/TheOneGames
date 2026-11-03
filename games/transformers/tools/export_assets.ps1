param([string]$Cargo='C:/Users/traid/.cargo/bin/cargo.exe')
$ErrorActionPreference='Stop'
$gameRoot=Split-Path $PSScriptRoot -Parent
$buildRoot=Join-Path $gameRoot 'build'
$sourceRoot=Join-Path $buildRoot 'source-assets'
$exportRoot=Join-Path $buildRoot 'exported'
$inventoryRoot=Join-Path $buildRoot 'inventory'
$checkout=Join-Path $buildRoot 'third-party/rotf-robots'
$revision='dd97296c9a618d06cc2e01bf38c6221b057f6eba'
if(!(Test-Path (Join-Path $sourceRoot 'bnxglobal.str'))){throw 'Run extract_disc.ps1 first'}
if(!(Test-Path (Join-Path $checkout '.git'))) {
    git clone --no-checkout https://github.com/HUNK2/rotf-robots $checkout
    if($LASTEXITCODE -ne 0){throw 'Exporter clone failed'}
    git -C $checkout checkout --detach $revision
    if($LASTEXITCODE -ne 0){throw 'Exporter checkout failed'}
}
$actual=git -C $checkout rev-parse HEAD
if($actual -ne $revision){throw "Exporter revision differs; preserve checkout and review it: $actual"}
New-Item -ItemType Directory -Force $inventoryRoot | Out-Null
foreach($name in @('optimus_supplement.rs','optimus_source_audit.rs')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "patches/$name") -Destination (Join-Path $checkout "export/examples/$name")
}
Push-Location $checkout
try {
    & $Cargo build --release -p rotf-export --locked
    if($LASTEXITCODE -ne 0){throw 'Exporter build failed'}
    foreach($variant in @('optimus','optimusflight','optimusgold')) {
        & './target/release/rotf-export.exe' --character $variant --game-dir $sourceRoot --out (Join-Path $exportRoot $variant) |
            Tee-Object -FilePath (Join-Path $inventoryRoot "export-$variant.log")
        if($LASTEXITCODE -ne 0){throw "Export failed: $variant"}
    }
    & $Cargo run --release --locked -p rotf-export --example optimus_supplement -- $sourceRoot $exportRoot (Join-Path $inventoryRoot 'optimus-supplement-export.json')
    if($LASTEXITCODE -ne 0){throw 'Supplemental export failed'}
} finally {Pop-Location}
