# SPDX-License-Identifier: Apache-2.0
"""Ephemeral host-test identities with an explicit positive validity interval.

Only synthetic test callers use this module. It does not alter device identity,
trust anchors or validation policy. Fixed dates avoid making handshake tests
race the issuing process's current second; outside this interval preflight fails.
"""
from pathlib import Path
import secrets
import tempfile

NOT_BEFORE = "20240101000000Z"
NOT_AFTER = "20300101000000Z"


def issue(
    run,
    root,
    *,
    certificate="cert.pem",
    key="key.pem",
    common_name="localhost",
    issuer=None,
    is_ca=True,
):
    root = Path(root)
    cert, private = root / certificate, root / key
    with tempfile.TemporaryDirectory(prefix="test-cert-", dir=root) as directory:
        temp = Path(directory)
        csr = temp / "request.pem"
        (temp / "index").write_text("")
        (temp / "serial").write_text(secrets.token_hex(16) + "\n")
        usage = "digitalSignature,keyEncipherment" + (
            ",keyCertSign,cRLSign" if is_ca else ""
        )
        config = temp / "ca.cnf"
        config.write_text(
            f"""[ca]
default_ca = fixture
[fixture]
database = {temp / 'index'}
serial = {temp / 'serial'}
new_certs_dir = {temp}
default_md = sha256
policy = subject
x509_extensions = extension
unique_subject = no
[subject]
commonName = supplied
[extension]
basicConstraints = critical,CA:{'TRUE' if is_ca else 'FALSE'}
keyUsage = critical,{usage}
extendedKeyUsage = serverAuth,clientAuth
subjectAltName = DNS:localhost
"""
        )
        run(
            [
                "openssl",
                "req",
                "-new",
                "-newkey",
                "ec",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-nodes",
                "-keyout",
                private,
                "-out",
                csr,
                "-subj",
                "/CN=" + common_name,
            ]
        )
        if private.exists():
            private.chmod(0o600)
        signing = (
            ["-selfsign", "-keyfile", private]
            if issuer is None
            else ["-cert", issuer[0], "-keyfile", issuer[1]]
        )
        run(
            [
                "openssl",
                "ca",
                "-batch",
                "-notext",
                "-config",
                config,
                *signing,
                "-in",
                csr,
                "-out",
                cert,
                "-startdate",
                NOT_BEFORE,
                "-enddate",
                NOT_AFTER,
            ]
        )
        # A bad host clock/fixture is a setup error before any protocol stimulus.
        run(
            [
                "openssl",
                "verify",
                "-CAfile",
                cert if issuer is None else issuer[0],
                cert,
            ]
        )
    return cert, private
