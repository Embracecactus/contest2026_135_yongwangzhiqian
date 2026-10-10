#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The real OTA writer identifies its mode without replay or payload logging."""
import importlib
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/bk7258'))
usb = importlib.import_module('_lib.deploy_usb')


class DeployWriteErrors(unittest.TestCase):
    def test_timeout_keeps_unknown_count_and_names_ota_phase(self):
        port = Mock()
        port.write.side_effect = OSError('Write timeout')
        with self.assertRaisesRegex(OSError, 'phase=hello.*requested=64.*completed=unknown.*BK7258_OTA_SOURCE_USB') as caught:
            usb.write_frame(port, usb.HELLO, 1)
        self.assertIn('workbench', str(caught.exception))
        self.assertEqual(port.write.call_count, 1)
        port.flush.assert_not_called()
        port.reset_output_buffer.assert_not_called()

    def test_partial_write_is_failed_without_replay_or_payload(self):
        port = Mock()
        port.write.return_value = 16
        with self.assertRaisesRegex(OSError, 'phase=data.*requested=64.*completed=16') as caught:
            usb.write_frame(port, usb.DATA, 2, payload=b'private-payload')
        self.assertNotIn('private-payload', str(caught.exception))
        self.assertEqual(port.write.call_count, 1)

    def test_complete_write_preserves_wire(self):
        port = Mock()
        port.write.return_value = 64
        usb.write_frame(port, usb.HELLO, 1)
        wire = port.write.call_args.args[0]
        self.assertEqual(len(wire), 64)
        self.assertEqual(usb.HEADER.unpack(wire[:usb.HEADER.size])[:4],
                         (usb.MAGIC, usb.VERSION, usb.HELLO, 1))
        port.flush.assert_not_called()


if __name__ == '__main__':
    unittest.main()
