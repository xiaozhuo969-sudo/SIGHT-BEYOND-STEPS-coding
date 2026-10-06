param(
    [ValidatePattern('^COM[0-9]+$')][string]$Port,
    [string]$FirmwareDirectory='D:\Espressif\releases\feedback-leds-2026-10-06\hand',
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$python='D:\Espressif\tools\python_env\idf6.0_py3.11_env\Scripts\python.exe'
if(!(Test-Path -LiteralPath $python)){ throw 'ESP-IDF Python environment not found.' }
$firmwareRoot=(Resolve-Path -LiteralPath $FirmwareDirectory).Path
$manifest=Get-Content -LiteralPath (Join-Path $firmwareRoot 'sha256.json') -Raw | ConvertFrom-Json
$files=@('bootloader/bootloader.bin','partition_table/partition-table.bin','hand_controller_imu.bin')
foreach($file in $files){
    $expected=$manifest.PSObject.Properties[$file].Value
    $actual=(Get-FileHash -LiteralPath (Join-Path $firmwareRoot $file) -Algorithm SHA256).Hash
    if(!$expected -or $actual -ne $expected){ throw "Firmware checksum mismatch: $file" }
}
Write-Host 'Three firmware files verified (SHA256).'
if($CheckOnly){ return }
if(!$Port){ throw 'Specify the connected ESP32-S3 port, for example -Port COM5.' }
Push-Location -LiteralPath $firmwareRoot
try {
    & $python -m esptool --chip esp32s3 --port $Port --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 $files[0] 0x8000 $files[1] 0x10000 $files[2]
    if($LASTEXITCODE -ne 0){ throw "Flashing failed: exit $LASTEXITCODE" }
} finally { Pop-Location }
