# SPDX-License-Identifier: Apache-2.0
"""Agent Approval Totem: PC side of the device PC_APPROVAL kind (29).

The PC submits one exact APR1 request over the existing authorized USB
workbench session. Only the phone owner can decide (kind 30, never granted
to a PC). The PC accepts a decision only when the APS1 receipt names the
same request ID and SHA-256 of the exact bytes it sent, and carries a valid
HMAC-SHA256 under this PC's independent grant key. Anything else is a deny.
"""
from __future__ import annotations

import hashlib
import hmac
import json
import os
import secrets
import shlex
import struct
import subprocess
import sys
import time

KIND = 29
AGENT_MAX, TARGET_MAX, OPERATION_MAX = 32, 256, 768
TTL_MIN_MS, TTL_MAX_MS = 5000, 300000
RECEIPT_SIZE = 96
TAG_CONTEXT = b"SHANIU-PC-APPROVAL-v1"
STATES = {0: "none", 1: "pending", 2: "approved", 3: "rejected", 4: "expired",
          5: "canceled"}
_BIDI = {0x061C, 0x200E, 0x200F, *range(0x202A, 0x202F), *range(0x2066, 0x206A)}


class ApprovalError(ValueError):
    pass


def check_text(name, text, minimum, maximum):
    if not isinstance(text, str):
        raise ApprovalError(f"{name} must be text")
    data = text.encode("utf-8")
    if not minimum <= len(data) <= maximum:
        raise ApprovalError(
            f"{name} is {len(data)} UTF-8 bytes; exact approval allows "
            f"{minimum}..{maximum}"
        )
    for char in text:
        code = ord(char)
        if (
            (code < 0x20 and char not in "\t\n")
            or 0x7F <= code < 0xA0
            or code in _BIDI
            or 0xD800 <= code <= 0xDFFF
        ):
            raise ApprovalError(f"{name} contains a control or bidi character")
    return data


def encode_request(request_id, ttl_ms, agent, target, operation):
    if not isinstance(request_id, bytes) or len(request_id) != 16 or not any(
        request_id
    ):
        raise ApprovalError("Invalid request ID")
    if type(ttl_ms) is not int or not TTL_MIN_MS <= ttl_ms <= TTL_MAX_MS:
        raise ApprovalError("Approval timeout must be 5..300 seconds")
    a = check_text("agent", agent, 1, AGENT_MAX)
    t = check_text("target", target, 0, TARGET_MAX)
    o = check_text("operation", operation, 1, OPERATION_MAX)
    return (
        b"APR1" + request_id + struct.pack(">IHHHH", ttl_ms, len(a), len(t), len(o), 0)
        + a + t + o
    )


def expected_tag(key, request_id, digest, state):
    return hmac.new(
        bytes(key), TAG_CONTEXT + request_id + digest + struct.pack(">I", state),
        hashlib.sha256,
    ).digest()


def decode_receipt(data, request_id, digest, key):
    """Return (state_name, remaining_ms, authenticated) for this request."""
    if len(data) != RECEIPT_SIZE or data[:4] != b"APS1":
        raise ApprovalError("Invalid approval receipt")
    state, = struct.unpack(">I", data[4:8])
    remaining, reserved = struct.unpack(">II", data[56:64])
    if state not in STATES or reserved:
        raise ApprovalError("Invalid approval receipt")
    if not hmac.compare_digest(data[8:24], request_id) or not hmac.compare_digest(
        data[24:56], digest
    ):
        # The device holds another request (or none): never our decision.
        return "superseded", 0, False
    tag = data[64:96]
    if state in (2, 3):
        if not hmac.compare_digest(tag, expected_tag(key, request_id, digest, state)):
            raise ApprovalError("Approval receipt authentication failed")
        return STATES[state], 0, True
    if any(tag):
        raise ApprovalError("Invalid approval receipt tag")
    return STATES[state], remaining, False


def _config_write(client, record, deadline):
    client._exchange(16, struct.pack(">II", KIND, len(record)), deadline)
    for offset in range(0, len(record), 32):
        client._exchange(17, record[offset : offset + 32], deadline)
    client._exchange(18, b"", deadline)


def _receipt(client, deadline):
    def chunk(offset):
        total, *words = client._exchange(
            15, struct.pack(">I", KIND << 16 | offset), deadline
        )
        if total != RECEIPT_SIZE:
            raise ApprovalError("Invalid approval receipt size")
        return struct.pack(">4I", *words)

    # Config reads are chunked transactions.  A phone decision can land after
    # the header was read but before the tag, producing a torn pending/final
    # receipt.  Re-read the immutable header and retry only that race; a
    # stable malformed or forged receipt is still rejected by decode_receipt.
    for _ in range(3):
        first = chunk(0)
        data = bytearray(first)
        for offset in range(16, RECEIPT_SIZE, 16):
            data += chunk(offset)
        if chunk(0) == first:
            return bytes(data)
    raise ApprovalError("Unstable approval receipt")


def request(client, key, agent, target, operation, timeout_s, *, poll_s=1.0,
            cancel_requested=None, clock=time.monotonic, sleep=time.sleep):
    """Submit one exact request and wait for an authenticated phone decision."""
    ttl_ms = int(round(timeout_s * 1000))
    request_id = secrets.token_bytes(16)
    record = encode_request(request_id, ttl_ms, agent, target, operation)
    digest = hashlib.sha256(record).digest()
    if key is None or len(key) != 32:
        raise ApprovalError("PC key is required to verify the decision")
    started = clock()
    deadline = started + ttl_ms / 1000 + 5
    result = dict(
        request_id=request_id.hex(), operation_sha256=digest.hex(),
        agent=agent, target=target, operation=operation,
        decision="unconfirmed", authenticated=False,
    )
    _config_write(client, record, clock() + client._timeout)
    print(
        f"Shaniu: approval requested (fingerprint {digest[:4].hex()}); "
        "confirm the exact operation in the phone app",
        file=sys.stderr, flush=True,
    )
    try:
        while True:
            state, remaining, authenticated = decode_receipt(
                _receipt(client, clock() + client._timeout), request_id, digest, key
            )
            if state != "pending":
                result.update(decision=state, authenticated=authenticated)
                return result
            if cancel_requested is not None and cancel_requested():
                break
            if clock() >= deadline:
                break
            sleep(min(poll_s, max(0.05, remaining / 1000)))
    except KeyboardInterrupt:
        pass
    # Leaving without a decision: withdraw it so nobody approves a request
    # whose requester is gone. Any failure here still denies.
    try:
        _config_write(client, b"APC1" + request_id, clock() + client._timeout)
        result["decision"] = "canceled"
    except Exception:
        result["decision"] = "unconfirmed"
    return result


def approved(result):
    return result.get("decision") == "approved" and result.get("authenticated") is True


def hook_operation(payload):
    """Exact text for a Claude Code PreToolUse payload."""
    tool = payload.get("tool_name")
    tool_input = payload.get("tool_input")
    if not isinstance(tool, str) or not isinstance(tool_input, dict):
        raise ApprovalError("Hook payload has no tool call")
    if tool == "Bash" and isinstance(tool_input.get("command"), str):
        return "Bash: " + tool_input["command"]
    return tool + " " + json.dumps(
        tool_input, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    )


def hook_output(allow, reason):
    return {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "allow" if allow else "deny",
            "permissionDecisionReason": reason,
        }
    }


def run_command(argv):
    """Run an approved argv exactly as approved, without a shell."""
    return subprocess.run(argv, check=False).returncode


def command_text(argv):
    if not argv:
        raise ApprovalError("approval-run requires --exec PROGRAM [ARGS...]")
    return shlex.join(argv)


def current_target():
    return os.getcwd()
