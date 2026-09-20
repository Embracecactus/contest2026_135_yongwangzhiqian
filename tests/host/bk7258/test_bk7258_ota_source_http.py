#!/usr/bin/env python3
"""Run the real HTTP source against a loopback signed-catalog fixture.

The package and generated public root are inputs.  Neither firmware nor any
private signing/TLS key is retained in the repository.
"""
import argparse
import hashlib
import http.server
import json
import pathlib
import shutil
import subprocess
import tempfile
import threading
import zipfile
import ssl

REPO = pathlib.Path(__file__).resolve().parents[3]
OPENVELA = REPO.parent
DEFAULT_PACKAGE = (
    OPENVELA
    / "out/shaniu-product-415/package/package/firmware-aidk_ai_toy-v18.6.351+415-ota.bkpack"
)
DEFAULT_ROOT = (
    OPENVELA
    / "out/bk7258-plan-validation/out/bk7258/aidk_ai_toy/openvela_cp__openvela_ap/bk7258-e66ee7b7206dc724/releases/mcuboot/trust/bk7258_ota_catalog_public_key.c"
)


class Handler(http.server.BaseHTTPRequestHandler):
    catalog = b""
    signature = b""

    def do_GET(self):
        if self.path.endswith("/catalog.json"):
            data = self.catalog
        elif self.path.endswith("/catalog.sig"):
            data = bytearray(self.signature)
            if self.path.startswith("/badsig/"):
                data[0] ^= 1
            data = bytes(data)
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt, *args):
        del fmt, args


def headers(include, layout):
    (include / "nuttx/net").mkdir(parents=True)
    (include / "arch/chip").mkdir(parents=True)
    (include / "netutils").mkdir()
    (include / "nuttx/config.h").write_text(
        '#define CONFIG_BK7258_OTA_SOURCE_HTTP 1\n#define CONFIG_BK7258_OTA_SOURCE_HTTP_PLAINTEXT 1\n#define CONFIG_BK7258_OTA_MANAGER 1\n#define CONFIG_ARCH_BOARD_CUSTOM_NAME "aidk_ai_toy"\n'
    )
    (include / "nuttx/compiler.h").write_text(
        "#define FAR\n#define CODE\n#define noreturn_function __attribute__((noreturn))\n"
    )
    (include / "nuttx/kmalloc.h").write_text(
        "#include <stdlib.h>\n#define kmm_malloc malloc\n#define kmm_zalloc(n) calloc(1,n)\n#define kmm_free free\n"
    )
    (include / "nuttx/clock.h").write_text("#define MSEC2TICK(ms) (ms)\n")
    (include / "nuttx/semaphore.h").write_text(
        "typedef int sem_t; static inline int nxsem_init(sem_t*s,int p,unsigned int v){(void)s;(void)p;(void)v;return 0;} static inline int nxsem_destroy(sem_t*s){(void)s;return 0;} static inline int nxsem_tickwait_uninterruptible(sem_t*s,unsigned int t){(void)s;(void)t;return -1;}\n"
    )
    (include / "poll.h").write_text(
        "#ifndef OTA_HOST_POLL_H\n#define OTA_HOST_POLL_H\n#define POLLOUT 4\nstruct pollfd {int fd; short events; short revents; void *arg; void (*cb)(void *);}; static inline void poll_default_cb(void *p){(void)p;}\n#endif\n"
    )
    (include / "nuttx/net/net.h").write_text(
        "#include <stdbool.h>\n#include <sys/socket.h>\n#include <sys/types.h>\n#include <sys/ioctl.h>\n#include <poll.h>\nstruct socket {int fd;};\nint psock_socket(int,int,int,struct socket*); int psock_close(struct socket*); int psock_connect(struct socket*,const struct sockaddr*,socklen_t); ssize_t psock_send(struct socket*,const void*,size_t,int); ssize_t psock_recv(struct socket*,void*,size_t,int); int psock_setsockopt(struct socket*,int,int,const void*,socklen_t); int psock_getsockopt(struct socket*,int,int,void*,socklen_t*); int psock_ioctl(struct socket*,int,...); int psock_shutdown(struct socket*,int); int psock_poll(struct socket*,struct pollfd*,bool);\n"
    )
    (include / "netutils/cJSON.h").write_text("#include <cJSON.h>\n")
    target = include / "arch/chip/bk7258_image_layout.h"
    shutil.copy2(layout, target)
    with target.open("a") as out:
        out.write(
            "\n#define BK7258_CP_RAW_PHYSICAL_SIZE BK7258_ARTIFACT_CP_SIZE\n#define BK7258_AP_RAW_PHYSICAL_SIZE BK7258_ARTIFACT_AP_SIZE\n"
        )
    shutil.copy2(
        REPO / "chips/bk7258/include/bk7258_ota_source_http.h",
        include / "arch/chip/bk7258_ota_source_http.h",
    )


def package_manifest(package):
    return package.parents[2] / "package/evidence/build-manifest.json"


def package_counter(catalog):
    try:
        value = json.loads(catalog)["security_counter"]
    except (json.JSONDecodeError, KeyError, TypeError) as error:
        raise SystemExit(f"catalog security_counter unavailable: {error}") from error
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not 0 < value <= 0xFFFFFFFF
    ):
        raise SystemExit("catalog security_counter is not a positive uint32")
    return value


def manifest_layout(package, public_root):
    manifest = package_manifest(package)
    try:
        identity = json.loads(manifest.read_text())["roles"]["ap"]["build_identity"]
    except (OSError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise SystemExit(f"package build manifest unavailable: {error}") from error
    return (
        public_root.parents[3]
        / "roles/mcuboot/ap"
        / identity
        / "generated/bk7258_partitions.h"
    )


def run(command, timeout):
    try:
        subprocess.run(command, check=True, timeout=timeout)
    except FileNotFoundError as error:
        raise SystemExit(f"host dependency unavailable: {error.filename}") from error
    except subprocess.TimeoutExpired as error:
        raise SystemExit(
            f"host test command timed out after {timeout}s: {error.cmd[0]}"
        ) from error


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", type=pathlib.Path, default=DEFAULT_PACKAGE)
    parser.add_argument("--public-root", type=pathlib.Path, default=DEFAULT_ROOT)
    parser.add_argument("--layout", type=pathlib.Path)
    parser.add_argument("--tls", action="store_true")
    args = parser.parse_args()
    layout = args.layout or manifest_layout(args.package, args.public_root)
    if not all(path.is_file() for path in (args.package, args.public_root, layout)):
        raise SystemExit(
            "package, public root, or manifest-derived layout missing; pass --layout to override"
        )
    with zipfile.ZipFile(args.package) as package:
        Handler.catalog = package.read("catalog.json")
        Handler.signature = package.read("catalog.sig")
    counter = package_counter(Handler.catalog)
    with tempfile.TemporaryDirectory(prefix="bk7258-ota-http-") as name:
        work = pathlib.Path(name)
        include = work / "include"
        headers(include, layout)
        key, cert = work / "server.key", work / "server.pem"
        bad_key, bad_cert = work / "bad.key", work / "bad.pem"
        try:
            subprocess.run(
                [
                    "openssl",
                    "req",
                    "-x509",
                    "-newkey",
                    "ec",
                    "-pkeyopt",
                    "ec_paramgen_curve:prime256v1",
                    "-nodes",
                    "-keyout",
                    str(key),
                    "-out",
                    str(cert),
                    "-days",
                    "1",
                    "-subj",
                    "/CN=shaniu-update.local",
                    "-addext",
                    "subjectAltName=DNS:shaniu-update.local",
                ],
                check=True,
                timeout=15,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if args.tls:
                subprocess.run(
                    [
                        "openssl",
                        "req",
                        "-x509",
                        "-newkey",
                        "ec",
                        "-pkeyopt",
                        "ec_paramgen_curve:prime256v1",
                        "-nodes",
                        "-keyout",
                        str(bad_key),
                        "-out",
                        str(bad_cert),
                        "-days",
                        "1",
                        "-subj",
                        "/CN=untrusted.local",
                    ],
                    check=True,
                    timeout=15,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
        except FileNotFoundError as error:
            raise SystemExit("host dependency unavailable: openssl") from error
        except subprocess.TimeoutExpired as error:
            raise SystemExit("temporary certificate generation timed out") from error
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        if args.tls:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.maximum_version = ssl.TLSVersion.TLSv1_2
            context.set_ciphers("ECDHE-ECDSA-AES128-GCM-SHA256")
            context.load_cert_chain(cert, key)
            server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            libs = REPO / "tests/host/bk7258/build/voice-ota-store-mbedtls/library"
            if not all(
                (libs / name).is_file()
                for name in ("libmbedtls.a", "libmbedx509.a", "libmbedcrypto.a")
            ):
                raise SystemExit(
                    "MbedTLS host libraries unavailable; run make run-voice-ota-store first"
                )
            command = [
                "gcc",
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O2",
                "-pthread",
                "-I",
                str(include),
                "-I",
                str(REPO / "tests/host/bk7258/mocks"),
                "-I",
                str(REPO / "chips/bk7258/include"),
                "-I",
                str(OPENVELA / "apps/crypto/mbedtls/mbedtls/include"),
                "-I",
                str(OPENVELA / "apps/netutils/cjson/cJSON"),
                str(REPO / "tests/host/bk7258/test_bk7258_ota_source_http.c"),
                str(REPO / "chips/bk7258/ap/bk7258_ota_source_http.c"),
                str(REPO / "chips/bk7258/ap/bk7258_ota_catalog.c"),
                str(OPENVELA / "apps/netutils/cjson/cJSON/cJSON.c"),
                str(args.public_root),
                "-o",
                str(work / "test"),
                str(libs / "libmbedtls.a"),
                str(libs / "libmbedx509.a"),
                str(libs / "libmbedcrypto.a"),
            ]
            run(command, 120)
            test_command = [
                str(work / "test"),
                str(server.server_port),
                str(cert),
                hashlib.sha256(Handler.catalog).hexdigest(),
                str(counter),
            ]
            if args.tls:
                test_command.extend(("tls", str(bad_cert)))
            run(test_command, 30)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


if __name__ == "__main__":
    main()
