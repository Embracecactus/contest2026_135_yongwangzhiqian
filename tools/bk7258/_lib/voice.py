#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private direct-cloud identity provisioning, consumed by ``bk7258.py``."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import time
import urllib.parse
from pathlib import Path

from . import voice_kws


class VoiceProvisionError(RuntimeError):
    """A deliberately non-sensitive provisioning failure."""


_CONSOLE_RE = re.compile(r"^COM[1-9][0-9]*$")
_MAX_TOTAL = 8192
_MAX_PART = 4096


def add_arguments(
    commands: argparse._SubParsersAction[argparse.ArgumentParser],
) -> None:
    """Register the sole private RAM-provisioning subcommand for bk7258.py."""

    pairing = commands.add_parser(
        "pairing",
        help="supply one device identity over the CP console and write the "
        "owner activation file",
    )
    pairing.add_argument("--console-port", required=True)
    pairing.add_argument("--device-id", required=True)
    pairing.add_argument(
        "--direct-cloud",
        action="store_true",
        required=True,
        help="write the current direct-cloud owner activation and device identity",
    )
    pairing.add_argument("--client-cert", type=Path, required=True)
    pairing.add_argument("--client-key", type=Path, required=True)
    pairing.add_argument("--activation-output", type=Path, required=True)
    pairing.add_argument(
        "--resume",
        action="store_true",
        help="retry the exact identity from the existing activation file",
    )
    pairing.add_argument("--openssl", type=Path, default=Path("openssl"))
    voice_kws.add_arguments(commands)


def _regular(path: Path, label: str, *, private: bool = False) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise VoiceProvisionError(f"{label} is unavailable") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise VoiceProvisionError(f"{label} must be a regular file")
    if private and os.name == "posix" and mode & 0o077:
        raise VoiceProvisionError(f"{label} permissions are too broad")


def _read_der(openssl: Path, arguments: list[str], label: str) -> bytes:
    try:
        result = subprocess.run(
            [str(openssl), *arguments],
            check=False,
            capture_output=True,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise VoiceProvisionError(f"{label} conversion failed") from error
    if result.returncode != 0 or not result.stdout or len(result.stdout) > _MAX_PART:
        raise VoiceProvisionError(f"{label} conversion failed")
    return result.stdout


def _identity_parts(args: argparse.Namespace) -> tuple[bytes, bytes]:
    _regular(args.client_cert, "client certificate")
    _regular(args.client_key, "client key", private=True)
    cert = _read_der(
        args.openssl,
        ["x509", "-in", str(args.client_cert), "-outform", "DER"],
        "certificate",
    )
    key = _read_der(
        args.openssl,
        ["pkcs8", "-topk8", "-nocrypt", "-in", str(args.client_key), "-outform", "DER"],
        "key",
    )
    return cert, key


_POWERSHELL_IDENTITY = r"""
param([string]$Port, [string]$PayloadPath)
$ErrorActionPreference = 'Stop'
$payload = [System.IO.File]::ReadAllBytes($PayloadPath)
$serial = New-Object System.IO.Ports.SerialPort($Port, 115200,
    [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
function Wait-Exact([string]$Expected, [int]$Milliseconds, [bool]$AllowPreamble) {
  $watch = [System.Diagnostics.Stopwatch]::StartNew()
  $line = New-Object System.Text.StringBuilder
  while ($watch.ElapsedMilliseconds -lt $Milliseconds -and $totalWatch.ElapsedMilliseconds -lt 180000) {
    try { $value = $serial.ReadByte() } catch [System.TimeoutException] { continue }
    if ($value -lt 0) { continue }
    if ($value -eq 10 -or $value -eq 13) {
      if ($line.Length -eq 0) { continue }
      $text = $line.ToString(); $line.Clear() | Out-Null
      if ($text -eq $Expected) { return }
      if ($text -match '^BKPROV SUPPLY FAIL ret=(-?[0-9]+)$') { throw ("target-rejected:" + $Matches[1]) }
      $plain = [regex]::Replace($text, ([string][char]27 + "\[[0-?]*[ -/]*[@-~]"), '')
      if ($AllowPreamble -and ($plain -match '^\s*$' -or
          $plain -match '^nsh>\s*(bkprov supply)?\s*$' -or
          $plain -match '^bkprov supply\s*$')) { continue }
      throw 'unexpected target status'
    }
    if ((($value -lt 32) -and ($value -ne 27)) -or $value -gt 126 -or $line.Length -ge 255) {
      throw 'invalid target status'
    }
    [void]$line.Append([char]$value)
  }
  throw 'target status timeout'
}
function Send-Line([string]$Text) {
  $serial.Write($Text + "`n")
}
if ($payload.Length -lt 48 -or $payload.Length -gt 8192) { throw 'payload size' }
$totalWatch = [System.Diagnostics.Stopwatch]::StartNew()
$stage = 'open'
try {
  $serial.Open()
  $serial.DiscardInBuffer()
  $stage = 'ready'
  $serial.Write("bkprov supply`r")
  Wait-Exact 'BKPROV SUPPLY READY' 5000 $true
  $stage = 'begin'
  Send-Line ([string]$payload.Length)
  Wait-Exact 'BKPROV SUPPLY NEXT offset=0' 10000 $false
  $offset = 0
  $stage = 'chunk'
  while ($offset -lt $payload.Length) {
    if ($totalWatch.ElapsedMilliseconds -ge 180000) { throw 'total timeout' }
    $count = [Math]::Min(64, $payload.Length - $offset)
    $chunk = New-Object byte[] $count
    [Array]::Copy($payload, $offset, $chunk, 0, $count)
    Send-Line ([BitConverter]::ToString($chunk).Replace('-', '').ToLowerInvariant())
    $offset += $count
    Wait-Exact ("BKPROV SUPPLY NEXT offset={0}" -f $offset) 10000 $false
    [Array]::Clear($chunk, 0, $chunk.Length)
  }
  $stage = 'commit'
  Wait-Exact ("BKPROV SUPPLY PASS bytes={0}" -f $payload.Length) 90000 $false
} catch {
  $reason = $_.Exception.Message
  if ($reason -notmatch '^(target-rejected:-?[0-9]+|unexpected target status|invalid target status|target status timeout|total timeout)$') {
    $reason = 'transport failure'
  }
  [Console]::Error.WriteLine("BKPROV_SUPPLY_ERROR stage=" + $stage + " reason=" + $reason)
  exit 1
} finally {
  if ($serial.IsOpen) { $serial.Close() }
  [Array]::Clear($payload, 0, $payload.Length)
}
"""


def _write_private(path: Path, data: bytes) -> None:
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(path, 0o600)
    except BaseException:
        try:
            path.unlink()
        except OSError:
            pass
        raise


def _windows_path(path: Path) -> str:
    if os.name == "nt":
        return str(path)
    try:
        result = subprocess.run(
            ["wslpath", "-w", str(path)],
            check=False,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise VoiceProvisionError("Windows path conversion failed") from error
    value = result.stdout.strip()
    if result.returncode != 0 or not value:
        raise VoiceProvisionError("Windows path conversion failed")
    return value


def _powershell() -> str:
    executable = shutil.which("powershell.exe")
    if executable:
        return executable
    fallback = (
        r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"
        if os.name == "nt"
        else "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    )
    if Path(fallback).is_file():
        return fallback
    raise VoiceProvisionError("PowerShell is unavailable")


def _run_console_identity(port: str, payload: bytes) -> None:
    """Send one BPI1 record through the current CP bkprov supply command."""

    with tempfile.TemporaryDirectory(
        prefix="bk7258-identity-", ignore_cleanup_errors=True
    ) as name:
        root = Path(name)
        os.chmod(root, 0o700)
        payload_path = root / "payload.bin"
        helper_path = root / "supply.ps1"
        _write_private(payload_path, payload)
        _write_private(helper_path, _POWERSHELL_IDENTITY.encode("utf-8"))
        try:
            helper_windows = _windows_path(helper_path)
            payload_windows = _windows_path(payload_path)
            result = subprocess.run(
                [
                    _powershell(),
                    "-NoProfile",
                    "-ExecutionPolicy",
                    "Bypass",
                    "-File",
                    helper_windows,
                    "-Port",
                    port,
                    "-PayloadPath",
                    payload_windows,
                ],
                check=False,
                capture_output=True,
                timeout=180,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise VoiceProvisionError("identity supply failed") from error
        finally:
            try:
                payload_path.write_bytes(b"\0" * len(payload))
            except OSError:
                pass
        if result.returncode != 0:
            match = re.search(
                rb"BKPROV_SUPPLY_ERROR stage=(open|ready|begin|chunk|commit) reason=(target-rejected:-?[0-9]+|unexpected target status|invalid target status|target status timeout|total timeout|transport failure)",
                result.stderr,
            )
            detail = (
                match.group(0).decode("ascii")
                if match
                else "unclassified transport failure"
            )
            raise VoiceProvisionError("identity supply failed: " + detail)


def _pairing(args: argparse.Namespace) -> dict[str, object]:
    if not _CONSOLE_RE.fullmatch(getattr(args, "console_port", "")):
        raise VoiceProvisionError("console port is invalid")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", args.device_id):
        raise VoiceProvisionError("device identity is invalid")
    if not args.direct_cloud:
        raise VoiceProvisionError("direct cloud identity supply is required")
    payload = bytearray()
    cert = bytearray()
    key = bytearray()
    try:
        cert_bytes, key_bytes = _identity_parts(args)
        cert.extend(cert_bytes)
        key.extend(key_bytes)
        cert_size, key_size = len(cert), len(key)
        proof = bytearray()
        try:
            activation = {
                "protocol": "provision-bootstrap-v1",
                "device_id": args.device_id,
                "certificate_sha256": hashlib.sha256(cert).hexdigest(),
            }
            if getattr(args, "resume", False):
                _regular(args.activation_output, "owner activation", private=True)
                with args.activation_output.open("rb") as stored:
                    raw = stored.read(_MAX_TOTAL + 1)
                if len(raw) > _MAX_TOTAL:
                    raise VoiceProvisionError("owner activation is too large")
                saved = json.loads(raw)
                if (
                    not isinstance(saved, dict)
                    or set(saved) != set(activation) | {"possession_secret"}
                    or any(saved[name] != value for name, value in activation.items())
                    or not isinstance(saved["possession_secret"], str)
                ):
                    raise VoiceProvisionError(
                        "owner activation does not match this identity and gateway"
                    )
                proof.extend(
                    base64.b64decode(saved["possession_secret"], validate=True)
                )
                if len(proof) != 32:
                    raise VoiceProvisionError("owner activation proof is invalid")
            else:
                proof.extend(os.urandom(32))
                activation["possession_secret"] = base64.b64encode(proof).decode(
                    "ascii"
                )
            payload = bytearray(
                struct.pack(">4sHHHHI", b"BPI1", 1, 0, cert_size, key_size, 0)
            )
            payload.extend(proof)
            payload.extend(cert)
            payload.extend(key)
            if len(payload) > _MAX_TOTAL:
                raise VoiceProvisionError("device identity is too large")
            # Preserve the original owner copy after uncertain serial results.
            # Resume validates it and sends the same BPI1 without rewriting it.
            if not getattr(args, "resume", False):
                descriptor = os.open(
                    args.activation_output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600
                )
                with os.fdopen(descriptor, "w", encoding="utf-8") as output:
                    json.dump(activation, output, separators=(",", ":"))
                    output.flush()
                    os.fsync(output.fileno())
            _run_console_identity(args.console_port, payload)
        finally:
            proof[:] = b"\0" * len(proof)
            key[:] = b"\0" * len(key)
        return {
            "status": "identity-supplied",
            "device_id": args.device_id,
            "bytes": len(payload),
        }
    except (OSError, ValueError, struct.error) as error:
        raise VoiceProvisionError(
            "identity supply failed; preserve any owner activation file for reconciliation"
        ) from error
    finally:
        cert[:] = b"\0" * len(cert)
        key[:] = b"\0" * len(key)
        payload[:] = b"\0" * len(payload)


def run(args: argparse.Namespace) -> dict[str, object]:
    """Supply one bounded BPI1 owner identity; return public facts."""

    if args.voice_command == "kws":
        try:
            return voice_kws.run(args)
        except voice_kws.KwsError as error:
            raise VoiceProvisionError(str(error)) from error

    if args.voice_command == "pairing":
        return _pairing(args)
    raise VoiceProvisionError("unsupported voice command")
