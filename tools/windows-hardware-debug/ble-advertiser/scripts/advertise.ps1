# SPDX-License-Identifier: Apache-2.0
<#
.SYNOPSIS
Builds when necessary and runs a bounded Windows BLE test advertisement.

.DESCRIPTION
Publishes legacy manufacturer data through the Windows Bluetooth stack. The
ready file is written only after the publisher reaches Started. Existing ready
files are overwritten by the native tool to prevent stale-success evidence.
#>

[CmdletBinding(DefaultParameterSetName = 'Text')]
param(
    [Parameter(ParameterSetName = 'Text')]
    [string]$Payload = 'BK7258-N12V',

    [Parameter(Mandatory, ParameterSetName = 'Hex')]
    [string]$PayloadHex,

    [string]$CompanyId = '0xFFFE',

    [string]$DurationSec = '30',

    [string]$StartupTimeoutMs = '5000',

    [string]$ReadyFile,

    [switch]$Probe,

    [switch]$Build
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$toolRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$executable = Join-Path $toolRoot 'out\ble_advertiser.exe'
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
$readyPath = $null

if (-not [string]::IsNullOrWhiteSpace($ReadyFile)) {
    $readyPath = [System.IO.Path]::GetFullPath($ReadyFile)
    if (Test-Path -LiteralPath $readyPath) {
        Remove-Item -LiteralPath $readyPath -Force
    }
}

if ($Build -or -not (Test-Path -LiteralPath $executable)) {
    & $buildScript
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$arguments = @()
if ($Probe) {
    $arguments += '--probe'
}
else {
    $arguments += @('--company-id', $CompanyId)
    if ($PSCmdlet.ParameterSetName -eq 'Hex') {
        $arguments += @('--payload-hex', $PayloadHex)
    }
    else {
        $arguments += @('--payload', $Payload)
    }

    $arguments += @(
        '--duration', $DurationSec,
        '--startup-timeout-ms', $StartupTimeoutMs
    )

    if ($null -ne $readyPath) {
        $arguments += @('--ready-file', $readyPath)
    }
}

& $executable @arguments
exit $LASTEXITCODE
