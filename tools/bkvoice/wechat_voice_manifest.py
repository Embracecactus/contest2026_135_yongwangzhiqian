#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create a privacy-minimized manifest of WeChat-exported WAV messages.

The tool never copies media or writes below --export-root.  It intentionally
does not parse message text, names, account identifiers, or HTML body content.
Only the exporter row direction and a WAV token are used for association.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import re
import sys
import wave
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


TOOL_VERSION = "1.0.0"
ROW_RE = re.compile(r"wce-msg-row-(sent|received)", re.IGNORECASE)
WAV_RE = re.compile(r"([A-Za-z0-9_-]+\.wav)\b", re.IGNORECASE)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def relative_path_hash(root: Path, path: Path) -> str:
    return sha256_bytes(path.relative_to(root).as_posix().encode("utf-8"))


def iter_paths(root: Path, suffixes: Iterable[str]) -> list[Path]:
    wanted = {suffix.lower() for suffix in suffixes}
    return sorted(
        path
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in wanted
    )


def direct_chat(root: Path, failures: collections.Counter[str]) -> bool:
    manifests = iter_paths(root, [".json"])
    found = False
    for manifest in manifests:
        try:
            document = json.loads(manifest.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            failures["invalid_json_manifest"] += 1
            continue
        if not isinstance(document, dict) or "isGroup" not in document:
            continue
        found = True
        if document["isGroup"] is not False:
            failures["not_direct_chat"] += 1
            return False
    if not found:
        failures["missing_direct_chat_manifest"] += 1
        return False
    return True


def collect_associations(
    root: Path, failures: collections.Counter[str]
) -> dict[str, set[str]]:
    associations: dict[str, set[str]] = collections.defaultdict(set)
    for page in iter_paths(root, [".js"]):
        if not page.name.startswith("page-"):
            continue
        try:
            source = page.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            failures["unreadable_page_js"] += 1
            continue
        markers = list(ROW_RE.finditer(source))
        for index, marker in enumerate(markers):
            end = (
                markers[index + 1].start() if index + 1 < len(markers) else len(source)
            )
            for wav_name in WAV_RE.findall(source[marker.end() : end]):
                associations[wav_name.lower()].add(marker.group(1).lower())
    return associations


def wav_header(path: Path) -> tuple[float, int, int] | None:
    try:
        with wave.open(str(path), "rb") as audio:
            rate = audio.getframerate()
            frames = audio.getnframes()
            channels = audio.getnchannels()
    except (OSError, EOFError, wave.Error):
        return None
    if rate <= 0 or channels <= 0:
        return None
    return round(frames * 1000.0 / rate, 3), rate, channels


def build_rows(
    root: Path,
    associations: dict[str, set[str]],
    exclude_collisions: bool,
    failures: collections.Counter[str],
) -> list[dict[str, object]]:
    physical: dict[str, list[Path]] = collections.defaultdict(list)
    for candidate in iter_paths(root, [".wav"]):
        physical[candidate.name.lower()].append(candidate)

    rows: list[dict[str, object]] = []
    for wav_name in sorted(physical):
        paths = physical[wav_name]
        directions = associations.get(wav_name, set())
        if len(paths) != 1:
            # A basename token must identify one physical WAV.  Do not select an
            # arbitrary duplicate, even for an explicit unknown-direction run.
            failures["duplicate_physical_basename"] += 1
            continue
        elif len(directions) == 1:
            direction = next(iter(directions))
            status = "associated"
        elif len(directions) > 1:
            direction = "unknown"
            status = "direction_collision"
            failures[status] += 1
        else:
            direction = "unknown"
            status = "unreferenced_wav"
            failures[status] += 1

        if exclude_collisions and status == "direction_collision":
            continue
        header = wav_header(paths[0]) if len(paths) == 1 else None
        if header is None:
            failures["invalid_wav_header"] += 1
            continue
        duration_ms, sample_rate, channels = header
        rows.append(
            {
                "path_sha256": relative_path_hash(root, paths[0]),
                "sha256": sha256_file(paths[0]),
                "_source_path": paths[0].relative_to(root).as_posix(),
                "direction": direction,
                "association_status": status,
                "duration_ms": duration_ms,
                "sample_rate": sample_rate,
                "channels": channels,
            }
        )
    return rows


def write_jsonl(
    path: Path, rows: list[dict[str, object]], include_source_path: bool = False
) -> str:
    output_rows = []
    for row in rows:
        output = {key: value for key, value in row.items() if not key.startswith("_")}
        if include_source_path:
            output["source_path"] = row["_source_path"]
        output_rows.append(output)
    payload = "".join(
        json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n"
        for row in output_rows
    ).encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return sha256_bytes(payload)


def audit_inputs(root: Path) -> list[dict[str, str]]:
    entries = []
    for path in iter_paths(root, [".json", ".js", ".wav"]):
        entries.append(
            {
                "kind": path.suffix.lower().lstrip("."),
                "path_sha256": relative_path_hash(root, path),
            }
        )
    return entries


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-root", required=True, type=Path)
    parser.add_argument(
        "--direction",
        choices=("received", "sent", "unknown", "all"),
        default="received",
    )
    parser.add_argument("--out", type=Path)
    parser.add_argument("--audit-out", type=Path)
    parser.add_argument(
        "--private-out",
        type=Path,
        help="optional private JSONL with source_path; keep outside Git/export root",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--exclude-collisions", action=argparse.BooleanOptionalAction, default=True
    )
    args = parser.parse_args(argv)
    if not args.dry_run and (args.out is None or args.audit_out is None):
        parser.error("--out and --audit-out are required unless --dry-run is used")
    if args.dry_run and args.private_out is not None:
        parser.error("--private-out cannot be used with --dry-run")
    return args


def is_within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root)
    except ValueError:
        return False
    return True


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])
    root = args.export_root.resolve()
    if not root.is_dir():
        raise SystemExit("--export-root must be an existing directory")
    outputs = [
        path
        for path in (args.out, args.audit_out, args.private_out)
        if path is not None
    ]
    if not args.dry_run and any(is_within(path, root) for path in outputs):
        raise SystemExit("all outputs must be outside --export-root")
    if len({path.resolve() for path in outputs}) != len(outputs):
        raise SystemExit("output paths must be distinct")

    failures: collections.Counter[str] = collections.Counter()
    allowed = direct_chat(root, failures)
    associations = collect_associations(root, failures) if allowed else {}
    rows = (
        build_rows(root, associations, args.exclude_collisions, failures)
        if allowed
        else []
    )
    selected = (
        rows
        if args.direction == "all"
        else [row for row in rows if row["direction"] == args.direction]
    )

    output_sha256 = None
    private_output_sha256 = None
    if not args.dry_run:
        output_sha256 = write_jsonl(args.out, selected)
        if args.private_out is not None:
            private_output_sha256 = write_jsonl(
                args.private_out, selected, include_source_path=True
            )

    audit = {
        "tool_version": TOOL_VERSION,
        "created_utc": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "parameters": {
            "direction": args.direction,
            "dry_run": args.dry_run,
            "exclude_collisions": args.exclude_collisions,
            "export_root_sha256": sha256_bytes(root.name.encode("utf-8")),
        },
        "direct_chat": allowed,
        "counts": {
            "associated_tokens": sum(len(value) for value in associations.values()),
            "physical_wav": len(iter_paths(root, [".wav"])),
            "eligible_rows": len(rows),
            "selected_rows": len(selected),
        },
        "failure_reasons": dict(sorted(failures.items())),
        "output_sha256": output_sha256,
        "private_output_sha256": private_output_sha256,
        "inputs": audit_inputs(root),
    }
    if args.dry_run:
        print(json.dumps(audit, sort_keys=True, separators=(",", ":")))
    else:
        args.audit_out.parent.mkdir(parents=True, exist_ok=True)
        args.audit_out.write_text(
            json.dumps(audit, sort_keys=True, indent=2) + "\n", encoding="utf-8"
        )
    return 0 if allowed else 2


if __name__ == "__main__":
    raise SystemExit(main())
