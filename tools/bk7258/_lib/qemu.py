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
import select
import time
from collections import Counter
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
    nsh = commands.add_parser(
        "native-nsh", help="verify the restricted native NuttX CP profile"
    )
    nsh.add_argument("--qemu", type=Path, required=True)
    nsh.add_argument("--build-manifest", type=Path, required=True)
    nsh.add_argument("--output", type=Path, required=True)
    nsh.add_argument("--timeout", type=float, default=10)
    nsh.add_argument(
        "--physical-nor",
        action="store_true",
        help="create a fresh simulated NOR in output and enter CP through CRC XIP",
    )


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
                        b"BK7258 WATCHDOG NMI OK",
                        b"BK7258 SYSTICK IRQ OK",
                        b"BK7258 EXTERNAL 32K SYSTICK OK",
                        b"BK7258 CPU1 CPU2 RELEASE AND PRIVATE TCM OK",
                        b"BK7258 HALT RESUME AND RESET OK",
                        b"BK7258 MAILBOX THREE CORE IRQ AND PROTECTION OK",
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


# Upstream arm_enable_dbgmonitor() is selected by ARCH_CORTEXM33 even when
# DEBUG_FEATURES and ARCH_PERF_EVENTS are disabled. QEMU lacks these optional
# hardware debug blocks. Preserve every message and reject any other access;
# this exception does not permit DWT cycle-counter reads or fabricated timing.
_DEBUG_PROBES = Counter(
    {
        "NVIC: Bad read offset 0xdf0": 1,
        "Read of unassigned area of PPB: offset 0x2000": 3,
        "Write of unassigned area of PPB: offset 0x2000": 1,
        "Read of unassigned area of PPB: offset 0x1000": 1,
        "NVIC: Bad read offset 0xdfc": 1,
        "NVIC: Bad write offset 0xdfc": 1,
    }
)


def _native_nsh(repository: Path, args: argparse.Namespace) -> dict:
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    evidence = output / "evidence.json"
    evidence.write_text('{"status": "running", "cases": []}\n')
    for name in ("uart.log", "mmio.log", "stderr.log"):
        (output / name).write_bytes(b"")
    try:
        return _native_nsh_run(repository, args)
    except Exception as error:
        report = json.loads(evidence.read_text())
        report.update(status="failed", error=str(error))
        evidence.write_text(json.dumps(report, indent=2) + "\n")
        if isinstance(error, QemuError):
            raise
        raise QemuError(f"native NSH failed: {error}") from error


def _diagnostic_nor(manifest, output: Path) -> tuple[list[str], dict]:
    """Create only a fresh simulation image, never reuse device-unique material."""
    if manifest.boot != "direct" or manifest.layout.flash_size != 8 * 1024 * 1024:
        raise QemuError("diagnostic NOR requires a direct 8-MiB build")
    image, status = output / "diagnostic-nor.bin", output / "diagnostic-nor.status"
    if image.exists() or status.exists():
        raise QemuError(
            "diagnostic NOR already exists; choose a fresh output directory"
        )
    data = bytearray(b"\xff" * manifest.layout.flash_size)
    placements = []
    # pair.bin starts at the CP partition, not physical offset zero. The
    # authoritative layout selects each independently finalized artifact.
    for row in manifest.layout.partitions:
        if row.artifact not in {"boot", "cp", "ap"}:
            continue
        payload = manifest.finalized_artifacts[row.artifact].read_bytes()
        if len(payload) > row.size:
            raise QemuError(f"diagnostic NOR artifact exceeds {row.name}")
        data[row.offset : row.offset + len(payload)] = payload
        placements.append(
            {
                "artifact": row.artifact,
                "offset": row.offset,
                "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )
    if {row["artifact"] for row in placements} != {"boot", "cp", "ap"}:
        raise QemuError(
            "diagnostic NOR requires exactly the direct boot/CP/AP artifacts"
        )
    with image.open("xb") as stream:
        stream.write(data)
    with status.open("xb") as stream:
        stream.write(b"\x00\x00\x20" + bytes(509))
    argv = []
    for unit, path in enumerate((image, status)):
        argv += [
            "-drive",
            f"if=pflash,unit={unit},format=raw,file={str(path).replace(',', ',,')}",
        ]
    return argv, {
        "scope": "fresh simulation NOR; no device identity/calibration; CP entry only",
        "part": "GD25WQ64E model choice",
        "placements": placements,
        "initial_array_sha256": _sha256(image),
        "initial_status_sha256": _sha256(status),
    }


def _native_nsh_run(repository: Path, args: argparse.Namespace) -> dict:
    from . import build as build_domain

    manifest = build_domain.load_build_manifest(repository, args.build_manifest)
    profile = "boards/bk7258/aidk_ai_toy/configs/native_nsh"
    if (
        manifest.boot != "direct"
        or not manifest.provenance
        or manifest.provenance["profiles"]["cp"] != profile
    ):
        raise QemuError("native-nsh requires the direct native_nsh build manifest")
    elf = manifest.elfs["cp"]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    evidence = output / "evidence.json"
    evidence.write_text('{"status": "running"}\n')
    mmio = output / "mmio.log"
    log = bytearray()
    cases = []
    report = {
        "status": "failed",
        "scope": "restricted native NuttX CP NSH only",
        "board": manifest.physical_board,
        "profile": profile,
        "elf_sha256": _sha256(elf),
        "build_manifest_sha256": _sha256(args.build_manifest),
        "build_provenance": manifest.provenance,
        "qemu_sha256": _sha256(args.qemu),
        "cases": cases,
    }
    argv = command(repository, args.qemu, manifest.physical_board, elf)
    if getattr(args, "physical_nor", False):
        drives, report["nor"] = _diagnostic_nor(manifest, output)
        index = argv.index("-kernel")
        del argv[index : index + 2]
        argv += drives
    report["launch_argv"] = argv
    with (output / "stderr.log").open("wb") as stderr:
        process = subprocess.Popen(
            argv + ["-d", "guest_errors,unimp", "-D", str(mmio)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=stderr,
            bufsize=0,
        )

        def prompt(command_text=None):
            start = len(log)
            if command_text is not None:
                process.stdin.write(command_text.encode() + b"\n")
                process.stdin.flush()
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise QemuError(f"native NSH exited: {process.returncode}")
                if mmio.exists() and mmio.stat().st_size > 4 * 1024 * 1024:
                    raise QemuError("native NSH exceeded the bounded MMIO log budget")
                ready, _, _ = select.select(
                    [process.stdout],
                    [],
                    [],
                    max(0, min(0.1, deadline - time.monotonic())),
                )
                if ready:
                    data = os.read(process.stdout.fileno(), 65536)
                    if not data:
                        raise QemuError("native NSH output closed")
                    log.extend(data)
                    if len(log) > 2 * 1024 * 1024:
                        raise QemuError("native NSH exceeded the bounded log budget")
                    if b"nsh> " in log[start:]:
                        return bytes(log[start:]).decode(errors="replace")
            raise QemuError(f"native NSH prompt timed out: {command_text!r}")

        def check(name, condition):
            cases.append({"case": name, "passed": bool(condition)})
            if not condition:
                raise QemuError(f"native NSH check failed: {name}")

        def uptime():
            text = prompt("cat /proc/uptime")
            value = re.search(r"(?m)^\s*(\d+\.\d+)\s*$", text)
            if value is None:
                raise QemuError("native NSH uptime is missing")
            return float(value[1])

        try:
            check("boot", "NuttShell (NSH)" in prompt())
            identity = prompt("uname -a")
            check(
                "native-kernel", "NuttX " in identity and " arm aidk_ai_toy" in identity
            )
            prompt("mount -t procfs /proc")
            tasks = prompt("ps")
            check(
                "kernel-tasks",
                all(x in tasks for x in ("CPU0 IDLE", "lpwork", "nsh_main")),
            )
            started = prompt("sleep 3 &")
            match = re.search(r"sh \[(\d+):\d+\]", started)
            check("background-created", match is not None)
            tasks = prompt("ps")
            check(
                "background-blocks",
                bool(
                    re.search(
                        rf"(?m)^\s*{match[1]}\s+.*Waiting\s+Signal.*sh -c sleep", tasks
                    )
                ),
            )
            check(
                "foreground-progress",
                bool(
                    re.search(
                        r"(?m)^foreground_alive\r?$", prompt("echo foreground_alive")
                    )
                ),
            )
            before = uptime()
            prompt("sleep 1")
            after = uptime()
            check("systick-sleep-elapsed", after - before >= 0.99)
            prompt("sleep 3")
            tasks = prompt("ps")
            check("background-exited", not re.search(rf"(?m)^\s*{match[1]}\s+", tasks))
            check(
                "invalid-command",
                "command not found" in prompt("bk7258_invalid_command"),
            )
            check(
                "still-alive-after-error",
                bool(
                    re.search(
                        r"(?m)^error_recovered\r?$", prompt("echo error_recovered")
                    )
                ),
            )
            check("guest-reboot", "NuttShell (NSH)" in prompt("reboot"))
            prompt("mount -t procfs /proc")
            check("reboot-resets-uptime", uptime() < after)
            check(
                "post-reset-console",
                bool(re.search(r"(?m)^reset_alive\r?$", prompt("echo reset_alive"))),
            )
            process.terminate()
            process.wait(timeout=5)
            observed = Counter(mmio.read_text().splitlines())
            check(
                "only-documented-debug-probes",
                observed
                == Counter({key: value * 2 for key, value in _DEBUG_PROBES.items()}),
            )
            if "nor" in report:
                report["nor"]["final_array_sha256"] = _sha256(
                    output / "diagnostic-nor.bin"
                )
                report["nor"]["final_status_sha256"] = _sha256(
                    output / "diagnostic-nor.status"
                )
            report["status"] = "passed"
            report["debug_probe_counts"] = dict(observed)
            report["uptime_before_sleep"] = before
            report["uptime_after_sleep"] = after
        except QemuError as error:
            report["error"] = str(error)
            raise
        finally:
            process.terminate()
            try:
                remaining, _ = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                remaining, _ = process.communicate(timeout=5)
            log.extend(remaining)
            (output / "uart.log").write_bytes(log)
            evidence.write_text(json.dumps(report, indent=2) + "\n")
    return report


def run(repository: Path, args: argparse.Namespace) -> dict:
    if args.qemu_command == "build":
        return _build(repository, args)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        raise QemuError("--timeout must be finite and positive")
    if args.qemu_command == "native-nsh":
        return _native_nsh(repository, args)
    if args.qemu_command == "smoke":
        return _smoke(repository, args)
    argv = command(repository, args.qemu, args.board, args.elf, args.semihosting)
    _call(argv, timeout=args.timeout)
    return {"board": args.board, "elf": str(args.elf.resolve()), "exit_status": 0}
