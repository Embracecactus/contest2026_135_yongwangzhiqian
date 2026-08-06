#!/usr/bin/env python3
"""Exhaustively verify the portable BK7258 N17 format-3 host model."""

from __future__ import annotations

import argparse
from dataclasses import replace
import json
import struct
from typing import Callable

from bk7258_n17_journal import (
    BANK_SIZE,
    ERASED_BANK,
    POLICY_SECTOR_SIZE,
    RECORD_SIZE,
    UINT64_MAX,
    BankSelection,
    BootAction,
    BootFormat,
    JournalError,
    Outcome,
    Phase,
    PolicyState,
    RecordV3,
    Slot,
    SlotEvidence,
    TailState,
    classify_policy_sector,
    crc32,
    decide_boot,
    encode_bank,
    next_generation,
    policy_marker,
    scan_bank,
    select_bank,
    select_boot_format,
    sha256,
    validate_counter_policy,
    validate_transition,
)


TIMESTAMP = 0x65A72580
DIGEST_A1 = sha256(b"n17-manifest-a-counter-5")
DIGEST_A2 = sha256(b"n17-manifest-a-counter-11")
DIGEST_B1 = sha256(b"n17-manifest-b-counter-5")
DIGEST_B2 = sha256(b"n17-manifest-b-counter-9")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise JournalError(message)


def expect_rejected(action: Callable[[], object], name: str) -> None:
    try:
        action()
    except JournalError:
        return
    raise JournalError(f"negative case unexpectedly accepted: {name}")


def make_record(
    *,
    phase: Phase,
    stable: Slot,
    target: Slot | None,
    outcome: Outcome,
    sequence: int,
    generation: int,
    floor: int,
    digest_a: bytes = DIGEST_A1,
    digest_b: bytes = DIGEST_B2,
    previous: RecordV3 | None = None,
    timestamp: int = TIMESTAMP,
) -> RecordV3:
    return RecordV3(
        phase=phase,
        stable_slot=stable,
        target_slot=0xFF if target is None else target,
        outcome=outcome,
        sequence=sequence,
        generation=generation,
        accepted_security_counter=floor,
        created_timestamp=timestamp,
        slot_a_manifest_sha256=digest_a,
        slot_b_manifest_sha256=digest_b,
        previous_record_sha256=(b"\x00" * 32 if previous is None else previous.digest()),
    )


def normal_path(
    *,
    generation: int = 101,
    stable: Slot = Slot.A,
    base_floor: int = 5,
    target_floor: int = 9,
    confirm: bool = True,
    use_trial: bool = True,
    digest_a: bytes = DIGEST_A1,
    digest_b: bytes = DIGEST_B2,
) -> tuple[RecordV3, ...]:
    target = Slot.B if stable == Slot.A else Slot.A
    pending = make_record(
        phase=Phase.PENDING,
        stable=stable,
        target=target,
        outcome=Outcome.NONE,
        sequence=1,
        generation=generation,
        floor=base_floor,
        digest_a=digest_a,
        digest_b=digest_b,
    )
    if not use_trial:
        rollback = make_record(
            phase=Phase.STABLE,
            stable=stable,
            target=None,
            outcome=Outcome.ROLLED_BACK,
            sequence=2,
            generation=generation,
            floor=base_floor,
            digest_a=digest_a,
            digest_b=digest_b,
            previous=pending,
        )
        return pending, rollback

    trial = make_record(
        phase=Phase.TRIAL,
        stable=stable,
        target=target,
        outcome=Outcome.NONE,
        sequence=2,
        generation=generation,
        floor=base_floor,
        digest_a=digest_a,
        digest_b=digest_b,
        previous=pending,
    )
    terminal = make_record(
        phase=Phase.STABLE,
        stable=target if confirm else stable,
        target=None,
        outcome=Outcome.CONFIRMED if confirm else Outcome.ROLLED_BACK,
        sequence=3,
        generation=generation,
        floor=target_floor if confirm else base_floor,
        digest_a=digest_a,
        digest_b=digest_b,
        previous=trial,
    )
    return pending, trial, terminal


def baseline(
    generation: int,
    *,
    stable: Slot = Slot.A,
    outcome: Outcome = Outcome.MIGRATED,
    floor: int = 5,
    digest_a: bytes = DIGEST_A1,
    digest_b: bytes = DIGEST_B1,
) -> RecordV3:
    return make_record(
        phase=Phase.STABLE,
        stable=stable,
        target=None,
        outcome=outcome,
        sequence=1,
        generation=generation,
        floor=floor,
        digest_a=digest_a,
        digest_b=digest_b,
    )


def verify_roundtrips() -> int:
    records = [
        baseline(1, outcome=Outcome.FACTORY),
        baseline(2),
        *normal_path(),
        *normal_path(generation=102, confirm=False),
        *normal_path(generation=103, use_trial=False),
        *normal_path(
            generation=104,
            stable=Slot.B,
            base_floor=9,
            target_floor=11,
            digest_a=DIGEST_A2,
            digest_b=DIGEST_B2,
        ),
    ]
    for index, record in enumerate(records):
        require(RecordV3.decode(record.encode()) == record, f"round-trip failed at {index}")
    return len(records)


def _with_crc(data: bytearray) -> bytes:
    struct.pack_into("<I", data, RECORD_SIZE - 4, crc32(bytes(data[:-4])))
    return bytes(data)


def verify_structural_negatives() -> int:
    raw = normal_path()[0].encode()

    def mutate(offset: int, value: bytes, *, repair_crc: bool = True) -> bytes:
        changed = bytearray(raw)
        changed[offset : offset + len(value)] = value
        return _with_crc(changed) if repair_crc else bytes(changed)

    cases = {
        "magic": mutate(0x000, b"X"),
        "version": mutate(0x008, struct.pack("<H", 4)),
        "record-size": mutate(0x00A, struct.pack("<H", 255)),
        "phase": mutate(0x00C, b"\x09"),
        "stable-slot": mutate(0x00D, b"\x02"),
        "target-slot": mutate(0x00E, b"\x00"),
        "outcome": mutate(0x00F, b"\x01"),
        "flags": mutate(0x010, struct.pack("<I", 1)),
        "digest-algorithm": mutate(0x014, struct.pack("<H", 2)),
        "reserved16": mutate(0x016, struct.pack("<H", 1)),
        "sequence": mutate(0x018, struct.pack("<Q", 2)),
        "generation": mutate(0x020, struct.pack("<Q", 0)),
        "counter-floor": mutate(0x028, struct.pack("<Q", 0)),
        "reserved32": mutate(0x034, struct.pack("<I", 1)),
        "slot-a-digest": mutate(0x038, b"\x00" * 32),
        "slot-b-digest": mutate(0x058, b"\xff" * 32),
        "previous-digest": mutate(0x078, b"\x01" * 32),
        "reserved": mutate(0x098, b"\x01"),
        "commit-magic": mutate(0x0F8, b"X"),
        "crc": mutate(0x0FC, b"\x00\x00\x00\x00", repair_crc=False),
    }
    for name, value in cases.items():
        expect_rejected(lambda payload=value: RecordV3.decode(payload), name)
    return len(cases)


def verify_bit_mutations() -> int:
    raw = normal_path()[0].encode()
    checked = 0
    for offset in range(RECORD_SIZE):
        for bit in range(8):
            changed = bytearray(raw)
            changed[offset] ^= 1 << bit
            expect_rejected(
                lambda payload=bytes(changed): RecordV3.decode(payload),
                f"bit@0x{offset:03x}:{bit}",
            )
            checked += 1
    return checked


def verify_torn_records() -> tuple[int, int]:
    pending, trial, _ = normal_path()
    raw = pending.encode()
    for cut in range(RECORD_SIZE):
        torn = raw[:cut] + b"\xff" * (RECORD_SIZE - cut)
        expect_rejected(lambda payload=torn: RecordV3.decode(payload), f"record-cut-{cut}")

    first = pending.encode()
    trial_raw = trial.encode()
    for cut in range(RECORD_SIZE):
        bank = bytearray(ERASED_BANK)
        bank[:RECORD_SIZE] = first
        bank[RECORD_SIZE : 2 * RECORD_SIZE] = (
            trial_raw[:cut] + b"\xff" * (RECORD_SIZE - cut)
        )
        scan = scan_bank(bytes(bank))
        require(len(scan.records) == 1 and scan.last == pending, f"torn append displaced pending at {cut}")
        expected_tail = TailState.ERASED if cut == 0 else TailState.DIRTY
        require(scan.tail_state == expected_tail, f"torn tail classification failed at {cut}")
    return RECORD_SIZE, RECORD_SIZE


def verify_transition_negatives() -> int:
    pending, trial, confirmed = normal_path()
    cases = {
        "generation-drift": replace(trial, generation=trial.generation + 1),
        "timestamp-drift": replace(trial, created_timestamp=trial.created_timestamp + 1),
        "manifest-drift": replace(trial, slot_b_manifest_sha256=DIGEST_B1),
        "previous-digest": replace(trial, previous_record_sha256=b"\x01" * 32),
        "trial-floor-drift": replace(trial, accepted_security_counter=6),
    }
    checked = 0
    for name, value in cases.items():
        expect_rejected(lambda candidate=value: validate_transition(pending, candidate), name)
        checked += 1

    direct_confirm = replace(confirmed, previous_record_sha256=pending.digest())
    expect_rejected(lambda: validate_transition(pending, direct_confirm), "pending-direct-confirm")
    checked += 1
    expect_rejected(lambda: validate_transition(confirmed, pending), "stable-is-terminal")
    checked += 1

    _, trial_rollback, _ = normal_path(generation=105)
    bad_confirm = make_record(
        phase=Phase.STABLE,
        stable=trial_rollback.stable_slot,
        target=None,
        outcome=Outcome.CONFIRMED,
        sequence=3,
        generation=trial_rollback.generation,
        floor=6,
        previous=trial_rollback,
    )
    expect_rejected(lambda: validate_transition(trial_rollback, bad_confirm), "confirm-kept-base")
    checked += 1
    return checked


def _selected_generation(selection: BankSelection | None) -> int | None:
    return None if selection is None else selection.bank.last.generation


def verify_bank_selection() -> int:
    old = baseline(100)
    old_bank = encode_bank([old])
    pending = normal_path(
        generation=101,
        digest_a=DIGEST_A1,
        digest_b=DIGEST_B2,
    )[0]
    new_bank = encode_bank([pending])
    checked = 0

    require(_selected_generation(select_bank(old_bank, new_bank)) == 101, "newer bank was not selected")
    checked += 1
    require(_selected_generation(select_bank(new_bank, old_bank)) == 101, "bank order affected selection")
    checked += 1
    expect_rejected(lambda: select_bank(old_bank, old_bank), "equal-generations")
    checked += 1

    lower_floor = replace(pending, accepted_security_counter=4)
    selection = select_bank(old_bank, encode_bank([lower_floor]))
    require(_selected_generation(selection) == 100 and selection.rejected_newer, "lower floor displaced old bank")
    checked += 1

    wrong_stable = normal_path(
        generation=102,
        stable=Slot.B,
        base_floor=5,
        target_floor=11,
        digest_a=DIGEST_A2,
        digest_b=DIGEST_B1,
    )[0]
    selection = select_bank(old_bank, encode_bank([wrong_stable]))
    require(_selected_generation(selection) == 100 and selection.rejected_newer, "wrong stable lineage displaced old bank")
    checked += 1

    factory_newer = baseline(102, outcome=Outcome.FACTORY)
    selection = select_bank(old_bank, encode_bank([factory_newer]))
    require(_selected_generation(selection) == 100 and selection.rejected_newer, "factory record displaced an existing bank")
    checked += 1

    migrated_newer = baseline(102)
    require(_selected_generation(select_bank(old_bank, encode_bank([migrated_newer]))) == 102, "second migration baseline was rejected")
    checked += 1

    torn = bytearray(ERASED_BANK)
    raw = pending.encode()
    torn[:97] = raw[:97]
    require(_selected_generation(select_bank(old_bank, bytes(torn))) == 100, "torn first record displaced old bank")
    checked += 1

    pending_record, trial, _ = normal_path(generation=101)
    partial = bytearray(encode_bank([pending_record]))
    trial_raw = trial.encode()
    partial[RECORD_SIZE : RECORD_SIZE + 173] = trial_raw[:173]
    require(_selected_generation(select_bank(old_bank, bytes(partial))) == 101, "torn append erased a committed newer prefix")
    checked += 1

    dirty_gap = bytearray(encode_bank([pending_record]))
    dirty_gap[3 * RECORD_SIZE] = 0
    scan = scan_bank(bytes(dirty_gap))
    require(len(scan.records) == 1 and scan.tail_state == TailState.DIRTY, "dirty gap was not contained")
    checked += 1

    require(select_bank(ERASED_BANK, ERASED_BANK) is None, "erased banks were eligible")
    checked += 1
    return checked


def verify_counter_rules() -> int:
    pending, trial, confirmed = normal_path()
    _, direct_rollback = normal_path(generation=102, use_trial=False)
    _, _, trial_rollback = normal_path(generation=103, confirm=False)
    counters = {Slot.A: 5, Slot.B: 9}
    for record in (pending, trial, confirmed, direct_rollback, trial_rollback):
        validate_counter_policy(record, counters)
    checked = 5

    migration = baseline(200)
    validate_counter_policy(migration, {Slot.A: 5, Slot.B: 5})
    checked += 1
    expect_rejected(
        lambda: validate_counter_policy(pending, {Slot.A: 5, Slot.B: 5}),
        "target-counter-not-newer",
    )
    checked += 1
    wrong_confirm = replace(confirmed, accepted_security_counter=6)
    expect_rejected(
        lambda: validate_counter_policy(wrong_confirm, counters),
        "confirmation-floor-not-target-counter",
    )
    checked += 1
    expect_rejected(
        lambda: validate_counter_policy(migration, {Slot.A: 5, Slot.B: 6}),
        "migration-baselines-differ",
    )
    checked += 1
    require(next_generation(41) == 42, "generation increment failed")
    checked += 1
    expect_rejected(lambda: next_generation(UINT64_MAX), "generation-wrap")
    checked += 1
    return checked


def verify_boot_decisions() -> int:
    pending, trial, confirmed = normal_path()
    evidence = {
        Slot.A: SlotEvidence(DIGEST_A1, 5),
        Slot.B: SlotEvidence(DIGEST_B2, 9),
    }
    cases = [
        (pending, evidence, BootAction.TARGET_TRIAL, Slot.B),
        (
            pending,
            {**evidence, Slot.B: SlotEvidence(DIGEST_B2, 9, False)},
            BootAction.STABLE_TARGET_REJECTED,
            Slot.A,
        ),
        (
            pending,
            {**evidence, Slot.B: SlotEvidence(DIGEST_B2, 5)},
            BootAction.STABLE_TARGET_REJECTED,
            Slot.A,
        ),
        (
            pending,
            {**evidence, Slot.A: SlotEvidence(DIGEST_A1, 5, False)},
            BootAction.FAIL_CLOSED,
            None,
        ),
        (trial, evidence, BootAction.STABLE_TRIAL_CONSUMED, Slot.A),
        (confirmed, evidence, BootAction.STABLE, Slot.B),
        (
            confirmed,
            {**evidence, Slot.B: SlotEvidence(DIGEST_B2, 9, False)},
            BootAction.FAIL_CLOSED,
            None,
        ),
    ]

    fallback_record = baseline(
        300,
        stable=Slot.B,
        outcome=Outcome.FACTORY,
        floor=9,
        digest_a=DIGEST_A2,
        digest_b=DIGEST_B2,
    )
    cases.append(
        (
            fallback_record,
            {
                Slot.A: SlotEvidence(DIGEST_A2, 11),
                Slot.B: SlotEvidence(DIGEST_B2, 9, False),
            },
            BootAction.SIGNED_FALLBACK,
            Slot.A,
        )
    )
    for record, items, action, slot in cases:
        decision = decide_boot(record, items)
        require((decision.action, decision.slot) == (action, slot), f"boot decision mismatch: {action}")
    return len(cases)


def verify_policy() -> tuple[int, int]:
    erased = b"\xff" * POLICY_SECTOR_SIZE
    marker = policy_marker()
    canonical = marker + b"\xff" * (POLICY_SECTOR_SIZE - len(marker))
    require(classify_policy_sector(erased) == PolicyState.UNARMED, "erased policy is not unarmed")
    require(classify_policy_sector(canonical) == PolicyState.ARMED_CANONICAL, "canonical marker is not armed")
    checked = 2

    for cut in range(1, len(marker)):
        torn = marker[:cut] + b"\xff" * (POLICY_SECTOR_SIZE - cut)
        require(classify_policy_sector(torn) == PolicyState.ARMED_DEGRADED, f"torn marker reopened policy at {cut}")
        checked += 1
    changed = bytearray(canonical)
    changed[0] ^= 1
    require(classify_policy_sector(bytes(changed)) == PolicyState.ARMED_DEGRADED, "changed marker was canonical")
    checked += 1
    changed = bytearray(erased)
    changed[-1] = 0xFE
    require(classify_policy_sector(bytes(changed)) == PolicyState.ARMED_DEGRADED, "tail programming reopened policy")
    checked += 1

    selection = select_bank(encode_bank([baseline(400)]), ERASED_BANK)
    format_cases = (
        (erased, None, True, BootFormat.FORMAT2),
        (erased, None, False, BootFormat.FAIL_CLOSED),
        (erased, selection, True, BootFormat.FORMAT3),
        (canonical, selection, True, BootFormat.FORMAT3),
        (canonical, None, True, BootFormat.FAIL_CLOSED),
        (bytes(changed), None, True, BootFormat.FAIL_CLOSED),
    )
    for sector, selected, format2_valid, expected in format_cases:
        require(
            select_boot_format(sector, selected, format2_valid=format2_valid) == expected,
            f"boot-format decision mismatch: {expected}",
        )
    return checked, len(format_cases)


def verify_migration_matrix() -> int:
    erased_policy = b"\xff" * POLICY_SECTOR_SIZE
    marker = policy_marker()
    canonical_policy = marker + b"\xff" * (POLICY_SECTOR_SIZE - len(marker))
    first = encode_bank([baseline(501)])
    second = encode_bank([baseline(502)])
    torn_first = bytearray(ERASED_BANK)
    torn_first[:111] = baseline(501).encode()[:111]
    torn_second = bytearray(ERASED_BANK)
    torn_second[:211] = baseline(502).encode()[:211]
    torn_policy = marker[:17] + b"\xff" * (POLICY_SECTOR_SIZE - 17)

    cases = (
        (erased_policy, ERASED_BANK, ERASED_BANK, True, BootFormat.FORMAT2),
        (erased_policy, bytes(torn_first), ERASED_BANK, True, BootFormat.FORMAT2),
        (erased_policy, first, ERASED_BANK, True, BootFormat.FORMAT3),
        (erased_policy, first, bytes(torn_second), True, BootFormat.FORMAT3),
        (erased_policy, first, second, True, BootFormat.FORMAT3),
        (torn_policy, first, second, True, BootFormat.FORMAT3),
        (canonical_policy, first, second, True, BootFormat.FORMAT3),
        (canonical_policy, ERASED_BANK, ERASED_BANK, True, BootFormat.FAIL_CLOSED),
    )
    for policy, bank0, bank1, format2_valid, expected in cases:
        selected = select_bank(bank0, bank1)
        actual = select_boot_format(policy, selected, format2_valid=format2_valid)
        require(actual == expected, f"migration matrix mismatch: expected {expected}, got {actual}")
    return len(cases)


def verify() -> dict[str, object]:
    require(BANK_SIZE // RECORD_SIZE == 16, "records-per-bank drifted")
    roundtrips = verify_roundtrips()
    structural = verify_structural_negatives()
    mutations = verify_bit_mutations()
    torn_records, torn_appends = verify_torn_records()
    transitions = verify_transition_negatives()
    banks = verify_bank_selection()
    counters = verify_counter_rules()
    boot_decisions = verify_boot_decisions()
    policy_states, format_decisions = verify_policy()
    migration = verify_migration_matrix()
    return {
        "bank_selection_cases": banks,
        "boot_decision_cases": boot_decisions,
        "boot_format_cases": format_decisions,
        "canonical_roundtrips": roundtrips,
        "counter_cases": counters,
        "migration_reset_cases": migration,
        "policy_marker_hex": policy_marker().hex(),
        "policy_state_cases": policy_states,
        "record_bit_mutations": mutations,
        "record_size": RECORD_SIZE,
        "records_per_bank": BANK_SIZE // RECORD_SIZE,
        "structural_negative_cases": structural,
        "torn_append_cuts": torn_appends,
        "torn_record_cuts": torn_records,
        "transition_negative_cases": transitions,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    try:
        result = verify()
    except JournalError as error:
        print(f"N17 format-3 journal verification failed: {error}")
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
