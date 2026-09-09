# SPDX-License-Identifier: Apache-2.0
"""Private, conservative persistence for one OTA transaction per device."""

from __future__ import annotations

import os
import re
import sqlite3
import stat
from dataclasses import dataclass
from pathlib import Path


_DEVICE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", re.ASCII)
_DIGEST = re.compile(r"[0-9a-f]{64}", re.ASCII)
_TRANSACTION = re.compile(r"[0-9a-f]{32}", re.ASCII)
_VERSION = re.compile(
    r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\+([1-9][0-9]*)",
    re.ASCII,
)
_STATES = frozenset({
    "dispatching", "awaiting_first_report", "downloading", "verifying", "staged",
    "rebooting", "trial", "confirming", "rollback_check", "confirmed",
    "rolled_back", "failed", "uncertain", "orphaned",
})
_TERMINAL = frozenset({"confirmed", "rolled_back", "failed", "orphaned"})
_TERMINAL_PENDING = frozenset({"confirming", "rollback_check"})
_ORDER = {
    "dispatching": 0, "awaiting_first_report": 0, "downloading": 1,
    "verifying": 2, "staged": 3, "rebooting": 4, "trial": 5,
}
_COLUMNS = (
    "device_id", "transaction_id", "manifest_sha256", "target_version", "state",
    "progress_percent", "result", "created_at_ms", "updated_at_ms",
    "dispatch_boot_generation", "dispatch_session_id", "dispatch_sequence",
)


class OtaTransactionError(RuntimeError):
    """Invalid caller input, invalid transition, or unusable persistent state."""


@dataclass(frozen=True, slots=True)
class OtaTransaction:
    device_id: str
    transaction_id: str
    manifest_sha256: str
    target_version: str
    state: str
    progress_percent: int
    result: int | None
    created_at_ms: int
    updated_at_ms: int
    dispatch_boot_generation: int | None
    dispatch_session_id: int | None
    dispatch_sequence: int | None


def _valid_time(value: object) -> bool:
    return type(value) is int and 0 <= value < (1 << 63)


def _valid_u32(value: object) -> bool:
    return type(value) is int and 0 < value < (1 << 32) - 1


def _validate_transaction(value: OtaTransaction) -> None:
    if (not _DEVICE.fullmatch(value.device_id)
            or not _TRANSACTION.fullmatch(value.transaction_id)
            or not _DIGEST.fullmatch(value.manifest_sha256)
            or value.manifest_sha256 in {"0" * 64, "f" * 64}
            or not _VERSION.fullmatch(value.target_version)
            or value.state not in _STATES
            or type(value.progress_percent) is not int
            or not 0 <= value.progress_percent <= 100
            or value.result is not None and (type(value.result) is not int or value.result > 0)
            or not _valid_time(value.created_at_ms) or not _valid_time(value.updated_at_ms)
            or value.updated_at_ms < value.created_at_ms):
        raise OtaTransactionError("invalid OTA transaction")
    dispatch = (value.dispatch_boot_generation, value.dispatch_session_id,
                value.dispatch_sequence)
    if any(item is None for item in dispatch):
        if any(item is not None for item in dispatch):
            raise OtaTransactionError("partial OTA dispatch identity")
    elif not all(_valid_u32(item) for item in dispatch):
        raise OtaTransactionError("invalid OTA dispatch identity")
    if value.state == "failed" and (value.result is None or value.result >= 0):
        raise OtaTransactionError("failed OTA requires negative result")
    if value.state in _TERMINAL_PENDING and value.result != 0:
        raise OtaTransactionError("pending terminal OTA requires success result")
    if value.state != "failed" and value.result not in {None, 0}:
        raise OtaTransactionError("invalid OTA result")


class OtaTransactionStore:
    """Fail-closed state store; it never decides to redeliver a transaction."""

    def __init__(self, path: Path) -> None:
        fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
        try:
            os.fchmod(fd, 0o600)
            info = os.fstat(fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                    or info.st_mode & 0o077):
                raise OtaTransactionError("OTA transaction state must be private")
        finally:
            os.close(fd)
        self._db = sqlite3.connect(path)
        try:
            self._db.execute("""CREATE TABLE IF NOT EXISTS ota_transaction (
                device_id TEXT PRIMARY KEY NOT NULL,
                transaction_id TEXT NOT NULL,
                manifest_sha256 TEXT NOT NULL,
                target_version TEXT NOT NULL,
                state TEXT NOT NULL,
                progress_percent INTEGER NOT NULL,
                result INTEGER,
                created_at_ms INTEGER NOT NULL,
                updated_at_ms INTEGER NOT NULL,
                dispatch_boot_generation INTEGER,
                dispatch_session_id INTEGER,
                dispatch_sequence INTEGER
            )""")
            columns = tuple(row[1] for row in self._db.execute("PRAGMA table_info(ota_transaction)"))
            if columns != _COLUMNS:
                raise OtaTransactionError("invalid OTA transaction schema")
            self._db.commit()
            self._validate_all()
        except BaseException:
            self._db.close()
            raise

    def close(self) -> None:
        self._db.close()

    @staticmethod
    def _row(row: tuple[object, ...]) -> OtaTransaction:
        if len(row) != len(_COLUMNS):
            raise OtaTransactionError("invalid OTA transaction row")
        value = OtaTransaction(*row)
        _validate_transaction(value)
        return value

    def _validate_all(self) -> None:
        for row in self._db.execute("SELECT " + ",".join(_COLUMNS) + " FROM ota_transaction"):
            self._row(row)

    def _one(self, device_id: str) -> OtaTransaction | None:
        row = self._db.execute(
            "SELECT " + ",".join(_COLUMNS) + " FROM ota_transaction WHERE device_id=?",
            (device_id,),
        ).fetchone()
        return None if row is None else self._row(row)

    @staticmethod
    def _input(device_id: str, transaction_id: str, manifest_sha256: str,
               target_version: str, now_ms: int) -> None:
        value = OtaTransaction(device_id, transaction_id, manifest_sha256, target_version,
                               "dispatching", 0, None, now_ms, now_ms, None, None, None)
        _validate_transaction(value)

    def create(self, device_id: str, transaction_id: str, manifest_sha256: str,
               target_version: str, now_ms: int) -> OtaTransaction:
        self._input(device_id, transaction_id, manifest_sha256, target_version, now_ms)
        try:
            self._db.execute("BEGIN IMMEDIATE")
            previous = self._one(device_id)
            if previous is not None:
                if (previous.transaction_id == transaction_id
                        and previous.manifest_sha256 == manifest_sha256
                        and previous.target_version == target_version):
                    self._db.commit()
                    return previous
                if previous.state not in _TERMINAL:
                    raise OtaTransactionError("OTA transaction already exists")
                self._db.execute("DELETE FROM ota_transaction WHERE device_id=?",
                                 (device_id,))
            self._db.execute(
                "INSERT INTO ota_transaction VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
                (device_id, transaction_id, manifest_sha256, target_version, "dispatching",
                 0, None, now_ms, now_ms, None, None, None),
            )
            self._db.commit()
            return self._one(device_id)  # type: ignore[return-value]
        except BaseException:
            self._db.rollback()
            raise

    def load(self, device_id: str) -> OtaTransaction | None:
        if not isinstance(device_id, str) or not _DEVICE.fullmatch(device_id):
            raise OtaTransactionError("invalid OTA device")
        return self._one(device_id)

    def list(self) -> tuple[OtaTransaction, ...]:
        return tuple(self._row(row) for row in self._db.execute(
            "SELECT " + ",".join(_COLUMNS) + " FROM ota_transaction ORDER BY device_id"))

    def bind_dispatch(self, device_id: str, transaction_id: str, boot_generation: int,
                      session_id: int, sequence: int, now_ms: int) -> OtaTransaction:
        if (not isinstance(transaction_id, str) or not _TRANSACTION.fullmatch(transaction_id)
                or not all(_valid_u32(value) for value in (boot_generation, session_id, sequence))
                or not _valid_time(now_ms)):
            raise OtaTransactionError("invalid OTA dispatch")
        return self._advance(
            device_id, transaction_id, "awaiting_first_report", 0, None, now_ms,
            (boot_generation, session_id, sequence),
        )

    def redispatch(self, device_id: str, transaction_id: str, boot_generation: int,
                   session_id: int, sequence: int, now_ms: int) -> OtaTransaction:
        """Bind an explicitly reconciled uncertain transaction to a new session."""

        if (not isinstance(device_id, str) or not _DEVICE.fullmatch(device_id)
                or not isinstance(transaction_id, str)
                or not _TRANSACTION.fullmatch(transaction_id)
                or not all(_valid_u32(value) for value in
                           (boot_generation, session_id, sequence))
                or not _valid_time(now_ms)):
            raise OtaTransactionError("invalid OTA redispatch")
        try:
            self._db.execute("BEGIN IMMEDIATE")
            previous = self._one(device_id)
            if (previous is None or previous.transaction_id != transaction_id
                    or previous.state != "uncertain"):
                raise OtaTransactionError("OTA transaction is not uncertain")
            self._db.execute(
                "UPDATE ota_transaction SET state='awaiting_first_report', "
                "progress_percent=0, result=NULL, updated_at_ms=?, "
                "dispatch_boot_generation=?, dispatch_session_id=?, "
                "dispatch_sequence=? WHERE device_id=?",
                (now_ms, boot_generation, session_id, sequence, device_id),
            )
            self._db.commit()
            return self._one(device_id)  # type: ignore[return-value]
        except BaseException:
            self._db.rollback()
            raise

    def advance(self, device_id: str, transaction_id: str, state: str,
                progress_percent: int, result: int | None, now_ms: int) -> OtaTransaction:
        return self._advance(device_id, transaction_id, state, progress_percent, result, now_ms, None)

    def _advance(self, device_id: str, transaction_id: str, state: str,
                 progress_percent: int, result: int | None, now_ms: int,
                 dispatch: tuple[int, int, int] | None) -> OtaTransaction:
        if (not isinstance(device_id, str) or not _DEVICE.fullmatch(device_id)
                or not isinstance(transaction_id, str) or not _TRANSACTION.fullmatch(transaction_id)
                or state not in _STATES or type(progress_percent) is not int
                or not 0 <= progress_percent <= 100 or not _valid_time(now_ms)):
            raise OtaTransactionError("invalid OTA transition")
        try:
            self._db.execute("BEGIN IMMEDIATE")
            previous = self._one(device_id)
            if previous is None or previous.transaction_id != transaction_id:
                raise OtaTransactionError("unknown OTA transaction")
            identity = (previous.dispatch_boot_generation, previous.dispatch_session_id,
                        previous.dispatch_sequence) if dispatch is None else dispatch
            candidate = OtaTransaction(previous.device_id, previous.transaction_id,
                                       previous.manifest_sha256, previous.target_version, state,
                                       progress_percent, result, previous.created_at_ms, now_ms,
                                       *identity)
            _validate_transaction(candidate)
            identical = (previous.state == candidate.state
                         and previous.progress_percent == candidate.progress_percent
                         and previous.result == candidate.result
                         and (previous.dispatch_boot_generation, previous.dispatch_session_id,
                              previous.dispatch_sequence) == identity)
            if identical:
                self._db.commit()
                return previous
            if previous.state in _TERMINAL:
                raise OtaTransactionError("terminal OTA transaction is frozen")
            if previous.state == "uncertain" and state not in {"uncertain", "orphaned"}:
                raise OtaTransactionError("uncertain OTA transaction requires explicit retry")
            if previous.state == "uncertain":
                raise OtaTransactionError("uncertain OTA transaction is frozen")
            if previous.state in _TERMINAL_PENDING:
                expected = ("confirmed" if previous.state == "confirming"
                            else "rolled_back")
                if state not in {expected, "orphaned"}:
                    raise OtaTransactionError("pending terminal OTA requires status validation")
            elif state in _TERMINAL_PENDING:
                if state == "confirming" and previous.state != "trial":
                    raise OtaTransactionError("OTA confirmation requires trial")
            elif state == "uncertain":
                pass
            elif state == "orphaned":
                pass
            elif previous.state == "dispatching":
                if state != "awaiting_first_report":
                    raise OtaTransactionError("invalid initial OTA transition")
            elif previous.state == "awaiting_first_report":
                if state not in {"downloading", "failed", "rolled_back"}:
                    raise OtaTransactionError("invalid OTA report transition")
            elif state in _TERMINAL:
                if state == "confirmed" and previous.state not in {
                        "trial", "confirming"}:
                    raise OtaTransactionError("OTA confirmation requires trial")
            elif state not in _ORDER or previous.state not in _ORDER or _ORDER[state] < _ORDER[previous.state]:
                raise OtaTransactionError("nonmonotonic OTA phase")
            if (state == previous.state and progress_percent < previous.progress_percent):
                raise OtaTransactionError("nonmonotonic OTA progress")
            self._db.execute(
                "UPDATE ota_transaction SET state=?, progress_percent=?, result=?, updated_at_ms=?, "
                "dispatch_boot_generation=?, dispatch_session_id=?, dispatch_sequence=? WHERE device_id=?",
                (state, progress_percent, result, now_ms, *identity, device_id),
            )
            self._db.commit()
            return self._one(device_id)  # type: ignore[return-value]
        except BaseException:
            self._db.rollback()
            raise

    def mark_nonterminal_uncertain(self, now_ms: int) -> int:
        if not _valid_time(now_ms):
            raise OtaTransactionError("invalid OTA timestamp")
        try:
            self._db.execute("BEGIN IMMEDIATE")
            rows = self.list()
            count = 0
            for row in rows:
                if (row.state not in _TERMINAL
                        and row.state not in _TERMINAL_PENDING
                        and row.state != "uncertain"):
                    self._db.execute(
                        "UPDATE ota_transaction SET state='uncertain', updated_at_ms=? WHERE device_id=?",
                        (now_ms, row.device_id),
                    )
                    count += 1
            self._db.commit()
            return count
        except BaseException:
            self._db.rollback()
            raise

    def mark_uncertain(self, device_id: str, transaction_id: str,
                       now_ms: int) -> OtaTransaction:
        """Record lost transport without converting an unknown result to failure."""

        if (not isinstance(device_id, str) or not _DEVICE.fullmatch(device_id)
                or not isinstance(transaction_id, str)
                or not _TRANSACTION.fullmatch(transaction_id)
                or not _valid_time(now_ms)):
            raise OtaTransactionError("invalid OTA uncertainty")
        try:
            self._db.execute("BEGIN IMMEDIATE")
            previous = self._one(device_id)
            if previous is None or previous.transaction_id != transaction_id:
                raise OtaTransactionError("unknown OTA transaction")
            if (previous.state in _TERMINAL
                    or previous.state in _TERMINAL_PENDING
                    or previous.state == "uncertain"):
                self._db.commit()
                return previous
            self._db.execute(
                "UPDATE ota_transaction SET state='uncertain', updated_at_ms=? "
                "WHERE device_id=?", (now_ms, device_id),
            )
            self._db.commit()
            return self._one(device_id)  # type: ignore[return-value]
        except BaseException:
            self._db.rollback()
            raise

    def delete(self, device_id: str) -> bool:
        if not isinstance(device_id, str) or not _DEVICE.fullmatch(device_id):
            raise OtaTransactionError("invalid OTA device")
        try:
            self._db.execute("BEGIN IMMEDIATE")
            result = self._db.execute("DELETE FROM ota_transaction WHERE device_id=?", (device_id,))
            self._db.commit()
            return result.rowcount == 1
        except BaseException:
            self._db.rollback()
            raise
