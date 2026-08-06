#!/usr/bin/env python3
"""Portable host model for the accepted BK7258 N17 format-3 journal.

The model is deliberately independent of NuttX, the Beken SDK and the
bootloader.  It defines canonical record encoding, fail-closed bank scanning,
generation selection, counter-floor checks and the one-way policy marker.  It
performs no filesystem, firmware or board writes.
"""

from __future__ import annotations

import binascii
import hashlib
import struct
from dataclasses import dataclass
from enum import Enum, IntEnum
from typing import Mapping


RECORD_MAGIC = b"BKOTA17J"
COMMIT_MAGIC = b"CMT3"
FORMAT_VERSION = 3
RECORD_SIZE = 0x100
BANK_SIZE = 0x1000
RECORDS_PER_BANK = BANK_SIZE // RECORD_SIZE
WRITE_CHUNK_SIZE = 0x20
DIGEST_ALGORITHM_SHA256 = 1
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
ERASED_RECORD = b"\xff" * RECORD_SIZE
ERASED_BANK = b"\xff" * BANK_SIZE
ZERO_DIGEST = b"\x00" * 32

LAYOUT_SHA256 = bytes.fromhex(
    "32d3519eada0a7f77a284998e785fdb1daa55c691b3bfaf1a92b4097ce398203"
)
POLICY_MAGIC = b"BKOTA17A"
POLICY_VERSION = 1
POLICY_MARKER_SIZE = 0x20
POLICY_SECTOR_SIZE = 0x1000

_RECORD_PREFIX = struct.Struct("<8sHHBBBBIHHQQQII32s32s32s96s4s")
_RECORD = struct.Struct("<8sHHBBBBIHHQQQII32s32s32s96s4sI")
_POLICY_PREFIX = struct.Struct("<8sHHI12s")


class JournalError(RuntimeError):
    """Raised when persistent N17 data is non-canonical or unsafe."""


class Slot(IntEnum):
    A = 0
    B = 1


class Phase(IntEnum):
    STABLE = 1
    PENDING = 2
    TRIAL = 3


class Outcome(IntEnum):
    NONE = 0
    FACTORY = 1
    MIGRATED = 2
    CONFIRMED = 3
    ROLLED_BACK = 4


class TailState(str, Enum):
    ERASED = "erased"
    FULL = "full"
    DIRTY = "dirty"


class PolicyState(str, Enum):
    UNARMED = "unarmed"
    ARMED_CANONICAL = "armed-canonical"
    ARMED_DEGRADED = "armed-degraded"


class BootAction(str, Enum):
    STABLE = "boot-stable"
    TARGET_TRIAL = "boot-target-after-trial-commit"
    STABLE_TARGET_REJECTED = "boot-stable-and-close-rejected-target"
    STABLE_TRIAL_CONSUMED = "boot-stable-and-close-consumed-trial"
    SIGNED_FALLBACK = "boot-signed-fallback"
    FAIL_CLOSED = "fail-closed"


class BootFormat(str, Enum):
    FORMAT3 = "format-3"
    FORMAT2 = "format-2"
    FAIL_CLOSED = "fail-closed"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise JournalError(message)


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & UINT32_MAX


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def _require_uint(value: int, maximum: int, name: str, *, nonzero: bool = False) -> None:
    require(isinstance(value, int), f"{name} is not an integer")
    minimum = 1 if nonzero else 0
    require(minimum <= value <= maximum, f"{name} is outside its wire range")


def _require_digest(value: bytes, name: str, *, allow_zero: bool = False) -> None:
    require(isinstance(value, bytes) and len(value) == 32, f"{name} is not 32 bytes")
    if not allow_zero:
        require(value not in (ZERO_DIGEST, b"\xff" * 32), f"{name} is unset")


def _coerce_phase(value: Phase | int) -> Phase:
    try:
        return Phase(value)
    except ValueError as error:
        raise JournalError("phase is invalid") from error


def _coerce_outcome(value: Outcome | int) -> Outcome:
    try:
        return Outcome(value)
    except ValueError as error:
        raise JournalError("outcome is invalid") from error


@dataclass(frozen=True)
class RecordV3:
    """One canonical 256-byte format-3 lifecycle record."""

    phase: Phase
    stable_slot: Slot
    target_slot: int
    outcome: Outcome
    sequence: int
    generation: int
    accepted_security_counter: int
    created_timestamp: int
    slot_a_manifest_sha256: bytes
    slot_b_manifest_sha256: bytes
    previous_record_sha256: bytes = ZERO_DIGEST

    def _validate(self) -> tuple[Phase, Outcome, Slot]:
        phase = _coerce_phase(self.phase)
        outcome = _coerce_outcome(self.outcome)
        try:
            stable = Slot(self.stable_slot)
        except ValueError as error:
            raise JournalError("stable slot is invalid") from error

        _require_uint(self.sequence, UINT64_MAX, "sequence", nonzero=True)
        _require_uint(self.generation, UINT64_MAX, "generation", nonzero=True)
        _require_uint(
            self.accepted_security_counter,
            UINT64_MAX,
            "accepted security counter",
            nonzero=True,
        )
        _require_uint(self.created_timestamp, UINT32_MAX, "created timestamp")
        _require_digest(self.slot_a_manifest_sha256, "slot A Manifest digest")
        _require_digest(self.slot_b_manifest_sha256, "slot B Manifest digest")
        _require_digest(
            self.previous_record_sha256,
            "previous record digest",
            allow_zero=self.sequence == 1,
        )

        if self.sequence == 1:
            require(
                self.previous_record_sha256 == ZERO_DIGEST,
                "sequence 1 must have a zero previous-record digest",
            )
        else:
            require(
                self.previous_record_sha256 != ZERO_DIGEST,
                "chained record has a zero previous-record digest",
            )

        if phase == Phase.STABLE:
            require(self.target_slot == 0xFF, "stable record must clear target slot")
            require(
                outcome
                in (
                    Outcome.FACTORY,
                    Outcome.MIGRATED,
                    Outcome.CONFIRMED,
                    Outcome.ROLLED_BACK,
                ),
                "stable outcome is invalid",
            )
            if outcome in (Outcome.FACTORY, Outcome.MIGRATED):
                require(self.sequence == 1, "baseline stable record must be sequence 1")
            elif outcome == Outcome.CONFIRMED:
                require(self.sequence == 3, "confirmed record must be sequence 3")
            else:
                require(
                    self.sequence in (2, 3),
                    "rolled-back record must be sequence 2 or 3",
                )
        else:
            try:
                target = Slot(self.target_slot)
            except ValueError as error:
                raise JournalError("pending/trial target slot is invalid") from error
            require(target != stable, "target slot must be opposite the stable slot")
            require(outcome == Outcome.NONE, "pending/trial outcome must be NONE")
            if phase == Phase.PENDING:
                require(self.sequence == 1, "pending record must be sequence 1")
            else:
                require(self.sequence == 2, "trial record must be sequence 2")

        return phase, outcome, stable

    def encode(self) -> bytes:
        phase, outcome, stable = self._validate()
        prefix = _RECORD_PREFIX.pack(
            RECORD_MAGIC,
            FORMAT_VERSION,
            RECORD_SIZE,
            int(phase),
            int(stable),
            self.target_slot,
            int(outcome),
            0,
            DIGEST_ALGORITHM_SHA256,
            0,
            self.sequence,
            self.generation,
            self.accepted_security_counter,
            self.created_timestamp,
            0,
            self.slot_a_manifest_sha256,
            self.slot_b_manifest_sha256,
            self.previous_record_sha256,
            b"\x00" * 96,
            COMMIT_MAGIC,
        )
        require(len(prefix) == RECORD_SIZE - 4, "format-3 prefix size drifted")
        return prefix + struct.pack("<I", crc32(prefix))

    @staticmethod
    def decode(data: bytes) -> "RecordV3":
        require(isinstance(data, bytes) and len(data) == RECORD_SIZE, "record size is not 256 bytes")
        (
            magic,
            version,
            record_size,
            phase,
            stable_slot,
            target_slot,
            outcome,
            flags,
            digest_algorithm,
            reserved16,
            sequence,
            generation,
            accepted_security_counter,
            created_timestamp,
            reserved32,
            slot_a_digest,
            slot_b_digest,
            previous_digest,
            reserved,
            commit_magic,
            stored_crc,
        ) = _RECORD.unpack(data)

        require(magic == RECORD_MAGIC, "record magic mismatch")
        require(version == FORMAT_VERSION, "record version mismatch")
        require(record_size == RECORD_SIZE, "encoded record size mismatch")
        require(flags == 0, "record flags are not zero")
        require(
            digest_algorithm == DIGEST_ALGORITHM_SHA256,
            "record digest algorithm mismatch",
        )
        require(reserved16 == 0, "reserved16 is not zero")
        require(reserved32 == 0, "reserved32 is not zero")
        require(reserved == b"\x00" * 96, "reserved record bytes are not zero")
        require(commit_magic == COMMIT_MAGIC, "record commit marker mismatch")
        require(crc32(data[:-4]) == stored_crc, "record CRC32 mismatch")

        try:
            record = RecordV3(
                phase=Phase(phase),
                stable_slot=Slot(stable_slot),
                target_slot=target_slot,
                outcome=Outcome(outcome),
                sequence=sequence,
                generation=generation,
                accepted_security_counter=accepted_security_counter,
                created_timestamp=created_timestamp,
                slot_a_manifest_sha256=slot_a_digest,
                slot_b_manifest_sha256=slot_b_digest,
                previous_record_sha256=previous_digest,
            )
        except ValueError as error:
            raise JournalError("record enum field is invalid") from error
        require(record.encode() == data, "record is not canonical format 3")
        return record

    def digest(self) -> bytes:
        return sha256(self.encode())

    def manifest_digest(self, slot: Slot) -> bytes:
        return (
            self.slot_a_manifest_sha256
            if slot == Slot.A
            else self.slot_b_manifest_sha256
        )


def validate_transition(previous: RecordV3, current: RecordV3) -> None:
    """Require one canonical append within a single generation."""

    previous._validate()
    current._validate()
    require(current.sequence == previous.sequence + 1, "record sequence is not contiguous")
    require(current.generation == previous.generation, "generation changed within one bank")
    require(
        current.created_timestamp == previous.created_timestamp,
        "created timestamp changed within one generation",
    )
    require(
        current.slot_a_manifest_sha256 == previous.slot_a_manifest_sha256
        and current.slot_b_manifest_sha256 == previous.slot_b_manifest_sha256,
        "Manifest identity changed within one generation",
    )
    require(
        current.previous_record_sha256 == previous.digest(),
        "previous-record digest mismatch",
    )

    if previous.phase == Phase.PENDING:
        require(
            current.stable_slot == previous.stable_slot,
            "pending successor changed the stable slot",
        )
        if current.phase == Phase.TRIAL:
            require(current.target_slot == previous.target_slot, "trial target changed")
            require(current.outcome == Outcome.NONE, "trial outcome is not NONE")
            require(
                current.accepted_security_counter
                == previous.accepted_security_counter,
                "trial changed the accepted counter floor",
            )
            return
        require(
            current.phase == Phase.STABLE
            and current.outcome == Outcome.ROLLED_BACK,
            "pending may only advance to trial or rolled-back stable",
        )
        require(current.target_slot == 0xFF, "rollback did not clear target")
        require(
            current.accepted_security_counter == previous.accepted_security_counter,
            "direct rollback changed the accepted counter floor",
        )
        return

    if previous.phase == Phase.TRIAL:
        require(current.phase == Phase.STABLE, "trial successor is not stable")
        require(current.target_slot == 0xFF, "terminal record did not clear target")
        if current.outcome == Outcome.CONFIRMED:
            require(
                current.stable_slot == previous.target_slot,
                "confirmation did not select the trial target",
            )
            require(
                current.accepted_security_counter
                > previous.accepted_security_counter,
                "confirmation did not advance the accepted counter floor",
            )
            return
        require(current.outcome == Outcome.ROLLED_BACK, "trial terminal outcome is invalid")
        require(
            current.stable_slot == previous.stable_slot,
            "rollback changed the stable slot",
        )
        require(
            current.accepted_security_counter == previous.accepted_security_counter,
            "rollback changed the accepted counter floor",
        )
        return

    raise JournalError("stable record is terminal within its generation")


@dataclass(frozen=True)
class BankScan:
    records: tuple[RecordV3, ...]
    tail_state: TailState
    tail_slot: int
    error: str | None = None

    @property
    def eligible(self) -> bool:
        return bool(self.records)

    @property
    def last(self) -> RecordV3:
        require(self.eligible, "bank has no valid record")
        return self.records[-1]


def scan_bank(data: bytes) -> BankScan:
    """Return the longest valid prefix; a torn append cannot erase it."""

    require(isinstance(data, bytes) and len(data) == BANK_SIZE, "bank size is not 4 KiB")
    records: list[RecordV3] = []
    for index in range(RECORDS_PER_BANK):
        offset = index * RECORD_SIZE
        raw = data[offset : offset + RECORD_SIZE]
        if raw == ERASED_RECORD:
            later = data[offset + RECORD_SIZE :]
            if later != b"\xff" * len(later):
                return BankScan(tuple(records), TailState.DIRTY, index, "programmed data follows an erased slot")
            return BankScan(tuple(records), TailState.ERASED, index)
        try:
            record = RecordV3.decode(raw)
            if not records:
                require(record.sequence == 1, "first record sequence is not 1")
            else:
                validate_transition(records[-1], record)
        except JournalError as error:
            return BankScan(tuple(records), TailState.DIRTY, index, str(error))
        records.append(record)
    return BankScan(tuple(records), TailState.FULL, RECORDS_PER_BANK)


def encode_bank(records: tuple[RecordV3, ...] | list[RecordV3]) -> bytes:
    require(len(records) <= RECORDS_PER_BANK, "too many records for one bank")
    output = bytearray(ERASED_BANK)
    for index, record in enumerate(records):
        if index:
            validate_transition(records[index - 1], record)
        output[index * RECORD_SIZE : (index + 1) * RECORD_SIZE] = record.encode()
    parsed = scan_bank(bytes(output))
    require(len(parsed.records) == len(records), "encoded bank did not round-trip")
    return bytes(output)


def _successor_valid(older: BankScan, newer: BankScan) -> bool:
    if not older.eligible or not newer.eligible:
        return False
    old = older.last
    first = newer.records[0]
    if first.generation <= old.generation or old.phase != Phase.STABLE:
        return False
    if first.phase == Phase.PENDING:
        if (
            first.stable_slot != old.stable_slot
            or first.accepted_security_counter != old.accepted_security_counter
            or first.manifest_digest(first.stable_slot)
            != old.manifest_digest(old.stable_slot)
        ):
            return False
        return True
    if first.phase == Phase.STABLE and first.outcome == Outcome.MIGRATED:
        return (
            old.outcome == Outcome.MIGRATED
            and first.stable_slot == old.stable_slot
            and first.accepted_security_counter == old.accepted_security_counter
            and first.slot_a_manifest_sha256 == old.slot_a_manifest_sha256
            and first.slot_b_manifest_sha256 == old.slot_b_manifest_sha256
        )
    return False


@dataclass(frozen=True)
class BankSelection:
    bank_index: int
    bank: BankScan
    rejected_newer: bool = False


def select_bank(bank0: bytes, bank1: bytes) -> BankSelection | None:
    """Select the greatest eligible generation without trusting bad lineage."""

    scans = (scan_bank(bank0), scan_bank(bank1))
    eligible = [index for index, scan in enumerate(scans) if scan.eligible]
    if not eligible:
        return None
    if len(eligible) == 1:
        index = eligible[0]
        return BankSelection(index, scans[index])

    left = scans[0].last
    right = scans[1].last
    require(left.generation != right.generation, "equal generations in two banks are ambiguous")
    newer_index = 0 if left.generation > right.generation else 1
    older_index = 1 - newer_index
    if _successor_valid(scans[older_index], scans[newer_index]):
        return BankSelection(newer_index, scans[newer_index])
    return BankSelection(older_index, scans[older_index], rejected_newer=True)


def next_generation(current: int) -> int:
    _require_uint(current, UINT64_MAX, "generation", nonzero=True)
    require(current < UINT64_MAX, "generation cannot wrap")
    return current + 1


def validate_counter_policy(
    record: RecordV3,
    counters: Mapping[Slot, int],
) -> None:
    """Validate the counters of already authenticated Manifests at publish time."""

    record._validate()
    require(record.stable_slot in counters, "stable Manifest counter is missing")
    stable_counter = counters[Slot(record.stable_slot)]
    _require_uint(stable_counter, UINT64_MAX, "stable Manifest counter", nonzero=True)
    require(
        stable_counter == record.accepted_security_counter,
        "accepted floor differs from the stable Manifest counter",
    )

    if record.phase in (Phase.PENDING, Phase.TRIAL):
        target = Slot(record.target_slot)
        require(target in counters, "target Manifest counter is missing")
        target_counter = counters[target]
        _require_uint(target_counter, UINT64_MAX, "target Manifest counter", nonzero=True)
        require(target_counter > stable_counter, "target counter does not advance the floor")
    elif record.outcome == Outcome.MIGRATED:
        other = Slot.B if record.stable_slot == Slot.A else Slot.A
        require(counters.get(other) == stable_counter, "migration baseline counters differ")


@dataclass(frozen=True)
class SlotEvidence:
    manifest_sha256: bytes
    security_counter: int
    valid: bool = True


@dataclass(frozen=True)
class BootDecision:
    action: BootAction
    slot: Slot | None


def _eligible_evidence(
    record: RecordV3,
    slot: Slot,
    evidence: Mapping[Slot, SlotEvidence],
    *,
    strictly_newer: bool = False,
) -> bool:
    item = evidence.get(slot)
    if item is None or not item.valid:
        return False
    if item.manifest_sha256 != record.manifest_digest(slot):
        return False
    minimum_ok = (
        item.security_counter > record.accepted_security_counter
        if strictly_newer
        else item.security_counter >= record.accepted_security_counter
    )
    return minimum_ok


def decide_boot(record: RecordV3, evidence: Mapping[Slot, SlotEvidence]) -> BootDecision:
    """Apply the accepted phase-specific signed-slot eligibility rules."""

    record._validate()
    stable = Slot(record.stable_slot)
    if not _eligible_evidence(record, stable, evidence):
        if record.phase == Phase.STABLE:
            other = Slot.B if stable == Slot.A else Slot.A
            if _eligible_evidence(record, other, evidence):
                return BootDecision(BootAction.SIGNED_FALLBACK, other)
        return BootDecision(BootAction.FAIL_CLOSED, None)

    if record.phase == Phase.STABLE:
        return BootDecision(BootAction.STABLE, stable)
    if record.phase == Phase.TRIAL:
        return BootDecision(BootAction.STABLE_TRIAL_CONSUMED, stable)

    target = Slot(record.target_slot)
    if _eligible_evidence(record, target, evidence, strictly_newer=True):
        return BootDecision(BootAction.TARGET_TRIAL, target)
    return BootDecision(BootAction.STABLE_TARGET_REJECTED, stable)


def policy_marker() -> bytes:
    prefix = _POLICY_PREFIX.pack(
        POLICY_MAGIC,
        POLICY_VERSION,
        POLICY_MARKER_SIZE,
        0,
        LAYOUT_SHA256[:12],
    )
    require(len(prefix) == POLICY_MARKER_SIZE - 4, "policy marker prefix size drifted")
    return prefix + struct.pack("<I", crc32(prefix))


def classify_policy_sector(data: bytes) -> PolicyState:
    require(
        isinstance(data, bytes) and len(data) == POLICY_SECTOR_SIZE,
        "policy sector size is not 4 KiB",
    )
    if data == b"\xff" * POLICY_SECTOR_SIZE:
        return PolicyState.UNARMED
    marker = policy_marker()
    if data[: len(marker)] == marker and data[len(marker) :] == b"\xff" * (POLICY_SECTOR_SIZE - len(marker)):
        return PolicyState.ARMED_CANONICAL
    return PolicyState.ARMED_DEGRADED


def select_boot_format(
    policy_sector: bytes,
    selection: BankSelection | None,
    *,
    format2_valid: bool,
) -> BootFormat:
    """Choose format 3 first; only a wholly erased policy permits format 2."""

    policy = classify_policy_sector(policy_sector)
    if selection is not None:
        return BootFormat.FORMAT3
    if policy == PolicyState.UNARMED and format2_valid:
        return BootFormat.FORMAT2
    return BootFormat.FAIL_CLOSED


require(_RECORD_PREFIX.size == RECORD_SIZE - 4, "record prefix ABI size drifted")
require(_RECORD.size == RECORD_SIZE, "record ABI size drifted")
require(
    policy_marker().hex()
    == "424b4f5441313741010020000000000032d3519eada0a7f77a2849981a6fa31b",
    "policy marker differs from the accepted ADR bytes",
)
