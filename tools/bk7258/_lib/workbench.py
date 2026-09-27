# SPDX-License-Identifier: Apache-2.0
"""Bounded PC SDC1 client, shared by the workbench CLI and future local UI.

TLS uses OpenSSL through Python's standard library. The exact out-of-band
certificate is the only trust anchor; its SHA256 is checked before opening a
port and again against the negotiated leaf before sending an independent PC
key. No shell, provisioning claim, OTA, mode switch or automatic replay.
"""
from __future__ import annotations

import hashlib
import hmac
import math
from pathlib import Path
import re
import ssl
import struct
import sys
import time

from . import deploy_usb


class ControlError(ValueError):
    pass


def _context(pem: str, pin: str) -> ssl.SSLContext:
    try:
        if len(pem) > 16384 or not re.fullmatch(r"[0-9a-f]{64}", pin):
            raise ValueError()
        der = ssl.PEM_cert_to_DER_cert(pem)
        if not hmac.compare_digest(hashlib.sha256(der).hexdigest(), pin):
            raise ValueError()
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.check_hostname = False  # Device identity is the exact pinned leaf.
        context.verify_mode = ssl.CERT_REQUIRED
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_verify_locations(cadata=pem)
        return context
    except (ValueError, ssl.SSLError):
        raise ControlError("Invalid pinned device certificate") from None


class ControlClient:
    """Single owner, one request at a time. Channel reads return None for idle,
    b'' for EOF; writes return an exact consumed count, zero for backpressure.
    Each channel call must itself be bounded. close owns the channel, not the
    caller's key. Python/OpenSSL internal copies cannot promise secure erasure.
    """

    def __init__(
        self,
        channel,
        certificate: str,
        pin: str,
        *,
        timeout=10,
        clock=time.monotonic,
        sleep=time.sleep,
    ):
        self.channel = channel
        self.closed = False
        self.authenticated = False
        self._clock, self._sleep = clock, sleep
        self._last = clock()
        self._sequence = 0
        self._pin = pin
        self._timeout = timeout
        try:
            if not math.isfinite(timeout) or not 0 < timeout <= 120:
                raise ControlError("Invalid operation deadline")
            context = _context(certificate, pin)
            self._incoming, self._outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
            self._tls = context.wrap_bio(
                self._incoming, self._outgoing, server_side=False
            )
        except Exception:
            self.close()
            raise

    def _now(self):
        value = self._clock()
        if not math.isfinite(value) or value < self._last:
            raise ControlError("Monotonic clock changed")
        self._last = value
        return value

    def _check(self, deadline):
        if self._now() >= deadline:
            raise ControlError("Control operation timed out; no retry was sent")

    def _flush(self, deadline):
        if self._outgoing.pending > 65536:
            raise ControlError("TLS output budget exceeded")
        while self._outgoing.pending:
            data = self._outgoing.read(4096)
            offset = 0
            while offset < len(data):
                self._check(deadline)
                count = self.channel.write(memoryview(data)[offset:])
                if type(count) is not int or not 0 <= count <= len(data) - offset:
                    raise ControlError("Invalid channel write")
                offset += count
                if not count:
                    self._sleep(0.001)

    def _call(self, function, deadline):
        received = 0
        while True:
            self._check(deadline)
            try:
                result = function()
            except ssl.SSLWantReadError:
                self._flush(deadline)
                data = self.channel.read(4096)
                if data is None:
                    self._sleep(0.001)
                    continue
                if not isinstance(data, bytes) or not 0 < len(data) <= 4096:
                    raise ControlError("Control transport closed or invalid")
                received += len(data)
                if received > 65536 or self._incoming.pending + len(data) > 65536:
                    raise ControlError("TLS input budget exceeded")
                self._incoming.write(data)
                continue
            except ssl.SSLWantWriteError:
                self._flush(deadline)
                self._sleep(0.001)
                continue
            self._flush(deadline)
            self._check(deadline)
            return result

    def start(self, pc_key):
        if self.closed or self.authenticated:
            raise ControlError("Control client cannot be reopened")
        try:
            if len(pc_key) != 32 or not any(pc_key):
                raise ControlError("Invalid independent PC credential")
            deadline = self._now() + self._timeout
            self._call(self._tls.do_handshake, deadline)
            peer = self._tls.getpeercert(binary_form=True)
            if not hmac.compare_digest(hashlib.sha256(peer).hexdigest(), self._pin):
                raise ControlError("Device identity does not match")
            fields = self._exchange(1, pc_key, deadline)
            if fields != (0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0):
                raise ControlError("Invalid authentication response")
            self.authenticated = True
        except Exception:
            self.close()
            raise ControlError(
                "PC authentication failed; no command was replayed"
            ) from None

    def _exchange(self, command, payload, deadline):
        if self._sequence >= 0x7FFFFFFF:
            raise ControlError("Control sequence exhausted")
        frame = bytearray(
            struct.pack(">4I", 0x53444331, command, self._sequence, len(payload))
        )
        frame.extend(payload)
        try:
            offset = 0
            while offset < len(frame):
                count = self._call(
                    lambda: self._tls.write(memoryview(frame)[offset:]), deadline
                )
                if not 0 < count <= len(frame) - offset:
                    raise ControlError("Invalid TLS write")
                offset += count
            response = bytearray()
            while len(response) < 40:
                part = self._call(lambda: self._tls.read(40 - len(response)), deadline)
                if not part:
                    raise ControlError("TLS stream ended before the response")
                response.extend(part)
            magic, reply, sequence, size, error, *fields = struct.unpack(
                ">4Ii5I", response
            )
            if (magic, reply, sequence, size) != (
                0x53444331,
                command | 0x80000000,
                self._sequence,
                24,
            ) or error > 0:
                raise ControlError("Invalid SDC1 response")
            self._sequence += 1
            if error:
                raise ControlError(f"Device rejected the operation ({error})")
            return tuple(fields)
        finally:
            frame[:] = b"\x00" * len(frame)

    def _read(self, command):
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            return self._exchange(command, b"", self._now() + self._timeout)
        except Exception:
            self.close()
            raise ControlError("Control read failed; result is unconfirmed") from None

    def status(self):
        flags, volume, persona, turn, runtime = self._read(2)
        valid = (
            flags & 32767 == flags
            and (not flags & 2048 or bool(flags & 1024))
            and (not flags & 64 or bool(flags & 32))
            and (not flags & 480 or bool(flags & 512))
            and (not flags & 128 or bool(flags & 2) and not flags & 352)
            and (0 <= volume <= 100 if flags & 8 else volume == 0xFFFFFFFF)
            and (0 <= persona <= 4 if flags & 16 else persona == 0xFFFFFFFF)
            and (
                turn < 0x80000000 if flags & 4 else turn == 0xFFFFFFFF and runtime == 0
            )
        )
        if not valid:
            self.close()
            raise ControlError("Invalid device status")
        return dict(
            ready=bool(flags & 1),
            busy=bool(flags & 2),
            volume=volume if flags & 8 else None,
            persona=persona if flags & 16 else None,
            turn=turn if flags & 4 else None,
            runtime_error=(
                struct.unpack(">i", struct.pack(">I", runtime))[0]
                if flags & 4
                else None
            ),
        )

    def info(self):
        return dict(
            zip(
                ("major", "minor", "revision", "build", "security_counter"),
                self._read(9),
            )
        )

    def close(self):
        if not self.closed:
            self.closed = True
            self.authenticated = False
            self._tls = None
            self.channel.close()


class SerialChannel:
    def __init__(self, port, timeout):
        deploy_usb._require_pyserial()
        matches = [
            item for item in deploy_usb.list_ports.comports() if item.device == port
        ]
        if len(matches) != 1 or (matches[0].vid, matches[0].pid) != (
            deploy_usb.NATIVE_VID,
            deploy_usb.NATIVE_PID,
        ):
            raise ControlError("Select the native USB port, not the UART debug port")
        # Reuse the existing bounded Windows native handle path; no SetCommState,
        # mode toggle, console fallback, scan, reset or provisioning command.
        self.port = deploy_usb.open_native_port(
            port, timeout, label="USB control", output=sys.stderr
        )

    def write(self, data):
        return self.port.write(data)

    def read(self, size):
        return self.port.read(size) or None

    def close(self):
        self.port.close()


def add_arguments(parser):
    parser.add_argument("operation", choices=("status", "info"))
    parser.add_argument("--port", required=True)
    parser.add_argument("--certificate", required=True, type=Path)
    parser.add_argument("--certificate-sha256", required=True)
    parser.add_argument("--pc-key-file", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=10)


def _read_file(path, limit):
    if path.is_symlink() or not path.is_file():
        raise ControlError("Credential input must be a regular file")
    with path.open("rb") as stream:
        data = bytearray(stream.read(limit + 1))
    if len(data) > limit:
        data[:] = b"\x00" * len(data)
        raise ControlError("Credential input exceeds its bound")
    return data


def run(args):
    key = _read_file(args.pc_key_file, 32)
    client = None
    try:
        certificate = _read_file(args.certificate, 16384).decode("ascii")
        _context(certificate, args.certificate_sha256)
        if (
            len(key) != 32
            or not any(key)
            or not math.isfinite(args.timeout)
            or not 0 < args.timeout <= 120
        ):
            raise ControlError("Invalid PC credential or deadline")
        client = ControlClient(
            SerialChannel(args.port, args.timeout),
            certificate,
            args.certificate_sha256,
            timeout=args.timeout,
        )
        client.start(key)
        key[:] = b"\x00" * len(key)
        return client.status() if args.operation == "status" else client.info()
    except Exception:
        raise ControlError(
            "USB control failed; no command replay or console fallback"
        ) from None
    finally:
        key[:] = b"\x00" * len(key)
        if client is not None:
            client.close()
