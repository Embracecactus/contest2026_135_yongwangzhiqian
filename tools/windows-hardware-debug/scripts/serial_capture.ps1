# SPDX-License-Identifier: Apache-2.0
<#
.SYNOPSIS
Captures exact bytes from a Windows serial port without third-party modules.

.DESCRIPTION
Works in Windows PowerShell 5.1 and PowerShell 7. The raw output is never
decoded or normalized. Optional JSON output is intended for CI and AI agents.
Open the console port before resetting the target so early boot bytes are not
lost.

.EXAMPLE
.\serial_capture.ps1 -ListPorts

.EXAMPLE
.\serial_capture.ps1 -Port COM11 -Baud 460800 -DurationSec 20 `
  -OutputFile .\logs\serial.raw -SummaryFile .\logs\serial.json

.EXAMPLE
.\serial_capture.ps1 -Port COM11 -Baud 115200 -DurationSec 5 `
  -OutputFile .\status.raw -Command 'version' -LineEnding CRLF
#>

[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'List')]
    [switch]$ListPorts,

    [Parameter(ParameterSetName = 'List')]
    [switch]$Json,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$Port,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 2147483647)]
    [int]$Baud = 115200,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(5, 8)]
    [int]$DataBits = 8,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateSet('None', 'Odd', 'Even', 'Mark', 'Space')]
    [string]$Parity = 'None',

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateSet('One', 'OnePointFive', 'Two')]
    [string]$StopBits = 'One',

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateSet('None', 'XOnXOff', 'RequestToSend', 'RequestToSendXOnXOff')]
    [string]$Handshake = 'None',

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateScript({ $_ -gt 0 })]
    [double]$DurationSec = 20,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$OutputFile = ("serial-{0}.raw" -f (Get-Date -Format 'yyyyMMdd-HHmmss')),

    [Parameter(ParameterSetName = 'Capture')]
    [string]$ReadyFile,

    [Parameter(ParameterSetName = 'Capture')]
    [string]$SummaryFile,

    [Parameter(ParameterSetName = 'Capture')]
    [string[]]$Command,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateSet('CRLF', 'LF', 'CR', 'None')]
    [string]$LineEnding = 'CRLF',

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(0, 60000)]
    [int]$CommandDelayMs = 200,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(0, 60000)]
    [int]$CommandIntervalMs = 100,

    [Parameter(ParameterSetName = 'Capture')]
    [switch]$DtrEnable,

    [Parameter(ParameterSetName = 'Capture')]
    [switch]$RtsEnable,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateSet('None', 'DTR', 'RTS', 'BOTH')]
    [string]$ControlPulseMode = 'None',

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateSet('Asserted', 'Deasserted')]
    [string]$ControlPulseActiveLevel = 'Asserted',

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 10000)]
    [int]$ControlPulseMs = 150,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(0, 60000)]
    [int]$ControlPulseDelayMs = 200,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(0, 10000)]
    [int]$ControlPulseSettleMs = 200,

    [Parameter(ParameterSetName = 'Capture')]
    [switch]$AllowTargetControl,

    [Parameter(ParameterSetName = 'Capture')]
    [switch]$EchoToStdout
)

[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Text.UTF8Encoding]::new($false)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ($ListPorts) {
    $ports = @([IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    if ($Json) {
        [ordered]@{ format = 1; ports = $ports } |
            ConvertTo-Json -Depth 3 -Compress
    }
    else {
        $ports
    }
    exit 0
}

$serial = $null
$outputStream = $null
$stdoutStream = $null
$stopwatch = $null
$exitCode = 0
$stage = 'validating arguments'
$totalBytes = [int64]0
$errorMessage = $null
$startedUtc = [DateTime]::UtcNow
$endedUtc = $null
$outputPath = $null
$readyPath = $null
$summaryPath = $null

function New-ParentDirectory([string]$Path) {
    $parent = [IO.Path]::GetDirectoryName($Path)
    if (-not [string]::IsNullOrEmpty($parent)) {
        [IO.Directory]::CreateDirectory($parent) | Out-Null
    }
}

function Set-ControlPulseLines(
    [IO.Ports.SerialPort]$Serial,
    [bool]$Value
) {
    if ($ControlPulseMode -eq 'DTR' -or $ControlPulseMode -eq 'BOTH') {
        $Serial.DtrEnable = $Value
    }
    if ($ControlPulseMode -eq 'RTS' -or $ControlPulseMode -eq 'BOTH') {
        $Serial.RtsEnable = $Value
    }
}

function Write-CaptureSummary {
    if ([string]::IsNullOrWhiteSpace($summaryPath)) {
        return
    }

    try {
        New-ParentDirectory $summaryPath
        $sha256 = $null
        if ($null -ne $outputPath -and [IO.File]::Exists($outputPath)) {
            $sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $outputPath).Hash.ToLowerInvariant()
        }

        $summary = [ordered]@{
            format       = 1
            tool         = 'serial_capture'
            status       = $(if ($exitCode -eq 0) { 'ok' } else { 'error' })
            error        = $errorMessage
            port         = $Port
            baud         = $Baud
            data_bits    = $DataBits
            parity       = $Parity
            stop_bits    = $StopBits
            handshake    = $Handshake
            duration_sec = $DurationSec
            control_pulse = [ordered]@{
                mode         = $ControlPulseMode
                active_level = $ControlPulseActiveLevel
                pulse_ms     = $ControlPulseMs
                delay_ms     = $ControlPulseDelayMs
                settle_ms    = $ControlPulseSettleMs
            }
            bytes        = $totalBytes
            output_file  = $outputPath
            sha256       = $sha256
            ready_file   = $readyPath
            started_utc  = $startedUtc.ToString('o')
            ended_utc    = $endedUtc.ToString('o')
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
                "ERROR: Failed to write summary '$summaryPath': $($_.Exception.Message)"
            )
        }
    }
}

try {
    if ($ControlPulseMode -ne 'None' -and -not $AllowTargetControl) {
        throw 'A DTR/RTS control pulse requires the explicit -AllowTargetControl switch.'
    }
    if ($ControlPulseMode -ne 'None' -and ($DtrEnable -or $RtsEnable)) {
        throw 'DtrEnable/RtsEnable cannot be combined with ControlPulseMode.'
    }
    if ($ControlPulseMode -ne 'None' -and $null -ne $Command -and $Command.Count -gt 0) {
        throw 'Serial commands cannot be combined with ControlPulseMode in one capture.'
    }

    $durationMs = $DurationSec * 1000.0
    if ($ControlPulseMode -ne 'None' -and
        $durationMs -lt ($ControlPulseDelayMs + $ControlPulseMs + $ControlPulseSettleMs)) {
        throw 'DurationSec must cover pulse delay, active pulse, and settle time.'
    }
    if ($null -ne $Command -and $Command.Count -gt 0) {
        $lastCommandAtMs = $CommandDelayMs +
            (($Command.Count - 1) * $CommandIntervalMs)
        if ($durationMs -le $lastCommandAtMs) {
            throw 'DurationSec must extend beyond the final scheduled serial command.'
        }
    }

    $outputPath = [IO.Path]::GetFullPath($OutputFile)
    if (-not [string]::IsNullOrWhiteSpace($ReadyFile)) {
        $readyPath = [IO.Path]::GetFullPath($ReadyFile)
        if ([string]::Equals($outputPath, $readyPath, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'OutputFile and ReadyFile must refer to different files.'
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($SummaryFile)) {
        $summaryPath = [IO.Path]::GetFullPath($SummaryFile)
    }

    $parityValue = [Enum]::Parse([IO.Ports.Parity], $Parity, $true)
    $stopBitsValue = [Enum]::Parse([IO.Ports.StopBits], $StopBits, $true)
    $handshakeValue = [Enum]::Parse([IO.Ports.Handshake], $Handshake, $true)

    $stage = "opening serial port '$Port'"
    $serial = [IO.Ports.SerialPort]::new(
        $Port, $Baud, $parityValue, $DataBits, $stopBitsValue
    )
    $serial.Handshake = $handshakeValue
    if ($ControlPulseMode -eq 'None') {
        $serial.DtrEnable = [bool]$DtrEnable
        $serial.RtsEnable = [bool]$RtsEnable
    }
    else {
        $pulseActive = $ControlPulseActiveLevel -eq 'Asserted'
        Set-ControlPulseLines $serial (-not $pulseActive)
    }
    $serial.ReadTimeout = 100
    $serial.WriteTimeout = 1000
    $serial.Open()

    $stage = "opening output file '$outputPath'"
    New-ParentDirectory $outputPath
    $outputStream = [IO.FileStream]::new(
        $outputPath,
        [IO.FileMode]::Create,
        [IO.FileAccess]::Write,
        [IO.FileShare]::Read
    )

    if ($null -ne $readyPath) {
        $stage = "creating ready file '$readyPath'"
        New-ParentDirectory $readyPath
        [IO.File]::WriteAllBytes($readyPath, [byte[]]@())
    }

    if ($EchoToStdout) {
        $stdoutStream = [Console]::OpenStandardOutput()
    }

    $lineSuffix = switch ($LineEnding) {
        'CRLF' { "`r`n" }
        'LF'   { "`n" }
        'CR'   { "`r" }
        default { '' }
    }

    [Console]::Error.WriteLine(
        "Capturing $Port at $Baud baud for $DurationSec second(s) to '$outputPath'."
    )

    $stage = "reading serial port '$Port'"
    $buffer = New-Object byte[] 8192
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $pulseStarted = $false
    $pulseCompleted = $ControlPulseMode -eq 'None'
    $pulseStartedAtMs = [double]0
    $commandIndex = 0
    $nextCommandAtMs = [double]$CommandDelayMs
    while ($stopwatch.Elapsed.TotalSeconds -lt $DurationSec) {
        $elapsedMs = $stopwatch.Elapsed.TotalMilliseconds

        if (-not $pulseStarted -and $ControlPulseMode -ne 'None' -and
            $elapsedMs -ge $ControlPulseDelayMs) {
            $stage = "asserting $ControlPulseMode on '$Port'"
            Set-ControlPulseLines $serial $pulseActive
            $pulseStarted = $true
            $pulseStartedAtMs = $elapsedMs
            $stage = "reading serial port '$Port'"
        }
        elseif ($pulseStarted -and -not $pulseCompleted -and
            ($elapsedMs - $pulseStartedAtMs) -ge $ControlPulseMs) {
            $stage = "deasserting $ControlPulseMode on '$Port'"
            Set-ControlPulseLines $serial (-not $pulseActive)
            $pulseCompleted = $true
            $stage = "reading serial port '$Port'"
        }

        if ($null -ne $Command -and $commandIndex -lt $Command.Count -and
            $elapsedMs -ge $nextCommandAtMs) {
            $stage = 'sending serial command'
            $serial.Write($Command[$commandIndex] + $lineSuffix)
            $commandIndex++
            $nextCommandAtMs += $CommandIntervalMs
            $stage = "reading serial port '$Port'"
        }

        $available = $serial.BytesToRead
        if ($available -le 0) {
            Start-Sleep -Milliseconds 5
            continue
        }

        $bytesToRead = [Math]::Min($available, $buffer.Length)
        try {
            $bytesRead = $serial.Read($buffer, 0, $bytesToRead)
        }
        catch [TimeoutException] {
            continue
        }

        if ($bytesRead -gt 0) {
            $outputStream.Write($buffer, 0, $bytesRead)
            if ($null -ne $stdoutStream) {
                $stdoutStream.Write($buffer, 0, $bytesRead)
                $stdoutStream.Flush()
            }
            $totalBytes += $bytesRead
        }
    }

    if ($pulseStarted -and -not $pulseCompleted) {
        Set-ControlPulseLines $serial (-not $pulseActive)
        $pulseCompleted = $true
    }
    if ($ControlPulseMode -ne 'None' -and -not $pulseCompleted) {
        throw 'The requested control pulse did not complete during the capture window.'
    }
    if ($null -ne $Command -and $commandIndex -ne $Command.Count) {
        throw 'Not all scheduled serial commands were sent during the capture window.'
    }
}
catch {
    $exitCode = 1
    $errorMessage = "Serial capture failed while ${stage}: $($_.Exception.Message)"
    [Console]::Error.WriteLine("ERROR: $errorMessage")
}
finally {
    if ($null -ne $stopwatch) {
        $stopwatch.Stop()
    }
    if ($null -ne $outputStream) {
        try {
            $outputStream.Flush()
            $outputStream.Dispose()
        }
        catch {
            $exitCode = 1
            $errorMessage = "Failed to finalize output file: $($_.Exception.Message)"
            [Console]::Error.WriteLine("ERROR: $errorMessage")
        }
    }
    if ($null -ne $stdoutStream) {
        try { $stdoutStream.Flush() } catch { }
    }
    if ($null -ne $serial) {
        try {
            if ($serial.IsOpen -and $ControlPulseMode -ne 'None') {
                Set-ControlPulseLines $serial (-not $pulseActive)
            }
            if ($serial.IsOpen) { $serial.Close() }
            $serial.Dispose()
        }
        catch {
            $exitCode = 1
            $errorMessage = "Failed to close serial port '$Port': $($_.Exception.Message)"
            [Console]::Error.WriteLine("ERROR: $errorMessage")
        }
    }
    $endedUtc = [DateTime]::UtcNow
    Write-CaptureSummary
}

if ($exitCode -eq 0) {
    [Console]::Error.WriteLine(
        "SERIAL_CAPTURE_OK port=$Port bytes=$totalBytes output='$outputPath'"
    )
}
exit $exitCode
