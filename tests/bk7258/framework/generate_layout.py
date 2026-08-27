#!/usr/bin/env python3
"""Generate the host-test partition ABI from the authoritative CSV."""

from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: generate_layout.py TOOLS_ROOT PARTITION_CSV OUTPUT_DIR",
              file=sys.stderr)
        return 2

    tools_root = Path(sys.argv[1]).resolve()
    partition_csv = Path(sys.argv[2]).resolve()
    output_dir = Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(tools_root))

    from _lib import layout  # pylint: disable=import-outside-toplevel

    generated = layout.emit(layout.load(partition_csv), output_dir)
    print(f"host layout={generated.header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
