# SPDX-License-Identifier: Apache-2.0
import sqlite3
import tempfile
import unittest
from pathlib import Path

from shaniu_gateway.ota_transactions import OtaTransactionError, OtaTransactionStore


DEVICE = "board-1"
TXN = "1" * 32
MANIFEST = "a" * 64
VERSION = "18.6.390+450"


class OtaTransactionStoreTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "ota.sqlite"
        self.store = OtaTransactionStore(self.path)

    def tearDown(self):
        if self.store is not None:
            self.store.close()
        self.directory.cleanup()

    def create(self):
        return self.store.create(DEVICE, TXN, MANIFEST, VERSION, 100)

    def test_restart_persists_dispatch_identity_and_row(self):
        self.create()
        bound = self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 101)
        self.assertEqual(bound.state, "awaiting_first_report")
        self.store.close()
        self.store = OtaTransactionStore(self.path)
        loaded = self.store.load(DEVICE)
        self.assertEqual((loaded.transaction_id, loaded.manifest_sha256,
                          loaded.target_version, loaded.dispatch_sequence),
                         (TXN, MANIFEST, VERSION, 11))

    def test_startup_marks_nonterminal_uncertain_but_keeps_terminal(self):
        self.create()
        self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 101)
        self.assertEqual(self.store.mark_nonterminal_uncertain(102), 1)
        self.assertEqual(self.store.load(DEVICE).state, "uncertain")
        self.assertEqual(self.store.mark_nonterminal_uncertain(103), 0)
        self.store.delete(DEVICE)
        self.create()
        self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 101)
        self.store.advance(DEVICE, TXN, "downloading", 1, None, 102)
        self.store.advance(DEVICE, TXN, "failed", 2, -5, 103)
        self.assertEqual(self.store.mark_nonterminal_uncertain(104), 0)
        self.assertEqual(self.store.load(DEVICE).state, "failed")

    def test_phases_progress_monotonically_and_terminal_freezes(self):
        self.create()
        self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 101)
        self.store.advance(DEVICE, TXN, "downloading", 5, None, 102)
        self.store.advance(DEVICE, TXN, "downloading", 8, None, 103)
        with self.assertRaises(OtaTransactionError):
            self.store.advance(DEVICE, TXN, "downloading", 7, None, 104)
        self.store.advance(DEVICE, TXN, "verifying", 0, None, 105)
        self.store.advance(DEVICE, TXN, "staged", 100, None, 106)
        self.store.advance(DEVICE, TXN, "rebooting", 0, None, 107)
        self.store.advance(DEVICE, TXN, "trial", 100, None, 108)
        confirmed = self.store.advance(DEVICE, TXN, "confirmed", 100, 0, 109)
        self.assertEqual(confirmed.state, "confirmed")
        self.assertEqual(self.store.advance(DEVICE, TXN, "confirmed", 100, 0, 110), confirmed)
        with self.assertRaises(OtaTransactionError):
            self.store.advance(DEVICE, TXN, "failed", 100, -1, 111)

    def test_pending_terminal_survives_disconnect_and_restart(self):
        self.create()
        self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 101)
        self.store.advance(DEVICE, TXN, "downloading", 5, None, 102)
        self.store.advance(DEVICE, TXN, "verifying", 100, None, 103)
        self.store.advance(DEVICE, TXN, "staged", 100, None, 104)
        self.store.advance(DEVICE, TXN, "rebooting", 100, None, 105)
        self.store.advance(DEVICE, TXN, "trial", 100, None, 106)
        pending = self.store.advance(DEVICE, TXN, "confirming", 100, 0, 107)
        self.assertEqual(self.store.mark_uncertain(DEVICE, TXN, 108), pending)
        self.assertEqual(self.store.mark_nonterminal_uncertain(109), 0)
        self.store.close()
        self.store = OtaTransactionStore(self.path)
        self.assertEqual(self.store.load(DEVICE).state, "confirming")
        with self.assertRaises(OtaTransactionError):
            self.store.redispatch(DEVICE, TXN, 8, 10, 12, 110)
        self.assertEqual(self.store.advance(
            DEVICE, TXN, "confirmed", 100, 0, 111).state, "confirmed")

    def test_create_and_dispatch_are_idempotent_or_conflict(self):
        created = self.create()
        self.assertEqual(self.create(), created)
        with self.assertRaises(OtaTransactionError):
            self.store.create(DEVICE, "2" * 32, MANIFEST, VERSION, 101)
        bound = self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 102)
        self.assertEqual(self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 103), bound)
        with self.assertRaises(OtaTransactionError):
            self.store.bind_dispatch(DEVICE, TXN, 7, 9, 12, 104)

    def test_uncertain_transaction_can_only_redispatch_same_identity(self):
        self.create()
        self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 101)
        self.store.advance(DEVICE, TXN, "downloading", 37, None, 102)
        uncertain = self.store.mark_uncertain(DEVICE, TXN, 103)
        self.assertEqual((uncertain.state, uncertain.progress_percent),
                         ("uncertain", 37))
        self.assertEqual(self.store.mark_uncertain(DEVICE, TXN, 104), uncertain)
        with self.assertRaises(OtaTransactionError):
            self.store.redispatch(DEVICE, "2" * 32, 8, 10, 12, 105)
        rebound = self.store.redispatch(DEVICE, TXN, 8, 10, 12, 106)
        self.assertEqual((rebound.state, rebound.progress_percent,
                          rebound.dispatch_boot_generation,
                          rebound.dispatch_session_id, rebound.dispatch_sequence),
                         ("awaiting_first_report", 0, 8, 10, 12))

    def test_new_transaction_replaces_terminal_record_only(self):
        self.create()
        self.store.bind_dispatch(DEVICE, TXN, 7, 9, 11, 101)
        self.store.advance(DEVICE, TXN, "downloading", 1, None, 102)
        with self.assertRaises(OtaTransactionError):
            self.store.create(DEVICE, "2" * 32, "3" * 64, "18.6.391+451", 103)
        self.store.advance(DEVICE, TXN, "failed", 1, -5, 104)
        created = self.store.create(
            DEVICE, "2" * 32, "3" * 64, "18.6.391+451", 105)
        self.assertEqual((created.transaction_id, created.state),
                         ("2" * 32, "dispatching"))

    def test_malformed_input_and_corrupt_row_fail_closed(self):
        with self.assertRaises(OtaTransactionError):
            self.store.create(DEVICE, "bad", MANIFEST, VERSION, 100)
        with self.assertRaises(OtaTransactionError):
            self.store.create(DEVICE, TXN, "A" * 64, VERSION, 100)
        with self.assertRaises(OtaTransactionError):
            self.store.create(DEVICE, TXN, MANIFEST, "18.6.390", 100)
        self.store.close()
        connection = sqlite3.connect(self.path)
        connection.execute(
            "INSERT INTO ota_transaction VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
            (DEVICE, TXN, MANIFEST, VERSION, "bad-state", 0, None, 1, 1,
             None, None, None),
        )
        connection.commit()
        connection.close()
        with self.assertRaises(OtaTransactionError):
            OtaTransactionStore(self.path)
        self.store = None

    def test_delete_is_scoped_and_idempotent(self):
        self.create()
        self.assertTrue(self.store.delete(DEVICE))
        self.assertIsNone(self.store.load(DEVICE))
        self.assertFalse(self.store.delete(DEVICE))


if __name__ == "__main__":
    unittest.main()
