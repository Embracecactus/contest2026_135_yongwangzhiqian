# SPDX-License-Identifier: Apache-2.0
"""Public PTE1/PTS1 task wire format; no device or process side effects."""
import re
import struct

STATES = {"start": 1, "progress": 2, "success": 3, "failure": 4, "canceled": 5}


def encode(task_id, sequence, state, ttl_ms, progress):
    if (
        not isinstance(task_id, str)
        or re.fullmatch(r"[0-9a-fA-F]{32}", task_id) is None
        or not int(task_id, 16)
        or type(sequence) is not int
        or not 0 < sequence < 2**64
        or state not in STATES
        or type(ttl_ms) is not int
        or not 0 < ttl_ms < 2**32
        or (
            progress is not None
            and (type(progress) is not int or not 0 <= progress <= 100)
        )
        or (state == "start" and progress != 0)
    ):
        raise ValueError(
            "Invalid task event; explicit ID, sequence, state and remaining TTL required"
        )
    return struct.pack(
        ">4sI16sQII",
        b"PTE1",
        STATES[state],
        bytes.fromhex(task_id),
        sequence,
        ttl_ms,
        0xFFFFFFFF if progress is None else progress,
    )


def decode(data):
    if len(data) != 48:
        raise ValueError("Invalid task snapshot length")
    magic, state, task_id, sequence, remaining, flags, progress = struct.unpack(
        ">4sI16sQQII", data
    )
    empty = state == 0
    if (
        magic != b"PTS1"
        or state > 5
        or flags & ~7
        or (progress > 100 and progress != 0xFFFFFFFF)
        or remaining > 0xFFFFFFFF
        or (empty and (any(task_id) or sequence or remaining or flags & 6 or progress))
        or (not empty and (not any(task_id) or not sequence))
        or (state == 1 and progress != 0)
        or (not empty and bool(flags & 2) != (remaining == 0))
        or bool(flags & 4) != (state >= 3 and remaining > 0)
    ):
        raise ValueError("Invalid task snapshot")
    return dict(
        state=(
            "none"
            if empty
            else next(name for name, number in STATES.items() if number == state)
        ),
        task_id=task_id.hex() if not empty else None,
        event_sequence=sequence,
        remaining_ms=remaining,
        admitted=bool(flags & 1),
        expired=bool(flags & 2),
        feedback_pending=bool(flags & 4),
        progress=None if progress == 0xFFFFFFFF else progress,
    )
