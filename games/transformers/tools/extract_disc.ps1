param(
    [string]$Iso = 'C:/Users/traid/Desktop/gameroms/PC/Transformers - Revenge of the Fallen/TRANSFORMERS2.iso',
    [string]$SevenZip = 'C:/Program Files/7-Zip/7z.exe',
    [string]$Python = 'C:/Program Files/Python311/python.exe'
)
$ErrorActionPreference = 'Stop'
$gameRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = Join-Path $gameRoot 'build'
$discRoot = Join-Path $buildRoot 'source-disc'
$inventoryRoot = Join-Path $buildRoot 'inventory'
New-Item -ItemType Directory -Force $discRoot,$inventoryRoot | Out-Null
& $SevenZip x $Iso 'Oth.cab' 'levels*.cab' 'ENG.cab' 'audio.cab' 'Transformers2.msi' 'Setup/Data/bnxglobal.str' 'Setup/Data/effects*.lsa' 'Setup/Data/shaders*.lsa' 'Setup/Data/sku.lxb' "-o$discRoot" -aos -bso0 -bsp0
if($LASTEXITCODE -ne 0){throw 'Disc extraction failed'}
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase((Join-Path $discRoot 'Transformers2.msi'),0)
# Read-only metadata lookup; this never executes the MSI or its custom actions.
foreach($table in @('Directory','Component')) {
    $view=$database.OpenView(('SELECT * FROM `' + $table + '`'));$view.Execute()
    $rows=@();$count=if($table -eq 'Directory'){3}else{6}
    while($record=$view.Fetch()) {
        $values=@();for($i=1;$i -le $count;$i++){$values+=$record.StringData($i)}
        $rows+=,@($values)
    }
    ConvertTo-Json -InputObject $rows -Depth 5 | Set-Content -Encoding utf8 (Join-Path $inventoryRoot "msi-$table.json")
    $view.Close()
}
$view=$database.OpenView('SELECT `File`,`FileName`,`FileSize`,`Component_` FROM `File`');$view.Execute()
$rows=@()
while($record=$view.Fetch()) {
    $rows += [pscustomobject]@{id=$record.StringData(1);name=$record.StringData(2);size=$record.IntegerData(3);component=$record.StringData(4)}
}
ConvertTo-Json -InputObject $rows -Depth 4 | Set-Content -Encoding utf8 (Join-Path $inventoryRoot 'msi-files.json')
$view.Close()
& $Python (Join-Path $PSScriptRoot 'stage_assets.py')
if($LASTEXITCODE -ne 0){throw 'Asset staging failed'}
