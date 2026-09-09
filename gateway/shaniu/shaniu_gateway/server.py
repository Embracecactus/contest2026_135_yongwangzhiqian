# SPDX-License-Identifier: Apache-2.0
"""TLS-only companion-v1 endpoint with fixed-fixture or MiMo replies."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import hashlib
import ipaddress
import json
import logging
import shutil
import sqlite3
import ssl
import struct
import sys
import time
from collections.abc import AsyncIterator, Callable, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol
import websockets
from websockets.exceptions import ConnectionClosed
from websockets.server import WebSocketServer, WebSocketServerProtocol

from .devices import DeviceBindings, load_device_bindings
from .firmware import FirmwareReleases
from .firmware_content import FirmwareContentStore
from .memory import MemoryStateError, MemoryStore
from .ota_transactions import (
    OtaTransactionError,
    OtaTransactionStore,
)

from .protocol import (
    AUDIO_FRAME_BYTES,
    CAP_OTA,
    HEADER_BYTES,
    MAX_PAYLOAD,
    Frame,
    GatewaySession,
    MessageType,
    ProtocolError,
    SessionEvent,
    SessionState,
    StatusReport,
    decode_status_report,
)


SUBPROTOCOL = "companion-v1"
DEFAULT_PATH = "/companion/v1"
_LOOPBACK_HOSTS = {"127.0.0.1", "::1", "localhost"}
EventSink = Callable[[Mapping[str, object]], None]


class ReplySession(Protocol):
    def reply(self, pcm: bytes) -> AsyncIterator[bytes]: ...
    def finish(self, turn_id: int, *, success: bool) -> None: ...
    def close(self) -> None: ...


ReplyFactory = Callable[[], ReplySession]
DeviceReplyFactory = Callable[[str], ReplySession]


def json_event_sink(event: Mapping[str, object]) -> None:
    """Write a log-safe aggregate event without payloads or device identity."""

    print(
        json.dumps(dict(event), sort_keys=True, separators=(",", ":")),
        file=sys.stderr,
        flush=True,
    )


@dataclass(frozen=True, slots=True)
class GatewayConfig:
    path: str = DEFAULT_PATH
    reply_frames: int = 10
    reply_interval_ms: int = 20
    downlink_window_timeout_ms: int = 2_000
    max_uplink_bytes: int = 30 * 16000 * 2
    reply_timeout_seconds: float = 110.0
    hello_timeout_seconds: float = 5.0
    cancel_ack_timeout_seconds: float = 5.0
    playback_ack_timeout_seconds: float = 5.0
    volume_timeout_seconds: float = 5.0
    ota_timeout_seconds: float = 10.0
    heartbeat_interval_seconds: float = 30.0

    def __post_init__(self) -> None:
        if not self.path.startswith("/") or "?" in self.path or "#" in self.path:
            raise ValueError("path must be an absolute path without query or fragment")
        if not 2 <= self.reply_frames <= 500:
            raise ValueError("reply_frames must be in 2..500")
        if not 0 <= self.reply_interval_ms <= 1_000:
            raise ValueError("reply_interval_ms must be in 0..1000")
        if not 10 <= self.downlink_window_timeout_ms <= 60_000:
            raise ValueError("downlink_window_timeout_ms must be in 10..60000")
        if not AUDIO_FRAME_BYTES <= self.max_uplink_bytes <= 30 * 16000 * 2:
            raise ValueError("uplink limit must fit at most 30 seconds of PCM")
        if not 0 < self.reply_timeout_seconds <= 110:
            raise ValueError("reply timeout must be in 0..110 seconds")
        if not 0 < self.hello_timeout_seconds <= 30:
            raise ValueError("HELLO timeout must be in 0..30 seconds")
        if not 0 < self.cancel_ack_timeout_seconds <= 30:
            raise ValueError('cancel ACK timeout must be in 0..30 seconds')
        if not 0 < self.playback_ack_timeout_seconds <= 30:
            raise ValueError('playback ACK timeout must be in 0..30 seconds')
        if not 0 < self.volume_timeout_seconds <= 30:
            raise ValueError('volume timeout must be in 0..30 seconds')
        if not 0 < self.ota_timeout_seconds <= 30:
            raise ValueError('OTA timeout must be in 0..30 seconds')
        if not 0 < self.heartbeat_interval_seconds <= 60:
            raise ValueError('heartbeat interval must be in 0..60 seconds')


@dataclass(slots=True)
class ConnectionMetrics:
    turns_started: int = 0
    turns_completed: int = 0
    playback_confirmed: int = 0
    turns_cancelled: int = 0
    aborted_turns: int = 0
    uplink_frames: int = 0
    uplink_bytes: int = 0
    downlink_frames: int = 0
    downlink_bytes: int = 0
    protocol_errors: int = 0


@dataclass(frozen=True, slots=True)
class OtaStatus:
    manifest_sha256: str
    phase: int
    progress_percent: int
    result: int


_OTA_STATE_BY_PHASE = {
    1: "downloading",
    2: "verifying",
    3: "staged",
    4: "rebooting",
    5: "trial",
    6: "confirmed",
    7: "rolled_back",
    8: "failed",
}


class GatewayConnection:
    def __init__(
        self,
        websocket: WebSocketServerProtocol,
        path: str,
        config: GatewayConfig,
        event_sink: EventSink,
        reply_factory: ReplyFactory | None = None,
        device_id: str | None = None,
        ota_report_sink: Callable[[str, "GatewayConnection", OtaStatus], None] | None = None,
        status_report_sink: Callable[[str, "GatewayConnection"], None] | None = None,
    ) -> None:
        self.websocket = websocket
        self.path = path
        self.config = config
        self.emit = event_sink
        self.reply_factory = reply_factory
        self.device_id = device_id
        self.ota_report_sink = ota_report_sink
        self.status_report_sink = status_report_sink
        self.reply_session: ReplySession | None = None
        self._pcm = bytearray()
        self._uplink_bytes = 0
        self.session = GatewaySession()
        self.metrics = ConnectionMetrics()
        self._credit_changed = asyncio.Condition()
        self._reply_task: asyncio.Task[None] | None = None
        self._closing = False
        self._turn_lock = asyncio.Lock()
        self.cancel_pending_turn = 0
        self._cancel_sent_at = 0.0
        self.playback_pending_turn = 0
        self._playback_sent_at = 0.0
        self.volume_percent: int | None = None
        self.device_status: StatusReport | None = None
        self._volume_sequence = 0
        self._volume_future: asyncio.Future[int] | None = None
        self.ota_status: OtaStatus | None = None
        self._ota_sequence = 0
        self._ota_manifest_sha256: str | None = None
        self._ota_future: asyncio.Future[OtaStatus] | None = None
        self.ota_transaction_id: str | None = None
        self.ota_reconcile_attempted = False
        self.ota_reconcile_task: asyncio.Task[None] | None = None
        self.ota_request_sent = False

    async def run(self) -> None:
        if self.path != self.config.path:
            await self.websocket.close(code=1008, reason="invalid_path")
            return
        if self.websocket.subprotocol != SUBPROTOCOL:
            await self.websocket.close(code=1002, reason="subprotocol_required")
            return

        self.emit({"event": "connection_open", "state": self.session.state.value})
        try:
            if self.reply_factory is not None:
                self.reply_session = self.reply_factory()
            async for wire in self._incoming():
                if not isinstance(wire, bytes):
                    raise ProtocolError("binary_frame_required")
                frame = Frame.decode(wire)
                async with self._turn_lock:
                    result = self.session.receive(frame)
                    self._account_inbound(result.event, frame)
                    for outbound in result.outbound:
                        await self._send(outbound)

                    if result.event is SessionEvent.VOLUME_REPORT:
                        self._receive_volume_report(frame)
                    elif result.event is SessionEvent.OTA_REPORT:
                        self._receive_ota_report(frame)
                    elif result.event is SessionEvent.STATUS_REPORT:
                        self.device_status = decode_status_report(frame.payload)
                        if self.device_id is not None and self.status_report_sink is not None:
                            self.status_report_sink(self.device_id, self)
                    elif result.event is SessionEvent.TURN_ENDED:
                        self.emit(
                            {"event": "turn_state", "state": SessionState.THINKING.value}
                        )
                        self._start_reply(frame.turn_id, time.monotonic())
                    elif result.event is SessionEvent.CANCELLED:
                        self.playback_pending_turn = 0
                        self._pcm.clear()
                        self.metrics.turns_cancelled += 1
                        await self._cancel_reply()
                        await self._notify_credit_changed()
                        self.emit({"event": "turn_state", "state": SessionState.IDLE.value})
                    elif result.event is SessionEvent.CONTROL:
                        if (frame.message_type is MessageType.ACK
                                and not self._closing
                                and frame.turn_id != 0
                                and frame.turn_id == self.playback_pending_turn):
                            if self.reply_session is not None:
                                self.reply_session.finish(frame.turn_id, success=True)
                            self.playback_pending_turn = 0
                            self.metrics.playback_confirmed += 1
                            self.emit({'event': 'playback_confirmed', 'turn_id': frame.turn_id})
                        if (frame.message_type is MessageType.ACK
                                and frame.turn_id != 0
                                and frame.turn_id == self.cancel_pending_turn):
                            self.cancel_pending_turn = 0
                            self.emit({'event': 'remote_cancel_confirmed',
                                       'turn_id': frame.turn_id})
                        await self._notify_credit_changed()
                    elif result.event is SessionEvent.PEER_ERROR:
                        self.playback_pending_turn = 0
                        self._pcm.clear()
                        await self._cancel_reply()
                        if self.reply_session is not None:
                            self.reply_session.finish(frame.turn_id, success=False)
                        await self._notify_credit_changed()
                        if self.session.state is SessionState.CLOSED:
                            await self.websocket.close(code=1000, reason="peer_error")
                        else:
                            self.emit(
                                {"event": "turn_state", "state": SessionState.IDLE.value}
                            )
        except ProtocolError as error:
            await self._fail_closed(error)
        except ConnectionClosed:
            pass
        except asyncio.CancelledError:
            raise
        except Exception:
            await self._internal_failure()
        finally:
            self.volume_percent = None
            if self._volume_future is not None and not self._volume_future.done():
                self._volume_future.set_exception(ProtocolError('device_offline'))
            if self._ota_future is not None and not self._ota_future.done():
                self._ota_future.set_exception(ProtocolError('device_offline'))
            terminal_state = self.session.state.value
            if self.session.state in {
                SessionState.UPLINK,
                SessionState.THINKING,
                SessionState.DOWNLINK,
            }:
                self.metrics.aborted_turns += 1
            await self._cancel_reply()
            self._pcm.clear()
            if self.reply_session is not None:
                self.reply_session.close()
            self.emit(
                {
                    "event": "connection_summary",
                    "terminal_state": terminal_state,
                    "turns_started": self.metrics.turns_started,
                    "turns_completed": self.metrics.turns_completed,
                    "playback_confirmed": self.metrics.playback_confirmed,
                    "turns_cancelled": self.metrics.turns_cancelled,
                    "aborted_turns": self.metrics.aborted_turns,
                    "uplink_frames": self.metrics.uplink_frames,
                    "uplink_bytes": self.metrics.uplink_bytes,
                    "downlink_frames": self.metrics.downlink_frames,
                    "downlink_bytes": self.metrics.downlink_bytes,
                    "protocol_errors": self.metrics.protocol_errors,
                }
            )

    async def _incoming(self) -> AsyncIterator[str | bytes]:
        try:
            first = await asyncio.wait_for(self.websocket.recv(),
                                           timeout=self.config.hello_timeout_seconds)
        except asyncio.TimeoutError as error:
            raise ProtocolError('hello_timeout') from error
        yield first
        while True:
            try:
                wire = await asyncio.wait_for(
                    self.websocket.recv(),
                    timeout=self.config.heartbeat_interval_seconds)
            except asyncio.TimeoutError:
                # WebSocket PING does not reach the device's companion
                # receive loop.  Keep that application session alive too.
                async with self._turn_lock:
                    await self._send(self.session.make_heartbeat())
            else:
                yield wire

    def _account_inbound(self, event: SessionEvent, frame: Frame) -> None:
        if event is SessionEvent.TURN_STARTED:
            if self.playback_pending_turn and self.reply_session is not None:
                self.reply_session.finish(self.playback_pending_turn, success=False)
            self.playback_pending_turn = 0
            self.cancel_pending_turn = 0
            self._pcm.clear()
            self._uplink_bytes = 0
            self.metrics.turns_started += 1
            self.emit({"event": "turn_state", "state": SessionState.UPLINK.value})
        elif event in {SessionEvent.AUDIO_UP, SessionEvent.DISCARDED_AUDIO}:
            self._uplink_bytes += len(frame.payload)
            if self._uplink_bytes > self.config.max_uplink_bytes:
                raise ProtocolError("uplink_too_long")
            if event is SessionEvent.DISCARDED_AUDIO:
                return
            if self.reply_session is not None:
                self._pcm.extend(frame.payload)
            self.metrics.uplink_frames += 1
            self.metrics.uplink_bytes += len(frame.payload)

    def _start_reply(self, turn_id: int, turn_end_time: float) -> None:
        if self._reply_task is not None and not self._reply_task.done():
            raise ProtocolError("reply_already_active")
        self._reply_task = asyncio.create_task(
            self._stream_reply(turn_id, turn_end_time),
            name="shaniu-reply",
        )

    async def _stream_reply(self, turn_id: int, turn_end_time: float) -> None:
        sent = False
        try:
            await asyncio.wait_for(
                self._generate_reply(turn_id, turn_end_time),
                timeout=self.config.reply_timeout_seconds,
            )
            # The generated pair remains pending until the peer confirms
            # playback cleanup. TTS_END only proves transport completion.
            sent = True
        except asyncio.CancelledError:
            raise
        except ProtocolError as error:
            await self._fail_closed(error)
        except ConnectionClosed:
            pass
        except Exception:
            await self._internal_failure()
        finally:
            if not sent and self.reply_session is not None:
                self.reply_session.finish(turn_id, success=False)

    async def _fixed_reply(self) -> AsyncIterator[bytes]:
        for index in range(self.config.reply_frames):
            yield deterministic_pcm_frame(index)

    async def _generate_reply(self, turn_id: int, turn_end_time: float) -> None:
        if self.session.state is not SessionState.THINKING or self.session.turn_id != turn_id:
            return
        pcm = bytes(self._pcm)
        self._pcm.clear()
        source = self.reply_session.reply(pcm) if self.reply_session else self._fixed_reply()
        del pcm
        first_audio = True
        async with contextlib.aclosing(source):
            async with contextlib.aclosing(pcm_frames(source)) as frames:
                async for payload, final in frames:
                    if first_audio:
                        await self._send(self.session.make_tts_start(turn_id))
                    await self._wait_for_downlink_credit(turn_id)
                    if self.session.state is not SessionState.DOWNLINK or self.session.turn_id != turn_id:
                        return
                    await self._send(self.session.make_audio_down(turn_id, payload, final=final))
                    self.metrics.downlink_frames += 1
                    self.metrics.downlink_bytes += len(payload)
                    if first_audio:
                        first_audio = False
                        self.emit({"event": "first_audio", "state": SessionState.DOWNLINK.value,
                                   "latency_ms": int((time.monotonic() - turn_end_time) * 1000)})
                    if self.config.reply_interval_ms:
                        await asyncio.sleep(self.config.reply_interval_ms / 1000)
        self._playback_sent_at = time.monotonic()
        self.playback_pending_turn = turn_id
        await self._send(self.session.make_tts_end(turn_id))
        self.metrics.turns_completed += 1
        self.emit({"event": "turn_state", "state": SessionState.IDLE.value})

    async def _wait_for_downlink_credit(self, turn_id: int) -> None:
        timeout = self.config.downlink_window_timeout_ms / 1_000
        try:
            async with self._credit_changed:
                await asyncio.wait_for(
                    self._credit_changed.wait_for(
                        lambda: self.session.state is not SessionState.DOWNLINK
                        or self.session.turn_id != turn_id
                        or self.session.downlink_credit >= AUDIO_FRAME_BYTES
                    ),
                    timeout=timeout,
                )
        except asyncio.TimeoutError as error:
            raise ProtocolError("downlink_window_timeout") from error

    async def _notify_credit_changed(self) -> None:
        async with self._credit_changed:
            self._credit_changed.notify_all()

    async def _send(self, frame: Frame) -> None:
        await self.websocket.send(frame.encode())

    def _receive_volume_report(self, frame: Frame) -> None:
        sequence, result, volume = struct.unpack('!IiI', frame.payload)
        future = self._volume_future
        if sequence != self._volume_sequence or future is None or future.done():
            return
        if result != 0:
            self.volume_percent = None
            future.set_exception(ProtocolError('volume_device_error'))
        else:
            self.volume_percent = volume
            future.set_result(volume)

    def _receive_ota_report(self, frame: Frame) -> None:
        from .protocol import decode_ota_report

        report = decode_ota_report(frame.payload)
        manifest = self._ota_manifest_sha256
        if (manifest is None or report.request_sequence != self._ota_sequence
                or report.manifest_sha256 != manifest):
            return
        previous = self.ota_status
        if previous is not None:
            # CONFIRMED, ROLLED_BACK and FAILED are final observations for
            # this request.  A delayed peer frame must not rewrite history.
            if previous.phase in {6, 7, 8}:
                return
            if report.phase in range(1, 7):
                if (report.phase < previous.phase
                        or (report.phase == previous.phase
                            and report.progress_percent < previous.progress_percent)):
                    return
        status = OtaStatus(manifest, report.phase, report.progress_percent, report.result)
        self.ota_status = status
        if self.device_id is not None and self.ota_report_sink is not None:
            self.ota_report_sink(self.device_id, self, status)
        future = self._ota_future
        if future is not None and not future.done():
            future.set_result(status)

    async def request_ota(
        self,
        manifest_sha256: str,
        *,
        transaction_id: str | None = None,
        dispatched: Callable[[int, int, int], None] | None = None,
    ) -> OtaStatus:
        async with self._turn_lock:
            # This is a per-attempt fact.  A prior dispatched request must not
            # make an early rejection of this request look uncertain.
            self.ota_request_sent = False
            if self._closing or not self.websocket.open:
                raise ProtocolError('device_offline')
            if (self._ota_future is not None
                    or (self._ota_manifest_sha256 is not None
                        and (self.ota_status is None
                             or self.ota_status.phase not in {6, 7, 8}))):
                raise ProtocolError('ota_busy')
            if self.session.state is not SessionState.IDLE or self.playback_pending_turn:
                raise ProtocolError('ota_busy')
            frame = self.session.make_ota_request(manifest_sha256)
            future = asyncio.get_running_loop().create_future()
            self._ota_sequence = frame.sequence
            self._ota_manifest_sha256 = manifest_sha256
            self.ota_transaction_id = transaction_id
            self._ota_future = future
            self.ota_status = None
            try:
                await self._send(frame)
                self.ota_request_sent = True
                if dispatched is not None:
                    dispatched(self.session.boot_generation,
                               self.session.session_id, frame.sequence)
            except BaseException:
                self._ota_future = None
                if not self.ota_request_sent:
                    self._ota_sequence = 0
                    self._ota_manifest_sha256 = None
                    self.ota_transaction_id = None
                future.cancel()
                raise
        try:
            return await asyncio.wait_for(future, self.config.ota_timeout_seconds)
        except asyncio.TimeoutError as error:
            raise ProtocolError('ota_timeout') from error
        finally:
            if self._ota_future is future:
                self._ota_future = None

    async def request_volume(self, percent: int | None = None) -> int:
        async with self._turn_lock:
            if self._closing or not self.websocket.open:
                raise ProtocolError('device_offline')
            if self._volume_future is not None:
                raise ProtocolError('volume_busy')
            frame = self.session.make_volume_request(percent)
            future = asyncio.get_running_loop().create_future()
            self._volume_sequence = frame.sequence
            self._volume_future = future
            self.volume_percent = None
            try:
                await self._send(frame)
            except BaseException:
                self._volume_future = None
                self._volume_sequence = 0
                future.cancel()
                raise
        try:
            # Release the turn lock so the receive loop can deliver the reply.
            return await asyncio.wait_for(future, self.config.volume_timeout_seconds)
        except asyncio.TimeoutError as error:
            raise ProtocolError('volume_timeout') from error
        finally:
            if self._volume_future is future:
                self._volume_future = None
                self._volume_sequence = 0

    async def cancel_turn(self, turn_id: int) -> None:
        """Stop this turn's provider and send CANCEL; not a playback-stop ACK."""
        async with self._turn_lock:
            if self._closing or not self.websocket.open:
                raise ProtocolError('device_offline')
            if turn_id == 0 or turn_id != self.session.turn_id:
                raise ProtocolError('stale_turn')
            if (self.session.state is SessionState.IDLE and not self.playback_pending_turn
                    and not self.cancel_pending_turn):
                raise ProtocolError('no_active_turn')
            await self._cancel_reply()
            if self.reply_session is not None:
                self.reply_session.finish(turn_id, success=False)
            self._pcm.clear()
            self._cancel_sent_at = time.monotonic()
            self.playback_pending_turn = 0
            self.cancel_pending_turn = turn_id
            await self._send(self.session.make_cancel(turn_id))
            self.metrics.turns_cancelled += 1
            await self._notify_credit_changed()
            self.emit({'event': 'remote_cancel_sent', 'turn_id': turn_id})

    @property
    def cancel_confirmation_timed_out(self) -> bool:
        return bool(self.cancel_pending_turn and time.monotonic() - self._cancel_sent_at
                    >= self.config.cancel_ack_timeout_seconds)

    @property
    def playback_confirmation_timed_out(self) -> bool:
        return bool(self.playback_pending_turn and time.monotonic() - self._playback_sent_at
                    >= self.config.playback_ack_timeout_seconds)

    async def _cancel_reply(self) -> None:
        task = self._reply_task
        self._reply_task = None
        if task is None or task.done():
            if task is not None:
                with contextlib.suppress(asyncio.CancelledError, Exception):
                    await task
            return
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task

    async def _fail_closed(self, error: ProtocolError) -> None:
        if self._closing:
            return
        self._closing = True
        self.metrics.protocol_errors += 1
        self.emit(
            {
                "event": "protocol_error",
                "state": self.session.state.value,
                "code": error.code,
            }
        )
        with contextlib.suppress(ProtocolError, ConnectionClosed):
            outbound = self.session.make_error()
            if outbound is not None:
                await self._send(outbound)
        with contextlib.suppress(ConnectionClosed):
            await self.websocket.close(code=1002, reason=error.code[:123])

    async def _internal_failure(self) -> None:
        if self._closing:
            return
        self._closing = True
        self.emit(
            {
                "event": "internal_error",
                "state": self.session.state.value,
                "code": "internal_error",
            }
        )
        with contextlib.suppress(ConnectionClosed):
            await self.websocket.close(code=1011, reason="internal_error")


class GatewayServer:
    def __init__(
        self,
        config: GatewayConfig | None = None,
        event_sink: EventSink = json_event_sink,
        reply_factory: ReplyFactory | None = None,
        device_bindings: DeviceBindings | None = None,
        device_reply_factory: DeviceReplyFactory | None = None,
        firmware_content: FirmwareContentStore | None = None,
        ota_transactions: OtaTransactionStore | None = None,
        firmware_releases: FirmwareReleases | None = None,
    ) -> None:
        if reply_factory is not None and device_reply_factory is not None:
            raise ValueError('choose one reply factory')
        if device_reply_factory is not None and device_bindings is None:
            raise ValueError('device reply factory requires device bindings')
        if firmware_content is not None and device_bindings is None:
            raise ValueError('firmware content requires device bindings')
        if ota_transactions is not None and device_bindings is None:
            raise ValueError('OTA transactions require device bindings')
        if firmware_releases is not None and device_bindings is None:
            raise ValueError('firmware releases require device bindings')
        self.config = config or GatewayConfig()
        self.event_sink = event_sink
        self.reply_factory = reply_factory
        self.device_reply_factory = device_reply_factory
        self.device_bindings = device_bindings
        self.firmware_content = firmware_content
        self.ota_transactions = ota_transactions
        self.firmware_releases = firmware_releases or FirmwareReleases()
        self._device_connections: dict[str, GatewayConnection] = {}

    def _ota_report(self, device_id: str, connection: GatewayConnection,
                    status: OtaStatus) -> None:
        store = self.ota_transactions
        transaction_id = connection.ota_transaction_id
        if store is None or transaction_id is None:
            return
        row = store.load(device_id)
        identity = (connection.session.boot_generation,
                    connection.session.session_id, connection._ota_sequence)
        if (row is None or row.transaction_id != transaction_id
                or row.manifest_sha256 != status.manifest_sha256
                or (row.dispatch_boot_generation, row.dispatch_session_id,
                    row.dispatch_sequence) != identity):
            raise OtaTransactionError('OTA report does not match durable dispatch')
        release = next((item for item in self.firmware_releases.releases
                        if item.device_id == device_id
                        and item.manifest_sha256 == row.manifest_sha256
                        and item.target_version == row.target_version), None)
        if release is None:
            raise OtaTransactionError('OTA report release is no longer registered')
        observed = connection.device_status
        if status.phase in {6, 7}:
            expected_version = (release.target_version if status.phase == 6
                                else release.required_source_version)
            if (observed is None or observed.firmware_version != expected_version
                    or observed.firmware_root_sha256 !=
                       release.required_source_root_sha256):
                # Persist the observation before waiting for a post-boot status;
                # disconnect or Gateway restart must never cause a completed
                # image to be dispatched again.
                pending = 'confirming' if status.phase == 6 else 'rollback_check'
                store.advance(device_id, transaction_id, pending,
                              status.progress_percent, status.result,
                              time.time_ns() // 1_000_000)
                return
        store.advance(device_id, transaction_id, _OTA_STATE_BY_PHASE[status.phase],
                      status.progress_percent,
                      status.result if status.phase in {6, 7, 8} else None,
                      time.time_ns() // 1_000_000)

    def _firmware_release(self, device_id: str, manifest: str,
                          target: str):
        return next((item for item in self.firmware_releases.releases
                     if item.device_id == device_id
                     and item.manifest_sha256 == manifest
                     and item.target_version == target), None)

    def _status_report(self, device_id: str,
                       connection: GatewayConnection) -> None:
        store = self.ota_transactions
        if store is None:
            return
        row = store.load(device_id)
        if row is not None and row.state in {'confirming', 'rollback_check'}:
            release = self._firmware_release(
                device_id, row.manifest_sha256, row.target_version)
            expected = (None if release is None else release.target_version
                        if row.state == 'confirming'
                        else release.required_source_version)
            status = connection.device_status
            if (release is None or status is None
                    or status.firmware_version != expected
                    or status.firmware_root_sha256 !=
                       release.required_source_root_sha256):
                store.advance(device_id, row.transaction_id, 'orphaned',
                              row.progress_percent, 0,
                              time.time_ns() // 1_000_000)
                raise OtaTransactionError(
                    'terminal OTA does not match post-boot firmware')
            terminal = ('confirmed' if row.state == 'confirming'
                        else 'rolled_back')
            store.advance(device_id, row.transaction_id,
                          terminal, row.progress_percent, row.result,
                          time.time_ns() // 1_000_000)
            return
        self._schedule_ota_reconcile(device_id, connection)

    @staticmethod
    def _ota_idle(connection: GatewayConnection) -> bool:
        return (connection.session.state is SessionState.IDLE
                and not connection.playback_pending_turn
                and not connection.cancel_pending_turn
                and (connection._reply_task is None
                     or connection._reply_task.done()))

    def _schedule_ota_reconcile(self, device_id: str,
                                connection: GatewayConnection) -> None:
        store = self.ota_transactions
        if (store is None or connection.ota_reconcile_attempted
                or not connection.session.capabilities & CAP_OTA
                or not self._ota_idle(connection)):
            return
        row = store.load(device_id)
        if row is None or row.state != 'uncertain':
            return
        release = self._firmware_release(
            device_id, row.manifest_sha256, row.target_version)
        status = connection.device_status
        if (release is None or status is None
                or status.firmware_version not in {
                    release.required_source_version, release.target_version}
                or status.firmware_root_sha256 !=
                   release.required_source_root_sha256):
            store.advance(device_id, row.transaction_id, 'orphaned',
                          row.progress_percent, 0,
                          time.time_ns() // 1_000_000)
            return
        connection.ota_reconcile_attempted = True
        connection.ota_reconcile_task = asyncio.create_task(
            self._resume_ota(device_id, connection, row.transaction_id,
                             row.manifest_sha256),
            name='shaniu-ota-reconcile')

    async def _resume_ota(self, device_id: str, connection: GatewayConnection,
                          transaction_id: str, manifest: str) -> None:
        store = self.ota_transactions
        assert store is not None
        try:
            await connection.request_ota(
                manifest, transaction_id=transaction_id,
                dispatched=lambda boot, session, sequence: store.redispatch(
                    device_id, transaction_id, boot, session, sequence,
                    time.time_ns() // 1_000_000))
        except (ProtocolError, ConnectionClosed, OtaTransactionError):
            if connection.ota_request_sent:
                with contextlib.suppress(OtaTransactionError):
                    store.mark_uncertain(device_id, transaction_id,
                                         time.time_ns() // 1_000_000)
            else:
                connection.ota_reconcile_attempted = False
            self.event_sink({'event': 'ota_reconcile', 'state': 'deferred'})

    def _ota_disconnected(self, device_id: str,
                          connection: GatewayConnection) -> None:
        store = self.ota_transactions
        transaction_id = connection.ota_transaction_id
        if store is None or transaction_id is None:
            return
        try:
            store.mark_uncertain(device_id, transaction_id,
                                 time.time_ns() // 1_000_000)
        except OtaTransactionError:
            self.event_sink({'event': 'ota_state_error', 'code': 'disconnect_state'})

    def device_connection(self, device_id: str) -> GatewayConnection | None:
        connection = self._device_connections.get(device_id)
        if (connection is None or connection._closing
                or not connection.websocket.open
                or not connection.session.identity_bound):
            return None
        return connection

    async def handle(self, websocket: WebSocketServerProtocol, path: str) -> None:
        device_id = None
        if self.device_bindings is not None:
            tls = websocket.transport.get_extra_info('ssl_object')
            certificate = (tls.getpeercert(binary_form=True)
                           if tls is not None and tls.context.verify_mode == ssl.CERT_REQUIRED
                           else None)
            if certificate:
                device_id = self.device_bindings.device_for_certificate(
                    hashlib.sha256(certificate).hexdigest())
            if device_id is None:
                await websocket.close(code=1008, reason='device_not_registered')
                return
            if device_id in self._device_connections:
                await websocket.close(code=1008, reason='device_already_connected')
                return
        reply_factory = self.reply_factory
        if self.device_reply_factory is not None:
            if device_id is None:
                await websocket.close(code=1008, reason='device_not_registered')
                return
            reply_factory = lambda: self.device_reply_factory(device_id)
        connection = GatewayConnection(
            websocket, path, self.config, self.event_sink, reply_factory,
            device_id, self._ota_report, self._status_report,
        )
        if device_id is not None:
            self._device_connections[device_id] = connection
        try:
            await connection.run()
        finally:
            task = connection.ota_reconcile_task
            if task is not None and not task.done():
                task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await task
            if (device_id is not None
                    and self._device_connections.get(device_id) is connection):
                del self._device_connections[device_id]
            if device_id is not None:
                self._ota_disconnected(device_id, connection)


class FirmwareContentProtocol(WebSocketServerProtocol):
    """Serve only authenticated OTA members before the WebSocket upgrade."""

    def __init__(self, *args, gateway: GatewayServer, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._gateway = gateway

    async def process_request(self, path, request_headers):
        store = self._gateway.firmware_content
        if store is None:
            return None
        return store.process_request(self, path, request_headers, self._gateway)


async def pcm_frames(source: AsyncIterator[bytes]) -> AsyncIterator[tuple[bytes, bool]]:
    """Keep one frame for EOS; only the downlink's last frame gets silence padding."""
    pending = bytearray()
    total = 0
    async for chunk in source:
        if not isinstance(chunk, bytes) or len(chunk) > 256 * 1024:
            raise ProtocolError("invalid_reply_chunk")
        total += len(chunk)
        if total > 90 * 16000 * 2:
            raise ProtocolError("reply_too_long")
        pending.extend(chunk)
        while len(pending) > AUDIO_FRAME_BYTES:
            payload = bytes(pending[:AUDIO_FRAME_BYTES])
            del pending[:AUDIO_FRAME_BYTES]
            yield payload, False
    if not pending or len(pending) % 2:
        raise ProtocolError("invalid_reply_audio")
    yield bytes(pending).ljust(AUDIO_FRAME_BYTES, b"\0"), True


def deterministic_pcm_frame(frame_index: int) -> bytes:
    """Generate one 20 ms 16 kHz mono S16LE tone frame without WAV buffering."""

    if frame_index < 0:
        raise ValueError("frame_index must be non-negative")
    payload = bytearray(AUDIO_FRAME_BYTES)
    first_sample = frame_index * (AUDIO_FRAME_BYTES // 2)
    for offset in range(0, AUDIO_FRAME_BYTES, 2):
        sample_index = first_sample + offset // 2
        sample = 1_400 if ((sample_index * 440) // 16_000) % 2 == 0 else -1_400
        struct.pack_into("<h", payload, offset, sample)
    return bytes(payload)


def build_tls_context(
    cert_path: Path, key_path: Path, client_ca: Path | None = None
) -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certfile=cert_path, keyfile=key_path)
    if client_ca is not None:
        context.load_verify_locations(cafile=client_ca)
        context.verify_mode = ssl.CERT_REQUIRED
    return context


def _bind_scope(host: str, client_ca: Path | None) -> str:
    if host in _LOOPBACK_HOSTS:
        return "loopback"

    if client_ca is None:
        raise ValueError("non-loopback binds require a client CA")

    try:
        address = ipaddress.ip_address(host)
    except ValueError as error:
        raise ValueError("non-loopback bind must be a literal IP address") from error

    if (address.is_loopback or address.is_unspecified or address.is_multicast
            or address.is_reserved or address == ipaddress.ip_address(
                "255.255.255.255")):
        raise ValueError("bind must be a unicast non-loopback IP address")

    return "mtls_unicast"


async def start_gateway_server(
    gateway: GatewayServer,
    host: str,
    port: int,
    tls_context: ssl.SSLContext,
    client_ca: Path | None = None,
) -> WebSocketServer:
    if tls_context is None:
        raise ValueError("TLS context is required")
    scope = _bind_scope(host, client_ca)
    if (gateway.device_bindings is not None
            and (client_ca is None or tls_context.verify_mode != ssl.CERT_REQUIRED)):
        raise ValueError("device bindings require verified mutual TLS")
    if tls_context.minimum_version < ssl.TLSVersion.TLSv1_2:
        raise ValueError("TLS 1.2 or newer is required")
    if scope != "loopback" and tls_context.verify_mode != ssl.CERT_REQUIRED:
        raise ValueError("non-loopback TLS context must require client certificates")
    if scope != "loopback" and gateway.device_bindings is None:
        raise ValueError("non-loopback gateway requires registered device bindings")

    quiet_logger = logging.getLogger("shaniu_gateway.websocket")
    quiet_logger.setLevel(logging.CRITICAL)
    return await websockets.serve(
        gateway.handle,
        host,
        port,
        ssl=tls_context,
        subprotocols=[SUBPROTOCOL],
        compression=None,
        max_size=HEADER_BYTES + MAX_PAYLOAD,
        max_queue=4,
        ping_interval=20,
        ping_timeout=20,
        close_timeout=2,
        logger=quiet_logger,
        create_protocol=lambda *args, **kwargs: FirmwareContentProtocol(
            *args, gateway=gateway, **kwargs),
    )


async def _run(args: argparse.Namespace) -> None:
    console_options = (args.console_port, args.console_access, args.console_state)
    if any(option is not None for option in console_options):
        if (not all(option is not None for option in console_options)
                or args.device_bindings is None or args.provider != 'mimo'
                or not 1 <= args.console_port <= 65535 or args.console_port == args.port):
            raise ValueError('console requires its own port, grants, state, device bindings and MiMo')
    if args.firmware_releases is not None and args.console_port is None:
        raise ValueError('firmware releases require the authenticated console')
    if (args.firmware_releases is None) != (args.ota_state is None):
        raise ValueError('firmware releases require a durable OTA state file')
    if args.firmware_package and (
            args.firmware_releases is None or args.console_port is None
            or args.device_bindings is None):
        raise ValueError('firmware packages require releases, console and device bindings')
    memory_devices = tuple(args.memory_device)
    if ((args.memory_state is None) != (not memory_devices)
            or len(set(memory_devices)) != len(memory_devices)):
        raise ValueError('long-term memory requires a state file and unique device opt-ins')
    if args.memory_state is not None and (
            args.console_port is None or args.provider != 'mimo'
            or args.device_bindings is None):
        raise ValueError('long-term memory requires console, MiMo and device bindings')
    config = GatewayConfig(
        path=args.path,
        reply_frames=args.reply_frames,
        reply_interval_ms=args.reply_interval_ms,
        downlink_window_timeout_ms=args.window_timeout_ms,
    )
    bindings = load_device_bindings(args.device_bindings) if args.device_bindings else None
    if memory_devices and (
            bindings is None
            or any(not bindings.contains_device(device) for device in memory_devices)):
        raise ValueError('memory opt-ins must name registered devices')
    provider = None
    mimo_provider = None
    if args.provider == "mimo":
        from .mimo import MiMoConfig, MiMoProvider, load_api_key
        if args.mimo_key_file is None or args.mimo_tts_rate is None:
            raise ValueError("MiMo requires a key file and verified TTS PCM rate")
        if args.mimo_tts_rate != 16000 and shutil.which("ffmpeg") is None:
            raise ValueError("FFmpeg is required for TTS resampling")
        mimo_provider = MiMoProvider(MiMoConfig(
            api_key=load_api_key(args.mimo_key_file), tts_sample_rate=args.mimo_tts_rate,
            base_url=args.mimo_base_url, chat_model=args.mimo_chat_model,
            persona_mode=args.persona_mode,
        ))
        provider = mimo_provider.new_conversation
    tls_context = build_tls_context(args.cert, args.key, args.client_ca)
    scope = _bind_scope(args.host, args.client_ca)
    memory_store = (MemoryStore(args.memory_state, memory_devices)
                    if args.memory_state is not None else None)
    device_provider = None
    if memory_store is not None:
        assert mimo_provider is not None

        def device_provider(device_id: str) -> ReplySession:
            memory = (memory_store.session(device_id)
                      if memory_store.configured(device_id) else None)
            return mimo_provider.new_conversation(memory)

        provider = None
    firmware_releases = None
    firmware_content = None
    ota_transactions = None
    try:
        if args.firmware_releases is not None:
            from .firmware import load_firmware_releases
            firmware_releases = load_firmware_releases(args.firmware_releases)
        if args.firmware_package:
            assert firmware_releases is not None
            firmware_content = FirmwareContentStore(firmware_releases,
                                                    tuple(args.firmware_package))
        if args.ota_state is not None:
            ota_transactions = OtaTransactionStore(args.ota_state)
            ota_transactions.mark_nonterminal_uncertain(
                time.time_ns() // 1_000_000)
    except BaseException:
        if memory_store is not None:
            memory_store.close()
        if firmware_content is not None:
            firmware_content.close()
        if ota_transactions is not None:
            ota_transactions.close()
        raise
    gateway = GatewayServer(
        config, reply_factory=provider, device_bindings=bindings,
        device_reply_factory=device_provider,
        firmware_content=firmware_content,
        ota_transactions=ota_transactions,
        firmware_releases=firmware_releases,
    )
    try:
        server = await start_gateway_server(
            gateway, args.host, args.port, tls_context, args.client_ca
        )
    except BaseException:
        if memory_store is not None:
            memory_store.close()
        if firmware_content is not None:
            firmware_content.close()
        if ota_transactions is not None:
            ota_transactions.close()
        raise
    console_runner = None
    try:
        if args.console_port is not None:
            from aiohttp import web
            from .console import ConsoleService, load_console_grants
            from .firmware import FirmwareReleases
            service = ConsoleService(gateway, load_console_grants(args.console_access),
                                     args.console_state,
                                     (firmware_releases
                                      if firmware_releases is not None
                                      else FirmwareReleases()),
                                     memory_store=memory_store,
                                     ota_transactions=ota_transactions)
            console_runner = web.AppRunner(service.app, access_log=None)
            await console_runner.setup()
            site = web.TCPSite(console_runner, args.host, args.console_port,
                              ssl_context=build_tls_context(args.cert, args.key))
            await site.start()
        gateway.event_sink(
            {'event': 'server_started', 'transport': 'wss', 'scope': scope,
             'port': args.port, 'console_port': args.console_port})
        await server.wait_closed()
    finally:
        try:
            if console_runner is not None:
                await console_runner.cleanup()
        finally:
            server.close()
            await server.wait_closed()
            if memory_store is not None:
                memory_store.close()
            if firmware_content is not None:
                firmware_content.close()
            if ota_transactions is not None:
                ota_transactions.close()


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Shaniu companion-v1 WSS Gateway"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--path", default=DEFAULT_PATH)
    parser.add_argument("--cert", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--client-ca", type=Path)
    parser.add_argument("--device-bindings", type=Path,
                        help="operator-owned device ID / client certificate pin registry")
    parser.add_argument('--console-port', type=int)
    parser.add_argument('--console-access', type=Path,
                        help='operator-provisioned per-device console token hash grants')
    parser.add_argument('--console-state', type=Path,
                        help='private persistent console generation counter database')
    parser.add_argument('--firmware-releases', type=Path,
                        help='operator-verified metadata-only OTA release registry')
    parser.add_argument('--ota-state', type=Path,
                        help='private durable OTA transaction database')
    parser.add_argument('--firmware-package', type=Path, action='append', default=[],
                        metavar='SIGNED_OTA_BKPACK',
                        help='verified signed OTA package; may be repeated')
    parser.add_argument('--memory-state', type=Path,
                        help='private long-term conversation memory database')
    parser.add_argument('--memory-device', action='append', default=[], metavar='DEVICE_ID',
                        help='explicitly opt one registered device into long-term memory')
    parser.add_argument("--reply-frames", type=int, default=10)
    parser.add_argument("--reply-interval-ms", type=int, default=20)
    parser.add_argument("--window-timeout-ms", type=int, default=2_000)
    parser.add_argument("--provider", choices=("fixed", "mimo"), default="fixed")
    from .mimo import PERSONA_MODES
    parser.add_argument("--persona-mode", choices=PERSONA_MODES, default="gentle")
    parser.add_argument("--mimo-key-file", type=Path)
    parser.add_argument("--mimo-base-url", default="https://token-plan-cn.xiaomimimo.com/v1")
    parser.add_argument("--mimo-chat-model", choices=("mimo-v2.5", "mimo-v2.5-pro"), default="mimo-v2.5")
    parser.add_argument("--mimo-tts-rate", type=int,
                        help="verified provider mono PCM16 sample rate; no assumed default")
    return parser


def main() -> int:
    args = _parser().parse_args()
    try:
        asyncio.run(_run(args))
    except KeyboardInterrupt:
        return 0
    except (OSError, ssl.SSLError, ValueError, sqlite3.Error, MemoryStateError,
            OtaTransactionError):
        json_event_sink({"event": "server_error", "code": "startup_failed"})
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
