# SPDX-License-Identifier: Apache-2.0
"""One-boot factory BKTEST enrollment over the existing CP operator channel."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from . import workbench, workbench_profile


class FactoryDiagnosticsError(ValueError):
    pass


_CONSOLE = re.compile(r"COM[1-9][0-9]{0,2}")
_PIN = re.compile(r"[0-9a-f]{64}")

_POWERSHELL = r"""
param([string]$Port, [ValidateSet('enable','status','revoke','power')][string]$Action)
$ErrorActionPreference = 'Stop'
$secret = [Console]::In.ReadToEnd().Trim()
$serial = New-Object System.IO.Ports.SerialPort($Port, 115200,
    [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
$total = [System.Diagnostics.Stopwatch]::StartNew()
function Read-Public([int]$Milliseconds, [bool]$Preamble) {
  $watch = [System.Diagnostics.Stopwatch]::StartNew()
  $line = New-Object System.Text.StringBuilder
  while ($watch.ElapsedMilliseconds -lt $Milliseconds -and $total.ElapsedMilliseconds -lt 30000) {
    try { $value = $serial.ReadByte() } catch [System.TimeoutException] { continue }
    if ($value -lt 0) { continue }
    if ($value -eq 10 -or $value -eq 13) {
      if ($line.Length -eq 0) { continue }
      $text = $line.ToString(); $line.Clear() | Out-Null
      $plain = [regex]::Replace($text, ([string][char]27 + "\[[0-?]*[ -/]*[@-~]"), '')
      if ($Preamble -and ($plain -match '^\s*$' -or
          $plain -match '^nsh>\s*((bkprov diagnostics-(enable|status|revoke))|(bkhealth power))?\s*$' -or
          $plain -match '^(bkprov diagnostics-(enable|status|revoke)|bkhealth power)\s*$')) { continue }
      return $plain
    }
    if ((($value -lt 32) -and ($value -ne 27)) -or $value -gt 126 -or $line.Length -ge 255) {
      throw 'invalid target status'
    }
    [void]$line.Append([char]$value)
  }
  throw 'target status timeout'
}
try {
  if ($Action -eq 'enable' -and $secret -notmatch '^[0-9a-f]{104}$') {
    throw 'invalid private input'
  }
  if ($Action -ne 'enable' -and $secret.Length -ne 0) { throw 'unexpected input' }
  $serial.Open()
  $serial.DiscardInBuffer()
  if ($Action -eq 'power') { $serial.Write("bkhealth power`r") }
  else { $serial.Write("bkprov diagnostics-" + $Action + "`r") }
  if ($Action -eq 'enable') {
    $ready = Read-Public 5000 $true
    if ($ready -ne 'BKPROV DIAGNOSTICS READY bytes=52') { throw 'unexpected target status' }
    $serial.Write($secret + "`n")
    while ($true) {
      $line = Read-Public 20000 $false
      if ($line -match '^BKPROV DIAGNOSTICS FAIL ret=(-?[0-9]+)$') {
        throw ('target-rejected:' + $Matches[1])
      }
      if ($line -match '^BKPROV DIAGNOSTICS PASS flags=([0-9]+) ttl_ms=([0-9]+) certificate_sha256=([0-9a-f]{64})$') {
        [Console]::WriteLine(('ENABLED {0} {1} {2}' -f $Matches[1],$Matches[2],$Matches[3]))
        break
      }
      throw 'unexpected target status'
    }
  } elseif ($Action -eq 'status') {
    while ($true) {
      $line = Read-Public 10000 $true
      if ($line -match '^BKPROV DIAGNOSTICS STATUS flags=([0-9]+) ttl_ms=([0-9]+) ret=(-?[0-9]+)$') {
        [Console]::WriteLine(('STATUS {0} {1} {2}' -f $Matches[1],$Matches[2],$Matches[3]))
        break
      }
      throw 'unexpected target status'
    }
  } elseif ($Action -eq 'revoke') {
    while ($true) {
      $line = Read-Public 10000 $true
      if ($line -eq 'BKPROV DIAGNOSTICS REVOKE PASS') {
        [Console]::WriteLine('REVOKED')
        break
      }
      if ($line -match '^BKPROV DIAGNOSTICS REVOKE FAIL ret=(-?[0-9]+)$') {
        throw ('target-rejected:' + $Matches[1])
      }
      throw 'unexpected target status'
    }
  } else {
    while ($true) {
      $line = Read-Public 10000 $true
      if ($line -match '^BKHEALTH POWER phase=([0-3]) unresolved=([01]) error=(-?[0-9]+)$') {
        $raw = [int]$Matches[1] + (256 * [int]$Matches[2])
        [Console]::WriteLine(('POWER {0} {1} 0' -f $raw,$Matches[3]))
        break
      }
      if ($line -match '^BKHEALTH POWER unavailable=(-?[0-9]+)$') {
        throw ('target-rejected:' + $Matches[1])
      }
      throw 'unexpected target status'
    }
  }
} catch {
  $reason = $_.Exception.Message
  if ($reason -notmatch '^(target-rejected:-?[0-9]+|unexpected target status|invalid target status|target status timeout|invalid private input|unexpected input)$') {
    $reason = 'transport failure'
  }
  [Console]::Error.WriteLine('BKPROV_DIAGNOSTICS_ERROR action=' + $Action + ' reason=' + $reason)
  exit 1
} finally {
  if ($serial.IsOpen) { $serial.Close() }
  $secret = $null
}
"""


def _powershell():
    executable = shutil.which("powershell.exe")
    if executable:
        return executable
    candidate = (
        Path(os.environ.get("SystemRoot", r"C:\Windows"))
        / "System32/WindowsPowerShell/v1.0/powershell.exe"
        if os.name == "nt"
        else Path("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")
    )
    if candidate.is_file():
        return str(candidate)
    raise FactoryDiagnosticsError("PowerShell serial bridge is unavailable")


def _run_console(port, action, secret=""):
    if not _CONSOLE.fullmatch(port or ""):
        raise FactoryDiagnosticsError("Invalid CH340 console port")
    try:
        result = subprocess.run(
            [
                _powershell(),
                "-NoProfile",
                "-NonInteractive",
                "-Command",
                _POWERSHELL,
                "-Port",
                port,
                "-Action",
                action,
            ],
            input=secret.encode("ascii"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=35,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise FactoryDiagnosticsError(
            f"Factory diagnostics {action} transport failed"
        ) from error
    output = result.stdout.decode("ascii", errors="strict").strip()
    if result.returncode != 0:
        match = re.search(
            rb"BKPROV_DIAGNOSTICS_ERROR action=(enable|status|revoke|power) reason=(target-rejected:-?[0-9]+|unexpected target status|invalid target status|target status timeout|invalid private input|unexpected input|transport failure)",
            result.stderr,
        )
        detail = match.group(0).decode("ascii") if match else "unclassified failure"
        raise FactoryDiagnosticsError(
            f"Factory diagnostics {action} failed: {detail}"
        )
    return output


def console_enable(port, record):
    if (
        not isinstance(record, (bytes, bytearray, memoryview))
        or len(record) != 52
        or bytes(record[:4]) != b"BKD1"
        or not any(record[4:20])
        or not any(record[20:52])
    ):
        raise FactoryDiagnosticsError("Invalid volatile diagnostics principal")
    output = _run_console(port, "enable", bytes(record).hex())
    match = re.fullmatch(r"ENABLED ([0-9]+) ([0-9]+) ([0-9a-f]{64})", output)
    if not match:
        raise FactoryDiagnosticsError("Invalid diagnostics enable receipt")
    flags, ttl_ms = (int(match.group(1)), int(match.group(2)))
    pin = match.group(3)
    if flags & 3 != 3 or not 0 < ttl_ms <= 600000 or not _PIN.fullmatch(pin):
        raise FactoryDiagnosticsError("Diagnostics enable receipt is not active")
    return {"flags": flags, "ttl_ms": ttl_ms, "certificate_sha256": pin}


def console_status(port):
    output = _run_console(port, "status")
    match = re.fullmatch(r"STATUS ([0-9]+) ([0-9]+) (-?[0-9]+)", output)
    if not match:
        raise FactoryDiagnosticsError("Invalid diagnostics status receipt")
    return {
        "flags": int(match.group(1)),
        "ttl_ms": int(match.group(2)),
        "result": int(match.group(3)),
    }


def console_revoke(port):
    if _run_console(port, "revoke") != "REVOKED":
        raise FactoryDiagnosticsError("Diagnostics revocation is unconfirmed")
    return {"revoked": True}


def console_power(port):
    output = _run_console(port, "power")
    match = re.fullmatch(r"POWER ([0-9]+) (-?[0-9]+) (-?[0-9]+)", output)
    if not match:
        raise FactoryDiagnosticsError("Invalid power status receipt")
    raw_state, error, result = (int(value) for value in match.groups())
    phase = raw_state & 0xFF
    unresolved = bool(raw_state & 0x100)
    if raw_state & ~0x103 or phase not in (0, 1, 2, 3) or error > 0 or result != 0:
        raise FactoryDiagnosticsError("Invalid power status fields")
    phase_name = ("running", "requested", "pending", "failed")[phase]
    if phase == 0:
        contract_state = "RUNNING"
    elif phase == 1:
        contract_state = "REQUESTED"
    elif phase == 2:
        contract_state = "WAIT_CP_OR_UNKNOWN" if unresolved else "WAIT_CP"
    else:
        contract_state = "FAILED"
    return {
        "raw_state": raw_state,
        "phase": phase_name,
        "contract_state": contract_state,
        "unresolved": unresolved,
        "error": error,
    }


def wait_power(
    port,
    *,
    timeout,
    interval=0.25,
    query=console_power,
    clock=time.monotonic,
    sleep=time.sleep,
):
    if not isinstance(timeout, (int, float)) or timeout <= 0:
        raise FactoryDiagnosticsError("Invalid power wait timeout")
    deadline = clock() + timeout
    timeline = []
    while True:
        status = query(port)
        timeline.append(status)
        if status.get("contract_state") == "FAILED":
            return {"timeline": timeline, "final": status}
        if clock() >= deadline:
            raise FactoryDiagnosticsError(
                "Power transition did not reach an observable terminal state"
            )
        sleep(interval)


def enroll(console_port, native_port, profile, *, timeout=10, random=os.urandom):
    profile = Path(profile)
    if profile.exists() or profile.is_symlink() or not profile.parent.is_dir():
        raise FactoryDiagnosticsError("Factory profile destination is unavailable")
    client = bytearray()
    key = bytearray()
    record = bytearray()
    enabled = False
    try:
        client.extend(random(16))
        key.extend(random(32))
        if len(client) != 16 or len(key) != 32 or not any(client) or not any(key):
            raise FactoryDiagnosticsError("Secure random principal generation failed")
        record.extend(b"BKD1")
        record.extend(client)
        record.extend(key)
        receipt = console_enable(console_port, record)
        enabled = True
        pem = workbench.probe_certificate(
            native_port, receipt["certificate_sha256"], timeout
        )
        workbench_profile.create(
            profile, pem, receipt["certificate_sha256"], key
        )
        return {
            **receipt,
            "profile_saved": True,
            "diagnostics_only": True,
        }
    except Exception as error:
        if enabled:
            try:
                console_revoke(console_port)
            except Exception:
                raise FactoryDiagnosticsError(
                    "Factory enrollment failed and revocation is unconfirmed"
                ) from error
        if isinstance(error, FactoryDiagnosticsError):
            raise
        raise FactoryDiagnosticsError(
            "Factory enrollment failed; no plaintext fallback"
        ) from error
    finally:
        client[:] = bytes(len(client))
        key[:] = bytes(len(key))
        record[:] = bytes(len(record))
