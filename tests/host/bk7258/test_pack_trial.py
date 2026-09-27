#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile actual renderer/cache/volume bodies with external mount/FB peers."""
from pathlib import Path
import re
import os
import json
import hashlib
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "app/bk7258"
HERE = ROOT / "tests/host/bk7258"


def main():
    source = (APP / "bk7258_display_service.c").read_text()
    fragments = []
    for marker in (
        "struct bkdisplay_service_s\n",
        "enum bkdisplay_diagnostic_stage_e\n",
    ):
        begin = source.index(marker)
        end = source.index("};", begin) + 2
        fragments.append(source[begin:end])
    for name in (
        "bkdisplay_service_errno",
        "bkdisplay_service_retryable",
        "bkdisplay_blockdev_acquire",
        "bkdisplay_blockdev_release",
        "bkdisplay_volume_open",
        "bkdisplay_volume_close",
        "bkdisplay_diagnostic_stage_name",
        "bkdisplay_diagnostic_failure",
        "bkdisplay_status_error",
        "bkdisplay_cache_frames",
        "bkdisplay_render_pack_pixels_locked",
    ):
        match = re.search(r"^static [^\n]*\b" + name + r"\([^;]*?\)\n\{", source, re.M)
        if not match:
            raise RuntimeError(
                "Required production renderer interface missing: " + name
            )
        start = match.start()
        pos = match.end()
        depth = 1
        while depth:
            depth += (source[pos] == "{") - (source[pos] == "}")
            pos += 1
        fragments.append(source[start:pos])
    with tempfile.TemporaryDirectory(prefix="pack-trial-build-") as d:
        temp = Path(d)
        renderer = "\n".join(fragments)
        if os.environ.get("SHANIU_TEST_MUTATE_FB1") == "1":
            needle = "ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, pixels);"
            if renderer.count(needle) != 1:
                raise RuntimeError("renderer mutation target changed")
            renderer = renderer.replace(needle, "ret = 0; /* isolated missed FB1 */")
        (temp / "pack-trial-render.inc").write_text(renderer)
        print(
            "RENDERER_SHA256=" + hashlib.sha256(renderer.encode()).hexdigest(),
            flush=True,
        )
        from test_nfc_rf_lifecycle import function

        product = (APP / "bk7258_agent_product.c").read_text()
        (temp / "selection-product.inc").write_text(
            function(product, "product_pc_pack_step")
            + function(product, "product_pc_config")
        )
        if sys.argv[1].startswith("selection-wire-phone-"):
            (temp / "selection-phone.inc").write_text(
                function(product, "product_phone_selection_step")
                + function(product, "product_phone_selection_config")
            )
        # Every palette entry is green: expected RGB565 is independently 0x07e0
        # for every rendered pixel, regardless of expression geometry.
        spec = json.loads((APP / "assets/display/shaniu-default-v1.json").read_text())
        spec["pack_id"] = "shaniu-upload-v1"
        spec["palette"] = ["#00FF00"] * len(spec["palette"])
        source_file = temp / "green.json"
        source_file.write_text(json.dumps(spec))
        fixture = temp / "green.bkep"
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/bk7258/bk7258.py"),
                "package",
                "eye-pack",
                "--source",
                str(source_file),
                "--output",
                str(fixture),
            ],
            check=True,
        )
        print(
            "PACK_TRIAL_INPUT_SHA256="
            + hashlib.sha256(fixture.read_bytes()).hexdigest(),
            flush=True,
        )
        tls = sys.argv[1].startswith(
            ("android-default-tls-", "android-trial-tls-", "web-display-tls-")
        )
        tls_flags = []
        if tls:
            from tls_test_identity import issue

            crypto = ROOT.parent / "apps/crypto/mbedtls/mbedtls"
            build = temp / "crypto"

            def run(args):
                subprocess.run([str(x) for x in args], check=True)

            run(
                [
                    "cmake",
                    "-S",
                    crypto,
                    "-B",
                    build,
                    "-DENABLE_PROGRAMS=OFF",
                    "-DENABLE_TESTING=OFF",
                    "-DCMAKE_C_FLAGS=-Wno-error=missing-prototypes",
                    "-DCMAKE_BUILD_TYPE=Release",
                ]
            )
            run(["cmake", "--build", build, "-j8"])
            issue(run, temp)
            print(
                "TLS_CERT_SHA256="
                + hashlib.sha256((temp / "cert.pem").read_bytes()).hexdigest(),
                flush=True,
            )
            tls_flags = ["-DTEST_SELECTION_TLS", "-I", str(crypto / "include")]
            tls_flags += [
                str(APP / (name + ".c"))
                for name in (
                    "bk7258_control_pair",
                    "bk7258_provision_tls",
                    "bk7258_provision_pair",
                    "bk7258_provision_claim",
                    "bk7258_provision_scan",
                )
            ]
            tls_flags += [
                str(build / "library" / ("lib" + name + ".a"))
                for name in ("mbedtls", "mbedx509", "mbedcrypto")
            ]
        subprocess.run(
            [
                "cc",
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pthread",
                *(
                    [
                        "-DTEST_SELECTION_PRODUCT",
                        "-I",
                        str(ROOT.parent / "apps/crypto/mbedtls/mbedtls/include"),
                    ]
                    if sys.argv[1] == "selection-wire-product"
                    else []
                ),
                *(
                    ["-DTEST_PHONE_SELECTION"]
                    if sys.argv[1].startswith("selection-wire-phone-")
                    else []
                ),
                "-I",
                str(temp),
                "-I",
                str(HERE / "mocks"),
                "-I",
                str(APP),
                str(HERE / "test_pack_trial.c"),
                str(APP / "bk7258_display_store.c"),
                str(APP / "bk7258_display_pack.c"),
                str(APP / "bk7258_media_volume.c"),
                str(APP / "bk7258_display_trial_control.c"),
                str(APP / "bk7258_display_selection_control.c"),
                str(APP / "bk7258_control_session.c"),
                *tls_flags,
                "-Wl,--wrap=write,--wrap=fsync,--wrap=readdir",
                "-o",
                str(temp / "test"),
            ],
            check=True,
        )
        if sys.argv[1].startswith("web-display-tls-"):
            command = [
                str(temp / "test"),
                str(HERE / "build/shaniu-default-v1.bkep"),
                str(fixture),
                "--selection-tls-peer",
            ]
            return subprocess.run(
                [
                    sys.executable,
                    str(HERE / "test_workbench_web_display.py"),
                    "DisplayHttpTest.test_"
                    + sys.argv[1].removeprefix("web-display-tls-"),
                ],
                timeout=40,
                env=dict(
                    os.environ,
                    SHANIU_WEB_DISPLAY_PEER=json.dumps(command),
                    SHANIU_TEST_CERT=str(temp / "cert.pem"),
                    SHANIU_TEST_KEY=str(temp / "key.pem"),
                ),
            ).returncode
        if tls:
            import time

            trial = sys.argv[1].startswith("android-trial-tls-")
            method = (
                {
                    "expiry": "packExpiryRestoresDefaultWithoutPersistence",
                    "cancel": "packCancellationWaitsForActualRestore",
                    "missing": "missingInstalledPackFailsWithoutFallback",
                    "supersede": "newDefaultSupersedesOldTrialAndLateExpiry",
                }
                if trial
                else {
                    "save": "authenticatedNativeSavePreservesAckAndRenderBoundary",
                    "cancel": "confirmedNativeCancelDoesNotWriteOrRender",
                    "recovery": "nativeReleaseFailureRemainsUnknownAfterRecovery",
                }
            )[
                sys.argv[1].removeprefix(
                    "android-trial-tls-" if trial else "android-default-tls-"
                )
            ]
            command = [
                str(temp / "test"),
                str(HERE / "build/shaniu-default-v1.bkep"),
                str(fixture),
                "--selection-tls-peer",
            ]
            app = ROOT / "android/shaniu-companion"
            name = "com.shaniu.companion.provision." + (
                "ExpressionTrialNativeTlsTest"
                if trial
                else "DefaultSelectionNativeTlsTest"
            )
            start = time.time()
            result = subprocess.run(
                [
                    "./gradlew",
                    ":app:testDebugUnitTest",
                    "--offline",
                    "--rerun-tasks",
                    "--tests",
                    name + "." + method,
                ],
                cwd=app,
                timeout=180,
                env=dict(
                    os.environ,
                    SHANIU_SELECTION_TLS_PEER="\n".join(command),
                    SHANIU_TEST_CERT=str(temp / "cert.pem"),
                    SHANIU_TEST_KEY=str(temp / "key.pem"),
                ),
            )
            report = (
                app
                / "app/build/test-results/testDebugUnitTest"
                / ("TEST-" + name + ".xml")
            )
            if not report.exists() or report.stat().st_mtime < start:
                raise RuntimeError("missing/stale native TLS JUnit report")
            doc = ET.parse(report).getroot()
            cases = doc.findall("testcase")
            if (
                doc.tag != "testsuite"
                or doc.get("name") != name
                or doc.get("tests") != "1"
                or doc.get("skipped") != "0"
                or len(cases) != 1
                or cases[0].get("name") != method
                or cases[0].get("classname") != name
                or cases[0].find("skipped") is not None
            ):
                raise RuntimeError("incomplete native TLS JUnit collection")
            print("NATIVE_TLS_JUNIT " + report.read_text(), flush=True)
            if result.returncode:
                import shutil

                failure = (
                    ROOT / "out/tls-failures" / ("selection-" + str(time.time_ns()))
                )
                failure.mkdir(parents=True, mode=0o700)
                for name in ("cert.pem", "key.pem"):
                    shutil.copyfile(temp / name, failure / name)
                    (failure / name).chmod(0o600)
                shutil.copyfile(report, failure / "junit.xml")
                print("Synthetic failing inputs retained: " + str(failure), flush=True)
            failed = (
                cases[0].find("failure") is not None
                or cases[0].find("error") is not None
            )
            if result.returncode and not failed:
                raise RuntimeError(
                    "Gradle failed without a collected assertion failure"
                )
            return 1 if failed else 0
        if sys.argv[1].startswith("pc-"):
            command = [
                str(temp / "test"),
                str(HERE / "build/shaniu-default-v1.bkep"),
                str(fixture),
                (
                    "--selection-peer"
                    if sys.argv[1].startswith("pc-default-")
                    else "--peer"
                ),
            ]
            result = subprocess.run(
                [
                    sys.executable,
                    str(
                        HERE
                        / (
                            "test_workbench_selection.py"
                            if sys.argv[1].startswith("pc-default-")
                            else "test_workbench_trial.py"
                        )
                    ),
                    (
                        "NativeTest.test_" + sys.argv[1].removeprefix("pc-default-")
                        if sys.argv[1].startswith("pc-default-")
                        else "PackWireTest.test_" + sys.argv[1].removeprefix("pc-")
                    ),
                ],
                env=dict(os.environ, SHANIU_PACK_TEST_PEER=json.dumps(command)),
                timeout=20,
            )
            return result.returncode
        result = subprocess.run(
            [
                str(temp / "test"),
                str(HERE / "build/shaniu-default-v1.bkep"),
                str(fixture),
                sys.argv[1],
            ],
            timeout=20,
        )
        return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        RuntimeError,
        subprocess.SubprocessError,
        OSError,
        ET.ParseError,
        KeyError,
    ) as e:
        print("SETUP_ERROR:", e, file=sys.stderr)
        raise SystemExit(2)
