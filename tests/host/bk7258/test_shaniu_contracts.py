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


def case(case_id, parent, layer, args, ready=True, marker=True):
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


def add(suite, case_id, parent, layer, args, ready=True, marker=True):
    def run():
        case(case_id, parent, layer, args, ready, marker)

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
            "voice_config",
        )
    ]
    snippet = (
        "from pathlib import Path; from test_bk7258_cloud_http import build_http_fixture; "
        "import sys; build_http_fixture(Path(sys.argv[1]), Path(sys.argv[2]), "
        "[Path(p) for p in sys.argv[3:]])"
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
        "provision.ProvisionSettingsTest",
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
            }
            parent = (
                "OTA-01"
                if name.startswith("ota")
                else (
                    "CFG-03"
                    if "Settings" in name
                    else session_parents.get(node.attrib["name"], "NET-02")
                )
            )
            RESULTS.append(
                dict(
                    id=name + "." + node.attrib["name"],
                    parent=parent,
                    layer="L2" if "Session" in name else "L1",
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
        "test_shaniu_key_contract",
        "test_shaniu_volume_contract",
        "test_shaniu_volume_transition",
        "test_bk7258_agent_capture",
        "test_shaniu_power_contract",
        "test_agent_tts_queue",
        "test_bk7258_product_keys",
        "test_bk7258_usbmode_lease",
        "test_bk7258_motion_core",
        "test_bk7258_nfc_core",
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
    ):
        add(
            suite,
            "LIFE-02.power-" + variant,
            "LIFE-02",
            "L1",
            [HERE / "build/test_shaniu_power_contract", variant],
            binaries["test_shaniu_power_contract"],
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
            "stale",
            "conflict",
            "keep-key",
            "replace-key",
            "clear-cloud",
            "cross-host-retain",
        ):
            parent = "CFG-01" if variant == "wifi-reopen" else "CFG-03"
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
        HERE / "test_bk7258_agent_media_player.c",
        HERE / "test_bk7258_cloud_http.py",
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
