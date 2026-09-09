# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from shaniu_gateway.console import (
    ConsoleEpochs,
    ConsoleGrant,
    ConsoleStateError,
    load_console_grants,
)
from shaniu_gateway.firmware import (
    FirmwareRelease,
    FirmwareReleases,
    load_firmware_releases,
)


def firmware_release(**changes):
    value = dict(
        device_id='board-1',
        manifest_sha256='a' * 64,
        target_version='18.6.390+450',
        required_source_version='18.6.389+449',
        required_source_root_sha256='b' * 64,
        board_family='bk7258',
        physical_board='aidk_ai_toy',
        layout_identity='bk7258-0123456789abcdef',
        layout_sha256='c' * 64,
        package_sha256='d' * 64,
        package_size_bytes=2_457_600,
    )
    value.update(changes)
    return value


class ConsoleConfigurationTest(unittest.TestCase):
    def test_grant_file_validation(self):
        grant = dict(token_sha256=hashlib.sha256(b'test-only-token').hexdigest(),
                     device_id='board-1', write=True, expires_at_ms=10000)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'access.json'
            def save(value):
                path.write_text(json.dumps(value))
                path.chmod(0o600)
            save(dict(format='shaniu.console-access/1', grants=[grant]))
            self.assertEqual(load_console_grants(path), (ConsoleGrant(**grant),))
            for invalid in ([grant, grant], [dict(grant, write=1)],
                            [dict(grant, expires_at_ms=True)], [dict(grant, extra=True)], []):
                save(dict(format='shaniu.console-access/1', grants=invalid))
                with self.assertRaises(ValueError):
                    load_console_grants(path)

    def test_epoch_persists_and_unsafe_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'epoch.db'
            epochs = ConsoleEpochs(path)
            first = epochs.next()
            epochs.close()
            epochs = ConsoleEpochs(path)
            self.assertGreater(epochs.next(), first)
            epochs.close()
            path.chmod(0o644)
            with self.assertRaises(ValueError):
                ConsoleEpochs(path)
            link = Path(directory) / 'link'
            link.symlink_to(path)
            with self.assertRaises(OSError):
                ConsoleEpochs(link)

    def test_persona_persists_and_corrupt_state_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'console.db'
            state = ConsoleEpochs(path)
            self.assertIsNone(state.persona('board-1'))
            state.set_persona('board-1', 'quiet')
            with self.assertRaises(ValueError):
                state.set_persona('board-1', 'unknown')
            state.close()

            state = ConsoleEpochs(path)
            self.assertEqual(state.persona('board-1'), 'quiet')
            state.db.execute("UPDATE console_persona SET mode='invalid' WHERE device_id='board-1'")
            state.db.commit()
            with self.assertRaises(ConsoleStateError):
                state.persona('board-1')
            state.close()

    def test_firmware_registry_is_strict_and_source_identity_bound(self):
        release = firmware_release()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'firmware.json'

            def save(value):
                path.write_text(json.dumps(value))
                path.chmod(0o600)

            save(dict(format='shaniu.firmware-release-registry/1',
                      releases=[release]))
            registry = load_firmware_releases(path)
            self.assertEqual(registry.device_ids, frozenset({'board-1'}))
            root = release['required_source_root_sha256']
            self.assertEqual(registry.compatible(
                                 'board-1', '18.6.389+449', root),
                             (FirmwareRelease(**release),))
            self.assertEqual(registry.compatible('board-1', None, root), ())
            self.assertEqual(registry.compatible(
                                 'board-1', '18.6.388+448', root), ())
            self.assertEqual(registry.compatible(
                                 'board-1', '18.6.389+449', '0' * 64), ())
            self.assertEqual(registry.compatible(
                                 'board-2', '18.6.389+449', root), ())

            link = Path(directory) / 'firmware-link.json'
            link.symlink_to(path)
            with self.assertRaises(OSError):
                load_firmware_releases(link)
            path.chmod(0o622)
            with self.assertRaises(ValueError):
                load_firmware_releases(path)
            path.chmod(0o600)

            invalid = (
                {},
                dict(format='shaniu.firmware-release-registry/1', releases=[]),
                dict(format='shaniu.firmware-release-registry/1',
                     releases=[dict(release, unexpected=True)]),
                dict(format='shaniu.firmware-release-registry/1',
                     releases=[dict(release, manifest_sha256='A' * 64)]),
                dict(format='shaniu.firmware-release-registry/1',
                     releases=[dict(release, manifest_sha256='0' * 64)]),
                dict(format='shaniu.firmware-release-registry/1',
                     releases=[dict(release, manifest_sha256='f' * 64)]),
                dict(format='shaniu.firmware-release-registry/1',
                     releases=[dict(release, package_size_bytes=True)]),
                dict(format='shaniu.firmware-release-registry/1',
                     releases=[dict(release, target_version='18.6.388+448')]),
                dict(format='shaniu.firmware-release-registry/1',
                     releases=[release, release]),
                dict(format='shaniu.firmware-release-registry/1',
                     releases=[dict(release, manifest_sha256=f'{index:064x}',
                                    target_version=f'18.6.{390 + index}+{450 + index}')
                               for index in range(33)]),
            )
            for value in invalid:
                save(value)
                with self.subTest(value=value), self.assertRaises(ValueError):
                    load_firmware_releases(path)

            path.write_text('{"format":"x","format":"y"}')
            with self.assertRaises(ValueError):
                load_firmware_releases(path)

    def test_firmware_registry_rejects_duplicate_device_target(self):
        first = FirmwareRelease(**firmware_release())
        second = FirmwareRelease(**firmware_release(manifest_sha256='e' * 64))
        with self.assertRaises(ValueError):
            FirmwareReleases((first, second))
