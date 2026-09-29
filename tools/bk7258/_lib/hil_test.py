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
AUDIO_RECORD_SIZE = 32
AUDIO_STATUS_SIZE = 64
AUDIO_STAGE_BYTES = 8192
AUDIO_STATES = {0: "idle", 1: "running", 2: "complete", 3: "failed"}


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


def encode_audio_run(*, session):
    session = _u32(session, "audio session")
    if session == 0:
        raise HilTestError("Invalid audio session")
    return b"BKA1" + struct.pack(">7I", VERSION, 1, session, 1, 0, 0, 0)


def decode_audio_command(record):
    if (
        not isinstance(record, (bytes, bytearray, memoryview))
        or len(record) != AUDIO_RECORD_SIZE
    ):
        raise HilTestError("Invalid engineering audio command size")
    magic, version, operation, session, sequence, flags, reserved0, reserved1 = (
        struct.unpack(">4s7I", bytes(record))
    )
    if (
        magic != b"BKA1"
        or version != VERSION
        or operation != 1
        or session == 0
        or sequence != 1
        or flags != 0
        or reserved0 != 0
        or reserved1 != 0
    ):
        raise HilTestError("Invalid engineering audio command")
    return {"operation": "run", "session": session, "sequence": sequence}


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


def decode_audio_status(record):
    if (
        not isinstance(record, (bytes, bytearray, memoryview))
        or len(record) != AUDIO_STATUS_SIZE
    ):
        raise HilTestError("Invalid engineering audio status size")
    words = struct.unpack(">4s15I", bytes(record))
    magic, version, state, session, sequence, stages, accepted = words[:7]
    values = [_signed(value) for value in words[7:15]]
    reserved = words[15]
    if magic != b"BAS1" or version != VERSION or state not in AUDIO_STATES:
        raise HilTestError("Invalid engineering audio status version")
    if reserved != 0 or stages > 3 or accepted > 3 * AUDIO_STAGE_BYTES:
        raise HilTestError("Invalid engineering audio status fields")
    if state == 0 and any((session, sequence, stages, accepted, *values)):
        raise HilTestError("Invalid idle engineering audio status")
    if state != 0 and (session == 0 or sequence != 1):
        raise HilTestError("Invalid engineering audio status identity")
    if state == 2 and (
        stages != 3
        or accepted != 3 * AUDIO_STAGE_BYTES
        or values != [0, 0, 0, -125, -125, 0, 0, 0]
    ):
        raise HilTestError("Engineering audio completion is incomplete")
    if state == 3 and values[0] >= 0:
        raise HilTestError("Engineering audio failure lacks an error")
    names = (
        "result",
        "eof_result",
        "eof_close",
        "cancel_write",
        "cancel_drain",
        "cancel_close",
        "next_result",
        "next_close",
    )
    return {
        "state": AUDIO_STATES[state],
        "session": session,
        "sequence": sequence,
        "stages": stages,
        "accepted_bytes": accepted,
        **dict(zip(names, values)),
    }


def require_status(client):
    status = client.engineering_status()
    if status.get("enabled") is not True:
        raise HilTestError("Device is not an engineering test build")
    return status


def key_sequence(client, *, session, held_ms, pm_mode="blocked", query=True):
    from . import workbench

    require_status(client)
    commands = (
        encode_command("session", session=session, sequence=1, value=pm_mode),
        encode_command("key", session=session, sequence=2, value=2),
        encode_command("advance", session=session, sequence=3, elapsed_ms=held_ms),
        encode_command("key", session=session, sequence=4, elapsed_ms=held_ms),
    )
    for index, record in enumerate(commands):
        try:
            result = client.engineering_command(record)
        except workbench.CommandUnconfirmed:
            if not query and held_ms >= 3000 and index == len(commands) - 1:
                return dict(
                    enabled=True,
                    session=session,
                    sequence=4,
                    elapsed_ms=held_ms,
                    key_mask=0,
                    pm_mode=pm_mode,
                    accepted=None,
                    completion_verified=False,
                    final_command="unconfirmed",
                )
            raise
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


def validate_key_result(held_ms, observation):
    if held_ms < 3000:
        if (
            observation.get("power_intent")
            or observation.get("pm_requests") != 0
            or observation.get("power_state") != "idle"
            or observation.get("power_error") != 0
        ):
            raise HilTestError("Short K2 sequence produced a power intent")
        return {"result": "PASS", "observation": observation}
    final = observation.get("final")
    if not isinstance(final, dict) or final.get("contract_state") not in (
        "FAILED",
        "DONE",
    ):
        raise HilTestError("Long K2 sequence lacks terminal power evidence")
    return {"result": "PASS", "observation": observation}


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

    parser.add_argument(
        "operation", choices=("enroll", "status", "key", "audio", "revoke")
    )
    workbench.add_connection_arguments(parser, port_required=True)
    parser.add_argument("--console-port")
    parser.add_argument("--git-sha", required=True)
    parser.add_argument("--session", type=int, default=1)
    parser.add_argument("--held-ms", type=int, default=3000)
    parser.add_argument("--pm-mode", choices=tuple(PM_MODES), default="blocked")


def run(args, command):
    from . import factory_diagnostics, workbench

    if not re.fullmatch(r"[0-9a-f]{40}", args.git_sha):
        raise HilTestError("Invalid source identity")
    if not 0 < args.session <= 0xFFFFFFFF or not 0 <= args.held_ms <= 120000:
        raise HilTestError("Invalid key sequence")

    if args.operation == "enroll":
        if args.profile is None or not args.console_port:
            raise HilTestError("Factory enrollment requires console and profile")
        enrollment = factory_diagnostics.enroll(
            args.console_port,
            args.port,
            args.profile,
            timeout=args.timeout,
        )
        with workbench.authorized_client(args) as client:
            info = client.info()
            runtime = client.status()
            bktest = require_status(client)
        firmware = {
            **info,
            "version": (
                f"{info['major']}.{info['minor']}.{info['revision']}+{info['build']}"
            ),
            "runtime": runtime,
        }
        return report(
            test="factory_diagnostics_enroll",
            result="PASS",
            firmware=firmware,
            git_sha=args.git_sha,
            command=command,
            input_sequence=["physical-factory-enroll", "tls-pin-match", "sdc1-auth"],
            observation={"enrollment": enrollment, "bktest": bktest},
        )

    if args.operation == "revoke":
        if args.profile is None or not args.console_port:
            raise HilTestError("Factory revocation requires console and profile")
        with workbench.authorized_client(args) as client:
            info = client.info()
            runtime = client.status()
            require_status(client)
        revoked = factory_diagnostics.console_revoke(args.console_port)
        invalidated = False
        try:
            with workbench.authorized_client(args) as client:
                client.info()
        except workbench.ControlError:
            invalidated = True
        if not invalidated:
            raise HilTestError("Revoked factory profile still authenticated")
        firmware = {
            **info,
            "version": (
                f"{info['major']}.{info['minor']}.{info['revision']}+{info['build']}"
            ),
            "runtime": runtime,
        }
        return report(
            test="factory_diagnostics_revoke",
            result="PASS",
            firmware=firmware,
            git_sha=args.git_sha,
            command=command,
            input_sequence=["physical-factory-revoke", "old-profile-auth"],
            observation={**revoked, "old_profile_rejected": True},
        )

    wait_for_power = False
    with workbench.authorized_client(args) as client:
        info = client.info()
        runtime = client.status()
        if args.operation == "status":
            observation = require_status(client)
            sequence = []
            result = "PASS"
            test = "bktest_status"
        elif args.operation == "key":
            observation = key_sequence(
                client,
                session=args.session,
                held_ms=args.held_ms,
                pm_mode=args.pm_mode,
                query=args.held_ms < 3000,
            )
            sequence = ["down", f"advance:{args.held_ms}", "up"]
            if args.held_ms < 3000:
                result = validate_key_result(args.held_ms, observation)["result"]
            else:
                if not args.console_port:
                    raise HilTestError(
                        "Long K2 validation requires an independent console observer"
                    )
                wait_for_power = True
                result = None
            test = f"k2_{args.held_ms}ms_{args.pm_mode}"
        else:
            require_status(client)
            client.engineering_audio_run(args.session)
            observation = client.engineering_audio_status()
            if observation.get("state") != "complete":
                raise HilTestError("Engineering audio lifecycle did not complete")
            sequence = ["fixed-pcm-eof", "cancel", "next-session-eof"]
            result = "PASS"
            test = "audio_eof_cancel_next"
    if wait_for_power:
        observation = factory_diagnostics.wait_power(args.console_port, timeout=35)
        result = validate_key_result(args.held_ms, observation)["result"]
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
