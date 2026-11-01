#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Play a bounded PCM WAV through a selected Windows PyAudio output.

The helper emits metadata and monotonic timestamps only.  It deliberately
does not capture audio or print transcript content.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import time
import wave
from array import array
from pathlib import Path

import pyaudio


def _gain_s16le(data: bytes, gain: float) -> bytes:
    samples = array("h")
    samples.frombytes(data)
    if samples.itemsize != 2:
        raise RuntimeError("unexpected host int16 width")
    for index, value in enumerate(samples):
        scaled = round(value * gain)
        samples[index] = max(-32768, min(32767, scaled))
    return samples.tobytes()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wav", type=Path)
    parser.add_argument("--device", type=int, required=True)
    parser.add_argument("--gain", type=float, default=1.0)
    parser.add_argument("--chunk-frames", type=int, default=1024)
    args = parser.parse_args()

    if not 0 < args.gain <= 1.0:
        parser.error("--gain must be in (0, 1]")
    if args.chunk_frames <= 0:
        parser.error("--chunk-frames must be positive")

    payload = args.wav.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    audio = pyaudio.PyAudio()
    stream = None
    try:
        device = audio.get_device_info_by_index(args.device)
        with wave.open(str(args.wav), "rb") as source:
            channels = source.getnchannels()
            width = source.getsampwidth()
            rate = source.getframerate()
            frames = source.getnframes()
            if width != 2:
                raise RuntimeError("only 16-bit PCM WAV input is supported")
            if source.getcomptype() != "NONE":
                raise RuntimeError("compressed WAV input is not supported")
            if channels > int(device["maxOutputChannels"]):
                raise RuntimeError("selected device has too few output channels")

            stream = audio.open(
                format=audio.get_format_from_width(width),
                channels=channels,
                rate=rate,
                output=True,
                output_device_index=args.device,
                frames_per_buffer=args.chunk_frames,
                start=False,
            )
            started_ns = time.monotonic_ns()
            started_utc_ns = time.time_ns()
            stream.start_stream()
            while True:
                data = source.readframes(args.chunk_frames)
                if not data:
                    break
                stream.write(_gain_s16le(data, args.gain))
            stream.stop_stream()
            ended_ns = time.monotonic_ns()
    finally:
        if stream is not None:
            stream.close()
        audio.terminate()

    print(json.dumps({
        "schema": 1,
        "wav_sha256": digest,
        "device_index": args.device,
        "device_name": device["name"],
        "sample_rate_hz": rate,
        "channels": channels,
        "frames": frames,
        "gain": args.gain,
        "playback_started_monotonic_ns": started_ns,
        "playback_started_utc_ns": started_utc_ns,
        "playback_ended_monotonic_ns": ended_ns,
        "write_duration_ms": round((ended_ns - started_ns) / 1_000_000, 3),
    }, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
