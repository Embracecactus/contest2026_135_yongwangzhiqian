# SPDX-License-Identifier: Apache-2.0
"""ECC1/ECL1 catalog operations; snapshots never scan or activate a pack."""
import re
import struct

from .workbench_selection import STATES, _filename, _integer, _token, validate_query


def encode(action, epoch, nonce, expected_id, after=None):
    operation = {"page": 1, "cancel": 2, "recover": 3}.get(action)
    epoch, nonce = _token(epoch), _token(nonce)
    _integer(expected_id, 2**32 - 1)
    if operation is None or (operation != 1 and expected_id == 0):
        raise ValueError("Invalid catalog action or target")
    if operation != 1 and after is not None:
        raise ValueError("Only a page request accepts a cursor")
    cursor = _filename(after) if after not in (None, "") else b""
    return (
        b"ECC1"
        + struct.pack(">I", operation)
        + epoch
        + nonce
        + struct.pack(">I", expected_id)
        + bytes(12)
        + cursor.ljust(40, b"\0")
    )


def _string(raw):
    name, separator, padding = bytes(raw).partition(b"\0")
    if not separator or any(padding):
        raise ValueError("Invalid catalog string padding")
    return name.decode("ascii")


def decode(data):
    if (
        not isinstance(data, (bytes, bytearray))
        or len(data) != 608
        or data[:4] != b"ECL1"
    ):
        raise ValueError("Invalid catalog snapshot")
    state = struct.unpack_from(">I", data, 4)[0]
    epoch = data[8:24].hex()
    _token(epoch)
    job_id, error, release, flags = struct.unpack_from(">IiiI", data, 24)
    sequence, count = struct.unpack_from(">QI", data, 56)
    catalog, recovering, available, more = (bool(flags & (1 << i)) for i in range(4))
    if (
        state >= len(STATES)
        or error > 0
        or release > 0
        or flags & ~15
        or not sequence
        or count > 4
        or any(data[68:96])
        or (state == 0) != (job_id == 0)
        or (available and (not catalog or state != 6 or error or release))
        or (catalog and state == 6 and not available)
        or (catalog and state in (3, 4))
        or (release and state != 9)
        or (recovering and (not release or state != 9))
        or (more and (not available or count != 4))
        or (not available and count)
        or any(data[96 + count * 128 :])
        or (state == 7 and (not error or release))
        or (state == 0 and (flags or error or release or any(data[40:56])))
    ):
        raise ValueError("Inconsistent catalog outcome")
    entries = []
    for index in range(count):
        raw = data[96 + index * 128 : 224 + index * 128]
        filename = _string(raw[:40])
        _filename(filename)
        pack_id = _string(raw[40:72])
        revision, api, width, height, items, palette = struct.unpack_from(
            ">IHHHHH", raw, 72
        )
        total = struct.unpack_from(">I", raw, 88)[0]
        if (
            not re.fullmatch(r"[a-z0-9][a-z0-9._-]*", pack_id)
            or not revision
            or not api
            or not width
            or not height
            or items < 2
            or not 2 <= palette <= 256
            or total < 128
            or any(raw[86:88])
            or any(raw[124:])
            or (entries and filename <= entries[-1]["filename"])
        ):
            raise ValueError("Invalid catalog entry")
        entries.append(
            dict(
                filename=filename,
                pack_id=pack_id,
                revision=revision,
                renderer_api=api,
                width=width,
                height=height,
                entry_count=items,
                palette_count=palette,
                total_bytes=total,
                source_sha256=raw[92:124].hex(),
            )
        )
    return dict(
        state=STATES[state],
        epoch=epoch,
        id=job_id,
        error=error,
        release_error=release,
        catalog=catalog,
        recovery_pending=recovering,
        page_available=available,
        more=more,
        entries=entries,
        next_cursor=entries[-1]["filename"] if more else None,
        operation_nonce=data[40:56].hex() if any(data[40:56]) else None,
        snapshot_sequence=sequence,
        snapshot_of="latest_shared_resource_job",
        cancel_confirmed=state == 7 and catalog,
        volatile=True,
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
            "pack_filename",
            "expected_default_revision",
        )
    ):
        raise ValueError("Catalog cannot be combined with other operations")
    if args.operation == "catalog-status":
        if args.catalog_after is not None:
            raise ValueError("Status cannot start a page")
        validate_query(
            args.selection_epoch, args.selection_nonce, args.expected_selection_id
        )
        return
    encode(
        args.operation.removeprefix("catalog-"),
        args.selection_epoch,
        args.selection_nonce,
        args.expected_selection_id,
        args.catalog_after,
    )


def perform(client, args):
    if args.operation == "catalog-status":
        return client.catalog_status(
            expected_epoch=args.selection_epoch,
            expected_nonce=args.selection_nonce,
            expected_id=args.expected_selection_id,
        )
    return client.catalog_request(
        args.operation.removeprefix("catalog-"),
        args.selection_epoch,
        args.selection_nonce,
        args.expected_selection_id,
        args.catalog_after,
    )
