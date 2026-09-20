#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Audit the six declared BK7258 kernel-wrapper ELFs.

This is deliberately an evidence collector, not a build selector: artifacts are
read only from the paths recorded in --baseline-json (or their saved snapshots).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import unittest
from dataclasses import dataclass
from pathlib import Path
from typing import Any


EXPECTED = (
    {("aidk_ai_toy", role) for role in ("ap", "cp")}
    | {("t5_board", role) for role in ("ap", "cp")}
    | {("t5ai_core", role) for role in ("ap", "cp")}
)
FUNCTION = re.compile(r"^\s*([0-9a-fA-F]+) <([^>]+)>:$")
DIRECT_BRANCH = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2,8}\s+)+"
    r"(b(?:l|lx)?(?:\.[a-z]+)?)\s+([0-9a-fA-F]+)\s+<([^>]+)>"
)
INDIRECT_BRANCH = re.compile(
    r"^\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{2,8}\s+)+"
    r"(?:blx?|bx)\s+(?:r[0-9]+|ip|lr|pc)\b|\bldr(?:\.w)?\s+pc,"
)
NM = re.compile(r"^([0-9a-fA-F]+)\s+(?:[0-9a-fA-F]+\s+)?([A-Za-z])\s+(.+)$")
OBJECT_BASENAMES = (
    "arm_exception.S.o",
    "arm_doirq.c.o",
    "sched_switchcontext.c.o",
    "nx_start.c.o",
    "nx_bringup.c.o",
    "bk7258_vectors.c.o",
    "bk7258_ap_vectors.c.o",
    "bk7258_ap_smp.c.o",
)
RELOCATION_TARGET = re.compile(
    r"(?:exception_common|arm_doirq|nxsched_resume_scheduler|nx_bringup|"
    r"__real_|__wrap_)",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class Branch:
    caller: str
    address: str
    mnemonic: str
    target_address: str
    target: str


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _text_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8", "surrogateescape")).hexdigest()


def _run(command: list[str]) -> str:
    result = subprocess.run(
        command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )
    if result.returncode:
        raise RuntimeError(f"{' '.join(command)} failed: {result.stderr.strip()}")
    return result.stdout


def _tool(prefix: str, name: str) -> str:
    candidate = prefix + name
    if shutil.which(candidate) is None:
        raise RuntimeError(f"missing tool: {candidate}")
    return candidate


def parse_branches(
    disassembly: str,
) -> tuple[dict[str, str], list[Branch], dict[str, list[str]]]:
    """Parse objdump's Thumb ``bl`` and ``b.w`` forms without symbol-only proof."""

    symbols: dict[str, str] = {}
    branches: list[Branch] = []
    indirect: dict[str, list[str]] = {}
    caller = ""
    for line in disassembly.splitlines():
        header = FUNCTION.match(line)
        if header:
            caller = header.group(2)
            symbols[caller] = header.group(1).lower()
            continue
        direct = DIRECT_BRANCH.match(line)
        if direct and caller:
            branches.append(
                Branch(
                    caller,
                    direct.group(1).lower(),
                    direct.group(2),
                    direct.group(3).lower(),
                    direct.group(4),
                )
            )
        elif caller and INDIRECT_BRANCH.search(line):
            indirect.setdefault(caller, []).append(line.strip())
    return symbols, branches, indirect


def _symbol_definitions(nm_output: str) -> dict[str, str]:
    found: dict[str, str] = {}
    for line in nm_output.splitlines():
        match = NM.match(line)
        if match and match.group(2).lower() in {"t", "w"}:
            found[match.group(3)] = match.group(1).lower()
    return found


def _branch_rows(branches: list[Branch], caller: str) -> list[dict[str, str]]:
    return [branch.__dict__ for branch in branches if branch.caller == caller]


def _same_address(left: str | None, right: str | None) -> bool:
    return left is not None and right is not None and int(left, 16) == int(right, 16)


def _check_chain(
    role: str,
    branches: list[Branch],
    definitions: dict[str, str],
    parsed: dict[str, str],
) -> tuple[list[str], dict[str, Any]]:
    expected = [
        ("exception_common", "__wrap_arm_doirq"),
        ("__wrap_arm_doirq", "arm_doirq"),
        ("arm_doirq", "__wrap_nxsched_resume_scheduler"),
        ("__wrap_nxsched_resume_scheduler", "nxsched_resume_scheduler"),
    ]
    if role == "ap":
        expected += [
            ("nx_start", "__wrap_nx_bringup"),
            ("__wrap_nx_bringup", "nx_bringup"),
        ]
    else:
        expected.append(("nx_start", "nx_bringup"))
    errors: list[str] = []
    chain: list[dict[str, Any]] = []
    for caller, target in expected:
        rows = [
            row for row in branches if row.caller == caller and row.target == target
        ]
        item: dict[str, Any] = {
            "caller": caller,
            "target": target,
            "branches": [row.__dict__ for row in rows],
        }
        if not rows:
            errors.append(f"missing direct branch {caller}->{target}")
        elif len(rows) != 1:
            errors.append(
                f"expected one direct call {caller}->{target}, got {len(rows)}"
            )
        elif not _same_address(definitions.get(target), rows[0].target_address):
            errors.append(
                f"branch target mismatch {caller}->{target}: "
                f"{rows[0].target_address}!={definitions.get(target)}"
            )
        item["definition"] = definitions.get(target)
        item["parsed_definition"] = parsed.get(target)
        chain.append(item)

    allowed = {
        "arm_doirq": {"__wrap_arm_doirq"},
        "nxsched_resume_scheduler": {"__wrap_nxsched_resume_scheduler"},
        "nx_bringup": {"__wrap_nx_bringup"} if role == "ap" else {"nx_start"},
    }
    bypasses: list[dict[str, str]] = []
    for row in branches:
        if row.target in allowed and row.caller not in allowed[row.target]:
            bypasses.append(row.__dict__)
            errors.append(f"direct bypass {row.caller}->{row.target}")
        if row.caller == row.target and row.caller in {
            "exception_common",
            "__wrap_arm_doirq",
            "arm_doirq",
            "__wrap_nxsched_resume_scheduler",
            "nx_start",
            "__wrap_nx_bringup",
        }:
            errors.append(f"direct recursion {row.caller}->{row.target}")
    if role == "cp" and "__wrap_nx_bringup" in definitions:
        errors.append("CP unexpectedly defines __wrap_nx_bringup")
    return errors, {"expected_chain": chain, "direct_bypasses": bypasses}


def _metadata_checks(snapshot: Path) -> tuple[list[str], dict[str, Any]]:
    errors: list[str] = []
    config = snapshot / ".config"
    ninja = snapshot / "build.ninja"
    if not config.is_file() or not ninja.is_file():
        return ["missing .config or build.ninja"], {}
    config_text = config.read_text(encoding="utf-8", errors="replace")
    ninja_text = ninja.read_text(encoding="utf-8", errors="replace")
    if "CONFIG_LTO_NONE=y" not in config_text or "CONFIG_LTO_FULL=y" in config_text:
        errors.append("unexpected LTO config (expected CONFIG_LTO_NONE=y)")
    lto_flags = sorted(set(re.findall(r"-flto(?:=[^\s]+)?", ninja_text)))
    if lto_flags:
        errors.append(f"unexpected LTO compiler flags: {', '.join(lto_flags)}")
    optimizations = sorted(set(re.findall(r"-O(?:[0-3]|s|g|fast)\b", ninja_text)))
    if optimizations != ["-Os"]:
        errors.append(
            f"unexpected optimization flags: {', '.join(optimizations) or 'none'}"
        )
    return errors, {
        "config": str(config),
        "build_ninja": str(ninja),
        "lto_flags": lto_flags,
        "optimizations": optimizations,
        "config_sha256": _sha256(config),
        "build_ninja_sha256": _sha256(ninja),
    }


def _entry(record: dict[str, Any], source: str) -> tuple[Path, Path]:
    snapshot = Path(record["snapshot"])
    if source == "snapshot":
        return snapshot / "nuttx", snapshot
    return Path(record["elf"]), Path(record["elf"]).parent


def _object_relocations(
    root: Path, destination: Path, readelf: str, source: str
) -> dict[str, Any]:
    """Preserve current-role object relocation evidence without choosing another build."""

    limitation = (
        "not collected from preserved snapshots; object files were not saved"
        if source == "snapshot"
        else None
    )
    if limitation is not None:
        return {
            "status": "not-saved",
            "limitation": limitation,
            "objects": [],
            "missing_basenames": list(OBJECT_BASENAMES),
        }
    object_dir = destination / "objects"
    object_dir.mkdir(exist_ok=True)
    rows: list[dict[str, Any]] = []
    missing: list[str] = []
    index = 0
    for basename in OBJECT_BASENAMES:
        matches = sorted(path for path in root.rglob(basename) if path.is_file())
        if not matches:
            missing.append(basename)
            continue
        for path in matches:
            index += 1
            copied = object_dir / f"{index:02d}-{basename}"
            shutil.copy2(path, copied)
            relocations = _run([readelf, "-rW", str(path)])
            relocation_file = object_dir / f"{index:02d}-{basename}.relocations.txt"
            relocation_file.write_text(relocations, encoding="utf-8")
            relevant = [
                line
                for line in relocations.splitlines()
                if RELOCATION_TARGET.search(line)
            ]
            rows.append(
                {
                    "basename": basename,
                    "path": str(path),
                    "sha256": _sha256(path),
                    "copied": str(copied),
                    "relocations": str(relocation_file),
                    "relocations_sha256": _text_hash(relocations),
                    "relevant_relocations": relevant,
                }
            )
    return {"status": "collected", "objects": rows, "missing_basenames": missing}


def audit(baseline: Path, output: Path, prefix: str, source: str) -> dict[str, Any]:
    document = json.loads(baseline.read_text(encoding="utf-8"))
    records = document.get("artifacts")
    if (
        not isinstance(records, list)
        or {(row.get("board"), row.get("role")) for row in records} != EXPECTED
    ):
        raise ValueError(
            "baseline must declare exactly the six BK7258 board/role artifacts"
        )
    baseline_root = baseline.parent.resolve()
    if output.resolve().is_relative_to(baseline_root):
        raise ValueError("--output must not be inside the preserved baseline directory")
    output.mkdir(parents=True, exist_ok=True)
    objdump, nm, readelf = (
        _tool(prefix, name) for name in ("objdump", "nm", "readelf")
    )
    matrix: list[dict[str, Any]] = []
    failures = 0
    for record in sorted(records, key=lambda row: (row["board"], row["role"])):
        elf, metadata_dir = _entry(record, source)
        if not elf.is_file():
            raise FileNotFoundError(f"declared ELF is unavailable: {elf}")
        disassembly = _run([objdump, "-d", str(elf)])
        symbols = _run([nm, "-n", "-S", str(elf)])
        relocations = _run([readelf, "-rW", str(elf)])
        parsed, branches, indirect = parse_branches(disassembly)
        definitions = _symbol_definitions(symbols)
        errors, chain = _check_chain(record["role"], branches, definitions, parsed)
        metadata_errors, metadata = _metadata_checks(metadata_dir)
        errors += metadata_errors
        key = f"{record['board']}-{record['role']}"
        destination = output / key
        destination.mkdir(exist_ok=True)
        (destination / "disassembly.txt").write_text(disassembly, encoding="utf-8")
        (destination / "symbols.txt").write_text(symbols, encoding="utf-8")
        (destination / "relocations.txt").write_text(relocations, encoding="utf-8")
        object_evidence = _object_relocations(elf.parent, destination, readelf, source)
        item = {
            "board": record["board"],
            "role": record["role"],
            "source": source,
            "elf": str(elf),
            "elf_sha256": _sha256(elf),
            "metadata": metadata,
            "disassembly_sha256": _text_hash(disassembly),
            "symbols_sha256": _text_hash(symbols),
            "relocations_sha256": _text_hash(relocations),
            "branch_callers": {
                name: _branch_rows(branches, name)
                for name in (
                    "exception_common",
                    "__wrap_arm_doirq",
                    "arm_doirq",
                    "__wrap_nxsched_resume_scheduler",
                    "nx_start",
                    "__wrap_nx_bringup",
                )
            },
            "indirect_transfer_limitations": {
                name: indirect.get(name, [])
                for name in (
                    "__wrap_arm_doirq",
                    "arm_doirq",
                    "__wrap_nxsched_resume_scheduler",
                    "nx_start",
                    "__wrap_nx_bringup",
                )
                if indirect.get(name)
            },
            "relocation_evidence": (
                "none" if "There are no relocations" in relocations else "present"
            ),
            "object_relocation_evidence": object_evidence,
            "same_tu_limitations": [
                "this audit does not assert a sched_switchcontext same-TU bypass; "
                "object relocation evidence is not runtime coverage or correctness proof"
            ],
            "checks": chain,
            "errors": errors,
            "pass": not errors,
        }
        (destination / "result.json").write_text(
            json.dumps(item, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        matrix.append(item)
        failures += bool(errors)
    result = {
        "baseline": str(baseline),
        "source": source,
        "tool_prefix": prefix,
        "artifacts": matrix,
        "pass": failures == 0,
    }
    (output / "matrix.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result


class ParserTest(unittest.TestCase):
    def test_missing_duplicate_and_bypass_are_rejected(self) -> None:
        pairs = [
            ("exception_common", "__wrap_arm_doirq"),
            ("__wrap_arm_doirq", "arm_doirq"),
            ("arm_doirq", "__wrap_nxsched_resume_scheduler"),
            ("__wrap_nxsched_resume_scheduler", "nxsched_resume_scheduler"),
            ("nx_start", "nx_bringup"),
        ]
        names = sorted({n for pair in pairs for n in pair})
        definitions = {
            name: hex(0x2000 + i * 0x100)[2:] for i, name in enumerate(names)
        }
        branches = [
            Branch(caller, definitions[caller], "bl", definitions[target], target)
            for caller, target in pairs
        ]
        self.assertFalse(_check_chain("cp", branches, definitions, definitions)[0])
        self.assertTrue(_check_chain("cp", branches[1:], definitions, definitions)[0])
        self.assertTrue(
            _check_chain("cp", branches + [branches[0]], definitions, definitions)[0]
        )
        bypass = Branch("other", "9000", "bl", definitions["arm_doirq"], "arm_doirq")
        self.assertTrue(
            _check_chain("cp", branches + [bypass], definitions, definitions)[0]
        )

    def test_thumb_bl_and_branch_w(self) -> None:
        text = """02151c30 <exception_common>:\n 2151c76: f7fe fc93 bl 21505a0 <__wrap_arm_doirq>\n021505a0 <__wrap_arm_doirq>:\n 21505da: f001 fb67 bl 2151cac <arm_doirq>\n02151cac <arm_doirq>:\n 2151cf0: f000 b812 b.w 2150500 <__wrap_nxsched_resume_scheduler>\n"""
        symbols, branches, indirect = parse_branches(text)
        self.assertEqual(symbols["exception_common"], "02151c30")
        self.assertEqual(
            [(row.caller, row.target, row.target_address) for row in branches],
            [
                ("exception_common", "__wrap_arm_doirq", "21505a0"),
                ("__wrap_arm_doirq", "arm_doirq", "2151cac"),
                ("arm_doirq", "__wrap_nxsched_resume_scheduler", "2150500"),
            ],
        )
        self.assertEqual(indirect, {})


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-json", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--tool-prefix", default="arm-none-eabi-")
    parser.add_argument("--source", choices=("snapshot", "current"), default="snapshot")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return (
            0
            if unittest.main(argv=[sys.argv[0]], exit=False).result.wasSuccessful()
            else 1
        )
    if args.baseline_json is None or args.output is None:
        parser.error("--baseline-json and --output are required")
    result = audit(
        args.baseline_json.resolve(),
        args.output.resolve(),
        args.tool_prefix,
        args.source,
    )
    print(
        json.dumps(
            {"pass": result["pass"], "matrix": str(args.output / "matrix.json")},
            sort_keys=True,
        )
    )
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
