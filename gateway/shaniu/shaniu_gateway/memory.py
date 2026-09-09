# SPDX-License-Identifier: Apache-2.0
"""Private, opt-in, per-device dialogue memory for the Shaniu Gateway.

This module intentionally has no network or provider dependency.  A caller must
create a :class:`MemoryStore` with an explicit device allow-list; omitting that
step means that no long-term conversation data is retained.
"""
from __future__ import annotations

import os
import re
import sqlite3
import stat
import time
from pathlib import Path
from typing import Callable, Iterable


_DEVICE_ID = re.compile(r'[A-Za-z0-9][A-Za-z0-9._:-]{0,127}', re.ASCII)

MAX_TEXT_CHARS = 4096
MAX_CONTEXT_CHARS = 8192
MAX_CONTEXT_TURNS = 4
_MAX_CONFIGURED_DEVICES = 256


class MemoryStateError(RuntimeError):
    """The private state cannot safely be used."""


def _now_ms() -> int:
    return time.time_ns() // 1_000_000


def _valid_device_id(value: object) -> bool:
    return isinstance(value, str) and _DEVICE_ID.fullmatch(value) is not None


class MemorySession:
    """A deliberately small per-connection view of one device's memory."""
    def __init__(self, store: 'MemoryStore', device_id: str):
        self._store = store
        self._device_id = device_id

    def context(self) -> tuple[tuple[str, str], ...]:
        return self._store.context(self._device_id)

    def append(self, transcript: str, reply: str) -> bool:
        """Persist a completed turn, returning false when memory is revoked."""
        return self._store.append(self._device_id, transcript, reply)


class MemoryStore:
    """SQLite-backed state with explicit opt-in and fail-closed validation."""
    def __init__(self, path: Path, configured_devices: Iterable[str], *,
                 retention_ms: int = 30 * 24 * 60 * 60 * 1000,
                 max_records_per_device: int = 32,
                 clock_ms: Callable[[], int] = _now_ms):
        devices = tuple(configured_devices)
        if (not 1 <= len(devices) <= _MAX_CONFIGURED_DEVICES
                or len(set(devices)) != len(devices)
                or any(not _valid_device_id(device) for device in devices)):
            raise ValueError('invalid configured memory devices')
        if (type(retention_ms) is not int or retention_ms <= 0
                or type(max_records_per_device) is not int
                or not 1 <= max_records_per_device <= 4096
                or not callable(clock_ms)):
            raise ValueError('invalid memory retention')
        self._path = Path(path)
        self._devices = frozenset(devices)
        self._retention_ms = retention_ms
        self._max_records = max_records_per_device
        self._clock_ms = clock_ms
        self._available = True
        self.db = self._open_private_database(self._path)
        try:
            self._create_schema()
            self._initialize_devices()
        except Exception as error:
            self.db.close()
            if isinstance(error, MemoryStateError):
                raise
            raise MemoryStateError('memory state initialization failed') from error

    @staticmethod
    def _open_private_database(path: Path) -> sqlite3.Connection:
        try:
            parent = Path(os.path.abspath(path.parent))
            current = Path(parent.anchor)
            for component in parent.parts[1:]:
                current /= component
                info = os.lstat(current)
                shared_tmp = (current in {Path('/tmp'), Path('/var/tmp')}
                              and info.st_mode & stat.S_ISVTX)
                if (not stat.S_ISDIR(info.st_mode)
                        or (info.st_uid not in {0, os.geteuid()} and not shared_tmp)
                        or (info.st_mode & 0o022 and not shared_tmp)):
                    raise MemoryStateError('memory state path is not trusted')
            parent_info = os.lstat(parent)
            if (parent_info.st_uid != os.geteuid() or parent_info.st_mode & 0o022):
                raise MemoryStateError('memory state directory must be operator-private')
            fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
            try:
                info = os.fstat(fd)
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                        or stat.S_IMODE(info.st_mode) != 0o600):
                    raise MemoryStateError('memory state must be an owner-private regular file')
            finally:
                os.close(fd)
            return sqlite3.connect(path)
        except MemoryStateError:
            raise
        except (OSError, sqlite3.Error) as error:
            raise MemoryStateError('memory state cannot be opened') from error

    def _create_schema(self) -> None:
        try:
            self.db.execute('PRAGMA foreign_keys=ON')
            self.db.execute(
                'CREATE TABLE IF NOT EXISTS memory_devices ('
                'device_id TEXT PRIMARY KEY, '
                "state TEXT NOT NULL CHECK(state IN ('enabled', 'revoked')))"
            )
            self.db.execute(
                'CREATE TABLE IF NOT EXISTS memory_turns ('
                'id INTEGER PRIMARY KEY AUTOINCREMENT, '
                'device_id TEXT NOT NULL, created_at_ms INTEGER NOT NULL, '
                'transcript TEXT NOT NULL, reply TEXT NOT NULL, '
                'FOREIGN KEY(device_id) REFERENCES memory_devices(device_id) '
                'ON DELETE CASCADE)'
            )
            self.db.execute(
                'CREATE INDEX IF NOT EXISTS memory_turns_device_time '
                'ON memory_turns(device_id, created_at_ms DESC, id DESC)'
            )
            self.db.commit()
        except sqlite3.Error:
            self._rollback()
            raise

    def _initialize_devices(self) -> None:
        try:
            self.db.execute('BEGIN IMMEDIATE')
            self.db.executemany(
                "INSERT OR IGNORE INTO memory_devices(device_id, state) VALUES (?, 'enabled')",
                ((device,) for device in self._devices),
            )
            self.db.commit()
        except sqlite3.Error:
            self._rollback()
            raise

    def _rollback(self) -> None:
        try:
            self.db.rollback()
        except sqlite3.Error:
            pass

    def _fail(self, message: str, error: BaseException | None = None):
        self._available = False
        if error is None:
            raise MemoryStateError(message)
        raise MemoryStateError(message) from error

    def _check_device(self, device_id: str) -> None:
        if not _valid_device_id(device_id) or device_id not in self._devices:
            raise ValueError('unknown memory device')

    def configured(self, device_id: str) -> bool:
        """Return whether this valid device was explicitly opted in at startup."""
        if not _valid_device_id(device_id):
            raise ValueError('invalid memory device')
        return device_id in self._devices

    def session(self, device_id: str) -> MemorySession:
        self._check_device(device_id)
        return MemorySession(self, device_id)

    def permission(self, device_id: str) -> str:
        self._check_device(device_id)
        if not self._available:
            raise MemoryStateError('memory state unavailable')
        try:
            row = self.db.execute(
                'SELECT state FROM memory_devices WHERE device_id=?', (device_id,),
            ).fetchone()
            # Authorization is meaningful only while the record store used by
            # conversations is readable too. This also keeps console status
            # from reporting allowed after a persistence failure.
            self.db.execute(
                'SELECT 1 FROM memory_turns WHERE device_id=? LIMIT 1', (device_id,),
            ).fetchone()
        except sqlite3.Error as error:
            self._fail('memory permission unavailable', error)
        if row is None or len(row) != 1 or row[0] not in {'enabled', 'revoked'}:
            self._fail('memory permission is corrupt')
        return row[0]

    def _prune(self, device_id: str, now_ms: int) -> None:
        cutoff = now_ms - self._retention_ms
        self.db.execute(
            'DELETE FROM memory_turns WHERE device_id=? AND created_at_ms < ?',
            (device_id, cutoff),
        )
        self.db.execute(
            'DELETE FROM memory_turns WHERE id IN ('
            'SELECT id FROM memory_turns WHERE device_id=? '
            'ORDER BY created_at_ms DESC, id DESC LIMIT -1 OFFSET ?)',
            (device_id, self._max_records),
        )

    def context(self, device_id: str) -> tuple[tuple[str, str], ...]:
        self._check_device(device_id)
        try:
            self.db.execute('BEGIN IMMEDIATE')
            if self.permission(device_id) != 'enabled':
                self.db.commit()
                return ()
            self._prune(device_id, self._clock_ms())
            rows = self.db.execute(
                'SELECT transcript, reply FROM memory_turns WHERE device_id=? '
                'ORDER BY created_at_ms DESC, id DESC LIMIT ?',
                (device_id, MAX_CONTEXT_TURNS),
            ).fetchall()
            self.db.commit()
        except (sqlite3.Error, MemoryStateError) as error:
            self._rollback()
            self._fail('memory context unavailable', error)
        retained: list[tuple[str, str]] = []
        chars = 0
        for row in rows:
            if (len(row) != 2 or not isinstance(row[0], str) or not isinstance(row[1], str)
                    or not row[0] or not row[1] or len(row[0]) > MAX_TEXT_CHARS
                    or len(row[1]) > MAX_TEXT_CHARS):
                self._fail('memory record is corrupt')
            turn_chars = len(row[0]) + len(row[1])
            if chars + turn_chars > MAX_CONTEXT_CHARS:
                continue
            retained.append((row[0], row[1]))
            chars += turn_chars
        retained.reverse()
        return tuple(retained)

    def append(self, device_id: str, transcript: str, reply: str) -> bool:
        self._check_device(device_id)
        if (not isinstance(transcript, str) or not isinstance(reply, str)
                or not transcript or not reply or len(transcript) > MAX_TEXT_CHARS
                or len(reply) > MAX_TEXT_CHARS
                or len(transcript) + len(reply) > MAX_CONTEXT_CHARS):
            raise ValueError('invalid memory turn')
        try:
            self.db.execute('BEGIN IMMEDIATE')
            if self.permission(device_id) != 'enabled':
                self.db.commit()
                return False
            now_ms = self._clock_ms()
            self.db.execute(
                'INSERT INTO memory_turns(device_id, created_at_ms, transcript, reply) '
                'VALUES (?, ?, ?, ?)', (device_id, now_ms, transcript, reply),
            )
            self._prune(device_id, now_ms)
            self.db.commit()
            return True
        except (sqlite3.Error, MemoryStateError) as error:
            self._rollback()
            self._fail('memory append unavailable', error)

    def delete(self, device_id: str, scope: str) -> None:
        self._check_device(device_id)
        if scope not in {'conversations', 'all'}:
            raise ValueError('unsupported memory delete scope')
        try:
            self.db.execute('BEGIN IMMEDIATE')
            self.db.execute('DELETE FROM memory_turns WHERE device_id=?', (device_id,))
            self.db.commit()
        except sqlite3.Error as error:
            self._rollback()
            self._fail('memory deletion unavailable', error)

    def revoke(self, device_id: str) -> None:
        self._check_device(device_id)
        try:
            self.db.execute('BEGIN IMMEDIATE')
            self.db.execute('DELETE FROM memory_turns WHERE device_id=?', (device_id,))
            changed = self.db.execute(
                "UPDATE memory_devices SET state='revoked' WHERE device_id=?", (device_id,),
            ).rowcount
            if changed != 1:
                raise MemoryStateError('memory permission is corrupt')
            self.db.commit()
        except (sqlite3.Error, MemoryStateError) as error:
            self._rollback()
            self._fail('memory revocation unavailable', error)

    def close(self) -> None:
        self._available = False
        self.db.close()
