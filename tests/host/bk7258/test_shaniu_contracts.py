#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Thin unittest/Make/JUnit adapter for the v2 contract baseline.

Consumer: make run-shaniu-contracts. It preserves failures, reports every
collected executable case, and never manufactures a PASS for missing bindings.
All mutable production experiments live in TemporaryDirectory, never the repo.
"""
import hashlib
from collections import Counter
import os
import json
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = Path(
    os.environ.get(
        "SHANIU_CONTRACT_OUT",
        str(ROOT / "out" / ("shaniu-contract-" + time.strftime("%Y%m%d-%H%M%S"))),
    )
)
BASE = "272b3b2f366cf9ac9ae510757ac4288f0c68d3a0"
GOLDEN = ROOT / "android/shaniu-companion/app/src/test/resources/shaniu/scp1-wifi.hex"
RESULTS = []
BUILDS = []
SELECTION = json.loads((HERE / "acceptance/required-units.v1.json").read_text())
REQUIRED = SELECTION["ids"]


def collection_errors(results, required):
    """Validate only the selected executable contract, not future specifications."""
    counts = Counter(r["id"] for r in results)
    errors = []
    if not required or len(set(required)) != len(required):
        errors.append("empty or duplicate required execution set")
    errors += ["missing: " + item for item in required if counts[item] == 0]
    errors += ["duplicate: " + item for item, n in counts.items() if n > 1]
    errors += ["unexpected: " + item for item in counts if item not in required]
    errors += [
        "not PASS: " + r["id"] + ": " + r["status"]
        for r in results
        if r["status"] != "PASS"
    ]
    return errors


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(args, log, cwd=HERE, timeout=180):
    start = time.monotonic()
    with (OUT / log).open("w") as stream:
        stream.write(json.dumps([str(a) for a in args]) + "\n")
        stream.flush()
        try:
            result = subprocess.run(
                [str(a) for a in args],
                cwd=cwd,
                stdout=stream,
                stderr=subprocess.STDOUT,
                timeout=timeout,
            )
            code = result.returncode
        except (OSError, subprocess.TimeoutExpired) as error:
            stream.write(type(error).__name__ + ": " + str(error) + "\n")
            code = 124
    return code, time.monotonic() - start


def build(args, label):
    log = label + ".log"
    code, duration = command(args, log)
    BUILDS.append(dict(id=label, exit_code=code, seconds=duration, evidence=log))
    return code == 0


def case(case_id, parent, layer, args, ready=True, marker=True, setup_exit_code=None):
    log = case_id.replace("/", "_") + ".log"
    if not ready:
        RESULTS.append(
            dict(
                id=case_id,
                parent=parent,
                layer=layer,
                status="SETUP_ERROR",
                seconds=0,
                evidence="build logs",
            )
        )
        raise RuntimeError("Required build failed: " + case_id)
    code, seconds = command(args, log, timeout=45)
    output = (OUT / log).read_text(errors="replace")
    status = (
        "PASS"
        if code == 0 and (not marker or "CONTRACT_PASS" in output)
        else (
            "FAIL_ASSERTION"
            if "Assertion" in output or "assertion" in output
            else "SETUP_ERROR"
        )
    )
    if setup_exit_code is not None and code == setup_exit_code:
        status = "SETUP_ERROR"
    RESULTS.append(
        dict(
            id=case_id,
            parent=parent,
            layer=layer,
            status=status,
            exit_code=code,
            seconds=seconds,
            evidence=log,
            evidence_sha256=digest(OUT / log),
        )
    )
    if status == "FAIL_ASSERTION":
        raise AssertionError(case_id + ": see " + log)
    if status != "PASS":
        raise RuntimeError(case_id + ": see " + log)


def add(suite, case_id, parent, layer, args, ready=True, marker=True, setup_exit_code=None):
    def run():
        case(case_id, parent, layer, args, ready, marker, setup_exit_code)

    suite.addTest(unittest.FunctionTestCase(run, description=case_id))


def git(*args, cwd=ROOT):
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


def production_digest():
    paths = git(
        "ls-files",
        "app",
        "chips",
        "boards",
        "android/shaniu-companion/app/src/main",
        "contest2026_135_yongwangzhiqian.xml",
    ).splitlines()
    h = hashlib.sha256()
    for path in paths:
        h.update(path.encode() + b"\0" + (ROOT / path).read_bytes())
    return h.hexdigest()


def config_build(temp, config_source):
    # Reuse the existing HTTP test build and its NuttX/webclient boundary shims.
    sources = [config_source] + [
        ROOT / "app/bk7258" / ("bk7258_" + name + ".c")
        for name in (
            "provision_settings",
            "provision_store",
            "provision_storage",
            "pc_grants",
            "voice_config",
        )
    ]
    snippet = (
        "from pathlib import Path; from test_bk7258_cloud_http import build_http_fixture; "
        "import sys; build_http_fixture(Path(sys.argv[1]), Path(sys.argv[2]), "
        "[Path(p) for p in sys.argv[3:]], ['-Wl,--wrap=fsync', '-Wl,--wrap=rename'])"
    )
    return [
        sys.executable,
        "-c",
        snippet,
        temp,
        HERE / "test_shaniu_config_contract.c",
        *sources,
    ]


def mutations(temp, config_temp, cert):
    reports = []
    specifications = [
        (
            "MSC-01.early-release",
            ROOT / "app/bk7258/bk7258_media_volume.c",
            'syslog(LOG_WARNING, "BKOTA mount cleanup retained ret=%d\\n", ret);\n          return ret;',
            'syslog(LOG_WARNING, "BKOTA mount cleanup retained ret=%d\\n", ret);\n          (void)bk7258_media_volume_release(BK7258_MEDIA_VOLUME_OTA);\n          return ret;',
            "!mounted",
        ),
        (
            "CFG-01.wifi-clears-cloud",
            ROOT / "app/bk7258/bk7258_provision_config.c",
            "if (flags & PATCH_CLEAR_CLOUD)",
            "if (flags & (PATCH_CLEAR_CLOUD | PATCH_WIFI))",
            "bkcloud_config_decode",
        ),
    ]
    for ident, source, old, new, assertion in specifications:
        folder = temp / ident
        folder.mkdir()
        text = source.read_text()
        if text.count(old) != 1:
            reports.append(
                dict(id=ident, status="SETUP_ERROR", reason="mutation anchor drift")
            )
            continue
        mutant = folder / source.name
        mutant.write_text(text.replace(old, new))
        if ident.startswith("MSC"):
            shutil.copyfile(
                ROOT / "app/bk7258/bk7258_media_volume.h",
                folder / "bk7258_media_volume.h",
            )
            binary = folder / "build/test_shaniu_volume_contract"
            ready = build(
                [
                    "make",
                    str(binary),
                    "BUILD=" + str(folder / "build"),
                    "VOICE_PACK_ROOT=" + str(folder),
                ],
                ident + "-build",
            )
            args = [binary, "unmount-failure"]
        else:
            ready = build(config_build(config_temp, mutant), ident + "-build")
            args = [
                config_temp / "test",
                "wifi-reopen",
                folder / "private",
                cert,
                GOLDEN,
            ]
        code, seconds = command(args, ident + ".log") if ready else (None, 0)
        output = (OUT / (ident + ".log")).read_text(errors="replace") if ready else ""
        status = (
            "DETECTED"
            if code == -6 and assertion in output
            else ("SURVIVED" if code == 0 else "SETUP_ERROR")
        )
        reports.append(
            dict(
                id=ident,
                status=status,
                exit_code=code,
                seconds=seconds,
                source=str(source.relative_to(ROOT)),
                original_sha256=digest(source),
                mutant_sha256=digest(mutant),
                expected_assertion=assertion,
                evidence=ident + ".log" if ready else ident + "-build.log",
            )
        )
    return reports


def run_jvm():
    classes = [
        "provision.DeviceControlSessionTest",
        "provision.ProvisionTlsTest",
        "provision.ProvisionSettingsTest",
        "provision.FocusTimerControllerTest",
        "provision.SceneControlProtocolTest",
        "provision.PcAuthorizationControllerTest",
        "provision.PcPairingExchangeTest",
        "provision.NfcBindingControllerTest",
        "provision.ExpressionTrialControllerTest",
        "ota.OtaControlUploadTest",
        "ota.OtaSessionContractTest",
    ]
    app = ROOT / "android/shaniu-companion"
    args = ["./gradlew", ":app:testDebugUnitTest", "--offline", "--rerun-tasks"]
    for name in classes:
        args += ["--tests", "com.shaniu.companion." + name]
    start = time.time()
    code, seconds = command(args, "jvm.log", cwd=app, timeout=180)
    collected = 0
    first_result = len(RESULTS)
    for name in classes:
        xml = (
            app
            / "app/build/test-results/testDebugUnitTest"
            / ("TEST-com.shaniu.companion." + name + ".xml")
        )
        if not xml.exists() or xml.stat().st_mtime < start:
            RESULTS.append(
                dict(
                    id=name,
                    parent="OTA-01" if name.startswith("ota") else "CFG-03",
                    layer="L2",
                    status="SETUP_ERROR",
                    seconds=0,
                    evidence="jvm.log",
                )
            )
            continue
        shutil.copyfile(xml, OUT / xml.name)
        try:
            document = ET.parse(xml).getroot()
            nodes = document.findall("testcase")
            if document.tag != "testsuite" or not nodes:
                raise ValueError("empty or invalid test suite")
            if int(document.get("tests", len(nodes))) != len(nodes):
                raise ValueError("inconsistent test count")
            for node in nodes:
                if (
                    not node.get("name")
                    or node.get("classname") != "com.shaniu.companion." + name
                ):
                    raise ValueError("missing method or wrong class identity")
                duration = float(node.get("time", 0))
                if not 0 <= duration < float("inf"):
                    raise ValueError("invalid duration")
        except (ET.ParseError, ValueError, OSError) as error:
            RESULTS.append(
                dict(
                    id=name,
                    parent="OTA-01" if name.startswith("ota") else "CFG-03",
                    layer="L2",
                    status="SETUP_ERROR",
                    seconds=0,
                    evidence=xml.name,
                    reason=str(error),
                )
            )
            continue
        for node in nodes:
            failure = node.find("failure")
            status = "PASS"
            if failure is not None:
                status = (
                    "FAIL_ASSERTION"
                    if "Assertion" in failure.get("type", "")
                    or "ComparisonFailure" in failure.get("type", "")
                    else "SETUP_ERROR"
                )
            if node.find("skipped") is not None:
                status = "NOT_RUN"
            if node.find("error") is not None:
                status = "SETUP_ERROR"
            session_parents = {
                "tabsSubscribeTwentyTimesWithoutOpeningAnotherConnection": "UI-02",
                "writeWaitsBehindReadAndRequiresQuantizedReadback": "NET-03",
                "temporaryReadFailurePreservesConnectionAndUnconfirmedWrite": "NET-01",
                "ordinaryErrorIsNotDisconnectionAndDoesNotReplaceValue": "UI-01",
                "infoAndOtaDoNotReplaceStatusAndFragmentsStayContiguous": "OTA-01",
                "failedReconnectKeepsOneCappedForegroundRetry": "NET-01",
                "otherActivityGraceAndForegroundReturnHaveDifferentPolicies": "UI-02",
                "claimHandoffDropsOldIdentityRetryAndCachedStatus": "UI-03",
                "graceDisconnectCannotReopenAnExplicitlyClosedSession": "UI-02",
                "configCancelWaitsForInFlightAckAndPreventsOtherWriters": "NET-03",
                "failedConfigAckDoesNotReleaseStagingUntilExplicitCancel": "NET-03",
                "identityReleaseRejectsLateConfigResultAndDoesNotReplayIt": "UI-03",
                "peerCertificateCannotAuthenticateSessionOrAppearBeforeStatus": "USB-01",
                "disconnectAndOldIdentityCannotContaminateNewGeneration": "UI-03",
                "delayedCurrentIdentityPublishesWithoutAdditionalCommands": "NET-02",
                "peerIdentityRejectsMalformedAndOversizedCertificates": "USB-01",
            }
            parent = (
                "TIMER-01"
                if "FocusTimer" in name
                else (
                    "OTA-01"
                    if name.startswith("ota")
                    else (
                        "CFG-03"
                        if "Settings" in name
                        else session_parents.get(node.attrib["name"], "NET-02")
                    )
                )
            )
            if "PcAuthorization" in name or "SceneControl" in name:
                parent = "NET-03"
            if "NfcBinding" in name:
                parent = "NFC-02"
            if "ExpressionTrial" in name:
                parent = "RES-02"
            if name in ("provision.ProvisionTlsTest", "provision.PcPairingExchangeTest"):
                parent = "USB-01"
            RESULTS.append(
                dict(
                    id=name + "." + node.attrib["name"],
                    parent=parent,
                    layer=(
                        "L2"
                        if any(
                            part in name
                            for part in (
                                "Session",
                                "FocusTimer",
                                "ExpressionTrial",
                                "NfcBinding",
                                "PcAuthorization",
                            )
                        )
                        else "L1"
                    ),
                    status=status,
                    seconds=float(node.get("time", 0)),
                    evidence=xml.name,
                    evidence_sha256=digest(OUT / xml.name),
                )
            )
            collected += 1
    BUILDS.append(
        dict(
            id="JVM",
            exit_code=code,
            seconds=seconds,
            collected=collected,
            evidence="jvm.log",
        )
    )
    selected = [
        item
        for item in REQUIRED
        if any(item.startswith(name + ".") for name in classes)
    ]
    return code or (1 if collection_errors(RESULTS[first_result:], selected) else 0)


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    OUT.mkdir(parents=True, exist_ok=True)
    before = production_digest()
    suite = unittest.TestSuite()
    binaries = {}
    for target in (
        "test_control_serial",
        "test_pc_grants",
        "test_pc_tasks",
        "test_pc_reset",
        "test_pc_storage",
        "test_pc_authorization",
        "test_pc_owner_binding",
        "test_shaniu_key_contract",
        "test_shaniu_volume_contract",
        "test_shaniu_volume_transition",
        "test_bk7258_agent_capture",
        "test_shaniu_power_contract",
        "test_shaniu_motion_quiesce",
        "test_shaniu_motion_actions",
        "test_shaniu_power_pixels",
        "test_shaniu_msc_stop",
        "test_shaniu_usb_cleanup",
        "test_shaniu_control_quiesce",
        "test_shaniu_owner",
        "test_shaniu_power_owner",
        "test_bk7258_agent_media_player",
        "test_shaniu_focus",
        "test_shaniu_focus_shared",
        "test_shaniu_focus_intent",
        "test_shaniu_nfc_bindings",
        "test_shaniu_nfc_jobs",
        "test_shaniu_nfc_worker_scene",
        "test_shaniu_nfc_scene",
        "test_shaniu_nfc_control",
        "test_shaniu_nfc_quiesce",
        "test_shaniu_focus_pixels",
        "test_shaniu_focus_render",
        "test_display_upload",
        "test_display_selection",
        "test_bk7258_display_pack",
        "test_display_job",
        "test_display_job_service",
        "test_display_job_control",
        "test_shaniu_display_snapshot",
        "test_shaniu_display_intent",
        "test_shaniu_expression_cancel",
        "test_shaniu_expression_ownership",
        "test_shaniu_expression_trial",
        "test_shaniu_trial_wire",
        "test_shaniu_models_durability",
        "test_shaniu_focus_wire",
        "test_agent_tts_queue",
        "test_bk7258_product_keys",
        "test_bk7258_usbmode_lease",
        "test_bk7258_motion_core",
        "test_bk7258_nfc_core",
        "test_bk7258_nfc_rpc",
    ):
        binaries[target] = build(["make", "build/" + target], "build-" + target)
    for variant in (
        "release-2999",
        "release-3000",
        "release-3001",
        "held",
        "epoch",
        "rollback",
        "release-rollback",
        "combination",
        "volume",
    ):
        parent = (
            "K2-03"
            if variant
            in ("epoch", "rollback", "release-rollback", "combination", "volume")
            else "K2-01"
        )
        add(
            suite,
            parent + "." + variant,
            parent,
            "L1",
            [HERE / "build/test_shaniu_key_contract", variant],
            binaries["test_shaniu_key_contract"],
        )
    for variant in ("collision", "cancel", "corrupt", "fragmented", "normal",
                    "preserve", "write-failure", "sync-failure", "close-failure",
                    "directory-failure"):
        add(suite, "RES-01.upload-" + variant, "RES-01", "L2",
            [HERE / "build/test_display_upload",
             HERE / "build/shaniu-default-v1.bkep", variant,
             HERE / "build/upload-second.bkep"], binaries["test_display_upload"])
    for variant in ("activate-collision", "activate-directory-failure"):
        add(suite, "RES-02." + variant, "RES-02", "L2",
            [HERE / "build/test_display_upload",
             HERE / "build/shaniu-default-v1.bkep", variant,
             HERE / "build/upload-second.bkep"], binaries["test_display_upload"])
    for variant in ("queued-cancel", "expiry", "rollback", "blocked-cancel", "gate",
                    "release-failure", "cleanup-failure", "commit-cancel", "commit-busy", "success"):
        add(suite, "RES-01.job-" + variant, "RES-01", "L2",
            [HERE / "build/test_display_job", HERE / "build/shaniu-default-v1.bkep", variant],
            binaries["test_display_job"])
    for variant in ("normal", "start-failure", "mount-failure", "unmount-failure", "volume-conflict"):
        add(suite, "RES-01.native-job-" + variant, "RES-01", "L2",
            [HERE / "build/test_display_job_service", variant], binaries["test_display_job_service"])
    add(suite, "RES-01.native-job-success", "RES-01", "L2",
        [HERE / "build/test_display_job_service", "success", HERE / "build/shaniu-default-v1.bkep"],
        binaries["test_display_job_service"])
    for variant in ("malformed", "retry", "snapshot", "authority", "success", "session"):
        add(suite, "RES-03.job-wire-" + variant, "RES-03", "L2",
            [HERE / "build/test_display_job_control", variant, HERE / "build/shaniu-default-v1.bkep"],
            binaries["test_display_job_control"])
    for variant in ("session", "revoke"):
        add(suite, "RES-03.product-job-route-" + variant, "RES-03", "L2",
            [sys.executable, HERE / "test_pack_product_route.py", variant])
    add(
        suite,
        "DISP-01.power-pixels",
        "DISP-01",
        "L1",
        [HERE / "build/test_shaniu_power_pixels"],
        binaries["test_shaniu_power_pixels"],
    )
    add(
        suite,
        "MSC-01.failed-start-handoff",
        "MSC-01",
        "L1",
        [HERE / "build/test_shaniu_usb_cleanup"],
        binaries["test_shaniu_usb_cleanup"],
    )
    for variant in ("pixels", "render"):
        add(
            suite,
            "TIMER-01." + variant,
            "TIMER-01",
            "L1",
            [HERE / ("build/test_shaniu_focus_" + variant)],
            binaries["test_shaniu_focus_" + variant],
        )
    add(
        suite,
        "NFC-01.rf-switch",
        "NFC-01",
        "L1",
        [sys.executable, HERE / "test_mfrc522_rf.py", "RfDriverTest.test_switch"],
        marker=False,
    )
    add(
        suite,
        "NFC-01.rf-stuck",
        "NFC-01",
        "L1",
        [sys.executable, HERE / "test_mfrc522_rf.py", "RfDriverTest.test_stuck"],
        marker=False,
    )
    add(
        suite,
        "NFC-01.rf-registration",
        "NFC-01",
        "L1",
        [sys.executable, HERE / "test_mfrc522_rf.py", "RfDriverTest.test_registration"],
        marker=False,
    )
    add(
        suite,
        "NFC-01.rf-registration-failure",
        "NFC-01",
        "L1",
        [
            sys.executable,
            HERE / "test_mfrc522_rf.py",
            "RfDriverTest.test_registration_failure",
        ],
        marker=False,
    )
    add(
        suite,
        "NFC-01.rf-close",
        "NFC-01",
        "L1",
        [
            sys.executable,
            HERE / "test_nfc_rf_lifecycle.py",
            "RfLifecycleTest.test_idle_and_close_release_field_and_descriptor",
        ],
        marker=False,
    )
    add(
        suite,
        "NFC-01.rf-open-cleanup",
        "NFC-01",
        "L1",
        [
            sys.executable,
            HERE / "test_nfc_rf_lifecycle.py",
            "RfLifecycleTest.test_controlled_open_failure_releases_partial_field",
        ],
        marker=False,
    )
    for variant in (
        "busy",
        "failed",
        "owner_failure_still_closes_admission",
        "resume_failure",
        "power_intent_and_no_reset",
    ):
        add(
            suite,
            "RST-02.nfc-" + variant,
            "RST-02",
            "L1",
            [
                sys.executable,
                HERE / "test_shaniu_reset_nfc.py",
                "ResetNfcTest.test_" + variant,
            ],
            marker=False,
        )
    for variant in ("filesystem", "cleanup"):
        add(
            suite,
            "RST-01.nfc-" + variant,
            "RST-01",
            "L1",
            [
                sys.executable,
                HERE / "test_shaniu_nfc_reset_path.py",
                "NfcResetPathTest.test_" + variant,
            ],
            marker=False,
        )
    for variant in ("reset", "reset-sync", "reset-path", "reset-absent"):
        add(
            suite,
            "RST-01.bindings-" + variant,
            "RST-01",
            "L2",
            [HERE / "build/test_shaniu_nfc_bindings", variant],
            binaries["test_shaniu_nfc_bindings"],
        )
    for variant in (
        "persist",
        "cancel",
        "stop",
        "pending",
        "failure",
        "reset",
        "commit",
        "unknown",
        "remove",
        "conflict",
    ):
        add(
            suite,
            "NFC-02.jobs-" + variant,
            "NFC-02",
            "L2",
            [HERE / "build/test_shaniu_nfc_jobs", variant],
            binaries["test_shaniu_nfc_jobs"],
        )
    for variant in (
        "auth",
        "invalid",
        "cancel",
        "disconnect",
        "quiesce",
        "floor",
        "sequence",
        "staging",
        "capabilities",
        "cap-auth",
    ):
        add(
            suite,
            "NFC-02.control-" + variant,
            "NFC-02",
            "L2",
            [HERE / "build/test_shaniu_nfc_control", variant],
            binaries["test_shaniu_nfc_control"],
        )
    for variant in (
        "probe_error",
        "timeout",
        "malformed",
        "select_error",
        "invalid",
        "valid",
    ):
        add(
            suite,
            "NFC-01.selection-" + variant,
            "NFC-01",
            "L1",
            [
                sys.executable,
                HERE / "test_mfrc522_selection.py",
                "SelectionTest.test_" + variant,
            ],
            marker=False,
        )
    for variant in (
        "valid",
        "error",
        "close",
        "version",
        "replay",
        "validation",
        "malformed",
    ):
        add(
            suite,
            "NFC-01.card-" + variant,
            "NFC-01",
            "L2",
            [HERE / "build/test_bk7258_nfc_rpc", "card-" + variant],
            binaries["test_bk7258_nfc_rpc"],
        )
    for variant in (
        "persist",
        "revision",
        "invalid",
        "writefail",
        "durability",
        "corrupt",
    ):
        add(
            suite,
            "NFC-02.bindings-" + variant,
            "NFC-02",
            "L2",
            [HERE / "build/test_shaniu_nfc_bindings", variant],
            binaries["test_shaniu_nfc_bindings"],
        )
    for variant in ("queued", "active", "close-error", "rf-error", "prestart", "late"):
        add(
            suite,
            "LIFE-02.nfc-" + variant,
            "LIFE-02",
            "L2",
            [HERE / "build/test_shaniu_nfc_quiesce", variant],
            binaries["test_shaniu_nfc_quiesce"],
        )
    for variant in ("busy", "failed", "other_failure", "resume_failure", "resume_rollback",
                    "owner_resume_failure", "power_intent"):
        add(suite, "RST-02.motion-" + variant, "RST-02", "L1",
            [sys.executable, HERE / "test_shaniu_reset_nfc.py",
             "ResetMotionTest.test_motion_" + variant], marker=False)
    for variant in ("move-settle", "tilt", "freshness", "gate", "error", "cooldown",
                    "creep", "extreme", "gate-cooldown"):
        add(suite, "MOT-02.candidate-" + variant, "MOT-02", "L1",
            [HERE / "build/test_shaniu_motion_actions", variant],
            binaries["test_shaniu_motion_actions"])
    for variant in (
        "prestart", "queued", "active", "idle", "close-error", "open-cleanup",
        "late", "queued-cycle", "waiter-cycle", "publication-cycle",
    ):
        add(suite, "MOT-01.quiesce-" + variant, "MOT-01", "L2",
            [HERE / "build/test_shaniu_motion_quiesce", variant],
            binaries["test_shaniu_motion_quiesce"])
    for variant in ("motion-busy", "motion-failed"):
        add(suite, "LIFE-01." + variant, "LIFE-01", "L1",
            [HERE / "build/test_shaniu_power_contract", variant],
            binaries["test_shaniu_power_contract"])
    for variant in ("usb-failed", "usb-close", "pack-busy", "pack-failed"):
        add(suite, "LIFE-02.power-" + variant, "LIFE-02", "L1",
            [HERE / "build/test_shaniu_power_contract", variant],
            binaries["test_shaniu_power_contract"])
    for variant in ("usb_failed", "usb_before_identity", "pack_busy", "pack_failed"):
        add(suite, "RST-02." + variant, "RST-02", "L1",
            [sys.executable, HERE / "test_shaniu_reset_nfc.py",
             "ResetNfcTest.test_" + variant], marker=False)
    for variant in ("nfc-busy", "nfc-failed"):
        add(
            suite,
            "LIFE-02.power-" + variant,
            "LIFE-02",
            "L1",
            [HERE / "build/test_shaniu_power_contract", variant],
            binaries["test_shaniu_power_contract"],
        )
    for variant in ("dwell", "unknown", "enroll", "pending-cancel", "revoke",
                    "stop", "release", "error", "load-revoke", "first-enroll", "capability", "capability-inflight"):
        add(suite, "NFC-02.worker-" + variant, "NFC-02", "L2",
            [HERE / "build/test_shaniu_nfc_worker_scene", variant],
            binaries["test_shaniu_nfc_worker_scene"])
    for variant in ("dwell", "unknown", "stale", "gate", "busy", "binding", "invalid"):
        add(suite, "NFC-02.scene-" + variant, "NFC-02", "L2",
            [HERE / "build/test_shaniu_nfc_scene", variant],
            binaries["test_shaniu_nfc_scene"])
    for variant in ("off", "quiet", "watchdog", "timer_error", "partial",
                    "selection_timeout", "invalid_uid", "collision", "present",
                    "residual_error", "null"):
        add(suite, "NFC-01.observe-" + variant, "NFC-01", "L1",
            [sys.executable, HERE / "test_mfrc522_observation.py",
             "ObservationTest.test_" + variant], marker=False)
    for variant in ("crc_deadline", "crc_wrap", "crc_success", "comm_deadline",
                    "comm_wrap", "comm_success", "hardware", "protocol"):
        add(suite, "NFC-01.deadline-" + variant, "NFC-01", "L1",
            [sys.executable, HERE / "test_mfrc522_deadline.py",
             "DeadlineTest.test_" + variant], marker=False)
    for variant in ("offline", "revision", "owner", "invalid", "unbind"):
        add(suite, "NET-03.pc-owner-" + variant, "NET-03", "L2",
            [sys.executable, HERE / "test_pc_owner_binding.py", variant],
            binaries["test_pc_owner_binding"])
        add(suite, "NET-03.pc-product-" + variant, "NET-03", "L2",
            [sys.executable, HERE / "test_pc_product_route.py", variant])
    for variant in ("terminal", "duplicate", "ordering", "expiry", "binding", "readonly", "invalid", "quiesce", "rate", "visual"):
        add(suite, "PC-01.task-" + variant, "PC-01", "L1",
            [HERE / "build/test_pc_tasks", variant], binaries["test_pc_tasks"])
    add(suite, "PC-01.task-product", "PC-01", "L2",
        [sys.executable, HERE / "test_pc_product_route.py", "tasks"])
    for variant in ("source", "source-revision"):
        add(suite, "NET-03.pc-owner-" + variant, "NET-03", "L2",
            [sys.executable, HERE / "test_pc_owner_binding.py", variant],
            binaries["test_pc_owner_binding"])
    for variant in ("basic", "auth", "invalid", "cancel", "failure", "unknown", "pending", "reopen", "retry-error"):
        add(suite, "NET-03.pc-auth-" + variant, "NET-03", "L2",
            [sys.executable, HERE / "test_pc_authorization.py", variant],
            binaries["test_pc_authorization"])
    for variant in ("blocked-copy", "revision", "reopen", "write-failure", "unknown", "reset"):
        add(suite, "NET-03.pc-storage-" + variant, "NET-03", "L2",
            [sys.executable, HERE / "test_pc_storage.py", variant],
            binaries["test_pc_storage"])
    for variant in ("clear", "unlink", "sync", "symlink", "absent", "no-marker"):
        add(suite, "RST-01.pc-" + variant, "RST-01", "L2",
            [sys.executable, HERE / "test_pc_reset.py", variant],
            binaries["test_pc_reset"])
    for variant in ("persist", "owner", "revision", "invalid", "writefail",
                    "uncertain", "corrupt", "golden", "aliased-key"):
        add(suite, "NET-03.pc-grant-" + variant, "NET-03", "L2",
            [HERE / "build/test_pc_grants", variant],
            binaries["test_pc_grants"])
    for variant in ("binary", "backpressure", "disconnect", "invalid", "failed-open",
                    "cleanup-get", "cleanup-set", "close-get", "close-set", "close-hangup", "close-lost"):
        add(suite, "USB-02.serial-" + variant, "USB-02", "L2",
            [HERE / "build/test_control_serial", variant],
            binaries["test_control_serial"])
    for variant in (
        "fragmented_tls_auth_status_and_info",
        "certificate_pin_mismatch_sends_no_secret",
        "valid_chain_with_wrong_leaf_never_receives_pc_key",
        "wrong_principal_closes_without_status",
        "malformed_authenticated_responses_close",
        "stall_and_backpressure_have_absolute_deadlines",
        "error_response_is_not_success_or_replayed",
        "no_request_before_auth_or_after_close",
        "cli_uses_real_client_without_printing_credentials",
        "native_adapter_preserves_partial_io_without_global_output_redirect",
        "native_port_filter_rejects_uart_without_opening",
        "cli_invalid_certificate_never_opens_port",
        "clock_rollback_terminates_without_replay",
    ):
        add(suite, "USB-01.pc-client-" + variant, "USB-01", "L2",
            [sys.executable, HERE / "test_workbench_client.py",
             "WorkbenchClientTest.test_" + variant], marker=False)
    for variant in ("golden", "invalid", "snapshot", "staging", "failure", "deadline", "cli"):
        add(suite, "RES-03.client-" + variant, "RES-03", "L1",
            [sys.executable, HERE / "test_workbench_resources.py",
             "ResourcesTest.test_" + variant], marker=False)
    for variant in ("upload", "cli_tls_upload", "lost_ack_resume", "receipt_precedes_begin",
                    "unknown_epoch_no_replay", "cancel_and_no_default",
                    "changed_file_and_existing_receipt", "non_pack_rejected_before_connect"):
        add(suite, "RES-03.flow-" + variant, "RES-03", "L2",
            [sys.executable, HERE / "test_workbench_resource_flow.py",
             "ResourceFlow.test_" + variant], marker=False)
    for variant in ("golden", "invalid", "staging", "readback", "failure", "cli"):
        add(suite, "PC-01.sender-" + variant, "PC-01", "L1",
            [sys.executable, HERE / "test_workbench_tasks.py",
             "TasksTest.test_" + variant], marker=False)
    for variant in (
        'profile_binds_pin_certificate_key_and_clears_borrowed_plaintext',
        'tampered_truncated_and_unknown_profiles_fail_closed',
        'existing_profile_and_symlink_are_never_overwritten',
        'invalid_material_and_os_failure_never_create_profile',
        'cli_profile_reaches_real_tls_client_without_plaintext_file',
        'cli_invalid_profile_never_opens_port_or_falls_back',
        'cli_save_profile_is_offline_and_preserves_import_source',
        'io_failures_do_not_publish_success_or_overwrite',
        'cli_mixed_profile_credentials_never_downgrade',
        'os_bridge_uses_stdin_current_user_and_generic_failure',
        'authenticated_profile_with_wrong_trust_binding_is_rejected',
    ):
        add(suite, "USB-01.pc-profile-" + variant, "USB-01", "L2",
            [sys.executable, HERE / "test_workbench_profile.py",
             "ProfileTest.test_" + variant], marker=False)
    for variant in ('request_layout_pending_protection_and_roundtrip', 'expiry_rollback_and_invalid_permissions_fail_without_profile', 'tamper_wrong_request_caps_key_and_trusted_pin_rejected', 'existing_outputs_are_not_replaced_and_import_does_not_authorize_device', 'invalid_pending_or_unavailable_protection_never_exposes_private_material', 'cli_pairing_is_offline_and_rejects_mixed_credentials', 'pending_readback_failure_never_publishes_request'):
        add(suite, "USB-01.pc-pairing-" + variant, "USB-01", "L2",
            [sys.executable, HERE / "test_workbench_pairing.py",
             "PairingTest.test_" + variant], marker=False)
    for variant in ("dates", "not_yet_valid", "valid", "expired"):
        add(suite, "USB-01.fixture-validity-" + variant, "USB-01", "L1",
            [sys.executable, HERE / "test_tls_test_identity.py", "IdentityTest.test_" + variant],
            marker=False, setup_exit_code=2)
    add(suite, "RES-02.selection-version-legacy-recovery", "RES-02", "L2",
        [HERE / "build/test_bk7258_display_pack", HERE / "build/shaniu-default-v1.bkep"],
        ready=binaries["test_bk7258_display_pack"], marker=False)
    for variant in ("success", "invalid", "revoke", "cancel", "refresh", "recover", "product"):
        add(suite, "RES-02.selection-wire-" + variant, "RES-02", "L2",
            [sys.executable, HERE / "test_pack_trial.py", "selection-wire-" + variant],
            ready=binaries["test_display_upload"], setup_exit_code=2)
    for variant in ("success", "gate", "failure"):
        add(suite, "RES-02.selection-recover-" + variant, "RES-02", "L2",
            [sys.executable, HERE / "test_pack_trial.py", "selection-recover-" + variant],
            ready=binaries["test_display_upload"], setup_exit_code=2)
    for variant in ("refresh", "cancel", "gate", "preparing-cancel", "commit-cancel",
                    "render-failure", "release-failure", "stale", "supersede", "stale-job", "commit-unknown"):
        add(suite, "RES-02.selection-job-" + variant, "RES-02", "L2",
            [sys.executable, HERE / "test_pack_trial.py", "selection-" + variant],
            ready=binaries["test_display_upload"], setup_exit_code=2)
    for variant in ("migration", "stale", "local-writer", "malformed", "overflow", "directory-failure"):
        add(suite, "RES-02.selection-version-" + variant, "RES-02", "L2",
            [HERE / "build/test_display_selection", HERE / "build/shaniu-default-v1.bkep", variant],
            ready=binaries["test_display_selection"])
    for variant in ("cancel", "expiry", "queued-cancel", "queued-expiry", "supersede", "missing", "invalid",
                    "wire-cancel", "wire-expiry", "wire-invalid", "wire-missing",
                    "pc-cancel", "pc-expiry", "pc-missing"):
        add(suite, "RES-02.pack-trial-" + variant, "RES-02", "L2",
            [sys.executable, HERE / "test_pack_trial.py", variant],
            ready=binaries["test_display_upload"], marker=not variant.startswith("pc-"),
            setup_exit_code=2)
    for cls, variants in (
        ("TrialTest", ("golden", "invalid", "snapshot", "staging", "coherent_read", "no_replay", "cli_preflight",
                       "pack_golden", "pack_invalid", "pack_staging", "pack_old_firmware", "pack_cli_preflight")),
        ("WireTest", ("lifecycle", "retry_expiry", "stale_cancel")),
    ):
        for variant in variants:
            add(suite, "RES-02.pc-trial-" + variant, "RES-02", "L2" if cls == "WireTest" else "L1",
                [sys.executable, HERE / "test_workbench_trial.py", cls + ".test_" + variant],
                ready=binaries["test_shaniu_trial_wire"] if cls == "WireTest" else True,
                marker=False, setup_exit_code=2)
    for variant in ("upload", "reconnect", "cancel", "wrong_principal"):
        add(suite, "RES-03.native-tls-" + variant, "RES-03", "L2",
            [sys.executable, HERE / "test_provision_tls.py", "--resource-case", variant],
            marker=False, setup_exit_code=2)
    add(suite, "USB-01.tls-transport", "USB-01", "L2",
        [sys.executable, HERE / "test_provision_tls.py"], marker=False,
        setup_exit_code=2)
    for variant in (
        "fast_reader", "slow_reader", "partial", "upper_backpressure",
        "arm_failure", "reset", "duplicate_callback",
    ):
        add(suite, "USB-02.rx-" + variant, "USB-02",
            "L2" if variant in ("fast_reader", "upper_backpressure") else "L1",
            [sys.executable, HERE / "test_shaniu_usbcdc_rx.py",
             "CdcRxTest.test_" + variant], marker=False)
    for variant in (
        "start_failure", "queued_progress", "upper_start",
        "upper_backpressure", "wrong_completion",
    ):
        add(suite, "USB-02.tx-" + variant, "USB-02",
            "L2" if variant.startswith("upper_") else "L1",
            [sys.executable, HERE / "test_shaniu_usbcdc_tx.py",
             "CdcTxTest.test_" + variant], marker=False)
    for variant in (
        "wake_disconnect", "fast_reconnect", "drop_old_queue", "new_open",
        "offline_open",
    ):
        add(suite, "USB-02.life-" + variant, "USB-02", "L2",
            [sys.executable, HERE / "test_shaniu_usbcdc_lifecycle.py",
             "CdcLifeTest.test_" + variant], marker=False)
    for variant in ("register", "inactive", "invalid", "not_ready", "partial",
                    "status_error", "data_error", "fresh"):
        add(suite, "MOT-01.sensor-" + variant, "MOT-01", "L1",
            [sys.executable, HERE / "test_sc7a20_sampling.py",
             "SamplingTest.test_" + variant], marker=False)
    for variant in ("voice", "gate", "revision", "cancel", "invalid"):
        add(
            suite,
            "TIMER-01.intent-" + variant,
            "TIMER-01",
            "L2",
            [HERE / "build/test_shaniu_focus_intent", variant],
            binaries["test_shaniu_focus_intent"],
        )
    for variant in ("start", "retry", "cancel", "invalid"):
        add(
            suite,
            "TIMER-01.shared-" + variant,
            "TIMER-01",
            "L2",
            [HERE / "build/test_shaniu_focus_shared", variant],
            binaries["test_shaniu_focus_shared"],
        )
    for variant in ("clock", "replay", "invalid"):
        add(
            suite,
            "TIMER-01." + variant,
            "TIMER-01",
            "L1",
            [HERE / "build/test_shaniu_focus", variant],
            binaries["test_shaniu_focus"],
        )
    add(
        suite,
        "TIMER-01.wire",
        "TIMER-01",
        "L2",
        [HERE / "build/test_shaniu_focus_wire"],
        binaries["test_shaniu_focus_wire"],
    )
    for variant in ("tail", "cancel-next"):
        add(
            suite,
            "AUD-03.media-" + variant,
            "AUD-03",
            "L1",
            [HERE / "build/test_bk7258_agent_media_player", variant],
            binaries["test_bk7258_agent_media_player"],
        )
    add(
        suite,
        "NET-02.display-snapshot",
        "NET-02",
        "L1",
        [HERE / "build/test_shaniu_display_snapshot"],
        binaries["test_shaniu_display_snapshot"],
    )
    add(
        suite,
        "DISP-01.expression-intent",
        "DISP-01",
        "L1",
        [HERE / "build/test_shaniu_display_intent"],
        binaries["test_shaniu_display_intent"],
    )
    add(
        suite,
        "DISP-01.expression-cancel",
        "DISP-01",
        "L1",
        [HERE / "build/test_shaniu_expression_cancel"],
        binaries["test_shaniu_expression_cancel"],
    )
    add(
        suite,
        "RES-02.trial-wire",
        "RES-02",
        "L2",
        [HERE / "build/test_shaniu_trial_wire"],
        binaries["test_shaniu_trial_wire"],
    )
    add(
        suite,
        "RES-02.expression-trial",
        "RES-02",
        "L2",
        [HERE / "build/test_shaniu_expression_trial"],
        binaries["test_shaniu_expression_trial"],
    )
    add(
        suite,
        "RES-02.expression-ownership",
        "RES-02",
        "L2",
        [HERE / "build/test_shaniu_expression_ownership"],
        binaries["test_shaniu_expression_ownership"],
    )
    add(
        suite,
        "CFG-02.models-unknown",
        "CFG-02",
        "L2",
        [HERE / "build/test_shaniu_models_durability"],
        binaries["test_shaniu_models_durability"],
    )
    for variant in ("normal", "retry", "start-cleanup", "close-error"):
        add(
            suite,
            "MSC-01.backend-stop-" + variant,
            "MSC-01",
            "L1",
            [HERE / "build/test_shaniu_msc_stop", variant],
            binaries["test_shaniu_msc_stop"],
        )
    for variant in (
        "normal",
        "admission-failure",
        "partial-failure",
        "cp-declined",
        "cp-unknown",
        "admission-stops-trigger",
        "storage-stops-trigger",
        "trigger-failure",
        "unpublished-trigger",
        "admission-drains",
        "failed-drains",
        "final-close-drains",
        "failure-display",
    ):
        add(
            suite,
            "LIFE-02.power-" + variant,
            "LIFE-02",
            "L1",
            [HERE / "build/test_shaniu_power_contract", variant],
            binaries["test_shaniu_power_contract"],
        )
    for variant in ("queries", "staging", "config-staging", "ota-commit", "invalid"):
        add(
            suite,
            "NET-03.quiesce-" + variant,
            "NET-03",
            "L1",
            [HERE / "build/test_shaniu_control_quiesce", variant],
            binaries["test_shaniu_control_quiesce"],
        )
    for variant in ("queries", "unauthenticated", "invalid-sequence"):
        add(
            suite,
            "NET-03.owner-" + variant,
            "NET-03",
            "L2",
            [HERE / "build/test_shaniu_owner", variant],
            binaries["test_shaniu_owner"],
        )
    add(
        suite,
        "NET-03.owner-legacy",
        "NET-03",
        "L1",
        [HERE / "build/test_shaniu_owner"],
        binaries["test_shaniu_owner"],
        marker=False,
    )
    add(
        suite,
        "LIFE-01.owner-integration",
        "LIFE-01",
        "L2",
        [HERE / "build/test_shaniu_power_owner", "owner-integration"],
        binaries["test_shaniu_power_owner"],
    )
    for variant in ("close-failure", "route-failure"):
        add(
            suite,
            "LIFE-02.capture-" + variant,
            "LIFE-02",
            "L2",
            [HERE / "build/test_bk7258_agent_capture", variant],
            binaries["test_bk7258_agent_capture"],
        )
    for variant in ("acquiring", "releasing", "retry"):
        add(
            suite,
            "MSC-01." + variant,
            "MSC-01",
            "L1",
            [HERE / "build/test_shaniu_volume_transition", variant],
            binaries["test_shaniu_volume_transition"],
        )
    for variant in ("unmount-failure", "local-busy", "wrong-owner", "handoff"):
        add(
            suite,
            "MSC-01." + variant,
            "MSC-01",
            "L2",
            [HERE / "build/test_shaniu_volume_contract", variant],
            binaries["test_shaniu_volume_contract"],
        )
    for variant in (
        "pcm-1",
        "pcm-20260924",
        "pcm-4294967295",
        "cancel-late",
        "sse-1",
        "sse-20260924",
    ):
        parent = "AUD-03" if variant == "cancel-late" else "AUD-01"
        add(
            suite,
            parent + "." + variant,
            parent,
            "L2",
            [HERE / "build/test_agent_tts_queue", variant],
            binaries["test_agent_tts_queue"],
        )
    for index, variant in enumerate(
        (
            "uid-empty",
            "select-timeout",
            "select-error",
            "uid-size",
            "uid-incomplete",
            "uid-4",
            "uid-7",
            "uid-10",
        )
    ):
        add(
            suite,
            "NFC-01." + variant,
            "NFC-01",
            "L2",
            [HERE / "build/test_bk7258_nfc_rpc", str(index)],
            binaries["test_bk7258_nfc_rpc"],
        )
    for variant in ("empty", "oversize", "hce-positive"):
        add(
            suite,
            "NFC-01." + variant,
            "NFC-01",
            "L1",
            [HERE / "build/test_bk7258_nfc_core", variant],
            binaries["test_bk7258_nfc_core"],
        )
    for parent, name in (
        ("K2-01", "product_keys"),
        ("MSC-02", "usbmode_lease"),
        ("MOT-01", "motion_core"),
        ("NFC-01", "nfc_core"),
    ):
        target = "test_bk7258_" + name
        add(
            suite,
            parent + ".legacy-suite",
            parent,
            "L1",
            [HERE / "build" / target],
            binaries[target],
            marker=False,
        )
    with tempfile.TemporaryDirectory(prefix="shaniu-contract-") as directory:
        temp = Path(directory)
        config_temp = temp / "config"
        config_temp.mkdir()
        config_source = ROOT / "app/bk7258/bk7258_provision_config.c"
        ready = build(config_build(config_temp, config_source), "config-build")
        cert = temp / "public-ca.der"
        cert_ready = build(
            [
                "openssl",
                "x509",
                "-in",
                ROOT.parent
                / "apps/crypto/mbedtls/mbedtls/tests/data_files/test-ca.crt",
                "-outform",
                "DER",
                "-out",
                cert,
            ],
            "public-cert",
        )
        for variant in (
            "wifi-reopen",
            "file-sync-failure",
            "file-sync-retry",
            "directory-sync-unknown",
            "stale",
            "conflict",
            "keep-key",
            "replace-key",
            "clear-cloud",
            "cross-host-retain",
        ):
            parent = (
                "STORE-02"
                if "sync-" in variant
                else "CFG-01" if variant == "wifi-reopen" else "CFG-03"
            )
            add(
                suite,
                parent + "." + variant,
                parent,
                "L2",
                [
                    config_temp / "test",
                    variant,
                    temp / ("private-" + variant),
                    cert,
                    GOLDEN,
                ],
                ready and cert_ready,
            )
        for phase in ("before-publish", "after-publish"):
            add(
                suite,
                "STORE-02.restart-" + phase,
                "STORE-02",
                "L2",
                [
                    sys.executable,
                    HERE / "test_shaniu_config_restart.py",
                    config_temp / "test",
                    phase,
                    temp / ("restart-" + phase),
                    cert,
                    GOLDEN,
                ],
                ready and cert_ready,
            )
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        mutation_results = (
            mutations(temp, config_temp, cert) if ready and cert_ready else []
        )
        if mutation_results:
            restored = build(
                config_build(config_temp, config_source), "config-restored-build"
            )
            restore_suite = unittest.TestSuite()
            add(
                restore_suite,
                "CFG-01.restored",
                "CFG-01",
                "L2",
                [
                    config_temp / "test",
                    "wifi-reopen",
                    temp / "private-restored",
                    cert,
                    GOLDEN,
                ],
                restored,
            )
            add(
                restore_suite,
                "MSC-01.restored",
                "MSC-01",
                "L2",
                [HERE / "build/test_shaniu_volume_contract", "unmount-failure"],
                binaries["test_shaniu_volume_contract"],
            )
            restore_result = unittest.TextTestRunner(verbosity=2).run(restore_suite)
        else:
            restore_result = None
        cert_hash = digest(cert) if cert_ready else None
    jvm_code = run_jvm()
    catalog = json.loads((HERE / "acceptance/cases.v1.json").read_text())
    cases = []
    for spec in catalog["cases"]:
        executed = [r["id"] for r in RESULTS if r["parent"] == spec["id"]]
        # A partial host check is never completion of a composite requirement.
        status = "BLOCKED_DEVICE" if "L3" in spec["layers"] else "BLOCKED_INTERFACE"
        if any(
            r["status"] == "FAIL_ASSERTION"
            for r in RESULTS
            if r["parent"] == spec["id"]
        ):
            status = "FAIL_ASSERTION"
        elif any(
            r["status"] == "SETUP_ERROR" for r in RESULTS if r["parent"] == spec["id"]
        ):
            status = "SETUP_ERROR"
        if spec["id"] in ("K2-03", "AGENT-02") and status not in (
            "FAIL_ASSERTION",
            "SETUP_ERROR",
        ):
            status = "NOT_RUN"  # Untrusted/missing-edge source policy still needs integration.
        cases.append(
            dict(
                id=spec["id"],
                status=status,
                collected=executed,
                outstanding_layers=spec["layers"],
                evidence_by_layer={
                    layer: dict(
                        status=(
                            "BLOCKED_DEVICE"
                            if layer == "L3"
                            else (
                                "PARTIAL"
                                if any(
                                    r["parent"] == spec["id"] and r["layer"] == layer
                                    for r in RESULTS
                                )
                                else "NOT_RUN"
                            )
                        ),
                        collected=[
                            r["id"]
                            for r in RESULTS
                            if r["parent"] == spec["id"] and r["layer"] == layer
                        ],
                        gap="Composite coverage remains outstanding; see binding and contract",
                    )
                    for layer in spec["layers"]
                },
                interface=dict(
                    status="PARTIAL_BINDING" if executed else "REQUIRES_BINDING_REVIEW",
                    gap=spec["binding"],
                ),
                binding=spec["binding"],
                remaining="See contracts.md binding and per-case scope; composite is not complete",
            )
        )
    counts = {
        s: sum(r["status"] == s for r in RESULTS)
        for s in ("PASS", "FAIL_ASSERTION", "SETUP_ERROR", "NOT_RUN")
    }
    after = production_digest()
    inputs = {str(p.relative_to(ROOT)): digest(p) for p in HERE.glob("test_shaniu*.*")}
    inputs.update(
        {
            str(p.relative_to(ROOT)): digest(p)
            for p in [
                HERE / "acceptance/cases.v1.json",
                HERE / "acceptance/data-manifest.v1.json",
                HERE / "acceptance/required-units.v1.json",
            ]
        }
    )
    for p in [
        GOLDEN,
        HERE / "Makefile",
        HERE / "test_provision_owner.c",
        HERE / "test_bk7258_agent_media_player.c",
        HERE / "test_bk7258_cloud_http.py",
        HERE / "test_sc7a20_sampling.py",
        HERE / "test_shaniu_usbcdc_rx.py",
        HERE / "test_shaniu_usbcdc_tx.py",
        HERE / "test_shaniu_usbcdc_lifecycle.py",
        HERE / "test_provision_tls.py",
        HERE / "tls_test_identity.py",
        HERE / "test_tls_test_identity.py",
        HERE / "tls_entropy_tape.c",
        HERE / "test_tls_entropy_tape.py",
        HERE / "test_provision_tls.c",
        HERE / "test_pack_native_fixture.c",
        HERE / "test_workbench_native_tls.py",
        HERE / "test_control_serial_peer.c",
        HERE / "test_provision_tls_transport.c",
        ROOT / "app/bk7258/bk7258_provision_tls.c",
        ROOT / "app/bk7258/bk7258_provision_tls.h",
        ROOT / "app/bk7258/bk7258_control_pair.c",
        ROOT / "app/bk7258/bk7258_control_pair.h",
        HERE / "test_display_job.c",
        HERE / "test_display_job_service.c",
        HERE / "test_pack_product_route.py",
        ROOT / "app/bk7258/bk7258_display_job.c",
        ROOT / "app/bk7258/bk7258_display_job.h",
        ROOT / "app/bk7258/bk7258_display_job_service.c",
        ROOT / "app/bk7258/bk7258_display_job_service.h",
        ROOT / "app/bk7258/bk7258_display_job_control.c",
        ROOT / "app/bk7258/bk7258_display_job_control.h",
        HERE / "test_display_upload.c",
        HERE / "build/shaniu-default-v1.bkep",
        HERE / "build/upload-second.bkep",
        ROOT / "app/bk7258/bk7258_display_store.c",
        ROOT / "app/bk7258/bk7258_display_store.h",
        ROOT / "app/bk7258/bk7258_display_pack.c",
        ROOT / "app/bk7258/bk7258_display_pack.h",
        HERE / "test_control_pair.c",
        HERE / "test_control_serial.c",
        HERE / "test_control_serial_peer.c",
        ROOT / "app/bk7258/bk7258_control_serial.c",
        ROOT / "app/bk7258/bk7258_control_serial.h",
        ROOT / "app/bk7258/bk7258_pc_grants.c",
        ROOT / "app/bk7258/bk7258_pc_grants.h",
        ROOT / "app/bk7258/bk7258_pc_control.c",
        ROOT / "app/bk7258/bk7258_pc_usb.c",
        ROOT / "app/bk7258/bk7258_pc_usb.h",
        ROOT / "app/bk7258/bk7258_pc_control.h",
        HERE / "test_pc_grants.c",
        HERE / "test_pc_reset.c",
        HERE / "test_pc_reset.py",
        HERE / "test_pc_storage.c",
        HERE / "test_pc_storage.py",
        HERE / "test_pc_authorization.c",
        HERE / "test_pc_authorization.py",
        HERE / "test_pc_tasks.c",
        ROOT / "app/bk7258/bk7258_pc_tasks.c",
        ROOT / "app/bk7258/bk7258_pc_tasks.h",
        HERE / "test_pc_owner_binding.c",
        HERE / "test_pc_owner_binding.py",
        HERE / "test_pc_product_route.py",
        ROOT / "app/bk7258/bk7258_pc_authorization_owner.c",
        ROOT / "app/bk7258/bk7258_pc_authorization_owner.h",
        ROOT / "app/bk7258/bk7258_pc_authorization.c",
        ROOT / "app/bk7258/bk7258_pc_authorization.h",
        ROOT / "tools/bk7258/_lib/workbench.py",
        ROOT / "tools/bk7258/_lib/workbench_profile.py",
        HERE / "test_workbench_profile.py",
        ROOT / "tools/bk7258/_lib/workbench_pairing.py",
        HERE / "test_workbench_pairing.py",
        HERE / "test_workbench_pairing_interop.py",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/PcPairingExchange.kt",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/PcPairingDelivery.kt",
        ROOT / "android/shaniu-companion/app/src/test/java/com/shaniu/companion/provision/PcPairingExchangeTest.kt",
        ROOT / "tools/bk7258/_lib/deploy_usb.py",
        ROOT / "tools/bk7258/bk7258.py",
        HERE / "test_workbench_client.py",
        HERE / "test_workbench_tasks.py",
        HERE / "test_workbench_resources.py",
        HERE / "test_workbench_resource_flow.py",
        ROOT / "tools/bk7258/_lib/workbench_resources.py",
        ROOT / "tools/bk7258/_lib/workbench_trial.py",
        HERE / "test_workbench_trial.py",
        HERE / "test_pack_trial.py",
        HERE / "test_pack_trial.c",
        HERE / "test_display_selection.c",
        HERE / "test_bk7258_display_pack.c",
        ROOT / "app/bk7258/bk7258_display_service.c",
        ROOT / "app/bk7258/bk7258_display_service.h",
        HERE / "test_shaniu_trial_wire.c",
        ROOT / "app/bk7258/bk7258_display_trial_control.c",
        ROOT / "app/bk7258/bk7258_display_selection_control.c",
        ROOT / "app/bk7258/bk7258_display_selection_control.h",
        ROOT / "app/bk7258/bk7258_display_trial_control.h",
        ROOT / "app/bk7258/bk7258_display_intent.inc",
        ROOT / "app/bk7258/bk7258_display_selection_request.inc",
        ROOT / "app/bk7258/bk7258_display_selection.inc",
        ROOT / "app/bk7258/bk7258_display_render_identity.inc",
        ROOT / "tools/bk7258/_lib/workbench_tasks.py",
        HERE / "test_provision_tls.c",
        HERE / "test_pack_native_fixture.c",
        HERE / "test_workbench_native_tls.py",
        HERE / "test_control_serial_peer.c",
        HERE / "test_provision_tls.py",
        HERE / "tls_entropy_tape.c",
        HERE / "test_tls_entropy_tape.py",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/ProvisionTls.kt",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/ProvisionTlsChannel.kt",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/ProvisionGattSession.kt",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/AndroidProvisionGatt.kt",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/DeviceControlConnection.kt",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/AndroidDeviceControlTransport.kt",
        ROOT / "android/shaniu-companion/app/src/test/java/com/shaniu/companion/provision/ProvisionTlsTest.kt",
        ROOT / "android/shaniu-companion/app/src/test/java/com/shaniu/companion/provision/DeviceControlTlsInteropTest.kt",
        ROOT / "android/shaniu-companion/app/src/test/resources/pc-identity.pem",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/PcAuthorizationController.kt",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/DeviceControlProtocol.kt",
        ROOT / "android/shaniu-companion/app/src/main/java/com/shaniu/companion/MainActivity.kt",
        ROOT / "chips/bk7258/ap/bk7258_usbcdc.c",
        ROOT / "nuttx/drivers/sensors/sc7a20.c",
        ROOT / "nuttx/include/nuttx/sensors/sc7a20.h",
        *ROOT.glob(
            "android/shaniu-companion/app/src/test/java/com/shaniu/companion/ota/*Test.kt"
        ),
        *ROOT.glob(
            "android/shaniu-companion/app/src/test/java/com/shaniu/companion/provision/*Test.kt"
        ),
    ]:
        inputs[str(p.relative_to(ROOT))] = digest(p)
    report = dict(
        schema_version=1,
        production_baseline=BASE,
        checkout_head=git("rev-parse", "HEAD"),
        test_commit="See submission commit; input hashes bind uncommitted test execution",
        agent_head=git("rev-parse", "HEAD", cwd=ROOT.parent / "packages/ai_agent"),
        production_before=before,
        production_after=after,
        production_unchanged=before == after,
        environment=dict(
            platform=platform.platform(),
            python=platform.python_version(),
            cc=subprocess.check_output(["cc", "--version"], text=True).splitlines()[0],
            cmake=subprocess.check_output(
                ["cmake", "--version"], text=True
            ).splitlines()[0],
            java=subprocess.run(
                ["java", "-version"], capture_output=True, text=True
            ).stderr.splitlines()[0],
        ),
        inputs=inputs,
        capture_inputs={
            str(p.relative_to(ROOT.parent)): digest(p)
            for p in (
                ROOT.parent / "packages/ai_agent/src/voice/audio_capture.c",
                ROOT.parent / "packages/ai_agent/include/voice/audio_capture.h",
                HERE / "test_bk7258_agent_media_recorder.c",
            )
        },
        public_ca_sha256=cert_hash,
        counts=counts,
        execution_groups={
            group: dict(
                collected=len(items),
                counts={
                    status: sum(r["status"] == status for r in items)
                    for status in counts
                },
            )
            for group, items in {
                "original_63_including_restores": [
                    r
                    for r in RESULTS
                    if r["id"] in SELECTION.get("baseline_ids", REQUIRED)
                ],
                "added": [
                    r for r in RESULTS if r["id"] in SELECTION.get("added_ids", [])
                ],
                "restores_also_in_original_63": [
                    r for r in RESULTS if r["id"].endswith(".restored")
                ],
            }.items()
        },
        collection_errors=collection_errors(RESULTS, REQUIRED),
        selected_execution_ids=REQUIRED,
        collected=RESULTS,
        cases=cases,
        builds=BUILDS,
        mutations=mutation_results,
    )
    (OUT / "results.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    )
    print(json.dumps(counts), "production_unchanged=", before == after)
    return (
        0
        if (
            result.wasSuccessful()
            and not collection_errors(RESULTS, REQUIRED)
            and jvm_code == 0
            and before == after
            and restore_result is not None
            and restore_result.wasSuccessful()
            and len(mutation_results) == 2
            and all(m["status"] == "DETECTED" for m in mutation_results)
        )
        else 1
    )


if __name__ == "__main__":
    sys.exit(main())
