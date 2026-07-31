# SPDX-License-Identifier: Apache-2.0
<#
.SYNOPSIS
Runs a reproducible Windows serial/J-Link debug session.

.DESCRIPTION
The serial capture is opened before any reset action. Raw bytes, readable text,
tool logs, and a machine-readable session.json are kept in one directory.
SerialPulse supports either the console port itself or a separate reset port.
JLinkReset and serial control-line pulses require -AllowTargetControl.

.EXAMPLE
.\debug_session.ps1 -ConsolePort COM11 -Baud 460800 -DurationSec 15 `
  -Action ManualReset -ExpectedRegex 'boot complete'

.EXAMPLE
.\debug_session.ps1 -ConsolePort COM11 -ResetPort COM7 -Baud 460800 `
  -Action SerialPulse -PulseMode RTS -AllowTargetControl

.EXAMPLE
.\debug_session.ps1 -ConsolePort COM11 -Action JLinkReset `
  -JLinkDevice CORTEX-M33 -ResumeAfterReset -AllowTargetControl `
  -ExpectedRegex 'NuttShell'
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ConsolePort,

    [ValidateRange(1, 2147483647)]
    [int]$Baud = 115200,

    [ValidateScript({ $_ -gt 0 })]
    [double]$DurationSec = 20,

    [ValidateSet('CaptureOnly', 'ManualReset', 'SerialPulse', 'JLinkReset')]
    [string]$Action = 'CaptureOnly',

    [string]$OutputDirectory = (Join-Path $PWD (
        'hardware-debug-{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
    )),

    [switch]$AllowOutputOverwrite,

    [string[]]$Command,

    [string]$CommandBase64Csv,

    [ValidateSet('CRLF', 'LF', 'CR', 'None')]
    [string]$LineEnding = 'CRLF',

    [ValidateRange(0, 60000)]
    [int]$CommandDelayMs = 200,

    [string[]]$ExpectedRegex,

    [string]$ExpectedRegexBase64Csv,

    [string[]]$FailRegex,

    [string]$FailRegexBase64Csv,

    [switch]$AllowEmptyCapture,

    [ValidateRange(100, 60000)]
    [int]$ReadyTimeoutMs = 5000,

    [ValidateRange(0, 60000)]
    [int]$ActionDelayMs = 200,

    [string]$ResetPort,

    [ValidateSet('DTR', 'RTS', 'BOTH')]
    [string]$PulseMode = 'RTS',

    [ValidateSet('Asserted', 'Deasserted')]
    [string]$PulseActiveLevel = 'Asserted',

    [ValidateRange(1, 10000)]
    [int]$PulseMs = 150,

    [ValidateRange(0, 10000)]
    [int]$PulseSettleMs = 200,

    [string]$JLinkDevice,

    [ValidateSet('SWD', 'JTAG')]
    [string]$JLinkInterface = 'SWD',

    [ValidateRange(1, 50000)]
    [int]$JLinkSpeed = 1000,

    [ValidateRange(-1, 255)]
    [int]$JLinkResetType = -1,

    [string]$JLinkExe,

    [switch]$ResumeAfterReset,

    [switch]$AllowTargetControl
)

[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Text.UTF8Encoding]::new($false)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$captureJob = $null
$actionJob = $null
$exitCode = 1
$status = 'error'
$errorMessage = $null
$startedUtc = [DateTime]::UtcNow
$endedUtc = $null
$outputPath = $null
$rawPath = $null
$textPath = $null
$readyPath = $null
$captureSummaryPath = $null
$captureLogPath = $null
$actionSummaryPath = $null
$actionLogPath = $null
$sessionSummaryPath = $null
$captureSummary = $null
$actionSummary = $null
$expectedResults = New-Object System.Collections.Generic.List[object]
$failResults = New-Object System.Collections.Generic.List[object]

function Write-Utf8Text([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Join-JobOutput([object[]]$Items) {
    if ($null -eq $Items -or $Items.Count -eq 0) {
        return ''
    }
    return (($Items | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine) +
        [Environment]::NewLine
}

function Wait-ForReadyFile([string]$Path, [System.Management.Automation.Job]$Job) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $ReadyTimeoutMs) {
        if ([IO.File]::Exists($Path)) {
            return
        }
        if ($Job.State -ne 'Running' -and $Job.State -ne 'NotStarted') {
            throw "Serial capture stopped before it became ready (job state: $($Job.State))."
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Serial capture did not become ready within $ReadyTimeoutMs ms."
}

function Read-JsonFile([string]$Path, [string]$Description) {
    if (-not [IO.File]::Exists($Path)) {
        throw "$Description was not produced: $Path"
    }
    $jsonText = [IO.File]::ReadAllText($Path, [Text.UTF8Encoding]::new($false))
    return ($jsonText | ConvertFrom-Json)
}

function ConvertFrom-Base64Csv([string]$Encoded, [string]$ParameterName) {
    if ([string]::IsNullOrWhiteSpace($Encoded)) {
        return
    }

    foreach ($item in $Encoded.Split(',')) {
        if ([string]::IsNullOrEmpty($item)) {
            throw "$ParameterName contains an empty Base64 item."
        }
        try {
            $bytes = [Convert]::FromBase64String($item)
            [Text.Encoding]::UTF8.GetString($bytes)
        }
        catch {
            throw "$ParameterName contains invalid Base64: $($_.Exception.Message)"
        }
    }
}

function Write-SessionSummary {
    if ([string]::IsNullOrWhiteSpace($sessionSummaryPath)) {
        return
    }

    try {
        $summary = [ordered]@{
            format          = 1
            tool            = 'debug_session'
            status          = $status
            error           = $errorMessage
            action          = $Action
            console_port    = $ConsolePort
            reset_port      = $ResetPort
            baud            = $Baud
            duration_sec    = $DurationSec
            output_directory = $outputPath
            raw_file        = $rawPath
            text_file       = $textPath
            capture_log     = $captureLogPath
            action_log      = $actionLogPath
            capture         = $captureSummary
            target_action   = $actionSummary
            requested_expected_regex = @(
                $ExpectedRegex | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
            )
            requested_fail_regex = @(
                $FailRegex | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
            )
            expected_regex_results = $expectedResults.ToArray()
            fail_regex_results = $failResults.ToArray()
            started_utc     = $startedUtc.ToString('o')
            ended_utc       = $endedUtc.ToString('o')
        }
        Write-Utf8Text $sessionSummaryPath (
            ($summary | ConvertTo-Json -Depth 8) + [Environment]::NewLine
        )
    }
    catch {
        [Console]::Error.WriteLine((
            "ERROR: Failed to write session summary: {0}: {1}`r`n{2}" -f
            $_.Exception.GetType().FullName,
            $_.Exception.Message,
            $_.ScriptStackTrace
        ))
    }
}

try {
    if (-not [string]::IsNullOrWhiteSpace($CommandBase64Csv)) {
        if ($null -ne $Command -and $Command.Count -gt 0) {
            throw 'Command and CommandBase64Csv cannot be combined.'
        }
        $Command = @(ConvertFrom-Base64Csv $CommandBase64Csv 'CommandBase64Csv')
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedRegexBase64Csv)) {
        if ($null -ne $ExpectedRegex -and $ExpectedRegex.Count -gt 0) {
            throw 'ExpectedRegex and ExpectedRegexBase64Csv cannot be combined.'
        }
        $ExpectedRegex = @(
            ConvertFrom-Base64Csv $ExpectedRegexBase64Csv 'ExpectedRegexBase64Csv'
        )
    }
    if (-not [string]::IsNullOrWhiteSpace($FailRegexBase64Csv)) {
        if ($null -ne $FailRegex -and $FailRegex.Count -gt 0) {
            throw 'FailRegex and FailRegexBase64Csv cannot be combined.'
        }
        $FailRegex = @(
            ConvertFrom-Base64Csv $FailRegexBase64Csv 'FailRegexBase64Csv'
        )
    }

    if (($Action -eq 'SerialPulse' -or $Action -eq 'JLinkReset') -and
        -not $AllowTargetControl) {
        throw "$Action requires the explicit -AllowTargetControl switch."
    }
    if ($Action -eq 'JLinkReset' -and [string]::IsNullOrWhiteSpace($JLinkDevice)) {
        throw 'JLinkReset requires -JLinkDevice.'
    }
    if ($Action -eq 'JLinkReset' -and -not $ResumeAfterReset) {
        throw 'JLinkReset capture requires -ResumeAfterReset so the target is not left halted.'
    }
    if ($Action -ne 'CaptureOnly' -and $null -ne $Command -and $Command.Count -gt 0) {
        throw 'Serial commands may only be combined with Action=CaptureOnly.'
    }

    foreach ($pattern in @($ExpectedRegex) + @($FailRegex)) {
        if (-not [string]::IsNullOrWhiteSpace($pattern)) {
            [void][regex]::new($pattern)
        }
    }

    $outputPath = [IO.Path]::GetFullPath($OutputDirectory)
    if ([IO.Directory]::Exists($outputPath) -and
        $null -ne (Get-ChildItem -LiteralPath $outputPath -Force | Select-Object -First 1) -and
        -not $AllowOutputOverwrite) {
        throw "OutputDirectory is not empty; choose a new directory or pass -AllowOutputOverwrite: $outputPath"
    }
    [IO.Directory]::CreateDirectory($outputPath) | Out-Null
    $rawPath = Join-Path $outputPath 'serial.raw'
    $textPath = Join-Path $outputPath 'serial.txt'
    $readyPath = Join-Path $outputPath 'serial.ready'
    $captureSummaryPath = Join-Path $outputPath 'serial.json'
    $captureLogPath = Join-Path $outputPath 'serial-tool.log'
    $actionSummaryPath = Join-Path $outputPath 'action.json'
    $actionLogPath = Join-Path $outputPath 'action.log'
    $sessionSummaryPath = Join-Path $outputPath 'session.json'

    foreach ($stalePath in @(
        $rawPath,
        $textPath,
        $readyPath,
        $captureSummaryPath,
        $captureLogPath,
        $actionSummaryPath,
        $actionLogPath,
        $sessionSummaryPath
    )) {
        if ([IO.File]::Exists($stalePath)) {
            [IO.File]::Delete($stalePath)
        }
    }

    $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    $captureScript = Join-Path $scriptDirectory 'serial_capture.ps1'
    $captureParameters = @{
        Port           = $ConsolePort
        Baud           = $Baud
        DurationSec    = $DurationSec
        OutputFile     = $rawPath
        ReadyFile      = $readyPath
        SummaryFile    = $captureSummaryPath
        LineEnding     = $LineEnding
        CommandDelayMs = $CommandDelayMs
    }
    if ($null -ne $Command -and $Command.Count -gt 0) {
        $captureParameters.Command = $Command
    }

    $inlinePulse = $false
    if ($Action -eq 'SerialPulse' -and
        ([string]::IsNullOrWhiteSpace($ResetPort) -or
         [string]::Equals($ResetPort, $ConsolePort, [StringComparison]::OrdinalIgnoreCase))) {
        $inlinePulse = $true
        $captureParameters.ControlPulseMode = $PulseMode
        $captureParameters.ControlPulseActiveLevel = $PulseActiveLevel
        $captureParameters.ControlPulseMs = $PulseMs
        $captureParameters.ControlPulseDelayMs = $ActionDelayMs
        $captureParameters.ControlPulseSettleMs = $PulseSettleMs
        $captureParameters.AllowTargetControl = $true
    }

    $captureJob = Start-Job -ScriptBlock {
        param($ScriptPath, $Parameters)
        & $ScriptPath @Parameters *>&1
    } -ArgumentList $captureScript, $captureParameters

    Wait-ForReadyFile $readyPath $captureJob
    Write-Output "SERIAL_READY port=$ConsolePort raw='$rawPath'"

    if ($Action -eq 'ManualReset') {
        Write-Utf8Text $actionLogPath (
            "MANUAL_ACTION_REQUIRED: reset the target while capture is active.`r`n"
        )
        Write-Output 'MANUAL_ACTION_REQUIRED reset the target now.'
        $actionSummary = [ordered]@{
            status = 'manual_action_requested'
            proof  = 'none; verify ExpectedRegex or inspect serial.raw'
        }
    }
    elseif ($Action -eq 'SerialPulse' -and $inlinePulse) {
        Write-Utf8Text $actionLogPath (
            "INLINE_SERIAL_PULSE port=$ConsolePort mode=$PulseMode active=$PulseActiveLevel pulse_ms=$PulseMs`r`n"
        )
        $actionSummary = [ordered]@{
            status       = 'delegated_to_serial_capture'
            port         = $ConsolePort
            mode         = $PulseMode
            active_level = $PulseActiveLevel
            pulse_ms     = $PulseMs
        }
    }
    elseif ($Action -eq 'SerialPulse') {
        $pulseScript = Join-Path $scriptDirectory 'serial_pulse.ps1'
        $pulseParameters = @{
            Port           = $ResetPort
            Mode           = $PulseMode
            ActiveLevel    = $PulseActiveLevel
            PulseMs        = $PulseMs
            SettleBeforeMs = $ActionDelayMs
            SettleAfterMs  = $PulseSettleMs
            AllowTargetControl = $true
            SummaryFile    = $actionSummaryPath
        }
        $actionJob = Start-Job -ScriptBlock {
            param($ScriptPath, $Parameters)
            & $ScriptPath @Parameters *>&1
        } -ArgumentList $pulseScript, $pulseParameters
    }
    elseif ($Action -eq 'JLinkReset') {
        $jlinkScript = Join-Path $scriptDirectory 'jlink_debug.ps1'
        $jlinkParameters = @{
            Action             = 'Reset'
            Device             = $JLinkDevice
            Interface          = $JLinkInterface
            Speed              = $JLinkSpeed
            ResetType          = $JLinkResetType
            ResumeAfterReset   = [bool]$ResumeAfterReset
            AllowTargetControl = $true
            OutputLog          = $actionLogPath
            SummaryFile        = $actionSummaryPath
        }
        if (-not [string]::IsNullOrWhiteSpace($JLinkExe)) {
            $jlinkParameters.JLinkExe = $JLinkExe
        }
        if ($ActionDelayMs -gt 0) {
            Start-Sleep -Milliseconds $ActionDelayMs
        }
        $actionJob = Start-Job -ScriptBlock {
            param($ScriptPath, $Parameters)
            & $ScriptPath @Parameters *>&1
        } -ArgumentList $jlinkScript, $jlinkParameters
    }
    else {
        Write-Utf8Text $actionLogPath "NO_TARGET_ACTION`r`n"
        $actionSummary = [ordered]@{ status = 'not_requested' }
    }

    if ($null -ne $actionJob) {
        $completedAction = Wait-Job -Job $actionJob -Timeout 60
        if ($null -eq $completedAction) {
            Stop-Job -Job $actionJob
            throw 'Target action did not complete within 60 seconds.'
        }
        $actionItems = @(Receive-Job -Job $actionJob)
        if ($Action -eq 'SerialPulse') {
            Write-Utf8Text $actionLogPath (Join-JobOutput $actionItems)
        }
        $actionSummary = Read-JsonFile $actionSummaryPath 'Target action summary'
        if ($actionSummary.status -ne 'ok') {
            throw "Target action failed: $($actionSummary.error)"
        }
    }

    $captureTimeout = [int][Math]::Ceiling($DurationSec + 30)
    $completedCapture = Wait-Job -Job $captureJob -Timeout $captureTimeout
    if ($null -eq $completedCapture) {
        Stop-Job -Job $captureJob
        throw "Serial capture did not complete within $captureTimeout seconds."
    }
    $captureItems = @(Receive-Job -Job $captureJob)
    Write-Utf8Text $captureLogPath (Join-JobOutput $captureItems)
    $captureSummary = Read-JsonFile $captureSummaryPath 'Serial capture summary'
    if ($captureSummary.status -ne 'ok') {
        throw "Serial capture failed: $($captureSummary.error)"
    }

    $rawBytes = [IO.File]::ReadAllBytes($rawPath)
    $decodedText = [Text.UTF8Encoding]::new($false, $false).GetString($rawBytes)
    Write-Utf8Text $textPath $decodedText
    if ($rawBytes.Length -eq 0 -and -not $AllowEmptyCapture) {
        throw 'Serial capture is empty; use -AllowEmptyCapture only when that is expected.'
    }

    $verificationFailed = $false
    foreach ($pattern in @($ExpectedRegex)) {
        if ([string]::IsNullOrWhiteSpace($pattern)) { continue }
        $matched = [regex]::IsMatch($decodedText, $pattern)
        $expectedResults.Add([ordered]@{ pattern = $pattern; matched = $matched })
        if (-not $matched) { $verificationFailed = $true }
    }
    foreach ($pattern in @($FailRegex)) {
        if ([string]::IsNullOrWhiteSpace($pattern)) { continue }
        $matched = [regex]::IsMatch($decodedText, $pattern)
        $failResults.Add([ordered]@{ pattern = $pattern; matched = $matched })
        if ($matched) { $verificationFailed = $true }
    }

    if ($verificationFailed) {
        throw 'Target-side serial evidence did not satisfy the requested regex checks.'
    }

    if ($expectedResults.Count -gt 0) {
        $status = 'passed'
    }
    else {
        $status = 'completed_unverified'
    }
    $exitCode = 0
}
catch {
    $exitCode = 1
    $status = 'error'
    $errorMessage = $_.Exception.Message
    if ($null -eq $captureSummary -and
        -not [string]::IsNullOrWhiteSpace($captureSummaryPath) -and
        [IO.File]::Exists($captureSummaryPath)) {
        try {
            $captureSummary = Read-JsonFile $captureSummaryPath 'Serial capture summary'
            if (-not [string]::IsNullOrWhiteSpace($captureSummary.error)) {
                $errorMessage = "Serial capture failed: $($captureSummary.error)"
            }
        }
        catch { }
    }
    [Console]::Error.WriteLine("ERROR: Debug session failed: $errorMessage")
}
finally {
    if ($null -ne $captureJob -and
        -not [string]::IsNullOrWhiteSpace($captureLogPath) -and
        -not [IO.File]::Exists($captureLogPath)) {
        try {
            $captureItems = @(Receive-Job -Job $captureJob -Keep)
            Write-Utf8Text $captureLogPath (Join-JobOutput $captureItems)
        }
        catch { }
    }
    if ($null -eq $captureSummary -and
        -not [string]::IsNullOrWhiteSpace($captureSummaryPath) -and
        [IO.File]::Exists($captureSummaryPath)) {
        try { $captureSummary = Read-JsonFile $captureSummaryPath 'Serial capture summary' }
        catch {
            [Console]::Error.WriteLine(
                "ERROR: Failed to recover serial summary: $($_.Exception.Message)"
            )
        }
    }
    if ($null -ne $captureSummary -and
        -not [string]::IsNullOrWhiteSpace($captureLogPath) -and
        [IO.File]::Exists($captureLogPath) -and
        ([IO.FileInfo]::new($captureLogPath)).Length -eq 0) {
        try {
            Write-Utf8Text $captureLogPath (
                "CAPTURE_STATUS=$($captureSummary.status)`r`n" +
                "CAPTURE_ERROR=$($captureSummary.error)`r`n"
            )
        }
        catch { }
    }
    if ($null -eq $actionSummary -and
        -not [string]::IsNullOrWhiteSpace($actionSummaryPath) -and
        [IO.File]::Exists($actionSummaryPath)) {
        try { $actionSummary = Read-JsonFile $actionSummaryPath 'Target action summary' }
        catch {
            [Console]::Error.WriteLine(
                "ERROR: Failed to recover target action summary: $($_.Exception.Message)"
            )
        }
    }
    foreach ($job in @($captureJob, $actionJob)) {
        if ($null -ne $job) {
            try {
                if ($job.State -eq 'Running') { Stop-Job -Job $job }
                Remove-Job -Job $job -Force
            }
            catch { }
        }
    }
    $endedUtc = [DateTime]::UtcNow
    Write-SessionSummary
}

if ($exitCode -eq 0) {
    Write-Output "DEBUG_SESSION_OK status=$status summary='$sessionSummaryPath'"
}
exit $exitCode
