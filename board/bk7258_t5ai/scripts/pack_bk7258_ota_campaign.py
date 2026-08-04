#!/usr/bin/env python3
"""Build a deterministic, host-only N15-V board-validation campaign.

Each case receives its own generation, version, timestamp, metadata record and
RBL identity.  This script never opens J-Link and never writes a board.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from pack_bk7258_ota_pair import parse_int


CAMPAIGN_FILE = "bk7258-n15v-campaign.json"
CAMPAIGN_FORMAT = 2
SDK_RELEASE = "v3.1.1.9"
MAX_U64 = (1 << 64) - 1
PUBLISH_TIMEOUT_MS = 180_000


class CampaignError(RuntimeError):
    """Raised when the input baseline or generated campaign is unsafe."""


@dataclass(frozen=True)
class CampaignCase:
    key: str
    version_suffix: str
    purpose: str
    operation: str
    fault_point: str | None = None
    fault_ordinal: int | None = None
    terminal: bool = False
    target_slot: str = "b"


CASES = (
    CampaignCase(
        "candidate-corrupt", "corrupt",
        "Corrupt one bounded PSRAM candidate byte; validation must reject it before Flash mutation.",
        "candidate-corrupt",
    ),
    CampaignCase(
        "stage-timeout", "timeout",
        "Use a 1 ms staging deadline; no trusted pending metadata may appear.",
        "stage-timeout",
    ),
    CampaignCase(
        "stage-erase-fault", "stg-erase",
        "Cancel before the first candidate-sector erase.",
        "stage-fault", "stage-erase", 1,
    ),
    CampaignCase(
        "stage-write-fault", "stg-write",
        "Cancel before the second 256-byte candidate program operation.",
        "stage-fault", "stage-write", 2,
    ),
    CampaignCase(
        "stage-readback-fault", "stg-read",
        "Cancel the first program read-back (stage-read ordinal 17).",
        "stage-fault", "stage-read", 17,
    ),
    CampaignCase(
        "publish-preread-fault", "pub-read",
        "Cancel the publication metadata pre-read before mutation.",
        "publish-fault", "publish-read", 1,
    ),
    CampaignCase(
        "pending-reset-no-confirm", "no-confirm",
        "Commit PENDING_B, consume exactly one trial, omit confirmation, then return to A.",
        "pending-reset",
    ),
    CampaignCase(
        "publish-write-fault", "pub-write",
        "Keep the selected trial bank intact and cancel the second 32-byte write into the erased inactive bank.",
        "publish-fault", "publish-write", 2,
    ),
    CampaignCase(
        "publish-erase-fault", "pub-erase",
        "Preserve the selected bank and cancel before reclaiming the torn inactive bank.",
        "publish-fault", "publish-erase", 1,
    ),
    CampaignCase(
        "publish-readback-fault", "pub-rdback",
        "Reclaim the torn inactive bank, program one chunk, then cancel its read-back (ordinal 4).",
        "publish-fault", "publish-read", 4,
    ),
    CampaignCase(
        "explicit-rollback", "rollback",
        "Commit a new candidate, enter its B trial, append ROLLBACK_A and return to A.",
        "rollback",
    ),
    CampaignCase(
        "trial-read-fault", "trial-read",
        "Cancel rollback metadata read; the consumed trial must still fail closed to A.",
        "trial-fault", "trial-read", 1,
    ),
    CampaignCase(
        "trial-write-fault", "trial-write",
        "Cancel rollback metadata append; the consumed trial must still fail closed to A.",
        "trial-fault", "trial-write", 1,
    ),
    CampaignCase(
        "health-gate-refusal", "health-fail",
        "Inject an AP primary-health fault; confirmation must time out and the next reset returns to A.",
        "health-refusal",
    ),
    CampaignCase(
        "successful-confirm", "confirm",
        "Run the retained N14 health window, append CONFIRMED_B and prove B persists.",
        "confirm",
    ),
    CampaignCase(
        "successful-return-a", "return-a",
        "From confirmed B, stage inactive A, append CONFIRMED_A and prove symmetric return to A.",
        "confirm", terminal=True, target_slot="a",
    ),
)


REQUIRED_PROFILE = {
    "CP_CONFIG_NAME": "cp_nsh_ota",
    "AP_CONFIG_NAME": "ap_smp_psram",
    "BK7258_SDK_BUNDLE_VERSION": SDK_RELEASE,
    "N15_OTA_VALIDATION_ENABLED": "true",
    "N15_OTA_BOOT_GATE_VALUE": "1",
    "N15_OTA_SELECTION_ENABLED": "true",
    "N15_OTA_REMAP_ENABLED": "true",
    "N15_OTA_TRIAL_METADATA_MUTATION_ENABLED": "true",
    "N15_OTA_CP_RUNTIME_GATES_INITIAL": "false",
    "N15_OTA_FAULT_INJECTION_ENABLED": "true",
    "N15_OTA_BOARD_WRITE_AUTHORIZED": "false",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CampaignError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact(path: Path) -> dict[str, object]:
    return {"file": path.name, "size": path.stat().st_size, "sha256": sha256(path)}


def load_json(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(value, dict), f"{path} root must be an object")
    return value


def load_profile(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        require("=" in raw, f"invalid build-profile line {lineno}")
        key, value = raw.split("=", 1)
        require(key and key not in values, f"duplicate build-profile key {key!r}")
        values[key] = value
    return values


def validate_baseline(package: Path, sdk_source: Path) -> dict[str, object]:
    require(package.is_dir(), f"validation package is not a directory: {package}")
    require(
        sdk_source.is_dir() and sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
        "SDK source must be the official bk_avdk_smp-release-v3.1.1.9 tree",
    )
    required_files = (
        "app.bin", "app1.bin", "app_crc.bin", "app1_crc.bin",
        "bootloader.elf", "bootloader.bin", "bl_crc.bin",
        "nuttx-cp.elf", "nuttx-ap.elf", "build-profile.txt",
        "bk7258-ota-validation.json", "bk7258-ota-fault.json",
    )
    for name in required_files:
        require((package / name).is_file(), f"validation baseline omits {name}")

    profile = load_profile(package / "build-profile.txt")
    for key, expected in REQUIRED_PROFILE.items():
        require(profile.get(key) == expected, f"validation profile drift: {key}")
    for report_name in ("bk7258-ota-validation.json", "bk7258-ota-fault.json"):
        report = load_json(package / report_name)
        require(report.get("status") == "pass", f"{report_name} is not PASS")
        require(
            report.get("board_write_authorized") is False,
            f"{report_name} unexpectedly authorizes board writes",
        )

    return {
        "build_profile_sha256": sha256(package / "build-profile.txt"),
        "bootloader_elf": artifact(package / "bootloader.elf"),
        "cp_elf": artifact(package / "nuttx-cp.elf"),
        "ap_elf": artifact(package / "nuttx-ap.elf"),
        "cp_raw": artifact(package / "app.bin"),
        "ap_raw": artifact(package / "app1.bin"),
    }


def run_checked(command: list[str]) -> None:
    result = subprocess.run(
        command, check=False, capture_output=True, text=True, timeout=180
    )
    if result.returncode != 0:
        raise CampaignError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout}{result.stderr}"
        )


def target_step(command: str, expect: str) -> dict[str, str]:
    return {"actor": "target", "command": command, "expect": expect}


def host_step(command: str, expect: str) -> dict[str, str]:
    return {"actor": "host", "command": command, "expect": expect}


def reset_step(expect: str) -> dict[str, str]:
    return {"actor": "operator", "command": "PHYSICAL RESET", "expect": expect}


def power_cycle_step(expect: str) -> dict[str, str]:
    return {
        "actor": "operator",
        "command": "CONTROLLED POWER CYCLE",
        "expect": expect,
    }


def workflow(
    case: CampaignCase,
    package: Path,
    generation: int,
    timestamp: int,
    version: str,
    base_version: str,
) -> list[dict[str, str]]:
    token = f"N15-WRITE-{generation}"
    loader = "board/bk7258_t5ai/scripts/load_bk7258_ota_psram.sh"
    dry_run = f"{loader} --generation {generation} --token {token} --package {package}"
    execute = (
        f"{loader} --generation {generation} --token {token} "
        f"--watchdog-stopped --execute --package {package}"
    )
    validate = f"bkota validate-mem {generation} {timestamp} {version} {base_version}"
    stage = (
        f"bkota stage-mem {generation} {timestamp} {version} {base_version} "
        f"180000 {token}"
    )
    # Publication re-hashes both complete executable pairs before touching
    # metadata.  The physical BK7258 path takes about 34 seconds, so the
    # former 10-second workflow value always timed out before mutation.
    publish = f"bkota publish-mem {generation} {PUBLISH_TIMEOUT_MS} {token}"
    common = [
        host_step(dry_run, "PASS; writes_enabled=false and board_authorized=false"),
        target_step(
            f"bkota prepare-transfer {generation} {token}",
            "BKOTA TRANSFER READY; watchdog_active=0",
        ),
        host_step(execute, "all three volatile PSRAM verifybin operations PASS"),
    ]

    if case.operation == "candidate-corrupt":
        return common + [
            target_step(
                f"bkota corrupt-mem 0x100 0x01 {generation} {token}",
                "ret=0; address remains inside the fixed candidate PSRAM window",
            ),
            target_step(validate, "ret<0; candidate corruption is rejected"),
            target_step("bkota status", "metadata remains erased/unmodified; mapping A"),
            power_cycle_step("A boots; watchdog restored"),
        ]

    if case.operation == "stage-timeout":
        short_stage = (
            f"bkota stage-mem {generation} {timestamp} {version} {base_version} "
            f"1 {token}"
        )
        return common + [
            target_step(validate, "ret=0"),
            target_step(short_stage, "ret=-ETIMEDOUT; no pending metadata"),
            target_step("bkota status", "mapping A; no trusted pending record"),
            power_cycle_step(
                "A boots; partial B remains untrusted and watchdog is restored"
            ),
        ]

    if case.operation == "stage-fault":
        arm = f"bkota fault-arm {case.fault_point} {case.fault_ordinal} {generation} {token}"
        return common + [
            target_step(validate, "ret=0"),
            target_step(arm, "ret=0; generation-bound one-shot plan armed"),
            target_step(stage, "ret=-ECANCELED and BKOTA FAULT RESULT triggered=1"),
            target_step("bkota status", "mapping A; no trusted pending record"),
            power_cycle_step(
                "A boots; committed pre-fault writes remain untrusted, watchdog is "
                "restored and the BSS fault plan is clear"
            ),
        ]

    success_prefix = common + [
        target_step(validate, "ret=0"),
        target_step(stage, "ret=0; full candidate read-back digest matches"),
    ]

    if case.operation == "publish-fault":
        arm = f"bkota fault-arm {case.fault_point} {case.fault_ordinal} {generation} {token}"
        return success_prefix + [
            target_step(arm, "ret=0; generation-bound one-shot plan armed"),
            target_step(publish, "ret=-ECANCELED and BKOTA FAULT RESULT triggered=1"),
            power_cycle_step(
                "A boots; metadata is old-reclaimable, erased, or fail-closed torn"
            ),
            target_step("bkota status", "no mixed generation is trusted"),
        ]

    pending_state = "PENDING_A" if case.target_slot == "a" else "PENDING_B"
    trial_state = "TRIAL_A" if case.target_slot == "a" else "TRIAL_B"
    trial_mapping = "primary" if case.target_slot == "a" else "secondary"
    successful_publish = success_prefix + [
        target_step(publish, f"ret=0; exactly one trusted {pending_state} record"),
        reset_step(
            f"exactly one {case.target_slot.upper()} trial starts; Boot appends {trial_state}"
        ),
        target_step("bkota status", f"{trial_mapping} mapping and trusted {trial_state}"),
    ]

    if case.operation == "pending-reset":
        return successful_publish + [
            power_cycle_step(
                "A boots because the one trial was consumed without confirmation"
            ),
            target_step("bkota status", "mapping A; trusted TRIAL_STARTED is reclaimable"),
        ]

    if case.operation == "rollback":
        return successful_publish + [
            target_step(
                f"bkota rollback {generation} 10000 {token}",
                "ret=0; trusted ROLLBACK_A appended",
            ),
            power_cycle_step("A boots from the durable ROLLBACK_A decision"),
            target_step("bkota status", "mapping A; trusted ROLLBACK_A"),
        ]

    if case.operation == "trial-fault":
        arm = f"bkota fault-arm {case.fault_point} {case.fault_ordinal} {generation} {token}"
        return successful_publish + [
            target_step(arm, "ret=0; generation-bound one-shot plan armed"),
            target_step(
                f"bkota rollback {generation} 10000 {token}",
                "ret=-ECANCELED and BKOTA FAULT RESULT triggered=1",
            ),
            power_cycle_step("A boots because the trial remains consumed"),
            target_step("bkota status", "mapping A; no false rollback/confirmation record"),
        ]

    if case.operation == "health-refusal":
        return successful_publish + [
            target_step("apctl inject primary", "fault injection is active"),
            target_step("apctl health", "supervisor is not fault-free"),
            target_step(
                f"bkota confirm {generation} 7000 {token}",
                "ret=-ETIMEDOUT; CONFIRMED_B is not appended",
            ),
            power_cycle_step(
                "A boots; AP injection and runtime write gates are reset"
            ),
            target_step("bkota status", "mapping A; trusted TRIAL_STARTED"),
        ]

    require(case.operation == "confirm", f"unsupported campaign operation {case.operation}")
    if case.target_slot == "a":
        return successful_publish + [
            target_step(
                f"bkota confirm {generation} 10000 {token}",
                "ret=0 after the 5000 ms stable AP-supervisor window; CONFIRMED_A appended",
            ),
            target_step("bkota status", "primary mapping; trusted CONFIRMED_A"),
            reset_step("A remains selected after a hardware reset"),
            target_step("bkota status", "primary mapping; trusted CONFIRMED_A"),
            power_cycle_step("A remains selected after complete power removal/restoration"),
            target_step("bkota status", "primary mapping; trusted CONFIRMED_A"),
            target_step(
                "RUN RETAINED N14 SERVICE MATRIX",
                "LittleFS/RPMsg/RPMsgFS/Bluetooth/PSRAM/timer/AP SMP all PASS",
            ),
        ]

    return successful_publish + [
        target_step(
            f"bkota confirm {generation} 10000 {token}",
            "ret=0 after the 5000 ms stable AP-supervisor window; CONFIRMED_B appended",
        ),
        target_step("bkota status", "secondary mapping; trusted CONFIRMED_B"),
        reset_step("B remains selected after a hardware reset"),
        target_step("bkota status", "secondary mapping; trusted CONFIRMED_B"),
        power_cycle_step("B remains selected after complete power removal/restoration"),
        target_step("bkota status", "secondary mapping; trusted CONFIRMED_B"),
        target_step(
            "RUN RETAINED N14 SERVICE MATRIX",
            "LittleFS/RPMsg/RPMsgFS/Bluetooth/PSRAM/timer/AP SMP all PASS",
        ),
    ]


def build_case(
    scripts: Path,
    baseline: Path,
    sdk_source: Path,
    output: Path,
    case: CampaignCase,
    generation: int,
    timestamp: int,
    base_version: str,
    base_pair: Path | None = None,
) -> dict[str, object]:
    version = f"n15v{generation}-{case.version_suffix}"
    require(len(version.encode("ascii")) <= 23, f"generated version is too long: {version}")
    token = f"N15-WRITE-{generation}"
    package = output / f"g{generation:06d}-{case.key}"
    package.mkdir()
    python = sys.executable

    run_checked([
        python, str(scripts / "pack_bk7258_ota_pair.py"),
        "--cp-raw", str(baseline / "app.bin"),
        "--ap-raw", str(baseline / "app1.bin"),
        "--output", str(package), "--generation", str(generation),
        "--version", version, "--base-version", base_version,
        "--timestamp", str(timestamp),
    ])
    run_checked([
        python, str(scripts / "verify_bk7258_ota_pair.py"),
        "--bundle", str(package), "--expected-generation", str(generation),
        "--expected-version", version, "--expected-base-version", base_version,
        "--expected-timestamp", str(timestamp), "--sdk-source", str(sdk_source),
    ])
    rotation_command = [
        python, str(scripts / "pack_bk7258_ota_rotation.py"),
        "--bundle", str(package),
        "--base-cp-crc", str(baseline / "app_crc.bin"),
        "--base-ap-crc", str(baseline / "app1_crc.bin"),
        "--target-slot", case.target_slot, "--bank", "0",
        "--output", str(package / "bk7258-ota-metadata.bin"),
        "--record-output", str(package / "bk7258-ota-pending-record.bin"),
        "--descriptor-output", str(package / "bk7258-ota-stage.bin"),
        "--generation", str(generation), "--version", version,
        "--base-version", base_version, "--timestamp", str(timestamp),
        "--sdk-source", str(sdk_source),
    ]
    if base_pair is not None:
        rotation_command.extend(("--base-pair", str(base_pair)))
    run_checked(rotation_command)

    boot_command = [
        python, str(scripts / "verify_bk7258_ota_boot.py"),
        "--bundle", str(package), "--cp-crc", str(baseline / "app_crc.bin"),
        "--ap-crc", str(baseline / "app1_crc.bin"),
        "--metadata", str(package / "bk7258-ota-metadata.bin"),
        "--expected-target-slot", case.target_slot,
        "--expected-generation", str(generation), "--expected-version", version,
        "--expected-base-version", base_version, "--expected-timestamp", str(timestamp),
        "--boot-elf", str(baseline / "bootloader.elf"),
        "--boot-bin", str(baseline / "bootloader.bin"),
        "--boot-crc", str(baseline / "bl_crc.bin"),
        "--expected-gate-value", "1",
        "--output", str(package / "bk7258-ota-boot-candidate.json"),
        "--sdk-source", str(sdk_source),
    ]
    if base_pair is not None:
        boot_command.extend(("--base-pair", str(base_pair)))
    run_checked(boot_command)
    run_checked([
        python, str(scripts / "verify_bk7258_ota_transfer.py"),
        "--package", str(package),
        "--expected-target-slot", case.target_slot,
        "--expected-bank", "0",
    ])

    pair = load_json(package / "bk7258-ota-pair.json")
    transfer = load_json(package / "bk7258-ota-transfer.json")
    require(pair.get("generation") == generation, "pair generation drift")
    require(pair.get("version") == version, "pair version drift")
    require(transfer.get("generation") == generation, "transfer generation drift")
    require(transfer.get("target_slot") == case.target_slot,
            "transfer target-slot drift")
    require(transfer.get("metadata_bank") == 0,
            "transfer metadata-bank drift")
    require(transfer.get("board_write_authorized") is False, "transfer authorizes board write")
    require(transfer.get("flash_write_performed") is False, "host packaging mutated Flash")

    board_inputs = {
        name: artifact(package / name)
        for name in (
            "s_app-candidate.bin", "bk7258-ota-stage.bin",
            "bk7258-ota-pending-record.bin",
        )
    }
    steps = workflow(case, package, generation, timestamp, version, base_version)
    return {
        "key": case.key, "purpose": case.purpose,
        "generation": generation, "version": version,
        "base_version": base_version, "timestamp": timestamp, "token": token,
        "target_slot": case.target_slot,
        "base_slot": "b" if case.target_slot == "a" else "a",
        "package": str(package),
        "fault": (
            {"point": case.fault_point, "ordinal": case.fault_ordinal}
            if case.fault_point is not None else None
        ),
        "terminal": case.terminal, "board_inputs": board_inputs,
        "controlled_power_cycle_required": any(
            step["command"] == "CONTROLLED POWER CYCLE" for step in steps
        ),
        "workflow": steps,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validation-package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sdk-source", type=Path, required=True)
    parser.add_argument("--start-generation", type=parse_int, default=42)
    parser.add_argument("--base-version", default="n15-m-board")
    parser.add_argument("--start-timestamp", type=parse_int, default=0)
    args = parser.parse_args()

    baseline = args.validation_package.resolve()
    output = args.output.resolve()
    sdk_source = args.sdk_source.resolve()
    scripts = Path(__file__).resolve().parent
    created = False
    try:
        require(not output.exists(), f"output already exists: {output}")
        require(args.start_generation > 0, "start generation must be positive")
        require(args.start_timestamp >= 0, "start timestamp must be non-negative")
        require(
            args.start_generation + len(CASES) - 1 <= MAX_U64,
            "campaign generation range exceeds uint64",
        )
        require(
            args.start_timestamp + len(CASES) - 1 <= MAX_U64,
            "campaign timestamp range exceeds uint64",
        )
        require(
            args.base_version and len(args.base_version.encode("ascii")) <= 23,
            "base version must be non-empty ASCII and at most 23 bytes",
        )
        baseline_report = validate_baseline(baseline, sdk_source)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.mkdir()
        created = True
        (output / ".incomplete").write_text(
            "N15-V campaign generation is incomplete.\n", encoding="ascii"
        )

        case_reports = []
        confirmed_b_pair: Path | None = None
        confirmed_b_version: str | None = None
        for index, case in enumerate(CASES):
            generation = args.start_generation + index
            timestamp = args.start_timestamp + index
            case_base_version = args.base_version
            base_pair: Path | None = None
            if case.target_slot == "a":
                require(
                    confirmed_b_pair is not None and confirmed_b_version is not None,
                    "target-A case requires the preceding confirmed-B package",
                )
                case_base_version = confirmed_b_version
                base_pair = confirmed_b_pair

            report = build_case(
                scripts, baseline, sdk_source, output, case,
                generation, timestamp, case_base_version, base_pair,
            )
            case_reports.append(report)
            if case.operation == "confirm" and case.target_slot == "b":
                confirmed_b_pair = (
                    Path(str(report["package"])) / "s_app-candidate.bin"
                )
                confirmed_b_version = str(report["version"])
            print(
                f"BK7258 N15-V campaign case PASS: order={index + 1}/{len(CASES)} "
                f"generation={generation} key={case.key}"
            )

        manifest = {
            "format": CAMPAIGN_FORMAT, "status": "pass",
            "sdk_release": SDK_RELEASE, "official_sdk_source": str(sdk_source),
            "validation_baseline": str(baseline),
            "validation_baseline_artifacts": baseline_report,
            "start_generation": args.start_generation,
            "end_generation": args.start_generation + len(CASES) - 1,
            "case_count": len(CASES), "ordered_execution_required": True,
            "terminal_case_last": case_reports[-1]["terminal"] is True,
            "board_write_authorized": False, "automatic_reset": False,
            "flash_write_performed": False,
            "physical_execution_performed": False,
            "controlled_power_cycle_required": True,
            "boundary_model": "fail-before callback, quiescent return, controlled power cycle",
            "mid_flash_pulse_brownout_tested": False,
            "cases": case_reports,
        }
        require(
            all(not case["terminal"] for case in case_reports[:-1]),
            "terminal campaign case appears before the final position",
        )
        (output / CAMPAIGN_FILE).write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (output / ".incomplete").unlink()
    except (OSError, UnicodeError, ValueError, subprocess.SubprocessError, CampaignError) as error:
        if created:
            shutil.rmtree(output)
        print(f"BK7258 N15-V campaign pack FAIL: {error}")
        return 1

    print(
        "BK7258 N15-V campaign pack PASS: "
        f"cases={len(CASES)} generations={args.start_generation}.."
        f"{args.start_generation + len(CASES) - 1} "
        "writes_enabled=false board_authorized=false"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
