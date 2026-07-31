# SPDX-License-Identifier: Apache-2.0
<#
.SYNOPSIS
Runs a logged SEGGER J-Link Commander debug action.

.DESCRIPTION
Generates the exact J-Link Commander input, stores it beside the log, and
writes an optional JSON summary. ReadMemory and Registers are the conservative
actions. Reset/halt/resume require -AllowTargetControl. An arbitrary command
file requires -AllowUnsafeCommands because it may erase flash, write memory,
or alter security state.

.EXAMPLE
.\jlink_debug.ps1 -Action ReadMemory -Device CORTEX-M33 `
  -Address 0x20000000 -Count 16 -Width 32 -OutputLog .\logs\memory.log

.EXAMPLE
.\jlink_debug.ps1 -Action Reset -Device STM32H743ZI -ResetType 2 `
  -ResumeAfterReset -AllowTargetControl -OutputLog .\logs\reset.log

.EXAMPLE
.\jlink_debug.ps1 -Action CommandFile -Device CORTEX-M33 `
  -CommandFile .\commands.jlink -AllowUnsafeCommands
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('ReadMemory', 'Registers', 'Reset', 'CommandFile')]
    [string]$Action,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Device,

    [ValidateSet('SWD', 'JTAG')]
    [string]$Interface = 'SWD',

    [ValidateRange(1, 50000)]
    [int]$Speed = 1000,

    [ValidateRange(1, 600)]
    [int]$TimeoutSec = 30,

    [string]$JLinkExe,

    [string]$Address,

    [ValidateRange(1, 1048576)]
    [int]$Count = 1,

    [ValidateSet(8, 16, 32)]
    [int]$Width = 32,

    [switch]$Halt,

    [switch]$Resume,

    [ValidateRange(-1, 255)]
    [int]$ResetType = -1,

    [switch]$ResumeAfterReset,

    [string]$CommandFile,

    [switch]$AllowTargetControl,

    [switch]$AllowUnsafeCommands,

    [string]$OutputLog = ("jlink-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss')),

    [string]$GeneratedCommandFile,

    [string]$SummaryFile,

    [switch]$DryRun
)

[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$exitCode = 0
$errorMessage = $null
$startedUtc = [DateTime]::UtcNow
$endedUtc = $null
$resolvedJLink = $null
$outputPath = $null
$generatedPath = $null
$summaryPath = $null
$scriptText = $null
$processExitCode = $null
$process = $null
$timedOut = $false

function New-ParentDirectory([string]$Path) {
    $parent = [IO.Path]::GetDirectoryName($Path)
    if (-not [string]::IsNullOrEmpty($parent)) {
        [IO.Directory]::CreateDirectory($parent) | Out-Null
    }
}

function Resolve-JLinkExecutable {
    if (-not [string]::IsNullOrWhiteSpace($JLinkExe)) {
        $candidate = [IO.Path]::GetFullPath($JLinkExe)
        if (-not [IO.File]::Exists($candidate)) {
            throw "J-Link executable not found: $candidate"
        }
        return $candidate
    }

    $candidates = New-Object System.Collections.Generic.List[string]
    $programFiles = [Environment]::GetEnvironmentVariable('ProgramFiles')
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not [string]::IsNullOrWhiteSpace($programFiles)) {
        $candidates.Add((Join-Path $programFiles 'SEGGER\JLink\JLink.exe'))
    }
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $candidates.Add((Join-Path $programFilesX86 'SEGGER\JLink\JLink.exe'))
    }
    foreach ($candidate in $candidates) {
        if ([IO.File]::Exists($candidate)) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'JLink.exe was not found. Install SEGGER J-Link or pass -JLinkExe.'
}

function Write-JLinkSummary {
    if ([string]::IsNullOrWhiteSpace($summaryPath)) {
        return
    }
    try {
        New-ParentDirectory $summaryPath
        $summary = [ordered]@{
            format                 = 1
            tool                   = 'jlink_debug'
            status                 = $(if ($exitCode -eq 0) { 'ok' } else { 'error' })
            error                  = $errorMessage
            action                 = $Action
            device                 = $Device
            interface              = $Interface
            speed_khz              = $Speed
            timeout_sec            = $TimeoutSec
            jlink_exe              = $resolvedJLink
            output_log             = $outputPath
            generated_command_file = $generatedPath
            process_exit_code      = $processExitCode
            dry_run                = [bool]$DryRun
            started_utc            = $startedUtc.ToString('o')
            ended_utc              = $endedUtc.ToString('o')
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
                "ERROR: Failed to write J-Link summary: $($_.Exception.Message)"
            )
        }
    }
}

try {
    if (($Action -eq 'Reset' -or $Halt -or $Resume) -and -not $AllowTargetControl) {
        throw 'Reset/halt/resume requires the explicit -AllowTargetControl switch.'
    }
    if ($Action -eq 'CommandFile' -and -not $AllowUnsafeCommands) {
        throw 'CommandFile requires -AllowUnsafeCommands; arbitrary J-Link commands can be destructive.'
    }

    $commands = New-Object System.Collections.Generic.List[string]
    switch ($Action) {
        'ReadMemory' {
            if ([string]::IsNullOrWhiteSpace($Address) -or
                $Address -notmatch '^0[xX][0-9a-fA-F]+$') {
                throw 'ReadMemory requires -Address in hexadecimal form, for example 0x20000000.'
            }
            if ($Halt) { $commands.Add('Halt') }
            $commands.Add(("mem{0} {1},{2}" -f $Width, $Address, $Count))
            if ($Resume) { $commands.Add('Go') }
        }
        'Registers' {
            if ($Halt) { $commands.Add('Halt') }
            $commands.Add('Regs')
            if ($Resume) { $commands.Add('Go') }
        }
        'Reset' {
            if ($ResetType -ge 0) {
                $commands.Add("RSetType $ResetType")
            }
            $commands.Add('Reset')
            if ($ResumeAfterReset) { $commands.Add('Go') }
        }
        'CommandFile' {
            if ([string]::IsNullOrWhiteSpace($CommandFile)) {
                throw 'CommandFile action requires -CommandFile.'
            }
            $commandPath = [IO.Path]::GetFullPath($CommandFile)
            if (-not [IO.File]::Exists($commandPath)) {
                throw "J-Link command file not found: $commandPath"
            }
            foreach ($line in [IO.File]::ReadAllLines($commandPath)) {
                $commands.Add($line)
            }
        }
    }

    if (-not ($commands | Where-Object { $_ -match '^\s*(Exit|Quit)\s*$' })) {
        $commands.Add('Exit')
    }
    $scriptText = ($commands -join [Environment]::NewLine) + [Environment]::NewLine

    $outputPath = [IO.Path]::GetFullPath($OutputLog)
    if ([string]::IsNullOrWhiteSpace($GeneratedCommandFile)) {
        $generatedPath = [IO.Path]::ChangeExtension($outputPath, '.jlink')
    }
    else {
        $generatedPath = [IO.Path]::GetFullPath($GeneratedCommandFile)
    }
    if (-not [string]::IsNullOrWhiteSpace($SummaryFile)) {
        $summaryPath = [IO.Path]::GetFullPath($SummaryFile)
    }
    New-ParentDirectory $outputPath
    New-ParentDirectory $generatedPath
    $utf8 = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText($generatedPath, $scriptText, $utf8)

    if ($DryRun) {
        [IO.File]::WriteAllText(
            $outputPath,
            "JLINK_DRY_RUN`r`n$scriptText",
            $utf8
        )
        Write-Output "JLINK_DRY_RUN command_file='$generatedPath'"
    }
    else {
        $resolvedJLink = Resolve-JLinkExecutable
        $arguments = "-device `"$Device`" -if $Interface -speed $Speed " +
                     "-autoconnect 1 -NoGui 1 -ExitOnError 1 " +
                     "-CommandFile `"$generatedPath`""
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $resolvedJLink
        $startInfo.Arguments = $arguments
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.RedirectStandardInput = $false
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        if (-not $process.Start()) {
            throw 'Failed to start JLink.exe.'
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSec * 1000)) {
            $timedOut = $true
            $process.Kill()
            $process.WaitForExit()
        }
        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result
        $processExitCode = $process.ExitCode
        $process.Dispose()
        $process = $null

        $logText = "COMMAND_FILE=$generatedPath`r`n" +
                   "COMMAND_LINE=$resolvedJLink $arguments`r`n" +
                   $stdout + $stderr
        [IO.File]::WriteAllText($outputPath, $logText, $utf8)
        Write-Output $stdout
        if (-not [string]::IsNullOrEmpty($stderr)) {
            [Console]::Error.Write($stderr)
        }
        if ($timedOut) {
            throw "JLink.exe did not complete within $TimeoutSec seconds."
        }
        if ($processExitCode -ne 0) {
            throw "JLink.exe exited with code $processExitCode."
        }
        Write-Output "JLINK_OK action=$Action log='$outputPath'"
    }
}
catch {
    $exitCode = 1
    $errorMessage = $_.Exception.Message
    [Console]::Error.WriteLine("ERROR: J-Link action failed: $errorMessage")
}
finally {
    if ($null -ne $process) {
        try {
            if (-not $process.HasExited) { $process.Kill() }
            $process.Dispose()
        }
        catch { }
    }
    $endedUtc = [DateTime]::UtcNow
    Write-JLinkSummary
}

exit $exitCode
