# SPDX-License-Identifier: Apache-2.0
"""Bounded PC SDC1 client, shared by the workbench CLI and future local UI.

TLS uses OpenSSL through Python's standard library. The exact out-of-band
certificate is the only trust anchor; its SHA256 is checked before opening a
port and again against the negotiated leaf before sending an independent PC
key. No shell, provisioning claim, OTA, mode switch or automatic replay.
"""
from __future__ import annotations

from contextlib import contextmanager
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

    def engineering_status(self):
        from . import hil_test

        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout

            def chunk(offset):
                total, *words = self._exchange(
                    15, struct.pack(">I", 19 << 16 | offset), deadline
                )
                if total != hil_test.STATUS_SIZE:
                    raise ControlError("Invalid engineering snapshot size")
                return struct.pack(">4I", *words)

            data = b"".join(chunk(offset) for offset in range(0, 64, 16))
            if chunk(0) != data[:16]:
                raise ControlError("Engineering snapshot changed during read")
            return hil_test.decode_status(data)
        except Exception:
            self.close()
            raise ControlError(
                "Engineering status unconfirmed; no command was replayed"
            ) from None

    def engineering_command(self, record):
        from . import hil_test

        hil_test.decode_command(record)
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout
            self._exchange(16, struct.pack(">II", 19, len(record)), deadline)
            self._exchange(17, record, deadline)
            self._exchange(18, b"", deadline)
            return dict(accepted=True, completion_verified=False)
        except Exception:
            self.close()
            raise ControlError(
                "Engineering command unconfirmed; query board state before retry"
            ) from None

    def task_event(self, task_id, sequence, state, ttl_ms, progress=None):
        from . import workbench_tasks

        record = workbench_tasks.encode(task_id, sequence, state, ttl_ms, progress)
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout
            self._exchange(16, struct.pack(">II", 15, len(record)), deadline)
            for offset in range(0, len(record), 32):
                self._exchange(17, record[offset : offset + 32], deadline)
            self._exchange(18, b"", deadline)
            return dict(accepted=True, event_sequence=sequence, feedback_verified=False)
        except Exception:
            self.close()
            raise ControlError(
                "Task event unconfirmed; query before any manual retry"
            ) from None

    def task_status(self):
        from . import workbench_tasks

        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout

            def chunk(offset):
                total, *words = self._exchange(
                    15, struct.pack(">I", 15 << 16 | offset), deadline
                )
                if total != 48:
                    raise ControlError("Invalid task snapshot size")
                return struct.pack(">4I", *words)

            data = b"".join(chunk(offset) for offset in (0, 16, 32))
            # Check the event identity again; TTL may count down during reads.
            if chunk(0) + chunk(16) != data[:32]:
                raise ControlError("Task changed during read; no coherent snapshot")
            return workbench_tasks.decode(data)
        except Exception:
            self.close()
            raise ControlError("Task read failed; result is unconfirmed") from None

    def trial_request(
        self,
        action,
        expected_id,
        operation_id,
        expression=None,
        ttl_ms=None,
        *,
        pack_filename=None,
    ):
        from . import workbench_trial

        record = workbench_trial.encode(
            action,
            expected_id,
            operation_id,
            expression,
            ttl_ms,
            pack_filename=pack_filename,
        )
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout
            self._exchange(16, struct.pack(">II", 11, len(record)), deadline)
            for offset in range(0, len(record), 32):
                self._exchange(17, record[offset : offset + 32], deadline)
            self._exchange(18, b"", deadline)
            return dict(
                accepted=True, operation_id=operation_id, completion_verified=False
            )
        except Exception:
            self.close()
            raise ControlError(
                "Trial request unconfirmed; query before manual retry"
            ) from None

    def trial_status(self):
        from . import workbench_trial

        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout

            def chunk(offset):
                total, *words = self._exchange(
                    15, struct.pack(">I", 11 << 16 | offset), deadline
                )
                if total != 32:
                    raise ControlError("Invalid trial snapshot size")
                return struct.pack(">4I", *words)

            header = chunk(0)
            data = header + chunk(16)
            if chunk(0) != header:
                raise ControlError("Trial changed during snapshot read")
            return workbench_trial.decode(data)
        except Exception:
            self.close()
            raise ControlError("Trial read unconfirmed; no request replayed") from None

    def selection_request(
        self, action, epoch, nonce, expected_id, revision=None, filename=None
    ):
        from . import workbench_selection

        record = workbench_selection.encode(
            action, epoch, nonce, expected_id, revision, filename
        )
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout
            self._exchange(16, struct.pack(">II", 17, len(record)), deadline)
            for offset in range(0, len(record), 32):
                self._exchange(17, record[offset : offset + 32], deadline)
            self._exchange(18, b"", deadline)
            return dict(
                accepted=True,
                action=action,
                epoch=epoch,
                operation_nonce=nonce,
                expected_job_id=expected_id,
                completion_verified=False,
            )
        except Exception:
            self.close()
            raise ControlError(
                "Default request unconfirmed; query its epoch/nonce before manual retry"
            ) from None

    def selection_status(
        self, *, expected_epoch=None, expected_nonce=None, expected_id=None
    ):
        from . import workbench_selection
        import uuid

        workbench_selection.validate_query(expected_epoch, expected_nonce, expected_id)
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout
            query = uuid.uuid4().bytes

            def chunk(offset):
                total, *words = self._exchange(
                    15, struct.pack(">I", 17 << 16 | offset) + query, deadline
                )
                if total != 128:
                    raise ControlError("Invalid selection snapshot size")
                return struct.pack(">4I", *words)

            data = b"".join(chunk(offset) for offset in range(0, 128, 16))
            if chunk(112) != data[112:]:
                raise ControlError("Selection snapshot changed during read")
            result = workbench_selection.decode(data)
            if (
                (expected_epoch is not None and result["epoch"] != expected_epoch)
                or (
                    expected_nonce is not None
                    and result["operation_nonce"] != expected_nonce
                )
                or (expected_id is not None and result["id"] != expected_id)
            ):
                raise ControlError(
                    "Selection receipt is stale or belongs to another operation"
                )
            return result
        except Exception:
            self.close()
            raise ControlError(
                "Default read unconfirmed; no request replayed"
            ) from None

    def catalog_request(self, action, epoch, nonce, expected_id, after=None):
        from . import workbench_catalog

        record = workbench_catalog.encode(action, epoch, nonce, expected_id, after)
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout
            self._exchange(16, struct.pack(">II", 18, len(record)), deadline)
            for offset in range(0, len(record), 32):
                self._exchange(17, record[offset : offset + 32], deadline)
            self._exchange(18, b"", deadline)
            return dict(
                accepted=True,
                completion_verified=False,
                action=action,
                epoch=epoch,
                operation_nonce=nonce,
                expected_job_id=expected_id,
            )
        except Exception:
            self.close()
            raise ControlError(
                "Catalog request unconfirmed; query its epoch/nonce before manual retry"
            ) from None

    def catalog_status(
        self, *, expected_epoch=None, expected_nonce=None, expected_id=None
    ):
        from . import workbench_catalog
        import uuid

        workbench_catalog.validate_query(expected_epoch, expected_nonce, expected_id)
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._now() + self._timeout
            query = uuid.uuid4().bytes

            def chunk(offset):
                total, *words = self._exchange(
                    15, struct.pack(">I", 18 << 16 | offset) + query, deadline
                )
                if total != 608:
                    raise ControlError("Invalid catalog snapshot size")
                return struct.pack(">4I", *words)

            data = b"".join(chunk(offset) for offset in range(0, 608, 16))
            if chunk(48) != data[48:64]:
                raise ControlError("Catalog snapshot changed during read")
            result = workbench_catalog.decode(data)
            if (
                (expected_epoch is not None and result["epoch"] != expected_epoch)
                or (
                    expected_nonce is not None
                    and result["operation_nonce"] != expected_nonce
                )
                or (expected_id is not None and result["id"] != expected_id)
            ):
                raise ControlError(
                    "Catalog receipt is stale or belongs to another operation"
                )
            return result
        except Exception:
            self.close()
            raise ControlError(
                "Catalog read unconfirmed; no request replayed"
            ) from None

    def _resource_deadline(self, deadline):
        now = self._now()
        if deadline is not None and (
            type(deadline) not in (int, float)
            or not math.isfinite(deadline)
            or deadline <= now
        ):
            raise ControlError("Invalid resource deadline")
        return (
            min(deadline, now + self._timeout)
            if deadline is not None
            else now + self._timeout
        )

    def resource_status(self, *, deadline=None):
        from . import workbench_resources
        import uuid

        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._resource_deadline(deadline)
            query = uuid.uuid4().bytes
            data = bytearray()
            for offset in range(0, 128, 16):
                total, *words = self._exchange(
                    15, struct.pack(">I", 16 << 16 | offset) + query, deadline
                )
                if total != 128:
                    raise ControlError("Invalid resource snapshot size")
                data.extend(struct.pack(">4I", *words))
            return workbench_resources.decode(data)
        except Exception:
            self.close()
            raise ControlError(
                "Resource read unconfirmed; no request replayed"
            ) from None

    def resource_request(
        self,
        operation,
        epoch,
        nonce,
        job_id,
        argument=0,
        ttl_ms=0,
        data=b"",
        *,
        deadline=None,
    ):
        from . import workbench_resources

        record = workbench_resources.encode(
            operation, epoch, nonce, job_id, argument, ttl_ms, data
        )
        if self.closed or not self.authenticated:
            raise ControlError("PC authentication is required")
        try:
            deadline = self._resource_deadline(deadline)
            self._exchange(16, struct.pack(">II", 16, len(record)), deadline)
            for offset in range(0, len(record), 32):
                self._exchange(17, record[offset : offset + 32], deadline)
            self._exchange(18, b"", deadline)
            return dict(accepted=True, completion_verified=False)
        except Exception:
            self.close()
            raise ControlError(
                "Resource request unconfirmed; query saved receipt before manual resume"
            ) from None

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


def add_connection_arguments(parser, *, port_required=False):
    parser.add_argument("--port", required=port_required)
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--certificate", type=Path)
    parser.add_argument("--certificate-sha256")
    parser.add_argument("--pc-key-file", type=Path)
    parser.add_argument("--timeout", type=float, default=10)


def add_arguments(parser):
    parser.add_argument(
        "operation",
        choices=(
            "status",
            "info",
            "serve",
            "save-profile",
            "pair-start",
            "pair-finish",
            "task-event",
            "task-status",
            "trial-start",
            "trial-status",
            "trial-cancel",
            "catalog-status",
            "catalog-page",
            "catalog-cancel",
            "catalog-recover",
            "default-status",
            "default-refresh",
            "default-set",
            "default-cancel",
            "default-recover",
            "resource-status",
            "resource-upload",
            "resource-resume",
            "resource-cancel",
        ),
    )
    parser.add_argument(
        "--task-id", help="32 hex digits; never reuse within an authorization binding"
    )
    parser.add_argument(
        "--event-sequence",
        type=int,
        help="Monotonic across tasks for this authorization",
    )
    parser.add_argument(
        "--state", choices=("start", "progress", "success", "failure", "canceled")
    )
    parser.add_argument(
        "--ttl-ms", type=int, help="Remaining receiver TTL; subtract caller queue age"
    )
    parser.add_argument(
        "--progress", type=int, help="0..100; omit for unknown, start requires 0"
    )
    parser.add_argument(
        "--file", type=Path, help="Public eye pack; never activated by upload"
    )
    parser.add_argument(
        "--receipt", type=Path, help="Exclusive local public job receipt"
    )
    parser.add_argument(
        "--expected-trial-id", type=int, help="Exact latest ID read from this device"
    )
    parser.add_argument(
        "--operation-id",
        help="Nonzero 16 lowercase hex digits; retain across a manual exact retry",
    )
    parser.add_argument(
        "--expression",
        choices=(
            "neutral",
            "happy",
            "shy",
            "sad",
            "surprised",
            "thinking",
            "listening",
            "speaking",
            "sleepy",
        ),
    )
    parser.add_argument(
        "--pack-filename",
        help="Installed .bkep name for trial-start or default-set; never a local path",
    )
    parser.add_argument(
        "--catalog-after", help="Last canonical filename from preceding catalog page"
    )
    parser.add_argument(
        "--selection-epoch", help="32 lowercase hex scope from default-status"
    )
    parser.add_argument(
        "--selection-nonce",
        help="Explicit nonzero 32-hex operation ID; retain for status/retry",
    )
    parser.add_argument(
        "--expected-selection-id",
        type=int,
        help="Latest selection job ID in this scope",
    )
    parser.add_argument(
        "--expected-default-revision",
        type=int,
        help="Durable version from completed explicit refresh",
    )
    parser.add_argument(
        "--workbench-dir",
        type=Path,
        help="New local directory for browser resource receipts",
    )
    parser.add_argument(
        "--listen-port",
        type=int,
        default=0,
        help="Loopback port; 0 chooses a free port",
    )
    add_connection_arguments(parser)
    parser.add_argument("--request", type=Path)
    parser.add_argument("--pending", type=Path)
    parser.add_argument("--response", type=Path)
    parser.add_argument("--confirm-device-sha256")
    parser.add_argument(
        "--allow", nargs="+", choices=("resources", "scenes", "tasks", "diagnostics")
    )


def _read_file(path, limit):
    if path.is_symlink() or not path.is_file():
        raise ControlError("Credential input must be a regular file")
    with path.open("rb") as stream:
        data = bytearray(stream.read(limit + 1))
    if len(data) > limit:
        data[:] = b"\x00" * len(data)
        raise ControlError("Credential input exceeds its bound")
    return data


@contextmanager
def _credentials(args):
    from . import workbench_profile

    profile_path = getattr(args, "profile", None)
    legacy = (args.certificate, args.certificate_sha256, args.pc_key_file)
    if (
        profile_path is not None
        and getattr(args, "operation", None) != "save-profile"
    ):
        if any(value is not None for value in legacy):
            raise ControlError(
                "A profile cannot be combined with plaintext credentials"
            )
        with workbench_profile.use(profile_path) as material:
            yield material
        return
    if not all(value is not None for value in legacy):
        raise ControlError("A profile or complete credential inputs are required")
    key = _read_file(args.pc_key_file, 32)
    try:
        certificate = _read_file(args.certificate, 16384).decode("ascii")
        _context(certificate, args.certificate_sha256)
        if len(key) != 32 or not any(key):
            raise ControlError("Invalid independent PC credential")
        yield certificate, args.certificate_sha256, key
    finally:
        key[:] = bytes(len(key))


@contextmanager
def authorized_client(args):
    client = None
    try:
        with _credentials(args) as (certificate, pin, key):
            if (
                not args.port
                or not math.isfinite(args.timeout)
                or not 0 < args.timeout <= 120
            ):
                raise ControlError("Invalid port or deadline")
            client = ControlClient(
                SerialChannel(args.port, args.timeout),
                certificate,
                pin,
                timeout=args.timeout,
            )
            client.start(key)
            key[:] = bytes(len(key))
            yield client
    finally:
        if client is not None:
            client.close()


def run(args, *, observe=None, cancel_requested=None):
    if args.operation == "serve":
        from . import workbench_web

        return workbench_web.serve(args)
    resource_plan = None
    if args.operation.startswith("catalog-"):
        from . import workbench_catalog

        workbench_catalog.prepare(args)
    elif getattr(args, "catalog_after", None) is not None:
        raise ControlError("Catalog cursor cannot be used by another operation")
    if args.operation.startswith("default-"):
        from . import workbench_selection

        workbench_selection.prepare(args)
    if args.operation.startswith("trial-"):
        from . import workbench_trial

        workbench_trial.prepare(args)
    if args.operation.startswith("resource-"):
        from . import workbench_resources

        resource_plan = workbench_resources.prepare(args)
    if args.operation == "task-event":
        from . import workbench_tasks

        # Validate before opening a device or borrowing any private material.
        workbench_tasks.encode(
            args.task_id, args.event_sequence, args.state, args.ttl_ms, args.progress
        )
    if args.operation in ("pair-start", "pair-finish"):
        from . import workbench_pairing

        return workbench_pairing.run(args)
    from . import workbench_profile

    try:
        if args.operation == "save-profile":
            with _credentials(args) as (certificate, pin, key):
                if getattr(args, "profile", None) is None or args.port is not None:
                    raise ControlError(
                        "Offline profile import requires only a profile destination"
                    )
                workbench_profile.create(args.profile, certificate, pin, key)
                return dict(profile_saved=True, device_authorization_verified=False)
        with authorized_client(args) as client:
            if args.operation.startswith("catalog-"):
                return workbench_catalog.perform(client, args)
            if args.operation.startswith("default-"):
                return workbench_selection.perform(client, args)
            if args.operation == "trial-status":
                return client.trial_status()
            if args.operation in ("trial-start", "trial-cancel"):
                return client.trial_request(
                    args.operation.removeprefix("trial-"),
                    args.expected_trial_id,
                    args.operation_id,
                    args.expression,
                    args.ttl_ms,
                    pack_filename=args.pack_filename,
                )
            if resource_plan is not None:
                return workbench_resources.perform(
                    client,
                    args,
                    resource_plan,
                    observe=observe,
                    cancel_requested=cancel_requested,
                )
            if args.operation == "task-event":
                return client.task_event(
                    args.task_id,
                    args.event_sequence,
                    args.state,
                    args.ttl_ms,
                    args.progress,
                )
            if args.operation == "task-status":
                return client.task_status()
            if args.operation == "status":
                return client.status()
            if args.operation == "info":
                return client.info()
            raise ControlError("Unknown workbench operation")
    except Exception:
        raise ControlError(
            "Workbench operation failed; no credential fallback or command replay"
        ) from None
    finally:
        if resource_plan is not None:
            resource_plan.close()
