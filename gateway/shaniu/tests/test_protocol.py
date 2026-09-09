# SPDX-License-Identifier: Apache-2.0

import unittest
import struct

from shaniu_gateway.protocol import (
    AUDIO_FRAME_BYTES,
    CAP_VOLUME,
    CAP_OTA,
    CAP_STATUS_REPORT,
    HEADER_BYTES,
    MAGIC,
    MAX_WINDOW,
    VERSION,
    Flag,
    Frame,
    GatewaySession,
    MessageType,
    ProtocolError,
    SessionEvent,
    SessionState,
    StatusReport,
    decode_status_report,
    decode_ota_report,
    decode_window_credit,
    encode_window_credit,
)


class FrameTest(unittest.TestCase):
    def test_ota_capability_and_report_contract(self) -> None:
        session = GatewaySession()
        session.receive(Frame(MessageType.HELLO, 0, 7, 1, 0, 1, 0,
                              struct.pack('!I', CAP_OTA)))
        request = session.make_ota_request('a' * 64)
        self.assertEqual((request.message_type, request.flags, request.turn_id,
                          request.payload), (MessageType.OTA_REQUEST, 0, 0, b'\xaa' * 32))
        payload = struct.pack('!IiBBH32s', request.sequence, 0, 1, 0, 0, b'\xaa' * 32)
        report = Frame(MessageType.OTA_REPORT, 0, 7, 1, 0, 2, 0, payload)
        self.assertEqual(session.receive(report).event, SessionEvent.OTA_REPORT)
        self.assertEqual(decode_ota_report(payload).manifest_sha256, 'a' * 64)
        for result, phase, progress, reserved in ((1, 1, 0, 0), (-1, 1, 0, 0),
                                                   (0, 8, 0, 0), (0, 1, 101, 0),
                                                   (0, 1, 0, 1)):
            with self.subTest(values=(result, phase, progress, reserved)):
                with self.assertRaisesRegex(ProtocolError, 'invalid_ota_report'):
                    Frame(MessageType.OTA_REPORT, 0, 7, 1, 0, 3, 0,
                          struct.pack('!IiBBH32s', request.sequence, result, phase,
                                      progress, reserved, b'\xaa' * 32)).encode()
        legacy = GatewaySession()
        legacy.receive(Frame(MessageType.HELLO, 0, 7, 1, 0, 1, 0))
        with self.assertRaisesRegex(ProtocolError, 'ota_not_supported'):
            legacy.make_ota_request('a' * 64)

    def test_status_report_requires_declared_capability_and_valid_contract(self) -> None:
        payload = struct.pack('!BBBBIHHHHI', 1, 73, 1, 4, 3800, 1, 2, 3, 0, 4)
        frame = Frame(MessageType.STATUS_REPORT, 0, 7, 1, 0, 2, 0, payload)
        self.assertEqual(Frame.decode(frame.encode()), frame)
        self.assertEqual(decode_status_report(payload), StatusReport(
            battery_percent=73,
            charging=True,
            battery_state='charging',
            battery_voltage_mv=3800,
            firmware_version='1.2.3+4',
            firmware_root_sha256=None,
        ))

        root = bytes(range(32))
        payload_v2 = struct.pack(
            '!BBBBIHHHHI32s', 2, 73, 1, 4, 3800, 1, 2, 3, 0, 4, root,
        )
        self.assertEqual(
            decode_status_report(payload_v2).firmware_root_sha256, root.hex())
        self.assertEqual(
            Frame.decode(Frame(MessageType.STATUS_REPORT, 0, 7, 1, 0, 2, 0,
                               payload_v2).encode()).payload,
            payload_v2,
        )

        legacy = GatewaySession()
        legacy.receive(Frame(MessageType.HELLO, 0, 7, 1, 0, 1, 0))
        with self.assertRaisesRegex(ProtocolError, 'status_report_not_supported'):
            legacy.receive(frame)

        session = GatewaySession()
        session.receive(Frame(MessageType.HELLO, 0, 7, 1, 0, 1, 0,
                              struct.pack('!I', CAP_STATUS_REPORT)))
        self.assertEqual(session.receive(frame).event, SessionEvent.STATUS_REPORT)
        with self.assertRaisesRegex(ProtocolError, 'invalid_direction'):
            session._outbound(MessageType.STATUS_REPORT, turn_id=0)

        for changed in (
            struct.pack('!BBBBIHHHHI', 2, 73, 1, 4, 3800, 1, 2, 3, 0, 4),
            struct.pack('!BBBBIHHHHI', 1, 73, 0, 4, 3800, 1, 2, 3, 0, 4),
            struct.pack('!BBBBIHHHHI', 1, 73, 0, 0xff, 3800, 1, 2, 3, 0, 4),
            struct.pack('!BBBBIHHHHI', 1, 73, 1, 4, 3800, 0xffff, 2, 3, 0, 4),
            struct.pack('!BBBBIHHHHI32s', 2, 73, 1, 4, 3800, 1, 2, 3, 0, 4,
                        bytes(32)),
            struct.pack('!BBBBIHHHHI32s', 2, 73, 1, 4, 3800, 0xffff, 0xffff,
                        0xffff, 0, 0xffffffff, root),
        ):
            with self.subTest(payload=changed), self.assertRaisesRegex(ProtocolError, 'invalid_status_report'):
                Frame(MessageType.STATUS_REPORT, 0, 7, 1, 0, 2, 0, changed).encode()
        with self.assertRaisesRegex(ProtocolError, 'invalid_status_report'):
            Frame(MessageType.STATUS_REPORT, int(Flag.SYNTHETIC), 7, 1, 0, 2, 0, payload).encode()

    def test_volume_capabilities_and_report_contract(self) -> None:
        legacy = GatewaySession()
        legacy.receive(Frame(MessageType.HELLO, 0, 7, 1, 0, 1, 0))
        with self.assertRaisesRegex(ProtocolError, 'volume_not_supported'):
            legacy.make_volume_request(50)

        session = GatewaySession()
        session.receive(Frame(MessageType.HELLO, 0, 7, 1, 0, 1, 0,
                              struct.pack('!I', CAP_VOLUME)))
        request = session.make_volume_request(65)
        self.assertEqual(request.message_type, MessageType.VOLUME_SET)
        self.assertEqual(request.turn_id, 0)
        self.assertEqual(request.payload, b'\x00\x00\x00A')
        report = Frame(MessageType.VOLUME_REPORT, 0, 7, 1, 0, 2, 0,
                       struct.pack('!IiI', request.sequence, 0, 65))
        self.assertEqual(Frame.decode(report.encode()), report)
        self.assertEqual(session.receive(report).event, SessionEvent.VOLUME_REPORT)
        self.assertEqual(session.make_volume_request().message_type, MessageType.VOLUME_GET)
        for value in (-1, 101, True, 1.5, '0' * 64, 'f' * 64):
            with self.subTest(value=value), self.assertRaises(ProtocolError):
                session.make_volume_request(value)

    def test_volume_rejects_invalid_wire_values(self) -> None:
        for request, result, volume in ((0, 0, 50), (0xffffffff, 0, 50),
                                        (3, 1, 50), (3, 0, 101), (3, -5, 50)):
            with self.subTest(values=(request, result, volume)):
                with self.assertRaisesRegex(ProtocolError, 'invalid_volume_report'):
                    Frame(MessageType.VOLUME_REPORT, 0, 7, 1, 0, 2, 0,
                          struct.pack('!IiI', request, result, volume)).encode()
        failure = Frame(MessageType.VOLUME_REPORT, 0, 7, 1, 0, 2, 0,
                        struct.pack('!IiI', 3, -5, 0xffffffff))
        self.assertEqual(Frame.decode(failure.encode()), failure)
        for kind, turn, payload in ((MessageType.VOLUME_GET, 1, b''),
                                    (MessageType.VOLUME_GET, 0, b'x'),
                                    (MessageType.VOLUME_SET, 0, b'xxx'),
                                    (MessageType.VOLUME_SET, 0, struct.pack('!I', 101)),
                                    (MessageType.HELLO, 0, b'x')):
            with self.subTest(kind=kind, turn=turn), self.assertRaises(ProtocolError):
                Frame(kind, 0, 7, 1, turn, 2, 0, payload).encode()

    def test_wire_layout_matches_the_board_contract(self) -> None:
        frame = Frame(
            message_type=MessageType.HELLO,
            flags=0,
            boot_generation=0x01020304,
            session_id=0x11121314,
            turn_id=0,
            sequence=0x21222324,
            timestamp_ms=0x3132333435363738,
        )

        wire = frame.encode()

        self.assertEqual(len(wire), HEADER_BYTES)
        self.assertEqual(wire[0:4], MAGIC.to_bytes(4, "big"))
        self.assertEqual(wire[4], VERSION)
        self.assertEqual(wire[5], MessageType.HELLO)
        self.assertEqual(wire[8:10], HEADER_BYTES.to_bytes(2, "big"))
        self.assertEqual(wire[16:20], b"\x01\x02\x03\x04")
        self.assertEqual(wire[20:24], b"\x11\x12\x13\x14")
        self.assertEqual(wire[28:32], b"\x21\x22\x23\x24")
        self.assertEqual(wire[32:40], b"\x31\x32\x33\x34\x35\x36\x37\x38")
        self.assertEqual(Frame.decode(wire), frame)

    def test_decode_rejects_size_flags_and_unsupported_vision(self) -> None:
        hello = Frame(
            MessageType.HELLO,
            0,
            boot_generation=1,
            session_id=1,
            turn_id=0,
            sequence=1,
            timestamp_ms=1,
        ).encode()
        with self.assertRaisesRegex(ProtocolError, "frame_size_mismatch"):
            Frame.decode(hello + b"extra")

        bad_flags = bytearray(hello)
        bad_flags[6:8] = (4).to_bytes(2, "big")
        with self.assertRaisesRegex(ProtocolError, "invalid_flags"):
            Frame.decode(bytes(bad_flags))

        vision = bytearray(hello)
        vision[5] = MessageType.VISION_START
        vision[24:28] = (1).to_bytes(4, "big")
        with self.assertRaisesRegex(ProtocolError, "unsupported_message"):
            Frame.decode(bytes(vision))

    def test_audio_and_synthetic_rules_match_the_board_contract(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "invalid_audio_frame"):
            Frame(
                MessageType.AUDIO_UP,
                0,
                1,
                1,
                1,
                1,
                1,
                b"short",
            ).encode()

        with self.assertRaisesRegex(ProtocolError, "invalid_synthetic_flag"):
            Frame(
                MessageType.TTS_START,
                0,
                1,
                1,
                1,
                1,
                1,
            ).encode()

        audio = Frame(
            MessageType.AUDIO_DOWN,
            int(Flag.SYNTHETIC | Flag.END_OF_STREAM),
            1,
            1,
            1,
            1,
            1,
            bytes(AUDIO_FRAME_BYTES),
        )
        self.assertEqual(Frame.decode(audio.encode()), audio)


class GatewaySessionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = GatewaySession()
        hello = Frame(MessageType.HELLO, 0, 7, 3, 0, 1, 1)
        result = self.session.receive(hello)
        self.assertEqual(result.event, SessionEvent.HELLO)
        self.assertEqual(
            [item.message_type for item in result.outbound],
            [MessageType.WELCOME, MessageType.WINDOW_UPDATE],
        )
        self.assertEqual(decode_window_credit(result.outbound[1].payload), MAX_WINDOW)

    def test_gap_replay_stale_identity_and_window_overflow_fail_closed(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "sequence_gap"):
            self.session.receive(Frame(MessageType.HEARTBEAT, 0, 7, 3, 0, 3, 1))

        self.session.receive(Frame(MessageType.HEARTBEAT, 0, 7, 3, 0, 2, 1))
        with self.assertRaisesRegex(ProtocolError, "sequence_replay"):
            self.session.receive(Frame(MessageType.HEARTBEAT, 0, 7, 3, 0, 2, 1))

        with self.assertRaisesRegex(ProtocolError, "stale_identity"):
            self.session.receive(Frame(MessageType.HEARTBEAT, 0, 8, 3, 0, 3, 1))

        self.session.receive(
            Frame(
                MessageType.WINDOW_UPDATE,
                0,
                7,
                3,
                0,
                3,
                1,
                encode_window_credit(MAX_WINDOW),
            )
        )
        with self.assertRaisesRegex(ProtocolError, "window_overflow"):
            self.session.receive(
                Frame(
                    MessageType.WINDOW_UPDATE,
                    0,
                    7,
                    3,
                    0,
                    4,
                    1,
                    encode_window_credit(1),
                )
            )

    def test_remote_cancel_keeps_sequence_and_new_turn_boundaries(self) -> None:
        with self.assertRaisesRegex(ProtocolError, 'stale_turn'):
            self.session.make_cancel(0)
        self.session.receive(Frame(MessageType.TURN_START, 0, 7, 3, 1, 2, 1))
        cancel = self.session.make_cancel(1)
        self.assertEqual(cancel.message_type, MessageType.CANCEL)
        crossed = Frame(MessageType.AUDIO_UP, 0, 7, 3, 1, 3, 1,
                        bytes(AUDIO_FRAME_BYTES))
        self.assertEqual(self.session.receive(crossed).event, SessionEvent.DISCARDED_AUDIO)
        with self.assertRaisesRegex(ProtocolError, 'sequence_replay'):
            self.session.receive(crossed)
        self.session.receive(Frame(MessageType.CANCEL, 0, 7, 3, 1, 4, 1))
        self.session.receive(Frame(MessageType.TURN_START, 0, 7, 3, 2, 5, 1))
        with self.assertRaisesRegex(ProtocolError, 'stale_turn'):
            self.session.make_cancel(1)
        with self.assertRaisesRegex(ProtocolError, 'stale_turn'):
            self.session.receive(Frame(MessageType.AUDIO_UP, 0, 7, 3, 1, 6, 1,
                                       bytes(AUDIO_FRAME_BYTES)))
        self.assertEqual(self.session.state, SessionState.UPLINK)
        self.assertEqual(self.session.turn_id, 2)

    def test_complete_turn_consumes_both_direction_windows(self) -> None:
        self.session.receive(
            Frame(
                MessageType.WINDOW_UPDATE,
                0,
                7,
                3,
                0,
                2,
                1,
                encode_window_credit(AUDIO_FRAME_BYTES),
            )
        )
        self.session.receive(Frame(MessageType.TURN_START, 0, 7, 3, 1, 3, 1))
        audio_result = self.session.receive(
            Frame(
                MessageType.AUDIO_UP,
                0,
                7,
                3,
                1,
                4,
                1,
                bytes(AUDIO_FRAME_BYTES),
            )
        )
        self.assertEqual(
            audio_result.outbound[0].message_type, MessageType.WINDOW_UPDATE
        )
        self.session.receive(Frame(MessageType.TURN_END, 0, 7, 3, 1, 5, 1))
        self.assertEqual(self.session.state, SessionState.THINKING)

        self.session.make_tts_start(1)
        audio_down = self.session.make_audio_down(
            1,
            bytes(AUDIO_FRAME_BYTES),
            final=True,
        )
        self.assertTrue(audio_down.flags & Flag.END_OF_STREAM)
        self.session.make_tts_end(1)
        self.assertEqual(self.session.state, SessionState.IDLE)


if __name__ == "__main__":
    unittest.main()
