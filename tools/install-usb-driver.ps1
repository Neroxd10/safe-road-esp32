#Requires -Version 5.1
#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'

$drivers = @(
    @{ Name = 'Espressif USB Serial/JTAG'; Id = 'VID_303A&PID_1001'; Package = 'idf-driver/idf-driver-esp32-usb-jtag-2021-07-15.zip' }
    @{ Name = 'Silicon Labs CP210x'; Id = 'VID_10C4&PID_EA60'; Package = 'idf-installer/CP210x_Universal_Windows_Driver.zip' }
    @{ Name = 'WCH CH340/CH341'; Id = 'VID_1A86&PID_(7523|5523)'; Package = 'idf-installer/CH341SER.ZIP' }
)

function Get-ConnectedUsbDevices {
    Get-CimInstance Win32_PnPEntity -Filter "Present = TRUE AND PNPDeviceID LIKE 'USB%'"
}

function Get-SerialPorts($devices, $driver) {
    $devices | Where-Object {
        $_.PNPDeviceID -match "\\$($driver.Id)(&|\\)" -and
        $_.ConfigManagerErrorCode -eq 0 -and $_.Name -match '\(COM\d+\)$'
    }
}

$devices = @(Get-ConnectedUsbDevices)
$connected = @($drivers | Where-Object {
    $driver = $_
    $devices.PNPDeviceID -match "\\$($driver.Id)(&|\\)"
})
if (!$connected.Count) {
    throw 'No supported ESP32 USB device detected. Plug in the board with a USB data cable and run the script again.'
}

foreach ($driver in $connected) {
    $ports = @(Get-SerialPorts $devices $driver)
    if (!$ports.Count) {
        Write-Host "Installing $($driver.Name) from Espressif..."
        $folder = Join-Path ([IO.Path]::GetTempPath()) "safe-road-usb-$([guid]::NewGuid())"
        New-Item -ItemType Directory -Path $folder | Out-Null
        try {
            $archive = Join-Path $folder 'driver.zip'
            Invoke-WebRequest -UseBasicParsing -Uri "https://dl.espressif.com/dl/$($driver.Package)" -OutFile $archive
            Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $folder 'driver')
            & pnputil.exe /add-driver (Join-Path $folder 'driver\*.inf') /subdirs /install
            $result = $LASTEXITCODE
            if ($result -in 1641, 3010) {
                Write-Host 'Restart Windows, then rerun this script to find the COM port.'
                exit 0
            }
            if ($result -ne 0) { throw "PnPUtil returned exit code $result." }
        }
        finally {
            Remove-Item -LiteralPath $folder -Recurse -Force
        }

        for ($attempt = 0; $attempt -lt 5 -and !$ports.Count; $attempt++) {
            Start-Sleep -Seconds 1
            $ports = @(Get-SerialPorts @(Get-ConnectedUsbDevices) $driver)
        }
    }
    if (!$ports.Count) {
        throw "Windows has no COM port for $($driver.Name). Reconnect the board and rerun this script."
    }
    $ports | ForEach-Object { Write-Host "Ready: $($_.Name)" }
}
