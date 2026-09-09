# SPDX-License-Identifier: Apache-2.0
import os
import tempfile
import unittest
from pathlib import Path

from shaniu_gateway.memory import (
    MAX_CONTEXT_CHARS,
    MemoryStateError,
    MemoryStore,
)


class Clock:
    def __init__(self, now=1000):
        self.now = now

    def __call__(self):
        return self.now


class MemoryStoreTest(unittest.TestCase):
    def make_store(self, directory, *, devices=('board-1',), clock=None, **kwargs):
        return MemoryStore(Path(directory) / 'memory.db', devices,
                           clock_ms=clock or Clock(), **kwargs)

    def test_opt_in_permission_is_isolated_and_revoke_clears(self):
        with tempfile.TemporaryDirectory() as directory:
            store = self.make_store(directory, devices=('board-1', 'board-2'))
            self.assertTrue(store.configured('board-1'))
            self.assertFalse(store.configured('board-3'))
            self.assertEqual(store.permission('board-1'), 'enabled')
            first = store.session('board-1')
            self.assertTrue(first.append('one', 'first reply'))
            self.assertEqual(first.context(), (('one', 'first reply'),))
            self.assertEqual(store.session('board-2').context(), ())

            store.revoke('board-1')
            self.assertEqual(store.permission('board-1'), 'revoked')
            self.assertFalse(first.append('two', 'second reply'))
            self.assertEqual(first.context(), ())
            self.assertEqual(store.session('board-2').context(), ())
            store.close()

    def test_revocation_survives_restart_and_delete_keeps_permission(self):
        with tempfile.TemporaryDirectory() as directory:
            store = self.make_store(directory)
            store.append('board-1', 'keep?', 'no')
            store.delete('board-1', 'conversations')
            self.assertEqual(store.permission('board-1'), 'enabled')
            self.assertEqual(store.context('board-1'), ())
            store.append('board-1', 'remove', 'all')
            store.revoke('board-1')
            store.close()

            store = self.make_store(directory)
            self.assertEqual(store.permission('board-1'), 'revoked')
            self.assertEqual(store.context('board-1'), ())
            store.close()

    def test_expiry_record_limit_and_context_budget(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = Clock(1000)
            store = self.make_store(directory, clock=clock, retention_ms=100,
                                    max_records_per_device=2)
            store.append('board-1', 'expired', 'old')
            clock.now = 1100
            store.append('board-1', 'middle', 'reply')
            clock.now = 1101
            store.append('board-1', 'newest', 'reply')
            self.assertEqual(store.context('board-1'),
                             (('middle', 'reply'), ('newest', 'reply')))
            store.close()

        with tempfile.TemporaryDirectory() as directory:
            store = self.make_store(directory)
            half = MAX_CONTEXT_CHARS // 2
            store.append('board-1', 'a' * half, 'b' * half)
            store.append('board-1', 'c', 'd')
            self.assertEqual(store.context('board-1'), (('c', 'd'),))
            with self.assertRaises(ValueError):
                store.append('board-1', 'a' * (MAX_CONTEXT_CHARS + 1), 'b')
            store.close()

    def test_private_file_and_arguments_are_strict(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'memory.db'
            store = MemoryStore(path, ('board-1',))
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
            store.close()
            path.chmod(0o640)
            with self.assertRaises(MemoryStateError):
                MemoryStore(path, ('board-1',))
            link = Path(directory) / 'link.db'
            link.symlink_to(path)
            with self.assertRaises(MemoryStateError):
                MemoryStore(link, ('board-1',))
            public = Path(directory) / 'public'
            public.mkdir(mode=0o777)
            public.chmod(0o777)
            with self.assertRaises(MemoryStateError):
                MemoryStore(public / 'memory.db', ('board-1',))
            safe = Path(directory) / 'safe'
            safe.mkdir(mode=0o700)
            safe_link = Path(directory) / 'safe-link'
            safe_link.symlink_to(safe, target_is_directory=True)
            with self.assertRaises(MemoryStateError):
                MemoryStore(safe_link / 'memory.db', ('board-1',))
            for devices in ((), ('bad space',), ('board-1', 'board-1')):
                with self.subTest(devices=devices), self.assertRaises(ValueError):
                    MemoryStore(Path(directory) / 'other.db', devices)

    def test_sqlite_error_rolls_back_and_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            store = self.make_store(directory)
            store.append('board-1', 'before', 'reply')
            store.db.execute('DROP TABLE memory_turns')
            store.db.commit()
            with self.assertRaises(MemoryStateError):
                store.append('board-1', 'after', 'reply')
            with self.assertRaises(MemoryStateError):
                store.context('board-1')
            with self.assertRaises(MemoryStateError):
                store.permission('board-1')
            store.close()


if __name__ == '__main__':
    unittest.main()
