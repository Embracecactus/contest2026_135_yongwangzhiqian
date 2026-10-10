# SPDX-License-Identifier: Apache-2.0
"""Explicit one-frame camera operation on the existing authenticated SDC1 link."""
import base64
import hashlib
import re
import struct
import uuid
from .workbench import ControlError

STATES = ("idle", "pending", "capturing", "ready", "failed", "canceled", "expired")
LIMIT = 102400


def decode(data):
    if len(data) != 80:
        raise ValueError("Invalid camera snapshot length")
    (
        magic,
        phase,
        identity,
        nonce,
        captured,
        size,
        width,
        height,
        fmt,
        error,
        flags,
        fps,
        sequence,
        elapsed,
        remaining,
    ) = struct.unpack(">4sIQ16sQIIIIiIIIII", data)
    if (
        magic != b"CCS1"
        or phase >= len(STATES)
        or size > LIMIT
        or flags & ~3
        or error > 0
        or remaining > 120000
        or fps > 30
        or (
            phase == 3
            and (
                not identity
                or not any(nonce)
                or size < 4
                or not 0 < width <= 4096
                or not 0 < height <= 4096
                or fmt != 0x4745504A
            )
        )
        or (flags & 2 and (phase != 3 or not flags & 1 or not remaining))
    ):
        raise ValueError("Invalid camera snapshot")
    return dict(
        state=STATES[phase],
        id=identity,
        nonce=nonce.hex(),
        captured_ms=captured,
        size=size,
        width=width,
        height=height,
        format="JPEG" if fmt == 0x4745504A else None,
        error=error,
        admitted=bool(flags & 1),
        valid=bool(flags & 2),
        sensor_fps=fps,
        capture_sequence=sequence,
        capture_elapsed_ms=elapsed,
        remaining_ms=remaining,
    )


def encode(action, identity, nonce):
    if (
        action not in (1, 2)
        or type(identity) is not int
        or not 0 <= identity < 2**64
        or not isinstance(nonce, str)
        or re.fullmatch(r"[0-9a-f]{32}", nonce) is None
        or not int(nonce, 16)
    ):
        raise ValueError("Explicit camera request identity required")
    return struct.pack(">4sIQ16s", b"CCQ1", action, identity, bytes.fromhex(nonce))


def _guard(client):
    if client.closed or not client.authenticated:
        raise ControlError("Independent PC camera authorization required")


def status(client):
    _guard(client)
    deadline = client._now() + client._timeout

    def chunk(offset):
        total, *words = client._exchange(
            15, struct.pack(">I", 23 << 16 | offset), deadline
        )
        if total != 80:
            raise ControlError("Unsupported camera snapshot")
        return struct.pack(">4I", *words)

    for _ in range(2):
        data = b"".join(chunk(offset) for offset in range(0, 80, 16))
        if chunk(0) == data[:16] and chunk(16) == data[16:32]:
            return decode(data)
    raise ControlError("Camera changed during read; no capture was replayed")


def request(client, action, identity, nonce):
    record = encode(action, identity, nonce)
    _guard(client)
    deadline = client._now() + client._timeout
    client._exchange(16, struct.pack(">II", 23, 32), deadline)
    client._exchange(17, record, deadline)
    client._exchange(18, b"", deadline)


def cancel(client):
    current = status(client)
    request(client, 2, current["id"], uuid.uuid4().hex)
    result = status(client)
    if result["id"] != current["id"] or result["state"] != "canceled":
        raise ControlError("Camera cancellation is unconfirmed")
    return result


def capture(client, *, preview=False, cancel_requested=None):
    current = status(client)
    if not current["admitted"] or current["state"] in ("pending", "capturing", "ready"):
        raise ControlError("Camera is busy or unavailable")
    nonce = uuid.uuid4().hex
    identity = current["id"] + 1
    frame = bytearray()
    result = None
    released = False
    started = client._now()
    request(client, 1, current["id"], nonce)
    try:
        deadline = started + 8
        while True:
            if cancel_requested is not None and cancel_requested():
                result = dict(state="canceled", id=identity)
                break
            current = status(client)
            if (current["id"], current["nonce"]) != (identity, nonce):
                raise ControlError("Camera request identity changed")
            if current["state"] == "ready" and current["valid"]:
                break
            if current["state"] in ("failed", "canceled", "expired"):
                result = current
                break
            if client._now() >= deadline:
                raise ControlError(
                    "Camera capture did not complete within its deadline"
                )
            client._sleep(0.1)
        if result is None:
            transfer_deadline = client._now() + min(120, current["remaining_ms"] / 1000)
            for offset in range(0, current["size"], 16):
                if cancel_requested is not None and cancel_requested():
                    result = dict(state="canceled", id=identity)
                    break
                selector = struct.pack(
                    ">IQII", 22 << 16, identity, offset, current["size"]
                )
                total, *words = client._exchange(15, selector, transfer_deadline)
                if total != current["size"]:
                    raise ControlError("Camera frame length changed")
                frame.extend(struct.pack(">4I", *words)[: min(16, total - offset)])
            if result is None:
                final = status(client)
                if (
                    (final["id"], final["nonce"], final["size"])
                    != (identity, nonce, len(frame))
                    or not final["valid"]
                    or not frame.startswith(b"\xff\xd8")
                    or not frame.endswith(b"\xff\xd9")
                ):
                    raise ControlError("Camera frame is stale or malformed")
                result = dict(
                    current,
                    transfer_seconds=client._now() - started,
                    sha256=hashlib.sha256(frame).hexdigest(),
                )
                if preview:
                    result["image_base64"] = base64.b64encode(frame).decode("ascii")
    finally:
        frame[:] = bytes(len(frame))
        if not client.closed:
            try:
                request(client, 2, identity, nonce)
                final = status(client)
                released = (
                    final["id"] == identity
                    and final["state"] == "canceled"
                    and not final["valid"]
                )
            except Exception:
                released = False
    if result is None:
        result = dict(state="unknown", id=identity)
    result["release_confirmed"] = released
    if not released:
        result.pop("image_base64", None)
        result["state"] = "unknown"
    return result
