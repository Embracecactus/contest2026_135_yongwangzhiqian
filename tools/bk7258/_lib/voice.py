#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private BKVoice RAM provisioning, consumed only by ``bk7258.py``.

The maintainer CLI supplies Wi-Fi independently.  This module converts the
operator-selected TLS material into a bounded BVC1 RAM record and sends it to
the CP console without printing serial input, credentials, or their paths.
"""

from __future__ import annotations

import argparse
import ipaddress
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
_HOST_RE = re.compile(
    r"^(?=.{1,127}$)(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)*"
    r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"
)
_MAX_TOTAL = 8192
_MAX_PART = 4096
_CHUNK = 64
_BVC1_HEADER = struct.Struct(">4sHHQHH4sHHHH")
_DEVICE_ID_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", re.ASCII)
_CONSOLE_TOKEN_RE = re.compile(r"[A-Za-z0-9._~-]{32,256}", re.ASCII)


def add_arguments(
    commands: argparse._SubParsersAction[argparse.ArgumentParser],
) -> None:
    """Register the sole private RAM-provisioning subcommand for bk7258.py."""

    provision = commands.add_parser(
        "provision", help="send one TLS gateway configuration to the CP RAM"
    )
    provision.add_argument("--console-port", required=True)
    provision.add_argument("--host", required=True)
    provision.add_argument("--peer", required=True)
    provision.add_argument("--port", type=int, default=8765)
    provision.add_argument("--server-ca", type=Path, required=True)
    provision.add_argument("--client-cert", type=Path, required=True)
    provision.add_argument("--client-key", type=Path, required=True)
    provision.add_argument("--openssl", type=Path, default=Path("openssl"))
    pairing = commands.add_parser(
        "pairing",
        help="supply a device TLS identity and write its private owner activation file",
    )
    pairing.add_argument("--console-port", required=True)
    pairing.add_argument("--device-id", required=True)
    pairing.add_argument(
        "--direct-cloud",
        action="store_true",
        help="write the four-field cloud bootstrap without a Gateway route",
    )
    pairing.add_argument("--host")
    pairing.add_argument("--peer")
    pairing.add_argument("--port", type=int)
    pairing.add_argument("--server-ca", type=Path)
    pairing.add_argument("--client-cert", type=Path, required=True)
    pairing.add_argument("--client-key", type=Path, required=True)
    pairing.add_argument("--activation-output", type=Path, required=True)
    pairing.add_argument(
        "--resume",
        action="store_true",
        help="retry the exact identity from the existing activation file",
    )
    pairing.add_argument("--openssl", type=Path, default=Path("openssl"))
    enrollment = commands.add_parser(
        "console-enrollment",
        help="write one private owner-only HTTPS console enrollment file",
    )
    enrollment.add_argument("--device-id", required=True)
    enrollment.add_argument("--https-origin", required=True)
    enrollment.add_argument(
        "--spki-pin",
        action="append",
        required=True,
        help="canonical sha256/<base64-SPKI-digest>; repeat 1-8 times",
    )
    enrollment.add_argument(
        "--token-file",
        type=Path,
        required=True,
        help="existing mode-0600 file containing only the bearer token",
    )
    enrollment.add_argument("--expires-at-ms", required=True)
    enrollment.add_argument("--output", type=Path, required=True)
    enrollment.add_argument(
        "--access-output",
        type=Path,
        help="also write a matching single-grant shaniu.console-access/1 registry",
    )
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


def _bundle(args: argparse.Namespace) -> bytes:
    port = getattr(args, "port", None)
    if port is None:
        port = 8765
    if not _CONSOLE_RE.fullmatch(args.console_port):
        raise VoiceProvisionError("console port is invalid")
    if not isinstance(port, int) or not 1 <= port <= 65535:
        raise VoiceProvisionError("gateway port is invalid")
    if not isinstance(args.host, str) or not _HOST_RE.fullmatch(args.host):
        raise VoiceProvisionError("gateway host is invalid")

    try:
        hostname = args.host.encode("ascii")
        peer = ipaddress.ip_address(args.peer)
    except (UnicodeEncodeError, ValueError) as error:
        raise VoiceProvisionError("gateway peer is invalid") from error
    if (
        not isinstance(peer, ipaddress.IPv4Address)
        or not hostname
        or peer.is_unspecified
        or peer.is_loopback
        or peer.is_multicast
        or int(peer) == 0xFFFFFFFF
    ):
        raise VoiceProvisionError("gateway peer is invalid")

    _regular(args.server_ca, "server CA")
    ca = _read_der(
        args.openssl, ["x509", "-in", str(args.server_ca), "-outform", "DER"], "CA"
    )
    cert, key = _identity_parts(args)
    if len(hostname) > 127:
        raise VoiceProvisionError("gateway host is invalid")
    record = (
        _BVC1_HEADER.pack(
            b"BVC1",
            1,
            0,
            int(time.time()),
            len(hostname),
            port,
            peer.packed,
            len(ca),
            len(cert),
            len(key),
            0,
        )
        + hostname
        + ca
        + cert
        + key
    )
    if len(record) > _MAX_TOTAL:
        raise VoiceProvisionError("configuration exceeds RAM limit")
    return record


_POWERSHELL = r"""
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
  while ($watch.ElapsedMilliseconds -lt $Milliseconds -and $totalWatch.ElapsedMilliseconds -lt 120000) {
    try { $value = $serial.ReadByte() } catch [System.TimeoutException] { continue }
    if ($value -lt 0) { continue }
    if ($value -eq 10 -or $value -eq 13) {
      if ($line.Length -eq 0) { continue }
      $text = $line.ToString(); $line.Clear() | Out-Null
      if ($text -eq $Expected) { return }
      if ($text -match '^BKVOICE PROVISION FAIL ret=(-?[0-9]+)$') { throw ("target-rejected:" + $Matches[1]) }
      $plain = [regex]::Replace($text, ([string][char]27 + "\[[0-?]*[ -/]*[@-~]"), '')
      if ($AllowPreamble -and ($plain -match '^\s*$' -or
          $plain -match '^nsh>\s*(bkvoice provision)?\s*$' -or
          $plain -match '^bkvoice provision\s*$')) { continue }
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
if ($payload.Length -lt 1 -or $payload.Length -gt 8192) { throw 'payload size' }
$totalWatch = [System.Diagnostics.Stopwatch]::StartNew()
$stage = 'open'
try {
  $serial.Open()
  $serial.DiscardInBuffer()
  $stage = 'ready'
  $serial.Write("bkvoice provision`r")
  Wait-Exact 'BKVOICE PROVISION READY' 5000 $true
  $stage = 'begin'
  Send-Line ([string]$payload.Length)
  Wait-Exact 'BKVOICE PROVISION NEXT offset=0' 10000 $false
  $offset = 0
  $stage = 'chunk'
  while ($offset -lt $payload.Length) {
    if ($totalWatch.ElapsedMilliseconds -ge 120000) { throw 'total timeout' }
    $count = [Math]::Min(64, $payload.Length - $offset)
    $chunk = New-Object byte[] $count
    [Array]::Copy($payload, $offset, $chunk, 0, $count)
    Send-Line ([BitConverter]::ToString($chunk).Replace('-', '').ToLowerInvariant())
    $offset += $count
    Wait-Exact ("BKVOICE PROVISION NEXT offset={0}" -f $offset) 10000 $false
    [Array]::Clear($chunk, 0, $chunk.Length)
  }
  $stage = 'commit'
  Wait-Exact ("BKVOICE PROVISION PASS bytes={0}" -f $payload.Length) 35000 $false
} catch {
  $reason = $_.Exception.Message
  if ($reason -notmatch '^(target-rejected:-?[0-9]+|unexpected target status|invalid target status|target status timeout|total timeout)$') {
    $reason = 'transport failure'
  }
  [Console]::Error.WriteLine("BKVOICE_SUPPLY_ERROR stage=" + $stage + " reason=" + $reason)
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


def _read_console_token(path: Path) -> str:
    _regular(path, "console token", private=True)
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError as error:
        raise VoiceProvisionError("console token is unavailable") from error
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or (
            os.name == "posix" and (info.st_mode & 0o777) != 0o600
        ):
            raise VoiceProvisionError("console token permissions are invalid")
        with os.fdopen(descriptor, "rb") as source:
            descriptor = -1
            raw = source.read(257)
        if len(raw) > 256:
            raise VoiceProvisionError("console token is invalid")
        token = raw.decode("ascii")
    except (OSError, UnicodeError) as error:
        raise VoiceProvisionError("console token is unavailable") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if not _CONSOLE_TOKEN_RE.fullmatch(token):
        raise VoiceProvisionError("console token is invalid")
    return token


def _console_origin(value: object) -> str:
    if not isinstance(value, str) or len(value) > 255:
        raise VoiceProvisionError("HTTPS origin is invalid")
    try:
        parsed = urllib.parse.urlsplit(value)
        port = parsed.port
    except ValueError as error:
        raise VoiceProvisionError("HTTPS origin is invalid") from error
    if (
        parsed.scheme != "https"
        or not parsed.netloc
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path not in ("", "/")
        or parsed.query
        or parsed.fragment
    ):
        raise VoiceProvisionError("HTTPS origin is invalid")
    hostname = parsed.hostname
    if hostname is None:
        raise VoiceProvisionError("HTTPS origin is invalid")
    try:
        encoded = hostname.encode("ascii")
        ipaddress.ip_address(hostname)
        host = hostname
    except ValueError:
        if not _HOST_RE.fullmatch(hostname):
            raise VoiceProvisionError("HTTPS origin is invalid")
        host = hostname.lower()
    except UnicodeEncodeError as error:
        raise VoiceProvisionError("HTTPS origin is invalid") from error
    if not encoded:
        raise VoiceProvisionError("HTTPS origin is invalid")
    if ":" in host:
        host = f"[{host}]"
    return f"https://{host}" + (f":{port}" if port is not None else "")


def _console_pins(values: object) -> list[str]:
    if not isinstance(values, list) or not 1 <= len(values) <= 8:
        raise VoiceProvisionError("SPKI pins are invalid")
    pins: list[str] = []
    for value in values:
        if not isinstance(value, str) or not value.startswith("sha256/"):
            raise VoiceProvisionError("SPKI pins are invalid")
        encoded = value[7:]
        try:
            digest = base64.b64decode(encoded, validate=True)
        except (ValueError, UnicodeError) as error:
            raise VoiceProvisionError("SPKI pins are invalid") from error
        if len(digest) != 32 or base64.b64encode(digest).decode("ascii") != encoded:
            raise VoiceProvisionError("SPKI pins are invalid")
        pins.append(value)
    if len(set(pins)) != len(pins):
        raise VoiceProvisionError("SPKI pins are invalid")
    return pins


def _console_enrollment(args: argparse.Namespace) -> dict[str, object]:
    # This command handles a bearer credential. Until a Windows DACL validator
    # is available, POSIX mode checks are the only audited private-file path.
    if os.name != "posix":
        raise VoiceProvisionError(
            "console enrollment requires a POSIX private-file host"
        )
    if not isinstance(args.device_id, str) or not _DEVICE_ID_RE.fullmatch(
        args.device_id
    ):
        raise VoiceProvisionError("device identity is invalid")
    try:
        expires_at_ms = int(args.expires_at_ms)
    except (TypeError, ValueError) as error:
        raise VoiceProvisionError("console enrollment expiry is invalid") from error
    if (
        not int(time.time() * 1000) < expires_at_ms <= (1 << 63) - 1
        or str(expires_at_ms) != args.expires_at_ms
    ):
        raise VoiceProvisionError("console enrollment expiry is invalid")
    origin = _console_origin(args.https_origin)
    pins = _console_pins(args.spki_pin)
    token = _read_console_token(args.token_file)
    access_output = getattr(args, "access_output", None)
    if access_output is not None and os.path.abspath(access_output) == os.path.abspath(
        args.output
    ):
        raise VoiceProvisionError("console output paths must be different")
    access_identity: tuple[int, int] | None = None
    try:
        enrollment = {
            "protocol": "shaniu.console-enrollment/1",
            "device_id": args.device_id,
            "gateway_origin": origin,
            "certificate_pins": pins,
            "access_token": token,
            "expires_at_ms": expires_at_ms,
        }
        if access_output is not None:
            access = {
                "format": "shaniu.console-access/1",
                "grants": [
                    {
                        "token_sha256": hashlib.sha256(
                            token.encode("ascii")
                        ).hexdigest(),
                        "device_id": args.device_id,
                        "write": True,
                        "expires_at_ms": expires_at_ms,
                    }
                ],
            }
            _write_private(
                access_output,
                json.dumps(access, separators=(",", ":"), sort_keys=True).encode(
                    "utf-8"
                ),
            )
            created = access_output.lstat()
            access_identity = (created.st_dev, created.st_ino)
        _write_private(
            args.output,
            json.dumps(enrollment, separators=(",", ":"), sort_keys=True).encode(
                "utf-8"
            ),
        )
    except (OSError, TypeError, ValueError) as error:
        if access_identity is not None:
            try:
                current = access_output.lstat()
                if (
                    stat.S_ISREG(current.st_mode)
                    and (current.st_dev, current.st_ino) == access_identity
                ):
                    access_output.unlink()
            except OSError:
                pass
        raise VoiceProvisionError("console enrollment write failed") from error
    return {
        "status": "console-enrollment-written",
        "device_id": args.device_id,
        "https_origin": origin,
        "spki_pins": len(pins),
        "expires_at_ms": expires_at_ms,
        "access_registry_written": access_output is not None,
    }


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


def _run_console(port: str, payload: bytes) -> None:
    with tempfile.TemporaryDirectory(
        prefix="bk7258-voice-", ignore_cleanup_errors=True
    ) as name:
        root = Path(name)
        os.chmod(root, 0o700)
        payload_path = root / "payload.bin"
        helper_path = root / "provision.ps1"
        _write_private(payload_path, payload)
        _write_private(helper_path, _POWERSHELL.encode("utf-8"))
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
                timeout=125,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise VoiceProvisionError("console provisioning failed") from error
        finally:
            try:
                payload_path.write_bytes(b"\0" * len(payload))
            except OSError:
                pass
        if result.returncode != 0:
            match = re.search(
                rb"BKVOICE_SUPPLY_ERROR stage=(open|ready|begin|chunk|commit) reason=(target-rejected:-?[0-9]+|unexpected target status|invalid target status|target status timeout|total timeout|transport failure)",
                result.stderr,
            )
            detail = (
                match.group(0).decode("ascii")
                if match
                else "unclassified transport failure"
            )
            raise VoiceProvisionError("console provisioning failed: " + detail)


def _pairing(args: argparse.Namespace) -> dict[str, object]:
    if not _CONSOLE_RE.fullmatch(getattr(args, "console_port", "")):
        raise VoiceProvisionError("console port is invalid")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", args.device_id):
        raise VoiceProvisionError("device identity is invalid")
    direct = bool(getattr(args, "direct_cloud", False))
    if direct and any(
        getattr(args, name, None) is not None
        for name in ("host", "peer", "server_ca", "port")
    ):
        raise VoiceProvisionError(
            "direct cloud mode does not accept Gateway parameters"
        )
    if not direct and any(
        getattr(args, name, None) is None for name in ("host", "peer", "server_ca")
    ):
        raise VoiceProvisionError("Gateway parameters are required")
    source = bytearray() if direct else bytearray(_bundle(args))
    payload = bytearray()
    cert = bytearray()
    key = bytearray()
    try:
        if direct:
            cert_bytes, key_bytes = _identity_parts(args)
            cert.extend(cert_bytes)
            key.extend(key_bytes)
            ca = bytearray()
            cert_size, key_size = len(cert), len(key)
        else:
            fields = _BVC1_HEADER.unpack_from(source)
            host_size, ca_size, cert_size, key_size = (
                fields[4],
                fields[7],
                fields[8],
                fields[9],
            )
            start = _BVC1_HEADER.size + host_size
            ca = source[start : start + ca_size]
            cert.extend(source[start + ca_size : start + ca_size + cert_size])
            key.extend(source[start + ca_size + cert_size :])
        proof = bytearray()
        try:
            activation = {
                "protocol": (
                    "provision-bootstrap-v1" if direct else "provision-activation-v1"
                ),
                "device_id": args.device_id,
                "certificate_sha256": hashlib.sha256(cert).hexdigest(),
            }
            if not direct:
                activation.update(
                    {
                        "gateway_host": args.host,
                        "gateway_ipv4": args.peer,
                        "gateway_port": str(getattr(args, "port", None) or 8765),
                        "gateway_ca_der": base64.b64encode(ca).decode("ascii"),
                    }
                )
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
            _run_console(args.console_port, payload)
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
        source[:] = b"\0" * len(source)
        cert[:] = b"\0" * len(cert)
        key[:] = b"\0" * len(key)
        payload[:] = b"\0" * len(payload)


def run(args: argparse.Namespace) -> dict[str, object]:
    """Build and provision one bounded BVC1 RAM record; return public facts."""

    if args.voice_command == "kws":
        try:
            return voice_kws.run(args)
        except voice_kws.KwsError as error:
            raise VoiceProvisionError(str(error)) from error

    if args.voice_command == "pairing":
        return _pairing(args)

    if args.voice_command == "console-enrollment":
        return _console_enrollment(args)

    payload = bytearray(_bundle(args))
    payload_bytes = len(payload)
    try:
        _run_console(args.console_port, payload)
    finally:
        payload[:] = b"\0" * len(payload)
    return {
        "status": "provisioned",
        "bytes": payload_bytes,
        "host": args.host,
        "peer": args.peer,
        "port": args.port,
    }
