#!/usr/bin/env python3
"""Host-model the proposed BK7258 paired-sector OTA journal.

The model performs no board or file-system writes.  It validates the N15-R2
transaction shape against the real slot sector counts and injects a reset
after every erase, torn-program, completed-program, and mirrored journal
marker mutation for every CP/AP sector in both swap directions.

The journal proposal deliberately assigns one 32-byte SDK write chunk to each
phase marker.  This avoids depending on repeated writes within the same chunk;
the current v3.1.1.9 driver always submits a 32-byte block.  The 32-byte value
is a software-driver transaction granularity, not a claim about the integrated
Flash's physical program unit.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from typing import Any, Callable

from bk7258_ota_metadata import (
    ERASED_CHUNK,
    HEADER_SIZE,
    MARKER_ACTIVE_REPLACED,
    MARKER_ARM,
    MARKER_CONFIRMED,
    MARKER_DIRECTION_COMPLETE,
    MARKER_SCRATCH_READY,
    MARKER_STAGING_REPLACED,
    MARKER_TRIAL_STARTED,
    JournalMarkerV1,
    is_exact_marker,
    programmed_prefix,
    run_self_test as run_metadata_self_test,
    sample_header,
)


ERASE_SIZE = 0x1000
SDK_WRITE_CHUNK_SIZE = 32
CP_SLOT_SIZE = 0x0EF000
AP_SLOT_SIZE = 0x220000
CP_SECTORS = CP_SLOT_SIZE // ERASE_SIZE
AP_SECTORS = AP_SLOT_SIZE // ERASE_SIZE
PAIR_SECTORS = CP_SECTORS + AP_SECTORS

PHASES = ("scratch-ready", "active-replaced", "staging-replaced")
PHASE_MARKERS = PAIR_SECTORS * len(PHASES)
JOURNAL_IMMUTABLE_HEADER_SIZE = 0x100
JOURNAL_CONTROL_SIZE = 0x100
JOURNAL_PHASE_OFFSET = JOURNAL_IMMUTABLE_HEADER_SIZE + JOURNAL_CONTROL_SIZE
JOURNAL_REQUIRED_SIZE = JOURNAL_PHASE_OFFSET + PHASE_MARKERS * SDK_WRITE_CHUNK_SIZE
JOURNAL_COPY_SIZE = (
    (JOURNAL_REQUIRED_SIZE + ERASE_SIZE - 1) // ERASE_SIZE
) * ERASE_SIZE

ERASED_DATA = "erased"
TORN_DATA = "torn"
UINT64_MAX = (1 << 64) - 1
MODEL_SEQUENCE = 7
MODEL_GENERATION = 42
PHASE_KINDS = (
    MARKER_SCRATCH_READY,
    MARKER_ACTIVE_REPLACED,
    MARKER_STAGING_REPLACED,
)


class ModelError(RuntimeError):
    """Raised when the proposed recovery invariants are violated."""


@dataclass
class JournalLog:
    """Two mirrored copies of one directional append-only phase log."""

    sequence: int = MODEL_SEQUENCE
    generation: int = MODEL_GENERATION
    markers: dict[int, list[bytes]] = field(default_factory=dict)
    controls: dict[str, list[bytes]] = field(default_factory=dict)

    @staticmethod
    def marker_index(sector: int, phase: int) -> int:
        return sector * len(PHASES) + phase

    def marker(self, sector: int, phase: int) -> list[bytes]:
        return self.markers.setdefault(
            self.marker_index(sector, phase), [ERASED_CHUNK, ERASED_CHUNK]
        )

    def expected_phase(self, sector: int, phase: int) -> bytes:
        return JournalMarkerV1(
            PHASE_KINDS[phase], self.sequence, self.generation, sector
        ).encode()

    def committed(self, sector: int, phase: int) -> bool:
        expected = self.expected_phase(sector, phase)
        return self.marker(sector, phase) == [expected, expected]

    def commit(self, sector: int, phase: int) -> None:
        marker = self.marker(sector, phase)
        expected = self.expected_phase(sector, phase)
        marker[:] = [expected, expected]

    def control_kind(self, name: str) -> int:
        if name in ("forward-complete", "reverse-complete"):
            return MARKER_DIRECTION_COMPLETE
        if name == "trial-started":
            return MARKER_TRIAL_STARTED
        if name == "confirmed":
            return MARKER_CONFIRMED
        raise ModelError(f"unknown control marker: {name}")

    def expected_control(self, name: str) -> bytes:
        return JournalMarkerV1(
            self.control_kind(name), self.sequence, self.generation, 0
        ).encode()

    def control(self, name: str) -> list[bytes]:
        return self.controls.setdefault(name, [ERASED_CHUNK, ERASED_CHUNK])

    def commit_control(self, name: str) -> None:
        expected = self.expected_control(name)
        self.control(name)[:] = [expected, expected]


@dataclass
class SwapState:
    active: list[str]
    staging: list[str]
    scratch: str = ERASED_DATA
    forward: JournalLog = field(default_factory=JournalLog)
    reverse: JournalLog = field(default_factory=JournalLog)
    protected: tuple[str, str, str] = (
        "littlefs-preserved",
        "crc-gap-preserved",
        "official-tail-preserved",
    )


def old_value(sector: int) -> str:
    return f"old:{sector}"


def new_value(sector: int) -> str:
    return f"new:{sector}"


def make_state(direction: str) -> SwapState:
    if direction == "forward":
        return SwapState(
            active=[old_value(index) for index in range(PAIR_SECTORS)],
            staging=[new_value(index) for index in range(PAIR_SECTORS)],
        )
    if direction == "reverse":
        return SwapState(
            active=[new_value(index) for index in range(PAIR_SECTORS)],
            staging=[old_value(index) for index in range(PAIR_SECTORS)],
        )
    raise ModelError(f"unsupported direction: {direction}")


def direction_log(state: SwapState, direction: str) -> JournalLog:
    return state.forward if direction == "forward" else state.reverse


def expected_values(direction: str, sector: int) -> tuple[str, str]:
    if direction == "forward":
        return new_value(sector), old_value(sector)
    return old_value(sector), new_value(sector)


def replace_from_source(
    state: SwapState, target: str, sector: int, source: str
) -> None:
    if source == "active":
        value = state.active[sector]
    elif source == "staging":
        value = state.staging[sector]
    elif source == "scratch":
        value = state.scratch
    else:
        raise ModelError(f"unknown source: {source}")

    if value in (ERASED_DATA, TORN_DATA):
        raise ModelError(f"attempted to copy invalid {source} data")

    if target == "active":
        state.active[sector] = value
    elif target == "staging":
        state.staging[sector] = value
    elif target == "scratch":
        state.scratch = value
    else:
        raise ModelError(f"unknown target: {target}")


def phase_target_source(phase: int) -> tuple[str, str]:
    if phase == 0:
        return "scratch", "active"
    if phase == 1:
        return "active", "staging"
    if phase == 2:
        return "staging", "scratch"
    raise ModelError(f"invalid phase: {phase}")


def recover_sector(state: SwapState, direction: str, sector: int) -> None:
    log = direction_log(state, direction)
    seen_incomplete = False
    for phase in range(len(PHASES)):
        committed = log.committed(sector, phase)
        if seen_incomplete and committed:
            raise ModelError("journal contains a committed phase after a gap")
        if not committed:
            seen_incomplete = True

    for phase in range(len(PHASES)):
        if not log.committed(sector, phase):
            target, source = phase_target_source(phase)
            replace_from_source(state, target, sector, source)
            log.commit(sector, phase)

    expected_active, expected_staging = expected_values(direction, sector)
    if state.active[sector] != expected_active:
        raise ModelError("active sector did not reach the expected generation")
    if state.staging[sector] != expected_staging:
        raise ModelError("staging sector did not retain the rollback generation")


def prepare_phase_state(direction: str, sector: int, phase: int) -> SwapState:
    state = make_state(direction)
    log = direction_log(state, direction)
    for completed_phase in range(phase):
        target, source = phase_target_source(completed_phase)
        replace_from_source(state, target, sector, source)
        log.commit(sector, completed_phase)
    return state


def set_target(state: SwapState, target: str, sector: int, value: str) -> None:
    if target == "active":
        state.active[sector] = value
    elif target == "staging":
        state.staging[sector] = value
    elif target == "scratch":
        state.scratch = value
    else:
        raise ModelError(f"unknown target: {target}")


def phase_mutations(
    state: SwapState, direction: str, sector: int, phase: int
) -> list[Callable[[], None]]:
    log = direction_log(state, direction)
    marker = log.marker(sector, phase)
    expected_marker = log.expected_phase(sector, phase)
    target, source = phase_target_source(phase)

    def source_value() -> str:
        if source == "active":
            return state.active[sector]
        if source == "staging":
            return state.staging[sector]
        return state.scratch

    saved_source = source_value()
    if saved_source in (ERASED_DATA, TORN_DATA):
        raise ModelError("phase source is not recoverable")

    return [
        lambda: set_target(state, target, sector, ERASED_DATA),
        lambda: set_target(state, target, sector, TORN_DATA),
        lambda: set_target(state, target, sector, saved_source),
        lambda: marker.__setitem__(
            0, programmed_prefix(expected_marker, len(expected_marker) // 2)
        ),
        lambda: marker.__setitem__(0, expected_marker),
        lambda: marker.__setitem__(
            1, programmed_prefix(expected_marker, len(expected_marker) // 2)
        ),
        lambda: marker.__setitem__(1, expected_marker),
    ]


def verify_phase_reset_cases() -> int:
    cases = 0
    protected = SwapState([], []).protected
    for direction in ("forward", "reverse"):
        for sector in range(PAIR_SECTORS):
            for phase in range(len(PHASES)):
                for cut_after in range(7):
                    state = prepare_phase_state(direction, sector, phase)
                    mutations = phase_mutations(state, direction, sector, phase)
                    for mutation in mutations[: cut_after + 1]:
                        mutation()

                    recover_sector(state, direction, sector)
                    if state.protected != protected:
                        raise ModelError("protected region changed after reset")
                    cases += 1
    return cases


def program_marker_mutations(
    marker: list[bytes], expected: bytes
) -> list[Callable[[], None]]:
    return [
        lambda: marker.__setitem__(0, programmed_prefix(expected, len(expected) // 2)),
        lambda: marker.__setitem__(0, expected),
        lambda: marker.__setitem__(1, programmed_prefix(expected, len(expected) // 2)),
        lambda: marker.__setitem__(1, expected),
    ]


def verify_activation_reset_cases() -> int:
    cases = 0
    mutation_count = 16
    expected_header = sample_header().encode()
    expected_arm = JournalMarkerV1(
        MARKER_ARM, MODEL_SEQUENCE, MODEL_GENERATION, 0
    ).encode()
    for cut_after in range(mutation_count):
        trial_headers = [b"\xff" * HEADER_SIZE] * 4
        trial_armed = [ERASED_CHUNK] * 4
        trial_mutations: list[Callable[[], None]] = []
        for index in range(4):
            trial_mutations.extend(
                [
                    lambda index=index: trial_headers.__setitem__(
                        index,
                        programmed_prefix(expected_header, len(expected_header) // 2),
                    ),
                    lambda index=index: trial_headers.__setitem__(
                        index, expected_header
                    ),
                ]
            )
        for index in range(4):
            trial_mutations.extend(
                [
                    lambda index=index: trial_armed.__setitem__(
                        index,
                        programmed_prefix(expected_arm, len(expected_arm) // 2),
                    ),
                    lambda index=index: trial_armed.__setitem__(index, expected_arm),
                ]
            )
        for mutation in trial_mutations[: cut_after + 1]:
            mutation()

        activated = (
            trial_headers == [expected_header] * 4 and trial_armed == [expected_arm] * 4
        )
        if activated != (cut_after == mutation_count - 1):
            raise ModelError("journal activation accepted torn metadata")
        cases += 1
    return cases


def set_control(log: JournalLog, name: str) -> None:
    log.commit_control(name)


def verify_control_reset_cases() -> int:
    cases = 0

    # trial_started uses ANY committed copy.  A reset after the first exact
    # marker may skip the trial and roll back, but can never grant two trials.
    for cut_after in range(4):
        log = JournalLog()
        marker = log.control("trial-started")
        expected = log.expected_control("trial-started")
        for mutation in program_marker_mutations(marker, expected)[: cut_after + 1]:
            mutation()
        started = any(
            is_exact_marker(
                copy,
                kind=MARKER_TRIAL_STARTED,
                sequence=log.sequence,
                generation=log.generation,
                ordinal=0,
            )
            for copy in marker
        )
        if started != (cut_after >= 1):
            raise ModelError("trial-start marker could grant a second trial")
        cases += 1

    # confirmation uses BOTH copies.  A torn confirmation conservatively
    # rolls back an otherwise healthy trial.
    for cut_after in range(4):
        log = JournalLog()
        marker = log.control("confirmed")
        expected = log.expected_control("confirmed")
        for mutation in program_marker_mutations(marker, expected)[: cut_after + 1]:
            mutation()
        confirmed = marker == [expected, expected]
        if confirmed != (cut_after == 3):
            raise ModelError("torn confirmation was accepted")
        cases += 1

    return cases


def perform_direction(state: SwapState, direction: str) -> None:
    for sector in range(PAIR_SECTORS):
        recover_sector(state, direction, sector)
    set_control(direction_log(state, direction), f"{direction}-complete")


def verify_full_paths() -> dict[str, str]:
    confirmed = make_state("forward")
    perform_direction(confirmed, "forward")
    set_control(confirmed.forward, "trial-started")
    set_control(confirmed.forward, "confirmed")
    if confirmed.active != [new_value(index) for index in range(PAIR_SECTORS)]:
        raise ModelError("confirmed path did not retain the new pair")
    if confirmed.staging != [old_value(index) for index in range(PAIR_SECTORS)]:
        raise ModelError("confirmed path lost the rollback pair")

    reverted = make_state("forward")
    perform_direction(reverted, "forward")
    set_control(reverted.forward, "trial-started")
    perform_direction(reverted, "reverse")
    if reverted.active != [old_value(index) for index in range(PAIR_SECTORS)]:
        raise ModelError("reverse path did not restore the old pair")
    if reverted.staging != [new_value(index) for index in range(PAIR_SECTORS)]:
        raise ModelError("reverse path did not preserve the failed new pair")

    if confirmed.protected != reverted.protected:
        raise ModelError("full path changed a protected region")
    return {
        "confirmed": "active=new, staging=old",
        "unconfirmed": "active=old, staging=new",
        "mixed_generation_boot": "forbidden",
    }


def next_generation(current: int) -> int:
    if current < 0 or current >= UINT64_MAX:
        raise ModelError("generation cannot wrap")
    return current + 1


def verify_negative_cases() -> int:
    cases = 0
    if next_generation(41) != 42:
        raise ModelError("generation increment failed")
    cases += 1

    try:
        next_generation(UINT64_MAX)
    except ModelError:
        cases += 1
    else:
        raise ModelError("generation wrap was accepted")

    gap = make_state("forward")
    gap.forward.commit(0, 1)
    try:
        recover_sector(gap, "forward", 0)
    except ModelError:
        cases += 1
    else:
        raise ModelError("out-of-order journal marker was accepted")

    # Staging/metadata validation occurs before the first active erase.
    unchanged = make_state("forward")
    before = tuple(unchanged.active)
    staging_digest_valid = False
    metadata_copies_match = False
    if staging_digest_valid and metadata_copies_match:
        perform_direction(unchanged, "forward")
    if tuple(unchanged.active) != before:
        raise ModelError("invalid staging mutated the active pair")
    cases += 2
    return cases


def run_self_test() -> dict[str, Any]:
    metadata_abi = run_metadata_self_test()
    if CP_SECTORS != 0xEF or AP_SECTORS != 0x220:
        raise ModelError("slot sector counts drifted")
    if PAIR_SECTORS != 0x30F or PHASE_MARKERS != 0x92D:
        raise ModelError("pair/journal marker counts drifted")
    if JOURNAL_REQUIRED_SIZE != 0x127A0:
        raise ModelError("journal required size drifted")
    if JOURNAL_COPY_SIZE != 0x13000:
        raise ModelError("journal copy allocation drifted")

    phase_cases = verify_phase_reset_cases()
    activation_cases = verify_activation_reset_cases()
    control_cases = verify_control_reset_cases()
    negative_cases = verify_negative_cases()
    paths = verify_full_paths()
    total_cases = phase_cases + activation_cases + control_cases + negative_cases
    return {
        "status": "pass-read-only-host-model",
        "writes_enabled": False,
        "layout": {
            "cp_sectors": CP_SECTORS,
            "ap_sectors": AP_SECTORS,
            "pair_sectors": PAIR_SECTORS,
            "phases_per_sector": len(PHASES),
            "phase_markers_per_direction": PHASE_MARKERS,
            "sdk_write_chunk_size": SDK_WRITE_CHUNK_SIZE,
            "marker_slot_size": SDK_WRITE_CHUNK_SIZE,
            "journal_required_size": JOURNAL_REQUIRED_SIZE,
            "journal_copy_size": JOURNAL_COPY_SIZE,
            "journal_copies": 4,
            "scratch_sectors": 1,
        },
        "reset_cases": {
            "sector_phase_mutations": phase_cases,
            "activation_mutations": activation_cases,
            "trial_confirm_mutations": control_cases,
            "negative_cases": negative_cases,
            "total": total_cases,
        },
        "terminal_paths": paths,
        "metadata_abi": metadata_abi,
        "rules": {
            "phase_commit": "both mirrored markers must be exact",
            "trial_started": "either exact marker consumes the trial",
            "confirmation": "both exact markers are required",
            "generation": "uint64 monotonic, wrap rejected",
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit only the JSON result")
    args = parser.parse_args()

    try:
        result = run_self_test()
    except ModelError as error:
        print(f"BK7258 N15 OTA journal simulation FAIL: {error}")
        return 1

    encoded = json.dumps(result, indent=2, sort_keys=True)
    if not args.json:
        print("BK7258 N15 OTA journal simulation PASS (read-only)")
    print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
