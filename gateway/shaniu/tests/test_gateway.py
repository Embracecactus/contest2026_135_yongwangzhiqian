# SPDX-License-Identifier: Apache-2.0

import asyncio
import base64
import io
import hashlib
import json
import sqlite3
import ssl
import struct
import subprocess
import tempfile
import time
import unittest
import wave
import zipfile
from dataclasses import replace
from pathlib import Path

import websockets
from aiohttp import web, ClientSession
from websockets.exceptions import ConnectionClosedError

from shaniu_gateway.protocol import (
    AUDIO_FRAME_BYTES,
    CAP_STATUS_REPORT,
    CAP_OTA,
    CAP_VOLUME,
    MAX_WINDOW,
    Flag,
    Frame,
    MessageType,
    ProtocolError,
    SessionState,
    StatusReport,
    decode_window_credit,
    encode_window_credit,
    monotonic_ms,
)
from shaniu_gateway.server import (
    DEFAULT_PATH,
    SUBPROTOCOL,
    GatewayConfig,
    GatewayConnection,
    GatewayServer,
    OtaStatus,
    _bind_scope,
    build_tls_context,
    deterministic_pcm_frame,
    start_gateway_server,
)
from shaniu_gateway.mimo import MiMoConfig, MiMoProvider, ProviderError
from shaniu_gateway.memory import MemoryStore
from shaniu_gateway.ota_transactions import OtaTransactionStore
from shaniu_gateway.devices import DeviceBindings
from shaniu_gateway.console import ConsoleService, ConsoleGrant
from shaniu_gateway.firmware import FirmwareRelease, FirmwareReleases
from shaniu_gateway.firmware_content import FirmwareContentError, FirmwareContentStore


class FixtureReplySession:
    def __init__(self, callback):
        self.callback = callback
        self.finished = []
        self.closed = False

    def reply(self, pcm):
        return self.callback(pcm)

    def finish(self, turn_id, *, success):
        self.finished.append((turn_id, success))

    def close(self):
        self.closed = True


class BusyPersonaReplySession(FixtureReplySession):
    def __init__(self):
        async def unused(_pcm):
            if False:
                yield b''
        super().__init__(unused)
        self.persona_mode = 'gentle'
        self.busy = True
        self.persona_attempts = 0

    def set_persona(self, mode):
        self.persona_attempts += 1
        if self.busy:
            raise ProviderError('conversation_unavailable')
        self.persona_mode = mode


class ClientWire:
    def __init__(self, boot_generation: int = 7, session_id: int = 3) -> None:
        self.boot_generation = boot_generation
        self.session_id = session_id
        self.next_sequence = 1

    def frame(
        self,
        message_type: MessageType,
        *,
        turn_id: int = 0,
        flags: int = 0,
        payload: bytes = b"",
        sequence: int | None = None,
    ) -> Frame:
        if sequence is None:
            sequence = self.next_sequence
            self.next_sequence += 1
        return Frame(
            message_type=message_type,
            flags=flags,
            boot_generation=self.boot_generation,
            session_id=self.session_id,
            turn_id=turn_id,
            sequence=sequence,
            timestamp_ms=monotonic_ms(),
            payload=payload,
        )


class OtaSocket:
    def __init__(self, *, open=True):
        self.open = open
        self.sent = []

    async def send(self, wire):
        self.sent.append(wire)


class GatewayIntegrationTest(unittest.IsolatedAsyncioTestCase):
    async def test_ota_timeout_and_offline_fail_closed(self):
        socket = OtaSocket()
        connection = GatewayConnection(socket, DEFAULT_PATH,
                                       GatewayConfig(ota_timeout_seconds=0.01),
                                       lambda _event: None)
        connection.session.receive(Frame(MessageType.HELLO, 0, 7, 1, 0, 1, 0,
                                         struct.pack('!I', CAP_OTA)))
        with self.assertRaisesRegex(ProtocolError, 'ota_timeout'):
            await connection.request_ota('a' * 64)
        self.assertEqual(Frame.decode(socket.sent[0]).message_type, MessageType.OTA_REQUEST)
        socket.open = False
        with self.assertRaisesRegex(ProtocolError, 'device_offline'):
            await connection.request_ota('a' * 64)

    @classmethod
    def setUpClass(cls) -> None:
        cls.temp_dir = tempfile.TemporaryDirectory(prefix="shaniu-gateway-test-")
        cls.cert_path = Path(cls.temp_dir.name) / "cert.pem"
        cls.key_path = Path(cls.temp_dir.name) / "key.pem"
        subprocess.run(
            [
                "openssl",
                "req",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-nodes",
                "-keyout",
                str(cls.key_path),
                "-out",
                str(cls.cert_path),
                "-days",
                "1",
                "-subj",
                "/CN=localhost",
                "-addext",
                "subjectAltName=DNS:localhost",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        cls.ca_path = Path(cls.temp_dir.name) / "client-ca.pem"
        cls.ca_key_path = Path(cls.temp_dir.name) / "client-ca.key"
        cls.mtls_cert_path = Path(cls.temp_dir.name) / "mtls-server.pem"
        cls.mtls_key_path = Path(cls.temp_dir.name) / "mtls-server.key"
        cls.trusted_client_cert = Path(cls.temp_dir.name) / "trusted-client.pem"
        cls.trusted_client_key = Path(cls.temp_dir.name) / "trusted-client.key"
        cls.untrusted_client_cert = Path(cls.temp_dir.name) / "untrusted-client.pem"
        cls.untrusted_client_key = Path(cls.temp_dir.name) / "untrusted-client.key"
        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", str(cls.ca_key_path), "-out", str(cls.ca_path),
                "-days", "1", "-subj", "/CN=shaniu-test-client-ca",
            ],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        cls._issue_certificate(
            cls.mtls_cert_path, cls.mtls_key_path, "localhost",
            "subjectAltName=DNS:localhost",
        )
        cls._issue_certificate(
            cls.trusted_client_cert, cls.trusted_client_key, "trusted-client"
        )
        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", str(cls.untrusted_client_key),
                "-out", str(cls.untrusted_client_cert), "-days", "1",
                "-subj", "/CN=untrusted-client",
            ],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        # New certificates have second-resolution notBefore timestamps.
        # Wait for their actual validity boundary, never disable TLS checks.
        valid_at = 0.0
        for certificate in (cls.cert_path, cls.ca_path, cls.mtls_cert_path,
                            cls.trusted_client_cert, cls.untrusted_client_cert):
            date = subprocess.run(
                ['openssl', 'x509', '-in', str(certificate), '-noout', '-startdate'],
                check=True, capture_output=True, text=True).stdout.strip()
            valid_at = max(valid_at, ssl.cert_time_to_seconds(date.split('=', 1)[1]))
        remaining = valid_at + 1 - time.time()
        if remaining > 3:
            raise RuntimeError('test certificate validity exceeds clock tolerance')
        if remaining > 0:
            time.sleep(remaining)

    @classmethod
    def _issue_certificate(
        cls, certificate: Path, key: Path, common_name: str,
        extension: str | None = None,
    ) -> None:
        request = certificate.with_suffix(".csr")
        command = [
            "openssl", "req", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(key), "-out", str(request),
            "-subj", f"/CN={common_name}",
        ]
        subprocess.run(
            command, check=True, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        command = [
            "openssl", "x509", "-req", "-in", str(request), "-CA",
            str(cls.ca_path), "-CAkey", str(cls.ca_key_path),
            "-CAcreateserial", "-out", str(certificate), "-days", "1",
        ]
        if extension is not None:
            extfile = certificate.with_suffix(".ext")
            extfile.write_text(extension + "\n")
            command += ["-extfile", str(extfile)]
        subprocess.run(
            command, check=True, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temp_dir.cleanup()

    async def asyncSetUp(self) -> None:
        self.events: list[dict[str, object]] = []
        self.loop_errors: list[dict[str, object]] = []
        self.loop = asyncio.get_running_loop()
        self.previous_exception_handler = self.loop.get_exception_handler()
        self.loop.set_exception_handler(
            lambda _loop, context: self.loop_errors.append(dict(context))
        )
        config = GatewayConfig(
            reply_frames=4,
            reply_interval_ms=0,
            downlink_window_timeout_ms=500,
        )
        self.gateway = GatewayServer(
            config, event_sink=lambda event: self.events.append(dict(event))
        )
        self.server = await start_gateway_server(
            self.gateway,
            "127.0.0.1",
            0,
            build_tls_context(self.cert_path, self.key_path),
        )
        self.port = self.server.sockets[0].getsockname()[1]
        self.client_tls = ssl.create_default_context(cafile=self.cert_path)
        self.client_tls.minimum_version = ssl.TLSVersion.TLSv1_2

    async def asyncTearDown(self) -> None:
        self.server.close()
        await self.server.wait_closed()
        self.loop.set_exception_handler(self.previous_exception_handler)
        unexpected = [
            context
            for context in self.loop_errors
            if not isinstance(context.get("exception"), ConnectionResetError)
            and not (
                getattr(self, "expect_tls_handshake_rejection", False)
                and isinstance(context.get("exception"), ssl.SSLError)
                and context["exception"].reason in {
                    "PEER_DID_NOT_RETURN_A_CERTIFICATE",
                    "CERTIFICATE_VERIFY_FAILED",
                }
            )
        ]
        if unexpected:
            kinds = [(type(c.get('exception')).__name__,
                      getattr(c.get('exception'), 'reason', None)) for c in unexpected]
            self.fail(f"unexpected asyncio loop error: {kinds}")

    def connect(self):
        return websockets.connect(
            f"wss://localhost:{self.port}{DEFAULT_PATH}",
            ssl=self.client_tls,
            subprotocols=[SUBPROTOCOL],
            compression=None,
            max_size=64 * 1024 + 40,
        )

    async def start_mtls_server(self):
        server = await start_gateway_server(
            self.gateway, "127.0.0.1", 0,
            build_tls_context(
                self.mtls_cert_path, self.mtls_key_path, self.ca_path
            ),
            self.ca_path,
        )
        self.addAsyncCleanup(server.wait_closed)
        self.addAsyncCleanup(server.close)
        return server

    def mtls_client_context(
        self, certificate: Path | None = None, key: Path | None = None
    ) -> ssl.SSLContext:
        context = ssl.create_default_context(cafile=self.ca_path)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        if certificate is not None:
            context.load_cert_chain(certificate, key)
        return context

    async def handshake(self, websocket, client: ClientWire, capabilities=0) -> tuple[Frame, Frame]:
        await websocket.send(client.frame(MessageType.HELLO,
            payload=struct.pack('!I', capabilities) if capabilities else b'').encode())
        welcome = Frame.decode(await websocket.recv())
        window = Frame.decode(await websocket.recv())
        self.assertEqual(welcome.message_type, MessageType.WELCOME)
        self.assertEqual(window.message_type, MessageType.WINDOW_UPDATE)
        self.assertEqual(decode_window_credit(window.payload), MAX_WINDOW)
        self.assertEqual(websocket.subprotocol, SUBPROTOCOL)
        return welcome, window

    async def wait_for_summary(self) -> dict[str, object]:
        for _ in range(100):
            summaries = [
                event
                for event in self.events
                if event.get("event") == "connection_summary"
            ]
            if summaries:
                return summaries[-1]
            await asyncio.sleep(0.005)
        self.fail("connection summary was not emitted")

    async def test_application_heartbeat_preserves_idle_and_active_turn(self) -> None:
        self.gateway.config = replace(self.gateway.config,
                                      heartbeat_interval_seconds=0.02)
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            first = Frame.decode(await asyncio.wait_for(websocket.recv(), 1))
            self.assertEqual(first.message_type, MessageType.HEARTBEAT)
            self.assertEqual(first.turn_id, 0)
            self.assertEqual(first.session_id, client.session_id)
            await websocket.send(client.frame(MessageType.TURN_START, turn_id=1).encode())
            second = Frame.decode(await asyncio.wait_for(websocket.recv(), 1))
            self.assertEqual(second.message_type, MessageType.HEARTBEAT)
            self.assertGreater(second.sequence, first.sequence)
            self.assertEqual(second.turn_id, 0)
            await websocket.send(client.frame(MessageType.CANCEL, turn_id=1).encode())
        summary = await self.wait_for_summary()
        self.assertEqual(summary['protocol_errors'], 0)
        self.assertEqual(summary['turns_cancelled'], 1)
        self.assertEqual(summary['terminal_state'], 'idle')

    async def test_normal_turn_streams_fixed_pcm_and_ends_idle(self) -> None:
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await websocket.send(
                client.frame(
                    MessageType.WINDOW_UPDATE,
                    payload=encode_window_credit(4 * AUDIO_FRAME_BYTES),
                ).encode()
            )
            await websocket.send(
                client.frame(MessageType.TURN_START, turn_id=1).encode()
            )
            await websocket.send(
                client.frame(
                    MessageType.AUDIO_UP,
                    turn_id=1,
                    payload=bytes(AUDIO_FRAME_BYTES),
                ).encode()
            )
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())

            replenishment = Frame.decode(await websocket.recv())
            self.assertEqual(replenishment.message_type, MessageType.WINDOW_UPDATE)
            received = []
            while not received or received[-1].message_type is not MessageType.TTS_END:
                received.append(Frame.decode(await websocket.recv()))

            self.assertEqual(received[0].message_type, MessageType.TTS_START)
            audio = [
                frame
                for frame in received
                if frame.message_type is MessageType.AUDIO_DOWN
            ]
            self.assertEqual(len(audio), 4)
            self.assertTrue(
                all(len(frame.payload) == AUDIO_FRAME_BYTES for frame in audio)
            )
            self.assertTrue(all(frame.flags & Flag.SYNTHETIC for frame in audio))
            self.assertTrue(audio[-1].flags & Flag.END_OF_STREAM)
            self.assertEqual(audio[0].payload, deterministic_pcm_frame(0))
            self.assertEqual(received[-1].message_type, MessageType.TTS_END)
            self.assertFalse(any(e.get('event') == 'playback_confirmed' for e in self.events))
            await websocket.send(client.frame(MessageType.ACK, turn_id=0).encode())
            await websocket.send(client.frame(MessageType.ACK, turn_id=1).encode())
            await websocket.send(client.frame(MessageType.ACK, turn_id=1).encode())

        summary = await self.wait_for_summary()
        self.assertEqual(summary["turns_completed"], 1)
        self.assertEqual(summary['playback_confirmed'], 1)
        self.assertEqual(summary["uplink_frames"], 1)
        self.assertEqual(summary["downlink_frames"], 4)
        self.assertEqual(summary["protocol_errors"], 0)

    async def _send_provider_turn(self, websocket, client, turn=1):
        await websocket.send(client.frame(
            MessageType.WINDOW_UPDATE, payload=encode_window_credit(4 * AUDIO_FRAME_BYTES)).encode())
        await websocket.send(client.frame(MessageType.TURN_START, turn_id=turn).encode())
        await websocket.send(client.frame(MessageType.AUDIO_UP, turn_id=turn,
                                         payload=b"\x01\x02" * 320).encode())
        await websocket.send(client.frame(MessageType.TURN_END, turn_id=turn).encode())
        self.assertEqual(Frame.decode(await websocket.recv()).message_type, MessageType.WINDOW_UPDATE)

    async def _mimo_endpoint(self, mode="normal"):
        requests = []
        pcm = b"\x03\x04" * 700

        async def handler(request):
            self.assertEqual(request.headers["Authorization"], "Bearer fixture-only")
            body = await request.json()
            requests.append(body)
            if mode == "unauthorized":
                return web.Response(status=401, text="private-provider-error")
            if mode == "redirect":
                return web.Response(status=307, headers={"Location": "/v1/redirected"})
            model = body["model"]
            if model.endswith("-asr"):
                encoded = body["messages"][0]["content"][0]["input_audio"]["data"]
                with wave.open(io.BytesIO(base64.b64decode(encoded.split(",")[1])), "rb") as wav:
                    self.assertEqual(wav.readframes(wav.getnframes()), b"\x01\x02" * 320)
                    self.assertEqual(wav.getframerate(), 16000)
                text = "fixture-transcript"
            elif model == "mimo-v2.5":
                self.assertEqual(body["messages"][-1]["content"], "fixture-transcript")
                self.assertNotIn("tools", body)
                self.assertEqual(body["thinking"], {"type": "disabled"})
                text = "fixture-response"
            else:
                self.assertEqual(model, "mimo-v2.5-tts")
                self.assertEqual(body["audio"]["format"], "pcm16")
                self.assertEqual(body["messages"], [{"role": "assistant", "content": "fixture-response"}])
                response = web.StreamResponse(headers={"Content-Type": "text/event-stream"})
                response.enable_compression(force=web.ContentCoding.gzip)
                await response.prepare(request)
                events = []
                for chunk in (pcm[:17], pcm[17:900], pcm[900:]):
                    events.append({"choices": [{"index": 0, "delta": {
                        "audio": {"data": base64.b64encode(chunk).decode()}}, "finish_reason": None}]})
                events.append({"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]})
                if mode == "bad_audio":
                    events[0]["choices"][0]["delta"]["audio"]["data"] = "!!!"
                wire = b"".join(("data: " + json.dumps(event) + "\r\n\r\n").encode() for event in events)
                if mode != "truncated":
                    wire += b"data: [DONE]\n\n"
                try:
                    for index in range(0, len(wire), 7):
                        await response.write(wire[index:index + 7])
                except ConnectionResetError:
                    pass
                return response
            response = web.json_response({"choices": [{"index": 0, "finish_reason": "stop",
                                                       "message": {"role": "assistant", "content": text}}]})
            response.enable_compression(force=web.ContentCoding.gzip)
            return response

        app = web.Application()
        app.router.add_post("/v1/chat/completions", handler)
        runner = web.AppRunner(app, access_log=None)
        await runner.setup()
        self.addAsyncCleanup(runner.cleanup)
        site = web.TCPSite(runner, "127.0.0.1", 0, ssl_context=build_tls_context(self.cert_path, self.key_path))
        await site.start()
        port = site._server.sockets[0].getsockname()[1]
        provider = MiMoProvider(MiMoConfig("fixture-only", 16000,
                                         base_url=f"https://localhost:{port}/v1"), tls_context=self.client_tls)
        return provider, requests, pcm

    async def test_mimo_https_asr_chat_tts_through_real_wss(self):
        provider, requests, pcm = await self._mimo_endpoint()
        self.gateway.reply_factory = provider.new_conversation
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await self._send_provider_turn(websocket, client)
            received = []
            while not received or received[-1].message_type is not MessageType.TTS_END:
                received.append(Frame.decode(await asyncio.wait_for(websocket.recv(), 3)))
            self.assertEqual(received[0].message_type, MessageType.TTS_START)
            audio = [frame for frame in received if frame.message_type is MessageType.AUDIO_DOWN]
            self.assertEqual(b"".join(frame.payload for frame in audio), pcm + bytes(520))
            self.assertEqual([bool(frame.flags & Flag.END_OF_STREAM) for frame in audio], [False, False, True])
        self.assertEqual(len(requests), 3)
        summary = await self.wait_for_summary()
        self.assertEqual(summary["turns_completed"], 1)
        for private in ("fixture-transcript", "fixture-response", "fixture-only"):
            self.assertNotIn(private, json.dumps(self.events))

    async def _drain_provider_turn(self, websocket):
        while True:
            frame = Frame.decode(await asyncio.wait_for(websocket.recv(), 3))
            if frame.message_type is MessageType.TTS_END:
                return
            self.assertIn(frame.message_type, (MessageType.TTS_START, MessageType.AUDIO_DOWN))

    async def test_mimo_context_is_per_connection_and_playback_error_rolls_back(self):
        provider, requests, _ = await self._mimo_endpoint()
        self.gateway.reply_factory = provider.new_conversation
        client = ClientWire()
        async with self.connect() as first, self.connect() as second:
            other = ClientWire(session_id=4)
            await self.handshake(first, client)
            await self.handshake(second, other)
            for turn in (1, 2):
                await self._send_provider_turn(first, client, turn=turn)
                await self._drain_provider_turn(first)
                await first.send(client.frame(MessageType.ACK, turn_id=turn).encode())
            chats = [r for r in requests if r["model"] == "mimo-v2.5"]
            self.assertEqual([len(r["messages"]) for r in chats], [2, 4])
            await self._send_provider_turn(second, other)
            await self._drain_provider_turn(second)
            self.assertEqual([r for r in requests if r["model"] == "mimo-v2.5"][-1]["messages"][1:],
                             [{"role": "user", "content": "fixture-transcript"}])
            await first.send(client.frame(MessageType.ERROR, turn_id=2, payload=b"playback_failed").encode())
            await self._send_provider_turn(first, client, turn=3)
            await self._drain_provider_turn(first)
            # Turn 2 was rejected, so only turn 1 is in turn 3's history.
            self.assertEqual(len([r for r in requests if r["model"] == "mimo-v2.5"][-1]["messages"]), 4)
        async with self.connect() as reconnected:
            fresh = ClientWire(session_id=5)
            await self.handshake(reconnected, fresh)
            await self._send_provider_turn(reconnected, fresh)
            await self._drain_provider_turn(reconnected)
            self.assertEqual(len([r for r in requests if r["model"] == "mimo-v2.5"][-1]["messages"]), 2)

    async def test_unconfirmed_playback_is_excluded_from_next_turn_context(self):
        provider, requests, _ = await self._mimo_endpoint()
        sessions = []

        def factory():
            session = provider.new_conversation()
            sessions.append(session)
            return session

        self.gateway.reply_factory = factory
        client = ClientWire()
        async with self.connect() as peer:
            await self.handshake(peer, client)
            await self._send_provider_turn(peer, client, turn=1)
            await self._drain_provider_turn(peer)
            self.assertFalse(sessions[0]._history)
            self.assertIsNotNone(sessions[0]._pending)
            # Starting another turn without ACK discards the unconfirmed pair.
            await self._send_provider_turn(peer, client, turn=2)
            await self._drain_provider_turn(peer)
            self.assertEqual([len(r['messages']) for r in requests
                              if r['model'] == 'mimo-v2.5'], [2, 2])
            await peer.send(client.frame(MessageType.ACK, turn_id=2).encode())
            await peer.send(client.frame(MessageType.ACK, turn_id=2).encode())
            await self._send_provider_turn(peer, client, turn=3)
            await self._drain_provider_turn(peer)
            self.assertEqual([len(r['messages']) for r in requests
                              if r['model'] == 'mimo-v2.5'], [2, 2, 4])
            self.assertEqual(len(sessions[0]._history), 1)

    async def test_cancelled_downlink_never_commits_generated_reply(self):
        provider, _, _ = await self._mimo_endpoint()
        sessions = []

        def factory():
            session = provider.new_conversation()
            sessions.append(session)
            return session

        self.gateway.reply_factory = factory
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await websocket.send(client.frame(MessageType.TURN_START, turn_id=1).encode())
            await websocket.send(client.frame(MessageType.AUDIO_UP, turn_id=1,
                                             payload=b"\x01\x02" * 320).encode())
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())
            await websocket.recv()  # Uplink credit.
            self.assertEqual(Frame.decode(await websocket.recv()).message_type, MessageType.TTS_START)
            # No downlink credit: generation has started but audio was not delivered.
            await websocket.send(client.frame(MessageType.CANCEL, turn_id=1).encode())
        await self.wait_for_summary()
        self.assertFalse(sessions[0]._history)
        self.assertIsNone(sessions[0]._pending)
        self.assertTrue(sessions[0]._closed)

    async def test_mimo_http_and_stream_errors_never_complete_turn(self):
        for mode in ("unauthorized", "redirect", "bad_audio", "truncated"):
            with self.subTest(mode=mode):
                provider, requests, _ = await self._mimo_endpoint(mode)
                self.gateway.reply_factory = provider.new_conversation
                client = ClientWire()
                async with self.connect() as websocket:
                    await self.handshake(websocket, client)
                    await self._send_provider_turn(websocket, client)
                    with self.assertRaises(ConnectionClosedError):
                        while True:
                            frame = Frame.decode(await asyncio.wait_for(websocket.recv(), 3))
                            self.assertNotEqual(frame.message_type, MessageType.TTS_END)
                if mode in ("unauthorized", "redirect"):
                    self.assertEqual(len(requests), 1)
                self.assertNotIn("private-provider-error", json.dumps(self.events))

    async def test_cancel_provider_joins_before_next_turn(self):
        started, closed = asyncio.Event(), asyncio.Event()
        calls = []

        async def provider(pcm):
            calls.append(pcm)
            try:
                started.set()
                await asyncio.Event().wait()
                yield b"unreachable"
            finally:
                closed.set()

        self.gateway.reply_factory = lambda: FixtureReplySession(provider)
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await self._send_provider_turn(websocket, client)
            await asyncio.wait_for(started.wait(), 1)
            await websocket.send(client.frame(MessageType.CANCEL, turn_id=1).encode())
            await asyncio.wait_for(closed.wait(), 1)
            started.clear()
            closed.clear()
            await self._send_provider_turn(websocket, client, turn=2)
            await asyncio.wait_for(started.wait(), 1)
        await asyncio.wait_for(closed.wait(), 1)
        self.assertEqual(calls, [b"\x01\x02" * 320] * 2)

    async def test_provider_timeout_cancels_worker(self):
        closed = asyncio.Event()

        async def provider(pcm):
            try:
                await asyncio.Event().wait()
                yield b"unreachable"
            finally:
                closed.set()

        self.gateway.config = replace(self.gateway.config, reply_timeout_seconds=0.05)
        self.gateway.reply_factory = lambda: FixtureReplySession(provider)
        async with self.connect() as websocket:
            client = ClientWire()
            await self.handshake(websocket, client)
            await self._send_provider_turn(websocket, client)
            with self.assertRaises(ConnectionClosedError):
                await asyncio.wait_for(websocket.recv(), 1)
        await asyncio.wait_for(closed.wait(), 1)

    async def test_provider_streams_before_eof_and_respects_credit(self):
        # This checks flow control, not timeout latency. Keep it independent
        # of short scheduler stalls during concurrent host builds.
        self.gateway.config = replace(self.gateway.config, downlink_window_timeout_ms=5000)
        finish = asyncio.Event()
        closed = asyncio.Event()

        async def provider(pcm):
            try:
                yield b"\x01\x00" * 320
                yield b"\x02\x00" * 320
                await finish.wait()
                yield b"\x03\x00" * 10
            finally:
                closed.set()

        self.gateway.reply_factory = lambda: FixtureReplySession(provider)
        async with self.connect() as websocket:
            client = ClientWire()
            await self.handshake(websocket, client)
            # Exactly one frame of receive credit.
            await websocket.send(client.frame(MessageType.WINDOW_UPDATE,
                                             payload=encode_window_credit(AUDIO_FRAME_BYTES)).encode())
            await websocket.send(client.frame(MessageType.TURN_START, turn_id=1).encode())
            await websocket.send(client.frame(MessageType.AUDIO_UP, turn_id=1,
                                             payload=bytes(AUDIO_FRAME_BYTES)).encode())
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())
            self.assertEqual(Frame.decode(await websocket.recv()).message_type, MessageType.WINDOW_UPDATE)
            self.assertEqual(Frame.decode(await websocket.recv()).message_type, MessageType.TTS_START)
            first = Frame.decode(await asyncio.wait_for(websocket.recv(), 1))
            self.assertEqual(first.payload, b"\x01\x00" * 320)
            self.assertFalse(finish.is_set())
            finish.set()
            with self.assertRaises(asyncio.TimeoutError):
                await asyncio.wait_for(websocket.recv(), 0.03)
            await websocket.send(client.frame(MessageType.WINDOW_UPDATE, turn_id=1,
                                             payload=encode_window_credit(2 * AUDIO_FRAME_BYTES)).encode())
            self.assertEqual(Frame.decode(await websocket.recv()).payload, b"\x02\x00" * 320)
            last = Frame.decode(await websocket.recv())
            self.assertEqual(last.payload, b"\x03\x00" * 10 + bytes(620))
            self.assertTrue(last.flags & Flag.END_OF_STREAM)
            self.assertEqual(Frame.decode(await websocket.recv()).message_type, MessageType.TTS_END)
        await asyncio.wait_for(closed.wait(), 1)

    async def test_downlink_window_timeout_is_a_protocol_error(self):
        self.gateway.config = replace(self.gateway.config, downlink_window_timeout_ms=20)
        async with self.connect() as websocket:
            client = ClientWire()
            await self.handshake(websocket, client)
            await websocket.send(client.frame(MessageType.TURN_START, turn_id=1).encode())
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())
            self.assertEqual(Frame.decode(await websocket.recv()).message_type, MessageType.TTS_START)
            error = Frame.decode(await websocket.recv())
            self.assertEqual(error.message_type, MessageType.ERROR)
            self.assertEqual(error.payload, b'')
            with self.assertRaises(ConnectionClosedError):
                await websocket.recv()
            self.assertEqual(websocket.close_code, 1002)


    async def test_uplink_limit_rejects_before_provider_invocation(self):
        async def provider(pcm):
            self.fail("overlong recording reached provider")
            yield b"unreachable"

        self.gateway.config = replace(self.gateway.config, max_uplink_bytes=AUDIO_FRAME_BYTES)
        self.gateway.reply_factory = lambda: FixtureReplySession(provider)
        async with self.connect() as websocket:
            client = ClientWire()
            await self.handshake(websocket, client)
            await websocket.send(client.frame(MessageType.TURN_START, turn_id=1).encode())
            for _ in range(2):
                await websocket.send(client.frame(MessageType.AUDIO_UP, turn_id=1,
                                                 payload=bytes(AUDIO_FRAME_BYTES)).encode())
            await websocket.recv()  # First frame's replenishment only.
            self.assertEqual(Frame.decode(await websocket.recv()).message_type, MessageType.ERROR)
            with self.assertRaises(ConnectionClosedError):
                await websocket.recv()

    async def test_cancel_stops_a_reply_and_keeps_the_session_usable(self) -> None:
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await websocket.send(
                client.frame(MessageType.TURN_START, turn_id=1).encode()
            )
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())
            await websocket.send(client.frame(MessageType.CANCEL, turn_id=1).encode())
            try:
                possible_start = Frame.decode(
                    await asyncio.wait_for(websocket.recv(), timeout=0.05)
                )
                self.assertEqual(possible_start.message_type, MessageType.TTS_START)
            except asyncio.TimeoutError:
                pass
            self.assertTrue(websocket.open)

            await websocket.send(
                client.frame(MessageType.TURN_START, turn_id=2).encode()
            )
            await websocket.send(client.frame(MessageType.CANCEL, turn_id=2).encode())
            await asyncio.sleep(0.02)
            self.assertTrue(websocket.open)

        summary = await self.wait_for_summary()
        self.assertEqual(summary["turns_cancelled"], 2)
        self.assertEqual(summary["turns_completed"], 0)
        self.assertEqual(summary["aborted_turns"], 0)

    async def test_sequence_gap_sends_error_then_closes(self) -> None:
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            gap = client.frame(MessageType.TURN_START, turn_id=1, sequence=3)
            await websocket.send(gap.encode())
            error = Frame.decode(await websocket.recv())
            self.assertEqual(error.message_type, MessageType.ERROR)
            self.assertEqual(error.turn_id, 0)
            with self.assertRaises(ConnectionClosedError) as closed:
                await websocket.recv()
            self.assertEqual(closed.exception.code, 1002)

        summary = await self.wait_for_summary()
        self.assertEqual(summary["protocol_errors"], 1)

    async def test_disconnect_mid_uplink_is_counted_as_aborted(self) -> None:
        client = ClientWire()
        websocket = await self.connect()
        await self.handshake(websocket, client)
        await websocket.send(client.frame(MessageType.TURN_START, turn_id=1).encode())
        await websocket.send(
            client.frame(
                MessageType.AUDIO_UP,
                turn_id=1,
                payload=bytes(AUDIO_FRAME_BYTES),
            ).encode()
        )
        replenishment = Frame.decode(await websocket.recv())
        self.assertEqual(replenishment.message_type, MessageType.WINDOW_UPDATE)
        await websocket.close()

        summary = await self.wait_for_summary()
        self.assertEqual(summary["terminal_state"], "uplink")
        self.assertEqual(summary["aborted_turns"], 1)
        self.assertEqual(summary["uplink_frames"], 1)

    async def test_downlink_waits_for_additional_window_credit(self) -> None:
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await websocket.send(
                client.frame(
                    MessageType.WINDOW_UPDATE,
                    payload=encode_window_credit(AUDIO_FRAME_BYTES),
                ).encode()
            )
            await websocket.send(
                client.frame(MessageType.TURN_START, turn_id=1).encode()
            )
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())

            self.assertEqual(
                Frame.decode(await websocket.recv()).message_type, MessageType.TTS_START
            )
            first_audio = Frame.decode(await websocket.recv())
            self.assertEqual(first_audio.message_type, MessageType.AUDIO_DOWN)
            with self.assertRaises(asyncio.TimeoutError):
                await asyncio.wait_for(websocket.recv(), timeout=0.05)

            await websocket.send(
                client.frame(
                    MessageType.WINDOW_UPDATE,
                    turn_id=1,
                    payload=encode_window_credit(3 * AUDIO_FRAME_BYTES),
                ).encode()
            )
            tail = [Frame.decode(await websocket.recv()) for _ in range(4)]
            self.assertEqual(
                [frame.message_type for frame in tail],
                [
                    MessageType.AUDIO_DOWN,
                    MessageType.AUDIO_DOWN,
                    MessageType.AUDIO_DOWN,
                    MessageType.TTS_END,
                ],
            )
            self.assertTrue(tail[-2].flags & Flag.END_OF_STREAM)

    async def test_certificate_hostname_mismatch_is_rejected(self) -> None:
        with self.assertRaises((ssl.SSLCertVerificationError, OSError)):
            async with websockets.connect(
                f"wss://127.0.0.1:{self.port}{DEFAULT_PATH}",
                ssl=self.client_tls,
                subprotocols=[SUBPROTOCOL],
            ):
                pass

    async def test_non_loopback_bind_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "loopback"):
            await start_gateway_server(
                self.gateway,
                "0.0.0.0",
                0,
                build_tls_context(self.cert_path, self.key_path),
            )

    async def test_non_loopback_requires_registered_device_bindings(self) -> None:
        with self.assertRaisesRegex(ValueError, "registered device bindings"):
            await start_gateway_server(
                self.gateway,
                "192.168.1.7",
                0,
                build_tls_context(self.cert_path, self.key_path, self.ca_path),
                self.ca_path,
            )

    async def test_mtls_trusted_client_connects(self) -> None:
        server = await self.start_mtls_server()
        port = server.sockets[0].getsockname()[1]
        async with websockets.connect(
            f"wss://localhost:{port}{DEFAULT_PATH}",
            ssl=self.mtls_client_context(
                self.trusted_client_cert, self.trusted_client_key
            ),
            subprotocols=[SUBPROTOCOL],
        ) as websocket:
            await self.handshake(websocket, ClientWire())

    async def test_registered_device_connection_lifecycle(self) -> None:
        der = ssl.PEM_cert_to_DER_cert(self.trusted_client_cert.read_text())
        self.gateway.device_bindings = DeviceBindings({
            hashlib.sha256(der).hexdigest(): 'board-1'})
        server = await self.start_mtls_server()
        port = server.sockets[0].getsockname()[1]
        uri = f'wss://localhost:{port}{DEFAULT_PATH}'
        context = self.mtls_client_context(self.trusted_client_cert, self.trusted_client_key)
        async with websockets.connect(uri, ssl=context, subprotocols=[SUBPROTOCOL]) as first:
            self.assertIsNone(self.gateway.device_connection('board-1'))
            await self.handshake(first, ClientWire())
            connection = self.gateway.device_connection('board-1')
            self.assertIsNotNone(connection)
            self.assertEqual(connection.session.boot_generation, 7)
            self.assertIsNone(self.gateway.device_connection('board-2'))
            async with websockets.connect(uri, ssl=context, subprotocols=[SUBPROTOCOL]) as duplicate:
                with self.assertRaises(ConnectionClosedError):
                    await duplicate.recv()
                self.assertEqual(duplicate.close_code, 1008)
            self.assertIs(self.gateway.device_connection('board-1'), connection)
        await self.wait_for_summary()
        self.assertIsNone(self.gateway.device_connection('board-1'))
        async with websockets.connect(uri, ssl=context, subprotocols=[SUBPROTOCOL]) as second:
            await self.handshake(second, ClientWire(session_id=4))
            self.assertIsNot(self.gateway.device_connection('board-1'), connection)
        self.assertNotIn('board-1', json.dumps(self.events))

    async def test_ca_trusted_but_unregistered_device_rejected(self) -> None:
        self.gateway.device_bindings = DeviceBindings({'0' * 64: 'board-1'})
        server = await self.start_mtls_server()
        port = server.sockets[0].getsockname()[1]
        context = self.mtls_client_context(self.trusted_client_cert, self.trusted_client_key)
        async with websockets.connect(f'wss://localhost:{port}{DEFAULT_PATH}',
                                      ssl=context, subprotocols=[SUBPROTOCOL]) as websocket:
            with self.assertRaises(ConnectionClosedError):
                await websocket.recv()
            self.assertEqual(websocket.close_code, 1008)
        self.assertIsNone(self.gateway.device_connection('board-1'))

    async def test_binding_requires_mtls_even_on_loopback(self) -> None:
        self.gateway.device_bindings = DeviceBindings({'0' * 64: 'board-1'})
        with self.assertRaisesRegex(ValueError, 'verified mutual TLS'):
            await start_gateway_server(self.gateway, '127.0.0.1', 0,
                                       build_tls_context(self.cert_path, self.key_path))

    async def test_registered_device_hello_timeout_releases_slot(self) -> None:
        der = ssl.PEM_cert_to_DER_cert(self.trusted_client_cert.read_text())
        self.gateway.device_bindings = DeviceBindings({
            hashlib.sha256(der).hexdigest(): 'board-1'})
        self.gateway.config = replace(self.gateway.config, hello_timeout_seconds=0.02)
        server = await self.start_mtls_server()
        port = server.sockets[0].getsockname()[1]
        uri = f'wss://localhost:{port}{DEFAULT_PATH}'
        context = self.mtls_client_context(self.trusted_client_cert, self.trusted_client_key)
        async with websockets.connect(uri, ssl=context, subprotocols=[SUBPROTOCOL]) as idle:
            with self.assertRaises(ConnectionClosedError):
                await idle.recv()
        await self.wait_for_summary()
        self.assertIsNone(self.gateway.device_connection('board-1'))
        async with websockets.connect(uri, ssl=context, subprotocols=[SUBPROTOCOL]) as active:
            await self.handshake(active, ClientWire())
            self.assertIsNotNone(self.gateway.device_connection('board-1'))

    async def console_fixture(self, capabilities=0, firmware_releases=None,
                              memory_store=None, ota_store=False):
        der = ssl.PEM_cert_to_DER_cert(self.trusted_client_cert.read_text())
        self.gateway.device_bindings = DeviceBindings({
            hashlib.sha256(der).hexdigest(): 'board-1', 'a' * 64: 'board-2'})
        self.gateway.reply_factory = MiMoProvider(MiMoConfig(
            api_key='test-only-not-sent', tts_sample_rate=16000)).new_conversation
        board_server = await self.start_mtls_server()
        board_port = board_server.sockets[0].getsockname()[1]
        peer = await websockets.connect(f'wss://localhost:{board_port}{DEFAULT_PATH}',
                                        ssl=self.mtls_client_context(self.trusted_client_cert,
                                                                     self.trusted_client_key),
                                        subprotocols=[SUBPROTOCOL])
        self.addAsyncCleanup(peer.close)
        wire = ClientWire()
        await self.handshake(peer, wire, capabilities)
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        ota_transactions = None
        if ota_store:
            ota_transactions = OtaTransactionStore(Path(directory.name) / 'ota.db')
            self.addCleanup(ota_transactions.close)
            self.gateway.ota_transactions = ota_transactions
            self.gateway.firmware_releases = firmware_releases or FirmwareReleases()
        now = time.time_ns() // 1000000
        grants = tuple(ConsoleGrant(hashlib.sha256((char * 43).encode()).hexdigest(),
                                    'board-1', write, expiry)
                       for char, write, expiry in [('w', True, now + 60000),
                                                   ('r', False, now + 60000),
                                                   ('e', True, now - 1)])
        service = ConsoleService(
            self.gateway, grants, Path(directory.name) / 'epoch.db', firmware_releases,
            memory_store=memory_store, ota_transactions=ota_transactions,
        )
        runner = web.AppRunner(service.app, access_log=None)
        await runner.setup()
        self.addAsyncCleanup(runner.cleanup)
        site = web.TCPSite(runner, '127.0.0.1', 0,
                          ssl_context=build_tls_context(self.cert_path, self.key_path))
        await site.start()
        url = f'https://localhost:{site._server.sockets[0].getsockname()[1]}/console/v1/devices/board-1'
        return url, peer, wire, service

    def console_mutation(self, snapshot, request_id='persona-1', **changes):
        now = time.time_ns() // 1000000
        value = dict(protocol='console-v1', kind='mutation', request_id=request_id,
                     device_id='board-1', generation=snapshot['generation'],
                     expected_revision=snapshot['payload']['revision'],
                     issued_at_ms=now, expires_at_ms=now + 30000,
                     operation='persona_mode.set', arguments={'persona_mode': 'quiet'})
        value.update(changes)
        return value

    async def test_console_volume_waits_for_board_and_caches_receipt(self):
        url, peer, wire, service = await self.console_fixture(CAP_VOLUME)
        async with ClientSession(headers={'Authorization': 'Bearer ' + 'w' * 43}) as client:
            fetch = asyncio.create_task(client.get(url + '/snapshot', ssl=self.client_tls))
            request = Frame.decode(await peer.recv())
            self.assertEqual(request.message_type, MessageType.VOLUME_GET)
            await peer.send(wire.frame(MessageType.VOLUME_REPORT,
                payload=struct.pack('!IiI', request.sequence, 0, 50)).encode())
            response = await fetch
            snapshot = await response.json()
            self.assertEqual(snapshot['payload']['volume_percent'], 50)
            mutation = self.console_mutation(snapshot, request_id='volume-1',
                operation='volume.set', arguments={'volume_percent': 65})
            submit = asyncio.create_task(client.post(url + '/mutations', json=mutation,
                                                     ssl=self.client_tls))
            request = Frame.decode(await peer.recv())
            self.assertEqual(request.message_type, MessageType.VOLUME_SET)
            self.assertFalse(submit.done())
            await peer.send(wire.frame(MessageType.VOLUME_REPORT,
                payload=struct.pack('!IiI', request.sequence, 0, 67)).encode())
            response = await submit
            receipt = await response.json()
            self.assertEqual(receipt['status'], 'accepted')
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                updated = await response.json()
            self.assertEqual(updated['payload']['volume_percent'], 67)
            async with client.post(url + '/mutations', json=mutation,
                                   ssl=self.client_tls) as response:
                self.assertEqual(await response.json(), receipt)
            with self.assertRaises(asyncio.TimeoutError):
                await asyncio.wait_for(peer.recv(), 0.03)

    async def test_console_volume_timeout_is_cached_and_query_recovers(self):
        url, peer, wire, service = await self.console_fixture(CAP_VOLUME)
        connection = self.gateway.device_connection('board-1')
        connection.config = replace(connection.config, volume_timeout_seconds=0.03)
        async with ClientSession(headers={'Authorization': 'Bearer ' + 'w' * 43}) as client:
            # Deliberately lose the initial query reply: snapshot must stay unknown.
            fetch = asyncio.create_task(client.get(url + '/snapshot', ssl=self.client_tls))
            await peer.recv()
            response = await fetch
            snapshot = await response.json()
            self.assertIsNone(snapshot['payload']['volume_percent'])
            mutation = self.console_mutation(snapshot, request_id='volume-lost',
                operation='volume.set', arguments={'volume_percent': 65})
            submit = asyncio.create_task(client.post(url + '/mutations', json=mutation,
                                                     ssl=self.client_tls))
            lost = Frame.decode(await peer.recv())
            receipt = await (await submit).json()
            self.assertEqual(receipt['error'], 'volume_timeout')
            async with client.post(url + '/mutations', json=mutation,
                                   ssl=self.client_tls) as response:
                self.assertEqual(await response.json(), receipt)
            # A late successful SET must not turn the timed-out receipt into success.
            await peer.send(wire.frame(MessageType.VOLUME_REPORT,
                payload=struct.pack('!IiI', lost.sequence, 0, 67)).encode())
            fetch = asyncio.create_task(client.get(url + '/snapshot', ssl=self.client_tls))
            query = Frame.decode(await peer.recv())
            self.assertEqual(query.message_type, MessageType.VOLUME_GET)
            await peer.send(wire.frame(MessageType.VOLUME_REPORT,
                payload=struct.pack('!IiI', query.sequence, 0, 67)).encode())
            updated = await (await fetch).json()
            self.assertEqual(updated['payload']['volume_percent'], 67)
            self.assertGreater(updated['payload']['revision'], snapshot['payload']['revision'])

    async def test_volume_request_matches_report_and_rejects_late_reply(self):
        url, peer, wire, service = await self.console_fixture(CAP_VOLUME)
        connection = self.gateway.device_connection('board-1')
        connection.config = replace(connection.config, volume_timeout_seconds=0.2)
        task = asyncio.create_task(connection.request_volume(65))
        request = Frame.decode(await peer.recv())
        self.assertEqual(request.message_type, MessageType.VOLUME_SET)
        self.assertEqual(request.payload, struct.pack('!I', 65))
        with self.assertRaisesRegex(ProtocolError, 'volume_busy'):
            await connection.request_volume(30)
        await peer.send(wire.frame(MessageType.VOLUME_REPORT,
            payload=struct.pack('!IiI', request.sequence + 1, 0, 99)).encode())
        await peer.send(wire.frame(MessageType.VOLUME_REPORT,
            payload=struct.pack('!IiI', request.sequence, 0, 67)).encode())
        self.assertEqual(await task, 67)
        self.assertEqual(connection.volume_percent, 67)
        task = asyncio.create_task(connection.request_volume())
        request = Frame.decode(await peer.recv())
        with self.assertRaisesRegex(ProtocolError, 'volume_timeout'):
            await task
        self.assertIsNone(connection.volume_percent)
        await peer.send(wire.frame(MessageType.VOLUME_REPORT,
            payload=struct.pack('!IiI', request.sequence, 0, 67)).encode())
        task = asyncio.create_task(connection.request_volume())
        request = Frame.decode(await peer.recv())
        await peer.send(wire.frame(MessageType.VOLUME_REPORT,
            payload=struct.pack('!IiI', request.sequence, -16, 0xffffffff)).encode())
        with self.assertRaisesRegex(ProtocolError, 'volume_device_error'):
            await task
        self.assertIsNone(connection.volume_percent)
        task = asyncio.create_task(connection.request_volume())
        await peer.recv()
        await peer.close()
        with self.assertRaisesRegex(ProtocolError, 'device_offline'):
            await task

    async def test_remote_cancel_discards_crossing_pcm_and_stops_provider(self):
        _, peer, wire, _ = await self.console_fixture()
        connection = self.gateway.device_connection('board-1')
        started, stopped = asyncio.Event(), asyncio.Event()

        async def provider(pcm):
            self.assertEqual(pcm, b'')
            started.set()
            try:
                await asyncio.Event().wait()
                yield b'unreachable'
            finally:
                stopped.set()

        connection.reply_session = FixtureReplySession(provider)
        await peer.send(wire.frame(MessageType.TURN_START, turn_id=1).encode())
        await peer.send(wire.frame(MessageType.AUDIO_UP, turn_id=1,
                                   payload=bytes(AUDIO_FRAME_BYTES)).encode())
        self.assertEqual(Frame.decode(await peer.recv()).message_type, MessageType.WINDOW_UPDATE)
        await connection.cancel_turn(1)
        cancel = Frame.decode(await peer.recv())
        self.assertEqual((cancel.message_type, cancel.turn_id), (MessageType.CANCEL, 1))
        await peer.send(wire.frame(MessageType.AUDIO_UP, turn_id=1,
                                   payload=bytes(AUDIO_FRAME_BYTES)).encode())
        await peer.send(wire.frame(MessageType.TURN_END, turn_id=1).encode())
        self.assertEqual(Frame.decode(await peer.recv()).message_type, MessageType.WINDOW_UPDATE)
        await peer.send(wire.frame(MessageType.TURN_START, turn_id=2).encode())
        await peer.send(wire.frame(MessageType.TURN_END, turn_id=2).encode())
        await asyncio.wait_for(started.wait(), 1)
        with self.assertRaisesRegex(ProtocolError, 'stale_turn'):
            await connection.cancel_turn(1)
        self.assertFalse(stopped.is_set())
        await connection.cancel_turn(2)
        self.assertTrue(stopped.is_set())
        cancel = Frame.decode(await peer.recv())
        self.assertEqual((cancel.message_type, cancel.turn_id), (MessageType.CANCEL, 2))
        self.assertFalse(connection._pcm)
        self.assertEqual(connection.session.state, SessionState.IDLE)
        self.assertEqual(connection.metrics.protocol_errors, 0)

    async def test_console_https_persona_events_and_idempotency(self):
        url, peer, wire, service = await self.console_fixture()
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                self.assertEqual(response.status, 200)
                self.assertEqual(response.headers['Cache-Control'], 'no-store')
                snapshot = await response.json()
            fixtures = Path(__file__).parent / 'fixtures/console-v1'
            expected = json.loads((fixtures / 'snapshot.json').read_text())
            self.assertEqual(dict(snapshot, occurred_at_ms=1000), expected)
            self.assertIsNone(snapshot['payload']['volume_percent'])
            self.assertIsNone(snapshot['payload']['charging'])
            self.assertEqual(snapshot['payload']['emotion'], 'unknown')
            self.assertEqual(snapshot['payload']['update']['phase'], 'unknown')
            events_url = url.replace('https:', 'wss:') + (
                f"/events?generation={snapshot['generation']}&after_sequence=0")
            async with client.ws_connect(events_url, ssl=self.client_tls) as events:
                self.assertEqual(await events.receive_json(timeout=2), snapshot)
                mutation = self.console_mutation(snapshot)
                expected_mutation = json.loads((fixtures / 'persona-mutation.json').read_text())
                self.assertEqual(dict(mutation, issued_at_ms=1000, expires_at_ms=31000),
                                 expected_mutation)
                async with client.post(
                    url + '/mutations',
                    data=json.dumps(mutation),
                    headers={'Content-Type': 'application/vnd.shaniu.console-v1+json'},
                    ssl=self.client_tls,
                ) as response:
                    self.assertEqual(response.status, 200)
                    receipt = await response.json()
                self.assertEqual(receipt, json.loads((fixtures / 'receipt.json').read_text()))
                self.assertEqual(receipt['status'], 'accepted')
                self.assertEqual(receipt['revision'], 1)
                self.assertEqual(self.gateway.device_connection('board-1').reply_session.persona_mode, 'quiet')
                event = await events.receive_json(timeout=2)
                self.assertEqual(event['payload']['persona_mode'], 'quiet')
                self.assertEqual(event['payload']['revision'], 1)
                async with client.post(url + '/mutations', json=mutation, ssl=self.client_tls) as response:
                    self.assertEqual(await response.json(), receipt)
                changed = dict(mutation, arguments={'persona_mode': 'playful'})
                async with client.post(url + '/mutations', json=changed, ssl=self.client_tls) as response:
                    self.assertEqual(response.status, 409)
                stale = dict(mutation, request_id='stale')
                async with client.post(url + '/mutations', json=stale, ssl=self.client_tls) as response:
                    self.assertEqual((await response.json())['error'], 'revision_conflict')
                unsupported = self.console_mutation(event, 'unsupported', operation='volume.set',
                                                    arguments={'volume_percent': 70})
                async with client.post(url + '/mutations', json=unsupported, ssl=self.client_tls) as response:
                    self.assertEqual((await response.json())['error'], 'volume_not_supported')
            board_port = peer.remote_address[1]
            await peer.close()
            await self.wait_for_summary()
            async with websockets.connect(f'wss://localhost:{board_port}{DEFAULT_PATH}',
                                          ssl=self.mtls_client_context(self.trusted_client_cert,
                                                                       self.trusted_client_key),
                                          subprotocols=[SUBPROTOCOL]) as reconnected:
                await self.handshake(reconnected, ClientWire(session_id=4))
                async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                    next_snapshot = await response.json()
                self.assertGreater(next_snapshot['generation'], snapshot['generation'])
                self.assertEqual(next_snapshot['payload']['persona_mode'], 'quiet')
                async with client.post(url + '/mutations', json=mutation, ssl=self.client_tls) as response:
                    self.assertEqual((await response.json())['error'], 'stale_generation')
        self.assertNotIn('w' * 43, json.dumps(self.events))

    async def test_console_persona_store_failure_reverts_live_session(self):
        url, peer, _, service = await self.console_fixture()
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                snapshot = await response.json()
            original = service.epochs.set_persona

            def fail_store(device_id, mode):
                raise sqlite3.OperationalError('fixture failure')

            service.epochs.set_persona = fail_store
            try:
                mutation = self.console_mutation(snapshot)
                async with client.post(url + '/mutations', json=mutation,
                                       ssl=self.client_tls) as response:
                    self.assertEqual(response.status, 503)
                self.assertEqual(
                    self.gateway.device_connection('board-1').reply_session.persona_mode,
                    'gentle',
                )
            finally:
                service.epochs.set_persona = original

    async def test_console_persona_restore_defers_while_provider_is_busy(self):
        url, _, _, service = await self.console_fixture()
        service.epochs.set_persona('board-1', 'quiet')
        connection = self.gateway.device_connection('board-1')
        reply = BusyPersonaReplySession()
        connection.reply_session = reply
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                self.assertEqual(response.status, 200)
                snapshot = await response.json()
            self.assertEqual(snapshot['payload']['persona_mode'], 'gentle')
            attempts_while_busy = reply.persona_attempts
            self.assertGreaterEqual(attempts_while_busy, 1)

            reply.busy = False
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                self.assertEqual(response.status, 200)
                restored = await response.json()
            self.assertEqual(restored['payload']['persona_mode'], 'quiet')
            self.assertEqual(reply.persona_attempts, attempts_while_busy + 1)

    async def test_console_projects_authenticated_device_status(self):
        url, peer, wire, _ = await self.console_fixture(CAP_STATUS_REPORT)
        root = bytes(range(32))
        payload = struct.pack(
            '!BBBBIHHHHI32s', 2, 0xff, 1, 4, 3888,
            18, 6, 389, 0, 449, root,
        )
        await peer.send(wire.frame(MessageType.STATUS_REPORT,
                                   payload=payload).encode())
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            for _ in range(100):
                async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                    self.assertEqual(response.status, 200)
                    snapshot = await response.json()
                if snapshot['payload']['charging'] is True:
                    break
                await asyncio.sleep(0.01)
        self.assertIsNone(snapshot['payload']['battery_percent'])
        self.assertTrue(snapshot['payload']['charging'])
        self.assertEqual(snapshot['payload']['firmware_version'], '18.6.389+449')
        status = self.gateway.device_connection('board-1').device_status
        self.assertEqual(status.battery_state, 'charging')
        self.assertEqual(status.battery_voltage_mv, 3888)
        self.assertEqual(status.firmware_root_sha256, root.hex())

    async def test_console_projects_only_source_compatible_verified_releases(self):
        release = FirmwareRelease(
            device_id='board-1', manifest_sha256='a' * 64,
            target_version='18.6.390+450',
            required_source_version='18.6.389+449',
            required_source_root_sha256='b' * 64,
            board_family='bk7258', physical_board='aidk_ai_toy',
            layout_identity='bk7258-0123456789abcdef',
            layout_sha256='c' * 64, package_sha256='d' * 64,
            package_size_bytes=2_457_600,
        )
        url, peer, _, _ = await self.console_fixture(
            CAP_STATUS_REPORT, FirmwareReleases((release,)),
        )
        connection = self.gateway.device_connection('board-1')
        connection.device_status = StatusReport(
            battery_percent=None, charging=None, battery_state=None,
            battery_voltage_mv=None, firmware_version='18.6.389+449',
            firmware_root_sha256='b' * 64,
        )
        headers = {'Authorization': 'Bearer ' + 'r' * 43}
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            generation = snapshot['generation']
            async with client.get(
                    f'{url}/firmware/releases?generation={generation}',
                    ssl=self.client_tls) as response:
                self.assertEqual(response.status, 200)
                self.assertEqual(response.headers['Cache-Control'], 'no-store')
                catalog = await response.json()
            self.assertEqual(catalog, {
                'protocol': 'console-v1',
                'kind': 'firmware.release_catalog',
                'device_id': 'board-1',
                'generation': generation,
                'releases': [release.projection()],
            })
            self.assertNotIn('uri', json.dumps(catalog))
            self.assertNotIn('path', json.dumps(catalog))

            connection.device_status = StatusReport(
                battery_percent=None, charging=None, battery_state=None,
                battery_voltage_mv=None, firmware_version='18.6.388+448',
                firmware_root_sha256='b' * 64,
            )
            async with client.get(
                    f'{url}/firmware/releases?generation={generation}',
                    ssl=self.client_tls) as response:
                self.assertEqual((await response.json())['releases'], [])

            connection.device_status = StatusReport(
                battery_percent=None, charging=None, battery_state=None,
                battery_voltage_mv=None, firmware_version='18.6.389+449',
                firmware_root_sha256='e' * 64,
            )
            async with client.get(
                    f'{url}/firmware/releases?generation={generation}',
                    ssl=self.client_tls) as response:
                self.assertEqual((await response.json())['releases'], [])

            for query, expected in (
                    ('generation=0', 400),
                    (f'generation={generation + 1}', 409),
                    (f'generation={generation}&extra=1', 400),
                    (f'generation={generation}&generation={generation}', 400)):
                async with client.get(f'{url}/firmware/releases?{query}',
                                      ssl=self.client_tls) as response:
                    self.assertEqual(response.status, expected)

        # The catalog GET never writes to the device transport.
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(peer.recv(), 0.03)

    async def test_console_firmware_update_stays_unsupported(self):
        url, peer, _, _ = await self.console_fixture()
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            mutation = self.console_mutation(
                snapshot, 'ota-disabled', operation='firmware.update',
                arguments={'release_manifest_sha256': 'a' * 64},
            )
            async with client.post(url + '/mutations', json=mutation,
                                   ssl=self.client_tls) as response:
                receipt = await response.json()
            self.assertEqual(receipt['status'], 'rejected')
            self.assertEqual(receipt['error'], 'update_manifest_invalid')
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(peer.recv(), 0.03)

    async def test_console_ota_accepts_only_matching_report_and_projects_status(self):
        release = FirmwareRelease(
            device_id='board-1', manifest_sha256='a' * 64,
            target_version='18.6.390+450', required_source_version='18.6.389+449',
            required_source_root_sha256='b' * 64, board_family='bk7258',
            physical_board='aidk_ai_toy', layout_identity='bk7258-0123456789abcdef',
            layout_sha256='c' * 64, package_sha256='d' * 64, package_size_bytes=100,
        )
        url, peer, wire, _ = await self.console_fixture(
            CAP_OTA | CAP_STATUS_REPORT, FirmwareReleases((release,)))
        connection = self.gateway.device_connection('board-1')
        connection.device_status = StatusReport(None, None, None, None,
                                                '18.6.389+449', 'b' * 64)
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            mutation = self.console_mutation(
                snapshot, 'ota-1', operation='firmware.update',
                arguments={'release_manifest_sha256': 'a' * 64})
            submit = asyncio.create_task(client.post(url + '/mutations', json=mutation,
                                                     ssl=self.client_tls))
            request = Frame.decode(await peer.recv())
            self.assertEqual((request.message_type, request.flags, request.turn_id,
                              request.payload), (MessageType.OTA_REQUEST, 0, 0, b'\xaa' * 32))
            self.assertFalse(submit.done())
            # Valid peer frames that do not bind this request cannot change status.
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence + 1, 0, 1, 0, 0, b'\xaa' * 32)).encode())
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 1, 0, 0, b'\xbb' * 32)).encode())
            await asyncio.sleep(0)
            self.assertFalse(submit.done())
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 1, 3, 0, b'\xaa' * 32)).encode())
            receipt = await (await submit).json()
            self.assertEqual((receipt['status'], receipt['error']), ('accepted', None))
            projected = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            self.assertEqual(projected['payload']['update'], {
                'phase': 'downloading', 'target_version': '18.6.390+450',
                'progress_percent': 3, 'error': None,
            })
            # Same-phase progress and ordinary phase ordering cannot regress.
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 1, 2, 0, b'\xaa' * 32)).encode())
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 2, 4, 0, b'\xaa' * 32)).encode())
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 1, 99, 0, b'\xaa' * 32)).encode())
            for _ in range(20):
                if connection.ota_status is not None and connection.ota_status.phase == 2:
                    break
                await asyncio.sleep(0.01)
            self.assertEqual((connection.ota_status.phase, connection.ota_status.progress_percent),
                             (2, 4))
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 6, 100, 0, b'\xaa' * 32)).encode())
            for _ in range(20):
                projected = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
                if projected['payload']['update']['phase'] == 'confirmed':
                    break
                await asyncio.sleep(0.01)
            self.assertEqual(projected['payload']['update']['phase'], 'confirmed')
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 5, 90, 0, b'\xaa' * 32)).encode())
            await asyncio.sleep(0)
            self.assertEqual(connection.ota_status.phase, 6)
            # A stale sent marker from this terminal request cannot turn the
            # next busy rejection into an uncertain dispatched update.
            connection._ota_future = asyncio.get_running_loop().create_future()
            blocked = self.console_mutation(
                await (await client.get(url + '/snapshot', ssl=self.client_tls)).json(),
                'ota-busy-after-terminal', operation='firmware.update',
                arguments={'release_manifest_sha256': 'a' * 64})
            response = await client.post(url + '/mutations', json=blocked, ssl=self.client_tls)
            blocked_receipt = await response.json()
            self.assertEqual((blocked_receipt['error'], blocked_receipt['revision']),
                             ('revision_conflict', 1))
            connection._ota_future.cancel()
            connection._ota_future = None

    async def test_console_ota_failed_report_rejects_and_projects_device_error(self):
        release = FirmwareRelease(
            device_id='board-1', manifest_sha256='a' * 64,
            target_version='18.6.390+450', required_source_version='18.6.389+449',
            required_source_root_sha256='b' * 64, board_family='bk7258',
            physical_board='aidk_ai_toy', layout_identity='bk7258-0123456789abcdef',
            layout_sha256='c' * 64, package_sha256='d' * 64, package_size_bytes=100,
        )
        url, peer, wire, _ = await self.console_fixture(
            CAP_OTA, FirmwareReleases((release,)))
        connection = self.gateway.device_connection('board-1')
        connection.device_status = StatusReport(None, None, None, None,
                                                '18.6.389+449', 'b' * 64)
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            mutation = self.console_mutation(snapshot, 'ota-failed', operation='firmware.update',
                                             arguments={'release_manifest_sha256': 'a' * 64})
            submit = asyncio.create_task(client.post(url + '/mutations', json=mutation,
                                                     ssl=self.client_tls))
            request = Frame.decode(await peer.recv())
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, -9, 8, 17, 0, b'\xaa' * 32)).encode())
            receipt = await (await submit).json()
            self.assertEqual((receipt['status'], receipt['error'], receipt['revision']),
                             ('rejected', 'update_device_error', 1))
            projected = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            self.assertEqual(projected['payload']['update'], {
                'phase': 'failed', 'target_version': '18.6.390+450',
                'progress_percent': 17, 'error': 'update_device_error',
            })

    async def test_console_ota_timeout_retains_release_for_late_report(self):
        release = FirmwareRelease(
            device_id='board-1', manifest_sha256='a' * 64,
            target_version='18.6.390+450', required_source_version='18.6.389+449',
            required_source_root_sha256='b' * 64, board_family='bk7258',
            physical_board='aidk_ai_toy', layout_identity='bk7258-0123456789abcdef',
            layout_sha256='c' * 64, package_sha256='d' * 64, package_size_bytes=100,
        )
        url, peer, wire, _ = await self.console_fixture(CAP_OTA, FirmwareReleases((release,)))
        connection = self.gateway.device_connection('board-1')
        connection.config = replace(connection.config, ota_timeout_seconds=0.01)
        connection.device_status = StatusReport(None, None, None, None,
                                                '18.6.389+449', 'b' * 64)
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            mutation = self.console_mutation(snapshot, 'ota-late', operation='firmware.update',
                                             arguments={'release_manifest_sha256': 'a' * 64})
            submit = asyncio.create_task(client.post(url + '/mutations', json=mutation,
                                                     ssl=self.client_tls))
            request = Frame.decode(await peer.recv())
            receipt = await (await submit).json()
            self.assertEqual((receipt['status'], receipt['error'], receipt['revision']),
                             ('rejected', 'update_device_error', 1))
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 1, 9, 0, b'\xaa' * 32)).encode())
            for _ in range(20):
                projected = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
                if projected['payload']['update']['phase'] == 'downloading':
                    break
                await asyncio.sleep(0.01)
            self.assertEqual(projected['payload']['update']['target_version'], '18.6.390+450')
            with self.assertRaisesRegex(ProtocolError, 'ota_busy'):
                await connection.request_ota('c' * 64)

    async def test_console_ota_persists_disconnect_and_reconciles_new_boot(self):
        release = FirmwareRelease(
            device_id='board-1', manifest_sha256='a' * 64,
            target_version='18.6.390+450', required_source_version='18.6.389+449',
            required_source_root_sha256='b' * 64, board_family='bk7258',
            physical_board='aidk_ai_toy', layout_identity='bk7258-0123456789abcdef',
            layout_sha256='c' * 64, package_sha256='d' * 64,
            package_size_bytes=100,
        )
        url, peer, wire, service = await self.console_fixture(
            CAP_OTA, FirmwareReleases((release,)), ota_store=True)
        connection = self.gateway.device_connection('board-1')
        connection.device_status = StatusReport(None, None, None, None,
                                                release.required_source_version,
                                                release.required_source_root_sha256)
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            mutation = self.console_mutation(
                snapshot, 'ota-durable', operation='firmware.update',
                arguments={'release_manifest_sha256': release.manifest_sha256})
            submit = asyncio.create_task(
                client.post(url + '/mutations', json=mutation, ssl=self.client_tls))
            request = Frame.decode(await peer.recv())
            row = service.ota_transactions.load('board-1')
            self.assertEqual((row.state, row.dispatch_boot_generation,
                              row.dispatch_session_id, row.dispatch_sequence),
                             ('awaiting_first_report', wire.boot_generation,
                              wire.session_id, request.sequence))
            await peer.send(wire.frame(MessageType.OTA_REPORT, payload=struct.pack(
                '!IiBBH32s', request.sequence, 0, 1, 19, 0, b'\xaa' * 32)).encode())
            self.assertEqual((await (await submit).json())['status'], 'accepted')
            self.assertEqual(service.ota_transactions.load('board-1').state,
                             'downloading')

            board_port = peer.remote_address[1]
            await peer.close()
            await self.wait_for_summary()
            for _ in range(100):
                if self.gateway.device_connection('board-1') is None:
                    break
                await asyncio.sleep(0.005)
            self.assertEqual(service.ota_transactions.load('board-1').state,
                             'uncertain')

            reconnect_wire = ClientWire(boot_generation=8, session_id=4)
            async with websockets.connect(
                    f'wss://localhost:{board_port}{DEFAULT_PATH}',
                    ssl=self.mtls_client_context(self.trusted_client_cert,
                                                 self.trusted_client_key),
                    subprotocols=[SUBPROTOCOL]) as reconnected:
                await self.handshake(
                    reconnected, reconnect_wire, CAP_OTA | CAP_STATUS_REPORT)
                status_payload = struct.pack(
                    '!BBBBIHHHHI32s', 2, 0xff, 2, 0xff, 0,
                    18, 6, 390, 0, 450, bytes.fromhex('b' * 64))
                await reconnected.send(reconnect_wire.frame(
                    MessageType.STATUS_REPORT, payload=status_payload).encode())
                active = None
                for _ in range(20):
                    active = self.gateway.device_connection('board-1')
                    if active is not None and active.device_status is not None:
                        break
                    await asyncio.sleep(0.01)
                self.assertIsNotNone(active, self.events)
                self.assertIsNotNone(active.device_status, self.events)
                resumed = Frame.decode(await asyncio.wait_for(reconnected.recv(), 1))
                self.assertEqual((resumed.message_type, resumed.payload),
                                 (MessageType.OTA_REQUEST, bytes.fromhex('a' * 64)))
                for phase, progress in ((1, 0), (2, 100), (3, 100), (4, 100),
                                        (5, 100), (6, 100)):
                    await reconnected.send(reconnect_wire.frame(
                        MessageType.OTA_REPORT, payload=struct.pack(
                            '!IiBBH32s', resumed.sequence, 0, phase, progress,
                            0, b'\xaa' * 32)).encode())
                for _ in range(50):
                    row = service.ota_transactions.load('board-1')
                    if row.state == 'confirmed':
                        break
                    await asyncio.sleep(0.01)
                self.assertEqual(row.state, 'confirmed')
                projected = await (await client.get(
                    url + '/snapshot', ssl=self.client_tls)).json()
                self.assertEqual(projected['payload']['update']['phase'], 'confirmed')

    async def test_console_ota_terminal_report_survives_disconnect_until_status(self):
        release = FirmwareRelease(
            device_id='board-1', manifest_sha256='a' * 64,
            target_version='18.6.390+450', required_source_version='18.6.389+449',
            required_source_root_sha256='b' * 64, board_family='bk7258',
            physical_board='aidk_ai_toy', layout_identity='bk7258-0123456789abcdef',
            layout_sha256='c' * 64, package_sha256='d' * 64,
            package_size_bytes=100,
        )
        url, peer, wire, service = await self.console_fixture(
            CAP_OTA | CAP_STATUS_REPORT, FirmwareReleases((release,)), ota_store=True)
        connection = self.gateway.device_connection('board-1')
        connection.device_status = StatusReport(None, None, None, None,
                                                release.required_source_version,
                                                release.required_source_root_sha256)
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            submit = asyncio.create_task(client.post(
                url + '/mutations', ssl=self.client_tls,
                json=self.console_mutation(
                    snapshot, 'ota-terminal-pending', operation='firmware.update',
                    arguments={'release_manifest_sha256': release.manifest_sha256})))
            request = Frame.decode(await peer.recv())
            for phase, progress in ((1, 1), (2, 100), (3, 100), (4, 100),
                                    (5, 100), (6, 100)):
                await peer.send(wire.frame(
                    MessageType.OTA_REPORT,
                    payload=struct.pack('!IiBBH32s', request.sequence, 0,
                                        phase, progress, 0, b'\xaa' * 32)).encode())
            self.assertEqual((await (await submit).json())['status'], 'accepted')
            for _ in range(20):
                row = service.ota_transactions.load('board-1')
                if row.state == 'confirming':
                    break
                await asyncio.sleep(0.01)
            self.assertEqual(row.state, 'confirming')

            board_port = peer.remote_address[1]
            await peer.close()
            await self.wait_for_summary()
            self.assertEqual(service.ota_transactions.load('board-1').state,
                             'confirming')

            reconnect_wire = ClientWire(boot_generation=8, session_id=4)
            async with websockets.connect(
                    f'wss://localhost:{board_port}{DEFAULT_PATH}',
                    ssl=self.mtls_client_context(self.trusted_client_cert,
                                                 self.trusted_client_key),
                    subprotocols=[SUBPROTOCOL]) as reconnected:
                await self.handshake(
                    reconnected, reconnect_wire, CAP_OTA | CAP_STATUS_REPORT)
                status_payload = struct.pack(
                    '!BBBBIHHHHI32s', 2, 0xff, 2, 0xff, 0,
                    18, 6, 390, 0, 450, bytes.fromhex('b' * 64))
                await reconnected.send(reconnect_wire.frame(
                    MessageType.STATUS_REPORT, payload=status_payload).encode())
                for _ in range(20):
                    row = service.ota_transactions.load('board-1')
                    if row.state == 'confirmed':
                        break
                    await asyncio.sleep(0.01)
                self.assertEqual(row.state, 'confirmed')
                with self.assertRaises(asyncio.TimeoutError):
                    await asyncio.wait_for(reconnected.recv(), 0.05)

    async def test_console_ota_rejects_registry_capability_busy_and_timeout(self):
        release = FirmwareRelease(
            device_id='board-1', manifest_sha256='a' * 64,
            target_version='18.6.390+450', required_source_version='18.6.389+449',
            required_source_root_sha256='b' * 64, board_family='bk7258',
            physical_board='aidk_ai_toy', layout_identity='bk7258-0123456789abcdef',
            layout_sha256='c' * 64, package_sha256='d' * 64, package_size_bytes=100,
        )
        url, peer, _, _ = await self.console_fixture(CAP_OTA, FirmwareReleases((release,)))
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        connection = self.gateway.device_connection('board-1')
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            invalid = self.console_mutation(snapshot, 'ota-source', operation='firmware.update',
                arguments={'release_manifest_sha256': 'a' * 64})
            response = await client.post(url + '/mutations', json=invalid, ssl=self.client_tls)
            self.assertEqual((await response.json())['error'], 'update_manifest_invalid')
            invalid_digest = self.console_mutation(snapshot, 'ota-sentinel', operation='firmware.update',
                arguments={'release_manifest_sha256': '0' * 64})
            response = await client.post(url + '/mutations', json=invalid_digest,
                                         ssl=self.client_tls)
            self.assertEqual(response.status, 400)
            connection.device_status = StatusReport(None, None, None, None,
                                                    '18.6.389+449', 'b' * 64)
            connection.session.capabilities = 0
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            no_cap = self.console_mutation(snapshot, 'ota-cap', operation='firmware.update',
                arguments={'release_manifest_sha256': 'a' * 64})
            response = await client.post(url + '/mutations', json=no_cap, ssl=self.client_tls)
            self.assertEqual((await response.json())['error'], 'unsupported_operation')
            connection.session.capabilities = CAP_OTA
            connection.session.state = SessionState.UPLINK
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            busy = self.console_mutation(snapshot, 'ota-busy', operation='firmware.update',
                arguments={'release_manifest_sha256': 'a' * 64})
            response = await client.post(url + '/mutations', json=busy, ssl=self.client_tls)
            self.assertEqual(response.status, 409)
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(peer.recv(), 0.03)

    async def test_console_memory_clear_and_revoke_are_device_scoped(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        memory = MemoryStore(Path(directory.name) / 'memory.db', ('board-1',))
        self.addCleanup(memory.close)
        memory.append('board-1', 'saved-user', 'saved-reply')
        url, peer, _, _ = await self.console_fixture(memory_store=memory)
        connection = self.gateway.device_connection('board-1')
        self.assertIsNotNone(connection)
        connection.reply_session._history = [(0, 'saved-user', 'saved-reply')]
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            snapshot = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            self.assertEqual(snapshot['payload']['permissions']['long_term_memory'], 'allowed')

            clear = self.console_mutation(
                snapshot, 'memory-clear', operation='memory.delete',
                arguments={'scope': 'conversations'},
            )
            async with client.post(url + '/mutations', json=clear,
                                   ssl=self.client_tls) as response:
                receipt = await response.json()
            self.assertEqual(receipt['status'], 'accepted')
            self.assertEqual(memory.context('board-1'), ())
            self.assertEqual(connection.reply_session._history, [])
            current = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            self.assertEqual(current['payload']['permissions']['long_term_memory'], 'allowed')

            enable = self.console_mutation(
                current, 'memory-enable', operation='permission.configure',
                arguments={'capability': 'long_term_memory', 'state': 'allowed'},
            )
            async with client.post(url + '/mutations', json=enable,
                                   ssl=self.client_tls) as response:
                refused = await response.json()
            self.assertEqual(refused['error'], 'local_confirmation_required')
            self.assertEqual(memory.permission('board-1'), 'enabled')

            memory.append('board-1', 'remove-user', 'remove-reply')
            connection.reply_session._history = [(0, 'remove-user', 'remove-reply')]
            revoke = self.console_mutation(
                current, 'memory-revoke', operation='permission.configure',
                arguments={'capability': 'long_term_memory', 'state': 'denied'},
            )
            async with client.post(url + '/mutations', json=revoke,
                                   ssl=self.client_tls) as response:
                revoked = await response.json()
            self.assertEqual(revoked['status'], 'accepted')
            self.assertEqual(memory.permission('board-1'), 'revoked')
            self.assertEqual(memory.context('board-1'), ())
            self.assertEqual(connection.reply_session._history, [])
            denied = await (await client.get(url + '/snapshot', ssl=self.client_tls)).json()
            self.assertEqual(denied['payload']['permissions']['long_term_memory'], 'denied')

            invalid = self.console_mutation(
                denied, 'memory-profile', operation='memory.delete',
                arguments={'scope': 'profile'},
            )
            async with client.post(url + '/mutations', json=invalid,
                                   ssl=self.client_tls) as response:
                self.assertEqual(response.status, 400)

            memory.db.execute('DROP TABLE memory_turns')
            memory.db.commit()
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                self.assertEqual(response.status, 503)
                self.assertEqual(await response.text(), 'console_state_unavailable')

        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(peer.recv(), 0.03)

    async def test_console_cancel_revision_binding_and_concurrent_retry(self):
        url, peer, wire, _ = await self.console_fixture()
        headers = {'Authorization': 'Bearer ' + 'w' * 43}
        async with ClientSession(headers=headers) as client:
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                idle = await response.json()
            await peer.send(wire.frame(MessageType.TURN_START, turn_id=1).encode())
            await peer.send(wire.frame(MessageType.AUDIO_UP, turn_id=1,
                                       payload=bytes(AUDIO_FRAME_BYTES)).encode())
            await peer.recv()  # Window response proves TURN_START was consumed.
            old = self.console_mutation(idle, 'old', operation='turn.cancel', arguments={})
            async with client.post(url + '/mutations', json=old, ssl=self.client_tls) as response:
                self.assertEqual((await response.json())['error'], 'revision_conflict')
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                active = await response.json()
            mutation = self.console_mutation(active, 'cancel-1', operation='turn.cancel', arguments={})

            async def submit():
                async with client.post(url + '/mutations', json=mutation,
                                       ssl=self.client_tls) as response:
                    self.assertEqual(response.status, 200)
                    return await response.json()

            receipts = await asyncio.gather(submit(), submit())
            self.assertEqual(receipts[0], receipts[1])
            self.assertEqual(receipts[0]['status'], 'accepted')
            cancel = Frame.decode(await peer.recv())
            self.assertEqual((cancel.message_type, cancel.turn_id), (MessageType.CANCEL, 1))
            self.assertEqual(self.gateway.device_connection('board-1').metrics.turns_cancelled, 1)
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                after_cancel = await response.json()
            self.assertEqual(after_cancel['payload']['turn'], 'cancelling')
            for ack_turn, expected in ((0, 'cancelling'), (1, 'idle')):
                await peer.send(wire.frame(MessageType.ACK, turn_id=ack_turn).encode())
                await peer.send(wire.frame(MessageType.AUDIO_UP, turn_id=1,
                                           payload=bytes(AUDIO_FRAME_BYTES)).encode())
                await peer.recv()
                async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                    observed = await response.json()
                self.assertEqual(observed['payload']['turn'], expected)
            self.assertEqual(sum(e.get('event') == 'remote_cancel_confirmed'
                                 for e in self.events), 1)
            await peer.send(wire.frame(MessageType.TURN_START, turn_id=2).encode())
            await peer.send(wire.frame(MessageType.AUDIO_UP, turn_id=2,
                                       payload=bytes(AUDIO_FRAME_BYTES)).encode())
            await peer.recv()
            # No snapshot poll in this new turn: the endpoint must still notice it.
            stale = self.console_mutation(after_cancel, 'late', operation='turn.cancel', arguments={})
            async with client.post(url + '/mutations', json=stale, ssl=self.client_tls) as response:
                self.assertEqual((await response.json())['error'], 'revision_conflict')
            self.assertEqual(self.gateway.device_connection('board-1').session.state, SessionState.UPLINK)
            self.assertEqual(self.gateway.device_connection('board-1').metrics.turns_cancelled, 1)

    async def test_console_playback_waits_for_ack_and_reports_unknown_on_timeout(self):
        url, peer, wire, _ = await self.console_fixture()
        provider, _, _ = await self._mimo_endpoint()
        connection = self.gateway.device_connection('board-1')
        connection.reply_session = provider.new_conversation()
        await self._send_provider_turn(peer, wire)
        await self._drain_provider_turn(peer)
        async with ClientSession(headers={'Authorization': 'Bearer ' + 'w' * 43}) as client:
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                snapshot = await response.json()
            self.assertEqual(snapshot['payload']['turn'], 'speaking')
            mutation = self.console_mutation(snapshot)
            async with client.post(url + '/mutations', json=mutation, ssl=self.client_tls) as response:
                self.assertEqual(response.status, 409)
            connection.config = replace(connection.config, playback_ack_timeout_seconds=0.02)
            await asyncio.sleep(0.03)
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                self.assertEqual((await response.json())['payload']['turn'], 'playback_unconfirmed')
            self.assertFalse(connection.reply_session._history)
            await peer.send(wire.frame(MessageType.ACK, turn_id=1).encode())
            for _ in range(100):
                async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                    snapshot = await response.json()
                if snapshot['payload']['turn'] == 'idle':
                    break
                await asyncio.sleep(0.01)
            self.assertEqual(snapshot['payload']['turn'], 'idle')
            self.assertEqual(len(connection.reply_session._history), 1)
            cancel = self.console_mutation(snapshot, 'idle-cancel',
                                           operation='turn.cancel', arguments={})
            async with client.post(url + '/mutations', json=cancel, ssl=self.client_tls) as response:
                self.assertEqual(response.status, 409)
            self.assertEqual(len(connection.reply_session._history), 1)

    async def test_cancel_ack_timeout_is_unknown_and_late_ack_recovers(self):
        url, peer, wire, _ = await self.console_fixture()
        connection = self.gateway.device_connection('board-1')
        connection.config = replace(connection.config, cancel_ack_timeout_seconds=0.02)
        await peer.send(wire.frame(MessageType.TURN_START, turn_id=1).encode())
        await peer.send(wire.frame(MessageType.AUDIO_UP, turn_id=1,
                                   payload=bytes(AUDIO_FRAME_BYTES)).encode())
        await peer.recv()
        await connection.cancel_turn(1)
        self.assertEqual(Frame.decode(await peer.recv()).message_type, MessageType.CANCEL)
        await asyncio.sleep(0.03)
        async with ClientSession(headers={'Authorization': 'Bearer ' + 'w' * 43}) as client:
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                self.assertEqual((await response.json())['payload']['turn'], 'cancel_unconfirmed')
            self.assertEqual(connection.cancel_pending_turn, 1)
            self.assertFalse(any(e.get('event') == 'remote_cancel_confirmed' for e in self.events))
            await peer.send(wire.frame(MessageType.ACK, turn_id=1).encode())
            await peer.send(wire.frame(MessageType.AUDIO_UP, turn_id=1,
                                       payload=bytes(AUDIO_FRAME_BYTES)).encode())
            await peer.recv()
            async with client.get(url + '/snapshot', ssl=self.client_tls) as response:
                self.assertEqual((await response.json())['payload']['turn'], 'idle')

    async def test_console_authorization_expiry_schema_and_busy_turn(self):
        url, peer, wire, service = await self.console_fixture()
        async with ClientSession() as client:
            for char in ('', 'x', 'e'):
                headers = {} if not char else {'Authorization': 'Bearer ' + char * 43}
                async with client.get(url + '/snapshot', headers=headers, ssl=self.client_tls) as response:
                    self.assertEqual(response.status, 401)
            headers = {'Authorization': 'Bearer ' + 'w' * 43}
            async with client.get(url.replace('board-1', 'board-2') + '/snapshot',
                                  headers=headers, ssl=self.client_tls) as response:
                self.assertEqual(response.status, 403)
            async with client.get(url + '/snapshot', headers=headers, ssl=self.client_tls) as response:
                snapshot = await response.json()
            mutation = self.console_mutation(snapshot)
            async with client.post(url + '/mutations', json=mutation,
                                   headers={'Authorization': 'Bearer ' + 'r' * 43},
                                   ssl=self.client_tls) as response:
                self.assertEqual(response.status, 403)
            for change in (dict(generation=True), dict(unexpected=True),
                           dict(arguments={'persona_mode': 'unknown'})):
                async with client.post(url + '/mutations', json=dict(mutation, **change),
                                       headers=headers, ssl=self.client_tls) as response:
                    self.assertEqual(response.status, 400)
            expired = dict(mutation, request_id='expired', issued_at_ms=1, expires_at_ms=2)
            async with client.post(url + '/mutations', json=expired, headers=headers,
                                   ssl=self.client_tls) as response:
                self.assertEqual((await response.json())['error'], 'request_expired')
            await peer.send(wire.frame(MessageType.TURN_START, turn_id=1).encode())
            for _ in range(100):
                async with client.get(url + '/snapshot', headers=headers, ssl=self.client_tls) as response:
                    current = await response.json()
                if current['payload']['turn'] == 'listening':
                    break
                await asyncio.sleep(0.01)
            self.assertEqual(current['payload']['turn'], 'listening')
            mutation = self.console_mutation(current)
            async with client.post(url + '/mutations', json=mutation, headers=headers,
                                   ssl=self.client_tls) as response:
                self.assertEqual(response.status, 409)
            await peer.send(wire.frame(MessageType.CANCEL, turn_id=1).encode())
            await peer.close()
            await self.wait_for_summary()
            async with client.get(url + '/snapshot', headers=headers, ssl=self.client_tls) as response:
                self.assertEqual(response.status, 503)

    async def test_bind_checks_actual_tls_context(self) -> None:
        context = build_tls_context(self.cert_path, self.key_path)
        with self.assertRaisesRegex(ValueError, "require client certificates"):
            await start_gateway_server(
                self.gateway, "192.168.1.7", 0, context, self.ca_path
            )
        context.minimum_version = ssl.TLSVersion.MINIMUM_SUPPORTED
        with self.assertRaisesRegex(ValueError, "TLS 1.2"):
            await start_gateway_server(self.gateway, "127.0.0.1", 0, context)

    def _firmware_content_fixture(self, *, compressed=False):
        root = Path(self.temp_dir.name)
        package = root / ('compressed.bkpack' if compressed else 'ota.bkpack')
        cp, ap = b'cp-image-content', b'ap-image-content-more'
        base = {
            'format': 'bk7258.ota/2', 'board_family': 'bk7258',
            'target': {'board_family': 'bk7258', 'physical_board': 'aidk_ai_toy'},
            'layout': {'identity': 'bk7258-0123456789abcdef', 'sha256': 'c' * 64},
            'version': '18.6.390+450', 'security_counter': 450,
            'cp': {'uri': 'images/cp/cp.bin', 'size': len(cp),
                   'sha256': hashlib.sha256(cp).hexdigest()},
            'ap': {'uri': 'images/ap/ap.bin', 'size': len(ap),
                   'sha256': hashlib.sha256(ap).hexdigest()},
        }
        catalog = dict(base)
        catalog['package_id'] = hashlib.sha256(
            (json.dumps(base, sort_keys=True, separators=(',', ':'), ensure_ascii=True) + '\n').encode()
        ).hexdigest()
        catalog_bytes = (json.dumps(catalog, sort_keys=True, separators=(',', ':'),
                                    ensure_ascii=True) + '\n').encode()
        compression = zipfile.ZIP_DEFLATED if compressed else zipfile.ZIP_STORED
        with zipfile.ZipFile(package, 'w', compression=compression, allowZip64=False) as archive:
            archive.writestr('catalog.json', catalog_bytes)
            archive.writestr('catalog.sig', b'0\x08fixture!')
            archive.writestr('images/cp/cp.bin', cp)
            archive.writestr('images/ap/ap.bin', ap)
        package.chmod(0o600)
        release = FirmwareRelease(
            device_id='board-1', manifest_sha256=hashlib.sha256(catalog_bytes).hexdigest(),
            target_version='18.6.390+450', required_source_version='18.6.389+449',
            required_source_root_sha256='b' * 64, board_family='bk7258',
            physical_board='aidk_ai_toy', layout_identity='bk7258-0123456789abcdef',
            layout_sha256='c' * 64,
            package_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
            package_size_bytes=package.stat().st_size,
        )
        return package, release, catalog_bytes, cp, ap

    def test_firmware_content_loader_rejects_compressed_package(self) -> None:
        package, release, _, _, _ = self._firmware_content_fixture(compressed=True)
        with self.assertRaises(FirmwareContentError):
            FirmwareContentStore(FirmwareReleases((release,)), (package,))

    async def test_firmware_content_requires_live_bound_ota_and_serves_bounded_ranges(self) -> None:
        package, release, catalog, cp, _ = self._firmware_content_fixture()
        store = FirmwareContentStore(FirmwareReleases((release,)), (package,))
        self.addCleanup(store.close)
        certificate_hash = hashlib.sha256(
            ssl.PEM_cert_to_DER_cert(self.trusted_client_cert.read_text())
        ).hexdigest()
        gateway = GatewayServer(
            device_bindings=DeviceBindings({certificate_hash: 'board-1'}),
            firmware_content=store,
        )
        server = await start_gateway_server(
            gateway, '127.0.0.1', 0,
            build_tls_context(self.mtls_cert_path, self.mtls_key_path, self.ca_path),
            self.ca_path,
        )
        self.addAsyncCleanup(server.wait_closed)
        self.addAsyncCleanup(server.close)
        port = server.sockets[0].getsockname()[1]
        context = self.mtls_client_context(self.trusted_client_cert, self.trusted_client_key)
        base = f'https://localhost:{port}/firmware/v1/{release.manifest_sha256}'
        async with ClientSession() as client:
            async with client.get(base + '/catalog.json', ssl=context) as response:
                self.assertEqual(response.status, 403)
        wire = ClientWire()
        async with websockets.connect(
                f'wss://localhost:{port}{DEFAULT_PATH}', ssl=context,
                subprotocols=[SUBPROTOCOL], compression=None) as peer:
            await self.handshake(peer, wire, CAP_OTA)
            connection = gateway.device_connection('board-1')
            self.assertIsNotNone(connection)
            connection.ota_status = OtaStatus(release.manifest_sha256, 1, 5, 0)
            async with ClientSession() as client:
                async with client.get(base + '/catalog.json', ssl=context) as response:
                    self.assertEqual(response.status, 200)
                    self.assertEqual(await response.read(), catalog)
                    self.assertEqual(response.headers['Cache-Control'], 'no-store')
                    self.assertEqual(response.headers['X-Content-Type-Options'], 'nosniff')
                async with client.get(base + '/images/cp/cp.bin', ssl=context,
                                      headers={'Range': 'bytes=1-3'}) as response:
                    self.assertEqual(response.status, 206)
                    self.assertEqual(await response.read(), cp[1:4])
                    self.assertEqual(response.headers['Content-Range'],
                                     f'bytes 1-3/{len(cp)}')
                    self.assertEqual(response.headers['Accept-Ranges'], 'bytes')
                # Catalog image paths are exact and no path normalization or
                # package listing is available.
                async with client.get(
                        base + '/images%2Fcp%2Fcp.bin', ssl=context,
                        headers={'Range': 'bytes=1-3'}) as response:
                    self.assertEqual(response.status, 404)
                async with client.get(base + '/images/cp/cp.bin?x=1', ssl=context,
                                      headers={'Range': 'bytes=1-3'}) as response:
                    self.assertEqual(response.status, 400)
                async with client.get(base + '/images/cp/cp.bin', ssl=context,
                                      headers={'Range': 'bytes=0-16384'}) as response:
                    self.assertEqual(response.status, 416)
            connection.ota_status = OtaStatus(release.manifest_sha256, 6, 100, 0)
            async with ClientSession() as client:
                async with client.get(base + '/catalog.sig', ssl=context) as response:
                    self.assertEqual(response.status, 403)

    async def test_mtls_rejects_missing_or_untrusted_client_certificate(self) -> None:
        server = await self.start_mtls_server()
        port = server.sockets[0].getsockname()[1]
        self.expect_tls_handshake_rejection = True
        for context in (
            self.mtls_client_context(),
            self.mtls_client_context(
                self.untrusted_client_cert, self.untrusted_client_key
            ),
        ):
            with self.assertRaises((OSError, ssl.SSLError, websockets.InvalidMessage)):
                async with websockets.connect(
                    f"wss://localhost:{port}{DEFAULT_PATH}",
                    ssl=context, subprotocols=[SUBPROTOCOL],
                ):
                    pass

    def test_bind_scope_gate_table(self) -> None:
        cases = (
            ("127.0.0.1", None, "loopback"),
            ("::1", None, "loopback"),
            ("localhost", None, "loopback"),
            ("192.168.1.7", self.ca_path, "mtls_unicast"),
            ("2001:db8::7", self.ca_path, "mtls_unicast"),
        )
        for host, client_ca, expected in cases:
            self.assertEqual(_bind_scope(host, client_ca), expected)

        for host, client_ca in (
            ("192.168.1.7", None), ("0.0.0.0", self.ca_path),
            ("::", self.ca_path), ("255.255.255.255", self.ca_path),
            ("224.0.0.1", self.ca_path), ("gateway.example", self.ca_path),
        ):
            with self.assertRaises(ValueError):
                _bind_scope(host, client_ca)


if __name__ == "__main__":
    unittest.main()
