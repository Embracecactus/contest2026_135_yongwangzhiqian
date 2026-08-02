# SPDX-License-Identifier: Apache-2.0
<#
.SYNOPSIS
Builds the Windows C++/WinRT BLE advertiser command-line tool.

.DESCRIPTION
Uses an installed Visual Studio Build Tools C++ workload and Windows SDK. No
NuGet package or network access is required. Output is written only below the
tool's out directory.
#>

[CmdletBinding()]
param(
    [switch]$Clean
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$toolRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $toolRoot 'src\ble_advertiser.cpp'
$outputDirectory = Join-Path $toolRoot 'out'
$outputExe = Join-Path $outputDirectory 'ble_advertiser.exe'
$outputObject = Join-Path $outputDirectory 'ble_advertiser.obj'
$outputPdb = Join-Path $outputDirectory 'ble_advertiser.pdb'

if ($Clean) {
    foreach ($path in @($outputExe, $outputObject, $outputPdb)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
}

[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$vswhere = $vswhereCandidates |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_) } |
    Select-Object -First 1

if ([string]::IsNullOrWhiteSpace($vswhere)) {
    throw 'vswhere.exe was not found. Install Visual Studio Build Tools with the C++ workload.'
}

$installationPath = & $vswhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
    throw 'Visual Studio Build Tools with the x64 C++ compiler were not found.'
}

$vsDevCmd = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "VsDevCmd.bat was not found below '$installationPath'."
}

function Quote-CmdArgument([string]$Value) {
    return '"' + $Value.Replace('"', '""') + '"'
}

$compileArguments = @(
    'cl.exe',
    '/nologo',
    '/std:c++17',
    '/permissive-',
    '/EHsc',
    '/W4',
    '/O2',
    '/MD',
    '/utf-8',
    '/DWIN32_LEAN_AND_MEAN',
    '/DNOMINMAX',
    '/DUNICODE',
    '/D_UNICODE',
    (Quote-CmdArgument $source),
    ('/Fo:' + (Quote-CmdArgument $outputObject)),
    ('/Fe:' + (Quote-CmdArgument $outputExe)),
    '/link',
    'windowsapp.lib',
    ('/PDB:' + (Quote-CmdArgument $outputPdb))
) -join ' '

$command = 'call ' + (Quote-CmdArgument $vsDevCmd) +
    ' -no_logo -arch=x64 -host_arch=x64 >nul && ' + $compileArguments

$buildExitCode = 1
Push-Location $env:TEMP
try {
    & $env:ComSpec /d /s /c $command
    $buildExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($buildExitCode -ne 0) {
    throw "C++ build failed with exit code $buildExitCode."
}

if (-not (Test-Path -LiteralPath $outputExe)) {
    throw "Build reported success but '$outputExe' does not exist."
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $outputExe
Write-Output ("BLEADV BUILD PASS exe='{0}' sha256={1}" -f $outputExe, $hash.Hash.ToLowerInvariant())
