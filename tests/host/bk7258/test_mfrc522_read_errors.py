#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Shared source-extraction helpers for host fault-frame checks."""

from __future__ import annotations

from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]


def extract_function(source: str, marker: str) -> str:
    start = source.rindex(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]
