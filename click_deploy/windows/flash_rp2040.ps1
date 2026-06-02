$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Elf = Join-Path $Root "firmware\rp2040\firmware.elf"
$BundledPicotool = Join-Path $Root "tools\picotool\windows-amd64\picotool.exe"
$Port = if ($args.Count -gt 0) { $args[0] } elseif ($env:RP2040_PORT) { $env:RP2040_PORT } else { "" }

if (!(Test-Path $Elf)) {
    throw "Missing RP2040 firmware: $Elf"
}

if (Test-Path $BundledPicotool) {
    $Picotool = $BundledPicotool
} else {
    $cmd = Get-Command picotool -ErrorAction SilentlyContinue
    if (!$cmd) {
        throw "No bundled picotool and no picotool on PATH."
    }
    $Picotool = $cmd.Source
}

function Get-PicotoolDeviceCount {
    $output = & $Picotool info -d 2>&1
    if ($LASTEXITCODE -ne 0 -and $output -notmatch "No accessible") {
        Write-Host $output
    }
    return ([regex]::Matches(($output -join "`n"), "(?m)^\s*type:")).Count
}

function Find-Rp2040Port {
    $ports = Get-CimInstance Win32_PnPEntity |
        Where-Object {
            $_.Name -match "\(COM\d+\)" -and
            ($_.PNPDeviceID -match "VID_2886&PID_0050" -or $_.PNPDeviceID -match "VID_2E8A&PID_00C0")
        }
    foreach ($p in $ports) {
        if ($p.Name -match "(COM\d+)") {
            return $Matches[1]
        }
    }
    return ""
}

function Touch-Serial1200 {
    param([string]$PortName)
    try {
        $serial = New-Object System.IO.Ports.SerialPort $PortName, 1200
        $serial.Open()
        Start-Sleep -Milliseconds 100
        $serial.Close()
    } catch {
        Write-Host "Serial reset failed; continuing in case BOOTSEL is already active."
    }
}

if (!$Port) {
    $Port = Find-Rp2040Port
}

$BeforeCount = Get-PicotoolDeviceCount

if ($BeforeCount -gt 0) {
    Write-Host "RP2040 BOOTSEL device is already visible to picotool."
} else {
    if (!$Port) {
        throw "RP2040 serial port was not auto-detected and no BOOTSEL device is visible. Set `$env:RP2040_PORT='COMx' and retry."
    }

    Write-Host "Triggering RP2040 BOOTSEL through $Port"
    Touch-Serial1200 $Port

    Write-Host "Waiting for RP2040 BOOTSEL device..."
    for ($i = 0; $i -lt 30; $i++) {
        $NowCount = Get-PicotoolDeviceCount
        if ($NowCount -gt $BeforeCount -or $NowCount -gt 0) {
            break
        }
        Start-Sleep -Milliseconds 200
    }

    if ((Get-PicotoolDeviceCount) -eq 0) {
        throw "RP2040 BOOTSEL device was not found by picotool."
    }
}

Write-Host "Flashing RP2040 with picotool"
& $Picotool load -v -x $Elf
if ($LASTEXITCODE -ne 0) {
    throw "picotool failed with exit code $LASTEXITCODE"
}
