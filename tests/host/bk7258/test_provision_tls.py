#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual product TLS over fragmented fake GATT, using workspace mbedTLS.

Named host gate: python3 tests/host/bk7258/test_provision_tls.py.
Builds crypto out of tree; successful runs remove the test-only identity.
Failures retain synthetic inputs under ignored out/ with restricted permissions.
MBEDTLS_SOURCE may select another checkout for upstream compatibility testing.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import time
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class ProvisionTlsTest(unittest.TestCase):
    def test_real_tls_fragmentation_and_teardown(self):
        source = Path(
            os.environ.get(
                "MBEDTLS_SOURCE", ROOT.parent / "apps/crypto/mbedtls/mbedtls"
            )
        )
        self.assertTrue((source / "CMakeLists.txt").is_file())
        with tempfile.TemporaryDirectory(prefix="bkprov-tls-test-") as directory:
            temp = Path(directory)
            (temp / "include/nuttx").mkdir(parents=True)
            (temp / "include/nuttx/config.h").write_text("")
            with (temp / "build.log").open("w+") as log:

                tape_sequence = 0

                def run(args, env=None):
                    nonlocal tape_sequence
                    tape = None
                    # Observe real randomness only in the synthetic C fixture.
                    # Peer subprocesses have separate lifetimes and are not taped.
                    if str(args[0]) == str(temp / "test"):
                        tape = temp / f"tls-random-{tape_sequence}.bin"
                        tape_sequence += 1
                        env = dict(os.environ if env is None else env)
                        env.pop("SHANIU_TLS_TAPE_REPLAY", None)
                        env["SHANIU_TLS_TAPE_RECORD"] = str(tape)
                    result = subprocess.run(
                        [str(a) for a in args], stdout=log, stderr=log, env=env
                    )
                    if result.returncode:
                        # Retain synthetic inputs for the first exact failure;
                        # never retry or erase its nonzero result.
                        failure_root = Path(
                            os.environ.get(
                                "SHANIU_TLS_FAILURE_DIR", ROOT / "out/tls-failures"
                            )
                        ) / str(time.time_ns())
                        failure_root.mkdir(parents=True, mode=0o700)
                        for name in ("cert.pem", "key.pem"):
                            if (temp / name).is_file():
                                shutil.copyfile(temp / name, failure_root / name)
                                (failure_root / name).chmod(0o600)
                        # Only explicit test modes and public/source hashes.
                        # Never serialize the process environment or private key.
                        context = os.environ if env is None else env
                        metadata = {
                            "exit_code": result.returncode,
                            "command": [
                                str(a).replace(str(temp), "<fixture>") for a in args
                            ],
                            "modes": {
                                key: context.get(key)
                                for key in ("SHANIU_TLS_STREAM", "SHANIU_TLS_SERIAL")
                            },
                            "source_sha256": {
                                name: hashlib.sha256(
                                    (ROOT / name).read_bytes()
                                ).hexdigest()
                                for name in (
                                    "tests/host/bk7258/test_provision_tls.c",
                                    "tests/host/bk7258/test_provision_tls.py",
                                    "tests/host/bk7258/tls_entropy_tape.c",
                                    "app/bk7258/bk7258_provision_tls.c",
                                )
                            },
                        }
                        if (temp / "cert.pem").is_file():
                            metadata["public_certificate_pem_sha256"] = hashlib.sha256(
                                (temp / "cert.pem").read_bytes()
                            ).hexdigest()
                        if tape is not None and tape.is_file():
                            retained = failure_root / "tls-random.bin"
                            shutil.copyfile(tape, retained)
                            retained.chmod(0o600)
                            metadata["synthetic_random_tape"] = {
                                "file": retained.name,
                                "sha256": hashlib.sha256(
                                    retained.read_bytes()
                                ).hexdigest(),
                                "bytes": retained.stat().st_size,
                                "replay_environment": "SHANIU_TLS_TAPE_REPLAY",
                                "scope": "C fixture RNG and time; not external Python peer",
                            }
                        (failure_root / "failure.json").write_text(
                            json.dumps(metadata, indent=2) + "\n"
                        )
                        log.flush()
                        shutil.copyfile(temp / "build.log", failure_root / "build.log")
                        log.seek(0)
                        self.fail(
                            log.read()[-6000:] + f"\nSynthetic inputs: {failure_root}"
                        )

                run(
                    [
                        sys.executable,
                        ROOT / "tests/host/bk7258/test_tls_entropy_tape.py",
                    ]
                )
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I",
                        temp / "include",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_owner.c",
                        ROOT / "app/bk7258/bk7258_provision_owner.c",
                        ROOT / "app/bk7258/bk7258_control_session.c",
                        ROOT / "app/bk7258/bk7258_provision_scan.c",
                        "-o",
                        temp / "owner",
                    ]
                )
                run([temp / "owner"])
                chip_include = temp / "include/arch/chip"
                chip_include.mkdir(parents=True)
                (chip_include / "bk7258_wifi.h").symlink_to(
                    ROOT / "chips/bk7258/include/bk7258_wifi.h"
                )
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-DCONFIG_BK7258_WIFI_VNET",
                        "-DCONFIG_BK7258_AP_CORE",
                        "-I",
                        temp / "include",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_network.c",
                        ROOT / "app/bk7258/bk7258_provision_network.c",
                        "-Wl,--wrap=clock_settime",
                        "-o",
                        temp / "network",
                    ]
                )
                run([temp / "network"])
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-pthread",
                        "-DFAR=",
                        "-DCONFIG_NETUTILS_NTPCLIENT_NUM_SAMPLES=3",
                        "-I",
                        ROOT / "tests/host/bk7258/mocks",
                        "-I",
                        ROOT.parent / "apps/include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_time.c",
                        ROOT / "app/bk7258/bk7258_provision_time.c",
                        "-Wl,--wrap=clock_gettime,--wrap=waitpid",
                        "-o",
                        temp / "time",
                    ]
                )
                run([temp / "time"])
                build = temp / "build"
                run(
                    [
                        "cmake",
                        "-S",
                        source,
                        "-B",
                        build,
                        "-DENABLE_PROGRAMS=OFF",
                        "-DENABLE_TESTING=OFF",
                        # Workspace mbedTLS has one PSA helper without a prior
                        # prototype. Keep its warning visible; product C below
                        # is still compiled with full -Werror.
                        "-DCMAKE_C_FLAGS=-Wno-error=missing-prototypes",
                        "-DCMAKE_BUILD_TYPE=Release",
                    ]
                )
                run(["cmake", "--build", build, "-j8"])
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_settings.c",
                        ROOT / "app/bk7258/bk7258_provision_settings.c",
                        ROOT / "app/bk7258/bk7258_cloud_config.c",
                        ROOT / "app/bk7258/bk7258_voice_config.c",
                        "-Wl,--wrap=clock_settime",
                        build / "library/libmbedx509.a",
                        build / "library/libmbedcrypto.a",
                        "-o",
                        temp / "settings",
                    ]
                )
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-DCONFIG_BK7258_WIFI_VNET",
                        "-DCONFIG_BK7258_AP_CORE",
                        "-I",
                        temp / "include",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_tls.c",
                        ROOT / "tests/host/bk7258/tls_entropy_tape.c",
                        "-Wl,--wrap=mbedtls_ctr_drbg_random,--wrap=time",
                        ROOT / "tests/host/bk7258/test_control_serial_peer.c",
                        ROOT / "app/bk7258/bk7258_control_serial.c",
                        ROOT / "app/bk7258/bk7258_pc_grants.c",
                        ROOT / "app/bk7258/bk7258_pc_control.c",
                        "-Wl,--wrap=open,--wrap=fsync",
                        ROOT / "app/bk7258/bk7258_provision_tls.c",
                        ROOT / "app/bk7258/bk7258_provision_claim.c",
                        ROOT / "app/bk7258/bk7258_provision_store.c",
                        ROOT / "app/bk7258/bk7258_provision_pair.c",
                        ROOT / "app/bk7258/bk7258_provision_scan.c",
                        ROOT / "app/bk7258/bk7258_control_pair.c",
                        ROOT / "app/bk7258/bk7258_control_session.c",
                        build / "library/libmbedtls.a",
                        build / "library/libmbedx509.a",
                        build / "library/libmbedcrypto.a",
                        "-o",
                        temp / "test",
                    ]
                )
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_tls_transport.c",
                        build / "library/libmbedtls.a",
                        build / "library/libmbedx509.a",
                        build / "library/libmbedcrypto.a",
                        "-o",
                        temp / "transport",
                    ]
                )
                run([temp / "transport"])
                if os.environ.get("SHANIU_ANDROID_INTEROP") == "1":
                    run(
                        [
                            "cc",
                            "-std=c11",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            "-I",
                            ROOT / "app/bk7258",
                            ROOT / "tests/host/bk7258/test_control_session.c",
                            ROOT / "app/bk7258/bk7258_control_session.c",
                            "-o",
                            temp / "control-peer",
                        ]
                    )
                    run(
                        [
                            ROOT / "android/shaniu-companion/gradlew",
                            "-p",
                            ROOT / "android/shaniu-companion",
                            ":app:testDebugUnitTest",
                            "--offline",
                        ],
                        env={
                            **os.environ,
                            "SHANIU_CONTROL_TLS_PEER": str(temp / "test"),
                            "SHANIU_CONTROL_PEER": str(temp / "control-peer"),
                        },
                    )
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_claim.c",
                        ROOT / "app/bk7258/bk7258_provision_claim.c",
                        ROOT / "app/bk7258/bk7258_provision_store.c",
                        "-Wl,--wrap=write,--wrap=fsync,--wrap=rename",
                        build / "library/libmbedcrypto.a",
                        "-o",
                        temp / "claim",
                    ]
                )
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_identity.c",
                        ROOT / "app/bk7258/bk7258_provision_identity.c",
                        build / "library/libmbedx509.a",
                        build / "library/libmbedcrypto.a",
                        "-o",
                        temp / "identity",
                    ]
                )
                private = temp / "private"
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-pthread",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_provision_storage.c",
                        ROOT / "app/bk7258/bk7258_provision_store.c",
                        ROOT / "app/bk7258/bk7258_provision_storage.c",
                        ROOT / "app/bk7258/bk7258_pc_grants.c",
                        "-Wl,--wrap=fsync,--wrap=rename",
                        build / "library/libmbedcrypto.a",
                        "-o",
                        temp / "storage",
                    ]
                )
                storage_root = temp / "storage-private"
                run([temp / "storage", storage_root])
                run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-pthread",
                        "-I",
                        ROOT / "tests/host/bk7258/mocks",
                        "-I",
                        source / "include",
                        "-I",
                        ROOT / "app/bk7258",
                        ROOT / "tests/host/bk7258/test_bk7258_voice_volume_store.c",
                        ROOT / "app/bk7258/bk7258_voice_volume_store.c",
                        ROOT / "app/bk7258/bk7258_provision_store.c",
                        "-Wl,--wrap=fsync,--wrap=rename",
                        build / "library/libmbedcrypto.a",
                        "-o",
                        temp / "volume-store",
                    ]
                )
                run([temp / "volume-store", temp / "voice-volume"])
                private.mkdir(mode=0o700)
                run([temp / "claim", private])
                # Vary both certificate and ephemeral handshake lengths.
                for index in range(20):
                    run(
                        [
                            "openssl",
                            "req",
                            "-x509",
                            "-newkey",
                            "ec",
                            "-pkeyopt",
                            "ec_paramgen_curve:P-256",
                            "-nodes",
                            "-keyout",
                            temp / "key.pem",
                            "-out",
                            temp / "cert.pem",
                            "-subj",
                            "/CN=localhost",
                            "-days",
                            "1",
                            "-addext",
                            "subjectAltName=DNS:localhost",
                        ]
                    )
                    pair_store = temp / f"pair-{index}"
                    pair_store.mkdir(mode=0o700)
                    run(
                        [temp / "test", temp / "cert.pem", temp / "key.pem", pair_store]
                    )
                    stream_store = temp / f"stream-{index}"
                    stream_store.mkdir(mode=0o700)
                    run(
                        [
                            temp / "test",
                            temp / "cert.pem",
                            temp / "key.pem",
                            stream_store,
                        ],
                        env={**os.environ, "SHANIU_TLS_STREAM": "1"},
                    )
                    serial_store = temp / f"serial-{index}"
                    serial_store.mkdir(mode=0o700)
                    run(
                        [
                            temp / "test",
                            temp / "cert.pem",
                            temp / "key.pem",
                            serial_store,
                        ],
                        env={
                            **os.environ,
                            "SHANIU_TLS_STREAM": "1",
                            "SHANIU_TLS_SERIAL": "1",
                        },
                    )
                    print(
                        f"TLS sample={index} GATT=PASS independent-stream=PASS "
                        f"SDC1-stream=PASS serial-SDC1=PASS PC-store=PASS PC-lease=PASS public_certificate_sha256="
                        f"{hashlib.sha256((temp / 'cert.pem').read_bytes()).hexdigest()}",
                        flush=True,
                    )
                    if index == 0:
                        run(
                            [
                                sys.executable,
                                ROOT / "tests/host/bk7258/test_workbench_client.py",
                                "--pc-peer",
                                temp / "test",
                                temp / "cert.pem",
                                temp / "key.pem",
                            ]
                        )
                        print(
                            "PC client interop: independent-principal=PASS owner-rejected=PASS STATUS/INFO=PASS",
                            flush=True,
                        )
                        run(
                            [
                                "openssl",
                                "x509",
                                "-in",
                                temp / "cert.pem",
                                "-outform",
                                "DER",
                                "-out",
                                temp / "cert.der",
                            ]
                        )
                        run(
                            [
                                "openssl",
                                "pkcs8",
                                "-topk8",
                                "-nocrypt",
                                "-in",
                                temp / "key.pem",
                                "-outform",
                                "DER",
                                "-out",
                                temp / "key.der",
                            ]
                        )
                        run([temp / "settings", temp / "cert.der", temp / "key.der"])
                        # Native device generation writes an EC SEC1 key,
                        # unlike the historical PC-supplied PKCS#8 fixture.
                        run(
                            [
                                "openssl",
                                "ec",
                                "-in",
                                temp / "key.pem",
                                "-outform",
                                "DER",
                                "-out",
                                temp / "native-key.der",
                            ]
                        )
                        run(
                            [
                                temp / "settings",
                                temp / "cert.der",
                                temp / "native-key.der",
                            ]
                        )
                        run(
                            [
                                "openssl",
                                "req",
                                "-x509",
                                "-key",
                                temp / "key.pem",
                                "-out",
                                temp / "identity.der",
                                "-outform",
                                "DER",
                                "-days",
                                "1",
                                "-subj",
                                "/CN=test-device",
                                "-addext",
                                "basicConstraints=critical,CA:FALSE",
                                "-addext",
                                "keyUsage=critical,digitalSignature",
                                "-addext",
                                "extendedKeyUsage=serverAuth,clientAuth",
                            ]
                        )
                        run(
                            [
                                temp / "identity",
                                temp / "identity.der",
                                temp / "key.der",
                                temp / "cert.der",
                            ]
                        )


if __name__ == "__main__":
    unittest.main()
