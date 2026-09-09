# SPDX-License-Identifier: Apache-2.0
import json
import tempfile
import unittest
from pathlib import Path

from shaniu_gateway.devices import DeviceBindings, load_device_bindings


class DeviceBindingsTest(unittest.TestCase):
    def test_validation_and_immutable_copy(self):
        pins = {'a' * 64: 'board-1'}
        bindings = DeviceBindings(pins)
        pins['a' * 64] = 'board-2'
        self.assertEqual(bindings.device_for_certificate('a' * 64), 'board-1')
        self.assertIsNone(bindings.device_for_certificate('b' * 64))
        for invalid in ({}, {'z' * 64: 'board-1'}, {'a' * 64: '../board'},
                        {'a' * 64: 'board-1', 'b' * 64: 'board-1'},
                        {'a' * 64: 123}):
            with self.subTest(invalid=type(invalid)), self.assertRaises(ValueError):
                DeviceBindings(invalid)

    def test_loader_and_file_boundaries(self):
        data = {'format': 'shaniu.device-bindings/1', 'devices': [
            {'device_id': 'board-1', 'client_certificate_sha256': 'a' * 64}]}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'devices.json'
            path.write_text(json.dumps(data))
            path.chmod(0o600)
            self.assertEqual(load_device_bindings(path).device_for_certificate('a' * 64),
                             'board-1')
            link = Path(tmp) / 'link'
            link.symlink_to(path)
            with self.assertRaises(OSError):
                load_device_bindings(link)
            path.chmod(0o666)
            with self.assertRaises(ValueError):
                load_device_bindings(path)
            path.chmod(0o600)
            for raw in ('{}', '{"format":1,"format":2}', ' ' * 65537,
                        json.dumps(dict(data, unexpected=True)),
                        json.dumps(dict(data, devices=data['devices'] * 2))):
                path.write_text(raw)
                with self.assertRaises(ValueError):
                    load_device_bindings(path)


if __name__ == '__main__':
    unittest.main()
