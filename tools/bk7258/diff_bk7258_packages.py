#!/usr/bin/env python3
"""Read-only black-box differential of two BK7258 boot images.

Usage (no writes, no device access):

  python3 diff_bk7258_packages.py --official FILE_A --own FILE_B [options]

  # also accept a raw (pre-CRC) own image alongside a CRC-expanded one:
  python3 diff_bk7258_packages.py --official flat.bin --own bl_crc.bin \
      --own-raw bl.bin

The tool:
  1. Detects each input's packaging format:
       - "crc32p2" : size % 34 == 0 and every 32-byte block's 2-byte CRC16
                     (poly 0x8005, init 0xFFFFFFFF, big-endian) verifies.
       - "flat"    : anything else (plain XIP, no interleaved CRC).
  2. Decodes crc32p2 -> logical (strips the 2-byte CRC after each 32-byte
     block) so different packaging formats can be compared on equal footing.
  3. Compares logical forms sector-by-sector (default 256 B; --sector N),
     printing a compact map of matching / differing regions (SHA256).
  4. Reports the tail / status-block difference (last --tail N bytes).
  5. Emits a JSON summary with --json PATH (still read-only).

Exit code: 0 if it ran, 2 if a file is missing / unreadable. Diff results
are reported, not asserted — this is an investigation aid, not a test gate.

This mirrors the black-box differential described in
docs/bk7258-t5ai/bootloader/full-reverse-synthesis.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

CRC_DATA = 32
CRC_TOTAL = 34
TAIL_DEFAULT = 64
SECTOR_DEFAULT = 256


def crc16_be(data: bytes) -> int:
    """Beken CRC-XIP: poly 0x8005, init 0xFFFFFFFF, MSB-first, big-endian out."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x8005) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def detect_format(buf: bytes) -> str:
    if len(buf) % CRC_TOTAL != 0:
        return "flat"
    bad = 0
    n = 0
    for off in range(0, len(buf) - 1, CRC_TOTAL):
        block = buf[off : off + CRC_DATA]
        stored = struct.unpack(">H", buf[off + CRC_DATA : off + CRC_TOTAL])[0]
        if stored != crc16_be(block):
            bad += 1
        n += 1
    return "crc32p2" if n and bad == 0 else "flat"


def decode_crc32p2(buf: bytes) -> bytes:
    out = bytearray()
    for off in range(0, len(buf), CRC_TOTAL):
        out += buf[off : off + CRC_DATA]
    return bytes(out)


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def load(path: Path) -> bytes:
    if not path.exists():
        raise FileNotFoundError(path)
    return path.read_bytes()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--official", required=True, type=Path,
                    help="official / reference packaged image (flat or crc32p2)")
    ap.add_argument("--own", required=True, type=Path,
                    help="own packaged image (flat or crc32p2)")
    ap.add_argument("--own-raw", type=Path, default=None,
                    help="optional own raw (pre-CRC) image to compare logically")
    ap.add_argument("--sector", type=int, default=SECTOR_DEFAULT,
                    help=f"sector size for sector diff (default {SECTOR_DEFAULT})")
    ap.add_argument("--tail", type=int, default=TAIL_DEFAULT,
                    help=f"tail bytes to inspect (default {TAIL_DEFAULT})")
    ap.add_argument("--json", type=Path, default=None,
                    help="write JSON summary to PATH (read-only output)")
    args = ap.parse_args()

    try:
        off_buf = load(args.official)
        own_buf = load(args.own)
        own_raw = load(args.own_raw) if args.own_raw else None
    except FileNotFoundError as e:
        print(f"error: file not found: {e}", file=sys.stderr)
        return 2

    off_fmt = detect_format(off_buf)
    own_fmt = detect_format(own_buf)
    off_log = decode_crc32p2(off_buf) if off_fmt == "crc32p2" else off_buf
    own_log = decode_crc32p2(own_buf) if own_fmt == "crc32p2" else own_buf

    print("=" * 72)
    print("BK7258 package black-box differential (read-only)")
    print("=" * 72)
    print(f"official : {args.official}  size={len(off_buf)}  format={off_fmt}")
    print(f"own      : {args.own}  size={len(own_buf)}  format={own_fmt}")
    if own_raw is not None:
        print(f"own-raw  : {args.own_raw}  size={len(own_raw)}  format=flat(pre-CRC)")
    print("-" * 72)
    print(f"logical sizes: official={len(off_log)}  own={len(own_log)}")
    print(f"packaging match: {'YES (both crc32p2)' if off_fmt == own_fmt == 'crc32p2' else 'NO (different container format)'}")
    if off_fmt != own_fmt:
        print("  -> container formats differ; comparison done on decoded logical form.")
    print("-" * 72)

    # Sector diff on logical forms (limited to common length)
    common = min(len(off_log), len(own_log))
    if common == 0:
        print("nothing comparable (one side empty).")
        return 0

    same = 0
    diff_regions = []
    start = None
    pos = 0
    while pos < common:
        a = off_log[pos : pos + args.sector]
        b = own_log[pos : pos + args.sector]
        if sha256(a) == sha256(b):
            if start is not None:
                diff_regions.append((start, pos))
                start = None
            same += 1
        else:
            if start is None:
                start = pos
        pos += args.sector
    if start is not None:
        diff_regions.append((start, common))

    total_sec = (common + args.sector - 1) // args.sector
    print(f"sector diff ({args.sector}B sectors, logical common={common}):")
    print(f"  matching sectors : {same}/{total_sec}")
    print(f"  differing regions: {len(diff_regions)}")
    for s, e in diff_regions:
        print(f"    [0x{s:05x} .. 0x{e:05x}]  ({e - s} B)")
    print("-" * 72)

    # Tail / status block
    ot = off_log[-args.tail:] if len(off_log) >= args.tail else off_log
    wt = own_log[-args.tail:] if len(own_log) >= args.tail else own_log
    print(f"tail ({args.tail}B logical):")
    print(f"  official: {ot.hex()}")
    print(f"  own     : {wt.hex()}")
    print(f"  tail match: {'YES' if ot == wt else 'NO'}")

    # Raw vs packed own (if provided) to isolate packer effect
    if own_raw is not None:
        print("-" * 72)
        rs = 0
        raw_diff = []
        su = None
        p = 0
        while p < min(len(own_raw), len(own_log)):
            a = own_raw[p : p + args.sector]
            b = own_log[p : p + args.sector]
            if sha256(a) == sha256(b):
                if su is not None:
                    raw_diff.append((su, p))
                    su = None
                rs += 1
            else:
                if su is None:
                    su = p
            p += args.sector
        if su is not None:
            raw_diff.append((su, min(len(own_raw), len(own_log))))
        print(f"own-raw vs own-packed (isolates packer/CRC expansion):")
        print(f"  matching sectors: {rs}")
        print(f"  differing regions: {len(raw_diff)}")
        for s, e in raw_diff:
            print(f"    [0x{s:05x} .. 0x{e:05x}]  ({e - s} B)")

    if args.json:
        summary = {
            "official": {"path": str(args.official), "size": len(off_buf), "format": off_fmt},
            "own": {"path": str(args.own), "size": len(own_buf), "format": own_fmt},
            "packaging_match": off_fmt == own_fmt,
            "logical_common": common,
            "matching_sectors": same,
            "total_sectors": total_sec,
            "differing_regions": [list(r) for r in diff_regions],
            "tail_official": ot.hex(),
            "tail_own": wt.hex(),
            "tail_match": ot == wt,
        }
        args.json.write_text(json.dumps(summary, indent=2) + "\n")
        print("-" * 72)
        print(f"JSON summary written: {args.json}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
