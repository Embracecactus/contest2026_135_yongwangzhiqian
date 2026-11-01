# SPDX-License-Identifier: Apache-2.0
"""Package owner-signed runtime Skills; never creates a signing identity."""

from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from . import trust

PROFILES = {
    "focus-coach": {"focus_timer", "device_status", "reply_expression"},
    "device-check": {"device_status", "device_motion", "reply_expression"},
    "object-guide": {"camera_capture", "device_status", "reply_expression"},
}


def add_arguments(parser):
    commands = parser.add_subparsers(dest="skill_command", required=True)
    pack = commands.add_parser("pack", help="sign a bounded Skill with an explicit key")
    pack.add_argument("--manifest", type=Path, required=True)
    pack.add_argument("--signing-key", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True, help="new output directory")
    pack.add_argument("--openssl", type=Path)


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate manifest field")
        result[key] = value
    return result


def validate(raw: bytes):
    if not 0 < len(raw) <= 2048 or b"\0" in raw:
        raise ValueError("Skill manifest must contain 1..2048 UTF-8 bytes")
    value = json.loads(raw.decode("utf-8"), object_pairs_hook=_object)
    if not isinstance(value, dict) or set(value) != {
        "id",
        "name",
        "profile",
        "triggers",
        "tools",
        "permissions",
        "body",
    }:
        raise ValueError("unexpected Skill manifest fields")
    if not isinstance(value["id"], str) or not re.fullmatch(
        r"[a-z0-9][a-z0-9-]{0,30}", value["id"]
    ):
        raise ValueError("invalid Skill ID")
    if (
        value["id"] in PROFILES
        or not isinstance(value["profile"], str)
        or value["profile"] not in PROFILES
    ):
        raise ValueError("Skill must use a new ID and one existing permission profile")
    for key, limit in (("name", 63), ("body", 512)):
        text = value[key]
        if (
            not isinstance(text, str)
            or not text
            or len(text.encode("utf-8")) > limit
            or any(ord(c) < 32 and not (key == "body" and c in "\n\t") for c in text)
        ):
            raise ValueError(f"invalid Skill {key}")
    triggers = value["triggers"]
    if (
        not isinstance(triggers, list)
        or len(triggers) != 1
        or not isinstance(triggers[0], str)
    ):
        raise ValueError("exactly one trigger is required")
    trigger = triggers[0]
    if (
        not trigger
        or len(trigger.encode("utf-8")) > 63
        or any(ord(c) < 32 or c in ":：" for c in trigger)
        or trigger in {"专注助手", "设备体检", "识物解说"}
    ):
        raise ValueError("invalid or reserved trigger")
    tools = value["tools"]
    permissions = value["permissions"]
    if (
        not isinstance(tools, list)
        or not isinstance(permissions, list)
        or not all(isinstance(x, str) for x in tools + permissions)
    ):
        raise ValueError("tools and permissions must be string arrays")
    if (
        len(set(tools)) != len(tools)
        or len(set(permissions)) != len(permissions)
        or set(tools) != set(permissions)
        or not set(tools) <= PROFILES[value["profile"]]
    ):
        raise ValueError("permissions must match a tool subset of the selected profile")
    return value


def run(args):
    raw = args.manifest.read_bytes()
    value = validate(raw)
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("output directory already exists")
    openssl = (
        args.openssl or Path(shutil.which("openssl") or "/usr/bin/openssl")
    ).resolve()
    key = trust._regular(args.signing_key, "Skill signing key")
    public = trust._public_der(key, openssl, private=True)
    # Canonical SubjectPublicKeyInfo, id-ecPublicKey and prime256v1 OIDs.
    if not public.startswith(
        bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d03010703420004")
    ):
        raise ValueError("Skill signer must use P-256")
    with tempfile.TemporaryDirectory(prefix="bk7258-skill-") as temp:
        manifest = Path(temp) / "manifest.json"
        signature = Path(temp) / "signature.der"
        manifest.write_bytes(raw)
        trust._run(
            [
                str(openssl),
                "dgst",
                "-sha256",
                "-sign",
                str(key),
                "-out",
                str(signature),
                str(manifest),
            ],
            "Skill signing",
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        signed = signature.read_bytes()
    bundle = json.dumps(
        {
            "manifest": base64.b64encode(raw).decode(),
            "signature": base64.b64encode(signed).decode(),
        },
        separators=(",", ":"),
    )
    if len(bundle) > 3800:
        raise ValueError("signed Skill exceeds the device transfer limit")
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "manifest.json").write_bytes(raw)
    (args.output / "publisher.der").write_bytes(public)
    (args.output / "package.json").write_text(bundle + "\n", encoding="utf-8")
    return {
        "id": value["id"],
        "output": str(args.output),
        "public_key_sha256": hashlib.sha256(public).hexdigest(),
        "manifest_sha256": hashlib.sha256(raw).hexdigest(),
        "installed": False,
    }
