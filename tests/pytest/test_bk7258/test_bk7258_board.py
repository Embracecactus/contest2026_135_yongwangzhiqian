#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""BK7258 board checks executed by OpenVela's official pytest fixture."""

import pytest


COMMON_CP_MARKERS = (
    "BK7258 SYSINIT PASS",
    "BK7258 FINALINIT PASS",
    "BK7258 RCS PASS",
)

BOARD_CP_MARKERS = {
    "t5_board": ("BPSR BOOT PASS",),
    "t5ai_core": (),
    "aidk_ai_toy": ("BPSR BOOT PASS",),
}

BOARD_PERIPHERAL_MARKERS = {
    "t5_board": (
        "BDAC BOOT PASS board=T5-Board",
        "BMIC BOOT PASS board=T5-Board",
    ),
    "t5ai_core": (
        "BMIC BOOT PASS board=T5AI-Core",
    ),
    "aidk_ai_toy": (
        "BDAC BOOT PASS board=AIDK AI Toy",
        "BMIC BOOT PASS board=AIDK AI Toy",
        "AIDK LCD BOOT PASS",
        "AIDK ETA4322 registered: /dev/bat0",
    ),
}


def _require_supported_board(p):
    if p.board not in BOARD_PERIPHERAL_MARKERS:
        pytest.fail(f"unsupported BK7258 board: {p.board}")


def _expect_dmesg_markers(p, markers):
    for marker in markers:
        result = p.sendCommand("dmesg", marker, p.PROMPT, timeout=20)
        assert result == 0, f"missing boot marker: {marker}"


@pytest.fixture(scope="module", autouse=True)
def verify_bk7258_board_identity(p):
    """Reject a mismatched -B value before applying board-specific checks."""

    _require_supported_board(p)
    result = p.sendCommand(
        "cmocka_bk7258_board_test",
        f"BK7258_TEST_BOARD={p.board}",
        r"\[  PASSED  \]",
        p.PROMPT,
        timeout=30,
    )
    assert result == 0


def test_bk7258_console_round_trip(p):
    marker = f"BK7258_PYTEST_CONSOLE_{p.board}"
    result = p.sendCommand(f"echo {marker}", marker, p.PROMPT, timeout=10)
    assert result == 0


def test_bk7258_cp_init_contract(p):
    _expect_dmesg_markers(p, COMMON_CP_MARKERS + BOARD_CP_MARKERS[p.board])


def test_bk7258_peripheral_boot_contract(p):
    _expect_dmesg_markers(p, BOARD_PERIPHERAL_MARKERS[p.board])
