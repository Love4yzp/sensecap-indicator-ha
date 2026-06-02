$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BundledEsptool = Join-Path $Root "tools\esptool\windows-amd64\esptool.exe"
$Fw = Join-Path $Root "firmware\esp32s3"
$Baud = if ($env:ESP_BAUD) { $env:ESP_BAUD } else { "460800" }
$Port = if ($args.Count -gt 0) { $args[0] } elseif ($env:ESPPORT) { $env:ESPPORT } else { "" }

if (Test-Path $BundledEsptool) {
    $Esptool = $BundledEsptool
} else {
    $cmd = Get-Command esptool -ErrorAction SilentlyContinue
    if (!$cmd) {
        $cmd = Get-Command esptool.py -ErrorAction SilentlyContinue
    }
    if (!$cmd) {
        throw "No bundled esptool and no esptool on PATH."
    }
    $Esptool = $cmd.Source
}

foreach ($file in @("bootloader.bin", "partition-table.bin", "indicator_ha.bin")) {
    $path = Join-Path $Fw $file
    if (!(Test-Path $path)) {
        throw "Missing ESP32-S3 firmware file: $path"
    }
}

$PortArgs = @()
if ($Port) {
    $PortArgs = @("--port", $Port)
} else {
    Write-Host "No ESP32-S3 port supplied; esptool will autodetect."
    Write-Host "To pin a port: `$env:ESPPORT='COM7'; .\windows\flash_esp32s3.ps1"
}

& $Esptool --chip esp32s3 @PortArgs --baud $Baud `
    --before default_reset --after hard_reset `
    write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m `
    0x0 (Join-Path $Fw "bootloader.bin") `
    0x8000 (Join-Path $Fw "partition-table.bin") `
    0x10000 (Join-Path $Fw "indicator_ha.bin")

if ($LASTEXITCODE -ne 0) {
    throw "esptool failed with exit code $LASTEXITCODE"
}
