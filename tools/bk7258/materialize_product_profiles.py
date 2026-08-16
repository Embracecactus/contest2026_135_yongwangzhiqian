#!/usr/bin/env python3
"""Canonical module name for the BK7258 legacy profile adapter.

The old filename is retained only as a compatibility import for frozen host
checks.  Product execution and framework code should import this module.
"""

from materialize_aidk_profiles import (  # noqa: F401
    BOARD_SYMBOLS,
    CP_CONTRACT,
    FORBIDDEN,
    OVERLAYS,
    PAIR,
    COMPAT,
    VALIDATION_SUITE_COMPAT,
    materialize,
    materialize_plan,
    materialized_hashes,
    main as _legacy_main,
    overlay_descriptor,
    overlay_sha256,
    profile_name,
    seed_record,
)

__all__ = [
    "BOARD_SYMBOLS", "CP_CONTRACT", "FORBIDDEN", "OVERLAYS", "PAIR",
    "COMPAT", "VALIDATION_SUITE_COMPAT", "materialize", "materialize_plan", "materialized_hashes",
    "overlay_descriptor",
    "overlay_sha256", "profile_name", "seed_record",
]


if __name__ == "__main__":
    raise SystemExit(_legacy_main())
