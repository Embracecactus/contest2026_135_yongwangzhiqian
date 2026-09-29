# SPDX-License-Identifier: Apache-2.0
"""Authenticated BKTEST codec and bounded native-USB HIL commands."""

from __future__ import annotations

from datetime import datetime, timezone
import re
import struct


class HilTestError(ValueError):
    pass


VERSION = 1
RECORD_SIZE = 32
STATUS_SIZE = 64
OPERATIONS = {"session": 1, "key": 2, "advance": 3, "end": 4}
OPERATION_NAMES = {value: name for name, value in OPERATIONS.items()}
PM_MODES = {"blocked": 0, "declined": 1, "unknown": 2, "pending": 3, "late-ack": 4}
PM_MODE_NAMES = {value: name for name, value in PM_MODES.items()}
POWER_STATES = {0: "idle", 1: "preparing", 2: "pending", 3: "failed"}
VOICE_STATES = {0: "unavailable", 1: "idle", 2: "busy"}
STORAGE_STATES = {0: "unavailable", 1: "ready"}
NETWORK_STATES = {0: "offline", 1: "link", 2: "ready"}


def _u32(value, name):
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
        raise HilTestError(f"Invalid {name}")
    return value


def encode_command(operation, *, session, sequence, value=0, elapsed_ms=0, flags=0):
    if operation not in OPERATIONS:
        raise HilTestError("Invalid engineering operation")
    if operation == "session" and isinstance(value, str):
        try:
            value = PM_MODES[value]
        except KeyError:
            raise HilTestError("Invalid PM peer mode") from None
    fields = (
        VERSION,
        OPERATIONS[operation],
        _u32(session, "session"),
        _u32(sequence, "sequence"),
        _u32(value, "value"),
        _u32(elapsed_ms, "elapsed time"),
        _u32(flags, "flags"),
    )
    if session == 0 or sequence == 0 or flags != 0:
        raise HilTestError("Invalid engineering command identity")
    return b"BKT1" + struct.pack(">7I", *fields)


def decode_command(record):
    if (
        not isinstance(record, (bytes, bytearray, memoryview))
        or len(record) != RECORD_SIZE
    ):
        raise HilTestError("Invalid engineering command size")
    magic, version, operation, session, sequence, value, elapsed, flags = struct.unpack(
        ">4s7I", bytes(record)
    )
    if magic != b"BKT1" or version != VERSION or operation not in OPERATION_NAMES:
        raise HilTestError("Invalid engineering command version")
    if session == 0 or sequence == 0 or flags != 0:
        raise HilTestError("Invalid engineering command identity")
    result = dict(
        operation=OPERATION_NAMES[operation],
        session=session,
        sequence=sequence,
    )
    if operation == OPERATIONS["session"]:
        if value not in PM_MODE_NAMES:
            raise HilTestError("Invalid PM peer mode")
        result["pm_mode"] = PM_MODE_NAMES[value]
    elif operation == OPERATIONS["key"]:
        if value & ~7:
            raise HilTestError("Invalid product key mask")
        result["key_mask"] = value
    elif value != 0:
        raise HilTestError("Invalid operation value")
    result["elapsed_ms"] = elapsed
    result["flags"] = flags
    return result


def _signed(value):
    return struct.unpack(">i", struct.pack(">I", value))[0]


def decode_status(record):
    if (
        not isinstance(record, (bytes, bytearray, memoryview))
        or len(record) != STATUS_SIZE
    ):
        raise HilTestError("Invalid engineering status size")
    words = struct.unpack(">4s15I", bytes(record))
    magic, version = words[:2]
    if magic != b"BKS1" or version != VERSION:
        raise HilTestError("Invalid engineering status version")
    flags, session, sequence, elapsed, key_mask, pm_mode = words[2:8]
    pm_requests, pm_queries, power, power_error, last_result = words[8:13]
    voice, storage, network = words[13:16]
    if flags & ~15 or key_mask & ~7 or pm_mode not in PM_MODE_NAMES:
        raise HilTestError("Invalid engineering status fields")
    phase = power & 0xFF
    if phase not in POWER_STATES or power & ~0x1FF:
        raise HilTestError("Invalid product power state")
    if (
        voice not in VOICE_STATES
        or storage not in STORAGE_STATES
        or network not in NETWORK_STATES
    ):
        raise HilTestError("Invalid product observer state")
    return dict(
        enabled=bool(flags & 1),
        active=bool(flags & 2),
        expired=bool(flags & 4),
        power_intent=bool(flags & 8),
        session=session,
        sequence=sequence,
        elapsed_ms=elapsed,
        key_mask=key_mask,
        pm_mode=PM_MODE_NAMES[pm_mode],
        pm_requests=pm_requests,
        pm_queries=pm_queries,
        power_state=POWER_STATES[phase],
        power_unresolved=bool(power & 0x100),
        power_error=_signed(power_error),
        last_result=_signed(last_result),
        voice_state=VOICE_STATES[voice],
        storage_state=STORAGE_STATES[storage],
        network_state=NETWORK_STATES[network],
    )


def require_status(client):
    status = client.engineering_status()
    if status.get("enabled") is not True:
        raise HilTestError("Device is not an engineering test build")
    return status


def key_sequence(client, *, session, held_ms, pm_mode="blocked", query=True):
    require_status(client)
    commands = (
        encode_command("session", session=session, sequence=1, value=pm_mode),
        encode_command("key", session=session, sequence=2, value=2),
        encode_command("advance", session=session, sequence=3, elapsed_ms=held_ms),
        encode_command("key", session=session, sequence=4, elapsed_ms=held_ms),
    )
    for record in commands:
        result = client.engineering_command(record)
        if result.get("accepted") is not True:
            raise HilTestError("Engineering command was not accepted")
    if query:
        return require_status(client)
    return dict(
        enabled=True,
        session=session,
        sequence=4,
        elapsed_ms=held_ms,
        key_mask=0,
        pm_mode=pm_mode,
        accepted=True,
        completion_verified=False,
    )


def report(*, test, result, firmware, git_sha, command, input_sequence, observation,
           failure_reason=None):
    if not re.fullmatch(r"[0-9a-f]{40}", git_sha):
        raise HilTestError("Invalid source identity")
    return dict(
        test=test,
        result=result,
        firmware=firmware,
        git_sha=git_sha,
        timestamp=datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        command=list(command),
        input_sequence=list(input_sequence),
        observation=dict(observation),
        failure_reason=failure_reason,
    )


def add_arguments(parser):
    from . import workbench

    parser.add_argument("operation", choices=("status", "key"))
    workbench.add_connection_arguments(parser, port_required=True)
    parser.add_argument("--git-sha", required=True)
    parser.add_argument("--session", type=int, default=1)
    parser.add_argument("--held-ms", type=int, default=3000)
    parser.add_argument("--pm-mode", choices=tuple(PM_MODES), default="blocked")


def run(args, command):
    from . import workbench

    if not re.fullmatch(r"[0-9a-f]{40}", args.git_sha):
        raise HilTestError("Invalid source identity")
    if not 0 < args.session <= 0xFFFFFFFF or not 0 <= args.held_ms <= 120000:
        raise HilTestError("Invalid key sequence")
    with workbench.authorized_client(args) as client:
        info = client.info()
        runtime = client.status()
        if args.operation == "status":
            observation = require_status(client)
            sequence = []
            result = "PASS"
            test = "bktest_status"
        else:
            observation = key_sequence(
                client,
                session=args.session,
                held_ms=args.held_ms,
                pm_mode=args.pm_mode,
                query=args.held_ms < 3000,
            )
            sequence = ["down", f"advance:{args.held_ms}", "up"]
            result = "PASS" if args.held_ms < 3000 else "ACCEPTED"
            test = f"k2_{args.held_ms}ms_{args.pm_mode}"
    firmware = {
        **info,
        "version": (
            f"{info['major']}.{info['minor']}.{info['revision']}+{info['build']}"
        ),
        "runtime": runtime,
    }
    return report(
        test=test,
        result=result,
        firmware=firmware,
        git_sha=args.git_sha,
        command=command,
        input_sequence=sequence,
        observation=observation,
    )
