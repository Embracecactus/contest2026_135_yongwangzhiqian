# SPDX-License-Identifier: Apache-2.0
<#
.SYNOPSIS
Pulses DTR and/or RTS on a Windows serial port.

.DESCRIPTION
This changes target control signals and may reset or enter the bootloader on
connected hardware. Wiring and electrical polarity are adapter-specific. A
successful pulse only proves that the host toggled the line; verify the target
effect through UART output, a generation counter, or another target-side fact.

.EXAMPLE
.\serial_pulse.ps1 -Port COM7 -Mode RTS -PulseMs 150 `
  -AllowTargetControl -SummaryFile .\logs\pulse.json

.EXAMPLE
.\serial_pulse.ps1 -Port COM7 -Mode DTR -ActiveLevel Deasserted `
  -PulseMs 50 -AllowTargetControl
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,

    [ValidateSet('DTR', 'RTS', 'BOTH')]
    [string]$Mode = 'RTS',

    [ValidateSet('Asserted', 'Deasserted')]
    [string]$ActiveLevel = 'Asserted',

    [ValidateRange(1, 2147483647)]
    [int]$Baud = 115200,

    [ValidateRange(1, 10000)]
    [int]$PulseMs = 150,

    [ValidateRange(0, 10000)]
    [int]$SettleBeforeMs = 200,

    [ValidateRange(0, 10000)]
    [int]$SettleAfterMs = 200,

    [switch]$AllowTargetControl,

    [string]$SummaryFile
)

[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$serial = $null
$exitCode = 0
$errorMessage = $null
$startedUtc = [DateTime]::UtcNow
$endedUtc = $null
$summaryPath = $null

function Set-ControlLines([IO.Ports.SerialPort]$Serial, [bool]$Value) {
    if ($Mode -eq 'DTR' -or $Mode -eq 'BOTH') {
        $Serial.DtrEnable = $Value
    }
    if ($Mode -eq 'RTS' -or $Mode -eq 'BOTH') {
        $Serial.RtsEnable = $Value
    }
}

function Write-PulseSummary {
    if ([string]::IsNullOrWhiteSpace($summaryPath)) {
        return
    }

    try {
        $parent = [IO.Path]::GetDirectoryName($summaryPath)
        if (-not [string]::IsNullOrEmpty($parent)) {
            [IO.Directory]::CreateDirectory($parent) | Out-Null
        }
        $summary = [ordered]@{
            format           = 1
            tool             = 'serial_pulse'
            status           = $(if ($exitCode -eq 0) { 'ok' } else { 'error' })
            error            = $errorMessage
            port             = $Port
            mode             = $Mode
            active_level     = $ActiveLevel
            pulse_ms         = $PulseMs
            settle_before_ms = $SettleBeforeMs
            settle_after_ms  = $SettleAfterMs
            started_utc      = $startedUtc.ToString('o')
            ended_utc        = $endedUtc.ToString('o')
        }
        $utf8 = [Text.UTF8Encoding]::new($false)
        [IO.File]::WriteAllText(
            $summaryPath,
            ($summary | ConvertTo-Json -Depth 4) + [Environment]::NewLine,
            $utf8
        )
    }
    catch {
        if ($exitCode -eq 0) {
            $exitCode = 1
            [Console]::Error.WriteLine(
                "ERROR: Failed to write pulse summary: $($_.Exception.Message)"
            )
        }
    }
}

try {
    if (-not $AllowTargetControl) {
        throw 'A DTR/RTS control pulse requires the explicit -AllowTargetControl switch.'
    }

    if (-not [string]::IsNullOrWhiteSpace($SummaryFile)) {
        $summaryPath = [IO.Path]::GetFullPath($SummaryFile)
    }

    $active = $ActiveLevel -eq 'Asserted'
    $idle = -not $active
    $serial = [IO.Ports.SerialPort]::new(
        $Port,
        $Baud,
        [IO.Ports.Parity]::None,
        8,
        [IO.Ports.StopBits]::One
    )
    $serial.Handshake = [IO.Ports.Handshake]::None
    Set-ControlLines $serial $idle
    $serial.Open()
    Set-ControlLines $serial $idle
    if ($SettleBeforeMs -gt 0) {
        Start-Sleep -Milliseconds $SettleBeforeMs
    }

    Set-ControlLines $serial $active
    Start-Sleep -Milliseconds $PulseMs
    Set-ControlLines $serial $idle
    if ($SettleAfterMs -gt 0) {
        Start-Sleep -Milliseconds $SettleAfterMs
    }
}
catch {
    $exitCode = 1
    $errorMessage = "Failed to pulse ${Port}/${Mode}: $($_.Exception.Message)"
    [Console]::Error.WriteLine("ERROR: $errorMessage")
}
finally {
    if ($null -ne $serial) {
        try {
            if ($serial.IsOpen -and $null -ne $idle) {
                Set-ControlLines $serial $idle
            }
            if ($serial.IsOpen) { $serial.Close() }
            $serial.Dispose()
        }
        catch { }
    }
    $endedUtc = [DateTime]::UtcNow
    Write-PulseSummary
}

if ($exitCode -eq 0) {
    Write-Output (
        "PULSE_OK port=$Port mode=$Mode active=$ActiveLevel pulse_ms=$PulseMs"
    )
}
exit $exitCode
