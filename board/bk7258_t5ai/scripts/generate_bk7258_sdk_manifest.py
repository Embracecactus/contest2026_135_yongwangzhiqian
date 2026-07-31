#!/usr/bin/env python3
"""Generate a deterministic SHA-256 manifest for one BK7258 role bundle."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import tempfile


REQUIRED_DIRS = ("include", "config", "libs")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    bundle_dir = args.bundle_dir.resolve()
    for name in REQUIRED_DIRS:
        path = bundle_dir / name
        if not path.is_dir():
            parser.error(f"required directory is missing: {path}")

    files: list[Path] = []
    for name in REQUIRED_DIRS:
        root = bundle_dir / name
        for path in root.rglob("*"):
            if path.is_symlink():
                parser.error(f"symbolic links are not allowed in a bundle: {path}")
            if path.is_file():
                files.append(path)

    files.sort(key=lambda path: path.relative_to(bundle_dir).as_posix())
    if not files:
        parser.error(f"bundle contains no regular files: {bundle_dir}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(
        prefix=f".{args.output.name}.", dir=args.output.parent, text=True
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            for path in files:
                relative = path.relative_to(bundle_dir).as_posix()
                stream.write(f"{sha256(path)}  {relative}\n")
        os.replace(temp_name, args.output)
    except BaseException:
        Path(temp_name).unlink(missing_ok=True)
        raise

    print(f"wrote {len(files)} entries: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
