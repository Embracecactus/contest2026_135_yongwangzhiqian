# SPDX-License-Identifier: Apache-2.0
<#
.SYNOPSIS
Builds when necessary and runs the bounded Windows BLE GATT client.

.DESCRIPTION
Uses the Windows Bluetooth stack without opening a GUI. A result file is
created only after every requested gate passes; an existing file is removed
before the native process starts so stale success cannot be reused.
#>

[CmdletBinding(DefaultParameterSetName = 'Target')]
param(
    [Parameter(ParameterSetName = 'Target')]
    [string]$Address,

    [Parameter(ParameterSetName = 'Target')]
    [string]$Name,

    [Parameter(ParameterSetName = 'Target')]
    [string]$ExpectedDeviceName,

    [Parameter(ParameterSetName = 'Target')]
    [switch]$N13,

    [Parameter(ParameterSetName = 'Target')]
    [switch]$N13Negative,

    [Parameter(ParameterSetName = 'Target')]
    [switch]$N13CachedDiscovery,

    [Parameter(ParameterSetName = 'Target')]
    [switch]$N13TargetedDiscovery,

    [Parameter(ParameterSetName = 'Target')]
    [string]$N13BurstCount = '100',

    [Parameter(ParameterSetName = 'Target')]
    [string]$ScanTimeoutMs = '10000',

    [Parameter(ParameterSetName = 'Target')]
    [string]$OperationTimeoutMs = '15000',

    [Parameter(ParameterSetName = 'Target')]
    [string]$RediscoverTimeoutMs = '10000',

    [Parameter(ParameterSetName = 'Target')]
    [string]$ConnectAttempts = '3',

    [Parameter(ParameterSetName = 'Target')]
    [string]$ResultFile,

    [Parameter(ParameterSetName = 'Target')]
    [switch]$ScanOnly,

    [Parameter(ParameterSetName = 'Target')]
    [switch]$NoRediscover,

    [Parameter(Mandatory, ParameterSetName = 'Probe')]
    [switch]$Probe,

    [switch]$Build
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$toolRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$executable = Join-Path $toolRoot 'out\ble_gatt_client.exe'
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
$resultPath = $null

if (-not [string]::IsNullOrWhiteSpace($ResultFile)) {
    $resultPath = [System.IO.Path]::GetFullPath($ResultFile)
    if (Test-Path -LiteralPath $resultPath) {
        Remove-Item -LiteralPath $resultPath -Force
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
    if ([string]::IsNullOrWhiteSpace($Address) -and
        [string]::IsNullOrWhiteSpace($Name)) {
        throw 'Address or Name is required unless Probe is used.'
    }

    if (-not [string]::IsNullOrWhiteSpace($Address)) {
        $arguments += @('--address', $Address)
    }
    if (-not [string]::IsNullOrWhiteSpace($Name)) {
        $arguments += @('--name', $Name)
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedDeviceName)) {
        $arguments += @('--expect-device-name', $ExpectedDeviceName)
    }
    if ($N13) {
        $arguments += @('--n13', '--n13-burst-count', $N13BurstCount)
    }
    if ($N13Negative) {
        if (-not $N13) {
            throw 'N13Negative requires N13.'
        }
        $arguments += '--n13-negative'
    }
    if ($N13CachedDiscovery) {
        if (-not $N13Negative) {
            throw 'N13CachedDiscovery requires N13Negative.'
        }
        $arguments += '--n13-cached-discovery'
    }
    if ($N13TargetedDiscovery) {
        if (-not $N13) {
            throw 'N13TargetedDiscovery requires N13.'
        }
        if ($N13CachedDiscovery) {
            throw 'N13TargetedDiscovery conflicts with N13CachedDiscovery.'
        }
        $arguments += '--n13-targeted-discovery'
    }

    $arguments += @(
        '--scan-timeout-ms', $ScanTimeoutMs,
        '--operation-timeout-ms', $OperationTimeoutMs,
        '--rediscover-timeout-ms', $RediscoverTimeoutMs,
        '--connect-attempts', $ConnectAttempts
    )

    if ($ScanOnly) {
        $arguments += '--scan-only'
    }
    if ($NoRediscover) {
        $arguments += '--no-rediscover'
    }
    if ($null -ne $resultPath) {
        $arguments += @('--result-file', $resultPath)
    }
}

& $executable @arguments
exit $LASTEXITCODE
