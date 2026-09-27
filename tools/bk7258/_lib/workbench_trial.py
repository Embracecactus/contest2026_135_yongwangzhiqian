# SPDX-License-Identifier: Apache-2.0
"""Existing ETC1/ETS1 expression trials, not pack activation or persistence."""
import re
import struct

EXPRESSIONS = (
    "neutral",
    "happy",
    "shy",
    "sad",
    "surprised",
    "thinking",
    "listening",
    "speaking",
    "sleepy",
)
STATES = (
    "idle",
    "pending",
    "rendering",
    "active",
    "cancel_pending",
    "restoring",
    "expired",
    "canceled",
    "superseded",
    "failed",
)


def encode(action, expected_id, operation_id, expression=None, ttl_ms=None):
    if (
        type(expected_id) is not int
        or not 0 <= expected_id <= 0xFFFFFFFF
        or not isinstance(operation_id, str)
        or re.fullmatch(r"[0-9a-f]{16}", operation_id) is None
        or int(operation_id, 16) == 0
    ):
        raise ValueError(
            "Explicit expected trial ID and nonzero 16-hex operation ID required"
        )
    if action == "start":
        if (
            expression not in EXPRESSIONS
            or type(ttl_ms) is not int
            or not 0 < ttl_ms <= 0xFFFFFFFF
        ):
            raise ValueError(
                "Trial requires a supported expression and positive u32 TTL"
            )
        action_code, expression_code = 1, EXPRESSIONS.index(expression) + 1
    elif action == "cancel":
        if expected_id == 0 or expression is not None or ttl_ms not in (None, 0):
            raise ValueError(
                "Cancel requires an exact nonzero trial ID without expression/TTL"
            )
        action_code, expression_code, ttl_ms = 2, 0, 0
    else:
        raise ValueError("Unsupported trial operation")
    return struct.pack(
        ">4sIIIQII",
        b"ETC1",
        action_code,
        expected_id,
        ttl_ms,
        int(operation_id, 16),
        expression_code,
        0,
    )


def decode(data):
    if len(data) != 32:
        raise ValueError("Invalid trial snapshot length")
    magic, state, trial_id, error, remaining, operation = struct.unpack(
        ">4sIIiQQ", data
    )
    if magic != b"ETS1" or state >= len(STATES) or error > 0:
        raise ValueError("Invalid trial snapshot")
    if (state == 0) != (trial_id == 0):
        raise ValueError("Inconsistent trial identity")
    return dict(
        state=STATES[state],
        id=trial_id,
        error=error,
        remaining_ms=None if remaining == 0xFFFFFFFFFFFFFFFF else remaining,
        operation_id=f"{operation:016x}",
        device_reports_rendered=state == 3,
        cancel_confirmed=state == 7,
    )


def prepare(args):
    if args.operation == "trial-status":
        if any(
            getattr(args, k, None) is not None
            for k in ("expected_trial_id", "operation_id", "expression", "ttl_ms")
        ):
            raise ValueError("Trial status does not accept mutation arguments")
        return
    encode(
        args.operation.removeprefix("trial-"),
        args.expected_trial_id,
        args.operation_id,
        args.expression,
        args.ttl_ms,
    )
