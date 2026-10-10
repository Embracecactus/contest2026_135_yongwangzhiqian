# SPDX-License-Identifier: Apache-2.0
"""Offline PC request/response envelopes. OAEP encrypts; it does not identify
its sender. The user must obtain the confirmation pin from the trusted phone,
not from this response. The live device still authenticates every PC command.
"""
import hashlib
import hmac
import os
from pathlib import Path
import re
import ssl
import struct
import time

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from . import workbench_profile as profile

TTL_MS = 600000
LABEL = b"shaniu-pc-pair-v1"


class PairingError(ValueError):
    pass


def _now(value):
    value = int(time.time() * 1000) if value is None else value
    if type(value) is not int or not 0 <= value <= (1 << 63) - 1 - TTL_MS:
        raise ValueError()
    return value


def _request(data, now):
    if not 60 < len(data) <= 1024:
        raise ValueError()
    magic, caps, created, expires, client, nonce, size = struct.unpack(
        ">4sIQQ16s16sI", data[:60]
    )
    if (
        magic != b"SPQ1"
        or not 1 <= caps <= 31
        or expires - created != TTL_MS
        or not created <= now < expires
        or not any(client)
        or not any(nonce)
        or size != len(data) - 60
    ):
        raise ValueError()
    public = serialization.load_der_public_key(bytes(data[60:]))
    if (
        not isinstance(public, rsa.RSAPublicKey)
        or public.key_size != 3072
        or public.public_numbers().e != 65537
    ):
        raise ValueError()
    if (
        public.public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
        != data[60:]
    ):
        raise ValueError()
    return caps, public


def start(request_path, pending_path, capabilities, *, now_ms=None):
    plain = bytearray()
    try:
        now = _now(now_ms)
        if type(capabilities) is not int or not 1 <= capabilities <= 31:
            raise ValueError()
        request_path, pending_path = Path(request_path), Path(pending_path)
        if (
            request_path.resolve() == pending_path.resolve()
            or request_path.exists()
            or pending_path.exists()
        ):
            raise ValueError()
        # Encryption-only ephemeral key, not a firmware/device signing identity.
        key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        public = key.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
        request = (
            struct.pack(
                ">4sIQQ16s16sI",
                b"SPQ1",
                capabilities,
                now,
                now + TTL_MS,
                os.urandom(16),
                os.urandom(16),
                len(public),
            )
            + public
        )
        _request(request, now)
        private = key.private_bytes(
            serialization.Encoding.DER,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
        plain.extend(struct.pack(">4sII", b"SPX1", len(request), len(private)))
        plain.extend(request)
        plain.extend(private)
        sealed = profile._protect(plain)
        profile._publish_new(
            pending_path, b"SPP1" + struct.pack(">I", len(sealed)) + sealed
        )
        restored = profile._protect(
            profile._read(pending_path, magic=b"SPP1"), decrypt=True
        )
        try:
            if not hmac.compare_digest(restored, plain):
                raise ValueError()
        finally:
            restored[:] = bytes(len(restored))
        # Publish only after protected readback; failures never reveal PKCS8.
        profile._publish_new(request_path, request)
        return dict(
            request_sha256=hashlib.sha256(request).hexdigest(),
            expires_at_ms=now + TTL_MS,
            device_authorization_verified=False,
        )
    except Exception:
        raise PairingError(
            "Pairing request not confirmed; existing outputs were not replaced"
        ) from None
    finally:
        plain[:] = bytes(len(plain))


def finish(pending_path, response, destination, expected_pin, *, now_ms=None):
    plain = bytearray()
    decoded = bytearray()
    key_bytes = bytearray()
    try:
        now = _now(now_ms)
        if not isinstance(expected_pin, str) or not re.fullmatch(
            "[0-9a-f]{64}", expected_pin
        ):
            raise ValueError()
        plain = profile._protect(
            profile._read(pending_path, magic=b"SPP1"), decrypt=True
        )
        if len(plain) < 12:
            raise ValueError()
        magic, request_size, private_size = struct.unpack(">4sII", plain[:12])
        if (
            magic != b"SPX1"
            or not 60 < request_size <= 1024
            or not 1 <= private_size <= 4096
            or len(plain) != 12 + request_size + private_size
        ):
            raise ValueError()
        request = bytes(plain[12 : 12 + request_size])
        caps, public = _request(request, now)
        private = serialization.load_der_private_key(
            bytes(plain[12 + request_size :]), password=None
        )
        if (
            not isinstance(private, rsa.RSAPrivateKey)
            or private.public_key().public_numbers() != public.public_numbers()
        ):
            raise ValueError()
        digest = hashlib.sha256(request).digest()
        if (
            not 424 < len(response) <= 8616
            or response[:4] != b"SPR1"
            or not hmac.compare_digest(response[4:36], digest)
        ):
            raise ValueError()
        size = struct.unpack(">I", response[36:40])[0]
        if not 1 <= size <= 8192 or len(response) != 40 + size + 384:
            raise ValueError()
        certificate = bytes(response[40 : 40 + size])
        pin = hashlib.sha256(certificate).digest()
        if not hmac.compare_digest(pin.hex(), expected_pin):
            raise ValueError()
        decoded = bytearray(
            private.decrypt(
                bytes(response[40 + size :]),
                padding.OAEP(
                    padding.MGF1(hashes.SHA256()), hashes.SHA256(), LABEL + digest
                ),
            )
        )
        if (
            len(decoded) != 104
            or decoded[:4] != b"SPK1"
            or not hmac.compare_digest(decoded[4:36], digest)
            or not hmac.compare_digest(decoded[36:68], pin)
            or struct.unpack(">I", decoded[100:104])[0] != caps
        ):
            raise ValueError()
        key_bytes = decoded[68:100]
        if not any(key_bytes):
            raise ValueError()
        pem = ssl.DER_cert_to_PEM_cert(certificate)
        profile.create(destination, pem, expected_pin, key_bytes)
        return dict(profile_saved=True, device_authorization_verified=False)
    except Exception:
        raise PairingError(
            "Pairing response rejected or profile save unconfirmed; no device operation was sent"
        ) from None
    finally:
        for data in (key_bytes, decoded, plain):
            data[:] = bytes(len(data))


def run(args):
    if args.port is not None or any(
        x is not None
        for x in (args.certificate, args.certificate_sha256, args.pc_key_file)
    ):
        raise PairingError(
            "Pairing is offline and cannot use plaintext credential options"
        )
    if args.operation == "pair-start":
        if (
            args.request is None
            or args.pending is None
            or not args.allow
            or args.profile is not None
            or args.response is not None
            or args.confirm_device_sha256 is not None
            or len(set(args.allow)) != len(args.allow)
        ):
            raise PairingError("Invalid pairing request arguments")
        bits = {"resources": 1, "scenes": 2, "tasks": 4, "diagnostics": 8, "camera": 16}
        return start(args.request, args.pending, sum(bits[x] for x in args.allow))
    if (
        args.pending is None
        or args.response is None
        or args.profile is None
        or args.confirm_device_sha256 is None
        or args.request is not None
        or args.allow is not None
    ):
        raise PairingError("Invalid pairing response arguments")
    from .workbench import _read_file

    return finish(
        args.pending,
        _read_file(args.response, 8616),
        args.profile,
        args.confirm_device_sha256,
    )
