#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real UART regression on an explicitly deployed per-board app pair.

Deployment/readback evidence must independently identify the installed image.
The host ELF hash below records the intended artifact, not a device attestation.
No reset, flash, fault injection or storage writes are performed by this test.
"""

import hashlib
import os
from pathlib import Path

import pytest


BOARDS = {
    "t5_board": "T5-Board",
    "t5ai_core": "T5AI-Core",
    "aidk_ai_toy": "AIDK AI Toy",
}


@pytest.fixture(scope="module")
def kernel_target(p):
    if os.environ.get("BK7258_KERNEL_HIL") != "1":
        pytest.skip("real kernel HIL not requested; not hardware acceptance")
    assert p.target in {"target", "module"}, "simulator is not physical acceptance"
    assert p.board in BOARDS, "an explicit supported physical board is required"
    elf = Path(os.environ["BK7258_KERNEL_HIL_ELF"])
    assert elf.is_file(), "record the exact deployed CP ELF"
    assert p.sendCommand("uname -a", r"arm " + p.board, p.PROMPT, timeout=30) == 0
    return p, hashlib.sha256(elf.read_bytes()).hexdigest()


def test_kernel_compat_live_tick_progress(kernel_target, record_property):
    p, digest = kernel_target
    record_property("board", p.board)
    record_property("intended_cp_elf_sha256", digest)
    record_property(
        "evidence_scope",
        "live UART ticks; deployment and peripheral acceptance separate",
    )
    previous = None
    samples = []
    for _ in range(10):
        assert (
            p.sendCommand(
                "apctl status",
                r"AP state=READY\([0-9]+\) error=0",
                r"CPU2 state=SCHEDULER_ONLINE\(8\) error=0 generation=[0-9]+ heartbeat=([0-9]+)",
                timeout=20,
            )
            == 0
        )
        cpu2_heartbeat = int(p.process.match.group(1))
        p.process.expect(
            r"IPI irq/wake cpu0=[0-9]+/[0-9]+ cpu1=([0-9]+)/[0-9]+", timeout=20
        )
        cpu2_ipi = int(p.process.match.group(1))
        p.process.expect(r"SMP SysTick cpu0/cpu1=([0-9]+)/[0-9]+", timeout=20)
        # BK7258 routes the periodic tick to AP's primary core. CPU2 is
        # evidenced by its heartbeat and real scheduler mailbox interrupts.
        current = (int(p.process.match.group(1)), cpu2_heartbeat, cpu2_ipi)
        p.process.expect(p.PROMPT, timeout=20)
        if previous is not None:
            assert all(
                0 < ((now - old) & 0xFFFFFFFF) < 0x80000000
                for now, old in zip(current, previous)
            ), "AP tick, CPU2 heartbeat or CPU2 scheduler IPI stalled"
        samples.append(current)
        previous = current
        assert p.sendCommand("sleep 1", p.PROMPT, timeout=10) == 0
    record_property("ap_tick_samples", repr(samples))
    assert p.sendCommand("ps", "PID", p.PROMPT, timeout=20) == 0
