<#
.SYNOPSIS
Pulses Windows serial-port modem-control lines without external dependencies.

.DESCRIPTION
Used on the current CH342-A adapter where COM7 RTS is wired to the board
physical reset path. A valid reset must still be proven by BClk on COM11.
#>

[CmdletBinding()]
param(
    [string]$Port = 'COM7',

    [ValidateSet('DTR', 'RTS', 'BOTH')]
    [string]$Mode = 'RTS',

    [ValidateRange(1, 10000)]
    [int]$PulseMs = 150,

    [ValidateRange(0, 10000)]
    [int]$SettleMs = 200
)

[System.Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$serial = $null

try {
    $serial = [System.IO.Ports.SerialPort]::new(
        $Port,
        115200,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serial.Handshake = [System.IO.Ports.Handshake]::None
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()
    Start-Sleep -Milliseconds $SettleMs

    if ($Mode -eq 'DTR' -or $Mode -eq 'BOTH') {
        $serial.DtrEnable = $true
    }
    if ($Mode -eq 'RTS' -or $Mode -eq 'BOTH') {
        $serial.RtsEnable = $true
    }

    Start-Sleep -Milliseconds $PulseMs
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    Start-Sleep -Milliseconds $SettleMs

    Write-Output "PULSE_OK port=$Port mode=$Mode pulse_ms=$PulseMs"
}
catch {
    [System.Console]::Error.WriteLine(
        "ERROR: Failed to pulse ${Port}/${Mode}: $($_.Exception.Message)"
    )
    exit 1
}
finally {
    if ($null -ne $serial) {
        try {
            if ($serial.IsOpen) {
                $serial.Close()
            }
            $serial.Dispose()
        }
        catch {
        }
    }
}
