#!/usr/bin/env python3
"""Independently verify an N15-V campaign and dry-run every PSRAM load.

The verifier never passes ``--execute`` to the loader and therefore never
opens J-Link or accesses a board.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


CAMPAIGN_FILE = "bk7258-n15v-campaign.json"
CAMPAIGN_FORMAT = 2
SDK_RELEASE = "v3.1.1.9"
POWER_CYCLE = "CONTROLLED POWER CYCLE"

EXPECTED_CASES: tuple[tuple[str, tuple[str, int] | None], ...] = (
    ("candidate-corrupt", None),
    ("stage-timeout", None),
    ("stage-erase-fault", ("stage-erase", 1)),
    ("stage-write-fault", ("stage-write", 2)),
    ("stage-readback-fault", ("stage-read", 17)),
    ("publish-preread-fault", ("publish-read", 1)),
    ("pending-reset-no-confirm", None),
    ("publish-write-fault", ("publish-write", 2)),
    ("publish-erase-fault", ("publish-erase", 1)),
    ("publish-readback-fault", ("publish-read", 4)),
    ("explicit-rollback", None),
    ("trial-read-fault", ("trial-read", 1)),
    ("trial-write-fault", ("trial-write", 1)),
    ("health-gate-refusal", None),
    ("successful-confirm", None),
    ("successful-return-a", None),
)

BOARD_INPUTS = (
    "s_app-candidate.bin",
    "bk7258-ota-stage.bin",
    "bk7258-ota-pending-record.bin",
)


class CampaignVerificationError(RuntimeError):
    """Raised when campaign provenance, contents or safety gates drift."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CampaignVerificationError(message)


def load_json(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(value, dict), f"{path} root must be an object")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_artifact(path: Path, entry: object, label: str) -> str:
    require(isinstance(entry, dict), f"{label} manifest entry is not an object")
    require(path.is_file(), f"{label} is missing: {path}")
    require(entry.get("file") == path.name, f"{label} filename drift")
    require(entry.get("size") == path.stat().st_size, f"{label} size drift")
    digest = sha256(path)
    require(entry.get("sha256") == digest, f"{label} SHA-256 drift")
    return digest


def run_checked(command: list[str], label: str) -> str:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=180,
    )
    if result.returncode != 0:
        raise CampaignVerificationError(
            f"{label} failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout + result.stderr


def verify_baseline(manifest: dict[str, object], sdk_source: Path) -> None:
    require(manifest.get("sdk_release") == SDK_RELEASE, "SDK release drift")
    recorded_sdk = Path(str(manifest.get("official_sdk_source", ""))).resolve()
    require(recorded_sdk == sdk_source, "official SDK source path drift")
    require(
        sdk_source.is_dir()
        and sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
        "SDK source is not the pinned official v3.1.1.9 tree",
    )

    baseline = Path(str(manifest.get("validation_baseline", ""))).resolve()
    require(baseline.is_dir(), "validation baseline is missing")
    entries = manifest.get("validation_baseline_artifacts")
    require(isinstance(entries, dict), "validation baseline artifacts are missing")
    names = {
        "bootloader_elf": "bootloader.elf",
        "cp_elf": "nuttx-cp.elf",
        "ap_elf": "nuttx-ap.elf",
        "cp_raw": "app.bin",
        "ap_raw": "app1.bin",
    }
    for key, name in names.items():
        verify_artifact(baseline / name, entries.get(key), f"baseline {key}")
    profile = baseline / "build-profile.txt"
    require(profile.is_file(), "validation build profile is missing")
    require(
        entries.get("build_profile_sha256") == sha256(profile),
        "validation build profile SHA-256 drift",
    )


def verify_workflow(
    case: dict[str, object], generation: int, package: Path, loader: Path
) -> None:
    steps = case.get("workflow")
    require(isinstance(steps, list) and steps, "campaign workflow is empty")
    commands: list[str] = []
    for index, step in enumerate(steps):
        require(isinstance(step, dict), f"workflow step {index} is not an object")
        require(
            step.get("actor") in ("host", "target", "operator"),
            f"workflow step {index} has an invalid actor",
        )
        command = step.get("command")
        require(isinstance(command, str) and command, f"workflow step {index} omits command")
        require(
            isinstance(step.get("expect"), str) and step.get("expect"),
            f"workflow step {index} omits expected result",
        )
        commands.append(command)

    require(POWER_CYCLE in commands, "workflow omits its controlled power cycle")
    require(
        case.get("controlled_power_cycle_required") is True,
        "case power-cycle gate is not true",
    )
    require(
        f"bkota prepare-transfer {generation} N15-WRITE-{generation}" in commands,
        "workflow prepare-transfer identity drift",
    )
    loader_steps = [command for command in commands if loader.name in command]
    require(len(loader_steps) == 2, "workflow must contain dry-run and execute loader steps")
    require("--execute" not in loader_steps[0], "first loader step is not a dry-run")
    require("--execute" in loader_steps[1], "second loader step omits explicit execution")
    for command in loader_steps:
        require(f"--generation {generation}" in command, "loader generation drift")
        require(f"--token N15-WRITE-{generation}" in command, "loader token drift")
        require(f"--package {package}" in command, "loader package path drift")
    joined = "\n".join(commands).lower()
    for forbidden in ("chip erase", "bluedebug", "0x7fa000", "0x800000"):
        require(forbidden not in joined, f"workflow contains forbidden token {forbidden}")


def verify_case(
    case: object,
    index: int,
    root: Path,
    sdk_source: Path,
    scripts: Path,
    loader: Path,
    expected_generation: int,
) -> tuple[str, str, str]:
    require(isinstance(case, dict), f"case {index} is not an object")
    expected_key, expected_fault = EXPECTED_CASES[index]
    expected_target_slot = "a" if expected_key == "successful-return-a" else "b"
    expected_base_slot = "b" if expected_target_slot == "a" else "a"
    require(case.get("key") == expected_key, f"case {index} order/key drift")
    generation = case.get("generation")
    require(generation == expected_generation, f"case {expected_key} generation drift")
    require(case.get("token") == f"N15-WRITE-{generation}", f"case {expected_key} token drift")
    require(
        case.get("terminal") == (index == len(EXPECTED_CASES) - 1),
        f"case {expected_key} terminal marker drift",
    )
    require(
        case.get("target_slot") == expected_target_slot,
        f"case {expected_key} target-slot drift",
    )
    require(
        case.get("base_slot") == expected_base_slot,
        f"case {expected_key} base-slot drift",
    )
    fault = case.get("fault")
    if expected_fault is None:
        require(fault is None, f"case {expected_key} has an unexpected fault")
    else:
        require(isinstance(fault, dict), f"case {expected_key} fault is missing")
        require(
            (fault.get("point"), fault.get("ordinal")) == expected_fault,
            f"case {expected_key} fault point/ordinal drift",
        )

    package = Path(str(case.get("package", ""))).resolve()
    expected_name = f"g{generation:06d}-{expected_key}"
    require(package.parent == root, f"case {expected_key} escapes campaign root")
    require(package.name == expected_name, f"case {expected_key} package name drift")
    require(package.is_dir() and not package.is_symlink(), f"case {expected_key} package is unsafe")

    inputs = case.get("board_inputs")
    require(isinstance(inputs, dict), f"case {expected_key} board inputs are missing")
    digests = tuple(
        verify_artifact(package / name, inputs.get(name), f"{expected_key} {name}")
        for name in BOARD_INPUTS
    )

    pair = load_json(package / "bk7258-ota-pair.json")
    transfer = load_json(package / "bk7258-ota-transfer.json")
    metadata = load_json(package / "bk7258-ota-metadata.json")
    for report, label in ((pair, "pair"), (transfer, "transfer")):
        require(report.get("generation") == generation, f"{expected_key} {label} generation drift")
    require(pair.get("version") == case.get("version"), f"{expected_key} version drift")
    require(pair.get("base_version") == case.get("base_version"), f"{expected_key} base version drift")
    require(pair.get("timestamp") == case.get("timestamp"), f"{expected_key} timestamp drift")
    require(transfer.get("board_write_authorized") is False, "transfer authorizes board write")
    require(transfer.get("flash_write_performed") is False, "transfer claims a Flash write")
    require(
        transfer.get("target_slot") == expected_target_slot,
        f"{expected_key} transfer target-slot drift",
    )
    require(
        transfer.get("metadata_bank") == 0,
        f"{expected_key} transfer carrier-bank drift",
    )
    require(
        metadata.get("target_slot") == expected_target_slot,
        f"{expected_key} metadata target-slot drift",
    )
    require(
        metadata.get("base_slot") == expected_base_slot,
        f"{expected_key} metadata base-slot drift",
    )
    require(metadata.get("bank") == 0, f"{expected_key} metadata bank drift")
    require(
        metadata.get("base_pair_representation")
        == ("crc-container" if expected_target_slot == "a" else "factory-split-pair"),
        f"{expected_key} base-pair representation drift",
    )

    verify_workflow(case, generation, package, loader)

    run_checked(
        [
            sys.executable,
            str(scripts / "verify_bk7258_ota_pair.py"),
            "--bundle", str(package),
            "--expected-generation", str(generation),
            "--expected-version", str(case.get("version")),
            "--expected-base-version", str(case.get("base_version")),
            "--expected-timestamp", str(case.get("timestamp")),
            "--sdk-source", str(sdk_source),
        ],
        f"{expected_key} pair verifier",
    )
    run_checked(
        [
            sys.executable,
            str(scripts / "verify_bk7258_ota_transfer.py"),
            "--package", str(package),
            "--expected-target-slot", expected_target_slot,
            "--expected-bank", "0",
            "--check-only",
        ],
        f"{expected_key} transfer verifier",
    )
    loader_output = run_checked(
        [
            str(loader),
            "--generation", str(generation),
            "--token", f"N15-WRITE-{generation}",
            "--package", str(package),
        ],
        f"{expected_key} loader dry-run",
    )
    require("writes_enabled=false" in loader_output, "loader dry-run gate is absent")
    require("board_authorized=false" in loader_output, "loader board authorization gate is absent")
    return digests


def verify_campaign(
    root: Path,
    sdk_source: Path,
    loader: Path,
    expected_start_generation: int,
) -> dict[str, object]:
    require(root.is_dir() and not root.is_symlink(), "campaign path is not a safe directory")
    require(loader.is_file(), f"loader is missing: {loader}")
    manifest_path = root / CAMPAIGN_FILE
    manifest = load_json(manifest_path)
    require(manifest.get("format") == CAMPAIGN_FORMAT, "campaign format drift")
    require(manifest.get("status") == "pass", "campaign status is not PASS")
    require(manifest.get("board_write_authorized") is False, "campaign authorizes board writes")
    require(manifest.get("automatic_reset") is False, "campaign enables automatic reset")
    require(manifest.get("flash_write_performed") is False, "campaign claims a Flash write")
    require(manifest.get("physical_execution_performed") is False, "campaign claims board execution")
    require(manifest.get("controlled_power_cycle_required") is True, "power-cycle gate is absent")
    require(manifest.get("mid_flash_pulse_brownout_tested") is False, "campaign overclaims brownout")
    require(
        manifest.get("boundary_model")
        == "fail-before callback, quiescent return, controlled power cycle",
        "campaign boundary model drift",
    )
    require(manifest.get("ordered_execution_required") is True, "campaign ordering is disabled")
    require(manifest.get("terminal_case_last") is True, "terminal-last gate is false")
    require(manifest.get("start_generation") == expected_start_generation, "start generation drift")
    require(
        manifest.get("end_generation") == expected_start_generation + len(EXPECTED_CASES) - 1,
        "end generation drift",
    )
    require(manifest.get("case_count") == len(EXPECTED_CASES), "case count drift")
    require(not (root / ".incomplete").exists(), "campaign has an incomplete marker")
    verify_baseline(manifest, sdk_source)

    cases = manifest.get("cases")
    require(isinstance(cases, list) and len(cases) == len(EXPECTED_CASES), "case list drift")
    final_case = cases[-1]
    confirmed_b_case = cases[-2]
    require(isinstance(final_case, dict), "final return-to-A case is not an object")
    require(isinstance(confirmed_b_case, dict), "confirmed-B case is not an object")
    require(
        final_case.get("base_version") == confirmed_b_case.get("version"),
        "return-to-A base version is not the preceding confirmed-B version",
    )
    scripts = Path(__file__).resolve().parent
    identity_hashes: list[tuple[str, str, str]] = []
    for index, case in enumerate(cases):
        identity_hashes.append(
            verify_case(
                case,
                index,
                root,
                sdk_source,
                scripts,
                loader,
                expected_start_generation + index,
            )
        )
        print(
            "BK7258 N15-V campaign verify case PASS: "
            f"order={index + 1}/{len(cases)} generation={expected_start_generation + index}"
        )

    confirmed_b_package = Path(str(confirmed_b_case.get("package", ""))).resolve()
    final_package = Path(str(final_case.get("package", ""))).resolve()
    final_metadata = load_json(final_package / "bk7258-ota-metadata.json")
    require(
        final_metadata.get("base_pair_sha256")
        == sha256(confirmed_b_package / "s_app-candidate.bin"),
        "return-to-A metadata does not pin the preceding confirmed-B image",
    )

    for position, label in enumerate(BOARD_INPUTS):
        require(
            len({digests[position] for digests in identity_hashes}) == len(cases),
            f"campaign {label} identities are not unique",
        )

    return {
        "format": 2,
        "status": "pass",
        "campaign_format": CAMPAIGN_FORMAT,
        "campaign_manifest_sha256": sha256(manifest_path),
        "sdk_release": SDK_RELEASE,
        "case_count": len(cases),
        "start_generation": expected_start_generation,
        "end_generation": expected_start_generation + len(cases) - 1,
        "unique_candidate_identities": len(cases),
        "unique_descriptor_identities": len(cases),
        "unique_metadata_identities": len(cases),
        "loader_dry_runs": len(cases),
        "controlled_power_cycle_required": True,
        "mid_flash_pulse_brownout_tested": False,
        "board_write_authorized": False,
        "physical_execution_performed": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--campaign", type=Path, required=True)
    parser.add_argument("--sdk-source", type=Path, required=True)
    parser.add_argument("--loader", type=Path)
    parser.add_argument("--expected-start-generation", type=int, default=42)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    scripts = Path(__file__).resolve().parent
    loader = (args.loader or scripts / "load_bk7258_ota_psram.sh").resolve()
    try:
        result = verify_campaign(
            args.campaign.resolve(),
            args.sdk_source.resolve(),
            loader,
            args.expected_start_generation,
        )
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
    except (
        CampaignVerificationError,
        OSError,
        UnicodeError,
        ValueError,
        subprocess.SubprocessError,
    ) as error:
        print(f"BK7258 N15-V campaign verification FAIL: {error}")
        return 1

    print(
        "BK7258 N15-V campaign verification PASS: "
        f"cases={result['case_count']} loaders={result['loader_dry_runs']} "
        "board_authorized=false physical_execution=false"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
