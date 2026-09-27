# SPDX-License-Identifier: Apache-2.0
"""Current-user Windows DPAPI profiles; no plaintext or machine-wide fallback.

The fixed PowerShell bridge also supports the existing WSL development host.
Only stdin/stdout carry sensitive buffers; arguments and diagnostics never do.
The OS remains the protection boundary, not another user's file permissions.
"""
import base64
from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile


class ProfileError(ValueError):
    pass


_LIMIT = 32768
_ENTROPY = "shaniu-pc-profile-v1"


def _protect(data, *, decrypt=False):
    if not 0 < len(data) <= _LIMIT:
        raise ProfileError("Invalid protected material size")
    executable = shutil.which("powershell.exe")
    if not executable:
        candidate = (
            (
                Path(os.environ.get("SystemRoot", r"C:\Windows"))
                / "System32/WindowsPowerShell/v1.0/powershell.exe"
            )
            if os.name == "nt"
            else Path("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")
        )
        if candidate.is_file():
            executable = str(candidate)
    if not executable:
        raise ProfileError("Current-user Windows protection is unavailable")
    action = "Unprotect" if decrypt else "Protect"
    script = (
        "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.Security;"
        "$inputBytes=$null; $outputBytes=$null; try {"
        "$inputBytes=[Convert]::FromBase64String([Console]::In.ReadToEnd());"
        f"$entropy=[Text.Encoding]::UTF8.GetBytes('{_ENTROPY}');"
        f"$outputBytes=[Security.Cryptography.ProtectedData]::{action}("
        "$inputBytes,$entropy,[Security.Cryptography.DataProtectionScope]::CurrentUser);"
        "[Console]::Write([Convert]::ToBase64String($outputBytes));"
        "} catch { exit 1 } finally {"
        "if ($inputBytes) {[Array]::Clear($inputBytes,0,$inputBytes.Length)};"
        "if ($outputBytes) {[Array]::Clear($outputBytes,0,$outputBytes.Length)} }"
    )
    try:
        result = subprocess.run(
            [executable, "-NoProfile", "-NonInteractive", "-Command", script],
            input=base64.b64encode(data),
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=10,
            check=False,
        )
        if result.returncode or len(result.stdout) > 4 * ((_LIMIT + 2) // 3):
            raise ValueError()
        decoded = bytearray(base64.b64decode(result.stdout, validate=True))
        if not 0 < len(decoded) <= _LIMIT:
            decoded[:] = bytes(len(decoded))
            raise ValueError()
        return decoded
    except Exception:
        raise ProfileError(
            "Current-user protection failed; no plaintext fallback"
        ) from None


def _validate(pem, pin, key):
    from .workbench import _context

    if len(key) != 32 or not any(key):
        raise ValueError()
    _context(pem, pin)
    encoded = pem.encode("ascii")
    if len(encoded) > 16384:
        raise ValueError()
    return encoded


def _read(path, *, magic=b"SPC1"):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError()
    descriptor = os.open(
        path,
        os.O_RDONLY
        | getattr(os, "O_NOFOLLOW", 0)
        | getattr(os, "O_BINARY", 0)
        | getattr(os, "O_NONBLOCK", 0),
    )
    with os.fdopen(descriptor, "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError()
        data = stream.read(_LIMIT + 9)
    if (
        not 8 < len(data) <= _LIMIT + 8
        or data[:4] != magic
        or struct.unpack(">I", data[4:8])[0] != len(data) - 8
    ):
        raise ValueError()
    return data[8:]


@contextmanager
def use(path):
    plain = None
    key = None
    try:
        try:
            plain = _protect(_read(path), decrypt=True)
            if len(plain) < 72 or plain[:4] != b"PCI1":
                raise ValueError()
            length = struct.unpack(">I", plain[68:72])[0]
            if len(plain) != 72 + length or length > 16384:
                raise ValueError()
            pin = plain[4:36].hex()
            key = plain[36:68]
            pem = plain[72:].decode("ascii")
            _validate(pem, pin, key)
        except Exception:
            raise ProfileError(
                "Profile unavailable or invalid; no credential fallback"
            ) from None
        yield pem, pin, key
    finally:
        if key is not None:
            key[:] = bytes(len(key))
        if plain is not None:
            plain[:] = bytes(len(plain))


def _publish_new(path, wire):
    """Shared exclusive file publication. Callers seal all private bytes first."""
    temporary = None
    try:
        descriptor, temporary = tempfile.mkstemp(
            prefix=".shaniu-pc-", dir=Path(path).parent
        )
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(wire)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, path)
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)


def create(path, pem, pin, key):
    """Publish a new encrypted file exclusively, then verify decrypted readback.
    Never replace an existing profile. A post-publication failure is uncertain;
    its encrypted file remains for explicit recovery, not automatic overwriting.
    This does not validate the device's grant or promise power-loss durability.
    """
    path = Path(path)
    plain = bytearray()
    try:
        certificate = _validate(pem, pin, key)
        plain.extend(b"PCI1" + bytes.fromhex(pin))
        plain.extend(key)
        plain.extend(struct.pack(">I", len(certificate)))
        plain.extend(certificate)
        sealed = _protect(plain)
        if not 0 < len(sealed) <= _LIMIT:
            raise ValueError()
        _publish_new(path, b"SPC1" + struct.pack(">I", len(sealed)) + sealed)
        with use(path) as (saved_pem, saved_pin, saved_key):
            if (saved_pem, saved_pin) != (pem, pin) or saved_key != key:
                raise ValueError()
    except Exception:
        raise ProfileError(
            "Profile save not confirmed; existing files were not replaced"
        ) from None
    finally:
        plain[:] = bytes(len(plain))
