<#
.SYNOPSIS
Captures raw bytes from a Windows serial port for a fixed duration.

.DESCRIPTION
Uses only System.IO.Ports.SerialPort. The output file contains the exact bytes
read from the port. Diagnostics are written to stderr so that -EchoToStdout can
write only captured serial bytes to stdout.

.PARAMETER Port
Serial port name. Defaults to COM11.

.PARAMETER Baud
Baud rate. Defaults to 460800.

.PARAMETER DurationSec
Capture duration in seconds. Defaults to 20.

.PARAMETER OutputFile
Raw output file. Defaults to a timestamped .bin file in the current directory.
An existing file is overwritten.

.PARAMETER ReadyFile
Optional readiness marker. It is created (or truncated) only after the serial
port and output file have both been opened successfully.

.PARAMETER Command
Optional line sent to the serial port after it opens. CRLF is appended.

.PARAMETER CommandDelayMs
Delay after opening the port before sending Command. Defaults to 200 ms.

.PARAMETER EchoToStdout
Also writes captured bytes, unchanged, to stdout. Status and error messages
remain on stderr.

.EXAMPLE
.\capture_windows_serial.ps1 -Port COM11 -Baud 460800 -DurationSec 20 -OutputFile .\capture.bin

.EXAMPLE
.\capture_windows_serial.ps1 -OutputFile .\capture.bin -ReadyFile .\serial.ready -EchoToStdout
#>

[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [string]$Port = 'COM11',

    [ValidateRange(1, 2147483647)]
    [int]$Baud = 460800,

    [ValidateScript({ $_ -gt 0 })]
    [double]$DurationSec = 20,

    [ValidateNotNullOrEmpty()]
    [string]$OutputFile = ("serial_capture_{0}.bin" -f (Get-Date -Format 'yyyyMMdd_HHmmss')),

    [string]$ReadyFile,

    [string]$Command,

    [ValidateRange(0, 60000)]
    [int]$CommandDelayMs = 200,

    [Alias('Echo')]
    [switch]$EchoToStdout
)

[System.Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[System.Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$serial = $null
$outputStream = $null
$stdoutStream = $null
$stopwatch = $null
$exitCode = 0
$stage = 'validating arguments'
$totalBytes = [int64]0

try {
    $outputPath = [System.IO.Path]::GetFullPath($OutputFile)
    $readyPath = $null

    if (-not [string]::IsNullOrWhiteSpace($ReadyFile)) {
        $readyPath = [System.IO.Path]::GetFullPath($ReadyFile)
        if ([string]::Equals($outputPath, $readyPath, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw 'OutputFile and ReadyFile must refer to different files.'
        }
    }

    $stage = "opening serial port '$Port' at $Baud baud"
    $serial = [System.IO.Ports.SerialPort]::new(
        $Port,
        $Baud,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serial.Handshake = [System.IO.Ports.Handshake]::None
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.ReadTimeout = 100
    $serial.WriteTimeout = 100
    $serial.Open()

    $stage = "opening output file '$outputPath'"
    $outputDirectory = [System.IO.Path]::GetDirectoryName($outputPath)
    if (-not [string]::IsNullOrEmpty($outputDirectory)) {
        [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
    }
    $outputStream = [System.IO.FileStream]::new(
        $outputPath,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read
    )

    if ($null -ne $readyPath) {
        $stage = "creating ready file '$readyPath'"
        $readyDirectory = [System.IO.Path]::GetDirectoryName($readyPath)
        if (-not [string]::IsNullOrEmpty($readyDirectory)) {
            [System.IO.Directory]::CreateDirectory($readyDirectory) | Out-Null
        }
        [System.IO.File]::WriteAllBytes($readyPath, [byte[]]@())
    }

    if ($EchoToStdout) {
        $stdoutStream = [System.Console]::OpenStandardOutput()
    }

    if (-not [string]::IsNullOrEmpty($Command)) {
        $stage = "sending command '$Command'"
        if ($CommandDelayMs -gt 0) {
            Start-Sleep -Milliseconds $CommandDelayMs
        }
        $serial.Write($Command + "`r`n")
    }

    [System.Console]::Error.WriteLine((
        "Capturing {0} at {1} baud for {2} second(s) to '{3}'." -f
            $Port, $Baud, $DurationSec, $outputPath
    ))

    $stage = "reading serial port '$Port'"
    $buffer = New-Object byte[] 8192
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    while ($stopwatch.Elapsed.TotalSeconds -lt $DurationSec) {
        $available = $serial.BytesToRead
        if ($available -le 0) {
            Start-Sleep -Milliseconds 5
            continue
        }

        $bytesToRead = [System.Math]::Min($available, $buffer.Length)
        try {
            $bytesRead = $serial.Read($buffer, 0, $bytesToRead)
        }
        catch [System.TimeoutException] {
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
}
catch {
    $exitCode = 1
    [System.Console]::Error.WriteLine((
        "ERROR: Serial capture failed while {0}: {1}" -f
            $stage, $_.Exception.Message
    ))
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
            if ($exitCode -eq 0) {
                $exitCode = 1
                [System.Console]::Error.WriteLine((
                    "ERROR: Failed to finalize output file: {0}" -f $_.Exception.Message
                ))
            }
        }
    }

    if ($null -ne $stdoutStream) {
        try {
            $stdoutStream.Flush()
        }
        catch {
            if ($exitCode -eq 0) {
                $exitCode = 1
                [System.Console]::Error.WriteLine((
                    "ERROR: Failed to flush stdout: {0}" -f $_.Exception.Message
                ))
            }
        }
    }

    if ($null -ne $serial) {
        try {
            if ($serial.IsOpen) {
                $serial.Close()
            }
            $serial.Dispose()
        }
        catch {
            if ($exitCode -eq 0) {
                $exitCode = 1
                [System.Console]::Error.WriteLine((
                    "ERROR: Failed to close serial port '{0}': {1}" -f
                        $Port, $_.Exception.Message
                ))
            }
        }
    }
}

if ($exitCode -eq 0) {
    [System.Console]::Error.WriteLine((
        "Capture complete: {0} byte(s) written to '{1}'." -f
            $totalBytes, $outputPath
    ))
}

exit $exitCode
