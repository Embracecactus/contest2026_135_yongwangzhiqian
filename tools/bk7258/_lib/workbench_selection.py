# SPDX-License-Identifier: Apache-2.0
"""ESC1 requests and ESS1 latest-job snapshots; never auto-replay a save."""
import re
import struct

STATES = (
    "idle",
    "pending",
    "preparing",
    "committing",
    "rendering",
    "cancel_pending",
    "done",
    "canceled",
    "failed",
    "unknown",
)


def _token(value):
    if (
        not isinstance(value, str)
        or re.fullmatch(r"[0-9a-f]{32}", value) is None
        or int(value, 16) == 0
    ):
        raise ValueError("Selection identity requires nonzero 32 lowercase hex digits")
    return bytes.fromhex(value)


def _integer(value, maximum):
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError("Invalid selection integer")
    return value


def _filename(value):
    if (
        not isinstance(value, str)
        or len(value) >= 40
        or re.fullmatch(r"[a-z][a-z0-9._-]*\.bkep", value) is None
    ):
        raise ValueError("Selection requires a canonical installed .bkep filename")
    return value.encode("ascii")


def encode(action, epoch, nonce, expected_id, revision=None, filename=None):
    operation = {"set": 1, "refresh": 2, "cancel": 3, "recover": 4}.get(action)
    epoch, nonce = _token(epoch), _token(nonce)
    _integer(expected_id, 2**32 - 1)
    if operation is None or (operation in (3, 4) and expected_id == 0):
        raise ValueError("Invalid selection action or target")
    if operation == 1:
        _integer(revision, 2**64 - 1)
        name = _filename(filename).ljust(40, b"\0")
    else:
        if revision is not None or filename is not None:
            raise ValueError("Only explicit default-set accepts revision and filename")
        revision, name = 0, bytes(40)
    return (
        b"ESC1"
        + struct.pack(">I", operation)
        + epoch
        + nonce
        + struct.pack(">IIQ", expected_id, 0, revision)
        + name
    )


def validate_query(expected_epoch=None, expected_nonce=None, expected_id=None):
    if expected_epoch is not None:
        _token(expected_epoch)
    if expected_nonce is not None:
        _token(expected_nonce)
    if expected_id is not None:
        _integer(expected_id, 2**32 - 1)
    if expected_epoch is None and (
        expected_nonce is not None or expected_id is not None
    ):
        raise ValueError("Job or operation matching also requires its device scope")


def decode(data):
    if (
        not isinstance(data, (bytes, bytearray))
        or len(data) != 128
        or data[:4] != b"ESS1"
    ):
        raise ValueError("Invalid selection snapshot")
    state = struct.unpack_from(">I", data, 4)[0]
    epoch = data[8:24].hex()
    _token(epoch)
    job_id, error, release, flags, revision, expected = struct.unpack_from(
        ">IiiIQQ", data, 24
    )
    sequence = struct.unpack_from(">Q", data, 112)[0]
    name, separator, padding = bytes(data[72:112]).partition(b"\0")
    if (
        state >= len(STATES)
        or error > 0
        or release > 0
        or flags & ~31
        or not sequence
        or any(data[120:])
        or not separator
        or any(padding)
    ):
        raise ValueError("Invalid selection snapshot fields")
    filename = name.decode("ascii") if name else None
    if filename is not None:
        _filename(filename)
    known, saved, rendered, refresh, recovering = (
        bool(flags & (1 << i)) for i in range(5)
    )
    if (
        (state == 0) != (job_id == 0)
        or (saved and (not known or refresh))
        or (saved and state in (1, 2, 5))
        or (state == 4 and not saved)
        or (rendered and not saved)
        or (known and filename is None)
        or (not known and revision != 0)
        or (release and state != 9)
        or (recovering and (not release or state != 9))
        or (state == 6 and (error or release or not known or not (refresh or rendered)))
        or (state == 7 and (not error or release or saved or rendered))
        or (
            state == 0
            and (
                flags
                or error
                or release
                or revision
                or expected
                or name
                or any(data[56:72])
            )
        )
    ):
        raise ValueError("Inconsistent selection outcome")
    return dict(
        state=STATES[state],
        epoch=epoch,
        id=job_id,
        error=error,
        release_error=release,
        version_known=known,
        revision=revision if known else None,
        expected_revision=expected,
        filename=filename,
        operation_nonce=data[56:72].hex() if any(data[56:72]) else None,
        snapshot_sequence=sequence,
        volatile=True,
        snapshot_of="latest_selection_job",
        device_reports_saved=saved,
        device_reports_rendered=rendered,
        refresh=refresh,
        refresh_complete=state == 6 and refresh,
        selection_complete=state == 6 and not refresh and saved and rendered,
        cancel_confirmed=state == 7,
        recovery_pending=recovering,
    )


def prepare(args):
    if any(
        getattr(args, key, None) is not None
        for key in (
            "expected_trial_id",
            "operation_id",
            "expression",
            "ttl_ms",
            "task_id",
            "event_sequence",
            "state",
            "progress",
            "file",
            "receipt",
        )
    ):
        raise ValueError("Default selection cannot be combined with other operations")
    if args.operation == "default-status":
        if args.pack_filename is not None or args.expected_default_revision is not None:
            raise ValueError("Status cannot change a default")
        validate_query(
            args.selection_epoch, args.selection_nonce, args.expected_selection_id
        )
        return
    encode(
        args.operation.removeprefix("default-"),
        args.selection_epoch,
        args.selection_nonce,
        args.expected_selection_id,
        args.expected_default_revision,
        args.pack_filename,
    )


def perform(client, args):
    if args.operation == "default-status":
        return client.selection_status(
            expected_epoch=args.selection_epoch,
            expected_nonce=args.selection_nonce,
            expected_id=args.expected_selection_id,
        )
    return client.selection_request(
        args.operation.removeprefix("default-"),
        args.selection_epoch,
        args.selection_nonce,
        args.expected_selection_id,
        args.expected_default_revision,
        args.pack_filename,
    )
