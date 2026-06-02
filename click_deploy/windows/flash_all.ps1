$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "flash_rp2040.ps1")
& (Join-Path $PSScriptRoot "flash_esp32s3.ps1")

Write-Host "Both firmware images have been flashed."
