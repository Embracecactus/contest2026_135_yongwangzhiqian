# SPDX-License-Identifier: Apache-2.0
"""BCSI recording, calibration and optional RuView ADR-018 adaptation.

Consumed by bk7258.py. Raw datagrams are the evidence; the amplitude scores
are experimental room-specific heuristics, never hardware acceptance.
"""

from __future__ import annotations

import argparse
import base64
import collections
import ipaddress
import json
import math
import socket
import statistics
import struct
import time
from pathlib import Path


HEADER = struct.Struct("<IHH8IHHbBBB I6sBBI")
MAGIC = 0x49534342
MAX_SAMPLES = 52
LABELS = ("empty", "still", "moving", "unlabeled")


def add_arguments(parser: argparse.ArgumentParser) -> None:
    commands = parser.add_subparsers(dest="csi_command", required=True)
    receive = commands.add_parser("receive", help="record real BCSI UDP datagrams")
    receive.add_argument("--bind", default="0.0.0.0")
    receive.add_argument("--port", type=int, default=5505)
    receive.add_argument("--source", help="accept only this device IPv4 address")
    receive.add_argument("--seconds", type=float, default=70)
    receive.add_argument("--output", type=Path, required=True)
    receive.add_argument("--label", choices=LABELS, default="unlabeled")
    receive.add_argument(
        "--ruview", metavar="IP:PORT", help="forward converted ADR-018"
    )
    receive.add_argument("--node-id", type=int, default=58)
    analyze = commands.add_parser(
        "analyze", help="measure recorded CSI and optional baseline scores"
    )
    analyze.add_argument("--input", type=Path, action="append", required=True)
    analyze.add_argument("--baseline", type=Path, help="independent empty-room capture")
    analyze.add_argument("--window-seconds", type=float, default=2)


def decode(data: bytes) -> dict:
    if len(data) < HEADER.size:
        raise ValueError("short BCSI header")
    (
        magic,
        version,
        kind,
        session,
        sequence,
        timestamp,
        callbacks,
        invalid,
        queue_drops,
        sent,
        send_errors,
        count,
        data_type,
        rssi,
        channel,
        frame_format,
        bandwidth,
        active,
        mac,
        mcs,
        rate,
        uptime,
    ) = HEADER.unpack_from(data)
    if magic != MAGIC or version != 1 or kind not in (1, 2):
        raise ValueError("unsupported BCSI header")
    if len(data) != HEADER.size + count * 4 or count > MAX_SAMPLES:
        raise ValueError("BCSI length mismatch")
    if (kind == 1 and (count == 0 or data_type != 0 or not 1 <= channel <= 14)) or (
        kind == 2 and count != 0
    ):
        raise ValueError("invalid BCSI payload")
    words = list(struct.unpack_from(f"<{count}I", data, HEADER.size))
    return {
        "kind": kind,
        "session": session,
        "sequence": sequence,
        "timestamp_us": timestamp,
        "callbacks": callbacks,
        "invalid": invalid,
        "queue_drops": queue_drops,
        "sent": sent,
        "send_errors": send_errors,
        "count": count,
        "data_type": data_type,
        "rssi": rssi,
        "channel": channel,
        "format": frame_format,
        "bandwidth": bandwidth,
        "active": active,
        "mac": mac.hex(":"),
        "mcs": mcs,
        "rate": rate,
        "uptime_ms": uptime,
        "words": words,
    }


def signed13(value: int) -> int:
    value &= 0x1FFF
    return value - 0x2000 if value & 0x1000 else value


def iq_pairs(frame: dict) -> list[tuple[int, int]]:
    # Pinned BK library csi_alg_data_2_abs extracts two adjacent signed
    # 13-bit lanes, at bits 0 and 13. Raw BCSI retains all 32 original bits.
    return [(signed13(word), signed13(word >> 13)) for word in frame["words"]]


def ruview_packet(frame: dict, node_id: int) -> bytes:
    if frame["kind"] != 1 or not 0 <= node_id <= 255:
        raise ValueError("ADR-018 requires a CSI frame and uint8 node ID")
    pairs = iq_pairs(frame)
    # A fixed divisor preserves relative amplitudes between frames. Do not
    # normalize each frame, which would hide actual channel gain changes.
    values = [
        max(-127, min(127, round(value / 32))) for pair in pairs for value in pair
    ]
    frequency = 2484 if frame["channel"] == 14 else 2407 + frame["channel"] * 5
    header = struct.pack(
        "<IBBHI IbbBB",
        0xC5110001,
        node_id,
        1,
        frame["count"],
        frequency,
        frame["sequence"],
        frame["rssi"],
        0,
        0,
        frame["bandwidth"] & 1,
    )
    return header + struct.pack(f"<{len(values)}b", *values)


def _endpoint(text: str) -> tuple[str, int]:
    host, port_text = text.rsplit(":", 1)
    host = str(ipaddress.IPv4Address(host))
    port = int(port_text)
    if not 1 <= port <= 65535:
        raise ValueError("UDP port must be 1..65535")
    return host, port


def receive(args: argparse.Namespace) -> dict:
    if (
        not 1 <= args.port <= 65535
        or not math.isfinite(args.seconds)
        or not 0 < args.seconds <= 900
    ):
        raise ValueError("invalid receive port or duration (maximum 900 seconds)")
    if not 0 <= args.node_id <= 255:
        raise ValueError("node ID must be 0..255")
    source = str(ipaddress.IPv4Address(args.source)) if args.source else None
    peer = _endpoint(args.ruview) if args.ruview else None
    counts = collections.Counter()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
        receiver.bind((str(ipaddress.IPv4Address(args.bind)), args.port))
        receiver.settimeout(0.5)
        # Exclusive creation preserves existing captures, including a prior
        # interrupted run. Always record raw data before optional conversion.
        with args.output.open("x", encoding="utf-8", buffering=1) as output:
            output.write(
                json.dumps(
                    {
                        "schema": "bk7258-csi-recording/1",
                        "label": args.label,
                        "started_ns": time.time_ns(),
                        "ruview_forward": peer is not None,
                        "node_id": args.node_id,
                    }
                )
                + "\n"
            )
            print(
                f"BCSI listening on {args.bind}:{args.port}; recording {args.output}",
                flush=True,
            )
            deadline = time.monotonic() + args.seconds
            try:
                while time.monotonic() < deadline:
                    try:
                        raw, address = receiver.recvfrom(2048)
                    except socket.timeout:
                        continue
                    if source is not None and address[0] != source:
                        counts["other_sources"] += 1
                        continue
                    try:
                        frame = decode(raw)
                    except ValueError:
                        counts["malformed"] += 1
                        continue
                    counts["frames" if frame["kind"] == 1 else "status_packets"] += 1
                    output.write(
                        json.dumps(
                            {
                                "received_ns": time.time_ns(),
                                "source": address[0],
                                "raw_b64": base64.b64encode(raw).decode("ascii"),
                            }
                        )
                        + "\n"
                    )
                    if peer is not None and frame["kind"] == 1:
                        receiver.sendto(ruview_packet(frame, args.node_id), peer)
                        counts["forwarded"] += 1
            except KeyboardInterrupt:
                counts["interrupted"] += 1
            output.write(
                json.dumps({"summary": dict(counts), "ended_ns": time.time_ns()}) + "\n"
            )
    return {
        "output": str(args.output),
        **counts,
        "has_csi_frames": counts["frames"] > 0,
    }


def read_recording(path: Path) -> tuple[str, list[dict]]:
    frames = []
    with path.open(encoding="utf-8") as source:
        metadata = json.loads(next(source))
        if metadata.get("schema") != "bk7258-csi-recording/1":
            raise ValueError(f"unsupported CSI recording: {path}")
        for number, line in enumerate(source, 2):
            record = json.loads(line)
            if "raw_b64" not in record:
                continue
            try:
                frame = decode(base64.b64decode(record["raw_b64"], validate=True))
            except ValueError as error:
                raise ValueError(f"invalid CSI record at line {number}") from error
            frame["received_ns"] = int(record["received_ns"])
            frame["source"] = record["source"]
            frames.append(frame)
    return metadata["label"], frames


def _stream(frame: dict) -> tuple:
    return (
        frame["source"],
        frame["mac"],
        frame["channel"],
        frame["format"],
        frame["bandwidth"],
        frame["count"],
    )


def _shape(frame: dict) -> list[float]:
    amplitudes = [math.hypot(i, q) for i, q in iq_pairs(frame)]
    norm = math.sqrt(sum(value * value for value in amplitudes))
    if norm == 0:
        raise ValueError("all-zero CSI frame has no amplitude shape")
    return [value / norm for value in amplitudes]


def _distance(left: list[float], right: list[float]) -> float:
    return math.sqrt(statistics.mean((a - b) ** 2 for a, b in zip(left, right)))


def _threshold(values: list[float]) -> float:
    center = statistics.median(values)
    mad = statistics.median(abs(value - center) for value in values)
    return max(0.002, center + 6 * 1.4826 * mad, max(values) * 1.2)


def calibrate(frames: list[dict]) -> dict:
    groups = collections.defaultdict(list)
    for frame in frames:
        if frame["kind"] == 1:
            groups[_stream(frame)].append(_shape(frame))
    models = {}
    for key, shapes in groups.items():
        if len(shapes) < 40:
            continue
        split = len(shapes) // 2
        reference = [statistics.mean(column) for column in zip(*shapes[:split])]
        held_out = shapes[split:]
        models[key] = {
            "reference": reference,
            "presence_threshold": _threshold(
                [_distance(shape, reference) for shape in held_out]
            ),
            "motion_threshold": _threshold(
                [_distance(a, b) for a, b in zip(held_out, held_out[1:])]
            ),
            "training_frames": split,
            "calibration_frames": len(held_out),
        }
    if not models:
        raise ValueError(
            "baseline needs at least 40 nonzero frames from one consistent CSI stream"
        )
    return models


def metrics(frames: list[dict]) -> dict:
    data = [frame for frame in frames if frame["kind"] == 1]
    groups = collections.defaultdict(list)
    for frame in frames:
        groups[(frame["source"], frame["session"])].append(frame)
    sessions = []
    for (source, session), events in groups.items():
        samples = [frame for frame in events if frame["kind"] == 1]
        ordinals = {frame["sent"] for frame in samples}
        final = [
            frame for frame in events if frame["kind"] == 2 and not frame["active"]
        ]
        counter = max(events, key=lambda frame: (frame["callbacks"], frame["sent"]))
        terminal_sent = max((frame["sent"] for frame in final), default=None)
        high = terminal_sent if terminal_sent is not None else max(ordinals, default=0)
        missing = max(0, high - len(ordinals))
        sessions.append(
            {
                "source": source,
                "session": session,
                "received_unique": len(ordinals),
                "duplicates": len(samples) - len(ordinals),
                "terminal_observed": bool(final),
                "missing_after_udp_accept": missing,
                "loss_fraction": missing / high if high else None,
                "loss_scope": (
                    "whole_session" if final else "through_last_received_ordinal"
                ),
                "callbacks": counter["callbacks"],
                "invalid": counter["invalid"],
                "queue_drops": counter["queue_drops"],
                "send_errors": counter["send_errors"],
                "pre_callback_radio_loss": "unknown: SDK exposes no transmitter frame count",
            }
        )
    intervals = []
    by_stream = collections.defaultdict(list)
    for frame in data:
        by_stream[(frame["session"], _stream(frame))].append(frame)
    for samples in by_stream.values():
        samples.sort(key=lambda frame: frame["sequence"])
        for a, b in zip(samples, samples[1:]):
            delta = (b["timestamp_us"] - a["timestamp_us"]) & 0xFFFFFFFF
            if 0 < delta < 0x80000000:
                intervals.append(delta / 1000)
    duration = (
        max((f["received_ns"] for f in data), default=0)
        - min((f["received_ns"] for f in data), default=0)
    ) / 1e9
    return {
        "frames": len(data),
        "length_counts": dict(collections.Counter(f["count"] for f in data)),
        "mean_receive_hz": (len(data) - 1) / duration if duration > 0 else None,
        "timestamp_interval_ms_median": (
            statistics.median(intervals) if intervals else None
        ),
        "timestamp_interval_ms_stdev": (
            statistics.pstdev(intervals) if intervals else None
        ),
        "sessions": sessions,
    }


def classify(frames: list[dict], models: dict, window_seconds: float) -> list[dict]:
    windows = collections.defaultdict(list)
    previous = {}
    seen = set()
    for frame in sorted(frames, key=lambda item: item["received_ns"]):
        if frame["kind"] != 1:
            continue
        identity = (frame["source"], frame["session"], frame["sequence"])
        if identity in seen:
            continue
        seen.add(identity)
        key = _stream(frame)
        model = models.get(key)
        if model is None:
            continue
        shape = _shape(frame)
        prior = previous.get((frame["session"], key))
        motion = None
        if prior is not None:
            gap = (frame["timestamp_us"] - prior[0]) & 0xFFFFFFFF
            if 0 < gap <= 250000:
                motion = _distance(shape, prior[1])
        previous[(frame["session"], key)] = (frame["timestamp_us"], shape)
        bucket = int(frame["uptime_ms"] / (window_seconds * 1000))
        windows[(frame["session"], key, bucket)].append(
            (
                _distance(shape, model["reference"]),
                motion,
            )
        )
    result = []
    for (session, key, bucket), values in windows.items():
        model = models[key]
        presence = statistics.median(value[0] for value in values)
        motion_values = [value[1] for value in values if value[1] is not None]
        motion = statistics.median(motion_values) if motion_values else None
        result.append(
            {
                "session": session,
                "stream": list(key),
                "window": bucket,
                "frames": len(values),
                "presence_score": presence,
                "motion_score": motion,
                "presence_candidate": presence > model["presence_threshold"],
                "motion_candidate": (
                    motion > model["motion_threshold"] if motion is not None else None
                ),
            }
        )
    return result


def analyze(args: argparse.Namespace) -> dict:
    if not math.isfinite(args.window_seconds) or not 0.1 <= args.window_seconds <= 60:
        raise ValueError("analysis window must be 0.1..60 seconds")
    models = None
    if args.baseline:
        label, frames = read_recording(args.baseline)
        if label != "empty":
            raise ValueError("baseline recording must be labeled empty")
        if any(path.resolve() == args.baseline.resolve() for path in args.input):
            raise ValueError(
                "use an independent capture for evaluation, not the baseline"
            )
        models = calibrate(frames)
    reports = []
    for path in args.input:
        label, frames = read_recording(path)
        report = {"input": str(path), "label": label, **metrics(frames)}
        if models is not None:
            windows = classify(frames, models, args.window_seconds)
            report["experimental_windows"] = windows
            report["unmatched_frames"] = sum(
                frame["kind"] == 1 and _stream(frame) not in models for frame in frames
            )
            if label != "unlabeled" and windows:
                report["presence_window_accuracy"] = statistics.mean(
                    row["presence_candidate"] == (label != "empty") for row in windows
                )
                motion = [row for row in windows if row["motion_candidate"] is not None]
                report["motion_window_accuracy"] = (
                    statistics.mean(
                        row["motion_candidate"] == (label == "moving") for row in motion
                    )
                    if motion
                    else None
                )
        reports.append(report)
    return {
        "schema": "bk7258-csi-analysis/1",
        "recordings": reports,
        "boundary": "Room-specific amplitude heuristics; stationary presence, IQ convention and RuView model transfer require real hardware evaluation.",
    }


def run(args: argparse.Namespace) -> dict:
    return receive(args) if args.csi_command == "receive" else analyze(args)
