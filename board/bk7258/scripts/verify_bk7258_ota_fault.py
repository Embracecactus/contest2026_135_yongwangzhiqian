#!/usr/bin/env python3
"""Verify the validation-only BK7258 N15-V deterministic failpoints."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


class FaultVerificationError(RuntimeError):
    """Raised when an N15-V fault-injection invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise FaultVerificationError(message)


def compile_harness(repo: Path, output: Path, *, analyzer: bool = False) -> None:
    compiler = shutil.which("cc")
    if compiler is None:
        raise FaultVerificationError("host cc is unavailable")
    board = repo / "board/bk7258"
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{board / 'chip/include'}",
        f"-I{board / 'chip/cp'}",
        str(board / "chip/cp/bk7258_ota_fault_core.c"),
        str(board / "scripts/host/bk7258_ota_fault_harness.c"),
    ]
    if analyzer:
        command.extend(["-fanalyzer", "-fsyntax-only"])
    else:
        command.extend(["-o", str(output)])
    subprocess.run(command, check=True, timeout=60)


def source_contract(repo: Path) -> None:
    board = repo / "board/bk7258"
    paths = {
        "public": board / "chip/include/bk7258_ota_fault.h",
        "core_header": board / "chip/cp/bk7258_ota_fault_core.h",
        "core": board / "chip/cp/bk7258_ota_fault_core.c",
        "adapter": board / "chip/cp/bk7258_ota_fault.c",
        "staging": board / "chip/cp/bk7258_ota_staging.c",
        "trial": board / "chip/cp/bk7258_ota_trial.c",
        "app": repo / "app/hello_app/bk7258_ota_main.c",
        "kconfig": board / "chip/Kconfig",
        "make": board / "chip/Make.defs",
        "cmake": board / "chip/CMakeLists.txt",
        "normal": board / "configs/cp_nsh_psram/defconfig",
        "validation": board / "configs/cp_nsh_ota/defconfig",
        "build": board / "scripts/build_dual_image.sh",
    }
    text = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}

    for token in (
        "BK7258_OTA_FAULT_STAGE_ERASE",
        "BK7258_OTA_FAULT_STAGE_WRITE",
        "BK7258_OTA_FAULT_STAGE_READ",
        "BK7258_OTA_FAULT_PUBLISH_READ",
        "BK7258_OTA_FAULT_PUBLISH_ERASE",
        "BK7258_OTA_FAULT_PUBLISH_WRITE",
        "BK7258_OTA_FAULT_TRIAL_READ",
        "BK7258_OTA_FAULT_TRIAL_WRITE",
        "BK7258_OTA_FAULT_MAX_ORDINAL 65535u",
    ):
        require(token in text["public"], f"public fault ABI missing {token}")
    for token in (
        "generation != generation",
        "allowed_mask & point_bit",
        "status.seen++",
        "status.seen != plan->status.ordinal",
        "status.triggered = true",
        "return -ECANCELED",
        "bk7258_ota_fault_core_initialize(plan)",
    ):
        require(token in text["core"], f"fault core contract missing {token}")
    for forbidden in (
        "<nuttx/",
        "<driver/",
        "malloc(",
        "free(",
        "printf(",
        "memcpy(",
        "memset(",
    ):
        require(forbidden not in text["core"], f"portable fault core uses {forbidden}")
    for token in (
        "spin_lock_irqsave",
        "bk7258_ota_fault_core_arm",
        "bk7258_ota_fault_core_begin",
        "bk7258_ota_fault_core_before",
        "bk7258_ota_fault_core_finish",
    ):
        require(token in text["adapter"], f"fault adapter missing {token}")
    for token in (
        "BK7258_OTA_FAULT_STAGE_ERASE",
        "BK7258_OTA_FAULT_STAGE_WRITE",
        "BK7258_OTA_FAULT_STAGE_READ",
    ):
        require(token in text["staging"], f"staging callback missing {token}")
    for token in (
        "bk7258_ota_publish_raw_read",
        "BK7258_OTA_FAULT_PUBLISH_READ",
        "BK7258_OTA_FAULT_PUBLISH_ERASE",
        "BK7258_OTA_FAULT_PUBLISH_WRITE",
        "BK7258_OTA_FAULT_TRIAL_READ",
        "BK7258_OTA_FAULT_TRIAL_WRITE",
    ):
        require(token in text["trial"], f"metadata callback missing {token}")
    for token in (
        "bkota fault-arm",
        "bkota fault-status",
        "bkota fault-clear",
        "bkota corrupt-mem",
        "bk7258_ota_fault_begin",
        "bkota_fault_finish_session",
        "BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS + offset",
        "BKOTA_TOKEN_PREFIX",
        "one_shot=1",
    ):
        require(token in text["app"], f"bkota fault contract missing {token}")
    for token in (
        "config BK7258_OTA_FAULT_INJECTION",
        "depends on BK7258_OTA_VALIDATION",
        "generation-bound, one-shot failpoints",
    ):
        require(token in text["kconfig"], f"fault Kconfig missing {token}")
    require(
        "CONFIG_BK7258_OTA_FAULT_INJECTION=y" in text["validation"],
        "validation profile omits deterministic failpoints",
    )
    require(
        "CONFIG_BK7258_OTA_FAULT_INJECTION=y" not in text["normal"],
        "normal profile enables deterministic failpoints",
    )
    for token in ("bk7258_ota_fault_core.c", "bk7258_ota_fault.c"):
        require(token in text["make"], f"Make.defs omits {token}")
        require(token in text["cmake"], f"CMake omits {token}")
    require(
        "verify_bk7258_ota_fault.py" in text["build"],
        "dual builder omits N15-V fault verifier",
    )


def self_test(repo: Path) -> dict[str, object]:
    source_contract(repo)
    with tempfile.TemporaryDirectory(prefix="bk7258-n15v-fault-") as directory:
        root = Path(directory)
        harness = root / "fault-harness"
        compile_harness(repo, harness)
        compile_harness(repo, root / "unused", analyzer=True)
        result = subprocess.run(
            [str(harness)],
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
        )
        require(
            result.returncode == 0,
            f"fault harness failed: {result.stdout}{result.stderr}",
        )
        match = re.search(r"PASS: positive=(\d+) negative=(\d+)", result.stdout)
        require(match is not None, "fault harness summary is absent")
        positive = int(match.group(1))
        negative = int(match.group(2))
        require(positive == 7, f"fault positive count drift: {positive}")
        require(negative == 12, f"fault negative count drift: {negative}")

    return {
        "format": 1,
        "status": "pass",
        "positive_cases": positive,
        "negative_cases": negative,
        "generation_bound": True,
        "operation_family_bound": True,
        "one_shot": True,
        "reset_clears_bss_plan": True,
        "normal_profile_enabled": False,
        "board_write_authorized": False,
        "static_analyzer": True,
        "source_contract": True,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if not args.self_test:
        parser.error("choose --self-test")
    repo = Path(__file__).resolve().parents[3]
    try:
        result = self_test(repo)
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
    except (OSError, subprocess.SubprocessError, FaultVerificationError) as error:
        print(f"BK7258 N15-V fault verification FAIL: {error}")
        return 1
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 N15-V fault verification PASS: "
            f"positive={result['positive_cases']} "
            f"negative={result['negative_cases']} "
            "normal_enabled=false board_authorized=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
