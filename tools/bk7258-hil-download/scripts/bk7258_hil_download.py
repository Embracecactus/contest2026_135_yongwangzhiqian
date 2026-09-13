#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Profile-aware BK7258 BK Loader transport with auditable evidence.

This helper is the BK7258-specific Flash layer. It deliberately composes with
the board-independent ``windows-hardware-debug`` toolkit instead of duplicating
UART capture, DTR/RTS, or J-Link control. It never discovers or authorizes a
target by itself.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Sequence


SUCCESS_MARKERS = (
    "Gotten Bus",
    "EraseFlash ->pass",
    "WriteFlash ->pass",
    "Writing Flash OK",
    "{All Finished Successfully}",
)

FAILURE_MARKERS = (
    "GetBus fail",
    "EraseFlash ->fail",
    "WriteFlash ->fail",
    "Writing Flash Fail",
    "Writing Flash Failed",
    "{All Finished Failed}",
)

SESSION_START_MARKERS = (b"Gotten Bus", b"GetBus fail")
SCRIPT_DIR = Path(__file__).resolve().parent
PROFILE_PATH = SCRIPT_DIR.parent / "references" / "board-profiles.json"


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_port(value: str) -> str:
    match = re.fullmatch(r"(?i)(?:COM)?([1-9][0-9]*)", value.strip())
    if match is None:
        raise ValueError("COM port must be positive, for example COM8 or 8")
    return f"COM{match.group(1)}"


def loader_port_number(value: str) -> str:
    return normalize_port(value)[3:]


def parse_integer(value: str, label: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise ValueError(f"{label} must be a decimal or 0x-prefixed integer") from exc
    if parsed < 0:
        raise ValueError(f"{label} must be non-negative")
    return parsed


def canonical_hex(value: int) -> str:
    return f"0x{value:x}"


def loader_image_path(image: Path, loader: Path) -> str:
    """Return a path form a Windows loader can open when called from WSL2."""
    resolved = image.resolve()
    if os.name != "nt" and loader.suffix.lower() == ".exe":
        try:
            completed = subprocess.run(
                ["wslpath", "-w", str(resolved)],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            raise RuntimeError(
                "cannot convert an image path for the Windows BK Loader"
            ) from exc
        return completed.stdout.strip()
    return str(resolved)


def decode_log(data: bytes) -> str:
    for encoding in ("utf-8", "gb18030"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            pass
    return data.decode("utf-8", errors="replace")


def analyze_log(data: bytes) -> dict[str, Any]:
    present = [marker for marker in SUCCESS_MARKERS if marker.encode() in data]
    missing = [marker for marker in SUCCESS_MARKERS if marker.encode() not in data]
    failures = [marker for marker in FAILURE_MARKERS if marker.encode() in data]
    safe_prewrite_failure = (
        b"GetBus fail" in data
        and b"EraseFlash" not in data
        and b"WriteFlash" not in data
    )
    return {
        "status": "passed" if not missing and not failures else "failed",
        "success_markers_present": present,
        "success_markers_missing": missing,
        "failure_markers_present": failures,
        "safe_prewrite_failure": safe_prewrite_failure,
    }


def select_last_session(data: bytes) -> tuple[bytes, int]:
    """Select the newest attempt from a BK Loader append-only log."""
    offset = max(data.rfind(marker) for marker in SESSION_START_MARKERS)
    if offset < 0:
        return data, 0
    return data[offset:], offset


def validate_sha256(value: str, label: str = "SHA256") -> str:
    normalized = value.strip().lower()
    if re.fullmatch(r"[0-9a-f]{64}", normalized) is None:
        raise ValueError(f"{label} must contain exactly 64 hex digits")
    return normalized


def load_profiles() -> tuple[dict[str, Any], dict[str, str]]:
    try:
        document = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot load board profiles: {PROFILE_PATH}: {exc}") from exc
    profiles = document.get("profiles")
    if document.get("format") != 1 or not isinstance(profiles, dict):
        raise RuntimeError(f"invalid board profile schema: {PROFILE_PATH}")

    aliases: dict[str, str] = {}
    for canonical, profile in profiles.items():
        if not isinstance(profile, dict):
            raise RuntimeError(f"invalid board profile: {canonical}")
        for alias in [canonical, *profile.get("aliases", [])]:
            key = str(alias).lower().replace("-", "_")
            if key in aliases and aliases[key] != canonical:
                raise RuntimeError(f"duplicate board alias: {alias}")
            aliases[key] = canonical
    return profiles, aliases


def resolve_profile(name: str) -> tuple[str, dict[str, Any]]:
    profiles, aliases = load_profiles()
    key = name.strip().lower().replace("-", "_")
    canonical = aliases.get(key)
    if canonical is None:
        raise ValueError(
            f"unknown board profile '{name}'; use the profiles command to list them"
        )
    return canonical, profiles[canonical]


def require_regular_file(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise ValueError(f"{label} does not exist: {resolved}")
    if resolved.suffix.lower() in (".zip", ".bkpack"):
        raise ValueError(
            f"{label} is a package, not a BK Loader binary: {resolved}; "
            "verify/extract it with the repository release workflow first"
        )
    return resolved


def parse_segment(
    spec: str, loader: Path, expected_sha256: str | None
) -> dict[str, Any]:
    try:
        path_text, range_text = spec.rsplit("@", 1)
        offset_text, length_text = range_text.split("-", 1)
    except ValueError as exc:
        raise ValueError(
            "--segment must use PATH@OFFSET-LENGTH, for example "
            "app.bin@0x11000-0x39000"
        ) from exc
    image = require_regular_file(Path(path_text), "segment image")
    offset = parse_integer(offset_text, "segment offset")
    length = parse_integer(length_text, "segment length")
    if length < 1:
        raise ValueError("segment length must be positive")
    size = image.stat().st_size
    if size != length:
        raise ValueError(
            f"segment length mismatch for {image}: declared {length}, file size {size}"
        )
    digest = sha256_file(image)
    expected = None
    if expected_sha256 is not None:
        expected = validate_sha256(expected_sha256, "segment SHA256")
        if digest != expected:
            raise ValueError(
                f"segment SHA256 mismatch for {image}: expected {expected}, got {digest}"
            )
    return {
        "image": str(image),
        "image_size": size,
        "image_sha256": digest,
        "expected_sha256": expected,
        "offset": offset,
        "offset_hex": canonical_hex(offset),
        "length": length,
        "length_hex": canonical_hex(length),
        "end": offset + length,
        "loader_spec": (
            f"{loader_image_path(image, loader)}@"
            f"{canonical_hex(offset)}-{canonical_hex(length)}"
        ),
    }


def parse_write_bound(spec: str) -> dict[str, Any]:
    try:
        start_text, length_text = spec.split("-", 1)
    except ValueError as exc:
        raise ValueError(
            "--write-bound must use START-LENGTH, for example 0x410000-0x20000"
        ) from exc
    start = parse_integer(start_text, "write bound start")
    length = parse_integer(length_text, "write bound length")
    if length < 1:
        raise ValueError("write bound length must be positive")
    if start % 4096 or length % 4096:
        raise ValueError("write bound start and length must be 4 KiB aligned")
    return {
        "offset": start,
        "offset_hex": canonical_hex(start),
        "length": length,
        "length_hex": canonical_hex(length),
        "end": start + length,
    }


def validate_nonoverlap(ranges: list[dict[str, Any]], label: str) -> None:
    ordered = sorted(ranges, key=lambda item: item["offset"])
    for previous, current in zip(ordered, ordered[1:]):
        if current["offset"] < previous["end"]:
            raise ValueError(
                f"{label} ranges overlap: "
                f"{previous.get('image', label)} [{previous['offset_hex']}, "
                f"{canonical_hex(previous['end'])}) and "
                f"{current.get('image', label)} [{current['offset_hex']}, "
                f"{canonical_hex(current['end'])})"
            )


def validate_segments_in_bounds(
    segments: list[dict[str, Any]], bounds: list[dict[str, Any]]
) -> None:
    for segment in segments:
        if not any(
            segment["offset"] >= bound["offset"] and segment["end"] <= bound["end"]
            for bound in bounds
        ):
            raise ValueError(
                "segment range is outside every operator-authorized write bound: "
                f"{segment['image']} [{segment['offset_hex']}, "
                f"{canonical_hex(segment['end'])})"
            )


def select_profile_value(
    args: argparse.Namespace,
    profile_download: dict[str, Any],
    name: str,
) -> tuple[Any, bool]:
    explicit = getattr(args, name)
    default = profile_download.get(name)
    if explicit is None:
        return default, False
    changed = explicit != default
    if changed and not args.allow_profile_override:
        option = "--" + name.replace("_", "-")
        raise ValueError(
            f"{option}={explicit!r} overrides board profile value {default!r}; "
            "review the electrical/loader consequence and add "
            "--allow-profile-override"
        )
    return explicit, changed


def add_download_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--board", required=True, help="board profile or alias")
    parser.add_argument(
        "--transport",
        choices=("single", "multi"),
        help="BK Loader input form; defaults to the selected board profile",
    )
    parser.add_argument(
        "--artifact-kind",
        required=True,
        choices=(
            "direct-full",
            "direct-segments",
            "device-bound-full",
            "signed-full",
            "signed-segments",
        ),
        help="operator claim recorded in evidence; trust still needs repository verification",
    )
    parser.add_argument(
        "--loader",
        default=os.environ.get("BK7258_LOADER_EXE"),
        help="path to bk_loader.exe (or set BK7258_LOADER_EXE)",
    )
    parser.add_argument("--port", required=True, help="download/control port, e.g. COM8")
    parser.add_argument("--image", type=Path, help="single binary for transport=single")
    parser.add_argument(
        "--segment",
        action="append",
        help="repeatable PATH@OFFSET-LENGTH for transport=multi",
    )
    parser.add_argument(
        "--segment-sha256",
        action="append",
        help="optional repeatable expected hash paired with each --segment",
    )
    parser.add_argument(
        "--write-bound",
        action="append",
        help="repeatable operator-authorized START-LENGTH range for transport=multi",
    )
    parser.add_argument("--expected-sha256")
    parser.add_argument("--expected-size", type=int)
    parser.add_argument("--baud", type=int)
    parser.add_argument("--start", help="single-image BK Loader start address")
    parser.add_argument("--reset-command")
    parser.add_argument("--uart-type")
    parser.add_argument("--fast-link", type=int, choices=(0, 1))
    parser.add_argument("--hard-reset", type=int, choices=(0, 1))
    parser.add_argument("--reboot", type=int, choices=(0, 1))
    parser.add_argument(
        "--allow-profile-override",
        action="store_true",
        help="allow reviewed loader values that differ from the board profile",
    )


def prepare(args: argparse.Namespace) -> dict[str, Any]:
    if not args.loader:
        raise ValueError("--loader or BK7258_LOADER_EXE is required")

    loader = Path(args.loader).expanduser().resolve()
    if not loader.is_file():
        raise ValueError(f"BK Loader does not exist: {loader}")
    canonical_board, profile = resolve_profile(args.board)
    profile_download = profile["download"]
    transport = args.transport or profile_download["default_transport"]
    if transport not in profile_download["allowed_transports"]:
        raise ValueError(
            f"board {canonical_board} does not permit transport={transport}; "
            f"allowed: {', '.join(profile_download['allowed_transports'])}"
        )
    if args.artifact_kind not in profile_download["allowed_artifact_kinds"]:
        raise ValueError(
            f"board {canonical_board} does not permit "
            f"artifact-kind={args.artifact_kind}; allowed: "
            f"{', '.join(profile_download['allowed_artifact_kinds'])}"
        )
    kind_transport = {
        "direct-full": "single",
        "device-bound-full": "single",
        "signed-full": "single",
        "direct-segments": "multi",
        "signed-segments": "multi",
    }
    required_transport = kind_transport[args.artifact_kind]
    if transport != required_transport:
        raise ValueError(
            f"artifact-kind={args.artifact_kind} requires "
            f"transport={required_transport}, got transport={transport}"
        )
    if transport == "multi":
        single_only = [
            option
            for option in ("start", "reset_command", "hard_reset")
            if getattr(args, option) is not None
        ]
        if single_only:
            rendered = ", ".join(
                "--" + item.replace("_", "-") for item in single_only
            )
            raise ValueError(
                f"{rendered} are single-image options and cannot be used with "
                "transport=multi"
            )
    elif args.write_bound:
        raise ValueError("--write-bound is only valid with transport=multi")

    resolved_values: dict[str, Any] = {}
    overridden: list[str] = []
    for name in (
        "baud",
        "start",
        "reset_command",
        "uart_type",
        "fast_link",
        "hard_reset",
        "reboot",
    ):
        value, changed = select_profile_value(args, profile_download, name)
        resolved_values[name] = value
        if changed:
            overridden.append(name)

    if not isinstance(resolved_values["baud"], int) or resolved_values["baud"] < 1:
        raise ValueError("download baud must be positive")
    if not resolved_values["uart_type"]:
        raise ValueError("board profile must resolve a BK Loader uart_type")
    if resolved_values["reboot"] not in (0, 1):
        raise ValueError("board profile must resolve reboot to 0 or 1")
    if resolved_values["fast_link"] not in (0, 1):
        raise ValueError("board profile must resolve fast_link to 0 or 1")
    if resolved_values["hard_reset"] not in (None, 0, 1):
        raise ValueError("board profile hard_reset must be null, 0, or 1")

    port = normalize_port(args.port)
    artifacts: list[dict[str, Any]] = []
    write_bounds: list[dict[str, Any]] = []
    command: list[str] = [
        str(loader),
        "download",
        "-p",
        loader_port_number(port),
        "-b",
        str(resolved_values["baud"]),
    ]

    if transport == "single":
        if args.image is None or args.segment:
            raise ValueError("transport=single requires --image and forbids --segment")
        if args.segment_sha256:
            raise ValueError("--segment-sha256 is only valid with transport=multi")
        image = require_regular_file(args.image, "firmware image")
        image_size = image.stat().st_size
        required_size = profile_download.get("single_image_size")
        if required_size is not None and image_size != required_size:
            raise ValueError(
                f"{canonical_board} single image must be {required_size} bytes, "
                f"got {image_size}"
            )
        if args.expected_size is not None:
            if args.expected_size < 1:
                raise ValueError("--expected-size must be positive")
            if image_size != args.expected_size:
                raise ValueError(
                    f"image size mismatch: expected {args.expected_size}, "
                    f"got {image_size}"
                )
        digest = sha256_file(image)
        expected = None
        if args.expected_sha256 is not None:
            expected = validate_sha256(args.expected_sha256, "image SHA256")
            if digest != expected:
                raise ValueError(
                    f"image SHA256 mismatch: expected {expected}, got {digest}"
                )
        start = parse_integer(str(resolved_values["start"]), "start address")
        artifacts.append(
            {
                "image": str(image),
                "image_size": image_size,
                "image_sha256": digest,
                "expected_sha256": expected,
                "offset": start,
                "offset_hex": canonical_hex(start),
                "length": image_size,
                "length_hex": canonical_hex(image_size),
                "end": start + image_size,
            }
        )
        command.extend(
            ["-s", canonical_hex(start), "-i", loader_image_path(image, loader)]
        )
    else:
        if args.image is not None or not args.segment:
            raise ValueError("transport=multi requires --segment and forbids --image")
        if args.expected_sha256 is not None or args.expected_size is not None:
            raise ValueError(
                "--expected-sha256/--expected-size are single-image options; "
                "use paired --segment-sha256 values for multi"
            )
        expected_hashes = args.segment_sha256 or []
        require_hashes = bool(profile_download.get("require_segment_sha256"))
        if require_hashes and len(expected_hashes) != len(args.segment):
            raise ValueError(
                f"board {canonical_board} requires one --segment-sha256 for every "
                "--segment"
            )
        if expected_hashes and len(expected_hashes) != len(args.segment):
            raise ValueError("provide one --segment-sha256 for every --segment, or none")
        for index, spec in enumerate(args.segment):
            expected = expected_hashes[index] if expected_hashes else None
            artifacts.append(parse_segment(spec, loader, expected))
        validate_nonoverlap(artifacts, "segment")
        write_bounds = [parse_write_bound(spec) for spec in args.write_bound or []]
        if profile_download.get("require_segment_bounds") and not write_bounds:
            raise ValueError(
                f"board {canonical_board} requires at least one --write-bound for "
                "transport=multi"
            )
        if write_bounds:
            validate_nonoverlap(write_bounds, "write bound")
            capacity = profile_download.get("single_image_size")
            if capacity is not None:
                for bound in write_bounds:
                    if bound["end"] > capacity:
                        raise ValueError(
                            "write bound exceeds known flash capacity: "
                            f"[{bound['offset_hex']}, {canonical_hex(bound['end'])}) "
                            f"> {capacity} bytes"
                        )
            validate_segments_in_bounds(artifacts, write_bounds)
        command.extend(
            [
                "--uart-type",
                str(resolved_values["uart_type"]),
                "--mainBin-multi",
                ",".join(item["loader_spec"] for item in artifacts),
            ]
        )

    if transport == "single" or profile_download.get("multi_emit_reset_options"):
        if resolved_values["reset_command"] is not None:
            command.extend(["--swrst", str(resolved_values["reset_command"])])
        if resolved_values["hard_reset"] is not None:
            command.extend(["--hard-reset", str(resolved_values["hard_reset"])])
    if transport == "single":
        command.extend(["--uart-type", str(resolved_values["uart_type"])])
    command.extend(
        [
            "--reboot",
            str(resolved_values["reboot"]),
            "--fast-link",
            str(resolved_values["fast_link"]),
        ]
    )

    return {
        "board_profile": canonical_board,
        "board_label": profile["label"],
        "profile_file": str(PROFILE_PATH),
        "profile_sources": profile.get("sources", []),
        "artifact_kind_claim": args.artifact_kind,
        "artifact_trust_verified_by_helper": False,
        "transport": transport,
        "artifacts": artifacts,
        "operator_authorized_write_bounds": write_bounds,
        "loader": str(loader),
        "port": port,
        "port_role": profile_download["port_role"],
        "loader_values": resolved_values,
        "reset_strategy": profile_download["reset_strategy"],
        "profile_overrides": overridden,
        "command": command,
        "invariants": {
            "target_and_port_are_caller_supplied": True,
            "automatic_retry": False,
            "generic_debug_tool_owns_capture_and_target_control": True,
            "rts_dtr_reset_policy": profile["debug"]["rts_dtr_policy"],
            "download_entry": profile_download["entry"],
        },
    }


def print_json(value: Any) -> None:
    print(json.dumps(value, ensure_ascii=False, indent=2))


def run_profiles(args: argparse.Namespace) -> int:
    profiles, _ = load_profiles()
    if args.board:
        canonical, profile = resolve_profile(args.board)
        print_json({"format": 1, "profile": canonical, **profile})
    else:
        print_json({"format": 1, "profiles": profiles})
    return 0


def run_preflight(args: argparse.Namespace) -> int:
    prepared = prepare(args)
    print_json({"status": "preflight-passed", **prepared})
    return 0


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def run_download(args: argparse.Namespace) -> int:
    if not args.execute:
        raise ValueError(
            "run is a Flash mutation and requires --execute after explicit authorization"
        )

    prepared = prepare(args)
    evidence = args.evidence_dir.expanduser().resolve()
    if evidence.exists() and (not evidence.is_dir() or any(evidence.iterdir())):
        raise ValueError(f"evidence directory must be new or empty: {evidence}")
    evidence.mkdir(parents=True, exist_ok=True)

    command_record = {
        "format": 2,
        "tool": "bk7258_hil_download",
        "stage": "prepared",
        "prepared_utc": utc_now(),
        **prepared,
    }
    write_json(evidence / "command.json", command_record)

    started = utc_now()
    raw_path = evidence / "bkloader.raw"
    text_path = evidence / "bkloader.txt"
    try:
        with raw_path.open("wb") as raw_stream:
            process = subprocess.Popen(
                prepared["command"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            assert process.stdout is not None
            while True:
                chunk = process.stdout.readline()
                if chunk:
                    raw_stream.write(chunk)
                    raw_stream.flush()
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
                elif process.poll() is not None:
                    break
            loader_exit_code = process.wait()
    except OSError as exc:
        raw_data = raw_path.read_bytes() if raw_path.exists() else b""
        text_path.write_text(decode_log(raw_data), encoding="utf-8")
        result = {
            "format": 2,
            "tool": "bk7258_hil_download",
            "status": "failed",
            "stage": "loader-start",
            "error": str(exc),
            "started_utc": started,
            "ended_utc": utc_now(),
            "evidence": {
                "raw": str(raw_path),
                "text": str(text_path),
                "raw_capture_available": bool(raw_data),
            },
            **prepared,
        }
        write_json(evidence / "result.json", result)
        raise RuntimeError(f"cannot start BK Loader: {exc}") from exc

    raw_data = raw_path.read_bytes()
    text_path.write_text(decode_log(raw_data), encoding="utf-8")
    analysis = analyze_log(raw_data)
    result = {
        "format": 2,
        "tool": "bk7258_hil_download",
        **analysis,
        "loader_exit_code": loader_exit_code,
        "loader_exit_code_advisory": True,
        "started_utc": started,
        "ended_utc": utc_now(),
        "evidence": {
            "raw": str(raw_path),
            "text": str(text_path),
            "raw_capture_available": bool(raw_data),
        },
        **prepared,
    }
    write_json(evidence / "result.json", result)
    print_json(result)
    return 0 if analysis["status"] == "passed" else 2


def run_verify_log(args: argparse.Namespace) -> int:
    log = args.log.expanduser().resolve()
    if not log.is_file():
        raise ValueError(f"log does not exist: {log}")
    data = log.read_bytes()
    offset = 0
    if args.last_session:
        data, offset = select_last_session(data)
    result = {
        "format": 2,
        "tool": "bk7258_hil_download",
        "log": str(log),
        "scope": "last-session" if args.last_session else "complete-log",
        "session_offset": offset,
        **analyze_log(data),
    }
    print_json(result)
    return 0 if result["status"] == "passed" else 2


def find_debug_tool(value: str | None) -> Path:
    candidates: list[Path] = []
    if value:
        candidates.append(Path(value).expanduser())
    environment = os.environ.get("WINDOWS_HARDWARE_DEBUG_DIR")
    if environment:
        candidates.append(Path(environment).expanduser())
    candidates.extend(
        [
            SCRIPT_DIR.parent.parent / "windows-hardware-debug",
            Path.home() / ".codex" / "skills" / "windows-hardware-debug",
        ]
    )
    for candidate in candidates:
        script = candidate.resolve() / "scripts" / "debug_session_wsl.sh"
        if script.is_file():
            return script
    raise ValueError(
        "windows-hardware-debug was not found; pass --debug-tool or set "
        "WINDOWS_HARDWARE_DEBUG_DIR"
    )


def run_debug_plan(args: argparse.Namespace) -> int:
    canonical, profile = resolve_profile(args.board)
    debug = profile["debug"]
    action = args.action or debug["default_action"]
    if action not in debug["allowed_actions"]:
        raise ValueError(
            f"board {canonical} does not permit debug action={action}; "
            f"allowed: {', '.join(debug['allowed_actions'])}"
        )
    console_port = normalize_port(args.console_port)
    reset_port = normalize_port(args.reset_port) if args.reset_port else None
    if args.baud is not None and args.baud < 1:
        raise ValueError("--baud must be positive")
    if args.duration < 1:
        raise ValueError("--duration must be positive")
    if args.command and action != "capture":
        raise ValueError("--command is only valid for action=capture")
    if action == "serial-pulse":
        if debug.get("reset_port_required") and reset_port is None:
            raise ValueError(f"board {canonical} serial-pulse requires --reset-port")
        if reset_port == console_port and debug.get("port_topology").startswith("separate"):
            raise ValueError(
                f"board {canonical} requires distinct console and reset/download ports"
            )
    elif reset_port is not None:
        raise ValueError("--reset-port is only valid for action=serial-pulse")

    script = find_debug_tool(args.debug_tool)
    command = [
        str(script),
        "--console-port",
        console_port,
        "--baud",
        str(args.baud or debug["console_baud"]),
        "--duration",
        str(args.duration),
        "--action",
        action,
        "--output-dir",
        str(args.output_dir.expanduser().resolve()),
    ]
    if reset_port:
        command.extend(["--reset-port", reset_port])
    requires_target_control = action in ("serial-pulse", "jlink-reset")
    if action == "serial-pulse":
        command.extend(
            [
                "--pulse-mode",
                debug["pulse_mode"],
                "--pulse-active-level",
                debug["pulse_active_level"],
                "--pulse-ms",
                str(debug["pulse_ms"]),
            ]
        )
    elif action == "jlink-reset":
        command.extend(
            [
                "--jlink-device",
                debug["jlink_device"],
                "--jlink-speed",
                str(debug["jlink_speed"]),
                "--resume-after-reset",
            ]
        )
    if requires_target_control:
        command.append("--allow-target-control")
    for regex in args.expected_regex or []:
        command.extend(["--expected-regex", regex])
    for regex in args.fail_regex or []:
        command.extend(["--fail-regex", regex])
    for serial_command in args.command or []:
        command.extend(["--command", serial_command])

    print_json(
        {
            "format": 1,
            "status": "debug-plan-ready",
            "executes_hardware": False,
            "board_profile": canonical,
            "board_label": profile["label"],
            "action": action,
            "requires_explicit_target_control_authorization": requires_target_control,
            "port_topology": debug["port_topology"],
            "rts_dtr_policy": debug["rts_dtr_policy"],
            "command": command,
            "sources": profile.get("sources", []),
        }
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Profile-aware BK7258 BK Loader transport and HIL debug planner"
    )
    subparsers = parser.add_subparsers(dest="operation", required=True)

    profiles = subparsers.add_parser(
        "profiles", help="list board profiles without opening hardware"
    )
    profiles.add_argument("--board", help="show one board profile or alias")
    profiles.set_defaults(handler=run_profiles)

    preflight = subparsers.add_parser(
        "preflight", help="validate inputs and print the command without Flash writes"
    )
    add_download_arguments(preflight)
    preflight.set_defaults(handler=run_preflight)

    run = subparsers.add_parser("run", help="execute one explicitly authorized download")
    add_download_arguments(run)
    run.add_argument("--evidence-dir", required=True, type=Path)
    run.add_argument("--execute", action="store_true")
    run.set_defaults(handler=run_download)

    verify = subparsers.add_parser(
        "verify-log", help="judge an existing raw or text BK Loader log"
    )
    verify.add_argument("--log", required=True, type=Path)
    verify.add_argument(
        "--last-session",
        action="store_true",
        help="analyze only the newest attempt in a BK Loader append-only log",
    )
    verify.set_defaults(handler=run_verify_log)

    debug_plan = subparsers.add_parser(
        "debug-plan",
        help="emit a board-safe windows-hardware-debug command without executing it",
    )
    debug_plan.add_argument("--board", required=True)
    debug_plan.add_argument("--console-port", required=True)
    debug_plan.add_argument("--reset-port")
    debug_plan.add_argument(
        "--action",
        choices=("capture", "manual-reset", "serial-pulse", "jlink-reset"),
    )
    debug_plan.add_argument("--baud", type=int)
    debug_plan.add_argument("--duration", type=int, default=20)
    debug_plan.add_argument("--output-dir", required=True, type=Path)
    debug_plan.add_argument("--expected-regex", action="append")
    debug_plan.add_argument("--fail-regex", action="append")
    debug_plan.add_argument("--command", action="append")
    debug_plan.add_argument("--debug-tool", help="windows-hardware-debug root")
    debug_plan.set_defaults(handler=run_debug_plan)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.handler(args))
    except (ValueError, RuntimeError) as exc:
        parser.error(str(exc))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
