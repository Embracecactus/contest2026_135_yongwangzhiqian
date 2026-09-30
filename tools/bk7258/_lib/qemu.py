# SPDX-License-Identifier: Apache-2.0
"""Native QEMU build and bounded diagnostic execution, separate from flashing."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


class QemuError(RuntimeError):
    """An emulator input, build, or diagnostic failed."""


def add_arguments(parser: argparse.ArgumentParser) -> None:
    commands = parser.add_subparsers(dest="qemu_command", required=True)
    build = commands.add_parser("build", help="build the native downstream QEMU")
    build.add_argument("--source", type=Path, help="explicit local experiment checkout")
    build.add_argument("--workspace", type=Path)
    build.add_argument("--build-dir", type=Path, required=True)
    build.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    run = commands.add_parser(
        "run", help="run a direct diagnostic ELF, not a flash BIN"
    )
    run.add_argument("--qemu", type=Path, required=True)
    run.add_argument("--board", required=True)
    run.add_argument("--elf", type=Path, required=True)
    run.add_argument("--timeout", type=float, default=30)
    run.add_argument(
        "--semihosting",
        action="store_true",
        help="allow trusted guest semihosting, including host file access",
    )
    smoke = commands.add_parser("smoke", help="compile and test the bare-metal fixture")
    smoke.add_argument("--qemu", type=Path, required=True)
    smoke.add_argument(
        "--cc",
        type=Path,
        required=True,
        help="explicit Arm compiler for the diagnostic fixture only",
    )
    smoke.add_argument("--output", type=Path, required=True)
    smoke.add_argument("--board", action="append")
    smoke.add_argument("--timeout", type=float, default=10)


def _call(command: list[str], **kwargs: object) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        raise QemuError(f"command failed ({error.returncode}): {command[0]}") from error
    except subprocess.TimeoutExpired as error:
        raise QemuError(f"command timed out: {command[0]}") from error


def _git(source: Path, *args: str) -> str:
    return _call(
        ["git", "-C", str(source), *args], capture_output=True, text=True
    ).stdout.strip()


def resolve_source(
    repository: Path, workspace: Path | None, explicit: Path | None
) -> Path:
    if explicit is not None:
        source = explicit.resolve()
    else:
        manifest = repository / "contest2026_135_yongwangzhiqian.xml"
        try:
            root = ET.parse(manifest).getroot()
        except (OSError, ET.ParseError) as error:
            raise QemuError("cannot read the team manifest") from error
        rows = [p for p in root.iter("project") if p.get("name") == "qemu-bk7258"]
        if len(rows) != 1 or not re.fullmatch(
            r"[0-9a-f]{40}", rows[0].get("revision", "")
        ):
            raise QemuError(
                "QEMU has no published manifest pin; use --source for a local experiment"
            )
        if rows[0].get("path") != "external/qemu-bk7258":
            raise QemuError("unexpected QEMU project path in manifest")
        source = (workspace or repository.parent).resolve() / rows[0].get("path")
        if _git(source, "rev-parse", "HEAD") != rows[0].get("revision"):
            raise QemuError("QEMU checkout differs from the manifest pin")
        if _git(source, "status", "--porcelain", "--untracked-files=normal"):
            raise QemuError("manifest-selected QEMU checkout has local changes")
    if (
        not (source / "configure").is_file()
        or not (source / "hw/arm/bk7258.c").is_file()
    ):
        raise QemuError(f"not a native BK7258 QEMU source checkout: {source}")
    return source


def boards(repository: Path) -> list[str]:
    return sorted(
        p.parent.name for p in (repository / "boards/bk7258").glob("*/openvela.conf")
    )


def command(
    repository: Path, qemu: Path, board: str, elf: Path, semihosting: bool = False
) -> list[str]:
    if board not in boards(repository):
        raise QemuError(f"unknown physical board: {board}")
    qemu, elf = qemu.resolve(), elf.resolve()
    if not qemu.is_file() or not os.access(qemu, os.X_OK):
        raise QemuError(f"QEMU executable is missing: {qemu}")
    with elf.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            raise QemuError(
                "--elf requires an ELF; raw signed/CRC flash images are unsupported"
            )
    result = [
        str(qemu),
        "-M",
        board,
        "-display",
        "none",
        "-monitor",
        "none",
        "-serial",
        "stdio",
        "-kernel",
        str(elf),
    ]
    if semihosting:
        result += ["-semihosting-config", "enable=on,target=native"]
    return result


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _build(repository: Path, args: argparse.Namespace) -> dict:
    if args.jobs < 1:
        raise QemuError("--jobs must be positive")
    source = resolve_source(repository, args.workspace, args.source)
    output = args.build_dir.resolve()
    if output == source or source.is_relative_to(output):
        raise QemuError("--build-dir must be a separate output directory")
    output.mkdir(parents=True, exist_ok=True)
    with (output / "bk7258-build.log").open("w") as log:
        _call(
            [
                str(source / "configure"),
                "--target-list=arm-softmmu",
                "--without-default-features",
                "--disable-docs",
                "--disable-user",
                "--disable-tools",
                "--disable-rust",
                "--enable-tcg",
                "--enable-werror",
            ],
            cwd=output,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        _call(
            ["ninja", "-C", str(output), "-j", str(args.jobs), "qemu-system-arm"],
            stdout=log,
            stderr=subprocess.STDOUT,
        )
    executable = output / "qemu-system-arm"
    return {
        "qemu": str(executable),
        "sha256": _sha256(executable),
        "source_commit": _git(source, "rev-parse", "HEAD"),
        "source_dirty": bool(_git(source, "status", "--porcelain")),
        "log": str(output / "bk7258-build.log"),
    }


def _smoke(repository: Path, args: argparse.Namespace) -> dict:
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "evidence.json").write_text('{"status": "running"}\n')
    fixture = repository / "tests/host/bk7258/qemu"
    elf = output / "diagnostic.elf"
    compiler = str(args.cc.resolve())
    _call(
        [
            compiler,
            "-mcpu=cortex-m33",
            "-mthumb",
            "-ffreestanding",
            "-nostdlib",
            "-Os",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-Wl,-T,{fixture / 'diagnostic.ld'}",
            str(fixture / "diagnostic.c"),
            "-o",
            str(elf),
        ]
    )
    cases = []
    selected = args.board or boards(repository)
    for board in selected:
        argv = command(repository, args.qemu, board, elf, True)
        for name, data, expected, marker in (
            ("diagnostic", b"Z", 0, b"BK7258 DIAGNOSTIC PASS"),
            ("bad-rx", b"X", 1, b"BK7258 FAULT"),
        ):
            log = output / f"{board}-{name}.log"
            errors = output / f"{board}-{name}-mmio.log"
            try:
                result = subprocess.run(
                    argv + ["-d", "guest_errors,unimp", "-D", str(errors)],
                    input=data,
                    capture_output=True,
                    timeout=args.timeout,
                )
                log.write_bytes(result.stdout + result.stderr)
                passed = (
                    result.returncode == expected
                    and marker in result.stdout
                    and not errors.read_bytes()
                )
                if name == "diagnostic":
                    required = (
                        b"BK7258 SRAM ALIASES OK",
                        b"BK7258 SYSTICK IRQ OK",
                        b"BK7258 EXTERNAL 32K SYSTICK OK",
                        b"BK7258 CPU1 CPU2 RELEASE AND PRIVATE TCM OK",
                        b"BK7258 HALT RESUME AND RESET OK",
                        b"BK7258 UART RX IRQ OK",
                        b"BK7258 SYSTEM RESET OK",
                    )
                    passed = (
                        passed
                        and all(item in result.stdout for item in required)
                        and result.stdout.count(b"BK7258 CP UART OK") == 2
                    )
            except subprocess.TimeoutExpired as error:
                log.write_bytes((error.stdout or b"") + (error.stderr or b""))
                passed = False
            cases.append(
                {
                    "board": board,
                    "case": name,
                    "passed": passed,
                    "log": str(log),
                    "mmio_log": str(errors),
                }
            )
    report = {
        "status": "passed" if all(row["passed"] for row in cases) else "failed",
        "scope": "unsigned bare-metal diagnostic; not NuttX, SMP or hardware acceptance",
        "qemu": str(args.qemu.resolve()),
        "qemu_sha256": _sha256(args.qemu),
        "qemu_version": _call(
            [str(args.qemu.resolve()), "--version"], capture_output=True, text=True
        ).stdout.splitlines()[0],
        "compiler": _call(
            [compiler, "--version"], capture_output=True, text=True
        ).stdout.splitlines()[0],
        "elf": str(elf),
        "elf_sha256": _sha256(elf),
        "cases": cases,
    }
    evidence = output / "evidence.json"
    evidence.write_text(json.dumps(report, indent=2) + "\n")
    if not cases or not all(row["passed"] for row in cases):
        raise QemuError(f"diagnostic failed; inspect {evidence}")
    return report


def run(repository: Path, args: argparse.Namespace) -> dict:
    if args.qemu_command == "build":
        return _build(repository, args)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        raise QemuError("--timeout must be finite and positive")
    if args.qemu_command == "smoke":
        return _smoke(repository, args)
    argv = command(repository, args.qemu, args.board, args.elf, args.semihosting)
    _call(argv, timeout=args.timeout)
    return {"board": args.board, "elf": str(args.elf.resolve()), "exit_status": 0}
