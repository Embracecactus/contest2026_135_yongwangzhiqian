#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the public AIDK wake acknowledgement as raw 16 kHz PCM16LE."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


def generate() -> bytes:
    rate = 16000
    data = bytearray()
    # Two short, gently faded notes; synthesized locally, with no voice asset.
    for frequency in (660, 880):
        count = rate * 14 // 100
        fade = rate // 100
        for sample in range(count):
            envelope = min(1.0, sample / fade, (count - sample - 1) / fade)
            value = round(9000 * max(0.0, envelope) * math.sin(2 * math.pi * frequency * sample / rate))
            data.extend(struct.pack("<h", value))
        data.extend(b"\0\0" * (rate // 50))
    return bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(generate())


if __name__ == "__main__":
    main()
