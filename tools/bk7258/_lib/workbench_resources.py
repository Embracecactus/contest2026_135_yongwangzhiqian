# SPDX-License-Identifier: Apache-2.0
"""Bounded RJI1/RJS1 file jobs; receipts contain no authentication material."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import tempfile
import uuid

from .display_assets import MAX_PACK_BYTES, PACK_MAGIC

MAX_FILE = MAX_PACK_BYTES
STATES = (
    "idle",
    "queued",
    "opening",
    "receiving",
    "writing",
    "commit_pending",
    "committing",
    "canceling",
    "done",
    "canceled",
    "failed",
    "unknown",
)


def token(value):
    if (
        not isinstance(value, str)
        or not re.fullmatch(r"[0-9a-f]{32}", value)
        or int(value, 16) == 0
    ):
        raise ValueError("Invalid resource identity")
    return bytes.fromhex(value)


def integer(value, maximum):
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError("Invalid resource integer")
    return value


def encode(operation, epoch, nonce, job_id, argument=0, ttl_ms=0, data=b""):
    op = {"begin": 1, "append": 2, "finish": 3, "cancel": 4}.get(operation)
    epoch, nonce = token(epoch), token(nonce)
    integer(job_id, 2**64 - 1)
    integer(argument, 2**32 - 1)
    integer(ttl_ms, 2**32 - 1)
    if not isinstance(data, bytes) or op is None:
        raise ValueError("Invalid resource operation")
    valid = (
        128 <= argument <= MAX_FILE and ttl_ms > 0 and not data
        if op == 1
        else (
            job_id > 0 and ttl_ms == 0 and 0 < len(data) <= 4096
            if op == 2
            else job_id > 0 and argument == 0 and ttl_ms == 0 and not data
        )
    )
    if not valid:
        raise ValueError("Invalid resource request")
    return (
        b"RJI1"
        + struct.pack(">I", op)
        + epoch
        + nonce
        + struct.pack(">QIIII", job_id, argument, ttl_ms, len(data), 0)
        + data
    )


def decode(data):
    if (
        not isinstance(data, (bytes, bytearray))
        or len(data) != 128
        or data[:4] != b"RJS1"
    ):
        raise ValueError("Invalid resource snapshot")
    (state,) = struct.unpack_from(">I", data, 4)
    epoch, nonce = data[8:24].hex(), data[24:40].hex()
    token(epoch)
    job_id, revision, total, written, error, release, flags, remaining = (
        struct.unpack_from(">QQIIiiII", data, 40)
    )
    name = bytes(data[80:120])
    prefix, separator, suffix = name.partition(b"\0")
    if (
        state >= len(STATES)
        or revision == 0
        or flags & ~6
        or not flags & 4
        or total > MAX_FILE
        or written > total
        or error > 0
        or release > 0
        or any(data[120:])
        or not separator
        or any(suffix)
    ):
        raise ValueError("Invalid resource snapshot fields")
    if state:
        token(nonce)
        if job_id == 0 or total < 128:
            raise ValueError("Invalid resource job")
    elif (
        any(data[24:40])
        or total
        or written
        or error
        or release
        or flags != 4
        or remaining
        or prefix
    ):
        raise ValueError("Invalid idle resource snapshot")
    if prefix and not re.fullmatch(rb"[a-zA-Z0-9_.-]+\.bkep", prefix):
        raise ValueError("Invalid installed resource filename")
    if state == 8 and (not prefix or written != total or error or release or flags & 2):
        raise ValueError("Invalid installed receipt")
    return dict(
        state=STATES[state],
        epoch=epoch,
        nonce=nonce,
        id=job_id,
        revision=revision,
        total=total,
        written=written,
        error=error,
        release_error=release,
        resources_held=bool(flags & 2),
        volatile=True,
        remaining_ms=remaining,
        filename=prefix.decode("ascii") or None,
        installed=state == 8,
    )


def read_receipt(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError("Receipt must be a regular file")
    with path.open("rb") as stream:
        raw = stream.read(4097)
    if len(raw) > 4096:
        raise ValueError("Receipt exceeds its bound")
    value = json.loads(raw)
    keys = {
        "format",
        "device_sha256",
        "epoch",
        "nonce",
        "previous_id",
        "total",
        "sha256",
        "ttl_ms",
    }
    if (
        not isinstance(value, dict)
        or set(value) != keys
        or value["format"] != "shaniu-resource-receipt/1"
    ):
        raise ValueError("Invalid resource receipt")
    for name in ("device_sha256", "sha256"):
        if not isinstance(value[name], str) or not re.fullmatch(
            r"[0-9a-f]{64}", value[name]
        ):
            raise ValueError("Invalid receipt digest")
    encode(
        "begin",
        value["epoch"],
        value["nonce"],
        value["previous_id"],
        value["total"],
        value["ttl_ms"],
    )
    return value


def create_receipt(path, value):
    # Exclusive creation, never overwrite another operation or follow a link.
    # Successful file sync precedes the first device mutation. If writing or
    # syncing fails, the partial receipt stays visible and no BEGIN is sent.
    data = (json.dumps(value, sort_keys=True) + "\n").encode("ascii")
    fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    if os.name != "nt":
        directory = os.open(Path(path).parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)


class Prepared:
    def __init__(self):
        self.stream = None
        self.receipt = None
        self.total = 0
        self.sha256 = None

    def close(self):
        if self.stream is not None:
            self.stream.close()


def prepare(args):
    result = Prepared()
    try:
        operation = args.operation
        receipt_path = getattr(args, "receipt", None)
        if operation != "resource-upload" and getattr(args, "ttl_ms", None) is not None:
            raise ValueError("Only a new upload accepts TTL; resume never renews it")
        if operation in ("resource-resume", "resource-cancel") and receipt_path is None:
            raise ValueError("A saved receipt is required")
        if operation != "resource-upload" and receipt_path is not None:
            result.receipt = read_receipt(receipt_path)
        if operation in ("resource-upload", "resource-resume"):
            if getattr(args, "file", None) is None or receipt_path is None:
                raise ValueError("A file and receipt path are required")
            if operation == "resource-upload":
                encode("begin", "01" * 16, "02" * 16, 0, 128, args.ttl_ms)
                if Path(receipt_path).exists() or Path(receipt_path).is_symlink():
                    raise ValueError(
                        "Receipt already exists; query or explicitly resume"
                    )
            path = Path(args.file)
            if path.is_symlink() or not path.is_file():
                raise ValueError("Pack must be a regular file")
            result.stream = tempfile.TemporaryFile()
            digest = hashlib.sha256()
            fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
            with os.fdopen(fd, "rb") as source:
                before = os.fstat(source.fileno())
                if (
                    not stat.S_ISREG(before.st_mode)
                    or not 128 <= before.st_size <= MAX_FILE
                ):
                    raise ValueError("Invalid pack size")
                while chunk := source.read(65536):
                    result.total += len(chunk)
                    if result.total > MAX_FILE:
                        raise ValueError("Pack exceeds bound")
                    digest.update(chunk)
                    result.stream.write(chunk)
                after = os.fstat(source.fileno())
                if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
                    after.st_size,
                    after.st_mtime_ns,
                    after.st_ctime_ns,
                ) or result.total != before.st_size:
                    raise ValueError("Source changed during preparation")
            result.stream.flush()
            result.stream.seek(0)
            if result.stream.read(len(PACK_MAGIC)) != PACK_MAGIC:
                raise ValueError("Input is not an eye pack; nothing sent")
            result.sha256 = digest.hexdigest()
            if result.receipt and (
                result.receipt["total"] != result.total
                or result.receipt["sha256"] != result.sha256
            ):
                raise ValueError("Pack does not match saved receipt")
        elif getattr(args, "file", None) is not None:
            raise ValueError("This operation does not accept a file")
        return result
    except Exception:
        result.close()
        raise


def correlate(snapshot, receipt, pin):
    if (
        pin != receipt["device_sha256"]
        or snapshot["epoch"] != receipt["epoch"]
        or snapshot["nonce"] != receipt["nonce"]
        or snapshot["id"] <= receipt["previous_id"]
        or snapshot["total"] != receipt["total"]
    ):
        raise ValueError(
            "Resource result unknown: identity, epoch or latest receipt changed"
        )


def perform(client, args, prepared, *, observe=None, cancel_requested=None):
    """Run on the sole client owner. Optional local hooks must be short and
    nonblocking; they must not use the client. Observers receive detached public
    snapshots. Cancellation is checked between bounded protocol requests, never
    in the middle of CONFIG staging. A requested cancel is sent once and only a
    device snapshot establishes CANCELED; lost replies propagate as unknown.
    Existing absolute deadline and saved receipt remain authoritative.
    """
    if observe is not None and not callable(observe):
        raise ValueError("Invalid resource observer")
    if cancel_requested is not None and not callable(cancel_requested):
        raise ValueError("Invalid resource cancellation source")

    def publish(value):
        if observe is not None:
            observe(dict(value))

    def canceled():
        return cancel_requested is not None and cancel_requested()

    deadline = client._now() + client._timeout
    snapshot = client.resource_status(deadline=deadline)
    receipt = prepared.receipt
    if args.operation == "resource-upload":
        if canceled():
            raise ValueError("Resource upload not started; no device mutation")
        if (
            snapshot["state"] not in ("idle", "done", "canceled", "failed")
            or snapshot["resources_held"]
        ):
            raise ValueError("Previous resource job is not safely terminal")
        receipt = dict(
            format="shaniu-resource-receipt/1",
            device_sha256=client._pin,
            epoch=snapshot["epoch"],
            nonce=uuid.uuid4().hex,
            previous_id=snapshot["id"],
            total=prepared.total,
            sha256=prepared.sha256,
            ttl_ms=args.ttl_ms,
        )
        create_receipt(args.receipt, receipt)
        client.resource_request(
            "begin",
            receipt["epoch"],
            receipt["nonce"],
            receipt["previous_id"],
            receipt["total"],
            receipt["ttl_ms"],
            deadline=deadline,
        )
        snapshot = client.resource_status(deadline=deadline)
    if receipt is not None:
        correlate(snapshot, receipt, client._pin)
    if args.operation == "resource-status":
        publish(snapshot)
        return snapshot
    canceling = args.operation == "resource-cancel"
    if canceling:
        if snapshot["state"] == "canceled":
            publish(snapshot)
            return snapshot
        client.resource_request(
            "cancel",
            receipt["epoch"],
            receipt["nonce"],
            snapshot["id"],
            deadline=deadline,
        )
    finished = False
    while True:
        client._check(deadline)
        correlate(snapshot, receipt, client._pin)
        publish(snapshot)
        state = snapshot["state"]
        if canceling and state == "canceled":
            return snapshot
        if not canceling and state == "done":
            return snapshot
        if state in ("idle", "done", "canceled", "failed", "unknown"):
            raise ValueError(
                "Resource job did not complete the requested operation; query saved receipt"
            )
        if not canceling and canceled():
            client.resource_request(
                "cancel",
                receipt["epoch"],
                receipt["nonce"],
                snapshot["id"],
                deadline=deadline,
            )
            canceling = True
        if not canceling and state == "receiving":
            offset = snapshot["written"]
            if offset < prepared.total:
                prepared.stream.seek(offset)
                chunk = prepared.stream.read(min(4096, prepared.total - offset))
                if not chunk:
                    raise ValueError("Prepared pack became unavailable")
                client.resource_request(
                    "append",
                    receipt["epoch"],
                    receipt["nonce"],
                    snapshot["id"],
                    offset,
                    data=chunk,
                    deadline=deadline,
                )
            elif not finished:
                client.resource_request(
                    "finish",
                    receipt["epoch"],
                    receipt["nonce"],
                    snapshot["id"],
                    deadline=deadline,
                )
                finished = True
        client._sleep(0.02)
        snapshot = client.resource_status(deadline=deadline)
