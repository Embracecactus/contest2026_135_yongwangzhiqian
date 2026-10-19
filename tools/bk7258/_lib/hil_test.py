# SPDX-License-Identifier: Apache-2.0
"""Authenticated BKTEST codec and bounded native-USB HIL commands."""

from __future__ import annotations

from bisect import bisect_right
from datetime import datetime, timezone
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
import time
import uuid
import wave


def voice_inputs(*, wav=None, text=None, dialogue=None, repeat=1):
    """Read fixed input before opening hardware. PCM is never recaptured."""
    if sum(x is not None for x in (wav, text, dialogue)) != 1:
        raise HilTestError("Select exactly one WAV, text, or dialogue")
    if type(repeat) is not int or not 1 <= repeat <= 10:
        raise HilTestError("Voice benchmark repeat must be 1..10")
    if dialogue is not None:
        path = Path(dialogue)
        if path.stat().st_size > 65536:
            raise HilTestError("Dialogue input exceeds 64 KiB")
        entries = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(entries, list) or not 1 <= len(entries) * repeat <= 10:
            raise HilTestError("Dialogue must contain 1..10 total turns")
        result = []
        for entry in entries:
            if not isinstance(entry, dict) or set(entry) not in ({"text"}, {"wav"}):
                raise HilTestError("Each dialogue turn needs exactly text or wav")
            result += (
                voice_inputs(text=entry["text"])
                if "text" in entry
                else voice_inputs(wav=path.parent / entry["wav"])
            )
        return result * repeat
    if text is not None:
        if not isinstance(text, str) or not text.strip() or "\0" in text:
            raise HilTestError("Nonempty fixed UTF-8 text required")
        payload = text.encode("utf-8")
        if len(payload) > 4096:
            raise HilTestError("Voice text exceeds 4096 bytes")
        mode = 1
    else:
        path = Path(wav)
        if path.stat().st_size > 512000:
            raise HilTestError("WAV input exceeds 15 seconds plus bounded headers")
        try:
            with wave.open(str(path), "rb") as audio:
                if (
                    audio.getnchannels(),
                    audio.getsampwidth(),
                    audio.getframerate(),
                    audio.getcomptype(),
                ) != (1, 2, 16000, "NONE"):
                    raise HilTestError("WAV requires PCM16 little-endian, 16 kHz mono")
                frames = audio.getnframes()
                if not 1 <= frames <= 240000:
                    raise HilTestError("WAV duration must be 0..15 seconds")
                payload = audio.readframes(frames)
                if len(payload) != frames * 2:
                    raise HilTestError("Truncated WAV input")
        except (wave.Error, EOFError) as error:
            raise HilTestError("Invalid WAV input") from error
        mode = 2
    item = dict(
        mode=mode,
        payload=payload,
        sha256=hashlib.sha256(payload).hexdigest(),
        bytes=len(payload),
    )
    return [dict(item) for _ in range(repeat)]


def tts_isolation_input(path):
    """Read one immutable development TTS input before opening hardware."""
    path = Path(path)
    if path.stat().st_size > 4096:
        raise HilTestError("TTS isolation input exceeds 4096 bytes")
    payload = path.read_bytes()
    if not 1 <= len(payload) <= 4096 or b"\0" in payload:
        raise HilTestError("TTS isolation requires 1..4096 UTF-8 bytes without NUL")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise HilTestError("TTS isolation input must be valid UTF-8") from error
    if not text.strip():
        raise HilTestError("TTS isolation input must not be blank")
    return dict(payload=payload, bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())


def voice_packet(operation, session, sequence, mode, value=0, payload=b""):
    numbers = (operation, session, sequence, mode, value)
    if (
        any(type(x) is not int or not 0 <= x <= 0xFFFFFFFF for x in numbers)
        or operation not in (1, 2, 3, 4)
        or not session
        or not sequence
        or mode not in (0, 1, 2, 3)
        or not isinstance(payload, bytes)
        or len(payload) > 4096
    ):
        raise HilTestError("Invalid voice benchmark packet")
    return struct.pack(">4s7I", b"BVB1", 1, *numbers, len(payload)) + payload


def decode_voice_packet(wire):
    if len(wire) < 32:
        raise HilTestError("Invalid voice benchmark packet")
    magic, version, operation, session, sequence, mode, value, size = struct.unpack(
        ">4s7I", wire[:32]
    )
    if magic != b"BVB1" or version != 1 or len(wire) != 32 + size:
        raise HilTestError("Invalid voice benchmark packet")
    # Reuse the encoder's complete bounds contract without retaining the data.
    voice_packet(operation, session, sequence, mode, value, wire[32:])
    return dict(
        operation=operation,
        session=session,
        sequence=sequence,
        mode=mode,
        value=value,
        payload=wire[32:],
    )


def decode_voice_status(wire):
    if len(wire) != 64:
        raise HilTestError("Invalid voice benchmark status size")
    (
        magic,
        version,
        state,
        session,
        sequence,
        mode,
        total,
        received,
        high,
        low,
        result,
        canceled,
        borrowed,
        *reserved,
    ) = struct.unpack(">4s15I", wire)
    if (
        magic != b"BVS1"
        or version != 1
        or state > 6
        or mode > 3
        or received > total
        or canceled > 1
        or borrowed > 1
        or any(reserved)
    ):
        raise HilTestError("Invalid voice benchmark status")
    return dict(
        state=state,
        session=session,
        sequence=sequence,
        mode=mode,
        total=total,
        received=received,
        request_id=(high << 32) | low,
        result=result if result < 2**31 else result - 2**32,
        canceled=bool(canceled),
        borrowed=bool(borrowed),
    )


def voice_execute(
    client, *, session, mode, payload, timeout=180, cancel_after_ms=None, progress=None
):
    if (
        not isinstance(payload, bytes)
        or not payload
        or len(payload)
        > (12288 if mode == 0 else 480000 if mode == 2 else 4096)
    ):
        raise HilTestError("Invalid voice benchmark payload")
    if not 1 <= timeout <= 240 or (
        cancel_after_ms is not None and not 0 <= cancel_after_ms < timeout * 1000
    ):
        raise HilTestError("Invalid voice benchmark deadline")
    sequence = 1
    if progress is None:
        progress = {}
    progress.update(
        execution_performed=False,
        run_confirmed=False,
        cancel_confirmed=False,
        completion_verified=False,
        uploaded_bytes=0,
        last_confirmed_sequence=0,
        control_command_max_ms=0,
        status_poll_count=0,
        status_poll_max_ms=0,
        status_polls=[],
    )

    def send(op, value=0, part=b""):
        nonlocal sequence
        if op == 3:
            progress["execution_performed"] = None
        command_started = time.perf_counter()
        try:
            receipt = client.engineering_voice_command(
                voice_packet(op, session, sequence, mode, value, part)
            )
        finally:
            duration = round((time.perf_counter() - command_started) * 1000)
            progress["control_command_max_ms"] = max(
                progress["control_command_max_ms"], duration
            )
        sequence += 1
        if receipt.get("accepted") is not True:
            raise HilTestError("Voice benchmark command unconfirmed")
        progress["last_confirmed_sequence"] = sequence - 1
        if op == 2:
            progress["uploaded_bytes"] = value + len(part)
        if op == 3:
            progress["run_confirmed"] = True
        if op == 4:
            progress["cancel_confirmed"] = True

    upload_started = time.perf_counter()
    try:
        send(1, len(payload))
        for offset in range(0, len(payload), 4096):
            send(2, offset, payload[offset : offset + 4096])
    finally:
        progress["upload_ms"] = round((time.perf_counter() - upload_started) * 1000)
    send(3)
    started = time.monotonic()
    canceled = False

    def status_poll(started_at, outcome):
        duration_ms = round((time.monotonic() - started_at) * 1000)
        diagnostic = getattr(client, "last_engineering_voice_status", None)
        if not isinstance(diagnostic, dict):
            diagnostic = {}
        offset = diagnostic.get("offset")
        sequence = diagnostic.get("sdc_sequence")
        entry = {
            "start_ms": round((started_at - started) * 1000),
            "duration_ms": duration_ms,
            "offset": offset if type(offset) is int else None,
            "sdc_sequence": sequence if type(sequence) is int else None,
            "outcome": outcome,
        }
        progress["status_poll_count"] += 1
        progress["status_poll_max_ms"] = max(
            progress["status_poll_max_ms"], duration_ms
        )
        progress["status_polls"].append(entry)
        del progress["status_polls"][:-16]

    while True:
        poll_started = time.monotonic()
        try:
            observation = client.engineering_voice_status()
        except Exception as error:
            status_poll(poll_started, type(error).__name__)
            raise
        status_poll(poll_started, "ok")
        if observation["session"] != session or observation["mode"] != mode:
            raise HilTestError("Voice benchmark identity changed")
        progress["last_observation"] = dict(observation)
        if (
            observation["state"] in (4, 5)
            or observation["request_id"]
            or observation["borrowed"]
        ):
            progress["execution_performed"] = True
        if observation["state"] in (5, 6) and not observation["borrowed"]:
            progress["completion_verified"] = True
            return dict(
                observation,
                cancel_sent=canceled,
                host_completion_ms=round((time.monotonic() - started) * 1000),
            )
        elapsed = (time.monotonic() - started) * 1000
        if elapsed >= timeout * 1000:
            if not canceled:
                send(4)
            raise HilTestError("Voice benchmark deadline; completion unverified")
        if cancel_after_ms is not None and elapsed >= cancel_after_ms and not canceled:
            send(4)
            canceled = True
        time.sleep(0.05)


class HilTestError(ValueError):
    pass


def audio_stream_report(trace):
    """Report one sealed, numeric AP trace; TLS reads are not server clocks."""
    events = trace["events"]
    if len(events) > 8192:
        raise HilTestError("Audio trace exceeds device bound")
    # Kinds 9..13 retain the BVA2 record layout and are only emitted by the
    # development TTS isolation build. Keep them out of the established audio
    # metrics so an isolation span cannot change a production gap statistic.
    diagnostic_names = {
        9: "socket_recv",
        10: "socket_readiness_wait",
        11: "readiness_to_poll_teardown",
        12: "crypto_lock",
        13: "ssl_read",
    }
    by_kind = {kind: [] for kind in range(1, 14)}
    for seq, event in enumerate(events, 1):
        if (
            len(event) != 6
            or any(type(v) is not int or not 0 <= v < 2**32 for v in event)
            or event[0] != seq
            or event[1] not in by_kind
            or event[2] > event[3]
        ):
            raise HilTestError("Invalid or discontinuous audio trace")
        by_kind[event[1]].append(event)
    # Completion order differs between the two threads. Use entry times for
    # production gaps and exit times for completed transport reads.
    pcm = sorted(by_kind[4], key=lambda e: (e[2], e[0]))
    network = sorted((e for e in by_kind[2] if e[4]), key=lambda e: e[3])
    submits = sorted(by_kind[1], key=lambda e: e[2])

    def maximum(values):
        return max(values, default=None)

    def gaps(samples, column):
        return [b[column] - a[column] for a, b in zip(samples, samples[1:])]

    def ordinal(event):
        prior = [s for s in submits if s[2] <= event[2]]
        return prior[-1][4] if prior else None

    within, between = [], []
    chunks = []
    for index, event in enumerate(pcm):
        gap = event[2] - pcm[index - 1][2] if index else None
        segment = ordinal(event)
        if index:
            (within if segment == ordinal(pcm[index - 1]) else between).append(gap)
        chunks.append(
            dict(
                chunk=index + 1,
                tts_request=segment,
                ready_ms=event[2],
                queued_ms=event[3],
                bytes=event[4],
                queue_bytes=event[5],
                gap_ms=gap,
            )
        )
    complete = bool(
        trace["sealed"] and not trace["dropped"] and len(by_kind[8]) == 1 and submits
    )
    rearm = trace["rearm_state"]
    diagnostic_events = sorted(
        (event for kind in diagnostic_names for event in by_kind[kind]),
        key=lambda event: event[0],
    )
    diagnostic_spans = [
        dict(
            seq=event[0],
            kind=event[1],
            name=diagnostic_names[event[1]],
            begin_ms=event[2],
            end_ms=event[3],
            duration_ms=event[3] - event[2],
            result_bytes=event[4],
            error_code=event[5] or None,
        )
        for event in diagnostic_events
    ]
    return dict(
        turn=trace["request_id"],
        observation_complete=complete,
        dropped_records=trace["dropped"],
        clock="AP CLOCK_MONOTONIC ms",
        first_pcm_ms=(pcm[0][2] - submits[0][2] if pcm and submits else None),
        first_pcm_origin="first TTS submit",
        max_pcm_gap_ms=maximum(gaps(pcm, 2)) if len(pcm) > 1 else (0 if pcm else None),
        max_pcm_gap_within_tts_ms=max(within, default=0) if pcm else None,
        max_pcm_gap_between_tts_ms=maximum(between),
        network_gap_ms=maximum(gaps(network, 3)),
        network_read_max_ms=maximum(e[3] - e[2] for e in by_kind[2]),
        decode_callback_max_ms=maximum(e[3] - e[2] for e in by_kind[3]),
        queue_wait_max_ms=maximum(e[3] - e[2] for e in pcm),
        media_write_max_ms=maximum(e[3] - e[2] for e in by_kind[5]),
        media_gap_ms=maximum(e[4] for e in by_kind[8]),
        queue_peak=maximum([e[5] for e in pcm] + [e[5] for e in by_kind[5]]),
        buffer_underflow=len(by_kind[6]) if complete and by_kind[5] else None,
        underflow_scope="consumer empty wait while TTS request is unfinished; not DAC",
        dac_underruns=None,
        kws_rearm=None if not rearm else rearm == 2,
        rearm_attempts=trace["rearm_attempts"],
        rearm_ms=trace["rearm_ms"] or None,
        pcm_chunks=chunks,
        diagnostic_spans=diagnostic_spans,
        events=events,
        attribution="unresolved; correlate spans and sentence boundaries",
        limitations=[
            "TLS receive timestamps cannot separate network delay from server generation.",
            "Receive gaps can include local queue backpressure; compare read entry/exit.",
            "Queue empty waits are not physical DAC underruns or acoustic gaps.",
            "An incomplete trace cannot establish maxima or zero underflow.",
        ],
    )


def audio_gap_report(turns):
    """Locate observed waits without promoting receive time to server time."""
    output = []
    for turn in turns:
        events = turn["events"]
        pcm = sorted((e for e in events if e[1] == 4), key=lambda e: (e[2], e[0]))
        reads = sorted((e for e in events if e[1] == 2 and e[4]), key=lambda e: e[3])
        read_times = [e[3] for e in reads]
        media = sorted((e for e in events if e[1] == 5), key=lambda e: e[2])
        requests = sorted((e for e in events if e[1] == 1), key=lambda e: e[2])
        responses = {e[4]: e for e in events if e[1] == 7}

        def request_at(ms):
            prior = [e for e in requests if e[2] <= ms]
            return prior[-1][4] if prior else None

        # FIFO byte ranges associate PCM with the consuming Media submission.
        # A TLS association is weaker: it identifies only the latest prior read,
        # since the HTTP/parser buffer can retain bytes across callbacks.
        chunks = []
        produced = consumed = consumer = 0
        for event in pcm:
            while consumer < len(media) and consumed + media[consumer][4] <= produced:
                consumed += media[consumer][4]
                consumer += 1
            read_index = bisect_right(read_times, event[2]) - 1
            read = reads[read_index] if read_index >= 0 else None
            chunk_media = media[consumer] if consumer < len(media) else None
            chunks.append(
                dict(
                    seq=event[0],
                    tts_request=request_at(event[2]),
                    bytes=event[4],
                    tls_receive_time=read[3] if read else None,
                    tls_receive_seq=read[0] if read else None,
                    tts_ready_time=event[2],
                    queue_push_time=event[3],
                    media_consume_time=chunk_media[2] if chunk_media else None,
                    media_consume_seq=chunk_media[0] if chunk_media else None,
                    queue_depth=event[5],
                )
            )
            produced += event[4]

        empty_waits = []
        closed = next((e for e in events if e[1] == 8), None)
        for empty in (e for e in events if e[1] == 6):
            resumed = next((e for e in media if e[0] > empty[0]), None)
            stop = resumed[2] if resumed else (closed[2] if closed else None)
            empty_waits.append(
                dict(
                    seq=empty[0],
                    begin_ms=empty[2],
                    end_ms=stop,
                    duration_ms=max(0, stop - empty[2]) if stop is not None else None,
                    outcome=(
                        "resumed" if resumed else ("terminal" if closed else "unknown")
                    ),
                )
            )
        complete = turn["observation_complete"]
        gap = None
        classification = "insufficient_pcm"
        candidates = []
        if len(pcm) > 1:
            left, right = max(
                zip(pcm, pcm[1:]), key=lambda pair: pair[1][2] - pair[0][2]
            )
            begin, end = left[2], right[2]

            def overlap(spans):
                # Merge clipped intervals: double counting would manufacture
                # apparent causal coverage when callbacks are nested.
                intervals = sorted(
                    (max(begin, start), min(end, stop))
                    for start, stop in spans
                    if start < end and stop > begin
                )
                total, last = 0, begin
                for start, stop in intervals:
                    total += max(0, stop - max(last, start))
                    last = max(last, stop)
                return total

            network_ms = overlap((e[2], e[3]) for e in events if e[1] == 2)
            enqueue_ms = overlap((e[2], e[3]) for e in pcm)
            between_ms = overlap(
                (responses[e[4] - 1][3], e[2])
                for e in requests
                if e[4] - 1 in responses
            )
            callback = next(
                (
                    e
                    for e in events
                    if e[1] == 3 and e[0] > right[0] and e[2] <= end <= e[3]
                ),
                None,
            )
            prior_reads = [
                e
                for e in events
                if e[1] == 2 and e[0] < right[0] and callback and e[3] <= callback[2]
            ]
            prior_read = max(prior_reads, key=lambda e: e[3], default=None)
            same_request = request_at(begin) == request_at(end)
            # This locates time after the last receive has returned and before
            # the decoder emits PCM. It includes parsing, locks and descheduling;
            # it does not assert that this thread was executing throughout.
            post_receive_ms = (
                max(0, callback[2] - max(begin, prior_read[3]))
                if same_request and prior_read and prior_read[4]
                else None
            )
            gap = dict(
                previous_pcm_seq=left[0],
                next_pcm_seq=right[0],
                begin_ms=begin,
                end_ms=end,
                duration_ms=end - begin,
                same_tts_request=same_request,
                tls_read_overlap_ms=network_ms,
                enqueue_wait_overlap_ms=enqueue_ms,
                between_requests_ms=between_ms,
                post_receive_to_decode_ms=post_receive_ms,
                last_receive_seq=prior_read[0] if prior_read else None,
                next_decode_seq=callback[0] if callback else None,
                tls_read_sequences=[
                    e[0] for e in events if e[1] == 2 and e[2] < end and e[3] > begin
                ],
            )
            classification, candidates = "unresolved", ["A", "B", "C", "D"]
            if end > begin and network_ms * 5 >= (end - begin) * 4:
                # The measured call includes time when its device thread
                # cannot run. Entry/return timestamps do not prove which
                # peer, transport or scheduler made it wait.
                classification, candidates = "receive_call_wait", ["A", "B", "C"]
            elif end > begin and enqueue_ms * 5 >= (end - begin) * 4:
                classification, candidates = "queue_backpressure", ["C", "D"]
            elif (
                end > begin
                and post_receive_ms is not None
                and post_receive_ms * 5 >= (end - begin) * 4
                and enqueue_ms == 0
            ):
                classification, candidates = "device_pre_pcm_no_receive", ["C"]
        if not turn["observation_complete"]:
            classification, candidates = "incomplete", []
        output.append(
            dict(
                turn_id=turn["turn"],
                turn_uid=turn.get("turn_uid"),
                observation_complete=turn["observation_complete"],
                input_sha256=turn.get("input_sha256"),
                classification=classification,
                candidate_sources=candidates,
                root_cause=None,
                max_gap=gap,
                first_pcm_ms=turn["first_pcm_ms"],
                queue_empty_wait_count=turn["buffer_underflow"],
                queue_starvation_count=(
                    sum(
                        e["outcome"] == "resumed" and e["duration_ms"] > 0
                        for e in empty_waits
                    )
                    if complete
                    else None
                ),
                queue_tail_wait_count=(
                    sum(e["outcome"] == "terminal" for e in empty_waits)
                    if complete
                    else None
                ),
                queue_empty_waits=empty_waits,
                queue_empty_observed_ms=[e[2] for e in events if e[1] == 6],
                pcm_bytes_produced=produced,
                media_bytes_submitted=sum(e[4] for e in media),
                queue_drained=(
                    produced == sum(e[4] for e in media)
                    if complete and closed
                    else None
                ),
                queue_peak=turn["queue_peak"],
                consumer_ready_gap_ms=turn["media_gap_ms"],
                media_write_max_ms=turn["media_write_max_ms"],
                kws_rearm=turn["kws_rearm"],
                rearm_attempts=turn["rearm_attempts"],
                pcm_chunks=chunks,
            )
        )
    return dict(
        schema="shaniu.audio-gap-report/1",
        clock="AP CLOCK_MONOTONIC ms",
        sources=dict(
            A="cloud/TTS",
            B="TLS/network",
            C="device processing/PCM queue",
            D="Media consumer",
        ),
        wait_classification_minimum_coverage=0.8,
        limitations=[
            "Wait coverage locates blocking; it is not a proven root cause.",
            "TLS read spans cannot separate cloud generation, network and TLS scheduling.",
            "Per-chunk TLS association is the latest preceding read, not byte provenance.",
            "Media timestamps map FIFO bytes to submission entry, not DAC or acoustics.",
            "Queue push is callback completion; a blocked callback can enqueue in parts.",
            "Queue backpressure can be normal audio pacing; it is not proof of starvation.",
            "Starvation counts positive empty-wait durations followed by Media submission; terminal empty waits are separate.",
            "Device pre-PCM time includes parsing, locks and descheduling; CPU time is unknown.",
        ],
        turns=output,
    )


def compare_audio_stream(current, baseline):
    """Compare equal file inputs; changed network/answers still need review."""
    fields = (
        "first_pcm_ms",
        "max_pcm_gap_ms",
        "max_pcm_gap_within_tts_ms",
        "max_pcm_gap_between_tts_ms",
        "network_read_max_ms",
        "queue_wait_max_ms",
        "media_gap_ms",
        "buffer_underflow",
        "queue_peak",
    )
    before = baseline.get("turns", [])
    result = []
    for index, turn in enumerate(current):
        prior = before[index] if index < len(before) else {}
        comparable = bool(
            turn.get("input_sha256")
            and turn.get("input_sha256") == prior.get("input_sha256")
            and turn.get("observation_complete")
            and prior.get("observation_complete")
        )
        result.append(
            dict(
                turn=turn["turn"],
                comparable=comparable,
                delta={
                    field: (
                        turn[field] - prior[field]
                        if comparable
                        and turn.get(field) is not None
                        and prior.get(field) is not None
                        else None
                    )
                    for field in fields
                },
            )
        )
    return result


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


def voice_report(log, *, boot_id, input_marks=()):
    """Aggregate one boot's existing ledger, without inventing missing clocks.

    This is the reporting half of the HIL benchmark. It never starts a turn,
    infers acoustic onset from Media, or treats untagged queue logs as per-turn
    driver evidence. Input annotations must already be aligned to this AP boot.
    """
    if not isinstance(boot_id, str) or not re.fullmatch(
        r"[A-Za-z0-9_.-]{1,80}", boot_id
    ):
        raise HilTestError("Explicit boot identity required")
    if not isinstance(log, str) or len(log) > 32 * 1024 * 1024:
        raise HilTestError("Invalid or oversized voice log")
    pattern = re.compile(
        r"\[voice-stage\] request=(\d+) mono_ms=(\d+) "
        r"stage=([a-z_]{1,48}) value=(\d+) result=(-?\d+)(?=\s|$)"
    )
    groups = {}
    background = 0
    for line in log.splitlines():
        if "[voice-stage]" not in line:
            continue
        match = pattern.search(line)
        if match is None or line.count("[voice-stage]") != 1:
            raise HilTestError("Malformed or interleaved voice trace")
        request, ms, stage, value, result = match.groups()
        request, ms, value, result = int(request), int(ms), int(value), int(result)
        if max(request, ms, value) >= 2**64 or not -(2**31) <= result < 2**31:
            raise HilTestError("Voice trace integer out of range")
        if request == 0:
            background += 1
            continue
        events = groups.setdefault(request, [])
        events.append(dict(mono_ms=ms, stage=stage, value=value, result=result))

    marks = {}
    if not isinstance(input_marks, (list, tuple)):
        raise HilTestError("Voice input annotations must be an array")
    for mark in input_marks:
        if not isinstance(mark, dict):
            raise HilTestError("Invalid voice input annotation")
        request = mark.get("request_id")
        if type(request) is not int or request not in groups or request in marks:
            raise HilTestError("Duplicate or unattributed voice input annotation")
        if mark.get("clock_id") != "ap:" + boot_id:
            raise HilTestError("Voice annotations require the same aligned AP clock")
        if mark.get("input_mode") not in ("wav", "text", "acoustic"):
            raise HilTestError("Explicit voice input mode required")
        for field in ("input_end_mono_ms", "uncertainty_ms"):
            number = mark.get(field)
            if type(number) is not int or not 0 <= number < 2**64:
                raise HilTestError("Invalid voice annotation timestamp or uncertainty")
        if (
            not isinstance(mark.get("input_evidence"), str)
            or not mark["input_evidence"]
        ):
            raise HilTestError("Input-end evidence reference required")
        audio = mark.get("effective_audio_mono_ms")
        if audio is not None and (
            type(audio) is not int
            or not mark["input_end_mono_ms"] <= audio < 2**64
            or not isinstance(mark.get("audio_evidence"), str)
            or not mark["audio_evidence"]
        ):
            raise HilTestError("Aligned effective-answer acoustic evidence required")
        marks[request] = mark
        dac = mark.get("dac_output_mono_ms")
        if dac is not None and (
            type(dac) is not int
            or not mark["input_end_mono_ms"] <= dac < 2**64
            or not isinstance(mark.get("dac_evidence"), str)
            or not mark["dac_evidence"]
        ):
            raise HilTestError("Aligned DAC output evidence required")

    turns = []
    for request, events in groups.items():
        # UART output from different workers may be interleaved. Order the
        # embedded AP timestamps, not host receive times or line positions.
        events.sort(key=lambda event: event["mono_ms"])
        indexed = {}
        for event in events:
            indexed.setdefault(event["stage"], []).append(event)
        for stage in (
            "endpoint_end",
            "asr_final",
            "first_pcm",
            "first_media",
            "channel_idle",
        ):
            if len(indexed.get(stage, ())) > 1:
                raise HilTestError("Reused turn identity; split captures by boot")
        closed = indexed.get("channel_idle", [])
        if closed and any(
            event["mono_ms"] > closed[0]["mono_ms"]
            and event["stage"]
            in (
                "endpoint_end",
                "asr_request",
                "route_request",
                "plan_request",
                "tts_submit",
            )
            for event in events
        ):
            raise HilTestError("Voice work after terminal event; check boot identity")
        mark = marks.get(request, {})
        injected = indexed.get("input_pcm", []) + indexed.get("input_text", [])
        if len(injected) > 1:
            raise HilTestError("Duplicate injected input identity")
        if injected and not mark:
            sample = injected[0]
            mark = dict(
                input_mode="wav" if sample["stage"] == "input_pcm" else "text",
                input_end_mono_ms=sample["mono_ms"],
                uncertainty_ms=0,
                input_evidence="AP ledger: " + sample["stage"],
            )
        origin = mark.get("input_end_mono_ms")

        def first(stage):
            return next((e for e in indexed.get(stage, ()) if e["result"] == 0), None)

        def offset(stage):
            event = first(stage)
            return (
                None if origin is None or event is None else event["mono_ms"] - origin
            )

        mode = mark.get("input_mode")
        terminal = closed[0]["result"] if closed else None
        state = (
            "incomplete"
            if terminal is None
            else (
                "completed"
                if terminal == 0
                else "canceled" if terminal == -125 else "failed"
            )
        )
        turn = dict(
            boot_id=boot_id,
            turn_id=request,
            input_mode=mode,
            latency_origin=(
                "injected-pcm-ready"
                if injected and mode == "wav"
                else "text-submit" if mode == "text" else "input-end" if mark else None
            ),
            input_end_mono_ms=origin,
            uncertainty_ms=mark.get("uncertainty_ms"),
            input_evidence=mark.get("input_evidence"),
            audio_evidence=mark.get("audio_evidence"),
            result=state,
            terminal_result=terminal,
            cancel_result=-125 if terminal == -125 else None,
            events=events,
        )
        stages = {
            "asr_start_ms": "asr_request",
            "asr_final_ms": "asr_final",
            "agent_ready_ms": "agent_ready",
            "llm_first_token_ms": "llm_first_token",
            "body_first_character_ms": "final_first",
            "first_sentence_submit_ms": "tts_submit",
            "tts_first_pcm_ms": "first_pcm",
            "media_first_ms": "first_media",
            "dac_start_ms": "dac_first_pcm",
            "turn_complete_ms": "channel_idle",
        }
        turn.update({field: offset(stage) for field, stage in stages.items()})
        dac = mark.get("dac_output_mono_ms")
        turn["dac_evidence"] = mark.get("dac_evidence")
        if dac is not None:
            pcm = first("first_pcm")
            if (pcm and dac < pcm["mono_ms"]) or (
                closed and dac > closed[0]["mono_ms"]
            ):
                raise HilTestError("DAC annotation outside voice output lifecycle")
            turn["dac_start_ms"] = dac - origin
        final_request = first("final_request") or first("final_first")
        decisions = (
            list(indexed.get("plan_response", ()))
            + list(indexed.get("voice_phase_final", ()))
            + [
                event
                for event in indexed.get("route_response", ())
                if event["value"] == 1
            ]
        )
        plans = [
            event
            for event in decisions
            if event["result"] == 0
            and final_request is not None
            and event["mono_ms"] <= final_request["mono_ms"]
        ]
        turn["planning_complete_ms"] = (
            max(event["mono_ms"] for event in plans) - origin
            if plans and origin is not None
            else None
        )
        # ASR can start before input end. A successful empty transcript is
        # insufficient evidence of a usable final semantic input.
        final_asr = first("asr_final")
        if final_asr is None or not final_asr["value"]:
            turn["asr_final_ms"] = None
        audio = mark.get("effective_audio_mono_ms")
        turn["total_ms"] = (
            audio - origin
            if audio is not None and mode != "text" and not injected
            else None
        )
        turn["requests"] = {
            name: len(indexed.get(stage, ()))
            for name, stage in (
                ("asr", "asr_request"),
                ("routing", "route_request"),
                ("voice_stream", "voice_stream_request"),
                ("planning", "plan_request"),
                ("final", "final_request"),
                ("tts", "tts_submit"),
            )
        }
        # Queue occupancy is observed even when synthesis ends in cancel/error.
        peaks = indexed.get("queue_peak", ())
        turn["queue_peak_bytes"] = max(e["value"] for e in peaks) if peaks else None
        for field in (
            "pcm_max_gap_ms",
            "network_max_gap_ms",
            "queue_rebuffers",
            "dac_underruns",
        ):
            turn[field] = None
        for field, stage in (
            ("pcm_max_gap_ms", "pcm_gap_max"),
            ("queue_rebuffers", "queue_rebuffers"),
        ):
            samples = indexed.get(stage, ())
            if samples:
                turn[field] = max(event["value"] for event in samples)
        rearms = indexed.get("rearm", ())
        turn["kws_rearm"] = (
            bool(rearms[-1]["value"]) and rearms[-1]["result"] == 0 if rearms else None
        )
        turn["rearm_attempts"] = len(rearms)
        turn["rearm_failures"] = sum(
            event["result"] != 0 or not event["value"] for event in rearms
        )
        for field, stage in (
            ("asr_released", "asr_released"),
            ("agent_idle", "agent_idle"),
            ("tts_released", "tts_released"),
            ("media_closed", "media_closed"),
        ):
            event = first(stage)
            turn[field] = True if event else None
        turns.append(turn)
    return dict(
        format=1,
        boot_id=boot_id,
        measurement_source="captured-log-and-annotations",
        turns=turns,
        background_events=background,
        execution_performed=False,
        limitations=[
            "Missing observations remain null; Media is not acoustic onset.",
            "Request counts cover captured events only; stream preconnect is not an ASR request.",
            "Untagged queue gaps and rebuffer counts are not per-turn DAC underruns.",
        ],
    )


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


def report(
    *,
    test,
    result,
    firmware,
    git_sha,
    command,
    input_sequence,
    observation,
    failure_reason=None,
):
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
        "operation",
        choices=("enroll", "status", "key", "audio", "revoke", "voice-report"),
    )
    workbench.add_connection_arguments(parser)
    parser.add_argument("--console-port")
    parser.add_argument("--git-sha", required=True)
    parser.add_argument("--session", type=int, default=1)
    parser.add_argument("--held-ms", type=int, default=3000)
    parser.add_argument("--pm-mode", choices=tuple(PM_MODES), default="blocked")
    parser.add_argument(
        "--from-log", type=Path, help="one boot's captured voice-stage log"
    )
    parser.add_argument(
        "--boot-id", help="explicit capture boot identity for voice-report"
    )
    parser.add_argument(
        "--input-marks", type=Path, help="aligned input/acoustic annotations JSON"
    )
    parser.add_argument(
        "--output-dir", type=Path, help="new directory for voice-report artifacts"
    )


def add_benchmark_arguments(parser):
    from . import workbench

    groups = parser.add_subparsers(dest="test_group", required=True)
    voice = groups.add_parser("voice").add_subparsers(dest="test_action", required=True)
    benchmark = voice.add_parser(
        "benchmark", help="run fixed inputs on the existing BKTEST voice channel"
    )
    workbench.add_connection_arguments(benchmark, port_required=True)
    inputs = benchmark.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--wav", type=Path)
    inputs.add_argument("--text")
    inputs.add_argument("--dialogue", type=Path)
    benchmark.add_argument("--repeat", type=int, default=1)
    benchmark.add_argument(
        "--audio-status",
        action="store_true",
        help="read independent USB rearm status without exporting chunk records",
    )
    benchmark.add_argument(
        "--audio-trace",
        action="store_true",
        help="read sealed engineering PCM timing and rearm over authenticated USB",
    )
    benchmark.add_argument(
        "--audio-baseline",
        type=Path,
        help="compare complete traces with identical file input hashes",
    )
    benchmark.add_argument(
        "--cloud-config",
        type=Path,
        help="private development JSON referencing key and CA DER files",
    )
    benchmark.add_argument("--turn-timeout", type=int, default=180)
    benchmark.add_argument("--cancel-after-ms", type=int)
    benchmark.add_argument("--git-sha", required=True)
    benchmark.add_argument("--output-dir", type=Path, required=True)
    isolation = voice.add_parser(
        "voice_tts_isolation",
        help="run one fixed development TTS input without ASR or LLM",
    )
    workbench.add_connection_arguments(isolation, port_required=True)
    isolation.add_argument(
        "--input-text", type=Path, required=True, help="immutable UTF-8 input file"
    )
    isolation.add_argument(
        "--audio-status",
        action="store_true",
        help="read independent USB rearm status without exporting chunk records",
    )
    isolation.add_argument(
        "--audio-trace",
        action="store_true",
        help="read sealed engineering PCM timing and rearm over authenticated USB",
    )
    isolation.add_argument(
        "--cloud-config",
        type=Path,
        help="private development JSON referencing key and CA DER files",
    )
    isolation.add_argument("--turn-timeout", type=int, default=180)
    isolation.add_argument("--git-sha", required=True)
    isolation.add_argument("--output-dir", type=Path, required=True)


def _benchmark_cloud(path):
    import ipaddress
    import socket
    from urllib.parse import urlsplit
    from . import voice

    voice._regular(path, "development cloud configuration", private=True)
    if path.stat().st_size > 16384:
        raise HilTestError("Development cloud configuration too large")
    config = json.loads(path.read_text(encoding="utf-8"))
    endpoint = urlsplit(config["base_url"])
    if (
        endpoint.scheme != "https"
        or not endpoint.hostname
        or endpoint.username
        or endpoint.password
        or endpoint.query
        or endpoint.fragment
    ):
        raise HilTestError("Credential-free HTTPS cloud endpoint required")
    key_file = Path(config["key_file"])
    voice._regular(key_file, "cloud credential", private=True)
    if key_file.stat().st_size > 4096:
        raise HilTestError("Cloud credential too large")
    key = key_file.read_text(encoding="ascii").strip()
    fields = [
        endpoint.hostname,
        endpoint.path.rstrip("/") or "/",
        key,
        config["asr_model"],
        config["chat_model"],
        config["tts_model"],
    ]
    encoded = [value.encode("ascii") for value in fields]
    if any(
        not value
        or len(value) > (4096 if index == 2 else 127)
        or any(c < 33 or c > 126 for c in value)
        for index, value in enumerate(encoded)
    ):
        raise HilTestError("Invalid cloud configuration fields")
    dialect = config["dialect"]
    if dialect not in (1, 2):
        raise HilTestError("Cloud dialect must be 1 or 2")
    cloud = struct.pack(
        ">4sBBH6H4x",
        b"CCF1",
        dialect,
        0,
        endpoint.port or 443,
        *(len(value) for value in encoded),
    ) + b"".join(encoded)
    ca_path = Path(config["ca_der_file"])
    if not 0 < ca_path.stat().st_size <= 4096:
        raise HilTestError("Bounded CA DER file required")
    ca = ca_path.read_bytes()
    address = ipaddress.IPv4Address(socket.gethostbyname(endpoint.hostname)).packed
    record = (
        struct.pack(
            ">4sIQII4s4x", b"BVC2", 1, int(time.time()), len(ca), len(cloud), address
        )
        + ca
        + cloud
    )
    public = dict(
        base_url=config["base_url"],
        dialect=dialect,
        models=fields[3:],
        ca_sha256=hashlib.sha256(ca).hexdigest(),
    )
    return record, public


def run_benchmark(args):
    baseline = None
    if getattr(args, "audio_baseline", None):
        if (
            not args.audio_trace
            or args.audio_baseline.stat().st_size > 16 * 1024 * 1024
        ):
            raise HilTestError(
                "Audio baseline requires --audio-trace and a bounded report"
            )
        baseline = json.loads(args.audio_baseline.read_text(encoding="utf-8"))
        if baseline.get("schema") != "shaniu.audio-stream-benchmark/1":
            raise HilTestError("Unsupported audio benchmark baseline")
    from . import workbench

    if not re.fullmatch(r"[0-9a-f]{40}", args.git_sha):
        raise HilTestError("Invalid source identity")
    turns = voice_inputs(
        wav=args.wav, text=args.text, dialogue=args.dialogue, repeat=args.repeat
    )
    cloud, cloud_public = (
        _benchmark_cloud(args.cloud_config) if args.cloud_config else (None, None)
    )
    args.output_dir.mkdir(parents=True, exist_ok=False)
    result = dict(
        schema="shaniu.voice-benchmark/1",
        run_id=uuid.uuid4().hex,
        execution_performed=False,
        declared_source_sha=args.git_sha,
        cloud=cloud_public,
        turns=[],
        measurement_scope="injected completed input; microphone and endpoint bypassed",
        latency_source="AP voice-stage UART ledger; merge with hil-test voice-report",
        total_ms=None,
    )
    path = args.output_dir / "benchmark.json"

    def save():
        path.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )

    try:
        with workbench.authorized_client(args) as client:
            result["firmware"] = client.info()
            session = client.engineering_voice_status()["session"] + 1
            if cloud is not None:
                result["configuration"] = voice_execute(
                    client, session=session, mode=0, payload=cloud, timeout=30
                )
                cloud = None
                if result["configuration"]["result"]:
                    raise HilTestError(
                        "Development cloud setup failed; inspect numeric receipt"
                    )
                session += 1
            save()
            for index, turn in enumerate(turns):
                entry = {key: value for key, value in turn.items() if key != "payload"}
                entry.update(
                    turn_index=index + 1,
                    turn_uid=f"{result['run_id']}:{index + 1}",
                    session=session,
                    total_ms=None,
                    progress={},
                )
                result["turns"].append(entry)
                save()
                entry["observation"] = voice_execute(
                    client,
                    session=session,
                    mode=turn["mode"],
                    payload=turn["payload"],
                    timeout=args.turn_timeout,
                    cancel_after_ms=args.cancel_after_ms,
                    progress=entry["progress"],
                )
                if (
                    getattr(args, "audio_trace", False)
                    or getattr(args, "audio_status", False)
                ) and entry["observation"]["request_id"]:
                    request_id = entry["observation"]["request_id"]
                    deadline = time.monotonic() + 10
                    while True:
                        header = client.engineering_audio_trace(
                            request_id, header_only=True
                        )
                        if header["rearm_state"] == 2 or time.monotonic() >= deadline:
                            break
                        time.sleep(0.1)
                    entry["audio_status"] = header
                if (
                    getattr(args, "audio_trace", False)
                    and entry["observation"]["request_id"]
                ):
                    entry["audio_readback"] = {}
                    read_started = time.monotonic()
                    try:
                        trace = client.engineering_audio_trace(
                            request_id, progress=entry["audio_readback"]
                        )
                    finally:
                        entry["audio_readback"]["duration_ms"] = round(
                            (time.monotonic() - read_started) * 1000
                        )
                    entry["audio_stream"] = audio_stream_report(trace)
                    entry["audio_stream"].update(
                        input_sha256=turn["sha256"],
                        input_mode=turn["mode"],
                        turn_uid=entry["turn_uid"],
                    )
                    audio_path = args.output_dir / "audio_stream_benchmark.json"
                    audio_path.write_text(
                        json.dumps(
                            dict(
                                schema="shaniu.audio-stream-benchmark/1",
                                run_id=result["run_id"],
                                firmware=result["firmware"],
                                declared_source_sha=args.git_sha,
                                evidence_scope="file-input device measurement; not acoustic",
                                comparison=(
                                    compare_audio_stream(
                                        [
                                            t["audio_stream"]
                                            for t in result["turns"]
                                            if "audio_stream" in t
                                        ],
                                        baseline,
                                    )
                                    if baseline is not None
                                    else None
                                ),
                                turns=[
                                    t["audio_stream"]
                                    for t in result["turns"]
                                    if "audio_stream" in t
                                ],
                            ),
                            indent=2,
                        )
                        + "\n",
                        encoding="utf-8",
                    )
                    gap_path = args.output_dir / "audio_gap_report.json"
                    gap_path.write_text(
                        json.dumps(
                            audio_gap_report(
                                [
                                    t["audio_stream"]
                                    for t in result["turns"]
                                    if "audio_stream" in t
                                ]
                            ),
                            indent=2,
                        )
                        + "\n",
                        encoding="utf-8",
                    )
                save()
                expected = -125 if args.cancel_after_ms is not None else 0
                if entry["observation"]["result"] != expected:
                    raise HilTestError(
                        "Voice turn failed or cancellation was not observed"
                    )
                session += 1
    finally:
        cloud = None
        executions = [
            turn["progress"].get("execution_performed", False)
            for turn in result["turns"]
        ]
        result["execution_performed"] = (
            True if True in executions else None if None in executions else False
        )
        save()
        files = [path]
        for name in ("audio_stream_benchmark.json", "audio_gap_report.json"):
            audio_path = args.output_dir / name
            if audio_path.exists():
                files.append(audio_path)
        (args.output_dir / "SHA256SUMS.txt").write_text(
            "".join(
                hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n"
                for p in files
            ),
            encoding="ascii",
        )
    return dict(
        turns=len(result["turns"]),
        execution_performed=result["execution_performed"],
        output_directory=str(args.output_dir),
        total_ms=None,
    )


def run_tts_isolation(args):
    """Run one fixed TTS-only development turn on the existing BKTEST channel."""
    from . import workbench

    if not re.fullmatch(r"[0-9a-f]{40}", args.git_sha):
        raise HilTestError("Invalid source identity")
    source = tts_isolation_input(args.input_text)
    cloud, cloud_public = (
        _benchmark_cloud(args.cloud_config) if args.cloud_config else (None, None)
    )
    args.output_dir.mkdir(parents=True, exist_ok=False)
    result = dict(
        schema="shaniu.voice-tts-isolation/1",
        run_id=uuid.uuid4().hex,
        mode="tts",
        execution_performed=False,
        declared_source_sha=args.git_sha,
        cloud=cloud_public,
        input={key: source[key] for key in ("bytes", "sha256")},
        total_ms=None,
        measurement_scope="fixed UTF-8 TTS input; ASR and LLM are not invoked",
        latency_source="AP voice-stage UART ledger; merge with hil-test voice-report",
    )
    path = args.output_dir / "voice_tts_isolation.json"

    def save():
        path.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )

    try:
        with workbench.authorized_client(args) as client:
            result["firmware"] = client.info()
            session = client.engineering_voice_status()["session"] + 1
            if cloud is not None:
                result["configuration"] = voice_execute(
                    client, session=session, mode=0, payload=cloud, timeout=30
                )
                cloud = None
                if result["configuration"]["result"]:
                    raise HilTestError(
                        "Development cloud setup failed; inspect numeric receipt"
                    )
                session += 1
            save()
            result["session"] = session
            result["progress"] = {}
            result["observation"] = voice_execute(
                client,
                session=session,
                mode=3,
                payload=source["payload"],
                timeout=args.turn_timeout,
                progress=result["progress"],
            )
            result["execution_performed"] = result["progress"]["execution_performed"]
            if result["observation"]["result"]:
                raise HilTestError("TTS isolation turn failed")
            request_id = result["observation"]["request_id"]
            if (args.audio_trace or args.audio_status) and request_id:
                deadline = time.monotonic() + 10
                while True:
                    header = client.engineering_audio_trace(request_id, header_only=True)
                    if header["rearm_state"] == 2 or time.monotonic() >= deadline:
                        break
                    time.sleep(0.1)
                result["audio_status"] = header
            if args.audio_trace and request_id:
                result["audio_readback"] = {}
                read_started = time.monotonic()
                try:
                    trace = client.engineering_audio_trace(
                        request_id, progress=result["audio_readback"]
                    )
                finally:
                    result["audio_readback"]["duration_ms"] = round(
                        (time.monotonic() - read_started) * 1000
                    )
                result["audio_stream"] = audio_stream_report(trace)
                result["audio_stream"].update(
                    input_sha256=source["sha256"], input_mode="tts", run_id=result["run_id"]
                )
    finally:
        cloud = None
        save()
        files = [path]
        if "audio_stream" in result:
            audio_path = args.output_dir / "audio_stream_benchmark.json"
            audio_path.write_text(
                json.dumps(
                    dict(
                        schema="shaniu.audio-stream-benchmark/1",
                        run_id=result["run_id"],
                        firmware=result.get("firmware"),
                        declared_source_sha=args.git_sha,
                        evidence_scope="fixed TTS input device measurement; not acoustic",
                        turns=[result["audio_stream"]],
                    ),
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            files.append(audio_path)
        (args.output_dir / "SHA256SUMS.txt").write_text(
            "".join(
                hashlib.sha256(item.read_bytes()).hexdigest() + "  " + item.name + "\n"
                for item in files
            ),
            encoding="ascii",
        )
    return dict(
        turns=1,
        execution_performed=result["execution_performed"],
        output_directory=str(args.output_dir),
        total_ms=None,
    )


def run_voice_test(args):
    if args.test_action == "benchmark":
        return run_benchmark(args)
    if args.test_action == "voice_tts_isolation":
        return run_tts_isolation(args)
    raise HilTestError("Unsupported BKTEST voice action")


def _voice_report_files(args):
    if args.from_log is None or args.output_dir is None or not args.boot_id:
        raise HilTestError(
            "voice-report requires --from-log, --boot-id and --output-dir"
        )
    if args.from_log.stat().st_size > 32 * 1024 * 1024:
        raise HilTestError("Voice log exceeds 32 MiB; split captures by boot")
    raw = args.from_log.read_bytes()
    marks = None
    if args.input_marks is not None:
        if args.input_marks.stat().st_size > 1024 * 1024:
            raise HilTestError("Voice input annotation file exceeds 1 MiB")
        marks = args.input_marks.read_bytes()
    result = voice_report(
        raw.decode("utf-8", errors="replace"),
        boot_id=args.boot_id,
        input_marks=json.loads(marks) if marks is not None else (),
    )
    result.update(
        input_log_sha256=hashlib.sha256(raw).hexdigest(),
        annotations_sha256=(
            hashlib.sha256(marks).hexdigest() if marks is not None else None
        ),
        declared_source_sha=args.git_sha,
    )
    # Never overwrite a measured baseline or an earlier failed run.
    args.output_dir.mkdir(parents=True, exist_ok=False)
    json_path = args.output_dir / "voice_latency.json"
    csv_path = args.output_dir / "turn_latency.csv"
    json_path.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    columns = [
        "boot_id",
        "turn_id",
        "input_mode",
        "latency_origin",
        "input_end_mono_ms",
        "uncertainty_ms",
        "asr_start_ms",
        "asr_final_ms",
        "agent_ready_ms",
        "planning_complete_ms",
        "llm_first_token_ms",
        "body_first_character_ms",
        "first_sentence_submit_ms",
        "tts_first_pcm_ms",
        "media_first_ms",
        "dac_start_ms",
        "total_ms",
        "turn_complete_ms",
        "queue_peak_bytes",
        "queue_rebuffers",
        "pcm_max_gap_ms",
        "network_max_gap_ms",
        "dac_underruns",
        "cancel_result",
        "kws_rearm",
        "asr_released",
        "agent_idle",
        "tts_released",
        "media_closed",
        "result",
        "asr_requests",
        "routing_requests",
        "voice_stream_requests",
        "planning_requests",
        "final_requests",
        "tts_requests",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for turn in result["turns"]:
            writer.writerow(
                {
                    **turn,
                    **{
                        key + "_requests": value
                        for key, value in turn["requests"].items()
                    },
                }
            )
    hashes = "".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
        for path in (json_path, csv_path)
    )
    (args.output_dir / "SHA256SUMS.txt").write_text(hashes, encoding="utf-8")
    return dict(
        operation="voice-report",
        execution_performed=False,
        turns=len(result["turns"]),
        acoustic_samples=sum(turn["total_ms"] is not None for turn in result["turns"]),
        output_directory=str(args.output_dir),
    )


def run(args, command):
    if not re.fullmatch(r"[0-9a-f]{40}", args.git_sha):
        raise HilTestError("Invalid source identity")
    if args.operation == "voice-report":
        return _voice_report_files(args)
    if not getattr(args, "port", None):
        raise HilTestError("Live HIL operations require --port")

    from . import factory_diagnostics, workbench

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
